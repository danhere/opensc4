// Our cSTETerrain setters (build 641): the slots of the city's terrain object
// (vtable 0x00AB3D18, the service 0x00B43CF4) that change its own state
// directly: the terrain type, the altitudes (one vertex, a rectangle, the
// whole map), the altitude locks, the hidden triangles and the other cell
// flags, the gathering of texture updates, the two display states, the long
// operation and brush scrub brackets, the sea level, and the two whole-map
// shapers (CreateFlatAreas, CreateShoreCliffs). 24 slots of the primary
// vtable; the fields are in terrain_sim.h (additions below, to be merged).
//
// No code outside the class calls any of these functions directly and no
// other vtable holds them (scan of every direct call and of every data
// pointer in the exe), so there are no detours. Inside the class they call
// each other through the vtable, as the game does (SetAltitude(rect) calls
// slot 50 for each vertex, ChangeSeaLevel calls slot 45, the scrub brackets
// call slots 87 and 88, SetTerrainCellFlags calls slot 94).
//
// What stays the game's and is called at its address:
// - everything that is drawing or belongs to the terrain view (the Renderer
//   thread's): slot 68 (0x00749850, the redisplay of a rectangle, which
//   posts the "terrain changed" messages; called through the vtable), the
//   refresh of a rectangle 0x007490B0 (normals, textures, the view), and the
//   view's 0x00752AF0 (a cell's flags changed);
// - the water object's functions (0x0073D180, 0x0073EBA0): another class;
// - the machinery of the two shapers, shared with the brushes and erosion
//   (piece 3): 0x00749660, 0x00743210, 0x00745020, 0x0074AD20, 0x0074AB60,
//   0x00749FA0 and the small flag walkers 0x007409B0, 0x00741DE0, 0x00740A20,
//   0x00740F70 with their callbacks. CreateFlatAreas and CreateShoreCliffs
//   here are the loops, the flags and the order of those calls;
// - the shared containers (std::deque<uint32_t> 0x00745980, 0x006DB440,
//   0x006DB4A0; the brush map's find 0x00497520) and the allocator.
//
// Floating point. As in terrain_queries.cpp: the game computes on the x87 at
// 53-bit precision, so what it keeps on the FPU stack is a double here and a
// float exactly where it stores one (FSTP/FST dword). The clamps compare the
// value still on the stack (not the stored float) with the limit. The limits
// and steps are floats in .data, read from the game's memory at each use.
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/terrain_sim.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <iterator>

