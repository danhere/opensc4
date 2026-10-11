// The application's side of a city save (docs/save-format.md), part 2:
// cSC4App::LoadCity 0x004555F0 and the reader of the application's record
// 0x00454620.
//
// The application object has its main vtable at +0 (0x00A86C2C), a message
// target at +4 and the interface cISC4App at +0xC (vtable 0x00A86AF0); the
// global 0x00B43C94 holds the interface (see ui_app_4.cpp). LoadCity is
// slot 13 of the interface, so its `this` is object + 0xC; the record
// reader is a helper and gets the object itself.
//
// What LoadCity does, in order:
//
//   - closes the open city (interface slot 14) and keeps the holder of the
//     regional city it was given;
//   - makes a save segment (cSC4DBSegmentPackedFile, 0x1DC bytes), gives it
//     the path and opens it for reading; a file that does not open ends the
//     load there;
//   - reads the file's version (0x3E to 0x41, else the load fails);
//   - sends message 0x26C63340 (the segment, a copy of the path), keeps the
//     segment as the city's open file and gives it to the resource manager;
//   - has the segment load every object (segment slot 0x1C);
//   - opens record 0x49DD6F4C / 0x49DD6F4E / 1, the application's, which
//     must be version 1, and reads it (0x00454620): the city by reference
//     and the regional city;
//   - gives the city the regional city's serial number and the path, drops
//     the screen and shows the city view;
//   - sends 0x26C63341 and 0x26C63342, then the messages around the city's
//     Init as MakeCity does (0x26D31EC0 sent before; 0x2B96B3EA, 0x26D31EC1,
//     0xAB96B05F and 0xEA8AE29A queued after);
//   - on any failure after the first message: the city is released and the
//     file taken out of the resource manager and closed (0x00454A10);
//   - always at the end: both message pumps of the application run (100,
//     500, 5000) and the resource manager collects its garbage.
//
// Replacement, off by default:
//
//   saves.app.part2: the record reader 0x00454620.
//
// LoadCityOurs below is LoadCity itself, with the game's signature. It is
// not hooked here: other features detour cSC4App::LoadCity (game_seed.cpp,
// trace.cpp, game_timelapse.cpp), so whoever owns that address connects it.
//
// Neither function has an exception frame: nothing is undone when a Read
// throws through them.
//
// The application, as far as this part touches it (offsets from the
// object): +0x50 the main window, +0x58 the holder of the regional city (a
// pointer to a counted pointer, the caller's), +0x5C the city, +0x654 the
// city's open file (a cIGZPersistDBSegment, counted).
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace opensc4::saves {

bool __fastcall LoadCityOurs(void* self, void* edx, void* path, void* holder);

namespace {

namespace app { // offsets from the object
constexpr std::size_t kInterface = 0x0C;
constexpr std::size_t kRegionalCity = 0x58;
constexpr std::size_t kCity = 0x5C;
} // namespace app

namespace iface { // the same fields from the interface (object + 0xC)
constexpr std::size_t kMainWindow = 0x44;
constexpr std::size_t kRegionalCity = 0x4C;
constexpr std::size_t kCity = 0x50;
constexpr std::size_t kCityFile = 0x648;
// cISC4App, by vtable offset
constexpr std::size_t kRunMessageServerPump = 0x10;  // (least, most, time)
constexpr std::size_t kRunMessageServer2Pump = 0x14; // (least, most, time)
constexpr std::size_t kCloseCity = 0x38;
} // namespace iface

namespace unknown { // cIGZUnknown, by vtable offset
constexpr std::size_t kAddRef = 0x4;
constexpr std::size_t kRelease = 0x8;
} // namespace unknown

namespace sc4segment { // cISC4DBSegment (the save segment's +0), by vtable offset
constexpr std::size_t kAsDBSegment = 0xC;
constexpr std::size_t kLoadClassObjects = 0x1C;
} // namespace sc4segment

namespace dbsegment { // cIGZPersistDBSegment, by vtable offset
constexpr std::size_t kInit = 0x10;
constexpr std::size_t kOpen = 0x18;    // (read, write)
constexpr std::size_t kSetPath = 0x2C; // (path)
} // namespace dbsegment

namespace message { // the framework's message, by vtable offset
constexpr std::size_t kSetType = 0x14;
constexpr std::size_t kSetVoid1 = 0x30;
constexpr std::size_t kSetVoid2 = 0x44;
} // namespace message

namespace server { // the message server, by vtable offset
constexpr std::size_t kSend = 0xC;
} // namespace server

namespace resources { // the resource manager, by vtable offset
constexpr std::size_t kRegisterSegment = 0x70;
constexpr std::size_t kCollectGarbage = 0xAC;
} // namespace resources

namespace city { // cISC4City, by vtable offset
constexpr std::size_t kInit = 0xC;
constexpr std::size_t kSetSerialNumber = 0x18;
constexpr std::size_t kSetSavePath = 0x2C;
constexpr std::size_t kSlot15 = 0x3C; // (0)
} // namespace city

namespace regional { // cSC4RegionalCity, by vtable offset
constexpr std::size_t kShutdown = 0x10;
constexpr std::size_t kGetSerialNumber = 0x6C;
} // namespace regional

constexpr std::size_t kGetUint16 = 0x1C; // cIGZIStream

constexpr uint32_t kOldestVersion = 0x3E;
constexpr uint32_t kCurrentVersion = 0x41;

// The application's record in a city file, and the version it must have.
constexpr uint32_t kAppRecordType = 0x49DD6F4C;
constexpr uint32_t kAppRecordGroup = 0x49DD6F4E;
constexpr uint32_t kAppRecordInstance = 1;
constexpr uint16_t kAppRecordHolderVersion = 1;
constexpr uint16_t kAppRecordVersion = 2;

constexpr uint32_t kCityIid = 0x66BA4DB5;

// The messages of a load.
constexpr uint32_t kLoadBegins = 0x26C63340;
constexpr uint32_t kLoadRead = 0x26C63341;
constexpr uint32_t kLoadEnds = 0x26C63342;
constexpr uint32_t kCityBeforeInit = 0x26D31EC0;
constexpr uint32_t kCityAfterInit1 = 0x2B96B3EA;
constexpr uint32_t kCityAfterInit2 = 0x26D31EC1;
constexpr uint32_t kCityAfterInit3 = 0xAB96B05F;
constexpr uint32_t kCityAfterInit4 = 0xEA8AE29A;

// The segment input stream holder of save_streams_1.cpp.
struct SegmentStream {
    std::uintptr_t vtable;
    void* segment;
    void* stream;
    uint16_t version;
    uint16_t unused;
};
static_assert(sizeof(SegmentStream) == 0x10);

// The scope that 0x005F8630 starts and 0x005F90D0 ends: a number of
// milliseconds and a stopwatch at +8. 0x20 bytes: in LoadCity's frame it
// runs from +0x24 of the locals to their end at +0x44.
struct TimedScope {
    uint32_t words[8];
};
static_assert(sizeof(TimedScope) == 0x20);

// The scope that 0x00891D9A starts and 0x00891DCA ends: one byte, in a slot
// of four.
struct SmallScope {
    uint8_t on;
    uint8_t unused[3];
};
static_assert(sizeof(SmallScope) == 0x4);

template <typename R, typename... Args>
R This(std::uintptr_t function, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(function)(self, args...);
}

template <typename R, typename... Args>
R Cdecl(std::uintptr_t function, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(function)(args...);
}

uint32_t AddRef(void* object)
{
    return Virtual<uint32_t>(object, unknown::kAddRef);
}

uint32_t Release(void* object)
{
    return Virtual<uint32_t>(object, unknown::kRelease);
}

// A framework message, not yet counted (null when there is no memory).
void* NewMessage()
{
    void* const memory = Cdecl<void*>(addr::RZ_operator_new, uint32_t{0x2C});
    return memory ? This<void*>(addr::cGZMessage_cGZMessage, memory) : nullptr;
}

// The three messages of a load carry the segment and the copy of the path.
void SendLoadMessage(void* messageServer, void* message, uint32_t type, void* part, void* pathCopy)
{
    Virtual<void>(message, message::kSetType, type);
    Virtual<void>(message, message::kSetVoid1, part);
    Virtual<void>(message, message::kSetVoid2, pathCopy);
    Virtual<uint8_t>(messageServer, server::kSend, message);
}

// The game's 0x00454620, thiscall (stream, segment), ret 8; `this` is the
// application object. Reads the application's record of a city file: a
// 16-bit version that must be 2, then the city by reference (interface
// 0x66BA4DB5) straight into the application's city pointer, whatever was
// there. False when the version is another or the reference does not read.
// Then the regional city is read from the segment's own record
// (0x005DC070): when that works, the regional city in the caller's holder
// is shut down (slot 0x10) and the new one takes its place; when it does
// not, the holder keeps what it had and the answer is true all the same.
uint8_t __fastcall ReadAppRecord(char* self, void*, void* stream, void* part)
{
    // The original reads the version into the slot of its first argument.
    uint32_t version = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(stream));
    Virtual<uint8_t>(stream, kGetUint16, &version);
    if (static_cast<uint16_t>(version) != kAppRecordVersion)
        return 0;
    if (!Cdecl<uint8_t>(addr::SC4Stream_ReadReference, stream, kCityIid,
                        reinterpret_cast<void**>(self + app::kCity), uint32_t{0}))
        return 0;

    void* read = nullptr;
    if (Cdecl<uint8_t>(addr::SC4RegionalCity_ReadFromSegment, part, &read)) {
        if (void* const old = *At<void**>(self, app::kRegionalCity))
            Virtual<uint8_t>(old, regional::kShutdown);
        This<void*>(addr::SC4RefPtr_Assign, At<void*>(self, app::kRegionalCity), read);
        Release(read);
    }
    return 1;
}

} // namespace

