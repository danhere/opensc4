// The query tool's part of the autotest, third piece (src/core/
// autotest_hooks.h; tool_query_3a.cpp, tool_query_3b.cpp): calls the
// functions of 0x004C7A70 to 0x004CA460 and hashes what comes out, so that a
// run with ui.tools.query3a/3b on and one with them off can be compared.
// Only the game's addresses and vtable slots are called, never our own
// functions.
//
//   OPENSC4_TEST_Q3=<day>    token "query3", sub-hashes on the log lines
//     "autotest: query3: ...". Everything here leaves the city as it was.
//     OPENSC4_TEST_Q3_PARTS picks the parts (default all, 255):
//
//   1   schools and hospitals (fillers 24 to 37) on up to three of each (a
//       building the residential simulator's slot 26 or 27 knows) and one
//       other building; the capacity fillers (26, 27, 29, 31, 35, 36) only
//       where the simulator's slot 28 writes both of its numbers.
//   2   garbage (42, 43, 44, 51, 95, 96) on the sample; the helpers
//       0x004C8290 and 0x004C82F0 called directly.
//   4   power (46 to 49, 53 to 56, 97, 98, 118) on the sample and the
//       plants; 99 only on plants (the power simulator's slot 50 says yes).
//   8   water (60, 61, 64, 115, 119) on the sample and the producers; 59
//       and 62 only where slot 24 writes the amount used.
//   16  transit switches (66 to 69) on the sample.
//   32  budget (70, 108 on the sample; 71 and 73 only where the budget
//       simulator has the occupant's item and its department).
//   64  lots, ports and texts (19, 74, 81, 82, 86, 88, 92, 120, 123 to
//       126); the helpers 0x004C8F20 at a few cells and 0x004C8E30 and
//       0x004CA1E0 over the cells just outside the city's edges for the
//       connection types 0 to 12.
//   128 the desirability log 0x004C9520 for the twelve developer types the
//       cell log uses, at three cells, through a stand-in log sink.
//   Every filler is called with fill 1, and with fill 0 where that does
//   not change the city.
//
//   OPENSC4_TEST_Q3_COMMIT=1 adds what changes the city and sets it back:
//   fillers 24 and 25 (a school), 33 and 34 (a hospital) with fill 0 and the
//   stand-in slider at 50, then both shares the record had put back through
//   the residential simulator's slot 19; filler 99 on a plant, then the
//   share it had put back through the power simulator's slot 51.
//
// Kept out of the hashes: every pointer (only whether it is set) and every
// out place a callee may leave unwritten (checked with two sentinels first).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/tool_query.h"

#include <opensc4/addresses.h>

