// Our cSTETerrain messages (build 641): DoMessage of the cIGZMessageTarget2
// part at +4 (vtable 0x00AB3D08 slot 3, 0x0074A470) with what only it calls,
// and slot 89, ReconcileEdgesWithNeighboringCities (0x0074ED40). Group
// simulators.terrain.messages.
//
// What DoMessage does, by message type (always true):
//   0x26D31EC1  slot 40 (DoPostCityInit);
//   0x2A612643  the terrain's own "reconcile the edges now": without a window
//               manager nothing; while the manager's slot 42 is true the
//               message is posted again to the terrain alone; else slot 89
//               with (0, 1, 1);
//   0x470BD214  erosion: 0x00747B20 with data 1 (0 or 1; other values do
//               nothing there) and 0, then slot 68 (1, 1, whole map, 1);
//   0x86AD10EE  the timer the terrain registers for in Init (0xC0): Tick
//               (0x0074A390) with data 2;
//   0xA6B79602  the zoom: the dword void 2 points at into +0xBC and, when it
//               changed, the whole map refreshed (0x007490B0);
//   0x66956816, 0x6716A4E6  nothing;
//   0x0C4A6AF0 to 0x0C4A6AF6  the seven cheat commands Init registers
//               (GetAltitude, SetAltitude, GetCellAltitude, SetCellAltitude,
//               GetSeaLevel, SetSeaLevel, ApplyTerrain...): void 1 is the
//               argument set, void 2 the answer set; each reads its
//               arguments and calls the terrain's own slots. Any other type
//               from 0x0C4A6AF0 up still reads void 1 and void 2.
// Init subscribes to 0x0C4A6AF0 to 0x0C4A6AF2 and 0x0C4A6AF4 to 0x0C4A6AF6
// (not 0x0C4A6AF3) and never to 0x66956816 or 0x2A612643 (the second comes
// addressed to the terrain).
//
// Game bugs kept: the SetAltitude command gives slot 51 the x vertex for
// both coordinates (z is tested for the bounds and then not used).
//
// The game's and called at their addresses: the erosion worker 0x00747B20,
// the flood step 0x007467F0 (a later piece); the water distance update
// 0x007440C0; the refresh 0x007490B0 and slot 68 (the Renderer's side); the
// flag walker 0x007409B0 (the brushes'); everything ReconcileEdges calls:
// the search 0x0074D010, 0x0074B0D0, 0x0074B2D0, the apply 0x00748C90, its
// three dialogs (0x00734FB0 to 0x00735250) and the shared containers.
//
// No code outside the class calls 0x0074A470, 0x0074A390, 0x00749AF0,
// 0x00743400 or 0x0074ED40 directly (scan of every direct call in the exe;
// 0x00743400's two callers and 0x0074A390's and 0x00749AF0's one are in the
// functions replaced here), so there are no detours: two vtable slots.
#include "core/game.h"
#include "core/game_access.h"
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
constexpr std::size_t kElapsed = 0xA8;        // uint32: timer time since the water distances were last updated (Tick)
constexpr std::size_t kChanged = 0xAC;        // int32[4]: joined by 0x007440C0; empty (0x00B0D698) at first
constexpr std::size_t kFlooding = 0xD4;       // uint8: Tick runs the flood step 0x007467F0 while it is set
constexpr std::size_t kTicks = 0xD8;          // int32: Tick counts to 2 once there is a view, then starts ReconcileEdges
constexpr std::size_t kPendingRects = 0x1B0;  // list head (node: next, previous, int32 rect[4], two uint32 timers)
} // namespace field

namespace message {
constexpr uint32_t kPostCityInit = 0x26D31EC1;
constexpr uint32_t kReconcileNow = 0x2A612643;   // posted by the terrain to itself
constexpr uint32_t kDoErosion = 0x470BD214;      // data 1 = kind
constexpr uint32_t kTimer = 0x86AD10EE;          // data 2 = elapsed
constexpr uint32_t kZoomChanged = 0xA6B79602;    // void 2 -> uint32 zoom
constexpr uint32_t kNothing1 = 0x66956816;
constexpr uint32_t kNothing2 = 0x6716A4E6;
constexpr uint32_t kFirstCommand = 0x0C4A6AF0;   // seven commands
constexpr uint32_t kEdgesReconciled = 0xCA63D517; // data: report, found, done, 0
} // namespace message

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