namespace opensc4::terrain_sim {

// --- Additions for terrain_sim.h (to be merged there) --------------------------

namespace field {
// The set of vertices touched since BeginBrushScrub: an STLport red-black
// tree (header node pointer, then the count). Slots 59 and 60 (0x00747A30,
// 0x00747BB0) empty it; the brushes fill it (not analysed here).
constexpr std::size_t kScrubbed = 0x1D4;
constexpr std::size_t kScrubbedCount = 0x1D8;
} // namespace field

namespace vertex {
constexpr std::size_t kFlagsHigh = 0x23; // the high byte of the flags word (the lock is its 0x40)
} // namespace vertex

namespace flag {
// Working marks of the whole-map shapers; they clear them when done.
constexpr uint16_t kMark1 = 0x0001; // CreateFlatAreas (0x0074A220) and CreateShoreCliffs (0x0074C0F0): visited
constexpr uint16_t kMark2 = 0x0002; // cleared with kMark1 by CreateFlatAreas (and 0xFC), set by its helpers
constexpr uint16_t kMark4 = 0x0004; // CreateShoreCliffs clears 1, 2 and 4 (and 0xF8)
// Set by CreateShoreCliffs on every vertex above the sea level with one of
// four neighbours below it (0x00741B80); cleared on the whole map first
// (0x007409B0) and NOT cleared afterwards.
constexpr uint16_t kShore = 0x0010;
} // namespace flag

namespace slot {
constexpr std::size_t kSetSeaLevel = 0xB4;         // slot 45
constexpr std::size_t kSetAltitudeAtIndex = 0xC8;  // slot 50
constexpr std::size_t kRedisplayRect = 0x110;      // slot 68 (bool, bool, const int32_t rect[4], uint32_t)
constexpr std::size_t kBeginLongOperation = 0x15C; // slot 87
constexpr std::size_t kEndLongOperation = 0x160;   // slot 88
constexpr std::size_t kSetFlipped2 = 0x178;        // slot 94
} // namespace slot

// A brush record (a node of the map at addr::cSTETerrain_Brushes).
namespace brush {
constexpr std::size_t kSoundArgument = 0x14; // dword given with the sounds
constexpr std::size_t kLoopSound = 0x74;     // started by BeginBrushScrub, stopped by EndBrushScrub; 0 for none
constexpr std::size_t kEndSound = 0x78;      // played by EndBrushScrub; 0 for none
} // namespace brush

// The messages the setters post (cIGZMessage2Standard, to the message server
// 0x00B43CCC; data 1 to 4 as given).
namespace message {
constexpr uint32_t kTerrainTypeChanged = 0xC922EB91;  // data 1 = the new type
constexpr uint32_t kSeaLevelChanged = 0x89171496;     // no data
constexpr uint32_t kShowWaterChanged = 0x6AB97795;    // data 1 = the new state (0 or 1)
constexpr uint32_t kLongOperationBegins = 0xEA3B0AF1; // no data
constexpr uint32_t kLongOperationEnds = 0x6A3B0AFD;   // no data
} // namespace message

// The sound service's commands (its slot 0x2C; SC4::Sound43CB8).
namespace sound {
constexpr uint32_t kPlay = 0xEA451903; // (kPlay, sound, argument, 0, 0)
constexpr uint32_t kStop = 0x6A451908; // (kStop, argument, 0, 0, 0)
} // namespace sound

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

inline float FloatAt(uint32_t address) { return AtAddress<float>(address); }

inline uint32_t U32(void* self, std::size_t offset) { return At<uint32_t>(self, offset); }

// A record's offset in the vertex and water arrays, as the game computes it
// (index * 9 * 4, 32-bit, wrapping). Nothing here tests an index's range
// unless the game does.
inline uint32_t RecordOffset(uint32_t index) { return index * 9u * 4u; }

inline bool Locked(uint32_t record) { return (AtAddress<uint8_t>(record + vertex::kFlagsHigh) & 0x40) != 0; }

// Slot 68 through the vtable (the game's; it does nothing unless the view is
// active, +0xEC).
inline void RedisplayRect(char* self, uint32_t first, uint32_t second, void* rect, uint32_t last)
{
    Virtual<void>(self, slot::kRedisplayRect, first, second, rect, last);
}

// The view's 0x00752AF0 when the view is active: its arguments are (z, x).
inline void TellViewCellChanged(char* self, uint32_t x, uint32_t z)
{
    if (At<uint8_t>(self, field::kViewActive))
        This<void>(addr::cSTETerrainView3D_CellFlagsChanged, At<void*>(self, field::kView), z, x);
}

// 0x00743380 (cdecl): a new standard message with the type and four data
// values, sent through the message server (its slot 4, with 0) and released.
// Nothing at all without a message server. As the game, no test of the
// allocation (it would fault on a null message as the game does).
void Post(uint32_t type, uint32_t data1, uint32_t data2, uint32_t data3, uint32_t data4)
{
    void* const server = Global<void*>(addr::SC4_MessageServer);
    if (!server)
        return;
    void* const memory = Cdecl<void*>(addr::RZ_operator_new, 0x2Cu);
    void* const posted = memory ? This<void*>(addr::cGZMessage_cGZMessage, memory) : nullptr;
    Virtual<uint32_t>(posted, 0x4);
    Virtual<void>(posted, 0x14, type);
    Virtual<void>(posted, 0x2C, data1);
    Virtual<void>(posted, 0x40, data2);
    Virtual<void>(posted, 0x54, data3);
    Virtual<void>(posted, 0x68, data4);
    Virtual<void>(server, 0x10, posted, 0u);
    Virtual<uint32_t>(posted, 0x8);
}

// 0x00744AF0: lowers an unlocked vertex by `amount`, not below the minimum
// allowed height, and sets its water depth to sea level - altitude (also a
// negative one). The clamp tests the difference before it is rounded.
void LowerVertex(char* self, uint32_t index, float amount)
{
    const uint32_t offset = RecordOffset(index);
    const uint32_t record = VertexBase(self) + offset;
    if (Locked(record))
        return;
    const double lowered = static_cast<double>(FloatAt(record)) - static_cast<double>(amount);
    const uint32_t wet = WaterBase(self) + offset;
    AtAddress<float>(record) = static_cast<float>(lowered);
    if (lowered < static_cast<double>(Global<float>(addr::cSTETerrain_MinAllowedHeightValue)))
        AtAddress<float>(record) = Global<float>(addr::cSTETerrain_MinAllowedHeightValue);
    AtAddress<float>(wet) = static_cast<float>(static_cast<double>(At<float>(self, field::kSeaLevel)) -
                                               static_cast<double>(FloatAt(record)));
}

// 0x00744CF0: the water depths of a rectangle of vertices from the sea level:
// sea level - altitude below it, else 0. The rectangle's far ends are read
// again as the game does (each column, each row); no range test.
void ResetWaterDepths(char* self, const int32_t* rect)
{
    for (int32_t z = rect[1]; z <= rect[3]; ++z) {
        for (int32_t x = rect[0]; x <= rect[2]; ++x) {
            const uint32_t offset =
                RecordOffset(U32(self, field::kVertsX) * static_cast<uint32_t>(z) + static_cast<uint32_t>(x));
            const uint32_t record = VertexBase(self) + offset;
            const uint32_t wet = WaterBase(self) + offset;
            if (FloatAt(record) < At<float>(self, field::kSeaLevel))
                AtAddress<float>(wet) = static_cast<float>(static_cast<double>(At<float>(self, field::kSeaLevel)) -
                                                           static_cast<double>(FloatAt(record)));
            else // also when unordered
                AtAddress<uint32_t>(wet) = 0;
        }
    }
}

// 0x00743990: frees a subtree of the scrub set (right branch first, by
// recursion; then on down the left).
void FreeScrubNodes(char* node)
{
    while (node) {
        FreeScrubNodes(At<char*>(node, 0xC));
        char* const next = At<char*>(node, 0x8);
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(node));
        node = next;
    }
}

// The start of both scrub brackets (inlined in 0x00747A30 and 0x00747BB0):
// empties the set of scrubbed vertices.
void ClearScrubbed(char* self)
{
    if (U32(self, field::kScrubbedCount) == 0)
        return;
    FreeScrubNodes(At<char*>(At<char*>(self, field::kScrubbed), 0x4));
    At<char*>(At<char*>(self, field::kScrubbed), 0x8) = At<char*>(self, field::kScrubbed);
    At<char*>(At<char*>(self, field::kScrubbed), 0x4) = nullptr;
    At<char*>(At<char*>(self, field::kScrubbed), 0xC) = At<char*>(self, field::kScrubbed);
    At<uint32_t>(self, field::kScrubbedCount) = 0;
}

// The brush record of an exemplar ID, null for an unknown one. The game gives
// the find the address of its own argument.
char* FindBrush(uint32_t* id)
{
    char* node = nullptr;
    This<void>(addr::SC4Uint32Map_Find, reinterpret_cast<void*>(addr::cSTETerrain_Brushes), &node, id);
    return node == Global<char*>(addr::cSTETerrain_Brushes) ? nullptr : node;
}

// --- The terrain type and the display states ---------------------------------

