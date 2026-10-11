// The occupant base class's part of the autotest (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/occbase_sim.h"
#include "core/occmgr_sim.h"

#include <cISC4City.h>

#include <opensc4/addresses.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace opensc4::autotest {
namespace {

namespace field = occbase_sim::field;
namespace slot = occbase_sim::slot;
using occbase_sim::kPropertyGroups;
using occbase_sim::Set;
using occbase_sim::TreeNode;

struct Hash {
    uint32_t h = 2166136261u;
    void Mix(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    }
};

// The occupant's groups without a copy, as IsOccupantGroup (0x005E92D0)
// reads them: the property 0xAA1DD396 through the holder (the occupant's
// own or its exemplar's), its value's data and count. False for none.
bool Groups(char* self, const uint32_t*& first, uint32_t& count)
{
    void* const holder = Virtual<void*>(self, slot::kAsPropertyHolder);
    void* const property = holder ? Virtual<void*>(holder, 0x24, kPropertyGroups) : nullptr;
    if (!property)
        return false;
    void* const variant = Virtual<void*>(property, 0x14);
    first = Virtual<const uint32_t*>(variant, 0x19C);
    count = Virtual<uint32_t>(variant, 0x10);
    return first != nullptr;
}

// One occupant's base state from its fields, without addresses: the type,
// the occupant manager's cells, the two removal counts, of the state byte
// the initialized and read-from-a-save bits, the flags as the game saves
// them (without 0x02 and 0x20) and the occupant groups.
//
// Left out, because the interface, the camera or frames drive them and so
// two runs of the original need not agree:
//   - the highlight (the state's high four bits, SetHighlight 0x005E90E0:
//     the interface's; docs/bldgocc.md found it differs between runs);
//   - flag 0x02 (set and cleared by view code, 0x00493B90, 0x004A2690,
//     0x005D1090 and the prop developer's ShowSignsAndLabels 0x006F1210)
//     and flag 0x20 (0x00446150 and the automata's code); Write 0x005E8CD0
//     masks both out of the save (& 0xDD);
//   - the placeable object (+0x30: made when the occupant is first drawn).
uint32_t StateHash(char* self)
{
    Hash hash;
    hash.Mix(At<uint32_t>(self, field::kType));
    hash.Mix(At<uint32_t>(self, field::kManagerBox));
    hash.Mix(At<uint8_t>(self, field::kRemoval1));
    hash.Mix(At<uint8_t>(self, field::kRemoval2));
    hash.Mix(At<uint8_t>(self, field::kState) & 3u);
    hash.Mix(At<uint8_t>(self, field::kFlags) & uint32_t{occbase_sim::kFlagsSaved});
    const uint32_t* first = nullptr;
    uint32_t count = 0;
    if (Groups(self, first, count)) {
        hash.Mix(count);
        for (uint32_t i = 0; i < count && i < 64; ++i)
            hash.Mix(first[i]);
    } else {
        hash.Mix(0xFFFFFFFF);
    }
    return hash.h;
}

using AllocateFn = void*(__cdecl*)(std::size_t);
using FreeFn = void(__cdecl*)(void*);
using EraseFn = void(__thiscall*)(Set*, TreeNode*);

// An empty set of the game's (the header node from the game's allocator,
// as the set's own code frees nodes with the game's free).
Set NewSet()
{
    auto* const header = static_cast<TreeNode*>(reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate)(0x14));
    header->color = 0;
    header->parent = nullptr;
    header->left = header;
    header->right = header;
    header->value = 0;
    return {header, 0};
}

void FreeSet(Set& set)
{
    if (set.size)
        reinterpret_cast<EraseFn>(addr::SC4Uint32Set_EraseNodes)(&set, set.header->parent);
    reinterpret_cast<FreeFn>(addr::SC4_Free)(set.header);
    set = {nullptr, 0};
}

void MixTree(Hash& hash, const TreeNode* node, int depth)
{
    if (!node || depth > 64)
        return;
    MixTree(hash, node->left, depth + 1);
    hash.Mix(node->value);
    MixTree(hash, node->right, depth + 1);
}

