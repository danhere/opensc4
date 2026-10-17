// Our cSC4AutomataControllerManager, part a1 (build 641): the object (its
// constructor, destructor, QueryInterface, Release, Init, Shutdown), the
// bookkeeping of controllers (add from a template or a template name, from
// an occupant's groups or a named or numbered occupant group; remove one,
// one by name, all of an occupant's or of a group's; move), the controller
// grid's cell range and set-up, the vector-of-lists helpers the grid is
// built with, the controller template base's destructor, and the save
// (Write, Read, GetGZCLSID and the automaton table's insert). The class map
// is in src/core/automata_ctlmgr.h. Calls between functions of the part go
// to the game's addresses, so each detour stands alone and
// OPENSC4_AUTOMATA_SKIP (function addresses, hex, eight digits) bisects
// them.
//
// Groups, off by default:
//   simulators.automata.ctlmgr: the object, Init/Shutdown, the adds,
//   removes and moves and their helpers (35 detours).
//   simulators.automata.ctlsave: Write, Read, GetGZCLSID and the
//   automaton table's insert 0x0051A900 (4 detours).
//
// What it does. The controller table (+0x58) maps an occupant (referenced
// once when its entry is made) to the list of its controllers (each the
// reference the COM made). A controller is also on the controller grid
// (+0x1C; the grid functions 0x00517A90 add, 0x005171D0 remove, 0x00517B70
// move are part a2's): one list per 128-metre cell. AddControllerFromTemplate
// refuses a template that a controller of an occupant on the same lot
// (lot manager slot 14) already has (unless the template's flag 0x40 is
// set or there is no occupant), and, for a radius of 1 or more, a template
// that a controller in the grid cells within the radius of the occupant's
// position (or the given one) already has; then it makes the controller
// (RZGetCOM slot 3: the template's class, interface 0x6A1857F8), gives the
// occupant an entry (with a reference) when it has none, appends the
// controller, gives it the occupant (slot 14) or else the position (slot
// 17), the template (slot 15), starts it (slot 3) and puts it on the grid.
// A failed creation or a failed insert builds an error text that nothing
// reads (the game's debug output, stripped) and answers 0 (the failed
// insert releases the new controller). Removing sends the message
// 0xEA298D50 with the controller (a new cGZMessage through the message
// server's slot 3), takes it off the grid, shuts it down (slot 4) and
// releases it; an occupant whose list empties leaves both tables (its
// automaton entry releases its automata and the occupant once more).
// Shutdown releases everything; Write saves the clock reference, the
// controller time, per occupant the controllers whose slot 12 with 8 is
// false, and the automaton table.
//
// Oddities kept: QueryInterface answers interface 1 (cIGZUnknown) with the
// serializable part (+0xC); its null-object branches write a null and then
// call through the null object (never reached); RemoveControllerByName
// (slot 8) answers 0 always; GridCellRange asks the city for SizeX twice
// (no SizeZ; both sides of a city are equal); GridInit takes both grid
// sides from the cells in x; AddControllerFromTemplate reads the template
// before testing for one only on the creation path (a null template
// crashes there, as the game); Read's temporary automaton value has a
// count and a byte that the game leaves as its stack had them: a new entry
// gets the count read right after, an entry already there is not touched,
// so they are read nowhere; ours writes zeros. Read's last byte goes
// through a stack byte the game never sets: a failed ReadBool leaves the
// entry with what that byte held (ours: 0 before the first read, then the
// last byte read). On a failed read of a count the game loops over what
// its stack slot held before: the outer counts start as leftovers in the
// game (ours 0), the inner count slot is shared by both tables in both.
//
// Still the game's, at their addresses:
//   - the six adjustor thunks 0x0051A320, 0x0051A330, 0x0051A340 (jump to
//     QueryInterface) and 0x0051B3B0, 0x0051B3C0, 0x0051B3D0 (jump to
//     Release): eight bytes each, they reach our detours at the entries.
//   - AddRef 0x00695330 and GetControllerTime 0x0051A310 (folded with other
//     classes).
//   - part a2: the queries and message handlers (0x005163D0
//     AddControllersFromGroup, the grid's 0x00517A90 / 0x005171D0 /
//     0x00517B70, the GetController overloads, the message handler
//     0x00519360, the cheat target 0x005187F0, ...).
//   - shared with the attractor code: 0x00518A40 (the automaton value's
//     copy), 0x0051B9D0.
//   - generic STL/hash helpers: SC4HashTable::Begin 0x004B0390,
//     SC4Uint32HashTable::Find 0x006F1530, SC4KeyListTable::{InsertUnique
//     0x006F2A90, Erase 0x006F28F0, Clear 0x006F2970},
//     SC4Uint32HashSet::{Construct 0x00829BC0, Resize 0x004F0940},
//     SC4StdList::{InsertRangeUint32 0x006F1570, DestroyListRange
//     0x006F1EA0, CopyConstructUint32 0x004C9430}, SC4List::Assign2
//     0x0064B660, SC4PairList::Destroy 0x005650A0.
//
// Not in the part's list, found next to it: the controller template base's
// constructor 0x0051AB80 and the attractor and generator templates'
// constructors and destructors 0x0051B1F0, 0x0051B240, 0x0051B280,
// 0x0051B2A0, 0x0051B2F0, 0x0051B310 (the last two of each tail-jump to
// 0x0051A9A0): not ours.
//
// Registers. The direct callers of every function of the part were scanned
// (scratch regscan.py / eaxscan.py): 0x00515400 from 0x0051AE91;
// 0x00517010 from 0x0051A4AE, 0x0051A6B1; 0x00518550 from 0x0051A858,
// 0x0051A8BF; 0x00518580 from 0x0051AC28, 0x0051B375; 0x005185C0 from
// 0x0051A213, 0x0051A882; 0x00518620 from 0x0051A84A; 0x00518AC0 from
// 0x0051A1B3, 0x0051A234, 0x0051A82F, 0x0051A8A4; 0x00518B20 from
// 0x0051A9E7 and the script system's 0x00521435 and 0x0052289C; 0x0051A160
// from 0x0051A8E7; 0x0051A280 from 0x0051B1DA; 0x0051A350 from 0x0051A527,
// 0x0051AB22, 0x0051AB62; 0x0051A6F0 from 0x0051B503; 0x0051A7D0 from
// 0x0051B3A1; 0x0051A900 from 0x0051B836 and 0x0051BA46; 0x0051A9A0 from
// 0x0051AA33 and the tail jumps 0x0051B2E7, 0x0051B344; 0x0051AA50 from the
// factory's tail jump 0x00514853; 0x0051AC10 from 0x0051B50A; 0x0051B350
// from 0x0051C0F5; 0x0051C090 from 0x0051C154. None reads ECX or EDX after
// the call, and EAX only where the function answers in it. Everything else
// is called through vtables only. No function reads a register beyond ECX
// at entry. No naked thunks.
//
// Floats. GridCellRange only (nothing else computes): the four bounds
// x - r, z - r, r + x, r + z as doubles; the first three stored as floats
// after the clamp (below 0: +0; above the city's SizeX: SizeX; else the
// value, NaN included: `test ah,5 / jp` then `test ah,0x41 / jne`), the
// fourth kept on the x87 stack as a double (below 0: the float 0x00A81054,
// above SizeX: SizeX). Each bound times the float 1/128 at 0x00B09A20 (the
// first three: float times float, exact; the fourth: double times float)
// goes to a cell number the game's way: (a - 0.5) + 12582912.0 (floats at
// 0x00A84D2C, 0x00A95D58) stored as a float, its bits plus 0xB4C00000 is n,
// and n + 1 when a equals n + 1 as a double (`fucompp`, `test ah,0x44 /
// jp`: equal only). All at the x87's 53-bit precision, which doubles
// repeat (one rounding per operation; no value comes near the exponent
// limits). The radius arrives and is passed on as its bits (an SNaN is
// quietened by the first operation either way). With a NaN in two
// operands at once the x87 and SSE pick different payloads; a NaN position
// and a NaN radius together are the only such case. GridInit converts the
// city's cells times 16 to floats (exact).
//
// Calls, each count from the asm's pushes and the callee's own `ret N`:
// the city (0x00B43CEC) slots 0x84 GetLotManager, 0xC0
// GetAutomataScriptSystem, 0x158 Get24HourClock, 0x164 SizeX (float in
// ST0), 0x174 / 0x178 the cells in x / z, all none; the script system
// (0x00A96BA8) slots 0x28 none, 0x2C (ID), 0x30 (name), 0x44 (name) one
// each; an occupant group (0x00A96C0C) slot 3 (occupant) one, its +0x30 the
// list of templates; the lot manager (0x00AB37C0) slot 14 (occupant) one;
// an occupant slot 6 (AsPropertyHolder) none, slot 8 (position out) one,
// slots 1/2 AddRef/Release none; a property holder slot 9 (ID) one; a
// property slot 5 none; a variant slot 4 (count) and 103 (the uint32s)
// none; a controller (0x00A95E48 / 0x00A95F00) slots 2, 3, 4, 7, 8, 27
// none, 12 (8), 14 (occupant), 15 (template), 17 (position) one each; an
// automaton slot 2 none; a message (cGZMessage 0x00ACEC00) slots 1, 2 none,
// 5 (type) and 12 (the controller) one each; the message server
// (0x00B43CCC) slot 3 (message) one, slots 5 / 6 (target, message ID) two;
// the service 0x86AD10EC (0x00602600) slots 5 / 6 (target, 0xC0) two; the
// cheat codes (0x00B43CAC) slot 3 (ID, text) two, slot 4 (ID) one, slots 5
// / 6 (target, 0) two; the service 0x00B43C94 slot 21 none; the COM
// (RZGetCOM) slot 3 (class, interface, out) three; a stream (cIGZOStream /
// cIGZIStream) slots 7 (uint16), 6 (int16), 9 (uint32), 12 (float32) one,
// 18 GetError none (AL); a cRZBaseString slot 28 (0) one.
// Direct: SC4HashTable::Begin (out) ret 4, SC4Uint32HashTable::Find (out,
// key) ret 8, SC4KeyListTable::InsertUnique (out, pair) ret 8,
// SC4KeyListTable::Erase (iterator) ret 4, SC4KeyListTable::Clear none,
// SC4Uint32HashSet::Construct (100) ret 4, SC4Uint32HashSet::Resize (count)
// ret 4, SC4StdList::InsertRangeUint32 (where, first, last, tag) ret 0x10,
// SC4StdList::CopyConstructUint32 (source) ret 4, SC4List::Assign2
// (source) ret 4, SC4PairList::Destroy none, CopyAutomatonValue 0x00518A40
// (source) ret 4, the a2 functions 0x005163D0 (group, occupant, position)
// ret 0xC and 0x00517A90 / 0x005171D0 / 0x00517B70 (controller) ret 4, the
// part's own 0x00515400 ret 0xC, 0x00517010 none, 0x0051A160 ret 0x14,
// 0x0051A280 none, 0x0051A350 ret 8, 0x0051A6F0 none, 0x0051A7D0 ret 0xC,
// 0x0051A900 ret 8, 0x0051A9A0 none, 0x0051AC10 none, 0x0051B350 ret 8,
// 0x0051C090 none, cGZMessage::cGZMessage none, SC4String::FromText (text)
// ret 4; cdecl: SC4::stl_allocate (size), SC4::Free (pointer),
// RZ::operator_new (size), SC4::operator_delete (pointer), RZGetCOM,
// SC4::GetService86AD10EC, cRZBaseString::Sprintf (string, format, value),
// SC4StdList::DestroyListRange (first, last, tag), SC4Stream::
// {WriteReference (stream, object), ReadReference (stream, interface, out,
// 0), WriteVector3 / ReadVector3 (stream, vector), WriteBool (stream,
// byte), ReadBool (stream, out)}, the part's 0x00518550 / 0x00518580 /
// 0x00518620 (three; 0x00518580 and 0x00518620 also get the two unread
// words the game pushes), 0x005185C0 / 0x00518AC0 (four), 0x00518B20
// (three).
#include "core/automata_ctlmgr.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace opensc4::automata_ctlmgr {
namespace {

// ====================================================================================
// Helpers

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

// A list head as the game makes one: 12 bytes, linked to itself.
ListNode* NewHead()
{
    auto* const head = static_cast<ListNode*>(Allocate(0xC));
    head->next = head;
    head->prev = head;
    return head;
}

// SC4StdList::InsertRangeUint32 on the list object `list`.
void InsertRange(ListNode** list, ListNode* where, ListNode* first, ListNode* last, void* tag)
{
    CallThis<void>(addr::SC4StdList_InsertRangeUint32, list, where, first, last, tag);
}

// A list's nodes and its head freed, the way the game writes it inline (no
// call of SC4PairList::Destroy).
void FreeListInline(ListNode* head)
{
    for (ListNode* node = head->next; node != head;) {
        ListNode* const next = node->next;
        Free(node);
        node = next;
    }
    head->next = head;
    head->prev = head;
    Free(head);
}

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

uint32_t Buckets(const HashTable* table)
{
    const int32_t bytes = static_cast<int32_t>(reinterpret_cast<const char*>(table->end) -
                                               reinterpret_cast<const char*>(table->begin));
    return static_cast<uint32_t>(bytes >> 2);
}

// The first node of a table, the game's inline walk.
template <typename Node>
Node* FirstNode(const HashTable* table)
{
    for (uint32_t i = 0; i < Buckets(table); ++i)
        if (table->begin[i])
            return static_cast<Node*>(table->begin[i]);
    return nullptr;
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

// A string of the game (cRZBaseString, 0x14 bytes).
struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t references;
};

// The game's error text that nothing reads: an 8-byte buffer and the text
// formatted into it; FreeErrorText frees the buffer.
GameString MakeErrorText(std::uintptr_t format, uint32_t value)
{
    GameString text{};
    char* const buffer = static_cast<char*>(Allocate(8));
    text.begin = buffer;
    text.end = buffer;
    text.capacity = buffer + 8;
    buffer[0] = 0;
    text.vtable = addr::SC4String_Vtable;
    text.references = 0;
    CallCdecl<void>(addr::cRZBaseString_Sprintf, &text, reinterpret_cast<const char*>(format), value);
    return text;
}

void FreeErrorText(GameString& text)
{
    text.vtable = addr::SC4String_Vtable;
    if (text.begin)
        Free(text.begin);
}

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

void GridRemove(char* self, void* controller)
{
    CallThis<void>(addr::cSC4AutomataControllerManager_GridRemove, self + field::kGrid, controller);
}

// The messages Init registers and Shutdown unregisters, in order.
constexpr uint32_t kMessages[] = {0x99EF1142, 0x99EF1143, 0x26D31EC1, 0x89D3E9F3, 0x44778230, 0xAA1D07F3,
                                  0xA6B79602, 0xA6B79612, 0xAA375FE8, 0xCAD61E00, 0xEAD61E16, 0xEBFF18A3};
constexpr uint32_t kCheatCode = 0xABA2B5BB;
constexpr uint32_t kServiceFlags = 0xC0;
constexpr uint32_t kGroupsProperty = 0xAA1DD396;

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
// The object

// 0x0051AA50, the constructor.
char* __fastcall Construct(char* self, void*)
{
    static bool logged;
    FirstCall("Construct", logged);
    At<std::uintptr_t>(self, 0x4) = addr::SC4Audio_OldTargetBaseVtable;
    At<std::uintptr_t>(self, 0x8) = addr::cSC4TrafficSimulator_VtableBase4;
    At<std::uintptr_t>(self, 0xC) = addr::SC4SimGrid_VtableBase;
    At<std::uintptr_t>(self, 0x0) = addr::cSC4AutomataControllerManager_Vtable;
    At<std::uintptr_t>(self, 0x4) = addr::cSC4AutomataControllerManager_MessageVtable;
    At<std::uintptr_t>(self, 0x8) = addr::cSC4AutomataControllerManager_TargetVtable;
    At<std::uintptr_t>(self, 0xC) = addr::cSC4AutomataControllerManager_SerializableVtable;
    At<uint8_t>(self, 0x10) = 0;
    At<uint8_t>(self, 0x11) = 0;
    for (const std::size_t offset : {0x14u, 0x18u, 0x1Cu, 0x20u, 0x24u})
        At<uint32_t>(self, offset) = 0;
    At<uint8_t>(self, 0x28) = 0;
    At<uint8_t>(self, 0x29) = 0;
    At<uint8_t>(self, 0x2A) = 0;
    At<uint32_t>(self, 0x2C) = 0;
    At<uint32_t>(self, 0x30) = 0;
    At<uint32_t>(self, 0x34) = 0;
    At<uint16_t>(self, 0x38) = 0;
    At<uint32_t>(self, 0x3C) = 0;
    At<uint16_t>(self, 0x40) = 0;
    for (const std::size_t offset : {0x44u, 0x48u, 0x4Cu, 0x50u})
        At<uint32_t>(self, offset) = 0;
    At<uint8_t>(self, field::kByte54) = 0;
    At<uint8_t>(self, field::kByte55) = 1;
    for (const std::size_t table : {field::kControllers, field::kAutomata}) {
        HashTable* const t = reinterpret_cast<HashTable*>(self + table);
        t->begin = nullptr;
        t->end = nullptr;
        t->capacity = nullptr;
        t->count = 0;
        CallThis<void>(addr::SC4Uint32HashSet_Construct, t, 0x64u);
    }
    return self;
}

// 0x0051A280, the destructor (Release calls it).
void __fastcall Destructor(char* self, void*)
{
    static bool logged;
    FirstCall("Destructor", logged);
    At<std::uintptr_t>(self, 0x0) = addr::cSC4AutomataControllerManager_Vtable;
    At<std::uintptr_t>(self, 0x4) = addr::cSC4AutomataControllerManager_MessageVtable;
    At<std::uintptr_t>(self, 0x8) = addr::cSC4AutomataControllerManager_TargetVtable;
    At<std::uintptr_t>(self, 0xC) = addr::cSC4AutomataControllerManager_SerializableVtable;
    CallThis<void>(addr::SC4KeyListTable_Clear, Automata(self));
    if (void* const buckets = Automata(self)->begin)
        Free(buckets);
    CallThis<void>(addr::SC4KeyListTable_Clear, Controllers(self));
    if (void* const buckets = Controllers(self)->begin)
        Free(buckets);
    Grid* const grid = reinterpret_cast<Grid*>(self + field::kGrid);
    uint8_t tag = 0;
    CallCdecl<void>(addr::SC4StdList_DestroyListRange, grid->begin, grid->end, &tag);
    if (grid->begin)
        Free(grid->begin);
}

// 0x00514D10, slot 0.
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** out)
{
    static bool logged;
    FirstCall("QueryInterface", logged);
    char* volatile object = self; // the null branches call through it, as the game
    if (iid == kIid) {
        *out = object;
        Virtual<uint32_t>(object, 0x4);
        return 1;
    }
    if (iid == 1 || iid == kIidSerializable) {
        *out = object ? object + field::kSerializable : nullptr;
        Virtual<uint32_t>(object, 0x4);
        return 1;
    }
    if (iid == kIidMessageTarget) {
        *out = object ? object + field::kTargetPart : nullptr;
        Virtual<uint32_t>(object, 0x4);
        return 1;
    }
    return 0;
}

