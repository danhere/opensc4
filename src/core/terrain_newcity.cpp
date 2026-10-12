// Our cSTETerrain new-city terrain (build 641), piece 7 of the city's
// terrain object (vtable 0x00AB3D18, the service 0x00B43CF4): what Init
// (slot 3, terrain_object.cpp) calls when no terrain was loaded (+0x68
// clear), after the water object's Init. Group simulators.terrain.newcity.
//
//   0x00748130  SetUpNewTerrain      the three ways a new city gets its land
//   0x00740D60  NewTerrainFromImage  a height picture stretched over the grid
//   0x00742860  NewTerrainFromFile   the picture loaded from a file first
//   0x007429A0  ClampToMinHeight     nothing below the least allowed height
//
// SetUpNewTerrain (all from the disassembly, statement by statement). The
// application's new city specification (its slot 39) starts with a file
// name (a C string), has a picture at +0x200 and a city file segment at
// +0x204:
// 1. with a segment: the terrain is loaded from it (slot 95) and the water
//    depths reset over the whole map (0x0073D180);
// 2. with neither a name nor a picture: every vertex gets the region's base
//    terrain height x 1.5 (the float 0x00B0D5E4, 50, without a region), so
//    do the least and greatest height, and the water depths are reset;
// 3. else: the heights come from the picture (0x00740D60), or from the
//    named file (0x00742860) without one; they are raised to the least
//    allowed height (0x007429A0); the water depths are reset; erosion kind 0
//    runs (0x00747B20, strong when the picture came with the specification);
//    the water depths are reset again; the edges are reconciled with the
//    neighbours (slot 89 with 0, 0, 0: no dialogs, the changes applied at
//    once); the least and greatest height are found (0x00740F70).
// In every case +0x68 (loaded) is set and the answer is 1.
//
// A picture gives a vertex 1.5 x the byte at its place: the place moves by
// picture width / vertices per row (and height / rows) from 0 with each
// vertex, added up in a float, and is cut down to a whole pixel with the
// game's own floor idiom (terrain_sim.h, FloorCell). The picture is
// released at the end; +0x80 is set.
//
// How the group reaches every caller: 0x00748130 is called directly by the
// game's Init (0x007504D6; the stack only afterwards) and at its address by
// ours (terrain_object.cpp), so it gets a detour: a plain thiscall, no
// thunk. The three helpers are called only from here (0x00740D60 also from
// 0x00742860) and are plain functions.
//
// The game's and called at their addresses or slots: the specification and
// the region (the application's slots 39 and 35, the region's slot 20), the
// picture and its loader (class 0xA2FFB5D3 through the framework's COM
// object), the string code 0x00407FA0, the water object's reset 0x0073D180,
// erosion 0x00747B20 (ours with simulators.terrain.erode), 0x00740F70 (ours
// with simulators.terrain.brush), slots 89 and 95 of the terrain itself.
//
// Floating point: the two steps are unsigned / unsigned on the x87 rounded
// to float; the places are float + float rounded to float each time; a
// height is integer x 1.5f rounded to float. No transcendental instruction,
// no random draws here (erosion draws none either).
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

namespace field {
constexpr std::size_t kFromPicture = 0x80; // uint8: set by 0x00740D60 once a height picture was read; the destructor clears it
} // namespace field

namespace slot {
constexpr std::size_t kReconcileEdges = 0x164; // slot 89 (report, ask, withEmpty)
constexpr std::size_t kLoad = 0x17C;           // slot 95 (segment): a jump to slot 96
} // namespace slot

// SC4NewCitySpecification as SetUpNewTerrain reads it.
namespace specification {
constexpr std::size_t kFileName = 0x0;  // a C string
constexpr std::size_t kPicture = 0x200; // a height picture (see picture_slot), or null
constexpr std::size_t kSegment = 0x204; // a city file segment to load the terrain from, or null
} // namespace specification

// The height picture's slots (byte offsets) as 0x00740D60 calls them.
namespace picture_slot {
constexpr std::size_t kRelease = 0x8;
constexpr std::size_t kWidth = 0x24;
constexpr std::size_t kHeight = 0x28;
constexpr std::size_t kGetPixel = 0x54; // (x, y) -> a byte in AL
} // namespace picture_slot

// The picture loader 0x00742860 asks the framework for.
namespace loader {
constexpr uint32_t kClass = 0xA2FFB5D3;
constexpr uint32_t kInterface = 0x02FFBB60;
constexpr std::size_t kRelease = 0x8;
constexpr std::size_t kLoad = 0x10; // (string object, picture**, 2) -> AL
} // namespace loader

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

inline uint32_t U32(void* self, std::size_t offset) { return At<uint32_t>(self, offset); }

