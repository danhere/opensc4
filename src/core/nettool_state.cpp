// Our cSC4NetworkTool, first part (build 641; the class map, the vtables,
// the fields and the type table row are in src/core/nettool_sim.h): the
// identity, the getters and setters, the failure state and its text, the
// type row's IDs, the bridge and neighbour queries, OccupantExists, the
// placement parameters' copy and the small cell helpers next to them. Each
// is a detour at the game's address, but for four functions the vtable slot
// is swapped instead (simulators.nettool.slots): three whose code the linker
// folded with other classes' (the same address stands in other vtables) and
// UpdateOnZoneChange, whose three bytes are too short for a detour's jump.
// Those slots are swapped in every vtable of the tool family that holds the
// game's address (the general tool, the underground base, the pipe, subway
// and power line tools); the other classes' vtables keep the game's code.
//
// Every function was written from the disassembly. The detours are plain
// functions, so they clobber EAX, ECX and EDX, but for four that go through
// naked thunks because the game's leave EDX as it was and set ECX to a known
// value, and a caller relies on it: 0x0062B8E9 pushes EDX right after the
// call of GetFirstNetworkTypeFromFlags 0x00623E60. The thunks keep EDX and
// leave ECX as the game's do (GetFirstNetworkTypeFromFlags and its
// lowest-flag form 0x00623EC0: ECX the flags; EdgeByte 0x00620DE0 and
// EdgeConnects 0x00620EC0: ECX the index times 8). Of the rest, the game's
// Release 0x00623270 keeps ECX and EDX while the count stays above 1 and the
// getters of the vtable keep EDX (or ECX): all are reached only through the
// vtables or the adjustor thunks, where the callers reload. The direct
// callers of the other non-virtual functions were read: CopyWord 0x00620DD0
// (0x006478EC, 0x00647906: the loop reloads, the next instructions write
// DL and CL before any whole-register use), 0x00620D80 and 0x00620DA0 (the
// game's clobber both registers), 0x00624B70 and 0x00626860 (the same).
// `self` is the object's start.
//
// Left as the game's: the two adjustor thunks 0x0062C9A0 and 0x0062C9B0
// (they jump to our QueryInterface and Release), and slot 22,
// SetAnchorPoints 0x0062CA20 ("add ecx, 0x118; jmp 0x00611B00": the shape of
// an adjustor thunk into the point vector's assignment, shared template code
// with eighteen callers that is the game's everywhere).
//
// NAM: its NetworkSlopes stub calls GetFirstNetworkTypeFromFlags 0x00623E60
// at its address (cdecl, one argument); the detour keeps that convention and
// the registers. It reads the placement parameters' byte +0x25 at the
// object's +0x2B5: CopyPlacementParams writes the parameters in place with
// the game's layout. Nothing here keeps a copy of the tool, a cell info or
// the type table.
//
// Calls, each count from the asm's pushes for that call and the callee's own
// `ret N`:
//   - the tool's own slots through the vtable: 0x4 AddRef none (0x00695330,
//     plain ret), 0xCC the deleting destructor one (0x0062CD30, ret 4), 0xD0
//     GetFailureReasonText two (0x0062BF60, ret 8; the power line tool's
//     0x0064F570 takes the same two);
//   - the tool's functions at their addresses: the cell entry 0x00633220
//     one (ret 4), MarkInvalidSteps 0x00624BF0 one (ret 4; reached by the
//     game's jump with the same stack), CopyPlacementParams 0x00626860 one
//     (ret 4), GetFirstNetworkTypeFromFlags 0x00623E60 cdecl one;
//   - shared template code: the point vector's assignment 0x00611B00 one
//     (ret 4), the uint32 vector's 0x00462C10 one (ret 4), the float
//     vector's 0x00464430 one (ret 4);
//   - the occupant manager (+0x48): slot 0x80 four (out, x, z, filter;
//     0x0049E690, ret 0x10); the occupant it gives: slot 0 two (IID, out;
//     ret 8), slot 2 none;
//   - a string: slot 3 two (0x0090F546, ret 8), slot 4 one (0x0090F50F, ret
//     4); the string's construction from a C string 0x00408480 one (ret 4)
//     and its range assignment 0x00408A70 three (ret 0xC: begin, end, a tag
//     that is not read); the localized text 0x00603040 cdecl four (`add esp,
//     0x10`); the allocation 0x0090CF54 and the free 0x0090CF63 cdecl one.
#include "core/nettool_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace opensc4::nettool_sim {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("nettool: first %s", name);
}

// Hooks to leave out, for bisecting: OPENSC4_NETTOOL_SKIP lists function
// addresses (hex, eight digits), as OPENSC4_NETLOT_SKIP does. A slot is
// named by the address the game has in it.
bool Skipped(std::uintptr_t address)
{
    char list[1024] = {};
    if (!GetEnvironmentVariableA("OPENSC4_NETTOOL_SKIP", list, sizeof list))
        return false;
    char wanted[16];
    std::snprintf(wanted, sizeof wanted, "%08X", static_cast<unsigned>(address));
    return std::strstr(list, wanted) != nullptr;
}

bool InstallDetourList(const Detour* detours, std::size_t count, const char* what)
{
    if (!game::IsBuild641())
        return false;
    for (std::size_t i = 0; i < count; ++i)
        if (!addr_check::Find(detours[i].address)) {
            log::Write("nettool %s: no address entry for 0x%08X", what, static_cast<unsigned>(detours[i].address));
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
    log::Write("nettool %s: %u detours", what, installed);
    return true;
}

bool SwapSlotList(const Slot* slots, std::size_t count, const char* what)
{
    if (!game::IsBuild641())
        return false;
    for (std::size_t i = 0; i < count; ++i) {
        const auto* const vtable = reinterpret_cast<const std::uintptr_t*>(slots[i].vtable);
        if (vtable[slots[i].index] != slots[i].original) {
            log::Write("nettool %s: slot %d of %08X holds %08X, expected %08X; not installing", what, slots[i].index,
                       static_cast<unsigned>(slots[i].vtable), static_cast<unsigned>(vtable[slots[i].index]),
                       static_cast<unsigned>(slots[i].original));
            return false;
        }
    }
    unsigned swapped = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (Skipped(slots[i].original))
            continue;
        auto* const entry = reinterpret_cast<std::uintptr_t*>(slots[i].vtable) + slots[i].index;
        DWORD old = 0;
        if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &old))
            return false;
        *entry = reinterpret_cast<std::uintptr_t>(slots[i].ours);
        VirtualProtect(entry, sizeof(*entry), old, &old);
        ++swapped;
    }
    log::Write("nettool %s: %u slots are ours", what, swapped);
    return true;
}

