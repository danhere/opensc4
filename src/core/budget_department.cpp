// The department budgets (cSC4DepartmentBudget, docs/budget.md "Departments
// and line items"): the interface slots, the internal helpers and the save
// record. Containers go through the game's own helpers, so their allocations
// are the game's.
#include "core/address_check.h"
#include "core/budget_department.h"
#include "core/budget_sim.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::budget_sim {
namespace {

using AllocateFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* p);
const auto Allocate = reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);

// A budget item (0x20 bytes) as the simulator's GetBudgetItemInfo makes it.
struct Item {
    uint32_t department;
    uint32_t line;
    uint32_t purpose;
    uint32_t untouched;
    int64_t cost;
    uint32_t index;
    float funding; // a fraction
};
static_assert(sizeof(Item) == 0x20);

template <typename T>
struct Vector {
    T* begin;
    T* end;
    T* capacity;
    std::size_t Size() const { return static_cast<std::size_t>(end - begin); }
};

struct Key {
    void* holder;
    uint32_t purpose;
};

void* Simulator()
{
    return Global<void*>(addr::SC4_BudgetSimulator);
}

char* Header(void* self, std::size_t offset)
{
    return At<char*>(self, offset);
}

// The line item map's find (0x00497520): the node, or the header.
char* FindLine(void* self, uint32_t id)
{
    using FindFn = char**(__thiscall*)(void* map, char** result, const uint32_t* key);
    char* result = nullptr;
    reinterpret_cast<FindFn>(addr::SC4Uint32Map_Find)(static_cast<char*>(self) + dept::kLines, &result, &id);
    return result;
}

// The line item map's operator[] (0x00497A00).
void** LineAt(void* self, const uint32_t* id)
{
    using AtFn = void**(__thiscall*)(void* map, const uint32_t* key);
    return reinterpret_cast<AtFn>(addr::SC4Uint32Map_At)(static_cast<char*>(self) + dept::kLines, id);
}

char* FindLocal(void* self, const Key& key)
{
    using FindFn = char*(__thiscall*)(void* set, const Key* key);
    return reinterpret_cast<FindFn>(addr::LocalFundingSet_Find)(static_cast<char*>(self) + dept::kLocal, &key);
}

void InsertLocal(void* self, const Key& key)
{
    struct {
        char* node;
        uint8_t inserted;
    } result{};
    using InsertFn = void*(__thiscall*)(void* set, void* result, const Key* key);
    reinterpret_cast<InsertFn>(addr::LocalFundingSet_Insert)(static_cast<char*>(self) + dept::kLocal, &result, &key);
}

// A tree node out of a map or set at `offset` (header pointer, then count).
void EraseNode(void* self, std::size_t offset, char* node)
{
    char* const header = Header(self, offset);
    using RebalanceFn = void*(__cdecl*)(char* node, void* root, void* leftmost, void* rightmost);
    if (void* const erased =
            reinterpret_cast<RebalanceFn>(addr::SC4Tree_RebalanceForErase)(node, header + 0x4, header + 0x8, header + 0xC))
        Free(erased);
    --At<uint32_t>(self, offset + 4);
}

// The simulator's GetBudgetItemForPurpose (slot 51) into an item whose
// funding starts at zero, as the game's callers do.
bool ItemFor(void* holder, uint32_t purpose, Item* item)
{
    item->funding = 0.0f;
    return Virtual<uint8_t>(Simulator(), bslot::kGetBudgetItemForPurpose, holder, purpose, item) != 0;
}

int64_t AtLeastZero(int64_t value)
{
    return value < 0 ? 0 : value;
}

// The game's rounding helper (0x006A98B0): floor(value + 0.5) of a float.
int32_t RoundHalfUp(float value)
{
    return static_cast<int32_t>(static_cast<int64_t>(std::floor(static_cast<double>(value) + 0.5)));
}

// A new framework message (0x2C bytes) of a type, referenced once.
void* NewMessage(uint32_t type)
{
    using MessageFn = void*(__thiscall*)(void* memory);
    void* const memory = reinterpret_cast<AllocateFn>(addr::RZ_operator_new)(0x2C);
    void* const message = memory ? reinterpret_cast<MessageFn>(addr::cGZMessage_cGZMessage)(memory) : nullptr;
    Virtual<uint32_t>(message, 0x4);
    Virtual<uint32_t>(message, 0x14, type);
    return message;
}

void PostAndRelease(void* message)
{
    Virtual<uint32_t>(Global<void*>(addr::SC4_MessageServer), 0x10, message, 0u);
    Virtual<uint32_t>(message, 0x8);
}

// The helpers through their game addresses, so each is ours or the game's
// as configured.
void SetFunding(void* self, const float* funding, uint32_t line, uint32_t share)
{
    using Fn = void(__thiscall*)(void* self, const float* funding, uint32_t line, uint32_t share);
    reinterpret_cast<Fn>(addr::cSC4DepartmentBudget_SetFunding)(self, funding, line, share);
}

void ShareLocalFunding(void* self)
{
    using Fn = void(__thiscall*)(void* self);
    reinterpret_cast<Fn>(addr::cSC4DepartmentBudget_ShareLocalFunding)(self);
}

void RecountLocalLine(void* self, uint32_t line)
{
    using Fn = void(__thiscall*)(void* self, uint32_t line);
    reinterpret_cast<Fn>(addr::cSC4DepartmentBudget_RecountLocalLine)(self, line);
}

void Announce(void* self, uint32_t message, uint32_t value)
{
    using Fn = void(__thiscall*)(void* self, uint32_t message, uint32_t value);
    reinterpret_cast<Fn>(addr::cSC4DepartmentBudget_Announce)(self, message, value);
}

// --- The helpers ---------------------------------------------------------------

// 0x006AB810 (funding*, line, share): clamped to the minimum (0x00B49030) and
// the maximum; unless the department is fixed or the value unchanged, each
// city-funded expense line's current expense is its full expense times the
// funding (rounded), the local shares are made again if asked, and
// 0x08DAC3E7 is announced with the line.
void __fastcall OurSetFunding(char* self, void*, const float* funding, uint32_t line, uint32_t share)
{
    static bool logged = false;
    FirstCall("department SetFunding", logged);
    float value = *funding;
    const float least = Global<float>(addr::Budget_MinFunding);
    if (value < least)
        value = least;
    if (value > At<float>(self, dept::kMaxFunding))
        value = At<float>(self, dept::kMaxFunding);
    if (At<uint8_t>(self, dept::kFixed) || value == At<float>(self, dept::kFunding))
        return;
    At<float>(self, dept::kFunding) = value;
    char* const header = Header(self, dept::kLines);
    for (char* node = At<char*>(header, 0x8); node != header; node = MapNext(node)) {
        void* const item = At<void*>(node, 0x14);
        if (Virtual<uint8_t>(item, lslot::kIsLocal) || Virtual<uint32_t>(item, lslot::kGetType) == 1)
            continue;
        const int64_t full = Virtual<int64_t>(item, lslot::kGetFull);
        const auto current = static_cast<int64_t>(static_cast<double>(full) * static_cast<double>(value) + 0.5);
        At<int64_t>(item, line::kCurrent) = AtLeastZero(current);
    }
    if (static_cast<uint8_t>(share))
        ShareLocalFunding(self);
    Announce(self, 0x08DAC3E7, line);
}

