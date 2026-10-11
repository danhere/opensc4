// The audio code's part of the autotest (src/core/autotest_hooks.h;
// docs/audio.md): switches that call the game's audio functions by address,
// so that a run with the audio replacements on and one with them off can be
// compared, and put what they leave into the fingerprint. Nothing here calls
// our own functions.
//
//   OPENSC4_TEST_AUDIO=<day>         token "audio": the mover and fade
//     arithmetic, a cluster set, the 65 event handler factories, the live
//     ambience manager read, read again and read after its tuning is read,
//     a private ambience manager, the sound manager's and the listener's
//     tuning, the music lists and the volume tuning. Each part has its own
//     sub-hash in the token and in the log.
//   OPENSC4_TEST_AUDIO_DIALOG=<day>  token "audiodlg": the audio options
//     dialog made, run with a player in place of the modal loop that
//     answers cancel, and let go. Nothing is saved.
//
// What is left out, on purpose: voices (they end in real time), fades and
// stamps, the listener's place (it follows the camera), heap addresses,
// and everything that starts or stops a sound.
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

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
    // A mark for something that is not there, so that a missing object
    // changes the hash instead of stopping the run.
    void Missing()
    {
        Add(uint8_t{0xEE});
    }
};

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

uint32_t Bits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

char g_audioToken[240];
char g_dialogToken[100];

// The audio system's static object (0x00B21978) and the objects it keeps.
char* Director()
{
    return reinterpret_cast<char*>(std::uintptr_t{0x00B21978});
}

// A list of the game's: a sentinel node first, each node next, previous.
constexpr int kMostNodes = 4096;

// A vector of 4-byte items at `vector` of `object`: its size and contents.
void AddVector(Fnv& f, char* object, std::size_t vector)
{
    const char* const begin = At<char*>(object, vector);
    const char* const end = At<char*>(object, vector + 4);
    if (!begin) {
        f.Missing();
        return;
    }
    const std::ptrdiff_t size = end - begin;
    if (size < 0 || size > 0x10000) {
        f.Missing();
        return;
    }
    f.Add(static_cast<uint32_t>(size));
    f.Bytes(begin, static_cast<std::size_t>(size));
}

// The text of a game string at `string`: its bytes from begin to end.
void AddString(Fnv& f, char* string)
{
    const char* const begin = At<char*>(string, 0x4);
    const char* const end = At<char*>(string, 0x8);
    if (begin && end >= begin && end - begin < 0x1000)
        f.Bytes(begin, static_cast<std::size_t>(end - begin));
    f.Add(uint8_t{0});
}

// --- 1. The mover and the fade --------------------------------------------------------

struct MoverCase {
    float start[3];
    float target[3];
    float placeRate;
    float levelRate;
    uint32_t level;
    uint32_t levelTarget;
};

// The mover (0x3C bytes) on a buffer of the test's own: made, placed, its
// rates and level set, stepped once on the spot, given a place and a level
// to walk to, and stepped by different times.
void DriveMovers(Fnv& f, int& count)
{
    const MoverCase cases[] = {
        {{100.0f, 20.0f, -50.0f}, {400.0f, 20.0f, 250.0f}, 500.0f, 2048.0f, 1024, 0},
        {{0.0f, 0.0f, 0.0f}, {3.0f, 4.0f, 0.0f}, 1.0f, 100.0f, 0, 700},
        {{-30.5f, 12.25f, 8.0f}, {-30.5f, 12.25f, 8.0f}, 10.0f, 0.0f, 300, 300},
    };
    const float times[] = {0.0f, 16.0f, 0.5f, 250.0f, 1000.0f, 100000.0f, 3.0e7f};
    for (const MoverCase& test : cases) {
        alignas(4) char mover[0x3C];
        std::memset(mover, 0, sizeof mover);
        This<char*>(0x004F81D0, mover);
        f.Bytes(mover, sizeof mover);
        uint32_t place[3] = {Bits(test.start[0]), Bits(test.start[1]), Bits(test.start[2])};
        This<void>(0x004F8240, mover, static_cast<const uint32_t*>(place), uint32_t{1});
        This<void>(0x004F7BD0, mover, test.level, uint32_t{1});
        This<void>(0x004F7BF0, mover, test.placeRate, test.levelRate);
        f.Bytes(mover, sizeof mover);
        This<void>(0x004F8650, mover, 0.0f); // the first step, on the spot
        f.Bytes(mover, sizeof mover);
        place[0] = Bits(test.target[0]);
        place[1] = Bits(test.target[1]);
        place[2] = Bits(test.target[2]);
        This<void>(0x004F8240, mover, static_cast<const uint32_t*>(place), uint32_t{0});
        This<void>(0x004F7BD0, mover, test.levelTarget, uint32_t{0});
        f.Bytes(mover, sizeof mover);
        for (const float time : times) {
            This<void>(0x004F8650, mover, time);
            f.Bytes(mover, sizeof mover);
            uint32_t where[3] = {};
            This<uint32_t*>(0x004F8280, mover, &where[0]);
            f.Bytes(where, sizeof where);
            ++count;
        }
        // A target taken at once.
        place[0] = Bits(1.0f);
        This<void>(0x004F8240, mover, static_cast<const uint32_t*>(place), uint32_t{1});
        This<void>(0x004F7BD0, mover, uint32_t{5}, uint32_t{1});
        f.Bytes(mover, sizeof mover);
    }
}

