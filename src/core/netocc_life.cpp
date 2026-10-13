// Our cSC4NetworkOccupant, second piece (build 641; src/core/netocc_sim.h has
// the class map and the fields): the class's life and its save record. Write
// and Read of the record (version 8; Read takes 4 and up), GetGZCLSID, the
// two Inits, QueryInterface, the constructor, the destructor, the deleting
// destructor, the pool's free and page set-up and the factory. Each is a
// detour at the game's address, because the bridge, tunnel and pipe classes
// call or hold the same code (their Write and Read call this class's first,
// their constructors the constructor with the flag 0).
//
// Every function was written from the disassembly. The detours are plain
// functions, so they clobber ECX and EDX. The game's functions that touch
// neither are GetGZCLSID 0x00619040 (called by the class registration at
// 0x00460724 before any replacement is installed, and through the vtable) and
// the pool's free 0x0061BD40 (called through the pool's table: a virtual
// call, so the caller keeps nothing in ECX or EDX); every other one uses
// both. `self` is the object's start (the cISC4NetworkOccupant part) unless
// a function says `part` (Write and Read get the cIGZSerializable part, the
// object + 8, the occupant part's Init the object + 4, the pool's free the
// pool).
//
// Virtual calls, each count from the asm's pushes for that call and the
// callee's own `ret N`:
//   - the streams (out vtable 0x00AA2310, in vtable 0x00AA2280; slots from
//     cIGZOStream and cIGZIStream): 0x14 SetUint8/GetUint8, 0x1C uint16,
//     0x24 uint32, 0x30 float, 0x28 SetSint64 two (low, high) and GetSint64
//     one (pointer), 0x48 GetError none; every other slot one (the value, or
//     the pointer to it);
//   - the interface's own slots: 0x18 CheckForRetrofit none, 0x1C
//     SetRetrofitFlag one, 0x28 SetNetworkFlag one, 0x2C ClearNetworkFlag
//     one, 0x120 SetComplexPieceOrigin two, 0x140 (slot 80, 0x0061A430) two
//     (ret 8); the object's AddRef/Release (slots 1, 2) none;
//   - the occupant part (object + 4): 0x0C Init none (0x0061CB50), 0x14
//     IsInitialized none (0x00458A40);
//   - the network manager (0x00B43CEC through 0x00602680): slot 0x5C one
//     (0x00612480, ret 4; the header names it GetEnglishNetworkName, the
//     manager's vtable 0x00AA7AD0 has it at slot 23);
//   - the virtual base (vtable 0x00AA7F10): slot 0x3C and 0x40 one pointer
//     (thunks to 0x00801E10 and 0x00801E50, each ret 4: copy 0x14 bytes out
//     of and into the model part's state at the object + 0x50);
//   - the resource reference of the object at +0x140: 0x005D9940 (stream,
//     object; three uint32, zeros for none) and 0x005D9820 (stream, class
//     ID, pointer to the pointer) are cdecl (plain ret).
// Direct callees: the occupant base's Write 0x005E8CD0, Read 0x005E8D60,
// QueryInterface 0x005E89C0 (each ret 4/4/8), constructor 0x005E9220 (none),
// Destroy 0x005E8C40 and Init 0x005E8C90 (none); the model part's
// constructor 0x00801CF0 (one: the flag), destructor 0x00801810 (none) and
// QueryInterface 0x00801940 (two, ret 8); the edge store's Write 0x00619C10
// and Read 0x00619C90 (one, ret 4); the path vector's resize 0x0061CAF0 (two,
// ret 8); the old flags' conversion 0x006191A0 (cdecl, one); the statics set
// up once 0x0061C690 (cdecl, none); the transform writers 0x005DA8C0 and
// readers 0x005DA710, 0x005DA930 (cdecl, stream and transform); the page
// allocator 0x0090D8E0 (cdecl, one); the pool's Grow 0x0090CF9A (none, the
// pool in ECX), ReleaseBlock 0x0090D031 and ReopenBlock 0x0090CFF9 (one,
// ret 4); the allocator's Free 0x0090CF63 and the operator delete 0x005E5620
// (cdecl, one).
#include "core/netocc_sim.h"

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

