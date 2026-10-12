// The MySim agent simulator's part of the autotest (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <cISC4City.h>

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

namespace opensc4::autotest {
namespace {

// The game's string (SC4String; a cIGZString), on our stack.
struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};

void FromText(GameString* text, const char* value)
{
    reinterpret_cast<void*(__thiscall*)(GameString*, const char*)>(addr::SC4String_FromText)(text, value);
}

void FreeText(GameString* text)
{
    text->vtable = addr::SC4String_Vtable;
    if (text->begin)
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(text->begin);
}

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
    void Text(const void* string) // an SC4String*, or null
    {
        if (!string) {
            Add(uint32_t{0xFFFFFFFF});
            return;
        }
        const auto* text = static_cast<const GameString*>(string);
        Bytes(text->begin, static_cast<std::size_t>(text->end - text->begin));
        Add(uint8_t{0});
    }
};

// OPENSC4_TEST_MYSIM_QUERIES=1: each day, what the MySim panel and the
// balloons ask every MySim (cISC4MySim slots; docs/mysim-agents.md), as
// one hash in the fingerprint.
uint32_t Queries(void* sim)
{
    Fnv h;
    h.Add(static_cast<uint32_t>(static_cast<char*>(Virtual<void*>(sim, 0xC)) - static_cast<char*>(sim))); // AsAdvisor
    uint32_t key[3] = {};
    Virtual<void>(sim, 0x3C, key); // GetVehicleKey
    h.Add(key);
    h.Text(Virtual<void*>(sim, 0x64)); // GetHomeName
    h.Text(Virtual<void*>(sim, 0x68)); // GetJobName
    h.Text(Virtual<void*>(sim, 0x70)); // GetJobTitle
    h.Text(At<void*>(sim, 0xBC));      // the job's city (UpdateHomeAndJob)
    h.Add(At<uint32_t>(sim, 0xC4));    // education
    h.Add(At<uint32_t>(sim, 0xC8));    // health
    for (std::size_t slot : {std::size_t{0x74}, std::size_t{0x78}}) { // GetEQAsText, GetHQAsText
        GameString text{};
        FromText(&text, "");
        h.Add(Virtual<uint8_t>(sim, slot, &text));
        h.Text(&text);
        FreeText(&text);
    }
    if (void* const job = Virtual<void*>(sim, 0x30)) { // GetJob, then GetJobTitle(job, text)
        GameString text{};
        FromText(&text, "");
        Virtual<void>(sim, 0x6C, job, &text);
        h.Text(&text);
        FreeText(&text);
    }
    h.Add(Virtual<uint32_t>(sim, 0xEC, uint32_t{0})); // GetModelButtonBitmapID
    h.Add(Virtual<uint32_t>(sim, 0xEC, uint32_t{1}));
    h.Add(Virtual<uint32_t>(sim, 0xF8)); // GetCurrentBalloonAdvice
    h.Add(Virtual<uint32_t>(sim, 0xFC)); // GetCurrentBalloonEventAdvice
    h.Add(Virtual<uint8_t>(sim, 0x100)); // CanUDriveIt
    // Not GetLocation: on the road that is the car's position, which moves
    // on frame time.
    return h.h;
}

// A MySim's plain fields as its Write (0x004251A0, and the agent base's
// 0x0040E2E0) saves them, without references and strings: offsets from the
// cIGZSerializable part at cISC4MySim - 0xAC, with their sizes. Only for
// cSC4MySim itself and the street sim.
std::string MySimState(void* sim)
{
    constexpr struct {
        uint16_t offset;
        uint16_t size;
    } kFields[] = {
        {0xB0, 1},  {0xBC, 4},  {0xC0, 4},  {0xC4, 12}, {0xDC, 16}, {0xF0, 4},  {0x100, 1}, {0x104, 2},
        {0x106, 2}, {0x11C, 2}, {0x120, 4}, {0x148, 4}, {0x14C, 1}, {0x150, 4}, {0x19C, 48}, {0x1CC, 2},
        {0x1D0, 8}, {0x1F8, 8}, {0x239, 1}, {0x8, 4},   {0x4C, 16},
        // Two stopwatches as stored (mode, start, accumulated; +0x25C is
        // saved, +0x274 paces the message handler) and +0x28C: read raw,
        // since reading them through the clock would move it.
        {0x25C, 0x34},
    };
    char* const self = static_cast<char*>(sim) - 0xAC;
    // The MySim itself, or the Microphone Tool's street sim (cSC4MyStreetSim:
    // the same record, its vtables 0x00A85D38 / 0x00A85E5C).
    const bool plain = At<uint32_t>(sim, 0) == 0x00A85080u && At<uint32_t>(self, 0) == 0x00A851A4u;
    const bool street = At<uint32_t>(sim, 0) == 0x00A85D38u && At<uint32_t>(self, 0) == 0x00A85E5Cu;
    if (!plain && !street)
        return " other";
    uint32_t h = 2166136261u;
    for (const auto& field : kFields)
        for (uint16_t k = 0; k < field.size; ++k)
            h = (h ^ static_cast<uint8_t>(self[field.offset + k])) * 16777619u;
    char text[16];
    std::snprintf(text, sizeof text, " %08X", h);
    return text;
}

// MySims (cSC4MySimAgentSimulator, the city's slot 71, build 641 offsets;
// docs/mysim.md): the six places (each MySim's slot number and whether it
// is active), the disaster key +0x20, the panel index +0x38 and +0xC.
void Fingerprint(cISC4City* city, std::string& line)
{
    char mysimText[96] = " | ms none";
    if (void* const mysims = Virtual<void*>(city, 0x11C); mysims && game::IsBuild641()) {
        void** const sims = At<void**>(mysims, 0x14);
        const uint32_t places = static_cast<uint32_t>(At<void**>(mysims, 0x18) - sims);
        uint32_t present = 0, active = 0, slots = 0;
        for (uint32_t i = 0; i < places && i < 6; ++i) {
            if (void* const sim = sims[i]) {
                ++present;
                active = active * 2 + Virtual<uint8_t>(sim, 0x50);
                slots = slots * 31 + Virtual<uint8_t>(sim, 0x28);
            }
        }
        std::snprintf(mysimText, sizeof mysimText, " | ms %u %u %X %08X %08X %d %u", places, present, active, slots,
                      At<uint32_t>(mysims, 0x20), At<int32_t>(mysims, 0x38), At<uint8_t>(mysims, 0xC));
        line += mysimText;
        // Each MySim's plain saved fields (docs/mysim-agents.md).
        for (uint32_t i = 0; i < places && i < 6; ++i)
            if (sims[i])
                line += MySimState(sims[i]);
        if (Env("OPENSC4_TEST_MYSIM_QUERIES") == "1") {
            line += " q";
            for (uint32_t i = 0; i < places && i < 6; ++i) {
                if (!sims[i])
                    continue;
                char text[12];
                std::snprintf(text, sizeof text, " %08X", Queries(sims[i]));
                line += text;
            }
        }
        return;
    }
    line += mysimText;
}

// OPENSC4_TEST_MYSIM_MOVEIN: a MySim moves in (the agent simulator's
// slot 6, MoveIn: name, male, sign 0, bio, bitmap 0, bitmap path, place
// 0), as the region view's MySim panel does, then a residence found for it,
// so the agents' code runs (docs/mysim-agents.md).
void MoveIn(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_MYSIM_MOVEIN") || !game::IsBuild641())
        return;
    void* const mysims = Virtual<void*>(day.city, 0x11C);
    if (!mysims)
        return;
    GameString name{}, bio{}, path{};
    FromText(&name, "Test Sim");
    FromText(&bio, "");
    FromText(&path, "");
    Virtual<void>(mysims, 0x18, &name, uint32_t{1}, uint32_t{0}, &bio, uint32_t{0}, &path, uint32_t{0});
    for (GameString* text : {&name, &bio, &path})
        FreeText(text);
    // MoveIn only starts the placement tool the player clicks a house
    // with; the MySim in the first place finds a residence itself (its slot
    // 5, FindNewResidence) and the tool is dropped (CancelMoveIn, slot 10).
    void* const sim = At<void**>(mysims, 0x14)[0];
    const uint8_t found = sim ? Virtual<uint8_t>(sim, 0x14) : 0;
    Virtual<void>(mysims, 0x28);
    log::Write("autotest: a MySim moved in (residence found %u, active %u, residence %p, job %p)", found,
               sim ? Virtual<uint8_t>(sim, 0x50) : 0, sim ? Virtual<void*>(sim, 0x2C) : nullptr,
               sim ? Virtual<void*>(sim, 0x30) : nullptr);
}