struct FadeCase {
    float now;
    float target;
    float rate;
};

// The fade (now, target, rate): stepped until it is there and once more.
void DriveFades(Fnv& f, int& count)
{
    const FadeCase cases[] = {
        {0.0f, 100.0f, 0.5f},   // up, then past
        {100.0f, 0.0f, -0.25f}, // down, then past
        {0.0f, 50.0f, 0.0f},    // no rate
        {5.0f, 5.0f, 1.0f},     // already there
        {10.0f, 20.0f, -1.0f},  // a rate the wrong way
        {-4.0f, 4.0f, 0.001f},  // slowly
    };
    const float times[] = {10.0f, 0.0f, 40.0f, 1000.0f, 1000.0f};
    for (const FadeCase& test : cases) {
        float fade[3] = {test.now, test.target, test.rate};
        for (const float time : times) {
            const uint8_t moved = static_cast<uint8_t>(This<uint8_t>(0x004F7C50, fade, time));
            f.Bytes(fade, sizeof fade);
            f.Add(moved);
            ++count;
        }
    }
}

// --- 2. The cluster set ----------------------------------------------------------------

struct Node {
    Node* next;
    Node* previous;
    void* value;
};

// The set's own state: clusters, the points in the map, and per cluster its
// count, peak, place and radius.
void AddSet(Fnv& f, char* set)
{
    f.Add(At<uint32_t>(set, 0x20));
    f.Add(At<uint32_t>(set, 0x18));
    Node* const head = At<Node*>(set, 0x1C);
    if (!head) {
        f.Missing();
        return;
    }
    int n = 0;
    for (Node* node = head->next; node != head && n < kMostNodes; node = node->next, ++n) {
        char* const cluster = static_cast<char*>(node->value);
        f.Add(At<uint32_t>(cluster, 0x4));
        f.Add(At<uint32_t>(cluster, 0x8));
        f.Bytes(cluster + 0xC, 12);
        f.Add(At<uint32_t>(cluster, 0x18));
    }
    f.Add(n);
}

// What SetCollect makes: nodes of 0x14 bytes in front of the list's
// sentinel, the place at +8. Taken and freed again.
void Collect(Fnv& f, char* set, const float* area, bool useArea)
{
    struct Sentinel {
        Node* next;
        Node* previous;
        uint32_t rest[3];
    } sentinel = {};
    sentinel.next = reinterpret_cast<Node*>(&sentinel);
    sentinel.previous = reinterpret_cast<Node*>(&sentinel);
    Node* list = reinterpret_cast<Node*>(&sentinel);
    uint32_t count = 0, total = 0;
    This<void>(0x004F0B40, set, area, &list, &count, &total, uint32_t{useArea});
    f.Add(count);
    f.Add(total);
    Node* const end = reinterpret_cast<Node*>(&sentinel);
    Node* node = sentinel.next;
    for (int n = 0; node != end && n < kMostNodes; ++n) {
        Node* const next = node->next;
        f.Bytes(reinterpret_cast<char*>(node) + 8, 12);
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(node));
        node = next;
    }
}

