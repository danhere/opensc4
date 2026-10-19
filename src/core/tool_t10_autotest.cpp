// OPENSC4_TEST_T10=<day>: the zoning tool's and the cheat tool's pointer
// drawing and the model holder's Place (ui.tools.zone2, docs/tools.md). As
// T9's test it calls only the game's addresses and the classes' vtable
// slots, never our functions. The terrain, the models, the view's pick, the
// overlay, the display service, the city, the network pieces, the model
// service, the prop manager and its prop, the lot configurations and the
// prop types are stand-ins that record what they are told (only which
// stand-in was meant goes into the hash, never a pointer). Every stand-in
// slot the game does not ask is counted: `unexpected slots` must be 0.
//
// OPENSC4_TEST_T10_PARTS (decimal mask, default 31):
//   1  Place on an empty holder and over cell sizes 16, 1, 3, 0.5, 0.1 and
//      1.7 with points on, between and far from the cells (a NaN among them)
//   2  the zoning tool's OnMouseMove on a stand-in tool: a failed pick, the
//      overlay busy or not, with and without the outline, shown or hidden,
//      a drag without the button (CancelDrag) and with it on the same cell
//   4  the same on a zoning tool the view made, with the view's pick
//      swapped for a stand-in for the call: a drag onto another cell (the
//      game's UpdateHover and UpdatePreview), then the drag cancelled
//   8  FindFootprint over the six network types and others, type masks and
//      the four sides' connection bytes
//  16  the cheat tool's OnMouseMove on a stand-in tool: footprints of each
//      kind, the models replaced, props made, moved and taken away, and the
//      ways making one stops short
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <GZServPtrs.h>
#include <cIGZWin.h>
#include <cISC4App.h>
#include <cISC4City.h>
#include <cISC4View3DWin.h>
#include <cRZAutoRefCount.h>

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
};

char g_token[32];
Fnv* g_log = nullptr;
int g_unexpected = 0;

template <typename T>
void Note(const T& value)
{
    if (g_log)
        g_log->Add(value);
}

template <typename R, typename... Args>
R Game(std::uintptr_t function, void* self, Args... args)
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

float Float(uint32_t bits)
{
    float value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

// --- Stand-ins --------------------------------------------------------------------------

constexpr std::size_t kSlots = 0x80;

struct Fake {
    void** vtable;
    uint8_t raw[0x300];
    void* slots[kSlots];
};

// Where in a stand-in its own state is kept (past every field the game's
// code reads of a tool).
constexpr std::size_t kState = 0x100;
constexpr std::size_t kRole = 0x2FE;
constexpr std::size_t kCount = 0x2FF;

template <typename T>
T& Field(Fake* fake, std::size_t offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<char*>(fake) + offset);
}

template <typename T>
T& Field(Fake& fake, std::size_t offset)
{
    return Field<T>(&fake, offset);
}

uint8_t Role(Fake* fake)
{
    return fake ? fake->raw[kRole - 4] : uint8_t{0};
}

uint32_t __fastcall Unexpected(Fake* self, void*)
{
    ++g_unexpected;
    Note(uint8_t{0xEE});
    Note(Role(self));
    return 0;
}

void Make(Fake& fake, uint8_t role)
{
    std::memset(&fake, 0, sizeof fake);
    fake.vtable = fake.slots;
    for (void*& slot : fake.slots)
        slot = reinterpret_cast<void*>(&Unexpected);
    fake.raw[kRole - 4] = role;
}

template <typename F>
void Slot(Fake& fake, std::size_t offset, F* function)
{
    fake.slots[offset / 4] = reinterpret_cast<void*>(function);
}

// The stand-ins; the role byte is what goes into the hash for a pointer.
Fake g_terrain, g_modelA, g_modelB, g_view, g_overlay, g_display, g_tool;
Fake g_city, g_networks, g_piece, g_modelService, g_footA, g_footB, g_props, g_prop, g_propObject;
Fake g_holder, g_property, g_variant, g_configs, g_config, g_types, g_cheat;

uint8_t Which(const void* pointer)
{
    if (!pointer)
        return 0;
    for (Fake* fake : {&g_terrain, &g_modelA, &g_modelB, &g_view, &g_overlay, &g_display, &g_tool, &g_city,
                       &g_networks, &g_piece, &g_modelService, &g_footA, &g_footB, &g_props, &g_prop,
                       &g_propObject, &g_holder, &g_property, &g_variant, &g_configs, &g_config, &g_types,
                       &g_cheat})
        if (fake == pointer)
            return Role(fake);
    return 0xFF;
}

uint32_t __fastcall AddRef(Fake* self, void*)
{
    Note(uint8_t{0x01});
    Note(Role(self));
    return ++self->raw[kCount - 4];
}

uint32_t __fastcall Release(Fake* self, void*)
{
    Note(uint8_t{0x02});
    Note(Role(self));
    return --self->raw[kCount - 4];
}

