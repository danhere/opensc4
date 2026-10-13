// The underground pieces' intermediate class in the autotest
// (src/core/autotest_hooks.h): the functions of src/core/pipebase_1.cpp and
// src/core/pipebase_2.cpp, reached through the pipes and the subway pieces
// of the city.
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/occmgr_sim.h"
#include "core/pipebase_sim.h"

#include <cISC4City.h>

#include <opensc4/addresses.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <unordered_set>
#include <vector>

namespace opensc4::autotest {
namespace {

namespace field = pipebase_sim::field;
namespace slot = pipebase_sim::slot;
namespace flag = pipebase_sim::flag;
namespace part = pipebase_sim::occupant;
namespace statics = pipebase_sim::statics;

struct Hash {
    uint32_t h = 2166136261u;
    void Mix(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    }
};

uint32_t Bits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

// How many pieces answered something that says the test reaches the paths
// of the functions (printed after the checksums).
struct Counters {
    uint32_t pipes = 0, subways = 0, placed = 0, flat = 0, sloped = 0, walls = 0, open = 0, runs = 0, sided = 0;
    uint32_t boxed = 0, keyed = 0;
};

// The functions of the class that are in no vtable an object has, called at
// the game's addresses (so the game's function or our detour).
uint8_t NeighborLacksTop(char* self, uint32_t edge)
{
    return reinterpret_cast<uint8_t(__thiscall*)(void*, uint32_t)>(addr::SC4PipeBase_F65A640)(self, edge);
}

// The class's own piece key (0x0065A0E0) wants the virtual base of an
// object of this very class, the object + 0x274; it only goes back to the
// object from there, so it is given that address of a pipe or a subway
// piece, whose own virtual base lies further on.
uint32_t BasePieceKey(char* self)
{
    uint32_t key = 0;
    reinterpret_cast<uint32_t(__thiscall*)(void*, uint32_t*)>(addr::SC4PipeBase_GetPieceKey)(self + field::kVbase,
                                                                                              &key);
    return key;
}

// One QueryInterface through a part: the answer, and where in the object
// the answer lies (never the address); the reference it gave is released.
void AskInterface(Hash& hash, char* self, char* asked, uint32_t iid)
{
    void* found = nullptr;
    const bool answered = Virtual<uint8_t>(asked, slot::kQueryInterface, iid, &found) != 0;
    hash.Mix(answered);
    if (answered && found) {
        hash.Mix(static_cast<uint32_t>(static_cast<char*>(found) - self));
        Virtual<uint32_t>(found, slot::kRelease);
    }
}

// The records of one corner vector: their number and their bytes (floats
// and four bytes each, no pointers).
void MixVector(Hash& hash, char* vector)
{
    const char* const begin = At<char*>(vector, 0x0);
    const char* const end = At<char*>(vector, 0x4);
    if (!begin || end < begin) {
        hash.Mix(0);
        return;
    }
    const std::size_t bytes = static_cast<std::size_t>(end - begin);
    hash.Mix(static_cast<uint32_t>(bytes / pipebase_sim::kCornerSize));
    hash.Mix(BytesChecksum(begin, bytes));
}

// The saved geometry of a piece: the wall count, the six saved vectors (the
// four side walls, the floor, +0x1C0) and the matrix. Slots 89 and 90 fill
// the vectors and slot 85 the matrix; Read loads them.
uint32_t GeometryHash(char* self)
{
    Hash hash;
    hash.Mix(At<uint32_t>(self, field::kWallCount));
    for (std::size_t i = 0; i < 4; ++i)
        MixVector(hash, self + field::kWalls + i * pipebase_sim::kVectorSize);
    MixVector(hash, self + field::kFloor);
    MixVector(hash, self + field::kVector1C0);
    for (std::size_t i = 0; i < 16; ++i)
        hash.Mix(At<uint32_t>(self, field::kMatrix + 4 * i));
    return hash.h;
}

// The six words of the box kept in the object (+0x124 low, +0x130 high).
void MixStoredBox(Hash& hash, char* self)
{
    for (std::size_t i = 0; i < 6; ++i)
        hash.Mix(At<uint32_t>(self, field::kBoxLow + 4 * i));
}

// What the intermediate class says of one piece, through the object's
// vtables (so the game's functions or ours; the network occupant's walk in
// src/core/netocc_autotest.cpp has the base's part and the pipe's in
// src/core/pipeocc_autotest.cpp the pipe's own): the class ID (slot 82 and
// the serializable part's), IsPlaced, the occupant type, the sides (slot 92),
// the heights of the ground (slot 94), the shape (slot 96), the depth of
// each edge (slot 93), the position and the box worked out (the occupant
// part's slots 8 and 10, and slot 80 into the test's own floats, with
// whether it is the box kept at +0x124), the tint (slot 103), whether the
// descriptor stored at +0x26C is slot 101's answer, the class's own piece
// key (at its address), for each edge whether the neighbour lacks the top
// flag (0x0065A640) and the run of flat pieces (slot 98, one more to
// follow), the side runs (slot 99), the network flags these functions set
// or read, and the saved fields: the two bytes and the float of the model
// part's state, the cell, the three side bytes, the floor depth, the word
// at +0x268. For the pieces of every fifth cell (by the cell, not by the
// order of the walk) QueryInterface through the interface part and the
// occupant part for the class's IID, the network occupant's, 0xA9764C8C,
// the occupant's, the serializable's, the model part's, 1 and one nobody
// answers. Not mixed: the dirty bytes at +0x260 to +0x262, the texture
// bindings, the object at +0x244, the two vectors that are not saved,
// pointers and reference counts.
uint32_t PieceHash(char* self, Counters& counters)
{
    Hash hash;
    char* const occupant = self + field::kOccupant;
    const uint32_t classId = Virtual<uint32_t>(self + field::kSerializable, pipebase_sim::kGetGZCLSID);
    hash.Mix(classId);
    hash.Mix(Virtual<uint32_t>(self, slot::kGetClassId));
    counters.pipes += classId == pipebase_sim::kPipeClassId;
    counters.subways += classId == pipebase_sim::kSubwayClassId;
    const uint32_t placed = Virtual<uint32_t>(self, slot::kIsPlaced);
    hash.Mix(placed);
    counters.placed += placed != 0;
    hash.Mix(Virtual<uint32_t>(occupant, part::kGetType));

    uint8_t sides[3] = {0xEE, 0xEE, 0xEE};
    Virtual<void>(self, slot::kGetSides, &sides[0], &sides[1], &sides[2]);
    for (const uint8_t side : sides)
        hash.Mix(side);
    uint32_t surface[4] = {};
    Virtual<uint32_t*>(self, slot::kGetSurfaceHeights, static_cast<uint32_t*>(surface));
    for (const uint32_t word : surface)
        hash.Mix(word);
    uint32_t pieceKey = 0, word268 = 0, heights[4] = {}, depth = 0;
    Virtual<uint32_t>(self, slot::kGetShape, &pieceKey, &word268, static_cast<uint32_t*>(heights), &depth);
    hash.Mix(pieceKey);
    hash.Mix(word268);
    for (const uint32_t word : heights)
        hash.Mix(word);
    hash.Mix(depth);
    counters.flat += pieceKey == pipebase_sim::key::kFlat || pieceKey == pipebase_sim::key::kFlat300;
    for (uint32_t edge = 0; edge < 4; ++edge) {
        uint32_t edgeDepth = 0;
        uint8_t sloped = 0xEE;
        Virtual<void>(self, slot::kGetEdgeDepth, &edgeDepth, &sloped, edge);
        hash.Mix(edgeDepth);
        hash.Mix(sloped);
        counters.sloped += sloped == 1;
    }

    uint32_t position[3] = {};
    Virtual<void>(occupant, part::kGetPosition, static_cast<uint32_t*>(position));
    for (const uint32_t word : position)
        hash.Mix(word);
    uint32_t low[3] = {}, high[3] = {};
    Virtual<void>(occupant, part::kGetBoundingBox, static_cast<uint32_t*>(low), static_cast<uint32_t*>(high));
    for (std::size_t i = 0; i < 3; ++i) {
        hash.Mix(low[i]);
        hash.Mix(high[i]);
    }
    uint32_t box[6] = {};
    Virtual<void>(self, slot::kComputeBoundingBox, &box[0], &box[3]);
    for (const uint32_t word : box)
        hash.Mix(word);
    const bool boxed = std::memcmp(box, self + field::kBoxLow, sizeof box) == 0;
    hash.Mix(boxed);
    counters.boxed += boxed;

    uint32_t tint[3] = {};
    Virtual<uint32_t*>(self, slot::kGetTint, static_cast<uint32_t*>(tint));
    for (const uint32_t word : tint)
        hash.Mix(word);
    hash.Mix(At<std::uintptr_t>(self, field::kDescriptor) == Virtual<std::uintptr_t>(self, slot::kGetDescriptor));
    const uint32_t key = BasePieceKey(self);
    hash.Mix(key);
    counters.keyed += key != pipebase_sim::kIidPipeBase;

    for (uint32_t edge = 0; edge < 4; ++edge) {
        const uint8_t open = NeighborLacksTop(self, edge);
        hash.Mix(open);
        counters.open += open != 0;
        const uint32_t run = Virtual<uint32_t>(self, slot::kCountRun, self, edge, 1u);
        hash.Mix(run);
        counters.runs += run > 1;
    }
    uint8_t side = 0xEE;
    const uint32_t sideRuns = Virtual<uint32_t>(self, slot::kGetSideRuns, &side);
    hash.Mix(sideRuns);
    hash.Mix(side);
    counters.sided += sideRuns != 0;

    hash.Mix(Virtual<uint32_t>(self, slot::kGetNetworkFlags) &
             (flag::kTextures | flag::kSide248 | flag::kSide249 | flag::kSide24A | flag::kTop));
    hash.Mix(At<uint8_t>(self, field::kModelState));
    hash.Mix(At<uint8_t>(self, field::kModelState + 1));
    hash.Mix(At<uint32_t>(self, field::kStateFloat));
    hash.Mix(At<uint32_t>(self, field::kCellX));
    hash.Mix(At<uint32_t>(self, field::kCellZ));
    for (std::size_t i = 0; i < 3; ++i)
        hash.Mix(At<uint8_t>(self, field::kSides + i));
    hash.Mix(At<uint32_t>(self, field::kFloorDepth));
    hash.Mix(At<uint32_t>(self, field::kWord268));
    counters.walls += At<uint32_t>(self, field::kWallCount);

    if ((At<uint32_t>(self, field::kCellX) * 31u + At<uint32_t>(self, field::kCellZ)) % 5 == 0) {
        const uint32_t ids[] = {pipebase_sim::kIidPipeBase,
                                pipebase_sim::kIidNetworkOccupant,
                                pipebase_sim::kIidObject,
                                0x26BB767Eu,
                                0xE4FDA3D4u,
                                0x26A68449u,
                                1u,
                                0x0F10A0CCu};
        for (char* const asked : {self, occupant})
            for (const uint32_t iid : ids)
                AskInterface(hash, self, asked, iid);
    }
    return hash.h;
}

// The class's statics: the three depth statics slot 97 sets (two floats'
// bits and a byte) and the byte that says the class's statics ran.
uint32_t StaticsHash()
{
    Hash hash;
    hash.Mix(Global<uint32_t>(addr::SC4PipeBase_DepthStatics + statics::kDepth));
    hash.Mix(Global<uint8_t>(addr::SC4PipeBase_DepthStatics + statics::kFromGround));
    hash.Mix(Global<uint32_t>(addr::SC4PipeBase_DepthStatics + statics::kRise));
    hash.Mix(Global<uint8_t>(addr::SC4PipeBase_StaticsDone));
    return hash.h;
}

// Every underground piece of the city (build 641; src/core/pipebase_sim.h),
// found through the occupant manager (the city's slot 34 / +0x88): every
// occupant of the network occupant type 0xC772BF98 in its grid, one in
// several cells once, whose serializable part answers the pipe's or the
// subway's class ID. The objects are the interface part (the occupant part
// less 4), in the order of the cell the class keeps for them (x, then z,
// then the class ID), so that what is done to them one after the other does
// not follow the order of the walk. False without a manager.
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
                if (Virtual<uint32_t>(object, part::kGetType) != pipebase_sim::kOccupantType)
                    continue;
                char* const self = static_cast<char*>(object) - field::kOccupant;
                const uint32_t classId = Virtual<uint32_t>(self + field::kSerializable, pipebase_sim::kGetGZCLSID);
                if (classId == pipebase_sim::kPipeClassId || classId == pipebase_sim::kSubwayClassId)
                    seen.insert(self);
            }
        }
    }
    Virtual<void>(manager + occmgr_sim::field::kLock, 0x8);
    found.assign(seen.begin(), seen.end());
    std::sort(found.begin(), found.end(), [](char* a, char* b) {
        for (const std::size_t offset : {field::kCellX, field::kCellZ, field::kClassId})
            if (At<uint32_t>(a, offset) != At<uint32_t>(b, offset))
                return At<uint32_t>(a, offset) < At<uint32_t>(b, offset);
        return false;
    });
    return true;
}

