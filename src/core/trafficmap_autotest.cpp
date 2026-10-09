// The traffic network map's part of the autotest (src/core/autotest_hooks.h;
// docs/traffic-map.md).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/trafficmap.h"

#include <cISC4City.h>

#include <cstdint>
#include <cstdio>
#include <vector>

namespace opensc4::autotest {
namespace {

using namespace opensc4::trafficmap;

struct Fnv {
    uint32_t h = 2166136261u;
    void Bytes(const void* data, std::size_t size)
    {
        const auto* b = static_cast<const uint8_t*>(data);
        for (std::size_t i = 0; i < size; ++i)
            h = (h ^ b[i]) * 16777619u;
    }
    template <typename T>
    void Add(const T& value)
    {
        Bytes(&value, sizeof value);
    }
};

uint32_t ListCount(const ListNode* head)
{
    uint32_t n = 0;
    for (const ListNode* node = head ? head->next : nullptr; node && node != head; node = node->next)
        ++n;
    return n;
}

uint32_t Bytes(const GameVector& v)
{
    return static_cast<uint32_t>(static_cast<const char*>(v.end) - static_cast<const char*>(v.begin));
}

// The map's state without heap addresses: the cell records in cell order
// (their counts and traffic figures, the lane and path entries), the hash
// tables keyed by pointers as order-free sums of per-node hashes, the walks,
// timers and blink states, and the map's own random generator.
void Fingerprint(cISC4City* city, std::string& line)
{
    void* const map = reinterpret_cast<void*>(city->GetTrafficNetwork());
    if (!map || !game::IsBuild641() ||
        Virtual<uint32_t>(static_cast<char*>(map) + 0xC, 0x14) != 0x6A0F82B2u || !At<uint8_t>(map, field::kInitialized)) {
        line += " | tm none";
        return;
    }
    Fnv cells, layout;
    uint32_t records = 0;
    const int32_t width = At<int32_t>(map, field::kWidth), height = At<int32_t>(map, field::kHeight);
    for (int32_t z = 0; z < height; ++z)
        for (int32_t x = 0; x < width; ++x)
            for (const CellRecord* r = CellAt(map, x, z); r; r = r->next) {
                ++records;
                layout.Add(x);
                layout.Add(z);
                cells.Add(x);
                cells.Add(z);
                cells.Add(ListCount(r->automata));
                cells.Add(r->flags20);
                cells.Add(r->flags24);
                cells.Add(r->flags28);
                cells.Add(r->approachTraffic);
                cells.Add(r->turnTraffic);
                cells.Add(Bytes(r->lanes));
                // Entries without their padding, which the game fills from
                // the stack: lanes {key, u32, u32, u16}, paths {key, float, u16}.
                for (auto* v = static_cast<const GameVector*>(r->lanes.begin); v != r->lanes.end; ++v) {
                    cells.Add(Bytes(*v));
                    for (auto* e = static_cast<const char*>(v->begin); e != v->end; e += 16) {
                        cells.Bytes(e, 12);
                        cells.Bytes(e + 12, 2);
                    }
                }
                cells.Add(Bytes(r->paths));
                for (auto* e = static_cast<const char*>(r->paths.begin); e != r->paths.end; e += 12) {
                    cells.Bytes(e, 8);
                    cells.Bytes(e + 8, 2);
                }
            }

    const Hashtable& signals = Table(map, field::kSignals);
    uint32_t signalSum = 0;
    for (auto* node = First<SignalNode>(signals); node; node = Next(signals, node)) {
        Fnv h;
        // A kind-0 approach's times are never read (left uninitialised).
        for (const Approach& a : node->approaches) {
            h.Add(a.kind);
            h.Add(a.green);
            h.Add(a.amber);
            if (a.kind) {
                h.Add(a.greenTime);
                h.Add(a.redTime);
                h.Add(a.timer);
            }
        }
        h.Add(node->byte58);
        h.Add(Bytes(node->objects));
        h.Add(ListCount(node->waiting));
        h.Add(node->changed);
        signalSum += h.h;
    }
    const Hashtable& lanes = Table(map, field::kLanes);
    uint32_t laneSum = 0;
    for (auto* node = First<LaneNode>(lanes); node; node = Next(lanes, node)) {
        Fnv h;
        for (const LaneWay& way : node->ways) {
            h.Add(Bytes(way.values));
            h.Bytes(way.values.begin, Bytes(way.values));
            if (way.values.begin != way.values.end) // an empty way's index is never read
                h.Add(way.index);
        }
        laneSum += h.h;
    }
    const Hashtable& objects = Table(map, field::kCellObjects);
    uint32_t objectSum = 0;
    for (auto* node = First<CellObjectsNode>(objects); node; node = Next(objects, node)) {
        Fnv h;
        h.Add(node->key);
        h.Add(Bytes(node->objects));
        objectSum += h.h;
    }

    Fnv state;
    state.Add(At<uint8_t>(map, field::kSignalsChanged));
    state.Add(At<uint8_t>(map, field::kRectDirty));
    state.Bytes(static_cast<char*>(map) + field::kRect, 16);
    state.Add(At<uint8_t>(map, field::kPaused));
    state.Add(At<int32_t>(map, field::kWalkX));
    state.Add(At<int32_t>(map, field::kWalkZ));
    state.Add(At<float>(map, field::kBlinkTimerA));
    state.Add(At<uint8_t>(map, field::kBlinkA));
    state.Add(At<float>(map, field::kBlinkTimerB));
    state.Add(At<uint8_t>(map, field::kBlinkB));
    state.Bytes(static_cast<char*>(map) + field::kUpdateX, 12);
    // The map runs on frame time (docs/traffic-map.md), so most of its state
    // differs from run to run even with the original game. By default the
    // line has only what does not: the records' cells and the table sizes.
    // OPENSC4_TEST_TRAFFICMAP=1 adds the rest, for runs of 0 days.
    char text[128];
    if (Env("OPENSC4_TEST_TRAFFICMAP") != "1") {
        std::snprintf(text, sizeof text, " | tm %u %08X %u %u %u", records, layout.h, signals.count, lanes.count,
                      objects.count);
        line += text;
        return;
    }
    std::snprintf(text, sizeof text, " | tm %u %08X %u %08X %u %08X %u %08X %08X", records, cells.h, signals.count,
                  signalSum, lanes.count, laneSum, objects.count, objectSum, state.h);
    line += text;
    // The map's generator is drawn from by frame-driven code (a number of
    // draws that differs from run to run even with the original), so it is
    // in the line only on request: OPENSC4_TEST_TRAFFICMAP_RANDOM=1.
    if (Env("OPENSC4_TEST_TRAFFICMAP_RANDOM") == "1") {
        Fnv random;
        random.Bytes(static_cast<char*>(map) + field::kRandom, field::kView - field::kRandom);
        std::snprintf(text, sizeof text, " rnd %08X", random.h);
        line += text;
    }
}

const FingerprintPart kFingerprint{"trafficmap", 95, &Fingerprint};

// OPENSC4_TEST_TRAFFICMAP_CYCLE=<n>: right after the load, n network
// occupants spread over the grid (in cell order) are taken off the map (slot
// 49) and put back (slot 48), which runs the occupant, signal and lane code
// that unattended runs otherwise never reach. Not frame-driven, so a run of 0
// days compares it exactly.
void OnLoad(cISC4City* city)
{
    const int count = EnvInt("OPENSC4_TEST_TRAFFICMAP_CYCLE", 0);
    void* const map = reinterpret_cast<void*>(city->GetTrafficNetwork());
    if (count <= 0 || !map || !game::IsBuild641() || !At<uint8_t>(map, field::kInitialized))
        return;
    std::vector<void*> all;
    const int32_t width = At<int32_t>(map, field::kWidth), height = At<int32_t>(map, field::kHeight);
    for (int32_t z = 0; z < height; ++z)
        for (int32_t x = 0; x < width; ++x)
            for (const CellRecord* r = CellAt(map, x, z); r; r = r->next)
                if (r->occupant && (all.empty() || all.back() != r->occupant))
                    all.push_back(r->occupant);
    std::vector<void*> chosen;
    const std::size_t step = all.size() / static_cast<std::size_t>(count) + 1;
    for (std::size_t i = 0; i < all.size() && chosen.size() < static_cast<std::size_t>(count); i += step) {
        bool seen = false;
        for (void* o : chosen)
            seen = seen || o == all[i];
        if (!seen)
            chosen.push_back(all[i]);
    }
    for (void* occupant : chosen) {
        Virtual<uint32_t>(occupant, 0x4);
        Virtual<uint8_t>(map, 0xC4, occupant);
        Virtual<uint8_t>(map, 0xC0, occupant, 0u);
        Virtual<uint32_t>(occupant, 0x8);
    }
}

const LoadHook kLoad{"trafficmap", 95, &OnLoad};

} // namespace
} // namespace opensc4::autotest
