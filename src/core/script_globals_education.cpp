// The advisors' script variables for health and education: the game's
// 0x00415480 (funding, city averages, the best and worst tracts) and
// 0x00414290 (hospitals, clinics, schools, museums and libraries: how many,
// the worst funded and worst graded of each, coverage and grades).
// Replacement, off by default:
//
//   scripts.globals.education: both publishers.
//
// The lists' helpers, the string's buffer and the location objects stay the
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

constexpr uint32_t kLocationClass = 0x6A26D049;
constexpr uint32_t kLocationInterface = 0x6A26D050;
constexpr uint32_t kHealthDepartment = 0x09188F42;
constexpr uint32_t kEducationDepartment = 0x09188F4C;
constexpr float kFloatMax = 3.40282347e+38f; // 0x7F7FFFFF

// What the service's slots 14, 15, 26 and 27 fill for a building or a
// group of buildings: a game string, then figures.
struct Stats {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
    uint32_t unknown14[4];
    uint32_t served; // +0x24
    float grade;     // +0x28
    uint32_t unknown2C;
    uint32_t unknown30;
    uint32_t unknown34;
    uint32_t unknown38[2]; // the fillers write past +0x38: the game's frame leaves 0x40 bytes
};
static_assert(sizeof(Stats) == 0x40);

struct Lowest {
    void* funding = nullptr;
    void* grade = nullptr;
    float minFunding = kFloatMax;
    float minGrade = kFloatMax;
};

// The locals of 0x00414290 that live through the whole function.
struct Frame {
    void* system;
    void* services;
    void* network;
    char* list = nullptr;      // [esp+0x14]: the head node of an occupant list
    void* location = nullptr;  // [esp+0x10]
    void* unknown = nullptr;   // [esp+0x18]: the building's cIGZUnknown
    float funding = 0.0f;      // [esp+0x2C]
    float other = 0.0f;        // [esp+0x30]
    Stats stats{};             // [esp+0x40]
};

void* Key(std::uintptr_t address)
{
    return reinterpret_cast<void*>(address);
}

void* GetCOM()
{
    return reinterpret_cast<void*(__cdecl*)()>(addr::RZGetCOM)();
}

void Free(void* memory)
{
    reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(memory);
}

void Reset(void*& ref)
{
    if (void* const object = ref) {
        ref = nullptr;
        Virtual<uint32_t>(object, 0x8);
    }
}

uint32_t CountNodes(char* head)
{
    uint32_t count = 0;
    for (char* node = At<char*>(head, 0x0); node != head; node = At<char*>(node, 0x0))
        ++count;
    return count;
}

float CountAsFloat(char* head)
{
    return static_cast<float>(static_cast<double>(CountNodes(head)));
}

// Empties the list from its back, keeping the building with the lowest
// funding and the one with the lowest grade. The kept pointers are used
// after the list's reference is released, as in the game.
Lowest Scan(Frame& frame, float count, int32_t flag, std::size_t gradeSlot)
{
    Lowest low;
    if (count == 0.0f)
        return low;
    while (At<char*>(frame.list, 0x0) != frame.list) {
        void* const building = At<void*>(At<char*>(frame.list, 0x4), 0x8);
        Virtual<uint8_t>(frame.services, 0x50, building, &frame.funding, &frame.other, flag);
        if (frame.funding < low.minFunding) {
            low.minFunding = frame.funding;
            low.funding = building;
        }
        Virtual<uint8_t>(frame.services, gradeSlot, building, &frame.stats);
        const double grade = static_cast<double>(frame.stats.grade) * 100.0;
        if (grade < static_cast<double>(low.minGrade)) {
            low.minGrade = static_cast<float>(grade);
            low.grade = building;
        }
        Virtual<uint32_t>(building, 0x8);
        char* const node = At<char*>(frame.list, 0x4);
        char* const previous = At<char*>(node, 0x4);
        char* const next = At<char*>(node, 0x0);
        At<char*>(previous, 0x0) = next;
        At<char*>(next, 0x4) = previous;
        Virtual<void>(ListNodeAllocator(), 0x10, node);
    }
    return low;
}

