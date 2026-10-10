// cSC4DisasterLayer (class 0x61F6ABF5; docs/disasters.md): the city's
// disaster managers, the count of disasters under way, where they are, and
// what they have cost. Replacement, off by default:
//
//   simulators.disasters.layer: the cISC4DisasterLayer methods (enabling,
//     the managers, a disaster's start and end, the locations, the camera's
//     next disaster), Init and Shutdown, the messages (the cost of what a
//     disaster destroys, the response timers) and QueryInterface.
//
// GetDisasterCost (4 bytes) and the map, vector and stopwatch helpers stay
// the game's. `layer` is the object; the interface is at +8 and the message
// target at +0xC.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstdint>
#include <utility>

namespace opensc4::disasters {
namespace {

namespace field {
constexpr std::size_t kTimersA = 0x10;   // map: key -> stopwatch (messages with data 1 or 5)
constexpr std::size_t kTimersB = 0x1C;   // map: key -> stopwatch (data 0 or 6)
constexpr std::size_t kStopwatch = 0x28; // since the first disaster began
constexpr std::size_t kDisabled = 0x40;  // count of disable requests
constexpr std::size_t kInited = 0x44;
constexpr std::size_t kOngoing = 0x48;
constexpr std::size_t kRecords = 0xA0;   // vector of {string, int64}, 0x20 each
constexpr std::size_t kShareA = 0xAC;    // the timers' share of the disaster's time
constexpr std::size_t kShareB = 0xB0;
constexpr std::size_t kManagers = 0xB8;  // map: disaster type -> manager
constexpr std::size_t kNext = 0xC8;      // the location the camera goes to next
constexpr std::size_t kLocations = 0xCC; // vector of Location
} // namespace field

using ThisFn = void(__thiscall*)(void* self);

struct Location {
    void* instance;
    int32_t x;
    int32_t z;
    int32_t zoom;
    void* occupant;
};
static_assert(sizeof(Location) == 0x14);

struct Node { // std::map node
    uint8_t color;
    uint8_t pad[3];
    Node* parent;
    Node* left;
    Node* right;
    uint32_t key;
    void* value;
};

Node* Next(Node* node)
{
    if (Node* right = node->right) {
        while (right->left)
            right = right->left;
        return right;
    }
    Node* parent = node->parent;
    while (node == parent->right) {
        node = parent;
        parent = parent->parent;
    }
    return node->right == parent ? node : parent;
}

void Release(void* object)
{
    if (object)
        Virtual<uint32_t>(object, 0x8);
}

Location*& Begin(char* layer)
{
    return At<Location*>(layer, field::kLocations);
}

Location*& End(char* layer)
{
    return At<Location*>(layer, field::kLocations + 4);
}

int32_t Count(char* layer)
{
    return static_cast<int32_t>(End(layer) - Begin(layer));
}

constexpr uint32_t kMessages[] = {0x99EF1143, 0xE94671EC, 0xE94671EB, 0x894671F6, 0xE94671ED};

void* NewMessage()
{
    void* const memory = reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::RZ_operator_new)(0x2C);
    void* const message = memory ? reinterpret_cast<void*(__thiscall*)(void*)>(addr::cGZMessage_cGZMessage)(memory)
                                 : nullptr;
    Virtual<uint32_t>(message, 0x4);
    return message;
}

// The game's 0x005774C0: a message with three data values, posted.
void __fastcall Post(char*, void*, uint32_t type, int32_t one, int32_t two, int32_t three)
{
    void* const server = Global<void*>(addr::SC4_MessageServer);
    if (!server)
        return;
    void* const message = NewMessage();
    Virtual<void>(message, 0x14, type);
    Virtual<void>(message, 0x2C, one);
    Virtual<void>(message, 0x40, two);
    Virtual<void>(message, 0x54, three);
    Virtual<void>(server, 0x10, message, uint32_t{0});
    Virtual<uint32_t>(message, 0x8);
}

// The game's 0x00577540: a location's cell from its occupant (if it is
// still on the network service's books), or else from the disaster.
void __fastcall RefreshLocation(char*, void*, Location* location)
{
    if (location->occupant &&
        Virtual<uint8_t>(Global<void*>(addr::SC4_NetworkService), 0x6C, location->occupant, 0, 0, 0)) {
        float position[3];
        Virtual<void>(location->occupant, 0x20, position);
        int32_t x;
        int32_t z;
        Virtual<void>(Global<void*>(addr::SC4_City), 0x17C, position[0], position[2], &x, &z);
        location->x = x;
        location->z = z;
        return;
    }
    if (!location->occupant) {
        int32_t x;
        int32_t z;
        if (Virtual<uint8_t>(location->instance, 0x18, &x, &z)) {
            location->x = x;
            location->z = z;
        }
    }
}

uint32_t __fastcall DisastersEnabled(char* interface, void*)
{
    return At<int32_t>(interface - 0x8, field::kDisabled) == 0;
}

uint32_t __fastcall EnableDisasters(char* interface, void*, uint8_t enable)
{
    int32_t& disabled = At<int32_t>(interface - 0x8, field::kDisabled);
    disabled += enable ? -1 : 1;
    if (disabled < 0)
        disabled = 0;
    return Virtual<uint32_t>(interface, 0x18);
}

uint32_t __fastcall IsADisasterOngoing(char* interface, void*)
{
    return At<uint32_t>(interface - 0x8, field::kOngoing) != 0;
}

uint8_t __fastcall QueryInterface(char* layer, void*, uint32_t iid, void** out)
{
    *out = nullptr;
    void* part = nullptr;
    if (iid == 0x29B02A2C)
        part = layer ? layer + 0x8 : nullptr;
    else if (iid == 0x452294AA)
        part = layer ? layer + 0xC : nullptr;
    else
        return reinterpret_cast<uint8_t(__thiscall*)(void*, uint32_t, void**)>(addr::cRZBaseUnknown_QueryInterface1)(
            layer, iid, out);
    *out = part;
    if (!part)
        return reinterpret_cast<uint8_t(__thiscall*)(void*, uint32_t, void**)>(addr::cRZBaseUnknown_QueryInterface1)(
            layer, iid, out);
    Virtual<uint32_t>(layer, 0x4);
    return 1;
}

uint8_t __fastcall GetMostRecentDisasterLocation(char* interface, void*, void* instance, int32_t* x, int32_t* z)
{
    char* const layer = interface - 0x8;
    int32_t index = Count(layer);
    if (!index)
        return 0;
    --index;
    if (instance) {
        while (index >= 0 && Begin(layer)[index].instance != instance)
            --index;
        if (index < 0)
            return 0;
    }
    Location* const location = &Begin(layer)[index];
    reinterpret_cast<void(__thiscall*)(void*, Location*)>(addr::cSC4DisasterLayer_RefreshLocation)(layer, location);
    if (x)
        *x = location->x;
    if (z)
        *z = location->z;
    return 1;
}

int32_t __fastcall GetDisasterLocationCount(char* interface, void*)
{
    return Count(interface - 0x8);
}

uint8_t __fastcall GetDisasterLocationByIndex(char* interface, void*, int32_t index, void** instance, int32_t* x,
                                              int32_t* z)
{
    char* const layer = interface - 0x8;
    if (index < 0 || index >= Count(layer))
        return 0;
    Location* const location = &Begin(layer)[index];
    if (!location->instance)
        return 0;
    *instance = location->instance;
    Virtual<uint32_t>(location->instance, 0x4);
    if (x)
        *x = location->x;
    if (z)
        *z = location->z;
    return 1;
}

// The location at `index` taken out of the vector (the game's range move
// 0x00624070 closes the gap).
void EraseLocation(char* layer, int32_t index)
{
    Location* const location = &Begin(layer)[index];
    Location* const end = End(layer);
    if (location + 1 != end) {
        void* tag = nullptr;
        reinterpret_cast<void*(__cdecl*)(Location*, Location*, Location*, void*, uint32_t)>(
            addr::SC4LocationVector_Move)(location + 1, end, location, &tag, 0);
    }
    End(layer) = End(layer) - 1;
}

uint8_t __fastcall UnregisterDisasterLocations(char* interface, void*, void* instance)
{
    char* const layer = interface - 0x8;
    uint8_t found = 0;
    for (int32_t index = Count(layer) - 1; index >= 0; --index) {
        Location* const location = &Begin(layer)[index];
        if (instance && location->instance != instance)
            continue;
        Release(location->occupant);
        EraseLocation(layer, index);
        found = 1;
    }
    return found;
}

uint8_t __fastcall UnregisterDisasterLocationByOccupant(char* interface, void*, void* instance, void* occupant)
{
    char* const layer = interface - 0x8;
    uint8_t found = 0;
    for (int32_t index = Count(layer) - 1; index >= 0; --index) {
        Location* const location = &Begin(layer)[index];
        if (location->instance != instance || location->occupant != occupant)
            continue;
        Virtual<uint32_t>(location->occupant, 0x8);
        EraseLocation(layer, index);
        found = 1;
    }
    return found;
}

uint8_t __fastcall RegisterDisasterLocation(char* interface, void*, void* instance, int32_t x, int32_t z,
                                            void* occupant, int32_t zoom, uint8_t teleport)
{
    char* const layer = interface - 0x8;
    Location location{instance, x, z, zoom, occupant};
    if (occupant)
        Virtual<uint32_t>(occupant, 0x4);
    Location*& end = End(layer);
    if (end != At<Location*>(layer, field::kLocations + 8)) {
        if (end)
            *end = location;
        ++end;
    } else {
        reinterpret_cast<void(__thiscall*)(void*, Location*, const Location*, void*, uint32_t, uint32_t)>(
            addr::SC4LocationVector_InsertN)(layer + field::kLocations, end, &location, &teleport, 1, 1);
    }
    At<int32_t>(layer, field::kNext) = Count(layer) - 1;
    if (teleport && At<uint8_t>(Global<char*>(addr::SC4_Options43CD8), 0xF08))
        Virtual<uint8_t>(interface, 0x2C); // GoToNextDisaster
    return 1;
}

void* __fastcall GetDisasterManager(char* interface, void*, uint32_t type)
{
    char* const map = interface - 0x8 + field::kManagers;
    Node* found;
    reinterpret_cast<void(__thiscall*)(void*, Node**, const uint32_t*)>(addr::SC4Uint32Map_Find)(map, &found, &type);
    return found == At<Node*>(map, 0x0) ? nullptr : found->value;
}

// The game's 0x00578D80: the cost zeroed, its records dropped.
void __fastcall ResetCost(char* layer, void*)
{
    At<uint32_t>(layer, 0x50) = 0; // the cost: what GetDisasterCost returns
    At<int64_t>(layer, 0x58) = 0;
    for (std::size_t at = 0x60; at <= 0x90; at += 0x10) {
        At<uint32_t>(layer, at) = 0;
        At<int64_t>(layer, at + 8) = 0;
    }
    char* const begin = At<char*>(layer, field::kRecords);
    char* const end = At<char*>(layer, field::kRecords + 4);
    uint8_t tag;
    char* const newEnd = reinterpret_cast<char*(__cdecl*)(char*, char*, char*, uint8_t*)>(
        addr::SC4CostRecords_Copy)(end, end, begin, &tag);
    reinterpret_cast<void(__cdecl*)(char*, char*, uint8_t*)>(addr::SC4CostRecords_Destroy)(
        newEnd, At<char*>(layer, field::kRecords + 4), &tag);
    At<char*>(layer, field::kRecords + 4) = newEnd;
}

// A timer map emptied: its stopwatches deleted, its nodes freed.
void ClearTimerMap(char* map)
{
    Node* const header = At<Node*>(map, 0x0);
    for (Node* node = header->left; node != At<Node*>(map, 0x0); node = Next(node)) {
        if (void* const stopwatch = node->value) {
            reinterpret_cast<ThisFn>(addr::SC4Stopwatch_Destruct)(stopwatch);
            reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_operator_delete)(stopwatch);
        }
    }
    if (At<uint32_t>(map, 0x4)) {
        Node* const head = At<Node*>(map, 0x0);
        reinterpret_cast<void(__thiscall*)(void*, Node*)>(addr::cSC4DisasterLayer_EraseTimerSubtree)(map, head->parent);
        At<Node*>(map, 0x0)->left = At<Node*>(map, 0x0);
        At<Node*>(map, 0x0)->parent = nullptr;
        At<Node*>(map, 0x0)->right = At<Node*>(map, 0x0);
        At<uint32_t>(map, 0x4) = 0;
    }
}

