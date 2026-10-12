// Our cSTETerrain brush loader (build 641), piece 6 of the city's terrain
// object: what fills the brush map 0x00B4BD58 that slot 57 (ApplyBrush) and
// the brush getters (slots 58, 61 to 63) read.
//
//   0x0074FDC0  LoadBrushes        empties the map, lists the brush
//                                  exemplars, reads each
//   0x0074FC70  LoadBrush          one exemplar into a new record of the map
//   0x00747EC0  ReadBrushBasics    the turn, the costs, two IDs
//   0x0074F4A0  ReadBrushLayers    up to four layers
//   0x00747DE0  ReadBrushCursors   four ID pairs and two IDs with defaults
//   0x00747CE0  PropertyPair       a property of exactly two uint32
//
// Where it runs. The terrain's Init (slot 3) calls 0x0074FE40 while a city
// loads; that function does its work once (the byte 0x00B4BC3C; a callback
// it registers with the application clears the byte and the map again, so
// a later Init repeats it): it keeps the resource manager (0x00B4BC40) and
// calls four loaders, of which this is the last:
//   0x0074F410  exemplars of type 0x6534284A, group 0x89C2A517 into the
//               table 0x00B4BCDC (0x0074F180 per exemplar);
//   0x0074DC30  the INI resource of group 0x8A5971C5, instance 0xAA597172
//               (sections TextureMapTable, MiscTerrainTunables, WindErosion,
//               Hydrology ...; with 0x0074C800 and an FPTAN at 0x0074E1DA);
//   0x0074B6A0  the tuning exemplar (group 0x88CD66E9, instance 1,
//               properties 0x48CD7B20 and on) and 0x0074B460, which fills
//               tables with draws from the terrain's generator;
//   0x0074FDC0  the brushes.
// The first three stay the game's (they are not the brushes'; see the
// report). A detour installed at PostAppInit is in place before the first
// city loads, so the brushes are read by this code from the first time on.
//
// The brushes: every exemplar of type 0x6534284A, group 0x6A01FC2A the
// resource manager lists (its slot 17 with a key filter; the list's slot 7
// calls LoadBrush per key, in the list's order). The record's key in the
// map is the exemplar's instance.
//
// Properties (ID, type, where it goes in the record; "layer" = base
// 0x8A020202 + 0x100 x n for n = 0 to 3):
//   0x8A020102  float     the turn step in degrees (0.5 without it); x the
//                         float 0x00AA8C60 (pi / 180) to +0x8; the brush
//                         turns (+0x4) when it is not 0
//   0x8A020101  float[5]  +0x24 to +0x34 (zeros without it)
//   0x8A020103  uint32    +0x60 (0 without it)
//   0x8A020104  uint32    +0x64 (0 without it)
//   layer + 3   uint32    the layer's kind; without it there is no layer n;
//                         12 or more: the brush is dropped
//   layer       float[]   the widths per step (required; the brush is
//                         dropped without); the record's +0xC keeps the
//                         greatest of each of the first five over the layers
//   layer + 1   float[]   the strengths per step (without it layer n is
//                         left out, but still counts as found)
//   layer - 1   bool      the layer follows the stroke's direction
//   layer - 2   uint32    the picture's instance      (kinds 0-5, 7-9, 11)
//   layer + 2   uint8     the neutral picture value   (kinds 0-5, 7-9, 11)
//   layer + 4   uint32    the decal's instance        (kind 3)
//   layer + 5   float     the greatest turn, x the float 0x00AB415C
//                         (kind 3; 2 pi for the other kinds)
//   layer + 6   uint32    the scrub count limit       (kinds 3, 4)
//   layer + 7   uint32[2] the effect                  (kind 4)
//   (which of the last six a kind needs: the table 0x00AB4160; a needed one
//   that is missing drops the brush)
//   0xAA2ED800 to 0xAA2ED803  uint32[2]  +0x38, +0x40, +0x48, +0x50
//                         (without: 0xEA3169E6 and 2, 1, 4, 3)
//   0xAA2ED804, 0xAA2ED805  uint32  +0x58, +0x5C (without: 0x071E5700,
//                         0x071E5705)
// A brush without any layer is dropped too.
//
// Random numbers: a brush that turns starts at an angle drawn between 0 and
// 2 pi (the double 0x00A96018) from the terrain's generator 0x00B4BC78: one
// uniform draw per such brush, in the order the resource manager lists the
// exemplars.
//
// Still the game's and called at their addresses: emptying the map
// (0x0074FB60, which also releases the layers' pictures; the application's
// callback 0x0074FC20 calls it too), the map's insert and erase (0x0074FAA0,
// 0x0074FA40), the record's copy and the two destructors the insert needs
// (0x0074D4F0, 0x0074DBF0, 0x0074DBB0), the layer vector's growth and a
// layer's copy (0x0074EFC0, 0x00749040), the float vector's resize, the
// property getters, the key filter, operator new, and every virtual call.
// The record the game builds on its stack before the insert is zero here
// where the game leaves stack memory (every such field is written before
// the record is used or the record is erased).
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/terrain_sim.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::terrain_sim {

// --- Additions for terrain_sim.h (to be merged there) --------------------------

// A brush record (0x70 bytes; the value of a node of the map 0x00B4BD58, at
// node + 0x14, the key at node + 0x10). The body's fields are in
// terrain_brushbody.cpp; these are the ones the loader fills besides.
namespace brush_record {
constexpr std::size_t kID = 0x0;         // uint32: the exemplar's instance
constexpr std::size_t kTurns = 0x4;      // uint8
constexpr std::size_t kTurnStep = 0x8;   // float, radians
constexpr std::size_t kMaxWidths = 0xC;  // vector<float> of 5: the greatest layer width per step
constexpr std::size_t kLayers = 0x18;    // vector of 0x4C-byte layers
constexpr std::size_t kCosts = 0x24;     // float[5]: property 0x8A020101
constexpr std::size_t kCursors = 0x38;   // uint32[4][2]: properties 0xAA2ED800 to 0xAA2ED803
constexpr std::size_t kCursorID1 = 0x58; // uint32: property 0xAA2ED804
constexpr std::size_t kCursorID2 = 0x5C; // uint32: property 0xAA2ED805
constexpr std::size_t kID3 = 0x60;       // uint32: property 0x8A020103
constexpr std::size_t kID4 = 0x64;       // uint32: property 0x8A020104
constexpr std::size_t kTurn = 0x68;      // float
constexpr std::size_t kDirection = 0x6C; // float
constexpr std::size_t kSize = 0x70;
} // namespace brush_record

namespace {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("terrain: first %s", name);
}

template <typename R, typename... Args>
inline R This(std::uintptr_t function, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(function)(self, args...);
}

template <typename R, typename... Args>
inline R Cdecl(std::uintptr_t function, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(function)(args...);
}

// A std::vector of the game (begin, end, capacity).
struct GameVector {
    uint32_t start, finish, capacity;
};

// A layer as the loader builds it on its stack (0x4C bytes) before it is
// copied to the end of the record's vector.
struct LayerData {
    uint32_t pictureID;
    uint32_t kind;
    uint32_t picture;
    uint8_t follows;
    uint8_t pad1[3];
    GameVector widths;
    GameVector strengths;
    uint8_t neutral;
    uint8_t pad2[3];
    uint32_t maxCount;
    uint32_t textureID;
    float maxTurn;
    uint32_t effect[2];
    GameVector third; // never filled
};
static_assert(sizeof(LayerData) == 0x4C);
static_assert(offsetof(LayerData, neutral) == 0x28);
static_assert(offsetof(LayerData, third) == 0x40);

inline bool GetFloat(void* exemplar, uint32_t property, float* out)
{
    return (Cdecl<uint32_t>(addr::SC4_GetPropertyFloat, exemplar, property, out) & 0xFFu) != 0;
}

inline bool GetUint32(void* exemplar, uint32_t property, uint32_t* out)
{
    return (Cdecl<uint32_t>(addr::SC4_GetPropertyUint32, exemplar, property, out) & 0xFFu) != 0;
}

inline bool GetUint8(void* exemplar, uint32_t property, uint8_t* out)
{
    return (Cdecl<uint32_t>(addr::SC4_GetPropertyUint8, exemplar, property, out) & 0xFFu) != 0;
}

inline bool GetFloats(void* exemplar, uint32_t property, GameVector* out)
{
    return (Cdecl<uint32_t>(addr::SC4_GetPropertyFloatVector, exemplar, property, out) & 0xFFu) != 0;
}

// What the game does with a layer it does not keep, and after it copied
// one: the three arrays freed, the third first (0x0074F5C3, 0x0074F644,
// 0x00747DA0, 0x0074F794: the same three calls).
inline void FreeLayer(LayerData* layer)
{
    if (layer->third.start)
        Cdecl<void>(addr::SC4_Free, layer->third.start);
    if (layer->strengths.start)
        Cdecl<void>(addr::SC4_Free, layer->strengths.start);
    if (layer->widths.start)
        Cdecl<void>(addr::SC4_Free, layer->widths.start);
}

// 0x00747CE0 (exemplar, property, out): a uint32 array property of exactly
// two values (its byte length less the low two bits is 8) into out[0] and
// out[1]; false, with out untouched, for anything else.
bool PropertyPair(void* exemplar, uint32_t property, uint32_t* out)
{
    GameVector values{0, 0, 0};
    const bool found = (Cdecl<uint32_t>(addr::SC4_GetPropertyUint32Vector, exemplar, property, &values) & 0xFFu) != 0;
    if (!found || ((values.finish - values.start) & 0xFFFFFFFCu) != 8u) {
        if (values.start)
            Cdecl<void>(addr::SC4_Free, values.start);
        return false;
    }
    out[0] = AtAddress<uint32_t>(values.start);
    out[1] = AtAddress<uint32_t>(values.start + 4u);
    Cdecl<void>(addr::SC4_Free, values.start);
    return true;
}

// 0x00747EC0 (exemplar, record): the turn step (property 0x8A020102, 0.5
// without it); the brush turns when that is not 0 (a NaN turns); the step
// x pi / 180 (rounded to float). A brush that turns starts at an angle
// drawn between 0 and 2 pi; the others at 0. The direction 0. The five
// costs (property 0x8A020101: the first five floats of the array, whatever
// its length; zeros without it), the two IDs. Answers true always.
bool ReadBrushBasics(void* exemplar, char* record)
{
    float* const step = &At<float>(record, brush_record::kTurnStep);
    if (!GetFloat(exemplar, 0x8A020102u, step))
        AtAddress<uint32_t>(static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(step))) = 0x3F000000u;
    const bool turns = !(*step == Global<float>(addr::SC4_FloatZero));
    *step = static_cast<float>(static_cast<double>(*step) * static_cast<double>(Global<float>(addr::SC4_FloatDegree)));
    At<uint8_t>(record, brush_record::kTurns) = turns ? 1 : 0;
    if (turns)
        At<float>(record, brush_record::kTurn) =
            static_cast<float>(This<double>(addr::cRZRandom_Uniform, reinterpret_cast<void*>(addr::cSTETerrain_Random),
                                            Global<double>(addr::SC4_DoubleZero), Global<double>(addr::SC4_DoubleTwoPiSingle)));
    else
        At<uint32_t>(record, brush_record::kTurn) = 0;
    At<uint32_t>(record, brush_record::kDirection) = 0;
    GameVector costs{0, 0, 0};
    if (GetFloats(exemplar, 0x8A020101u, &costs)) {
        for (uint32_t i = 0; i < 5; ++i)
            At<uint32_t>(record, brush_record::kCosts + 4 * i) = AtAddress<uint32_t>(costs.start + 4u * i);
    } else {
        for (uint32_t i = 0; i < 5; ++i)
            At<uint32_t>(record, brush_record::kCosts + 4 * i) = 0;
    }
    if (!GetUint32(exemplar, 0x8A020103u, &At<uint32_t>(record, brush_record::kID3)))
        At<uint32_t>(record, brush_record::kID3) = 0;
    if (!GetUint32(exemplar, 0x8A020104u, &At<uint32_t>(record, brush_record::kID4)))
        At<uint32_t>(record, brush_record::kID4) = 0;
    if (costs.start)
        Cdecl<void>(addr::SC4_Free, costs.start);
    return true;
}