namespace {

char* Row(char* self) { return At<char*>(self, field::kRow); }

// ====================================================================================
// simulators.nettool.state: identity, the setters and flags, the failure
// state and its text, the drag's queries, the placement parameters
// ====================================================================================

// Slot 0 (0x00623290, ret 8; the part at +4's thunk 0x0062C9A0 takes its
// offset off and jumps here): IIDs 1 and 0x0A74CA15 give the object,
// 0x452294AA the part at +4, each referenced through slot 1 of the object.
// The game's answer is AL = 1 over AddRef's EAX; for any other ID AL = 0
// over what its subtractions left in EAX, the ID less 0x452294AA. (The game
// also has a branch for a null object asked for the part at +4: it stores
// null and then reads the null object's vtable. Not written.)
uint32_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    static bool logged = false;
    FirstCall("QueryInterface", logged);
    if (iid == kIidUnknown || iid == kIidTool) {
        *object = self;
    } else if (iid == kIidMessageTarget) {
        *object = self + kMessagePart;
    } else {
        return (iid - kIidMessageTarget) & 0xFFFFFF00u;
    }
    return (Virtual<uint32_t>(self, slot::kAddRef) & 0xFFFFFF00u) | 1u;
}

// Slot 2 (0x00623270; the thunk 0x0062C9B0 jumps here): above 1 (unsigned)
// the count at +0x14 goes down by one and is the answer; else the count is
// left as it is and slot 51, the deleting destructor, is called with 1; the
// answer is 0.
uint32_t __fastcall Release(char* self, void*)
{
    static bool logged = false;
    FirstCall("Release", logged);
    const uint32_t count = At<uint32_t>(self, field::kRefCount);
    if (count > 1) {
        At<uint32_t>(self, field::kRefCount) = count - 1;
        return count - 1;
    }
    Virtual<void*>(self, slot::kDeletingDestructor, 1u);
    return 0;
}

// Slot 11 (0x0062C8C0): the byte +0x53 set to 1. Not a no-op as the survey
// and the header have it.
void __fastcall ForceReload(char* self, void*)
{
    static bool logged = false;
    FirstCall("ForceReload", logged);
    At<uint8_t>(self, field::kReload) = 1;
}

// Slot 13 (0x0062C8E0): the failure reason at +0x20C cleared.
void __fastcall ClearFailureReason(char* self, void*)
{
    static bool logged = false;
    FirstCall("ClearFailureReason", logged);
    At<uint32_t>(self, field::kFailure) = 0;
}

// Slot 14 (0x0062C8F0): 1 when the failure reason is not 0, else 0 (all of
// EAX).
uint32_t __fastcall GetFailureState(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetFailureState", logged);
    return At<uint32_t>(self, field::kFailure) != 0 ? 1u : 0u;
}

// Slot 15 (0x0062CA40, ret 4: a string): the text of the failure reason. A
// string of the game is made on the stack: its three pointers cleared, 8
// bytes from the STL allocator (0x0090CF54) for its characters (begin and
// end there, the capacity 8 further, the first byte 0), then the vtable
// 0x00A80810 and a reference count of 0. Slot 52 fills it from the reason
// at +0x20C; the caller's string is told to take its characters (its slot
// 3: the begin and the length); the vtable is stored again (the string's
// destructor, inlined) and the characters freed (0x0090CF63) when there are
// any. EAX is not set to anything the callers read.
void __fastcall GetFailureReasonString(char* self, void*, void* text)
{
    static bool logged = false;
    FirstCall("GetFailureReasonString", logged);
    GameString local;
    local.begin = nullptr;
    local.end = nullptr;
    local.capacity = nullptr;
    char* const memory = CallCdecl<char*, std::size_t>(addr::SC4_stl_allocate, kFirstTextBuffer);
    local.capacity = memory + kFirstTextBuffer;
    local.begin = memory;
    local.end = memory;
    *memory = 0;
    const uint32_t reason = At<uint32_t>(self, field::kFailure);
    local.vtable = addr::SC4String_Vtable;
    local.references = 0;
    Virtual<void>(self, slot::kGetFailureReasonText, reason, &local);
    Virtual<uint32_t>(text, string_slot::kFromCharLength, local.begin,
                      static_cast<uint32_t>(local.end - local.begin));
    local.vtable = addr::SC4String_Vtable;
    if (local.begin)
        CallCdecl<void, void*>(addr::SC4_Free, local.begin);
}

