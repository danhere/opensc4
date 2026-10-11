// The lot retaining wall occupants' part of the autotest
// (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/occmgr_sim.h"
#include "core/wallocc_sim.h"

#include <cISC4City.h>

#include <opensc4/addresses.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_set>

namespace opensc4::autotest {
namespace {

namespace field = wallocc_sim::field;
namespace slot = wallocc_sim::slot;
namespace occupant = wallocc_sim::occupant;

struct Hash {
    uint32_t h = 2166136261u;
    void Mix(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    }
};

// One wall's state from its fields, without addresses: the class's own
// fields (the wall type, whether there is a lot and that lot's cell, the
// four edge hashes, the position and the size, so the box, as the floats'
// bits), whether there is an exemplar, and of the occupant base the type,
// the occupant manager's cells, the removal counts, the initialized and
// read-from-a-save bits, the flags as they are saved and the IDs of the
// holder's own properties.
//
// Left out because the interface or the view drives them: the edge flags
// (+0x40: the code that builds the walls' drawables, 0x005E3400, sets and
// clears a side's bit by whether its geometry could be made), the highlight
// (+0x32's high bits), the flags 0x02 and 0x20 (not saved either) and the
// placeable object (+0x34; made when the occupant is first drawn). The
// reference count is left out too (the view's and the interface's objects
// hold references). The lot enters by its cell (its slot 21, two bytes of
// the lot), not by its address. No field is left unset by the constructor.
uint32_t WallHash(char* self)
{
    Hash hash;
    hash.Mix(At<uint32_t>(self, field::kWallType));
    void* const lot = At<void*>(self, field::kLot);
    hash.Mix(lot != nullptr);
    if (lot) {
        uint32_t x = 0, z = 0;
        Virtual<void>(lot, 0x54, &x, &z);
        hash.Mix(x);
        hash.Mix(z);
    }
    for (std::size_t offset = field::kEdgeHashes; offset < wallocc_sim::kObjectSize; offset += 4)
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

// The pure query slots of a wall (OPENSC4_TEST_WALLOCC=1): of the primary
// interface AsOccupant, GetLot (whether it is the field's), GetEdgeHashArray
// (a buffer of four values, all the callee writes) and
// GetRetainingWallType; QueryInterface through the occupant part for the
// wall interface, the class ID (not answered), 1 and an ID nobody answers;
// of the occupant part IsInitialized, GetType, GetPosition, GetBoundingBox
// (two buffers of three floats, all the callee writes), GetFlags (the saved
// bits) and IsFlagSet(4); GetGZCLSID of the serializable part. Then, for a
// wall with a lot, the sides' hashes computed again by the class's free
// function 0x005E2CE0, called at its address with the lot's cells and
// altitude as the game calls it (it reads the lot manager and the terrain
// and writes only its two buffers): its result, the four hashes and the
// height. Never a setter, the save functions or a message; not GetEdgeFlags
// (the view's field) or GetHighlight (the interface's).
uint32_t Queries(char* self)
{
    Hash hash;
    char* const part = self + field::kOccupant;
    hash.Mix(static_cast<uint32_t>(Virtual<char*>(self, slot::kAsOccupant) - self));
    void* const lot = Virtual<void*>(self, slot::kGetLot);
    hash.Mix(lot == At<void*>(self, field::kLot));
    uint32_t stored[4] = {};
    Virtual<void*>(self, slot::kGetEdgeHashArray, static_cast<uint32_t*>(stored));
    for (const uint32_t value : stored)
        hash.Mix(value);
    hash.Mix(Virtual<uint32_t>(self, slot::kGetRetainingWallType));
    for (const uint32_t iid : {wallocc_sim::kIidWall, wallocc_sim::kClassId, 1u, 0x0F10A0CCu}) {
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
    float low[3] = {}, high[3] = {};
    Virtual<void*>(part, occupant::kGetBoundingBox, static_cast<float*>(low), static_cast<float*>(high));
    for (int i = 0; i < 3; ++i) {
        hash.Mix(BytesChecksum(&low[i], 4));
        hash.Mix(BytesChecksum(&high[i], 4));
    }
    hash.Mix(Virtual<uint8_t>(part, occupant::kGetFlags) & 0xDDu);
    hash.Mix(Virtual<uint8_t>(part, occupant::kIsFlagSet, 4u));
    hash.Mix(Virtual<uint32_t>(self + field::kSerializable, 0x14));
    if (lot) {
        int32_t cells[4] = {};
        Virtual<void>(lot, 0x38, static_cast<int32_t*>(cells));
        const float altitude = Virtual<float>(lot, 0x40);
        uint32_t hashes[4] = {};
        float highest = 0.0f;
        using ComputeFn = uint8_t(__cdecl*)(void*, float, const int32_t*, uint32_t*, float*);
        hash.Mix(reinterpret_cast<ComputeFn>(addr::cSC4LotRetainingWallOccupant_ComputeEdgeHashes)(
            lot, altitude, cells, hashes, &highest));
        for (const uint32_t value : hashes)
            hash.Mix(value);
        hash.Mix(BytesChecksum(&highest, 4));
    }
    return hash.h;
}

// The city's retaining walls (build 641; src/core/wallocc_sim.h), found
// through the occupant manager (the city's slot 34): every occupant of type
// 0x895D1169 in its grid, one in several cells counted once. The count and
// two order-independent checksums of the walls' state (the sum and the
// exclusive or of WallHash above; no addresses).
//
// OPENSC4_TEST_WALLOCC=1 adds the sum of the pure query slots' answers
// (Queries above) over every third wall (those whose state hash is a
// multiple of 3, so the choice does not follow the order of the walk).
void Fingerprint(cISC4City* city, std::string& line)
{
    if (!game::IsBuild641()) {
        line += " | wallocc none";
        return;
    }
    char* const manager = Virtual<char*>(city, 0x88);
    if (!manager || !At<uint8_t>(manager, occmgr_sim::field::kInitialized)) {
        line += " | wallocc none";
        return;
    }
    const int test = EnvInt("OPENSC4_TEST_WALLOCC", 0);
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
                if (Virtual<uint32_t>(object, occupant::kGetType) == wallocc_sim::kOccupantType)
                    seen.insert(static_cast<char*>(object) - field::kOccupant);
            }
        }
    }
    Virtual<void>(manager + occmgr_sim::field::kLock, 0x8);
    uint32_t sum = 0, mixed = 0, asked = 0;
    for (char* const self : seen) {
        const uint32_t hash = WallHash(self);
        sum += hash;
        mixed ^= hash;
        if (test == 1 && hash % 3 == 0)
            asked += Queries(self);
    }
    char text[96];
    std::snprintf(text, sizeof text, " | wallocc %u,%08X,%08X", static_cast<unsigned>(seen.size()), sum, mixed);
    line += text;
    if (test == 1) {
        std::snprintf(text, sizeof text, ",q%08X", asked);
        line += text;
    }
}

const FingerprintPart kFingerprint{"wallocc", 112, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
