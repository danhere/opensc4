// The game's audio code, third part (0x004E6C10 to 0x004E9810): the end of
// the ambience layer and the first half of the ambience manager.
//
// cSC4AudioAmbienceLayer (vtable 0x00A934A8, 29 slots, at least 0x25C bytes;
// made by the manager at 0x004E9C4C with the instance of its tuning
// exemplar). The object is its own message target (slot 3 is DoMessage).
// Here are its constructor and destructor, Release, Init, Shutdown,
// DoMessage (0xC53D10AA: the tuning is read again), the tuning reader
// (_ReadTuningValues: exemplar group 0xCA25875D), the occupant filters the
// layers share (_AddFilter and the static _StaticRemoveAllFilters; a hash
// table at 0x00B21958 from class ID to filter), the freshness scores
// (_LoadFreshnessScore: one sound service object per key, kept in a list)
// and five small accessors. The class's first part is in the range before
// this one (0x004E4A60 to 0x004E6C10).
//
// cSC4AudioAmbienceManager (a system service: vtables 0x00A9378C at +0,
// message target 0x00A9377C at +4, 0x00A93768 at +8, the sound service's
// callback 0x00A93758 at +0xC). Here are QueryInterface, Release, Init and
// Shutdown, the handlers of occupant messages that start, place and stop the
// sounds of occupants, the sounds played at hours of the day, the manager's
// own tuning (exemplar instance 0x6A359C5C), the view rectangle taken from
// the listener, the sweep that hands the occupants of a few tracts to the
// layers on every update, and the list of layers being emptied. The rest of
// the class follows this range (0x004E9810 onward).
//
// The manager's occupant filter (vtable 0x00A93640, 6 slots, a counted
// object): occupants of seven types pass.
//
// Replacement, off by default:
//
//   audio.part3: everything named above but what is listed next.
//
// Still the game's: 0x004E76A0 (the layer's slot 9, a getter of +0x1FC whose
// code another class's vtable 0x00A87774 holds as well), 0x004E82A0 (a getter
// of +0x11C in the vtables of three other classes, the city's slot 94 among
// them) and 0x004E8380 (the overlap of two rectangles, also called from
// 0x0076237C).
//
// Not functions: 0x004E7050 (inside _AddFilter 0x004E7010), 0x004E8400
// (inside 0x004E8380), 0x004E8B20 (inside 0x004E8AD0), 0x004E8E20 (inside
// 0x004E8E10), 0x004E9450 (inside 0x004E92A0) and 0x004E96C0 (inside the
// manager's Shutdown 0x004E96A0).
//
// The sound service (framework service 0x23CECBA0, interface 0x106077D2,
// vtable 0x00AD7180) stays the game's. Slot 11 (+0x2C) takes a command and
// four numbers: 0xEA451903 starts a sound (sound, key, looped, 0), 0xCA45190F
// places it (key, x, y, z as integers), 0x6A451908 stops it (key, 0, 0, 0).
// Slot 13 (+0x34) is the same with an interface and a result between the
// command and the numbers, slot 12 (+0x30) is given the result, slots 15 and
// 16 (+0x3C, +0x40) add and remove a callback. A sound's key is the address
// of the object it belongs to, most often with the sound's ID added.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace opensc4::audio {
namespace {

namespace layer { // cSC4AudioAmbienceLayer
constexpr std::size_t kStarted = 0x4;       // byte: Init has run
constexpr std::size_t kReferences = 0x8;
constexpr std::size_t kFlagC = 0xC;         // byte, 1 from the constructor
constexpr std::size_t kFlagD = 0xD;         // byte
constexpr std::size_t kFloat20 = 0x20;      // tuning 0x0A4168E1
constexpr std::size_t kFloat24 = 0x24;      // tuning 0x0A56212A
constexpr std::size_t kHeld28 = 0x28;       // three references
constexpr std::size_t kHeld2C = 0x2C;
constexpr std::size_t kHeld30 = 0x30;
constexpr std::size_t kTuning34 = 0x34;     // tuning 0x8A552758
constexpr std::size_t kTuning38 = 0x38;     // tuning 0x8A70B1A2
constexpr std::size_t kVectors = 0x3C;      // 27 floats
constexpr std::size_t kVectorC8 = 0xC8;     // three floats (slot 10)
constexpr std::size_t kTimer = 0xE8;        // 0x004F81D0's object; +0x20 and +0x24 two times
constexpr std::size_t kFilterCounts = 0x138; // three counts of filter interfaces
constexpr std::size_t kBits = 0x144;        // three bit vectors of 0x14 bytes
constexpr std::size_t kScores = 0x180;      // the freshness scores: a list's sentinel node
constexpr std::size_t kPairs = 0x184;       // a list's sentinel node
constexpr std::size_t kName = 0x188;        // a string
constexpr std::size_t kCurves = 0x1A0;      // three curves of 16 bytes
constexpr std::size_t kScoreCount = 0x1D0;  // tuning 0x6A3FBDDA
constexpr std::size_t kScoreKeys = 0x1D4;   // tuning 0xEA3FBDD7: pairs of group and instance
constexpr std::size_t kByte1E0 = 0x1E0;     // tuning 0x0A25896B
constexpr std::size_t kByte1E1 = 0x1E1;     // tuning 0xAA3FBDDE
constexpr std::size_t kTuning1E8 = 0x1E8;   // tuning 0xAA3FC48E
constexpr std::size_t kTuning1EC = 0x1EC;   // tuning 0xEA3FC42A
constexpr std::size_t kTuning1F0 = 0x1F0;   // tuning 0x6A3FC4C7
constexpr std::size_t kInstance = 0x1F8;    // the tuning exemplar's instance
constexpr std::size_t kField1FC = 0x1FC;    // slots 8 and 9
constexpr std::size_t kFloat200 = 0x200;    // tuning 0x8A3FC5DD
constexpr std::size_t kPreference = 0x204;  // a preference of the application
// Three kinds of filters, 0x1C bytes each: the class IDs from the tuning
// (a vector), the filters made from them (a vector) and their number.
constexpr std::size_t kKinds = 0x208;
constexpr std::size_t kKindFilters = 0xC;
constexpr std::size_t kKindCount = 0x18;
constexpr std::size_t kKindSize = 0x1C;
} // namespace layer

namespace manager { // cSC4AudioAmbienceManager
constexpr std::size_t kTarget = 0x4;        // the message target
constexpr std::size_t kCallback = 0xC;      // the sound service's callback
constexpr std::size_t kReferences = 0x10;
constexpr std::size_t kService1C = 0x1C;    // service 0x49E50BA1
constexpr std::size_t kSound = 0x20;        // the sound service
constexpr std::size_t kOccupants = 0x24;    // the occupant manager
constexpr std::size_t kFilter = 0x28;       // the occupant filter of the sweep
constexpr std::size_t kLayers = 0x2C;       // the layers: a list's sentinel node
constexpr std::size_t kZoom = 0x30;
constexpr std::size_t kPlace = 0x34;        // three floats from the listener's +0x24
constexpr std::size_t kFacing = 0x40;       // three floats from the listener's +0xC
constexpr std::size_t kTimed = 0x4C;        // the timed sounds: a list's sentinel node
constexpr std::size_t kStarted = 0x50;      // byte
constexpr std::size_t kReach = 0x54;        // a vector of floats, one for every zoom
constexpr std::size_t kVector60 = 0x60;     // a vector of numbers (tuning 0x2A3FF8CD)
constexpr std::size_t kCells = 0x6C;        // the view in cells: left, top, right, bottom
constexpr std::size_t kCellsBefore = 0x7C;  // the same, one update earlier
constexpr std::size_t kView = 0x8C;         // the view in metres, cut to the city
constexpr std::size_t kCity = 0x9C;         // the city's rectangle in metres
constexpr std::size_t kState = 0xB0;
constexpr std::size_t kTracts = 0xB4;       // the tract iterator of the sweep
constexpr std::size_t kBudget = 0xE8;       // a stopwatch
constexpr std::size_t kStopwatch = 0x100;   // another
constexpr std::size_t kClassFilter = 0x118; // a filter of class 0x4A401BA7
} // namespace manager

namespace timed { // a node of the manager's timed sounds
constexpr std::size_t kOccupant = 0x8;
constexpr std::size_t kSound = 0xC;
constexpr std::size_t kLast = 0x10;         // float: the hour of the last look
constexpr std::size_t kCount = 0x14;
constexpr std::size_t kHours = 0x18;        // floats
constexpr std::size_t kPlaying = 0x24;      // byte
constexpr std::size_t kLasting = 0x25;      // byte: on and off hours, not single ones
} // namespace timed

constexpr uint32_t kFrameworkMessages = 0x04FA845B;
constexpr uint32_t kMessagesIid = 0x652294C7;
constexpr uint32_t kSoundService = 0x23CECBA0;
constexpr uint32_t kSoundIid = 0x106077D2;
constexpr uint32_t kTuningChanged = 0xC53D10AA;
constexpr uint32_t kExemplarType = 0x6534284A;
constexpr uint32_t kExemplarGroup = 0xCA25875D;
constexpr uint32_t kExemplarIid = 0xA52160F5;
constexpr uint32_t kFilterIid = 0x4A40074C;
constexpr uint32_t kAutomatonIid = 0xA9B40F05;
constexpr uint32_t kSoundProperty = 0x6A36AFAC;
constexpr uint32_t kSilentProperty = 0x029244DB;
constexpr uint32_t kResultIid = 0x5B523BA4;
constexpr uint32_t kStart = 0xEA451903;
constexpr uint32_t kPlaceAt = 0xCA45190F;
constexpr uint32_t kStop = 0x6A451908;

// The game's texts, used where they lie.
// "Error: cSC4AudioAmbienceLayer::_StaticRemoveAllFilters : Failed to release filter (%08x)\n"
constexpr std::uintptr_t kTextRelease = 0x00A93330;
// "Error : cSC4AudioAmbienceLayer::_LoadFreshnessScore : Failed to load freshness score %08x:%08x:%08x\n"
constexpr std::uintptr_t kTextScore = 0x00A93390;
// "Error: cSC4AudioAmbienceLayer::_AddFilter : Failed to init filter (%08x)\n"
constexpr std::uintptr_t kTextInit = 0x00A933F8;
// "Error: cSC4AudioAmbienceLayer::_AddFilter : Failed to create class object filter (%08x)\n"
constexpr std::uintptr_t kTextCreate = 0x00A93448;
// "Error cSC4AudioAmbienceLayer::_ReadTuningValues : Failed to get resource (%08x)\n"
constexpr std::uintptr_t kTextResource = 0x00A93520;
// "Noname_0x%08x"
constexpr std::uintptr_t kTextNoName = 0x00A93574;
// ""
constexpr std::uintptr_t kTextEmpty = 0x00A80BF0;

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

// A node of the filters' hash table.
struct FilterNode {
    FilterNode* next;
    uint32_t classId;
    void* filter;
};

struct TableIterator {
    FilterNode* node;
    char* table;
};

// A curve's point, as bits of two floats, and a vector of them.
struct Point {
    uint32_t x;
    uint32_t y;
};

struct PointVector {
    Point* begin;
    Point* end;
    Point* capacity;
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

uint32_t AddRef(void* object)
{
    return Virtual<uint32_t>(object, 0x4);
}

uint32_t Release(void* object)
{
    return Virtual<uint32_t>(object, 0x8);
}

const char* Text(std::uintptr_t address)
{
    return reinterpret_cast<const char*>(address);
}

float AsFloat(uint32_t bits)
{
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

// The runtime's ftol (0x009EEF04): truncation.
int32_t Truncate(double value)
{
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

// An empty string as the game's inlined constructor makes it.
void InitString(GameString& text)
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
    text.zero = 0;
}

// The inlined destructor.
void DestroyString(GameString& text)
{
    text.vtable = addr::SC4String_Vtable;
    if (char* const buffer = text.body.begin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(buffer));
}

// The inlined assignment to a reference holder.
void Hold(void*& holder, void* object)
{
    void* const old = holder;
    if (object == old)
        return;
    AddRef(object);
    holder = object;
    if (old)
        Release(old);
}

// The framework's service `service` by interface `iid` into `out`, as the
// game's inlined helper asks for it.
void GetService(uint32_t service, uint32_t iid, void** out)
{
    if (void* const framework = Cdecl<void*>(addr::RZGetFramework))
        Virtual<uint8_t>(framework, 0x14, service, iid, out);
}

bool GetUint32(void* holder, uint32_t property, void* out)
{
    return Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, property, out) != 0;
}

// The number of 4-byte items in a vector at `vector` of `object`.
int32_t Count(char* object, std::size_t vector)
{
    return static_cast<int32_t>(At<char*>(object, vector + 4) - At<char*>(object, vector)) >> 2;
}

// The sound service's command that places a sound: the three coordinates are
// truncated, z first.
void PlaceSound(void* service, uint32_t key, const uint32_t* position)
{
    const int32_t z = Truncate(static_cast<double>(AsFloat(position[2])));
    const int32_t y = Truncate(static_cast<double>(AsFloat(position[1])));
    const int32_t x = Truncate(static_cast<double>(AsFloat(position[0])));
    Virtual<void>(service, 0x2C, kPlaceAt, key, x, y, z);
}

uint32_t Key(void* object, uint32_t sound)
{
    return static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(object)) + sound;
}

// --- The ambience layer ---------------------------------------------------------------

char* Filters()
{
    return reinterpret_cast<char*>(addr::SC4AudioAmbienceLayer_Filters);
}

// The game's 0x004E6C10 (no arguments, no object): _StaticRemoveAllFilters.
// Shuts every filter of the table down (slot 4), lets the table's reference
// go and empties the table. A filter whose count reaches zero before the
// table's own reference is released is reported.
void __cdecl RemoveAllFilters()
{
    TableIterator at;
    This<void*>(addr::SC4HashTable_Begin, Filters(), &at);
    FilterNode* node = at.node;
    if (node) {
        char* const table = at.table;
        do {
            void* const filter = node->filter;
            if (filter)
                AddRef(filter);
            Virtual<void>(filter, 0x10);
            if (Release(filter) == 0) {
                GameString text;
                InitString(text);
                Cdecl<void>(addr::cRZBaseString_Sprintf, &text, Text(kTextRelease), node->classId);
                DestroyString(text);
            }
            Release(filter);

            // The inlined step to the next node of the table.
            if (FilterNode* const next = node->next) {
                node = next;
            } else {
                const uint32_t buckets = static_cast<uint32_t>(Count(table, 0x4));
                uint32_t index = node->classId % buckets;
                node = nullptr;
                while (++index < buckets) {
                    node = At<FilterNode**>(table, 0x4)[index];
                    if (node)
                        break;
                }
            }
        } while (node);
    }
    This<void>(addr::SC4Uint32HashSet_Clear, Filters());
}

// The game's 0x004E6FD0 (the object is not used): the last layer's
// shutdown. The filters go, and the two services the layers share.
uint8_t __fastcall StaticShutdown(char*, void*)
{
    RemoveAllFilters();
    if (void* const resources = Global<void*>(addr::SC4Audio_ResourceManagerRef)) {
        Global<void*>(addr::SC4Audio_ResourceManagerRef) = nullptr;
        Release(resources);
    }
    if (void* const sound = Global<void*>(addr::SC4Audio_SoundService)) {
        Global<void*>(addr::SC4Audio_SoundService) = nullptr;
        Release(sound);
    }
    return 1;
}

// The game's 0x004E6E00 (slot 15): _LoadFreshnessScore. Nothing and false
// when the layer has the key (type, group, instance) already. Otherwise an
// object of class 0x3C1CF59E and a sound service object (command 0x2A2C0384)
// are made, the second is given the first and the key and handed to the
// service, and the key and the first object go to the end of the layer's
// list.
uint8_t __fastcall LoadFreshnessScore(char* self, void*, const uint32_t* key)
{
    char* const sentinel = At<char*>(self, layer::kScores);
    for (char* node = At<char*>(sentinel, 0x0); node != sentinel; node = At<char*>(node, 0x0)) {
        if (At<uint32_t>(node, 0x14) == key[0] && At<uint32_t>(node, 0x18) == key[1]
            && At<uint32_t>(node, 0x1C) == key[2])
            return 0;
    }

    void* score = nullptr;
    void* const com = Cdecl<void*>(addr::RZGetCOM);
    if (!Virtual<uint8_t>(com, 0xC, uint32_t{0x3C1CF59E}, uint32_t{0xDC1CF595}, &score)) {
        if (score)
            Release(score);
        return 0;
    }

    void* sound = nullptr;
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4Audio_SoundService), 0x34, uint32_t{0x2A2C0384},
                          kResultIid, &sound, uint32_t{0}, uint32_t{0}, uint32_t{0}, uint32_t{0})) {
        if (sound)
            Release(sound);
        if (score)
            Release(score);
        return 0;
    }

    Virtual<void>(Virtual<void*>(sound, 0x18), 0x84, score);
    Virtual<void>(Virtual<void*>(sound, 0x18), 0x2C, key[0]);
    Virtual<void>(Virtual<void*>(sound, 0x18), 0x40, key[1]);
    Virtual<void>(Virtual<void*>(sound, 0x18), 0x54, key[2]);
    Virtual<void>(Global<void*>(addr::SC4Audio_SoundService), 0x30, sound);

    void* const held = score;
    if (held)
        AddRef(held);
    const uint32_t group = key[1];
    const uint32_t instance = key[2];
    const uint32_t type = key[0];
    char* const tail = At<char*>(self, layer::kScores);
    char* const node = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{0x24});
    At<uint8_t>(node, 0x8) = 0;
    At<uint8_t>(node, 0x9) = 1;
    At<uint32_t>(node, 0xC) = 0;
    At<uint32_t>(node, 0x10) = 0;
    At<uint32_t>(node, 0x18) = group;
    At<uint32_t>(node, 0x14) = type;
    At<uint32_t>(node, 0x1C) = instance;
    At<void*>(node, 0x20) = held;
    if (held)
        AddRef(held);
    char* const before = At<char*>(tail, 0x4);
    At<char*>(node, 0x0) = tail;
    At<char*>(node, 0x4) = before;
    At<char*>(before, 0x0) = node;
    At<char*>(tail, 0x4) = node;
    if (held)
        Release(held);

    if (sound)
        Release(sound);
    if (score)
        Release(score);
    return 1;
}

