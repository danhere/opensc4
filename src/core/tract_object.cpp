// Our cSC4TractDeveloper (build 641; docs/tract.md), the parts around the
// simulation: Init with its tuning, Shutdown, the save record, the style
// slots, DoMessage (the style change clock), Decline and UpdatePopulation,
// and our own object (constructor, destructor, identity). Grow, the lots and
// the buildings are in tract_grow.cpp, tract_lots.cpp, tract_buildings.cpp.
//
// Fields this file verified (offsets from the object's start; DoMessage gets
// `this` = object + 4 and Write/Read get object + 8, so their disassembly
// reads 4 or 8 less):
//   +0x0C uint8   initialized (Init sets it last, Shutdown clears it first)
//   +0x10 int32   reference count (signed compare in Release 0x007048B0)
//   +0x14 message server (0x00B43CCC; Init takes it last, Shutdown releases it first)
//   +0x18 city (0x00B43CEC); +0x1C 0x00B43D68 (the building development
//         simulator: Decline's slot 39 (+0x9C) maps a building type to its
//         info); +0x20 0x00B43D88; +0x24 0x00B43D18; +0x28 0x00B43D20
//         (Decline's slot 8 (+0x20) with (lot, 6)); +0x2C the lot manager
//         0x00B43D08 (slot 6 (+0x18) lot at a cell, slot 7 (+0x1C) lots in a
//         rectangle); +0x30 0x00B43D94; +0x34 0x00B43DA0; +0x38 0x00B43DA8;
//         +0x3C the zone manager 0x00B43D14 (all referenced by Init)
//   +0x40 float   tuning 0x47E2C500 (1.5)
//   +0x44, +0x48 float pair 0x47E2C501 (0, 1.2)
//   +0x4C float   0x47E2C502 (1.5)
//   +0x50, +0x54 float pair 0x47E2C510 (1.0, 0.2)
//   +0x58, +0x5C float pair 0x47E2C511 (10, 5)
//   +0x60 float   0x47E2C530 (0.2): UpdatePopulation's noise, uniform(-v, v)
//   +0x64, +0x68 float pair 0x47E2C531 (0.01, 0.1; the constructor says 0.1,
//         0.1): UpdatePopulation's vacancy range for a full tract
//   +0x6C uint8   bool 0x47E2C540 (1)
//   +0x70 vector<uint32> the tuning's style list 0x47E2C550 (end +0x74)
//   +0x7C int32   years between style changes (0x47E2C551, 40 when 0; slot 14)
//   +0x80-+0x117 a member object (constructor 0x00706390, destructor
//         0x00706430): trees at +0xCC (0x14-byte nodes, size +0xD0), +0xD8
//         (0x14, +0xDC), +0x10C (0x18, +0x110), a vector at +0xE4
//   +0x118 vector<uint32> the active styles (end +0x11C; slots 8-10)
//   +0x124 int32  the current style's index in +0x118 (saved)
//   +0x128 int32  years since the last style change (saved; DoMessage 0x66956817)
//   +0x12C uint8  one style at a time (1) or all at once (0) (saved; slots 11, 12)
//   +0x130 map (0x1C-byte nodes, value +0x18 an object of service +0x24; size +0x134)
//   +0x13C cRZDate (vtable 0x00A827D8, day +0x140 = 0)
//   +0x16C list head (0x3C-byte nodes; 0x007075C0 empties it)
//   +0x170 vector<vector<int16>> (destroyed by 0x004638B0)
//   +0x17C map (0x20-byte nodes, value +0x14 an inner map of records; size +0x180)
//   Never written by the constructors: +0x80-+0xC8, +0xD4, +0xE0,
//   +0xF0-+0x108, +0x114, +0x138, +0x144-+0x168, +0x184.
//
// The save record (cIGZSerializable at +0x8, Write 0x00704B60 version 3; Read
// 0x00707950 accepts versions 0-3):
//   uint16 version (3)
//   sint32 +0x124 current style index
//   sint32 +0x128 years since the last style change
//   bool   +0x12C one style at a time (one byte; versions >= 2)
//   uint32 count, then count x uint32 the active styles +0x118 (versions >= 2)
//   sint32 +0x7C years between style changes (versions >= 3)
// Versions 0 and 1 set +0x12C to 1 and the styles to 0x2000, 0x2001, 0x2002.
#include "core/tract_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::tract_sim {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("tract: first %s", name);
}

namespace {

constexpr uint32_t kClassId = 0x2990C142;
constexpr uint32_t kIidUnknown = 0x00000001;
constexpr uint32_t kIidDeveloper = 0xA818ED1B;
constexpr uint32_t kIidMessageTarget2 = 0x452294AA;
constexpr uint32_t kIidSerializable = 0xE4FDA3D4;
constexpr uint32_t kMessageStyleYear = 0x66956817;

constexpr std::size_t kInitialized = 0x0C;
constexpr std::size_t kRefCount = 0x10;
constexpr std::size_t kMessageServer = 0x14;
constexpr std::size_t kBuildings = 0x1C;
constexpr std::size_t kService24 = 0x24;
constexpr std::size_t kService28 = 0x28;
constexpr std::size_t kLots = 0x2C;
constexpr std::size_t kNoise = 0x60;
constexpr std::size_t kVacancyLow = 0x64;
constexpr std::size_t kVacancyHigh = 0x68;
constexpr std::size_t kTuningStyles = 0x70;
constexpr std::size_t kYearsBetween = 0x7C;
constexpr std::size_t kMember = 0x80;
constexpr std::size_t kStyles = 0x118;
constexpr std::size_t kStyleIndex = 0x124;
constexpr std::size_t kYearsSince = 0x128;
constexpr std::size_t kOneAtATime = 0x12C;
constexpr std::size_t kObjects = 0x130;
constexpr std::size_t kRecordLists = 0x16C;
constexpr std::size_t kRows = 0x170;
constexpr std::size_t kRecords = 0x17C;

using NewFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* block);
using FreeRetFn = uint32_t(__cdecl*)(void* block);
using SelfFn = void*(__thiscall*)(void* self);
using VoidSelfFn = void(__thiscall*)(void* self);
using EraseFn = void(__thiscall*)(void* tree, void* node);
using ArgFn = void(__thiscall*)(void* self, void* argument);
using AssignFn = uint32_t(__thiscall*)(void* vector, const void* other);
using ResizeFn = void(__thiscall*)(void* vector, uint32_t count, const uint32_t* value);
using InsertRangeFn = void(__thiscall*)(void* vector, uint32_t* pos, const uint32_t* first, const uint32_t* last,
                                        void* tag);
