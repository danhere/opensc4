// OPENSC4_TEST_T11=<day>: the plop tool's preview drawing (ui.tools.plop4,
// docs/tools.md). As T10's test it calls only the game's addresses and the
// tool's vtable slots, never our functions. The terrain, the city, the
// models and their service, the lot configurations, the preview and its
// occupant, the prop manager, the networks' service, the development
// simulator, the police, fire and other services, the property holders, the
// view and its markers, the image buffers, the framework's texture factory
// are stand-ins that record what they are told (only which stand-in was
// meant goes into the hash, never a pointer). Every stand-in slot the game
// does not ask is counted: `unexpected slots` must be 0.
//
// OPENSC4_TEST_T11_PARTS (decimal mask, default 15):
//   1  UpdateFootprint on a stand-in tool: no configuration, a point off the
//      terrain, records of kinds 5, 6 and others over the four rotations,
//      the vectors growing, refused models and models left over (hidden)
//   2  UpdatePreview on a stand-in tool (records for the footprint only
//      with the rotations 0 to 3): hidden (with and without a
//      preview), shown with the cursor effect's radius above, at and below
//      zero, the allowed byte changed or not, the footprint model missing,
//      hidden or shown, with and without a picture, the arrow over odd
//      rotations, cell sizes and lot depths (one moving it back past zero)
//   4  BuildPreview on a stand-in tool: no configuration; the preview made
//      (no info, no building types, no key, refused), with each service and
//      the placement count's properties; the prop records (kinds, flags,
//      type counts, the prop manager's answers, drawn or not); the picture
//      over sizes from below 16 to above 256 pixels and its refusals
//   8  a plop tool the view made (Init builds the preview), its preview
//      update shown over the four rotations and hidden, then put away
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <GZServPtrs.h>
#include <cIGZWin.h>
#include <cISC4App.h>
#include <cISC4City.h>
#include <cISC4Lot.h>
#include <cISC4LotConfiguration.h>
#include <cISC4LotManager.h>
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
// OPENSC4_TEST_T11_TRACE=1: every noted value logged too (to compare two
// runs call by call).
bool g_trace = false;

template <typename T>
void Note(const T& value)
{
    if (!g_log)
        return;
    g_log->Add(value);
    if (g_trace) {
        uint32_t word = 0;
        std::memcpy(&word, &value, sizeof value < sizeof word ? sizeof value : sizeof word);
        log::Write("autotest: plop4: n%u %08X", static_cast<unsigned>(sizeof value), word);
    }
}

// The hash so far of a part, logged after each case.
void CaseDone(const char* part, int index, const Fnv& f)
{
    log::Write("autotest: plop4: %s case %d %08X (unexpected %d)", part, index, f.h, g_unexpected);
}

void NoteText(const char* text)
{
    const uint32_t length = text ? static_cast<uint32_t>(std::strlen(text)) : 0xFFFFFFFFu;
    Note(length);
    if (g_log && text)
        g_log->Bytes(text, length);
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
    uint8_t raw[0x400];
    void* slots[kSlots];
};

// Where in a stand-in its own state is kept (past every field the game's
// code reads of the plop tool, 0x158 bytes).
constexpr std::size_t kState = 0x200;
constexpr std::size_t kRole = 0x3FE;
constexpr std::size_t kCount = 0x3FF;

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

uint8_t Role(const Fake* fake)
{
    return fake ? fake->raw[kRole - 4] : uint8_t{0};
}

