// The intermediate class of the underground pieces, second half (build 641;
// the fields are in src/core/pipebase_2.h, the class map and the small slots
// in the first half's files): the class's life and the functions that fill
// its saved vectors, each a detour at the game's address, because the pipe
// and the subway classes call the constructor, the destructor, the occupant
// part's Init and Read directly and reach the others through their vtables
// and thunks.
//
// Every function was written from the disassembly. The detours are plain
// functions, so they clobber ECX and EDX; none of the game's functions
// replaced here keeps both. `self` is the object's start unless a function
// says `part` (Read gets the cIGZSerializable part, the object + 8; the
// occupant part's Init the object + 4).
//
// Left as the game's, with the reason (the notes for docs/pipebase.md have
// the evidence): Init 0x0065C6B0 and slots 78 (0x0065CF20) and 87
// (0x0065D810) load textures, 0x0065D160 builds the two vectors that are
// not saved for slot 87 (drawing); slot 85 (0x0065B700, the transform and
// the matrix, 2,414 bytes of FPU code) for its size; 0x0065E6A0 and
// 0x0065E710 are called only by other classes' code, 0x0065E740, 0x0065E780
// and 0x0065E7C0 are a tree's helpers with callers elsewhere.
//
// Calls, each count from the asm's pushes for that call and the callee's own
// `ret N`:
//   - the stream (in vtable 0x00AA2280): 0x14 GetUint8, 0x1C GetUint16, 0x20
//     GetSint32, 0x24 GetUint32, 0x30 GetFloat32 one argument each (the
//     pointer), 0x48 GetError none;
//   - the object's own slots: 0xBC ConnectsToNeighbor two (edge, type;
//     0x00619320, ret 8), 0x148 none (0x00648460), 0x160 none (0x0065A3F0),
//     0x190 the refresh one (0x0065AF10, ret 4), 0x194 and 0x198 none (the
//     derived classes'); the occupant part's 0x14 IsInitialized none
//     (0x00458A40); a referenced object's Release (slot 2) none;
//   - the network occupant: constructor 0x0061BDD0 one (the flag, ret 4),
//     destructor 0x0061C170 none, the occupant part's Init 0x0061CB50 none,
//     Read 0x0061CC00 one (ret 4), the statics 0x0061C690 (cdecl, none); the
//     occupant base's Shutdown 0x005E8980 none (the game goes through the
//     thunk 0x00619100, which adds 4 to the pointer);
//   - this class: 0x0065AE80 (cdecl, none), 0x0065A640 one (the edge, ret 4),
//     0x0065CFF0 three (ret 0xC), the destructor 0x0065C0C0 none;
//   - 0x006585E0 (cdecl, four: `add esp, 0x10` at 0x0065E2DF; the fourth, the
//     out pointer, is pushed before the virtual call that gives the third);
//   - the corner vectors: the copy 0x006240D0 (cdecl, five pushed, three
//     read: first, last, destination; the result is the destination's end),
//     the push 0x00627100 one (ret 4), the growing insert 0x0082CDF0 five
//     (ret 0x14: position, record, a pointer that is not read, count, a byte
//     that says the position is the end), the resize 0x005E1B30 two (ret 8);
//   - the texture binding's release 0x008090E0 none; the stream helpers
//     0x005DA6B0 (sixteen floats) and 0x005D9FA0 (a bool), cdecl (stream,
//     pointer); the allocator's Free 0x0090CF63 (cdecl, one); the pool's
//     ReleaseBlock 0x0090D031 and ReopenBlock 0x0090CFF9 (one each, ret 4).
#include "core/pipebase_2.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/occ_vtables.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace opensc4::pipebase_2 {
namespace {

// Logs a function's first call once ("pipebase: first <name>").
void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("pipebase: first %s", name);
}

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