// 0x0051B1C0, slot 2.
uint32_t __fastcall Release(char* self, void*)
{
    static bool logged;
    FirstCall("Release", logged);
    const uint32_t references = At<uint32_t>(self, field::kReferences);
    if (references > 1) {
        At<uint32_t>(self, field::kReferences) = references - 1;
        return references - 1;
    }
    Virtual<uint8_t>(self, 0x10);
    CallThis<void>(addr::cSC4AutomataControllerManager_Destructor, self);
    CallCdecl<void>(addr::SC4_operator_delete, self);
    return 0;
}

// 0x0051C140, slot 3.
uint8_t __fastcall Init(char* self, void*)
{
    static bool logged;
    FirstCall("Init", logged);
    if (At<uint8_t>(self, field::kInitialized))
        return 1;
    CallThis<uint8_t>(addr::cSC4AutomataControllerManager_GridInit, self + field::kGrid);
    if (!At<void*>(self, field::kClock)) {
        void* const clock = Virtual<void*>(City(), 0x158);
        At<void*>(self, field::kClock) = clock;
        Virtual<uint32_t>(clock, 0x4);
    }
    if (!At<void*>(self, field::kScripts)) {
        void* const scripts = Virtual<void*>(City(), 0xC0);
        At<void*>(self, field::kScripts) = scripts;
        Virtual<uint32_t>(scripts, 0x4);
    }
    if (void* const server = Global<void*>(addr::SC4_MessageServer))
        for (const uint32_t message : kMessages)
            Virtual<uint8_t>(server, 0x14, self + field::kTargetPart, message);
    if (void* const service = CallCdecl<void*>(addr::SC4_GetService86AD10EC))
        Virtual<uint8_t>(service, 0x14, self + field::kTargetPart, kServiceFlags);
    if (void* const cheats = Global<void*>(addr::SC4_CheatCodes)) {
        if (Virtual<uint8_t>(Global<void*>(addr::SC4_Service43C94), 0x54)) {
            const char* const name = Global<const char*>(addr::cSC4AutomataControllerManager_CheatName);
            GameString text{};
            void* const made = CallThis<void*>(addr::SC4String_FromText, &text, name);
            Virtual<uint32_t>(cheats, 0xC, kCheatCode, made);
            text.vtable = addr::SC4String_Vtable;
            if (text.begin)
                Free(text.begin);
            Virtual<uint32_t>(cheats, 0x14, self + field::kMessagePart, 0u);
        }
    }
    At<uint8_t>(self, field::kInitialized) = 1;
    return 1;
}

