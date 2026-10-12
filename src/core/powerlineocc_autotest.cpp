// The power line occupants' part of the autotest (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/occmgr_sim.h"
#include "core/powerlineocc_sim.h"

#include <cISC4City.h>

#include <opensc4/addresses.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_set>
#include <vector>

namespace opensc4::autotest {
namespace {

namespace field = powerlineocc_sim::field;
namespace slot = powerlineocc_sim::slot;
namespace occupant = powerlineocc_sim::occupant;

struct Hash {
    uint32_t h = 2166136261u;
    void Mix(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    }
};

// How many wires answered something that says the test reaches the paths of
// the functions (printed after the checksums): wires whose flag is set, with
// both poles, with one pole, with a cell that is not 0,0, whose position is
// not the constructor's, and whose cell is what SetPosition gives for the
// position.
struct Counters {
    uint32_t flagged = 0, both = 0, one = 0, celled = 0, moved = 0, consistent = 0;
};

// The cell SetPosition (0x0064A460) makes of a position: the float times
// 0.0625, truncated.
int32_t CellOf(float value)
{
    return static_cast<int32_t>(static_cast<int64_t>(static_cast<double>(value) * 0.0625));
}

// One wire's state from its fields, without addresses: the class's own
// fields (the position and the two cells as the floats' and integers' bits,
// the flag, whether each pole is there), whether there is an exemplar, and
// of the occupant base the type, the occupant manager's cells, the removal
// counts, the initialized and read-from-a-save bits, the flags as they are
// saved and the IDs of the holder's own properties.
//
// Left out because the view drives them: the preview byte (+0x55), the
// highlight (+0x32's high bits), the flags 0x02 and 0x20 (not saved either)
// and the placeable object (+0x34; made when the occupant is first drawn).
// Left out as never written: +0x56 and +0x57 (the pool's memory). The
// reference count is left out too (the view's and the interface's objects
// hold references, and every pole holds the wires it joins).
uint32_t WireHash(char* self, Counters& counters)
{
    Hash hash;
    for (std::size_t offset = field::kPosition; offset < field::kPole1; offset += 4)
        hash.Mix(At<uint32_t>(self, offset));
    const bool first = At<void*>(self, field::kPole1) != nullptr;
    const bool second = At<void*>(self, field::kPole2) != nullptr;
    hash.Mix(first);
    hash.Mix(second);
    hash.Mix(At<uint8_t>(self, field::kFlag));
    hash.Mix(At<void*>(self, field::kExemplar) != nullptr);
    hash.Mix(At<uint32_t>(self, field::kType));
    hash.Mix(At<uint32_t>(self, field::kManagerBox));
    hash.Mix(At<uint16_t>(self, field::kRemoval));
    hash.Mix(At<uint8_t>(self, field::kState) & 3u);
    hash.Mix(At<uint8_t>(self, field::kFlags) & 0xDDu);
    const char* const begin = At<char*>(self, field::kProperties);
    const char* const end = At<char*>(self, field::kProperties + 4);
    const uint32_t count = begin && end >= begin ? static_cast<uint32_t>((end - begin) / 8) : 0;
    hash.Mix(count);
    for (uint32_t i = 0; i < count && i < 256; ++i)
        hash.Mix(*reinterpret_cast<const uint32_t*>(begin + 8 * i));
    counters.flagged += At<uint8_t>(self, field::kFlag) != 0;
    counters.both += first && second;
    counters.one += first != second;
    counters.celled += At<int32_t>(self, field::kCellX) != 0 || At<int32_t>(self, field::kCellZ) != 0;
    counters.moved +=
        At<uint32_t>(self, field::kPosition) != Global<uint32_t>(addr::SC4_ZeroVector3);
    counters.consistent += At<int32_t>(self, field::kCellX) == CellOf(At<float>(self, field::kPosition)) &&
                           At<int32_t>(self, field::kCellZ) == CellOf(At<float>(self, field::kPosition + 8));
    return hash.h;
}

// QueryInterface through one part for an ID: whether it answered and, if so,
// which part it gave (as an offset from the object) after the count it took
// is let go.
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

// The IDs the class answers (its interface, its class ID, 0xA9764C8C), the
// occupant base's (1, the occupant, the part at +4) and one nobody has.
constexpr uint32_t kIds[] = {powerlineocc_sim::kIidPowerLine,
                             powerlineocc_sim::kClassId,
                             powerlineocc_sim::kIidShared,
                             1u,
                             0x26BB767Eu,
                             0xE4FDA3D4u,
                             0x0F10A0CCu};

// The poles and the flag of a wire as the interface gives them (the counts
// the call takes are let go): whether each is there, and the flag.
void MixPoles(Hash& hash, char* wire)
{
    void* first = nullptr;
    void* second = nullptr;
    uint8_t flag = 0;
    Virtual<uint32_t>(wire, slot::kGetConnectedPoles, &first, &second, &flag);
    hash.Mix(first != nullptr);
    hash.Mix(second != nullptr);
    hash.Mix(flag);
    if (first)
        Virtual<uint32_t>(first, 0x8);
    if (second)
        Virtual<uint32_t>(second, 0x8);
}

// The pure query slots of a wire (OPENSC4_TEST_POWERLINEOCC=1): of the
// interface GetConnectedPoles (the counts let go) and GetCell; QueryInterface
// through the interface and the occupant part for the IDs above; of the
// occupant part IsInitialized, GetType, GetPosition, GetBoundingBox (two
// buffers of three floats, all the callee writes), GetFlags (the saved bits)
// and IsFlagSet(4); GetGZCLSID of the serializable part. Never a setter, the
// save functions or a message; not IsPreview (the view's).
uint32_t Queries(char* self)
{
    Hash hash;
    char* const part = self + field::kOccupant;
    MixPoles(hash, self);
    int32_t x = 0, z = 0;
    Virtual<uint32_t>(self, slot::kGetCell, &x, &z);
    hash.Mix(static_cast<uint32_t>(x));
    hash.Mix(static_cast<uint32_t>(z));
    for (const uint32_t iid : kIds) {
        AskInterface(hash, self, self, iid);
        AskInterface(hash, self, part, iid);
    }
    hash.Mix(Virtual<uint8_t>(part, occupant::kIsInitialized));
    hash.Mix(Virtual<uint32_t>(part, occupant::kGetType));
    uint32_t position[3] = {};
    Virtual<void*>(part, occupant::kGetPosition, static_cast<uint32_t*>(position));
    for (const uint32_t value : position)
        hash.Mix(value);
    float low[3] = {}, high[3] = {};
    Virtual<void*>(part, occupant::kGetBoundingBox, static_cast<float*>(low), static_cast<float*>(high));
    for (int i = 0; i < 3; ++i) {
        hash.Mix(BytesChecksum(&low[i], 4));
        hash.Mix(BytesChecksum(&high[i], 4));
    }
    hash.Mix(Virtual<uint8_t>(part, occupant::kGetFlags) & 0xDDu);
    hash.Mix(Virtual<uint8_t>(part, occupant::kIsFlagSet, 4u));
    hash.Mix(Virtual<uint32_t>(self + field::kSerializable, 0x14));
    return hash.h;
}

// The position, the cells and the box through the interface: the setter's
// effect (the position and both cells), read back through GetPosition,
// GetCell and GetBoundingBox.
void MixPosition(Hash& hash, char* self)
{
    char* const part = self + field::kOccupant;
    uint32_t position[3] = {};
    Virtual<void*>(part, occupant::kGetPosition, static_cast<uint32_t*>(position));
    for (const uint32_t value : position)
        hash.Mix(value);
    int32_t x = 0, z = 0;
    Virtual<uint32_t>(self, slot::kGetCell, &x, &z);
    hash.Mix(static_cast<uint32_t>(x));
    hash.Mix(static_cast<uint32_t>(z));
    float low[3] = {}, high[3] = {};
    Virtual<void*>(part, occupant::kGetBoundingBox, static_cast<float*>(low), static_cast<float*>(high));
    for (int i = 0; i < 3; ++i) {
        hash.Mix(BytesChecksum(&low[i], 4));
        hash.Mix(BytesChecksum(&high[i], 4));
    }
}

// The setters' exercise on one wire (OPENSC4_TEST_POWERLINEOCC=<day>), each
// value put back after: the poles (the wire's counts held by the test first,
// so no pole is let go for good: both removed, read back; swapped, read
// back; the flag changed; then the old ones set again and the test's counts
// released), the position (moved by 16 and 32, read back through the
// position, the cells and the box; then the old position set, the cells
// written back as they were) and the preview byte (the other value, read
// back, restored). The read-backs are mixed into the result.
uint32_t Exercise(char* self)
{
    Hash hash;
    void* first = nullptr;
    void* second = nullptr;
    uint8_t flag = 0;
    Virtual<uint32_t>(self, slot::kGetConnectedPoles, &first, &second, &flag);
    Virtual<uint32_t>(self, slot::kSetConnectedPoles, static_cast<void*>(nullptr), static_cast<void*>(nullptr),
                      static_cast<uint32_t>(flag ^ 1u));
    MixPoles(hash, self);
    Virtual<uint32_t>(self, slot::kSetConnectedPoles, second, first, static_cast<uint32_t>(flag));
    MixPoles(hash, self);
    hash.Mix(At<void*>(self, field::kPole1) == second);
    hash.Mix(At<void*>(self, field::kPole2) == first);
    Virtual<uint32_t>(self, slot::kSetConnectedPoles, first, second, static_cast<uint32_t>(flag));
    MixPoles(hash, self);
    if (first)
        Virtual<uint32_t>(first, 0x8);
    if (second)
        Virtual<uint32_t>(second, 0x8);

    char* const part = self + field::kOccupant;
    float old[3] = {};
    Virtual<void*>(part, occupant::kGetPosition, static_cast<float*>(old));
    const int32_t oldX = At<int32_t>(self, field::kCellX);
    const int32_t oldZ = At<int32_t>(self, field::kCellZ);
    const float moved[3] = {old[0] + 16.0f, old[1], old[2] + 32.0f};
    Virtual<uint32_t>(part, occupant::kSetPosition, static_cast<const float*>(moved));
    MixPosition(hash, self);
    Virtual<uint32_t>(part, occupant::kSetPosition, static_cast<const float*>(old));
    At<int32_t>(self, field::kCellX) = oldX;
    At<int32_t>(self, field::kCellZ) = oldZ;
    MixPosition(hash, self);

    const uint8_t preview = Virtual<uint8_t>(self, slot::kIsPreview);
    Virtual<uint8_t>(self, slot::kSetPreview, static_cast<uint32_t>(preview ^ 1u));
    hash.Mix(Virtual<uint8_t>(self, slot::kIsPreview));
    Virtual<uint8_t>(self, slot::kSetPreview, static_cast<uint32_t>(preview));
    return hash.h;
}

// Every wire of the city (build 641; src/core/powerlineocc_sim.h), found
// through the occupant manager (the city's slot 34 / +0x88) as the
// foundations are (src/core/foundocc_autotest.cpp): every occupant of type
// 0x0990B2EA in its grid, one in several cells once. The objects are the
// interface part (the occupant part less 4). False without a manager.
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
                if (Virtual<uint32_t>(object, occupant::kGetType) == powerlineocc_sim::kOccupantType)
                    seen.insert(static_cast<char*>(object) - field::kOccupant);
            }
        }
    }
    Virtual<void>(manager + occmgr_sim::field::kLock, 0x8);
    found.assign(seen.begin(), seen.end());
    return true;
}

