// Our cSC4PropDeveloper (build 641), the object part: everything from
// 0x006F0D70 up to 0x006F3620. The object (constructor, destructor,
// identity), Init, Shutdown, Write, the requesters with the wealth requester
// (cSC4WealthRequester, a 12-byte class inside the range), the signs and
// labels slots, the handlers DoMessage and the optional props code call, and
// the container helpers only this class uses. Read, the optional props and
// DoMessage are the props part (src/core/prop_props.cpp).
//
// The registry groups (all off by default, each switchable alone):
//   simulators.prop.sim        slots 3-6 and 8-10 of the primary vtable
//   simulators.prop.save       Write (+8 slot 3)
//   simulators.prop.handlers   detours on the nine functions the props part
//                              calls at the game's address
//   simulators.prop.requester  the wealth requester's slots and factory
//   simulators.prop.containers detours on five container helpers the props
//                              part calls
//   simulators.prop.object     our own object (detour on the constructor)
//
// Fields are listed in prop_sim.h. What this file verified beyond it:
//   an optional prop item (16 bytes, the value of a 0x18-byte list node):
//     +0 uint8 cell x, +1 uint8 cell z, +4 pointer to the lot
//     configuration's vector of 0x34-byte prop slots, +8 the slot's index;
//     a slot has its ID at +0x24 and a vector<uint32_t> of candidate props
//     or prop families at +0x28
//   a prop's record (service 0x00B43D10 slot 13): +0x10 the requester type
//     that chooses it, +0x14 uint8 the wealth it is for
//   a requester (cISC4PropRequester): slot 4 its type, slot 5 whether it
//     chooses itself, slot 6 the choice (candidates, cell x, cell z)
#include "core/prop_sim.h"

#include "core/address_check.h"
#include "core/fixed_seed.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::prop_sim {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("prop: first %s", name);
}

namespace {

constexpr uint32_t kClassId = 0x89C48F47;
constexpr uint32_t kRequesterClassId = 0x89C49986;
constexpr uint32_t kIidUnknown = 0x00000001;
constexpr uint32_t kIidDeveloper = 0xE9A1F4B6;
constexpr uint32_t kIidMessageTarget2 = 0x452294AA;
constexpr uint32_t kIidSerializable = 0xE4FDA3D4;
constexpr uint32_t kIidRequester = 0x89C498BE;
constexpr uint32_t kIidTimedProp = 0xE9793A65;
constexpr uint32_t kRequesterType = 0x49A1E995;
constexpr uint32_t kSignOnTerrain = 0xAB72FBB3;
constexpr uint32_t kSignOnNetwork = 0xCB79919B;
// Init's and Shutdown's messages, in their order.
constexpr uint32_t kMessages[13] = {0x99EF1142, 0x99EF1143, 0xC9DA96E8, 0xC9DA96E9, 0x44778231,
                                    0x44778234, 0x26D31EC1, 0xE8BDA2E7, 0x8A07AE00, 0xAA375FE8,
                                    0xCA5E5027, 0x278E6003, 0x678E6E3A};

// A list node of pointers (12 bytes) and of optional prop items (0x18).
struct Node {
    Node* next;
    Node* prev;
    void* value;
};

struct ItemNode {
    ItemNode* next;
    ItemNode* prev;
    uint32_t value[4];
};

struct Vector {
    char* begin;
    char* end;
    char* capacity;
};

// A node of the table +0xA48 and the table's iterator.
struct TableNode {
    TableNode* next;
    uint32_t key;
    Node* list;
};

struct TableIterator {
    TableNode* node;
    char* table;
};

struct Position {
    float x;
    float y;
    float z;
};

struct BitIterator {
    uint32_t* word;
    uint32_t bit;
};

struct BitVector {
    uint32_t* start;
    uint32_t startBit;
    uint32_t* finish;
    uint32_t finishBit;
    uint32_t* endOfStorage;
};

struct GameString {
    const void* vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};

using NewFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* block);
using SelfFn = void*(__thiscall*)(void* self);
using GetFn = uint8_t(__cdecl*)(void* holder, uint32_t property, void* value);
using SetFn = void(__cdecl*)(void* holder, uint32_t property, uint32_t value);
using RefFn = uint8_t(__cdecl*)(void* stream, void* object);
using BoolFn = void*(__cdecl*)(void* stream, uint32_t value);
using IteratorFn = void*(__cdecl*)(void* stream, void* iterator);
using InsertNFn = void(__thiscall*)(void* vector, void* where, const void* value, void* allocator, uint32_t count,
                                    uint32_t atEnd);
using CountFn = void(__thiscall*)(void* self, uint32_t count);
using BelowFn = uint32_t(__thiscall*)(void* random, uint32_t n);
using SeedFn = void*(__thiscall*)(void* random, uint32_t seed);
using TimeFn = uint32_t*(__thiscall*)(uint32_t* out);
using ServiceFn = void*(__cdecl*)();
using TextFn = void*(__thiscall*)(void* string, const char* text);
using BeginFn = void*(__thiscall*)(void* table, TableIterator* out);
using FindFn = void(__thiscall*)(void* table, TableIterator* out, const uint32_t* key);
using TableEraseFn = void(__thiscall*)(void* table, const TableIterator* where);
using TableInsertFn = void(__thiscall*)(void* table, void* out, const void* value);
using ListFindFn = void(__cdecl*)(Node** out, Node* first, Node* last, void* const* value);
using ListEraseFn = void(__thiscall*)(void* list, Node** out, Node* node);
using ListRemoveFn = void(__thiscall*)(void* list, void* const* value);
using RangeEraseFn = void(__thiscall*)(void* list, ItemNode** out, ItemNode* first, ItemNode* last);
using RangeInsertFn = void(__thiscall*)(void* list, Node* where, Node* first, Node* last, void* dummy);
using DestroyRangeFn = void(__cdecl*)(void* first, void* last, void* dummy);
using BitCopyFn = BitIterator*(__cdecl*)(BitIterator* out, uint32_t* first, uint32_t firstBit, uint32_t* last,
                                         uint32_t lastBit, uint32_t* dest, uint32_t destBit);

const auto OperatorNew = reinterpret_cast<NewFn>(addr::SC4_operator_new);
const auto OperatorDelete = reinterpret_cast<FreeFn>(addr::SC4_operator_delete);
const auto Allocate = reinterpret_cast<NewFn>(addr::SC4_stl_allocate);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);
const auto GetUint8 = reinterpret_cast<GetFn>(addr::SC4_GetPropertyUint8);
const auto GetUint32 = reinterpret_cast<GetFn>(addr::SC4_GetPropertyUint32);
const auto SetUint32 = reinterpret_cast<SetFn>(addr::SC4_SetPropertyUint32);
const auto WriteRef = reinterpret_cast<RefFn>(addr::SC4Stream_WriteReference);
const auto WriteBool = reinterpret_cast<BoolFn>(addr::SC4Stream_WriteBool);
const auto Below = reinterpret_cast<BelowFn>(addr::SC4Random_Below);
// Generic container code other classes share; left the game's.
const auto InsertN = reinterpret_cast<InsertNFn>(addr::SC4PtrVector_InsertN);
const auto PairListDestroy = reinterpret_cast<SelfFn>(addr::SC4PairList_Destroy);
const auto ListClear2 = reinterpret_cast<SelfFn>(addr::SC4List_Clear2);
const auto ListDestroy2 = reinterpret_cast<SelfFn>(addr::SC4List_Destroy2);
const auto DestroyListRange = reinterpret_cast<DestroyRangeFn>(addr::SC4StdList_DestroyListRange);

// The truncating float-to-int conversion (_ftol, 0x009EEF04): the low dword.
int32_t Ftol(double value) { return static_cast<int32_t>(static_cast<int64_t>(value)); }

int32_t Count4(const char* first, const char* last) { return static_cast<int32_t>((last - first) >> 2); }

Vector* Requesters(char* self) { return reinterpret_cast<Vector*>(self + field::kRequesters); }
Vector* Timed(char* self) { return reinterpret_cast<Vector*>(self + field::kTimed); }
Node* List(char* self, std::size_t offset) { return At<Node*>(self, offset); }

// push_back of a vector of 32-bit values: in place while there is room,
// else through the game's insert (its allocator argument is a dummy the
// game points at whatever stack slot is near).
void PushBack(Vector* vector, uint32_t value)
{
    if (vector->end != vector->capacity) {
        if (vector->end)
            *reinterpret_cast<uint32_t*>(vector->end) = value;
        vector->end += 4;
        return;
    }
    uint32_t allocator = 0;
    InsertN(vector, vector->end, &value, &allocator, 1u, 1u);
}

// A new empty list of the node allocator 0x00B207E4 (fetched as the game's
// inline code does).
Node* NewScratchList()
{
    Node* const head = Virtual<Node*>(ListNodeAllocator(), 0xC, 0xCu);
    head->next = head;
    head->prev = head;
    return head;
}

// The position the game presets before it asks an occupant for its own:
// the zero float 0x00B4A5B8 three times.
Position NoPosition()
{
    Position p;
    const uint32_t bits = Global<uint32_t>(addr::cSC4PropDeveloper_ZeroB4A5B8);
    std::memcpy(&p.x, &bits, 4);
    std::memcpy(&p.y, &bits, 4);
    std::memcpy(&p.z, &bits, 4);
    return p;
}