// The terrain: slot 6 the cell size (kState), slot 13 the height (kState + 4).
float __fastcall TerrainCell(Fake* self, void*)
{
    Note(uint8_t{0x10});
    return Field<float>(self, kState);
}

float __fastcall TerrainHeight(Fake* self, void*, float x, float z)
{
    Note(uint8_t{0x11});
    Note(Bits(x));
    Note(Bits(z));
    return Field<float>(self, kState + 4);
}

// A model: shown at kState; slot 15 fills a transform with marks of its own.
uint8_t __fastcall ModelShown(Fake* self, void*)
{
    Note(uint8_t{0x20});
    Note(Role(self));
    return Field<uint8_t>(self, kState);
}

void __fastcall ModelShow(Fake* self, void*, uint32_t argument)
{
    Note(uint8_t{0x21});
    Note(Role(self));
    Note(argument);
    Field<uint8_t>(self, kState) = 1;
}

void __fastcall ModelHide(Fake* self, void*, uint32_t argument)
{
    Note(uint8_t{0x22});
    Note(Role(self));
    Note(argument);
    Field<uint8_t>(self, kState) = 0;
}

// A transform's count, flags, rotation, place and scale (the two bytes after
// the flags are the game's stack and left out).
void NoteTransform(const uint8_t* t)
{
    Note(t[0]);
    Note(t[1]);
    for (std::size_t i = 4; i < 0x38; i += 4) {
        uint32_t word;
        std::memcpy(&word, t + i, sizeof word);
        Note(word);
    }
}

void __fastcall ModelSet(Fake* self, void*, const uint8_t* t)
{
    Note(uint8_t{0x23});
    Note(Role(self));
    NoteTransform(t);
}

void __fastcall ModelGet(Fake* self, void*, uint8_t* t)
{
    Note(uint8_t{0x24});
    Note(Role(self));
    NoteTransform(t);
    t[0] = 4;
    t[1] = 0x30;
    for (std::size_t i = 4; i < 0x38; i += 4) {
        const float value = 0.25f * static_cast<float>(i) - 3.0f;
        std::memcpy(t + i, &value, sizeof value);
    }
}

// The view's pick: answers kState, writes the three floats at kState + 4.
uint8_t __fastcall ViewPick(Fake* self, void*, int32_t x, int32_t z, float* point, uint32_t flag)
{
    Note(uint8_t{0x30});
    Note(x);
    Note(z);
    Note(flag);
    for (int i = 0; i < 3; ++i)
        Note(Bits(point[i]));
    if (!Field<uint8_t>(self, kState))
        return 0;
    std::memcpy(point, &Field<float>(self, kState + 4), 3 * sizeof(float));
    return 1;
}

uint8_t __fastcall OverlayBusy(Fake* self, void*)
{
    Note(uint8_t{0x31});
    return Field<uint8_t>(self, kState);
}

void __fastcall DisplayModifiers(Fake*, void*, uint32_t first, uint32_t second, uint32_t third)
{
    Note(uint8_t{0x32});
    Note(first);
    Note(second);
    Note(third);
}

void __fastcall ToolCancel(Fake*, void*)
{
    Note(uint8_t{0x33});
}

// The city: slot 95 a point's cell, slot 97 a cell's centre.
void __fastcall CityCell(Fake*, void*, float x, float z, int32_t* cellX, int32_t* cellZ)
{
    Note(uint8_t{0x40});
    Note(Bits(x));
    Note(Bits(z));
    *cellX = static_cast<int32_t>(x) / 16 + 1;
    *cellZ = static_cast<int32_t>(z) / 16 + 2;
}

void __fastcall CityCentre(Fake*, void*, int32_t cellX, int32_t cellZ, float* x, float* z)
{
    Note(uint8_t{0x41});
    Note(cellX);
    Note(cellZ);
    *x = static_cast<float>(cellX) * 16.0f + 8.25f;
    *z = static_cast<float>(cellZ) * 16.0f + 7.75f;
}

// The networks' service: the piece at a cell (kState: the piece or null).
void* __fastcall NetworkPiece(Fake* self, void*, int32_t x, int32_t z, uint32_t kinds, uint32_t one)
{
    Note(uint8_t{0x42});
    Note(x);
    Note(z);
    Note(kinds);
    Note(one);
    return Field<void*>(self, kState);
}

// A piece: the types it has (a mask at kState), its type mask (kState + 4)
// and its sides (kState + 8).
uint8_t __fastcall PieceHas(Fake* self, void*, int32_t type)
{
    Note(uint8_t{0x43});
    Note(type);
    return type >= 0 && type < 32 && ((Field<uint32_t>(self, kState) >> type) & 1) != 0;
}

uint32_t __fastcall PieceMask(Fake* self, void*)
{
    Note(uint8_t{0x44});
    return Field<uint32_t>(self, kState + 4);
}

