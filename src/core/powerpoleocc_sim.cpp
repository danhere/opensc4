// Our cSC4PowerPoleOccupant (build 641; src/core/powerpoleocc_sim.h has the
// class map and the fields): the class's save record (Write, Read,
// GetGZCLSID), its life (QueryInterface, the occupant part's Init, the
// message handler), its object (constructor, destructor, deleting
// destructor, pool allocation and factory) and its small state and queries.
// Each is a detour at the game's address. Not ours: everything that builds
// or draws the tower's and the wires' models (listed in
// src/core/powerpoleocc_sim.h and docs/powerpoleocc.md), and the pool's
// free (0x0064ABB0) and page set-up (0x006497E0), which a second pool table
// (0x00AA9D04) shares.
//
// Every function was written from the disassembly. The detours are plain
// functions, so they clobber ECX and EDX. `self` is the object's start
// unless a function says `part` (Write and Read get the cIGZSerializable
// part, the object + 0xC; the occupant part's functions the object + 8; the
// message handler the object + 4).
//
// Virtual calls, each count from the asm's pushes for that call and the
// callee's own `ret N`:
//   - the streams (out vtable 0x00AA2310, in vtable 0x00AA2280; slots from
//     cIGZOStream and cIGZIStream): 0x14 SetUint8/GetUint8, 0x1C uint16,
//     0x20 sint32, 0x24 uint32, 0x30 float, each one argument (the value, or
//     the pointer to it); 0x48 GetError none;
//   - the objects' AddRef and Release (slots 0x4 and 0x8): none;
//   - the interface's slot 9 (0x24, 0x0064A650): none;
//   - the message (vtable 0x00ACEC00): slot 0x10 (0x009CEDE1) and 0x28
//     (0x0099BE66) none;
//   - the message server (vtable 0x00A8CAFC): slot 0x14 AddNotification
//     (0x0092E795) and 0x18 RemoveNotification (0x0092E560), two each:
//     the target, then the message;
//   - the city (0x00B43CEC): slots 0x16C and 0x170 (0x0045A390, 0x0045A3A0)
//     none, a float in ST0;
//   - the resource at +0xBC: slot 0 QueryInterface two.
// Direct callees: the occupant base's Write 0x005E8CD0, Read 0x005E8D60,
// QueryInterface 0x005E89C0 (each ret 4/4/8), constructor 0x005E9220 (none),
// Destroy 0x005E8C40 and Init 0x005E8C90 (none); the model part's
// constructor 0x00801CF0 (one: the flag), destructor 0x00801810 (none) and
// QueryInterface 0x00801940 (two, ret 8); the stream helpers (cdecl, the
// stream first): WriteTransform 0x005DA8C0, ReadTransform 0x005DA710 and
// ReadMatrixAsTransform 0x005DA930 (the transform), WriteReference 0x005D99A0
// (the object), ReadReference 0x005D9730 (class, pointer to the pointer, 0),
// WriteBool 0x005D9FD0 and ReadBool 0x005D9FA0, WriteVector3 0x005DA580 and
// ReadVector3 0x005DA550; the record vector's resize 0x0064E990 (one, ret 4),
// the list's resize 0x0064BBA0 (the count and a pointer to the item to copy,
// ret 8), the vector of vectors' erase 0x0064CF40 (first, last; ret 8) and
// insert 0x0064D130 (position, count, a pointer to the item; ret 0xC), the
// three-float vector's insert 0x004FBD30 (the same, ret 0xC) and erase tail
// 0x00506280 (cdecl, five), the corner vector's resize 0x005E1B30 (the
// count and the pointer; ret 8), the records' destruction 0x0064D450 (cdecl,
// three), the table pointer's update 0x00649B30 (none) and the statics set
// up once 0x0064CC20 (none); the allocator's Free 0x0090CF63 (cdecl, one);
// the pool's Grow 0x0090CF9A (none, the pool in ECX), ReleaseBlock
// 0x0090D031 and ReopenBlock 0x0090CFF9 (one, ret 4).
#include "core/powerpoleocc_sim.h"

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

