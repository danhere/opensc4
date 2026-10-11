// The record streams of the city save code (docs/save-format.md), part 3:
// 0x005DAD50 to 0x005DBC10, the typed helpers every Read and Write of the
// game calls for vectors, rows and reference lists, and the container
// helpers the row readers use. Replacement, off by default:
//
//   saves.streams.part3: vectors of floats, 32-bit values and bytes, rows of
//     bytes and of bits, a curve's points, the tract walk structure, lists
//     of references, and the helpers of a vector of bit rows (assign, copy,
//     fill, erase, insert, resize).
//
// Every helper takes the stream first and returns the stream pointer, not a
// bool; no caller reads the result. Counts are 32-bit and come first. The
// readers that check a count refuse 1,000,000 or more (10,000,000 for a
// reference list) and then leave their output as it was.
//
// A bit row is the game's vector of bools, 0x14 bytes: +0 the first word,
// +4 the first bit's number in it, +8 the word of the end, +0xC the bit
// number of the end, +0x10 the end of the storage. A byte row is a vector of
// bytes (0xC bytes: begin, end, end of storage).
//
// Still the game's: 0x005DB110 (copy constructor of a bit row, also called
// by the property reader 0x005FF000), 0x005DB590 (destroys a vector of bit
// rows, also called by the effects code at 0x0058A861, 0x0058BE25 and
// 0x0058C70E) and 0x005DB490 (resize of a vector of 32-bit values, called
// only by framework code at 0x0090A499). 0x005DBAD0 is not a function: it is
// the early exit of 0x005DB9B0.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace opensc4::saves {
namespace {

namespace ostream { // cIGZOStream, by vtable offset
constexpr std::size_t kSetUint8 = 0x14;
constexpr std::size_t kSetUint32 = 0x24;
constexpr std::size_t kSetFloat32 = 0x30;
} // namespace ostream

namespace istream { // cIGZIStream, by vtable offset
constexpr std::size_t kGetSint8 = 0x10;
constexpr std::size_t kGetUint8 = 0x14;
constexpr std::size_t kGetUint16 = 0x1C;
constexpr std::size_t kGetSint32 = 0x20;
constexpr std::size_t kGetUint32 = 0x24;
constexpr std::size_t kGetFloat32 = 0x30;
} // namespace istream

constexpr uint32_t kCountLimit = 1000000;
constexpr uint32_t kListLimit = 10000000;
constexpr uint32_t kUnknownIid = 0x26BB767E; // what a list's references are asked for
constexpr uint32_t kRowSize = 0x14;

template <typename T>
struct GameVector {
    T* begin;
    T* end;
    T* capacity;
};
static_assert(sizeof(GameVector<uint8_t>) == 0xC);

using ByteVector = GameVector<uint8_t>;

struct BitRow {
    uint32_t* start;
    uint32_t startBit;
    uint32_t* finish;
    uint32_t finishBit;
    uint32_t* endOfStorage;
};
static_assert(sizeof(BitRow) == kRowSize);

using RowVector = GameVector<BitRow>;

struct ListNode {
    ListNode* next;
    ListNode* previous;
    void* value;
};

template <typename R, typename... Args>
R This(std::uintptr_t function, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(function)(self, args...);
}

template <typename R, typename... Args>
R Cdecl(std::uintptr_t function, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(function)(args...);
}

void Free(void* memory)
{
    Cdecl<void>(addr::SC4_Free, memory);
}

// The distance between two pointers in bytes, as the signed 32-bit value the
// original divides.
template <typename T>
int32_t Bytes(const T* from, const T* to)
{
    return static_cast<int32_t>(reinterpret_cast<std::uintptr_t>(to) - reinterpret_cast<std::uintptr_t>(from));
}

BitRow* Rows(BitRow* row, uint32_t count)
{
    return reinterpret_cast<BitRow*>(reinterpret_cast<char*>(row) + count * kRowSize);
}

// The word and bit `offset` bits after the start of `words`, as the
// original's signed arithmetic makes them.
void BitPlace(uint32_t* words, int32_t offset, uint32_t*& word, int32_t& bit)
{
    word = words + offset / 32;
    bit = offset % 32;
    if (bit < 0) {
        bit += 32;
        --word;
    }
}

// A float that goes through the x87 stack on its way (fld, fstp): a
// signalling not-a-number comes out quiet.
uint32_t ThroughX87(uint32_t bits)
{
    if ((bits & 0x7F800000) == 0x7F800000 && (bits & 0x007FFFFF) != 0)
        bits |= 0x00400000;
    return bits;
}

uint32_t Address(void* pointer)
{
    return static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(pointer));
}