char* NewSet(float radius)
{
    void* const memory = Cdecl<void*>(addr::SC4_operator_new, std::size_t{0x28});
    if (!memory)
        return nullptr;
    char* const set = This<char*>(0x004F12E0, memory, Bits(radius));
    Virtual<uint32_t>(set, 0x4); // one reference
    return set;
}

void ReleaseSet(char* set)
{
    if (!set)
        return;
    This<uint8_t>(0x004F1200, set); // SetClearAll
    Virtual<uint32_t>(set, 0x8);
}

struct PointCase {
    uint32_t key;
    float place[3];
};

// Points under keys 1 to 20 at places that join the cluster of one of five
// centres (the set's radius is 10 and a point's 2, so within 12) or are too
// far and begin their own. A key whose point began a cluster is never taken
// out or moved: that is a null read in the original.
void DriveClusters(Fnv& f, int& count)
{
    char* const set = NewSet(10.0f);
    char* const other = NewSet(6.0f);
    char* const merged = NewSet(4.0f);
    if (!set || !other || !merged) {
        f.Missing();
        ReleaseSet(set);
        ReleaseSet(other);
        ReleaseSet(merged);
        return;
    }
    const PointCase points[] = {
        {1, {0, 0, 0}},        {2, {100, 0, 0}},      {3, {0, 0, 100}},    {4, {100, 0, 100}},
        {5, {200, 0, 50}},     {6, {3, 0, 4}},        {7, {99, 0, 4}},     {8, {-5, 0, 8}},
        {9, {5, 0, 105}},      {10, {104, 0, 96}},    {11, {198, 0, 55}},  {12, {11, 0, 0}},
        {13, {12.5f, 0, 0}},   {14, {0, 0, 111}},     {15, {96, 0, 0}},    {16, {90, 0, 100}},
        {17, {200, 0, 40}},    {18, {-60, 0, -60}},   {19, {-62, 0, -59}}, {20, {1, 0, 1}},
    };
    uint32_t joiners[32];
    int joinerCount = 0;
    for (const PointCase& point : points) {
        const uint32_t clusters = At<uint32_t>(set, 0x20);
        const uint32_t place[3] = {Bits(point.place[0]), Bits(point.place[1]), Bits(point.place[2])};
        f.Add(static_cast<uint8_t>(This<uint8_t>(0x004F1210, set, point.key, static_cast<const uint32_t*>(place), Bits(2.0f))));
        if (At<uint32_t>(set, 0x20) == clusters && joinerCount < 32)
            joiners[joinerCount++] = point.key;
        ++count;
    }
    {
        // A key that is there already is refused.
        const uint32_t place[3] = {Bits(0.0f), Bits(0.0f), Bits(0.0f)};
        f.Add(static_cast<uint8_t>(This<uint8_t>(0x004F1210, set, uint32_t{1}, static_cast<const uint32_t*>(place), Bits(2.0f))));
    }
    f.Add(joinerCount);
    AddSet(f, set);

    const float counted[][3] = {{1, 0, 1}, {101, 0, 1}, {50, 0, 50}, {300, 0, 300}, {2, 0, 2}};
    for (const auto& place : counted) {
        This<void>(0x004F0C20, set, static_cast<const float*>(place), 1.0f);
        AddSet(f, set);
        ++count;
    }

    // Joiners moved: one close to where it was, one to another cluster, one
    // far away (it begins a cluster then, and is not moved again).
    const float moves[][3] = {{1, 0, 1}, {101, 0, 2}, {-200, 0, -200}, {4, 0, 99}};
    for (int i = 0; i < 4 && i < joinerCount; ++i) {
        const uint32_t key = joiners[i * 2 < joinerCount ? i * 2 : i];
        const uint32_t place[3] = {Bits(moves[i][0]), Bits(moves[i][1]), Bits(moves[i][2])};
        f.Add(static_cast<uint8_t>(This<uint8_t>(0x004F12B0, set, key, static_cast<const uint32_t*>(place), Bits(2.0f))));
        AddSet(f, set);
        ++count;
    }
    // A joiner not moved above taken out, and a key that is not there.
    for (int i = joinerCount - 1; i >= 0; --i) {
        const uint32_t key = joiners[i];
        if (key == 20) {
            f.Add(static_cast<uint8_t>(This<uint8_t>(0x004F0AC0, set, key)));
            break;
        }
    }
    f.Add(static_cast<uint8_t>(This<uint8_t>(0x004F0AC0, set, uint32_t{9999})));
    AddSet(f, set);

    const float area[4] = {-50.0f, -50.0f, 150.0f, 150.0f};
    Collect(f, set, area, false);
    Collect(f, set, area, true);

    // The second set and the merges.
    const float others[][3] = {{2, 0, 3}, {99, 0, 2}, {150, 0, 150}, {0, 0, 98}, {-61, 0, -60}, {3, 0, 3}};
    for (const auto& place : others) {
        This<void>(0x004F0C20, other, static_cast<const float*>(place), 2.0f);
        ++count;
    }
    AddSet(f, other);
    This<void>(0x004F0EA0, merged, set, other, uint32_t{0});
    AddSet(f, merged);
    This<void>(0x004F0EA0, merged, set, other, uint32_t{1});
    AddSet(f, merged);
    Collect(f, merged, area, false);

    // Cleared and kept, the empty clusters dropped, points added again,
    // cleared and freed.
    This<void>(0x004F0DE0, set, uint32_t{1});
    AddSet(f, set);
    This<void>(0x004F0A30, set);
    AddSet(f, set);
    for (uint32_t key = 100; key < 106; ++key) {
        const uint32_t place[3] = {Bits(static_cast<float>(key) - 100.0f), Bits(0.0f), Bits(0.0f)};
        This<uint8_t>(0x004F1210, set, key, static_cast<const uint32_t*>(place), Bits(2.0f));
    }
    AddSet(f, set);
    This<void>(0x004F0DE0, set, uint32_t{0});
    AddSet(f, set);
    f.Add(static_cast<uint8_t>(This<uint8_t>(0x004F1200, set)));
    AddSet(f, set);

    ReleaseSet(merged);
    ReleaseSet(other);
    ReleaseSet(set);
}