namespace opensc4::powerpoleocc_sim {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("powerpoleocc: first %s", name);
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

// The result of the stream's GetError, as the game turns it into the
// function's answer (neg al, sbb eax, eax, inc eax): 1 for no error.
uint32_t NoError(void* stream)
{
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// The sizes the vectors use: records 0x2C, the wire vectors 12 (three
// floats, or a vector's three words) and the corner records 0x18. The game
// counts them with a signed division of the bytes between begin and end.
constexpr uint32_t kRecordSize = 0x2C;
constexpr uint32_t kTripleSize = 12;
constexpr uint32_t kCornerSize = 0x18;

// The number of items of the vector at `vector` (begin, +4 end): the signed
// division of the bytes between them.
int32_t Count(char* vector, uint32_t size)
{
    const int32_t span = static_cast<int32_t>(At<std::uintptr_t>(vector, 4) - At<std::uintptr_t>(vector, 0));
    return span / static_cast<int32_t>(size);
}

// ====================================================================================
// simulators.powerpoleocc.save: the save record
// ====================================================================================

// The writer of a corner vector (0x0065B070, with the vector in EBX and
// the stream in ESI; the game's stays as it is for the other classes that
// call it): the count as a uint32, then each 0x18-byte record as the network
// occupant's corners are written: the floats at +0, +4, +8, +0x10 and +0x14,
// then the bytes at +0xE, +0xD, +0xC and +0xF.
void WriteCornerVector(void* stream, char* vector)
{
    const uint32_t count = static_cast<uint32_t>(Count(vector, kCornerSize));
    Virtual<uint8_t>(stream, 0x24, count);
    for (uint32_t i = 0; i < count; ++i) {
        char* const record = At<char*>(vector, 0) + i * kCornerSize;
        for (const std::size_t offset : {std::size_t{0x0}, std::size_t{0x4}, std::size_t{0x8}, std::size_t{0x10},
                                         std::size_t{0x14}})
            Virtual<uint8_t>(stream, 0x30, At<uint32_t>(record, offset));
        for (const std::size_t offset : {std::size_t{0xE}, std::size_t{0xD}, std::size_t{0xC}, std::size_t{0xF}})
            Virtual<uint8_t>(stream, 0x14, uint32_t{At<uint8_t>(record, offset)});
    }
}

// The writer of a vector of vectors of three floats (a record's +0x14 and
// +0x20): the outer count, then for each inner vector its count and its
// triples.
void WriteOuter(void* stream, char* outer)
{
    const uint32_t count = static_cast<uint32_t>(Count(outer, kTripleSize));
    Virtual<uint8_t>(stream, 0x24, count);
    for (uint32_t j = 0; j < count; ++j) {
        char* const inner = At<char*>(outer, 0) + j * kTripleSize;
        const uint32_t inside = static_cast<uint32_t>(Count(inner, kTripleSize));
        Virtual<uint8_t>(stream, 0x24, inside);
        for (uint32_t k = 0; k < inside; ++k)
            CallCdecl<void*, void*, const void*>(addr::SC4Stream_WriteVector3, stream,
                                                 At<char*>(inner, 0) + k * kTripleSize);
    }
}

// The cIGZSerializable part's slot 3 (0x0064B010; `part` is the object +
// 0xC). Version 5, then the occupant base's record (when that fails the rest
// is left out and the answer is still the stream's state), the transform,
// the position (three floats), the number of records and each one (the
// other tower as a reference to the object + 0xC, the number of list items
// and each item as a reference to the object + 8, two bools, two int32, the
// two vectors of vectors), the uint32 at +0xB8, the four int32 from +0xC0,
// the four triples of the table at +0xE4, the three corner vectors, the
// float at +0x10C, the bool at +0x110, the two highlight bytes, the float at
// +0x64 and the byte at +0x112. The layout is in
// tools/re/save_schemas/powerpoleocc.py.
uint32_t __fastcall Write(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    char* const self = part - field::kSerializable;
    Virtual<uint8_t>(stream, 0x1C, 5u);
    if (CallThis<uint8_t, void*>(addr::cSC4Occupant_Write, part, stream)) {
        CallCdecl<void*, void*, const void*>(addr::SC4Stream_WriteTransform, stream, self + field::kTransform);
        for (std::size_t i = 0; i < 3; ++i)
            Virtual<uint8_t>(stream, 0x30, At<uint32_t>(self, field::kPosition + 4 * i));
        const uint32_t count = static_cast<uint32_t>(Count(self + field::kRecords, kRecordSize));
        Virtual<uint8_t>(stream, 0x24, count);
        for (uint32_t i = 0; i < count; ++i) {
            char* const record = At<char*>(self, field::kRecords) + i * kRecordSize;
            char* const target = At<char*>(record, 0x0);
            CallCdecl<void*, void*, const void*>(addr::SC4Stream_WriteReference, stream,
                                                 target ? target + 0xC : nullptr);
            char* const header = At<char*>(record, 0x4);
            uint32_t items = 0;
            for (char* node = At<char*>(header, 0); node != header; node = At<char*>(node, 0))
                ++items;
            Virtual<uint8_t>(stream, 0x24, items);
            for (char* node = At<char*>(header, 0); node != header; node = At<char*>(node, 0)) {
                char* const value = At<char*>(node, 0x8);
                CallCdecl<void*, void*, const void*>(addr::SC4Stream_WriteReference, stream,
                                                     value ? value + 0x8 : nullptr);
            }
            CallCdecl<void*, void*, uint32_t>(addr::SC4Stream_WriteBool, stream, uint32_t{At<uint8_t>(record, 0x8)});
            CallCdecl<void*, void*, uint32_t>(addr::SC4Stream_WriteBool, stream, uint32_t{At<uint8_t>(record, 0x9)});
            Virtual<uint8_t>(stream, 0x20, At<uint32_t>(record, 0xC));
            Virtual<uint8_t>(stream, 0x20, At<uint32_t>(record, 0x10));
            WriteOuter(stream, record + 0x14);
            WriteOuter(stream, record + 0x20);
        }
        Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, field::kCount));
        for (std::size_t i = 0; i < 4; ++i)
            Virtual<uint8_t>(stream, 0x20, At<uint32_t>(self, field::kInts + 4 * i));
        for (std::size_t offset = 0; offset < 0x30; offset += 0xC)
            CallCdecl<void*, void*, const void*>(addr::SC4Stream_WriteVector3, stream,
                                                 At<char*>(self, field::kTable) + offset);
        WriteCornerVector(stream, self + field::kCorners0);
        WriteCornerVector(stream, self + field::kCorners1);
        WriteCornerVector(stream, self + field::kCorners2);
        Virtual<uint8_t>(stream, 0x30, At<uint32_t>(self, field::kFloat10C));
        CallCdecl<void*, void*, uint32_t>(addr::SC4Stream_WriteBool, stream,
                                          uint32_t{At<uint8_t>(self, field::kBool110)});
        Virtual<uint8_t>(stream, 0x14, uint32_t{At<uint8_t>(self, field::kHighlight)});
        Virtual<uint8_t>(stream, 0x14, uint32_t{At<uint8_t>(self, field::kHighlightValue)});
        Virtual<uint8_t>(stream, 0x30, At<uint32_t>(self, field::kFloat64));
        Virtual<uint8_t>(stream, 0x14, uint32_t{At<uint8_t>(self, field::kByte112)});
    }
    return NoError(stream);
}

// The reader of a corner vector (0x006574A0, vector in EBX, stream in ESI):
// the count, the vector resized to it with a zeroed record (0x005E1B30),
// each record read as it is written.
void ReadCornerVector(void* stream, char* vector)
{
    uint32_t count = 0;
    Virtual<uint8_t>(stream, 0x24, &count);
    uint32_t fill[6] = {};
    CallThis<void, uint32_t, void*>(addr::SC4CornerVector_Resize, vector, count, fill);
    for (uint32_t i = 0; i < count; ++i) {
        char* const record = At<char*>(vector, 0) + i * kCornerSize;
        for (const std::size_t offset : {std::size_t{0x0}, std::size_t{0x4}, std::size_t{0x8}, std::size_t{0x10},
                                         std::size_t{0x14}})
            Virtual<uint8_t>(stream, 0x30, record + offset);
        for (const std::size_t offset : {std::size_t{0xE}, std::size_t{0xD}, std::size_t{0xC}, std::size_t{0xF}})
            Virtual<uint8_t>(stream, 0x14, record + offset);
    }
}

