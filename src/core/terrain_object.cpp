// Our cSTETerrain around the simulation (build 641): Init (slot 3,
// 0x0074FEA0), Shutdown (slot 4, 0x007435C0), DoPostCityInit (slot 40,
// 0x00749540) as simulators.terrain.init, and our own object (constructor,
// destructor, identity) as simulators.terrain.object. The queries, setters,
// brushes, messages and the save records are in the other terrain files.
//
// The class: object 0x1E0 bytes, made only by its GZCOM factory 0x0074D580
// (operator new, then a jump to the constructor 0x0074C380), class ID
// 0xE98F9525. Three vtables: 0x00AB3D18 (98 slots; cISTETerrain, deleting
// destructor slot 91), 0x00AB3D08 at +4 (4 slots; cIGZMessageTarget2,
// DoMessage 0x0074A470) and 0x00AB3CF0 at +8 (6 slots; cIGZSerializable:
// Write 0x00745300, Read 0x00745380, GetGZCLSID 0x007405D0). QueryInterface
// (0x00741E90) answers 1, 0xAB953253 and 0xC70BD37E with the object,
// 0x452294AA with +4 and 0xE4FDA3D4 with +8.
//
// Init, in the game's order (each step below is from the disassembly):
//  1. the class's static part once (0x0074FE40: the brush loader and three
//     more; the game's, called at its address);
//  2. Shutdown through the vtable when already initialized; +0xC = 1;
//  3. the city (0x00B43CEC) and the water object (0x00B43CF0), referenced;
//  4. the grid: cells x and z from the city's slots 93 and 94 (+0x174,
//     +0x178), and from them +0x30/+0x34 (cells - 1), +0x38/+0x3C (cells +
//     1), +0x5C (vertex count), +0x60/+0x64 (2 x cells x and that less 1),
//     the whole rectangle +0x40 (0, 0, cells x, cells z), the size in
//     metres +0x50/+0x54 (cells x 16);
//  5. +0x58 the city's size type (its slot 87) and +0x78 the float of that
//     type from the table at 0x00B4BCF8; +0xA4 a byte from 0x00B4B3FC x 64;
//  6. the rows +0x190 (a vector of byte vectors, the water distance map):
//     resized to rows (0x00461120), each row to vertices per row bytes
//     (0x004618E0 with zeros; a longer row is cut);
//  7. three objects of the city (its slots 70, 59 and 85) into +0x100 and
//     +0x104 (when null) and +0xFC (always), referenced;
//  8. +0x130 (0x0075C9E0 with 16); the sea level +0x7C from 0x00B4B404; the
//     eight neighbour index offsets +0x84 to +0xA0;
//  9. the vertex array when there is none: operator new of vertex count x
//     36 bytes, zeroed; least and greatest height 0;
// 10. the vectors +0x118 and +0x124 (12-byte elements) resized to 2 x cells
//     x elements of the default word (0x00B4BC60) three times;
// 11. eight draws of the terrain's generator into +0x1B4 to +0x1D0;
// 12. the dword array +0xE0 when there is none: operator new of vertex
//     count x 4 bytes (not initialised);
// 13. ten message subscriptions, seven cheat commands, the simulator
//     service's agent list (6, the name cSTETerrain, 2), the timer service
//     (0xC0);
// 14. when no terrain was loaded (+0x68 clear): the water object's Init and
//     0x00748130 (the game's).
//
// Floating point: the two sizes are cells (as an unsigned number) x 16.0f
// rounded to float once; +0xA4 is float x float on the x87 truncated by
// __ftol2. The eight draws come back in ST0 and are stored as floats.
//
// No code outside the class calls Init, Shutdown, DoPostCityInit or the
// identity functions directly (scan of every direct call in the exe): the
// destructor 0x0074C740 calls Shutdown directly and slot 91 calls the
// destructor, both ours with the object group. The constructor is detoured;
// its one caller is the factory's jump (ECX the new memory, nothing else in
// registers), and the factory's own callers take the object from EAX.
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
#include <cstring>
#include <initializer_list>