// OPENSC4_TEST_MYSIM_DRIVE=<day>: on that day every MySim leaves for work
// and returns home, plays its night sound, hears that its home changed and
// that a neighbour connection went, of a fire, a pipe burst, a tornado and
// disasters near it, of its home added, of its job's building replaced,
// and changes job to the same one (cISC4MySim slots 38, 39, 54, 43, 49,
// 45-48, 42, 41, 44, 7, 8; docs/mysim-agents.md). Not the balloon target
// (+0 slots 9-11): the original crashes making one unattended.
void Drive(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_MYSIM_DRIVE") || !game::IsBuild641())
        return;
    void* const mysims = Virtual<void*>(day.city, 0x11C);
    if (!mysims)
        return;
    void** const sims = At<void**>(mysims, 0x14);
    const uint32_t places = static_cast<uint32_t>(At<void**>(mysims, 0x18) - sims);
    for (uint32_t i = 0; i < places && i < 6; ++i) {
        void* const sim = sims[i];
        if (!sim)
            continue;
        Virtual<void>(sim, 0x98); // LeaveForWork
        const int32_t leaving = At<int32_t>(sim, 0x8);
        Virtual<void>(sim, 0x9C); // ReturnHome
        Virtual<void>(sim, 0xD8); // PlayNightSound
        Virtual<void>(sim, 0xAC, Virtual<void*>(sim, 0x2C)); // DoBuildingChanged(GetResidence)
        Virtual<void>(sim, 0xC4, int32_t{0}, int32_t{0}); // DoNeighborConnectionRemoved
        // News near it: a fire, a pipe burst, a tornado and two disasters
        // where it is, a building added (its home), a script command.
        float location[3] = {};
        Virtual<uint8_t>(sim, 0x38, location); // GetLocation
        Virtual<void>(sim, 0xB4, location[0], location[2]); // DoFireAdded
        Virtual<void>(sim, 0xB8, location[0] + 64.0f, location[2]); // DoPipeBurst
        Virtual<void>(sim, 0xBC, location[0], location[2] - 48.0f); // DoTornado
        const auto cellX = static_cast<int32_t>(location[0] / 16.0f);
        const auto cellZ = static_cast<int32_t>(location[2] / 16.0f);
        Virtual<void>(sim, 0xC0, 0xCA380B06u, cellX, cellZ); // DoDisaster
        Virtual<void>(sim, 0xC0, 0xC4C90997u, cellX + 3, cellZ); // DoDisaster
        void* const residence = Virtual<void*>(sim, 0x2C);
        void* const job = Virtual<void*>(sim, 0x30);
        if (residence) {
            Virtual<void>(sim, 0xA8, residence); // DoBuildingAdded
            log::Write("autotest: MySim %u residence test %u", i,
                       Virtual<uint8_t>(sim, 0x1C, residence, uint32_t{1}, uint32_t{1})); // TestForResidence
        }
        Virtual<void>(sim, 0xA4, 0x6A5E6860u); // DoScriptCommand: find a new job
        if (job) {
            Virtual<void>(sim, 0xB0, job, job); // DoBuildingRemoved, replaced by itself
            log::Write("autotest: MySim %u job change %u", i, Virtual<uint8_t>(sim, 0x20, job)); // ChangeJob
        }
        log::Write("autotest: MySim %u driven (state after leaving %d, now %d)", i, leaving, At<int32_t>(sim, 0x8));
    }
}

// OPENSC4_TEST_MYSIM_DELETE=<day>: the MySim in the first place leaves
// the game (cISC4MySim slot 9, DeleteFromGame).
void Delete(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_MYSIM_DELETE") || !game::IsBuild641())
        return;
    void* const mysims = Virtual<void*>(day.city, 0x11C);
    void* const sim = mysims ? At<void**>(mysims, 0x14)[0] : nullptr;
    if (!sim)
        return;
    log::Write("autotest: MySim deleted (%u)", Virtual<uint8_t>(sim, 0x24));
}

// OPENSC4_TEST_MYSIM_COMMANDS=<day>: on that day every MySim gets the
// script commands for richer, fired and a new generation, and the MySim
// in the first place the one for dying (cISC4MySim slot 41,
// DoScriptCommand; docs/mysim-agents.md).
void Commands(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_MYSIM_COMMANDS") || !game::IsBuild641())
        return;
    void* const mysims = Virtual<void*>(day.city, 0x11C);
    if (!mysims)
        return;
    void** const sims = At<void**>(mysims, 0x14);
    const uint32_t places = static_cast<uint32_t>(At<void**>(mysims, 0x18) - sims);
    for (uint32_t i = 0; i < places && i < 6; ++i) {
        void* const sim = sims[i];
        if (!sim)
            continue;
        Virtual<void>(sim, 0xA4, 0x6A4A5E9Eu); // richer
        Virtual<void>(sim, 0xA4, 0xAA4934B4u); // fired
        Virtual<void>(sim, 0xA4, 0x6A495F7Fu); // a new generation
        if (i == 0)
            Virtual<void>(sim, 0xA4, 0xEA4D269Cu); // died
        log::Write("autotest: MySim %u commands sent (state now %d)", i, At<int32_t>(sim, 0x8));
    }
}

// --- OPENSC4_TEST_MYSIM_TOOLS (docs/ui.md) -----------------------------------

char g_toolsToken[160];

// The current tool swept over a grid of screen points (OnMouseMove, slot
// 20), as the mouse would: what each point answers and whether a building
// or figure is under it (+0x2C of the three building tools).
void Sweep(Fnv& f, bool building, int32_t* hitX, int32_t* hitZ)
{
    *hitX = -1;
    for (int32_t z = 80; z <= 720; z += 64) {
        for (int32_t x = 80; x <= 960; x += 64) {
            char* const tool = Global<char*>(addr::SC4_DispatchTool);
            if (!tool)
                return;
            f.Add(Virtual<uint8_t>(tool, 0x50, x, z, uint32_t{0}));
            if (building) {
                const bool hit = At<void*>(tool, 0x2C) != nullptr;
                f.Add(hit);
                if (hit && *hitX < 0) {
                    *hitX = x;
                    *hitZ = z;
                }
            } else {
                f.Add(At<int32_t>(tool, 0x30));
            }
        }
    }
}

