// The region's part of the autotest (src/core/autotest_hooks.h;
// docs/region.md): what the open region (cSC4Region) answers once a city
// has loaded, as one token in the fingerprint.
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <GZServPtrs.h>
#include <cISC4App.h>
class cIGZVariant;
#include <cIGZOStream.h>
#include <cIGZSerializable.h>
#include <cIGZString.h>
#include <cISC4City.h>
#include <cRZBaseString.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace opensc4::autotest {
namespace {

std::string g_token;

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
};

// An output stream that keeps only a checksum and a count of what a save
// record writes to it.
class SumStream final : public cIGZOStream {
public:
    bool QueryInterface(uint32_t, void**) override { return false; }
    uint32_t AddRef() override { return 1; }
    uint32_t Release() override { return 1; }
    void Flush() override {}
    bool SetSint8(int8_t v) override { return Put(v); }
    bool SetUint8(uint8_t v) override { return Put(v); }
    bool SetSint16(int16_t v) override { return Put(v); }
    bool SetUint16(uint16_t v) override { return Put(v); }
    bool SetSint32(int32_t v) override { return Put(v); }
    bool SetUint32(uint32_t v) override { return Put(v); }
    bool SetSint64(int64_t v) override { return Put(v); }
    bool SetUint64(uint64_t v) override { return Put(v); }
    bool SetFloat32(float v) override { return Put(v); }
    bool SetFloat64(double v) override { return Put(v); }
    bool SetRZCharStr(char const* text) override { return SetVoid(text, static_cast<uint32_t>(std::strlen(text))); }
    bool SetGZStr(cIGZString const& text) override
    {
        const uint32_t length = text.Strlen();
        Put(length);
        return SetVoid(text.ToChar(), length);
    }
    bool SetGZSerializable(cIGZSerializable const& object) override
    {
        return const_cast<cIGZSerializable&>(object).Write(*this);
    }
    bool SetVoid(void const* data, uint32_t size) override
    {
        sum.Bytes(data, size);
        bytes += size;
        return true;
    }
    int32_t GetError() override { return 0; }
    int32_t SetUserData(cIGZVariant*) override { return 0; }
    int32_t GetUserData() override { return 0; }

    Fnv sum;
    uint32_t bytes = 0;

private:
    template <typename T>
    bool Put(const T& value)
    {
        return SetVoid(&value, sizeof value);
    }
};

// A regional city (cSC4RegionalCity; docs/region.md) into a checksum: its
// save record as its own Write gives it (through the serialisable part,
// interface 0xE4FDA3D4), and what its getters answer: the three interfaces,
// population by type (slot 9) and its extrapolation (17), tax rates (16),
// budget, income and the commodity balances (50, 52, 64), the tutorial
// flag (67), names and the file's name (31 to 39), hemisphere (49) and the
// number of connections (21).
void AddCity(Fnv& f, void* city)
{
    static const uint32_t kTypes[] = {0x1010, 0x1020, 0x1030, 0x3110, 0x3120, 0x3130, 0x3320,
                                      0x3330, 0x4100, 0x4200, 0x4300, 0x4400, 0x9999};
    for (const uint32_t id : {0x69D2BE1Du, 0x452294AAu, 0xE4FDA3D4u, 0x12345678u}) {
        void* part = nullptr;
        const uint8_t ok = Virtual<uint8_t>(city, 0x0, id, &part);
        f.Add(ok);
        if (ok && part) {
            f.Add(static_cast<int32_t>(static_cast<char*>(part) - static_cast<char*>(city)));
            if (id == 0xE4FDA3D4u) {
                SumStream stream;
                f.Add(Virtual<uint8_t>(part, 0xC, static_cast<cIGZOStream*>(&stream)));
                f.Add(Virtual<uint32_t>(part, 0x14));
                f.Add(stream.sum.h);
                f.Add(stream.bytes);
            }
            Virtual<uint32_t>(city, 0x8);
        }
    }
    for (const uint32_t type : kTypes) {
        f.Add(Virtual<int32_t>(city, 0x24, type));
        f.Add(Virtual<int32_t>(city, 0x44, type));
        f.Add(Virtual<double>(city, 0x40, type));
    }
    f.Add(Virtual<double>(city, 0xC8));
    f.Add(Virtual<double>(city, 0xD0));
    for (uint32_t commodity = 1; commodity <= 3; ++commodity)
        f.Add(Virtual<double>(city, 0x100, commodity));
    f.Add(Virtual<uint8_t>(city, 0x10C));
    f.Add(Virtual<uint32_t>(city, 0xC4));
    for (const std::size_t slot : {0x7Cu, 0x84u, 0x8Cu, 0x94u, 0x9Cu}) {
        cRZBaseString text;
        Virtual<void>(city, slot, static_cast<cIGZString*>(&text));
        std::string value = text.ToChar() ? text.ToChar() : "";
        if (slot == 0x7C)
            value.erase(0, value.find_last_of('\\') + 1);
        f.Bytes(value.data(), value.size());
    }
    void** const sentinel = static_cast<void**>(Virtual<void*>(NodeAllocatorAt(addr::SC4List_NodeAllocator3), 0xC, 0xCu));
    sentinel[0] = sentinel;
    sentinel[1] = sentinel;
    void* connections = sentinel;
    Virtual<void>(city, 0x54, &connections);
    uint32_t count = 0;
    for (void** node = static_cast<void**>(sentinel[0]); node != connections; node = static_cast<void**>(node[0]))
        ++count;
    f.Add(count);
    reinterpret_cast<void(__thiscall*)(void*)>(addr::SC4List_ClearReleasing)(&connections);
    Virtual<void>(NodeAllocatorAt(addr::SC4List_NodeAllocator3), 0x10, connections);
}

