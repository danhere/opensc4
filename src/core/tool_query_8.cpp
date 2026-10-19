// The query tool of the city view, eighth piece: its last drawing, the
// terrain's clamped height, the bar, the bar painter and the route line.
// Replacement, off by default:
//
//   ui.tools.query8: 4 functions of the query tool
//     (cSC4ViewInputControlQuery, docs/tools.md): the height of the terrain
//     at a point held inside the city (0x004C38D0, the terrain in ESI), the
//     bar a query window shows a share with (0x004C5040), the bar painter's
//     message slot (0x004C6830, slot 3 of 0x00A90B38) and the line of one
//     trip's route (0x004C5E20).
//
// With Q1 to Q7 every function of the query tool's range is ours but for
// the containers, the folded helpers with callers outside the class and the
// jump tables the pieces listed. The line object (0x007DD*, 0x007DC*,
// 0x0053C940), the route iterator, the buffers' blit (GZBuffer::TileBlit)
// and the C runtime's pow stay the game's and are called by address; so are
// these four among themselves, so each reaches ours only through its own
// hook. The route builder (0x004CA460, ui.tools.query4a) calls the height
// and the route line by address too.
//
// Floats: the game runs the x87 at 53 bit (docs/pollution.md, "Floating
// point") and keeps its chains of operations in registers, so ours computes
// each chain in double and rounds to float only where the game stores a
// float. What the terrain's slots and the height give back on the x87 stack
// is kept as a double, as the game keeps it. Integers from floats are the
// game's _ftol (truncation; the integer indefinite for what does not fit).
//
// Every function was written from the disassembly. No random number is
// drawn and no clock is read.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"
#include "core/tool_query.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::tool_query_8 {
namespace {

namespace field = tool_query::field;

// A trip record as a traffic source's list holds it (the node's +8).
namespace trip {
constexpr std::size_t kRouteBack = 0x0C; // the way back's route bytes (the way there at +0)
constexpr std::size_t kCount = 0x18;     // uint32: how many make the trip
constexpr std::size_t kFlags = 0x1E;     // bit 0: no way back of its own
constexpr std::size_t kX = 0x20;         // int16: the trip's cell
constexpr std::size_t kZ = 0x22;
} // namespace trip

// One cell of a route as the game's iterator hands it out (0x006D6510; 10
// bytes written of the 12 the game's frame gives it) and the iterator.
struct RouteStep {
    int16_t x;
    int16_t z;
    uint8_t type;
    uint8_t nextType;
    uint8_t stepped;   // 0: reached by a jump (or the first cell)
    uint8_t side;      // the side entered by, 0xFF none
    uint8_t direction; // the side left by, 0xFF none
    uint8_t jump;
    uint8_t rest[2];
};
static_assert(sizeof(RouteStep) == 12);

struct RouteIterator {
    uint32_t words[6];
};
static_assert(sizeof(RouteIterator) == 0x18);

// The route kinds' table at 0x00B09628 (nine records, by the route cell's
// type; the game reads past them for a type above 8, and so does ours).
struct RouteKind {
    uint32_t color;     // the line's colour; its top byte is replaced
    float fade;         // the alpha's base is 1 less this
    float lift;         // added to the height of the route's points
    uint32_t occupants; // what a network occupant at the cell must be (its slot 9)
    uint32_t pathType;  // what its path information is asked for (slot 5)
};
static_assert(sizeof(RouteKind) == 20);

constexpr uint32_t kBarProperty = 0xEA563AB0; // the window's share (a float), drawn as a bar
constexpr uint32_t kBarTarget = 0xC12CEA13;   // what the window is asked for to draw on
constexpr uint32_t kBarPicture = 0x20732180;  // what the window is asked for the bar's picture
constexpr uint32_t kBufferLock = 0x8010;
constexpr uint32_t kPaint = 0x16;             // the message type the painter draws on

constexpr uint32_t kFerryColor = 0x00A800FF;  // a ferry's colour; the alpha goes in the top byte
constexpr uint32_t kFerryWidth = 0x3E4CCCCD;  // 0.2f
constexpr double kFerryLift = 0.2;            // above the water (0x00A90B98)
constexpr double kFerryFade = 0.75;           // 0x00A90B90
constexpr uint8_t kNoKind = 9;                // no route kind yet
constexpr uint8_t kFerryKind = 0xA;

template <typename R, typename... Args>
R This(std::uintptr_t function, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(function)(self, args...);
}

template <typename R, typename... Args>
R Cdecl(std::uintptr_t function, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(function)(args...);
}

void Release(void* object)
{
    Virtual<uint32_t>(object, 0x8);
}

// x87 fistp as _ftol does: truncation, the "integer indefinite" for what does
// not fit.
int32_t Truncate(double value)
{
    if (!(value > -2147483649.0 && value < 2147483648.0))
        return INT32_MIN;
    return static_cast<int32_t>(value);
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

// The C runtime's _CIpow (0x009EFC60): the base in ST1, the exponent in ST0.
double Pow(double base, double exponent)
{
    double result;
    const std::uintptr_t pow = addr::CRT_CIpow;
    __asm {
        fld base
        fld exponent
        call pow
        fstp result
    }
    return result;
}

// --- 0x004C38D0: the height at a point held inside the city -------------------------------

// The game's 0x004C38D0 has the terrain in ESI and the point (x, z) on the
// stack, which the caller removes; the height is left on the x87 stack. The
// point is held to the terrain: x to [0, the cell size times the cells in x
// (slot 6 times slot 7), stored as a float], z to [0, the same for z (slot
// 8), kept as a double]; a NaN passes, -0 stays. Then the terrain's height
// there (slot 13).
double __cdecl ClampedHeightBody(void* terrain, float x, float z)
{
    const int32_t cellsX = Virtual<int32_t>(terrain, 0x1C);
    const float limitX =
        static_cast<float>(Virtual<double>(terrain, 0x18) * static_cast<double>(static_cast<float>(cellsX)));
    const int32_t cellsZ = Virtual<int32_t>(terrain, 0x20);
    const double limitZ = Virtual<double>(terrain, 0x18) * static_cast<double>(static_cast<float>(cellsZ));
    if (x < 0.0f)
        x = 0.0f;
    if (x >= limitX)
        x = limitX;
    if (z < 0.0f)
        z = 0.0f;
    if (static_cast<double>(z) >= limitZ)
        z = static_cast<float>(limitZ);
    return Virtual<double>(terrain, 0x34, x, z);
}

__declspec(naked) void ClampedHeight()
{
    __asm push dword ptr [esp + 8]
    __asm push dword ptr [esp + 8]
    __asm push esi
    __asm call ClampedHeightBody
    __asm add esp, 12
    __asm ret
}

// 0x004C38D0 by address, the terrain in ESI: (terrain, x, z, function).
__declspec(naked) void HeightThunk()
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

double HeightAt(void* terrain, float x, float z)
{
    // The height comes back on the x87 stack; the game keeps it as a double.
    using ThunkFn = double(__cdecl*)(void*, float, float, std::uintptr_t);
    return reinterpret_cast<ThunkFn>(&HeightThunk)(terrain, x, z, addr::SC4QueryTool_AddClampedPoint);
}

// --- 0x004C5040: the bar ---------------------------------------------------------------------

// The game's 0x004C5040 (cdecl (window, float share)): the window's buffer
// (its slot 3's slot 94) locked (slot 6 with 0x8010) and, when the window
// gives a picture (slot 5 for 0x20732180), the picture's lower half blitted
// over the window's area (the buffer's slot 11), then its upper half over the
// share of the area's width (TileBlit 0x008D8BC0 both times); the buffer
// unlocked (slot 7) and the picture let go. The picture's height (slot 10)
// and width (slot 9) are asked as the game asks them.
void __cdecl DrawBar(void* window, float share)
{
    void* const buffer = Virtual<void*>(Virtual<void*>(window, 0xC), 0x178);
    void* picture = nullptr;
    Virtual<uint32_t>(window, 0x14, kBarPicture, &picture);
    if (Virtual<uint8_t>(buffer, 0x18, kBufferLock)) {
        if (picture) {
            int32_t area[4];
            Virtual<uint32_t>(buffer, 0x2C, static_cast<int32_t*>(area));
            const uint32_t height = Virtual<uint32_t>(picture, 0x28);
            const int32_t bottom = static_cast<int32_t>((height >> 1) - static_cast<uint32_t>(area[1]) +
                                                        static_cast<uint32_t>(area[3]));
            const uint32_t width = Virtual<uint32_t>(picture, 0x24);
            const uint32_t half = Virtual<uint32_t>(picture, 0x28) >> 1;
            int32_t source[4] = {0, static_cast<int32_t>(half), static_cast<int32_t>(width), bottom};
            Cdecl<uint8_t>(addr::GZBuffer_TileBlit, buffer, picture, static_cast<int32_t*>(source),
                           static_cast<int32_t*>(area), int32_t{0}, int32_t{0}, static_cast<void*>(nullptr));
            const int32_t left = area[0];
            const int32_t across = static_cast<int32_t>(static_cast<uint32_t>(area[2]) - static_cast<uint32_t>(left));
            area[2] = static_cast<int32_t>(static_cast<uint32_t>(left) +
                                           static_cast<uint32_t>(Truncate(static_cast<double>(across) *
                                                                          static_cast<double>(share))));
            const int32_t rows = static_cast<int32_t>(static_cast<uint32_t>(area[3]) - static_cast<uint32_t>(area[1]));
            source[0] = 0;
            source[1] = 0;
            source[2] = static_cast<int32_t>(Virtual<uint32_t>(picture, 0x24));
            source[3] = rows;
            Cdecl<uint8_t>(addr::GZBuffer_TileBlit, buffer, picture, static_cast<int32_t*>(source),
                           static_cast<int32_t*>(area), int32_t{0}, int32_t{0}, static_cast<void*>(nullptr));
        }
        Virtual<void>(buffer, 0x1C, kBufferLock);
    }
    if (picture)
        Release(picture);
}

// --- 0x004C6830: the bar painter ---------------------------------------------------------------

// The game's 0x004C6830 (slot 3 of the painter 0x00A90B38, (window,
// message), ret 8; the painter itself is not read): on a message of type
// 0x16, the window's property 0xEA563AB0 (slot 113) and what it gives for
// 0xC12CEA13 (slot 0); with both, the property's float (its slot 81's first
// dword) drawn as a bar there (0x004C5040, by address) and true. Both are let
// go (what was asked for last first) and false when one is missing.
uint8_t __fastcall BarPainterOnMessage(void*, void*, void* window, const uint32_t* message)
{
    if (message[0] != kPaint)
        return 0;
    void* value = nullptr;
    if (!Virtual<uint8_t>(window, 0x1C4, kBarProperty, &value)) {
        if (value)
            Release(value);
        return 0;
    }
    void* target = nullptr;
    if (!Virtual<uint8_t>(window, 0x0, kBarTarget, &target)) {
        if (target)
            Release(target);
        if (value)
            Release(value);
        return 0;
    }
    const uint32_t share = *Virtual<uint32_t*>(value, 0x144);
    Cdecl<void>(addr::SC4QueryTool_DrawBar, target, share);
    if (target)
        Release(target);
    if (value)
        Release(value);
    return 1;
}

// --- 0x004C5E20: the route line ------------------------------------------------------------

struct Point {
    float v[3];
};

// The three floats as the game sets them before they are filled: the dword
// at 0x00B21858, as bits.
Point Unset(uint32_t unset)
{
    Point p;
    for (float& value : p.v)
        std::memcpy(&value, &unset, sizeof value);
    return p;
}

void AddPoint(char* self, const Point& point)
{
    This<void>(addr::SC4RouteLine_AddPoint, At<void*>(self, field::kObject9C), point.v);
}

void BeginRun(char* self, uint32_t color, uint32_t width, uint32_t mode)
{
    This<void>(addr::SC4RouteLine_BeginRun, At<void*>(self, field::kObject9C), color, width, mode);
}

// A run's alpha: (1 - base ^ (the trip's count + 1)) * 255, truncated.
uint32_t Alpha(double base, uint32_t count)
{
    const double exponent = static_cast<double>(count) + 1.0;
    return static_cast<uint8_t>(Truncate((1.0 - Pow(base, exponent)) * 255.0));
}

// A kind's colour with its alpha in the top byte.
uint32_t KindColor(const RouteKind& kind, uint32_t count)
{
    const double base = 1.0 - static_cast<double>(kind.fade);
    return (kind.color & 0x00FFFFFFu) | Alpha(base, count) << 24;
}

uint32_t RunMode(uint8_t reversed)
{
    return reversed ? 1 : 2;
}

// The game's 0x004C5E20 (source, trip, x, y, z, way back, kind mask), ret
// 0x1C: one trip's route as runs of points on the tool's line (+0x9C;
// 0x007DDC50 starts a run, 0x007DDB30 adds a point, both the game's).
//
// The route walked is the way back (+0xC) when it is asked for and the
// record has one (bit 0 of +0x1E clear), else the way there, walked
// backwards along network paths when the way back was asked for. One end is
// the place given (x, z at the terrain's clamped height; y is not read), the
// other the centre of the trip's cell (city slot 97), and the walk starts at
// the trip's cell or the source's first corner cell (its slot 5).
//
// For each cell of the route:
// - reached by a jump (or the first): the traffic simulator's ferry route
//   from the cell before (slot 29); with one, the kind is 0xA and, when the
//   mask's top bit is set, a run of every other cell of the ferry's route at
//   the water's height (the terrain's slot 29) plus 0.2.
// - a network occupant at the cell (the network map's slot 14, a chain at
//   +0x54) of the kind's occupants (its slot 9) with path information (slot
//   49) and a path for the kind's path type between the sides (slot 5, from
//   the last point): the path's ends (or what slot 22 gives for a side of
//   0xFF) at the kind's lift; a new run on a new kind or every eighth cell;
//   on a turn a middle point between the two ends and the path's point
//   (slot 22).
// - else the cell's centre at the clamped height plus the kind's lift; a new
//   run on a new kind, the old one ended at this point.
// A kind not in the mask leaves its cells out. The end is the other end's
// point at the last point's height, when the cell is in the city and a
// kind was drawn.
void __fastcall BuildRouteLine(char* self, void*, void* source, char* record, uint32_t x, uint32_t, uint32_t z,
                               uint32_t back, uint32_t mask)
{
    void* const network = Global<void*>(addr::SC4_Service43D54);
    void* const city = Global<void*>(addr::SC4_City);
    void* const terrain = Virtual<void*>(city, 0x150);
    const uint32_t unset = Global<uint32_t>(addr::SC4QueryTool_DwordB21858);
    Point last = Unset(unset);  // the last point placed
    Point entry = Unset(unset); // a network path's first point
    Point start = Unset(unset);
    Point end = Unset(unset);
    uint8_t lastKind = kNoKind;
    uint32_t count = 0; // cells since the run began
    uint8_t shown = 1;  // the last kind is in the mask
    uint8_t wayBack = 0;
    uint8_t reversed = 0;
    const int32_t lastZ = Virtual<int32_t>(city, 0x178) - 1;
    const int32_t lastX = Virtual<int32_t>(city, 0x174) - 1;
    int32_t corners[4];
    const int32_t* const cells = Virtual<int32_t*>(source, 0x14, static_cast<int32_t*>(corners));
    const int32_t cornerX = cells[0];
    const int32_t cornerZ = cells[1];

    RouteIterator walk;
    int16_t fromX, fromZ, endX, endZ;
    Point* centre;
    if (!(At<uint8_t>(record, trip::kFlags) & 1) && static_cast<uint8_t>(back)) {
        wayBack = 1;
        This<void>(addr::SC4RouteIterator_Init, &walk, record + trip::kRouteBack);
        end.v[0] = Float(x);
        end.v[2] = Float(z);
        fromX = At<int16_t>(record, trip::kX);
        fromZ = At<int16_t>(record, trip::kZ);
        endX = static_cast<int16_t>(cornerX);
        endZ = static_cast<int16_t>(cornerZ);
        // The game asks at the last point, which is not set yet; the end's
        // height is replaced before it is used.
        end.v[1] = static_cast<float>(HeightAt(terrain, last.v[0], last.v[2]));
        centre = &start;
    } else {
        reversed = static_cast<uint8_t>(back);
        This<void>(addr::SC4RouteIterator_Init, &walk, record);
        start.v[0] = Float(x);
        start.v[2] = Float(z);
        fromX = static_cast<int16_t>(cornerX);
        fromZ = static_cast<int16_t>(cornerZ);
        endX = At<int16_t>(record, trip::kX);
        endZ = At<int16_t>(record, trip::kZ);
        start.v[1] = static_cast<float>(HeightAt(terrain, Float(x), Float(z)));
        centre = &end;
    }
    Virtual<void>(city, 0x184, int32_t{At<int16_t>(record, trip::kX)}, int32_t{At<int16_t>(record, trip::kZ)},
                  &centre->v[0], &centre->v[2]);
    last = start;

    const RouteKind* const kinds = reinterpret_cast<const RouteKind*>(addr::SC4QueryTool_RouteKindTable);
    RouteStep step{};
    if (This<uint8_t>(addr::SC4RouteIterator_Next, &walk, &step)) {
        do {
            if (!step.stepped) {
                void* const ferry = Virtual<void*>(Global<void*>(addr::SC4_Service43DA8), 0x74, int32_t{fromX},
                                                   int32_t{fromZ}, int32_t{step.x}, int32_t{step.z});
                if (ferry) {
                    Point water = Unset(Global<uint32_t>(addr::SC4QueryTool_DwordB21858));
                    lastKind = kFerryKind;
                    if (static_cast<int32_t>(mask) < 0) {
                        water.v[1] = static_cast<float>(Virtual<double>(terrain, 0x74) + kFerryLift);
                        BeginRun(self, kFerryColor | Alpha(kFerryFade, At<uint32_t>(record, trip::kCount)) << 24, kFerryWidth, 0);
                        RouteIterator ferryWalk;
                        RouteStep ferryStep{};
                        This<void>(addr::SC4RouteIterator_Init, &ferryWalk, ferry);
                        uint8_t take = 1;
                        while (This<uint8_t>(addr::SC4RouteIterator_Next, &ferryWalk, &ferryStep)) {
                            if (take) {
                                Virtual<void>(city, 0x184, int32_t{ferryStep.x}, int32_t{ferryStep.z}, &water.v[0],
                                              &water.v[2]);
                                AddPoint(self, water);
                            }
                            take = !take;
                        }
                        if (take)
                            AddPoint(self, water);
                    }
                }
            }

            fromX = step.x;
            fromZ = step.z;
            const uint8_t type = step.type;
            const RouteKind& kind = kinds[type];
            const uint32_t bit = uint32_t{1} << (type & 0x1F);
            void* occupant = nullptr;
            for (void** node = Virtual<void**>(network, 0x38, int32_t{step.x}, int32_t{step.z}); node;
                 node = At<void**>(node, 0x54)) {
                if (node[0] && Virtual<uint8_t>(node[0], 0x24, kind.occupants)) {
                    occupant = node[0];
                    break;
                }
            }
            void* const info = occupant ? Virtual<void*>(occupant, 0xC4) : nullptr;
            // The path's side bytes: the callee reads only their low bytes
            // (the game's dwords carry stack bytes above them).
            const uint32_t side = step.side, direction = step.direction;
            char* const path = info ? Virtual<char*>(info, 0x14, kind.pathType, reversed ? direction : side,
                                                     reversed ? side : direction, last.v, uint32_t{0}, uint32_t{0})
                                    : nullptr;
            if (path) {
                // The path's points (a vector: begin, end), read after each
                // call as the game reads them.
                const auto first = [path] { return At<float*>(path, 0); };
                const auto final = [path] { return At<float*>(path, 4) - 3; };
                if (step.side == 0xFF)
                    Virtual<void>(info, 0x58, path, entry.v);
                else
                    std::memcpy(entry.v, reversed ? final() : first(), sizeof entry.v);
                if (step.direction == 0xFF)
                    Virtual<void>(info, 0x58, path, last.v);
                else
                    std::memcpy(last.v, reversed ? first() : final(), sizeof last.v);
                entry.v[1] = static_cast<float>(static_cast<double>(entry.v[1]) + static_cast<double>(kind.lift));
                last.v[1] = static_cast<float>(static_cast<double>(last.v[1]) + static_cast<double>(kind.lift));
                shown = (mask & bit) != 0;
                if (shown) {
                    uint8_t aligned = 0;
                    if (count > 1)
                        aligned = ((static_cast<uint8_t>((step.side & 1) ? step.z : step.x) & 7) == 0) ? 1 : 0;
                    if (type != lastKind || aligned) {
                        BeginRun(self, KindColor(kind, At<uint32_t>(record, trip::kCount)), Bits(kind.lift), RunMode(reversed));
                        count = 0;
                        if (lastKind == kNoKind) {
                            if (wayBack)
                                start.v[1] = static_cast<float>(static_cast<double>(entry.v[1]) -
                                                                static_cast<double>(kind.lift));
                            Point lifted = start;
                            lifted.v[1] =
                                static_cast<float>(static_cast<double>(start.v[1]) + static_cast<double>(kind.lift));
                            AddPoint(self, lifted);
                        }
                        AddPoint(self, entry);
                    }
                    if (step.side != 0xFF && step.direction != 0xFF && step.direction != ((step.side + 2) & 3)) {
                        // A turn: half the path's point (at the lift) and a
                        // quarter of the two ends' sum.
                        Point turn = Unset(Global<uint32_t>(addr::SC4QueryTool_DwordB21858));
                        Virtual<void>(info, 0x58, path, turn.v);
                        const float halfX = static_cast<float>(static_cast<double>(turn.v[0]) * 0.5);
                        const float halfY = static_cast<float>(
                            (static_cast<double>(turn.v[1]) + static_cast<double>(kind.lift)) * 0.5);
                        const double halfZ = static_cast<double>(turn.v[2]) * 0.5;
                        const double sumX = static_cast<double>(last.v[0]) + static_cast<double>(entry.v[0]);
                        const double sumY = static_cast<double>(last.v[1]) + static_cast<double>(entry.v[1]);
                        const float sumZ =
                            static_cast<float>(static_cast<double>(last.v[2]) + static_cast<double>(entry.v[2]));
                        const float quarterX = static_cast<float>(sumX * 0.25);
                        const float quarterY = static_cast<float>(sumY * 0.25);
                        const float quarterZ = static_cast<float>(static_cast<double>(sumZ) * 0.25);
                        turn.v[0] = static_cast<float>(static_cast<double>(quarterX) + static_cast<double>(halfX));
                        turn.v[1] = static_cast<float>(static_cast<double>(quarterY) + static_cast<double>(halfY));
                        turn.v[2] = static_cast<float>(static_cast<double>(quarterZ) + halfZ);
                        AddPoint(self, turn);
                    }
                    AddPoint(self, last);
                }
            } else {
                const int32_t cellX = step.x, cellZ = step.z;
                if (cellX >= 0 && cellZ >= 0 && cellX <= lastX && cellZ <= lastZ) {
                    Point cell = Unset(Global<uint32_t>(addr::SC4QueryTool_DwordB21858));
                    Virtual<void>(city, 0x184, cellX, cellZ, &cell.v[0], &cell.v[2]);
                    cell.v[1] =
                        static_cast<float>(HeightAt(terrain, cell.v[0], cell.v[2]) + static_cast<double>(kind.lift));
                    if (type != lastKind) {
                        if (shown && lastKind < kNoKind)
                            AddPoint(self, cell);
                        shown = (mask & bit) != 0;
                        if (shown) {
                            BeginRun(self, KindColor(kind, At<uint32_t>(record, trip::kCount)), Bits(kind.lift), RunMode(reversed));
                            AddPoint(self, cell);
                            count = 0;
                        }
                    }
                    last = cell;
                }
            }
            lastKind = type;
            ++count;
        } while (This<uint8_t>(addr::SC4RouteIterator_Next, &walk, &step));
        if (!shown)
            return;
    }
    if (endX < 0 || endZ < 0 || endX > lastX || endZ > lastZ || lastKind >= kNoKind)
        return;
    end.v[1] = last.v[1];
    AddPoint(self, end);
}

bool InstallQuery8(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[4] = {};
    static const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::SC4QueryTool_AddClampedPoint, reinterpret_cast<void*>(&ClampedHeight)},
        {addr::SC4QueryTool_DrawBar, reinterpret_cast<void*>(&DrawBar)},
        {addr::SC4QueryTool_BuildRouteLine, reinterpret_cast<void*>(&BuildRouteLine)},
        {addr::SC4QueryBarPainter_OnMessage, reinterpret_cast<void*>(&BarPainterOnMessage)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    static_assert(count <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < count; ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].address);
        if (!found || !hook::Install(*found, hooks[i].ours, &originals[i]))
            return false;
    }
    return true;
}

const bool g_registered = registry::Register({
    "ui.tools.query8",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4QueryTool_BuildRouteLine,
    "Our query tool's drawing: the clamped height, the bar, the bar painter and the route line (docs/tools.md)",
    false,
    InstallQuery8,
});

} // namespace
} // namespace opensc4::tool_query_8