// On that run day the MySim tools are used as the player would use them,
// with no player: the list of Sims the picker shows; then for the MySim in
// the first place the home tool, the job tool and the dispatch tool, each
// made as its panel button makes it, swept over the screen, clicked on the
// first building it accepted (the home and job tools: the MySim moves or
// changes job) and ended. One fingerprint token from then on; the MySims'
// own fields are in the fingerprint already.
void Tools(const Day& day)
{
    // OPENSC4_TEST_MYSIM_TOOLS_DAYS=<n>: on that many days in a row.
    const int first = EnvInt("OPENSC4_TEST_MYSIM_TOOLS", 0);
    const int run = static_cast<int>(day.day - day.first);
    if (first <= 0 || !day.counted || run < first || run >= first + EnvInt("OPENSC4_TEST_MYSIM_TOOLS_DAYS", 1) ||
        !game::IsBuild641())
        return;
    Fnv picker;
    int records = 0;
    if (char* const memory = static_cast<char*>(reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0x2C))) {
        char* const list = reinterpret_cast<char*(__thiscall*)(void*)>(addr::SC4UI_F43B3B0)(memory);
        picker.Add(Virtual<uint8_t>(list, 0x20)); // FindSims
        char* out[3] = {nullptr, nullptr, nullptr};
        Virtual<void*>(list, 0x18, &out[0]); // GetSims
        for (char* record = out[0]; record && record != out[1]; record += 0x30, ++records) {
            picker.Text(record + 0x4);
            picker.Text(record + 0x18);
        }
        if (out[0]) {
            uint8_t tag = 0;
            reinterpret_cast<void(__cdecl*)(char*, char*, uint8_t*)>(addr::SC4UI_F43B180)(out[0], out[1], &tag);
            reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(out[0]);
        }
        uint32_t imported = 77;
        picker.Add(Virtual<uint8_t>(list, 0x1C, &imported)); // Import: The Sims is not installed
        picker.Add(imported);
        Virtual<void*>(list, 0xC, uint32_t{1}); // delete
    }

    Fnv tools;
    int clicks = 0;
    void* const mysims = Virtual<void*>(day.city, 0x11C);
    void** const sims = mysims ? At<void**>(mysims, 0x14) : nullptr;
    char* const sim = sims && At<void**>(mysims, 0x18) != sims ? static_cast<char*>(sims[0]) : nullptr;
    if (sim) {
        char* const object = sim - 0xB8;
        using MakeFn = uint8_t(__thiscall*)(void*);
        const struct {
            std::uintptr_t maker;
            void* self;
            bool building;
        } kinds[] = {
            {addr::cSC4MySim_MakeHomeTool, object, true},
            {addr::cSC4MySim_MakeJobTool, object, true},
            {addr::cSC4MySim_CreateDispatchTool, sim, false},
        };
        for (const auto& kind : kinds) {
            const uint8_t made = reinterpret_cast<MakeFn>(kind.maker)(kind.self);
            tools.Add(made);
            if (!made)
                continue;
            int32_t x = -1, z = -1;
            Sweep(tools, kind.building, &x, &z);
            char* tool = Global<char*>(addr::SC4_DispatchTool);
            if (tool && kind.building) {
                tools.Add(Virtual<uint8_t>(tool, 0x58)); // the mouse leaves
                if (x >= 0) {
                    Virtual<uint8_t>(tool, 0x50, x, z, uint32_t{0});
                    tools.Add(Virtual<uint8_t>(tool, 0x40, x, z, uint32_t{0})); // the click ends the tool
                    ++clicks;
                }
            }
            // Whatever is still the view's tool is ended as Escape ends it.
            void* const view = Global<void*>(addr::SC4_Service43DC4);
            tool = Global<char*>(addr::SC4_DispatchTool);
            if (tool && view && Virtual<void*>(view, 0x60) == tool)
                Virtual<uint8_t>(tool, 0x38, uint32_t{0x1B}, uint32_t{0});
        }
    }
    // The MySim's own panel methods: its picture, the camera sent to it,
    // the info tip over its home and its place on the map, each put away
    // again.
    if (sim) {
        void* picture = nullptr;
        const uint8_t has = Virtual<uint8_t>(sim, 0x60, &picture); // GetBitmapBuffer
        tools.Add(has);
        tools.Add(picture != nullptr);
        if (has && picture)
            Virtual<uint32_t>(picture, 0x8);
        // Find and the two info tips move or steer the camera (the place
        // shown follows a travelling MySim), and the camera travels on frame
        // time: after them the sweeps of later days and the view-dependent
        // parts of a save no longer repeat. So only with
        // OPENSC4_TEST_MYSIM_FIND=1.
        if (EnvInt("OPENSC4_TEST_MYSIM_FIND", 0)) {
            if (!(At<int32_t>(sim, 0x8) == 3 && !At<void*>(sim, 0x28) && At<void*>(sim, 0x2C)))
                Virtual<void>(sim, 0x7C); // Find
            Virtual<void>(sim, 0x80);     // ShowResidence
            Virtual<void>(sim, 0x84);     // HideResidence
            Virtual<void>(sim, 0x88);     // ShowLocation
            Virtual<void>(sim, 0x8C);     // HideLocation
        }
        tools.Add(At<int32_t>(sim, 0x8));
    }
    std::snprintf(g_toolsToken, sizeof g_toolsToken, " mstools %d %08X sim %d clicks %d %08X", records, picker.h,
                  sim ? 1 : 0, clicks, tools.h);
    log::Write("autotest: MySim tools driven:%s", g_toolsToken);
}

void ToolsFingerprint(cISC4City*, std::string& line)
{
    line += g_toolsToken;
}

// --- OPENSC4_TEST_MYSIM_DIALOG (docs/ui.md) ----------------------------------

char g_dialogToken[120];
char* g_dialog = nullptr;
Fnv g_dialogHash;
int g_dialogCount = -1;

void AddDialogState(Fnv& f, char* dialog)
{
    const uint32_t count = At<uint32_t>(dialog, 0x40);
    f.Add(count);
    if (const uint32_t* const order = At<uint32_t*>(dialog, 0x14))
        for (uint32_t i = 0; i < count && i < 4096; ++i)
            f.Add(order[i]);
    for (std::size_t at : {std::size_t{0x18}, std::size_t{0x20}, std::size_t{0x3C}, std::size_t{0x44}, std::size_t{0x48}})
        f.Add(At<uint32_t>(dialog, at));
    f.Add(At<uint8_t>(dialog, 0x1C));
    f.Text(dialog + 0x28);
}

// What stands in for the window manager's modal loop while the dialog
// runs: the player's part. The list sorted each way, rows picked, the name
// checked, and a few buttons pressed through the dialog's window procedure
// (a page, the page strip's arrows, a picture). Then it returns, as the loop
// does when the dialog is closed.
void __fastcall DialogPlayer(void*, void*, void*)
{
    char* const dialog = g_dialog;
    Fnv& f = g_dialogHash;
    g_dialogCount = At<int32_t>(dialog, 0x40);
    AddDialogState(f, dialog);
    using ByteFn = void(__thiscall*)(void*, uint32_t);
    const struct {
        std::uintptr_t sort;
        uint32_t way;
    } sorts[] = {{addr::SC4UI_F7753A0, 1}, {addr::SC4UI_F775410, 1}, {addr::SC4UI_F775410, 0},
                 {addr::SC4UI_F7754B0, 1}, {addr::SC4UI_F7754B0, 0}, {addr::SC4UI_F7753A0, 0}};
    for (const auto& sort : sorts) {
        reinterpret_cast<ByteFn>(sort.sort)(dialog, sort.way);
        AddDialogState(f, dialog);
    }
    for (const uint32_t row : {0u, 5u, 20u, 2u}) {
        if (At<uint32_t>(dialog, 0x3C) + row >= At<uint32_t>(dialog, 0x40))
            continue;
        reinterpret_cast<ByteFn>(addr::SC4UI_F7757F0)(dialog, row);
        AddDialogState(f, dialog);
        f.Add(reinterpret_cast<uint8_t(__thiscall*)(void*)>(addr::SC4UI_F774FC0)(dialog));
    }
    void* const window = Virtual<void*>(At<void*>(dialog, 0x10), 0xC);
    const uint32_t picture = Global<uint32_t>(0x00AB5208 + 4 * 3); // the fourth of the 21 picture buttons
    for (const uint32_t id : {0x12345001u, 0x12345000u, 0xCA243E04u, 0x2A243DFAu, picture, 0x12345002u}) {
        uint32_t message[4] = {3, 0x287259F6, id, 0};
        f.Add(Virtual<uint8_t>(dialog + 0x8, 0xC, window, &message[0]));
        AddDialogState(f, dialog);
    }
}