// The text of one of the game's strings (begin at +4, end at +8).
std::string Text(void* string)
{
    const char* const begin = At<const char*>(string, 0x4);
    const char* const end = At<const char*>(string, 0x8);
    return begin && end >= begin ? std::string(begin, end) : std::string();
}

uint32_t Count(void* head)
{
    uint32_t n = 0;
    for (void** node = static_cast<void**>(static_cast<void**>(head)[0]); node != head;
         node = static_cast<void**>(node[0]))
        ++n;
    return n;
}

// Name (slot 3; the folder, slot 5, only in the log, since test runs use
// different folders), the settings fields (+0x30 to +0x3F) with
// what slots 20 and 21 answer, the bounding box of the layout (slot 24), the
// layout tiles (slot 23), the cities waiting in the list at +0x40 with their
// positions and sizes, and for every cell of the box (the box holds tile
// origins, so four cells more each way) the position and size of the city
// slot 11 finds there; `cells` counts the cities found at their own origin.
std::string Describe(void* region, const char* when)
{
    Fnv settings;
    settings.Bytes(static_cast<char*>(region) + 0x30, 0x10);
    settings.Add(Virtual<uint32_t>(region, 0x50));
    uint8_t water[2] = {};
    Virtual<void>(region, 0x54, &water[0], &water[1]);
    settings.Add(water);

    int32_t box[4] = {};
    Virtual<void>(region, 0x60, &box[0]);

    void* tiles[3] = {};
    Virtual<void>(region, 0x5C, &tiles[0]);
    Fnv tileSum;
    const std::size_t tileBytes = static_cast<char*>(tiles[1]) - static_cast<char*>(tiles[0]);
    tileSum.Bytes(tiles[0], tileBytes);
    if (tiles[0])
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(tiles[0]);

    Fnv cities;
    void* const head = At<void*>(region, 0x40);
    for (void** node = static_cast<void**>(static_cast<void**>(head)[0]); node != head;
         node = static_cast<void**>(node[0])) {
        int32_t place[4] = {};
        Virtual<void>(node[2], 0x14, &place[0], &place[1]);
        Virtual<void>(node[2], 0x1C, &place[2], &place[3]);
        cities.Add(place);
    }

    Fnv cells;
    uint32_t found = 0;
    for (int32_t y = box[1]; y < box[3] + 4; ++y)
        for (int32_t x = box[0]; x < box[2] + 4; ++x) {
            void** const holder = Virtual<void**>(region, 0x2C, x, y);
            int32_t place[5] = {};
            if (holder && *holder) {
                place[0] = 1;
                Virtual<void>(*holder, 0x14, &place[1], &place[2]);
                Virtual<void>(*holder, 0x1C, &place[3], &place[4]);
                found += place[1] == x && place[2] == y;
            }
            cells.Add(place);
        }

    char text[200];
    std::snprintf(text, sizeof text, "%s set %08X box %d %d %d %d tiles %u %08X list %u %08X map %u cells %u %08X",
                  Text(Virtual<void*>(region, 0xC)).c_str(), settings.h, box[0], box[1], box[2], box[3],
                  static_cast<unsigned>(tileBytes / 12), tileSum.h, Count(head), cities.h,
                  At<uint32_t>(region, 0x60), found, cells.h);
    log::Write("autotest: region %s (%s): %s", when, Text(Virtual<void*>(region, 0x14)).c_str(), text);
    return text;
}

void* Region()
{
    cISC4AppPtr app;
    return app && game::IsBuild641() ? static_cast<void*>(app->GetRegion()) : nullptr;
}

