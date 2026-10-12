// The prop manager's part of the autotest (src/core/autotest_hooks.h;
// propmgr_1.cpp, propmgr_2.cpp): a switch that calls the game's
// cSC4PropManager through its vtable (and the one non-virtual helper,
// SetUsage, by address), so that a run with simulators.propmgr.* on and one
// with it off can be compared. Nothing here calls our own functions.
//
//   OPENSC4_TEST_PROPMGR=<day>      token "propmgr": the read-only tier, one
//     sub-hash per part on the log lines "autotest: propmgr: ...".
//   OPENSC4_TEST_PROPMGR_EDIT=1     with it, the round-trip tier (7 to 9):
//     props are made, added and taken away again, the city is left as found.
//   OPENSC4_TEST_PROPMGR_EDIT=2     the same and 10, the building's round
//     trips. Those are separate because the game's own AddBuildingProp and
//     RemoveBuildingProp leave the flammability grid's checksum changed (the
//     original does it too; the sums of the grid are the same): the daily
//     fingerprint is no longer that of a run without the test.
//   OPENSC4_TEST_PROPMGR_EDITPARTS=<mask>   which of the round trips 7 to 10
//     run (bit 0 to 3; default 7, with EDIT=2 15), for finding which one
//     disturbs something; bits 4 to 6 pick the three rounds of 10 (none set:
//     all).
//   OPENSC4_TEST_PROPMGR_VERBOSE=1  one log line per lot and per lot prop.
//
// Read-only tier:
//   1. the object: QueryInterface with the IDs it takes and two it does not
//      (which part came back, as an offset from the object), AddRef and
//      Release as changes of the count, the documented fields, the filter's
//      mode as found;
//   2. the prop types (GetAllPropTypes, sorted), and for every type, or a
//      fixed stride of them: IsPropTypeValid, GetPropKeyFromType, the
//      profile through GetPropProfile and GetPropProfilePtr (the three high
//      bytes of the wealth dword are stack garbage in the original and are
//      masked), the name record of a sample (GetPropName), and a few IDs
//      that are not types (never given to the two getters that insert);
//   3. the families (GetAllPropFamilyTypes, sorted), GetPropTypesByFamily,
//      IsPropInFamily, IsPropFamilyTypeValid, over a stride of the families;
//   4. the city queries into fresh lists: GetCityPropsInRectangle (cells) and
//      GetCityPropsInArea (metres) over three rectangles, GetStreetPropsIn-
//      Rectangle over the same, GetStreetProps for a sample of cells; each
//      occupant is read (its type, prop type, usage and position, sorted) and
//      released, the list destroyed as the game's callers do;
//   5. GetLotPropsAt and GetLotPropsOfLot for a sample of lots;
//   6. GetBuildingPropsAt and GetBuildingPropsOf for the buildings found.
//
// Round-trip tier (the city is left as found; the counts of props of every
// usage over the whole city and the counts in the test rectangles are taken
// before and after and must be equal):
//   7. CreateProp and CreateGenericProp for valid types, read and released
//      without being inserted; SetUsage on one of them;
//   8. at an empty spot (no lot, no city prop within a cell): AddCityPropNew
//      and RemoveCityPropsInRectangle, then CreateProp, AddCityProp and
//      RemoveCityPropsInArea;
//   9. on a lot that has no props: AddLotPropNew and RemoveLotPropsAt, then
//      CreateProp, AddLotProp and RemoveLotPropsOfLot;
//  10. on a building that has no props: CreateProp, AddBuildingProp and
//      RemoveBuildingProp; again with RemoveBuildingPropsOf; again with
//      RemoveBuildingPropsAt.
//
// What is left out, on purpose: Init, Shutdown, Release beyond what was
// added, Write, Read, the deleting destructor, DoMessage, the exemplar
// loader functions, the clean-up, and the two getters that insert a record
// for an unknown type (they are only given types the table returned).
// Every pointer is left out of the hashes; every bool is masked to its low
// byte.
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <cISC4City.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <tuple>
#include <vector>