// The game's 0x005DAD50: writes a vector of floats: the count, then each
// value.
void* __cdecl WriteFloatVector(void* stream, GameVector<uint32_t>* vector)
{
    Virtual<uint8_t>(stream, ostream::kSetUint32, Bytes(vector->begin, vector->end) >> 2);
    const int32_t count = Bytes(vector->begin, vector->end) >> 2;
    for (int32_t i = 0; i < count; ++i)
        Virtual<uint8_t>(stream, ostream::kSetFloat32, vector->begin[i]);
    return stream;
}

// The game's 0x005DADA0: writes a vector of bytes: the count, then the
// bytes.
void* __cdecl WriteByteVector(void* stream, ByteVector* vector)
{
    Virtual<uint8_t>(stream, ostream::kSetUint32, Bytes(vector->begin, vector->end));
    const int32_t count = Bytes(vector->begin, vector->end);
    for (int32_t i = 0; i < count; ++i)
        Virtual<uint8_t>(stream, ostream::kSetUint8, uint32_t{vector->begin[i]});
    return stream;
}

// The game's 0x005DADE0: writes a vector of byte rows: the number of rows,
// the first row's length (0 without rows), then of every row as many bytes
// as there are rows, whatever the row's length: it takes the rows for a
// square.
void* __cdecl WriteByteRows(void* stream, GameVector<ByteVector>* rows)
{
    Virtual<uint8_t>(stream, ostream::kSetUint32, Bytes(rows->begin, rows->end) / 12);
    ByteVector* const first = rows->begin;
    int32_t width = 0;
    if (Bytes(first, rows->end) / 12 != 0)
        width = Bytes(first->begin, first->end);
    Virtual<uint8_t>(stream, ostream::kSetUint32, width);
    const int32_t count = Bytes(rows->begin, rows->end) / 12;
    if (count > 0) {
        uint32_t offset = 0;
        int32_t left = count;
        do {
            int32_t i = 0;
            do {
                const uint8_t* const row = *reinterpret_cast<uint8_t**>(reinterpret_cast<char*>(rows->begin) + offset);
                Virtual<uint8_t>(stream, ostream::kSetUint8, uint32_t{row[i]});
                ++i;
            } while (i < count);
            offset += 0xC;
            --left;
        } while (left != 0);
    }
    return stream;
}

// The game's 0x005DAEA0: writes a vector of 32-bit values: the count, then
// each value.
void* __cdecl WriteUint32Vector(void* stream, GameVector<uint32_t>* vector)
{
    Virtual<uint8_t>(stream, ostream::kSetUint32, Bytes(vector->begin, vector->end) >> 2);
    const int32_t count = Bytes(vector->begin, vector->end) >> 2;
    for (int32_t i = 0; i < count; ++i)
        Virtual<uint8_t>(stream, ostream::kSetUint32, vector->begin[i]);
    return stream;
}

