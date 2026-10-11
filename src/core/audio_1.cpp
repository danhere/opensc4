// The sound event system and the audio conditions (0x004E1320 to
// 0x004E3F80).
//
// 1. cSC4AudioAEMSEventSystem (0x004E1320 to 0x004E2570): the game's link
//    to EA's sound library. The object is 0x150 bytes (constructor
//    0x004E1C20, made by the application at 0x004511BF), a system service
//    with the ID 0xAC1FAE31, in five parts:
//
//      +0x00  vtable 0x00A93284, 4 slots: the sound service's message target
//             (interface 0xBB523BB2). Slot 3 takes the service's message
//             0x2A5B69C0, the master volume changed.
//      +0x04  vtable 0x00A9325C, 10 slots: the event interface 0x8C1FAEB7.
//             Slot 3 loads a sound bank, 4 unloads one, 5 starts an event,
//             6 stops one, 7 to 9 set the volume of the library's three
//             channels.
//      +0x08  vtable 0x00A9324C, 4 slots: a message target (0x452294AA) for
//             the game's message 0xA6B79602, the view changed.
//      +0x0C  vtable 0x00A93218, 13 slots: the system service base. Slot 8
//             is Init, slot 9 Shutdown.
//      +0x10  vtable 0x00A93200, 6 slots: the base's last part, with the
//             deleting destructor at slot 3.
//
//    What is heard is the library's business. The system keeps the handles
//    of the running events (one per event number, 21 of them), the loaded
//    banks, the tuning values and the three numbers the library's master
//    volume is made of: the player's volume, the tuning's scale and a
//    volume by zoom, multiplied, sent through a curve and scaled to 0..127.
//
//    A sound bank is an object of 0x24 bytes (vtable 0x00A932EC, 3 slots):
//    +0x4 the reference count, +0x8 the bank's bytes (a byte vector), +0x14
//    the resource key (three dwords), +0x20 a handle.
//
// 2. The audio conditions (0x004E2570 to 0x004E3F80): 74 one-function
//    classes of three kinds, each a six-slot vtable whose last slot is the
//    test. What uses them is the rest of the audio code (their makers are at
//    0x004EC5xx onward).
//
//      - an occupant condition (interface 0xCA400753; 0x10 bytes: +0x4 the
//        occupant last asked about, +0x8 the answer, +0xC the reference
//        count): Test(occupant), ret 4;
//      - a cell condition (0xCA400757; 0x14 bytes: +0x4 and +0x8 the cell
//        last asked about, +0xC the answer, +0x10 the count): Test(x, z),
//        ret 8;
//      - a state condition (0xEA40074F; +0x4 the count): Test(), no
//        arguments, nothing kept.
//
//    The first two answer from what they keep when asked about the same
//    thing twice running.
//
// The fence: EA's sound library (0x00864D10 to 0x00867A00, and the eight
// functions of the table at 0x00B107D8) stays the game's and is called as
// the original calls it. So does the framework's sound service.
//
// Left the game's in this range: 0x004E13E0 (returns 0; three bytes, shared
// by code folding with many classes, and the callback the system gives the
// library at +0x100), 0x004E2410 (returns its argument; a callback given to
// the library with each bank), and 0x004E3F80 (the assignment of a map with
// 0x18-byte nodes, generic container code whose callers are in cSC4Lot).
// 0x004E22A0, 0x004E3400, 0x004E3470 and 0x004E34E0 are labels inside
// 0x004E2210, 0x004E33B0, 0x004E3420 and 0x004E3490, not functions.
//
// Replacement, off by default:
//
//   audio.part1: everything named above.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace opensc4::audio {
namespace {

namespace sys { // the event system, from the object's start
constexpr std::size_t kSound = 0x04;       // the event interface
constexpr std::size_t kTarget = 0x08;      // the game message target
constexpr std::size_t kService = 0x0C;     // the system service base
constexpr std::size_t kLast = 0x10;        // the base's last part
constexpr std::size_t kHandles = 0x28;     // vector of uint32: an event's handle + 1, or 0
constexpr std::size_t kBuffer = 0x34;      // vector of bytes: the library's working memory
constexpr std::size_t kConfig = 0x40;      // the library's settings, 0xD0 bytes
constexpr std::size_t kCallback = 0x100;   // inside the settings: a function
constexpr std::size_t kScale = 0x110;      // float: tuning 0xCBFB03FA
constexpr std::size_t kRate = 0x114;       // float: tuning 0xCBFB03FF
constexpr std::size_t kVoices = 0x118;     // uint32: tuning 0xEBFB0404
constexpr std::size_t kCurve = 0x11C;      // the volume curve
constexpr std::size_t kEvents = 0x12C;     // vector of bytes: the event data
constexpr std::size_t kBanks = 0x138;      // list of banks (pointer to the sentinel)
constexpr std::size_t kMaster = 0x13C;     // float: the player's volume
constexpr std::size_t kZoomVolume = 0x140; // float: the volume at the view's zoom
constexpr std::size_t kZoomVolumes = 0x144; // vector of float, one a zoom
} // namespace sys

namespace bank {
constexpr std::size_t kCount = 0x04;
constexpr std::size_t kData = 0x08; // vector of bytes
constexpr std::size_t kKey = 0x14;  // three dwords
} // namespace bank

constexpr uint32_t kSoundTargetId = 0xBB523BB2;
constexpr uint32_t kEventInterfaceId = 0x8C1FAEB7;
constexpr uint32_t kMessageTargetId = 0x452294AA;
constexpr uint32_t kVolumeMessage = 0x2A5B69C0;
constexpr uint32_t kViewMessage = 0xA6B79602;
constexpr uint32_t kBankGroup = 0x8A5971C5;

// The camera view's getter of its zoom and rotation (0x007F66A0, thiscall,
// ret 8). The camera view is the renderer's: called by address, no entry.
constexpr std::uintptr_t kCameraGetZoomAndRotation = 0x007F66A0;

// The sound library's function table: eight cdecl functions.
namespace table {
constexpr std::size_t kLoadEvents = 0x00; // (data): 0 when it took the data
constexpr std::size_t kReset = 0x04;      // ()
constexpr std::size_t kRemoveBank = 0x08; // (handle)
constexpr std::size_t kStart = 0x0C;      // (parameters*): a handle, negative on failure
constexpr std::size_t kStop = 0x14;       // (handle, parameters*)
constexpr std::size_t kSetVolume = 0x18;  // (channel, level 0..127): negative on failure
constexpr std::size_t kAddBank = 0x1C;    // (data, 0, 0, callback): 0 when it took the bank
} // namespace table

struct Node {
    Node* next;
    Node* previous;
    void* value;
};

struct ByteVector {
    uint8_t* begin;
    uint8_t* end;
    uint8_t* capacity;
};

struct FloatVector {
    float* begin;
    float* end;
    float* capacity;
};

struct Uint32Vector {
    uint32_t* begin;
    uint32_t* end;
    uint32_t* capacity;
};

// The game's string body and string.
struct StringBody {
    char* begin;
    char* end;
    char* capacity;
};

struct GameString {
    std::uintptr_t vtable;
    StringBody body;
    uint32_t zero;
};
static_assert(sizeof(GameString) == 0x14);

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

// A function of the sound library's table.
template <typename R, typename... Args>
R Library(std::size_t slot, Args... args)
{
    return Global<R(__cdecl*)(Args...)>(addr::AEMS_FunctionTable + slot)(args...);
}

uint32_t AddRef(void* object)
{
    return Virtual<uint32_t>(object, 0x4);
}

uint32_t Release(void* object)
{
    return Virtual<uint32_t>(object, 0x8);
}

void Free(void* memory)
{
    Cdecl<void>(addr::SC4_Free, memory);
}

// The game's ftol (0x009EEF04).
int32_t ToInt(double value)
{
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

void Lock()
{
    Cdecl<void>(addr::AEMS_Lock);
}

void Unlock()
{
    Cdecl<void>(addr::AEMS_Unlock);
}

// The tail the three volume paths share: the level through the curve, times
// 127, truncated, to the library as its master volume.
void SendVolume(char* self, float level)
{
    const double curved = This<double>(addr::SC4Curve_Evaluate, self + sys::kCurve, level);
    Cdecl<int32_t>(addr::AEMS_SetMasterVolume, ToInt(curved * static_cast<double>(127.0f)));
}

// The inlined loop that empties every event's handle. The original reads
// the vector's ends anew each turn.
void ClearHandles(char* self)
{
    const auto count = [self] {
        return static_cast<uint32_t>((At<int32_t>(self, sys::kHandles + 4) - At<int32_t>(self, sys::kHandles)) >> 2);
    };
    for (uint32_t i = 0; i < count(); ++i)
        At<uint32_t*>(self, sys::kHandles)[i] = 0;
}

uint8_t __fastcall RemoveBank(char* self, void*, uint32_t instance);
uint8_t __fastcall AddBank(char* self, void*, void* loaded);
uint8_t __fastcall BankLoad(char* self, void*, uint32_t* key);
char* __fastcall BankConstruct(char* self, void*);
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** out);
uint32_t __fastcall AddRefService(char* self, void*);
uint32_t __fastcall ReleaseService(char* self, void*);
char* __fastcall DeletingDestruct(char* self, void*, uint32_t flags);

// --- The event system: events and volume ----------------------------------------------

// The game's 0x004E1320 (thiscall, no arguments; `this` is the object).
// Stops the events 0 to 0xE, 0x11, 0x12, 0x14 and 0x13 through the event
// interface's slot 6. Plain state: the handles.
void __fastcall StopEvents(char* self, void*)
{
    static const uint32_t events[] = {0x0, 0x1, 0x2, 0x3, 0x4, 0x5,  0x6,  0x7,  0x8, 0x9,
                                      0xA, 0xB, 0xC, 0xD, 0xE, 0x11, 0x12, 0x14, 0x13};
    char* const sound = self + sys::kSound;
    for (const uint32_t event : events)
        Virtual<uint8_t>(sound, 0x18, event);
}

// The game's 0x004E13F0 (thiscall, no arguments; `this` is the object).
// Sends the master volume: zoom volume times player's volume times scale.
void __fastcall ApplyVolume(char* self, void*)
{
    const double product = static_cast<double>(At<float>(self, sys::kZoomVolume)) *
                           static_cast<double>(At<float>(self, sys::kMaster)) *
                           static_cast<double>(At<float>(self, sys::kScale));
    SendVolume(self, static_cast<float>(product));
}

// The game's 0x004E14B0 (thiscall, ret 4; slot 3 of the part at +0, `this`
// is the object). The sound service's message: for 0x2A5B69C0 the first
// number of its standard message, over 1024, becomes the player's volume
// and the master volume is sent. The product uses the new volume before it
// is rounded to a float, as the original's x87 code does. Always false.
uint8_t __fastcall OnSoundMessage(char* self, void*, void* message)
{
    if (Virtual<uint32_t>(message, 0x10) != kVolumeMessage)
        return 0;
    void* const standard = Virtual<void*>(message, 0x18);
    const uint32_t raw = Virtual<uint32_t>(standard, 0x24);
    const double master = static_cast<double>(raw) * static_cast<double>(1.0f / 1024.0f);
    At<float>(self, sys::kMaster) = static_cast<float>(master);
    const double product =
        static_cast<double>(At<float>(self, sys::kZoomVolume)) * static_cast<double>(At<float>(self, sys::kScale)) *
        master;
    SendVolume(self, static_cast<float>(product));
    return 0;
}

// What slots 7 to 9 of the event interface share: the volume kept within 0
// and 1 (anything that is not a number passes as it is), times 127,
// truncated, to the library for one of its three channels. True when the
// library does not answer with an error.
uint8_t SetChannelVolume(uint32_t channel, float volume)
{
    if (volume < 0.0f)
        volume = 0.0f;
    else if (volume > 1.0f)
        volume = 1.0f;
    Lock();
    const int32_t level = ToInt(static_cast<double>(volume) * static_cast<double>(127.0f));
    const int32_t result = Library<int32_t>(table::kSetVolume, channel, level);
    Unlock();
    return result >= 0;
}

// The game's 0x004E1530 (thiscall, ret 4; event interface slot 7).
uint8_t __fastcall SetVolume0(char*, void*, float volume)
{
    return SetChannelVolume(0, volume);
}

// The game's 0x004E15A0 (slot 8).
uint8_t __fastcall SetVolume1(char*, void*, float volume)
{
    return SetChannelVolume(1, volume);
}

// The game's 0x004E1610 (slot 9).
uint8_t __fastcall SetVolume2(char*, void*, float volume)
{
    return SetChannelVolume(2, volume);
}

// The game's 0x004E1680 (thiscall, ret 4; slot 3 of the part at +8, `this`
// is that part). The game's message 0xA6B79602: the volume for the camera
// view's zoom is looked up and the master volume sent. The camera view is
// not tested for null. True for that message, else false.
uint8_t __fastcall OnViewMessage(char* target, void*, void* message)
{
    if (Virtual<uint32_t>(message, 0x10) != kViewMessage)
        return 0;
    char* const self = target - sys::kTarget;
    const uint32_t zoom = At<uint32_t>(Global<void*>(addr::SC4_CameraView), 0xC);
    const float volume = At<float*>(self, sys::kZoomVolumes)[zoom];
    At<float>(self, sys::kZoomVolume) = volume;
    const double product = static_cast<double>(At<float>(self, sys::kMaster)) *
                           static_cast<double>(At<float>(self, sys::kScale)) * static_cast<double>(volume);
    SendVolume(self, static_cast<float>(product));
    return 1;
}

// The game's 0x004E16F0 (thiscall, ret 4; event interface slot 5, `this` is
// the interface). Starts an event that is not running: the library gets a
// pointer to the event's number and answers with a handle, kept plus one
// (0 when the library failed). False when the event is running.
uint8_t __fastcall StartEvent(char* sound, void*, uint32_t event)
{
    if (At<uint32_t*>(sound, sys::kHandles - sys::kSound)[event] != 0)
        return 0;
    uint32_t parameters = event;
    Lock();
    const int32_t handle = Library<int32_t>(table::kStart, &parameters) + 1;
    Unlock();
    At<uint32_t*>(sound, sys::kHandles - sys::kSound)[event] = handle <= 0 ? 0 : static_cast<uint32_t>(handle);
    return 1;
}

// The game's 0x004E1740 (thiscall, ret 4; event interface slot 6). Stops a
// running event and forgets its handle. False when it is not running.
uint8_t __fastcall StopEvent(char* sound, void*, uint32_t event)
{
    const uint32_t handle = At<uint32_t*>(sound, sys::kHandles - sys::kSound)[event];
    if (handle == 0)
        return 0;
    uint32_t parameters = event;
    Lock();
    Library<int32_t>(table::kStop, handle - 1, &parameters);
    Unlock();
    At<uint32_t*>(sound, sys::kHandles - sys::kSound)[event] = 0;
    return 1;
}

// --- The event system: banks ----------------------------------------------------------

// The game's 0x004E1790 (thiscall, ret 4; `this` is the object). Takes the
// bank with this instance ID out of the library and off the list. A null
// bank in the list faults as in the original.
uint8_t __fastcall RemoveBank(char* self, void*, uint32_t instance)
{
    Node* const head = At<Node*>(self, sys::kBanks);
    for (Node* node = head->next; node != head; node = node->next) {
        void* const loaded = node->value;
        if (loaded)
            AddRef(loaded);
        uint32_t key[3] = {0, 0, 0};
        This<void>(addr::SC4LocalizedString_GetKey, loaded, static_cast<uint32_t*>(key));
        if (key[2] == instance) {
            Library<int32_t>(table::kRemoveBank, This<uint32_t>(addr::SC4Audio_F4ED900, loaded));
            Release(loaded);
            Node* const next = node->next;
            Node* const previous = node->previous;
            previous->next = next;
            next->previous = previous;
            Free(node);
            Release(loaded);
            return 1;
        }
        if (loaded)
            Release(loaded);
    }
    return 0;
}

// The game's 0x004E1840 (thiscall, no arguments; `this` is the object).
// Takes every bank out of the library and empties the list.
void __fastcall RemoveBanks(char* self, void*)
{
    while (At<Node*>(self, sys::kBanks)->next != At<Node*>(self, sys::kBanks)) {
        void* const loaded = At<Node*>(self, sys::kBanks)->next->value;
        if (loaded)
            AddRef(loaded);
        Node* const node = At<Node*>(self, sys::kBanks)->next;
        Node* const next = node->next;
        Node* const previous = node->previous;
        previous->next = next;
        next->previous = previous;
        Free(node);
        Library<int32_t>(table::kRemoveBank, This<uint32_t>(addr::SC4Audio_F4ED900, loaded));
        Release(loaded);
        Release(loaded);
    }
}

// The game's 0x004E18B0 (thiscall, ret 8; event interface slot 4, `this` is
// the interface). Unloads the bank a property names: the property's value
// (0xEBF44DF7 without it) is the bank's instance ID. Every event is stopped
// and every handle forgotten first.
uint8_t __fastcall UnloadBank(char* sound, void*, void* holder, uint32_t property)
{
    uint32_t instance = 0xEBF44DF7;
    Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, property, &instance);
    char* const self = sound - sys::kSound;
    StopEvents(self, nullptr);
    ClearHandles(self);
    return RemoveBank(self, nullptr, instance);
}

