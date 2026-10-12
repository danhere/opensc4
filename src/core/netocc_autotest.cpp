// The network occupants' part of the autotest (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/netocc_sim.h"
#include "core/occmgr_sim.h"

#include <cISC4City.h>

#include <opensc4/addresses.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>

namespace opensc4::autotest {
namespace {

namespace field = netocc_sim::field;
namespace flag = netocc_sim::flag;
namespace property = netocc_sim::property;
namespace slot = netocc_sim::slot;
namespace holder = netocc_sim::holder;

struct Hash {
    uint32_t h = 2166136261u;
    void Mix(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    }
    void MixFloat(float v) { Mix(BytesChecksum(&v, 4)); }
};

// How many network occupants answered something that says the test reaches
// the paths of the functions (printed after the checksums).
struct Counters {
    uint32_t intersections = 0, lit = 0, immovable = 0, unusable = 0, oneWay = 0, ceiling = 0, retrofit = 0;
    uint32_t origin = 0, nodeEdges = 0, pylon = 0, placed = 0, pathOverride = 0, variation = 0, edges = 0;
};

// The network types the edge store is asked about (0 to 12).
constexpr uint32_t kTypes = 13;

// What the save record holds and the interface's queries do not reach
// (src/core/netocc_life.cpp, tools/re/save_schemas/netocc.py), from the
// object's fields: the transform's two bytes and its thirteen words, each
// corner's x, y, z and its two last words (the corner heights are the y
// words, hashed again here with the rest), the words at +0xFC and +0x100,
// the edge store's raw entries in their order, the path records (count and
// every word). `inited` adds the
// six words at +0x124 that the interface's Init sets (a made object has
// the pool's leftovers there). Not mixed: the four bytes of each corner and
// of each path record's corners (the lighting follows the view), the
// transform's bytes at +0x66 and +0x67 (the constructor leaves them), pointers,
// and whether the object at +0x140 is there yet: it is fetched when the piece
// is first drawn, and in eight runs of the original on Big City it was there
// on the first day for a different number of pieces in each second run.
void MixSaved(Hash& hash, char* self, bool inited)
{
    constexpr std::size_t kCornerWords[] = {0x0, 0x4, 0x8, 0x10, 0x14};
    constexpr std::size_t kPathWords[] = {0x4, 0x8, 0xC, 0x14, 0x18};
    hash.Mix(At<uint8_t>(self, field::kTransform));
    hash.Mix(At<uint8_t>(self, field::kTransform + 1));
    for (std::size_t offset = field::kModelBase; offset <= field::kTransform + 0x34; offset += 4)
        hash.Mix(At<uint32_t>(self, offset));
    for (std::size_t corner = 0; corner < 4; ++corner) {
        char* const record = self + field::kCorners + corner * field::kVertexStride;
        for (const std::size_t offset : kCornerWords)
            hash.Mix(At<uint32_t>(record, offset));
    }
    hash.Mix(At<uint32_t>(self, field::kUnknownFC));
    hash.Mix(At<uint32_t>(self, field::kUnderTexture));
    const uint32_t further = At<uint8_t>(self, field::kEdgeCount);
    hash.Mix(further);
    hash.Mix(At<uint8_t>(self, field::kEdgeStore + 4));
    hash.Mix(At<uint32_t>(self, field::kEdgeFirstValue));
    if (char* const entries = At<char*>(self, field::kEdgeEntries)) {
        for (uint32_t i = 0; i < further; ++i) {
            hash.Mix(At<uint8_t>(entries, 8 * i));
            hash.Mix(At<uint32_t>(entries, 8 * i + 4));
        }
    }
    const std::uintptr_t begin = At<std::uintptr_t>(self, field::kPaths);
    const std::uintptr_t end = At<std::uintptr_t>(self, field::kPaths + 4);
    const uint32_t records = end >= begin ? static_cast<uint32_t>((end - begin) / 0x4C) : 0;
    hash.Mix(records);
    for (uint32_t i = 0; i < records; ++i) {
        char* const item = reinterpret_cast<char*>(begin) + 0x4C * i;
        hash.Mix(At<uint32_t>(item, 0));
        for (std::size_t sub = 0; sub < 0x48; sub += field::kVertexStride)
            for (const std::size_t offset : kPathWords)
                hash.Mix(At<uint32_t>(item, sub + offset));
    }
    if (inited)
        for (std::size_t i = 0; i < 6; ++i)
            hash.Mix(At<uint32_t>(self, field::kPairs + 4 * i));
}

// One network occupant's state as the interface's read-only queries answer
// it (so through the vtable: the game's functions or ours): every flag bit
// but the lit bit (the lighting follows the view), HasNetworkFlag and
// HasAnyNetworkFlag with the same masks, the retrofit property, the
// connection node's edge types, the path ID overrides, immovable, usable,
// placed, IsOfType for every type, the piece ID, rotation, flip, both in
// one byte, variation, the four corner heights (both ways), the edge store's
// values for every type and the active and grouped edges for it, the
// intersection answer, the demolition cost, the passage ceiling, the
// one-way directions, the complex piece origin and the pylon. Out values
// are mixed only when the query says it wrote them. Not mixed: the lit bit
// (only counted), pointers and the edge store's own pointer. Not called:
// functions that are not of this piece (GetEdgeStore, GetPathInfo ...).
uint32_t ObjectHash(char* self, Counters& counters)
{
    Hash hash;
    for (uint32_t bit = 0; bit < 32; ++bit)
        if ((1u << bit) != flag::kLit)
            hash.Mix(Virtual<uint8_t>(self, slot::kHasNetworkFlag, 1u << bit));
    for (const uint32_t mask : {0x10400000u, 0x40000040u, 0xDFFFFFFFu, 0x00006000u}) {
        hash.Mix(Virtual<uint8_t>(self, slot::kHasNetworkFlag, mask));
        hash.Mix(Virtual<uint8_t>(self, slot::kHasAnyNetworkFlag, mask));
    }
    const bool retrofit = Virtual<uint8_t>(self, slot::kCheckForRetrofit) != 0;
    hash.Mix(retrofit);
    counters.retrofit += retrofit;
    uint32_t nodeFirst = 0, nodeSecond = 0;
    const bool nodeEdges = Virtual<uint8_t>(self, slot::kGetConnectionNodeEdgeTypes, &nodeFirst, &nodeSecond) != 0;
    hash.Mix(nodeEdges);
    if (nodeEdges) {
        hash.Mix(nodeFirst);
        hash.Mix(nodeSecond);
    }
    counters.nodeEdges += nodeEdges;
    const uint32_t connectionPath = Virtual<uint32_t>(self, slot::kGetConnectionPathIDOverride);
    hash.Mix(connectionPath);
    const uint32_t pathOverride = Virtual<uint32_t>(self, slot::kGetPathIDOverride);
    hash.Mix(pathOverride);
    counters.pathOverride += pathOverride != 0 || connectionPath != 0;
    const bool immovable = Virtual<uint8_t>(self, slot::kIsImmovable) != 0;
    const bool usable = Virtual<uint8_t>(self, slot::kIsUsable) != 0;
    const bool placed = Virtual<uint8_t>(self, slot::kIsPlaced) != 0;
    hash.Mix(immovable);
    hash.Mix(usable);
    hash.Mix(placed);
    counters.immovable += immovable;
    counters.unusable += !usable;
    counters.placed += placed;
    for (uint32_t type = 0; type < kTypes; ++type)
        hash.Mix(Virtual<uint8_t>(self, slot::kIsOfType, type));
    hash.Mix(Virtual<uint32_t>(self, slot::kPieceId));
    hash.Mix(Virtual<uint8_t>(self, slot::kGetRotation));
    hash.Mix(Virtual<uint8_t>(self, slot::kGetFlip));
    hash.Mix(Virtual<uint8_t>(self, slot::kGetRotationAndFlip));
    const uint8_t variation = Virtual<uint8_t>(self, slot::kGetVariation);
    hash.Mix(variation);
    counters.variation += variation != 0;
    uint32_t heights[4] = {};
    Virtual<void>(self, slot::kGetVertexHeights, static_cast<uint32_t*>(heights));
    for (uint32_t i = 0; i < 4; ++i) {
        hash.Mix(heights[i]);
        hash.MixFloat(static_cast<float>(Virtual<double>(self, slot::kGetVertexHeight, i)));
    }
    for (uint32_t type = 0; type < kTypes; ++type) {
        hash.Mix(Virtual<uint32_t>(self, slot::kGetEdgeTypesFlag, type));
        hash.Mix(Virtual<uint32_t>(self, slot::kGetActiveEdgesForType, type));
        hash.Mix(Virtual<uint32_t>(self, slot::kGetActiveEdgesForTypeNoGroup, type));
    }
    const uint32_t active = Virtual<uint32_t>(self, slot::kGetActiveEdges);
    hash.Mix(active);
    hash.Mix(Virtual<uint32_t>(self, slot::kGetGroupedEdges));
    counters.edges += active;
    const bool intersection = Virtual<uint8_t>(self, slot::kIsIntersection) != 0;
    hash.Mix(intersection);
    counters.intersections += intersection;
    counters.lit += Virtual<uint8_t>(self, slot::kIsLit) != 0;
    const int64_t cost = Virtual<int64_t>(self, slot::kGetCostOfDemolition);
    hash.Mix(static_cast<uint32_t>(cost));
    hash.Mix(static_cast<uint32_t>(static_cast<uint64_t>(cost) >> 32));
    uint32_t ceiling = 0;
    const bool hasCeiling = Virtual<uint8_t>(self, slot::kGetPassageCeiling, &ceiling) != 0;
    hash.Mix(hasCeiling);
    if (hasCeiling)
        hash.Mix(ceiling);
    counters.ceiling += hasCeiling;
    const uint8_t oneWay = Virtual<uint8_t>(self, slot::kGetOneWayDirections);
    hash.Mix(oneWay);
    counters.oneWay += oneWay != 0;
    int32_t originX = 0, originY = 0;
    const bool hasOrigin = Virtual<uint8_t>(self, slot::kGetComplexPieceOrigin, &originX, &originY) != 0;
    hash.Mix(hasOrigin);
    if (hasOrigin) {
        hash.Mix(static_cast<uint32_t>(originX));
        hash.Mix(static_cast<uint32_t>(originY));
    }
    counters.origin += hasOrigin;
    const bool pylon = Virtual<uint8_t>(self, slot::kIsSupportPylonPresent) != 0;
    hash.Mix(pylon);
    counters.pylon += pylon;
    MixSaved(hash, self, true);
    return hash.h;
}

// Every network occupant of the city (build 641; src/core/netocc_sim.h),
// found through the occupant manager (the city's slot 34 / +0x88) as the
// foundations are (src/core/foundocc_autotest.cpp): every occupant of type
// 0xC772BF98 in its grid, one in several cells once. The objects are the
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
                if (Virtual<uint32_t>(object, 0x1C) == netocc_sim::kOccupantType)
                    seen.insert(static_cast<char*>(object) - field::kOccupant);
            }
        }
    }
    Virtual<void>(manager + occmgr_sim::field::kLock, 0x8);
    found.assign(seen.begin(), seen.end());
    return true;
}

