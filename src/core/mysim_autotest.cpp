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
// cSC4MySim itself.
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
    if (At<uint32_t>(sim, 0) != 0x00A85080u || At<uint32_t>(self, 0) != 0x00A851A4u)
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
        // Find moves the camera, which travels on frame time: after it the
        // sweeps of later days, and anything that depends on the view, no
        // longer repeat. So only with OPENSC4_TEST_MYSIM_FIND=1.
        if (EnvInt("OPENSC4_TEST_MYSIM_FIND", 0) &&
            !(At<int32_t>(sim, 0x8) == 3 && !At<void*>(sim, 0x28) && At<void*>(sim, 0x2C)))
            Virtual<void>(sim, 0x7C);
        Virtual<void>(sim, 0x80);     // ShowResidence
        Virtual<void>(sim, 0x84);     // HideResidence
        Virtual<void>(sim, 0x88);     // ShowLocation
        Virtual<void>(sim, 0x8C);     // HideLocation
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
