// Our cSC4NetworkTool, part t9, second file (build 641): the per-cell height
// constraints next to the NAM-guarded 0x00636690 (group
// simulators.nettool.heightcons): InsertEqualityConstraint 0x00636000,
// InsertSmoothnessConstraint 0x00636030 and GetCrossSections 0x00636360
// (NAM's NetworkSlopes calls all three at their addresses: naked thunks that
// keep the game's registers and flags), IsCellInHeightSet 0x00636070 and
// slot 76 InsertRangeConstraintsForCell 0x006360C0. The top of
// src/core/nettool_heights_1.cpp has the analysis of the whole part (what
// stays the game's, NAM, the register scan, floats, stack leftovers, the
// call list); the layouts and helpers are in src/core/nettool_heights.h.
#include "core/nettool_heights.h"

#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::nettool_sim {
namespace {

using namespace heights;

// ====================================================================================
// The per-cell constraints (simulators.nettool.heightcons)
// ====================================================================================

// 0x00636070 (ret 8: x, z; GetCrossSections'): EAX 1 when (z << 16) + x is
// in the set +0xA4, else 0.
uint32_t __fastcall IsCellInHeightSet(char* self, void*, uint32_t x, uint32_t z)
{
    static bool logged = false;
    FirstCall("IsCellInHeightSet", logged);
    return satisfier::Find(self + hfield::kSetA4, (z << 16) + x) ? 1u : 0u;
}

// 0x006360C0 slot 76 InsertRangeConstraintsForCell (ret 4: a cell key; this
// the tool; InsertCellConstraints' and 0x00658040's): nothing without a
// cell info. A network lot: its lot (the lot manager's slot 0x18 with x, z,
// 0; none: nothing) and the lot's rectangle (slot 0x38); the sides of the
// cell whose neighbour lies inside the rectangle are taken out of the cell's
// combined edge word, and a side mask made of the bytes left (the game's
// bit trick); per corner the lot vertex (+ 100000) added and fixed at the
// corner's height, and the plain vertex too where the mask meets the
// corner's two sides (0x00AA8A78). An immovable cell: its four vertices
// fixed. Else per corner: fixed when the vertex is in +0xB8, otherwise
// added at the height, restricted to the first pair, its preferred range the
// second pair, and for a cell from an occupant its preferred height the
// corner's height.
void __fastcall InsertRangeConstraintsForCell(char* self, void*, uint32_t key)
{
    static bool logged = false;
    FirstCall("InsertRangeConstraintsForCell", logged);
    char* const info = Entry(self, key);
    if (!info)
        return;
    char* const sat = Sat1(self);
    if (At<uint8_t>(info, cell::kNetworkLot)) {
        void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x18, At<uint32_t>(info, cell::kX),
                                         At<uint32_t>(info, cell::kZ), 0u);
        if (!lot)
            return;
        int32_t rect[4] = {0, 0, 0, 0};
        Virtual<uint8_t>(lot, 0x38, rect);
        uint32_t edges = 0;
        for (std::size_t t = 0; t < cell::kEdgeWords; ++t)
            edges |= At<uint32_t>(info, cell::kByType + 4 * t);
        const int32_t x = At<int32_t>(info, cell::kX), z = At<int32_t>(info, cell::kZ);
        auto inside = [&rect](int32_t px, int32_t pz) {
            return px >= rect[0] && pz >= rect[1] && px <= rect[2] && pz <= rect[3];
        };
        if (inside(x - 1, z))
            edges &= 0xFFFFFF00u;
        if (inside(x, z - 1))
            edges &= 0xFFFF00FFu;
        if (inside(x + 1, z))
            edges &= 0xFF00FFFFu;
        if (inside(x, z + 1))
            edges &= 0x00FFFFFFu;
        const uint32_t mask = ((((edges & 0x07070707u) + 0x07070707u) & 0x08080808u) * 0x01020408u) >> 27;
        for (uint32_t k = 0; k < 4; ++k) {
            const uint32_t v = At<uint32_t>(info, cell::kVertices + 4 * k);
            const uint32_t h = At<uint32_t>(info, cell::kHeights + 4 * k);
            AddVertex(sat, v + kLotOffset, h);
            AddFixed(sat, v + kLotOffset, h);
            if (static_cast<uint8_t>(mask) & Global<uint8_t>(addr::cSC4NetworkTool_CornerSideMasks + k)) {
                AddVertex(sat, v, h);
                AddFixed(sat, v, h);
            }
        }
        return;
    }
    if (At<uint8_t>(info, cell::kImmovable)) {
        for (uint32_t k = 0; k < 4; ++k)
            AddFixed(sat, At<uint32_t>(info, cell::kVertices + 4 * k), At<uint32_t>(info, cell::kHeights + 4 * k));
        return;
    }
    for (uint32_t k = 0; k < 4; ++k) {
        const uint32_t v = At<uint32_t>(info, cell::kVertices + 4 * k);
        const uint32_t h = At<uint32_t>(info, cell::kHeights + 4 * k);
        if (satisfier::Find(self + hfield::kSetB8, v)) {
            AddFixed(sat, v, h);
            continue;
        }
        AddVertex(sat, v, h);
        Restrict(sat, v, At<uint32_t>(info, cell::kRanges + 8 * k), At<uint32_t>(info, cell::kRanges + 8 * k + 4));
        PreferRange(sat, v, At<uint32_t>(info, cell::kRanges2 + 8 * k), At<uint32_t>(info, cell::kRanges2 + 8 * k + 4));
        if (At<uint8_t>(info, cell::kOccupied))
            PreferHeight(sat, v, h);
    }
}

