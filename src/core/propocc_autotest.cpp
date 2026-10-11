// The prop occupants' part of the autotest (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/occmgr_sim.h"
#include "core/propocc_sim.h"

#include <cISC4City.h>

#include <opensc4/addresses.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace opensc4::autotest {
namespace {

namespace field = propocc_sim::field;
namespace dates = propocc_sim::dates;
namespace slot = propocc_sim::slot;
namespace occupant = propocc_sim::occupant;

constexpr uint32_t kIidOccupant = 0x26BB767E;

// The exemplar properties that hand a prop's state to frames or the view
// (the prop developer's OnPropAdded and its timed steps, docs/prop.md):
// the night switch, the view's value, the hours, the dates (whose state
// comes from a draw of the generator all timer props share) and the cell
// test.
constexpr uint32_t kFrameProperties[] = {0x49C9C93C, 0x6A3FCC37, 0x4A149631, 0x0A751675, 0x6A959E71};

struct Hash {
    uint32_t h = 2166136261u;
    void Mix(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    }
};

// Whether frames or the view drive the prop's state: it or its exemplar
// has one of the properties above (the holder's slot 3, a pure query). The
// answer is kept per exemplar for props without properties of their own.
bool FrameDriven(char* self, std::unordered_map<void*, bool>& known)
{
    void* const exemplar = At<void*>(self, field::kExemplar);
    const bool own = At<char*>(self, field::kProperties) != At<char*>(self, field::kProperties + 4);
    if (!own) {
        const auto found = known.find(exemplar);
        if (found != known.end())
            return found->second;
    }
    bool any = false;
    for (const uint32_t id : kFrameProperties)
        any = any || Virtual<uint8_t>(self + field::kHolder, 0xC, id) != 0;
    if (!own)
        known.emplace(exemplar, any);
    return any;
}

// One prop's simulation-driven state from its fields, without addresses:
// the prop type's ID (not its index, which follows the order props were
// met in), the orientation, the hours, the date range (whether there is
// one, interval, duration, the two day numbers), timer bit 2, the chance,
// the usage, the lot tag, whether there is an exemplar, the occupant
// manager's cells, and the state of props whose state the simulation
// alone changes.
//
// Left out because frames or the view drive them, so that two runs of the
// original need not agree (src/core/propocc_sim.h, "What follows frames"):
// the state of timer and night props, timer bits 1, 4 and 8, base flags 1
// and 2, the box (some props move by frames, docs/occmgr.md), the
// placeable object.
uint32_t PropHash(char* self, bool frameDriven)
{
    Hash hash;
    hash.Mix(propocc_sim::TypeAt(At<uint16_t>(self, field::kTypeIndex)));
    hash.Mix(At<uint8_t>(self, field::kOrientation));
    hash.Mix(At<uint16_t>(self, field::kHours));
    if (char* const range = At<char*>(self, field::kDates)) {
        hash.Mix(1);
        hash.Mix(At<uint32_t>(range, dates::kInterval));
        hash.Mix(At<uint32_t>(range, dates::kDuration));
        hash.Mix(At<uint32_t>(range, dates::kStart + dates::kDayNumber));
        hash.Mix(At<uint32_t>(range, dates::kEnd + dates::kDayNumber));
    } else {
        hash.Mix(0);
    }
    hash.Mix(At<uint8_t>(self, field::kTimer) & 2u);
    hash.Mix(At<uint8_t>(self, field::kChance));
    hash.Mix(At<uint8_t>(self, field::kUsage));
    hash.Mix(At<uint32_t>(self, field::kLotTag));
    hash.Mix(At<void*>(self, field::kExemplar) != nullptr);
    hash.Mix(At<uint32_t>(self, field::kManagerBox));
    // The state is not mixed for any prop: besides the timer and night
    // props, pairs of other props trade their state between two runs of the
    // original (Big City with landfill zoned: one shown where the other is,
    // the sum of the hashes equal and their exclusive or not; the prop
    // developer's own generator differs between runs, docs/prop.md).
    static_cast<void>(frameDriven);
    return hash.h;
}

// The pure query slots of a prop (OPENSC4_TEST_PROPOCC=1). Of the primary
// interface: AsOccupant, GetPropPlacementProperties, GetPropType,
// GetOrientation, GetState, GetTimeOfDayRange, GetRandomChance,
// GetPropTimerBitfield, GetPropTimerState and GetPropUsage; QueryInterface
// (through the occupant part's thunk) for the prop, the message target,
// the class ID, the occupant and 1; of the occupant part IsInitialized,
// GetType, GetPosition, GetBoundingBox and GetLotTag; GetGZCLSID of the
// serializable part. Where an answer is one of the frame-driven fields
// (the state, the timer bits, the box) the checksum takes whether it
// agrees with the field, not its value. Never a setter, the save functions
// or a message.
uint32_t Queries(char* self)
{
    Hash hash;
    char* const part = self + field::kOccupant;
    hash.Mix(static_cast<uint32_t>(Virtual<char*>(self, slot::kAsOccupant) - self));
    hash.Mix(Virtual<void*>(self, slot::kGetPropPlacementProperties) != nullptr);
    hash.Mix(Virtual<uint32_t>(self, slot::kGetPropType));
    hash.Mix(static_cast<uint32_t>(Virtual<int32_t>(self, slot::kGetOrientation)));
    hash.Mix(Virtual<uint32_t>(self, slot::kGetState) == At<uint8_t>(self, field::kState));
    uint32_t hours[2] = {};
    Virtual<uint32_t>(self, slot::kGetTimeOfDayRange, static_cast<uint32_t*>(&hours[0]), static_cast<uint32_t*>(&hours[1]));
    hash.Mix(hours[0]);
    hash.Mix(hours[1]);
    hash.Mix(Virtual<uint8_t>(self, slot::kGetRandomChance));
    const uint8_t timer = At<uint8_t>(self, field::kTimer);
    hash.Mix(Virtual<uint32_t>(self, slot::kGetPropTimerBitfield) == timer);
    hash.Mix(Virtual<uint32_t>(self, slot::kGetPropTimerState, 2u));
    hash.Mix(Virtual<uint32_t>(self, slot::kGetPropTimerState, 0xDu) == ((timer & 0xD) != 0 ? 1u : 0u));
    hash.Mix(Virtual<uint8_t>(self, slot::kGetPropUsage));
    for (const uint32_t iid : {propocc_sim::kIidProp, propocc_sim::kIidMessageTarget, propocc_sim::kClassId, kIidOccupant, 1u}) {
        void* found = nullptr;
        const bool answered = Virtual<uint8_t>(part, 0x0, iid, &found) != 0;
        hash.Mix(answered);
        if (answered && found) {
            hash.Mix(static_cast<uint32_t>(static_cast<char*>(found) - self));
            Virtual<uint32_t>(found, 0x8);
        }
    }
    hash.Mix(Virtual<uint32_t>(part, occupant::kIsInitialized) & 0xFFu);
    hash.Mix(Virtual<uint32_t>(part, occupant::kGetType));
    uint32_t position[3] = {}, low[3] = {}, high[3] = {};
    Virtual<void*>(part, occupant::kGetPosition, static_cast<uint32_t*>(position));
    Virtual<void*>(part, occupant::kGetBoundingBox, static_cast<uint32_t*>(low), static_cast<uint32_t*>(high));
    // The position the box gives: the centre in x and z, the low y.
    float centre[3];
    centre[0] = static_cast<float>(
        (static_cast<double>(At<float>(self, field::kMax)) + static_cast<double>(At<float>(self, field::kMin))) * 0.5);
    std::memcpy(&centre[1], self + field::kMin + 4, 4);
    centre[2] = static_cast<float>(
        (static_cast<double>(At<float>(self, field::kMax + 8)) + static_cast<double>(At<float>(self, field::kMin + 8))) * 0.5);
    hash.Mix(std::memcmp(position, centre, sizeof position) == 0);
    hash.Mix(std::memcmp(low, self + field::kMin, sizeof low) == 0);
    hash.Mix(std::memcmp(high, self + field::kMax, sizeof high) == 0);
    uint32_t tag = 0;
    hash.Mix(Virtual<uint8_t>(part, occupant::kGetLotTag, &tag));
    hash.Mix(tag);
    hash.Mix(Virtual<uint32_t>(self + field::kSerializable, 0x14));
    return hash.h;
}

// The city's props (build 641; src/core/propocc_sim.h), found through the
// occupant manager (the city's slot 34): every occupant of type 0xA823821E
// in its grid whose class is cSC4PropOccupant itself (the derived classes
// keep the type; they have their own records and are left out), one in
// several cells counted once. The count and two order-independent
// checksums of the props' state (the sum and the exclusive or of PropHash
// above; no addresses).
//
// OPENSC4_TEST_PROPOCC=1 adds the sum of the pure query slots' answers
// (Queries above) over every 11th prop (those whose state hash is a
// multiple of 11, so the choice does not follow the order of the walk).
// OPENSC4_TEST_PROPOCC=2 adds instead the fields that follow frames: how
// many props have a state other than 0, timer bits 1, 4 and 8, base flags
// 1 and 2, and the sum of a hash of each prop's state hash with its state,
// timer byte, the two flags and its box. Two runs of the original need not
// agree on those.
void Fingerprint(cISC4City* city, std::string& line)
{
    if (!game::IsBuild641()) {
        line += " | propocc none";
        return;
    }
    char* const manager = Virtual<char*>(city, 0x88);
    if (!manager || !At<uint8_t>(manager, occmgr_sim::field::kInitialized)) {
        line += " | propocc none";
        return;
    }
    const int test = EnvInt("OPENSC4_TEST_PROPOCC", 0);
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
                char* const part = static_cast<char*>(occmgr_sim::OccupantOf(node));
                if (Virtual<uint32_t>(part, occupant::kGetType) == propocc_sim::kOccupantType &&
                    Virtual<uint32_t>(part + 4, 0x14) == propocc_sim::kClassId)
                    seen.insert(part - field::kOccupant);
            }
        }
    }
    Virtual<void>(manager + occmgr_sim::field::kLock, 0x8);
    std::unordered_map<void*, bool> known;
    uint32_t sum = 0, mixed = 0, asked = 0, timed = 0;
    uint32_t counts[6] = {};
    // OPENSC4_TEST_PROPOCC_DUMP=<n>: the n-th fingerprint of the run also logs
    // every prop's hashed fields, to find what two runs differ in.
    static int calls = 0;
    const bool dump = ++calls == EnvInt("OPENSC4_TEST_PROPOCC_DUMP", 0);
    for (char* const self : seen) {
        const bool frameDriven = FrameDriven(self, known);
        const uint32_t hash = PropHash(self, frameDriven);
        if (dump)
            log::Write("propocc dump: type %08X cells %08X tag %08X orientation %u hours %04X dates %d timer %u chance %u "
                       "usage %u state %u frame %d hash %08X",
                       propocc_sim::TypeAt(At<uint16_t>(self, field::kTypeIndex)), At<uint32_t>(self, field::kManagerBox),
                       At<uint32_t>(self, field::kLotTag), At<uint8_t>(self, field::kOrientation),
                       At<uint16_t>(self, field::kHours), At<char*>(self, field::kDates) != nullptr,
                       At<uint8_t>(self, field::kTimer) & 2u, At<uint8_t>(self, field::kChance),
                       At<uint8_t>(self, field::kUsage), At<uint8_t>(self, field::kState), frameDriven ? 1 : 0, hash);
        sum += hash;
        mixed ^= hash;
        if (test == 1 && hash % 11 == 0)
            asked += Queries(self);
        if (test == 2) {
            const uint8_t state = At<uint8_t>(self, field::kState);
            const uint8_t timer = At<uint8_t>(self, field::kTimer);
            const uint8_t flags = At<uint8_t>(self, field::kFlags);
            counts[0] += state != 0;
            counts[1] += (timer & 1) != 0;
            counts[2] += (timer & 4) != 0;
            counts[3] += (timer & 8) != 0;
            counts[4] += (flags & 1) != 0;
            counts[5] += (flags & 2) != 0;
            Hash both;
            both.Mix(hash);
            both.Mix(state);
            both.Mix(timer);
            both.Mix(flags & 3u);
            for (std::size_t offset = field::kMin; offset < field::kDates; offset += 4)
                both.Mix(At<uint32_t>(self, offset));
            timed += both.h;
        }
    }
    char text[160];
    std::snprintf(text, sizeof text, " | propocc %u,%08X,%08X", static_cast<unsigned>(seen.size()), sum, mixed);
    line += text;
    if (test == 1) {
        std::snprintf(text, sizeof text, ",q%08X", asked);
        line += text;
    } else if (test == 2) {
        std::snprintf(text, sizeof text, ",f%u/%u/%u/%u/%u/%u/%08X", counts[0], counts[1], counts[2], counts[3], counts[4],
                      counts[5], timed);
        line += text;
    }
}

const FingerprintPart kFingerprint{"propocc", 107, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