// Slot 52 (0x0062BF60, ret 8: a failure reason and a string of the game;
// `this` is not read; the power line tool's 0x0064F570 jumps here for a
// reason that is not 0): the string emptied (when it has characters the
// first is set to 0 and the end set to the begin), then, unless the reason
// is 0, the localized text of LTEXT group 0x2A592FD1 with the reason as the
// instance (0x00603040 with the string, the group, the reason and 0). When
// that fails the text "## Missing failure reason ##" (0x00AA8714): a string
// of the game made from it on the stack (0x00408480), its characters
// assigned to the caller's string's buffer (0x00408A70, unless the two are
// the same object, which a stack temporary never is; the tag the game
// passes is the address of its own second argument, not read), and the
// temporary's characters freed (0x0090CF63). EAX is not set to anything the
// callers read.
void __fastcall GetFailureReasonText(void*, void*, int32_t reason, GameString* text)
{
    static bool logged = false;
    FirstCall("GetFailureReasonText", logged);
    if (text->begin != text->end) {
        *text->begin = 0;
        text->end = text->begin;
    }
    if (reason == 0)
        return;
    if (CallCdecl<uint8_t, void*, uint32_t, int32_t, uint32_t>(addr::SC4_LoadLocalizedText, text,
                                                                 kFailureTextGroup, reason, 0u))
        return;
    GameString missing;
    CallThis<void*, const char*>(addr::SC4String_FromText, &missing,
                                 reinterpret_cast<const char*>(addr::cSC4NetworkTool_MissingFailureText));
    char* const characters = missing.begin;
    if (&missing.begin != &text->begin)
        CallThis<void, char*, char*, void*>(addr::SC4String_AssignRange, &text->begin, characters, missing.end,
                                            &text);
    if (characters)
        CallCdecl<void, void*>(addr::SC4_Free, characters);
}

// Slot 21 (0x0062C8D0, ret 4: a byte): the byte stored at +0x234.
void __fastcall EnablePreviewUpdate(char* self, void*, uint32_t enable)
{
    static bool logged = false;
    FirstCall("EnablePreviewUpdate", logged);
    At<uint8_t>(self, field::kPreviewUpdate) = static_cast<uint8_t>(enable);
}

// Slot 23 (0x0062CA30, ret 4: a vector of points): the dragged cells at
// +0x60 assigned to the caller's vector by the point vector's assignment
// 0x00611B00 (this the caller's vector). EAX is the assignment's.
void* __fastcall GetDraggedCells(char* self, void*, void* cells)
{
    static bool logged = false;
    FirstCall("GetDraggedCells", logged);
    return CallThis<void*, void*>(addr::cSC4NetworkManager_PointVectorAssign, cells, self + field::kCells);
}

// Slot 24 (0x0062CA10, ret 4: a count): the valid step count at +0x78 set,
// then MarkInvalidSteps 0x00624BF0 (t6; it marks the cells of the steps from
// the count on in the preview drawable and does not read its argument),
// which the game reaches by a jump with the same stack after storing the
// count over its own argument (the same value). The survey's "tail jump to
// 0x00611B00" is SetAnchorPoints'; this one is SetValidStepCount as the
// header has it. EAX is MarkInvalidSteps'.
uint32_t __fastcall SetValidStepCount(char* self, void*, uint32_t count)
{
    static bool logged = false;
    FirstCall("SetValidStepCount", logged);
    At<uint32_t>(self, field::kValidSteps) = count;
    return CallThis<uint32_t, uint32_t>(addr::cSC4NetworkTool_MarkInvalidSteps, self, count);
}

// Slot 28 (0x0062C9C0, ret 4: a step's index): whether the step's third
// word is 0. No bounds. AL only (the rest of EAX is the index times 3 in the
// game).
uint8_t __fastcall IsRegularStep(char* self, void*, uint32_t index)
{
    static bool logged = false;
    FirstCall("IsRegularStep", logged);
    char* const steps = At<char*>(self, field::kSteps);
    return At<uint32_t>(steps, index * step::kSize + step::kIrregular) == 0 ? 1 : 0;
}

// Slot 29 (0x0062C9E0, ret 4: a point out): the dragged cell whose index is
// the second word of the last valid step (the step count at +0x78 less one;
// no bounds: with no valid step it reads before the vector). The vector of
// cells is read again for the second word.
void __fastcall GetLastValidPoint(char* self, void*, uint32_t* point)
{
    static bool logged = false;
    FirstCall("GetLastValidPoint", logged);
    const uint32_t valid = At<uint32_t>(self, field::kValidSteps);
    char* const steps = At<char*>(self, field::kSteps);
    const uint32_t index = At<uint32_t>(steps, valid * step::kSize - (step::kSize - step::kLastCell));
    point[0] = At<uint32_t>(At<char*>(self, field::kCells), index * kPointSize);
    point[1] = At<uint32_t>(At<char*>(self, field::kCells), index * kPointSize + 4);
}

// Slot 30 (0x00626E80, ret 4: a vector of uint32): false (AL) when the
// possible bridges at +0x1F4 are none; else they are assigned to the
// caller's vector (0x00462C10, this the caller's vector) and the answer is
// true. The game reads the end before the begin.
uint8_t __fastcall GetPossibleBridges(char* self, void*, void* bridges)
{
    static bool logged = false;
    FirstCall("GetPossibleBridges", logged);
    const Vector* const possible = reinterpret_cast<const Vector*>(self + field::kPossibleBridges);
    char* const end = possible->end;
    if (possible->begin == end)
        return 0;
    CallThis<void*, const void*>(addr::SC4Uint32Vector_CopyAssign, bridges, possible);
    return 1;
}

// Slot 33 (0x00623B90): the float at +0x204, in ST0.
double __fastcall GetBridgeAltitudeOverride(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetBridgeAltitudeOverride", logged);
    return static_cast<double>(At<float>(self, field::kAltitudeOverride));
}

// Slot 34 (0x00623BA0, ret 4: a float): its four bytes stored at +0x204 as
// they are (the game moves them through EAX; taken as bits here so no
// conversion touches them). EAX is the bits.
uint32_t __fastcall SetBridgeAltitudeOverride(char* self, void*, uint32_t altitudeBits)
{
    static bool logged = false;
    FirstCall("SetBridgeAltitudeOverride", logged);
    At<uint32_t>(self, field::kAltitudeOverride) = altitudeBits;
    return altitudeBits;
}