// Slot 41 (0x00749A00): nothing for the type it has; else stores it, posts
// the change, refreshes the whole terrain (0x007490B0 with 0, 0, cells x,
// cells z) and redisplays that rectangle (slot 68 with 0, 0, rect, 0).
// Always answers 1.
uint8_t __fastcall SetTerrainType(char* self, void*, uint32_t type)
{
    static bool logged = false;
    FirstCall("SetTerrainType", logged);
    if (U32(self, field::kTerrainType) != type) {
        At<uint32_t>(self, field::kTerrainType) = type;
        Post(message::kTerrainTypeChanged, type, 0, 0, 0);
        int32_t rect[4] = {0, 0, static_cast<int32_t>(U32(self, field::kCellsX)),
                           static_cast<int32_t>(U32(self, field::kCellsZ))};
        This<void>(addr::cSTETerrain_RefreshRect, self, static_cast<void*>(rect));
        RedisplayRect(self, 0, 0, rect, 0);
    }
    return 1;
}

// Slot 84 (0x007434E0): nothing for the state it has; else stores it and
// posts the change. Unlike 0x00743380 this does not look whether there is a
// message server, and adds the reference only to a message that exists.
void __fastcall SetWaterSurfaceDisplayState(char* self, void*, uint8_t show)
{
    static bool logged = false;
    FirstCall("SetWaterSurfaceDisplayState", logged);
    if (At<uint8_t>(self, field::kShowWater) == show)
        return;
    At<uint8_t>(self, field::kShowWater) = show;
    void* const memory = Cdecl<void*>(addr::RZ_operator_new, 0x2Cu);
    void* const posted = memory ? This<void*>(addr::cGZMessage_cGZMessage, memory) : nullptr;
    if (posted)
        Virtual<uint32_t>(posted, 0x4);
    Virtual<void>(posted, 0x14, message::kShowWaterChanged);
    Virtual<void>(posted, 0x2C, static_cast<uint32_t>(show));
    Virtual<void>(Global<void*>(addr::SC4_MessageServer), 0x10, posted, 0u);
    Virtual<uint32_t>(posted, 0x8);
}

// Slot 86 (0x00741FF0): a global, not a field.
void __fastcall SetPlumbingInfoDisplayState(char*, void*, uint8_t show)
{
    static bool logged = false;
    FirstCall("SetPlumbingInfoDisplayState", logged);
    Global<uint8_t>(addr::STE_DebugCells) = show;
}

// Slots 87 and 88 (0x00743B40, 0x007443A0): one message each.
void __fastcall BeginLongTerrainOperation(char*, void*)
{
    static bool logged = false;
    FirstCall("BeginLongTerrainOperation", logged);
    Post(message::kLongOperationBegins, 0, 0, 0, 0);
}

void __fastcall EndLongTerrainOperation(char*, void*)
{
    static bool logged = false;
    FirstCall("EndLongTerrainOperation", logged);
    Post(message::kLongOperationEnds, 0, 0, 0, 0);
}

// --- Texture updates ---------------------------------------------------------

// Slot 78 (0x007420B0): starts gathering with an empty rectangle; nothing
// while already gathering.
void __fastcall StartAccumulatingTextureUpdates(char* self, void*)
{
    static bool logged = false;
    FirstCall("StartAccumulatingTextureUpdates", logged);
    if (At<uint8_t>(self, field::kAccumulating))
        return;
    int32_t* const rect = &At<int32_t>(self, field::kAccumulated);
    rect[1] = 0x7FFFFFFF;
    rect[0] = 0x7FFFFFFF;
    At<uint8_t>(self, field::kAccumulating) = 1;
    rect[3] = INT32_MIN;
    rect[2] = INT32_MIN;
}

// Slot 79 (0x00749A70): stops gathering; for a rectangle that is not empty
// the refresh (0x007490B0) and the redisplay (slot 68 with 0, 0, rect, 0),
// then the rectangle is emptied. An empty one is left as it is.
void __fastcall StopAccumulatingTextureUpdates(char* self, void*)
{
    static bool logged = false;
    FirstCall("StopAccumulatingTextureUpdates", logged);
    if (!At<uint8_t>(self, field::kAccumulating))
        return;
    int32_t* const rect = &At<int32_t>(self, field::kAccumulated);
    At<uint8_t>(self, field::kAccumulating) = 0;
    if (rect[0] > rect[2] || rect[1] > rect[3])
        return;
    This<void>(addr::cSTETerrain_RefreshRect, self, static_cast<void*>(rect));
    RedisplayRect(self, 0, 0, rect, 0);
    rect[1] = 0x7FFFFFFF;
    rect[0] = 0x7FFFFFFF;
    rect[3] = INT32_MIN;
    rect[2] = INT32_MIN;
}

// --- Cell flags --------------------------------------------------------------

// The flags word of a cell (of the vertex that is its first corner); 0 for a
// cell outside the grid (unsigned tests, x first).
inline uint32_t CellFlagsAddress(char* self, uint32_t x, uint32_t z)
{
    if (x >= U32(self, field::kCellsX) || z >= U32(self, field::kCellsZ))
        return 0;
    return VertexBase(self) + RecordOffset(U32(self, field::kVertsX) * z + x) + vertex::kFlags;
}

// What slots 71 to 73 share: the bits set or cleared in the flags' low byte,
// then the view is told, whether or not anything changed.
inline void HideBits(char* self, uint32_t x, uint32_t z, uint8_t hide, uint8_t bits)
{
    const uint32_t flags = CellFlagsAddress(self, x, z);
    if (!flags)
        return;
    if (hide)
        AtAddress<uint8_t>(flags) |= bits;
    else
        AtAddress<uint8_t>(flags) &= static_cast<uint8_t>(~bits);
    TellViewCellChanged(self, x, z);
}

// Slot 71 (0x007418F0): both triangles.
void __fastcall HideTerrainCell(char* self, void*, uint32_t x, uint32_t z, uint8_t hide)
{
    static bool logged = false;
    FirstCall("HideTerrainCell", logged);
    HideBits(self, x, z, hide, 0x60);
}