uint32_t __fastcall PieceSides(Fake* self, void*, int32_t type)
{
    Note(uint8_t{0x45});
    Note(type);
    return Field<uint32_t>(self, kState + 8);
}

// The model service: a model by name (kState: give none when set).
void __fastcall ModelMake(Fake* self, void*, const char* name, void** holder)
{
    Note(uint8_t{0x50});
    Note(static_cast<uint32_t>(std::strlen(name)));
    if (g_log)
        g_log->Bytes(name, std::strlen(name));
    Note(Which(*holder));
    if (Field<uint8_t>(self, kState))
        return;
    Fake* const model = std::strcmp(name, "building_footprint_ok") == 0 ? &g_footA : &g_footB;
    ++model->raw[kCount - 4];
    *holder = model;
}

// The prop manager: slot 29 takes a prop away, slot 16 makes one (kState:
// refuse when set).
uint8_t __fastcall PropsRemove(Fake*, void*, void* object)
{
    Note(uint8_t{0x51});
    Note(Which(object));
    return 1;
}

uint8_t __fastcall PropsMake(Fake* self, void*, const float* type, void** out)
{
    Note(uint8_t{0x52});
    for (int i = 0; i < 3; ++i)
        Note(Bits(type[i]));
    Note(Which(*out));
    if (Field<uint8_t>(self, kState))
        return 0;
    ++g_prop.raw[kCount - 4];
    *out = &g_prop;
    return 1;
}

void* __fastcall PropObject(Fake* self, void*)
{
    Note(uint8_t{0x53});
    Note(Role(self));
    return &g_propObject;
}

void __fastcall PropTurn(Fake*, void*, int32_t orientation)
{
    Note(uint8_t{0x54});
    Note(orientation);
}

void __fastcall ObjectFlags(Fake*, void*, uint32_t flags)
{
    Note(uint8_t{0x55});
    Note(flags);
}

void* __fastcall ObjectHolder(Fake*, void*)
{
    Note(uint8_t{0x56});
    return &g_holder;
}

void __fastcall ObjectPlace(Fake*, void*, const float* point)
{
    Note(uint8_t{0x57});
    for (int i = 0; i < 3; ++i)
        Note(Bits(point[i]));
}

void* __fastcall HolderProperty(Fake*, void*, uint32_t id)
{
    Note(uint8_t{0x58});
    Note(id);
    return &g_property;
}

void* __fastcall PropertyData(Fake*, void*)
{
    Note(uint8_t{0x59});
    return &g_variant;
}

void __fastcall VariantSet(Fake*, void*, uint32_t value)
{
    Note(uint8_t{0x5A});
    Note(static_cast<uint8_t>(value));
}

// The lot configurations: slot 8 by ID (kState: none when set); a
// configuration's size (kState: leave the outputs when set; kState + 4 and
// + 8 the size) and its info (the block at kState + 0x10).
void* __fastcall ConfigFind(Fake* self, void*, uint32_t id)
{
    Note(uint8_t{0x60});
    Note(id);
    return Field<uint8_t>(self, kState) ? nullptr : &g_config;
}

void __fastcall ConfigSize(Fake* self, void*, uint32_t* width, uint32_t* depth)
{
    Note(uint8_t{0x61});
    Note(*width);
    Note(*depth);
    if (Field<uint8_t>(self, kState))
        return;
    *width = Field<uint32_t>(self, kState + 4);
    *depth = Field<uint32_t>(self, kState + 8);
}

const char* __fastcall ConfigInfo(Fake* self, void*)
{
    Note(uint8_t{0x62});
    return &Field<char>(self, kState + 0x10);
}

// The prop types: slot 14 (kState: refuse when set).
uint8_t __fastcall PropTypeOf(Fake* self, void*, uint32_t type, float* out)
{
    Note(uint8_t{0x63});
    Note(type);
    for (int i = 0; i < 3; ++i)
        Note(Bits(out[i]));
    if (Field<uint8_t>(self, kState))
        return 0;
    out[0] = Float(0x3F800001u);
    out[1] = -2.5f;
    out[2] = static_cast<float>(type & 0xFF);
    return 1;
}

uint32_t g_configPropType = 0x6534284Au;

