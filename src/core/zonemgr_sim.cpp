// Our cSC4ZoneManager (build 641; the class map is in zonemgr_sim.h), all
// but PlaceZone (zonemgr_place.cpp): the queries and small setters, Init
// and Shutdown with the tuning exemplar, the message that dezones the cells
// under a new network occupant, the save record and our own object
// (constructor, destructor, identity).
//
// Every virtual call below has its argument count from the callee's own
// `ret N`: the city 0x00A87620, the lot manager 0x00AB37C0, the lot
// 0x00AB19B0, the building development simulator 0x00AB0AF8 and the lot
// developer 0x00AB1B98 its slot 36 gives, the message servers 0x00A8CAFC
// and 0x00AD1BF4, the resource manager 0x00AD89D0, the exemplar 0x00AAF0D0,
// the property holders (slot 9), the property 0x00AA6988 and the variants
// (0x00AA6788, 0x00AA69E8), the int8 grid 0x00A87950, the stream parts
// 0x00AA2310 and 0x00AA2280, the occupants (57 vtables, slots 0 and 11
// shared) and the network occupants (7 vtables, slot 9). The message's
// getters (cIGZMessage2Standard) are the common ones.
#include "core/zonemgr_sim.h"

#include "core/address_check.h"
#include "core/fixed_seed.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::zonemgr_sim {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("zonemgr: first %s", name);
}

bool SwapSlots(const Slot* slots, std::size_t count, const char* what)
{
    if (!game::IsBuild641())
        return false;
    for (std::size_t i = 0; i < count; ++i) {
        const Slot& slot = slots[i];
        const auto* const vtable = reinterpret_cast<const std::uintptr_t*>(slot.vtable);
        if (vtable[slot.index] != slot.original) {
            log::Write("zonemgr %s: slot %d of %08X holds %08X, expected %08X; not installing", what, slot.index,
                       static_cast<unsigned>(slot.vtable), static_cast<unsigned>(vtable[slot.index]),
                       static_cast<unsigned>(slot.original));
            return false;
        }
    }
    for (std::size_t i = 0; i < count; ++i) {
        const Slot& slot = slots[i];
        auto* const entry = reinterpret_cast<std::uintptr_t*>(slot.vtable) + slot.index;
        DWORD old = 0;
        if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &old))
            return false;
        *entry = reinterpret_cast<std::uintptr_t>(slot.ours);
        VirtualProtect(entry, sizeof(*entry), old, &old);
    }
    log::Write("zonemgr %s: %u slots are ours", what, static_cast<unsigned>(count));
    return true;
}

namespace {

using NewFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* block);
using RebalanceFn = void(__cdecl*)(void* node, char** root);
using RefFn = uint8_t(__cdecl*)(void* stream, void* object);
using ReadRefFn = uint8_t(__cdecl*)(void* stream, uint32_t iid, void* out, uint32_t zero);
using PropertyByteFn = uint8_t(__cdecl*)(void* holder, uint32_t property, uint8_t* value);
using RegionFn = void*(__thiscall*)(void* region, const int32_t* rect, uint32_t flag);
using SelfFn = void(__thiscall*)(void* self);
using RandomFn = void*(__thiscall*)(void* random, int32_t seed);

const auto OperatorNew = reinterpret_cast<NewFn>(addr::SC4_operator_new);
const auto OperatorDelete = reinterpret_cast<FreeFn>(addr::SC4_operator_delete);
const auto Allocate = reinterpret_cast<NewFn>(addr::SC4_stl_allocate);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);
const auto WriteRef = reinterpret_cast<RefFn>(addr::SC4Stream_WriteReference);
const auto ReadRef = reinterpret_cast<ReadRefFn>(addr::SC4Stream_ReadReference);

constexpr uint32_t kIidCity = 0x66BA4DB5;
constexpr uint32_t kIidOccupants = 0xC6BB8334;
constexpr uint32_t kIidTerrain = 0xC70BD37E;
constexpr uint32_t kIidPollution = 0x877181EC;
constexpr uint32_t kIidBudget = 0x6772BD23;
constexpr uint32_t kIidGrid = 0xC9BD939A;
constexpr uint32_t kIidNetworkOccupant = 0xA821EF94;
constexpr uint32_t kOccupantTypeNetwork = 0xC772BF98;
constexpr uint32_t kExemplarIid = 0xA52160F5;

char* Target(char* self) { return self + kMessagePart; }

Tree* Failures(char* self) { return reinterpret_cast<Tree*>(self + field::kFailures); }
Tree* Map370(char* self) { return reinterpret_cast<Tree*>(self + field::kMap370); }

// --- The failure maps ---------------------------------------------------------------

// 0x004DC380: the maps' order, by reason then type (signed bytes).
bool KeyLess(int8_t type, int8_t reason, int8_t otherType, int8_t otherReason)
{
    if (reason != otherReason)
        return reason < otherReason;
    return type < otherType;
}

// The in-order predecessor of a node that is not the first (0x006809E0
// without its head case, which the insert below never meets).
char* TreePrevious(char* at)
{
    if (char* left = At<char*>(at, node::kLeft)) {
        while (At<char*>(left, node::kRight))
            left = At<char*>(left, node::kRight);
        return left;
    }
    char* parent = At<char*>(at, node::kParent);
    while (at == At<char*>(parent, node::kLeft)) {
        at = parent;
        parent = At<char*>(parent, node::kParent);
    }
    return parent;
}

