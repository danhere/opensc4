// Our cSC4NetworkTool family's power line tool, first part (build 641; part
// w1 of docs/nettool.md): the tool's life (constructor 0x006503C0, the
// destructor and deleting destructor, Init, Shutdown, the once-per-run load
// of the pole exemplars from the network ini with its line callback), the
// slots that keep to its own fields (16 ClearOccupantHighlights, 20
// GetCostOfSolution, 52 GetFailureReasonText, 82 the cell previews), and the
// template code of its vectors (the pole records' copy, fill, assignment,
// erase, growing insert and destructor; the 16-byte element vectors and the
// vectors of them; the vectors of 32-bit vectors). One group,
// simulators.nettool.power, with the second and third parts
// (src/core/nettool_power_2.cpp: the pole records' lookups, the occupant
// lookups, slots 53 and 10; src/core/nettool_power_3.cpp, part w2: the
// pole planner and placer); this file registers it. Calls between the
// part's functions go to the game's addresses, so each detour stands alone
// and OPENSC4_NETTOOL_SKIP bisects them. The layout is in nettool_power.h.
//
// NAM. None of these functions is guarded, patched or called by NAM's DLL
// (its patch sites are all outside 0x0064EFE0-0x00652A6B). The once-load
// LoadRulesOnce 0x00626990 Init calls is the rulload group's and is called
// at its address.
//
// Floats. GetCostOfSolution: the count +0x384 as FILD of an int32 plus 2^32
// when negative (its unsigned value, exact in a double) times the float
// +0x3B0 (one double product at the x87's 53-bit precision), then __ftol2
// (0x009EEF04: truncation to 64 bits, EAX the low half; out-of-range and NaN
// give 0x8000000000000000 as FISTP's indefinite value: Ftol below).
//
// Registers: the direct callers of every function here (the game's
// 0x00650760, 0x00650840, 0x00651560, 0x006520C0, the network manager's
// 0x006108D0 and 0x00610C10 for the constructor) were scanned (scratch
// regscan.py / eaxscan.py): none reads ECX or EDX after the call; EAX is read
// after the constructor (the object), the copies and erases (the
// destination's end / the first), as each function here gives it. The
// vtable slots (3, 4, 16, 20, 51, 52, 82) are the general tool's slots with
// the general tool's conventions (scanned in parts t1/t3/t4); slot 82 (ret
// 4) is called only by slot 53 (0x00652A58), which reads nothing after it.
//
// Calls, each count from the asm's pushes and the callee's own `ret N`:
// the general tool's constructor 0x0062C5F0 one (ret 4: 5), destructor
// 0x0062CAF0 none (the game jumps to it), Init 0x006269C0 none, Shutdown
// 0x00626D30 none (jumped to; AL), ClearOccupantHighlights 0x00625580 none
// (jumped to), GetFailureReasonText 0x0062BF60 two (ret 8: jumped to with
// the caller's arguments); LoadRulesOnce 0x00626990 cdecl none; this part's
// 0x0064F880 cdecl none, 0x006502F0 two (ret 8), 0x00650360 two (ret 8),
// 0x00650580 none, 0x006502B0 none, 0x0064F5D0 cdecl four, 0x0064F920 cdecl
// four, 0x0064F9A0 cdecl four, 0x0064F710 one (ret 4), 0x0064F770 one (ret
// 4), 0x0064FA10 / 0x0064FA60 / 0x0064FAF0 cdecl five; the STL allocator
// 0x0090CF54 and the free 0x0090CF63 cdecl one; operator delete 0x005E5620
// cdecl one; SC4ShortVectorVector::Destroy 0x004638B0 and SC4PairList::
// Destroy 0x005650A0 none; SC4Vector16::Copy 0x007257C0 cdecl five (two
// unread); SC4PointVector::CopyConstruct 0x007CED60 one (ret 4), the point
// vector's assignment 0x00611B00 one (ret 4), SC4IntVector::Assign
// 0x0050DB90 one (ret 4); the resource manager 0x00B43CA8 slot 0xC five
// (key, IID, out, 0, 0; AL), the resource's slot 0x1C none and slot 8 none;
// SC4::GetPropertyUint32 0x005FD480 and GetPropertyFloat 0x005FD4F0 cdecl
// three; SC4::OpenResourceRecord 0x005F9670 cdecl three (key, out, 0; AL),
// the ini reader's constructor 0x0091CF46 one (ret 4), ForEachEntry
// 0x0091D91C three (ret 0xC), destructor 0x0091CDE3 none, the string's
// FromText 0x00408480 one (ret 4); SC4::TextToUint32 0x0090FE93 cdecl one;
// the zone manager 0x00B43D14 slot 0x30 two (x, z); SC4::LoadLocalizedText
// 0x00603040 cdecl four; the preview's AddCell 0x00620820 five (ret 0x14);
// an occupant's +8 part slot 0x44 two (SetHighlight), the occupant's slot
// 0x38 one and slot 8 none; the occupant manager (+0x48) slot 0x54 three;
// cSC4PowerPoleOccupant 0x00649910 two (ret 8).
#include "core/nettool_power.h"
#include "core/nettool_sim.h"

