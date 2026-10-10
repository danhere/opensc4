// Our cSC4LotDeveloper, the build part (build 641; the class map is in
// lot_sim.h): every function with its entry in 0x006C7280-0x006CB4D0 that is
// not the object part's (the destructors, Shutdown, the constructor, the
// factory and the interface thunks). Nine slots of the primary vtable and 21
// helpers.
//
// The slots:
//   13 EndDemolishLot (0x006C90A0)       33 RemoveLotFromItsBusinessUnit (0x006C7320)
//   36 NeedsAnchorBuilding (0x006C7A70)  38 GetFirstBuilding (0x006C9100)
//   44 SetNormalLotBaseTexture (0x006CA8A0)
//   47 SetForceEmptyLotBaseTextures (0x006C77D0)
//   49 GetFoundationByStandardCityCell (0x006C9480)
//   50 GetFoundationByPosition (0x006C95E0)
//   55 AddLotFoundation (0x006C8AB0)
//
// The helpers (names ours), with who calls them:
//   0x006C7630 LowestNeighbourAltitude       AddLotFoundation
//   0x006C7720 UpdateNeighbourFoundations    ClearLot, AddLotFoundation
//   0x006C7860 CreateConfigurationProps      DoMessage (object part)
//   0x006C78D0 TryPlaceSpecialLot            UpdateLot (construct part)
//   0x006C8830 RemoveNightBuilding           DoMessage
//   0x006C8A20 ClearLot                      EndDemolishLot
//   0x006C8E10 LightBuilding                 the night steps (construct part)
//   0x006C8F80 AddNightBuilding              DoMessage
//   0x006C91F0 SettleLotOccupants            UpdateLotElements (construct)
//   0x006C9780 RemoveZotProps                this part, construct part
//   0x006C98C0 ShowZot                       this part, construct part
//   0x006C9A40 HideZot                       CheckAccess
//   0x006C9AD0 DemolishFilteredOccupants     StartConstruction (construct)
//   0x006C9C80 PlaceNetworkPieces            DoMessage
//   0x006C9FB0 RemoveLotFlora                StartDemolishLot, ExplodeLot
//   0x006CA160 FillBoundaryTextureMap        Init (object part)
//   0x006CAAF0 RemoveBaseTextures            SetEmptyLotBaseTexture (construct)
//   0x006CABB0 CheckPower, 0x006CAE30 CheckWater, 0x006CAFC0 CheckAccess
//                                            UpdateLot (construct part)
//   0x006CB260 JoinBusinessUnit              construct part (three sites)
// The helpers other parts or the game's remaining code call are detoured;
// every call site (listed at the detour table) pushes its arguments and
// keeps nothing in EAX, ECX or EDX across the call, so plain detours do.
//
// Left the game's: the building deque's container code in this range
// (0x006C7C50 erase one, 0x006C7E50 erase a range, 0x006C8080 with its tail
// 0x006C8562 and 0x006C8C50 fill inserts) and the generic helpers it shares
// (0x006C6860 iterator + n, 0x006C69D0 find).
//
// Fields used here (offsets from the object's start; lot_sim.h has the
// whole list). Evidence is the code of the functions below:
//   0x18 kDevelopment  the building development simulator: slots 23
//                      (foundation occupant), 33 (developer), 39 (building
//                      profile), 50 (claim construction resources), 54/55
//                      (the plumbing and power cheats)
//   0x1C kCity         slots 91/92: the cell size (CheckPower)
//   0x20 kService20    0x00B43D00, the demolition utility (city slot 31):
//                      slots 7 and 8 demolish a list of lots / an occupant
//   0x24 kLots         the lot manager: slots 6, 7, 9, 10, 24
//   0x28 kOccupants    the occupant manager: slots 20, 24, 36, 37, 41, 48;
//                      GetFoundationByPosition fetches it when null
//   0x2C kService2C    0x00B43D94, the plumbing simulator: slot 11
//                      IsCellWatered
//   0x30 kProps        the prop manager: slots 24, 26, 29 (zot props)
//   0x34 kPower        the power simulator: slot 37 IsPowered
//   0x3C kTerrain      slot 13: the altitude at (x, z)
//   0x4C kCellsZ, 0x50 kCellsX  texture entries outside the city are dropped
//   0x54 kTraffic      slot 13: a byte grid, read at the lot's cell
//   0x58 kFilter       the occupant filter DemolishFilteredOccupants passes
//   0x5C kForceEmptyTextures
//   0x5E kDirty5E      set when business units change
//   0x64 kBuildings    the deque of buildings of the day/night step
//   0x8C kNextBuilding the step's iterator, kept on its building when the
//                      deque changes
//   0xA8 kFlagA8       uint8: whether buildings are lit now (a new building
//                      gets it through slot 17 SetLit)
//   0xFC kTree         std::map<int32, {uint32 texture, uint8 rotation,
//                      uint8 flip}>: the boundary textures by neighbour key
//
// A lot's properties used for the zots (the lot's property holder, slot 3):
// 0xC999C45E the zot shown (4 no power, 5 no water, 6 no road, 7 no job
// access; 0 none), 0xE999B11F / 0x8999B20B / 0x8999B10B the counts of
// updates without power / water / access. A zot prop carries its kind in
// its property 0x48E95539.
#include "core/lot_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>