// --- InsertEqualityConstraint 0x00636000 (NAM calls it) ----------------------------

// Which exit the game takes: 0 the tail jump, 1 v1 == v2, 2 v1 < 0, 3 v2 <
// 0 (tested in that order).
uint32_t __cdecl EqualityExit(int32_t v1, int32_t v2)
{
    static bool logged = false;
    FirstCall("InsertEqualityConstraint", logged);
    if (v1 == v2)
        return 1;
    if (v1 < 0)
        return 2;
    if (v2 < 0)
        return 3;
    return 0;
}

std::uintptr_t g_insertEquality = addr::SC4VertexHeightSatisfier_InsertEqualityConstraint;

__declspec(naked) void InsertEqualityConstraintThunk()
{
    __asm {
        push ecx
        push dword ptr [esp + 12]
        push dword ptr [esp + 12]
        call EqualityExit
        add esp, 8
        pop ecx
        mov edx, dword ptr [esp + 8]
        cmp eax, 1
        je same
        cmp eax, 2
        je first_negative
        cmp eax, 3
        je second_negative
        mov eax, dword ptr [esp + 4]
        add ecx, 0x124
        jmp dword ptr [g_insertEquality]
    same:
        mov eax, dword ptr [esp + 4]
        cmp eax, edx
        ret 8
    first_negative:
        mov eax, dword ptr [esp + 4]
        test eax, eax
        ret 8
    second_negative:
        mov eax, dword ptr [esp + 4]
        test edx, edx
        ret 8
    }
}

// --- InsertSmoothnessConstraint 0x00636030 (NAM calls it) --------------------------

// Which exit the game takes (the arguments v1, v2, v3, s): 0 the call, 1 v1
// < 0, 2 v2 < 0, 3 v3 < 0, 4 v1 == v2, 5 v2 == v3, 6 v3 == v1.
uint32_t __cdecl SmoothnessExit(const int32_t* args)
{
    static bool logged = false;
    FirstCall("InsertSmoothnessConstraint", logged);
    if (args[0] < 0)
        return 1;
    if (args[1] < 0)
        return 2;
    if (args[2] < 0)
        return 3;
    if (args[0] == args[1])
        return 4;
    if (args[1] == args[2])
        return 5;
    if (args[2] == args[0])
        return 6;
    return 0;
}

std::uintptr_t g_insertCurvature = addr::SC4VertexHeightSatisfier_InsertCurvatureConstraint;