// The game's 0x00577EF0.
void __fastcall ClearTimers(char* layer, void*)
{
    ClearTimerMap(layer + field::kTimersA);
    ClearTimerMap(layer + field::kTimersB);
    reinterpret_cast<ThisFn>(addr::SC4Stopwatch_Reset)(layer + field::kStopwatch);
}

float Elapsed(void* stopwatch)
{
    float seconds = 0;
    reinterpret_cast<void(__thiscall*)(void*, float*)>(addr::SC4Stopwatch_GetElapsed)(stopwatch, &seconds);
    return seconds;
}

// A timer map's stopwatches stopped; their time added up (in floats, as
// the game does).
float StopTimers(char* map)
{
    float sum = 0;
    Node* const header = At<Node*>(map, 0x0);
    for (Node* node = header->left; node != At<Node*>(map, 0x0); node = Next(node)) {
        reinterpret_cast<ThisFn>(addr::SC4Stopwatch_Stop)(node->value);
        float seconds = 0;
        reinterpret_cast<void(__thiscall*)(void*, float*)>(addr::SC4Stopwatch_GetElapsed)(node->value, &seconds);
        sum = static_cast<float>(static_cast<double>(sum) + static_cast<double>(seconds));
    }
    return sum;
}

// The game's 0x00577AF0: the timers' share of the time since the first
// disaster began, per unit of 0x00B43D78's slot 0x64 and the police
// simulator's slot 0x68.
void __fastcall Shares(char* layer, void*)
{
    reinterpret_cast<ThisFn>(addr::SC4Stopwatch_Stop)(layer + field::kStopwatch);
    const float total = Elapsed(layer + field::kStopwatch);
    const float sumA = StopTimers(layer + field::kTimersA);
    const float sumB = StopTimers(layer + field::kTimersB);
    void* const first = Global<void*>(addr::SC4_Service43D78);
    if (!first || !Global<void*>(addr::SC4_PoliceSimulator))
        return;
    const uint32_t unitsA = Virtual<uint32_t>(first, 0x64);
    const uint32_t unitsB = Virtual<uint32_t>(Global<void*>(addr::SC4_PoliceSimulator), 0x68);
    At<float>(layer, field::kShareA) =
        unitsA ? static_cast<float>(static_cast<double>(sumA) / (static_cast<double>(unitsA) * static_cast<double>(total)))
               : 0.0f;
    At<float>(layer, field::kShareB) =
        unitsB ? static_cast<float>(static_cast<double>(sumB) / (static_cast<double>(unitsB) * static_cast<double>(total)))
               : 0.0f;
}