// Slot 72 (0x00741830).
void __fastcall HideEvenTriangle(char* self, void*, uint32_t x, uint32_t z, uint8_t hide)
{
    static bool logged = false;
    FirstCall("HideEvenTriangle", logged);
    HideBits(self, x, z, hide, 0x20);
}

// Slot 73 (0x00741890).
void __fastcall HideOddTriangle(char* self, void*, uint32_t x, uint32_t z, uint8_t hide)
{
    static bool logged = false;
    FirstCall("HideOddTriangle", logged);
    HideBits(self, x, z, hide, 0x40);
}

// What slots 93 and 94 share: one bit of the flags word set or cleared; the
// word is written and the view told only when it changes.
inline void SetFlipBit(char* self, uint32_t x, uint32_t z, uint8_t on, uint16_t bit)
{
    const uint32_t flags = CellFlagsAddress(self, x, z);
    if (!flags)
        return;
    const uint16_t before = AtAddress<uint16_t>(flags);
    const uint16_t after = on ? static_cast<uint16_t>(before | bit) : static_cast<uint16_t>(before & ~bit);
    if (after == before)
        return;
    AtAddress<uint16_t>(flags) = after;
    TellViewCellChanged(self, x, z);
}

// Slot 93 (0x00741180): the flag 0x1000.
void __fastcall SetFlipped1(char* self, void*, uint32_t x, uint32_t z, uint8_t on)
{
    static bool logged = false;
    FirstCall("SetFlipped1", logged);
    SetFlipBit(self, x, z, on, flag::kFlipped1);
}

// Slot 94 (0x007411F0): the flag 0x2000 (cell flag 8).
void __fastcall SetFlipped2(char* self, void*, uint32_t x, uint32_t z, uint8_t on)
{
    static bool logged = false;
    FirstCall("SetFlipped2", logged);
    SetFlipBit(self, x, z, on, flag::kFlipped2);
}

// Slot 81 (0x00742000): the cell flags 1 (even triangle hidden), 2 (odd
// hidden) and 4 written into the word (the other bits kept), then flag 8
// through slot 94 (given (flags >> 3) & 0xFFFFFF01, of which slot 94 reads
// the low byte), then the view is told. Answers whether the word differs from
// what it was; 0 for a cell outside the grid.
uint8_t __fastcall SetTerrainCellFlags(char* self, void*, uint32_t x, uint32_t z, uint32_t flags)
{
    static bool logged = false;
    FirstCall("SetTerrainCellFlags", logged);
    if (x >= U32(self, field::kCellsX) || z >= U32(self, field::kCellsZ))
        return 0;
    const uint32_t offset = RecordOffset(U32(self, field::kVertsX) * z + x);
    const uint32_t word = VertexBase(self) + offset + vertex::kFlags;
    const uint32_t before = AtAddress<uint16_t>(word);
    uint32_t value = before & 0xFD9Fu;
    if (flags & 1u)
        value |= flag::kEvenHidden;
    if (flags & 2u)
        value |= flag::kOddHidden;
    if (flags & 4u)
        value |= flag::kCell4;
    AtAddress<uint16_t>(word) = static_cast<uint16_t>(value);
    Virtual<void>(self, slot::kSetFlipped2, x, z, (flags >> 3) & 0xFFFFFF01u);
    TellViewCellChanged(self, x, z);
    return AtAddress<uint16_t>(VertexBase(self) + offset + vertex::kFlags) != static_cast<uint16_t>(before) ? 1 : 0;
}

// --- Altitudes ---------------------------------------------------------------

// Slot 52 (0x00741F90): sets or clears the lock (0x4000) of every vertex of a
// rectangle (x0, z0, x1, z1 inclusive, signed); no range test.
void __fastcall LockAltitude(char* self, void*, const int32_t* rect, uint8_t lock)
{
    static bool logged = false;
    FirstCall("LockAltitude", logged);
    for (int32_t z = rect[1]; z <= rect[3]; ++z) {
        for (int32_t x = rect[0]; x <= rect[2]; ++x) {
            const uint32_t high = VertexBase(self) +
                                  RecordOffset(U32(self, field::kVertsX) * static_cast<uint32_t>(z) +
                                               static_cast<uint32_t>(x)) +
                                  vertex::kFlagsHigh;
            if (lock)
                AtAddress<uint8_t>(high) |= 0x40;
            else
                AtAddress<uint8_t>(high) &= 0xBF;
        }
    }
}

// Slot 50 (0x00744E60), SetAltitude(index, altitude): nothing for a locked
// vertex. The altitude is stored as given: the game's code compares it only
// with itself, so nothing clamps it here. Then the water: a vertex under
// water (depth above 0) keeps its water surface (old depth + old altitude -
// new altitude, in double precision, also when that is negative); a dry one
// gets sea level - altitude when the new altitude is below the sea level and
// is left alone otherwise. No range test of the index.
void __fastcall SetAltitudeAtIndex(char* self, void*, uint32_t index, float altitude)
{
    static bool logged = false;
    FirstCall("SetAltitude(index)", logged);
    const uint32_t offset = RecordOffset(index);
    const uint32_t record = VertexBase(self) + offset;
    if (Locked(record))
        return;
    const uint32_t wet = WaterBase(self) + offset;
    const double surface = static_cast<double>(FloatAt(wet)) + static_cast<double>(FloatAt(record));
    AtAddress<uint32_t>(record) = Bits(altitude);
    if (FloatAt(wet) > 0.0f) {
        AtAddress<float>(wet) = static_cast<float>(surface - static_cast<double>(FloatAt(record)));
        return;
    }
    if (FloatAt(record) < At<float>(self, field::kSeaLevel))
        AtAddress<float>(wet) = static_cast<float>(static_cast<double>(At<float>(self, field::kSeaLevel)) -
                                                   static_cast<double>(FloatAt(record)));
}

