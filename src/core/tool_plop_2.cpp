// The plop tool, middle part: cSC4ViewInputControlPlaceLot (vtable
// 0x00A90738, message target 0x00A90728 at +0x28), the tool the player places
// a building or a lot from a menu with. Replacement, off by default:
//
//   ui.tools.plop2: Init (slot 17, 0x004C13E0), SetConfiguration (0x004C1360),
//     TestPlacement (0x004C1110: whether the footprint at a position is
//     allowed, what it costs and why not) and PlaceLot (0x004C1740: the
//     commit).
//
// Called by address: BuildPreview (0x004C0680; ours in ui.tools.plop4,
// tool_plop_4.cpp, which hooks its entry). The helpers of the neighbouring
// parts (0x004BE750 and the like) are called by address too.
//
// Fields of the object (from its start; see tool_plop_3.cpp for the rest):
//   +0x04 byte: the base class's Init has run   +0x0C, +0x10 tool IDs
//   +0x1C the city view         +0x28 the message target part
//   +0x2C a flag (set: a building chosen by key rather than by lot configuration)
//   +0x30 the lot configuration ID   +0x34 a second ID (the lot configuration
//       filter's key)   +0x3C the rotation in use
//   +0x4C the lot configuration manager (0x00B43D18, referenced)
//   +0x50 byte given to the configuration filter
//   +0x54 a hash set of IDs (0x14 bytes), its count at +0x64
//   +0x68 the building development simulator (0x00B43D68, referenced)
//   +0x6C a vector (begin, end) of IDs
//   +0x78, +0x7C, +0x80, +0x84 the last footprint's bounds (x0, z0, x1, z1)
//   +0xAC an overlay holder (base class)   +0xB0, +0xC4 two game strings
//       (embedded; slot 5 gives the text): the effect names
//   +0xD8 the effect made from +0xB0   +0xE4 the chosen exemplar (referenced)
//   +0x118 byte   +0x120 float (a scale)   +0x128 int64 (a base cost)
//   +0x138 int64 the cached cost (-100000 for none)   +0x140 a cursor ID
//   +0x150 byte
//
// The cost: TestPlacement works the cost out once per rectangle change; the
// answer is kept at +0x138 and PlaceLot recomputes it only while it is
// -100000.
//
// PlaceLot and who calls it: the tool's own OnMouseDownL (0x004C3120, call at
// 0x004C318B, smoke flag 1, returns to 0x004C3190) and a vtable function at
// 0x005C5380 (call at 0x005C5554), which makes its own tool object on the
// stack with the constructor 0x004C0430 (the one shared by the city view and
// the region code), points it at a lot configuration and places a lot at a
// position with a smoke flag of its own: the same function, so one
// translation serves both. game.quiet.plop's detour on the entry clears the
// smoke flag (the third argument) when the call returns to 0x004C3190.
//
// Random draws: none. Clocks: none.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/tool_plop.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::tool_plop {

// The entry as the game's callers see it. Defined below; external so that
// PlaceLotEntry can hand its address to a wrapper.
uint8_t __fastcall PlaceLotOurs(char* self, void* edx, const float* position, uint32_t rotation, uint32_t smoke,
                                int32_t* rect);