// One locally funded object in ShareLocalFunding (0x40 bytes, the game's
// stack record copied into the vector).
struct Share {
    void* holder;
    uint32_t unused4;
    Item item;
    uint8_t done;
    uint8_t pad[3];
    int32_t cost;  // the item's cost, low word
    int32_t most;  // cost x the maximum funding, rounded
    int32_t share; // what the city pays
    float funding; // the item's local funding
    uint32_t unused3c;
};
static_assert(sizeof(Share) == 0x40);

// The game's rounding through the float 1.5 x 2^23: x rounded up (towards
// +infinity) when `up`, else down, as the shares are handed out.
int32_t Step(double x, bool up)
{
    const double moved = up ? 0.5 + x : x - 0.5;
    const auto rounded = static_cast<float>(moved + 12582912.0);
    uint32_t bits = 0;
    std::memcpy(&bits, &rounded, sizeof(bits));
    int32_t n = static_cast<int32_t>(bits - 0x4B400000u);
    if (up) {
        if (x == static_cast<double>(n - 1))
            --n;
    } else if (x == static_cast<double>(n + 1)) {
        ++n;
    }
    return n;
}

// 0x006AB340: the department's funding shared out over its locally funded
// objects. Each object starts at its own local funding; what is left of the
// department's funding x the objects' costs is handed out in proportion to
// cost, each object up to the maximum funding (or down to zero), until
// nothing is left. Each object's funding is written back to its budget item
// (simulator slot 54) and its line's current expense raised by its share.
void __fastcall OurShareLocalFunding(char* self, void*)
{
    static bool logged = false;
    FirstCall("department ShareLocalFunding", logged);
    if (!At<uint32_t>(self, dept::kLocal + 4))
        return;
    int32_t costs = 0;
    int64_t shares = 0;
    const float* const funding = Virtual<const float*>(self, dslot::kGetFunding);
    const float* const most = Virtual<const float*>(self, dslot::kGetMaxFunding);
    void* const simulator = Simulator();
    Vector<Share> records{};
    char* const header = Header(self, dept::kLocal);
    for (char* node = At<char*>(header, 0x8); node != header; node = MapNext(node)) {
        Item item;
        item.funding = 0.0f;
        if (!Virtual<uint8_t>(simulator, bslot::kGetBudgetItemForPurpose, At<void*>(node, 0x10), At<uint32_t>(node, 0x14),
                              &item))
            continue;
        Share record{};
        record.holder = At<void*>(node, 0x10);
        record.item = item;
        record.done = 0;
        record.cost = static_cast<int32_t>(static_cast<uint32_t>(item.cost));
        record.funding = item.funding;
        record.share = static_cast<int32_t>(static_cast<int64_t>(
            std::floor(static_cast<double>(record.cost) * static_cast<double>(record.funding) + 0.5)));
        record.most = static_cast<int32_t>(
            static_cast<int64_t>(std::floor(static_cast<double>(record.cost) * static_cast<double>(*most) + 0.5)));
        if (records.end != records.capacity) {
            if (records.end)
                *records.end = record;
            ++records.end;
        } else {
            char allocator = 0;
            using InsertFn = void(__thiscall*)(void* vector, Share* at, const Share* value, void* allocator, uint32_t count,
                                               uint32_t one);
            reinterpret_cast<InsertFn>(addr::LocalShareVector_InsertN)(&records, records.end, &record, &allocator, 1, 1);
        }
        shares += record.share;
        costs += record.cost;
    }
    const auto count = static_cast<int32_t>(records.Size());
    const auto costsAsFloat = static_cast<float>(costs);
    int32_t left = static_cast<int32_t>(static_cast<int64_t>(
                       std::floor(static_cast<double>(costs) * static_cast<double>(*funding) + 0.5))) -
                   static_cast<int32_t>(shares);
    auto ratio = static_cast<float>(static_cast<double>(left) / static_cast<double>(costsAsFloat));
    int32_t open = count;
    int32_t openCosts = costs;
    if (left != 0) {
        while (open != 0) {
            for (int32_t i = 0; i < count; ++i) {
                Share& record = records.begin[i];
                if (record.done)
                    continue;
                const double x = static_cast<double>(record.cost) * static_cast<double>(ratio);
                int32_t step;
                if (left < 0) {
                    const int32_t n = Step(x, false);
                    step = n >= left ? n : left;
                    if (step + record.share < 0) {
                        step = -record.share;
                        --open;
                        openCosts -= record.cost;
                        record.done = 1;
                    }
                } else {
                    const int32_t n = Step(x, true);
                    step = n <= left ? n : left;
                    if (step + record.share > record.most) {
                        step = record.most - record.share;
                        --open;
                        openCosts -= record.cost;
                        record.done = 1;
                    }
                }
                record.share += step;
                left -= step;
                if (left == 0)
                    break;
            }
            ratio = static_cast<float>(static_cast<double>(left) / static_cast<double>(openCosts));
            if (left == 0)
                break;
        }
    }
    char* const lines = Header(self, dept::kLines);
    for (char* node = At<char*>(lines, 0x8); node != lines; node = MapNext(node)) {
        void* const item = At<void*>(node, 0x14);
        if (Virtual<uint8_t>(item, lslot::kIsLocal))
            At<int64_t>(item, line::kCurrent) = 0;
    }
    for (int32_t i = 0; i < count; ++i) {
        const Share& record = records.begin[i];
        float fraction = static_cast<float>(static_cast<double>(record.share) / static_cast<double>(record.cost));
        Virtual<uint8_t>(simulator, bslot::kChangeBudgetItemLocalFunding, record.holder, record.item.index, &fraction);
        void* const item = *LineAt(self, &record.item.line);
        const int64_t current = Virtual<int64_t>(item, lslot::kGetCurrent) + record.share;
        At<int64_t>(item, line::kCurrent) = AtLeastZero(current);
    }
    if (records.begin)
        Free(records.begin);
}

