// Our cSC4AutomataControllerManager, part a2 (build 641): the queries (the
// three GetController overloads, GetRelevantControllersByWorldPos,
// ReleaseControllerList, the composite attraction effect and commotion),
// the controller grid's add, remove and move and the cell helpers they use,
// AddControllersFromGroup, and the message side: the cheat code target, the
// message handler and its helpers (the controller time, the frame update,
// the templates after a load, the move notifications). The class map is in
// src/core/automata_ctlmgr.h, part a1 in src/core/automata_ctlmgr.cpp.
// Calls between functions of the class go to the game's addresses, so each
// detour stands alone and OPENSC4_AUTOMATA_SKIP (function addresses, hex,
// eight digits; shared with part a1) bisects them.
//
// Groups, off by default:
//   simulators.automata.ctlquery (15 detours): 0x00514CE0
//   GetControllerByName (slot 19), 0x005173A0 GetControllerByTemplate (slot
//   18), 0x00516410 GetControllerNear (slot 17), 0x005179C0
//   GetRelevantControllersByWorldPos (slot 20), 0x00516800
//   ReleaseControllerList (slot 21), 0x00516530
//   GetCompositeAttractionEffect (slot 22), 0x00516720
//   GetCompositeAttractionCommotion (slot 23), the grid's 0x00517A90 GridAdd,
//   0x005171D0 GridRemove, 0x00517B70 GridMove, 0x005163D0
//   AddControllersFromGroup, and the private helpers 0x00515290
//   GridControllerRange, 0x00515660 GridCellAt, 0x00515180 (a controller's)
//   AttractionStrength, 0x00515DA0 (a controller's) IsRelevantTo.
//   simulators.automata.ctlmsg (9 detours): 0x005187F0 OnCheat (+4 part slot
//   3), 0x00519360 DoMessage (+8 part slot 3), 0x00518660 ResolveTemplates,
//   0x00518950 MoveFlaggedControllers, 0x00518B80 Update, 0x005177F0
//   NotifyControllersMoved, and the controller time's 0x00514FC0 Normalize,
//   0x00515100 SubtractHours, 0x00515D50 Difference.
//
// What it does. The grid keeps one list of controllers per 128-metre cell.
// GridAdd puts a controller on the cells of its bounds (controller slot 21,
// through GridControllerRange, kept in the controller's slot 34 bytes),
// GridMove moves it from the kept range to the new one, GridRemove takes it
// off every cell. GridCellAt answers the cell list at a point; the point
// queries look at that one cell only. GridAdd and GridMove index a cell
// (z << shift) + x, as GridCellAt does; part a1's AddControllerFromTemplate
// (the game's 0x0051AD20) indexes (x << shift) + z for its radius test: the
// game's own mix-up (the grid is square, so it only looks at the mirrored
// cells), kept on both sides.
// The message handler: occupant inserted (0x99EF1142) AddControllers (slot
// 13) and removed (0x99EF1143) RemoveControllers (slot 14), each unless the
// occupant's property holder has 0x029244DB, the removal of a network
// occupant also clears every automaton entry's flag; 0x26D31EC1 (after a
// load) ResolveTemplates; 0x86AD10EE (the frame message) Update; 0x44778230
// one hour on (tControllerTime::AddHours, the game's) and each controller's
// slot 24 with the time and the time passed; 0x09D92FC3 (an automaton
// event; with code 3) each controller's slot 31; 0x89D3E9F3 (a network
// piece's usable flag) clears every automaton entry's flag; 0xAA375FE8 and
// 0xAA1D07F3 each controller's slots 25 / 26; 0xA6B79602 and 0xEAD61E16
// (and the camera point moved far enough, 0xA6B79612, unless 0xCAD61E00
// held it) each controller's slot 27; 0xEBFF18A3 (an occupant group event)
// the group's controllers for the occupant when a fresh clock-seeded draw
// is at most the group's +0x2C. Update takes the hour from the 24-hour
// clock, and while +0x55 is set and the vehicle or pedestrian manager says
// so (their slot 6) moves flagged controllers, ticks every controller and
// removes finished ones (as RemoveController: message 0xEA298D50, off the
// grid, shut down, released).
//
// Oddities kept: ResolveTemplates loops for ever on a null controller in a
// list (it does not step on); the composite attraction's direction has a y
// of 0 * (1 / length) (a NaN at length 0, as the game); Commotion zeroes
// the position's y after reading it; GridControllerRange does not clamp the
// low cells (below 0 they wrap as bytes); GridCellAt's x below 0 becomes the
// integer 0, its z the float 0; GridAdd and GridMove loop for ever when a
// range reaches cell 255 (byte counters), as the game; GridMove appends to
// a new cell without looking whether the controller is there already (it
// is not: the cell was outside the old range). The composite attraction
// calls the controller's slot 8 and drops the answer; DoMessage 0xAA375FE8
// asks the message's slots 9 and 14 and drops the answers. GetControllerNear
// and the others read a null name's pointers (a crash, as the game).
//
// Still the game's, at their addresses: tControllerTime::AddHours 0x00515080
// (shared: the generator controller's 0x00515200 and 0x00515E60 call it);
// SC4HashTable::Begin 0x004B0390 and NextBucket 0x0053DCF0 (Update and
// DoMessage call it where a chain ends; the other walks are the game's
// inline ones); SC4::DistanceSquared3 0x005D6F90; SC4Curve::Evaluate
// 0x005F4250; SC4Random::Construct 0x00913A5C and cRZRandom::NextFloat
// 0x0091372E; SC4Tokenizer 0x00914E14 / 0x00913E8A / 0x00419490 and its
// slot 16; SC4String::Equals 0x00418A40; cRZBaseString::Sprintf 0x0090F574;
// cGZMessage 0x009134D6 through RZ::operator_new 0x009133DA; the part a1
// functions (called at their addresses).
//
// Registers. The direct callers were scanned (scratch regscan.py /
// eaxscan.py): 0x00514FC0 from 0x00518C10; 0x00515100 from 0x00515D6E,
// 0x00518BF3; 0x00515180 from 0x00516628, 0x005167CF; 0x00515290 from
// 0x00517AB1, 0x00517B95; 0x00515660 from 0x00516423, 0x005165A3,
// 0x00516783, 0x005179E3; 0x00515D50 from 0x00519415; 0x00515DA0 from
// 0x0051660C; 0x005163D0 from 0x005170CA, 0x00517177, 0x005171B7,
// 0x00519875; 0x005171D0 from 0x0051794C, 0x00518D64, 0x0051A43C,
// 0x0051A638; 0x005177F0 from 0x005197D5, 0x0051988C; 0x00517A90 from
// 0x00518779, 0x0051B13D; 0x00517B70 from 0x0051890B, 0x0051893D,
// 0x00518998; 0x00518660 from 0x0051945C; 0x00518950 from 0x00518C68;
// 0x00518B80 from 0x00519509. The scanners' flags are partial writes (mov
// cl/dl/dx/ax after the call), not reads; EAX is read only where the
// function answers in it (0x00515660, 0x00515D50, 0x00515DA0). The x87
// stack is empty at every call; 0x00515180's answer is used unrounded by
// Commotion (fabs, fadd), so ours answers a double in ST0. No function
// reads a register beyond ECX at entry. Everything else is called through
// vtables. No naked thunks.
//
// Floats. All at the x87's 53-bit precision, which doubles repeat (one
// rounding per operation, no value near the exponent limits; products of
// two floats are exact in a double). Compares follow the asm's flag tests:
// `test ah,5 / jp` = below only, `test ah,0x41 / jnp` = below or equal,
// `test ah,0x41 / jne` skips unless above, `test ah,1 / jne` skips below or
// unordered (so `>=`), `test ah,0x44 / jp` = equal only. Cell numbers are
// part a1's: (a - 0.5) + 12582912.0 stored as a float, its bits plus
// 0xB4C00000, one more when a equals that as a double. Distances: sqrt of
// the squares' sum in the asm's order (correctly rounded both ways). The
// composite effect: the strength rounded to a float; the direction (dx, 0,
// dz) times K / length (K the float 1 at 0x00B09A14) stored as floats; x and
// y times the strength added to the vector unrounded, z rounded to a float
// first (the asm parks it in memory); the strongest by |strength| (above
// only). Commotion sums |strength| unrounded into a float. The controller
// time: hour + minute * (1/60) from the clock's unsigned answers (2^32 added
// below 0); the hour loops add or take 24 in double until in range (the
// value stays on the x87 stack through the loop). Answers in ST0 from game
// functions (slot 9, SC4Curve::Evaluate, SC4::DistanceSquared3,
// cRZRandom::NextFloat) are taken as doubles (exact: a double holds any
// 53-bit x87 value). Floats that the game only moves (positions, the camera
// point, the strength and value written out) go as their bits.
//
// Stack junk. GridControllerRange hands slot 21 two vectors it never sets;
// slot 21 (0x00515BD0, the same in all three controller classes) fills the
// x and z of both from slot 20 (0x00516EC0: the occupant's slot 10 or the
// position twice) before reading them: ours start them at 0, never read
// otherwise. The composite attraction's value slot is set by slot 23 when
// it answers true and read only then (ours starts at 0 and keeps its last
// value from one controller to the next, as the game's stack slot does).
// Update's time passed and DoMessage 0x44778230's difference are copied as
// float + 16-bit word into a stack pair whose upper half the game leaves as
// it was; the controllers' slots 24 and 38 (0x00515200, 0x006F7DC0,
// 0x00514D90, 0x00515E60) read the float and the word only.
//
// Calls, each count from the asm's pushes and the callee's own `ret N`:
// a controller (attractor 0x00A95E48, generator 0x00A95F00, base
// 0x00A95D90, the same counts) slots 1, 2, 3, 4, 5, 7, 8, 9 (ST0), 10, 25,
// 26, 27, 33, 34, 35, 36, 37 none, 11, 12, 15, 22, 31 one, 21, 23, 24 two,
// 38 three; the script system slots 0x2C (ID) and 0x44 (name) one; an
// occupant slot 0 (QI: IID, out) two, 6 (holder) none, 8 (position out)
// one, 22 IsOccupantGroup (ID) one; a property holder slot 3 (ID) one; an
// automaton (0x00A94608 ...) slot 56 none; a network occupant (0x00AA8080)
// slot 48 none; a name (cRZBaseString) slot 5 (ToChar) none; a message
// (0x00ACEC00) slots 4, 9, 10, 14, 19, 24, 32 none; a cheat's text slot 5
// none; the tokenizer (vtable 0x00A82A20) slot 16 (index) one; the 24-hour
// clock (0x00A86588) slots 10 GetHour and 13 GetMinute none; the city slots
// 43 GetVehicleManager and 44 GetPedestrianManager none, their objects'
// slot 6 none; the manager's own slots 6 (occupant, template, position),
// 13 / 14 (occupant), 18 (occupant, template) through its vtable; a new
// cGZMessage slots 1, 2 none, 5 (type), 12 (controller) one; the message
// server (0x00B43CCC) slot 3 (message) one. Direct: the part's own
// 0x00515660 (x, z) ret 8, 0x00515290 (controller, out) ret 8, 0x00515180
// (time, value) ret 8, 0x00515DA0 (occupant) ret 4, 0x00515100 (hours) ret
// 4, 0x00515D50 (out, other) ret 8, 0x00514FC0 none, 0x00518950 none,
// 0x00518B80 (a, b) ret 8, 0x00518660 none, 0x005177F0 none, 0x005163D0
// (group, occupant, position) ret 0xC, 0x00517A90 / 0x005171D0 / 0x00517B70
// (controller) ret 4; tControllerTime::AddHours (hours) ret 4;
// SC4HashTable::Begin (out) ret 4, NextBucket none; SC4Curve::Evaluate (x)
// ret 4; SC4Random::Construct (seed) ret 4; cRZRandom::NextFloat none;
// SC4Tokenizer::SC4Tokenizer (text) ret 4, Count none, Destroy none;
// cGZMessage::cGZMessage none. cdecl: SC4::stl_allocate (size), SC4::Free
// (pointer), RZ::operator_new (size), SC4::DistanceSquared3 (a, b),
// SC4String::Equals (string, text), cRZBaseString::Sprintf (string, format,
// text).
#include "core/automata_ctlmgr.h"

