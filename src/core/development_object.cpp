// Our cSC4BuildingDevelopmentSimulator (build 641; docs/development.md), the
// parts around the simulation: Init with its tuning, Shutdown, the save
// record, and our own object under the class ID. The queries, the occupant
// functions and the messages are in development_queries.cpp,
// development_occupants.cpp and development_messages.cpp.
#include "core/development_sim.h"

#include "core/director.h"
#include "core/game.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cIGZUnknown.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::development_sim {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("development: first %s", name);
}

namespace {

constexpr uint32_t kTuningType = 0x6534284A;
constexpr uint32_t kTuningKey[3] = {kTuningType, 0xE7E2C2DB, 0xE8DA7677};
constexpr uint32_t kIidExemplar = 0xA52160F5;
constexpr uint32_t kIidGrid = 0xC9BD939A;
constexpr uint32_t kIidDemand = 0x0797A4E8;
constexpr uint32_t kIidDeveloper = 0xE7CDD662;
constexpr uint32_t kIidOccupant = 0x26BB767E;
constexpr uint32_t kCommand = 0x0B99AE81;
constexpr uint32_t kMessages[5] = {0xE8BDA2E7, 0x278E6003, 0x99EF1142, 0x99EF1143, kCommand};
constexpr uint32_t kCheats[3] = {0x491865E1, 0x08DAD89A, 0x6AB6A2D7};

using NewFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* block);
using SelfFn = void(__thiscall*)(void* self);
using VoidFn = void(__cdecl*)();
using GetIntFn = uint8_t(__cdecl*)(void* holder, uint32_t property, void* value);
using RefFn = void(__cdecl*)(void* stream, void* object);
using ReadRefFn = uint8_t(__cdecl*)(void* stream, uint32_t iid, void* out, uint32_t zero);
using BoolFn = void(__cdecl*)(void* stream, uint32_t value);
using ReadBoolFn = void(__cdecl*)(void* stream, void* value);
using AtFn = void*(__thiscall*)(void* map, const void* key);

const auto OperatorNew = reinterpret_cast<NewFn>(addr::SC4_operator_new);
const auto OperatorDelete = reinterpret_cast<FreeFn>(addr::SC4_operator_delete);
const auto Allocate = reinterpret_cast<NewFn>(addr::SC4_stl_allocate);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);
const auto GetFloat = reinterpret_cast<GetIntFn>(addr::SC4_GetPropertyFloat);
const auto GetUint32 = reinterpret_cast<GetIntFn>(addr::SC4_GetPropertyUint32);
const auto GetFloats = reinterpret_cast<GetIntFn>(addr::SC4_GetPropertyFloatVector);
const auto WriteRef = reinterpret_cast<RefFn>(addr::SC4Stream_WriteReference);
const auto ReadRef = reinterpret_cast<ReadRefFn>(addr::SC4Stream_ReadReference);
const auto WriteBool = reinterpret_cast<BoolFn>(addr::SC4Stream_WriteBool);
const auto ReadBool = reinterpret_cast<ReadBoolFn>(addr::SC4Stream_ReadBool);

// --- The game's red-black trees (SGI layout) -----------------------------------
//
// A tree is {head, size}; a node is color (uint8) +0x0, parent +0x4, left
// +0x8, right +0xC, the value from +0x10. The head's parent is the root,
// its left the leftmost node, its right the rightmost.

char* Left(char* node) { return At<char*>(node, 0x8); }
char* Right(char* node) { return At<char*>(node, 0xC); }
char* Parent(char* node) { return At<char*>(node, 0x4); }

// The in-order successor, as the game's inline code.
char* Next(char* node)
{
    if (char* x = Right(node)) {
        while (Left(x))
            x = Left(x);
        return x;
    }
    char* y = Parent(node);
    while (node == Right(y)) {
        node = y;
        y = Parent(y);
    }
    if (Right(node) != y)
        node = y;
    return node;
}

// 0x00695CD0, 0x00695D50, 0x00695D90, 0x00695E10 (the same code for four
// node types whose values own nothing): the right subtrees first, then the
// node, down the left spine.
void EraseNodes(char* node)
{
    while (node) {
        EraseNodes(Right(node));
        char* const left = Left(node);
        Free(node);
        node = left;
    }
}

// A tree's head reset to empty, as after each inline clear.
void ResetHead(char* tree)
{
    char* const head = At<char*>(tree, 0x0);
    At<char*>(head, 0x8) = head;
    At<char*>(head, 0x4) = nullptr;
    At<char*>(head, 0xC) = head;
    At<uint32_t>(tree, 0x4) = 0;
}

// The inline clear: the nodes freed when the size is not 0, the head reset.
void ClearTree(char* tree)
{
    if (!At<uint32_t>(tree, 0x4))
        return;
    EraseNodes(Parent(At<char*>(tree, 0x0)));
    ResetHead(tree);
}

// A new empty tree head (0x18 bytes from the STL allocator) into `tree`.
void NewTree(char* tree)
{
    At<char*>(tree, 0x0) = nullptr;
    char* const head = static_cast<char*>(Allocate(0x18));
    At<char*>(tree, 0x0) = head;
    At<uint32_t>(tree, 0x4) = 0;
    At<uint8_t>(head, 0x0) = 0;
    At<char*>(head, 0x4) = nullptr;
    At<char*>(head, 0x8) = head;
    At<char*>(head, 0xC) = head;
}

// --- The sets at 0x00B485E0 -------------------------------------------------------
//
// A process-wide map<uint32, set<pair<int32, int32>>>: an outer node holds
// the key at +0x10 and its set (head, size) at +0x14; a set node holds the
// pair at +0x10. Saved with the simulator.

