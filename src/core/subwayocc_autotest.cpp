// The subway occupants' part of the autotest (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/occ_vtables.h"
#include "core/occmgr_sim.h"
#include "core/subwayocc_sim.h"

#include <cIGZIStream.h>
#include <cISC4City.h>

#include <opensc4/addresses.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace opensc4::autotest {
namespace {

namespace field = subwayocc_sim::field;
namespace slot = subwayocc_sim::slot;

constexpr std::size_t kStride = subwayocc_sim::kCornerStride;

// IDs the class does not answer: the pipe's (the sibling class), the class
// before the pipe's (0xEA0A5ACF), the network occupant's class ID and one
// nobody has.
constexpr uint32_t kIidPipe = 0x49C05B9F, kIidOther = 0xEA0A5ACF, kNetworkClass = 0xC9C05C6E, kIidNobody = 0x0F10A0CC;

// The network flag AddPathConnection sets when the path connection table is
// filled (src/core/netocc_sim.h).
constexpr uint32_t kFlagPathTable = 0x2000;

struct Hash {
    uint32_t h = 2166136261u;
    void Mix(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    }
};

uint32_t Bits(float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

// How many subway pieces answered something that says the test reaches the
// paths of the functions (printed after the checksums).
struct Counters {
    uint32_t key300 = 0, key4B00 = 0, placed = 0, stored = 0, keyClass = 0, keyOther = 0;
};

// One QueryInterface through a part: the answer, and where in the object the
// answer lies (never the address); the reference it gave is released.
void AskInterface(Hash& hash, char* self, char* part, uint32_t iid)
{
    void* found = nullptr;
    const bool answered = Virtual<uint8_t>(part, 0x0, iid, &found) != 0;
    hash.Mix(answered);
    if (answered && found) {
        hash.Mix(static_cast<uint32_t>(static_cast<char*>(found) - self));
        Virtual<uint32_t>(found, slot::kRelease);
    }
}

// The number of 0x18-byte records of one of the intermediate class's vectors
// (begin, end; a signed division as the game's).
int32_t Records(char* self, std::size_t vector)
{
    return static_cast<int32_t>(At<std::uintptr_t>(self, vector + 4) - At<std::uintptr_t>(self, vector)) /
           static_cast<int32_t>(kStride);
}

// The part of a subway piece that the network occupant's walk
// (src/core/netocc_autotest.cpp, which takes every occupant of type
// 0xC772BF98, subway pieces included, and asks slot 56, the subway's own
// height, at eight points of each) does not reach: the piece key word and
// the class word the intermediate class keeps (slot 82), the rotation and
// the flip, the answers of the subway's own overrides (the descriptor and
// whether the intermediate class stored it at +0x26C, the default flags,
// slot 102, the virtual base's piece key), IsPlaced, the class ID of the
// serializable part, the occupant type, the saved vector at +0x1B4 that
// slot 106 copies (its count and up to eight records) and QueryInterface
// through the interface part and the occupant part for the subway's IID,
// the intermediate class's, the network occupant's, 0xA9764C8C, 1, the
// pipe's and 0xEA0A5ACF (neither answered) and one nobody answers (each
// reference given is released).
//
// Not mixed, because they follow the view: the four corner records at
// +0x270 (the constructor leaves them as the pool's memory holds them and
// slot 106 makes them only when the path information is made), whether the
// path information is there, the byte at +0x262 and the vector at +0x1E4
// (the geometry, made with the path information), the dirty byte; and
// pointers and reference counts.
uint32_t SubwayHash(char* self, Counters& counters)
{
    Hash hash;
    char* const part = self + field::kOccupant;
    const uint32_t key = At<uint32_t>(self, field::kPieceKey);
    hash.Mix(key);
    counters.key300 += key == subwayocc_sim::kKeySpecialA;
    counters.key4B00 += key == subwayocc_sim::kKeySpecialB;
    hash.Mix(Virtual<uint32_t>(self, slot::kGetPieceType));
    hash.Mix(Virtual<uint32_t>(self, slot::kGetRotation));
    hash.Mix(Virtual<uint32_t>(self, slot::kGetFlip));
    const std::uintptr_t descriptor = Virtual<std::uintptr_t>(self, slot::kGetDescriptor);
    hash.Mix(descriptor == addr::cSC4SubwayOccupant_Descriptor);
    const bool stored = At<std::uintptr_t>(self, field::kDescriptor) == descriptor;
    hash.Mix(stored);
    counters.stored += stored;
    hash.Mix(Virtual<uint32_t>(self, slot::kGetDefaultFlags));
    hash.Mix(Virtual<uint32_t>(self, slot::kGetSeven));
    uint32_t pieceKey = 0;
    Virtual<uint32_t>(self + field::kVbase, subwayocc_sim::kVbaseGetPieceKey, &pieceKey);
    hash.Mix(pieceKey);
    if (pieceKey == subwayocc_sim::kClassId)
        ++counters.keyClass;
    else
        ++counters.keyOther;
    const bool placed = Virtual<uint8_t>(self, slot::kIsPlaced) != 0;
    hash.Mix(placed);
    counters.placed += placed;
    hash.Mix(Virtual<uint32_t>(self + field::kSerializable, slot::kGetGZCLSID));
    hash.Mix(Virtual<uint32_t>(part, slot::kGetType));
    const int32_t records = At<char*>(self, field::kPlain) ? Records(self, field::kPlain) : 0;
    hash.Mix(static_cast<uint32_t>(records));
    for (int32_t i = 0; i < records && i < 8; ++i)
        for (std::size_t offset = 0; offset < kStride; offset += 4)
            hash.Mix(At<uint32_t>(At<char*>(self, field::kPlain), static_cast<std::size_t>(i) * kStride + offset));
    const uint32_t ids[] = {subwayocc_sim::kIidSubway,
                            subwayocc_sim::kIidUnderground,
                            subwayocc_sim::kIidNetworkOccupant,
                            subwayocc_sim::kIidObject,
                            1u,
                            kIidPipe,
                            kIidOther,
                            kIidNobody};
    for (char* const asked : {self, part})
        for (const uint32_t iid : ids)
            AskInterface(hash, self, asked, iid);
    return hash.h;
}

// The statics the first Init sets up (0x00653640): the ten floats from
// 0x00B467F8 and the done byte. Zero before any subway piece was
// initialised.
uint32_t StaticsHash()
{
    Hash hash;
    const std::uintptr_t statics = addr::cSC4SubwayOccupant_Statics;
    for (std::size_t offset = 0; offset < subwayocc_sim::kStaticsDone; offset += 4)
        hash.Mix(Global<uint32_t>(statics + offset));
    hash.Mix(Global<uint8_t>(statics + subwayocc_sim::kStaticsDone));
    return hash.h;
}

// Every subway piece of the city (build 641; src/core/subwayocc_sim.h),
// found through the occupant manager (the city's slot 34 / +0x88): every
// occupant of the network occupant type 0xC772BF98 in its grid, one in
// several cells once, whose serializable part answers the subway's class
// ID. The objects are the interface part (the occupant part less 4), in the
// order of the cell the intermediate class keeps (x at +0x23C, then z at
// +0x240), so that what is done to them one after the other does not follow
// the order of the walk. False without a manager.
bool Collect(cISC4City* city, std::vector<char*>& found)
{
    if (!game::IsBuild641())
        return false;
    char* const manager = Virtual<char*>(city, 0x88);
    if (!manager || !At<uint8_t>(manager, occmgr_sim::field::kInitialized))
        return false;
    std::unordered_set<char*> seen;
    Virtual<void>(manager + occmgr_sim::field::kLock, 0x4);
    const occmgr_sim::Vector* const grid = occmgr_sim::Grid(manager);
    const int32_t columns = static_cast<int32_t>((grid->end - grid->begin) / 12);
    for (int32_t x = 0; x < columns; ++x) {
        const auto* const column = reinterpret_cast<const occmgr_sim::Vector*>(grid->begin + x * 12);
        const int32_t depth = static_cast<int32_t>((column->end - column->begin) >> 2);
        for (int32_t z = 0; z < depth; ++z) {
            const occmgr_sim::Cell* const cell = occmgr_sim::CellAt(manager, x, z);
            if (!cell)
                continue;
            for (const occmgr_sim::ListNode* node = cell->head->next; node != cell->head; node = node->next) {
                void* const object = occmgr_sim::OccupantOf(node);
                if (Virtual<uint32_t>(object, slot::kGetType) != subwayocc_sim::kOccupantType)
                    continue;
                char* const self = static_cast<char*>(object) - field::kOccupant;
                if (Virtual<uint32_t>(self + field::kSerializable, slot::kGetGZCLSID) == subwayocc_sim::kClassId)
                    seen.insert(self);
            }
        }
    }
    Virtual<void>(manager + occmgr_sim::field::kLock, 0x8);
    found.assign(seen.begin(), seen.end());
    const auto position = [](char* self) {
        return std::make_pair(At<int32_t>(self, field::kCellX), At<int32_t>(self, field::kCellZ));
    };
    std::stable_sort(found.begin(), found.end(), [&](char* a, char* b) { return position(a) < position(b); });
    return true;
}

// The four corner records at +0x270, word by word.
void MixCorners(Hash& hash, char* self)
{
    for (std::size_t offset = 0; offset < 4 * kStride; offset += 4)
        hash.Mix(At<uint32_t>(self, field::kCorners + offset));
}

// Slot 56 (the height, a double in ST0, mixed as the float it is) at ten
// points given as offsets from corner 0's x and z: the four corners, the
// middle, two points in each of the two triangles and two near the edges.
// Only for a piece whose corner records are made.
void MixAltitudes(Hash& hash, char* self)
{
    constexpr float kPoints[][2] = {{0, 0}, {0, 16}, {16, 16}, {16, 0}, {8, 8},
                                    {4, 12}, {12, 4}, {2, 3}, {15, 1}, {1, 15}};
    const float x0 = At<float>(self, field::kCorners);
    const float z0 = At<float>(self, field::kCorners + 8);
    for (const auto& point : kPoints)
        hash.Mix(Bits(static_cast<float>(Virtual<double>(self, slot::kGetAltitude, x0 + point[0], z0 + point[1]))));
}

// Whether slot 106 can be called on the piece as it is: the game's function
// copies four records of the vector at +0x1B4 without looking for them, and
// for the piece keys 0x300 and 0x4B00 reads up to the twelfth record of the
// vector at +0x1E4 once that has eight.
bool CornersCallable(char* self)
{
    if (!At<char*>(self, field::kPlain) || Records(self, field::kPlain) < 4)
        return false;
    const uint32_t key = At<uint32_t>(self, field::kPieceKey);
    if (key != subwayocc_sim::kKeySpecialA && key != subwayocc_sim::kKeySpecialB)
        return true;
    const int32_t wide = At<char*>(self, field::kWide) ? Records(self, field::kWide) : 0;
    return wide < 8 || wide >= 12;
}

// What the day hook did to the city's pieces, for its log line.
struct Driven {
    uint32_t hadInfo = 0, made = 0, noInfo = 0, points = 0, agree = 0, corners = 0;
};

// The paths of a piece's path information (its slot 0x78 answers the hash
// table GetPathInfo walks: the buckets are the words between +4 and +8; a
// node has the next one at +0, its key at +4, the start and the end of its
// 12-byte points at +8 and +0xC and its length, a float, at +0x14): the sum
// over the nodes of a checksum of the key, the points' three words each and
// the length (no order, no address). For every point slot 56 is asked again
// for its x and z, and the points whose stored height is that answer are
// counted.
uint32_t PathsOf(char* self, void* info, Driven& driven)
{
    char* const table = Virtual<char*>(info, 0x78);
    if (!table)
        return 0;
    uint32_t sum = 0;
    for (char* bucket = At<char*>(table, 0x4); bucket != At<char*>(table, 0x8); bucket += 4)
        for (char* node = At<char*>(bucket, 0x0); node; node = At<char*>(node, 0x0)) {
            Hash hash;
            hash.Mix(At<uint32_t>(node, 0x4));
            for (char* point = At<char*>(node, 0x8); point != At<char*>(node, 0xC); point += 0xC) {
                for (std::size_t offset = 0; offset < 0xC; offset += 4)
                    hash.Mix(At<uint32_t>(point, offset));
                const double height =
                    Virtual<double>(self, slot::kGetAltitude, At<float>(point, 0x0), At<float>(point, 0x8));
                ++driven.points;
                driven.agree += Bits(static_cast<float>(height)) == At<uint32_t>(point, 0x4);
            }
            hash.Mix(At<uint32_t>(node, 0x14));
            sum += hash.h;
        }
    return sum;
}

// The exercise of one subway piece of the city (OPENSC4_TEST_SUBWAYOCC=<day>).
//   - The virtual base's piece key with the network flags 0x06000000 both
//     cleared (the class ID) and with 0x02000000 set (the word at +0xFC):
//     the flags word is written and put back (the interface's flag setters
//     are the intermediate class's and do more).
//   - With `paths`: GetPathInfo twice (the second answer is the first's).
//     A piece that had no path information gets it made here: slot 86 when
//     the geometry is not made, slot 106 (the corner records; for the keys
//     0x300 and 0x4B00 through slot 99 and the vector at +0x1E4), the path
//     resource, every path point's height and every path's length. Then
//     the corner records, the height at ten points, the paths (PathsOf) and
//     the eight words of the path connection table are mixed; slot 106 is
//     called once more (when CornersCallable) and the corner records mixed
//     again. Then the table's words are put back, and for a piece that had
//     no path information before it is dropped (ReleasePathInfo), the
//     corner records are put back as they were and the network flag 0x2000
//     (set when the table is filled) is cleared if it was clear. What slot
//     86 did cannot be undone: it makes the piece's geometry once (the byte
//     at +0x262), which loads its textures, fills the vectors and turns the
//     transform, as the game does the first time anything asks a loaded
//     piece for its path information. Whether the information was there
//     before is counted for the log line and not mixed: that follows the
//     view.
//   - Without `paths` (for bisecting): only slot 106 on a piece that is
//     neither of the two keys, the corner records mixed and put back.
uint32_t Exercise(char* self, bool paths, Driven& driven)
{
    Hash hash;
    const uint32_t flags = At<uint32_t>(self, field::kFlags);
    for (const uint32_t value : {flags & ~0x06000000u, (flags & ~0x06000000u) | 0x02000000u}) {
        At<uint32_t>(self, field::kFlags) = value;
        uint32_t key = 0;
        Virtual<uint32_t>(self + field::kVbase, subwayocc_sim::kVbaseGetPieceKey, &key);
        hash.Mix(key);
    }
    At<uint32_t>(self, field::kFlags) = flags;

    uint32_t corners[4 * kStride / 4];
    std::memcpy(corners, self + field::kCorners, sizeof corners);
    if (!paths) {
        const uint32_t key = At<uint32_t>(self, field::kPieceKey);
        if (key != subwayocc_sim::kKeySpecialA && key != subwayocc_sim::kKeySpecialB && CornersCallable(self)) {
            Virtual<void>(self, slot::kUpdateCorners);
            MixCorners(hash, self);
            ++driven.corners;
            std::memcpy(self + field::kCorners, corners, sizeof corners);
        }
        return hash.h;
    }

    uint32_t table[8];
    for (std::size_t i = 0; i < 8; ++i)
        table[i] = At<uint32_t>(self, field::kPathTable + 4 * i);
    const bool hadInfo = At<void*>(self, field::kPathInfo) != nullptr;
    void* const info = Virtual<void*>(self, slot::kGetPathInfo);
    hash.Mix(info != nullptr);
    hash.Mix(Virtual<void*>(self, slot::kGetPathInfo) == info);
    hash.Mix(At<void*>(self, field::kPathInfo) == info);
    if (hadInfo)
        ++driven.hadInfo;
    else if (info)
        ++driven.made;
    else
        ++driven.noInfo;
    // Without path information and with none made the corner records were
    // still made by slot 106 (GetPathInfo calls it before it asks for the
    // resource); with information that was there they are as its making
    // left them.
    MixCorners(hash, self);
    MixAltitudes(hash, self);
    if (info)
        hash.Mix(PathsOf(self, info, driven));
    for (std::size_t i = 0; i < 8; ++i)
        hash.Mix(At<uint32_t>(self, field::kPathTable + 4 * i));
    if (CornersCallable(self)) {
        Virtual<void>(self, slot::kUpdateCorners);
        MixCorners(hash, self);
        ++driven.corners;
    }
    for (std::size_t i = 0; i < 8; ++i)
        At<uint32_t>(self, field::kPathTable + 4 * i) = table[i];
    if (!hadInfo) {
        Virtual<void>(self, slot::kReleasePathInfo);
        std::memcpy(self + field::kCorners, corners, sizeof corners);
        if (!(flags & kFlagPathTable))
            At<uint32_t>(self, field::kFlags) &= ~kFlagPathTable;
    }
    return hash.h;
}

// An input stream over a few bytes, as src/core/save_autotest.cpp's: a read
// past the end stores nothing, fails and leaves the stream in error. It
// answers no interface.
class ByteIStream final : public cIGZIStream {
public:
    explicit ByteIStream(std::vector<uint8_t> data) : bytes(std::move(data)) {}
    bool QueryInterface(uint32_t, void**) override { return false; }
    uint32_t AddRef() override { return 1; }
    uint32_t Release() override { return 1; }
    bool Skip(uint32_t count) override
    {
        if (bytes.size() - at < count)
            return Fail();
        at += count;
        return true;
    }
    bool GetSint8(int8_t& v) override { return Get(v); }
    bool GetUint8(uint8_t& v) override { return Get(v); }
    bool GetSint16(int16_t& v) override { return Get(v); }
    bool GetUint16(uint16_t& v) override { return Get(v); }
    bool GetSint32(int32_t& v) override { return Get(v); }
    bool GetUint32(uint32_t& v) override { return Get(v); }
    bool GetSint64(int64_t& v) override { return Get(v); }
    bool GetUint64(uint64_t& v) override { return Get(v); }
    bool GetFloat32(float& v) override { return GetVoid(&v, sizeof v); }
    bool GetFloat64(double& v) override { return GetVoid(&v, sizeof v); }
    bool GetRZCharStr(char*, uint32_t) override { return Fail(); }
    bool GetGZStr(cIGZString&) override { return Fail(); }
    bool GetGZSerializable(cIGZSerializable&) override { return Fail(); }
    bool GetVoid(void* out, uint32_t size) override
    {
        if (bytes.size() - at < size)
            return Fail();
        std::memcpy(out, bytes.data() + at, size);
        at += size;
        return true;
    }
    int32_t GetError() override { return error; }
    int32_t SetUserData(cIGZVariant*) override { return 0; }
    int32_t GetUserData() override { return 0; }

    std::vector<uint8_t> bytes;
    std::size_t at = 0;
    int32_t error = 0;

private:
    bool Fail()
    {
        error = 1;
        return false;
    }
    template <typename T>
    bool Get(T& value)
    {
        return GetVoid(&value, sizeof value);
    }
};

// Read on a made object, with streams that end before anything is stored in
// the object: through the serializable part's slot 4 and through the
// holder's thunk (slot 24, this - 4).
//   - version 1, version 3 and an empty stream (the version's slot then
//     keeps the low word of the stream's address, a multiple of four, so
//     never 2): Read asks the stream for one uint16 and answers AL 0;
//   - version 2 and then 4: the intermediate class's Read (0x0065E510) is
//     called, reads its own version and refuses it (AL 0, nothing else
//     read), so the dirty byte stays and the answer is the stream's state,
//     1;
//   - version 2 and nothing else: the intermediate class's read of its
//     version fails (its slot keeps the low word of the stream's address
//     too, never 3), the stream is in error and the answer is 0.
// The bytes taken, the stream's error and the dirty byte are mixed with the
// answer.
void ReadRefusals(Hash& hash, char* self)
{
    const std::vector<uint8_t> streams[] = {
        {1, 0, 0xEE, 0xEE}, {3, 0, 0xEE, 0xEE}, {}, {2, 0, 4, 0, 0xEE, 0xEE}, {2, 0}};
    for (const std::vector<uint8_t>& bytes : streams)
        for (const bool throughHolder : {false, true}) {
            ByteIStream in(bytes);
            cIGZIStream* const stream = &in;
            if (throughHolder)
                hash.Mix(Virtual<uint8_t>(self + field::kHolder, slot::kHolderRead, static_cast<void*>(stream)));
            else
                hash.Mix(Virtual<uint8_t>(self + field::kSerializable, slot::kRead, static_cast<void*>(stream)));
            hash.Mix(static_cast<uint32_t>(in.at));
            hash.Mix(static_cast<uint32_t>(in.error));
            hash.Mix(At<uint8_t>(self, field::kDirty));
        }
}

// One corner record of the test's own: three floats, a word of four bytes
// and two more floats, all different for every record `i` of table `base`.
void FillRecord(uint32_t* record, uint32_t base, uint32_t i)
{
    const float x = static_cast<float>(base) + static_cast<float>(i) * 16.0f;
    const float y = static_cast<float>(base) * 0.5f + static_cast<float>(i * i) * 0.375f + static_cast<float>(i);
    const float z = static_cast<float>(base) * 2.0f + static_cast<float>(i) * 3.0f;
    record[0] = Bits(x);
    record[1] = Bits(y);
    record[2] = Bits(z);
    record[3] = 0x01020300u + base + i;
    record[4] = Bits(static_cast<float>(i) * 0.25f);
    record[5] = Bits(1.0f - static_cast<float>(i) * 0.125f);
}

// Slot 106 on a made object, whose two vectors (+0x1B4 and +0x1E4, empty
// after construction) are pointed at tables of the test's own for the
// calls and put back before anything else happens to the object (the
// destructor frees what they point at):
//   - a piece key that is neither 0x300 nor 0x4B00: the four records of the
//     first vector;
//   - key 0x300 with the second vector empty: slot 99 is asked (the
//     intermediate class's 0x0065A200, which answers 0 at once for an object
//     whose class word at +0x264 is not the subway's: a made object's is 0),
//     then the four records of the first vector again;
//   - the second vector with twelve records, the rotation 0 to 3 and 2 with
//     the flip bit, the keys 0x300 and 0x4B00 in turn: records 4, 5, 7 and
//     6 turned by the rotation and, slot 99 answering 0, lowered by the
//     differences with records 0, 1, 11 and 10.
// The corner records are mixed after each call; after the last the height
// (slot 56) at ten points. Not reached: the branch of slot 99 answering
// something else (records turned, not lowered), which wants a piece in the
// network with neighbours.
void MadeCorners(Hash& hash, char* self)
{
    uint32_t plain[4 * 6];
    uint32_t wide[12 * 6];
    for (uint32_t i = 0; i < 4; ++i)
        FillRecord(plain + 6 * i, 96, i);
    for (uint32_t i = 0; i < 12; ++i)
        FillRecord(wide + 6 * i, 320, i);
    const uint32_t oldKey = At<uint32_t>(self, field::kPieceKey);
    const uint8_t oldRotation = At<uint8_t>(self, field::kRotation);
    std::uintptr_t oldVectors[6];
    for (std::size_t i = 0; i < 3; ++i) {
        oldVectors[i] = At<std::uintptr_t>(self, field::kPlain + 4 * i);
        oldVectors[3 + i] = At<std::uintptr_t>(self, field::kWide + 4 * i);
    }
    const auto point = [self](std::size_t vector, uint32_t* table, std::size_t words) {
        const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(table);
        At<std::uintptr_t>(self, vector) = begin;
        At<std::uintptr_t>(self, vector + 4) = begin + words * 4;
        At<std::uintptr_t>(self, vector + 8) = begin + words * 4;
    };
    const auto update = [&](uint32_t key, uint8_t rotation) {
        At<uint32_t>(self, field::kPieceKey) = key;
        At<uint8_t>(self, field::kRotation) = rotation;
        Virtual<void>(self, slot::kUpdateCorners);
        MixCorners(hash, self);
    };
    point(field::kPlain, plain, 4 * 6);
    update(0x1234u, 0);
    update(subwayocc_sim::kKeySpecialA, 1);
    point(field::kWide, wide, 12 * 6);
    for (uint8_t rotation = 0; rotation < 4; ++rotation)
        update((rotation & 1) ? subwayocc_sim::kKeySpecialB : subwayocc_sim::kKeySpecialA, rotation);
    update(subwayocc_sim::kKeySpecialB, 0x82);
    MixAltitudes(hash, self);
    for (std::size_t i = 0; i < 3; ++i) {
        At<std::uintptr_t>(self, field::kPlain + 4 * i) = oldVectors[i];
        At<std::uintptr_t>(self, field::kWide + 4 * i) = oldVectors[3 + i];
    }
    At<uint8_t>(self, field::kRotation) = oldRotation;
    At<uint32_t>(self, field::kPieceKey) = oldKey;
}

// Slot 56 on a made object whose corner records are a tile of the test's
// own (corner 0 at (64, 128), heights 10, 12.5, 17.25 and 11): the height
// at points of both triangles, on the diagonal, on the corners and outside
// the tile, each answer mixed as the float it is.
void MadeAltitudes(Hash& hash, char* self)
{
    constexpr float kTile[4][3] = {{64, 10, 128}, {64, 12.5f, 144}, {80, 17.25f, 144}, {80, 11, 128}};
    for (std::size_t corner = 0; corner < 4; ++corner)
        for (std::size_t i = 0; i < 3; ++i)
            At<float>(self, field::kCorners + corner * kStride + 4 * i) = kTile[corner][i];
    MixAltitudes(hash, self);
    constexpr float kMore[][2] = {{64.5f, 143.25f}, {79.75f, 128.5f}, {72, 136}, {48, 100}, {100, 160}, {70.125f, 70}};
    for (const auto& at : kMore)
        hash.Mix(Bits(static_cast<float>(Virtual<double>(self, slot::kGetAltitude, at[0], at[1]))));
}

void* Create()
{
    return reinterpret_cast<void*(__cdecl*)()>(addr::cSC4SubwayOccupant_Create)();
}

// The eight vtable and table pointers the constructor stores.
void MixVtables(Hash& hash, char* self)
{
    hash.Mix(occ_vtables::IsTable(At<std::uintptr_t>(self, 0x0), addr::cSC4SubwayOccupant_vftable));
    hash.Mix(occ_vtables::IsTable(At<std::uintptr_t>(self, field::kOccupant), addr::cSC4SubwayOccupant_vftable_4));
    hash.Mix(occ_vtables::IsTable(At<std::uintptr_t>(self, field::kSerializable), addr::cSC4SubwayOccupant_vftable_8));
    hash.Mix(occ_vtables::IsTable(At<std::uintptr_t>(self, field::kHolder), addr::cSC4SubwayOccupant_vftable_C));
    hash.Mix(occ_vtables::IsTable(At<std::uintptr_t>(self, field::kExemplarPart), addr::cSC4SubwayOccupant_vftable_20));
    hash.Mix(occ_vtables::IsTable(At<std::uintptr_t>(self, field::kModelPart), addr::cSC4SubwayOccupant_vftable_38));
    hash.Mix(occ_vtables::IsTable(At<std::uintptr_t>(self, field::kVbase), addr::cSC4SubwayOccupant_vbase_vftable));
    hash.Mix(At<std::uintptr_t>(self, field::kVbtable) == addr::cSC4SubwayOccupant_vbtable);
    hash.Mix(At<uint32_t>(self, field::kVbaseShift));
}

// The class's life on made objects (OPENSC4_TEST_SUBWAYOCC=<day>), which are
// in no cell and no network.
//   - `self` from the game's factory (0x00653560: the pool, the constructor;
//     it returns the occupant part), one reference held by the test;
//     GetGZCLSID of the serializable part; QueryInterface through the parts
//     that answer to it (the interface, the occupant part, the serializable
//     part, the holder, the exemplar part and the virtual base, whose slot 0
//     is a thunk to it) for the IDs the class answers (its own, the
//     intermediate class's, the network occupant's, 0xA9764C8C, 1, the
//     occupant, the serializable and 0x26A68449) and some it does not (the
//     network occupant's class ID, the pipe's, 0xEA0A5ACF, one nobody has).
//   - the fields the constructors set: the eight vtable and table pointers
//     and the virtual base's shift, the network flags, the class word, the
//     dirty and the geometry bytes; the descriptor, default flags and slot
//     102 answers.
//   - the virtual base's piece key with the flags 0x06000000 clear (the
//     class ID) and with 0x04000000 set (the word at +0xFC, given a value),
//     both put back.
//   - slot 56 (MadeAltitudes), slot 106 (MadeCorners), and GetPathInfo with
//     a path information pointer of the test's own in place for the call
//     (it answers the pointer and does nothing else).
//   - both Inits on the object marked initialised (the branch that leaves
//     the object alone: they only look at the statics); Read's refusals
//     (ReadRefusals).
//   - a second object made as the maker class does (0x00653A8B): the pool
//     allocation 0x00652FE0 with the size, then the constructor with 1; its
//     pointers mixed, then released.
//   - with `pool`: 90 more objects from the factory (a page holds 90, so the
//     pool takes another page, through its Grow and the page set-up
//     0x00652E00), how many of them are not on `self`'s page, then all
//     released, last made first (pages emptied and given back, a full one
//     opened again at 67 used).
// The last Release destroys `self` through the deleting destructor and
// returns the item to its page.
// Not called: the Inits of an object not initialised, which end in the
// occupant message 0x99EF1140 (queued, with the made object as its object):
// listeners would meet an occupant that is in no cell and no network.
// GetPathInfo's making of the information: it starts with slot 86, the
// geometry of a piece in the network. Write and Read of a whole record: no
// autotest makes the game's memory stream, and the record holds the
// property holder's, the network occupant's and the intermediate class's
// parts, whose Reads end in the model's state; they run when the Rush Hour
// city is loaded and saved. Not mixed: the corner records and the
// descriptor at +0x26C as the constructors leave them (the pool's memory).
uint32_t Life(bool pool, uint32_t& elsewhere)
{
    Hash hash;
    char* const part = static_cast<char*>(Create());
    if (!part)
        return 0;
    char* const self = part - field::kOccupant;
    Virtual<uint32_t>(self, slot::kAddRef);
    hash.Mix(Virtual<uint32_t>(self + field::kSerializable, slot::kGetGZCLSID));
    const uint32_t ids[] = {subwayocc_sim::kIidSubway,
                            subwayocc_sim::kIidUnderground,
                            subwayocc_sim::kIidNetworkOccupant,
                            subwayocc_sim::kIidObject,
                            1u,
                            subwayocc_sim::kIidOccupant,
                            subwayocc_sim::kIidSerializable,
                            0x26A68449u,
                            kNetworkClass,
                            kIidPipe,
                            kIidOther,
                            kIidNobody};
    for (const std::size_t offset : {std::size_t{0}, field::kOccupant, field::kSerializable, field::kHolder,
                                     field::kExemplarPart, field::kVbase})
        for (const uint32_t iid : ids)
            AskInterface(hash, self, self + offset, iid);
    MixVtables(hash, self);
    hash.Mix(At<uint32_t>(self, field::kFlags));
    hash.Mix(At<uint32_t>(self, field::kClassWord));
    hash.Mix(At<uint8_t>(self, field::kDirty));
    hash.Mix(At<uint8_t>(self, field::kBuilt));
    hash.Mix(Virtual<std::uintptr_t>(self, slot::kGetDescriptor) == addr::cSC4SubwayOccupant_Descriptor);
    hash.Mix(Virtual<uint32_t>(self, slot::kGetDefaultFlags));
    hash.Mix(Virtual<uint32_t>(self, slot::kGetSeven));

    const uint32_t flags = At<uint32_t>(self, field::kFlags);
    const uint32_t word = At<uint32_t>(self, field::kPieceKey);
    At<uint32_t>(self, field::kPieceKey) = 0x00C0FFEEu;
    for (const uint32_t value : {flags & ~0x06000000u, (flags & ~0x06000000u) | 0x04000000u}) {
        At<uint32_t>(self, field::kFlags) = value;
        uint32_t key = 0;
        Virtual<uint32_t>(self + field::kVbase, subwayocc_sim::kVbaseGetPieceKey, &key);
        hash.Mix(key);
    }
    At<uint32_t>(self, field::kFlags) = flags;
    At<uint32_t>(self, field::kPieceKey) = word;

    MadeAltitudes(hash, self);
    MadeCorners(hash, self);

    void* const oldInfo = At<void*>(self, field::kPathInfo);
    uint32_t marker = 0;
    At<void*>(self, field::kPathInfo) = &marker;
    hash.Mix(Virtual<void*>(self, slot::kGetPathInfo) == static_cast<void*>(&marker));
    At<void*>(self, field::kPathInfo) = oldInfo;

    At<uint8_t>(self, field::kState) |= 1;
    hash.Mix(Virtual<uint8_t>(part, slot::kOccupantInit));
    uint32_t description[16] = {};
    hash.Mix(Virtual<uint8_t>(self, slot::kInit, static_cast<uint32_t*>(description)));
    At<uint8_t>(self, field::kState) &= 0xFE;
    ReadRefusals(hash, self);

    void* const raw = reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::cSC4SubwayOccupant_Allocate)(
        subwayocc_sim::kObjectSize);
    char* const other = raw ? reinterpret_cast<char*(__thiscall*)(void*, uint32_t)>(
                                  addr::cSC4SubwayOccupant_cSC4SubwayOccupant)(raw, 1u)
                            : nullptr;
    hash.Mix(other != nullptr);
    if (other) {
        Virtual<uint32_t>(other, slot::kAddRef);
        MixVtables(hash, other);
        hash.Mix(At<uint32_t>(other, field::kRefCount));
        Virtual<uint32_t>(other, slot::kRelease);
    }

    if (pool) {
        const std::uintptr_t page = reinterpret_cast<std::uintptr_t>(self) & subwayocc_sim::kPageMask;
        std::vector<char*> made;
        for (uint32_t i = 0; i < subwayocc_sim::kPageItems; ++i) {
            char* const more = static_cast<char*>(Create());
            if (!more)
                break;
            char* const object = more - field::kOccupant;
            Virtual<uint32_t>(object, slot::kAddRef);
            elsewhere += (reinterpret_cast<std::uintptr_t>(object) & subwayocc_sim::kPageMask) != page;
            made.push_back(object);
        }
        hash.Mix(static_cast<uint32_t>(made.size()));
        while (!made.empty()) {
            Virtual<uint32_t>(made.back(), slot::kRelease);
            made.pop_back();
        }
    }
    hash.Mix(At<uint32_t>(self, field::kRefCount));
    Virtual<uint32_t>(self, slot::kRelease);
    return hash.h;
}

// The exercise's results (OPENSC4_TEST_SUBWAYOCC=<day>): the made objects'
// checksum, the checksum summed over the city's pieces driven and how many
// were driven.
uint32_t g_lifeSum = 0;
uint32_t g_setSum = 0;
uint32_t g_setCount = 0;
bool g_setDone = false;

// OPENSC4_TEST_SUBWAYOCC=<day>: on that day made objects go through Life
// (its pool part unless OPENSC4_TEST_SUBWAYOCC_POOL=0) and every subway
// piece of the city, in the order of its cell, through Exercise (subway
// pieces are few: none is left out, so nothing is picked; its path part
// unless OPENSC4_TEST_SUBWAYOCC_PATHS=0). Both switches are for bisecting.
// The checksums are in the fingerprint's ` set` field from then on, and one
// log line says what ran, also when the city has no subway. How many pieces
// are on another page is in the log line only: it depends on how full the
// pool's pages were.
void Drive(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_SUBWAYOCC") || !game::IsBuild641() || g_setDone)
        return;
    g_setDone = true;
    std::vector<char*> objects;
    const bool collected = Collect(day.city, objects);
    const bool pool = EnvInt("OPENSC4_TEST_SUBWAYOCC_POOL", 1) != 0;
    const bool paths = EnvInt("OPENSC4_TEST_SUBWAYOCC_PATHS", 1) != 0;
    uint32_t elsewhere = 0;
    g_lifeSum = Life(pool, elsewhere);
    Driven driven;
    for (char* const self : objects) {
        g_setSum += Exercise(self, paths, driven);
        ++g_setCount;
    }
    log::Write("autotest: subwayocc: made objects %08X (pool part %s, %u on another page), %u subway pieces of the "
               "city driven %08X (path part %s: %u had path information, %u got it made, %u got none, %u path points "
               "of which %u have slot 56's height, slot 106 called on %u)%s",
               g_lifeSum, pool ? "on" : "off", elsewhere, g_setCount, g_setSum, paths ? "on" : "off", driven.hadInfo,
               driven.made, driven.noInfo, driven.points, driven.agree, driven.corners,
               collected ? "" : " (no manager)");
}

// The fingerprint part: the number of subway pieces and the sum and the
// exclusive or of SubwayHash over them (order independent; no addresses),
// then the counters (pieces with the key 0x300 and 0x4B00, placed, with the
// descriptor stored, whose piece key is the class ID or the word) and the
// statics' checksum, and after OPENSC4_TEST_SUBWAYOCC's day ` set <made
// objects' checksum>,<the city pieces' checksum>,<count>`.
void Fingerprint(cISC4City* city, std::string& line)
{
    std::vector<char*> objects;
    if (!Collect(city, objects)) {
        line += " | subwayocc none";
        return;
    }
    uint32_t sum = 0, mixed = 0;
    Counters counters;
    for (char* const self : objects) {
        const uint32_t hash = SubwayHash(self, counters);
        sum += hash;
        mixed ^= hash;
    }
    char text[256];
    std::snprintf(text, sizeof text,
                  " | subwayocc %u,%08X,%08X k300 %u k4b %u placed %u stored %u keyc %u keyo %u st %08X",
                  static_cast<unsigned>(objects.size()), sum, mixed, counters.key300, counters.key4B00,
                  counters.placed, counters.stored, counters.keyClass, counters.keyOther, StaticsHash());
    line += text;
    if (g_setDone) {
        std::snprintf(text, sizeof text, " set %08X,%08X,%u", g_lifeSum, g_setSum, static_cast<unsigned>(g_setCount));
        line += text;
    }
}

const FingerprintPart kFingerprint{"subwayocc", 130, &Fingerprint};
const DayHook kDrive{"subwayocc.drive", 193, &Drive};

} // namespace
} // namespace opensc4::autotest