// The class's life on one made object (OPENSC4_TEST_POWERLINEOCC=<day>):
// the game's factory (0x0064AB70: the pool, the constructor), one count held
// by the test, QueryInterface through the parts that answer to it (the
// interface, the occupant part, the serializable part, the holder and the
// exemplar part) for the IDs above; GetGZCLSID of the serializable part; the
// fields the constructor sets; the setters (the position and the preview
// byte); the poles of the donor wire (if there is one) set on it and read
// back, and let go again by the occupant part's Shutdown (which calls
// SetConnectedPoles with zeros, then the base's Shutdown, which does nothing
// for an object that was never initialised). The last Release destroys the
// object through the deleting destructor and returns the item to its pool
// page. Not called: Init (the occupant message 0x99EF1140 would tell the
// listeners of a wire that is in no cell and no network), the save functions
// (the stream is the game's).
uint32_t Life(char* donor)
{
    Hash hash;
    char* const self = reinterpret_cast<char*(__cdecl*)()>(addr::cSC4PowerLineOccupant_Create)();
    if (!self)
        return 0;
    Virtual<uint32_t>(self, 0x4);
    hash.Mix(Virtual<uint32_t>(self + field::kSerializable, 0x14));
    for (const std::size_t offset : {std::size_t{0}, field::kOccupant, field::kSerializable, field::kHolder,
                                     field::kExemplarPart})
        for (const uint32_t iid : kIds)
            AskInterface(hash, self, self + offset, iid);
    Counters unused;
    hash.Mix(WireHash(self, unused));
    hash.Mix(At<uint8_t>(self, field::kPreview));
    MixPosition(hash, self);
    const float position[3] = {640.0f, 12.5f, -96.0f};
    Virtual<uint32_t>(self + field::kOccupant, occupant::kSetPosition, static_cast<const float*>(position));
    MixPosition(hash, self);
    Virtual<uint8_t>(self, slot::kSetPreview, 1u);
    hash.Mix(Virtual<uint8_t>(self, slot::kIsPreview));
    if (donor) {
        void* first = nullptr;
        void* second = nullptr;
        uint8_t flag = 0;
        Virtual<uint32_t>(donor, slot::kGetConnectedPoles, &first, &second, &flag);
        Virtual<uint32_t>(self, slot::kSetConnectedPoles, first, second, 1u);
        MixPoles(hash, self);
        Virtual<uint32_t>(self + field::kOccupant, occupant::kShutdown);
        MixPoles(hash, self);
        if (first)
            Virtual<uint32_t>(first, 0x8);
        if (second)
            Virtual<uint32_t>(second, 0x8);
    }
    Virtual<uint32_t>(self, 0x8);
    return hash.h;
}