// The node after `node` of a hash table (buckets +4/+8), as the game's
// inline iteration: the chain, then the next non-empty bucket.
TableNode* NextTableNode(char* table, TableNode* node)
{
    if (node->next)
        return node->next;
    uint32_t bucket = node->key % static_cast<uint32_t>(Count4(At<char*>(table, 0x4), At<char*>(table, 0x8)));
    while (++bucket < static_cast<uint32_t>(Count4(At<char*>(table, 0x4), At<char*>(table, 0x8))))
        if (TableNode* const next = At<TableNode**>(table, 0x4)[bucket])
            return next;
    return nullptr;
}

// --- The handlers DoMessage calls ------------------------------------------------

// 0x006F0EC0 (message 0x86AD10EE, data 2 / 1000): above 200 ms since the
// last step, the next +0xA34 timed props are switched: on when a change to
// night runs (+0xA30 = 1) and the light service says so at their cell, else
// off. The state returns to 0 with the last step.
void __fastcall StepTimedProps(char* self, void*, uint32_t milliseconds)
{
    static bool logged = false;
    FirstCall("StepTimedProps", logged);
    uint32_t& elapsed = At<uint32_t>(self, field::kTimedMs);
    elapsed += milliseconds;
    if (elapsed <= 200)
        return;
    elapsed = 0;
    const uint8_t on = At<uint32_t>(self, field::kTimedState) == 1;
    // The city's cell sizes (slots 91, 92), truncated to integers.
    const int32_t sizeX = Ftol(Virtual<double>(Global<void*>(addr::SC4_City), 0x16C));
    const int32_t sizeZ = Ftol(Virtual<double>(Global<void*>(addr::SC4_City), 0x170));
    Vector* const timed = Timed(self);
    uint32_t& index = At<uint32_t>(self, field::kTimedIndex);
    int32_t last = static_cast<int32_t>(At<uint32_t>(self, field::kTimedStep) + index);
    const int32_t count = Count4(timed->begin, timed->end);
    if (last > count) {
        last = count;
        At<uint32_t>(self, field::kTimedState) = 0;
    }
    if (index >= static_cast<uint32_t>(last))
        return;
    do {
        Position p = NoPosition();
        void* const prop = reinterpret_cast<void**>(timed->begin)[index];
        uint8_t flag = on;
        if (on == 1) {
            void* const occupant = Virtual<void*>(prop, 0xC);
            Virtual<void>(occupant, 0x20, &p);
            void* const light = At<void*>(self, field::kLight);
            // x87: the position (float) over the size made unsigned, _ftol.
            const int32_t z = Ftol(static_cast<double>(p.z) / static_cast<double>(static_cast<uint32_t>(sizeZ)));
            const int32_t x = Ftol(static_cast<double>(p.x) / static_cast<double>(static_cast<uint32_t>(sizeX)));
            if (!Virtual<uint8_t>(light, 0x94, x, z))
                flag = 0;
        }
        Virtual<void>(prop, 0x2C, static_cast<uint32_t>(flag));
        ++index;
    } while (index < static_cast<uint32_t>(last));
}

// 0x006F10E0 (messages 0x26D31EC1 and 0x44778234): every timed prop
// switched at once, as StepTimedProps does it (here with the reciprocal
// cell sizes kept as floats); null entries are erased on the way.
void __fastcall SetTimedProps(char* self, void*, uint8_t on)
{
    static bool logged = false;
    FirstCall("SetTimedProps", logged);
    const float perX = static_cast<float>(1.0 / Virtual<double>(Global<void*>(addr::SC4_City), 0x16C));
    const float perZ = static_cast<float>(1.0 / Virtual<double>(Global<void*>(addr::SC4_City), 0x170));
    Vector* const timed = Timed(self);
    char* at = timed->begin;
    while (at != timed->end) {
        Position p = NoPosition();
        void* const prop = *reinterpret_cast<void**>(at);
        if (!prop) {
            char* const next = at + 4;
            if (next != timed->end)
                std::memmove(at, next, static_cast<std::size_t>(timed->end - next));
            timed->end -= 4;
            continue;
        }
        uint8_t flag = on;
        if (on == 1) {
            void* const occupant = Virtual<void*>(prop, 0xC);
            Virtual<void>(occupant, 0x20, &p);
            void* const light = At<void*>(self, field::kLight);
            const int32_t z = Ftol(static_cast<double>(p.z) * static_cast<double>(perZ));
            const int32_t x = Ftol(static_cast<double>(p.x) * static_cast<double>(perX));
            if (!Virtual<uint8_t>(light, 0x94, x, z))
                flag = 0;
        }
        Virtual<void>(prop, 0x2C, static_cast<uint32_t>(flag));
        at += 4;
    }
    At<uint32_t>(self, field::kTimedIndex) = 0;
    At<uint32_t>(self, field::kTimedState) = 0;
}

// 0x006F1270 (messages 0x278E6003 and 0x678E6E3A; the argument is not
// read): the signs standing on terrain are put back on it: the higher of
// the terrain's slots 30 and 13 at their place, when they are more than
// 0.01 off; the network service is told of each one moved.
void __fastcall SettleSigns(char* self, void*, void*)
{
    static bool logged = false;
    FirstCall("SettleSigns", logged);
    void* const network = Global<void*>(addr::SC4_NetworkService);
    void* const terrain = Global<void*>(addr::SC4_Terrain);
    Node* const head = List(self, field::kSigns);
    for (Node* node = head->next; node != head; node = node->next) {
        void* const sign = node->value;
        if (Virtual<uint32_t>(sign, 0x1C) != kSignOnTerrain)
            continue;
        void* const occupant = Virtual<void*>(sign, 0xC);
        Position p{};
        Virtual<void>(occupant, 0x20, &p);
        // x87: the first height stored as a float, the second compared
        // with it unrounded and stored as a float when it is used.
        const float first = static_cast<float>(Virtual<double>(terrain, 0x78, p.x, p.z));
        const double second = Virtual<double>(terrain, 0x34, p.x, p.z);
        const float height = second < static_cast<double>(first) ? first : static_cast<float>(second);
        const float off =
            std::fabs(static_cast<float>(static_cast<double>(p.y) - static_cast<double>(height)));
        if (!(off > 0.01f))
            continue;
        p.y = height;
        Virtual<void>(occupant, 0x24, &p);
        Virtual<void>(network, 0x60, occupant, 1u);
    }
}

// 0x006F1360 (message 0x99EF1143 with data 3 = 0xC772BF98): the signs on
// networks standing in the removed occupant's cell are handed to the prop
// service's slot 29.
void __fastcall OnNetworkOccupantRemoved(char* self, void*, void* removed)
{
    static bool logged = false;
    FirstCall("OnNetworkOccupantRemoved", logged);
    void* const city = Global<void*>(addr::SC4_City);
    // The game's buffer for the occupant's slot 11 is 16 bytes of stack;
    // the first two words are the cell.
    int32_t cell[4] = {};
    Virtual<void>(removed, 0x2C, cell);
    const int32_t x = cell[0];
    const int32_t z = cell[1];
    Node* const head = List(self, field::kSigns);
    for (Node* node = head->next; node != head; node = node->next) {
        void* const sign = node->value;
        if (Virtual<uint32_t>(sign, 0x1C) != kSignOnNetwork)
            continue;
        void* const occupant = Virtual<void*>(sign, 0xC);
        Position p{};
        Virtual<void>(occupant, 0x20, &p);
        // Stack slots the game does not set before the city fills them.
        int32_t signX = 0;
        int32_t signZ = 0;
        Virtual<void>(city, 0x17C, p.x, p.z, &signX, &signZ);
        if (signX != x || signZ != z)
            continue;
        void* const props = Global<void*>(addr::SC4_Service43D10);
        Virtual<void>(props, 0x74, Virtual<void*>(sign, 0xC));
    }
}