#include "core/game.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::nettool_sim {
namespace {

// A list node of the tool's lists (+0x38C, +0x390: 0xC bytes; +0x394: 0x14
// bytes, two poles).
struct PowerListNode {
    PowerListNode* next;
    PowerListNode* previous;
    void* first;
    void* second;
};

// The 16-byte elements of the vectors at +0x378 (three words and a byte;
// the three bytes after it are never copied).
inline constexpr std::size_t kElementSize = 0x10, kInnerSize = 0xC;

// __ftol2 (0x009EEF04): truncation toward zero to 64 bits; FISTP's
// indefinite value for NaN and out of range.
int64_t Ftol(double value)
{
    if (!(value > -9223372036854775808.0 && value < 9223372036854775808.0))
        return INT64_MIN;
    return static_cast<int64_t>(value);
}

char* Alloc(uint32_t bytes) { return CallCdecl<char*, uint32_t>(addr::SC4_stl_allocate, bytes); }
void Free(void* p) { CallCdecl<void, void*>(addr::SC4_Free, p); }
Vector& VectorAt(char* object, std::size_t offset) { return At<Vector>(object, offset); }

// Every node of a list freed (the head read again at each step, as the
// game does), the head linked to itself.
void FreeNodes(char* self, std::size_t list)
{
    PowerListNode* node = At<PowerListNode*>(self, list)->next;
    while (node != At<PowerListNode*>(self, list)) {
        PowerListNode* const freed = node;
        node = node->next;
        Free(freed);
    }
    At<PowerListNode*>(self, list)->next = At<PowerListNode*>(self, list);
    At<PowerListNode*>(self, list)->previous = At<PowerListNode*>(self, list);
}

// A new list head from the STL allocator linked to itself.
PowerListNode* NewHead(uint32_t size)
{
    PowerListNode* const head = reinterpret_cast<PowerListNode*>(Alloc(size));
    head->next = head;
    head->previous = head;
    return head;
}

// ====================================================================================
// The tool's life
// ====================================================================================

// 0x006503C0 (no arguments): the general tool's constructor with 5, the
// power line tool's vtables, +0x368 cleared, the words +0x36C to +0x388
// cleared, the lists +0x38C and +0x390 (the field cleared, a 0xC-byte head
// linked to itself stored) and +0x394 (a 0x14-byte head), the words +0x398
// to +0x3A0, +0x3AC and +0x3B0 cleared (+0x3A4 and +0x3A8 are not written).
// EAX the object.
char* __fastcall Construct(char* self, void*)
{
    static bool logged = false;
    FirstCall("PowerLineToolConstruct", logged);
    CallThis<char*, uint32_t>(addr::cSC4NetworkManager_NetworkToolConstruct, self, power::kType);
    At<std::uintptr_t>(self, 0) = addr::cSC4NetworkTool_PowerLineVftable;
    At<std::uintptr_t>(self, 4) = power::kVtable4;
    At<uint8_t>(self, power::kInited) = 0;
    for (std::size_t offset = power::kPoles; offset <= power::kCount388; offset += 4)
        At<uint32_t>(self, offset) = 0;
    At<void*>(self, power::kList38C) = nullptr;
    At<PowerListNode*>(self, power::kList38C) = NewHead(power::kListHeadSize);
    At<void*>(self, power::kList390) = nullptr;
    At<PowerListNode*>(self, power::kList390) = NewHead(power::kListHeadSize);
    At<void*>(self, power::kWires) = nullptr;
    At<PowerListNode*>(self, power::kWires) = NewHead(power::kWireHeadSize);
    At<uint32_t>(self, power::kIntVectors) = 0;
    At<uint32_t>(self, power::kIntVectors + 4) = 0;
    At<uint32_t>(self, power::kIntVectors + 8) = 0;
    At<uint32_t>(self, power::kPoleCost) = 0;
    At<uint32_t>(self, power::kWireCost) = 0;
    return self;
}

// 0x00650580 (no arguments; the deleting destructor's): +0x398
// (SC4ShortVectorVector::Destroy), the lists +0x394, +0x390, +0x38C
// (SC4PairList::Destroy), +0x378 (SC4ShortVectorVector::Destroy), the pole
// records +0x36C (0x006502B0), then the general tool's destructor (the game
// jumps to it).
void __fastcall Destruct(char* self, void*)
{
    static bool logged = false;
    FirstCall("PowerLineToolDestruct", logged);
    CallThis<void>(addr::SC4ShortVectorVector_Destroy, self + power::kIntVectors);
    CallThis<void>(addr::SC4PairList_Destroy, self + power::kWires);
    CallThis<void>(addr::SC4PairList_Destroy, self + power::kList390);
    CallThis<void>(addr::SC4PairList_Destroy, self + power::kList38C);
    CallThis<void>(addr::SC4ShortVectorVector_Destroy, self + power::kRuns);
    CallThis<void>(addr::SC4PowerPoleRecordVector_Destroy, self + power::kPoles);
    CallThis<void>(addr::cSC4NetworkTool_Destruct, self);
}

// 0x00650560 slot 51 (ret 4: flags): the destructor 0x00650580; bit 0:
// operator delete. EAX the object.
char* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("PowerLineToolDeletingDestructor", logged);
    CallThis<void>(addr::cSC4PowerLineTool_Destruct, self);
    if (flags & 1)
        CallCdecl<void, void*>(addr::SC4_operator_delete, self);
    return self;
}

// 0x0064F140 (cdecl: the line's key string, its value string, the context
// (0, unread)): the ini reader's callback of the section "PowerPoles": the
// word of the pole exemplars at the key's number gets the value's number
// (SC4::TextToUint32 of each, the key first; the index is not checked, as
// the game's).
void __cdecl OnPowerPoleEntry(const GameString* key, const GameString* value, void*)
{
    static bool logged = false;
    FirstCall("PowerLineOnPowerPoleEntry", logged);
    const uint32_t index = CallCdecl<uint32_t, const char*>(addr::SC4_TextToUint32, key->begin);
    const uint32_t word = CallCdecl<uint32_t, const char*>(addr::SC4_TextToUint32, value->begin);
    Global<uint32_t>(power::kPoleExemplars + index * 4) = word;
}

