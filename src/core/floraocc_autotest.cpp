// The flora occupants' part of the autotest (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/floraocc_sim.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/occmgr_sim.h"

#include <cISC4City.h>

#include <opensc4/addresses.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_set>

namespace opensc4::autotest {
namespace {

namespace field = floraocc_sim::field;
namespace data = floraocc_sim::data;
namespace slot = floraocc_sim::slot;
namespace occupant = floraocc_sim::occupant;

struct Hash {
    uint32_t h = 2166136261u;
    void Mix(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    }
};

// One tree's state from its fields, without addresses. The `flora` part
// (src/core/flora_autotest.cpp) has each tree's type, dates, state,
// orientation and position already; the type and the position are mixed in
// here only to tell the trees apart. What this adds: the lot tag, the
// type's record (whether there is one, whether it is the record of the
// tree's type, and the radius and height the box is made of, so with the
// position the box), whether there is an exemplar and whether it is the
// record's, and of the occupant base the type, the occupant manager's
// cells, the removal counts, the initialized and read-from-a-save bits, the
// flags as they are saved and the IDs of the holder's own properties.
//
// The lot tag of a tree no lot planted is memory the class never wrote
// (src/core/floraocc_sim.h): zero in a fresh pool page, a dead tree's tag
// in a reused item. It is saved, and it follows the order trees are made
// and die in, so it is in the hash.
//
// Left out because the interface or the view drives them: the highlight
// (+0x32's high bits), the flags 0x02 and 0x20 (not saved either) and the
// placeable object (+0x34; made when the tree is first drawn). Left out as
// never written: +0x5A and +0x5B.
uint32_t TreeHash(char* self)
{
    Hash hash;
    hash.Mix(At<uint32_t>(self, field::kFloraType));
    for (std::size_t offset = field::kPosition; offset < field::kBirthDate; offset += 4)
        hash.Mix(At<uint32_t>(self, offset));
    hash.Mix(At<uint32_t>(self, field::kLotTag));
    char* const record = At<char*>(self, field::kData);
    hash.Mix(record != nullptr);
    if (record) {
        hash.Mix(At<uint32_t>(record, data::kType) == At<uint32_t>(self, field::kFloraType));
        hash.Mix(At<uint32_t>(record, data::kRadius));
        hash.Mix(At<uint32_t>(record, data::kHeight));
        hash.Mix(At<void*>(record, data::kExemplar) == At<void*>(self, field::kExemplar));
    }
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
    return hash.h;
}

// The pure query slots of a tree (OPENSC4_TEST_FLORAOCC=1): of the primary
// interface AsOccupant, GetFloraType, GetBirthDate, GetLastSeedingDate,
// GetState and GetOrientation; QueryInterface through the occupant part
// for the flora interface, the class ID, 1 and an ID nobody answers; of the
// occupant part IsInitialized, GetType, GetPosition, GetLotTag, GetFlags
// (the saved bits), IsFlagSet(8) and, for a tree that has its type's
// record, GetBoundingBox (without one the call would fetch and set the
// record: not a pure query); GetGZCLSID of the serializable part. Never a
// setter, the save functions or a message; not GetHighlight (the
// interface's).
uint32_t Queries(char* self)
{
    Hash hash;
    char* const part = self + field::kOccupant;
    hash.Mix(static_cast<uint32_t>(Virtual<char*>(self, slot::kAsOccupant) - self));
    hash.Mix(Virtual<uint32_t>(self, slot::kGetFloraType));
    hash.Mix(Virtual<uint32_t>(self, slot::kGetBirthDate));
    hash.Mix(Virtual<uint32_t>(self, slot::kGetLastSeedingDate));
    hash.Mix(static_cast<uint32_t>(Virtual<int32_t>(self, slot::kGetState)));
    hash.Mix(Virtual<uint32_t>(self, slot::kGetOrientation));
    for (const uint32_t iid : {floraocc_sim::kIidFlora, floraocc_sim::kClassId, 1u, 0x0F10A0CCu}) {
        void* found = nullptr;
        const bool answered = Virtual<uint8_t>(part, 0x0, iid, &found) != 0;
        hash.Mix(answered);
        if (answered && found) {
            hash.Mix(static_cast<uint32_t>(static_cast<char*>(found) - self));
            Virtual<uint32_t>(found, 0x8);
        }
    }
    hash.Mix(Virtual<uint8_t>(part, occupant::kIsInitialized));
    hash.Mix(Virtual<uint32_t>(part, occupant::kGetType));
    uint32_t position[3] = {};
    Virtual<void*>(part, occupant::kGetPosition, static_cast<uint32_t*>(position));
    for (const uint32_t value : position)
        hash.Mix(value);
    uint32_t tag = 0x7A67A67Au;
    hash.Mix(Virtual<uint8_t>(part, occupant::kGetLotTag, &tag));
    hash.Mix(tag);
    hash.Mix(Virtual<uint8_t>(part, occupant::kGetFlags) & 0xDDu);
    hash.Mix(Virtual<uint8_t>(part, occupant::kIsFlagSet, 8u));
    if (At<char*>(self, field::kData)) {
        float low[3] = {}, high[3] = {};
        Virtual<void*>(part, occupant::kGetBoundingBox, static_cast<float*>(low), static_cast<float*>(high));
        for (int i = 0; i < 3; ++i) {
            hash.Mix(BytesChecksum(&low[i], 4));
            hash.Mix(BytesChecksum(&high[i], 4));
        }
    }
    hash.Mix(Virtual<uint32_t>(self + field::kSerializable, 0x14));
    return hash.h;
}

// The city's flora (build 641; src/core/floraocc_sim.h), found through the
// occupant manager (the city's slot 34): every occupant of type 0x74758926
// in its grid, one in several cells counted once. The count and two
// order-independent checksums of the trees' state (the sum and the
// exclusive or of TreeHash above; no addresses).
//
// OPENSC4_TEST_FLORAOCC=1 adds the sum of the pure query slots' answers
// (Queries above) over every 13th tree (those whose state hash is a
// multiple of 13, so the choice does not follow the order of the walk).
void Fingerprint(cISC4City* city, std::string& line)
{
    if (!game::IsBuild641()) {
        line += " | floraocc none";
        return;
    }
    char* const manager = Virtual<char*>(city, 0x88);
    if (!manager || !At<uint8_t>(manager, occmgr_sim::field::kInitialized)) {
        line += " | floraocc none";
        return;
    }
    const int test = EnvInt("OPENSC4_TEST_FLORAOCC", 0);
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
                if (Virtual<uint32_t>(object, occupant::kGetType) == floraocc_sim::kOccupantType)
                    seen.insert(static_cast<char*>(object) - field::kOccupant);
            }
        }
    }
    Virtual<void>(manager + occmgr_sim::field::kLock, 0x8);
    uint32_t sum = 0, mixed = 0, asked = 0;
    for (char* const self : seen) {
        const uint32_t hash = TreeHash(self);
        sum += hash;
        mixed ^= hash;
        if (test == 1 && hash % 13 == 0)
            asked += Queries(self);
    }
    char text[96];
    std::snprintf(text, sizeof text, " | floraocc %u,%08X,%08X", static_cast<unsigned>(seen.size()), sum, mixed);
    line += text;
    if (test == 1) {
        std::snprintf(text, sizeof text, ",q%08X", asked);
        line += text;
    }
}

const FingerprintPart kFingerprint{"floraocc", 108, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