#include "core/address_check.h"
#include "core/fixed_seed.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace opensc4::automata_ctlmgr {
namespace {

// ====================================================================================
// Helpers (part a1's, file-local there)

template <typename R, typename... Args>
inline R CallThis(std::uintptr_t address, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(address)(self, args...);
}

template <typename R, typename... Args>
inline R CallCdecl(std::uintptr_t address, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(address)(args...);
}

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("automata: first %s", name);
}

void* City() { return Global<void*>(addr::SC4_City); }

void* Allocate(uint32_t size) { return CallCdecl<void*>(addr::SC4_stl_allocate, size); }
void Free(void* pointer) { CallCdecl<void>(addr::SC4_Free, pointer); }

// A node appended to a list, the game's order of writes.
void PushBack(ListNode* head, void* value)
{
    auto* const node = static_cast<ListNode*>(Allocate(0xC));
    node->value = value; // the game tests node + 8 against 0 first (never 0)
    ListNode* const prev = head->prev;
    node->next = head;
    node->prev = prev;
    prev->next = node;
    head->prev = node;
}

// A node taken out of its list and freed.
ListNode* Unlink(ListNode* node)
{
    ListNode* const next = node->next;
    ListNode* const prev = node->prev;
    prev->next = next;
    next->prev = prev;
    Free(node);
    return next;
}

uint32_t Buckets(const HashTable* table)
{
    const int32_t bytes = static_cast<int32_t>(reinterpret_cast<const char*>(table->end) -
                                               reinterpret_cast<const char*>(table->begin));
    return static_cast<uint32_t>(bytes >> 2);
}

// The next node, the game's inline increment: the chain, else the next
// bucket after the key's.
template <typename Node>
Node* NextNode(const Node* node, const HashTable* table)
{
    if (node->next)
        return node->next;
    const uint32_t buckets = Buckets(table);
    uint32_t bucket = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(node->occupant)) % buckets;
    for (++bucket; bucket < buckets; ++bucket)
        if (table->begin[bucket])
            return static_cast<Node*>(table->begin[bucket]);
    return nullptr;
}

// The node of a key, the game's inline lookup.
template <typename Node>
Node* FindNode(const HashTable* table, const void* key)
{
    const uint32_t bucket = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(key)) % Buckets(table);
    for (auto* node = static_cast<Node*>(table->begin[bucket]); node; node = node->next)
        if (node->occupant == key)
            return node;
    return nullptr;
}

HashTable* Controllers(char* self) { return reinterpret_cast<HashTable*>(self + field::kControllers); }
HashTable* Automata(char* self) { return reinterpret_cast<HashTable*>(self + field::kAutomata); }

void Begin(HashTable* table, Iterator* out) { CallThis<Iterator*>(addr::SC4HashTable_Begin, table, out); }

// The next node the way Update and DoMessage step: the chain, else
// SC4HashTable::NextBucket on the iterator (both node kinds keep the chain
// at +0).
void* Advance(Iterator* it)
{
    void* const next = *static_cast<void**>(it->node);
    it->node = next ? next : CallThis<void*>(addr::SC4HashTable_NextBucket, it);
    return it->node;
}