// The node of a key, made with `value` when the map has none: the game's
// insert_unique (0x004DCD90, used by Read) and its operator[] (0x004DE0C0:
// the lower bound 0x004DC760, then the hinted insert 0x004DD140) with the
// node insert 0x004DCB40. A new key has one place in the tree whatever the
// hint, so the two give the same tree: a 0x18-byte node from the STL
// allocator, linked below `parent`, then the game's rebalance.
char* FindOrInsert(Tree* tree, int8_t type, int8_t reason, uint32_t value)
{
    char* const head = tree->head;
    char* parent = head;
    bool left = true;
    for (char* at = At<char*>(head, node::kParent); at;) {
        parent = at;
        left = KeyLess(type, reason, At<int8_t>(at, node::kType), At<int8_t>(at, node::kReason));
        at = At<char*>(at, left ? node::kLeft : node::kRight);
    }
    char* lower = parent;
    bool isNew = false;
    if (left) {
        if (parent == At<char*>(head, node::kLeft))
            isNew = true;
        else
            lower = TreePrevious(parent);
    }
    if (!isNew && !KeyLess(At<int8_t>(lower, node::kType), At<int8_t>(lower, node::kReason), type, reason))
        return lower;
    // The bytes +0x12 and +0x13 of the node stay unset, as in the game.
    char* const made = static_cast<char*>(Allocate(node::kSize));
    At<int8_t>(made, node::kType) = type;
    At<int8_t>(made, node::kReason) = reason;
    At<uint32_t>(made, node::kValue) = value;
    if (left) {
        At<char*>(parent, node::kLeft) = made;
        if (parent == head) {
            At<char*>(head, node::kParent) = made;
            At<char*>(head, node::kRight) = made;
        } else if (parent == At<char*>(head, node::kLeft)) {
            At<char*>(head, node::kLeft) = made;
        }
    } else {
        At<char*>(parent, node::kRight) = made;
        if (parent == At<char*>(head, node::kRight))
            At<char*>(head, node::kRight) = made;
    }
    At<char*>(made, node::kParent) = parent;
    At<char*>(made, node::kLeft) = nullptr;
    At<char*>(made, node::kRight) = nullptr;
    reinterpret_cast<RebalanceFn>(addr::SC4Tree_RebalanceForInsert)(made, &At<char*>(head, node::kParent));
    ++tree->count;
    return made;
}

// 0x004DC6D0: the nodes below and at `at` freed, right subtree first.
void EraseNodes(char* at)
{
    while (at) {
        EraseNodes(At<char*>(at, node::kRight));
        char* const left = At<char*>(at, node::kLeft);
        Free(at);
        at = left;
    }
}

// clear() as the destructor and Read have it inline: nothing for an empty
// map.
void ClearTree(Tree* tree)
{
    if (!tree->count)
        return;
    char* const head = tree->head;
    EraseNodes(At<char*>(head, node::kParent));
    At<char*>(head, node::kLeft) = head;
    At<char*>(head, node::kParent) = nullptr;
    At<char*>(head, node::kRight) = head;
    tree->count = 0;
}

} // namespace

void FreeRefList(char* head)
{
    for (char* at = At<char*>(head, 0x0); at != head;) {
        char* const next = At<char*>(at, 0x0);
        if (void* const object = At<void*>(at, 0x8))
            ReleaseOf(object);
        Free(at);
        at = next;
    }
    At<char*>(head, 0x0) = head;
    At<char*>(head, 0x4) = head;
    Free(head);
}