// 0x0064F880 (cdecl, no arguments; Init calls it once per run): the record
// of the key (type 0, group 0x8A5971C5, instance 0x8A5993B9) opened
// (0x005F9670); when it opens, the game's ini reader (0x3C bytes on the
// stack) made over it, its section "PowerPoles" (the game's text 0x00AA9F14
// as a string of the game's on the stack) run with the callback 0x0064F140
// and context 0, the string's end as the game has it inline (the base
// vtable stored, the characters freed) and the reader destroyed. A record
// the opener left is released last.
void __cdecl LoadPoleExemplars()
{
    static bool logged = false;
    FirstCall("PowerLineLoadPoleExemplars", logged);
    void* record = nullptr;
    uint32_t key[3] = {0u, power::kIniGroup, power::kIniInstance};
    if (CallCdecl<uint8_t, uint32_t*, void**, uint32_t>(addr::SC4_OpenResourceRecord, key, &record, 0u)) {
        alignas(4) uint8_t reader[0x3C];
        CallThis<void*, void*>(addr::cRZIniFile_ConstructFromRecord, reader, record);
        GameString section;
        CallThis<GameString*, const char*>(addr::SC4String_FromText, &section,
                                           reinterpret_cast<const char*>(power::kSectionName));
        CallThis<uint8_t, GameString*, std::uintptr_t, void*>(addr::cRZIniFile_ForEachEntry, reader, &section,
                                                              addr::cSC4PowerLineTool_OnPowerPoleEntry, nullptr);
        char* const text = section.begin;
        section.vtable = addr::SC4String_Vtable;
        if (text)
            Free(text);
        CallThis<void>(addr::cRZIniFile_Destruct, reader);
    }
    if (record)
        ReleaseOf(record);
}

// 0x00650630 slot 3 Init (no arguments): the first time (+0x368 clear):
// once per run (the flag 0x00B467A8, set first) LoadRulesOnce 0x00626990
// and the pole exemplars 0x0064F880; the byte set; the general tool's Init;
// +0x3A4 = 20 and +0x3A8 = 7.0, then the exemplar (0x6534284A, 0xE7E2C2DB,
// 0xC911E35B) from the resource manager (slot 0xC: key, 0xA52160F5, out, 0,
// 0); when found its property holder (slot 0x1C): the uint32 0x098B25C8 into
// +0x3A4, the float 0x098B27D0 into +0x3A8, the uint32 0x4A5F5953 into
// +0x3AC, the float 0x0A5F596B into +0x3B0, and the resource released.
// Then every pole record erased (0x006502F0 with begin and end) and every
// vector of +0x378 (0x00650360). AL 1.
bool __fastcall Init(char* self, void*)
{
    static bool logged = false;
    FirstCall("PowerLineToolInit", logged);
    if (At<uint8_t>(self, power::kInited))
        return true;
    if (!Global<uint8_t>(power::kLoadedOnce)) {
        Global<uint8_t>(power::kLoadedOnce) = 1;
        CallCdecl<uint8_t>(addr::cSC4NetworkTool_LoadRulesOnce);
        CallCdecl<void>(addr::cSC4PowerLineTool_LoadPoleExemplars);
    }
    At<uint8_t>(self, power::kInited) = 1;
    CallThis<uint8_t>(addr::cSC4NetworkTool_Init, self);
    uint32_t key[3] = {power::kTuningType, power::kTuningGroup, power::kTuningInstance};
    At<uint32_t>(self, power::kTuning3A4) = power::kInit3A4;
    At<uint32_t>(self, power::kTuning3A8) = power::kInit3A8Bits;
    void* resource = nullptr; // the game's out is an unwritten local, read only when found
    void* const manager = Global<void*>(addr::SC4_ResourceManager);
    if (Virtual<uint8_t>(manager, 0xC, key, power::kResourceIid, &resource, 0u, 0u)) {
        void* const holder = Virtual<void*>(resource, 0x1C);
        CallCdecl<uint8_t, void*, uint32_t, char*>(addr::SC4_GetPropertyUint32, holder, power::kProp3A4,
                                                   self + power::kTuning3A4);
        CallCdecl<uint8_t, void*, uint32_t, char*>(addr::SC4_GetPropertyFloat, holder, power::kProp3A8,
                                                   self + power::kTuning3A8);
        CallCdecl<uint8_t, void*, uint32_t, char*>(addr::SC4_GetPropertyUint32, holder, power::kPropPoleCost,
                                                   self + power::kPoleCost);
        CallCdecl<uint8_t, void*, uint32_t, char*>(addr::SC4_GetPropertyFloat, holder, power::kPropWireCost,
                                                   self + power::kWireCost);
        ReleaseOf(resource);
    }
    CallThis<char*, char*, char*>(addr::SC4PowerPoleRecordVector_Erase, self + power::kPoles,
                                  VectorAt(self, power::kPoles).begin, VectorAt(self, power::kPoles).end);
    CallThis<char*, char*, char*>(addr::SC4Vector16Vector_Erase, self + power::kRuns,
                                  VectorAt(self, power::kRuns).begin, VectorAt(self, power::kRuns).end);
    return true;
}

// 0x0064F0D0 slot 4 Shutdown (no arguments): when Init ran the byte cleared
// and the general tool's Shutdown (the game jumps to it: its AL is the
// answer); else AL 1.
uint8_t __fastcall Shutdown(char* self, void*)
{
    static bool logged = false;
    FirstCall("PowerLineToolShutdown", logged);
    if (!At<uint8_t>(self, power::kInited))
        return 1;
    At<uint8_t>(self, power::kInited) = 0;
    return CallThis<uint8_t>(addr::cSC4NetworkTool_Shutdown, self);
}

// ====================================================================================
// Slots of the tool's own
// ====================================================================================