// OPENSC4_TEST_REGION_DRIVE=1: a second region object on the open region's
// folder, worked through the class: made, named, started (settings, layout
// and cities read again), asked for all its cities and its interface, its
// terrain type set and put back, one city read from its file again, the
// settings, the layout and the folder read once more, shut down (which
// writes region.ini) and released.
std::string DriveSecond(void* open)
{
    void* const memory = reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0x64);
    if (!memory)
        return " 2nd none";
    void* const region = reinterpret_cast<void*(__thiscall*)(void*)>(addr::cSC4Region_Construct)(memory);
    Virtual<uint32_t>(region, 0x4);
    std::string made = Describe(region, "second, made");
    Virtual<void>(region, 0x18, Virtual<void*>(open, 0x14));
    const uint32_t started = Virtual<uint32_t>(region, 0x20) & 0xFF;
    std::string text = " 2nd " + std::to_string(BytesChecksum(made.data(), made.size())) + " " +
                       std::to_string(started) + " " + Describe(region, "second, started");
    Fnv citySum;
    for (int32_t* tile = At<int32_t*>(region, 0x44); tile != At<int32_t*>(region, 0x48); tile += 3)
        if (void** const holder = Virtual<void**>(region, 0x2C, tile[0], tile[1]); holder && *holder)
            AddCity(citySum, *holder);
    text += " city " + std::to_string(citySum.h);

    void** const sentinel = static_cast<void**>(Virtual<void*>(NodeAllocatorAt(addr::SC4List_NodeAllocator4), 0xC, 0xCu));
    sentinel[0] = sentinel;
    sentinel[1] = sentinel;
    void* list = sentinel;
    Virtual<void>(region, 0x44, &list);
    const uint32_t all = Count(list);
    reinterpret_cast<void(__thiscall*)(void*)>(addr::SC4List_ClearReleasing4)(&list);
    Virtual<void>(NodeAllocatorAt(addr::SC4List_NodeAllocator4), 0x10, list);

    void* same = nullptr;
    void* other = nullptr;
    const uint32_t asked = Virtual<uint8_t>(region, 0x0, 0x89D2BE26u, &same) * 2u +
                           Virtual<uint8_t>(region, 0x0, 0x12345678u, &other);
    if (same)
        Virtual<uint32_t>(region, 0x8);

    const uint32_t terrain = At<uint32_t>(region, 0x34);
    const uint32_t set = Virtual<uint32_t>(region, 0x4C, uint32_t{1});
    const uint32_t height = Virtual<uint32_t>(region, 0x50);
    Virtual<uint32_t>(region, 0x4C, terrain);

    uint32_t reloaded = 0;
    if (int32_t* const tile = At<int32_t*>(region, 0x44); tile != At<int32_t*>(region, 0x48))
        if (void* holder = Virtual<void*>(region, 0x2C, tile[0], tile[1])) {
            Virtual<uint32_t>(region, 0x3C, &holder);
            reloaded = 1;
        }
    Virtual<uint32_t>(region, 0x1C);
    reinterpret_cast<uint32_t(__thiscall*)(void*)>(addr::cSC4Region_LoadLayout)(region);
    reinterpret_cast<uint32_t(__thiscall*)(void*)>(addr::cSC4Region_MatchCities)(region);
    text += " all " + std::to_string(all) + " qi " + std::to_string(asked) + " terrain " + std::to_string(set) + " " +
            std::to_string(height) + " re " + std::to_string(reloaded) + " " + Describe(region, "second, read again");

    const uint32_t stopped = Virtual<uint32_t>(region, 0x24) & 0xFF;
    text += " off " + std::to_string(stopped) + " " + Describe(region, "second, shut down");
    Virtual<uint32_t>(region, 0x8);
    return text;
}

// OPENSC4_TEST_REGION_CREATE=1: a new region made as the game makes one (a
// new folder with a layout picture drawn from the built-in ones), started,
// described and deleted again, folder and all.
std::string DriveCreate()
{
    cRZBaseString name("OpenSC4 region test");
    void* const region = reinterpret_cast<void*(__cdecl*)(cIGZString*)>(addr::cSC4Region_Create)(&name);
    if (!region)
        return " new none";
    Virtual<uint32_t>(region, 0x4);
    const std::string folder = Text(Virtual<void*>(region, 0x14));
    const uint32_t started = Virtual<uint32_t>(region, 0x20) & 0xFF;
    std::string text = " new " + std::to_string(started) + " " + Describe(region, "created");
    Virtual<uint32_t>(region, 0x28);
    text += " gone " + Describe(region, "created, deleted");
    Virtual<uint32_t>(region, 0x8);
    log::Write("autotest: region created in \"%s\" and deleted", folder.c_str());
    return text;
}