namespace {

// --- The queries and small setters ------------------------------------------------

// Slot 6 (0x004DC2D0). No bounds test, as the game.
int32_t __fastcall GetZoneCount(char* self, void*, uint32_t type)
{
    static bool logged = false;
    FirstCall("GetZoneCount", logged);
    return At<int32_t>(self, field::kZoned + type * 4);
}

// Slot 7 (0x004DC2F0).
int32_t __fastcall GetUndevelopedTileCount(char* self, void*, uint32_t type)
{
    static bool logged = false;
    FirstCall("GetUndevelopedTileCount", logged);
    return At<int32_t>(self, field::kUndeveloped + type * 4);
}

// Slot 8 (0x004DC2E0).
int32_t __fastcall GetUndevelopedTileCountTotal(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetUndevelopedTileCount (total)", logged);
    return At<int32_t>(self, field::kUndevelopedTotal);
}

// Slot 9 (0x004DE130): signed tests; a key the map does not have is added
// with 0 (the game's operator[]), so asking changes the saved map.
uint32_t __fastcall GetDevelopmentFailureCount(char* self, void*, int32_t type, int32_t reason)
{
    static bool logged = false;
    FirstCall("GetDevelopmentFailureCount", logged);
    if (type >= static_cast<int32_t>(kTypes) || reason >= kReasons)
        return 0;
    char* const found = FindOrInsert(Failures(self), static_cast<int8_t>(type), static_cast<int8_t>(reason), 0);
    return At<uint32_t>(found, node::kValue);
}

// Slot 12 (0x004DC740): the cell's type sign extended. No bounds test.
int32_t __fastcall GetZoneType(char* self, void*, int32_t x, int32_t z)
{
    static bool logged = false;
    FirstCall("GetZoneType", logged);
    return GridCell(At<void*>(self, field::kGrid), x, z);
}

// Slot 13 (0x004DC300): the jump table's byte is 1 for 1 to 9.
uint8_t __fastcall IsRCIZone(char*, void*, uint32_t type)
{
    static bool logged = false;
    FirstCall("IsRCIZone", logged);
    return type >= 1 && type <= 9;
}

// Slot 14 (0x004DC340).
uint8_t __fastcall GetTextureForZone(char* self, void*, uint32_t type, uint32_t* key)
{
    static bool logged = false;
    FirstCall("GetTextureForZone", logged);
    if (type >= kTypes)
        return 0;
    key[0] = 0x7AB50E44;
    key[1] = 0x1ABE787D;
    key[2] = At<uint32_t>(self, field::kTextures + type * 4);
    return 1;
}

// Slot 15 (0x004DCD20).
uint32_t __fastcall GetZoneDragColor(char* self, void*, uint32_t type)
{
    static bool logged = false;
    FirstCall("GetZoneDragColor", logged);
    return At<uint32_t>(self, field::kColors + type * 4);
}

// Slot 16 (0x004DCD10).
uint8_t __fastcall GetZoneDisplayAlpha(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetZoneDisplayAlpha", logged);
    return At<uint8_t>(self, field::kAlpha);
}

// Slot 17 (0x004DC370).
uint8_t __fastcall GetDefaultZoneDisplayAlpha(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetDefaultZoneDisplayAlpha", logged);
    return At<uint8_t>(self, field::kDefaultAlpha);
}

// Slot 18 (0x004DCF60): nothing when the value is the same. Else every lot
// of the city (the lot manager's slot 7 over (0, 0, cells x, cells z); the
// city's cell counts asked z first) that is in state 1 is handed to the lot
// developer's slot 43 (SetEmptyLotBaseTexture), which draws its zone
// texture again.
void __fastcall SetZoneDisplayAlpha(char* self, void*, uint32_t value)
{
    static bool logged = false;
    FirstCall("SetZoneDisplayAlpha", logged);
    const uint8_t alpha = static_cast<uint8_t>(value);
    if (At<uint8_t>(self, field::kAlpha) == alpha)
        return;
    At<uint8_t>(self, field::kAlpha) = alpha;
    void* const developer = Virtual<void*>(Global<void*>(addr::SC4_Service43D68), 0x90);
    void* const lots = Global<void*>(addr::SC4_LotManager);
    char* head = static_cast<char*>(Allocate(0xC));
    At<char*>(head, 0x0) = head;
    At<char*>(head, 0x4) = head;
    void* const city = Global<void*>(addr::SC4_City);
    const uint32_t depth = Virtual<uint32_t>(city, 0x178);
    const uint32_t width = Virtual<uint32_t>(city, 0x174);
    Virtual<void>(lots, 0x1C, &head, 0u, 0u, width, depth, 0u);
    for (char* at = At<char*>(head, 0x0); at != head; at = At<char*>(at, 0x0)) {
        void* const lot = At<void*>(at, 0x8);
        if (Virtual<int32_t>(lot, 0x84) == 1)
            Virtual<void>(developer, 0xAC, lot);
    }
    FreeRefList(head);
}

// Slot 19 (0x004DCD30).
int32_t __fastcall GetMinZoneSize(char* self, void*, uint32_t type)
{
    static bool logged = false;
    FirstCall("GetMinZoneSize", logged);
    return At<int32_t>(self, field::kMinSize + type * 4);
}

// Slot 20 (0x004DCD40).
int32_t __fastcall GetMaxZoneSize(char* self, void*, uint32_t type)
{
    static bool logged = false;
    FirstCall("GetMaxZoneSize", logged);
    return At<int32_t>(self, field::kMaxSize + type * 4);
}

const Slot kQuerySlots[] = {
    {addr::cSC4ZoneManager_vftable, 6, addr::cSC4ZoneManager_GetZoneCount, reinterpret_cast<void*>(&GetZoneCount)},
    {addr::cSC4ZoneManager_vftable, 7, addr::cSC4ZoneManager_GetUndevelopedTileCount,
     reinterpret_cast<void*>(&GetUndevelopedTileCount)},
    {addr::cSC4ZoneManager_vftable, 8, addr::cSC4ZoneManager_GetUndevelopedTileCountTotal,
     reinterpret_cast<void*>(&GetUndevelopedTileCountTotal)},
    {addr::cSC4ZoneManager_vftable, 9, addr::cSC4ZoneManager_GetDevelopmentFailureCount,
     reinterpret_cast<void*>(&GetDevelopmentFailureCount)},
    {addr::cSC4ZoneManager_vftable, 12, addr::cSC4ZoneManager_GetZoneType, reinterpret_cast<void*>(&GetZoneType)},
    {addr::cSC4ZoneManager_vftable, 13, addr::cSC4ZoneManager_IsRCIZone, reinterpret_cast<void*>(&IsRCIZone)},
    {addr::cSC4ZoneManager_vftable, 14, addr::cSC4ZoneManager_GetTextureForZone,
     reinterpret_cast<void*>(&GetTextureForZone)},
    {addr::cSC4ZoneManager_vftable, 15, addr::cSC4ZoneManager_GetZoneDragColor,
     reinterpret_cast<void*>(&GetZoneDragColor)},
    {addr::cSC4ZoneManager_vftable, 16, addr::cSC4ZoneManager_GetZoneDisplayAlpha,
     reinterpret_cast<void*>(&GetZoneDisplayAlpha)},
    {addr::cSC4ZoneManager_vftable, 17, addr::cSC4ZoneManager_GetDefaultZoneDisplayAlpha,
     reinterpret_cast<void*>(&GetDefaultZoneDisplayAlpha)},
    {addr::cSC4ZoneManager_vftable, 18, addr::cSC4ZoneManager_SetZoneDisplayAlpha,
     reinterpret_cast<void*>(&SetZoneDisplayAlpha)},
    {addr::cSC4ZoneManager_vftable, 19, addr::cSC4ZoneManager_GetMinZoneSize,
     reinterpret_cast<void*>(&GetMinZoneSize)},
    {addr::cSC4ZoneManager_vftable, 20, addr::cSC4ZoneManager_GetMaxZoneSize,
     reinterpret_cast<void*>(&GetMaxZoneSize)},
};

bool InstallSlots(registry::Mode)
{
    return SwapSlots(kQuerySlots, sizeof kQuerySlots / sizeof kQuerySlots[0], "slots");
}

// --- Init and Shutdown ------------------------------------------------------------

// A property's values (the holder's slot 9, the property's slot 5, then
// the variant's getter at `getter`), or null when the exemplar has none.
void* PropertyValues(void* holder, uint32_t property, std::size_t getter)
{
    void* const found = Virtual<void*>(holder, 0x24, property);
    if (!found)
        return nullptr;
    void* const variant = Virtual<void*>(found, 0x14);
    return Virtual<void*>(variant, getter);
}

// 0x004DC460 (Init only): the costs zeroed, then the tuning exemplar
// (type 0x6534284A, group 0xE7E2C2DB, instance 0xE9482490); a property it
// lacks leaves its field as it was. The game does not test the arrays'
// lengths: sixteen values each.
void LoadTuning(char* self)
{
    static bool logged = false;
    FirstCall("LoadTuning", logged);
    for (uint32_t i = 0; i < kTypes; ++i) {
        At<int64_t>(self, field::kZoneCost + i * 8) = 0;
        At<int64_t>(self, field::kDezoneCost + i * 8) = 0;
    }
    uint32_t key[3] = {0x6534284A, 0xE7E2C2DB, 0xE9482490};
    void* exemplar = nullptr;
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, static_cast<uint32_t*>(key), kExemplarIid,
                          &exemplar, 0u, 0u))
        return;
    void* const holder = Virtual<void*>(exemplar, 0x1C);
    const struct {
        uint32_t property;
        std::size_t getter;
        std::size_t field;
        std::size_t bytes;
    } arrays[] = {
        {0xE94825B2, 0x1A4, field::kMinSize, 4},    {0xE94825B3, 0x1A4, field::kMaxSize, 4},
        {0xE94825B9, 0x1BC, field::kMaxSlope, 4},   {0xE94825B4, 0x1B4, field::kZoneCost, 8},
        {0xE94825B5, 0x1B4, field::kDezoneCost, 8}, {0xE94825B6, 0x19C, field::kTextures, 4},
    };
    for (const auto& array : arrays)
        if (const void* const values = PropertyValues(holder, array.property, array.getter))
            std::memcpy(self + array.field, values, array.bytes * kTypes);
    reinterpret_cast<PropertyByteFn>(addr::SC4_GetPropertyUint8)(holder, 0xE94825B7u,
                                                                 &At<uint8_t>(self, field::kAlpha));
    At<uint8_t>(self, field::kDefaultAlpha) = At<uint8_t>(self, field::kAlpha);
    if (const void* const values = PropertyValues(holder, 0xE94825B8, 0x19C))
        std::memcpy(self + field::kColors, values, 4 * kTypes);
    ReleaseOf(exemplar);
}