// 0x0064F0F0 slot 20 GetCostOfSolution (no arguments): 0 unless the pole
// count +0x388 is above 1 (unsigned); else __ftol2 of the count +0x384
// (unsigned) times the float +0x3B0, plus the pole cost +0x3AC times the
// pole count (32-bit products and sum).
uint32_t __fastcall GetCostOfSolution(char* self, void*)
{
    static bool logged = false;
    FirstCall("PowerLineGetCostOfSolution", logged);
    const uint32_t poles = At<uint32_t>(self, power::kCount388);
    if (poles <= 1)
        return 0;
    const double cells = static_cast<double>(At<uint32_t>(self, power::kCount384));
    const double wire = cells * static_cast<double>(At<float>(self, power::kWireCost));
    const uint32_t low = static_cast<uint32_t>(Ftol(wire));
    return low + At<uint32_t>(self, power::kPoleCost) * poles;
}

// 0x0064F570 slot 52 GetFailureReasonText (ret 8: a reason, a game string):
// a reason that is not 0 goes to the general tool's 0x0062BF60 with both
// arguments (the game jumps to it). For 0: when there are anchor points
// (+0x118), the zone manager's slot 0x30 at the last one (x, z); above 0
// (signed) the LTEXT 0x2A592FD1/0x8AADD2B5 into the string
// (SC4::LoadLocalizedText, cdecl: string, group, instance, 0). No caller
// reads the answer (part t1's EAX scan); ours gives the last call's.
uint32_t __fastcall GetFailureReasonText(char* self, void*, uint32_t reason, void* text)
{
    static bool logged = false;
    FirstCall("PowerLineGetFailureReasonText", logged);
    if (reason != 0)
        return CallThis<uint32_t, uint32_t, void*>(addr::cSC4NetworkTool_GetFailureReasonText, self, reason, text);
    const Vector& anchors = VectorAt(self, field::kAnchors);
    if (anchors.begin == anchors.end)
        return 0;
    void* const zones = Global<void*>(addr::SC4_ZoneManager);
    const uint32_t x = At<uint32_t>(anchors.end, static_cast<std::size_t>(-8));
    const uint32_t z = At<uint32_t>(anchors.end, static_cast<std::size_t>(-4));
    const int32_t zone = Virtual<int32_t>(zones, 0x30, x, z);
    if (zone <= 0)
        return static_cast<uint32_t>(zone);
    return CallCdecl<uint32_t, void*, uint32_t, uint32_t, uint32_t>(addr::SC4_LoadLocalizedText, text,
                                                                    kFailureTextGroup, 0x8AADD2B5u, 0u);
}

// 0x0064F3F0 slot 16 ClearOccupantHighlights (no arguments):
//   - the list +0x38C: each occupant's +8 part's SetHighlight (slot 0x44:
//     0, 0); when the tool has an occupant manager (+0x48), its slot 0x54
//     (the +8 part, 1, 0) and the occupant released (slot 8; not released
//     without a manager, as the game's); the nodes freed;
//   - the list +0x390: each one's +8 part's slot 0x44 (0, 0), its slot 0x38
//     (0), released; the nodes freed;
//   - the list +0x394: for each node, its first pole (when set) given to
//     0x00649910 (this the first, the second, 1), released and cleared;
//     its second (when set) given to 0x00649910 (this and argument the
//     second, 1), released and cleared; the nodes freed;
//   - then the general tool's ClearOccupantHighlights (jumped to).
uint32_t __fastcall ClearOccupantHighlights(char* self, void*)
{
    static bool logged = false;
    FirstCall("PowerLineClearOccupantHighlights", logged);
    PowerListNode* const head38C = At<PowerListNode*>(self, power::kList38C);
    for (PowerListNode* node = head38C->next; node != head38C; node = node->next) {
        char* const occupant = static_cast<char*>(node->first);
        Virtual<void>(occupant + 8, 0x44, 0u, 0u);
        if (void* const manager = At<void*>(self, field::kOccupants)) {
            Virtual<uint32_t>(manager, 0x54, occupant + 8, 1u, 0u);
            ReleaseOf(occupant);
        }
    }
    FreeNodes(self, power::kList38C);
    PowerListNode* const head390 = At<PowerListNode*>(self, power::kList390);
    for (PowerListNode* node = head390->next; node != head390; node = node->next) {
        char* const occupant = static_cast<char*>(node->first);
        Virtual<void>(occupant + 8, 0x44, 0u, 0u);
        Virtual<void>(occupant, 0x38, 0u);
        ReleaseOf(occupant);
    }
    FreeNodes(self, power::kList390);
    PowerListNode* const wires = At<PowerListNode*>(self, power::kWires);
    for (PowerListNode* node = wires->next; node != wires; node = node->next) {
        if (node->first) {
            CallThis<void, void*, uint32_t>(addr::cSC4PowerPoleOccupant_Unwire, node->first, node->second, 1u);
            ReleaseOf(node->first);
            node->first = nullptr;
        }
        if (node->second) {
            CallThis<void, void*, uint32_t>(addr::cSC4PowerPoleOccupant_Unwire, node->second, node->second, 1u);
            ReleaseOf(node->second);
            node->second = nullptr;
        }
    }
    FreeNodes(self, power::kWires);
    return CallThis<uint32_t>(addr::cSC4NetworkTool_ClearOccupantHighlights, self);
}

// One cell's preview as slot 82 makes it: AddCell on the preview drawable
// (+0x1F0) with the cell's point (a copy on the stack), a colour and three
// zeros.
void PreviewCell(char* self, uint32_t cell, uint32_t colour)
{
    char* const cells = VectorAt(self, field::kCells).begin;
    uint32_t point[2] = {At<uint32_t>(cells, cell * kPointSize), At<uint32_t>(cells, cell * kPointSize + 4)};
    CallThis<void, uint32_t*, uint32_t, uint32_t, uint32_t, uint32_t>(addr::cSC4NetworkToolPreview_AddCell,
                                                                      At<void*>(self, field::kPreview), point,
                                                                      colour, 0u, 0u, 0u);
}