// The game's 0x00577CA0: the messages dropped, then the shares.
void __fastcall Unsubscribe(char* layer, void*)
{
    if (void* const server = Global<void*>(addr::SC4_MessageServer)) {
        for (uint32_t type : kMessages)
            Virtual<void>(server, 0x18, layer ? layer + 0xC : nullptr, type);
    }
    reinterpret_cast<ThisFn>(addr::cSC4DisasterLayer_Shares)(layer);
}

// The game's 0x00578F10: the messages, a fresh cost and fresh timers.
void __fastcall Subscribe(char* layer, void*)
{
    if (void* const server = Global<void*>(addr::SC4_MessageServer)) {
        for (uint32_t type : kMessages)
            Virtual<void>(server, 0x14, layer ? layer + 0xC : nullptr, type);
    }
    reinterpret_cast<ThisFn>(addr::cSC4DisasterLayer_ResetCost)(layer);
    reinterpret_cast<ThisFn>(addr::cSC4DisasterLayer_ClearTimers)(layer);
    char* const stopwatch = layer + field::kStopwatch;
    reinterpret_cast<void(__thiscall*)(void*, int32_t)>(addr::SC4Stopwatch_ConstructMode)(stopwatch, 5);
    reinterpret_cast<ThisFn>(addr::SC4Stopwatch_Reset)(stopwatch);
    reinterpret_cast<ThisFn>(addr::SC4Stopwatch_Start)(stopwatch);
}