// Slots of the terrain's own vtable that DoMessage calls.
namespace own {
constexpr std::size_t kLocationIsInBounds = 0x2C;   // slot 11 (x, z)
constexpr std::size_t kGetAltitude = 0x34;          // slot 13 (x, z)
constexpr std::size_t kGetAltitudeAtVertex = 0x38;  // slot 14 (x, z)
constexpr std::size_t kGetSeaLevel = 0x74;          // slot 29
constexpr std::size_t kDoPostCityInit = 0xA0;       // slot 40
constexpr std::size_t kSetSeaLevel = 0xB4;          // slot 45
constexpr std::size_t kSetAltitudeInRect = 0xC4;    // slot 49 (rect, altitude, 1, 2)
constexpr std::size_t kSetAltitudeAtVertex = 0xCC;  // slot 51 (x, z, altitude)
constexpr std::size_t kApplyBrush = 0xE4;           // slot 57 (brush, parameters)
constexpr std::size_t kRedisplayRect = 0x110;       // slot 68 (bool, bool, rect, uint32)
constexpr std::size_t kReconcileEdges = 0x164;      // slot 89
} // namespace own

// The message's getters (cIGZMessage2Standard).
constexpr std::size_t kGetType = 0x10, kData1 = 0x24, kVoid1 = 0x28, kData2 = 0x38, kVoid2 = 0x3C;

// The command argument and answer sets (the cheat command service's; slots
// as DoMessage calls them: nothing more is known of the class).
namespace set {
constexpr std::size_t kCount = 0xC;     // ()
constexpr std::size_t kGet = 0x14;      // (index) -> a value object
constexpr std::size_t kHas = 0x18;      // (index) -> non-zero when there is one
constexpr std::size_t kSet = 0x1C;      // (index, value object)
constexpr std::size_t kSetCode = 0x28;  // (code): 2 = failed, 0 = done
} // namespace set
constexpr std::size_t kValueInt = 0x60, kValueFloat = 0x90;

// 0x00743380: a new cRZMessage2Standard with the type and four data values
// through the message server's slot 4 (with 0), then released. Nothing
// without a message server. As the game, no test of the allocation.
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

// 0x00743400: a new message of the type through the message server's slot 7
// (+0x1C) with one target, then released.
void PostTo(uint32_t type, void* target)
{
    void* const server = Global<void*>(addr::SC4_MessageServer);
    if (!server)
        return;
    void* const memory = Cdecl<void*>(addr::RZ_operator_new, 0x2Cu);
    void* const posted = memory ? This<void*>(addr::cGZMessage_cGZMessage, memory) : nullptr;
    Virtual<uint32_t>(posted, 0x4);
    Virtual<void>(posted, 0x14, type);
    Virtual<void>(server, 0x1C, posted, target);
    Virtual<uint32_t>(posted, 0x8);
}

// 0x00741660.
void WholeRect(char* self, int32_t* rect)
{
    rect[1] = 0;
    rect[0] = 0;
    rect[2] = At<int32_t>(self, field::kCellsX);
    rect[3] = At<int32_t>(self, field::kCellsZ);
}

// 0x007404E0.
bool VertexInGrid(char* self, uint32_t x, uint32_t z)
{
    return x < U32(self, field::kVertsX) && z < U32(self, field::kVertsZ);
}

void Join(int32_t* bounds, const int32_t* rect)
{
    if (rect[0] < bounds[0])
        bounds[0] = rect[0];
    if (rect[1] < bounds[1])
        bounds[1] = rect[1];
    if (rect[2] > bounds[2])
        bounds[2] = rect[2];
    if (rect[3] > bounds[3])
        bounds[3] = rect[3];
}