// 0x0064F2B0 slot 82 (ret 4: the first step): for each step from it below
// the valid step count (+0x78, re-read; signed), each dragged cell from the
// step's first to its last (unsigned, the last re-read each time): the
// cell's preview in 0x800000FF unless a pole record has its x and z; then
// for each step from the valid count below the step count (+0x70), every
// cell's preview in 0x80FF0000.
void __fastcall PreviewCells(char* self, void*, int32_t first)
{
    static bool logged = false;
    FirstCall("PowerLinePreviewCells", logged);
    for (int32_t s = first; s < At<int32_t>(self, field::kValidSteps); ++s) {
        const std::size_t offset = static_cast<std::size_t>(s) * step::kSize;
        uint32_t cell = At<uint32_t>(VectorAt(self, field::kSteps).begin, offset);
        if (cell > At<uint32_t>(VectorAt(self, field::kSteps).begin, offset + step::kLastCell))
            continue;
        do {
            const char* const point = VectorAt(self, field::kCells).begin + cell * kPointSize;
            bool found = false;
            for (char* record = VectorAt(self, power::kPoles).begin; record != VectorAt(self, power::kPoles).end;
                 record += pole::kSize) {
                if (At<uint32_t>(record, pole::kX) == At<uint32_t>(const_cast<char*>(point), 0) &&
                    At<uint32_t>(record, pole::kZ) == At<uint32_t>(const_cast<char*>(point), 4)) {
                    found = true;
                    break;
                }
            }
            if (!found)
                PreviewCell(self, cell, 0x800000FFu);
            ++cell;
        } while (cell <= At<uint32_t>(VectorAt(self, field::kSteps).begin, offset + step::kLastCell));
    }
    for (int32_t s = At<int32_t>(self, field::kValidSteps); s < At<int32_t>(self, field::kStepCount); ++s) {
        const std::size_t offset = static_cast<std::size_t>(s) * step::kSize;
        uint32_t cell = At<uint32_t>(VectorAt(self, field::kSteps).begin, offset);
        if (cell > At<uint32_t>(VectorAt(self, field::kSteps).begin, offset + step::kLastCell))
            continue;
        do {
            PreviewCell(self, cell, 0x80FF0000u);
            ++cell;
        } while (cell <= At<uint32_t>(VectorAt(self, field::kSteps).begin, offset + step::kLastCell));
    }
}

// ====================================================================================
// Template code of the tool's vectors
// ====================================================================================

// 0x0064F5D0 (cdecl: first, last, the destination, an unread allocator):
// the 16-byte elements copied (three words and the byte +0xC) where the
// destination is not null; EAX the destination's end.
char* __cdecl Vector16UninitializedCopy(const char* first, const char* last, char* dest, void*)
{
    static bool logged = false;
    FirstCall("PowerLineVector16UninitializedCopy", logged);
    for (; first != last; first += kElementSize, dest += kElementSize) {
        if (dest) {
            At<uint32_t>(dest, 0) = At<uint32_t>(const_cast<char*>(first), 0);
            At<uint32_t>(dest, 4) = At<uint32_t>(const_cast<char*>(first), 4);
            At<uint32_t>(dest, 8) = At<uint32_t>(const_cast<char*>(first), 8);
            At<uint8_t>(dest, 0xC) = At<uint8_t>(const_cast<char*>(first), 0xC);
        }
    }
    return dest;
}

// 0x0064F610 (ret 4: the source; this a vector of 16-byte elements): the
// copy constructor: three words cleared, the source's count (signed shift)
// of elements allocated (none for 0: a null begin), begin = end, capacity =
// begin + count * 16, the elements copied by 0x0064F5D0 (its fourth
// argument is the address of the caller's argument slot), the end. EAX
// this.
char* __fastcall Vector16CopyConstruct(char* self, void*, Vector* source)
{
    static bool logged = false;
    FirstCall("PowerLineVector16CopyConstruct", logged);
    Vector& v = *reinterpret_cast<Vector*>(self);
    const int32_t count = static_cast<int32_t>(source->end - source->begin) >> 4;
    v.begin = nullptr;
    v.end = nullptr;
    v.capacity = nullptr;
    char* const memory = count != 0 ? Alloc(static_cast<uint32_t>(count) << 4) : nullptr;
    v.begin = memory;
    v.end = memory;
    v.capacity = memory + (static_cast<uint32_t>(count) << 4);
    v.end = CallCdecl<char*, char*, char*, char*, Vector**>(addr::SC4Vector16_UninitializedCopy13, source->begin,
                                                            source->end, memory, &source);
    return self;
}

// 0x0064F670 (cdecl: first, last, the destination; vectors of 16-byte
// elements, 0xC bytes each): each copy-constructed where the destination
// is not null (the copy constructor 0x0064F610 inline: the source's count
// allocated, the elements copied where the new buffer is not null). EAX the
// destination's end.
char* __cdecl Vector16VectorUninitializedCopy(char* first, char* last, char* dest)
{
    static bool logged = false;
    FirstCall("PowerLineVector16VectorUninitializedCopy", logged);
    for (; first != last; first += kInnerSize, dest += kInnerSize) {
        if (!dest)
            continue;
        Vector& out = *reinterpret_cast<Vector*>(dest);
        const Vector& in = *reinterpret_cast<const Vector*>(first);
        const int32_t count = static_cast<int32_t>(in.end - in.begin) >> 4;
        out.begin = nullptr;
        out.end = nullptr;
        out.capacity = nullptr;
        char* const memory = count != 0 ? Alloc(static_cast<uint32_t>(count) << 4) : nullptr;
        out.begin = memory;
        out.end = memory;
        out.capacity = memory + (static_cast<uint32_t>(count) << 4);
        char* to = memory;
        for (const char* from = in.begin; from != in.end; from += kElementSize, to += kElementSize) {
            if (to) {
                At<uint32_t>(to, 0) = At<uint32_t>(const_cast<char*>(from), 0);
                At<uint32_t>(to, 4) = At<uint32_t>(const_cast<char*>(from), 4);
                At<uint32_t>(to, 8) = At<uint32_t>(const_cast<char*>(from), 8);
                At<uint8_t>(to, 0xC) = At<uint8_t>(const_cast<char*>(from), 0xC);
            }
        }
        out.end = to;
    }
    return dest;
}