// 0x00626860 (ret 4: the parameters to copy; this the parameters copied to;
// Init 0x00626AFC copies the type row's +0x50 into the tool's +0x290, slot
// 48 the tool's into the caller's): the placement parameters' assignment,
// member by member in the game's order: the words +0x0 to +0x20, the bytes
// +0x24 and +0x25, the uint32 vector +0x28 (0x00462C10), the words +0x34 to
// +0x40, the byte +0x44, the words +0x48 to +0x50, the uint32 vectors +0x54
// and +0x60, the float vector +0x6C (0x00464430), the words +0x78 to +0x9C
// and the uint32 vector +0xA0. EAX is this.
char* __fastcall CopyPlacementParams(char* self, void*, char* from)
{
    static bool logged = false;
    FirstCall("CopyPlacementParams", logged);
    for (std::size_t offset = 0; offset <= 0x20; offset += 4)
        At<uint32_t>(self, offset) = At<uint32_t>(from, offset);
    At<uint8_t>(self, params::kByte24) = At<uint8_t>(from, params::kByte24);
    At<uint8_t>(self, params::kDiagonal) = At<uint8_t>(from, params::kDiagonal);
    CallThis<void*, char*>(addr::SC4Uint32Vector_CopyAssign, self + params::kVector28, from + params::kVector28);
    for (std::size_t offset = 0x34; offset <= 0x40; offset += 4)
        At<uint32_t>(self, offset) = At<uint32_t>(from, offset);
    At<uint8_t>(self, params::kByte44) = At<uint8_t>(from, params::kByte44);
    for (std::size_t offset = 0x48; offset <= 0x50; offset += 4)
        At<uint32_t>(self, offset) = At<uint32_t>(from, offset);
    CallThis<void*, char*>(addr::SC4Uint32Vector_CopyAssign, self + params::kVector54, from + params::kVector54);
    CallThis<void*, char*>(addr::SC4Uint32Vector_CopyAssign, self + params::kVector60, from + params::kVector60);
    CallThis<void*, char*>(addr::SC4FloatVector_Assign, self + params::kFloats6C, from + params::kFloats6C);
    for (std::size_t offset = 0x78; offset <= 0x9C; offset += 4)
        At<uint32_t>(self, offset) = At<uint32_t>(from, offset);
    CallThis<void*, char*>(addr::SC4Uint32Vector_CopyAssign, self + params::kVectorA0, from + params::kVectorA0);
    return self;
}

// Slot 48 (0x0062CAD0, ret 4: the caller's parameters): the tool's
// parameters at +0x290 copied into them by 0x00626860 (at its address).
// EAX is the copy's (the caller's parameters).
char* __fastcall GetPlacementParams(char* self, void*, char* out)
{
    static bool logged = false;
    FirstCall("GetPlacementParams", logged);
    return CallThis<char*, char*>(addr::cSC4NetworkTool_CopyPlacementParams, out, self + field::kParams);
}

// Slot 74 (0x0062C990, ret 4: a float; also the power line tool's): the
// float given, back in ST0 (the general tool's network sits on the ground).
double __fastcall GetNetworkHeightForTerrainHeight(void*, void*, float height)
{
    static bool logged = false;
    FirstCall("GetNetworkHeightForTerrainHeight", logged);
    return static_cast<double>(height);
}

// ====================================================================================
// simulators.nettool.queries: the type row's IDs, the bridge and neighbour
// queries, OccupantExists and the small helpers
// ====================================================================================

// Slots 42, 47, 43 and 44 (0x0062C910, 0x0062C920, 0x0062C930, 0x0062C940):
// the word of the type's row at +0x8, +0x10, +0x14 and +0x18. The row is
// not tested for null (it is null until Init).
uint32_t __fastcall ViewInputControlId(char* self, void*)
{
    static bool logged = false;
    FirstCall("ViewInputControlId", logged);
    return At<uint32_t>(Row(self), row::kViewInputControlId);
}

uint32_t __fastcall GetToolViewId(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetToolViewId", logged);
    return At<uint32_t>(Row(self), row::kToolViewId);
}

uint32_t __fastcall CursorId(char* self, void*)
{
    static bool logged = false;
    FirstCall("CursorId", logged);
    return At<uint32_t>(Row(self), row::kCursorId);
}

uint32_t __fastcall DirectionalCursorBase(char* self, void*)
{
    static bool logged = false;
    FirstCall("DirectionalCursorBase", logged);
    return At<uint32_t>(Row(self), row::kDirectionalCursorBase);
}

// Slot 45 (0x0062C950, ret 8: x out, z out): the row's +0x1C and +0x20; the
// row pointer is read again for the second. EAX is the second out's address
// in the game; the callers do not read it.
void __fastcall PenWidth(char* self, void*, int32_t* x, int32_t* z)
{
    static bool logged = false;
    FirstCall("PenWidth", logged);
    *x = At<int32_t>(Row(self), row::kPenWidthX);
    *z = At<int32_t>(Row(self), row::kPenWidthZ);
}

// The address of the type row's bridge record `id` (the game's 32-bit
// product, no bounds).
char* BridgeRecord(char* self, int32_t id)
{
    const uint32_t offset = static_cast<uint32_t>(id) * static_cast<uint32_t>(bridge::kSize);
    return At<char*>(Row(self), row::kBridges) + offset;
}

// Slot 35 (0x00624AF0, ret 8: a string, a bridge ID): the record's name
// (the word at +0x15C: the characters' begin of the game string at +0x158,
// which the RUL loader 0x006401D5 makes) given to the string's slot 4. EAX
// is the string's answer.
uint32_t __fastcall GetBridgeName(char* self, void*, void* text, int32_t id)
{
    static bool logged = false;
    FirstCall("GetBridgeName", logged);
    const uint32_t name = At<uint32_t>(BridgeRecord(self, id), bridge::kName);
    return Virtual<uint32_t>(text, string_slot::kFromChar, name);
}