// The game's 0x004E6D20 (slot 14): _LoadFreshnessScores. One score for every
// pair of group and instance of the tuning's list, through slot 15; a
// failure is reported.
void __fastcall LoadFreshnessScores(char* self, void*)
{
    const int32_t count = static_cast<int32_t>(static_cast<uint32_t>(Count(self, layer::kScoreKeys)) >> 1);
    for (int32_t i = 0; i < count; ++i) {
        const uint32_t* const pair = At<uint32_t*>(self, layer::kScoreKeys) + i * 2;
        uint32_t key[3];
        key[1] = pair[0];
        key[2] = pair[1];
        key[0] = 0x2026960B;
        if (Virtual<uint8_t>(self, 0x3C, &key[0]))
            continue;
        GameString text;
        text.body.begin = nullptr;
        text.body.end = nullptr;
        text.body.capacity = nullptr;
        char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
        text.body.begin = buffer;
        text.body.end = buffer;
        text.body.capacity = buffer + 8;
        *buffer = 0;
        const uint32_t instance = key[2];
        const uint32_t group = key[1];
        const uint32_t type = key[0];
        text.vtable = addr::SC4String_Vtable;
        text.zero = 0;
        Cdecl<void>(addr::cRZBaseString_Sprintf, &text, Text(kTextScore), type, group, instance);
        DestroyString(text);
    }
}