// One of the game's own strings, empty, and its end.
struct GameText {
    std::uintptr_t vtable = addr::SC4String_Vtable;
    char* begin = nullptr;
    char* end = nullptr;
    char* capacity = nullptr;
    uint32_t zero = 0;

    GameText()
    {
        begin = end = reinterpret_cast<char*(__cdecl*)(uint32_t)>(addr::SC4_stl_allocate)(8);
        capacity = begin + 8;
        *begin = 0;
    }
    ~GameText()
    {
        if (begin)
            reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(begin);
    }
    explicit GameText(const char* text) : GameText()
    {
        const std::size_t size = std::strlen(text);
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(begin);
        begin = reinterpret_cast<char*(__cdecl*)(uint32_t)>(addr::SC4_stl_allocate)(static_cast<uint32_t>(size + 1));
        std::memcpy(begin, text, size + 1);
        end = begin + size;
        capacity = end + 1;
    }
    GameText(const GameText&) = delete;
    GameText& operator=(const GameText&) = delete;
    std::string FileName() const
    {
        const std::string text(begin, end);
        return text.substr(text.find_last_of('\\') + 1);
    }
};

// The city file helpers (0x005DBC10 on; docs/region.md) on the file of the
// first tile's city: opened as a segment, its version, its name text, its
// regional city record's bytes, closed; read as a regional city; the file
// names the game would give a city of that name with its serial number, with
// none and with another; a new serial number without its random bits.
std::string DriveFiles(void* region)
{
    int32_t* const tile = At<int32_t*>(region, 0x44);
    void** const holder = tile != At<int32_t*>(region, 0x48) ? Virtual<void**>(region, 0x2C, tile[0], tile[1]) : nullptr;
    if (!holder || !*holder)
        return " files none";
    void* const city = *holder;
    GameText path, name;
    Virtual<void>(city, 0x7C, static_cast<void*>(&path));
    Virtual<void>(city, 0x84, static_cast<void*>(&name));
    const uint32_t serial = Virtual<uint32_t>(city, 0x6C);

    Fnv f;
    if (void* const segment =
            reinterpret_cast<void*(__cdecl*)(void*, uint32_t, uint32_t)>(addr::SC4CityFiles_OpenSegment)(&path, 0, 1)) {
        f.Add(reinterpret_cast<uint32_t(__cdecl*)(void*)>(addr::SC4CityFiles_GetVersion)(segment));
        GameText text;
        f.Add(reinterpret_cast<uint8_t(__cdecl*)(void*, uint32_t, uint32_t, void*)>(addr::SC4Resource_LoadText)(
            segment, 0x6AAEE931, 0, &text));
        f.Bytes(text.begin, text.end - text.begin);
        uint32_t key[3] = {0xCA027EDB, 0xCA027EE1, 0};
        char* bytes[3] = {};
        f.Add(reinterpret_cast<uint8_t(__cdecl*)(void*, uint32_t*, void*, uint32_t)>(addr::SC4CityFiles_ReadRecordToVector)(
            segment, key, bytes, 0x100000));
        f.Bytes(bytes[0], bytes[1] - bytes[0]);
        f.Add(static_cast<uint32_t>(bytes[1] - bytes[0]));
        if (bytes[0])
            reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(bytes[0]);
        f.Add(reinterpret_cast<uint8_t(__cdecl*)(void*)>(addr::SC4CityFiles_CloseSegment)(segment));
    } else {
        f.Add(uint8_t{0xEE});
    }

    void* loaded = nullptr;
    f.Add(reinterpret_cast<uint8_t(__cdecl*)(void*, void**)>(addr::SC4RegionalCity_LoadFromFile)(&path, &loaded));
    if (loaded) {
        AddCity(f, loaded);
        Virtual<uint32_t>(loaded, 0x10);
        Virtual<uint32_t>(loaded, 0x8);
    }

    std::string names;
    for (const uint32_t number : {serial, uint32_t{0}, serial + 16}) {
        GameText out;
        f.Add(reinterpret_cast<uint8_t(__cdecl*)(void*, uint32_t, void*)>(addr::SC4RegionalCity_MakeSavePath)(
            &name, number, &out));
        const std::string file = out.FileName();
        f.Bytes(file.data(), file.size());
        names += " '" + file + "'";
    }
    const uint32_t fresh = reinterpret_cast<uint32_t(__cdecl*)()>(addr::SC4_NewCitySerial)();
    log::Write("autotest: region files of '%s':%s, new serial %08X", path.FileName().c_str(), names.c_str(), fresh);
    return " files " + std::to_string(f.h) + " " + std::to_string(fresh >> 4);
}