// The zone grid as Init constructs it (inline in the game:
// SC4::operator_new(0x44) and the fields of cSC4SimGridSint8).
void* NewGrid()
{
    auto* const grid = static_cast<uint32_t*>(OperatorNew(0x44));
    if (grid) {
        grid[1] = static_cast<uint32_t>(addr::SC4SimGrid_VtableBase);
        grid[0] = static_cast<uint32_t>(addr::SC4SimGridSint8_Vtable);
        grid[1] = static_cast<uint32_t>(addr::SC4SimGridSint8_vftable_4);
        reinterpret_cast<uint8_t*>(grid)[8] = 0;
        grid[3] = 0;
        grid[4] = 0x49B9E603;
        grid[6] = 0;
        grid[14] = 0;
        grid[15] = 0;
        grid[16] = 0;
    }
    return grid;
}

// Slot 3 (0x004DE170). Always true. A service the save gave (Read) is
// kept; the others come from the globals, referenced without a null test.
// A city not read from a save starts with every cell counted as type 0 and
// the two other count arrays at the city's cell count.
uint8_t __fastcall Init(char* self, void*)
{
    static bool logged = false;
    FirstCall("Init", logged);
    if (At<uint8_t>(self, field::kInitialized))
        return 1;
    uint8_t& flag = Global<uint8_t>(addr::cSC4ZoneManager_InitFlag);
    if (!flag)
        flag = 1;
    LoadTuning(self);
    const struct {
        std::size_t offset;
        std::uintptr_t global;
    } services[] = {{field::kCity, addr::SC4_City},
                    {field::kOccupants, addr::SC4_NetworkService},
                    {field::kTerrain, addr::SC4_Terrain},
                    {field::kPollution, addr::SC4_PollutionSimulator},
                    {field::kBudget, addr::SC4_BudgetSimulator}};
    for (const auto& service : services) {
        if (At<void*>(self, service.offset))
            continue;
        void* const object = Global<void*>(service.global);
        At<void*>(self, service.offset) = object;
        AddRefOf(object);
    }
    if (!At<void*>(self, field::kMessages)) {
        void* const messages = Global<void*>(addr::SC4_MessageServer);
        At<void*>(self, field::kMessages) = messages;
        AddRefOf(messages);
        Virtual<uint8_t>(At<void*>(self, field::kMessages), 0x14, Target(self), kMessageOccupant);
    }
    if (!At<uint8_t>(self, field::kLoaded)) {
        const int32_t width = Virtual<int32_t>(At<void*>(self, field::kCity), 0x174);
        const int32_t cells = Virtual<int32_t>(At<void*>(self, field::kCity), 0x178) * width;
        for (uint32_t i = 0; i < kTypes; ++i) {
            At<int32_t>(self, field::kZoned + i * 4) = 0;
            At<int32_t>(self, field::kUndeveloped + i * 4) = cells;
            At<int32_t>(self, field::kCountsCC + i * 4) = cells;
        }
        At<int32_t>(self, field::kUndeveloped) = cells;
        At<int32_t>(self, field::kZoned) = cells;
        At<int32_t>(self, field::kUndevelopedTotal) = cells;
    }
    if (!At<void*>(self, field::kGrid)) {
        void* const grid = NewGrid();
        At<void*>(self, field::kGrid) = grid;
        AddRefOf(grid);
        Virtual<uint32_t>(At<void*>(self, field::kGrid), 0xC);
        Virtual<uint32_t>(At<void*>(self, field::kGrid), 0x24, 1u);
        Virtual<uint32_t>(At<void*>(self, field::kGrid), 0x6C, 0u);
    }
    At<uint8_t>(self, field::kInitialized) = 1;
    return 1;
}