// The game's 0x004E7010: _AddFilter. The filter of a class comes from the
// shared table, or is made (interface 0x4A40074C), started (slot 3) and put
// there. The layer counts which of three further interfaces it answers and
// keeps it in the vector of its kind (1 to 3).
void __fastcall AddFilter(char* self, void*, uint32_t classId, int32_t kind)
{
    void* filter = nullptr;
    FilterNode** const buckets = At<FilterNode**>(Filters(), 0x4);
    const uint32_t bucketCount = static_cast<uint32_t>(Count(Filters(), 0x4));
    FilterNode* found = buckets[classId % bucketCount];
    while (found && found->classId != classId)
        found = found->next;

    if (found) {
        if (void* const kept = found->filter) {
            AddRef(kept);
            filter = kept;
        }
    } else {
        void* const com = Cdecl<void*>(addr::RZGetCOM);
        if (!Virtual<uint8_t>(com, 0xC, classId, kFilterIid, &filter)) {
            GameString text;
            InitString(text);
            Cdecl<void>(addr::cRZBaseString_Sprintf, &text, Text(kTextCreate), classId);
            DestroyString(text);
            if (filter)
                Release(filter);
            return;
        }
        if (!Virtual<uint8_t>(filter, 0xC)) {
            GameString text;
            This<void*>(addr::SC4String_SC4String, &text);
            Cdecl<void>(addr::cRZBaseString_Sprintf, &text, Text(kTextInit), classId);
            DestroyString(text);
            if (filter)
                Release(filter);
            return;
        }
        AddRef(filter);
        void* const made = filter;
        *This<void**>(addr::SC4Uint32HashMap_Lookup, Filters(), &classId) = made;
    }

    AddRef(filter);

    void* third = nullptr;
    void* second = nullptr;
    void* first = nullptr;
    if (Virtual<uint8_t>(filter, 0x0, uint32_t{0xCA400757}, &first))
        ++At<uint32_t>(self, layer::kFilterCounts);
    if (Virtual<uint8_t>(filter, 0x0, uint32_t{0xCA400753}, &second))
        ++At<uint32_t>(self, layer::kFilterCounts + 4);
    if (Virtual<uint8_t>(filter, 0x0, uint32_t{0xEA40074F}, &third))
        ++At<uint32_t>(self, layer::kFilterCounts + 8);

    if (kind >= 1 && kind <= 3) {
        char* const group = self + layer::kKinds + static_cast<std::size_t>(kind - 1) * layer::kKindSize;
        void* item = filter;
        This<void>(addr::SC4PtrVector_PushBack, group + layer::kKindFilters, &item);
        ++At<uint32_t>(group, layer::kKindCount);
    }

    if (third)
        Release(third);
    if (second)
        Release(second);
    if (first)
        Release(first);
    if (filter)
        Release(filter);
}

// The game's 0x004E7290: the constructor; the argument is the instance of
// the tuning exemplar.
char* __fastcall LayerConstruct(char* self, void*, uint32_t instance)
{
    const std::uintptr_t nothing = addr::SC4Audio_ZeroFloat;
    At<std::uintptr_t>(self, 0x0) = addr::SC4AudioAmbienceLayer_Vtable;
    At<uint8_t>(self, layer::kStarted) = 0;
    At<uint32_t>(self, layer::kReferences) = 0;
    At<uint8_t>(self, layer::kFlagD) = 0;
    At<uint8_t>(self, layer::kFlagC) = 1;
    At<uint32_t>(self, layer::kFloat20) = 0x42480000;
    At<uint32_t>(self, layer::kFloat24) = 0x42480000;
    At<void*>(self, layer::kHeld28) = nullptr;
    At<void*>(self, layer::kHeld2C) = nullptr;
    At<void*>(self, layer::kHeld30) = nullptr;
    At<uint32_t>(self, layer::kTuning34) = 1;
    At<uint32_t>(self, layer::kTuning38) = 5;
    for (std::size_t at = layer::kVectors; at <= 0xA4; at += 4)
        At<uint32_t>(self, at) = Global<uint32_t>(nothing);
    At<uint32_t>(self, 0xA8) = 0;
    At<uint32_t>(self, 0xAC) = 0;
    for (std::size_t at = 0xB0; at <= 0xD0; at += 4)
        At<uint32_t>(self, at) = Global<uint32_t>(nothing);
    At<uint32_t>(self, 0xD4) = 0;
    This<void*>(addr::SC4Audio_F4F81D0, self + layer::kTimer);
    At<uint32_t>(self, 0x124) = 0;
    At<uint32_t>(self, 0x128) = 0;
    At<uint8_t>(self, 0x12C) = 0;
    At<uint8_t>(self, 0x12D) = 0;
    for (std::size_t at = 0x138; at <= 0x17C; at += 4)
        At<uint32_t>(self, at) = 0;

    At<char*>(self, layer::kScores) = nullptr;
    char* const scores = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{0x24});
    At<char*>(scores, 0x0) = scores;
    At<char*>(scores, 0x4) = scores;
    At<char*>(self, layer::kScores) = scores;

    At<char*>(self, layer::kPairs) = nullptr;
    char* const pairs = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{0x20});
    At<char*>(pairs, 0x0) = pairs;
    At<char*>(pairs, 0x4) = pairs;
    At<char*>(self, layer::kPairs) = pairs;

    GameString& name = At<GameString>(self, layer::kName);
    InitString(name);

    At<uint32_t>(self, 0x19C) = 0;
    This<void>(addr::SC4Curve_Init, self + layer::kCurves);
    This<void>(addr::SC4Curve_Init, self + layer::kCurves + 0x10);
    This<void>(addr::SC4Curve_Init, self + layer::kCurves + 0x20);
    At<uint32_t>(self, layer::kScoreCount) = 0;
    At<uint32_t>(self, layer::kScoreKeys) = 0;
    At<uint32_t>(self, layer::kScoreKeys + 4) = 0;
    At<uint32_t>(self, layer::kScoreKeys + 8) = 0;
    At<uint8_t>(self, layer::kByte1E0) = 0;
    At<uint8_t>(self, layer::kByte1E1) = 0;
    At<uint32_t>(self, 0x1E4) = 0;
    At<uint32_t>(self, layer::kTuning1E8) = 0;
    At<uint32_t>(self, layer::kTuning1EC) = 0;
    At<uint32_t>(self, layer::kInstance) = instance;
    for (std::size_t at = layer::kKinds; at <= 0x258; at += 4)
        At<uint32_t>(self, at) = 0;
    return self;
}

// The game's 0x004E7650 (slot 6): the layer's name.
char* __fastcall LayerName(char* self, void*)
{
    return self + layer::kName;
}

// The game's 0x004E7660 (slot 7): sets the name, in lower case.
uint32_t __fastcall LayerSetName(char* self, void*, void* text)
{
    char* const name = self + layer::kName;
    Virtual<void>(name, 0x38, text);
    return This<uint32_t>(addr::cRZBaseString_MakeLower, name);
}

// The game's 0x004E7690 (slot 8).
uint32_t __fastcall LayerSetField1FC(char* self, void*, uint32_t value)
{
    At<uint32_t>(self, layer::kField1FC) = value;
    return value;
}

// The game's 0x004E76B0 (slot 10): three floats.
uint32_t* __fastcall LayerGetVector(char* self, void*, uint32_t* out)
{
    out[0] = At<uint32_t>(self, layer::kVectorC8);
    out[1] = At<uint32_t>(self, layer::kVectorC8 + 4);
    out[2] = At<uint32_t>(self, layer::kVectorC8 + 8);
    return out;
}

void FreeAt(char* self, std::size_t offset)
{
    if (void* const memory = At<void*>(self, offset))
        Cdecl<void>(addr::SC4_Free, memory);
}

// The game's 0x004E76E0: the destructor.
void __fastcall LayerDestruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4AudioAmbienceLayer_Vtable;
    FreeAt(self, 0x24C);
    FreeAt(self, 0x240);
    FreeAt(self, 0x230);
    FreeAt(self, 0x224);
    FreeAt(self, 0x214);
    FreeAt(self, 0x208);
    FreeAt(self, layer::kScoreKeys);
    FreeAt(self, layer::kCurves + 0x20);
    FreeAt(self, layer::kCurves + 0x10);
    FreeAt(self, layer::kCurves);
    At<std::uintptr_t>(self, layer::kName) = addr::SC4String_Vtable;
    FreeAt(self, layer::kName + 4);
    This<void>(addr::SC4PairList_Destroy, self + layer::kPairs);
    This<void>(addr::SC4Audio_F4E6300, self + layer::kScores);
    FreeAt(self, layer::kScores);
    FreeAt(self, layer::kBits + 0x28);
    FreeAt(self, layer::kBits + 0x14);
    FreeAt(self, layer::kBits);
    // An empty function (0x00407990) stands for the timer's destructor.
    This<void>(addr::SC4AgentBase_MonthPassed, self + layer::kTimer);
    if (void* const held = At<void*>(self, layer::kHeld30))
        Release(held);
    if (void* const held = At<void*>(self, layer::kHeld2C))
        Release(held);
    if (void* const held = At<void*>(self, layer::kHeld28))
        Release(held);
}