// 0x0051B3E0, slot 4.
uint8_t __fastcall Shutdown(char* self, void*)
{
    static bool logged;
    FirstCall("Shutdown", logged);
    if (!At<uint8_t>(self, field::kInitialized))
        return 1;
    if (void* const cheats = Global<void*>(addr::SC4_CheatCodes)) {
        Virtual<uint32_t>(cheats, 0x10, kCheatCode);
        Virtual<uint32_t>(cheats, 0x18, self + field::kMessagePart, 0u);
    }
    if (void* const service = CallCdecl<void*>(addr::SC4_GetService86AD10EC))
        Virtual<uint8_t>(service, 0x18, self + field::kTargetPart, kServiceFlags);
    if (void* const server = Global<void*>(addr::SC4_MessageServer))
        for (const uint32_t message : kMessages)
            Virtual<uint8_t>(server, 0x18, self + field::kTargetPart, message);
    if (void* const scripts = At<void*>(self, field::kScripts)) {
        Virtual<uint32_t>(scripts, 0x8);
        At<void*>(self, field::kScripts) = nullptr;
    }
    if (void* const clock = At<void*>(self, field::kClock)) {
        Virtual<uint32_t>(clock, 0x8);
        At<void*>(self, field::kClock) = nullptr;
    }
    CallThis<void>(addr::cSC4AutomataControllerManager_ClearAutomata, self);
    CallThis<void>(addr::cSC4AutomataControllerManager_ClearControllers, self);
    At<uint8_t>(self, field::kInitialized) = 0;
    return 1;
}

// 0x0051AC10, Shutdown's helper: the grid's lists erased, the controller
// table emptied.
void __fastcall ClearControllers(char* self, void*)
{
    static bool logged;
    FirstCall("ClearControllers", logged);
    Grid* const grid = reinterpret_cast<Grid*>(self + field::kGrid);
    uint8_t tag = 0;
    ListNode** const end = grid->end;
    ListNode** const kept =
        CallCdecl<ListNode**>(addr::SC4ListHeadVector_Copy, end, end, grid->begin, &tag, 0u);
    CallCdecl<void>(addr::SC4StdList_DestroyListRange, kept, grid->end, &tag);
    grid->end = kept;
    Iterator it{};
    Begin(Controllers(self), &it);
    for (auto* node = static_cast<ControllerNode*>(it.node); node; node = NextNode(node, it.table)) {
        void* const occupant = node->occupant;
        for (ListNode* e = node->head->next; e != node->head; e = e->next) {
            void* const controller = e->value;
            Virtual<uint8_t>(controller, controller::kShutdown);
            Virtual<uint32_t>(controller, controller::kRelease);
        }
        for (ListNode* e = node->head->next; e != node->head;) {
            ListNode* const next = e->next;
            Free(e);
            e = next;
        }
        node->head->next = node->head;
        node->head->prev = node->head;
        if (occupant)
            Virtual<uint32_t>(occupant, 0x8);
    }
    CallThis<void>(addr::SC4KeyListTable_Clear, Controllers(self));
}

// 0x00517010, on an automaton value (node + 8): its automata released, the
// list emptied, the count 0.
void __fastcall ClearAutomatonList(AutomatonValue* value, void*)
{
    static bool logged;
    FirstCall("ClearAutomatonList", logged);
    for (ListNode* e = value->head->next; e != value->head; e = e->next)
        Virtual<uint32_t>(e->value, 0x8);
    for (ListNode* e = value->head->next; e != value->head;) {
        ListNode* const next = e->next;
        Free(e);
        e = next;
    }
    value->head->next = value->head;
    value->head->prev = value->head;
    value->count = 0;
}

// 0x0051A6F0, Shutdown's helper: the automaton table emptied.
void __fastcall ClearAutomata(char* self, void*)
{
    static bool logged;
    FirstCall("ClearAutomata", logged);
    Iterator it{};
    Begin(Automata(self), &it);
    for (auto* node = static_cast<AutomatonNode*>(it.node); node; node = NextNode(node, it.table)) {
        if (void* const occupant = node->occupant)
            Virtual<uint32_t>(occupant, 0x8);
        AutomatonValue& value = node->value;
        for (ListNode* e = value.head->next; e != value.head; e = e->next)
            Virtual<uint32_t>(e->value, 0x8);
        for (ListNode* e = value.head->next; e != value.head;) {
            ListNode* const next = e->next;
            Free(e);
            e = next;
        }
        value.head->next = value.head;
        value.head->prev = value.head;
        value.count = 0;
    }
    CallThis<void>(addr::SC4KeyListTable_Clear, Automata(self));
}

// ====================================================================================
// The grid