namespace opensc4::terrain_sim {

// --- Additions for terrain_sim.h (to be merged there) --------------------------

namespace field {
constexpr std::size_t kPostCityInitDone = 0x14; // uint8: DoPostCityInit runs once (Shutdown does not clear it)
constexpr std::size_t kSimulators = 0x20;       // the simulator service (0x00B43D5C); the terrain is its agent of kind 6
constexpr std::size_t kTwiceCellsX = 0x60;      // 2 x cells x; +0x64 the same less 1
constexpr std::size_t kCitySizeType = 0x58;     // the city's slot 87
constexpr std::size_t kSizeTypeFloat = 0x78;    // float of that type from 0x00B4BCF8; saved
constexpr std::size_t kNeighbours = 0x84;       // int32[8]: index offsets -1, -w-1, -w, -w+1, +1, +w+1, +w, +w-1 (w = vertices per row)
constexpr std::size_t kInitByte = 0xA4;         // uint8: 0x00B4B3FC x 64 clamped to 0..255
constexpr std::size_t kCells = 0xE0;            // uint32[vertex count], operator new; Init allocates, Shutdown frees
constexpr std::size_t kViewObject1 = 0xF0;      // the view's +0x70, referenced (DoPostCityInit)
constexpr std::size_t kViewObject2 = 0xF4;      // the view's +0x74
constexpr std::size_t kViewObject3 = 0xF8;      // slot 18 of the view's +0xC part
constexpr std::size_t kCityObject1 = 0xFC;      // the city's slot 85
constexpr std::size_t kCityObject2 = 0x100;     // the city's slot 70; DoPostCityInit calls its slot 5
constexpr std::size_t kCityObject3 = 0x104;     // the city's slot 59
constexpr std::size_t kVectorA = 0x10C;         // vector storage freed by the destructor (not touched here)
constexpr std::size_t kVectorB = 0x118;         // vector of 12-byte elements, 2 x cells x of them
constexpr std::size_t kVectorC = 0x124;         // the same
constexpr std::size_t kPart130 = 0x130;         // 0x0075C9D0 / 0x0075C9E0: byte +0 = 1, dword +4 = 16
constexpr std::size_t kRows = 0x190;            // vector<vector<uint8>>: rows x vertices per row (the water distance map)
constexpr std::size_t kPendingRects = 0x1B0;    // list head (0x20-byte nodes): rectangles waiting (terrain_messages.cpp)
constexpr std::size_t kPhases = 0x1B4;          // float[8]: draws between 0 and two pi (Init)
constexpr std::size_t kScrubSet = 0x1D4;        // tree head (0x18 bytes), count at +0x1D8
} // namespace field

constexpr uint32_t kObjectSize = 0x1E0;
constexpr uint32_t kClassId = 0xE98F9525;

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

inline uint32_t& U32(void* self, std::size_t offset) { return At<uint32_t>(self, offset); }

// __ftol2 (0x009EEF04): truncation toward zero to 64 bits; the x87 "integer
// indefinite" for what does not fit and for a NaN.
int64_t Ftol64(double value)
{
    if (!(value > -9.2233720368547758e18 && value < 9.2233720368547758e18))
        return INT64_MIN;
    return static_cast<int64_t>(value);
}

constexpr uint32_t kIidUnknown = 0x00000001;
constexpr uint32_t kIidTerrain1 = 0xAB953253;
constexpr uint32_t kIidTerrain2 = 0xC70BD37E;
constexpr uint32_t kIidMessageTarget2 = 0x452294AA;
constexpr uint32_t kIidSerializable = 0xE4FDA3D4;

// The messages Init subscribes to, in its order, and the ones Shutdown
// unsubscribes from, in its order (0x0C4A6AF3 only there).
constexpr uint32_t kSubscribed[10] = {0x26D31EC1, 0x470BD214, 0x6716A4E6, 0xA6B79602, 0x0C4A6AF0,
                                      0x0C4A6AF1, 0x0C4A6AF2, 0x0C4A6AF4, 0x0C4A6AF5, 0x0C4A6AF6};
constexpr uint32_t kUnsubscribed[11] = {0x26D31EC1, 0xA6B79602, 0x470BD214, 0x6716A4E6, 0x0C4A6AF0, 0x0C4A6AF1,
                                        0x0C4A6AF2, 0x0C4A6AF3, 0x0C4A6AF4, 0x0C4A6AF5, 0x0C4A6AF6};

// The cheat commands: the service is the framework's system service
// 0xEB903A32 (interface 0xCB90253A); slot 6 registers (ID, name, 0), slot 9
// unregisters (ID).
constexpr uint32_t kCommandService = 0xEB903A32, kCommandInterface = 0xCB90253A;
struct CommandName {
    uint32_t id;
    std::uintptr_t name;
};
constexpr CommandName kCommands[7] = {
    {0x0C4A6AF0, addr::cSTETerrain_CommandGetAltitude},     {0x0C4A6AF1, addr::cSTETerrain_CommandSetAltitude},
    {0x0C4A6AF2, addr::cSTETerrain_CommandGetCellAltitude}, {0x0C4A6AF3, addr::cSTETerrain_CommandSetCellAltitude},
    {0x0C4A6AF4, addr::cSTETerrain_CommandGetSeaLevel},     {0x0C4A6AF5, addr::cSTETerrain_CommandSetSeaLevel},
    {0x0C4A6AF6, addr::cSTETerrain_CommandApplyBrush},
};

constexpr uint32_t kTimerInterval = 0xC0;
constexpr uint32_t kViewClass = 0xC9B84E10, kViewInterface = 0x6771477D;

struct GameString {
    const void* vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};

// 0x00741970: eight uniform draws between the doubles at 0x00A80990 (0) and
// 0x00A96018 from the terrain's generator, each stored as a float.
void DrawPhases(char* self)
{
    void* const random = reinterpret_cast<void*>(addr::cSTETerrain_Random);
    for (std::size_t i = 0; i < 8; ++i)
        At<float>(self, field::kPhases + 4 * i) = static_cast<float>(This<double>(
            addr::cRZRandom_Uniform, random, Global<double>(addr::SC4_DoubleZero), Global<double>(addr::SC4_DoubleTwoPiSingle)));
}

// One of the vectors of 12-byte elements resized to `count` elements of
// the default word three times: cut when longer (the game runs an empty
// copy 0x00506280 for the new end: begin + count x 12), else grown at the
// end (0x004FBD30). The size is the signed difference over 12, compared
// unsigned.
void ResizeTriples(char* vector, uint32_t count)
{
    const uint32_t word = Global<uint32_t>(addr::cSTETerrain_DefaultVertexWord);
    uint32_t value[3] = {word, word, word};
    char* const end = At<char*>(vector, 0x4);
    char* const begin = At<char*>(vector, 0x0);
    const uint32_t size = static_cast<uint32_t>(static_cast<int32_t>(end - begin) / 12);
    if (count < size)
        At<char*>(vector, 0x4) = begin + static_cast<std::size_t>(count) * 12u;
    else
        This<void>(addr::SC4TripleVector_InsertN, static_cast<void*>(vector), static_cast<void*>(end), count - size,
                   static_cast<void*>(value));
}

// A city object into `offset`, referenced when there is one.
void TakeFromCity(char* self, std::size_t offset, std::size_t citySlot)
{
    void* const object = Virtual<void*>(At<void*>(self, field::kCity), citySlot);
    At<void*>(self, offset) = object;
    if (object)
        Virtual<uint32_t>(object, 0x4);
}

// Slot 3 (0x0074FEA0). Always true.
uint8_t __fastcall Init(char* self, void*)
{
    static bool logged = false;
    FirstCall("Init", logged);
    if (!Global<uint8_t>(addr::cSTETerrain_StaticsDone))
        Cdecl<uint8_t>(addr::cSTETerrain_InitStatics);
    if (At<uint8_t>(self, field::kInitialized))
        Virtual<uint8_t>(self, 0x10);
    At<uint8_t>(self, field::kInitialized) = 1;
    if (!At<void*>(self, field::kCity)) {
        void* const city = Global<void*>(addr::SC4_City);
        At<void*>(self, field::kCity) = city;
        if (city)
            Virtual<uint32_t>(city, 0x4);
    }
    if (!At<void*>(self, field::kWater)) {
        // No test of the service, as the game.
        void* const water = Global<void*>(addr::SC4_Service43CF0);
        At<void*>(self, field::kWater) = water;
        Virtual<uint32_t>(water, 0x4);
    }

    // The grid.
    const uint32_t cellsX = Virtual<uint32_t>(At<void*>(self, field::kCity), 0x174);
    U32(self, field::kCellsX) = cellsX;
    const uint32_t cellsZ = Virtual<uint32_t>(At<void*>(self, field::kCity), 0x178);
    U32(self, field::kLastCellX) = cellsX - 1;
    U32(self, field::kLastCellZ) = cellsZ - 1;
    U32(self, field::kVertsX) = cellsX + 1;
    U32(self, field::kVertexCount) = (cellsX + 1) * (cellsZ + 1);
    U32(self, field::kTwiceCellsX) = cellsX + cellsX;
    U32(self, field::kCellsZ) = cellsZ;
    U32(self, field::kVertsZ) = cellsZ + 1;
    U32(self, field::kTwiceCellsX + 4) = cellsX + cellsX - 1;
    U32(self, field::kCellRect + 4) = 0;
    U32(self, field::kCellRect) = 0;
    U32(self, field::kCellRect + 8) = cellsX;
    U32(self, field::kCellRect + 12) = cellsZ;
    // FILD of the count, 2^32 added when its sign bit is set: the unsigned
    // number; times the float 16, stored as a float.
    const double width = static_cast<double>(Global<float>(addr::cSTETerrain_CellWidthFloat));
    At<float>(self, field::kSizeX) = static_cast<float>(static_cast<double>(cellsX) * width);
    At<float>(self, field::kSizeZ) = static_cast<float>(static_cast<double>(cellsZ) * width);

    const uint32_t sizeType = Virtual<uint32_t>(At<void*>(self, field::kCity), 0x15C);
    U32(self, field::kCitySizeType) = sizeType;
    // No range test of the type, as the game.
    U32(self, field::kSizeTypeFloat) = Global<uint32_t*>(addr::cSTETerrain_SizeTypeFloats)[sizeType];
    const int32_t scaled = static_cast<int32_t>(Ftol64(static_cast<double>(Global<float>(addr::cSTETerrain_InitScale)) *
                                                       static_cast<double>(Global<float>(addr::SC4_Float64))));
    At<uint8_t>(self, field::kInitByte) = scaled > 0xFF ? 0xFF : (scaled < 0 ? 0 : static_cast<uint8_t>(scaled));

    // The rows: one byte vector per row of vertices, each as long as a row.
    {
        uint32_t empty[3] = {0, 0, 0};
        This<void>(addr::SC4RowVector_Resize, static_cast<void*>(self + field::kRows), U32(self, field::kVertsZ),
                   static_cast<void*>(empty));
        uint8_t zero = 0;
        for (uint32_t i = 0; i < U32(self, field::kVertsZ); ++i) {
            char* const row = At<char*>(self, field::kRows) + static_cast<std::size_t>(i) * 0xC;
            char* const end = At<char*>(row, 0x4);
            char* const begin = At<char*>(row, 0x0);
            const uint32_t length = U32(self, field::kVertsX);
            const uint32_t size = static_cast<uint32_t>(end - begin);
            if (length < size)
                At<char*>(row, 0x4) = begin + length;
            else
                This<void>(addr::SC4ByteVector_InsertN, static_cast<void*>(row), static_cast<void*>(end), length - size,
                           static_cast<void*>(&zero));
        }
    }

    if (!At<void*>(self, field::kCityObject2))
        TakeFromCity(self, field::kCityObject2, 0x118);
    if (!At<void*>(self, field::kCityObject3))
        TakeFromCity(self, field::kCityObject3, 0xEC);
    TakeFromCity(self, field::kCityObject1, 0x154);
    This<uint8_t>(addr::cSTETerrainView_InitPart130, static_cast<void*>(self + field::kPart130), 0x10u);
    U32(self, field::kSeaLevel) = Global<uint32_t>(addr::cSTETerrain_DefaultSeaLevel);
    {
        const uint32_t row = U32(self, field::kVertsX);
        uint32_t* const offsets = &U32(self, field::kNeighbours);
        offsets[1] = 0xFFFFFFFFu - row;
        offsets[2] = 0u - row;
        offsets[6] = row;
        offsets[7] = row - 1;
        offsets[0] = 0xFFFFFFFFu;
        offsets[3] = 1u - row;
        offsets[4] = 1;
        offsets[5] = row + 1;
    }

    // The vertex array. The game constructs each record (the default word
    // into +4, +8, +0xC) and then zeroes the whole array; the same here. A
    // failed allocation is stored and zeroed through, as the game does.
    if (!U32(self, field::kVertices)) {
        const uint32_t count = U32(self, field::kVertexCount);
        char* const records = Cdecl<char*>(addr::SC4_operator_new, count * 9u * 4u);
        if (records && static_cast<int32_t>(count - 1) >= 0) {
            for (uint32_t i = 0; i < count; ++i) {
                char* const one = records + static_cast<std::size_t>(i) * vertex::kSize;
                At<uint32_t>(one, 0x4) = Global<uint32_t>(addr::cSTETerrain_DefaultVertexWord);
                At<uint32_t>(one, 0x8) = Global<uint32_t>(addr::cSTETerrain_DefaultVertexWord);
                At<uint32_t>(one, 0xC) = Global<uint32_t>(addr::cSTETerrain_DefaultVertexWord);
            }
        }
        At<char*>(self, field::kVertices) = records;
        std::memset(records, 0, U32(self, field::kVertexCount) * 9u * 4u);
        uint32_t offset = 0;
        for (uint32_t i = 0; i < U32(self, field::kVertexCount); ++i, offset += vertex::kSize) {
            AtAddress<uint32_t>(VertexBase(self) + offset) = 0;
            AtAddress<uint16_t>(VertexBase(self) + offset + vertex::kFlags) = 0;
        }
        U32(self, field::kMinHeight) = 0;
        U32(self, field::kMaxHeight) = 0;
    }

    ResizeTriples(self + field::kVectorB, U32(self, field::kCellsX) << 1);
    ResizeTriples(self + field::kVectorC, U32(self, field::kCellsX) << 1);
    DrawPhases(self);
    if (!U32(self, field::kCells))
        At<void*>(self, field::kCells) = Cdecl<void*>(addr::SC4_operator_new, U32(self, field::kVertexCount) << 2);

    void* const target = self + 0x4;
    if (void* const server = Global<void*>(addr::SC4_MessageServer))
        for (const uint32_t type : kSubscribed)
            Virtual<uint8_t>(server, 0x14, target, type);

    void* commands = nullptr;
    if (void* const framework = Cdecl<void*>(addr::RZGetFramework))
        Virtual<uint8_t>(framework, 0x14, kCommandService, kCommandInterface, &commands);
    if (commands)
        for (const CommandName& command : kCommands)
            Virtual<uint8_t>(commands, 0x18, command.id, reinterpret_cast<const char*>(command.name), 0u);

    if (!At<void*>(self, field::kSimulators)) {
        void* const simulators = Global<void*>(addr::SC4_SimulatorService);
        At<void*>(self, field::kSimulators) = simulators;
        Virtual<uint32_t>(simulators, 0x4);
        // The name as a game string built in place: 12 bytes from the STL
        // allocator, the 11 characters, a terminator.
        GameString name{nullptr, nullptr, nullptr, nullptr, 0};
        char* const text = Cdecl<char*>(addr::SC4_stl_allocate, 0xCu);
        name.begin = text;
        name.end = text;
        name.capacity = text + 0xC;
        std::memcpy(text, reinterpret_cast<const char*>(addr::cSTETerrain_ClassName), 0xB);
        name.end = text + 0xB;
        *name.end = 0;
        name.vtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
        name.zero = 0;
        Virtual<uint32_t>(At<void*>(self, field::kSimulators), 0x4C, target, 6u, &name, 2u);
        name.vtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
        if (name.begin)
            Cdecl<void>(addr::SC4_Free, static_cast<void*>(name.begin));
    }

    if (void* const timers = Cdecl<void*>(addr::SC4_GetService86AD10EC))
        Virtual<uint8_t>(timers, 0x14, target, kTimerInterval);

    if (!At<uint8_t>(self, field::kLoaded)) {
        Virtual<uint8_t>(At<void*>(self, field::kWater), 0xC);
        This<void>(addr::cSTETerrain_SetUpNewTerrain, self);
    }
    This<void>(addr::SC4RefPtr_Destruct562DD0, static_cast<void*>(&commands));
    return 1;
}

void ReleaseField(char* self, std::size_t offset)
{
    if (void* const object = At<void*>(self, offset)) {
        Virtual<uint32_t>(object, 0x8);
        At<void*>(self, offset) = nullptr;
    }
}

// Slot 4 (0x007435C0). Always true. +0x14 (DoPostCityInit done), +0x68
// (loaded) and the counters stay as they are.
uint8_t __fastcall Shutdown(char* self, void*)
{
    static bool logged = false;
    FirstCall("Shutdown", logged);
    if (!At<uint8_t>(self, field::kInitialized))
        return 1;
    At<uint8_t>(self, field::kInitialized) = 0;
    void* const target = self + 0x4;
    if (void* const timers = Cdecl<void*>(addr::SC4_GetService86AD10EC))
        Virtual<uint8_t>(timers, 0x18, target, kTimerInterval);
    if (At<void*>(self, field::kSimulators)) {
        Virtual<uint32_t>(At<void*>(self, field::kSimulators), 0x50, target);
        Virtual<uint32_t>(At<void*>(self, field::kSimulators), 0x8);
        At<void*>(self, field::kSimulators) = nullptr;
    }
    if (void* const server = Global<void*>(addr::SC4_MessageServer))
        for (const uint32_t type : kUnsubscribed)
            Virtual<uint8_t>(server, 0x18, target, type);
    void* commands = nullptr;
    if (void* const framework = Cdecl<void*>(addr::RZGetFramework))
        Virtual<uint8_t>(framework, 0x14, kCommandService, kCommandInterface, &commands);
    if (commands)
        for (const CommandName& command : kCommands)
            Virtual<uint8_t>(commands, 0x24, command.id);
    ReleaseField(self, field::kCityObject1);
    ReleaseField(self, field::kCityObject3);
    ReleaseField(self, field::kCityObject2);
    ReleaseField(self, field::kViewObject3);
    ReleaseField(self, field::kViewObject2);
    ReleaseField(self, field::kViewObject1);
    if (void* const view = At<void*>(self, field::kView)) {
        This<void>(addr::cSTETerrainView3D_Shutdown7560F0, view);
        Virtual<uint32_t>(At<void*>(self, field::kView), 0x8);
        At<void*>(self, field::kView) = nullptr;
        At<uint8_t>(self, field::kViewActive) = 0;
    }
    ReleaseField(self, field::kWater);
    ReleaseField(self, field::kCity);
    if (void* const cells = At<void*>(self, field::kCells)) {
        Cdecl<void>(addr::SC4_operator_delete, cells);
        At<void*>(self, field::kCells) = nullptr;
    }
    if (void* const records = At<void*>(self, field::kVertices)) {
        Cdecl<void>(addr::SC4_operator_delete, records);
        At<void*>(self, field::kVertices) = nullptr;
    }
    This<void>(addr::SC4RefPtr_Destruct562DD0, static_cast<void*>(&commands));
    return 1;
}

// Slot 40 (0x00749540): once per object. The zoom from the camera view,
// the terrain view made (through GZCOM, whose answer is the view's +0xC
// part; else directly, 0xE8 bytes) and given the terrain, the first
// preparation and refresh of the whole map, and three of the view's
// objects kept.
void __fastcall DoPostCityInit(char* self, void*)
{
    static bool logged = false;
    FirstCall("DoPostCityInit", logged);
    if (At<uint8_t>(self, field::kPostCityInitDone))
        return;
    At<uint8_t>(self, field::kPostCityInitDone) = 1;
    if (char* const camera = Global<char*>(addr::SC4_CameraView))
        U32(self, field::kBrushStep) = At<uint32_t>(camera, 0xC);
    if (!At<void*>(self, field::kView)) {
        // The game's out slot holds its saved ECX (the terrain) until the
        // call writes it.
        char* part = self;
        void* const com = Cdecl<void*>(addr::RZGetCOM);
        if (Virtual<uint8_t>(com, 0xC, kViewClass, kViewInterface, &part)) {
            At<char*>(self, field::kView) = part ? part - 0xC : nullptr;
        } else {
            void* const memory = Cdecl<void*>(addr::SC4_operator_new, 0xE8u);
            void* const view = memory ? This<void*>(addr::cSTETerrainView3D_cSTETerrainView3D, memory) : nullptr;
            At<void*>(self, field::kView) = view;
            Virtual<uint32_t>(view, 0x4);
        }
    }
    This<void>(addr::cSTETerrainView3D_SetTerrain, At<void*>(self, field::kView), static_cast<void*>(self));
    At<uint8_t>(self, field::kViewActive) = 1;
    Virtual<void>(At<void*>(self, field::kCityObject2), 0x14);
    void* const whole = self + field::kCellRect;
    This<void>(addr::cSTETerrain_PrepareRect, self, whole);
    This<void>(addr::cSTETerrain_RefreshRect, self, whole);
    void* const first = At<void*>(At<void*>(self, field::kView), 0x70);
    At<void*>(self, field::kViewObject1) = first;
    if (first)
        Virtual<uint32_t>(first, 0x4);
    void* const second = At<void*>(At<void*>(self, field::kView), 0x74);
    At<void*>(self, field::kViewObject2) = second;
    if (second)
        Virtual<uint32_t>(second, 0x4);
    void* const third = Virtual<void*>(At<char*>(self, field::kView) + 0xC, 0x48, 0u);
    At<void*>(self, field::kViewObject3) = third;
    if (third)
        Virtual<uint32_t>(third, 0x4);
}

// --- Installation of Init, Shutdown and DoPostCityInit -------------------------

struct Slot {
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(index, original, fn) {index, original, reinterpret_cast<void*>(&fn)}

const Slot kInitSlots[] = {
    OPENSC4_SLOT(3, addr::cSTETerrain_Init, Init),
    OPENSC4_SLOT(4, addr::cSTETerrain_Shutdown, Shutdown),
    OPENSC4_SLOT(40, addr::cSTETerrain_DoPostCityInit, DoPostCityInit),
};

#undef OPENSC4_SLOT

bool InstallInit(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    auto* const vtable = reinterpret_cast<std::uintptr_t*>(addr::cSTETerrain_vftable);
    for (const Slot& slot : kInitSlots) {
        if (vtable[slot.index] != slot.original) {
            log::Write("terrain init: slot %d of %08X holds %08X, expected %08X; not installing", slot.index,
                       static_cast<unsigned>(addr::cSTETerrain_vftable), static_cast<unsigned>(vtable[slot.index]),
                       static_cast<unsigned>(slot.original));
            return false;
        }
    }
    for (const Slot& slot : kInitSlots) {
        std::uintptr_t* const entry = vtable + slot.index;
        DWORD old = 0;
        if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &old))
            return false;
        *entry = reinterpret_cast<std::uintptr_t>(slot.ours);
        VirtualProtect(entry, sizeof(*entry), old, &old);
    }
    log::Write("terrain init: slots 3, 4 and 40 of %08X (Init, Shutdown, DoPostCityInit) are ours",
               static_cast<unsigned>(addr::cSTETerrain_vftable));
    return true;
}