__declspec(naked) void InsertSmoothnessConstraintThunk()
{
    __asm {
        sub esp, 4
        push eax
        push edx
        push ecx
        lea eax, [esp + 20]
        push eax
        call SmoothnessExit
        add esp, 4
        mov dword ptr [esp + 12], eax
        pop ecx
        pop edx
        pop eax
        cmp dword ptr [esp], 0
        je call_curvature
        cmp dword ptr [esp], 1
        je first_negative
        cmp dword ptr [esp], 2
        je second_negative
        cmp dword ptr [esp], 3
        je third_negative
        cmp dword ptr [esp], 4
        je first_second
        cmp dword ptr [esp], 5
        je second_third
        // v3 == v1
        lea esp, [esp + 4]
        push esi
        mov esi, dword ptr [esp + 8]
        mov edx, dword ptr [esp + 12]
        mov eax, dword ptr [esp + 16]
        cmp eax, esi
        pop esi
        ret 0x10
    first_negative:
        lea esp, [esp + 4]
        push esi
        mov esi, dword ptr [esp + 8]
        test esi, esi
        pop esi
        ret 0x10
    second_negative:
        lea esp, [esp + 4]
        mov edx, dword ptr [esp + 8]
        test edx, edx
        ret 0x10
    third_negative:
        lea esp, [esp + 4]
        mov edx, dword ptr [esp + 8]
        mov eax, dword ptr [esp + 12]
        test eax, eax
        ret 0x10
    first_second:
        lea esp, [esp + 4]
        push esi
        mov esi, dword ptr [esp + 8]
        mov edx, dword ptr [esp + 12]
        mov eax, dword ptr [esp + 16]
        cmp esi, edx
        pop esi
        ret 0x10
    second_third:
        lea esp, [esp + 4]
        push esi
        mov esi, dword ptr [esp + 8]
        mov edx, dword ptr [esp + 12]
        mov eax, dword ptr [esp + 16]
        cmp edx, eax
        pop esi
        ret 0x10
    call_curvature:
        lea esp, [esp + 4]
        push esi
        push edi
        mov esi, dword ptr [esp + 12]
        mov edx, dword ptr [esp + 16]
        mov eax, dword ptr [esp + 20]
        mov edi, dword ptr [esp + 24]
        push edi
        push eax
        push edx
        push esi
        add ecx, 0x124
        call dword ptr [g_insertCurvature]
        pop edi
        pop esi
        ret 0x10
    }
}

// --- GetCrossSections 0x00636360 (NAM calls it) ------------------------------------

// What the thunk sets on the exits the body decides, and what it needs for
// the one whose last call is the cell entry.
struct CrossRegs {
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t key;
    char* self;
    uint32_t* start;
};

// Whether `vertex` is one of the cell's four corners, and the address of the
// corner it matched (else info + 0xB8, the game's pointer after the walk).
inline bool IsCorner(char* info, uint32_t vertex, std::uintptr_t* where)
{
    for (uint32_t k = 0; k < 4; ++k) {
        const std::uintptr_t at = reinterpret_cast<std::uintptr_t>(info) + cell::kVertices + 4 * k;
        if (*reinterpret_cast<const uint32_t*>(at) == vertex) {
            if (where)
                *where = at;
            return true;
        }
    }
    if (where)
        *where = reinterpret_cast<std::uintptr_t>(info) + cell::kVertices + 16;
    return false;
}

