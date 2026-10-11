// The audio code, part 4 (0x004E9810 to 0x004EC3D0): the second half of the
// ambience manager, and the audio code's COM director with its class
// factories.
//
// cSC4AudioAmbienceManager (0x120 bytes, class and interface ID 0x6A12F51C,
// service ID 0xAA12F911, constructor 0x004EA390, factory 0x004EB0C0). Four
// vtables: +0 the system service (0x00A9378C, 14 slots: Init 0x004E84D0,
// Shutdown 0x004E96A0, OnTick 0x004EA150), +4 a message target (0x00A9377C,
// DoMessage 0x004EAF20), +8 the audio managers' start and stop interface
// (0x00A93768: slot 3 Init, slot 4 Shutdown), +0xC an event target
// (0x00A93758, DoEvent 0x004E9870). Its QueryInterface answers only for the
// object itself; the three other parts are handed out by address.
//
// What this half does:
//
//   - the city's start (message 0x26D31EC1): the ambience layers are made
//     from the list of layer IDs (one 0x25C-byte cSC4AudioAmbienceLayer
//     each, given the ambience volume of the preferences), the occupant
//     manager is taken, the city's corners are worked out, the tract walk
//     is restarted, the manager joins the framework's tick and every
//     building, prop and flora occupant of the city is looked at once;
//   - the city's end (message 0x26D31EC2) undoes that;
//   - an occupant put in, taken out or changed (messages 0x99EF1142,
//     0x99EF1143, 0x99EF1146) and a lot whose state changed (0xE99B3D4D)
//     start and stop the sounds the occupants' properties name;
//   - the tick (real time) walks the city's tracts a slice at a time for
//     the layers: state 0 or 1 starts a walk, 2 walks, 3 finishes;
//   - sound events (the +0xC target) work the sound object the event
//     carries: play, stop, volume and the like.
//
// The audio director (cRZCOMDllDirector's layout, 0x6C bytes, ID
// 0xFDB5CA17, vtables 0x00A93E18 and at +4 the framework hooks 0x00A93DE8,
// constructor 0x004EC710 in the next part) registers every audio class.
// Here: its eight factories of the large classes, the 65 factories of the
// event handlers (two shapes, 0x10 and 0x14 bytes, each a six-slot vtable
// in the run from 0x00A937D0), its start (the framework's sound directors
// are added to the application's director), the hook after the
// application's start (the sound configuration is read, the sound service
// and the seven audio objects are made and started) and the hook before
// its shutdown (they are stopped and let go), and its destructor.
//
// Replacement, off by default:
//
//   audio.part4: everything above.
//
// Left the game's and called by address: the framework's sound code
// (the sound service and data source classes at 0x008D35AB and 0x008D376E,
// the configuration reader at 0x0093638E, the five directors), the
// ambience layers' and the manager's first half (0x004E82B0 to 0x004E9810,
// part 3), the occupant, lot and property code.
//
// The manager (names ours): +0x10 reference count, +0x14 service ID,
// +0x18 and +0x19 the service's flags, +0x1C and +0x20 two objects set in
// part 3, +0x24 the occupant manager, +0x28 the occupant type filter (8
// bytes, vtable 0x00A93640), +0x2C the list of layers, +0x34 to +0x48 six
// dwords, +0x4C the list of timed sounds (0x28-byte nodes), +0x50 "in a
// city", +0x54 and +0x60 two vectors (+0x60 the layer IDs), +0x6C to +0x78
// the tract rectangle doubled, +0x8C what the layers are given at a walk's
// start, +0x9C to +0xA8 the city's corners, +0xAC a flag, +0xB0 the walk's
// state, +0xB4 the tract walk, +0xCC a buffer, +0xE0 a dword of 1, +0xE8
// and +0x100 two stopwatches, +0x118 an occupant filter, +0x11C the count
// of occupants it passes.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <utility>

namespace opensc4::audio {
namespace {

namespace mgr { // the ambience manager
constexpr std::size_t kTarget = 0x04;
constexpr std::size_t kStarter = 0x08;
constexpr std::size_t kEvents = 0x0C;
constexpr std::size_t kRefCount = 0x10;
constexpr std::size_t kServiceId = 0x14;
constexpr std::size_t kFlag18 = 0x18;
constexpr std::size_t kRunning = 0x19;
constexpr std::size_t kObject1C = 0x1C;
constexpr std::size_t kObject20 = 0x20;
constexpr std::size_t kOccupantManager = 0x24;
constexpr std::size_t kTypeFilter = 0x28;
constexpr std::size_t kLayers = 0x2C;       // list head
constexpr std::size_t kField30 = 0x30;
constexpr std::size_t kSix = 0x34;          // six dwords
constexpr std::size_t kTimed = 0x4C;        // list head
constexpr std::size_t kInCity = 0x50;       // byte
constexpr std::size_t kVector54 = 0x54;
constexpr std::size_t kLayerIds = 0x60;     // vector of uint32
constexpr std::size_t kRect = 0x6C;         // four int32
constexpr std::size_t kWalkData = 0x8C;
constexpr std::size_t kCornerLow = 0x9C;    // two dwords
constexpr std::size_t kCornerHigh = 0xA4;   // two dwords
constexpr std::size_t kFlagAC = 0xAC;
constexpr std::size_t kState = 0xB0;
constexpr std::size_t kWalk = 0xB4;
constexpr std::size_t kBufferCC = 0xCC;
constexpr std::size_t kFieldE0 = 0xE0;
constexpr std::size_t kSliceWatch = 0xE8;
constexpr std::size_t kTickWatch = 0x100;
constexpr std::size_t kFilter = 0x118;
constexpr std::size_t kPassed = 0x11C;
} // namespace mgr

namespace dir { // the audio director
constexpr std::size_t kHooks = 0x04;
constexpr std::size_t kObject4C = 0x4C;     // class 0xBDB6171E
constexpr std::size_t kObject50 = 0x50;     // class 0x69E500BC
constexpr std::size_t kAmbience = 0x54;     // class 0x6A12F51C
constexpr std::size_t kObject5C = 0x5C;     // class 0xAA355871
constexpr std::size_t kSounds = 0x60;       // class 0x4A47DC0D
constexpr std::size_t kObject64 = 0x64;     // class 0xCA4A66B9
constexpr std::size_t kObject68 = 0x68;     // class 0x29E2797F
} // namespace dir

constexpr uint32_t kMessageStandard = 0x65297976;
constexpr uint32_t kOccupantIid = 0x26BB767E;
constexpr uint32_t kBuildingIid = 0x87DDA3A9;
constexpr uint32_t kPropIid = 0xE9793A65;
constexpr uint32_t kLotIid = 0xE818FCED;
constexpr uint32_t kSoundIid = 0xDC1CF595;
constexpr uint32_t kStarterIid = 0x89E51472;
constexpr uint32_t kSystemServiceIid = 0x287FB697;

// Occupant properties.
constexpr uint32_t kNoSoundProperty = 0x029244DB;    // bool
constexpr uint32_t kSoundA = 0x0A4C13CB;
constexpr uint32_t kSoundB = 0xCA19D7CA;
constexpr uint32_t kSoundC = 0x4A4C132E;
constexpr uint32_t kSoundLoop = 0x6A36AFAC;
constexpr uint32_t kSoundTimes = 0x6A36AFAB;         // float vector
constexpr uint32_t kTimedSoundA = 0x0A36AFA2;
constexpr uint32_t kTimedSoundB = 0x2A53BE13;
constexpr uint32_t kSoundOnce = 0xC9B93A56;

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

// A list node holding one pointer.
struct RefNode {
    RefNode* next;
    RefNode* previous;
    void* value;
};
static_assert(sizeof(RefNode) == 0xC);

struct FloatVector {
    float* begin;
    float* end;
    float* capacity;
};

// A timed sound of an occupant: what the list at +0x4C holds.
struct TimedSound {
    void* occupant;         // +0x00, a reference
    uint32_t sound;         // +0x04
    float madeAt;           // +0x08, the hour of the day
    int32_t count;          // +0x0C, the number of times
    FloatVector times;      // +0x10
    uint8_t playing;        // +0x1C
    uint8_t second;         // +0x1D
    uint8_t pad[2];
};
static_assert(sizeof(TimedSound) == 0x20);

struct TimedNode {
    TimedNode* next;
    TimedNode* previous;
    TimedSound sound;
};
static_assert(sizeof(TimedNode) == 0x28);

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
    return Virtual<uint32_t>(object, 0x4);
}

uint32_t Release(void* object)
{
    return Virtual<uint32_t>(object, 0x8);
}

// The inlined release of a local reference at a function's end.
void ReleaseIfSet(void* object)
{
    if (object)
        Release(object);
}

// The inlined "forget, then release" of a reference holder.
void Drop(void*& holder)
{
    if (void* const object = holder) {
        holder = nullptr;
        Release(object);
    }
}

// The framework, as the audio code caches it at 0x00B628AC.
void* Framework()
{
    void*& cached = Global<void*>(addr::SC4Audio_Framework);
    if (!cached)
        cached = Cdecl<void*>(addr::RZGetFramework);
    return cached;
}

bool Uint32Property(void* holder, uint32_t id, uint32_t* out)
{
    return Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, id, out) != 0;
}

