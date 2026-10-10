// Our cSC4Lot, the state part (build 641): the functions with their entry
// from 0x006C2340 to the end of the class (0x006C3AF8) that are not the
// object part's (the constructors 0x006C2340 and 0x006C2450, the destructors
// 0x006C25D0 and 0x006C3000, the factory 0x006C2FC0 and Shutdown 0x006C3060
// are citylot_object.cpp's; that file has Write only, so Read is here):
//
//   0x006C26D0 SetCondition (primary slot 36)
//   0x006C27E0 UpdateTrips (slot 4 of the traffic source part at +0x1C)
//   0x006C2E30 the populations' clear            (plain function)
//   0x006C2E70 the populations' insert(n, value) (plain function)
//   0x006C3100 SetState (primary slot 34)
//   0x006C34F0 the populations' resize(n, value) (plain function)
//   0x006C3560 the populations' resize(n)        (detour: Read calls it)
//   0x006C35F0 Read (slot 4 of the part at +0x4; group simulators.citylot.load)
//   0x006C3AF0 the holder part's Read thunk (slot 24 of 0x00AB1930)
//
// Fields used here (offsets from the lot's start; names in citylot_sim.h):
//   0x38 kStateDay      uint32: SetState stores the simulator date's day
//                       number (simulator slot 7, the date's slot 26);
//                       SetCondition too when the state is 4
//   0x3C kBuildingType  uint32: sent as data 3 of both messages; SetState
//                       asks the development simulator for its profile
//   0x54 kPopulations   vector of 12-byte map<uint32 developer type, uint16>
//                       (begin, end, capacity): SetState copies the building
//                       profile's maps (profile + 0xC, the same vector type)
//                       into it with every count 0 on entering state 2, and
//                       zeroes every count outside states 2 and 3
//   0x7F kState         uint8 (SetState's first argument)
//   0x80 kCondition     uint8 (SetCondition's argument)
//   0x82 kWealth        uint8: SetState's second argument when the lot
//                       enters states 2-3 and the argument is not 0; the
//                       message's data 4
//   0x83 kVacateReason  uint8: SetState's third argument on entering state 4
//   0x84 kTrips         list of trip records (head node; nodes of 0x34
//                       bytes: next, previous, record). UpdateTrips reads and
//                       writes the records' +0x18 count, +0x1C type (0-2
//                       residential wealth, 3 freight), +0x1D index (0-2: the
//                       travel strategy for residential, 2 for freight),
//                       +0x1E flags (2: the placeholder of a lot with nothing
//                       to send), +0x1F kind (4 and 5 take added trips, 4
//                       gives them back first)
//   0x88 kRandom        uint8 0-99: picks the one travel strategy of a lot
//                       with fewer than 50 travellers
//
// A building profile (development simulator slot 39): +0x0 and +0x4 the two
// arguments of the simulator's slot 30 (the developer type), +0x8 uint16
// (slot 51's argument), +0xC the vector of maps.
#include "core/citylot_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::citylot_sim {
namespace {

// --- Game code called at its address ---------------------------------------------

template <typename R, typename... Args>
R CallThis(std::uintptr_t address, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(address)(self, args...);
}

template <typename R, typename... Args>
R CallCdecl(std::uintptr_t address, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(address)(args...);
}

void* Allocate(std::size_t size) { return CallCdecl<void*, std::size_t>(addr::SC4_stl_allocate, size); }
void Free(void* memory) { CallCdecl<void, void*>(addr::SC4_Free, memory); }

// The day number of the simulator's date.
uint32_t Today()
{
    void* const date = Virtual<void*>(Global<void*>(addr::SC4_SimulatorService), 0x1C);
    return Virtual<uint32_t>(date, 0x68);
}

// --- The populations: a vector of 12-byte maps ------------------------------------

constexpr std::size_t kMapSize = 0xC;

struct MapVector {
    char* begin;
    char* end;
    char* capacity;
};

// A map as the game lays it out: the head node, the node count, padding.
struct Map {
    char* head;
    uint32_t size;
    uint32_t pad;
};
static_assert(sizeof(Map) == kMapSize);

// Elements between two addresses, as the game divides (signed, toward zero).
int32_t MapCount(const char* first, const char* last) { return static_cast<int32_t>(last - first) / 12; }

// 0x006C2E30: destroys every map; the memory stays. (The game first "moves
// down" the empty range behind the end through 0x006C10A0, which does
// nothing.)
void ClearMaps(MapVector* vector)
{
    uint8_t tag = 0;
    char* const begin = vector->begin;
    CallCdecl<void, void*, void*, void*>(addr::SC4MapVector_DestroyRange, begin, vector->end, &tag);
    vector->end = begin;
}

// 0x006C2E70: vector::insert(position, count, value).
void InsertMaps(MapVector* vector, char* position, uint32_t count, const void* value)
{
    if (count == 0)
        return;
    uint8_t tag = 0;
    if (static_cast<uint32_t>(MapCount(vector->end, vector->capacity)) < count) {
        CallThis<void, char*, const void*, void*, uint32_t, uint32_t>(addr::SC4MapVector_InsertOverflow, vector, position,
                                                                     value, &tag, count, 0u);
        return;
    }
    Map copy;
    CallThis<void*, const void*>(addr::SC4LotTravelMap_CopyConstruct, &copy, value);
    char* const oldEnd = vector->end;
    const auto after = static_cast<uint32_t>(MapCount(position, oldEnd));
    if (after > count) {
        const std::size_t bytes = count * kMapSize;
        CallCdecl<void*, void*, void*, void*, void*>(addr::SC4MapVector_UninitializedCopy, oldEnd - bytes, oldEnd, oldEnd,
                                                     &tag);
        vector->end += bytes;
        CallCdecl<void*, void*, void*, void*, void*, int32_t>(addr::cSC4Lot_Fn_006C0B40, position, oldEnd - bytes, oldEnd,
                                                              &tag, 0);
        CallCdecl<void, void*, void*, const void*>(addr::cSC4Lot_Fn_006C1040, position, position + bytes, &copy);
    } else {
        const uint32_t extra = count - after;
        CallCdecl<void*, void*, uint32_t, const void*>(addr::cSC4Lot_Fn_006C1070, oldEnd, extra, &copy);
        char* const filledEnd = vector->end + extra * kMapSize;
        vector->end = filledEnd;
        CallCdecl<void*, void*, void*, void*, void*>(addr::SC4MapVector_UninitializedCopy, position, oldEnd, filledEnd,
                                                     &tag);
        vector->end += after * kMapSize;
        CallCdecl<void, void*, void*, const void*>(addr::cSC4Lot_Fn_006C1040, position, oldEnd, &copy);
    }
    CallThis<void>(addr::SC4Uint32Uint16Map_Destroy, &copy);
}

// 0x006C34F0: vector::resize(count, value).
void ResizeMapsWith(MapVector* vector, uint32_t count, const void* value)
{
    char* const end = vector->end;
    const auto size = static_cast<uint32_t>(MapCount(vector->begin, end));
    if (count < size) {
        // erase(begin + count, end): nothing behind the end to move down.
        uint8_t tag = 0;
        char* const newEnd = vector->begin + count * kMapSize;
        CallCdecl<void, void*, void*, void*>(addr::SC4MapVector_DestroyRange, newEnd, vector->end, &tag);
        vector->end = newEnd;
        return;
    }
    InsertMaps(vector, end, count - size, value);
}

// 0x006C3560: vector::resize(count) with an empty map as the value.
void __fastcall ResizeMaps(MapVector* vector, void*, uint32_t count)
{
    static bool logged = false;
    FirstCall("ResizePopulations", logged);
    Map empty{};
    char* const head = static_cast<char*>(Allocate(0x18));
    empty.head = head;
    empty.size = 0;
    At<uint8_t>(head, 0x0) = 0;
    At<char*>(head, 0x4) = nullptr;
    At<char*>(head, 0x8) = head;
    At<char*>(head, 0xC) = head;
    ResizeMapsWith(vector, count, &empty);
    // Nothing writes the temporary, so it is still empty: the game's tree
    // erase (0x00458E00) is skipped as there, and the head freed.
    if (empty.head)
        Free(empty.head);
}

// --- The messages -----------------------------------------------------------------

// A copy of the lot (from the lots' pool, through the game's copy
// constructor), referenced, and a framework message about it with the old
// and new value and the building type.
struct Change {
    void* copy;
    void* message;
};

Change BeginChange(char* self, uint32_t type, uint32_t before, uint32_t now)
{
    Change change{};
    void* const memory = CallCdecl<void*, std::size_t>(addr::cSC4Lot_Allocate, kObjectSize);
    change.copy = CallThis<void*, const void*>(addr::cSC4Lot_CopyConstruct, memory, self);
    Virtual<uint32_t>(change.copy, 0x4);
    void* const message = CallCdecl<void*, std::size_t>(addr::RZ_operator_new, 0x2C);
    change.message = message ? CallThis<void*>(addr::cGZMessage_cGZMessage, message) : nullptr;
    Virtual<uint32_t>(change.message, 0x4);
    Virtual<void>(change.message, 0x14, type);
    Virtual<void>(change.message, 0x84, change.copy);
    Virtual<void>(change.message, 0x2C, before);
    Virtual<void>(change.message, 0x40, now);
    Virtual<void>(change.message, 0x54, At<uint32_t>(self, field::kBuildingType));
    return change;
}

void EndChange(const Change& change)
{
    Virtual<void>(Global<void*>(addr::SC4_MessageServer), 0x10, change.message, int32_t{0});
    Virtual<uint32_t>(change.message, 0x8);
    Virtual<uint32_t>(change.copy, 0x8);
}

// --- Slot 36: SetCondition (0x006C26D0) -------------------------------------------

void __fastcall SetCondition(char* self, void*, uint32_t condition)
{
    static bool logged = false;
    FirstCall("SetCondition", logged);
    const uint8_t before = At<uint8_t>(self, field::kCondition);
    if (condition == before)
        return;
    At<uint8_t>(self, field::kCondition) = static_cast<uint8_t>(condition);
    if (At<uint8_t>(self, field::kState) == 4)
        At<uint32_t>(self, field::kStateDay) = Today();
    const Change change = BeginChange(self, 0xE99B3D4Du, before, At<uint8_t>(self, field::kCondition));
    EndChange(change);
}

// --- Slot 34: SetState (0x006C3100) -----------------------------------------------

bool HasOccupants(uint8_t state) { return state >= 2 && state < 4; }

// Sets every count of a populations map to 0, through the game's operator[]
// as the game does (the keys exist, so nothing is inserted).
void ZeroCounts(char* map)
{
    char* const head = At<char*>(map, 0x0);
    for (char* node = At<char*>(head, 0x8); node != At<char*>(map, 0x0); node = TreeNext(node)) {
        uint32_t key = At<uint32_t>(node, 0x10);
        *CallThis<uint16_t*, const uint32_t*>(addr::SC4Uint32Uint16Map_Index, map, &key) = 0;
    }
}

void __fastcall SetState(char* self, void*, uint32_t state, uint32_t wealth, uint32_t vacateReason)
{
    static bool logged = false;
    FirstCall("SetState", logged);
    const uint8_t before = At<uint8_t>(self, field::kState);
    if (state == before)
        return;
    At<uint8_t>(self, field::kState) = static_cast<uint8_t>(state);
    At<uint32_t>(self, field::kStateDay) = Today();

    if (before == 2 && At<uint32_t>(self, field::kBuildingType) != 0) {
        void* const development = Global<void*>(addr::SC4_Service43D68);
        char* const profile = Virtual<char*>(development, 0x9C, At<uint32_t>(self, field::kBuildingType));
        if (At<uint32_t>(profile, 0x0) != 0) {
            Virtual<void>(development, 0xCC, uint32_t{At<uint16_t>(profile, 0x8)});
            if (At<uint8_t>(self, field::kState) == 3) {
                const uint32_t developer = Virtual<uint32_t>(Global<void*>(addr::SC4_Service43D68), 0x78,
                                                             At<uint32_t>(profile, 0x0), At<uint32_t>(profile, 0x4));
                // The capacity goes on as the whole EAX of slot 50.
                const uint32_t capacity = Virtual<uint32_t>(self, slot::kGetCapacityByType, developer, 1u);
                Virtual<void>(self, slot::kSetPopulation, developer, capacity);
            }
        }
    }

    auto* const populations = &At<MapVector>(self, field::kPopulations);
    switch (At<uint8_t>(self, field::kState)) {
    case 4:
        At<uint8_t>(self, field::kVacateReason) = static_cast<uint8_t>(vacateReason);
        break;
    case 2: {
        const uint32_t building = Virtual<uint32_t>(self, slot::kGetBuildingType, 1u);
        if (building == 0)
            break;
        void* const development = Global<void*>(addr::SC4_Service43D68);
        ClearMaps(populations);
        char* const profile = Virtual<char*>(development, 0x9C, building);
        auto* const source = &At<MapVector>(profile, 0xC);
        // The source's size is read again after every map, as the game does.
        for (uint32_t i = 0; i < static_cast<uint32_t>(MapCount(source->begin, source->end)); ++i) {
            const std::size_t offset = i * kMapSize;
            char* const from = source->begin + offset;
            if (populations->end != populations->capacity) {
                if (populations->end)
                    CallThis<void*, const void*>(addr::SC4LotTravelMap_CopyConstruct, populations->end, from);
                populations->end += kMapSize;
            } else {
                uint8_t tag = 0;
                CallThis<void, char*, const void*, void*, uint32_t, uint32_t>(addr::SC4MapVector_InsertOverflow, populations,
                                                                             populations->end, from, &tag, 1u, 1u);
            }
            // Every key of the profile's map, set to 0 in our copy.
            char* const head = At<char*>(from, 0x0);
            for (char* node = At<char*>(head, 0x8); node != At<char*>(from, 0x0); node = TreeNext(node)) {
                uint32_t key = At<uint32_t>(node, 0x10);
                *CallThis<uint16_t*, const uint32_t*>(addr::SC4Uint32Uint16Map_Index, populations->begin + offset, &key) = 0;
            }
        }
        break;
    }
    default:
        break;
    }

    if (!HasOccupants(At<uint8_t>(self, field::kState))) {
        const int32_t count = MapCount(populations->begin, populations->end);
        for (int32_t i = 0; i < count; ++i)
            ZeroCounts(At<char*>(self, field::kPopulations) + static_cast<std::size_t>(i) * kMapSize);
    }

    const uint8_t wealthBefore = At<uint8_t>(self, field::kWealth);
    CallThis<void>(addr::cSC4Lot_Fn_006C1430, self);
    uint8_t reported = At<uint8_t>(self, field::kWealth);
    const uint8_t now = At<uint8_t>(self, field::kState);
    if (HasOccupants(before) != HasOccupants(now)) {
        if (HasOccupants(now)) {
            if (wealth != 0) {
                At<uint8_t>(self, field::kWealth) = static_cast<uint8_t>(wealth);
                reported = static_cast<uint8_t>(wealth);
            }
        } else {
            reported = wealthBefore;
        }
    }
    CallThis<void>(addr::cSC4Lot_UpdateNeedsPower, self);

    const Change change = BeginChange(self, 0xE8BDA2E7u, before, At<uint8_t>(self, field::kState));
    Virtual<void>(change.message, 0x68, uint32_t{reported});
    EndChange(change);
}

// --- The trips (0x006C27E0) -------------------------------------------------------

// A trip record as the list's nodes hold it at +0x8 (copy constructor
// 0x006C0AE0; saved by 0x0070E0B0).
struct TripRecord {
    void* a[3]; // two byte vectors
    void* b[3];
    uint32_t count;
    uint8_t type;
    uint8_t index;
    uint8_t flags;
    uint8_t kind;
    uint16_t word20;
    uint16_t word22;
    uint32_t float24;
    uint32_t dword28;
};
static_assert(sizeof(TripRecord) == 0x2C);

namespace node {
constexpr std::size_t kNext = 0x0, kPrevious = 0x4, kVectorA = 0x8, kVectorB = 0x14;
constexpr std::size_t kCount = 0x20, kType = 0x24, kIndex = 0x25, kFlags = 0x26, kKind = 0x27;
constexpr std::size_t kSize = 0x34;
} // namespace node

// Takes a node out of its list and frees it (second vector, first vector,
// node, as the game does); the node after it.
char* EraseTrip(char* trip)
{
    char* const previous = At<char*>(trip, node::kPrevious);
    char* const next = At<char*>(trip, node::kNext);
    At<char*>(previous, node::kNext) = next;
    At<char*>(next, node::kPrevious) = previous;
    if (void* const memory = At<void*>(trip, node::kVectorB))
        Free(memory);
    if (void* const memory = At<void*>(trip, node::kVectorA))
        Free(memory);
    Free(trip);
    return next;
}

// The game's rounding of a positive value: 1.5 x 2^23 (0x00AB16B4) added,
// stored as a float, and the float's bits less that constant's
// (+ 0xB4C00000). The low bits are the value rounded to nearest, ties to
// even. The x87 unit runs at 53 bits, so the sums are doubles.
uint32_t RoundBits(double value)
{
    const double sum = value + 12582912.0;
    const float stored = static_cast<float>(sum);
    uint32_t bits = 0;
    std::memcpy(&bits, &stored, sizeof bits);
    return bits + 0xB4C00000u;
}

void __fastcall UpdateTrips(char* part, void*)
{
    static bool logged = false;
    FirstCall("UpdateTrips", logged);
    char* const lot = part - field::kTrafficSource;

    uint32_t x = 0;
    uint32_t z = 0;
    Virtual<void>(lot, slot::kGetLocation, &x, &z);
    uint16_t amounts[3] = {0, 0, 0};
    uint16_t total = 0;
    uint8_t type = 0;

    const uint32_t zone = Virtual<uint32_t>(lot, slot::kGetZoneType);
    if (zone >= 1 && zone <= 3) {
        // Residential: the first wealth with travellers.
        type = 0;
        if (Virtual<uint8_t>(lot, slot::kIsInLivableState) != 0) {
            float factor = 0.0f;
            factor = *Virtual<float*>(Global<void*>(addr::SC4_Service43DA4), 0x78, &factor, x, z);
            uint32_t wealth = 1;
            uint16_t travellers = Virtual<uint16_t>(lot, slot::kGetTravelPopulation, 0x1010u);
            if (travellers == 0) {
                travellers = Virtual<uint16_t>(lot, slot::kGetTravelPopulation, 0x1020u);
                if (travellers != 0) {
                    type = 1;
                    wealth = 2;
                } else {
                    travellers = Virtual<uint16_t>(lot, slot::kGetTravelPopulation, 0x1030u);
                    if (travellers != 0) {
                        type = 2;
                        wealth = 3;
                    }
                }
            }
            if (travellers != 0) {
                // The travellers times the factor, rounded down, at least 1.
                const double scaled = static_cast<double>(static_cast<int32_t>(travellers)) * static_cast<double>(factor);
                uint32_t trips = RoundBits(scaled - 0.5);
                if (static_cast<double>(static_cast<int32_t>(trips + 1)) == scaled)
                    trips += 1;
                if (trips < 1)
                    trips = 1;
                total = static_cast<uint16_t>(trips);

                // The traffic simulator's travel strategy shares for the
                // wealth, into a vector of three floats.
                struct {
                    float* begin;
                    float* end;
                    float* capacity;
                } shares{};
                auto* const memory = static_cast<float*>(Allocate(0xC));
                shares.capacity = memory + 3;
                shares.begin = memory;
                float* fill = memory;
                for (int i = 0; i < 3; ++i, ++fill) {
                    if (fill)
                        *fill = 0.0f;
                }
                shares.end = fill;
                Virtual<void>(Global<void*>(addr::SC4_Service43DA8), 0x68, wealth, &shares);
                float* const share = shares.begin;
                if (total < 0x32) {
                    // Few travellers all use one strategy, picked by the
                    // lot's random byte against the running percentages.
                    const uint8_t random = At<uint8_t>(lot, field::kRandom);
                    uint8_t limit = 1;
                    for (int i = 0; i < 3; ++i) {
                        limit = static_cast<uint8_t>(
                            limit + static_cast<uint8_t>(RoundBits(static_cast<double>(share[i]) * 100.0)));
                        if (random < limit) {
                            amounts[i] = total;
                            break;
                        }
                    }
                } else {
                    const auto whole = static_cast<double>(static_cast<int32_t>(total));
                    for (int i = 0; i < 3; ++i)
                        amounts[i] = static_cast<uint16_t>(RoundBits(whole * static_cast<double>(share[i])));
                }
                if (share)
                    Free(share);
            }
        }
    } else if (zone >= 7 && zone <= 9) {
        // Industrial: freight, for a lot with road access and no designate.
        if (Virtual<uint8_t>(lot, slot::kHasRoadAccess) == 0)
            return;
        if (Virtual<void*>(lot, slot::kGetTravelDesignate) != nullptr)
            return;
        type = 3;
        if (Virtual<uint8_t>(lot, slot::kIsInLivableState) != 0) {
            void* const traffic = Global<void*>(addr::SC4_Service43DA8);
            uint32_t jobs = Virtual<uint16_t>(lot, slot::kGetTravelPopulation, 0x4400u);
            jobs += Virtual<uint16_t>(lot, slot::kGetTravelPopulation, 0x4300u);
            jobs += Virtual<uint16_t>(lot, slot::kGetTravelPopulation, 0x4200u);
            jobs += Virtual<uint16_t>(lot, slot::kGetTravelPopulation, 0x4100u);
            const float workers = static_cast<float>(static_cast<int32_t>(jobs));
            // The freight factor (ST0) times the jobs, rounded up.
            const double scaled = Virtual<double>(traffic, 0x6C) * static_cast<double>(workers);
            uint32_t trips = RoundBits(0.5 + scaled);
            if (static_cast<double>(static_cast<int32_t>(trips - 1)) == scaled)
                trips -= 1;
            amounts[2] = static_cast<uint16_t>(trips);
            total = static_cast<uint16_t>(trips);
        }
    } else {
        return;
    }

    char* const list = lot + field::kTrips;
    char* const head = At<char*>(list, 0x0);

    if (total == 0) {
        // Nothing to send: one placeholder record, unless the list starts
        // with one already.
        if (At<char*>(head, node::kNext) != head && (At<uint8_t>(At<char*>(head, node::kNext), node::kFlags) & 2) != 0)
            return;
        CallThis<void>(addr::SC4TripList_Clear, list);
        // The game leaves +0x20, +0x22 and +0x28 of the record as its stack
        // had them; zeros here.
        TripRecord record{};
        record.count = 1;
        record.type = type;
        record.index = 0;
        record.flags = 2;
        record.kind = 0;
        CallThis<void, const void*>(addr::cSC4Lot_Fn_006C1E10, list, &record);
        return;
    }

    for (char* trip = At<char*>(head, node::kNext); trip != head;) {
        if ((At<uint8_t>(trip, node::kFlags) & 2) != 0)
            trip = EraseTrip(trip);
        else
            trip = At<char*>(trip, node::kNext);
    }

    for (uint32_t index = 0; index < 3; ++index) {
        const uint32_t wanted = amounts[index];
        uint32_t present = 0;
        for (char* trip = At<char*>(head, node::kNext); trip != head;) {
            if (At<uint8_t>(trip, node::kType) != type) {
                trip = EraseTrip(trip);
                continue;
            }
            if (At<uint8_t>(trip, node::kIndex) == index)
                present += At<uint32_t>(trip, node::kCount);
            trip = At<char*>(trip, node::kNext);
        }

        if (present < wanted) {
            auto missing = static_cast<uint16_t>(wanted - present);
            bool known = false;
            for (char* trip = At<char*>(head, node::kNext); trip != head && missing != 0; trip = At<char*>(trip, node::kNext)) {
                if (At<uint8_t>(trip, node::kIndex) != index)
                    continue;
                known = true;
                const uint8_t kind = At<uint8_t>(trip, node::kKind);
                if (kind == 4 || kind == 5) {
                    At<uint32_t>(trip, node::kCount) += missing;
                    missing = 0;
                }
            }
            if (missing == 0)
                continue;
            // A new record at the end of the list. As above, the game leaves
            // +0x20, +0x22 and +0x28 as its stack had them.
            TripRecord record{};
            record.count = missing;
            record.type = type;
            record.index = static_cast<uint8_t>(index);
            record.flags = 0;
            record.kind = known ? 4 : 0;
            char* const end = At<char*>(list, 0x0);
            char* const created = static_cast<char*>(Allocate(node::kSize));
            CallThis<void*, const void*>(addr::cSC4Lot_Fn_006C0AE0, created + 0x8, &record);
            char* const last = At<char*>(end, node::kPrevious);
            At<char*>(created, node::kNext) = end;
            At<char*>(created, node::kPrevious) = last;
            At<char*>(last, node::kNext) = created;
            At<char*>(end, node::kPrevious) = created;
        } else if (present > wanted) {
            auto excess = static_cast<uint16_t>(present - wanted);
            // First from the records of kind 4, then from any.
            for (int pass = 0; pass < 2; ++pass) {
                for (char* trip = At<char*>(head, node::kNext); trip != head && excess != 0;) {
                    if (At<uint8_t>(trip, node::kIndex) != index || (pass == 0 && At<uint8_t>(trip, node::kKind) != 4)) {
                        trip = At<char*>(trip, node::kNext);
                        continue;
                    }
                    const uint32_t count = At<uint32_t>(trip, node::kCount);
                    if (count > excess) {
                        At<uint32_t>(trip, node::kCount) = count - excess;
                        excess = 0;
                        trip = At<char*>(trip, node::kNext);
                    } else {
                        excess = static_cast<uint16_t>(excess - static_cast<uint16_t>(count));
                        trip = EraseTrip(trip);
                    }
                }
            }
        }
    }
}

// --- The holder part's Read thunk (0x006C3AF0) -------------------------------------

// Slot 24 of the property holder part (lot + 8): the lot's Read, which the
// part at lot + 4 has in its slot 4. The game's thunk jumps to 0x006C35F0
// itself; through the slot it reaches whichever Read is installed.
uint8_t __fastcall ReadThunk(char* holder, void*, void* stream)
{
    static bool logged = false;
    FirstCall("ReadThunk", logged);
    return Virtual<uint8_t>(holder - 4, 0x10, stream);
}

// --- Read (0x006C35F0) --------------------------------------------------------------

// Slot 4 of the cIGZSerializable part (`this` = lot + 4): a lot's save
// record, versions up to 8. Stream slots: 5 (+0x14) uint8, 7 (+0x1C) uint16,
// 8 (+0x20) sint32, 9 (+0x24) uint32, 12 (+0x30) float, 18 (+0x48) the error.
//   uint16 version; uint32 lot configuration ID (+0x30); uint8 flags;
//   the cell rectangle, the location, the size (+0x7C, +0x7D): uint8 each
//   (sint32 each before version 6), with altitude, slope at construction
//   and slope (floats) between location and size; uint8 facing, state,
//   condition, zone type, wealth; [before version 6: bool vertices locked];
//   uint32 day of the last state change, building type; [before version 6:
//   bool needs power, has power, historical]; uint8 vacate reason;
//   references: the business unit, the travel designate; uint8 n, then per
//   populations map uint8 m and m x (uint32 developer type, uint16 count);
//   uint8 m and m x (uint32, uint16) of the travel population; three
//   floats travel jobs; the property holder; [from version 7: uint32 n and
//   n trip records]; [from version 8: uint8 the random byte].
// 1 when the stream has no error; 0 for a newer version or a trip record
// that does not read.
uint8_t __fastcall Read(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Read", logged);
    char* const lot = part - field::kSerializable;
    auto u8 = [stream](void* to) { Virtual<uint8_t>(stream, 0x14, to); };
    auto u16 = [stream](void* to) { Virtual<uint8_t>(stream, 0x1C, to); };
    auto s32 = [stream](void* to) { Virtual<uint8_t>(stream, 0x20, to); };
    auto u32 = [stream](void* to) { Virtual<uint8_t>(stream, 0x24, to); };
    auto f32 = [stream](void* to) { Virtual<uint8_t>(stream, 0x30, to); };
    // A byte field that older versions saved as an int32.
    auto small = [&](std::size_t offset, bool old) {
        if (old) {
            int32_t value = 0;
            s32(&value);
            At<uint8_t>(lot, offset) = static_cast<uint8_t>(value);
        } else {
            u8(lot + offset);
        }
    };
    auto boolean = [stream]() {
        uint32_t value = 0; // the game's temporary is a dword of its stack; only the low byte is read
        CallCdecl<void*, void*, void*>(addr::SC4Stream_ReadBool, stream, &value);
        return value;
    };

    At<uint8_t>(lot, field::kInitialized) = 1;
    uint16_t version = 0;
    u16(&version);
    if (version > 8)
        return 0;
    const bool old = version < 6;
    u32(lot + field::kConfigId);
    u8(lot + field::kFlags);
    if (old) {
        // All four are read before any is stored.
        int32_t rect[4] = {0, 0, 0, 0};
        for (int32_t& value : rect)
            s32(&value);
        At<uint8_t>(lot, field::kMinX) = static_cast<uint8_t>(rect[0]);
        At<uint8_t>(lot, field::kMaxX) = static_cast<uint8_t>(rect[2]);
        At<uint8_t>(lot, field::kMinZ) = static_cast<uint8_t>(rect[1]);
        At<uint8_t>(lot, field::kMaxZ) = static_cast<uint8_t>(rect[3]);
    } else {
        u8(lot + field::kMinX);
        u8(lot + field::kMinZ);
        u8(lot + field::kMaxX);
        u8(lot + field::kMaxZ);
    }
    small(field::kCellX, old);
    small(field::kCellZ, old);
    f32(lot + field::kAltitude);
    f32(lot + field::kBuiltSlope);
    f32(lot + field::kSlope);
    small(field::kWidth, old);
    small(field::kDepth, old);
    u8(lot + field::kFacing);
    u8(lot + field::kState);
    u8(lot + field::kCondition);
    u8(lot + field::kZoneType);
    uint8_t wealth = 0;
    u8(&wealth);
    At<uint8_t>(lot, field::kWealth) = wealth;
    if (old)
        Virtual<void>(lot, slot::kSetVerticesAreLocked, boolean());
    u32(lot + field::kStateDay);
    u32(lot + field::kBuildingType);
    if (old) {
        const bool needsPower = static_cast<uint8_t>(boolean()) != 0;
        uint8_t flags = At<uint8_t>(lot, field::kFlags);
        flags = needsPower ? static_cast<uint8_t>(flags | 0x08) : static_cast<uint8_t>(flags & 0xF7);
        At<uint8_t>(lot, field::kFlags) = flags;
        Virtual<void>(lot, slot::kSetHasPower, boolean());
        Virtual<void>(lot, slot::kSetHistorical, boolean());
    }
    u8(lot + field::kVacateReason);

    CallCdecl<uint8_t, void*, uint32_t, void*, uint32_t>(addr::SC4Stream_ReadReference, stream, kIidBusinessUnit,
                                                         lot + field::kUnit, 0u);
    if (void* const designate = At<void*>(lot, field::kDesignate)) {
        At<void*>(lot, field::kDesignate) = nullptr;
        Virtual<uint32_t>(designate, 0x8);
    }
    CallCdecl<uint8_t, void*, uint32_t, void*, uint32_t>(addr::SC4Stream_ReadReference, stream, kIidLot,
                                                         lot + field::kDesignate, 0u);

    uint8_t maps = 0;
    u8(&maps);
    // Through the game's function, so that the call is the same whether the
    // state group's detour on it is installed or not.
    CallThis<void, uint32_t>(addr::cSC4LotPopulations_Resize, lot + field::kPopulations, uint32_t{maps});
    for (int32_t i = 0; i < static_cast<int32_t>(maps); ++i) {
        uint8_t entries = 0;
        u8(&entries);
        for (int32_t j = 0; j < static_cast<int32_t>(entries); ++j) {
            uint32_t key = 0;
            uint16_t count = 0;
            u32(&key);
            u16(&count);
            char* const map = At<char*>(lot, field::kPopulations) + static_cast<std::size_t>(i) * kMapSize;
            *CallThis<uint16_t*, const uint32_t*>(addr::SC4Uint32Uint16Map_Index, map, &key) = count;
        }
    }
    uint8_t travel = 0;
    u8(&travel);
    for (int32_t j = 0; j < static_cast<int32_t>(travel); ++j) {
        uint32_t key = 0;
        uint16_t count = 0;
        u32(&key);
        u16(&count);
        *CallThis<uint16_t*, const uint32_t*>(addr::SC4Uint32Uint16Map_Index, lot + field::kTravel, &key) = count;
    }
    f32(lot + field::kTravelJobs);
    f32(lot + field::kTravelJobs + 4);
    f32(lot + field::kTravelJobs + 8);
    CallThis<uint8_t, void*>(addr::SC4PropertyHolder_Read, lot + field::kHolder, stream);

    if (version > 6) {
        uint32_t trips = 0;
        u32(&trips);
        for (uint32_t i = 0; i < trips; ++i) {
            // The game zeroes the two vectors only; what its reader leaves
            // alone (+0x28 before the record's version 2) stays as its stack
            // had it. Zeros here.
            TripRecord record{};
            if (CallThis<uint8_t, void*>(addr::SC4TripRecord_Read, &record, stream) == 0) {
                if (record.b[0])
                    Free(record.b[0]);
                if (record.a[0])
                    Free(record.a[0]);
                return 0;
            }
            char* const end = At<char*>(lot, field::kTrips);
            char* const created = static_cast<char*>(Allocate(node::kSize));
            auto* const copy = reinterpret_cast<TripRecord*>(created + 0x8);
            CallThis<void*, const void*>(addr::SC4ByteVector_CopyConstruct, copy->a, record.a);
            CallThis<void*, const void*>(addr::SC4ByteVector_CopyConstruct, copy->b, record.b);
            copy->count = record.count;
            copy->type = record.type;
            copy->index = record.index;
            copy->flags = record.flags;
            copy->kind = record.kind;
            copy->word20 = record.word20;
            copy->word22 = record.word22;
            copy->float24 = record.float24;
            copy->dword28 = record.dword28;
            char* const last = At<char*>(end, node::kPrevious);
            At<char*>(created, node::kNext) = end;
            At<char*>(created, node::kPrevious) = last;
            At<char*>(last, node::kNext) = created;
            At<char*>(end, node::kPrevious) = created;
            if (record.b[0])
                Free(record.b[0]);
            if (record.a[0])
                Free(record.a[0]);
        }
    }
    if (version > 7)
        u8(lot + field::kRandom);
    return Virtual<uint8_t>(stream, 0x48) == 0 ? 1 : 0;
}

// --- Installation -------------------------------------------------------------------

struct Slot {
    std::uintptr_t vtable;
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(vtable, index, original, fn) {vtable, index, original, reinterpret_cast<void*>(&fn)}

const Slot kSlots[] = {
    OPENSC4_SLOT(addr::cSC4Lot_vftable, 34, addr::cSC4Lot_SetState, SetState),
    OPENSC4_SLOT(addr::cSC4Lot_vftable, 36, addr::cSC4Lot_SetCondition, SetCondition),
    OPENSC4_SLOT(addr::cSC4Lot_vftable_1C, 4, addr::cSC4Lot_UpdateTrips, UpdateTrips),
    OPENSC4_SLOT(addr::cSC4Lot_vftable_8, 24, addr::cSC4Lot_ReadThunk, ReadThunk),
};

#undef OPENSC4_SLOT

using ResizeFn = void(__fastcall*)(MapVector*, void*, uint32_t);
ResizeFn g_originalResize = nullptr;

// Checks that every slot still holds the original, detours the populations'
// resize (the lot's Read calls it), then swaps the slots.
bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    for (const Slot& slot : kSlots) {
        const std::uintptr_t held = reinterpret_cast<std::uintptr_t*>(slot.vtable)[slot.index];
        if (held != slot.original) {
            log::Write("citylot state: slot %d of %08X holds %08X, expected %08X; not installing", slot.index,
                       static_cast<unsigned>(slot.vtable), static_cast<unsigned>(held),
                       static_cast<unsigned>(slot.original));
            return false;
        }
    }
    const addr::Entry* const resize = addr_check::Find(addr::cSC4LotPopulations_Resize);
    if (!resize || !hook::Install(*resize, &ResizeMaps, &g_originalResize))
        return false;
    for (const Slot& slot : kSlots) {
        std::uintptr_t* const entry = reinterpret_cast<std::uintptr_t*>(slot.vtable) + slot.index;
        DWORD old = 0;
        if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &old))
            return false;
        *entry = reinterpret_cast<std::uintptr_t>(slot.ours);
        VirtualProtect(entry, sizeof(*entry), old, &old);
    }
    log::Write("citylot state: %u slots and the populations' resize are ours",
               static_cast<unsigned>(sizeof(kSlots) / sizeof(kSlots[0])));
    return true;
}

