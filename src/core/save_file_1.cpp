// The write path of the framework's packed file (the container behind every
// saved city; docs/resources.md 'Package format', docs/save-format.md),
// part 1: the record a caller writes into, and the typed set calls every
// record kind inherits. Replacement, off by default:
//
//   saves.file.part1: the write record (kind 3, vtable 0x00AD8628): position
//     and seeks (0x0096DBA0 to 0x0096DC2D), size, read back, the write-out
//     into the segment's file, discard, destructors, the buffer's reserve,
//     both constructors, SetSize and SetFieldVoid; and the record base's
//     typed sets (0x00975AFF to 0x00976FC8): 8 to 64-bit values and arrays,
//     floats, strings, resource keys, variants, and the output stream part's
//     slots that forward to them.
//
// The ten adjustor thunks of the record's secondary vtables (0x0096F3AD to
// 0x0096F3E5, 0x00971981, 0x00971989: eight bytes each, `sub ecx, n` and a
// jump) stay the game's: they jump to the functions replaced here, so ours
// runs behind them, and a patch on eight bytes that are mostly a jump buys
// nothing.
//
// A record is one object with four vtables: +0x00 the serial record (64
// slots for the write record), +0x04 the record interface, +0x1C an output
// stream part, +0x2C an input stream part. +0x0A is the access word (3 for a
// record being written, 0 once discarded: every call then answers 0),
// +0x0C the key (type, group, instance), +0x18 the segment (the
// cIGZPersistDBSegment part, which is the packed file + 4; the record calls
// the packed file's slots 44 ReadAt and 46 WriteAt on that pointer - 4),
// +0x3C a byte: 1 keeps 16, 32 and 64-bit values as they are in memory, 0
// swaps their bytes. The write record adds +0x44 a vector of bytes (begin,
// end, end of storage), +0x50 the position, +0x54 a temporary file once the
// record has 2,000,000 bytes, +0x58 a byte: the move to a file was tried.
//
// Every typed set ends in the record's SetFieldVoid (slot 51, offset 0xCC),
// called virtually as the original does.
//
// Kept from the original as it is: the constructor's copy of a large
// existing record into the temporary file counts from the record's offset
// in the file up to its size, so it copies size - offset bytes (none when
// the offset is past the size); SetFieldVoid removes the temporary file by
// path even when it never opened, the other three clean-ups only when it is
// open; the second write of the move to a file is not checked.
//
// 0x00970F0F (SetFieldVoid) is also hooked by diagnostics.prop_pox: the two
// cannot be on together.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace opensc4::saves {
namespace {

namespace record { // offsets in a record
constexpr std::size_t kRecordPart = 0x04;
constexpr std::size_t kAccess = 0x0A;
constexpr std::size_t kKey = 0x0C;
constexpr std::size_t kSegment = 0x18;
constexpr std::size_t kOStreamPart = 0x1C;
constexpr std::size_t kIStreamPart = 0x2C;
constexpr std::size_t kNativeOrder = 0x3C;
constexpr std::size_t kBuffer = 0x44;
constexpr std::size_t kPosition = 0x50;
constexpr std::size_t kTempFile = 0x54;
constexpr std::size_t kSpillTried = 0x58;
} // namespace record

namespace slot { // the record's main vtable, by offset
constexpr std::size_t kSetFieldUint8Array = 0x70;
constexpr std::size_t kSetFieldUint8 = 0x74;
constexpr std::size_t kSetFieldSint8Array = 0x78;
constexpr std::size_t kSetFieldSint8 = 0x7C;
constexpr std::size_t kSetFieldUint16Array = 0x80;
constexpr std::size_t kSetFieldUint16 = 0x84;
constexpr std::size_t kSetFieldSint16Array = 0x88;
constexpr std::size_t kSetFieldSint16 = 0x8C;
constexpr std::size_t kSetFieldUint32Array = 0x90;
constexpr std::size_t kSetFieldUint32 = 0x94;
constexpr std::size_t kSetFieldSint32Array = 0x98;
constexpr std::size_t kSetFieldSint32 = 0x9C;
constexpr std::size_t kSetFieldUint64Array = 0xA0;
constexpr std::size_t kSetFieldUint64 = 0xA4;
constexpr std::size_t kSetFieldSint64Array = 0xA8;
constexpr std::size_t kSetFieldSint64 = 0xAC;
constexpr std::size_t kSetFieldFloat32Array = 0xB0;
constexpr std::size_t kSetFieldFloat32 = 0xB4;
constexpr std::size_t kSetFieldFloat64Array = 0xB8;
constexpr std::size_t kSetFieldFloat64 = 0xBC;
constexpr std::size_t kSetFieldString = 0xC0;
constexpr std::size_t kSetFieldVoid = 0xCC;
constexpr std::size_t kGetPosition = 0xD8;
constexpr std::size_t kSeekAbsolute = 0xDC;
} // namespace slot

namespace packed { // the packed file's main vtable, by offset
constexpr std::size_t kReadAt = 0xB0;  // (buffer, position, size)
constexpr std::size_t kWriteAt = 0xB8; // (buffer, position, size)
} // namespace packed

namespace file { // the game's file object (vtable 0x00ABFED0), by offset
constexpr std::size_t kOpen = 0x0C; // (access, creation, share)
constexpr std::size_t kIsOpen = 0x10;
constexpr std::size_t kClose = 0x14;
constexpr std::size_t kPosition = 0x18;
constexpr std::size_t kLength = 0x1C;
constexpr std::size_t kSetLength = 0x20;
constexpr std::size_t kSeek = 0x30; // (position) from the start; answers the new position
constexpr std::size_t kRead = 0x38; // (buffer, size)
constexpr std::size_t kWrite = 0x40; // (buffer, size)
constexpr std::size_t kPath = 0x60;
constexpr std::size_t kDelete = 0x80; // (flags)
} // namespace file

namespace string { // cIGZString, by offset
constexpr std::size_t kToChar = 0x18;
constexpr std::size_t kStrlen = 0x1C;
} // namespace string

namespace variant { // cIGZVariant, by offset
constexpr std::size_t kGetType = 0x0C;
constexpr std::size_t kGetCount = 0x10;
constexpr std::size_t kBool = 0xF8;
constexpr std::size_t kUint8 = 0x100;
constexpr std::size_t kSint8 = 0x108;
constexpr std::size_t kUint16 = 0x110;
constexpr std::size_t kSint16 = 0x118;
constexpr std::size_t kUint32 = 0x120;
constexpr std::size_t kSint32 = 0x128;
constexpr std::size_t kUint64 = 0x130;
constexpr std::size_t kSint64 = 0x138;
constexpr std::size_t kFloat32 = 0x140;
constexpr std::size_t kFloat64 = 0x148;
constexpr std::size_t kUnicodeChar = 0x158;
constexpr std::size_t kChar = 0x160;
constexpr std::size_t kRefBool = 0x174;
constexpr std::size_t kRefUint8 = 0x17C;
constexpr std::size_t kRefSint8 = 0x184;
constexpr std::size_t kRefUint16 = 0x18C;
constexpr std::size_t kRefSint16 = 0x194;
constexpr std::size_t kRefUint32 = 0x19C;
constexpr std::size_t kRefSint32 = 0x1A4;
constexpr std::size_t kRefUint64 = 0x1AC;
constexpr std::size_t kRefSint64 = 0x1B4;
constexpr std::size_t kRefFloat32 = 0x1BC;
constexpr std::size_t kRefFloat64 = 0x1C4;
constexpr std::size_t kRefCharA = 0x1CC;
constexpr std::size_t kRefUnicodeChar = 0x1D4;
constexpr std::size_t kRefCharB = 0x1DC;
constexpr std::size_t kRefVoid = 0x1EC;
} // namespace variant

constexpr uint32_t kSpillSize = 2000000; // 0x1E8480
constexpr uint32_t kChunk = 0x4000;
constexpr uint32_t kFileObjectSize = 0x80;
constexpr uint32_t kArrayOnStack = 0x100; // items an array set swaps on the stack

struct ByteVector {
    uint8_t* begin;
    uint8_t* end;
    uint8_t* capacity;
};
static_assert(sizeof(ByteVector) == 0xC);

struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};
static_assert(sizeof(GameString) == 0x14);

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