// The game's 0x004E7860 (slot 5): Shutdown. The last layer to stop takes the
// shared filters and services with it.
uint8_t __fastcall LayerShutdown(char* self, void*)
{
    if (At<uint8_t>(self, layer::kStarted)) {
        void* messages = nullptr;
        GetService(kFrameworkMessages, kMessagesIid, &messages);
        Virtual<void>(messages, 0x18, self, kTuningChanged);
        This<void>(addr::SC4Audio_F4E50A0, self);
        This<void>(addr::SC4Audio_F4E6A70, self);
        This<void>(addr::SC4Audio_F4E6910, self);
        This<void>(addr::SC4Audio_F4E6280, self);
        if (--Global<uint32_t>(addr::SC4AudioAmbienceLayer_InstanceCount) == 0)
            StaticShutdown(self, nullptr);
        At<uint8_t>(self, layer::kStarted) = 0;
        This<void>(addr::SC4RefPtr_Destruct, &messages);
    }
    return 1;
}

// Empties a bit vector (0x14 bytes: the first and the last place as a word
// and a bit each, and the end of the storage).
void ClearBits(char* bits)
{
    uint32_t result[2];
    This<void*>(addr::SC4BitVector_Erase, bits, &result[0], At<uint32_t>(bits, 0x0), At<uint32_t>(bits, 0x4),
                At<uint32_t>(bits, 0x8), At<uint32_t>(bits, 0xC));
}

// The game's 0x004E78E0: the layer's filters, made again from the class IDs
// of the tuning's three lists. The three bit vectors are emptied and then
// sized to the lists.
void __fastcall AddFilters(char* self, void*)
{
    At<uint32_t>(self, layer::kFilterCounts + 4) = 0;
    At<uint32_t>(self, layer::kFilterCounts) = 0;
    At<uint32_t>(self, layer::kFilterCounts + 8) = 0;
    ClearBits(self + layer::kBits);
    ClearBits(self + layer::kBits + 0x14);
    ClearBits(self + layer::kBits + 0x28);
    for (int32_t kind = 1; kind <= 3; ++kind) {
        const std::size_t ids = layer::kKinds + static_cast<std::size_t>(kind - 1) * layer::kKindSize;
        const int32_t count = Count(self, ids);
        This<void>(addr::SC4BitVector_Resize, self + layer::kBits + static_cast<std::size_t>(kind - 1) * 0x14,
                   count, uint32_t{0});
        for (int32_t i = 0; i < count; ++i)
            AddFilter(self, nullptr, At<uint32_t*>(self, ids)[i], kind);
    }
}

// The game's 0x004E7A60 (slot 2): Release.
uint32_t __fastcall LayerRelease(char* self, void*)
{
    const int32_t count = At<int32_t>(self, layer::kReferences);
    if (count > 1) {
        At<int32_t>(self, layer::kReferences) = count - 1;
        return static_cast<uint32_t>(count - 1);
    }
    LayerDestruct(self, nullptr);
    Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return 0;
}

// Adds a point to the end of a vector as the inlined push_back does.
void PushPoint(PointVector& points, Point& value, uint32_t x, uint32_t y, uint8_t* tag)
{
    value.x = x;
    value.y = y;
    if (points.end != points.capacity) {
        if (points.end) {
            points.end->x = x;
            points.end->y = y;
        }
        ++points.end;
    } else {
        This<void>(addr::SC4PointVector_InsertN, &points, points.end, &value, tag, uint32_t{1}, uint32_t{1});
    }
}

// Empties a vector of numbers and fills it from a property.
void ReadNumbers(void* holder, uint32_t property, char* vector)
{
    At<char*>(vector, 0x4) = At<char*>(vector, 0x0);
    Cdecl<uint8_t>(addr::SC4_GetPropertyUint32Vector, holder, property, vector);
}

// The game's 0x004E7A90: _ReadTuningValues. Sets the defaults, then reads
// the layer's exemplar (type 0x6534284A, group 0xCA25875D and the given
// instance); false and a report when there is none.
uint8_t __fastcall ReadTuningValues(char* self, void*, uint32_t instance)
{
    constexpr uint32_t k20 = 0x41A00000;   // 20.0f
    constexpr uint32_t k100 = 0x42C80000;  // 100.0f
    constexpr uint32_t k1024 = 0x44800000; // 1024.0f

    This<void>(addr::SC4Audio_F4F7BF0, self + layer::kTimer, 100.0f, 4000.0f);
    char* const name = self + layer::kName;
    using SprintfFn = void*(__cdecl*)(void*, const char*, ...);
    Global<SprintfFn>(At<std::uintptr_t>(name, 0x0) + 0x6C)(name, Text(kTextNoName), instance);

    At<uint8_t>(self, layer::kByte1E1) = 0x1F;
    At<uint32_t>(self, layer::kTuning1EC) = 0x400;
    At<uint32_t>(self, layer::kTuning1E8) = 0x400;
    At<uint32_t>(self, layer::kFloat20) = 0x42480000;
    At<uint32_t>(self, layer::kFloat24) = 0x42480000;
    At<uint32_t>(self, layer::kTuning1F0) = 0x64;
    At<uint32_t>(self, layer::kTuning34) = 1;
    At<uint32_t>(self, layer::kTuning38) = 5;
    At<uint32_t>(self, layer::kFloat200) = 0x3F800000;
    At<uint8_t>(self, layer::kByte1E0) = 0x1F;
    At<uint32_t>(self, layer::kPreference) =
        At<uint32_t>(At<char*>(Global<char*>(addr::SC4_Preferences43CC4), 0x18), 0x314);

    // The three curves' defaults.
    uint8_t tag;
    Point value;
    PointVector points = {nullptr, nullptr, nullptr};
    PushPoint(points, value, 0, k100, &tag);
    PushPoint(points, value, k20, k100, &tag);
    This<void>(addr::SC4Curve_SetPoints, self + layer::kCurves, &points);
    points.end = points.begin;
    PushPoint(points, value, 0, 0, &tag);
    PushPoint(points, value, k20, k1024, &tag);
    This<void>(addr::SC4Curve_SetPoints, self + layer::kCurves + 0x10, &points);
    points.end = points.begin;
    PushPoint(points, value, 0, k1024, &tag);
    PushPoint(points, value, k20, k1024, &tag);
    This<void>(addr::SC4Curve_SetPoints, self + layer::kCurves + 0x20, &points);

    uint32_t key[3];
    key[2] = instance;
    key[0] = kExemplarType;
    key[1] = kExemplarGroup;
    void* exemplar = nullptr;
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4Audio_ResourceManagerRef), 0xC, &key[0], kExemplarIid,
                          &exemplar, uint32_t{0}, uint32_t{0})) {
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
        Cdecl<void>(addr::cRZBaseString_Sprintf, &text, Text(kTextResource), instance);
        DestroyString(text);
        if (exemplar)
            Release(exemplar);
        if (points.begin)
            Cdecl<void>(addr::SC4_Free, static_cast<void*>(points.begin));
        return 0;
    }

    void* const holder = Virtual<void*>(exemplar, 0x1C);
    if (holder)
        AddRef(holder);

    // The name: emptied, then the exemplar's own (property 0x20).
    GameString empty;
    This<void*>(addr::SC4String_FromText, &empty, Text(kTextEmpty));
    if (&empty.body != &At<StringBody>(self, layer::kName + 4))
        This<void>(addr::SC4String_AssignRange, self + layer::kName + 4, empty.body.begin, empty.body.end, &tag);
    if (empty.body.begin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(empty.body.begin));
    Cdecl<uint8_t>(addr::SC4_GetPropertyString, holder, uint32_t{0x20}, name);

    ReadNumbers(holder, 0xEA3FBDD7, self + layer::kScoreKeys);
    ReadNumbers(holder, 0x2A3FBDE1, self + layer::kKinds);
    ReadNumbers(holder, 0x2A3FBDE5, self + layer::kKinds + layer::kKindSize);
    ReadNumbers(holder, 0x0A410BA0, self + layer::kKinds + 2 * layer::kKindSize);

    At<uint32_t>(self, layer::kScoreCount) = 0;
    GetUint32(holder, 0x6A3FBDDA, self + layer::kScoreCount);
    At<uint8_t>(self, layer::kByte1E0) = 0;
    Cdecl<uint8_t>(addr::SC4_GetPropertyUint8, holder, uint32_t{0x0A25896B}, self + layer::kByte1E0);
    At<uint8_t>(self, layer::kByte1E1) = 0;
    Cdecl<uint8_t>(addr::SC4_GetPropertyUint8, holder, uint32_t{0xAA3FBDDE}, self + layer::kByte1E1);
    GetUint32(holder, 0xEA3FC42A, self + layer::kTuning1EC);
    GetUint32(holder, 0xAA3FC48E, self + layer::kTuning1E8);
    GetUint32(holder, 0x6A3FC4C7, self + layer::kTuning1F0);
    GetUint32(holder, 0x8A552758, self + layer::kTuning34);
    GetUint32(holder, 0x8A70B1A2, self + layer::kTuning38);
    Cdecl<uint8_t>(addr::SC4_GetPropertyFloat, holder, uint32_t{0x8A3FC5DD}, self + layer::kFloat200);

    This<uint8_t>(addr::SC4Curve_LoadFromExemplar, self + layer::kCurves, &key[0], uint32_t{0xCA4132E9});
    This<uint8_t>(addr::SC4Curve_LoadFromExemplar, self + layer::kCurves + 0x10, &key[0], uint32_t{0x6A4132F3});
    This<uint8_t>(addr::SC4Curve_LoadFromExemplar, self + layer::kCurves + 0x20, &key[0], uint32_t{0x6A4132F6});

    Cdecl<uint8_t>(addr::SC4_GetPropertyFloat, holder, uint32_t{0x0A4168E1}, self + layer::kFloat20);
    At<uint32_t>(self, layer::kFloat24) = At<uint32_t>(self, layer::kFloat20);
    Cdecl<uint8_t>(addr::SC4_GetPropertyFloat, holder, uint32_t{0x0A56212A}, self + layer::kFloat24);

    // The timer's two times, whole numbers in the exemplar.
    uint32_t first = 0x64;
    uint32_t second = 0xFA0;
    GetUint32(holder, 0x8AA039B7, &first);
    GetUint32(holder, 0x4AA03DB1, &second);
    const float secondTime = static_cast<float>(static_cast<double>(second));
    const float firstTime = static_cast<float>(static_cast<double>(first));
    This<void>(addr::SC4Audio_F4F7BF0, self + layer::kTimer, firstTime, secondTime);

    if (holder)
        Release(holder);
    if (exemplar)
        Release(exemplar);
    if (points.begin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(points.begin));
    return 1;
}