// The result of the stream's GetError, as the game turns it into the
// function's answer (neg al, sbb eax, eax, inc eax): 1 for no error.
uint32_t NoError(void* stream)
{
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// A corner record: the layout of the network occupant's corners.
struct Corner {
    float x, y, z;
    uint8_t bytes[4];
    float across, up;
};
static_assert(sizeof(Corner) == kCornerSize);

// An emptied vector, as the game empties it everywhere here: the records
// from the end to the end copied to the begin (0x006240D0, which copies
// nothing and answers the begin), and the answer stored as the new end. The
// fourth argument is the address of a stack slot the callee does not read.
void ClearVector(char* vector)
{
    uint32_t unused = 0;
    char* const begin = At<char*>(vector, 0x0);
    char* const end = At<char*>(vector, 0x4);
    At<char*>(vector, 0x4) = CallCdecl<char*, char*, char*, char*, uint32_t*, uint32_t>(
        addr::SC4CornerVector_CopyRange, end, end, begin, &unused, 0u);
}

// The four side wall vectors, then the floor's, +0x1C0, +0x1E4 and +0x1F0,
// in the order the constructor and Shutdown empty them.
void ClearVectors(char* self)
{
    for (std::size_t i = 0; i < 4; ++i)
        ClearVector(self + field::kWalls + i * kVectorSize);
    ClearVector(self + field::kFloor);
    ClearVector(self + field::kVector1C0);
    ClearVector(self + field::kVector1E4);
    ClearVector(self + field::kVector1F0);
}

// ====================================================================================
// simulators.pipebase.object: construction, destruction and Shutdown
// ====================================================================================

// simulators.pipebase.own: with it the objects carry our copies of the class's
// seven vtables (106, 34, 6, 25, 5, 1 and 19 slots) instead of the game's
// tables. The copies are taken at the first use (BuildOwnVtables, with the
// installation: it needs the object group's detour list).
bool g_own = false;
occ_vtables::VtableCopy<106> g_vtable;
occ_vtables::VtableCopy<34> g_vtable4;
occ_vtables::VtableCopy<6> g_vtable8;
occ_vtables::VtableCopy<25> g_vtableC;
occ_vtables::VtableCopy<5> g_vtable20;
occ_vtables::VtableCopy<1> g_vtable38;
occ_vtables::VtableCopy<19> g_vbaseVtable;
void BuildOwnVtables();

// The vtables the constructor and the destructor store (the game's, or with
// simulators.pipebase.own our copies of them): the six parts', then the
// virtual base's and the displacement word before it, both found through the
// table at +0x3C as the game does (the table is read twice).
void SetVtables(char* self)
{
    if (g_own && !g_vtable.Built())
        BuildOwnVtables();
    At<std::uintptr_t>(self, 0x0) = g_own ? g_vtable.Address() : addr::SC4PipeBase_vftable;
    At<std::uintptr_t>(self, field::kOccupant) = g_own ? g_vtable4.Address() : addr::SC4PipeBase_vftable_4;
    At<std::uintptr_t>(self, field::kSerializable) = g_own ? g_vtable8.Address() : addr::SC4PipeBase_vftable_8;
    At<std::uintptr_t>(self, field::kHolder) = g_own ? g_vtableC.Address() : addr::SC4PipeBase_vftable_C;
    At<std::uintptr_t>(self, field::kExemplarPart) = g_own ? g_vtable20.Address() : addr::SC4PipeBase_vftable_20;
    At<std::uintptr_t>(self, field::kModelPart) = g_own ? g_vtable38.Address() : addr::SC4PipeBase_vftable_38;
    const uint32_t displacement = At<uint32_t>(At<char*>(self, field::kVbtable), 4);
    At<std::uintptr_t>(self, displacement + field::kVbtable) =
        g_own ? g_vbaseVtable.Address() : addr::SC4PipeBase_vbase_vftable;
    const uint32_t again = At<uint32_t>(At<char*>(self, field::kVbtable), 4);
    At<uint32_t>(self, again + field::kModelPart) = again - kVbaseDisplacement;
}

// The constructor (0x0065C330; one argument: non-zero for the most derived
// object, which sets the virtual base table at +0x3C and the abstract
// vtable at +0x274 itself; the pipe and the subway call with 0 after their
// own table is in place). The network occupant's constructor with 0, the
// vtables; every word from +0x178 to +0x1F8 zero (the texture bindings, the
// wall count and the eight vectors); the matrix filled with the float the
// game keeps at 0x00B46874 (zero; read as the game does); the cell, the
// object at +0x244, the bytes at +0x248 to +0x24A and +0x260 to +0x262 and
// the words at +0x264 and +0x268 zero; the model part's state (+0x50 to
// +0x63) zero; the eight vectors emptied (of nothing); the matrix the
// identity. Not set, as in the game: the floats at +0x24C to +0x25C and
// the descriptor at +0x26C. The game's EAX is the object.
void* __fastcall Construct(char* self, void*, uint32_t mostDerived)
{
    static bool logged = false;
    FirstCall("Construct", logged);
    if (mostDerived) {
        At<std::uintptr_t>(self, field::kVbtable) = addr::SC4PipeBase_vbtable;
        At<std::uintptr_t>(self, field::kVbase) = addr::cSC4NetworkOccupant_vbase_abstract_vftable;
    }
    CallThis<void*, uint32_t>(addr::cSC4NetworkOccupant_cSC4NetworkOccupant, self, 0u);
    SetVtables(self);
    for (std::size_t offset = field::kTexture178; offset < field::kMatrix; offset += 4)
        At<uint32_t>(self, offset) = 0;
    for (std::size_t i = 0; i < 16; ++i)
        At<uint32_t>(self, field::kMatrix + 4 * i) = Global<uint32_t>(addr::SC4PipeBase_MatrixFill);
    At<uint32_t>(self, field::kCellX) = 0;
    At<uint32_t>(self, field::kCellZ) = 0;
    At<uint32_t>(self, field::kObject244) = 0;
    for (std::size_t i = 0; i < 3; ++i)
        At<uint8_t>(self, field::kByte248 + i) = 0;
    for (std::size_t i = 0; i < 3; ++i)
        At<uint8_t>(self, field::kDirty + i) = 0;
    At<uint32_t>(self, field::kPieceType) = 0;
    At<uint32_t>(self, field::kWord268) = 0;
    for (std::size_t i = 0; i < 5; ++i)
        At<uint32_t>(self, field::kModelState + 4 * i) = 0;
    ClearVectors(self);
    constexpr uint32_t kOne = 0x3F800000u;
    for (std::size_t i = 0; i < 16; ++i)
        At<uint32_t>(self, field::kMatrix + 4 * i) = i % 5 == 0 ? kOne : 0u;
    return self;
}

// The destructor (0x0065C0C0; the pipe's and the subway's deleting
// destructors end in it): the vtables as the constructor stores them, the
// memory of the eight vectors freed (+0x1F0, +0x1E4, +0x1C0, +0x1B4, then
// the four walls from the last to the first), then the network occupant's
// destructor (a tail jump). The texture bindings and the object at +0x244
// are not released here: Shutdown does that.
void __fastcall Destroy(char* self, void*)
{
    static bool logged = false;
    FirstCall("Destroy", logged);
    SetVtables(self);
    for (const std::size_t offset : {field::kVector1F0, field::kVector1E4, field::kVector1C0, field::kFloor})
        if (void* const memory = At<void*>(self, offset))
            CallCdecl<void, void*>(addr::SC4_Free, memory);
    for (std::size_t i = 4; i-- > 0;)
        if (void* const memory = At<void*>(self, field::kWalls + i * kVectorSize))
            CallCdecl<void, void*>(addr::SC4_Free, memory);
    CallThis<void>(addr::cSC4NetworkOccupant_Destroy, self);
}

// The deleting destructor (0x0065C650, one argument; the thunks 0x0065C1C0
// and 0x0065C1E3 of this class's own vtables jump to it, and no object has
// those vtables once a derived constructor ran, so the game never gets
// here): the destructor (at its address), and with flag 1 for a non-null
// object the memory back to the pool at 0x00B0D008. The result is the
// object.
void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("DeletingDestructor", logged);
    CallThis<void>(addr::SC4PipeBase_Destroy, self);
    if ((flags & 1) && self) {
        void* const pool = reinterpret_cast<void*>(addr::SC4PipeBase_Pool);
        char* const page = reinterpret_cast<char*>(reinterpret_cast<std::uintptr_t>(self) & kPoolPageMask);
        At<void*>(self, 0x0) = At<void*>(page, 0x8);
        const uint32_t used = At<uint32_t>(page, 0xC) - 1;
        At<void*>(page, 0x8) = self;
        At<uint32_t>(page, 0xC) = used;
        if (used == 0)
            CallThis<void, void*>(addr::SC4Pool_ReleaseBlock, pool, page);
        else if (used == kPoolReopenCount)
            CallThis<void, void*>(addr::SC4Pool_ReopenBlock, pool, page);
    }
    return self;
}

