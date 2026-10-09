// Our cSC4NeighborsSimulator (build 641), third part: neighbor deals
// (docs/neighbors.md, "Deals"): proposing and adding deals, their price and
// largest amount, the automatic proposals and the commodity statistics the
// region view keeps for each city. Commodities: 1 power, 2 water, 3
// garbage; deal types 0x4773FFE0 to E5 (even: we buy, odd: we sell).
#include "core/neighbors_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstdint>
#include <cstring>

namespace opensc4::neighbors_sim {
namespace {

constexpr uint32_t kFirstDeal = 0x4773FFE0;

using ListFn = void(__thiscall*)(void* list);
using AllocateNodesFn = ListNode*(__thiscall*)(void* list, uint32_t count, uint32_t zero);
using CtorFn = void*(__thiscall*)(void* object);
using NewFn = void*(__cdecl*)(std::size_t size);
using GetFloatFn = uint8_t(__cdecl*)(void* holder, uint32_t property, float* value);
using GetDoubleFn = uint8_t(__cdecl*)(void* holder, uint32_t property, double* value);
using EvaluateFn = double(__thiscall*)(void* curve, float x);
using CurveLoadFn = void(__thiscall*)(void* curve, void* property);
using EraseFn = void*(__thiscall*)(void* list, ListNode** out, ListNode* node);
using MakeDealFn = uint8_t(__thiscall*)(void* self, uint32_t type, int32_t amount, void* city, void* list,
                                        uint8_t now);
using DateFn = void*(__thiscall*)(void* date, uint32_t day);
using DateCopyFn = void*(__thiscall*)(void* date, const void* other);
using DateFieldFn = uint32_t(__thiscall*)(const void* face);
using DateDtorFn = void(__thiscall*)(void* date);
using RandomCtorFn = void*(__thiscall*)(void* random, uint32_t seed);
using UniformFn = double(__thiscall*)(void* random, double low, double high);

const auto GetFloat = reinterpret_cast<GetFloatFn>(addr::SC4_GetPropertyFloat);

void* Region() { return Virtual<void*>(Global<void*>(addr::SC4_Service43C94), 0x90); }

// The tuning exemplar's property holder through slot 37 (its slot 6).
void* TuningHolder(void* self) { return Virtual<void*>(Virtual<void*>(self, 0x94), 0x18); }

// The game's date (16 bytes; its cIGZDate at +8).
struct Date {
    const void* vtable;
    uint32_t day;
    const void* face;
    uint32_t zero;
};

// A node from 0x00420FD0 (the 0x00B20D58 allocator) at the end of a deal
// list, with the deal referenced.
void AppendDeal(void* list, void* deal)
{
    ListNode* const head = *static_cast<ListNode**>(list);
    ListNode* const node = reinterpret_cast<AllocateNodesFn>(addr::SC4List_AllocateNodes1)(list, 1, 0);
    node->value = deal;
    if (deal)
        Virtual<uint32_t>(deal, 0x4);
    ListNode* const last = head->prev;
    node->next = head;
    node->prev = last;
    last->next = node;
    head->prev = node;
}

// The deals handed to every established adjacent city (slots 69 and 71 of
// each, last first), then to ours; the dirty flags cleared.
void ShareDeals(void* self, void* cities)
{
    Virtual<void>(self, 0x50, &cities, 0u);
    void* const ours = Region();
    auto* const head = static_cast<ListNode*>(cities);
    while (head->next != head) {
        ListNode* const node = head->prev;
        if (void* const city = node->value) {
            Virtual<void>(city, 0x114, static_cast<char*>(self) + field::kDeals);
            Virtual<void>(city, 0x11C);
        }
        node->prev->next = node->next;
        node->next->prev = node->prev;
        if (node->value)
            Virtual<uint32_t>(node->value, 0x8);
        Virtual<void>(NodeAllocatorAt(addr::SC4List_NodeAllocator4), 0x10, node);
    }
    Virtual<void>(ours, 0x114, static_cast<char*>(self) + field::kDeals);
    Virtual<void>(ours, 0x11C);
    At<uint8_t>(self, field::kDealsDirty) = 0;
    At<uint8_t>(self, 0x3D) = 0;
    reinterpret_cast<ListFn>(addr::SC4List_Destroy4)(&cities);
}

void* NewCityList()
{
    auto* const sentinel = static_cast<ListNode*>(Virtual<void*>(NodeAllocatorAt(addr::SC4List_NodeAllocator4), 0xC, 0xCu));
    sentinel->next = sentinel;
    sentinel->prev = sentinel;
    return sentinel;
}

void* NewDealList()
{
    auto* const sentinel = static_cast<ListNode*>(Virtual<void*>(NodeAllocatorAt(addr::SC4List_NodeAllocator), 0xC, 0xCu));
    sentinel->next = sentinel;
    sentinel->prev = sentinel;
    return sentinel;
}

// --- Deals ------------------------------------------------------------------

// 24 (0x006CF780): a new deal of a type with a city, over a suitable
// connection (slot 23): the region tiles of both sides, the type, amount 0.
uint8_t __fastcall ProposeDeal(void* self, void*, uint32_t type, void* city, void** deal)
{
    static bool logged = false;
    FirstCall("ProposeDeal", logged);
    void* made = reinterpret_cast<NewFn>(addr::SC4_operator_new)(0x80);
    if (made)
        made = reinterpret_cast<CtorFn>(addr::cSC4NeighborDeal_cSC4NeighborDeal)(made);
    if (made)
        Virtual<uint32_t>(made, 0x4);
    void* const connection = Virtual<void*>(self, 0x5C, type, city);
    if (!connection) {
        if (made)
            Virtual<uint32_t>(made, 0x8);
        return 0;
    }
    int32_t x = 0, z = 0;
    Virtual<void>(connection, 0x20, &x, &z);
    Virtual<void>(made, 0x14, x, z);
    Virtual<void>(Region(), 0x14, &x, &z);
    Virtual<void>(made, 0xC, x, z);
    Virtual<void>(made, 0x24, type);
    Virtual<void>(made, 0x2C, 0);
    *deal = made;
    Virtual<uint32_t>(made, 0x4);
    Virtual<uint32_t>(made, 0x8);
    return 1;
}

// 26 (0x006D2870): a deal added (flag 0x80 off) and shared.
uint8_t __fastcall AddDeal(void* self, void*, void* deal)
{
    static bool logged = false;
    FirstCall("AddDeal", logged);
    Virtual<void>(deal, 0x94, 0x80u, 0u);
    Virtual<uint32_t>(deal, 0x4);
    AppendDeal(static_cast<char*>(self) + field::kDeals, deal);
    Virtual<uint32_t>(deal, 0x8);
    ShareDeals(self, NewCityList());
    return 1;
}

// 0x006D0520: a proposed deal (slot 24) with an amount, added now if
// asked, running for a random number of years between the tuning's two
// (0xAA46934B, 1; 0xEA46934F, 5) from today; into the list.
uint8_t __fastcall MakeDeal(void* self, void*, uint32_t type, int32_t amount, void* city, void* list, uint8_t now)
{
    static bool logged = false;
    FirstCall("MakeDeal", logged);
    void* deal = nullptr;
    if (!Virtual<uint8_t>(self, 0x60, type, city, &deal)) {
        if (deal)
            Virtual<uint32_t>(deal, 0x8);
        return 0;
    }
    if (now) {
        Virtual<void>(deal, 0x94, 0x80u, 0u);
        AppendDeal(static_cast<char*>(self) + field::kDeals, deal);
    }
    Virtual<void>(deal, 0x2C, amount);
    float low = 1.0f, high = 5.0f;
    void* const holder = TuningHolder(self);
    GetFloat(holder, 0xAA46934B, &low);
    GetFloat(holder, 0xEA46934F, &high);
    if (low > high)
        high = low;
    uint32_t& guard = Global<uint32_t>(addr::NeighborDeal_RandomGuard);
    void* const random = reinterpret_cast<void*>(addr::NeighborDeal_Random);
    if (!(guard & 1)) {
        guard |= 1;
        reinterpret_cast<RandomCtorFn>(addr::SC4Random_Construct)(random, 0xFFFFFFFFu);
    }
    const float years = static_cast<float>(reinterpret_cast<UniformFn>(addr::cRZRandom_Uniform)(
        random, static_cast<double>(low), static_cast<double>(high) + Global<double>(addr::NeighborTuning_YearsSlack)));
    Date start{}, end{};
    reinterpret_cast<DateFn>(addr::cRZDate_FromDay)(&start, Virtual<uint32_t>(Global<void*>(addr::SC4_SimulatorService), 0x1C));
    const float rounded = static_cast<float>(static_cast<double>(years) - Global<float>(addr::SC4_FloatHalf) +
                                             Global<float>(addr::SC4_FloatRoundMagic));
    uint32_t bits;
    std::memcpy(&bits, &rounded, sizeof bits);
    int32_t whole = static_cast<int32_t>(bits + 0xB4C00000u);
    if (static_cast<double>(whole + 1) == static_cast<double>(years))
        whole += 1;
    const uint32_t day = reinterpret_cast<DateFieldFn>(addr::cRZDate_Day)(&start.face);
    reinterpret_cast<DateFn>(addr::cRZDate_cRZDate)(&end, day + whole * 365);
    Virtual<void>(deal, 0x74, &start.face);
    Virtual<void>(deal, 0x64, &start.face);
    Virtual<void>(deal, 0x6C, &end.face);
    AppendDeal(list, deal);
    reinterpret_cast<DateDtorFn>(addr::cRZDate_Destroy)(&end);
    reinterpret_cast<DateDtorFn>(addr::cRZDate_Destroy)(&start);
    if (deal)
        Virtual<uint32_t>(deal, 0x8);
    return 1;
}

// The amount of a deal of this type with the city now (0 for none).
double CurrentAmount(void* deals)
{
    auto* const head = static_cast<ListNode*>(deals);
    if (head->next == head)
        return 0.0;
    return static_cast<double>(Virtual<int32_t>(head->next->value, 0x28));
}

// The tuning's price of a commodity (and the deal's commodity), or 1 and
// none for other types; `sells` false for the types we buy.
uint32_t Commodity(uint32_t type, void* holder, float* price, bool* sells)
{
    *sells = true;
    switch (type - kFirstDeal) {
    case 0:
        *sells = false;
        [[fallthrough]];
    case 1:
        GetFloat(holder, 0xEA46932C, price);
        return 1;
    case 2:
        *sells = false;
        [[fallthrough]];
    case 3:
        GetFloat(holder, 0x6A469337, price);
        return 2;
    case 4:
        *sells = false;
        [[fallthrough]];
    case 5:
        GetFloat(holder, 0x8A46933D, price);
        return 3;
    default:
        return 0;
    }
}

// 35 (0x006D3250): the month's price of an amount of a deal with a city.
// The seller's spare supply and the buyer's unmet demand go through the
// price curve (+0x2C) and set the price between the buyer's cost (slot 62)
// times the tuning's low and high factors (0x6A469342 + 1, 0xCA469346 + 1).
// In ST0 (the game returns a double here, not an integer).
double __fastcall GetDealCost(void* self, void*, uint32_t type, double amount, void* city)
{
    static bool logged = false;
    FirstCall("GetDealCost", logged);
    void* const ours = Region();
    void* const holder = Virtual<void*>(At<void*>(self, field::kTuning), 0x1C);
    float scale = 1.0f, price = 0.0f;
    GetFloat(holder, 0xEA4AD2FF, &scale);
    if (!(amount >= Global<double>(addr::NeighborTuning_Tiny)))
        return Global<double>(addr::SC4_DoubleZero);
    bool sells = true;
    uint32_t commodity = Commodity(type, holder, &price, &sells);
    if (!commodity)
        commodity = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(self));
    void* deals = NewDealList();
    Virtual<void>(self, 0x74, type, city, &deals);
    const double existing = CurrentAmount(deals);
    void* seller = city;
    void* buyer = ours;
    if (!sells) {
        seller = ours;
        buyer = city;
    }
    double t = Virtual<double>(seller, 0xF0, commodity);
    const double sellerSupply = Virtual<double>(seller, 0xD8, commodity) + t;
    t = Virtual<double>(buyer, 0xF0, commodity);
    const double buyerSupply = Virtual<double>(buyer, 0xD8, commodity) + t - existing;
    t = Virtual<double>(seller, 0xE8, commodity);
    const double sellerDemand = Virtual<double>(seller, 0xE0, commodity) + t - existing;
    t = Virtual<double>(buyer, 0xE8, commodity);
    double buyerDemand = Virtual<double>(buyer, 0xE0, commodity) + t;
    price = static_cast<float>(static_cast<double>(price) / scale + Global<float>(addr::SC4_FloatOneA81228));
    const double supply = sellerSupply >= price ? sellerSupply : static_cast<double>(price);
    const double spare = Global<double>(addr::SC4_DoubleOne) - (supply - sellerDemand);
    if (!(buyerDemand >= Global<double>(addr::NeighborTuning_Tiny)))
        buyerDemand = Global<double>(addr::NeighborTuning_Tiny);
    if (!(amount >= Global<double>(addr::NeighborTuning_Tiny)))
        amount = Global<double>(addr::NeighborTuning_Tiny);
    double high = Global<double>(addr::SC4_DoubleOne);
    double low = Global<double>(addr::SC4_DoubleOne);
    const auto GetDouble = reinterpret_cast<GetDoubleFn>(addr::SC4_GetPropertyDouble);
    GetDouble(holder, 0xCA469346, &high);
    high = high + Global<double>(addr::SC4_DoubleOne);
    GetDouble(holder, 0x6A469342, &low);
    low = low + Global<double>(addr::SC4_DoubleOne);
    const double cost = Virtual<double>(buyer, 0xF8, commodity);
    const double bottom = low * cost;
    const double top = cost * high;
    void* const curve = static_cast<char*>(self) + field::kCurve;
    const auto Evaluate = reinterpret_cast<EvaluateFn>(addr::SC4Curve_Evaluate);
    const double first = Evaluate(curve, static_cast<float>(spare / amount));
    const double second = Evaluate(curve, static_cast<float>(((buyerDemand - amount) - buyerSupply) / buyerDemand));
    double unit = (top - bottom) * (second + first) * Global<double>(addr::SC4_DoubleHalf) + bottom;
    if (!(unit >= bottom))
        unit = bottom;
    if (unit > top)
        unit = top;
    reinterpret_cast<ListFn>(addr::SC4List_Destroy)(&deals);
    return unit * amount;
}

// 36 (0x006D3620): the most of a commodity a deal with a city can carry:
// the smaller of the seller's spare supply (above the tuning's price, times
// its scale) and the buyer's unmet demand, both counting the deal there is;
// at least 0. In ST0.
double __fastcall GetDealMaxAmount(void* self, void*, uint32_t type, void* city)
{
    static bool logged = false;
    FirstCall("GetDealMaxAmount", logged);
    void* const ours = Region();
    if (!city)
        return Global<double>(addr::SC4_DoubleZero);
    void* const holder = TuningHolder(self);
    float scale = 1.0f, price = 0.0f;
    GetFloat(holder, 0xEA4AD2FF, &scale);
    bool sells = true;
    uint32_t commodity = Commodity(type, holder, &price, &sells);
    if (!commodity)
        commodity = 1;
    void* deals = NewDealList();
    Virtual<void>(self, 0x74, type, city, &deals);
    const double existing = CurrentAmount(deals);
    void* seller = city;
    void* buyer = ours;
    if (!sells) {
        seller = ours;
        buyer = city;
    }
    double t = Virtual<double>(seller, 0xF0, commodity);
    const double supply = Virtual<double>(seller, 0xD8, commodity) + t;
    const double floorPrice = static_cast<double>(price) / scale + Global<float>(addr::SC4_FloatOneA81228);
    price = static_cast<float>(floorPrice);
    const double top = supply >= floorPrice ? supply : static_cast<double>(price);
    const double scaled = static_cast<double>(scale) * top;
    t = Virtual<double>(seller, 0xE8, commodity);
    const double room = scaled - (Virtual<double>(seller, 0xE0, commodity) + t - existing);
    t = Virtual<double>(buyer, 0xE8, commodity);
    const double buyerDemand = Virtual<double>(buyer, 0xE0, commodity) + t;
    t = Virtual<double>(buyer, 0xF0, commodity);
    const double need = buyerDemand - (Virtual<double>(buyer, 0xD8, commodity) + t - existing);
    double most = need < room ? need : room;
    if (!(most >= Global<double>(addr::SC4_DoubleZero)))
        most = Global<double>(addr::SC4_DoubleZero);
    reinterpret_cast<ListFn>(addr::SC4List_Destroy)(&deals);
    return most;
}

// One record of ProposeDeals' table.
struct DealKinds {
    uint32_t buy;
    uint32_t sell;
    uint32_t commodity;
    float smallest;
};

// The checks before ProposeDeals offers a deal of `type`: the city has a
// connection for it and no such deal; the tuning's smallest amount fits;
// the paying side's budget (slot 50) is above the floor and the price, and
// above the price times the margin.
void Offer(void* self, uint32_t type, void* city, void* payer, const DealKinds& kinds, float floor, float margin,
           void*& deals, void* list, uint8_t now)
{
    if (!Virtual<void*>(self, 0x5C, type, city))
        return;
    Virtual<void>(self, 0x74, type, city, &deals);
    auto* const head = static_cast<ListNode*>(deals);
    if (head->next != head)
        return;
    const double budget = Virtual<double>(payer, 0xC8);
    const double most = Virtual<double>(self, 0x90, type, city);
    const double smallest = kinds.smallest;
    const double amount = smallest < most ? smallest : most;
    if (!(amount >= static_cast<double>(kinds.smallest)))
        return;
    const double cost = Virtual<double>(self, 0x8C, type, amount, city);
    if (!(static_cast<double>(floor) < budget))
        return;
    if (!(budget > cost))
        return;
    if (!(static_cast<double>(margin) * cost < budget))
        return;
    reinterpret_cast<MakeDealFn>(addr::cSC4NeighborsSimulator_MakeDeal)(self, type, 0, city, list, now);
}

// 25 (0x006D21A0): unless +0xC is clear, the commodity statistics brought
// up to date (slot 34), then for each established adjacent city and each
// commodity, a deal to buy and one to sell proposed into the list where the
// checks pass; shared if any and asked.
uint8_t __fastcall ProposeDeals(void* self, void*, void* list, uint8_t now)
{
    static bool logged = false;
    FirstCall("ProposeDeals", logged);
    if (!At<uint8_t>(self, field::kLoaded))
        return 1;
    Virtual<void>(self, 0x88, Global<void*>(addr::SC4_City));
    reinterpret_cast<ListFn>(addr::SC4List_Clear)(list);
    void* const ours = Region();
    DealKinds table[4] = {{kFirstDeal, kFirstDeal + 1, 1, 0.0f},
                          {kFirstDeal + 2, kFirstDeal + 3, 2, 0.0f},
                          {kFirstDeal + 4, kFirstDeal + 5, 3, 0.0f},
                          {0, 0, 0, 0.0f}};
    void* const holder = TuningHolder(self);
    GetFloat(holder, 0xEA46932C, &table[0].smallest);
    GetFloat(holder, 0x6A469337, &table[1].smallest);
    GetFloat(holder, 0x8A46933D, &table[2].smallest);
    float unused = 1.0f, floor = 0.0f, margin = 1.0f;
    GetFloat(holder, 0xCA469346, &unused);
    unused = static_cast<float>(static_cast<double>(unused) + Global<float>(addr::SC4_FloatOneA81228));
    GetFloat(holder, 0xEA4AE9EF, &floor);
    GetFloat(holder, 0xCA46931F, &margin);
    void* cities = NewCityList();
    Virtual<void>(self, 0x50, &cities, 0u);
    for (auto* head = static_cast<ListNode*>(cities); head->next != head;) {
        void* const city = head->next->value;
        for (const DealKinds* kinds = table; kinds->buy; ++kinds) {
            void* bought = NewDealList();
            void* sold = NewDealList();
            Offer(self, kinds->buy, city, ours, *kinds, floor, margin, bought, list, now);
            Offer(self, kinds->sell, city, city, *kinds, floor, margin, sold, list, now);
            reinterpret_cast<ListFn>(addr::SC4List_Clear)(&sold);
            Virtual<void>(NodeAllocatorAt(addr::SC4List_NodeAllocator), 0x10, sold);
            reinterpret_cast<ListFn>(addr::SC4List_Clear)(&bought);
            Virtual<void>(NodeAllocatorAt(addr::SC4List_NodeAllocator), 0x10, bought);
        }
        ListNode* out = nullptr;
        reinterpret_cast<EraseFn>(addr::SC4List_EraseReleasing4)(&cities, &out, head->next);
    }
    auto* const proposed = *static_cast<ListNode**>(list);
    if (proposed->next != proposed && now) {
        At<uint8_t>(self, field::kDealsDirty) = 1;
        ShareDeals(self, NewCityList());
    }
    reinterpret_cast<ListFn>(addr::SC4List_Destroy4)(&cities);
    return 1;
}

// One commodity's statistics into the region view: what the city makes
// (`made`), its capacity (`capacity`), our demand and supply (slots 56,
// 54), and the ratio of the budget item's monthly cost (with the deals
// that buy it) to the demand.
struct Commodities {
    double made;
    double capacity;
};

void Publish(void* ours, uint32_t commodity, double made, double capacity, double demand, double supply,
             double ratio)
{
    Virtual<void>(ours, 0xEC, commodity, made);
    Virtual<void>(ours, 0xF4, commodity, capacity);
    Virtual<void>(ours, 0xDC, commodity, supply);
    Virtual<void>(ours, 0xE4, commodity, demand);
    Virtual<void>(ours, 0xFC, commodity, ratio);
}

int32_t Truncate(double value) { return static_cast<int32_t>(static_cast<int64_t>(value)); }

// Unsigned int as the game reads it (FILD, + 2^32 when negative).
double Unsigned(int32_t value)
{
    double result = value;
    if (value < 0)
        result += Global<float>(addr::SC4_FloatTwoTo32);
    return result;
}

// 34 (0x006D2A20): for our own city only: its funds (budget slots 6 and
// 16) and the three commodities' statistics into the region view (slots
// 51, 53, 55-63), some also as city properties; the deals shared.
void __fastcall UpdateCommodityStats(void* self, void*, void* city)
{
    static bool logged = false;
    FirstCall("UpdateCommodityStats", logged);
    if (!city || city != Global<void*>(addr::SC4_City))
        return;
    void* const ours = Region();
    Virtual<void>(ours, 0xCC, static_cast<double>(Virtual<int64_t>(Virtual<void*>(city, 0xD0), 0x18)));
    Virtual<void>(ours, 0xD4, static_cast<double>(Virtual<int64_t>(Virtual<void*>(city, 0xD0), 0x40)));
    void* const holder = TuningHolder(self);
    void* const properties = Virtual<void*>(city, 0x80);
    float years = 0.0f;
    GetFloat(holder, 0xEA46934F, &years);
    years = static_cast<float>(static_cast<double>(years) * Global<float>(addr::NeighborTuning_Months));
    Date today{}, earlier{};
    reinterpret_cast<DateFn>(addr::cRZDate_FromDay)(&today, Virtual<uint32_t>(Global<void*>(addr::SC4_SimulatorService), 0x1C));
    reinterpret_cast<DateCopyFn>(addr::cRZDate_Copy)(&earlier, &today);
    Virtual<void>(&earlier.face, 0xA0, 0x5Bu);
    double garbage = 0.0, water = 0.0, power = 0.0;
    ListNode* const deals = Head(self, field::kDeals);
    for (ListNode* node = deals->next; node != deals;) {
        void* const deal = node->value;
        node = node->next;
        const float cost = static_cast<float>(Virtual<int64_t>(deal, 0x58));
        switch (Virtual<uint32_t>(deal, 0x20)) {
        case kFirstDeal:
            power = cost + power;
            break;
        case kFirstDeal + 2:
            water = cost + water;
            break;
        case kFirstDeal + 4:
            garbage = cost + garbage;
            break;
        default:
            break;
        }
    }
    ShareDeals(self, NewCityList());
    void* const budget = Global<void*>(addr::SC4_BudgetSimulator);

    // Power.
    void* const powerSim = Global<void*>(addr::SC4_Service43DA0);
    double made = static_cast<double>(Virtual<int32_t>(powerSim, 0x30));
    double capacity = static_cast<double>(Virtual<int32_t>(powerSim, 0x2C));
    double demand = Virtual<double>(ours, 0xE0, 1u);
    double supply = Virtual<double>(ours, 0xD8, 1u);
    double ratio = Global<double>(addr::NeighborTuning_DefaultRatio);
    void* const powerItem = Virtual<void*>(budget, 0x70, 0x8910BC8Au);
    if (powerItem)
        ratio = (static_cast<double>(Virtual<int64_t>(powerItem, 0x3C)) + power) /
                (demand + made + Global<double>(addr::SC4_DoubleOne));
    Publish(ours, 1, made, capacity, demand, supply, ratio);
    Virtual<void>(properties, 0x30, 0xAA11F837u, Truncate(demand));
    Virtual<void>(properties, 0x30, 0xAA11F83Au, Truncate(supply));

    // Water.
    double recycled = 0.0;
    if (void* const item = Virtual<void*>(powerItem, 0x50, 0x1F470000u))
        recycled = static_cast<double>(Virtual<int64_t>(item, 0x28));
    void* const waterSim = Global<void*>(addr::SC4_Service43D94);
    made = static_cast<double>(Virtual<int32_t>(waterSim, 0x38));
    capacity = static_cast<double>(Virtual<int32_t>(waterSim, 0x5C));
    demand = Virtual<double>(ours, 0xE0, 2u);
    supply = Virtual<double>(ours, 0xD8, 2u);
    ratio = Global<double>(addr::NeighborTuning_DefaultRatio);
    if (void* const item = Virtual<void*>(budget, 0x70, 0xAA4014B4u))
        ratio = (static_cast<double>(Virtual<int64_t>(item, 0x3C)) + water) /
                (demand + made + Global<double>(addr::SC4_DoubleOne));
    Publish(ours, 2, made, capacity, demand, supply, ratio);
    Virtual<void>(properties, 0x30, 0x0A14EBB5u, Truncate(demand));
    Virtual<void>(properties, 0x30, 0xCA14EBC6u, Truncate(supply));

    // Garbage.
    int32_t span[2] = {0, 0};
    Virtual<void>(Global<void*>(addr::SC4_Value43D28), 0x28, &span[1], &span[0]);
    const int32_t days = span[0] - span[1];
    void* const pollution = Global<void*>(addr::SC4_PollutionSimulator);
    float handled = static_cast<float>(Virtual<int64_t>(pollution, 0x98));
    handled = static_cast<float>(static_cast<double>(Virtual<int64_t>(pollution, 0x88)) + handled);
    handled = static_cast<float>(Unsigned(Virtual<int32_t>(pollution, 0x80)) + handled);
    made = (Unsigned(Virtual<int32_t>(pollution, 0x7C)) + handled) + Unsigned(days) / static_cast<double>(years);
    capacity = static_cast<double>(Virtual<int64_t>(pollution, 0x8C));
    demand = Virtual<double>(ours, 0xE0, 3u);
    supply = Virtual<double>(ours, 0xD8, 3u);
    ratio = Global<double>(addr::NeighborTuning_DefaultRatio);
    if (void* const item = Virtual<void*>(budget, 0x70, 0x6A42A8F5u)) {
        const double cost = static_cast<double>(Virtual<int64_t>(item, 0x3C));
        if (void* const property = Virtual<void*>(holder, 0x24, 0x4B61FFC9u))
            reinterpret_cast<CurveLoadFn>(addr::SC4Curve_LoadFromProperty)(static_cast<char*>(self) + field::kCurve,
                                                                         property);
        ratio = ((cost + recycled) + garbage) / (demand + made + Global<double>(addr::SC4_DoubleOne));
        ratio = reinterpret_cast<EvaluateFn>(addr::SC4Curve_Evaluate)(static_cast<char*>(self) + field::kCurve,
                                                                      static_cast<float>(ratio));
    }
    Publish(ours, 3, made, capacity, demand, supply, ratio);
    Virtual<void>(properties, 0x30, 0xEA11017Bu, Truncate(supply));
    Virtual<void>(properties, 0x30, 0xEA11017Eu, Truncate(demand));
    Virtual<void>(properties, 0x30, 0xEB54F32Eu, Truncate(capacity));
    Virtual<void>(properties, 0x30, 0x4B54F333u, Truncate(made));
    reinterpret_cast<DateDtorFn>(addr::cRZDate_Destroy)(&earlier);
    reinterpret_cast<DateDtorFn>(addr::cRZDate_Destroy)(&today);
}

// --- Installation -----------------------------------------------------------

struct Slot {
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(index, original, fn) {index, original, reinterpret_cast<void*>(&fn)}

const Slot kSlots[] = {
    OPENSC4_SLOT(24, 0x006CF780, ProposeDeal),
    OPENSC4_SLOT(25, 0x006D21A0, ProposeDeals),
    OPENSC4_SLOT(26, 0x006D2870, AddDeal),
    OPENSC4_SLOT(34, 0x006D2A20, UpdateCommodityStats),
    OPENSC4_SLOT(35, 0x006D3250, GetDealCost),
    OPENSC4_SLOT(36, 0x006D3620, GetDealMaxAmount),
};

#undef OPENSC4_SLOT

using MakeOursFn = uint8_t(__fastcall*)(void*, void*, uint32_t, int32_t, void*, void*, uint8_t);
MakeOursFn g_originalMake = nullptr;

bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    auto* const vtable = reinterpret_cast<std::uintptr_t*>(addr::cSC4NeighborsSimulator_vftable);
    for (const Slot& slot : kSlots) {
        if (vtable[slot.index] != slot.original) {
            log::Write("neighbors deals: slot %d holds %08X, expected %08X; not installing", slot.index,
                       static_cast<unsigned>(vtable[slot.index]), static_cast<unsigned>(slot.original));
            return false;
        }
    }
    const addr::Entry* make = addr_check::Find(addr::cSC4NeighborsSimulator_MakeDeal);
    if (!make || !hook::Install(*make, &MakeDeal, &g_originalMake))
        return false;
    DWORD old = 0;
    if (!VirtualProtect(vtable + 24, 13 * sizeof(*vtable), PAGE_READWRITE, &old))
        return false;
    for (const Slot& slot : kSlots)
        vtable[slot.index] = reinterpret_cast<std::uintptr_t>(slot.ours);
    VirtualProtect(vtable + 24, 13 * sizeof(*vtable), old, &old);
    return true;
}

const bool g_registered = registry::Register({
    "simulators.neighbors.deals",
    "simulators/neighbors",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4NeighborsSimulator_vftable,
    "Our neighbor deals: proposing, adding, prices, amounts, automatic offers, commodity statistics "
    "(docs/neighbors.md)",
    false,
    Install,
});

} // namespace
} // namespace opensc4::neighbors_sim