// 0x006F1620: one optional prop for an item. The candidates of the item's
// slot (a prop, or a family whose members are listed) that the requester's
// type chooses; one of them by the requester (its slot 6) or by a draw of
// the generator +0x38; placed on the lot at the cell, else on the network
// lot there, and tagged with the requester's index. True when placed.
uint8_t __fastcall PlaceProp(char* self, void*, uint8_t* item, uint32_t requesterIndex)
{
    static bool logged = false;
    FirstCall("PlaceProp", logged);
    char* const slots = At<char*>(At<char*>(item, 0x4), 0x0);
    auto* const candidates = reinterpret_cast<Vector*>(slots + At<uint32_t>(item, 0x8) * 0x34 + 0x28);
    if (candidates->begin == candidates->end)
        return 0;
    const uint32_t type = Virtual<uint32_t>(reinterpret_cast<void**>(Requesters(self)->begin)[requesterIndex], 0x10);
    const int32_t count = Count4(candidates->begin, candidates->end);
    Vector chosen{};
    for (int32_t i = 0; i < count; ++i) {
        const auto Candidate = [&] { return reinterpret_cast<uint32_t*>(candidates->begin)[i]; };
        if (Virtual<uint8_t>(At<void*>(self, field::kProps), 0x20, Candidate())) {
            char* const record = Virtual<char*>(At<void*>(self, field::kProps), 0x34, Candidate());
            if (At<uint32_t>(record, 0x10) == type)
                PushBack(&chosen, Candidate());
            continue;
        }
        Vector family{};
        if (Virtual<uint32_t>(At<void*>(self, field::kProps), 0x24, &family, Candidate())) {
            const int32_t members = Count4(family.begin, family.end);
            for (int32_t j = 0; j < members; ++j) {
                char* const record =
                    Virtual<char*>(At<void*>(self, field::kProps), 0x34, reinterpret_cast<uint32_t*>(family.begin)[j]);
                if (At<uint32_t>(record, 0x10) == type)
                    PushBack(&chosen, reinterpret_cast<uint32_t*>(family.begin)[j]);
            }
        }
        if (family.begin)
            Free(family.begin);
    }
    if ((static_cast<uint32_t>(chosen.end - chosen.begin) & 0xFFFFFFFCu) == 0) {
        if (chosen.begin)
            Free(chosen.begin);
        return 0;
    }
    uint32_t prop;
    if (Virtual<uint8_t>(reinterpret_cast<void**>(Requesters(self)->begin)[requesterIndex], 0x14)) {
        prop = Virtual<uint32_t>(reinterpret_cast<void**>(Requesters(self)->begin)[requesterIndex], 0x18, &chosen,
                                 static_cast<uint32_t>(item[0]), static_cast<uint32_t>(item[1]));
    } else {
        const uint32_t pick =
            Below(self + field::kRandom, static_cast<uint32_t>(Count4(chosen.begin, chosen.end)));
        prop = reinterpret_cast<uint32_t*>(chosen.begin)[pick];
    }
    if (!prop) {
        if (chosen.begin)
            Free(chosen.begin);
        return 0;
    }
    void* placed;
    if (void* const lot = Virtual<void*>(At<void*>(self, field::kLots), 0x18, static_cast<uint32_t>(item[0]),
                                         static_cast<uint32_t>(item[1]), 0u)) {
        void* const builder = Virtual<void*>(Global<void*>(addr::SC4_Service43D68), 0x90);
        placed = Virtual<void*>(builder, 0xFC, lot, At<uint32_t>(item, 0x8), prop);
    } else if (void* const networkLot =
                   Virtual<void*>(At<void*>(self, field::kNetworkLots), 0x14, static_cast<uint32_t>(item[0]),
                                  static_cast<uint32_t>(item[1]))) {
        placed = Virtual<void*>(At<void*>(self, field::kNetworkLots), 0x20, networkLot, At<uint32_t>(item, 0x8), prop);
    } else {
        // A game bug, copied: with neither a lot nor a network lot at the
        // cell the game goes on with its item argument (0x006F18C2) as the
        // placed prop and calls through the item's first word.
        placed = item;
    }
    if (placed) {
        Virtual<uint32_t>(placed, 0x4);
        SetUint32(Virtual<void*>(placed, 0x18), 0x89A1C16C, requesterIndex);
        Virtual<uint32_t>(placed, 0x8);
    }
    if (chosen.begin)
        Free(chosen.begin);
    return placed ? 1 : 0;
}

// 0x006F2330 (message 0xE8BDA2E7): the lot's props whose slot of the lot
// configuration has bit 0 of its byte +1 are handed to the prop service's
// slot 29.
void __fastcall UpdateLotOptionalProps(char* self, void*, void* lot)
{
    static bool logged = false;
    FirstCall("UpdateLotOptionalProps", logged);
    if (!lot)
        return;
    void* const configuration = Virtual<void*>(lot, 0x24);
    if (!configuration)
        return;
    Vector* const slots = Virtual<Vector*>(configuration, 0x5C);
    if (!slots)
        return;
    const uint32_t count = static_cast<uint32_t>(static_cast<int32_t>(slots->end - slots->begin) / 0x34);
    Node* list = NewScratchList();
    Virtual<void>(At<void*>(self, field::kProps), 0x60, &list, lot);
    Node* const head = list;
    for (Node* node = head->next; node != head; node = node->next) {
        void* const prop = node->value;
        // A stack slot of the game that holds leftovers when the prop does
        // not fill it; only read when the prop says it did.
        uint32_t id = 0;
        if (Virtual<uint8_t>(prop, 0x6C, &id) && count > 0) {
            char* const first = slots->begin;
            uint32_t i = 0;
            while (i < count && At<uint32_t>(first, i * 0x34 + 0x24) != id)
                ++i;
            if (i < count && (At<uint8_t>(first, i * 0x34 + 0x1) & 1))
                Virtual<void>(At<void*>(self, field::kProps), 0x74, prop);
        }
        Virtual<uint32_t>(prop, 0x8);
    }
    ListDestroy2(&list);
}

// 0x006F2480 (message 0x8A07AE00; rect = cells x0, z0, x1, z1): the timed
// props in the rectangle are switched to what the light service says at
// their cell (on only at night, +0xA3C); the props of the list +0xA64 in
// the rectangle are told the same through their slot 21.
void __fastcall UpdatePropsInRect(char* self, void*, int32_t* rect)
{
    static bool logged = false;
    FirstCall("UpdatePropsInRect", logged);
    const int32_t sizeX = Ftol(Virtual<double>(Global<void*>(addr::SC4_City), 0x16C));
    const int32_t sizeZ = Ftol(Virtual<double>(Global<void*>(addr::SC4_City), 0x170));
    Node* list = NewScratchList();
    if (Virtual<uint8_t>(At<void*>(self, field::kProps), 0x78, &list, rect)) {
        for (Node* node = list->next; node != list; node = node->next) {
            void* const prop = node->value;
            uint8_t timed = 0;
            void* state = nullptr;
            if (GetUint8(Virtual<void*>(prop, 0x18), 0x49C9C93C, &timed) && timed == 1 &&
                Virtual<uint8_t>(prop, 0x0, kIidTimedProp, &state)) {
                Position p = NoPosition();
                Virtual<void>(prop, 0x20, &p);
                void* const light = At<void*>(self, field::kLight);
                const int32_t z = Ftol(static_cast<double>(p.z) / static_cast<double>(static_cast<uint32_t>(sizeZ)));
                const int32_t x = Ftol(static_cast<double>(p.x) / static_cast<double>(static_cast<uint32_t>(sizeX)));
                if (!Virtual<uint8_t>(light, 0x94, x, z)) {
                    if (Virtual<uint32_t>(state, 0x30) == 1)
                        Virtual<void>(state, 0x2C, 0u);
                } else if (At<uint8_t>(self, field::kNight) && Virtual<uint32_t>(state, 0x30) == 0) {
                    Virtual<void>(state, 0x2C, 1u);
                }
                Virtual<uint32_t>(state, 0x8);
            }
            Virtual<uint32_t>(prop, 0x8);
        }
    }
    for (Node* node = List(self, field::kList64)->next; node != List(self, field::kList64);) {
        void* const prop = node->value;
        if (!prop) {
            // The node's value replaced by the last node's, the last node
            // dropped; the same node is looked at again.
            Node* const head = List(self, field::kList64);
            if (node->next != head)
                node->value = head->prev->value;
            Node* const last = List(self, field::kList64)->prev;
            last->prev->next = last->next;
            last->next->prev = last->prev;
            Free(last);
            continue;
        }
        Position p = NoPosition();
        Virtual<void>(Virtual<void*>(prop, 0xC), 0x20, &p);
        // The float 0.0625 at 0x00AB298C: a sixteenth of the position.
        const int32_t x = Ftol(static_cast<double>(p.x) * 0.0625);
        const int32_t z = Ftol(static_cast<double>(p.z) * 0.0625);
        if (x >= rect[0] && x <= rect[2] && z >= rect[1] && z <= rect[3]) {
            const uint8_t lit = Virtual<uint8_t>(At<void*>(self, field::kLight), 0x94, x, z);
            // The game passes the dword its cell size x was in with the
            // low byte replaced by the answer.
            Virtual<void>(prop, 0x54, 8u, (static_cast<uint32_t>(sizeX) & 0xFFFFFF00u) | lit);
        }
        node = node->next;
    }
    ListClear2(&list);
    Virtual<void>(ListNodeAllocator(), 0x10, list);
}

// 0x006F2790 (message 0xCA5E5027): the lot's props that have property
// 0x6A5E0727 are switched on when its value differs from the lot's slot
// 33, else off.
void __fastcall UpdateLotConditionProps(char* self, void*, void* lot)
{
    static bool logged = false;
    FirstCall("UpdateLotConditionProps", logged);
    const uint8_t condition = Virtual<uint8_t>(lot, 0x84);
    Node* list = NewScratchList();
    Virtual<void>(At<void*>(self, field::kProps), 0x60, &list, lot);
    Node* const head = list;
    for (Node* node = head->next; node != head; node = node->next) {
        void* const prop = node->value;
        // The byte lands in the low byte of the game's argument slot.
        uint8_t wanted = 0;
        void* state = nullptr;
        if (GetUint8(Virtual<void*>(prop, 0x18), 0x6A5E0727, &wanted) &&
            Virtual<uint8_t>(prop, 0x0, kIidTimedProp, &state)) {
            Virtual<void>(state, 0x2C, static_cast<uint32_t>(wanted != condition));
            Virtual<uint32_t>(state, 0x8);
        }
        Virtual<uint32_t>(prop, 0x8);
    }
    ListClear2(&list);
    Virtual<void>(ListNodeAllocator(), 0x10, list);
}