// --- 3. The event handler factories -------------------------------------------------------

// A handler of 0x10 bytes (reference count at +0xC) or 0x14 (at +0x10): a
// factory leaves the count at 0, and Release frees it when the count is 1
// or less, so one reference is taken first and let go last.
void DriveHandler(Fnv& f, std::uintptr_t factory, bool timed, int& made)
{
    char* const handler = Cdecl<char*>(factory);
    if (!handler) {
        f.Missing();
        return;
    }
    ++made;
    f.Add(At<uint32_t>(handler, 0x0));
    f.Add(At<uint32_t>(handler, 0x4));
    f.Add(At<uint32_t>(handler, 0x8));
    if (timed) {
        f.Add(At<uint8_t>(handler, 0xC));
        f.Add(At<uint32_t>(handler, 0x10));
    } else {
        f.Add(At<uint32_t>(handler, 0xC));
    }
    f.Add(Virtual<uint32_t>(handler, 0x4)); // one reference
    void* out = nullptr;
    f.Add(static_cast<uint8_t>(Virtual<uint8_t>(handler, 0x0, uint32_t{0x12345678}, &out)));
    f.Add(out != nullptr);
    for (const uint32_t id : {0x4A40074Cu, 0xCA400757u}) {
        out = nullptr;
        f.Add(static_cast<uint8_t>(Virtual<uint8_t>(handler, 0x0, id, &out)));
        f.Add(out == handler);
        if (out)
            f.Add(Virtual<uint32_t>(out, 0x8)); // the reference the answer took
    }
    f.Add(Virtual<uint32_t>(handler, 0x8)); // the last one: freed
}

void DriveHandlers(Fnv& f, int& made)
{
    for (int i = 0; i < 30; ++i)
        DriveHandler(f, 0x004EB260 + 0x30 * static_cast<std::uintptr_t>(i), false, made);
    for (int i = 0; i < 13; ++i)
        DriveHandler(f, 0x004EB800 + 0x30 * static_cast<std::uintptr_t>(i), true, made);
    for (int i = 0; i < 22; ++i)
        DriveHandler(f, 0x004EBAA0 + 0x30 * static_cast<std::uintptr_t>(i), true, made);
}

