// The public safety advisor's script variables, and the message handlers
// of the safety, transportation and utilities advisors that publish
// variables (fire, police, crime, strikes, disasters, zotted cars).
// Replacement, off by default:
//
//   scripts.globals.safety: the safety advisor's two publishers and its
//     message handler, the transportation and the utilities advisors'
//     message handlers.
//
// The handlers are on the advisors' message target at +0x10. The advisors'
// base handler, their update request and the other publishers stay the
// game's.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstdint>
#include <utility>

namespace opensc4::script_globals {
namespace {

constexpr uint32_t kUpdate = 0xCA8F3DB6;      // the advisor's own update request
constexpr uint32_t kSubjectClass = 0x6A26D049;
constexpr uint32_t kSubjectInterface = 0x6A26D050;

namespace field { // from the message target at +0x10
constexpr std::size_t kPending = 0xAC;    // which publishers the update runs
constexpr std::size_t kZots = 0xB0;       // transportation: a set, its size at +4
constexpr std::size_t kFireSeen = 0xB1;   // safety
constexpr std::size_t kRiotSeen = 0xB2;
constexpr std::size_t kQuiet = 0xB3;
} // namespace field

void* Advisors()
{
    return Global<void*>(addr::cSC4AdvisorSystem_Instance);
}

void Set(void* system, const char* name, double value)
{
    Virtual<uint8_t>(system, 0x34, name, value);
}

void SetWithSubject(void* system, const char* name, double value, void* subject)
{
    Virtual<uint8_t>(system, 0x38, name, value, subject);
}

void Push(void* system, uint32_t event, void* subject)
{
    Virtual<uint8_t>(system, 0x3C, event, subject);
}

void Reset(void*& ref)
{
    if (void* const held = ref) {
        ref = nullptr;
        Virtual<uint32_t>(held, 0x8);
    }
}

// A new subject object in `ref`.
void MakeSubject(void*& ref)
{
    void* const com = reinterpret_cast<void*(__cdecl*)()>(addr::RZGetCOM)();
    Reset(ref);
    Virtual<uint8_t>(com, 0xC, kSubjectClass, kSubjectInterface, &ref);
}

// A new subject in `subject` for an occupant.
void SubjectOf(void* occupant, void*& unknown, void*& subject)
{
    Reset(unknown);
    Virtual<uint8_t>(occupant, 0x0, uint32_t{1}, &unknown);
    MakeSubject(subject);
    Virtual<void>(subject, 0x1C, unknown);
}

double Percent(const float* value)
{
    return static_cast<double>(*value) * static_cast<double>(100.0f);
}

double Unsigned(uint32_t value)
{
    return static_cast<double>(value);
}

float UnsignedFloat(uint32_t value)
{
    return static_cast<float>(static_cast<double>(value));
}

int32_t Ftol(double value)
{
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

char* NewList()
{
    char* const head = Virtual<char*>(ListNodeAllocator(), 0xC, int32_t{0xC});
    At<char*>(head, 0x0) = head;
    At<char*>(head, 0x4) = head;
    return head;
}

void DestroyList(char** list)
{
    reinterpret_cast<void(__thiscall*)(char**)>(addr::SC4List_Destroy2)(list);
}

uint8_t BaseDoMessage(char* self, void* message)
{
    return reinterpret_cast<uint8_t(__thiscall*)(char*, void*)>(addr::cSC4Advisor_DoMessage)(self, message);
}

void RequestUpdate(char* self)
{
    reinterpret_cast<void(__thiscall*)(char*, int32_t, int32_t)>(addr::cSC4Advisor_RequestUpdate)(self - 0x10, 0, 0);
}

// A building was added or removed: when it is of one of `count` kinds,
// ask for an update with `bits`.
void OccupantChanged(char* self, void* message, int count, uint32_t bits)
{
    void* const occupant = Virtual<void*>(message, 0x28);
    if (Virtual<uint32_t>(occupant, 0x1C) == 0x278128A0 ||
        (count > 1 && Virtual<uint32_t>(occupant, 0x1C) == 0xC772BF98)) {
        if (At<uint32_t>(self, field::kPending) == 0)
            RequestUpdate(self);
        At<uint32_t>(self, field::kPending) |= bits;
    }
}

// The game's 0x00416030: the stations' figures, and of each kind the worst
// funded station and the first one without a road.
void __fastcall PublishStations(char*, void*)
{
    void* const system = Advisors();
    void* unknown = nullptr;
    void* subject = nullptr;
    void* const network = Global<void*>(addr::SC4_NetworkService);
    uint32_t other; // an output of the stations' funding query that nothing reads

    void* const budget = Global<void*>(addr::SC4_BudgetSimulator);
    Set(system, "g_fire_funding_p", Percent(Virtual<float*>(Virtual<void*>(budget, 0x70, uint32_t{0x28F55A9F}), 0x28)));
    Set(system, "g_fire_station_count", Unsigned(Virtual<uint32_t>(Global<void*>(addr::SC4_Service43D78), 0x5C)));
    Set(system, "ga_flammability",
        static_cast<double>(static_cast<int32_t>(Virtual<uint8_t>(Global<void*>(addr::SC4_Service43D7C), 0x20))));
    Set(system, "ga_fire_coverage", Virtual<double>(Global<void*>(addr::SC4_Service43D78), 0x4C));
    Set(system, "g_fire_coverage_p", Percent(Virtual<float*>(Global<void*>(addr::SC4_Service43D78), 0x50)));

    char* fireList = nullptr;
    fireList = NewList();
    void* const fire = Global<void*>(addr::SC4_Service43D78);
    Virtual<void>(fire, 0x58, &fireList);
    float worst = 100.0f;
    void* worstStation = nullptr;
    void* noRoad = nullptr;
    for (char* node = At<char*>(fireList, 0x0); node != fireList; node = At<char*>(node, 0x0)) {
        float funding = 0.0f;
        void* const station = At<void*>(node, 0x8);
        Virtual<void>(fire, 0x20, station, &other, &funding);
        if (funding < worst) {
            worst = funding;
            worstStation = station;
        }
        if (!noRoad) {
            void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x38, station);
            if (lot && !Virtual<uint8_t>(lot, 0x70))
                noRoad = station;
        }
    }
    Virtual<void>(network, 0xC0, &fireList);
    Reset(subject);
    if (worstStation)
        SubjectOf(worstStation, unknown, subject);
    SetWithSubject(system, "l_fire_funding_p", static_cast<double>(worst) * static_cast<double>(100.0f), subject);
    Reset(subject);
    if (noRoad)
        SubjectOf(noRoad, unknown, subject);
    SetWithSubject(system, "l_fire_station_no_roads", static_cast<double>(static_cast<int32_t>(noRoad != nullptr)),
                   subject);

    Set(system, "g_police_funding_p",
        Percent(Virtual<float*>(Virtual<void*>(Global<void*>(addr::SC4_BudgetSimulator), 0x70, uint32_t{0xA2963983}),
                                0x28)));
    Set(system, "g_jail_capacity",
        static_cast<double>(Virtual<int32_t>(Global<void*>(addr::SC4_PoliceSimulator), 0x74)));
    Set(system, "g_police_station_count", Unsigned(Virtual<uint32_t>(Global<void*>(addr::SC4_PoliceSimulator), 0x60)));
    const uint32_t jails = Virtual<uint32_t>(Global<void*>(addr::SC4_PoliceSimulator), 0x8C);
    other = jails; // the same stack slot
    Set(system, "g_jail_count", Unsigned(jails));
    Set(system, "ga_police_coverage", Virtual<double>(Global<void*>(addr::SC4_PoliceSimulator), 0x48));
    Set(system, "g_police_coverage_p", Percent(Virtual<float*>(Global<void*>(addr::SC4_PoliceSimulator), 0x4C)));

    char* policeList = nullptr;
    policeList = NewList();
    void* const police = Global<void*>(addr::SC4_PoliceSimulator);
    Virtual<void>(police, 0x54, &policeList);
    worst = 100.0f;
    worstStation = nullptr;
    noRoad = nullptr;
    for (char* node = At<char*>(policeList, 0x0); node != policeList; node = At<char*>(node, 0x0)) {
        float funding = 0.0f;
        void* const station = At<void*>(node, 0x8);
        Virtual<void>(police, 0x1C, station, &other, &funding);
        if (funding < worst) {
            worst = funding;
            worstStation = station;
        }
        if (!noRoad) {
            void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x38, station);
            if (lot && !Virtual<uint8_t>(lot, 0x70))
                noRoad = station;
        }
    }
    Virtual<void>(network, 0xC0, &policeList);
    Reset(subject);
    if (worstStation)
        SubjectOf(worstStation, unknown, subject);
    SetWithSubject(system, "l_police_funding_p", static_cast<double>(worst) * static_cast<double>(100.0f), subject);
    Reset(subject);
    if (noRoad)
        SubjectOf(noRoad, unknown, subject);
    SetWithSubject(system, "l_police_no_roads", static_cast<double>(static_cast<int32_t>(noRoad != nullptr)), subject);

