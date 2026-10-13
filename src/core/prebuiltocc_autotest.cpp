// The pre-built-model network occupants' part of the autotest
// (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/occmgr_sim.h"
#include "core/prebuiltocc_sim.h"

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

namespace field = prebuiltocc_sim::field;
namespace flag = prebuiltocc_sim::flag;
namespace slot = prebuiltocc_sim::slot;

struct Hash {
    uint32_t h = 2166136261u;
    void Mix(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    }
    void MixFloat(float v)
    {
        uint32_t bits = 0;
        std::memcpy(&bits, &v, 4);
        Mix(bits);
    }
};

// How many objects answered something that says the test reaches the paths
// of the functions (printed after the checksums).
struct Counters {
    uint32_t pylon = 0, otherDiagonal = 0, sloped = 0, offset = 0, pieceOverride = 0, firstPlane = 0, turned = 0;
};

// The points the surface is asked at, from the piece's position: the
// position itself and three on each side of both diagonals.
constexpr float kPoints[][2] = {{0.0f, 0.0f}, {4.0f, -2.0f}, {-4.0f, 2.0f}, {3.0f, 5.0f},
                                {-3.0f, -5.0f}, {6.0f, 6.0f}, {-6.0f, -6.0f}};

// The vectors TransformNode is given.
constexpr float kNodes[][3] = {{1.0f, 10.0f, 2.0f}, {-3.0f, 0.0f, 4.0f}, {0.0f, 0.0f, 0.0f}};

// The surface of one object through its vtable (GetAltitude, GetNormal and
// TransformNode: the game's functions or ours): the height (a double in
// ST0, mixed as the float it is) and the normal's three words at each of
// kPoints from the position (slot 75), and what TransformNode makes of each
// of kNodes. TransformNode only for a rotation of 0 to 3: above that the
// game's function works on two stack slots it never set (the vector's
// address and the object's).
void MixSurface(Hash& hash, char* self, Counters* counters)
{
    float position[3] = {};
    Virtual<void>(self, slot::kGetPosition, static_cast<float*>(position));
    for (const auto& point : kPoints) {
        const float x = position[0] + point[0];
        const float z = position[2] + point[1];
        hash.MixFloat(static_cast<float>(Virtual<double>(self, slot::kGetAltitude, x, z)));
        uint32_t normal[3] = {};
        Virtual<uint32_t*>(self, slot::kGetNormal, static_cast<uint32_t*>(normal), x, z);
        for (const uint32_t word : normal)
            hash.Mix(word);
        if (counters)
            counters->firstPlane += normal[0] == (At<uint32_t>(self, field::kPlaneA) ^ 0x80000000u) &&
                                    normal[2] == (At<uint32_t>(self, field::kPlaneA + 4) ^ 0x80000000u);
    }
    if (Virtual<uint8_t>(self, slot::kGetRotation) > 3)
        return;
    for (const auto& node : kNodes) {
        float vector[3] = {node[0], node[1], node[2]};
        Virtual<void>(self, slot::kTransformNode, static_cast<float*>(vector));
        for (const float value : vector)
            hash.MixFloat(value);
    }
}

// One QueryInterface through a part: the answer, and where in the object the
// answer lies (never the address); the reference it gave is released.
void AskInterface(Hash& hash, char* self, char* part, uint32_t iid)
{
    void* found = nullptr;
    const bool answered = (Virtual<uint32_t>(part, 0x0, iid, &found) & 1) != 0;
    hash.Mix(answered);
    if (answered && found) {
        hash.Mix(static_cast<uint32_t>(static_cast<char*>(found) - self));
        Virtual<uint32_t>(found, 0x8);
    }
}