// Slot 4 (0x004DC230). Always true.
uint8_t __fastcall Shutdown(char* self, void*)
{
    static bool logged = false;
    FirstCall("Shutdown", logged);
    if (!At<uint8_t>(self, field::kInitialized))
        return 1;
    At<uint8_t>(self, field::kInitialized) = 0;
    if (void* const messages = At<void*>(self, field::kMessages)) {
        Virtual<uint8_t>(messages, 0x18, Target(self), kMessageOccupant);
        ReleaseOf(At<void*>(self, field::kMessages));
        At<void*>(self, field::kMessages) = nullptr;
    }
    for (const std::size_t offset :
         {field::kBudget, field::kPollution, field::kTerrain, field::kCity, field::kOccupants}) {
        if (void* const object = At<void*>(self, offset)) {
            ReleaseOf(object);
            At<void*>(self, offset) = nullptr;
        }
    }
    if (void* const grid = At<void*>(self, field::kGrid)) {
        Virtual<uint32_t>(grid, 0x10);
        ReleaseOf(At<void*>(self, field::kGrid));
        At<void*>(self, field::kGrid) = nullptr;
    }
    return 1;
}

const Slot kInitSlots[] = {
    {addr::cSC4ZoneManager_vftable, 3, addr::cSC4ZoneManager_Init, reinterpret_cast<void*>(&Init)},
    {addr::cSC4ZoneManager_vftable, 4, addr::cSC4ZoneManager_Shutdown, reinterpret_cast<void*>(&Shutdown)},
};

bool InstallInit(registry::Mode) { return SwapSlots(kInitSlots, 2, "init"); }

// --- The message ------------------------------------------------------------------

// cIGZMessageTarget2 slot 3 (0x004DCA70), on the part at +0x4. Always
// true. Message 0x99EF1142 about an occupant (void 1) of type 0xC772BF98
// (data 3) that is a network occupant without any of the flags 0x80200090
// (its slot 9): the cells of its bounding rectangle (the occupant's slot
// 11) are dezoned through our own slot 11: type 0, placed, no funds, not
// custom, no size test, whole lots only, no outputs. The game's
// QueryInterface output is the stack slot of its argument.
uint8_t __fastcall DoMessage(char* part, void*, void* message)
{
    static bool logged = false;
    FirstCall("DoMessage", logged);
    char* const self = part - kMessagePart;
    if (Virtual<uint32_t>(message, 0x10) != kMessageOccupant)
        return 1;
    void* const occupant = Virtual<void*>(message, 0x28);
    if (Virtual<uint32_t>(message, 0x4C) != kOccupantTypeNetwork)
        return 1;
    void* network = message;
    if (!Virtual<uint8_t>(occupant, 0x0, kIidNetworkOccupant, &network))
        return 1;
    const bool dezone = !Virtual<uint8_t>(network, 0x24, 0x80200090u);
    ReleaseOf(network);
    if (!dezone)
        return 1;
    int32_t rect[4] = {};
    Virtual<void>(occupant, 0x2C, static_cast<int32_t*>(rect));
    struct {
        int32_t rect[4];
        uint32_t cells[5];
    } region;
    reinterpret_cast<RegionFn>(addr::SC4CellRegion_Construct)(&region, rect, 1u);
    Virtual<uint8_t>(self, slot::kPlaceZone, static_cast<void*>(&region), 0u, 1u, 1u, 0u, 1u, 1u,
                     static_cast<int64_t*>(nullptr), static_cast<int32_t*>(nullptr), static_cast<void*>(nullptr));
    region.cells[0] = static_cast<uint32_t>(addr::SC4CellRegion_CellsVtable);
    reinterpret_cast<SelfFn>(addr::SC4CellRegion_DestroyCells)(region.cells);
    return 1;
}