// 0x0074F4A0 (exemplar, record): the layers, as the list at the top of the
// file says. Answers whether any layer's kind property was found (also when
// that layer was then left out for want of strengths); false at once for a
// kind of 12 or more, missing widths or a missing property the kind needs.
// The widths' first five values are read whatever the array's length.
bool ReadBrushLayers(void* exemplar, char* record)
{
    int32_t found = 0;
    uint32_t base = 0x8A020202u;
    for (int32_t n = 0; n < 4; ++n, base += 0x100u) {
        LayerData layer{};
        if (!GetUint32(exemplar, base + 3u, &layer.kind)) {
            FreeLayer(&layer);
            continue;
        }
        ++found;
        if (layer.kind >= 0xCu || !GetFloats(exemplar, base, &layer.widths)) {
            FreeLayer(&layer);
            return false;
        }
        for (uint32_t i = 0; i < 5; ++i) {
            const uint32_t width = layer.widths.start + 4u * i;
            const uint32_t greatest = At<uint32_t>(record, brush_record::kMaxWidths) + 4u * i;
            if (AtAddress<float>(width) > AtAddress<float>(greatest))
                AtAddress<uint32_t>(greatest) = AtAddress<uint32_t>(width);
        }
        if (!GetFloats(exemplar, base + 1u, &layer.strengths)) {
            FreeLayer(&layer);
            continue;
        }
        layer.follows = 0;
        Cdecl<uint32_t>(addr::SC4_GetPropertyBool, exemplar, base - 1u, &layer.follows);
        const uint8_t* const needs = &Global<uint8_t>(addr::cSTETerrain_BrushKindProperties + 6u * layer.kind);
        if (needs[0]) {
            if (!GetUint32(exemplar, base - 2u, &layer.pictureID)) {
                FreeLayer(&layer);
                return false;
            }
        } else {
            layer.pictureID = 0;
        }
        layer.picture = 0;
        if (needs[1]) {
            if (!GetUint8(exemplar, base + 2u, &layer.neutral)) {
                FreeLayer(&layer);
                return false;
            }
        } else {
            layer.neutral = 0;
        }
        if (needs[2]) {
            if (!GetUint32(exemplar, base + 4u, &layer.textureID)) {
                FreeLayer(&layer);
                return false;
            }
        } else {
            layer.textureID = 0;
        }
        if (needs[3]) {
            if (!GetFloat(exemplar, base + 5u, &layer.maxTurn)) {
                FreeLayer(&layer);
                return false;
            }
            layer.maxTurn = static_cast<float>(static_cast<double>(layer.maxTurn) *
                                               static_cast<double>(Global<float>(addr::cSTETerrain_BrushTurnScale)));
        } else {
            AtAddress<uint32_t>(static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(&layer.maxTurn))) = 0x40C90FDBu;
        }
        if (needs[4]) {
            if (!GetUint32(exemplar, base + 6u, &layer.maxCount)) {
                FreeLayer(&layer);
                return false;
            }
        } else {
            layer.maxCount = 0;
        }
        if (needs[5]) {
            if (!PropertyPair(exemplar, base + 7u, layer.effect)) {
                FreeLayer(&layer);
                return false;
            }
        } else {
            layer.effect[0] = 0;
            layer.effect[1] = 0;
        }
        // push_back as inlined at 0x0074F759.
        GameVector* const layers = &At<GameVector>(record, brush_record::kLayers);
        const uint32_t end = layers->finish;
        if (end != layers->capacity) {
            if (end)
                This<void*>(addr::cSTETerrain_BrushLayerCopy, reinterpret_cast<void*>(static_cast<std::uintptr_t>(end)),
                            static_cast<void*>(&layer));
            layers->finish += 0x4Cu;
        } else {
            uint8_t tag = 0;
            This<void>(addr::cSTETerrain_BrushLayersInsertN, layers, end, static_cast<void*>(&layer), &tag, 1u, 1u);
        }
        FreeLayer(&layer);
    }
    return found > 0;
}