// --- 4. The ambience manager ----------------------------------------------------------------

void AddLayer(Fnv& f, char* layer)
{
    AddString(f, layer + 0x188);
    for (const std::size_t at : {std::size_t{0x20}, std::size_t{0x24}, std::size_t{0x200}, std::size_t{0x34}, std::size_t{0x38},
                                 std::size_t{0x1D0}, std::size_t{0x1E8}, std::size_t{0x1EC}, std::size_t{0x1F0},
                                 std::size_t{0x1F8}, std::size_t{0x220}, std::size_t{0x23C}, std::size_t{0x258},
                                 std::size_t{0x138}, std::size_t{0x13C}, std::size_t{0x140}, std::size_t{0x108},
                                 std::size_t{0x10C}})
        f.Add(At<uint32_t>(layer, at));
    f.Add(At<uint8_t>(layer, 0x1E0));
    f.Add(At<uint8_t>(layer, 0x1E1));
    for (const std::size_t at : {std::size_t{0x1D4}, std::size_t{0x208}, std::size_t{0x224}, std::size_t{0x240}})
        AddVector(f, layer, at);
    char* const sentinel = At<char*>(layer, 0x180);
    if (!sentinel) {
        f.Missing();
        return;
    }
    int n = 0;
    for (char* node = At<char*>(sentinel, 0x0); node != sentinel && n < kMostNodes; node = At<char*>(node, 0x0), ++n) {
        f.Add(At<uint32_t>(node, 0x14));
        f.Add(At<uint32_t>(node, 0x18));
        f.Add(At<uint32_t>(node, 0x1C));
    }
    f.Add(n);
}

// The filters' table (0x00B21958): the classes in it, bucket by bucket.
void AddFilterTable(Fnv& f)
{
    char* const table = reinterpret_cast<char*>(std::uintptr_t{0x00B21958});
    char** const buckets = At<char**>(table, 0x4);
    char** const end = At<char**>(table, 0x8);
    if (!buckets || end < buckets || end - buckets > 0x10000) {
        f.Add(uint32_t{0});
        return;
    }
    uint32_t nodes = 0;
    for (char** bucket = buckets; bucket != end; ++bucket) {
        int n = 0;
        for (char* node = *bucket; node && n < 1000; node = At<char*>(node, 0x0), ++n) {
            f.Add(At<uint32_t>(node, 0x4));
            ++nodes;
        }
    }
    f.Add(nodes);
}

int AddAmbience(Fnv& f, char* manager)
{
    f.Add(At<uint8_t>(manager, 0x50));
    f.Add(At<uint32_t>(manager, 0x11C));
    AddVector(f, manager, 0x54);
    AddVector(f, manager, 0x60);
    int layers = 0;
    char* const sentinel = At<char*>(manager, 0x2C);
    if (sentinel) {
        for (char* node = At<char*>(sentinel, 0x0); node != sentinel && layers < kMostNodes; node = At<char*>(node, 0x0), ++layers) {
            if (char* const layer = At<char*>(node, 0x8))
                AddLayer(f, layer);
            else
                f.Missing();
        }
    } else {
        f.Missing();
    }
    f.Add(layers);
    int timed = 0;
    char* const head = At<char*>(manager, 0x4C);
    if (head) {
        for (char* node = At<char*>(head, 0x0); node != head && timed < kMostNodes; node = At<char*>(node, 0x0), ++timed) {
            f.Add(At<uint32_t>(node, 0xC));
            f.Add(At<uint32_t>(node, 0x14));
        }
    } else {
        f.Missing();
    }
    f.Add(timed);
    f.Add(Global<uint32_t>(0x00B21944));
    AddFilterTable(f);
    return layers;
}