// Slot 4 (0x0065CD90; the occupant part's slot 4 is the thunk 0x0065CFE0 to
// it and the subway's 0x00652CF0 a jump): the eight texture bindings
// released (+0x1B0, +0x178, then +0x1CC to +0x1E0; all tested and released
// before any is cleared) and their pointers zeroed, the eight vectors
// emptied (their memory kept), the object at +0x244 released and cleared,
// then the occupant base's Shutdown, whose answer is the result.
uint32_t __fastcall Shutdown(char* self, void*)
{
    static bool logged = false;
    FirstCall("Shutdown", logged);
    constexpr std::size_t kBindings[] = {
        field::kTexture1B0,        field::kTexture178,        field::kTextures1CC,
        field::kTextures1CC + 0x4, field::kTextures1CC + 0x8, field::kTextures1CC + 0xC,
        field::kTextures1CC + 0x10, field::kTextures1CC + 0x14,
    };
    for (const std::size_t offset : kBindings)
        if (void* const binding = At<void*>(self, offset))
            CallThis<uint32_t>(addr::SC4TextureBinding_Release, binding);
    for (const std::size_t offset : kBindings)
        At<uint32_t>(self, offset) = 0;
    ClearVectors(self);
    if (void* const object = At<void*>(self, field::kObject244))
        Virtual<uint32_t>(object, slot::kRelease);
    At<uint32_t>(self, field::kObject244) = 0;
    return CallThis<uint32_t>(addr::cSC4Occupant_Shutdown, self + field::kOccupant);
}

