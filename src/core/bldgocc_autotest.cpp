// The building occupants' part of the autotest (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/bldgocc_sim.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/occmgr_sim.h"

#include <cISC4City.h>

#include <opensc4/addresses.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_set>

namespace opensc4::autotest {
namespace {

namespace field = bldgocc_sim::field;
namespace slot = bldgocc_sim::slot;
namespace occupant = bldgocc_sim::occupant;

constexpr uint32_t kPropertyName = 0x899AFBAD;

struct Hash {
    uint32_t h = 2166136261u;
    void Mix(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    }
    void MixBytes(const char* text, std::size_t size)
    {
        Mix(static_cast<uint32_t>(size));
        for (std::size_t i = 0; i < size; ++i)
            h = (h ^ static_cast<uint8_t>(text[i])) * 16777619u;
    }
};

// The game's string (vtable 0x00A80810): begin, end and capacity of its
// buffer, then a count.
struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};

void NewString(GameString& string)
{
    using AllocateFn = void*(__cdecl*)(std::size_t);
    string.begin = static_cast<char*>(reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate)(8));
    string.end = string.begin;
    string.capacity = string.begin + 8;
    string.begin[0] = 0;
    string.vtable = addr::SC4String_Vtable;
    string.zero = 0;
}

void FreeString(GameString& string)
{
    using FreeFn = void(__cdecl*)(void*);
    if (string.begin)
        reinterpret_cast<FreeFn>(addr::SC4_Free)(string.begin);
    string.begin = string.end = string.capacity = nullptr;
}

// A cIGZString's text: the buffer of the game's own string class, else
// through ToChar (slot 7), at most 256 characters.
void MixString(Hash& hash, void* string)
{
    if (!string) {
        hash.Mix(0xFFFFFFFF);
        return;
    }
    if (At<std::uintptr_t>(string, 0x0) == addr::SC4String_Vtable) {
        const char* const begin = At<char*>(string, 0x4);
        const char* const end = At<char*>(string, 0x8);
        hash.MixBytes(begin, begin && end >= begin ? static_cast<std::size_t>(end - begin) : 0);
        return;
    }
    const char* const text = Virtual<const char*>(string, 0x1C);
    std::size_t size = 0;
    while (text && size < 256 && text[size])
        ++size;
    hash.MixBytes(text, size);
}

// One building's state from its fields, without addresses: the building
// type, the box, the orientation, and of the occupant base the type, the
// occupant manager's cells, the removal counts, the initialized and
// read-from-a-save bits, the flags as they are saved, the IDs of the
// holder's own properties, whether there is an exemplar, and the name a
// player gave (the property 0x899AFBAD, through `name`).
//
// Left out because they follow frames or a timer, not game days, and so
// differ between two runs of the original: the lit bit (+0x55; the night
// lights' step is timed, docs/lot.md), the completion (+0x58: the saves
// hold values of elapsed milliseconds over 1,000 to 10,000 for buildings
// under construction), the highlight (+0x32's high bits; the interface),
// the flags 0x02 and 0x20 (not saved either) and the placeable object
// (+0x34; made when the building is first drawn).
uint32_t BuildingHash(char* self, GameString& name)
{
    Hash hash;
    hash.Mix(At<uint32_t>(self, field::kBuildingType));
    for (std::size_t offset = field::kMin; offset < field::kOrientation; offset += 4)
        hash.Mix(At<uint32_t>(self, offset));
    hash.Mix(At<uint8_t>(self, field::kOrientation));
    hash.Mix(At<uint32_t>(self, field::kType));
    hash.Mix(At<uint32_t>(self, field::kManagerBox));
    hash.Mix(At<uint16_t>(self, field::kRemoval));
    hash.Mix(At<uint8_t>(self, field::kState) & 3u);
    hash.Mix(At<uint8_t>(self, field::kFlags) & 0xDDu);
    hash.Mix(At<void*>(self, field::kExemplar) != nullptr);
    const char* const begin = At<char*>(self, field::kProperties);
    const char* const end = At<char*>(self, field::kProperties + 4);
    const uint32_t count = begin && end >= begin ? static_cast<uint32_t>((end - begin) / 8) : 0;
    hash.Mix(count);
    bool named = false;
    for (uint32_t i = 0; i < count && i < 256; ++i) {
        const uint32_t id = *reinterpret_cast<const uint32_t*>(begin + 8 * i);
        hash.Mix(id);
        named = named || id == kPropertyName;
    }
    if (named) {
        using GetStringFn = uint8_t(__cdecl*)(void* holder, uint32_t property, void* string);
        hash.Mix(reinterpret_cast<GetStringFn>(addr::SC4_GetPropertyString)(self + field::kHolder, kPropertyName, &name));
        MixString(hash, &name);
    }
    return hash.h;
}