// 0x00749AF0: the rectangles waiting in the list +0x1B0. Each node's first
// timer runs up by the elapsed time; at the expiry limit (0x00B0D624,
// unsigned) the rectangle joins the bounds, its vertices lose flag 0x400
// and the node is unlinked and freed. Before that its second timer runs up
// and at the repeat limit (0x00B0D628) is reset with the rectangle joined.
// Bounds that are not empty are refreshed and redisplayed (slot 68 with
// 0, 0, bounds, 0).
void UpdatePendingRects(char* self, uint32_t elapsed)
{
    {
        char* const head = At<char*>(self, field::kPendingRects);
        if (At<char*>(head, 0x0) == head)
            return;
    }
    int32_t bounds[4] = {INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN};
    char* node = At<char*>(At<char*>(self, field::kPendingRects), 0x0);
    if (node == At<char*>(self, field::kPendingRects))
        return;
    do {
        int32_t* const rect = reinterpret_cast<int32_t*>(node + 0x8);
        const uint32_t age = At<uint32_t>(node, 0x18) + elapsed;
        At<uint32_t>(node, 0x18) = age;
        if (age >= Global<uint32_t>(addr::cSTETerrain_PendingExpiry)) {
            Join(bounds, rect);
            This<void>(addr::cSTETerrain_ClearFlagsInRect, self, static_cast<void*>(rect), 0x400u);
            char* const gone = node;
            char* const next = At<char*>(gone, 0x0);
            char* const previous = At<char*>(gone, 0x4);
            node = next;
            At<char*>(previous, 0x0) = next;
            At<char*>(next, 0x4) = previous;
            Cdecl<void>(addr::SC4_Free, static_cast<void*>(gone));
        } else {
            const uint32_t since = At<uint32_t>(node, 0x1C) + elapsed;
            At<uint32_t>(node, 0x1C) = since;
            if (since >= Global<uint32_t>(addr::cSTETerrain_PendingRepeat)) {
                At<uint32_t>(node, 0x1C) = 0;
                Join(bounds, rect);
            }
            node = At<char*>(node, 0x0);
        }
    } while (node != At<char*>(self, field::kPendingRects));
    if (bounds[0] > bounds[2] || bounds[1] > bounds[3])
        return;
    This<void>(addr::cSTETerrain_RefreshRect, self, static_cast<void*>(bounds));
    Virtual<void>(self, own::kRedisplayRect, 0u, 0u, static_cast<void*>(bounds), 0u);
}

// 0x0074A390: the timer's work. With a view, the first two ticks are
// counted and the second one starts the edge reconciliation: message
// 0x2A612643 to the terrain itself when the options' byte +0x4EC is set
// (no test of the options object, as the game), else message 0xCA63D517
// with zeros. Then the flood step while one runs, the pending rectangles,
// and the elapsed time: from 10,000,000 on (unsigned), unless the changed
// rectangle +0xAC is the empty one, the water distances are updated
// (0x007440C0 with the whole map, which resets the time once the area is
// large enough).
void Tick(char* self, uint32_t elapsed)
{
    static bool logged = false;
    FirstCall("Tick", logged);
    if (At<void*>(self, field::kView) && At<int32_t>(self, field::kTicks) < 2) {
        const int32_t ticks = At<int32_t>(self, field::kTicks) + 1;
        At<int32_t>(self, field::kTicks) = ticks;
        if (ticks == 2) {
            if (At<uint8_t>(Global<char*>(addr::SC4_Options43CD8), 0x4EC))
                PostTo(message::kReconcileNow, self + 0x4);
            else
                Post(message::kEdgesReconciled, 0, 0, 0, 0);
        }
    }
    if (At<uint8_t>(self, field::kFlooding))
        This<void>(addr::cSTETerrain_FloodStep, self);
    UpdatePendingRects(self, elapsed);
    const uint32_t total = U32(self, field::kElapsed) + elapsed;
    U32(self, field::kElapsed) = total;
    if (total < 10000000u)
        return;
    const int32_t* const empty = &Global<int32_t>(addr::cSTETerrain_EmptyRect);
    const int32_t* const changed = &At<int32_t>(self, field::kChanged);
    if (changed[0] == empty[0] && changed[1] == empty[1] && changed[2] == empty[2] && changed[3] == empty[3])
        return;
    This<void>(addr::cSTETerrain_UpdateWaterDistances, self, static_cast<void*>(self + field::kCellRect));
}