// The pure query slots of an occupant (OPENSC4_TEST_OCCBASE=1), through
// its vtable: IsInitialized, GetType, AsPropertyHolder (where it points),
// IsFlagSet for each flag that is kept and for two combinations, GetFlags
// (masked), CanRemove for 0 to 3, GetOccupantManagerBBox,
// GetBoundingCityCells (not a prop's: some props move by frames inside
// their manager cells, as occmgr_autotest.cpp found), IsOccupantGroup for
// a few fixed groups and for the occupant's own first and last,
// GetOccupantGroups into `set` (the groups in order), GetLotTag (whether
// there is one; the tag itself is not mixed) and QueryInterface for the occupant and the serializable interface (where
// they point; each reference released again). Never a setter, a message
// or the save functions; the highlight and the placeable object are not
// asked.
uint32_t Queries(char* self, Set& set)
{
    Hash hash;
    hash.Mix(Virtual<uint32_t>(self, slot::kIsInitialized));
    const uint32_t type = Virtual<uint32_t>(self, slot::kGetType);
    hash.Mix(type);
    char* const holder = Virtual<char*>(self, slot::kAsPropertyHolder);
    hash.Mix(static_cast<uint32_t>(holder - self));
    for (const uint32_t bits : {0x01u, 0x04u, 0x08u, 0x10u, 0x40u, 0x80u, 0x05u, 0xC0u, 0x100u})
        hash.Mix(Virtual<uint32_t>(self, slot::kIsFlagSet, bits));
    hash.Mix(Virtual<uint32_t>(self, slot::kGetFlags) & uint32_t{occbase_sim::kFlagsSaved});
    for (uint32_t flags = 0; flags < 4; ++flags)
        hash.Mix(Virtual<uint8_t>(self, slot::kCanRemove, flags));
    uint8_t box[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    hash.Mix(Virtual<uint8_t>(self, slot::kGetOccupantManagerBBox, static_cast<uint8_t*>(box)));
    hash.Mix(static_cast<uint32_t>(box[0]) | static_cast<uint32_t>(box[1]) << 8 | static_cast<uint32_t>(box[2]) << 16 |
             static_cast<uint32_t>(box[3]) << 24);
    if (type != occbase_sim::kTypeProp) {
        int32_t cells[4] = {};
        Virtual<uint32_t>(self, slot::kGetBoundingCityCells, static_cast<int32_t*>(cells));
        for (const int32_t cell : cells)
            hash.Mix(static_cast<uint32_t>(cell));
    }
    for (const uint32_t group : {0x1000u, 0x1001u, 0x1002u, 0x1500u, 0x1700u, 0x1906u, 0u})
        hash.Mix(Virtual<uint8_t>(self, slot::kIsOccupantGroup, group));
    const uint32_t* first = nullptr;
    uint32_t count = 0;
    if (Groups(self, first, count) && count > 0) {
        hash.Mix(Virtual<uint8_t>(self, slot::kIsOccupantGroup, first[0]));
        hash.Mix(Virtual<uint8_t>(self, slot::kIsOccupantGroup, first[count - 1]));
        hash.Mix(Virtual<uint8_t>(self, slot::kIsOccupantGroup, first[0] ^ 0x40000000u));
    }
    hash.Mix(Virtual<uint8_t>(self, slot::kGetOccupantGroups, &set));
    hash.Mix(set.size);
    MixTree(hash, set.header->parent, 0);
    uint32_t tag = 0;
    hash.Mix(Virtual<uint8_t>(self, slot::kGetLotTag, &tag));
    for (const uint32_t iid : {occbase_sim::kIidOccupant, occbase_sim::kIidSerializable}) {
        void* found = nullptr;
        const bool answered = Virtual<uint8_t>(self, slot::kQueryInterface, iid, &found) != 0;
        hash.Mix(answered);
        if (answered && found) {
            hash.Mix(static_cast<uint32_t>(static_cast<char*>(found) - self));
            Virtual<uint32_t>(found, 0x8);
        }
    }
    return hash.h;
}

// The occupant base's state over the city (build 641; src/core/
// occbase_sim.h): every occupant in the occupant manager's grid (the
// city's slot 34), one in several cells counted once, except the automata
// (types 0xC7CC75F1 and 0x496E5783: they come and go by frames). The
// count and two order-independent checksums (the sum and the exclusive or
// of StateHash above; no addresses).
//
// OPENSC4_TEST_OCCBASE=1 adds the sum of the pure query slots' answers
// (Queries above) over the occupants whose state hash is a multiple of 7,
// asked in the order of their hashes so that the set's allocations do not
// follow addresses.
// OPENSC4_TEST_OCCBASE=2 adds instead what the checksums leave out, to see
// how it varies: how many occupants have each of the eight flags, a
// highlight, a placeable object. Two runs of the original need not agree
// on those.
void Fingerprint(cISC4City* city, std::string& line)
{
    if (!game::IsBuild641()) {
        line += " | occbase none";
        return;
    }
    char* const manager = Virtual<char*>(city, 0x88);
    if (!manager || !At<uint8_t>(manager, occmgr_sim::field::kInitialized)) {
        line += " | occbase none";
        return;
    }
    const int test = EnvInt("OPENSC4_TEST_OCCBASE", 0);
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
                char* const object = static_cast<char*>(occmgr_sim::OccupantOf(node));
                const uint32_t type = Virtual<uint32_t>(object, slot::kGetType);
                if (type != occbase_sim::kTypeAutomaton && type != occbase_sim::kTypeAutomaton2)
                    seen.insert(object);
            }
        }
    }
    Virtual<void>(manager + occmgr_sim::field::kLock, 0x8);

    uint32_t sum = 0, mixed = 0;
    uint32_t flagged[8] = {}, highlighted = 0, placeable = 0;
    std::vector<std::pair<uint32_t, char*>> chosen;
    for (char* const self : seen) {
        const uint32_t hash = StateHash(self);
        sum += hash;
        mixed ^= hash;
        if (test == 1 && hash % 7 == 0)
            chosen.emplace_back(hash, self);
        if (test == 2) {
            const uint8_t flags = At<uint8_t>(self, field::kFlags);
            for (int bit = 0; bit < 8; ++bit)
                flagged[bit] += (flags >> bit) & 1u;
            highlighted += (At<uint8_t>(self, field::kState) >> 4) != 0;
            placeable += At<void*>(self, field::kPlaceable) != nullptr;
        }
    }
    char text[200];
    std::snprintf(text, sizeof text, " | occbase %u,%08X,%08X", static_cast<unsigned>(seen.size()), sum, mixed);
    line += text;
    if (test == 1) {
        // Equal hashes mean equal state, so their order does not matter.
        std::sort(chosen.begin(), chosen.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        Set set = NewSet();
        uint32_t asked = 0;
        for (const auto& [hash, self] : chosen)
            asked += Queries(self, set);
        FreeSet(set);
        std::snprintf(text, sizeof text, ",q%u/%08X", static_cast<unsigned>(chosen.size()), asked);
        line += text;
    } else if (test == 2) {
        std::snprintf(text, sizeof text, ",f%u/%u/%u/%u/%u/%u/%u/%u,h%u,p%u", flagged[0], flagged[1], flagged[2],
                      flagged[3], flagged[4], flagged[5], flagged[6], flagged[7], highlighted, placeable);
        line += text;
    }
}

const FingerprintPart kFingerprint{"occbase", 106, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