// Read, read again, then the tuning read: the layers' (with their own
// instance), the layers' freshness scores (which add nothing) and the
// manager's.
void DriveAmbience(Fnv& f, int& layersSeen)
{
    char* const manager = At<char*>(Director(), 0x54);
    if (!manager) {
        f.Missing();
        return;
    }
    layersSeen = AddAmbience(f, manager);
    AddAmbience(f, manager);
    char* const sentinel = At<char*>(manager, 0x2C);
    if (sentinel) {
        int n = 0;
        for (char* node = At<char*>(sentinel, 0x0); node != sentinel && n < kMostNodes; node = At<char*>(node, 0x0), ++n) {
            char* const layer = At<char*>(node, 0x8);
            if (!layer)
                continue;
            f.Add(static_cast<uint8_t>(This<uint8_t>(0x004E7A90, layer, At<uint32_t>(layer, 0x1F8))));
            Virtual<void>(layer, 0x38);
        }
    }
    This<void>(0x004E91D0, manager);
    AddAmbience(f, manager);
}

// --- 5. A private ambience manager ---------------------------------------------------------------

void DrivePrivateManager(Fnv& f)
{
    char* const manager = Cdecl<char*>(0x004EB0C0);
    if (!manager) {
        f.Missing();
        return;
    }
    for (const std::size_t at : {std::size_t{0x0}, std::size_t{0x4}, std::size_t{0x8}, std::size_t{0xC}, std::size_t{0x10},
                                 std::size_t{0x14}, std::size_t{0xB0}, std::size_t{0xE0}, std::size_t{0x11C}})
        f.Add(At<uint32_t>(manager, at));
    f.Add(At<uint8_t>(manager, 0x18));
    f.Add(At<uint8_t>(manager, 0x19));
    f.Add(At<uint8_t>(manager, 0x50));
    for (const std::size_t at : {std::size_t{0xB4}, std::size_t{0xB5}, std::size_t{0xB6}})
        f.Add(At<uint8_t>(manager, at));
    for (std::size_t at = 0x34; at <= 0x48; at += 4)
        f.Add(At<uint32_t>(manager, at));
    for (const std::size_t at : {std::size_t{0x1C}, std::size_t{0x20}, std::size_t{0x24}, std::size_t{0x28}})
        f.Add(At<void*>(manager, at) != nullptr);
    for (const std::size_t at : {std::size_t{0x2C}, std::size_t{0x4C}}) {
        char* const head = At<char*>(manager, at);
        f.Add(head && At<char*>(head, 0x0) == head && At<char*>(head, 0x4) == head);
    }
    f.Add(Virtual<uint32_t>(manager, 0x8)); // one Release frees it: the count is 0
}

// --- 6. The sound manager ---------------------------------------------------------------------------

void AddCurve(Fnv& f, char* curve)
{
    char* const begin = At<char*>(curve, 0x0);
    const uint32_t count = At<uint32_t>(curve, 0xC);
    f.Add(count);
    if (begin && count <= 256)
        f.Bytes(begin, count * 20u);
    else
        f.Missing();
}

void AddSoundManager(Fnv& f, char* manager)
{
    f.Add(At<uint32_t>(manager, 0x14));
    f.Add(At<uint32_t>(manager, 0x64));
    f.Add(At<uint32_t>(manager, 0x68));
    f.Add(At<uint32_t>(manager, 0x6C));
    AddCurve(f, manager + 0x70);
}

void DriveSoundManager(Fnv& f)
{
    char* const manager = At<char*>(Director(), 0x60);
    if (!manager) {
        f.Missing();
        return;
    }
    AddSoundManager(f, manager);
    f.Add(static_cast<uint8_t>(This<uint8_t>(0x004F9250, manager)));
    AddSoundManager(f, manager);
}

// --- 7. The listener --------------------------------------------------------------------------------------

void AddListener(Fnv& f, char* listener)
{
    for (const std::size_t at : {std::size_t{0x3C}, std::size_t{0x48}, std::size_t{0x54}, std::size_t{0x60}})
        AddVector(f, listener, at);
    f.Add(At<uint32_t>(listener, 0x6C));
    f.Add(At<uint32_t>(listener, 0x70));
    f.Bytes(reinterpret_cast<void*>(std::uintptr_t{0x00B21A20}), 5 * sizeof(float));
}

