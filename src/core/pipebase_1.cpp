// The intermediate class of the underground pieces, first half (build 641;
// the class map, the vtables and the fields are in src/core/pipebase_sim.h,
// the class's life, Read and the walls in src/core/pipebase_2.cpp): the
// small slots, the geometry state (the sides, the floor depth, the position
// and the boxes, the refresh) and Write, each a detour at the game's
// address, because the pipe's and the subway's vtables hold the same
// functions and their code calls QueryInterface, Write and the statics
// directly.
//
// Every function was written from the disassembly. The detours are plain
// functions, so they clobber ECX and EDX. The game's functions that touch
// neither are GetClassId 0x00648460 (virtual calls only) and, on its early
// return, 0x0065A640 (after a virtual call of its own); GetTint 0x0065B020
// leaves EDX alone. `self` is the object's start (the interface part) unless
// a function says `part` (Write gets the cIGZSerializable part, the object +
// 8; GetPosition, GetBoundingBox and PostOccupantMessage the occupant part,
// the object + 4) or `vbase` (the piece key gets the object + 0x274).
//
// Left as the game's, with the reason (docs/pipebase.md has the evidence):
// the draw 0x0065B290 and the render states 0x0065AE80 (drawing);
// GetModelInstances 0x00659D70 (slot 33, which the network occupant's work
// left to the renderer too) and GetTextures 0x0065A880 (slot 104: the
// texture bindings for the draw, its only caller); the corner vector writer
// 0x0065B070 (the vector in EBX and the stream in ESI: written out in
// Write here); the stub 0x00657730 (nine vtables share it); the jumps
// 0x0065A860 and 0x0065A870; GetVertexHeights 0x0065A6C0 (the network
// occupant's, ours there); slot 85 0x0065B700 (the second half's notes).
//
// Calls, each count from the asm's pushes for that call and the callee's own
// `ret N`:
//   - the stream (out vtable 0x00AA2310): 0x14 SetUint8, 0x1C SetUint16,
//     0x20 SetSint32, 0x24 SetUint32, 0x30 SetFloat32 one argument each (the
//     value; a float goes as its 32 bits), 0x48 GetError none;
//   - the object's own slots: 0x4 AddRef and 0x8 Release none, 0x24
//     HasAnyNetworkFlag one (0x00604D30), 0x28 and 0x2C the flag setters one
//     (0x0065A900, 0x0065A940), 0x30 GetNetworkFlags none (0x006E3000), 0x60
//     GetRotation and 0x64 GetFlip none (0x00604DC0, 0x00604DD0), 0xBC
//     ConnectsToNeighbor two (0x00619320, ret 8), 0x140 the box two
//     (0x00659E00, ret 8), 0x148 none (0x00648460), 0x154 none (0x0065B700),
//     0x15C three (0x0065D810, ret 0xC), 0x164 and 0x168 one (0x0065E260,
//     0x0065E410, ret 4), 0x188 three (0x0065A120, ret 0xC), 0x190 one
//     (0x0065AF10, ret 4), 0x198 none (the derived classes');
//   - the occupant part's slots: 0x1C GetType none, 0x20 GetPosition one
//     (0x0065A720, ret 4), 0x80 IsFlagSet one, 0x84 GetFlags none;
//   - the network occupant: SetNetworkFlag 0x00619EC0 and ClearNetworkFlag
//     0x00619F70 one (ret 4), Write 0x0061A190 one (ret 4), the statics
//     0x0061C690 (cdecl, none); the occupant base: QueryInterface 0x005E89C0
//     two (ret 8, on the object + 4), PostOccupantMessage 0x005E9010 two
//     (ret 8); the model part's QueryInterface 0x00801940 two (ret 8, on the
//     object + 0x68);
//   - this class: the floor depth 0x0065AB50 none, 0x0065AE80 (cdecl, none);
//   - the searches of a cell's occupants, both cdecl with four arguments
//     (`add esp, 0x10` after each): 0x006584D0 (x, z, out, interface ID) and
//     0x006585E0 (x, z, type, out);
//   - the city (0x00B43CEC): 0x18C CellIsInBounds two (0x004594D0); the view
//     (0x00B43DD0): 0x7C two (the code after the call at 0x0065AF92 reads
//     its own argument at [esp+8], so the callee took both off);
//   - the message (0x00ACEC00): 0x14, 0x30, 0x40, 0x54, 0x68, 0x84 one, 0x4
//     and 0x8 none; the message server: 0x10 two; the allocator 0x009133DA
//     (cdecl, one) and the message's constructor 0x009134D6 (none);
//   - the stream helpers 0x005DA6E0 (sixteen floats; cdecl: stream, pointer)
//     and 0x005D9FD0 (a bool; cdecl: stream, value).
#include "core/pipebase_sim.h"

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
#include <initializer_list>

namespace opensc4::pipebase_sim {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("pipebase: first %s", name);
}