// The game's 0x004E1BB0 (thiscall, ret 4; `this` is the object). Gives a
// loaded bank's bytes to the library and puts the bank at the list's end.
// False with no bank or when the library refuses.
uint8_t __fastcall AddBank(char* self, void*, void* loaded)
{
    if (!loaded)
        return 0;
    const uint32_t data = This<uint32_t>(addr::cSC4LotConfiguration_GetID, loaded); // shared code: +0x8
    if (Library<int32_t>(table::kAddBank, data, uint32_t{0}, uint32_t{0},
                         static_cast<std::uintptr_t>(addr::SC4Audio_F4E2410)) != 0)
        return 0;
    This<void>(addr::SC4Shared_SetField20, loaded, uint32_t{0});
    AddRef(loaded);
    Node* const head = At<Node*>(self, sys::kBanks);
    Node* const node = Cdecl<Node*>(addr::SC4_stl_allocate, uint32_t{0xC});
    node->value = loaded;
    Node* const last = head->previous;
    node->next = head;
    node->previous = last;
    last->next = node;
    head->previous = node;
    return 1;
}

// The game's 0x004E1DA0 (thiscall, ret 4; `this` is the object). Makes a
// bank, loads it by its resource key and adds it. When memory runs out the
// load is called on null, as in the original.
uint8_t __fastcall LoadBankByKey(char* self, void*, uint32_t* key)
{
    void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x24});
    char* const loaded = memory ? BankConstruct(static_cast<char*>(memory), nullptr) : nullptr;
    if (loaded)
        AddRef(loaded);
    if (!BankLoad(loaded, nullptr, key)) {
        if (loaded)
            Release(loaded);
        return 0;
    }
    const uint8_t added = AddBank(self, nullptr, loaded);
    if (loaded)
        Release(loaded);
    return added;
}

// The game's 0x004E1E10 (thiscall, ret 8; event interface slot 3). Loads
// the bank a property names (group 0x8A5971C5, the property's value as the
// instance), after stopping every event.
uint8_t __fastcall LoadBank(char* sound, void*, void* holder, uint32_t property)
{
    uint32_t instance = 0xEBF44DF7;
    Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, property, &instance);
    char* const self = sound - sys::kSound;
    StopEvents(self, nullptr);
    ClearHandles(self);
    uint32_t key[3] = {0, kBankGroup, instance};
    return LoadBankByKey(self, nullptr, key);
}

// --- The bank -------------------------------------------------------------------------

// The game's 0x004E2420 (thiscall, no arguments). The bank's constructor.
char* __fastcall BankConstruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0) = addr::cSC4AudioSoundBank_Vtable;
    for (std::size_t offset = 0x4; offset <= 0x20; offset += 4)
        At<uint32_t>(self, offset) = 0;
    return self;
}

// The game's 0x004E2450 (thiscall, no arguments; bank slot 2). Release:
// the count that is left, or 0 after destroying the bank.
int32_t __fastcall BankRelease(char* self, void*)
{
    const int32_t count = At<int32_t>(self, bank::kCount);
    if (count > 1) {
        At<int32_t>(self, bank::kCount) = count - 1;
        return count - 1;
    }
    At<std::uintptr_t>(self, 0) = addr::cSC4AudioSoundBank_Vtable;
    if (void* const data = At<void*>(self, bank::kData))
        Free(data);
    Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return 0;
}