using GetFn = uint8_t(__cdecl*)(void* holder, uint32_t property, void* value);
using ReadBoolFn = void*(__cdecl*)(void* stream, uint8_t* value);
using WriteBoolFn = void*(__cdecl*)(void* stream, uint32_t value);
using TextFn = void*(__thiscall*)(void* string, const char* text);
using SeedFn = void*(__thiscall*)(void* random, uint32_t seed);
using UniformFn = double(__thiscall*)(void* random, double low, double high);
using WalkRestartFn = void(__thiscall*)(void* walk, int32_t mode);
using WalkNextFn = bool(__thiscall*)(void* walk, int32_t* x, int32_t* z);
using AtExitFn = int(__cdecl*)(void (*)());

const auto OperatorDelete = reinterpret_cast<FreeFn>(addr::SC4_operator_delete);
const auto Allocate = reinterpret_cast<NewFn>(addr::SC4_stl_allocate);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);
const auto GetFloat = reinterpret_cast<GetFn>(addr::SC4_GetPropertyFloat);
const auto GetBool = reinterpret_cast<GetFn>(addr::SC4_GetPropertyBool);
const auto GetSint32 = reinterpret_cast<GetFn>(addr::SC4_GetPropertySint32);
const auto Assign = reinterpret_cast<AssignFn>(addr::SC4Uint32Vector_CopyAssign);
const auto Resize = reinterpret_cast<ResizeFn>(addr::SC4Uint32Vector_Resize);

// --- The game's red-black trees (head: color, parent = root, left most,
// right most; null leaves) ---------------------------------------------------

char* Left(char* node) { return At<char*>(node, 0x8); }
char* Right(char* node) { return At<char*>(node, 0xC); }
char* Parent(char* node) { return At<char*>(node, 0x4); }

// The inline iterator increment (MSVC _Tree::_Inc on null leaves).
char* Next(char* node)
{
    if (char* right = Right(node)) {
        node = right;
        while (char* const left = Left(node))
            node = left;
        return node;
    }
    char* parent = Parent(node);
    while (node == Right(parent)) {
        node = parent;
        parent = Parent(parent);
    }
    if (Right(node) != parent)
        node = parent;
    return node;
}

// 0x00704A50 (as 0x00704990, 0x007049D0 and 0x00704A10, the same code for
// other node types): the right subtrees first, then the node, down the left
// spine. The tree is only passed on to the recursion.
void __fastcall EraseNodes(void* tree, void*, char* node)
{
    static bool logged = false;
    FirstCall("EraseNodes 0x00704A50", logged);
    while (node) {
        EraseNodes(tree, nullptr, Right(node));
        char* const left = Left(node);
        Free(node);
        node = left;
    }
}

// The inline clear (size checked first) with an erase function (the game's
// at `gameErase`, ours when 0), then the head freed: what the destructors do
// with each tree.
void DestroyTree(char* tree, std::uintptr_t gameErase)
{
    if (At<uint32_t>(tree, 0x4)) {
        char* const root = Parent(At<char*>(tree, 0x0));
        if (gameErase)
            reinterpret_cast<EraseFn>(gameErase)(tree, root);
        else
            EraseNodes(tree, nullptr, root);
        char* const head = At<char*>(tree, 0x0);
        At<char*>(head, 0x8) = head;
        At<char*>(head, 0x4) = nullptr;
        At<char*>(head, 0xC) = head;
        At<uint32_t>(tree, 0x4) = 0;
    }
    if (void* const head = At<void*>(tree, 0x0))
        Free(head);
}

// A new empty tree head of `size` bytes (STL allocator) into `tree`: the
// field cleared first, the size cleared after the allocation, as the game.
void NewTree(char* tree, std::size_t size)
{
    At<char*>(tree, 0x0) = nullptr;
    char* const head = static_cast<char*>(Allocate(size));
    At<char*>(tree, 0x0) = head;
    At<uint32_t>(tree, 0x4) = 0;
    At<uint8_t>(head, 0x0) = 0;
    At<char*>(head, 0x4) = nullptr;
    At<char*>(head, 0x8) = head;
    At<char*>(head, 0xC) = head;
}

uint32_t Size(char* self, std::size_t vector)
{
    // sar 2 of the byte difference.
    return static_cast<uint32_t>((At<char*>(self, vector + 4) - At<char*>(self, vector)) >> 2);
}

// --- Init ------------------------------------------------------------------------

struct GameString {
    const void* vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};

// A float pair from an array property (variant slot 111, +0x1BC), copied as
// two dwords, when the holder has it.
void LoadPair(void* holder, uint32_t property, char* self, std::size_t offset)
{
    void* const value = Virtual<void*>(holder, 0x24, property);
    if (!value)
        return;
    void* const variant = Virtual<void*>(value, 0x14);
    const uint32_t* const pair = Virtual<const uint32_t*>(variant, 0x1BC);
    At<uint32_t>(self, offset) = pair[0];
    At<uint32_t>(self, offset + 4) = pair[1];
}

// 0x00706FC0: the tuning from the exemplar (0x6534284A, 0xE7E2C2DB,
// 0xE8DA7677). Nothing when the resource is missing; each field gets its
// default before its property is read.
void LoadTuning(char* self)
{
    static bool logged = false;
    FirstCall("LoadTuning", logged);
    const uint32_t key[3] = {0x6534284A, 0xE7E2C2DB, 0xE8DA7677};
    void* resource = nullptr;
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, key, 0xA52160F5u, &resource, 0u, 0u))
        return;
    void* const holder = Virtual<void*>(resource, 0x1C);
    At<float>(self, 0x40) = 1.5f;
    GetFloat(holder, 0x47E2C500, self + 0x40);
    At<uint32_t>(self, 0x44) = 0;
    At<float>(self, 0x48) = 1.2f;
    LoadPair(holder, 0x47E2C501, self, 0x44);
    At<float>(self, 0x4C) = 1.5f;
    GetFloat(holder, 0x47E2C502, self + 0x4C);
    At<float>(self, 0x50) = 1.0f;
    At<float>(self, 0x54) = 0.2f;
    LoadPair(holder, 0x47E2C510, self, 0x50);
    At<float>(self, 0x58) = 10.0f;
    At<float>(self, 0x5C) = 5.0f;
    LoadPair(holder, 0x47E2C511, self, 0x58);
    At<float>(self, kNoise) = 0.2f;
    GetFloat(holder, 0x47E2C530, self + kNoise);
    At<float>(self, kVacancyLow) = 0.01f;
    At<float>(self, kVacancyHigh) = 0.1f;
    LoadPair(holder, 0x47E2C531, self, kVacancyLow);
    At<uint8_t>(self, 0x6C) = 1;
    GetBool(holder, 0x47E2C540, self + 0x6C);
    // The inline clear (its move of nothing left out): end = begin.
    At<char*>(self, kTuningStyles + 4) = At<char*>(self, kTuningStyles);
    if (void* const value = Virtual<void*>(holder, 0x24, 0x47E2C550u)) {
        void* const variant = Virtual<void*>(value, 0x14);
        uint32_t* const data = Virtual<uint32_t*>(variant, 0x19C);
        const uint32_t count = Virtual<uint32_t>(variant, 0x10);
        uint8_t tag = 0; // an unused stack byte the game passes by address
        reinterpret_cast<InsertRangeFn>(addr::SC4Uint32Vector_InsertRange)(
            self + kTuningStyles, At<uint32_t*>(self, kTuningStyles + 4), data, data + count, &tag);
    }
    if (At<char*>(self, kStyles) == At<char*>(self, kStyles + 4)) {
        At<uint32_t>(self, kStyleIndex) = 0;
        Assign(self + kStyles, self + kTuningStyles);
    }
    if (At<uint32_t>(self, kStyleIndex) >= Size(self, kStyles))
        At<uint32_t>(self, kStyleIndex) = 0;
    if (!At<int32_t>(self, kYearsBetween)) {
        At<int32_t>(self, kYearsBetween) = 40;
        GetSint32(holder, 0x47E2C551, self + kYearsBetween);
    }
    Virtual<uint32_t>(resource, 0x8);
}

