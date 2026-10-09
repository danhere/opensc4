// The MySim agent simulator's part of the autotest (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <cISC4City.h>

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
// 45-48, 42, 41, 44, 7, 8; docs/mysim-agents.md).
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

const FingerprintPart kFingerprint{"mysim", 85, &Fingerprint};
const DayHook kDelete{"mysim.delete", 85, &Delete};
const DayHook kDrive{"mysim.drive", 85, &Drive};
const DayHook kMoveIn{"mysim.movein", 85, &MoveIn};

} // namespace
} // namespace opensc4::autotest