// The game's string object on the stack (vtable 0x00A80810).
struct GameString {
    std::uintptr_t vtable;
    char* start;
    char* finish;
    char* capacity;
    uint32_t zero;
};
static_assert(sizeof(GameString) == 0x14);

constexpr std::size_t kGetNewCitySpecification = 0x9C; // the application's slot 39
constexpr std::size_t kGetRegion = 0x8C;               // the application's slot 35
constexpr std::size_t kGetBaseTerrainHeight = 0x50;    // the region's slot 20

// 0x00740D60 (picture): 0 for a picture without a width or a height (it is
// not released then, as in the game). Else every vertex, rows first, gets
// 1.5 (0x00B0D5E8) x the picture's byte at (floor of the place along x,
// floor of the place along z); the picture is released, +0x80 set, 1.
uint8_t NewTerrainFromImage(char* self, void* picture)
{
    static bool logged = false;
    FirstCall("NewTerrainFromImage", logged);
    const uint32_t width = Virtual<uint32_t>(picture, picture_slot::kWidth);
    if (width < 1)
        return 0;
    const float stepX =
        static_cast<float>(static_cast<double>(width) / static_cast<double>(U32(self, field::kVertsX)));
    const uint32_t height = Virtual<uint32_t>(picture, picture_slot::kHeight);
    if (height < 1)
        return 0;
    const float stepZ =
        static_cast<float>(static_cast<double>(height) / static_cast<double>(U32(self, field::kVertsZ)));
    uint32_t row = 0;
    float placeZ = 0.0f;
    if (U32(self, field::kVertsZ) != 0) {
        do {
            const uint32_t pixelZ = FloorCell(static_cast<double>(placeZ));
            uint32_t column = 0;
            float placeX = 0.0f;
            if (U32(self, field::kVertsX) != 0) {
                do {
                    const uint32_t pixelX = FloorCell(static_cast<double>(placeX));
                    const uint32_t byte = Virtual<uint32_t>(picture, picture_slot::kGetPixel, pixelX, pixelZ) & 0xFFu;
                    const uint32_t index = U32(self, field::kVertsX) * row + column;
                    AtAddress<float>(RecordAddress(VertexBase(self), index)) = static_cast<float>(
                        static_cast<double>(static_cast<int32_t>(byte)) *
                        static_cast<double>(Global<float>(addr::cSTETerrain_HeightUnit)));
                    ++column;
                    placeX = static_cast<float>(static_cast<double>(placeX) + static_cast<double>(stepX));
                } while (column < U32(self, field::kVertsX));
            }
            placeZ = static_cast<float>(static_cast<double>(placeZ) + static_cast<double>(stepZ));
            ++row;
        } while (row < U32(self, field::kVertsZ));
    }
    Virtual<uint32_t>(picture, picture_slot::kRelease);
    At<uint8_t>(self, field::kFromPicture) = 1;
    return 1;
}

// 0x00742860 (file name: a string object): the framework's COM object
// makes a picture loader, which loads the named file (with 2); 0 when it
// does not; else 0x00740D60 with the picture. The loader is released either
// way. Nothing tests that the loader was made, as in the game.
uint8_t NewTerrainFromFile(char* self, void* fileName)
{
    static bool logged = false;
    FirstCall("NewTerrainFromFile", logged);
    void* picture = nullptr;
    void* made = nullptr;
    void* const com = Cdecl<void*>(addr::RZGetCOM);
    Virtual<uint8_t>(com, 0xC, loader::kClass, loader::kInterface, &made);
    if (!Virtual<uint8_t>(made, loader::kLoad, fileName, &picture, 2u)) {
        Virtual<uint32_t>(made, loader::kRelease);
        return 0;
    }
    Virtual<uint32_t>(made, loader::kRelease);
    return NewTerrainFromImage(self, picture);
}

// 0x007429A0: when the least height (+0x70) is below the least allowed one
// (0x00B0D5E0), every vertex below that is raised to it (a NaN stays) and
// the least height becomes it. +0x70 is what it was before the new heights
// were made: the game's, kept.
void ClampToMinHeight(char* self)
{
    if (!(At<float>(self, field::kMinHeight) < Global<float>(addr::cSTETerrain_MinAllowedHeightValue)))
        return;
    uint32_t offset = 0;
    for (uint32_t index = 0; index < U32(self, field::kVertexCount); ++index, offset += vertex::kSize) {
        const uint32_t record = VertexBase(self) + offset;
        if (AtAddress<float>(record) < Global<float>(addr::cSTETerrain_MinAllowedHeightValue))
            AtAddress<uint32_t>(record) = Global<uint32_t>(addr::cSTETerrain_MinAllowedHeightValue);
    }
    At<uint32_t>(self, field::kMinHeight) = Global<uint32_t>(addr::cSTETerrain_MinAllowedHeightValue);
}

