// The overlay manager's part of the autotest (src/core/autotest_hooks.h;
// docs/renderer.md, "The overlays", "Bookkeeping").
//
//   OPENSC4_TEST_OVERLAYS=<day>   token "ovl": on that day, drives every
//                                 function of renderer.overlays.manage1,
//                                 manage2 and manage3 on the city's four overlay
//                                 managers (the terrain view's +0x6C to
//                                 +0x78) through their vtables, or by address
//                                 for the ones the vtable does not reach, and
//                                 hashes what they give back.
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
// to the message target at +0x10. Then the adds (part 2): slots 3 to 11
// once each with the shadow texture (0xAA592B77) at the terrain's middle
// (slot 3 with a stand-in texture source, slot 11 twice, the second time with
// fewer than five texture stages), the ring decal's handle through part 1's
// getters, setters, MoveDecal and UpdateDecalInfo (a target that is a group),
// a decal moved east and back; on the first manager with buckets the range
// helpers by address on scratch records and ring decals until all three
// vectors have grown; then everything added removed again, last first, which
// leaves the overlays in use as they were (the vectors larger, their free
// lists longer). Then part 3 on a scratch manager made with operator new,
// constructed and set up with the first manager's view: its buckets' lists
// assigned (cut, copied, from themselves), the buckets' vector shrunk,
// lists inserted in place (with more after them than inserted, with fewer,
// from one of its own), at full capacity and at the end, a decal added,
// then Clear and the last Release (the deleting destructor). Works muted
// and in the original game. The token leaves out
// what frames change (alphas, decal descriptions, the draw counts in the
// stats text).
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
#include <initializer_list>
#include <string>
#include <vector>

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

// Part 2: the adds.

constexpr std::size_t kGroups = 0xC0, kGroupSize = 0x24, kTextures = 0x90;
constexpr uint32_t kShadowTexture = 0xAA592B77; // the shadow texture Init loads by ID
constexpr uint32_t kFshType = 0x7AB50E44;
constexpr uint32_t kNone = 0xFFFFFFFF;

uint32_t g_added = 0, g_grown = 0;

// A stand-in texture source (slot 3's argument): its references counted, a
// size of 64 by 32. Static: a removed overlay keeps its reference until the
// record is used again.
struct Source {
    void** vtable;
    int32_t references;
};

uint32_t __fastcall SourceQuery(Source*, void*, uint32_t, void**)
{
    return 0;
}

uint32_t __fastcall SourceAddRef(Source* self, void*)
{
    return static_cast<uint32_t>(++self->references);
}

uint32_t __fastcall SourceRelease(Source* self, void*)
{
    return static_cast<uint32_t>(--self->references);
}

void __fastcall SourceSize(Source*, void*, uint32_t* out)
{
    out[0] = 64;
    out[1] = 32;
}

void* g_sourceVtable[4] = {};
Source g_source{g_sourceVtable, 0};

// An added ID's records mixed in: the overlay (or a group's eight) and the
// handle's description.
void MixAdded(void* m, uint32_t id)
{
    Mix(id);
    if (id == kNone)
        return;
    const auto overlay = [&](uint32_t index) {
        const char* const r = At<char*>(m, kOverlays) + index * kOverlaySize;
        Mix(r + kFlags, 1);
        Mix(r + kCells, kOverlaySize - kCells - 8);
    };
    if (static_cast<int32_t>(id) >= 0) {
        overlay(id);
        return;
    }
    const char* const handle = At<char*>(m, kHandles) + (id & 0x7FFFFFFF) * kHandleSize;
    Mix(handle + 4, kHandleSize - 4);
    const uint32_t target = At<uint32_t>(const_cast<char*>(handle), kTarget);
    if (static_cast<int32_t>(target) >= 0) {
        overlay(target);
        return;
    }
    const uint32_t* ids = reinterpret_cast<const uint32_t*>(At<char*>(m, kGroups) + (target & 0x7FFFFFFF) * kGroupSize + 4);
    for (int i = 0; i < 8; ++i)
        overlay(ids[i]);
}

