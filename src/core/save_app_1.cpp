// The application's side of saving a city: cSC4App::SaveCity 0x00455E20 and
// its helpers. Third piece of the save code, part 1.
//
// The application object has its main vtable at +0 (0x00A86C2C) and the
// interface cISC4App at +0xC (0x00A86AF0); the global 0x00B43C94 holds the
// interface. The two SaveCity slots get the interface as `this`, the three
// helpers the object (interface - 0xC).
//
// What SaveCity (interface slot 15: path, fast) does, in order:
//
//   1. Guards for the length of the call: a scope of the framework
//      (0x00891D9A with 0xF), a timed scope of 1000 ms, the modal guard
//      (0, 1, 1, 1) and a guard that switches the system keys off
//      (0x005F88A0). With no city nothing else happens and the answer is
//      false.
//   2. The paths. The city's name (city slot 12) and serial number (slot 5)
//      give the real path through MakeSavePath: the file is always written
//      under the name the region gives the city, NOT under the path passed
//      in. The path passed in only matters at the end: when it differs from
//      the real path (0x00454BB0), the file at it is removed. The city's
//      old path (slot 10) is kept for the failure case and the city is given
//      the real path at once (slot 11).
//   3. A temporary file name (0x0091A497). Without one nothing is written.
//   4. A new save segment on the temporary file, opened for writing. Then:
//      messages 0x26C63343 and 0x26C63346 (sent, not posted: every
//      simulator saves its state into the segment while they run), the
//      file version record (0x005DBFA0), the game version record (type
//      0x0B7D45E0, group 0x0B7D45E1, instance 0x0B7D45E2: four dwords, the
//      numbers of the exe's file version), the application's record (type
//      0x49DD6F4C, group 0x49DD6F4E, instance 1) through 0x004545D0, and
//      when that went well messages 0x26C63344 and 0x26C63345. Then the
//      segment writes every registered object (segment slot 10) and is
//      closed. Each message carries the segment, the temporary path and the
//      real path as counted strings, and `fast` as 0 or 1; the last one
//      (0x26C63345) has no segment.
//   5. The city's own file is taken out of the resource manager and closed
//      (0x00454A10).
//   6. The file moves, only when everything so far went well: an existing
//      real file becomes the backup (the real path with '_' added; an older
//      backup is removed first), the temporary file becomes the real file,
//      the backup is removed. When the last move fails the temporary file is
//      removed and the backup is moved back. See MoveFiles below.
//   7. Whatever is left of the temporary file is removed, the city gets the
//      real path (or its old one after a failure), and the file at the
//      city's path is opened and given to the resource manager again
//      (0x00454B20).
//   8. After a save that went well, every adjacent regional city (the
//      neighbours simulator's slot 20 with 1) that has a file is written to
//      its file (0x005DC2E0).
//
// SaveCity(fast) (interface slot 16, 0x00454DA0) asks the city for its path
// and calls slot 15 with it; nothing happens when the path is empty.
//
// Replacement, off by default:
//
//   saves.app.part1: SaveCity(fast) and the three helpers. SaveCity itself
//   is SaveCityOurs below, a plain function that other code connects (the
//   time lapse and the traces keep detours of their own on 0x00455E20).
//
// Still the game's: 0x00454BB0 (cdecl (a, b): true when two strings DIFFER;
// string code that the framework and a dozen unrelated callers share).
//
// The application, as far as this part touches it (offsets from the
// object): +0x0C the interface, +0x58 the holder of the regional city (a
// pointer to a pointer), +0x5C the city, +0x654 the open file of the city
// (the cIGZPersistDBSegment part of a save segment, counted once).
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

uint8_t __fastcall SaveCityOurs(char* self, void* edx, void* path, uint32_t fast);