// A string of the game (cRZBaseString, 0x14 bytes).
struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t references;
};

// The message 0xEA298D50 (a controller goes) with the controller.
constexpr uint32_t kControllerRemoved = 0xEA298D50;
void SendRemoved(void* server, void* controller)
{
    void* const memory = CallCdecl<void*>(addr::RZ_operator_new, 0x2Cu);
    void* const message = memory ? CallThis<void*>(addr::cGZMessage_cGZMessage, memory) : nullptr;
    Virtual<uint32_t>(message, 0x4);
    Virtual<uint32_t>(message, 0x14, kControllerRemoved);
    Virtual<uint32_t>(message, 0x30, controller);
    Virtual<uint8_t>(server, 0xC, message);
    Virtual<uint32_t>(message, 0x8);
}

Grid* GridOf(char* self) { return reinterpret_cast<Grid*>(self + field::kGrid); }

// A coordinate to its cell number, the game's rounding (part a1's CellOf).
uint32_t CellOf(double scaled)
{
    const double half = Global<float>(addr::SC4_FloatHalf);
    const double magic = Global<float>(addr::SC4_FloatRoundMagicA95D58);
    const float rounded = static_cast<float>((scaled - half) + magic);
    const uint32_t n = std::bit_cast<uint32_t>(rounded) + 0xB4C00000u;
    const uint32_t up = n + 1;
    return scaled == static_cast<double>(static_cast<int32_t>(up)) ? up : n;
}

float AsFloat(uint32_t bits) { return std::bit_cast<float>(bits); }
double Zero() { return Global<float>(addr::SC4_FloatZero); }

constexpr uint32_t kNoControllersProperty = 0x029244DB;
constexpr uint32_t kIidNetworkOccupant = 0xA821EF94;

// The occupant's property holder (slot 6) has 0x029244DB (slot 3).
bool KeepsNoControllers(void* occupant)
{
    void* const holder = Virtual<void*>(occupant, 0x18);
    return Virtual<uint8_t>(holder, 0xC, kNoControllersProperty) != 0;
}

// Hooks to leave out, for bisecting: OPENSC4_AUTOMATA_SKIP lists function
// addresses (hex, eight digits).
bool Skipped(std::uintptr_t address)
{
    char list[1024] = {};
    if (!GetEnvironmentVariableA("OPENSC4_AUTOMATA_SKIP", list, sizeof list))
        return false;
    char wanted[16];
    std::snprintf(wanted, sizeof wanted, "%08X", static_cast<unsigned>(address));
    return std::strstr(list, wanted) != nullptr;
}

struct Detour {
    std::uintptr_t address;
    void* ours;
};

bool InstallList(const Detour* detours, std::size_t count, const char* what)
{
    if (!game::IsBuild641())
        return false;
    for (std::size_t i = 0; i < count; ++i)
        if (!addr_check::Find(detours[i].address)) {
            log::Write("automata %s: no address entry for 0x%08X", what, static_cast<unsigned>(detours[i].address));
            return false;
        }
    unsigned installed = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (Skipped(detours[i].address))
            continue;
        void* original = nullptr;
        if (!hook::Install(*addr_check::Find(detours[i].address), detours[i].ours, &original))
            return false;
        ++installed;
    }
    log::Write("automata %s: %u detours", what, installed);
    return true;
}

// ====================================================================================
// The grid

// 0x00515660, on the grid: the cell list at (x, z).
ListNode** __fastcall GridCellAt(Grid* grid, void*, uint32_t xBits, uint32_t zBits)
{
    static bool logged;
    FirstCall("GridCellAt", logged);
    // `fcomp 0 / test ah,5 / jp`: below 0 only (a NaN stays).
    if (static_cast<double>(AsFloat(xBits)) < Zero())
        xBits = 0;
    const double z = static_cast<double>(AsFloat(zBits)) < Zero() ? Zero() : static_cast<double>(AsFloat(zBits));
    const double scale = Global<float>(addr::cSC4AutomataControllerManager_CellScale);
    const uint32_t cellX = CellOf(scale * static_cast<double>(AsFloat(xBits)));
    const uint32_t cellZ = CellOf(z * scale);
    const uint32_t lastX = static_cast<uint32_t>(grid->cellsX) - 1;
    const uint32_t x = lastX < cellX ? lastX : cellX;
    const uint32_t lastZ = static_cast<uint32_t>(grid->cellsZ) - 1;
    const uint32_t zz = lastZ < cellZ ? lastZ : cellZ;
    return grid->begin + ((zz << (grid->shift & 31)) + x);
}

// 0x00515290, on the grid: the cells of a controller's bounds.
void __fastcall GridControllerRange(Grid* grid, void*, void* controller, uint8_t* out)
{
    static bool logged;
    FirstCall("GridControllerRange", logged);
    uint32_t low[3] = {}, high[3] = {}; // the game's are not set: slot 21 fills them
    Virtual<void>(controller, controller::kBounds, static_cast<uint32_t*>(low), static_cast<uint32_t*>(high));
    const double scale = Global<float>(addr::cSC4AutomataControllerManager_CellScale);
    out[0] = static_cast<uint8_t>(CellOf(scale * static_cast<double>(AsFloat(low[0]))));
    out[1] = static_cast<uint8_t>(CellOf(scale * static_cast<double>(AsFloat(low[2]))));
    out[2] = static_cast<uint8_t>(CellOf(scale * static_cast<double>(AsFloat(high[0]))));
    out[3] = static_cast<uint8_t>(CellOf(scale * static_cast<double>(AsFloat(high[2]))));
    const uint8_t lastX = static_cast<uint8_t>(grid->cellsX - 1);
    out[2] = lastX < out[2] ? lastX : out[2];
    const uint8_t lastZ = static_cast<uint8_t>(grid->cellsZ - 1);
    out[3] = lastZ >= out[3] ? out[3] : lastZ;
}

// The first node of a cell list holding the controller, or the head.
ListNode* FindIn(ListNode* head, void* controller)
{
    ListNode* e = head->next;
    while (e != head && e->value != controller)
        e = e->next;
    return e;
}

// 0x00517A90, on the grid (controller): on each cell of its range that
// lacks it.
void __fastcall GridAdd(Grid* grid, void*, void* controller)
{
    static bool logged;
    FirstCall("GridAdd", logged);
    uint8_t* const range = Virtual<uint8_t*>(controller, controller::kCellRange);
    CallThis<void>(addr::cSC4AutomataControllerManager_GridControllerRange, grid, controller, range);
    uint8_t z = range[1];
    if (z > range[3])
        return;
    do {
        uint32_t index = (static_cast<uint32_t>(z) << (grid->shift & 31)) + range[0];
        uint8_t x = range[0];
        if (!(x > range[2])) {
            do {
                ListNode* const head = grid->begin[index];
                if (FindIn(head, controller) == head)
                    PushBack(head, controller);
                x = static_cast<uint8_t>(x + 1);
                ++index;
            } while (x <= range[2]);
        }
        z = static_cast<uint8_t>(z + 1);
    } while (z <= range[3]);
}

// 0x005171D0, on the grid (controller): off every cell, its range zeroed.
void __fastcall GridRemove(Grid* grid, void*, void* controller)
{
    static bool logged;
    FirstCall("GridRemove", logged);
    for (ListNode** cell = grid->begin; cell != grid->end; ++cell) {
        ListNode* e = (*cell)->next;
        while (e != *cell)
            e = e->value == controller ? Unlink(e) : e->next;
    }
    uint8_t* const range = Virtual<uint8_t*>(controller, controller::kCellRange);
    range[0] = 0;
    range[1] = 0;
    range[2] = 0;
    range[3] = 0;
}