// On that run day the MySim picker dialog is run as the MySim panel runs
// it (made, Run, Close, released), with DialogPlayer in place of the
// window manager's modal loop for the time of the run.
void Dialog(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_MYSIM_DIALOG") || !game::IsBuild641())
        return;
    void* const manager = reinterpret_cast<void*(__cdecl*)()>(addr::SC4_GetWinManager)();
    char* const memory = static_cast<char*>(reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0x64));
    if (!manager || !memory)
        return;
    char* const dialog = reinterpret_cast<char*(__thiscall*)(void*, uint32_t)>(addr::SC4UI_F776560)(memory, 1);
    Virtual<uint32_t>(dialog, 0x4);
    void** const slot = &(*static_cast<void***>(manager))[0xA4 / sizeof(void*)]; // DoModalWin
    void* const loop = *slot;
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &old)) {
        Virtual<uint32_t>(dialog, 0x8);
        return;
    }
    *slot = reinterpret_cast<void*>(&DialogPlayer);
    g_dialog = dialog;
    g_dialogHash = Fnv{};
    g_dialogCount = -1;
    reinterpret_cast<void(__thiscall*)(void*)>(addr::SC4UI_F775CC0)(dialog); // Run
    *slot = loop;
    VirtualProtect(slot, sizeof *slot, old, &old);
    AddDialogState(g_dialogHash, dialog);
    reinterpret_cast<void(__thiscall*)(void*)>(addr::SC4UI_F7766A0)(dialog); // Close
    g_dialogHash.Add(At<void*>(dialog, 0x10) != nullptr);
    g_dialogHash.Add(At<uint8_t>(dialog, 0x24));
    Virtual<uint32_t>(dialog, 0x8);
    g_dialog = nullptr;
    std::snprintf(g_dialogToken, sizeof g_dialogToken, " msdlg %d %08X", g_dialogCount, g_dialogHash.h);
    log::Write("autotest: MySim dialog driven:%s", g_dialogToken);
}

void DialogFingerprint(cISC4City*, std::string& line)
{
    line += g_dialogToken;
}

// --- OPENSC4_TEST_STREET_SIM and OPENSC4_TEST_MYSIM_BALLOONS (docs/mysim-agents.md) --------
//
// These two call the game's addresses only (never our functions), so the
// same test runs with the street sim, balloon and small-function replacements
// on or off.

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

char g_streetToken[24];
char g_balloonsToken[24];
// Where the street test made its automata and the day it counts what is left
// of them (the game removes a vehicle let go of when its automata manager
// next runs): log only.
float g_streetWhere[3];
int32_t g_streetCheckDay = -1;
int g_streetBefore = -1;
uint32_t g_streetGenerator = 0; // the generator's hash at the last step

constexpr uint32_t kStreetVtable = 0x00A85ED0;
constexpr uint32_t kStreetInterfaceVtable = 0x00A85D20;
constexpr uint32_t kStreetInterfaceId = 0x0BF61276;
constexpr uint32_t kAutomatonId = 0x8B99EE7C;
constexpr uint32_t kRefusedProperty = 0x1BC42C49;
constexpr uint32_t kMicrophoneToolId = 0xCC23D564;
constexpr uint32_t kAutomatonEvent = 0x09D92FC3;
constexpr std::size_t kStreetPart = 0x2B8;
constexpr std::size_t kObjectPart = 0xB8;

// What UpdateGlobals and Follow publish (script variable names).
const char* const kStreetGlobals[] = {
    "trip_traffic_congestion", "trip_time_ratio", "trip_wait_length", "trip_pollution_air", "trip_pollution_water",
    "trip_pollution_garbage", "trip_crime", "trip_mayor_rating", "trip_eq", "trip_hq", "trip_distress1_p",
    "trip_distress2_p", "trip_distress3_p", "trip_wealth_low_p", "trip_wealth_med_p", "trip_wealth_high_p",
    "trip_landmark_nearby", "trip_landmark_name", "trip_landmark_funding_p", "trip_fire_nearby", "trip_riot_nearby",
    "trip_any_disaster_nearby", "wealth", "trip_in_transit", "trip_at_location", "trip_at_transit_switch",
    "trip_idle", "trip_approaching_location", "trip_on_bus", "trip_on_foot", "trip_on_subway", "trip_in_car",
    "trip_on_train", "trip_on_elevated_train", "trip_on_monorail", "state",
};

struct Street {
    char* object; // the cSC4MyStreetSim from its start
    char* street; // its interface of Follow, Stop and MakeTool (+0x2B8)
    char* sim;    // its cISC4MySim part (+0xB8)
};

// The advisor system's generator (the object at its +0x30; 0x18 bytes of
// plain data), which Follow draws from once for a vehicle.
void AddGenerator(Fnv& f, std::string* text)
{
    void* const system = Global<void*>(addr::SC4_Service43D34);
    void* const generator = system ? Virtual<void*>(system, 0x24) : nullptr;
    if (!generator) {
        f.Add(uint8_t{0xEE});
        return;
    }
    f.Bytes(generator, 0x18);
    if (text) {
        char part[16];
        for (std::size_t i = 0; i < 6; ++i) {
            std::snprintf(part, sizeof part, "%s%08X", i ? "," : "", At<uint32_t>(generator, i * 4));
            *text += part;
        }
    }
}

// The script variables of the street sim, read back from its script table
// (the reference at +0x164, pushed by slot 0xAC of the context at +0x1F0).
// Numbers as bit patterns, strings as text, anything else by its type.
void ReadGlobals(Fnv& f, char* object, std::string* text)
{
    void* const script = At<void*>(object, 0x1F0);
    const int32_t reference = At<int32_t>(object, 0x164);
    if (!script || reference == 0 || reference == -1) {
        f.Add(uint8_t{0xEE});
        return;
    }
    const int32_t top = Virtual<int32_t>(script, 0x18);
    Virtual<void>(script, 0xAC, reference);
    if (Virtual<int32_t>(script, 0x30, int32_t{-1}) != 5) { // not a table
        Virtual<void>(script, 0x1C, top);
        f.Add(uint8_t{0xED});
        return;
    }
    Virtual<void>(script, 0x1C, top);
    for (const char* const name : kStreetGlobals) {
        Virtual<void>(script, 0xAC, reference);
        Virtual<void>(script, 0x84, name);
        Virtual<void>(script, 0x98, int32_t{-2});
        const int32_t type = Virtual<int32_t>(script, 0x30, int32_t{-1});
        f.Add(type);
        char part[96];
        if (type == 3) {
            const double value = Virtual<double>(script, 0x5C, int32_t{-1});
            f.Add(value);
            if (text) {
                std::snprintf(part, sizeof part, " %s=%g", name, value);
                *text += part;
            }
        } else if (type == 4) {
            const char* const value = Virtual<const char*>(script, 0x60, int32_t{-1});
            if (value)
                f.Bytes(value, std::strlen(value) + 1);
            if (text) {
                std::snprintf(part, sizeof part, " %s=\"%s\"", name, value ? value : "");
                *text += part;
            }
        } else if (text) {
            std::snprintf(part, sizeof part, " %s:%d", name, type);
            *text += part;
        }
        Virtual<void>(script, 0x1C, top);
    }
}

// One step of the sequence: what a call returned (low byte), the street
// sim's state, means of travel, following byte, whether it holds an
// automaton and a marker, the generator, and the script variables once they
// were published. Appended to the phase's list of step hashes.
uint32_t StreetStep(const Street& s, uint32_t returned, bool published, std::string& steps, std::string* text)
{
    Fnv f;
    f.Add(static_cast<uint8_t>(returned));
    f.Add(At<int32_t>(s.object, 0xC0));
    f.Add(At<int32_t>(s.object, 0x2BC));
    f.Add(At<uint8_t>(s.object, 0x210));
    f.Add(static_cast<uint8_t>(At<void*>(s.object, 0xF8) != nullptr));
    f.Add(static_cast<uint8_t>(At<int32_t>(s.object, 0x150) != -1));
    // Whether the generator moved since the last step: its state at the
    // start differs from run to run (the balloons' sounds draw from it on
    // real time), so only its advance is compared.
    Fnv now;
    AddGenerator(now, nullptr);
    f.Add(static_cast<uint8_t>(now.h != g_streetGenerator));
    g_streetGenerator = now.h;
    if (published)
        ReadGlobals(f, s.object, text);
    char part[16];
    std::snprintf(part, sizeof part, " %08X", f.h);
    steps += part;
    return f.h;
}