const bool g_registered = registry::Register(
    {"simulators.citylot.state", "simulators/citylot", registry::Kind::Vtable, registry::Phase::PostAppInit,
     addr::cSC4Lot_vftable,
     "Our cSC4Lot SetState, SetCondition, the trip list update of its traffic source part, the holder part's "
     "Read thunk and the populations' resize (src/core/citylot_state.cpp)",
     false, Install});

// The lot's Read, in a group of its own (simulators.citylot.load): the object
// part's save group (simulators.citylot.save) has Write only, so this is the
// only Read of ours.
bool InstallRead(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    std::uintptr_t* const entry = reinterpret_cast<std::uintptr_t*>(addr::cSC4Lot_vftable_4) + 4;
    if (*entry != addr::cSC4Lot_Fn_006C35F0) {
        log::Write("citylot state: slot 4 of %08X holds %08X, expected %08X; Read not installed",
                   static_cast<unsigned>(addr::cSC4Lot_vftable_4), static_cast<unsigned>(*entry),
                   static_cast<unsigned>(addr::cSC4Lot_Fn_006C35F0));
        return false;
    }
    DWORD old = 0;
    if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &old))
        return false;
    *entry = reinterpret_cast<std::uintptr_t>(&Read);
    VirtualProtect(entry, sizeof(*entry), old, &old);
    log::Write("citylot state: Read is ours");
    return true;
}

const bool g_registeredRead = registry::Register(
    {"simulators.citylot.load", "simulators/citylot", registry::Kind::Vtable, registry::Phase::PostAppInit,
     addr::cSC4Lot_vftable_4, "Our cSC4Lot Read: a lot's save record (src/core/citylot_state.cpp)", false,
     InstallRead});

} // namespace
} // namespace opensc4::citylot_sim
