// The power poles' part of the autotest (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/occbase_sim.h"
#include "core/occmgr_sim.h"
#include "core/powerpoleocc_sim.h"

#include <cISC4City.h>

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_set>
#include <vector>

namespace opensc4::autotest {
namespace {

namespace field = powerpoleocc_sim::field;
namespace slot = powerpoleocc_sim::slot;
namespace occupant = powerpoleocc_sim::occupant;
namespace base = occbase_sim::field;

struct Hash {
    uint32_t h = 2166136261u;
    void Mix(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    }
    void MixFloat(float v) { Mix(BytesChecksum(&v, 4)); }
};

// How many poles answered something that says the test reaches the paths of
// the functions (printed after the checksums).
struct Counters {
    uint32_t withRecords = 0, withItems = 0, withPoints = 0, withCorners = 0, flag112 = 0, byte118 = 0;
    uint32_t listening = 0, bool110 = 0, linked = 0;
};

// The signed count of items of the vector at `vector`.
int32_t Items(char* vector, int32_t size)
{
    return static_cast<int32_t>(At<std::uintptr_t>(vector, 4) - At<std::uintptr_t>(vector, 0)) / size;
}

// A vector of vectors of three floats (a record's +0x14 and +0x20): every
// inner count and every float, as bits. How many inner vectors had points
// is added to `points`.
void MixOuter(Hash& hash, char* outer, uint32_t& points)
{
    const int32_t count = Items(outer, 12);
    hash.Mix(static_cast<uint32_t>(count));
    for (int32_t j = 0; j < count && j < 4096; ++j) {
        char* const inner = At<char*>(outer, 0) + 12 * j;
        const int32_t inside = Items(inner, 12);
        hash.Mix(static_cast<uint32_t>(inside));
        if (inside > 0)
            ++points;
        for (int32_t k = 0; k < inside && k < 4096; ++k)
            for (std::size_t i = 0; i < 3; ++i)
                hash.Mix(At<uint32_t>(At<char*>(inner, 0), 12 * k + 4 * i));
    }
}

// A corner vector: the count and the floats of each record, as bits (not the
// four bytes at +0xC to +0xF, which the lighting changes with the view).
void MixCorners(Hash& hash, char* vector)
{
    const int32_t count = Items(vector, 0x18);
    hash.Mix(static_cast<uint32_t>(count));
    for (int32_t i = 0; i < count && i < 4096; ++i)
        for (const std::size_t offset : {std::size_t{0x0}, std::size_t{0x4}, std::size_t{0x8}, std::size_t{0x10},
                                         std::size_t{0x14}})
            hash.Mix(At<uint32_t>(At<char*>(vector, 0), 0x18 * i + offset));
}

// What the save record holds, from the object's fields (src/core/powerpoleocc_sim.cpp,
// tools/re/save_schemas/powerpoleocc.py), without addresses: the transform's
// two bytes and its thirteen words, the position, each record (the other
// tower only as present or not, the number of list items, the two bools, the
// two integers, the two vectors of vectors), the words at +0xB8 and from
// +0xC0, the three corner vectors, the float at +0x10C, the bool at +0x110,
// the float at +0x64, the byte at +0x112; the byte at +0x118 (set only by
// its setter); of the occupant base the type, the occupant manager's cells,
// the removal counts, the initialized and read-from-a-save bits, the flags
// as they are saved and the IDs of the holder's own properties.
//
// Left out because the view drives them, or as pointers: the highlight
// (the bytes at +0x54 and +0x55 and the base's highlight bits), the flags
// 0x02 and 0x20, the placeable object, the objects at +0xBC and +0x114, the
// table pointer at +0xE4, the bounds (worked out by GetBoundingBox, which
// the queries call), the listening byte, and the reference count.
uint32_t PoleHash(char* self, Counters& counters, bool made)
{
    Hash hash;
    hash.Mix(At<uint8_t>(self, field::kTransform));
    hash.Mix(At<uint8_t>(self, field::kTransform + 1));
    for (std::size_t offset = field::kMatrix; offset < field::kPosition; offset += 4)
        hash.Mix(At<uint32_t>(self, offset));
    for (std::size_t i = 0; i < 3; ++i)
        hash.Mix(At<uint32_t>(self, field::kPosition + 4 * i));
    const int32_t records = Items(self + field::kRecords, 0x2C);
    hash.Mix(static_cast<uint32_t>(records));
    if (records > 0)
        ++counters.withRecords;
    for (int32_t i = 0; i < records && i < 4096; ++i) {
        char* const record = At<char*>(self, field::kRecords) + 0x2C * i;
        const bool linked = At<void*>(record, 0x0) != nullptr;
        hash.Mix(linked);
        counters.linked += linked;
        char* const header = At<char*>(record, 0x4);
        uint32_t items = 0;
        for (char* node = At<char*>(header, 0); node != header && items < 4096; node = At<char*>(node, 0))
            ++items;
        hash.Mix(items);
        if (items)
            ++counters.withItems;
        hash.Mix(At<uint8_t>(record, 0x8));
        hash.Mix(At<uint8_t>(record, 0x9));
        hash.Mix(At<uint32_t>(record, 0xC));
        hash.Mix(At<uint32_t>(record, 0x10));
        uint32_t points = 0;
        MixOuter(hash, record + 0x14, points);
        MixOuter(hash, record + 0x20, points);
        if (points)
            ++counters.withPoints;
    }
    hash.Mix(At<uint32_t>(self, field::kCount));
    for (std::size_t i = 0; i < 4; ++i)
        hash.Mix(At<uint32_t>(self, field::kInts + 4 * i));
    MixCorners(hash, self + field::kCorners0);
    MixCorners(hash, self + field::kCorners1);
    MixCorners(hash, self + field::kCorners2);
    if (Items(self + field::kCorners0, 0x18) > 0)
        ++counters.withCorners;
    hash.Mix(At<uint32_t>(self, field::kFloat10C));
    hash.Mix(At<uint8_t>(self, field::kBool110));
    counters.bool110 += At<uint8_t>(self, field::kBool110) != 0;
    // The float at +0x64 is set by the interface's Init: a made object has the pool's memory there.
    if (!made)
        hash.Mix(At<uint32_t>(self, field::kFloat64));
    hash.Mix(At<uint8_t>(self, field::kByte112));
    counters.flag112 += At<uint8_t>(self, field::kByte112) != 0;
    hash.Mix(At<uint8_t>(self, field::kByte118));
    counters.byte118 += At<uint8_t>(self, field::kByte118) != 0;
    counters.listening += At<uint8_t>(self, field::kListening) != 0;
    char* const part = self + field::kOccupant;
    hash.Mix(At<uint32_t>(part, base::kType));
    hash.Mix(At<uint32_t>(part, base::kManagerBox));
    hash.Mix(At<uint16_t>(part, base::kRemoval2));
    hash.Mix(At<uint8_t>(part, base::kState) & 3u);
    hash.Mix(At<uint8_t>(part, base::kFlags) & 0xDDu);
    const char* const begin = At<char*>(part, base::kProperties);
    const char* const end = At<char*>(part, base::kProperties + 4);
    const uint32_t count = begin && end >= begin ? static_cast<uint32_t>((end - begin) / 8) : 0;
    hash.Mix(count);
    for (uint32_t i = 0; i < count && i < 256; ++i)
        hash.Mix(*reinterpret_cast<const uint32_t*>(begin + 8 * i));
    return hash.h;
}

// One QueryInterface through `part` for `iid`: whether it answered and where
// in the object the answer lies (never the address); the reference it gave
// is released.
void AskInterface(Hash& hash, char* self, char* part, uint32_t iid)
{
    void* found = nullptr;
    const bool answered = Virtual<uint8_t>(part, 0x0, iid, &found) != 0;
    hash.Mix(answered);
    if (answered && found) {
        hash.Mix(static_cast<uint32_t>(static_cast<char*>(found) - self));
        Virtual<uint32_t>(found, 0x8);
    }
}

// The pure query slots of a pole, through the vtables (so the game's
// functions or ours): of the interface GetRecordCount, GetRecordTarget of
// each record (present or not), GetCellPair, IsFlag112 and GetByte118;
// FindRecord of a pointer that no record holds (the answer is false without
// the wire-point builder, slot 5, running); of the occupant part
// IsInitialized, GetType, GetPosition, GetBoundingBox (two buffers of three
// floats; it works out the cell bounds while they are marked dirty), GetFlags
// and IsFlagSet(4); QueryInterface for the IDs the class answers (the
// interface, the class ID, 0xA9764C8C, the message target's, 1 and one
// nobody answers) through each of the parts that jump to it; GetGZCLSID of
// the serializable part.
uint32_t Queries(char* self)
{
    Hash hash;
    char* const part = self + field::kOccupant;
    const int32_t count = Virtual<int32_t>(self, slot::kGetRecordCount);
    hash.Mix(static_cast<uint32_t>(count));
    for (int32_t i = 0; i < count && i < 4096; ++i)
        hash.Mix(Virtual<void*>(self, slot::kGetRecordTarget, static_cast<uint32_t>(i)) != nullptr);
    uint32_t pair[2] = {};
    Virtual<void>(self, slot::kGetCellPair, &pair[0], &pair[1]);
    hash.Mix(pair[0]);
    hash.Mix(pair[1]);
    hash.Mix(Virtual<uint32_t>(self, slot::kIsFlag112));
    hash.Mix(Virtual<uint8_t>(self, slot::kGetByte118));
    int unrelated = 0;
    hash.Mix(Virtual<uint8_t>(self, slot::kFindRecord, static_cast<void*>(&unrelated), 0u));
    hash.Mix(Virtual<uint8_t>(part, occupant::kIsInitialized));
    hash.Mix(Virtual<uint32_t>(part, occupant::kGetType));
    uint32_t position[3] = {};
    Virtual<void>(part, occupant::kGetPosition, static_cast<uint32_t*>(position));
    for (const uint32_t value : position)
        hash.Mix(value);
    float low[3] = {}, high[3] = {};
    Virtual<void*>(part, occupant::kGetBoundingBox, static_cast<float*>(low), static_cast<float*>(high));
    for (int i = 0; i < 3; ++i) {
        hash.MixFloat(low[i]);
        hash.MixFloat(high[i]);
    }
    hash.Mix(Virtual<uint8_t>(part, occupant::kGetFlags) & 0xDDu);
    hash.Mix(Virtual<uint8_t>(part, 0x80, 4u));
    for (const std::size_t offset : {std::size_t{0}, field::kMessageTarget, field::kOccupant, field::kSerializable,
                                     field::kHolder, field::kExemplarPart})
        for (const uint32_t iid : {powerpoleocc_sim::kIidInterface, powerpoleocc_sim::kClassId,
                                   powerpoleocc_sim::kIidReference, powerpoleocc_sim::kIidTarget, 1u,
                                   0x0F10A0CCu})
            AskInterface(hash, self, self + offset, iid);
    hash.Mix(Virtual<uint32_t>(self + field::kSerializable, 0x14));
    return hash.h;
}

// The city's power poles (build 641; src/core/powerpoleocc_sim.h), found
// through the occupant manager (the city's slot 34): every occupant of type
// 0x2890D4DE in its grid, one in several cells counted once.
bool Collect(cISC4City* city, std::vector<char*>& poles)
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
                if (Virtual<uint32_t>(object, occupant::kGetType) == powerpoleocc_sim::kOccupantType)
                    seen.insert(static_cast<char*>(object) - field::kOccupant);
            }
        }
    }
    Virtual<void>(manager + occmgr_sim::field::kLock, 0x8);
    poles.assign(seen.begin(), seen.end());
    return true;
}