// The game's 0x004E8180 (slot 4): Init. The first layer sets the shared
// services up (0x004E4EB0); every layer reads its tuning, listens for
// 0xC53D10AA, makes its filters and loads its freshness scores.
uint8_t __fastcall LayerInit(char* self, void*)
{
    if (At<uint8_t>(self, layer::kStarted))
        return 1;
    At<uint8_t>(self, layer::kStarted) = 1;
    const uint32_t others = Global<uint32_t>(addr::SC4AudioAmbienceLayer_InstanceCount);
    Global<uint32_t>(addr::SC4AudioAmbienceLayer_InstanceCount) = others + 1;
    if (others == 0 && !This<uint8_t>(addr::SC4Audio_F4E4EB0, self))
        return 0;

    ReadTuningValues(self, nullptr, At<uint32_t>(self, layer::kInstance));
    void* messages = nullptr;
    GetService(kFrameworkMessages, kMessagesIid, &messages);
    Virtual<void>(messages, 0x14, self, kTuningChanged);
    AddFilters(self, nullptr);
    This<void>(addr::SC4Audio_F4E4F60, self);
    Virtual<void>(self, 0x38);
    Virtual<void>(self, 0x30, At<uint32_t>(self, layer::kScoreCount));
    This<void>(addr::SC4RefPtr_Destruct, &messages);
    return 1;
}

// The game's 0x004E8230 (slot 3): DoMessage. On 0xC53D10AA the tuning is
// read again and the layer started afresh; slot 11 is called twice with the
// inverse of the byte at +0xC as it is each time.
uint8_t __fastcall LayerDoMessage(char* self, void*, void* message)
{
    if (!message || Virtual<uint32_t>(message, 0x10) != kTuningChanged)
        return 0;
    ReadTuningValues(self, nullptr, At<uint32_t>(self, layer::kInstance));
    This<void>(addr::SC4Audio_F4E6A70, self);
    AddFilters(self, nullptr);
    This<void>(addr::SC4Audio_F4E50A0, self);
    This<void>(addr::SC4Audio_F4E4F60, self);
    Virtual<void>(self, 0x2C, uint32_t{At<uint8_t>(self, layer::kFlagC) == 0});
    Virtual<void>(self, 0x2C, uint32_t{At<uint8_t>(self, layer::kFlagC) == 0});
    return 1;
}

// --- The ambience manager -------------------------------------------------------------

// The game's 0x004E82B0 (slot 2): Release; the last reference deletes
// through slot 13.
uint32_t __fastcall ManagerRelease(char* self, void*)
{
    const uint32_t count = At<uint32_t>(self, manager::kReferences);
    if (count > 1) {
        At<uint32_t>(self, manager::kReferences) = count - 1;
        return count - 1;
    }
    Virtual<void>(self, 0x34, uint32_t{1});
    return 0;
}

// The game's 0x004E82D0 (slot 0): QueryInterface.
uint8_t __fastcall ManagerQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    if (iid != 1 && iid != 0x287FB697 && iid != 0x6A12F51C)
        return 0;
    *out = self;
    AddRef(self);
    return 1;
}

// The game's 0x004E8300 (the filter's slot 4): whether occupants of a type
// pass.
uint8_t __fastcall FilterIsTypeIncluded(char*, void*, uint32_t type)
{
    switch (type) {
    case 0x278128A0:
    case 0x480E6151:
    case 0x74758926:
    case 0x897F7EC2:
    case 0xA823821E:
    case 0xC772BF98:
    case 0xC7CC75F1:
        return 1;
    default:
        return 0;
    }
}

// The game's 0x004E8350 (the filter's slot 0): QueryInterface.
uint8_t __fastcall FilterQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    if (iid != 1 && iid != 0x46A80026)
        return 0;
    *out = self;
    AddRef(self);
    return 1;
}

// The game's 0x004E8CD0 (the filter's slot 2): Release.
uint32_t __fastcall FilterRelease(char* self, void*)
{
    const int32_t count = At<int32_t>(self, 0x4);
    if (count > 1) {
        At<int32_t>(self, 0x4) = count - 1;
        return static_cast<uint32_t>(count - 1);
    }
    At<std::uintptr_t>(self, 0x0) = addr::SC4AudioAmbienceManager_FilterVtable;
    Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return 0;
}

// The game's 0x004E8440: a reference holder's destructor (the original runs
// the release under an exception frame of its own).
void __fastcall HolderDestruct(void** holder, void*)
{
    if (*holder)
        Release(*holder);
}

// The game's 0x004E8490 (no arguments, no object): the listener kept by the
// sound code's static object (0x00B21978) at +0x64, null without the object.
void* __cdecl Listener()
{
    void* const statics = Cdecl<void*>(addr::SC4Sound_StaticObject);
    return statics ? At<void*>(statics, 0x64) : nullptr;
}

constexpr uint32_t kManagerMessages[] = {
    0x26D31EC1, 0x26D31EC2, 0x8A4A69C7, 0x99EF1142, 0x99EF1146, 0x99EF1143,
    0xE99B3D4D, 0xE8BDA2E7, 0x09D92FC3, 0xC53D10AA, 0x8A878385, 0x4348B111,
};

constexpr uint32_t kManagerMessagesLeaving[] = {
    0x4348B111, 0x8A878385, 0x26D31EC1, 0x26D31EC2, 0x8A4A69C7, 0x99EF1142,
    0x99EF1146, 0x99EF1143, 0xE8BDA2E7, 0xE99B3D4D, 0x09D92FC3, 0xC53D10AA,
};

// The game's 0x004E84D0 (slot 8): Init. Joins the framework's services,
// takes service 0x49E50BA1 and the sound service, gives the sound service
// its callback, listens for twelve messages and makes the filter of class
// 0x4A401BA7.
uint8_t __fastcall ManagerInit(char* self, void*)
{
    void* framework = Global<void*>(addr::SC4Audio_Framework);
    if (!framework) {
        framework = Cdecl<void*>(addr::RZGetFramework);
        Global<void*>(addr::SC4Audio_Framework) = framework;
        if (!framework)
            return 0;
    }
    Virtual<void>(framework, 0xC, self);

    void* service = nullptr;
    GetService(0x49E50BA1, 0x29E39DB4, &service);
    if (!service) {
        HolderDestruct(&service, nullptr);
        return 0;
    }
    Hold(At<void*>(self, manager::kService1C), service);

    void* sound = nullptr;
    GetService(kSoundService, kSoundIid, &sound);
    if (sound) {
        This<void>(addr::SC4RefPtr_Assign, self + manager::kSound, sound);
        Virtual<void>(At<void*>(self, manager::kSound), 0x3C, self ? self + manager::kCallback : nullptr);

        void* messages = nullptr;
        GetService(kFrameworkMessages, kMessagesIid, &messages);
        if (messages) {
            for (const uint32_t id : kManagerMessages)
                Virtual<void>(messages, 0x14, self + manager::kTarget, id);
        }

        void* const com = Cdecl<void*>(addr::RZGetCOM);
        void*& filter = At<void*>(self, manager::kClassFilter);
        if (void* const old = filter) {
            filter = nullptr;
            Release(old);
        }
        if (Virtual<uint8_t>(com, 0xC, uint32_t{0x4A401BA7}, kFilterIid, &filter)) {
            This<void>(addr::SC4Stopwatch_Start, self + manager::kStopwatch);
            At<uint8_t>(self, manager::kStarted) = 1;
            This<void>(addr::SC4RefPtr_Destruct, &messages);
            This<void>(addr::SC4RefPtr_DestructGuarded, &sound);
            HolderDestruct(&service, nullptr);
            return 1;
        }
        This<void>(addr::SC4RefPtr_Destruct, &messages);
    }
    This<void>(addr::SC4RefPtr_DestructGuarded, &sound);
    HolderDestruct(&service, nullptr);
    return 0;
}

// The game's 0x004E8720: starts or stops the sound of an occupant (its
// property 0x6A36AFAC). `occupant` is the occupant's part at +4, whose slot
// 6 gives the property holder and slot 8 the place. Started sounds are
// looped and placed.
void __fastcall OccupantSound(char* self, void*, void* occupant, uint32_t start)
{
    if (!occupant || !At<void*>(self, manager::kSound))
        return;
    void* const holder = Virtual<void*>(occupant, 0x18);
    if (!holder)
        return;
    uint32_t sound = 0;
    if (!GetUint32(holder, kSoundProperty, &sound))
        return;
    if (!static_cast<uint8_t>(start)) {
        Virtual<void>(At<void*>(self, manager::kSound), 0x2C, kStop, Key(occupant, sound), uint32_t{0}, uint32_t{0},
                      uint32_t{0});
        return;
    }
    const uint32_t nothing = Global<uint32_t>(addr::SC4AudioAmbienceManager_DefaultFloat);
    uint32_t position[3] = {nothing, nothing, nothing};
    Virtual<void>(occupant, 0x20, &position[0]);
    Virtual<void>(At<void*>(self, manager::kSound), 0x2C, kStart, sound, Key(occupant, sound), uint32_t{1},
                  uint32_t{0});
    PlaceSound(At<void*>(self, manager::kSound), Key(occupant, sound), position);
}