void* New(uint32_t size)
{
    return Cdecl<void*>(addr::SC4_operator_new, size);
}

void Delete(void* memory)
{
    Cdecl<void>(addr::SC4_operator_delete, memory);
}

void Copy(void* to, const void* from, uint32_t size)
{
    Cdecl<void*>(addr::CRT_memcpy, to, from, size);
}

// The inlined destructor of a string on the stack.
void DestroyString(GameString& text)
{
    char* buffer = text.begin;
    text.vtable = addr::SC4String_Vtable;
    if (buffer)
        Free(buffer);
}

ByteVector& Buffer(char* self)
{
    return At<ByteVector>(self, record::kBuffer);
}

uint32_t Used(const ByteVector& buffer)
{
    return static_cast<uint32_t>(buffer.end - buffer.begin);
}

// The packed file a record belongs to: the segment pointer less 4, or null.
void* PackedFile(char* self)
{
    char* segment = At<char*>(self, record::kSegment);
    return segment ? segment - 4 : nullptr;
}

void SetVtables(char* self)
{
    At<std::uintptr_t>(self, 0) = addr::cGZDBWriteRecord_Vtable;
    At<std::uintptr_t>(self, record::kRecordPart) = addr::cGZDBWriteRecord_RecordVtable;
    At<std::uintptr_t>(self, record::kOStreamPart) = addr::cGZDBWriteRecord_OStreamVtable;
    At<std::uintptr_t>(self, record::kIStreamPart) = addr::cGZDBWriteRecord_IStreamVtable;
}

uint8_t SetVoid(char* self, const void* data, uint32_t size)
{
    return Virtual<uint8_t>(self, slot::kSetFieldVoid, data, size);
}

// Closes and removes the temporary file when it is open, deletes the file
// object and clears the pointer (the constructor, Discard, the destructor).
void DropTempFile(char* self)
{
    if (Virtual<uint8_t>(At<void*>(self, record::kTempFile), file::kIsOpen)) {
        Virtual<uint8_t>(At<void*>(self, record::kTempFile), file::kClose);
        void* path = Virtual<void*>(At<void*>(self, record::kTempFile), file::kPath);
        Cdecl<uint8_t>(addr::RZFile_Remove, path);
    }
    if (void* temp = At<void*>(self, record::kTempFile))
        Virtual<void*>(temp, file::kDelete, uint32_t{1});
    At<void*>(self, record::kTempFile) = nullptr;
}

// Makes the temporary file's name ('Record_<type>_' and what the game's
// temporary name maker adds) and its file object, stored at +0x54. False
// when no name could be made; the caller opens the file and destroys `name`.
bool MakeTempFile(char* self, GameString& name, uint32_t type)
{
    const char* format = reinterpret_cast<const char*>(addr::cGZDBWriteRecord_kTempNameFormat);
    Cdecl<int32_t>(addr::cRZBaseString_Sprintf, &name, format, type);
    if (!Cdecl<uint8_t>(addr::RZFile_MakeTempName, &name, static_cast<void*>(nullptr)))
        return false;
    void* memory = New(kFileObjectSize);
    void* temp = memory ? This<void*>(addr::cRZFile_Construct, memory, &name) : nullptr;
    At<void*>(self, record::kTempFile) = temp;
    return true;
}

// The game's 0x0096DBA0: slot 54, GetPosition.
uint32_t __fastcall GetPosition(char* self, void*)
{
    if (At<uint16_t>(self, record::kAccess) == 0)
        return 0;
    if (At<void*>(self, record::kTempFile))
        return Virtual<uint32_t>(At<void*>(self, record::kTempFile), file::kPosition);
    return At<uint32_t>(self, record::kPosition);
}