// Slot 51 (0x00744F90), SetAltitude(x, z, altitude): the vertex index is
// (cells x + 1) * z + x, no range test. A vertex below the wanted altitude is
// raised ((wanted - old) + old as the game computes it, not above the maximum
// allowed height; the clamp tests the sum before it is rounded) and its water
// depth set to sea level - altitude; one above it is lowered by the
// difference rounded to float (0x00744AF0); nothing for an equal one, a NaN
// or a locked vertex.
void __fastcall SetAltitudeAtVertex(char* self, void*, uint32_t x, uint32_t z, float altitude)
{
    static bool logged = false;
    FirstCall("SetAltitude(x, z)", logged);
    const uint32_t index = (U32(self, field::kCellsX) + 1u) * z + x;
    const uint32_t offset = RecordOffset(index);
    const uint32_t record = VertexBase(self) + offset;
    if (FloatAt(record) < altitude) {
        if (Locked(record))
            return;
        const uint32_t wet = WaterBase(self) + offset;
        const double difference = static_cast<double>(altitude) - static_cast<double>(FloatAt(record));
        const double raised = difference + static_cast<double>(FloatAt(record));
        AtAddress<float>(record) = static_cast<float>(raised);
        if (raised > static_cast<double>(Global<float>(addr::cSTETerrain_MaxAllowedHeightValue)))
            AtAddress<float>(record) = Global<float>(addr::cSTETerrain_MaxAllowedHeightValue);
        AtAddress<float>(wet) = static_cast<float>(static_cast<double>(At<float>(self, field::kSeaLevel)) -
                                                   static_cast<double>(FloatAt(record)));
        return;
    }
    if (FloatAt(record) > altitude) {
        const float amount = static_cast<float>(static_cast<double>(FloatAt(record)) - static_cast<double>(altitude));
        LowerVertex(self, index, amount);
    }
}

// Slot 49 (0x00744F10), SetAltitude(rect, altitude, update, how): slot 50
// for every vertex of the rectangle (rows first; the far ends are read again
// each column and row); then, with `update`, the water depths of the
// rectangle from the sea level (0x00744CF0, which drops what slot 50 kept of
// a water surface) and the redisplay (slot 68 with 1, 1, rect, how).
void __fastcall SetAltitudeInRect(char* self, void*, const int32_t* rect, float altitude, uint8_t update, uint32_t how)
{
    static bool logged = false;
    FirstCall("SetAltitude(rect)", logged);
    for (int32_t z = rect[1]; z <= rect[3]; ++z)
        for (int32_t x = rect[0]; x <= rect[2]; ++x)
            Virtual<void>(self, slot::kSetAltitudeAtIndex,
                          U32(self, field::kVertsX) * static_cast<uint32_t>(z) + static_cast<uint32_t>(x), altitude);
    if (!update)
        return;
    ResetWaterDepths(self, rect);
    RedisplayRect(self, 1, 1, const_cast<int32_t*>(rect), how);
}

// Slot 48 (0x00746440), ChangeTerrainLevel(raise, amount): every unlocked
// vertex of the map raised (not above the maximum allowed height) or lowered
// (not below the minimum) by `amount`, its water depth set to sea level -
// altitude; then the whole terrain is redisplayed (slot 68 with 1, 1, the
// cell rectangle at +0x40, 1). The clamps test the sum or difference before
// it is rounded. The minimum and maximum heights (+0x70, +0x74) are not
// touched here.
void __fastcall ChangeTerrainLevel(char* self, void*, uint8_t raise, float amount)
{
    static bool logged = false;
    FirstCall("ChangeTerrainLevel", logged);
    for (uint32_t row = 0; row < U32(self, field::kVertsZ); ++row) {
        const uint32_t stride = U32(self, field::kVertsX);
        const uint32_t start = stride * row;
        for (uint32_t column = 0; column < stride; ++column) {
            const uint32_t offset = RecordOffset(start + column);
            const uint32_t record = VertexBase(self) + offset;
            if (Locked(record))
                continue;
            const uint32_t wet = WaterBase(self) + offset;
            if (raise) {
                const double raised = static_cast<double>(amount) + static_cast<double>(FloatAt(record));
                AtAddress<float>(record) = static_cast<float>(raised);
                if (raised > static_cast<double>(Global<float>(addr::cSTETerrain_MaxAllowedHeightValue)))
                    AtAddress<float>(record) = Global<float>(addr::cSTETerrain_MaxAllowedHeightValue);
            } else {
                const double lowered = static_cast<double>(FloatAt(record)) - static_cast<double>(amount);
                AtAddress<float>(record) = static_cast<float>(lowered);
                if (lowered < static_cast<double>(Global<float>(addr::cSTETerrain_MinAllowedHeightValue)))
                    AtAddress<float>(record) = Global<float>(addr::cSTETerrain_MinAllowedHeightValue);
            }
            AtAddress<float>(wet) = static_cast<float>(static_cast<double>(At<float>(self, field::kSeaLevel)) -
                                                       static_cast<double>(FloatAt(record)));
        }
    }
    RedisplayRect(self, 1, 1, self + field::kCellRect, 1);
}

// --- The sea level -----------------------------------------------------------

// Slot 45 (0x00743550): refuses (0) a level outside the allowed range (the
// floats at 0x00B0D57C and 0x00B0D580, both ends allowed) or a NaN. Else
// stores it, has the water object set every water record anew from it
// (0x0073D180 with the cell rectangle at +0x40), posts the change and
// redisplays the whole terrain (slot 68 with 1, 1, that rectangle, 0).
uint8_t __fastcall SetSeaLevel(char* self, void*, float level)
{
    static bool logged = false;
    FirstCall("SetSeaLevel", logged);
    if (!(level >= Global<float>(addr::cSTETerrain_MinSeaLevel)))
        return 0;
    if (!(level <= Global<float>(addr::cSTETerrain_MaxSeaLevel)))
        return 0;
    At<uint32_t>(self, field::kSeaLevel) = Bits(level);
    This<void>(addr::STE_WaterResetDepths, At<void*>(self, field::kWater), static_cast<void*>(self + field::kCellRect));
    Post(message::kSeaLevelChanged, 0, 0, 0, 0);
    RedisplayRect(self, 1, 1, self + field::kCellRect, 0);
    return 1;
}