// The tuning writes five floats into the first two vectors when their
// property is missing, so it is read only when both hold five.
bool HasFive(char* listener, std::size_t vector)
{
    char* const begin = At<char*>(listener, vector);
    char* const end = At<char*>(listener, vector + 4);
    return begin && end - begin >= 5 * static_cast<std::ptrdiff_t>(sizeof(float));
}

void DriveListener(Fnv& f)
{
    char* const listener = At<char*>(Director(), 0x64);
    if (!listener) {
        f.Missing();
        return;
    }
    AddListener(f, listener);
    if (HasFive(listener, 0x3C) && HasFive(listener, 0x48))
        This<void>(0x004F1A80, listener);
    else
        f.Missing();
    This<void>(0x004F18C0, listener);
    AddListener(f, listener);
}

// --- 8. The music lists and the volume tuning ----------------------------------------------------------------

void DriveMusic(Fnv& f, int& tracks)
{
    Cdecl<void>(0x004F2D40); // EnsurePlaylists
    f.Add(Global<uint8_t>(0x00B21A39));
    for (int i = 0; i < 2; ++i) {
        char* const list = reinterpret_cast<char*>(std::uintptr_t{0x00B21A88}) + 0x30 * i;
        f.Add(At<uint32_t>(list, 0x0));
        f.Add(At<uint32_t>(list, 0x4));
        f.Add(At<uint32_t>(list, 0x14));
        char* const begin = At<char*>(list, 0x8);
        char* const end = At<char*>(list, 0xC);
        const std::ptrdiff_t bytes = end - begin;
        if (!begin || bytes < 0 || bytes > 30 * 16 * 4) {
            f.Missing();
        } else {
            f.Add(static_cast<uint32_t>(bytes / 16));
            for (char* track = begin; track != end; track += 16) {
                f.Add(At<uint32_t>(track, 0x0));
                f.Add(At<uint32_t>(track, 0x4));
                f.Add(At<uint32_t>(track, 0x8));
                f.Add(At<uint8_t>(track, 0xC));
                ++tracks;
            }
        }
        f.Add(static_cast<uint32_t>(At<char*>(list, 0x24) - At<char*>(list, 0x20)));
    }
    for (const int32_t index : {-1, 0, 1, 2}) {
        void* const found = Cdecl<void*>(0x004F1F70, index);
        f.Add(found != nullptr);
        f.Add(found == reinterpret_cast<void*>(std::uintptr_t{0x00B21A88} + 0x30 * static_cast<std::uintptr_t>(index < 0 ? 0 : index)));
    }
    Cdecl<void>(0x004F22C0); // ReadVolumeTuning
    for (const std::uintptr_t at : {std::uintptr_t{0x00B21A3C}, std::uintptr_t{0x00B21A40}, std::uintptr_t{0x00B09884},
                                    std::uintptr_t{0x00B09888}, std::uintptr_t{0x00B0988C}})
        f.Add(Global<uint32_t>(at));
    for (const uint32_t which : {0u, 1u})
        for (const uint32_t position : {0u, 1u, 500u, 999u, 1000u})
            f.Add(Cdecl<uint32_t>(0x004F1F90, position, which));
}

// --- OPENSC4_TEST_AUDIO ------------------------------------------------------------------------------------------

void DriveAudio(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_AUDIO") || !game::IsBuild641())
        return;
    Fnv movers, fades, clusters, handlers, ambience, privateManager, sounds, listener, music;
    int moverSteps = 0, fadeSteps = 0, clusterSteps = 0, handlersMade = 0, layers = 0, tracks = 0;
    DriveMovers(movers, moverSteps);
    DriveFades(fades, fadeSteps);
    DriveClusters(clusters, clusterSteps);
    DriveHandlers(handlers, handlersMade);
    DriveAmbience(ambience, layers);
    DrivePrivateManager(privateManager);
    DriveSoundManager(sounds);
    DriveListener(listener);
    DriveMusic(music, tracks);
    log::Write("autotest: audio: mover %08X (%d steps) fade %08X (%d) clusters %08X (%d) handlers %08X (%d made)", movers.h, moverSteps,
               fades.h, fadeSteps, clusters.h, clusterSteps, handlers.h, handlersMade);
    log::Write("autotest: audio: ambience %08X (%d layers) private %08X sound %08X listener %08X music %08X (%d tracks)", ambience.h,
               layers, privateManager.h, sounds.h, listener.h, music.h, tracks);
    // One short hash: the game cuts the daily fingerprint line at 4093 characters.
    Fnv all;
    for (const uint32_t h : {movers.h, fades.h, clusters.h, handlers.h, ambience.h, privateManager.h, sounds.h, listener.h, music.h})
        all.Add(h);
    std::snprintf(g_audioToken, sizeof g_audioToken, " audio %08X", all.h);
}