namespace {

bool g_placeLotOurs = false;

namespace field {
constexpr std::size_t kInitialised = 0x04;
constexpr std::size_t kIdA = 0x0C;
constexpr std::size_t kIdB = 0x10;
constexpr std::size_t kView = 0x1C;
constexpr std::size_t kMessageTarget = 0x28;
constexpr std::size_t kByKey = 0x2C;
constexpr std::size_t kConfigId = 0x30;
constexpr std::size_t kKeyId = 0x34;
constexpr std::size_t kRotation = 0x3C;
constexpr std::size_t kConfigManager = 0x4C;
constexpr std::size_t kFilterFlag = 0x50;
constexpr std::size_t kIdSet = 0x54;
constexpr std::size_t kIdSetCount = 0x64;
constexpr std::size_t kDevelopment = 0x68;
constexpr std::size_t kIdVector = 0x6C;
constexpr std::size_t kBounds = 0x78;
constexpr std::size_t kHolder = 0xAC;
constexpr std::size_t kEffectName = 0xB0;
constexpr std::size_t kSmokeName = 0xC4;
constexpr std::size_t kEffect = 0xD8;
constexpr std::size_t kExemplar = 0xE4;
constexpr std::size_t kMarker = 0x118;
constexpr std::size_t kScale = 0x120;
constexpr std::size_t kBaseCost = 0x128;
constexpr std::size_t kCostCache = 0x138;
constexpr std::size_t kCursor = 0x140;
constexpr std::size_t kSpecial = 0x150;
} // namespace field

constexpr int64_t kNoCost = -100000;
constexpr uint32_t kSpecialConfig = 0xEBF9A440;

// Strings of the effect names for the lot's smoke, by size (0x18-byte game
// strings in the exe's data).
constexpr std::uintptr_t kSmokeBySize[4] = {0x00A908C0, 0x00A908A8, 0x00A90890, 0x00A90878};

// Language texts for the answers of the lot manager (group 0x0A419226).
constexpr uint32_t kReasonGroup = 0x0A419226;
constexpr uint32_t kReasonOther = 0x6A711969;
constexpr uint32_t kReasons[8] = {0xCA711914, 0xAA71194E, 0xAA711938, 0x6A71193F,
                                  0xCABC118F, 0x6A711955, 0xAA71195C, 0x6A711944};

// The game's cRZBaseString (0x14 bytes) and its localized string (0x20 bytes,
// sized from the frame of 0x004C1110: the object stands at its +0x20 to +0x40).
struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t references;
};
struct LocalizedString {
    GameString string;
    uint32_t key[3];
};
static_assert(sizeof(GameString) == 0x14);
static_assert(sizeof(LocalizedString) == 0x20);

// A cell region (0x24 bytes): the rectangle, then a part at +0x10 whose
// vtable 0x00A86364 the destructor sets before freeing its cells.
struct CellRegion {
    int32_t rect[4];
    uint32_t cells[5];
};
static_assert(sizeof(CellRegion) == 0x24);

// What the effects manager is given to start an effect (0x38 bytes, from the
// frame of PlaceLot at 0x004C1EA0): a kind byte and a flag byte, a 3 by 3
// matrix, a position and a scale.
struct EffectState {
    uint8_t kind;
    uint8_t flag;
    uint8_t unused[2];
    float matrix[9];
    float position[3];
    float scale;
};
static_assert(sizeof(EffectState) == 0x38);

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

void AddRef(void* object)
{
    Virtual<uint32_t>(object, 0x4);
}

void Release(void* object)
{
    Virtual<uint32_t>(object, 0x8);
}

void Free(void* memory)
{
    Cdecl<void>(addr::SC4_Free, memory);
}

void ConstructRegion(CellRegion* region, const int32_t* rect)
{
    This<void*>(addr::SC4CellRegion_Construct, region, rect, uint32_t{1});
}

void DestroyRegion(CellRegion* region)
{
    region->cells[0] = static_cast<uint32_t>(addr::SC4CellRegion_CellsVtable);
    This<void>(addr::SC4CellRegion_DestroyCells, region->cells);
}

// The game's integer cast of a float (a truncation).
int32_t Truncate(float value)
{
    return static_cast<int32_t>(value);
}

// --- TestPlacement (0x004C1110) -------------------------------------------------------