// 0x0051C090, on the grid (+0x1C).
uint8_t __fastcall GridInit(Grid* grid, void*)
{
    static bool logged;
    FirstCall("GridInit", logged);
    const int32_t cells = Virtual<int32_t>(City(), 0x174);
    const uint8_t side = static_cast<uint8_t>(cells / 8);
    grid->cellsX = side;
    grid->cellsZ = side;
    const uint32_t wanted = side;
    grid->shift = 0;
    for (;;) {
        uint8_t shift = grid->shift;
        if ((1u << (shift & 31)) == wanted)
            break;
        ++shift;
        grid->shift = shift;
        if (shift >= 0x20)
            break;
    }
    ListNode* value = NewHead();
    CallThis<void>(addr::SC4ListHeadVector_Resize, grid, wanted * wanted, &value);
    CallThis<void>(addr::SC4PairList_Destroy, &value);
    grid->sizeX = static_cast<float>(static_cast<int32_t>(static_cast<uint32_t>(cells) << 4));
    const int32_t cellsZ = Virtual<int32_t>(City(), 0x178);
    grid->sizeZ = static_cast<float>(static_cast<int32_t>(static_cast<uint32_t>(cellsZ) << 4));
    return 1;
}

// A bound clamped to [0, limit] and stored as a float (0x00515400's first
// three).
float ClampBound(double value, float limit)
{
    if (value < static_cast<double>(Global<float>(addr::SC4_FloatZero)))
        return 0.0f;
    if (value > static_cast<double>(limit))
        return limit;
    return static_cast<float>(value);
}

// A coordinate to its cell number, the game's rounding (see the header).
uint32_t CellOf(double scaled)
{
    const double half = Global<float>(addr::SC4_FloatHalf);
    const double magic = Global<float>(addr::SC4_FloatRoundMagicA95D58);
    const float rounded = static_cast<float>((scaled - half) + magic);
    const uint32_t n = std::bit_cast<uint32_t>(rounded) + 0xB4C00000u;
    const uint32_t up = n + 1;
    return scaled == static_cast<double>(static_cast<int32_t>(up)) ? up : n;
}

// 0x00515400, on the grid: the cells a square of `radius` around `position`
// covers.
void __fastcall GridCellRange(Grid* grid, void*, const float* position, uint32_t radiusBits, uint8_t* out)
{
    static bool logged;
    FirstCall("GridCellRange", logged);
    const float sizeX = Virtual<float>(City(), 0x164);
    const float sizeZ = Virtual<float>(City(), 0x164);
    const double radius = std::bit_cast<float>(radiusBits);
    const double x = position[0], z = position[2];
    const float x0 = ClampBound(x - radius, sizeX);
    const float z0 = ClampBound(z - radius, sizeZ);
    const float x1 = ClampBound(radius + x, sizeX);
    double z1 = radius + z;
    if (z1 < static_cast<double>(Global<float>(addr::SC4_FloatZero)))
        z1 = Global<float>(addr::SC4_FloatZero);
    else if (z1 > static_cast<double>(sizeZ))
        z1 = sizeZ;
    const double scale = Global<float>(addr::cSC4AutomataControllerManager_CellScale);
    out[0] = static_cast<uint8_t>(CellOf(scale * static_cast<double>(x0)));
    out[1] = static_cast<uint8_t>(CellOf(scale * static_cast<double>(z0)));
    out[2] = static_cast<uint8_t>(CellOf(scale * static_cast<double>(x1)));
    out[3] = static_cast<uint8_t>(CellOf(z1 * scale));
    const uint8_t lastX = static_cast<uint8_t>(grid->cellsX - 1);
    out[2] = lastX < out[2] ? lastX : out[2];
    const uint8_t lastZ = static_cast<uint8_t>(grid->cellsZ - 1);
    out[3] = lastZ >= out[3] ? out[3] : lastZ;
}

// ====================================================================================
// The vector-of-lists helpers (the grid's vector; a list object is its head
// pointer)

void Assign(ListNode** list, ListNode** source) { CallThis<void*>(addr::SC4List_Assign2, list, source); }

// A new list at `dest` copied from the list object `source`.
void CopyInto(ListNode** dest, ListNode* const* source, void* tag)
{
    *dest = nullptr;
    ListNode* const head = NewHead();
    *dest = head;
    InsertRange(dest, head->next, (*source)->next, *source, tag);
}

// 0x00518550.
void __cdecl Fill(ListNode** first, ListNode** last, ListNode** value)
{
    static bool logged;
    FirstCall("Fill", logged);
    for (ListNode** p = first; p != last; ++p)
        Assign(p, value);
}

// 0x00518580.
ListNode** __cdecl Copy(ListNode** first, ListNode** last, ListNode** dest)
{
    static bool logged;
    FirstCall("Copy", logged);
    const int32_t count = static_cast<int32_t>(reinterpret_cast<char*>(last) - reinterpret_cast<char*>(first)) >> 2;
    if (count <= 0)
        return dest;
    for (int32_t i = count; i != 0; --i) {
        Assign(dest, first);
        ++first;
        ++dest;
    }
    return dest;
}

// 0x00518620.
ListNode** __cdecl CopyBackward(ListNode** first, ListNode** last, ListNode** destEnd)
{
    static bool logged;
    FirstCall("CopyBackward", logged);
    const int32_t count = static_cast<int32_t>(reinterpret_cast<char*>(last) - reinterpret_cast<char*>(first)) >> 2;
    if (count <= 0)
        return destEnd;
    for (int32_t i = count; i != 0; --i) {
        --last;
        --destEnd;
        Assign(destEnd, last);
    }
    return destEnd;
}

// 0x005185C0.
ListNode** __cdecl UninitializedFillN(ListNode** dest, uint32_t count, ListNode** value, void*)
{
    static bool logged;
    FirstCall("UninitializedFillN", logged);
    for (uint32_t i = count; i != 0; --i) {
        if (dest)
            CopyInto(dest, value, &dest);
        ++dest;
    }
    return dest;
}

// 0x00518AC0.
ListNode** __cdecl UninitializedCopy(ListNode** first, ListNode** last, ListNode** dest, void*)
{
    static bool logged;
    FirstCall("UninitializedCopy", logged);
    for (; first != last; ++first) {
        if (dest)
            CopyInto(dest, first, &dest);
        ++dest;
    }
    return dest;
}

// 0x0051A160, (position, value, tag, n, at end): the vector regrown.
void __fastcall InsertOverflow(ListNode*** vector, void*, ListNode** position, ListNode** value, void*,
                               uint32_t count, uint32_t atEnd)
{
    static bool logged;
    FirstCall("InsertOverflow", logged);
    const uint32_t size = static_cast<uint32_t>(
        static_cast<int32_t>(reinterpret_cast<char*>(vector[1]) - reinterpret_cast<char*>(vector[0])) >> 2);
    const uint32_t length = size + (size < count ? count : size);
    auto* const fresh = length ? static_cast<ListNode**>(Allocate(length * 4)) : nullptr;
    ListNode** at =
        CallCdecl<ListNode**>(addr::SC4ListHeadVector_UninitializedCopy, vector[0], position, fresh, &atEnd);
    if (count == 1) {
        if (at)
            CopyInto(at, value, &atEnd);
        ++at;
    } else {
        at = CallCdecl<ListNode**>(addr::SC4ListHeadVector_UninitializedFillN, at, count, value, &atEnd);
    }
    if (!static_cast<uint8_t>(atEnd))
        at = CallCdecl<ListNode**>(addr::SC4ListHeadVector_UninitializedCopy, position, vector[1], at, &atEnd);
    CallCdecl<void>(addr::SC4StdList_DestroyListRange, vector[0], vector[1], &atEnd);
    if (vector[0])
        Free(vector[0]);
    vector[0] = fresh;
    vector[1] = at;
    vector[2] = fresh + length;
}

// 0x0051A7D0, (position, n, value).
void __fastcall FillInsert(ListNode*** vector, void*, ListNode** position, uint32_t count, ListNode** value)
{
    static bool logged;
    FirstCall("FillInsert", logged);
    if (!count)
        return;
    const uint32_t room = static_cast<uint32_t>(
        static_cast<int32_t>(reinterpret_cast<char*>(vector[2]) - reinterpret_cast<char*>(vector[1])) >> 2);
    if (room < count) {
        CallThis<void>(addr::SC4ListHeadVector_InsertOverflow, vector, position, value, &count, count, 0u);
        return;
    }
    ListNode* copy = nullptr;
    CallThis<void*>(addr::SC4StdList_CopyConstructUint32, &copy, value);
    ListNode** const oldEnd = vector[1];
    const uint32_t after = static_cast<uint32_t>(
        static_cast<int32_t>(reinterpret_cast<char*>(oldEnd) - reinterpret_cast<char*>(position)) >> 2);
    std::uintptr_t slot = count; // the game's count argument slot, reused as the tag
    if (after > count) {
        ListNode** const from = oldEnd - count;
        slot = reinterpret_cast<std::uintptr_t>(from);
        CallCdecl<ListNode**>(addr::SC4ListHeadVector_UninitializedCopy, from, oldEnd, oldEnd, &slot);
        vector[1] = vector[1] + count;
        CallCdecl<ListNode**>(addr::SC4ListHeadVector_CopyBackward, position, from, oldEnd, &slot, 0u);
        CallCdecl<void>(addr::SC4ListHeadVector_Fill, position, position + count, &copy);
    } else {
        const uint32_t extra = count - after;
        slot = extra;
        CallCdecl<ListNode**>(addr::SC4ListHeadVector_UninitializedFillN, oldEnd, extra, &copy, &slot);
        ListNode** const newEnd = vector[1] + extra;
        vector[1] = newEnd;
        CallCdecl<ListNode**>(addr::SC4ListHeadVector_UninitializedCopy, position, oldEnd, newEnd, &slot);
        vector[1] = vector[1] + after;
        CallCdecl<void>(addr::SC4ListHeadVector_Fill, position, oldEnd, &copy);
    }
    CallThis<void>(addr::SC4PairList_Destroy, &copy);
}