// A picture (the game's image buffer) into a checksum: its area and every
// pixel.
void AddPicture(Fnv& f, void* picture)
{
    f.Add(picture != nullptr);
    if (!picture)
        return;
    const int32_t* const area = Virtual<int32_t*>(picture, 0x30);
    f.Bytes(area, 16);
    if (Virtual<uint8_t>(picture, 0x18, uint32_t{0x40})) {
        for (int32_t y = area[1]; y < area[3]; ++y)
            for (int32_t x = area[0]; x < area[2]; ++x)
                f.Add(Virtual<uint32_t>(picture, 0x54, x, y));
        Virtual<uint8_t>(picture, 0x1C, uint32_t{0x40});
    }
}

// The region pictures of the first tile's city file (0x005DCA10 on;
// docs/region.md): all four read, then the first two only, and the average
// of the first with itself and, when they are one size, with the second.
std::string DrivePictures(void* region)
{
    int32_t* const tile = At<int32_t*>(region, 0x44);
    void** const holder = tile != At<int32_t*>(region, 0x48) ? Virtual<void**>(region, 0x2C, tile[0], tile[1]) : nullptr;
    if (!holder || !*holder)
        return " pics none";
    GameText path;
    Virtual<void>(*holder, 0x7C, static_cast<void*>(&path));
    using LoadFn = uint8_t(__cdecl*)(void*, void**, void**, void**, void**);
    using AverageFn = void*(__cdecl*)(void**);
    Fnv f;
    void* pictures[4] = {};
    f.Add(reinterpret_cast<LoadFn>(addr::SC4_LoadSavePictures)(&path, &pictures[0], &pictures[1], &pictures[2],
                                                               &pictures[3]));
    for (void* const picture : pictures)
        AddPicture(f, picture);
    void* two[2] = {};
    f.Add(reinterpret_cast<LoadFn>(addr::SC4_LoadSavePictures)(&path, &two[0], &two[1], nullptr, nullptr));
    for (void* const picture : two)
        AddPicture(f, picture);
    if (pictures[0]) {
        void* same[4] = {pictures[0], pictures[0], pictures[0], pictures[0]};
        void* const averaged = reinterpret_cast<AverageFn>(addr::SC4CityFiles_AveragePictures)(same);
        AddPicture(f, averaged);
        if (averaged)
            Virtual<uint32_t>(averaged, 0x8);
        if (pictures[1] &&
            std::memcmp(Virtual<int32_t*>(pictures[0], 0x30), Virtual<int32_t*>(pictures[1], 0x30), 16) == 0) {
            void* mixed[4] = {pictures[0], pictures[1], pictures[1], pictures[0]};
            void* const blend = reinterpret_cast<AverageFn>(addr::SC4CityFiles_AveragePictures)(mixed);
            AddPicture(f, blend);
            if (blend)
                Virtual<uint32_t>(blend, 0x8);
        }
    }
    const int count = (pictures[0] != nullptr) + (pictures[1] != nullptr) + (pictures[2] != nullptr) +
                      (pictures[3] != nullptr);
    for (void* const picture : pictures)
        if (picture)
            Virtual<uint32_t>(picture, 0x8);
    for (void* const picture : two)
        if (picture)
            Virtual<uint32_t>(picture, 0x8);
    return " pics " + std::to_string(count) + " " + std::to_string(f.h);
}

// The open city's own regional city (the application's slot 0x90): its
// checksum as the load left it, and again after its figures are taken from
// the city once more (slot 24) and after a fresh object from the class's
// factory is described too.
std::string DriveOpenCity(cISC4City* city)
{
    cISC4AppPtr app;
    void* const regional = Virtual<void*>(static_cast<cISC4App*>(app), 0x90);
    if (!regional)
        return " open none";
    Fnv before, after, fresh;
    AddCity(before, regional);
    Virtual<void>(regional, 0x60, city);
    AddCity(after, regional);
    if (void* const made = reinterpret_cast<void*(__cdecl*)()>(addr::cSC4RegionalCity_Create)()) {
        Virtual<uint32_t>(made, 0x4);
        AddCity(fresh, made);
        Virtual<uint32_t>(made, 0x8);
    }
    return " open " + std::to_string(before.h) + " " + std::to_string(after.h) + " " + std::to_string(fresh.h);
}

void Loaded(cISC4City* city)
{
    void* const region = Region();
    if (!region)
        return;
    g_token = " | rgn " + Describe(region, "at load");
    if (Env("OPENSC4_TEST_REGION_DRIVE") == "1")
        g_token += DriveOpenCity(city) + DriveSecond(region) + DriveFiles(region) + DrivePictures(region);
    if (Env("OPENSC4_TEST_REGION_CREATE") == "1")
        g_token += DriveCreate();
}

