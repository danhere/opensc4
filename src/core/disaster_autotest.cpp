// The disaster layer's part of the autotest (src/core/autotest_hooks.h;
// docs/disasters.md): a fingerprint of the layer and its managers, and a
// switch that starts a disaster. Build 641 layout; fields only.
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <cISC4City.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace opensc4::autotest {
namespace {

// The layer object from the city's interface pointer (the interface is at +8).
char* Layer(cISC4City* city)
{
    void* const layer = city->GetDisasterLayer();
    return layer && game::IsBuild641() ? static_cast<char*>(layer) - 0x8 : nullptr;
}

struct Fnv {
    uint32_t h = 2166136261u;
    template <typename T>
    void Add(const T& value)
    {
        for (std::size_t i = 0; i < sizeof value; ++i)
            h = (h ^ reinterpret_cast<const uint8_t*>(&value)[i]) * 16777619u;
    }
};

char* NextNode(char* node)
{
    if (char* right = At<char*>(node, 0xC)) {
        while (At<char*>(right, 0x8))
            right = At<char*>(right, 0x8);
        return right;
    }
    char* parent = At<char*>(node, 0x4);
    while (node == At<char*>(parent, 0xC)) {
        node = parent;
        parent = At<char*>(parent, 0x4);
    }
    return At<char*>(node, 0xC) == parent ? node : parent;
}

// The layer: disabled count, ongoing count, the cost's building count and
// total, the registered locations (cells) and, per manager in type order,
// its instance count.
void Fingerprint(cISC4City* city, std::string& line)
{
    char* const layer = Layer(city);
    if (!layer)
        return;
    Fnv locations;
    char* const first = At<char*>(layer, 0xCC);
    char* const last = At<char*>(layer, 0xD0);
    for (char* at = first; at != last; at += 0x14) {
        locations.Add(At<int32_t>(at, 0x4));
        locations.Add(At<int32_t>(at, 0x8));
        locations.Add(At<int32_t>(at, 0xC));
    }
    char text[200];
    int used = std::snprintf(text, sizeof text, " | dis %d %u cost %u %lld loc %d %08X mgr", At<int32_t>(layer, 0x40),
                             At<uint32_t>(layer, 0x48), At<uint32_t>(layer, 0x50), At<long long>(layer, 0x58),
                             static_cast<int>((last - first) / 0x14), locations.h);
    char* const header = At<char*>(layer, 0xB8);
    for (char* node = At<char*>(header, 0x8); node != header; node = NextNode(node)) {
        char* const manager = At<char*>(node, 0x14) - 0x8;
        char* const list = At<char*>(manager, 0x14);
        int count = 0;
        for (char* item = At<char*>(list, 0x0); item != list; item = At<char*>(item, 0x0))
            ++count;
        if (used < static_cast<int>(sizeof text))
            used += std::snprintf(text + used, sizeof text - used, " %d", count);
    }
    line += text;
    // The riots' rioters: how many, their times and levels added up.
    for (char* node = At<char*>(header, 0x8); node != header; node = NextNode(node)) {
        if (At<uint32_t>(node, 0x10) != 0xCA380B06)
            continue;
        char* const list = At<char*>(At<char*>(node, 0x14) - 0x8, 0x14);
        int rioters = 0;
        long long time = 0;
        int levels = 0;
        for (char* item = At<char*>(list, 0x0); item != list; item = At<char*>(item, 0x0)) {
            char* const riot = At<char*>(item, 0x8) - 0x8;
            char* const members = At<char*>(riot, 0x1C);
            for (char* member = At<char*>(members, 0x0); member != members; member = At<char*>(member, 0x0)) {
                ++rioters;
                time += At<int32_t>(member, 0xC);
                levels += At<int32_t>(member, 0x10);
            }
        }
        char riots[64];
        std::snprintf(riots, sizeof riots, " riot %d %lld %d", rioters, time, levels);
        line += riots;
    }
    // The fires (one instance, its interface at +0xC): how many, their heats
    // and stages added up, the burned queue and what was destroyed.
    for (char* node = At<char*>(header, 0x8); node != header; node = NextNode(node)) {
        if (At<uint32_t>(node, 0x10) != 0x621CDA33)
            continue;
        char* const list = At<char*>(At<char*>(node, 0x14) - 0x8, 0x14);
        for (char* item = At<char*>(list, 0x0); item != list; item = At<char*>(item, 0x0)) {
            char* const instance = At<char*>(item, 0x8) - 0xC;
            int fires = 0;
            long long heat = 0;
            int stages = 0;
            char* const burning = At<char*>(instance, 0xE0);
            for (char* fire = At<char*>(burning, 0x0); fire != burning; fire = At<char*>(fire, 0x0)) {
                void* const occupant = At<void*>(fire, 0x8);
                ++fires;
                heat += Virtual<int32_t>(occupant, 0x24);
                stages += Virtual<int32_t>(occupant, 0x18);
            }
            int queued = 0;
            char* const burned = At<char*>(instance, 0xEC);
            for (char* entry = At<char*>(burned, 0x0); entry != burned; entry = At<char*>(entry, 0x0))
                ++queued;
            // The instance's random generator (+0x18: the integer state,
            // the uniform state, a flag): every draw the fires made shows
            // in it.
            Fnv draws;
            for (std::size_t at = 0x18; at < 0x24; at += 4)
                draws.Add(At<uint32_t>(instance, at));
            char fireText[112];
            std::snprintf(fireText, sizeof fireText, " fires %d %lld %d q %d gone %u groups %u rng %08X", fires, heat, stages,
                          queued, At<uint32_t>(instance, 0xE4), At<uint32_t>(instance, 0xF4), draws.h);
            line += fireText;
        }
    }
    // The earthquakes: phase, the fault (direction, control points) and how
    // far along it is.
    for (char* node = At<char*>(header, 0x8); node != header; node = NextNode(node)) {
        if (At<uint32_t>(node, 0x10) != 0x42963812)
            continue;
        char* const list = At<char*>(At<char*>(node, 0x14) - 0x8, 0x14);
        for (char* item = At<char*>(list, 0x0); item != list; item = At<char*>(item, 0x0)) {
            char* const quake = At<char*>(item, 0x8) - 0x8;
            Fnv fault;
            for (std::size_t at = 0x24; at < 0x38; at += 4)
                fault.Add(At<uint32_t>(quake, at));
            fault.Add(At<uint32_t>(quake, 0x40));
            fault.Add(At<uint32_t>(quake, 0x44));
            for (std::size_t vector : {std::size_t{0x48}, std::size_t{0x54}}) {
                for (char* at = At<char*>(quake, vector); at != At<char*>(quake, vector + 4); at += 4)
                    fault.Add(At<uint32_t>(at, 0x0));
            }
            Fnv progress;
            progress.Add(At<uint16_t>(quake, 0x60));
            for (std::size_t at : {std::size_t{0x64}, std::size_t{0x68}, std::size_t{0x8C}, std::size_t{0x90},
                                   std::size_t{0x94}, std::size_t{0x98}})
                progress.Add(At<uint32_t>(quake, at));
            const auto news = static_cast<int>((At<char*>(quake, 0x118) - At<char*>(quake, 0x114)) / 4);
            char quakes[80];
            std::snprintf(quakes, sizeof quakes, " quake %d %08X %08X %d", At<int32_t>(quake, 0x14), fault.h, progress.h,
                          news);
            line += quakes;
        }
    }
}

// OPENSC4_TEST_PIPE_FUSE=<ms>: after a pipe burst starts, the fuse range
// (0x00B0A1D8) and the fuses of the bursts there are set to this, so
// pipes explode and the bursts spread within a short run.
void ShortenPipeFuses(char* manager)
{
    const int fuse = EnvInt("OPENSC4_TEST_PIPE_FUSE", 0);
    if (fuse <= 0 || EnvInt("OPENSC4_TEST_DISASTER_TYPE", 1) != 3)
        return;
    int32_t* const range = reinterpret_cast<int32_t*>(addr::cSC4DisasterInstancePipeBurst_FuseRange);
    range[0] = fuse;
    range[1] = fuse;
    char* const instances = At<char*>(manager - 0x8, 0x14);
    for (char* item = At<char*>(instances, 0x0); item != instances; item = At<char*>(item, 0x0)) {
        char* const instance = At<char*>(item, 0x8) - 0x8;
        char* const bursts = At<char*>(instance, 0x28);
        for (char* burst = At<char*>(bursts, 0x0); burst != bursts; burst = At<char*>(burst, 0x0))
            At<uint32_t>(burst, 0x8 + 0x18) = static_cast<uint32_t>(fuse);
    }
}

// OPENSC4_TEST_DISASTER=<day> with OPENSC4_TEST_DISASTER_TYPE=<n>: on that
// run day a disaster of the n-th type (0 volcano, 1 fire, 2 earthquake,
// 3 pipe burst, 4 riot, 5 effect, 6 toxic) is started through its manager
// (slot 6, by cell), at the first cell that takes it, walking out from the
// city's centre in steps of 4 cells. The camera stays where it is unless
// OPENSC4_TEST_DISASTER_CAMERA=1.
void Drive(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_DISASTER") || !game::IsBuild641())
        return;
    static const uint32_t kTypes[] = {0xC4C90997, 0x621CDA33, 0x42963812, 0x4A1332E8,
                                      0xCA380B06, 0x6A7B4397, 0xEBFB0404};
    const int which = EnvInt("OPENSC4_TEST_DISASTER_TYPE", 1);
    void* const layer = day.city->GetDisasterLayer();
    if (!layer || which < 0 || which > 6)
        return;
    void* const manager = Virtual<void*>(layer, 0x1C, kTypes[which]);
    if (!manager) {
        log::Write("autotest: no disaster manager for type %d", which);
        return;
    }
    // The camera jumping to the disaster makes view-dependent state (the
    // traffic record's densities) vary run to run with the original, so
    // the option for it (0x00B43CD8 + 0xF08) is turned off unless
    // OPENSC4_TEST_DISASTER_CAMERA=1.
    if (!EnvInt("OPENSC4_TEST_DISASTER_CAMERA", 0)) {
        if (char* const options = Global<char*>(addr::SC4_Options43CD8))
            At<uint8_t>(options, 0xF08) = 0;
    }
    char* const self = Layer(day.city);
    if (which == 5) {
        // The effect disaster has no start by cell: as its tool does, an
        // effect (OPENSC4_TEST_DISASTER_EFFECT, default "QuakeStart") is
        // made and given to the manager's effect part at the city's
        // centre; the part's SetLocation then moves it 8 cells.
        void* extra = nullptr;
        void* effect = nullptr;
        const char* name = std::getenv("OPENSC4_TEST_DISASTER_EFFECT");
        if (!name || !*name)
            name = "QuakeStart";
        void* const effects = Virtual<void*>(Global<void*>(addr::SC4_City), 0x154);
        if (!Virtual<uint8_t>(manager, 0x0, uint32_t{0xCA7B3F3C}, &extra) || !extra || !effects ||
            !Virtual<uint8_t>(effects, 0x1C, name, &effect) || !effect) {
            log::Write("autotest: effect disaster not started (part %p, effect %p)", extra, effect);
            return;
        }
        const float middle = static_cast<float>(day.city->CellCountX()) * 8.0f;
        float position[3] = {middle, 0.0f, middle};
        Virtual<void*>(extra, 0xC, effect, position, kTypes[5]);
        position[0] += 128.0f;
        Virtual<void>(extra, 0x10, position);
        Virtual<uint32_t>(effect, 0x8);
        Virtual<uint32_t>(extra, 0x8);
        log::Write("autotest: effect disaster started with '%s' (ongoing %u, locations %d)", name, At<uint32_t>(self, 0x48),
                   static_cast<int>((At<char*>(self, 0xD0) - At<char*>(self, 0xCC)) / 0x14));
        return;
    }
    // The managers' return values differ; the layer's counts show a start.
    const uint32_t ongoing = At<uint32_t>(self, 0x48);
    char* const end = At<char*>(self, 0xD0);
    const auto size = static_cast<int32_t>(day.city->CellCountX());
    for (int32_t ring = 0; ring < size / 2; ring += 4) {
        for (int32_t dz = -ring; dz <= ring; dz += 4) {
            for (int32_t dx = -ring; dx <= ring; dx += 4) {
                if (dx != -ring && dx != ring && dz != -ring && dz != ring)
                    continue;
                const int32_t x = size / 2 + dx;
                const int32_t z = size / 2 + dz;
                Virtual<void*>(manager, 0x18, x, z);
                if (At<uint32_t>(self, 0x48) != ongoing || At<char*>(self, 0xD0) != end) {
                    log::Write("autotest: disaster type %d started at cell %d,%d", which, x, z);
                    ShortenPipeFuses(static_cast<char*>(manager));
                    return;
                }
            }
        }
    }
    // No cell on the walk took it: the manager's own choice (slot 7).
    Virtual<void*>(manager, 0x1C);
    if (At<uint32_t>(self, 0x48) != ongoing || At<char*>(self, 0xD0) != end) {
        log::Write("autotest: disaster type %d started at the manager's choice", which);
        ShortenPipeFuses(static_cast<char*>(manager));
        return;
    }
    log::Write("autotest: disaster type %d started nowhere", which);
}

// OPENSC4_TEST_DISASTER_END=<day>: on that run day every instance of the
// chosen type is shut down (its interface's slot 3, as the manager's
// Shutdown does), so the end of a disaster runs inside a short run.
void End(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_DISASTER_END") || !game::IsBuild641())
        return;
    static const uint32_t kTypes[] = {0xC4C90997, 0x621CDA33, 0x42963812, 0x4A1332E8,
                                      0xCA380B06, 0x6A7B4397, 0xEBFB0404};
    const int which = EnvInt("OPENSC4_TEST_DISASTER_TYPE", 1);
    void* const layer = day.city->GetDisasterLayer();
    if (!layer || which < 0 || which > 6)
        return;
    char* const manager = Virtual<char*>(layer, 0x1C, kTypes[which]);
    if (!manager)
        return;
    void* instances[64];
    int count = 0;
    char* const list = At<char*>(manager - 0x8, 0x14);
    for (char* item = At<char*>(list, 0x0); item != list && count < 64; item = At<char*>(item, 0x0))
        instances[count++] = At<void*>(item, 0x8);
    for (int i = 0; i < count; ++i)
        Virtual<uint8_t>(instances[i], 0xC);
    log::Write("autotest: %d disaster instance(s) of type %d shut down", count, which);
}

// OPENSC4_TEST_RIOT_PUSH=<n>: every run day each rioter's time goes up by
// n and each riot's tick (0x0057DFE0) is run with its stopwatch gate
// opened: the riot's own tick hardly ever comes under the harness's clock,
// and without it no rioter changes level, spawns another or leaves.
void PushRiots(const Day& day)
{
    const int push = EnvInt("OPENSC4_TEST_RIOT_PUSH", 0);
    if (push <= 0 || !day.counted || day.day <= day.first || !game::IsBuild641())
        return;
    void* const layer = day.city->GetDisasterLayer();
    char* const manager = layer ? Virtual<char*>(layer, 0x1C, uint32_t{0xCA380B06}) : nullptr;
    if (!manager)
        return;
    char* riots[16];
    int count = 0;
    char* const list = At<char*>(manager - 0x8, 0x14);
    for (char* item = At<char*>(list, 0x0); item != list && count < 16; item = At<char*>(item, 0x0))
        riots[count++] = At<char*>(item, 0x8) - 0x8;
    for (int i = 0; i < count; ++i) {
        char* const members = At<char*>(riots[i], 0x1C);
        for (char* member = At<char*>(members, 0x0); member != members; member = At<char*>(member, 0x0))
            At<int32_t>(member, 0xC) += push;
        At<uint32_t>(riots[i], 0x48) = 0;
        reinterpret_cast<void(__thiscall*)(void*)>(addr::cSC4DisasterInstanceRiot_Tick)(riots[i]);
    }
}

// OPENSC4_TEST_QUAKE_PUSH=<n>: the earthquake's tick comes on frame time,
// so runs differ in how far the fault got. With this set, each quake is
// taken off the tick service and its tick (0x00576B10) is run once every
// run day with n as the time passed.
void PushQuakes(const Day& day)
{
    const int push = EnvInt("OPENSC4_TEST_QUAKE_PUSH", 0);
    if (push <= 0 || !day.counted || !game::IsBuild641())
        return;
    void* const layer = day.city->GetDisasterLayer();
    char* const manager = layer ? Virtual<char*>(layer, 0x1C, uint32_t{0x42963812}) : nullptr;
    if (!manager)
        return;
    char* quakes[4];
    int count = 0;
    char* const list = At<char*>(manager - 0x8, 0x14);
    for (char* item = At<char*>(list, 0x0); item != list && count < 4; item = At<char*>(item, 0x0))
        quakes[count++] = At<char*>(item, 0x8) - 0x8;
    void* const ticks = reinterpret_cast<void*(__cdecl*)()>(addr::SC4_GetService86AD10EC)();
    for (int i = 0; i < count; ++i) {
        if (ticks)
            Virtual<void>(ticks, 0x18, quakes[i] + 0xC, int32_t{0xC0});
        if (day.day > day.first)
            reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(addr::cSC4DisasterInstanceEarthquake_Tick)(
                quakes[i], static_cast<uint32_t>(push));
    }
}

// OPENSC4_TEST_FIRE_CALLS=<day>: on that run day the fire manager's fire
// part (0x49B43F57) is called as the game's tools and other simulators
// call it: an area burned around the city's centre (inner radius 1, outer
// 4), the first three fires doused by 3 at their cells, the cell 6 east
// and south of the centre burned down, a forced start 6 west and north,
// and the owner and count queries.
void FireCalls(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_FIRE_CALLS") || !game::IsBuild641())
        return;
    void* const layer = day.city->GetDisasterLayer();
    char* const manager = layer ? Virtual<char*>(layer, 0x1C, uint32_t{0x621CDA33}) : nullptr;
    void* extra = nullptr;
    if (!manager || !Virtual<uint8_t>(manager, 0x0, uint32_t{0x49B43F57}, &extra) || !extra)
        return;
    const auto middle = static_cast<int32_t>(day.city->CellCountX()) / 2;
    const uint8_t area = Virtual<uint8_t>(extra, 0x20, middle, middle, uint32_t{1}, uint32_t{4});
    int doused = 0;
    char* const list = At<char*>(manager - 0x8, 0x14);
    if (At<char*>(list, 0x0) != list) {
        char* const instance = At<char*>(At<char*>(list, 0x0), 0x8) - 0xC;
        int32_t cells[3][4];
        char* const burning = At<char*>(instance, 0xE0);
        for (char* fire = At<char*>(burning, 0x0); fire != burning && doused < 3; fire = At<char*>(fire, 0x0))
            Virtual<void>(At<char*>(fire, 0x8) + 0x4, 0x2C, cells[doused++]);
        for (int i = 0; i < doused; ++i)
            Virtual<uint8_t>(extra, 0x18, cells[i][0], cells[i][1], uint32_t{3});
    }
    const uint8_t down = Virtual<uint8_t>(extra, 0x1C, middle + 6, middle + 6);
    // A forced start (no chance, no fire cover check), the owner value, and
    // the instance's counts.
    const uint8_t forced = Virtual<uint8_t>(extra, 0x14, middle - 6, middle - 6, uint32_t{0}, uint32_t{1}, uint32_t{0});
    const uint32_t owner = Virtual<uint32_t>(extra, 0x24, uint32_t{5});
    const uint32_t ownerNow = Virtual<uint32_t>(extra, 0x28);
    int32_t fires = -1;
    int32_t inCity = -1;
    if (void* const part = Virtual<void*>(extra, 0x2C)) {
        const int32_t all[4] = {0, 0, middle * 2 - 1, middle * 2 - 1};
        fires = Virtual<int32_t>(part, 0x40);
        inCity = Virtual<int32_t>(part, 0x3C, all);
        Virtual<uint32_t>(part, 0x8);
    }
    Virtual<uint32_t>(extra, 0x8);
    log::Write("autotest: fire calls: area %d, doused %d, burned down %d, forced %d, owner %u %u, fires %d (%d in the city)",
               area, doused, down, forced, owner, ownerNow, fires, inCity);
}

// OPENSC4_TEST_FIRE_PUSH=<n>: the fires step once a second of the
// instance's stopwatch, on a tick that comes on frame time; under the
// harness's clock that is about one step a week. With this set, every run
// day the fire instance's update (fire interface slot 4) is run n times
// with its next-step time cleared, so each call steps every fire.
void PushFires(const Day& day)
{
    const int push = EnvInt("OPENSC4_TEST_FIRE_PUSH", 0);
    if (push <= 0 || !day.counted || day.day <= day.first || !game::IsBuild641())
        return;
    void* const layer = day.city->GetDisasterLayer();
    char* const manager = layer ? Virtual<char*>(layer, 0x1C, uint32_t{0x621CDA33}) : nullptr;
    if (!manager)
        return;
    char* const list = At<char*>(manager - 0x8, 0x14);
    if (At<char*>(list, 0x0) == list)
        return;
    char* const instance = At<char*>(At<char*>(list, 0x0), 0x8) - 0xC;
    Virtual<uint32_t>(instance, 0x4);
    for (int i = 0; i < push; ++i) {
        At<uint32_t>(instance, 0xC8) = 0;
        Virtual<void>(instance + 0x8, 0x10);
    }
    Virtual<uint32_t>(instance, 0x8);
}

// OPENSC4_TEST_TUTORIAL_OFF=1: the test cities are tutorial cities, and
// while a tutorial is on a fire does not burn its occupant down or spread
// from its last stage (the fire's step asks the tutorial system's
// IsTutorial, slot 8). On the first run day the byte that getter returns
// is cleared, if the getter starts `mov dl, [ecx + n]; xor eax, eax` as it
// does in build 641 (n is 0x1F).
void TutorialOff(const Day& day)
{
    if (!EnvInt("OPENSC4_TEST_TUTORIAL_OFF", 0) || !day.counted || day.day != day.first + 1 || !game::IsBuild641())
        return;
    char* const tutorial = Virtual<char*>(Global<void*>(addr::SC4_City), 0x148);
    if (!tutorial)
        return;
    const uint8_t* const code = At<const uint8_t* const*>(tutorial, 0x0)[8];
    if (code[0] == 0x8A && code[1] == 0x51 && code[3] == 0x33 && code[4] == 0xC0) {
        log::Write("autotest: tutorial flag at +0x%X was %d, cleared", code[2], At<uint8_t>(tutorial, code[2]));
        At<uint8_t>(tutorial, code[2]) = 0;
    } else {
        log::Write("autotest: IsTutorial is %02X %02X %02X %02X %02X %02X at %p: flag left alone", code[0], code[1], code[2],
                   code[3], code[4], code[5], static_cast<const void*>(code));
    }
}

const FingerprintPart kFingerprint{"disasters", 88, &Fingerprint};
const DayHook kPushRiots{"disasters.riots", 90, &PushRiots};
const DayHook kPushQuakes{"disasters.quakes", 90, &PushQuakes};
const DayHook kFireCalls{"disasters.firecalls", 91, &FireCalls};
const DayHook kPushFires{"disasters.fires", 92, &PushFires};
const DayHook kTutorialOff{"disasters.tutorial", 87, &TutorialOff};
const DayHook kEnd{"disasters.end", 89, &End};
const DayHook kDrive{"disasters.drive", 88, &Drive};

} // namespace
} // namespace opensc4::autotest