// The prop taken out of one of the lists +0xA5C, +0xA60, +0xA64: its
// node's value replaced by the last node's, the last node dropped, the
// prop released.
void RemoveFromList(char* self, std::size_t offset, void* prop)
{
    Node* node = List(self, offset)->next;
    while (node != List(self, offset) && node->value != prop)
        node = node->next;
    Node* const head = List(self, offset);
    if (node == head)
        return;
    if (node->next != head)
        node->value = head->prev->value;
    Node* const last = List(self, offset)->prev;
    last->prev->next = last->next;
    last->next->prev = last->prev;
    Free(last);
    Virtual<uint32_t>(prop, 0x8);
}

// 0x006F2FF0 (message 0x99EF1143 with data 3 = 0xA823821E): a prop that
// leaves the city is forgotten everywhere, unless it has property
// 0x029244DB.
void __fastcall OnPropRemoved(char* self, void*, void* occupant)
{
    static bool logged = false;
    FirstCall("OnPropRemoved", logged);
    if (Virtual<uint8_t>(Virtual<void*>(occupant, 0x18), 0xC, 0x029244DBu))
        return;
    uint8_t timed = 0;
    void* state = nullptr;
    if (GetUint8(Virtual<void*>(occupant, 0x18), 0x49C9C93C, &timed) && timed == 1 &&
        Virtual<uint8_t>(occupant, 0x0, kIidTimedProp, &state)) {
        Vector* const all = Timed(self);
        char* at = all->begin;
        while (at != all->end && *reinterpret_cast<void**>(at) != state)
            at += 4;
        if (at != all->end) {
            char* const next = at + 4;
            if (all->end != next)
                std::memmove(at, next, static_cast<std::size_t>(all->end - next));
            all->end -= 4;
            Virtual<uint32_t>(state, 0x8);
        }
        Virtual<uint32_t>(state, 0x8);
    }
    void* prop = nullptr;
    if (!Virtual<uint8_t>(occupant, 0x0, kIidTimedProp, &prop))
        return;
    // The key's stack slot is the one the first interface pointer was in;
    // it is only read when the property was found.
    uint32_t key = 0;
    if (GetUint32(Virtual<void*>(occupant, 0x18), 0x6A3FCC37, &key) && key != 0) {
        void* shown = nullptr;
        void* const service = Global<void*>(addr::SC4_Service43DC8);
        if (service && Virtual<uint8_t>(service, 0x0, 0xFA47B3F9u, &shown)) {
            char* const table = self + field::kTable;
            TableIterator found{};
            reinterpret_cast<FindFn>(addr::SC4Uint32HashTable_Find)(table, &found, &key);
            if (TableNode* const entry = found.node) {
                Node** const list = &entry->list;
                Node* at = nullptr;
                reinterpret_cast<ListFindFn>(addr::SC4List_Find)(&at, (*list)->next, *list, &prop);
                if (at != *list) {
                    Node* next = nullptr;
                    reinterpret_cast<ListEraseFn>(addr::SC4StdList_Erase)(list, &next, at);
                    Virtual<uint32_t>(prop, 0x8);
                    // 0x006F2A60: the entry erased once its list is empty.
                    if ((*list)->next == *list) {
                        const TableIterator where{entry, found.table};
                        reinterpret_cast<TableEraseFn>(addr::SC4KeyListTable_Erase)(table, &where);
                    }
                }
            }
            Virtual<uint32_t>(shown, 0x8);
        }
    }
    void* const holder = Virtual<void*>(occupant, 0x18);
    if (Virtual<uint8_t>(holder, 0xC, 0x4A149631u))
        RemoveFromList(self, field::kList60, prop);
    if (Virtual<uint8_t>(holder, 0xC, 0x0A751675u))
        RemoveFromList(self, field::kList5C, prop);
    if (Virtual<uint8_t>(holder, 0xC, 0x6A959E71u))
        RemoveFromList(self, field::kList64, prop);
    const uint32_t kind = Virtual<uint32_t>(prop, 0x1C);
    if (kind == kSignOnTerrain || kind == kSignOnNetwork)
        reinterpret_cast<ListRemoveFn>(addr::SC4ListIterator_FreeList)(self + field::kSigns, &prop);
    Virtual<uint32_t>(prop, 0x8);
}

// --- Slots of the primary interface ---------------------------------------------

// Slot 5 (0x006F21C0).
void __fastcall AddPropRequester(char* self, void*, void* requester)
{
    static bool logged = false;
    FirstCall("AddPropRequester", logged);
    Virtual<uint32_t>(requester, 0x4);
    PushBack(Requesters(self), static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(requester)));
}

// Slot 6 (0x006F1080): the first match erased and released.
void __fastcall RemovePropRequester(char* self, void*, void* requester)
{
    static bool logged = false;
    FirstCall("RemovePropRequester", logged);
    Vector* const all = Requesters(self);
    for (char* at = all->begin; at != all->end; at += 4) {
        if (*reinterpret_cast<void**>(at) != requester)
            continue;
        char* const next = at + 4;
        if (all->end != next)
            std::memmove(at, next, static_cast<std::size_t>(all->end - next));
        all->end -= 4;
        Virtual<uint32_t>(requester, 0x8);
        return;
    }
}

// Slot 8 (0x006F1210).
void __fastcall ShowSignsAndLabels(char* self, void*, uint8_t show)
{
    static bool logged = false;
    FirstCall("ShowSignsAndLabels", logged);
    if (show == At<uint8_t>(self, field::kSignsVisible))
        return;
    At<uint8_t>(self, field::kSignsVisible) = show;
    Node* const head = List(self, field::kSigns);
    for (Node* node = head->next; node != head; node = node->next)
        Virtual<void>(Virtual<void*>(node->value, 0xC), show ? 0x74 : 0x7C, 2u);
}

// Slot 9 (0x006F2BF0): slot 8 with the opposite.
void __fastcall ToggleSignsAndLabels(char* self, void*)
{
    static bool logged = false;
    FirstCall("ToggleSignsAndLabels", logged);
    Virtual<void>(self, 0x20, static_cast<uint32_t>(At<uint8_t>(self, field::kSignsVisible) == 0));
}

// Slot 10 (0x006F2C10).
uint8_t __fastcall AreSignsAndLabelsVisible(char* self, void*)
{
    static bool logged = false;
    FirstCall("AreSignsAndLabelsVisible", logged);
    return At<uint8_t>(self, field::kSignsVisible);
}

// A service into `offset` when there is none: stored and referenced (the
// game does not test it).
void Take(char* self, std::size_t offset, void* service)
{
    At<void*>(self, offset) = service;
    Virtual<uint32_t>(service, 0x4);
}

// The cell iterator's rectangle set and its walk restarted, as the game's
// inline code in Init and the constructor.
void ResetIterator(char* self, int32_t x0, int32_t z0, int32_t x1, int32_t z1, bool clamp)
{
    At<int32_t>(self, 0x28) = x0;
    At<int32_t>(self, 0x2C) = z0;
    At<int32_t>(self, 0x30) = x1;
    At<int32_t>(self, 0x34) = z1;
    if (clamp) {
        if (At<int32_t>(self, 0x30) < At<int32_t>(self, 0x28))
            At<int32_t>(self, 0x30) = At<int32_t>(self, 0x28);
        if (At<int32_t>(self, 0x34) < At<int32_t>(self, 0x2C))
            At<int32_t>(self, 0x34) = At<int32_t>(self, 0x2C);
    }
    At<int32_t>(self, 0x1C) = 0;
    At<int32_t>(self, 0x20) = 0;
    At<uint8_t>(self, 0x18) = 0;
    if (At<int32_t>(self, 0x28) > 0 || At<int32_t>(self, 0x2C) > 0 || At<int32_t>(self, 0x30) < 0 ||
        At<int32_t>(self, 0x34) < 0)
        At<uint8_t>(self, 0x18) = 2;
}