// What the bank's and the event data's reads share: the byte vector sized
// to the record and filled from it (record slots 7 and 14).
uint8_t ReadRecord(ByteVector* bytes, void* record)
{
    uint8_t fill = 0;
    This<void>(addr::SC4ByteVector_Resize2, bytes, Virtual<uint32_t>(record, 0x1C), &fill);
    uint8_t* const begin = bytes->begin;
    return Virtual<uint8_t>(record, 0x38, begin, Virtual<uint32_t>(record, 0x1C));
}

// The game's 0x004E2490 (thiscall, ret 4). Reads the bank's bytes from an
// open record.
uint8_t __fastcall BankRead(char* self, void*, void* record)
{
    if (!record)
        return 0;
    return ReadRecord(&At<ByteVector>(self, bank::kData), record);
}

// The game's 0x004E24E0 (thiscall, ret 4). Loads the bank from the record
// with this key and keeps the key. The answer is the record's closing.
uint8_t __fastcall BankLoad(char* self, void*, uint32_t* key)
{
    void* record = nullptr;
    if (Cdecl<uint8_t>(addr::SC4_OpenResourceRecord, key, &record, uint32_t{0})) {
        if (BankRead(self, nullptr, record)) {
            At<uint32_t>(self, bank::kKey) = key[0];
            At<uint32_t>(self, bank::kKey + 4) = key[1];
            At<uint32_t>(self, bank::kKey + 8) = key[2];
            const uint8_t closed = Virtual<uint8_t>(record, 0x14);
            if (record)
                Release(record);
            return closed;
        }
        Virtual<uint8_t>(record, 0x14);
    }
    if (record)
        Release(record);
    return 0;
}

// --- The event system: tuning, start and shutdown -------------------------------------

// The game's 0x004E1EA0 (thiscall, no arguments; `this` is the object).
// ReadTuningValues: the exemplar 0x6534284A / 0xCA25875D / 0x6A359C5C gives
// the scale, the rate and the voices, the volume curve (property
// 0x4BFDD955; (0, 0) to (1, 1) without it) and the volume by zoom (property
// 0x8C376D4F; 0, 0, 0, 0.5, 1 without it). Without the exemplar an error
// text is made and thrown away, and the answer is false.
uint8_t __fastcall ReadTuning(char* self, void*)
{
    using InsertPointFn = void(__thiscall*)(FloatVector* points, float* where, const float* value, void* allocator,
                                            uint32_t count, uint32_t atEnd);
    void* const resources = Global<void*>(addr::SC4_ResourceManager);
    uint32_t key[3] = {0x6534284A, 0xCA25875D, 0x6A359C5C};
    if (resources)
        AddRef(resources);
    void* exemplar = nullptr;
    if (!Virtual<uint8_t>(resources, 0xC, static_cast<uint32_t*>(key), uint32_t{0xA52160F5}, &exemplar, uint32_t{0},
                          uint32_t{0})) {
        GameString text;
        text.body.begin = nullptr;
        text.body.end = nullptr;
        text.body.capacity = nullptr;
        char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
        text.body.begin = buffer;
        text.body.end = buffer;
        text.body.capacity = buffer + 8;
        *buffer = 0;
        text.vtable = addr::SC4String_Vtable;
        text.zero = 0;
        reinterpret_cast<void(__cdecl*)(void*, const char*, ...)>(addr::cRZBaseString_Sprintf)(
            &text, "Error cSC4AudioAEMSEventSystem::ReadTuningValues : Failed to read tuning values\n");
        char* const written = text.body.begin;
        text.vtable = addr::SC4String_Vtable;
        if (written)
            Free(written);
        if (exemplar)
            Release(exemplar);
        Release(resources);
        return 0;
    }

    void* const holder = Virtual<void*>(exemplar, 0x1C);
    if (holder)
        AddRef(holder);
    Cdecl<uint8_t>(addr::SC4_GetPropertyFloat, holder, uint32_t{0xCBFB03FA}, &At<float>(self, sys::kScale));
    Cdecl<uint8_t>(addr::SC4_GetPropertyFloat, holder, uint32_t{0xCBFB03FF}, &At<float>(self, sys::kRate));
    Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, uint32_t{0xEBFB0404}, &At<uint32_t>(self, sys::kVoices));

    // The curve's default points.
    uint8_t allocator = 0;
    FloatVector points{nullptr, nullptr, nullptr};
    float point[2] = {0.0f, 0.0f};
    const auto insertPoint = reinterpret_cast<InsertPointFn>(addr::SC4PointVector_InsertN);
    insertPoint(&points, nullptr, point, &allocator, 1, 1);
    point[0] = 1.0f;
    point[1] = 1.0f;
    if (points.end != points.capacity) {
        float* end = points.end;
        if (end) {
            end[0] = 1.0f;
            end[1] = 1.0f;
            end = points.end;
        }
        points.end = end + 2;
    } else {
        insertPoint(&points, points.end, point, &allocator, 1, 1);
    }
    char* const curve = self + sys::kCurve;
    This<uint8_t>(addr::SC4Curve_SetPoints, curve, &points);
    This<uint8_t>(addr::SC4Curve_LoadFromExemplar, curve, static_cast<uint32_t*>(key), uint32_t{0x4BFDD955});

    // The volume by zoom: five defaults, then the property.
    FloatVector* const volumes = &At<FloatVector>(self, sys::kZoomVolumes);
    volumes->end = volumes->begin;
    const uint32_t* defaults = &Global<uint32_t>(addr::cSC4AudioAEMSEventSystem_DefaultZoomVolumes);
    for (int i = 0; i < 5; ++i, ++defaults) {
        float* const end = volumes->end;
        if (end != volumes->capacity) {
            if (end)
                *reinterpret_cast<uint32_t*>(end) = *defaults;
            volumes->end = volumes->end + 1;
        } else {
            This<void>(addr::SC4FloatVector_InsertN5, volumes, end, defaults, &allocator, uint32_t{1}, uint32_t{1});
        }
    }
    Cdecl<uint8_t>(addr::SC4_GetPropertyFloatVector, holder, uint32_t{0x8C376D4F}, volumes);

    if (points.begin)
        Free(points.begin);
    if (holder)
        Release(holder);
    if (exemplar)
        Release(exemplar);
    Release(resources);
    return 1;
}

// The game's 0x004E2120 (thiscall, ret 4; `this` is the object). Reads the
// event data from an open record and gives it to the library after a
// reset. True when the library answers 0.
uint8_t __fastcall ReadEvents(char* self, void*, void* record)
{
    if (!record)
        return 0;
    ByteVector* const events = &At<ByteVector>(self, sys::kEvents);
    if (!ReadRecord(events, record))
        return 0;
    Lock();
    Library<int32_t>(table::kReset);
    const int32_t result = Library<int32_t>(table::kLoadEvents, events->begin);
    Unlock();
    return result == 0;
}

// The game's 0x004E21A0 (thiscall, ret 4; `this` is the object). Loads the
// event data from the record with this key. The answer is the record's
// closing.
uint8_t __fastcall LoadEvents(char* self, void*, uint32_t* key)
{
    void* record = nullptr;
    if (Cdecl<uint8_t>(addr::SC4_OpenResourceRecord, key, &record, uint32_t{0})) {
        if (ReadEvents(self, nullptr, record)) {
            const uint8_t closed = Virtual<uint8_t>(record, 0x14);
            if (record)
                Release(record);
            return closed;
        }
        Virtual<uint8_t>(record, 0x14);
    }
    if (record)
        Release(record);
    return 0;
}

// The game's 0x004E2210 (thiscall, no arguments; service slot 8, `this` is
// the service part). Init, once: the three channels' volumes set from the
// dword at 0x00B2193C, the player's volume taken from the application's
// preferences, the tuning read, the library given 200000 bytes of working
// memory and started, the master volume sent, the object registered with
// the sound service, the event data (0x8A5971C5 / 0x6B955C37) loaded and
// the view message asked for. When the library fails, Shutdown (slot 9) is
// called and the answer is false.
uint8_t __fastcall Init(char* service, void*)
{
    if (Global<uint8_t>(addr::cSC4AudioAEMSEventSystem_Started))
        return 1;
    char* const self = service - sys::kService;
    Global<uint8_t>(addr::cSC4AudioAEMSEventSystem_Started) = 1;
    ClearHandles(self);

    const uint32_t volume = Global<uint32_t>(addr::cSC4AudioAEMSEventSystem_StartVolume); // a float's bits
    char* const sound = self + sys::kSound;
    Virtual<uint8_t>(sound, 0x1C, volume);
    Virtual<uint8_t>(sound, 0x20, volume);
    Virtual<uint8_t>(sound, 0x24, volume);

    char* const preferences = Virtual<char*>(Global<void*>(addr::SC4_Service43C94), 0x98);
    if (At<uint8_t>(preferences, 0x4))
        At<float>(self, sys::kMaster) = static_cast<float>(static_cast<double>(At<int32_t>(preferences, 0x8)) *
                                                             static_cast<double>(1.0f / 1024.0f));
    else
        At<float>(self, sys::kMaster) = 0.0f;
    ReadTuning(self, nullptr);

    // The working memory: 200000 bytes.
    ByteVector* const buffer = &At<ByteVector>(self, sys::kBuffer);
    uint8_t fill = 0;
    const uint32_t size = static_cast<uint32_t>(buffer->end - buffer->begin);
    if (size > 200000)
        buffer->end = buffer->begin + 200000;
    else
        This<void>(addr::SC4ByteVector_InsertN, buffer, buffer->end, uint32_t{200000} - size, &fill);

    char* const config = self + sys::kConfig;
    bool started = Cdecl<int32_t>(addr::AEMS_GetConfig, config) >= 0;
    if (started) {
        At<std::uintptr_t>(self, sys::kCallback) = addr::SC4Audio_F4E13E0;
        started = Cdecl<int32_t>(addr::AEMS_SetConfig, config) >= 0 &&
                  Cdecl<int32_t>(addr::AEMS_Start, buffer->begin, static_cast<uint32_t>(buffer->end - buffer->begin),
                                 uint32_t{0x72B00}) >= 0;
        if (started) {
            const uint32_t voices = At<uint32_t>(self, sys::kVoices);
            const int32_t rate = ToInt(static_cast<double>(At<float>(self, sys::kRate)));
            started = Cdecl<int32_t>(addr::AEMS_Open, uint32_t{0}, rate, voices, uint32_t{0}, uint32_t{0}) >= 0;
        }
    }
    if (!started) {
        Virtual<uint8_t>(service, 0x24);
        return 0;
    }

    ApplyVolume(self, nullptr);
    void* soundService = nullptr;
    This<void*>(addr::SC4RefPtr_ConstructSoundService, &soundService);
    if (soundService)
        Virtual<uint32_t>(soundService, 0x3C, self);
    uint32_t key[3] = {0, kBankGroup, 0x6B955C37};
    LoadEvents(self, nullptr, key);
    if (void* const messages = Global<void*>(addr::SC4_MessageServer))
        Virtual<uint32_t>(messages, 0x14, self ? self + sys::kTarget : nullptr, kViewMessage);
    This<void>(addr::SC4RefPtr_DestructGuarded, &soundService);
    return 1;
}

