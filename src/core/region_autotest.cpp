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

void Loaded(cISC4City*)
{
    void* const region = Region();
    if (!region)
        return;
    g_token = " | rgn " + Describe(region, "at load");
    if (Env("OPENSC4_TEST_REGION_DRIVE") == "1")
        g_token += DriveSecond(region);
    if (Env("OPENSC4_TEST_REGION_CREATE") == "1")
        g_token += DriveCreate();
}

void Fingerprint(cISC4City*, std::string& line)
{
    line += g_token;
}

const FingerprintPart kFingerprint{"region", 98, &Fingerprint};
const LoadHook kLoaded{"region", 98, &Loaded};

} // namespace
} // namespace opensc4::autotest