namespace {

namespace app { // offsets from the object
constexpr std::size_t kRegionalCityHolder = 0x58;
constexpr std::size_t kCity = 0x5C;
constexpr std::size_t kCityFile = 0x654;
} // namespace app

namespace iface { // the same from the interface (object + 0xC)
constexpr std::size_t kCity = 0x50;
constexpr std::ptrdiff_t kToObject = -0xC;
} // namespace iface

namespace city { // cISC4City, by vtable offset
constexpr std::size_t kGetSerial = 0x14;
constexpr std::size_t kGetSavePath = 0x28;     // (cIGZString&)
constexpr std::size_t kSetSavePath = 0x2C;     // (const cIGZString&)
constexpr std::size_t kGetName = 0x30;         // (cIGZString&)
constexpr std::size_t kSetNameChanged = 0x3C;  // (bool)
} // namespace city

namespace segment { // cIGZPersistDBSegment, by vtable offset
constexpr std::size_t kAddRef = 0x4;
constexpr std::size_t kRelease = 0x8;
constexpr std::size_t kInit = 0x10;
constexpr std::size_t kShutdown = 0x14;
constexpr std::size_t kOpen = 0x18; // (read, write)
constexpr std::size_t kClose = 0x20;
constexpr std::size_t kSetPath = 0x2C; // (const cIGZString&)
} // namespace segment

namespace save { // cISC4DBSegment, the save segment's own interface
constexpr std::size_t kAsSegment = 0xC;
constexpr std::size_t kSaveClassObjects = 0x28;
} // namespace save

namespace message { // the game's message (0x2C bytes, vtable 0x00ACEC00)
constexpr std::size_t kSetType = 0x14;
constexpr std::size_t kSetFirst = 0x30;
constexpr std::size_t kSetSecond = 0x44;
constexpr std::size_t kSetThird = 0x58;
constexpr std::size_t kSetFourth = 0x68;
} // namespace message

constexpr std::size_t kMessageSend = 0xC;       // message server
constexpr std::size_t kRegisterSegment = 0x70;  // resource manager: RegisterDBSegmentBack
constexpr std::size_t kUnregisterSegment = 0x74; // resource manager
constexpr std::size_t kAdjacentCities = 0x50;   // neighbours simulator: (list, with unestablished)
constexpr std::size_t kRegionalCityPath = 0x7C; // cISC4RegionalCity: GetCitySaveFilePath
constexpr std::size_t kSetUint16 = 0x1C;        // cIGZOStream
constexpr std::size_t kSetUint32 = 0x24;

constexpr uint32_t kBeforeSave = 0x26C63343;
constexpr uint32_t kBeforeSaveSecond = 0x26C63346;
constexpr uint32_t kAfterSave = 0x26C63344;
constexpr uint32_t kAfterSaveSecond = 0x26C63345;

constexpr uint32_t kGameVersionType = 0x0B7D45E0;
constexpr uint32_t kGameVersionGroup = 0x0B7D45E1;
constexpr uint32_t kGameVersionInstance = 0x0B7D45E2;
constexpr uint32_t kAppRecordType = 0x49DD6F4C;
constexpr uint32_t kAppRecordGroup = 0x49DD6F4E;
constexpr uint32_t kAppRecordVersion = 2;

// The game's string body and string.
struct StringBody {
    char* begin;
    char* end;
    char* capacity;
};

struct GameString {
    std::uintptr_t vtable;
    StringBody body;
    uint32_t references;
};
static_assert(sizeof(GameString) == 0x14);

// The stream object the game puts on the stack for one record of a segment
// (SC4SegmentOStream): vtable, the segment, the record's stream, a word.
struct SegmentStream {
    std::uintptr_t vtable;
    void* segment;
    void* stream;
    uint32_t word;
};
static_assert(sizeof(SegmentStream) == 0x10);

// A node of the game's list of pointers.
struct ListNode {
    ListNode* next;
    ListNode* previous;
    void* value;
};

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

// An empty string as the game makes one inline: eight bytes of buffer.
void MakeEmpty(GameString& text)
{
    text.body.begin = nullptr;
    text.body.end = nullptr;
    text.body.capacity = nullptr;
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    text.body.begin = buffer;
    text.body.end = buffer;
    text.body.capacity = buffer + 8;
    *buffer = 0;
    text.vtable = addr::SC4String_Vtable;
    text.references = 0;
}

void DestroyString(GameString& text)
{
    char* const buffer = text.body.begin;
    text.vtable = addr::SC4String_Vtable;
    if (buffer)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(buffer));
}

bool IsEmpty(const GameString& text)
{
    return text.body.begin == text.body.end;
}

// One character added to a string, as the game does it inline: when no
// room is left for it and the closing zero, the buffer grows by the larger
// of the length and 1.
void AppendChar(GameString& text, char character)
{
    char* end = text.body.end;
    if (end + 1 == text.body.capacity) {
        const uint32_t length = static_cast<uint32_t>(end - text.body.begin);
        const uint32_t more = length < 1 ? uint32_t{1} : length;
        This<void>(addr::cSC4Advisor_F40F1B0, &text.body, more + length); // reserve
        end = text.body.end;
    }
    end[1] = 0;
    *text.body.end = character;
    text.body.end = text.body.end + 1;
}