char* Sets() { return reinterpret_cast<char*>(addr::cSC4BuildingDevelopmentSimulator_Sets); }

// 0x006990B0: outer nodes freed with their sets.
void EraseSetNodes(char* node)
{
    while (node) {
        EraseSetNodes(Right(node));
        char* const left = Left(node);
        char* const set = node + 0x14;
        ClearTree(set);
        if (char* const head = At<char*>(set, 0x0))
            Free(head);
        Free(node);
        node = left;
    }
}

struct InsertResult {
    char* node;
    uint8_t inserted;
};

// 0x00696300: set<pair<int32, int32>>::insert_unique (pairs ordered by the
// first, then the second).
InsertResult* InsertPair(char* set, InsertResult* out, const int32_t* value)
{
    using DecrementFn = void(__thiscall*)(char** iterator);
    using InsertAtFn = char**(__thiscall*)(char* set, char** out, char* x, char* y, const int32_t* value,
                                           uint32_t zero);
    char* const head = At<char*>(set, 0x0);
    char* x = Parent(head);
    char* y = head;
    bool less = true;
    while (x) {
        y = x;
        const int32_t first = At<int32_t>(x, 0x10);
        less = first > value[0] || (first == value[0] && value[1] < At<int32_t>(x, 0x14));
        x = less ? Left(x) : Right(x);
    }
    char* j = y;
    bool insert;
    if (less && y == Left(head)) {
        insert = true;
    } else {
        if (less)
            reinterpret_cast<DecrementFn>(addr::SC4TreeIterator_Decrement)(&j);
        const int32_t first = At<int32_t>(j, 0x10);
        insert = value[0] > first || (value[0] == first && At<int32_t>(j, 0x14) < value[1]);
    }
    if (!insert) {
        out->node = j;
        out->inserted = 0;
        return out;
    }
    char** const at =
        reinterpret_cast<InsertAtFn>(addr::SC4IntPairSet_InsertAt)(set, &j, x, y, value, 0u);
    out->node = *at;
    out->inserted = 1;
    return out;
}

// 0x0069AB40: the map's operator[]: the set of `*key`, a new empty one
// inserted (by copy, hinted at the lower bound) when missing.
char* SetsAt(char* map, const uint32_t* key)
{
    using CopyFn = void*(__thiscall*)(void* to, const void* from);
    using InsertHintFn = char**(__thiscall*)(char* map, char** out, char* hint, const void* value);
    char* const head = At<char*>(map, 0x0);
    char* y = head;
    for (char* x = Parent(head); x;) {
        if (At<uint32_t>(x, 0x10) < *key) {
            x = Right(x);
        } else {
            y = x;
            x = Left(x);
        }
    }
    if (y != head && *key >= At<uint32_t>(y, 0x10))
        return y + 0x14;
    char empty[8];
    NewTree(empty);
    struct {
        uint32_t key;
        char set[8];
    } value;
    value.key = *key;
    reinterpret_cast<CopyFn>(addr::SC4IntPairSet_CopyConstruct)(value.set, empty);
    char* node = nullptr;
    reinterpret_cast<InsertHintFn>(addr::SC4IntPairSetMap_InsertHint)(map, &node, y, &value);
    ClearTree(value.set);
    if (char* const p = At<char*>(value.set, 0x0))
        Free(p);
    ClearTree(empty);
    if (char* const p = At<char*>(empty, 0x0))
        Free(p);
    return node + 0x14;
}

// --- The hash table +0x9C ---------------------------------------------------------
//
// hash_map<(developer, x, z, byte), uint32> (the key's reference is not
// counted): buckets +0x4/+0x8, count +0x10;
// a node is next, the key (16 bytes), the value.

uint32_t CellHash(const uint32_t* key)
{
    uint32_t hash = (key[1] & 0x3F) | (key[0] << 6);
    hash = (hash << 6) | (key[2] & 0x3F);
    return (hash << 4) | (reinterpret_cast<const uint8_t*>(key)[0xC] & 0xF);
}

// 0x00697470: the table resized for one more, then the entry put at the
// head of its bucket (no check for an equal key); the entry's address.
uint32_t* InsertCell(char* table, const uint32_t* entry)
{
    using ResizeFn = void(__thiscall*)(char* table, uint32_t count);
    reinterpret_cast<ResizeFn>(addr::SC4BuildingCellHash_Resize)(table, At<uint32_t>(table, 0x10) + 1);
    const uint32_t buckets =
        static_cast<uint32_t>(At<char*>(table, 0x8) - At<char*>(table, 0x4)) / 4;
    const uint32_t bucket = CellHash(entry) % buckets;
    void* const first = At<void**>(table, 0x4)[bucket];
    auto* const node = static_cast<uint32_t*>(Allocate(0x18));
    node[0] = 0;
    for (int i = 0; i < 5; ++i)
        node[1 + i] = entry[i];
    node[0] = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(first));
    At<void**>(table, 0x4)[bucket] = node;
    ++At<uint32_t>(table, 0x10);
    return node + 1;
}

// --- Init --------------------------------------------------------------------------

struct GameString {
    const void* vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};

// 0x0069D1D0 (its argument unused): once per run, a clean-up callback
// (0x0069B850) with service 0x00B43C94, a walk over the tuning type's keys
// (callback 0x0069D130), then 0x00695FD0 and 0x0069C370.
uint8_t SetUpOnce(char*)
{
    uint8_t& done = Global<uint8_t>(addr::cSC4BuildingDevelopmentSimulator_SetUpDone);
    if (done)
        return 1;
    done = 1;
    Virtual<void>(Global<void*>(addr::SC4_Service43C94), 0x114,
                  reinterpret_cast<void*>(addr::cSC4BuildingDevelopmentSimulator_CleanUpOnce), 0u);
    if (void* const resources = Global<void*>(addr::SC4_ResourceManager)) {
        void* keys = nullptr;
        if (Virtual<uint32_t>(resources, 0x48, &keys, kTuningType)) {
            Virtual<void>(keys, 0x1C, reinterpret_cast<void*>(addr::cSC4BuildingDevelopmentSimulator_OnTuningKeyOnce),
                          0u);
            Virtual<uint32_t>(keys, 0x8);
        }
    }
    reinterpret_cast<VoidFn>(addr::cSC4BuildingDevelopmentSimulator_SendStartMessage)();
    reinterpret_cast<VoidFn>(addr::cSC4BuildingDevelopmentSimulator_SetUpTables)();
    return 1;
}