// A new location object for the building.
void Locate(Frame& frame, void* building)
{
    Reset(frame.unknown);
    Virtual<uint8_t>(building, 0x0, uint32_t{1}, &frame.unknown);
    void* const com = GetCOM();
    Reset(frame.location);
    Virtual<uint8_t>(com, 0xC, kLocationClass, kLocationInterface, &frame.location);
    Virtual<void>(frame.location, 0x1C, frame.unknown);
}

// The second variable keeps its location object in the frame; the next
// user releases it.
void PublishLowest(Frame& frame, const Lowest& low, const char* fundingName, const char* gradeName)
{
    Reset(frame.location);
    if (low.funding)
        Locate(frame, low.funding);
    Virtual<uint8_t>(frame.system, 0x38, fundingName, static_cast<double>(low.minFunding) * 100.0, frame.location);
    Reset(frame.location);
    if (low.grade)
        Locate(frame, low.grade);
    Virtual<uint8_t>(frame.system, 0x38, gradeName, static_cast<double>(low.minGrade), frame.location);
}

void PublishNumber(void* system, const char* name, double value)
{
    Virtual<uint8_t>(system, 0x34, name, value);
}

// The served figure over the buildings (plus one), as a percentage.
void PublishCoverage(Frame& frame, const char* name)
{
    const float served = static_cast<float>(static_cast<double>(frame.stats.served));
    const int32_t buildings = Virtual<int32_t>(frame.services, 0x40);
    PublishNumber(frame.system, name, static_cast<double>(served) / (static_cast<double>(buildings) + 1.0) * 100.0);
}

void PublishGrade(Frame& frame, const char* name, float count)
{
    double grade = 0.0;
    if (count > 0.0f)
        grade = static_cast<double>(frame.stats.grade) / static_cast<double>(count) * 100.0;
    PublishNumber(frame.system, name, grade);
}

// Schools, museums and libraries.
float PublishEducationGroup(Frame& frame, std::uintptr_t key, const char* countName, const char* fundingName,
                            const char* gradeName)
{
    Virtual<void>(frame.network, 0xC0, &frame.list);
    Virtual<uint8_t>(frame.services, 0xBC, &frame.list, Key(key));
    const float count = CountAsFloat(frame.list);
    PublishNumber(frame.system, countName, static_cast<double>(count));
    const Lowest low = Scan(frame, count, int32_t{1}, 0x68);
    PublishLowest(frame, low, fundingName, gradeName);
    Virtual<uint8_t>(frame.services, 0x38, &frame.stats, Key(key));
    return count;
}

void PublishCount(Frame& frame, std::uintptr_t key, const char* name)
{
    Virtual<void>(frame.network, 0xC0, &frame.list);
    Virtual<uint8_t>(frame.services, 0xBC, &frame.list, Key(key));
    PublishNumber(frame.system, name, static_cast<double>(CountNodes(frame.list)));
}