// Slot 3 (0x006F1F30): Init. Always true.
uint8_t __fastcall Init(char* self, void*)
{
    static bool logged = false;
    FirstCall("Init", logged);
    if (At<uint8_t>(self, field::kInitialized))
        return 1;
    At<uint8_t>(self, field::kInitialized) = 1;
    if (!At<void*>(self, field::kLots))
        Take(self, field::kLots, Global<void*>(addr::SC4_LotManager));
    if (!At<void*>(self, field::kProps))
        Take(self, field::kProps, Global<void*>(addr::SC4_Service43D10));
    if (!At<void*>(self, field::kNetworkLots))
        Take(self, field::kNetworkLots, Virtual<void*>(Global<void*>(addr::SC4_City), 0xA8));
    if (!At<void*>(self, field::kLight))
        Take(self, field::kLight, Global<void*>(addr::SC4_Service43DA0));
    if (!At<uint8_t>(self, field::kRequesterAdded)) {
        // A new wealth requester (its constructor inline, as in the
        // factory 0x006F1500), held only by the vector after this.
        char* const requester = static_cast<char*>(OperatorNew(0xC));
        if (requester) {
            At<std::uintptr_t>(requester, 0x0) = addr::cSC4WealthRequester_vftable;
            At<std::uintptr_t>(requester, 0x4) = addr::cSC4WealthRequester_vftable_4;
            At<uint32_t>(requester, 0x8) = 0;
        }
        Virtual<uint32_t>(requester, 0x4);
        Virtual<void>(self, 0x14, static_cast<void*>(requester));
        Virtual<uint32_t>(requester, 0x8);
        At<uint8_t>(self, field::kRequesterAdded) = 1;
    }
    if (!At<uint8_t>(self, field::kOptionalState)) {
        At<uint8_t>(self, field::kOptionalState) = 1;
        // The city's cell counts (slots 94, 93) in tracts of 8 cells.
        void* const city = Global<void*>(addr::SC4_City);
        const int32_t lastZ = static_cast<int32_t>((Virtual<uint32_t>(city, 0x178) >> 3) - 1);
        const int32_t lastX = static_cast<int32_t>((Virtual<uint32_t>(city, 0x174) >> 3) - 1);
        ResetIterator(self, 0, 0, lastX, lastZ, true);
    }
    void* const simulators = Global<void*>(addr::SC4_SimulatorService);
    GameString name;
    reinterpret_cast<TextFn>(addr::SC4String_FromText)(&name,
                                                       reinterpret_cast<const char*>(addr::cSC4PropDeveloper_Name));
    void* const target = self + 0x4;
    if (simulators) {
        Virtual<uint32_t>(simulators, 0x4C, target, 10u, &name, 2u);
        Virtual<uint32_t>(simulators, 0x4C, target, 4u, &name, 2u);
    }
    if (void* const server = Global<void*>(addr::SC4_MessageServer))
        for (const uint32_t message : kMessages)
            Virtual<uint8_t>(server, 0x14, target, message);
    Virtual<uint8_t>(reinterpret_cast<ServiceFn>(addr::SC4_GetService86AD10EC)(), 0x14, target, 0xC0u);
    reinterpret_cast<SelfFn>(addr::SC4String_Destroy)(&name);
    return 1;
}

// Every non-null value of a list released (the nodes stay).
void ReleaseList(char* self, std::size_t offset)
{
    for (Node* node = List(self, offset)->next; node != List(self, offset); node = node->next)
        if (node->value)
            Virtual<uint32_t>(node->value, 0x8);
}

// A list's nodes freed, the sentinel linked to itself.
void ClearList(char* self, std::size_t offset)
{
    for (Node* node = List(self, offset)->next; node != List(self, offset);) {
        Node* const next = node->next;
        Free(node);
        node = next;
    }
    List(self, offset)->next = List(self, offset);
    List(self, offset)->prev = List(self, offset);
}

void ReleaseField(char* self, std::size_t offset)
{
    if (void* const object = At<void*>(self, offset)) {
        Virtual<uint32_t>(object, 0x8);
        At<void*>(self, offset) = nullptr;
    }
}

// Slot 4 (0x006F2C60): Shutdown. Always true. The list +0xA64 keeps its
// nodes (their props released) and the signs +0xA6C are not touched, as in
// the game.
uint8_t __fastcall Shutdown(char* self, void*)
{
    static bool logged = false;
    FirstCall("Shutdown", logged);
    if (!At<uint8_t>(self, field::kInitialized))
        return 1;
    At<uint8_t>(self, field::kInitialized) = 0;
    char* const table = self + field::kTable;
    TableIterator at{};
    reinterpret_cast<BeginFn>(addr::SC4HashTable_Begin)(table, &at);
    for (TableNode* node = at.node; node; node = NextTableNode(at.table, node))
        for (Node* item = node->list->next; item != node->list; item = item->next)
            if (item->value)
                Virtual<uint32_t>(item->value, 0x8);
    reinterpret_cast<SelfFn>(addr::SC4KeyListTable_Clear)(table);
    ReleaseList(self, field::kList60);
    ClearList(self, field::kList60);
    ReleaseList(self, field::kList5C);
    ClearList(self, field::kList5C);
    ReleaseList(self, field::kList64);
    Vector* const timed = Timed(self);
    for (char* p = timed->begin; p != timed->end; p += 4)
        if (void* const prop = *reinterpret_cast<void**>(p))
            Virtual<uint32_t>(prop, 0x8);
    timed->end = timed->begin;
    void* const target = self + 0x4;
    if (void* const simulators = Virtual<void*>(Global<void*>(addr::SC4_City), 0xC8))
        Virtual<uint32_t>(simulators, 0x50, target);
    if (void* const server = Global<void*>(addr::SC4_MessageServer))
        for (const uint32_t message : kMessages)
            Virtual<uint8_t>(server, 0x18, target, message);
    Virtual<uint8_t>(reinterpret_cast<ServiceFn>(addr::SC4_GetService86AD10EC)(), 0x18, target, 0xC0u);
    if (At<uint8_t>(self, field::kOptionalState))
        At<uint8_t>(self, field::kOptionalState) = 0;
    ReleaseField(self, field::kLots);
    ReleaseField(self, field::kProps);
    ReleaseField(self, field::kNetworkLots);
    ReleaseField(self, field::kLight);
    if (At<uint8_t>(self, field::kRequesterAdded)) {
        Vector* const all = Requesters(self);
        const int32_t count = Count4(all->begin, all->end);
        for (int32_t i = 0; i < count; ++i)
            Virtual<uint32_t>(reinterpret_cast<void**>(all->begin)[i], 0x8);
        all->end = all->begin;
        At<uint8_t>(self, field::kRequesterAdded) = 0;
    }
    return 1;
}

// --- Save --------------------------------------------------------------------------

// A list as the game writes one: the count of nodes, then a reference each.
void WriteList(void* stream, char* self, std::size_t offset)
{
    uint32_t count = 0;
    for (Node* node = List(self, offset)->next; node != List(self, offset); node = node->next)
        ++count;
    Virtual<uint8_t>(stream, 0x24, count);
    for (Node* node = List(self, offset)->next; node != List(self, offset); node = node->next)
        WriteRef(stream, node->value);
}

// cIGZSerializable slot 3 (0x006F1930), on the part at +0x8: version 5
// (tools/re/save_schemas/prop.py has the layout). 1 unless the stream is
// in error after.
uint32_t __fastcall Write(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    char* const self = part - 0x8;
    Virtual<uint8_t>(stream, 0x1C, static_cast<uint16_t>(5));
    Virtual<uint8_t>(stream, 0x10, static_cast<uint32_t>(At<uint8_t>(self, field::kOptionalState)));
    reinterpret_cast<IteratorFn>(addr::SC4Stream_WriteCellIterator)(stream, self + field::kIterator);
    Virtual<uint8_t>(stream, 0x14, static_cast<uint32_t>(At<uint8_t>(self, field::kRequesterAdded)));
    Vector* const requesters = Requesters(self);
    Virtual<uint8_t>(stream, 0x24, static_cast<uint32_t>(Count4(requesters->begin, requesters->end)));
    for (int32_t i = 0, n = Count4(requesters->begin, requesters->end); i < n; ++i)
        WriteRef(stream, reinterpret_cast<void**>(requesters->begin)[i]);
    WriteRef(stream, At<void*>(self, field::kLots));
    WriteRef(stream, At<void*>(self, field::kProps));
    WriteRef(stream, At<void*>(self, field::kNetworkLots));
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, field::kTimedIndex));
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, field::kTimedState));
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, field::kTimedStep));
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, field::kTimedMs));
    WriteBool(stream, At<uint8_t>(self, field::kNight));
    Vector* const timed = Timed(self);
    Virtual<uint8_t>(stream, 0x24, static_cast<uint32_t>(Count4(timed->begin, timed->end)));
    for (int32_t i = 0, n = Count4(timed->begin, timed->end); i < n; ++i)
        WriteRef(stream, reinterpret_cast<void**>(timed->begin)[i]);
    // The table: its count, then per entry the key and its list, in
    // bucket order.
    char* const table = self + field::kTable;
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(table, 0x10));
    TableNode* node = nullptr;
    for (uint32_t i = 0; i < static_cast<uint32_t>(Count4(At<char*>(table, 0x4), At<char*>(table, 0x8))); ++i)
        if (At<TableNode**>(table, 0x4)[i]) {
            node = At<TableNode**>(table, 0x4)[i];
            break;
        }
    for (; node; node = NextTableNode(table, node)) {
        Virtual<uint8_t>(stream, 0x24, node->key);
        uint32_t count = 0;
        for (Node* item = node->list->next; item != node->list; item = item->next)
            ++count;
        Virtual<uint8_t>(stream, 0x24, count);
        for (Node* item = node->list->next; item != node->list; item = item->next)
            WriteRef(stream, item->value);
    }
    WriteList(stream, self, field::kList60);
    WriteList(stream, self, field::kList5C);
    WriteList(stream, self, field::kList64);
    WriteBool(stream, At<uint8_t>(self, field::kSignsVisible));
    WriteList(stream, self, field::kSigns);
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// --- The wealth requester (cSC4WealthRequester, 12 bytes: the primary
// vtable 0x00AB29B0, the serializable part's 0x00AB2998 at +4, the count at
// +8) -------------------------------------------------------------------------------