// A new string object (0x14 bytes, the game's own allocator) with a copy of
// `source`, counted once. As in the original a failed allocation is not
// survived: the count is taken through a null pointer.
void* NewCountedCopy(GameString& source)
{
    void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x14});
    void* const copy =
        memory ? This<void*>(addr::SC4String_Substring, memory, &source, uint32_t{0}, uint32_t{0xFFFFFFFF}) : nullptr;
    Virtual<uint32_t>(copy, 0x4);
    return copy;
}

// A new message, not counted; null when the allocation failed.
void* NewMessage()
{
    void* const memory = Cdecl<void*>(addr::RZ_operator_new, uint32_t{0x2C});
    return memory ? This<void*>(addr::cGZMessage_cGZMessage, memory) : nullptr;
}

bool Exists(void* path)
{
    return Cdecl<uint8_t>(addr::RZFile_Exists, path) != 0;
}

bool Remove(void* path)
{
    return Cdecl<uint8_t>(addr::RZFile_Remove, path) != 0;
}

bool Move(void* from, void* to)
{
    return Cdecl<uint8_t>(addr::RZFile_Move, from, to) != 0;
}

// The game's 0x004545D0, thiscall on the object (stream, segment), ret 8:
// writes the application's record to the stream: the version (2, a word)
// and the city by reference; then, when the application holds a regional
// city, has that write its own record into the segment (0x005DC1A0) and
// answers what that said. False when the reference could not be written
// (nothing more is done then), true when there is no regional city.
uint8_t __fastcall WriteAppRecord(char* self, void*, void* stream, void* part)
{
    Virtual<uint8_t>(stream, kSetUint16, kAppRecordVersion);
    if (!Cdecl<uint8_t>(addr::SC4Stream_WriteReference, stream, At<void*>(self, app::kCity)))
        return 0;
    void** const holder = At<void**>(self, app::kRegionalCityHolder);
    if (!holder)
        return 1;
    return Cdecl<uint8_t>(addr::SC4RegionalCity_WriteToSegment, part, *holder);
}

// The game's 0x00454A10, thiscall on the object, no arguments: when the
// application has the city's file open, takes it out of the resource
// manager, shuts it down, closes it (in that order), releases it and
// forgets it.
void __fastcall CloseCityFile(char* self, void*)
{
    void* const file = At<void*>(self, app::kCityFile);
    if (!file)
        return;
    Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), kUnregisterSegment, file);
    Virtual<uint8_t>(At<void*>(self, app::kCityFile), segment::kShutdown);
    Virtual<uint8_t>(At<void*>(self, app::kCityFile), segment::kClose);
    Virtual<uint32_t>(At<void*>(self, app::kCityFile), segment::kRelease);
    At<void*>(self, app::kCityFile) = nullptr;
}

// The game's 0x00454B20, thiscall on the object (path), ret 4: a new save
// segment on the file at `path`, opened for reading. When it opens, the
// application keeps it at +0x654 (counted once) and gives it to the
// resource manager, at the back; when it does not, it is shut down and
// dropped and +0x654 stays as it was. Init's answer is not looked at.
void __fastcall OpenCityFile(char* self, void*, void* path)
{
    void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x1DC});
    void* const file = memory ? This<void*>(addr::cSC4DBSegmentPackedFile_Construct, memory) : nullptr;
    void* const part = Virtual<void*>(file, save::kAsSegment);
    Virtual<uint32_t>(part, segment::kAddRef);
    Virtual<uint8_t>(part, segment::kInit);
    Virtual<uint8_t>(part, segment::kSetPath, path);
    if (Virtual<uint8_t>(part, segment::kOpen, uint32_t{1}, uint32_t{0})) {
        At<void*>(self, app::kCityFile) = part;
        Virtual<uint32_t>(part, segment::kAddRef);
        Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), kRegisterSegment, part);
        Virtual<uint32_t>(part, segment::kRelease);
        return;
    }
    Virtual<uint8_t>(part, segment::kShutdown);
    Virtual<uint32_t>(part, segment::kRelease);
}