namespace {

template <typename R, typename... Args>
R CallThis(std::uintptr_t address, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(address)(self, args...);
}

template <typename R, typename... Args>
R CallCdecl(std::uintptr_t address, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(address)(args...);
}

float AsFloat(uint32_t bits)
{
    float value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

uint32_t AsBits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

// The result of the stream's GetError, as the game turns it into the
// function's answer (neg al, sbb eax, eax, inc eax): 1 for no error.
uint32_t NoError(void* stream)
{
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// A new framework message (0x2C bytes), or null when the allocator has none.
void* NewMessage()
{
    void* const memory = CallCdecl<void*, std::size_t>(addr::RZ_operator_new, 0x2C);
    return memory ? CallThis<void*>(addr::cGZMessage_cGZMessage, memory) : nullptr;
}

// One of the three depth statics (0x00B0CFD0; slot 97 sets them).
template <typename T>
T& Static(std::size_t offset)
{
    return Global<T>(addr::SC4PipeBase_DepthStatics + offset);
}

// The search of the cell over one edge of `piece`'s cell for an occupant
// that answers the interface `iid` (0x006584D0: the cell's occupants are
// walked and each is asked; the last one that answers is stored,
// referenced, and the answer is true). The steps are the table at
// 0x00B0CFE8; the edge is not checked, as in the game.
uint8_t FindOverEdge(char* piece, uint32_t edge, void** found, uint32_t iid)
{
    const std::uintptr_t step = addr::SC4PipeBase_EdgeSteps + edge * kEdgeStepSize;
    const uint32_t x = Global<uint32_t>(step) + At<uint32_t>(piece, field::kCellX);
    const uint32_t z = Global<uint32_t>(step + 4) + At<uint32_t>(piece, field::kCellZ);
    return CallCdecl<uint8_t, uint32_t, uint32_t, void**, uint32_t>(addr::SC4PipeBase_FindByInterfaceAt, x, z, found,
                                                                    iid);
}

// The search of `piece`'s own cell for a piece of the given type
// (0x006585E0), as slot 89 does it.
uint8_t FindInCell(char* piece, uint32_t type, void** found)
{
    return CallCdecl<uint8_t, uint32_t, uint32_t, uint32_t, void**>(addr::SC4PipeBase_FindPieceAt,
                                                                    At<uint32_t>(piece, field::kCellX),
                                                                    At<uint32_t>(piece, field::kCellZ), type, found);
}

// ====================================================================================
// simulators.pipebase.slots: the small slots
// ====================================================================================

// Slot 0 (0x0065A080, ret 8; the pipe's and the subway's QueryInterface jump
// here with every ID they do not answer themselves, and the thunks of this
// class's own parts): the class's IID, the network occupant's and
// 0xA9764C8C give the object itself, referenced through slot 1 (AL = 1, the
// rest of EAX from AddRef). Every other ID goes to the occupant base's
// QueryInterface on the object + 4 (AL = 1 when it answers, the rest of EAX
// its own) and, when that says no, to the model part's on the object + 0x68,
// whose answer is the result.
uint32_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    static bool logged = false;
    FirstCall("QueryInterface", logged);
    if (iid == kIidPipeBase || iid == kIidNetworkOccupant || iid == kIidObject) {
        *object = self;
        const uint32_t count = Virtual<uint32_t>(self, slot::kAddRef);
        return (count & 0xFFFFFF00u) | 1u;
    }
    const uint32_t base = CallThis<uint32_t, uint32_t, void**>(addr::cSC4Occupant_QueryInterface,
                                                               self + field::kOccupant, iid, object);
    if (static_cast<uint8_t>(base))
        return (base & 0xFFFFFF00u) | 1u;
    return CallThis<uint32_t, uint32_t, void**>(addr::SC4Model_QueryInterface, self + field::kModelBase, iid, object);
}

// Slot 82 (0x00648460): the word at +0x264, the derived class's ID. The
// game's function touches only EAX.
uint32_t __fastcall GetClassId(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetClassId", logged);
    return At<uint32_t>(self, field::kClassId);
}

// Slot 21 (0x00648470): 1 when bit 0 of the occupant part's flags (slot
// 0x84) is set and the object at +0x244 or the descriptor at +0x26C is
// there, else 0 (all of EAX).
uint32_t __fastcall IsPlaced(char* self, void*)
{
    static bool logged = false;
    FirstCall("IsPlaced", logged);
    if (!(Virtual<uint32_t>(self + field::kOccupant, occupant::kGetFlags) & 1u))
        return 0;
    if (At<void*>(self, field::kModel) || At<void*>(self, field::kDescriptor))
        return 1;
    return 0;
}

// The virtual base's slot 17 (0x0065A0E0 behind the thunk 0x0065C2F0, which
// takes the base's displacement word off the pointer; `vbase` is the object
// + 0x274; one argument, the out word): when the piece has any of the bits
// 0x06000000 in its network flags (slot 9) the word at +0xFC, else the
// class's IID. The game's EAX is slot 9's answer in the first case and the
// out pointer in the second. No object's virtual base has this class's
// vtable once a derived constructor ran, so the game never gets here.
uint32_t __fastcall GetPieceKey(char* vbase, void*, uint32_t* out)
{
    static bool logged = false;
    FirstCall("GetPieceKey", logged);
    char* const self = vbase - field::kVbase;
    const uint32_t any = Virtual<uint32_t>(self, slot::kHasAnyNetworkFlag, flag::kKeyed);
    if (static_cast<uint8_t>(any)) {
        *out = At<uint32_t>(self, field::kPieceKey);
        return any;
    }
    *out = kIidPipeBase;
    return static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(out));
}

// The two flag setters (slots 10 and 11) share this: the network flags'
// bits 0x30000 (slot 12) before and after the network occupant's own setter
// at `setter`; when they differ the piece is marked dirty (+0x260). EAX is
// the difference.
uint32_t ChangeNetworkFlag(char* self, std::uintptr_t setter, uint32_t flags)
{
    const uint32_t before = Virtual<uint32_t>(self, slot::kGetNetworkFlags) & flag::kTextures;
    CallThis<void, uint32_t>(setter, self, flags);
    const uint32_t changed = (Virtual<uint32_t>(self, slot::kGetNetworkFlags) & flag::kTextures) ^ before;
    if (changed != 0)
        At<uint8_t>(self, field::kDirty) = 1;
    return changed;
}

// Slot 10 (0x0065A900; one argument).
uint32_t __fastcall SetNetworkFlag(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("SetNetworkFlag", logged);
    return ChangeNetworkFlag(self, addr::cSC4NetworkOccupant_SetNetworkFlag, flags);
}

// Slot 11 (0x0065A940; one argument).
uint32_t __fastcall ClearNetworkFlag(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("ClearNetworkFlag", logged);
    return ChangeNetworkFlag(self, addr::cSC4NetworkOccupant_ClearNetworkFlag, flags);
}

// Slot 92 (0x0065A6F0; three arguments, a byte pointer each): the three
// bytes at +0x248, +0x249 and +0x24A. The game leaves the last byte in AL
// and the rest of EAX as it was; no value is returned here.
void __fastcall GetSides(char* self, void*, uint8_t* first, uint8_t* second, uint8_t* third)
{
    static bool logged = false;
    FirstCall("GetSides", logged);
    *first = At<uint8_t>(self, field::kSides);
    *second = At<uint8_t>(self, field::kSides + 1);
    *third = At<uint8_t>(self, field::kSides + 2);
}

// Slot 94 (0x00659DD0; one argument): the four heights of the ground over
// the corners (+0x24C; the floats' bits copied). EAX is the out pointer.
uint32_t* __fastcall GetSurfaceHeights(char* self, void*, uint32_t* heights)
{
    static bool logged = false;
    FirstCall("GetSurfaceHeights", logged);
    for (std::size_t i = 0; i < 4; ++i)
        heights[i] = At<uint32_t>(self, field::kSurface + 4 * i);
    return heights;
}

// Slot 96 (0x0065A810; four arguments): the piece key, the word at +0x268,
// the four corners' heights (the floor) and the floor depth, the floats'
// bits copied. EAX is the floor depth's bits.
uint32_t __fastcall GetShape(char* self, void*, uint32_t* pieceKey, uint32_t* word, uint32_t* heights,
                             uint32_t* depth)
{
    static bool logged = false;
    FirstCall("GetShape", logged);
    *pieceKey = At<uint32_t>(self, field::kPieceKey);
    *word = At<uint32_t>(self, field::kWord268);
    for (std::size_t i = 0; i < 4; ++i)
        heights[i] = At<uint32_t>(self, field::kCornerY + i * kCornerSize);
    const uint32_t bits = At<uint32_t>(self, field::kFloorDepth);
    *depth = bits;
    return bits;
}

// Slot 97 (0x00659DB0; three arguments; `this` is not read): the three
// depth statics stored: the depth (a float's bits), the from-the-ground
// byte and the rise (a float's bits). EAX is the first argument.
uint32_t __fastcall SetDepthStatics(void*, void*, uint32_t depth, uint32_t fromGround, uint32_t rise)
{
    static bool logged = false;
    FirstCall("SetDepthStatics", logged);
    Static<uint32_t>(statics::kDepth) = depth;
    Static<uint8_t>(statics::kFromGround) = static_cast<uint8_t>(fromGround);
    Static<uint32_t>(statics::kRise) = rise;
    return depth;
}

// Slot 103 (0x0065B020; one argument; `this` is not read): the tint, three
// floats of 1.0 (the subway's; the pipe has its own slot). EAX is the out
// pointer, which the draw hands on.
uint32_t* __fastcall GetTint(void*, void*, uint32_t* out)
{
    static bool logged = false;
    FirstCall("GetTint", logged);
    out[0] = 0x3F800000u;
    out[1] = 0x3F800000u;
    out[2] = 0x3F800000u;
    return out;
}

// Slot 93 (0x0065AA70; three arguments: a float out, a byte out, the edge).
// The float is the floor depth first. A slope (the keys 0x200, 0x800, 0xA00,
// 0x1400 and 0x14E00) sets the byte to 1 and that is all; a flat piece
// (0x300, 0x4B00) gets the mean of the two corners' heights at the ends of
// the edge plus the rise static (as the FPU makes it, so in double), and the
// byte 0; any other key the byte 0. The corners of the edges are a table the
// game builds on its stack: (0, 1), (0, 3), (3, 2), (1, 2); it does not
// check the edge, and reads its own stack with one above 3, where ours stays
// inside the table. The game's EAX is not a result.
void __fastcall GetEdgeDepth(char* self, void*, uint32_t* depth, uint8_t* sloped, uint32_t edge)
{
    static bool logged = false;
    FirstCall("GetEdgeDepth", logged);
    static const uint32_t kEdgeCorners[8] = {0, 1, 0, 3, 3, 2, 1, 2};
    *depth = At<uint32_t>(self, field::kFloorDepth);
    switch (At<uint32_t>(self, field::kPieceKey)) {
    case key::kSlope200:
    case key::kSlope800:
    case key::kSlopeA00:
    case key::kSlope1400:
    case key::k14E00:
        *sloped = 1;
        return;
    case key::kFlat300:
    case key::kFlat: {
        const uint32_t second = kEdgeCorners[(2 * edge + 1) & 7];
        const uint32_t first = kEdgeCorners[(2 * edge) & 7];
        const double sum = static_cast<double>(At<float>(self, field::kCornerY + second * kCornerSize)) +
                           static_cast<double>(At<float>(self, field::kCornerY + first * kCornerSize));
        const float value = static_cast<float>(sum * static_cast<double>(Global<float>(addr::SC4_FloatHalf)) +
                                               static_cast<double>(Static<float>(statics::kRise)));
        *depth = AsBits(value);
        *sloped = 0;
        return;
    }
    default:
        *sloped = 0;
        return;
    }
}

// 0x0065A640 (one argument, the edge; slot 89 calls it): false for a piece
// without bit 31 of its network flags (slot 12). Else the cell over the
// edge is searched for an occupant that answers this piece's class ID as an
// interface; when there is one the answer is whether that one lacks the
// bit, and it is released. Only AL is set by the game.
uint8_t __fastcall NeighborLacksTop(char* self, void*, uint32_t edge)
{
    static bool logged = false;
    FirstCall("NeighborLacksTop", logged);
    if (!(Virtual<uint32_t>(self, slot::kGetNetworkFlags) & flag::kTop))
        return 0;
    void* found = nullptr;
    uint8_t answer = 0;
    if (FindOverEdge(self, edge, &found, At<uint32_t>(self, field::kClassId))) {
        answer = (Virtual<uint32_t>(found, slot::kGetNetworkFlags) & flag::kTop) ? 0 : 1;
        Virtual<uint32_t>(found, slot::kRelease);
    }
    return answer;
}

// Slot 98 (0x0065A120; three arguments: a piece, a direction, how many more
// to follow): how many flat pieces lie in a row from `other` in that
// direction, itself counted. 0 when `other` is not the flat piece 0x4B00, or
// when its cell holds a piece of this one's type (slot 82; 0x006585E0), which
// is released. Else 1, and when more are to be followed and the cell over
// that edge of `other` holds an occupant that answers this piece's class ID
// (only the pointer is looked at, not the search's answer), 1 plus this
// slot's answer for that one with one less to follow; it is released after.
uint32_t __fastcall CountRun(char* self, void*, char* other, uint32_t direction, uint32_t remaining)
{
    static bool logged = false;
    FirstCall("CountRun", logged);
    if (At<uint32_t>(other, field::kPieceKey) != key::kFlat)
        return 0;
    void* here = nullptr;
    const uint32_t type = Virtual<uint32_t>(self, slot::kGetClassId);
    if (FindInCell(other, type, &here)) {
        Virtual<uint32_t>(here, slot::kRelease);
        return 0;
    }
    if (remaining == 0)
        return 1;
    void* next = nullptr;
    FindOverEdge(other, direction, &next, At<uint32_t>(self, field::kClassId));
    if (!next)
        return 1;
    const uint32_t count = Virtual<uint32_t>(self, slot::kCountRun, next, direction, remaining - 1) + 1;
    Virtual<uint32_t>(next, slot::kRelease);
    return count;
}

// Slot 99 (0x0065A200; one argument, a byte out). 0 unless the piece is a
// subway's (the word at +0x264) flat piece 0x4B00 whose cell holds no piece
// of its type (as in slot 98). Then the two edges across the piece's
// direction: (rotation + 3) mod 4 and (rotation + 1) mod 4, each turned by
// two more when the piece is flipped and the rotation is 1 or 3. The cell
// over each is searched for an occupant that answers the class ID, both
// before either is used; the run from each (slot 98 with 4 more to follow)
// is counted, 0 without one, and each is released. Both runs 2 or more: 2.
// The first exactly 1 and the second 3 or more: the byte 1, the answer 1.
// The second exactly 1 and the first 3 or more: the byte 0, the answer 1.
// Else 0, the byte untouched. The compares are signed.
uint32_t __fastcall GetSideRuns(char* self, void*, uint8_t* side)
{
    static bool logged = false;
    FirstCall("GetSideRuns", logged);
    if (At<uint32_t>(self, field::kClassId) != kSubwayClassId || At<uint32_t>(self, field::kPieceKey) != key::kFlat)
        return 0;
    void* here = nullptr;
    const uint32_t type = Virtual<uint32_t>(self, slot::kGetClassId);
    if (FindInCell(self, type, &here)) {
        Virtual<uint32_t>(here, slot::kRelease);
        return 0;
    }
    const uint8_t rotation = Virtual<uint8_t>(self, slot::kGetRotation);
    const uint8_t flip = Virtual<uint8_t>(self, slot::kGetFlip);
    const bool turned = flip != 0 && (rotation == 1 || rotation == 3);
    uint32_t firstEdge = (uint32_t{rotation} + 3) & 3;
    if (turned)
        firstEdge = (firstEdge + 2) & 3;
    uint32_t secondEdge = (uint32_t{rotation} + 1) & 3;
    if (turned)
        secondEdge = (secondEdge + 2) & 3;
    void* first = nullptr;
    void* second = nullptr;
    FindOverEdge(self, firstEdge, &first, At<uint32_t>(self, field::kClassId));
    FindOverEdge(self, secondEdge, &second, At<uint32_t>(self, field::kClassId));
    int32_t firstRun = 0, secondRun = 0;
    if (first) {
        firstRun = Virtual<int32_t>(self, slot::kCountRun, first, firstEdge, 4u);
        Virtual<uint32_t>(first, slot::kRelease);
    }
    if (second) {
        secondRun = Virtual<int32_t>(self, slot::kCountRun, second, secondEdge, 4u);
        Virtual<uint32_t>(second, slot::kRelease);
    }
    if (firstRun >= 2 && secondRun >= 2)
        return 2;
    if (firstRun == 1 && secondRun >= 3) {
        *side = 1;
        return 1;
    }
    if (secondRun == 1 && firstRun >= 3) {
        *side = 0;
        return 1;
    }
    return 0;
}

// The occupant part's slot 15 (0x0065A980; `part` is the object + 4; two
// arguments): the occupant base's post, then for the occupant manager's
// messages 0x99EF1142 and 0x99EF1143 a second message, 0x8B87A974 or
// 0x8B87A975, with the occupant (as void 1 and as the object; null for a
// null object), its flag 4, its type and the data, queued on the occupants'
// cached message server. No return value in the game: EAX is what the last
// call left.
uint32_t __fastcall PostOccupantMessage(char* part, void*, uint32_t id, uint32_t data)
{
    static bool logged = false;
    FirstCall("PostOccupantMessage", logged);
    const uint32_t result = CallThis<uint32_t, uint32_t, uint32_t>(addr::cSC4Occupant_PostOccupantMessage, part, id,
                                                                   data);
    if (id != kMessageInserted && id != kMessageRemoved)
        return result;
    void*& server = Global<void*>(addr::cSC4Occupant_MessageServer);
    if (!server)
        server = Global<void*>(addr::SC4_MessageServer);
    void* const message = NewMessage();
    if (message)
        Virtual<uint32_t>(message, 0x4);
    Virtual<void>(message, 0x14, id != kMessageInserted ? kMessagePieceRemoved : kMessagePieceInserted);
    char* const self = part - field::kOccupant;
    void* const occupant = self ? part : nullptr;
    Virtual<void>(message, 0x30, occupant);
    Virtual<void*>(message, 0x84, occupant);
    const uint8_t flagged = Virtual<uint8_t>(part, occupant::kIsFlagSet, 4u);
    Virtual<void>(message, 0x40, uint32_t{flagged});
    const uint32_t type = Virtual<uint32_t>(part, occupant::kGetType);
    Virtual<void>(message, 0x54, type);
    Virtual<void>(message, 0x68, data);
    Virtual<uint8_t>(Global<void*>(addr::cSC4Occupant_MessageServer), 0x10, message, 0u);
    return Virtual<uint32_t>(message, 0x8);
}

// 0x0065B040 (cdecl, none; the pipe's and the subway's statics call it when
// the network occupant's are not done): once, marked by the byte at
// 0x00B46870: the render states (0x0065AE80, the game's), then the network
// occupant's statics when those are not done (a tail jump: their answer),
// else 1. Already done: 0. Only AL is set.
uint8_t __cdecl InitStatics()
{
    static bool logged = false;
    FirstCall("InitStatics", logged);
    if (Global<uint8_t>(addr::SC4PipeBase_StaticsDone))
        return 0;
    Global<uint8_t>(addr::SC4PipeBase_StaticsDone) = 1;
    CallCdecl<void>(addr::SC4PipeBase_InitModels);
    if (!Global<uint8_t>(addr::cSC4NetworkOccupant_StaticsDone))
        return CallCdecl<uint8_t>(addr::cSC4NetworkOccupant_InitStatics);
    return 1;
}

// ====================================================================================
// simulators.pipebase.state: the sides, the floor depth, the position and
// the boxes, the setters, the refresh and Write
// ====================================================================================

// Slot 88 (0x0065A3F0; no arguments): the three bytes at +0x248 from the
// piece key, the rotation and the flip, then the network flags after them.
//   - key 0x200: by the rotation and flip byte (+0x104): 0 or 0x83 gives
//     (0, 1, 0), 1 or 0x82 (1, 1, 0), 2 or 0x81 (0, 0, 1), 3 or 0x80
//     (1, 0, 1); any other value leaves the bytes alone;
//   - keys 0x800, 0xA00, 0xC00, 0x1400 and 0x1E00: the first byte from the
//     table at 0x00AAAB9C by the rotation (slot 24; 1, 0, 1, 0), the other
//     two from the word in the table at 0x00AAABA0 (1, 0, 0, 1): 0 gives
//     (1, 0), 1 gives (0, 1), another value leaves them; then, flipped (slot
//     25), all three bytes are turned over (zero becomes 1, the rest 0);
//   - any other key: (0, 1, 1).
// The flags, each through the slots 10 and 11: 0x01000000 set when the
// first byte is not zero, 0x02000000 set when the second is zero,
// 0x04000000 set when the third is zero, cleared otherwise. The bytes are
// read again from the object before each. The tables are read as the game
// reads them, the rotation not checked.
void __fastcall UpdateSides(char* self, void*)
{
    static bool logged = false;
    FirstCall("UpdateSides", logged);
    const auto set = [self](uint8_t first, uint8_t second, uint8_t third) {
        At<uint8_t>(self, field::kSides) = first;
        At<uint8_t>(self, field::kSides + 1) = second;
        At<uint8_t>(self, field::kSides + 2) = third;
    };
    switch (At<uint32_t>(self, field::kPieceKey)) {
    case key::kSlope200:
        switch (At<uint8_t>(self, field::kRotationFlip)) {
        case 0x00:
        case 0x83:
            set(0, 1, 0);
            break;
        case 0x01:
        case 0x82:
            set(1, 1, 0);
            break;
        case 0x02:
        case 0x81:
            set(0, 0, 1);
            break;
        case 0x03:
        case 0x80:
            set(1, 0, 1);
            break;
        default:
            break;
        }
        break;
    case key::kSlope800:
    case key::kSlopeA00:
    case key::kC00:
    case key::kSlope1400:
    case key::k1E00: {
        const uint32_t rotation = Virtual<uint8_t>(self, slot::kGetRotation);
        const uint8_t flip = Virtual<uint8_t>(self, slot::kGetFlip);
        At<uint8_t>(self, field::kSides) = Global<uint8_t>(addr::SC4PipeBase_SideByRotation + rotation);
        const uint32_t end = Global<uint32_t>(addr::SC4PipeBase_EndByRotation + 4 * rotation);
        if (end == 0) {
            At<uint8_t>(self, field::kSides + 1) = 1;
            At<uint8_t>(self, field::kSides + 2) = 0;
        } else if (end == 1) {
            At<uint8_t>(self, field::kSides + 1) = 0;
            At<uint8_t>(self, field::kSides + 2) = 1;
        }
        if (flip != 0) {
            const uint8_t first = At<uint8_t>(self, field::kSides) == 0;
            const uint8_t second = At<uint8_t>(self, field::kSides + 1) == 0;
            At<uint8_t>(self, field::kSides + 1) = second;
            const uint8_t third = At<uint8_t>(self, field::kSides + 2) == 0;
            At<uint8_t>(self, field::kSides) = first;
            At<uint8_t>(self, field::kSides + 2) = third;
        }
        break;
    }
    default:
        set(0, 1, 1);
        break;
    }
    Virtual<void>(self, At<uint8_t>(self, field::kSides) != 0 ? slot::kSetNetworkFlag : slot::kClearNetworkFlag,
                  flag::kSide248);
    Virtual<void>(self, At<uint8_t>(self, field::kSides + 1) != 0 ? slot::kClearNetworkFlag : slot::kSetNetworkFlag,
                  flag::kSide249);
    Virtual<void>(self, At<uint8_t>(self, field::kSides + 2) != 0 ? slot::kClearNetworkFlag : slot::kSetNetworkFlag,
                  flag::kSide24A);
}

// The occupant part's slot 8 (0x0065A720; `part` is the object + 4; one
// argument, three floats out): x and z the mean of the four corners', y the
// lowest of the four heights of the ground over them (+0x24C). The game
// zeroes x and z and sets y to the largest float, stores the first corner's
// x and z, takes the first height when it is below the float at 0x00AAABB0
// (the largest float), then for each further corner adds its x and z (each
// sum rounded to a float) and takes its height when lower; last x and z
// times the float at 0x00A8821C (a quarter). An unordered compare takes
// nothing. EAX is not a result.
void __fastcall GetPosition(char* part, void*, uint32_t* out)
{
    static bool logged = false;
    FirstCall("GetPosition", logged);
    char* const self = part - field::kOccupant;
    out[2] = 0;
    out[0] = 0;
    out[1] = 0x7F7FFFFFu;
    out[0] = At<uint32_t>(self, field::kCorners);
    out[2] = At<uint32_t>(self, field::kCornerZ);
    if (At<float>(self, field::kSurface) < Global<float>(addr::SC4PipeBase_FloatMax))
        out[1] = At<uint32_t>(self, field::kSurface);
    for (std::size_t i = 1; i < 4; ++i) {
        out[0] = AsBits(static_cast<float>(static_cast<double>(At<float>(self, field::kCorners + i * kCornerSize)) +
                                           static_cast<double>(AsFloat(out[0]))));
        out[2] = AsBits(static_cast<float>(static_cast<double>(At<float>(self, field::kCornerZ + i * kCornerSize)) +
                                           static_cast<double>(AsFloat(out[2]))));
        if (At<float>(self, field::kSurface + 4 * i) < AsFloat(out[1]))
            out[1] = At<uint32_t>(self, field::kSurface + 4 * i);
    }
    const double scale = static_cast<double>(Global<float>(addr::cSC4NetworkOccupant_PositionScale));
    out[0] = AsBits(static_cast<float>(static_cast<double>(AsFloat(out[0])) * scale));
    out[2] = AsBits(static_cast<float>(static_cast<double>(AsFloat(out[2])) * scale));
}

// The box of the two box functions: the low and the high point are the
// position twice (the occupant part's slot 8, called once for each), then
// widened by each corner in turn: the low point by the corner's x, y and z
// where they are lower, the high point by the corner's x, the height of the
// ground over that corner (+0x24C) and the corner's z where they are higher
// (the words copied; an unordered compare changes nothing). So the box
// reaches from the floor to the ground.
void Box(char* self, uint32_t* low, uint32_t* high)
{
    char* const part = self + field::kOccupant;
    Virtual<void>(part, occupant::kGetPosition, low);
    Virtual<void>(part, occupant::kGetPosition, high);
    for (std::size_t corner = 0; corner < 4; ++corner) {
        char* const record = self + field::kCorners + corner * kCornerSize;
        for (std::size_t i = 0; i < 3; ++i)
            if (At<float>(record, 4 * i) < AsFloat(low[i]))
                low[i] = At<uint32_t>(record, 4 * i);
        if (At<float>(record, 0) > AsFloat(high[0]))
            high[0] = At<uint32_t>(record, 0);
        if (At<float>(self, field::kSurface + 4 * corner) > AsFloat(high[1]))
            high[1] = At<uint32_t>(self, field::kSurface + 4 * corner);
        if (At<float>(record, 8) > AsFloat(high[2]))
            high[2] = At<uint32_t>(record, 8);
    }
}

// The occupant part's slot 10 (0x00659B00; `part` is the object + 4; two
// arguments, the low and the high point out): the box worked out, not the
// one kept at +0x124. EAX is not a result.
void __fastcall GetBoundingBox(char* part, void*, uint32_t* low, uint32_t* high)
{
    static bool logged = false;
    FirstCall("GetBoundingBox", logged);
    Box(part - field::kOccupant, low, high);
}

// Slot 80 (0x00659E00; two arguments, the low and the high point out; the
// refresh, slot 95 and the network occupant's Init call it with +0x124 and
// +0x130): the same box.
void __fastcall ComputeBoundingBox(char* self, void*, uint32_t* low, uint32_t* high)
{
    static bool logged = false;
    FirstCall("ComputeBoundingBox", logged);
    Box(self, low, high);
}

// 0x0065AB50 (no arguments; slots 30, 84, 86 and 95 call it): the floor
// depth at +0x25C. The position is asked first (the occupant part's slot 8;
// its y is the lowest height of the ground over the corners).
//   - a slope (the keys 0x200, 0x800, 0xA00, 0x1400): three corners by the
//     rotation and flip byte (+0x104): the low one and the two it is
//     measured against: 0 or 0x81 gives 2 against 1 and 3; 1 or 0x80, 1
//     against 0 and 2; 2 or 0x83, 0 against 1 and 3; 3 or 0x82, 3 against 0
//     and 2. With the from-the-ground static set: the lower of the mean of
//     the two heights of the ground and the third, less the depth static;
//     else the lower of the mean of the two corners' own heights and the
//     third's, plus the rise static. The mean is compared as the FPU has it
//     (in double) and used as the float it was stored as; an unordered
//     compare takes the third.
//   - a flat piece (0x300, 0x4B00): with the static set the position's y
//     less the depth static; else the mean of the four corners' heights
//     plus the rise (the game stores the running sum in the field on the
//     way, as here).
//   - any other key: the rise plus the first corner's height.
// With another value in the rotation byte the game takes the three corner
// numbers from a stack slot that nothing has set yet; ours takes corner 0
// for all three. EAX is not a result.
void __fastcall ComputeFloorDepth(char* self, void*)
{
    static bool logged = false;
    FirstCall("ComputeFloorDepth", logged);
    uint32_t position[3];
    Virtual<void>(self + field::kOccupant, occupant::kGetPosition, static_cast<uint32_t*>(position));
    const double half = static_cast<double>(Global<float>(addr::SC4_FloatHalf));
    switch (At<uint32_t>(self, field::kPieceKey)) {
    case key::kSlope200:
    case key::kSlope800:
    case key::kSlopeA00:
    case key::kSlope1400: {
        // The low corner and the two it is measured against, by the byte.
        static const uint8_t kSlopeCorners[4][3] = {{2, 1, 3}, {1, 0, 2}, {0, 1, 3}, {3, 0, 2}};
        static const uint8_t kFlipped[4] = {1, 0, 3, 2};
        const uint8_t turn = At<uint8_t>(self, field::kRotationFlip);
        uint32_t low = 0, a = 0, b = 0;
        if (turn < 4 || (turn >= 0x80 && turn <= 0x83)) {
            const uint8_t* const corners = kSlopeCorners[turn < 4 ? turn : kFlipped[turn - 0x80]];
            low = corners[0];
            a = corners[1];
            b = corners[2];
        }
        if (Static<uint8_t>(statics::kFromGround) != 0) {
            const double mean = (static_cast<double>(At<float>(self, field::kSurface + 4 * b)) +
                                 static_cast<double>(At<float>(self, field::kSurface + 4 * a))) *
                                half;
            const float stored = static_cast<float>(mean);
            float chosen = At<float>(self, field::kSurface + 4 * low);
            if (mean < static_cast<double>(chosen))
                chosen = stored;
            At<float>(self, field::kFloorDepth) =
                static_cast<float>(static_cast<double>(chosen) - static_cast<double>(Static<float>(statics::kDepth)));
        } else {
            const double mean = (static_cast<double>(At<float>(self, field::kCornerY + b * kCornerSize)) +
                                 static_cast<double>(At<float>(self, field::kCornerY + a * kCornerSize))) *
                                half;
            const float stored = static_cast<float>(mean);
            float chosen = At<float>(self, field::kCornerY + low * kCornerSize);
            if (mean < static_cast<double>(chosen))
                chosen = stored;
            At<float>(self, field::kFloorDepth) =
                static_cast<float>(static_cast<double>(Static<float>(statics::kRise)) + static_cast<double>(chosen));
        }
        return;
    }
    case key::kFlat300:
    case key::kFlat: {
        if (Static<uint8_t>(statics::kFromGround) != 0) {
            At<float>(self, field::kFloorDepth) = static_cast<float>(
                static_cast<double>(AsFloat(position[1])) - static_cast<double>(Static<float>(statics::kDepth)));
            return;
        }
        At<uint32_t>(self, field::kFloorDepth) = 0;
        double sum = static_cast<double>(At<float>(self, field::kCornerY));
        At<float>(self, field::kFloorDepth) = static_cast<float>(sum);
        sum += static_cast<double>(At<float>(self, field::kCornerY + kCornerSize));
        At<float>(self, field::kFloorDepth) = static_cast<float>(sum);
        sum += static_cast<double>(At<float>(self, field::kCornerY + 2 * kCornerSize));
        At<float>(self, field::kFloorDepth) = static_cast<float>(sum);
        sum += static_cast<double>(At<float>(self, field::kCornerY + 3 * kCornerSize));
        At<float>(self, field::kFloorDepth) =
            static_cast<float>(sum * static_cast<double>(Global<float>(addr::cSC4NetworkOccupant_PositionScale)) +
                               static_cast<double>(Static<float>(statics::kRise)));
        return;
    }
    default:
        At<float>(self, field::kFloorDepth) = static_cast<float>(static_cast<double>(Static<float>(statics::kRise)) +
                                                                 static_cast<double>(At<float>(self, field::kCornerY)));
        return;
    }
}

// Slot 30 (0x0065ADB0; two arguments): the piece key and the rotation and
// flip byte stored, then the floor depth (at its address). EAX is not a
// result.
void __fastcall SetPiece(char* self, void*, uint32_t pieceKey, uint32_t rotationFlip)
{
    static bool logged = false;
    FirstCall("SetPiece", logged);
    At<uint32_t>(self, field::kPieceKey) = pieceKey;
    At<uint8_t>(self, field::kRotationFlip) = static_cast<uint8_t>(rotationFlip);
    CallThis<void>(addr::SC4PipeBase_ComputeFloorDepth, self);
}

// Slot 84 (0x0065AE30; one argument, four floats): the four corners'
// heights stored (the bits copied), the floor depth, then the side walls
// (slot 89) and the floor (slot 90), each with 1.
void __fastcall SetHeights(char* self, void*, const uint32_t* heights)
{
    static bool logged = false;
    FirstCall("SetHeights", logged);
    for (std::size_t i = 0; i < 4; ++i)
        At<uint32_t>(self, field::kCornerY + i * kCornerSize) = heights[i];
    CallThis<void>(addr::SC4PipeBase_ComputeFloorDepth, self);
    Virtual<void>(self, slot::kSetSideWalls, 1u);
    Virtual<void>(self, slot::kSetFloor, 1u);
}

// Slot 95 (0x0065B6B0; one argument, four floats): the four heights of the
// ground stored (the bits copied), the box worked out again into +0x124 and
// +0x130 (slot 80), then the floor depth.
void __fastcall SetSurfaceHeights(char* self, void*, const uint32_t* heights)
{
    static bool logged = false;
    FirstCall("SetSurfaceHeights", logged);
    for (std::size_t i = 0; i < 4; ++i)
        At<uint32_t>(self, field::kSurface + 4 * i) = heights[i];
    Virtual<void>(self, slot::kComputeBoundingBox, self + field::kBoxLow, self + field::kBoxHigh);
    CallThis<void>(addr::SC4PipeBase_ComputeFloorDepth, self);
}

// Slot 86 (0x0065ADD0; one argument, handed on): the rotation (slot 24) and
// the flip (slot 25) asked, the floor depth, the matrix (slot 85), then slot
// 87 with the rotation, the flip and the argument, the side walls (slot 89)
// and the floor (slot 90) with the argument. The flip goes as the low byte
// of a stack slot whose other three bytes still hold the object's address
// (the game's `push ecx`), as here.
void __fastcall Rebuild(char* self, void*, uint32_t refresh)
{
    static bool logged = false;
    FirstCall("Rebuild", logged);
    const uint8_t rotation = Virtual<uint8_t>(self, slot::kGetRotation);
    const uint32_t flip = (static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(self)) & 0xFFFFFF00u) |
                          Virtual<uint8_t>(self, slot::kGetFlip);
    CallThis<void>(addr::SC4PipeBase_ComputeFloorDepth, self);
    Virtual<void>(self, slot::kBuildMatrix);
    Virtual<void>(self, slot::kBuildModel, uint32_t{rotation}, flip, refresh);
    Virtual<void>(self, slot::kSetSideWalls, refresh);
    Virtual<void>(self, slot::kSetFloor, refresh);
}