// ====================================================================================
// simulators.pipebase.init: the occupant part's Init and Read
// ====================================================================================

// The occupant part's Init (0x0065C070; `part` is the object + 4; the
// pipe's and the subway's occupant Inits call it). The statics the first
// time (the byte at 0x00B46870 set, 0x0065AE80, and the network occupant's
// 0x0061C690 when those are not done). An object that is not initialised
// yet goes through the network occupant's occupant Init, and the answer of
// its slot 101 is stored as the descriptor at +0x26C. AL is 1.
uint8_t __fastcall OccupantInit(char* part, void*)
{
    static bool logged = false;
    FirstCall("OccupantInit", logged);
    if (!Global<uint8_t>(addr::SC4PipeBase_StaticsDone)) {
        Global<uint8_t>(addr::SC4PipeBase_StaticsDone) = 1;
        CallCdecl<void>(addr::SC4PipeBase_InitModels);
        if (!Global<uint8_t>(addr::cSC4NetworkOccupant_StaticsDone))
            CallCdecl<uint8_t>(addr::cSC4NetworkOccupant_InitStatics);
    }
    if (!Virtual<uint8_t>(part, 0x14)) {
        CallThis<uint8_t>(addr::cSC4NetworkOccupant_OccupantInit, part);
        char* const self = part - field::kOccupant;
        At<uint32_t>(self, field::kDescriptor) = Virtual<uint32_t>(self, slot::kGetDescriptor);
    }
    return 1;
}

// The reader of a corner vector (0x006574A0, the vector in EBX and the
// stream in ESI, so it is written out here; other classes call it too): the
// count, the vector resized to it with a zeroed record (0x005E1B30), each
// record read as it is written: x, y, z and the two last floats, then the
// bytes at +0xE, +0xD, +0xC and +0xF. The count lands in a slot of the
// game's frame that nothing sets first, so with a stream already in error
// the game goes on with whatever the stack held, ours with 0.
void ReadCornerVector(void* stream, char* vector)
{
    uint32_t count = 0;
    Virtual<uint8_t>(stream, 0x24, &count);
    uint32_t fill[kCornerSize / 4] = {};
    CallThis<void, uint32_t, void*>(addr::SC4CornerVector_Resize, vector, count, static_cast<void*>(fill));
    for (uint32_t i = 0; i < count; ++i) {
        for (const std::size_t offset : {std::size_t{0x0}, std::size_t{0x4}, std::size_t{0x8}, std::size_t{0x10},
                                         std::size_t{0x14}})
            Virtual<uint8_t>(stream, 0x30, At<char*>(vector, 0) + i * kCornerSize + offset);
        for (const std::size_t offset : {std::size_t{0xE}, std::size_t{0xD}, std::size_t{0xC}, std::size_t{0xF}})
            Virtual<uint8_t>(stream, 0x14, At<char*>(vector, 0) + i * kCornerSize + offset);
    }
}