bool Inside(uint8_t x, uint8_t z, const uint8_t* range)
{
    return x >= range[0] && z >= range[1] && x <= range[2] && z <= range[3];
}

// 0x00517B70, on the grid (controller): from the kept range to the new one.
void __fastcall GridMove(Grid* grid, void*, void* controller)
{
    static bool logged;
    FirstCall("GridMove", logged);
    uint8_t* const old = Virtual<uint8_t*>(controller, controller::kCellRange);
    uint8_t now[4] = {};
    CallThis<void>(addr::cSC4AutomataControllerManager_GridControllerRange, grid, controller,
                   static_cast<uint8_t*>(now));
    if (now[0] == old[0] && now[1] == old[1] && now[2] == old[2] && now[3] == old[3])
        return;
    // The new cells outside the old range get it (the row index a 32-bit
    // counter beside the byte, as the game's).
    uint8_t z = now[1];
    if (!(z > now[3])) {
        uint32_t row = now[1];
        do {
            uint32_t index = (row << (grid->shift & 31)) + now[0];
            uint8_t x = now[0];
            if (!(x > now[2])) {
                do {
                    if (!Inside(x, z, old))
                        PushBack(grid->begin[index], controller);
                    x = static_cast<uint8_t>(x + 1);
                    ++index;
                } while (x <= now[2]);
            }
            z = static_cast<uint8_t>(z + 1);
            ++row;
        } while (z <= now[3]);
    }
    // The old cells outside the new range lose it (the first node only).
    z = old[1];
    if (!(z > old[3])) {
        do {
            uint32_t index = (static_cast<uint32_t>(z) << (grid->shift & 31)) + old[0];
            uint8_t x = old[0];
            if (!(x > old[2])) {
                do {
                    if (!Inside(x, z, now)) {
                        ListNode* const head = grid->begin[index];
                        ListNode* const found = FindIn(head, controller);
                        if (found != head)
                            Unlink(found);
                    }
                    x = static_cast<uint8_t>(x + 1);
                    ++index;
                } while (x <= old[2]);
            }
            z = static_cast<uint8_t>(z + 1);
        } while (z <= old[3]);
    }
    std::memcpy(old, now, 4);
}

// ====================================================================================
// The queries

// 0x00514CE0, slot 19 (occupant, template name).
void* __fastcall GetControllerByName(char* self, void*, void* occupant, void* name)
{
    static bool logged;
    FirstCall("GetControllerByName", logged);
    void* const found = Virtual<void*>(At<void*>(self, field::kScripts), 0x44, name);
    if (!found)
        return nullptr;
    return Virtual<void*>(self, 0x48, occupant, found);
}

// 0x005173A0, slot 18 (occupant, template).
void* __fastcall GetControllerByTemplate(char* self, void*, void* occupant, void* tmplate)
{
    static bool logged;
    FirstCall("GetControllerByTemplate", logged);
    auto* const node = FindNode<ControllerNode>(Controllers(self), occupant);
    if (!node || !tmplate)
        return nullptr;
    for (ListNode* e = node->head->next; e != node->head; e = e->next) {
        void* const controller = e->value;
        if (controller && Virtual<void*>(controller, controller::kTemplate) == tmplate)
            return controller;
    }
    return nullptr;
}

// Two cRZBaseStrings' texts equal (the game's length test and repz cmpsb;
// a null string's pointers are read, as the game).
bool SameName(char* name, char* other)
{
    char* const* const wanted = name ? reinterpret_cast<char* const*>(name + 4) : nullptr;
    char* const* const have = other ? reinterpret_cast<char* const*>(other + 4) : nullptr;
    const std::ptrdiff_t haveLength = have[1] - have[0];
    const std::ptrdiff_t wantedLength = wanted[1] - wanted[0];
    return haveLength == wantedLength &&
           std::memcmp(have[0], wanted[0], static_cast<std::size_t>(static_cast<uint32_t>(haveLength))) == 0;
}

// 0x00516410, slot 17 (x, y, z, template name, radius).
void* __fastcall GetControllerNear(char* self, void*, uint32_t xBits, uint32_t yBits, uint32_t zBits, char* name,
                                   uint32_t radiusBits)
{
    static bool logged;
    FirstCall("GetControllerNear", logged);
    ListNode** const cell =
        CallThis<ListNode**>(addr::cSC4AutomataControllerManager_GridCellAt, GridOf(self), xBits, zBits);
    for (ListNode* e = (*cell)->next; e != *cell; e = e->next) {
        void* const controller = e->value;
        if (!controller)
            continue;
        if (!SameName(name, Virtual<char*>(controller, controller::kName)))
            continue;
        const uint32_t zero = Global<uint32_t>(addr::SC4_ZeroWord);
        uint32_t where[3] = {zero, zero, zero};
        Virtual<void>(controller, controller::kPosition, static_cast<uint32_t*>(where));
        const double dx = static_cast<double>(AsFloat(where[0])) - static_cast<double>(AsFloat(xBits));
        const double dy = static_cast<double>(AsFloat(where[1])) - static_cast<double>(AsFloat(yBits));
        const double dz = static_cast<double>(AsFloat(where[2])) - static_cast<double>(AsFloat(zBits));
        const double distance = std::sqrt((dz * dz + dy * dy) + dx * dx);
        // `test ah,0x41 / jnp`: below or equal (not unordered).
        if (distance <= static_cast<double>(AsFloat(radiusBits)))
            return controller;
    }
    return nullptr;
}

// 0x005179C0, slot 20 (x, z, controller type, list).
uint8_t __fastcall GetRelevantControllersByWorldPos(char* self, void*, uint32_t xBits, uint32_t zBits, uint32_t type,
                                                    ListNode** list)
{
    static bool logged;
    FirstCall("GetRelevantControllersByWorldPos", logged);
    if (At<uint8_t>(self, field::kEnabled)) {
        ListNode** const cell =
            CallThis<ListNode**>(addr::cSC4AutomataControllerManager_GridCellAt, GridOf(self), xBits, zBits);
        uint32_t point[3] = {xBits, 0, zBits};
        for (ListNode* e = (*cell)->next; e != *cell; e = e->next) {
            void* const controller = e->value;
            if (!Virtual<uint8_t>(controller, controller::kActive))
                continue;
            if (Virtual<uint32_t>(controller, controller::kType) != type)
                continue;
            if (!Virtual<uint8_t>(controller, controller::kTakesPoint, static_cast<uint32_t*>(point), 0u))
                continue;
            Virtual<uint32_t>(controller, controller::kAddRef);
            PushBack(*list, controller);
        }
    }
    return (*list)->next != *list ? 1 : 0;
}

// 0x00516800, slot 21 (list).
void __fastcall ReleaseControllerList(char*, void*, ListNode** list)
{
    static bool logged;
    FirstCall("ReleaseControllerList", logged);
    for (ListNode* e = (*list)->next; e != *list; e = e->next)
        Virtual<uint32_t>(e->value, controller::kRelease);
    for (ListNode* e = (*list)->next; e != *list;) {
        ListNode* const next = e->next;
        Free(e);
        e = next;
    }
    (*list)->next = *list;
    (*list)->prev = *list;
}

// 0x00515180, on a controller (time, value): its strength, unrounded.
double __fastcall AttractionStrength(char* controller, void*, const ControllerTime* time, uint32_t valueBits)
{
    static bool logged;
    FirstCall("AttractionStrength", logged);
    const uint32_t day = 1u << (static_cast<uint8_t>(time->day) & 31);
    if (!(At<uint32_t>(At<char*>(controller, controller::kTemplateField), tmpl2::kWeekdays) & day))
        return Zero();
    const double base = Virtual<double>(controller, controller::kStrengthBase);
    const float first = static_cast<float>(static_cast<double>(Global<float>(addr::SC4_FloatOneA81228)) -
                                           static_cast<double>(AsFloat(valueBits)) / base);
    const double atValue = CallThis<double>(addr::SC4Curve_Evaluate, controller + controller::kCurve, valueBits);
    const float second = static_cast<float>(atValue * static_cast<double>(first));
    const double atHour = CallThis<double>(addr::SC4Curve_Evaluate,
                                           At<char*>(controller, controller::kTemplateField) + tmpl2::kHourCurve,
                                           std::bit_cast<uint32_t>(time->hour));
    return atHour * static_cast<double>(second);
}