// Slot 100 (0x0065AF10; one argument, a byte: tell the neighbours). The
// city is asked whether the piece's cell is inside it (slot 0x18C); when
// not, the dirty and the rebuild bytes (+0x260, +0x261) are cleared and
// that is all. A piece that is dirty already is left alone. Else both bytes
// are set, the box is worked out into +0x124 and +0x130 (slot 80), and when
// bit 1 of the occupant's flags byte (+0x33) is set the view (0x00B43DD0)
// is told through its slot 0x7C with the object's virtual base and 3. Then,
// with the argument set, for each edge 0 to 3 over which the piece connects
// to its neighbour for its network type (slot 47 with slot 102's answer):
// the cell over that edge is searched for an occupant that answers the
// class ID, and that one's refresh is called with 1 and it is released. EAX
// is not a result.
void __fastcall Refresh(char* self, void*, uint32_t neighbours)
{
    static bool logged = false;
    FirstCall("Refresh", logged);
    const uint8_t inside = Virtual<uint8_t>(Global<void*>(addr::SC4_City), 0x18C, At<uint32_t>(self, field::kCellX),
                                            At<uint32_t>(self, field::kCellZ));
    if (!inside) {
        At<uint8_t>(self, field::kDirty) = 0;
        At<uint8_t>(self, field::kRebuild) = 0;
        return;
    }
    if (At<uint8_t>(self, field::kDirty) != 0)
        return;
    At<uint8_t>(self, field::kDirty) = 1;
    At<uint8_t>(self, field::kRebuild) = 1;
    Virtual<void>(self, slot::kComputeBoundingBox, self + field::kBoxLow, self + field::kBoxHigh);
    if (At<uint8_t>(self, field::kOccupantFlags) & 2)
        Virtual<void>(Global<void*>(addr::SC4_View43DD0), 0x7C, VirtualBase(self), 3u);
    if (static_cast<uint8_t>(neighbours) == 0)
        return;
    for (uint32_t edge = 0; edge < 4; ++edge) {
        const uint32_t network = Virtual<uint32_t>(self, slot::kGetNetworkType);
        if (!Virtual<uint8_t>(self, slot::kConnectsToNeighbor, edge, network))
            continue;
        void* found = nullptr;
        if (!FindOverEdge(self, edge, &found, At<uint32_t>(self, field::kClassId)))
            continue;
        Virtual<void>(found, slot::kRefresh, 1u);
        Virtual<uint32_t>(found, slot::kRelease);
    }
}