// The range helpers no caller in the game reaches with other arguments than
// one record at a vector's end (or Clear's empty assigns), called by address
// on scratch records from the manager's first ones; the copies of overlays
// destroyed again (0x00737E10).
void DriveRanges(void* m)
{
    using RangeFn = char*(__cdecl*)(const char*, const char*, char*);
    using FillFn = char*(__cdecl*)(char*, uint32_t, const char*);
    using DestroyFn = void(__cdecl*)(char*, char*, void*);
    const auto range = [](std::uintptr_t a) { return reinterpret_cast<RangeFn>(a); };
    const auto fill = [](std::uintptr_t a) { return reinterpret_cast<FillFn>(a); };
    const auto destroy = reinterpret_cast<DestroyFn>(addr::cSC4OverlayManager_DestroyOverlays);
    const auto mixOverlays = [](const char* r, int n) {
        for (int i = 0; i < n; ++i, r += kOverlaySize) {
            Mix(r, 8);
            Mix(r + kCells, 0xAC - kCells);
        }
    };
    char tag = 0;
    if (Used(m, kOverlays, kOverlaySize) >= 2) {
        const char* const first = At<char*>(m, kOverlays);
        alignas(8) char scratch[3 * kOverlaySize] = {};
        char* end = range(addr::cSC4OverlayManager_CopyOverlays)(first, first + 2 * kOverlaySize, scratch);
        Mix(static_cast<uint32_t>(end - scratch));
        mixOverlays(scratch, 2);
        destroy(scratch, scratch + 2 * kOverlaySize, &tag);
        end = fill(addr::cSC4OverlayManager_FillOverlays)(scratch, 3, first + kOverlaySize);
        Mix(static_cast<uint32_t>(end - scratch));
        mixOverlays(scratch, 3);
        end = range(addr::cSC4OverlayManager_AssignOverlays)(first, first + 2 * kOverlaySize, scratch);
        Mix(static_cast<uint32_t>(end - scratch));
        mixOverlays(scratch, 3);
        destroy(scratch, scratch + 3 * kOverlaySize, &tag);
    }
    if (Used(m, kHandles, kHandleSize) >= 2) {
        const char* const first = At<char*>(m, kHandles);
        alignas(8) char scratch[3 * kHandleSize] = {};
        char* end = fill(addr::cSC4OverlayManager_FillHandles)(scratch, 3, first);
        Mix(static_cast<uint32_t>(end - scratch));
        end = range(addr::cSC4OverlayManager_CopyHandles)(first, first + 2 * kHandleSize, scratch);
        Mix(static_cast<uint32_t>(end - scratch));
        end = range(addr::cSC4OverlayManager_AssignHandles)(first + kHandleSize, first + 2 * kHandleSize, scratch + 2 * kHandleSize);
        Mix(static_cast<uint32_t>(end - scratch));
        Mix(scratch, sizeof scratch);
    }
    if (Used(m, kGroups, kGroupSize) >= 1) {
        const uint32_t* const first = reinterpret_cast<const uint32_t*>(At<char*>(m, kGroups));
        uint32_t scratch[2 * kGroupSize / 4] = {};
        const uint32_t* const end = reinterpret_cast<uint32_t*(__cdecl*)(const uint32_t*, const uint32_t*, uint32_t*)>(
            addr::cSC4OverlayManager_CopyGroups)(first, first + kGroupSize / 4, scratch);
        Mix(static_cast<uint32_t>(end - scratch));
        Mix(scratch, sizeof scratch);
    }
}