inline void ResetWater(char* self)
{
    This<void>(addr::STE_WaterResetDepths, At<void*>(self, field::kWater), static_cast<void*>(self + field::kCellRect));
}

// 0x00748130: see the top of the file.
uint8_t __fastcall SetUpNewTerrain(char* self, void*)
{
    static bool logged = false;
    FirstCall("SetUpNewTerrain", logged);
    void* const application = Global<void*>(addr::SC4_Service43C94);
    char* const spec = Virtual<char*>(application, kGetNewCitySpecification);
    GameString name;
    name.start = name.finish = name.capacity = nullptr;
    const char* end = spec + specification::kFileName;
    while (*end)
        ++end;
    uint8_t tag = 0; // the game's allocator byte is not set either
    This<void>(addr::SC4String_FromRange, static_cast<void*>(&name.start), static_cast<const char*>(spec), end, &tag);
    name.vtable = addr::SC4String_Vtable;
    name.zero = 0;
    if (void* const segment = At<void*>(spec, specification::kSegment)) {
        Virtual<uint8_t>(self, slot::kLoad, segment);
        ResetWater(self);
    } else if (name.start == name.finish && !At<void*>(spec, specification::kPicture)) {
        // The float is read before the region is asked for.
        const float standard = Global<float>(addr::cSTETerrain_DefaultNewHeight);
        double height;
        if (void* const region = Virtual<void*>(Global<void*>(addr::SC4_Service43C94), kGetRegion))
            height = static_cast<double>(Virtual<int32_t>(region, kGetBaseTerrainHeight)) *
                     static_cast<double>(Global<float>(addr::cSTETerrain_HeightUnit));
        else
            height = static_cast<double>(standard);
        uint32_t offset = 0;
        for (uint32_t index = 0; index < U32(self, field::kVertexCount); ++index, offset += vertex::kSize)
            AtAddress<float>(VertexBase(self) + offset) = static_cast<float>(height);
        At<float>(self, field::kMinHeight) = static_cast<float>(height);
        At<float>(self, field::kMaxHeight) = static_cast<float>(height);
        ResetWater(self);
    } else {
        void* const picture = At<void*>(spec, specification::kPicture);
        // The game pushes a dword whose low byte this is and whose other
        // bytes it never wrote; erosion reads the low byte only.
        uint32_t strong = 0;
        if (picture) {
            NewTerrainFromImage(self, picture);
            strong = 1;
        } else {
            NewTerrainFromFile(self, static_cast<void*>(&name));
        }
        ClampToMinHeight(self);
        ResetWater(self);
        This<void>(addr::cSTETerrain_Erode, self, 0u, strong);
        ResetWater(self);
        Virtual<void>(self, slot::kReconcileEdges, 0u, 0u, 0u);
        This<void>(addr::cSTETerrain_UpdateMinMaxHeight, self);
    }
    At<uint8_t>(self, field::kLoaded) = 1;
    name.vtable = addr::SC4String_Vtable;
    if (name.start)
        Cdecl<void>(addr::SC4_Free, name.start);
    return 1;
}

// --- Installation ------------------------------------------------------------

void* g_original = nullptr;

// The only direct call site of 0x00748130 (scan of all direct calls in the
// exe) is Init's, 0x007504D6: ECX loaded just before, the stack only
// afterwards.
bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    const addr::Entry* const entry = addr_check::Find(addr::cSTETerrain_SetUpNewTerrain);
    if (!entry) {
        log::Write("terrain new city: no address entry for %08X; not installing",
                   static_cast<unsigned>(addr::cSTETerrain_SetUpNewTerrain));
        return false;
    }
    if (!hook::Install(*entry, reinterpret_cast<void*>(&SetUpNewTerrain), &g_original)) {
        log::Write("terrain new city: the detour failed (%08X)", static_cast<unsigned>(addr::cSTETerrain_SetUpNewTerrain));
        return false;
    }
    log::Write("terrain new city: the new terrain set-up %08X (detour) is ours",
               static_cast<unsigned>(addr::cSTETerrain_SetUpNewTerrain));
    return true;
}

const bool g_registered = registry::Register(
    {"simulators.terrain.newcity", "simulators/terrain", registry::Kind::Detour, registry::Phase::PostAppInit,
     addr::cSTETerrain_SetUpNewTerrain,
     "Our cSTETerrain new-city terrain: loaded from a segment, flat at the region's base height, or from a height "
     "picture with erosion and the edges reconciled, by a detour",
     false, Install});

} // namespace
} // namespace opensc4::terrain_sim