// 0x0051B350, (n, value).
void __fastcall Resize(ListNode*** vector, void*, uint32_t count, ListNode** value)
{
    static bool logged;
    FirstCall("Resize", logged);
    ListNode** const end = vector[1];
    const int32_t bytes = static_cast<int32_t>(reinterpret_cast<char*>(end) - reinterpret_cast<char*>(vector[0]));
    const uint32_t size = static_cast<uint32_t>(bytes >> 2);
    if (count < size) {
        ListNode** const kept =
            CallCdecl<ListNode**>(addr::SC4ListHeadVector_Copy, end, end, vector[0] + count, &count, 0u);
        CallCdecl<void>(addr::SC4StdList_DestroyListRange, kept, vector[1], &count);
        vector[1] = kept;
        return;
    }
    CallThis<void>(addr::SC4ListHeadVector_FillInsert, vector, end, count - size, value);
}

// ====================================================================================
// The controller template base (vtable 0x00A960BC)

// 0x00518B20, (first, last, tag): the template's 0x7C-byte records.
void __cdecl DestroyRecords(char* first, char* last, void*)
{
    static bool logged;
    FirstCall("DestroyRecords", logged);
    for (char* record = first; record != last; record += 0x7C) {
        if (void* const block = At<void*>(record, 0x1C))
            Free(block);
        char* const end = At<char*>(record, 0x14);
        for (char* text = At<char*>(record, 0x10); text != end; text += 0x14)
            Virtual<void*>(text, 0x70, 0u);
        if (void* const texts = At<void*>(record, 0x10))
            Free(texts);
    }
}

// 0x0051A9A0, the destructor.
void __fastcall TemplateDestructor(char* self, void*)
{
    static bool logged;
    FirstCall("TemplateDestructor", logged);
    At<std::uintptr_t>(self, 0x0) = addr::cSC4AutomataControllerTemplate_BaseVtable;
    if (void* const block = At<void*>(self, 0x6C))
        Free(block);
    if (void* const block = At<void*>(self, 0x60))
        Free(block);
    CallThis<void>(addr::SC4PairList_Destroy, self + 0x48);
    CallThis<void>(addr::SC4PairList_Destroy, self + 0x44);
    uint8_t tag = 0;
    CallCdecl<void>(addr::cSC4AutomataControllerTemplate_DestroyRecords, At<char*>(self, 0x38), At<char*>(self, 0x3C),
                    &tag);
    if (void* const block = At<void*>(self, 0x38))
        Free(block);
    if (void* const block = At<void*>(self, 0x24))
        Free(block);
    At<std::uintptr_t>(self, 0x4) = addr::SC4String_Vtable;
    if (void* const text = At<void*>(self, 0x8))
        Free(text);
}

// 0x0051AA30, slot 0 of 0x00A960BC (flags).
char* __fastcall TemplateDeletingDestructor(char* self, void*, uint32_t flags)
{
    static bool logged;
    FirstCall("TemplateDeletingDestructor", logged);
    CallThis<void>(addr::cSC4AutomataControllerTemplate_Destructor, self);
    if (static_cast<uint8_t>(flags) & 1)
        CallCdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// ====================================================================================
// Adding

// 0x0051AD20, slot 6 (occupant, template, position).
void* __fastcall AddControllerFromTemplate(char* self, void*, void* occupant, char* tmpl, const float* position)
{
    static bool logged;
    FirstCall("AddControllerFromTemplate", logged);
    void* const lots = Virtual<void*>(City(), 0x84);
    void* controller = nullptr;
    if (tmpl) {
        if (!(At<uint8_t>(tmpl, tmpl::kFlags) & tmpl::kAnyLot) && occupant) {
            if (void* const lot = Virtual<void*>(lots, 0x38, occupant)) {
                Iterator it{};
                Begin(Controllers(self), &it);
                for (auto* node = static_cast<ControllerNode*>(it.node); node; node = NextNode(node, it.table)) {
                    void* const other = node->occupant ? Virtual<void*>(lots, 0x38, node->occupant) : nullptr;
                    if (other != lot)
                        continue;
                    for (ListNode* e = node->head->next; e != node->head; e = e->next)
                        if (Virtual<void*>(e->value, controller::kTemplate) == tmpl)
                            return nullptr;
                }
            }
        }
        // `fcomp`, `test ah,1 / jne`: below 1 or unordered skips the grid.
        if (static_cast<double>(At<float>(tmpl, tmpl::kRadius)) >=
            static_cast<double>(Global<float>(addr::SC4_FloatOneA81228))) {
            const uint32_t zero = Global<uint32_t>(addr::SC4_ZeroWord);
            uint32_t where[3] = {zero, zero, zero};
            if (occupant) {
                Virtual<uint8_t>(occupant, 0x20, reinterpret_cast<float*>(where));
            } else {
                const auto* const given = reinterpret_cast<const uint32_t*>(position);
                where[0] = given[0];
                where[1] = given[1];
                where[2] = given[2];
            }
            Grid* const grid = reinterpret_cast<Grid*>(self + field::kGrid);
            uint8_t range[4] = {};
            CallThis<void>(addr::cSC4AutomataControllerManager_GridCellRange, grid,
                           reinterpret_cast<const float*>(where), At<uint32_t>(tmpl, tmpl::kRadius),
                           static_cast<uint8_t*>(range));
            bool found = false;
            if (!(range[1] > range[3])) {
                uint8_t z = range[1];
                do {
                    if (!(range[0] > range[2])) {
                        uint8_t x = range[0];
                        do {
                            const uint8_t lastX = static_cast<uint8_t>(grid->cellsX - 1);
                            const uint8_t cellX = lastX < x ? lastX : x;
                            const uint8_t lastZ = static_cast<uint8_t>(grid->cellsZ - 1);
                            const uint8_t cellZ = lastZ < z ? lastZ : z;
                            const uint32_t index = (static_cast<uint32_t>(cellX) << (grid->shift & 31)) + cellZ;
                            ListNode* const head = grid->begin[index];
                            for (ListNode* e = head->next; e != head; e = e->next)
                                if (e->value && Virtual<void*>(e->value, controller::kTemplate) == tmpl) {
                                    found = true;
                                    break;
                                }
                            x = static_cast<uint8_t>(x + 1);
                        } while (x <= range[2]);
                    }
                    z = static_cast<uint8_t>(z + 1);
                } while (z <= range[3]);
                if (found)
                    return nullptr;
            }
        }
    }
    void* const com = CallCdecl<void*>(addr::RZGetCOM);
    if (!Virtual<uint8_t>(com, 0xC, At<uint32_t>(tmpl, tmpl::kClassId), kIidController, &controller)) {
        GameString text =
            MakeErrorText(addr::cSC4AutomataControllerManager_NoControllerFormat, At<uint32_t>(tmpl, tmpl::kClassId));
        FreeErrorText(text);
        return controller;
    }
    HashTable* const table = Controllers(self);
    auto* node = FindNode<ControllerNode>(table, occupant);
    if (!node) {
        ListNode* temporary = NewHead();
        struct {
            void* key;
            ListNode* list;
        } pair{occupant, nullptr};
        ListNode* const head = NewHead();
        pair.list = head;
        InsertRange(&pair.list, head, temporary->next, temporary, &occupant);
        CallThis<void>(addr::SC4Uint32HashSet_Resize, table, table->count + 1);
        InsertResult result{};
        CallThis<void>(addr::SC4KeyListTable_InsertUnique, table, &result, &pair);
        CallThis<void>(addr::SC4PairList_Destroy, &pair.list);
        CallThis<void>(addr::SC4PairList_Destroy, &temporary);
        if (!result.inserted) {
            GameString text = MakeErrorText(addr::cSC4AutomataControllerManager_NoEntryFormat,
                                            static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(occupant)));
            Virtual<uint32_t>(controller, controller::kRelease);
            FreeErrorText(text);
            return nullptr;
        }
        if (occupant)
            Virtual<uint32_t>(occupant, 0x4);
        node = static_cast<ControllerNode*>(result.node);
    }
    PushBack(node->head, controller);
    if (occupant)
        Virtual<uint32_t>(controller, controller::kSetOccupant, occupant);
    else if (position)
        Virtual<uint32_t>(controller, controller::kSetPosition, position);
    Virtual<uint32_t>(controller, controller::kSetTemplate, tmpl);
    Virtual<uint8_t>(controller, controller::kInit);
    CallThis<void>(addr::cSC4AutomataControllerManager_GridAdd, self + field::kGrid, controller);
    return controller;
}

