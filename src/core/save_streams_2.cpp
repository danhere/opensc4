// The city save code's record streams (docs/save-format.md), part 2:
// 0x005DA0D0 to 0x005DAD50, the typed helpers every serialisable class calls
// from its Read and Write. Replacement, off by default:
//
//   saves.streams.part2: variants, dates, stopwatches, random generators,
//     the two grid iterators, float vectors, boxes, 3x3 and 4x4 matrices,
//     transforms (current and old format), the pollution structure and the
//     rows of bits.
//
// All of them are cdecl (stream, value). All but two return the stream, like
// a stream operator; 0x005DA930 and 0x005DAA80 return what their last stream
// call left in EAX. No caller reads any of these results.
//
// A reader that fails leaves what the stream left: the helpers never look at
// a stream call's result and always make every call. Where the original
// reads into a reused argument slot or an unset local, the value a failed
// read leaves behind is noted at the function.
//
// A transform is 0x38 bytes: +0 a change counter (one byte, raised by every
// change), +1 the flags (1 has a translation, 2 has a scale other than 1,
// 4 has a rotation), +4 nine floats of rotation, +0x28 three floats of
// translation, +0x34 the scale.
//
// Still the game's: 0x005DAAE0, the copy of a range of bits between two bit
// vectors (the standard library's, shared with SC4BitVector::Erase and
// Resize at 0x00477E20 and 0x004780D0, which the whole game uses).
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace opensc4::saves {
namespace {

namespace in { // cIGZIStream, by vtable offset
constexpr std::size_t kGetSint8 = 0x10;
constexpr std::size_t kGetUint8 = 0x14;
constexpr std::size_t kGetSint32 = 0x20;
constexpr std::size_t kGetUint32 = 0x24;
constexpr std::size_t kGetUint64 = 0x2C;
constexpr std::size_t kGetFloat32 = 0x30;
constexpr std::size_t kGetFloat64 = 0x34;
constexpr std::size_t kReadVariant = 0x70; // cISC4DBSegmentIStream
} // namespace in

namespace out { // cIGZOStream, by vtable offset
constexpr std::size_t kSetSint8 = 0x10;
constexpr std::size_t kSetUint8 = 0x14;
constexpr std::size_t kSetUint16 = 0x1C;
constexpr std::size_t kSetSint32 = 0x20;
constexpr std::size_t kSetUint32 = 0x24;
constexpr std::size_t kSetUint64 = 0x2C;
constexpr std::size_t kSetFloat32 = 0x30;
constexpr std::size_t kSetFloat64 = 0x34;
constexpr std::size_t kWriteVariant = 0x70; // cISC4DBSegmentOStream
} // namespace out

constexpr uint32_t kSegmentIStreamIid = 0x4999A8F5;
constexpr uint32_t kSegmentOStreamIid = 0x6999A8FB;

namespace transform {
constexpr std::size_t kChanges = 0x0;
constexpr std::size_t kFlags = 0x1;
constexpr std::size_t kRotation = 0x4;
constexpr std::size_t kTranslation = 0x28;
constexpr std::size_t kScale = 0x34;
constexpr uint8_t kHasTranslation = 0x1;
constexpr uint8_t kHasScale = 0x2;
constexpr uint8_t kHasRotation = 0x4;
} // namespace transform

template <typename R, typename... Args>
R This(std::uintptr_t function, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(function)(self, args...);
}

uint32_t Bits(void* pointer)
{
    return static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(pointer));
}

// The game's 0x005DA0D0: asks the stream for its segment stream part and has
// it read a variant. The variant is untouched when the stream has no such
// part.
void* __cdecl ReadVariant(void* stream, void* variant)
{
    void* segmentStream = nullptr;
    if (Virtual<uint8_t>(stream, 0x0, kSegmentIStreamIid, &segmentStream))
        Virtual<uint8_t>(segmentStream, in::kReadVariant, variant);
    if (segmentStream)
        Virtual<uint32_t>(segmentStream, 0x8);
    return stream;
}

// The game's 0x005DA120: the same for writing a variant.
void* __cdecl WriteVariant(void* stream, void* variant)
{
    void* segmentStream = nullptr;
    if (Virtual<uint8_t>(stream, 0x0, kSegmentOStreamIid, &segmentStream))
        Virtual<uint8_t>(segmentStream, out::kWriteVariant, variant);
    if (segmentStream)
        Virtual<uint32_t>(segmentStream, 0x8);
    return stream;
}

// The game's 0x005DA170: a date is its day number, a uint32. The original
// reads into its own stream argument's slot, so a failed read sets the day
// to the stream's address.
void* __cdecl ReadDate(void* stream, void* date)
{
    uint32_t day = Bits(stream);
    Virtual<uint8_t>(stream, in::kGetUint32, &day);
    This<void>(addr::cRZDate_SetDay, date, day);
    return stream;
}

// The game's 0x005DA1A0.
void* __cdecl WriteDate(void* stream, void* date)
{
    Virtual<uint8_t>(stream, out::kSetUint32, At<uint32_t>(date, 0x4));
    return stream;
}

// The game's 0x005DA1C0: mode byte, elapsed time, running byte. The three
// locals are unset in the original too: a failed read gives the stopwatch
// whatever they held.
void* __cdecl ReadStopwatch(void* stream, void* watch)
{
    uint8_t mode;
    uint64_t elapsed;
    uint8_t running;
    This<void>(addr::SC4Stopwatch_Reset, watch);
    Virtual<uint8_t>(stream, in::kGetUint8, &mode);
    This<void>(addr::SC4Stopwatch_ConstructMode, watch, static_cast<uint32_t>(mode));
    Virtual<uint8_t>(stream, in::kGetUint64, &elapsed);
    This<void>(addr::SC4Stopwatch_Elapsed4, watch, elapsed);
    Virtual<uint8_t>(stream, in::kGetUint8, &running);
    if (running != 0)
        This<void>(addr::SC4Stopwatch_Start, watch);
    else
        This<void>(addr::SC4Stopwatch_Stop, watch);
    return stream;
}

// The game's 0x005DA240. The bytes are pushed as dwords whose upper three
// bytes are leftovers: unset stack for the mode (zero here), the stream's
// address for the running byte (kept).
void* __cdecl WriteStopwatch(void* stream, void* watch)
{
    const uint8_t mode = static_cast<uint8_t>(This<uint32_t>(addr::SC4Stopwatch_GetMode, watch));
    Virtual<uint8_t>(stream, out::kSetUint8, static_cast<uint32_t>(mode));
    uint64_t elapsed;
    This<void>(addr::SC4Stopwatch_Elapsed2, watch, &elapsed);
    Virtual<uint8_t>(stream, out::kSetUint64, elapsed);
    const uint8_t running = static_cast<uint8_t>(This<uint32_t>(addr::GZStopwatch_IsRunning, watch));
    Virtual<uint8_t>(stream, out::kSetUint8, (Bits(stream) & 0xFFFFFF00u) | running);
    return stream;
}

// The game's 0x005DA2A0: uint32 +0, uint32 +4, int32 +8, double +0x10. The
// third is read into the stream argument's slot and copied: a failed read
// stores the stream's address at +8.
void* __cdecl ReadRandom(void* stream, char* random)
{
    Virtual<uint8_t>(stream, in::kGetUint32, random);
    Virtual<uint8_t>(stream, in::kGetUint32, random + 0x4);
    uint32_t third = Bits(stream);
    Virtual<uint8_t>(stream, in::kGetSint32, &third);
    At<uint32_t>(random, 0x8) = third;
    Virtual<uint8_t>(stream, in::kGetFloat64, random + 0x10);
    return stream;
}

// The game's 0x005DA2E0.
void* __cdecl WriteRandom(void* stream, char* random)
{
    Virtual<uint8_t>(stream, out::kSetUint32, At<uint32_t>(random, 0x0));
    Virtual<uint8_t>(stream, out::kSetUint32, At<uint32_t>(random, 0x4));
    Virtual<uint8_t>(stream, out::kSetSint32, At<uint32_t>(random, 0x8));
    Virtual<uint8_t>(stream, out::kSetFloat64, At<double>(random, 0x10));
    return stream;
}

// The game's 0x005DA320: int8 +0, int8 +1, then seven int32 from +4.
void* __cdecl ReadFlaggedIterator(void* stream, char* iterator)
{
    Virtual<uint8_t>(stream, in::kGetSint8, iterator);
    Virtual<uint8_t>(stream, in::kGetSint8, iterator + 0x1);
    Virtual<uint8_t>(stream, in::kGetSint32, iterator + 0x4);
    Virtual<uint8_t>(stream, in::kGetSint32, iterator + 0x8);
    Virtual<uint8_t>(stream, in::kGetSint32, iterator + 0xC);
    Virtual<uint8_t>(stream, in::kGetSint32, iterator + 0x10);
    Virtual<uint8_t>(stream, in::kGetSint32, iterator + 0x14);
    Virtual<uint8_t>(stream, in::kGetSint32, iterator + 0x18);
    Virtual<uint8_t>(stream, in::kGetSint32, iterator + 0x1C);
    return stream;
}

// The game's 0x005DA390.
void* __cdecl WriteFlaggedIterator(void* stream, char* iterator)
{
    Virtual<uint8_t>(stream, out::kSetSint8, static_cast<uint32_t>(At<uint8_t>(iterator, 0x0)));
    Virtual<uint8_t>(stream, out::kSetSint8, static_cast<uint32_t>(At<uint8_t>(iterator, 0x1)));
    Virtual<uint8_t>(stream, out::kSetSint32, At<uint32_t>(iterator, 0x4));
    Virtual<uint8_t>(stream, out::kSetSint32, At<uint32_t>(iterator, 0x8));
    Virtual<uint8_t>(stream, out::kSetSint32, At<uint32_t>(iterator, 0xC));
    Virtual<uint8_t>(stream, out::kSetSint32, At<uint32_t>(iterator, 0x10));
    Virtual<uint8_t>(stream, out::kSetSint32, At<uint32_t>(iterator, 0x14));
    Virtual<uint8_t>(stream, out::kSetSint32, At<uint32_t>(iterator, 0x18));
    Virtual<uint8_t>(stream, out::kSetSint32, At<uint32_t>(iterator, 0x1C));
    return stream;
}

// The game's 0x005DA410: int8 +0, int32 +4 and +8, uint32 +0xC, then four
// int32 from +0x10.
void* __cdecl ReadCellIterator(void* stream, char* iterator)
{
    Virtual<uint8_t>(stream, in::kGetSint8, iterator);
    Virtual<uint8_t>(stream, in::kGetSint32, iterator + 0x4);
    Virtual<uint8_t>(stream, in::kGetSint32, iterator + 0x8);
    Virtual<uint8_t>(stream, in::kGetUint32, iterator + 0xC);
    Virtual<uint8_t>(stream, in::kGetSint32, iterator + 0x10);
    Virtual<uint8_t>(stream, in::kGetSint32, iterator + 0x14);
    Virtual<uint8_t>(stream, in::kGetSint32, iterator + 0x18);
    Virtual<uint8_t>(stream, in::kGetSint32, iterator + 0x1C);
    return stream;
}

// The game's 0x005DA480.
void* __cdecl WriteCellIterator(void* stream, char* iterator)
{
    Virtual<uint8_t>(stream, out::kSetSint8, static_cast<uint32_t>(At<uint8_t>(iterator, 0x0)));
    Virtual<uint8_t>(stream, out::kSetSint32, At<uint32_t>(iterator, 0x4));
    Virtual<uint8_t>(stream, out::kSetSint32, At<uint32_t>(iterator, 0x8));
    Virtual<uint8_t>(stream, out::kSetUint32, At<uint32_t>(iterator, 0xC));
    Virtual<uint8_t>(stream, out::kSetSint32, At<uint32_t>(iterator, 0x10));
    Virtual<uint8_t>(stream, out::kSetSint32, At<uint32_t>(iterator, 0x14));
    Virtual<uint8_t>(stream, out::kSetSint32, At<uint32_t>(iterator, 0x18));
    Virtual<uint8_t>(stream, out::kSetSint32, At<uint32_t>(iterator, 0x1C));
    return stream;
}

// Floats are read in place and written as the raw dword the original pushes.
void ReadFloats(void* stream, char* floats, std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i)
        Virtual<uint8_t>(stream, in::kGetFloat32, floats + 4 * i);
}