// A command's float answer: a value object (0x14 bytes on the stack) made
// from the float, set as the answer's entry 0, destroyed.
void AnswerFloat(void* answer, float value)
{
    char number[0x14];
    void* const made = This<void*>(addr::SC4ScriptNumber_FromFloat, static_cast<void*>(number), Bits(value));
    Virtual<void>(answer, set::kSet, 0u, made);
    This<void>(addr::SC4ScriptNumber_Destroy, static_cast<void*>(number));
}

float FloatArgument(void* arguments, uint32_t index)
{
    return Virtual<float>(Virtual<void*>(arguments, set::kGet, index), kValueFloat);
}

uint32_t IntArgument(void* arguments, uint32_t index)
{
    return Virtual<uint32_t>(Virtual<void*>(arguments, set::kGet, index), kValueInt);
}

// The floats go to the terrain's slots as their bits, as the game pushes
// the dwords it stored.
bool InBounds(char* self, float x, float z)
{
    return Virtual<uint8_t>(self, own::kLocationIsInBounds, Bits(x), Bits(z)) != 0;
}

// The seven commands (0x0074A5E0 to 0x0074AAD6), in the game's order of
// calls. `arguments` is the message's void 1, `answer` its void 2.
void Command(char* self, uint32_t which, void* arguments, void* answer)
{
    switch (which) {
    case 0: { // GetAltitude x z
        const float x = FloatArgument(arguments, 0);
        const float z = FloatArgument(arguments, 1);
        if (!InBounds(self, x, z)) {
            Virtual<void>(answer, set::kSetCode, 2u);
            return;
        }
        const float altitude = Virtual<float>(self, own::kGetAltitude, Bits(x), Bits(z));
        AnswerFloat(answer, altitude);
        return;
    }
    case 1: { // SetAltitude x z altitude
        const float x = FloatArgument(arguments, 0);
        const float z = FloatArgument(arguments, 1);
        if (!InBounds(self, x, z)) {
            Virtual<void>(answer, set::kSetCode, 2u);
            return;
        }
        const float altitude = FloatArgument(arguments, 2);
        // The game's: x / 16 truncated, given as both vertex coordinates.
        const uint32_t vertex = static_cast<uint32_t>(
            Ftol64(static_cast<double>(x) * static_cast<double>(Global<float>(addr::SC4_FloatSixteenth))));
        Virtual<void>(self, own::kSetAltitudeAtVertex, vertex, vertex, Bits(altitude));
        return;
    }
    case 2: { // GetCellAltitude x z
        const uint32_t x = IntArgument(arguments, 0);
        const uint32_t z = IntArgument(arguments, 1);
        if (!VertexInGrid(self, x, z)) {
            Virtual<void>(answer, set::kSetCode, 2u);
            return;
        }
        const float altitude = Virtual<float>(self, own::kGetAltitudeAtVertex, x, z);
        AnswerFloat(answer, altitude);
        return;
    }
    case 3: { // SetCellAltitude x z [x2 z2] altitude
        const uint32_t x = IntArgument(arguments, 0);
        const uint32_t z = IntArgument(arguments, 1);
        uint32_t x2 = x, z2 = z, altitudeAt = 2;
        if (Virtual<uint32_t>(arguments, set::kCount) >= 4) {
            x2 = IntArgument(arguments, 2);
            z2 = IntArgument(arguments, 3);
            altitudeAt = 4;
        }
        if (!VertexInGrid(self, x, z) || !VertexInGrid(self, x2, z2)) {
            Virtual<void>(answer, set::kSetCode, 2u);
            return;
        }
        const float altitude = FloatArgument(arguments, altitudeAt);
        uint32_t rect[4] = {x, z, x2, z2};
        Virtual<void>(self, own::kSetAltitudeInRect, static_cast<void*>(rect), Bits(altitude), 1u, 2u);
        return;
    }
    case 4: { // GetSeaLevel
        const float level = Virtual<float>(self, own::kGetSeaLevel);
        AnswerFloat(answer, level);
        return;
    }
    case 5: { // SetSeaLevel level
        const float level = FloatArgument(arguments, 0);
        if (!Virtual<uint8_t>(self, own::kSetSeaLevel, Bits(level)))
            Virtual<void>(answer, set::kSetCode, 2u);
        return;
    }
    case 6: { // ApplyTerrain... brush x1 z1 x2 z2 [strength1 strength2]
        Virtual<void>(answer, set::kSetCode, 2u);
        if (Virtual<uint32_t>(arguments, set::kCount) < 5)
            return;
        const float z1 = FloatArgument(arguments, 2);
        const float x1 = FloatArgument(arguments, 1);
        const float z2 = FloatArgument(arguments, 4);
        const float x2 = FloatArgument(arguments, 3);
        if (!InBounds(self, x1, z1))
            return;
        if (!InBounds(self, x2, z2))
            return;
        const uint32_t brush = IntArgument(arguments, 0);
        // tBrushParameters (0x38 bytes): the constructor's defaults, then
        // the stroke from (x1, z1) to (x2, z2).
        char parameters[0x38];
        This<void*>(addr::SC4BrushParameters_Construct, static_cast<void*>(parameters));
        At<float>(parameters, 0x0) = x1;
        At<float>(parameters, 0xC) = z2;
        At<float>(parameters, 0x4) = z1;
        At<float>(parameters, 0x8) = x2;
        At<uint32_t>(parameters, 0x10) = 0xFFFFFFFFu;
        At<uint8_t>(parameters, 0x14) = 1;
        if (Virtual<uint32_t>(arguments, set::kHas, 5u))
            At<float>(parameters, 0x18) = FloatArgument(arguments, 5);
        else
            At<float>(parameters, 0x18) = 1.0f;
        if (Virtual<uint32_t>(arguments, set::kHas, 6u))
            At<float>(parameters, 0x1C) = FloatArgument(arguments, 6);
        else
            At<float>(parameters, 0x1C) = 1.0f;
        At<uint32_t>(parameters, 0x20) = 2;
        At<float>(parameters, 0x34) = Virtual<float>(self, own::kGetAltitude, Bits(x2), Bits(z2));
        if (!Virtual<uint8_t>(self, own::kApplyBrush, brush, static_cast<void*>(parameters)))
            return;
        Virtual<void>(answer, set::kSetCode, 0u);
        return;
    }
    default:
        return;
    }
}