// The setters' exercise (OPENSC4_TEST_NETOCC=<day>): the checksum, summed
// over the objects driven, of what each getter answers after its setter,
// and how many objects were driven.
uint32_t g_setSum = 0;
uint32_t g_setCount = 0;
bool g_setDone = false;

bool HasProperty(char* self, uint32_t id)
{
    return Virtual<void*>(self + field::kHolder, holder::kGetProperty, id) != nullptr;
}

void RemoveProperty(char* self, uint32_t id)
{
    Virtual<uint32_t>(self + field::kHolder, holder::kRemoveProperty, id, 0u);
}

// Every setter of the piece with a changed value on one object, the
// answers read back and mixed, the value restored with the same setter or,
// for a property that was not there, with the holder's RemoveProperty. What
// the setters do besides storing is kept: SetNetworkFlag/ClearNetworkFlag of
// the usable bit post a message (queued) for a placed piece and, for 0x4000,
// call SetTranslucency (twice, the original translucency is not read back);
// SetVariation and SetUnderTexture post a changed message (queued);
// SetConnectionPathIDOverride calls ReleasePathInfo (the cached path
// information is dropped and made again) and clears flag 0x2000, which is
// set again when it was; SetEdgeType and its restore keep the store's
// entry. Left alone: a SetRetrofitFlag(false) of a present retrofit property
// (its value would be lost) and a present path ID override whose value is 0
// (a zero removes the property).
uint32_t Exercise(char* self)
{
    Hash hash;

    // Retrofit: set, read, clear (the property was not there).
    if (!Virtual<uint8_t>(self, slot::kCheckForRetrofit)) {
        Virtual<void>(self, slot::kSetRetrofitFlag, 1u);
        hash.Mix(Virtual<uint8_t>(self, slot::kCheckForRetrofit));
        Virtual<void>(self, slot::kSetRetrofitFlag, 0u);
        hash.Mix(Virtual<uint8_t>(self, slot::kCheckForRetrofit));
    }

    // Flags: a plain bit, the usable bit, the translucent bit, immovable.
    uint32_t plain = 1;
    while (plain < 0x1000 && Virtual<uint8_t>(self, slot::kHasNetworkFlag, plain))
        plain <<= 1;
    if (plain < 0x1000) {
        Virtual<void>(self, slot::kSetNetworkFlag, plain);
        hash.Mix(Virtual<uint8_t>(self, slot::kHasNetworkFlag, plain));
        Virtual<void>(self, slot::kClearNetworkFlag, plain);
        hash.Mix(Virtual<uint8_t>(self, slot::kHasNetworkFlag, plain));
    }
    const bool usable = Virtual<uint8_t>(self, slot::kIsUsable) != 0;
    Virtual<uint32_t>(self, slot::kSetUsable, usable ? 0u : 1u);
    hash.Mix(Virtual<uint8_t>(self, slot::kIsUsable));
    Virtual<uint32_t>(self, slot::kSetUsable, usable ? 1u : 0u);
    hash.Mix(Virtual<uint8_t>(self, slot::kIsUsable));
    const bool immovable = Virtual<uint8_t>(self, slot::kIsImmovable) != 0;
    Virtual<uint32_t>(self, slot::kSetImmovable, immovable ? 0u : 1u);
    hash.Mix(Virtual<uint8_t>(self, slot::kIsImmovable));
    Virtual<uint32_t>(self, slot::kSetImmovable, immovable ? 1u : 0u);
    hash.Mix(Virtual<uint8_t>(self, slot::kIsImmovable));
    const bool translucent = Virtual<uint8_t>(self, slot::kHasNetworkFlag, flag::kTranslucent) != 0;
    if (translucent) {
        Virtual<void>(self, slot::kClearNetworkFlag, flag::kTranslucent);
        hash.Mix(Virtual<uint8_t>(self, slot::kHasNetworkFlag, flag::kTranslucent));
        Virtual<void>(self, slot::kSetNetworkFlag, flag::kTranslucent);
    } else {
        Virtual<void>(self, slot::kSetNetworkFlag, flag::kTranslucent);
        hash.Mix(Virtual<uint8_t>(self, slot::kHasNetworkFlag, flag::kTranslucent));
        Virtual<void>(self, slot::kClearNetworkFlag, flag::kTranslucent);
    }
    hash.Mix(Virtual<uint8_t>(self, slot::kHasNetworkFlag, flag::kTranslucent));

    // Variation and under texture.
    const uint8_t variation = Virtual<uint8_t>(self, slot::kGetVariation);
    Virtual<void>(self, slot::kSetVariation, uint32_t{static_cast<uint8_t>(variation ^ 1)});
    hash.Mix(Virtual<uint8_t>(self, slot::kGetVariation));
    Virtual<void>(self, slot::kSetVariation, uint32_t{variation});
    hash.Mix(Virtual<uint8_t>(self, slot::kGetVariation));
    const uint32_t texture = At<uint32_t>(self, field::kUnderTexture);
    Virtual<void>(self, slot::kSetUnderTexture, texture ^ 0x5A5A5A5Au);
    hash.Mix(At<uint32_t>(self, field::kUnderTexture));
    Virtual<void>(self, slot::kSetUnderTexture, texture);
    hash.Mix(At<uint32_t>(self, field::kUnderTexture));

    // The corner heights: the first one raised by one.
    uint32_t heights[4] = {};
    Virtual<void>(self, slot::kGetVertexHeights, static_cast<uint32_t*>(heights));
    uint32_t changed[4] = {heights[0], heights[1], heights[2], heights[3]};
    float first = 0;
    std::memcpy(&first, &heights[0], 4);
    first += 1.0f;
    std::memcpy(&changed[0], &first, 4);
    Virtual<void>(self, slot::kSetVertexHeights, static_cast<uint32_t*>(changed));
    hash.MixFloat(static_cast<float>(Virtual<double>(self, slot::kGetVertexHeight, 0u)));
    uint32_t after[4] = {};
    Virtual<void>(self, slot::kGetVertexHeights, static_cast<uint32_t*>(after));
    for (const uint32_t value : after)
        hash.Mix(value);
    Virtual<void>(self, slot::kSetVertexHeights, static_cast<uint32_t*>(heights));
    hash.MixFloat(static_cast<float>(Virtual<double>(self, slot::kGetVertexHeight, 0u)));

    // The edge store: edge 1 of the first entry's type set to 5, the whole
    // value restored (the first entry's key is the one the store keeps in
    // place).
    const uint32_t type = At<uint8_t>(self, field::kEdgeStore + 4);
    const uint32_t edges = Virtual<uint32_t>(self, slot::kGetEdgeTypesFlag, type);
    Virtual<uint32_t>(self, slot::kSetEdgeType, type, 1u, 5u);
    hash.Mix(Virtual<uint32_t>(self, slot::kGetEdgeTypesFlag, type));
    hash.Mix(Virtual<uint32_t>(self, slot::kGetActiveEdgesForType, type));
    hash.Mix(Virtual<uint32_t>(self, slot::kGetActiveEdgesForTypeNoGroup, type));
    hash.Mix(Virtual<uint32_t>(self, slot::kGetActiveEdges));
    hash.Mix(Virtual<uint32_t>(self, slot::kGetGroupedEdges));
    Virtual<uint32_t>(self, slot::kSetEdgeTypesFlag, type, edges);
    hash.Mix(Virtual<uint32_t>(self, slot::kGetEdgeTypesFlag, type));

    // The path ID: the plain one, then the connection override (which also
    // clears flag 0x2000 and drops the path information).
    const uint32_t pathId = Virtual<uint32_t>(self, slot::kGetPathIDOverride);
    Virtual<void>(self, slot::kSetPathIDOverride, pathId ^ 0x5A5Au);
    hash.Mix(Virtual<uint32_t>(self, slot::kGetPathIDOverride));
    Virtual<void>(self, slot::kSetPathIDOverride, pathId);
    hash.Mix(Virtual<uint32_t>(self, slot::kGetPathIDOverride));
    const bool pathPresent = HasProperty(self, property::kPathIDOverride);
    const uint32_t connection = Virtual<uint32_t>(self, slot::kGetConnectionPathIDOverride);
    if (!pathPresent || connection != 0) {
        const bool pathFlag = Virtual<uint8_t>(self, slot::kHasNetworkFlag, flag::kPathInfo) != 0;
        const uint32_t value = (connection ^ 0x1234u) != 0 ? (connection ^ 0x1234u) : 0x4321u;
        Virtual<void>(self, slot::kSetConnectionPathIDOverride, value);
        hash.Mix(Virtual<uint32_t>(self, slot::kGetConnectionPathIDOverride));
        hash.Mix(Virtual<uint32_t>(self, slot::kGetPathIDOverride));
        hash.Mix(Virtual<uint8_t>(self, slot::kHasNetworkFlag, flag::kPathInfo));
        Virtual<void>(self, slot::kSetConnectionPathIDOverride, connection);
        hash.Mix(Virtual<uint32_t>(self, slot::kGetConnectionPathIDOverride));
        Virtual<void>(self, slot::kSetPathIDOverride, pathId);
        if (pathFlag)
            Virtual<void>(self, slot::kSetNetworkFlag, flag::kPathInfo);
        hash.Mix(Virtual<uint8_t>(self, slot::kHasNetworkFlag, flag::kPathInfo));
    }

    // The demolition cost.
    const int64_t cost = Virtual<int64_t>(self, slot::kGetCostOfDemolition);
    const uint32_t costLow = static_cast<uint32_t>(cost);
    const uint32_t costHigh = static_cast<uint32_t>(static_cast<uint64_t>(cost) >> 32);
    Virtual<void>(self, slot::kSetCostOfDemolition, costLow ^ 1u, costHigh ^ 2u);
    const int64_t costAfter = Virtual<int64_t>(self, slot::kGetCostOfDemolition);
    hash.Mix(static_cast<uint32_t>(costAfter));
    hash.Mix(static_cast<uint32_t>(static_cast<uint64_t>(costAfter) >> 32));
    Virtual<void>(self, slot::kSetCostOfDemolition, costLow, costHigh);
    hash.Mix(static_cast<uint32_t>(Virtual<int64_t>(self, slot::kGetCostOfDemolition)));

    // The connection node's edge types.
    {
        const bool present = HasProperty(self, property::kNodeEdgeTypes);
        uint32_t a = 0, b = 0;
        Virtual<uint8_t>(self, slot::kGetConnectionNodeEdgeTypes, &a, &b);
        Virtual<void>(self, slot::kSetConnectionNodeEdgeTypes, a ^ 1u, b ^ 2u);
        uint32_t c = 0, d = 0;
        hash.Mix(Virtual<uint8_t>(self, slot::kGetConnectionNodeEdgeTypes, &c, &d));
        hash.Mix(c);
        hash.Mix(d);
        if (present)
            Virtual<void>(self, slot::kSetConnectionNodeEdgeTypes, a, b);
        else
            RemoveProperty(self, property::kNodeEdgeTypes);
        hash.Mix(HasProperty(self, property::kNodeEdgeTypes));
    }

    // The passage ceiling.
    {
        const bool present = HasProperty(self, property::kPassageCeiling);
        uint32_t old = 0;
        Virtual<uint8_t>(self, slot::kGetPassageCeiling, &old);
        float value = 0;
        std::memcpy(&value, &old, 4);
        value += 1.0f;
        uint32_t bits = 0;
        std::memcpy(&bits, &value, 4);
        Virtual<void>(self, slot::kSetPassageCeiling, bits);
        uint32_t now = 0;
        hash.Mix(Virtual<uint8_t>(self, slot::kGetPassageCeiling, &now));
        hash.Mix(now);
        if (present)
            Virtual<void>(self, slot::kSetPassageCeiling, old);
        else
            RemoveProperty(self, property::kPassageCeiling);
        hash.Mix(HasProperty(self, property::kPassageCeiling));
    }

    // The one-way directions (a zero is asked first when the property is
    // not there: it does nothing).
    {
        const bool present = HasProperty(self, property::kOneWayDirections);
        const uint8_t old = Virtual<uint8_t>(self, slot::kGetOneWayDirections);
        if (!present) {
            Virtual<void>(self, slot::kSetOneWayDirections, 0u);
            hash.Mix(HasProperty(self, property::kOneWayDirections));
        }
        Virtual<void>(self, slot::kSetOneWayDirections, uint32_t{static_cast<uint8_t>(old ^ 5)});
        hash.Mix(Virtual<uint8_t>(self, slot::kGetOneWayDirections));
        if (present)
            Virtual<void>(self, slot::kSetOneWayDirections, uint32_t{old});
        else
            RemoveProperty(self, property::kOneWayDirections);
        hash.Mix(HasProperty(self, property::kOneWayDirections));
    }

    // The complex piece's origin.
    {
        const bool present = HasProperty(self, property::kComplexPieceOrigin);
        int32_t x = 0, y = 0;
        Virtual<uint8_t>(self, slot::kGetComplexPieceOrigin, &x, &y);
        Virtual<void>(self, slot::kSetComplexPieceOrigin, static_cast<uint32_t>(x + 1), static_cast<uint32_t>(y + 2));
        int32_t nx = 0, ny = 0;
        hash.Mix(Virtual<uint8_t>(self, slot::kGetComplexPieceOrigin, &nx, &ny));
        hash.Mix(static_cast<uint32_t>(nx));
        hash.Mix(static_cast<uint32_t>(ny));
        if (present)
            Virtual<void>(self, slot::kSetComplexPieceOrigin, static_cast<uint32_t>(x), static_cast<uint32_t>(y));
        else
            RemoveProperty(self, property::kComplexPieceOrigin);
        hash.Mix(HasProperty(self, property::kComplexPieceOrigin));
    }
    return hash.h;
}