const bool g_registeredInit = registry::Register(
    {"simulators.terrain.init", "simulators/terrain", registry::Kind::Vtable, registry::Phase::PostAppInit,
     addr::cSTETerrain_vftable,
     "Our cSTETerrain Init (the grid, its arrays, subscriptions), Shutdown and DoPostCityInit (slots 3, 4 and 40 "
     "of 0x00AB3D18)",
     false, InstallInit});

// --- Our object -------------------------------------------------------------------

// Copies of the game's 0x00AB3D18 (98 slots, deleting destructor slot 91),
// 0x00AB3D08 (at +4) and 0x00AB3CF0 (at +8), taken at the first
// construction, after every PostAppInit slot swap of the other groups.
constexpr int kSlots4 = 4, kSlots8 = 6;
void* g_vtable[kSlotCount];
void* g_vtable4[kSlots4];
void* g_vtable8[kSlots8];

// As the game's (0x004F7BA0, shared with other classes).
uint32_t __fastcall AddRef(char* self, void*) { return ++At<uint32_t>(self, field::kRefCount); }

// As the game's (0x00741E90). A null object gives a null part (the game
// then calls through the null object's vtable; the same fault here).
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    std::size_t offset;
    switch (iid) {
    case kIidUnknown:
    case kIidTerrain1:
    case kIidTerrain2:
        *object = self;
        Virtual<uint32_t>(self, 0x4);
        return 1;
    case kIidMessageTarget2: offset = 0x4; break;
    case kIidSerializable: offset = 0x8; break;
    default: return 0;
    }
    *object = self ? self + offset : nullptr;
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// As the game's (0x00741F70): above one reference (unsigned) one less;
// else the deleting destructor (slot 91) with 1, and 0. The count itself is
// left at its last value.
uint32_t __fastcall ReleaseObject(char* self, void*)
{
    const uint32_t references = At<uint32_t>(self, field::kRefCount);
    if (references > 1) {
        At<uint32_t>(self, field::kRefCount) = references - 1;
        return references - 1;
    }
    Virtual<void*>(self, 0x16C, 1u);
    return 0;
}