uint8_t StopSound(char* self, void* occupant, uint32_t sound)
{
    return This<uint8_t>(addr::SC4Audio_F4E8BF0, self, occupant, sound);
}

uint8_t StartSound(char* self, void* occupant, uint32_t sound, uint32_t flag)
{
    return This<uint8_t>(addr::SC4Audio_F4E8AD0, self, occupant, sound, flag);
}

void __fastcall CreateAmbienceLayers(char* self, void*);
void __fastcall UpdateLotSounds(char* self, void*, void* lot, int32_t data1, int32_t condition);
uint8_t __fastcall RemoveTimedSound(char* self, void*, void* occupant, uint32_t sound);
void __fastcall OnOccupantFound(char* self, void*, void* occupant);
void __fastcall LookAtOccupants(char* self, void*);
char* __fastcall Construct(char* self, void*);

// --- The city's start and end ---------------------------------------------------------

// The game's 0x004E9810 (thiscall, no arguments; `this` is the manager).
// The city's end (message 0x26D31EC2): out of the tick, the layers and
// sounds dropped by 0x004E9660, the occupant manager and the type filter
// let go. Returns 0.
uint8_t __fastcall LeaveCity(char* self, void*)
{
    At<uint8_t>(self, mgr::kInCity) = 0;
    Virtual<uint8_t>(Framework(), 0x28, static_cast<void*>(self));
    This<void>(addr::SC4Audio_F4E9660, self);
    Drop(At<void*>(self, mgr::kOccupantManager));
    Drop(At<void*>(self, mgr::kTypeFilter));
    return 0;
}

// The game's 0x004E9C10 (thiscall, no arguments), by its own error text
// cSC4AudioAmbienceManager::_CreateAmbienceLayers. One layer per ID of the
// vector at +0x60: made, started (layer slot 4), given the ambience volume
// of the preferences (slot 17; 0 when the preferences' byte at +4 is
// clear) and appended to the list at +0x2C. A layer that does not start is
// reported and dropped. As in the original a failed allocation is followed
// by a call through the null pointer.
void __fastcall CreateAmbienceLayers(char* self, void*)
{
    const int32_t count =
        static_cast<int32_t>(At<uint32_t*>(self, mgr::kLayerIds + 4) - At<uint32_t*>(self, mgr::kLayerIds));
    for (int32_t index = 0; index < count; ++index) {
        void* layer = nullptr;
        if (void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x25C})) {
            layer = This<void*>(addr::SC4Audio_F4E7290, memory, At<uint32_t*>(self, mgr::kLayerIds)[index]);
            if (layer)
                AddRef(layer);
        }
        if (Virtual<uint8_t>(layer, 0x10)) {
            void* const app = Global<void*>(addr::SC4_Service43C94);
            if (Virtual<uint8_t*>(app, 0x98)[4]) {
                uint8_t* const preferences = Virtual<uint8_t*>(Global<void*>(addr::SC4_Service43C94), 0x98);
                Virtual<void>(layer, 0x44, *reinterpret_cast<uint32_t*>(preferences + 8));
            } else {
                Virtual<void>(layer, 0x44, uint32_t{0});
            }
            AddRef(layer);
            auto* const head = At<RefNode*>(self, mgr::kLayers);
            auto* const node = static_cast<RefNode*>(Cdecl<void*>(addr::SC4_stl_allocate, uint32_t{0xC}));
            node->value = layer;
            RefNode* const last = head->previous;
            node->next = head;
            node->previous = last;
            last->next = node;
            head->previous = node;
        } else {
            // An inlined string: made with an 8-byte buffer, printed into,
            // destroyed. Nothing reads the text.
            GameString text;
            text.body.begin = nullptr;
            text.body.end = nullptr;
            text.body.capacity = nullptr;
            char* const buffer = static_cast<char*>(Cdecl<void*>(addr::SC4_stl_allocate, uint32_t{8}));
            text.body.capacity = buffer + 8;
            text.body.begin = buffer;
            text.body.end = buffer;
            *buffer = 0;
            text.vtable = addr::SC4String_Vtable;
            text.zero = 0;
            reinterpret_cast<void(__cdecl*)(GameString*, const char*, ...)>(addr::cRZBaseString_Sprintf)(
                &text, "Error : cSC4AudioAmbienceManager::_CreateAmbienceLayers : Failed to init layer (%08x)\n",
                At<uint32_t*>(self, mgr::kLayerIds)[index]);
            char* const printed = text.body.begin;
            text.vtable = addr::SC4String_Vtable;
            if (printed)
                Cdecl<void>(addr::SC4_Free, static_cast<void*>(printed));
        }
        Release(layer);
    }
}

// The game's 0x004EAAA0 (cdecl: occupant, the manager). What the occupant
// manager calls for each occupant of the city at the city's start.
uint8_t __cdecl OccupantCallback(void* occupant, char* manager)
{
    if (!occupant || !manager)
        return 0;
    AddRef(manager);
    OnOccupantFound(manager, nullptr, occupant);
    Release(manager);
    return 1;
}

// The game's 0x004EAAE0 (thiscall, no arguments). Makes an occupant type
// filter for three types (0x278128A0, 0xA823821E, 0xC7CC75F1) and has the
// occupant manager call 0x004EAAA0 for every occupant that passes. As in
// the original a failed allocation is followed by a read through null.
void __fastcall LookAtOccupants(char* self, void*)
{
    char* filter = nullptr;
    if (char* const memory = static_cast<char*>(Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x18}))) {
        This<void*>(addr::SC4_OccupantFilterBase, memory);
        At<std::uintptr_t>(memory, 0x0) = addr::SC4OccupantTypeFilter_vftable;
        At<std::uintptr_t>(memory, 0x4) = addr::SC4OccupantTypeFilter_vftable_4;
        At<uint32_t*>(memory, 0xC) = nullptr;
        At<uint32_t*>(memory, 0x10) = nullptr;
        At<uint32_t*>(memory, 0x14) = nullptr;
        filter = memory;
        AddRef(filter);
    }
    char* const types = filter + 0xC;
    // The byte the game passes by address is never set there either.
    uint8_t tag = 0;
    for (const uint32_t type : {uint32_t{0x278128A0}, uint32_t{0xA823821E}, uint32_t{0xC7CC75F1}}) {
        uint32_t value = type;
        uint32_t* const end = At<uint32_t*>(types, 0x4);
        if (end != At<uint32_t*>(types, 0x8)) {
            if (end)
                *end = type;
            At<uint32_t*>(types, 0x4) += 1;
        } else {
            This<void>(addr::SC4PtrVector_InsertN, types, end, &value, &tag, uint32_t{1}, uint32_t{1});
        }
    }
    Virtual<void>(At<void*>(self, mgr::kOccupantManager), 0xB0, addr::SC4Audio_F4EAAA0, static_cast<void*>(self),
                  uint32_t{0}, uint32_t{0}, static_cast<void*>(filter));
    if (filter)
        Release(filter);
}

// The game's 0x004EABF0 (thiscall, no arguments). The city's start (message
// 0x26D31EC1). Returns 1. The city's first size (slot 93) is asked for and
// not used: both corners come from the second (slot 94).
uint8_t __fastcall EnterCity(char* self, void*)
{
    This<void>(addr::SC4Audio_F4E91D0, self);
    CreateAmbienceLayers(self, nullptr);

    void* const occupants = Global<void*>(addr::SC4_NetworkService);
    void* const oldOccupants = At<void*>(self, mgr::kOccupantManager);
    if (occupants != oldOccupants) {
        if (occupants)
            AddRef(occupants);
        At<void*>(self, mgr::kOccupantManager) = occupants;
        if (oldOccupants)
            Release(oldOccupants);
    }

    Virtual<uint32_t>(Global<void*>(addr::SC4_City), 0x174);
    const int32_t size = Virtual<int32_t>(Global<void*>(addr::SC4_City), 0x178);
    Virtual<void>(At<void*>(self, mgr::kOccupantManager), 0x48, int32_t{0}, int32_t{0},
                  static_cast<void*>(self + mgr::kCornerLow), static_cast<void*>(self + mgr::kCornerLow + 4));
    Virtual<void>(At<void*>(self, mgr::kOccupantManager), 0x48, size - 1, size - 1,
                  static_cast<void*>(self + mgr::kCornerHigh), static_cast<void*>(self + mgr::kCornerHigh + 4));
    At<uint8_t>(self, mgr::kFlagAC) = 1;
    This<void>(addr::SC4Audio_F4E92A0, self);

    char* filter = static_cast<char*>(Cdecl<void*>(addr::SC4_operator_new, uint32_t{8}));
    if (filter) {
        At<std::uintptr_t>(filter, 0x0) = addr::SC4AudioAmbienceManager_FilterVtable;
        At<uint32_t>(filter, 0x4) = 0;
    }
    void* const oldFilter = At<void*>(self, mgr::kTypeFilter);
    if (filter != oldFilter) {
        if (filter)
            AddRef(filter);
        At<void*>(self, mgr::kTypeFilter) = filter;
        if (oldFilter)
            Release(oldFilter);
    }

    char* const walk = self + mgr::kWalk;
    if (At<uint8_t>(walk, 0x1) != 1) {
        At<uint8_t>(walk, 0x1) = 1;
        At<uint8_t>(walk, 0x2) = 1;
    }
    This<void>(addr::TractIterator_Restart, walk, uint32_t{1});
    At<uint32_t>(self, mgr::kState) = 0;
    Virtual<uint8_t>(Framework(), 0x24, static_cast<void*>(self));
    LookAtOccupants(self, nullptr);
    At<uint8_t>(self, mgr::kInCity) = 1;
    return 1;
}