// The game's 0x004E8820: an occupant message (0x99EF1142 and its like).
// Kind 12 stops and kind 13 starts the sound of an occupant of type 0x10,
// 0x14 or 0x1E (a type 0x10 occupant with flag 0x10 is always stopped); kind
// 9 places the sound again.
uint8_t __fastcall OnOccupantMessage(char* self, void*, void* message)
{
    const uint32_t kind = Virtual<uint32_t>(message, 0x38);
    if (kind == 9) {
        if (!At<void*>(self, manager::kSound))
            return 1;
        const uint32_t nothing = Global<uint32_t>(addr::SC4AudioAmbienceManager_DefaultFloat);
        uint32_t position[3] = {nothing, nothing, nothing};
        void* const occupant = Virtual<void*>(message, 0x28);
        Virtual<void>(occupant, 0x1C, &position[0]);
        void* const holder = Virtual<void*>(Virtual<void*>(occupant, 0x14), 0x18);
        if (!holder)
            return 1;
        // The original reads the sound into the slot of its argument.
        uint32_t sound = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(message));
        if (!GetUint32(holder, kSoundProperty, &sound))
            return 1;
        void* const service = At<void*>(self, manager::kSound);
        const int32_t z = Truncate(static_cast<double>(AsFloat(position[2])));
        const int32_t y = Truncate(static_cast<double>(AsFloat(position[1])));
        const int32_t x = Truncate(static_cast<double>(AsFloat(position[0])));
        const uint32_t key = Key(Virtual<void*>(occupant, 0x14), sound);
        Virtual<void>(service, 0x2C, kPlaceAt, key, x, y, z);
        return 1;
    }
    if (kind <= 0xB || kind > 0xD)
        return 1;

    bool stop = Virtual<uint32_t>(message, 0x38) == 0xC;
    const uint32_t type = Virtual<uint32_t>(message, 0x4C);
    if (type == 0x10) {
        if (Virtual<uint32_t>(Virtual<void*>(message, 0x28), 0xE4, uint32_t{0x10}) != 0)
            stop = true;
    } else if (type != 0x14 && type != 0x1E) {
        return 1;
    }
    char* const occupant = Virtual<char*>(message, 0x28);
    char* part = nullptr;
    if (occupant) {
        AddRef(occupant);
        part = occupant + 4;
    }
    OccupantSound(self, nullptr, part, uint32_t{!stop});
    if (occupant)
        Release(occupant);
    return 1;
}

// The game's 0x004E8990: message 0x8A878385. The message's object (slot 32)
// as an automaton (interface 0xA9B40F05) with property 0xAA87817A gets that
// sound, looped and placed where the automaton is; the key is the
// automaton.
uint8_t __fastcall OnAutomatonMessage(char* self, void*, void* message)
{
    if (!At<void*>(self, manager::kSound))
        return 1;
    if (Virtual<uint32_t>(message, 0x10) != 0x8A878385)
        return 0;

    void* automaton = nullptr;
    void* const object = Virtual<void*>(message, 0x80);
    if (object)
        AddRef(object);
    if (Virtual<uint8_t>(object, 0x0, kAutomatonIid, &automaton)) {
        void* const holder = Virtual<void*>(Virtual<void*>(automaton, 0x14), 0x18);
        uint32_t sound;
        if (GetUint32(holder, 0xAA87817A, &sound)) {
            const uint32_t nothing = Global<uint32_t>(addr::SC4AudioAmbienceManager_DefaultFloat);
            uint32_t position[3] = {nothing, nothing, nothing};
            Virtual<void>(automaton, 0x1C, &position[0]);
            Virtual<void>(At<void*>(self, manager::kSound), 0x2C, kStart, sound, automaton, uint32_t{1},
                          uint32_t{0});
            PlaceSound(At<void*>(self, manager::kSound), Key(automaton, 0), position);
        }
    }
    Release(object);
    if (automaton)
        Release(automaton);
    return 1;
}

// The game's 0x004E8AD0: starts a sound for an occupant with flag 5 unless
// its property 0x029244DB says no. An automaton is told the manager's
// message target (slot 39). The sound is placed at the occupant.
uint8_t __fastcall StartSound(char* self, void*, void* occupant, uint32_t sound, uint32_t looped)
{
    if (!occupant || !Virtual<uint8_t>(occupant, 0x80, uint32_t{5}))
        return 0;
    // The original keeps the answer in the low byte of its first argument.
    uint32_t silent = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(occupant)) & 0xFFFFFF00;
    void* const holder = Virtual<void*>(occupant, 0x18);
    Cdecl<uint8_t>(addr::SC4_GetPropertyBool, holder, kSilentProperty, &silent);
    if (static_cast<uint8_t>(silent))
        return 0;

    void* automaton = nullptr;
    if (Virtual<uint8_t>(occupant, 0x0, kAutomatonIid, &automaton))
        Virtual<void>(automaton, 0x9C, self ? self + manager::kTarget : nullptr);

    if (At<void*>(self, manager::kSound)) {
        const uint32_t nothing = Global<uint32_t>(addr::SC4AudioAmbienceManager_DefaultFloat);
        uint32_t position[3] = {nothing, nothing, nothing};
        Virtual<void>(occupant, 0x20, &position[0]);
        void* const service = At<void*>(self, manager::kSound);
        const uint32_t key = Key(occupant, sound);
        Virtual<void>(service, 0x2C, kStart, sound, key, uint32_t{static_cast<uint8_t>(looped) != 0}, uint32_t{0});
        PlaceSound(At<void*>(self, manager::kSound), key, position);
    }
    if (automaton)
        Release(automaton);
    return 1;
}

// The game's 0x004E8BF0: stops an occupant's sound. An automaton is told
// (slot 40).
uint8_t __fastcall StopSound(char* self, void*, void* occupant, uint32_t sound)
{
    if (!occupant)
        return 0;
    void* automaton = nullptr;
    if (Virtual<uint8_t>(occupant, 0x0, kAutomatonIid, &automaton))
        Virtual<void>(automaton, 0xA0, self ? self + manager::kTarget : nullptr);
    if (void* const service = At<void*>(self, manager::kSound))
        Virtual<void>(service, 0x2C, kStop, Key(occupant, sound), uint32_t{0}, uint32_t{0}, uint32_t{0});
    if (automaton)
        Release(automaton);
    return 1;
}

// The game's 0x004E8C70: places the sound whose key is an occupant where the
// occupant is.
uint8_t __fastcall PlaceOccupantSound(char* self, void*, void* occupant)
{
    if (At<void*>(self, manager::kSound)) {
        uint32_t position[3];
        Virtual<void>(occupant, 0x20, &position[0]);
        PlaceSound(At<void*>(self, manager::kSound), Key(occupant, 0), position);
    }
    return 1;
}

// The game's 0x004E8CF0: a message with a number (slot 9). The manager's
// state becomes 1 for a number that is not zero and 4 for zero, and every
// layer's slot 28 is told whether it was zero.
uint8_t __fastcall OnStateMessage(char* self, void*, void* message)
{
    const bool on = Virtual<uint32_t>(message, 0x24) != 0;
    char* const sentinel = At<char*>(self, manager::kLayers);
    At<uint32_t>(self, manager::kState) = on ? 1 : 4;
    for (char* node = At<char*>(sentinel, 0x0); node != sentinel; node = At<char*>(node, 0x0))
        Virtual<void>(At<void*>(node, 0x8), 0x70, uint32_t{!on});
    return 1;
}

// The game's 0x004E8D40: an occupant has moved. Unless its property
// 0x029244DB says no, an occupant with a sound property has its sound
// placed (key: the occupant with nothing added), and one with property
// 0xCA19D7CA has it placed through 0x004E8C70.
void __fastcall OnOccupantMoved(char* self, void*, void* occupant)
{
    // The original keeps the answer in the low byte of its argument.
    uint32_t silent = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(occupant));
    void* const first = Virtual<void*>(occupant, 0x18);
    if (Cdecl<uint8_t>(addr::SC4_GetPropertyBool, first, kSilentProperty, &silent) && static_cast<uint8_t>(silent))
        return;
    void* const holder = Virtual<void*>(occupant, 0x18);
    if (!holder)
        return;
    if (Virtual<uint8_t>(holder, 0xC, kSoundProperty) && At<void*>(self, manager::kSound)) {
        uint32_t position[3];
        Virtual<void>(occupant, 0x20, &position[0]);
        PlaceSound(At<void*>(self, manager::kSound), Key(occupant, 0), position);
    }
    uint32_t value = 0;
    if (GetUint32(holder, 0xCA19D7CA, &value))
        PlaceOccupantSound(self, nullptr, occupant);
}

// The game's 0x004E8E10: an occupant found by the sweep. One with flag 4
// goes to every layer (slot 21).
uint8_t __fastcall SweepOccupant(char* self, void*, void* occupant)
{
    if (!At<void*>(self, manager::kService1C))
        return 0;
    if (Virtual<uint8_t>(occupant, 0x80, uint32_t{4})) {
        char* const sentinel = At<char*>(self, manager::kLayers);
        for (char* node = At<char*>(sentinel, 0x0); node != sentinel; node = At<char*>(node, 0x0))
            Virtual<void>(At<void*>(node, 0x8), 0x54, occupant);
    }
    return 1;
}

// The game's 0x004E8E60 (cdecl): the sweep's callback for the occupant
// manager; the context is the manager.
uint8_t __cdecl SweepCallback(void* occupant, char* context)
{
    if (!occupant || !context)
        return 0;
    AddRef(context);
    const uint8_t result = SweepOccupant(context, nullptr, occupant);
    Release(context);
    return result;
}