namespace opensc4::lot_sim {
namespace {

// --- Types ---------------------------------------------------------------------

struct Rect { // cells: cISC4Lot slot 14, cISC4Occupant slot 11
    int32_t x1, z1, x2, z2;
};

struct Vec3 {
    float x, y, z;
};

// A node of the game's lists (std::list of pointers): the list itself is one
// pointer to a sentinel node.
struct ListNode {
    ListNode* next;
    ListNode* prev;
    void* value;
};

// An iterator of the building deque (blocks of 32 pointers).
struct DequeIt {
    void** cur;
    void** first;
    void** last;
    void*** node;
};

struct Deque {
    DequeIt start;
    DequeIt finish;
    void*** map;
    uint32_t mapSize;
};

// The lot manager's lot iterator (slot 15; 8 bytes from operator new).
struct LotIterator {
    ListNode* cur;
    ListNode** list; // the manager's list of lots; its +4 is a list of the iterators on it
};

struct PointerVector {
    void** begin;
    void** end;
    void** capacity;
};

// A base texture entry (cSC4LotBaseTextureOccupant::SetTextureSpecification;
// constructor 0x005CA770).
struct TextureEntry {
    uint32_t texture;
    uint8_t x, z, rotation, flag;
    uint8_t ff[6];
    uint8_t unset[2];
};
static_assert(sizeof(TextureEntry) == 16, "texture entry size");

struct TextureVector {
    TextureEntry* begin;
    TextureEntry* end;
    TextureEntry* capacity;
};

// A lot configuration object (0x34 bytes; cISC4LotConfiguration slot 23
// gives the vector of them).
namespace object {
constexpr std::size_t kType = 0x0;   // uint8: 1 prop, 2 base texture, 7 network piece
constexpr std::size_t kFlags = 0x1;  // uint8
constexpr std::size_t kByte3 = 0x3;  // uint8, copied into a texture entry
constexpr std::size_t kIds = 0x28;   // std::vector<uint32>: begin, end at +0x2C
constexpr std::size_t kSize = 0x34;
} // namespace object

constexpr uint32_t kTypeBuilding = 0x278128A0;
constexpr uint32_t kTypeProp = 0xA823821E;
constexpr uint32_t kTypeFlora = 0xC772BF98;
constexpr uint32_t kTypeFoundation = 0x68FD0C69;
constexpr uint32_t kTypeBaseTexture = 0x497F6D9D;
constexpr uint32_t kIidBuilding = 0x87DDA3A9;
constexpr uint32_t kIidFoundation = 0xC8FCE101;
constexpr uint32_t kIidFlora = 0xA821EF94;
constexpr uint32_t kPropertyZot = 0xC999C45E;
constexpr uint32_t kPropertyZotKind = 0x48E95539;

// --- The game's helpers ----------------------------------------------------------

using AllocFn = void*(__cdecl*)(std::size_t);
using FreeFn = void(__cdecl*)(void*);
using BelowFn = uint32_t(__thiscall*)(void* random, uint32_t n);
using ListFn = void(__thiscall*)(ListNode** list);
using ListConstructFn = void(__thiscall*)(ListNode** list, void* unused);
using ListPushFn = void(__thiscall*)(ListNode** list, void* const* object);
using HolderFn = void**(__thiscall*)(void** holder, void* object);
using VectorPushFn = void(__thiscall*)(PointerVector* vector, void* const* object);
using VectorDestroyFn = void(__thiscall*)(PointerVector* vector);
using IteratorFreeFn = void(__thiscall*)(void* iterators, LotIterator* const* iterator);
using LotHelper2Fn = void(__cdecl*)(void* lot, uint32_t flag);
using LotHelperFn = void(__cdecl*)(void* lot);
using MapIndexFn = uint32_t*(__thiscall*)(void* map, const int32_t* key);
using TexturePushFn = void(__thiscall*)(TextureVector* vector, const TextureEntry* entry);
using DequePushFn = void(__thiscall*)(Deque* deque, void* const* value);
using DequeFindFn = void*(__cdecl*)(DequeIt* result, DequeIt first, DequeIt last, void* const* value);
using DequeEraseFn = DequeIt*(__thiscall*)(Deque* deque, DequeIt* result, DequeIt at);
using DequeAdvanceFn = DequeIt*(__thiscall*)(const DequeIt* it, DequeIt* result, int32_t n);
using PostMessageFn = void(__cdecl*)(uint32_t message, uint32_t a, uint32_t b, uint32_t c, uint32_t d, void* e);
using UnitConstructFn = void*(__thiscall*)(void* unit);
// Other parts' functions, called at the game's address.
// (0x006C4360 is the object part's; 0x006C5360, 0x006C60C0, 0x006C62F0 and
// 0x006C6D70 the terrain part's.)
using CornerAltitudesFn = void(__thiscall*)(void* self, const Rect* rect, float* corners, uint32_t flag);
using FoundationAltitudeFn = float(__thiscall*)(void* self, void* lot, float lowest, float highest,
                                                const float* corners);
using PickTextureFn = uint32_t(__thiscall*)(void* self, void* lot, void* ids);
using RemoveListedFn = void(__thiscall*)(void* self, ListNode** list);
using CreatePropFn = uint32_t(__thiscall*)(void* self, void* lot, int32_t index);

void* Allocate(std::size_t size) { return reinterpret_cast<AllocFn>(addr::SC4_stl_allocate)(size); }
void Free(void* block) { reinterpret_cast<FreeFn>(addr::SC4_Free)(block); }
void Release(void* object) { Virtual<uint32_t>(object, 0x8); }

uint32_t Below(uint32_t n)
{
    return reinterpret_cast<BelowFn>(addr::SC4Random_BelowLcg)(reinterpret_cast<void*>(addr::cSC4LotDeveloper_Random),
                                                               n);
}

// The truncating float-to-int conversion (_ftol, 0x009EEF04): the low dword
// of the int64.
int32_t Ftol(double value) { return static_cast<int32_t>(static_cast<int64_t>(value)); }

// An empty list with its sentinel from the STL allocator (inline in the
// game: 0x0090CF54 for 0xC bytes, self-linked; the value is left unset).
ListNode* NewStlList()
{
    auto* const head = static_cast<ListNode*>(Allocate(sizeof(ListNode)));
    head->next = head;
    head->prev = head;
    return head;
}

// The end of such a list of references (inline in the game): every object
// released, every node and then the sentinel freed.
void ReleaseStlList(ListNode* head)
{
    for (ListNode* node = head->next; node != head;) {
        ListNode* const current = node;
        void* const object = current->value;
        node = node->next;
        if (object)
            Release(object);
        Free(current);
    }
    head->next = head;
    head->prev = head;
    Free(head);
}

// An empty list with its sentinel from the node allocator cached at
// 0x00B207E4 (inline in the game: the allocator's slot 3 for 0xC bytes).
ListNode* NewNodeList()
{
    auto* const head = Virtual<ListNode*>(ListNodeAllocator(), 0xC, 0xCu);
    head->next = head;
    head->prev = head;
    return head;
}

void FreeNode(ListNode* node) { Virtual<void>(ListNodeAllocator(), 0x10, node); }

// The inline end of such a list: the nodes (0x00413E60), then the sentinel.
void ClearAndFreeNodeList(ListNode** list)
{
    reinterpret_cast<ListFn>(addr::SC4List_Clear2)(list);
    FreeNode(*list);
}

// The same through the game's helper 0x00413FE0.
void DestroyNodeList(ListNode** list) { reinterpret_cast<ListFn>(addr::SC4List_Destroy2)(list); }

// The network manager: 0x00602680, the city's slot 38 (null without a city).
void* NetworkManager()
{
    void* const city = Global<void*>(addr::SC4_City);
    return city ? Virtual<void*>(city, 0x98) : nullptr;
}

// The value object of a property of a holder (holder slot 9, then the
// property's slot 5), made first (holder slot 13 with 0, 0) when missing.
void* PropertyValueMade(void* holder, uint32_t property)
{
    void* found = Virtual<void*>(holder, 0x24, property);
    if (!found) {
        Virtual<void>(holder, 0x34, property, 0u, 0u);
        found = Virtual<void*>(holder, 0x24, property);
    }
    return Virtual<void*>(found, 0x14);
}

// The distance of a deque iterator from the deque's start, as the game
// computes it inline (each byte difference shifted right by 2).
int32_t DequeIndex(const DequeIt& it, const DequeIt& start)
{
    const auto bytes = [](const void* a, const void* b) {
        return static_cast<int32_t>(reinterpret_cast<intptr_t>(a) - reinterpret_cast<intptr_t>(b));
    };
    return (bytes(it.cur, it.first) >> 2) + (bytes(it.node, start.node) >> 2) * 32 +
           (bytes(start.last, start.cur) >> 2) - 0x20;
}

DequeIt DequeAt(const DequeIt& start, int32_t index)
{
    DequeIt from = start;
    DequeIt result;
    return *reinterpret_cast<DequeAdvanceFn>(addr::cSC4LotDeveloper_Fn_006C6860)(&from, &result, index);
}

// --- The helpers ---------------------------------------------------------------

bool RemoveZotProps(void* self, void* lot, uint32_t kind);

// 0x006C7630: the lower of the highest altitude of the lot's rectangle (slot
// 17 with 1, 1) and the altitudes of its neighbours (lot manager slot 9 with
// the mask 0xF) whose flags have bit 0 or 1. ST0 in the game; its one
// caller stores it as a float.
float LowestNeighbourAltitude(void* self, void* lot)
{
    static bool logged = false;
    FirstCall("LowestNeighbourAltitude", logged);
    Rect rect;
    Virtual<void>(lot, 0x38, &rect);
    float lowest = Virtual<float>(self, 0x44, &rect, 1u, 1u);
    ListNode* list = NewStlList();
    // The game tests all of EAX.
    if (Virtual<uint32_t>(At<void*>(self, field::kLots), 0x24, lot, &list, 0xFu)) {
        for (ListNode* node = list->next; node != list; node = node->next) {
            void* const neighbour = node->value;
            if (Virtual<uint32_t>(neighbour, 0x30) & 3) {
                const float altitude = Virtual<float>(neighbour, 0x40);
                if (lowest > altitude)
                    lowest = altitude;
            }
        }
    }
    ReleaseStlList(list);
    return lowest;
}

// 0x006C7720: UpdateLotFoundation (slot 61) for each such neighbour.
void UpdateNeighbourFoundations(void* self, void* lot)
{
    static bool logged = false;
    FirstCall("UpdateNeighbourFoundations", logged);
    ListNode* list = NewStlList();
    if (Virtual<uint32_t>(At<void*>(self, field::kLots), 0x24, lot, &list, 0xFu)) {
        for (ListNode* node = list->next; node != list; node = node->next) {
            void* const neighbour = node->value;
            if (Virtual<uint32_t>(neighbour, 0x30) & 3)
                Virtual<void>(self, 0xF4, neighbour);
        }
    }
    ReleaseStlList(list);
}

// 0x006C7860 (lot, configuration, unused): the terrain part's 0x006C6D70
// (lot, index) for each type-1 object of the configuration whose flags have
// bit 0 clear.
void __fastcall CreateConfigurationProps(void* self, void*, void* lot, void* configuration, uint32_t)
{
    static bool logged = false;
    FirstCall("CreateConfigurationProps", logged);
    auto* const objects = Virtual<char**>(configuration, 0x5C);
    if (!objects)
        return;
    const int32_t count = static_cast<int32_t>(objects[1] - objects[0]) / static_cast<int32_t>(object::kSize);
    for (int32_t i = 0; i < count; ++i) {
        char* const item = objects[0] + i * static_cast<int32_t>(object::kSize);
        if (At<uint8_t>(item, object::kType) == 1 && !(At<uint8_t>(item, object::kFlags) & 1))
            reinterpret_cast<CreatePropFn>(addr::cSC4LotDeveloper_Fn_006C6D70)(self, lot, i);
    }
}

// 0x006C78D0 (lot): one update in about eleven (a draw below 9 of 100, made
// first), a lot in state 3 or 4 that is not historical, whose configuration
// is not 0x60000C40 and has a slot 11 value of at most 3, and whose size is
// that of configuration 0x60000C40, is demolished (demolition utility slot
// 7) and, when the development simulator grants the resources of building
// 0x05B80000 (profile +8), rebuilt as that (slot 6) and made historical.
void __fastcall TryPlaceSpecialLot(void* self, void*, void* lot)
{
    static bool logged = false;
    FirstCall("TryPlaceSpecialLot", logged);
    const int32_t state = Virtual<int32_t>(lot, 0x84);
    void* const configuration = Virtual<void*>(lot, 0x24);
    const uint32_t draw = Below(100);
    if (Virtual<uint8_t>(lot, 0xAC))
        return;
    if (state != 3 && state != 4)
        return;
    if (draw >= 9 || !configuration)
        return;
    if (Virtual<uint32_t>(configuration, 0xC) == 0x60000C40)
        return;
    if (Virtual<uint8_t>(configuration, 0x2C) > 3)
        return;
    uint8_t width = 0;
    uint8_t depth = 0;
    Virtual<void>(lot, 0x58, &width, &depth);
    void* const special = Virtual<void*>(Global<void*>(addr::SC4_Service43D18), 0x20, 0x60000C40u);
    if (!special)
        return;
    uint32_t specialWidth = 0;
    uint32_t specialDepth = 0;
    Virtual<void>(special, 0x1C, &specialWidth, &specialDepth);
    if (width != specialWidth || depth != specialDepth)
        return;
    Virtual<uint32_t>(lot, 0x4);
    ListNode* list = nullptr;
    reinterpret_cast<ListConstructFn>(addr::SC4RefList_Construct)(&list, &width);
    void* holder = nullptr;
    reinterpret_cast<HolderFn>(addr::SC4RefHolder_SC4RefHolder)(&holder, lot);
    reinterpret_cast<ListPushFn>(addr::SC4RefList_PushBack2)(&list, &holder);
    if (holder)
        Release(holder);
    Virtual<void>(Global<void*>(addr::SC4_Service43D00), 0x1C, 1u, &list, 0u, 0x800u, 0u, 0u, 0u, 0u);
    void* const development = At<void*>(self, field::kDevelopment);
    void* const profile = Virtual<void*>(development, 0x9C, 0x05B80000u);
    if (Virtual<uint8_t>(At<void*>(self, field::kDevelopment), 0xC8, static_cast<uint32_t>(At<uint16_t>(profile, 8)))) {
        Virtual<void>(self, 0x18, lot, special, 0x05B80000u, 0u);
        Virtual<void>(lot, 0xB0, 1u);
    }
    Release(lot);
    reinterpret_cast<ListFn>(addr::SC4RefList_Destroy)(&list);
}

// 0x006C8830 (occupant): a building leaves the deque of the day/night step.
// The step's iterator stays on its building: when it was on the erased one
// it moves to the one after; else its index is kept, less one when the
// erased building was before it. The deque's reference is released.
void __fastcall RemoveNightBuilding(void* self, void*, void* occupant)
{
    static bool logged = false;
    FirstCall("RemoveNightBuilding", logged);
    void* building = nullptr;
    if (Virtual<uint8_t>(occupant, 0x0, kIidBuilding, &building)) {
        auto& deque = At<Deque>(self, field::kBuildings);
        auto& next = At<DequeIt>(self, field::kNextBuilding);
        DequeIt found;
        reinterpret_cast<DequeFindFn>(addr::cSC4LotDeveloper_Fn_006C69D0)(&found, deque.start, deque.finish, &building);
        if (found.cur != deque.finish.cur) {
            const auto erase = reinterpret_cast<DequeEraseFn>(addr::cSC4LotDeveloper_Fn_006C7C50);
            DequeIt result;
            if (next.cur == found.cur) {
                next = *erase(&deque, &result, found);
            } else {
                int32_t index = DequeIndex(next, deque.start);
                const DequeIt oldStart = deque.start;
                erase(&deque, &result, found);
                if (index > DequeIndex(found, oldStart))
                    --index;
                next = DequeAt(deque.start, index);
            }
            Release(building);
        }
    }
    if (building)
        Release(building);
}

// 0x006C8A20 (lot): empties a demolished lot.
void ClearLot(void* self, void* lot)
{
    static bool logged = false;
    FirstCall("ClearLot", logged);
    uint32_t x = 0;
    uint32_t z = 0;
    Virtual<void>(lot, 0x54, &x, &z);
    reinterpret_cast<LotHelper2Fn>(addr::SC4_LotHelper_005E3070)(lot, 0);
    Virtual<void>(self, 0xD4, x, z);
    UpdateNeighbourFoundations(self, lot);
    reinterpret_cast<LotHelperFn>(addr::SC4_LotHelper_005E3960)(lot);
    Virtual<void>(lot, 0x114, 0u);
    Virtual<void>(lot, 0x88, 1u, 0u, 0u);
    Virtual<void>(lot, 0xBC, 0u);
    Virtual<void>(lot, 0x28, 0u);
    Virtual<void>(self, 0xAC, lot);
}

// 0x006C8E10 (building, lit): lights a building (slot 17), then tells the
// props of its lot that have a set property 0x4A9F188B (occupant message
// 0x09D8E0B6). The lot is the one at the middle of the building's cells.
void __fastcall LightBuilding(void* self, void*, void* building, uint32_t lit)
{
    static bool logged = false;
    FirstCall("LightBuilding", logged);
    Virtual<void>(building, 0x44, lit);
    void* const occupant = Virtual<void*>(building, 0xC);
    Rect cells;
    Virtual<void>(occupant, 0x2C, &cells);
    void* const lot =
        Virtual<void*>(At<void*>(self, field::kLots), 0x18, (cells.x1 + cells.x2) >> 1, (cells.z1 + cells.z2) >> 1, 0u);
    if (!lot)
        return;
    Virtual<void>(lot, 0x38, &cells);
    int32_t minCell[2] = {cells.x1, cells.z1};
    int32_t maxCell[2] = {cells.x2, cells.z2};
    ListNode* list = NewNodeList();
    if (Virtual<uint8_t>(At<void*>(self, field::kOccupants), 0xA4, &list, minCell, maxCell, kTypeProp, 0xFFFFFFFFu)) {
        ListNode* const head = list;
        for (ListNode* node = head->next; node != head; node = node->next) {
            void* const prop = node->value;
            void* const holder = Virtual<void*>(prop, 0x18);
            void* const property = Virtual<void*>(holder, 0x24, 0x4A9F188Bu);
            if (property) {
                void* const value = Virtual<void*>(property, 0x14);
                if (*Virtual<uint8_t*>(value, 0xFC))
                    Virtual<void>(prop, 0x3C, 0x09D8E0B6u, 0u);
            }
            Release(prop);
        }
    }
    DestroyNodeList(&list);
}

// 0x006C8F80 (occupant): a building joins the deque of the day/night step,
// lit as the developer's flag +0xA8 says; the step's iterator keeps its
// index.
void __fastcall AddNightBuilding(void* self, void*, void* occupant)
{
    static bool logged = false;
    FirstCall("AddNightBuilding", logged);
    void* building = nullptr;
    if (Virtual<uint8_t>(occupant, 0x0, kIidBuilding, &building)) {
        Virtual<uint32_t>(building, 0x4);
        auto& deque = At<Deque>(self, field::kBuildings);
        auto& next = At<DequeIt>(self, field::kNextBuilding);
        const int32_t index = DequeIndex(next, deque.start);
        if (deque.finish.cur != deque.finish.last - 1) {
            if (deque.finish.cur)
                *deque.finish.cur = building;
            ++deque.finish.cur;
        } else {
            void* value = building;
            reinterpret_cast<DequePushFn>(addr::SC4Uint32Deque_PushBackSlow)(&deque, &value);
        }
        Virtual<void>(building, 0x44, static_cast<uint32_t>(At<uint8_t>(self, field::kFlagA8)));
        next = DequeAt(deque.start, index);
    }
    if (building)
        Release(building);
}

// 0x006C91F0 (lot): after the ground under a lot moved. Each building, prop
// and type-0x74758926 occupant of the lot whose altitude is more than 0.1
// off its place (slot 39 for an occupant of a configuration object, else
// the terrain at its position) is moved there (occupant slot 9, occupant
// manager slot 24); a building's foundation is removed and made again when
// one is still required (slot 59 with the old foundation's type, else slot
// 58 with the lot's configuration). Returns false (AL).
uint8_t __fastcall SettleLotOccupants(void* self, void*, void* lot)
{
    static bool logged = false;
    FirstCall("SettleLotOccupants", logged);
    ListNode* list = NewNodeList();
    Virtual<void>(lot, 0xC4, &list);
    for (ListNode* node = list->next; node != list; node = node->next) {
        void* const occupant = node->value;
        const uint32_t type = Virtual<uint32_t>(occupant, 0x1C);
        if (type == kTypeBuilding || type == kTypeProp || type == 0x74758926) {
            Vec3 position;
            Virtual<void>(occupant, 0x20, &position);
            void* const configuration = Virtual<void*>(lot, 0x24);
            void* const item = Virtual<void*>(lot, 0x2C, occupant);
            // The game leaves the target and the rotation unset before slot 39.
            Vec3 target = {};
            uint32_t rotation = 0;
            if (item) {
                Virtual<void>(self, 0x9C, lot, configuration, item, &target, &rotation);
            } else {
                target.x = position.x;
                target.y = Virtual<float>(At<void*>(self, field::kTerrain), 0x34, position.x, position.z);
                target.z = position.z;
            }
            // 0x00A8C950: 0.1f.
            const double off = std::fabs(static_cast<double>(position.y) - static_cast<double>(target.y));
            if (off > static_cast<double>(0.1f)) {
                position.y = target.y;
                Virtual<void>(occupant, 0x24, &position);
                Virtual<void>(At<void*>(self, field::kOccupants), 0x60, occupant, 1u);
                if (type == kTypeBuilding) {
                    uint32_t foundationType = 0;
                    void* const foundation = Virtual<void*>(self, 0xC0, occupant);
                    if (foundation) {
                        foundationType = Virtual<uint32_t>(foundation, 0x10);
                        Virtual<void>(self, 0xF0, occupant);
                    }
                    if (Virtual<uint8_t>(self, 0xE0, occupant)) {
                        void* building = nullptr;
                        if (Virtual<uint8_t>(occupant, 0x0, kIidBuilding, &building)) {
                            if (foundationType)
                                Virtual<void>(self, 0xEC, building, foundationType);
                            else
                                Virtual<void>(self, 0xE8, building, configuration);
                        }
                        if (building)
                            Release(building);
                    }
                }
            }
        } else if (type == kTypeFlora) {
            // A query with nothing done to the result.
            void* flora = nullptr;
            Virtual<uint8_t>(occupant, 0x0, kIidFlora, &flora);
            if (flora)
                Release(flora);
        }
        Release(occupant);
    }
    ClearAndFreeNodeList(&list);
    return 0;
}

// 0x006C9780 (lot, kind): removes the lot's props (prop manager slot 24
// lists them, slot 29 removes one) whose property 0x48E95539 is `kind`.
// True (AL) when one was removed.
bool RemoveZotProps(void* self, void* lot, uint32_t kind)
{
    static bool logged = false;
    FirstCall("RemoveZotProps", logged);
    ListNode* list = NewNodeList();
    Virtual<void>(At<void*>(self, field::kProps), 0x60, &list, lot);
    bool removed = false;
    for (ListNode* node = list->next; node != list; node = node->next) {
        void* const prop = node->value;
        if (!prop)
            continue;
        void* const holder = Virtual<void*>(prop, 0x18);
        void* const property = Virtual<void*>(holder, 0x24, kPropertyZotKind);
        if (property) {
            void* const value = Virtual<void*>(property, 0x14);
            if (Virtual<uint32_t>(value, 0x120) == kind) {
                Virtual<void>(At<void*>(self, field::kProps), 0x74, prop);
                removed = true;
            }
        }
        Release(prop);
    }
    ClearAndFreeNodeList(&list);
    return removed;
}

uint8_t __fastcall RemoveZotPropsDetour(void* self, void*, void* lot, uint32_t kind)
{
    return RemoveZotProps(self, lot, kind) ? 1 : 0;
}

// 0x006C98C0 (lot, prop type, kind): shows a zot over a lot. A zot of a
// lower kind stays (the lot's property 0xC999C45E; 0 is none); else the old
// zot's props go, the property becomes `kind` and a prop of the type is made
// (prop manager slot 26) 3 above the building's bounding box top (when the
// lot has a building and a configuration with a slot 24 value: at the
// place slot 40 gives) or 3 above the lot's centre. Kind 6 posts 0x4C04DE69
// (1, lot) first.
void __fastcall ShowZot(void* self, void*, void* lot, uint32_t propType, uint32_t kind)
{
    static bool logged = false;
    FirstCall("ShowZot", logged);
    void* const building = Virtual<void*>(lot, 0xC0);
    void* const configuration = Virtual<void*>(lot, 0x24);
    uint32_t configurationValue = configuration ? Virtual<uint32_t>(configuration, 0x60) : 0;
    if (kind == 6)
        reinterpret_cast<PostMessageFn>(addr::SC4_PostMessage)(0x4C04DE69, 1, 0, 0, 0, lot);
    void* const value = PropertyValueMade(Virtual<void*>(lot, 0xC), kPropertyZot);
    const uint32_t current = *Virtual<uint32_t*>(value, 0x124);
    if (current) {
        if (kind >= current)
            return;
        RemoveZotProps(self, lot, current);
    }
    Virtual<void>(value, 0x68, kind);
    Vec3 position = {};
    double top;
    if (building && configuration && configurationValue) {
        Virtual<void>(self, 0xA0, lot, &position, &configurationValue);
        void* const occupant = Virtual<void*>(Virtual<void*>(lot, 0xC0), 0xC);
        Vec3 boxA = {};
        Vec3 boxB = {};
        Virtual<void>(occupant, 0x28, &boxA, &boxB);
        top = boxB.y;
    } else {
        Virtual<void>(lot, 0x3C, &position);
        top = position.y;
    }
    position.y = static_cast<float>(top + static_cast<double>(3.0f)); // 0x00A85074
    void* const view = Global<void*>(addr::SC4_CameraView);
    void* const zot =
        Virtual<void*>(At<void*>(self, field::kProps), 0x68, propType, &position, At<uint32_t>(view, 0x10));
    // Not checked for null by the game either.
    Virtual<void>(zot, 0x30, 3u);
    Virtual<void>(Virtual<void*>(zot, 0x18), 0x34, kPropertyZotKind, kind, 0u);
}

// 0x006C9A40 (lot, kind): takes the lot's zot away when it is of this kind
// (kind 0: whatever it is). Kind 6 posts 0x4C04DE69 (2, lot) first.
void HideZot(void* self, void* lot, uint32_t kind)
{
    static bool logged = false;
    FirstCall("HideZot", logged);
    void* const property = Virtual<void*>(Virtual<void*>(lot, 0xC), 0x24, kPropertyZot);
    if (!property)
        return;
    void* const value = Virtual<void*>(property, 0x14);
    const uint32_t current = *Virtual<uint32_t*>(value, 0x124);
    if (kind == 6) {
        reinterpret_cast<PostMessageFn>(addr::SC4_PostMessage)(0x4C04DE69, 2, 0, 0, 0, lot);
        if (current != kind)
            return;
    } else if (kind != 0 && current != kind) {
        return;
    }
    if (current)
        RemoveZotProps(self, lot, current);
    Virtual<void>(value, 0x68, 0u);
}

// The tail the checks share: a zot of `kind` that is still shown goes.
void DropZot(void* self, void* lot, uint32_t kind)
{
    void* const property = Virtual<void*>(Virtual<void*>(lot, 0xC), 0x24, kPropertyZot);
    if (!property)
        return;
    void* const value = Virtual<void*>(property, 0x14);
    if (*Virtual<uint32_t*>(value, 0x124) != kind)
        return;
    RemoveZotProps(self, lot, kind);
    Virtual<void>(value, 0x68, 0u);
}

// 0x006C9AD0 (lot, three unused): demolishes (demolition utility slot 8)
// every occupant the filter +0x58 accepts in the box over the lot, from the
// ground (y 0) to 32 above its highest point, half a unit inside its edges.
void __fastcall DemolishFilteredOccupants(void* self, void*, void* lot, uint32_t, uint32_t, uint32_t)
{
    static bool logged = false;
    FirstCall("DemolishFilteredOccupants", logged);
    Rect rect;
    Virtual<void>(lot, 0x38, &rect);
    // ST0, kept on the x87 stack while the other coordinates are made.
    const double top = static_cast<double>(Virtual<float>(self, 0x44, &rect, 0u, 1u)) + static_cast<double>(32.0f);
    const double half = static_cast<double>(0.5f);
    Vec3 low;
    Vec3 high;
    low.x = static_cast<float>(static_cast<double>(rect.x1 << 4) + half);
    low.z = static_cast<float>(static_cast<double>(rect.z1 << 4) + half);
    low.y = 0.0f;
    high.x = static_cast<float>(static_cast<double>((rect.x2 + 1) << 4) - half);
    high.y = static_cast<float>(top);
    high.z = static_cast<float>(static_cast<double>((rect.z2 + 1) << 4) - half);
    ListNode* list = NewNodeList();
    Virtual<void>(At<void*>(self, field::kOccupants), 0x90, &list, &low, &high, At<void*>(self, field::kFilter),
                  0xFFFFFFFFu);
    ListNode* const head = list;
    for (ListNode* node = head->next; node != head; node = node->next) {
        void* const occupant = node->value;
        Virtual<void>(At<void*>(self, field::kService20), 0x20, 1u, occupant, 0u, 1u, 0u, 0u, 0u);
        Release(occupant);
    }
    ClearAndFreeNodeList(&list);
}

// The cell of a coordinate, as 0x006C9D74 computes it: v = coordinate / 16
// stays on the x87 stack; (v - 0.5) + 12582912.0 is stored as a float whose
// bits less 0x4B400000 are v rounded; one more when v equals that plus one.
int32_t CellOf(float coordinate)
{
    // 0x00AB1B04: 0.0625f; 0x00A84D2C: 0.5f; 0x00AB1B08: 12582912.0f.
    const double v = static_cast<double>(coordinate) * static_cast<double>(0.0625f);
    const float biased = static_cast<float>((v - static_cast<double>(0.5f)) + static_cast<double>(12582912.0f));
    uint32_t bits;
    std::memcpy(&bits, &biased, sizeof(bits));
    const int32_t rounded = static_cast<int32_t>(bits + 0xB4C00000u);
    const int32_t above = static_cast<int32_t>(bits + 0xB4C00001u);
    return v == static_cast<double>(above) ? above : rounded;
}

// 0x006C9C80 (lot, configuration, unused): the configuration's network
// pieces (type-7 objects with at least three IDs). Each is made by the
// network manager (slot 20) at the object's cell (slot 39 gives its place
// and rotation), its second and third IDs turned left by 8 bits per
// rotation step and the fourth (or 0) passed on; the pieces are then given
// to the manager's slot 21 with the lot and released (occupant manager
// slot 48).
void __fastcall PlaceNetworkPieces(void* self, void*, void* lot, void* configuration, uint32_t)
{
    static bool logged = false;
    FirstCall("PlaceNetworkPieces", logged);
    auto* const objects = Virtual<char**>(configuration, 0x5C);
    ListNode* list = NewNodeList();
    // The game's local for the new piece is never initialised and keeps its
    // value when the manager fails (it writes it only on success): zero at
    // first here.
    void* piece = nullptr;
    uint32_t rotation = 0; // likewise unset before slot 39 writes it
    if (objects) {
        const int32_t count = static_cast<int32_t>(objects[1] - objects[0]) / static_cast<int32_t>(object::kSize);
        for (int32_t i = 0; i < count; ++i) {
            char* const item = objects[0] + i * static_cast<int32_t>(object::kSize);
            if (At<uint8_t>(item, object::kType) != 7)
                continue;
            const float zero = Global<float>(addr::cSC4LotDeveloper_ZeroFloat);
            Vec3 position = {zero, zero, zero};
            Virtual<void>(self, 0x9C, lot, configuration, item, &position, &rotation);
            int32_t cell[2] = {CellOf(position.x), CellOf(position.z)};
            auto* const ids = At<uint32_t*>(item, object::kIds);
            const uint32_t idCount = static_cast<uint32_t>(
                static_cast<int32_t>(reinterpret_cast<char*>(At<uint32_t*>(item, object::kIds + 4)) -
                                     reinterpret_cast<char*>(ids)) >> 2);
            if (idCount < 3)
                continue;
            const uint32_t first = ids[0];
            uint32_t second = ids[1];
            uint32_t third = ids[2];
            if (rotation) {
                const uint32_t left = (rotation << 3) & 31;
                const uint32_t right = (32 - (rotation << 3)) & 31;
                second = (second << left) + (second >> right);
                third = (third << left) + (third >> right);
            }
            const uint32_t fourth = idCount >= 4 ? ids[3] : 0;
            Virtual<uint8_t>(NetworkManager(), 0x50, &piece, first, cell, second, third, fourth, rotation);
            auto* const node = Virtual<ListNode*>(ListNodeAllocator(), 0xC, 0xCu);
            node->value = piece;
            ListNode* const head = list;
            ListNode* const last = head->prev;
            node->next = head;
            node->prev = last;
            last->next = node;
            head->prev = node;
        }
    }
    Virtual<void>(NetworkManager(), 0x54, lot, &list);
    Virtual<void>(At<void*>(self, field::kOccupants), 0xC0, &list);
    ClearAndFreeNodeList(&list);
}

// 0x006C9FB0 (lot): removes the lot's flora (occupant type 0xC772BF98 in
// its cells) that the flora's slot 8 does not report for 0x200000: those it
// reports are taken out of the list first, the rest go through the network
// manager's slot 6 and are released (occupant manager slot 48).
void __fastcall RemoveLotFlora(void* self, void*, void* lot)
{
    static bool logged = false;
    FirstCall("RemoveLotFlora", logged);
    Rect rect;
    Virtual<void>(lot, 0x38, &rect);
    ListNode* list = NewNodeList();
    int32_t maxCell[2] = {rect.x2, rect.z2};
    int32_t minCell[2] = {rect.x1, rect.z1};
    if (Virtual<uint8_t>(At<void*>(self, field::kOccupants), 0xA4, &list, minCell, maxCell, kTypeFlora, 0xFFFFFFFFu)) {
        ListNode* const head = list;
        for (ListNode* node = head->next; node != head;) {
            void* const occupant = node->value;
            void* flora = nullptr;
            Virtual<uint8_t>(occupant, 0x0, kIidFlora, &flora);
            // Not checked for null by the game either.
            if (Virtual<uint8_t>(flora, 0x20, 0x200000u)) {
                node = node->next;
            } else {
                Release(occupant);
                ListNode* const next = node->next;
                ListNode* const previous = node->prev;
                previous->next = next;
                next->prev = previous;
                FreeNode(node);
                node = next;
            }
            if (flora)
                Release(flora);
        }
        Virtual<void>(NetworkManager(), 0x18, &list);
        Virtual<void>(At<void*>(self, field::kOccupants), 0xC0, &list);
    }
    DestroyNodeList(&list);
}

// 0x006CA160: the boundary textures. The key's four hex digits are the
// neighbours' kinds on the four sides (0 none, 1, 2); the value is a texture
// ID with a rotation and a flip.
struct BoundaryTexture {
    int32_t key;
    uint32_t texture;
    uint8_t rotation;
    uint8_t flip;
};

const BoundaryTexture kBoundaryTextures[] = {
    {0x0000, 0x3000, 0, 0}, {0x1000, 0x2000, 0, 0}, {0x0100, 0x2000, 1, 0}, {0x0010, 0x2000, 2, 0},
    {0x0001, 0x2000, 3, 0}, {0x2000, 0x2600, 0, 0}, {0x0200, 0x2600, 1, 0}, {0x0020, 0x2600, 2, 0},
    {0x0002, 0x2600, 3, 0}, {0x1100, 0x2100, 1, 0}, {0x0110, 0x2100, 2, 0}, {0x0011, 0x2100, 3, 0},
    {0x1001, 0x2100, 0, 0}, {0x1200, 0x2400, 1, 0}, {0x0120, 0x2400, 2, 0}, {0x0012, 0x2400, 3, 0},
    {0x2001, 0x2400, 0, 0}, {0x2100, 0x2400, 0, 1}, {0x0210, 0x2400, 1, 1}, {0x0021, 0x2400, 2, 1},
    {0x1002, 0x2400, 3, 1}, {0x1010, 0x2500, 0, 0}, {0x0101, 0x2500, 1, 0}, {0x1020, 0x2900, 2, 0},
    {0x0102, 0x2900, 3, 0}, {0x2010, 0x2900, 0, 0}, {0x0201, 0x2900, 1, 0}, {0x1110, 0x2200, 1, 0},
    {0x0111, 0x2200, 2, 0}, {0x1011, 0x2200, 3, 0}, {0x1101, 0x2200, 0, 0}, {0x1120, 0x2700, 2, 0},
    {0x0112, 0x2700, 3, 0}, {0x2011, 0x2700, 0, 0}, {0x1201, 0x2700, 1, 0}, {0x1210, 0x2800, 1, 0},
    {0x0121, 0x2800, 2, 0}, {0x1012, 0x2800, 3, 0}, {0x2101, 0x2800, 0, 0}, {0x2110, 0x2700, 0, 1},
    {0x0211, 0x2700, 1, 1}, {0x1021, 0x2700, 2, 1}, {0x1102, 0x2700, 3, 1}, {0x1112, 0x2300, 3, 0},
    {0x2111, 0x2300, 0, 0}, {0x1211, 0x2300, 1, 0}, {0x1121, 0x2300, 2, 0},
};

void __fastcall FillBoundaryTextureMap(void* self, void*)
{
    static bool logged = false;
    FirstCall("FillBoundaryTextureMap", logged);
    void* const map = static_cast<char*>(self) + field::kTree;
    for (const BoundaryTexture& entry : kBoundaryTextures) {
        const int32_t key = entry.key;
        uint32_t* const value = reinterpret_cast<MapIndexFn>(addr::SC4Sint32PairMap_Index)(map, &key);
        value[0] = entry.texture;
        // The game copies a dword whose upper two bytes it never wrote
        // (stack memory): zeros here.
        value[1] = static_cast<uint32_t>(entry.rotation) | static_cast<uint32_t>(entry.flip) << 8;
    }
}

// 0x006CAAF0 (lot): the base texture occupants (type 0x497F6D9D) in the
// lot's cells go to the terrain part's 0x006C62F0 (which removes them).
void __fastcall RemoveBaseTextures(void* self, void*, void* lot)
{
    static bool logged = false;
    FirstCall("RemoveBaseTextures", logged);
    ListNode* list = NewNodeList();
    if (lot && At<void*>(self, field::kOccupants)) {
        Rect rect;
        Virtual<void>(lot, 0x38, &rect);
        if (Virtual<uint8_t>(At<void*>(self, field::kOccupants), 0xA4, &list, &rect.x1, &rect.x2, kTypeBaseTexture,
                             0xFFFFFFFFu))
            reinterpret_cast<RemoveListedFn>(addr::cSC4LotDeveloper_Fn_006C62F0)(self, &list);
    }
    DestroyNodeList(&list);
}

// 0x006CABB0 (lot, two unused): the lot's power. A lot without a building,
// or whose slot 65 says so, needs power; it has it with the power cheat or
// when the power simulator says the cell is powered (the middle of the
// building's bounding box, or the lot's cell). The lot's power flag (slots
// 66/67) follows, and its condition (0 to 1 as power goes, 1 to 0 as it
// comes). Without power the count of the lot's property 0xE999B11F goes up
// and the zot 4 (prop 0x0FD10000) is shown: returns 1 while the count is at
// most 2, then 0. With power (or no need) the count is cleared, a kind-4
// zot of a lot with a building goes, and the result is 2.
uint32_t __fastcall CheckPower(void* self, void*, void* lot, uint32_t, uint32_t)
{
    static bool logged = false;
    FirstCall("CheckPower", logged);
    void* const counter = PropertyValueMade(Virtual<void*>(lot, 0xC), 0xE999B11Fu);
    const uint32_t count = *Virtual<uint32_t*>(counter, 0x124);
    void* const building = Virtual<void*>(lot, 0xC0);
    const bool needsPower = !building || Virtual<uint8_t>(lot, 0x104) != 0;
    uint8_t powered = 0;
    Rect rect;
    Virtual<void>(lot, 0x38, &rect);
    if (Virtual<uint8_t>(At<void*>(self, field::kDevelopment), 0xDC) || !needsPower) {
        powered = 1;
    } else {
        uint32_t x = 0;
        uint32_t z = 0;
        if (building) {
            void* const occupant = Virtual<void*>(building, 0xC);
            Vec3 boxA = {1.0f, 1.0f, 1.0f};
            Vec3 boxB = {0.0f, 0.0f, 0.0f};
            Virtual<void>(occupant, 0x28, &boxA, &boxB);
            void* const city = At<void*>(self, field::kCity);
            const float sumX = static_cast<float>(static_cast<double>(boxB.x) + static_cast<double>(boxA.x));
            const double widthX = Virtual<double>(city, 0x16C);
            x = static_cast<uint32_t>(Ftol(static_cast<double>(sumX) / (widthX + widthX)));
            const float sumZ = static_cast<float>(static_cast<double>(boxB.z) + static_cast<double>(boxA.z));
            const double widthZ = Virtual<double>(At<void*>(self, field::kCity), 0x170);
            z = static_cast<uint32_t>(Ftol(static_cast<double>(sumZ) / (widthZ + widthZ)));
        } else {
            Virtual<void>(lot, 0x54, &x, &z);
        }
        if (Virtual<uint8_t>(At<void*>(self, field::kPower), 0x94, x, z))
            powered = 1;
    }
    if (powered != Virtual<uint8_t>(lot, 0x108)) {
        // The game pushes the dword the flag byte is in; its upper bytes are
        // unset stack memory (zeros here).
        Virtual<void>(lot, 0x10C, static_cast<uint32_t>(powered));
        if (!powered) {
            if (Virtual<int32_t>(lot, 0x8C) == 0)
                Virtual<void>(lot, 0x90, 1u);
        } else if (Virtual<int32_t>(lot, 0x8C) == 1) {
            Virtual<void>(lot, 0x90, 0u);
        }
    }
    if (!powered && needsPower) {
        const uint32_t next = count + 1;
        Virtual<void>(counter, 0x68, next);
        ShowZot(self, nullptr, lot, 0x0FD10000u, 4);
        return next > 2 ? 0u : 1u;
    }
    Virtual<void>(counter, 0x68, 0u);
    if (building)
        DropZot(self, lot, 4);
    return 2;
}

// 0x006CAE30 (lot, x, z): the lot's water. A lot in state 2 to 4 needs
// water when its building's profile names no developer (+0 is 0) or when
// its configuration's slot 11 value is above the developer's water supply
// cap (slot 20). Without the plumbing cheat, water at the cell (plumbing
// slot 11) or no need: the count of property 0x8999B20B goes up and the zot
// 5 (prop 0x1C430000) is shown; true (AL) while the count is at most 2.
// Else the count is cleared and a kind-5 zot goes; true.
uint8_t __fastcall CheckWater(void* self, void*, void* lot, uint32_t x, uint32_t z)
{
    static bool logged = false;
    FirstCall("CheckWater", logged);
    void* const counter = PropertyValueMade(Virtual<void*>(lot, 0xC), 0x8999B20Bu);
    const uint32_t count = *Virtual<uint32_t*>(counter, 0x124);
    bool needsWater = false;
    const int32_t state = Virtual<int32_t>(lot, 0x84);
    if (state >= 2 && state <= 4) {
        const uint32_t buildingType = Virtual<uint32_t>(lot, 0xB8, 0u);
        auto* const profile = Virtual<uint32_t*>(At<void*>(self, field::kDevelopment), 0x9C, buildingType);
        if (profile[0] == 0) {
            needsWater = true;
        } else {
            void* const developer = Virtual<void*>(At<void*>(self, field::kDevelopment), 0x84, profile[0], profile[1]);
            void* const configuration = Virtual<void*>(lot, 0x24);
            if (developer && configuration) {
                const uint8_t stage = Virtual<uint8_t>(configuration, 0x2C);
                const uint8_t cap = Virtual<uint8_t>(developer, 0x50);
                if (stage > cap)
                    needsWater = true;
            }
        }
    }
    if (!Virtual<uint8_t>(At<void*>(self, field::kDevelopment), 0xD8) &&
        !Virtual<uint8_t>(At<void*>(self, field::kService2C), 0x2C, x, z) && needsWater) {
        const uint32_t next = count + 1;
        Virtual<void>(counter, 0x68, next);
        ShowZot(self, nullptr, lot, 0x1C430000u, 5);
        return next <= 2 ? 1 : 0;
    }
    Virtual<void>(counter, 0x68, 0u);
    DropZot(self, lot, 5);
    return 1;
}

// 0x006CAFC0 (lot, profile, x, z): the lot's access, for a lot in state 1, 3
// or 4 (any other state is fine). By the profile's first value: 1 wants
// road and job access (lot slots 28, 27) and reads the traffic simulator's
// byte grid (slot 13) at (x, z); 5 to 8 want slot 29 with the value; any
// other wants road access when the lot has travel jobs (slot 63 for 1, 2 or
// 3 above 0; none: fine). Fine: the count of property 0x8999B10B is cleared
// (when the lot has it), a kind-6 zot hidden and a kind-7 zot dropped; true.
// Else the count goes up: without access the zot 6 (prop 0x107A0000) is
// shown and the result is true while the count is at most 2; with access
// but a zero grid byte, a lot in state 3 gets the zot 7 (prop 0x1C440000)
// from the third time (false from the seventh), and before that (or in
// another state) the zots go as when fine.
uint8_t __fastcall CheckAccess(void* self, void*, void* lot, const int32_t* profile, uint32_t x, uint32_t z)
{
    static bool logged = false;
    FirstCall("CheckAccess", logged);
    const int32_t state = Virtual<int32_t>(lot, 0x84);
    bool fine = true;
    bool access = false;
    uint8_t gridValue = 0xFF;
    if (state == 3 || state == 4 || state == 1) {
        const int32_t kind = profile[0];
        bool decided = false;
        if (kind == 1) {
            access = Virtual<uint8_t>(lot, 0x70) && Virtual<uint8_t>(lot, 0x6C);
            void* const grid = Virtual<void*>(At<void*>(self, field::kTraffic), 0x34);
            gridValue = Virtual<uint8_t>(grid, 0x1C, x, z);
        } else if (kind > 4 && kind <= 8) {
            access = Virtual<uint8_t>(lot, 0x74, kind) != 0;
        } else {
            float jobs[4] = {};
            Virtual<void>(lot, 0x100, jobs);
            // 0x00A81054: 0.0f. Each result is compared on the x87 stack.
            if (Virtual<double>(lot, 0xFC, 1u) > 0.0 || Virtual<double>(lot, 0xFC, 2u) > 0.0 ||
                Virtual<double>(lot, 0xFC, 3u) > 0.0)
                access = Virtual<uint8_t>(lot, 0x70) != 0;
            else
                decided = true;
        }
        if (!decided)
            fine = access && gridValue != 0;
    }
    if (fine) {
        void* const property = Virtual<void*>(Virtual<void*>(lot, 0xC), 0x24, 0x8999B10Bu);
        if (property)
            Virtual<void>(Virtual<void*>(property, 0x14), 0x68, 0u);
        HideZot(self, lot, 6);
        DropZot(self, lot, 7);
        return 1;
    }
    void* const counter = PropertyValueMade(Virtual<void*>(lot, 0xC), 0x8999B10Bu);
    const uint32_t next = *Virtual<uint32_t*>(counter, 0x124) + 1;
    Virtual<void>(counter, 0x68, next);
    if (!access) {
        ShowZot(self, nullptr, lot, 0x107A0000u, 6);
        return next > 2 ? 0 : 1;
    }
    if (state == 3) {
        if (next > 6) {
            ShowZot(self, nullptr, lot, 0x1C440000u, 7);
            return 0;
        }
        if (next > 2) {
            ShowZot(self, nullptr, lot, 0x1C440000u, 7);
            return 1;
        }
    }
    HideZot(self, lot, 6);
    DropZot(self, lot, 7);
    return 1;
}

// 0x006CB260 (lot): a lot without a business unit, whose building has one
// of the purposes 5 to 8 (profile +0), joins the unit of a random neighbour
// (lot manager slot 9 for each side 1, 2, 4, 8) of the same purpose that is
// in state 2 or more or in a unit and, for purpose 5, has a building, for
// the others has road access itself or through its travel designate. A
// neighbour without a unit gets a new one first. A purpose-5 lot gets the
// unit's first member as travel designate (lot slot 60); the others set the
// dirty flag +0x5E.
void __fastcall JoinBusinessUnit(void* self, void*, void* lot)
{
    static bool logged = false;
    FirstCall("JoinBusinessUnit", logged);
    uint32_t x = 0;
    uint32_t z = 0;
    Virtual<void>(lot, 0x54, &x, &z);
    if (Virtual<void*>(lot, 0x64))
        return;
    const uint32_t buildingType = Virtual<uint32_t>(lot, 0xB8, 1u);
    if (!buildingType)
        return;
    auto* const profile = Virtual<int32_t*>(At<void*>(self, field::kDevelopment), 0x9C, buildingType);
    if (profile[0] < 5 || profile[0] > 8)
        return;
    ListNode* list = NewStlList();
    for (uint32_t side = 1; side <= 8; side <<= 1)
        Virtual<uint32_t>(At<void*>(self, field::kLots), 0x24, lot, &list, side);
    PointerVector candidates = {nullptr, nullptr, nullptr};
    const auto holdFn = reinterpret_cast<HolderFn>(addr::SC4RefHolder_SC4RefHolder);
    const auto pushFn = reinterpret_cast<VectorPushFn>(addr::SC4RefVector_PushBack);
    for (ListNode* node = list->next; node != list; node = node->next) {
        void* const neighbour = node->value;
        const uint32_t neighbourType = Virtual<uint32_t>(neighbour, 0xB8, 1u);
        if (!neighbourType)
            continue;
        if (Virtual<int32_t>(neighbour, 0x84) <= 1 && !Virtual<void*>(neighbour, 0x64))
            continue;
        auto* const neighbourProfile = Virtual<int32_t*>(At<void*>(self, field::kDevelopment), 0x9C, neighbourType);
        const int32_t purpose = profile[0];
        if (neighbourProfile[0] != purpose)
            continue;
        if (purpose == 5) {
            if (!Virtual<void*>(neighbour, 0xC0))
                continue;
        } else if (!Virtual<uint8_t>(neighbour, 0x70)) {
            void* const designate = Virtual<void*>(neighbour, 0xEC);
            if (!designate || !Virtual<uint8_t>(designate, 0x70))
                continue;
        }
        void* holder = nullptr;
        holdFn(&holder, neighbour);
        pushFn(&candidates, &holder);
        if (holder)
            Release(holder);
    }
    const int32_t count =
        static_cast<int32_t>(reinterpret_cast<char*>(candidates.end) - reinterpret_cast<char*>(candidates.begin)) >> 2;
    if (count > 0) {
        void* const chosen = candidates.begin[Below(static_cast<uint32_t>(count))];
        void* unit = Virtual<void*>(chosen, 0x64);
        if (!unit) {
            void* const memory = reinterpret_cast<AllocFn>(addr::SC4_operator_new)(0x20);
            const auto construct = reinterpret_cast<UnitConstructFn>(addr::cSC4BusinessUnit_cSC4BusinessUnit);
            unit = memory ? construct(memory) : nullptr;
            // Not checked for null by the game either.
            Virtual<void>(unit, 0xC, chosen);
        }
        Virtual<void>(unit, 0xC, lot);
        if (profile[0] == 5) {
            auto* const members = Virtual<ListNode**>(unit, 0x14);
            Virtual<void>(lot, 0xF0, (*members)->next->value);
        } else {
            At<uint8_t>(self, field::kDirty5E) = 1;
        }
    }
    reinterpret_cast<VectorDestroyFn>(addr::SC4RefVector_Destroy)(&candidates);
    reinterpret_cast<ListFn>(addr::SC4RefList_Clear)(&list);
    if (list)
        Free(list);
}

// --- The interface -------------------------------------------------------------

// Slot 13 (0x006C90A0).
void __fastcall EndDemolishLot(void* self, void*, void* lot)
{
    static bool logged = false;
    FirstCall("EndDemolishLot", logged);
    if (!Virtual<uint8_t>(lot, 0x18))
        return;
    Virtual<void>(lot, 0x88, 6u, 0u, 0u);
    ClearLot(self, lot);
    const int32_t zone = Virtual<int32_t>(lot, 0x7C);
    if (zone == 0 || zone == 0xF || zone == 0xB || zone == 0xC)
        Virtual<void>(At<void*>(self, field::kLots), 0x60, lot);
}

// Slot 33 (0x006C7320): the lot's unit is not checked for null. EAX is what
// the unit's slot 4 returned.
uint32_t __fastcall RemoveLotFromItsBusinessUnit(void* self, void*, void* lot)
{
    static bool logged = false;
    FirstCall("RemoveLotFromItsBusinessUnit", logged);
    void* const unit = Virtual<void*>(lot, 0x64);
    const uint32_t result = Virtual<uint32_t>(unit, 0x10, lot);
    At<uint8_t>(self, field::kDirty5E) = 1;
    return result;
}

// Slot 36 (0x006C7A70).
uint8_t __fastcall NeedsAnchorBuilding(void* self, void*, const Rect* rect, int32_t purpose)
{
    static bool logged = false;
    FirstCall("NeedsAnchorBuilding", logged);
    if (purpose < 6 || purpose > 8)
        return 0;
    ListNode* list = NewStlList();
    void* const lots = At<void*>(self, field::kLots);
    Virtual<uint32_t>(lots, 0x1C, &list, rect->x1, rect->z1, rect->x2, rect->z2, 0u);
    const auto clear = reinterpret_cast<ListFn>(addr::SC4RefList_Clear);
    for (ListNode* node = list->next; node != list; node = node->next) {
        void* const building = Virtual<void*>(node->value, 0xC0);
        if (building && Virtual<uint8_t>(Virtual<void*>(building, 0xC), 0x58, 0x3000u)) {
            clear(&list);
            if (list)
                Free(list);
            return 1;
        }
    }
    clear(&list);
    for (uint32_t side = 1; side <= 8; side <<= 1)
        Virtual<uint32_t>(At<void*>(self, field::kLots), 0x28, rect, &list, side);
    for (ListNode* node = list->next; node != list; node = node->next) {
        void* const lot = node->value;
        const uint32_t buildingType = Virtual<uint32_t>(lot, 0xB8, 1u);
        if (!buildingType)
            continue;
        if (Virtual<int32_t>(lot, 0x84) <= 1 && !Virtual<void*>(lot, 0x64))
            continue;
        auto* const profile = Virtual<int32_t*>(At<void*>(self, field::kDevelopment), 0x9C, buildingType);
        if (profile[0] == purpose) {
            clear(&list);
            if (list)
                Free(list);
            return 0;
        }
    }
    ReleaseStlList(list);
    return 1;
}

// Slot 38 (0x006C9100).
void* __fastcall GetFirstBuilding(void* self, void*, void* lot)
{
    static bool logged = false;
    FirstCall("GetFirstBuilding", logged);
    void* building = nullptr;
    Rect rect;
    Virtual<void>(lot, 0x38, &rect);
    ListNode* list = NewNodeList();
    void* const occupants = At<void*>(self, field::kOccupants);
    if (Virtual<uint8_t>(occupants, 0xA4, &list, &rect.x1, &rect.x2, kTypeBuilding, 1u)) {
        ListNode* const first = list->next;
        if (first != list) {
            if (Virtual<uint8_t>(first->value, 0x0, kIidBuilding, &building))
                Release(building);
            Virtual<void>(At<void*>(self, field::kOccupants), 0xC0, &list);
        }
    }
    DestroyNodeList(&list);
    return building;
}

// Slot 44 (0x006CA8A0).
void __fastcall SetNormalLotBaseTexture(void* self, void*, void* lot, void* configuration, uint32_t)
{
    static bool logged = false;
    FirstCall("SetNormalLotBaseTexture", logged);
    if (!lot || !configuration)
        return;
    if (At<uint8_t>(self, field::kForceEmptyTextures)) {
        // The jump table at 0x006CAADC: zone types 0, 11, 12 and 15 keep
        // their textures.
        const uint32_t zone = Virtual<uint32_t>(lot, 0x7C);
        if (!(zone == 0 || zone == 11 || zone == 12 || zone == 15)) {
            Virtual<void>(self, 0xAC, lot);
            return;
        }
    }
    TextureVector entries = {nullptr, nullptr, nullptr};
    Rect rect;
    Virtual<void>(lot, 0x38, &rect);
    auto* const objects = Virtual<char**>(configuration, 0x5C);
    if (!objects)
        return;
    const int32_t count = static_cast<int32_t>(objects[1] - objects[0]) / static_cast<int32_t>(object::kSize);
    if (count <= 0)
        return;
    for (int32_t i = 0; i < count; ++i) {
        char* const item = objects[0] + i * static_cast<int32_t>(object::kSize);
        if (At<uint8_t>(item, object::kType) != 2)
            continue;
        const float zero = Global<float>(addr::cSC4LotDeveloper_ZeroFloat);
        Vec3 position = {zero, zero, zero};
        uint32_t rotation = 0; // unset in the game before slot 39
        Virtual<void>(self, 0x9C, lot, configuration, item, &position, &rotation);
        uint32_t texture = 0;
        void* const ids = item + object::kIds;
        auto* const idsBegin = At<uint32_t*>(item, object::kIds);
        if (idsBegin == At<uint32_t*>(item, object::kIds + 4))
            continue;
        if (*idsBegin != 0) {
            texture = reinterpret_cast<PickTextureFn>(addr::cSC4LotDeveloper_Fn_006C60C0)(self, lot, ids);
            if (!texture)
                continue;
        }
        // The entry's constructor 0x005CA770; its last two bytes stay unset
        // (stack memory in the game, zeros here).
        TextureEntry entry = {};
        std::memset(entry.ff, 0xFF, sizeof(entry.ff));
        entry.texture = texture;
        entry.x = static_cast<uint8_t>(Ftol(static_cast<double>(position.x) * static_cast<double>(0.0625f)));
        entry.z = static_cast<uint8_t>(Ftol(static_cast<double>(position.z) * static_cast<double>(0.0625f)));
        entry.rotation = static_cast<uint8_t>(rotation);
        entry.flag = At<uint8_t>(item, object::kByte3);
        if (static_cast<uint32_t>(entry.x) >= At<uint32_t>(self, field::kCellsX))
            continue;
        if (static_cast<uint32_t>(entry.z) >= At<uint32_t>(self, field::kCellsZ))
            continue;
        reinterpret_cast<TexturePushFn>(addr::SC4LaneEntryVector_PushBack)(&entries, &entry);
    }
    if (entries.begin != entries.end) {
        void* textures = configuration; // the game reuses the argument's slot
        if (Virtual<uint8_t>(self, 0xA4, lot, &textures, 1u)) {
            const int32_t entryCount = static_cast<int32_t>(reinterpret_cast<char*>(entries.end) -
                                                            reinterpret_cast<char*>(entries.begin)) >> 4;
            Virtual<void>(textures, 0x20, entries.begin, entryCount, 0u);
            void* const occupant = Virtual<void*>(textures, 0xC);
            const uint8_t placed = Virtual<uint8_t>(occupant, 0x80, 1u);
            void* const occupants = At<void*>(self, field::kOccupants);
            if (placed) {
                Virtual<void>(occupants, 0x60, occupant, 0u);
                Virtual<void>(occupant, 0x3C, 0x99EF1145u, 0u);
            } else {
                Virtual<void>(occupants, 0x50, occupant, 0u);
            }
            Release(textures);
        }
    }
    if (entries.begin)
        Free(entries.begin);
}

// Slot 47 (0x006C77D0).
void __fastcall SetForceEmptyLotBaseTextures(void* self, void*, uint32_t forceArgument)
{
    static bool logged = false;
    FirstCall("SetForceEmptyLotBaseTextures", logged);
    const uint8_t force = static_cast<uint8_t>(forceArgument);
    if (force == At<uint8_t>(self, field::kForceEmptyTextures))
        return;
    At<uint8_t>(self, field::kForceEmptyTextures) = force;
    auto* const iterator = Virtual<LotIterator*>(Global<void*>(addr::SC4_LotManager), 0x3C);
    while (iterator->list && iterator->cur != *iterator->list) {
        ListNode* const node = iterator->cur;
        iterator->cur = node->next;
        void* const lot = node->value;
        if (At<uint8_t>(self, field::kForceEmptyTextures))
            Virtual<void>(self, 0xAC, lot);
        else
            Virtual<void>(self, 0xB0, lot, Virtual<void*>(lot, 0x24), 1u);
    }
    if (iterator->list) {
        LotIterator* const own = iterator;
        reinterpret_cast<IteratorFreeFn>(addr::SC4ListIterator_FreeList)(iterator->list + 1, &own);
    }
    reinterpret_cast<FreeFn>(addr::SC4_operator_delete)(iterator);
}

// The loop the two foundation queries share: the first foundation occupant
// whose slot 9 value is `kind`; the occupants are released as they are
// passed.
void* FindFoundation(ListNode* list, uint32_t kind)
{
    void* found = nullptr;
    for (ListNode* node = list->next; node != list; node = node->next) {
        void* const occupant = node->value;
        if (!found) {
            void* foundation = nullptr;
            if (Virtual<uint8_t>(occupant, 0x0, kIidFoundation, &foundation)) {
                if (Virtual<uint32_t>(foundation, 0x24) == kind)
                    found = foundation;
                Release(foundation);
            }
        }
        Release(occupant);
    }
    return found;
}

// Slot 49 (0x006C9480).
void* __fastcall GetFoundationByStandardCityCell(void* self, void*, uint32_t kind, uint32_t x, uint32_t z)
{
    static bool logged = false;
    FirstCall("GetFoundationByStandardCityCell", logged);
    uint32_t minCell[2] = {x, z};
    uint32_t maxCell[2] = {x, z};
    void* found = nullptr;
    ListNode* list = NewNodeList();
    void* const occupants = At<void*>(self, field::kOccupants);
    if (Virtual<uint8_t>(occupants, 0xA4, &list, minCell, maxCell, kTypeFoundation, 0xFFFFFFFFu))
        found = FindFoundation(list, kind);
    ClearAndFreeNodeList(&list);
    return found;
}

// Slot 50 (0x006C95E0). Both corners of the box get x - 0.05 and z + 0.05
// (the game's doing); only the heights differ.
void* __fastcall GetFoundationByPosition(void* self, void*, uint32_t kind, float x, float z)
{
    static bool logged = false;
    FirstCall("GetFoundationByPosition", logged);
    void* found = nullptr;
    ListNode* list = NewNodeList();
    const double margin = static_cast<double>(0.05f); // 0x00A880D8
    const float boxX = static_cast<float>(static_cast<double>(x) - margin);
    const float boxZ = static_cast<float>(static_cast<double>(z) + margin);
    uint32_t highest = 0x7F7FFFFF;
    uint32_t lowest = 0xFF7FFFFF;
    Vec3 high = {boxX, 0.0f, boxZ};
    Vec3 low = {boxX, 0.0f, boxZ};
    std::memcpy(&high.y, &highest, sizeof(highest));
    std::memcpy(&low.y, &lowest, sizeof(lowest));
    if (!At<void*>(self, field::kOccupants)) {
        At<void*>(self, field::kOccupants) = Global<void*>(addr::SC4_NetworkService);
        Virtual<uint32_t>(At<void*>(self, field::kOccupants), 0x4);
    }
    if (Virtual<uint8_t>(At<void*>(self, field::kOccupants), 0x94, &list, &low, &high, kTypeFoundation, 0xFFFFFFFFu))
        found = FindFoundation(list, kind);
    ClearAndFreeNodeList(&list);
    return found;
}

// Slot 55 (0x006C8AB0). (lot, foundation type, keep the lot's altitude,
// level the ground, update the neighbours.)
uint8_t __fastcall AddLotFoundation(void* self, void*, void* lot, uint32_t type, uint32_t keepArgument,
                                    uint32_t levelArgument, uint32_t neighboursArgument)
{
    static bool logged = false;
    FirstCall("AddLotFoundation", logged);
    Vec3 centre;
    Virtual<void>(lot, 0x3C, &centre);
    Rect rect;
    Virtual<void>(lot, 0x38, &rect);
    const uint32_t facing = Virtual<uint32_t>(lot, 0x5C);
    const float width = static_cast<float>((rect.x2 - rect.x1 + 1) << 4);
    const float depth = static_cast<float>((rect.z2 - rect.z1 + 1) << 4);
    const float lowest = LowestNeighbourAltitude(self, lot);
    const float highest = Virtual<float>(self, 0x44, &rect, 0u, 1u);
    float altitude;
    if (static_cast<uint8_t>(keepArgument)) {
        altitude = Virtual<float>(lot, 0x40);
    } else {
        float corners[4] = {};
        reinterpret_cast<CornerAltitudesFn>(addr::cSC4LotDeveloper_Fn_006C4360)(self, &rect, corners, 1);
        const auto foundationAltitude = reinterpret_cast<FoundationAltitudeFn>(addr::cSC4LotDeveloper_Fn_006C5360);
        altitude = foundationAltitude(self, lot, lowest, highest, corners);
    }
    centre.y = altitude;
    if (static_cast<uint8_t>(levelArgument)) {
        if (rect.x2 - rect.x1 > 1 && rect.z2 - rect.z1 > 1) {
            // 0x00A84D2C: 0.5f.
            const float inner = static_cast<float>(static_cast<double>(altitude) - static_cast<double>(0.5f));
            Virtual<void>(self, 0x4C, &rect, inner, 1u, 1u, lot);
        }
        Virtual<void>(self, 0x50, &rect, altitude, 1u, lot);
    }
    const uint8_t result =
        Virtual<uint8_t>(At<void*>(self, field::kDevelopment), 0x5C, type, 2u, &centre, width, depth, facing, lowest);
    Virtual<void>(lot, 0x44, altitude);
    Virtual<void>(lot, 0x34, (Virtual<uint32_t>(lot, 0x30) & 0xFFFFFFFBu) | 3u);
    if (static_cast<uint8_t>(neighboursArgument) && altitude < highest) {
        reinterpret_cast<LotHelper2Fn>(addr::SC4_LotHelper_005E3070)(lot, 1);
        UpdateNeighbourFoundations(self, lot);
        reinterpret_cast<LotHelperFn>(addr::SC4_LotHelper_005E3960)(lot);
    }
    return result;
}

// --- Installation --------------------------------------------------------------

struct Slot {
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(index, original, fn) {index, original, reinterpret_cast<void*>(&fn)}

const Slot kSlots[] = {
    OPENSC4_SLOT(13, 0x006C90A0, EndDemolishLot),
    OPENSC4_SLOT(33, 0x006C7320, RemoveLotFromItsBusinessUnit),
    OPENSC4_SLOT(36, 0x006C7A70, NeedsAnchorBuilding),
    OPENSC4_SLOT(38, 0x006C9100, GetFirstBuilding),
    OPENSC4_SLOT(44, 0x006CA8A0, SetNormalLotBaseTexture),
    OPENSC4_SLOT(47, 0x006C77D0, SetForceEmptyLotBaseTextures),
    OPENSC4_SLOT(49, 0x006C9480, GetFoundationByStandardCityCell),
    OPENSC4_SLOT(50, 0x006C95E0, GetFoundationByPosition),
    OPENSC4_SLOT(55, 0x006C8AB0, AddLotFoundation),
};

#undef OPENSC4_SLOT

// The helpers other parts and the game's remaining code call directly. Call
// sites checked for values kept in EAX, ECX or EDX across the call (none
// does; every argument is pushed):
//   0x006C7860  0x006CDEBD (DoMessage)
//   0x006C78D0  0x006CD5CB (UpdateLot)
//   0x006C8830  0x006CDD00 (DoMessage)
//   0x006C8E10  0x006CBE20, 0x006CBEBC (the night steps)
//   0x006C8F80  0x006CDC28 (DoMessage)
//   0x006C91F0  0x006CC947 (UpdateLotElements)
//   0x006C9780  0x006CB97D, 0x006CC160, 0x006CC55B
//   0x006C98C0  0x006CC125
//   0x006C9AD0  0x006CC6E1 (StartConstruction)
//   0x006C9C80  0x006CDED3 (DoMessage)
//   0x006C9FB0  0x006CC234, 0x006CC373
//   0x006CA160  0x006CBD18 (Init)
//   0x006CAAF0  0x006CC99F (SetEmptyLotBaseTexture)
//   0x006CABB0  0x006CD45A, 0x006CD511, 0x006CD678 (UpdateLot)
//   0x006CAE30  0x006CD531
//   0x006CAFC0  0x006CD4C8, 0x006CD556, 0x006CD64A
//   0x006CB260  0x006CB697, 0x006CC4E3, 0x006CD722
struct Detour {
    std::uintptr_t address;
    void* ours;
    void* original;
};

#define OPENSC4_DETOUR(address, fn) {address, reinterpret_cast<void*>(&fn), nullptr}

Detour g_detours[] = {
    OPENSC4_DETOUR(addr::cSC4LotDeveloper_Fn_006C7860, CreateConfigurationProps),
    OPENSC4_DETOUR(addr::cSC4LotDeveloper_Fn_006C78D0, TryPlaceSpecialLot),
    OPENSC4_DETOUR(addr::cSC4LotDeveloper_Fn_006C8830, RemoveNightBuilding),
    OPENSC4_DETOUR(addr::cSC4LotDeveloper_Fn_006C8E10, LightBuilding),
    OPENSC4_DETOUR(addr::cSC4LotDeveloper_Fn_006C8F80, AddNightBuilding),
    OPENSC4_DETOUR(addr::cSC4LotDeveloper_Fn_006C91F0, SettleLotOccupants),
    OPENSC4_DETOUR(addr::cSC4LotDeveloper_Fn_006C9780, RemoveZotPropsDetour),
    OPENSC4_DETOUR(addr::cSC4LotDeveloper_Fn_006C98C0, ShowZot),
    OPENSC4_DETOUR(addr::cSC4LotDeveloper_Fn_006C9AD0, DemolishFilteredOccupants),
    OPENSC4_DETOUR(addr::cSC4LotDeveloper_Fn_006C9C80, PlaceNetworkPieces),
    OPENSC4_DETOUR(addr::cSC4LotDeveloper_Fn_006C9FB0, RemoveLotFlora),
    OPENSC4_DETOUR(addr::cSC4LotDeveloper_Fn_006CA160, FillBoundaryTextureMap),
    OPENSC4_DETOUR(addr::cSC4LotDeveloper_Fn_006CAAF0, RemoveBaseTextures),
    OPENSC4_DETOUR(addr::cSC4LotDeveloper_Fn_006CABB0, CheckPower),
    OPENSC4_DETOUR(addr::cSC4LotDeveloper_Fn_006CAE30, CheckWater),
    OPENSC4_DETOUR(addr::cSC4LotDeveloper_Fn_006CAFC0, CheckAccess),
    OPENSC4_DETOUR(addr::cSC4LotDeveloper_Fn_006CB260, JoinBusinessUnit),
};

#undef OPENSC4_DETOUR

// Checks that every slot still holds the original, installs the detours,
// then swaps the slots.
bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    auto* const vtable = reinterpret_cast<std::uintptr_t*>(addr::cSC4LotDeveloper_vftable);
    for (const Slot& slot : kSlots) {
        if (vtable[slot.index] != slot.original) {
            log::Write("lot developer build: slot %d of %08X holds %08X, expected %08X; not installing", slot.index,
                       static_cast<unsigned>(addr::cSC4LotDeveloper_vftable), static_cast<unsigned>(vtable[slot.index]),
                       static_cast<unsigned>(slot.original));
            return false;
        }
    }
    for (Detour& detour : g_detours) {
        const addr::Entry* entry = addr_check::Find(detour.address);
        if (!entry || !hook::Install(*entry, detour.ours, &detour.original)) {
            log::Write("lot developer build: no detour at %08X", static_cast<unsigned>(detour.address));
            return false;
        }
    }
    for (const Slot& slot : kSlots) {
        std::uintptr_t* const entry = vtable + slot.index;
        DWORD old = 0;
        if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &old))
            return false;
        *entry = reinterpret_cast<std::uintptr_t>(slot.ours);
        VirtualProtect(entry, sizeof(*entry), old, &old);
    }
    log::Write("lot developer build: %u slots and %u detours are ours", static_cast<unsigned>(std::size(kSlots)),
               static_cast<unsigned>(std::size(g_detours)));
    return true;
}

const bool g_registered = registry::Register(
    {"simulators.lot.build", "simulators/lot", registry::Kind::Vtable, registry::Phase::PostAppInit,
     addr::cSC4LotDeveloper_AddLotFoundation_006C8AB0,
     "Our cSC4LotDeveloper build part: business units, lot foundations, the buildings of the day/night step, the "
     "end of a demolition, the zots (power, water, access), base textures and network pieces of a lot",
     false, Install});

} // namespace
} // namespace opensc4::lot_sim