// Slot 46 (0x00740850): the sea level one step (the float at 0x00B0D584) up
// or down, rounded to float, through slot 45; its answer.
uint8_t __fastcall ChangeSeaLevel(char* self, void*, uint8_t up)
{
    static bool logged = false;
    FirstCall("ChangeSeaLevel", logged);
    const double step = Global<float>(addr::cSTETerrain_SeaLevelStep);
    const double sea = At<float>(self, field::kSeaLevel);
    const float level = up ? static_cast<float>(step + sea) : static_cast<float>(sea - step);
    return Virtual<uint8_t>(self, slot::kSetSeaLevel, level);
}

// --- The brush scrub brackets ------------------------------------------------

// Slot 59 (0x00747A30), BeginBrushScrub(brush, unused): empties the set of
// scrubbed vertices; for a known brush, with the sound service there, starts
// the brush's looping sound if it has one; then slot 87.
void __fastcall BeginBrushScrub(char* self, void*, uint32_t id, uint32_t)
{
    static bool logged = false;
    FirstCall("BeginBrushScrub", logged);
    ClearScrubbed(self);
    char* const node = FindBrush(&id);
    void* const sounds = Global<void*>(addr::SC4_Sound43CB8);
    if (node && sounds) {
        Virtual<uint32_t>(sounds, 0x4);
        if (At<uint32_t>(node, brush::kLoopSound))
            Virtual<void>(sounds, 0x2C, sound::kPlay, At<uint32_t>(node, brush::kLoopSound),
                          At<uint32_t>(node, brush::kSoundArgument), 0u, 0u);
        Virtual<uint32_t>(sounds, 0x8);
    }
    Virtual<void>(self, slot::kBeginLongOperation);
}

// Slot 60 (0x00747BB0), EndBrushScrub(brush, second): empties the set; for a
// known brush stops its looping sound and plays its end sound (argument + 1);
// then the water object's 0x0073EBA0 with both arguments, then slot 88.
void __fastcall EndBrushScrub(char* self, void*, uint32_t id, uint32_t second)
{
    static bool logged = false;
    FirstCall("EndBrushScrub", logged);
    ClearScrubbed(self);
    char* const node = FindBrush(&id);
    void* const sounds = Global<void*>(addr::SC4_Sound43CB8);
    if (node && sounds) {
        Virtual<uint32_t>(sounds, 0x4);
        if (At<uint32_t>(node, brush::kLoopSound))
            Virtual<void>(sounds, 0x2C, sound::kStop, At<uint32_t>(node, brush::kSoundArgument), 0u, 0u, 0u);
        if (At<uint32_t>(node, brush::kEndSound))
            Virtual<void>(sounds, 0x2C, sound::kPlay, At<uint32_t>(node, brush::kEndSound),
                          At<uint32_t>(node, brush::kSoundArgument) + 1u, 0u, 0u);
        Virtual<uint32_t>(sounds, 0x8);
    }
    This<void>(addr::STE_WaterEndBrushScrub, At<void*>(self, field::kWater), id, second);
    Virtual<void>(self, slot::kEndLongOperation);
}

// --- The whole-map shapers ---------------------------------------------------

// Clears bits of the low flags byte of every vertex (the count is read again
// each time, as the game does).
inline void ClearLowFlags(char* self, uint16_t marks)
{
    const uint8_t keep = static_cast<uint8_t>(~marks);
    for (uint32_t index = 0; index < U32(self, field::kVertexCount); ++index)
        AtAddress<uint8_t>(VertexBase(self) + RecordOffset(index) + vertex::kFlags) &= keep;
}

// Slot 43 (0x0074A220), CreateFlatAreas(redisplay). The minimum and maximum
// heights are found anew (0x00740F70); then every vertex not yet marked
// (flag 1) whose altitude is strictly between sea level + 0.1 * maximum
// height and sea level + 0.6 * maximum height (the factors are the floats at
// 0x00B0D5F8 and 0x00B0D5FC; both bounds rounded to float) is given to
// 0x00749660 with the float 0x00B0D5F4 * the maximum allowed height and 1,
// in index order (rows first). If any of those answered, the marks are spread
// (0x00743210), their bounds taken (0x00740A20, mask 2) and smoothed
// (0x00745020), the marks 1 and 2 are cleared on the whole map, and with
// `redisplay` the whole terrain is redisplayed (slot 68 with 1, 1, the cell
// rectangle, 0). When none answered nothing more happens (the marks are not
// cleared either). The game leaves no defined answer in AL.
void __fastcall CreateFlatAreas(char* self, void*, uint8_t redisplay)
{
    static bool logged = false;
    FirstCall("CreateFlatAreas", logged);
    bool changed = false;
    This<void>(addr::cSTETerrain_UpdateMinMaxHeight, self);
    const double lowProduct = static_cast<double>(Global<float>(addr::cSTETerrain_FlatBandLow)) *
                              static_cast<double>(At<float>(self, field::kMaxHeight));
    const float low = static_cast<float>(lowProduct + static_cast<double>(At<float>(self, field::kSeaLevel)));
    const double highProduct = static_cast<double>(Global<float>(addr::cSTETerrain_FlatBandHigh)) *
                               static_cast<double>(At<float>(self, field::kMaxHeight));
    const float high = static_cast<float>(highProduct + static_cast<double>(At<float>(self, field::kSeaLevel)));
    const float reach = static_cast<float>(static_cast<double>(Global<float>(addr::cSTETerrain_FlatReachFactor)) *
                                           static_cast<double>(Global<float>(addr::cSTETerrain_MaxAllowedHeightValue)));
    for (uint32_t row = 0; row < U32(self, field::kVertsZ); ++row) {
        uint32_t index = U32(self, field::kVertsX) * row;
        for (uint32_t column = 0; column < U32(self, field::kVertsX); ++column, ++index) {
            const uint32_t record = VertexBase(self) + RecordOffset(index);
            if (AtAddress<uint8_t>(record + vertex::kFlags) & flag::kMark1)
                continue;
            if (!(FloatAt(record) > low))
                continue;
            if (!(FloatAt(record) < high))
                continue;
            if (This<uint8_t>(addr::cSTETerrain_FlattenAround, self, index, reach, 1u))
                changed = true;
        }
    }
    if (!changed)
        return;
    This<void>(addr::cSTETerrain_SpreadMarks, self, Global<uint32_t>(addr::cSTETerrain_FlatSpread),
               static_cast<uint32_t>(addr::cSTETerrain_IsMarked1), 0u,
               static_cast<uint32_t>(addr::cSTETerrain_IsLandUnmarked1), 0u, 2u);
    int32_t rect[4];
    This<void>(addr::cSTETerrain_FlaggedBounds, self, static_cast<void*>(rect), 2u);
    This<void>(addr::cSTETerrain_SmoothRect, self, static_cast<void*>(rect),
               Global<uint32_t>(addr::cSTETerrain_FlatSmoothPasses), 2u);
    ClearLowFlags(self, flag::kMark1 | flag::kMark2); // and 0xFC
    if (redisplay)
        RedisplayRect(self, 1, 1, self + field::kCellRect, 0);
}