// --- The tick -------------------------------------------------------------------------

// The game's 0x004EA150 (thiscall, ret 4; service slot 10, OnTick). Real
// time: the stopwatch at +0x100 gives the milliseconds since the last tick,
// which 0x004E94E0 walks tracts by. The argument is not read. Returns 1.
uint8_t __fastcall OnTick(char* self, void*, uint32_t)
{
    if (!At<uint8_t>(self, mgr::kInCity))
        return 1;
    char* const watch = self + mgr::kTickWatch;
    const uint32_t elapsed = This<uint32_t>(addr::GZStopwatch_ElapsedMilliseconds, watch);
    This<void>(addr::GZStopwatch_Start, watch);
    This<void>(addr::SC4Audio_F4E8EA0, self);

    const uint32_t state = At<uint32_t>(self, mgr::kState);
    if (state > 3)
        return 1;
    if (state < 2) {
        int32_t rect[4];
        rect[0] = At<int32_t>(self, mgr::kRect + 0x0) >> 1;
        rect[1] = At<int32_t>(self, mgr::kRect + 0x4) >> 1;
        rect[2] = At<int32_t>(self, mgr::kRect + 0x8) >> 1;
        rect[3] = At<int32_t>(self, mgr::kRect + 0xC) >> 1;
        char* const walk = self + mgr::kWalk;
        This<void>(addr::TractIterator_SetBounds, walk, &rect[0]);
        This<void>(addr::TractIterator_Restart, walk, uint32_t{0});
        RefNode* const head = At<RefNode*>(self, mgr::kLayers);
        for (RefNode* node = head->next; node != head; node = node->next)
            Virtual<void>(node->value, 0x64, static_cast<void*>(self + mgr::kWalkData));
        At<uint32_t>(self, mgr::kState) = 2;
    }
    if (state < 3) {
        if (!This<uint8_t>(addr::SC4Audio_F4E94E0, self, elapsed))
            return 1;
        At<uint32_t>(self, mgr::kState) = 3;
    }
    RefNode* const head = At<RefNode*>(self, mgr::kLayers);
    for (RefNode* node = head->next; node != head; node = node->next)
        Virtual<void>(node->value, 0x6C);
    At<uint32_t>(self, mgr::kState) = 1;
    return 1;
}

// --- Lots and occupants ---------------------------------------------------------------

// The game's 0x004E9DA0 (thiscall, ret 0xC: lot, a number, the lot's
// condition or a negative number to ask the lot). For every building or
// prop occupant of the lot, starts or stops the sounds its properties name
// for that condition. The occupants come in a list the lot fills and the
// occupant manager empties.
void __fastcall UpdateLotSounds(char* self, void*, void* lot, int32_t data1, int32_t condition)
{
    if (!lot)
        return;
    if (condition < 0)
        condition = Virtual<int32_t>(lot, 0x8C);

    auto* head = static_cast<RefNode*>(Virtual<void*>(ListNodeAllocator(), 0xC, uint32_t{0xC}));
    head->next = head;
    head->previous = head;
    Virtual<void>(lot, 0xC4, &head);

    // The original's slot for a property's value is the lot argument's.
    uint32_t sound = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(lot));
    RefNode* const end = head;
    for (RefNode* node = head->next; node != end; node = node->next) {
        void* const occupant = node->value;
        void* building = nullptr;
        void* prop = nullptr;
        bool known = Virtual<uint8_t>(occupant, 0x0, kBuildingIid, &building) != 0;
        if (!known) {
            Drop(prop);
            known = Virtual<uint8_t>(occupant, 0x0, kPropIid, &prop) != 0;
        }
        if (known) {
            if (void* const holder = Virtual<void*>(occupant, 0x18)) {
                if (condition == 0 || condition == 5 || condition == 6) {
                    if (Uint32Property(holder, kSoundA, &sound))
                        StopSound(self, occupant, sound);
                    if (Uint32Property(holder, kSoundB, &sound))
                        StartSound(self, occupant, sound, 1);
                }
                if (condition == 2 || condition == 3) {
                    if (Uint32Property(holder, kSoundB, &sound))
                        StopSound(self, occupant, sound);
                    if (Uint32Property(holder, kSoundA, &sound))
                        StartSound(self, occupant, sound, 1);
                }
                if (condition == 4 || condition == 1) {
                    if (Uint32Property(holder, kSoundA, &sound))
                        StopSound(self, occupant, sound);
                    if (Uint32Property(holder, kSoundB, &sound))
                        StopSound(self, occupant, sound);
                }
                if (condition == 0 && data1 == 1) {
                    if (Uint32Property(holder, kSoundC, &sound))
                        StartSound(self, occupant, sound, 1);
                }
            }
        }
        ReleaseIfSet(prop);
        ReleaseIfSet(building);
    }

    Virtual<void>(Global<void*>(addr::SC4_NetworkService), 0xC0, &head);
    This<void>(addr::SC4List_Clear2, &head);
    Virtual<void>(ListNodeAllocator(), 0x10, static_cast<void*>(head));
}

// The game's 0x004EA080 (thiscall, ret 8: occupant, sound). Takes the
// occupant's timed sound out of the list at +0x4C, stopping it first when
// it plays. Returns whether there was one.
uint8_t __fastcall RemoveTimedSound(char* self, void*, void* occupant, uint32_t sound)
{
    if (!occupant)
        return 0;
    TimedNode* const head = At<TimedNode*>(self, mgr::kTimed);
    TimedNode* node = head->next;
    for (;; node = node->next) {
        if (node == head)
            return 0;
        if (node->sound.occupant == occupant)
            break;
    }
    if (node->sound.playing) {
        StopSound(self, node->sound.occupant, sound);
        node->sound.playing = 0;
    }
    Release(node->sound.occupant);
    TimedNode* const next = node->next;
    TimedNode* const previous = node->previous;
    previous->next = next;
    next->previous = previous;
    if (float* const buffer = node->sound.times.begin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(buffer));
    Cdecl<void>(addr::SC4_Free, static_cast<void*>(node));
    return 1;
}

// The game's 0x004EA100 (thiscall, ret 4; `this`, the list, is not read).
// A new node of the timed sounds' list holding a copy of the argument.
TimedNode* __fastcall NewTimedNode(void*, void*, TimedSound* from)
{
    auto* const node = static_cast<TimedNode*>(Cdecl<void*>(addr::SC4_stl_allocate, uint32_t{0x28}));
    TimedSound* const to = &node->sound;
    to->occupant = from->occupant;
    to->sound = from->sound;
    to->madeAt = from->madeAt;
    to->count = from->count;
    This<void*>(addr::SC4FloatVector_CopyConstruct, &to->times, &from->times);
    to->playing = from->playing;
    to->second = from->second;
    return node;
}

// The game's 0x004E9D60 (thiscall, no arguments; `this` is the list's
// head pointer). Frees every node of the timed sounds' list with its
// buffer; no reference is released.
void __fastcall ClearTimedList(TimedNode** self, void*)
{
    TimedNode* node = (*self)->next;
    while (node != *self) {
        TimedNode* const dead = node;
        float* const buffer = dead->sound.times.begin;
        node = node->next;
        if (buffer)
            Cdecl<void>(addr::SC4_Free, static_cast<void*>(buffer));
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(dead));
    }
    (*self)->next = *self;
    (*self)->previous = *self;
}

// The game's 0x004EA690 (thiscall, ret 0x10: occupant, sound, the times,
// a flag). Appends a timed sound for an occupant that has flag 5 and not
// flag 0x10: the hour of the day (the clock's slot 10 plus its slot 13
// over 60, both taken as unsigned) is kept with it. Returns whether it did.
// As in the original a missing clock is called through.
uint8_t __fastcall AddTimedSound(char* self, void*, void* occupant, uint32_t sound, FloatVector* times, uint8_t second)
{
    if (!occupant || !Virtual<uint8_t>(occupant, 0x80, uint32_t{5}) || Virtual<uint8_t>(occupant, 0x80, uint32_t{0x10}))
        return 0;
    AddRef(occupant);
    void* const clock = Global<void*>(addr::SC4_Clock43DBC);
    if (clock)
        AddRef(clock);
    const uint32_t hour = Virtual<uint32_t>(clock, 0x28);
    const uint32_t minute = Virtual<uint32_t>(clock, 0x34);

    // 0x20 bytes at [esp+0x14] of the original's frame.
    TimedSound entry;
    entry.occupant = occupant;
    entry.madeAt = 0.0f;
    entry.count = 0;
    entry.times.begin = nullptr;
    entry.times.end = nullptr;
    entry.times.capacity = nullptr;
    entry.playing = 0;
    entry.second = 0;
    entry.sound = sound;
    This<void*>(addr::SC4FloatVector_Assign, &entry.times, times);
    entry.count = static_cast<int32_t>(times->end - times->begin);
    entry.madeAt = static_cast<float>(static_cast<double>(minute / 60) + static_cast<double>(hour));
    TimedNode* const head = At<TimedNode*>(self, mgr::kTimed);
    entry.second = second;
    TimedNode* const node = NewTimedNode(self + mgr::kTimed, nullptr, &entry);
    TimedNode* const last = head->previous;
    node->next = head;
    node->previous = last;
    last->next = node;
    head->previous = node;
    if (float* const buffer = entry.times.begin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(buffer));
    Release(clock);
    return 1;
}