// The writer of a corner vector (0x0065B070, the vector in EBX and the
// stream in ESI, so it is written out here; other classes' Writes call the
// game's): the count (the bytes between begin and end over 0x18, a signed
// division), then each record: x, y, z and the two last floats as their
// bits, then the bytes at +0xE, +0xD, +0xC and +0xF. The begin pointer is
// read again for every field.
void WriteCornerVector(void* stream, char* vector)
{
    const int32_t bytes = static_cast<int32_t>(At<std::uintptr_t>(vector, 0x4) - At<std::uintptr_t>(vector, 0x0));
    uint32_t count = static_cast<uint32_t>(bytes / static_cast<int32_t>(kCornerSize));
    Virtual<uint8_t>(stream, 0x24, count);
    for (std::size_t record = 0; count != 0; --count, record += kCornerSize) {
        for (const std::size_t offset : {std::size_t{0x0}, std::size_t{0x4}, std::size_t{0x8}, std::size_t{0x10},
                                         std::size_t{0x14}})
            Virtual<uint8_t>(stream, 0x30, At<uint32_t>(At<char*>(vector, 0x0), record + offset));
        for (const std::size_t offset : {std::size_t{0xE}, std::size_t{0xD}, std::size_t{0xC}, std::size_t{0xF}})
            Virtual<uint8_t>(stream, 0x14, uint32_t{At<uint8_t>(At<char*>(vector, 0x0), record + offset)});
    }
}