// The game's 0x0096DBBC: slot 55, SeekAbsolute. In memory the position is
// stored unchecked.
uint8_t __fastcall SeekAbsolute(char* self, void*, uint32_t position)
{
    if (At<uint16_t>(self, record::kAccess) == 0)
        return 0;
    if (void* temp = At<void*>(self, record::kTempFile))
        return Virtual<uint32_t>(temp, file::kSeek, position) == position ? 1 : 0;
    At<uint32_t>(self, record::kPosition) = position;
    return 1;
}

// The game's 0x0096DBF0: slot 56, SeekRelative. The sum is made in 64 bits
// and refused when negative; its low half goes to SeekAbsolute.
uint8_t __fastcall SeekRelative(char* self, void*, int32_t offset)
{
    const uint32_t position = Virtual<uint32_t>(self, slot::kGetPosition);
    const int64_t sum = static_cast<int64_t>(static_cast<uint64_t>(position)) + static_cast<int64_t>(offset);
    if (sum < 0)
        return 0;
    return Virtual<uint8_t>(self, slot::kSeekAbsolute, static_cast<uint32_t>(sum));
}

// The game's 0x0096E91B: slot 52, GetSize.
uint32_t __fastcall GetSize(char* self, void*)
{
    if (At<uint16_t>(self, record::kAccess) == 0)
        return 0;
    if (At<void*>(self, record::kTempFile))
        return Virtual<uint32_t>(At<void*>(self, record::kTempFile), file::kLength);
    return Used(Buffer(self));
}

// The game's 0x0096EB2A: slot 27, GetFieldVoid: reads back what was written.
uint8_t __fastcall GetFieldVoid(char* self, void*, void* out, uint32_t size)
{
    if (At<uint16_t>(self, record::kAccess) == 0)
        return 0;
    if (void* temp = At<void*>(self, record::kTempFile))
        return Virtual<uint8_t>(temp, file::kRead, out, size);
    const ByteVector& buffer = Buffer(self);
    const uint32_t position = At<uint32_t>(self, record::kPosition);
    if (position + size > Used(buffer))
        return 0;
    Copy(out, buffer.begin + position, size);
    At<uint32_t>(self, record::kPosition) += size;
    return 1;
}

// The game's 0x0096EB78: slot 63, the write-out of the record's bytes into
// the segment's file at `position`: from the temporary file in pieces of
// 0x4000 (its own position is put back after), else from the buffer in one
// call, never more than the buffer holds.
uint8_t __fastcall WriteOut(char* self, void*, uint32_t position, uint32_t size)
{
    uint8_t ok = 0;
    if (At<uint16_t>(self, record::kAccess) == 0)
        return ok;
    if (!At<void*>(self, record::kTempFile)) {
        void* owner = PackedFile(self);
        const ByteVector& buffer = Buffer(self);
        uint32_t count = size;
        if (count > Used(buffer))
            count = Used(buffer);
        return Virtual<uint8_t>(owner, packed::kWriteAt, buffer.begin, position, count);
    }
    const uint32_t saved = Virtual<uint32_t>(At<void*>(self, record::kTempFile), file::kPosition);
    uint32_t done = 0;
    void* chunk = New(kChunk);
    Virtual<uint32_t>(At<void*>(self, record::kTempFile), file::kSeek, uint32_t{0});
    ++ok;
    if (size > 0) {
        do {
            if (!ok)
                break;
            const uint32_t count = done + kChunk > size ? size - done : kChunk;
            ok = Virtual<uint8_t>(At<void*>(self, record::kTempFile), file::kRead, chunk, count);
            if (ok) {
                ok = Virtual<uint8_t>(PackedFile(self), packed::kWriteAt, chunk, position, count);
                done += count;
                position += count;
            }
        } while (done < size);
    }
    Delete(chunk);
    Virtual<uint32_t>(At<void*>(self, record::kTempFile), file::kSeek, saved);
    return ok;
}

// The game's 0x0096F33D: the destructor.
void __fastcall Destruct(char* self, void*)
{
    void* temp = At<void*>(self, record::kTempFile);
    SetVtables(self);
    if (temp) {
        if (Virtual<uint8_t>(temp, file::kIsOpen)) {
            Virtual<uint8_t>(At<void*>(self, record::kTempFile), file::kClose);
            void* path = Virtual<void*>(At<void*>(self, record::kTempFile), file::kPath);
            Cdecl<uint8_t>(addr::RZFile_Remove, path);
        }
        if (void* again = At<void*>(self, record::kTempFile))
            Virtual<void*>(again, file::kDelete, uint32_t{1});
        At<void*>(self, record::kTempFile) = nullptr;
    }
    if (uint8_t* begin = Buffer(self).begin)
        Free(begin);
    This<void>(addr::cGZPersistDBSerialRecord_Destruct, self);
}

// The game's 0x0096F6E9: the deleting destructor: the record goes back to
// its pool's free list (0x00B17A20).
char* __fastcall DeleteRecord(char* self, void*, uint32_t flags)
{
    Destruct(self, nullptr);
    if ((flags & 1) != 0 && self) {
        At<void*>(self, 0) = Global<void*>(addr::cGZDBWriteRecord_FreeList);
        Global<void*>(addr::cGZDBWriteRecord_FreeList) = self;
    }
    return self;
}

// The game's 0x0096F70F: slot 62, Discard.
void __fastcall Discard(char* self, void*)
{
    void* segment = At<void*>(self, record::kSegment);
    At<uint16_t>(self, record::kAccess) = 0;
    if (segment) {
        Virtual<uint32_t>(segment, 0x8);
        At<void*>(self, record::kSegment) = nullptr;
    }
    if (At<void*>(self, record::kTempFile))
        DropTempFile(self);
    ByteVector& buffer = Buffer(self);
    This<uint8_t*>(addr::cSC4Advisor_F40E490, &buffer, buffer.begin, buffer.end); // erase(begin, end)
    At<uint32_t>(self, record::kPosition) = 0;
    At<uint8_t>(self, record::kSpillTried) = 0;
}