// 0x006AB1E0 (line): a local expense line's current expense again: the sum
// over the locally funded objects' budget items on this department and line
// of cost x local funding (in hundredths), divided by 100.
void __fastcall OurRecountLocalLine(char* self, void*, uint32_t id)
{
    static bool logged = false;
    FirstCall("department RecountLocalLine", logged);
    void* const item = *LineAt(self, &id);
    if (!Virtual<uint8_t>(item, lslot::kIsLocal) || Virtual<uint32_t>(item, lslot::kGetType) != 0)
        return;
    int64_t sum = 0;
    char* const header = Header(self, dept::kLocal);
    for (char* node = At<char*>(header, 0x8); node != header; node = MapNext(node)) {
        Vector<Item> items{};
        Virtual<uint8_t>(Simulator(), bslot::kGetBudgetItemInfo, At<void*>(node, 0x10), &items);
        for (Item* entry = items.begin; entry != items.end; ++entry) {
            if (entry->line != id || entry->department != At<uint32_t>(self, dept::kId))
                continue;
            sum = static_cast<int64_t>(static_cast<double>(entry->cost) * static_cast<double>(entry->funding) *
                                           static_cast<double>(100.0f) +
                                       static_cast<double>(sum));
        }
        if (items.begin)
            Free(items.begin);
    }
    At<int64_t>(item, line::kCurrent) = AtLeastZero(sum / 100);
}

// 0x006A98D0 (message, value): the simulator told (slot 33 for 0x08DAC3E7,
// 32 for 0x08DAC3E8), then the message posted with the department, its ID
// and the value.
void __fastcall OurAnnounce(char* self, void*, uint32_t type, uint32_t value)
{
    static bool logged = false;
    FirstCall("department Announce", logged);
    if (type == 0x08DAC3E7)
        Virtual<uint32_t>(Simulator(), bslot::kFundingChanged, self);
    else if (type == 0x08DAC3E8)
        Virtual<uint32_t>(Simulator(), bslot::kNeededFundingChanged, self);
    void* const message = NewMessage(type);
    Virtual<uint32_t>(message, 0x30, self);
    Virtual<uint32_t>(message, 0x84, self);
    Virtual<uint32_t>(message, 0x40, At<uint32_t>(self, dept::kId));
    Virtual<uint32_t>(message, 0x54, value);
    PostAndRelease(message);
}

// 0x006A9980: as Announce for 0x08DAC3E8 with the value 0.
void __fastcall OurAnnounceNeededFunding(char* self, void*)
{
    static bool logged = false;
    FirstCall("department AnnounceNeededFunding", logged);
    Virtual<uint32_t>(Simulator(), bslot::kNeededFundingChanged, self);
    void* const message = NewMessage(0x08DAC3E8);
    Virtual<uint32_t>(message, 0x30, self);
    Virtual<uint32_t>(message, 0x84, self);
    Virtual<uint32_t>(message, 0x40, At<uint32_t>(self, dept::kId));
    Virtual<uint32_t>(message, 0x54, 0u);
    PostAndRelease(message);
}

// Clears a map or set at `offset` with the game's node eraser.
void ClearTree(void* self, std::size_t offset, std::uintptr_t eraser)
{
    char* const header = Header(self, offset);
    using EraseFn = void(__thiscall*)(void* tree, char* node);
    char* node = At<char*>(header, 0x4);
    while (node) {
        reinterpret_cast<EraseFn>(eraser)(static_cast<char*>(self) + offset, At<char*>(node, 0xC));
        char* const left = At<char*>(node, 0x8);
        Free(node);
        node = left;
    }
    At<char*>(header, 0x8) = header;
    At<char*>(header, 0x4) = nullptr;
    At<char*>(header, 0xC) = header;
    At<uint32_t>(self, offset + 4) = 0;
}

// 0x006AA300: once initialised, each line item let go of (its department
// reference and the map's), each locally funded object released, both
// containers cleared.
uint8_t __fastcall OurShutdown(char* self, void*)
{
    static bool logged = false;
    FirstCall("department Shutdown", logged);
    if (!At<uint8_t>(self, dept::kInitialised))
        return 1;
    At<uint8_t>(self, dept::kInitialised) = 0;
    char* const lines = Header(self, dept::kLines);
    for (char* node = At<char*>(lines, 0x8); node != lines; node = MapNext(node)) {
        char* const item = At<char*>(node, 0x14);
        if (At<uint8_t>(item, line::kOwned)) {
            At<uint8_t>(item, line::kOwned) = 0;
            if (void* const department = At<void*>(item, line::kDepartment)) {
                At<void*>(item, line::kDepartment) = nullptr;
                Virtual<uint32_t>(department, 0x8);
            }
        }
        Virtual<uint32_t>(item, 0x8);
    }
    if (At<uint32_t>(self, dept::kLines + 4)) {
        using EraseFn = void(__thiscall*)(void* tree, char* node);
        reinterpret_cast<EraseFn>(addr::LineItemMap_EraseNodes)(self + dept::kLines, At<char*>(lines, 0x4));
        At<char*>(lines, 0x8) = lines;
        At<char*>(lines, 0x4) = nullptr;
        At<char*>(lines, 0xC) = lines;
        At<uint32_t>(self, dept::kLines + 4) = 0;
    }
    char* const local = Header(self, dept::kLocal);
    for (char* node = At<char*>(local, 0x8); node != local; node = MapNext(node))
        Virtual<uint32_t>(At<void*>(node, 0x10), 0x8);
    if (At<uint32_t>(self, dept::kLocal + 4))
        ClearTree(self, dept::kLocal, addr::LocalFundingSet_EraseNodes);
    return 1;
}

// --- The interface (vtable 0x00AB0EB0) -----------------------------------------

uint32_t __fastcall GetDepartmentId(char* self, void*)
{
    return At<uint32_t>(self, dept::kId);
}

// 4 (0x0060FB60) (cIGZString&): the name copied out (the string's slot 13).
uint8_t __fastcall GetDepartmentName(char* self, void*, void* name)
{
    return Virtual<uint8_t>(name, 0x34, self + dept::kName);
}

// The game's stack localised string (0x20 bytes): string, then the key.
struct LocalizedString {
    const void* vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
    uint32_t key[3];
};

// The localised text (group, instance) into a string whose buffer is at
// `buffer`.
void AssignText(char* buffer, uint32_t group, uint32_t instance)
{
    LocalizedString text{};
    using TextFn = void*(__thiscall*)(LocalizedString* string, uint32_t group, uint32_t instance, uint32_t flag);
    auto* const made =
        static_cast<char*>(reinterpret_cast<TextFn>(addr::SC4LocalizedString_SC4LocalizedString)(&text, group, instance, 1));
    char* const from = made ? made + 4 : nullptr;
    if (from != buffer) {
        char allocator = 0;
        using AssignRangeFn = void*(__thiscall*)(void* buffer, char* first, char* last, void* allocator);
        reinterpret_cast<AssignRangeFn>(addr::SC4String_AssignRange)(buffer, At<char*>(from, 0x0), At<char*>(from, 0x4),
                                                                     &allocator);
    }
    text.vtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
    if (text.begin)
        Free(text.begin);
}