// Write (0x0065B140; `part` is the object + 8; the pipe's and the subway's
// Writes call it, and the property holder's slot 23 through the thunk
// 0x0065C310): version 3, then the network occupant's record; when that is
// good: the two first bytes of the model part's state (+0x50, +0x51) and
// its float (+0x60), the wall count, the six saved vectors (the four walls,
// the floor, +0x1C0), the matrix (0x005DA6E0), the cell as two signed
// words, the three bytes at +0x248 as bools (0x005D9FD0), the four heights
// of the ground, the floor depth, the words at +0x264 and +0x268. The
// answer is the stream's state. The layout is in
// tools/re/save_schemas/pipeocc.py.
uint32_t __fastcall Write(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    Virtual<uint8_t>(stream, 0x1C, 3u);
    if (CallThis<uint8_t, void*>(addr::cSC4NetworkOccupant_Write, part, stream)) {
        char* const self = part - field::kSerializable;
        Virtual<uint8_t>(stream, 0x14, uint32_t{At<uint8_t>(self, field::kModelState)});
        Virtual<uint8_t>(stream, 0x14, uint32_t{At<uint8_t>(self, field::kModelState + 1)});
        Virtual<uint8_t>(stream, 0x30, At<uint32_t>(self, field::kStateFloat));
        Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, field::kWallCount));
        for (std::size_t i = 0; i < 4; ++i)
            WriteCornerVector(stream, self + field::kWalls + i * kVectorSize);
        WriteCornerVector(stream, self + field::kFloor);
        WriteCornerVector(stream, self + field::kVector1C0);
        CallCdecl<void*, void*, char*>(addr::SC4Save_F5DA6E0, stream, self + field::kMatrix);
        Virtual<uint8_t>(stream, 0x20, At<uint32_t>(self, field::kCellX));
        Virtual<uint8_t>(stream, 0x20, At<uint32_t>(self, field::kCellZ));
        for (std::size_t i = 0; i < 3; ++i)
            CallCdecl<void*, void*, uint32_t>(addr::SC4Stream_WriteBool, stream,
                                              uint32_t{At<uint8_t>(self, field::kSides + i)});
        for (std::size_t i = 0; i < 4; ++i)
            Virtual<uint8_t>(stream, 0x30, At<uint32_t>(self, field::kSurface + 4 * i));
        Virtual<uint8_t>(stream, 0x30, At<uint32_t>(self, field::kFloorDepth));
        Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, field::kClassId));
        Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, field::kWord268));
    }
    return NoError(stream);
}