// 0x00515DA0, on a controller (occupant).
uint8_t __fastcall IsRelevantTo(char* controller, void*, void* occupant)
{
    static bool logged;
    FirstCall("IsRelevantTo", logged);
    if (At<void*>(controller, controller::kOccupant) == occupant)
        return 0;
    char* const t = At<char*>(controller, controller::kTemplateField);
    const uint32_t* group = At<const uint32_t*>(t, tmpl2::kGroupsBegin);
    if (group == At<const uint32_t*>(t, tmpl2::kGroupsEnd))
        return 0;
    while (!Virtual<uint8_t>(occupant, 0x58, *group)) {
        ++group;
        if (group == At<const uint32_t*>(t, tmpl2::kGroupsEnd))
            return 0;
    }
    if (At<const uint32_t*>(t, tmpl2::kExcludedBegin) == At<const uint32_t*>(t, tmpl2::kExcludedEnd))
        return 1;
    uint8_t answer = 1;
    void* automaton = nullptr;
    if (Virtual<uint8_t>(occupant, 0x0, kIidAutomaton, &automaton)) {
        if (char* const kind = Virtual<char*>(automaton, 0xE0)) {
            const uint32_t* id = At<const uint32_t*>(t, tmpl2::kExcludedBegin);
            const uint32_t* const end = At<const uint32_t*>(t, tmpl2::kExcludedEnd);
            if (id != end) {
                const uint32_t wanted = At<uint32_t>(kind, 4);
                for (; id != end; ++id)
                    if (wanted == *id) {
                        answer = 0;
                        break;
                    }
            }
        }
    }
    if (automaton)
        Virtual<uint32_t>(automaton, 0x8);
    return answer;
}

// 0x00516530, slot 22 (occupant, vector out, value out, strength out).
void* __fastcall GetCompositeAttractionEffect(char* self, void*, void* occupant, uint32_t* out, uint32_t* valueOut,
                                              uint32_t* strengthOut)
{
    static bool logged;
    FirstCall("GetCompositeAttractionEffect", logged);
    if (!At<uint8_t>(self, field::kEnabled) || !occupant)
        return nullptr;
    void* strongest = nullptr;
    const uint32_t zero = Global<uint32_t>(addr::SC4_ZeroWord);
    uint32_t direction[3] = {zero, zero, zero};
    uint32_t where[3] = {zero, zero, zero};
    float best = 0.0f; // the integer 0
    uint32_t value = 0;
    Virtual<uint8_t>(occupant, 0x20, static_cast<uint32_t*>(where));
    out[0] = 0;
    out[1] = 0;
    out[2] = 0;
    ListNode** const cell =
        CallThis<ListNode**>(addr::cSC4AutomataControllerManager_GridCellAt, GridOf(self), where[0], where[2]);
    for (ListNode* e = (*cell)->next; e != *cell; e = e->next) {
        char* const controller = static_cast<char*>(e->value);
        if (!Virtual<uint8_t>(controller, controller::kActive))
            continue;
        if (Virtual<uint8_t>(controller, controller::kSaturated))
            continue;
        if (Virtual<uint32_t>(controller, controller::kType) != controller::kTypeAttractor)
            continue;
        if (!Virtual<uint8_t>(controller, controller::kTakesPoint, static_cast<uint32_t*>(where), &value))
            continue;
        if (!CallThis<uint8_t>(addr::cSC4AutomataController_IsRelevantTo, controller, occupant))
            continue;
        const float strength = static_cast<float>(CallThis<double>(
            addr::cSC4AutomataController_AttractionStrength, controller, self + field::kTimeA, value));
        Virtual<void*>(controller, controller::kTemplate); // answer unused, as the game
        if (Virtual<uint8_t>(controller, controller::kTestFlag, 0x10u))
            Virtual<void>(controller, controller::kOtherPosition, static_cast<uint32_t*>(direction));
        else
            Virtual<void>(controller, controller::kPosition, static_cast<uint32_t*>(direction));
        const double dx = static_cast<double>(AsFloat(direction[0])) - static_cast<double>(AsFloat(where[0]));
        const double dz = static_cast<double>(AsFloat(direction[2])) - static_cast<double>(AsFloat(where[2]));
        const double length = std::sqrt(dz * dz + dx * dx);
        const double scale = static_cast<double>(Global<float>(addr::cSC4AutomataControllerManager_AttractionScale)) / length;
        const float unitX = static_cast<float>(dx * scale);
        const float unitY = static_cast<float>(Zero() * scale);
        const float unitZ = static_cast<float>(dz * scale);
        // The game stores the unit vector over the position it read (the
        // same stack vector), so the next controller's slot 11 / 22 starts
        // from it where it writes nothing (slot 11 can hand over to the
        // occupant's slot 8).
        direction[0] = std::bit_cast<uint32_t>(unitX);
        direction[1] = std::bit_cast<uint32_t>(unitY);
        direction[2] = std::bit_cast<uint32_t>(unitZ);
        const double s = strength;
        const double addX = static_cast<double>(unitX) * s;
        const double addY = static_cast<double>(unitY) * s;
        const float addZ = static_cast<float>(static_cast<double>(unitZ) * s);
        out[0] = std::bit_cast<uint32_t>(static_cast<float>(addX + static_cast<double>(AsFloat(out[0]))));
        out[1] = std::bit_cast<uint32_t>(static_cast<float>(addY + static_cast<double>(AsFloat(out[1]))));
        out[2] = std::bit_cast<uint32_t>(
            static_cast<float>(static_cast<double>(addZ) + static_cast<double>(AsFloat(out[2]))));
        const double magnitude = std::fabs(s);
        // `test ah,0x41 / jne`: above only.
        if (magnitude > static_cast<double>(best)) {
            best = static_cast<float>(magnitude);
            *strengthOut = std::bit_cast<uint32_t>(strength);
            strongest = controller;
            *valueOut = value;
        }
    }
    return strongest;
}

// 0x00516720, slot 23 (network occupant): float in ST0.
float __fastcall GetCompositeAttractionCommotion(char* self, void*, void* network)
{
    static bool logged;
    FirstCall("GetCompositeAttractionCommotion", logged);
    if (!At<uint8_t>(self, field::kEnabled) || !network)
        return Global<float>(addr::SC4_FloatZero);
    const uint32_t zero = Global<uint32_t>(addr::SC4_ZeroWord);
    uint32_t where[3] = {zero, zero, zero};
    float total = 0.0f; // the integer 0
    uint32_t value = 0;
    void* const occupant = Virtual<void*>(network, 0xC0);
    Virtual<uint8_t>(occupant, 0x20, static_cast<uint32_t*>(where));
    where[1] = 0;
    ListNode** const cell =
        CallThis<ListNode**>(addr::cSC4AutomataControllerManager_GridCellAt, GridOf(self), where[0], where[2]);
    for (ListNode* e = (*cell)->next; e != *cell; e = e->next) {
        char* const controller = static_cast<char*>(e->value);
        if (!Virtual<uint8_t>(controller, controller::kActive))
            continue;
        if (Virtual<uint32_t>(controller, controller::kType) != controller::kTypeAttractor)
            continue;
        if (!Virtual<uint8_t>(controller, controller::kTakesPoint, static_cast<uint32_t*>(where), &value))
            continue;
        const double strength = CallThis<double>(addr::cSC4AutomataController_AttractionStrength, controller,
                                                 self + field::kTimeA, value);
        total = static_cast<float>(std::fabs(strength) + static_cast<double>(total));
    }
    return total;
}