// The game's 0x004E1AB0 (thiscall, no arguments; service slot 9). Shutdown,
// when started: events stopped, banks removed, the library reset and shut
// down, the working memory emptied, the object taken off the sound service
// and off the view message.
uint8_t __fastcall Shutdown(char* service, void*)
{
    if (!Global<uint8_t>(addr::cSC4AudioAEMSEventSystem_Started))
        return 1;
    char* const self = service - sys::kService;
    Global<uint8_t>(addr::cSC4AudioAEMSEventSystem_Started) = 0;
    StopEvents(self, nullptr);
    ClearHandles(self);
    RemoveBanks(self, nullptr);
    Lock();
    Library<int32_t>(table::kReset);
    Unlock();
    Cdecl<int32_t>(addr::AEMS_Shutdown);
    ByteVector* const buffer = &At<ByteVector>(self, sys::kBuffer);
    buffer->end = buffer->begin;

    void* soundService = nullptr;
    if (void* const framework = Cdecl<void*>(addr::RZGetFramework))
        Virtual<uint8_t>(framework, 0x14, uint32_t{0x23CECBA0}, uint32_t{0x106077D2}, &soundService);
    if (soundService)
        Virtual<uint32_t>(soundService, 0x40, self);
    if (void* const messages = Global<void*>(addr::SC4_MessageServer))
        Virtual<uint32_t>(messages, 0x18, self ? self + sys::kTarget : nullptr, kViewMessage);
    This<void>(addr::SC4RefPtr_DestructGuarded, &soundService);
    return 1;
}

// --- The event system: the object -----------------------------------------------------

// The game's 0x004E1C20 (thiscall, no arguments). The constructor.
char* __fastcall Construct(char* self, void*)
{
    At<std::uintptr_t>(self, 0) = addr::cSC4AudioAEMSEventSystem_BaseVtable0;
    At<std::uintptr_t>(self, sys::kSound) = addr::cSC4AudioAEMSEventSystem_BaseVtable4;
    At<std::uintptr_t>(self, sys::kTarget) = addr::cSC4TrafficSimulator_VtableBase4;
    This<void*>(addr::cRZBaseSystemService_cRZBaseSystemService, self + sys::kService, uint32_t{0xAC1FAE31},
                uint32_t{0x80000000});
    At<std::uintptr_t>(self, sys::kService) = addr::cSC4AudioAEMSEventSystem_ServiceVtable;
    At<std::uintptr_t>(self, 0) = addr::cSC4AudioAEMSEventSystem_Vtable;
    At<std::uintptr_t>(self, sys::kSound) = addr::cSC4AudioAEMSEventSystem_EventVtable;
    At<std::uintptr_t>(self, sys::kTarget) = addr::cSC4AudioAEMSEventSystem_TargetVtable;
    At<std::uintptr_t>(self, sys::kLast) = addr::cSC4AudioAEMSEventSystem_LastVtable;
    for (std::size_t offset = sys::kHandles; offset <= 0x3C; offset += 4)
        At<uint32_t>(self, offset) = 0;
    At<float>(self, sys::kScale) = 1.0f;
    At<float>(self, sys::kRate) = 1.0f;
    At<uint32_t>(self, sys::kVoices) = 0x25;
    This<void*>(addr::SC4Curve_Init, self + sys::kCurve);
    At<uint32_t>(self, sys::kEvents) = 0;
    At<uint32_t>(self, sys::kEvents + 4) = 0;
    At<uint32_t>(self, sys::kEvents + 8) = 0;
    At<Node*>(self, sys::kBanks) = nullptr;
    Node* const head = Cdecl<Node*>(addr::SC4_stl_allocate, uint32_t{0xC});
    head->next = head;
    head->previous = head;
    At<Node*>(self, sys::kBanks) = head;
    At<float>(self, sys::kMaster) = 1.0f;
    At<float>(self, sys::kZoomVolume) = 1.0f;
    At<uint32_t>(self, sys::kZoomVolumes) = 0;
    At<uint32_t>(self, sys::kZoomVolumes + 4) = 0;
    At<uint32_t>(self, sys::kZoomVolumes + 8) = 0;

    // Twenty-one empty handles.
    uint32_t* const zeros = Cdecl<uint32_t*>(addr::SC4_stl_allocate, uint32_t{0x54});
    for (std::size_t i = 0; i < 21; ++i)
        zeros[i] = 0;
    Uint32Vector initial{zeros, zeros + 21, zeros + 21};
    This<void*>(addr::SC4Uint32Vector_CopyAssign, self + sys::kHandles, &initial);
    if (zeros)
        Free(zeros);
    return self;
}

// The game's 0x004E1920 (thiscall, no arguments; `this` is the object). The
// destructor. The list's nodes are freed without letting the banks go.
void __fastcall Destruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0) = addr::cSC4AudioAEMSEventSystem_Vtable;
    At<std::uintptr_t>(self, sys::kSound) = addr::cSC4AudioAEMSEventSystem_EventVtable;
    At<std::uintptr_t>(self, sys::kTarget) = addr::cSC4AudioAEMSEventSystem_TargetVtable;
    At<std::uintptr_t>(self, sys::kService) = addr::cSC4AudioAEMSEventSystem_ServiceVtable;
    At<std::uintptr_t>(self, sys::kLast) = addr::cSC4AudioAEMSEventSystem_LastVtable;
    if (void* const volumes = At<void*>(self, sys::kZoomVolumes))
        Free(volumes);
    This<void>(addr::SC4PairList_Destroy, self + sys::kBanks);
    if (void* const events = At<void*>(self, sys::kEvents))
        Free(events);
    if (void* const segments = At<void*>(self, sys::kCurve))
        Free(segments);
    if (void* const buffer = At<void*>(self, sys::kBuffer))
        Free(buffer);
    if (void* const handles = At<void*>(self, sys::kHandles))
        Free(handles);
    This<void>(addr::cRZBaseSystemService_Destroy, self + sys::kService);
}