// The setters' exercise (OPENSC4_TEST_PIPEBASE=<day>): the checksum, summed
// over the pieces driven, how many were driven, how many of them came back
// from the exercise with another box, floor depth, side bytes or flags than
// they had (put back by hand then), and how many kept their saved vectors
// when the walls and the floor were made again.
uint32_t g_setSum = 0;
uint32_t g_setCount = 0;
uint32_t g_setDrift = 0;
uint32_t g_setSame = 0;
uint32_t g_onceSum = 0;
bool g_setDone = false;

// What a piece's state is after a setter: the floor depth, the side bytes
// and the flags slot 88 sets.
void MixState(Hash& hash, char* self)
{
    hash.Mix(At<uint32_t>(self, field::kFloorDepth));
    for (std::size_t i = 0; i < 3; ++i)
        hash.Mix(At<uint8_t>(self, field::kSides + i));
    hash.Mix(At<uint32_t>(self, field::kFlags) & (flag::kSide248 | flag::kSide249 | flag::kSide24A));
}

// The setters on one piece of the city, each read back, and the piece as it
// was after (the checksum is of the read-backs):
//   - the flag setters (slots 10 and 11) with the texture bit 0x10000, which
//     the network occupant's own setters only store: the other state first,
//     then the old one, with the dirty byte cleared before each and read
//     after (a change of the bits 0x30000 sets it), and once with no bit;
//   - SetSurfaceHeights (slot 95) with every height two higher and with the
//     old heights: the heights (slot 94), the floor depth and the box kept
//     at +0x124 after each;
//   - the depth statics (slot 97) as they are and as (5, from the corners,
//     7), and under each SetPiece (slot 30) with eight piece keys and the
//     eight rotation and flip bytes, UpdateSides (slot 88) and the depth of
//     each edge (slot 93) after every one; for the flat key also the runs
//     (slot 98) and, with the class ID word set to the subway's for the
//     call, the side runs (slot 99). Then the old statics, the old piece
//     and UpdateSides again;
//   - the floor and the side walls made again without the refresh (slots 90
//     and 89 with 0), then SetHeights (slot 84) with the heights the corners
//     have, which makes them a third time with the refresh (slot 100, with
//     1): the dirty bytes are cleared before, so the refresh does its work
//     (the box, the view, the neighbours, which are left alone by their own
//     refresh while they are dirty), and read after; the saved vectors'
//     checksum after each.
// Put back by hand after: the dirty bytes, and the box, the floor depth,
// the side bytes and the flags when the functions did not bring them back
// themselves (counted). The saved vectors stay as slots 89 and 90 made
// them; whether they are what the piece had is counted. Not called: slot 86
// (the rebuild: it runs slot 87, which loads textures).
uint32_t Exercise(char* self)
{
    Hash hash;
    const uint32_t oldFlags = At<uint32_t>(self, field::kFlags);
    uint8_t oldDirty[3], oldSides[3];
    std::memcpy(oldDirty, self + field::kDirty, sizeof oldDirty);
    std::memcpy(oldSides, self + field::kSides, sizeof oldSides);
    uint32_t oldSurface[4], oldBox[6];
    std::memcpy(oldSurface, self + field::kSurface, sizeof oldSurface);
    std::memcpy(oldBox, self + field::kBoxLow, sizeof oldBox);
    const uint32_t oldDepth = At<uint32_t>(self, field::kFloorDepth);
    const uint32_t oldKey = At<uint32_t>(self, field::kPieceKey);
    const uint8_t oldTurn = At<uint8_t>(self, field::kRotationFlip);
    const uint32_t oldClass = At<uint32_t>(self, field::kClassId);
    const uint32_t geometry = GeometryHash(self);

    const bool had = (oldFlags & flag::kTextureA) != 0;
    for (const std::size_t setter : {had ? slot::kClearNetworkFlag : slot::kSetNetworkFlag,
                                     had ? slot::kSetNetworkFlag : slot::kClearNetworkFlag}) {
        At<uint8_t>(self, field::kDirty) = 0;
        Virtual<uint32_t>(self, setter, flag::kTextureA);
        hash.Mix(Virtual<uint32_t>(self, slot::kGetNetworkFlags) & flag::kTextures);
        hash.Mix(At<uint8_t>(self, field::kDirty));
    }
    At<uint8_t>(self, field::kDirty) = 0;
    Virtual<uint32_t>(self, slot::kSetNetworkFlag, 0u);
    Virtual<uint32_t>(self, slot::kClearNetworkFlag, 0u);
    hash.Mix(At<uint8_t>(self, field::kDirty));

    uint32_t raised[4];
    for (std::size_t i = 0; i < 4; ++i) {
        float height;
        std::memcpy(&height, &oldSurface[i], sizeof height);
        raised[i] = Bits(height + 2.0f);
    }
    for (const uint32_t* const heights : {static_cast<const uint32_t*>(raised),
                                          static_cast<const uint32_t*>(oldSurface)}) {
        Virtual<void>(self, slot::kSetSurfaceHeights, heights);
        uint32_t back[4] = {};
        Virtual<uint32_t*>(self, slot::kGetSurfaceHeights, static_cast<uint32_t*>(back));
        for (const uint32_t word : back)
            hash.Mix(word);
        hash.Mix(At<uint32_t>(self, field::kFloorDepth));
        MixStoredBox(hash, self);
    }

    const std::uintptr_t depthStatics = addr::SC4PipeBase_DepthStatics;
    const uint32_t oldStaticDepth = Global<uint32_t>(depthStatics + statics::kDepth);
    const uint8_t oldFromGround = Global<uint8_t>(depthStatics + statics::kFromGround);
    const uint32_t oldRise = Global<uint32_t>(depthStatics + statics::kRise);
    const uint32_t keys[] = {0x200u, 0x300u, 0x800u, 0xC00u, 0x1E00u, 0x4B00u, 0x14E00u, 0x1234u};
    const uint32_t turns[] = {0u, 1u, 2u, 3u, 0x80u, 0x81u, 0x82u, 0x83u};
    for (int pass = 0; pass < 2; ++pass) {
        if (pass == 0)
            Virtual<uint32_t>(self, slot::kSetDepthStatics, oldStaticDepth, uint32_t{oldFromGround}, oldRise);
        else
            Virtual<uint32_t>(self, slot::kSetDepthStatics, Bits(5.0f), 0u, Bits(7.0f));
        hash.Mix(StaticsHash());
        for (const uint32_t pieceKey : keys)
            for (const uint32_t turn : turns) {
                Virtual<void>(self, slot::kSetPiece, pieceKey, turn);
                Virtual<void>(self, slot::kUpdateSides);
                MixState(hash, self);
                for (uint32_t edge = 0; edge < 4; ++edge) {
                    uint32_t edgeDepth = 0;
                    uint8_t sloped = 0xEE;
                    Virtual<void>(self, slot::kGetEdgeDepth, &edgeDepth, &sloped, edge);
                    hash.Mix(edgeDepth);
                    hash.Mix(sloped);
                }
                if (pass != 0 || pieceKey != pipebase_sim::key::kFlat)
                    continue;
                for (uint32_t edge = 0; edge < 4; ++edge)
                    hash.Mix(Virtual<uint32_t>(self, slot::kCountRun, self, edge, 3u));
                uint8_t side = 0xEE;
                At<uint32_t>(self, field::kClassId) = pipebase_sim::kSubwayClassId;
                hash.Mix(Virtual<uint32_t>(self, slot::kGetSideRuns, &side));
                At<uint32_t>(self, field::kClassId) = oldClass;
                hash.Mix(side);
            }
    }
    Virtual<uint32_t>(self, slot::kSetDepthStatics, oldStaticDepth, uint32_t{oldFromGround}, oldRise);
    Virtual<void>(self, slot::kSetPiece, oldKey, uint32_t{oldTurn});
    Virtual<void>(self, slot::kUpdateSides);
    MixState(hash, self);

    Virtual<void>(self, slot::kSetFloor, 0u);
    Virtual<void>(self, slot::kSetSideWalls, 0u);
    hash.Mix(GeometryHash(self));
    uint32_t corners[4];
    for (std::size_t i = 0; i < 4; ++i)
        corners[i] = At<uint32_t>(self, field::kCornerY + i * pipebase_sim::kCornerSize);
    At<uint8_t>(self, field::kDirty) = 0;
    At<uint8_t>(self, field::kRebuild) = 0;
    Virtual<void>(self, slot::kSetHeights, static_cast<const uint32_t*>(corners));
    hash.Mix(At<uint8_t>(self, field::kDirty));
    hash.Mix(At<uint8_t>(self, field::kRebuild));
    const uint32_t after = GeometryHash(self);
    hash.Mix(after);
    MixState(hash, self);
    MixStoredBox(hash, self);
    g_setSame += after == geometry;

    const bool drift = std::memcmp(oldBox, self + field::kBoxLow, sizeof oldBox) != 0 ||
                       oldDepth != At<uint32_t>(self, field::kFloorDepth) ||
                       std::memcmp(oldSides, self + field::kSides, sizeof oldSides) != 0 ||
                       oldFlags != At<uint32_t>(self, field::kFlags);
    g_setDrift += drift;
    std::memcpy(self + field::kBoxLow, oldBox, sizeof oldBox);
    At<uint32_t>(self, field::kFloorDepth) = oldDepth;
    std::memcpy(self + field::kSides, oldSides, sizeof oldSides);
    At<uint32_t>(self, field::kFlags) = oldFlags;
    std::memcpy(self + field::kDirty, oldDirty, sizeof oldDirty);
    return hash.h;
}