// The game's 0x00414290. It never reads ECX.
void __fastcall PublishBuildings(char* /*self*/, void*)
{
    Frame frame;
    frame.system = Global<void*>(addr::cSC4AdvisorSystem_Instance);
    frame.services = Global<void*>(addr::SC4_Service43DA4);
    frame.network = Global<void*>(addr::SC4_NetworkService);

    char* const buffer = static_cast<char*>(reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_stl_allocate)(8));
    buffer[0] = 0;
    frame.stats.vtable = addr::SC4String_Vtable;
    frame.stats.begin = buffer;
    frame.stats.end = buffer;
    frame.stats.capacity = buffer + 8;

    char* const head = Virtual<char*>(ListNodeAllocator(), 0xC, uint32_t{0xC});
    At<char*>(head, 0x0) = head;
    At<char*>(head, 0x4) = head;
    frame.list = head;

    // Hospitals.
    Virtual<void>(frame.network, 0xC0, &frame.list);
    Virtual<uint8_t>(frame.services, 0xC4, &frame.list, Key(addr::ScriptGlobals_HospitalKey));
    const float hospitals = CountAsFloat(frame.list);
    PublishNumber(frame.system, "g_num_hospitals", static_cast<double>(hospitals));
    Lowest low = Scan(frame, hospitals, int32_t{0}, 0x6C);
    PublishLowest(frame, low, "l_hospital_funding_l", "l_hospital_grade_l");

    // Clinics.
    Virtual<void>(frame.network, 0xC0, &frame.list);
    Virtual<uint8_t>(frame.services, 0xC4, &frame.list, Key(addr::ScriptGlobals_ClinicKey));
    const float clinics = CountAsFloat(frame.list);
    const float healthBuildings = static_cast<float>(static_cast<double>(hospitals) + static_cast<double>(clinics));
    PublishNumber(frame.system, "g_num_clinics", static_cast<double>(clinics));
    low = Scan(frame, clinics, int32_t{0}, 0x6C);
    PublishLowest(frame, low, "l_clinic_funding_l", "l_clinic_grade_l");

    // Health as a whole.
    Virtual<uint8_t>(frame.services, 0x3C, &frame.stats, Key(addr::ScriptGlobals_HealthKey));
    PublishCoverage(frame, "g_health_coverage_p");
    PublishGrade(frame, "ga_health_grade", healthBuildings);

    // Schools.
    const float schools = PublishEducationGroup(frame, addr::ScriptGlobals_SchoolKey, "g_num_schools",
                                                "l_school_funding_l", "l_school_grade_l");
    PublishGrade(frame, "ga_school_grade", schools);

    // Museums.
    const float museums = PublishEducationGroup(frame, addr::ScriptGlobals_MuseumKey, "g_num_museums",
                                                "l_museum_funding_l", "l_museum_grade_l");
    PublishNumber(frame.system, "g_num_museum_served", static_cast<double>(frame.stats.served));
    PublishGrade(frame, "ga_museum_grade", museums);

    // Libraries.
    const float libraries = PublishEducationGroup(frame, addr::ScriptGlobals_LibraryKey, "g_num_libraries",
                                                  "l_library_funding_l", "l_library_grade_l");
    PublishNumber(frame.system, "g_num_library_served", static_cast<double>(frame.stats.served));
    PublishGrade(frame, "ga_library_grade", libraries);

    Virtual<uint8_t>(frame.services, 0x38, &frame.stats, Key(addr::ScriptGlobals_SchoolKey));
    PublishCoverage(frame, "g_education_coverage_p");

    PublishCount(frame, addr::ScriptGlobals_SchoolKey, "g_num_educational_buildings");
    PublishCount(frame, addr::ScriptGlobals_ElementaryKey, "g_num_elem_schools");
    PublishCount(frame, addr::ScriptGlobals_HighSchoolKey, "g_num_high_schools");
    PublishCount(frame, addr::ScriptGlobals_CollegeKey, "g_num_colleges");
    Virtual<void>(frame.network, 0xC0, &frame.list);

    if (frame.unknown)
        Virtual<uint32_t>(frame.unknown, 0x8);
    if (frame.location)
        Virtual<uint32_t>(frame.location, 0x8);
    reinterpret_cast<void(__thiscall*)(void*)>(addr::SC4List_Destroy2)(&frame.list);
    frame.stats.vtable = addr::SC4String_Vtable;
    if (frame.stats.begin)
        Free(frame.stats.begin);
}

// A tract's cell as a new location object in `location`, published with
// the tract's value.
void PublishTract(void* system, void*& location, const char* name, uint32_t x, uint32_t z, float value)
{
    void* const com = GetCOM();
    Reset(location);
    Virtual<uint8_t>(com, 0xC, kLocationClass, kLocationInterface, &location);
    const float floatZ = static_cast<float>(static_cast<double>(z));
    const float floatX = static_cast<float>(static_cast<double>(x));
    Virtual<void>(location, 0xC, floatX, floatZ);
    Virtual<uint8_t>(system, 0x38, name, static_cast<double>(value), location);
}