// The game's 0x005DAEF0: a bit row takes another's bits. New storage only
// when the other holds more bits than this one has room for.
BitRow* __fastcall AssignBitRow(BitRow* self, void*, BitRow* other)
{
    // The original passes the address of its argument's slot as the copy's
    // unused sixth argument.
    void* tag = other;
    if (other != self) {
        const uint32_t size =
            (static_cast<uint32_t>(Bytes(other->start, other->finish) >> 2) << 5) - other->startBit + other->finishBit;
        const uint32_t room =
            (static_cast<uint32_t>(Bytes(self->start, self->endOfStorage) >> 2) << 5) - self->startBit;
        if (size > room) {
            if (self->start)
                Free(self->start);
            This<void>(addr::SC4BitVector_Initialize, self,
                       (static_cast<uint32_t>(Bytes(other->start, other->finish) >> 2) << 5) - other->startBit +
                           other->finishBit);
        }
        // The copy's own result (where it stopped) is dropped; the end is
        // worked out again below.
        uint32_t stopped[2];
        Cdecl<void*>(addr::SC4Save_F5DAAE0, &stopped[0], other->start, other->startBit, other->finish,
                     other->finishBit, self->start, self->startBit, &tag, uint32_t{0});
        const int32_t end = static_cast<int32_t>((static_cast<uint32_t>(Bytes(other->start, other->finish) >> 2) << 5) -
                                                 other->startBit + self->startBit + other->finishBit);
        uint32_t* word;
        int32_t bit;
        BitPlace(self->start, end, word, bit);
        self->finish = word;
        self->finishBit = static_cast<uint32_t>(bit);
    }
    return self;
}

// The game's 0x005DAFF0: writes a list of references: the number of nodes,
// then each node's object as a reference (8 bytes).
void* __cdecl WriteReferenceList(void* stream, ListNode** list)
{
    ListNode* const sentinel = *list;
    uint32_t count = 0;
    for (ListNode* node = sentinel->next; node != sentinel; node = node->next)
        ++count;
    Virtual<uint8_t>(stream, ostream::kSetUint32, count);
    for (ListNode* node = (*list)->next; node != *list; node = node->next)
        Cdecl<uint8_t>(addr::SC4Stream_WriteReference, stream, node->value);
    return stream;
}

// The game's 0x005DB040: gives every bit row of a range the bits of one.
void __cdecl FillRows(BitRow* first, BitRow* last, BitRow* value)
{
    for (; first != last; first = Rows(first, 1))
        AssignBitRow(first, nullptr, value);
}

// The game's 0x005DB070: assigns a range of bit rows to the rows from `out`
// on, first to last; returns the row after the last one written. The fourth
// and fifth arguments are not read.
BitRow* __cdecl CopyRows(BitRow* first, BitRow* last, BitRow* out, void*, uint32_t)
{
    int32_t count = Bytes(first, last) / 20;
    if (count <= 0)
        return out;
    do {
        AssignBitRow(out, nullptr, first);
        first = Rows(first, 1);
        out = Rows(out, 1);
        --count;
    } while (count != 0);
    return out;
}

// The game's 0x005DB0C0: assigns a range of bit rows to the rows that end at
// `outEnd`, last to first; returns the first row written. The fourth and
// fifth arguments are not read.
BitRow* __cdecl CopyRowsBackward(BitRow* first, BitRow* last, BitRow* outEnd, void*, uint32_t)
{
    int32_t count = Bytes(first, last) / 20;
    if (count <= 0)
        return outEnd;
    do {
        last = reinterpret_cast<BitRow*>(reinterpret_cast<char*>(last) - kRowSize);
        outEnd = reinterpret_cast<BitRow*>(reinterpret_cast<char*>(outEnd) - kRowSize);
        AssignBitRow(outEnd, nullptr, last);
        --count;
    } while (count != 0);
    return outEnd;
}

