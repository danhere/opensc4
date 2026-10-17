// Our cSC4NetworkTool family's underground tools, first part (build 641;
// part u1 of docs/nettool.md): the underground tools' base (vtables
// 0x00AAA950/0x00AAA93C, constructor 0x00657FD0), the pipe tool (0x00AA99A8/
// 0x00AA9994, constructor 0x00648EC0) and the subway tool (0x00AAA5C0/
// 0x00AAA5AC, constructor 0x00653D60). One group,
// simulators.nettool.underground: a list of detours at the game's
// addresses. Calls between them go to the game's addresses, so each detour
// stands alone and OPENSC4_NETTOOL_SKIP bisects them.
//
// Object fields of the base past the general tool's: +0x368 a byte (Init
// once), +0x36C a float (the depth subtracted from the terrain's height),
// +0x384 and +0x388 two lists (SC4PairList: a 0xC-byte head linked to
// itself, freed with SC4PairList::Destroy 0x005650A0); the pipe and subway
// tools add a byte at +0x390.
//
// The second half (src/core/nettool_under_2.cpp, the same group) has slot
// 82 of the pipe and subway tools, the three cdecl lookups the network
// manager forwards to, slots 78, 80, 16, 71, 76, 70, 58, 55 and the
// occupant placer 0x006596C0. Left as the game's: 0x00659240
// (PropagateFromCell: its 12-byte solved-cell record's second word is a
// byte store over three bytes of stack leftover) and 0x006588D0 (the
// netmgr's underground rebuild, 2,402 bytes, keeps the depth on the x87
// stack across its loops). Slot 81 of the pipe and subway tools (0x00648F40,
// 0x00653D90) is folded with the occupants' class-ID getters (the class
// registration at 0x004606A6/0x004606B8 calls them): 6-byte constant
// getters, not detoured and not swapped.
//
// NAM. None of these functions is guarded or called by NAM's DLL; the
// fields are written in the game's memory in the game's layout.
//
// Floats: slot 74 (0x00648F20) and slot 73 (0x00657790) give ST0 unrounded
// (a float minus the float +0x36C at the x87's 53-bit precision). Slot 74
// is the game's three instructions in a naked thunk; slot 73 is C++ on
// doubles: the float load is exact, the terrain's ST0 (slot 0x30) comes back
// as a double (lossless at 53-bit precision), the subtraction is one double
// operation, the result is returned in ST0 as a double.
//
// Registers: slots 59, 68, 74 and 54 are naked thunks that keep every
// register the game's leave (slot 68 is a bare `ret 0xC`: its AL is the
// caller's leftover EAX, the vtable pointer it called through; slot 54
// leaves EAX alone when the out pointer is null). The constructors give EAX
// the object (0x006108D0's callers at 0x00610E3E/0x00610F0C read it); no
// caller of the constructors, 0x00657FD0 or the destructors reads ECX or
// EDX after the call (scratch regscan.py).
//
// Calls, each count from the asm's pushes and the callee's own `ret N`:
// the general tool's constructor 0x0062C5F0 one (ret 4: the type), its
// destructor 0x0062CAF0 none, its Init 0x006269C0 none; SC4PairList::
// Destroy 0x005650A0 none; the STL allocator 0x0090CF54 cdecl one (0xC);
// operator delete 0x005E5620 cdecl one; the base constructor 0x00657FD0 one
// (ret 4: 4 pipe, 7 subway); the destructor 0x00648E90 none;
// SC4VertexHeightSatisfier::GetResult 0x0065EB00 two (ret 8: the vertex, the
// address of the result); the terrain's slot 0x30 one (ret 4: the vertex;
// ST0); the tool's slot 82 (+0x148) four (ret 0x10); ToolCellEntry
// 0x00633220 one (ret 4: a cell key); 0x00659240 two (ret 8: the cell
// entry, 0); the resource manager 0x00B43CA8 slot 0xC five (ret 0x14:
// key, IID, out, 0, 0; AL), the resource's slot 0x1C none and slot 8 none;
// SC4::GetPropertyFloat 0x005FD4F0 and GetPropertyBool 0x005FD390 cdecl
// three; the tool's slot 81 (+0x144) none; SC4PipeBase::FindByInterfaceAt
// 0x006584D0 and FindPieceAt 0x006585E0 cdecl four; the general tool's Shutdown 0x00626D30 none (AL);
// the free 0x0090CF63 cdecl one.
#include "core/nettool_heights.h"
#include "core/nettool_sim.h"
#include "core/nettool_under.h"