// Slot 40 (0x00624B20, ret 0xC: a bridge ID, group out, instance out):
// false (AL) unless the record's bitmap group (+0x148) and instance
// (+0x14C) are both not 0; else both stored (the instance read again after
// the group is stored) and true.
uint8_t __fastcall GetBridgeBitmapKey(char* self, void*, int32_t id, uint32_t* group, uint32_t* instance)
{
    static bool logged = false;
    FirstCall("GetBridgeBitmapKey", logged);
    char* const record = BridgeRecord(self, id);
    const uint32_t found = At<uint32_t>(record, bridge::kBitmapGroup);
    if (found == 0 || At<uint32_t>(record, bridge::kBitmapInstance) == 0)
        return 0;
    *group = found;
    *instance = At<uint32_t>(record, bridge::kBitmapInstance);
    return 1;
}

// Whether the major bridges vector (+0xD8) is empty; its end.
bool NoMajorBridge(char* self)
{
    const Vector* const bridges = reinterpret_cast<const Vector*>(self + field::kMajorBridges);
    return bridges->begin == bridges->end;
}
char* MajorBridgesEnd(char* self) { return reinterpret_cast<const Vector*>(self + field::kMajorBridges)->end; }

// Slot 36 (0x00624130): -1 with no major bridge, else the word 0x1C bytes
// before the vector's end (the last bridge's ID).
int32_t __fastcall GetMajorBridge(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetMajorBridge", logged);
    if (NoMajorBridge(self))
        return -1;
    return *reinterpret_cast<const int32_t*>(MajorBridgesEnd(self) + majorbridge::kId);
}

// Slot 37 (0x00624180, ret 4: a float out): false (AL) with no major
// bridge; else the four bytes 0x18 before the end stored as they are, true.
uint8_t __fastcall GetMajorBridgeAltitude(char* self, void*, uint32_t* altitude)
{
    static bool logged = false;
    FirstCall("GetMajorBridgeAltitude", logged);
    if (NoMajorBridge(self))
        return 0;
    *altitude = *reinterpret_cast<const uint32_t*>(MajorBridgesEnd(self) + majorbridge::kAltitude);
    return 1;
}

// Slot 38 (0x00624150, ret 4: a float out): the same with the four bytes
// 0x14 before the end.
uint8_t __fastcall GetMajorBridgeReferenceAltitude(char* self, void*, uint32_t* altitude)
{
    static bool logged = false;
    FirstCall("GetMajorBridgeReferenceAltitude", logged);
    if (NoMajorBridge(self))
        return 0;
    *altitude = *reinterpret_cast<const uint32_t*>(MajorBridgesEnd(self) + majorbridge::kReferenceAltitude);
    return 1;
}

// Slot 39 (0x006241B0): false (AL) with no major bridge; else the byte 0x10
// before the end.
uint8_t __fastcall IsMajorBridgeLandBridge(char* self, void*)
{
    static bool logged = false;
    FirstCall("IsMajorBridgeLandBridge", logged);
    if (NoMajorBridge(self))
        return 0;
    return *reinterpret_cast<const uint8_t*>(MajorBridgesEnd(self) + majorbridge::kLandBridge);
}

// Slot 41 (0x006241D0, ret 4: the caller's record): false (AL) when the
// possible neighbour connections at +0x10C are none; else the first one's
// words +0x8, +0xC, +0x10 and its byte +0x14 copied to the record's +0x0,
// +0x4, +0x8 and +0xC, true. The vector's begin is read again for the copy.
uint8_t __fastcall GetPossibleNeighborConnection(char* self, void*, char* info)
{
    static bool logged = false;
    FirstCall("GetPossibleNeighborConnection", logged);
    const Vector* const connections = reinterpret_cast<const Vector*>(self + field::kNeighborConnections);
    if (connections->begin == connections->end)
        return 0;
    char* const first = connections->begin;
    At<uint32_t>(info, 0x0) = At<uint32_t>(first, neighbor::kFrom);
    At<uint32_t>(info, 0x4) = At<uint32_t>(first, neighbor::kFrom + 0x4);
    At<uint32_t>(info, 0x8) = At<uint32_t>(first, neighbor::kFrom + 0x8);
    At<uint8_t>(info, 0xC) = At<uint8_t>(first, neighbor::kFrom + 0xC);
    return 1;
}

// Slot 9 (0x006248D0, ret 0x10: x, z, a network mask, an ID out): the cell's
// entry from 0x00633220 with (z << 16) + x (an add, as the game has it). It
// finds the cell in the world cache or MAKES one (0x00633030), so this is not
// a pure query. False (AL) without an entry, when its byte +0x51 is 0 or
// when its flags (+0x8) have none of the mask; else the entry's word for the
// tool's type (+0x18 + 4 x type) is stored and the answer is true.
uint8_t __fastcall OccupantExists(char* self, void*, uint32_t x, uint32_t z, uint32_t mask, uint32_t* id)
{
    static bool logged = false;
    FirstCall("OccupantExists", logged);
    char* const entry = CallThis<char*, uint32_t>(addr::cSC4NetworkManager_ToolCellEntry, self, (z << 16) + x);
    if (!entry || !At<uint8_t>(entry, cell::kOccupied) || !(At<uint32_t>(entry, cell::kFlags) & mask))
        return 0;
    *id = At<uint32_t>(entry, At<uint32_t>(self, field::kType) * 4 + cell::kByType);
    return 1;
}

// 0x00624B70 (ret 0x10: x, z, an occupant filter, a network occupant out;
// PlaceNetworkOccupants, 0x00629CF0 and the cell info maker 0x00633030 call
// it): false (AL) when x or z is not below the tool's city size (+0x240,
// +0x244; unsigned). Else the occupant manager at +0x48 is asked for the
// first occupant of the cell that passes the filter (its slot 0x80 with an
// out pointer the game keeps in its own first argument's place, cleared
// first). When it says no, the occupant it may still have given is
// released and the answer is false. Else the occupant (not tested for null)
// is asked for the network occupant interface into the caller's out, it is
// released (tested for null), and the answer is the interface query's.
uint8_t __fastcall GetNetworkOccupantAt(char* self, void*, uint32_t x, uint32_t z, void* filter, void** out)
{
    static bool logged = false;
    FirstCall("GetNetworkOccupantAt", logged);
    if (x >= At<uint32_t>(self, field::kCellsX))
        return 0;
    if (z >= At<uint32_t>(self, field::kCellsZ))
        return 0;
    void* const occupants = At<void*>(self, field::kOccupants);
    void* found = nullptr;
    if (!Virtual<uint8_t>(occupants, occupants_slot::kGetFirstByCell, &found, x, z, filter)) {
        if (found)
            ReleaseOf(found);
        return 0;
    }
    const uint8_t answer = Virtual<uint8_t>(found, slot::kQueryInterface, kIidNetworkOccupant, out);
    if (found)
        ReleaseOf(found);
    return answer;
}