// The words +0x0 to +0x10 and the bytes +0x14, +0x15 of a pole record
// copied; the vector at +0x18 by `vector` (copy-construct or assign); the
// words +0x24 to +0x2C.
void CopyRecord(char* to, const char* from, std::uintptr_t vector)
{
    char* const src = const_cast<char*>(from);
    for (std::size_t offset = 0; offset <= pole::kOccupant; offset += 4)
        At<uint32_t>(to, offset) = At<uint32_t>(src, offset);
    At<uint8_t>(to, pole::kNew) = At<uint8_t>(src, pole::kNew);
    At<uint8_t>(to, pole::kSides) = At<uint8_t>(src, pole::kSides);
    CallThis<char*, char*>(vector, to + pole::kLinks, src + pole::kLinks);
    At<uint32_t>(to, pole::kObject24) = At<uint32_t>(src, pole::kObject24);
    At<uint32_t>(to, pole::kObject28) = At<uint32_t>(src, pole::kObject28);
    At<uint32_t>(to, pole::kWord2C) = At<uint32_t>(src, pole::kWord2C);
}

// 0x0064F710 (ret 4: the source; this a pole record): the copy
// constructor (CopyRecord with SC4PointVector::CopyConstruct). EAX this.
char* __fastcall PoleRecordCopyConstruct(char* self, void*, const char* source)
{
    static bool logged = false;
    FirstCall("PowerLinePoleRecordCopyConstruct", logged);
    CopyRecord(self, source, addr::SC4PointVector_CopyConstruct);
    return self;
}

// 0x0064F920 (cdecl: first, last, the destination, an unread allocator):
// the pole records copy-constructed where the destination is not null. EAX
// the destination's end.
char* __cdecl PoleRecordUninitializedCopy(const char* first, const char* last, char* dest, void*)
{
    static bool logged = false;
    FirstCall("PowerLinePoleRecordUninitializedCopy", logged);
    for (; first != last; first += pole::kSize, dest += pole::kSize)
        if (dest)
            CopyRecord(dest, first, addr::SC4PointVector_CopyConstruct);
    return dest;
}

// 0x0064F9A0 (cdecl: the destination, a count, the value, an unread
// allocator): `count` copies of the record (none for 0) where the
// destination is not null. EAX the destination's end.
char* __cdecl PoleRecordUninitializedFillN(char* dest, uint32_t count, const char* value, void*)
{
    static bool logged = false;
    FirstCall("PowerLinePoleRecordUninitializedFillN", logged);
    for (; count != 0; --count, dest += pole::kSize)
        if (dest)
            CopyRecord(dest, value, addr::SC4PointVector_CopyConstruct);
    return dest;
}

// 0x0064FA60 (cdecl: first, last, the destination, two unread): the pole
// records assigned (CopyRecord with the point vector's assignment
// 0x00611B00), (last - first) / 0x30 of them (signed). EAX the destination's
// end (the destination for none).
char* __cdecl PoleRecordCopy(const char* first, const char* last, char* dest, void*, uint32_t)
{
    static bool logged = false;
    FirstCall("PowerLinePoleRecordCopy", logged);
    const int32_t count = static_cast<int32_t>(last - first) / static_cast<int32_t>(pole::kSize);
    for (int32_t i = 0; i < count; ++i, first += pole::kSize, dest += pole::kSize)
        CopyRecord(dest, first, addr::cSC4NetworkManager_PointVectorAssign);
    return dest;
}

// 0x0064F770 (ret 4: the source; this a vector of 16-byte elements): the
// assignment. Nothing for itself. The source's end is kept in the caller's
// argument slot, whose address the copies get as their unread fourth
// argument. More elements than the capacity: a new buffer of the count
// (none for 0), the elements copied in (0x0064F5D0), the old buffer freed,
// begin, end and capacity set. Else, when the size is at least the count:
// SC4Vector16::Copy over the start; else the first `size` copied over
// (SC4Vector16::Copy) and the rest copied in after the end (0x0064F5D0);
// the end = begin + count * 16. EAX this.
char* __fastcall Vector16Assign(char* self, void*, Vector* source)
{
    static bool logged = false;
    FirstCall("PowerLineVector16Assign", logged);
    if (reinterpret_cast<char*>(source) == self)
        return self;
    Vector& v = *reinterpret_cast<Vector*>(self);
    char* slot = source->end; // the game's argument slot after `mov [esp+0x14], edx`
    char* const srcBegin = source->begin;
    const uint32_t count = static_cast<uint32_t>(static_cast<int32_t>(source->end - srcBegin) >> 4);
    const uint32_t capacity = static_cast<uint32_t>(static_cast<int32_t>(v.capacity - v.begin) >> 4);
    if (count > capacity) {
        char* const memory = count != 0 ? Alloc(count << 4) : nullptr;
        CallCdecl<char*, char*, char*, char*, char**>(addr::SC4Vector16_UninitializedCopy13, srcBegin, slot, memory,
                                                      &slot);
        if (v.begin)
            Free(v.begin);
        v.end = memory + (count << 4);
        v.begin = memory;
        v.capacity = memory + (count << 4);
        return self;
    }
    const uint32_t size = static_cast<uint32_t>(static_cast<int32_t>(v.end - v.begin) >> 4);
    if (size >= count) {
        CallCdecl<char*, char*, char*, char*, char**, uint32_t>(addr::SC4Vector16_Copy, srcBegin, slot, v.begin,
                                                                &slot, 0u);
        v.end = v.begin + (count << 4);
        return self;
    }
    CallCdecl<char*, char*, char*, char*, char**, uint32_t>(addr::SC4Vector16_Copy, srcBegin, srcBegin + (size << 4),
                                                            v.begin, &slot, 0u);
    const uint32_t done = static_cast<uint32_t>(static_cast<int32_t>(v.end - v.begin) >> 4);
    CallCdecl<char*, char*, char*, char*, char**>(addr::SC4Vector16_UninitializedCopy13, source->begin + (done << 4),
                                                  source->end, v.end, &slot);
    v.end = v.begin + (count << 4);
    return self;
}