namespace opensc4::netocc_sim {
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

// The result of the stream's GetError, as the game turns it into the
// function's answer (neg al, sbb eax, eax, inc eax): 1 for no error.
uint32_t NoError(void* stream)
{
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// The corner records and the path records share a layout: three floats, four
// bytes and two more floats (0x18 bytes). Their order in the record: the
// three floats, then the two last, then the bytes at +0xE, +0xD, +0xC, +0xF.
constexpr std::size_t kCornerWords[] = {0x0, 0x4, 0x8, 0x10, 0x14};
constexpr std::size_t kCornerBytes[] = {0xE, 0xD, 0xC, 0xF};

// A path record is 0x4C bytes: a uint32 and three such corners from +4.
constexpr std::size_t kPathSize = 0x4C;

// ====================================================================================
// simulators.netocc.save: the save record
// ====================================================================================

// +0x8 slot 3 (0x0061A190; `part` is the object + 8). Version 8, then the
// occupant base's record (when that fails the rest is left out and the
// answer is still the stream's state), the transform, the four corners, the
// uint32 at +0xFC, the variation byte, the under texture, the rotation and
// flip byte, the edge store's record, the path records (a uint32 count and
// each), three pairs of floats, the flags, the path ID override, the
// resource key of the object at +0x140 (three uint32) and the demolition cost as one int64. The
// layout is in tools/re/save_schemas/netocc.py.
uint32_t __fastcall Write(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    char* const self = part - field::kSerializable;
    Virtual<uint8_t>(stream, 0x1C, 8u);
    if (CallThis<uint8_t, void*>(addr::cSC4Occupant_Write, part, stream)) {
        CallCdecl<void*, void*, const void*>(addr::SC4Stream_WriteTransform, stream, self + field::kTransform);
        for (std::size_t corner = 0; corner < 4; ++corner) {
            char* const record = self + field::kCorners + corner * field::kVertexStride;
            for (const std::size_t offset : kCornerWords)
                Virtual<uint8_t>(stream, 0x30, At<uint32_t>(record, offset));
            for (const std::size_t offset : kCornerBytes)
                Virtual<uint8_t>(stream, 0x14, uint32_t{At<uint8_t>(record, offset)});
        }
        Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, field::kUnknownFC));
        Virtual<uint8_t>(stream, 0x14, uint32_t{At<uint8_t>(self, field::kVariation)});
        Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, field::kUnderTexture));
        Virtual<uint8_t>(stream, 0x14, uint32_t{At<uint8_t>(self, field::kRotationFlip)});
        // The result is not looked at.
        CallThis<uint8_t, void*>(addr::cSC4EdgeConnectionStore_Write, self + field::kEdgeStore, stream);
        // The count is a signed division of the bytes between begin and end.
        const int32_t span = static_cast<int32_t>(At<std::uintptr_t>(self, field::kPaths + 4) -
                                                  At<std::uintptr_t>(self, field::kPaths));
        const uint32_t count = static_cast<uint32_t>(span / static_cast<int32_t>(kPathSize));
        Virtual<uint8_t>(stream, 0x24, count);
        for (uint32_t i = 0; i < count; ++i) {
            char* const item = At<char*>(self, field::kPaths) + i * kPathSize;
            Virtual<uint8_t>(stream, 0x24, At<uint32_t>(item, 0));
            for (std::size_t sub = 0; sub < 0x48; sub += field::kVertexStride) {
                for (const std::size_t offset : kCornerWords)
                    Virtual<uint8_t>(stream, 0x30, At<uint32_t>(item, 4 + sub + offset));
                for (const std::size_t offset : kCornerBytes)
                    Virtual<uint8_t>(stream, 0x14, uint32_t{At<uint8_t>(item, 4 + sub + offset)});
            }
        }
        for (std::size_t i = 0; i < 3; ++i) {
            Virtual<uint8_t>(stream, 0x30, At<uint32_t>(self, field::kPairs + 4 * i));
            Virtual<uint8_t>(stream, 0x30, At<uint32_t>(self, field::kPairs + 0xC + 4 * i));
        }
        Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, field::kFlags));
        Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, field::kPathIDOverride));
        CallCdecl<uint8_t, void*, void*>(addr::SC4Save_F5D9940, stream, At<void*>(self, field::kReference));
        Virtual<uint8_t>(stream, 0x28, At<uint32_t>(self, field::kDemolitionCost),
                         At<uint32_t>(self, field::kDemolitionCost + 4));
    }
    return NoError(stream);
}