#include "core/game.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::nettool_sim {
namespace {

// The first-call log lines of the naked thunks (cdecl, called with every
// register the thunk keeps pushed around them).
void __cdecl LogHeightForTerrain()
{
    static bool logged = false;
    FirstCall("UndergroundGetNetworkHeightForTerrainHeight", logged);
}
void __cdecl LogHighlights()
{
    static bool logged = false;
    FirstCall("UndergroundUpdateOccupantHighlights", logged);
}
void __cdecl LogReserved()
{
    static bool logged = false;
    FirstCall("UndergroundCheckForReservedTiles", logged);
}
void __cdecl LogCreate()
{
    static bool logged = false;
    FirstCall("UndergroundCreateNetworkOccupant", logged);
}

// 0x00648F20 slot 74 GetNetworkHeightForTerrainHeight (ret 4: a float): the
// float minus +0x36C, in ST0 unrounded. The game's instructions.
__declspec(naked) void GetNetworkHeightForTerrainHeightThunk()
{
    __asm {
        push eax
        push ecx
        push edx
        call LogHeightForTerrain
        pop edx
        pop ecx
        pop eax
        fld dword ptr [esp + 4]
        fsub dword ptr [ecx + 0x36C]
        ret 4
    }
}

// 0x00648F30 slot 59 UpdateOccupantHighlights (ret 4: a count): EAX the
// argument; nothing else.
__declspec(naked) void UpdateOccupantHighlightsThunk()
{
    __asm {
        push ecx
        push edx
        call LogHighlights
        pop edx
        pop ecx
        mov eax, dword ptr [esp + 4]
        ret 4
    }
}

// 0x00657730 slot 68 CheckForReservedTiles (ret 0xC): nothing at all; the
// caller's AL is whatever EAX held (kept).
__declspec(naked) void CheckForReservedTilesThunk()
{
    __asm {
        push eax
        push ecx
        push edx
        call LogReserved
        pop edx
        pop ecx
        pop eax
        ret 0xC
    }
}

// 0x00657740 slot 54 CreateNetworkOccupant (ret 0x10: a cell info, the
// occupant out, a box out, a kind): with an out pointer, the tool's slot 82
// (+0x148) with (the cell info, the address of the out argument's own
// slot, the box, the kind), then the out pointer gets what slot 82 left in
// that slot, and EAX it; without one nothing (EAX kept). The game's
// instructions.
__declspec(naked) void CreateNetworkOccupantThunk()
{
    __asm {
        push eax
        push ecx
        push edx
        call LogCreate
        pop edx
        pop ecx
        pop eax
        push esi
        mov esi, dword ptr [esp + 0xC]
        test esi, esi
        je done
        mov edx, dword ptr [esp + 0x14]
        mov eax, dword ptr [ecx]
        push edx
        mov edx, dword ptr [esp + 0x14]
        push edx
        lea edx, [esp + 0x14]
        push edx
        mov edx, dword ptr [esp + 0x14]
        push edx
        call dword ptr [eax + 0x148]
        mov eax, dword ptr [esp + 0xC]
        mov dword ptr [esi], eax
    done:
        pop esi
        ret 0x10
    }
}

// 0x00657FD0 (ret 4: the network type): the general tool's constructor
// 0x0062C5F0 with the type, the base's vtables, +0x368 cleared, then each
// list: the field cleared, a 0xC-byte head from the STL allocator linked to
// itself, the field set. EAX the object.
char* __fastcall Construct(char* self, void*, uint32_t type)
{
    static bool logged = false;
    FirstCall("UndergroundConstruct", logged);
    CallThis<char*, uint32_t>(addr::cSC4NetworkManager_NetworkToolConstruct, self, type);
    At<std::uintptr_t>(self, 0) = under::kVtable;
    At<std::uintptr_t>(self, 4) = under::kVtable4;
    At<uint8_t>(self, under::kInited) = 0;
    for (const std::size_t list : {under::kListA, under::kListB}) {
        At<void*>(self, list) = nullptr;
        char* const head = CallCdecl<char*, uint32_t>(addr::SC4_stl_allocate, 0xC);
        At<char*>(head, 0) = head;
        At<char*>(head, 4) = head;
        At<char*>(self, list) = head;
    }
    return self;
}

// 0x00648EC0 / 0x00653D60 (no arguments): the base constructor 0x00657FD0
// with the type (4 pipe, 7 subway), the tool's vtables, +0x390 cleared. EAX
// the object.
char* ToolConstruct(char* self, uint32_t type, std::uintptr_t vtable, std::uintptr_t vtable4)
{
    CallThis<char*, uint32_t>(addr::cSC4UndergroundTool_Construct, self, type);
    At<std::uintptr_t>(self, 0) = vtable;
    At<std::uintptr_t>(self, 4) = vtable4;
    At<uint8_t>(self, under::kByte390) = 0;
    return self;
}
char* __fastcall PipeConstruct(char* self, void*)
{
    static bool logged = false;
    FirstCall("PipeToolConstruct", logged);
    return ToolConstruct(self, under::kPipeType, under::kPipeVtable, under::kPipeVtable4);
}
char* __fastcall SubwayConstruct(char* self, void*)
{
    static bool logged = false;
    FirstCall("SubwayToolConstruct", logged);
    return ToolConstruct(self, under::kSubwayType, under::kSubwayVtable, under::kSubwayVtable4);
}

// 0x00648E90 (no arguments; the pipe and subway deleting destructor's): the
// lists +0x388 then +0x384 destroyed, then the general tool's destructor
// (the game jumps to it).
void __fastcall Destruct(char* self, void*)
{
    static bool logged = false;
    FirstCall("PipeToolDestruct", logged);
    CallThis<void>(addr::SC4PairList_Destroy, self + under::kListB);
    CallThis<void>(addr::SC4PairList_Destroy, self + under::kListA);
    CallThis<void>(addr::cSC4NetworkTool_Destruct, self);
}

// 0x00648F50 slot 51 of the pipe and subway tools (ret 4: flags): the
// destructor 0x00648E90; bit 0: operator delete. EAX the object.
char* __fastcall PipeDeletingDestructor(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("PipeToolDeletingDestructor", logged);
    CallThis<void>(addr::cSC4PipeTool_Destruct, self);
    if (flags & 1)
        CallCdecl<void, void*>(addr::SC4_operator_delete, self);
    return self;
}

// 0x00658250 slot 51 of the base (ret 4: flags): the same as the pipe's,
// with the destructor inlined (the lists +0x388 and +0x384, the general
// tool's destructor). EAX the object.
char* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("UndergroundDeletingDestructor", logged);
    CallThis<void>(addr::SC4PairList_Destroy, self + under::kListB);
    CallThis<void>(addr::SC4PairList_Destroy, self + under::kListA);
    CallThis<void>(addr::cSC4NetworkTool_Destruct, self);
    if (flags & 1)
        CallCdecl<void, void*>(addr::SC4_operator_delete, self);
    return self;
}