// 0x00747DE0 (exemplar, record): the four pairs and two IDs with their
// defaults. Answers true always.
bool ReadBrushCursors(void* exemplar, char* record)
{
    static const uint32_t kSecond[4] = {2, 1, 4, 3};
    for (uint32_t i = 0; i < 4; ++i) {
        uint32_t* const pair = &At<uint32_t>(record, brush_record::kCursors + 8 * i);
        if (!PropertyPair(exemplar, 0xAA2ED800u + i, pair)) {
            pair[0] = 0xEA3169E6u;
            pair[1] = kSecond[i];
        }
    }
    if (!GetUint32(exemplar, 0xAA2ED804u, &At<uint32_t>(record, brush_record::kCursorID1)))
        At<uint32_t>(record, brush_record::kCursorID1) = 0x071E5700u;
    if (!GetUint32(exemplar, 0xAA2ED805u, &At<uint32_t>(record, brush_record::kCursorID2)))
        At<uint32_t>(record, brush_record::kCursorID2) = 0x071E5705u;
    return true;
}

// 0x0074FC70 (key, context; cdecl, the key list's callback): nothing for an
// instance the map already has or an exemplar the resource manager does not
// give (interface 0xA52160F5). Else a record with empty vectors is inserted
// under the instance (the game's copy, insert and destructors), its greatest
// widths become five zeros, its ID the instance, and the three readers fill
// it; when one of them fails the record is erased again. The exemplar is
// released.
void __cdecl LoadBrush(uint32_t* key, void*)
{
    void* const map = reinterpret_cast<void*>(addr::cSTETerrain_Brushes);
    uint32_t* const instance = key + 2;
    char* node = nullptr;
    This<void>(addr::SC4Uint32Map_Find, map, &node, instance);
    if (node != Global<char*>(addr::cSTETerrain_Brushes))
        return;
    void* resource = nullptr;
    if (!Virtual<uint8_t>(Global<void*>(addr::cSTETerrain_ResourceManager), 0xC, static_cast<void*>(key), 0xA52160F5u, &resource,
                          0u, 0u))
        return;
    struct {
        uint32_t key;
        uint8_t record[brush_record::kSize];
    } pair{};
    uint8_t empty[brush_record::kSize] = {};
    pair.key = *instance;
    This<void*>(addr::cSTETerrain_BrushRecordCopy, static_cast<void*>(pair.record), static_cast<void*>(empty));
    struct {
        char* node;
        uint32_t inserted; // the game's writes a byte
    } result{nullptr, 0};
    This<void>(addr::cSTETerrain_BrushMapInsert, map, static_cast<void*>(&result), static_cast<void*>(&pair));
    This<void>(addr::cSTETerrain_BrushPairDestruct, static_cast<void*>(&pair));
    This<void>(addr::cSTETerrain_BrushRecordDestruct, static_cast<void*>(empty));
    char* const record = result.node + 0x14;
    void* const exemplar = Virtual<void*>(resource, 0x1C);
    float zero = 0.0f;
    This<void>(addr::SC4FloatVector_Resize2, static_cast<void*>(record + brush_record::kMaxWidths), 5u, &zero);
    for (uint32_t i = 0; i < 5; ++i)
        AtAddress<uint32_t>(At<uint32_t>(record, brush_record::kMaxWidths) + 4u * i) = 0;
    At<uint32_t>(record, brush_record::kID) = *instance;
    if (!ReadBrushBasics(exemplar, record) || !ReadBrushLayers(exemplar, record) || !ReadBrushCursors(exemplar, record))
        This<uint32_t>(addr::cSTETerrain_BrushMapErase, map, instance);
    Virtual<uint32_t>(resource, 0x8);
}

