// The occupant manager's part of the autotest (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/occmgr_sim.h"

#include <cISC4City.h>

#include <opensc4/addresses.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

namespace opensc4::autotest {
namespace {

using occmgr_sim::Cell;
using occmgr_sim::CellAt;
using occmgr_sim::Grid;
using occmgr_sim::kWorldCells;
using occmgr_sim::ListNode;
using occmgr_sim::OccupantOf;
using occmgr_sim::Vector;
namespace field = occmgr_sim::field;
namespace slot = occmgr_sim::slot;
namespace occupant = occmgr_sim::occupant;

struct Hash {
    uint32_t h = 2166136261u;
    void Mix(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    }
    void MixFloat(float v)
    {
        uint32_t bits;
        std::memcpy(&bits, &v, sizeof bits);
        Mix(bits);
    }
};

// What identifies an occupant without its address: its type, the bounding
// cells it recorded (0xFFFFFFFF for none) and its position in x and z
// (not a prop's).
// The two occupant types of the four automaton classes (registered
// together at 0x00514A00).
constexpr uint32_t kTypeAutomaton = 0xC7CC75F1;
constexpr uint32_t kTypeAutomaton2 = 0x496E5783;
constexpr uint32_t kTypeProp = 0xA823821E;

// The game's own functions behind the manager's 50 slots (vtable
// 0x00A8F7F0), to ask them the same questions as whatever the manager's
// vtable holds now.
constexpr std::uintptr_t kGameSlots[50] = {
    0x0049D910, 0x0049DF80, 0x0049DFB0, 0x004A18E0, 0x0049E000, 0x0049E050, 0x0049D990, 0x0049D9B0, 0x0049D9D0,
    0x0049DB50, 0x0049D9F0, 0x0049DA10, 0x0049DA30, 0x0049DA50, 0x0049DA80, 0x0049E0E0, 0x0049DAF0, 0x0049E150,
    0x0049DB20, 0x0049E1B0, 0x0049EF60, 0x0049F240, 0x0049FCD0, 0x0049DB80, 0x0049F3C0, 0x0049E490, 0x0049DBA0,
    0x0049FB30, 0x0049E4D0, 0x0049DC80, 0x0049E5E0, 0x0049DCB0, 0x0049E690, 0x0049DCE0, 0x0049E800, 0x0049DD10,
    0x004A0670, 0x0049DD40, 0x004A0A70, 0x0049DD80, 0x0049E250, 0x0049DDC0, 0x004A0D70, 0x0049DE00, 0x004A1360,
    0x0049DE40, 0x0049DE80, 0x0049E370, 0x0049EF30, 0x004A18C0,
};

// Whether Slot calls the game's functions or the manager's vtable.
bool g_gameSlots = false;

template <typename R, typename... Args>
R Slot(char* manager, std::size_t offset, Args... args)
{
    using Fn = R(__thiscall*)(void*, Args...);
    const std::size_t index = offset / sizeof(void*);
    const auto fn = g_gameSlots && index < 50 ? reinterpret_cast<Fn>(kGameSlots[index])
                                              : reinterpret_cast<Fn>((*reinterpret_cast<void***>(manager))[index]);
    return fn(manager, args...);
}

void MixOccupant(Hash& hash, void* object)
{
    const uint32_t type = Virtual<uint32_t>(object, occupant::kGetType);
    hash.Mix(type);
    uint8_t rect[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    Virtual<uint8_t>(object, occupant::kGetCells, static_cast<uint8_t*>(rect));
    hash.Mix(static_cast<uint32_t>(rect[0]) | static_cast<uint32_t>(rect[1]) << 8 |
             static_cast<uint32_t>(rect[2]) << 16 | static_cast<uint32_t>(rect[3]) << 24);
    // A prop's position inside its cells is not mixed: some props move by
    // frames, and their positions differ between two runs of the original
    // (Rush Hour d0090) while their cells agree.
    if (type == kTypeProp)
        return;
    float position[3] = {};
    Virtual<uint8_t>(object, occupant::kGetPosition, static_cast<float*>(position));
    hash.MixFloat(position[0]);
    hash.MixFloat(position[2]);
}

// The occupants of a list of the node allocator in order, then released
// and the list emptied through the manager's slot 48.
void MixList(Hash& hash, char* manager, ListNode** list)
{
    uint32_t count = 0;
    for (const ListNode* node = (*list)->next; node != *list; node = node->next, ++count)
        MixOccupant(hash, reinterpret_cast<void*>(node->value));
    hash.Mix(count);
    Slot<uint32_t>(manager, slot::kReleaseOccupantList, list);
}

// A first-occupant slot: the result (AL) and the occupant, released.
template <typename... Args>
void First(Hash& hash, char* manager, std::size_t offset, Args... args)
{
    void* found = nullptr;
    const uint32_t result = Slot<uint32_t>(manager, offset, &found, args...) & 0xFF;
    hash.Mix(result);
    if (result && found) {
        MixOccupant(hash, found);
        Virtual<uint32_t>(found, occupant::kRelease);
    }
}

struct Walk {
    Hash* hash;
    int32_t left;
};

// The iterate slots' callback: the occupant mixed; false after `left`.
uint8_t __cdecl OnOccupant(void* object, void* context)
{
    Walk* const walk = static_cast<Walk*>(context);
    MixOccupant(*walk->hash, object);
    return --walk->left > 0 ? 1 : 0;
}

using CallbackFn = uint8_t(__cdecl*)(void*, void*);

// OPENSC4_TEST_OCCMGR=1: a checksum of the manager's slots, pure queries
// only (nothing is inserted, removed or moved; the by-type slots set the
// type of the manager's own filter, which every such call sets anew).
//   - the conversions (slots 6 to 19) and GetBoundingCells (26) on a
//     lattice of cells and positions, far outside the grid included: they
//     are plain arithmetic and test no range. The values the slots leave
//     in EAX are not mixed (slot 19's is the FPU status word).
//   - IsCellEmpty (25) and GetFirstOccupant (30, 31) on world cells inside
//     and outside the grid (both test the range).
//   - GetFirstOccupantByPosition (28, 29) and ByStandardCityCell (32, 33)
//     only where the position's world cell is inside the grid: the game
//     reads its grid there without a range test.
//   - ByStandardCityCells (34, 35), the get slots (36 to 41) by box, by
//     world cells and by city cells, with and without limits, corners and
//     types, and the iterate slots (42 to 47) with a callback that stops
//     after a while; each occupant's type, cells and position in the order
//     returned.
//   - FindOccupant (27), both ways, for the first occupants found.
uint32_t Queries(char* manager)
{
    Hash hash;
    float sizes[2] = {};
    int32_t counts[2] = {};
    int32_t bounds[4] = {};
    Slot<uint32_t>(manager, 0x18, &sizes[0], &sizes[1]);
    hash.MixFloat(sizes[0]);
    hash.MixFloat(sizes[1]);
    Slot<uint32_t>(manager, 0x1C, &counts[0], &counts[1]);
    hash.Mix(static_cast<uint32_t>(counts[0]));
    hash.Mix(static_cast<uint32_t>(counts[1]));
    Slot<uint32_t>(manager, 0x20, &counts[0], &counts[1]);
    hash.Mix(static_cast<uint32_t>(counts[0]));
    hash.Mix(static_cast<uint32_t>(counts[1]));
    Slot<uint32_t>(manager, 0x24, &bounds[0], &bounds[1], &bounds[2], &bounds[3]);
    for (const int32_t bound : bounds)
        hash.Mix(static_cast<uint32_t>(bound));

    static const int32_t kCells[] = {-70000, -129, -5, -1, 0, 1, 2, 5, 63, 64, 65, 100, 127,
                                     128,    189,  190, 191, 192, 255, 256, 1027, 70000};
    const int cellCount = static_cast<int>(sizeof kCells / sizeof kCells[0]);
    for (int i = 0; i < cellCount; ++i)
        for (int j = 0; j < 3; ++j) {
            const int32_t x = kCells[i], z = kCells[(i * 7 + j * 5 + 3) % cellCount];
            for (const std::size_t offset :
                 {slot::kWorldCellToOccupantManagerCell, slot::kOccupantManagerCellToWorldCell,
                  slot::kWorldCellToStandardCityCell, slot::kStandardCityCellToWorldCell}) {
                int32_t outX = 0x1234, outZ = 0x4321;
                Slot<uint32_t>(manager, offset, x, z, &outX, &outZ);
                hash.Mix(static_cast<uint32_t>(outX));
                hash.Mix(static_cast<uint32_t>(outZ));
            }
            for (const std::size_t offset : {slot::kWorldCellToPosition, slot::kOccupantManagerCellToPosition,
                                             slot::kStandardCityCellToPosition}) {
                float outX = 0.0f, outZ = 0.0f;
                Slot<uint32_t>(manager, offset, x, z, &outX, &outZ);
                hash.MixFloat(outX);
                hash.MixFloat(outZ);
            }
            int32_t around[16] = {};
            int32_t found = -1;
            hash.Mix(static_cast<uint32_t>(
                Slot<int32_t>(manager, slot::kGetBoundingCells, x, z, static_cast<int32_t*>(around), &found)));
            hash.Mix(static_cast<uint32_t>(found));
            for (int32_t k = 0; k < found * 2 && k < 16; ++k)
                hash.Mix(static_cast<uint32_t>(around[k]));
            hash.Mix(Slot<uint8_t>(manager, slot::kIsCellEmpty, x, z));
            First(hash, manager, slot::kFirst, x, z, static_cast<void*>(nullptr));
        }

    static const float kPositions[] = {-1.0e30f, -70000.5f, -4096.0f, -4064.1f, -64.01f, -31.9f,   -0.5f,   0.0f,
                                       0.25f,    7.99f,     8.0f,     15.99f,   16.0f,   24.0f,    31.936f, 31.94f,
                                       32.0f,    48.0f,     63.9f,    64.0f,    95.9f,   96.0f,    100.25f, 1024.0f,
                                       2047.99f, 2048.0f,   4095.9f,  4096.0f,  8191.0f, 70000.5f, 1.0e30f};
    const int positionCount = static_cast<int>(sizeof kPositions / sizeof kPositions[0]);
    for (int i = 0; i < positionCount; ++i) {
        const float x = kPositions[i], z = kPositions[(i * 11 + 4) % positionCount];
        for (const std::size_t offset : {slot::kPositionToWorldCell, slot::kPositionToOccupantManagerCell,
                                         slot::kPositionToStandardCityCell}) {
            int32_t outX = 0, outZ = 0;
            Slot<uint32_t>(manager, offset, x, z, &outX, &outZ);
            hash.Mix(static_cast<uint32_t>(outX));
            hash.Mix(static_cast<uint32_t>(outZ));
        }
    }

    const float sizeX = At<float>(manager, field::kSizeX), sizeZ = At<float>(manager, field::kSizeZ);
    const int32_t cityX = At<int32_t>(manager, field::kCityCellsX), cityZ = At<int32_t>(manager, field::kCityCellsZ);
    const int32_t cellsX = At<int32_t>(manager, field::kCellsX), cellsZ = At<int32_t>(manager, field::kCellsZ);
    if (!(sizeX >= 64.0f && sizeX <= 4096.0f && sizeZ >= 64.0f && sizeZ <= 4096.0f) || cityX < 4 || cityZ < 4)
        return hash.h;

    ListNode* list = occmgr_sim::NewNodeList();
    void* const noFilter = nullptr;

    // The whole city, a few at most: where the types for the by-type slots
    // and the occupants for FindOccupant come from.
    uint32_t types[3] = {0, 0, 0};
    int typeCount = 0;
    hash.Mix(Slot<uint8_t>(manager, slot::kGetByBBox, &list, static_cast<const float*>(nullptr),
                              static_cast<const float*>(nullptr), noFilter, 60u));
    int probed = 0;
    for (const ListNode* node = list->next; node != list; node = node->next) {
        void* const object = reinterpret_cast<void*>(node->value);
        const uint32_t type = Virtual<uint32_t>(object, occupant::kGetType);
        bool known = false;
        for (int i = 0; i < typeCount; ++i)
            known = known || types[i] == type;
        if (!known && typeCount < 3)
            types[typeCount++] = type;
        if (probed++ < 12) {
            int32_t x = -1, z = -1;
            hash.Mix(Slot<uint8_t>(manager, slot::kFindOccupant, object, 0u, &x, &z));
            hash.Mix(Slot<uint8_t>(manager, slot::kFindOccupant, object, 1u, &x, &z));
            hash.Mix(static_cast<uint32_t>(x));
            hash.Mix(static_cast<uint32_t>(z));
        }
    }
    MixList(hash, manager, &list);
    hash.Mix(Slot<uint8_t>(manager, slot::kGetByBBox, &list, static_cast<const float*>(nullptr),
                              static_cast<const float*>(nullptr), noFilter, 0u));
    for (int i = 0; i < 3; ++i) {
        hash.Mix(Slot<uint8_t>(manager, slot::kGetByBBox + 4, &list, static_cast<const float*>(nullptr),
                                  static_cast<const float*>(nullptr), types[i], 40u));
        MixList(hash, manager, &list);
    }

    // Positions and boxes on a lattice over the city and a little beyond
    // its edges (inside the world grid).
    uint32_t turn = 0;
    for (int i = -1; i <= 7; ++i)
        for (int j = -1; j <= 7; ++j, ++turn) {
            const float x = sizeX * (static_cast<float>(i) + 0.37f) / 7.0f;
            const float z = sizeZ * (static_cast<float>(j) + 0.61f) / 7.0f;
            const uint32_t type = types[turn % 3];
            First(hash, manager, slot::kFirstByPosition, x, z, noFilter);
            First(hash, manager, slot::kFirstByPosition + 4, x, z, type);

            const float low[3] = {x - 40.0f, -1000.0f, z - 24.0f};
            const float high[3] = {x + (turn % 4 == 0 ? 8.0f : 150.0f), 5000.0f, z + (turn % 5 == 0 ? 5.0f : 90.0f)};
            hash.Mix(Slot<uint8_t>(manager, slot::kGetByBBox, &list, static_cast<const float*>(low),
                                      static_cast<const float*>(high), noFilter, turn % 6 == 0 ? 3u : 0xFFFFFFFFu));
            MixList(hash, manager, &list);
            hash.Mix(Slot<uint8_t>(manager, slot::kGetByBBox + 4, &list, static_cast<const float*>(low),
                                      static_cast<const float*>(high), type, 0xFFFFFFFFu));
            MixList(hash, manager, &list);
            Walk walk{&hash, turn % 3 == 0 ? 5 : 100000};
            hash.Mix(Slot<uint8_t>(manager, slot::kIterateByBBox, static_cast<CallbackFn>(&OnOccupant),
                                      static_cast<void*>(&walk), static_cast<const float*>(low),
                                      static_cast<const float*>(high), noFilter));
            walk.left = 100000;
            hash.Mix(Slot<uint8_t>(manager, slot::kIterateByBBox + 4, static_cast<CallbackFn>(&OnOccupant),
                                      static_cast<void*>(&walk), static_cast<const float*>(low),
                                      static_cast<const float*>(high), type));
        }

    // Standard city cells: single cells and rectangles, a few beyond the
    // edges.
    turn = 0;
    for (int i = -1; i <= 6; ++i)
        for (int j = -1; j <= 6; ++j, ++turn) {
            const int32_t x = i < 0 ? -6 : i == 6 ? cityX + 5 : cityX * i / 6 + 1;
            const int32_t z = j < 0 ? -3 : j == 6 ? cityZ + 9 : cityZ * j / 6 + 2;
            const uint32_t type = types[turn % 3];
            First(hash, manager, slot::kFirstByCityCell, x, z, noFilter);
            First(hash, manager, slot::kFirstByCityCell + 4, x, z, type);

            const int32_t low[2] = {x, z};
            const int32_t high[2] = {x + (turn % 3 == 0 ? 0 : 7), z + (turn % 4 == 0 ? 0 : 5)};
            const int32_t* const lowArg = turn % 11 == 10 ? nullptr : low;
            const int32_t* const highArg = turn % 13 == 12 ? nullptr : high;
            First(hash, manager, slot::kFirstByCityCells, lowArg, highArg, noFilter);
            First(hash, manager, slot::kFirstByCityCells + 4, lowArg, highArg, type);
            hash.Mix(Slot<uint8_t>(manager, slot::kGetByCityCells, &list, lowArg, highArg, noFilter,
                                      turn % 7 == 0 ? 2u : 500u));
            MixList(hash, manager, &list);
            hash.Mix(Slot<uint8_t>(manager, slot::kGetByCityCells + 4, &list, lowArg, highArg, type, 500u));
            MixList(hash, manager, &list);
            Walk walk{&hash, turn % 4 == 0 ? 3 : 300};
            hash.Mix(Slot<uint8_t>(manager, slot::kIterateByCityCells, static_cast<CallbackFn>(&OnOccupant),
                                      static_cast<void*>(&walk), lowArg, highArg, noFilter));
            walk.left = 300;
            hash.Mix(Slot<uint8_t>(manager, slot::kIterateByCityCellsByType, static_cast<CallbackFn>(&OnOccupant),
                                      static_cast<void*>(&walk), lowArg, highArg, type));
        }

    // World cells: single cells and rectangles, the grid's edges and
    // beyond them (clamped by the slots), and the city by default.
    turn = 0;
    for (int i = -1; i <= 5; ++i)
        for (int j = -1; j <= 5; ++j, ++turn) {
            const int32_t x = i < 0 ? -3 : i == 5 ? 200 : 64 + cellsX * i / 5;
            const int32_t z = j < 0 ? 62 : j == 5 ? 191 : 64 + cellsZ * j / 5;
            const uint32_t type = types[turn % 3];
            const int32_t low[2] = {x, z};
            const int32_t high[2] = {x + (turn % 3 == 0 ? 0 : 2), z + (turn % 4 == 0 ? 0 : 1)};
            const int32_t* const lowArg = turn % 11 == 10 ? nullptr : low;
            const int32_t* const highArg = turn % 13 == 12 ? nullptr : high;
            hash.Mix(Slot<uint8_t>(manager, slot::kGetByCells, &list, lowArg, highArg, noFilter,
                                      turn % 7 == 0 ? 4u : 400u));
            MixList(hash, manager, &list);
            hash.Mix(Slot<uint8_t>(manager, slot::kGetByCells + 4, &list, lowArg, highArg, type, 400u));
            MixList(hash, manager, &list);
            const int32_t lowTriple[3] = {low[0], 0x55, low[1]};
            const int32_t highTriple[3] = {high[0], 0x66, high[1]};
            Walk walk{&hash, turn % 4 == 0 ? 3 : 300};
            hash.Mix(Slot<uint8_t>(manager, slot::kIterate, static_cast<CallbackFn>(&OnOccupant),
                                      static_cast<void*>(&walk), lowArg ? lowTriple : nullptr,
                                      highArg ? highTriple : nullptr, noFilter));
            walk.left = 300;
            hash.Mix(Slot<uint8_t>(manager, slot::kIterate + 4, static_cast<CallbackFn>(&OnOccupant),
                                      static_cast<void*>(&walk), lowArg ? lowTriple : nullptr,
                                      highArg ? highTriple : nullptr, type));
        }

    Virtual<void>(ListNodeAllocator(), 0x10, static_cast<void*>(list));
    return hash.h;
}

// The occupant manager (build 641; src/core/occmgr_sim.h), found through
// the city's slot 34 (the getter of +0x12C; the same object as the global
// 0x00B43D0C): its reference count, the initialized and loaded flags, the
// city's size in 64 m and in 16 m cells, the grid's columns and the cells
// of its first column, the number of cells with a list, of entries and of
// occupants (an occupant of several cells counted at its first), and an
// order-sensitive checksum of the whole index as stored: every cell with a
// list in grid order (x then z), its entries front to back, each with its
// one-cell bit, the occupant's type, its recorded cells and its position
// in x and z. No addresses. The walk is under the manager's lock.
void Fingerprint(cISC4City* city, std::string& line)
{
    if (!game::IsBuild641()) {
        line += " | occmgr none";
        return;
    }
    char* const manager = Virtual<char*>(city, 0x88);
    if (!manager || !At<uint8_t>(manager, field::kInitialized)) {
        line += " | occmgr none";
        return;
    }
    Virtual<void>(manager + field::kLock, 0x4);
    const Vector* const grid = Grid(manager);
    const int32_t columns = static_cast<int32_t>((grid->end - grid->begin) / 12);
    int32_t rows = 0;
    Hash index;
    uint32_t lists = 0, entries = 0, occupants = 0, automata = 0;
    // OPENSC4_TEST_OCCMGR_TYPES=1: the entries per occupant type, logged
    // with each fingerprint, to see which types differ between two runs.
    const bool byType = EnvInt("OPENSC4_TEST_OCCMGR_TYPES", 0) != 0;
    std::map<uint32_t, uint32_t> types, typeSums;
    for (int32_t x = 0; x < columns; ++x) {
        const Vector* const column = reinterpret_cast<const Vector*>(grid->begin + x * 12);
        const int32_t depth = static_cast<int32_t>((column->end - column->begin) >> 2);
        if (x == 0)
            rows = depth;
        for (int32_t z = 0; z < depth; ++z) {
            const Cell* const cell = CellAt(manager, x, z);
            if (!cell)
                continue;
            bool counted = false;
            for (const ListNode* node = cell->head->next; node != cell->head; node = node->next) {
                void* const object = OccupantOf(node);
                const uint32_t type = Virtual<uint32_t>(object, occupant::kGetType);
                if (byType)
                    ++types[type];
                // Automata (vehicles, pedestrians) move by frames, not game
                // days: their number differs between two runs of the
                // original. Counted apart, not hashed.
                if (type == kTypeAutomaton || type == kTypeAutomaton2) {
                    ++automata;
                    continue;
                }
                if (!counted) {
                    counted = true;
                    ++lists;
                    index.Mix(static_cast<uint32_t>(x) << 16 | static_cast<uint32_t>(z));
                }
                ++entries;
                index.Mix(static_cast<uint32_t>(node->value & 1));
                MixOccupant(index, object);
                if (byType) {
                    Hash one;
                    MixOccupant(one, object);
                    typeSums[type] += one.h;
                }
                uint8_t rect[4] = {0xFF, 0xFF, 0xFF, 0xFF};
                const bool recorded = Virtual<uint8_t>(object, occupant::kGetCells, static_cast<uint8_t*>(rect)) != 0;
                if ((node->value & 1) || !recorded || (rect[0] == x && rect[1] == z))
                    ++occupants;
            }
        }
    }
    Virtual<void>(manager + field::kLock, 0x8);
    if (byType)
        log::Write("occmgr types: automata %u", automata);
    for (const auto& [type, count] : types)
        log::Write("occmgr types: %08X %u sum %08X", type, count, typeSums[type]);
    char text[200];
    std::snprintf(text, sizeof text, " | occmgr %u,%u%u,%dx%d,%dx%d,%dx%d,%u,%u,%u,%08X",
                  At<uint32_t>(manager, field::kRefCount), At<uint8_t>(manager, field::kInitialized),
                  At<uint8_t>(manager, field::kLoaded), At<int32_t>(manager, field::kCellsX),
                  At<int32_t>(manager, field::kCellsZ), At<int32_t>(manager, field::kCityCellsX),
                  At<int32_t>(manager, field::kCityCellsZ), columns, rows, lists, entries, occupants, index.h);
    line += text;
    // On a grid other than 192 (big cities) the game's own functions answer
    // for 192 by design, so the comparison is left out.
    if (EnvInt("OPENSC4_TEST_OCCMGR", 0) >= 1 && occmgr_sim::WorldCells(manager) == kWorldCells) {
        // The answers differ between two runs of the original (automata are
        // among them), so the checksum itself is not printed: the same
        // questions go to the manager's slots and to the game's own
        // functions, one after the other on the same index, and the two
        // checksums must agree.
        g_gameSlots = false;
        const uint32_t ours = Queries(manager);
        g_gameSlots = true;
        const uint32_t games = Queries(manager);
        g_gameSlots = false;
        static uint32_t asked = 0, differed = 0;
        ++asked;
        if (ours != games && differed++ < 5)
            log::Write("occmgr: queries differ from the game's (%08X, the game's %08X)", ours, games);
        char text2[48];
        std::snprintf(text2, sizeof text2, ",q%u/%u", asked - differed, asked);
        line += text2;
    }
}

const FingerprintPart kFingerprint{"occmgr", 104, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