// +0x8 slot 4 (0x0061CC00; `part` is the object + 8). Version 4 and up are
// read (no upper bound), a lower one gives false with the rest of EAX as the
// stream's read left it. The version, the path count and the two origin
// floats land in uninitialised slots of the game's frame, which a stream
// already in error does not write (its getters return at once): the game
// then goes on with whatever the stack held, ours with 0. A good stream
// writes them all. The
// occupant base's record first (when that fails the rest is skipped and the
// answer is the stream's state), then:
//   - the transform: version 5 and up as written, 4 as nine floats of the
//     matrix (0x005DA930);
//   - the corners, the uint32 at +0xFC, the variation, the under texture,
//     the rotation and flip byte, the edge store (0x00619C90, result not
//     looked at), the path records (the vector resized to the count with a
//     zeroed record, each read; the last byte of each corner is set to 0xFF
//     after being read), the three pairs of floats;
//   - the flags: below version 6 converted from the old bits (0x006191A0);
//     bits 0xE000 (path information, translucent, 0x8000) cleared;
//   - the path ID override; when it is not 0: below version 7 two floats are
//     read and turned into the complex piece origin (the sum of corner 2's and
//     corner 0's x and z, half of it plus the float, over 16, floored with the
//     game's magic-number rounding and a fix for exact integers); below
//     version 8 the retrofit flag is set;
//   - the resource key (class 0xA9667793) at +0x140 and the cost;
//   - the model's state (the virtual base's slots 0x3C and 0x40): when the
//     last byte of corner 0 is not 0xFF, or bit 1 of the state's first byte
//     is set, that bit is cleared, the last byte of all four corners set to
//     0xFF and the state's float at +0x10 set to 255 * (1/255).
uint32_t __fastcall Read(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Read", logged);
    char* const self = part - field::kSerializable;
    uint32_t versionSlot = 0;
    const uint32_t first = Virtual<uint32_t>(stream, 0x1C, &versionSlot);
    const uint16_t version = static_cast<uint16_t>(versionSlot);
    if (version < 4)
        return first & 0xFFFFFF00u;
    if (CallThis<uint8_t, void*>(addr::cSC4Occupant_Read, part, stream)) {
        CallCdecl<void*, void*, void*>(version < 5 ? addr::SC4Stream_ReadMatrixAsTransform
                                                   : addr::SC4Stream_ReadTransform,
                                       stream, self + field::kTransform);
        for (std::size_t corner = 0; corner < 4; ++corner) {
            char* const record = self + field::kCorners + corner * field::kVertexStride;
            for (const std::size_t offset : kCornerWords)
                Virtual<uint8_t>(stream, 0x30, &At<uint32_t>(record, offset));
            for (const std::size_t offset : kCornerBytes)
                Virtual<uint8_t>(stream, 0x14, &At<uint8_t>(record, offset));
        }
        Virtual<uint8_t>(stream, 0x24, &At<uint32_t>(self, field::kUnknownFC));
        Virtual<uint8_t>(stream, 0x14, &At<uint8_t>(self, field::kVariation));
        Virtual<uint8_t>(stream, 0x24, &At<uint32_t>(self, field::kUnderTexture));
        Virtual<uint8_t>(stream, 0x14, &At<uint8_t>(self, field::kRotationFlip));
        CallThis<uint8_t, void*>(addr::cSC4EdgeConnectionStore_Read, self + field::kEdgeStore, stream);
        uint32_t count = 0;
        Virtual<uint8_t>(stream, 0x24, &count);
        uint32_t zeroRecord[kPathSize / 4] = {};
        CallThis<void, uint32_t, void*>(addr::cSC4NetworkOccupant_ResizePaths, self + field::kPaths, count,
                                        static_cast<void*>(zeroRecord));
        for (uint32_t i = 0; i < count; ++i) {
            Virtual<uint8_t>(stream, 0x24, At<char*>(self, field::kPaths) + i * kPathSize);
            for (std::size_t sub = 0; sub < 0x48; sub += field::kVertexStride) {
                char* const item = At<char*>(self, field::kPaths) + i * kPathSize;
                for (const std::size_t offset : kCornerWords)
                    Virtual<uint8_t>(stream, 0x30, &At<uint32_t>(item, 4 + sub + offset));
                for (const std::size_t offset : kCornerBytes)
                    Virtual<uint8_t>(stream, 0x14, &At<uint8_t>(item, 4 + sub + offset));
                At<uint8_t>(item, 4 + sub + 0xF) = 0xFF;
            }
        }
        for (std::size_t i = 0; i < 3; ++i) {
            Virtual<uint8_t>(stream, 0x30, &At<uint32_t>(self, field::kPairs + 4 * i));
            Virtual<uint8_t>(stream, 0x30, &At<uint32_t>(self, field::kPairs + 0xC + 4 * i));
        }
        Virtual<uint8_t>(stream, 0x24, &At<uint32_t>(self, field::kFlags));
        if (version < 6)
            At<uint32_t>(self, field::kFlags) = CallCdecl<uint32_t, uint32_t>(
                addr::cSC4NetworkOccupant_ConvertOldFlags, At<uint32_t>(self, field::kFlags));
        At<uint32_t>(self, field::kFlags) &= 0xFFFF1FFFu;
        Virtual<uint8_t>(stream, 0x24, &At<uint32_t>(self, field::kPathIDOverride));
        if (At<uint32_t>(self, field::kPathIDOverride) != 0) {
            if (version < 7) {
                uint32_t xBits = 0, zBits = 0;
                Virtual<uint8_t>(stream, 0x30, &xBits);
                Virtual<uint8_t>(stream, 0x30, &zBits);
                // The game works on the FPU: corner 2's and corner 0's x
                // added, halved, plus the stream's float, over 16, floored
                // as (value - 0.5 + 1.5 * 2^23) stored as a float with the
                // magic number's bits taken off, plus one when the value is
                // exactly that integer. Then the same for z.
                const double half = static_cast<double>(Global<float>(addr::SC4_FloatHalf));
                const double scale = static_cast<double>(Global<float>(addr::cSC4NetworkOccupant_OriginScale));
                const double magic = static_cast<double>(Global<float>(addr::cSC4NetworkOccupant_RoundMagic));
                const auto roundDown = [&](double value) {
                    const float stored = static_cast<float>((value - half) + magic);
                    uint32_t bits = 0;
                    std::memcpy(&bits, &stored, 4);
                    int32_t result = static_cast<int32_t>(bits + 0xB4C00000u);
                    const int32_t next = result + 1;
                    if (value == static_cast<double>(next))
                        result = next;
                    return result;
                };
                float xFloat = 0, zFloat = 0;
                std::memcpy(&xFloat, &xBits, 4);
                std::memcpy(&zFloat, &zBits, 4);
                const double x =
                    ((static_cast<double>(At<float>(self, field::kCorners + 2 * field::kVertexStride)) +
                      static_cast<double>(At<float>(self, field::kCorners))) *
                         half +
                     static_cast<double>(xFloat)) *
                    scale;
                const int32_t originX = roundDown(x);
                const double z =
                    ((static_cast<double>(At<float>(self, field::kCorners + 2 * field::kVertexStride + 8)) +
                      static_cast<double>(At<float>(self, field::kCorners + 8))) *
                         half +
                     static_cast<double>(zFloat)) *
                    scale;
                const int32_t originY = roundDown(z);
                Virtual<void>(self, slot::kSetComplexPieceOrigin, static_cast<uint32_t>(originX),
                              static_cast<uint32_t>(originY));
            }
            if (version < 8)
                Virtual<void>(self, slot::kSetRetrofitFlag, 1u);
        }
        CallCdecl<uint8_t, void*, uint32_t, void*>(addr::SC4Save_F5D9820, stream, 0xA9667793u,
                                                   &At<void*>(self, field::kReference));
        Virtual<uint8_t>(stream, 0x28, &At<uint32_t>(self, field::kDemolitionCost));
        char* const vbase = part + 0x34 + At<uint32_t>(At<char*>(self, field::kVbtable), 4);
        uint8_t state[0x14];
        Virtual<void>(vbase, 0x3C, static_cast<uint8_t*>(state));
        const uint32_t flagsAddress = field::kCorners + 0xF;
        if (At<uint8_t>(self, flagsAddress) != 0xFF || (state[0] & 2)) {
            state[0] &= 0xFD;
            for (std::size_t corner = 0; corner < 4; ++corner)
                At<uint8_t>(self, flagsAddress + corner * field::kVertexStride) = 0xFF;
            const float value = static_cast<float>(static_cast<double>(At<uint8_t>(self, flagsAddress)) *
                                                   static_cast<double>(Global<float>(addr::SC4_FloatInv255)));
            std::memcpy(state + 0x10, &value, 4);
            Virtual<void>(vbase, 0x40, static_cast<uint8_t*>(state));
        }
    }
    return NoError(stream);
}