// 0x0074FDC0 (no arguments): nothing without the terrain's resource manager.
// Else the map is emptied (0x0074FB60), a key filter for type 0x6534284A,
// group 0x6A01FC2A is made (0x14 bytes; the game goes on with a null one,
// and so would this), the resource manager's slot 17 lists the keys it lets
// through, and the list's slot 7 calls LoadBrush for each.
void __cdecl LoadBrushes()
{
    static bool logged = false;
    FirstCall("LoadBrushes", logged);
    if (!Global<void*>(addr::cSTETerrain_ResourceManager))
        return;
    Cdecl<void>(addr::cSTETerrain_ClearBrushes);
    void* list = nullptr;
    void* const memory = Cdecl<void*>(addr::SC4_operator_new, 0x14u);
    char* const filter = memory ? This<char*>(addr::SC4ResourceKeyFilter_ctor, memory, 0x6534284Au, 0x6A01FC2Au) : nullptr;
    Virtual<uint32_t>(filter, 0x4);
    if (Virtual<uint32_t>(Global<void*>(addr::cSTETerrain_ResourceManager), 0x44, &list, static_cast<void*>(filter + 8))) {
        Virtual<void>(list, 0x1C, reinterpret_cast<void*>(&LoadBrush), 0u);
        Virtual<uint32_t>(list, 0x8);
    }
    Virtual<uint32_t>(filter, 0x8);
}

