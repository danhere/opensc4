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

// A MySim's plain fields as its Write (0x004251A0, and the agent base's
// 0x0040E2E0) saves them, without references and strings: offsets from the cIGZSerializable part at
// cISC4MySim - 0xAC, with their sizes. Only for cSC4MySim itself.
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
    struct GameString {
        std::uintptr_t vtable;
        char* begin;
        char* end;
        char* capacity;
        uint32_t zero;
    } name{}, bio{}, path{};
    using FromTextFn = void*(__thiscall*)(GameString* text, const char* value);
    const auto fromText = reinterpret_cast<FromTextFn>(addr::SC4String_FromText);
    fromText(&name, "Test Sim");
    fromText(&bio, "");
    fromText(&path, "");
    Virtual<void>(mysims, 0x18, &name, uint32_t{1}, uint32_t{0}, &bio, uint32_t{0}, &path, uint32_t{0});
    using FreeFn = void(__cdecl*)(void* p);
    for (GameString* text : {&name, &bio, &path}) {
        text->vtable = addr::SC4String_Vtable;
        if (text->begin)
            reinterpret_cast<FreeFn>(addr::SC4_Free)(text->begin);
    }
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

const FingerprintPart kFingerprint{"mysim", 85, &Fingerprint};
const DayHook kMoveIn{"mysim.movein", 85, &MoveIn};

} // namespace
} // namespace opensc4::autotest