// What the MySim panel's questions answer for the street sim (log only): the
// balloon advice and event advice now asked for, the vehicle key, the two
// model button pictures, whether it can be driven, its balloon queue sizes.
int ListSize(char* sim, std::size_t at)
{
    char* const head = At<char*>(sim, at);
    int count = 0;
    for (char* node = head ? At<char*>(head, 0) : nullptr; node && node != head && count < 1000; node = At<char*>(node, 0))
        ++count;
    return count;
}

std::string AskStreet(const Street& s)
{
    uint32_t key[3] = {};
    Virtual<void>(s.sim, 0x3C, key);
    char text[200];
    std::snprintf(text, sizeof text, "advice %X event advice %X key %X,%X,%X buttons %X,%X drive %u lists %d,%d",
                  Virtual<uint32_t>(s.sim, 0xF8), Virtual<uint32_t>(s.sim, 0xFC), key[0], key[1], key[2],
                  Virtual<uint32_t>(s.sim, 0xEC, uint32_t{0}), Virtual<uint32_t>(s.sim, 0xEC, uint32_t{1}),
                  Virtual<uint8_t>(s.sim, 0x100), ListSize(s.sim, 0x178), ListSize(s.sim, 0x174));
    return text;
}

// How many occupants stand in a box around a point (log only: moving cars
// come and go). The query is the Microphone Tool's own (the network
// service's slot 0x94).
int CountOccupants(const float* where)
{
    void* const allocator = ListNodeAllocator();
    void* const manager = Global<void*>(addr::SC4_NetworkService);
    if (!allocator || !manager)
        return -1;
    const float low[3] = {where[0] - 6.0f, where[1] - 14.0f, where[2] - 6.0f};
    const float high[3] = {where[0] + 6.0f, where[1] + 14.0f, where[2] + 6.0f};
    char* head = Virtual<char*>(allocator, 0xC, uint32_t{0xC});
    if (!head)
        return -1;
    At<char*>(head, 0) = head;
    At<char*>(head, 4) = head;
    char* list = head;
    Virtual<void>(manager, 0x94, &list, low, high, uint32_t{0}, int32_t{-1});
    int count = 0;
    for (char* node = At<char*>(list, 0); node != list && count < 100000; node = At<char*>(node, 0))
        ++count;
    Virtual<void>(manager, 0xC0, &list);
    This<void>(addr::SC4List_Destroy2, &list);
    return count;
}

// An automaton made as the dispatch's MakeVehicle (0x00435170) makes one,
// without the property that function sets and without launching it: the
// model for the status (0 walker, 1 car), its key from the dispatch's map,
// the model service's slot 0x3C at the MySim's home. The callback it was
// given is taken off again, as the dispatch does when a launch fails. One
// reference is ours (the caller's, as MakeVehicle's out parameter holds
// it); null when the game gave none.
void* MakeMover(char* dispatch, uint32_t status, const float* where, std::string& log)
{
    uint32_t model = 0;
    int32_t kind = -1;
    This<void>(addr::cSC4MySimDispatch_ModelFor, dispatch, status, &model, &kind);
    uint32_t key[3] = {0, 0, 0};
    void* node = nullptr;
    This<void>(addr::SC4Sint32Map_Find, dispatch + 0x150, &node, &kind);
    const bool found = node && node != At<void*>(dispatch, 0x150);
    if (found) {
        key[0] = At<uint32_t>(node, 0x14);
        key[1] = At<uint32_t>(node, 0x18);
        key[2] = At<uint32_t>(node, 0x1C);
    }
    char text[120];
    // The dispatch of a MySim that never moved in has no key for its kinds
    // (and no position): a vehicle made with an empty key kills the game in
    // the street sim's Follow (original too), so nothing is made then.
    if (!found) {
        std::snprintf(text, sizeof text, "model %X kind %d key none, not made", model, kind);
        log = text;
        return nullptr;
    }
    void* const service = This<void*>(addr::cSC4MySimDispatch_ModelService, dispatch, model);
    std::snprintf(text, sizeof text, "model %X kind %d key found service %s", model, kind, service ? "yes" : "no");
    log = text;
    if (!service)
        return nullptr;
    const float position[3] = {where[0], 0.0f, where[2]};
    void* const made = Virtual<void*>(service, 0x3C, model, position, uint32_t{0x214042}, key, dispatch + 8,
                                      uint32_t{0xFF}, uint32_t{0});
    if (!made)
        return nullptr;
    std::snprintf(text, sizeof text, "%s vtable %X", log.c_str(), At<uint32_t>(made, 0));
    log = text;
    Virtual<uint32_t>(made, 0x4);
    Virtual<uint32_t>(made, 0x4);
    Virtual<uint32_t>(made, 0x8);
    Virtual<void>(made, 0xA0, dispatch + 8);
    return made;
}

// The game's way of letting go of a vehicle that was never launched (the
// dispatch's failed-launch path: slot 0x34 with 0), then our reference.
void DisposeMover(void* mover)
{
    Virtual<void>(mover, 0x34, uint32_t{0});
    Virtual<uint32_t>(mover, 0x8);
}

// An automaton event as the automaton would send it: the type, the sender
// (the automaton) and the kind in data 2; handed to the street sim's message
// target (object + 0x10, slot 3).
uint8_t SendEvent(char* object, void* automaton, uint32_t kind)
{
    void* const memory = Cdecl<void*>(addr::RZ_operator_new, std::size_t{0x2C});
    void* const message = memory ? This<void*>(addr::cGZMessage_cGZMessage, memory) : nullptr;
    if (!message)
        return 0xEE;
    Virtual<uint32_t>(message, 0x4);
    Virtual<void>(message, 0x14, kAutomatonEvent);
    Virtual<void>(message, 0x30, automaton);
    Virtual<void>(message, 0x40, kind);
    const uint8_t result = Virtual<uint8_t>(object + 0x10, 0xC, message);
    Virtual<uint32_t>(message, 0x8);
    return result;
}

// Follow, hide, show and the events on one automaton, a step each. `turns`
// also lets the advisor system's turns (slots 6 and 7) run once while
// following. Ends stopped.
void DriveAutomaton(const Street& s, void* automaton, bool turns, Fnv& phase, std::string& steps, std::string* text)
{
    bool published = false;
    const auto step = [&](uint32_t returned, std::string* dump = nullptr) {
        if (At<int32_t>(s.object, 0xC0) != 0)
            published = true;
        phase.Add(StreetStep(s, returned, published, steps, dump));
    };
    step(Virtual<uint8_t>(s.street, 0xC, automaton), text); // Follow
    if (turns) {
        Virtual<void>(s.object, 0x18);                        // slot 6: Think(0)
        const uint32_t events = Virtual<uint32_t>(s.object, 0x1C); // slot 7: advice events
        step(events);
    }
    Virtual<void>(s.sim, 0x8C); // HideLocation: stops, keeps the automaton, puts the tool away
    step(0);
    Virtual<void>(s.sim, 0x88); // ShowLocation: follows the kept automaton
    step(0);
    step(SendEvent(s.object, automaton, 2)); // it arrived: stops
    for (const uint32_t kind : {3u, 5u, 4u, 7u}) {
        step(Virtual<uint8_t>(s.street, 0xC, automaton));
        step(SendEvent(s.object, automaton, kind)); // 3 and 5 stop, 4 and 7 do not
    }
    step(Virtual<uint8_t>(s.street, 0x10)); // Stop
}