// The game's 0x00454DA0, slot 16 of the interface: SaveCity(fast), ret 4;
// `this` is the interface. With no city: false. Else the city is asked for
// its path, and when that is not empty, slot 15 is called with it and
// `fast`; true when slot 15 said so.
uint8_t __fastcall SaveCityInPlace(char* self, void*, uint32_t fast)
{
    uint8_t result = 0;
    if (!At<void*>(self, iface::kCity))
        return 0;
    GameString path;
    MakeEmpty(path);
    Virtual<void>(At<void*>(self, iface::kCity), city::kGetSavePath, &path);
    if (!IsEmpty(path)) {
        if (Virtual<uint8_t>(self, 0x3C, &path, fast))
            result = 1;
    }
    DestroyString(path);
    return result;
}

// The four setters every save message but the last gets, and the send.
void SendSaveMessage(void* server, void* sent, uint32_t type, void* part, void* tempName, void* realName,
                     uint32_t fast)
{
    Virtual<void>(sent, message::kSetType, type);
    if (type != kAfterSaveSecond)
        Virtual<void>(sent, message::kSetFirst, part);
    Virtual<void>(sent, message::kSetSecond, tempName);
    Virtual<void>(sent, message::kSetThird, realName);
    Virtual<void>(sent, message::kSetFourth, fast);
    Virtual<uint8_t>(server, kMessageSend, sent);
}

// Step 4 of SaveCity: the city written into a new segment on the temporary
// file. True when the application's record was written and the segment
// then saved every object.
uint8_t WriteSegment(char* self, GameString& tempPath, GameString& realPath, uint32_t fastArgument)
{
    uint8_t result = 0;
    void* const tempName = NewCountedCopy(tempPath);
    void* const realName = NewCountedCopy(realPath);

    void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x1DC});
    void* const file = memory ? This<void*>(addr::cSC4DBSegmentPackedFile_Construct, memory) : nullptr;
    void* const part = Virtual<void*>(file, save::kAsSegment);
    Virtual<uint32_t>(part, segment::kAddRef);
    if (Virtual<uint8_t>(part, segment::kInit)) {
        Virtual<uint8_t>(part, segment::kSetPath, &tempPath);
        if (Virtual<uint8_t>(part, segment::kOpen, uint32_t{0}, uint32_t{1})) {
            void* const server = Global<void*>(addr::SC4_MessageServer);

            // The two messages before: counted when they could be made, and
            // used either way.
            void* const before = NewMessage();
            if (before)
                Virtual<uint32_t>(before, 0x4);
            const uint32_t fast = static_cast<uint8_t>(fastArgument) != 0 ? 1u : 0u;
            SendSaveMessage(server, before, kBeforeSave, part, tempName, realName, fast);

            void* const beforeSecond = NewMessage();
            if (beforeSecond)
                Virtual<uint32_t>(beforeSecond, 0x4);
            SendSaveMessage(server, beforeSecond, kBeforeSaveSecond, part, tempName, realName, fast);

            Cdecl<uint8_t>(addr::SC4CityFiles_SetVersion, part);

            // The game version record: the four numbers of the exe's file
            // version, each -1 when the version cannot be read.
            SegmentStream stream;
            This<void*>(addr::SC4SegmentOStream_SC4SegmentOStream, &stream, part, kGameVersionType, kGameVersionGroup,
                        kGameVersionInstance, uint32_t{3});
            alignas(4) char version[0x24];
            This<void*>(addr::RZVersion_Construct, &version[0]);
            uint32_t numbers[4];
            numbers[0] = This<uint32_t>(addr::RZVersion_GetNumber, &version[0], uint32_t{0});
            numbers[1] = This<uint32_t>(addr::RZVersion_GetNumber, &version[0], uint32_t{1});
            numbers[2] = This<uint32_t>(addr::RZVersion_GetNumber, &version[0], uint32_t{2});
            numbers[3] = This<uint32_t>(addr::RZVersion_GetNumber, &version[0], uint32_t{3});
            Virtual<uint8_t>(stream.stream, kSetUint32, numbers[0]);
            Virtual<uint8_t>(stream.stream, kSetUint32, numbers[1]);
            Virtual<uint8_t>(stream.stream, kSetUint32, numbers[2]);
            Virtual<uint8_t>(stream.stream, kSetUint32, numbers[3]);
            This<void>(addr::RZVersion_Destruct, &version[0]);
            This<void>(addr::SC4SegmentOStream_Close, &stream);

            // The application's record.
            This<void*>(addr::SC4SegmentOStream_SC4SegmentOStream, &stream, part, kAppRecordType, kAppRecordGroup,
                        uint32_t{1}, uint32_t{1});
            if (WriteAppRecord(self + iface::kToObject, nullptr, stream.stream, part)) {
                // The two messages after, each in a counting holder; the
                // second holder is made over the first without a release,
                // and both messages are released at the end.
                void* held = nullptr;
                This<void**>(addr::SC4RefHolder_SC4RefHolder, &held, NewMessage());
                void* const after = held;
                SendSaveMessage(server, after, kAfterSave, part, tempName, realName, fast);

                This<void**>(addr::SC4RefHolder_SC4RefHolder, &held, NewMessage());
                SendSaveMessage(server, held, kAfterSaveSecond, part, tempName, realName, fast);

                result = 1;
                Virtual<uint32_t>(held, 0x8);
                Virtual<uint32_t>(after, 0x8);
            }
            This<void>(addr::SC4SegmentOStream_Close, &stream);
            if (result)
                result = Virtual<uint8_t>(file, save::kSaveClassObjects);
            Virtual<uint8_t>(part, segment::kClose);
            Virtual<uint32_t>(beforeSecond, 0x8);
            Virtual<uint32_t>(before, 0x8);
        }
        Virtual<uint8_t>(part, segment::kShutdown);
    }
    Virtual<uint32_t>(part, segment::kRelease);
    Virtual<uint32_t>(tempName, 0x8);
    Virtual<uint32_t>(realName, 0x8);
    return result;
}