// The pure query slots of a building (OPENSC4_TEST_BLDGOCC=1): of the
// primary interface AsOccupant, GetBuildingType, GetBuildingAge,
// GetCompletionPercent (where it points, not the value), GetOrientation,
// IsLit (that it answers 0 or 1), GetBuildingProfile (purpose, wealth,
// capacity, category flags, size and costs), GetBuildingName,
// GetExemplarName and GetName; QueryInterface for the building interface
// and for 1 through the occupant part; of the occupant part IsInitialized,
// GetType, GetPosition, GetBoundingBox and GetHighlight; GetGZCLSID of the
// serializable part. Never a setter, the save functions or a message.
uint32_t Queries(char* self, GameString& name)
{
    Hash hash;
    char* const part = self + field::kOccupant;
    hash.Mix(static_cast<uint32_t>(Virtual<char*>(self, slot::kAsOccupant) - self));
    hash.Mix(Virtual<uint32_t>(self, slot::kGetBuildingType));
    hash.Mix(Virtual<uint32_t>(self, slot::kGetBuildingAge));
    hash.Mix(static_cast<uint32_t>(Virtual<char*>(self, slot::kGetCompletionPercent) - self));
    hash.Mix(static_cast<uint32_t>(Virtual<int32_t>(self, slot::kGetOrientation)));
    hash.Mix(Virtual<uint32_t>(self, slot::kIsLit) <= 1);
    if (char* const profile = Virtual<char*>(self, slot::kGetBuildingProfile)) {
        hash.Mix(At<uint32_t>(profile, 0x0));
        hash.Mix(At<uint32_t>(profile, 0x4));
        hash.Mix(At<uint16_t>(profile, 0x8));
        hash.Mix(At<uint32_t>(profile, 0x1C));
        for (std::size_t offset = 0x20; offset < 0x2C; offset += 4)
            hash.Mix(At<uint32_t>(profile, offset));
        for (std::size_t offset = 0x30; offset < 0x40; offset += 4)
            hash.Mix(At<uint32_t>(profile, offset));
    } else {
        hash.Mix(0xFFFFFFFF);
    }
    MixString(hash, Virtual<void*>(self, slot::kGetBuildingName));
    MixString(hash, Virtual<void*>(self, slot::kGetExemplarName));
    hash.Mix(Virtual<uint8_t>(self, slot::kGetName, static_cast<void*>(&name)));
    MixString(hash, &name);
    for (const uint32_t iid : {bldgocc_sim::kIidBuilding, 1u}) {
        void* found = nullptr;
        const bool answered = Virtual<uint8_t>(part, 0x0, iid, &found) != 0;
        hash.Mix(answered);
        if (answered && found) {
            hash.Mix(static_cast<uint32_t>(static_cast<char*>(found) - self));
            Virtual<uint32_t>(found, 0x8);
        }
    }
    hash.Mix(Virtual<uint32_t>(part, occupant::kIsInitialized));
    hash.Mix(Virtual<uint32_t>(part, occupant::kGetType));
    uint32_t position[3] = {}, low[3] = {}, high[3] = {};
    Virtual<void*>(part, occupant::kGetPosition, static_cast<uint32_t*>(position));
    Virtual<void*>(part, occupant::kGetBoundingBox, static_cast<uint32_t*>(low), static_cast<uint32_t*>(high));
    for (int i = 0; i < 3; ++i) {
        hash.Mix(position[i]);
        hash.Mix(low[i]);
        hash.Mix(high[i]);
    }
    hash.Mix(Virtual<uint32_t>(part, occupant::kGetHighlight));
    hash.Mix(Virtual<uint32_t>(self + field::kSerializable, 0x14));
    return hash.h;
}

// The city's buildings (build 641; src/core/bldgocc_sim.h), found through
// the occupant manager (the city's slot 34): every occupant of type
// 0x278128A0 in its grid, one in several cells counted once. The count and
// two order-independent checksums of the buildings' state (the sum and the
// exclusive or of BuildingHash above; no addresses).
//
// OPENSC4_TEST_BLDGOCC=1 adds the sum of the pure query slots' answers
// (Queries above) over every 5th building (those whose state hash is a
// multiple of 5, so the choice does not follow the order of the walk).
// OPENSC4_TEST_BLDGOCC=2 adds instead the fields that follow frames: the
// number of lit buildings, of buildings under construction, and the sum of
// a hash of each building's state with its lit bit and completion. Two
// runs of the original need not agree on those.
void Fingerprint(cISC4City* city, std::string& line)
{
    if (!game::IsBuild641()) {
        line += " | bldgocc none";
        return;
    }
    char* const manager = Virtual<char*>(city, 0x88);
    if (!manager || !At<uint8_t>(manager, occmgr_sim::field::kInitialized)) {
        line += " | bldgocc none";
        return;
    }
    const int test = EnvInt("OPENSC4_TEST_BLDGOCC", 0);
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
                if (Virtual<uint32_t>(object, occupant::kGetType) == bldgocc_sim::kOccupantType)
                    seen.insert(static_cast<char*>(object) - field::kOccupant);
            }
        }
    }
    Virtual<void>(manager + occmgr_sim::field::kLock, 0x8);
    GameString name{};
    NewString(name);
    uint32_t sum = 0, mixed = 0, asked = 0, lit = 0, building = 0, timed = 0;
    for (char* const self : seen) {
        const uint32_t hash = BuildingHash(self, name);
        sum += hash;
        mixed ^= hash;
        if (test == 1 && hash % 5 == 0)
            asked += Queries(self, name);
        if (test == 2) {
            const uint32_t completion = At<uint32_t>(self, field::kCompletion);
            lit += At<uint8_t>(self, field::kLit) & 1u;
            building += completion != 0x3F800000u;
            Hash both;
            both.Mix(hash);
            both.Mix(At<uint8_t>(self, field::kLit));
            both.Mix(completion);
            timed += both.h;
        }
    }
    FreeString(name);
    char text[120];
    std::snprintf(text, sizeof text, " | bldgocc %u,%08X,%08X", static_cast<unsigned>(seen.size()), sum, mixed);
    line += text;
    if (test == 1) {
        std::snprintf(text, sizeof text, ",q%08X", asked);
        line += text;
    } else if (test == 2) {
        std::snprintf(text, sizeof text, ",f%u/%u/%08X", lit, building, timed);
        line += text;
    }
}

const FingerprintPart kFingerprint{"bldgocc", 105, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
