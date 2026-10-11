// The landfill occupants' part of the autotest (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/landfillocc_sim.h"
#include "core/occmgr_sim.h"

#include <cISC4City.h>

#include <opensc4/addresses.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_set>

namespace opensc4::autotest {
namespace {

namespace field = landfillocc_sim::field;
namespace slot = landfillocc_sim::slot;
namespace occupant = landfillocc_sim::occupant;

struct Hash {
    uint32_t h = 2166136261u;
    void Mix(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    }
};

// One landfill occupant's state from its fields, without addresses: the
// tile ID, the orientation, the plain flag, the position and the box (the
// floats' bits), whether there is an exemplar, and of the occupant base the
// type, the occupant manager's cells, the removal counts, the initialized
// and read-from-a-save bits, the flags as they are saved and the IDs of the
// holder's own properties.
//
// Left out because the interface or the view drives them: the highlight
// (+0x32's high bits), the flags 0x02 and 0x20 (not saved either) and the
// placeable object (+0x34; made when the heap is first drawn). Left out as
// never written: +0x65 to +0x67 (the pool's memory). The reference count
// (+0x10) follows who holds the occupant at the moment and is left out too.
uint32_t OccupantHash(char* self)
{
    Hash hash;
    hash.Mix(At<uint32_t>(self, field::kTile));
    hash.Mix(At<uint32_t>(self, field::kOrientation));
    hash.Mix(At<uint8_t>(self, field::kPlain));
    for (std::size_t offset = field::kPosition; offset < field::kTile; offset += 4)
        hash.Mix(At<uint32_t>(self, offset));
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

// The pure query slots of a landfill occupant (OPENSC4_TEST_LANDFILLOCC=1):
// of the primary interface AsOccupant, GetOrientation and the flag's
// getter; QueryInterface through the occupant part for the landfill
// interface, the occupant and serializable interfaces, 1, the class ID and
// an ID nobody answers (this class answers neither of the last two); of the
// occupant part IsInitialized, GetType, GetPosition, GetBoundingBox,
// GetFlags (the saved bits) and IsFlagSet(4); GetGZCLSID of the
// serializable part; and the exemplar's resource key (its slot 0x2C, the
// query SetTileID makes), whose instance is the tile ID. Never a setter,
// Init, Shutdown, the save functions or a message.
uint32_t Queries(char* self)
{
    Hash hash;
    char* const part = self + field::kOccupant;
    hash.Mix(static_cast<uint32_t>(Virtual<char*>(self, slot::kAsOccupant) - self));
    hash.Mix(Virtual<uint32_t>(self, slot::kGetOrientation));
    hash.Mix(Virtual<uint8_t>(self, slot::kIsPlain));
    for (const uint32_t iid : {landfillocc_sim::kIidLandfill, landfillocc_sim::kIidOccupant,
                               landfillocc_sim::kIidSerializable, 1u, landfillocc_sim::kClassId, 0x0F10A0CCu}) {
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
    uint32_t low[3] = {}, high[3] = {};
    Virtual<void*>(part, occupant::kGetBoundingBox, static_cast<uint32_t*>(low), static_cast<uint32_t*>(high));
    for (int i = 0; i < 3; ++i) {
        hash.Mix(low[i]);
        hash.Mix(high[i]);
    }
    hash.Mix(Virtual<uint8_t>(part, occupant::kGetFlags) & 0xDDu);
    hash.Mix(Virtual<uint8_t>(part, occupant::kIsFlagSet, 4u));
    hash.Mix(Virtual<uint32_t>(self + field::kSerializable, 0x14));
    if (void* const exemplar = At<void*>(self, field::kExemplar)) {
        uint32_t key[3] = {};
        Virtual<uint8_t>(exemplar, 0x2C, static_cast<uint32_t*>(key));
        for (const uint32_t value : key)
            hash.Mix(value);
    }
    return hash.h;
}

// The city's landfill occupants (build 641; src/core/landfillocc_sim.h),
// found through the occupant manager (the city's slot 34): every occupant
// of type 0x6A11BD2B in its grid, each counted once. The count and two
// order-independent checksums of their state (the sum and the exclusive or
// of OccupantHash above; no addresses).
//
// OPENSC4_TEST_LANDFILLOCC=1 adds the sum of the pure query slots' answers
// (Queries above) over every third occupant (those whose state hash is a
// multiple of 3, so the choice does not follow the order of the walk; a
// city has few of them).
void Fingerprint(cISC4City* city, std::string& line)
{
    if (!game::IsBuild641()) {
        line += " | landfillocc none";
        return;
    }
    char* const manager = Virtual<char*>(city, 0x88);
    if (!manager || !At<uint8_t>(manager, occmgr_sim::field::kInitialized)) {
        line += " | landfillocc none";
        return;
    }
    const int test = EnvInt("OPENSC4_TEST_LANDFILLOCC", 0);
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
                if (Virtual<uint32_t>(object, occupant::kGetType) == landfillocc_sim::kOccupantType)
                    seen.insert(static_cast<char*>(object) - field::kOccupant);
            }
        }
    }
    Virtual<void>(manager + occmgr_sim::field::kLock, 0x8);
    uint32_t sum = 0, mixed = 0, asked = 0;
    for (char* const self : seen) {
        const uint32_t hash = OccupantHash(self);
        sum += hash;
        mixed ^= hash;
        if (test == 1 && hash % 3 == 0)
            asked += Queries(self);
    }
    char text[96];
    std::snprintf(text, sizeof text, " | landfillocc %u,%08X,%08X", static_cast<unsigned>(seen.size()), sum, mixed);
    line += text;
    if (test == 1) {
        std::snprintf(text, sizeof text, ",q%08X", asked);
        line += text;
    }
}

const FingerprintPart kFingerprint{"landfillocc", 111, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