// The game's 0x005DB190: writes a curve's points: the count, then x and y
// of each as floats.
void* __cdecl WriteCurve(void* stream, void* curve)
{
    GameVector<uint32_t> points = {nullptr, nullptr, nullptr};
    This<void>(addr::SC4Curve_GetPoints, curve, &points);
    const uint32_t count = static_cast<uint32_t>(Bytes(points.begin, points.end) >> 3);
    Virtual<uint8_t>(stream, ostream::kSetUint32, count);
    for (uint32_t i = 0; i < count; ++i) {
        Virtual<uint8_t>(stream, ostream::kSetFloat32, points.begin[i * 2]);
        Virtual<uint8_t>(stream, ostream::kSetFloat32, points.begin[i * 2 + 1]);
    }
    if (points.begin)
        Free(points.begin);
    return stream;
}

// The game's 0x005DB220: makes copies of a range of bit rows in raw memory
// from `out` on; returns the end of the copies. The fourth argument is not
// read.
BitRow* __cdecl CopyRowsToRaw(BitRow* first, BitRow* last, BitRow* out, void*)
{
    for (; first != last; first = Rows(first, 1), out = Rows(out, 1)) {
        if (out)
            This<BitRow*>(addr::SC4Save_F5DB110, out, first);
    }
    return out;
}

// The game's 0x005DB250: makes `count` copies of a bit row in raw memory
// from `out` on; returns the end of the copies.
BitRow* __cdecl FillRawRows(BitRow* out, uint32_t count, BitRow* value)
{
    if (count > 0) {
        do {
            if (out)
                This<BitRow*>(addr::SC4Save_F5DB110, out, value);
            out = Rows(out, 1);
            --count;
        } while (count != 0);
    }
    return out;
}

// The game's 0x005DB280: reads the tract walk structure the simulators keep
// (0x30 bytes). The count of 16-bit values is not checked against a limit,
// and its variable is the stream argument's own slot: when the stream does
// not store a count, the vector is resized to the stream's address.
void* __cdecl ReadWalk(void* stream, char* walk)
{
    Virtual<uint8_t>(stream, istream::kGetSint8, walk);
    Virtual<uint8_t>(stream, istream::kGetSint8, walk + 1);
    Virtual<uint8_t>(stream, istream::kGetUint8, walk + 2);
    Virtual<uint8_t>(stream, istream::kGetSint32, walk + 4);
    Virtual<uint8_t>(stream, istream::kGetSint32, walk + 8);
    Virtual<uint8_t>(stream, istream::kGetSint32, walk + 0xC);
    Virtual<uint8_t>(stream, istream::kGetSint32, walk + 0x10);
    Virtual<uint8_t>(stream, istream::kGetSint32, walk + 0x14);
    uint32_t count = Address(stream);
    Virtual<uint8_t>(stream, istream::kGetUint32, &count);
    GameVector<uint16_t>* const values = reinterpret_cast<GameVector<uint16_t>*>(walk + 0x18);
    uint32_t zero = 0;
    This<void>(addr::SC4Uint16Vector_Resize, values, count, &zero);
    for (uint32_t i = 0; i < count; ++i)
        Virtual<uint8_t>(stream, istream::kGetUint16, values->begin + i);
    Virtual<uint8_t>(stream, istream::kGetSint32, walk + 0x24);
    Virtual<uint8_t>(stream, istream::kGetSint32, walk + 0x28);
    Virtual<uint8_t>(stream, istream::kGetSint32, walk + 0x2C);
    return stream;
}

// The game's 0x005DB350: reads a vector of 32-bit values. The count's
// variable is the stream argument's own slot, so a stream that stores no
// count leaves the stream's address there, which is over the limit.
void* __cdecl ReadUint32Vector(void* stream, GameVector<uint32_t>* vector)
{
    uint32_t count = Address(stream);
    Virtual<uint8_t>(stream, istream::kGetUint32, &count);
    if (count >= kCountLimit)
        return stream;
    uint32_t value = 0;
    This<void>(addr::SC4Uint32Vector_Resize, vector, count, &value);
    for (uint32_t i = 0; i < count; ++i) {
        Virtual<uint8_t>(stream, istream::kGetUint32, &value);
        vector->begin[i] = value;
    }
    return stream;
}