void WriteFloats(void* stream, char* floats, std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i)
        Virtual<uint8_t>(stream, out::kSetFloat32, At<uint32_t>(floats, 4 * i));
}

// The game's 0x005DA4F0: two floats.
void* __cdecl ReadVector2(void* stream, char* vector)
{
    ReadFloats(stream, vector, 2);
    return stream;
}

// The game's 0x005DA520.
void* __cdecl WriteVector2(void* stream, char* vector)
{
    WriteFloats(stream, vector, 2);
    return stream;
}

// The game's 0x005DA550: three floats.
void* __cdecl ReadVector3(void* stream, char* vector)
{
    ReadFloats(stream, vector, 3);
    return stream;
}

// The game's 0x005DA580.
void* __cdecl WriteVector3(void* stream, char* vector)
{
    WriteFloats(stream, vector, 3);
    return stream;
}

// The game's 0x005DA5B0: six floats.
void* __cdecl ReadBox(void* stream, char* box)
{
    ReadFloats(stream, box, 6);
    return stream;
}

// The game's 0x005DA600.
void* __cdecl WriteBox(void* stream, char* box)
{
    WriteFloats(stream, box, 6);
    return stream;
}

// The game's 0x005DA650: nine floats, three rows of three.
void* __cdecl ReadMatrix3(void* stream, char* matrix)
{
    ReadFloats(stream, matrix, 9);
    return stream;
}