// As the game's (0x006F0E30).
uint8_t __fastcall RequesterQueryInterface(char* self, void*, uint32_t iid, void** object)
{
    if (iid == kIidUnknown || iid == kIidRequester)
        *object = self;
    else if (iid == kIidSerializable)
        *object = self ? self + 0x4 : nullptr;
    else
        return 0;
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// As the game's (0x006F1040): the 12 bytes freed at the last reference.
uint32_t __fastcall RequesterRelease(char* self, void*)
{
    uint32_t& refs = At<uint32_t>(self, 0x8);
    if (refs > 1)
        return --refs;
    At<std::uintptr_t>(self, 0x0) = addr::cSC4WealthRequester_vftable;
    At<std::uintptr_t>(self, 0x4) = addr::cSC4WealthRequester_vftable_4;
    OperatorDelete(self);
    return 0;
}

uint8_t __fastcall RequesterQueryInterfaceAt4(char* part, void*, uint32_t iid, void** object)
{
    return RequesterQueryInterface(part - 0x4, nullptr, iid, object);
}

uint32_t __fastcall RequesterReleaseAt4(char* part, void*) { return RequesterRelease(part - 0x4, nullptr); }

// Slot 3 (0x006F0E10): 10 whatever the argument.
uint32_t __fastcall RequesterSlot3(char*, void*, uint32_t)
{
    static bool logged = false;
    FirstCall("WealthRequester slot 3", logged);
    return 10;
}

// Slot 4 (0x006F0E20).
uint32_t __fastcall RequesterGetType(char*, void*)
{
    static bool logged = false;
    FirstCall("WealthRequester GetType", logged);
    return kRequesterType;
}

// +4 slot 5 (0x006F0E00).
uint32_t __fastcall RequesterGetGZCLSID(void*, void*) { return kRequesterClassId; }

// Slot 6 (0x006F1CB0): a prop for the wealth at a cell. The wealth is the
// lot's (its slot 41; 2 on zone types 11, 12 and 15), without a lot the
// network lot manager's (slot 10); of the candidates whose record has that
// wealth one is drawn from the static generator 0x00B4A5C0. 0 when none
// fits (no draw then).
uint32_t __fastcall RequesterChooseProp(char*, void*, Vector* candidates, int32_t x, int32_t z)
{
    static bool logged = false;
    FirstCall("WealthRequester ChooseProp", logged);
    uint8_t wealth;
    if (void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x18, x, z, 0u)) {
        wealth = Virtual<uint8_t>(lot, 0xA4);
        const int32_t zone = Virtual<int32_t>(Global<void*>(addr::SC4_ZoneManager), 0x30, x, z);
        if (zone >= 0xB && (zone <= 0xC || zone == 0xF))
            wealth = 2;
    } else {
        void* const networkLots = Virtual<void*>(Global<void*>(addr::SC4_City), 0xA8);
        wealth = Virtual<uint8_t>(networkLots, 0x28, x, z);
    }
    void* const props = Global<void*>(addr::SC4_Service43D10);
    Vector matching{};
    const uint32_t offered = static_cast<uint32_t>(Count4(candidates->begin, candidates->end));
    reinterpret_cast<CountFn>(addr::SC4PtrVector_Reserve)(&matching, offered);
    uint32_t count = 0;
    const char* const end = candidates->end;
    for (const char* at = candidates->begin; at != end; at += 4) {
        const uint32_t prop = *reinterpret_cast<const uint32_t*>(at);
        char* const record = Virtual<char*>(props, 0x34, prop);
        if (At<uint8_t>(record, 0x14) != wealth)
            continue;
        PushBack(&matching, prop);
        ++count;
    }
    uint32_t chosen = 0;
    if (count) {
        const uint32_t pick = Below(reinterpret_cast<void*>(addr::cSC4WealthRequester_Random), count);
        chosen = reinterpret_cast<uint32_t*>(matching.begin)[pick];
    }
    if (matching.begin)
        Free(matching.begin);
    return chosen;
}

// The factory (0x006F1500, registered for class 0x89C49986).
void* __cdecl CreateRequester()
{
    static bool logged = false;
    FirstCall("WealthRequester Create", logged);
    char* const requester = static_cast<char*>(OperatorNew(0xC));
    if (!requester)
        return nullptr;
    At<std::uintptr_t>(requester, 0x0) = addr::cSC4WealthRequester_vftable;
    At<std::uintptr_t>(requester, 0x4) = addr::cSC4WealthRequester_vftable_4;
    At<uint32_t>(requester, 0x8) = 0;
    return requester;
}

// --- Container helpers only this class uses ---------------------------------------

// 0x006F15C0: copies of the items of [first, last) linked in before `where`.
void ItemListInsertRange(ItemNode* where, ItemNode* first, ItemNode* last)
{
    for (; first != last; first = first->next) {
        auto* const node = static_cast<ItemNode*>(Allocate(0x18));
        for (int i = 0; i < 4; ++i)
            node->value[i] = first->value[i];
        ItemNode* const prev = where->prev;
        node->next = where;
        node->prev = prev;
        prev->next = node;
        where->prev = node;
    }
}

// 0x006F1E10: list = other. Items copied over the nodes there are, then
// the rest of this list erased (the game's shared range erase) or the rest
// of the other inserted.
ItemNode** ItemListAssign(ItemNode** self, ItemNode* const* other)
{
    if (self == other)
        return self;
    ItemNode* const head = *self;
    ItemNode* const otherHead = *other;
    ItemNode* to = head->next;
    ItemNode* from = otherHead->next;
    while (to != head && from != otherHead) {
        for (int i = 0; i < 4; ++i)
            to->value[i] = from->value[i];
        to = to->next;
        from = from->next;
    }
    if (to == head && from != otherHead) {
        ItemListInsertRange(head, from, otherHead);
    } else {
        ItemNode* out = nullptr;
        reinterpret_cast<RangeEraseFn>(addr::SC4StdList_EraseRange)(self, &out, to, head);
    }
    return self;
}

// 0x006F1EF0: lists assigned from [first, last) to dest on; the end.
ItemNode** ItemListCopy(ItemNode** first, ItemNode** last, ItemNode** dest)
{
    for (std::ptrdiff_t n = last - first; n > 0; --n)
        ItemListAssign(dest++, first++);
    return dest;
}

// 0x006F2270.
void ItemListFill(ItemNode** first, ItemNode** last, ItemNode* const* value)
{
    for (; first != last; ++first)
        ItemListAssign(first, value);
}

// 0x006F22A0.
ItemNode** ItemListFillN(ItemNode** first, uint32_t count, ItemNode* const* value)
{
    for (; count; --count)
        ItemListAssign(first++, value);
    return first;
}

// 0x006F22D0: `count` new lists, each a copy of value.
ItemNode** ItemListUninitializedFillN(ItemNode** first, uint32_t count, ItemNode* const* value)
{
    for (; count; --count, ++first) {
        if (!first)
            continue;
        *first = nullptr;
        auto* const head = static_cast<ItemNode*>(Allocate(0x18));
        head->next = head;
        head->prev = head;
        *first = head;
        ItemListInsertRange(head->next, (*value)->next, *value);
    }
    return first;
}

// 0x006F2A20: erase(first, last) of a vector of lists.
ItemNode** ItemListVectorErase(Vector* vector, ItemNode** first, ItemNode** last)
{
    ItemNode** const newEnd = ItemListCopy(last, reinterpret_cast<ItemNode**>(vector->end), first);
    uint32_t dummy = 0;
    DestroyListRange(newEnd, vector->end, &dummy);
    vector->end = reinterpret_cast<char*>(newEnd);
    return first;
}

// 0x006F3330: assign(count, value) of a vector of lists of items.
void __fastcall ItemListVectorAssign(Vector* vector, void*, uint32_t count, ItemNode* const* value)
{
    static bool logged = false;
    FirstCall("ItemListVectorAssign", logged);
    uint32_t dummy = 0;
    if (count > static_cast<uint32_t>(Count4(vector->begin, vector->capacity))) {
        char* const storage = count ? static_cast<char*>(Allocate(count * 4)) : nullptr;
        ItemNode** const newEnd = ItemListUninitializedFillN(reinterpret_cast<ItemNode**>(storage), count, value);
        char* const old = vector->begin;
        char* const oldEnd = vector->end;
        vector->begin = storage;
        vector->end = reinterpret_cast<char*>(newEnd);
        vector->capacity = storage + count * 4;
        DestroyListRange(old, oldEnd, &dummy);
        if (old)
            Free(old);
        return;
    }
    const uint32_t size = static_cast<uint32_t>(Count4(vector->begin, vector->end));
    if (count > size) {
        ItemListFill(reinterpret_cast<ItemNode**>(vector->begin), reinterpret_cast<ItemNode**>(vector->end), value);
        const uint32_t more = count - static_cast<uint32_t>(Count4(vector->begin, vector->end));
        vector->end = reinterpret_cast<char*>(
            ItemListUninitializedFillN(reinterpret_cast<ItemNode**>(vector->end), more, value));
        return;
    }
    ItemNode** const last = reinterpret_cast<ItemNode**>(vector->end);
    ItemNode** const keep = ItemListFillN(reinterpret_cast<ItemNode**>(vector->begin), count, value);
    ItemListVectorErase(vector, keep, last);
}

// 0x006F1430: pop_back of a list of pointers.
void __fastcall ListPopBack(Node** list, void*)
{
    static bool logged = false;
    FirstCall("ListPopBack", logged);
    Node* const last = (*list)->prev;
    Node* const next = last->next;
    Node* const prev = last->prev;
    prev->next = next;
    next->prev = prev;
    Free(last);
}