uint32_t g_setSum = 0;
uint32_t g_setCount = 0;
bool g_setDone = false;

// The setter on one pole (SetByte118, slot 14): another value, read back
// through the getter (slot 13); a value with the high bits set (the game
// keeps the low byte); zero; then the old value put back. Every read-back
// is mixed.
uint32_t Exercise(char* self)
{
    Hash hash;
    const uint8_t old = Virtual<uint8_t>(self, slot::kGetByte118);
    for (const uint32_t value : {old + 1u, 0x1A5u, 0u, 0xFFu}) {
        Virtual<void>(self, slot::kSetByte118, value);
        hash.Mix(Virtual<uint8_t>(self, slot::kGetByte118));
        hash.Mix(At<uint8_t>(self, field::kByte118));
    }
    Virtual<void>(self, slot::kSetByte118, uint32_t{old});
    hash.Mix(Virtual<uint8_t>(self, slot::kGetByte118));
    return hash.h;
}

// The class's life on one made object: the game's factory (0x0064E340: the
// pool, the constructor), one reference held by the test, the fields the
// constructor sets, QueryInterface through the parts for the IDs the class
// answers and some it does not, GetGZCLSID, the pure queries, the setter,
// and the last Release, which destroys the object through the deleting
// destructor and returns the item to its pool page. Not called: the
// occupant part's Init (it registers the object with the message server for
// 0x99EF1142, which the destruction does not undo), Shutdown, the message
// handlers and the builders.
uint32_t Life()
{
    Hash hash;
    char* const self = reinterpret_cast<char*(__cdecl*)()>(addr::cSC4PowerPoleOccupant_Create)();
    if (!self)
        return 0;
    Virtual<uint32_t>(self, slot::kAddRef);
    hash.Mix(Virtual<uint32_t>(self + field::kSerializable, 0x14));
    Counters unused;
    hash.Mix(PoleHash(self, unused, true));
    hash.Mix(Queries(self));
    hash.Mix(Exercise(self));
    Virtual<uint32_t>(self, slot::kRelease);
    return hash.h;
}