// The game's 0x004E1D80 (thiscall, ret 4; `this` is the object). The
// deleting destructor.
char* __fastcall DeletingDestruct(char* self, void*, uint32_t flags)
{
    Destruct(self, nullptr);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// The game's 0x004E1430 (thiscall, ret 8; `this` is the object).
// QueryInterface: the object for the sound target, the event interface, the
// game message target, else the service base's answer. The original's null
// tests of `this` are kept (with no `this` it stores null and then faults
// in AddRef).
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** out)
{
    if (iid == kMessageTargetId) {
        *out = self ? self + sys::kTarget : nullptr;
    } else if (iid == kEventInterfaceId) {
        *out = self ? self + sys::kSound : nullptr;
    } else if (iid == kSoundTargetId) {
        *out = self;
    } else {
        return This<uint8_t>(addr::cRZBaseSystemService_QueryInterface, self + sys::kService, iid, out);
    }
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// The game's 0x004E19C0 (thiscall, no arguments; `this` is the object).
// AddRef: the service base's.
uint32_t __fastcall AddRefService(char* self, void*)
{
    return This<uint32_t>(addr::cRZBaseSystemService_AddRef, self + sys::kService);
}

// The game's 0x004E19D0. Release: the service base's.
uint32_t __fastcall ReleaseService(char* self, void*)
{
    return This<uint32_t>(addr::cRZBaseSystemService_Release, self + sys::kService);
}

// The parts' thunks. 0x004E19E0, 0x004E19F0, 0x004E1A00: the event
// interface's QueryInterface, AddRef and Release.
uint8_t __fastcall SoundQueryInterface(char* part, void*, uint32_t iid, void** out)
{
    return QueryInterface(part - sys::kSound, nullptr, iid, out);
}

uint32_t __fastcall SoundAddRef(char* part, void*)
{
    return AddRefService(part - sys::kSound, nullptr);
}

uint32_t __fastcall SoundRelease(char* part, void*)
{
    return ReleaseService(part - sys::kSound, nullptr);
}

// 0x004E1A10, 0x004E1A20, 0x004E1A30: the game message target's.
uint8_t __fastcall TargetQueryInterface(char* part, void*, uint32_t iid, void** out)
{
    return QueryInterface(part - sys::kTarget, nullptr, iid, out);
}

uint32_t __fastcall TargetAddRef(char* part, void*)
{
    return AddRefService(part - sys::kTarget, nullptr);
}

uint32_t __fastcall TargetRelease(char* part, void*)
{
    return ReleaseService(part - sys::kTarget, nullptr);
}

// 0x004E1A40, 0x004E1A50, 0x004E1A60: the service part's.
uint8_t __fastcall ServiceQueryInterface(char* part, void*, uint32_t iid, void** out)
{
    return QueryInterface(part - sys::kService, nullptr, iid, out);
}

uint32_t __fastcall ServiceAddRef(char* part, void*)
{
    return AddRefService(part - sys::kService, nullptr);
}

uint32_t __fastcall ServiceRelease(char* part, void*)
{
    return ReleaseService(part - sys::kService, nullptr);
}

// 0x004E1A70, 0x004E1A80, 0x004E1A90, 0x004E1AA0: the last part's, and its
// deleting destructor (ret 4).
uint8_t __fastcall LastQueryInterface(char* part, void*, uint32_t iid, void** out)
{
    return QueryInterface(part - sys::kLast, nullptr, iid, out);
}

uint32_t __fastcall LastAddRef(char* part, void*)
{
    return AddRefService(part - sys::kLast, nullptr);
}

uint32_t __fastcall LastRelease(char* part, void*)
{
    return ReleaseService(part - sys::kLast, nullptr);
}

char* __fastcall LastDeletingDestruct(char* part, void*, uint32_t flags)
{
    return DeletingDestruct(part - sys::kLast, nullptr, flags);
}

// --- Occupant conditions --------------------------------------------------------------
//
// Test(occupant), thiscall, ret 4. The same occupant as last time gets the
// answer kept at +0x8.

namespace occ {
constexpr std::size_t kLast = 0x4;
constexpr std::size_t kAnswer = 0x8;
} // namespace occ

bool Asked(char* self, void* occupant)
{
    return occupant == At<void*>(self, occ::kLast);
}

uint8_t Kept(char* self)
{
    return At<uint8_t>(self, occ::kAnswer);
}

uint8_t Keep(char* self, void* occupant, bool answer)
{
    At<void*>(self, occ::kLast) = occupant;
    At<uint8_t>(self, occ::kAnswer) = answer ? 1 : 0;
    return answer ? 1 : 0;
}

// Occupant slot 22 (+0x58): whether it is of a type.
bool IsOfType(void* occupant, uint32_t type)
{
    return Virtual<uint8_t>(occupant, 0x58, type) != 0;
}

uint8_t TestType(char* self, void* occupant, uint32_t type)
{
    if (Asked(self, occupant))
        return Kept(self);
    return Keep(self, occupant, IsOfType(occupant, type));
}

// The game's 0x004E2570: type 0x4002.
uint8_t __fastcall OccupantIs4002(char* self, void*, void* occupant)
{
    return TestType(self, occupant, 0x4002);
}

// The game's 0x004E25B0: type 0x4202, 0x4228 or 0x4211.
uint8_t __fastcall OccupantIs4202Or4228Or4211(char* self, void*, void* occupant)
{
    if (Asked(self, occupant))
        return Kept(self);
    return Keep(self, occupant,
                IsOfType(occupant, 0x4202) || IsOfType(occupant, 0x4228) || IsOfType(occupant, 0x4211));
}

// The game's 0x004E2620: type 0x4224 or 0x4225.
uint8_t __fastcall OccupantIs4224Or4225(char* self, void*, void* occupant)
{
    if (Asked(self, occupant))
        return Kept(self);
    return Keep(self, occupant, IsOfType(occupant, 0x4224) || IsOfType(occupant, 0x4225));
}

// The game's 0x004E2680: type 0x4219.
uint8_t __fastcall OccupantIs4219(char* self, void*, void* occupant)
{
    return TestType(self, occupant, 0x4219);
}

// The game's 0x004E26C0: type 0x420B.
uint8_t __fastcall OccupantIs420B(char* self, void*, void* occupant)
{
    return TestType(self, occupant, 0x420B);
}

// The game's 0x004E2700: type 0x420C.
uint8_t __fastcall OccupantIs420C(char* self, void*, void* occupant)
{
    return TestType(self, occupant, 0x420C);
}

// The game's 0x004E2740: type 0x420D.
uint8_t __fastcall OccupantIs420D(char* self, void*, void* occupant)
{
    return TestType(self, occupant, 0x420D);
}

// The game's 0x004E2780: type 0x420E.
uint8_t __fastcall OccupantIs420E(char* self, void*, void* occupant)
{
    return TestType(self, occupant, 0x420E);
}

// The game's 0x004E27C0: type 0x420A.
uint8_t __fastcall OccupantIs420A(char* self, void*, void* occupant)
{
    return TestType(self, occupant, 0x420A);
}

// The game's 0x004E2800: type 0x4218.
uint8_t __fastcall OccupantIs4218(char* self, void*, void* occupant)
{
    return TestType(self, occupant, 0x4218);
}

// The game's 0x004E2840: type 0x4005.
uint8_t __fastcall OccupantIs4005(char* self, void*, void* occupant)
{
    return TestType(self, occupant, 0x4005);
}

// The game's 0x004E2880: type 0x4200 or 0x420F.
uint8_t __fastcall OccupantIs4200Or420F(char* self, void*, void* occupant)
{
    if (Asked(self, occupant))
        return Kept(self);
    return Keep(self, occupant, IsOfType(occupant, 0x4200) || IsOfType(occupant, 0x420F));
}

// The game's 0x004E28E0: type 0x4206.
uint8_t __fastcall OccupantIs4206(char* self, void*, void* occupant)
{
    return TestType(self, occupant, 0x4206);
}

// The game's 0x004E2920: type 0x4205.
uint8_t __fastcall OccupantIs4205(char* self, void*, void* occupant)
{
    return TestType(self, occupant, 0x4205);
}

// The game's 0x004E2960: type 0x4209.
uint8_t __fastcall OccupantIs4209(char* self, void*, void* occupant)
{
    return TestType(self, occupant, 0x4209);
}

// The game's 0x004E29A0: type 0x4002 and not 0x4005.
uint8_t __fastcall OccupantIs4002Not4005(char* self, void*, void* occupant)
{
    if (Asked(self, occupant))
        return Kept(self);
    return Keep(self, occupant, IsOfType(occupant, 0x4002) && !IsOfType(occupant, 0x4005));
}

// The game's 0x004E2A00: the occupant's own type (slot 7) is 0x6A11BD2B.
uint8_t __fastcall OccupantIsClass6A11BD2B(char* self, void*, void* occupant)
{
    if (Asked(self, occupant))
        return Kept(self);
    return Keep(self, occupant, Virtual<uint32_t>(occupant, 0x1C) == 0x6A11BD2B);
}

// The game's 0x004E2A40: the occupant's property 0x69DBEBE1 is true. The
// answer's place on the stack is the argument's, so without the property
// the original reads the low byte of the occupant's address; kept.
uint8_t __fastcall OccupantHasFlag69DBEBE1(char* self, void*, void* occupant)
{
    if (Asked(self, occupant))
        return Kept(self);
    uint32_t value = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(occupant));
    Cdecl<uint8_t>(addr::SC4_GetPropertyBool, Virtual<void*>(occupant, 0x18), uint32_t{0x69DBEBE1},
                   reinterpret_cast<uint8_t*>(&value));
    return Keep(self, occupant, (value & 0xFF) != 0);
}

// The lot at a cell (lot manager slot 6).
void* LotAt(int32_t x, int32_t z)
{
    return Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x18, x, z, uint32_t{0});
}

// Lot slot 35 (+0x8C) answering 2, 3, 5 or 6: the jump tables at
// 0x004E2D7C and 0x004E3E04.
bool Is2356(uint32_t value)
{
    return value == 2 || value == 3 || value == 5 || value == 6;
}

// The game's 0x004E2CC0: the occupant's first cell (slot 11 gives its
// cells) is inside the city and the lot there answers 2, 3, 5 or 6 at slot
// 35.
uint8_t __fastcall OccupantOnLot2356(char* self, void*, void* occupant)
{
    if (Asked(self, occupant))
        return Kept(self);
    int32_t cells[4];
    Virtual<void>(occupant, 0x2C, static_cast<int32_t*>(cells));
    void* const city = Global<void*>(addr::SC4_City);
    bool answer = false;
    if (cells[0] >= 0 && cells[0] < Virtual<int32_t>(city, 0x174) && cells[1] >= 0 &&
        cells[1] < Virtual<int32_t>(city, 0x178)) {
        if (void* const lot = LotAt(cells[0], cells[1]))
            answer = Is2356(Virtual<uint32_t>(lot, 0x8C));
    }
    return Keep(self, occupant, answer);
}

// The game's 0x004E2D90: the lot at the occupant's first cell answers 4 at
// slot 35. The cell is not tested against the city's size.
uint8_t __fastcall OccupantOnLot4(char* self, void*, void* occupant)
{
    if (Asked(self, occupant))
        return Kept(self);
    int32_t cells[4];
    Virtual<void>(occupant, 0x2C, static_cast<int32_t*>(cells));
    void* const lot = LotAt(cells[0], cells[1]);
    return Keep(self, occupant, lot && Virtual<uint32_t>(lot, 0x8C) == 4);
}

// What the next two share: whether the occupant has an interface.
uint8_t TestInterface(char* self, void* occupant, uint32_t iid)
{
    if (Asked(self, occupant))
        return Kept(self);
    void* found = nullptr;
    const uint8_t has = Virtual<uint8_t>(occupant, 0x0, iid, &found);
    At<void*>(self, occ::kLast) = occupant;
    At<uint8_t>(self, occ::kAnswer) = has ? 1 : 0;
    if (found)
        Release(found);
    return has ? 1 : 0;
}

// The game's 0x004E2E10: interface 0xC83DD057.
uint8_t __fastcall OccupantHasC83DD057(char* self, void*, void* occupant)
{
    return TestInterface(self, occupant, 0xC83DD057);
}

// The game's 0x004E2E80: interface 0x87DDA3A9.
uint8_t __fastcall OccupantHas87DDA3A9(char* self, void*, void* occupant)
{
    return TestInterface(self, occupant, 0x87DDA3A9);
}

// What the next three share: the occupant's lot (lot manager slot 14) has
// this wealth (lot slot 41).
uint8_t TestLotWealth(char* self, void* occupant, uint32_t wealth)
{
    if (Asked(self, occupant))
        return Kept(self);
    void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x38, occupant);
    if (lot) {
        AddRef(lot);
        if (Virtual<uint32_t>(lot, 0xA4) == wealth) {
            Keep(self, occupant, true);
            Release(lot);
            return 1;
        }
    }
    Keep(self, occupant, false);
    if (lot)
        Release(lot);
    return 0;
}

// The game's 0x004E2EF0: wealth 1.
uint8_t __fastcall OccupantLotWealth1(char* self, void*, void* occupant)
{
    return TestLotWealth(self, occupant, 1);
}

// The game's 0x004E2F60: wealth 2.
uint8_t __fastcall OccupantLotWealth2(char* self, void*, void* occupant)
{
    return TestLotWealth(self, occupant, 2);
}

// The game's 0x004E2FD0: wealth 3.
uint8_t __fastcall OccupantLotWealth3(char* self, void*, void* occupant)
{
    return TestLotWealth(self, occupant, 3);
}