// The destructor (0x0074C740): Shutdown when still initialized (the game
// calls 0x007435C0 directly; here slot 4 of our copy, which is that
// function or ours), the scrub set, the pending list, the rows and three
// vectors' storage.
void Destroy(char* self)
{
    At<void**>(self, 0x0) = g_vtable;
    At<void**>(self, 0x4) = g_vtable4;
    At<void**>(self, 0x8) = g_vtable8;
    if (At<uint8_t>(self, field::kInitialized))
        Virtual<uint8_t>(self, 0x10);
    char* const set = self + field::kScrubSet;
    if (At<uint32_t>(set, 0x4)) {
        This<void>(addr::cSTETerrain_FreeScrubNodes, static_cast<void*>(set), At<void*>(At<char*>(set, 0x0), 0x4));
        At<char*>(At<char*>(set, 0x0), 0x8) = At<char*>(set, 0x0);
        At<char*>(At<char*>(set, 0x0), 0x4) = nullptr;
        At<char*>(At<char*>(set, 0x0), 0xC) = At<char*>(set, 0x0);
        At<uint32_t>(set, 0x4) = 0;
    }
    if (void* const head = At<void*>(set, 0x0))
        Cdecl<void>(addr::SC4_Free, head);
    This<void>(addr::SC4PairList_Destroy, static_cast<void*>(self + field::kPendingRects));
    This<void>(addr::SC4ShortVectorVector_Destroy, static_cast<void*>(self + field::kRows));
    for (const std::size_t offset : {field::kVectorC, field::kVectorB, field::kVectorA})
        if (void* const storage = At<void*>(self, offset))
            Cdecl<void>(addr::SC4_Free, storage);
}