// The game's 0x005DA680.
void* __cdecl WriteMatrix3(void* stream, char* matrix)
{
    WriteFloats(stream, matrix, 9);
    return stream;
}

// The game's 0x005DA6B0: sixteen floats, four rows of four.
void* __cdecl ReadMatrix4(void* stream, char* matrix)
{
    ReadFloats(stream, matrix, 16);
    return stream;
}

// The game's 0x005DA6E0.
void* __cdecl WriteMatrix4(void* stream, char* matrix)
{
    WriteFloats(stream, matrix, 16);
    return stream;
}

void BumpTransform(char* target, uint8_t flags)
{
    const uint8_t changes = static_cast<uint8_t>(At<uint8_t>(target, transform::kChanges) + 1);
    At<uint8_t>(target, transform::kFlags) = flags;
    At<uint8_t>(target, transform::kChanges) = changes;
}

// Reads the rotation into a filled temporary and copies it over.
void ReadRotation(void* stream, char* target)
{
    const uint32_t fill = Global<uint32_t>(addr::SC4Save_ZeroB23044);
    uint32_t rotation[9];
    for (uint32_t& word : rotation)
        word = fill;
    ReadMatrix3(stream, reinterpret_cast<char*>(rotation));
    for (std::size_t i = 0; i < 9; ++i)
        At<uint32_t>(target, transform::kRotation + 4 * i) = rotation[i];
    BumpTransform(target, static_cast<uint8_t>(At<uint8_t>(target, transform::kFlags) | transform::kHasRotation));
}