// On one piece, once (the first in the order of the cells): the occupant
// part's PostOccupantMessage (slot 15) with a message nobody listens to
// (the base's post queues it; the class adds its own message only for the
// occupant manager's two); and, when the class's statics ran, the class's
// own occupant Init and statics at their addresses, which then leave an
// initialised object and the statics alone (their answers are mixed).
uint32_t Once(char* self)
{
    Hash hash;
    char* const occupant = self + field::kOccupant;
    Virtual<uint32_t>(occupant, part::kPostOccupantMessage, 0x0F10A0CCu, 0u);
    hash.Mix(1);
    if (Global<uint8_t>(addr::SC4PipeBase_StaticsDone) && Virtual<uint8_t>(occupant, part::kIsInitialized)) {
        hash.Mix(reinterpret_cast<uint8_t(__thiscall*)(void*)>(addr::SC4PipeBase_OccupantInit)(occupant));
        hash.Mix(reinterpret_cast<uint8_t(__cdecl*)()>(addr::SC4PipeBase_InitStatics)());
    }
    return hash.h;
}

// OPENSC4_TEST_PIPEBASE=<day>: on that day the pieces of every seventh cell
// (chosen by the cell the class keeps at +0x23C and +0x240, not by the order
// of the walk), in the order of their cells, go through Exercise, and the
// first piece of the city through Once. The checksums are in the
// fingerprint's ` set` field from then on, and one log line says what ran,
// also when the city has no underground piece.
void Drive(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_PIPEBASE") || !game::IsBuild641() || g_setDone)
        return;
    g_setDone = true;
    std::vector<char*> objects;
    const bool collected = Collect(day.city, objects);
    if (!objects.empty())
        g_onceSum = Once(objects.front());
    for (char* const self : objects) {
        if ((At<uint32_t>(self, field::kCellX) * 31u + At<uint32_t>(self, field::kCellZ)) % 7 != 0)
            continue;
        g_setSum += Exercise(self);
        ++g_setCount;
    }
    log::Write("autotest: pipebase: %u of %u underground pieces driven %08X (%u came back changed, %u kept their "
               "saved vectors), once %08X%s",
               g_setCount, static_cast<unsigned>(objects.size()), g_setSum, g_setDrift, g_setSame, g_onceSum,
               collected ? "" : " (no manager)");
}