std::string g_screenToken;

// The region screen window (cSC4WinRegionScreen, window ID 0xEA659793;
// docs/region.md): the application's window object keeps it at +0xEC while
// a region is up (the city screen is there otherwise).
void* RegionScreen()
{
    void* const owner = Global<void*>(addr::SC4_MainWindowObject);
    void* const screen = owner ? At<void*>(owner, 0xEC) : nullptr;
    return screen && At<uint32_t>(screen, 0x10) == 0xEA659793 ? screen : nullptr;
}

uint32_t Bits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

// What the region screen holds once its region is up: its plain members
// (not the paint countdown at +0x1E8), which of its parts exist, every tile
// record (cells, place in the map picture, size, its pictures pixel by pixel
// after their shift, the mask's runs and centre), the scroll window's
// tuning, and what the flat terrain map answers.
uint32_t DescribeScreen(void* screen)
{
    Fnv f;
    char* const s = static_cast<char*>(screen);
    f.Bytes(s + 0x178, 0x18);
    f.Bytes(s + 0x198, 0x14);
    f.Bytes(s + 0x1EC, 8);
    f.Bytes(s + 0x1FC, 2);
    for (std::size_t offset = 0xE0; offset < 0x178; offset += 4)
        if (offset < 0x10C || offset >= 0x124)
            f.Add(At<void*>(screen, offset) != nullptr);
    char* const first = At<char*>(screen, 0x118);
    char* const last = At<char*>(screen, 0x11C);
    f.Add(static_cast<uint32_t>((last - first) / 0x80));
    for (char* record = first; record != last; record += 0x80) {
        f.Bytes(record, 0x19);
        for (const std::size_t offset : {0x1Cu, 0x24u})
            AddPicture(f, At<void*>(record, offset));
        for (const std::size_t offset : {0x20u, 0x28u, 0x2Cu, 0x30u})
            f.Add(At<void*>(record, offset) != nullptr);
        for (const std::size_t offset : {0x38u, 0x44u, 0x50u, 0x5Cu}) {
            char* const begin = At<char*>(record, offset);
            char* const end = At<char*>(record, offset + 4);
            f.Add(static_cast<uint32_t>(end - begin));
            f.Bytes(begin, end - begin);
        }
        f.Bytes(record + 0x68, 8);
    }
    if (char* const scroll = At<char*>(screen, 0x174)) {
        f.Bytes(scroll + 0xEC, 0x1C);
        f.Bytes(scroll + 0x110, 0x10);
    }
    if (void* const terrain = Global<void*>(addr::SC4_RegionTerrain)) {
        f.Bytes(static_cast<char*>(terrain) + 0xC, 0x19);
        for (const float x : {-1.0f, 0.0f, 63.999f, 64.0f, 64.001f, 1000.5f, 65536.0f})
            for (const float z : {-0.5f, 0.0f, 128.0f, 4095.9f}) {
                f.Add(Virtual<uint32_t>(terrain, 0x2C, x, z));
                f.Add(Virtual<uint32_t>(terrain, 0x64, x, z));
            }
        f.Add(Virtual<uint32_t>(terrain, 0x24, int32_t{3}, int32_t{5}));
        int32_t x = 0, z = 0;
        Virtual<void>(terrain, 0x28, uint32_t{1234}, &x, &z);
        f.Add(x);
        f.Add(z);
    }
    return f.h;
}