// The game's 0x0096F6BE: a new block of `size` bytes holding a copy of
// [first, last). `this` is the vector.
uint8_t* __fastcall CopyToNewBlock(ByteVector* buffer, void*, uint32_t size, uint8_t* first, uint8_t* last)
{
    uint8_t* block = This<uint8_t*>(addr::SC4Allocator_Allocate, &buffer->capacity, size);
    uint8_t tag = 0;
    Cdecl<uint8_t*>(addr::SC4_CopyBytes, first, last, block, &tag);
    return block;
}

// The game's 0x00970080: the buffer's reserve. `this` is the vector.
void __fastcall Reserve(ByteVector* buffer, void*, uint32_t size)
{
    uint8_t* begin = buffer->begin;
    if (static_cast<uint32_t>(buffer->capacity - begin) >= size)
        return;
    uint8_t* end = buffer->end;
    const uint32_t used = static_cast<uint32_t>(end - begin);
    uint8_t* block;
    if (begin) {
        block = CopyToNewBlock(buffer, nullptr, size, begin, end);
        if (uint8_t* old = buffer->begin)
            Free(old);
    } else {
        block = This<uint8_t*>(addr::SC4Allocator_Allocate, &buffer->capacity, size);
    }
    buffer->end = block + used;
    buffer->begin = block;
    buffer->capacity = block + size;
}

// What both constructors do before they fill the buffer.
void ConstructEmpty(char* self, const uint32_t* key, char* owner, uint32_t nativeOrder)
{
    This<void*>(addr::cGZDBSegmentPackedFile_F96D859, self, uint32_t{3}, key,
                owner ? owner + 4 : static_cast<char*>(nullptr), nativeOrder);
    SetVtables(self);
    ByteVector& buffer = Buffer(self);
    buffer.begin = nullptr;
    buffer.end = nullptr;
    buffer.capacity = nullptr;
}

// The game's 0x00970C4D: the constructor over `size` bytes at `offset` of
// the packed file `owner` (both 0 for a new record). The last argument is
// the byte at +0x3C.
char* __fastcall Construct(char* self, void*, uint32_t offset, uint32_t size, const uint32_t* key, char* owner,
                           uint32_t nativeOrder)
{
    ConstructEmpty(self, key, owner, nativeOrder);
    ByteVector& buffer = Buffer(self);
    At<uint32_t>(self, record::kPosition) = 0;
    At<void*>(self, record::kTempFile) = nullptr;
    At<uint8_t>(self, record::kSpillTried) = 0;
    At<uint16_t>(self, record::kAccess) = 3;
    Reserve(&buffer, nullptr, 0x80);
    if (size == 0)
        return self;
    if (size > kSpillSize && At<uint8_t>(self, record::kSpillTried) == 0) {
        GameString name;
        This<void*>(addr::SC4String_SC4String, &name);
        At<uint8_t>(self, record::kSpillTried) = 1;
        if (MakeTempFile(self, name, key[0])) {
            uint8_t ok = 0;
            if (Virtual<uint8_t>(At<void*>(self, record::kTempFile), file::kOpen, uint32_t{3}, uint32_t{3},
                                 uint32_t{1})) {
                uint32_t at = offset;
                void* chunk = New(kChunk);
                void* source = PackedFile(self);
                Virtual<uint32_t>(At<void*>(self, record::kTempFile), file::kSeek, uint32_t{0});
                ok = 1;
                bool rewind = true;
                if (offset < size) {
                    do {
                        if (!ok)
                            break;
                        const uint32_t count = at + kChunk > size ? size - at : kChunk;
                        ok = Virtual<uint8_t>(source, packed::kReadAt, chunk, at, count);
                        if (ok) {
                            ok = Virtual<uint8_t>(At<void*>(self, record::kTempFile), file::kWrite, chunk, count);
                            at += count;
                        }
                    } while (at < size);
                    rewind = ok != 0;
                }
                if (rewind) {
                    Virtual<uint32_t>(At<void*>(self, record::kTempFile), file::kSeek,
                                      At<uint32_t>(self, record::kPosition));
                }
                Delete(chunk);
            }
            if (!ok)
                DropTempFile(self);
        }
        DestroyString(name);
    }
    if (!At<void*>(self, record::kTempFile)) {
        void* source = PackedFile(self);
        This<void>(addr::SC4ByteVector_ResizeZeroed, &buffer, size);
        Virtual<uint8_t>(source, packed::kReadAt, buffer.begin, offset, size);
    }
    return self;
}

// The game's 0x00970E67: the constructor over a copy of `size` bytes of
// memory (the old contents of a compressed record, decompressed).
char* __fastcall ConstructFromMemory(char* self, void*, const void* data, uint32_t size, const uint32_t* key,
                                     char* owner, uint32_t nativeOrder)
{
    ConstructEmpty(self, key, owner, nativeOrder);
    ByteVector& buffer = Buffer(self);
    At<uint32_t>(self, record::kPosition) = 0;
    At<void*>(self, record::kTempFile) = nullptr;
    At<uint8_t>(self, record::kSpillTried) = 0;
    At<uint16_t>(self, record::kAccess) = 3;
    This<void>(addr::SC4ByteVector_ResizeZeroed, &buffer, size);
    Copy(buffer.begin, data, size);
    return self;
}

// The game's 0x00970EE5: slot 53, SetSize.
uint8_t __fastcall SetSize(char* self, void*, uint32_t size)
{
    if (At<uint16_t>(self, record::kAccess) == 0)
        return 0;
    if (At<void*>(self, record::kTempFile))
        return Virtual<uint8_t>(At<void*>(self, record::kTempFile), file::kSetLength, size);
    This<void>(addr::SC4ByteVector_ResizeZeroed, &Buffer(self), size);
    return 1;
}