const Slot kMessageSwaps[] = {
    {addr::cSC4ZoneManager_vftable_4, 3, addr::cSC4ZoneManager_DoMessage, reinterpret_cast<void*>(&DoMessage)},
};

bool InstallMessages(registry::Mode) { return SwapSlots(kMessageSwaps, 1, "messages"); }

// --- Save -------------------------------------------------------------------------

// One of the two maps: the count, then (type, reason, value) in order.
void WriteMap(void* stream, const Tree* tree)
{
    Virtual<uint8_t>(stream, 0x24, tree->count);
    char* const head = tree->head;
    for (char* at = At<char*>(head, node::kLeft); at != head; at = TreeNext(at)) {
        const uint32_t value = At<uint32_t>(at, node::kValue);
        Virtual<uint8_t>(stream, 0x10, static_cast<uint32_t>(At<uint8_t>(at, node::kType)));
        Virtual<uint8_t>(stream, 0x10, static_cast<uint32_t>(At<uint8_t>(at, node::kReason)));
        Virtual<uint8_t>(stream, 0x24, value);
    }
}

// cIGZSerializable slot 3 (0x004DC7A0), on the part at +0x8: version 1
// (tools/re/save_schemas/zonemgr.py). False when the stream is in error
// after.
uint8_t __fastcall Write(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    char* const self = part - kSerializablePart;
    Virtual<uint8_t>(stream, 0x1C, 1u);
    WriteRef(stream, At<void*>(self, field::kGrid));
    for (const std::size_t offset : {field::kZoned, field::kUndeveloped, field::kCountsCC})
        for (uint32_t i = 0; i < kTypes; ++i)
            Virtual<uint8_t>(stream, 0x20, At<uint32_t>(self, offset + i * 4));
    for (const std::size_t offset :
         {field::kUndevelopedTotal, field::kCount110, field::kAbandoned, field::kCount118})
        Virtual<uint8_t>(stream, 0x20, At<uint32_t>(self, offset));
    for (const std::size_t offset : {field::kMinSize, field::kMaxSize})
        for (uint32_t i = 0; i < kTypes; ++i)
            Virtual<uint8_t>(stream, 0x20, At<uint32_t>(self, offset + i * 4));
    for (const std::size_t offset : {field::kZoneCost, field::kDezoneCost})
        for (uint32_t i = 0; i < kTypes; ++i)
            Virtual<uint8_t>(stream, 0x28, At<uint32_t>(self, offset + i * 8), At<uint32_t>(self, offset + i * 8 + 4));
    for (const std::size_t offset : {field::kTextures, field::kColors})
        for (uint32_t i = 0; i < kTypes; ++i)
            Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, offset + i * 4));
    Virtual<uint8_t>(stream, 0x14, static_cast<uint32_t>(At<uint8_t>(self, field::kAlpha)));
    WriteMap(stream, Failures(self));
    WriteMap(stream, Map370(self));
    for (const std::size_t offset :
         {field::kCity, field::kOccupants, field::kTerrain, field::kPollution, field::kBudget})
        WriteRef(stream, At<void*>(self, offset));
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// What the game keeps in one set of locals for both maps: the entry last
// read. A read that fails leaves it as it was, so the second map's loop
// starts from the first map's last entry.
struct MapEntry {
    int8_t type = 0, reason = 0;
    uint32_t value = 0;
};

// One of the two maps read: the count, the map emptied, then the entries
// through insert_unique. The game's count local is unset before the read:
// zero here.
void ReadMap(void* stream, Tree* tree, MapEntry& entry)
{
    uint32_t count = 0;
    Virtual<uint8_t>(stream, 0x24, &count);
    ClearTree(tree);
    for (uint32_t i = 0; i < count; ++i) {
        Virtual<uint8_t>(stream, 0x10, &entry.type);
        Virtual<uint8_t>(stream, 0x10, &entry.reason);
        Virtual<uint8_t>(stream, 0x24, &entry.value);
        FindOrInsert(tree, entry.type, entry.reason, entry.value);
    }
}