// The game's 0x004E3040: the occupant's lot does not answer 1 at slot 35.
uint8_t __fastcall OccupantLotNot1(char* self, void*, void* occupant)
{
    if (Asked(self, occupant))
        return Kept(self);
    void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x38, occupant);
    if (lot) {
        AddRef(lot);
        if (Virtual<uint32_t>(lot, 0x8C) != 1) {
            Keep(self, occupant, true);
            Release(lot);
            return 1;
        }
    }
    Keep(self, occupant, false);
    if (lot)
        Release(lot);
    return 0;
}

// --- State conditions -----------------------------------------------------------------
//
// Test(), thiscall, no arguments; `this` is not used.

// The game's 0x004E2AA0: the lighting manager's slot 9 (its byte +0x24).
uint32_t __fastcall StateLighting(char*, void*)
{
    return Virtual<uint32_t>(Global<void*>(addr::SC4_Transform43DDC), 0x24);
}

// The game's 0x004E2AB0: not that.
uint32_t __fastcall StateNotLighting(char*, void*)
{
    return Virtual<uint8_t>(Global<void*>(addr::SC4_Transform43DDC), 0x24) == 0 ? 1 : 0;
}

uint32_t ZoomIs(int32_t wanted)
{
    int32_t zoom;
    int32_t rotation;
    reinterpret_cast<void(__thiscall*)(void*, int32_t*, int32_t*)>(kCameraGetZoomAndRotation)(
        Global<void*>(addr::SC4_CameraView), &zoom, &rotation);
    return zoom == wanted ? 1 : 0;
}

// The game's 0x004E2AD0: the camera view's zoom is 4.
uint32_t __fastcall StateZoom4(char*, void*)
{
    return ZoomIs(4);
}

// The game's 0x004E2B00: zoom 3.
uint32_t __fastcall StateZoom3(char*, void*)
{
    return ZoomIs(3);
}

// The game's 0x004E2B30: zoom 2.
uint32_t __fastcall StateZoom2(char*, void*)
{
    return ZoomIs(2);
}

// The game's 0x004E2B60: zoom 1.
uint32_t __fastcall StateZoom1(char*, void*)
{
    return ZoomIs(1);
}

// The game's 0x004E2B90: zoom 0.
uint32_t __fastcall StateZoom0(char*, void*)
{
    return ZoomIs(0);
}

// The city view's slot 22 (its dword +0x324).
uint32_t ViewModeIs(uint32_t wanted)
{
    return Virtual<uint32_t>(Global<void*>(addr::SC4_Service43DC4), 0x58) == wanted ? 1 : 0;
}

// The game's 0x004E2BC0: the city view's slot 22 answers 3.
uint32_t __fastcall StateView3(char*, void*)
{
    return ViewModeIs(3);
}

// The game's 0x004E2BE0: 2.
uint32_t __fastcall StateView2(char*, void*)
{
    return ViewModeIs(2);
}

// The game's 0x004E2C00: 1.
uint32_t __fastcall StateView1(char*, void*)
{
    return ViewModeIs(1);
}

// The game's 0x004E2C20: the simulator's slot 15 says yes and its slot 17
// (emergency pause) no.
uint32_t __fastcall StatePausedNotEmergency(char*, void*)
{
    void* const simulator = Global<void*>(addr::SC4_SimulatorService);
    if (Virtual<uint8_t>(simulator, 0x3C) && !Virtual<uint8_t>(simulator, 0x44))
        return 1;
    return 0;
}

// The simulator's slot 27 (its dword +0x3C, the speed).
uint32_t SpeedIs(uint32_t wanted)
{
    return Virtual<uint32_t>(Global<void*>(addr::SC4_SimulatorService), 0x6C) == wanted ? 1 : 0;
}

// The game's 0x004E2C50: the simulator's slot 27 answers 0.
uint32_t __fastcall StateSpeed0(char*, void*)
{
    return SpeedIs(0);
}

// The game's 0x004E2C70: 1.
uint32_t __fastcall StateSpeed1(char*, void*)
{
    return SpeedIs(1);
}

// The game's 0x004E2C90: 2.
uint32_t __fastcall StateSpeed2(char*, void*)
{
    return SpeedIs(2);
}

// The game's 0x004E2CB0: the vehicle manager's slot 13.
uint32_t __fastcall StateVehicles(char*, void*)
{
    return Virtual<uint32_t>(Global<void*>(addr::SC4_Service43D44), 0x34);
}

// --- Cell conditions ------------------------------------------------------------------
//
// Test(x, z), thiscall, ret 8. The same cell as last time gets the answer
// kept at +0xC.

namespace cell {
constexpr std::size_t kX = 0x4;
constexpr std::size_t kZ = 0x8;
constexpr std::size_t kAnswer = 0xC;
} // namespace cell

bool AskedCell(char* self, int32_t x, int32_t z)
{
    return x == At<int32_t>(self, cell::kX) && z == At<int32_t>(self, cell::kZ);
}

uint8_t KeptCell(char* self)
{
    return At<uint8_t>(self, cell::kAnswer);
}

uint8_t KeepCell(char* self, int32_t x, int32_t z, bool answer)
{
    At<int32_t>(self, cell::kX) = x;
    At<int32_t>(self, cell::kZ) = z;
    At<uint8_t>(self, cell::kAnswer) = answer ? 1 : 0;
    return answer ? 1 : 0;
}

// The zone manager's zone type of a cell (slot 12).
uint32_t ZoneAt(int32_t x, int32_t z)
{
    return Virtual<uint32_t>(Global<void*>(addr::SC4_ZoneManager), 0x30, x, z);
}

// The game's 0x004E30B0: the cell has no zone (type 0).
uint8_t __fastcall CellZoneNone(char* self, void*, int32_t x, int32_t z)
{
    if (AskedCell(self, x, z))
        return KeptCell(self);
    return KeepCell(self, x, z, ZoneAt(x, z) == 0);
}

// The game's 0x004E3100: the occupant manager finds no occupant at the cell
// (slot 33). The place the found occupant goes to is the first argument's;
// one found is released.
uint8_t __fastcall CellEmpty(char* self, void*, int32_t x, int32_t z)
{
    if (AskedCell(self, x, z))
        return KeptCell(self);
    void* found = reinterpret_cast<void*>(static_cast<std::uintptr_t>(static_cast<uint32_t>(x)));
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_NetworkService), 0x84, &found, x, z, uint32_t{0}))
        return KeepCell(self, x, z, true);
    Release(found);
    return KeepCell(self, x, z, false);
}

// What the next six share: the zone developer's slot 8 (a network of these
// kinds at the cell). The original asks twice: the first answer is kept as
// it comes, the second returned.
uint8_t TestNetwork(char* self, int32_t x, int32_t z, uint32_t kinds)
{
    if (AskedCell(self, x, z))
        return KeptCell(self);
    At<int32_t>(self, cell::kX) = x;
    At<int32_t>(self, cell::kZ) = z;
    At<uint8_t>(self, cell::kAnswer) = Virtual<uint8_t>(Global<void*>(addr::SC4_Service43DB0), 0x20, x, z, kinds);
    return Virtual<uint8_t>(Global<void*>(addr::SC4_Service43DB0), 0x20, x, z, kinds);
}

// The game's 0x004E3170: kinds 0x1.
uint8_t __fastcall CellNetwork01(char* self, void*, int32_t x, int32_t z)
{
    return TestNetwork(self, x, z, 0x1);
}

// The game's 0x004E31C0: kinds 0x48.
uint8_t __fastcall CellNetwork48(char* self, void*, int32_t x, int32_t z)
{
    return TestNetwork(self, x, z, 0x48);
}

// The game's 0x004E3210: kinds 0x4.
uint8_t __fastcall CellNetwork04(char* self, void*, int32_t x, int32_t z)
{
    return TestNetwork(self, x, z, 0x4);
}

// The game's 0x004E3260: kinds 0x2.
uint8_t __fastcall CellNetwork02(char* self, void*, int32_t x, int32_t z)
{
    return TestNetwork(self, x, z, 0x2);
}

// The game's 0x004E32B0: kinds 0x80.
uint8_t __fastcall CellNetwork80(char* self, void*, int32_t x, int32_t z)
{
    return TestNetwork(self, x, z, 0x80);
}

// The game's 0x004E3310: kinds 0x10.
uint8_t __fastcall CellNetwork10(char* self, void*, int32_t x, int32_t z)
{
    return TestNetwork(self, x, z, 0x10);
}

// The game's 0x004E3360: the zone developer's slot 9 (the cell is water),
// asked twice in the same way.
uint8_t __fastcall CellWater(char* self, void*, int32_t x, int32_t z)
{
    if (AskedCell(self, x, z))
        return KeptCell(self);
    At<int32_t>(self, cell::kX) = x;
    At<int32_t>(self, cell::kZ) = z;
    At<uint8_t>(self, cell::kAnswer) = Virtual<uint8_t>(Global<void*>(addr::SC4_Service43DB0), 0x24, x, z);
    return Virtual<uint8_t>(Global<void*>(addr::SC4_Service43DB0), 0x24, x, z);
}

uint8_t TestZoneRange(char* self, int32_t x, int32_t z, uint32_t first)
{
    if (AskedCell(self, x, z))
        return KeptCell(self);
    const uint32_t zone = ZoneAt(x, z);
    return KeepCell(self, x, z, zone == first || zone == first + 1 || zone == first + 2);
}

uint8_t TestZone(char* self, int32_t x, int32_t z, uint32_t wanted)
{
    if (AskedCell(self, x, z))
        return KeptCell(self);
    return KeepCell(self, x, z, ZoneAt(x, z) == wanted);
}

// The game's 0x004E33B0: zone type 1, 2 or 3 (residential).
uint8_t __fastcall CellZoneResidential(char* self, void*, int32_t x, int32_t z)
{
    return TestZoneRange(self, x, z, 1);
}

// The game's 0x004E3420: 4, 5 or 6 (commercial).
uint8_t __fastcall CellZoneCommercial(char* self, void*, int32_t x, int32_t z)
{
    return TestZoneRange(self, x, z, 4);
}

// The game's 0x004E3490: 7, 8 or 9 (industrial).
uint8_t __fastcall CellZoneIndustrial(char* self, void*, int32_t x, int32_t z)
{
    return TestZoneRange(self, x, z, 7);
}

// The game's 0x004E3500: zone type 0xB.
uint8_t __fastcall CellZone0B(char* self, void*, int32_t x, int32_t z)
{
    return TestZone(self, x, z, 0xB);
}