// 5 (0x006AC3F0) (group, instance): the localised name.
uint8_t __fastcall SetDepartmentName(char* self, void*, uint32_t group, uint32_t instance)
{
    At<uint32_t>(self, dept::kNameInstance) = instance;
    At<uint32_t>(self, dept::kNameGroup) = group;
    AssignText(self + dept::kName + 4, group, instance);
    return 1;
}

uint32_t __fastcall GetBudgetGroup(char* self, void*)
{
    return At<uint32_t>(self, dept::kGroup);
}

uint8_t __fastcall SetBudgetGroup(char* self, void*, uint32_t group)
{
    At<uint32_t>(self, dept::kGroup) = group;
    return static_cast<uint8_t>(group);
}

uint8_t __fastcall GetIsFixedFunding(char* self, void*)
{
    return At<uint8_t>(self, dept::kFixed);
}

uint8_t __fastcall SetFixedFunding(char* self, void*, uint8_t fixed)
{
    At<uint8_t>(self, dept::kFixed) = fixed;
    return fixed;
}

float* __fastcall GetFundingPercentage(char* self, void*)
{
    return &At<float>(self, dept::kFunding);
}

// 11 (0x006AC1B0) (funding*, line): SetFunding with the local shares made
// again.
uint8_t __fastcall SetFundingPercentage(char* self, void*, const float* funding, uint32_t line)
{
    static bool logged = false;
    FirstCall("department SetFundingPercentage", logged);
    SetFunding(self, funding, line, 1);
    return 1;
}

float* __fastcall GetMaxAllowedFundingPercentage(char* self, void*)
{
    return &At<float>(self, dept::kMaxFunding);
}

uint8_t __fastcall SetMaxAllowedFundingPercentage(char* self, void*, const float* most)
{
    At<float>(self, dept::kMaxFunding) = *most;
    return static_cast<uint8_t>(reinterpret_cast<std::uintptr_t>(most));
}

// 14-16: the sums over the line items of slots 10, 11 and 14.
template <std::size_t LineSlot>
int64_t __fastcall SumLines(char* self, void*)
{
    int64_t sum = 0;
    char* const header = Header(self, dept::kLines);
    for (char* node = At<char*>(header, 0x8); node != header; node = MapNext(node))
        sum += Virtual<int64_t>(At<void*>(node, 0x14), LineSlot);
    return sum;
}

// 17 (0x006AB040) (id, local): a new line item (0x68 bytes) in the map under
// its ID, in this department.
void* __fastcall CreateLineItem(char* self, void*, uint32_t id, uint8_t local)
{
    static bool logged = false;
    FirstCall("department CreateLineItem", logged);
    using ConstructFn = void*(__thiscall*)(void* memory);
    void* const memory = reinterpret_cast<AllocateFn>(addr::SC4_operator_new)(line::kSize);
    auto* const item =
        static_cast<char*>(memory ? reinterpret_cast<ConstructFn>(addr::cSC4LineItem_cSC4LineItem)(memory) : nullptr);
    Virtual<uint32_t>(item, 0x4);
    if (!At<uint8_t>(item, line::kOwned))
        At<uint8_t>(item, line::kOwned) = 1;
    At<uint32_t>(item, line::kId) = id;
    void* const old = At<void*>(item, line::kDepartment);
    if (self != old) {
        if (self)
            Virtual<uint32_t>(self, 0x4);
        At<void*>(item, line::kDepartment) = self;
        if (old)
            Virtual<uint32_t>(old, 0x8);
    }
    At<uint8_t>(item, line::kLocal) = local;
    *LineAt(self, &id) = item;
    return item;
}

// 18 (0x006A9790) (building exemplar, local): a line named after the
// building.
void* __fastcall CreateLineItemForBuildingType(char* self, void*, uint32_t instance, uint8_t local)
{
    void* const item = Virtual<void*>(self, dslot::kCreateLineItem, instance, static_cast<uint32_t>(local));
    Virtual<uint32_t>(item, lslot::kSetName, 0u, instance);
    return item;
}

// 19 (0x006AA480) (id): the line item out of the map, let go of.
uint8_t __fastcall RemoveLineItem(char* self, void*, uint32_t id)
{
    static bool logged = false;
    FirstCall("department RemoveLineItem", logged);
    char* const node = FindLine(self, id);
    if (node == Header(self, dept::kLines))
        return 1;
    if (char* const item = At<char*>(node, 0x14)) {
        if (At<uint8_t>(item, line::kOwned)) {
            At<uint8_t>(item, line::kOwned) = 0;
            if (void* const department = At<void*>(item, line::kDepartment)) {
                At<void*>(item, line::kDepartment) = nullptr;
                Virtual<uint32_t>(department, 0x8);
            }
        }
        Virtual<uint32_t>(item, 0x8);
    }
    EraseNode(self, dept::kLines, node);
    return 1;
}

void* __fastcall GetLineItem(char* self, void*, uint32_t id)
{
    char* const node = FindLine(self, id);
    return node == Header(self, dept::kLines) ? nullptr : At<void*>(node, 0x14);
}

// 21 (0x006AAD50) (vector): every line item, referenced.
uint8_t __fastcall GetAllLineItems(char* self, void*, Vector<void*>* items)
{
    items->end = items->begin;
    using ReserveFn = void(__thiscall*)(void* vector, uint32_t count);
    reinterpret_cast<ReserveFn>(addr::PtrVector_Reserve)(items, At<uint32_t>(self, dept::kLines + 4));
    char* const header = Header(self, dept::kLines);
    for (char* node = At<char*>(header, 0x8); node != header; node = MapNext(node)) {
        void* item = At<void*>(node, 0x14);
        Virtual<uint32_t>(item, 0x4);
        if (items->end != items->capacity) {
            if (items->end)
                *items->end = item;
            ++items->end;
        } else {
            char allocator = 0;
            using InsertFn = void(__thiscall*)(void* vector, void** at, void* const* value, void* allocator, uint32_t count,
                                               uint32_t one);
            reinterpret_cast<InsertFn>(addr::SC4PtrVector_InsertN)(items, items->end, &item, &allocator, 1, 1);
        }
    }
    return 1;
}