void ReadTranslation(void* stream, char* target)
{
    const uint32_t fill = Global<uint32_t>(addr::SC4Save_ZeroB23044);
    uint32_t translation[3] = {fill, fill, fill};
    Virtual<uint8_t>(stream, in::kGetFloat32, &translation[0]);
    Virtual<uint8_t>(stream, in::kGetFloat32, &translation[1]);
    Virtual<uint8_t>(stream, in::kGetFloat32, &translation[2]);
    At<uint32_t>(target, transform::kTranslation) = translation[0];
    At<uint32_t>(target, transform::kTranslation + 4) = translation[1];
    At<uint32_t>(target, transform::kTranslation + 8) = translation[2];
    BumpTransform(target, static_cast<uint8_t>(At<uint8_t>(target, transform::kFlags) | transform::kHasTranslation));
}

// Reads the scale into `slot` (which holds what the original's reused
// argument slot held) and sets or clears the scale flag by whether it is 1.
void ReadScale(void* stream, char* target, uint32_t slot)
{
    Virtual<uint8_t>(stream, in::kGetFloat32, &slot);
    float scale;
    std::memcpy(&scale, &slot, sizeof(scale));
    At<uint32_t>(target, transform::kScale) = slot;
    uint8_t flags = At<uint8_t>(target, transform::kFlags);
    if (scale == Global<float>(addr::SC4_FloatOneA81228))
        flags = static_cast<uint8_t>(flags & ~transform::kHasScale);
    else
        flags = static_cast<uint8_t>(flags | transform::kHasScale);
    At<uint8_t>(target, transform::kFlags) = flags;
    ++At<uint8_t>(target, transform::kChanges);
}