// The arguments: a cell info, a network type, a side, the pairs mid, start
// and end (two vertex words each), the diagonal flag out. The edge word of
// the type, its side's kind k (its low two bits on that side; 0: AL 0). r =
// (k + 2 side + 2) & 3 picks the vertex steps (+0x250 / +0x270 by r); the
// flag is r's low bit. mid: the cell's pair (+ 100000 for a network lot);
// end: the neighbour's on the side (+ 100000 when that neighbour is a
// network lot; when the neighbour is not in +0xA4, a word that is not a
// corner of the cell replaced by the other, both -1 when neither is); start:
// the pair of the cell across, side d2 = (side + k) & 3: when the edge has a
// byte on d2 and that cell is in the city and in +0xA4, the cell entry is
// the last call (the thunk's) and the pair gets + 100000 for a network lot;
// otherwise the cell's lot offset is added and the words are checked
// against the cell's corners as end's. AL 1.
uint32_t __stdcall CrossSectionsBody(char* self, const uint32_t* args, CrossRegs* regs)
{
    static bool logged = false;
    FirstCall("GetCrossSections", logged);
    char* const info = reinterpret_cast<char*>(args[0]);
    const uint32_t type = args[1], side = args[2];
    uint32_t* const mid = reinterpret_cast<uint32_t*>(args[3]);
    uint32_t* const start = reinterpret_cast<uint32_t*>(args[4]);
    uint32_t* const end = reinterpret_cast<uint32_t*>(args[5]);
    uint8_t* const diagonal = reinterpret_cast<uint8_t*>(args[6]);
    uint32_t* const edgeWord =
        reinterpret_cast<uint32_t*>(reinterpret_cast<std::uintptr_t>(info) + type * 4 + cell::kByType);
    const uint32_t shift = (side * 8) & 31;
    const uint32_t shifted = *edgeWord >> shift;
    const uint32_t kind = shifted & 3;
    if (kind == 0) {
        regs->eax = shifted & 0xFFFFFF00u;
        regs->ecx = side * 8;
        regs->edx = side;
        return 0;
    }
    const uint32_t r = (kind + 2 * (side + 1)) & 3;
    *diagonal = static_cast<uint8_t>(r & 1);
    const uint32_t stride = At<uint32_t>(self, field::kVertexStride);
    const uint32_t stepA = At<uint32_t>(self, hfield::kVertexStepsA + r * 4);
    const uint32_t stepB = At<uint32_t>(self, hfield::kVertexStepsB + r * 4);
    const uint32_t cx = Field<uint32_t>(info, cell::kX), cz = Field<uint32_t>(info, cell::kZ);
    const uint32_t base = cz * stride + cx;
    const uint32_t offset = Field<uint8_t>(info, cell::kNetworkLot) ? kLotOffset : 0u;
    mid[0] = offset + base + stepA;
    mid[1] = offset + base + stepB;
    // The neighbour on the side.
    const uint32_t nx = static_cast<uint32_t>(Global<int32_t>(addr::cSC4NetworkTool_CrossSideDx + side * 4)) + cx;
    const uint32_t nz = static_cast<uint32_t>(Global<int32_t>(addr::cSC4NetworkTool_CrossSideDz + side * 4)) + cz;
    const uint32_t nbase = stride * nz + nx;
    end[0] = stepA + nbase;
    end[1] = stepB + nbase;
    if (nx < At<uint32_t>(self, field::kCellsX) && nz < At<uint32_t>(self, field::kCellsZ)) {
        char* const neighbour = Entry(self, (nz << 16) + nx);
        if (neighbour && At<uint8_t>(neighbour, cell::kNetworkLot)) {
            end[0] += kLotOffset;
            end[1] += kLotOffset;
        }
    }
    if (!CallThis<uint8_t, uint32_t, uint32_t>(addr::cSC4NetworkTool_IsCellInHeightSet, self, nx, nz)) {
        const uint32_t first = end[0];
        const bool firstIs = IsCorner(info, first, nullptr);
        const bool secondIs = IsCorner(info, end[1], nullptr);
        if (!firstIs) {
            if (secondIs) {
                end[0] = end[1];
            } else {
                end[1] = 0xFFFFFFFFu;
                end[0] = 0xFFFFFFFFu;
            }
        } else if (!secondIs) {
            end[1] = first;
        }
    }
    // The cell across (kind is 1 to 3, so the game's default case, which
    // would take the end pair's address, is never reached).
    const uint32_t across = (side + kind) & 3;
    const uint32_t ax = static_cast<uint32_t>(Global<int32_t>(addr::cSC4NetworkTool_CrossSideDx + across * 4)) + cx;
    const uint32_t az = static_cast<uint32_t>(Global<int32_t>(addr::cSC4NetworkTool_CrossSideDz + across * 4)) + cz;
    const uint32_t abase = stride * az + ax;
    start[0] = stepA + abase;
    start[1] = stepB + abase;
    if (CallThis<uint8_t, uint32_t>(addr::cSC4NetworkTool_EdgeByte, edgeWord, across) &&
        ax < At<uint32_t>(self, field::kCellsX) && az < At<uint32_t>(self, field::kCellsZ) &&
        CallThis<uint8_t, uint32_t, uint32_t>(addr::cSC4NetworkTool_IsCellInHeightSet, self, ax, az)) {
        regs->key = (az << 16) + ax;
        regs->self = self;
        regs->start = start;
        return 1;
    }
    start[1] += offset;
    start[0] += offset;
    std::uintptr_t matched = 0;
    const uint32_t first = start[0], second = start[1];
    const bool firstIs = IsCorner(info, first, &matched);
    const bool secondIs = IsCorner(info, second, nullptr);
    regs->eax = (reinterpret_cast<std::uintptr_t>(start) & 0xFFFFFF00u) | 1u;
    regs->ecx = secondIs ? 1u : 0u;
    regs->edx = static_cast<uint32_t>(matched & 0xFFFFFF00u) | (firstIs ? 1u : 0u);
    if (firstIs) {
        if (!secondIs)
            start[1] = first;
    } else if (secondIs) {
        start[0] = second;
    } else {
        start[1] = 0xFFFFFFFFu;
        start[0] = 0xFFFFFFFFu;
    }
    return 0;
}