void SetUpFakes()
{
    static bool done = false;
    if (done)
        return;
    done = true;
    uint8_t role = 1;
    for (Fake* fake : {&g_terrain, &g_modelA, &g_modelB, &g_view, &g_overlay, &g_display, &g_tool, &g_city,
                       &g_networks, &g_piece, &g_modelService, &g_footA, &g_footB, &g_props, &g_prop,
                       &g_propObject, &g_holder, &g_property, &g_variant, &g_configs, &g_config, &g_types,
                       &g_cheat})
        Make(*fake, role++);
    Slot(g_terrain, 0x18, &TerrainCell);
    Slot(g_terrain, 0x34, &TerrainHeight);
    for (Fake* model : {&g_modelA, &g_modelB, &g_footA, &g_footB}) {
        Slot(*model, 0x4, &AddRef);
        Slot(*model, 0x8, &Release);
        Slot(*model, 0xC, &ModelShow);
        Slot(*model, 0x10, &ModelHide);
        Slot(*model, 0x14, &ModelShown);
        Slot(*model, 0x20, &ModelSet);
        Slot(*model, 0x3C, &ModelGet);
    }
    Slot(g_view, 0x90, &ViewPick);
    Slot(g_overlay, 0x50, &OverlayBusy);
    Slot(g_display, 0x1C, &DisplayModifiers);
    Slot(g_tool, 0x6C, &ToolCancel);
    Slot(g_city, 0x17C, &CityCell);
    Slot(g_city, 0x184, &CityCentre);
    Slot(g_networks, 0x20, &NetworkPiece);
    Slot(g_piece, 0x30, &PieceMask);
    Slot(g_piece, 0x58, &PieceHas);
    Slot(g_piece, 0x98, &PieceSides);
    Slot(g_modelService, 0x1C, &ModelMake);
    Slot(g_props, 0x40, &PropsMake);
    Slot(g_props, 0x74, &PropsRemove);
    Slot(g_prop, 0x4, &AddRef);
    Slot(g_prop, 0x8, &Release);
    Slot(g_prop, 0xC, &PropObject);
    Slot(g_prop, 0x28, &PropTurn);
    Slot(g_propObject, 0x18, &ObjectHolder);
    Slot(g_propObject, 0x24, &ObjectPlace);
    Slot(g_propObject, 0x74, &ObjectFlags);
    Slot(g_holder, 0x24, &HolderProperty);
    Slot(g_property, 0x14, &PropertyData);
    Slot(g_variant, 0x2C, &VariantSet);
    Slot(g_configs, 0x20, &ConfigFind);
    Slot(g_config, 0x1C, &ConfigSize);
    Slot(g_config, 0x60, &ConfigInfo);
    Slot(g_types, 0x38, &PropTypeOf);
    // The configuration's info block: a corner at +4..+0xC and at +0x28 a
    // pointer to the prop type.
    char* const info = &Field<char>(g_config, kState + 0x10);
    const float corner[3] = {100.75f, 3.5f, Float(0x3E99999Au)};
    std::memcpy(info + 4, corner, sizeof corner);
    uint32_t* const typeAt = &g_configPropType;
    std::memcpy(info + 0x28, &typeAt, sizeof typeAt);
}

// The game's globals the parts swap for stand-ins, put back by the destructor.
struct Swap {
    std::uintptr_t address;
    void* saved;
    Swap(std::uintptr_t at, void* with) : address(at), saved(Global<void*>(at)) { Global<void*>(at) = with; }
    ~Swap() { Global<void*>(address) = saved; }
    Swap(const Swap&) = delete;
    Swap& operator=(const Swap&) = delete;
};

// --- 1. Place ---------------------------------------------------------------------------

void PartPlace(Fnv& out)
{
    Fnv f;
    g_log = &f;
    Swap terrain(addr::SC4_Terrain, &g_terrain);
    void* holder = nullptr;
    const float origin[3] = {1.0f, 2.0f, 3.0f};
    Game<void>(addr::SC4ViewModel_Place, &holder, origin);
    holder = &g_modelA;
    // 0.1 and 1.7 with the last two points give other bits when the chain
    // is rounded to float at each step instead of kept in double.
    static const float kCells[] = {16.0f, 1.0f, 3.0f, 0.5f, 0.1f, 1.7f};
    static const uint32_t kPoints[][3] = {
        {0x00000000u, 0x00000000u, 0x00000000u}, // 0, 0, 0
        {0x42C8999Au, 0x40A00000u, 0xC0FCCCCDu}, // 100.3, 5, -7.9
        {0xBDCCCCCDu, 0x40000000u, 0x457FFFD7u}, // -0.1, 2, 4095.99
        {0x4E6E6B28u, 0xC2280000u, 0x3F7FFFFFu}, // 1e9, -42, 0.99999994
        {0x7FC00000u, 0x3F800000u, 0xC1800000u}, // NaN, 1, -16
        {0x41AAAAABu, 0x00000001u, 0x40400000u}, // 21.333334, the smallest denormal, 3
        {0xC57DD677u, 0x3F800000u, 0x458B25A8u}, // -4061.404, 1, 4452.707
        {0x4551DA6Bu, 0x00000000u, 0xC529860Bu}, // 3357.6511, 0, -2712.3777
    };
    for (const float cell : kCells) {
        Field<float>(g_terrain, kState) = cell;
        for (const auto& bits : kPoints) {
            float point[3];
            for (int i = 0; i < 3; ++i)
                point[i] = Float(bits[i]);
            Game<void>(addr::SC4ViewModel_Place, &holder, static_cast<const float*>(point));
        }
    }
    f.Add(g_unexpected);
    g_log = nullptr;
    log::Write("autotest: zone2: place %08X", f.h);
    out.Add(f.h);
}