// The game's 0x005DA710: a flags byte, then only the parts it names. A
// transform that is not the identity is first made the identity. The flags
// byte is read into the stream argument's slot (a failed read leaves the low
// byte of the stream's address as the flags) and the scale into the
// transform argument's slot (a failed read leaves the transform's address as
// the scale's bits).
void* __cdecl ReadTransform(void* stream, char* target)
{
    uint8_t flags = static_cast<uint8_t>(Bits(stream));
    Virtual<uint8_t>(stream, in::kGetUint8, &flags);
    if (At<uint8_t>(target, transform::kFlags) != 0) {
        At<uint32_t>(target, 0x10) = 0;
        At<uint32_t>(target, 0x1C) = 0;
        At<uint32_t>(target, 0x4) = 0x3F800000;
        At<uint32_t>(target, 0x8) = 0;
        At<uint32_t>(target, 0x20) = 0;
        At<uint32_t>(target, 0x14) = 0x3F800000;
        At<uint32_t>(target, 0xC) = 0;
        At<uint32_t>(target, 0x18) = 0;
        At<uint32_t>(target, 0x24) = 0x3F800000;
        At<uint32_t>(target, 0x34) = 0x3F800000;
        At<uint32_t>(target, 0x28) = 0;
        At<uint32_t>(target, 0x2C) = 0;
        At<uint32_t>(target, 0x30) = 0;
        At<uint8_t>(target, transform::kFlags) = 0;
        ++At<uint8_t>(target, transform::kChanges);
    }
    if (flags & transform::kHasRotation)
        ReadRotation(stream, target);
    if (flags & transform::kHasTranslation)
        ReadTranslation(stream, target);
    if (flags & transform::kHasScale)
        ReadScale(stream, target, Bits(target));
    return stream;
}

// The game's 0x005DA8C0: the flags byte (pushed in the low byte of the
// transform argument's slot), then the parts it names.
void* __cdecl WriteTransform(void* stream, char* source)
{
    const uint8_t flags = At<uint8_t>(source, transform::kFlags);
    Virtual<uint8_t>(stream, out::kSetUint8, (Bits(source) & 0xFFFFFF00u) | flags);
    if (flags & transform::kHasRotation)
        WriteMatrix3(stream, source + transform::kRotation);
    if (flags & transform::kHasTranslation)
        WriteFloats(stream, source + transform::kTranslation, 3);
    if (flags & transform::kHasScale)
        Virtual<uint8_t>(stream, out::kSetFloat32, At<uint32_t>(source, transform::kScale));
    return stream;
}

// The game's 0x005DA930, the old format: rotation, translation and scale
// always, then a uint32 whose low byte becomes the change counter. The scale
// is read into the stream argument's slot; the last local is unset in the
// original. Returns what the last stream call left in EAX.
uint32_t __cdecl ReadMatrixAsTransform(void* stream, char* target)
{
    ReadRotation(stream, target);
    ReadTranslation(stream, target);
    ReadScale(stream, target, Bits(stream));
    uint32_t changes;
    const uint32_t result = Virtual<uint32_t>(stream, in::kGetUint32, &changes);
    At<uint8_t>(target, transform::kChanges) = static_cast<uint8_t>(changes);
    return result;
}