// The game's 0x004E3560: 0xC.
uint8_t __fastcall CellZone0C(char* self, void*, int32_t x, int32_t z)
{
    return TestZone(self, x, z, 0xC);
}

// The game's 0x004E35C0: 0xD.
uint8_t __fastcall CellZone0D(char* self, void*, int32_t x, int32_t z)
{
    return TestZone(self, x, z, 0xD);
}

// The game's 0x004E3620: 0xE.
uint8_t __fastcall CellZone0E(char* self, void*, int32_t x, int32_t z)
{
    return TestZone(self, x, z, 0xE);
}

// What the lot tests share: the lot at the cell answers `wanted` at a slot.
uint8_t TestLotValue(char* self, int32_t x, int32_t z, std::size_t slot, uint32_t wanted)
{
    if (AskedCell(self, x, z))
        return KeptCell(self);
    void* const lot = LotAt(x, z);
    return KeepCell(self, x, z, lot && Virtual<uint32_t>(lot, slot) == wanted);
}

// The game's 0x004E3680: the lot's state (slot 33) is 2.
uint8_t __fastcall CellLotState2(char* self, void*, int32_t x, int32_t z)
{
    return TestLotValue(self, x, z, 0x84, 2);
}

// The game's 0x004E36F0: the lot's wealth (slot 41) is 1.
uint8_t __fastcall CellLotWealth1(char* self, void*, int32_t x, int32_t z)
{
    return TestLotValue(self, x, z, 0xA4, 1);
}

// The game's 0x004E3760: wealth 2.
uint8_t __fastcall CellLotWealth2(char* self, void*, int32_t x, int32_t z)
{
    return TestLotValue(self, x, z, 0xA4, 2);
}

// The game's 0x004E37D0: wealth 3.
uint8_t __fastcall CellLotWealth3(char* self, void*, int32_t x, int32_t z)
{
    return TestLotValue(self, x, z, 0xA4, 3);
}

// What the three density tests share: the lot at the cell, its
// configuration (lot slot 9) held while its byte at slot 11 and the lot's
// zone type (slot 31) are read; `test` gets the byte and whether the zone
// is industrial (7, 8 or 9).
template <typename Test>
uint8_t TestDensity(char* self, int32_t x, int32_t z, Test test)
{
    if (AskedCell(self, x, z))
        return KeptCell(self);
    if (void* const lot = LotAt(x, z)) {
        if (void* const configuration = Virtual<void*>(lot, 0x24)) {
            AddRef(configuration);
            const uint8_t stage = Virtual<uint8_t>(configuration, 0x2C);
            const uint32_t zone = Virtual<uint32_t>(lot, 0x7C);
            if (test(stage, zone == 7 || zone == 8 || zone == 9)) {
                KeepCell(self, x, z, true);
                Release(configuration);
                return 1;
            }
            Release(configuration);
        }
    }
    return KeepCell(self, x, z, false);
}

// The game's 0x004E3840: an industrial lot's byte is 0, or any lot's is
// under 3.
uint8_t __fastcall CellDensityLow(char* self, void*, int32_t x, int32_t z)
{
    return TestDensity(self, x, z,
                       [](uint8_t stage, bool industrial) { return (industrial && stage < 1) || stage < 3; });
}

// The game's 0x004E3920: an industrial lot's byte is 1, or any lot's is 3
// to 6.
uint8_t __fastcall CellDensityMedium(char* self, void*, int32_t x, int32_t z)
{
    return TestDensity(self, x, z, [](uint8_t stage, bool industrial) {
        return (industrial && stage >= 1 && stage < 2) || (stage >= 3 && stage < 7);
    });
}

// The game's 0x004E3A10: an industrial lot's byte is 2 or more, or any
// lot's is 7 or more.
uint8_t __fastcall CellDensityHigh(char* self, void*, int32_t x, int32_t z)
{
    return TestDensity(self, x, z,
                       [](uint8_t stage, bool industrial) { return (industrial && stage >= 2) || stage >= 7; });
}

// What the grid tests share: a simulator's byte grid (the simulator's slot
// `getter`), read at the cell (grid slot 7) and tested against a range.
// False without the simulator.
uint8_t TestGrid(char* self, int32_t x, int32_t z, std::uintptr_t global, std::size_t getter, uint32_t low,
                 uint32_t high)
{
    if (AskedCell(self, x, z))
        return KeptCell(self);
    bool answer = false;
    if (void* const simulator = Global<void*>(global)) {
        void* const grid = Virtual<void*>(simulator, getter);
        const uint32_t value = Virtual<uint8_t>(grid, 0x1C, x, z);
        answer = value >= low && value < high;
    }
    return KeepCell(self, x, z, answer);
}

// The game's 0x004E3AF0: the crime simulator's grid (slot 5) is 200 or more
// at the cell.
uint8_t __fastcall CellCrimeHigh(char* self, void*, int32_t x, int32_t z)
{
    return TestGrid(self, x, z, addr::SC4_Service43D70, 0x14, 0xC8, 0x100);
}

// The game's 0x004E3B60: 120 to 199.
uint8_t __fastcall CellCrimeMedium(char* self, void*, int32_t x, int32_t z)
{
    return TestGrid(self, x, z, addr::SC4_Service43D70, 0x14, 0x78, 0xC8);
}

// The game's 0x004E3BD0: under 120.
uint8_t __fastcall CellCrimeLow(char* self, void*, int32_t x, int32_t z)
{
    return TestGrid(self, x, z, addr::SC4_Service43D70, 0x14, 0, 0x78);
}

// The game's 0x004E3C40: the land value simulator's grid (slot 8) is under
// 140 at the cell.
uint8_t __fastcall CellLandValueLow(char* self, void*, int32_t x, int32_t z)
{
    return TestGrid(self, x, z, addr::SC4_Service43D88, 0x20, 0, 0x8C);
}

// The game's 0x004E3CB0: 140 to 199.
uint8_t __fastcall CellLandValueMedium(char* self, void*, int32_t x, int32_t z)
{
    return TestGrid(self, x, z, addr::SC4_Service43D88, 0x20, 0x8C, 0xC8);
}

// The game's 0x004E3D20: 200 or more.
uint8_t __fastcall CellLandValueHigh(char* self, void*, int32_t x, int32_t z)
{
    return TestGrid(self, x, z, addr::SC4_Service43D88, 0x20, 0xC8, 0x100);
}

// The game's 0x004E3D90: the lot at the cell answers 2, 3, 5 or 6 at slot
// 35.
uint8_t __fastcall CellLot2356(char* self, void*, int32_t x, int32_t z)
{
    if (AskedCell(self, x, z))
        return KeptCell(self);
    void* const lot = LotAt(x, z);
    return KeepCell(self, x, z, lot && Is2356(Virtual<uint32_t>(lot, 0x8C)));
}

// The game's 0x004E3E20: the lot at the cell has a building (slot 48).
uint8_t __fastcall CellLotBuilding(char* self, void*, int32_t x, int32_t z)
{
    if (AskedCell(self, x, z))
        return KeptCell(self);
    void* const lot = LotAt(x, z);
    return KeepCell(self, x, z, lot && Virtual<void*>(lot, 0xC0) != nullptr);
}

// The game's 0x004E3E90: the lot at the cell answers 4 at slot 35.
uint8_t __fastcall CellLot4(char* self, void*, int32_t x, int32_t z)
{
    return TestLotValue(self, x, z, 0x8C, 4);
}

// The game's 0x004E3F00: the building of the lot at the cell, taken as an
// occupant (its slot 3), is of type 0x1702.
uint8_t __fastcall CellBuilding1702(char* self, void*, int32_t x, int32_t z)
{
    if (AskedCell(self, x, z))
        return KeptCell(self);
    bool answer = false;
    if (void* const lot = LotAt(x, z)) {
        if (void* const building = Virtual<void*>(lot, 0xC0))
            answer = IsOfType(Virtual<void*>(building, 0xC), 0x1702);
    }
    return KeepCell(self, x, z, answer);
}

// --- Registration ---------------------------------------------------------------------