// ====================================================================================
// Installation
// ====================================================================================

struct Detour {
    std::uintptr_t address;
    void* ours;
};

#define OPENSC4_DETOUR(address, fn) {address, reinterpret_cast<void*>(&fn)}

// Hooks to leave out, for bisecting: OPENSC4_PIPEBASE_SKIP lists function
// addresses (hex, eight digits), as OPENSC4_PIPEOCC_SKIP does; the second
// half reads the same list.
bool Skipped(std::uintptr_t address)
{
    char list[1024] = {};
    if (!GetEnvironmentVariableA("OPENSC4_PIPEBASE_SKIP", list, sizeof list))
        return false;
    char wanted[16];
    std::snprintf(wanted, sizeof wanted, "%08X", static_cast<unsigned>(address));
    return std::strstr(list, wanted) != nullptr;
}

template <std::size_t N>
bool InstallDetours(const Detour (&detours)[N], const char* what)
{
    if (!game::IsBuild641())
        return false;
    for (const Detour& detour : detours)
        if (!addr_check::Find(detour.address)) {
            log::Write("pipebase %s: no address entry for 0x%08X", what, static_cast<unsigned>(detour.address));
            return false;
        }
    unsigned installed = 0;
    for (const Detour& detour : detours) {
        if (Skipped(detour.address))
            continue;
        void* original = nullptr;
        if (!hook::Install(*addr_check::Find(detour.address), detour.ours, &original))
            return false;
        ++installed;
    }
    log::Write("pipebase %s: %u detours", what, installed);
    return true;
}