// The game's 0x00970F0F: slot 51, SetFieldVoid: every write into the record.
uint8_t __fastcall SetFieldVoid(char* self, void*, const uint8_t* data, uint32_t size)
{
    uint8_t result = 0;
    if (At<uint16_t>(self, record::kAccess) == 0)
        return result;
    if (void* temp = At<void*>(self, record::kTempFile))
        return Virtual<uint8_t>(temp, file::kWrite, data, size);
    const uint32_t reach = At<uint32_t>(self, record::kPosition) + size;
    const bool large = static_cast<int32_t>(reach) >= static_cast<int32_t>(kSpillSize);
    if (large && At<uint8_t>(self, record::kSpillTried) == 0) {
        // The record moves to a temporary file: what the buffer holds, then
        // the new bytes, whatever the position was.
        GameString name;
        This<void*>(addr::SC4String_SC4String, &name);
        At<uint8_t>(self, record::kSpillTried) = 1;
        if (MakeTempFile(self, name, At<uint32_t>(self, record::kKey))) {
            bool moved = false;
            if (Virtual<uint8_t>(At<void*>(self, record::kTempFile), file::kOpen, uint32_t{3}, uint32_t{3},
                                 uint32_t{1})) {
                Virtual<uint32_t>(At<void*>(self, record::kTempFile), file::kSeek, uint32_t{0});
                Virtual<uint8_t>(At<void*>(self, record::kTempFile), file::kSetLength, uint32_t{0});
                void* temp = At<void*>(self, record::kTempFile);
                ByteVector& buffer = Buffer(self);
                uint8_t* begin = buffer.begin;
                const uint32_t used = static_cast<uint32_t>(buffer.end - begin);
                if (Virtual<uint8_t>(temp, file::kWrite, begin, used)) {
                    Virtual<uint8_t>(At<void*>(self, record::kTempFile), file::kWrite, data, size);
                    This<void>(addr::SC4ByteVector_ResizeZeroed, &buffer, uint32_t{0});
                    At<uint32_t>(self, record::kPosition) = 0;
                    result = 1;
                    moved = true;
                }
            }
            if (!moved) {
                if (Virtual<uint8_t>(At<void*>(self, record::kTempFile), file::kIsOpen))
                    Virtual<uint8_t>(At<void*>(self, record::kTempFile), file::kClose);
                void* path = Virtual<void*>(At<void*>(self, record::kTempFile), file::kPath);
                Cdecl<uint8_t>(addr::RZFile_Remove, path);
                if (void* temp = At<void*>(self, record::kTempFile))
                    Virtual<void*>(temp, file::kDelete, uint32_t{1});
                At<void*>(self, record::kTempFile) = nullptr;
            }
        }
        DestroyString(name);
    }
    if (At<void*>(self, record::kTempFile))
        return result;
    const uint32_t position = At<uint32_t>(self, record::kPosition);
    ByteVector& buffer = Buffer(self);
    const uint32_t used = Used(buffer);
    if (position == used && size < 0x40) {
        for (uint32_t i = 0; i < size; ++i) {
            uint8_t value = data[i];
            This<void>(addr::SC4ByteVector_PushBack, &buffer, &value);
        }
    } else {
        if (reach > used) {
            uint8_t zero = 0;
            This<void>(addr::SC4ByteVector_Resize2, &buffer, position + size, &zero);
        }
        Copy(buffer.begin + At<uint32_t>(self, record::kPosition), data, size);
    }
    At<uint32_t>(self, record::kPosition) += size;
    return 1;
}

// The typed sets of the record base, inherited by every record kind.

// The game's 0x0097567E: the code a variant's type is stored as (table
// 0x00AD8C50 by the type's low 15 bits; the array bit 0x8000 is kept).
uint32_t __cdecl VariantTypeCode(uint32_t type)
{
    uint32_t code = Global<uint32_t>(addr::cGZPersistDBRecord_kVariantTypeCodes + (type & 0x7FFF) * 4);
    if ((type & 0x8000) != 0)
        code |= 0x8000;
    return code;
}

// The game's 0x00975AFF: slots 29 and 31, one byte.
uint8_t __fastcall SetFieldUint8(char* self, void*, uint32_t value)
{
    return SetVoid(self, &value, 1);
}

// The game's 0x00975B11: slots 28 and 30, bytes: a jump to SetFieldVoid.
uint8_t __fastcall SetFieldUint8Array(char* self, void*, const void* values, uint32_t count)
{
    return SetVoid(self, values, count);
}

// The game's 0x00975B19: slot 45, a float, never swapped.
uint8_t __fastcall SetFieldFloat32(char* self, void*, uint32_t bits)
{
    return SetVoid(self, &bits, 4);
}

// The game's 0x00975B2B: slot 44.
uint8_t __fastcall SetFieldFloat32Array(char* self, void*, const void* values, uint32_t count)
{
    return SetVoid(self, values, count << 2);
}

// The game's 0x00975B42: slot 47, a double, never swapped.
uint8_t __fastcall SetFieldFloat64(char* self, void*, uint64_t bits)
{
    return SetVoid(self, &bits, 8);
}

// The game's 0x00975B54: slot 46.
uint8_t __fastcall SetFieldFloat64Array(char* self, void*, const void* values, uint32_t count)
{
    return SetVoid(self, values, count << 3);
}

// The game's 0x00975B6B: slot 15 of the output stream part: SetFieldString.
uint8_t __fastcall OStreamSetString(char* self, void*, void* text)
{
    return Virtual<uint8_t>(self - record::kOStreamPart, slot::kSetFieldString, text);
}

// The game's 0x00975B84: slot 59: SetFieldString, answering the output
// stream part.
char* __fastcall SetFieldStringChained(char* self, void*, void* text)
{
    Virtual<uint8_t>(self, slot::kSetFieldString, text);
    return self + record::kOStreamPart;
}