// A std::deque<uint32_t> of the game (STLport; blocks of 0x80 bytes), as
// CreateShoreCliffs keeps one on its stack.
struct IndexDeque {
    uint32_t startCurrent, startFirst, startLast, startNode;
    uint32_t finishCurrent, finishFirst, finishLast, finishNode;
    uint32_t map, mapSize;
};

// Slot 44 (0x0074C0F0), CreateShoreCliffs(redisplay). The shore flag (0x10)
// is cleared on the whole map (0x007409B0) and set on every vertex above the
// sea level with a neighbour below it (0x00741DE0 with 0x00741B80). Then, in
// index order, every shore vertex not yet marked (flag 1) starts a queue (a
// deque made, emptied and given the vertex, all with the game's functions so
// the allocations are the game's), is marked, and the queue goes through
// 0x0074AD20, 0x0074AB60 and 0x00749FA0; the queue's blocks and map are freed.
// If any 0x00749FA0 answered: the marks are spread (0x00743210 with 8 and the
// flag 4 callbacks), their bounds taken (0x00740A20, mask 2) and smoothed
// (0x00745020 with 0x14 and 2), and with `redisplay` the whole terrain is
// redisplayed. The marks 1, 2 and 4 are always cleared at the end; the shore
// flags stay. Answers whether anything was changed.
uint8_t __fastcall CreateShoreCliffs(char* self, void*, uint8_t redisplay)
{
    static bool logged = false;
    FirstCall("CreateShoreCliffs", logged);
    int32_t rect[4] = {0, 0, static_cast<int32_t>(U32(self, field::kCellsX)),
                       static_cast<int32_t>(U32(self, field::kCellsZ))};
    This<void>(addr::cSTETerrain_ClearFlagsInRect, self, static_cast<void*>(rect), 0x10u);
    This<void>(addr::cSTETerrain_FlagVerticesWhere, self, static_cast<uint32_t>(addr::cSTETerrain_IsShoreVertex), 0u,
               0x10u);
    uint8_t changed = 0;
    for (uint32_t row = 0; row < U32(self, field::kVertsZ); ++row) {
        uint32_t index = U32(self, field::kVertsX) * row;
        for (uint32_t column = 0; column < U32(self, field::kVertsX); ++column, ++index) {
            const uint32_t offset = RecordOffset(index);
            const uint32_t flags = AtAddress<uint16_t>(VertexBase(self) + offset + vertex::kFlags);
            if (flags & flag::kMark1)
                continue;
            if (!(flags & flag::kShore))
                continue;
            IndexDeque queue{};
            This<void>(addr::SC4CellDeque_Construct, &queue, 0u);
            This<void>(addr::SC4CellDeque_Clear, &queue);
            // push_back(index), as inlined at 0x0074C1B8.
            uint32_t current = queue.finishCurrent;
            if (current != queue.finishLast - 4u) {
                if (current)
                    AtAddress<uint32_t>(current) = index;
                queue.finishCurrent = current + 4u;
            } else {
                const uint32_t used = static_cast<uint32_t>(static_cast<int32_t>(queue.finishNode - queue.map) >> 2);
                if (queue.mapSize - used < 2u) {
                    This<void>(addr::SC4CellDeque_ReserveMapAtBack, &queue, 1u, 0u);
                    current = queue.finishCurrent;
                }
                const uint32_t node = queue.finishNode + 4u;
                AtAddress<uint32_t>(node) = Cdecl<uint32_t>(addr::SC4_stl_allocate, 0x80u);
                if (current)
                    AtAddress<uint32_t>(current) = index;
                queue.finishNode = node;
                const uint32_t block = AtAddress<uint32_t>(node);
                queue.finishFirst = block;
                queue.finishCurrent = block;
                queue.finishLast = block + 0x80u;
            }
            AtAddress<uint8_t>(VertexBase(self) + offset + vertex::kFlags) |= static_cast<uint8_t>(flag::kMark1);
            This<void>(addr::cSTETerrain_ShoreCollect1, self, index, static_cast<void*>(&queue), 1u);
            This<void>(addr::cSTETerrain_ShoreCollect2, self, index, static_cast<void*>(&queue), 1u);
            if (This<uint8_t>(addr::cSTETerrain_ShoreApply, self, static_cast<void*>(&queue)))
                changed = 1;
            // The deque's destructor, as inlined at 0x0074C278.
            if (queue.map) {
                const uint32_t end = queue.finishNode + 4u;
                for (uint32_t node = queue.startNode; node < end; node += 4u)
                    if (AtAddress<uint32_t>(node))
                        Cdecl<void>(addr::SC4_Free, AtAddress<uint32_t>(node));
                Cdecl<void>(addr::SC4_Free, queue.map);
            }
        }
    }
    if (changed) {
        This<void>(addr::cSTETerrain_SpreadMarks, self, 8u, static_cast<uint32_t>(addr::cSTETerrain_IsMarked4), 0u,
                   static_cast<uint32_t>(addr::cSTETerrain_IsLandUnmarked4), 0u, 2u);
        This<void>(addr::cSTETerrain_FlaggedBounds, self, static_cast<void*>(rect), 2u);
        // The game calls 0x00745020 here without loading ECX again:
        // 0x00740A20 leaves it as it was (the terrain).
        This<void>(addr::cSTETerrain_SmoothRect, self, static_cast<void*>(rect), 0x14u, 2u);
        if (redisplay)
            RedisplayRect(self, 1, 1, self + field::kCellRect, 0);
    }
    ClearLowFlags(self, flag::kMark1 | flag::kMark2 | flag::kMark4); // and 0xF8
    return changed;
}