// One QueryInterface through `part` for `iid`: whether it answered and where
// in the object the answer lies (never the address); the reference it gave
// is released.
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

// The class's life on one made object (OPENSC4_TEST_NETOCC=<day>): the game's
// factory (0x0061C480: the pool, the constructor), one reference held by the
// test, QueryInterface through the parts that answer to it (the interface,
// the occupant part, the serializable part, the holder, the exemplar part and
// the virtual base, whose slot 0 is a thunk to it) for the IDs the class
// answers (its interface, 0xA9764C8C, 1, the occupant, the serializable and
// 0x26A68449 of the model part) and some it does not (its class ID, one
// nobody has); GetGZCLSID of the serializable part; the fields the
// constructor sets (the save's, but for the six words the interface's Init
// sets); and the occupant part's Init on an object marked initialised (the
// second branch: the usable bit and the bit 0x800000, set in the flags,
// cleared and set again through SetNetworkFlag). The last Release destroys
// the object through the deleting destructor and returns the item to its
// pool page. Not called: the interface's Init (0x00619050) and the first
// branch of the occupant part's, which end in the occupant message 0x99EF1140
// (queued, with the made object as its object): listeners of that message
// would meet an occupant that is in no cell and no network.
uint32_t Life()
{
    Hash hash;
    char* const self = reinterpret_cast<char*(__cdecl*)()>(addr::cSC4NetworkOccupant_Create)();
    if (!self)
        return 0;
    Virtual<uint32_t>(self, 0x4);
    hash.Mix(Virtual<uint32_t>(self + field::kSerializable, 0x14));
    const uint32_t ids[] = {netocc_sim::kIidNetworkOccupant,
                            0xA9764C8Cu,
                            1u,
                            0x26BB767Eu,
                            0xE4FDA3D4u,
                            0x26A68449u,
                            netocc_sim::kClassId,
                            0x0F10A0CCu};
    for (const std::size_t offset : {std::size_t{0}, field::kOccupant, field::kSerializable, field::kHolder,
                                     field::kExemplarPart, field::kVbase})
        for (const uint32_t iid : ids)
            AskInterface(hash, self, self + offset, iid);
    MixSaved(hash, self, false);
    hash.Mix(At<uint32_t>(self, field::kFlags));
    At<uint8_t>(self, field::kOccupant + 0x2E) |= 1;
    At<uint32_t>(self, field::kFlags) |= 0x10800000u;
    hash.Mix(Virtual<uint8_t>(self + field::kOccupant, 0xC));
    hash.Mix(At<uint32_t>(self, field::kFlags));
    At<uint8_t>(self, field::kOccupant + 0x2E) &= 0xFE;
    Virtual<uint32_t>(self, 0x8);
    return hash.h;
}