// cIGZSerializable slot 4 (0x004DD400): the loaded flag set first, then
// version 1 only (false otherwise). The game's version local is unset
// before the read: zero here.
uint8_t __fastcall Read(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Read", logged);
    char* const self = part - kSerializablePart;
    At<uint8_t>(self, field::kLoaded) = 1;
    uint16_t version = 0;
    Virtual<uint8_t>(stream, 0x1C, &version);
    if (version != 1)
        return 0;
    ReadRef(stream, kIidGrid, self + field::kGrid, 0u);
    for (const std::size_t offset : {field::kZoned, field::kUndeveloped, field::kCountsCC})
        for (uint32_t i = 0; i < kTypes; ++i)
            Virtual<uint8_t>(stream, 0x20, &At<uint32_t>(self, offset + i * 4));
    for (const std::size_t offset :
         {field::kUndevelopedTotal, field::kCount110, field::kAbandoned, field::kCount118})
        Virtual<uint8_t>(stream, 0x20, &At<uint32_t>(self, offset));
    for (const std::size_t offset : {field::kMinSize, field::kMaxSize})
        for (uint32_t i = 0; i < kTypes; ++i)
            Virtual<uint8_t>(stream, 0x20, &At<uint32_t>(self, offset + i * 4));
    for (const std::size_t offset : {field::kZoneCost, field::kDezoneCost})
        for (uint32_t i = 0; i < kTypes; ++i)
            Virtual<uint8_t>(stream, 0x28, &At<int64_t>(self, offset + i * 8));
    for (const std::size_t offset : {field::kTextures, field::kColors})
        for (uint32_t i = 0; i < kTypes; ++i)
            Virtual<uint8_t>(stream, 0x24, &At<uint32_t>(self, offset + i * 4));
    Virtual<uint8_t>(stream, 0x14, &At<uint8_t>(self, field::kAlpha));
    MapEntry entry;
    ReadMap(stream, Failures(self), entry);
    ReadMap(stream, Map370(self), entry);
    ReadRef(stream, kIidCity, self + field::kCity, 0u);
    ReadRef(stream, kIidOccupants, self + field::kOccupants, 0u);
    ReadRef(stream, kIidTerrain, self + field::kTerrain, 0u);
    ReadRef(stream, kIidPollution, self + field::kPollution, 0u);
    ReadRef(stream, kIidBudget, self + field::kBudget, 0u);
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

const Slot kSaveSlots[] = {
    {addr::cSC4ZoneManager_vftable_8, 3, addr::cSC4ZoneManager_Write, reinterpret_cast<void*>(&Write)},
    {addr::cSC4ZoneManager_vftable_8, 4, addr::cSC4ZoneManager_Read, reinterpret_cast<void*>(&Read)},
};

bool InstallSave(registry::Mode) { return SwapSlots(kSaveSlots, 2, "save"); }

const bool g_registered[] = {
    registry::Register({"simulators.zonemgr.slots", "simulators/zonemgr", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4ZoneManager_vftable,
                        "Our zone manager queries and small setters: the counts, the failure counts, the zone type "
                        "of a cell, textures, colours, sizes and the display alpha (src/core/zonemgr_sim.cpp)",
                        false, InstallSlots}),
    registry::Register({"simulators.zonemgr.init", "simulators/zonemgr", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4ZoneManager_Init,
                        "Our zone manager Init (the tuning exemplar, the services, the counts and the zone grid) "
                        "and Shutdown (src/core/zonemgr_sim.cpp)",
                        false, InstallInit}),
    registry::Register({"simulators.zonemgr.messages", "simulators/zonemgr", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4ZoneManager_vftable_4,
                        "Our zone manager DoMessage: the cells under a new network occupant dezoned "
                        "(src/core/zonemgr_sim.cpp)",
                        false, InstallMessages}),
    registry::Register({"simulators.zonemgr.save", "simulators/zonemgr", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4ZoneManager_vftable_8,
                        "Our zone manager Write and Read, version 1 (src/core/zonemgr_sim.cpp)", false,
                        InstallSave}),
};

// --- Our object -------------------------------------------------------------------

// Copies of the game's 0x00A92E00 (22 slots, deleting destructor last),
// 0x00A92DF0 (at +4) and 0x00A92DD8 (at +8), taken at the first
// construction: a city is made long after PostAppInit, so the other
// groups' slot swaps are in them.
void* g_vtable[kSlots];
void* g_vtable4[kMessageSlots];
void* g_vtable8[kSerializableSlots];

// As the game's (0x004F7BA0, shared with other classes).
uint32_t __fastcall AddRef(char* self, void*) { return ++At<uint32_t>(self, field::kRefCount); }

// As the game's (0x004DC1B0). The game's null tests of `this` for the
// parts cannot be met.
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    std::size_t offset;
    switch (iid) {
    case kIidUnknown:
    case kIidManager: offset = 0; break;
    case kIidMessageTarget: offset = kMessagePart; break;
    case kIidSerializable: offset = kSerializablePart; break;
    default: return 0;
    }
    *object = self + offset;
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// As the game's (0x004DC430): Shutdown and the deleting destructor (slot
// 21) at the last reference, which is not counted down.
uint32_t __fastcall ReleaseObject(char* self, void*)
{
    uint32_t& refs = At<uint32_t>(self, field::kRefCount);
    if (refs > 1)
        return --refs;
    Virtual<uint8_t>(self, slot::kShutdown);
    Virtual<void*>(self, slot::kDeletingDestructor, 1u);
    return 0;
}

// The destructor (0x004DCC70): the map at +0x370, then the one at +0x364,
// each emptied and its head freed. The services are Shutdown's.
void Destroy(char* self)
{
    At<void**>(self, 0x0) = g_vtable;
    At<void**>(self, kMessagePart) = g_vtable4;
    At<void**>(self, kSerializablePart) = g_vtable8;
    for (Tree* const tree : {Map370(self), Failures(self)}) {
        ClearTree(tree);
        if (tree->head)
            Free(tree->head);
    }
}

// Slot 21, as the game's (0x004DCF40).
void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    static int destroyed = 0;
    if (destroyed++ < 3)
        log::Write("zonemgr object: destroying %p", static_cast<void*>(self));
    Destroy(self);
    if (flags & 1)
        OperatorDelete(self);
    return self;
}

// As the game's (0x004DC1A0).
uint32_t __fastcall GetGZCLSID(void*, void*) { return kClassId; }