// The game's 0x004555F0, cSC4App::LoadCity: slot 13 of the interface
// cISC4App, thiscall (path, holder of the regional city), ret 8; `this` is
// the interface (object + 0xC). The path is the game's string object, the
// holder a pointer to a counted pointer to a regional city, which the
// application keeps (not counted) and whose city the record reader may
// replace. True when the city was loaded and its Init ran; see the head of
// this file for the steps. AL is the answer; the rest of EAX is not set by
// the original.
bool __fastcall LoadCityOurs(void* self, void*, void* path, void* holder)
{
    uint8_t result = 0;
    if (At<void*>(self, iface::kCity))
        Virtual<uint8_t>(self, iface::kCloseCity);
    At<void*>(self, iface::kRegionalCity) = holder;

    SmallScope scope;
    This<void*>(addr::SC4Scope891D9A_Construct, &scope, uint32_t{0xF});
    TimedScope timed;
    This<void*>(addr::SC4TimedScope_Construct, &timed, uint32_t{0x3E8});

    void* const segmentMemory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x1DC});
    void* const segment = segmentMemory ? This<void*>(addr::cSC4DBSegmentPackedFile_Construct, segmentMemory) : nullptr;
    void* const part = Virtual<void*>(segment, sc4segment::kAsDBSegment);
    AddRef(part);
    Virtual<uint8_t>(part, dbsegment::kInit);
    Virtual<uint8_t>(part, dbsegment::kSetPath, path);

    if (Virtual<uint8_t>(part, dbsegment::kOpen, uint32_t{1}, uint32_t{0})) {
        const uint32_t version = Cdecl<uint32_t>(addr::SC4CityFiles_GetVersion, part);
        bool closeFile = true;
        if (version >= kOldestVersion && version <= kCurrentVersion) {
            // A copy of the path for the messages.
            void* const stringMemory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x14});
            void* const pathCopy = stringMemory
                ? This<void*>(addr::SC4String_Substring, stringMemory, path, uint32_t{0}, uint32_t{0xFFFFFFFF})
                : nullptr;
            AddRef(pathCopy);

            void* const messageServer = Global<void*>(addr::SC4_MessageServer);
            void* const begins = NewMessage();
            if (begins)
                AddRef(begins);
            SendLoadMessage(messageServer, begins, kLoadBegins, part, pathCopy);

            At<void*>(self, iface::kCityFile) = part;
            AddRef(part);
            Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), resources::kRegisterSegment, part);

            if (Virtual<uint8_t>(segment, sc4segment::kLoadClassObjects)) {
                SegmentStream stream;
                This<void*>(addr::SC4SegmentIStream_SC4SegmentIStream, &stream, part, kAppRecordType, kAppRecordGroup,
                            kAppRecordInstance);
                if (This<uint8_t>(addr::SC4SegmentStream_IsOpen, &stream) && stream.version == kAppRecordHolderVersion
                    && ReadAppRecord(static_cast<char*>(self) - app::kInterface, nullptr, stream.stream, part)) {
                    // The serial number the region knows the city by. The
                    // original takes the city's vtable before it asks the
                    // regional city, and the city pointer after.
                    void* const regionalCity = *At<void**>(self, iface::kRegionalCity);
                    void** const cityVtable = *At<void***>(self, iface::kCity);
                    const uint32_t serial = Virtual<uint32_t>(regionalCity, regional::kGetSerialNumber);
                    reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(
                        cityVtable[city::kSetSerialNumber / sizeof(void*)])(At<void*>(self, iface::kCity), serial);
                    Virtual<void>(At<void*>(self, iface::kCity), city::kSetSavePath, path);
                    Virtual<void>(At<void*>(self, iface::kCity), city::kSlot15, uint32_t{0});

                    This<uint8_t>(addr::SC4MainWindow_DropScreen, At<void*>(self, iface::kMainWindow));
                    This<uint8_t>(addr::SC4UI_F4E0DD0, At<void*>(self, iface::kMainWindow));

                    void* read = nullptr;
                    This<void**>(addr::SC4RefHolder_SC4RefHolder, &read, NewMessage());
                    SendLoadMessage(messageServer, read, kLoadRead, part, pathCopy);

                    void* ends = nullptr;
                    This<void**>(addr::SC4RefHolder_SC4RefHolder, &ends, NewMessage());
                    SendLoadMessage(messageServer, ends, kLoadEnds, part, pathCopy);

                    char* const object = static_cast<char*>(self) - app::kInterface;
                    This<void>(addr::cSC4App_CityMessage, object, At<void*>(self, iface::kCity), kCityBeforeInit,
                               uint32_t{0}, uint32_t{0}, uint32_t{0});
                    Virtual<uint8_t>(At<void*>(self, iface::kCity), city::kInit);
                    This<void>(addr::cSC4App_CityMessage, object, At<void*>(self, iface::kCity), kCityAfterInit1,
                               uint32_t{0}, uint32_t{0}, uint32_t{1});
                    This<void>(addr::cSC4App_CityMessage, object, At<void*>(self, iface::kCity), kCityAfterInit2,
                               uint32_t{0}, uint32_t{0}, uint32_t{1});
                    This<void>(addr::cSC4App_CityMessage, object, At<void*>(self, iface::kCity), kCityAfterInit3,
                               uint32_t{0}, uint32_t{0}, uint32_t{1});
                    This<void>(addr::cSC4App_CityMessage, object, At<void*>(self, iface::kCity), kCityAfterInit4,
                               uint32_t{0}, uint32_t{0}, uint32_t{1});
                    result = 1;
                    Release(ends);
                    Release(read);
                }
                This<void>(addr::SC4SegmentIStream_Close, &stream);
            }

            Release(pathCopy);
            if (!result) {
                if (void* const loaded = At<void*>(self, iface::kCity)) {
                    Release(loaded);
                    At<void*>(self, iface::kCity) = nullptr;
                }
            }
            Release(begins);
            closeFile = !result;
        }
        if (closeFile)
            This<void>(addr::SC4UI_F454A10, static_cast<char*>(self) - app::kInterface);
    }
    Release(part);

    if (void* const application = Global<void*>(addr::SC4_Service43C94)) {
        AddRef(application);
        Virtual<uint8_t>(application, iface::kRunMessageServerPump, uint32_t{0x64}, uint32_t{0x1F4}, uint32_t{0x1388});
        Virtual<uint8_t>(application, iface::kRunMessageServer2Pump, uint32_t{0x64}, uint32_t{0x1F4}, uint32_t{0x1388});
        Release(application);
    }
    if (void* const resourceManager = Global<void*>(addr::SC4_ResourceManager))
        Virtual<void>(resourceManager, resources::kCollectGarbage);

    This<void>(addr::SC4TimedScope_Destruct, &timed);
    This<void>(addr::SC4Scope891D9A_Destruct, &scope);
    return result != 0;
}

namespace {

bool InstallSaveApp2(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[1] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F454620, reinterpret_cast<void*>(&ReadAppRecord)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    static_assert(count <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < count; ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].first);
        if (!found || !hook::Install(*found, hooks[i].second, &originals[i]))
            return false;
    }
    return true;
}

const bool g_saveApp2Registered = registry::Register({
    "saves.app.part2",
    "saves",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F454620,
    "Our reader of the application's record in a city file: the city by reference and the regional city "
    "(docs/save-format.md)",
    false,
    InstallSaveApp2,
});

} // namespace
} // namespace opensc4::saves