// Slot 3 of the +4 vtable (0x0074A470).
uint8_t __fastcall DoMessage(char* part, void*, void* received)
{
    static bool logged = false;
    FirstCall("DoMessage", logged);
    char* const self = part - 0x4;
    const uint32_t type = Virtual<uint32_t>(received, kGetType);
    switch (type) {
    case message::kNothing1:
    case message::kNothing2:
        return 1;
    case message::kPostCityInit:
        Virtual<void>(self, own::kDoPostCityInit);
        return 1;
    case message::kReconcileNow: {
        void* const windows = Cdecl<void*>(addr::SC4_GetWinManager);
        if (!windows)
            return 1;
        if (Virtual<uint8_t>(windows, 0xA8)) {
            PostTo(message::kReconcileNow, self ? part : nullptr);
            return 1;
        }
        Virtual<void>(self, own::kReconcileEdges, 0u, 1u, 1u);
        return 1;
    }
    case message::kDoErosion: {
        const uint32_t kind = Virtual<uint32_t>(received, kData1);
        This<void>(addr::cSTETerrain_Erode, self, kind, 0u);
        int32_t rect[4];
        WholeRect(self, rect);
        Virtual<void>(self, own::kRedisplayRect, 1u, 1u, static_cast<void*>(rect), 1u);
        return 1;
    }
    case message::kTimer:
        Tick(self, Virtual<uint32_t>(received, kData2));
        return 1;
    case message::kZoomChanged: {
        const uint32_t zoom = *Virtual<uint32_t*>(received, kVoid2);
        if (zoom == U32(self, field::kBrushStep))
            return 1;
        U32(self, field::kBrushStep) = zoom;
        int32_t rect[4];
        WholeRect(self, rect);
        This<void>(addr::cSTETerrain_RefreshRect, self, static_cast<void*>(rect));
        return 1;
    }
    default:
        break;
    }
    if (type < message::kFirstCommand)
        return 1;
    void* const arguments = Virtual<void*>(received, kVoid1);
    void* const answer = Virtual<void*>(received, kVoid2);
    const uint32_t which = type - message::kFirstCommand;
    if (which > 6)
        return 1;
    static bool commandLogged = false;
    FirstCall("command", commandLogged);
    Command(self, which, arguments, answer);
    return 1;
}