uint8_t& Count(Fake& fake)
{
    return fake.raw[kCount - 4];
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

constexpr int kModels = 24;

Fake g_tool, g_terrain, g_surface, g_city, g_modelService, g_configs, g_config, g_holderModel, g_effect;
Fake g_preview, g_occupant, g_placeable, g_part, g_props, g_networks, g_development, g_police, g_fire;
Fake g_others, g_counts, g_view, g_propertyHolder, g_property, g_variant, g_bufferService, g_buffer;
Fake g_framework, g_factory, g_binding, g_image, g_propHolder, g_propOccupant;
Fake g_models[kModels];
int g_nextModel = 0;
// The markers object the view's slot 0xC4 gives: an empty hash table (the
// game's code reads its fields, no vtable).
uint8_t g_markers[0x40];

Fake* AllFakes(std::size_t i)
{
    static Fake* const fakes[] = {&g_tool, &g_terrain, &g_surface, &g_city, &g_modelService, &g_configs, &g_config,
                                  &g_holderModel, &g_effect, &g_preview, &g_occupant, &g_placeable, &g_part, &g_props,
                                  &g_networks, &g_development, &g_police, &g_fire, &g_others, &g_counts, &g_view,
                                  &g_propertyHolder, &g_property, &g_variant, &g_bufferService, &g_buffer,
                                  &g_framework, &g_factory, &g_binding, &g_image, &g_propHolder, &g_propOccupant};
    return i < sizeof fakes / sizeof fakes[0] ? fakes[i] : nullptr;
}

uint8_t Which(const void* pointer)
{
    if (!pointer)
        return 0;
    for (std::size_t i = 0; Fake* fake = AllFakes(i); ++i)
        if (fake == pointer)
            return Role(fake);
    for (Fake& model : g_models)
        if (&model == pointer)
            return Role(&model);
    if (pointer == g_markers)
        return 0xFD;
    return 0xFF;
}

uint32_t __fastcall AddRef(Fake* self, void*)
{
    Note(uint8_t{0x01});
    Note(Role(self));
    return ++Count(*self);
}

uint32_t __fastcall Release(Fake* self, void*)
{
    Note(uint8_t{0x02});
    Note(Role(self));
    return --Count(*self);
}

// The terrain: slot 6 the cell size (kState), slots 7 and 8 its size in cells
// (kState + 4, + 8), slot 66 the surface.
float __fastcall TerrainCell(Fake* self, void*)
{
    Note(uint8_t{0x10});
    return Field<float>(self, kState);
}

int32_t __fastcall TerrainWidth(Fake* self, void*)
{
    Note(uint8_t{0x11});
    return Field<int32_t>(self, kState + 4);
}

int32_t __fastcall TerrainDepth(Fake* self, void*)
{
    Note(uint8_t{0x12});
    return Field<int32_t>(self, kState + 8);
}

void* __fastcall TerrainSurface(Fake*, void*)
{
    Note(uint8_t{0x13});
    return &g_surface;
}

uint8_t __fastcall SurfaceCovered(Fake* self, void*)
{
    Note(uint8_t{0x14});
    return Field<uint8_t>(self, kState);
}

// The city: slot 95 a point's cell.
void __fastcall CityCell(Fake*, void*, float x, float z, int32_t* cellX, int32_t* cellZ)
{
    Note(uint8_t{0x15});
    Note(Bits(x));
    Note(Bits(z));
    *cellX = static_cast<int32_t>(x) / 16 + 1;
    *cellZ = static_cast<int32_t>(z) / 16 - 2;
}

// A model: shown at kState.
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
void __fastcall ModelSet(Fake* self, void*, const uint8_t* t)
{
    Note(uint8_t{0x23});
    Note(Role(self));
    Note(t[0]);
    Note(t[1]);
    for (std::size_t i = 4; i < 0x38; i += 4) {
        uint32_t word;
        std::memcpy(&word, t + i, sizeof word);
        Note(word);
    }
}

void __fastcall ModelCorners(Fake* self, void*, uint32_t what, const float* corners, uint32_t count)
{
    Note(uint8_t{0x24});
    Note(Role(self));
    Note(what);
    Note(count);
    for (uint32_t i = 0; i < 4; ++i)
        Note(Bits(corners[i]));
}

void __fastcall ModelTexture(Fake* self, void*, uint32_t what, void** texture, uint32_t one)
{
    Note(uint8_t{0x25});
    Note(Role(self));
    Note(what);
    Note(Which(*texture));
    Note(one);
}

// The model service: the next model of the pool by name (kState: give none).
void __fastcall ModelMake(Fake* self, void*, const char* name, void** holder)
{
    Note(uint8_t{0x26});
    NoteText(name);
    Note(Which(*holder));
    if (Field<uint8_t>(self, kState) || g_nextModel >= kModels)
        return;
    Fake* const model = &g_models[g_nextModel++];
    ++Count(*model);
    *holder = model;
}

// The lot configurations: slot 8 by ID (kState: none); a configuration's
// size (kState: how many calls leave the outputs; kState + 4, + 8 the
// size), its records
// (the vector at kState + 0x10) and its info (kState + 0x20: give none when
// set; the block at kState + 0x40).
void* __fastcall ConfigFind(Fake* self, void*, uint32_t id)
{
    Note(uint8_t{0x30});
    Note(id);
    return Field<uint8_t>(self, kState) ? nullptr : &g_config;
}

void __fastcall ConfigSize(Fake* self, void*, uint32_t* width, uint32_t* depth)
{
    Note(uint8_t{0x31});
    if (Field<uint8_t>(self, kState)) {
        --Field<uint8_t>(self, kState);
        return;
    }
    *width = Field<uint32_t>(self, kState + 4);
    *depth = Field<uint32_t>(self, kState + 8);
}

void* __fastcall ConfigRecords(Fake* self, void*)
{
    Note(uint8_t{0x32});
    return &Field<char>(self, kState + 0x10);
}

const char* __fastcall ConfigInfo(Fake* self, void*)
{
    Note(uint8_t{0x33});
    return Field<uint8_t>(self, kState + 0x20) ? nullptr : &Field<char>(self, kState + 0x40);
}

// The tool's own slot 8 (the cursor).
void __fastcall ToolCursor(Fake*, void*, uint32_t id)
{
    Note(uint8_t{0x34});
    Note(id);
}

// The preview's holder: slot 3 the occupant, slots 9 and 10 its turn.
void* __fastcall PreviewOccupant(Fake*, void*)
{
    Note(uint8_t{0x40});
    return &g_occupant;
}

int32_t __fastcall PreviewTurn(Fake* self, void*)
{
    Note(uint8_t{0x41});
    return Field<int32_t>(self, kState);
}

void __fastcall PreviewTurnSet(Fake* self, void*, int32_t turn)
{
    Note(uint8_t{0x42});
    Note(turn);
    Field<int32_t>(self, kState) = turn;
}

// An occupant: its place, its "in the city" flag (kState), a message, its
// parts (kState + 1: none), its flags, its properties, its kind (slot 22:
// kState + 4 the kind it answers true for).
void __fastcall OccupantPlace(Fake* self, void*, const float* point)
{
    Note(uint8_t{0x43});
    Note(Role(self));
    for (int i = 0; i < 3; ++i)
        Note(Bits(point[i]));
}

uint8_t __fastcall OccupantIn(Fake* self, void*, uint32_t one)
{
    Note(uint8_t{0x44});
    Note(Role(self));
    Note(one);
    return Field<uint8_t>(self, kState);
}

void __fastcall OccupantMessage(Fake* self, void*, uint32_t message, uint32_t data)
{
    Note(uint8_t{0x45});
    Note(Role(self));
    Note(message);
    Note(data);
}

void* __fastcall OccupantParts(Fake* self, void*)
{
    Note(uint8_t{0x46});
    return Field<uint8_t>(self, kState + 1) ? nullptr : &g_placeable;
}

void __fastcall OccupantFlags(Fake* self, void*, uint32_t flags)
{
    Note(uint8_t{0x47});
    Note(Role(self));
    Note(flags);
}

void* __fastcall OccupantProperties(Fake*, void*)
{
    Note(uint8_t{0x48});
    return &g_propertyHolder;
}

uint8_t __fastcall OccupantKind(Fake* self, void*, uint32_t kind)
{
    Note(uint8_t{0x49});
    Note(kind);
    return Field<uint32_t>(self, kState + 4) == kind;
}

// The parts: one part; the value it is handed is the tint block (the two
// bytes after the mode and tint only when the case knows them).
bool g_tintStackKnown = false;

void __fastcall PlaceableLock(Fake*, void*)
{
    Note(uint8_t{0x4A});
}

void __fastcall PlaceableUnlock(Fake*, void*)
{
    Note(uint8_t{0x4B});
}

uint32_t __fastcall PlaceableCount(Fake*, void*)
{
    Note(uint8_t{0x4C});
    return 1;
}

void __fastcall PlaceableFill(Fake*, void*, void** buffer, uint32_t* count)
{
    Note(uint8_t{0x4D});
    Note(*count);
    buffer[0] = &g_part;
}

void __fastcall PartState(Fake*, void*, uint32_t state)
{
    Note(uint8_t{0x4E});
    Note(state);
}

void __fastcall PartValue(Fake*, void*, const uint8_t* tint)
{
    Note(uint8_t{0x4F});
    Note(tint[0]);
    Note(tint[1]);
    if (g_tintStackKnown) {
        Note(tint[2]);
        Note(tint[3]);
    }
    for (std::size_t i = 4; i < 0x14; i += 4) {
        uint32_t word;
        std::memcpy(&word, tint + i, sizeof word);
        Note(word);
    }
}

// The prop manager: slot 19 adds an occupant, 29 takes one away; slot 8
// whether a prop type is known (kState: no), slot 15 a prop for a type
// (kState + 1: refuse), slot 16 the preview for a key (kState + 2: refuse).
void __fastcall PropsAdd(Fake*, void*, void* occupant)
{
    Note(uint8_t{0x50});
    Note(Which(occupant));
}

void __fastcall PropsRemove(Fake*, void*, void* occupant)
{
    Note(uint8_t{0x51});
    Note(Which(occupant));
}

uint8_t __fastcall PropsKnown(Fake* self, void*, uint32_t type)
{
    Note(uint8_t{0x52});
    Note(type);
    return !Field<uint8_t>(self, kState) && type != 0x5150;
}

uint8_t __fastcall PropsMakeProp(Fake* self, void*, uint32_t type, void** holder)
{
    Note(uint8_t{0x53});
    Note(type);
    Note(Which(*holder));
    if (Field<uint8_t>(self, kState + 1) || type == 0x5151)
        return 0;
    ++Count(g_propHolder);
    *holder = &g_propHolder;
    return 1;
}

uint8_t __fastcall PropsMakePreview(Fake* self, void*, const uint32_t* key, void** holder)
{
    Note(uint8_t{0x54});
    for (int i = 0; i < 3; ++i)
        Note(key[i]);
    Note(Which(*holder));
    if (Field<uint8_t>(self, kState + 2))
        return 0;
    ++Count(g_preview);
    *holder = &g_preview;
    return 1;
}

void __fastcall NetworksMove(Fake*, void*, void* occupant, uint32_t one)
{
    Note(uint8_t{0x55});
    Note(Which(occupant));
    Note(one);
}

// A prop's holder (from the prop manager's slot 15): slot 3 its occupant.
void* __fastcall PropHolderOccupant(Fake*, void*)
{
    Note(uint8_t{0x56});
    return &g_propOccupant;
}

// The development simulator: slot 19 adds the building types of an ID to
// the set (kState: add none; IDs below 3 give none), slot 14 the key of a
// type (kState + 1: refuse).
void __fastcall DevelopmentTypes(Fake* self, void*, void* set, uint32_t id)
{
    Note(uint8_t{0x57});
    Note(id);
    if (Field<uint8_t>(self, kState) || id < 3)
        return;
    for (uint32_t type : {id * 7, id * 7 + 1}) {
        uint32_t result[4] = {};
        Game<void>(addr::SC4Uint32HashSet_InsertUnique, set, result, &type);
    }
}

uint8_t __fastcall DevelopmentKey(Fake* self, void*, uint32_t type, uint32_t* key)
{
    Note(uint8_t{0x58});
    Note(type);
    for (int i = 0; i < 3; ++i)
        Note(key[i]);
    if (Field<uint8_t>(self, kState + 1))
        return 0;
    key[0] = 0x12345678u;
    key[1] = type;
    key[2] = 0xABCD0000u ^ type;
    return 1;
}

// The services: police slot 22 "gives cover" (kState), slot 20 the radius
// (kState + 1: refuse; the radius kState + 4), slot 21 the list of its
// buildings (none); fire slot 7, 21 and 22 likewise; the others slot 45
// (kState + 1: refuse) and slots 47 and 49 their lists (left empty).
uint8_t __fastcall ServiceCovers(Fake* self, void*, void* occupant)
{
    Note(uint8_t{0x60});
    Note(Role(self));
    Note(Which(occupant));
    return Field<uint8_t>(self, kState);
}

uint8_t __fastcall ServiceRadius(Fake* self, void*, void* occupant, const float* share, float* out, float* radius)
{
    Note(uint8_t{0x61});
    Note(Role(self));
    Note(Which(occupant));
    Note(Bits(*share));
    Note(Bits(*out));
    Note(Bits(*radius));
    if (Field<uint8_t>(self, kState + 1))
        return 0;
    *out = *share * 3.0f;
    *radius = Field<float>(self, kState + 4) * *share;
    return 1;
}

uint32_t __fastcall ServiceList(Fake* self, void*, void*)
{
    Note(uint8_t{0x62});
    Note(Role(self));
    return 0;
}

void __fastcall ServiceListOf(Fake* self, void*, void*, uint32_t zero)
{
    Note(uint8_t{0x63});
    Note(Role(self));
    Note(zero);
}

uint32_t __fastcall CountsOf(Fake*, void*, uint32_t value)
{
    Note(uint8_t{0x64});
    Note(value);
    return value * 3 + 1;
}

// The view: slot 49 the markers object.
void* __fastcall ViewMarkers(Fake*, void*)
{
    Note(uint8_t{0x65});
    return g_markers;
}

// The properties: the holder's slot 9 by ID gives the property when the ID
// is in the table (with its value), slot 15 adds one (not expected); the
// property's slot 5 the variant; the variant's getters write the value of
// the property last asked, its setter notes the value.
struct PropertyValue {
    uint32_t id;
    uint32_t value;
};
PropertyValue g_values[8];
int g_valueCount = 0;
uint32_t g_asked = 0;

void* __fastcall HolderProperty(Fake*, void*, uint32_t id)
{
    Note(uint8_t{0x70});
    Note(id);
    g_asked = id;
    if (id == 0x029244DBu || id == 0x29F013E6u)
        return &g_property;
    for (int i = 0; i < g_valueCount; ++i)
        if (g_values[i].id == id)
            return &g_property;
    return nullptr;
}

void* __fastcall PropertyVariant(Fake*, void*)
{
    Note(uint8_t{0x71});
    return &g_variant;
}

uint32_t Asked()
{
    for (int i = 0; i < g_valueCount; ++i)
        if (g_values[i].id == g_asked)
            return g_values[i].value;
    return 0;
}

uint8_t __fastcall VariantBool(Fake*, void*, uint8_t* out)
{
    Note(uint8_t{0x72});
    *out = static_cast<uint8_t>(Asked() != 0);
    return 1;
}

uint8_t __fastcall VariantUint8(Fake*, void*, uint8_t* out)
{
    Note(uint8_t{0x73});
    *out = static_cast<uint8_t>(Asked());
    return 1;
}

uint8_t __fastcall VariantUint32(Fake*, void*, uint32_t* out)
{
    Note(uint8_t{0x74});
    *out = Asked();
    return 1;
}

void __fastcall VariantSet(Fake*, void*, uint32_t value)
{
    Note(uint8_t{0x75});
    Note(static_cast<uint8_t>(value));
}

// The image buffers: the service's slot 3 makes one (kState: refuse); the
// buffer's slot 3 sizes it (kState: refuse), 6 locks (kState + 1, + 2:
// refuse the two kinds), 7 unlocks, 18 clears, 19 fills a rectangle, 34 its
// image, 4 ends.
uint8_t __fastcall BufferMake(Fake* self, void*, void** out)
{
    Note(uint8_t{0x80});
    Note(Which(*out));
    if (Field<uint8_t>(self, kState))
        return 0;
    ++Count(g_buffer);
    *out = &g_buffer;
    return 1;
}

uint8_t __fastcall BufferInit(Fake* self, void*, uint32_t width, uint32_t height, uint32_t format, uint32_t bits)
{
    Note(uint8_t{0x81});
    Note(width);
    Note(height);
    Note(format);
    Note(bits);
    return !Field<uint8_t>(self, kState);
}

uint8_t __fastcall BufferLock(Fake* self, void*, uint32_t kind)
{
    Note(uint8_t{0x82});
    Note(kind);
    if (kind == 0x8001)
        return !Field<uint8_t>(self, kState + 1);
    return !Field<uint8_t>(self, kState + 2);
}

void __fastcall BufferUnlock(Fake*, void*, uint32_t kind)
{
    Note(uint8_t{0x83});
    Note(kind);
}

void __fastcall BufferClear(Fake*, void*, uint32_t a, uint32_t b)
{
    Note(uint8_t{0x84});
    Note(a);
    Note(b);
}

void __fastcall BufferFill(Fake*, void*, uint32_t colour, uint32_t x0, uint32_t z0, uint32_t x1, uint32_t z1,
                           uint32_t last)
{
    Note(uint8_t{0x85});
    Note(colour);
    Note(x0);
    Note(z0);
    Note(x1);
    Note(z1);
    Note(last);
}

void* __fastcall BufferImage(Fake*, void*)
{
    Note(uint8_t{0x86});
    return &g_image;
}

void __fastcall BufferEnd(Fake*, void*)
{
    Note(uint8_t{0x87});
}

// The framework: the texture factory for its class, the real framework for
// every other (the game's list allocator may be asked for).
void* g_realFramework = nullptr;

uint8_t __fastcall FrameworkGet(Fake* self, void*, uint32_t classId, uint32_t interfaceId, void** out)
{
    if (classId != 0x1AC0E11Au)
        return Virtual<uint8_t>(g_realFramework, 0x14, classId, interfaceId, out);
    Note(uint8_t{0x88});
    Note(interfaceId);
    if (Field<uint8_t>(self, kState))
        return 0;
    ++Count(g_factory);
    *out = &g_factory;
    return 1;
}

void __fastcall FactoryMake(Fake*, void*, const uint32_t* description, void** binding)
{
    Note(uint8_t{0x89});
    for (int i = 0; i < 8; ++i)
        if (i != 4) // the word the game's stack held
            Note(i == 7 ? static_cast<uint32_t>(Which(*reinterpret_cast<void* const*>(description[7])))
                        : description[i]);
    Note(Which(*binding));
    *binding = &g_binding;
}

void SetUpFakes()
{
    static bool done = false;
    if (done)
        return;
    done = true;
    uint8_t role = 1;
    for (std::size_t i = 0; Fake* fake = AllFakes(i); ++i)
        Make(*fake, role++);
    for (Fake& model : g_models)
        Make(model, role++);
}

// The stand-ins' slots, set again before each case (the cases change their
// states only).
void Wire()
{
    Slot(g_tool, 0x20, &ToolCursor);
    Slot(g_terrain, 0x18, &TerrainCell);
    Slot(g_terrain, 0x1C, &TerrainWidth);
    Slot(g_terrain, 0x20, &TerrainDepth);
    Slot(g_terrain, 0x108, &TerrainSurface);
    Slot(g_surface, 0x50, &SurfaceCovered);
    Slot(g_city, 0x4, &AddRef);
    Slot(g_city, 0x8, &Release);
    Slot(g_city, 0x17C, &CityCell);
    auto model = [](Fake& m) {
        Slot(m, 0x4, &AddRef);
        Slot(m, 0x8, &Release);
        Slot(m, 0xC, &ModelShow);
        Slot(m, 0x10, &ModelHide);
        Slot(m, 0x14, &ModelShown);
        Slot(m, 0x20, &ModelSet);
        Slot(m, 0x30, &ModelCorners);
        Slot(m, 0x34, &ModelTexture);
    };
    for (Fake& m : g_models)
        model(m);
    model(g_holderModel);
    model(g_effect);
    Slot(g_modelService, 0x1C, &ModelMake);
    Slot(g_configs, 0x20, &ConfigFind);
    Slot(g_config, 0x1C, &ConfigSize);
    Slot(g_config, 0x5C, &ConfigRecords);
    Slot(g_config, 0x60, &ConfigInfo);
    Slot(g_preview, 0x4, &AddRef);
    Slot(g_preview, 0x8, &Release);
    Slot(g_preview, 0xC, &PreviewOccupant);
    Slot(g_preview, 0x24, &PreviewTurn);
    Slot(g_preview, 0x28, &PreviewTurnSet);
    for (Fake* occupant : {&g_occupant, &g_propOccupant}) {
        Slot(*occupant, 0x18, &OccupantProperties);
        Slot(*occupant, 0x24, &OccupantPlace);
        Slot(*occupant, 0x3C, &OccupantMessage);
        Slot(*occupant, 0x4C, &OccupantParts);
        Slot(*occupant, 0x58, &OccupantKind);
        Slot(*occupant, 0x74, &OccupantFlags);
        Slot(*occupant, 0x80, &OccupantIn);
    }
    Slot(g_placeable, 0x4, &PlaceableLock);
    Slot(g_placeable, 0x8, &PlaceableUnlock);
    Slot(g_placeable, 0xC, &PlaceableCount);
    Slot(g_placeable, 0x10, &PlaceableFill);
    Slot(g_part, 0x38, &PartState);
    Slot(g_part, 0x40, &PartValue);
    Slot(g_props, 0x20, &PropsKnown);
    Slot(g_props, 0x3C, &PropsMakeProp);
    Slot(g_props, 0x40, &PropsMakePreview);
    Slot(g_props, 0x4C, &PropsAdd);
    Slot(g_props, 0x74, &PropsRemove);
    Slot(g_networks, 0x60, &NetworksMove);
    Slot(g_propHolder, 0x4, &AddRef);
    Slot(g_propHolder, 0x8, &Release);
    Slot(g_propHolder, 0xC, &PropHolderOccupant);
    Slot(g_development, 0x4C, &DevelopmentTypes);
    Slot(g_development, 0x38, &DevelopmentKey);
    Slot(g_police, 0x58, &ServiceCovers);
    Slot(g_police, 0x50, &ServiceRadius);
    Slot(g_police, 0x54, &ServiceList);
    Slot(g_fire, 0x1C, &ServiceCovers);
    Slot(g_fire, 0x54, &ServiceRadius);
    Slot(g_fire, 0x58, &ServiceList);
    Slot(g_others, 0xB4, &ServiceRadius);
    Slot(g_others, 0xC4, &ServiceListOf);
    Slot(g_others, 0xBC, &ServiceListOf);
    Slot(g_counts, 0x80, &CountsOf);
    Slot(g_view, 0xC4, &ViewMarkers);
    Slot(g_propertyHolder, 0x24, &HolderProperty);
    Slot(g_property, 0x14, &PropertyVariant);
    Slot(g_variant, 0x28, &VariantBool);
    Slot(g_variant, 0x2C, &VariantSet);
    Slot(g_variant, 0x34, &VariantUint8);
    Slot(g_variant, 0x64, &VariantUint32);
    Slot(g_bufferService, 0xC, &BufferMake);
    Slot(g_buffer, 0x4, &AddRef);
    Slot(g_buffer, 0x8, &Release);
    Slot(g_buffer, 0xC, &BufferInit);
    Slot(g_buffer, 0x10, &BufferEnd);
    Slot(g_buffer, 0x18, &BufferLock);
    Slot(g_buffer, 0x1C, &BufferUnlock);
    Slot(g_buffer, 0x48, &BufferClear);
    Slot(g_buffer, 0x4C, &BufferFill);
    Slot(g_buffer, 0x88, &BufferImage);
    Slot(g_framework, 0x14, &FrameworkGet);
    Slot(g_factory, 0x4, &AddRef);
    Slot(g_factory, 0x8, &Release);
    Slot(g_factory, 0x10, &FactoryMake);
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

// The tool's fields (docs/tools.md).
namespace tool {
constexpr std::size_t kView = 0x1C, kConfig = 0x30, kRotation = 0x3C, kConfigManager = 0x4C, kDevelopment = 0x68;
constexpr std::size_t kPoint = 0x88, kAllowed = 0x94, kHolder = 0xAC, kInactive = 0xB0, kPlop = 0xC4;
constexpr std::size_t kEffect = 0xD8, kFootprint = 0xDC, kArrow = 0xE0, kPreview = 0xE4, kBlue = 0xE8, kRed = 0xF4;
constexpr std::size_t kTexture = 0x100, kPictureSize = 0x104, kTurnOffset = 0x108, kOffsetA = 0x10C;
constexpr std::size_t kRadiusOut = 0x11C, kRadius = 0x120, kServices = 0x124, kCursor = 0x140, kRecords = 0x144;
constexpr std::size_t kPlacementsLeft = 0x154;
} // namespace tool

// A vector of model pointers in the tool (begin, end, capacity), from the
// game's allocator so that the game's vector code may grow and free it.
void SetModels(std::size_t at, std::initializer_list<Fake*> models)
{
    char* const self = reinterpret_cast<char*>(&g_tool);
    void** const buffer = Cdecl<void**>(addr::SC4_stl_allocate, uint32_t{16 * 4});
    std::size_t n = 0;
    for (Fake* model : models) {
        buffer[n++] = model;
        if (model)
            ++Count(*model);
    }
    At<void**>(self, at) = buffer;
    At<void**>(self, at + 4) = buffer + n;
    At<void**>(self, at + 8) = buffer + 16;
}

// What a vector holds: its size and which stand-ins.
void HashModels(Fnv& f, std::size_t at)
{
    char* const self = reinterpret_cast<char*>(&g_tool);
    void** const begin = At<void**>(self, at);
    void** const end = At<void**>(self, at + 4);
    f.Add(static_cast<uint32_t>(end - begin));
    for (void** p = begin; p != end; ++p) {
        f.Add(Which(*p));
        if (*p)
            f.Add(Field<uint8_t>(static_cast<Fake*>(*p), kState));
    }
}

// The tool stand-in made afresh: its vtable, its services, no vectors.
void FreshTool()
{
    Make(g_tool, Role(&g_tool));
    char* const self = reinterpret_cast<char*>(&g_tool);
    At<void*>(self, tool::kConfigManager) = &g_configs;
    At<void*>(self, tool::kDevelopment) = &g_development;
    At<void*>(self, tool::kView) = &g_view;
    At<uint32_t>(self, tool::kConfig) = 0x30000B61u;
    g_nextModel = 0;
    for (Fake& model : g_models) {
        Field<uint8_t>(&model, kState) = 0;
        Count(model) = 0;
    }
}

// A record of a lot configuration (0x34 bytes): kind, flags, turn, floats a
// to c, the two points the footprint uses, a vector of prop types.
struct Record {
    uint8_t kind;
    uint8_t flags;
    uint8_t turn;
    uint8_t pad;
    float a, b, c;
    float p[4];
    uint8_t rest[8];
    uint32_t* typesBegin;
    uint32_t* typesEnd;
    uint32_t* typesCap;
};
static_assert(sizeof(Record) == 0x34);

Record g_records[12];

void SetRecords(int count)
{
    char** const vector = &Field<char*>(g_config, kState + 0x10);
    vector[0] = reinterpret_cast<char*>(g_records);
    vector[1] = reinterpret_cast<char*>(g_records + count);
}

// --- 1. UpdateFootprint ------------------------------------------------------------------

void PartFootprint(Fnv& out)
{
    Fnv f;
    g_log = &f;
    Swap terrain(addr::SC4_Terrain, &g_terrain);
    Swap city(addr::SC4_City, &g_city);
    Swap models(addr::SC4_Service43D1C, &g_modelService);
    Field<float>(g_terrain, kState) = 16.0f;
    Field<int32_t>(g_terrain, kState + 4) = 64;
    Field<int32_t>(g_terrain, kState + 8) = 64;
    std::memset(g_records, 0, sizeof g_records);
    const struct {
        uint8_t kind;
        float p[4];
    } kRecords[] = {
        {5, {8.0f, 8.0f, 40.0f, 24.0f}},   {6, {-8.5f, 0.0f, 23.9f, 31.75f}}, {1, {0.0f, 0.0f, 0.0f, 0.0f}},
        {5, {16.0f, 32.0f, 16.0f, 32.0f}}, {6, {100.0f, 3.0f, 4.0f, 77.5f}},  {7, {1.0f, 2.0f, 3.0f, 4.0f}},
        {5, {-40.0f, -40.0f, 7.5f, 9.5f}},
    };
    int n = 0;
    for (const auto& r : kRecords) {
        g_records[n].kind = r.kind;
        std::memcpy(g_records[n].p, r.p, sizeof r.p);
        ++n;
    }
    struct Case {
        uint8_t noConfig;
        float point[3];
        int32_t rotation;
        uint32_t width, depth;
        int records;
        uint8_t blueBefore; // models in the blue vector before
        uint8_t redBefore;
        uint8_t refuse;
    };
    static const Case kCases[] = {
        {1, {100.0f, 0.0f, 100.0f}, 0, 2, 3, 7, 0, 0, 0},
        {0, {-500.0f, 0.0f, 100.0f}, 0, 2, 3, 7, 2, 1, 0}, // off the terrain: all hidden
        {0, {100.0f, 1.0f, 100.0f}, 0, 2, 3, 7, 0, 0, 0},
        {0, {100.0f, 1.0f, 100.0f}, 1, 2, 3, 7, 0, 0, 0},
        {0, {231.5f, 1.0f, 87.25f}, 2, 3, 2, 7, 1, 3, 0},
        {0, {231.5f, 1.0f, 87.25f}, 3, 4, 4, 7, 4, 4, 0}, // models left over: hidden
        {0, {300.0f, 2.0f, 300.0f}, 0, 1, 1, 7, 0, 0, 1}, // the model service refuses
        {0, {300.0f, 2.0f, 300.0f}, 1, 5, 1, 3, 0, 0, 0},
        {0, {64.0f, 0.0f, 64.0f}, 2, 2, 2, 0, 2, 2, 0},   // no records: all hidden
    };
    for (const Case& c : kCases) {
        Fnv one;
        g_log = &one;
        FreshTool();
        Wire();
        char* const self = reinterpret_cast<char*>(&g_tool);
        At<int32_t>(self, tool::kRotation) = c.rotation;
        Field<uint8_t>(g_configs, kState) = c.noConfig;
        Field<uint8_t>(g_config, kState) = 0;
        Field<uint32_t>(g_config, kState + 4) = c.width;
        Field<uint32_t>(g_config, kState + 8) = c.depth;
        Field<uint8_t>(g_modelService, kState) = c.refuse;
        SetRecords(c.records);
        std::initializer_list<Fake*> none{};
        SetModels(tool::kBlue, none);
        SetModels(tool::kRed, none);
        {
            Fake* blue[4];
            Fake* red[4];
            for (int i = 0; i < c.blueBefore; ++i)
                blue[i] = &g_models[g_nextModel++];
            for (int i = 0; i < c.redBefore; ++i)
                red[i] = &g_models[g_nextModel++];
            void** const b = At<void**>(self, tool::kBlue);
            for (int i = 0; i < c.blueBefore; ++i) {
                b[i] = blue[i];
                Field<uint8_t>(blue[i], kState) = 1;
            }
            At<void**>(self, tool::kBlue + 4) = b + c.blueBefore;
            void** const r = At<void**>(self, tool::kRed);
            for (int i = 0; i < c.redBefore; ++i) {
                r[i] = red[i];
                Field<uint8_t>(red[i], kState) = 1;
            }
            At<void**>(self, tool::kRed + 4) = r + c.redBefore;
        }
        Game<void>(addr::cSC4ViewInputControlPlaceLot_UpdateFootprint, &g_tool, static_cast<const float*>(c.point));
        HashModels(one, tool::kBlue);
        HashModels(one, tool::kRed);
        one.Add(Count(g_city));
        f.Add(one.h);
        CaseDone("footprint", static_cast<int>(&c - kCases), one);
    }
    f.Add(g_unexpected);
    g_log = nullptr;
    log::Write("autotest: plop4: footprint %08X", f.h);
    out.Add(f.h);
}

// --- 2. UpdatePreview --------------------------------------------------------------------

void PartPreview(Fnv& out)
{
    Fnv f;
    g_log = &f;
    Swap terrain(addr::SC4_Terrain, &g_terrain);
    Swap city(addr::SC4_City, &g_city);
    Swap models(addr::SC4_Service43D1C, &g_modelService);
    Swap props(addr::SC4_Service43D10, &g_props);
    Swap networks(addr::SC4_NetworkService, &g_networks);
    Field<int32_t>(g_terrain, kState + 4) = 64;
    Field<int32_t>(g_terrain, kState + 8) = 64;
    std::memset(g_records, 0, sizeof g_records);
    g_records[0].kind = 5;
    const float p0[4] = {8.0f, 8.0f, 24.0f, 40.0f};
    std::memcpy(g_records[0].p, p0, sizeof p0);
    g_records[1].kind = 6;
    const float p1[4] = {-8.0f, 16.0f, 0.0f, 16.0f};
    std::memcpy(g_records[1].p, p1, sizeof p1);
    struct Case {
        uint32_t allowed;  // the dword pushed
        uint32_t shown;    // the dword pushed
        uint8_t allowedBefore;
        float point[3];
        int32_t rotation;
        float cell;
        uint32_t width, depth;
        uint8_t keepSize; // the footprint's and the arrow's size queries left (no records then)
        uint8_t effect;   // 0 none, 1 hidden, 2 shown
        uint32_t radius;  // bits
        uint8_t footprint; // 0 none, 1 hidden, 2 shown
        uint8_t arrow;     // 0 none, 1 hidden, 2 shown
        uint8_t preview;
        uint8_t texture;
        uint32_t pictureSize; // bits
        uint8_t covered;
        uint8_t refuse;   // the model service refuses
        uint8_t noConfig;
        uint8_t stackKnown; // a turn runs, so the tint block's stack bytes are known
    };
    static const Case kCases[] = {
        // Hidden.
        {1, 0, 1, {10.0f, 2.0f, 30.0f}, 0, 16.0f, 2, 3, 0, 2, 0x40000000u, 2, 2, 1, 1, 0x43000000u, 0, 0, 0, 0},
        {0, 0xABCDEF00u, 0, {10.0f, 2.0f, 30.0f}, 1, 16.0f, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        // Shown, the allowed byte unchanged, all present.
        {1, 1, 1, {100.0f, 3.5f, 200.0f}, 0, 16.0f, 2, 3, 0, 2, 0x41200000u, 2, 2, 1, 1, 0x43000000u, 0, 0, 0, 1},
        {1, 1, 1, {100.0f, 3.5f, 200.0f}, 1, 16.0f, 2, 3, 0, 1, 0x3F000000u, 2, 2, 1, 1, 0x43000000u, 1, 0, 0, 1},
        {1, 1, 1, {100.0f, 3.5f, 200.0f}, 2, 16.0f, 3, 1, 0, 2, 0x00000000u, 1, 1, 1, 0, 0x41800000u, 0, 0, 0, 1},
        {1, 1, 1, {100.0f, 3.5f, 200.0f}, 3, 16.0f, 1, 4, 0, 2, 0xBF800000u, 2, 2, 1, 1, 0x3F800054u, 0, 0, 0, 1},
        // Changed allowed byte: models made again, the cursor set.
        {0, 1, 1, {-3.25f, 0.0f, 1e6f}, 0, 16.0f, 2, 2, 0, 2, 0x7FC00000u, 2, 2, 1, 1, 0x43800000u, 0, 0, 0, 1},
        {1, 0x01020301u, 0, {55.5f, 1.0f, 66.6f}, 5, 1.7f, 6, 5, 0, 0, 0, 0, 0, 1, 0, 0x42800000u, 0, 0, 0, 1},
        {1, 1, 0, {55.5f, 1.0f, 66.6f}, -1, 0.1f, 3, 7, 0, 0, 0, 0, 0, 0, 1, 0x3F800000u, 0, 0, 0, 1},
        {0, 1, 0, {4096.0f, 9.0f, -4096.0f}, 7, 16.0f, 1, 0, 0, 1, 0x3F800000u, 1, 1, 1, 1, 0x43000000u, 0, 0, 0, 1},
        // The size left as the stack had it; the models refused; no configuration.
        {1, 0x77665501u, 1, {12.0f, 1.0f, 12.0f}, 1, 16.0f, 2, 3, 1, 0, 0, 0, 0, 1, 1, 0x43000000u, 0, 0, 0, 1},
        {1, 0x77665501u, 1, {12.0f, 1.0f, 12.0f}, 2, 16.0f, 2, 3, 1, 0, 0, 2, 0, 1, 0, 0x43000000u, 0, 0, 0, 1},
        {0, 1, 1, {12.0f, 1.0f, 12.0f}, 3, 16.0f, 2, 3, 0, 0, 0, 0, 0, 1, 1, 0x43000000u, 0, 1, 0, 0},
        {1, 1, 0, {12.0f, 1.0f, 12.0f}, 0, 16.0f, 2, 3, 0, 2, 0x41000000u, 2, 2, 1, 1, 0x43000000u, 0, 0, 1, 0},
    };
    for (const Case& c : kCases) {
        Fnv one;
        g_log = &one;
        FreshTool();
        Wire();
        char* const self = reinterpret_cast<char*>(&g_tool);
        Field<float>(g_terrain, kState) = c.cell;
        At<int32_t>(self, tool::kRotation) = c.rotation;
        At<uint8_t>(self, tool::kAllowed) = c.allowedBefore;
        Field<uint8_t>(g_configs, kState) = c.noConfig;
        Field<uint8_t>(g_config, kState) = static_cast<uint8_t>(c.keepSize ? 2 : 0);
        Field<uint32_t>(g_config, kState + 4) = c.width;
        Field<uint32_t>(g_config, kState + 8) = c.depth;
        // RotateRectangle leaves its output as it was for a rotation outside
        // 0 to 3 (the game's stack then), so those cases have no records.
        SetRecords(c.keepSize || c.rotation < 0 || c.rotation > 3 ? 0 : 2);
        Field<uint8_t>(g_modelService, kState) = c.refuse;
        Field<uint8_t>(g_surface, kState) = c.covered;
        At<void*>(self, tool::kHolder) = &g_holderModel;
        Field<uint8_t>(g_holderModel, kState) = 0;
        At<void*>(self, tool::kEffect) = c.effect ? &g_effect : nullptr;
        Field<uint8_t>(g_effect, kState) = c.effect == 2;
        At<uint32_t>(self, tool::kRadius) = c.radius;
        auto give = [&](std::size_t at, uint8_t state) {
            if (!state)
                return;
            Fake* const m = &g_models[g_nextModel++];
            ++Count(*m);
            Field<uint8_t>(m, kState) = state == 2;
            At<void*>(self, at) = m;
        };
        give(tool::kFootprint, c.footprint);
        give(tool::kArrow, c.arrow);
        At<void*>(self, tool::kPreview) = c.preview ? &g_preview : nullptr;
        Field<uint8_t>(g_occupant, kState) = static_cast<uint8_t>(c.rotation & 1);
        Field<uint8_t>(g_occupant, kState + 1) = 0;
        Field<int32_t>(g_preview, kState) = 1;
        At<void*>(self, tool::kTexture) = c.texture ? &g_binding : nullptr;
        At<uint32_t>(self, tool::kPictureSize) = c.pictureSize;
        std::initializer_list<Fake*> none{};
        SetModels(tool::kBlue, none);
        SetModels(tool::kRed, none);
        if (!(c.shown & 0xFF)) {
            // Models in both vectors to hide and let go.
            for (std::size_t at : {tool::kBlue, tool::kRed}) {
                void** const b = At<void**>(self, at);
                for (int i = 0; i < 2; ++i) {
                    Fake* const m = &g_models[g_nextModel++];
                    ++Count(*m);
                    Field<uint8_t>(m, kState) = 1;
                    b[i] = m;
                }
                At<void**>(self, at + 4) = b + 2;
            }
        }
        g_tintStackKnown = c.stackKnown != 0;
        Game<void>(addr::cSC4ViewInputControlPlaceLot_UpdatePreview, &g_tool, c.allowed, c.shown,
                   static_cast<const float*>(c.point));
        g_tintStackKnown = false;
        for (std::size_t at : {tool::kFootprint, tool::kArrow}) {
            one.Add(Which(At<void*>(self, at)));
            if (At<void*>(self, at))
                one.Add(Field<uint8_t>(At<Fake*>(self, at), kState));
        }
        one.Add(Field<uint8_t>(g_holderModel, kState));
        one.Add(Field<uint8_t>(g_effect, kState));
        HashModels(one, tool::kBlue);
        HashModels(one, tool::kRed);
        for (Fake& m : g_models)
            one.Add(Count(m));
        f.Add(one.h);
        CaseDone("preview", static_cast<int>(&c - kCases), one);
    }
    f.Add(g_unexpected);
    g_log = nullptr;
    log::Write("autotest: plop4: preview %08X", f.h);
    out.Add(f.h);
}

// --- 3. BuildPreview ---------------------------------------------------------------------

// A game string in the tool (cRZBaseString, 0x14 bytes) with an empty text.
void StartString(std::size_t at)
{
    char* const self = reinterpret_cast<char*>(&g_tool);
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    buffer[0] = 0;
    At<std::uintptr_t>(self, at) = addr::SC4String_Vtable;
    At<char*>(self, at + 4) = buffer;
    At<char*>(self, at + 8) = buffer;
    At<char*>(self, at + 0xC) = buffer + 8;
    At<uint32_t>(self, at + 0x10) = 0;
}

void HashString(Fnv& f, std::size_t at)
{
    char* const self = reinterpret_cast<char*>(&g_tool);
    const char* const begin = At<const char*>(self, at + 4);
    const char* const end = At<const char*>(self, at + 8);
    const std::size_t length = begin && end >= begin ? static_cast<std::size_t>(end - begin) : 0;
    f.Add(static_cast<uint32_t>(length));
    if (length < 256)
        f.Bytes(begin, length);
    if (begin)
        Cdecl<void>(addr::SC4_Free, const_cast<char*>(begin));
}

void PartBuild(Fnv& out)
{
    Fnv f;
    g_log = &f;
    g_realFramework = Global<void*>(addr::GZFrameWork_Instance);
    Swap terrain(addr::SC4_Terrain, &g_terrain);
    Swap models(addr::SC4_Service43D1C, &g_modelService);
    Swap props(addr::SC4_Service43D10, &g_props);
    Swap networks(addr::SC4_NetworkService, &g_networks);
    Swap police(addr::SC4_PoliceSimulator, &g_police);
    Swap fire(addr::SC4_Service43D78, &g_fire);
    Swap others(addr::SC4_Service43DA4, &g_others);
    Swap counts(addr::SC4_Service43DA8, &g_counts);
    Swap buffers(addr::SC4_Service43C9C, &g_bufferService);
    Swap framework(addr::GZFrameWork_Instance, &g_framework);
    // The configuration's info: a turn byte, three floats, the building IDs.
    static uint32_t ids[4] = {9, 2, 11, 9};
    char* const info = &Field<char>(g_config, kState + 0x40);
    info[2] = 3;
    const float corner[3] = {24.5f, 1.25f, Float(0x41C80001u)};
    std::memcpy(info + 4, corner, sizeof corner);
    // Prop types of the records.
    static uint32_t types[6] = {0x5100, 0x5101, 0x5150, 0x5151, 0x5102, 0x5103};
    std::memset(g_records, 0, sizeof g_records);
    auto record = [](int i, uint8_t kind, uint8_t flags, uint8_t turn, uint32_t* begin, int count) {
        Record& r = g_records[i];
        r.kind = kind;
        r.flags = flags;
        r.turn = turn;
        r.a = 3.5f + static_cast<float>(i);
        r.b = Float(0x40490FDBu);
        r.c = -7.25f * static_cast<float>(i);
        r.typesBegin = begin;
        r.typesEnd = begin + count;
        r.typesCap = begin + count;
    };
    record(0, 1, 0, 2, &types[0], 1);  // drawn
    record(1, 1, 1, 0, &types[1], 1);  // flag bit 1: skipped
    record(2, 1, 0, 1, &types[0], 2);  // two types: skipped
    record(3, 2, 0, 0, &types[1], 1);  // another kind
    record(4, 1, 0, 3, &types[2], 1);  // not known
    record(5, 1, 0, 0, &types[3], 1);  // not made
    record(6, 1, 0, 1, &types[4], 1);  // not drawn
    record(7, 1, 2, 7, &types[5], 1);  // drawn
    record(8, 1, 0, 0, &types[0], 0);  // no types
    struct Case {
        uint8_t noConfig;
        uint8_t havePreview; // the preview and the picture there before
        uint8_t noInfo;
        int idCount;
        uint8_t noKey;
        uint8_t noPreview; // the prop manager refuses it
        uint8_t police, fire;       // the services cover; | 2: refuse the radius
        uint32_t health, education; // the properties' presence
        uint32_t once, onceFlag, count;
        uint32_t kind; // the occupant's slot 22 answer
        int records;
        uint8_t propsUnknown, noProps;
        uint32_t drawn6;   // record 6's drawn property
        uint32_t width, depth;
        float cell;
        uint8_t noBuffers, noInit, noPaint, noData;
    };
    static const Case kCases[] = {
        {1, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 3, 16.0f, 0, 0, 0, 0},
        // The preview and picture there: only the records.
        {0, 1, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 9, 0, 0, 0, 2, 3, 16.0f, 0, 0, 0, 0},
        {0, 1, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 9, 1, 0, 1, 2, 3, 16.0f, 0, 0, 0, 0},
        {0, 1, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 9, 0, 1, 1, 2, 3, 1.7f, 0, 0, 0, 0},
        // The preview made, stopping short each way.
        {0, 0, 1, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 16.0f, 0, 0, 0, 0},
        {0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 16.0f, 0, 0, 0, 0},
        {0, 0, 0, 4, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 16.0f, 0, 0, 0, 0},
        {0, 0, 0, 4, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 16.0f, 0, 0, 0, 0},
        // Made, with the services and the placement count's properties.
        {0, 0, 0, 4, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0x1401, 0, 0, 0, 0, 3, 3, 16.0f, 0, 0, 0, 0},
        {0, 0, 0, 4, 0, 0, 3, 1, 1, 0, 7, 0, 0, 0x1302, 0, 0, 0, 0, 4, 2, 16.0f, 0, 0, 0, 0},
        {0, 0, 0, 4, 0, 0, 0, 3, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 5, 5, 16.0f, 0, 0, 0, 0},
        {0, 0, 0, 4, 0, 0, 1, 1, 1, 1, 0, 0, 5, 0x1401, 9, 0, 0, 1, 1, 6, 16.0f, 0, 0, 0, 0},
        // The picture over its sizes and refusals.
        {0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 2, 8.0f, 0, 0, 0, 0},
        {0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 1, 16.0f, 0, 0, 0, 0},
        {0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16, 9, 16.0f, 0, 0, 0, 0},
        {0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 25, 3, 16.0f, 0, 0, 0, 0},
        {0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 3, 1.7f, 0, 0, 0, 0},
        {0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 3, 16.0f, 1, 0, 0, 0},
        {0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 3, 16.0f, 0, 1, 0, 0},
        {0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 3, 16.0f, 0, 0, 1, 0},
        {0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 3, 16.0f, 0, 0, 0, 1},
    };
    int caseIndex = 0;
    for (const Case& c : kCases) {
        Fnv one;
        g_log = &one;
        FreshTool();
        Wire();
        char* const self = reinterpret_cast<char*>(&g_tool);
        StartString(tool::kInactive);
        StartString(tool::kPlop);
        At<float>(self, tool::kPoint) = 40.0f;
        At<float>(self, tool::kPoint + 4) = 2.0f;
        At<float>(self, tool::kPoint + 8) = -12.5f;
        At<uint32_t>(self, tool::kRotation) = static_cast<uint32_t>(caseIndex & 3);
        At<uint32_t>(self, tool::kRadius) = 0x3F800000u;
        At<uint32_t>(self, tool::kPlacementsLeft) = 77;
        At<uint32_t>(self, tool::kCursor) = 5;
        At<uint32_t>(self, tool::kServices) = 0xF0;
        if (c.havePreview) {
            ++Count(g_preview);
            At<void*>(self, tool::kPreview) = &g_preview;
            At<void*>(self, tool::kTexture) = &g_binding;
        }
        Field<float>(g_terrain, kState) = c.cell;
        Field<uint8_t>(g_configs, kState) = c.noConfig;
        Field<uint8_t>(g_config, kState) = 0;
        Field<uint32_t>(g_config, kState + 4) = c.width;
        Field<uint32_t>(g_config, kState + 8) = c.depth;
        Field<uint8_t>(g_config, kState + 0x20) = c.noInfo;
        char* const block = &Field<char>(g_config, kState + 0x40);
        uint32_t* const idsAt = ids;
        uint32_t* const idsEnd = ids + c.idCount;
        std::memcpy(block + 0x28, &idsAt, sizeof idsAt);
        std::memcpy(block + 0x2C, &idsEnd, sizeof idsEnd);
        SetRecords(c.records);
        Field<uint8_t>(g_development, kState) = 0;
        Field<uint8_t>(g_development, kState + 1) = c.noKey;
        Field<uint8_t>(g_props, kState) = c.propsUnknown;
        Field<uint8_t>(g_props, kState + 1) = c.noProps;
        Field<uint8_t>(g_props, kState + 2) = c.noPreview;
        Field<uint8_t>(g_occupant, kState) = 1;
        Field<uint8_t>(g_occupant, kState + 1) = 1; // no parts
        Field<uint32_t>(g_occupant, kState + 4) = c.kind;
        Field<uint8_t>(g_propOccupant, kState) = 0;
        Field<uint8_t>(g_police, kState) = c.police & 1;
        Field<uint8_t>(g_police, kState + 1) = (c.police & 2) != 0;
        Field<float>(g_police, kState + 4) = 40.0f;
        Field<uint8_t>(g_fire, kState) = c.fire & 1;
        Field<uint8_t>(g_fire, kState + 1) = (c.fire & 2) != 0;
        Field<float>(g_fire, kState + 4) = 52.5f;
        Field<uint8_t>(g_others, kState + 1) = 0;
        Field<float>(g_others, kState + 4) = 33.3f;
        g_valueCount = 0;
        auto value = [](uint32_t id, uint32_t v, bool present) {
            if (present)
                g_values[g_valueCount++] = {id, v};
        };
        value(0x091B42A2u, 1, c.health != 0);
        value(0x691B42B3u, 1, c.education != 0);
        value(0xEA2E078Bu, c.once, c.once != 0);
        value(0xEA3209F8u, c.onceFlag, true);
        value(0x0BD86FD3u, c.count, c.count != 0);
        // The drawn property, for every prop record.
        value(0x4A89FCF3u, c.drawn6 ? 1u : 0u, true);
        Field<uint8_t>(g_bufferService, kState) = c.noBuffers;
        Field<uint8_t>(g_buffer, kState) = c.noInit;
        Field<uint8_t>(g_buffer, kState + 1) = c.noPaint;
        Field<uint8_t>(g_buffer, kState + 2) = c.noData;
        // The game calls the factory without testing that it got one, so
        // the framework always gives it here.
        Field<uint8_t>(g_framework, kState) = 0;
        std::memset(g_markers, 0, sizeof g_markers);
        log::Write("autotest: plop4: build case %d starts", caseIndex);
        Game<void>(addr::cSC4ViewInputControlPlaceLot_BuildPreview, &g_tool);
        one.Add(Which(At<void*>(self, tool::kPreview)));
        one.Add(Which(At<void*>(self, tool::kTexture)));
        for (std::size_t at : {tool::kPictureSize, tool::kOffsetA, tool::kOffsetA + 4, tool::kOffsetA + 8,
                               tool::kRadiusOut, tool::kRadius, tool::kServices, tool::kCursor, tool::kPlacementsLeft})
            one.Add(At<uint32_t>(self, at));
        one.Add(At<uint8_t>(self, tool::kTurnOffset));
        one.Add(At<uint32_t>(g_markers, 0x14));
        HashString(one, tool::kInactive);
        HashString(one, tool::kPlop);
        // The prop records: turn, the floats, which holder.
        char* const begin = At<char*>(self, tool::kRecords);
        char* const end = At<char*>(self, tool::kRecords + 4);
        one.Add(static_cast<uint32_t>(end - begin));
        for (char* r = begin; r && r != end; r += 0x14) {
            one.Add(static_cast<uint8_t>(r[0]));
            for (std::size_t i = 4; i < 0x10; i += 4)
                one.Add(At<uint32_t>(r, i));
            one.Add(Which(At<void*>(r, 0x10)));
        }
        if (begin)
            Cdecl<void>(addr::SC4_Free, static_cast<void*>(begin));
        for (Fake* counted : {&g_preview, &g_propHolder, &g_buffer, &g_factory})
            one.Add(Count(*counted));
        f.Add(one.h);
        CaseDone("build", caseIndex, one);
        ++caseIndex;
    }
    f.Add(g_unexpected);
    g_log = nullptr;
    log::Write("autotest: plop4: build %08X", f.h);
    out.Add(f.h);
}

// --- 4. A tool the view made -------------------------------------------------------------

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

// The tool as the city view makes it (as tool_plop_autotest.cpp does).
char* MakeTool(char* view, uint32_t configuration)
{
    void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x158});
    void* const made = memory ? Game<void*>(addr::SC4ViewInputControls_F4C0430, memory, uint32_t{0}, configuration,
                                            uint32_t{0}, uint32_t{0})
                              : nullptr;
    if (!made)
        return nullptr;
    void* held = nullptr;
    Game<void*>(addr::SC4RefHolder_SC4RefHolder, &held, made);
    Virtual<uint8_t>(view, 0x64, held, int32_t{2});
    Virtual<uint32_t>(held, 0x8);
    return Virtual<char*>(view, 0x60);
}

// The fields BuildPreview and UpdatePreview leave: no pointers.
void HashTool(Fnv& f, char* t)
{
    for (std::size_t at : {tool::kPictureSize, tool::kOffsetA, tool::kOffsetA + 4, tool::kOffsetA + 8,
                           tool::kRadiusOut, tool::kRadius, tool::kServices, tool::kCursor, tool::kPlacementsLeft})
        f.Add(At<uint32_t>(t, at));
    f.Add(At<uint8_t>(t, tool::kTurnOffset));
    for (std::size_t at : {tool::kEffect, tool::kFootprint, tool::kArrow, tool::kPreview, tool::kTexture})
        f.Add(uint8_t{At<void*>(t, at) != nullptr});
    for (std::size_t at : {tool::kBlue, tool::kRed})
        f.Add(static_cast<uint32_t>(At<void**>(t, at + 4) - At<void**>(t, at)));
    f.Add(static_cast<uint32_t>(At<char*>(t, tool::kRecords + 4) - At<char*>(t, tool::kRecords)));
    for (std::size_t at : {tool::kInactive, tool::kPlop}) {
        const char* const begin = At<const char*>(t, at + 4);
        const char* const end = At<const char*>(t, at + 8);
        const std::size_t length = begin && end >= begin && end - begin < 256 ? static_cast<std::size_t>(end - begin) : 0;
        f.Add(static_cast<uint32_t>(length));
        f.Bytes(begin, length);
    }
}

void PartReal(Fnv& out)
{
    Fnv f;
    char* const view = CityView();
    void* const city = Global<void*>(addr::SC4_City);
    void* const manager = Global<void*>(addr::SC4_Service43D18);
    void* const terrain = Global<void*>(addr::SC4_Terrain);
    if (!view || !city || !manager || !terrain) {
        log::Write("autotest: plop4: real: no city view");
        out.Add(uint32_t{0xDEAD});
        return;
    }
    const int32_t sizeX = Virtual<int32_t>(city, 0x174), sizeZ = Virtual<int32_t>(city, 0x178);
    cISC4LotManager* const lotManager = static_cast<cISC4City*>(city)->GetLotManager();
    uint32_t configurationId = 0;
    for (int32_t z = 0; z < sizeZ && !configurationId; ++z)
        for (int32_t x = 0; x < sizeX && !configurationId; ++x) {
            cISC4Lot* const lot = lotManager ? lotManager->GetLot(x, z, false) : nullptr;
            cISC4LotConfiguration* const configuration = lot ? lot->GetLotConfiguration() : nullptr;
            if (!configuration)
                continue;
            const uint32_t id = configuration->GetID();
            uint32_t w = 0, d = 0;
            configuration->GetSize(w, d);
            if (id && w >= 1 && d >= 1 && w <= 6 && d <= 6 && Virtual<void*>(manager, 0x20, id) == configuration)
                configurationId = id;
        }
    if (!configurationId) {
        log::Write("autotest: plop4: real: no lot configuration found");
        out.Add(uint32_t{0xC0FF});
        return;
    }
    if (Virtual<void*>(view, 0x60)) {
        log::Write("autotest: plop4: real: the view has a tool already");
        out.Add(uint32_t{0xBEEF});
        return;
    }
    char* const t = MakeTool(view, configurationId);
    if (!t) {
        log::Write("autotest: plop4: real: the view did not take the tool");
        out.Add(uint32_t{0xFEED});
        return;
    }
    f.Add(configurationId);
    HashTool(f, t);
    const double cell = Virtual<double>(terrain, 0x18);
    const float point[3] = {static_cast<float>(sizeX * cell * 0.5 + 3.25), 0.0f,
                            static_cast<float>(sizeZ * cell * 0.5 - 5.5)};
    float place[3] = {point[0], Virtual<float>(terrain, 0x34, point[0], point[2]), point[2]};
    for (int32_t rotation = 0; rotation < 4; ++rotation) {
        At<int32_t>(t, tool::kRotation) = rotation;
        const uint32_t allowed = static_cast<uint32_t>(rotation & 1);
        Game<void>(addr::cSC4ViewInputControlPlaceLot_UpdatePreview, t, allowed, uint32_t{1},
                   static_cast<const float*>(place));
        At<uint8_t>(t, tool::kAllowed) = static_cast<uint8_t>(allowed);
        HashTool(f, t);
        place[0] += 16.0f;
    }
    Game<void>(addr::cSC4ViewInputControlPlaceLot_UpdatePreview, t, uint32_t{0}, uint32_t{0},
               static_cast<const float*>(place));
    HashTool(f, t);
    At<int32_t>(t, tool::kRotation) = 0;
    Virtual<void>(view, 0x68, uint32_t{0});
    f.Add(uint8_t{Virtual<void*>(view, 0x60) != nullptr});
    log::Write("autotest: plop4: real %08X (configuration %08X)", f.h, configurationId);
    out.Add(f.h);
}

void Drive(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_T11") || !game::IsBuild641())
        return;
    if (!Global<void*>(addr::SC4_City)) {
        log::Write("autotest: plop4: no city");
        return;
    }
    const int parts = EnvInt("OPENSC4_TEST_T11_PARTS", 15);
    g_trace = EnvInt("OPENSC4_TEST_T11_TRACE", 0) != 0;
    SetUpFakes();
    g_unexpected = 0;
    Fnv total;
    if (parts & 1)
        PartFootprint(total);
    if (parts & 2)
        PartPreview(total);
    if (parts & 4)
        PartBuild(total);
    if (parts & 8)
        PartReal(total);
    total.Add(g_unexpected);
    std::snprintf(g_token, sizeof g_token, " plop4 %08X", total.h);
    log::Write("autotest: plop4: total %08X (parts %d, unexpected slots %d)", total.h, parts, g_unexpected);
}

const DayHook kPlop4{"plop4", 72, &Drive};

void Fingerprint(cISC4City*, std::string& line)
{
    line += g_token;
}

const FingerprintPart kFingerprint{"plop4", 34, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