// The part of a pre-built-model occupant that the network occupant's walk
// (src/core/netocc_autotest.cpp, which takes every occupant of type
// 0xC772BF98, these included) does not reach: its own saved fields (the
// model's offset, the piece override and its byte, the pylon's model and,
// with one, its angle and offset: the constructor leaves the angle as the
// pool had it), the two planes (worked out by the occupant part's Init
// from the corners), the answers of its own overrides (the pylon, the
// surface at fixed points, the class ID of the serializable part) and
// QueryInterface for the class's IID, 0xA9764C8C, the network occupant's
// IID, 1 and one nobody answers, through the interface part and the
// occupant part (each reference given is released). Not mixed: the effect
// IDs, the two seen bytes and the model instances (the view's), pointers
// and reference counts, and the path information (made when the game first
// needs it, which follows the view).
uint32_t ObjectHash(char* self, Counters& counters)
{
    Hash hash;
    char* const part = self + field::kOccupant;
    bool offset = false;
    for (std::size_t i = 0; i < 3; ++i) {
        hash.Mix(At<uint32_t>(self, field::kModelOffset + 4 * i));
        offset = offset || (At<uint32_t>(self, field::kModelOffset + 4 * i) & 0x7FFFFFFFu) != 0;
    }
    counters.offset += offset;
    hash.Mix(At<uint32_t>(self, field::kPieceOverride));
    hash.Mix(At<uint8_t>(self, field::kRotationOverride));
    counters.pieceOverride += At<uint32_t>(self, field::kPieceOverride) != 0;
    const uint32_t pylon = At<uint32_t>(self, field::kPylon);
    hash.Mix(pylon);
    if (pylon != 0)
        for (std::size_t i = 0; i < 4; ++i)
            hash.Mix(At<uint32_t>(self, field::kPylonAngle + 4 * i));
    bool sloped = false;
    for (std::size_t i = 0; i < 6; ++i) {
        hash.Mix(At<uint32_t>(self, field::kPlaneA + 4 * i));
        sloped = sloped || (i % 3 != 2 && (At<uint32_t>(self, field::kPlaneA + 4 * i) & 0x7FFFFFFFu) != 0);
    }
    counters.sloped += sloped;
    const uint32_t present = Virtual<uint32_t>(self, slot::kIsSupportPylonPresent);
    hash.Mix(present);
    counters.pylon += present != 0;
    const bool other = Virtual<uint8_t>(self, slot::kHasNetworkFlag, flag::kOtherDiagonal) != 0;
    hash.Mix(other);
    counters.otherDiagonal += other;
    counters.turned += Virtual<uint8_t>(self, slot::kGetRotation) != 0;
    MixSurface(hash, self, &counters);
    hash.Mix(Virtual<uint32_t>(self + field::kSerializable, 0x14));
    hash.Mix(Virtual<uint32_t>(part, 0x1C));
    const uint32_t ids[] = {prebuiltocc_sim::kIidPrebuilt, prebuiltocc_sim::kIidObject,
                            prebuiltocc_sim::kIidNetworkOccupant, 1u, 0x0F10A0CCu};
    for (char* const asked : {self, part})
        for (const uint32_t iid : ids)
            AskInterface(hash, self, asked, iid);
    return hash.h;
}

// Every pre-built-model occupant of the city (build 641;
// src/core/prebuiltocc_sim.h), found through the occupant manager (the
// city's slot 34 / +0x88): every occupant of the network occupant type
// 0xC772BF98 in its grid, one in several cells once, whose serializable
// part answers the class ID 0x49C1A034 (so no bridge and no tunnel piece,
// which have their own). The objects are the interface part (the occupant
// part less 4). False without a manager.
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
                if (Virtual<uint32_t>(object, 0x1C) != prebuiltocc_sim::kOccupantType)
                    continue;
                char* const self = static_cast<char*>(object) - field::kOccupant;
                if (Virtual<uint32_t>(self + field::kSerializable, 0x14) == prebuiltocc_sim::kClassId)
                    seen.insert(self);
            }
        }
    }
    Virtual<void>(manager + occmgr_sim::field::kLock, 0x8);
    found.assign(seen.begin(), seen.end());
    return true;
}

// The setters' exercise (OPENSC4_TEST_PREBUILTOCC=<day>): the checksum,
// summed over the objects driven, of what is read back, and how many
// objects were driven.
uint32_t g_setSum = 0;
uint32_t g_setCount = 0;
bool g_setDone = false;