// 0x00514CA0, slot 5 (occupant, name, position).
void* __fastcall AddControllerFromTemplateName(char* self, void*, void* occupant, void* name, const float* position)
{
    static bool logged;
    FirstCall("AddControllerFromTemplateName", logged);
    void* const found = Virtual<void*>(At<void*>(self, field::kScripts), 0x44, name);
    if (!found)
        return nullptr;
    return Virtual<void*>(self, 0x18, occupant, found, position);
}

// 0x00517190, slot 9 (group ID, occupant, position).
uint32_t __fastcall AddControllersFromOccupantGroupByID(char* self, void*, uint32_t id, void* occupant, float* position)
{
    static bool logged;
    FirstCall("AddControllersFromOccupantGroupByID", logged);
    void* const scripts = At<void*>(self, field::kScripts);
    if (!scripts)
        return 0;
    void* const group = Virtual<void*>(scripts, 0x2C, id);
    if (!group)
        return 0;
    return CallThis<uint32_t>(addr::cSC4AutomataControllerManager_AddControllersFromGroup, self, group, occupant,
                              position);
}

// 0x00517150, slot 10 (group name, occupant, position).
uint32_t __fastcall AddControllersFromOccupantGroupByName(char* self, void*, void* name, void* occupant,
                                                         float* position)
{
    static bool logged;
    FirstCall("AddControllersFromOccupantGroupByName", logged);
    void* const scripts = At<void*>(self, field::kScripts);
    if (!scripts)
        return 0;
    void* const group = Virtual<void*>(scripts, 0x30, name);
    if (!group)
        return 0;
    return CallThis<uint32_t>(addr::cSC4AutomataControllerManager_AddControllersFromGroup, self, group, occupant,
                              position);
}

// 0x00517060, slot 13 (occupant).
uint8_t __fastcall AddControllers(char* self, void*, void* occupant)
{
    static bool logged;
    FirstCall("AddControllers", logged);
    if (!occupant)
        return 1;
    void* const holder = Virtual<void*>(occupant, 0x18);
    if (void* const property = Virtual<void*>(holder, 0x24, kGroupsProperty)) {
        void* const value = Virtual<void*>(property, 0x14);
        const uint32_t* const ids = Virtual<const uint32_t*>(value, 0x19C);
        for (uint32_t i = 0; i < Virtual<uint32_t>(value, 0x10); ++i)
            if (void* const group = Virtual<void*>(At<void*>(self, field::kScripts), 0x2C, ids[i]))
                CallThis<uint32_t>(addr::cSC4AutomataControllerManager_AddControllersFromGroup, self, group,
                                   occupant, static_cast<float*>(nullptr));
    }
    ListNode** const groups = Virtual<ListNode**>(At<void*>(self, field::kScripts), 0x28);
    for (ListNode* n = (*groups)->next; n != *groups; n = n->next) {
        char* const group = static_cast<char*>(n->value);
        if (!Virtual<uint8_t>(group, 0xC, occupant))
            continue;
        for (ListNode* t = At<ListNode*>(group, 0x30)->next; t != At<ListNode*>(group, 0x30); t = t->next)
            Virtual<void*>(self, 0x18, occupant, t->value, static_cast<const float*>(nullptr));
    }
    return 1;
}

// ====================================================================================
// Removing and moving

// 0x00517880, slot 7 (controller).
uint8_t __fastcall RemoveController(char* self, void*, void* target)
{
    static bool logged;
    FirstCall("RemoveController", logged);
    uint8_t removed = 0;
    if (!target)
        return removed;
    Iterator it{};
    Begin(Controllers(self), &it);
    for (auto* node = static_cast<ControllerNode*>(it.node); node; node = NextNode(node, it.table)) {
        ListNode* const head = node->head;
        ListNode* e = head->next;
        while (e != head && e->value != target)
            e = e->next;
        if (e == head)
            continue;
        ListNode* const next = e->next;
        ListNode* const prev = e->prev;
        prev->next = next;
        removed = 1;
        next->prev = prev;
        Free(e);
        SendRemoved(Global<void*>(addr::SC4_MessageServer), target);
        GridRemove(self, target);
        Virtual<uint8_t>(target, controller::kShutdown);
        Virtual<uint32_t>(target, controller::kRelease);
    }
    return removed;
}

// 0x0051A350, (group or 0, occupant).
uint8_t __fastcall RemoveControllersFromOccupant(char* self, void*, char* group, void* occupant)
{
    static bool logged;
    FirstCall("RemoveControllersFromOccupant", logged);
    uint8_t removed = 0;
    HashTable* const table = Controllers(self);
    auto* const node = FindNode<ControllerNode>(table, occupant);
    if (!node)
        return removed;
    for (ListNode* e = node->head->next; e != node->head;) {
        void* const controller = e->value;
        if (group) {
            void* const tmpl = Virtual<void*>(controller, controller::kTemplate);
            ListNode* const templates = At<ListNode*>(group, 0x30);
            ListNode* t = templates->next;
            while (t != templates && t->value != tmpl)
                t = t->next;
            if (t == templates) {
                e = e->next;
                continue;
            }
        }
        SendRemoved(Global<void*>(addr::SC4_MessageServer), controller);
        GridRemove(self, controller);
        Virtual<uint8_t>(controller, controller::kShutdown);
        Virtual<uint32_t>(controller, controller::kRelease);
        ListNode* const next = e->next;
        ListNode* const prev = e->prev;
        prev->next = next;
        removed = 1;
        next->prev = prev;
        Free(e);
        e = next;
    }
    if (node->head->next != node->head)
        return removed;
    void* key = occupant;
    Iterator found{};
    CallThis<void>(addr::SC4Uint32HashTable_Find, Automata(self), &found, &key);
    if (found.node) {
        CallThis<void>(addr::cSC4AutomataControllerManager_ClearAutomatonList,
                       &static_cast<AutomatonNode*>(found.node)->value);
        Virtual<uint32_t>(occupant, 0x8);
        Iterator at{found.node, found.table};
        CallThis<void>(addr::SC4KeyListTable_Erase, Automata(self), &at);
    }
    Iterator at{node, table};
    CallThis<void>(addr::SC4KeyListTable_Erase, table, &at);
    Virtual<uint32_t>(occupant, 0x8);
    return removed;
}

// 0x0051A520, slot 14 (occupant).
uint32_t __fastcall RemoveControllers(char* self, void*, void* occupant)
{
    static bool logged;
    FirstCall("RemoveControllers", logged);
    return CallThis<uint32_t>(addr::cSC4AutomataControllerManager_RemoveControllersFromOccupant, self,
                              static_cast<void*>(nullptr), occupant);
}

// 0x0051AB40, slot 11 (group ID, occupant).
uint32_t __fastcall RemoveControllersFromOccupantGroupByID(char* self, void*, uint32_t id, void* occupant)
{
    static bool logged;
    FirstCall("RemoveControllersFromOccupantGroupByID", logged);
    void* const scripts = At<void*>(self, field::kScripts);
    if (!scripts)
        return 0;
    void* const group = Virtual<void*>(scripts, 0x2C, id);
    if (!group)
        return 0;
    return CallThis<uint32_t>(addr::cSC4AutomataControllerManager_RemoveControllersFromOccupant, self, group,
                              occupant);
}

// 0x0051AB00, slot 12 (group name, occupant).
uint32_t __fastcall RemoveControllersFromOccupantGroupByName(char* self, void*, void* name, void* occupant)
{
    static bool logged;
    FirstCall("RemoveControllersFromOccupantGroupByName", logged);
    void* const scripts = At<void*>(self, field::kScripts);
    if (!scripts)
        return 0;
    void* const group = Virtual<void*>(scripts, 0x30, name);
    if (!group)
        return 0;
    return CallThis<uint32_t>(addr::cSC4AutomataControllerManager_RemoveControllersFromOccupant, self, group,
                              occupant);
}