// The game's 0x005DB3C0: reads a list of references: empties the list, reads
// the count, then reads each reference and appends its object, null or not.
// The count's variable is not set before the read.
void* __cdecl ReadReferenceList(void* stream, ListNode** list)
{
    This<void>(addr::SC4List_Clear2, list);
    uint32_t count;
    Virtual<uint8_t>(stream, istream::kGetUint32, &count);
    if (count >= kListLimit || count == 0)
        return stream;
    do {
        void* object;
        --count;
        Cdecl<uint8_t>(addr::SC4Stream_ReadReference, stream, kUnknownIid, &object, uint32_t{0});
        ListNode* const sentinel = *list;
        void* const allocator = ListNodeAllocator();
        ListNode* const node = Virtual<ListNode*>(allocator, 0xC, uint32_t{0xC});
        node->value = object; // the original tests the value's address for null first
        ListNode* const previous = sentinel->previous;
        node->next = sentinel;
        node->previous = previous;
        previous->next = node;
        sentinel->previous = node;
    } while (count != 0);
    return stream;
}

// The game's 0x005DB4B0: reads a vector of floats. Each value goes through
// the x87 stack on its way into the vector. The count's variable is the
// stream argument's own slot, as in 0x005DB350.
void* __cdecl ReadFloatVector(void* stream, GameVector<uint32_t>* vector)
{
    uint32_t count = Address(stream);
    Virtual<uint8_t>(stream, istream::kGetUint32, &count);
    if (count >= kCountLimit)
        return stream;
    uint32_t value = 0;
    This<void>(addr::SC4FloatVector_Resize2, vector, count, &value);
    for (uint32_t i = 0; i < count; ++i) {
        Virtual<uint8_t>(stream, istream::kGetFloat32, &value);
        vector->begin[i] = ThroughX87(value);
    }
    return stream;
}

// The game's 0x005DB520: reads a vector of bytes. The count's variable is
// never set before the read: it is a slot the original makes with a push of
// ECX, so a stream that stores no count leaves the caller's ECX there (at
// every call site the address of the vector, which is over the limit).
void* __cdecl ReadByteVectorWith(void* stream, ByteVector* vector, uint32_t count)
{
    Virtual<uint8_t>(stream, istream::kGetUint32, &count);
    if (count >= kCountLimit)
        return stream;
    uint8_t value = 0;
    This<void>(addr::SC4ByteVector_Resize2, vector, count, &value);
    for (uint32_t i = 0; i < count; ++i) {
        Virtual<uint8_t>(stream, istream::kGetUint8, &value);
        vector->begin[i] = value;
    }
    return stream;
}

__declspec(naked) void ReadByteVector()
{
    __asm push ecx
    __asm push dword ptr [esp + 12]
    __asm push dword ptr [esp + 12]
    __asm call ReadByteVectorWith
    __asm add esp, 12
    __asm ret
}

// The game's 0x005DB5D0: reads a curve's points into a vector of its own and
// gives them to the curve. The count is not checked against a limit and its
// variable is the stream argument's own slot. The new points start as
// whatever the stack held (the value the insert copies is never set).
void* __cdecl ReadCurve(void* stream, void* curve)
{
    GameVector<uint32_t> points = {nullptr, nullptr, nullptr};
    uint32_t count = Address(stream);
    Virtual<uint8_t>(stream, istream::kGetUint32, &count);
    const uint32_t size = static_cast<uint32_t>(Bytes(points.begin, points.end) >> 3);
    if (count < size) {
        This<void>(addr::SC4Vector2Vector_EraseRange, &points, points.begin + count * 2, points.end);
    } else {
        uint32_t point[2];
        This<void>(addr::SC4Vector2Vector_InsertN, &points, points.end, count - size, &point[0]);
    }
    for (uint32_t i = 0; i < count; ++i) {
        Virtual<uint8_t>(stream, istream::kGetFloat32, points.begin + i * 2);
        Virtual<uint8_t>(stream, istream::kGetFloat32, points.begin + i * 2 + 1);
    }
    This<void>(addr::SC4Curve_SetPoints, curve, &points);
    if (points.begin)
        Free(points.begin);
    return stream;
}