// The lot of an occupant from the lot manager, and its sounds brought up
// to date: the tail the two functions below share.
void UpdateLotOf(char* self, void* occupant)
{
    void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x38, occupant);
    UpdateLotSounds(self, nullptr, lot, -1, -1);
}

// The game's 0x004EA7D0 (thiscall, ret 4). An occupant was put in (message
// 0x99EF1142): unless its property 0x029244DB is set, it is counted when
// the filter at +0x118 passes it, its looping and one-off sounds are
// started, its timed sounds listed and its lot's sounds brought up to date.
void __fastcall OnOccupantAdded(char* self, void*, void* occupant)
{
    // The original uses the argument's slot for the flag.
    std::uintptr_t flag = reinterpret_cast<std::uintptr_t>(occupant);
    void* const first = Virtual<void*>(occupant, 0x18);
    if (Cdecl<uint8_t>(addr::SC4_GetPropertyBool, first, kNoSoundProperty, reinterpret_cast<uint8_t*>(&flag))
        && static_cast<uint8_t>(flag) != 0)
        return;
    if (Virtual<uint8_t>(At<void*>(self, mgr::kFilter), 0x14, occupant))
        ++At<uint32_t>(self, mgr::kPassed);
    void* const holder = Virtual<void*>(occupant, 0x18);
    if (!holder)
        return;

    uint32_t sound = 0;
    if (Uint32Property(holder, kSoundLoop, &sound))
        StartSound(self, occupant, sound, 1);
    FloatVector times = {nullptr, nullptr, nullptr};
    if (Cdecl<uint8_t>(addr::SC4_GetPropertyFloatVector, holder, kSoundTimes, &times)) {
        if (Uint32Property(holder, kTimedSoundA, &sound))
            AddTimedSound(self, nullptr, occupant, sound, &times, 0);
        if (Uint32Property(holder, kTimedSoundB, &sound))
            AddTimedSound(self, nullptr, occupant, sound, &times, 1);
    }
    if (Uint32Property(holder, kSoundOnce, &sound))
        StartSound(self, occupant, sound, 0);
    if (Uint32Property(holder, kSoundB, &sound) || Uint32Property(holder, kSoundA, &sound)
        || Uint32Property(holder, kSoundC, &sound))
        UpdateLotOf(self, occupant);
    if (times.begin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(times.begin));
}

// The game's 0x004EA970 (thiscall, ret 4). An occupant found in the city at
// its start: counted when the filter passes it, its timed sounds listed,
// its lot's sounds brought up to date.
void __fastcall OnOccupantFound(char* self, void*, void* occupant)
{
    if (Virtual<uint8_t>(At<void*>(self, mgr::kFilter), 0x14, occupant))
        ++At<uint32_t>(self, mgr::kPassed);
    void* const holder = Virtual<void*>(occupant, 0x18);
    if (!holder)
        return;

    uint32_t sound = 0;
    FloatVector times = {nullptr, nullptr, nullptr};
    if (Cdecl<uint8_t>(addr::SC4_GetPropertyFloatVector, holder, kSoundTimes, &times)) {
        if (Uint32Property(holder, kTimedSoundA, &sound))
            AddTimedSound(self, nullptr, occupant, sound, &times, 0);
        if (Uint32Property(holder, kTimedSoundB, &sound))
            AddTimedSound(self, nullptr, occupant, sound, &times, 1);
    }
    if (Uint32Property(holder, kSoundB, &sound) || Uint32Property(holder, kSoundA, &sound)
        || Uint32Property(holder, kSoundC, &sound))
        UpdateLotOf(self, occupant);
    if (times.begin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(times.begin));
}

// The game's 0x004EA270 (thiscall, ret 4; the splitter missed its start
// behind the tick's jump table). An occupant was taken out (message
// 0x99EF1143): unless its property 0x029244DB is set, it is uncounted when
// the filter passes it and every sound its properties name is stopped.
void __fastcall OnOccupantRemoved(char* self, void*, void* occupant)
{
    std::uintptr_t flag = reinterpret_cast<std::uintptr_t>(occupant);
    void* const first = Virtual<void*>(occupant, 0x18);
    if (Cdecl<uint8_t>(addr::SC4_GetPropertyBool, first, kNoSoundProperty, reinterpret_cast<uint8_t*>(&flag))
        && static_cast<uint8_t>(flag) != 0)
        return;
    if (Virtual<uint8_t>(At<void*>(self, mgr::kFilter), 0x14, occupant))
        --At<uint32_t>(self, mgr::kPassed);
    void* const holder = Virtual<void*>(occupant, 0x18);
    if (!holder)
        return;

    uint32_t sound = 0;
    if (Uint32Property(holder, kSoundA, &sound))
        StopSound(self, occupant, sound);
    if (Uint32Property(holder, kSoundB, &sound))
        StopSound(self, occupant, sound);
    if (Uint32Property(holder, kSoundLoop, &sound))
        StopSound(self, occupant, sound);
    if (Uint32Property(holder, kTimedSoundA, &sound))
        RemoveTimedSound(self, nullptr, occupant, sound);
    if (Uint32Property(holder, kTimedSoundB, &sound))
        RemoveTimedSound(self, nullptr, occupant, sound);
}

// Whether the message's object is an occupant without the property
// 0x029244DB; `reference` gets the occupant.
bool SoundingOccupant(void* object, void** reference)
{
    if (!Virtual<uint8_t>(object, 0x0, kOccupantIid, reference))
        return false;
    void* const holder = Virtual<void*>(*reference, 0x18);
    return !Virtual<uint8_t>(holder, 0xC, kNoSoundProperty);
}

// The game's 0x004EAD30 (thiscall, ret 4: a standard message). The
// occupant and lot messages. Returns 0 with no message, else 1.
uint8_t __fastcall OnCityMessage(char* self, void*, void* message)
{
    if (!message)
        return 0;
    void* const object = Virtual<void*>(message, 0x80);
    if (object)
        AddRef(object);
    void* occupant = nullptr;
    void* lot = nullptr;
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    if (type == 0x99EF1143) {
        if (object && SoundingOccupant(object, This<void**>(addr::SC4RefPtr_Reset, &occupant)))
            OnOccupantRemoved(self, nullptr, occupant);
    } else if (type == 0x99EF1142) {
        if (object && SoundingOccupant(object, This<void**>(addr::SC4RefPtr_Reset, &occupant)))
            OnOccupantAdded(self, nullptr, occupant);
    } else if (type == 0x99EF1146) {
        if (object) {
            Drop(occupant);
            if (SoundingOccupant(object, &occupant))
                This<void>(addr::SC4Audio_F4E8D40, self, occupant);
        }
    } else if (type == 0xE99B3D4D) {
        if (object) {
            Drop(lot);
            if (Virtual<uint8_t>(object, 0x0, kLotIid, &lot)) {
                void* const found = lot;
                const int32_t data2 = Virtual<int32_t>(message, 0x38);
                const int32_t data1 = Virtual<int32_t>(message, 0x24);
                UpdateLotSounds(self, nullptr, found, data1, data2);
            }
        }
    }
    ReleaseIfSet(lot);
    ReleaseIfSet(occupant);
    if (object)
        Release(object);
    return 1;
}

// The game's 0x004EAF20 (thiscall, ret 4; `this` is the message target at
// +4; slot 3, DoMessage).
uint8_t __fastcall DoMessage(char* target, void*, void* message)
{
    char* const self = target - mgr::kTarget;
    if (!message)
        return 0;
    void* standard = nullptr;
    Virtual<uint8_t>(message, 0x0, kMessageStandard, &standard);
    if (!standard)
        return 0;
    switch (Virtual<uint32_t>(standard, 0x10)) {
    case 0x26D31EC1: {
        const uint8_t result = EnterCity(self, nullptr);
        ReleaseIfSet(standard);
        return result;
    }
    case 0x26D31EC2: {
        const uint8_t result = LeaveCity(self, nullptr);
        ReleaseIfSet(standard);
        return result;
    }
    case 0x4348B111: {
        const uint8_t result = This<uint8_t>(addr::SC4Audio_F4E8CF0, self, standard);
        ReleaseIfSet(standard);
        return result;
    }
    case 0x09D92FC3:
        This<void>(addr::SC4Audio_F4E8820, self, standard);
        break;
    case 0x8A4A69C7:
        This<void>(addr::SC4Audio_F4E92A0, self);
        break;
    case 0x8A878385:
        This<void>(addr::SC4Audio_F4E8990, self, standard);
        break;
    case 0xC53D10AA:
        This<void>(addr::SC4Audio_F4E91D0, self);
        break;
    case 0x99EF1142:
    case 0x99EF1143:
    case 0x99EF1146:
    case 0xE8BDA2E7:
    case 0xE99B3D4D:
        OnCityMessage(self, nullptr, standard);
        break;
    default:
        break;
    }
    ReleaseIfSet(standard);
    return 1;
}