// 0x0051A530, slot 8 (occupant, template name): answers 0 always.
uint8_t __fastcall RemoveControllerByName(char* self, void*, void* occupant, char* name)
{
    static bool logged;
    FirstCall("RemoveControllerByName", logged);
    HashTable* const table = Controllers(self);
    auto* const node = FindNode<ControllerNode>(table, occupant);
    if (!node)
        return 0;
    for (ListNode* e = node->head->next; e != node->head;) {
        char* const* const wanted = name ? reinterpret_cast<char* const*>(name + 4) : nullptr;
        void* const controller = e->value;
        char* const text = Virtual<char*>(controller, controller::kName);
        char* const* const have = text ? reinterpret_cast<char* const*>(text + 4) : nullptr;
        const std::ptrdiff_t haveLength = have[1] - have[0];
        const std::ptrdiff_t wantedLength = wanted[1] - wanted[0];
        if (haveLength != wantedLength ||
            std::memcmp(have[0], wanted[0], static_cast<std::size_t>(static_cast<uint32_t>(haveLength))) != 0) {
            e = e->next;
            continue;
        }
        ListNode* const next = e->next;
        ListNode* const prev = e->prev;
        prev->next = next;
        next->prev = prev;
        Free(e);
        e = next;
        SendRemoved(Global<void*>(addr::SC4_MessageServer), controller);
        GridRemove(self, controller);
        Virtual<uint8_t>(controller, controller::kShutdown);
        Virtual<uint32_t>(controller, controller::kRelease);
    }
    if (node->head->next != node->head)
        return 0;
    Iterator at{node, table};
    CallThis<void>(addr::SC4KeyListTable_Erase, table, &at);
    if (occupant)
        Virtual<uint32_t>(occupant, 0x8);
    void* key = occupant;
    Iterator found{};
    CallThis<void>(addr::SC4Uint32HashTable_Find, Automata(self), &found, &key);
    if (found.node) {
        CallThis<void>(addr::cSC4AutomataControllerManager_ClearAutomatonList,
                       &static_cast<AutomatonNode*>(found.node)->value);
        if (occupant)
            Virtual<uint32_t>(occupant, 0x8);
        Iterator at2{found.node, found.table};
        CallThis<void>(addr::SC4KeyListTable_Erase, Automata(self), &at2);
    }
    return 0;
}

// 0x005188C0, slot 15 (occupant).
uint8_t __fastcall MoveControllers(char* self, void*, void* occupant)
{
    static bool logged;
    FirstCall("MoveControllers", logged);
    if (auto* const node = FindNode<ControllerNode>(Controllers(self), occupant))
        for (ListNode* e = node->head->next; e != node->head; e = e->next) {
            void* const controller = e->value;
            CallThis<void>(addr::cSC4AutomataControllerManager_GridMove, self + field::kGrid, controller);
            Virtual<uint32_t>(controller, controller::kMoved);
        }
    return 1;
}

// 0x00518930, slot 16 (controller).
uint8_t __fastcall MoveController(char* self, void*, void* target)
{
    static bool logged;
    FirstCall("MoveController", logged);
    if (target) {
        CallThis<void>(addr::cSC4AutomataControllerManager_GridMove, self + field::kGrid, target);
        Virtual<uint32_t>(target, controller::kMoved);
    }
    return 1;
}

// ====================================================================================
// The save

// 0x0051A300, the serializable part's slot 5.
uint32_t __fastcall GetGZCLSID(char*, void*)
{
    static bool logged;
    FirstCall("GetGZCLSID", logged);
    return kClassId;
}

// 0x0051A900, on the automaton table (out, pair).
InsertResult* __fastcall InsertAutomatonEntry(HashTable* table, void*, InsertResult* out, const char* pair)
{
    static bool logged;
    FirstCall("InsertAutomatonEntry", logged);
    void* const key = At<void*>(const_cast<char*>(pair), 0);
    const uint32_t bucket = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(key)) % Buckets(table);
    auto* const first = static_cast<AutomatonNode*>(table->begin[bucket]);
    for (AutomatonNode* node = first; node; node = node->next)
        if (node->occupant == key) {
            out->table = table;
            out->node = node;
            out->inserted = 0;
            return out;
        }
    auto* const node = static_cast<AutomatonNode*>(Allocate(sizeof(AutomatonNode)));
    node->next = nullptr;
    // The game tests node + 4 against 0 first (never 0).
    node->occupant = At<void*>(const_cast<char*>(pair), 0);
    CallThis<void*>(addr::cSC4AutomataControllerManager_CopyAutomatonValue, &node->value, pair + 4);
    node->next = first;
    table->begin[bucket] = node;
    ++table->count;
    out->node = node;
    out->table = table;
    out->inserted = 1;
    return out;
}

void WriteReference(void* stream, void* object) { CallCdecl<uint8_t>(addr::SC4Stream_WriteReference, stream, object); }

bool Saved(void* controller) { return !Virtual<uint8_t>(controller, controller::kTestFlag, 8u); }

// 0x005174D0, the serializable part's slot 3 (stream); `part` is the
// object + 0xC.
uint32_t __fastcall Write(char* part, void*, void* stream)
{
    static bool logged;
    FirstCall("Write", logged);
    char* const self = part - field::kSerializable;
    Virtual<uint8_t>(stream, 0x1C, 1u);
    WriteReference(stream, At<void*>(self, field::kClock));
    Virtual<uint8_t>(stream, 0x30, At<uint32_t>(self, field::kTimeA));
    Virtual<uint8_t>(stream, 0x18, static_cast<uint32_t>(At<uint16_t>(self, field::kTimeB)));
    Virtual<uint8_t>(stream, 0x30, At<uint32_t>(self, field::kTimeC));
    Virtual<uint8_t>(stream, 0x18, static_cast<uint32_t>(At<uint16_t>(self, field::kTimeD)));
    HashTable* const controllers = Controllers(self);
    uint32_t occupants = 0;
    Iterator it{};
    Begin(controllers, &it);
    for (auto* node = static_cast<ControllerNode*>(it.node); node; node = NextNode(node, it.table)) {
        ListNode* e = node->head->next;
        while (e != node->head && !Saved(e->value))
            e = e->next;
        if (e != node->head)
            ++occupants;
    }
    Virtual<uint8_t>(stream, 0x24, occupants);
    for (auto* node = FirstNode<ControllerNode>(controllers); node; node = NextNode(node, controllers)) {
        void* const occupant = node->occupant;
        uint32_t count = 0;
        for (ListNode* e = node->head->next; e != node->head; e = e->next)
            if (Saved(e->value))
                ++count;
        if (!count)
            continue;
        WriteReference(stream, occupant);
        Virtual<uint8_t>(stream, 0x24, count);
        for (ListNode* e = node->head->next; e != node->head; e = e->next) {
            void* const controller = e->value;
            if (Saved(controller))
                WriteReference(stream, controller);
        }
    }
    HashTable* const automata = Automata(self);
    Virtual<uint8_t>(stream, 0x24, automata->count);
    for (auto* node = FirstNode<AutomatonNode>(automata); node; node = NextNode(node, automata)) {
        WriteReference(stream, node->occupant);
        uint32_t count = 0;
        for (ListNode* e = node->value.head->next; e != node->value.head; e = e->next)
            ++count;
        Virtual<uint8_t>(stream, 0x24, count);
        for (ListNode* e = node->value.head->next; e != node->value.head; e = e->next)
            WriteReference(stream, e->value);
        CallCdecl<void*>(addr::SC4Stream_WriteVector3, stream, reinterpret_cast<const float*>(node->value.first));
        CallCdecl<void*>(addr::SC4Stream_WriteVector3, stream, reinterpret_cast<const float*>(node->value.second));
        CallCdecl<void*>(addr::SC4Stream_WriteBool, stream, static_cast<uint32_t>(node->value.flag));
    }
    return Virtual<uint8_t>(stream, 0x48) == 0 ? 1u : 0u;
}

uint8_t ReadReference(void* stream, uint32_t iid, void** out)
{
    return CallCdecl<uint8_t>(addr::SC4Stream_ReadReference, stream, iid, out, 0u);
}