// 0x006959E0: defaults (100, 2000), 0.5, 10, then from the tuning exemplar
// 6534284A-E7E2C2DB-E8DA7677 properties 0x47E2C380-0x47E2C383.
void LoadTuning(char* self)
{
    At<uint32_t>(self, field::kTuningC0) = 100;
    At<uint32_t>(self, field::kTuningC0 + 4) = 2000;
    At<uint32_t>(self, field::kTuningC8) = 0x3F000000; // 0.5
    uint32_t count = 10;
    void* exemplar = nullptr;
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, kTuningKey, kIidExemplar, &exemplar, 0u,
                          0u)) {
        At<uint32_t>(self, field::kTuningCC) = count;
        return;
    }
    void* const holder = Virtual<void*>(exemplar, 0x1C);
    if (void* const property = Virtual<void*>(holder, 0x24, 0x47E2C380u)) {
        void* const variant = Virtual<void*>(property, 0x14);
        const uint32_t* const pair = Virtual<const uint32_t*>(variant, 0x19C);
        At<uint32_t>(self, field::kTuningC0) = pair[0];
        At<uint32_t>(self, field::kTuningC0 + 4) = pair[1];
    }
    GetFloat(holder, 0x47E2C381, self + field::kTuningC8);
    GetUint32(holder, 0x47E2C382, &count);
    GetFloats(holder, 0x47E2C383, self + field::kTuningD0);
    Virtual<uint32_t>(exemplar, 0x8);
    At<uint32_t>(self, field::kTuningCC) = count;
}

// A service into `offset` when there is none, referenced.
void Take(char* self, std::size_t offset, void* service)
{
    if (At<void*>(self, offset))
        return;
    At<void*>(self, offset) = service;
    Virtual<uint32_t>(service, 0x4);
}