// --- 2. The zoning tool on a stand-in tool -----------------------------------------------

namespace zone {
constexpr std::size_t kView = 0x1C, kOverlay = 0x2C, kDragging = 0x40, kStart = 0x44, kCur = 0x4C;
constexpr std::size_t kCell = 0x6C, kPointer = 0x90, kOutline = 0x94, kRect = 0x5C, kLotCount = 0x88;
constexpr std::size_t kTarget = 0x28, kHoverTime = 0x54;
constexpr uint32_t kTimerInterval = 0xC0;
} // namespace zone

struct ZoneCase {
    uint8_t picks;
    float point[3];
    uint8_t busy;
    uint8_t dragging;
    uint8_t outline;      // 0 none, 1 hidden, 2 shown
    uint8_t pointerShown;
    uint32_t modifiers;
    int32_t current[2];
    float cell = 16.0f;
};

void PartZone(Fnv& out)
{
    Fnv f;
    g_log = &f;
    Swap terrain(addr::SC4_Terrain, &g_terrain);
    Swap display(addr::SC4_Service43DB0, &g_display);
    Field<float>(g_terrain, kState) = 16.0f;
    static const ZoneCase kCases[] = {
        {0, {0.0f, 0.0f, 0.0f}, 0, 0, 2, 1, 0x00, {0, 0}},
        {0, {0.0f, 0.0f, 0.0f}, 0, 1, 2, 0, 0x10, {0, 0}},
        {1, {100.3f, 12.5f, 64.0f}, 0, 0, 1, 0, 0x00, {0, 0}},
        {1, {100.3f, 12.5f, 64.0f}, 0, 0, 2, 1, 0x01, {0, 0}},
        {1, {-0.5f, 1.0f, 2047.9f}, 1, 0, 1, 1, 0x02, {0, 0}},
        {1, {33.0f, 1.0f, 17.0f}, 1, 0, 0, 0, 0x04, {0, 0}},
        {1, {33.0f, 1.0f, 17.0f}, 0, 1, 2, 1, 0x00, {2, 1}},
        {1, {33.0f, 1.0f, 17.0f}, 0, 1, 1, 0, 0x08, {2, 1}},
        {1, {33.0f, 1.0f, 17.0f}, 0, 1, 2, 0, 0x10, {2, 1}},
        {1, {47.99f, -3.0f, 31.5f}, 1, 1, 0, 1, 0x17, {2, 1}},
        {1, {0.0f, 0.0f, 0.0f}, 0, 1, 1, 0, 0xFF, {0, 0}},
        // A cell size of 1.7 (not dragging): the outline's centre rounds
        // differently when the chain is not kept in double.
        {1, {-4061.404f, 2.0f, 4452.707f}, 0, 0, 1, 0, 0x00, {0, 0}, 1.7f},
        {1, {3357.6511f, 2.0f, -2712.3777f}, 1, 0, 2, 1, 0x00, {0, 0}, 1.7f},
    };
    for (const ZoneCase& c : kCases) {
        Make(g_tool, Role(&g_tool));
        Slot(g_tool, 0x6C, &ToolCancel);
        Field<void*>(g_tool, zone::kView) = &g_view;
        Field<void*>(g_tool, zone::kOverlay) = &g_overlay;
        Field<uint8_t>(g_tool, zone::kDragging) = c.dragging;
        Field<int32_t>(g_tool, zone::kCur) = c.current[0];
        Field<int32_t>(g_tool, zone::kCur + 4) = c.current[1];
        Field<float>(g_tool, zone::kCell) = c.cell;
        Field<void*>(g_tool, zone::kPointer) = &g_modelA;
        Field<void*>(g_tool, zone::kOutline) = c.outline ? &g_modelB : nullptr;
        Field<uint8_t>(g_modelA, kState) = c.pointerShown;
        Field<uint8_t>(g_modelB, kState) = c.outline == 2;
        Field<uint8_t>(g_overlay, kState) = c.busy;
        Field<uint8_t>(g_view, kState) = c.picks;
        std::memcpy(&Field<float>(g_view, kState + 4), c.point, sizeof c.point);
        const uint8_t result = Game<uint8_t>(addr::cSC4ViewInputControlZone_OnMouseMove, &g_tool, int32_t{311},
                                             int32_t{-27}, c.modifiers);
        f.Add(result);
        f.Add(Field<int32_t>(g_tool, zone::kCur));
        f.Add(Field<int32_t>(g_tool, zone::kCur + 4));
        f.Add(Field<uint8_t>(g_tool, zone::kDragging));
        f.Add(Field<uint8_t>(g_modelA, kState));
        f.Add(Field<uint8_t>(g_modelB, kState));
    }
    f.Add(g_unexpected);
    g_log = nullptr;
    log::Write("autotest: zone2: zone %08X", f.h);
    out.Add(f.h);
}