// Read (0x0065E510; `part` is the object + 8; the pipe's and the subway's
// Reads call it, and the property holder's slot 24 through the thunk
// 0x0065E690): version 3 only. The game reads the version into its
// argument's stack slot (the stream pointer until the stream writes two
// bytes of it), so it starts with that value here. Another version: AL
// cleared, the rest of EAX as the stream's read left it. Then the network
// occupant's record; when that is good: the two first bytes of the model
// part's state (+0x50, +0x51) and its float (+0x60), the wall count, the
// six saved vectors (the four walls, the floor, +0x1C0), the matrix, the
// cell (two signed words, each read into a local slot that holds the
// part's address until the stream writes it, the `push ecx` of the game's
// frame, and copied to the object), the three bytes at +0x248 as bools, the
// four heights, the float at +0x25C, the words at +0x264 and +0x268; and
// the byte at +0x261 set to 1 and +0x262 to 0. The answer is the stream's
// state. The layout is in tools/re/save_schemas/pipeocc.py.
uint32_t __fastcall Read(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Read", logged);
    char* const self = part - field::kSerializable;
    uint32_t version = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(stream));
    const uint32_t read = Virtual<uint32_t>(stream, 0x1C, &version);
    if (static_cast<uint16_t>(version) != 3)
        return read & 0xFFFFFF00u;
    if (CallThis<uint8_t, void*>(addr::cSC4NetworkOccupant_Read, part, stream)) {
        Virtual<uint8_t>(stream, 0x14, self + field::kModelState);
        Virtual<uint8_t>(stream, 0x14, self + field::kModelState + 1);
        Virtual<uint8_t>(stream, 0x30, self + field::kStateFloat);
        Virtual<uint8_t>(stream, 0x24, self + field::kWallCount);
        for (std::size_t i = 0; i < 4; ++i)
            ReadCornerVector(stream, self + field::kWalls + i * kVectorSize);
        ReadCornerVector(stream, self + field::kFloor);
        ReadCornerVector(stream, self + field::kVector1C0);
        CallCdecl<void*, void*, char*>(addr::SC4Save_F5DA6B0, stream, self + field::kMatrix);
        uint32_t value = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(part));
        Virtual<uint8_t>(stream, 0x20, &value);
        At<uint32_t>(self, field::kCellX) = value;
        Virtual<uint8_t>(stream, 0x20, &value);
        At<uint32_t>(self, field::kCellZ) = value;
        for (std::size_t i = 0; i < 3; ++i)
            CallCdecl<void*, void*, char*>(addr::SC4Stream_ReadBool, stream, self + field::kByte248 + i);
        for (std::size_t i = 0; i < 4; ++i)
            Virtual<uint8_t>(stream, 0x30, self + field::kTops + 4 * i);
        Virtual<uint8_t>(stream, 0x30, self + field::kFloat25C);
        Virtual<uint8_t>(stream, 0x24, self + field::kPieceType);
        Virtual<uint8_t>(stream, 0x24, self + field::kWord268);
        At<uint8_t>(self, field::kDirty + 1) = 1;
        At<uint8_t>(self, field::kDirty + 2) = 0;
    }
    return NoError(stream);
}

// ====================================================================================
// simulators.pipebase.walls: the saved vectors (slots 89 and 90)
// ====================================================================================

// The first three of a record's four bytes set to 0xFF (the game stores
// +0xE, +0xD, +0xC); the fourth stays.
void White(Corner& record)
{
    record.bytes[2] = 0xFF;
    record.bytes[1] = 0xFF;
    record.bytes[0] = 0xFF;
}