// The game's 0x005DAA80, the old format's writer: rotation, translation,
// scale, the change counter as a uint32. Returns what the last stream call
// left in EAX.
uint32_t __cdecl WriteTransformAsMatrix(void* stream, char* source)
{
    WriteMatrix3(stream, source + transform::kRotation);
    WriteFloats(stream, source + transform::kTranslation, 3);
    Virtual<uint8_t>(stream, out::kSetFloat32, At<uint32_t>(source, transform::kScale));
    return Virtual<uint32_t>(stream, out::kSetUint32,
                             static_cast<uint32_t>(At<uint8_t>(source, transform::kChanges)));
}

// How many uint16 the vector at +0x18 of the pollution structure holds.
uint32_t WordCount(char* value)
{
    return static_cast<uint32_t>(static_cast<int32_t>(At<uint32_t>(value, 0x1C) - At<uint32_t>(value, 0x18)) >> 1);
}

// How many 0x14-byte bit vectors a vector of them holds.
int32_t RowCount(char* rows)
{
    return static_cast<int32_t>(At<uint32_t>(rows, 0x4) - At<uint32_t>(rows, 0x0)) / 20;
}

// The game's 0x005DAB70: int8 +0, int8 +1, uint8 +2, five int32 from +4, the
// uint16 vector at +0x18 (count, values), three int32 from +0x24.
void* __cdecl WritePollutionStruct(void* stream, char* value)
{
    Virtual<uint8_t>(stream, out::kSetSint8, static_cast<uint32_t>(At<uint8_t>(value, 0x0)));
    Virtual<uint8_t>(stream, out::kSetSint8, static_cast<uint32_t>(At<uint8_t>(value, 0x1)));
    Virtual<uint8_t>(stream, out::kSetUint8, static_cast<uint32_t>(At<uint8_t>(value, 0x2)));
    Virtual<uint8_t>(stream, out::kSetSint32, At<uint32_t>(value, 0x4));
    Virtual<uint8_t>(stream, out::kSetSint32, At<uint32_t>(value, 0x8));
    Virtual<uint8_t>(stream, out::kSetSint32, At<uint32_t>(value, 0xC));
    Virtual<uint8_t>(stream, out::kSetSint32, At<uint32_t>(value, 0x10));
    Virtual<uint8_t>(stream, out::kSetSint32, At<uint32_t>(value, 0x14));
    Virtual<uint8_t>(stream, out::kSetUint32, WordCount(value));
    const uint32_t count = WordCount(value);
    for (uint32_t i = 0; i < count; ++i)
        Virtual<uint8_t>(stream, out::kSetUint16, static_cast<uint32_t>(At<uint16_t*>(value, 0x18)[i]));
    Virtual<uint8_t>(stream, out::kSetSint32, At<uint32_t>(value, 0x24));
    Virtual<uint8_t>(stream, out::kSetSint32, At<uint32_t>(value, 0x28));
    Virtual<uint8_t>(stream, out::kSetSint32, At<uint32_t>(value, 0x2C));
    return stream;
}

// The game's 0x005DAC40: a vector of bit vectors (0x14 bytes each: first
// word, first bit, last word, last bit, end of storage). Writes the number
// of rows, the first row's length in bits (0 without rows), then for every
// row one byte per bit. As in the original, every row is written as far as
// the number of ROWS, not its own length, and the bit's byte is pushed
// under the upper three bytes of the bit's mask.
void* __cdecl WriteBitRows(void* stream, char* rows)
{
    Virtual<uint8_t>(stream, out::kSetUint32, static_cast<uint32_t>(RowCount(rows)));
    char* const first = At<char*>(rows, 0x0);
    uint32_t columns = 0;
    if (RowCount(rows) != 0) {
        const int32_t words = static_cast<int32_t>(At<uint32_t>(first, 0x8) - At<uint32_t>(first, 0x0)) >> 2;
        columns = (static_cast<uint32_t>(words) << 5) - At<uint32_t>(first, 0x4) + At<uint32_t>(first, 0xC);
    }
    Virtual<uint8_t>(stream, out::kSetUint32, columns);
    const int32_t count = RowCount(rows);
    if (count <= 0)
        return stream;
    std::size_t offset = 0;
    int32_t left = count;
    do {
        int32_t column = 0;
        do {
            char* const row = At<char*>(rows, 0x0) + offset;
            uint32_t* word = At<uint32_t*>(row, 0x0);
            const int32_t position = static_cast<int32_t>(At<uint32_t>(row, 0x4) + static_cast<uint32_t>(column));
            word += position / 32;
            int32_t bit = position % 32;
            if (bit < 0) {
                bit += 32;
                --word;
            }
            const uint32_t mask = uint32_t{1} << bit;
            const uint32_t set = (*word & mask) != 0 ? 1u : 0u;
            Virtual<uint8_t>(stream, out::kSetUint8, (mask & 0xFFFFFF00u) | set);
            ++column;
        } while (column < count);
        offset += 0x14;
        --left;
    } while (left != 0);
    return stream;
}