// 0x0051B520, the serializable part's slot 4 (stream).
uint32_t __fastcall Read(char* part, void*, void* stream)
{
    static bool logged;
    FirstCall("Read", logged);
    char* const self = part - field::kSerializable;
    At<uint8_t>(self, field::kLoaded) = 1;
    uint16_t version = 0;
    Virtual<uint8_t>(stream, 0x1C, &version);
    ReadReference(stream, kIidClock, &At<void*>(self, field::kClock));
    Virtual<uint8_t>(stream, 0x30, &At<float>(self, field::kTimeA));
    Virtual<uint8_t>(stream, 0x18, &At<int16_t>(self, field::kTimeB));
    Virtual<uint8_t>(stream, 0x30, &At<float>(self, field::kTimeC));
    Virtual<uint8_t>(stream, 0x18, &At<int16_t>(self, field::kTimeD));
    // The game's stack slots: the outer count (both tables), the inner
    // count (both tables), the last byte read.
    uint32_t outer = 0, inner = 0;
    uint8_t byte = 0;
    HashTable* const controllers = Controllers(self);
    Virtual<uint8_t>(stream, 0x24, &outer);
    for (uint32_t i = 0; i < outer; ++i) {
        void* occupant = nullptr;
        ReadReference(stream, kIidOccupant, &occupant);
        void* controller = nullptr;
        ListNode* const temporary = NewHead();
        struct {
            void* key;
            ListNode* list;
        } pair{occupant, nullptr};
        ListNode* const head = NewHead();
        pair.list = head;
        InsertRange(&pair.list, head, temporary->next, temporary, &byte);
        CallThis<void>(addr::SC4Uint32HashSet_Resize, controllers, controllers->count + 1);
        InsertResult result{};
        CallThis<void>(addr::SC4KeyListTable_InsertUnique, controllers, &result, &pair);
        FreeListInline(pair.list);
        FreeListInline(temporary);
        ListNode** const list =
            result.inserted ? &static_cast<ControllerNode*>(result.node)->head : static_cast<ListNode**>(controller);
        Virtual<uint8_t>(stream, 0x24, &inner);
        for (uint32_t j = 0; j < inner; ++j) {
            controller = nullptr;
            ReadReference(stream, kIidController, &controller);
            if (list)
                PushBack(*list, controller);
        }
    }
    HashTable* const automata = Automata(self);
    Virtual<uint8_t>(stream, 0x24, &outer);
    for (uint32_t i = 0; i < outer; ++i) {
        void* occupant = nullptr;
        ReadReference(stream, kIidOccupant, &occupant);
        void* automaton = nullptr;
        ListNode* const temporary = NewHead();
        const uint32_t zero = Global<uint32_t>(addr::SC4_ZeroWord);
        struct {
            void* key;
            AutomatonValue value;
        } pair{};
        pair.key = occupant;
        ListNode* const head = NewHead();
        pair.value.head = head;
        InsertRange(&pair.value.head, head, temporary->next, temporary, &byte);
        pair.value.count = 0; // the game: a stack leftover (overwritten or unread)
        for (int k = 0; k < 3; ++k) {
            pair.value.first[k] = zero;
            pair.value.second[k] = zero;
        }
        pair.value.flag = 0; // the game: a stack leftover (overwritten or unread)
        CallThis<void>(addr::SC4Uint32HashSet_Resize, automata, automata->count + 1);
        InsertResult result{};
        CallThis<InsertResult*>(addr::cSC4AutomataControllerManager_InsertAutomatonEntry, automata, &result, &pair);
        FreeListInline(pair.value.head);
        FreeListInline(temporary);
        AutomatonValue* const value = result.inserted ? &static_cast<AutomatonNode*>(result.node)->value
                                                      : static_cast<AutomatonValue*>(automaton);
        Virtual<uint8_t>(stream, 0x24, &inner);
        if (value)
            value->count = inner;
        for (uint32_t j = 0; j < inner; ++j) {
            automaton = nullptr;
            ReadReference(stream, kIidAutomaton, &automaton);
            if (value)
                PushBack(value->head, automaton);
        }
        const uint32_t again = Global<uint32_t>(addr::SC4_ZeroWord);
        uint32_t vector[3] = {again, again, again};
        CallCdecl<void*>(addr::SC4Stream_ReadVector3, stream, reinterpret_cast<float*>(vector));
        if (value)
            std::memcpy(value->first, vector, sizeof vector);
        CallCdecl<void*>(addr::SC4Stream_ReadVector3, stream, reinterpret_cast<float*>(vector));
        if (value)
            std::memcpy(value->second, vector, sizeof vector);
        CallCdecl<uint8_t>(addr::SC4Stream_ReadBool, stream, &byte);
        if (value)
            value->flag = byte;
    }
    return Virtual<uint8_t>(stream, 0x48) == 0 ? 1u : 0u;
}

// ====================================================================================
// Registration

#define OPENSC4_DETOUR(address, fn) {address, reinterpret_cast<void*>(&fn)}

const Detour kControllerManager[] = {
    OPENSC4_DETOUR(addr::cSC4AutomataControllerManager_Construct, Construct),
    OPENSC4_DETOUR(addr::cSC4AutomataControllerManager_Destructor, Destructor),
    OPENSC4_DETOUR(addr::cSC4AutomataControllerManager_QueryInterface, QueryInterface),
    OPENSC4_DETOUR(addr::cSC4AutomataControllerManager_Release, Release),
    OPENSC4_DETOUR(addr::cSC4AutomataControllerManager_Init, Init),
    OPENSC4_DETOUR(addr::cSC4AutomataControllerManager_Shutdown, Shutdown),
    OPENSC4_DETOUR(addr::cSC4AutomataControllerManager_ClearControllers, ClearControllers),
    OPENSC4_DETOUR(addr::cSC4AutomataControllerManager_ClearAutomatonList, ClearAutomatonList),
    OPENSC4_DETOUR(addr::cSC4AutomataControllerManager_ClearAutomata, ClearAutomata),
    OPENSC4_DETOUR(addr::cSC4AutomataControllerManager_GridInit, GridInit),
    OPENSC4_DETOUR(addr::cSC4AutomataControllerManager_GridCellRange, GridCellRange),
    OPENSC4_DETOUR(addr::SC4ListHeadVector_Fill, Fill),
    OPENSC4_DETOUR(addr::SC4ListHeadVector_Copy, Copy),
    OPENSC4_DETOUR(addr::SC4ListHeadVector_CopyBackward, CopyBackward),
    OPENSC4_DETOUR(addr::SC4ListHeadVector_UninitializedFillN, UninitializedFillN),
    OPENSC4_DETOUR(addr::SC4ListHeadVector_UninitializedCopy, UninitializedCopy),
    OPENSC4_DETOUR(addr::SC4ListHeadVector_InsertOverflow, InsertOverflow),
    OPENSC4_DETOUR(addr::SC4ListHeadVector_FillInsert, FillInsert),
    OPENSC4_DETOUR(addr::SC4ListHeadVector_Resize, Resize),
    OPENSC4_DETOUR(addr::cSC4AutomataControllerTemplate_DestroyRecords, DestroyRecords),
    OPENSC4_DETOUR(addr::cSC4AutomataControllerTemplate_Destructor, TemplateDestructor),
    OPENSC4_DETOUR(addr::cSC4AutomataControllerTemplate_DeletingDestructor, TemplateDeletingDestructor),
    OPENSC4_DETOUR(addr::cSC4AutomataControllerManager_AddControllerFromTemplate, AddControllerFromTemplate),
    OPENSC4_DETOUR(addr::cSC4AutomataControllerManager_AddControllerFromTemplateName, AddControllerFromTemplateName),
    OPENSC4_DETOUR(addr::cSC4AutomataControllerManager_AddControllersFromOccupantGroupByID,
                   AddControllersFromOccupantGroupByID),
    OPENSC4_DETOUR(addr::cSC4AutomataControllerManager_AddControllersFromOccupantGroupByName,
                   AddControllersFromOccupantGroupByName),
    OPENSC4_DETOUR(addr::cSC4AutomataControllerManager_AddControllers, AddControllers),
    OPENSC4_DETOUR(addr::cSC4AutomataControllerManager_RemoveController, RemoveController),
    OPENSC4_DETOUR(addr::cSC4AutomataControllerManager_RemoveControllersFromOccupant, RemoveControllersFromOccupant),
    OPENSC4_DETOUR(addr::cSC4AutomataControllerManager_RemoveControllers, RemoveControllers),
    OPENSC4_DETOUR(addr::cSC4AutomataControllerManager_RemoveControllersFromOccupantGroupByID,
                   RemoveControllersFromOccupantGroupByID),
    OPENSC4_DETOUR(addr::cSC4AutomataControllerManager_RemoveControllersFromOccupantGroupByName,
                   RemoveControllersFromOccupantGroupByName),
    OPENSC4_DETOUR(addr::cSC4AutomataControllerManager_RemoveControllerByName, RemoveControllerByName),
    OPENSC4_DETOUR(addr::cSC4AutomataControllerManager_MoveControllers, MoveControllers),
    OPENSC4_DETOUR(addr::cSC4AutomataControllerManager_MoveController, MoveController),
};

const Detour kControllerSave[] = {
    OPENSC4_DETOUR(addr::cSC4AutomataControllerManager_GetGZCLSID, GetGZCLSID),
    OPENSC4_DETOUR(addr::cSC4AutomataControllerManager_InsertAutomatonEntry, InsertAutomatonEntry),
    OPENSC4_DETOUR(addr::cSC4AutomataControllerManager_Write, Write),
    OPENSC4_DETOUR(addr::cSC4AutomataControllerManager_Read, Read),
};

#undef OPENSC4_DETOUR

bool InstallControllerManager(registry::Mode)
{
    return InstallList(kControllerManager, sizeof kControllerManager / sizeof kControllerManager[0], "ctlmgr");
}

bool InstallControllerSave(registry::Mode)
{
    return InstallList(kControllerSave, sizeof kControllerSave / sizeof kControllerSave[0], "ctlsave");
}

const bool g_registered[] = {
    registry::Register({"simulators.automata.ctlmgr", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4AutomataControllerManager_Construct,
                        "Our automata controller manager: the object (constructor, destructor, QueryInterface, "
                        "Release, Init, Shutdown and their clean-ups), the controller grid's set-up and cell range, "
                        "adding controllers (from a template or its name, from an occupant's groups or one group), "
                        "removing (one, by name, an occupant's, a group's) and moving them, the vector-of-lists "
                        "helpers and the controller template base's destructor (35 detours; "
                        "src/core/automata_ctlmgr.cpp)",
                        false, InstallControllerManager}),
    registry::Register({"simulators.automata.ctlsave", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4AutomataControllerManager_Write,
                        "Our automata controller manager's save: Write, Read, GetGZCLSID and the automaton "
                        "table's insert (4 detours; src/core/automata_ctlmgr.cpp)",
                        false, InstallControllerSave}),
};

} // namespace
} // namespace opensc4::automata_ctlmgr