// 0x0065CFF0 (three arguments, ret 0xC; only slot 89 calls it): the side
// wall between the corners `from` and `to`, four records pushed onto the
// vector (0x00627100): corner `from` at the height Init was given for it
// (+0x24C) with (0, 0); the corner as it is with (0, d); corner `to` as it
// is with (1, d); corner `to` at its given height with (1, 0), where d is
// (the given height of `from` less its own) x the float at 0x00A8E174
// (1/16), worked out on the FPU (so as a double) and stored as a float,
// once for the second record and again for the third. The first three
// bytes of each record are 0xFF, the fourth the corner's own. EAX is what
// the last push left.
void __fastcall AddSideWall(char* self, void*, uint32_t from, uint32_t to, char* vector)
{
    static bool logged = false;
    FirstCall("AddSideWall", logged);
    char* const first = self + field::kCorners + from * kCornerSize;
    Corner record;
    std::memcpy(&record, first, kCornerSize);
    record.y = At<float>(self, field::kTops + 4 * from);
    record.across = 0.0f;
    record.up = 0.0f;
    White(record);
    CallThis<void, Corner*>(addr::SC4CornerVector_PushBack, vector, &record);

    record.y = At<float>(first, 0x4);
    record.up = static_cast<float>((static_cast<double>(At<float>(self, field::kTops + 4 * from)) -
                                    static_cast<double>(At<float>(first, 0x4))) *
                                   static_cast<double>(Global<float>(addr::SC4_FloatSixteenth)));
    record.across = 0.0f;
    White(record);
    CallThis<void, Corner*>(addr::SC4CornerVector_PushBack, vector, &record);

    const float drop = static_cast<float>((static_cast<double>(At<float>(self, field::kTops + 4 * from)) -
                                           static_cast<double>(At<float>(first, 0x4))) *
                                          static_cast<double>(Global<float>(addr::SC4_FloatSixteenth)));
    std::memcpy(&record, self + field::kCorners + to * kCornerSize, kCornerSize);
    record.up = drop;
    record.across = 1.0f;
    White(record);
    CallThis<void, Corner*>(addr::SC4CornerVector_PushBack, vector, &record);

    record.across = 1.0f;
    record.y = At<float>(self, field::kTops + 4 * to);
    record.up = 0.0f;
    White(record);
    CallThis<void, Corner*>(addr::SC4CornerVector_PushBack, vector, &record);
}

// The next free side wall vector: +0x180 + 0xC x the wall count.
char* NextWall(char* self)
{
    return self + (At<uint32_t>(self, field::kWallCount) + 0x20) * kVectorSize;
}