// The game's 0x00975BB5: slot 49: a key's type, group and instance, stopping
// at the first that fails.
uint8_t __fastcall SetFieldResKey(char* self, void*, const uint32_t* key)
{
    if (!Virtual<uint8_t>(self, slot::kSetFieldUint32, key[0]) ||
        !Virtual<uint8_t>(self, slot::kSetFieldUint32, key[1]))
        return 0;
    return Virtual<uint8_t>(self, slot::kSetFieldUint32, key[2]);
}

// The game's 0x00975C10: slot 17 of the output stream part: SetFieldVoid.
uint8_t __fastcall OStreamSetVoid(char* self, void*, const void* data, uint32_t size)
{
    return SetVoid(self - record::kOStreamPart, data, size);
}

// The game's 0x00975C1B: slot 58: `count` items of `size` bytes.
uint8_t __fastcall SetFieldVoidSized(char* self, void*, const void* data, uint32_t size, uint32_t count)
{
    return SetVoid(self, data, size * count);
}

uint32_t Swap32(uint32_t value)
{
    return (value >> 24) | ((value >> 8) & 0x0000FF00) | ((value << 8) & 0x00FF0000) | (value << 24);
}

// The game's 0x0092B55B on a 64-bit value: its bytes in reverse order.
uint64_t Swap64(uint64_t value)
{
    return reinterpret_cast<uint64_t(__stdcall*)(uint64_t)>(addr::RZ_ByteSwap64)(value);
}

// The game's 0x00975E97: slots 33 and 35, 16 bits.
uint8_t __fastcall SetFieldUint16(char* self, void*, uint32_t value)
{
    if (At<uint8_t>(self, record::kNativeOrder) == 0)
        value = Swap32(value & 0xFFFF) >> 16;
    return SetVoid(self, &value, 2);
}

// The game's 0x00975ED0: slots 32 and 34. Swapped copies of up to 0x100
// items are made on the stack, of more in a block from the heap.
uint8_t __fastcall SetFieldUint16Array(char* self, void*, const uint8_t* values, uint32_t count)
{
    uint8_t local[kArrayOnStack * 2];
    if (At<uint8_t>(self, record::kNativeOrder) != 0)
        return SetVoid(self, values, count + count);
    void* block = count > kArrayOnStack ? New(count + count) : nullptr;
    uint8_t* swapped = block ? static_cast<uint8_t*>(block) : local;
    for (uint32_t i = 0; i < count; ++i) {
        uint16_t item;
        std::memcpy(&item, values + i * 2, 2);
        item = static_cast<uint16_t>(Swap32(static_cast<uint32_t>(item)) >> 16);
        std::memcpy(swapped + i * 2, &item, 2);
    }
    const uint8_t result = SetVoid(self, swapped, count + count);
    Delete(block);
    return result;
}

// The game's 0x00975F74: slots 37 and 39, 32 bits.
uint8_t __fastcall SetFieldUint32(char* self, void*, uint32_t value)
{
    if (At<uint8_t>(self, record::kNativeOrder) == 0)
        value = Swap32(value);
    return SetVoid(self, &value, 4);
}

// The game's 0x00975FA4: slots 36 and 38.
uint8_t __fastcall SetFieldUint32Array(char* self, void*, const uint8_t* values, uint32_t count)
{
    uint8_t local[kArrayOnStack * 4];
    if (At<uint8_t>(self, record::kNativeOrder) != 0)
        return SetVoid(self, values, count << 2);
    void* block = count > kArrayOnStack ? New(count << 2) : nullptr;
    uint8_t* swapped = block ? static_cast<uint8_t*>(block) : local;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t item;
        std::memcpy(&item, values + i * 4, 4);
        item = Swap32(item);
        std::memcpy(swapped + i * 4, &item, 4);
    }
    const uint8_t result = SetVoid(self, swapped, count << 2);
    Delete(block);
    return result;
}

// The game's 0x00976049: slots 41 and 43, 64 bits.
uint8_t __fastcall SetFieldUint64(char* self, void*, uint64_t value)
{
    uint64_t stored;
    if (At<uint8_t>(self, record::kNativeOrder) != 0)
        stored = value;
    else
        stored = Swap64(value);
    return SetVoid(self, &stored, 8);
}

// The game's 0x0097608B: slots 40 and 42.
uint8_t __fastcall SetFieldUint64Array(char* self, void*, const uint8_t* values, uint32_t count)
{
    uint8_t local[kArrayOnStack * 8];
    if (At<uint8_t>(self, record::kNativeOrder) != 0)
        return SetVoid(self, values, count << 3);
    void* block = count > kArrayOnStack ? New(count << 3) : nullptr;
    uint8_t* swapped = block ? static_cast<uint8_t*>(block) : local;
    for (uint32_t i = 0; i < count; ++i) {
        uint64_t item;
        std::memcpy(&item, values + i * 8, 8);
        item = Swap64(item);
        std::memcpy(swapped + i * 8, &item, 8);
    }
    const uint8_t result = SetVoid(self, swapped, count << 3);
    Delete(block);
    return result;
}

// The game's 0x00976139: slot 48: a string's length in 32 bits, then its
// characters.
uint8_t __fastcall SetFieldString(char* self, void*, void* text)
{
    const uint32_t length = Virtual<uint32_t>(text, string::kStrlen);
    if (!Virtual<uint8_t>(self, slot::kSetFieldUint32, length))
        return 0;
    const void* characters = Virtual<const void*>(text, string::kToChar);
    return SetVoid(self, characters, length);
}

// The game's 0x00976A6E: slot 14 of the output stream part: a C string, as
// a string field.
uint8_t __fastcall OStreamSetText(char* self, void*, const char* text)
{
    GameString copy;
    This<void*>(addr::SC4String_FromText, &copy, text);
    const uint8_t result = Virtual<uint8_t>(self - record::kOStreamPart, slot::kSetFieldString, &copy);
    DestroyString(copy);
    return result;
}