// Slot 3 (0x0069D240): the one-time setup; the tuning keys walked
// (callback 0x00699E20); the tuning; the flags +0x50 reset for every
// developer unless a save was read; the services; the grid and the demand
// (new or Init); the messages and the command (once); registration with the
// simulator service (0, 4, 6, 10); the three cheat codes. Always true.
uint8_t __fastcall Init(char* self, void*)
{
    using TextFn = void*(__thiscall*)(void* string, const char* text);
    static bool logged = false;
    FirstCall("Init", logged);
    if (At<uint8_t>(self, field::kInitialized))
        return 1;
    At<uint8_t>(self, field::kInitialized) = 1;
    SetUpOnce(self);
    if (void* const resources = Global<void*>(addr::SC4_ResourceManager)) {
        void* keys = nullptr;
        if (Virtual<uint32_t>(resources, 0x48, &keys, kTuningType)) {
            Virtual<void>(keys, 0x1C, reinterpret_cast<void*>(addr::cSC4BuildingDevelopmentSimulator_OnTuningKey), self);
            Virtual<uint32_t>(keys, 0x8);
        }
    }
    LoadTuning(self);
    if (!At<uint8_t>(self, field::kLoaded)) {
        char* const head = At<char*>(self, field::kDevelopers);
        for (char* node = Left(head); node != At<char*>(self, field::kDevelopers); node = Next(node)) {
            uint32_t key = At<uint32_t>(node, 0x10);
            *static_cast<uint8_t*>(reinterpret_cast<AtFn>(addr::SC4Sint32ByteMap_At)(self + field::kFlags, &key)) = 0;
        }
    }
    Take(self, field::kResources, Global<void*>(addr::SC4_ResourceManager));
    Take(self, field::kCity, Global<void*>(addr::SC4_City));
    if (!At<void*>(self, field::kOccupants)) {
        void* const occupants = Virtual<void*>(At<void*>(self, field::kCity), 0x88);
        At<void*>(self, field::kOccupants) = occupants;
        Virtual<uint32_t>(occupants, 0x4);
    }
    Take(self, field::kZones, Global<void*>(addr::SC4_Service43D14));
    Take(self, field::kLots, Global<void*>(addr::SC4_LotManager));
    Take(self, field::kService30, Global<void*>(addr::SC4_Service43D88));
    Take(self, field::kService34, Global<void*>(addr::SC4_Service43DA0));
    if (!At<void*>(self, field::kTerrain)) {
        Take(self, field::kTerrain, Global<void*>(addr::SC4_Terrain));
        At<int32_t>(self, field::kTerrain3C) = Virtual<int32_t>(At<void*>(self, field::kTerrain), 0x1C) + 1;
        At<int32_t>(self, field::kTerrain40) = Virtual<int32_t>(At<void*>(self, field::kTerrain), 0x1C);
        At<int32_t>(self, field::kTerrain44) = Virtual<int32_t>(At<void*>(self, field::kTerrain), 0x20);
    }
    if (!At<void*>(self, field::kGrid)) {
        char* const grid = static_cast<char*>(OperatorNew(0x44));
        if (grid) {
            At<std::uintptr_t>(grid, 0x4) = addr::SC4SimGrid_VtableBase;
            At<std::uintptr_t>(grid, 0x0) = 0x00A878C0; // cSC4SimGridBool8
            At<std::uintptr_t>(grid, 0x4) = 0x00A878A4;
            At<uint8_t>(grid, 0x8) = 0;
            At<uint32_t>(grid, 0xC) = 0;
            At<uint32_t>(grid, 0x10) = 0x49B9E602;
            At<uint32_t>(grid, 0x18) = 0;
            At<uint32_t>(grid, 0x38) = 0;
            At<uint32_t>(grid, 0x3C) = 0;
            At<uint32_t>(grid, 0x40) = 0;
        }
        At<void*>(self, field::kGrid) = grid;
        Virtual<uint32_t>(grid, 0x4);
        Virtual<void>(At<void*>(self, field::kGrid), 0xC);
        Virtual<void>(At<void*>(self, field::kGrid), 0x24, 1u);
        Virtual<void>(At<void*>(self, field::kGrid), 0x18, 0x49D5B678u);
    }
    if (void* const demand = At<void*>(self, field::kDemand)) {
        Virtual<void>(demand, 0xC);
    } else {
        void* fresh = OperatorNew(0x54);
        if (fresh)
            fresh = reinterpret_cast<void*(__thiscall*)(void*)>(addr::cSC4Demand_cSC4Demand)(fresh);
        At<void*>(self, field::kDemand) = fresh;
        Virtual<uint32_t>(fresh, 0x4);
        Virtual<void>(At<void*>(self, field::kDemand), 0xC);
        Virtual<void>(At<void*>(self, field::kDemand), 0x48, 0x42C80000u); // 100
        Virtual<void>(At<void*>(self, field::kDemand), 0x4C, 0u);
    }
    Take(self, field::kService6C, Global<void*>(addr::SC4_Service43D20));
    Take(self, field::kService68, Global<void*>(addr::SC4_Service43D24));
    if (!At<void*>(self, field::kMessages)) {
        Take(self, field::kMessages, Global<void*>(addr::SC4_MessageServer));
        void* const target = self + 0x8;
        for (const uint32_t message : kMessages)
            Virtual<uint8_t>(At<void*>(self, field::kMessages), 0x14, target, message);
        Virtual<void>(Global<void*>(addr::SC4_Commands), 0x18, kCommand,
                      reinterpret_cast<void*>(addr::cSC4BuildingDevelopmentSimulator_Command), 0u);
    }
    void* const simulators = Global<void*>(addr::SC4_SimulatorService);
    GameString name{};
    name.begin = static_cast<char*>(Allocate(0x21));
    name.end = name.begin;
    name.capacity = name.begin + 0x21;
    std::memcpy(name.begin, reinterpret_cast<const void*>(addr::cSC4BuildingDevelopmentSimulator_Name), 0x20);
    name.end = name.begin + 0x20;
    *name.end = 0;
    name.zero = 0;
    name.vtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
    void* const target = self + 0x8;
    for (const uint32_t phase : {0u, 4u, 6u, 10u})
        Virtual<uint32_t>(simulators, 0x4C, target, phase, &name, 2u);
    if (void* const cheats = Global<void*>(addr::SC4_CheatCodes)) {
        const std::uintptr_t texts[3] = {addr::cSC4BuildingDevelopmentSimulator_CheatHowDryIAm,
                                         addr::cSC4BuildingDevelopmentSimulator_CheatFightThePower,
                                         addr::cSC4BuildingDevelopmentSimulator_CheatBarstuck};
        for (int i = 0; i < 3; ++i) {
            GameString text;
            void* const string = reinterpret_cast<TextFn>(addr::SC4String_FromText)(&text, Global<const char*>(texts[i]));
            Virtual<void>(cheats, 0xC, kCheats[i], string);
            text.vtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
            if (text.begin)
                Free(text.begin);
            Virtual<void>(cheats, 0x1C, target, kCheats[i]);
        }
    }
    name.vtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
    if (name.begin)
        Free(name.begin);
    return 1;
}

// --- Shutdown ---------------------------------------------------------------------

void Release(char* self, std::size_t offset)
{
    if (void* const object = At<void*>(self, offset)) {
        Virtual<uint32_t>(object, 0x8);
        At<void*>(self, offset) = nullptr;
    }
}

// A list whose 0x1C-byte nodes own nothing emptied, the head kept.
void FreeListNodes(char* self, std::size_t offset)
{
    char* node = *At<char**>(self, offset);
    while (node != At<char*>(self, offset)) {
        char* const current = node;
        node = At<char*>(node, 0x0);
        Free(current);
    }
    char* const head = At<char*>(self, offset);
    At<char*>(head, 0x0) = head;
    At<char*>(head, 0x4) = head;
}