#include <cISC4City.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace opensc4::autotest {
namespace {

struct Fnv {
    uint32_t h = 2166136261u;
    void Bytes(const void* data, std::size_t size)
    {
        for (std::size_t i = 0; i < size; ++i)
            h = (h ^ static_cast<const uint8_t*>(data)[i]) * 16777619u;
    }
    template <typename T>
    void Add(const T& value)
    {
        Bytes(&value, sizeof value);
    }
    void Text(const std::string& text)
    {
        Add(static_cast<uint32_t>(text.size()));
        Bytes(text.data(), text.size());
    }
};

char g_token[48];

namespace school_record = tool_query::school_record;

constexpr uint32_t kTextControl = 0x212CDC1F, kNumberControl = 0x21325207, kValueControl = 0xC12CEA13;
constexpr uint32_t kValueProperty = 0xEA563AB0;
constexpr uint32_t kSliderProperties[] = {0x6A8CC9D6, 0x2AA6E8AD, 0x0AB3F6DB};
constexpr uint32_t kChanging[] = {24, 25, 33, 34, 99};
constexpr uint32_t kSentinelA = 0x5A5A5A5A, kSentinelB = 0x13572468;

template <typename R, typename... Args>
R This(std::uintptr_t function, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(function)(self, args...);
}

template <typename R, typename... Args>
R Cdecl(std::uintptr_t function, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(function)(args...);
}

uint32_t FloatBits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

// The text of a game string (begin and end at +4 and +8).
std::string TextOf(const void* text)
{
    if (!text)
        return "(null)";
    const char* const begin = At<const char*>(const_cast<void*>(text), 0x4);
    const char* const end = At<const char*>(const_cast<void*>(text), 0x8);
    return begin && end >= begin ? std::string(begin, end) : std::string("?");
}

// --- The stand-ins ---------------------------------------------------------------------------

struct Fake {
    void** vtable;
    int32_t references;
};

struct Property {
    uint32_t id;
    void* object;
};

void* g_windowVtable[128];
void* g_textVtable[8];
void* g_numberVtable[12];
void* g_sinkVtable[16];
Fake g_window, g_text, g_number, g_sink;
Fnv g_events;
Property g_properties[8];
std::string g_caption;
int32_t g_numberValue = 0;
Fnv g_sinkLines;
int32_t g_sinkCount = 0;

uint32_t __fastcall FakeAddRef(Fake* self, void*)
{
    return static_cast<uint32_t>(++self->references);
}

uint32_t __fastcall FakeRelease(Fake* self, void*)
{
    return static_cast<uint32_t>(--self->references);
}

void* __fastcall FakeSelf(Fake* self, void*)
{
    return self;
}

void* __fastcall FakeWindowOf(Fake*, void*)
{
    return &g_window;
}

void __fastcall FakeSlot23(Fake*, void*)
{
    g_events.Add(uint8_t{0x23});
}

uint8_t __fastcall FakeGetChildAs(Fake*, void*, uint32_t id, uint32_t iid, void** out)
{
    g_events.Add(uint8_t{0x25});
    g_events.Add(id);
    g_events.Add(iid);
    Fake* child = nullptr;
    if (iid == kTextControl)
        child = &g_text;
    else if (iid == kNumberControl || iid == kValueControl)
        child = &g_number;
    if (!child)
        return 0;
    ++child->references;
    *out = child;
    return 1;
}

uint8_t __fastcall FakeGetProperty(Fake*, void*, uint32_t id, void** out)
{
    g_events.Add(uint8_t{0x71});
    g_events.Add(id);
    for (const Property& p : g_properties) {
        if (p.object && p.id == id) {
            Virtual<uint32_t>(p.object, 0x4);
            *out = p.object;
            return 1;
        }
    }
    return 0;
}

uint8_t __fastcall FakeSetProperty(Fake*, void*, uint32_t id, void* object)
{
    g_events.Add(uint8_t{0x72});
    g_events.Add(id);
    g_events.Add(uint8_t{object != nullptr});
    if (object)
        Virtual<uint32_t>(object, 0x4);
    Property* place = nullptr;
    for (Property& p : g_properties) {
        if (p.object && p.id == id) {
            Virtual<uint32_t>(p.object, 0x8);
            p.object = nullptr;
        }
        if (!p.object && !place)
            place = &p;
    }
    if (object && place) {
        place->id = id;
        place->object = object;
    }
    return 1;
}

void __fastcall FakeSetCaption(Fake*, void*, const void* text)
{
    g_caption = TextOf(text);
    g_events.Add(uint8_t{0x04});
    g_events.Text(g_caption);
}

int32_t __fastcall FakeGetNumber(Fake*, void*)
{
    g_events.Add(uint8_t{0x05});
    return g_numberValue;
}

void __fastcall FakeNumberSet(Fake*, void*, int32_t value)
{
    g_events.Add(uint8_t{0x14});
    g_events.Add(value);
}

void __fastcall FakeNumberLow(Fake*, void*, int32_t value)
{
    g_events.Add(uint8_t{0x18});
    g_events.Add(value);
}

void __fastcall FakeNumberHigh(Fake*, void*, int32_t value)
{
    g_events.Add(uint8_t{0x20});
    g_events.Add(value);
}

void __fastcall FakeSinkLine(Fake*, void*, const char* text)
{
    const std::string line = text ? text : "(null)";
    g_sinkLines.Text(line);
    ++g_sinkCount;
}

void* PropertyObject(uint32_t id)
{
    for (const Property& p : g_properties)
        if (p.object && p.id == id)
            return p.object;
    return nullptr;
}

void ClearProperties()
{
    for (Property& p : g_properties) {
        if (p.object)
            Virtual<uint32_t>(p.object, 0x8);
        p.object = nullptr;
    }
}

void SetUpFakes()
{
    void* const addRef = reinterpret_cast<void*>(&FakeAddRef);
    void* const release = reinterpret_cast<void*>(&FakeRelease);
    for (void** table : {g_windowVtable, g_textVtable, g_numberVtable, g_sinkVtable}) {
        table[1] = addRef;
        table[2] = release;
    }
    g_windowVtable[3] = reinterpret_cast<void*>(&FakeSelf);
    g_windowVtable[23] = reinterpret_cast<void*>(&FakeSlot23);
    g_windowVtable[37] = reinterpret_cast<void*>(&FakeGetChildAs);
    g_windowVtable[113] = reinterpret_cast<void*>(&FakeGetProperty);
    g_windowVtable[114] = reinterpret_cast<void*>(&FakeSetProperty);
    g_textVtable[4] = reinterpret_cast<void*>(&FakeSetCaption);
    g_numberVtable[3] = reinterpret_cast<void*>(&FakeWindowOf);
    g_numberVtable[4] = reinterpret_cast<void*>(&FakeGetNumber);
    g_numberVtable[5] = reinterpret_cast<void*>(&FakeNumberSet);
    g_numberVtable[6] = reinterpret_cast<void*>(&FakeNumberLow);
    g_numberVtable[8] = reinterpret_cast<void*>(&FakeNumberHigh);
    g_sinkVtable[14] = reinterpret_cast<void*>(&FakeSinkLine);
    g_window = {g_windowVtable, 0};
    g_text = {g_textVtable, 0};
    g_number = {g_numberVtable, 0};
    g_sink = {g_sinkVtable, 0};
    for (Property& p : g_properties)
        p = {0, nullptr};
    g_numberValue = 0;
}

// A game value object (0x009166D4): an array (type bit 0x8000) by its
// dwords, any other by the eight bytes at +4. The reference count is left
// out.
void HashValue(Fnv& f, void* value)
{
    f.Add(uint8_t{value != nullptr});
    if (!value)
        return;
    char* const v = static_cast<char*>(value);
    const uint16_t type = At<uint16_t>(v, 0xE);
    const uint32_t count = At<uint32_t>(v, 0x10);
    f.Add(type);
    f.Add(count);
    if (type & 0x8000) {
        const uint32_t* const data = At<const uint32_t*>(v, 0x4);
        f.Add(uint8_t{data != nullptr});
        if (data && count < 0x100)
            f.Bytes(data, count * 4);
    } else {
        f.Bytes(v + 4, 8);
    }
}

void TakeProperties(Fnv& f)
{
    for (const uint32_t id : kSliderProperties)
        HashValue(f, PropertyObject(id));
    HashValue(f, PropertyObject(kValueProperty));
    ClearProperties();
}

// One filler through the table's reader 0x004D3A90 on the stand-in window:
// its answer, the caption, the properties it left.
uint32_t Fill(Fnv& f, uint32_t index, uint32_t fill, void* occupant)
{
    g_caption.clear();
    const uint8_t answer =
        Cdecl<uint8_t>(addr::SC4QueryTool_FillControl, &g_window, 0x1000 | index, fill, occupant) != 0;
    f.Add(index);
    f.Add(fill);
    f.Add(answer);
    f.Text(g_caption);
    TakeProperties(f);
    return answer;
}

bool Changing(uint32_t index)
{
    return std::find(std::begin(kChanging), std::end(kChanging), index) != std::end(kChanging);
}

// Fill 1, then fill 0 unless the filler changes the city.
uint32_t FillBoth(Fnv& f, uint32_t index, void* occupant)
{
    uint32_t yes = Fill(f, index, 1, occupant);
    if (!Changing(index))
        yes += Fill(f, index, 0, occupant);
    return yes;
}

// --- The city's buildings -------------------------------------------------------------------

// A school or hospital record as the fillers keep it: a game string (made
// by 0x00578390), the rest 0.
struct Record {
    char bytes[school_record::kSize];
    Record()
    {
        std::memset(bytes, 0, sizeof bytes);
        This<void*>(addr::SC4String_SC4String, &bytes[0]);
    }
    ~Record()
    {
        if (void* const buffer = At<void*>(&bytes[0], 0x4))
            Cdecl<void>(addr::SC4_Free, buffer);
    }
    Record(const Record&) = delete;
    Record& operator=(const Record&) = delete;
    float Share(std::size_t at) { return At<float>(&bytes[0], at); }
};

bool HasRecord(void* occupant, std::size_t slot)
{
    Record record;
    return Virtual<uint8_t>(Global<void*>(addr::SC4_Service43DA4), slot, occupant, &record.bytes[0]) != 0;
}

struct Building {
    void* lot;
    void* occupant;
};

struct Sample {
    std::vector<Building> buildings; // up to 24, every third cell
    void* other = nullptr;           // a building that is neither school nor hospital
    std::vector<void*> schools, hospitals, capacities, plants, producers, usedKnown, items;
};

// Does slot 28 write both of its numbers?
bool CapacityWrites(void* occupant)
{
    void* const residential = Global<void*>(addr::SC4_Service43DA4);
    uint32_t a[2] = {kSentinelA, kSentinelA}, b[2] = {kSentinelB, kSentinelB};
    Virtual<uint8_t>(residential, 0x70, occupant, &a[0], &a[1]);
    Virtual<uint8_t>(residential, 0x70, occupant, &b[0], &b[1]);
    return a[0] == b[0] && a[1] == b[1];
}

// Does the plumbing simulator's slot 24 answer and write the amount used?
bool UsedWrites(void* occupant)
{
    void* const plumbing = Global<void*>(addr::SC4_Service43D94);
    uint32_t a = kSentinelA, b = kSentinelB;
    const auto ask = [&](uint32_t* used) {
        return Virtual<uint8_t>(plumbing, 0x60, occupant, static_cast<uint32_t*>(nullptr),
                                static_cast<uint32_t*>(nullptr), static_cast<uint32_t*>(nullptr),
                                static_cast<uint8_t*>(nullptr), used) != 0;
    };
    return ask(&a) && ask(&b) && a == b;
}

bool IsProducer(void* occupant)
{
    uint32_t production = 0;
    return Virtual<uint8_t>(Global<void*>(addr::SC4_Service43D94), 0x60, occupant, &production,
                            static_cast<uint32_t*>(nullptr), static_cast<uint32_t*>(nullptr),
                            static_cast<uint8_t*>(nullptr), static_cast<uint32_t*>(nullptr)) != 0;
}

bool IsPlant(void* occupant)
{
    float share = 0.0f;
    return Virtual<uint8_t>(Global<void*>(addr::SC4_Service43DA0), 0xC8, occupant, &share) != 0;
}

// Does the budget simulator have the occupant's item (purpose 0xAA59670C),
// with the department of it?
bool HasItem(void* occupant)
{
    void* const budget = Global<void*>(addr::SC4_BudgetSimulator);
    uint32_t item[8] = {};
    if (!Virtual<uint8_t>(budget, 0xCC, Virtual<void*>(occupant, 0x18), uint32_t{0xAA59670C}, &item[0]))
        return false;
    return Virtual<void*>(budget, 0x70, item[0]) != nullptr;
}

void Collect(Sample& out, int32_t sizeX, int32_t sizeZ)
{
    void* const lotManager = Global<void*>(addr::SC4_LotManager);
    if (!lotManager)
        return;
    std::vector<void*> lots;
    for (int32_t z = 0; z < sizeZ; ++z) {
        for (int32_t x = 0; x < sizeX; ++x) {
            void* const lot = Virtual<void*>(lotManager, 0x18, x, z, uint32_t{0});
            void* const building = lot ? Virtual<void*>(lot, 0xC0) : nullptr;
            void* const occupant = building ? Virtual<void*>(building, 0xC) : nullptr;
            if (!occupant || std::find(lots.begin(), lots.end(), lot) != lots.end())
                continue;
            lots.push_back(lot);
            if (out.buildings.size() < 24 && x % 3 == 0 && z % 3 == 0)
                out.buildings.push_back({lot, occupant});
            const bool school = HasRecord(occupant, 0x68);
            const bool hospital = HasRecord(occupant, 0x6C);
            if (school && out.schools.size() < 3)
                out.schools.push_back(occupant);
            if (hospital && out.hospitals.size() < 3)
                out.hospitals.push_back(occupant);
            if (!school && !hospital && !out.other)
                out.other = occupant;
            if ((school || hospital) && out.capacities.size() < 4 && CapacityWrites(occupant))
                out.capacities.push_back(occupant);
            if (out.plants.size() < 3 && IsPlant(occupant))
                out.plants.push_back(occupant);
            if (out.producers.size() < 3 && IsProducer(occupant)) {
                out.producers.push_back(occupant);
                if (UsedWrites(occupant))
                    out.usedKnown.push_back(occupant);
            }
            if (out.items.size() < 4 && HasItem(occupant))
                out.items.push_back(occupant);
        }
    }
}

std::string Counts(const Sample& s)
{
    char text[200];
    std::snprintf(text, sizeof text,
                  "%d buildings, other %d, %d schools, %d hospitals, %d capacities, %d plants, %d producers (%d with "
                  "the amount), %d budget items",
                  static_cast<int32_t>(s.buildings.size()), s.other != nullptr, static_cast<int32_t>(s.schools.size()),
                  static_cast<int32_t>(s.hospitals.size()), static_cast<int32_t>(s.capacities.size()),
                  static_cast<int32_t>(s.plants.size()), static_cast<int32_t>(s.producers.size()),
                  static_cast<int32_t>(s.usedKnown.size()), static_cast<int32_t>(s.items.size()));
    return text;
}

// --- The parts ---------------------------------------------------------------------------------

uint32_t Part(const char* name, Fnv& f, int32_t yes)
{
    f.Add(g_events.h);
    log::Write("autotest: query3: %s %08X (%d fillers said yes)", name, f.h, yes);
    return f.h;
}

uint32_t DriveSchools(const Sample& s)
{
    Fnv f;
    g_events = Fnv();
    int32_t yes = 0;
    constexpr uint32_t kRecordFillers[] = {24, 25, 28, 30, 32, 33, 34, 37};
    constexpr uint32_t kCapacityFillers[] = {26, 27, 29, 31, 35, 36};
    std::vector<void*> all = s.schools;
    all.insert(all.end(), s.hospitals.begin(), s.hospitals.end());
    if (s.other)
        all.push_back(s.other);
    for (void* const occupant : all)
        for (const uint32_t index : kRecordFillers)
            yes += static_cast<int32_t>(FillBoth(f, index, occupant));
    for (void* const occupant : s.capacities)
        for (const uint32_t index : kCapacityFillers)
            yes += static_cast<int32_t>(FillBoth(f, index, occupant));
    return Part("schools", f, yes);
}

uint32_t DriveGarbage(const Sample& s)
{
    Fnv f;
    g_events = Fnv();
    int32_t yes = 0;
    for (const Building& b : s.buildings)
        for (const uint32_t index : {42u, 43u, 44u, 51u, 95u, 96u})
            yes += static_cast<int32_t>(FillBoth(f, index, b.occupant));
    for (const std::uintptr_t helper : {addr::SC4QueryTool_FillGarbageLandfilled, addr::SC4QueryTool_FillLandfillCost}) {
        for (const uint32_t fill : {1u, 0u}) {
            g_caption.clear();
            f.Add(Cdecl<uint8_t>(helper, &g_window, uint32_t{0x1077}, fill, int32_t{3}, int32_t{4}));
            f.Text(g_caption);
        }
    }
    return Part("garbage", f, yes);
}

uint32_t DrivePower(const Sample& s)
{
    Fnv f;
    g_events = Fnv();
    int32_t yes = 0;
    constexpr uint32_t kFillers[] = {46, 47, 48, 49, 53, 54, 55, 56, 97, 98, 118};
    std::vector<void*> all;
    for (const Building& b : s.buildings)
        all.push_back(b.occupant);
    all.insert(all.end(), s.plants.begin(), s.plants.end());
    for (void* const occupant : all)
        for (const uint32_t index : kFillers)
            yes += static_cast<int32_t>(FillBoth(f, index, occupant));
    for (void* const plant : s.plants)
        yes += static_cast<int32_t>(Fill(f, 99, 1, plant));
    return Part("power", f, yes);
}

uint32_t DriveWater(const Sample& s)
{
    Fnv f;
    g_events = Fnv();
    int32_t yes = 0;
    std::vector<void*> all;
    for (const Building& b : s.buildings)
        all.push_back(b.occupant);
    all.insert(all.end(), s.producers.begin(), s.producers.end());
    for (void* const occupant : all)
        for (const uint32_t index : {60u, 61u, 64u, 115u, 119u})
            yes += static_cast<int32_t>(FillBoth(f, index, occupant));
    for (void* const occupant : s.usedKnown)
        for (const uint32_t index : {59u, 62u})
            yes += static_cast<int32_t>(FillBoth(f, index, occupant));
    return Part("water", f, yes);
}

uint32_t DriveTransit(const Sample& s)
{
    Fnv f;
    g_events = Fnv();
    int32_t yes = 0;
    for (const Building& b : s.buildings)
        for (const uint32_t index : {66u, 67u, 68u, 69u})
            yes += static_cast<int32_t>(FillBoth(f, index, b.occupant));
    return Part("transit", f, yes);
}

uint32_t DriveBudget(const Sample& s)
{
    Fnv f;
    g_events = Fnv();
    int32_t yes = static_cast<int32_t>(FillBoth(f, 70, nullptr));
    for (const Building& b : s.buildings)
        for (const uint32_t index : {16u, 108u})
            yes += static_cast<int32_t>(FillBoth(f, index, b.occupant));
    for (void* const occupant : s.items)
        for (const uint32_t index : {71u, 73u})
            yes += static_cast<int32_t>(FillBoth(f, index, occupant));
    return Part("budget", f, yes);
}

uint32_t DriveLots(const Sample& s, int32_t sizeX, int32_t sizeZ)
{
    Fnv f;
    g_events = Fnv();
    int32_t yes = 0;
    for (const Building& b : s.buildings)
        for (const uint32_t index : {19u, 74u, 81u, 82u, 86u, 88u, 92u, 120u, 123u, 124u, 125u, 126u})
            yes += static_cast<int32_t>(FillBoth(f, index, b.occupant));
    // The traffic at a cell, held inside the city.
    const int32_t cells[][2] = {{-5, -5}, {0, 0}, {sizeX / 2, sizeZ / 3}, {sizeX + 7, 2}, {3, sizeZ}};
    for (const auto& c : cells) {
        for (const uint32_t fill : {1u, 0u}) {
            g_caption.clear();
            f.Add(Cdecl<uint8_t>(addr::SC4QueryTool_FillTrafficCellPercent, &g_window, uint32_t{0x1078}, fill, c[0],
                                 c[1]));
            f.Text(g_caption);
        }
    }
    // The neighbours across the edges.
    int32_t found = 0;
    std::string first;
    const auto edge = [&](int32_t x, int32_t z) {
        for (uint32_t type = 0; type <= 12; ++type) {
            g_caption.clear();
            if (!Cdecl<uint8_t>(addr::SC4QueryTool_FillNeighbourMayor, &g_window, uint32_t{0x1079}, uint32_t{1}, x, z,
                                type))
                continue;
            ++found;
            f.Add(x);
            f.Add(z);
            f.Add(type);
            f.Text(g_caption);
            if (first.empty())
                first = g_caption;
            g_caption.clear();
            f.Add(Cdecl<uint8_t>(addr::SC4QueryTool_FillNeighbourName, &g_window, uint32_t{0x107A}, uint32_t{1}, x, z,
                                 type));
            f.Text(g_caption);
        }
    };
    for (int32_t z = 0; z < sizeZ; ++z) {
        edge(-1, z);
        edge(sizeX, z);
    }
    for (int32_t x = 0; x < sizeX; ++x) {
        edge(x, -1);
        edge(x, sizeZ);
    }
    g_caption.clear();
    f.Add(Cdecl<uint8_t>(addr::SC4QueryTool_FillNeighbourName, &g_window, uint32_t{0x107A}, uint32_t{1}, int32_t{5},
                         int32_t{5}, uint32_t{0}));
    f.Text(g_caption);
    const std::string empty = g_caption;
    f.Add(found);
    f.Add(g_events.h);
    log::Write("autotest: query3: lots %08X (%d fillers said yes; %d neighbour places, first mayor '%s', no city '%s')",
               f.h, yes, found, first.c_str(), empty.c_str());
    return f.h;
}

uint32_t DriveLog(const Sample& s, int32_t sizeX, int32_t sizeZ)
{
    Fnv f;
    void*& sink = Global<void*>(addr::SC4QueryTool_LogSink);
    void* const before = sink;
    sink = &g_sink;
    g_sinkLines = Fnv();
    g_sinkCount = 0;
    std::vector<std::pair<int32_t, int32_t>> cells = {{0, 0}, {sizeX / 2, sizeZ / 2}};
    if (!s.buildings.empty()) {
        int32_t box[4] = {};
        Virtual<void>(s.buildings[0].lot, 0x38, static_cast<int32_t*>(box));
        cells.push_back({box[0], box[1]});
    }
    constexpr uint32_t kTypes[] = {0x1010, 0x1020, 0x1030, 0x3110, 0x3120, 0x3130,
                                   0x3320, 0x3330, 0x4100, 0x4200, 0x4300, 0x4400};
    for (const auto& c : cells)
        for (const uint32_t type : kTypes)
            Cdecl<void>(addr::SC4QueryTool_LogDesirability, c.first, c.second, type);
    sink = before;
    f.Add(g_sinkLines.h);
    f.Add(g_sinkCount);
    log::Write("autotest: query3: log %08X (%d lines for %d cells)", f.h, g_sinkCount,
               static_cast<int32_t>(cells.size()));
    return f.h;
}

// --- The commit ----------------------------------------------------------------------------------

uint32_t CommitRecord(void* occupant, std::size_t slot, uint32_t first, uint32_t second, uint32_t school,
                      const char* name)
{
    Fnv f;
    if (!occupant) {
        log::Write("autotest: query3: commit: no %s", name);
        return f.h;
    }
    void* const residential = Global<void*>(addr::SC4_Service43DA4);
    float was[2], mid[2], now[2], after[2];
    const auto read = [&](float* out) {
        Record record;
        Virtual<uint8_t>(residential, slot, occupant, &record.bytes[0]);
        out[0] = record.Share(school_record::kShare);
        out[1] = record.Share(school_record::kShareSecond);
    };
    read(was);
    g_numberValue = 50;
    const uint32_t a = Fill(f, first, 0, occupant);
    read(mid);
    g_numberValue = 30;
    const uint32_t b = Fill(f, second, 0, occupant);
    read(now);
    float back[2] = {was[0], was[1]};
    Virtual<uint8_t>(residential, 0x4C, occupant, &back[0], &back[1], school);
    read(after);
    g_numberValue = 0;
    for (const float* v : {was, mid, now, after}) {
        f.Add(FloatBits(v[0]));
        f.Add(FloatBits(v[1]));
    }
    log::Write("autotest: query3: commit %s %08X (shares %08X %08X; set %u to %08X %08X, %u to %08X %08X; back %08X "
               "%08X)",
               name, f.h, FloatBits(was[0]), FloatBits(was[1]), a, FloatBits(mid[0]), FloatBits(mid[1]), b,
               FloatBits(now[0]), FloatBits(now[1]), FloatBits(after[0]), FloatBits(after[1]));
    return f.h;
}

uint32_t CommitPlant(void* plant)
{
    Fnv f;
    if (!plant) {
        log::Write("autotest: query3: commit: no plant");
        return f.h;
    }
    void* const power = Global<void*>(addr::SC4_Service43DA0);
    float was = 0.0f, now = 0.0f, after = 0.0f;
    Virtual<uint8_t>(power, 0xC8, plant, &was);
    g_numberValue = 50;
    const uint32_t stored = Fill(f, 99, 0, plant);
    Virtual<uint8_t>(power, 0xC8, plant, &now);
    float back = was;
    Virtual<uint8_t>(power, 0xCC, plant, &back);
    Virtual<uint8_t>(power, 0xC8, plant, &after);
    g_numberValue = 0;
    f.Add(FloatBits(was));
    f.Add(FloatBits(now));
    f.Add(FloatBits(after));
    log::Write("autotest: query3: commit plant %08X (share %08X, set %u to %08X, back %08X)", f.h, FloatBits(was),
               stored, FloatBits(now), FloatBits(after));
    return f.h;
}

void Drive(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_Q3") || !game::IsBuild641())
        return;
    if (!day.city || !Global<void*>(addr::SC4_City) || !Global<void*>(addr::SC4_LotManager)) {
        log::Write("autotest: query3: no city");
        return;
    }
    const int parts = EnvInt("OPENSC4_TEST_Q3_PARTS", 255);
    const bool commit = EnvInt("OPENSC4_TEST_Q3_COMMIT", 0) != 0;
    SetUpFakes();
    const int32_t sizeX = static_cast<int32_t>(day.city->CellCountX());
    const int32_t sizeZ = static_cast<int32_t>(day.city->CellCountZ());
    Sample sample;
    Collect(sample, sizeX, sizeZ);
    const std::string counts = Counts(sample);
    log::Write("autotest: query3: %s", counts.c_str());
    Fnv total;
    total.Text(counts);
    if (parts & 1)
        total.Add(DriveSchools(sample));
    if (parts & 2)
        total.Add(DriveGarbage(sample));
    if (parts & 4)
        total.Add(DrivePower(sample));
    if (parts & 8)
        total.Add(DriveWater(sample));
    if (parts & 16)
        total.Add(DriveTransit(sample));
    if (parts & 32)
        total.Add(DriveBudget(sample));
    if (parts & 64)
        total.Add(DriveLots(sample, sizeX, sizeZ));
    if (parts & 128)
        total.Add(DriveLog(sample, sizeX, sizeZ));
    if (commit) {
        total.Add(CommitRecord(sample.schools.empty() ? nullptr : sample.schools[0], 0x68, 24, 25, 1, "school"));
        total.Add(CommitRecord(sample.hospitals.empty() ? nullptr : sample.hospitals[0], 0x6C, 33, 34, 0, "hospital"));
        total.Add(CommitPlant(sample.plants.empty() ? nullptr : sample.plants[0]));
    }
    ClearProperties();
    total.Add(g_window.references);
    total.Add(g_text.references);
    total.Add(g_number.references);
    std::snprintf(g_token, sizeof g_token, " query3 %08X", total.h);
    log::Write("autotest: query3: total %08X (references %d %d %d)", total.h, g_window.references,
               g_text.references, g_number.references);
}

const DayHook kQuery3{"query3", 63, &Drive};

void Fingerprint(cISC4City*, std::string& line)
{
    line += g_token;
}

const FingerprintPart kFingerprint{"query3", 23, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