// The game's 0x004E8EA0: the sounds tied to hours of the day (the 24 hour
// clock, 0x00B43DBC). A lasting sound has on and off hours in turn and is
// started inside a span and stopped inside the next; a single sound is
// played once when one of its hours has come since the last look.
void __fastcall TimedSounds(char* self, void*)
{
    void* const clock = Global<void*>(addr::SC4_Clock43DBC);
    if (!clock)
        return;
    AddRef(clock);
    const uint32_t hour = Virtual<uint32_t>(clock, 0x28);
    const uint32_t minute = Virtual<uint32_t>(clock, 0x34);
    const float now = static_cast<float>(static_cast<double>(minute / 60) + static_cast<double>(hour));

    char* const sentinel = At<char*>(self, manager::kTimed);
    for (char* node = At<char*>(sentinel, 0x0); node != sentinel; node = At<char*>(node, 0x0)) {
        bool on = false;
        const int32_t count = At<int32_t>(node, timed::kCount);
        for (int32_t i = 0; i < count; ++i) {
            const float* const hours = At<float*>(node, timed::kHours);
            const float from = hours[i];
            on = !on;
            const float to = hours[i + 1 == count ? 0 : i + 1];

            if (At<uint8_t>(node, timed::kLasting)) {
                if (!(now >= from && now < to)) {
                    // A span over midnight.
                    if (!(to < from))
                        continue;
                    if (!(now >= from)) {
                        const double earlier = static_cast<double>(now) - static_cast<double>(24.0f);
                        if (!(earlier >= static_cast<double>(from)))
                            continue;
                        if (!(earlier < static_cast<double>(to)))
                            continue;
                    }
                }
                uint32_t sound = 0;
                void* const holder = Virtual<void*>(At<void*>(node, timed::kOccupant), 0x18);
                if (!holder)
                    continue;
                if (!GetUint32(holder, 0x2A53BE13, &sound))
                    continue;
                const uint8_t playing = At<uint8_t>(node, timed::kPlaying);
                if (on) {
                    if (playing)
                        continue;
                    StartSound(self, nullptr, At<void*>(node, timed::kOccupant), sound, uint32_t{1});
                    At<uint8_t>(node, timed::kPlaying) = 1;
                } else {
                    if (!playing)
                        continue;
                    StopSound(self, nullptr, At<void*>(node, timed::kOccupant), sound);
                    At<uint8_t>(node, timed::kPlaying) = 0;
                }
                continue;
            }

            if (!(now >= from))
                continue;
            if (!(At<float>(node, timed::kLast) < from))
                continue;
            // The original fetches the static object here and drops it.
            Cdecl<void*>(addr::SC4Sound_StaticObject);
            const uint32_t instance = Cdecl<uint32_t>(addr::SC4Sound_NextInstance);
            uint32_t position[3];
            Virtual<void>(At<void*>(node, timed::kOccupant), 0x20, &position[0]);
            void* result = nullptr;
            Virtual<void>(At<void*>(self, manager::kSound), 0x34, kStart, kResultIid, &result,
                          At<uint32_t>(node, timed::kSound), instance, uint32_t{1}, uint32_t{0});
            Virtual<void>(At<void*>(self, manager::kSound), 0x30, result);
            void* const service = At<void*>(self, manager::kSound);
            if (void* const old = result) {
                result = nullptr;
                Release(old);
            }
            const int32_t z = Truncate(static_cast<double>(AsFloat(position[2])));
            const int32_t y = Truncate(static_cast<double>(AsFloat(position[1])));
            const int32_t x = Truncate(static_cast<double>(AsFloat(position[0])));
            Virtual<void>(service, 0x34, kPlaceAt, kResultIid, &result, instance, x, y, z);
            Virtual<void>(At<void*>(self, manager::kSound), 0x30, result);
            if (result)
                Release(result);
        }
        At<float>(node, timed::kLast) = now;
    }
    Release(clock);
}

// The game's 0x004E91D0: the manager's tuning (exemplar instance
// 0x6A359C5C): the reach of the view for every zoom (70 for each of five
// when the property is missing) and a vector of numbers.
void __fastcall ManagerReadTuning(char* self, void*)
{
    void* const resources = Global<void*>(addr::SC4_ResourceManager);
    if (resources)
        AddRef(resources);
    void* exemplar = nullptr;
    uint32_t key[3] = {kExemplarType, kExemplarGroup, 0x6A359C5C};
    if (Virtual<uint8_t>(resources, 0xC, &key[0], kExemplarIid, &exemplar, uint32_t{0}, uint32_t{0})) {
        void* const holder = Virtual<void*>(exemplar, 0x1C);
        char* const reach = self + manager::kReach;
        if (!Cdecl<uint8_t>(addr::SC4_GetPropertyFloatVector, holder, uint32_t{0x0A36F410}, reach)) {
            for (std::size_t i = 0; i < 5; ++i)
                At<uint32_t*>(reach, 0x0)[i] = 0x428C0000;
        }
        Cdecl<uint8_t>(addr::SC4_GetPropertyUint32Vector, holder, uint32_t{0x2A3FF8CD}, self + manager::kVector60);
    }
    if (exemplar)
        Release(exemplar);
    Release(resources);
}

// The game's 0x004E92A0: the listener's place and the part of the city
// around it. The view is the square of the zoom's reach around the place,
// cut to the city's rectangle (0x004E8380), in cells with even numbers and
// inside the city; the earlier cells are kept beside the new ones.
void __fastcall UpdateView(char* self, void*)
{
    void* const statics = Cdecl<void*>(addr::SC4Sound_StaticObject);
    char* const listener = statics ? At<char*>(statics, 0x64) : nullptr;
    At<uint32_t>(self, manager::kPlace) = At<uint32_t>(listener, 0x24);
    At<uint32_t>(self, manager::kPlace + 4) = At<uint32_t>(listener, 0x28);
    At<uint32_t>(self, manager::kPlace + 8) = At<uint32_t>(listener, 0x2C);
    At<uint32_t>(self, manager::kFacing) = At<uint32_t>(listener, 0xC);
    At<uint32_t>(self, manager::kFacing + 4) = At<uint32_t>(listener, 0x10);
    At<uint32_t>(self, manager::kFacing + 8) = At<uint32_t>(listener, 0x14);

    const uint32_t zoom = At<uint32_t>(Global<char*>(addr::SC4_CameraView), 0xC);
    const float* const reach = At<float*>(self, manager::kReach) + zoom;
    At<uint32_t>(self, manager::kZoom) = zoom;
    float around[4];
    around[0] = static_cast<float>(static_cast<double>(At<float>(self, manager::kPlace))
                                   - static_cast<double>(*reach));
    around[1] = static_cast<float>(static_cast<double>(At<float>(self, manager::kPlace + 8))
                                   - static_cast<double>(*reach));
    around[2] = static_cast<float>(static_cast<double>(*reach)
                                   + static_cast<double>(At<float>(self, manager::kPlace)));
    around[3] = static_cast<float>(static_cast<double>(*reach)
                                   + static_cast<double>(At<float>(self, manager::kPlace + 8)));
    This<uint8_t>(addr::SC4Audio_F4E8380, self + manager::kView, &around[0], self + manager::kCity);

    int32_t x0;
    int32_t z0;
    int32_t x1;
    int32_t z1;
    Virtual<void>(At<void*>(self, manager::kOccupants), 0x4C, At<uint32_t>(self, manager::kView),
                  At<uint32_t>(self, manager::kView + 4), &x0, &z0);
    Virtual<void>(At<void*>(self, manager::kOccupants), 0x4C, At<uint32_t>(self, manager::kView + 8),
                  At<uint32_t>(self, manager::kView + 0xC), &x1, &z1);
    const int32_t lastX = Virtual<int32_t>(Global<void*>(addr::SC4_City), 0x174) - 1;
    const int32_t lastZ = Virtual<int32_t>(Global<void*>(addr::SC4_City), 0x178) - 1;

    const int32_t left = (x0 < 0 ? 0 : x0) & ~1;
    const int32_t right = (lastX < x1 ? lastX : x1) & ~1;
    const int32_t top = (z0 < 0 ? 0 : z0) & ~1;
    const int32_t bottom = (lastZ < z1 ? lastZ : z1) & ~1;

    if (Global<uint8_t>(addr::SC4AudioAmbienceManager_FirstView)) {
        At<int32_t>(self, manager::kCellsBefore + 8) = right;
        At<int32_t>(self, manager::kCellsBefore) = left;
        At<int32_t>(self, manager::kCellsBefore + 4) = top;
        At<int32_t>(self, manager::kCellsBefore + 0xC) = bottom;
        At<int32_t>(self, manager::kCells + 8) = right;
        At<int32_t>(self, manager::kCells) = left;
        At<int32_t>(self, manager::kCells + 4) = top;
        At<int32_t>(self, manager::kCells + 0xC) = bottom;
        Global<uint8_t>(addr::SC4AudioAmbienceManager_FirstView) = 0;
        return;
    }
    At<int32_t>(self, manager::kCellsBefore) = At<int32_t>(self, manager::kCells);
    At<int32_t>(self, manager::kCellsBefore + 4) = At<int32_t>(self, manager::kCells + 4);
    At<int32_t>(self, manager::kCellsBefore + 8) = At<int32_t>(self, manager::kCells + 8);
    At<int32_t>(self, manager::kCellsBefore + 0xC) = At<int32_t>(self, manager::kCells + 0xC);
    At<int32_t>(self, manager::kCells + 8) = right;
    At<int32_t>(self, manager::kCells) = left;
    At<int32_t>(self, manager::kCells + 4) = top;
    At<int32_t>(self, manager::kCells + 0xC) = bottom;
}

