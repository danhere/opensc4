// OPENSC4_TEST_Q8=<day>: the query tool's drawing, eighth piece
// (ui.tools.query8, docs/tools.md): the terrain's clamped height
// (0x004C38D0), the bar (0x004C5040), the bar painter (0x004C6830) and the
// route line (0x004C5E20). As T10's test it calls only the game's addresses
// and vtable slots, never our functions. The terrain, the city, the network
// map, the network occupants and their path information, the traffic
// simulator, the traffic source, the windows, the buffer and the picture are
// stand-ins that record what they are told (only which stand-in was meant
// goes into the hash, never a pointer; a pointer into the stack is hashed as
// the 16 bytes it points to). The line is the game's own object, made with
// its constructor; what it holds after each call (every run's three words,
// every point's bits) is hashed. Every stand-in slot the game does not ask
// is counted: `unexpected slots` must be 0. Nothing here changes the city.
//
// OPENSC4_TEST_Q8_PARTS (decimal mask, default 31):
//   1  the clamped height over cell sizes 16, 1.7, 0.1 and a third, four
//      sizes in cells (none and a negative one among them) and points on,
//      below, past and at the limits (a NaN, -0 and a denormal among them)
//   2  the bar on a stand-in window: the buffer locked or not, with and
//      without a picture, picture heights 2, 7 and 2^32 - 1, areas and
//      shares from 0 to past 1 (a NaN and one too big for an integer); the
//      blit is the game's, through its one-pixel path, so the area it is
//      given reaches a stand-in
//   4  the bar painter: other messages, the property or the window's target
//      missing (with and without what they left), and shares drawn
//   8  the route line on stand-ins: routes made for it (runs, jumps, kinds
//      0 to 8, cells outside the city, network paths straight and turning,
//      ferries with and without cells), the way there, the way back and the
//      way there walked backwards, eight kind masks, counts 0 to 2^32 - 1,
//      places outside the city and a NaN
//  16  the route line on real trips: the tool of kind 1 as the view makes it,
//      ShowRoutes (0x004CAC50) for up to six buildings and three network
//      pieces, with the window's "all routes" box off and on
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/tool_query.h"

#include <opensc4/addresses.h>

#include <GZServPtrs.h>
#include <cIGZWin.h>
#include <cISC4App.h>
#include <cISC4City.h>
#include <cISC4View3DWin.h>
#include <cRZAutoRefCount.h>

#include <intrin.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