// OPENSC4_TEST_REGION_DRIVE=1, on the region screen before the city loads:
// the map laid out and the mode applied again; the eight scroll commands
// through the command handler; the three map modes through the control
// handler and back; the scroll window's drag at four pointer places and its
// end; a region and a city asked for by names nothing has.
uint32_t DriveScreen(void* screen)
{
    Fnv f;
    using Plain = uint32_t(__thiscall*)(void*);
    reinterpret_cast<Plain>(addr::cSC4WinRegionScreen_LayOutMap)(screen);
    f.Bytes(static_cast<char*>(screen) + 0x180, 0x10);
    reinterpret_cast<Plain>(addr::cSC4WinRegionScreen_ApplyMode)(screen);
    char* const map = At<char*>(screen, 0xE0);
    if (map) {
        f.Bytes(map + 0xF0, 8);
        f.Bytes(map + 0x110, 7);
    }
    f.Add(At<uint8_t>(screen, 0x1A0));

    if (char* const scroll = At<char*>(screen, 0x174)) {
        using CommandFn = uint32_t(__thiscall*)(void*, uint32_t, void*, void*);
        const float speed = At<float>(scroll, 0x104);
        At<float>(scroll, 0x104) = 37.5f;
        for (const uint32_t command : {0x6A935CD8u, 0x2A948275u, 0x6A935CE0u, 0x2A94826Eu, 0x6A935CDDu, 0x2A948272u,
                                       0x6A935CE2u, 0x2A94826Bu}) {
            reinterpret_cast<CommandFn>(addr::cSC4WinRegionScreen_DoCommand)(screen, command, nullptr, nullptr);
            f.Add(Bits(At<float>(scroll, 0xFC)));
            f.Add(Bits(At<float>(scroll, 0x100)));
        }
        At<float>(scroll, 0x104) = speed;
        At<float>(scroll, 0xFC) = 0.0f;
        At<float>(scroll, 0x100) = 0.0f;

        const int32_t anchor[2] = {At<int32_t>(scroll, 0xDC), At<int32_t>(scroll, 0xE0)};
        At<int32_t>(scroll, 0xDC) = 100;
        At<int32_t>(scroll, 0xE0) = 100;
        const int32_t places[4][2] = {{100, 100}, {105, 100}, {120, 110}, {160, 60}};
        for (const auto& place : places) {
            f.Add(Virtual<uint8_t>(scroll, 0x228, place[0], place[1], uint32_t{0}));
            f.Bytes(scroll + 0xF4, 8);
            f.Add(At<uint32_t>(scroll, 0xD8));
        }
        f.Add(Virtual<uint8_t>(scroll, 0x224, int32_t{160}, int32_t{60}, uint32_t{0}));
        f.Bytes(scroll + 0xF4, 8);
        f.Add(At<uint8_t>(scroll, 0x109));
        At<int32_t>(scroll, 0xDC) = anchor[0];
        At<int32_t>(scroll, 0xE0) = anchor[1];
    }

    if (map) {
        static const uint32_t kModes[3] = {0xABA290E1, 0xCBA290EC, 0xABA290F6};
        const uint32_t mode = At<uint32_t>(map, 0x118);
        for (const uint32_t id : kModes) {
            f.Add(Virtual<uint8_t>(screen, 0x23C, uint32_t{0x287259F6}, id));
            f.Add(At<uint32_t>(map, 0x118));
        }
        if (mode < 3)
            Virtual<uint8_t>(screen, 0x23C, uint32_t{0x287259F6}, kModes[mode]);
        f.Add(At<uint32_t>(map, 0x118));
    }

    GameText nowhere("OpenSC4 no such place");
    GameText out;
    f.Add(reinterpret_cast<uint8_t(__thiscall*)(void*, void*)>(addr::cSC4WinRegionScreen_GoToRegionNamed)(screen, &nowhere));
    f.Add(reinterpret_cast<uint8_t(__thiscall*)(void*, void*, uint32_t, void*, uint32_t)>(
        addr::cSC4WinRegionScreen_OpenCityByNameOrSize)(screen, &nowhere, 3, &out, 0));
    f.Bytes(out.begin, out.end - out.begin);
    return f.h;
}