// Slot 4 (0x0069A210): the cheat codes off; unregistered from the
// simulator service; the messages and the command off; each developer shut
// down (slot 4) and released, the map emptied; the occupant list released
// and emptied; the other containers emptied; the grid, the demand (shut
// down) and the services released; the sets at 0x00B485E0 emptied.
uint8_t __fastcall Shutdown(char* self, void*)
{
    static bool logged = false;
    FirstCall("Shutdown", logged);
    if (!At<uint8_t>(self, field::kInitialized))
        return 1;
    At<uint8_t>(self, field::kInitialized) = 0;
    void* const target = self + 0x8;
    if (void* const cheats = Global<void*>(addr::SC4_CheatCodes)) {
        for (const uint32_t cheat : kCheats) {
            Virtual<void>(cheats, 0x20, target, cheat);
            Virtual<void>(cheats, 0x10, cheat);
        }
    }
    if (void* const simulators = Virtual<void*>(Global<void*>(addr::SC4_City), 0xC8))
        Virtual<uint32_t>(simulators, 0x50, target);
    if (At<void*>(self, field::kMessages)) {
        for (const uint32_t message : kMessages)
            Virtual<uint8_t>(At<void*>(self, field::kMessages), 0x18, target, message);
        Virtual<void>(Global<void*>(addr::SC4_Commands), 0x24, kCommand);
        Virtual<uint32_t>(At<void*>(self, field::kMessages), 0x8);
        At<void*>(self, field::kMessages) = nullptr;
    }
    char* const developers = self + field::kDevelopers;
    for (char* node = Left(At<char*>(developers, 0x0)); node != At<char*>(developers, 0x0); node = Next(node)) {
        void* const developer = At<void*>(node, 0x14);
        Virtual<uint8_t>(developer, 0x10);
        Virtual<uint32_t>(developer, 0x8);
    }
    ClearTree(developers);
    char* const list = self + field::kList;
    for (char* node = *At<char**>(list, 0x0); node != At<char*>(list, 0x0); node = At<char*>(node, 0x0))
        Virtual<uint32_t>(At<void*>(node, 0x8), 0x8);
    reinterpret_cast<SelfFn>(addr::SC4List_Clear2)(list);
    FreeListNodes(self, field::kList7C);
    FreeListNodes(self, field::kList80);
    ClearTree(self + field::kTree84);
    ClearTree(self + field::kTree90);
    reinterpret_cast<SelfFn>(addr::SC4Uint32HashSet_Clear)(self + field::kCells);
    ClearTree(self + field::kFlags);
    Release(self, field::kGrid);
    if (void* const demand = At<void*>(self, field::kDemand)) {
        Virtual<uint8_t>(demand, 0x10);
        Virtual<uint32_t>(At<void*>(self, field::kDemand), 0x8);
        At<void*>(self, field::kDemand) = nullptr;
    }
    Release(self, field::kService68);
    Release(self, field::kService6C);
    Release(self, field::kService30);
    Release(self, field::kTerrain);
    Release(self, field::kService34);
    Release(self, field::kZones);
    Release(self, field::kLots);
    Release(self, field::kCity);
    Release(self, field::kResources);
    Release(self, field::kOccupants);
    if (At<uint32_t>(Sets(), 0x4)) {
        EraseSetNodes(Parent(At<char*>(Sets(), 0x0)));
        ResetHead(Sets());
    }
    return 1;
}

// --- Save --------------------------------------------------------------------------