// The game's 0x004E9490: the sound service's callback hands a sound's
// number (slot 6 of the argument, then slot 9) to every layer's slot 17.
void __fastcall TellLayers(char* self, void*, void* sound)
{
    if (!sound)
        return;
    char* const sentinel = At<char*>(self, manager::kLayers);
    for (char* node = At<char*>(sentinel, 0x0); node != sentinel; node = At<char*>(node, 0x0)) {
        void* const inner = Virtual<void*>(sound, 0x18);
        void* const one = At<void*>(node, 0x8);
        const uint32_t number = Virtual<uint32_t>(inner, 0x24);
        Virtual<void>(one, 0x44, number);
    }
}

// Calls slot 20 of every layer with a cell.
void CellToLayers(char* self, int32_t x, int32_t z)
{
    char* const sentinel = At<char*>(self, manager::kLayers);
    for (char* node = At<char*>(sentinel, 0x0); node != sentinel; node = At<char*>(node, 0x0))
        Virtual<void>(At<void*>(node, 0x8), 0x50, x, z);
}

// The game's 0x004E94E0: the sweep. While the manager's stopwatch reads
// under 1000 since the start of the call, the next tract's occupants that
// pass the filter go to the layers (0x004E8E60) and the layers are told its
// four cells; then every layer's slot 26 gets the argument and the
// listener's place and facing. True when the last tract has been done.
uint8_t __fastcall Sweep(char* self, void*, uint32_t elapsed)
{
    char* const budget = self + manager::kBudget;
    This<void>(addr::GZStopwatch_Start, budget);
    uint8_t finished = 0;
    if (This<uint32_t>(addr::GZStopwatch_ElapsedMilliseconds, budget) < 1000) {
        for (;;) {
            int32_t tractX;
            int32_t tractZ;
            if (!This<uint8_t>(addr::TractIterator_Next, self + manager::kTracts, &tractX, &tractZ)) {
                finished = 1;
                break;
            }
            const int32_t x = tractX << 1;
            const int32_t z = tractZ << 1;
            int32_t last[2] = {x + 1, z + 1};
            int32_t first[2] = {x, z};
            Virtual<void>(At<void*>(self, manager::kOccupants), 0xBC, reinterpret_cast<void*>(&SweepCallback),
                          self, &first[0], &last[0], At<void*>(self, manager::kFilter));
            CellToLayers(self, x, z);
            CellToLayers(self, x + 1, z);
            CellToLayers(self, x, z + 1);
            CellToLayers(self, x + 1, z + 1);
            if (This<uint32_t>(addr::GZStopwatch_ElapsedMilliseconds, self + manager::kBudget) >= 1000)
                break;
        }
    }
    char* const sentinel = At<char*>(self, manager::kLayers);
    for (char* node = At<char*>(sentinel, 0x0); node != sentinel; node = At<char*>(node, 0x0))
        Virtual<void>(At<void*>(node, 0x8), 0x68, elapsed, self + manager::kPlace, self + manager::kFacing);
    return finished;
}

// The game's 0x004E9660: every layer is shut down and let go, and the list
// emptied.
uint8_t __fastcall RemoveLayers(char* self, void*)
{
    char* const sentinel = At<char*>(self, manager::kLayers);
    char* node = At<char*>(sentinel, 0x0);
    while (node != sentinel) {
        void* const one = At<void*>(node, 0x8);
        Virtual<void>(one, 0x14);
        Release(one);
        char* const next = At<char*>(node, 0x0);
        char* const before = At<char*>(node, 0x4);
        At<char*>(before, 0x0) = next;
        At<char*>(next, 0x4) = before;
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(node));
        node = next;
    }
    return 1;
}

// The game's 0x004E96A0 (slot 9): Shutdown.
uint8_t __fastcall ManagerShutdown(char* self, void*)
{
    void* framework = Global<void*>(addr::SC4Audio_Framework);
    if (!framework) {
        framework = Cdecl<void*>(addr::RZGetFramework);
        Global<void*>(addr::SC4Audio_Framework) = framework;
        if (!framework)
            return 0;
    }
    Virtual<void>(framework, 0x10, self);
    if (void* const sound = At<void*>(self, manager::kSound))
        Virtual<void>(sound, 0x40, self + manager::kCallback);

    void* messages = nullptr;
    GetService(kFrameworkMessages, kMessagesIid, &messages);
    if (messages) {
        for (const uint32_t id : kManagerMessagesLeaving)
            Virtual<void>(messages, 0x18, self + manager::kTarget, id);
    }
    RemoveLayers(self, nullptr);
    if (void* const service = At<void*>(self, manager::kService1C)) {
        At<void*>(self, manager::kService1C) = nullptr;
        Release(service);
    }
    if (void* const filter = At<void*>(self, manager::kClassFilter)) {
        At<void*>(self, manager::kClassFilter) = nullptr;
        Release(filter);
    }
    This<void>(addr::SC4RefPtr_Destruct, &messages);
    return 1;
}

bool InstallAudio3(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[44] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4Audio_F4E6C10, reinterpret_cast<void*>(&RemoveAllFilters)},
        {addr::SC4Audio_F4E6D20, reinterpret_cast<void*>(&LoadFreshnessScores)},
        {addr::SC4Audio_F4E6E00, reinterpret_cast<void*>(&LoadFreshnessScore)},
        {addr::SC4Audio_F4E6FD0, reinterpret_cast<void*>(&StaticShutdown)},
        {addr::SC4Audio_F4E7010, reinterpret_cast<void*>(&AddFilter)},
        {addr::SC4Audio_F4E7290, reinterpret_cast<void*>(&LayerConstruct)},
        {addr::SC4Audio_F4E7650, reinterpret_cast<void*>(&LayerName)},
        {addr::SC4Audio_F4E7660, reinterpret_cast<void*>(&LayerSetName)},
        {addr::SC4Audio_F4E7690, reinterpret_cast<void*>(&LayerSetField1FC)},
        {addr::SC4Audio_F4E76B0, reinterpret_cast<void*>(&LayerGetVector)},
        {addr::SC4Audio_F4E76E0, reinterpret_cast<void*>(&LayerDestruct)},
        {addr::SC4Audio_F4E7860, reinterpret_cast<void*>(&LayerShutdown)},
        {addr::SC4Audio_F4E78E0, reinterpret_cast<void*>(&AddFilters)},
        {addr::SC4Audio_F4E7A60, reinterpret_cast<void*>(&LayerRelease)},
        {addr::SC4Audio_F4E7A90, reinterpret_cast<void*>(&ReadTuningValues)},
        {addr::SC4Audio_F4E8180, reinterpret_cast<void*>(&LayerInit)},
        {addr::SC4Audio_F4E8230, reinterpret_cast<void*>(&LayerDoMessage)},
        {addr::SC4Audio_F4E82B0, reinterpret_cast<void*>(&ManagerRelease)},
        {addr::SC4Audio_F4E82D0, reinterpret_cast<void*>(&ManagerQueryInterface)},
        {addr::SC4Audio_F4E8300, reinterpret_cast<void*>(&FilterIsTypeIncluded)},
        {addr::SC4Audio_F4E8350, reinterpret_cast<void*>(&FilterQueryInterface)},
        {addr::SC4Audio_F4E8440, reinterpret_cast<void*>(&HolderDestruct)},
        {addr::SC4Audio_F4E8490, reinterpret_cast<void*>(&Listener)},
        {addr::SC4Audio_F4E84D0, reinterpret_cast<void*>(&ManagerInit)},
        {addr::SC4Audio_F4E8720, reinterpret_cast<void*>(&OccupantSound)},
        {addr::SC4Audio_F4E8820, reinterpret_cast<void*>(&OnOccupantMessage)},
        {addr::SC4Audio_F4E8990, reinterpret_cast<void*>(&OnAutomatonMessage)},
        {addr::SC4Audio_F4E8AD0, reinterpret_cast<void*>(&StartSound)},
        {addr::SC4Audio_F4E8BF0, reinterpret_cast<void*>(&StopSound)},
        {addr::SC4Audio_F4E8C70, reinterpret_cast<void*>(&PlaceOccupantSound)},
        {addr::SC4Audio_F4E8CD0, reinterpret_cast<void*>(&FilterRelease)},
        {addr::SC4Audio_F4E8CF0, reinterpret_cast<void*>(&OnStateMessage)},
        {addr::SC4Audio_F4E8D40, reinterpret_cast<void*>(&OnOccupantMoved)},
        {addr::SC4Audio_F4E8E10, reinterpret_cast<void*>(&SweepOccupant)},
        {addr::SC4Audio_F4E8E60, reinterpret_cast<void*>(&SweepCallback)},
        {addr::SC4Audio_F4E8EA0, reinterpret_cast<void*>(&TimedSounds)},
        {addr::SC4Audio_F4E91D0, reinterpret_cast<void*>(&ManagerReadTuning)},
        {addr::SC4Audio_F4E92A0, reinterpret_cast<void*>(&UpdateView)},
        {addr::SC4Audio_F4E9490, reinterpret_cast<void*>(&TellLayers)},
        {addr::SC4Audio_F4E94E0, reinterpret_cast<void*>(&Sweep)},
        {addr::SC4Audio_F4E9660, reinterpret_cast<void*>(&RemoveLayers)},
        {addr::SC4Audio_F4E96A0, reinterpret_cast<void*>(&ManagerShutdown)},
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

const bool g_audio3Registered = registry::Register({
    "audio.part3",
    "audio",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Audio_F4E7A90,
    "Our ambience layer's tuning, filters and lifetime and the ambience manager's first half (docs/audio.md)",
    false,
    InstallAudio3,
});

} // namespace
} // namespace opensc4::audio