// Whether the lot configuration fits at the position. `position` is three
// floats; the cell rectangle goes to `rect` (-1 each while unknown); `ok`
// gets the answer of the footprint function; `cost` the base cost plus the
// demolition cost of the rectangle (and the ground's cost when it fits);
// `code` the lot manager's verdict (8: fits); `reason` gets the text of the
// verdict when it does not fit. `ret 0x1C`. True when it fits.
//
// An answer inside the box at +0x78 (the last footprint) is false without a
// reason, with the cost left at zero: kept as the original does it.
uint8_t __fastcall TestPlacement(char* self, void*, const float* position, uint32_t rotation, int32_t* rect,
                                 uint8_t* ok, int64_t* cost, int32_t* code, GameString* reason)
{
    for (int i = 0; i < 4; ++i)
        rect[i] = -1;
    *ok = 0;
    *cost = 0;
    *code = 0;
    if (reason->begin != reason->end) {
        *reason->begin = 0;
        reason->end = reason->begin;
    }
    uint8_t fits = 0;

    void* const manager = At<void*>(self, field::kConfigManager);
    void* const config = Virtual<void*>(manager, 0x20, At<uint32_t>(self, field::kConfigId));
    if (!config)
        return fits;

    float x = position[0];
    float z = position[2];
    const uint8_t snapped = This<uint8_t>(addr::cSC4ViewInputControlPlaceLot_SnapFootprint, self, rotation, x, z, &x, &z,
                                          rect);
    *ok = snapped;
    if (!snapped)
        return fits;

    const int32_t cellZ = Truncate(z);
    const int32_t cellX = Truncate(x);
    const int32_t* const last = reinterpret_cast<const int32_t*>(self + field::kBounds);
    if (cellX >= last[0] && cellZ >= last[1] && cellX <= last[2] && cellZ <= last[3])
        return fits;

    int64_t base = 0;
    if (At<int64_t>(self, field::kBaseCost) != 0) {
        void* const development = Global<void*>(addr::SC4_Service43D68);
        void* const demolition = Virtual<void*>(development, 0x90);
        base = static_cast<int64_t>(static_cast<uint64_t>(Virtual<int64_t>(demolition, 0x80, rect)) +
                                    static_cast<uint64_t>(At<int64_t>(self, field::kBaseCost)));
    }
    *cost = base;

    void* const lots = Global<void*>(addr::SC4_LotManager);
    const uint32_t verdict = Virtual<uint32_t>(lots, 0x54, rect[0], rect[1], rect[2], rect[3], rotation, config);
    *code = static_cast<int32_t>(verdict);

    uint32_t text;
    if (verdict == 8) {
        int64_t extra = 0;
        if (This<uint8_t>(addr::cSC4ViewInputControlPlaceLot_PriceDemolition, self, config, position, rotation, rect,
                          &extra)) {
            *cost = static_cast<int64_t>(static_cast<uint64_t>(*cost) + static_cast<uint64_t>(extra));
            return 1;
        }
        text = kReasonOther;
    } else if (verdict <= 7) {
        text = kReasons[verdict];
    } else {
        return fits;
    }
    LocalizedString made{};
    void* const got = This<void*>(addr::SC4LocalizedString_SC4LocalizedString, &made, kReasonGroup, text, uint32_t{1});
    This<void>(addr::cRZBaseString_Assign, reason, got);
    This<void>(addr::SC4String_Destroy, &made);
    return fits;
}

// --- SetConfiguration (0x004C1360) ----------------------------------------------------

// `ret 4`: a new lot configuration ID; the preview of the old one is put
// away and the new one's made.
void __fastcall SetConfiguration(char* self, void*, uint32_t id)
{
    if (At<uint32_t>(self, field::kConfigId) != id) {
        This<void>(addr::cSC4ViewInputControlPlaceLot_ClearPreview, self);
        At<uint32_t>(self, field::kConfigId) = id;
        This<void>(addr::cSC4ViewInputControlPlaceLot_BuildPreview, self);
    }
}

// --- Init (0x004C13E0) ----------------------------------------------------------------