// 0x006F1450: reserve() of a bit vector: new words, the bits copied (the
// game's shared bit copy), the old words freed.
void __fastcall BitVectorReserve(BitVector* vector, void*, uint32_t bits)
{
    static bool logged = false;
    FirstCall("BitVectorReserve", logged);
    const uint32_t capacity = static_cast<uint32_t>(vector->endOfStorage - vector->start) << 5;
    if (capacity - vector->startBit >= bits)
        return;
    const uint32_t words = (bits + 31) >> 5;
    uint32_t* const storage = words ? static_cast<uint32_t*>(Allocate(words * 4)) : nullptr;
    BitIterator end{};
    const BitIterator* const result = reinterpret_cast<BitCopyFn>(addr::SC4BitVector_Copy)(
        &end, vector->start, vector->startBit, vector->finish, vector->finishBit, storage, 0);
    vector->finish = result->word;
    vector->finishBit = result->bit;
    if (vector->start)
        Free(vector->start);
    vector->startBit = 0;
    vector->start = storage;
    vector->endOfStorage = storage + words;
}

// 0x006F2220: pair<key, list of pointers> from a key and a copy of a list.
uint32_t* __fastcall KeyListPairConstruct(uint32_t* pair, void*, const uint32_t* key, Node* const* list)
{
    static bool logged = false;
    FirstCall("KeyListPairConstruct", logged);
    pair[0] = *key;
    auto** const copy = reinterpret_cast<Node**>(pair + 1);
    *copy = nullptr;
    auto* const head = static_cast<Node*>(Allocate(0xC));
    head->next = head;
    head->prev = head;
    *copy = head;
    uint32_t dummy = 0;
    reinterpret_cast<RangeInsertFn>(addr::SC4StdList_InsertRangeUint32)(copy, head->next, (*list)->next, *list, &dummy);
    return pair;
}

// 0x006F35D0: insert_unique into the table: room for one more, then the
// game's shared insert (out: node, table, inserted; 9 bytes).
void* __fastcall KeyListTableInsert(char* table, void*, void* out, const void* value)
{
    static bool logged = false;
    FirstCall("KeyListTableInsert", logged);
    reinterpret_cast<CountFn>(addr::SC4Uint32HashSet_Resize)(table, At<uint32_t>(table, 0x10) + 1);
    reinterpret_cast<TableInsertFn>(addr::SC4KeyListTable_InsertUnique)(table, out, value);
    return out;
}

// --- Installation -------------------------------------------------------------------