// Step 6 of SaveCity: the temporary file takes the real file's place. False
// when the save has failed after all.
//
//   - a real file is there: the backup path is the real path with '_'
//     added. A backup that is there is removed; when that fails, stop
//     (false; the real file and the old backup stay, the temporary file is
//     removed by the caller). The real file is moved to the backup path;
//     when that fails, stop (false; the real file stays);
//   - the temporary file is moved to the real path. When that went well the
//     backup is removed (its answer is not looked at). When it failed the
//     temporary file is removed, the backup is moved back to the real path
//     (not looked at either) and the answer is false;
//   - in both of the last two cases, when the path the caller gave differs
//     from the real path and a file is there, that file is removed: after a
//     failed last move too.
uint8_t MoveFiles(GameString& tempPath, GameString& realPath, GameString& backupPath, void* givenPath,
                  uint8_t givenDiffers)
{
    uint8_t result = 1;
    if (Exists(&realPath)) {
        This<void>(addr::cRZBaseString_Assign, &backupPath, &realPath);
        AppendChar(backupPath, '_');
        if (Exists(&backupPath) && !Remove(&backupPath))
            return 0;
        if (!Move(&realPath, &backupPath))
            return 0;
    }
    if (Move(&tempPath, &realPath)) {
        if (!IsEmpty(backupPath))
            Remove(&backupPath);
    } else {
        Remove(&tempPath);
        if (!IsEmpty(backupPath))
            Move(&backupPath, &realPath);
        result = 0;
    }
    if (givenDiffers && Exists(givenPath))
        Remove(givenPath);
    return result;
}

// Step 8 of SaveCity: every adjacent regional city that has a file is
// written to its file.
void SaveNeighbours()
{
    ListNode* list = nullptr;
    void* const allocator = NodeAllocatorAt(addr::SC4List_NodeAllocator4);
    ListNode* const head = Virtual<ListNode*>(allocator, 0xC, uint32_t{0xC});
    head->next = head;
    head->previous = head;
    list = head;
    Virtual<uint32_t>(Global<void*>(addr::SC4_MonthEndService), kAdjacentCities, &list, uint32_t{1});
    for (ListNode* node = list->next; node != list; node = node->next) {
        void* const neighbour = node->value;
        GameString file;
        MakeEmpty(file);
        Virtual<void>(neighbour, kRegionalCityPath, &file);
        if (!IsEmpty(file))
            Cdecl<uint8_t>(addr::SC4RegionalCity_SaveToFile, &file, neighbour);
        DestroyString(file);
    }
    This<void>(addr::SC4List_ClearReleasing4, &list);
    ListNode* const emptied = list;
    Virtual<void>(NodeAllocatorAt(addr::SC4List_NodeAllocator4), 0x10, emptied);
}