// The game's 0x005DB690: takes the rows from `first` up to `last` out of a
// vector of bit rows: the rows after them move down by assignment and the
// rows left over at the end lose their storage. Returns `first`.
BitRow* __fastcall EraseRows(RowVector* self, void*, BitRow* first, BitRow* last)
{
    void* tag = first;
    BitRow* const newEnd = CopyRows(last, self->end, first, &tag, 0);
    BitRow* const end = self->end;
    for (BitRow* row = newEnd; row != end; row = Rows(row, 1)) {
        if (row->start)
            Free(row->start);
    }
    self->end = newEnd;
    return first;
}

// The game's 0x005DB6F0: puts `count` copies of a bit row in at `where` when
// the vector has no room for them: new storage for the size plus the larger
// of the size and the count, copies of the rows before `where`, the new
// rows, the rows after `where` unless `atEnd`, then the old rows and storage
// go. The third argument is not read.
void __fastcall InsertRowsGrowing(RowVector* self, void*, BitRow* where, BitRow* value, void*, uint32_t count,
                                  uint8_t atEnd)
{
    void* tag = nullptr;
    const uint32_t size = static_cast<uint32_t>(Bytes(self->begin, self->end) / 20);
    const uint32_t capacity = size + (size < count ? count : size);
    BitRow* storage = nullptr;
    if (capacity != 0)
        storage = Cdecl<BitRow*>(addr::SC4_stl_allocate, capacity * kRowSize);
    BitRow* out = storage;
    for (BitRow* row = self->begin; row != where; row = Rows(row, 1), out = Rows(out, 1)) {
        if (out)
            This<BitRow*>(addr::SC4Save_F5DB110, out, row);
    }
    if (count == 1) {
        if (out)
            This<BitRow*>(addr::SC4Save_F5DB110, out, value);
        out = Rows(out, 1);
    } else if (count > 0) {
        uint32_t left = count;
        do {
            if (out)
                This<BitRow*>(addr::SC4Save_F5DB110, out, value);
            out = Rows(out, 1);
            --left;
        } while (left != 0);
    }
    if (!atEnd)
        out = CopyRowsToRaw(where, self->end, out, &tag);
    This<void>(addr::SC4BitRowVector_Destroy, self);
    self->end = out;
    self->begin = storage;
    self->capacity = Rows(storage, capacity);
}

// The game's 0x005DB800: puts `count` copies of a bit row in at `where`.
// With room in the storage the rows after `where` move up (copies in the raw
// part, assignments in the rest) and the gap is filled from a copy of the
// value made first.
void __fastcall InsertRows(RowVector* self, void*, BitRow* where, uint32_t count, BitRow* value)
{
    void* tag = nullptr;
    if (count == 0)
        return;
    if (static_cast<uint32_t>(Bytes(self->end, self->capacity) / 20) < count) {
        InsertRowsGrowing(self, nullptr, where, value, &tag, count, 0);
        return;
    }
    BitRow copy;
    This<BitRow*>(addr::SC4Save_F5DB110, &copy, value);
    BitRow* const oldEnd = self->end;
    const uint32_t after = static_cast<uint32_t>(Bytes(where, self->end) / 20);
    if (after > count) {
        BitRow* const tail = reinterpret_cast<BitRow*>(reinterpret_cast<char*>(oldEnd) - count * kRowSize);
        CopyRowsToRaw(tail, oldEnd, oldEnd, &tag);
        self->end = Rows(self->end, count);
        CopyRowsBackward(where, tail, oldEnd, &tag, 0);
        FillRows(where, Rows(where, count), &copy);
    } else {
        const uint32_t extra = count - after;
        FillRawRows(oldEnd, extra, &copy);
        BitRow* const filledEnd = Rows(self->end, extra);
        self->end = filledEnd;
        CopyRowsToRaw(where, oldEnd, filledEnd, &tag);
        self->end = Rows(self->end, after);
        FillRows(where, oldEnd, &copy);
    }
    if (copy.start)
        Free(copy.start);
}