// The reader of a record's vector of vectors (at +0x14 and at +0x20): the
// outer count; the vector cut to it (0x0064CF40) or grown with empty inner
// vectors (0x0064D130, a zeroed one to copy); then for each inner vector
// its count, the vector cut (0x00506280 copies the nothing from the end to
// the new end) or grown with copies of the zero vector the game keeps at
// 0x00B46778 (read as the game does, not assumed zero; 0x004FBD30), and the
// triples read in place.
void ReadOuter(void* stream, char* outer)
{
    uint32_t m = 0;
    Virtual<uint8_t>(stream, 0x24, &m);
    const uint32_t existing = static_cast<uint32_t>(Count(outer, kTripleSize));
    if (m < existing) {
        CallThis<void, void*, void*>(addr::cSC4PowerPoleOccupant_EraseOuter, outer,
                                     At<char*>(outer, 0) + m * kTripleSize, At<char*>(outer, 4));
    } else {
        uint32_t emptyInner[3] = {};
        CallThis<void, void*, uint32_t, void*>(addr::cSC4PowerPoleOccupant_InsertOuter, outer, At<char*>(outer, 4),
                                               m - existing, emptyInner);
    }
    for (uint32_t j = 0; j < m; ++j) {
        uint32_t k = 0;
        Virtual<uint8_t>(stream, 0x24, &k);
        char* const inner = At<char*>(outer, 0) + j * kTripleSize;
        const uint32_t zero = Global<uint32_t>(addr::SC4_ZeroVector3);
        const uint32_t fill[3] = {zero, zero, zero};
        char* const begin = At<char*>(inner, 0);
        char* const end = At<char*>(inner, 4);
        const uint32_t inside = static_cast<uint32_t>(Count(inner, kTripleSize));
        if (k < inside) {
            uint32_t unused[3] = {};
            At<char*>(inner, 4) = CallCdecl<char*, char*, char*, char*, void*, uint32_t>(
                addr::cSC4PowerPoleOccupant_EraseTail, end, end, begin + k * kTripleSize, unused, 0u);
        } else {
            CallThis<void, void*, uint32_t, const void*>(addr::SC4TripleVector_InsertN, inner, end, k - inside, fill);
        }
        for (uint32_t l = 0; l < k; ++l)
            CallCdecl<void*, void*, void*>(addr::SC4Stream_ReadVector3, stream, At<char*>(inner, 0) + l * kTripleSize);
    }
}

// The cIGZSerializable part's slot 4 (0x0064EA00; `part` is the object +
// 0xC). Version 4 and up are read (no upper bound), a lower one gives false.
// The statics are set up first (0x0064CC20) when they are not yet. The
// version, the count of records and the integers land in uninitialised
// slots of the game's frame, which a stream already in error does not write
// (its getters return at once): the game then goes on with whatever the
// stack held (the counts with the stream's address), ours with 0. A good stream
// writes them all. The occupant base's record first (when that fails the
// rest is skipped and the answer is the stream's state), then the transform
// (version 5 and up as written, 4 as a matrix: 0x005DA930), the position,
// the count and the records (the vector resized to it with a zeroed record
// by 0x0064E990; each record's other tower, its list resized to the count
// and each item read as a reference to class 0xC9C05C5D, the two bools, the
// two integers and the two vectors of vectors), the uint32 at +0xB8, the
// four integers, the table pointer updated (0x00649B30) and its four
// triples read, the three corner vectors, the float at +0x10C, the bool, the
// two highlight bytes, the float at +0x64, the byte at +0x112; last the
// object at +0xBC released and cleared.
uint32_t __fastcall Read(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Read", logged);
    char* const self = part - field::kSerializable;
    if (!Global<uint8_t>(addr::cSC4PowerPoleOccupant_StaticsDone))
        CallCdecl<void>(addr::cSC4PowerPoleOccupant_InitStatics);
    uint16_t version = 0;
    Virtual<uint8_t>(stream, 0x1C, &version);
    if (version < 4)
        return 0;
    if (CallThis<uint8_t, void*>(addr::cSC4Occupant_Read, part, stream)) {
        if (version < 5)
            CallCdecl<void*, void*, void*>(addr::SC4Stream_ReadMatrixAsTransform, stream, self + field::kTransform);
        else
            CallCdecl<void*, void*, void*>(addr::SC4Stream_ReadTransform, stream, self + field::kTransform);
        for (std::size_t i = 0; i < 3; ++i)
            Virtual<uint8_t>(stream, 0x30, self + field::kPosition + 4 * i);
        uint32_t count = 0;
        Virtual<uint8_t>(stream, 0x24, &count);
        CallThis<void, uint32_t>(addr::cSC4PowerPoleOccupant_ResizeRecords, self + field::kRecords, count);
        uint32_t integer = 0;
        for (uint32_t i = 0; i < count; ++i) {
            char* record = At<char*>(self, field::kRecords) + i * kRecordSize;
            CallCdecl<void*, void*, uint32_t, void*, uint32_t>(addr::SC4Stream_ReadReference, stream, kClassId, record,
                                                               0u);
            uint32_t items = 0;
            Virtual<uint8_t>(stream, 0x24, &items);
            record = At<char*>(self, field::kRecords) + i * kRecordSize;
            uint32_t copy = 0;
            CallThis<void, uint32_t, void*>(addr::cSC4PowerPoleOccupant_ResizeList, record + 0x4, items, &copy);
            integer = 0;
            record = At<char*>(self, field::kRecords) + i * kRecordSize;
            char* const header = At<char*>(record, 0x4);
            for (char* node = At<char*>(header, 0); node != header; node = At<char*>(node, 0))
                CallCdecl<void*, void*, uint32_t, void*, uint32_t>(addr::SC4Stream_ReadReference, stream,
                                                                   kClassWireTarget, node + 0x8, 0u);
            CallCdecl<void*, void*, void*>(addr::SC4Stream_ReadBool, stream,
                                           At<char*>(self, field::kRecords) + i * kRecordSize + 0x8);
            CallCdecl<void*, void*, void*>(addr::SC4Stream_ReadBool, stream,
                                           At<char*>(self, field::kRecords) + i * kRecordSize + 0x9);
            Virtual<uint8_t>(stream, 0x20, &integer);
            At<uint32_t>(At<char*>(self, field::kRecords) + i * kRecordSize, 0xC) = integer;
            Virtual<uint8_t>(stream, 0x20, &integer);
            At<uint32_t>(At<char*>(self, field::kRecords) + i * kRecordSize, 0x10) = integer;
            ReadOuter(stream, At<char*>(self, field::kRecords) + i * kRecordSize + 0x14);
            ReadOuter(stream, At<char*>(self, field::kRecords) + i * kRecordSize + 0x20);
        }
        Virtual<uint8_t>(stream, 0x24, self + field::kCount);
        for (std::size_t i = 0; i < 4; ++i) {
            Virtual<uint8_t>(stream, 0x20, &integer);
            At<uint32_t>(self, field::kInts + 4 * i) = integer;
        }
        CallThis<void>(addr::cSC4PowerPoleOccupant_UpdateTable, self);
        for (std::size_t offset = 0; offset < 0x30; offset += 0xC)
            CallCdecl<void*, void*, void*>(addr::SC4Stream_ReadVector3, stream,
                                           At<char*>(self, field::kTable) + offset);
        ReadCornerVector(stream, self + field::kCorners0);
        ReadCornerVector(stream, self + field::kCorners1);
        ReadCornerVector(stream, self + field::kCorners2);
        Virtual<uint8_t>(stream, 0x30, self + field::kFloat10C);
        CallCdecl<void*, void*, void*>(addr::SC4Stream_ReadBool, stream, self + field::kBool110);
        Virtual<uint8_t>(stream, 0x14, self + field::kHighlight);
        Virtual<uint8_t>(stream, 0x14, self + field::kHighlightValue);
        Virtual<uint8_t>(stream, 0x30, self + field::kFloat64);
        Virtual<uint8_t>(stream, 0x14, self + field::kByte112);
        if (void* const resource = At<void*>(self, field::kResource)) {
            At<void*>(self, field::kResource) = nullptr;
            Virtual<uint32_t>(resource, 0x8);
        }
    }
    return NoError(stream);
}