// The region map window (cSC4WinRegionView, at screen +0xE0) worked with
// the screen up: where every tile's picture sits and which tile is under a
// grid of points; the map composed into a picture of our own and read pixel
// by pixel, as it is, with a tile picked, and in the two other modes; the
// view moved to three places and back, with the picture cache's state after
// each.
uint32_t DriveMap(void* screen)
{
    Fnv f;
    char* const map = At<char*>(screen, 0xE0);
    void* const factory = At<void*>(screen, 0x154);
    if (!map || !factory)
        return 0;
    char* const first = At<char*>(screen, 0x118);
    char* const last = At<char*>(screen, 0x11C);
    const auto index = [&](void* record) {
        return record ? static_cast<int32_t>((static_cast<char*>(record) - first) / 0x80) : -1;
    };
    using PlaceFn = void(__thiscall*)(void*, void*, int32_t*, int32_t*);
    using AreaFn = void(__thiscall*)(void*, void*, int32_t*);
    using AtFn = void*(__thiscall*)(void*, int32_t, int32_t);
    using PointFn = uint32_t(__thiscall*)(void*, int32_t, int32_t);
    using PlainFn = uint32_t(__thiscall*)(void*);
    using ModeFn = uint32_t(__thiscall*)(void*, uint32_t);
    using PaintFn = uint8_t(__thiscall*)(void*, void*, int32_t, int32_t);

    for (char* record = first; record != last; record += 0x80) {
        int32_t place[6] = {};
        reinterpret_cast<PlaceFn>(addr::cSC4WinRegionView_TilePlace)(map, record, &place[0], &place[1]);
        reinterpret_cast<AreaFn>(addr::cSC4WinRegionView_TileArea)(map, record, &place[2]);
        f.Add(place);
    }
    int32_t pick[2] = {-1, -1};
    for (int32_t y = 0; y < 768; y += 24)
        for (int32_t x = 0; x < 1024; x += 24) {
            void* const record = reinterpret_cast<AtFn>(addr::cSC4WinRegionView_TileAt)(map, x, y);
            f.Add(index(record));
            if (record && pick[0] < 0) {
                pick[0] = x;
                pick[1] = y;
            }
        }

    void* const grid = At<void*>(map, 0x10C);
    const auto compose = [&] {
        void* picture = nullptr;
        if (!grid || !Virtual<uint8_t>(factory, 0xC, &picture) || !picture)
            return;
        if (Virtual<uint8_t>(picture, 0xC, int32_t{512}, int32_t{384}, int32_t{9}, int32_t{0x20})) {
            f.Add(reinterpret_cast<PaintFn>(addr::SC4RegionViewGrid_Paint)(grid, picture, 0, 0));
            AddPicture(f, picture);
        }
        Virtual<uint32_t>(picture, 0x8);
    };
    const auto cache = [&] {
        if (!grid)
            return;
        f.Bytes(static_cast<char*>(grid) + 0x18, 0x28);
        for (char* cell = At<char*>(grid, 0xC); cell != At<char*>(grid, 0x10); cell += 8)
            f.Add(static_cast<uint8_t>(cell[4]));
    };

    reinterpret_cast<PlainFn>(addr::cSC4WinRegionView_Invalidate)(map);
    cache();
    compose();
    cache();
    if (pick[0] >= 0) {
        reinterpret_cast<PointFn>(addr::cSC4WinRegionView_PickAt)(map, pick[0], pick[1]);
        f.Add(index(At<void*>(map, 0xE4)));
        cache();
        compose();
    }
    const uint32_t mode = At<uint32_t>(map, 0x118);
    for (const uint32_t other : {1u, 2u, 0u}) {
        reinterpret_cast<ModeFn>(addr::cSC4WinRegionView_SetMode)(map, other);
        compose();
    }
    reinterpret_cast<ModeFn>(addr::cSC4WinRegionView_SetMode)(map, mode);
    reinterpret_cast<PlainFn>(addr::cSC4WinRegionView_Unpick)(map);
    f.Add(index(At<void*>(map, 0xE4)));

    const int32_t home[2] = {At<int32_t>(map, 0xE8), At<int32_t>(map, 0xEC)};
    const int32_t places[4][2] = {{home[0] + 40, home[1] - 30}, {home[0] - 300, home[1] + 270}, {-5000, 9000},
                                  {home[0], home[1]}};
    for (const auto& place : places) {
        reinterpret_cast<PointFn>(addr::cSC4WinRegionView_SetViewPlace)(map, place[0], place[1]);
        f.Bytes(map + 0xE8, 8);
        cache();
    }
    compose();
    return f.h;
}

// OPENSC4_TEST_REGION_SCREEN=1: unattended runs load their city from the
// start screen, so the region screen never comes up. With the switch the
// test puts it up first, as the start screen does when the player goes on
// (0x0079D381: the kept screen removed, the region screen made and started),
// and describes it; with OPENSC4_TEST_REGION_DRIVE=1 it also works it.
void AtRegion(cISC4Region*)
{
    if (Env("OPENSC4_TEST_REGION_SCREEN") != "1" || !game::IsBuild641())
        return;
    void* const owner = Global<void*>(addr::SC4_MainWindowObject);
    if (owner && !RegionScreen()) {
        reinterpret_cast<uint8_t(__thiscall*)(void*)>(addr::SC4MainWindow_DropScreen)(owner);
        reinterpret_cast<uint8_t(__thiscall*)(void*)>(addr::SC4MainWindow_ShowScreen200)(owner);
    }
    void* const screen = RegionScreen();
    if (!screen) {
        g_screenToken = " scr none";
        log::Write("autotest: region screen did not come up");
        return;
    }
    g_screenToken = " scr " + std::to_string(DescribeScreen(screen));
    if (Env("OPENSC4_TEST_REGION_DRIVE") == "1") {
        g_screenToken += " " + std::to_string(DriveScreen(screen));
        g_screenToken += " map " + std::to_string(DriveMap(screen));
        g_screenToken += " " + std::to_string(DescribeScreen(screen));
    }
    log::Write("autotest: region screen:%s", g_screenToken.c_str());
}

void Fingerprint(cISC4City*, std::string& line)
{
    line += g_token;
    line += g_screenToken;
}

const FingerprintPart kFingerprint{"region", 98, &Fingerprint};
const LoadHook kLoaded{"region", 98, &Loaded};
const RegionHook kAtRegion{"region", 98, &AtRegion};

} // namespace
} // namespace opensc4::autotest