// 22 (0x006AB0C0) (holder, purpose): the object kept (referenced) as
// locally funded; its budget item's cost added to its line's full expense,
// the line's current expense raised by cost x the department's funding, the
// item's local funding set to the department's.
uint8_t __fastcall AddLocallyFundedObject(char* self, void*, void* holder, uint32_t purpose)
{
    static bool logged = false;
    FirstCall("department AddLocallyFundedObject", logged);
    Item item{};
    ItemFor(holder, purpose, &item);
    void* serializable = nullptr;
    if (Virtual<uint8_t>(holder, 0x0, 0xE4FDA3D4u, &serializable))
        Virtual<uint32_t>(serializable, 0x8);
    Virtual<uint32_t>(holder, 0x4);
    InsertLocal(self, Key{holder, purpose});
    void* const entry = *LineAt(self, &item.line);
    Virtual<uint32_t>(entry, lslot::kAddToFull, item.cost);
    const float* const funding = Virtual<const float*>(self, dslot::kGetFunding);
    const float value = *funding;
    const int64_t current = Virtual<int64_t>(entry, lslot::kGetCurrent) +
                            static_cast<int64_t>(static_cast<double>(item.cost) * static_cast<double>(value));
    At<int64_t>(entry, line::kCurrent) = AtLeastZero(current);
    Virtual<uint8_t>(Simulator(), bslot::kChangeBudgetItemLocalFunding, holder, item.index, funding);
    Virtual<uint32_t>(Simulator(), bslot::kFundingChanged, self);
    return 1;
}

// 23 (0x006AA530) (holder, purpose): its full funding set to 0, released,
// out of the set; the last one gone with nothing needed puts the department
// back at full funding.
uint8_t __fastcall RemoveLocallyFundedObject(char* self, void*, void* holder, uint32_t purpose)
{
    static bool logged = false;
    FirstCall("department RemoveLocallyFundedObject", logged);
    char* const node = FindLocal(self, Key{holder, purpose});
    if (node == Header(self, dept::kLocal))
        return 0;
    Virtual<uint8_t>(self, dslot::kSetLocalFullFunding, holder, int64_t{0}, purpose);
    Virtual<uint32_t>(holder, 0x8);
    EraseNode(self, dept::kLocal, node);
    if (!At<uint32_t>(self, dept::kLocal + 4) && Virtual<int64_t>(self, dslot::kIdealFunding) == 0)
        Virtual<uint8_t>(self, dslot::kSetFunding, &Global<float>(addr::Budget_FullFunding), 0u);
    return 1;
}

// 24 (0x006AB940) (holder, funding*, purpose): a locally funded object's
// funding (within the minimum and the maximum): the department's funding
// follows the change in what the city pays (in whole hundredths of the
// cost); the object is told (message 0x6A5FD3FD through its interface
// 0x26BB767E), its budget item and its line's current expense updated.
uint8_t __fastcall SetLocalFundingPercent(char* self, void*, void* holder, const float* funding, uint32_t purpose)
{
    static bool logged = false;
    FirstCall("department SetLocalFundingPercent", logged);
    if (*funding < Global<float>(addr::Budget_MinFunding) || *funding > At<float>(self, dept::kMaxFunding))
        return 1;
    if (FindLocal(self, Key{holder, purpose}) == Header(self, dept::kLocal))
        return 1;
    Item item;
    if (!ItemFor(holder, purpose, &item))
        return 1;
    const int64_t now = RoundHalfUp(static_cast<float>(static_cast<double>(item.cost) * static_cast<double>(*funding) *
                                                       static_cast<double>(100.0f)));
    const int64_t before = RoundHalfUp(static_cast<float>(static_cast<double>(item.cost) *
                                                          static_cast<double>(item.funding) * static_cast<double>(100.0f)));
    const int64_t change = now - before;
    float total = At<float>(self, dept::kFunding);
    if (change != 0) {
        const int64_t ideal = Virtual<int64_t>(self, dslot::kIdealFunding);
        if (ideal != 0) {
            const float* const current = Virtual<const float*>(self, dslot::kGetFunding);
            const auto idealAsFloat = static_cast<float>(ideal);
            const int64_t paid =
                RoundHalfUp(static_cast<float>(static_cast<double>(ideal) * static_cast<double>(*current) *
                                               static_cast<double>(100.0f))) +
                change;
            total = static_cast<float>(static_cast<double>(paid) / static_cast<double>(idealAsFloat) *
                                       static_cast<double>(0.01f));
        } else {
            total = *funding;
        }
        void* target = nullptr;
        if (Virtual<uint8_t>(holder, 0x0, 0x26BB767Eu, &target)) {
            void* const message = NewMessage(0x6A5FD3FD);
            Virtual<uint32_t>(message, 0x84, target);
            PostAndRelease(message);
            Virtual<uint32_t>(target, 0x8);
        }
    }
    Virtual<uint8_t>(Simulator(), bslot::kChangeBudgetItemLocalFunding, holder, item.index, funding);
    RecountLocalLine(self, item.line);
    if (!(total == At<float>(self, dept::kFunding)))
        SetFunding(self, &total, 0, 0);
    return 1;
}

// 25 (0x006ABFD0) (holder, cost, purpose): a locally funded object's cost:
// its budget item and its line's full expense changed; the line's current
// expense by the change x its local funding (in whole hundredths); the
// department's funding so that the city pays as before.
uint8_t __fastcall SetLocalFullFunding(char* self, void*, void* holder, int64_t cost, uint32_t purpose)
{
    static bool logged = false;
    FirstCall("department SetLocalFullFunding", logged);
    if (FindLocal(self, Key{holder, purpose}) == Header(self, dept::kLocal))
        return 1;
    Item item;
    if (!ItemFor(holder, purpose, &item))
        return 1;
    Virtual<uint8_t>(Simulator(), bslot::kChangeBudgetItemCost, holder, item.index, cost);
    const int64_t change = cost - item.cost;
    const auto local = static_cast<int64_t>(static_cast<int32_t>(static_cast<int64_t>(
        std::floor(static_cast<double>(change) * static_cast<double>(item.funding) * static_cast<double>(100.0f) + 0.5))));
    const int64_t ideal = Virtual<int64_t>(self, dslot::kIdealFunding);
    const float current = *Virtual<const float*>(self, dslot::kGetFunding);
    const auto paid = static_cast<int64_t>(static_cast<int32_t>(static_cast<int64_t>(
        std::floor(static_cast<double>(ideal) * static_cast<double>(current) * static_cast<double>(100.0f) + 0.5))));
    const int64_t sum = paid + local;
    const int64_t newIdeal = ideal + change;
    float total = newIdeal != 0 ? static_cast<float>(static_cast<double>(sum) / static_cast<double>(newIdeal) *
                                                     static_cast<double>(0.01f))
                                : Global<float>(addr::Budget_FullFunding);
    void* const entry = *LineAt(self, &item.line);
    Virtual<uint32_t>(entry, lslot::kAddToFull, change);
    const int64_t expense = Virtual<int64_t>(entry, lslot::kGetCurrent) + local / 100;
    At<int64_t>(entry, line::kCurrent) = AtLeastZero(expense);
    AnnounceNeededFunding(self);
    SetFunding(self, &total, 0, 0);
    return 1;
}