// The cIGZSerializable part's slot 5 (0x0064DD50): the class ID. The game's
// leaves ECX and EDX alone.
uint32_t __fastcall GetGZCLSID(char*, void*)
{
    static bool logged = false;
    FirstCall("GetGZCLSID", logged);
    return kClassId;
}

// ====================================================================================
// simulators.powerpoleocc.life: QueryInterface, Init and the message handler
// ====================================================================================

// Slot 0 (0x0064A030; the other parts' thunks add or take off their offset
// and jump here): the interface's IID, the class ID and 0xA9764C8C give the
// object itself (referenced through slot 1), 0x452294AA the message target
// (the object + 4, the same reference); 0x26A68448 is asked of the object at
// +0xBC when there is one (its slot 0 is jumped to with the same arguments);
// then the occupant base's QueryInterface (with the object + 8), and last the
// model part's (0x00801940, with the object + 0x6C).
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    static bool logged = false;
    FirstCall("QueryInterface", logged);
    if (iid == kIidInterface || iid == kClassId) {
        *object = self;
        Virtual<uint32_t>(self, 0x4);
        return 1;
    }
    if (iid == kIidTarget) {
        // The game tests the object for null first and then answers with a
        // null; an object is never null.
        if (self) {
            *object = self + field::kMessageTarget;
            Virtual<uint32_t>(self, 0x4);
            return 1;
        }
        *object = nullptr;
        Virtual<uint32_t>(self, 0x4);
        return 1;
    }
    if (iid == kIidResource) {
        if (void* const resource = At<void*>(self, field::kResource))
            return Virtual<uint8_t>(resource, 0x0, kIidResource, object);
    } else if (iid == kIidReference) {
        *object = self;
        Virtual<uint32_t>(self, 0x4);
        return 1;
    }
    if (CallThis<uint8_t, uint32_t, void**>(addr::cSC4Occupant_QueryInterface, self + field::kOccupant, iid, object))
        return 1;
    return CallThis<uint8_t, uint32_t, void**>(addr::SC4Model_QueryInterface, self + field::kMatrix, iid,
                                               object);
}

// The occupant part's slot 3 (0x00649FA0; `part` is the object + 8): the
// base's Init (its answer not looked at), the width and depth of a city cell
// taken from the city into the two floats at 0x00B46760 and 0x00B4675C, the
// object listening for the message 0x99EF1142 (once: the byte at +0x111 says
// so; only when the message server exists), and the object at +0xBC let go
// of. The answer is true.
uint8_t __fastcall OccupantInit(char* part, void*)
{
    static bool logged = false;
    FirstCall("OccupantInit", logged);
    CallThis<uint8_t>(addr::cSC4Occupant_Init, part);
    void* const city = Global<void*>(addr::SC4_City);
    Global<float>(addr::cSC4PowerPoleOccupant_CellWidthX) = Virtual<float>(city, 0x16C);
    Global<float>(addr::cSC4PowerPoleOccupant_CellWidthZ) = Virtual<float>(city, 0x170);
    char* const self = part - field::kOccupant;
    if (!At<uint8_t>(self, field::kListening)) {
        if (void* const server = Global<void*>(addr::SC4_MessageServer)) {
            Virtual<uint8_t>(server, 0x14, self + field::kMessageTarget, kMessageOccupant);
            At<uint8_t>(self, field::kListening) = 1;
        }
    }
    if (void* const resource = At<void*>(self, field::kResource)) {
        At<void*>(self, field::kResource) = nullptr;
        Virtual<uint32_t>(resource, 0x8);
    }
    return 1;
}

// A message about an occupant (0x0064A0F0; one argument: the occupant): when
// it is this object's own occupant part (the object + 8), the object stops
// listening (the message server's RemoveNotification, only when the byte at
// +0x111 says it listens and the server exists; the byte cleared with it),
// and in every case slot 9 of the interface runs.
void __fastcall OnOccupantMessage(char* self, void*, void* object)
{
    static bool logged = false;
    FirstCall("OnOccupantMessage", logged);
    if (object != (self ? self + field::kOccupant : nullptr))
        return;
    if (At<uint8_t>(self, field::kListening)) {
        if (void* const server = Global<void*>(addr::SC4_MessageServer)) {
            Virtual<uint8_t>(server, 0x18, self + field::kMessageTarget, kMessageOccupant);
            At<uint8_t>(self, field::kListening) = 0;
        }
    }
    Virtual<void>(self, slot::kSlot9);
}

// The message target's slot 3 (0x0064A140; `part` is the object + 4, one
// argument: the message): messages of type 0x99EF1142 hand their data (slot
// 0x28) to OnOccupantMessage. The answer is true whatever the message.
uint8_t __fastcall DoMessage(char* part, void*, void* message)
{
    static bool logged = false;
    FirstCall("DoMessage", logged);
    char* const self = part - field::kMessageTarget;
    if (Virtual<uint32_t>(message, 0x10) == kMessageOccupant) {
        void* const data = Virtual<void*>(message, 0x28);
        OnOccupantMessage(self, nullptr, data);
    }
    return 1;
}

// ====================================================================================
// simulators.powerpoleocc.object: construction and destruction
// ====================================================================================