// --- Installation ------------------------------------------------------------

struct Slot {
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(index, original, fn) {index, original, reinterpret_cast<void*>(&fn)}

const Slot kSlots[] = {
    OPENSC4_SLOT(41, addr::cSTETerrain_SetTerrainType, SetTerrainType),
    OPENSC4_SLOT(43, addr::cSTETerrain_CreateFlatAreas, CreateFlatAreas),
    OPENSC4_SLOT(44, addr::cSTETerrain_CreateShoreCliffs, CreateShoreCliffs),
    OPENSC4_SLOT(45, addr::cSTETerrain_SetSeaLevel, SetSeaLevel),
    OPENSC4_SLOT(46, addr::cSTETerrain_ChangeSeaLevel, ChangeSeaLevel),
    OPENSC4_SLOT(48, addr::cSTETerrain_ChangeTerrainLevel, ChangeTerrainLevel),
    OPENSC4_SLOT(49, addr::cSTETerrain_SetAltitudeInRect, SetAltitudeInRect),
    OPENSC4_SLOT(50, addr::cSTETerrain_SetAltitudeAtIndex, SetAltitudeAtIndex),
    OPENSC4_SLOT(51, addr::cSTETerrain_SetAltitudeAtVertex, SetAltitudeAtVertex),
    OPENSC4_SLOT(52, addr::cSTETerrain_LockAltitude, LockAltitude),
    OPENSC4_SLOT(59, addr::cSTETerrain_BeginBrushScrub, BeginBrushScrub),
    OPENSC4_SLOT(60, addr::cSTETerrain_EndBrushScrub, EndBrushScrub),
    OPENSC4_SLOT(71, addr::cSTETerrain_HideTerrainCell, HideTerrainCell),
    OPENSC4_SLOT(72, addr::cSTETerrain_HideEvenTriangle, HideEvenTriangle),
    OPENSC4_SLOT(73, addr::cSTETerrain_HideOddTriangle, HideOddTriangle),
    OPENSC4_SLOT(78, addr::cSTETerrain_StartAccumulatingTextureUpdates, StartAccumulatingTextureUpdates),
    OPENSC4_SLOT(79, addr::cSTETerrain_StopAccumulatingTextureUpdates, StopAccumulatingTextureUpdates),
    OPENSC4_SLOT(81, addr::cSTETerrain_SetTerrainCellFlags, SetTerrainCellFlags),
    OPENSC4_SLOT(84, addr::cSTETerrain_SetWaterSurfaceDisplayState, SetWaterSurfaceDisplayState),
    OPENSC4_SLOT(86, addr::cSTETerrain_SetPlumbingInfoDisplayState, SetPlumbingInfoDisplayState),
    OPENSC4_SLOT(87, addr::cSTETerrain_BeginLongTerrainOperation, BeginLongTerrainOperation),
    OPENSC4_SLOT(88, addr::cSTETerrain_EndLongTerrainOperation, EndLongTerrainOperation),
    OPENSC4_SLOT(93, addr::cSTETerrain_SetFlipped1, SetFlipped1),
    OPENSC4_SLOT(94, addr::cSTETerrain_SetFlipped2, SetFlipped2),
};

#undef OPENSC4_SLOT

// Checks that every slot still holds the original, then swaps them all.
bool SwapSlots()
{
    if (!game::IsBuild641())
        return false;
    auto* const vtable = reinterpret_cast<std::uintptr_t*>(addr::cSTETerrain_vftable);
    for (const Slot& slot : kSlots) {
        if (vtable[slot.index] != slot.original) {
            log::Write("terrain edit: slot %d of %08X holds %08X, expected %08X; not installing", slot.index,
                       static_cast<unsigned>(addr::cSTETerrain_vftable), static_cast<unsigned>(vtable[slot.index]),
                       static_cast<unsigned>(slot.original));
            return false;
        }
    }
    for (const Slot& slot : kSlots) {
        std::uintptr_t* const entry = vtable + slot.index;
        DWORD old = 0;
        if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &old))
            return false;
        *entry = reinterpret_cast<std::uintptr_t>(slot.ours);
        VirtualProtect(entry, sizeof(*entry), old, &old);
    }
    log::Write("terrain edit: %u slots of %08X are ours", static_cast<unsigned>(std::size(kSlots)),
               static_cast<unsigned>(addr::cSTETerrain_vftable));
    return true;
}

bool Install(registry::Mode) { return SwapSlots(); }

const bool g_registered = registry::Register(
    {"simulators.terrain.edit", "simulators/terrain", registry::Kind::Vtable, registry::Phase::PostAppInit,
     addr::cSTETerrain_vftable,
     "Our cSTETerrain setters: terrain type, altitudes and locks, hidden triangles and cell flags, texture update "
     "gathering, display states, long operation and brush scrub brackets, sea level, terrain level, flat areas and "
     "shore cliffs (24 slots of 0x00AB3D18)",
     false, Install});

} // namespace
} // namespace opensc4::terrain_sim