// +0x8 slot 5 (0x00619040).
uint32_t __fastcall GetGZCLSID(void*, void*)
{
    static bool logged = false;
    FirstCall("GetGZCLSID", logged);
    return kClassId;
}

// ====================================================================================
// simulators.netocc.life: Inits and QueryInterface
// ====================================================================================

// The occupant part's slot 3 (0x0061CB50; `part` is the object + 4). The
// statics (0x0061C690) are set up the first time. An occupant that is not
// initialised yet: flag 0x2000 cleared, the model part's dword at +0x4C set
// to 0x1A, and one with the retrofit property is handed to the network
// manager's slot 0x5C; then the occupant base's Init (a tail jump). An
// initialised one: the usable bit (0x10000000) and the bit 0x800000 are
// cleared in memory and set again through SetNetworkFlag. The game's AL is 1
// in the second case and the base's (1) in the first; the rest of EAX is not
// reproduced.
uint8_t __fastcall OccupantInit(char* part, void*)
{
    static bool logged = false;
    FirstCall("OccupantInit", logged);
    char* const self = part - field::kOccupant;
    if (!Global<uint8_t>(addr::cSC4NetworkOccupant_StaticsDone))
        CallCdecl<uint8_t>(addr::cSC4NetworkOccupant_InitStatics);
    if (!Virtual<uint8_t>(part, 0x14)) {
        Virtual<void>(self, slot::kClearNetworkFlag, 0x2000u);
        At<uint32_t>(self, 0x4C) = 0x1A;
        if (Virtual<uint8_t>(self, slot::kCheckForRetrofit)) {
            void* const manager = CallCdecl<void*>(addr::SC4_GetNetworkManager);
            Virtual<void>(manager, 0x5C, self);
        }
        return CallThis<uint8_t>(addr::cSC4Occupant_Init, part);
    }
    uint32_t flags = At<uint32_t>(self, field::kFlags);
    if (flags & flag::kUsable) {
        flags &= 0xEFFFFFFFu;
        At<uint32_t>(self, field::kFlags) = flags;
        Virtual<void>(self, slot::kSetNetworkFlag, uint32_t{flag::kUsable});
    }
    flags = At<uint32_t>(self, field::kFlags);
    if (flags & 0x800000u) {
        flags &= 0xFF7FFFFFu;
        At<uint32_t>(self, field::kFlags) = flags;
        Virtual<void>(self, slot::kSetNetworkFlag, 0x800000u);
    }
    return 1;
}