// --- Installation ------------------------------------------------------------

void* g_original = nullptr;

// The one direct call site of 0x0074FDC0 is 0x0074FE89 in 0x0074FE40 (no
// arguments; MOV AL, 1 and RET follow). 0x0074FC70 is reached only through
// the pointer 0x0074FDC0 hands the key list (its address occurs nowhere else
// in the image), and 0x00747EC0, 0x0074F4A0 and 0x00747DE0 only from
// 0x0074FC70 (0x0074FD71, 0x0074FD7F, 0x0074FD8D); 0x00747CE0 from those two
// readers: plain functions here.
bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    if (!addr_check::Find(addr::cSTETerrain_LoadBrushes)) {
        log::Write("terrain brushload: no address entry for %08X; not installing",
                   static_cast<unsigned>(addr::cSTETerrain_LoadBrushes));
        return false;
    }
    if (!hook::Install(*addr_check::Find(addr::cSTETerrain_LoadBrushes), reinterpret_cast<void*>(&LoadBrushes), &g_original)) {
        log::Write("terrain brushload: the detour failed (%08X)", static_cast<unsigned>(addr::cSTETerrain_LoadBrushes));
        return false;
    }
    log::Write("terrain brushload: the brush loader %08X (detour) is ours%s",
               static_cast<unsigned>(addr::cSTETerrain_LoadBrushes),
               Global<uint8_t>(addr::cSTETerrain_StaticsDone) ? "; the game has loaded the brushes already" : "");
    return true;
}

const bool g_registered = registry::Register(
    {"simulators.terrain.brushload", "simulators/terrain", registry::Kind::Detour, registry::Phase::PostAppInit,
     addr::cSTETerrain_LoadBrushes,
     "Our cSTETerrain brush loader: the brush exemplars (type 0x6534284A, group 0x6A01FC2A) read into the brush map: "
     "turn, costs, up to four layers with their widths, strengths, pictures and per-kind properties, cursors, by a "
     "detour",
     false, Install});

} // namespace
} // namespace opensc4::terrain_sim