uint32_t __fastcall DisasterHasBegun(char* interface, void*, void* instance)
{
    char* const layer = interface - 0x8;
    if (++At<uint32_t>(layer, field::kOngoing) == 1) {
        if (void* const simulator = Global<void*>(addr::SC4_SimulatorService))
            Virtual<void>(simulator, 0x2C);
        reinterpret_cast<ThisFn>(addr::cSC4DisasterLayer_Subscribe)(layer);
        if (void* const city = Global<void*>(addr::SC4_City))
            Virtual<void>(city, 0x1A0);
    }
    int32_t x = 0;
    int32_t z = 0;
    const uint32_t result = Virtual<uint8_t>(interface, 0x30, instance, &x, &z); // GetMostRecentDisasterLocation
    void* const server = Global<void*>(addr::SC4_MessageServer);
    if (!server)
        return result;
    void* const message = NewMessage();
    Virtual<void>(message, 0x14, uint32_t{0xCA1B1E64});
    Virtual<void>(message, 0x2C, At<uint32_t>(layer, field::kOngoing));
    Virtual<void>(message, 0x40, x);
    Virtual<void>(message, 0x54, z);
    Virtual<void>(message, 0x68, Virtual<uint32_t>(instance, 0x14)); // the disaster's ID
    Virtual<void>(message, 0x84, instance);
    Virtual<void>(server, 0x10, message, uint32_t{0});
    return Virtual<uint32_t>(message, 0x8);
}