// The interface's slot 3 (0x00619050; one argument: twelve floats, the x, y
// and z of the four corners). The occupant part's Init first (false when it
// says so, nothing else done); the corners' x, y and z stored; then slot 80
// of the interface with the addresses of the two float triples at +0x124 and
// +0x130. True.
uint8_t __fastcall NetworkInit(char* self, void*, const uint32_t* corners)
{
    static bool logged = false;
    FirstCall("NetworkInit", logged);
    if (!Virtual<uint8_t>(self + field::kOccupant, 0xC))
        return 0;
    for (std::size_t corner = 0; corner < 4; ++corner)
        for (std::size_t i = 0; i < 3; ++i)
            At<uint32_t>(self, field::kCorners + corner * field::kVertexStride + 4 * i) = corners[3 * corner + i];
    Virtual<void>(self, 0x140, self + field::kPairs, self + field::kPairs + 0xC);
    return 1;
}

// Slot 0 (0x006192C0; the other parts' thunks add or take off their offset
// and jump here): the interface's IID and 0xA9764C8C give the object itself
// (referenced through slot 1); the occupant base's QueryInterface is asked
// next (with the object + 4), and then the model part's (0x00801940, with the
// object + 0x68).
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    static bool logged = false;
    FirstCall("QueryInterface", logged);
    if (iid == kIidNetworkOccupant || iid == 0xA9764C8C) {
        *object = self;
        Virtual<uint32_t>(self, 0x4);
        return 1;
    }
    if (CallThis<uint8_t, uint32_t, void**>(addr::cSC4Occupant_QueryInterface, self + field::kOccupant, iid, object))
        return 1;
    return CallThis<uint8_t, uint32_t, void**>(addr::SC4Model_QueryInterface, self + field::kModelBase, iid, object);
}

// ====================================================================================
// simulators.netocc.object: construction and destruction
// ====================================================================================

// simulators.netocc.own: with it the objects carry our copies of the class's
// seven vtables (81, 34, 6, 25, 5, 1 and 19 slots) instead of the game's
// tables. The copies are taken at the first use (BuildOwnVtables, with the
// installation: it needs the object group's detour list).
bool g_own = false;
occ_vtables::VtableCopy<81> g_vtable;
occ_vtables::VtableCopy<34> g_vtable4;
occ_vtables::VtableCopy<6> g_vtable8;
occ_vtables::VtableCopy<25> g_vtableC;
occ_vtables::VtableCopy<5> g_vtable20;
occ_vtables::VtableCopy<1> g_vtable38;
occ_vtables::VtableCopy<19> g_vbaseVtable;
void BuildOwnVtables();

// The vtables the constructor and the destructor store (the game's, or with
// simulators.netocc.own our copies of them).
void SetVtables(char* self)
{
    if (g_own && !g_vtable.Built())
        BuildOwnVtables();
    At<std::uintptr_t>(self, 0x0) = g_own ? g_vtable.Address() : addr::cSC4NetworkOccupant_vftable;
    At<std::uintptr_t>(self, field::kOccupant) = g_own ? g_vtable4.Address() : addr::cSC4NetworkOccupant_vftable_4;
    At<std::uintptr_t>(self, field::kSerializable) = g_own ? g_vtable8.Address() : addr::cSC4NetworkOccupant_vftable_8;
    At<std::uintptr_t>(self, field::kHolder) = g_own ? g_vtableC.Address() : addr::cSC4NetworkOccupant_vftable_C;
    At<std::uintptr_t>(self, field::kExemplarPart) =
        g_own ? g_vtable20.Address() : addr::cSC4NetworkOccupant_vftable_20;
    At<std::uintptr_t>(self, field::kModelPart) = g_own ? g_vtable38.Address() : addr::cSC4NetworkOccupant_vftable_38;
    // The virtual base: its vtable and the displacement word before it
    // (0x140 less 0x140: zero for the layout of this class), both found
    // through the table at +0x3C, as the game does. The table is read
    // twice, as the game's code reads it.
    const uint32_t displacement = At<uint32_t>(At<char*>(self, field::kVbtable), 4);
    At<std::uintptr_t>(self, displacement + field::kVbtable) =
        g_own ? g_vbaseVtable.Address() : addr::cSC4NetworkOccupant_vbase_vftable;
    const uint32_t again = At<uint32_t>(At<char*>(self, field::kVbtable), 4);
    At<uint32_t>(self, again + field::kModelPart) = again - 0x140;
}