// 0x00657770 slot 3 Init (no arguments): the first time (+0x368 clear) the
// byte set and the general tool's Init 0x006269C0. AL 1.
bool __fastcall Init(char* self, void*)
{
    static bool logged = false;
    FirstCall("UndergroundInit", logged);
    if (!At<uint8_t>(self, under::kInited)) {
        At<uint8_t>(self, under::kInited) = 1;
        CallThis<uint8_t>(addr::cSC4NetworkTool_Init, self);
    }
    return true;
}

// 0x00657790 slot 73 GetNetworkVertexHt (ret 4: a vertex): the first
// satisfier's result for the vertex (written into the game's own argument
// slot, which holds the vertex until then: ours starts the same), else the
// terrain's height (slot 0x30, ST0) minus the float +0x36C, unrounded.
double __fastcall GetNetworkVertexHt(char* self, void*, uint32_t vertex)
{
    static bool logged = false;
    FirstCall("UndergroundGetNetworkVertexHt", logged);
    uint32_t slot = vertex;
    if (heights::GetResult(self + field::kSatisfier1, vertex, &slot))
        return static_cast<double>(satisfier::Float(slot));
    const double terrain = heights::TerrainHeight(self, vertex);
    return terrain - static_cast<double>(At<float>(self, under::kDepth));
}