// The game's 0x005DB940: makes a vector of bit rows `count` long: erases
// the rows past the count or appends copies of `value`.
void __fastcall ResizeRows(RowVector* self, void*, uint32_t count, BitRow* value)
{
    BitRow* const end = self->end;
    BitRow* const begin = self->begin;
    if (count < static_cast<uint32_t>(Bytes(begin, end) / 20)) {
        EraseRows(self, nullptr, Rows(begin, count), end);
        return;
    }
    BitRow* const last = self->end;
    InsertRows(self, nullptr, last, count - static_cast<uint32_t>(Bytes(begin, last) / 20), value);
}

// The game's 0x005DB9B0: reads a vector of byte rows: the number of rows and
// the length of a row, then for every row that many bytes. Nothing is
// touched when either number is 1,000,000 or more. Neither number's variable
// is set before its read, nor the byte's.
void* __cdecl ReadByteRows(void* stream, GameVector<ByteVector>* rows)
{
    uint32_t count;
    uint32_t width;
    Virtual<uint8_t>(stream, istream::kGetUint32, &count);
    Virtual<uint8_t>(stream, istream::kGetUint32, &width);
    if (count >= kCountLimit || width >= kCountLimit)
        return stream;
    ByteVector empty = {nullptr, nullptr, nullptr};
    This<void>(addr::SC4RowVector_Resize, rows, count, &empty);
    uint32_t done = 0;
    if (count > 0) {
        uint8_t fill = 0;
        uint8_t value;
        uint32_t offset = 0;
        do {
            ByteVector* const row = reinterpret_cast<ByteVector*>(reinterpret_cast<char*>(rows->begin) + offset);
            uint8_t* const end = row->end;
            uint8_t* const begin = row->begin;
            const uint32_t size = static_cast<uint32_t>(end - begin);
            if (width < size)
                row->end = begin + width; // the erase of a tail of bytes: nothing to move
            else
                This<void>(addr::SC4ByteVector_InsertN, row, end, width - size, &fill);
            for (uint32_t i = 0; i < width; ++i) {
                Virtual<uint8_t>(stream, istream::kGetUint8, &value);
                uint8_t* const bytes = *reinterpret_cast<uint8_t**>(reinterpret_cast<char*>(rows->begin) + offset);
                bytes[i] = value;
            }
            ++done;
            offset += 0xC;
        } while (done < count);
    }
    return stream;
}

// The game's 0x005DBAE0: reads a vector of bit rows: the number of rows and
// the bits in a row, then one byte for every bit (set when not zero).
// Nothing is touched when either number is 1,000,000 or more. Neither
// number's variable is set before its read; the byte's variable is the low
// byte of the stream argument's own slot.
void* __cdecl ReadBitRows(void* stream, RowVector* rows)
{
    uint32_t count;
    uint32_t width;
    Virtual<uint8_t>(stream, istream::kGetUint32, &count);
    Virtual<uint8_t>(stream, istream::kGetUint32, &width);
    if (count >= kCountLimit || width >= kCountLimit)
        return stream;
    BitRow empty = {nullptr, 0, nullptr, 0, nullptr};
    ResizeRows(rows, nullptr, count, &empty);
    uint32_t done = 0;
    if (count > 0) {
        uint8_t value = static_cast<uint8_t>(Address(stream));
        uint32_t offset = 0;
        do {
            This<void>(addr::SC4BitVector_Resize, reinterpret_cast<char*>(rows->begin) + offset, width, uint32_t{0});
            for (uint32_t i = 0; i < width; ++i) {
                Virtual<uint8_t>(stream, istream::kGetUint8, &value);
                BitRow* const row = reinterpret_cast<BitRow*>(reinterpret_cast<char*>(rows->begin) + offset);
                uint32_t* word;
                int32_t bit;
                BitPlace(row->start, static_cast<int32_t>(row->startBit + i), word, bit);
                const uint32_t mask = uint32_t{1} << bit;
                if (value != 0)
                    *word |= mask;
                else
                    *word &= ~mask;
            }
            ++done;
            offset += kRowSize;
        } while (done < count);
    }
    return stream;
}