// --- 3. The zoning tool the view made --------------------------------------------------

char* CityView()
{
    constexpr uint32_t kWinSC4App = 0x6104489A;
    constexpr uint32_t kWinView3D = 0x9A47B417;
    cISC4AppPtr app;
    cIGZWin* const main = app ? app->GetMainWindow() : nullptr;
    cIGZWin* const parent = main ? main->GetChildWindowFromID(kWinSC4App) : nullptr;
    cRZAutoRefCount<cISC4View3DWin> view;
    if (parent)
        parent->GetChildAs(kWinView3D, kGZIID_cISC4View3DWin, view.AsPPVoid());
    return reinterpret_cast<char*>(static_cast<cISC4View3DWin*>(view));
}

// The pick the real view answers while its vtable is swapped: the point at
// g_realPick, true.
float g_realPick[3];

uint8_t __fastcall RealPick(void*, void*, int32_t x, int32_t z, float* point, uint32_t flag)
{
    Note(uint8_t{0x70});
    Note(x);
    Note(z);
    Note(flag);
    std::memcpy(point, g_realPick, sizeof g_realPick);
    return 1;
}

// One call of the tool's slot 20 with the view's slot 36 answered by
// RealPick: a copy of the view's vtable stands in for the call.
uint8_t MoveWithPick(char* tool, void* view, const float* point, uint32_t modifiers)
{
    constexpr std::size_t kViewSlots = 256;
    static void* copy[kViewSlots];
    void** const original = *static_cast<void***>(view);
    std::memcpy(copy, original, sizeof copy);
    copy[0x90 / 4] = reinterpret_cast<void*>(&RealPick);
    std::memcpy(g_realPick, point, sizeof g_realPick);
    *static_cast<void***>(view) = copy;
    const uint8_t result = Virtual<uint8_t>(tool, 0x50, int32_t{400}, int32_t{300}, modifiers);
    *static_cast<void***>(view) = original;
    return result;
}

void PartRealZone(Fnv& out)
{
    char* const view = CityView();
    void* const city = Global<void*>(addr::SC4_City);
    if (!view || !city) {
        log::Write("autotest: zone2: real: no city view");
        return;
    }
    if (Virtual<void*>(view, 0x60)) {
        log::Write("autotest: zone2: real: the view holds a tool");
        return;
    }
    const int32_t sizeX = Virtual<int32_t>(city, 0x174), sizeZ = Virtual<int32_t>(city, 0x178);
    if (sizeX < 64 || sizeZ < 64 || sizeX > 4096 || sizeZ > 4096) {
        log::Write("autotest: zone2: real: city size %d x %d", sizeX, sizeZ);
        return;
    }
    Fnv f;
    g_log = &f;
    Game<void>(addr::SC4UI_F7E6DA0, view, uint32_t{1});
    char* const tool = Virtual<char*>(view, 0x60);
    if (!tool) {
        g_log = nullptr;
        log::Write("autotest: zone2: real: the view did not take the tool");
        return;
    }
    void* const toolView = At<void*>(tool, zone::kView);
    const float cell = At<float>(tool, zone::kCell);
    f.Add(Bits(cell));
    const int32_t startX = sizeX / 2 - 3, startZ = sizeZ / 2 - 2;
    // A drag begun the way the zone tool's test begins one.
    Game<uint8_t>(addr::SC4ViewInputControl_BeginCapture, tool);
    At<uint8_t>(tool, zone::kDragging) = 1;
    At<int32_t>(tool, zone::kStart) = startX;
    At<int32_t>(tool, zone::kStart + 4) = startZ;
    At<int32_t>(tool, zone::kCur) = startX;
    At<int32_t>(tool, zone::kCur + 4) = startZ;
    void* const timers = Cdecl<void*>(addr::SC4_GetService86AD10EC);
    Virtual<void>(timers, 0x14, static_cast<void*>(tool + zone::kTarget), zone::kTimerInterval);
    At<uint32_t>(tool, zone::kHoverTime) = 0;
    const auto fields = [&](uint8_t result) {
        f.Add(result);
        f.Add(At<uint8_t>(tool, zone::kDragging));
        for (std::size_t i = 0; i < 4; ++i)
            f.Add(At<int32_t>(tool, zone::kStart + 4 * i));
        for (std::size_t i = 0; i < 4; ++i)
            f.Add(At<int32_t>(tool, zone::kRect + 4 * i));
        f.Add(At<uint32_t>(tool, zone::kLotCount));
    };
    // The same cell, then three cells on (the drag grows to 4 x 3), then
    // back to the start, then without the button.
    const float same[3] = {(static_cast<float>(startX) + 0.25f) * cell, 10.0f, (static_cast<float>(startZ) + 0.5f) * cell};
    const float further[3] = {(static_cast<float>(startX + 3) + 0.5f) * cell, 11.0f,
                              (static_cast<float>(startZ + 2) + 0.75f) * cell};
    if (toolView) {
        fields(MoveWithPick(tool, toolView, same, 0x10));
        fields(MoveWithPick(tool, toolView, further, 0x10));
        fields(MoveWithPick(tool, toolView, same, 0x10));
        fields(MoveWithPick(tool, toolView, further, 0x00));
        fields(MoveWithPick(tool, toolView, further, 0x00));
    }
    if (At<uint8_t>(tool, zone::kDragging))
        Virtual<void>(tool, 0x6C); // CancelDrag
    fields(0);
    Virtual<void>(view, 0x68, uint32_t{0});
    f.Add(uint8_t{Virtual<void*>(view, 0x60) != nullptr});
    f.Add(g_unexpected);
    g_log = nullptr;
    log::Write("autotest: zone2: real %08X (cell %08X, start %d,%d)", f.h, Bits(cell), startX, startZ);
    out.Add(f.h);
}