// 0x00620D80 (cdecl: a direction byte, x step out, z step out; 0x00623EE0,
// 0x00623F40 and 0x00635290 call it): the bytes of the two step tables at
// 0x00B0B3D8 (x) and 0x00B0B3F4 (z), 28 entries each, by the direction's low
// byte. EAX is the z step's byte, zero-extended.
uint32_t __cdecl DirectionOffsets(uint32_t direction, uint8_t* dx, uint8_t* dz)
{
    static bool logged = false;
    FirstCall("DirectionOffsets", logged);
    const uint32_t index = direction & 0xFF;
    *dx = Global<uint8_t>(addr::cSC4NetworkTool_DirectionDx + index);
    const uint8_t stepZ = Global<uint8_t>(addr::cSC4NetworkTool_DirectionDz + index);
    *dz = stepZ;
    return stepZ;
}

// 0x00620DA0 (ret 0x10: x, z, x out, z out; this a record whose byte +4 is
// a direction (inf); 0x00633FA0 and 0x006340D0 call it): the cell one step
// in that direction: x plus the x table's signed byte, z plus the z table's.
// The z byte is read before the x out is stored. EAX is the new z.
int32_t __fastcall DirectionStep(char* self, void*, int32_t x, int32_t z, int32_t* outX, int32_t* outZ)
{
    static bool logged = false;
    FirstCall("DirectionStep", logged);
    const uint32_t index = At<uint8_t>(self, 4);
    const int8_t stepZ = Global<int8_t>(addr::cSC4NetworkTool_DirectionDz + index);
    *outX = static_cast<int32_t>(Global<int8_t>(addr::cSC4NetworkTool_DirectionDx + index)) + x;
    const int32_t newZ = static_cast<int32_t>(stepZ) + z;
    *outZ = newZ;
    return newZ;
}

// 0x00620DD0 (ret 4: a word's address; the world cache's 0x006478A0 is the
// only caller): the word copied into this. EAX is this.
uint32_t* __fastcall CopyWord(uint32_t* self, void*, const uint32_t* from)
{
    static bool logged = false;
    FirstCall("CopyWord", logged);
    *self = *from;
    return self;
}

// 0x00620DE0 (ret 4: an index; this a word of four bytes, one per side, such
// as a cell info's edge word; 0x00628BD0 and 0x00636360 call it): the word
// shifted right by 8 times the index (the shift count's low five bits, as
// the processor takes it); the whole of EAX, not masked. The game leaves
// EDX as it was and the shift count in ECX: EdgeByteThunk below.
uint32_t __cdecl EdgeByte(const uint32_t* self, uint32_t index)
{
    static bool logged = false;
    FirstCall("EdgeByte", logged);
    return *self >> ((index << 3) & 31u);
}

// 0x00620EC0 (ret 4: an index; 0x006345B0, 0x00643FB0 and 0x00659240 call
// it): 1 when the low two bits of that byte are not both 0, else 0 (all of
// EAX). Registers as EdgeByte's: EdgeConnectsThunk below.
uint32_t __cdecl EdgeConnects(const uint32_t* self, uint32_t index)
{
    static bool logged = false;
    FirstCall("EdgeConnects", logged);
    return ((*self >> ((index << 3) & 31u)) & 3u) != 0 ? 1u : 0u;
}

// 0x00623E60 (cdecl: network flags; the slot 50 code 0x0062B8E4/0x0062B8EC,
// 0x00634877 to 0x006348AC, 0x00623EC0's jump, and NAM's NetworkSlopes stub
// at this address): the first network type 0 to 11 whose flag in the table
// at 0x00AA835C (read from the game's memory) shares a bit with the flags;
// 12 when none does (type 12's flag is never tested). The game leaves EDX as
// it was and the flags in ECX: FirstNetworkTypeThunk below.
uint32_t __cdecl GetFirstNetworkTypeFromFlags(uint32_t flags)
{
    static bool logged = false;
    FirstCall("GetFirstNetworkTypeFromFlags", logged);
    for (uint32_t type = 0; type < kTypesTested; ++type)
        if (Global<uint32_t>(addr::cSC4NetworkTool_TypeFlags + type * 4) & flags)
            return type;
    return kTypesTested;
}

// 0x00623EC0 (cdecl: network flags; 0x00636690 (guarded, the game's) and
// 0x00643FB0 call it): the lowest bit of the flags (x & ~(x - 1)) written
// over the caller's argument, as the game does, and 0x00623E60 on it (at its
// address: the game jumps there). Registers as 0x00623E60's: ECX the lowest
// bit, EDX kept (LowestNetworkTypeThunk below). `flags` is the argument's
// place on the caller's stack.
uint32_t __cdecl GetLowestNetworkTypeFromFlags(uint32_t* flags)
{
    static bool logged = false;
    FirstCall("GetLowestNetworkTypeFromFlags", logged);
    *flags = ~(*flags - 1) & *flags;
    return CallCdecl<uint32_t, uint32_t>(addr::cSC4NetworkTool_GetFirstNetworkTypeFromFlags, *flags);
}