// Slot 17, no arguments; a bool. Registers the message target for six
// messages, takes the lot configuration manager and the building development
// simulator, works out the lot configuration (and the second ID) to place
// from the menu's choice, and starts the cursor effect. False (after calling
// Shutdown, slot 4) when the choice leaves no configuration.
uint8_t __fastcall Init(char* self, void*)
{
    if (At<uint8_t>(self, field::kInitialised))
        return 1;
    At<uint32_t>(self, field::kIdA) = 0x88F154FB;
    At<uint32_t>(self, field::kIdB) = 0x88E70B76;
    This<void>(addr::SC4ViewInputControl_Init, self);

    void* const server = Global<void*>(addr::SC4_MessageServer);
    if (server) {
        void* const target = self + field::kMessageTarget;
        static const uint32_t messages[6] = {0x277A9364, 0x99EF1142, 0x99EF1143, 0x49D5B83F, 0xA6B79602, 0xC9F775BB};
        for (const uint32_t message : messages)
            Virtual<void>(server, 0x14, target, message);
    }

    if (!At<void*>(self, field::kConfigManager)) {
        void* const manager = Global<void*>(addr::SC4_Service43D18);
        At<void*>(self, field::kConfigManager) = manager;
        if (manager)
            AddRef(manager);
    }
    if (!At<void*>(self, field::kDevelopment)) {
        void* const development = Global<void*>(addr::SC4_Service43D68);
        At<void*>(self, field::kDevelopment) = development;
        AddRef(development);
    }

    char* const set = self + field::kIdSet;
    if (!At<void*>(self, field::kByKey)) {
        if (!At<uint32_t>(self, field::kConfigId)) {
            if (!At<uint32_t>(self, field::kIdSetCount)) {
                void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x1C});
                void* const filter = memory ? This<void*>(addr::cSC4PlaceLotConfigFilter_cSC4PlaceLotConfigFilter, memory,
                                                          uint32_t{At<uint8_t>(self, field::kFilterFlag)})
                                            : nullptr;
                AddRef(filter);
                Virtual<void>(At<void*>(self, field::kConfigManager), 0x34, set, filter);
                Release(filter);
            }
            if (At<uint32_t>(self, field::kIdSetCount)) {
                void* first[2] = {nullptr, nullptr};
                This<void>(addr::SC4HashTable_Begin, set, first);
                At<uint32_t>(self, field::kConfigId) = At<uint32_t>(first[0], 4);
            }
            At<uint32_t>(self, field::kConfigId) = This<uint32_t>(addr::cSC4ViewInputControlPlaceLot_ChooseLotConfiguration,
                                                                 self, At<uint32_t>(self, field::kConfigId));
        }
        if (At<uint32_t>(self, field::kConfigId) == kSpecialConfig)
            At<uint8_t>(self, field::kSpecial) = 1;
    } else {
        bool skip = false;
        if (!At<uint32_t>(self, field::kKeyId)) {
            void* view = At<void*>(self, field::kView);
            if (view)
                At<uint32_t>(self, field::kKeyId) = Virtual<uint32_t>(view, 0xCC);
            char* const vector = self + field::kIdVector;
            if (At<void*>(vector, 0) == At<void*>(vector, 4))
                Virtual<void>(At<void*>(self, field::kDevelopment), 0x2C, vector);
            if (At<void*>(vector, 0) != At<void*>(vector, 4) && !At<uint32_t>(self, field::kKeyId))
                At<uint32_t>(self, field::kKeyId) = *At<uint32_t*>(vector, 0);
            At<uint32_t>(self, field::kKeyId) = This<uint32_t>(addr::cSC4ViewInputControlPlaceLot_ChooseBuilding, self,
                                                              At<uint32_t>(self, field::kKeyId));
            view = At<void*>(self, field::kView);
            if (view)
                Virtual<void>(view, 0xD4, At<uint32_t>(self, field::kKeyId));
            skip = !At<uint32_t>(self, field::kKeyId);
        }
        if (!skip) {
            This<void>(addr::SC4Uint32HashSet_Clear, set);
            void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x10});
            void* const filter = memory ? This<void*>(addr::cSC4PlaceLotTypeFilter_cSC4PlaceLotTypeFilter, memory,
                                                      At<uint32_t>(self, field::kKeyId))
                                        : nullptr;
            AddRef(filter);
            Virtual<void>(At<void*>(self, field::kConfigManager), 0x34, set, filter);
            Release(filter);
            if (!At<uint32_t>(self, field::kIdSetCount)) {
                Virtual<void>(self, 0x10);
                return 0;
            }
            void* first[2] = {nullptr, nullptr};
            This<void>(addr::SC4HashTable_Begin, set, first);
            At<uint32_t>(self, field::kConfigId) = At<uint32_t>(first[0], 4);
            // No test of the view here, as in the original.
            const uint32_t chosen = Virtual<uint32_t>(At<void*>(self, field::kView), 0xD0);
            At<uint32_t>(self, field::kConfigId) = chosen;
            const uint32_t id = This<uint32_t>(addr::cSC4ViewInputControlPlaceLot_ChooseLotConfiguration, self, chosen);
            void* const view = At<void*>(self, field::kView);
            At<uint32_t>(self, field::kConfigId) = id;
            if (view)
                Virtual<void>(view, 0xD8, id);
        }
    }

    // 0x004C162F: the base cost of the building (when a key is set, or a
    // building ID can be found for the configuration).
    uint32_t key = At<uint32_t>(self, field::kKeyId);
    if (!key) {
        const uint32_t id = At<uint32_t>(self, field::kConfigId);
        if (id) {
            void* const config = Virtual<void*>(At<void*>(self, field::kConfigManager), 0x20, id);
            if (config) {
                void* const object = Virtual<void*>(config, 0x60);
                if (object) {
                    // A vector of IDs filled by the manager (begin, end, capacity).
                    uint32_t* vector[3] = {nullptr, nullptr, nullptr};
                    Virtual<void>(At<void*>(self, field::kConfigManager), 0x6C, object, vector);
                    if ((reinterpret_cast<std::uintptr_t>(vector[1]) - reinterpret_cast<std::uintptr_t>(vector[0])) &
                        0xFFFFFFFCu)
                        key = *vector[0];
                    if (vector[0])
                        Free(vector[0]);
                }
            }
        }
    }
    if (key) {
        void* const building = Virtual<void*>(Global<void*>(addr::SC4_Service43D68), 0xA0, key);
        if (building) {
            At<uint32_t>(self, field::kBaseCost) = At<uint32_t>(building, 0x30);
            At<uint32_t>(self, field::kBaseCost + 4) = At<uint32_t>(building, 0x34);
        }
    }

    This<void>(addr::cSC4ViewInputControlPlaceLot_BuildPreview, self);
    This<void>(addr::SC4ViewModel_Make, self + field::kHolder);

    if (!At<void*>(self, field::kEffect)) {
        void* const effects = Global<void*>(addr::SC4_Service43D1C);
        void* const name = Virtual<void*>(self + field::kEffectName, 0x14);
        Virtual<void>(effects, 0x1C, name, static_cast<void*>(self + field::kEffect));
    }
    if (At<uint32_t>(self, field::kCursor)) {
        void* const service = Cdecl<void*>(addr::SC4_Service43DC4Slot6);
        Virtual<void>(service, 0x10, At<uint32_t>(self, field::kCursor));
    }
    return 1;
}