struct Slot {
    std::uintptr_t vtable;
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(vtable, index, original, fn) {vtable, index, original, reinterpret_cast<void*>(&fn)}

const Slot kSimSlots[] = {
    OPENSC4_SLOT(addr::cSC4PropDeveloper_vftable, 3, addr::cSC4PropDeveloper_Init, Init),
    OPENSC4_SLOT(addr::cSC4PropDeveloper_vftable, 4, addr::cSC4PropDeveloper_Shutdown, Shutdown),
    OPENSC4_SLOT(addr::cSC4PropDeveloper_vftable, 5, addr::cSC4PropDeveloper_AddPropRequester, AddPropRequester),
    OPENSC4_SLOT(addr::cSC4PropDeveloper_vftable, 6, addr::cSC4PropDeveloper_RemovePropRequester,
                 RemovePropRequester),
    OPENSC4_SLOT(addr::cSC4PropDeveloper_vftable, 8, addr::cSC4PropDeveloper_ShowSignsAndLabels, ShowSignsAndLabels),
    OPENSC4_SLOT(addr::cSC4PropDeveloper_vftable, 9, addr::cSC4PropDeveloper_ToggleSignsAndLabels,
                 ToggleSignsAndLabels),
    OPENSC4_SLOT(addr::cSC4PropDeveloper_vftable, 10, addr::cSC4PropDeveloper_AreSignsAndLabelsVisible,
                 AreSignsAndLabelsVisible),
};
const Slot kSaveSlots[] = {
    OPENSC4_SLOT(addr::cSC4PropDeveloper_vftable_8, 3, addr::cSC4PropDeveloper_Write, Write)};
// The +4 part's thunks 0x006F0E90 and 0x006F1070 have no entries of their own.
const Slot kRequesterSlots[] = {
    OPENSC4_SLOT(addr::cSC4WealthRequester_vftable, 0, addr::cSC4WealthRequester_QueryInterface,
                 RequesterQueryInterface),
    OPENSC4_SLOT(addr::cSC4WealthRequester_vftable, 2, addr::cSC4WealthRequester_Release, RequesterRelease),
    OPENSC4_SLOT(addr::cSC4WealthRequester_vftable, 3, addr::cSC4WealthRequester_Slot3, RequesterSlot3),
    OPENSC4_SLOT(addr::cSC4WealthRequester_vftable, 4, addr::cSC4WealthRequester_GetType, RequesterGetType),
    OPENSC4_SLOT(addr::cSC4WealthRequester_vftable, 6, addr::cSC4WealthRequester_ChooseProp, RequesterChooseProp),
    OPENSC4_SLOT(addr::cSC4WealthRequester_vftable_4, 0, 0x006F0E90, RequesterQueryInterfaceAt4),
    OPENSC4_SLOT(addr::cSC4WealthRequester_vftable_4, 2, 0x006F1070, RequesterReleaseAt4),
    OPENSC4_SLOT(addr::cSC4WealthRequester_vftable_4, 5, addr::cSC4WealthRequester_GetGZCLSID, RequesterGetGZCLSID),
};

#undef OPENSC4_SLOT

template <std::size_t N>
bool SwapSlots(const Slot (&slots)[N], const char* what)
{
    if (!game::IsBuild641())
        return false;
    for (const Slot& slot : slots) {
        const auto* const vtable = reinterpret_cast<const std::uintptr_t*>(slot.vtable);
        if (vtable[slot.index] != slot.original) {
            log::Write("prop %s: slot %d of %08X holds %08X, expected %08X; not installing", what, slot.index,
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
    log::Write("prop %s: %u slots are ours", what, static_cast<unsigned>(N));
    return true;
}

struct Detour {
    std::uintptr_t address;
    void* ours;
};

// The originals are not called: each detour replaces its function whole.
template <std::size_t N>
bool InstallDetours(const Detour (&detours)[N], void* (&originals)[N], const char* what)
{
    if (!game::IsBuild641())
        return false;
    for (std::size_t i = 0; i < N; ++i) {
        const addr::Entry* const entry = addr_check::Find(detours[i].address);
        if (!entry || !hook::Install(*entry, detours[i].ours, &originals[i])) {
            log::Write("prop %s: no detour at %08X", what, static_cast<unsigned>(detours[i].address));
            return false;
        }
    }
    log::Write("prop %s: %u detours installed", what, static_cast<unsigned>(N));
    return true;
}

#define OPENSC4_DETOUR(name, fn) {addr::name, reinterpret_cast<void*>(&fn)}

bool InstallSim(registry::Mode) { return SwapSlots(kSimSlots, "sim"); }
bool InstallSave(registry::Mode) { return SwapSlots(kSaveSlots, "save"); }

// Checked at every game call site of the nine (all in DoMessage 0x006F4E90
// but PlaceProp's, 0x006F4C52 in 0x006F4080): each passes `this` in ECX and
// its arguments on the stack, and none reads EAX (but PlaceProp's AL), ECX
// or EDX after the call before loading it again.
bool InstallHandlers(registry::Mode)
{
    static const Detour detours[] = {
        OPENSC4_DETOUR(cSC4PropDeveloper_StepTimedProps, StepTimedProps),
        OPENSC4_DETOUR(cSC4PropDeveloper_SetTimedProps, SetTimedProps),
        OPENSC4_DETOUR(cSC4PropDeveloper_SettleSigns, SettleSigns),
        OPENSC4_DETOUR(cSC4PropDeveloper_OnNetworkOccupantRemoved, OnNetworkOccupantRemoved),
        OPENSC4_DETOUR(cSC4PropDeveloper_PlaceProp, PlaceProp),
        OPENSC4_DETOUR(cSC4PropDeveloper_UpdateLotOptionalProps, UpdateLotOptionalProps),
        OPENSC4_DETOUR(cSC4PropDeveloper_UpdatePropsInRect, UpdatePropsInRect),
        OPENSC4_DETOUR(cSC4PropDeveloper_UpdateLotConditionProps, UpdateLotConditionProps),
        OPENSC4_DETOUR(cSC4PropDeveloper_OnPropRemoved, OnPropRemoved),
    };
    static void* originals[sizeof(detours) / sizeof(detours[0])];
    return InstallDetours(detours, originals, "handlers");
}

// The factory is only called through the class registration (0x00460424).
bool InstallRequester(registry::Mode)
{
    static const Detour detours[] = {OPENSC4_DETOUR(cSC4WealthRequester_Create, CreateRequester)};
    static void* originals[1];
    return SwapSlots(kRequesterSlots, "requester") && InstallDetours(detours, originals, "requester");
}

// Checked at the call sites (0x006F5017 in DoMessage, 0x006F43D2 and
// 0x006F40D3 in 0x006F4080, 0x006F3D77 and 0x006F3D84 in 0x006F3B70): plain
// __thiscall, nothing kept in EAX, ECX or EDX across the call (the pair
// constructor's and the insert's results are their EAX).
bool InstallContainers(registry::Mode)
{
    static const Detour detours[] = {
        OPENSC4_DETOUR(cSC4PropDeveloper_ListPopBack, ListPopBack),
        OPENSC4_DETOUR(cSC4PropDeveloper_BitVectorReserve, BitVectorReserve),
        OPENSC4_DETOUR(cSC4PropDeveloper_KeyListPairConstruct, KeyListPairConstruct),
        OPENSC4_DETOUR(cSC4PropDeveloper_ItemListVectorAssign, ItemListVectorAssign),
        OPENSC4_DETOUR(cSC4PropDeveloper_KeyListTableInsert, KeyListTableInsert),
    };
    static void* originals[sizeof(detours) / sizeof(detours[0])];
    return InstallDetours(detours, originals, "containers");
}

#undef OPENSC4_DETOUR

const bool g_registered[] = {
    registry::Register({"simulators.prop.sim", "simulators/prop", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4PropDeveloper_vftable,
                        "Our prop developer Init, Shutdown, requesters and signs and labels slots "
                        "(src/core/prop_object.cpp)",
                        false, InstallSim}),
    registry::Register({"simulators.prop.save", "simulators/prop", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4PropDeveloper_vftable_8,
                        "Our prop developer Write: the save record, version 5 (src/core/prop_object.cpp)", false,
                        InstallSave}),
    registry::Register({"simulators.prop.handlers", "simulators/prop", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4PropDeveloper_StepTimedProps,
                        "Our prop developer message handlers and PlaceProp, by detours (the props part calls them "
                        "at the game's address; src/core/prop_object.cpp)",
                        false, InstallHandlers}),
    registry::Register({"simulators.prop.requester", "simulators/prop", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4WealthRequester_vftable,
                        "Our cSC4WealthRequester: its slots in the game's vtables and its factory "
                        "(src/core/prop_object.cpp)",
                        false, InstallRequester}),
    registry::Register({"simulators.prop.containers", "simulators/prop", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4PropDeveloper_ItemListVectorAssign,
                        "Our copies of five container helpers only the prop developer uses, by detours "
                        "(src/core/prop_object.cpp)",
                        false, InstallContainers}),
};

// --- Our object -------------------------------------------------------------------

// Copies of the game's 0x00AB29F8 (12 slots, deleting destructor last),
// 0x00AB29E8 (at +4) and 0x00AB29D0 (at +8), taken at the first
// construction, after every PostAppInit slot swap of the other groups and
// of the props part.
constexpr int kSlots = 12, kSlots4 = 4, kSlots8 = 6;
void* g_vtable[kSlots];
void* g_vtable4[kSlots4];
void* g_vtable8[kSlots8];

// As the game's (0x004F7BA0, shared with other classes).
uint32_t __fastcall AddRef(char* self, void*) { return ++At<uint32_t>(self, field::kRefCount); }

// As the game's (0x006F0D80).
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

// As the game's (0x006F0EA0): the deleting destructor (slot 11) at the
// last reference; Shutdown is the owner's to call.
uint32_t __fastcall ReleaseObject(char* self, void*)
{
    uint32_t& refs = At<uint32_t>(self, field::kRefCount);
    if (refs > 1)
        return --refs;
    Virtual<void*>(self, 0x2C, 1u);
    return 0;
}

// The destructor (0x006F2B60): what it frees; the references are
// Shutdown's.
void Destroy(char* self)
{
    At<void**>(self, 0x0) = g_vtable;
    At<void**>(self, 0x4) = g_vtable4;
    At<void**>(self, 0x8) = g_vtable8;
    for (const std::size_t offset : {field::kSigns, field::kList64, field::kList60, field::kList5C})
        PairListDestroy(self + offset);
    char* const table = self + field::kTable;
    reinterpret_cast<SelfFn>(addr::SC4KeyListTable_Clear)(table);
    if (void* const buckets = At<void*>(table, 0x4))
        Free(buckets);
    if (void* const timed = At<void*>(self, field::kTimed))
        Free(timed);
    if (void* const requesters = At<void*>(self, field::kRequesters))
        Free(requesters);
}

// Slot 11, as the game's (0x006F35B0).
void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    static int destroyed = 0;
    if (destroyed++ < 3)
        log::Write("prop object: destroying %p", static_cast<void*>(self));
    Destroy(self);
    if (flags & 1)
        OperatorDelete(self);
    return self;
}

// As the game's (0x006F0D70).
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

void BuildVtables()
{
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4PropDeveloper_vftable), sizeof(g_vtable));
    std::memcpy(g_vtable4, reinterpret_cast<const void*>(addr::cSC4PropDeveloper_vftable_4), sizeof(g_vtable4));
    std::memcpy(g_vtable8, reinterpret_cast<const void*>(addr::cSC4PropDeveloper_vftable_8), sizeof(g_vtable8));
    g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
    g_vtable[1] = reinterpret_cast<void*>(&AddRef);
    g_vtable[2] = reinterpret_cast<void*>(&ReleaseObject);
    g_vtable[11] = reinterpret_cast<void*>(&DeletingDestructor);
    g_vtable4[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x4>);
    g_vtable4[1] = reinterpret_cast<void*>(&AddRefAt<0x4>);
    g_vtable4[2] = reinterpret_cast<void*>(&ReleaseAt<0x4>);
    g_vtable8[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x8>);
    g_vtable8[1] = reinterpret_cast<void*>(&AddRefAt<0x8>);
    g_vtable8[2] = reinterpret_cast<void*>(&ReleaseAt<0x8>);
    g_vtable8[5] = reinterpret_cast<void*>(&GetGZCLSID);
    log::Write("prop object: vtables copied (slots 3/7 %p %p, +4 slot 3 %p, +8 slots 3/4 %p %p)", g_vtable[3],
               g_vtable[7], g_vtable4[3], g_vtable8[3], g_vtable8[4]);
}

// A new empty list of pointers (a 12-byte sentinel from the STL allocator).
void NewList(char* self, std::size_t offset)
{
    At<Node*>(self, offset) = nullptr;
    auto* const head = static_cast<Node*>(Allocate(0xC));
    head->next = head;
    head->prev = head;
    At<Node*>(self, offset) = head;
}

// The constructor (detour of 0x006F3410, reached only by the factory's
// jump at 0x006F3613): the fields the game's sets, in its order of calls
// into the game. +0xA34 and the table's first word stay unset, as there.
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
    At<uint8_t>(self, field::kInitialized) = 0;
    At<uint32_t>(self, field::kRefCount) = 0;
    for (std::size_t offset = 0x14; offset <= 0x34; offset += 4)
        At<uint32_t>(self, offset) = 0;
    At<uint32_t>(self, 0x24) = 1;
    ResetIterator(self, INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX, false);
    {
        // The seed: the time of day (0x00470B50) * 28. The site is the
        // game's return address after the generator's constructor, which
        // fixed_seed.cpp treats as a clock seed.
        uint32_t now = 0;
        const uint32_t* const time = reinterpret_cast<TimeFn>(addr::SC4_TimeNow)(&now);
        const fixed_seed::SiteOverride site(0x006F34B6);
        reinterpret_cast<SeedFn>(addr::SC4Random_ConstructSeeded)(self + field::kRandom, *time * 0x1C);
    }
    At<uint8_t>(self, field::kRequesterAdded) = 0;
    for (std::size_t offset = 0xA04; offset <= 0xA30; offset += 4)
        At<uint32_t>(self, offset) = 0;
    At<uint32_t>(self, field::kTimedMs) = 0;
    At<uint8_t>(self, field::kNight) = 0;
    At<float>(self, field::kThreshold) = 0.4f;
    At<float>(self, field::kLastValue) = -100.0f;
    char* const table = self + field::kTable;
    At<uint32_t>(table, 0x4) = 0;
    At<uint32_t>(table, 0x8) = 0;
    At<uint32_t>(table, 0xC) = 0;
    At<uint32_t>(table, 0x10) = 0;
    reinterpret_cast<CountFn>(addr::SC4Uint32HashSet_Construct)(table, 100);
    NewList(self, field::kList5C);
    NewList(self, field::kList60);
    NewList(self, field::kList64);
    At<uint8_t>(self, field::kSignsVisible) = 1;
    NewList(self, field::kSigns);
    At<uint8_t>(self, field::kOptionalState) = 0;
    static int created = 0;
    if (created++ < 3)
        log::Write("prop object: constructed %p", static_cast<void*>(self));
    return self;
}

using ConstructFn = void*(__fastcall*)(char*, void*);
ConstructFn g_originalConstruct = nullptr;

bool ApplyObject(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    const addr::Entry* const entry = addr_check::Find(addr::cSC4PropDeveloper_cSC4PropDeveloper);
    return entry && hook::Install(*entry, &Construct, &g_originalConstruct);
}

const bool g_registeredObject = registry::Register({
    "simulators.prop.object",
    "simulators/prop",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PropDeveloper_cSC4PropDeveloper,
    "Our cSC4PropDeveloper object: constructor (detour), destructor, identity; methods from copies of the "
    "(patched) game vtables (src/core/prop_object.cpp)",
    false,
    ApplyObject,
});

} // namespace
} // namespace opensc4::prop_sim