const Detour kSlots[] = {
    OPENSC4_DETOUR(addr::SC4PipeBase_QueryInterface, QueryInterface),
    OPENSC4_DETOUR(addr::SC4PipeBase_GetClassId, GetClassId),
    OPENSC4_DETOUR(addr::SC4PipeBase_IsPlaced, IsPlaced),
    OPENSC4_DETOUR(addr::SC4PipeBase_GetPieceKey, GetPieceKey),
    OPENSC4_DETOUR(addr::SC4PipeBase_SetNetworkFlag, SetNetworkFlag),
    OPENSC4_DETOUR(addr::SC4PipeBase_ClearNetworkFlag, ClearNetworkFlag),
    OPENSC4_DETOUR(addr::SC4PipeBase_GetSides, GetSides),
    OPENSC4_DETOUR(addr::SC4PipeBase_GetSurfaceHeights, GetSurfaceHeights),
    OPENSC4_DETOUR(addr::SC4PipeBase_GetShape, GetShape),
    OPENSC4_DETOUR(addr::SC4PipeBase_SetDepthStatics, SetDepthStatics),
    OPENSC4_DETOUR(addr::SC4PipeBase_GetTint, GetTint),
    OPENSC4_DETOUR(addr::SC4PipeBase_GetEdgeDepth, GetEdgeDepth),
    OPENSC4_DETOUR(addr::SC4PipeBase_F65A640, NeighborLacksTop),
    OPENSC4_DETOUR(addr::SC4PipeBase_CountRun, CountRun),
    OPENSC4_DETOUR(addr::SC4PipeBase_GetSideRuns, GetSideRuns),
    OPENSC4_DETOUR(addr::SC4PipeBase_PostOccupantMessage, PostOccupantMessage),
    OPENSC4_DETOUR(addr::SC4PipeBase_InitStatics, InitStatics),
};