// --- 4. FindFootprint ------------------------------------------------------------------

void PartFootprint(Fnv& out)
{
    Fnv f;
    g_log = &f;
    Swap city(addr::SC4_City, &g_city);
    Swap networks(addr::SC4_Service43D54, &g_networks);
    const auto call = [&](int32_t x, int32_t z) {
        int32_t kind = 0x55, orientation = 0x66;
        uint32_t config = 0x77;
        float point[3] = {Float(0x3F000001u), -1.0f, 9.5f};
        Game<void>(addr::SC4CheatTool_FindFootprint, &g_cheat, x, z, &kind, &config, &orientation, point);
        f.Add(kind);
        f.Add(config);
        f.Add(orientation);
        for (const float value : point)
            f.Add(Bits(value));
    };
    Field<void*>(g_networks, kState) = nullptr;
    call(10, 20);
    Field<void*>(g_networks, kState) = &g_piece;
    static const uint32_t kSides[] = {
        0x00020002u, // sides 0 and 2
        0x00020402u, // the same, side 1 flipped
        0x02000200u, // sides 1 and 3
        0x02000204u, // the same, side 0 flipped
        0x00F2F0F2u, // sides 0 and 2 with high bits, side 1 flipped
        0x00000202u, // a corner
        0x00020202u, // three sides
        0x02020202u, // four
        0x00000000u, // none
        0x00020003u, // side 0 is 3
        0x03020002u, // side 3 is 3
        0x00010002u, // side 1 is 1
        0x02040204u, // sides 1 and 3, both flipped... and side 0 flipped
    };
    static const int32_t kTypes[] = {0, 2, 3, 6, 10, 12, 1, 7, -1};
    for (const int32_t type : kTypes) {
        const uint32_t has = type < 0 ? 0u : (1u << type);
        for (const uint32_t extra : {0u, 0x10u, 0x100u}) {
            Field<uint32_t>(g_piece, kState) = has;
            Field<uint32_t>(g_piece, kState + 4) = has | extra;
            for (const uint32_t sides : kSides) {
                Field<uint32_t>(g_piece, kState + 8) = sides;
                call(type * 3 + 1, -type);
            }
        }
    }
    // A piece of two types: the first in the game's order counts.
    Field<uint32_t>(g_piece, kState) = (1u << 6) | (1u << 3);
    Field<uint32_t>(g_piece, kState + 4) = 1u << 3;
    Field<uint32_t>(g_piece, kState + 8) = 0x00020402u;
    call(5, 5);
    Field<uint32_t>(g_piece, kState + 4) = 1u << 6;
    call(5, 5);
    f.Add(g_unexpected);
    g_log = nullptr;
    log::Write("autotest: zone2: footprint %08X", f.h);
    out.Add(f.h);
}

// --- 5. The cheat tool -----------------------------------------------------------------

namespace cheat {
constexpr std::size_t kProp = 0x28, kModel = 0x2C, kKind = 0x30, kConfig = 0x34;
} // namespace cheat

struct CheatCase {
    uint8_t picks;
    uint8_t piece;       // 0 none, else the sides below apply
    int32_t type;
    uint32_t sides;
    uint8_t noModel;     // the model service gives none
    uint8_t noConfig;    // the configurations give none
    uint8_t keepSize;    // the size query writes nothing
    uint8_t noType;      // the prop types refuse
    uint8_t noProp;      // the prop manager refuses
};