// simulators.powerpoleocc.own: with it the objects carry our copies of the
// class's eight vtables (19, 4, 34, 6, 25, 5, 1 and 19 slots) instead of the
// game's tables. The copies are taken at the first use (BuildOwnVtables, with
// the installation: it needs the object group's detour list).
bool g_own = false;
occ_vtables::VtableCopy<19> g_vtable;
occ_vtables::VtableCopy<4> g_vtable4;
occ_vtables::VtableCopy<34> g_vtable8;
occ_vtables::VtableCopy<6> g_vtableC;
occ_vtables::VtableCopy<25> g_vtable10;
occ_vtables::VtableCopy<5> g_vtable24;
occ_vtables::VtableCopy<1> g_vtable3C;
occ_vtables::VtableCopy<19> g_vbaseVtable;
void BuildOwnVtables();

// The vtables the constructor and the destructor store (the game's, or with
// simulators.powerpoleocc.own our copies of them).
void SetVtables(char* self)
{
    if (g_own && !g_vtable.Built())
        BuildOwnVtables();
    At<std::uintptr_t>(self, 0x0) = g_own ? g_vtable.Address() : addr::cSC4PowerPoleOccupant_vftable;
    At<std::uintptr_t>(self, field::kMessageTarget) =
        g_own ? g_vtable4.Address() : addr::cSC4PowerPoleOccupant_vftable_4;
    At<std::uintptr_t>(self, field::kOccupant) = g_own ? g_vtable8.Address() : addr::cSC4PowerPoleOccupant_vftable_8;
    At<std::uintptr_t>(self, field::kSerializable) =
        g_own ? g_vtableC.Address() : addr::cSC4PowerPoleOccupant_vftable_C;
    At<std::uintptr_t>(self, field::kHolder) = g_own ? g_vtable10.Address() : addr::cSC4PowerPoleOccupant_vftable_10;
    At<std::uintptr_t>(self, field::kExemplarPart) =
        g_own ? g_vtable24.Address() : addr::cSC4PowerPoleOccupant_vftable_24;
    At<std::uintptr_t>(self, field::kModelPart) = g_own ? g_vtable3C.Address() : addr::cSC4PowerPoleOccupant_vftable_3C;
    // The virtual base: its vtable and the displacement word before it
    // (0xE0 less 0xE0: zero for the layout of this class), both found
    // through the table at +0x40, as the game does. The table is read
    // twice, as the game's code reads it.
    const uint32_t displacement = At<uint32_t>(At<char*>(self, field::kVbtable), 4);
    At<std::uintptr_t>(self, displacement + field::kVbtable) =
        g_own ? g_vbaseVtable.Address() : addr::cSC4PowerPoleOccupant_vbase_vftable;
    const uint32_t again = At<uint32_t>(At<char*>(self, field::kVbtable), 4);
    At<uint32_t>(self, again + 0x3C) = again - 0xE0;
}

// The constructor (0x0064DB30; one argument: non-zero for the most derived
// object, which sets the virtual base table at +0x40 and the temporary
// virtual base vtable at +0x120 itself). The abstract vtables at +0 and +4,
// the occupant base (0x005E9220) at +8, the model part (0x00801CF0, flag 0)
// at +0x3C, then the vtables; the transform: two flag bytes zero, the
// identity matrix, zero translation, scale 1.0; the position, the records
// (an empty vector), the count, the object at +0xBC and the integers (-1,
// -1, 0, 0); the bounds table pointer, the dirty byte (1), the corner
// vectors, the float, the three bytes and the model pointer zero and the byte
// at +0x118; last the occupant type. The game first fills the transform's
// matrix and translation with the vector it keeps at 0x00B46778 and then
// overwrites all of it; those stores are not made. Left as the pool's
// memory holds them (as in the game): +0x54, +0x55 and +0x64 (set by the
// interface's Init), the bounds at +0xD4 to +0xE0 (worked out when the dirty
// byte is set), +0x50 to +0x53 and +0x5C to +0x67 of the model part.
void* __fastcall Construct(char* self, void*, uint32_t mostDerived)
{
    static bool logged = false;
    FirstCall("Construct", logged);
    if (mostDerived) {
        At<std::uintptr_t>(self, field::kVbtable) = addr::cSC4PowerPoleOccupant_vbtable;
        At<std::uintptr_t>(self, 0x120) = addr::cSC4NetworkOccupant_vbase_abstract_vftable;
    }
    At<std::uintptr_t>(self, 0x0) = addr::cSC4PowerPoleOccupant_abstract_vftable;
    At<std::uintptr_t>(self, field::kMessageTarget) = addr::cSC4TrafficSimulator_VtableBase4;
    CallThis<void*>(addr::cSC4Occupant_cSC4Occupant, self + field::kOccupant);
    CallThis<void*, uint32_t>(addr::SC4Model_ConstructModelPart, self + field::kModelPart, 0u);
    SetVtables(self);
    constexpr uint32_t kOne = 0x3F800000u;
    At<uint8_t>(self, field::kTransform) = 0;
    At<uint8_t>(self, field::kTransform + 1) = 0;
    for (std::size_t i = 0; i < 9; ++i)
        At<uint32_t>(self, field::kMatrix + 4 * i) = i % 4 == 0 ? kOne : 0u;
    for (std::size_t i = 0; i < 3; ++i)
        At<uint32_t>(self, field::kTranslation + 4 * i) = 0;
    At<uint32_t>(self, field::kScale) = kOne;
    for (std::size_t offset = field::kPosition; offset < field::kResource + 4; offset += 4)
        At<uint32_t>(self, offset) = 0;
    At<uint32_t>(self, field::kInts) = 0xFFFFFFFFu;
    At<uint32_t>(self, field::kInts + 4) = 0xFFFFFFFFu;
    At<uint32_t>(self, field::kInts + 8) = 0;
    At<uint32_t>(self, field::kInts + 12) = 0;
    At<uint32_t>(self, field::kTable) = 0;
    At<uint8_t>(self, field::kBoundsDirty) = 1;
    for (std::size_t offset = field::kCorners0; offset < field::kFloat10C; offset += 4)
        At<uint32_t>(self, offset) = 0;
    At<uint32_t>(self, field::kFloat10C) = 0;
    At<uint8_t>(self, field::kBool110) = 0;
    At<uint8_t>(self, field::kListening) = 0;
    At<uint8_t>(self, field::kByte112) = 0;
    At<uint32_t>(self, field::kHighlightModel) = 0;
    At<uint8_t>(self, field::kByte118) = 0;
    At<uint32_t>(self, field::kType) = kOccupantType;
    return self;
}