// --- ReconcileEdgesWithNeighboringCities ---------------------------------------

// The game's hash set as it lies on the stack here (0x14 bytes; the bucket
// storage at +4 is freed after the clear).
struct HashSet {
    uint32_t first;
    void* buckets;
    uint32_t words[3];
};
static_assert(sizeof(HashSet) == 0x14);

void MakeHashSet(HashSet& hash)
{
    // The game leaves the first word as the stack had it; zero here.
    hash = HashSet{};
    This<void>(addr::SC4Uint32HashSet_Construct, &hash, 0x64u);
}

void DestroyHashSet(HashSet& hash)
{
    This<void>(addr::SC4Uint32HashSet_Clear, &hash);
    if (hash.buckets)
        Cdecl<void>(addr::SC4_Free, hash.buckets);
}

// One of the three 8-byte reference-counted dialog helpers: operator new,
// its constructor, a reference; null when the allocation fails.
void* MakeDialog(std::uintptr_t constructor)
{
    void* const memory = Cdecl<void*>(addr::SC4_operator_new, 8u);
    if (!memory)
        return nullptr;
    void* const dialog = This<void*>(constructor, memory);
    if (dialog)
        Virtual<uint32_t>(dialog, 0x4);
    return dialog;
}

void ReleaseDialog(void* dialog)
{
    if (dialog)
        Virtual<uint32_t>(dialog, 0x8);
}

// Slot 89 (0x0074ED40): (report, ask, arg3). Nothing unless the
// application's slot 23 is true. The differences along the edges are
// gathered into a set (0x0074D010, with arg3). With none: the "nothing to
// do" dialog when `report`. With some: when `report` is clear and `ask`
// set, the question (its answer decides); then the changes are collected
// (0x0074B0D0), with `ask` prepared (0x0074B2D0) and confirmed by a second
// dialog (the view's +0xC slot 9 after it), and applied (0x00748C90) when
// still wanted. Message 0xCA63D517 (report, found, done, 0) at the end.
// The dialogs' functions run with a null object when their allocation
// failed, as the game's code does.
void __fastcall ReconcileEdges(char* self, void*, uint32_t reportArgument, uint32_t askArgument, uint32_t arg3)
{
    static bool logged = false;
    FirstCall("ReconcileEdgesWithNeighboringCities", logged);
    const uint8_t report = static_cast<uint8_t>(reportArgument);
    const uint8_t ask = static_cast<uint8_t>(askArgument);
    if (!Cdecl<uint32_t>(addr::SC4UI_Service43C94Slot23))
        return;
    // The set of differences: a tree (head, count) with an 0x18-byte head.
    struct {
        char* head;
        uint32_t count;
    } found{nullptr, 0};
    found.head = Cdecl<char*>(addr::SC4_stl_allocate, 0x18u);
    found.count = 0;
    At<uint8_t>(found.head, 0x0) = 0;
    At<char*>(found.head, 0x4) = nullptr;
    At<char*>(found.head, 0x8) = found.head;
    At<char*>(found.head, 0xC) = found.head;
    HashSet second;
    MakeHashSet(second);
    HashSet first;
    MakeHashSet(first);
    This<void>(addr::cSTETerrain_FindEdgeDifferences, self, static_cast<void*>(&found), arg3);
    const uint8_t any = found.count != 0 ? 1 : 0;
    uint8_t wanted = any;
    if (!any) {
        if (report) {
            void* const dialog = MakeDialog(addr::SC4EdgeDialog_ConstructNone);
            This<void>(addr::SC4EdgeDialog_ShowNone, dialog);
            ReleaseDialog(dialog);
        }
    } else {
        if (!report && ask) {
            void* const dialog = MakeDialog(addr::SC4EdgeDialog_ConstructAsk);
            wanted = This<uint8_t>(addr::SC4EdgeDialog_Ask, dialog);
            ReleaseDialog(dialog);
        }
        if (wanted) {
            This<void>(addr::cSTETerrain_CollectEdgeChanges, self, static_cast<void*>(&found),
                       static_cast<void*>(&first), static_cast<void*>(&second));
            if (ask) {
                This<void>(addr::cSTETerrain_PrepareEdgeChanges, self, static_cast<void*>(&first));
                void* const dialog = MakeDialog(addr::SC4EdgeDialog_ConstructConfirm);
                wanted = This<uint8_t>(addr::SC4EdgeDialog_Confirm, dialog);
                if (char* const view = At<char*>(self, field::kView))
                    Virtual<void>(view + 0xC, 0x24);
                ReleaseDialog(dialog);
            }
            if (wanted)
                This<void>(addr::cSTETerrain_ApplyEdgeChanges, self, static_cast<void*>(&found),
                           static_cast<void*>(&second));
        }
    }
    Post(message::kEdgesReconciled, report, any, wanted, 0);
    DestroyHashSet(first);
    DestroyHashSet(second);
    if (found.count) {
        This<void>(addr::cSTETerrain_FreeEdgeNodes, static_cast<void*>(&found), At<void*>(found.head, 0x4));
        At<char*>(found.head, 0x8) = found.head;
        At<char*>(found.head, 0x4) = nullptr;
        At<char*>(found.head, 0xC) = found.head;
        found.count = 0;
    }
    if (found.head)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(found.head));
}