// 26 (0x006AA610) (holder, purpose): the object's local funding, or the
// minimum. The game hands out a pointer into its finished stack frame; ours
// points at a static copy.
const float* __fastcall GetLocalFundingPercent(char* self, void*, void* holder, uint32_t purpose)
{
    static float funding = 0.0f;
    Item item;
    if (FindLocal(self, Key{holder, purpose}) != Header(self, dept::kLocal) && ItemFor(holder, purpose, &item)) {
        funding = item.funding;
        return &funding;
    }
    return &Global<float>(addr::Budget_MinFunding);
}

// 27 (0x006AA680) (holder, purpose): the object's cost, or 0.
int64_t __fastcall GetLocalFullFunding(char* self, void*, void* holder, uint32_t purpose)
{
    Item item;
    if (FindLocal(self, Key{holder, purpose}) != Header(self, dept::kLocal) && ItemFor(holder, purpose, &item))
        return item.cost;
    return 0;
}

uint8_t __fastcall IsLocallyFundedObjectInDepartment(char* self, void*, void* holder, uint32_t purpose)
{
    return FindLocal(self, Key{holder, purpose}) != Header(self, dept::kLocal) ? 1 : 0;
}

// A std::map<uint32, int64> (Int64Map, nodes 0x20 bytes: key +0x10, value
// +0x18).
struct Int64Map {
    char* header;
    uint32_t count;
};

// 29 (0x006ABB50): each line's current expense again: city-funded lines
// are counted, local expense lines get the sum of cost x local funding of
// their objects' budget items (in hundredths, divided by 100). If the
// department's lines now cost a share of their full expense more than half a
// percent (and more than the count plus 2) away from its funding, the
// funding follows that share (within 0 and the maximum).
uint8_t __fastcall RecalculateAllLineItemCurrentExpenses(char* self, void*)
{
    static bool logged = false;
    FirstCall("department RecalculateAll", logged);
    Int64Map sums{};
    sums.header = static_cast<char*>(Allocate(0x20));
    sums.header[0] = 0;
    At<char*>(sums.header, 0x4) = nullptr;
    At<char*>(sums.header, 0x8) = sums.header;
    At<char*>(sums.header, 0xC) = sums.header;
    using SumAtFn = int64_t*(__thiscall*)(Int64Map* map, const uint32_t* key);
    const auto sumAt = reinterpret_cast<SumAtFn>(addr::Int64Map_At);
    uint32_t counted = 0;
    char* const lines = Header(self, dept::kLines);
    for (char* node = At<char*>(lines, 0x8); node != lines; node = MapNext(node)) {
        void* const item = At<void*>(node, 0x14);
        if (!Virtual<uint8_t>(item, lslot::kIsLocal))
            ++counted;
        else if (Virtual<uint32_t>(item, lslot::kGetType) == 0)
            *sumAt(&sums, &At<uint32_t>(node, 0x10)) = 0;
    }
    char* const local = Header(self, dept::kLocal);
    for (char* node = At<char*>(local, 0x8); node != local; node = MapNext(node)) {
        Vector<Item> items{};
        Virtual<uint8_t>(Simulator(), bslot::kGetBudgetItemInfo, At<void*>(node, 0x10), &items);
        for (Item* entry = items.begin; entry != items.end; ++entry) {
            if (entry->department != At<uint32_t>(self, dept::kId))
                continue;
            char* found = sums.header;
            for (char* at = At<char*>(sums.header, 0x4); at;) {
                if (At<uint32_t>(at, 0x10) < entry->line) {
                    at = At<char*>(at, 0xC);
                } else {
                    found = at;
                    at = At<char*>(at, 0x8);
                }
            }
            if (found == sums.header || entry->line < At<uint32_t>(found, 0x10))
                continue;
            int64_t& sum = At<int64_t>(found, 0x18);
            sum = static_cast<int64_t>(static_cast<double>(entry->cost) * static_cast<double>(entry->funding) *
                                           static_cast<double>(100.0f) +
                                       static_cast<double>(sum));
            ++counted;
        }
        if (items.begin)
            Free(items.begin);
    }
    for (char* node = At<char*>(sums.header, 0x8); node != sums.header; node = MapNext(node)) {
        void* const item = *LineAt(self, &At<uint32_t>(node, 0x10));
        const int64_t current = At<int64_t>(node, 0x18) / 100;
        Virtual<int64_t>(item, lslot::kGetCurrent);
        At<int64_t>(item, line::kCurrent) = AtLeastZero(current);
    }
    int64_t current = 0, full = 0;
    for (char* node = At<char*>(lines, 0x8); node != lines; node = MapNext(node)) {
        void* const item = At<void*>(node, 0x14);
        current += Virtual<int64_t>(item, lslot::kGetCurrent);
        full += Virtual<int64_t>(item, lslot::kGetFull);
    }
    if (full > 0) {
        const auto currentAsFloat = static_cast<float>(current);
        const auto fullAsFloat = static_cast<float>(full);
        float share = static_cast<float>(static_cast<double>(currentAsFloat) * static_cast<double>(100.0f) /
                                         static_cast<double>(fullAsFloat));
        const double now = static_cast<double>(*Virtual<const float*>(self, dslot::kGetFunding)) * static_cast<double>(100.0f);
        if (std::fabs(static_cast<double>(share) - now) > 0.5) {
            const double off = std::fabs(static_cast<double>(currentAsFloat) -
                                         now * static_cast<double>(0.01f) * static_cast<double>(fullAsFloat));
            if (static_cast<double>(counted) + 2.0 < off) {
                if (share < 0.0f)
                    share = 0.0f;
                const double most = static_cast<double>(At<float>(self, dept::kMaxFunding)) * static_cast<double>(100.0f);
                if (static_cast<double>(share) > most)
                    share = static_cast<float>(most);
                float fraction = static_cast<float>(static_cast<double>(share) * static_cast<double>(0.01f));
                SetFunding(self, &fraction, 0, 1);
            }
        }
    }
    if (sums.count) {
        using EraseFn = void(__thiscall*)(Int64Map* map, char* node);
        char* node = At<char*>(sums.header, 0x4);
        while (node) {
            reinterpret_cast<EraseFn>(addr::Int64Map_EraseNodes)(&sums, At<char*>(node, 0xC));
            char* const left = At<char*>(node, 0x8);
            Free(node);
            node = left;
        }
    }
    Free(sums.header);
    return 1;
}