bool InstallSaveApp1(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[4] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F454DA0, reinterpret_cast<void*>(&SaveCityInPlace)},
        {addr::SC4UI_F4545D0, reinterpret_cast<void*>(&WriteAppRecord)},
        {addr::SC4UI_F454A10, reinterpret_cast<void*>(&CloseCityFile)},
        {addr::SC4UI_F454B20, reinterpret_cast<void*>(&OpenCityFile)},
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

const bool g_saveApp1Registered = registry::Register({
    "saves.app.part1",
    "saves",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F454DA0,
    "Our application save code, part 1: SaveCity(fast), the application's record, closing and opening the city's file",
    false,
    InstallSaveApp1,
});

} // namespace

// The game's 0x00455E20, slot 15 of the interface: SaveCity(path, fast),
// ret 8; `this` is the interface (object + 0xC). Written as a plain function
// with the slot's convention (ECX the interface, two stack arguments, the
// callee cleans); not hooked here. `path` is a cIGZString const*, `fast` is
// read as a byte. True when the city was written and its file is in place.
// See the top of the file for the steps.
uint8_t __fastcall SaveCityOurs(char* self, void*, void* path, uint32_t fast)
{
    uint8_t result = 0;
    alignas(4) char scope[4];
    This<void*>(addr::SC4Scope891D9A_Construct, &scope[0], uint32_t{0xF});
    alignas(8) char timed[0x20];
    This<void*>(addr::SC4TimedScope_Construct, &timed[0], uint32_t{0x3E8});
    alignas(4) char modal[4];
    This<void*>(addr::SC4ModalGuard_Construct, &modal[0], uint32_t{0}, uint32_t{1}, uint32_t{1}, uint32_t{1});
    alignas(4) char keys[4];
    This<void*>(addr::SC4SystemKeysGuard_Construct, &keys[0]);

    if (At<void*>(self, iface::kCity)) {
        GameString realPath;
        MakeEmpty(realPath);
        GameString oldPath;
        MakeEmpty(oldPath);
        GameString cityName;
        MakeEmpty(cityName);
        GameString backupPath;
        MakeEmpty(backupPath);
        GameString tempPath;
        MakeEmpty(tempPath);

        Virtual<void>(At<void*>(self, iface::kCity), city::kGetName, &cityName);
        const uint32_t serial = Virtual<uint32_t>(At<void*>(self, iface::kCity), city::kGetSerial);
        Cdecl<void>(addr::SC4RegionalCity_MakeSavePath, &cityName, serial, &realPath);
        const uint8_t givenDiffers = Cdecl<uint8_t>(addr::SC4UI_F454BB0, &realPath, path);
        Virtual<void>(At<void*>(self, iface::kCity), city::kGetSavePath, &oldPath);
        Virtual<void>(At<void*>(self, iface::kCity), city::kSetSavePath, &realPath);

        if (Cdecl<uint8_t>(addr::RZFile_MakeTempName, &tempPath, static_cast<void*>(nullptr)))
            result = WriteSegment(self, tempPath, realPath, fast);

        char* const object = self + iface::kToObject;
        CloseCityFile(object, nullptr);

        if (result) {
            Virtual<void>(At<void*>(self, iface::kCity), city::kSetNameChanged, uint32_t{0});
            result = MoveFiles(tempPath, realPath, backupPath, path, givenDiffers);
        }
        if (!IsEmpty(tempPath))
            Remove(&tempPath);

        Virtual<void>(At<void*>(self, iface::kCity), city::kSetSavePath, result ? &realPath : &oldPath);
        GameString current;
        MakeEmpty(current);
        Virtual<void>(At<void*>(self, iface::kCity), city::kGetSavePath, &current);
        OpenCityFile(object, nullptr, &current);
        DestroyString(current);

        DestroyString(tempPath);
        DestroyString(backupPath);
        DestroyString(cityName);
        DestroyString(oldPath);
        DestroyString(realPath);

        if (result)
            SaveNeighbours();
    }

    This<void>(addr::SC4SystemKeysGuard_Destruct, &keys[0]);
    This<void>(addr::SC4ModalGuard_Destruct, &modal[0]);
    This<void>(addr::SC4TimedScope_Destruct, &timed[0]);
    This<void>(addr::SC4Scope891D9A_Destruct, &scope[0]);
    return result;
}

} // namespace opensc4::saves