// The thunks: the game's register state on return. Each keeps EDX and sets
// ECX as the game's function leaves it.
__declspec(naked) void FirstNetworkTypeThunk()
{
    __asm push edx
    __asm push dword ptr [esp + 8]
    __asm call GetFirstNetworkTypeFromFlags
    __asm add esp, 4
    __asm pop edx
    __asm mov ecx, dword ptr [esp + 4]
    __asm ret
}

__declspec(naked) void LowestNetworkTypeThunk()
{
    __asm push edx
    __asm lea eax, [esp + 8]
    __asm push eax
    __asm call GetLowestNetworkTypeFromFlags
    __asm add esp, 4
    __asm pop edx
    __asm mov ecx, dword ptr [esp + 4]
    __asm ret
}

__declspec(naked) void EdgeByteThunk()
{
    __asm push edx
    __asm push dword ptr [esp + 8]
    __asm push ecx
    __asm call EdgeByte
    __asm add esp, 8
    __asm pop edx
    __asm mov ecx, dword ptr [esp + 4]
    __asm shl ecx, 3
    __asm ret 4
}

__declspec(naked) void EdgeConnectsThunk()
{
    __asm push edx
    __asm push dword ptr [esp + 8]
    __asm push ecx
    __asm call EdgeConnects
    __asm add esp, 8
    __asm pop edx
    __asm mov ecx, dword ptr [esp + 4]
    __asm shl ecx, 3
    __asm ret 4
}

// ====================================================================================
// simulators.nettool.slots: the folded code and the three-byte slot, in the
// tool family's vtables only
// ====================================================================================

// Slot 31 (0x00623B70; also slot 38 of the city's vtable, GetNetworkManager):
// the preferred bridge at +0x200.
int32_t __fastcall GetPreferredBridge(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetPreferredBridge", logged);
    return At<int32_t>(self, field::kPreferredBridge);
}

// Slot 32 (0x00623B80, ret 4; in 19 other vtables): the preferred bridge
// stored at +0x200. EAX is the argument.
int32_t __fastcall SetPreferredBridge(char* self, void*, int32_t id)
{
    static bool logged = false;
    FirstCall("SetPreferredBridge", logged);
    At<int32_t>(self, field::kPreferredBridge) = id;
    return id;
}

// Slot 53 (0x0062C980, ret 8; "xor al, al; ret 8" in about 70 vtables):
// false (AL). The power line tool has its own (0x00652980).
uint8_t __fastcall DoNetworkSpecificPlacement(void*, void*, uint32_t, uint32_t)
{
    static bool logged = false;
    FirstCall("DoNetworkSpecificPlacement", logged);
    return 0;
}

// Slot 10 (0x0062C900, "ret 0x14": five arguments, none read; the header
// has none). The power line tool has its own (0x006522F0).
void __fastcall UpdateOnZoneChange(void*, void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)
{
    static bool logged = false;
    FirstCall("UpdateOnZoneChange", logged);
}

// ====================================================================================
// Installation
// ====================================================================================

#define OPENSC4_DETOUR(address, fn) OPENSC4_NETTOOL_DETOUR(address, fn)
#define OPENSC4_SLOT(vtable, index, original, fn) OPENSC4_NETTOOL_SLOT(vtable, index, original, fn)

const Detour kState[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkTool_QueryInterface, QueryInterface),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_Release, Release),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_ForceReload, ForceReload),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_ClearFailureReason, ClearFailureReason),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_GetFailureState, GetFailureState),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_GetFailureReasonString, GetFailureReasonString),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_GetFailureReasonText, GetFailureReasonText),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_EnablePreviewUpdate, EnablePreviewUpdate),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_GetDraggedCells, GetDraggedCells),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_SetValidStepCount, SetValidStepCount),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_IsRegularStep, IsRegularStep),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_GetLastValidPoint, GetLastValidPoint),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_GetPossibleBridges, GetPossibleBridges),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_GetBridgeAltitudeOverride, GetBridgeAltitudeOverride),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_SetBridgeAltitudeOverride, SetBridgeAltitudeOverride),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_CopyPlacementParams, CopyPlacementParams),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_GetPlacementParams, GetPlacementParams),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_GetNetworkHeightForTerrainHeight, GetNetworkHeightForTerrainHeight),
};

const Detour kQueries[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkTool_ViewInputControlId, ViewInputControlId),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_GetToolViewId, GetToolViewId),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_CursorId, CursorId),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_DirectionalCursorBase, DirectionalCursorBase),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_PenWidth, PenWidth),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_GetBridgeName, GetBridgeName),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_GetBridgeBitmapKey, GetBridgeBitmapKey),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_GetMajorBridge, GetMajorBridge),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_GetMajorBridgeAltitude, GetMajorBridgeAltitude),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_GetMajorBridgeReferenceAltitude, GetMajorBridgeReferenceAltitude),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_IsMajorBridgeLandBridge, IsMajorBridgeLandBridge),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_GetPossibleNeighborConnection, GetPossibleNeighborConnection),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_OccupantExists, OccupantExists),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_GetNetworkOccupantAt, GetNetworkOccupantAt),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_DirectionOffsets, DirectionOffsets),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_DirectionStep, DirectionStep),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_CopyWord, CopyWord),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_EdgeByte, EdgeByteThunk),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_EdgeConnects, EdgeConnectsThunk),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_GetFirstNetworkTypeFromFlags, FirstNetworkTypeThunk),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_GetLowestNetworkTypeFromFlags, LowestNetworkTypeThunk),
};