// cIGZSerializable slot 3 (0x00697800), on the part at +0xC: version 4 (see
// docs/development.md for the layout). False when the stream is in error,
// before or after.
uint8_t __fastcall Write(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    char* const self = part - 0xC;
    if (Virtual<uint8_t>(stream, 0x48))
        return 0;
    Virtual<uint8_t>(stream, 0x1C, static_cast<uint16_t>(4));
    WriteRef(stream, At<void*>(self, field::kGrid));
    WriteRef(stream, At<void*>(self, field::kDemand));
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, field::kDevelopers + 4));
    for (char* node = Left(At<char*>(self, field::kDevelopers)); node != At<char*>(self, field::kDevelopers);
         node = Next(node)) {
        void* const developer = At<void*>(node, 0x14);
        Virtual<uint8_t>(stream, 0x24, At<uint32_t>(node, 0x10));
        WriteRef(stream, developer);
    }
    char* const list = At<char*>(self, field::kList);
    uint32_t count = 0;
    for (char* node = At<char*>(list, 0x0); node != list; node = At<char*>(node, 0x0))
        ++count;
    Virtual<uint8_t>(stream, 0x24, count);
    for (char* node = At<char*>(At<char*>(self, field::kList), 0x0); node != At<char*>(self, field::kList);
         node = At<char*>(node, 0x0))
        WriteRef(stream, At<void*>(node, 0x8));
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, field::kFlags + 4));
    for (char* node = Left(At<char*>(self, field::kFlags)); node != At<char*>(self, field::kFlags); node = Next(node)) {
        const uint8_t flag = At<uint8_t>(node, 0x14);
        Virtual<uint8_t>(stream, 0x24, At<uint32_t>(node, 0x10));
        Virtual<uint8_t>(stream, 0x14, static_cast<uint32_t>(flag));
    }
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(Sets(), 0x4));
    for (char* outer = Left(At<char*>(Sets(), 0x0)); outer != At<char*>(Sets(), 0x0); outer = Next(outer)) {
        Virtual<uint8_t>(stream, 0x24, At<uint32_t>(outer, 0x10));
        Virtual<uint8_t>(stream, 0x24, At<uint32_t>(outer, 0x18));
        for (char* node = Left(At<char*>(outer, 0x14)); node != At<char*>(outer, 0x14); node = Next(node)) {
            const int32_t second = At<int32_t>(node, 0x14);
            Virtual<uint8_t>(stream, 0x20, At<int32_t>(node, 0x10));
            Virtual<uint8_t>(stream, 0x20, second);
        }
    }
    char* const table = self + field::kCells;
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(table, 0x10));
    uint32_t* node = nullptr;
    for (uint32_t i = 0; i < static_cast<uint32_t>(At<char*>(table, 0x8) - At<char*>(table, 0x4)) / 4; ++i) {
        if (At<uint32_t**>(table, 0x4)[i]) {
            node = At<uint32_t**>(table, 0x4)[i];
            break;
        }
    }
    while (node) {
        const uint32_t value = node[5];
        WriteRef(stream, reinterpret_cast<void*>(static_cast<std::uintptr_t>(node[1])));
        Virtual<uint8_t>(stream, 0x20, node[2]);
        Virtual<uint8_t>(stream, 0x20, node[3]);
        Virtual<uint8_t>(stream, 0x14, static_cast<uint32_t>(reinterpret_cast<const uint8_t*>(node)[0x10]));
        Virtual<uint8_t>(stream, 0x24, value);
        if (node[0]) {
            node = reinterpret_cast<uint32_t*>(static_cast<std::uintptr_t>(node[0]));
            continue;
        }
        const uint32_t buckets = static_cast<uint32_t>(At<char*>(table, 0x8) - At<char*>(table, 0x4)) / 4;
        uint32_t bucket = CellHash(node + 1) % buckets;
        node = nullptr;
        while (++bucket < buckets) {
            node = At<uint32_t**>(table, 0x4)[bucket];
            if (node)
                break;
        }
    }
    WriteBool(stream, At<uint8_t>(self, field::kCheatDD));
    WriteBool(stream, At<uint8_t>(self, field::kCheatIgnorePower));
    WriteBool(stream, At<uint8_t>(self, field::kCheatIgnoreCaps));
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// cIGZSerializable slot 4 (0x0069ACB0): version 4 only (false otherwise,
// nothing read); +0x10 set; the entries added to what the containers
// already hold. False when the stream is in error, before or after.
uint8_t __fastcall Read(char* part, void*, void* stream)
{
    using FindFn = uint32_t*(__thiscall*)(void* table, const void* key);
    static bool logged = false;
    FirstCall("Read", logged);
    char* const self = part - 0xC;
    if (Virtual<uint8_t>(stream, 0x48))
        return 0;
    uint16_t version = 0;
    Virtual<uint8_t>(stream, 0x1C, &version);
    if (version != 4)
        return 0;
    At<uint8_t>(self, field::kLoaded) = 1;
    ReadRef(stream, kIidGrid, self + field::kGrid, 0u);
    ReadRef(stream, kIidDemand, self + field::kDemand, 0u);
    uint32_t count = 0;
    Virtual<uint8_t>(stream, 0x24, &count);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t key = 0;
        Virtual<uint8_t>(stream, 0x24, &key);
        void* developer = nullptr;
        ReadRef(stream, kIidDeveloper, &developer, 0u);
        *static_cast<void**>(reinterpret_cast<AtFn>(addr::SC4Uint32Map_At)(self + field::kDevelopers, &key)) =
            developer;
    }
    Virtual<uint8_t>(stream, 0x24, &count);
    for (uint32_t i = 0; i < count; ++i) {
        void* occupant = nullptr;
        ReadRef(stream, kIidOccupant, &occupant, 0u);
        char* const head = At<char*>(self, field::kList);
        void** const node = Virtual<void**>(ListNodeAllocator(), 0xC, 0xCu);
        node[2] = occupant;
        char* const last = At<char*>(head, 0x4);
        node[0] = head;
        node[1] = last;
        At<void*>(last, 0x0) = node;
        At<void*>(head, 0x4) = node;
    }
    Virtual<uint8_t>(stream, 0x24, &count);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t read = 0;
        Virtual<uint8_t>(stream, 0x24, &read);
        uint32_t key = read;
        uint8_t flag = 0;
        Virtual<uint8_t>(stream, 0x14, &flag);
        *static_cast<uint8_t*>(reinterpret_cast<AtFn>(addr::SC4Sint32ByteMap_At)(self + field::kFlags, &key)) = flag;
    }
    Virtual<uint8_t>(stream, 0x24, &count);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t key = 0;
        Virtual<uint8_t>(stream, 0x24, &key);
        char* const set = SetsAt(Sets(), &key);
        uint32_t pairs = 0;
        Virtual<uint8_t>(stream, 0x24, &pairs);
        for (uint32_t j = 0; j < pairs; ++j) {
            int32_t first = 0;
            int32_t second = 0;
            Virtual<uint8_t>(stream, 0x20, &first);
            Virtual<uint8_t>(stream, 0x20, &second);
            const int32_t pair[2] = {first, second};
            InsertResult result;
            InsertPair(set, &result, pair);
        }
    }
    Virtual<uint8_t>(stream, 0x24, &count);
    char* const table = self + field::kCells;
    for (uint32_t i = 0; i < count; ++i) {
        void* developer = nullptr;
        ReadRef(stream, kIidDeveloper, &developer, 0u);
        uint32_t key[4] = {static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(developer)), 0, 0, 0};
        Virtual<uint8_t>(stream, 0x20, &key[1]);
        Virtual<uint8_t>(stream, 0x20, &key[2]);
        Virtual<uint8_t>(stream, 0x14, &key[3]);
        uint32_t value = 0;
        Virtual<uint8_t>(stream, 0x24, &value);
        uint32_t* slot;
        if (uint32_t* const found = reinterpret_cast<FindFn>(addr::SC4BuildingCellHash_Find)(table, key)) {
            slot = found + 5;
        } else {
            const uint32_t entry[5] = {key[0], key[1], key[2], key[3], 0};
            slot = InsertCell(table, entry) + 4;
        }
        *slot = value;
        if (developer)
            Virtual<uint32_t>(developer, 0x8);
    }
    ReadBool(stream, self + field::kCheatDD);
    ReadBool(stream, self + field::kCheatIgnorePower);
    ReadBool(stream, self + field::kCheatIgnoreCaps);
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// --- Installation -------------------------------------------------------------------