// A service into `offset` when there is none: stored even when null, then
// referenced; false when null.
bool Take(char* self, std::size_t offset, void* service)
{
    if (At<void*>(self, offset))
        return true;
    At<void*>(self, offset) = service;
    if (!service)
        return false;
    Virtual<uint32_t>(service, 0x4);
    return true;
}

// Slot 3 (0x00707780): Init(). False (initialized left 0) when a service is
// missing; true once done or when already done.
uint8_t __fastcall Init(char* self, void*)
{
    static bool logged = false;
    FirstCall("Init", logged);
    if (At<uint8_t>(self, kInitialized))
        return 1;
    uint8_t& once = Global<uint8_t>(addr::cSC4TractDeveloper_InitOnce);
    if (!once)
        once = 1;
    LoadTuning(self);
    if (!Take(self, 0x18, Global<void*>(addr::SC4_City)) ||
        !Take(self, kBuildings, Global<void*>(addr::SC4_Service43D68)) ||
        !Take(self, 0x20, Global<void*>(addr::SC4_Service43D88)) ||
        !Take(self, kService28, Global<void*>(addr::SC4_Service43D20)) ||
        !Take(self, kService24, Global<void*>(addr::SC4_Service43D18)) ||
        !Take(self, kLots, Global<void*>(addr::SC4_LotManager)) ||
        !Take(self, 0x30, Global<void*>(addr::SC4_Service43D94)) ||
        !Take(self, 0x34, Global<void*>(addr::SC4_Service43DA0)) ||
        !Take(self, 0x38, Global<void*>(addr::SC4_Service43DA8)) ||
        !Take(self, 0x3C, Global<void*>(addr::SC4_Service43D14)))
        return 0;
    void* const simulators = Global<void*>(addr::SC4_SimulatorService);
    if (!simulators)
        return 0;
    GameString name;
    reinterpret_cast<TextFn>(addr::SC4String_FromText)(&name,
                                                       reinterpret_cast<const char*>(addr::cSC4TractDeveloper_Name));
    void* const target = self + 0x4;
    Virtual<uint32_t>(simulators, 0x4C, target, 6u, &name, 2u);
    Virtual<uint32_t>(simulators, 0x4C, target, 7u, &name, 2u);
    if (!At<void*>(self, kMessageServer)) {
        void* const server = Global<void*>(addr::SC4_MessageServer);
        At<void*>(self, kMessageServer) = server;
        if (!server) {
            reinterpret_cast<SelfFn>(addr::SC4String_Destroy)(&name);
            return 0;
        }
        Virtual<uint32_t>(server, 0x4);
    }
    At<uint8_t>(self, kInitialized) = 1;
    reinterpret_cast<SelfFn>(addr::SC4String_Destroy)(&name);
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

// The inline clear of a vector of 12-byte vectors (the game's move of
// nothing left out): each element's storage freed, end = begin.
void ClearVectors(char* vector)
{
    char* const begin = At<char*>(vector, 0x0);
    char* const end = At<char*>(vector, 0x4);
    for (char* p = begin; p != end; p += 0xC)
        if (void* const block = At<void*>(p, 0x0))
            Free(block);
    At<char*>(vector, 0x4) = begin;
}

// 0x00706B60's lists in a record: each node's value (+8) through it.
void ForEachListed(char* self, char* record, std::size_t list)
{
    for (char* node = At<char*>(At<char*>(record, list), 0x0); node != At<char*>(record, list);
         node = At<char*>(node, 0x0))
        reinterpret_cast<ArgFn>(addr::cSC4TractDeveloper_Fn_00706B60)(self, node + 0x8);
}

// 0x0070AF60: for every record of every inner map of +0x17C, the two lists
// (+0x48, +0x88) through 0x00706B60 and the vectors of vectors after each
// (+0x4C, +0x8C) emptied.
void ClearRecords(char* self)
{
    static bool logged = false;
    FirstCall("ClearRecords", logged);
    for (char* outer = Left(At<char*>(self, kRecords)); outer != At<char*>(self, kRecords); outer = Next(outer)) {
        for (char* record = Left(At<char*>(outer, 0x14)); record != At<char*>(outer, 0x14); record = Next(record)) {
            ForEachListed(self, record, 0x48);
            ClearVectors(record + 0x4C);
            ForEachListed(self, record, 0x88);
            ClearVectors(record + 0x8C);
        }
    }
}

// Slot 4 (0x0070B210): the services released (the message server first),
// unregistered from the simulators, the objects of +0x130 handed back to
// service +0x24 and deleted, the records cleared and the record pool
// emptied. Always true.
uint8_t __fastcall Shutdown(char* self, void*)
{
    static bool logged = false;
    FirstCall("Shutdown", logged);
    if (!At<uint8_t>(self, kInitialized))
        return 1;
    At<uint8_t>(self, kInitialized) = 0;
    Release(self, kMessageServer);
    // The game does not check the simulator service for null here.
    void* const simulators = Virtual<void*>(Global<void*>(addr::SC4_City), 0xC8);
    Virtual<uint32_t>(simulators, 0x50, self + 0x4);
    for (const std::size_t offset : {0x18u, 0x1Cu, 0x20u, 0x28u})
        Release(self, offset);
    if (At<void*>(self, kService24)) {
        char* const tree = self + kObjects;
        for (char* node = Left(At<char*>(tree, 0x0)); node != At<char*>(tree, 0x0); node = Next(node)) {
            void* const object = At<void*>(node, 0x18);
            Virtual<uint32_t>(At<void*>(self, kService24), 0x4C, object);
            OperatorDelete(object);
        }
        if (At<uint32_t>(tree, 0x4)) {
            EraseNodes(tree, nullptr, Parent(At<char*>(tree, 0x0)));
            char* const head = At<char*>(tree, 0x0);
            At<char*>(head, 0x8) = head;
            At<char*>(head, 0x4) = nullptr;
            At<char*>(head, 0xC) = head;
            At<uint32_t>(tree, 0x4) = 0;
        }
        Release(self, kService24);
    }
    for (const std::size_t offset : {0x2Cu, 0x30u, 0x34u, 0x38u, 0x3Cu})
        Release(self, offset);
    ClearRecords(self);
    const auto destroyRecord = reinterpret_cast<VoidSelfFn>(addr::cSC4TractDeveloper_Fn_00705D10);
    // vector<record*> +0xE4 (in the member object): each destroyed and
    // deleted, then end = begin.
    for (char** p = At<char**>(self, 0xE4); p != At<char**>(self, 0xE8); ++p) {
        if (char* const record = *p) {
            destroyRecord(record);
            OperatorDelete(record);
        }
    }
    At<char*>(self, 0xE8) = At<char*>(self, 0xE4);
    // The static record pool, the same way.
    char**& poolBegin = Global<char**>(addr::cSC4TractDeveloper_CandidatePool);
    char**& poolEnd = Global<char**>(addr::cSC4TractDeveloper_CandidatePool + 4);
    for (char** p = poolBegin; p != poolEnd; ++p) {
        if (char* const record = *p) {
            destroyRecord(record);
            OperatorDelete(record);
        }
    }
    poolEnd = poolBegin;
    return 1;
}

// --- Save --------------------------------------------------------------------------

// cIGZSerializable slot 3 (0x00704B60), on the part at +0x8: version 3 (see
// the record at the top). False when the stream is in error after.
uint32_t __fastcall Write(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    char* const self = part - 0x8;
    Virtual<uint8_t>(stream, 0x1C, static_cast<uint16_t>(3));
    Virtual<uint8_t>(stream, 0x20, At<int32_t>(self, kStyleIndex));
    Virtual<uint8_t>(stream, 0x20, At<int32_t>(self, kYearsSince));
    reinterpret_cast<WriteBoolFn>(addr::SC4Stream_WriteBool)(stream, At<uint8_t>(self, kOneAtATime));
    const uint32_t count = Size(self, kStyles);
    Virtual<uint8_t>(stream, 0x24, count);
    for (uint32_t i = 0; i < count; ++i)
        Virtual<uint8_t>(stream, 0x24, At<uint32_t*>(self, kStyles)[i]);
    Virtual<uint8_t>(stream, 0x20, At<int32_t>(self, kYearsBetween));
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// cIGZSerializable slot 4 (0x00707950): versions 0-3 (false for later ones,
// nothing else read), the fields of the record at the top. False when the
// stream is in error after.
uint32_t __fastcall Read(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Read", logged);
    char* const self = part - 0x8;
    // The game reads the version into the stack slot of its stream
    // argument: when the read writes nothing, the pointer's low 16 bits
    // are compared.
    uint16_t version = static_cast<uint16_t>(reinterpret_cast<std::uintptr_t>(stream));
    Virtual<uint8_t>(stream, 0x1C, &version);
    if (version > 3)
        return 0;
    Virtual<uint8_t>(stream, 0x20, self + kStyleIndex);
    Virtual<uint8_t>(stream, 0x20, self + kYearsSince);
    const uint32_t zero = 0;
    if (version >= 2) {
        reinterpret_cast<ReadBoolFn>(addr::SC4Stream_ReadBool)(stream, reinterpret_cast<uint8_t*>(self + kOneAtATime));
        // A count the read does not write stays the game's stack; 0 here.
        uint32_t count = 0;
        Virtual<uint8_t>(stream, 0x24, &count);
        Resize(self + kStyles, count, &zero);
        for (uint32_t i = 0; i < count; ++i)
            Virtual<uint8_t>(stream, 0x24, At<uint32_t*>(self, kStyles) + i);
    } else {
        At<uint8_t>(self, kOneAtATime) = 1;
        Resize(self + kStyles, 3, &zero);
        At<uint32_t*>(self, kStyles)[0] = 0x2000;
        At<uint32_t*>(self, kStyles)[1] = 0x2001;
        At<uint32_t*>(self, kStyles)[2] = 0x2002;
    }
    if (version >= 3)
        Virtual<uint8_t>(stream, 0x20, self + kYearsBetween);
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// --- Messages ----------------------------------------------------------------------

// cIGZMessageTarget2 slot 3 (0x007048E0), on the part at +0x4: on
// 0x66956817 (a year, by the simulator registration) the years since the
// last style change counted; at the years between changes the next active
// style becomes current (back to the first after the last). Always true.
uint8_t __fastcall DoMessage(char* part, void*, void* message)
{
    static bool logged = false;
    FirstCall("DoMessage", logged);
    char* const self = part - 0x4;
    if (Virtual<uint32_t>(message, 0x10) != kMessageStyleYear)
        return 1;
    const int32_t years = ++At<int32_t>(self, kYearsSince);
    if (years < At<int32_t>(self, kYearsBetween))
        return 1;
    const uint32_t index = ++At<uint32_t>(self, kStyleIndex);
    At<int32_t>(self, kYearsSince) = 0;
    if (index >= Size(self, kStyles))
        At<uint32_t>(self, kStyleIndex) = 0;
    return 1;
}

// --- Styles ------------------------------------------------------------------------

// Slot 8 (0x0070C8D0): the current style, 0 when there are no active styles.
uint32_t __fastcall GetCurrentStyle(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetCurrentStyle", logged);
    if (At<char*>(self, kStyles) == At<char*>(self, kStyles + 4))
        return 0;
    return At<uint32_t*>(self, kStyles)[At<int32_t>(self, kStyleIndex)];
}

// Slot 9 (0x0070C840): the active styles' vector.
void* __fastcall GetActiveStyles(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetActiveStyles", logged);
    return self + kStyles;
}

// Slot 10 (0x00707470): the active styles replaced. The current style keeps
// being current when the new list has it (its last position); else the
// index stays when it fits the new list, else 0. Returns what the vector
// assignment leaves in EAX.
uint32_t __fastcall SetActiveStyles(char* self, void*, const uint32_t* const* styles)
{
    static bool logged = false;
    FirstCall("SetActiveStyles", logged);
    const uint32_t* const first = styles[0];
    const uint32_t count = static_cast<uint32_t>((styles[1] - styles[0]));
    const uint32_t index = At<uint32_t>(self, kStyleIndex);
    uint32_t chosen = index < count ? index : 0;
    // Read without a bounds check (an empty list reads through null), as the game.
    const uint32_t current = At<uint32_t*>(self, kStyles)[index];
    for (uint32_t i = 0; i < count; ++i)
        if (first[i] == current)
            chosen = i;
    const uint32_t result = Assign(self + kStyles, styles);
    At<uint32_t>(self, kStyleIndex) = chosen;
    return result;
}

// Slot 11 (0x0070C850): true when all styles are used at once (+0x12C == 0).
uint32_t __fastcall IsUsingAllStylesAtOnce(char* self, void*)
{
    static bool logged = false;
    FirstCall("IsUsingAllStylesAtOnce", logged);
    return At<uint8_t>(self, kOneAtATime) == 0 ? 1 : 0;
}

// Slot 12 (0x0070C860): +0x12C = !flag; returns that byte (the game leaves
// EAX's upper bytes as they were).
uint8_t __fastcall UseAllStylesAtOnce(char* self, void*, uint32_t flag)
{
    static bool logged = false;
    FirstCall("UseAllStylesAtOnce", logged);
    const uint8_t oneAtATime = static_cast<uint8_t>(flag) == 0 ? 1 : 0;
    At<uint8_t>(self, kOneAtATime) = oneAtATime;
    return oneAtATime;
}

// Slot 14 (0x0070C880): +0x7C = years; returns years.
int32_t __fastcall SetYearsBetweenStyleChanges(char* self, void*, int32_t years)
{
    static bool logged = false;
    FirstCall("SetYearsBetweenStyleChanges", logged);
    At<int32_t>(self, kYearsBetween) = years;
    return years;
}

// --- Decline and UpdatePopulation ----------------------------------------------------

// The growth profile both get (the building development simulator's cell
// key): the growth developer and a tract position.
struct Profile {
    void* developer;
    int32_t x;
    int32_t z;
};

// The developer's tract grid's shift (cells per tract = 1 << shift).
uint8_t TractShift(void* developer)
{
    return Virtual<uint8_t>(Virtual<void*>(developer, 0x34), 0x2C);
}

// Slot 6 (0x00709200): the lots of the tract that have stood in state 3
// for at least 60 days and hold a building of the developer's purpose
// (building info's first dword == developer slot 7) and wealth (slot 8)
// are handed to service +0x28 (slot 8, reason 6). Returns the sum of their
// capacities for the developer's type (the header's bool is wrong; the
// building development simulator tests > 0).
uint32_t __fastcall Decline(char* self, void*, const Profile* profile)
{
    static bool logged = false;
    FirstCall("Decline", logged);
    // The function-local static iterator, in the game's memory.
    char* const walk = reinterpret_cast<char*>(addr::cSC4TractDeveloper_DeclineWalk);
    uint32_t& guard = Global<uint32_t>(addr::cSC4TractDeveloper_DeclineWalkGuard);
    if (!(guard & 1)) {
        guard |= 1;
        At<uint8_t>(walk, 0x0) = 1;
        At<uint8_t>(walk, 0x1) = 1;
        At<uint8_t>(walk, 0x2) = 1;
        for (std::size_t offset = 0x4; offset <= 0x28; offset += 4)
            At<uint32_t>(walk, offset) = 0;
        At<uint32_t>(walk, 0x2C) = 1;
        reinterpret_cast<AtExitFn>(addr::SC4_atexit)(
            reinterpret_cast<void (*)()>(addr::cSC4TractDeveloper_DeclineWalkAtExit));
    }
    void* const developer = profile->developer;
    uint32_t total = 0;
    const int shift = TractShift(developer) & 31;
    const int32_t x = profile->x;
    const int32_t z = profile->z;
    At<int32_t>(walk, 0x4) = x << shift;
    At<int32_t>(walk, 0x8) = z << shift;
    At<int32_t>(walk, 0xC) = ((x + 1) << shift) - 1;
    At<int32_t>(walk, 0x10) = ((z + 1) << shift) - 1;
    if (At<int32_t>(walk, 0xC) < At<int32_t>(walk, 0x4))
        At<int32_t>(walk, 0xC) = At<int32_t>(walk, 0x4);
    if (At<int32_t>(walk, 0x10) < At<int32_t>(walk, 0x8))
        At<int32_t>(walk, 0x10) = At<int32_t>(walk, 0x8);
    At<uint8_t>(walk, 0x2) = 1;
    reinterpret_cast<WalkRestartFn>(addr::TractIterator_Restart)(walk, 1);
    // The game uses its stack argument's slot for the z coordinate.
    int32_t cellX = 0, cellZ = 0;
    const auto next = reinterpret_cast<WalkNextFn>(addr::TractIterator_Next);
    while (next(walk, &cellX, &cellZ)) {
        void* const lot = Virtual<void*>(At<void*>(self, kLots), 0x18, cellX, cellZ, 0u);
        if (!lot || Virtual<int32_t>(lot, 0x84) != 3 || Virtual<uint32_t>(lot, 0xA0) < 0x3C)
            continue;
        const uint32_t buildingType = Virtual<uint32_t>(lot, 0xB8, 0u);
        if (!buildingType)
            continue;
        const int32_t* const info = Virtual<const int32_t*>(At<void*>(self, kBuildings), 0x9C, buildingType);
        const uint32_t wealth = Virtual<uint32_t>(lot, 0xA4);
        const int32_t purpose = Virtual<int32_t>(developer, 0x1C);
        if (*info != purpose)
            continue;
        if (wealth != Virtual<uint32_t>(developer, 0x20))
            continue;
        const uint32_t type = Virtual<uint32_t>(developer, 0x18);
        const uint16_t capacity = Virtual<uint16_t>(lot, 0xC8, type, 0u);
        total += capacity;
        Virtual<uint8_t>(At<void*>(self, kService28), 0x20, lot, 6u);
    }
    return total;
}

// Slot 7 (0x00706BC0): each lot of the tract that is livable (slot 38) and
// of the developer's wealth gets the developer type's population = its
// capacity (slot 52) x a factor: the tract's population (developer grid
// value x 0.01) x (1 + uniform(-noise, noise)), 0 when below 0, 1 -
// uniform(+0x64, +0x68) when 1 or more; rounded to nearest. The generator
// is seeded from the tract position. The game leaves SC4::Free's EAX as the
// result (callers do not use it); so do we.
uint32_t __fastcall UpdatePopulation(char* self, void*, const Profile* profile)
{
    static bool logged = false;
    FirstCall("UpdatePopulation", logged);
    void* const developer = profile->developer;
    void* const grid = Virtual<void*>(developer, 0x34);
    const int shift = Virtual<uint8_t>(grid, 0x2C) & 31;
    char* list = nullptr;
    list = static_cast<char*>(Allocate(0xC));
    At<char*>(list, 0x0) = list;
    At<char*>(list, 0x4) = list;
    const int32_t x = profile->x;
    const int32_t z = profile->z;
    Virtual<void>(At<void*>(self, kLots), 0x1C, &list, x << shift, z << shift, ((x + 1) << shift) - 1,
                  ((z + 1) << shift) - 1, 1u);
    const auto freeRet = reinterpret_cast<FreeRetFn>(addr::SC4_Free);
    if (At<char*>(list, 0x0) == list) {
        reinterpret_cast<VoidSelfFn>(addr::SC4RefList_Clear)(&list);
        if (!list)
            return 0;
        return freeRet(list);
    }
    const int16_t value = Virtual<int16_t>(grid, 0x58, profile->x, profile->z);
    // x87: fild, fmul by the float 0.01, stored as float.
    const float population = static_cast<float>(static_cast<double>(static_cast<int32_t>(value)) *
                                                static_cast<double>(Global<float>(addr::SC4Traffic_FerryCostScale)));
    const uint32_t type = Virtual<uint32_t>(developer, 0x18);
    const uint32_t wealth = Virtual<uint32_t>(developer, 0x20);
    alignas(8) unsigned char random[0x18];
    const uint32_t seed = ((static_cast<uint32_t>(profile->x) << 16) + static_cast<uint32_t>(profile->z)) << 4;
    reinterpret_cast<SeedFn>(addr::SC4Random_Construct)(random, seed);
    const auto uniform = reinterpret_cast<UniformFn>(addr::SC4Random_Uniform);
    for (char* node = At<char*>(list, 0x0); node != list; node = At<char*>(node, 0x0)) {
        void* const lot = At<void*>(node, 0x8);
        if (!Virtual<uint8_t>(lot, 0x98) || Virtual<uint32_t>(lot, 0xA4) != wealth)
            continue;
        const uint16_t capacity = Virtual<uint16_t>(lot, 0xD0, type, 0u);
        if (!capacity)
            continue;
        const double noise = static_cast<double>(At<float>(self, kNoise));
        double factor = (uniform(random, -noise, noise) + Global<double>(addr::SC4_DoubleOne)) *
                        static_cast<double>(population);
        // fcom against the floats 0.0 and 1.0: NaN is kept.
        if (factor < static_cast<double>(Global<float>(addr::SC4_FloatZero)))
            factor = static_cast<double>(Global<float>(addr::SC4_FloatZero));
        else if (factor >= static_cast<double>(Global<float>(addr::SC4_FloatOneA81228)))
            factor = Global<double>(addr::SC4_DoubleOne) - uniform(random, static_cast<double>(At<float>(self, kVacancyLow)),
                                                                   static_cast<double>(At<float>(self, kVacancyHigh)));
        // fild, fmul, fadd 1.5 x 2^23 stored as float: its bits are passed
        // (the low bits are the product rounded to nearest).
        const float rounded = static_cast<float>(static_cast<double>(static_cast<int32_t>(capacity)) * factor +
                                                 static_cast<double>(Global<float>(addr::cSC4TractDeveloper_RoundMagic)));
        uint32_t bits;
        std::memcpy(&bits, &rounded, sizeof bits);
        Virtual<uint8_t>(lot, 0xDC, type, bits);
    }
    // The list freed (each lot released), then its head.
    char* node = At<char*>(list, 0x0);
    while (node != list) {
        char* const current = node;
        void* const lot = At<void*>(current, 0x8);
        node = At<char*>(node, 0x0);
        if (lot)
            Virtual<uint32_t>(lot, 0x8);
        Free(current);
    }
    At<char*>(list, 0x0) = list;
    At<char*>(list, 0x4) = list;
    return freeRet(list);
}

// --- Installation of the slot groups ----------------------------------------------

struct Slot {
    std::uintptr_t vtable;
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(vtable, index, original, fn) {vtable, index, original, reinterpret_cast<void*>(&fn)}

const Slot kInitSlots[] = {OPENSC4_SLOT(addr::cSC4TractDeveloper_vftable, 3, addr::cSC4TractDeveloper_Init, Init)};
const Slot kShutdownSlots[] = {
    OPENSC4_SLOT(addr::cSC4TractDeveloper_vftable, 4, addr::cSC4TractDeveloper_Shutdown, Shutdown)};
const Slot kSaveSlots[] = {
    OPENSC4_SLOT(addr::cSC4TractDeveloper_vftable_8, 3, addr::cSC4TractDeveloper_Fn_00704B60, Write)};
const Slot kLoadSlots[] = {
    OPENSC4_SLOT(addr::cSC4TractDeveloper_vftable_8, 4, addr::cSC4TractDeveloper_Fn_00707950, Read)};
const Slot kMessageSlots[] = {
    OPENSC4_SLOT(addr::cSC4TractDeveloper_vftable_4, 3, addr::cSC4TractDeveloper_Fn_007048E0, DoMessage)};
const Slot kStyleSlots[] = {
    OPENSC4_SLOT(addr::cSC4TractDeveloper_vftable, 8, addr::cSC4TractDeveloper_GetCurrentStyle, GetCurrentStyle),
    OPENSC4_SLOT(addr::cSC4TractDeveloper_vftable, 9, addr::cSC4TractDeveloper_GetActiveStyles, GetActiveStyles),
    OPENSC4_SLOT(addr::cSC4TractDeveloper_vftable, 10, addr::cSC4TractDeveloper_SetActiveStyles, SetActiveStyles),
    OPENSC4_SLOT(addr::cSC4TractDeveloper_vftable, 11, addr::cSC4TractDeveloper_IsUsingAllStylesAtOnce,
                 IsUsingAllStylesAtOnce),
    OPENSC4_SLOT(addr::cSC4TractDeveloper_vftable, 12, addr::cSC4TractDeveloper_UseAllStylesAtOnce,
                 UseAllStylesAtOnce),
    OPENSC4_SLOT(addr::cSC4TractDeveloper_vftable, 14, addr::cSC4TractDeveloper_SetYearsBetweenStyleChanges,
                 SetYearsBetweenStyleChanges)};
const Slot kDeclineSlots[] = {
    OPENSC4_SLOT(addr::cSC4TractDeveloper_vftable, 6, addr::cSC4TractDeveloper_Decline, Decline),
    OPENSC4_SLOT(addr::cSC4TractDeveloper_vftable, 7, addr::cSC4TractDeveloper_UpdatePopulation, UpdatePopulation)};

#undef OPENSC4_SLOT

template <std::size_t N>
bool SwapSlots(const Slot (&slots)[N], const char* what)
{
    if (!game::IsBuild641())
        return false;
    for (const Slot& slot : slots) {
        const auto* const vtable = reinterpret_cast<const std::uintptr_t*>(slot.vtable);
        if (vtable[slot.index] != slot.original) {
            log::Write("tract %s: slot %d of %08X holds %08X, expected %08X; not installing", what, slot.index,
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
    log::Write("tract %s: %u slots are ours", what, static_cast<unsigned>(N));
    return true;
}

using EraseNodesFn = void(__fastcall*)(void*, void*, char*);
EraseNodesFn g_originalEraseNodes = nullptr;

bool InstallInit(registry::Mode) { return SwapSlots(kInitSlots, "init"); }

// Shutdown's slot, and a detour on 0x00704A50 (also used by the game's
// destructor; its callers keep nothing in ECX/EDX/EAX across it).
bool InstallShutdown(registry::Mode)
{
    if (!SwapSlots(kShutdownSlots, "shutdown"))
        return false;
    const addr::Entry* const entry = addr_check::Find(addr::cSC4TractDeveloper_Fn_00704A50);
    return entry && hook::Install(*entry, &EraseNodes, &g_originalEraseNodes);
}

bool InstallSave(registry::Mode) { return SwapSlots(kSaveSlots, "save"); }
bool InstallLoad(registry::Mode) { return SwapSlots(kLoadSlots, "load"); }
bool InstallMessages(registry::Mode) { return SwapSlots(kMessageSlots, "messages"); }
bool InstallStyles(registry::Mode) { return SwapSlots(kStyleSlots, "styles"); }
bool InstallDecline(registry::Mode) { return SwapSlots(kDeclineSlots, "decline"); }

const bool g_registered[] = {
    registry::Register({"simulators.tract.init", "simulators/tract", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4TractDeveloper_vftable,
                        "Our tract developer Init and its tuning load (docs/tract.md)", false, InstallInit}),
    registry::Register({"simulators.tract.shutdown", "simulators/tract", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4TractDeveloper_vftable,
                        "Our tract developer Shutdown and its record clean-up; detour of the tree erase 0x00704A50 "
                        "(docs/tract.md)",
                        false, InstallShutdown}),
    registry::Register({"simulators.tract.save", "simulators/tract", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4TractDeveloper_vftable_8,
                        "Our tract developer Write: the save record, version 3 (docs/tract.md)", false, InstallSave}),
    registry::Register({"simulators.tract.load", "simulators/tract", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4TractDeveloper_vftable_8,
                        "Our tract developer Read, versions 0-3 (docs/tract.md)", false, InstallLoad}),
    registry::Register({"simulators.tract.messages", "simulators/tract", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4TractDeveloper_vftable_4,
                        "Our tract developer DoMessage: the yearly style change (docs/tract.md)", false,
                        InstallMessages}),
    registry::Register({"simulators.tract.styles", "simulators/tract", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4TractDeveloper_vftable,
                        "Our tract developer style slots 8-12 and 14 (docs/tract.md)", false, InstallStyles}),
    registry::Register({"simulators.tract.decline", "simulators/tract", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4TractDeveloper_vftable,
                        "Our tract developer Decline and UpdatePopulation (docs/tract.md)", false, InstallDecline}),
};

// --- Our object -------------------------------------------------------------------

// Copies of the game's 0x00AB3118 (16 slots, deleting destructor last),
// 0x00AB3104 (at +4) and 0x00AB30EC (at +8), taken at the first
// construction, after every PostAppInit slot swap of the other groups.
constexpr int kSlots = 16, kSlots4 = 4, kSlots8 = 6;
void* g_vtable[kSlots];
void* g_vtable4[kSlots4];
void* g_vtable8[kSlots8];

// As the game's (0x004F7BA0, shared with other classes).
uint32_t __fastcall AddRef(char* self, void*) { return ++At<uint32_t>(self, kRefCount); }

// As the game's (0x007045B0).
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    std::size_t offset;
    switch (iid) {
    case kIidUnknown:
    case kIidDeveloper: offset = 0; break;
    case kIidMessageTarget2: offset = 0x4; break;
    case kIidSerializable: offset = 0x8; break;
    default: return 0;
    }
    *object = self ? self + offset : nullptr;
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// As the game's (0x007048B0): Shutdown and the deleting destructor (slot
// 15) at the last reference.
uint32_t __fastcall ReleaseObject(char* self, void*)
{
    int32_t& refs = At<int32_t>(self, kRefCount);
    if (refs > 1)
        return static_cast<uint32_t>(--refs);
    Virtual<uint8_t>(self, 0x10);
    Virtual<void*>(self, 0x3C, 1u);
    return 0;
}

// 0x00706430: the member object at +0x80's destructor (its trees and vector).
void DestroyMember(char* member)
{
    static bool logged = false;
    FirstCall("DestroyMember", logged);
    DestroyTree(member + 0x8C, addr::cSC4TractDeveloper_Fn_00704A10);
    if (void* const block = At<void*>(member, 0x64))
        Free(block);
    DestroyTree(member + 0x58, addr::cSC4TractDeveloper_Fn_007049D0);
    DestroyTree(member + 0x4C, addr::cSC4TractDeveloper_Fn_00704990);
}

// The destructor (0x0070C750): only what it frees; the services and records
// are Shutdown's.
void Destroy(char* self)
{
    At<void**>(self, 0x0) = g_vtable;
    At<void**>(self, 0x4) = g_vtable4;
    At<void**>(self, 0x8) = g_vtable8;
    DestroyTree(self + kRecords, addr::cSC4TractDeveloper_Fn_0070C3F0);
    // Generic (shared): left the game's.
    reinterpret_cast<VoidSelfFn>(addr::SC4ShortVectorVector_Destroy)(self + kRows);
    // Grow's (0x007075C0); the list head after.
    reinterpret_cast<VoidSelfFn>(addr::cSC4TractDeveloper_Fn_007075C0)(self + kRecordLists);
    if (void* const head = At<void*>(self, kRecordLists))
        Free(head);
    DestroyTree(self + kObjects, 0);
    if (void* const block = At<void*>(self, kStyles))
        Free(block);
    DestroyMember(self + kMember);
    if (void* const block = At<void*>(self, kTuningStyles))
        Free(block);
}

// Slot 15, as the game's (0x0070CA80).
void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    static int destroyed = 0;
    if (destroyed++ < 3)
        log::Write("tract object: destroying %p", static_cast<void*>(self));
    Destroy(self);
    if (flags & 1)
        OperatorDelete(self);
    return self;
}

// As the game's (0x007045A0).
uint32_t __fastcall GetGZCLSID(void*, void*) { return kClassId; }

// The game's thunks 0x0070C890/0x0070C8A0 (QueryInterface) and
// 0x0070C8B0/0x0070C8C0 (Release) at +4 and +8; AddRef at +4/+8 is shared
// code (0x0068D230/0x0068D250).
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

void BuildVtables()
{
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4TractDeveloper_vftable), sizeof(g_vtable));
    std::memcpy(g_vtable4, reinterpret_cast<const void*>(addr::cSC4TractDeveloper_vftable_4), sizeof(g_vtable4));
    std::memcpy(g_vtable8, reinterpret_cast<const void*>(addr::cSC4TractDeveloper_vftable_8), sizeof(g_vtable8));
    g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
    g_vtable[1] = reinterpret_cast<void*>(&AddRef);
    g_vtable[2] = reinterpret_cast<void*>(&ReleaseObject);
    g_vtable[15] = reinterpret_cast<void*>(&DeletingDestructor);
    g_vtable4[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x4>);
    g_vtable4[1] = reinterpret_cast<void*>(&AddRefAt<0x4>);
    g_vtable4[2] = reinterpret_cast<void*>(&ReleaseAt<0x4>);
    g_vtable8[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x8>);
    g_vtable8[1] = reinterpret_cast<void*>(&AddRefAt<0x8>);
    g_vtable8[2] = reinterpret_cast<void*>(&ReleaseAt<0x8>);
    g_vtable8[5] = reinterpret_cast<void*>(&GetGZCLSID);
    log::Write("tract object: vtables copied (slot 3 %p, slot 5 %p, +4 slot 3 %p, +8 slots 3/4 %p %p)", g_vtable[3],
               g_vtable[5], g_vtable4[3], g_vtable8[3], g_vtable8[4]);
}

// 0x00706390: the member object at +0x80's constructor (three tree heads of
// 0x14, 0x14 and 0x18 bytes, in that order; the vector +0xE4 cleared).
void ConstructMember(char* member)
{
    NewTree(member + 0x4C, 0x14);
    NewTree(member + 0x58, 0x14);
    At<uint32_t>(member, 0x64) = 0;
    At<uint32_t>(member, 0x68) = 0;
    At<uint32_t>(member, 0x6C) = 0;
    NewTree(member + 0x8C, 0x18);
}

// The constructor (detour of 0x0070C900; its only caller is the factory
// Create 0x0070CAA0, which jumps to it): the fields the game's sets, in its
// order of allocations (0x14, 0x14, 0x18, 0x1C, 0x3C, 0x20 bytes). The
// game's first, overwritten stores of base vtables at +4/+8 are left out.
void* __fastcall Construct(char* self, void*)
{
    static bool built = false;
    if (!built) {
        built = true;
        BuildVtables();
    }
    At<void**>(self, 0x0) = g_vtable;
    At<void**>(self, 0x4) = g_vtable4;
    At<void**>(self, 0x8) = g_vtable8;
    At<uint8_t>(self, kInitialized) = 0;
    for (std::size_t offset = 0x10; offset <= 0x3C; offset += 4)
        At<uint32_t>(self, offset) = 0;
    At<float>(self, 0x40) = 1.5f;
    At<uint32_t>(self, 0x44) = 0;
    At<float>(self, 0x48) = 1.2f;
    At<float>(self, 0x4C) = 1.5f;
    At<float>(self, 0x50) = 1.0f;
    At<float>(self, 0x54) = 0.2f;
    At<float>(self, 0x58) = 10.0f;
    At<float>(self, 0x5C) = 5.0f;
    At<float>(self, 0x60) = 0.2f;
    At<float>(self, 0x64) = 0.1f;
    At<float>(self, 0x68) = 0.1f;
    At<uint8_t>(self, 0x6C) = 1;
    for (std::size_t offset = 0x70; offset <= 0x7C; offset += 4)
        At<uint32_t>(self, offset) = 0;
    ConstructMember(self + kMember);
    for (std::size_t offset = 0x118; offset <= 0x128; offset += 4)
        At<uint32_t>(self, offset) = 0;
    At<uint8_t>(self, kOneAtATime) = 1;
    NewTree(self + kObjects, 0x1C);
    At<std::uintptr_t>(self, 0x13C) = addr::cRZDate_vftable;
    At<uint32_t>(self, 0x140) = 0;
    At<char*>(self, kRecordLists) = nullptr;
    char* const head = static_cast<char*>(Allocate(0x3C));
    At<char*>(head, 0x0) = head;
    At<char*>(head, 0x4) = head;
    At<char*>(self, kRecordLists) = head;
    At<uint32_t>(self, 0x170) = 0;
    At<uint32_t>(self, 0x174) = 0;
    At<uint32_t>(self, 0x178) = 0;
    NewTree(self + kRecords, 0x20);
    static int created = 0;
    if (created++ < 3)
        log::Write("tract object: constructed %p", static_cast<void*>(self));
    return self;
}

using ConstructFn = void*(__fastcall*)(char*, void*);
ConstructFn g_originalConstruct = nullptr;

bool ApplyObject(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    const addr::Entry* const entry = addr_check::Find(addr::cSC4TractDeveloper_cSC4TractDeveloper);
    return entry && hook::Install(*entry, &Construct, &g_originalConstruct);
}

const bool g_registeredObject = registry::Register({
    "simulators.tract.object",
    "simulators/tract",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4TractDeveloper_cSC4TractDeveloper,
    "Our cSC4TractDeveloper object: constructor (detour), destructor, identity; methods from copies of the "
    "(patched) game vtables (docs/tract.md)",
    false,
    ApplyObject,
});

} // namespace
} // namespace opensc4::tract_sim