// The destructor (0x0064DFC0): the vtables as the constructor stores them,
// the object at +0xBC released and cleared, the highlight model at +0x114
// released (not cleared), the memory of the three corner vectors freed
// (0x0090CF63; the one at +0x100 first, then +0xF4, then +0xE8), the object
// at +0xBC released once more (it is clear by then), the records destroyed
// (0x0064D450) and their memory freed, the model part destroyed (0x00801810)
// and the occupant base destroyed (0x005E8C40).
void __fastcall Destroy(char* self, void*)
{
    static bool logged = false;
    FirstCall("Destroy", logged);
    SetVtables(self);
    if (void* const resource = At<void*>(self, field::kResource)) {
        At<void*>(self, field::kResource) = nullptr;
        Virtual<uint32_t>(resource, 0x8);
    }
    if (void* const highlight = At<void*>(self, field::kHighlightModel))
        Virtual<uint32_t>(highlight, 0x8);
    for (const std::size_t offset : {field::kCorners2, field::kCorners1}) {
        if (void* const memory = At<void*>(self, offset))
            CallCdecl<void, void*>(addr::SC4_Free, memory);
    }
    if (void* const memory = At<void*>(self, field::kCorners0))
        CallCdecl<void, void*>(addr::SC4_Free, memory);
    if (void* const resource = At<void*>(self, field::kResource))
        Virtual<uint32_t>(resource, 0x8);
    uint8_t unused = 0;
    CallCdecl<void*, void*, void*, void*>(addr::cSC4PowerPoleOccupant_DestroyRecords,
                                          At<void*>(self, field::kRecords), At<void*>(self, field::kRecords + 4),
                                          &unused);
    if (void* const memory = At<void*>(self, field::kRecords))
        CallCdecl<void, void*>(addr::SC4_Free, memory);
    CallThis<void>(addr::SC4Model_DestroyModelPart, self + field::kModelPart);
    CallThis<void>(addr::cSC4Occupant_Destroy, self + field::kOccupant);
}

// The deleting destructor (0x0064E380; the +0x3C part's slot 0 and the
// holder part's slot 22 reach it through thunks that take their offset off):
// the destructor, and with flag 1 for a non-null object the memory back to
// the pool at 0x00B0C0C8 (the item onto its page's free list, the page
// header at the address with its low 15 bits clear, the used count down by
// one; at 0 the page is released, at 0x40000054 opened again). The result is
// the object.
void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("DeletingDestructor", logged);
    Destroy(self, nullptr);
    if ((flags & 1) && self) {
        char* const page = reinterpret_cast<char*>(reinterpret_cast<std::uintptr_t>(self) & 0xFFFF8000u);
        At<void*>(self, 0x0) = At<void*>(page, 0x8);
        const uint32_t used = At<uint32_t>(page, 0xC) - 1;
        At<void*>(page, 0x8) = self;
        At<uint32_t>(page, 0xC) = used;
        void* const pool = reinterpret_cast<void*>(addr::cSC4PowerPoleOccupant_Pool);
        if (used == 0)
            CallThis<void, void*>(addr::SC4Pool_ReleaseBlock, pool, page);
        else if (used == 0x40000054u)
            CallThis<void, void*>(addr::SC4Pool_ReopenBlock, pool, page);
    }
    return self;
}

// An item of the pool (0x00B0C0C8): the current page's free head, the pool
// refilled by Grow when the page has none (it returns the new head; the page
// is read again after), the head moved to the item's first word and the
// page's used count up by one.
char* TakeFromPool()
{
    char* const pool = reinterpret_cast<char*>(addr::cSC4PowerPoleOccupant_Pool);
    char* item = At<char*>(At<char*>(pool, 0x4), 0x8);
    if (!item)
        item = CallThis<char*>(addr::SC4EdgePool_Grow, pool);
    At<void*>(At<char*>(pool, 0x4), 0x8) = *reinterpret_cast<void**>(item);
    ++At<uint32_t>(At<char*>(pool, 0x4), 0xC);
    return item;
}

// The pool allocation (0x006499D0; the caller pushes the size, 0x124, and
// takes it off again): an item of the pool, not constructed.
char* __cdecl Allocate()
{
    static bool logged = false;
    FirstCall("Allocate", logged);
    return TakeFromPool();
}

// The factory (0x0064E340): an item of the pool, constructed as the most
// derived object. The result is the object itself, not the occupant part.
char* __cdecl Create()
{
    static bool logged = false;
    FirstCall("Create", logged);
    return static_cast<char*>(Construct(TakeFromPool(), nullptr, 1u));
}

// ====================================================================================
// simulators.powerpoleocc.slots: the small state and queries
// ====================================================================================

// __ftol (0x009EEF04): truncation toward zero; the low dword of its int64
// (so a NaN, which the cell statics give before any tower's Init, is 0).
int32_t Ftol(double value) { return static_cast<int32_t>(static_cast<int64_t>(value)); }