// No argument, though the interface (gzcom-dll) declares the disaster.
void __fastcall DisasterHasEnded(char* interface, void*)
{
    char* const layer = interface - 0x8;
    uint32_t& ongoing = At<uint32_t>(layer, field::kOngoing);
    if (!At<uint8_t>(layer, field::kInited) || ongoing < 1)
        return;
    if (--ongoing)
        return;
    if (void* const city = Global<void*>(addr::SC4_City))
        Virtual<void>(city, 0x19C);
    if (void* const simulator = Global<void*>(addr::SC4_SimulatorService))
        Virtual<void>(simulator, 0x38);
    reinterpret_cast<ThisFn>(addr::cSC4DisasterLayer_Unsubscribe)(layer);
    reinterpret_cast<void(__thiscall*)(void*, uint32_t, int32_t, int32_t, int32_t)>(addr::cSC4DisasterLayer_Post)(
        layer, 0xAA1B2002, 0, 0, 0);
}

uint8_t __fastcall Init(char* interface, void*)
{
    char* const layer = interface - 0x8;
    if (At<uint8_t>(layer, field::kInited))
        return 1;
    At<uint8_t>(layer, field::kInited) = 1;
    At<int32_t>(layer, field::kDisabled) = 0;
    struct Entry {
        uint32_t type;
        uint32_t classId;
    };
    const auto* const table = reinterpret_cast<const Entry*>(addr::cSC4DisasterLayer_ManagerTable);
    for (int i = 0; i < 7; ++i) {
        void* manager = Virtual<void*>(interface, 0x1C, table[i].type); // GetDisasterManager
        if (!manager) {
            void* const com = reinterpret_cast<void*(__cdecl*)()>(addr::RZGetCOM)();
            Virtual<uint8_t>(com, 0xC, table[i].classId, uint32_t{0x021E9CBF}, &manager);
            if (!manager)
                continue;
            uint32_t id = Virtual<uint32_t>(manager, 0x14);
            *reinterpret_cast<void**(__thiscall*)(void*, const uint32_t*)>(addr::SC4Uint32Map_At)(
                layer + field::kManagers, &id) = manager;
            if (!manager)
                continue;
        }
        Virtual<uint8_t>(manager, 0xC);
    }
    reinterpret_cast<ThisFn>(addr::cSC4DisasterLayer_ResetCost)(layer);
    return 1;
}