// Each of the interface's slots 3 to 11 once with the shadow texture at the
// terrain's middle, the ring decal's handle through part 1's group paths,
// a decal moved, then (on the first manager) ring decals until all three
// vectors have grown; everything added removed again, last first.
void DriveAdds(void* m, bool grow)
{
    void* const textures = At<void*>(m, kTextures);
    void* texture = nullptr;
    Virtual<void>(textures, 0x14, kShadowTexture, static_cast<void*>(&texture));
    if (!texture)
        return;
    const float cell = At<float>(m, 0x64);
    const float at[2] = {static_cast<float>(At<int32_t>(m, 0x5C)) * cell * 0.5f, static_cast<float>(At<int32_t>(m, 0x60)) * cell * 0.5f};
    const uint32_t key[3] = {kFshType, Virtual<uint32_t>(textures, 0x2C), kShadowTexture};
    std::vector<uint32_t> ids;
    const auto add = [&](uint32_t id) {
        ids.push_back(id);
        MixAdded(m, id);
        return id;
    };
    add(Virtual<uint32_t>(m, 0x0C, static_cast<void*>(&g_source), static_cast<const void*>(at), 64.0f, 0.0f));
    add(Virtual<uint32_t>(m, 0x10, texture, static_cast<const void*>(at), 32.0f, 0.0f));
    const uint32_t moved = add(Virtual<uint32_t>(m, 0x14, static_cast<const void*>(key), static_cast<const void*>(at), 48.0f, 0.125f));
    add(Virtual<uint32_t>(m, 0x18, kShadowTexture, static_cast<const void*>(at), 16.0f, 0.3f));
    const uint32_t ring = add(Virtual<uint32_t>(m, 0x1C, kShadowTexture, static_cast<const void*>(at), 96.0f, 0.0f));
    const uint32_t height = add(Virtual<uint32_t>(m, 0x20, kShadowTexture));
    add(Virtual<uint32_t>(m, 0x24, static_cast<const void*>(key), static_cast<const void*>(at), 24.0f, 0.25f, 1u));
    add(Virtual<uint32_t>(m, 0x28, kShadowTexture, static_cast<const void*>(at), 24.0f, 0.0f, 0u));
    const float points2[8] = {at[0] - 8, at[1] - 8, at[0] + 8, at[1] - 8, at[0] + 8, at[1] + 8, at[0] - 8, at[1] + 8};
    const float points3[12] = {at[0] - 8, 30, at[1] - 8, at[0] + 8, 30, at[1] - 8, at[0] + 8, 30, at[1] + 8, at[0] - 8, 30, at[1] + 8};
    const float box[6] = {at[0] - 8, 0, at[1] - 8, at[0] + 8, 30, at[1] + 8};
    // cS3DTransform: flags, a rotation (rows), a translation, a scale.
    const float transform[14] = {0, 1, 0, 0, 0, 1, 0, 0, 0, 1, at[0], 0, at[1], 1};
    const auto shadow = [&] {
        return add(Virtual<uint32_t>(m, 0x2C, texture, 4, static_cast<const void*>(points3), static_cast<const void*>(points2),
                                     static_cast<const void*>(box), static_cast<const void*>(transform)));
    };
    shadow();
    // Again with fewer than five texture stages, for the cells from the
    // bounds (0x007354E0).
    int32_t& stages = At<int32_t>(At<char*>(Global<char*>(addr::SC4_App43CC4), 0x18), 0x1FC);
    const int32_t saved = stages;
    stages = 4;
    shadow();
    stages = saved;
    if (grow) {
        std::string text;
        for (uint32_t id : ids) {
            char word[12];
            std::snprintf(word, sizeof word, " %08X", id);
            text += word;
        }
        log::Write("autotest: overlays added (slots 3 to 11, two shadows):%s", text.c_str());
    }
    if (height != kNone)
        Virtual<void>(m, 0x54, height, 2.0f, 0.5f);
    if (moved != kNone) {
        const float east[2] = {at[0] + 32.0f, at[1]};
        Virtual<void>(m, 0x30, moved, static_cast<const void*>(east));
        Virtual<void>(m, 0x30, moved, static_cast<const void*>(at));
        MixAdded(m, moved);
    }
    if (ring != kNone) {
        // A handle whose target is a group: part 1's getters, setters, a
        // move, a new description with texture scales and the old one again.
        WriteBack(m, ring, Read(m, ring));
        uint32_t info[9];
        Virtual<void>(m, 0x38, ring, static_cast<void*>(info));
        Mix(info);
        const float east[2] = {at[0] + 48.0f, at[1] + 16.0f};
        Virtual<void>(m, 0x30, ring, static_cast<const void*>(east));
        MixAdded(m, ring);
        uint32_t scaled[9];
        std::memcpy(scaled, info, sizeof scaled);
        const float scale = 2.0f, turn = 0.2f;
        std::memcpy(&scaled[3], &turn, 4);
        std::memcpy(&scaled[5], &scale, 4);
        Virtual<void>(m, 0x34, ring, static_cast<const void*>(scaled));
        MixAdded(m, ring);
        Virtual<void>(m, 0x34, ring, static_cast<const void*>(info));
        MixAdded(m, ring);
    }
    if (grow)
        DriveRanges(m);
    if (grow) {
        char* begins[3] = {At<char*>(m, kOverlays), At<char*>(m, kHandles), At<char*>(m, kGroups)};
        for (int i = 0; i < 20000; ++i) {
            const bool grown = At<char*>(m, kOverlays) != begins[0] && At<char*>(m, kHandles) != begins[1]
                               && At<char*>(m, kGroups) != begins[2];
            if (grown)
                break;
            const uint32_t id = Virtual<uint32_t>(m, 0x1C, kShadowTexture, static_cast<const void*>(at), 32.0f + (i % 7), 0.0f);
            ids.push_back(id);
            Mix(id);
        }
        g_grown += (At<char*>(m, kOverlays) != begins[0]) + (At<char*>(m, kHandles) != begins[1]) + (At<char*>(m, kGroups) != begins[2]);
        for (std::size_t v : {kOverlays, kHandles, kGroups}) {
            const uint32_t capacity = static_cast<uint32_t>(At<char*>(m, v + 8) - At<char*>(m, v));
            Mix(capacity);
        }
    }
    for (auto it = ids.rbegin(); it != ids.rend(); ++it)
        if (*it != kNone) {
            ++g_added;
            Virtual<void>(m, 0x3C, *it);
        }
    reinterpret_cast<uint32_t(__thiscall*)(void*)>(addr::SC4TextureBinding_Release)(texture);
}