// On that run day: the Microphone Tool's street sim follows a car and a
// pedestrian made by hand (not launched), is told they arrived, is driven
// through its location overrides, and gets its tool made, hovered over a
// car that is refused and one that is followed, and put away.
// OPENSC4_TEST_STREET_TURNS=1 also lets the advisor system's turns (slots 6
// and 7: Think and the advice events) run once while the car is followed;
// they queue balloon advice for the street sim, picked by a draw that differs
// from run to run (also in the original), so the MySim panel questions of
// OPENSC4_TEST_MYSIM_QUERIES differ for it afterwards.
void StreetSim(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_STREET_SIM") || !game::IsBuild641())
        return;
    Fnv all;
    void* const system = Global<void*>(addr::SC4_Service43D34);
    void* const advisor = system ? Virtual<void*>(system, 0x1C, uint32_t{0x1E}) : nullptr;
    void* iface = nullptr;
    if (!advisor || !Virtual<uint8_t>(advisor, 0x0, kStreetInterfaceId, &iface) || !iface ||
        At<uint32_t>(iface, 0) != kStreetInterfaceVtable) {
        if (iface)
            Virtual<uint32_t>(iface, 0x8);
        all.Add(uint8_t{0xEE});
        std::snprintf(g_streetToken, sizeof g_streetToken, " street %08X", all.h);
        log::Write("autotest: street: no street sim (advisor %p)", advisor);
        return;
    }
    const Street s{static_cast<char*>(iface) - kStreetPart, static_cast<char*>(iface),
                   static_cast<char*>(iface) - kStreetPart + kObjectPart};
    if (At<uint32_t>(s.object, 0) != kStreetVtable || At<void*>(s.object, 0xF8) || At<int32_t>(s.object, 0xC0)) {
        Virtual<uint32_t>(iface, 0x8);
        all.Add(uint8_t{0xED});
        std::snprintf(g_streetToken, sizeof g_streetToken, " street %08X", all.h);
        log::Write("autotest: street: street sim not idle (vtable %X, state %d)", At<uint32_t>(s.object, 0),
                   At<int32_t>(s.object, 0xC0));
        return;
    }
    std::string generator;
    {
        Fnv f;
        AddGenerator(f, &generator);
    }
    std::string globals;
    log::Write("autotest: street: script context %s, table reference %s, generator %s",
               At<void*>(s.object, 0x1F0) ? "yes" : "none", At<int32_t>(s.object, 0x164) > 0 ? "yes" : "none",
               generator.c_str());
    {
        Fnv f;
        AddGenerator(f, nullptr);
        g_streetGenerator = f.h;
    }

    // The dispatch of the first MySim, for the models and the position.
    char* dispatch = nullptr;
    void* const mysims = Virtual<void*>(day.city, 0x11C);
    if (mysims) {
        void** const sims = At<void**>(mysims, 0x14);
        const uint32_t places = static_cast<uint32_t>(At<void**>(mysims, 0x18) - sims);
        for (uint32_t i = 0; i < places && i < 6 && !dispatch; ++i)
            if (sims[i] && At<uint32_t>(sims[i], 0) == 0x00A85080u)
                dispatch = At<char*>(sims[i], 0x154);
    }
    float where[3] = {0.0f, 0.0f, 0.0f};
    if (dispatch) {
        where[0] = At<float>(dispatch, 0x88);
        where[2] = At<float>(dispatch, 0x8C);
        // After OPENSC4_TEST_MYSIM_COMMANDS (fired, a new generation) the
        // dispatch is reset: its position reads -16,-16 and a vehicle made
        // there kills the game in Follow (original too), so none is made.
        if (where[0] < 0.0f || where[2] < 0.0f) {
            log::Write("autotest: street: dispatch has no position (%g,%g), nothing made", where[0], where[2]);
            dispatch = nullptr;
            where[0] = where[2] = 0.0f;
        }
    }
    const int before = CountOccupants(where);

    const std::string asked = AskStreet(s);
    std::string log1, log2, log3;
    Fnv car, walker, tool;
    std::string carSteps, walkerSteps, toolSteps;
    void* carMover = dispatch ? MakeMover(dispatch, 1, where, log1) : nullptr;
    void* walkerMover = dispatch ? MakeMover(dispatch, 0, where, log2) : nullptr;
    const int during = CountOccupants(where);
    // The car and the walker in turn.
    const struct {
        void* mover;
        bool turns;
        Fnv* phase;
        std::string* steps;
        const char* name;
    } runs[] = {{carMover, true, &car, &carSteps, "car"}, {walkerMover, false, &walker, &walkerSteps, "walker"}};
    for (const auto& run : runs) {
        if (!run.mover) {
            run.phase->Add(uint8_t{0xEE});
            log::Write("autotest: street: %s: not made", run.name);
            continue;
        }
        void* automaton = nullptr;
        Virtual<uint8_t>(run.mover, 0x0, kAutomatonId, &automaton);
        if (!automaton) {
            run.phase->Add(uint8_t{0xEF});
            continue;
        }
        std::string text;
        DriveAutomaton(s, automaton, run.turns && EnvInt("OPENSC4_TEST_STREET_TURNS", 0) != 0, *run.phase, *run.steps,
                       &text);
        // The generator's state is logged, not hashed: its start differs from
        // run to run; the steps compare its advance.
        std::string after;
        Fnv generator;
        AddGenerator(generator, &after);
        log::Write("autotest: street: %s: hash %08X steps%s | generator %s | variables on the first follow:%s", run.name,
                   run.phase->h, run.steps->c_str(), after.c_str(), text.c_str());
        Virtual<uint32_t>(automaton, 0x8);
    }

    // The tool: made, the view's current control, hovered over a car that
    // the dispatch's property refuses and then one without it; put away by
    // HideLocation.
    void* const view = Global<void*>(addr::SC4_Service43DC4);
    int refused = -1, accepted = -1;
    if (view && dispatch) {
        std::string unused;
        void* toolCar = MakeMover(dispatch, 1, where, log3);
        void* refusedCar = MakeMover(dispatch, 1, where, unused);
        if (refusedCar) {
            void* const exemplar = Virtual<void*>(Virtual<void*>(refusedCar, 0x14), 0x18);
            Cdecl<void>(addr::SC4_SetPropertyBool, exemplar, kRefusedProperty, uint32_t{1});
        }
        void* toolCarIface = nullptr;
        void* refusedIface = nullptr;
        if (toolCar)
            Virtual<uint8_t>(toolCar, 0x0, kAutomatonId, &toolCarIface);
        if (refusedCar)
            Virtual<uint8_t>(refusedCar, 0x0, kAutomatonId, &refusedIface);
        bool published = false;
        const auto step = [&](uint32_t returned) {
            if (At<int32_t>(s.object, 0xC0) != 0)
                published = true;
            tool.Add(StreetStep(s, returned, published, toolSteps, nullptr));
        };
        const uint32_t made = Virtual<uint8_t>(s.street, 0x14); // MakeTool
        char* const current = Virtual<char*>(view, 0x60);
        const uint32_t id = current ? Virtual<uint32_t>(current, 0x14) : 0;
        tool.Add(static_cast<uint8_t>(made));
        tool.Add(static_cast<uint8_t>(id == kMicrophoneToolId));
        step(made);
        if (current && id == kMicrophoneToolId && toolCarIface && refusedIface) {
            // The tool holds a reference of its own on what it hovers over.
            This<void>(addr::SC4RefPtr_Assign, current + 0x2C, refusedIface);
            step(Virtual<uint8_t>(current, 0x40, int32_t{0}, int32_t{0}, uint32_t{0})); // OnMouseDownL: refused
            refused = At<int32_t>(s.object, 0xC0);
            This<void>(addr::SC4RefPtr_Assign, current + 0x2C, toolCarIface);
            step(Virtual<uint8_t>(current, 0x40, int32_t{0}, int32_t{0}, uint32_t{0})); // followed
            accepted = At<int32_t>(s.object, 0xC0);
            Virtual<void>(s.sim, 0x8C); // HideLocation: puts the tool away
            step(0);
        }
        void* const left = Virtual<void*>(view, 0x60);
        tool.Add(static_cast<uint8_t>(left == nullptr));
        if (left && Virtual<uint32_t>(left, 0x14) == kMicrophoneToolId)
            Virtual<void>(view, 0x68, uint32_t{0});
        Virtual<uint8_t>(s.street, 0x10); // Stop (clears the kept automaton)
        step(0);
        if (toolCarIface)
            Virtual<uint32_t>(toolCarIface, 0x8);
        if (refusedIface)
            Virtual<uint32_t>(refusedIface, 0x8);
        if (toolCar)
            DisposeMover(toolCar);
        if (refusedCar)
            DisposeMover(refusedCar);
        log::Write("autotest: street: tool: hash %08X steps%s | made %u, control id %s, refused state %d, followed "
                   "state %d, control cleared %s",
                   tool.h, toolSteps.c_str(), made, id == kMicrophoneToolId ? "ours" : "other", refused, accepted,
                   left == nullptr ? "by HideLocation" : "by the test");
    } else {
        tool.Add(uint8_t{0xEE});
    }
    if (carMover)
        DisposeMover(carMover);
    if (walkerMover)
        DisposeMover(walkerMover);
    const int after = CountOccupants(where);
    std::memcpy(g_streetWhere, where, sizeof where);
    g_streetCheckDay = day.day + 1;
    g_streetBefore = before;
    {
        Fnv f;
        std::string text;
        AddGenerator(f, &text);
        log::Write("autotest: street: models: car %s | walker %s | tool car %s", log1.c_str(), log2.c_str(),
                   log3.c_str());
        log::Write("autotest: street: questions before: %s | after: %s", asked.c_str(), AskStreet(s).c_str());
        log::Write("autotest: street: generator after %s; occupants near %.0f,%.0f before %d, with the made ones "
                   "%d, after %d; state %d, automaton %s, marker %s, control %p",
                   text.c_str(), where[0], where[2], before, during, after, At<int32_t>(s.object, 0xC0),
                   At<void*>(s.object, 0xF8) ? "kept" : "none", At<int32_t>(s.object, 0x150) != -1 ? "set" : "none",
                   view ? Virtual<void*>(view, 0x60) : nullptr);
    }
    all.Add(car.h);
    all.Add(walker.h);
    all.Add(tool.h);
    std::snprintf(g_streetToken, sizeof g_streetToken, " street %08X", all.h);
    log::Write("autotest: street: token%s (car %08X, walker %08X, tool %08X)", g_streetToken, car.h, walker.h, tool.h);
    Virtual<uint32_t>(iface, 0x8);
}