// The fingerprint part: the number of underground pieces and the sum and
// the exclusive or of PieceHash over them (order independent; no
// addresses), `geo`, the sum of the saved geometry's checksum (the vectors
// and the matrix, apart from the rest so that a difference says where it
// is), the counters (pipes, subway pieces, placed, flat pieces, edges of
// slopes, side walls in use, edges whose neighbour lacks the top flag, runs
// longer than one, side runs, boxes that are the kept box, pieces whose own
// key is the piece key) and the statics' checksum; after
// OPENSC4_TEST_PIPEBASE's day ` set <checksum>,<count>,<changed>,<same
// vectors>,<once>`.
void Fingerprint(cISC4City* city, std::string& line)
{
    std::vector<char*> objects;
    if (!Collect(city, objects)) {
        line += " | pipebase none";
        return;
    }
    uint32_t sum = 0, mixed = 0, geometry = 0;
    Counters counters;
    for (char* const self : objects) {
        const uint32_t hash = PieceHash(self, counters);
        sum += hash;
        mixed ^= hash;
        geometry += GeometryHash(self);
    }
    char text[320];
    std::snprintf(text, sizeof text,
                  " | pipebase %u,%08X,%08X geo %08X pipe %u sub %u placed %u flat %u slope %u wall %u open %u "
                  "run %u side %u box %u key %u st %08X",
                  static_cast<unsigned>(objects.size()), sum, mixed, geometry, counters.pipes, counters.subways,
                  counters.placed, counters.flat, counters.sloped, counters.walls, counters.open, counters.runs,
                  counters.sided, counters.boxed, counters.keyed, StaticsHash());
    line += text;
    if (g_setDone) {
        std::snprintf(text, sizeof text, " set %08X,%u,%u,%u,%08X", g_setSum, static_cast<unsigned>(g_setCount),
                      g_setDrift, g_setSame, g_onceSum);
        line += text;
    }
}

const FingerprintPart kFingerprint{"pipebase", 129, &Fingerprint};
const DayHook kDrive{"pipebase.drive", 192, &Drive};

} // namespace
} // namespace opensc4::autotest