// --- Sound events ---------------------------------------------------------------------

// The game's 0x004E9870 (thiscall, ret 4; `this` is the event target at
// +0xC; slot 3). The original's error text calls it
// cSC4AudioAmbienceLayer::DoEvent. An event has its type at slot 4 and a
// standard message at slot 6; the message's object is the sound (interface
// 0xDC1CF595) the event works. Event 0x2A5B69C0 goes to 0x004E9490.
// Returns whether the event was one of the seven sound events.
uint8_t __fastcall DoEvent(char* target, void*, void* event)
{
    if (!event)
        return 0;
    if (Virtual<uint32_t>(event, 0x10) == 0x2A5B69C0) {
        This<void>(addr::SC4Audio_F4E9490, target - mgr::kEvents, event);
        return 0;
    }
    void* const object = Virtual<void*>(Virtual<void*>(event, 0x18), 0x80);
    if (!object)
        return 0;
    AddRef(object);
    void* sound = nullptr;
    if (!Virtual<uint8_t>(object, 0x0, kSoundIid, &sound)) {
        ReleaseIfSet(sound);
        Release(object);
        return 0;
    }
    if (!sound) {
        Release(object);
        return 0;
    }

    void* standard = nullptr;
    uint8_t handled = 1;
    switch (Virtual<uint32_t>(event, 0x10)) {
    case 0x2A2C0393:
        Virtual<void>(sound, 0x30, uint32_t{0xBB8}, uint32_t{0}, uint32_t{1});
        break;
    case 0x2A2C0384:
        if (Virtual<uint8_t>(event, 0x0, kMessageStandard, This<void**>(addr::SC4RefPtr_Reset, &standard))) {
            uint32_t key[3];
            const uint32_t data3 = Virtual<uint32_t>(standard, 0x4C);
            const uint32_t data2 = Virtual<uint32_t>(standard, 0x38);
            key[0] = Virtual<uint32_t>(standard, 0x24);
            key[1] = data2;
            key[2] = data3;
            if (!Virtual<uint8_t>(sound, 0xC, &key[0])) {
                GameString text;
                This<void*>(addr::SC4String_SC4String, &text);
                reinterpret_cast<void(__cdecl*)(GameString*, const char*, ...)>(addr::cRZBaseString_Sprintf)(
                    &text,
                    "Error: cSC4AudioAmbienceLayer::DoEvent : Failed to load freshness score (%08x:%08x:%08x)\n",
                    key[0], key[1], key[2]);
                This<void>(addr::SC4String_Destroy, &text);
            }
        }
        break;
    case 0x0A414C5C: {
        void* const message = Virtual<void*>(event, 0x18);
        Virtual<void>(sound, 0x40, Virtual<uint32_t>(message, 0x24));
        break;
    }
    case 0x4A2C0389:
        Virtual<void>(sound, 0x28);
        Virtual<void>(sound, 0x1C);
        break;
    case 0x4A3DA208: {
        void* const message = Virtual<void*>(event, 0x18);
        Virtual<void>(sound, 0x2C, Virtual<uint32_t>(message, 0x24));
        break;
    }
    case 0xEA2C0390: {
        void* const message = Virtual<void*>(event, 0x18);
        // setne al on the whole value: the upper three bytes go along.
        const uint32_t value = Virtual<uint32_t>(message, 0x24);
        Virtual<void>(sound, 0x70, (value & 0xFFFFFF00u) | (value != 0 ? 1u : 0u));
        Virtual<void>(sound, 0x20);
        break;
    }
    case 0xEA2D827C: {
        void* const first = Virtual<void*>(event, 0x18);
        void* const second = Virtual<void*>(event, 0x18);
        const uint32_t data2 = Virtual<uint32_t>(first, 0x38);
        const uint32_t data1 = Virtual<uint32_t>(second, 0x24);
        Virtual<void>(sound, 0x74, data1, data2);
        break;
    }
    default:
        handled = 0;
        break;
    }
    ReleaseIfSet(standard);
    ReleaseIfSet(sound);
    Release(object);
    return handled;
}

// --- Construction, destruction and the thunks -------------------------------------------

// The game's 0x004EA390 (thiscall, no arguments; returns `this`).
char* __fastcall Construct(char* self, void*)
{
    At<std::uintptr_t>(self, mgr::kTarget) = addr::cSC4TrafficSimulator_VtableBase4;
    At<std::uintptr_t>(self, mgr::kStarter) = addr::SC4AudioStarter_VtableBase;
    At<std::uintptr_t>(self, mgr::kEvents) = addr::cSC4AudioAEMSEventSystem_BaseVtable0;
    At<std::uintptr_t>(self, 0x0) = addr::SC4AudioAmbienceManager_Vtable;
    At<std::uintptr_t>(self, mgr::kTarget) = addr::SC4AudioAmbienceManager_VtableTarget;
    At<std::uintptr_t>(self, mgr::kStarter) = addr::SC4AudioAmbienceManager_VtableStarter;
    At<std::uintptr_t>(self, mgr::kEvents) = addr::SC4AudioAmbienceManager_VtableEvents;
    At<uint32_t>(self, mgr::kRefCount) = 0;
    At<uint32_t>(self, mgr::kServiceId) = 0xAA12F911;
    At<uint8_t>(self, mgr::kFlag18) = 0;
    At<uint8_t>(self, mgr::kRunning) = 1;
    At<void*>(self, mgr::kObject1C) = nullptr;
    At<void*>(self, mgr::kObject20) = nullptr;
    At<void*>(self, mgr::kOccupantManager) = nullptr;
    At<void*>(self, mgr::kTypeFilter) = nullptr;

    // The list of layers: the head, then the list's "insert no copies of a
    // null pointer" as the original has it.
    At<RefNode*>(self, mgr::kLayers) = nullptr;
    auto* const layers = static_cast<RefNode*>(Cdecl<void*>(addr::SC4_stl_allocate, uint32_t{0xC}));
    layers->next = layers;
    layers->previous = layers;
    At<RefNode*>(self, mgr::kLayers) = layers;
    void* nothing = nullptr;
    This<void>(addr::SC4RefList_InsertN, self + mgr::kLayers, layers->next, uint32_t{0}, &nothing);

    At<uint32_t>(self, mgr::kField30) = 0;
    for (std::size_t i = 0; i < 6; ++i)
        At<uint32_t>(self, mgr::kSix + 4 * i) = Global<uint32_t>(addr::SC4AudioAmbienceManager_DefaultFloat);
    At<TimedNode*>(self, mgr::kTimed) = nullptr;
    auto* const timed = static_cast<TimedNode*>(Cdecl<void*>(addr::SC4_stl_allocate, uint32_t{0x28}));
    timed->next = timed;
    timed->previous = timed;
    At<TimedNode*>(self, mgr::kTimed) = timed;
    At<uint8_t>(self, mgr::kInCity) = 1;
    for (std::size_t offset = mgr::kVector54; offset <= 0x68; offset += 4)
        At<uint32_t>(self, offset) = 0;
    At<uint8_t>(self, mgr::kFlagAC) = 0;
    At<uint32_t>(self, mgr::kState) = 0;
    At<uint8_t>(self, mgr::kWalk + 0) = 1;
    At<uint8_t>(self, mgr::kWalk + 1) = 1;
    At<uint8_t>(self, mgr::kWalk + 2) = 1;
    for (std::size_t offset = 0xB8; offset <= 0xDC; offset += 4)
        At<uint32_t>(self, offset) = 0;
    At<uint32_t>(self, mgr::kFieldE0) = 1;
    This<void*>(addr::SC4Stopwatch_Construct, self + mgr::kSliceWatch, uint32_t{3});
    This<void*>(addr::SC4Stopwatch_Construct, self + mgr::kTickWatch, uint32_t{4});
    At<void*>(self, mgr::kFilter) = nullptr;
    At<uint32_t>(self, mgr::kPassed) = 0;
    return self;
}

// The game's 0x004EA5A0 (thiscall, no arguments). The destructor.
void __fastcall Destruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4AudioAmbienceManager_Vtable;
    At<std::uintptr_t>(self, mgr::kTarget) = addr::SC4AudioAmbienceManager_VtableTarget;
    At<std::uintptr_t>(self, mgr::kStarter) = addr::SC4AudioAmbienceManager_VtableStarter;
    At<std::uintptr_t>(self, mgr::kEvents) = addr::SC4AudioAmbienceManager_VtableEvents;
    ReleaseIfSet(At<void*>(self, mgr::kFilter));
    This<void>(addr::SC4Stopwatch_Destruct, self + mgr::kTickWatch);
    This<void>(addr::SC4Stopwatch_Destruct, self + mgr::kSliceWatch);
    if (void* const buffer = At<void*>(self, mgr::kBufferCC))
        Cdecl<void>(addr::SC4_Free, buffer);
    if (void* const buffer = At<void*>(self, mgr::kLayerIds))
        Cdecl<void>(addr::SC4_Free, buffer);
    if (void* const buffer = At<void*>(self, mgr::kVector54))
        Cdecl<void>(addr::SC4_Free, buffer);
    ClearTimedList(&At<TimedNode*>(self, mgr::kTimed), nullptr);
    if (TimedNode* const head = At<TimedNode*>(self, mgr::kTimed))
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(head));
    This<void>(addr::SC4PairList_Destroy, self + mgr::kLayers);
    ReleaseIfSet(At<void*>(self, mgr::kTypeFilter));
    ReleaseIfSet(At<void*>(self, mgr::kOccupantManager));
    ReleaseIfSet(At<void*>(self, mgr::kObject20));
    ReleaseIfSet(At<void*>(self, mgr::kObject1C));
}