// 0x005163D0 (group, occupant, position): slot 6 per template of the group.
uint8_t __fastcall AddControllersFromGroup(char* self, void*, char* group, void* occupant, float* position)
{
    static bool logged;
    FirstCall("AddControllersFromGroup", logged);
    for (ListNode* t = At<ListNode*>(group, 0x30)->next; t != At<ListNode*>(group, 0x30); t = t->next)
        Virtual<void*>(self, 0x18, occupant, t->value, position);
    return 1;
}

// ====================================================================================
// The controller time

double TwentyFour() { return Global<float>(addr::SC4_FloatTwentyFour); }

// 0x00514FC0, on a time: into [0, 24) and 0..6.
ControllerTime* __fastcall NormalizeTime(ControllerTime* time, void*)
{
    static bool logged;
    FirstCall("NormalizeTime", logged);
    const uint32_t bits = std::bit_cast<uint32_t>(time->hour);
    if ((bits & 0x7F800000u) == 0x7F800000u && (bits & 0x7FFFFFu)) {
        *reinterpret_cast<uint32_t*>(&time->hour) = 0;
    } else {
        if (static_cast<double>(time->hour) < Zero()) {
            double hour = time->hour;
            uint16_t day = static_cast<uint16_t>(time->day);
            do {
                hour += TwentyFour();
                day = static_cast<uint16_t>(day - 1);
            } while (hour < Zero());
            time->hour = static_cast<float>(hour);
            time->day = static_cast<int16_t>(day);
        }
        // `test ah,1 / jne`: below or unordered skips.
        if (static_cast<double>(time->hour) >= TwentyFour()) {
            double hour = time->hour;
            uint16_t day = static_cast<uint16_t>(time->day);
            do {
                hour -= TwentyFour();
                day = static_cast<uint16_t>(day + 1);
            } while (hour >= TwentyFour());
            time->hour = static_cast<float>(hour);
            time->day = static_cast<int16_t>(day);
        }
    }
    if (time->day < 0) {
        int16_t day = time->day;
        do
            day = static_cast<int16_t>(day + 7);
        while (day < 0);
        time->day = day;
    }
    time->day = static_cast<int16_t>(static_cast<int32_t>(time->day) % 7);
    return time;
}

// 0x00515100, on a time (hours): the hours less, the day moved for them.
ControllerTime* __fastcall SubtractHours(ControllerTime* time, void*, uint32_t hoursBits)
{
    static bool logged;
    FirstCall("SubtractHours", logged);
    double hours = AsFloat(hoursBits);
    if (hours < Zero()) {
        uint16_t day = static_cast<uint16_t>(time->day);
        do {
            hours += TwentyFour();
            day = static_cast<uint16_t>(day + 1);
        } while (hours < Zero());
        time->day = static_cast<int16_t>(day);
    }
    if (hours >= TwentyFour()) {
        uint16_t day = static_cast<uint16_t>(time->day);
        do {
            hours -= TwentyFour();
            day = static_cast<uint16_t>(day - 1);
        } while (hours >= TwentyFour());
        time->day = static_cast<int16_t>(day);
    }
    time->hour = static_cast<float>(static_cast<double>(time->hour) - hours);
    return time;
}

// 0x00515D50, on a time (out, other): this less other into out.
ControllerTime* __fastcall TimeDifference(ControllerTime* time, void*, ControllerTime* out, const ControllerTime* other)
{
    static bool logged;
    FirstCall("TimeDifference", logged);
    ControllerTime copy{};
    copy.hour = time->hour;
    copy.day = time->day;
    CallThis<ControllerTime*>(addr::tControllerTime_SubtractHours, &copy, std::bit_cast<uint32_t>(other->hour));
    out->hour = copy.hour;
    out->day = static_cast<int16_t>(static_cast<uint16_t>(copy.day) - static_cast<uint16_t>(other->day));
    return out;
}

// ====================================================================================
// The message side

ControllerTime* TimeAt(char* self, std::size_t offset) { return reinterpret_cast<ControllerTime*>(self + offset); }

// 0x005177F0: slot 27 of every controller.
void __fastcall NotifyControllersMoved(char* self, void*)
{
    static bool logged;
    FirstCall("NotifyControllersMoved", logged);
    Iterator it{};
    Begin(Controllers(self), &it);
    for (auto* node = static_cast<ControllerNode*>(it.node); node; node = NextNode(node, it.table))
        for (ListNode* e = node->head->next; e != node->head; e = e->next)
            Virtual<uint32_t>(e->value, controller::kMoved);
}

// 0x00518950: each controller with flag 1 moved on the grid, its slot 27.
void __fastcall MoveFlaggedControllers(char* self, void*)
{
    static bool logged;
    FirstCall("MoveFlaggedControllers", logged);
    Iterator it{};
    Begin(Controllers(self), &it);
    for (auto* node = static_cast<ControllerNode*>(it.node); node; node = NextNode(node, it.table))
        for (ListNode* e = node->head->next; e != node->head; e = e->next) {
            void* const controller = e->value;
            if (!Virtual<uint8_t>(controller, controller::kTestFlag, 1u))
                continue;
            CallThis<void>(addr::cSC4AutomataControllerManager_GridMove, GridOf(self), controller);
            Virtual<uint32_t>(controller, controller::kMoved);
        }
}

// 0x00518660: after a load, each controller's template by its name.
void __fastcall ResolveTemplates(char* self, void*)
{
    static bool logged;
    FirstCall("ResolveTemplates", logged);
    if (!At<uint8_t>(self, field::kLoaded))
        return;
    Iterator it{};
    Begin(Controllers(self), &it);
    for (auto* node = static_cast<ControllerNode*>(it.node); node; node = NextNode(node, it.table)) {
        ListNode* e = node->head->next;
        while (e != node->head) {
            // A null controller: the game does not step on (for ever); the
            // volatile read keeps that loop.
            char* const controller = static_cast<char*>(*reinterpret_cast<void* volatile*>(&e->value));
            if (!controller)
                continue;
            void* const name = Virtual<void*>(controller, controller::kName);
            void* const found = Virtual<void*>(At<void*>(self, field::kScripts), 0x44, name);
            if (found) {
                Virtual<uint32_t>(controller, controller::kSetTemplate, found);
                Virtual<uint8_t>(controller, controller::kInit);
                CallThis<void>(addr::cSC4AutomataControllerManager_GridAdd, GridOf(self), controller);
                e = e->next;
                continue;
            }
            // The error text nothing reads (the game's debug output).
            GameString text{};
            char* const buffer = static_cast<char*>(Allocate(8));
            text.capacity = buffer + 8;
            text.begin = buffer;
            text.end = buffer;
            buffer[0] = 0;
            text.vtable = addr::SC4String_Vtable;
            text.references = 0;
            const char* const chars = Virtual<const char*>(name, 0x14);
            CallCdecl<void>(addr::cRZBaseString_Sprintf, &text,
                            reinterpret_cast<const char*>(addr::cSC4AutomataControllerManager_NoTemplateFormat), chars);
            e = Unlink(e);
            Virtual<uint8_t>(controller, controller::kShutdown);
            Virtual<uint32_t>(controller, controller::kRelease);
            text.vtable = addr::SC4String_Vtable;
            if (text.begin)
                Free(text.begin);
        }
    }
}