// The day after the street test: what is left of the automata it made.
void StreetFollowUp(const Day& day)
{
    if (g_streetCheckDay < 0 || !day.counted || day.day < g_streetCheckDay)
        return;
    g_streetCheckDay = -1;
    log::Write("autotest: street: occupants near %.0f,%.0f a day later %d (before the test %d)", g_streetWhere[0],
               g_streetWhere[2], CountOccupants(g_streetWhere), g_streetBefore);
}

void StreetFingerprint(cISC4City*, std::string& line)
{
    line += g_streetToken;
}

// --- Balloons and the small leftovers ---------------------------------------------------

// One balloon advice: the balloon part's pure reads (icon count and
// instance IDs, whether each icon is loaded, the two sound keys, the flag
// tests, the chance) and the message text of the advice object.
void AddAdvice(Fnv& f, char* advice, char* part)
{
    const int32_t icons = Virtual<int32_t>(part, 0x1C);
    f.Add(icons);
    for (int32_t i = 0; i < icons && i < 16; ++i) {
        f.Add(Virtual<uint32_t>(part, 0x24, static_cast<uint32_t>(i)));
        f.Add(static_cast<uint8_t>(Virtual<void*>(part, 0x20, static_cast<uint32_t>(i)) != nullptr));
    }
    f.Add(Virtual<uint32_t>(part, 0x24, uint32_t{0xFFFF})); // an index past the end
    f.Add(static_cast<uint8_t>(Virtual<void*>(part, 0x20, uint32_t{0xFFFF}) != nullptr));
    f.Add(Virtual<uint32_t>(part, 0x28, uint32_t{0}));
    f.Add(Virtual<uint32_t>(part, 0x28, uint32_t{1}));
    for (const uint32_t mask : {1u, 2u, 4u, 0xFFu})
        f.Add(Virtual<uint32_t>(part, 0x38, mask));
    f.Add(At<uint32_t>(part, 0x100 - 0xC0));
    f.Add(At<uint32_t>(advice, 0xF0));
    f.Text(advice + 0xD0);
}

// The MySim-like advisor objects with balloon advice: for each, its balloon
// advice read, the balloon object's safe calls.
void BalloonsOf(Fnv& f, char* object, char* sim, std::string& log, bool first)
{
    char* const begin = At<char*>(object, 0x84);
    char* const end = At<char*>(object, 0x88);
    int32_t advices = 0, balloonAdvices = 0;
    Fnv mine;
    for (char* item = begin; begin && item != end && advices < 64; item += 0x10, ++advices) {
        void* const advice = At<void*>(item, 0);
        void* part = nullptr;
        if (!advice || !Virtual<uint8_t>(advice, 0x0, 0x6B70E6D9u, &part) || !part)
            continue;
        ++balloonAdvices;
        AddAdvice(mine, static_cast<char*>(part) - 0xC0, static_cast<char*>(part));
        Virtual<uint32_t>(advice, 0x8);
    }
    f.Add(advices);
    f.Add(balloonAdvices);
    f.Add(mine.h);

    // The balloon object: whether it exists, its delays and window flag,
    // whether it belongs to this MySim, what it answers to the questions of
    // QueryInterface; Sync only on the log line (it follows the MySim's
    // activity and the balloon's state, which run on real time).
    Fnv balloon;
    char* const object2 = At<char*>(object, 0x238);
    char text[200] = "none";
    if (!object2) {
        balloon.Add(uint8_t{0xEE});
    } else {
        balloon.Add(At<int32_t>(object2, 0x48));
        balloon.Add(At<int32_t>(object2, 0x4C));
        balloon.Add(At<int32_t>(object2, 0x50));
        balloon.Add(At<uint8_t>(object2, 0x54));
        balloon.Add(static_cast<uint8_t>(At<void*>(object2, 0x18) == sim));
        for (const uint32_t iid : {0x22E85D8Eu, 0x452294AAu, 0x12345678u}) {
            void* out = reinterpret_cast<void*>(std::uintptr_t{1});
            const uint8_t result = Virtual<uint8_t>(object2, 0x0, iid, &out);
            balloon.Add(result);
            balloon.Add(static_cast<uint8_t>(out == nullptr ? 0 : out == object2 ? 1 : out == object2 + 4 ? 2 : 3));
            if (result && out)
                Virtual<uint32_t>(out, 0x8);
        }
        const int32_t stateBefore = At<int32_t>(object2, 0x10);
        const uint8_t synced = This<uint8_t>(addr::SC4MySimBalloon_Sync, object2);
        std::snprintf(text, sizeof text, "state %d, sync %u, state after %d, queue %s, icons %td, active %u",
                      stateBefore, synced, At<int32_t>(object2, 0x10), At<void*>(object2, 0x14) ? "has advice" : "empty",
                      (At<char*>(object2, 0x24) - At<char*>(object2, 0x20)) / 4, Virtual<uint8_t>(sim, 0x50));
    }
    f.Add(balloon.h);
    char line[400];
    std::snprintf(line, sizeof line, " [%s: advice %d (balloon %d) %08X, balloon %08X: %s]", first ? "first" : "next",
                  advices, balloonAdvices, mine.h, balloon.h, text);
    log += line;
}