// The game's 0x004EA670 (thiscall, ret 4; service slot 13). The deleting
// destructor.
char* __fastcall DeletingDestruct(char* self, void*, uint32_t flags)
{
    Destruct(self, nullptr);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// The game's 0x004EA510 (thiscall, no arguments; service slot 12, also in
// the vtable 0x00A94308 of another audio service). The tick priority is the
// service's priority (slot 5).
int32_t __fastcall GetTickPriority(char* self, void*)
{
    return Virtual<int32_t>(self, 0x14);
}

// The game's 0x004EA520, 0x004EA540 and 0x004EA560 (thiscall, ret 8): the
// three parts' QueryInterface, the manager's.
uint8_t __fastcall TargetQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    return This<uint8_t>(addr::SC4Audio_F4E82D0, self - mgr::kTarget, iid, out);
}

uint8_t __fastcall StarterQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    return This<uint8_t>(addr::SC4Audio_F4E82D0, self - mgr::kStarter, iid, out);
}

uint8_t __fastcall EventsQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    return This<uint8_t>(addr::SC4Audio_F4E82D0, self - mgr::kEvents, iid, out);
}

// The game's 0x004EA530, 0x004EA550 and 0x004EA570 (thiscall, no
// arguments): the three parts' Release, the manager's.
uint32_t __fastcall TargetRelease(char* self, void*)
{
    return This<uint32_t>(addr::SC4Audio_F4E82B0, self - mgr::kTarget);
}

uint32_t __fastcall StarterRelease(char* self, void*)
{
    return This<uint32_t>(addr::SC4Audio_F4E82B0, self - mgr::kStarter);
}

uint32_t __fastcall EventsRelease(char* self, void*)
{
    return This<uint32_t>(addr::SC4Audio_F4E82B0, self - mgr::kEvents);
}

// The game's 0x004EA580 and 0x004EA590 (thiscall, no arguments; slots 3 and
// 4 of the part at +8): the service's Init and Shutdown.
uint8_t __fastcall StarterInit(char* self, void*)
{
    return This<uint8_t>(addr::SC4Audio_F4E84D0, self - mgr::kStarter);
}

uint8_t __fastcall StarterShutdown(char* self, void*)
{
    return This<uint8_t>(addr::SC4Audio_F4E96A0, self - mgr::kStarter);
}

// --- The director's factories ---------------------------------------------------------

// The shape of the eight factories of the large classes: the object's
// bytes from the game's allocator, the constructor, and the interface the
// class registry is given (the object, or a part of it).
void* MakeObject(uint32_t size, std::uintptr_t constructor, std::size_t part)
{
    void* const memory = Cdecl<void*>(addr::SC4_operator_new, size);
    if (!memory)
        return nullptr;
    char* const object = This<char*>(constructor, memory);
    if (part == 0)
        return object;
    return object ? object + part : nullptr;
}

// The game's 0x004EB080 (cdecl): class 0x4A47DC0D, the sound manager.
void* __cdecl MakeSoundManager()
{
    return MakeObject(0x80, addr::SC4Audio_F4F9830, 0);
}

// The game's 0x004EB0A0 (cdecl): class 0xBDB6171E.
void* __cdecl MakeClassBDB6171E()
{
    return MakeObject(0x50, addr::SC4Audio_F4EF6F0, 0);
}

// The game's 0x004EB0C0 (cdecl): class 0x6A12F51C, the ambience manager.
void* __cdecl MakeAmbienceManager()
{
    void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x120});
    if (!memory)
        return nullptr;
    return Construct(static_cast<char*>(memory), nullptr);
}

// The game's 0x004EB0E0 (cdecl): class 0xCA4A66B9.
void* __cdecl MakeClassCA4A66B9()
{
    return MakeObject(0x7C, addr::SC4Audio_F4F1930, 0);
}

// The game's 0x004EB100 (cdecl): class 0x69E500BC, its part at +4.
void* __cdecl MakeClass69E500BC()
{
    return MakeObject(0x34, addr::SC4Audio_F4F0510, 0x4);
}

// The game's 0x004EB120 (cdecl): class 0x29E2797F, its part at +4.
void* __cdecl MakeClass29E2797F()
{
    return MakeObject(0x98, addr::SC4Audio_F4F6140, 0x4);
}

// The game's 0x004EB150 (cdecl): class 0x5CFBDBBF, the framework's sound
// service (constructor 0x008D35AB), its part at +0x1C.
void* __cdecl MakeSoundService()
{
    return MakeObject(0x90, addr::GZSound_ConstructService, 0x1C);
}

// The game's 0x004EB180 (cdecl): class 0x5CFBDBD5, a framework sound
// class (constructor 0x008D376E).
void* __cdecl MakeClass5CFBDBD5()
{
    return MakeObject(0x100, addr::GZSound_Construct8D376E, 0);
}

// The game's 0x004EB260 to 0x004EB7D0 (cdecl), thirty of them: an event
// handler of 0x10 bytes. +4 a dword, +8 a byte, +0xC the reference count.
template <int Index>
void* __cdecl MakePlainHandler()
{
    char* const handler = static_cast<char*>(Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x10}));
    if (!handler)
        return nullptr;
    At<std::uintptr_t>(handler, 0x0) = addr::SC4AudioEventHandler_Vtables + 0x18 * Index;
    At<uint32_t>(handler, 0x4) = 0;
    At<uint8_t>(handler, 0x8) = 0;
    At<uint32_t>(handler, 0xC) = 0;
    return handler;
}

// The game's 0x004EB800 to 0x004EBE90 (cdecl), thirty-five of them: an
// event handler of 0x14 bytes. +4 and +8 two dwords of -1, +0xC a byte,
// +0x10 the reference count.
template <int Index>
void* __cdecl MakeTimedHandler()
{
    char* const handler = static_cast<char*>(Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x14}));
    if (!handler)
        return nullptr;
    At<std::uintptr_t>(handler, 0x0) = addr::SC4AudioEventHandler_Vtables + 0x18 * Index;
    At<uint32_t>(handler, 0x4) = 0xFFFFFFFF;
    At<uint32_t>(handler, 0x8) = 0xFFFFFFFF;
    At<uint8_t>(handler, 0xC) = 0;
    At<uint32_t>(handler, 0x10) = 0;
    return handler;
}

// The game's 0x004EBA70 (thiscall, ret 8; slot 0 of the thirty-five
// 0x14-byte handlers). QueryInterface: the object for 0x4A40074C and
// 0xCA400757.
uint8_t __fastcall TimedHandlerQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    if (iid != 0x4A40074C && iid != 0xCA400757)
        return 0;
    *out = self;
    AddRef(self);
    return 1;
}

// --- The director ---------------------------------------------------------------------

// The game's 0x004EB1A0 (thiscall, no arguments; director slot 13). The
// director's ID.
uint32_t __fastcall GetDirectorId(char*, void*)
{
    return 0xFDB5CA17;
}

// The game's 0x004EB1B0 (thiscall, ret 4; director slot 4, OnStart). The
// hooks part joins the framework's hooks; five framework directors are
// added to the application's director and started with the same argument.
uint8_t __fastcall OnStart(char* self, void*, void* com)
{
    void* const root = Cdecl<void*>(addr::RZGetCOMDllDirector);
    void* const hooks = self ? self + dir::kHooks : nullptr;
    Virtual<uint8_t>(Cdecl<void*>(addr::RZGetFramework), 0x1C, hooks);
    for (const std::uintptr_t getter :
         {addr::GZDirector_Get9666C9, addr::GZDirector_Get87497E, addr::GZDirector_Get94AC43,
          addr::GZDirector_Get9361D5, addr::GZDirector_Get945785}) {
        void* const child = Cdecl<void*>(getter);
        This<void>(addr::cRZCOMDllDirector_AddDirector, root, child);
        Virtual<uint8_t>(child, 0x10, com);
    }
    return 1;
}

// The game's 0x004EBEC0 (thiscall, no arguments). The destructor: the
// eight object fields let go from the last to the first, then the base's.
void __fastcall DirectorDestruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4AudioDirector_Vtable;
    At<std::uintptr_t>(self, dir::kHooks) = addr::SC4AudioDirector_VtableHooks;
    for (std::size_t offset = dir::kObject68; offset >= dir::kObject4C; offset -= 4)
        ReleaseIfSet(At<void*>(self, offset));
    This<void>(addr::cRZCOMDllDirector_Destruct, self);
}

// One object of the director made by class and interface: the COM object
// first (director slot 12), then the field emptied, then the instance.
bool MakeInto(char* director, std::size_t field, uint32_t classId, uint32_t iid)
{
    void* const com = Virtual<void*>(director, 0x30);
    Drop(At<void*>(director, field));
    return Virtual<uint8_t>(com, 0xC, classId, iid, &At<void*>(director, field)) != 0;
}