// The occupant part's slot 10 (0x00649A00; `part` is the object + 8; two
// arguments: the low and the high corner, three floats each). While the
// dirty byte is set the cell bounds are worked out again: the position over
// the width and depth of a cell, cut to whole cells (the game's 0x009EEF04,
// a truncation), give both corners of the bounds; then every record that has
// points (its vector at +0x14 is not empty) widens them with its two
// integers, as unsigned numbers (x with the first and z with the second);
// the dirty byte is cleared. The corners are the bounds times the cell
// width and depth (the high ones one cell more), the lower one's height the
// float at +0x10C and the upper one's the position's y plus the float at
// 0x00AA9B10. The game's EAX at the end is the second pointer.
void* __fastcall GetBoundingBox(char* part, void*, float* low, float* high)
{
    static bool logged = false;
    FirstCall("GetBoundingBox", logged);
    char* const self = part - field::kOccupant;
    const double cellX = Global<float>(addr::cSC4PowerPoleOccupant_CellWidthX);
    const double cellZ = Global<float>(addr::cSC4PowerPoleOccupant_CellWidthZ);
    if (At<uint8_t>(self, field::kBoundsDirty)) {
        const uint32_t x = static_cast<uint32_t>(Ftol(
            static_cast<double>(At<float>(self, field::kPosition)) / cellX));
        At<uint32_t>(self, field::kBounds + 8) = x;
        At<uint32_t>(self, field::kBounds) = x;
        const uint32_t z = static_cast<uint32_t>(Ftol(
            static_cast<double>(At<float>(self, field::kPosition + 8)) / cellZ));
        At<uint32_t>(self, field::kBounds + 0xC) = z;
        At<uint32_t>(self, field::kBounds + 4) = z;
        for (char* record = At<char*>(self, field::kRecords); record != At<char*>(self, field::kRecords + 4);
             record += kRecordSize) {
            if (At<uint32_t>(record, 0x14) == At<uint32_t>(record, 0x18))
                continue;
            const uint32_t cellColumn = At<uint32_t>(record, 0xC);
            if (At<uint32_t>(self, field::kBounds) > cellColumn)
                At<uint32_t>(self, field::kBounds) = cellColumn;
            else if (At<uint32_t>(self, field::kBounds + 8) < cellColumn)
                At<uint32_t>(self, field::kBounds + 8) = cellColumn;
            const uint32_t cellRow = At<uint32_t>(record, 0x10);
            if (At<uint32_t>(self, field::kBounds + 4) > cellRow)
                At<uint32_t>(self, field::kBounds + 4) = cellRow;
            else if (At<uint32_t>(self, field::kBounds + 0xC) < cellRow)
                At<uint32_t>(self, field::kBounds + 0xC) = cellRow;
        }
        At<uint8_t>(self, field::kBoundsDirty) = 0;
    }
    low[0] = static_cast<float>(static_cast<double>(At<int32_t>(self, field::kBounds)) * cellX);
    low[2] = static_cast<float>(static_cast<double>(At<int32_t>(self, field::kBounds + 4)) * cellZ);
    std::memcpy(&low[1], &At<uint32_t>(self, field::kFloat10C), sizeof(uint32_t));
    high[0] = static_cast<float>(static_cast<double>(At<int32_t>(self, field::kBounds + 8) + 1) * cellX);
    high[2] = static_cast<float>(static_cast<double>(At<int32_t>(self, field::kBounds + 0xC) + 1) * cellZ);
    high[1] = static_cast<float>(static_cast<double>(At<float>(self, field::kPosition + 4)) +
                                 static_cast<double>(Global<float>(addr::cSC4PowerPoleOccupant_BoxTop)));
    return high;
}

// The occupant part's slot 8 (0x00649DA0; `part` is the object + 8): the
// position's three floats into the buffer. EAX keeps the argument.
uint32_t* __fastcall GetPosition(char* part, void*, uint32_t* out)
{
    static bool logged = false;
    FirstCall("GetPosition", logged);
    char* const self = part - field::kOccupant;
    out[0] = At<uint32_t>(self, field::kPosition);
    out[1] = At<uint32_t>(self, field::kPosition + 4);
    out[2] = At<uint32_t>(self, field::kPosition + 8);
    return out;
}

// Slot 4 (0x00649D80): the number of records, a signed division of the
// bytes of the vector by 0x2C.
int32_t __fastcall GetRecordCount(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetRecordCount", logged);
    return Count(self + field::kRecords, kRecordSize);
}

// Slot 7 (0x006499B0): the other tower of a record (no check of the index).
void* __fastcall GetRecordTarget(char* self, void*, uint32_t index)
{
    static bool logged = false;
    FirstCall("GetRecordTarget", logged);
    return At<void*>(At<char*>(self, field::kRecords), index * kRecordSize);
}

// Slot 8 (0x00649210; two arguments): the two integers at +0xC8 and +0xCC.
// EAX is left holding the second.
uint32_t __fastcall GetCellPair(char* self, void*, uint32_t* first, uint32_t* second)
{
    static bool logged = false;
    FirstCall("GetCellPair", logged);
    *first = At<uint32_t>(self, field::kCellPair);
    const uint32_t value = At<uint32_t>(self, field::kCellPair + 4);
    *second = value;
    return value;
}

// Slot 11 (0x00649230): whether the byte at +0x112 is not zero (0 or 1 in
// EAX; the game's clobbers EDX).
uint32_t __fastcall IsFlag112(char* self, void*)
{
    static bool logged = false;
    FirstCall("IsFlag112", logged);
    return At<uint8_t>(self, field::kByte112) != 0;
}

// Slot 13 (0x00649240): the byte at +0x118 in AL (the game leaves the rest
// of EAX alone and touches neither ECX nor EDX).
uint8_t __fastcall GetByte118(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetByte118", logged);
    return At<uint8_t>(self, field::kByte118);
}

// Slot 14 (0x00649250; one argument): the low byte of it stored at +0x118
// (the game touches neither ECX nor EDX; AL is left holding the byte).
uint8_t __fastcall SetByte118(char* self, void*, uint32_t value)
{
    static bool logged = false;
    FirstCall("SetByte118", logged);
    At<uint8_t>(self, field::kByte118) = static_cast<uint8_t>(value);
    return static_cast<uint8_t>(value);
}

// Slot 16 (0x00649DD0; two arguments): the record whose other tower is the
// first argument (the records in order), then slot 5 of the interface with
// the record's index and the second argument; its answer. False when there
// is no such record.
uint8_t __fastcall FindRecord(char* self, void*, void* target, uint32_t argument)
{
    static bool logged = false;
    FirstCall("FindRecord", logged);
    const uint32_t count = static_cast<uint32_t>(Count(self + field::kRecords, kRecordSize));
    for (uint32_t i = 0; i < count; ++i) {
        if (At<void*>(At<char*>(self, field::kRecords), i * kRecordSize) == target)
            return Virtual<uint8_t>(self, 0x14, i, argument);
    }
    return 0;
}

// ====================================================================================
// Installation
// ====================================================================================

struct Detour {
    std::uintptr_t address;
    void* ours;
};

#define OPENSC4_DETOUR(address, fn) {address, reinterpret_cast<void*>(&fn)}

// Hooks to leave out, for bisecting: OPENSC4_POWERPOLEOCC_SKIP lists function
// addresses (hex, eight digits).
bool Skipped(std::uintptr_t address)
{
    char list[1024] = {};
    if (!GetEnvironmentVariableA("OPENSC4_POWERPOLEOCC_SKIP", list, sizeof list))
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
            log::Write("powerpoleocc %s: no address entry for 0x%08X", what, static_cast<unsigned>(detour.address));
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
    log::Write("powerpoleocc %s: %u detours", what, installed);
    return true;
}

const Detour kSave[] = {
    OPENSC4_DETOUR(addr::cSC4PowerPoleOccupant_Write, Write),
    OPENSC4_DETOUR(addr::cSC4PowerPoleOccupant_Read, Read),
    OPENSC4_DETOUR(addr::cSC4PowerPoleOccupant_GetGZCLSID, GetGZCLSID),
};