namespace opensc4::autotest {
namespace {

struct Fnv {
    uint32_t h = 2166136261u;
    void Bytes(const void* data, std::size_t size)
    {
        for (std::size_t i = 0; i < size; ++i)
            h = (h ^ static_cast<const uint8_t*>(data)[i]) * 16777619u;
    }
    template <typename T>
    void Add(const T& value)
    {
        Bytes(&value, sizeof value);
    }
    // A mark for something that is not there, so that a missing object
    // changes the hash instead of stopping the run.
    void Missing() { Add(uint8_t{0xEE}); }
};

template <typename R, typename... Args>
R This(std::uintptr_t function, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(function)(self, args...);
}

char g_token[40];
uint32_t g_moving = 0; // props of prop type 0 met in the lists, left out of the hashes

// The prop manager's slots (vtable 0x00A8F928; the argument counts are the
// callees' `ret N`, listed by analysis/va-tools/vt.py).
namespace slot {
constexpr std::size_t kQueryInterface = 0x00;
constexpr std::size_t kAddRef = 0x04;
constexpr std::size_t kRelease = 0x08;
constexpr std::size_t kGetAllPropTypes = 0x14;       // (vector)
constexpr std::size_t kGetAllPropFamilyTypes = 0x18; // (vector)
constexpr std::size_t kGetPropKeyFromType = 0x1C;    // (type, key of 3)
constexpr std::size_t kIsPropTypeValid = 0x20;       // (type)
constexpr std::size_t kGetPropTypesByFamily = 0x24;  // (vector, family)
constexpr std::size_t kIsPropInFamily = 0x28;        // (type, family)
constexpr std::size_t kIsPropFamilyTypeValid = 0x2C; // (id)
constexpr std::size_t kGetPropName = 0x30;           // (type)
constexpr std::size_t kGetPropProfile = 0x34;        // (type)
constexpr std::size_t kGetPropProfilePtr = 0x38;     // (type)
constexpr std::size_t kCreateProp = 0x3C;            // (type, out)
constexpr std::size_t kCreateGenericProp = 0x40;     // (key, out)
constexpr std::size_t kCityPropsInArea = 0x44;       // (list, 4 floats)
constexpr std::size_t kCityPropsInRectangle = 0x48;  // (list, 4 cells)
constexpr std::size_t kAddCityProp = 0x4C;           // (occupant)
constexpr std::size_t kAddCityPropNew = 0x50;        // (type, position, orientation)
constexpr std::size_t kRemoveCityPropsInRectangle = 0x54; // (4 floats)
constexpr std::size_t kRemoveCityPropsInArea = 0x58;      // (4 cells)
constexpr std::size_t kGetLotPropsAt = 0x5C;         // (list, x, z)
constexpr std::size_t kGetLotPropsOfLot = 0x60;      // (list, lot)
constexpr std::size_t kAddLotProp = 0x64;            // (lot, occupant)
constexpr std::size_t kAddLotPropNew = 0x68;         // (type, position, orientation)
constexpr std::size_t kRemoveLotPropsAt = 0x6C;      // (x, z)
constexpr std::size_t kRemoveLotPropsOfLot = 0x70;   // (lot)
constexpr std::size_t kGetStreetPropsInRectangle = 0x78; // (list, 4 cells)
constexpr std::size_t kGetStreetProps = 0x7C;        // (list, x, z)
constexpr std::size_t kGetBuildingPropsAt = 0x88;    // (list, x, z)
constexpr std::size_t kGetBuildingPropsOf = 0x8C;    // (list, building)
constexpr std::size_t kAddBuildingProp = 0x90;       // (building, occupant)
constexpr std::size_t kRemoveBuildingPropsAt = 0x94; // (x, z)
constexpr std::size_t kRemoveBuildingPropsOf = 0x98; // (building)
constexpr std::size_t kRemoveBuildingProp = 0x9C;    // (building, occupant)
} // namespace slot

constexpr uint32_t kIidManager = 0x28326F3E;
constexpr uint32_t kIidProp = 0xE9793A65;
constexpr uint32_t kBuildingType = 0x278128A0;
constexpr uint32_t kClassDefault = 0x2977AA47;
constexpr std::size_t kFilterField = 0x1C;
constexpr std::size_t kFilterMode = 0xC;
constexpr uint32_t kMostTypes = 2500;
constexpr uint32_t kMostFamilies = 400;
constexpr uint32_t kMostNames = 40;
constexpr std::size_t kMostStreetCells = 300;
constexpr std::size_t kMostLots = 200;
constexpr std::size_t kMostBuildings = 800;

// --- Helpers --------------------------------------------------------------------------

// A vector of uint32 as the game's: begin, end, capacity. The game grows it
// and the game's destructor frees it.
struct Vec {
    uint32_t* begin = nullptr;
    uint32_t* end = nullptr;
    uint32_t* capacity = nullptr;
};

std::vector<uint32_t> Contents(const Vec& v)
{
    std::vector<uint32_t> out;
    for (const uint32_t* p = v.begin; p && p != v.end && out.size() < 1000000; ++p)
        out.push_back(*p);
    return out;
}

void DestroyVec(Vec& v)
{
    This<void>(addr::SC4OccupantVector_Destroy, &v);
    v = Vec{};
}

uint32_t Hash(const std::vector<uint32_t>& values)
{
    Fnv f;
    f.Add(static_cast<uint32_t>(values.size()));
    for (const uint32_t value : values)
        f.Add(value);
    return f.h;
}

// A list of the game's (a head node: next, previous, then nodes with the
// value at +8), made from the list node allocator as the game's callers do
// (propmgr_2.cpp's NewList) and destroyed with the game's destroyer.
char* NewList()
{
    char* const head = static_cast<char*>(Virtual<void*>(ListNodeAllocator(), 0xC, uint32_t{0xC}));
    At<char*>(head, 0x0) = head;
    At<char*>(head, 0x4) = head;
    return head;
}

struct PropInfo {
    uint32_t occupantType;
    uint32_t propType;
    uint32_t usage;
    uint32_t position[3]; // the floats' bits
    // The height is left out of the order and the hashes (no prop of a type
    // in the table changes it; see ReadAndFree for the others).
    bool operator<(const PropInfo& other) const
    {
        return std::tie(occupantType, propType, usage, position[0], position[2], position[1]) <
               std::tie(other.occupantType, other.propType, other.usage, other.position[0], other.position[2],
                        other.position[1]);
    }
    void AddTo(Fnv& f) const
    {
        f.Add(occupantType);
        f.Add(propType);
        f.Add(usage);
        f.Add(position[0]);
        f.Add(position[2]);
    }
};

// One occupant of a query's list, read.
PropInfo ReadProp(void* occupant)
{
    PropInfo info{};
    info.occupantType = Virtual<uint32_t>(occupant, 0x1C);
    void* prop = nullptr;
    if (Virtual<uint8_t>(occupant, 0x0, kIidProp, &prop) && prop) {
        info.propType = Virtual<uint32_t>(prop, 0x1C);
        info.usage = Virtual<uint8_t>(prop, 0x58);
        Virtual<uint32_t>(prop, 0x8);
    } else {
        info.propType = 0xEEEEEEEEu;
        info.usage = 0xEEu;
    }
    float position[3] = {0, 0, 0};
    Virtual<void>(occupant, 0x20, position);
    std::memcpy(info.position, position, sizeof position);
    return info;
}

struct ListResult {
    uint32_t count = 0;
    uint32_t sorted = 0;  // the hash of the sorted infos
    uint32_t ordered = 0; // the hash in the list's order (the same in two runs of the original)
    uint8_t answer = 0;
    std::vector<PropInfo> infos; // sorted
};

// Reads every occupant of a list, releases the reference each holds and
// destroys the list.
void ReadAndFree(char* head, ListResult& result)
{
    Fnv ordered;
    for (char* node = At<char*>(head, 0x0); node != head && result.infos.size() < 1000000; node = At<char*>(node, 0x0)) {
        void* const occupant = At<void*>(node, 0x8);
        if (!occupant)
            continue;
        const PropInfo info = ReadProp(occupant);
        Virtual<uint32_t>(occupant, 0x8);
        // Props of prop type 0 (none of the table's) move from run to run in
        // the original, x and z too: a lot's count changes as they cross its
        // edge. They are counted for the log and left out of the hashes.
        if (info.propType == 0) {
            ++g_moving;
            continue;
        }
        result.infos.push_back(info);
        info.AddTo(ordered);
    }
    This<void>(addr::SC4List_Destroy2, &head);
    std::sort(result.infos.begin(), result.infos.end());
    Fnv sorted;
    sorted.Add(static_cast<uint32_t>(result.infos.size()));
    for (const PropInfo& info : result.infos)
        info.AddTo(sorted);
    result.count = static_cast<uint32_t>(result.infos.size());
    result.sorted = sorted.h;
    result.ordered = ordered.h;
}

// A manager query that fills a list: the list is made, the slot called with
// it, and the result read.
template <typename... Args>
ListResult Query(void* manager, std::size_t slotOffset, Args... args)
{
    ListResult result;
    char* head = NewList();
    result.answer = Virtual<uint8_t>(manager, slotOffset, static_cast<void*>(&head), args...);
    ReadAndFree(head, result);
    return result;
}

uint32_t FilterMode(void* manager)
{
    void* const filter = At<void*>(manager, kFilterField);
    return filter ? At<uint32_t>(filter, kFilterMode) : 0xEEEEEEEEu;
}

void AddResult(Fnv& f, const ListResult& r, uint32_t mode)
{
    f.Add(r.answer);
    f.Add(r.count);
    f.Add(r.sorted);
    f.Add(r.ordered);
    f.Add(mode);
}

struct Rect {
    int32_t x0, z0, x1, z1;
};

// What the walk over the city gathers.
struct Sample {
    std::vector<void*> held;      // references to release at the end
    std::vector<void*> buildings; // in walk order
    std::vector<Rect> buildingCell; // the cell each was found at (x0, z0)
    std::vector<void*> lots;
    std::vector<Rect> lotRect;
    std::vector<Rect> streetCells; // x0, z0
    std::vector<Rect> rects;       // the test rectangles, in cells
};

void Collect(Sample& out, void* manager, int32_t sizeX, int32_t sizeZ)
{
    void* const occupants = At<void*>(manager, 0x14);
    void* const lotManager = At<void*>(manager, 0x20);
    if (!occupants || !lotManager || sizeX <= 0 || sizeZ <= 0)
        return;
    out.rects = {{sizeX / 4, sizeZ / 4, sizeX * 3 / 4 - 1, sizeZ * 3 / 4 - 1},
                 {0, 0, 3, sizeZ - 1},
                 {sizeX / 2 - 2, sizeZ / 2 - 2, sizeX / 2 + 5, sizeZ / 2 + 5}};
    for (int32_t z = 0; z < sizeZ; ++z) {
        for (int32_t x = 0; x < sizeX; ++x) {
            if (out.lots.size() < kMostLots) {
                if (void* const lot = Virtual<void*>(lotManager, 0x18, x, z, uint32_t{0})) {
                    if (std::find(out.lots.begin(), out.lots.end(), lot) == out.lots.end()) {
                        int32_t bounds[4] = {0, 0, 0, 0};
                        Virtual<void>(lot, 0x38, bounds);
                        out.lots.push_back(lot);
                        out.lotRect.push_back({bounds[0], bounds[1], bounds[2], bounds[3]});
                    }
                }
            }
            void* found = nullptr;
            if (Virtual<uint8_t>(occupants, 0x84, &found, x, z, kBuildingType) && found) {
                out.held.push_back(found);
                if (out.buildings.size() < kMostBuildings &&
                    std::find(out.buildings.begin(), out.buildings.end(), found) == out.buildings.end()) {
                    out.buildings.push_back(found);
                    out.buildingCell.push_back({x, z, x, z});
                }
            }
            if (x % 8 == 0 && z % 8 == 0 && out.streetCells.size() < kMostStreetCells)
                out.streetCells.push_back({x, z, x, z});
        }
    }
}

// --- 1. The object ---------------------------------------------------------------------

void DriveObject(Fnv& f, void* m, void* city, uint32_t* modeFound)
{
    if (!m) {
        f.Missing();
        return;
    }
    // QueryInterface: the IDs it takes and two it does not (the prop's ID and
    // an unknown one).
    for (const uint32_t iid : {0x00000001u, kIidManager, 0x452294AAu, 0xE4FDA3D4u, kIidProp, 0x12345678u}) {
        void* out = nullptr;
        const uint32_t before = At<uint32_t>(m, 0x10);
        const uint8_t has = Virtual<uint8_t>(m, slot::kQueryInterface, iid, &out);
        f.Add(iid);
        f.Add(has);
        f.Add(out != nullptr);
        if (out)
            f.Add(static_cast<int32_t>(static_cast<char*>(out) - static_cast<char*>(m)));
        f.Add(At<uint32_t>(m, 0x10) - before);
        if (has)
            Virtual<uint32_t>(m, slot::kRelease); // the reference the answer took
        f.Add(At<uint32_t>(m, 0x10) - before);
    }
    // AddRef and Release, on a count above one (a Release at one shuts down).
    const uint32_t before = At<uint32_t>(m, 0x10);
    f.Add(before > 0);
    Virtual<uint32_t>(m, slot::kAddRef);
    f.Add(At<uint32_t>(m, 0x10) - before);
    Virtual<uint32_t>(m, slot::kRelease);
    f.Add(At<uint32_t>(m, 0x10) - before);
    // The fields.
    f.Add(At<uint8_t>(m, 0x0C));
    f.Add(At<void*>(m, 0x14) == Global<void*>(addr::SC4_NetworkService));
    f.Add(At<void*>(m, 0x18) == Global<void*>(addr::SC4_ResourceManager));
    f.Add(At<void*>(m, 0x20) == Global<void*>(addr::SC4_LotManager));
    for (const std::size_t at : {std::size_t{0x14}, std::size_t{0x18}, std::size_t{0x1C}, std::size_t{0x20},
                                 std::size_t{0x24}})
        f.Add(At<void*>(m, at) != nullptr);
    *modeFound = FilterMode(m);
    f.Add(*modeFound);
    f.Add(city != nullptr);
}

// --- 2. The prop types -------------------------------------------------------------------

struct Types {
    std::vector<uint32_t> sorted;
};

// The name record's content: the string's text when it is one, else what is
// there (the first dword when it is in the program's own image).
void AddName(Fnv& f, void* record, int* logged, uint32_t type)
{
    if (!record) {
        f.Missing();
        return;
    }
    const uint32_t first = At<uint32_t>(record, 0);
    if (*logged < 4) {
        ++*logged;
        log::Write("autotest: propmgr: name record of %08X: %08X %08X %08X %08X %08X", type, first,
                   At<uint32_t>(record, 4), At<uint32_t>(record, 8), At<uint32_t>(record, 12), At<uint32_t>(record, 16));
    }
    // The record is a string of the game's (vtable 0x00A80810: the text's
    // first byte at +4, the end, one past its terminator, at +0xC).
    const bool isString = first == 0x00A80810u;
    f.Add(isString);
    if (isString) {
        const char* const text = At<const char*>(record, 4);
        const char* const end = At<const char*>(record, 0xC);
        if (!text || end <= text || end - text > 65) {
            f.Missing();
            return;
        }
        const std::size_t length = static_cast<std::size_t>(end - text) - 1;
        f.Add(static_cast<uint32_t>(length));
        f.Bytes(text, length);
    } else {
        f.Add(first >= 0x00400000u && first < 0x01000000u ? first : 0xFFFFFFFFu);
    }
}

void DriveTypes(Fnv& f, void* m, Types& types, int& calls, uint32_t* subs)
{
    if (!m) {
        f.Missing();
        return;
    }
    Vec all;
    Virtual<void>(m, slot::kGetAllPropTypes, &all);
    types.sorted = Contents(all);
    DestroyVec(all);
    const uint32_t unsortedHash = Hash(types.sorted);
    std::sort(types.sorted.begin(), types.sorted.end());
    Fnv list;
    list.Add(Hash(types.sorted));
    list.Add(unsortedHash);
    const uint32_t count = static_cast<uint32_t>(types.sorted.size());
    // A second call must give the same list (the vector is emptied first).
    Vec again;
    Virtual<void>(m, slot::kGetAllPropTypes, &again);
    Virtual<void>(m, slot::kGetAllPropTypes, &again);
    list.Add(static_cast<uint32_t>(Contents(again).size()));
    DestroyVec(again);
    subs[0] = list.h;
    f.Add(list.h);
    calls += 3;
    log::Write("autotest: propmgr: types %08X (%u types, unsorted %08X)", list.h, count, unsortedHash);

    const uint32_t stride = count / kMostTypes + 1;
    Fnv per;
    int namesLogged = 0;
    uint32_t sampled = 0;
    uint32_t valid = 0;
    uint32_t classCounts[4] = {0, 0, 0, 0};
    for (std::size_t i = 0; i < types.sorted.size(); i += stride) {
        const uint32_t type = types.sorted[i];
        ++sampled;
        per.Add(type);
        const uint32_t ok = Virtual<uint32_t>(m, slot::kIsPropTypeValid, type) & 0xFF;
        per.Add(ok);
        valid += ok != 0;
        uint32_t key[3] = {0x77777777u, 0x77777777u, 0x77777777u};
        const uint8_t hasKey = Virtual<uint8_t>(m, slot::kGetPropKeyFromType, type, key);
        per.Add(hasKey);
        per.Add(key);
        uint32_t profile[7] = {};
        uint32_t viaPointer[7] = {};
        if (const uint32_t* const p = Virtual<const uint32_t*>(m, slot::kGetPropProfile, type))
            std::memcpy(profile, p, sizeof profile);
        const uint32_t* const q = Virtual<const uint32_t*>(m, slot::kGetPropProfilePtr, type);
        if (q)
            std::memcpy(viaPointer, q, sizeof viaPointer);
        // The wealth dword has stack garbage in its three high bytes.
        profile[5] &= 0xFF;
        viaPointer[5] &= 0xFF;
        per.Add(profile);
        per.Add(q != nullptr);
        per.Add(std::memcmp(profile, viaPointer, sizeof profile) == 0);
        const uint32_t kind = profile[0] - kClassDefault;
        ++classCounts[kind < 3 ? kind : 3];
        calls += 4;
        if ((i / stride) % (count / stride / kMostNames + 1) == 0) {
            AddName(per, Virtual<void*>(m, slot::kGetPropName, type), &namesLogged, type);
            ++calls;
        }
    }
    subs[1] = per.h;
    f.Add(per.h);
    log::Write("autotest: propmgr: per type %08X (%u sampled, %u valid; classes plain %u decal %u effect %u other %u)",
               per.h, sampled, valid, classCounts[0], classCounts[1], classCounts[2], classCounts[3]);

    // IDs that are not types (the getters that insert are not given them).
    Fnv others;
    for (const uint32_t id : {0u, 1u, 0xDEADBEEFu, 0x12345678u, 0xFFFFFFFFu, 0x2977AA47u}) {
        others.Add(id);
        others.Add(static_cast<uint8_t>(Virtual<uint32_t>(m, slot::kIsPropTypeValid, id) & 0xFF));
        others.Add(static_cast<uint8_t>(Virtual<uint32_t>(m, slot::kIsPropFamilyTypeValid, id) & 0xFF));
        uint32_t key[3] = {0x77777777u, 0x77777777u, 0x77777777u};
        others.Add(Virtual<uint8_t>(m, slot::kGetPropKeyFromType, id, key));
        others.Add(key);
        calls += 3;
    }
    subs[2] = others.h;
    f.Add(others.h);
}

// --- 3. The families ---------------------------------------------------------------------

void DriveFamilies(Fnv& f, void* m, const Types& types, int& calls, uint32_t* subs)
{
    if (!m) {
        f.Missing();
        return;
    }
    Vec all;
    Virtual<void>(m, slot::kGetAllPropFamilyTypes, &all);
    std::vector<uint32_t> families = Contents(all);
    DestroyVec(all);
    const uint32_t unsortedHash = Hash(families);
    std::sort(families.begin(), families.end());
    Fnv list;
    list.Add(Hash(families));
    list.Add(unsortedHash);
    ++calls;
    subs[0] = list.h;
    f.Add(list.h);
    log::Write("autotest: propmgr: families %08X (%zu families, unsorted %08X)", list.h, families.size(), unsortedHash);

    const uint32_t stride = static_cast<uint32_t>(families.size()) / kMostFamilies + 1;
    Fnv per;
    Fnv ordered;
    uint32_t sampled = 0;
    uint32_t members = 0;
    for (std::size_t i = 0; i < families.size(); i += stride) {
        const uint32_t family = families[i];
        ++sampled;
        per.Add(family);
        per.Add(static_cast<uint8_t>(Virtual<uint32_t>(m, slot::kIsPropFamilyTypeValid, family) & 0xFF));
        Vec vec;
        const uint32_t count = Virtual<uint32_t>(m, slot::kGetPropTypesByFamily, &vec, family);
        std::vector<uint32_t> in = Contents(vec);
        DestroyVec(vec);
        ordered.Add(Hash(in));
        per.Add(Hash(in));
        std::sort(in.begin(), in.end());
        per.Add(count);
        per.Add(Hash(in));
        members += static_cast<uint32_t>(in.size());
        calls += 3;
        // Members (the first three and the last), non-members, the family itself.
        std::vector<uint32_t> probe;
        for (std::size_t k = 0; k < in.size() && k < 3; ++k)
            probe.push_back(in[k]);
        if (!in.empty())
            probe.push_back(in.back());
        for (const uint32_t type : probe) {
            per.Add(Virtual<uint8_t>(m, slot::kIsPropInFamily, type, family));
            ++calls;
        }
        per.Add(Virtual<uint8_t>(m, slot::kIsPropInFamily, family, family));
        per.Add(Virtual<uint8_t>(m, slot::kIsPropInFamily, uint32_t{0xDEADBEEF}, family));
        // Some types that are not in it (the next types in the sorted table).
        for (std::size_t k = 0; k < 3 && !types.sorted.empty(); ++k) {
            const uint32_t type = types.sorted[(i * 7 + k * 13) % types.sorted.size()];
            per.Add(type);
            per.Add(Virtual<uint8_t>(m, slot::kIsPropInFamily, type, family));
            ++calls;
        }
        calls += 3;
    }
    // A type that is no family gives the type alone and 0; an unknown ID leaves the vector alone.
    for (std::size_t k = 0; k < 3 && k < types.sorted.size(); ++k) {
        const uint32_t type = types.sorted[k * types.sorted.size() / 3];
        Vec vec;
        const uint32_t count = Virtual<uint32_t>(m, slot::kGetPropTypesByFamily, &vec, type);
        per.Add(type);
        per.Add(count);
        per.Add(Hash(Contents(vec)));
        DestroyVec(vec);
        ++calls;
    }
    {
        Vec vec;
        const uint32_t count = Virtual<uint32_t>(m, slot::kGetPropTypesByFamily, &vec, uint32_t{0xDEADBEEF});
        per.Add(count);
        per.Add(static_cast<uint32_t>(Contents(vec).size()));
        DestroyVec(vec);
        ++calls;
    }
    subs[1] = per.h;
    f.Add(per.h);
    log::Write("autotest: propmgr: per family %08X (%u sampled, %u members, in order %08X)", per.h, sampled, members,
               ordered.h);
}

// --- 4 to 6. The city queries ------------------------------------------------------------

void FloatRect(void* city, const Rect& r, float* out)
{
    // Metres: the first cell's corner to the last cell's far corner.
    Virtual<void>(city, 0x180, r.x0, r.z0, &out[0], &out[1]);
    Virtual<void>(city, 0x180, r.x1 + 1, r.z1 + 1, &out[2], &out[3]);
}

void DriveCity(Fnv& f, void* m, void* city, const Sample& sample, int& calls, uint32_t* subs, uint32_t* totals)
{
    if (!m || !city) {
        f.Missing();
        return;
    }
    Fnv rects;
    Fnv streets;
    uint32_t ordered[3] = {0, 0, 0};
    for (const Rect& r : sample.rects) {
        const int32_t cells[4] = {r.x0, r.z0, r.x1, r.z1};
        float metres[4];
        FloatRect(city, r, metres);
        const ListResult a = Query(m, slot::kCityPropsInRectangle, static_cast<const int32_t*>(cells));
        AddResult(rects, a, FilterMode(m));
        const ListResult b = Query(m, slot::kCityPropsInArea, static_cast<const float*>(metres));
        AddResult(rects, b, FilterMode(m));
        const ListResult c = Query(m, slot::kGetStreetPropsInRectangle, static_cast<const int32_t*>(cells));
        AddResult(streets, c, FilterMode(m));
        calls += 3;
        totals[0] += a.count;
        totals[1] += b.count;
        totals[2] += c.count;
        ordered[0] ^= a.ordered;
        ordered[1] ^= b.ordered;
        ordered[2] ^= c.ordered;
        log::Write("autotest: propmgr: rectangle %d,%d,%d,%d: city cells %u (%08X), city metres %u (%08X), street %u (%08X)",
                   r.x0, r.z0, r.x1, r.z1, a.count, a.sorted, b.count, b.sorted, c.count, c.sorted);
    }
    subs[0] = rects.h;
    f.Add(rects.h);
    uint32_t cellProps = 0;
    for (const Rect& cell : sample.streetCells) {
        const ListResult r = Query(m, slot::kGetStreetProps, cell.x0, cell.z0);
        AddResult(streets, r, FilterMode(m));
        cellProps += r.count;
        ++calls;
    }
    subs[1] = streets.h;
    f.Add(streets.h);
    totals[3] = cellProps;
    log::Write("autotest: propmgr: city %08X street %08X (%zu street cells, %u props; in order xor %08X %08X %08X)",
               rects.h, streets.h, sample.streetCells.size(), cellProps, ordered[0], ordered[1], ordered[2]);
}

void DriveLots(Fnv& f, void* m, const Sample& sample, int& calls, uint32_t* total)
{
    if (!m) {
        f.Missing();
        return;
    }
    Fnv per;
    const bool verbose = EnvInt("OPENSC4_TEST_PROPMGR_VERBOSE", 0) != 0;
    for (std::size_t i = 0; i < sample.lots.size(); ++i) {
        const ListResult of = Query(m, slot::kGetLotPropsOfLot, sample.lots[i]);
        AddResult(per, of, FilterMode(m));
        const ListResult at = Query(m, slot::kGetLotPropsAt, sample.lotRect[i].x0, sample.lotRect[i].z0);
        AddResult(per, at, FilterMode(m));
        if (verbose) {
            log::Write("autotest: propmgr: lot %zu rect %d,%d,%d,%d: of %u (%08X) at %u (%08X)", i, sample.lotRect[i].x0,
                       sample.lotRect[i].z0, sample.lotRect[i].x1, sample.lotRect[i].z1, of.count, of.sorted, at.count,
                       at.sorted);
            for (const PropInfo& info : of.infos)
                log::Write("autotest: propmgr:   lot %zu prop %08X %08X %u at %08X %08X %08X", i, info.occupantType,
                           info.propType, info.usage, info.position[0], info.position[1], info.position[2]);
        }
        calls += 2;
        *total += of.count + at.count;
    }
    // Without a lot.
    const ListResult none = Query(m, slot::kGetLotPropsOfLot, static_cast<void*>(nullptr));
    AddResult(per, none, FilterMode(m));
    ++calls;
    f.Add(per.h);
    log::Write("autotest: propmgr: lots %08X (%zu lots, %u props)", per.h, sample.lots.size(), *total);
}

void DriveBuildings(Fnv& f, void* m, const Sample& sample, int& calls, uint32_t* total, uint32_t* withProps)
{
    if (!m) {
        f.Missing();
        return;
    }
    Fnv per;
    for (std::size_t i = 0; i < sample.buildings.size(); ++i) {
        const ListResult of = Query(m, slot::kGetBuildingPropsOf, sample.buildings[i]);
        AddResult(per, of, 0);
        const ListResult at = Query(m, slot::kGetBuildingPropsAt, sample.buildingCell[i].x0, sample.buildingCell[i].z0);
        AddResult(per, at, 0);
        calls += 2;
        *total += of.count;
        *withProps += of.count != 0;
    }
    f.Add(per.h);
    log::Write("autotest: propmgr: buildings %08X (%zu buildings, %u props, %u with props)", per.h,
               sample.buildings.size(), *total, *withProps);
}

// --- The round trips ---------------------------------------------------------------------

// The props of every usage over the whole city, and the three test
// rectangles' props of the city and street kinds, counted through the
// occupant manager directly with the prop filter (the filter's mode is left
// as it was found).
struct Counts {
    uint32_t perUsage[5] = {};
    uint32_t rects[6] = {};
    bool operator==(const Counts& o) const
    {
        return std::memcmp(perUsage, o.perUsage, sizeof perUsage) == 0 && std::memcmp(rects, o.rects, sizeof rects) == 0;
    }
};

uint32_t CountThrough(void* m, uint32_t mode, const Rect& r)
{
    void* const occupants = At<void*>(m, 0x14);
    void* const filter = At<void*>(m, kFilterField);
    if (!occupants || !filter)
        return 0xEEEEEEEEu;
    const uint32_t found = At<uint32_t>(filter, kFilterMode);
    At<uint32_t>(filter, kFilterMode) = mode;
    const int32_t cells[4] = {r.x0, r.z0, r.x1, r.z1};
    char* head = NewList();
    Virtual<uint32_t>(occupants, 0xA0, static_cast<void*>(&head), static_cast<const int32_t*>(cells),
                      static_cast<const int32_t*>(cells + 2), filter, uint32_t{0xFFFFFFFF});
    uint32_t count = 0;
    for (char* node = At<char*>(head, 0x0); node != head && count < 10000000; node = At<char*>(node, 0x0)) {
        if (void* const occupant = At<void*>(node, 0x8))
            Virtual<uint32_t>(occupant, 0x8);
        ++count;
    }
    This<void>(addr::SC4List_Destroy2, &head);
    At<uint32_t>(filter, kFilterMode) = found;
    return count;
}

Counts CountAll(void* m, const Sample& sample, int32_t sizeX, int32_t sizeZ)
{
    Counts c;
    const Rect whole{0, 0, sizeX - 1, sizeZ - 1};
    for (uint32_t usage = 0; usage < 5; ++usage)
        c.perUsage[usage] = CountThrough(m, usage, whole);
    for (std::size_t i = 0; i < 3 && i < sample.rects.size(); ++i) {
        c.rects[i * 2] = CountThrough(m, 1, sample.rects[i]);
        c.rects[i * 2 + 1] = CountThrough(m, 4, sample.rects[i]);
    }
    return c;
}

// The first types in the table's sorted order whose profile class is the plain prop.
std::vector<uint32_t> PlainTypes(void* m, const Types& types, std::size_t most)
{
    std::vector<uint32_t> out;
    for (const uint32_t type : types.sorted) {
        if (out.size() >= most)
            break;
        const uint32_t* const profile = Virtual<const uint32_t*>(m, slot::kGetPropProfilePtr, type);
        if (profile && profile[0] == kClassDefault)
            out.push_back(type);
    }
    return out;
}

// Plain fields of a made prop. `type` is 0 for a prop made from a key.
void AddMade(Fnv& f, void* m, void* prop, uint32_t type)
{
    if (!prop) {
        f.Missing();
        return;
    }
    void* const occupant = Virtual<void*>(prop, 0x0C);
    f.Add(occupant != nullptr);
    if (occupant)
        f.Add(Virtual<uint32_t>(occupant, 0x1C));
    // The class: the vtable's address is a constant of the program's image.
    const uint32_t vtable = At<uint32_t>(prop, 0);
    f.Add(vtable >= 0x00400000u && vtable < 0x01000000u ? vtable : 0xFFFFFFFFu);
    if (type) {
        f.Add(Virtual<uint32_t>(prop, 0x1C));
        if (const uint32_t* const profile = Virtual<const uint32_t*>(m, slot::kGetPropProfilePtr, type)) {
            f.Add(profile[0]);
            f.Add(profile[1]);
            f.Add(profile[2]);
            f.Add(profile[3]);
        }
    }
}

// A prop made by CreateProp, placed and oriented as the game's own adders do
// before they Init it. The caller holds the reference it comes with.
void* MakePlaced(void* m, uint32_t type, float* position)
{
    void* prop = nullptr;
    if (!Virtual<uint8_t>(m, slot::kCreateProp, type, &prop) || !prop)
        return nullptr;
    void* const occupant = Virtual<void*>(prop, 0x0C);
    if (occupant)
        Virtual<void>(occupant, 0x24, static_cast<const float*>(position));
    Virtual<void>(prop, 0x28, int32_t{0});
    return prop;
}

void CellCentre(void* city, void* terrain, int32_t x, int32_t z, float* position)
{
    float corner[2] = {0, 0};
    Virtual<void>(city, 0x180, x, z, &corner[0], &corner[1]);
    position[0] = corner[0] + 8.0f;
    position[1] = terrain ? Virtual<float>(terrain, 0x38, x, z) : 0.0f;
    position[2] = corner[1] + 8.0f;
}

void DriveMade(Fnv& f, void* m, const Types& types, int& calls)
{
    // Made and released without being inserted.
    std::vector<uint32_t> chosen = PlainTypes(m, types, 3);
    uint32_t decal = 0, effect = 0;
    for (const uint32_t type : types.sorted) {
        if (decal && effect)
            break;
        const uint32_t* const profile = Virtual<const uint32_t*>(m, slot::kGetPropProfilePtr, type);
        if (!profile)
            continue;
        if (!decal && profile[0] == kClassDefault + 1)
            decal = type;
        if (!effect && profile[0] == kClassDefault + 2)
            effect = type;
    }
    if (decal)
        chosen.push_back(decal);
    if (effect)
        chosen.push_back(effect);
    for (const uint32_t type : chosen) {
        Fnv mine;
        mine.Add(type);
        void* prop = nullptr;
        const uint8_t made = Virtual<uint8_t>(m, slot::kCreateProp, type, &prop);
        mine.Add(made);
        AddMade(mine, m, prop, type);
        ++calls;
        // SetUsage (0x004A1F00, not virtual) on the made prop: the usage it reads back.
        if (prop) {
            if (void* const occupant = Virtual<void*>(prop, 0x0C)) {
                for (const uint32_t usage : {2u, 3u}) {
                    This<void>(addr::cSC4PropManager_SetUsage, m, occupant, usage);
                    void* viaProp = nullptr;
                    if (Virtual<uint8_t>(occupant, 0x0, kIidProp, &viaProp) && viaProp) {
                        mine.Add(static_cast<uint8_t>(Virtual<uint8_t>(viaProp, 0x58)));
                        Virtual<uint32_t>(viaProp, 0x8);
                    }
                    ++calls;
                }
            }
            Virtual<uint32_t>(prop, 0x8);
        }
        // The same type from its key.
        uint32_t key[3] = {};
        if (Virtual<uint8_t>(m, slot::kGetPropKeyFromType, type, key)) {
            void* generic = nullptr;
            const uint8_t madeGeneric = Virtual<uint8_t>(m, slot::kCreateGenericProp, static_cast<const uint32_t*>(key), &generic);
            mine.Add(madeGeneric);
            if (madeGeneric)
                AddMade(mine, m, generic, 0);
            if (madeGeneric && generic)
                Virtual<uint32_t>(generic, 0x8);
            ++calls;
        }
        f.Add(mine.h);
        log::Write("autotest: propmgr: made %08X: %08X", type, mine.h);
    }
    // An unknown type makes nothing.
    void* prop = nullptr;
    f.Add(Virtual<uint8_t>(m, slot::kCreateProp, uint32_t{0xDEADBEEF}, &prop));
    f.Add(prop == nullptr);
    ++calls;
}

void DriveCityEdit(Fnv& f, void* m, void* city, void* terrain, uint32_t type, int32_t sizeX, int32_t sizeZ, int& calls,
                   bool& skipped)
{
    void* const lots = At<void*>(m, 0x20);
    if (!lots || !city || type == 0) {
        skipped = true;
        f.Missing();
        return;
    }
    // The first cell, walking from a quarter of the city on, with no lot and
    // no prop in its 3 x 3 neighbourhood. The first pass wants the occupant
    // manager's 64 m cell around it to be empty too (the flammability
    // simulator recalculates where occupants come and go); the second takes
    // the first cell that qualifies without that.
    int32_t spotX = -1, spotZ = -1;
    bool wholeCellEmpty = false;
    void* const occupants = At<void*>(m, 0x14);
    for (int pass = 0; pass < 2 && spotX < 0; ++pass) {
        for (int32_t z = sizeZ / 4; z < sizeZ * 3 / 4 && spotX < 0; ++z) {
            for (int32_t x = sizeX / 4; x < sizeX * 3 / 4 && spotX < 0; ++x) {
                if (Virtual<void*>(lots, 0x18, x, z, uint32_t{0}))
                    continue;
                if (pass == 0 && (!occupants || !(Virtual<uint8_t>(occupants, 0x64, x / 4 + 64, z / 4 + 64) & 1)))
                    continue;
                const Rect around{x - 1, z - 1, x + 1, z + 1};
                if (CountThrough(m, 0, around) != 0)
                    continue;
                spotX = x;
                spotZ = z;
                wholeCellEmpty = pass == 0;
            }
        }
    }
    if (spotX < 0) {
        skipped = true;
        f.Missing();
        log::Write("autotest: propmgr: city edit skipped: no empty spot");
        return;
    }
    float position[3];
    CellCentre(city, terrain, spotX, spotZ, position);
    const Rect cell{spotX, spotZ, spotX, spotZ};
    const int32_t cells[4] = {spotX, spotZ, spotX, spotZ};
    float metres[4];
    FloatRect(city, cell, metres);
    Fnv mine;
    // AddCityPropNew, read back both ways, RemoveCityPropsInRectangle (metres).
    mine.Add(Virtual<uint8_t>(m, slot::kAddCityPropNew, type, static_cast<const float*>(position), int32_t{0}));
    const ListResult a = Query(m, slot::kCityPropsInRectangle, static_cast<const int32_t*>(cells));
    const ListResult b = Query(m, slot::kCityPropsInArea, static_cast<const float*>(metres));
    AddResult(mine, a, FilterMode(m));
    AddResult(mine, b, FilterMode(m));
    mine.Add(Virtual<uint8_t>(m, slot::kRemoveCityPropsInRectangle, static_cast<const float*>(metres)));
    const ListResult c = Query(m, slot::kCityPropsInRectangle, static_cast<const int32_t*>(cells));
    AddResult(mine, c, FilterMode(m));
    calls += 6;
    log::Write("autotest: propmgr: city edit new: %u/%u found, %u after removal (64 m cell %s)", a.count, b.count,
               c.count, wholeCellEmpty ? "empty" : "not empty");
    // CreateProp, AddCityProp, RemoveCityPropsInArea (cells).
    if (void* const prop = MakePlaced(m, type, position)) {
        void* const occupant = Virtual<void*>(prop, 0x0C);
        mine.Add(Virtual<uint8_t>(m, slot::kAddCityProp, occupant));
        const ListResult d = Query(m, slot::kCityPropsInRectangle, static_cast<const int32_t*>(cells));
        AddResult(mine, d, FilterMode(m));
        mine.Add(Virtual<uint8_t>(m, slot::kRemoveCityPropsInArea, static_cast<const int32_t*>(cells)));
        const ListResult e = Query(m, slot::kCityPropsInRectangle, static_cast<const int32_t*>(cells));
        AddResult(mine, e, FilterMode(m));
        Virtual<uint32_t>(prop, 0x8);
        calls += 5;
        log::Write("autotest: propmgr: city edit existing: %u found, %u after removal", d.count, e.count);
    } else {
        mine.Missing();
    }
    f.Add(mine.h);
}

void DriveLotEdit(Fnv& f, void* m, void* city, void* terrain, uint32_t type, const Sample& sample, int& calls,
                  bool& skipped)
{
    // A lot with no props.
    std::size_t index = sample.lots.size();
    for (std::size_t i = 0; i < sample.lots.size(); ++i) {
        const ListResult of = Query(m, slot::kGetLotPropsOfLot, sample.lots[i]);
        const ListResult at = Query(m, slot::kGetLotPropsAt, sample.lotRect[i].x0, sample.lotRect[i].z0);
        calls += 2;
        if (of.count == 0 && at.count == 0 && of.answer && at.answer) {
            index = i;
            break;
        }
    }
    if (index == sample.lots.size() || !city || type == 0) {
        skipped = true;
        f.Missing();
        log::Write("autotest: propmgr: lot edit skipped: no lot without props");
        return;
    }
    void* const lot = sample.lots[index];
    const Rect rect = sample.lotRect[index];
    float position[3];
    CellCentre(city, terrain, rect.x0, rect.z0, position);
    Fnv mine;
    // AddLotPropNew, RemoveLotPropsAt.
    void* const added = Virtual<void*>(m, slot::kAddLotPropNew, type, static_cast<const float*>(position), int32_t{0});
    mine.Add(added != nullptr);
    const ListResult a = Query(m, slot::kGetLotPropsOfLot, lot);
    const ListResult b = Query(m, slot::kGetLotPropsAt, rect.x0, rect.z0);
    AddResult(mine, a, FilterMode(m));
    AddResult(mine, b, FilterMode(m));
    mine.Add(Virtual<uint8_t>(m, slot::kRemoveLotPropsAt, rect.x0, rect.z0));
    const ListResult c = Query(m, slot::kGetLotPropsOfLot, lot);
    AddResult(mine, c, FilterMode(m));
    calls += 6;
    log::Write("autotest: propmgr: lot edit new: %u/%u found, %u after removal", a.count, b.count, c.count);
    // CreateProp, AddLotProp, RemoveLotPropsOfLot.
    if (void* const prop = MakePlaced(m, type, position)) {
        void* const occupant = Virtual<void*>(prop, 0x0C);
        mine.Add(Virtual<uint8_t>(m, slot::kAddLotProp, lot, occupant));
        const ListResult d = Query(m, slot::kGetLotPropsOfLot, lot);
        AddResult(mine, d, FilterMode(m));
        mine.Add(Virtual<uint8_t>(m, slot::kRemoveLotPropsOfLot, lot));
        const ListResult e = Query(m, slot::kGetLotPropsOfLot, lot);
        AddResult(mine, e, FilterMode(m));
        Virtual<uint32_t>(prop, 0x8);
        calls += 5;
        log::Write("autotest: propmgr: lot edit existing: %u found, %u after removal", d.count, e.count);
    } else {
        mine.Missing();
    }
    f.Add(mine.h);
}

void DriveBuildingEdit(Fnv& f, void* m, void* city, uint32_t type, const Sample& sample, int rounds, int& calls,
                       bool& skipped)
{
    // A building with no props.
    std::size_t index = sample.buildings.size();
    for (std::size_t i = 0; i < sample.buildings.size(); ++i) {
        const ListResult of = Query(m, slot::kGetBuildingPropsOf, sample.buildings[i]);
        ++calls;
        if (of.count == 0) {
            index = i;
            break;
        }
    }
    if (index == sample.buildings.size() || !city || type == 0) {
        skipped = true;
        f.Missing();
        log::Write("autotest: propmgr: building edit skipped: no building without props");
        return;
    }
    void* const building = sample.buildings[index];
    const Rect cell = sample.buildingCell[index];
    void* const occupants = At<void*>(m, 0x14);
    // The building the cell's query finds must be this one.
    void* atCell = nullptr;
    const bool same = occupants && Virtual<uint8_t>(occupants, 0x84, &atCell, cell.x0, cell.z0, kBuildingType) && atCell == building;
    if (atCell)
        Virtual<uint32_t>(atCell, 0x8);
    float position[3] = {0, 0, 0};
    Virtual<void>(building, 0x20, position);
    Fnv mine;
    mine.Add(same);
    for (int round = 0; round < 3; ++round) {
        if (!(rounds & (1 << round)))
            continue;
        if (round == 2 && !same) {
            mine.Missing();
            continue;
        }
        void* const prop = MakePlaced(m, type, position);
        if (!prop) {
            mine.Missing();
            continue;
        }
        void* const occupant = Virtual<void*>(prop, 0x0C);
        mine.Add(Virtual<uint8_t>(m, slot::kAddBuildingProp, building, occupant));
        const ListResult a = Query(m, slot::kGetBuildingPropsOf, building);
        AddResult(mine, a, 0);
        if (round == 0)
            mine.Add(Virtual<uint8_t>(m, slot::kRemoveBuildingProp, building, occupant));
        else if (round == 1)
            mine.Add(Virtual<uint8_t>(m, slot::kRemoveBuildingPropsOf, building));
        else
            mine.Add(Virtual<uint8_t>(m, slot::kRemoveBuildingPropsAt, cell.x0, cell.z0));
        const ListResult b = Query(m, slot::kGetBuildingPropsOf, building);
        AddResult(mine, b, 0);
        Virtual<uint32_t>(prop, 0x8);
        calls += 5;
        log::Write("autotest: propmgr: building edit round %d: %u found, %u after removal (answers %u %u)", round,
                   a.count, b.count, static_cast<unsigned>(a.answer), static_cast<unsigned>(b.answer));
    }
    f.Add(mine.h);
}

// --- The switch -------------------------------------------------------------------------------

void Drive(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_PROPMGR") || !game::IsBuild641())
        return;
    void* const m = Global<void*>(addr::cSC4PropManager_Global);
    void* const city = Global<void*>(addr::SC4_City);
    void* const terrain = Global<void*>(addr::SC4_Terrain);
    int32_t sizeX = 0, sizeZ = 0;
    if (city) {
        sizeX = Virtual<int32_t>(city, 0x174);
        sizeZ = Virtual<int32_t>(city, 0x178);
    }
    if (sizeX <= 0 || sizeZ <= 0 || sizeX > 4096 || sizeZ > 4096)
        sizeX = sizeZ = 0;
    const int editLevel = EnvInt("OPENSC4_TEST_PROPMGR_EDIT", 0);
    const bool edit = editLevel != 0;
    Fnv object, typesHash, families, cityHash, lotsHash, buildingsHash, editHash;
    uint32_t modeFound = 0;
    uint32_t typeSubs[3] = {}, familySubs[2] = {}, citySubs[2] = {};
    uint32_t cityTotals[4] = {}, lotTotal = 0, buildingTotal = 0, buildingsWithProps = 0;
    int calls = 0;
    Types types;
    Sample sample;
    if (m)
        Collect(sample, m, sizeX, sizeZ);

    DriveObject(object, m, city, &modeFound);
    DriveTypes(typesHash, m, types, calls, typeSubs);
    DriveFamilies(families, m, types, calls, familySubs);
    DriveCity(cityHash, m, city, sample, calls, citySubs, cityTotals);
    DriveLots(lotsHash, m, sample, calls, &lotTotal);
    DriveBuildings(buildingsHash, m, sample, calls, &buildingTotal, &buildingsWithProps);

    bool restored = true;
    bool cityEditSkipped = false, lotEditSkipped = false, buildingEditSkipped = false;
    if (edit && m && sizeX > 0) {
        const Counts before = CountAll(m, sample, sizeX, sizeZ);
        const std::vector<uint32_t> plain = PlainTypes(m, types, 3);
        const int parts = EnvInt("OPENSC4_TEST_PROPMGR_EDITPARTS", editLevel >= 2 ? 15 : 7);
        const uint32_t type = plain.empty() ? 0 : plain[0];
        if (parts & 1)
            DriveMade(editHash, m, types, calls);
        if (parts & 2)
            DriveCityEdit(editHash, m, city, terrain, type, sizeX, sizeZ, calls, cityEditSkipped);
        if (parts & 4)
            DriveLotEdit(editHash, m, city, terrain, plain.size() > 1 ? plain[1] : type, sample, calls, lotEditSkipped);
        if (parts & 8)
            DriveBuildingEdit(editHash, m, city, plain.size() > 2 ? plain[2] : type, sample, (parts & 0x70) ? (parts >> 4) & 7 : 7,
                              calls, buildingEditSkipped);
        const Counts after = CountAll(m, sample, sizeX, sizeZ);
        restored = before == after;
        editHash.Add(restored);
        log::Write("autotest: propmgr: restored %s: per usage %u %u %u %u %u -> %u %u %u %u %u; rectangles %u %u %u %u %u %u -> %u %u %u %u %u %u",
                   restored ? "yes" : "NO", before.perUsage[0], before.perUsage[1], before.perUsage[2],
                   before.perUsage[3], before.perUsage[4], after.perUsage[0], after.perUsage[1], after.perUsage[2],
                   after.perUsage[3], after.perUsage[4], before.rects[0], before.rects[1], before.rects[2],
                   before.rects[3], before.rects[4], before.rects[5], after.rects[0], after.rects[1], after.rects[2],
                   after.rects[3], after.rects[4], after.rects[5]);
    }
    // The filter's mode was left by the queries as the game's callers leave
    // it; it is put back to what it was found as.
    if (m) {
        if (void* const filter = At<void*>(m, kFilterField))
            At<uint32_t>(filter, kFilterMode) = modeFound;
    }
    for (void* const held : sample.held)
        Virtual<uint32_t>(held, 0x8);

    log::Write("autotest: propmgr: object %08X types %08X (%08X %08X %08X) families %08X (%08X %08X)", object.h, typesHash.h,
               typeSubs[0], typeSubs[1], typeSubs[2], families.h, familySubs[0], familySubs[1]);
    log::Write("autotest: propmgr: city %08X (%08X %08X; %u %u %u %u) lots %08X (%u) buildings %08X (%u, %u with props)",
               cityHash.h, citySubs[0], citySubs[1], cityTotals[0], cityTotals[1], cityTotals[2], cityTotals[3],
               lotsHash.h, lotTotal, buildingsHash.h, buildingTotal, buildingsWithProps);
    log::Write("autotest: propmgr: %d calls, edit %s %08X (skipped: city %d lot %d building %d), %u props of prop type 0 left out",
               calls, edit ? "on" : "off", editHash.h, cityEditSkipped, lotEditSkipped, buildingEditSkipped, g_moving);
    // One short hash: the game cuts the daily fingerprint line at 4093 characters.
    Fnv all;
    for (const uint32_t h : {object.h, typesHash.h, families.h, cityHash.h, lotsHash.h, buildingsHash.h})
        all.Add(h);
    if (edit)
        all.Add(editHash.h);
    std::snprintf(g_token, sizeof g_token, " propmgr %08X", all.h);
}

const DayHook kPropMgr{"propmgr", 57, &Drive};

void Fingerprint(cISC4City*, std::string& line)
{
    line += g_token;
}

const FingerprintPart kFingerprint{"propmgr", 9, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