// The game's 0x004EBF40 (thiscall, no arguments; `this` is the hooks part
// at +4; hook slot 5, after the application's start). Reads the sound
// configuration (resource 0x00000000 / 0x8A5971C5 / 0xEA8A1115) and, when
// that worked, makes and starts the sound service and the seven audio
// objects. Returns whether everything worked; an object that could not be
// made is called through null as in the original.
uint8_t __fastcall PostAppInit(char* hooks, void*)
{
    char* const self = hooks - dir::kHooks;
    void* resource = nullptr;
    uint32_t key[3] = {0, 0x8A5971C5, 0xEA8A1115};
    uint8_t result = 0;
    if (Cdecl<uint8_t>(addr::SC4_OpenResourceRecord, &key[0], &resource, uint32_t{0})) {
        // The reader: 0x68 bytes at [esp+0x3C] of the original's frame, to
        // the frame's end.
        alignas(4) char reader[0x68];
        This<void*>(addr::GZSoundConfig_Construct, &reader[0], resource);
        if (This<uint8_t>(addr::GZSoundConfig_Apply, &reader[0x3C])) {
            uint8_t ok = 0;
            void* service = nullptr;
            void* com = Virtual<void*>(self, 0x30);
            Drop(service);
            if (Virtual<uint8_t>(com, 0xC, uint32_t{0x5CFBDBBF}, kSystemServiceIid, &service)) {
                if (Virtual<uint8_t>(service, 0x20)) {
                    void* const framework = Cdecl<void*>(addr::RZGetFramework);
                    if (Virtual<uint8_t>(framework, 0xC, service))
                        ok = 1;
                }
            }
            if (!MakeInto(self, dir::kObject5C, 0xAA355871, 0xAA355871))
                ok = 0;
            if (!MakeInto(self, dir::kObject68, 0x29E2797F, 0x29E39DB4))
                ok = 0;
            if (!MakeInto(self, dir::kObject4C, 0xBDB6171E, 0xBB523BB2))
                ok = 0;
            if (!MakeInto(self, dir::kObject50, 0x69E500BC, 0xBB523BB2))
                ok = 0;
            if (!MakeInto(self, dir::kAmbience, 0x6A12F51C, 0x6A12F51C))
                ok = 0;
            if (!MakeInto(self, dir::kSounds, 0x4A47DC0D, 0x4A47DC0D))
                ok = 0;
            if (!MakeInto(self, dir::kObject64, 0xCA4A66B9, 0xCA4A66B9))
                ok = 0;

            if (!Virtual<uint8_t>(At<void*>(self, dir::kObject68), 0xC))
                ok = 0;
            if (!Virtual<uint8_t>(At<void*>(self, dir::kAmbience), 0x20))
                ok = 0;
            if (!This<uint8_t>(addr::SC4Audio_F4FA2F0, At<void*>(self, dir::kSounds)))
                ok = 0;
            if (!This<uint8_t>(addr::SC4Audio_F4F1360, At<void*>(self, dir::kObject64)))
                ok = 0;
            if (!This<uint8_t>(addr::SC4Audio_F4EDAA0, At<void*>(self, dir::kObject5C)))
                ok = 0;

            void* starter = nullptr;
            if (Virtual<uint8_t>(At<void*>(self, dir::kObject4C), 0x0, kStarterIid, &starter)) {
                if (!Virtual<uint8_t>(starter, 0xC))
                    ok = 0;
            }
            void* const second = At<void*>(self, dir::kObject50);
            Drop(starter);
            if (Virtual<uint8_t>(second, 0x0, kStarterIid, &starter)) {
                if (!Virtual<uint8_t>(starter, 0xC))
                    ok = 0;
            }
            ReleaseIfSet(starter);
            ReleaseIfSet(service);
            result = ok;
        }
        This<void>(addr::GZSoundConfig_Destruct, &reader[0]);
    }
    ReleaseIfSet(resource);
    return result;
}

// The game's 0x004EC2B0 (thiscall, no arguments; `this` is the hooks part;
// hook slot 6, before the application's shutdown). Stops the seven audio
// objects and lets them go. Returns 1.
uint8_t __fastcall PreAppShutdown(char* hooks, void*)
{
    char* const self = hooks - dir::kHooks;
    void* starter = nullptr;
    if (void* const object = At<void*>(self, dir::kObject50)) {
        if (Virtual<uint8_t>(object, 0x0, kStarterIid, &starter))
            Virtual<uint8_t>(starter, 0x10);
        Drop(At<void*>(self, dir::kObject50));
    }
    if (void* const object = At<void*>(self, dir::kObject4C)) {
        Drop(starter);
        if (Virtual<uint8_t>(object, 0x0, kStarterIid, &starter))
            Virtual<uint8_t>(starter, 0x10);
        Drop(At<void*>(self, dir::kObject4C));
    }
    if (void* const object = At<void*>(self, dir::kObject5C)) {
        This<void>(addr::SC4Audio_F4ED750, object);
        Drop(At<void*>(self, dir::kObject5C));
    }
    if (void* const object = At<void*>(self, dir::kObject64)) {
        This<void>(addr::SC4Audio_F4F1400, object);
        Drop(At<void*>(self, dir::kObject64));
    }
    if (void* const object = At<void*>(self, dir::kSounds)) {
        This<void>(addr::SC4Audio_F4F82E0, object);
        Drop(At<void*>(self, dir::kSounds));
    }
    if (void* const object = At<void*>(self, dir::kAmbience)) {
        Virtual<uint8_t>(object, 0x24);
        Drop(At<void*>(self, dir::kAmbience));
    }
    if (void* const object = At<void*>(self, dir::kObject68)) {
        Virtual<uint8_t>(object, 0x10);
        Drop(At<void*>(self, dir::kObject68));
    }
    ReleaseIfSet(starter);
    return 1;
}

// --- Registration ---------------------------------------------------------------------