struct Slot {
    std::uintptr_t vtable;
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(vtable, index, original, fn) {vtable, index, original, reinterpret_cast<void*>(&fn)}

const Slot kInitSlots[] = {OPENSC4_SLOT(addr::cSC4BuildingDevelopmentSimulator_vftable, 3,
                                        addr::cSC4BuildingDevelopmentSimulator_Init, Init)};
const Slot kShutdownSlots[] = {OPENSC4_SLOT(addr::cSC4BuildingDevelopmentSimulator_vftable, 4,
                                            addr::cSC4BuildingDevelopmentSimulator_Shutdown, Shutdown)};
const Slot kSaveSlots[] = {OPENSC4_SLOT(addr::cSC4BuildingDevelopmentSimulator_vftable_8, 3,
                                        addr::cSC4BuildingDevelopmentSimulator_Write, Write)};
const Slot kLoadSlots[] = {OPENSC4_SLOT(addr::cSC4BuildingDevelopmentSimulator_vftable_8, 4,
                                        addr::cSC4BuildingDevelopmentSimulator_Read, Read)};

#undef OPENSC4_SLOT

template <std::size_t N>
bool SwapSlots(const Slot (&slots)[N], const char* what)
{
    if (!game::IsBuild641())
        return false;
    for (const Slot& slot : slots) {
        const auto* const vtable = reinterpret_cast<const std::uintptr_t*>(slot.vtable);
        if (vtable[slot.index] != slot.original) {
            log::Write("development %s: slot %d of %08X holds %08X, expected %08X; not installing", what, slot.index,
                       static_cast<unsigned>(slot.vtable), static_cast<unsigned>(vtable[slot.index]),
                       static_cast<unsigned>(slot.original));
            return false;
        }
    }
    for (const Slot& slot : slots) {
        auto* const entry = reinterpret_cast<std::uintptr_t*>(slot.vtable) + slot.index;
        DWORD old = 0;
        if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &old))
            return false;
        *entry = reinterpret_cast<std::uintptr_t>(slot.ours);
        VirtualProtect(entry, sizeof(*entry), old, &old);
    }
    log::Write("development %s: %u slots are ours", what, static_cast<unsigned>(N));
    return true;
}

bool InstallInit(registry::Mode) { return SwapSlots(kInitSlots, "init"); }
bool InstallShutdown(registry::Mode) { return SwapSlots(kShutdownSlots, "shutdown"); }
bool InstallSave(registry::Mode) { return SwapSlots(kSaveSlots, "save"); }
bool InstallLoad(registry::Mode) { return SwapSlots(kLoadSlots, "load"); }

const bool g_registered[] = {
    registry::Register({"simulators.development.init", "simulators/development", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4BuildingDevelopmentSimulator_vftable,
                        "Our building development Init, its one-time setup and tuning (docs/development.md)", false,
                        InstallInit}),
    registry::Register({"simulators.development.shutdown", "simulators/development", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4BuildingDevelopmentSimulator_vftable,
                        "Our building development Shutdown (docs/development.md)", false, InstallShutdown}),
    registry::Register({"simulators.development.save", "simulators/development", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4BuildingDevelopmentSimulator_vftable_8,
                        "Our building development Write: the save record, version 4 (docs/development.md)", false,
                        InstallSave}),
    registry::Register({"simulators.development.load", "simulators/development", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4BuildingDevelopmentSimulator_vftable_8,
                        "Our building development Read, version 4 (docs/development.md)", false, InstallLoad}),
};

// --- Our object -------------------------------------------------------------------

constexpr uint32_t kClassId = 0x8990BE7A;
constexpr uint32_t kIidUnknown = 0x00000001;
constexpr uint32_t kIidDevelopment = 0x07813EAE;
constexpr uint32_t kIidMessageTarget = 0xE98B2F57;
constexpr uint32_t kIidMessageTarget2 = 0x452294AA;
constexpr uint32_t kIidSerializable = 0xE4FDA3D4;
constexpr std::size_t kSize = 0xE4;

// The game's: 0x00AB0AF8 (deleting destructor in slot 59), 0x00AB0AE8 (at
// +4), 0x00AB0AD8 (at +8), 0x00AB0AC0 (at +0xC).
constexpr int kSlots = 60, kSlots4 = 4, kSlots8 = 4, kSlotsC = 6;
void* g_vtable[kSlots];
void* g_vtable4[kSlots4];
void* g_vtable8[kSlots8];
void* g_vtableC[kSlotsC];

// As the game's (0x00695330, shared with other simulators).
uint32_t __fastcall AddRef(char* self, void*) { return ++At<uint32_t>(self, field::kRefCount); }

// As the game's (0x00695290).
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    std::size_t offset;
    switch (iid) {
    case kIidDevelopment:
    case kIidUnknown: offset = 0; break;
    case kIidMessageTarget: offset = 0x4; break;
    case kIidMessageTarget2: offset = 0x8; break;
    case kIidSerializable: offset = 0xC; break;
    default: return 0;
    }
    *object = self ? self + offset : nullptr;
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// As the game's (0x006959B0): Shutdown and the deleting destructor (slot
// 59) at the last reference.
uint32_t __fastcall ReleaseObject(char* self, void*)
{
    int32_t& refs = At<int32_t>(self, field::kRefCount);
    if (refs > 1)
        return static_cast<uint32_t>(--refs);
    Virtual<uint8_t>(self, 0x10);
    Virtual<void*>(self, 0xEC, 1u);
    return 0;
}

// As the game's (0x00699180): the members destroyed by the game's own
// destructor (0x00698B30), which also restores the game's vtables.
void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    static int destroyed = 0;
    if (destroyed++ < 3)
        log::Write("development object: destroying %p", static_cast<void*>(self));
    reinterpret_cast<SelfFn>(addr::cSC4BuildingDevelopmentSimulator_DestroyMembers)(self);
    if (flags & 1)
        OperatorDelete(self);
    return self;
}

// As the game's (0x00695280).
uint32_t __fastcall GetGZCLSID(void*, void*) { return kClassId; }

template <std::size_t Offset>
uint8_t __fastcall QueryInterfaceAt(char* part, void*, uint32_t iid, void** object)
{
    return QueryInterface(part - Offset, nullptr, iid, object);
}