// 0x00518B80 (a, b): the frame update.
void __fastcall Update(char* self, void*, uint32_t a, uint32_t b)
{
    static bool logged;
    FirstCall("Update", logged);
    void* const clock = At<void*>(self, field::kClock);
    const int32_t minute = Virtual<int32_t>(clock, 0x34);
    const int32_t hour = Virtual<int32_t>(clock, 0x28);
    const double twoTo32 = Global<float>(addr::SC4_FloatTwoTo32);
    double hourValue = static_cast<double>(hour);
    if (hour < 0)
        hourValue += twoTo32;
    double minuteValue = static_cast<double>(minute);
    if (minute < 0)
        minuteValue += twoTo32;
    const double now = hourValue + minuteValue * static_cast<double>(Global<float>(addr::SC4_FloatOneSixtieth));
    ControllerTime* const current = TimeAt(self, field::kTimeA);
    // `fcom / test ah,5 / jp`: below only.
    if (now < static_cast<double>(current->hour))
        current->day = static_cast<int16_t>(current->day + 1);
    current->hour = static_cast<float>(now);
    ControllerTime copy{};
    copy.hour = current->hour;
    copy.day = current->day;
    CallThis<ControllerTime*>(addr::tControllerTime_SubtractHours, &copy,
                              At<uint32_t>(self, field::kTimeC));
    ControllerTime passed{};
    passed.hour = copy.hour;
    passed.day = static_cast<int16_t>(static_cast<uint16_t>(copy.day) - At<uint16_t>(self, field::kTimeD));
    CallThis<ControllerTime*>(addr::tControllerTime_Normalize, current);
    void* const vehicles = Virtual<void*>(City(), 0xAC);
    void* const pedestrians = Virtual<void*>(City(), 0xB0);
    if (At<uint8_t>(self, field::kEnabled) &&
        ((vehicles && Virtual<uint8_t>(vehicles, 0x18)) || (pedestrians && Virtual<uint8_t>(pedestrians, 0x18)))) {
        CallThis<void>(addr::cSC4AutomataControllerManager_MoveFlaggedControllers, self);
        Iterator it{};
        Begin(Controllers(self), &it);
        for (auto* node = static_cast<ControllerNode*>(it.node); node;
             node = static_cast<ControllerNode*>(Advance(&it))) {
            ListNode* e = node->head->next;
            while (e != node->head) {
                char* const controller = static_cast<char*>(e->value);
                if (Virtual<uint8_t>(controller, controller::kWantsTick))
                    Virtual<void>(controller, controller::kTick, b, a, &passed);
                if (!Virtual<uint8_t>(controller, controller::kFinishedA) ||
                    !Virtual<uint8_t>(controller, controller::kFinishedB)) {
                    e = e->next;
                    continue;
                }
                e = Unlink(e);
                SendRemoved(Global<void*>(addr::SC4_MessageServer), controller);
                CallThis<void>(addr::cSC4AutomataControllerManager_GridRemove, GridOf(self), controller);
                Virtual<uint8_t>(controller, controller::kShutdown);
                Virtual<uint32_t>(controller, controller::kRelease);
            }
        }
    }
    At<uint32_t>(self, field::kTimeC) = At<uint32_t>(self, field::kTimeA);
    At<uint32_t>(self, field::kTimeD) = At<uint32_t>(self, field::kTimeB);
}

// 0x005187F0, the +4 part's slot 3 (cheat): Controllers off / on.
constexpr uint32_t kCheatNotice = 0x230E27AC;
constexpr uint32_t kCheatCode = 0xABA2B5BB;
uint8_t __fastcall OnCheat(char* part, void*, char* cheat)
{
    static bool logged;
    FirstCall("OnCheat", logged);
    if (At<uint32_t>(cheat, 0) != kCheatNotice || At<uint32_t>(cheat, 4) != kCheatCode)
        return 0;
    const char* const text = Virtual<const char*>(At<void*>(cheat, 8), 0x14);
    alignas(4) char words[0x3C];
    CallThis<void*>(addr::SC4Tokenizer_SC4Tokenizer, words, text);
    if (CallThis<int32_t>(addr::SC4Tokenizer_Count, words) >= 2) {
        char* word = Virtual<char*>(words, 0x40, 1u);
        if (CallCdecl<uint8_t>(addr::SC4String_Equals, word ? word + 4 : nullptr,
                               reinterpret_cast<const char*>(addr::SC4_TextOff))) {
            At<uint8_t>(part, field::kEnabled - field::kMessagePart) = 0;
            CallThis<void>(addr::SC4Tokenizer_Destroy, words);
            return 1;
        }
        word = Virtual<char*>(words, 0x40, 1u);
        if (CallCdecl<uint8_t>(addr::SC4String_Equals, word ? word + 4 : nullptr,
                               reinterpret_cast<const char*>(addr::SC4_TextOn)))
            At<uint8_t>(part, field::kEnabled - field::kMessagePart) = 1;
    }
    CallThis<void>(addr::SC4Tokenizer_Destroy, words);
    return 1;
}

// Each controller of every occupant, the walk Update and DoMessage use
// (NextBucket where a chain ends).
template <typename Fn>
void EachController(char* self, Fn&& fn)
{
    Iterator it{};
    Begin(Controllers(self), &it);
    for (auto* node = static_cast<ControllerNode*>(it.node); node; node = static_cast<ControllerNode*>(Advance(&it)))
        for (ListNode* e = node->head->next; e != node->head; e = e->next)
            fn(static_cast<char*>(e->value));
}

// Every automaton entry's flag cleared.
void ClearAutomatonFlags(char* self)
{
    Iterator it{};
    Begin(Automata(self), &it);
    for (auto* node = static_cast<AutomatonNode*>(it.node); node; node = static_cast<AutomatonNode*>(Advance(&it)))
        node->value.flag = 0;
}