// The grade texts for a value, as the game's two functions give them.
void AddGradeTexts(Fnv& f)
{
    const float values[] = {0.0f, -5.0f, 49.9f, 50.0f, 99.9f, 100.0f, 250.0f, std::numeric_limits<float>::quiet_NaN()};
    for (const std::uintptr_t function : {addr::SC4UI_FirstGradeText, addr::SC4UI_SecondGradeText}) {
        for (const float value : values) {
            GameString text{};
            FromText(&text, "");
            reinterpret_cast<void(__cdecl*)(float, void*)>(function)(value, &text);
            f.Text(&text);
            FreeText(&text);
        }
    }
}

// On that run day: every MySim-like advisor's balloon advice and balloon, the
// dispatch's small functions, the grade texts, and a balloon advice made
// and let go.
void MySimBalloons(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_MYSIM_BALLOONS") || !game::IsBuild641())
        return;
    Fnv all;
    std::string log;
    int objects = 0;
    char* firstDispatch = nullptr;
    void* const mysims = Virtual<void*>(day.city, 0x11C);
    char* streetSim = nullptr;
    if (void* const system = Global<void*>(addr::SC4_Service43D34)) {
        void* iface = nullptr;
        void* const advisor = Virtual<void*>(system, 0x1C, uint32_t{0x1E});
        if (advisor && Virtual<uint8_t>(advisor, 0x0, kStreetInterfaceId, &iface) && iface) {
            if (At<uint32_t>(iface, 0) == kStreetInterfaceVtable)
                streetSim = static_cast<char*>(iface) - kStreetPart + kObjectPart;
            Virtual<uint32_t>(iface, 0x8);
        }
    }
    bool streetSeen = false;
    if (mysims) {
        void** const sims = At<void**>(mysims, 0x14);
        const uint32_t places = static_cast<uint32_t>(At<void**>(mysims, 0x18) - sims);
        for (uint32_t i = 0; i < places && i < 6; ++i) {
            char* const sim = static_cast<char*>(sims[i]);
            if (!sim) {
                all.Add(uint8_t{0xEE});
                continue;
            }
            const uint32_t vtable = At<uint32_t>(sim, 0);
            if (vtable != 0x00A85080u && vtable != 0x00A85D38u) {
                all.Add(uint8_t{0xED});
                continue;
            }
            streetSeen = streetSeen || sim == streetSim;
            if (!firstDispatch && vtable == 0x00A85080u)
                firstDispatch = At<char*>(sim, 0x154);
            BalloonsOf(all, sim - kObjectPart, sim, log, objects == 0);
            ++objects;
        }
    }
    if (streetSim && !streetSeen) {
        BalloonsOf(all, streetSim - kObjectPart, streetSim, log, false);
        ++objects;
    }
    all.Add(objects);

    // The dispatch's small functions: the place getters (as offsets), the
    // flag test for some masks.
    Fnv small;
    Fnv getters, flags, grades;
    char flagText[160] = "";
    if (firstDispatch) {
        for (const std::uintptr_t getter : {addr::cSC4MySimDispatch_GetTargetPlace, std::uintptr_t{0x004312A0},
                                            std::uintptr_t{0x004312B0}})
            getters.Add(static_cast<uint32_t>(This<char*>(getter, firstDispatch) - firstDispatch));
        int used = std::snprintf(flagText, sizeof flagText, "flags %X:", At<uint32_t>(firstDispatch, 0x48));
        for (const uint32_t mask : {1u, 2u, 4u, 8u, 0x80u, 0x100u, 0xFFFFu, 0x80000000u}) {
            const uint8_t has = static_cast<uint8_t>(This<uint32_t>(addr::cSC4MySimDispatch_HasFlag, firstDispatch, mask));
            flags.Add(has);
            used += std::snprintf(flagText + used, sizeof flagText - static_cast<std::size_t>(used), " %X=%u", mask, has);
        }
    } else {
        getters.Add(uint8_t{0xEE});
    }
    AddGradeTexts(grades);
    small.Add(getters.h);
    small.Add(grades.h);
    all.Add(small.h);

    // A balloon advice made by its class factory (0x0042FB10) and let go by
    // its deleting destructor (the counted part at +4, slot 3): its plain
    // fields and what its part answers.
    Fnv fresh;
    if (char* const part = Cdecl<char*>(addr::cSC4MySimBalloonAdvice_Create)) {
        char* const advice = part - 0xC0;
        fresh.Add(Virtual<int32_t>(part, 0x1C));
        fresh.Add(At<uint32_t>(advice, 0xF0));
        fresh.Add(At<uint32_t>(advice, 0xF4));
        fresh.Add(At<uint32_t>(advice, 0xF8));
        fresh.Add(At<uint32_t>(advice, 0xFC));
        fresh.Add(At<uint32_t>(advice, 0x100));
        fresh.Add(Virtual<uint32_t>(part, 0x28, uint32_t{0}));
        fresh.Add(Virtual<uint32_t>(part, 0x28, uint32_t{1}));
        for (const uint32_t mask : {1u, 2u, 4u, 0xFFu})
            fresh.Add(Virtual<uint32_t>(part, 0x38, mask));
        fresh.Add(static_cast<uint8_t>(Virtual<void*>(part, 0x20, uint32_t{0}) != nullptr));
        fresh.Text(advice + 0xD0);
        void* queried = nullptr;
        fresh.Add(Virtual<uint8_t>(advice, 0x0, 0x2B722C71u, &queried));
        fresh.Add(static_cast<uint8_t>(queried == advice));
        if (queried)
            Virtual<uint32_t>(queried, 0x8);
        void* other = nullptr;
        fresh.Add(Virtual<uint8_t>(advice, 0x0, 0x6B70E6D9u, &other));
        fresh.Add(static_cast<uint8_t>(other == part));
        if (other)
            Virtual<uint32_t>(other, 0x8);
        Virtual<void*>(advice + 4, 0xC, uint32_t{1});
    } else {
        fresh.Add(uint8_t{0xEE});
    }
    all.Add(fresh.h);

    std::snprintf(g_balloonsToken, sizeof g_balloonsToken, " balloons %08X", all.h);
    log::Write("autotest: balloons: objects %d,%s", objects, log.c_str());
    log::Write("autotest: balloons: small functions %08X (place getters %08X, grade texts %08X; flag tests %08X, "
               "not in the hash: %s), a fresh advice %08X, token%s",
               small.h, getters.h, grades.h, flags.h, flagText, fresh.h, g_balloonsToken);
}

void BalloonsFingerprint(cISC4City*, std::string& line)
{
    line += g_balloonsToken;
}

const FingerprintPart kStreetFingerprint{"mysim.street", 6, &StreetFingerprint};
const DayHook kStreetSim{"mysim.street", 88, &StreetSim};
const DayHook kStreetFollowUp{"mysim.street.followup", 90, &StreetFollowUp};
const FingerprintPart kBalloonsFingerprint{"mysim.balloons", 7, &BalloonsFingerprint};
const DayHook kBalloons{"mysim.balloons", 89, &MySimBalloons};

const FingerprintPart kDialogFingerprint{"mysim.dialog", 87, &DialogFingerprint};
const DayHook kDialog{"mysim.dialog", 87, &Dialog};

const FingerprintPart kToolsFingerprint{"mysim.tools", 86, &ToolsFingerprint};
const DayHook kTools{"mysim.tools", 86, &Tools};
const FingerprintPart kFingerprint{"mysim", 85, &Fingerprint};
const DayHook kDelete{"mysim.delete", 85, &Delete};
const DayHook kDrive{"mysim.drive", 85, &Drive};
const DayHook kMoveIn{"mysim.movein", 85, &MoveIn};
const DayHook kCommands{"mysim.commands", 85, &Commands};

} // namespace
} // namespace opensc4::autotest