// --- PlaceLot (0x004C1740) --------------------------------------------------------------

// Making a flame-free occupant (0x250 bytes) at the lot's middle for a few
// lot kinds: the constructor is the game's (0x006483C0 or 0x00652C30), the
// effects belong to the occupant manager. The vector the occupant is placed
// at is {x, the high word of the cost, y}: the frame of the original
// (the slots below the cost) reused, kept as it was.
void MakeMarker(std::uintptr_t construct, const int32_t* rect, uint32_t (&place)[3], const float* position,
                void* building)
{
    const int32_t middleX = (rect[2] - rect[0] + 2) / 2 + rect[0] - 1;
    const int32_t middleZ = (rect[3] - rect[1] + 2) / 2 + rect[1] - 1;
    void* const city = Global<void*>(addr::SC4_City);
    float worldX = 0.0f, worldZ = 0.0f;
    Virtual<void>(city, 0x184, middleX, middleZ, &worldX, &worldZ);
    std::memcpy(&place[0], &worldX, sizeof(float));
    std::memcpy(&place[2], &position[1], sizeof(float));
    void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x250});
    void* const made = memory ? This<void*>(construct, memory, uint32_t{1}) : nullptr;
    void* holder = nullptr;
    This<void*>(addr::SC4RefHolder_SC4RefHolder, &holder, made);
    void* const occupant = holder;
    Virtual<void>(occupant, 0x24, static_cast<void*>(place));
    Virtual<void*>(occupant, 0xC);
    void* const buildingHolder = Virtual<void*>(building, 0xC);
    Virtual<void>(occupant, 0xA0, buildingHolder);
    Virtual<void>(Global<void*>(addr::SC4_NetworkService), 0x50, occupant, uint32_t{0});
    Release(occupant);
}

EffectState MakeState(const float* position)
{
    EffectState state{};
    state.kind = 1;
    state.flag = 1;
    state.matrix[0] = 1.0f;
    state.matrix[4] = 1.0f;
    state.matrix[8] = 1.0f;
    state.position[0] = position[0];
    state.position[1] = position[1];
    state.position[2] = position[2];
    state.scale = 1.0f;
    return state;
}

} // namespace