// --- Installation ------------------------------------------------------------

struct Slot {
    std::uintptr_t vtable;
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(vtable, index, original, fn) {vtable, index, original, reinterpret_cast<void*>(&fn)}

const Slot kSlots[] = {
    OPENSC4_SLOT(addr::cSTETerrain_vftable_4, 3, addr::cSTETerrain_DoMessage, DoMessage),
    OPENSC4_SLOT(addr::cSTETerrain_vftable, 89, addr::cSTETerrain_ReconcileEdgesWithNeighboringCities, ReconcileEdges),
};

#undef OPENSC4_SLOT

// Checks that both slots still hold the originals, then swaps them.
bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    for (const Slot& slot : kSlots) {
        const auto* const vtable = reinterpret_cast<const std::uintptr_t*>(slot.vtable);
        if (vtable[slot.index] != slot.original) {
            log::Write("terrain messages: slot %d of %08X holds %08X, expected %08X; not installing", slot.index,
                       static_cast<unsigned>(slot.vtable), static_cast<unsigned>(vtable[slot.index]),
                       static_cast<unsigned>(slot.original));
            return false;
        }
    }
    for (const Slot& slot : kSlots) {
        auto* const entry = reinterpret_cast<std::uintptr_t*>(slot.vtable) + slot.index;
        DWORD old = 0;
        if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &old))
            return false;
        *entry = reinterpret_cast<std::uintptr_t>(slot.ours);
        VirtualProtect(entry, sizeof(*entry), old, &old);
    }
    log::Write("terrain messages: DoMessage (slot 3 of %08X) and slot 89 of %08X are ours",
               static_cast<unsigned>(addr::cSTETerrain_vftable_4), static_cast<unsigned>(addr::cSTETerrain_vftable));
    return true;
}

const bool g_registered = registry::Register(
    {"simulators.terrain.messages", "simulators/terrain", registry::Kind::Vtable, registry::Phase::PostAppInit,
     addr::cSTETerrain_vftable_4,
     "Our cSTETerrain DoMessage (post city init, the edge reconciliation, erosion, the timer with the pending "
     "rectangles, zoom, the seven cheat commands) and ReconcileEdgesWithNeighboringCities (slot 3 of 0x00AB3D08, "
     "slot 89 of 0x00AB3D18)",
     false, Install});

} // namespace
} // namespace opensc4::terrain_sim