uint32_t g_setSum = 0;
uint32_t g_setCount = 0;
bool g_setDone = false;

// OPENSC4_TEST_POWERLINEOCC=<day>: on that day every third wire (those
// whose state hash is a multiple of 3, so the choice does not follow the
// order of the walk) goes through Exercise, and one made object through
// Life. The read-back checksums are summed into the fingerprint's ` set`
// field from then on.
void Drive(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_POWERLINEOCC") || !game::IsBuild641() || g_setDone)
        return;
    g_setDone = true;
    std::vector<char*> objects;
    if (!Collect(day.city, objects))
        return;
    // The donor of the poles for Life: of the wires with both poles, the one
    // with the smallest state hash (not the first of the walk).
    char* donor = nullptr;
    uint32_t donorHash = 0;
    for (char* const self : objects) {
        if (!At<void*>(self, field::kPole1) || !At<void*>(self, field::kPole2))
            continue;
        Counters unused;
        const uint32_t hash = WireHash(self, unused);
        if (!donor || hash < donorHash) {
            donor = self;
            donorHash = hash;
        }
    }
    g_setSum += Life(donor);
    for (char* const self : objects) {
        Counters unused;
        if (WireHash(self, unused) % 3 != 0)
            continue;
        g_setSum += Exercise(self);
        ++g_setCount;
    }
}