namespace opensc4::autotest {
namespace {

namespace field = tool_query::field;

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

// A pointer into this thread's stack (the game's and our frames differ, so
// such a pointer is hashed as what it points to).
bool OnStack(uint32_t value)
{
    const uint32_t base = __readfsdword(0x04);
    const uint32_t limit = __readfsdword(0x08);
    return value >= limit && value < base;
}

void NoteWord(uint32_t value)
{
    if (OnStack(value)) {
        Note(uint8_t{0x5A});
        if (g_log)
            g_log->Bytes(reinterpret_cast<const void*>(value), 16);
    } else {
        Note(value);
    }
}

// --- Stand-ins --------------------------------------------------------------------------

constexpr std::size_t kSlots = 0x80;

struct Fake {
    void** vtable;
    uint8_t raw[0x300];
    void* slots[kSlots];
};

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

Fake g_terrain, g_city, g_source, g_network, g_traffic, g_occA, g_occB, g_occC, g_info;
Fake g_window, g_holder, g_buffer, g_picture, g_painted, g_value;

uint8_t Which(const void* pointer)
{
    if (!pointer)
        return 0;
    for (Fake* fake : {&g_terrain, &g_city, &g_source, &g_network, &g_traffic, &g_occA, &g_occB, &g_occC, &g_info,
                       &g_window, &g_holder, &g_buffer, &g_picture, &g_painted, &g_value})
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

// The terrain: slot 6 the cell size (a double at kState, given on the x87
// stack as it is), slots 7 and 8 the cells in x and z (kState + 8, + 0xC),
// slot 13 a height with more bits than a float, slot 29 the water's height
// (a double at kState + 0x10).
double __fastcall TerrainCell(Fake* self, void*)
{
    Note(uint8_t{0x10});
    return Field<double>(self, kState);
}

int32_t __fastcall TerrainCellsX(Fake* self, void*)
{
    Note(uint8_t{0x11});
    return Field<int32_t>(self, kState + 8);
}

int32_t __fastcall TerrainCellsZ(Fake* self, void*)
{
    Note(uint8_t{0x12});
    return Field<int32_t>(self, kState + 0xC);
}

double __fastcall TerrainHeight(Fake*, void*, float x, float z)
{
    Note(uint8_t{0x13});
    Note(Bits(x));
    Note(Bits(z));
    if (x != x || z != z)
        return 1.0 / 3.0;
    return static_cast<double>(x) * 0.1 - static_cast<double>(z) / 7.0 + 2.0 / 3.0;
}

double __fastcall TerrainWater(Fake* self, void*)
{
    Note(uint8_t{0x14});
    return Field<double>(self, kState + 0x10);
}

// The city: slot 84 the terrain, slots 93 and 94 its size in cells (kState,
// kState + 4), slot 97 a cell's centre.
void* __fastcall CityTerrain(Fake*, void*)
{
    Note(uint8_t{0x20});
    return &g_terrain;
}

int32_t __fastcall CitySizeX(Fake* self, void*)
{
    Note(uint8_t{0x21});
    return Field<int32_t>(self, kState);
}

int32_t __fastcall CitySizeZ(Fake* self, void*)
{
    Note(uint8_t{0x22});
    return Field<int32_t>(self, kState + 4);
}

void __fastcall CityCentre(Fake*, void*, int32_t cellX, int32_t cellZ, float* x, float* z)
{
    Note(uint8_t{0x23});
    Note(cellX);
    Note(cellZ);
    *x = static_cast<float>(cellX) * 16.0f + 8.25f;
    *z = static_cast<float>(cellZ) * 16.0f + 7.75f;
}

// The traffic source: slot 5 its cells (kState: four int32).
int32_t* __fastcall SourceCells(Fake* self, void*, int32_t* out)
{
    Note(uint8_t{0x24});
    std::memcpy(out, &Field<int32_t>(self, kState), 16);
    return out;
}

// --- Routes made for the test -------------------------------------------------------------

// Route bytes as the game's iterator reads them (cells one byte each): the
// start (type, x, z); runs (type | 0x40, count, the 2-bit sides four a byte,
// the low bits first: 0 -x, 1 -z, 2 +x, 3 +z); cells by a jump (type, with
// 0x80 or not, x, z).
struct Route {
    std::vector<uint8_t> bytes;
    void Start(uint8_t type, uint8_t x, uint8_t z) { bytes.insert(bytes.end(), {type, x, z}); }
    void Run(uint8_t type, std::initializer_list<uint8_t> sides)
    {
        bytes.push_back(static_cast<uint8_t>(type | 0x40));
        bytes.push_back(static_cast<uint8_t>(sides.size()));
        uint8_t packed = 0;
        int at = 0;
        for (const uint8_t side : sides) {
            packed = static_cast<uint8_t>(packed | (side & 3) << (2 * at));
            if (++at == 4) {
                bytes.push_back(packed);
                packed = 0;
                at = 0;
            }
        }
        if (at)
            bytes.push_back(packed);
    }
    void Jump(uint8_t head, uint8_t x, uint8_t z) { bytes.insert(bytes.end(), {head, x, z}); }
};

// A byte vector as the game keeps one: begin, end, capacity.
struct Vector {
    const uint8_t* begin;
    const uint8_t* end;
    const uint8_t* capacity;
};

Vector Of(const Route& route)
{
    const uint8_t* const begin = route.bytes.data();
    return {begin, begin + route.bytes.size(), begin + route.bytes.size()};
}

// The ferry routes the traffic stand-in hands out, by the cells of the jump.
Route g_ferry, g_emptyFerry;
Vector g_ferryVector, g_emptyVector;

void* __fastcall TrafficFerry(Fake*, void*, int32_t fromX, int32_t fromZ, int32_t toX, int32_t toZ)
{
    Note(uint8_t{0x30});
    Note(fromX);
    Note(fromZ);
    Note(toX);
    Note(toZ);
    if (toX == 30 && toZ == 40)
        return &g_ferryVector;
    if (toX == 5 && toZ == 5)
        return &g_emptyVector;
    return nullptr;
}

// The network map's chains (a node: the occupant at +0, the next at +0x54).
struct Node {
    void* occupant;
    uint8_t unused[0x50];
    Node* next;
};
static_assert(offsetof(Node, next) == 0x54);

Node g_corner, g_straight, g_chainEmpty, g_chainOther, g_chainMatch, g_start, g_row;

void* __fastcall NetworkAt(Fake*, void*, int32_t x, int32_t z)
{
    Note(uint8_t{0x31});
    Note(x);
    Note(z);
    if (x == 16 && z == 10)
        return &g_corner;
    if (x == 13 && z == 10)
        return &g_straight;
    if (x == 14 && z == 10)
        return &g_chainEmpty;
    if ((x == 40 && z == 20) || (x == 38 && z == 17))
        return &g_start;
    if (z == 50 && x >= 1 && x <= 24)
        return &g_row;
    return nullptr;
}

// An occupant: slot 9 whether it is of a mask (its own at kState), slot 49
// its path information (kState + 4).
uint8_t __fastcall OccupantHas(Fake* self, void*, uint32_t mask)
{
    Note(uint8_t{0x32});
    Note(Role(self));
    Note(mask);
    return (mask & Field<uint32_t>(self, kState)) != 0;
}

void* __fastcall OccupantInfo(Fake* self, void*)
{
    Note(uint8_t{0x33});
    Note(Role(self));
    return Field<void*>(self, kState + 4);
}

// The path information: slot 5 a path for a type between two sides (only
// the sides' low bytes are read, as the game's does), slot 22 a point of a
// path. A path is a vector of three-float points.
struct Path {
    float* begin;
    float* end;
    float* capacity;
};

float g_pathPoints[3][3] = {
    {250.5f, 4.25f, 166.125f}, {258.0f, Float(0x40A00001u), 170.3f}, {263.75f, 6.0f, 175.0f}};
Path g_path = {&g_pathPoints[0][0], &g_pathPoints[0][0] + 9, &g_pathPoints[0][0] + 9};

void* __fastcall InfoPath(Fake* self, void*, uint32_t type, uint32_t from, uint32_t to, const float* near,
                          uint32_t a, uint32_t b)
{
    Note(uint8_t{0x34});
    Note(type);
    Note(static_cast<uint8_t>(from));
    Note(static_cast<uint8_t>(to));
    for (int i = 0; i < 3; ++i)
        Note(Bits(near[i]));
    Note(a);
    Note(b);
    // kState: refuse every n-th call (0 never).
    const uint32_t every = Field<uint32_t>(self, kState);
    const uint32_t calls = ++Field<uint32_t>(self, kState + 4);
    return every && calls % every == 0 ? nullptr : &g_path;
}

void __fastcall InfoPoint(Fake*, void*, const Path* path, float* out)
{
    Note(uint8_t{0x35});
    Note(uint8_t{path == &g_path});
    out[0] = 255.125f;
    out[1] = Float(0x3F800001u);
    out[2] = 171.0f / 3.0f + 100.0f;
}

// --- The line ---------------------------------------------------------------------------------

// The line's runs: a list at +0x14 of 0x44-byte nodes, each with three words
// at +8 and its points in a deque at +0x14 (start iterator: current, first,
// last, map node; then the finish iterator), blocks of 0x78 bytes.
void HashLine(Fnv& f, void* line, int32_t& runs, int32_t& points)
{
    runs = 0;
    points = 0;
    void** const sentinel = At<void**>(line, 0x14);
    for (void** node = static_cast<void**>(sentinel[0]); node != sentinel && runs < 100000;
         node = static_cast<void**>(node[0])) {
        ++runs;
        char* const run = reinterpret_cast<char*>(node);
        f.Add(At<uint32_t>(run, 0x8));
        f.Add(At<uint32_t>(run, 0xC));
        f.Add(At<uint32_t>(run, 0x10));
        char* cur = At<char*>(run, 0x14);
        char* last = At<char*>(run, 0x1C);
        char** map = At<char**>(run, 0x20);
        char* const finish = At<char*>(run, 0x24);
        int32_t inRun = 0;
        while (cur != finish && inRun < 1000000) {
            f.Bytes(cur, 12);
            ++inRun;
            cur += 12;
            if (cur == last) {
                ++map;
                cur = *map;
                last = cur + 0x78;
            }
        }
        f.Add(inRun);
        points += inRun;
    }
    f.Add(runs);
}

void* MakeLine()
{
    void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x2C});
    if (!memory)
        return nullptr;
    void* const line = Game<void*>(addr::SC4RouteLine_Construct, memory);
    Virtual<uint32_t>(line, 0x4);
    return line;
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

void SetUpFakes()
{
    uint8_t role = 1;
    for (Fake* fake : {&g_terrain, &g_city, &g_source, &g_network, &g_traffic, &g_occA, &g_occB, &g_occC, &g_info,
                       &g_window, &g_holder, &g_buffer, &g_picture, &g_painted, &g_value})
        Make(*fake, role++);
    Slot(g_terrain, 0x18, &TerrainCell);
    Slot(g_terrain, 0x1C, &TerrainCellsX);
    Slot(g_terrain, 0x20, &TerrainCellsZ);
    Slot(g_terrain, 0x34, &TerrainHeight);
    Slot(g_terrain, 0x74, &TerrainWater);
    Slot(g_city, 0x150, &CityTerrain);
    Slot(g_city, 0x174, &CitySizeX);
    Slot(g_city, 0x178, &CitySizeZ);
    Slot(g_city, 0x184, &CityCentre);
    Slot(g_source, 0x14, &SourceCells);
    Slot(g_traffic, 0x74, &TrafficFerry);
    Slot(g_network, 0x38, &NetworkAt);
    for (Fake* occupant : {&g_occA, &g_occB, &g_occC}) {
        Slot(*occupant, 0x24, &OccupantHas);
        Slot(*occupant, 0xC4, &OccupantInfo);
    }
    Slot(g_info, 0x14, &InfoPath);
    Slot(g_info, 0x58, &InfoPoint);
}

// --- 1. The clamped height ----------------------------------------------------------------

// 0x004C38D0 with the terrain in ESI: (terrain, x, z, function); the height
// read back from the x87 stack as a double.
__declspec(naked) void HeightCall()
{
    __asm push esi
    __asm mov esi, dword ptr [esp + 8]
    __asm push dword ptr [esp + 16]
    __asm push dword ptr [esp + 16]
    __asm call dword ptr [esp + 28]
    __asm add esp, 8
    __asm pop esi
    __asm ret
}

double Height(void* terrain, uint32_t x, uint32_t z)
{
    using CallFn = double(__cdecl*)(void*, uint32_t, uint32_t, std::uintptr_t);
    return reinterpret_cast<CallFn>(&HeightCall)(terrain, x, z, addr::SC4QueryTool_AddClampedPoint);
}

void PartHeight(Fnv& out)
{
    Fnv f;
    g_log = &f;
    static const double kCells[] = {16.0, 1.7, 0.1, 1.0 / 3.0};
    static const int32_t kSizes[][2] = {{64, 64}, {256, 100}, {0, 0}, {-5, 3}};
    static const uint32_t kPoints[][2] = {
        {0x00000000u, 0x00000000u}, // 0, 0
        {0xBF800000u, 0x40A00000u}, // -1, 5
        {0x459C4000u, 0x459C4000u}, // 5000, 5000
        {0x7FC00000u, 0x80000000u}, // NaN, -0
        {0x80000000u, 0x7FC00000u}, // -0, NaN
        {0x44800000u, 0x44800000u}, // 1024, 1024 (the 64-cell limits at 16)
        {0x447FFFFFu, 0x447FFFFFu}, // just below them
        {0x432A0000u, 0x432A0001u}, // 170, a bit more (1.7 times 100)
        {0x3E99999Au, 0x3E99999Au}, // 0.3, 0.3 (0.1 times 3)
        {0x00000001u, 0x80000001u}, // the smallest denormals
        {0x42AAAAABu, 0x41055555u}, // 85.33, 8.33 (a third times 256 and 25)
        {0xFF800000u, 0x7F800000u}, // -inf, inf
    };
    for (const double cell : kCells) {
        Field<double>(g_terrain, kState) = cell;
        for (const auto& size : kSizes) {
            Field<int32_t>(g_terrain, kState + 8) = size[0];
            Field<int32_t>(g_terrain, kState + 0xC) = size[1];
            for (const auto& point : kPoints) {
                const double height = Height(&g_terrain, point[0], point[1]);
                f.Add(height);
            }
        }
    }
    f.Add(g_unexpected);
    g_log = nullptr;
    log::Write("autotest: query8: height %08X", f.h);
    out.Add(f.h);
}

// --- 2. The bar ------------------------------------------------------------------------------

// The window: slot 3 its holder (whose slot 94 is the buffer), slot 5 the
// picture for an IID (kState: give it or not).
void* __fastcall WindowHolder(Fake*, void*)
{
    Note(uint8_t{0x40});
    return &g_holder;
}

uint32_t __fastcall WindowPicture(Fake* self, void*, uint32_t iid, void** out)
{
    Note(uint8_t{0x41});
    Note(iid);
    Note(Which(*out));
    if (Field<uint8_t>(self, kState))
        *out = &g_picture;
    return 0x55;
}

void* __fastcall HolderBuffer(Fake*, void*)
{
    Note(uint8_t{0x42});
    return &g_buffer;
}

// The buffer: slot 6 lock (kState: says yes), slot 7 unlock, slot 11 its
// area (kState + 4: four int32); as the blit's target, slot 30 and slot 20.
uint8_t __fastcall BufferLock(Fake* self, void*, uint32_t flags)
{
    Note(uint8_t{0x43});
    Note(flags);
    return Field<uint8_t>(self, kState);
}

void __fastcall BufferUnlock(Fake*, void*, uint32_t flags)
{
    Note(uint8_t{0x44});
    Note(flags);
}

int32_t* __fastcall BufferArea(Fake* self, void*, int32_t* area)
{
    Note(uint8_t{0x45});
    std::memcpy(area, &Field<int32_t>(self, kState + 4), 16);
    return area;
}

uint32_t __fastcall BufferFill(Fake*, void*, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    Note(uint8_t{0x46});
    for (const uint32_t word : {a, b, c, d, e})
        NoteWord(word);
    return 0x77;
}

uint32_t __fastcall BufferUse(Fake*, void*, uint32_t a)
{
    Note(uint8_t{0x47});
    NoteWord(a);
    return 1;
}

// The picture: slot 9 width 1 and slot 10 its height (kState), so that both
// blits take the blit's one-pixel path; slot 12 its area, slot 25 says no,
// slot 21 a pixel, slot 31 its colour.
int32_t g_pictureArea[4] = {-100, -100, 100, 100};

uint32_t __fastcall PictureWidth(Fake*, void*)
{
    Note(uint8_t{0x48});
    return 1;
}

uint32_t __fastcall PictureHeight(Fake* self, void*)
{
    Note(uint8_t{0x49});
    return Field<uint32_t>(self, kState);
}

int32_t* __fastcall PictureArea(Fake*, void*)
{
    Note(uint8_t{0x4A});
    return g_pictureArea;
}

uint8_t __fastcall PictureOther(Fake*, void*, int32_t x, int32_t y)
{
    Note(uint8_t{0x4B});
    Note(x);
    Note(y);
    return 0;
}

// The three outputs are dwords: the blit hands it two of its locals and its
// own source area argument slot, which then goes to the target's slot 30.
uint32_t __fastcall PicturePixel(Fake*, void*, int32_t x, int32_t y, uint32_t* a, uint32_t* b, uint32_t* c)
{
    Note(uint8_t{0x4C});
    Note(x);
    Note(y);
    *a = 0x12;
    *b = 0x34;
    *c = 0x56;
    return 0xABCDEF;
}

uint32_t __fastcall PictureColor(Fake*, void*, uint32_t pixel)
{
    Note(uint8_t{0x4D});
    Note(pixel);
    return 0x99;
}

void SetUpBar()
{
    Slot(g_window, 0x4, &AddRef);
    Slot(g_window, 0x8, &Release);
    Slot(g_window, 0xC, &WindowHolder);
    Slot(g_window, 0x14, &WindowPicture);
    Slot(g_holder, 0x178, &HolderBuffer);
    Slot(g_buffer, 0x18, &BufferLock);
    Slot(g_buffer, 0x1C, &BufferUnlock);
    Slot(g_buffer, 0x2C, &BufferArea);
    Slot(g_buffer, 0x78, &BufferFill);
    Slot(g_buffer, 0x50, &BufferUse);
    Slot(g_picture, 0x4, &AddRef);
    Slot(g_picture, 0x8, &Release);
    Slot(g_picture, 0x24, &PictureWidth);
    Slot(g_picture, 0x28, &PictureHeight);
    Slot(g_picture, 0x30, &PictureArea);
    Slot(g_picture, 0x64, &PictureOther);
    Slot(g_picture, 0x54, &PicturePixel);
    Slot(g_picture, 0x7C, &PictureColor);
}

struct BarCase {
    uint8_t locks;
    uint8_t picture;
    uint32_t height;
    int32_t area[4];
    uint32_t share;
};

void PartBar(Fnv& out)
{
    Fnv f;
    g_log = &f;
    SetUpBar();
    static const BarCase kCases[] = {
        {0, 1, 2, {10, 20, 110, 21}, 0x3F000000u},          // not locked
        {1, 0, 2, {10, 20, 110, 21}, 0x3F000000u},          // no picture
        {0, 0, 2, {10, 20, 110, 21}, 0x3F000000u},
        {1, 1, 2, {10, 20, 110, 21}, 0x3F000000u},          // 0.5
        {1, 1, 2, {10, 20, 110, 21}, 0x00000000u},          // 0
        {1, 1, 2, {10, 20, 110, 21}, 0x3F800000u},          // 1
        {1, 1, 2, {10, 20, 110, 21}, 0x3EAAAAABu},          // a third
        {1, 1, 2, {-7, 20, 93, 21}, 0xBE800000u},           // -0.25
        {1, 1, 7, {0, 0, 1000001, 1}, 0x40200000u},         // 2.5
        {1, 1, 7, {0, -3, 333, -2}, 0x7FC00000u},           // NaN
        {1, 1, 2, {5, 5, 1000000, 6}, 0x501502F9u},         // 1e10: too big
        {1, 1, 2, {5, 5, 77, 6}, 0x00000001u},              // a denormal
        {1, 1, 0xFFFFFFFFu, {10, 20, 110, 21}, 0x3F000000u}, // half the height outside the picture
        {1, 1, 2, {-2147483647 - 1, 0, 2147483647, 1}, 0x3F400000u}, // the width overflows
        {1, 1, 3, {1, 2, 3, 4}, 0x3F000000u},               // three rows: the blit's other path is not taken
    };
    for (const BarCase& c : kCases) {
        Field<uint8_t>(g_buffer, kState) = c.locks;
        std::memcpy(&Field<int32_t>(g_buffer, kState + 4), c.area, sizeof c.area);
        Field<uint8_t>(g_window, kState) = c.picture;
        Field<uint32_t>(g_picture, kState) = c.height;
        if (c.area[3] - c.area[1] != 1) {
            // The blit would take its tiling path: skipped.
            f.Add(uint8_t{0xFD});
            continue;
        }
        Cdecl<void>(addr::SC4QueryTool_DrawBar, static_cast<void*>(&g_window), c.share);
        f.Add(g_picture.raw[kCount - 4]);
    }
    f.Add(g_unexpected);
    g_log = nullptr;
    log::Write("autotest: query8: bar %08X", f.h);
    out.Add(f.h);
}

// --- 3. The bar painter ------------------------------------------------------------------

// The painted window: slot 113 a property (kState: say yes; kState + 1: give
// the value even when saying no), slot 0 what it gives for an IID (kState +
// 2: say yes; kState + 3: give the window even when saying no). The value:
// slot 81 its data (kState: the float's bits).
uint8_t __fastcall PaintedProperty(Fake* self, void*, uint32_t id, void** out)
{
    Note(uint8_t{0x50});
    Note(id);
    Note(Which(*out));
    if (Field<uint8_t>(self, kState) || Field<uint8_t>(self, kState + 1)) {
        *out = &g_value;
        ++g_value.raw[kCount - 4];
    }
    return Field<uint8_t>(self, kState);
}

uint8_t __fastcall PaintedAs(Fake* self, void*, uint32_t iid, void** out)
{
    Note(uint8_t{0x51});
    Note(iid);
    Note(Which(*out));
    if (Field<uint8_t>(self, kState + 2) || Field<uint8_t>(self, kState + 3)) {
        *out = &g_window;
        ++g_window.raw[kCount - 4];
    }
    return Field<uint8_t>(self, kState + 2);
}

uint32_t* __fastcall ValueData(Fake* self, void*)
{
    Note(uint8_t{0x52});
    return &Field<uint32_t>(self, kState);
}

struct PaintCase {
    uint32_t type;
    uint8_t property, valueAnyway, target, targetAnyway;
    uint32_t share;
};

void PartPainter(Fnv& out)
{
    Fnv f;
    g_log = &f;
    SetUpBar();
    Slot(g_painted, 0x0, &PaintedAs);
    Slot(g_painted, 0x1C4, &PaintedProperty);
    Slot(g_value, 0x4, &AddRef);
    Slot(g_value, 0x8, &Release);
    Slot(g_value, 0x144, &ValueData);
    Field<uint8_t>(g_buffer, kState) = 1;
    const int32_t area[4] = {40, 8, 140, 9};
    std::memcpy(&Field<int32_t>(g_buffer, kState + 4), area, sizeof area);
    Field<uint8_t>(g_window, kState) = 1;
    Field<uint32_t>(g_picture, kState) = 2;
    static const PaintCase kCases[] = {
        {0x15, 1, 0, 1, 0, 0x3F000000u}, // another message
        {0x17, 1, 0, 1, 0, 0x3F000000u},
        {0x16, 0, 0, 1, 0, 0x3F000000u}, // no property
        {0x16, 0, 1, 1, 0, 0x3F000000u}, // no, but a value left
        {0x16, 1, 0, 0, 0, 0x3F000000u}, // no target
        {0x16, 1, 0, 0, 1, 0x3F000000u}, // no, but a window left
        {0x16, 1, 0, 1, 0, 0x3F000000u}, // drawn: a half
        {0x16, 1, 0, 1, 0, 0x3E4CCCCDu}, // 0.2
        {0x16, 1, 0, 1, 0, 0x3F7FFFFFu}, // just below 1
        {0x16, 1, 0, 1, 0, 0xFFC00000u}, // a NaN
    };
    for (const PaintCase& c : kCases) {
        Field<uint8_t>(g_painted, kState) = c.property;
        Field<uint8_t>(g_painted, kState + 1) = c.valueAnyway;
        Field<uint8_t>(g_painted, kState + 2) = c.target;
        Field<uint8_t>(g_painted, kState + 3) = c.targetAnyway;
        Field<uint32_t>(g_value, kState) = c.share;
        uint32_t message[4] = {c.type, 0x11, 0x22, 0x33};
        char painter[4] = {};
        f.Add(Game<uint8_t>(addr::SC4QueryBarPainter_OnMessage, painter, static_cast<void*>(&g_painted),
                            static_cast<uint32_t*>(message)));
        f.Add(g_value.raw[kCount - 4]);
        f.Add(g_window.raw[kCount - 4]);
        f.Add(g_picture.raw[kCount - 4]);
    }
    f.Add(g_unexpected);
    g_log = nullptr;
    log::Write("autotest: query8: painter %08X", f.h);
    out.Add(f.h);
}

// --- 4. The route line on stand-ins ----------------------------------------------------------

// A trip record: the two routes' vectors at +0 and +0xC, the count at +0x18,
// the flags at +0x1E, the cell at +0x20.
struct Trip {
    Vector there;
    Vector back;
    uint32_t count;
    uint8_t bytes[2];
    uint8_t flags;
    uint8_t byte;
    int16_t x, z;
    uint8_t rest[8];
};
static_assert(offsetof(Trip, count) == 0x18 && offsetof(Trip, flags) == 0x1E && offsetof(Trip, x) == 0x20);

Route g_routeA, g_routeB, g_routeRow, g_routeEmpty, g_routeOne;

void MakeRoutes()
{
    g_routeA = Route();
    g_routeA.Start(0, 10, 10);
    g_routeA.Run(0, {2, 2, 2, 2, 2, 2}); // to 16, 10: 13 and 14 have network occupants
    g_routeA.Run(0, {3, 3});             // 16, 12 (16, 10 turns)
    g_routeA.Run(3, {3, 3, 3});          // 16, 15
    g_routeA.Jump(6 | 0x80, 30, 40);     // a ferry
    g_routeA.Run(6, {2, 2});
    g_routeA.Run(7, {2});
    g_routeA.Jump(1, 62, 40);
    g_routeA.Run(1, {2, 2, 2});          // out of the city
    g_routeA.Run(1, {1});
    g_routeA.Jump(2, 5, 5);              // a ferry without cells
    g_routeA.Run(2, {0, 0, 0, 0, 0, 0}); // out at the low edge
    g_routeA.Run(8, {3, 3});
    g_routeA.Run(5, {2, 2, 2, 2, 2});

    g_routeB = Route();
    g_routeB.Start(4, 40, 20);           // a network occupant at the start: side 0xFF
    g_routeB.Run(4, {1, 1, 1});
    g_routeB.Run(5, {0, 0});             // ends at 38, 17 on an occupant: direction 0xFF
    g_routeRow = Route();
    g_routeRow.Start(0, 1, 50);
    g_routeRow.Run(0, {2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2});
    g_routeRow.Run(1, {3, 3});
    g_routeEmpty = Route();
    g_routeOne = Route();
    g_routeOne.Start(0, 70, 70);

    g_ferry = Route();
    g_ferry.Start(0, 17, 16);
    g_ferry.Run(0, {2, 2, 2, 3, 3});
    g_emptyFerry = Route();
    g_ferryVector = Of(g_ferry);
    g_emptyVector = {nullptr, nullptr, nullptr};
}

// The network occupants: A matches every kind's mask with path information,
// B matches none, C matches with information that has no path every other
// call.
void SetUpNetwork()
{
    Field<uint32_t>(g_occA, kState) = 0xFFFFFFFFu;
    Field<void*>(g_occA, kState + 4) = &g_info;
    Field<uint32_t>(g_occB, kState) = 0;
    Field<void*>(g_occB, kState + 4) = &g_info;
    Field<uint32_t>(g_occC, kState) = 0x1C4Du;
    Field<void*>(g_occC, kState + 4) = nullptr;
    g_corner = {&g_occA, {}, nullptr};
    g_straight = {&g_occC, {}, nullptr};
    g_chainMatch = {&g_occA, {}, nullptr};
    g_chainOther = {&g_occB, {}, &g_chainMatch};
    g_chainEmpty = {nullptr, {}, &g_chainOther};
    g_start = {&g_occA, {}, nullptr};
    g_row = {&g_occA, {}, nullptr};
}

struct RouteCase {
    const Route* there;
    const Route* back;
    uint8_t flags;
    uint32_t backArg;
    uint32_t mask;
    uint32_t count;
    uint32_t place[3];
    int16_t cell[2];
    uint32_t refuseEvery; // the path information refuses every n-th call
};

void PartRoutes(Fnv& out)
{
    void* const line = MakeLine();
    if (!line) {
        log::Write("autotest: query8: routes: no line");
        return;
    }
    Fnv f;
    g_log = &f;
    Swap city(addr::SC4_City, &g_city);
    Swap network(addr::SC4_Service43D54, &g_network);
    Swap traffic(addr::SC4_Service43DA8, &g_traffic);
    MakeRoutes();
    SetUpNetwork();
    Field<double>(g_terrain, kState) = 16.0;
    Field<int32_t>(g_terrain, kState + 8) = 64;
    Field<int32_t>(g_terrain, kState + 0xC) = 64;
    Field<double>(g_terrain, kState + 0x10) = 7.25 + 1.0 / 3.0;
    Field<int32_t>(g_city, kState) = 64;
    Field<int32_t>(g_city, kState + 4) = 64;
    const int32_t corners[4] = {9, 11, 10, 12};
    std::memcpy(&Field<int32_t>(g_source, kState), corners, sizeof corners);
    char tool[field::kSize] = {};
    At<void*>(tool, field::kObject9C) = line;
    constexpr uint32_t kAll = 0xFFFFFFFFu;
    const uint32_t place[3] = {0x42C90000u, 0x41100000u, 0x43484000u}; // 100.5, 9, 200.25
    static const RouteCase kCases[] = {
        {&g_routeA, &g_routeB, 0, 0, kAll, 7, {}, {12, 11}, 0},
        {&g_routeA, &g_routeB, 1, 1, kAll, 7, {}, {12, 11}, 0},          // walked backwards
        {&g_routeA, &g_routeB, 0, 1, kAll, 7, {}, {12, 11}, 0},          // the way back
        {&g_routeA, &g_routeB, 0, 0x100, kAll, 7, {}, {12, 11}, 0},      // only the low byte counts
        {&g_routeA, &g_routeB, 0, 0, 0x7FFFFFFFu, 7, {}, {12, 11}, 0},   // no ferries
        {&g_routeA, &g_routeB, 0, 0, 0xFFFFFFFEu, 7, {}, {12, 11}, 0},   // no kind 0
        {&g_routeA, &g_routeB, 0, 0, 0, 7, {}, {12, 11}, 0},             // nothing
        {&g_routeA, &g_routeB, 0, 0, 0x80000048u, 7, {}, {12, 11}, 0},   // kinds 3 and 6, ferries
        {&g_routeA, &g_routeB, 1, 1, 0x80000021u, 3, {}, {12, 11}, 0},
        {&g_routeA, &g_routeB, 0, 0, kAll, 0xFFFFFFFFu, {0xC0A00000u, 0u, 0x44FA0000u}, {12, 11}, 0},
        {&g_routeA, &g_routeB, 0, 0, kAll, 0, {0x7FC00000u, 0u, 0x41200000u}, {12, 11}, 2},
        {&g_routeB, &g_routeA, 0, 0, kAll, 100, {}, {40, 22}, 0},
        {&g_routeB, &g_routeA, 1, 1, kAll, 100, {}, {40, 22}, 0},
        {&g_routeB, &g_routeA, 0, 1, kAll, 100, {}, {-3, 22}, 3},        // the way back, its end outside
        {&g_routeRow, &g_routeEmpty, 0, 0, kAll, 12, {}, {0, 50}, 0},    // every eighth cell a new run
        {&g_routeRow, &g_routeEmpty, 1, 1, kAll, 12, {}, {0, 50}, 5},
        {&g_routeRow, &g_routeEmpty, 0, 1, kAll, 12, {}, {0, 50}, 0},    // an empty way back
        {&g_routeEmpty, &g_routeEmpty, 0, 0, kAll, 1, {}, {20, 20}, 0},
        {&g_routeOne, &g_routeOne, 0, 0, kAll, 1, {}, {70, 70}, 0},      // outside
        {&g_routeOne, &g_routeOne, 0, 0, kAll, 1, {}, {63, 63}, 0},
    };
    int32_t totalRuns = 0, totalPoints = 0;
    for (const RouteCase& c : kCases) {
        Game<void>(addr::SC4RouteLine_Clear, line);
        Trip trip{};
        trip.there = Of(*c.there);
        trip.back = Of(*c.back);
        trip.count = c.count;
        trip.flags = c.flags;
        trip.x = c.cell[0];
        trip.z = c.cell[1];
        const bool given = c.place[0] || c.place[1] || c.place[2];
        const uint32_t* const at = given ? c.place : place;
        Field<uint32_t>(g_info, kState) = c.refuseEvery;
        Field<uint32_t>(g_info, kState + 4) = 0;
        Game<void>(addr::SC4QueryTool_BuildRouteLine, tool, static_cast<void*>(&g_source), &trip, Float(at[0]),
                   Float(at[1]), Float(at[2]), c.backArg, c.mask);
        int32_t runs = 0, points = 0;
        HashLine(f, line, runs, points);
        totalRuns += runs;
        totalPoints += points;
    }
    Game<void>(addr::SC4RouteLine_Clear, line);
    Virtual<uint32_t>(line, 0x8);
    f.Add(g_unexpected);
    g_log = nullptr;
    log::Write("autotest: query8: routes %08X (%d runs, %d points)", f.h, totalRuns, totalPoints);
    out.Add(f.h);
}

// --- 5. The route line on real trips ---------------------------------------------------------

constexpr uint32_t kTypeNetwork = 0xC772BF98;

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

// Buildings (every third cell, up to six) and network pieces (every 37th,
// up to three, referenced), as the fourth piece's test samples them.
void Collect(std::vector<void*>& buildings, std::vector<void*>& networks, int32_t sizeX, int32_t sizeZ)
{
    void* const lotManager = Global<void*>(addr::SC4_LotManager);
    std::vector<void*> lots;
    for (int32_t z = 0; z < sizeZ && buildings.size() < 6; z += 3) {
        for (int32_t x = 0; x < sizeX && buildings.size() < 6; x += 3) {
            void* const lot = Virtual<void*>(lotManager, 0x18, x, z, uint32_t{0});
            void* const building = lot ? Virtual<void*>(lot, 0xC0) : nullptr;
            void* const occupant = building ? Virtual<void*>(building, 0xC) : nullptr;
            if (!occupant || std::find(lots.begin(), lots.end(), lot) != lots.end())
                continue;
            lots.push_back(lot);
            buildings.push_back(occupant);
        }
    }
    void** const sentinel = static_cast<void**>(Virtual<void*>(ListNodeAllocator(), 0xC, uint32_t{0xC}));
    sentinel[0] = sentinel;
    sentinel[1] = sentinel;
    void* list = sentinel;
    int32_t first[2] = {0, 0};
    int32_t last[2] = {sizeX - 1, sizeZ - 1};
    void* const occupants = Global<void*>(addr::SC4_NetworkService);
    Virtual<void>(occupants, 0xA4, &list, static_cast<int32_t*>(first), static_cast<int32_t*>(last), uint32_t{0},
                  uint32_t{0xFFFFFFFF});
    void** const end = static_cast<void**>(list);
    int32_t seen = 0;
    for (void** node = static_cast<void**>(end[0]); node != end; node = static_cast<void**>(node[0])) {
        void* const occupant = node[2];
        if (Virtual<uint32_t>(occupant, 0x1C) != kTypeNetwork)
            continue;
        if (++seen % 37 != 1 || networks.size() >= 3)
            continue;
        Virtual<uint32_t>(occupant, 0x4);
        networks.push_back(occupant);
    }
    Virtual<void>(occupants, 0xC0, &list);
    Game<void>(addr::SC4List_Destroy2, &list);
}

// The occupant at +0x2C replaced by `occupant` (referenced), its own
// highlight at +0x40, as OnMouseMove leaves it.
void SetOccupant(char* tool, void* occupant)
{
    if (void* const old = At<void*>(tool, field::kOccupant))
        Virtual<uint32_t>(old, 0x8);
    At<void*>(tool, field::kOccupant) = occupant;
    if (occupant) {
        Virtual<uint32_t>(occupant, 0x4);
        At<uint32_t>(tool, field::kField40) = Virtual<uint32_t>(occupant, 0x40);
    } else {
        At<uint32_t>(tool, field::kField40) = 0;
    }
}

void PartReal(Fnv& out, cISC4City* city)
{
    char* const view = CityView();
    if (!view || !city || !Global<void*>(addr::SC4_LotManager)) {
        log::Write("autotest: query8: real: no city view");
        return;
    }
    const int32_t sizeX = static_cast<int32_t>(city->CellCountX());
    const int32_t sizeZ = static_cast<int32_t>(city->CellCountZ());
    std::vector<void*> buildings, networks;
    Collect(buildings, networks, sizeX, sizeZ);
    Fnv f;
    f.Add(static_cast<uint32_t>(buildings.size()));
    f.Add(static_cast<uint32_t>(networks.size()));
    Virtual<uint8_t>(view, 0x68, uint32_t{0});
    Virtual<uint8_t>(view, 0x1C, uint32_t{0x6A935CF4}, static_cast<void*>(nullptr), uint32_t{0});
    char* const tool = Virtual<char*>(view, 0x60);
    const bool ours = tool && At<std::uintptr_t>(tool, 0) == addr::SC4QueryTool_Vtable;
    f.Add(uint8_t{ours});
    std::string counts;
    if (ours) {
        Virtual<uint32_t>(tool, 0x4);
        const auto route = [&](void* occupant) {
            SetOccupant(tool, occupant);
            f.Add(Game<uint8_t>(addr::SC4QueryTool_ShowRoutes, tool));
            void* const line = At<void*>(tool, field::kObject9C);
            f.Add(uint8_t{line != nullptr});
            if (!line)
                return;
            int32_t runs = 0, points = 0;
            HashLine(f, line, runs, points);
            char text[32];
            std::snprintf(text, sizeof text, "%d/%d ", runs, points);
            counts += text;
        };
        const auto all = [&] {
            for (void* const building : buildings)
                route(building);
            for (void* const network : networks)
                route(network);
        };
        all();
        // The window's "all routes" box (the way back too) checked, then the
        // radio set back.
        void* const panel = At<void*>(tool, field::kPanel);
        f.Add(uint8_t{panel != nullptr});
        if (panel) {
            char target[tool_query::panel_target::kSize] = {};
            At<char*>(target, tool_query::panel_target::kTool) = tool;
            const auto send = [&](uint32_t id) {
                uint32_t message[4] = {3, 0x287259F6, id, 0};
                f.Add(Game<uint8_t>(addr::SC4QueryPanelTarget_OnMessage, target, panel,
                                    static_cast<uint32_t*>(message)));
                f.Add(Cdecl<uint8_t>(addr::SC4Win_IsChildChecked, panel, uint32_t{0xEBB0F666}));
            };
            send(0xEBB0F666);
            counts += "| ";
            all();
            send(0xABB0F65F);
        }
        SetOccupant(tool, nullptr);
        Virtual<uint8_t>(view, 0x68, uint32_t{0});
        Virtual<uint32_t>(tool, 0x8);
    } else {
        Virtual<uint8_t>(view, 0x68, uint32_t{0});
    }
    for (void* const network : networks)
        Virtual<uint32_t>(network, 0x8);
    log::Write("autotest: query8: real %08X (%d buildings, %d network pieces; runs/points %s)", f.h,
               static_cast<int32_t>(buildings.size()), static_cast<int32_t>(networks.size()), counts.c_str());
    out.Add(f.h);
}

void Drive(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_Q8") || !game::IsBuild641())
        return;
    if (!Global<void*>(addr::SC4_City)) {
        log::Write("autotest: query8: no city");
        return;
    }
    const int parts = EnvInt("OPENSC4_TEST_Q8_PARTS", 31);
    SetUpFakes();
    g_unexpected = 0;
    Fnv total;
    if (parts & 1)
        PartHeight(total);
    if (parts & 2)
        PartBar(total);
    if (parts & 4)
        PartPainter(total);
    if (parts & 8)
        PartRoutes(total);
    if (parts & 16)
        PartReal(total, day.city);
    total.Add(g_unexpected);
    std::snprintf(g_token, sizeof g_token, " query8 %08X", total.h);
    log::Write("autotest: query8: total %08X (parts %d, unexpected slots %d)", total.h, parts, g_unexpected);
}

const DayHook kQuery8{"query8", 73, &Drive};

void Fingerprint(cISC4City*, std::string& line)
{
    line += g_token;
}

const FingerprintPart kFingerprint{"query8", 35, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