const DayHook kAudio{"audio", 181, &DriveAudio};

// --- OPENSC4_TEST_AUDIO_DIALOG -----------------------------------------------------------------------------------------

char* g_dialog = nullptr;
Fnv g_played;

// The options dialog's player: what the dialog holds when it is up, the
// leave-out switch pressed through the window procedure and read back, then
// cancel, which puts the settings back. The leave-out byte is put back by
// hand at the end in case a message was not taken. Nothing is accepted.
uint32_t PlayDialog(void* window, int depth)
{
    char* const dialog = g_dialog;
    g_played.Add(depth);
    g_played.Add(window != nullptr);
    char* const options = Global<char*>(0x00B43CD8);
    if (!dialog || !window || !options) {
        g_played.Missing();
        return 0;
    }
    char* const audio = options + 4;
    g_played.Add(At<void*>(dialog, 0x8) != nullptr);
    g_played.Bytes(dialog + 0x14, 0x68);
    g_played.Add(Global<uint8_t>(0x00B21A38));
    const uint8_t leaveOut = At<uint8_t>(audio, 0x64);
    g_played.Add(leaveOut);
    uint32_t message[4] = {3, 0x287259F7, 0x8A550C5A, static_cast<uint32_t>(leaveOut == 0)};
    g_played.Add(static_cast<uint8_t>(Virtual<uint8_t>(dialog, 0xC, window, &message[0])));
    g_played.Add(At<uint8_t>(audio, 0x64));
    uint32_t cancel[4] = {3, 0x287259F6, 0xEA5509E3, 0};
    g_played.Add(static_cast<uint8_t>(Virtual<uint8_t>(dialog, 0xC, window, &cancel[0])));
    g_played.Add(At<void*>(dialog, 0x4) != nullptr);
    g_played.Add(At<uint8_t>(audio, 0x64));
    At<uint8_t>(audio, 0x64) = leaveOut;
    return 0;
}

void RunDialog(void*)
{
    This<uint8_t>(0x004F25F0, g_dialog);
}

void DriveAudioDialog(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_AUDIO_DIALOG") || !game::IsBuild641())
        return;
    Fnv f;
    int ran = 0;
    if (!Global<void*>(0x00B43CD8)) {
        f.Missing();
    } else if (void* const memory = Cdecl<void*>(addr::SC4_operator_new, std::size_t{0x80})) {
        char* const dialog = This<char*>(0x004F2450, memory);
        Virtual<uint32_t>(dialog, 0x4);
        const uint8_t loaded = Virtual<uint8_t>(dialog, 0x14); // Init
        f.Add(loaded);
        f.Add(At<void*>(dialog, 0x4) != nullptr);
        if (loaded && At<void*>(dialog, 0x4)) {
            g_dialog = dialog;
            g_played = Fnv{};
            ran = RunModalPlayer(&PlayDialog, &RunDialog, nullptr) ? 1 : 0;
            f.Add(g_played.h);
            g_dialog = nullptr;
        }
        f.Add(At<void*>(dialog, 0x4) != nullptr);
        f.Add(Global<uint8_t>(0x00B21A38));
        f.Add(Virtual<uint32_t>(dialog, 0x8)); // let go: freed
    }
    std::snprintf(g_dialogToken, sizeof g_dialogToken, " audiodlg %d %08X", ran, f.h);
    log::Write("autotest: audio dialog driven:%s", g_dialogToken);
}

const DayHook kAudioDialog{"audio.dialog", 182, &DriveAudioDialog};

void Fingerprint(cISC4City*, std::string& line)
{
    line += g_audioToken;
    line += g_dialogToken;
}

const FingerprintPart kFingerprint{"audio", 5, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