// Part 3: the set-up and the containers, on a scratch manager.

struct Node {
    Node* next;
    uint32_t value;
};

Node* MakeList(std::initializer_list<uint32_t> values)
{
    Node* head = nullptr;
    Node** link = &head;
    for (uint32_t value : values) {
        Node* const node = reinterpret_cast<Node*(__cdecl*)(std::size_t)>(addr::SC4_stl_allocate)(8);
        node->next = nullptr;
        node->value = value;
        *link = node;
        link = &node->next;
    }
    return head;
}

void FreeList(Node* node)
{
    while (node) {
        Node* const next = node->next;
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(node);
        node = next;
    }
}

// The buckets' vector: its size and capacity and every list's values.
void MixBuckets(char* vector)
{
    Node** const begin = At<Node**>(vector, 0);
    const uint32_t size = static_cast<uint32_t>(At<Node**>(vector, 4) - begin);
    Mix(size);
    Mix(static_cast<uint32_t>(At<Node**>(vector, 8) - begin));
    for (uint32_t i = 0; i < size; ++i)
        for (const Node* node = begin[i]; node; node = node->next) {
            Mix(i);
            Mix(node->value);
        }
}

uint32_t g_scratch = 0;

// A manager made (0x0073BC20) in memory from operator new and set up (Init
// 0x0073CB60) with the real one's view, as the terrain view makes its
// first; the fields that do not depend on memory mixed in. Its buckets'
// lists assigned from longer, shorter and equal lists and from themselves
// (the cut and the copy of nodes); the vector shrunk (the copy of an empty
// range, the lists destroyed), lists inserted with more after them than
// inserted (the copy backwards), with fewer, with a list of the vector's
// own, at full capacity (a new block, one list, the tail copied) and grown
// at the end. A decal added (part 2's slot 4), then Clear (the records
// destroyed) and the last Release (the deleting destructor 0x0073C980).
void DriveSetup(void* real)
{
    using ConstructFn = char*(__thiscall*)(void*);
    using InitFn = void(__thiscall*)(void*, void*, uint32_t, uint32_t);
    using SelfFn = void(__thiscall*)(void*);
    using AssignFn = Node**(__thiscall*)(Node**, Node* const*);
    using InsertFn = void(__thiscall*)(void*, Node**, uint32_t, Node* const*);
    using ResizeFn = void(__thiscall*)(void*, uint32_t, Node* const*);
    const auto assign = reinterpret_cast<AssignFn>(addr::cSC4OverlayManager_AssignList);
    const auto insert = reinterpret_cast<InsertFn>(addr::cSC4OverlayManager_InsertLists);
    const auto resize = reinterpret_cast<ResizeFn>(addr::cSC4OverlayManager_ResizeBuckets);
    char* const m = reinterpret_cast<char*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0xFC);
    if (!m)
        return;
    ++g_scratch;
    reinterpret_cast<ConstructFn>(addr::cSC4OverlayManager_Construct)(m);
    Mix(m, 0x14);
    // Held as the terrain view holds its managers (the message server's
    // Release in Clear must not destroy it).
    Virtual<uint32_t>(m, 0x4);
    Mix(m + 0x18, 0x9);
    Mix(m + 0x24, 0x5C - 0x24);
    Mix(m + 0x6C, 0xC);
    Mix(m + 0x88, 0xC);
    Mix(m + 0x98, 0xD8 - 0x98);
    Mix(m + 0xF0, 0xC);
    reinterpret_cast<InitFn>(addr::cSC4OverlayManager_Init)(m, At<void*>(real, 0x8C), 0, 0);
    Mix(m + 0x14, 0x3);
    Mix(m + 0x5C, 0x7C - 0x5C);
    Mix(static_cast<uint32_t>(At<void*>(m, 0x88) == At<void*>(real, 0x88)));
    Mix(static_cast<uint32_t>(At<void*>(m, 0x90) == At<void*>(real, 0x90)));
    Mix(static_cast<uint32_t>(At<void*>(m, 0xD4) == At<void*>(real, 0xD4)));
    Mix(At<char*>(m, 0x24), 5 * 8);
    Mix(static_cast<uint32_t>(At<char*>(m, 0x28) - At<char*>(m, 0x24)));
    Mix(m + 0x38, 0x5C - 0x38);
    Mix(At<uint16_t>(m, 0xE0));
    Mix(m + 0xE4, 0xC);
    char* const vector = m + 0xF0;
    MixBuckets(vector);
    Node* const three = MakeList({7, 8, 9});
    Node* const one = MakeList({5});
    Node* const empty = nullptr;
    const auto bucket = [&](uint32_t i) { return At<Node**>(vector, 0) + i; };
    const uint32_t size = static_cast<uint32_t>(At<Node**>(vector, 4) - At<Node**>(vector, 0));
    if (size >= 16) {
        assign(bucket(0), &three);
        assign(bucket(1), &one);
        assign(bucket(0), &one);
        assign(bucket(2), &three);
        assign(bucket(2), bucket(2));
        assign(bucket(1), &three);
        MixBuckets(vector);
        resize(vector, size - 10, &empty);
        insert(vector, bucket(1), 2, &three);
        insert(vector, At<Node**>(vector, 4) - 1, 3, &one);
        MixBuckets(vector);
        insert(vector, bucket(3), 5, bucket(0));
        insert(vector, bucket(2), 1, &three);
        const uint32_t now = static_cast<uint32_t>(At<Node**>(vector, 4) - At<Node**>(vector, 0));
        resize(vector, now + 4, &one);
        MixBuckets(vector);
        resize(vector, size, &empty);
        MixBuckets(vector);
    }
    FreeList(three);
    FreeList(one);
    void* const textures = At<void*>(m, 0x90);
    void* texture = nullptr;
    if (textures)
        Virtual<void>(textures, 0x14, kShadowTexture, static_cast<void*>(&texture));
    if (texture) {
        const float cell = At<float>(m, 0x64);
        const float at[2] = {static_cast<float>(At<int32_t>(m, 0x5C)) * cell * 0.5f, static_cast<float>(At<int32_t>(m, 0x60)) * cell * 0.5f};
        Mix(Virtual<uint32_t>(m, 0x10, texture, static_cast<const void*>(at), 32.0f, 0.0f));
        reinterpret_cast<uint32_t(__thiscall*)(void*)>(addr::SC4TextureBinding_Release)(texture);
    }
    Mix(static_cast<uint32_t>((At<char*>(m, 0x9C) - At<char*>(m, 0x98)) / kOverlaySize));
    reinterpret_cast<SelfFn>(addr::cSC4OverlayManager_Clear)(m);
    Mix(m + 0x14, 1);
    Mix(static_cast<uint32_t>(At<char*>(m, 0x9C) - At<char*>(m, 0x98)));
    Mix(static_cast<uint32_t>(At<char*>(m, 0xB0) - At<char*>(m, 0xAC)));
    Mix(m + 0x88, 0xC);
    Mix(m + 0xA4, 8);
    Mix(m + 0xB8, 8);
    // The last reference: the deleting destructor through the adjustor
    // thunk 0x0073BDC0.
    Virtual<uint32_t>(m, 0x8);
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
    g_sourceVtable[0] = reinterpret_cast<void*>(&SourceQuery);
    g_sourceVtable[1] = reinterpret_cast<void*>(&SourceAddRef);
    g_sourceVtable[2] = reinterpret_cast<void*>(&SourceRelease);
    g_sourceVtable[3] = reinterpret_cast<void*>(&SourceSize);
    void* seen[4] = {};
    bool grown = false;
    for (uint32_t kind = 0; kind < 4; ++kind) {
        void* const m = At<void*>(view, 0x6C + 4 * kind);
        bool again = !m;
        for (void* s : seen)
            again = again || s == m;
        seen[kind] = m;
        if (again)
            continue;
        Drive(m);
        const bool grow = !grown && !At<uint8_t>(m, kWaterRows);
        DriveAdds(m, grow);
        if (grow)
            DriveSetup(m);
        grown = grown || grow;
    }
    log::Write("autotest: overlays driven: %u managers, %u overlays, %u handles (%u with groups), %u added (%u vectors grown, "
               "source references %d), %u scratch managers, hash %08X",
               g_managers, g_overlays, g_handles, g_groups, g_added, g_grown, g_source.references, g_scratch, g_hash);
}

void Fingerprint(cISC4City*, std::string& line)
{
    if (!g_on)
        return;
    char text[80];
    std::snprintf(text, sizeof text, " | ovl %u %u %u %u %u %08X", g_overlays, g_handles, g_groups, g_added, g_grown, g_hash);
    line += text;
}

const FingerprintPart kFingerprint{"overlays", 4, &Fingerprint};
const DayHook kDrive{"overlays", 4, &DriveOverlays};

} // namespace
} // namespace opensc4::autotest
