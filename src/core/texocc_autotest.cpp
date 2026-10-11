// The lot base texture occupants' part of the autotest (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/occmgr_sim.h"
#include "core/texocc_sim.h"

#include <cISC4City.h>

#include <opensc4/addresses.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_set>

namespace opensc4::autotest {
namespace {

namespace field = texocc_sim::field;
namespace slot = texocc_sim::slot;
namespace occupant = texocc_sim::occupant;
using texocc_sim::Entry;

struct Hash {
    uint32_t h = 2166136261u;
    void Mix(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    }
};

// The number of entries, held to what the walk below will read.
uint32_t EntryCount(char* self)
{
    const char* const begin = At<char*>(self, field::kEntries);
    const char* const end = At<char*>(self, field::kEntriesEnd);
    return begin && end >= begin ? static_cast<uint32_t>((end - begin) / 16) : 0;
}

// One base texture's state from its fields, without addresses: the class's
// own fields (the box as the floats' bits, the usage, the number of entries
// and of each entry the 14 bytes that are saved: the texture, the cell, the
// orientation, the mirror, the colour, the alpha, +0xC and the picture),
// whether there is an exemplar, and of the occupant base the type, the
// occupant manager's cells, the removal counts, the initialized and
// read-from-a-save bits, the flags as they are saved and the IDs of the
// holder's own properties.
//
// Left out because the interface or the view drives them: the highlight
// (+0x32's high bits), the flags 0x02 and 0x20 (not saved either) and the
// placeable object (+0x34) with the models on it (made when the occupant is
// first shown). Left out as never written: +0x51 to +0x53 (the pool's
// memory) and each entry's last two bytes (the caller's stack, or the
// reader's). The entry vector's capacity is left out (it follows how the
// list was built, not what it holds), and the reference count (the view's
// and the interface's objects hold references).
uint32_t TextureHash(char* self)
{
    Hash hash;
    for (std::size_t offset = field::kBoxLow; offset < field::kUsage; offset += 4)
        hash.Mix(At<uint32_t>(self, offset));
    hash.Mix(At<uint8_t>(self, field::kUsage));
    const uint32_t entries = EntryCount(self);
    hash.Mix(entries);
    const auto* const bytes = At<const uint8_t*>(self, field::kEntries);
    for (uint32_t i = 0; i < entries && i < 4096; ++i)
        hash.Mix(BytesChecksum(bytes + 16 * i, 14));
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

// The game's vectors GetCellCoverage fills (src/core/texocc_sim.cpp).
struct CellVector {
    int32_t* begin;
    int32_t* end;
    int32_t* capacity;
};

struct BitVector {
    uint32_t* startWord;
    int32_t startBit;
    uint32_t* finishWord;
    int32_t finishBit;
    uint32_t* endOfStorage;
};

// The pure query slots of a base texture (OPENSC4_TEST_TEXOCC=1): of the
// primary interface AsOccupant, GetUsage, IsCellCovered (the first entry's
// cell and the cell one further in x and z, each with and without the
// pointer) and GetCellCoverage; QueryInterface through the occupant part
// for the base texture interface, the class ID (not answered), 1 and an ID
// nobody answers; of the occupant part IsInitialized, GetType, GetPosition,
// GetBoundingBox (two buffers of three floats, all the callee writes),
// GetFlags (the saved bits) and IsFlagSet(4); GetGZCLSID of the
// serializable part.
//
// GetCellCoverage appends to the caller's vectors and grows them through
// the game's allocator when they are full: it gets vectors over buffers of
// ours with room for 64 cells and 128 bits, and is asked only of a base
// texture with at most 64 entries, so nothing is allocated (64 cells fill
// the cell buffer exactly; the bit vector's push looks for room before it
// writes, and two of its four words stay free).
//
// Never a setter, the save functions, UpdateVertexAltitudes (it moves the
// occupant and touches the models), LoadModels, UnloadModels; not
// GetHighlight (the interface's).
uint32_t Queries(char* self)
{
    Hash hash;
    char* const part = self + field::kOccupant;
    hash.Mix(static_cast<uint32_t>(Virtual<char*>(self, slot::kAsOccupant) - self));
    hash.Mix(static_cast<uint32_t>(Virtual<int32_t>(self, slot::kGetUsage)));
    const uint32_t entries = EntryCount(self);
    if (entries) {
        const Entry* const first = At<const Entry*>(self, field::kEntries);
        for (uint32_t step = 0; step < 2; ++step) {
            const uint32_t x = first->x + step, z = first->z + step;
            uint8_t fully = 0xEE;
            hash.Mix(Virtual<uint8_t>(self, slot::kIsCellCovered, x, z, &fully));
            hash.Mix(fully);
            hash.Mix(Virtual<uint8_t>(self, slot::kIsCellCovered, x, z, static_cast<uint8_t*>(nullptr)));
        }
    }
    if (entries <= 64) {
        int32_t cellBuffer[128] = {};
        uint32_t bitBuffer[4] = {};
        CellVector cells = {cellBuffer, cellBuffer, cellBuffer + 128};
        BitVector covered = {bitBuffer, 0, bitBuffer, 0, bitBuffer + 4};
        hash.Mix(static_cast<uint32_t>(Virtual<int32_t>(self, slot::kGetCellCoverage, &cells, &covered)));
        const uint32_t listed = static_cast<uint32_t>(cells.end - cells.begin);
        hash.Mix(listed);
        hash.Mix(BytesChecksum(cellBuffer, sizeof(int32_t) * (listed <= 128 ? listed : 128)));
        hash.Mix(static_cast<uint32_t>(covered.finishWord - covered.startWord) * 32u +
                 static_cast<uint32_t>(covered.finishBit));
        hash.Mix(bitBuffer[0]);
        hash.Mix(bitBuffer[1]);
        // And without the bit vector.
        CellVector again = {cellBuffer, cellBuffer, cellBuffer + 128};
        hash.Mix(static_cast<uint32_t>(
            Virtual<int32_t>(self, slot::kGetCellCoverage, &again, static_cast<BitVector*>(nullptr))));
        hash.Mix(static_cast<uint32_t>(again.end - again.begin));
    }
    for (const uint32_t iid : {texocc_sim::kIidBaseTexture, texocc_sim::kClassId, 1u, 0x0F10A0CCu}) {
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
    float position[3] = {};
    Virtual<void*>(part, occupant::kGetPosition, static_cast<float*>(position));
    for (const float value : position)
        hash.Mix(BytesChecksum(&value, 4));
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

// The city's base textures (build 641; src/core/texocc_sim.h), found
// through the occupant manager (the city's slot 34): every occupant of type
// 0x497F6D9D in its grid, one in several cells counted once. The count, the
// number of entries of them all, and two order-independent checksums of the
// base textures' state (the sum and the exclusive or of TextureHash above;
// no addresses).
//
// OPENSC4_TEST_TEXOCC=1 adds the sum of the pure query slots' answers
// (Queries above) over every third base texture (those whose state hash is
// a multiple of 3, so the choice does not follow the order of the walk).
void Fingerprint(cISC4City* city, std::string& line)
{
    if (!game::IsBuild641()) {
        line += " | texocc none";
        return;
    }
    char* const manager = Virtual<char*>(city, 0x88);
    if (!manager || !At<uint8_t>(manager, occmgr_sim::field::kInitialized)) {
        line += " | texocc none";
        return;
    }
    const int test = EnvInt("OPENSC4_TEST_TEXOCC", 0);
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
                if (Virtual<uint32_t>(object, occupant::kGetType) == texocc_sim::kOccupantType)
                    seen.insert(static_cast<char*>(object) - field::kOccupant);
            }
        }
    }
    Virtual<void>(manager + occmgr_sim::field::kLock, 0x8);
    uint32_t sum = 0, mixed = 0, asked = 0, entries = 0;
    for (char* const self : seen) {
        const uint32_t hash = TextureHash(self);
        sum += hash;
        mixed ^= hash;
        entries += EntryCount(self);
        if (test == 1 && hash % 3 == 0)
            asked += Queries(self);
    }
    char text[112];
    std::snprintf(text, sizeof text, " | texocc %u,%u,%08X,%08X", static_cast<unsigned>(seen.size()),
                  static_cast<unsigned>(entries), sum, mixed);
    line += text;
    if (test == 1) {
        std::snprintf(text, sizeof text, ",q%08X", asked);
        line += text;
    }
}

const FingerprintPart kFingerprint{"texocc", 114, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