// `ret 0x10`; a bool. `position` is three floats (x, y, z); `rotation` 0 to
// 3 (kept at +0x3C when the tool is in its special mode); `smoke` starts the
// smoke effects after a placement (the flag game.quiet.plop clears); `rect`
// gets the lot's cell rectangle (-1 each until known). The answer is true
// when the placement could be tested and, if the cost was computed in this
// call and nothing was placed, the same true: only a placement or its
// failure sets it otherwise (kept).
uint8_t __fastcall PlaceLotOurs(char* self, void*, const float* position, uint32_t rotation, uint32_t smoke,
                                int32_t* rect)
{
    for (int i = 0; i < 4; ++i)
        rect[i] = -1;
    int64_t cost = At<int64_t>(self, field::kCostCache);
    uint8_t result = 0;

    if (cost == kNoCost) {
        // The reason text starts as an empty game string with an 8-byte
        // buffer (0x00413xxx allocator); only the buffer is given back.
        char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
        buffer[0] = 0;
        GameString reason{};
        reason.vtable = addr::SC4String_Vtable;
        reason.begin = buffer;
        reason.end = buffer;
        reason.capacity = buffer + 8;
        reason.references = 0;
        uint8_t fits = 0;
        int32_t code = 0;
        int64_t fresh = 0;
        result = This<uint8_t>(addr::cSC4ViewInputControlPlaceLot_TestPlacement, self, position, rotation, rect, &fits,
                               &fresh, &code, &reason);
        if (!result) {
            if (reason.begin)
                Free(reason.begin);
            return 0;
        }
        cost = fresh;
        At<int64_t>(self, field::kCostCache) = cost;
        if (reason.begin)
            Free(reason.begin);
    }

    void* const config =
        Virtual<void*>(At<void*>(self, field::kConfigManager), 0x20, At<uint32_t>(self, field::kConfigId));
    if (!config)
        return result;

    float spot[3] = {position[0], position[1], position[2]};
    uint8_t ok;
    if (At<uint8_t>(self, field::kSpecial)) {
        // The tool's special mode: the configuration is picked by the game
        // from the position, and the position is moved to the middle of the
        // rectangle it answers with.
        uint32_t chosen = 0;
        int32_t aux = 0;
        ok = This<uint8_t>(addr::cSC4ViewInputControlPlaceLot_FitNetworkPiece, self, &chosen, &aux, spot, rect);
        void* const city = Global<void*>(addr::SC4_City);
        float firstX, firstZ, secondX, secondZ;
        Virtual<void>(city, 0x184, rect[0], rect[1], &firstX, &firstZ);
        Virtual<void>(city, 0x184, rect[2], rect[3], &secondX, &secondZ);
        spot[0] = static_cast<float>((static_cast<double>(secondX) - firstX) * 0.5 + firstX);
        spot[2] = static_cast<float>((static_cast<double>(secondZ) - firstZ) * 0.5 + firstZ);
        if (chosen != At<uint32_t>(self, field::kConfigId)) {
            This<void>(addr::cSC4ViewInputControlPlaceLot_ClearPreview, self);
            At<uint32_t>(self, field::kConfigId) = chosen;
            This<void>(addr::cSC4ViewInputControlPlaceLot_BuildPreview, self);
        }
        At<uint32_t>(self, field::kRotation) = rotation;
    } else {
        ok = This<uint8_t>(addr::cSC4ViewInputControlPlaceLot_SnapFootprint, self, rotation, spot[0], spot[2], &spot[0],
                           &spot[2], rect);
    }

    // The tutorial may forbid the place.
    uint8_t allowed = 1;
    if (void* const city = Global<void*>(addr::SC4_City)) {
        void* const tutorial = Virtual<void*>(city, 0x148);
        if (tutorial && Virtual<uint8_t>(tutorial, 0x20)) {
            void* const part = Virtual<void*>(tutorial, 0x28);
            if (part) {
                const uint32_t area = Virtual<uint32_t>(part, 0x14);
                allowed = Cdecl<uint8_t>(addr::SC4_TutorialAllowsPlacement, area, rect);
            }
        }
    }
    if (!ok || !allowed)
        return result;

    void* const budget = Global<void*>(addr::SC4_BudgetSimulator);
    void* lotOut = nullptr;
    uint8_t isFree = 0;
    bool affordable = true;
    if (cost == 0) {
        isFree = 1;
    } else if (Virtual<int64_t>(budget, 0x18) < cost) {
        affordable = false;
    }

    if (affordable) {
        void* const lots = Global<void*>(addr::SC4_LotManager);
        void* const zones = Global<void*>(addr::SC4_ZoneManager);
        void* const demolition = Global<void*>(addr::SC4_Service43D00);
        const uint8_t smokeConfig = Virtual<uint8_t>(config, 0x38);

        CellRegion first;
        ConstructRegion(&first, rect);
        void* filter = nullptr;
        if (void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0xC})) {
            filter = This<void*>(addr::cSC4PlaceLotOccupantFilter_cSC4PlaceLotOccupantFilter, memory);
            if (filter)
                AddRef(filter);
        }
        // DemolishRegion: demolish, region, privilege 1, flags 0x2000,
        // clear zoned 0, the filter, no cost, nothing, nothing, 0, 0.
        const uint8_t cleared = Virtual<uint8_t>(demolition, 0x18, uint32_t{1}, static_cast<void*>(&first), uint32_t{1},
                                                 uint32_t{0x2000}, uint32_t{0}, filter, static_cast<int64_t*>(nullptr),
                                                 static_cast<void*>(nullptr), static_cast<void*>(nullptr), uint32_t{0},
                                                 uint32_t{0});
        if (cleared) {
            // PlaceZone with no zone, funds skipped when the placement is
            // free. The skip flag is a byte in the callee; the game pushes
            // the whole stack word (garbage above the byte), we the byte.
            const uint8_t emptied = Virtual<uint8_t>(zones, 0x2C, static_cast<void*>(&first), uint32_t{0}, uint32_t{1},
                                                     uint32_t{isFree}, uint32_t{1}, uint32_t{1}, uint32_t{0},
                                                     static_cast<int64_t*>(nullptr), static_cast<int32_t*>(nullptr),
                                                     static_cast<void*>(nullptr));
            if (emptied) {
                uint32_t zone = 0xF;
                void* const exemplar = At<void*>(self, field::kExemplar);
                if (exemplar) {
                    if (Virtual<uint8_t>(Virtual<void*>(exemplar, 0xC), 0x58, uint32_t{0x1508}))
                        zone = 0xB;
                    if (Virtual<uint8_t>(Virtual<void*>(exemplar, 0xC), 0x58, uint32_t{0x1509}) || smokeConfig)
                        zone = 0xC;
                }
                CellRegion second;
                ConstructRegion(&second, rect);
                const uint8_t zoned =
                    Virtual<uint8_t>(zones, 0x2C, static_cast<void*>(&second), zone, uint32_t{1}, uint32_t{1},
                                     uint32_t{1}, uint32_t{1}, uint32_t{0}, static_cast<int64_t*>(nullptr),
                                     static_cast<int32_t*>(nullptr), static_cast<void*>(nullptr));
                if (zoned) {
                    // Slot 22 of the lot manager (nine arguments): the cell
                    // rectangle as origin and size, the rotation, the lot out,
                    // 1, the config's flag (a byte; the game pushes a stack
                    // word), the configuration.
                    result = Virtual<uint8_t>(lots, 0x58, rect[0], rect[1], rect[2] - rect[0] + 1, rect[3] - rect[1] + 1,
                                              rotation, &lotOut, uint32_t{1}, uint32_t{smokeConfig}, config);
                }
                DestroyRegion(&second);
            }
        }
        if (filter)
            Release(filter);
        DestroyRegion(&first);
    }

    if (!lotOut)
        return result;

    // The lot is made.
    void* const development = Virtual<void*>(Global<void*>(addr::SC4_Service43D68), 0x90);
    const uint8_t configFlag = Virtual<uint8_t>(config, 0x50);
    const uint8_t built = Virtual<uint8_t>(development, 0x1C, lotOut, config, At<uint32_t>(self, field::kKeyId),
                                           uint32_t{configFlag});
    result = built;
    // The slots of the frame the original reuses for a position: the cost's
    // low word (overwritten by the first coordinate), its high word, and the
    // third word (the second coordinate, replaced by the height).
    uint32_t place[3] = {static_cast<uint32_t>(cost), static_cast<uint32_t>(static_cast<uint64_t>(cost) >> 32), 0};
    if (built) {
        void* const building = Virtual<void*>(lotOut, 0xC0);
        void* const holder = Virtual<void*>(building, 0xC);
        void* const exemplar = Virtual<void*>(holder, 0x18);
        if (!isFree)
            Virtual<void>(budget, 0x20, cost);
        This<void>(addr::cSC4ViewInputControlPlaceLot_GroundEffect, self, Virtual<void*>(building, 0xC));
        if (Virtual<void*>(exemplar, 0x24, uint32_t{0x88ED3303}))
            MakeMarker(addr::SC4PlopOccupantA_Construct, rect, place, spot, building);
        void* const network = Cdecl<void*>(addr::SC4_GetNetworkManager);
        if (Virtual<uint8_t>(network, 0x24, Virtual<void*>(building, 0xC)))
            MakeMarker(addr::SC4PlopOccupantB_Construct, rect, place, spot, building);
    } else {
        Virtual<void>(budget, 0x1C, cost);
    }

    if (smoke) {
        const int32_t depth = rect[3] - rect[1];
        const int32_t width = rect[2] - rect[0];
        const int32_t size = (width < depth ? depth : width) + 1;
        void* const effects = Global<void*>(addr::SC4_Service43D1C);
        const std::uintptr_t name = size <= 1 ? kSmokeBySize[0] : size <= 2 ? kSmokeBySize[1]
                                  : size <= 3 ? kSmokeBySize[2] : kSmokeBySize[3];
        void* first = nullptr;
        void* second = nullptr;
        if (Virtual<uint8_t>(effects, 0x1C, reinterpret_cast<void*>(name), static_cast<void*>(&first))) {
            EffectState state = MakeState(spot);
            Virtual<void>(first, 0x20, static_cast<void*>(&state));
            Virtual<void>(first, 0xC, uint32_t{0});
        }
        void* const smokeName = Virtual<void*>(self + field::kSmokeName, 0x14);
        if (Virtual<uint8_t>(effects, 0x1C, smokeName, static_cast<void*>(&second))) {
            EffectState state = MakeState(spot);
            const double scale = static_cast<double>(At<float>(self, field::kScale)) * 2.0;
            state.scale = static_cast<float>(scale);
            At<uint8_t>(self, field::kMarker) = 1;
            state.kind = 2;
            state.flag = scale == 1.0 ? 1 : 3;
            Virtual<void>(second, 0x20, static_cast<void*>(&state));
            Virtual<void>(second, 0x28, uint32_t{0x49D5B83F}, uint32_t{0});
            Virtual<void>(second, 0xC, uint32_t{0});
        }
        if (second)
            Release(second);
        if (first)
            Release(first);
    }
    Release(lotOut);
    return result;
}