// 0x00519360, the +8 part's slot 3 (message).
uint8_t __fastcall DoMessage(char* part, void*, void* message)
{
    static bool logged;
    FirstCall("DoMessage", logged);
    char* const self = part - field::kTargetPart;
    switch (Virtual<uint32_t>(message, 0x10)) {
    case 0x44778230: {
        CallThis<ControllerTime*>(addr::tControllerTime_AddHours, TimeAt(self, field::kTimeA), 0x3F800000u);
        EachController(self, [&](char* controller) {
            ControllerTime difference{};
            ControllerTime* const passed =
                CallThis<ControllerTime*>(addr::tControllerTime_Difference, TimeAt(self, field::kTimeA), &difference,
                                          TimeAt(self, field::kTimeC));
            Virtual<void>(controller, controller::kTimePassed, TimeAt(self, field::kTimeA), passed);
        });
        return 1;
    }
    case 0x26D31EC1:
        CallThis<void>(addr::cSC4AutomataControllerManager_ResolveTemplates, self);
        return 1;
    case 0x09D92FC3: {
        void* const data = Virtual<void*>(message, 0x28);
        if (Virtual<uint32_t>(message, 0x38) != 3)
            return 1;
        EachController(self, [&](char* controller) { Virtual<uint32_t>(controller, controller::kOnEvent, data); });
        return 1;
    }
    case 0x86AD10EE: {
        const uint32_t a = Virtual<uint32_t>(message, 0x38);
        const uint32_t b = Virtual<uint32_t>(message, 0x4C);
        CallThis<void>(addr::cSC4AutomataControllerManager_Update, self, a, b);
        return 1;
    }
    case 0x89D3E9F3: {
        void* const data = Virtual<void*>(message, 0x80);
        const bool on = Virtual<uint32_t>(message, 0x24) != 0;
        if (data && on)
            ClearAutomatonFlags(self);
        return 1;
    }
    case 0x99EF1142: {
        void* const occupant = Virtual<void*>(message, 0x28);
        if (!KeepsNoControllers(occupant))
            Virtual<uint8_t>(self, 0x34, occupant);
        return 1;
    }
    case 0x99EF1143: {
        void* const occupant = Virtual<void*>(message, 0x28);
        void* network = nullptr;
        if (Virtual<uint8_t>(occupant, 0x0, kIidNetworkOccupant, &network)) {
            ClearAutomatonFlags(self);
            Virtual<uint32_t>(network, 0x8);
        }
        if (!KeepsNoControllers(occupant))
            Virtual<uint8_t>(self, 0x38, occupant);
        return 1;
    }
    case 0xAA375FE8:
        Virtual<uint32_t>(message, 0x24); // answers unused, as the game
        Virtual<uint32_t>(message, 0x38);
        EachController(self, [](char* controller) { Virtual<uint32_t>(controller, controller::kOnMessageA); });
        return 1;
    case 0xAA1D07F3:
        EachController(self, [](char* controller) { Virtual<uint32_t>(controller, controller::kOnMessageB); });
        return 1;
    case 0xA6B79612: {
        if (At<uint8_t>(self, field::kHoldCamera))
            return 1;
        char* const camera = Global<char*>(addr::SC4_CameraView);
        uint32_t* const point = &At<uint32_t>(self, field::kCameraPoint);
        uint32_t* const from = &At<uint32_t>(camera, 0x3C);
        const double distance = CallCdecl<double>(addr::SC4_DistanceSquared3, point, from);
        const uint32_t zoom = At<uint32_t>(camera, 0xC);
        const float limit = Global<float>(addr::cSC4AutomataControllerManager_CameraMoveLimits + zoom * 4u);
        // `test ah,0x41 / jne`: above only.
        if (!(distance > static_cast<double>(limit)))
            return 1;
        point[0] = from[0];
        point[1] = from[1];
        point[2] = from[2];
        CallThis<void>(addr::cSC4AutomataControllerManager_NotifyControllersMoved, self);
        return 1;
    }
    case 0xCAD61E00:
        At<uint8_t>(self, field::kHoldCamera) = 1;
        return 1;
    case 0xEAD61E16:
        At<uint8_t>(self, field::kHoldCamera) = 0;
        CallThis<void>(addr::cSC4AutomataControllerManager_NotifyControllersMoved, self);
        return 1;
    case 0xA6B79602:
        CallThis<void>(addr::cSC4AutomataControllerManager_NotifyControllersMoved, self);
        return 1;
    case 0xEBFF18A3: {
        const uint32_t id = Virtual<uint32_t>(message, 0x60);
        void* const occupant = Virtual<void*>(message, 0x28);
        if (KeepsNoControllers(occupant))
            return 1;
        char* const group = Virtual<char*>(At<void*>(self, field::kScripts), 0x2C, id);
        if (!group)
            return 1;
        alignas(8) char random[0x20];
        {
            const fixed_seed::SiteOverride site(0x00519853);
            CallThis<void*>(addr::SC4Random_Construct, random, -1);
        }
        const double chance = At<float>(group, 0x2C);
        const double draw = CallThis<double>(addr::cRZRandom_NextFloat, random);
        // `test ah,0x41 / jp`: below or equal (not unordered).
        if (draw <= chance)
            CallThis<uint8_t>(addr::cSC4AutomataControllerManager_AddControllersFromGroup, self, group, occupant,
                              static_cast<float*>(nullptr));
        return 1;
    }
    default:
        return 1;
    }
}

// ====================================================================================
// Registration

#define OPENSC4_DETOUR(address, fn) {address, reinterpret_cast<void*>(&fn)}

const Detour kQueries[] = {
    OPENSC4_DETOUR(addr::cSC4AutomataControllerManager_GetControllerByName, GetControllerByName),
    OPENSC4_DETOUR(addr::cSC4AutomataControllerManager_GetControllerByTemplate, GetControllerByTemplate),
    OPENSC4_DETOUR(addr::cSC4AutomataControllerManager_GetControllerNear, GetControllerNear),
    OPENSC4_DETOUR(addr::cSC4AutomataControllerManager_GetRelevantControllersByWorldPos,
                   GetRelevantControllersByWorldPos),
    OPENSC4_DETOUR(addr::cSC4AutomataControllerManager_ReleaseControllerList, ReleaseControllerList),
    OPENSC4_DETOUR(addr::cSC4AutomataControllerManager_GetCompositeAttractionEffect, GetCompositeAttractionEffect),
    OPENSC4_DETOUR(addr::cSC4AutomataControllerManager_GetCompositeAttractionCommotion,
                   GetCompositeAttractionCommotion),
    OPENSC4_DETOUR(addr::cSC4AutomataControllerManager_GridAdd, GridAdd),
    OPENSC4_DETOUR(addr::cSC4AutomataControllerManager_GridRemove, GridRemove),
    OPENSC4_DETOUR(addr::cSC4AutomataControllerManager_GridMove, GridMove),
    OPENSC4_DETOUR(addr::cSC4AutomataControllerManager_AddControllersFromGroup, AddControllersFromGroup),
    OPENSC4_DETOUR(addr::cSC4AutomataControllerManager_GridControllerRange, GridControllerRange),
    OPENSC4_DETOUR(addr::cSC4AutomataControllerManager_GridCellAt, GridCellAt),
    OPENSC4_DETOUR(addr::cSC4AutomataController_AttractionStrength, AttractionStrength),
    OPENSC4_DETOUR(addr::cSC4AutomataController_IsRelevantTo, IsRelevantTo),
};

const Detour kMessages[] = {
    OPENSC4_DETOUR(addr::cSC4AutomataControllerManager_OnCheat, OnCheat),
    OPENSC4_DETOUR(addr::cSC4AutomataControllerManager_DoMessage, DoMessage),
    OPENSC4_DETOUR(addr::cSC4AutomataControllerManager_ResolveTemplates, ResolveTemplates),
    OPENSC4_DETOUR(addr::cSC4AutomataControllerManager_MoveFlaggedControllers, MoveFlaggedControllers),
    OPENSC4_DETOUR(addr::cSC4AutomataControllerManager_Update, Update),
    OPENSC4_DETOUR(addr::cSC4AutomataControllerManager_NotifyControllersMoved, NotifyControllersMoved),
    OPENSC4_DETOUR(addr::tControllerTime_Normalize, NormalizeTime),
    OPENSC4_DETOUR(addr::tControllerTime_SubtractHours, SubtractHours),
    OPENSC4_DETOUR(addr::tControllerTime_Difference, TimeDifference),
};

#undef OPENSC4_DETOUR

bool InstallQueries(registry::Mode)
{
    return InstallList(kQueries, sizeof kQueries / sizeof kQueries[0], "ctlquery");
}

bool InstallMessages(registry::Mode)
{
    return InstallList(kMessages, sizeof kMessages / sizeof kMessages[0], "ctlmsg");
}

const bool g_registered[] = {
    registry::Register({"simulators.automata.ctlquery", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4AutomataControllerManager_GetRelevantControllersByWorldPos,
                        "Our automata controller manager's queries: the three GetController overloads, "
                        "GetRelevantControllersByWorldPos, ReleaseControllerList, the composite attraction effect "
                        "and commotion, the controller grid's add, remove and move and their cell helpers, "
                        "AddControllersFromGroup (15 detours; src/core/automata_ctlquery.cpp)",
                        false, InstallQueries}),
    registry::Register({"simulators.automata.ctlmsg", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4AutomataControllerManager_DoMessage,
                        "Our automata controller manager's messages: the message handler, the cheat Controllers, "
                        "the frame update, the templates after a load, the move notifications and the controller "
                        "time's helpers (9 detours; src/core/automata_ctlquery.cpp)",
                        false, InstallMessages}),
};

} // namespace
} // namespace opensc4::automata_ctlmgr