// 0x00659410 slot 75 PropagateSolvedCellsForSmoothing (no arguments): for
// each of the (end - begin) / 12 elements of the vector +0x98 (counted
// once, signed; the begin read again each time), the cell entry of its key
// (+8) and, when there is one, 0x00659240 with it and 0.
void __fastcall PropagateSolvedCellsForSmoothing(char* self, void*)
{
    static bool logged = false;
    FirstCall("UndergroundPropagateSolvedCellsForSmoothing", logged);
    const int32_t bytes = At<int32_t>(self, under::kCellsEnd) - At<int32_t>(self, under::kCellsBegin);
    const int32_t count = bytes / static_cast<int32_t>(under::kCellSize);
    for (int32_t i = 0; i < count; ++i) {
        char* const begin = At<char*>(self, under::kCellsBegin);
        const uint32_t key = At<uint32_t>(begin + i * under::kCellSize, under::kCellKey);
        char* const entry = CallThis<char*, uint32_t>(addr::cSC4NetworkManager_ToolCellEntry, self, key);
        if (entry)
            CallThis<void, char*, uint32_t>(addr::cSC4UndergroundTool_PropagateFromCell, self, entry, 0u);
    }
}

// 0x00657980 slot 79 ReloadNetworkSpecificInfo (ret 4: an instance): the
// resource (0x6534284A, 0x084344E0, the instance) from the resource manager
// (slot 0xC: key, 0xA52160F5, out, 0, 0); without it nothing (AL 0). Else
// its property holder (slot 0x1C): the floats 0xA96E5641 into +0x36C,
// 0x496E564C into +0x370, 0x096E5653 into +0x378, the bool 0x496E5A36 into
// +0x374; the resource released (slot 8), EAX what that answers. The game's
// out pointer is its own argument slot (unread when the lookup fails).
uint32_t __fastcall ReloadNetworkSpecificInfo(char* self, void*, uint32_t instance)
{
    static bool logged = false;
    FirstCall("UndergroundReloadNetworkSpecificInfo", logged);
    void* resource = reinterpret_cast<void*>(static_cast<std::uintptr_t>(instance));
    uint32_t key[3] = {under::kInfoType, under::kInfoGroup, instance};
    void* const manager = Global<void*>(addr::SC4_ResourceManager);
    const uint8_t found = Virtual<uint8_t>(manager, 0xC, key, under::kResourceIid, &resource, 0u, 0u);
    if (!found)
        return found;
    void* const holder = Virtual<void*>(resource, 0x1C);
    CallCdecl<uint8_t, void*, uint32_t, char*>(addr::SC4_GetPropertyFloat, holder, 0xA96E5641u, self + under::kDepth);
    CallCdecl<uint8_t, void*, uint32_t, char*>(addr::SC4_GetPropertyFloat, holder, 0x496E564Cu, self + 0x370);
    CallCdecl<uint8_t, void*, uint32_t, char*>(addr::SC4_GetPropertyFloat, holder, 0x096E5653u, self + 0x378);
    CallCdecl<uint8_t, void*, uint32_t, char*>(addr::SC4_GetPropertyBool, holder, 0x496E5A36u, self + 0x374);
    return Virtual<uint32_t>(resource, 0x8);
}

// 0x00659470 slot 65 TestForAutoConnectOccupant (ret 8: x, z): two locals
// cleared; SC4PipeBase::FindByInterfaceAt (x, z, &first, the tool's slot 81
// (+0x144) as the interface ID): found, the first released (slot 8) and AL
// 1. Else SC4PipeBase::FindPieceAt (x, z, slot 81 again as the piece type,
// &second): found, the second released and AL 1; else AL 0. (The game
// pushes FindPieceAt's out pointer before it calls slot 81; slot 81 takes
// nothing, so the order of the calls is the same.)
bool __fastcall TestForAutoConnectOccupant(char* self, void*, uint32_t x, uint32_t z)
{
    static bool logged = false;
    FirstCall("UndergroundTestForAutoConnectOccupant", logged);
    void* first = nullptr;
    void* second = nullptr;
    const uint32_t id = Virtual<uint32_t>(self, 0x144);
    if (CallCdecl<uint8_t, uint32_t, uint32_t, void**, uint32_t>(addr::SC4PipeBase_FindByInterfaceAt, x, z, &first,
                                                                  id)) {
        Virtual<uint32_t>(first, 0x8);
        return true;
    }
    const uint32_t type = Virtual<uint32_t>(self, 0x144);
    if (CallCdecl<uint8_t, uint32_t, uint32_t, uint32_t, void**>(addr::SC4PipeBase_FindPieceAt, x, z, type, &second)) {
        Virtual<uint32_t>(second, 0x8);
        return true;
    }
    return false;
}