// OPENSC4_TEST_POWERPOLEOCC=<day>: on that day every pole (a city has few)
// goes through Exercise, and one made object through Life. The
// read-back checksums are summed into the fingerprint's ` set` field from
// then on.
void Drive(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_POWERPOLEOCC") || !game::IsBuild641() || g_setDone)
        return;
    g_setDone = true;
    std::vector<char*> poles;
    if (!Collect(day.city, poles))
        return;
    g_setSum += Life();
    for (char* const self : poles) {
        g_setSum += Exercise(self);
        ++g_setCount;
    }
}

// The fingerprint part: the number of poles and the sum and the exclusive or
// of PoleHash over them (order independent; no addresses), then the sum of
// the queries' answers (Queries) over every pole and the counters, and after
// OPENSC4_TEST_POWERPOLEOCC's day ` set <checksum>,<count>`.
void Fingerprint(cISC4City* city, std::string& line)
{
    std::vector<char*> poles;
    if (!Collect(city, poles)) {
        line += " | powerpoleocc none";
        return;
    }
    uint32_t sum = 0, mixed = 0, asked = 0;
    Counters counters;
    for (char* const self : poles) {
        const uint32_t hash = PoleHash(self, counters, false);
        sum += hash;
        mixed ^= hash;
        asked += Queries(self);
    }
    char text[256];
    std::snprintf(text, sizeof text,
                  " | powerpoleocc %u,%08X,%08X,q%08X rec %u item %u pts %u cor %u f112 %u b118 %u b110 %u link %u",
                  static_cast<unsigned>(poles.size()), sum, mixed, asked, counters.withRecords, counters.withItems,
                  counters.withPoints, counters.withCorners, counters.flag112, counters.byte118, counters.bool110,
                  counters.linked);
    line += text;
    if (g_setDone) {
        std::snprintf(text, sizeof text, " set %08X,%u", g_setSum, static_cast<unsigned>(g_setCount));
        line += text;
    }
}

const FingerprintPart kFingerprint{"powerpoleocc", 121, &Fingerprint};
const DayHook kDrive{"powerpoleocc.drive", 186, &Drive};

} // namespace
} // namespace opensc4::autotest