// Slot 91, as the game's (0x0074DB90).
void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    static int destroyed = 0;
    if (destroyed++ < 3)
        log::Write("terrain object: destroying %p", static_cast<void*>(self));
    Destroy(self);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// As the game's (0x007405D0).
uint32_t __fastcall GetGZCLSID(void*, void*) { return kClassId; }

template <std::size_t Offset>
uint8_t __fastcall QueryInterfaceAt(char* part, void*, uint32_t iid, void** object)
{
    return QueryInterface(part - Offset, nullptr, iid, object);
}

template <std::size_t Offset>
uint32_t __fastcall AddRefAt(char* part, void*)
{
    return AddRef(part - Offset, nullptr);
}

template <std::size_t Offset>
uint32_t __fastcall ReleaseAt(char* part, void*)
{
    return ReleaseObject(part - Offset, nullptr);
}

void BuildVtables()
{
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSTETerrain_vftable), sizeof(g_vtable));
    std::memcpy(g_vtable4, reinterpret_cast<const void*>(addr::cSTETerrain_vftable_4), sizeof(g_vtable4));
    std::memcpy(g_vtable8, reinterpret_cast<const void*>(addr::cSTETerrain_vftable_8), sizeof(g_vtable8));
    g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
    g_vtable[1] = reinterpret_cast<void*>(&AddRef);
    g_vtable[2] = reinterpret_cast<void*>(&ReleaseObject);
    g_vtable[91] = reinterpret_cast<void*>(&DeletingDestructor);
    g_vtable4[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x4>);
    g_vtable4[1] = reinterpret_cast<void*>(&AddRefAt<0x4>);
    g_vtable4[2] = reinterpret_cast<void*>(&ReleaseAt<0x4>);
    g_vtable8[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x8>);
    g_vtable8[1] = reinterpret_cast<void*>(&AddRefAt<0x8>);
    g_vtable8[2] = reinterpret_cast<void*>(&ReleaseAt<0x8>);
    g_vtable8[5] = reinterpret_cast<void*>(&GetGZCLSID);
    log::Write("terrain object: vtables copied (slots 3/4/40 %p %p %p, +4 slot 3 %p, +8 slots 3/4 %p %p)", g_vtable[3],
               g_vtable[4], g_vtable[40], g_vtable4[3], g_vtable8[3], g_vtable8[4]);
}