bool InstallAudio1(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[128] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4Audio_F4E1320, reinterpret_cast<void*>(&StopEvents)},
        {addr::SC4Audio_F4E13F0, reinterpret_cast<void*>(&ApplyVolume)},
        {addr::SC4Audio_F4E1430, reinterpret_cast<void*>(&QueryInterface)},
        {addr::SC4Audio_F4E14B0, reinterpret_cast<void*>(&OnSoundMessage)},
        {addr::SC4Audio_F4E1530, reinterpret_cast<void*>(&SetVolume0)},
        {addr::SC4Audio_F4E15A0, reinterpret_cast<void*>(&SetVolume1)},
        {addr::SC4Audio_F4E1610, reinterpret_cast<void*>(&SetVolume2)},
        {addr::SC4Audio_F4E1680, reinterpret_cast<void*>(&OnViewMessage)},
        {addr::SC4Audio_F4E16F0, reinterpret_cast<void*>(&StartEvent)},
        {addr::SC4Audio_F4E1740, reinterpret_cast<void*>(&StopEvent)},
        {addr::SC4Audio_F4E1790, reinterpret_cast<void*>(&RemoveBank)},
        {addr::SC4Audio_F4E1840, reinterpret_cast<void*>(&RemoveBanks)},
        {addr::SC4Audio_F4E18B0, reinterpret_cast<void*>(&UnloadBank)},
        {addr::SC4Audio_F4E1920, reinterpret_cast<void*>(&Destruct)},
        {addr::SC4Audio_F4E19C0, reinterpret_cast<void*>(&AddRefService)},
        {addr::SC4Audio_F4E19D0, reinterpret_cast<void*>(&ReleaseService)},
        {addr::SC4Audio_F4E19E0, reinterpret_cast<void*>(&SoundQueryInterface)},
        {addr::SC4Audio_F4E19F0, reinterpret_cast<void*>(&SoundAddRef)},
        {addr::SC4Audio_F4E1A00, reinterpret_cast<void*>(&SoundRelease)},
        {addr::SC4Audio_F4E1A10, reinterpret_cast<void*>(&TargetQueryInterface)},
        {addr::SC4Audio_F4E1A20, reinterpret_cast<void*>(&TargetAddRef)},
        {addr::SC4Audio_F4E1A30, reinterpret_cast<void*>(&TargetRelease)},
        {addr::SC4Audio_F4E1A40, reinterpret_cast<void*>(&ServiceQueryInterface)},
        {addr::SC4Audio_F4E1A50, reinterpret_cast<void*>(&ServiceAddRef)},
        {addr::SC4Audio_F4E1A60, reinterpret_cast<void*>(&ServiceRelease)},
        {addr::SC4Audio_F4E1A70, reinterpret_cast<void*>(&LastQueryInterface)},
        {addr::SC4Audio_F4E1A80, reinterpret_cast<void*>(&LastAddRef)},
        {addr::SC4Audio_F4E1A90, reinterpret_cast<void*>(&LastRelease)},
        {addr::SC4Audio_F4E1AA0, reinterpret_cast<void*>(&LastDeletingDestruct)},
        {addr::SC4Audio_F4E1AB0, reinterpret_cast<void*>(&Shutdown)},
        {addr::SC4Audio_F4E1BB0, reinterpret_cast<void*>(&AddBank)},
        {addr::SC4Audio_F4E1C20, reinterpret_cast<void*>(&Construct)},
        {addr::SC4Audio_F4E1D80, reinterpret_cast<void*>(&DeletingDestruct)},
        {addr::SC4Audio_F4E1DA0, reinterpret_cast<void*>(&LoadBankByKey)},
        {addr::SC4Audio_F4E1E10, reinterpret_cast<void*>(&LoadBank)},
        {addr::SC4Audio_F4E1EA0, reinterpret_cast<void*>(&ReadTuning)},
        {addr::SC4Audio_F4E2120, reinterpret_cast<void*>(&ReadEvents)},
        {addr::SC4Audio_F4E21A0, reinterpret_cast<void*>(&LoadEvents)},
        {addr::SC4Audio_F4E2210, reinterpret_cast<void*>(&Init)},
        {addr::SC4Audio_F4E2420, reinterpret_cast<void*>(&BankConstruct)},
        {addr::SC4Audio_F4E2450, reinterpret_cast<void*>(&BankRelease)},
        {addr::SC4Audio_F4E2490, reinterpret_cast<void*>(&BankRead)},
        {addr::SC4Audio_F4E24E0, reinterpret_cast<void*>(&BankLoad)},
        {addr::SC4Audio_F4E2570, reinterpret_cast<void*>(&OccupantIs4002)},
        {addr::SC4Audio_F4E25B0, reinterpret_cast<void*>(&OccupantIs4202Or4228Or4211)},
        {addr::SC4Audio_F4E2620, reinterpret_cast<void*>(&OccupantIs4224Or4225)},
        {addr::SC4Audio_F4E2680, reinterpret_cast<void*>(&OccupantIs4219)},
        {addr::SC4Audio_F4E26C0, reinterpret_cast<void*>(&OccupantIs420B)},
        {addr::SC4Audio_F4E2700, reinterpret_cast<void*>(&OccupantIs420C)},
        {addr::SC4Audio_F4E2740, reinterpret_cast<void*>(&OccupantIs420D)},
        {addr::SC4Audio_F4E2780, reinterpret_cast<void*>(&OccupantIs420E)},
        {addr::SC4Audio_F4E27C0, reinterpret_cast<void*>(&OccupantIs420A)},
        {addr::SC4Audio_F4E2800, reinterpret_cast<void*>(&OccupantIs4218)},
        {addr::SC4Audio_F4E2840, reinterpret_cast<void*>(&OccupantIs4005)},
        {addr::SC4Audio_F4E2880, reinterpret_cast<void*>(&OccupantIs4200Or420F)},
        {addr::SC4Audio_F4E28E0, reinterpret_cast<void*>(&OccupantIs4206)},
        {addr::SC4Audio_F4E2920, reinterpret_cast<void*>(&OccupantIs4205)},
        {addr::SC4Audio_F4E2960, reinterpret_cast<void*>(&OccupantIs4209)},
        {addr::SC4Audio_F4E29A0, reinterpret_cast<void*>(&OccupantIs4002Not4005)},
        {addr::SC4Audio_F4E2A00, reinterpret_cast<void*>(&OccupantIsClass6A11BD2B)},
        {addr::SC4Audio_F4E2A40, reinterpret_cast<void*>(&OccupantHasFlag69DBEBE1)},
        {addr::SC4Audio_F4E2AA0, reinterpret_cast<void*>(&StateLighting)},
        {addr::SC4Audio_F4E2AB0, reinterpret_cast<void*>(&StateNotLighting)},
        {addr::SC4Audio_F4E2AD0, reinterpret_cast<void*>(&StateZoom4)},
        {addr::SC4Audio_F4E2B00, reinterpret_cast<void*>(&StateZoom3)},
        {addr::SC4Audio_F4E2B30, reinterpret_cast<void*>(&StateZoom2)},
        {addr::SC4Audio_F4E2B60, reinterpret_cast<void*>(&StateZoom1)},
        {addr::SC4Audio_F4E2B90, reinterpret_cast<void*>(&StateZoom0)},
        {addr::SC4Audio_F4E2BC0, reinterpret_cast<void*>(&StateView3)},
        {addr::SC4Audio_F4E2BE0, reinterpret_cast<void*>(&StateView2)},
        {addr::SC4Audio_F4E2C00, reinterpret_cast<void*>(&StateView1)},
        {addr::SC4Audio_F4E2C20, reinterpret_cast<void*>(&StatePausedNotEmergency)},
        {addr::SC4Audio_F4E2C50, reinterpret_cast<void*>(&StateSpeed0)},
        {addr::SC4Audio_F4E2C70, reinterpret_cast<void*>(&StateSpeed1)},
        {addr::SC4Audio_F4E2C90, reinterpret_cast<void*>(&StateSpeed2)},
        {addr::SC4Audio_F4E2CB0, reinterpret_cast<void*>(&StateVehicles)},
        {addr::SC4Audio_F4E2CC0, reinterpret_cast<void*>(&OccupantOnLot2356)},
        {addr::SC4Audio_F4E2D90, reinterpret_cast<void*>(&OccupantOnLot4)},
        {addr::SC4Audio_F4E2E10, reinterpret_cast<void*>(&OccupantHasC83DD057)},
        {addr::SC4Audio_F4E2E80, reinterpret_cast<void*>(&OccupantHas87DDA3A9)},
        {addr::SC4Audio_F4E2EF0, reinterpret_cast<void*>(&OccupantLotWealth1)},
        {addr::SC4Audio_F4E2F60, reinterpret_cast<void*>(&OccupantLotWealth2)},
        {addr::SC4Audio_F4E2FD0, reinterpret_cast<void*>(&OccupantLotWealth3)},
        {addr::SC4Audio_F4E3040, reinterpret_cast<void*>(&OccupantLotNot1)},
        {addr::SC4Audio_F4E30B0, reinterpret_cast<void*>(&CellZoneNone)},
        {addr::SC4Audio_F4E3100, reinterpret_cast<void*>(&CellEmpty)},
        {addr::SC4Audio_F4E3170, reinterpret_cast<void*>(&CellNetwork01)},
        {addr::SC4Audio_F4E31C0, reinterpret_cast<void*>(&CellNetwork48)},
        {addr::SC4Audio_F4E3210, reinterpret_cast<void*>(&CellNetwork04)},
        {addr::SC4Audio_F4E3260, reinterpret_cast<void*>(&CellNetwork02)},
        {addr::SC4Audio_F4E32B0, reinterpret_cast<void*>(&CellNetwork80)},
        {addr::SC4Audio_F4E3310, reinterpret_cast<void*>(&CellNetwork10)},
        {addr::SC4Audio_F4E3360, reinterpret_cast<void*>(&CellWater)},
        {addr::SC4Audio_F4E33B0, reinterpret_cast<void*>(&CellZoneResidential)},
        {addr::SC4Audio_F4E3420, reinterpret_cast<void*>(&CellZoneCommercial)},
        {addr::SC4Audio_F4E3490, reinterpret_cast<void*>(&CellZoneIndustrial)},
        {addr::SC4Audio_F4E3500, reinterpret_cast<void*>(&CellZone0B)},
        {addr::SC4Audio_F4E3560, reinterpret_cast<void*>(&CellZone0C)},
        {addr::SC4Audio_F4E35C0, reinterpret_cast<void*>(&CellZone0D)},
        {addr::SC4Audio_F4E3620, reinterpret_cast<void*>(&CellZone0E)},
        {addr::SC4Audio_F4E3680, reinterpret_cast<void*>(&CellLotState2)},
        {addr::SC4Audio_F4E36F0, reinterpret_cast<void*>(&CellLotWealth1)},
        {addr::SC4Audio_F4E3760, reinterpret_cast<void*>(&CellLotWealth2)},
        {addr::SC4Audio_F4E37D0, reinterpret_cast<void*>(&CellLotWealth3)},
        {addr::SC4Audio_F4E3840, reinterpret_cast<void*>(&CellDensityLow)},
        {addr::SC4Audio_F4E3920, reinterpret_cast<void*>(&CellDensityMedium)},
        {addr::SC4Audio_F4E3A10, reinterpret_cast<void*>(&CellDensityHigh)},
        {addr::SC4Audio_F4E3AF0, reinterpret_cast<void*>(&CellCrimeHigh)},
        {addr::SC4Audio_F4E3B60, reinterpret_cast<void*>(&CellCrimeMedium)},
        {addr::SC4Audio_F4E3BD0, reinterpret_cast<void*>(&CellCrimeLow)},
        {addr::SC4Audio_F4E3C40, reinterpret_cast<void*>(&CellLandValueLow)},
        {addr::SC4Audio_F4E3CB0, reinterpret_cast<void*>(&CellLandValueMedium)},
        {addr::SC4Audio_F4E3D20, reinterpret_cast<void*>(&CellLandValueHigh)},
        {addr::SC4Audio_F4E3D90, reinterpret_cast<void*>(&CellLot2356)},
        {addr::SC4Audio_F4E3E20, reinterpret_cast<void*>(&CellLotBuilding)},
        {addr::SC4Audio_F4E3E90, reinterpret_cast<void*>(&CellLot4)},
        {addr::SC4Audio_F4E3F00, reinterpret_cast<void*>(&CellBuilding1702)},
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

const bool g_audio1Registered = registry::Register({
    "audio.part1",
    "audio",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Audio_F4E1320,
    "Our sound event system (banks, events, volume, tuning) and the audio conditions (docs/audio.md)",
    false,
    InstallAudio1,
});

} // namespace
} // namespace opensc4::audio