double FundingPercentage(void* budget, uint32_t department)
{
    void* const line = Virtual<void*>(budget, 0x70, department); // GetDepartmentBudget
    const float* const funding = Virtual<const float*>(line, 0x28); // GetFundingPercentage
    return static_cast<double>(*funding) * 100.0;
}

// The body of the game's 0x00415480. `treated` and `capacity` are what the
// game's frame holds at [esp+0x5C] and [esp+0x4C], which it never sets.
void __stdcall PublishHealthEducationBody(char* self, uint32_t treated, uint32_t capacity)
{
    void* const budget = Global<void*>(addr::SC4_BudgetSimulator);
    void* const system = Global<void*>(addr::cSC4AdvisorSystem_Instance);
    void* const services = Global<void*>(addr::SC4_Service43DA4);

    char* const buffer = static_cast<char*>(reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_stl_allocate)(8));
    buffer[0] = 0;
    void* location = nullptr;

    PublishNumber(system, "g_health_funding_p", FundingPercentage(budget, kHealthDepartment));
    const uint8_t lifeExpectancy = Virtual<uint8_t>(services, 0x88);
    PublishNumber(system, "ga_life_exp", static_cast<double>(static_cast<int32_t>(lifeExpectancy)));
    PublishNumber(system, "ga_health", Virtual<double>(services, 0x84));
    PublishNumber(system, "ga_education", Virtual<double>(services, 0x80));

    // The lowest tract (two cell numbers and a value) and the highest.
    uint32_t lowA = 0;    // [esp+0x18]
    uint32_t lowB = 0;    // [esp+0x1C]
    float lowValue = 0;   // [esp+0x20]
    uint32_t highA = 0;   // [esp+0x24]
    uint32_t highB = 0;   // [esp+0x28]
    float highValue = 0;  // [esp+0x2C]
    Virtual<uint8_t>(services, 0xB0, &lowB, &lowA, &highB, &highA, &lowValue, &highValue);
    PublishTract(system, location, "l_tract_life_exp_l", lowB, lowA, lowValue);
    PublishTract(system, location, "l_tract_hq_l", lowB, lowA, lowValue);
    PublishTract(system, location, "l_tract_life_exp_h", highB, highA, highValue);
    PublishTract(system, location, "l_tract_hq_h", highB, highA, highValue);

    PublishNumber(system, "g_num_treated_patients", static_cast<double>(treated));
    PublishNumber(system, "g_health_capacity", static_cast<double>(capacity));
    PublishNumber(system, "g_education_funding_p", FundingPercentage(budget, kEducationDepartment));

    Virtual<uint8_t>(services, 0xAC, &lowB, &lowA, &highB, &highA, &lowValue, &highValue);
    PublishTract(system, location, "l_tract_eq_l", lowB, lowA, lowValue);
    PublishTract(system, location, "l_tract_eq_h", highB, highA, highValue);

    PublishBuildings(self, nullptr);

    if (location)
        Virtual<uint32_t>(location, 0x8);
    Free(buffer);
}

// The game's 0x00415480 (thiscall, no arguments). Its frame is at
// ((esp - 4) & ~7) - 0x78 and it publishes two slots of it that nothing
// has written: read them here, before our own frame is laid over them.
__declspec(naked) void PublishHealthEducation()
{
    __asm lea eax, [esp - 4]
    __asm and eax, 0xFFFFFFF8
    __asm push dword ptr [eax - 0x2C]
    __asm push dword ptr [eax - 0x1C]
    __asm push ecx
    __asm call PublishHealthEducationBody
    __asm ret
}

bool InstallEducation(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[2] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::ScriptGlobals_PublishEducationBuildings, reinterpret_cast<void*>(&PublishBuildings)},
        {addr::ScriptGlobals_PublishHealthEducation, reinterpret_cast<void*>(&PublishHealthEducation)},
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

const bool g_educationRegistered = registry::Register({
    "scripts.globals.education",
    "scripts/globals",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::ScriptGlobals_PublishHealthEducation,
    "Our publishers of the health and education script variables",
    false,
    InstallEducation,
});

} // namespace
} // namespace opensc4::script_globals