// Slot 89 (0x0065E260; one argument, a byte: refresh after). The four wall
// vectors emptied, slot 88 called, the wall count zero. Then the piece asks
// 0x006585E0 for an object at its own cell and of its own type (slot 82):
//   - found: the object is released, nothing else;
//   - none, and the bytes at +0x249 and +0x24A differ: one wall is added
//     without asking the neighbours, between the corners 1 and 3 when the
//     byte at +0x248 is set and 0 and 2 when not, and two of the four edges
//     are struck out for the loop below (+0x248 set: edges 2 and 3 when
//     +0x249 is set, else 0 and 1; clear: 1 and 2 when set, else 0 and 3).
// Then for each edge 0 to 3 that is not struck out: a wall between the
// edge's corners (1 and 0, 0 and 3, 3 and 2, 2 and 1) unless the piece
// connects to its neighbour over that edge for its network type (slot 47
// with slot 102's answer) and 0x0065A640 says no for the edge. Each wall
// goes into the next of the four vectors and counts. Last, when the
// argument is set, the refresh (slot 100) with 1. EAX is what the last
// call left; it is not reproduced.
void __fastcall SetSideWalls(char* self, void*, uint32_t refresh)
{
    static bool logged = false;
    FirstCall("SetSideWalls", logged);
    for (std::size_t i = 0; i < 4; ++i)
        ClearVector(self + field::kWalls + i * kVectorSize);
    Virtual<uint32_t>(self, slot::kSlot88);
    At<uint32_t>(self, field::kWallCount) = 0;
    uint8_t open[4] = {1, 1, 1, 1};
    void* found = nullptr;
    const uint32_t type = Virtual<uint32_t>(self, slot::kGetPieceType);
    if (CallCdecl<uint8_t, uint32_t, uint32_t, uint32_t, void**>(addr::SC4PipeBase_FindPieceAt,
                                                                At<uint32_t>(self, field::kCellX),
                                                                At<uint32_t>(self, field::kCellZ), type, &found)) {
        Virtual<uint32_t>(found, slot::kRelease);
    } else {
        const uint8_t second = At<uint8_t>(self, field::kByte248 + 1);
        if ((At<uint8_t>(self, field::kByte248 + 2) ^ second) != 0) {
            uint32_t from = 0, to = 0;
            if (At<uint8_t>(self, field::kByte248) != 0) {
                from = 1;
                to = 3;
                if (second != 0) {
                    open[2] = 0;
                    open[3] = 0;
                } else {
                    open[0] = 0;
                    open[1] = 0;
                }
            } else {
                from = 0;
                to = 2;
                if (second != 0) {
                    open[1] = 0;
                    open[2] = 0;
                } else {
                    open[0] = 0;
                    open[3] = 0;
                }
            }
            CallThis<void, uint32_t, uint32_t, char*>(addr::SC4PipeBase_AddSideWall, self, from, to, NextWall(self));
            ++At<uint32_t>(self, field::kWallCount);
        }
    }
    constexpr uint32_t kEdgeCorners[4][2] = {{1, 0}, {0, 3}, {3, 2}, {2, 1}};
    for (uint32_t edge = 0; edge < 4; ++edge) {
        if (open[edge] == 0)
            continue;
        const uint32_t network = Virtual<uint32_t>(self, slot::kGetNetworkType);
        if (Virtual<uint8_t>(self, slot::kConnectsToNeighbor, edge, network) != 0 &&
            CallThis<uint8_t, uint32_t>(addr::SC4PipeBase_F65A640, self, edge) == 0)
            continue;
        CallThis<void, uint32_t, uint32_t, char*>(addr::SC4PipeBase_AddSideWall, self, kEdgeCorners[edge][0],
                                                  kEdgeCorners[edge][1], NextWall(self));
        ++At<uint32_t>(self, field::kWallCount);
    }
    if (static_cast<uint8_t>(refresh) != 0)
        Virtual<uint32_t>(self, slot::kRefresh, 1u);
}