// The constructor (0x0061BDD0; one argument: non-zero for the most derived
// object, which sets the virtual base table at +0x3C and the temporary
// virtual base vtable at +0x17C itself; the derived classes call with 0
// after their own table is in place). The abstract vtable at +0, the
// occupant base (0x005E9220) at +4, the model part (0x00801CF0, flag 0) at
// +0x38, then the vtables; the transform: two flag bytes zero, the identity
// matrix, zero translation, scale 1.0; the corners, +0xFC, +0x100 zero; the
// rotation, variation and edge store zero; the path vector empty; flags
// 0x06000000; the reference, path ID override and cost zero; the occupant
// type; +0x144 to +0x167 zero and +0x168/+0x16C zero. The game first fills
// the transform's matrix and translation with the float at 0x00B4525C
// (zero) and then overwrites all of it; those stores are not made. Left as
// the pool's memory holds them (as in the game): +0x52 to +0x53, +0x66 and
// +0x67, +0x124 to +0x13B (set by the interface's Init).
void* __fastcall Construct(char* self, void*, uint32_t mostDerived)
{
    static bool logged = false;
    FirstCall("Construct", logged);
    if (mostDerived) {
        At<std::uintptr_t>(self, field::kVbtable) = addr::cSC4NetworkOccupant_vbtable;
        At<std::uintptr_t>(self, field::kVbase) = addr::cSC4NetworkOccupant_vbase_abstract_vftable;
    }
    At<std::uintptr_t>(self, 0x0) = addr::cSC4NetworkOccupant_abstract_vftable;
    CallThis<void*>(addr::cSC4Occupant_cSC4Occupant, self + field::kOccupant);
    CallThis<void*, uint32_t>(addr::SC4Model_ConstructModelPart, self + field::kModelPart, 0u);
    SetVtables(self);
    constexpr uint32_t kOne = 0x3F800000u;
    At<uint8_t>(self, field::kTransform) = 0;
    At<uint8_t>(self, field::kTransform + 1) = 0;
    for (std::size_t i = 0; i < 9; ++i)
        At<uint32_t>(self, field::kModelBase + 4 * i) = i % 4 == 0 ? kOne : 0u;
    for (std::size_t i = 0; i < 3; ++i)
        At<uint32_t>(self, field::kTransform + 0x28 + 4 * i) = 0;
    At<uint32_t>(self, field::kTransform + 0x34) = kOne;
    At<uint32_t>(self, field::kUnknownFC) = 0;
    At<uint32_t>(self, field::kUnderTexture) = 0;
    At<uint8_t>(self, field::kRotationFlip) = 0;
    At<uint8_t>(self, field::kVariation) = 0;
    At<uint8_t>(self, field::kEdgeStore) = 0;
    At<uint32_t>(self, field::kEdgeEntries) = 0;
    At<uint8_t>(self, field::kEdgeStore + 4) = 0;
    At<uint32_t>(self, field::kEdgeFirstValue) = 0;
    At<uint32_t>(self, field::kPaths) = 0;
    At<uint32_t>(self, field::kPaths + 4) = 0;
    At<uint32_t>(self, field::kPaths + 8) = 0;
    At<uint32_t>(self, field::kFlags) = 0x06000000;
    At<uint32_t>(self, field::kReference) = 0;
    At<uint32_t>(self, field::kPathIDOverride) = 0;
    At<uint32_t>(self, field::kDemolitionCost) = 0;
    At<uint32_t>(self, field::kDemolitionCost + 4) = 0;
    for (std::size_t offset = field::kCorners; offset < field::kUnknownFC; offset += 4)
        At<uint32_t>(self, offset) = 0;
    At<uint32_t>(self, field::kOccupant + 0x24) = kOccupantType;
    for (std::size_t offset = 0x144; offset < 0x164; offset += 4)
        At<uint32_t>(self, offset) = 0;
    At<uint32_t>(self, 0x168) = 0;
    At<uint32_t>(self, 0x16C) = 0;
    return self;
}