uint8_t __fastcall Shutdown(char* interface, void*)
{
    char* const layer = interface - 0x8;
    if (!At<uint8_t>(layer, field::kInited))
        return 1;
    At<uint8_t>(layer, field::kInited) = 0;
    reinterpret_cast<ThisFn>(addr::cSC4DisasterLayer_Unsubscribe)(layer);
    char* const map = layer + field::kManagers;
    Node* const header = At<Node*>(map, 0x0);
    for (Node* node = header->left; node != header; node = Next(node)) {
        void* const manager = node->value;
        Virtual<uint8_t>(manager, 0x10);
        Virtual<uint32_t>(manager, 0x8);
    }
    if (At<uint32_t>(map, 0x4)) {
        reinterpret_cast<void(__thiscall*)(void*, Node*)>(addr::cSC4DisasterLayer_EraseManagerSubtree)(map,
                                                                                               At<Node*>(map, 0x0)->parent);
        At<Node*>(map, 0x0)->left = At<Node*>(map, 0x0);
        At<Node*>(map, 0x0)->parent = nullptr;
        At<Node*>(map, 0x0)->right = At<Node*>(map, 0x0);
        At<uint32_t>(map, 0x4) = 0;
    }
    return 1;
}

struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t references;
};

struct CostRecord {
    GameString name;
    uint32_t pad;
    int64_t value;
};
static_assert(sizeof(CostRecord) == 0x20);

// The game's 0x00578BA0, on message 0x99EF1143 (an occupant destroyed):
// its value (property 0x8A1C3E72) added to the cost; a building is also
// counted, by the purposes in its type's flags, and named in the records
// when the flags have 0x1800400.
void __fastcall OnDestroyed(char* layer, void*, void* message)
{
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_City), 0x58))
        return;
    void* const occupant = Virtual<void*>(message, 0x28);
    void* const properties = Virtual<void*>(occupant, 0x18);
    int64_t value = 0;
    const uint32_t type = Virtual<uint32_t>(occupant, 0x1C);
    const uint8_t found = reinterpret_cast<uint8_t(__cdecl*)(void*, uint32_t, int64_t*)>(addr::SC4_GetPropertySint64)(
        properties, 0x8A1C3E72, &value);
    if (type == 0x278128A0) {
        void* building = nullptr;
        if (Virtual<uint8_t>(occupant, 0x0, uint32_t{0x87DDA3A9}, &building)) {
            char* const info = Virtual<char*>(building, 0x2C);
            ++At<uint32_t>(layer, 0x50);
            static const struct {
                uint32_t flag;
                std::size_t at;
            } kPurposes[] = {{0x1, 0x60}, {0x2, 0x70}, {0x4, 0x80}, {0x20, 0x90}};
            for (const auto& purpose : kPurposes) {
                if (At<uint8_t>(info, 0x1C) & purpose.flag) {
                    At<int64_t>(layer, purpose.at + 8) += value;
                    ++At<uint32_t>(layer, purpose.at);
                }
            }
            if (At<uint32_t>(info, 0x1C) & 0x1800400) {
                CostRecord record;
                reinterpret_cast<ThisFn>(addr::SC4String_SC4String)(&record.name);
                record.value = value;
                Virtual<void>(&record.name, 0x34, Virtual<void*>(building, 0x30));
                reinterpret_cast<void(__thiscall*)(void*, CostRecord*)>(addr::SC4CostRecords_PushBack)(
                    layer + field::kRecords, &record);
                record.name.vtable = addr::SC4String_Vtable;
                if (record.name.begin)
                    reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(record.name.begin);
            }
            Virtual<uint32_t>(building, 0x8);
        }
    } else if (type == 0xC772BF98 && !found) {
        value = Global<int64_t>(addr::cSC4DisasterLayer_DefaultCost);
    }
    At<int64_t>(layer, 0x58) += value;
}