bool InstallSaveStreams3(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[24] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4Save_F5DAD50, reinterpret_cast<void*>(&WriteFloatVector)},
        {addr::SC4Stream_WriteByteVector, reinterpret_cast<void*>(&WriteByteVector)},
        {addr::SC4Stream_WriteByteRows, reinterpret_cast<void*>(&WriteByteRows)},
        {addr::SC4Save_F5DAEA0, reinterpret_cast<void*>(&WriteUint32Vector)},
        {addr::SC4Save_F5DAEF0, reinterpret_cast<void*>(&AssignBitRow)},
        {addr::SC4Stream_WriteReferenceList, reinterpret_cast<void*>(&WriteReferenceList)},
        {addr::SC4Save_F5DB040, reinterpret_cast<void*>(&FillRows)},
        {addr::SC4Save_F5DB070, reinterpret_cast<void*>(&CopyRows)},
        {addr::SC4Save_F5DB0C0, reinterpret_cast<void*>(&CopyRowsBackward)},
        {addr::SC4Save_F5DB190, reinterpret_cast<void*>(&WriteCurve)},
        {addr::SC4Save_F5DB220, reinterpret_cast<void*>(&CopyRowsToRaw)},
        {addr::SC4Save_F5DB250, reinterpret_cast<void*>(&FillRawRows)},
        {addr::SC4Stream_ReadPollutionStruct, reinterpret_cast<void*>(&ReadWalk)},
        {addr::SC4Save_F5DB350, reinterpret_cast<void*>(&ReadUint32Vector)},
        {addr::SC4Stream_ReadReferenceList, reinterpret_cast<void*>(&ReadReferenceList)},
        {addr::SC4Save_F5DB4B0, reinterpret_cast<void*>(&ReadFloatVector)},
        {addr::SC4Stream_ReadByteVector, reinterpret_cast<void*>(&ReadByteVector)},
        {addr::SC4Save_F5DB5D0, reinterpret_cast<void*>(&ReadCurve)},
        {addr::SC4BitRowVector_Erase, reinterpret_cast<void*>(&EraseRows)},
        {addr::SC4Save_F5DB6F0, reinterpret_cast<void*>(&InsertRowsGrowing)},
        {addr::SC4BitRowVector_InsertN, reinterpret_cast<void*>(&InsertRows)},
        {addr::SC4Save_F5DB940, reinterpret_cast<void*>(&ResizeRows)},
        {addr::SC4Stream_ReadByteRows, reinterpret_cast<void*>(&ReadByteRows)},
        {addr::SC4Stream_ReadBitRows, reinterpret_cast<void*>(&ReadBitRows)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    static_assert(count <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < count; ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].first);
        if (!found || !hook::Install(*found, hooks[i].second, &originals[i]))
            return false;
    }
    return true;
}

const bool g_saveStreams3Registered = registry::Register({
    "saves.streams.part3",
    "saves",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Stream_ReadReferenceList,
    "Our record stream helpers, part 3: vectors, byte and bit rows, curves, reference lists, bit row vectors",
    false,
    InstallSaveStreams3,
});

} // namespace
} // namespace opensc4::saves