// A variant's array: the count in 32 bits, then the items through `set`.
uint8_t SetVariantArray(char* self, void* value, std::size_t items, std::size_t set)
{
    const uint32_t count = Virtual<uint32_t>(value, variant::kGetCount);
    if (!Virtual<uint8_t>(self, slot::kSetFieldUint32, count))
        return 0;
    const void* data = Virtual<const void*>(value, items);
    return Virtual<uint8_t>(self, set, data, count);
}

// The game's 0x00976AB2: slot 50, SetFieldVariant: the type's code in 32
// bits, then the value by type. Types with no case write the code alone and
// answer false.
uint8_t __fastcall SetFieldVariant(char* self, void*, void* value)
{
    const uint32_t code = VariantTypeCode(Virtual<uint32_t>(value, variant::kGetType));
    if (!Virtual<uint8_t>(self, slot::kSetFieldUint32, code))
        return 0;
    if (code > 0x8000) {
        switch (code - 0x8001) {
        case 0x0:
            return SetVariantArray(self, value, variant::kRefUint8, slot::kSetFieldUint8Array);
        case 0x1:
            return SetVariantArray(self, value, variant::kRefUint16, slot::kSetFieldUint16Array);
        case 0x2:
            return SetVariantArray(self, value, variant::kRefUint32, slot::kSetFieldUint32Array);
        case 0x3:
            return SetVariantArray(self, value, variant::kRefUint64, slot::kSetFieldUint64Array);
        case 0x4:
            return SetVariantArray(self, value, variant::kRefSint8, slot::kSetFieldSint8Array);
        case 0x5:
            return SetVariantArray(self, value, variant::kRefSint16, slot::kSetFieldSint16Array);
        case 0x6:
            return SetVariantArray(self, value, variant::kRefSint32, slot::kSetFieldSint32Array);
        case 0x7:
            return SetVariantArray(self, value, variant::kRefSint64, slot::kSetFieldSint64Array);
        case 0x8:
            return SetVariantArray(self, value, variant::kRefFloat32, slot::kSetFieldFloat32Array);
        case 0x9:
            return SetVariantArray(self, value, variant::kRefFloat64, slot::kSetFieldFloat64Array);
        case 0xA:
            return SetVariantArray(self, value, variant::kRefBool, slot::kSetFieldVoid);
        case 0xB: {
            // Characters go out as a string field, with no count before it.
            const uint32_t count = Virtual<uint32_t>(value, variant::kGetCount);
            const char* characters = Virtual<const char*>(value, variant::kRefCharB);
            GameString copy;
            This<void*>(addr::SC4String_FromBytes, &copy, characters, count);
            const uint8_t result = Virtual<uint8_t>(self, slot::kSetFieldString, &copy);
            DestroyString(copy);
            return result;
        }
        case 0xF:
            return SetVariantArray(self, value, variant::kRefUnicodeChar, slot::kSetFieldUint16Array);
        case 0x10:
            return SetVariantArray(self, value, variant::kRefCharA, slot::kSetFieldUint8Array);
        default:
            return 0;
        }
    }
    if (code == 0x8000) {
        // An array of no type: the count, then that many raw bytes. The count
        // is asked for twice.
        if (!Virtual<uint8_t>(self, slot::kSetFieldUint32, Virtual<uint32_t>(value, variant::kGetCount)))
            return 0;
        const uint32_t count = Virtual<uint32_t>(value, variant::kGetCount);
        const void* data = Virtual<const void*>(value, variant::kRefVoid);
        return Virtual<uint8_t>(self, slot::kSetFieldVoid, data, count);
    }
    switch (code) {
    case 0x0:
        return 1;
    case 0x1:
        return Virtual<uint8_t>(self, slot::kSetFieldUint8, Virtual<uint32_t>(value, variant::kUint8));
    case 0x2:
        return Virtual<uint8_t>(self, slot::kSetFieldUint16, Virtual<uint32_t>(value, variant::kUint16));
    case 0x3:
        return Virtual<uint8_t>(self, slot::kSetFieldUint32, Virtual<uint32_t>(value, variant::kUint32));
    case 0x4:
        return Virtual<uint8_t>(self, slot::kSetFieldUint64, Virtual<uint64_t>(value, variant::kUint64));
    case 0x5:
        return Virtual<uint8_t>(self, slot::kSetFieldSint8, Virtual<uint32_t>(value, variant::kSint8));
    case 0x6:
        return Virtual<uint8_t>(self, slot::kSetFieldSint16, Virtual<uint32_t>(value, variant::kSint16));
    case 0x7:
        return Virtual<uint8_t>(self, slot::kSetFieldSint32, Virtual<uint32_t>(value, variant::kSint32));
    case 0x8:
        return Virtual<uint8_t>(self, slot::kSetFieldSint64, Virtual<uint64_t>(value, variant::kSint64));
    case 0x9: {
        const double number = Virtual<double>(value, variant::kFloat32);
        return Virtual<uint8_t>(self, slot::kSetFieldFloat32, static_cast<float>(number));
    }
    case 0xA: {
        const double number = Virtual<double>(value, variant::kFloat64);
        return Virtual<uint8_t>(self, slot::kSetFieldFloat64, number);
    }
    case 0xB:
        return Virtual<uint8_t>(self, slot::kSetFieldUint8, Virtual<uint32_t>(value, variant::kBool));
    case 0xC:
    case 0x11:
        return Virtual<uint8_t>(self, slot::kSetFieldUint8, Virtual<uint32_t>(value, variant::kChar));
    case 0x10:
        return Virtual<uint8_t>(self, slot::kSetFieldUint16, Virtual<uint32_t>(value, variant::kUnicodeChar));
    default:
        return 0;
    }
}

