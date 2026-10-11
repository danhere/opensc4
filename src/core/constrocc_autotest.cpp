// The construction occupants' part of the autotest (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/constrocc_sim.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/occmgr_sim.h"

#include <cISC4City.h>

#include <opensc4/addresses.h>

#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <unordered_set>

namespace opensc4::autotest {
namespace {

namespace field = constrocc_sim::field;
namespace slot = constrocc_sim::slot;
namespace occupant = constrocc_sim::occupant;

struct Hash {
    uint32_t h = 2166136261u;
    void Mix(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    }
};

// One construction's state from its fields, without addresses: what the
// simulation's two functions keep (the stage, the start day, the day of the
// next step, the floors and the floors started, the days of the four
// stages), the two heights the schedule is made from, the occupant's box
// and the second box's five steady floats (as the floats' bits), whether
// there is a lot, a building and an exemplar, and of the occupant base the
// type, the occupant manager's cells, the removal counts, the initialized
// and read-from-a-save bits, the flags as they are saved and the IDs of the
// holder's own properties.
//
// Left out because frames drive them (the frame's animation 0x005CE110 on
// message 0x86AD10EE, by real milliseconds): the scaffolding's height now
// (+0xD0, the second box's high y), the crane's y (+0x10C), the elapsed
// time (+0x170) and the crane's state, times and targets (+0x174, +0x184 to
// +0x190). Left out because the drawing's setup makes them, with draws from
// the class's own random generator (0x00B22FB0, which the frame's animation
// draws from too, so its sequence follows frames): the crane's place and
// angles (+0x108, +0x110, +0x178, +0x17C), the side byte (+0x124), and the
// counts and members of the two prop lists, the effects and the network
// list. Left out as the view's or the interface's: the
// transform (the renderer's slots read and set it), the vertex buffers and
// texture rows, flag 0x02 (the model is in the renderer) and 0x20, the
// highlight, the placeable object, the model part. Left out as never
// written by the constructor (the pool's memory): +0xD8, the padding bytes.
// The reference count is left out too.
uint32_t ConstructionHash(char* self)
{
    Hash hash;
    hash.Mix(At<uint32_t>(self, field::kStage));
    hash.Mix(At<uint32_t>(self, field::kStartDay));
    hash.Mix(At<uint32_t>(self, field::kNextDay));
    hash.Mix(At<uint32_t>(self, field::kFloors));
    hash.Mix(At<uint32_t>(self, field::kFloor));
    hash.Mix(At<uint32_t>(self, field::kHeight158));
    hash.Mix(At<uint32_t>(self, field::kFirstHeight));
    hash.Mix(At<uint32_t>(self, field::kFirstDays));
    hash.Mix(At<uint32_t>(self, field::kPauseDays));
    hash.Mix(At<uint32_t>(self, field::kLastDays));
    hash.Mix(At<uint32_t>(self, field::kFloorDays));
    for (std::size_t offset = 0; offset < 0x18; offset += 4)
        hash.Mix(At<uint32_t>(self, field::kBox + offset));
    for (std::size_t offset = 0; offset < 0x18; offset += 4) {
        if (offset != 0x10)
            hash.Mix(At<uint32_t>(self, field::kInnerBox + offset));
    }
    hash.Mix(At<void*>(self, field::kLot) != nullptr);
    hash.Mix(At<void*>(self, field::kBuilding) != nullptr);
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

// One QueryInterface through the slot 0 of a part: whether it answered and,
// if so, where the answer lies in the object; the reference it gave is
// released (slot 2 of whatever part came back).
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

// The pure query slots of a construction (OPENSC4_TEST_CONSTROCC=1): of the
// primary interface AsOccupant; QueryInterface through the occupant part
// for the construction interface, the class ID (not answered), 1 and an ID
// nobody answers, and for the construction interface through every other
// part (the message target, the serializable part, the holder, the exemplar
// holder and the model part's virtual base); of the occupant part
// IsInitialized, GetType, GetPosition (three floats), GetBoundingBox (two
// buffers of three floats, all the callee writes), GetFlags (the saved
// bits) and IsFlagSet(4); GetGZCLSID of the serializable part. Never
// SetLot, the step, the save functions or a message; not GetHighlight (the
// interface's). The position and the box are steady: the frame's animation
// moves the second box, not this one.
uint32_t Queries(char* self)
{
    Hash hash;
    char* const part = self + field::kOccupant;
    hash.Mix(static_cast<uint32_t>(Virtual<char*>(self, slot::kAsOccupant) - self));
    for (const uint32_t iid : {constrocc_sim::kIidConstruction, constrocc_sim::kClassId, 1u, 0x0F10A0CCu})
        AskInterface(hash, self, part, iid);
    for (const std::size_t other : {field::kMessageTarget, field::kSerializable, field::kHolder, field::kExemplarPart,
                                    field::kModelBase})
        AskInterface(hash, self, self + other, constrocc_sim::kIidConstruction);
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
    return hash.h;
}

// The city's construction sites (build 641; src/core/constrocc_sim.h),
// found through the occupant manager (the city's slot 34): every occupant
// of type 0x096E763C in its grid, one in several cells counted once. A
// site is in the grid from StartConstruction until its last step takes it
// out. The count and two order-independent checksums of the sites' state
// (the sum and the exclusive or of ConstructionHash above; no addresses).
//
// OPENSC4_TEST_CONSTROCC=1 adds the sum of the pure query slots' answers
// (Queries above) over every second site (those whose state hash is even,
// so the choice does not follow the order of the walk).
void Fingerprint(cISC4City* city, std::string& line)
{
    if (!game::IsBuild641()) {
        line += " | constrocc none";
        return;
    }
    char* const manager = Virtual<char*>(city, 0x88);
    if (!manager || !At<uint8_t>(manager, occmgr_sim::field::kInitialized)) {
        line += " | constrocc none";
        return;
    }
    const int test = EnvInt("OPENSC4_TEST_CONSTROCC", 0);
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
                if (Virtual<uint32_t>(object, occupant::kGetType) == constrocc_sim::kOccupantType)
                    seen.insert(static_cast<char*>(object) - field::kOccupant);
            }
        }
    }
    Virtual<void>(manager + occmgr_sim::field::kLock, 0x8);
    uint32_t sum = 0, mixed = 0, asked = 0;
    for (char* const self : seen) {
        const uint32_t hash = ConstructionHash(self);
        sum += hash;
        mixed ^= hash;
        if (test == 1 && hash % 2 == 0)
            asked += Queries(self);
    }
    char text[96];
    std::snprintf(text, sizeof text, " | constrocc %u,%08X,%08X", static_cast<unsigned>(seen.size()), sum, mixed);
    line += text;
    if (test == 1) {
        std::snprintf(text, sizeof text, ",q%08X", asked);
        line += text;
    }
}

const FingerprintPart kFingerprint{"constrocc", 113, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
