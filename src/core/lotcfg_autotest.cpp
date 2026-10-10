// The lot configuration's part of the autotest (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/lotcfg_sim.h"
#include "core/lotconfig_sim.h"

#include <cISC4City.h>

#include <opensc4/addresses.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace opensc4::autotest {
namespace {

using lotcfg_sim::BitVector;
using lotcfg_sim::GameString;
using lotcfg_sim::ObjectVector;
using lotcfg_sim::Vector32;
using lotconfig_sim::FirstNode;
using lotconfig_sim::HashTable;
using lotconfig_sim::NextNode;
namespace field = lotcfg_sim::field;

struct Hash {
    uint32_t h = 2166136261u;
    void Mix(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    }
    void MixVector(const Vector32* vector)
    {
        Mix(static_cast<uint32_t>(vector->end - vector->begin));
        for (const uint32_t* value = vector->begin; value != vector->end; ++value)
            Mix(*value);
    }
};

uint32_t Bits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

// The offset of a pointer a slot returned from a base, never the address.
uint32_t Offset(const void* pointer, const void* base)
{
    if (!pointer)
        return 0xFFFFFFFFu;
    return static_cast<uint32_t>(static_cast<const char*>(pointer) - static_cast<const char*>(base));
}

// Every pure query slot of one configuration (vtable 0x00A8CEB8; argument
// counts from each slot's `ret N`, src/core/lotcfg_sim.h):
//   - QueryInterface (0) with an unknown IID and with the configuration's
//     own, the reference it adds given back through Release (2; the map
//     holds a reference, so the count never reaches the last one);
//   - GetID (3); GetName (4) into a game string of the test's own (8 bytes
//     from the STL allocator, freed after: alike in both runs);
//   - BelongsToFamily (5) with the first family and with 0xFFFFFFFF; the
//     family vector (6); GetSize (7); the three slopes (8 to 10); the
//     growth stage (11); the capacities (12, 13); water (14);
//   - the zone, wealth and purpose bits (15 to 17), every index of each
//     vector; the style test (18) with the first style and 0xFFFFFFFF; the
//     stub (19); IsDoConstruction (20);
//   - GetRequiredRoads (21) into a bit vector of the test's own (its words
//     from the game's allocator, freed after);
//   - the object count and vector (22, 23); the first building, texture
//     and foundation object (24 to 26) as offsets in the vector;
//     HasObjectsOfType (27) for kinds 0 to 8;
//   - the retaining wall, fence and fence position vectors (28 to 30).
// Nothing here changes a configuration.
void MixQueries(Hash& hash, char* configuration)
{
    void* same = nullptr;
    hash.Mix(Virtual<uint8_t>(configuration, 0x0, 0x12345678u, &same));
    hash.Mix(same == nullptr);
    if (At<uint32_t>(configuration, field::kRefCount) >= 1) {
        const uint8_t found = Virtual<uint8_t>(configuration, 0x0, lotcfg_sim::kIidConfiguration, &same);
        hash.Mix(found);
        hash.Mix(same == configuration);
        if (found)
            Virtual<uint32_t>(configuration, 0x8);
    }
    hash.Mix(Virtual<uint32_t>(configuration, 0xC));

    GameString name{};
    char* const text = static_cast<char*>(lotcfg_sim::Allocate(8));
    name.begin = text;
    name.end = text;
    name.capacity = text + 8;
    *text = 0;
    name.vtable = addr::SC4String_Vtable;
    name.references = 0;
    Virtual<uint32_t>(configuration, 0x10, static_cast<void*>(&name));
    hash.Mix(static_cast<uint32_t>(name.end - name.begin));
    for (const char* letter = name.begin; letter != name.end; ++letter)
        hash.Mix(static_cast<uint8_t>(*letter));
    if (name.begin)
        lotcfg_sim::Free(name.begin);

    const Vector32* const families = Virtual<Vector32*>(configuration, 0x18);
    hash.Mix(Offset(families, configuration));
    hash.MixVector(families);
    if (families->begin != families->end)
        hash.Mix(Virtual<uint8_t>(configuration, 0x14, families->begin[0]));
    hash.Mix(Virtual<uint8_t>(configuration, 0x14, 0xFFFFFFFFu));

    uint32_t width = 0, depth = 0;
    Virtual<void>(configuration, 0x1C, &width, &depth);
    hash.Mix(width | (depth << 16));
    hash.Mix(Bits(Virtual<float>(configuration, 0x20)));
    hash.Mix(Bits(Virtual<float>(configuration, 0x24)));
    hash.Mix(Bits(Virtual<float>(configuration, 0x28)));
    hash.Mix(Virtual<uint8_t>(configuration, 0x2C));
    hash.Mix(Virtual<uint16_t>(configuration, 0x30));
    hash.Mix(Virtual<uint16_t>(configuration, 0x34));
    hash.Mix(Virtual<uint8_t>(configuration, 0x38));

    const struct {
        std::size_t slot;
        std::size_t bits;
    } tests[] = {{0x3C, field::kZoneTypes}, {0x40, field::kWealthTypes}, {0x44, field::kPurposeTypes}};
    for (const auto& test : tests) {
        uint32_t count = lotcfg_sim::BitCount(reinterpret_cast<const BitVector*>(configuration + test.bits));
        hash.Mix(count);
        if (count > 64)
            count = 64;
        uint32_t packed = 0;
        for (uint32_t i = 0; i < count; ++i)
            packed = (packed << 1) | (Virtual<uint32_t>(configuration, test.slot, i) & 1u);
        hash.Mix(packed);
    }

    const Vector32* const styles = reinterpret_cast<const Vector32*>(configuration + field::kStyles);
    if (styles->begin != styles->end)
        hash.Mix(Virtual<uint8_t>(configuration, 0x48, styles->begin[0]));
    hash.Mix(Virtual<uint8_t>(configuration, 0x48, 0xFFFFFFFFu));
    hash.Mix(Virtual<uint8_t>(configuration, 0x4C, 0u));
    hash.Mix(Virtual<uint8_t>(configuration, 0x50));

    BitVector roads{};
    Virtual<uint32_t>(configuration, 0x54, &roads);
    const uint32_t roadBits = lotcfg_sim::BitCount(&roads);
    hash.Mix(roadBits);
    for (uint32_t i = 0; i < roadBits && i < 32; ++i) {
        uint32_t bit = 0;
        const uint32_t* const word = lotcfg_sim::BitWord(&roads, i, bit);
        hash.Mix((*word >> bit) & 1u);
    }
    if (roads.beginWord)
        lotcfg_sim::Free(roads.beginWord);

    hash.Mix(static_cast<uint32_t>(Virtual<int32_t>(configuration, 0x58)));
    const ObjectVector* const objects = Virtual<ObjectVector*>(configuration, 0x5C);
    hash.Mix(Offset(objects, configuration));
    for (const std::size_t slot : {std::size_t{0x60}, std::size_t{0x64}, std::size_t{0x68}})
        hash.Mix(Offset(Virtual<char*>(configuration, slot), objects->begin));
    uint32_t kinds = 0;
    for (uint32_t kind = 0; kind <= 8; ++kind)
        kinds = (kinds << 1) | (Virtual<uint8_t>(configuration, 0x6C, kind) & 1u);
    hash.Mix(kinds);

    for (const std::size_t slot : {std::size_t{0x70}, std::size_t{0x74}, std::size_t{0x78}}) {
        const Vector32* const vector = Virtual<Vector32*>(configuration, slot);
        hash.Mix(Offset(vector, configuration));
        hash.MixVector(vector);
    }
}

// The lot configurations (build 641; src/core/lotcfg_sim.h), found through
// the lot configuration manager (the city's slot 37) and its static map
// 0x00B216C8, walked in bucket order as lotconfig_autotest.cpp does: the
// number of configurations. The configurations' full state is in the
// `lotconfig` part. OPENSC4_TEST_LOTCFG=1 adds the number of configurations
// asked and a checksum of every pure query slot (MixQueries above) called
// on every 16th configuration of the walk. No addresses.
void Fingerprint(cISC4City* city, std::string& line)
{
    if (!game::IsBuild641()) {
        line += " | lotcfg none";
        return;
    }
    char* const manager = Virtual<char*>(city, 0x94);
    if (!manager || !At<uint8_t>(manager, lotconfig_sim::field::kInitialized)) {
        line += " | lotcfg none";
        return;
    }
    const HashTable* const map = lotconfig_sim::Map();
    char text[64];
    std::snprintf(text, sizeof text, " | lotcfg %u", map->count);
    line += text;
    if (EnvInt("OPENSC4_TEST_LOTCFG", 0) < 1)
        return;
    Hash hash;
    uint32_t walked = 0, asked = 0;
    for (char* node = FirstNode(map); node && walked < 0x100000;
         node = NextNode(map, node, At<uint32_t>(node, 0x4)), ++walked) {
        if (walked % 16 != 0)
            continue;
        hash.Mix(At<uint32_t>(node, 0x4));
        MixQueries(hash, At<char*>(node, 0x8));
        ++asked;
    }
    std::snprintf(text, sizeof text, ",%u,q%08X", asked, hash.h);
    line += text;
}

const FingerprintPart kFingerprint{"lotcfg", 102, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
