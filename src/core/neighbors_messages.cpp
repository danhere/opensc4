// Our cSC4NeighborsSimulator (build 641), fourth part: messages, the daily
// and monthly passes over the deals, connection markers after terrain or
// network changes, Shutdown and the save record (docs/neighbors.md).
#include "core/neighbors_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstdint>

namespace opensc4::neighbors_sim {
namespace {

constexpr uint32_t kFirstDeal = 0x4773FFE0;
constexpr uint32_t kDealNews = 0xEA418D8F;
constexpr uint32_t kMarker = 0xCAA46595;

using ListFn = void(__thiscall*)(void* list);
using SelfFn = void(__thiscall*)(void* self);
using DailyFn = void(__thiscall*)(void* self, uint8_t share, uint8_t save);
using RectFn = void(__thiscall*)(void* self, const int32_t* rect);
using GetFloatFn = uint8_t(__cdecl*)(void* holder, uint32_t property, float* value);
using GetBoolFn = uint8_t(__cdecl*)(void* holder, uint32_t property, uint8_t* value);
using PostFn = void(__cdecl*)(uint32_t type, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e);
using PushFn = void(__thiscall*)(void* list, void* const* value);
using EraseFn = void*(__thiscall*)(void* list, ListNode** out, ListNode* node);
using DateFn = void*(__thiscall*)(void* date, uint32_t day);
using DateDefaultFn = void*(__thiscall*)(void* date);
using DateFieldFn = uint32_t(__thiscall*)(const void* face);
using DateDtorFn = void(__thiscall*)(void* date);
using CompareFn = uint32_t(__cdecl*)(void* a, void* b);
using NewFn = void*(__cdecl*)(std::size_t size);
using CtorFn = void*(__thiscall*)(void* object);
using FreeFn = void(__cdecl*)(void* block);
using StreamFn = uint8_t(__cdecl*)(void* stream, void* object);
using ReadReferenceFn = uint8_t(__cdecl*)(void* stream, uint32_t iid, void* object, uint32_t flags);

const auto GetFloat = reinterpret_cast<GetFloatFn>(addr::SC4_GetPropertyFloat);

void* Region() { return Virtual<void*>(Global<void*>(addr::SC4_Service43C94), 0x90); }

struct Date {
    const void* vtable;
    uint32_t day;
    const void* face;
    uint32_t zero;
};

void DefaultDate(Date* date) { reinterpret_cast<DateDefaultFn>(addr::cRZDate_Default)(date); }
void TodayDate(Date* date)
{
    reinterpret_cast<DateFn>(addr::cRZDate_FromDay)(date, Virtual<uint32_t>(Global<void*>(addr::SC4_SimulatorService), 0x1C));
}
void DropDate(Date* date) { reinterpret_cast<DateDtorFn>(addr::cRZDate_Destroy)(date); }
uint32_t Day(Date* date) { return reinterpret_cast<DateFieldFn>(addr::cRZDate_Day)(&date->face); }

// 0x006CE6A0: slot 27 of a's date (a comparison) with b's, or with none;
// true when it answers 0.
uint32_t __cdecl DateNotAfter(void* a, void* b)
{
    void* const face = static_cast<char*>(a) + 8;
    void* const other = b ? static_cast<char*>(b) + 8 : nullptr;
    return Virtual<uint32_t>(face, 0x6C, other) ? 0 : 1;
}

// 0x006CF5C0 (its own convention: the deal in ECX, the arguments popped
// by the caller; called only from the monthly pass, so not hooked): deal
// news (message 0xEA418D8F with the code and the deal), posted or sent.
void Notify(void* deal, uint8_t post, uint32_t code)
{
    void* unknown = nullptr;
    if (!deal || Virtual<uint8_t>(deal, 0x0, 1u, &unknown)) {
        void* message = reinterpret_cast<NewFn>(addr::RZ_operator_new)(0x2C);
        if (message)
            message = reinterpret_cast<CtorFn>(addr::cGZMessage_cGZMessage)(message);
        if (message)
            Virtual<uint32_t>(message, 0x4);
        Virtual<void*>(message, 0x14, kDealNews);
        Virtual<void*>(message, 0x2C, code);
        Virtual<void*>(message, 0x84, unknown);
        void* const server = Global<void*>(addr::SC4_MessageServer);
        if (post)
            Virtual<uint8_t>(server, 0x10, message, 0u);
        else
            Virtual<uint8_t>(server, 0xC, message);
        Virtual<uint32_t>(message, 0x8);
    }
    if (unknown)
        Virtual<uint32_t>(unknown, 0x8);
}

// --- Daily, markers, monthly ------------------------------------------------

// 0x006D0A40 (the daily messages, with +0x3C and +0x3D): the deals handed
// to each established adjacent city and then ours (slot 69) when `share`,
// each saved (slot 71) when `save`; the flag of each cleared.
void __fastcall Daily(void* self, void*, uint8_t share, uint8_t save)
{
    static bool logged = false;
    FirstCall("Daily", logged);
    if (!share && !save)
        return;
    auto* const cities = static_cast<ListNode*>(Virtual<void*>(NodeAllocatorAt(addr::SC4List_NodeAllocator4), 0xC, 0xCu));
    cities->next = cities;
    cities->prev = cities;
    void* head = cities;
    Virtual<void>(self, 0x50, &head, 0u);
    void* const ours = Region();
    while (cities->next != cities) {
        ListNode* const node = cities->prev;
        if (void* const city = node->value) {
            if (share)
                Virtual<void>(city, 0x114, static_cast<char*>(self) + field::kDeals);
            if (save)
                Virtual<void>(city, 0x11C);
        }
        node->prev->next = node->next;
        node->next->prev = node->prev;
        if (node->value)
            Virtual<uint32_t>(node->value, 0x8);
        Virtual<void>(NodeAllocatorAt(addr::SC4List_NodeAllocator4), 0x10, node);
    }
    if (share)
        Virtual<void>(ours, 0x114, static_cast<char*>(self) + field::kDeals);
    if (save)
        Virtual<void>(ours, 0x11C);
    if (share)
        At<uint8_t>(self, field::kDealsDirty) = 0;
    if (save)
        At<uint8_t>(self, 0x3D) = 0;
    reinterpret_cast<ListFn>(addr::SC4List_Destroy4)(&head);
}

// 0x006D0BE0 (message 0x278E6003): the markers of the connections whose
// cell (clamped into the city) is in the rectangle back on the ground
// (terrain slot 18 at the cell; 15.5 m up for elevated kinds).
void __fastcall UpdateMarkers(void* self, void*, const int32_t* rect)
{
    static bool logged = false;
    FirstCall("UpdateMarkers", logged);
    void* const city = At<void*>(self, field::kCity);
    const int32_t lastX = Virtual<int32_t>(city, 0x174) - 1;
    const int32_t lastZ = Virtual<int32_t>(city, 0x178) - 1;
    ListNode* const connections = Head(self, field::kConnections);
    for (ListNode* node = connections->next; node != Head(self, field::kConnections); node = node->next) {
        void* const connection = node->value;
        int32_t x = 0, z = 0;
        Virtual<void>(connection, 0x18, &x, &z);
        if (x < 0)
            x = 0;
        else if (x > lastX)
            x = lastX;
        if (z < 0)
            z = 0;
        else if (z > lastZ)
            z = lastZ;
        if (x < rect[0] || z < rect[1] || x > rect[2] || z > rect[3])
            continue;
        float position[3];
        Virtual<void>(city, 0x184, x, z, &position[0], &position[2]);
        position[1] = static_cast<float>(Virtual<double>(Global<void*>(addr::SC4_Terrain), 0x48, x, z));
        const uint32_t type = Virtual<uint32_t>(connection, 0x14);
        if (type == 3 || type == 7 || type == 11)
            position[1] = static_cast<float>(static_cast<double>(position[1]) + Global<float>(addr::NeighborTuning_ElevatedHeight));
        auto* const list = static_cast<ListNode*>(Virtual<void*>(ListNodeAllocator(), 0xC, 0xCu));
        list->next = list;
        list->prev = list;
        void* head = list;
        const int32_t cell[4] = {x, z, x, z};
        Virtual<uint8_t>(Global<void*>(addr::SC4_Service43D10), 0x48, &head, cell);
        for (ListNode* item = list->next; item != list; item = item->next) {
            void* const occupant = item->value;
            uint8_t marker = 0;
            reinterpret_cast<GetBoolFn>(addr::SC4_GetPropertyBool)(Virtual<void*>(occupant, 0x18), kMarker, &marker);
            if (marker) {
                Virtual<uint8_t>(occupant, 0x24, position);
                Virtual<uint8_t>(Global<void*>(addr::SC4_NetworkService), 0x60, occupant, 1u);
            }
        }
        Virtual<uint8_t>(Global<void*>(addr::SC4_NetworkService), 0xC0, &head);
        reinterpret_cast<ListFn>(addr::SC4List_Clear2)(&head);
        Virtual<void>(ListNodeAllocator(), 0x10, head);
    }
}

// The deal's node queued (+0x24) to come out of the list, news `code`,
// amount 0.
void Retire(void* self, ListNode* node, void* deal, uint32_t code)
{
    reinterpret_cast<PushFn>(addr::SC4StlList_PushBack)(static_cast<char*>(self) + field::kPairs,
                                                       reinterpret_cast<void* const*>(&node));
    Notify(deal, 1, code);
    Virtual<void>(deal, 0x2C, 0);
}

// 0x006D0E50 (MonthPassed and deal news): unless already running (then the
// news is posted again), the commodity statistics (slot 34); unless the UI
// is off, each active deal's month: its state (slot 33), garbage sold
// above what the city can spare marked (0x20), ending deals retired with
// news, new deals started, suspended ones waiting out the tuning's days,
// paid deals renewed for their length if the payer can afford them (else
// 0x10), others flagged due at their end (0x40); then the retired deals
// out of the list and the statistics again; then the daily sharing.
void __fastcall Monthly(void* self, void*)
{
    static bool logged = false;
    FirstCall("Monthly", logged);
    if (!At<uint8_t>(self, field::kLoaded))
        return;
    if (At<uint8_t>(self, 0x3F)) {
        reinterpret_cast<PostFn>(addr::SC4_PostMessage)(kDealNews, 8, 0, 0, 0, 0);
        return;
    }
    At<uint8_t>(self, 0x3F) = 1;
    Virtual<void>(self, 0x88, Global<void*>(addr::SC4_City));
    if (!At<uint8_t>(self, field::kUIDisabled)) {
        void* const pending = static_cast<char*>(self) + field::kPairs;
        reinterpret_cast<ListFn>(addr::SC4StlList_Clear)(pending);
        void* const holder = Virtual<void*>(Virtual<void*>(self, 0x94), 0x18);
        float years = 0.0f;
        GetFloat(holder, 0xEA46934F, &years);
        years = static_cast<float>(static_cast<double>(years) * Global<float>(addr::NeighborTuning_Months));
        int32_t span[2] = {0, 0};
        Virtual<void>(Global<void*>(addr::SC4_Value43D28), 0x28, &span[0], &span[1]);
        const int32_t days = span[1] - span[0];
        void* const ours = Region();
        double garbageSupply = Virtual<double>(ours, 0xD8, 3u);
        const double extra = Virtual<double>(ours, 0xE8, 3u);
        const double demand = Virtual<double>(ours, 0xE0, 3u) + extra;
        double factor = Global<float>(addr::SC4_FloatOneA81228) /
                        (static_cast<double>(years) + Global<float>(addr::NeighborTuning_YearsBias));
        factor = Global<float>(addr::SC4_FloatOneA81228) - factor;
        double spanDays = days;
        if (days < 0)
            spanDays += Global<float>(addr::SC4_FloatTwoTo32);
        const double threshold = spanDays * factor + demand;
        for (ListNode* node = Head(self, field::kDeals)->next; node != Head(self, field::kDeals); node = node->next) {
            void* const deal = node->value;
            if (Virtual<uint8_t>(deal, 0x9C, 0x80u))
                continue;
            Virtual<void>(deal, 0x84);
            if (Virtual<uint32_t>(deal, 0x20) == kFirstDeal + 5 && garbageSupply > threshold) {
                garbageSupply = garbageSupply - static_cast<double>(Virtual<int32_t>(deal, 0x28));
                Virtual<void>(deal, 0x94, 0x20u, 1u);
            }
            if (Virtual<uint8_t>(deal, 0x9C, 0x3Cu)) {
                if (Virtual<uint8_t>(deal, 0x9C, 4u)) {
                    Retire(self, node, deal, 2);
                } else if (!Virtual<uint8_t>(deal, 0x9C, 0x200u)) {
                    Virtual<void>(deal, 0x94, 0x100u, 1u);
                    Virtual<void>(deal, 0x94, 0x38u, 0u);
                    Virtual<void>(deal, 0x7C, Virtual<uint32_t>(Global<void*>(addr::SC4_SimulatorService), 0x1C));
                } else if (Virtual<uint8_t>(deal, 0x9C, 8u)) {
                    Retire(self, node, deal, 3);
                } else if (Virtual<uint8_t>(deal, 0x9C, 0x10u)) {
                    Retire(self, node, deal, 4);
                } else if (Virtual<uint8_t>(deal, 0x9C, 0x20u)) {
                    Retire(self, node, deal, 5);
                }
                continue;
            }
            if (Virtual<uint8_t>(deal, 0x9C, 1u)) {
                if (Virtual<uint8_t>(deal, 0x8C)) {
                    Virtual<void>(deal, 0x74, Virtual<uint32_t>(Global<void*>(addr::SC4_SimulatorService), 0x1C));
                    Notify(deal, 1, 1);
                    At<uint8_t>(self, 0x3D) = 1;
                }
                continue;
            }
            if (!Virtual<uint8_t>(deal, 0x9C, 2u)) {
                Virtual<void>(deal, 0x88);
                continue;
            }
            Date end{}, today{};
            DefaultDate(&end);
            TodayDate(&today);
            Virtual<void>(deal, 0x74, &today.face);
            Virtual<void>(deal, 0x68, &end.face);
            const auto NotAfter = reinterpret_cast<CompareFn>(addr::cSC4NeighborsSimulator_DateNotAfter);
            if (Virtual<uint8_t>(deal, 0x9C, 0x100u)) {
                float wait = 0.0f;
                GetFloat(holder, 0x6B588FAD, &wait);
                Date resume{};
                DefaultDate(&resume);
                Virtual<void>(deal, 0x78, &resume.face);
                Virtual<void>(&resume.face, 0x9C, static_cast<int32_t>(static_cast<int64_t>(wait)));
                if (NotAfter(&today, &resume)) {
                    Virtual<void>(deal, 0x94, 0x100u, 0u);
                    Virtual<void>(deal, 0x94, 0x200u, 1u);
                }
                DropDate(&resume);
            } else if (Virtual<uint8_t>(deal, 0x9C, 0x200u)) {
                float first = 0.0f, wait = 0.0f;
                GetFloat(holder, 0x8B58C59E, &first);
                GetFloat(holder, 0x6B588FAD, &wait);
                Date resume{};
                DefaultDate(&resume);
                Virtual<void>(deal, 0x78, &resume.face);
                Virtual<void>(&resume.face, 0x9C,
                              static_cast<int32_t>(static_cast<int64_t>(static_cast<double>(wait) + first)));
                if (NotAfter(&today, &resume))
                    Virtual<void>(deal, 0x94, 0x200u, 0u);
                DropDate(&resume);
            } else if (Virtual<uint8_t>(deal, 0x98, 0x40u)) {
                Virtual<void>(deal, 0x94, 0x40u, 0u);
                void* payer = ours;
                const uint32_t type = Virtual<uint32_t>(deal, 0x20);
                if (type == kFirstDeal + 1 || type == kFirstDeal + 3 || type == kFirstDeal + 5)
                    payer = Virtual<void*>(deal, 0x1C);
                Virtual<void>(deal, 0x5C);
                const double cost = static_cast<double>(Virtual<int64_t>(deal, 0x58));
                const double budget = Virtual<double>(payer, 0xC8);
                if (!(budget >= cost)) {
                    Virtual<void>(deal, 0x94, 0x10u, 1u);
                } else {
                    Date start{}, finish{}, now{};
                    DefaultDate(&start);
                    DefaultDate(&finish);
                    TodayDate(&now);
                    Virtual<void>(deal, 0x74, &now.face);
                    Virtual<void>(deal, 0x60, &start.face);
                    Virtual<void>(deal, 0x68, &finish.face);
                    const uint32_t from = Day(&start);
                    const uint32_t length = Day(&finish) - from;
                    Virtual<void>(deal, 0x64, &now.face);
                    Date renewed{};
                    reinterpret_cast<DateFn>(addr::cRZDate_cRZDate)(&renewed, Day(&now) + length);
                    Virtual<void>(deal, 0x6C, &renewed.face);
                    DropDate(&renewed);
                    Notify(deal, 1, 7);
                    DropDate(&now);
                    DropDate(&finish);
                    DropDate(&start);
                }
            } else if (NotAfter(&today, &end)) {
                Virtual<void>(deal, 0x94, 0x40u, 1u);
                Notify(deal, 1, 6);
            }
            DropDate(&today);
            DropDate(&end);
        }
        auto* const queue = *static_cast<ListNode**>(pending);
        At<uint8_t>(self, field::kDealsDirty) = (At<uint8_t>(self, field::kDealsDirty) || queue->next != queue) ? 1 : 0;
        while (queue->next != queue) {
            ListNode* out = nullptr;
            reinterpret_cast<EraseFn>(addr::SC4List_EraseReleasing1)(static_cast<char*>(self) + field::kDeals, &out,
                                                                    static_cast<ListNode*>(queue->prev->value));
            ListNode* const last = queue->prev;
            last->prev->next = last->next;
            last->next->prev = last->prev;
            reinterpret_cast<FreeFn>(addr::SC4_Free)(last);
        }
        Virtual<void>(self, 0x88, Global<void*>(addr::SC4_City));
    }
    reinterpret_cast<DailyFn>(addr::cSC4NeighborsSimulator_Daily)(self, At<uint8_t>(self, field::kDealsDirty),
                                                                  At<uint8_t>(self, 0x3D));
    At<uint8_t>(self, 0x3F) = 0;
}

// --- DoMessage --------------------------------------------------------------

// cIGZMessageTarget2 slot 3 (0x006D42B0), on the part at +4.
uint8_t __fastcall DoMessage(void* part, void*, void* message)
{
    static bool logged = false;
    FirstCall("DoMessage", logged);
    void* const self = static_cast<char*>(part) - 0x4;
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    switch (type) {
    case 0x278E6003: {
        const uint32_t first = Virtual<uint32_t>(message, 0x24);
        const uint32_t second = Virtual<uint32_t>(message, 0x38);
        int32_t rect[4] = {static_cast<int32_t>(first >> 16) - 1, static_cast<int32_t>(second >> 16) - 1,
                           static_cast<int32_t>(first & 0xFFFF), static_cast<int32_t>(second & 0xFFFF)};
        void* const city = At<void*>(self, field::kCity);
        const int32_t lastX = Virtual<int32_t>(city, 0x174) - 1;
        const int32_t lastZ = Virtual<int32_t>(city, 0x178) - 1;
        if (rect[0] < 0)
            rect[0] = 0;
        if (rect[2] > lastX)
            rect[2] = lastX;
        if (rect[1] < 0)
            rect[1] = 0;
        if (rect[3] > lastZ)
            rect[3] = lastZ;
        reinterpret_cast<RectFn>(addr::cSC4NeighborsSimulator_UpdateMarkers)(self, rect);
        return 1;
    }
    case 0x26D31EC4:
    case 0x26D31EC5: {
        reinterpret_cast<ListFn>(addr::SC4List_Clear)(static_cast<char*>(self) + field::kDeals);
        auto* const cities = static_cast<ListNode*>(Virtual<void*>(NodeAllocatorAt(addr::SC4List_NodeAllocator4), 0xC, 0xCu));
        cities->next = cities;
        cities->prev = cities;
        void* head = cities;
        Virtual<void>(self, 0x50, &head, 0u);
        void* const ours = Region();
        while (cities->next != cities) {
            ListNode* const node = cities->prev;
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
        reinterpret_cast<ListFn>(addr::SC4List_Destroy4)(&head);
        return 1;
    }
    case 0x66956814:
    case 0x6695681A:
        reinterpret_cast<DailyFn>(addr::cSC4NeighborsSimulator_Daily)(self, At<uint8_t>(self, field::kDealsDirty),
                                                                      At<uint8_t>(self, 0x3D));
        return 1;
    case 0x66956816:
        reinterpret_cast<SelfFn>(addr::cSC4NeighborsSimulator_Monthly)(self);
        return 1;
    case 0x6AC284F3:
        At<uint8_t>(self, field::kLoaded) = 1;
        return 1;
    case 0x99EF1143: {
        void* const occupant = Virtual<void*>(message, 0x28);
        if (Virtual<uint32_t>(occupant, 0x1C) != 0x2890D4DE)
            return 1;
        void* pole = nullptr;
        Virtual<uint8_t>(occupant, 0x0, 0xA8C86142u, &pole);
        if (!Virtual<uint8_t>(pole, 0x34))
            Virtual<void>(self, 0x20, occupant);
        if (pole)
            Virtual<uint32_t>(pole, 0x8);
        return 1;
    }
    case 0xAA7A014D:
        Virtual<void>(self, 0x20, Virtual<void*>(message, 0x28));
        return 1;
    case kDealNews: {
        const uint32_t code = Virtual<uint32_t>(message, 0x24);
        if (code == 0 || code == 8)
            At<uint8_t>(self, 0x3D) = 1;
        else
            At<uint8_t>(self, field::kDealsDirty) = 1;
        reinterpret_cast<SelfFn>(addr::cSC4NeighborsSimulator_Monthly)(self);
        return 1;
    }
    default:
        return 1;
    }
}

// --- Shutdown and the save record -------------------------------------------

// 0x006CFBC0 (slot 4): the city and the message notifications released,
// unregistered from the simulator service (via the city, slot 50), the
// connections and deals emptied, the tuning released.
uint8_t __fastcall Shutdown(void* self, void*)
{
    static bool logged = false;
    FirstCall("Shutdown", logged);
    if (!At<uint8_t>(self, field::kInitialized))
        return 1;
    At<uint8_t>(self, field::kInitialized) = 0;
    if (void* const city = At<void*>(self, field::kCity)) {
        Virtual<uint32_t>(city, 0x8);
        At<void*>(self, field::kCity) = nullptr;
    }
    if (At<void*>(self, field::kMessages)) {
        void* const target = static_cast<char*>(self) + 0x4;
        for (const uint32_t type : {0xAA7A014Du, 0x278E6003u, 0xEA418D8Fu, 0x6AC284F3u, 0x99EF1143u, 0x26D31EC5u,
                                    0x26D31EC4u})
            Virtual<uint8_t>(At<void*>(self, field::kMessages), 0x18, target, type);
        Virtual<uint32_t>(At<void*>(self, field::kMessages), 0x8);
        At<void*>(self, field::kMessages) = nullptr;
    }
    if (void* const simulators = Virtual<void*>(Global<void*>(addr::SC4_City), 0xC8))
        Virtual<uint32_t>(simulators, 0x50, static_cast<char*>(self) + 0x4);
    reinterpret_cast<ListFn>(addr::SC4List_ClearReleasing)(static_cast<char*>(self) + field::kConnections);
    reinterpret_cast<ListFn>(addr::SC4List_Clear)(static_cast<char*>(self) + field::kDeals);
    if (void* const tuning = At<void*>(self, field::kTuning)) {
        At<void*>(self, field::kTuning) = nullptr;
        Virtual<uint32_t>(tuning, 0x8);
    }
    return 1;
}

// cIGZSerializable slot 3 (0x006CFB60), on the part at +8: version 2, the
// connection count and each connection (a reference). The whole word.
uint32_t __fastcall Write(void* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    char* const self = static_cast<char*>(part) - 8;
    Virtual<uint8_t>(stream, 0x1C, static_cast<uint16_t>(2));
    ListNode* const head = Head(self, field::kConnections);
    uint32_t count = 0;
    for (ListNode* node = head->next; node != head; node = node->next)
        ++count;
    Virtual<uint8_t>(stream, 0x24, count);
    for (ListNode* node = Head(self, field::kConnections)->next; node != Head(self, field::kConnections);
         node = node->next)
        reinterpret_cast<StreamFn>(addr::SC4Stream_WriteReference)(stream, node->value);
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// cIGZSerializable slot 4 (0x006CFDC0): the connections read (interface
// 0x0773FEF2) and appended.
uint32_t __fastcall Read(void* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Read", logged);
    char* const self = static_cast<char*>(part) - 8;
    uint16_t version = 0;
    const uint32_t read = Virtual<uint32_t>(stream, 0x1C, &version);
    if (version != 2)
        return read & 0xFFFFFF00u;
    uint32_t count = 0;
    Virtual<uint8_t>(stream, 0x24, &count);
    for (uint32_t i = 0; i < count; ++i) {
        void* connection = nullptr;
        if (!reinterpret_cast<ReadReferenceFn>(addr::SC4Stream_ReadReference)(stream, 0x0773FEF2, &connection, 0))
            continue;
        if (connection)
            Virtual<uint32_t>(connection, 0x4);
        PushBackReferenced(Head(self, field::kConnections), connection, addr::SC4List_NodeAllocator3);
        if (connection)
            Virtual<uint32_t>(connection, 0x8);
        Virtual<uint32_t>(connection, 0x8);
    }
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// --- Installation -----------------------------------------------------------

template <typename Fn>
bool Install(std::uintptr_t address, Fn ours, Fn* original)
{
    const addr::Entry* entry = addr_check::Find(address);
    return entry && hook::Install(*entry, ours, original);
}

bool SwapSlot(std::uintptr_t vtableAddress, int index, std::uintptr_t original, void* ours, const char* what)
{
    auto* const slot = reinterpret_cast<std::uintptr_t*>(vtableAddress) + index;
    if (*slot != original) {
        log::Write("neighbors %s: slot %d holds %08X, expected %08X; not installing", what, index,
                   static_cast<unsigned>(*slot), static_cast<unsigned>(original));
        return false;
    }
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old))
        return false;
    *slot = reinterpret_cast<std::uintptr_t>(ours);
    VirtualProtect(slot, sizeof(*slot), old, &old);
    return true;
}

using DailyOursFn = void(__fastcall*)(void*, void*, uint8_t, uint8_t);
using RectOursFn = void(__fastcall*)(void*, void*, const int32_t*);
using SelfOursFn = void(__fastcall*)(void*, void*);
DailyOursFn g_originalDaily = nullptr;
RectOursFn g_originalMarkers = nullptr;
SelfOursFn g_originalMonthly = nullptr;
CompareFn g_originalCompare = nullptr;

bool InstallMessages(registry::Mode)
{
    return game::IsBuild641() && Install(addr::cSC4NeighborsSimulator_Daily, &Daily, &g_originalDaily) &&
           Install(addr::cSC4NeighborsSimulator_UpdateMarkers, &UpdateMarkers, &g_originalMarkers) &&
           Install(addr::cSC4NeighborsSimulator_Monthly, &Monthly, &g_originalMonthly) &&
           Install(addr::cSC4NeighborsSimulator_DateNotAfter, &DateNotAfter, &g_originalCompare) &&
           SwapSlot(addr::cSC4NeighborsSimulator_vftable_4, 3, 0x006D42B0, reinterpret_cast<void*>(&DoMessage),
                    "messages");
}

bool InstallShutdown(registry::Mode)
{
    return game::IsBuild641() &&
           SwapSlot(addr::cSC4NeighborsSimulator_vftable, 4, 0x006CFBC0, reinterpret_cast<void*>(&Shutdown), "shutdown");
}

bool InstallSave(registry::Mode)
{
    return game::IsBuild641() &&
           SwapSlot(addr::cSC4NeighborsSimulator_vftable_8, 3, 0x006CFB60, reinterpret_cast<void*>(&Write), "save");
}

bool InstallLoad(registry::Mode)
{
    return game::IsBuild641() &&
           SwapSlot(addr::cSC4NeighborsSimulator_vftable_8, 4, 0x006CFDC0, reinterpret_cast<void*>(&Read), "load");
}

const bool g_registeredMessages = registry::Register({
    "simulators.neighbors.messages",
    "simulators/neighbors",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4NeighborsSimulator_vftable_4,
    "Our neighbors DoMessage, daily and monthly deal passes and connection markers (docs/neighbors.md)",
    false,
    InstallMessages,
});

const bool g_registeredShutdown = registry::Register({
    "simulators.neighbors.shutdown",
    "simulators/neighbors",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4NeighborsSimulator_vftable,
    "Our neighbors Shutdown (docs/neighbors.md)",
    false,
    InstallShutdown,
});

const bool g_registeredSave = registry::Register({
    "simulators.neighbors.save",
    "simulators/neighbors",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4NeighborsSimulator_vftable_8,
    "Our neighbors Write: the save record, version 2 (docs/neighbors.md)",
    false,
    InstallSave,
});

const bool g_registeredLoad = registry::Register({
    "simulators.neighbors.load",
    "simulators/neighbors",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4NeighborsSimulator_vftable_8,
    "Our neighbors Read: the save record, version 2 (docs/neighbors.md)",
    false,
    InstallLoad,
});

} // namespace
} // namespace opensc4::neighbors_sim