template <std::size_t Offset>
uint32_t __fastcall AddRefAt(char* part, void*)
{
    return AddRef(part - Offset, nullptr);
}

template <std::size_t Offset>
uint32_t __fastcall ReleaseAt(char* part, void*)
{
    return ReleaseObject(part - Offset, nullptr);
}

void SetVtables(char* self)
{
    At<void**>(self, 0x0) = g_vtable;
    At<void**>(self, 0x4) = g_vtable4;
    At<void**>(self, 0x8) = g_vtable8;
    At<void**>(self, 0xC) = g_vtableC;
}

// A list head of 0x1C bytes from the STL allocator, empty, into `offset`.
void NewList1C(char* self, std::size_t offset)
{
    At<void*>(self, offset) = nullptr;
    void** const head = static_cast<void**>(Allocate(0x1C));
    head[0] = head;
    head[1] = head;
    At<void**>(self, offset) = head;
}

// As the game's constructor (0x0069A000): only the fields it sets.
void Construct(char* self)
{
    using AllocateFn = void**(__thiscall*)(void* list, uint32_t count, uint32_t zero);
    using HashFn = void*(__thiscall*)(void* table, uint32_t buckets);
    SetVtables(self);
    At<uint8_t>(self, field::kLoaded) = 0;
    At<uint8_t>(self, field::kInitialized) = 0;
    for (std::size_t offset = 0x14; offset <= 0x48; offset += 4)
        At<uint32_t>(self, offset) = 0;
    At<void*>(self, field::kList) = nullptr;
    void** const node = reinterpret_cast<AllocateFn>(addr::SC4List_AllocateNodes)(self + field::kList, 1, 0);
    node[0] = node;
    node[1] = node;
    At<void**>(self, field::kList) = node;
    NewTree(self + field::kFlags);
    NewTree(self + field::kDevelopers);
    At<uint32_t>(self, 0x68) = 0;
    At<uint32_t>(self, 0x6C) = 0;
    At<uint32_t>(self, 0x70) = 0;
    At<uint32_t>(self, 0x78) = 0;
    NewList1C(self, field::kList7C);
    NewList1C(self, field::kList80);
    NewTree(self + field::kTree84);
    NewTree(self + field::kTree90);
    for (std::size_t offset = 0xA0; offset <= 0xAC; offset += 4)
        At<uint32_t>(self, offset) = 0;
    reinterpret_cast<HashFn>(addr::SC4Uint32HashSet_Construct)(self + field::kCells, 100);
    NewTree(self + field::kTreeB0);
    At<uint32_t>(self, 0xBC) = 0;
    At<uint32_t>(self, 0xD0) = 0;
    At<uint32_t>(self, 0xD4) = 0;
    At<uint32_t>(self, 0xD8) = 0;
    for (std::size_t offset = 0xDC; offset <= 0xE1; ++offset)
        At<uint8_t>(self, offset) = 0;
    At<uint32_t>(self, 0x74) = 0;
}

void BuildVtables()
{
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4BuildingDevelopmentSimulator_vftable),
                sizeof(g_vtable));
    std::memcpy(g_vtable4,
                reinterpret_cast<const void*>(addr::cSC4BuildingDevelopmentSimulator_vftable_cIGZMessageTarget),
                sizeof(g_vtable4));
    std::memcpy(g_vtable8, reinterpret_cast<const void*>(addr::cSC4BuildingDevelopmentSimulator_vftable_4),
                sizeof(g_vtable8));
    std::memcpy(g_vtableC, reinterpret_cast<const void*>(addr::cSC4BuildingDevelopmentSimulator_vftable_8),
                sizeof(g_vtableC));
    g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
    g_vtable[1] = reinterpret_cast<void*>(&AddRef);
    g_vtable[2] = reinterpret_cast<void*>(&ReleaseObject);
    g_vtable[59] = reinterpret_cast<void*>(&DeletingDestructor);
    g_vtable4[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x4>);
    g_vtable4[1] = reinterpret_cast<void*>(&AddRefAt<0x4>);
    g_vtable4[2] = reinterpret_cast<void*>(&ReleaseAt<0x4>);
    g_vtable8[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x8>);
    g_vtable8[1] = reinterpret_cast<void*>(&AddRefAt<0x8>);
    g_vtable8[2] = reinterpret_cast<void*>(&ReleaseAt<0x8>);
    g_vtableC[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0xC>);
    g_vtableC[1] = reinterpret_cast<void*>(&AddRefAt<0xC>);
    g_vtableC[2] = reinterpret_cast<void*>(&ReleaseAt<0xC>);
    g_vtableC[5] = reinterpret_cast<void*>(&GetGZCLSID);
}

// The factory, as the game's (0x0069A7A0): operator new(0xE4), then the
// constructor.
cIGZUnknown* Create()
{
    static bool built = false;
    if (!built) {
        built = true;
        BuildVtables();
    }
    auto* const self = static_cast<char*>(OperatorNew(kSize));
    if (!self)
        return nullptr;
    Construct(self);
    static int created = 0;
    if (created++ < 3)
        log::Write("development object: created %p", static_cast<void*>(self));
    return reinterpret_cast<cIGZUnknown*>(self);
}

bool ApplyObject(registry::Mode) { return game::IsBuild641() && director::AddClass(kClassId, Create); }

const bool g_registeredObject = registry::Register({
    "simulators.development.object",
    "simulators/development",
    registry::Kind::Gzcom,
    registry::Phase::DirectorInit,
    kClassId,
    "Our cSC4BuildingDevelopmentSimulator object under the game's class ID: factory, constructor, identity; methods "
    "from the (patched) vtables; the members still destroyed by the game's code (docs/development.md)",
    false,
    ApplyObject,
});

} // namespace
} // namespace opensc4::development_sim