// Slot 90 (0x0065E410; one argument, a byte: refresh after). The floor's
// vector (+0x1B4) emptied, then the four corners of the piece pushed onto
// it as they are but for the first three of their four bytes, which are
// 0xFF. The push is inline in the game: the record stored at the end when
// there is room (and the end is not null) and the end moved on, else the
// growing insert 0x0082CDF0 (position the end, one record, at the end).
// Last, when the argument is set, the refresh (slot 100) with 1. EAX is
// what the last call left; it is not reproduced.
void __fastcall SetFloor(char* self, void*, uint32_t refresh)
{
    static bool logged = false;
    FirstCall("SetFloor", logged);
    char* const vector = self + field::kFloor;
    ClearVector(vector);
    for (std::size_t corner = 0; corner < 4; ++corner) {
        Corner record;
        std::memcpy(&record, self + field::kCorners + corner * kCornerSize, kCornerSize);
        White(record);
        char* const end = At<char*>(vector, 0x4);
        if (end != At<char*>(vector, 0x8)) {
            if (end)
                std::memcpy(end, &record, kCornerSize);
            At<std::uintptr_t>(vector, 0x4) += kCornerSize;
        } else {
            uint32_t unused = 0;
            CallThis<void, char*, Corner*, uint32_t*, uint32_t, uint32_t>(addr::SC4CornerVector_InsertOverflow, vector,
                                                                         end, &record, &unused, 1u, 1u);
        }
    }
    if (static_cast<uint8_t>(refresh) != 0)
        Virtual<uint32_t>(self, slot::kRefresh, 1u);
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
// addresses (hex, eight digits), as OPENSC4_PIPEOCC_SKIP does.
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

const Detour kObject[] = {
    OPENSC4_DETOUR(addr::SC4PipeBase_cSC4PipeBase, Construct),
    OPENSC4_DETOUR(addr::SC4PipeBase_Destroy, Destroy),
    OPENSC4_DETOUR(addr::SC4PipeBase_DeletingDestructor, DeletingDestructor),
    OPENSC4_DETOUR(addr::SC4PipeBase_Shutdown, Shutdown),
};

const Detour kInit[] = {
    OPENSC4_DETOUR(addr::SC4PipeBase_OccupantInit, OccupantInit),
    OPENSC4_DETOUR(addr::SC4PipeBase_Read, Read),
};

const Detour kWalls[] = {
    OPENSC4_DETOUR(addr::SC4PipeBase_AddSideWall, AddSideWall),
    OPENSC4_DETOUR(addr::SC4PipeBase_SetSideWalls, SetSideWalls),
    OPENSC4_DETOUR(addr::SC4PipeBase_SetFloor, SetFloor),
};

bool InstallObject(registry::Mode) { return InstallDetours(kObject, "object"); }
bool InstallInit(registry::Mode) { return InstallDetours(kInit, "init"); }
bool InstallWalls(registry::Mode) { return InstallDetours(kWalls, "walls"); }

// simulators.pipebase.own: the copies, taken at the first construction or
// destruction that wants them, so after every PostAppInit replacement. Each is
// the game's table word for word. An entry that holds a function of the object
// group directly becomes ours without the detour: Shutdown, slot 4 of the
// interface's table (the deleting destructor is behind thunks only). Every
// other entry stays the game's address or adjustor thunk, which the other
// groups' detours still decide.
void BuildOwnVtables()
{
    g_vtable.Build(reinterpret_cast<const void*>(addr::SC4PipeBase_vftable));
    g_vtable4.Build(reinterpret_cast<const void*>(addr::SC4PipeBase_vftable_4));
    g_vtable8.Build(reinterpret_cast<const void*>(addr::SC4PipeBase_vftable_8));
    g_vtableC.Build(reinterpret_cast<const void*>(addr::SC4PipeBase_vftable_C));
    g_vtable20.Build(reinterpret_cast<const void*>(addr::SC4PipeBase_vftable_20));
    g_vtable38.Build(reinterpret_cast<const void*>(addr::SC4PipeBase_vftable_38));
    g_vbaseVtable.Build(reinterpret_cast<const void*>(addr::SC4PipeBase_vbase_vftable));
    unsigned redirected = 0;
    for (const Detour& detour : kObject) {
        if (Skipped(detour.address))
            continue;
        const void* const game = reinterpret_cast<const void*>(detour.address);
        redirected += occ_vtables::RedirectAll(game, detour.ours, g_vtable, g_vtable4, g_vtable8, g_vtableC, g_vtable20,
                                               g_vtable38, g_vbaseVtable);
    }
    log::Write("pipebase own: vtables copied (106, 34, 6, 25, 5, 1 and 19 slots), %u entries redirected", redirected);
}

// simulators.pipebase.own installs nothing: it only says which tables the
// object group's constructor and destructor store.
bool InstallOwn(registry::Mode)
{
    g_own = true;
    return true;
}

const bool g_registered[] = {
    registry::Register({"simulators.pipebase.object", "simulators/pipebase", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::SC4PipeBase_cSC4PipeBase,
                        "Our constructor, destructor, deleting destructor and Shutdown of the underground pieces' "
                        "intermediate class (4 detours; src/core/pipebase_2.cpp)",
                        false, InstallObject}),
    registry::Register({"simulators.pipebase.init", "simulators/pipebase", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::SC4PipeBase_OccupantInit,
                        "Our occupant part Init and Read (version 3) of the underground pieces' intermediate class "
                        "(2 detours; src/core/pipebase_2.cpp)",
                        false, InstallInit}),
    registry::Register({"simulators.pipebase.walls", "simulators/pipebase", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::SC4PipeBase_SetSideWalls,
                        "Our side walls and floor of the underground pieces (slots 89 and 90 and the wall helper "
                        "0x0065CFF0; 3 detours; src/core/pipebase_2.cpp)",
                        false, InstallWalls}),
    registry::Register({"simulators.pipebase.own", "simulators/pipebase", registry::Kind::Api,
                        registry::Phase::PostAppInit, addr::SC4PipeBase_vftable,
                        "Our own underground pieces' intermediate class: objects carry our copies of the class's seven "
                        "vtables; only has an effect with simulators.pipebase.object (src/core/pipebase_2.cpp)",
                        false, InstallOwn}),
};

} // namespace
} // namespace opensc4::pipebase_2