// 0x0064FA10 (cdecl: first, last, the destination, two unread; vectors of
// 16-byte elements): (last - first) / 0xC of them assigned (0x0064F770).
// EAX the destination's end (the destination for none).
char* __cdecl Vector16VectorCopy(char* first, char* last, char* dest, void*, uint32_t)
{
    static bool logged = false;
    FirstCall("PowerLineVector16VectorCopy", logged);
    const int32_t count = static_cast<int32_t>(last - first) / static_cast<int32_t>(kInnerSize);
    for (int32_t i = 0; i < count; ++i, first += kInnerSize, dest += kInnerSize)
        CallThis<char*, char*>(addr::SC4Vector16_Assign, dest, first);
    return dest;
}

// 0x0064FAF0 (cdecl: first, last, the destination, two unread; vectors of
// 32-bit values): (last - first) / 0xC of them assigned (SC4IntVector::
// Assign). EAX the destination's end (the destination for none).
char* __cdecl IntVectorVectorCopy(char* first, char* last, char* dest, void*, uint32_t)
{
    static bool logged = false;
    FirstCall("PowerLineIntVectorVectorCopy", logged);
    const int32_t count = static_cast<int32_t>(last - first) / static_cast<int32_t>(kInnerSize);
    for (int32_t i = 0; i < count; ++i, first += kInnerSize, dest += kInnerSize)
        CallThis<char*, char*>(addr::SC4IntVector_Assign, dest, first);
    return dest;
}

// The erase of a vector of vectors (0x00650360 and 0x00650250): the
// elements from `last` to the end assigned over `first` on (`copy`, cdecl
// five: last, end, first, the address of the caller's first argument slot,
// 0), every element from the new end to the old end freed (its begin when
// not null), the end set. EAX first.
char* EraseVectors(char* self, char* first, char* last, std::uintptr_t copy)
{
    Vector& v = *reinterpret_cast<Vector*>(self);
    char* slot = first;
    char* const newEnd =
        CallCdecl<char*, char*, char*, char*, char**, uint32_t>(copy, last, v.end, first, &slot, 0u);
    char* const oldEnd = v.end;
    for (char* element = newEnd; element != oldEnd; element += kInnerSize)
        if (char* const begin = At<char*>(element, 0))
            Free(begin);
    v.end = newEnd;
    return first;
}

// 0x00650360 (ret 8: first, last; this +0x378, vectors of 16-byte
// elements): EraseVectors with 0x0064FA10.
char* __fastcall Vector16VectorErase(char* self, void*, char* first, char* last)
{
    static bool logged = false;
    FirstCall("PowerLineVector16VectorErase", logged);
    return EraseVectors(self, first, last, addr::SC4Vector16Vector_Copy);
}

// 0x00650250 (ret 8: first, last; vectors of 32-bit values): EraseVectors
// with 0x0064FAF0.
char* __fastcall IntVectorVectorErase(char* self, void*, char* first, char* last)
{
    static bool logged = false;
    FirstCall("PowerLineIntVectorVectorErase", logged);
    return EraseVectors(self, first, last, addr::SC4IntVectorVector_CopyRange);
}

// 0x006502F0 (ret 8: first, last; this the pole records): the records
// from `last` to the end assigned over `first` on (0x0064FA60: last, end,
// first, the address of the first argument's slot, 0), each record from
// the new end to the old end gets its vector's buffer (+0x18) freed when
// not null, the end set. EAX first.
char* __fastcall PoleRecordErase(char* self, void*, char* first, char* last)
{
    static bool logged = false;
    FirstCall("PowerLinePoleRecordErase", logged);
    Vector& v = *reinterpret_cast<Vector*>(self);
    char* slot = first;
    char* const newEnd = CallCdecl<char*, char*, char*, char*, char**, uint32_t>(addr::SC4PowerPoleRecord_Copy, last,
                                                                                 v.end, first, &slot, 0u);
    char* const oldEnd = v.end;
    for (char* record = newEnd; record != oldEnd; record += pole::kSize)
        if (char* const links = At<char*>(record, pole::kLinks))
            Free(links);
    v.end = newEnd;
    return first;
}

// 0x006502B0 (no arguments; this the pole records): each record's vector
// buffer (+0x18) freed when not null, then the records' buffer when not
// null.
void __fastcall PoleRecordVectorDestroy(char* self, void*)
{
    static bool logged = false;
    FirstCall("PowerLinePoleRecordVectorDestroy", logged);
    Vector& v = *reinterpret_cast<Vector*>(self);
    for (char* record = v.begin; record != v.end; record += pole::kSize)
        if (char* const links = At<char*>(record, pole::kLinks))
            Free(links);
    if (v.begin)
        Free(v.begin);
}