    DestroyList(&policeList);
    DestroyList(&fireList);
    if (subject)
        Virtual<uint32_t>(subject, 0x8);
    if (unknown)
        Virtual<uint32_t>(unknown, 0x8);
}

// The game's 0x004166B0: the safety advisor's variables.
void __fastcall PublishSafety(char* self, void*)
{
    void* const system = Advisors();
    void* const figures = Global<void*>(addr::SC4_MonthlyFigures);
    void* subject = nullptr;
    float chance;

    Virtual<void*>(Global<void*>(addr::SC4_BudgetSimulator), 0x70, uint32_t{0x28F55A9F});
    Set(system, "g_fire_strike_chance", Percent(Virtual<float*>(Global<void*>(addr::SC4_Service43D78), 0x30, &chance)));
    Set(system, "g_population",
        static_cast<double>(Virtual<int32_t>(Virtual<void*>(Global<void*>(addr::SC4_City), 0x110), 0x40)));

    int32_t x = 0;
    int32_t z = 0;
    uint8_t found = 0;
    MakeSubject(subject);
    Virtual<void>(Global<void*>(addr::SC4_Service43D7C), 0x1C, &found, &x, &z);
    Virtual<void>(subject, 0xC, static_cast<float>(x), static_cast<float>(z));
    SetWithSubject(system, "l_flammability_h", static_cast<double>(static_cast<int32_t>(found)), subject);

    const float crime =
        static_cast<float>(static_cast<int32_t>(Virtual<uint8_t>(Global<void*>(addr::SC4_Service43D70), 0x18)));
    Set(system, "ga_crime", static_cast<double>(crime));
    Virtual<void>(figures, 0x30, uint32_t{0xAA662693}, Ftol(crime));

    MakeSubject(subject);
    Virtual<void>(Global<void*>(addr::SC4_Service43D70), 0x20, &found, &x, &z);
    Virtual<void>(subject, 0xC, static_cast<float>(x), static_cast<float>(z));
    SetWithSubject(system, "l_crime_h", static_cast<double>(static_cast<int32_t>(found)), subject);

    Set(system, "g_inmate_count",
        static_cast<double>(Virtual<int32_t>(Global<void*>(addr::SC4_PoliceSimulator), 0x78)));
    Set(system, "g_criminal_count",
        static_cast<double>(Virtual<int32_t>(Global<void*>(addr::SC4_PoliceSimulator), 0x7C)));
    Set(system, "g_crime_commited_count", Unsigned(Virtual<uint32_t>(Global<void*>(addr::SC4_Service43D70), 0x1C)));
    Set(system, "g_arrest_count",
        static_cast<double>(Virtual<int32_t>(Global<void*>(addr::SC4_PoliceSimulator), 0x88)));
    Set(system, "g_police_strike_chance",
        Percent(Virtual<float*>(Global<void*>(addr::SC4_PoliceSimulator), 0x2C, &chance)));
    Set(system, "g_school_strike_chance",
        Percent(Virtual<float*>(Global<void*>(addr::SC4_Service43DA4), 0x28, &chance)));
    Set(system, "g_health_strike_chance",
        Percent(Virtual<float*>(Global<void*>(addr::SC4_Service43DA4), 0x2C, &chance)));

    PublishStations(self, nullptr);
    if (subject)
        Virtual<uint32_t>(subject, 0x8);
}

// A strike that starts or ends with the message's flag: the variable, then
// the event, with a subject when it starts.
uint8_t StrikeChanged(void* system, void* message, const char* name, uint32_t started, uint32_t ended)
{
    const bool on = Virtual<uint32_t>(message, 0x38) != 0;
    Set(system, name, static_cast<double>(static_cast<int32_t>(on)));
    void* subject = nullptr;
    if (on) {
        MakeSubject(subject);
        Virtual<void>(subject, 0x1C, Virtual<void*>(message, 0x80));
    }
    Push(system, on ? started : ended, subject);
    if (subject)
        Virtual<uint32_t>(subject, 0x8);
    return 1;
}

// A strike that starts: the variable, then the event with a subject.
uint8_t StrikeStarted(void* system, void* message, const char* name, uint32_t event)
{
    Set(system, name, 1.0);
    void* subject = nullptr;
    MakeSubject(subject);
    Virtual<void>(subject, 0x1C, Virtual<void*>(message, 0x80));
    Push(system, event, subject);
    if (subject)
        Virtual<uint32_t>(subject, 0x8);
    return 1;
}

uint8_t StrikeEnded(void* system, const char* name, double value, uint32_t event)
{
    Set(system, name, value);
    Push(system, event, static_cast<void*>(nullptr));
    return 1;
}

uint8_t Jailbreak(void* system, void* message, double value, uint32_t event)
{
    void* const jail = Virtual<void*>(message, 0x80);
    if (!jail)
        return 1;
    void* subject = nullptr;
    MakeSubject(subject);
    Set(system, "g_jailbreak", value);
    Virtual<void>(subject, 0x1C, jail);
    Push(system, event, subject);
    if (subject)
        Virtual<uint32_t>(subject, 0x8);
    return 1;
}

// A disaster began (message 0xCA1B1E64).
uint8_t DisasterBegan(char* self, void* system, void* message)
{
    void* const data = Virtual<void*>(message, 0x80);
    if (data)
        Virtual<uint32_t>(data, 0x4);
    void* disaster = nullptr;
    if (data && Virtual<uint8_t>(data, 0x0, uint32_t{0x22223BA4}, &disaster)) {
        Set(system, "g_disasters_in_progress", 1.0);
        void* subject = nullptr;
        MakeSubject(subject);
        const uint32_t kind = Virtual<uint32_t>(Virtual<void*>(disaster, 0x10), 0x14);
        const uint32_t event = 0xCA1B1E64u ^ kind;
        const float z = UnsignedFloat(Virtual<uint32_t>(message, 0x4C));
        const float x = UnsignedFloat(Virtual<uint32_t>(message, 0x38));
        Virtual<void>(subject, 0xC, x, z);
        const bool fire = kind == 0x621CDA33;
        Set(system, "g_disasters_fire_started", static_cast<double>(fire ? 1.0f : 0.0f));
        Set(system, "g_disasters_fire_ended", 0.0);
        if (fire) {
            At<uint8_t>(self, field::kFireSeen) = 1;
            void* instance = nullptr;
            Virtual<uint8_t>(disaster, 0x0, uint32_t{0x49B43F58}, &instance);
            Set(system, "g_disasters_fire_cause_arson", 0.0);
            Set(system, "g_disasters_fire_cause_effects", 0.0);
            Set(system, "g_disasters_fire_cause_flammability", 0.0);
            switch (Virtual<uint32_t>(instance, 0x38)) {
            case 1:
            case 3:
                At<uint8_t>(self, field::kQuiet) = 1;
                break;
            case 2:
                Set(system, "g_disasters_fire_cause_arson", 1.0);
                break;
            case 4:
                Set(system, "g_disasters_fire_cause_effects", 1.0);
                At<uint8_t>(self, field::kQuiet) = 1;
                break;
            case 5:
                Set(system, "g_disasters_fire_cause_flammability", 1.0);
                break;
            default:
                break;
            }
            if (instance)
                Virtual<uint32_t>(instance, 0x8);
        }
        const bool riot = kind == 0xCA380B06;
        if (riot)
            At<uint8_t>(self, field::kRiotSeen) = 1;
        Set(system, "g_disasters_riot_started", static_cast<double>(riot ? 1.0f : 0.0f));
        Set(system, "g_disasters_riot_ended", 0.0);
        if (!At<uint8_t>(self, field::kQuiet) && !fire && !riot && kind != 0x4A1332E8)
            At<uint8_t>(self, field::kQuiet) = 1;
        if (!At<uint8_t>(self, field::kQuiet))
            Push(system, event, subject);
        else
            Push(system, 0xCC3298DCu ^ kind, subject);
        if (subject)
            Virtual<uint32_t>(subject, 0x8);
    }
    if (!At<uint8_t>(self, field::kQuiet))
        Push(system, 0xCA3C0412, static_cast<void*>(nullptr));
    if (disaster)
        Virtual<uint32_t>(disaster, 0x8);
    if (data)
        Virtual<uint32_t>(data, 0x8);
    return 1;
}

// The disasters are over (message 0xAA1B2002).
uint8_t DisastersEnded(char* self)
{
    void* const layer = Virtual<void*>(Global<void*>(addr::SC4_City), 0x120);
    void* const system = Advisors();
    if (char* const totals = Virtual<char*>(layer, 0x48)) {
        const double buildings = Unsigned(At<uint32_t>(totals, 0x0));
        Set(system, "g_disasters_in_progress", 0.0);
        Set(system, "g_disasters_damage_building_count", buildings);
        Set(system, "g_disasters_damage_cost", static_cast<double>(At<int64_t>(totals, 0x8)));
        Set(system, "g_disasters_police_dispatch_effectiveness", Percent(&At<float>(totals, 0x60)));
        Set(system, "g_disasters_fire_dispatch_effectiveness", Percent(&At<float>(totals, 0x5C)));
        if (At<uint8_t>(self, field::kFireSeen)) {
            Set(system, "g_disasters_fire_ended", 1.0);
            Set(system, "g_disasters_fire_started", 0.0);
        }
        if (At<uint8_t>(self, field::kRiotSeen)) {
            Set(system, "g_disasters_riot_ended", 1.0);
            Set(system, "g_disasters_riot_started", 0.0);
        }
        At<uint8_t>(self, field::kFireSeen) = 0;
        At<uint8_t>(self, field::kRiotSeen) = 0;
    }
    Push(system, 0xAA31A2F7, static_cast<void*>(nullptr));
    At<uint8_t>(self, field::kQuiet) = 0;
    return 1;
}

// The game's 0x004169E0, on the safety advisor's message target at +0x10.
uint8_t __fastcall SafetyDoMessage(char* self, void*, void* message)
{
    void* const system = Advisors();
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    switch (type) {
    case 0x077ACCD2:
        return StrikeChanged(system, message, "g_health_strike", 0xAA60FACA, 0x2A60FACF);
    case 0x077ACCD0:
        return StrikeChanged(system, message, "g_school_strike", 0xEA60FABF, 0x2A60FAC7);
    case 0x2A614891:
        return Jailbreak(system, message, 1.0, 0xCA616650);
    case 0x2A614892:
        return Jailbreak(system, message, 0.0, 0x6A61665A);
    case 0x68F54CE1:
        return StrikeStarted(system, message, "g_fire_strike", 0x0A31A314);
    case 0x68F54CE2:
        return StrikeEnded(system, "g_fire_strike", 0.0, 0x6A31A319);
    case 0x69247DC4:
        return StrikeEnded(system, "g_transit_strike", 1.0, 0x69247DC4);
    case 0x69247DC5:
        return StrikeEnded(system, "g_transit_strike", 0.0, 0x69247DC5);
    case 0x99EF1142:
    case 0x99EF1143:
        OccupantChanged(self, message, 1, 1);
        BaseDoMessage(self, message);
        return 1;
    case 0xA8DAF511:
        return StrikeStarted(system, message, "g_police_strike", 0x6A31A2FE);
    case 0xA8DAF512:
        return StrikeEnded(system, "g_police_strike", 0.0, 0x8A31A303);
    case 0xAA1B2002:
        return DisastersEnded(self);
    case 0xAA552F77: {
        void* subject = nullptr;
        MakeSubject(subject);
        Virtual<void>(subject, 0x1C, Virtual<void*>(message, 0x80));
        Push(system, 0xAA552F77, subject);
        if (subject)
            Virtual<uint32_t>(subject, 0x8);
        return 1;
    }
    case 0xAA667CE2: {
        void* const data = Virtual<void*>(message, 0x80);
        if (!data)
            return 1;
        void* subject = nullptr;
        MakeSubject(subject);
        Virtual<void>(subject, 0x1C, data);
        Push(system, 0x2A667FC3, subject);
        if (subject)
            Virtual<uint32_t>(subject, 0x8);
        return 1;
    }
    case 0xCA1B1E64:
        return DisasterBegan(self, system, message);
    case 0xCA8DCDB4: {
        const uint32_t which = Virtual<uint32_t>(message, 0x24);
        const uint32_t amount = Virtual<uint32_t>(message, 0x38);
        if (!Virtual<void*>(message, 0x80))
            return 1;
        if (which == 0) {
            if (amount > 2)
                Push(system, 0xCA3FCEFA, static_cast<void*>(nullptr));
        } else if (which == 1) {
            if (amount > 2)
                Push(system, 0xCA3FCEF4, static_cast<void*>(nullptr));
        }
        return 1;
    }
    case kUpdate:
        if (At<uint8_t>(self, field::kPending) & 1)
            PublishStations(self - 0x10, nullptr);
        At<uint32_t>(self, field::kPending) = 0;
        BaseDoMessage(self, message);
        return 1;
    case 0xEA1F0DCD: {
        void* subject = nullptr;
        MakeSubject(subject);
        const float z = UnsignedFloat(Virtual<uint32_t>(message, 0x38));
        const float x = UnsignedFloat(Virtual<uint32_t>(message, 0x24));
        Virtual<void>(subject, 0xC, x, z);
        if (!At<uint8_t>(self, field::kQuiet))
            Push(system, 0x4A31CF15, subject);
        if (subject)
            Virtual<uint32_t>(subject, 0x8);
        return 1;
    }
    default:
        return BaseDoMessage(self, message);
    }
}

// The game's 0x0041D250, on the transportation advisor's message target at
// +0x10: the set of zotted cars and its size.
uint8_t __fastcall TransportationDoMessage(char* self, void*, void* message)
{
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    if (type == 0x99EF1142 || type == 0x99EF1143) {
        OccupantChanged(self, message, 2, 3);
        BaseDoMessage(self, message);
        return 1;
    }
    if (type == kUpdate) {
        if (At<uint8_t>(self, field::kPending) & 3)
            reinterpret_cast<void(__thiscall*)(char*)>(addr::ScriptGlobals_PublishTransportCounts)(self - 0x10);
        At<uint32_t>(self, field::kPending) = 0;
        BaseDoMessage(self, message);
        return 1;
    }
    if (type != 0x4C04DE69)
        return BaseDoMessage(self, message);

    void* car;
    if (Virtual<uint32_t>(message, 0x24) == 2) {
        car = Virtual<void*>(message, 0x80);
        reinterpret_cast<uint32_t(__thiscall*)(char*, void**)>(addr::cSC4AdvisorTransportation_ZotErase)(
            self + field::kZots, &car);
    } else if (Virtual<uint32_t>(message, 0x24) == 1) {
        car = Virtual<void*>(message, 0x80);
        struct {
            void* at;
            uint8_t added;
        } result;
        reinterpret_cast<void*(__thiscall*)(char*, void*, void**)>(addr::cSC4AdvisorTransportation_ZotInsert)(
            self + field::kZots, &result, &car);
    }
    if (void* const system = Advisors())
        Set(system, "g_car_zot_count", Unsigned(At<uint32_t>(self, field::kZots + 0x4)));
    return 1;
}

// The game's 0x00422770, on the utilities advisor's message target at +0x10.
uint8_t __fastcall UtilitiesDoMessage(char* self, void*, void* message)
{
    void* const system = Advisors();
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    switch (type) {
    case 0xCA358F85:
        Set(system, "g_power_strike", 1.0);
        Push(Advisors(), 0xEA31A30A, static_cast<void*>(nullptr));
        return 1;
    case 0xCA358F9C:
        Set(system, "g_power_strike", 0.0);
        Push(Advisors(), 0x2A31A30F, static_cast<void*>(nullptr));
        return 1;
    case 0x99EF1142:
    case 0x99EF1143:
        OccupantChanged(self, message, 2, 3);
        BaseDoMessage(self, message);
        return 1;
    case kUpdate:
        if (At<uint8_t>(self, field::kPending) & 3)
            reinterpret_cast<void(__thiscall*)(char*)>(addr::ScriptGlobals_PublishUtilityBuildings)(self - 0x10);
        if (At<uint8_t>(self, field::kPending) & 4)
            reinterpret_cast<void(__thiscall*)(char*)>(addr::ScriptGlobals_PublishNeighborDeals)(self - 0x10);
        BaseDoMessage(self, message);
        return 1;
    case 0xEA418D8F: {
        if (At<uint32_t>(self, field::kPending) == 0)
            RequestUpdate(self);
        At<uint32_t>(self, field::kPending) |= 4;
        uint32_t event;
        switch (Virtual<uint32_t>(message, 0x24)) {
        case 3:
            event = 0x3A383059;
            break;
        case 4:
            event = 0x0A383053;
            break;
        case 5:
            event = 0xAA383052;
            break;
        case 6:
            event = 0x4A383049;
            break;
        default:
            return 1;
        }
        void* subject = nullptr;
        MakeSubject(subject);
        Virtual<void>(subject, 0x1C, Virtual<void*>(message, 0x80));
        Virtual<void>(self - 0x8, 0x48, event, subject);
        if (subject)
            Virtual<uint32_t>(subject, 0x8);
        return 1;
    }
    default:
        return BaseDoMessage(self, message);
    }
}

bool InstallSafety(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[8] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSC4AdvisorSafety_PublishStations, reinterpret_cast<void*>(&PublishStations)},
        {addr::cSC4AdvisorSafety_PublishGlobals, reinterpret_cast<void*>(&PublishSafety)},
        {addr::cSC4AdvisorSafety_DoMessage, reinterpret_cast<void*>(&SafetyDoMessage)},
        {addr::cSC4AdvisorTransportation_DoMessage, reinterpret_cast<void*>(&TransportationDoMessage)},
        {addr::cSC4AdvisorUtilities_DoMessage, reinterpret_cast<void*>(&UtilitiesDoMessage)},
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

const bool g_safetyRegistered = registry::Register({
    "scripts.globals.safety",
    "scripts/globals",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4AdvisorSafety_PublishGlobals,
    "Our safety advisor's script variables and the safety, transportation and utilities advisors' messages",
    false,
    InstallSafety,
});

} // namespace
} // namespace opensc4::script_globals
