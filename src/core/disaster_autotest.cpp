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
    // The managers' return values differ; the layer's counts show a start.
    char* const self = Layer(day.city);
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
                    return;
                }
            }
        }
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

const FingerprintPart kFingerprint{"disasters", 88, &Fingerprint};
const DayHook kEnd{"disasters.end", 89, &End};
const DayHook kDrive{"disasters.drive", 88, &Drive};

} // namespace
} // namespace opensc4::autotest