bool InstallSaveStreams2(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[28] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4Save_F5DA0D0, reinterpret_cast<void*>(&ReadVariant)},
        {addr::SC4Save_F5DA120, reinterpret_cast<void*>(&WriteVariant)},
        {addr::SC4Stream_ReadDate, reinterpret_cast<void*>(&ReadDate)},
        {addr::SC4Stream_WriteDate, reinterpret_cast<void*>(&WriteDate)},
        {addr::SC4Stream_ReadStopwatch, reinterpret_cast<void*>(&ReadStopwatch)},
        {addr::SC4Stream_WriteStopwatch, reinterpret_cast<void*>(&WriteStopwatch)},
        {addr::SC4Stream_ReadRandom, reinterpret_cast<void*>(&ReadRandom)},
        {addr::SC4Stream_WriteRandom, reinterpret_cast<void*>(&WriteRandom)},
        {addr::SC4Stream_ReadFlaggedIterator, reinterpret_cast<void*>(&ReadFlaggedIterator)},
        {addr::SC4Stream_WriteFlaggedIterator, reinterpret_cast<void*>(&WriteFlaggedIterator)},
        {addr::SC4Stream_ReadCellIterator, reinterpret_cast<void*>(&ReadCellIterator)},
        {addr::SC4Stream_WriteCellIterator, reinterpret_cast<void*>(&WriteCellIterator)},
        {addr::SC4Stream_ReadVector2, reinterpret_cast<void*>(&ReadVector2)},
        {addr::SC4Stream_WriteVector2, reinterpret_cast<void*>(&WriteVector2)},
        {addr::SC4Stream_ReadVector3, reinterpret_cast<void*>(&ReadVector3)},
        {addr::SC4Stream_WriteVector3, reinterpret_cast<void*>(&WriteVector3)},
        {addr::SC4Stream_ReadBox, reinterpret_cast<void*>(&ReadBox)},
        {addr::SC4Stream_WriteBox, reinterpret_cast<void*>(&WriteBox)},
        {addr::SC4Save_F5DA650, reinterpret_cast<void*>(&ReadMatrix3)},
        {addr::SC4Save_F5DA680, reinterpret_cast<void*>(&WriteMatrix3)},
        {addr::SC4Save_F5DA6B0, reinterpret_cast<void*>(&ReadMatrix4)},
        {addr::SC4Save_F5DA6E0, reinterpret_cast<void*>(&WriteMatrix4)},
        {addr::SC4Stream_ReadTransform, reinterpret_cast<void*>(&ReadTransform)},
        {addr::SC4Stream_WriteTransform, reinterpret_cast<void*>(&WriteTransform)},
        {addr::SC4Stream_ReadMatrixAsTransform, reinterpret_cast<void*>(&ReadMatrixAsTransform)},
        {addr::SC4Save_F5DAA80, reinterpret_cast<void*>(&WriteTransformAsMatrix)},
        {addr::SC4Stream_WritePollutionStruct, reinterpret_cast<void*>(&WritePollutionStruct)},
        {addr::SC4Stream_WriteBitRows, reinterpret_cast<void*>(&WriteBitRows)},
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

const bool g_saveStreams2Registered = registry::Register({
    "saves.streams.part2",
    "saves",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Stream_ReadTransform,
    "Our save stream helpers, part 2: dates, stopwatches, random generators, iterators, vectors, transforms, bit rows",
    false,
    InstallSaveStreams2,
});

} // namespace
} // namespace opensc4::saves