std::uintptr_t PlaceLotEntry()
{
    return reinterpret_cast<std::uintptr_t>(&PlaceLotOurs);
}

bool PlaceLotIsOurs()
{
    return g_placeLotOurs;
}

namespace {

bool InstallPlop2(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[8] = {};
    const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::cSC4ViewInputControlPlaceLot_Init, reinterpret_cast<void*>(&Init)},
        {addr::cSC4ViewInputControlPlaceLot_SetConfiguration, reinterpret_cast<void*>(&SetConfiguration)},
        {addr::cSC4ViewInputControlPlaceLot_TestPlacement, reinterpret_cast<void*>(&TestPlacement)},
    };
    static_assert(sizeof(hooks) / sizeof(hooks[0]) <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < sizeof(hooks) / sizeof(hooks[0]); ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].address);
        if (!found || !hook::Install(*found, hooks[i].ours, &originals[i]))
            return false;
    }
    // game.quiet.plop puts an entry detour on PlaceLot. Two detours cannot
    // share an address: ours goes in first (PreAppInit), and the wrapper then
    // hooks ours (tool_plop.h). A foreign detour already there is skipped.
    static void* placeLotOriginal = nullptr;
    const addr::Entry* const placeLot = addr_check::Find(addr::cSC4ViewInputControlPlaceLot_PlaceLot);
    if (!placeLot)
        return false;
    if (hook::IsInstalledAt(placeLot->address))
        log::Write("tool_plop: PlaceLot is already hooked; ours not installed there");
    else
        g_placeLotOurs = hook::Install(*placeLot, reinterpret_cast<void*>(&PlaceLotOurs), &placeLotOriginal);
    return g_placeLotOurs || hook::IsInstalledAt(placeLot->address);
}

const bool g_registered = registry::Register({
    "ui.tools.plop2",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PreAppInit, // before game.quiet.plop wraps PlaceLot (tool_plop.h)
    addr::cSC4ViewInputControlPlaceLot_PlaceLot,
    "Our plop tool: Init, SetConfiguration, TestPlacement and PlaceLot (src/core/tool_plop_2.cpp)",
    false,
    InstallPlop2,
});

} // namespace
} // namespace opensc4::tool_plop