// The destructor (0x0061C170; the derived classes' end in it): the vtables
// as the constructor stores them, the referenced object released, the path
// vector's memory freed (0x0090CF63), the edge store's further entries
// freed (0x005E5620), the model part destroyed (0x00801810) and the
// occupant base destroyed (0x005E8C40, a tail jump).
void __fastcall Destroy(char* self, void*)
{
    static bool logged = false;
    FirstCall("Destroy", logged);
    SetVtables(self);
    if (void* const reference = At<void*>(self, field::kReference)) {
        Virtual<uint32_t>(reference, 0x8);
        At<uint32_t>(self, field::kReference) = 0;
    }
    if (void* const paths = At<void*>(self, field::kPaths))
        CallCdecl<void, void*>(addr::SC4_Free, paths);
    if (void* const entries = At<void*>(self, field::kEdgeEntries))
        CallCdecl<void, void*>(addr::SC4_operator_delete, entries);
    CallThis<void>(addr::SC4Model_DestroyModelPart, self + field::kModelPart);
    CallThis<void>(addr::cSC4Occupant_Destroy, self + field::kOccupant);
}

// An item of the pool back in its page (the page header is at the address
// with its low 15 bits clear): onto the page's free list, the used count
// down by one; at 0 the page is released, at 0x4000003F opened again.
void PoolReturn(void* pool, char* item)
{
    char* const page = reinterpret_cast<char*>(reinterpret_cast<std::uintptr_t>(item) & 0xFFFF8000u);
    At<void*>(item, 0x0) = At<void*>(page, 0x8);
    const uint32_t used = At<uint32_t>(page, 0xC) - 1;
    At<void*>(page, 0x8) = item;
    At<uint32_t>(page, 0xC) = used;
    if (used == 0)
        CallThis<void, void*>(addr::SC4Pool_ReleaseBlock, pool, page);
    else if (used == 0x4000003F)
        CallThis<void, void*>(addr::SC4Pool_ReopenBlock, pool, page);
}

// The deleting destructor (0x0061C4C0; the +0x38 part's slot 0 and the
// holder part's slot 22 reach it through thunks that take their offset off):
// the destructor, and with flag 1 for a non-null object the memory back to
// the pool at 0x00B0B394. The result is the object.
void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("DeletingDestructor", logged);
    Destroy(self, nullptr);
    if ((flags & 1) && self)
        PoolReturn(reinterpret_cast<void*>(addr::cSC4NetworkOccupant_Pool), self);
    return self;
}

// The pool's free (0x0061BD40; slot 1 of the pool's table at 0x00AA7DA8,
// ECX the pool, one argument: the item; null does nothing).
void __fastcall PoolFree(void* pool, void*, char* item)
{
    static bool logged = false;
    FirstCall("PoolFree", logged);
    if (!item)
        return;
    PoolReturn(pool, item);
}

// The pool's page set-up (0x00619D50; slot 2 of the same table; ECX is not
// read): a page of 32 KB (the allocator's order 0xF), 85 items of 0x180
// bytes from +0x10 chained upwards (each item's first word is the address of
// the next one, the last has 0), the page's free list at +8 the lowest item
// and its used count at +0xC zero. The result is the page.
char* __cdecl InitPoolPage()
{
    static bool logged = false;
    FirstCall("InitPoolPage", logged);
    char* const page = CallCdecl<char*, uint32_t>(addr::SC4_AllocatePoolPage, 0xFu);
    char* item = page + 0x7E10;
    std::uintptr_t previous = 0;
    char* lowest = nullptr;
    for (int i = 0; i < 0x55; ++i) {
        At<std::uintptr_t>(item, 0) = previous;
        lowest = item;
        previous = reinterpret_cast<std::uintptr_t>(item);
        item -= kObjectSize;
    }
    At<void*>(page, 0x8) = lowest;
    At<uint32_t>(page, 0xC) = 0;
    return page;
}

// The factory (0x0061C480): an item of the pool (the current page's free
// head; the pool refilled by Grow when the page has none, which returns the
// new head; the page is read again after), constructed as the most derived
// object. The result is the object itself, not the occupant part.
char* __cdecl Create()
{
    static bool logged = false;
    FirstCall("Create", logged);
    char* const pool = reinterpret_cast<char*>(addr::cSC4NetworkOccupant_Pool);
    char* item = At<char*>(At<char*>(pool, 0x4), 0x8);
    if (!item)
        item = CallThis<char*>(addr::SC4EdgePool_Grow, pool);
    At<void*>(At<char*>(pool, 0x4), 0x8) = *reinterpret_cast<void**>(item);
    ++At<uint32_t>(At<char*>(pool, 0x4), 0xC);
    return static_cast<char*>(Construct(item, nullptr, 1u));
}

// ====================================================================================
// Installation
// ====================================================================================

struct Detour {
    std::uintptr_t address;
    void* ours;
};

#define OPENSC4_DETOUR(address, fn) {address, reinterpret_cast<void*>(&fn)}