const Detour kLife[] = {
    OPENSC4_DETOUR(addr::cSC4PowerPoleOccupant_QueryInterface, QueryInterface),
    OPENSC4_DETOUR(addr::cSC4PowerPoleOccupant_OccupantInit, OccupantInit),
    OPENSC4_DETOUR(addr::cSC4PowerPoleOccupant_OnOccupantMessage, OnOccupantMessage),
    OPENSC4_DETOUR(addr::cSC4PowerPoleOccupant_DoMessage, DoMessage),
};

const Detour kObject[] = {
    OPENSC4_DETOUR(addr::cSC4PowerPoleOccupant_cSC4PowerPoleOccupant, Construct),
    OPENSC4_DETOUR(addr::cSC4PowerPoleOccupant_Destroy, Destroy),
    OPENSC4_DETOUR(addr::cSC4PowerPoleOccupant_DeletingDestructor, DeletingDestructor),
    OPENSC4_DETOUR(addr::cSC4PowerPoleOccupant_Allocate, Allocate),
    OPENSC4_DETOUR(addr::cSC4PowerPoleOccupant_Create, Create),
};

const Detour kSlots[] = {
    OPENSC4_DETOUR(addr::cSC4PowerPoleOccupant_GetBoundingBox, GetBoundingBox),
    OPENSC4_DETOUR(addr::cSC4PowerPoleOccupant_GetPosition, GetPosition),
    OPENSC4_DETOUR(addr::cSC4PowerPoleOccupant_GetRecordCount, GetRecordCount),
    OPENSC4_DETOUR(addr::cSC4PowerPoleOccupant_GetRecordTarget, GetRecordTarget),
    OPENSC4_DETOUR(addr::cSC4PowerPoleOccupant_GetCellPair, GetCellPair),
    OPENSC4_DETOUR(addr::cSC4PowerPoleOccupant_IsFlag112, IsFlag112),
    OPENSC4_DETOUR(addr::cSC4PowerPoleOccupant_GetByte118, GetByte118),
    OPENSC4_DETOUR(addr::cSC4PowerPoleOccupant_SetByte118, SetByte118),
    OPENSC4_DETOUR(addr::cSC4PowerPoleOccupant_FindRecord, FindRecord),
};

bool InstallSave(registry::Mode) { return InstallDetours(kSave, "save"); }
bool InstallLife(registry::Mode) { return InstallDetours(kLife, "life"); }
bool InstallObject(registry::Mode) { return InstallDetours(kObject, "object"); }
bool InstallSlots(registry::Mode) { return InstallDetours(kSlots, "slots"); }

// simulators.powerpoleocc.own: the copies, taken at the first construction or
// destruction that wants them, so after every PostAppInit replacement. Each is
// the game's table word for word. An entry that holds a function of the object
// group directly would become ours without the detour; none of this class's
// does (the deleting destructor is behind thunks only), so the count logged is
// 0. Every other entry stays the game's address or adjustor thunk, which the
// other groups' detours still decide.
void BuildOwnVtables()
{
    g_vtable.Build(reinterpret_cast<const void*>(addr::cSC4PowerPoleOccupant_vftable));
    g_vtable4.Build(reinterpret_cast<const void*>(addr::cSC4PowerPoleOccupant_vftable_4));
    g_vtable8.Build(reinterpret_cast<const void*>(addr::cSC4PowerPoleOccupant_vftable_8));
    g_vtableC.Build(reinterpret_cast<const void*>(addr::cSC4PowerPoleOccupant_vftable_C));
    g_vtable10.Build(reinterpret_cast<const void*>(addr::cSC4PowerPoleOccupant_vftable_10));
    g_vtable24.Build(reinterpret_cast<const void*>(addr::cSC4PowerPoleOccupant_vftable_24));
    g_vtable3C.Build(reinterpret_cast<const void*>(addr::cSC4PowerPoleOccupant_vftable_3C));
    g_vbaseVtable.Build(reinterpret_cast<const void*>(addr::cSC4PowerPoleOccupant_vbase_vftable));
    unsigned redirected = 0;
    for (const Detour& detour : kObject) {
        if (Skipped(detour.address))
            continue;
        const void* const game = reinterpret_cast<const void*>(detour.address);
        redirected += occ_vtables::RedirectAll(game, detour.ours, g_vtable, g_vtable4, g_vtable8, g_vtableC, g_vtable10,
                                               g_vtable24, g_vtable3C, g_vbaseVtable);
    }
    log::Write("powerpoleocc own: vtables copied (19, 4, 34, 6, 25, 5, 1 and 19 slots), %u entries redirected",
               redirected);
}

// simulators.powerpoleocc.own installs nothing: it only says which tables the
// object group's constructor and destructor store.
bool InstallOwn(registry::Mode)
{
    g_own = true;
    return true;
}

const bool g_registered[] = {
    registry::Register({"simulators.powerpoleocc.save", "simulators/powerpoleocc", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4PowerPoleOccupant_Write,
                        "Our cSC4PowerPoleOccupant save record: Write, Read (version 5; 4 and up read) and "
                        "GetGZCLSID (3 detours; src/core/powerpoleocc_sim.cpp)",
                        false, InstallSave}),
    registry::Register({"simulators.powerpoleocc.life", "simulators/powerpoleocc", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4PowerPoleOccupant_QueryInterface,
                        "Our cSC4PowerPoleOccupant QueryInterface, the occupant part's Init and the message "
                        "handlers (4 detours; src/core/powerpoleocc_sim.cpp)",
                        false, InstallLife}),
    registry::Register({"simulators.powerpoleocc.object", "simulators/powerpoleocc", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4PowerPoleOccupant_cSC4PowerPoleOccupant,
                        "Our cSC4PowerPoleOccupant constructor, destructor, deleting destructor, pool allocation "
                        "and factory (5 detours; src/core/powerpoleocc_sim.cpp)",
                        false, InstallObject}),
    registry::Register({"simulators.powerpoleocc.slots", "simulators/powerpoleocc", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4PowerPoleOccupant_GetBoundingBox,
                        "Our cSC4PowerPoleOccupant bounding box, position and the small getters and setters "
                        "(9 detours; src/core/powerpoleocc_sim.cpp)",
                        false, InstallSlots}),
    registry::Register({"simulators.powerpoleocc.own", "simulators/powerpoleocc", registry::Kind::Api,
                        registry::Phase::PostAppInit, addr::cSC4PowerPoleOccupant_vftable,
                        "Our own cSC4PowerPoleOccupant: objects carry our copies of the class's eight vtables; only "
                        "has an effect with simulators.powerpoleocc.object (src/core/powerpoleocc_sim.cpp)",
                        false, InstallOwn}),
};

} // namespace
} // namespace opensc4::powerpoleocc_sim