int64_t __fastcall GetTotalSpending(char* self, void*)
{
    return At<int64_t>(self, dept::kSpending);
}

uint8_t __fastcall SetTotalSpending(char* self, void*, int64_t spending)
{
    At<int64_t>(self, dept::kSpending) = spending;
    return static_cast<uint8_t>(spending);
}

// 32 (0x006AA9C0) (purpose, list): the locally funded objects for a purpose
// appended to a list (nodes 0xC bytes: next, previous, holder), referenced.
uint8_t __fastcall GetLocallyFundedItemsByPurpose(char* self, void*, uint32_t purpose, char** list)
{
    char* const header = Header(self, dept::kLocal);
    for (char* node = At<char*>(header, 0x8); node != header; node = MapNext(node)) {
        if (At<uint32_t>(node, 0x14) != purpose)
            continue;
        void* const holder = At<void*>(node, 0x10);
        if (holder)
            Virtual<uint32_t>(holder, 0x4);
        char* const head = *list;
        auto* const entry = static_cast<char*>(Allocate(0xC));
        At<void*>(entry, 0x8) = holder;
        if (holder)
            Virtual<uint32_t>(holder, 0x4);
        char* const last = At<char*>(head, 0x4);
        At<char*>(entry, 0x0) = head;
        At<char*>(entry, 0x4) = last;
        At<char*>(last, 0x0) = entry;
        At<char*>(head, 0x4) = entry;
        if (holder)
            Virtual<uint32_t>(holder, 0x8);
    }
    return 1;
}

uint8_t __fastcall HasLocallyFundedItemsByPurpose(char* self, void*, uint32_t purpose)
{
    char* const header = Header(self, dept::kLocal);
    for (char* node = At<char*>(header, 0x8); node != header; node = MapNext(node))
        if (At<uint32_t>(node, 0x14) == purpose)
            return 1;
    return 0;
}

#define OPENSC4_SLOT(index, original, fn) {index, original, reinterpret_cast<void*>(&fn)}

const Slot kSlots[] = {
    OPENSC4_SLOT(3, 0x0040CE60, GetDepartmentId),
    OPENSC4_SLOT(4, 0x0060FB60, GetDepartmentName),
    OPENSC4_SLOT(5, 0x006AC3F0, SetDepartmentName),
    OPENSC4_SLOT(6, 0x006AAC50, GetBudgetGroup),
    OPENSC4_SLOT(7, 0x006AAC60, SetBudgetGroup),
    OPENSC4_SLOT(8, 0x006AAC70, GetIsFixedFunding),
    OPENSC4_SLOT(9, 0x006AAC80, SetFixedFunding),
    OPENSC4_SLOT(10, 0x0051A310, GetFundingPercentage),
    OPENSC4_SLOT(11, 0x006AC1B0, SetFundingPercentage),
    OPENSC4_SLOT(12, 0x006A9750, GetMaxAllowedFundingPercentage),
    OPENSC4_SLOT(13, 0x006A9760, SetMaxAllowedFundingPercentage),
    OPENSC4_SLOT(14, 0x006A9EF0, SumLines<lslot::kGetFull>),
    OPENSC4_SLOT(15, 0x006A9F60, SumLines<lslot::kGetCurrent>),
    OPENSC4_SLOT(16, 0x006A9FD0, SumLines<lslot::kGetIncome>),
    OPENSC4_SLOT(17, 0x006AB040, CreateLineItem),
    OPENSC4_SLOT(18, 0x006A9790, CreateLineItemForBuildingType),
    OPENSC4_SLOT(19, 0x006AA480, RemoveLineItem),
    OPENSC4_SLOT(20, 0x006AA500, GetLineItem),
    OPENSC4_SLOT(21, 0x006AAD50, GetAllLineItems),
    OPENSC4_SLOT(22, 0x006AB0C0, AddLocallyFundedObject),
    OPENSC4_SLOT(23, 0x006AA530, RemoveLocallyFundedObject),
    OPENSC4_SLOT(24, 0x006AB940, SetLocalFundingPercent),
    OPENSC4_SLOT(25, 0x006ABFD0, SetLocalFullFunding),
    OPENSC4_SLOT(26, 0x006AA610, GetLocalFundingPercent),
    OPENSC4_SLOT(27, 0x006AA680, GetLocalFullFunding),
    OPENSC4_SLOT(28, 0x006AA5D0, IsLocallyFundedObjectInDepartment),
    OPENSC4_SLOT(29, 0x006ABB50, RecalculateAllLineItemCurrentExpenses),
    OPENSC4_SLOT(30, 0x006AAEF0, GetTotalSpending),
    OPENSC4_SLOT(31, 0x006A9770, SetTotalSpending),
    OPENSC4_SLOT(32, 0x006AA9C0, GetLocallyFundedItemsByPurpose),
    OPENSC4_SLOT(33, 0x006AA040, HasLocallyFundedItemsByPurpose),
};

bool InstallInterface(registry::Mode)
{
    return SwapSlots("department budget", addr::cSC4DepartmentBudget_vftable, kSlots, sizeof(kSlots) / sizeof(kSlots[0]));
}

const bool g_registeredInterface = registry::Register({
    "simulators.budget.department",
    "simulators/budget",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4DepartmentBudget_vftable,
    "Our department budgets' slots 3-33: name, group, funding, line items, locally funded objects, the current "
    "expenses (docs/budget.md)",
    false,
    InstallInterface,
});

// --- The save record (cIGZSerializable at +0x4, vtable 0x00AB0E94) -------------

using WriteBoolFn = void(__cdecl*)(void* stream, uint32_t value);
using ReadBoolFn = void(__cdecl*)(void* stream, void* value);
using WriteObjectFn = uint8_t(__cdecl*)(void* stream, const void* object);
using ReadObjectFn = uint8_t(__cdecl*)(void* stream, uint32_t clsid, void* object, uint32_t flags);