const Detour kState[] = {
    OPENSC4_DETOUR(addr::SC4PipeBase_UpdateSides, UpdateSides),
    OPENSC4_DETOUR(addr::SC4PipeBase_GetPosition, GetPosition),
    OPENSC4_DETOUR(addr::SC4PipeBase_GetBoundingBox, GetBoundingBox),
    OPENSC4_DETOUR(addr::SC4PipeBase_ComputeBoundingBox, ComputeBoundingBox),
    OPENSC4_DETOUR(addr::SC4PipeBase_ComputeFloorDepth, ComputeFloorDepth),
    OPENSC4_DETOUR(addr::SC4PipeBase_SetPiece, SetPiece),
    OPENSC4_DETOUR(addr::SC4PipeBase_SetHeights, SetHeights),
    OPENSC4_DETOUR(addr::SC4PipeBase_SetSurfaceHeights, SetSurfaceHeights),
    OPENSC4_DETOUR(addr::SC4PipeBase_Rebuild, Rebuild),
    OPENSC4_DETOUR(addr::SC4PipeBase_Refresh, Refresh),
    OPENSC4_DETOUR(addr::SC4PipeBase_Write, Write),
};

bool InstallSlots(registry::Mode) { return InstallDetours(kSlots, "slots"); }
bool InstallState(registry::Mode) { return InstallDetours(kState, "state"); }

const bool g_registered[] = {
    registry::Register({"simulators.pipebase.slots", "simulators/pipebase", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::SC4PipeBase_QueryInterface,
                        "Our small slots of the underground pieces' intermediate class: QueryInterface, the class "
                        "ID, IsPlaced, the piece key, the flag setters, the getters, the depth statics, the tint, "
                        "the edge depth, the runs, the occupant message and the statics (17 detours; "
                        "src/core/pipebase_1.cpp)",
                        false, InstallSlots}),
    registry::Register({"simulators.pipebase.state", "simulators/pipebase", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::SC4PipeBase_ComputeFloorDepth,
                        "Our geometry state of the underground pieces: the sides, the position and the boxes, the "
                        "floor depth, the setters, the rebuild, the refresh and Write (version 3) (11 detours; "
                        "src/core/pipebase_1.cpp)",
                        false, InstallState}),
};

} // namespace
} // namespace opensc4::pipebase_sim