void PartCheat(Fnv& out)
{
    Fnv f;
    g_log = &f;
    Swap view(addr::SC4_Service43DC4, &g_view);
    Swap city(addr::SC4_City, &g_city);
    Swap networks(addr::SC4_Service43D54, &g_networks);
    Swap terrain(addr::SC4_Terrain, &g_terrain);
    Swap models(addr::SC4_Service43D1C, &g_modelService);
    Swap props(addr::cSC4PropManager_Global, &g_props);
    Swap configs(addr::SC4_Service43D18, &g_configs);
    Swap types(addr::SC4_Service43D68, &g_types);
    Make(g_cheat, Role(&g_cheat));
    Field<int32_t>(g_cheat, cheat::kKind) = 1; // as Init leaves it
    Field<float>(g_terrain, kState + 4) = 12.125f;
    Field<uint32_t>(g_config, kState + 4) = 3;
    Field<uint32_t>(g_config, kState + 8) = 2;
    static const CheatCase kCases[] = {
        {0, 0, 0, 0, 0, 0, 0, 0, 0},
        {1, 0, 0, 0, 0, 0, 0, 0, 0},           // no piece: kind 0
        {1, 0, 0, 0, 0, 0, 0, 0, 0},           // again: nothing new
        {1, 1, 0, 0x00020002u, 0, 0, 0, 0, 0}, // a road: kind 1, a prop
        {1, 1, 0, 0x00020002u, 0, 0, 0, 0, 0}, // again: the prop moved
        {1, 1, 0, 0x02000200u, 0, 0, 0, 0, 0}, // turned: the same configuration
        {1, 1, 2, 0x02000204u, 0, 0, 0, 1, 0}, // rail: kind 2, the type refused
        {1, 1, 6, 0x00020402u, 0, 1, 0, 0, 0}, // no configuration
        {1, 1, 12, 0x00020002u, 0, 0, 0, 0, 1}, // the prop manager refuses
        {1, 1, 3, 0x02000200u, 0, 0, 1, 0, 0}, // the size left as the stack had it
        {1, 1, 10, 0x00020402u, 0, 0, 0, 0, 0},
        {1, 1, 0, 0x00000202u, 0, 0, 0, 0, 0}, // a corner: kind 0, the prop away
        {1, 1, 0, 0x00020002u, 1, 0, 0, 0, 0}, // no model given
        {1, 0, 0, 0, 0, 0, 0, 0, 0},
        {1, 1, 2, 0x00020002u, 0, 0, 0, 0, 0},
    };
    float pick = 40.0f;
    for (const CheatCase& c : kCases) {
        Field<uint8_t>(g_view, kState) = c.picks;
        const float point[3] = {pick, 1.5f, 1000.0f - pick};
        std::memcpy(&Field<float>(g_view, kState + 4), point, sizeof point);
        pick += 37.5f;
        Field<void*>(g_networks, kState) = c.piece ? &g_piece : nullptr;
        Field<uint32_t>(g_piece, kState) = 1u << c.type;
        Field<uint32_t>(g_piece, kState + 4) = 1u << c.type;
        Field<uint32_t>(g_piece, kState + 8) = c.sides;
        Field<uint8_t>(g_modelService, kState) = c.noModel;
        Field<uint8_t>(g_configs, kState) = c.noConfig;
        Field<uint8_t>(g_config, kState) = c.keepSize;
        Field<uint8_t>(g_types, kState) = c.noType;
        Field<uint8_t>(g_props, kState) = c.noProp;
        const uint8_t result = Game<uint8_t>(addr::SC4CheatTool_OnMouseMove, &g_cheat, int32_t{12}, int32_t{34},
                                             uint32_t{0x10});
        f.Add(result);
        f.Add(Which(Field<void*>(g_cheat, cheat::kProp)));
        f.Add(Which(Field<void*>(g_cheat, cheat::kModel)));
        f.Add(Field<int32_t>(g_cheat, cheat::kKind));
        f.Add(Field<uint32_t>(g_cheat, cheat::kConfig));
        for (Fake* counted : {&g_footA, &g_footB, &g_prop})
            f.Add(counted->raw[kCount - 4]);
    }
    f.Add(g_unexpected);
    g_log = nullptr;
    log::Write("autotest: zone2: cheat %08X", f.h);
    out.Add(f.h);
}

void Drive(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_T10") || !game::IsBuild641())
        return;
    if (!Global<void*>(addr::SC4_City)) {
        log::Write("autotest: zone2: no city");
        return;
    }
    const int parts = EnvInt("OPENSC4_TEST_T10_PARTS", 31);
    SetUpFakes();
    g_unexpected = 0;
    Fnv total;
    if (parts & 1)
        PartPlace(total);
    if (parts & 2)
        PartZone(total);
    if (parts & 4)
        PartRealZone(total);
    if (parts & 8)
        PartFootprint(total);
    if (parts & 16)
        PartCheat(total);
    total.Add(g_unexpected);
    std::snprintf(g_token, sizeof g_token, " zone2 %08X", total.h);
    log::Write("autotest: zone2: total %08X (parts %d, unexpected slots %d)", total.h, parts, g_unexpected);
}

const DayHook kZone2{"zone2", 71, &Drive};

void Fingerprint(cISC4City*, std::string& line)
{
    line += g_token;
}

const FingerprintPart kFingerprint{"zone2", 33, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