template <std::size_t Offset>
uint8_t __fastcall QueryInterfaceAt(char* part, void*, uint32_t iid, void** object)
{
    return QueryInterface(part - Offset, nullptr, iid, object);
}

template <std::size_t Offset>
uint32_t __fastcall AddRefAt(char* part, void*)
{
    return AddRef(part - Offset, nullptr);
}

template <std::size_t Offset>
uint32_t __fastcall ReleaseAt(char* part, void*)
{
    return ReleaseObject(part - Offset, nullptr);
}

void BuildVtables()
{
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4ZoneManager_vftable), sizeof(g_vtable));
    std::memcpy(g_vtable4, reinterpret_cast<const void*>(addr::cSC4ZoneManager_vftable_4), sizeof(g_vtable4));
    std::memcpy(g_vtable8, reinterpret_cast<const void*>(addr::cSC4ZoneManager_vftable_8), sizeof(g_vtable8));
    g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
    g_vtable[1] = reinterpret_cast<void*>(&AddRef);
    g_vtable[2] = reinterpret_cast<void*>(&ReleaseObject);
    g_vtable[21] = reinterpret_cast<void*>(&DeletingDestructor);
    g_vtable4[0] = reinterpret_cast<void*>(&QueryInterfaceAt<kMessagePart>);
    g_vtable4[1] = reinterpret_cast<void*>(&AddRefAt<kMessagePart>);
    g_vtable4[2] = reinterpret_cast<void*>(&ReleaseAt<kMessagePart>);
    g_vtable8[0] = reinterpret_cast<void*>(&QueryInterfaceAt<kSerializablePart>);
    g_vtable8[1] = reinterpret_cast<void*>(&AddRefAt<kSerializablePart>);
    g_vtable8[2] = reinterpret_cast<void*>(&ReleaseAt<kSerializablePart>);
    g_vtable8[5] = reinterpret_cast<void*>(&GetGZCLSID);
    log::Write("zonemgr object: vtables copied (slots 3/9/11/12 %p %p %p %p, +4 slot 3 %p, +8 slots 3/4 %p %p)",
               g_vtable[3], g_vtable[9], g_vtable[11], g_vtable[12], g_vtable4[3], g_vtable8[3], g_vtable8[4]);
}

// An empty map's head node (0x18 bytes from the STL allocator).
char* NewTreeHead()
{
    char* const head = static_cast<char*>(Allocate(node::kSize));
    At<uint8_t>(head, 0x0) = 0;
    At<char*>(head, node::kParent) = nullptr;
    At<char*>(head, node::kLeft) = head;
    At<char*>(head, node::kRight) = head;
    return head;
}

// The constructor (detour of 0x004DCE50, reached only by the jump from the
// factory 0x004DD3E0): the fields the game's sets, in its order; the count
// arrays, the tuning and +0x361 stay unset until Init.
void* __fastcall Construct(char* self, void*)
{
    static bool built = false;
    if (!built) {
        built = true;
        BuildVtables();
    }
    At<void**>(self, 0x0) = g_vtable;
    At<void**>(self, kMessagePart) = g_vtable4;
    At<void**>(self, kSerializablePart) = g_vtable8;
    At<uint8_t>(self, field::kInitialized) = 0;
    At<uint32_t>(self, field::kRefCount) = 0;
    At<uint8_t>(self, field::kLoaded) = 0;
    At<void*>(self, field::kMessages) = nullptr;
    At<void*>(self, field::kCity) = nullptr;
    At<void*>(self, field::kOccupants) = nullptr;
    At<void*>(self, field::kTerrain) = nullptr;
    At<void*>(self, field::kPollution) = nullptr;
    At<void*>(self, field::kBudget) = nullptr;
    {
        // Clock seeds count under the original's call site.
        const fixed_seed::SiteOverride site(0x004DCE9D);
        reinterpret_cast<RandomFn>(addr::SC4Random_Construct)(self + field::kRandom, -1);
    }
    At<void*>(self, field::kGrid) = nullptr;
    At<int32_t>(self, field::kUndevelopedTotal) = 0;
    At<int32_t>(self, field::kCount110) = 0;
    At<int32_t>(self, field::kAbandoned) = 0;
    At<int32_t>(self, field::kCount118) = 0;
    At<uint8_t>(self, field::kAlpha) = 0xFF;
    Failures(self)->head = nullptr;
    Failures(self)->head = NewTreeHead();
    Failures(self)->count = 0;
    Map370(self)->head = nullptr;
    Map370(self)->head = NewTreeHead();
    Map370(self)->count = 0;
    static int created = 0;
    if (created++ < 3)
        log::Write("zonemgr object: constructed %p", static_cast<void*>(self));
    return self;
}

using ConstructFn = void*(__fastcall*)(char*, void*);
ConstructFn g_originalConstruct = nullptr;

bool ApplyObject(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    const addr::Entry* const entry = addr_check::Find(addr::cSC4ZoneManager_cSC4ZoneManager);
    return entry && hook::Install(*entry, &Construct, &g_originalConstruct);
}

const bool g_registeredObject = registry::Register({
    "simulators.zonemgr.object",
    "simulators/zonemgr",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4ZoneManager_cSC4ZoneManager,
    "Our cSC4ZoneManager object: constructor (detour), destructor, identity; methods from copies of the "
    "(patched) game vtables (src/core/zonemgr_sim.cpp)",
    false,
    ApplyObject,
});

} // namespace
} // namespace opensc4::zonemgr_sim