// The fingerprint part: the number of wires and the sum and the exclusive
// or of WireHash over them (order independent; no addresses), then the
// counters, with OPENSC4_TEST_POWERLINEOCC=1 the sum of the query slots'
// answers over every third wire (Queries above), and after
// OPENSC4_TEST_POWERLINEOCC's day ` set <checksum>,<count>`.
void Fingerprint(cISC4City* city, std::string& line)
{
    std::vector<char*> objects;
    if (!Collect(city, objects)) {
        line += " | powerlineocc none";
        return;
    }
    const int test = EnvInt("OPENSC4_TEST_POWERLINEOCC", 0);
    uint32_t sum = 0, mixed = 0, asked = 0;
    Counters counters;
    for (char* const self : objects) {
        const uint32_t hash = WireHash(self, counters);
        sum += hash;
        mixed ^= hash;
        if (test == 1 && hash % 3 == 0)
            asked += Queries(self);
    }
    char text[256];
    std::snprintf(text, sizeof text, " | powerlineocc %u,%08X,%08X flag %u both %u one %u cell %u moved %u ok %u",
                  static_cast<unsigned>(objects.size()), sum, mixed, counters.flagged, counters.both, counters.one,
                  counters.celled, counters.moved, counters.consistent);
    line += text;
    if (test == 1) {
        std::snprintf(text, sizeof text, ",q%08X", asked);
        line += text;
    }
    if (g_setDone) {
        std::snprintf(text, sizeof text, " set %08X,%u", g_setSum, static_cast<unsigned>(g_setCount));
        line += text;
    }
}

const FingerprintPart kFingerprint{"powerlineocc", 121, &Fingerprint};
const DayHook kDrive{"powerlineocc.drive", 184, &Drive};

} // namespace
} // namespace opensc4::autotest
