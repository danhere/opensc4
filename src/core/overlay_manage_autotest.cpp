// The overlay manager's part of the autotest (src/core/autotest_hooks.h;
// docs/renderer.md, "The overlays", "Bookkeeping").
//
//   OPENSC4_TEST_OVERLAYS=<day>   token "ovl": on that day, drives every
//                                 function of renderer.overlays.manage1 on
//                                 the city's four overlay managers (the
//                                 terrain view's +0x6C to +0x78) through
//                                 their vtables, or by address for the ones
//                                 the vtable does not reach, and hashes what
//                                 they give back.
//
// Every call leaves the managers as they were, apart from the order of the
// nodes in the buckets of the overlays it moves and places (a moved decal
// goes back to its place, a placed overlay is taken out of its buckets and
// put in once again) and the redraws it asks for. For each overlay in use:
// the five getters, then the four setters with what the getters gave (the
// flags' with 0, which sets nothing). For each decal handle in use: the
// getters and DecalInfo; a move to where it is, two cells east and back;
// UpdateDecalInfo with its own description; the setters with the getters'
// values unless its target is a group (whose eight overlays the getters do
// not all describe). Then on the first overlay in use SetHeightMapParams
// (its matrix and flags put back after), Place (its nodes then taken out and
// put back once), QueryInterface for the manager's interface and for
// cIGZUnknown, GetStatsString into a stand-in string and the options message
// to the message target at +0x10. Works muted and in the original game.
// The token leaves out what frames change (alphas, decal descriptions, the
// draw counts in the stats text).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <cISC4City.h>

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace opensc4::autotest {
namespace {

constexpr std::size_t kOverlays = 0x98, kHandles = 0xAC, kWaterRows = 0x15;
constexpr std::size_t kOverlaySize = 0xB4, kHandleSize = 0x30;
constexpr std::size_t kTarget = 0x2C, kCells = 0xC, kFlags = 0x4, kMatrix = 0x1C;
constexpr uint32_t kHandleBit = 0x80000000;
constexpr uint32_t kIidOverlayManager = 0x895D6544, kIidUnknown = 1;
constexpr uint32_t kMessageOptions = 0xC9F775BB;

bool g_on = false;
uint32_t g_hash = 0x811C9DC5;
uint32_t g_overlays = 0, g_handles = 0, g_groups = 0, g_managers = 0;

void Mix(const void* data, std::size_t size)
{
    const auto* p = static_cast<const uint8_t*>(data);
    for (std::size_t i = 0; i < size; ++i)
        g_hash = (g_hash ^ p[i]) * 0x01000193u;
}

template <typename T>
void Mix(const T& value)
{
    Mix(&value, sizeof value);
}

// A stand-in cIGZString: Sprintf (slot 0x6C, cdecl) and ToChar (slot 0x14).
struct Text {
    void** vtable;
    char buffer[128];
};

void __cdecl TextSprintf(Text* self, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    std::vsnprintf(self->buffer, sizeof self->buffer, format, args);
    va_end(args);
}

const char* __fastcall TextToChar(Text* self, void*)
{
    return self->buffer;
}

void* g_textVtable[0x6C / 4 + 1] = {};

// A stand-in message whose type (slot 0x10) is the options message.
struct Message {
    void** vtable;
};

uint32_t __fastcall MessageType(Message*, void*)
{
    return kMessageOptions;
}

void* g_messageVtable[0x10 / 4 + 1] = {};

// The getters' values of an ID, mixed in.
struct Values {
    uint32_t enabled, alpha, colour[3], mode, flags;
};

Values Read(void* m, uint32_t id)
{
    Values v{};
    v.enabled = Virtual<uint32_t>(m, 0x58, id) & 0xFF;
    const float alpha = Virtual<float>(m, 0x5C, id);
    std::memcpy(&v.alpha, &alpha, sizeof alpha);
    Virtual<void*>(m, 0x60, static_cast<void*>(v.colour), id);
    v.mode = Virtual<uint32_t>(m, 0x64, id) & 0xFF;
    v.flags = Virtual<uint32_t>(m, 0x68, id);
    // The alpha is left out: fades step it by frame time.
    Mix(v.enabled);
    Mix(v.colour);
    Mix(v.mode);
    Mix(v.flags);
    return v;
}

void WriteBack(void* m, uint32_t id, const Values& v)
{
    Virtual<void>(m, 0x40, id, v.enabled);
    float alpha;
    std::memcpy(&alpha, &v.alpha, sizeof alpha);
    Virtual<void>(m, 0x44, id, alpha);
    Virtual<void>(m, 0x48, id, static_cast<const void*>(v.colour));
    Virtual<void>(m, 0x4C, id, v.mode);
    Virtual<void>(m, 0x50, id, 0u);
}

uint32_t Used(void* m, std::size_t vector, std::size_t size)
{
    return static_cast<uint32_t>((At<char*>(m, vector + 4) - At<char*>(m, vector)) / size);
}

void Drive(void* m)
{
    ++g_managers;
    const uint32_t overlays = Used(m, kOverlays, kOverlaySize);
    uint32_t first = 0xFFFFFFFF;
    for (uint32_t i = 0; i < overlays; ++i) {
        if (At<int32_t>(At<char*>(m, kOverlays) + i * kOverlaySize, 0) != -1)
            continue;
        if (first == 0xFFFFFFFF)
            first = i;
        ++g_overlays;
        WriteBack(m, i, Read(m, i));
    }
    const uint32_t handles = Used(m, kHandles, kHandleSize);
    for (uint32_t h = 0; h < handles; ++h) {
        char* const handle = At<char*>(m, kHandles) + h * kHandleSize;
        if (At<int32_t>(handle, 0) != -1)
            continue;
        ++g_handles;
        const uint32_t id = kHandleBit | h;
        const Values v = Read(m, id);
        uint32_t info[9];
        Virtual<void>(m, 0x38, id, static_cast<void*>(info));
        float at[2];
        std::memcpy(at, info, sizeof at);
        Virtual<void>(m, 0x30, id, static_cast<const void*>(at));
        const float east[2] = {at[0] + 32.0f, at[1]};
        Virtual<void>(m, 0x30, id, static_cast<const void*>(east));
        Virtual<void>(m, 0x30, id, static_cast<const void*>(at));
        Virtual<void>(m, 0x34, id, static_cast<const void*>(info));
        // Whether the round trip gave the description back; the description
        // itself is left out (some decals are moved by frame time).
        uint32_t after[9];
        Virtual<void>(m, 0x38, id, static_cast<void*>(after));
        Mix(static_cast<uint32_t>(std::memcmp(info, after, sizeof info) == 0));
        if (static_cast<int32_t>(At<uint32_t>(At<char*>(m, kHandles) + h * kHandleSize, kTarget)) < 0)
            ++g_groups;
        else
            WriteBack(m, id, v);
    }
    if (first != 0xFFFFFFFF) {
        char* overlay = At<char*>(m, kOverlays) + first * kOverlaySize;
        float matrix[16];
        std::memcpy(matrix, overlay + kMatrix, sizeof matrix);
        const uint32_t flags = At<uint32_t>(overlay, kFlags);
        Virtual<void>(m, 0x54, first, 2.0f, 0.5f);
        overlay = At<char*>(m, kOverlays) + first * kOverlaySize;
        Mix(overlay + kMatrix, sizeof matrix);
        Mix(At<uint32_t>(overlay, kFlags));
        std::memcpy(overlay + kMatrix, matrix, sizeof matrix);
        At<uint32_t>(overlay, kFlags) = flags;
        if (!At<uint8_t>(m, kWaterRows)) {
            int32_t cells[4];
            std::memcpy(cells, overlay + kCells, sizeof cells);
            reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(addr::cSC4OverlayManager_Place)(m, first);
            reinterpret_cast<void(__thiscall*)(void*, uint32_t, const int32_t*)>(addr::cSC4OverlayManager_BucketRemove)(m, first,
                                                                                                                      cells);
            reinterpret_cast<void(__thiscall*)(void*, uint32_t, const int32_t*)>(addr::cSC4OverlayManager_BucketInsert)(m, first,
                                                                                                                      cells);
        }
    }
    for (uint32_t iid : {kIidOverlayManager, kIidUnknown}) {
        void* out = nullptr;
        const uint32_t found = Virtual<uint32_t>(m, 0x0, iid, &out) & 0xFF;
        Mix(found);
        Mix(static_cast<uint32_t>(out == m));
        if (found && out)
            Virtual<uint32_t>(out, 0x8);
    }
    Text text{g_textVtable, {}};
    Virtual<void>(m, 0x6C, static_cast<void*>(&text));
    // The counts are the last frame's: only the form is mixed in.
    int decals = 0, shadows = 0;
    Mix(static_cast<uint32_t>(std::sscanf(text.buffer, "%d decals, %d shadows", &decals, &shadows)));
    Message message{g_messageVtable};
    char* const target = static_cast<char*>(m) + 0x10;
    Mix(Virtual<uint32_t>(target, 0xC, static_cast<void*>(&message)) & 0xFF);
    Mix(static_cast<char*>(m) + 0x18, 9);
}

void DriveOverlays(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_OVERLAYS") || !game::IsBuild641())
        return;
    g_textVtable[0x14 / 4] = reinterpret_cast<void*>(&TextToChar);
    g_textVtable[0x6C / 4] = reinterpret_cast<void*>(&TextSprintf);
    g_messageVtable[0x10 / 4] = reinterpret_cast<void*>(&MessageType);
    void* const view = reinterpret_cast<void*(__cdecl*)()>(addr::SC4_TerrainPicker)();
    if (!view) {
        log::Write("autotest: overlays: no terrain view");
        return;
    }
    g_on = true;
    void* seen[4] = {};
    for (uint32_t kind = 0; kind < 4; ++kind) {
        void* const m = At<void*>(view, 0x6C + 4 * kind);
        bool again = !m;
        for (void* s : seen)
            again = again || s == m;
        seen[kind] = m;
        if (!again)
            Drive(m);
    }
    log::Write("autotest: overlays driven: %u managers, %u overlays, %u handles (%u with groups), hash %08X", g_managers, g_overlays,
               g_handles, g_groups, g_hash);
}

void Fingerprint(cISC4City*, std::string& line)
{
    if (!g_on)
        return;
    char text[64];
    std::snprintf(text, sizeof text, " | ovl %u %u %u %08X", g_overlays, g_handles, g_groups, g_hash);
    line += text;
}

const FingerprintPart kFingerprint{"overlays", 4, &Fingerprint};
const DayHook kDrive{"overlays", 4, &DriveOverlays};

} // namespace
} // namespace opensc4::autotest