// The game's 0x005784B0, on the response messages: a stopwatch per key
// (data 2), in one of two maps by data 1; 0xE94671EC starts it, the others
// stop it.
void __fastcall OnResponse(char* layer, void*, void* message)
{
    const int32_t kind = Virtual<int32_t>(message, 0x24);
    uint32_t key = Virtual<uint32_t>(message, 0x38);
    char* map;
    if (kind == 1 || kind == 5)
        map = layer + field::kTimersA;
    else if (kind == 0 || kind == 6)
        map = layer + field::kTimersB;
    else
        return;
    Node* found;
    reinterpret_cast<void(__thiscall*)(void*, Node**, const uint32_t*)>(addr::SC4Uint32Map_Find)(map, &found, &key);
    const auto slot = [&] {
        return reinterpret_cast<void**(__thiscall*)(void*, const uint32_t*)>(addr::SC4Uint32Map_At)(map, &key);
    };
    void* stopwatch;
    if (found == At<Node*>(map, 0x0)) {
        void* const memory = reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0x18);
        stopwatch = memory ? reinterpret_cast<void*(__thiscall*)(void*, int32_t)>(addr::SC4Stopwatch_Construct)(memory, 4)
                           : nullptr;
        *slot() = stopwatch;
        reinterpret_cast<void(__thiscall*)(void*, int32_t)>(addr::SC4Stopwatch_ConstructMode)(stopwatch, 5);
    } else {
        stopwatch = *slot();
    }
    const auto running = [stopwatch] {
        return reinterpret_cast<uint8_t(__thiscall*)(void*)>(addr::GZStopwatch_IsRunning)(stopwatch) != 0;
    };
    switch (Virtual<uint32_t>(message, 0x10)) {
    case 0xE94671EC:
        if (!running())
            reinterpret_cast<ThisFn>(addr::SC4Stopwatch_Start)(stopwatch);
        break;
    case 0x894671F6:
    case 0xE94671EB:
    case 0xE94671ED:
        if (running())
            reinterpret_cast<ThisFn>(addr::SC4Stopwatch_Stop)(stopwatch);
        break;
    default:
        break;
    }
}

// The message target at +0xC.
uint8_t __fastcall DoMessage(char* target, void*, void* message)
{
    char* const layer = target - 0xC;
    Virtual<uint32_t>(message, 0x10);
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    if (type == 0x99EF1143)
        reinterpret_cast<void(__thiscall*)(void*, void*)>(addr::cSC4DisasterLayer_OnDestroyed)(layer, message);
    else if (type == 0x894671F6 || (type >= 0xE94671EB && type <= 0xE94671ED))
        reinterpret_cast<void(__thiscall*)(void*, void*)>(addr::cSC4DisasterLayer_OnResponse)(layer, message);
    return 1;
}

// The game's 0x00577770: the location at the camera's index, refreshed,
// and the index stepped back (around to the newest).
uint8_t __fastcall NextLocation(char* layer, void*, int32_t* x, int32_t* z, int32_t* zoom)
{
    const int32_t count = Count(layer);
    if (!count)
        return 0;
    int32_t& next = At<int32_t>(layer, field::kNext);
    if (next >= count)
        next = count - 1;
    Location* const location = &Begin(layer)[next];
    reinterpret_cast<void(__thiscall*)(void*, Location*)>(addr::cSC4DisasterLayer_RefreshLocation)(layer, location);
    if (x)
        *x = location->x;
    if (z)
        *z = location->z;
    if (zoom)
        *zoom = location->zoom;
    if (--next < 0)
        next = count - 1;
    return 1;
}

// The camera to the next disaster location, zoomed in to the location's
// zoom when the view is further out (and the option at +0x4BC is on).
uint8_t __fastcall GoToNextDisaster(char* interface, void*)
{
    char* const layer = interface - 0x8;
    int32_t x = 0;
    int32_t z = 0;
    int32_t zoom;
    if (!reinterpret_cast<uint8_t(__thiscall*)(void*, int32_t*, int32_t*, int32_t*)>(addr::cSC4DisasterLayer_NextLocation)(
            layer, &x, &z, &zoom))
        return 0;
    float position[3];
    Virtual<void>(Global<void*>(addr::SC4_City), 0x184, x, z, &position[0], &position[2]);
    position[1] = Virtual<float>(Global<void*>(addr::SC4_Terrain), 0x34, position[0], position[2]);
    char* const camera = Virtual<char*>(Global<void*>(addr::SC4_View43DD0), 0x40);
    if (!camera)
        return 1;
    reinterpret_cast<void(__thiscall*)(void*, const float*)>(addr::cSC4CameraControl_SetCenter)(camera, position);
    const float wanted = static_cast<float>(zoom);
    const float current = static_cast<float>(At<int32_t>(camera, 0x108));
    if (!(static_cast<double>(wanted) > static_cast<double>(current) - 1.0))
        return 1;
    char* const app = Global<char*>(addr::SC4_App43CC4);
    if (!app || !At<uint8_t>(At<char*>(app, 0xC), 0x4BC))
        return 1;
    if (static_cast<float>(zoom - 1) > current)
        reinterpret_cast<void(__thiscall*)(void*, float)>(addr::cSC4CameraControl_SetZoom)(camera, wanted);
    return 1;
}

bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[32] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSC4DisasterLayer_DisastersEnabled, reinterpret_cast<void*>(&DisastersEnabled)},
        {addr::cSC4DisasterLayer_EnableDisasters, reinterpret_cast<void*>(&EnableDisasters)},
        {addr::cSC4DisasterLayer_IsADisasterOngoing, reinterpret_cast<void*>(&IsADisasterOngoing)},
        {addr::cSC4DisasterLayer_QueryInterface, reinterpret_cast<void*>(&QueryInterface)},
        {addr::cSC4DisasterLayer_Post, reinterpret_cast<void*>(&Post)},
        {addr::cSC4DisasterLayer_RefreshLocation, reinterpret_cast<void*>(&RefreshLocation)},
        {addr::cSC4DisasterLayer_GetMostRecentDisasterLocation, reinterpret_cast<void*>(&GetMostRecentDisasterLocation)},
        {addr::cSC4DisasterLayer_GetDisasterLocationCount, reinterpret_cast<void*>(&GetDisasterLocationCount)},
        {addr::cSC4DisasterLayer_NextLocation, reinterpret_cast<void*>(&NextLocation)},
        {addr::cSC4DisasterLayer_GoToNextDisaster, reinterpret_cast<void*>(&GoToNextDisaster)},
        {addr::cSC4DisasterLayer_GetDisasterLocationByIndex, reinterpret_cast<void*>(&GetDisasterLocationByIndex)},
        {addr::cSC4DisasterLayer_Shares, reinterpret_cast<void*>(&Shares)},
        {addr::cSC4DisasterLayer_Unsubscribe, reinterpret_cast<void*>(&Unsubscribe)},
        {addr::cSC4DisasterLayer_UnregisterDisasterLocations, reinterpret_cast<void*>(&UnregisterDisasterLocations)},
        {addr::cSC4DisasterLayer_UnregisterDisasterLocationByOccupant,
         reinterpret_cast<void*>(&UnregisterDisasterLocationByOccupant)},
        {addr::cSC4DisasterLayer_DisasterHasEnded, reinterpret_cast<void*>(&DisasterHasEnded)},
        {addr::cSC4DisasterLayer_ClearTimers, reinterpret_cast<void*>(&ClearTimers)},
        {addr::cSC4DisasterLayer_GetDisasterManager, reinterpret_cast<void*>(&GetDisasterManager)},
        {addr::cSC4DisasterLayer_Shutdown, reinterpret_cast<void*>(&Shutdown)},
        {addr::cSC4DisasterLayer_RegisterDisasterLocation, reinterpret_cast<void*>(&RegisterDisasterLocation)},
        {addr::cSC4DisasterLayer_OnResponse, reinterpret_cast<void*>(&OnResponse)},
        {addr::cSC4DisasterLayer_OnDestroyed, reinterpret_cast<void*>(&OnDestroyed)},
        {addr::cSC4DisasterLayer_ResetCost, reinterpret_cast<void*>(&ResetCost)},
        {addr::cSC4DisasterLayer_DoMessage, reinterpret_cast<void*>(&DoMessage)},
        {addr::cSC4DisasterLayer_Init, reinterpret_cast<void*>(&Init)},
        {addr::cSC4DisasterLayer_Subscribe, reinterpret_cast<void*>(&Subscribe)},
        {addr::cSC4DisasterLayer_DisasterHasBegun, reinterpret_cast<void*>(&DisasterHasBegun)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    static_assert(count <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < count; ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].first);
        if (!found || !hook::Install(*found, hooks[i].second, &originals[i]))
            return false;
    }
    return true;
}

const bool g_registered = registry::Register({
    "simulators.disasters.layer",
    "simulators/disasters",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4DisasterLayer_Init,
    "Our cSC4DisasterLayer (docs/disasters.md)",
    false,
    Install,
});

} // namespace
} // namespace opensc4::disasters