// The constructor (detour of 0x0074C380, reached only from the factory
// 0x0074D580): the fields the game's sets, with its two allocations in its
// order (the pending list's head, 0x20 bytes, then the scrub set's, 0x18).
// What it leaves unset stays unset: +0x30, +0x34, +0x58, the gathered
// rectangle +0xC4 to +0xD0, +0x134 to +0x18F and +0x1DC.
void* __fastcall Construct(char* self, void*)
{
    static bool built = false;
    if (!built) {
        built = true;
        BuildVtables();
    }
    At<void**>(self, 0x0) = g_vtable;
    At<void**>(self, 0x4) = g_vtable4;
    At<void**>(self, 0x8) = g_vtable8;
    At<uint8_t>(self, 0xC) = 0;
    At<uint32_t>(self, 0x10) = 0;
    At<uint8_t>(self, 0x14) = 0;
    for (std::size_t offset = 0x18; offset <= 0x2C; offset += 4)
        At<uint32_t>(self, offset) = 0;
    for (std::size_t offset = 0x38; offset <= 0x54; offset += 4)
        At<uint32_t>(self, offset) = 0;
    At<uint32_t>(self, 0x5C) = 0;
    At<uint32_t>(self, 0x60) = 0;
    At<uint32_t>(self, 0x64) = 0;
    At<uint8_t>(self, 0x68) = 0;
    for (std::size_t offset = 0x6C; offset <= 0x7C; offset += 4)
        At<uint32_t>(self, offset) = 0;
    At<uint8_t>(self, 0x80) = 0;
    At<uint8_t>(self, 0xA4) = 0;
    At<uint32_t>(self, 0xA8) = 0;
    At<uint32_t>(self, 0xBC) = 0;
    At<uint8_t>(self, 0xC0) = 0;
    At<uint8_t>(self, 0xD4) = 0;
    At<uint32_t>(self, 0xD8) = 0;
    At<uint8_t>(self, 0xDC) = 0;
    At<uint8_t>(self, 0xDD) = 1;
    At<uint32_t>(self, 0xE0) = 0;
    At<uint32_t>(self, 0xE4) = 0;
    At<uint32_t>(self, 0xE8) = 0;
    At<uint8_t>(self, 0xEC) = 0;
    for (std::size_t offset = 0xF0; offset <= 0x12C; offset += 4)
        At<uint32_t>(self, offset) = 0;
    This<void*>(addr::cSTETerrainView_ConstructPart130, static_cast<void*>(self + field::kPart130));
    for (std::size_t offset = 0x190; offset <= 0x1B0; offset += 4)
        At<uint32_t>(self, offset) = 0;
    char* const pending = Cdecl<char*>(addr::SC4_stl_allocate, 0x20u);
    At<char*>(pending, 0x0) = pending;
    At<char*>(pending, 0x4) = pending;
    At<char*>(self, field::kPendingRects) = pending;
    for (std::size_t offset = 0x1B4; offset <= 0x1D4; offset += 4)
        At<uint32_t>(self, offset) = 0;
    char* const scrub = Cdecl<char*>(addr::SC4_stl_allocate, 0x18u);
    At<char*>(self, field::kScrubSet) = scrub;
    At<uint32_t>(self, field::kScrubSet + 4) = 0;
    At<uint8_t>(scrub, 0x0) = 0;
    At<char*>(scrub, 0x4) = nullptr;
    At<char*>(scrub, 0x8) = scrub;
    At<char*>(scrub, 0xC) = scrub;
    for (std::size_t offset = 0x84; offset <= 0xA0; offset += 4)
        At<uint32_t>(self, offset) = 0;
    for (std::size_t i = 0; i < 4; ++i)
        At<uint32_t>(self, 0xAC + 4 * i) = (&Global<uint32_t>(addr::cSTETerrain_EmptyRect))[i];
    static int created = 0;
    if (created++ < 3)
        log::Write("terrain object: constructed %p", static_cast<void*>(self));
    return self;
}

using ConstructFn = void*(__fastcall*)(char*, void*);
ConstructFn g_originalConstruct = nullptr;

bool ApplyObject(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    const addr::Entry* const entry = addr_check::Find(addr::cSTETerrain_cSTETerrain);
    if (!entry) {
        log::Write("terrain object: no address entry for the constructor; not installing");
        return false;
    }
    if (!hook::Install(*entry, &Construct, &g_originalConstruct))
        return false;
    log::Write("terrain object: the constructor %08X is ours (object %u bytes, class %08X)",
               static_cast<unsigned>(addr::cSTETerrain_cSTETerrain), static_cast<unsigned>(kObjectSize),
               static_cast<unsigned>(kClassId));
    return true;
}

const bool g_registeredObject = registry::Register({
    "simulators.terrain.object",
    "simulators/terrain",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSTETerrain_cSTETerrain,
    "Our cSTETerrain object: constructor (detour; the factory is the only creation path), destructor, "
    "QueryInterface, AddRef, Release, GetGZCLSID; methods from copies of the (patched) game vtables",
    false,
    ApplyObject,
});

} // namespace
} // namespace opensc4::terrain_sim