// Hooks to leave out, for bisecting: OPENSC4_NETOCC_SKIP lists function
// addresses (hex, eight digits), as in src/core/netocc_state.cpp.
bool Skipped(std::uintptr_t address)
{
    char list[1024] = {};
    if (!GetEnvironmentVariableA("OPENSC4_NETOCC_SKIP", list, sizeof list))
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
            log::Write("netocc %s: no address entry for 0x%08X", what, static_cast<unsigned>(detour.address));
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
    log::Write("netocc %s: %u detours", what, installed);
    return true;
}

const Detour kSave[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_Write, Write),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_Read, Read),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_GetGZCLSID, GetGZCLSID),
};

const Detour kLife[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_OccupantInit, OccupantInit),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_NetworkInit, NetworkInit),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_QueryInterface, QueryInterface),
};

const Detour kObject[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_cSC4NetworkOccupant, Construct),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_Destroy, Destroy),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_DeletingDestructor, DeletingDestructor),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_PoolFree, PoolFree),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_InitPoolPage, InitPoolPage),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_Create, Create),
};

bool InstallSave(registry::Mode) { return InstallDetours(kSave, "save"); }
bool InstallLife(registry::Mode) { return InstallDetours(kLife, "life"); }
bool InstallObject(registry::Mode) { return InstallDetours(kObject, "object"); }

// simulators.netocc.own: the copies, taken at the first construction or
// destruction that wants them, so after every PostAppInit replacement. Each is
// the game's table word for word. An entry that holds a function of the object
// group directly would become ours without the detour; none of this class's
// does (the deleting destructor is behind thunks only), so the count logged is
// 0. Every other entry stays the game's address or adjustor thunk, which the
// other groups' detours still decide.
void BuildOwnVtables()
{
    g_vtable.Build(reinterpret_cast<const void*>(addr::cSC4NetworkOccupant_vftable));
    g_vtable4.Build(reinterpret_cast<const void*>(addr::cSC4NetworkOccupant_vftable_4));
    g_vtable8.Build(reinterpret_cast<const void*>(addr::cSC4NetworkOccupant_vftable_8));
    g_vtableC.Build(reinterpret_cast<const void*>(addr::cSC4NetworkOccupant_vftable_C));
    g_vtable20.Build(reinterpret_cast<const void*>(addr::cSC4NetworkOccupant_vftable_20));
    g_vtable38.Build(reinterpret_cast<const void*>(addr::cSC4NetworkOccupant_vftable_38));
    g_vbaseVtable.Build(reinterpret_cast<const void*>(addr::cSC4NetworkOccupant_vbase_vftable));
    unsigned redirected = 0;
    for (const Detour& detour : kObject) {
        if (Skipped(detour.address))
            continue;
        const void* const game = reinterpret_cast<const void*>(detour.address);
        redirected += occ_vtables::RedirectAll(game, detour.ours, g_vtable, g_vtable4, g_vtable8, g_vtableC, g_vtable20,
                                               g_vtable38, g_vbaseVtable);
    }
    log::Write("netocc own: vtables copied (81, 34, 6, 25, 5, 1 and 19 slots), %u entries redirected", redirected);
}

// simulators.netocc.own installs nothing: it only says which tables the object
// group's constructor and destructor store.
bool InstallOwn(registry::Mode)
{
    g_own = true;
    return true;
}

const bool g_registered[] = {
    registry::Register({"simulators.netocc.save", "simulators/netocc", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkOccupant_Write,
                        "Our cSC4NetworkOccupant save record: Write, Read (version 8; 4 and up read) and "
                        "GetGZCLSID (3 detours; src/core/netocc_life.cpp)",
                        false, InstallSave}),
    registry::Register({"simulators.netocc.life", "simulators/netocc", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkOccupant_OccupantInit,
                        "Our cSC4NetworkOccupant Inits (the occupant part's and the interface's) and "
                        "QueryInterface (3 detours; src/core/netocc_life.cpp)",
                        false, InstallLife}),
    registry::Register({"simulators.netocc.object", "simulators/netocc", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkOccupant_cSC4NetworkOccupant,
                        "Our cSC4NetworkOccupant constructor, destructor, deleting destructor, the pool's free and "
                        "page set-up and the factory (6 detours; src/core/netocc_life.cpp)",
                        false, InstallObject}),
    registry::Register({"simulators.netocc.own", "simulators/netocc", registry::Kind::Api,
                        registry::Phase::PostAppInit, addr::cSC4NetworkOccupant_vftable,
                        "Our own cSC4NetworkOccupant: objects carry our copies of the class's seven vtables; only has "
                        "an effect with simulators.netocc.object (src/core/netocc_life.cpp)",
                        false, InstallOwn}),
};

} // namespace
} // namespace opensc4::netocc_sim