std::uintptr_t g_cellEntry = addr::cSC4NetworkManager_ToolCellEntry;

// The frame: the game's `sub esp, 8` first, so that the last `add esp, 8`
// before `ret 0x1C` runs from the same depth as the game's and leaves its
// flags.
__declspec(naked) void GetCrossSectionsThunk()
{
    __asm {
        sub esp, 8
        push ebx
        push esi
        push edi
        push ebp
        sub esp, 24
        mov eax, esp
        lea edx, [esp + 52]
        push eax
        push edx
        push ecx
        call CrossSectionsBody
        test eax, eax
        jne entry_call
        mov eax, dword ptr [esp]
        mov ecx, dword ptr [esp + 4]
        mov edx, dword ptr [esp + 8]
        jmp done
    entry_call:
        mov esi, dword ptr [esp + 20]
        mov ecx, dword ptr [esp + 16]
        push dword ptr [esp + 12]
        call dword ptr [g_cellEntry]
        test eax, eax
        je answer
        mov cl, byte ptr [eax + 0x56]
        test cl, cl
        je answer
        mov eax, esi
        mov edx, dword ptr [eax]
        mov ecx, dword ptr [eax + 4]
        add edx, 0x186A0
        add ecx, 0x186A0
        mov dword ptr [eax], edx
        mov dword ptr [eax + 4], ecx
    answer:
        mov al, 1
    done:
        lea esp, [esp + 24]
        pop ebp
        pop edi
        pop esi
        pop ebx
        add esp, 8
        ret 0x1C
    }
}

// ====================================================================================
// Installation
// ====================================================================================

#define OPENSC4_DETOUR(address, fn) OPENSC4_NETTOOL_DETOUR(address, fn)

const Detour kHeightCons[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkTool_InsertEqualityConstraint, InsertEqualityConstraintThunk),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_InsertSmoothnessConstraint, InsertSmoothnessConstraintThunk),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_GetCrossSections, GetCrossSectionsThunk),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_IsCellInHeightSet, IsCellInHeightSet),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_InsertRangeConstraintsForCell, InsertRangeConstraintsForCell),
};

#undef OPENSC4_DETOUR

bool InstallHeightCons(registry::Mode) { return InstallDetours(kHeightCons, "heightcons"); }

const bool g_registered[] = {
    registry::Register({"simulators.nettool.heightcons", "simulators/nettool", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkTool_GetCrossSections,
                        "Our network tool's per-cell height constraints next to the NAM-guarded 0x00636690: the "
                        "equality and smoothness inserts and GetCrossSections (which NAM calls; naked thunks), the "
                        "set test and slot 76 InsertRangeConstraintsForCell (5 detours; "
                        "src/core/nettool_heights_2.cpp)",
                        false, InstallHeightCons}),
};

} // namespace
} // namespace opensc4::nettool_sim