using SetSupportPylonFn = uint32_t(__thiscall*)(void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
using UpdatePlanesFn = void(__thiscall*)(void*);

// The pylon setter on one object (0x0061D340, called at its address as the
// maker does): the five words read back and the pylon's query after a
// setting with a model, and the query after one without (the network
// occupant's own answer then), mixed with the setter's EAX (its last
// argument).
void MixPylon(Hash& hash, char* self)
{
    const auto setPylon = reinterpret_cast<SetSupportPylonFn>(addr::cSC4PrebuiltNetworkOccupant_SetSupportPylon);
    hash.Mix(setPylon(self, 0x5AD0E817u, 0x3FC00000u, 0x40200000u, 0xC0400000u, 0x40900000u));
    for (std::size_t i = 0; i < 5; ++i)
        hash.Mix(At<uint32_t>(self, field::kPylon + 4 * i));
    hash.Mix(Virtual<uint32_t>(self, slot::kIsSupportPylonPresent));
    hash.Mix(setPylon(self, 0u, 0x40490FDBu, 0u, 0x3F800000u, 0x80000000u));
    for (std::size_t i = 0; i < 5; ++i)
        hash.Mix(At<uint32_t>(self, field::kPylon + 4 * i));
    hash.Mix(Virtual<uint32_t>(self, slot::kIsSupportPylonPresent));
}

// On one object of the city: the pylon setter with other values, read back,
// then the five words put back with the same setter; the planes worked out
// again (0x0061D230, at its address), the six floats mixed with whether
// they are what the object had, and the old words put back; and the path
// information: GetPathInfo twice (the second answer is the first's), what
// it did (an answer or not, the eight words of the table; never the
// pointer), then the table's words put back, flag 0x2000 cleared when it
// was not set and the information dropped when there was none before, as
// the network occupant's test does
// (src/core/netocc_autotest.cpp, Paths). Whether the information was there
// before is not mixed: that follows the view.
uint32_t Exercise(char* self)
{
    Hash hash;
    uint32_t pylon[5];
    for (std::size_t i = 0; i < 5; ++i)
        pylon[i] = At<uint32_t>(self, field::kPylon + 4 * i);
    MixPylon(hash, self);
    reinterpret_cast<SetSupportPylonFn>(addr::cSC4PrebuiltNetworkOccupant_SetSupportPylon)(
        self, pylon[0], pylon[1], pylon[2], pylon[3], pylon[4]);

    uint32_t planes[6];
    for (std::size_t i = 0; i < 6; ++i)
        planes[i] = At<uint32_t>(self, field::kPlaneA + 4 * i);
    reinterpret_cast<UpdatePlanesFn>(addr::cSC4PrebuiltNetworkOccupant_UpdatePlanes)(self);
    bool same = true;
    for (std::size_t i = 0; i < 6; ++i) {
        hash.Mix(At<uint32_t>(self, field::kPlaneA + 4 * i));
        same = same && At<uint32_t>(self, field::kPlaneA + 4 * i) == planes[i];
        At<uint32_t>(self, field::kPlaneA + 4 * i) = planes[i];
    }
    hash.Mix(same);

    uint32_t table[8];
    for (std::size_t i = 0; i < 8; ++i)
        table[i] = At<uint32_t>(self, field::kPathTable + 4 * i);
    const bool hadInfo = At<void*>(self, field::kReference) != nullptr;
    const bool hadFlag = Virtual<uint8_t>(self, slot::kHasNetworkFlag, flag::kPathInfo) != 0;
    void* const info = Virtual<void*>(self, slot::kGetPathInfo);
    hash.Mix(info != nullptr);
    hash.Mix(Virtual<void*>(self, slot::kGetPathInfo) == info);
    hash.Mix(At<void*>(self, field::kReference) == info);
    for (std::size_t i = 0; i < 8; ++i) {
        hash.Mix(At<uint32_t>(self, field::kPathTable + 4 * i));
        At<uint32_t>(self, field::kPathTable + 4 * i) = table[i];
    }
    if (!hadFlag)
        Virtual<void>(self, slot::kClearNetworkFlag, flag::kPathInfo);
    if (!hadInfo)
        Virtual<void>(self, slot::kReleasePathInfo);
    return hash.h;
}

// The class's life on one made object (OPENSC4_TEST_PREBUILTOCC=<day>): the
// game's factory (0x0061E820: the pool, the constructor; it returns the
// occupant part), one reference held by the test, GetGZCLSID of the
// serializable part, QueryInterface through the parts that answer to it
// (the interface, the occupant part, the serializable part, the holder, the
// exemplar part and the virtual base, whose slot 0 is a thunk to it) for
// the IDs the class answers (its own, 0xA9764C8C, the network occupant's,
// 1, the occupant, the serializable and 0x26A68449) and some it does not
// (the network occupant's and the bridge's class IDs, one nobody has); the
// fields the constructor sets (the flags, the planes, the pylon's model and
// offset, the piece override and its byte, the effect IDs and the two seen
// bytes, which are constants on an object no view has met, whether the two
// model instances are null, +0x168 and +0x16C, the virtual base's vtable
// and shift, the table); the pylon setter and query; GetPathInfo without an
// exemplar (null); then the surface on corners the test stores in the
// object (heights 10, 12, 15 and 11 on a 16 by 16 square): with flag
// 0x01000000 clear and set (the bit changed in the flags word), the planes
// (0x0061D230 at its address), GetAltitude and GetNormal at the points,
// and TransformNode for the rotations 0 to 3, plain and flipped (slot 74),
// without and with a path ID override (the word at +0x164 and the floats at
// +0x168 and +0x16C stored by the test and zeroed after); Shutdown on the
// object, not initialised (AL 1, nothing done); and the occupant part's
// Init on the object marked initialised (1, nothing done). The last
// Release destroys the object through the deleting destructor and returns
// the item to its pool page. Not called: the occupant part's Init of an
// object not initialised, which ends in the occupant message 0x99EF1140
// (queued, with the made object as its object) and in SetLit and the
// effects; Shutdown of an initialised object (the occupant base's posts);
// the set-up 0x0061D120, which needs an exemplar and asks for the model
// instances; Write and Read (no autotest has a stream of the game's to
// give them; the saves of the parity runs go through them when a city has
// such pieces). Not mixed: the model's offset and the pylon's angle, which
// the constructor leaves as the pool's memory holds them.
uint32_t Life()
{
    Hash hash;
    char* const part = reinterpret_cast<char*(__cdecl*)()>(addr::cSC4PrebuiltNetworkOccupant_Create)();
    if (!part)
        return 0;
    char* const self = part - field::kOccupant;
    Virtual<uint32_t>(self, 0x4);
    hash.Mix(Virtual<uint32_t>(self + field::kSerializable, 0x14));
    const uint32_t ids[] = {prebuiltocc_sim::kIidPrebuilt,
                            prebuiltocc_sim::kIidObject,
                            prebuiltocc_sim::kIidNetworkOccupant,
                            1u,
                            0x26BB767Eu,
                            0xE4FDA3D4u,
                            0x26A68449u,
                            0xC9C05C6Eu,
                            0x49CC1BCDu,
                            0x0F10A0CCu};
    for (const std::size_t offset : {std::size_t{0}, field::kOccupant, field::kSerializable, field::kHolder,
                                     field::kExemplarPart, field::kVbase})
        for (const uint32_t iid : ids)
            AskInterface(hash, self, self + offset, iid);
    hash.Mix(At<uint32_t>(self, field::kFlags));
    for (std::size_t i = 0; i < 6; ++i)
        hash.Mix(At<uint32_t>(self, field::kPlaneA + 4 * i));
    hash.Mix(At<uint32_t>(self, field::kPylon));
    for (std::size_t i = 0; i < 3; ++i)
        hash.Mix(At<uint32_t>(self, field::kPylonOffset + 4 * i));
    hash.Mix(At<uint32_t>(self, field::kPieceOverride));
    hash.Mix(At<uint8_t>(self, field::kRotationOverride));
    hash.Mix(At<uint32_t>(self, field::kEffects));
    hash.Mix(At<uint32_t>(self, field::kEffects + 4));
    hash.Mix(At<uint8_t>(self, field::kSeenZoom));
    hash.Mix(At<uint8_t>(self, field::kSeenDetail));
    hash.Mix(At<void*>(self, field::kModel) == nullptr);
    hash.Mix(At<void*>(self, field::kPylonModel) == nullptr);
    hash.Mix(At<uint32_t>(self, field::kPathOriginX));
    hash.Mix(At<uint32_t>(self, field::kPathOriginZ));
    hash.Mix(At<uint32_t>(self, field::kVbaseShift));
    hash.Mix(At<std::uintptr_t>(self, field::kVbase) == addr::cSC4PrebuiltNetworkOccupant_vbase_vftable);
    hash.Mix(At<std::uintptr_t>(self, field::kVbtable) == addr::cSC4PrebuiltNetworkOccupant_vbtable);
    hash.Mix(Virtual<uint32_t>(self, slot::kIsSupportPylonPresent));
    MixPylon(hash, self);
    hash.Mix(Virtual<void*>(self, slot::kGetPathInfo) == nullptr);

    constexpr float kCorners[4][3] = {
        {32.0f, 10.0f, 48.0f}, {32.0f, 12.0f, 64.0f}, {48.0f, 15.0f, 64.0f}, {48.0f, 11.0f, 48.0f}};
    for (std::size_t corner = 0; corner < 4; ++corner)
        for (std::size_t i = 0; i < 3; ++i)
            At<float>(self, field::kCorners + corner * field::kVertexStride + 4 * i) = kCorners[corner][i];
    const uint32_t flags = At<uint32_t>(self, field::kFlags);
    for (const bool other : {false, true}) {
        At<uint32_t>(self, field::kFlags) = other ? flags | flag::kOtherDiagonal : flags & ~flag::kOtherDiagonal;
        reinterpret_cast<UpdatePlanesFn>(addr::cSC4PrebuiltNetworkOccupant_UpdatePlanes)(self);
        for (std::size_t i = 0; i < 6; ++i)
            hash.Mix(At<uint32_t>(self, field::kPlaneA + 4 * i));
        for (const uint32_t rotationFlip : {0u, 1u, 2u, 3u, 0x80u, 0x81u, 0x82u, 0x83u}) {
            Virtual<void>(self, slot::kSetRotationAndFlip, rotationFlip);
            for (const uint32_t pathOverride : {0u, 1u}) {
                At<uint32_t>(self, field::kPathIDOverride) = pathOverride;
                At<float>(self, field::kPathOriginX) = 2.5f;
                At<float>(self, field::kPathOriginZ) = -1.5f;
                MixSurface(hash, self, nullptr);
            }
        }
    }
    At<uint32_t>(self, field::kPathIDOverride) = 0;
    At<uint32_t>(self, field::kPathOriginX) = 0;
    At<uint32_t>(self, field::kPathOriginZ) = 0;
    At<uint32_t>(self, field::kFlags) = flags;
    Virtual<void>(self, slot::kSetRotationAndFlip, 0u);

    hash.Mix(Virtual<uint8_t>(self, slot::kShutdown));
    At<uint8_t>(self, field::kOccupant + 0x2E) |= 1;
    hash.Mix(Virtual<uint8_t>(part, 0xC));
    At<uint8_t>(self, field::kOccupant + 0x2E) &= 0xFE;
    Virtual<uint32_t>(self, 0x8);
    return hash.h;
}

// OPENSC4_TEST_PREBUILTOCC=<day>: on that day one made object goes through
// Life and every second object of the city (chosen by the cell of its
// corner 0, slot 59, so the choice does not follow the order of the walk)
// through Exercise. The read-back checksums are summed into the
// fingerprint's ` set` field from then on (printed also when the city has
// no such object).
void Drive(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_PREBUILTOCC") || !game::IsBuild641() || g_setDone)
        return;
    g_setDone = true;
    std::vector<char*> objects;
    if (!Collect(day.city, objects))
        return;
    g_setSum += Life();
    for (char* const self : objects) {
        uint32_t cellX = 0, cellZ = 0;
        Virtual<void>(self, slot::kGetOccupiedCell, &cellX, &cellZ);
        if ((cellX * 31u + cellZ) % 2 != 0)
            continue;
        g_setSum += Exercise(self);
        ++g_setCount;
    }
}

// The fingerprint part: the number of objects and the sum and the exclusive
// or of ObjectHash over them (order independent; no addresses), then the
// counters, and after OPENSC4_TEST_PREBUILTOCC's day ` set <checksum>,
// <count>`.
void Fingerprint(cISC4City* city, std::string& line)
{
    std::vector<char*> objects;
    if (!Collect(city, objects)) {
        line += " | prebuiltocc none";
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
                  " | prebuiltocc %u,%08X,%08X pyl %u diag %u slope %u off %u pov %u first %u rot %u",
                  static_cast<unsigned>(objects.size()), sum, mixed, counters.pylon, counters.otherDiagonal,
                  counters.sloped, counters.offset, counters.pieceOverride, counters.firstPlane, counters.turned);
    line += text;
    if (g_setDone) {
        std::snprintf(text, sizeof text, " set %08X,%u", g_setSum, static_cast<unsigned>(g_setCount));
        line += text;
    }
}

const FingerprintPart kFingerprint{"prebuiltocc", 126, &Fingerprint};
const DayHook kDrive{"prebuiltocc.drive", 189, &Drive};

} // namespace
} // namespace opensc4::autotest