// The folded code and the three-byte slot, in every vtable of the tool
// family that holds the game's address (the power line tool has its own
// slots 10 and 53; the underground base's vtable is the pipe and subway
// tools' base and is swapped for completeness). None of these is called at
// its address by the tool's code.
const Slot kSlots[] = {
    OPENSC4_SLOT(addr::cSC4NetworkTool_vftable, slot_index::kGetPreferredBridge,
                 addr::cSC4NetworkTool_GetPreferredBridge, GetPreferredBridge),
    OPENSC4_SLOT(addr::cSC4NetworkTool_UndergroundVftable, slot_index::kGetPreferredBridge,
                 addr::cSC4NetworkTool_GetPreferredBridge, GetPreferredBridge),
    OPENSC4_SLOT(addr::cSC4NetworkTool_PipeVftable, slot_index::kGetPreferredBridge,
                 addr::cSC4NetworkTool_GetPreferredBridge, GetPreferredBridge),
    OPENSC4_SLOT(addr::cSC4NetworkTool_SubwayVftable, slot_index::kGetPreferredBridge,
                 addr::cSC4NetworkTool_GetPreferredBridge, GetPreferredBridge),
    OPENSC4_SLOT(addr::cSC4NetworkTool_PowerLineVftable, slot_index::kGetPreferredBridge,
                 addr::cSC4NetworkTool_GetPreferredBridge, GetPreferredBridge),
    OPENSC4_SLOT(addr::cSC4NetworkTool_vftable, slot_index::kSetPreferredBridge,
                 addr::cSC4NetworkTool_SetPreferredBridge, SetPreferredBridge),
    OPENSC4_SLOT(addr::cSC4NetworkTool_UndergroundVftable, slot_index::kSetPreferredBridge,
                 addr::cSC4NetworkTool_SetPreferredBridge, SetPreferredBridge),
    OPENSC4_SLOT(addr::cSC4NetworkTool_PipeVftable, slot_index::kSetPreferredBridge,
                 addr::cSC4NetworkTool_SetPreferredBridge, SetPreferredBridge),
    OPENSC4_SLOT(addr::cSC4NetworkTool_SubwayVftable, slot_index::kSetPreferredBridge,
                 addr::cSC4NetworkTool_SetPreferredBridge, SetPreferredBridge),
    OPENSC4_SLOT(addr::cSC4NetworkTool_PowerLineVftable, slot_index::kSetPreferredBridge,
                 addr::cSC4NetworkTool_SetPreferredBridge, SetPreferredBridge),
    OPENSC4_SLOT(addr::cSC4NetworkTool_vftable, slot_index::kDoNetworkSpecificPlacement,
                 addr::cSC4NetworkTool_DoNetworkSpecificPlacement, DoNetworkSpecificPlacement),
    OPENSC4_SLOT(addr::cSC4NetworkTool_UndergroundVftable, slot_index::kDoNetworkSpecificPlacement,
                 addr::cSC4NetworkTool_DoNetworkSpecificPlacement, DoNetworkSpecificPlacement),
    OPENSC4_SLOT(addr::cSC4NetworkTool_PipeVftable, slot_index::kDoNetworkSpecificPlacement,
                 addr::cSC4NetworkTool_DoNetworkSpecificPlacement, DoNetworkSpecificPlacement),
    OPENSC4_SLOT(addr::cSC4NetworkTool_SubwayVftable, slot_index::kDoNetworkSpecificPlacement,
                 addr::cSC4NetworkTool_DoNetworkSpecificPlacement, DoNetworkSpecificPlacement),
    OPENSC4_SLOT(addr::cSC4NetworkTool_vftable, slot_index::kUpdateOnZoneChange,
                 addr::cSC4NetworkTool_UpdateOnZoneChange, UpdateOnZoneChange),
    OPENSC4_SLOT(addr::cSC4NetworkTool_UndergroundVftable, slot_index::kUpdateOnZoneChange,
                 addr::cSC4NetworkTool_UpdateOnZoneChange, UpdateOnZoneChange),
    OPENSC4_SLOT(addr::cSC4NetworkTool_PipeVftable, slot_index::kUpdateOnZoneChange,
                 addr::cSC4NetworkTool_UpdateOnZoneChange, UpdateOnZoneChange),
    OPENSC4_SLOT(addr::cSC4NetworkTool_SubwayVftable, slot_index::kUpdateOnZoneChange,
                 addr::cSC4NetworkTool_UpdateOnZoneChange, UpdateOnZoneChange),
};

#undef OPENSC4_DETOUR
#undef OPENSC4_SLOT

bool InstallState(registry::Mode) { return InstallDetours(kState, "state"); }
bool InstallQueries(registry::Mode) { return InstallDetours(kQueries, "queries"); }
bool InstallSlots(registry::Mode) { return SwapSlots(kSlots, "slots"); }

const bool g_registered[] = {
    registry::Register({"simulators.nettool.state", "simulators/nettool", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkTool_QueryInterface,
                        "Our network tool state: QueryInterface, Release, ForceReload, the failure reason's clear, "
                        "state and two texts, EnablePreviewUpdate, GetDraggedCells, SetValidStepCount, "
                        "IsRegularStep, GetLastValidPoint, GetPossibleBridges, the bridge altitude override, the "
                        "placement parameters' copy and getter, GetNetworkHeightForTerrainHeight (18 detours; "
                        "src/core/nettool_state.cpp)",
                        false, InstallState}),
    registry::Register({"simulators.nettool.queries", "simulators/nettool", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkTool_ViewInputControlId,
                        "Our network tool queries: the type row's view, cursor and pen IDs, the bridge name and "
                        "bitmap key, the four major bridge getters, GetPossibleNeighborConnection, OccupantExists, "
                        "the network occupant of a cell, the direction tables' two helpers, the edge byte helpers, "
                        "the word copy and the first network type of a set of flags (21 detours; "
                        "src/core/nettool_state.cpp)",
                        false, InstallQueries}),
    registry::Register({"simulators.nettool.slots", "simulators/nettool", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4NetworkTool_vftable,
                        "Our network tool code that is folded with other classes or too short for a detour, by "
                        "vtable slot in the tool family's vtables: GetPreferredBridge, SetPreferredBridge, "
                        "DoNetworkSpecificPlacement and UpdateOnZoneChange (4 functions in 18 slots; "
                        "src/core/nettool_state.cpp)",
                        false, InstallSlots}),
};

} // namespace
} // namespace opensc4::nettool_sim