// One list of Shutdown's (the head at `list`): every node's object (+8)
// released (slot 8) walking the next links (+0), then every node freed
// (0x0090CF63; the head read again at each step, as the game does), the
// head linked to itself.
void EmptyList(char* self, std::size_t list)
{
    char* const head = At<char*>(self, list);
    if (At<char*>(head, 0) != head) {
        for (char* node = At<char*>(head, 0); node != head; node = At<char*>(node, 0))
            Virtual<uint32_t>(At<void*>(node, 8), 0x8);
    }
    char* node = At<char*>(At<char*>(self, list), 0);
    while (node != At<char*>(self, list)) {
        char* const freed = node;
        node = At<char*>(node, 0);
        CallCdecl<void, void*>(addr::SC4_Free, freed);
    }
    At<char*>(At<char*>(self, list), 0) = At<char*>(self, list);
    At<char*>(At<char*>(self, list), 4) = At<char*>(self, list);
}

// 0x00657AF0 slot 4 Shutdown (no arguments): when Init ran (+0x368) the
// byte cleared, the lists +0x384 and +0x388 emptied (EmptyList), then the
// general tool's Shutdown 0x00626D30 (the game jumps to it: its AL is the
// answer); else AL 1.
uint8_t __fastcall Shutdown(char* self, void*)
{
    static bool logged = false;
    FirstCall("UndergroundShutdown", logged);
    if (!At<uint8_t>(self, under::kInited))
        return 1;
    At<uint8_t>(self, under::kInited) = 0;
    EmptyList(self, under::kListA);
    EmptyList(self, under::kListB);
    return CallThis<uint8_t>(addr::cSC4NetworkTool_Shutdown, self);
}

#define OPENSC4_DETOUR(address, fn) OPENSC4_NETTOOL_DETOUR(address, fn)

const Detour kUnderground[] = {
    OPENSC4_DETOUR(addr::cSC4UndergroundTool_Construct, Construct),
    OPENSC4_DETOUR(addr::cSC4NetworkManager_PipeToolConstruct, PipeConstruct),
    OPENSC4_DETOUR(addr::cSC4NetworkManager_SubwayToolConstruct, SubwayConstruct),
    OPENSC4_DETOUR(addr::cSC4PipeTool_Destruct, Destruct),
    OPENSC4_DETOUR(addr::cSC4PipeTool_DeletingDestructor, PipeDeletingDestructor),
    OPENSC4_DETOUR(addr::cSC4UndergroundTool_DeletingDestructor, DeletingDestructor),
    OPENSC4_DETOUR(addr::cSC4UndergroundTool_Init, Init),
    OPENSC4_DETOUR(addr::cSC4UndergroundTool_GetNetworkVertexHt, GetNetworkVertexHt),
    OPENSC4_DETOUR(addr::cSC4UndergroundTool_GetNetworkHeightForTerrainHeight, GetNetworkHeightForTerrainHeightThunk),
    OPENSC4_DETOUR(addr::cSC4UndergroundTool_UpdateOccupantHighlights, UpdateOccupantHighlightsThunk),
    OPENSC4_DETOUR(addr::cSC4UndergroundTool_CheckForReservedTiles, CheckForReservedTilesThunk),
    OPENSC4_DETOUR(addr::cSC4UndergroundTool_CreateNetworkOccupant, CreateNetworkOccupantThunk),
    OPENSC4_DETOUR(addr::cSC4UndergroundTool_PropagateSolvedCellsForSmoothing, PropagateSolvedCellsForSmoothing),
    OPENSC4_DETOUR(addr::cSC4UndergroundTool_ReloadNetworkSpecificInfo, ReloadNetworkSpecificInfo),
    OPENSC4_DETOUR(addr::cSC4UndergroundTool_TestForAutoConnectOccupant, TestForAutoConnectOccupant),
    OPENSC4_DETOUR(addr::cSC4UndergroundTool_Shutdown, Shutdown),
};

#undef OPENSC4_DETOUR

bool InstallUnderground(registry::Mode)
{
    const bool first = InstallDetours(kUnderground, "underground");
    const bool second = InstallUnderground2();
    return first && second;
}

const bool g_registered[] = {
    registry::Register({"simulators.nettool.underground", "simulators/nettool", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4UndergroundTool_Construct,
                        "Our underground network tools' small functions (part u1): the base, pipe and subway "
                        "constructors, the destructor and two deleting destructors, Init, slots 73, 74, 59, 68, 54, "
                        "75, 79, 65 and 4 (16 detours; src/core/nettool_under.cpp), and slots 82, 78, 80, 16, 71, "
                        "76, 70, 58, 55 with their helpers (16 detours; src/core/nettool_under_2.cpp)",
                        false, InstallUnderground}),
};

} // namespace
} // namespace opensc4::nettool_sim