// 0x00650480 (ret 0x14: where, the value, an unread tag, a count, a byte
// "at the end"; this the pole records): the growing insert. The new
// capacity is size + max(size, count) records (none for 0); the records
// before `where` copied in (0x0064F920), then one copy of the value
// (0x0064F710, where the buffer is not null) for a count of 1, else
// `count` copies (0x0064F9A0); unless at the end, the records from `where`
// copied after them (0x0064F920); the old records destroyed (0x006502B0);
// begin, end and capacity set. The copies' unread fourth argument is the
// address of the byte's argument slot.
void __fastcall PoleRecordInsertOverflow(char* self, void*, char* where, const char* value, void*, uint32_t count,
                                         uint32_t atEnd)
{
    static bool logged = false;
    FirstCall("PowerLinePoleRecordInsertOverflow", logged);
    Vector& v = *reinterpret_cast<Vector*>(self);
    const uint32_t size =
        static_cast<uint32_t>(static_cast<int32_t>(v.end - v.begin) / static_cast<int32_t>(pole::kSize));
    const uint32_t grown = (size < count ? count : size) + size;
    char* const memory = grown != 0 ? Alloc(grown * static_cast<uint32_t>(pole::kSize)) : nullptr;
    uint32_t* const tag = &atEnd;
    char* end = CallCdecl<char*, char*, char*, char*, uint32_t*>(addr::SC4PowerPoleRecord_UninitializedCopy, v.begin,
                                                                 where, memory, tag);
    if (count == 1) {
        if (end)
            CallThis<char*, const char*>(addr::SC4PowerPoleRecord_CopyConstruct, end, value);
        end += pole::kSize;
    } else {
        end = CallCdecl<char*, char*, uint32_t, const char*, uint32_t*>(addr::SC4PowerPoleRecord_UninitializedFillN,
                                                                        end, count, value, tag);
    }
    if (static_cast<uint8_t>(atEnd) == 0)
        end = CallCdecl<char*, char*, char*, char*, uint32_t*>(addr::SC4PowerPoleRecord_UninitializedCopy, where,
                                                               v.end, end, tag);
    CallThis<void>(addr::SC4PowerPoleRecordVector_Destroy, self);
    v.end = end;
    v.begin = memory;
    v.capacity = memory + grown * pole::kSize;
}

#define OPENSC4_DETOUR(address, fn) OPENSC4_NETTOOL_DETOUR(address, fn)

const Detour kPower[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkManager_PowerLineToolConstruct, Construct),
    OPENSC4_DETOUR(addr::cSC4PowerLineTool_Destruct, Destruct),
    OPENSC4_DETOUR(addr::cSC4PowerLineTool_DeletingDestructor, DeletingDestructor),
    OPENSC4_DETOUR(addr::cSC4PowerLineTool_OnPowerPoleEntry, OnPowerPoleEntry),
    OPENSC4_DETOUR(addr::cSC4PowerLineTool_LoadPoleExemplars, LoadPoleExemplars),
    OPENSC4_DETOUR(addr::cSC4PowerLineTool_Init, Init),
    OPENSC4_DETOUR(addr::cSC4PowerLineTool_Shutdown, Shutdown),
    OPENSC4_DETOUR(addr::cSC4PowerLineTool_GetCostOfSolution, GetCostOfSolution),
    OPENSC4_DETOUR(addr::cSC4PowerLineTool_GetFailureReasonText, GetFailureReasonText),
    OPENSC4_DETOUR(addr::cSC4PowerLineTool_ClearOccupantHighlights, ClearOccupantHighlights),
    OPENSC4_DETOUR(addr::cSC4PowerLineTool_PreviewCells, PreviewCells),
    OPENSC4_DETOUR(addr::SC4Vector16_UninitializedCopy13, Vector16UninitializedCopy),
    OPENSC4_DETOUR(addr::SC4Vector16_CopyConstruct, Vector16CopyConstruct),
    OPENSC4_DETOUR(addr::SC4Vector16Vector_UninitializedCopy, Vector16VectorUninitializedCopy),
    OPENSC4_DETOUR(addr::SC4PowerPoleRecord_CopyConstruct, PoleRecordCopyConstruct),
    OPENSC4_DETOUR(addr::SC4PowerPoleRecord_UninitializedCopy, PoleRecordUninitializedCopy),
    OPENSC4_DETOUR(addr::SC4PowerPoleRecord_UninitializedFillN, PoleRecordUninitializedFillN),
    OPENSC4_DETOUR(addr::SC4PowerPoleRecord_Copy, PoleRecordCopy),
    OPENSC4_DETOUR(addr::SC4Vector16_Assign, Vector16Assign),
    OPENSC4_DETOUR(addr::SC4Vector16Vector_Copy, Vector16VectorCopy),
    OPENSC4_DETOUR(addr::SC4IntVectorVector_CopyRange, IntVectorVectorCopy),
    OPENSC4_DETOUR(addr::SC4Vector16Vector_Erase, Vector16VectorErase),
    OPENSC4_DETOUR(addr::SC4IntVectorVector_EraseRange, IntVectorVectorErase),
    OPENSC4_DETOUR(addr::SC4PowerPoleRecordVector_Erase, PoleRecordErase),
    OPENSC4_DETOUR(addr::SC4PowerPoleRecordVector_Destroy, PoleRecordVectorDestroy),
    OPENSC4_DETOUR(addr::SC4PowerPoleRecordVector_InsertOverflow, PoleRecordInsertOverflow),
};

#undef OPENSC4_DETOUR

bool InstallPower(registry::Mode)
{
    const bool first = InstallDetours(kPower, "power");
    const bool second = InstallPower2();
    const bool third = InstallPower3();
    return first && second && third;
}

const bool g_registered[] = {
    registry::Register({"simulators.nettool.power", "simulators/nettool", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkManager_PowerLineToolConstruct,
                        "Our power line tool (parts w1 and w2): its life, the pole exemplars' ini load, slots 3, "
                        "4, 16, 20, 51, 52 and 82 and its vectors' template code (src/core/nettool_power.cpp), "
                        "the pole records' lookups, the occupant lookups, the pole maker and slots 53 and 10 "
                        "(src/core/nettool_power_2.cpp), the pole planner and placer "
                        "(src/core/nettool_power_3.cpp)",
                        false, InstallPower}),
};

} // namespace
} // namespace opensc4::nettool_sim