bool InstallAudio4(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[112] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4Audio_F4E9810, reinterpret_cast<void*>(&LeaveCity)},
        {addr::SC4Audio_F4E9870, reinterpret_cast<void*>(&DoEvent)},
        {addr::SC4Audio_F4E9C10, reinterpret_cast<void*>(&CreateAmbienceLayers)},
        {addr::SC4Audio_F4E9D60, reinterpret_cast<void*>(&ClearTimedList)},
        {addr::SC4Audio_F4E9DA0, reinterpret_cast<void*>(&UpdateLotSounds)},
        {addr::SC4Audio_F4EA080, reinterpret_cast<void*>(&RemoveTimedSound)},
        {addr::SC4Audio_F4EA100, reinterpret_cast<void*>(&NewTimedNode)},
        {addr::SC4Audio_F4EA150, reinterpret_cast<void*>(&OnTick)},
        {addr::SC4Audio_F4EA270, reinterpret_cast<void*>(&OnOccupantRemoved)},
        {addr::SC4Audio_F4EA390, reinterpret_cast<void*>(&Construct)},
        {addr::SC4Audio_F4EA510, reinterpret_cast<void*>(&GetTickPriority)},
        {addr::SC4Audio_F4EA520, reinterpret_cast<void*>(&TargetQueryInterface)},
        {addr::SC4Audio_F4EA530, reinterpret_cast<void*>(&TargetRelease)},
        {addr::SC4Audio_F4EA540, reinterpret_cast<void*>(&StarterQueryInterface)},
        {addr::SC4Audio_F4EA550, reinterpret_cast<void*>(&StarterRelease)},
        {addr::SC4Audio_F4EA560, reinterpret_cast<void*>(&EventsQueryInterface)},
        {addr::SC4Audio_F4EA570, reinterpret_cast<void*>(&EventsRelease)},
        {addr::SC4Audio_F4EA580, reinterpret_cast<void*>(&StarterInit)},
        {addr::SC4Audio_F4EA590, reinterpret_cast<void*>(&StarterShutdown)},
        {addr::SC4Audio_F4EA5A0, reinterpret_cast<void*>(&Destruct)},
        {addr::SC4Audio_F4EA670, reinterpret_cast<void*>(&DeletingDestruct)},
        {addr::SC4Audio_F4EA690, reinterpret_cast<void*>(&AddTimedSound)},
        {addr::SC4Audio_F4EA7D0, reinterpret_cast<void*>(&OnOccupantAdded)},
        {addr::SC4Audio_F4EA970, reinterpret_cast<void*>(&OnOccupantFound)},
        {addr::SC4Audio_F4EAAA0, reinterpret_cast<void*>(&OccupantCallback)},
        {addr::SC4Audio_F4EAAE0, reinterpret_cast<void*>(&LookAtOccupants)},
        {addr::SC4Audio_F4EABF0, reinterpret_cast<void*>(&EnterCity)},
        {addr::SC4Audio_F4EAD30, reinterpret_cast<void*>(&OnCityMessage)},
        {addr::SC4Audio_F4EAF20, reinterpret_cast<void*>(&DoMessage)},
        {addr::SC4Audio_F4EB080, reinterpret_cast<void*>(&MakeSoundManager)},
        {addr::SC4Audio_F4EB0A0, reinterpret_cast<void*>(&MakeClassBDB6171E)},
        {addr::SC4Audio_F4EB0C0, reinterpret_cast<void*>(&MakeAmbienceManager)},
        {addr::SC4Audio_F4EB0E0, reinterpret_cast<void*>(&MakeClassCA4A66B9)},
        {addr::SC4Audio_F4EB100, reinterpret_cast<void*>(&MakeClass69E500BC)},
        {addr::SC4Audio_F4EB120, reinterpret_cast<void*>(&MakeClass29E2797F)},
        {addr::SC4Audio_F4EB150, reinterpret_cast<void*>(&MakeSoundService)},
        {addr::SC4Audio_F4EB180, reinterpret_cast<void*>(&MakeClass5CFBDBD5)},
        {addr::SC4Audio_F4EB1A0, reinterpret_cast<void*>(&GetDirectorId)},
        {addr::SC4Audio_F4EB1B0, reinterpret_cast<void*>(&OnStart)},
        {addr::SC4Audio_F4EB260, reinterpret_cast<void*>(&MakePlainHandler<0>)},
        {addr::SC4Audio_F4EB290, reinterpret_cast<void*>(&MakePlainHandler<1>)},
        {addr::SC4Audio_F4EB2C0, reinterpret_cast<void*>(&MakePlainHandler<2>)},
        {addr::SC4Audio_F4EB2F0, reinterpret_cast<void*>(&MakePlainHandler<3>)},
        {addr::SC4Audio_F4EB320, reinterpret_cast<void*>(&MakePlainHandler<4>)},
        {addr::SC4Audio_F4EB350, reinterpret_cast<void*>(&MakePlainHandler<5>)},
        {addr::SC4Audio_F4EB380, reinterpret_cast<void*>(&MakePlainHandler<6>)},
        {addr::SC4Audio_F4EB3B0, reinterpret_cast<void*>(&MakePlainHandler<7>)},
        {addr::SC4Audio_F4EB3E0, reinterpret_cast<void*>(&MakePlainHandler<8>)},
        {addr::SC4Audio_F4EB410, reinterpret_cast<void*>(&MakePlainHandler<9>)},
        {addr::SC4Audio_F4EB440, reinterpret_cast<void*>(&MakePlainHandler<10>)},
        {addr::SC4Audio_F4EB470, reinterpret_cast<void*>(&MakePlainHandler<11>)},
        {addr::SC4Audio_F4EB4A0, reinterpret_cast<void*>(&MakePlainHandler<12>)},
        {addr::SC4Audio_F4EB4D0, reinterpret_cast<void*>(&MakePlainHandler<13>)},
        {addr::SC4Audio_F4EB500, reinterpret_cast<void*>(&MakePlainHandler<14>)},
        {addr::SC4Audio_F4EB530, reinterpret_cast<void*>(&MakePlainHandler<15>)},
        {addr::SC4Audio_F4EB560, reinterpret_cast<void*>(&MakePlainHandler<16>)},
        {addr::SC4Audio_F4EB590, reinterpret_cast<void*>(&MakePlainHandler<17>)},
        {addr::SC4Audio_F4EB5C0, reinterpret_cast<void*>(&MakePlainHandler<18>)},
        {addr::SC4Audio_F4EB5F0, reinterpret_cast<void*>(&MakePlainHandler<19>)},
        {addr::SC4Audio_F4EB620, reinterpret_cast<void*>(&MakePlainHandler<20>)},
        {addr::SC4Audio_F4EB650, reinterpret_cast<void*>(&MakePlainHandler<21>)},
        {addr::SC4Audio_F4EB680, reinterpret_cast<void*>(&MakePlainHandler<22>)},
        {addr::SC4Audio_F4EB6B0, reinterpret_cast<void*>(&MakePlainHandler<23>)},
        {addr::SC4Audio_F4EB6E0, reinterpret_cast<void*>(&MakePlainHandler<24>)},
        {addr::SC4Audio_F4EB710, reinterpret_cast<void*>(&MakePlainHandler<25>)},
        {addr::SC4Audio_F4EB740, reinterpret_cast<void*>(&MakePlainHandler<26>)},
        {addr::SC4Audio_F4EB770, reinterpret_cast<void*>(&MakePlainHandler<27>)},
        {addr::SC4Audio_F4EB7A0, reinterpret_cast<void*>(&MakePlainHandler<28>)},
        {addr::SC4Audio_F4EB7D0, reinterpret_cast<void*>(&MakePlainHandler<29>)},
        {addr::SC4Audio_F4EB800, reinterpret_cast<void*>(&MakeTimedHandler<30>)},
        {addr::SC4Audio_F4EB830, reinterpret_cast<void*>(&MakeTimedHandler<31>)},
        {addr::SC4Audio_F4EB860, reinterpret_cast<void*>(&MakeTimedHandler<32>)},
        {addr::SC4Audio_F4EB890, reinterpret_cast<void*>(&MakeTimedHandler<33>)},
        {addr::SC4Audio_F4EB8C0, reinterpret_cast<void*>(&MakeTimedHandler<34>)},
        {addr::SC4Audio_F4EB8F0, reinterpret_cast<void*>(&MakeTimedHandler<35>)},
        {addr::SC4Audio_F4EB920, reinterpret_cast<void*>(&MakeTimedHandler<36>)},
        {addr::SC4Audio_F4EB950, reinterpret_cast<void*>(&MakeTimedHandler<37>)},
        {addr::SC4Audio_F4EB980, reinterpret_cast<void*>(&MakeTimedHandler<38>)},
        {addr::SC4Audio_F4EB9B0, reinterpret_cast<void*>(&MakeTimedHandler<39>)},
        {addr::SC4Audio_F4EB9E0, reinterpret_cast<void*>(&MakeTimedHandler<40>)},
        {addr::SC4Audio_F4EBA10, reinterpret_cast<void*>(&MakeTimedHandler<41>)},
        {addr::SC4Audio_F4EBA40, reinterpret_cast<void*>(&MakeTimedHandler<42>)},
        {addr::SC4Audio_F4EBAA0, reinterpret_cast<void*>(&MakeTimedHandler<43>)},
        {addr::SC4Audio_F4EBAD0, reinterpret_cast<void*>(&MakeTimedHandler<44>)},
        {addr::SC4Audio_F4EBB00, reinterpret_cast<void*>(&MakeTimedHandler<45>)},
        {addr::SC4Audio_F4EBB30, reinterpret_cast<void*>(&MakeTimedHandler<46>)},
        {addr::SC4Audio_F4EBB60, reinterpret_cast<void*>(&MakeTimedHandler<47>)},
        {addr::SC4Audio_F4EBB90, reinterpret_cast<void*>(&MakeTimedHandler<48>)},
        {addr::SC4Audio_F4EBBC0, reinterpret_cast<void*>(&MakeTimedHandler<49>)},
        {addr::SC4Audio_F4EBBF0, reinterpret_cast<void*>(&MakeTimedHandler<50>)},
        {addr::SC4Audio_F4EBC20, reinterpret_cast<void*>(&MakeTimedHandler<51>)},
        {addr::SC4Audio_F4EBC50, reinterpret_cast<void*>(&MakeTimedHandler<52>)},
        {addr::SC4Audio_F4EBC80, reinterpret_cast<void*>(&MakeTimedHandler<53>)},
        {addr::SC4Audio_F4EBCB0, reinterpret_cast<void*>(&MakeTimedHandler<54>)},
        {addr::SC4Audio_F4EBCE0, reinterpret_cast<void*>(&MakeTimedHandler<55>)},
        {addr::SC4Audio_F4EBD10, reinterpret_cast<void*>(&MakeTimedHandler<56>)},
        {addr::SC4Audio_F4EBD40, reinterpret_cast<void*>(&MakeTimedHandler<57>)},
        {addr::SC4Audio_F4EBD70, reinterpret_cast<void*>(&MakeTimedHandler<58>)},
        {addr::SC4Audio_F4EBDA0, reinterpret_cast<void*>(&MakeTimedHandler<59>)},
        {addr::SC4Audio_F4EBDD0, reinterpret_cast<void*>(&MakeTimedHandler<60>)},
        {addr::SC4Audio_F4EBE00, reinterpret_cast<void*>(&MakeTimedHandler<61>)},
        {addr::SC4Audio_F4EBE30, reinterpret_cast<void*>(&MakeTimedHandler<62>)},
        {addr::SC4Audio_F4EBE60, reinterpret_cast<void*>(&MakeTimedHandler<63>)},
        {addr::SC4Audio_F4EBE90, reinterpret_cast<void*>(&MakeTimedHandler<64>)},
        {addr::SC4Audio_F4EBA70, reinterpret_cast<void*>(&TimedHandlerQueryInterface)},
        {addr::SC4Audio_F4EBEC0, reinterpret_cast<void*>(&DirectorDestruct)},
        {addr::SC4Audio_F4EBF40, reinterpret_cast<void*>(&PostAppInit)},
        {addr::SC4Audio_F4EC2B0, reinterpret_cast<void*>(&PreAppShutdown)},
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

const bool g_audio4Registered = registry::Register({
    "audio.part4",
    "audio",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Audio_F4E9810,
    "Our ambience manager's second half and the audio director with its class factories (docs/audio.md)",
    false,
    InstallAudio4,
});

} // namespace
} // namespace opensc4::audio