// OPENSC4_TEST_NETOCC=<day>: on that day every seventh network occupant
// (those whose state hash is a multiple of 7, so the choice does not follow
// the order of the walk) goes through Exercise, and one made object through
// Life. The read-back checksums are summed into the fingerprint's ` set`
// field from then on.
void Drive(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_NETOCC") || !game::IsBuild641() || g_setDone)
        return;
    g_setDone = true;
    std::vector<char*> objects;
    if (!Collect(day.city, objects))
        return;
    g_setSum += Life();
    for (char* const self : objects) {
        Counters unused;
        if (ObjectHash(self, unused) % 7 != 0)
            continue;
        g_setSum += Exercise(self);
        ++g_setCount;
    }
}

// The fingerprint part: the number of network occupants and the sum and the
// exclusive or of ObjectHash over them (order independent; no addresses),
// then the counters, and after OPENSC4_TEST_NETOCC's day ` set <checksum>,
// <count>`.
void Fingerprint(cISC4City* city, std::string& line)
{
    std::vector<char*> objects;
    if (!Collect(city, objects)) {
        line += " | netocc none";
        return;
    }
    uint32_t sum = 0, mixed = 0;
    Counters counters;
    for (char* const self : objects) {
        const uint32_t hash = ObjectHash(self, counters);
        sum += hash;
        mixed ^= hash;
    }
    char text[256];
    std::snprintf(text, sizeof text,
                  " | netocc %u,%08X,%08X inter %u lit %u imm %u unus %u ow %u ceil %u retro %u node %u orig %u "
                  "pyl %u placed %u pov %u var %u act %u",
                  static_cast<unsigned>(objects.size()), sum, mixed, counters.intersections, counters.lit,
                  counters.immovable, counters.unusable, counters.oneWay, counters.ceiling, counters.retrofit,
                  counters.nodeEdges, counters.origin, counters.pylon, counters.placed, counters.pathOverride,
                  counters.variation, counters.edges);
    line += text;
    if (g_setCount) {
        std::snprintf(text, sizeof text, " set %08X,%u", g_setSum, static_cast<unsigned>(g_setCount));
        line += text;
    }
}

const FingerprintPart kFingerprint{"netocc", 119, &Fingerprint};
const DayHook kDrive{"netocc.drive", 183, &Drive};

} // namespace
} // namespace opensc4::autotest