bool InstallSaveFile1(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[48] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        // The write record.
        {addr::cGZDBSegmentPackedFile_F96DBA0, reinterpret_cast<void*>(&GetPosition)},
        {addr::cGZDBSegmentPackedFile_F96DBBC, reinterpret_cast<void*>(&SeekAbsolute)},
        {addr::cGZDBSegmentPackedFile_F96DBF0, reinterpret_cast<void*>(&SeekRelative)},
        {addr::cGZDBSegmentPackedFile_F96E91B, reinterpret_cast<void*>(&GetSize)},
        {addr::cGZDBSegmentPackedFile_F96EB2A, reinterpret_cast<void*>(&GetFieldVoid)},
        {addr::cGZDBSegmentPackedFile_F96EB78, reinterpret_cast<void*>(&WriteOut)},
        {addr::cGZDBSegmentPackedFile_F96F33D, reinterpret_cast<void*>(&Destruct)},
        {addr::cGZDBSegmentPackedFile_F96F6E9, reinterpret_cast<void*>(&DeleteRecord)},
        {addr::cGZDBSegmentPackedFile_F96F70F, reinterpret_cast<void*>(&Discard)},
        {addr::cGZDBSegmentPackedFile_F96F6BE, reinterpret_cast<void*>(&CopyToNewBlock)},
        {addr::cGZDBSegmentPackedFile_F970080, reinterpret_cast<void*>(&Reserve)},
        {addr::cGZDBSegmentPackedFile_F970C4D, reinterpret_cast<void*>(&Construct)},
        {addr::cGZDBSegmentPackedFile_F970E67, reinterpret_cast<void*>(&ConstructFromMemory)},
        {addr::cGZDBSegmentPackedFile_F970EE5, reinterpret_cast<void*>(&SetSize)},
        {addr::cGZDBSegmentPackedFile_WriteRecordSetFieldVoid, reinterpret_cast<void*>(&SetFieldVoid)},
        // The record base's typed sets.
        {addr::cGZDBSegmentPackedFile_F97567E, reinterpret_cast<void*>(&VariantTypeCode)},
        {addr::cGZPersistDBRecord_SetFieldUint8, reinterpret_cast<void*>(&SetFieldUint8)},
        {addr::cGZPersistDBRecord_SetFieldUint8Array, reinterpret_cast<void*>(&SetFieldUint8Array)},
        {addr::cGZPersistDBRecord_SetFieldFloat32, reinterpret_cast<void*>(&SetFieldFloat32)},
        {addr::cGZPersistDBRecord_SetFieldFloat32Array, reinterpret_cast<void*>(&SetFieldFloat32Array)},
        {addr::cGZPersistDBRecord_SetFieldFloat64, reinterpret_cast<void*>(&SetFieldFloat64)},
        {addr::cGZPersistDBRecord_SetFieldFloat64Array, reinterpret_cast<void*>(&SetFieldFloat64Array)},
        {addr::cGZPersistDBRecord_OStreamSetString, reinterpret_cast<void*>(&OStreamSetString)},
        {addr::cGZPersistDBRecord_SetFieldStringChained, reinterpret_cast<void*>(&SetFieldStringChained)},
        {addr::cGZPersistDBRecord_SetFieldResKey, reinterpret_cast<void*>(&SetFieldResKey)},
        {addr::cGZPersistDBRecord_OStreamSetVoid, reinterpret_cast<void*>(&OStreamSetVoid)},
        {addr::cGZPersistDBRecord_SetFieldVoidSized, reinterpret_cast<void*>(&SetFieldVoidSized)},
        {addr::cGZPersistDBRecord_SetFieldUint16, reinterpret_cast<void*>(&SetFieldUint16)},
        {addr::cGZPersistDBRecord_SetFieldUint16Array, reinterpret_cast<void*>(&SetFieldUint16Array)},
        {addr::cGZPersistDBRecord_SetFieldUint32, reinterpret_cast<void*>(&SetFieldUint32)},
        {addr::cGZPersistDBRecord_SetFieldUint32Array, reinterpret_cast<void*>(&SetFieldUint32Array)},
        {addr::cGZPersistDBRecord_SetFieldUint64, reinterpret_cast<void*>(&SetFieldUint64)},
        {addr::cGZPersistDBRecord_SetFieldUint64Array, reinterpret_cast<void*>(&SetFieldUint64Array)},
        {addr::cGZPersistDBRecord_SetFieldString, reinterpret_cast<void*>(&SetFieldString)},
        {addr::cGZPersistDBRecord_OStreamSetText, reinterpret_cast<void*>(&OStreamSetText)},
        {addr::cGZPersistDBRecord_SetFieldVariant, reinterpret_cast<void*>(&SetFieldVariant)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    static_assert(count <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < count; ++i) {
        // diagnostics.prop_pox watches SetFieldVoid with a detour of its own,
        // and one address takes one detour: with the diagnostic on (or its
        // jump already in place), this one function stays the game's.
        if (hooks[i].first == addr::cGZDBSegmentPackedFile_WriteRecordSetFieldVoid
            && (registry::ModeOf("diagnostics.prop_pox") != registry::Mode::Original
                || *reinterpret_cast<const uint8_t*>(hooks[i].first) == 0xE9)) {
            log::Write("saves: 0x%08X left to the game while diagnostics.prop_pox is on",
                       static_cast<unsigned>(hooks[i].first));
            continue;
        }
        const addr::Entry* found = addr_check::Find(hooks[i].first);
        if (!found || !hook::Install(*found, hooks[i].second, &originals[i]))
            return false;
    }
    return true;
}

const bool g_saveFile1Registered = registry::Register({
    "saves.file.part1",
    "saves",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cGZDBSegmentPackedFile_WriteRecordSetFieldVoid,
    "Our packed file write path, part 1: the write record and the record base's typed sets",
    false,
    InstallSaveFile1,
});

} // namespace
} // namespace opensc4::saves