// 3 (0x006A9D70): version 10.
uint8_t __fastcall Write(char* serializable, void*, void* stream)
{
    static bool logged = false;
    FirstCall("department Write", logged);
    char* const self = serializable - dept::kSerializable;
    const auto writeBool = reinterpret_cast<WriteBoolFn>(addr::SC4Stream_WriteBool);
    const auto writeObject = reinterpret_cast<WriteObjectFn>(addr::SC4Stream_WriteReference);
    Virtual<uint8_t>(stream, 0x1C, static_cast<uint16_t>(10));
    writeBool(stream, At<uint8_t>(self, dept::kInitialised));
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, dept::kId));
    Virtual<uint8_t>(stream, 0x3C, self + dept::kName);
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, dept::kGroup));
    Virtual<uint8_t>(stream, 0x30, At<float>(self, dept::kFunding));
    Virtual<uint8_t>(stream, 0x30, At<float>(self, dept::kMaxFunding));
    Virtual<uint8_t>(stream, 0x28, At<int64_t>(self, dept::kSpending));
    writeBool(stream, At<uint8_t>(self, dept::kFixed));
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, dept::kLines + 4));
    char* const lines = Header(self, dept::kLines);
    for (char* node = At<char*>(lines, 0x8); node != lines; node = MapNext(node))
        writeObject(stream, At<void*>(node, 0x14));
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, dept::kLocal + 4));
    char* const local = Header(self, dept::kLocal);
    for (char* node = At<char*>(local, 0x8); node != local; node = MapNext(node)) {
        writeObject(stream, At<void*>(node, 0x10));
        Virtual<uint8_t>(stream, 0x24, At<uint32_t>(node, 0x14));
    }
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, dept::kNameInstance));
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, dept::kNameGroup));
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// 4 (0x006AC1D0): versions up to 10 (before 10 the fundings were in
// percent).
uint8_t __fastcall Read(char* serializable, void*, void* stream)
{
    static bool logged = false;
    FirstCall("department Read", logged);
    char* const self = serializable - dept::kSerializable;
    const auto readBool = reinterpret_cast<ReadBoolFn>(addr::SC4Stream_ReadBool);
    const auto readObject = reinterpret_cast<ReadObjectFn>(addr::SC4Stream_ReadReference);
    uint16_t version = 0;
    Virtual<uint8_t>(stream, 0x1C, &version);
    if (version > 10)
        return 0;
    readBool(stream, self + dept::kInitialised);
    Virtual<uint8_t>(stream, 0x24, self + dept::kId);
    Virtual<uint8_t>(stream, 0x3C, self + dept::kName);
    Virtual<uint8_t>(stream, 0x24, self + dept::kGroup);
    float value = 0.0f;
    for (const std::size_t offset : {dept::kFunding, dept::kMaxFunding}) {
        Virtual<uint8_t>(stream, 0x30, &value);
        At<float>(self, offset) =
            version >= 10 ? value : static_cast<float>(static_cast<double>(value) * static_cast<double>(0.01f));
    }
    Virtual<uint8_t>(stream, 0x28, self + dept::kSpending);
    readBool(stream, self + dept::kFixed);
    uint32_t count = 0;
    Virtual<uint8_t>(stream, 0x24, &count);
    for (uint32_t i = 0; i < count; ++i) {
        void* item = nullptr;
        readObject(stream, 0xAA313C9F, &item, 0);
        const uint32_t id = Virtual<uint32_t>(item, lslot::kGetId);
        *LineAt(self, &id) = item;
    }
    Virtual<uint8_t>(stream, 0x24, &count);
    for (uint32_t i = 0; i < count; ++i) {
        Key key{};
        readObject(stream, 0x25216283, &key.holder, 0);
        Virtual<uint8_t>(stream, 0x24, &key.purpose);
        InsertLocal(self, key);
    }
    Virtual<uint8_t>(stream, 0x24, self + dept::kNameInstance);
    Virtual<uint8_t>(stream, 0x24, self + dept::kNameGroup);
    const uint32_t instance = At<uint32_t>(self, dept::kNameInstance);
    const uint32_t group = At<uint32_t>(self, dept::kNameGroup);
    if (instance && group)
        AssignText(self + dept::kName + 4, group, instance);
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

const Slot kSaveSlots[] = {
    OPENSC4_SLOT(3, 0x006A9D70, Write),
    OPENSC4_SLOT(4, 0x006AC1D0, Read),
};

#undef OPENSC4_SLOT

bool InstallSave(registry::Mode)
{
    return SwapSlots("department budget save", addr::cSC4DepartmentBudget_vftable_4, kSaveSlots,
                     sizeof(kSaveSlots) / sizeof(kSaveSlots[0]));
}

const bool g_registeredSave = registry::Register({
    "simulators.budget.department_save",
    "simulators/budget",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4DepartmentBudget_vftable_4,
    "Our department budgets' save record: Write (version 10), Read (up to 10) (docs/budget.md)",
    false,
    InstallSave,
});

// --- The helpers as detours -----------------------------------------------------

void* g_originalSetFunding = nullptr;
void* g_originalShare = nullptr;
void* g_originalRecount = nullptr;
void* g_originalAnnounce = nullptr;
void* g_originalNeeded = nullptr;
void* g_originalShutdown = nullptr;

bool InstallHelpers(registry::Mode)
{
    const addr::Entry* setFunding = addr_check::Find(addr::cSC4DepartmentBudget_SetFunding);
    const addr::Entry* share = addr_check::Find(addr::cSC4DepartmentBudget_ShareLocalFunding);
    const addr::Entry* recount = addr_check::Find(addr::cSC4DepartmentBudget_RecountLocalLine);
    const addr::Entry* announce = addr_check::Find(addr::cSC4DepartmentBudget_Announce);
    const addr::Entry* needed = addr_check::Find(addr::cSC4DepartmentBudget_AnnounceNeededFunding);
    const addr::Entry* shutdown = addr_check::Find(addr::cSC4DepartmentBudget_Shutdown);
    return game::IsBuild641() && setFunding && share && recount && announce && needed && shutdown &&
           hook::Install(*setFunding, reinterpret_cast<void*>(&OurSetFunding), &g_originalSetFunding) &&
           hook::Install(*share, reinterpret_cast<void*>(&OurShareLocalFunding), &g_originalShare) &&
           hook::Install(*recount, reinterpret_cast<void*>(&OurRecountLocalLine), &g_originalRecount) &&
           hook::Install(*announce, reinterpret_cast<void*>(&OurAnnounce), &g_originalAnnounce) &&
           hook::Install(*needed, reinterpret_cast<void*>(&OurAnnounceNeededFunding), &g_originalNeeded) &&
           hook::Install(*shutdown, reinterpret_cast<void*>(&OurShutdown), &g_originalShutdown);
}

const bool g_registeredHelpers = registry::Register({
    "simulators.budget.department_helpers",
    "simulators/budget",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4DepartmentBudget_SetFunding,
    "Our department budget helpers: the funding setter, the local funding shares, a local line's expense, the two "
    "announcements, Shutdown (docs/budget.md)",
    false,
    InstallHelpers,
});

} // namespace

void AnnounceNeededFunding(void* department)
{
    using Fn = void(__thiscall*)(void* self);
    reinterpret_cast<Fn>(addr::cSC4DepartmentBudget_AnnounceNeededFunding)(department);
}

} // namespace opensc4::budget_sim
