// The terrain object's part of the autotest (src/core/autotest_hooks.h), for
// simulators.terrain.init, .messages, .save and .object
// (src/core/terrain_object.cpp, terrain_messages.cpp, terrain_save.cpp).
//
// The fingerprint part "terrainobj" prints what Init, the constructor and
// the messages set and the "terrain" part does not show, read from the
// object's memory: the flags (initialized, post city init done, loaded),
// the grid's derived sizes, the size type and its float, the neighbour
// offsets, which of the object's pointers are set (one bit each, never an
// address), the byte sizes of its three vectors, the rows (count, bytes),
// the eight phases Init draws, the zoom, the display bytes and the brush
// table's size; then, as the token T, what the timer message drives (+0xA8,
// +0xD4, +0xD8, the number of pending rectangles and the checksum of the
// rows, the water distance map the timer refills): these follow the timer
// service's time, not game days, and do not repeat between two runs of the
// original (the elapsed time differs from the first day on): only the
// flooding byte is printed unless OPENSC4_TEST_TERRAIN_TIMERS=1.
// Left out: the reference count +0x10 (whoever holds the terrain at the
// moment), the gathered rectangle +0xC4 (unset until the first gathering;
// in the "terrainedit" part), +0x134 to +0x18F and +0x1DC (never set by the
// code read so far), and what "terrain" and "terrainedit" print already
// (cells, heights, sea level, water vertices, type, +0xC0, +0xDD, the scrub
// set).
//
// OPENSC4_TEST_TERRAIN_MESSAGES=<n>: on the n-th day of the run, the
// messages DoMessage handles, as message objects given to the DoMessage of
// the object's +4 part as it is then (ours or the game's), in a fixed
// order, with a state checksum after each: the two that do nothing; post
// city init (done already: nothing); the zoom with the value it has, with
// another and back; the timer with a fixed time; the erosion message with
// kind 2 (nothing but the redisplay), 1 and 0; then, unless
// OPENSC4_TEST_TERRAIN_RECONCILE=0, the terrain's own reconcile message
// 0x2A612643 and slot 89 with (0, 1, 1) as DoMessage calls it.
// Not sent: the seven cheat commands (0x0C4A6AF0 to 0x0C4A6AF6): they need
// the command service's argument and answer sets, which only that service
// makes (no made-up objects).
// ReconcileEdges can ask through dialogs when the neighbours' edges differ:
// the calls run under the autotest's modal stand-in, which answers 0 and
// logs; a dialog that does not go through the window manager's DoModalWin
// would wait for a person.
// The erosion changes the land, so the "terrain" part differs from an
// unedited run's afterwards.
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/terrain_sim.h"

#include <opensc4/addresses.h>

#include <cIGZMessage2Standard.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace opensc4::autotest {
namespace {

using terrain_sim::AtAddress;
using terrain_sim::RecordAddress;
namespace field = terrain_sim::field;
namespace vertex = terrain_sim::vertex;
namespace water = terrain_sim::water;

// Fields terrain_sim.h does not have yet (see terrain_object.cpp and
// terrain_messages.cpp).
constexpr std::size_t kPostCityInitDone = 0x14, kSimulators = 0x20, kTwiceCellsX = 0x60, kCitySizeType = 0x58,
                      kSizeTypeFloat = 0x78, kByte80 = 0x80, kNeighbours = 0x84, kInitByte = 0xA4, kElapsed = 0xA8,
                      kChanged = 0xAC, kFlooding = 0xD4, kTicks = 0xD8, kByteDC = 0xDC, kCells = 0xE0,
                      kVectorA = 0x10C, kVectorB = 0x118, kVectorC = 0x124, kPart130 = 0x130, kRows = 0x190,
                      kPendingRects = 0x1B0, kPhases = 0x1B4, kScrubbedCount = 0x1D8;

constexpr uint32_t kNothing1 = 0x66956816, kNothing2 = 0x6716A4E6, kPostCityInit = 0x26D31EC1,
                   kZoomChanged = 0xA6B79602, kTimer = 0x86AD10EE, kDoErosion = 0x470BD214,
                   kReconcileNow = 0x2A612643;
constexpr int kReconcileSlot = 89;

struct Hash {
    uint32_t h = 2166136261u;
    void Mix(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    }
};

uint32_t U32(char* terrain, std::size_t offset) { return At<uint32_t>(terrain, offset); }

// The terrain object once it exists (also when shut down: the fingerprint
// then shows what is left).
char* Terrain() { return game::IsBuild641() ? Global<char*>(addr::SC4_Terrain) : nullptr; }

char* LiveTerrain()
{
    char* const terrain = Terrain();
    if (!terrain || !At<uint8_t>(terrain, field::kInitialized) || !U32(terrain, field::kVertices) ||
        !At<void*>(terrain, field::kWater) || !At<uint32_t>(At<void*>(terrain, field::kWater), water::kRecords))
        return nullptr;
    return terrain;
}

// Elements of a vector (begin, end at +4) of `size`-byte elements; -1 when
// the pointers make no sense.
int32_t VectorCount(char* vector, uint32_t size)
{
    const uint32_t begin = At<uint32_t>(vector, 0x0), end = At<uint32_t>(vector, 0x4);
    if (end < begin || (!begin && end))
        return -1;
    return static_cast<int32_t>((end - begin) / size);
}

// The rows +0x190: how many, their bytes in all and a checksum of them.
void Rows(char* terrain, int32_t& count, uint32_t& bytes, uint32_t& checksum)
{
    count = VectorCount(terrain + kRows, 0xC);
    bytes = 0;
    Hash hash;
    if (count > 0 && count <= 0x4000) {
        char* const rows = At<char*>(terrain, kRows);
        for (int32_t i = 0; i < count; ++i) {
            char* const row = rows + static_cast<std::size_t>(i) * 0xC;
            const int32_t length = VectorCount(row, 1);
            if (length < 0 || length > 0x10000) {
                hash.Mix(0xBAD00000u + static_cast<uint32_t>(i));
                continue;
            }
            bytes += static_cast<uint32_t>(length);
            hash.Mix(BytesChecksum(At<char*>(row, 0x0), static_cast<std::size_t>(length)));
        }
    }
    checksum = hash.h;
}

// Nodes of the pending-rectangle list (head +0x1B0; next at +0); -1 for no
// head, 100000 when the walk does not come back.
int32_t PendingCount(char* terrain)
{
    char* const head = At<char*>(terrain, kPendingRects);
    if (!head)
        return -1;
    int32_t count = 0;
    for (char* node = At<char*>(head, 0x0); node && node != head && count < 100000; node = At<char*>(node, 0x0))
        ++count;
    return count;
}

// One bit per pointer field that is set.
uint32_t PointerBits(char* terrain)
{
    constexpr std::size_t kPointers[] = {field::kCity, kSimulators, field::kWater, field::kVertices, kCells,
                                         field::kView, 0xF0,        0xF4,          0xF8,             0xFC,
                                         0x100,        0x104};
    uint32_t bits = 0;
    for (std::size_t i = 0; i < sizeof kPointers / sizeof kPointers[0]; ++i)
        if (At<void*>(terrain, kPointers[i]))
            bits |= 1u << i;
    return bits;
}

// --- OPENSC4_TEST_TERRAIN_MESSAGES ---------------------------------------------

struct Gathered {
    Hash hash;
    uint32_t calls = 0;
    bool ran = false;
};
Gathered g_messages;

// Every vertex (altitude and water depth bits, flags), the sea level and
// the height range.
uint32_t MapState(char* terrain)
{
    Hash hash;
    const uint32_t count = U32(terrain, field::kVertexCount);
    for (uint32_t index = 0; index < count; ++index) {
        const uint32_t record = RecordAddress(terrain_sim::VertexBase(terrain), index);
        hash.Mix(AtAddress<uint32_t>(record));
        hash.Mix(AtAddress<uint32_t>(RecordAddress(terrain_sim::WaterBase(terrain), index) + water::kDepth));
        hash.Mix(AtAddress<uint16_t>(record + vertex::kFlags));
    }
    hash.Mix(U32(terrain, field::kSeaLevel));
    hash.Mix(U32(terrain, field::kMinHeight));
    hash.Mix(U32(terrain, field::kMaxHeight));
    return hash.h;
}

// The object's fields the messages write, without the elapsed time +0xA8
// (logged apart: it follows the timer).
uint32_t ObjectState(char* terrain)
{
    Hash hash;
    hash.Mix(At<uint8_t>(terrain, kPostCityInitDone));
    hash.Mix(U32(terrain, field::kBrushStep));
    hash.Mix(At<uint8_t>(terrain, kFlooding));
    hash.Mix(U32(terrain, kTicks));
    hash.Mix(U32(terrain, field::kWaterVertices));
    hash.Mix(static_cast<uint32_t>(PendingCount(terrain)));
    for (uint32_t i = 0; i < 4; ++i)
        hash.Mix(U32(terrain, kChanged + 4 * i));
    return hash.h;
}

void Note(const char* what, uint32_t answer, char* terrain)
{
    Gathered& g = g_messages;
    const uint32_t map = MapState(terrain), object = ObjectState(terrain);
    g.hash.Mix(answer);
    g.hash.Mix(map);
    g.hash.Mix(object);
    ++g.calls;
    if (EnvInt("OPENSC4_TEST_TERRAIN_TIMERS", 0) != 0)
        log::Write("terrain messages test: %s: answer %08X, state %08X, object %08X, elapsed %u", what, answer, map,
                   object, U32(terrain, kElapsed));
    else
        log::Write("terrain messages test: %s: answer %08X, state %08X, object %08X", what, answer, map, object);
}

// A cRZMessage2Standard of the type, with data 1 or data 2 or void 2 set
// as asked, to the DoMessage of the terrain's +4 part (its slot 3); the
// answer's low byte.
enum class With { Nothing, Data1, Data2, Void2 };

uint32_t Send(char* terrain, uint32_t type, With with, uint32_t data, void* pointer)
{
    using NewFn = void*(__cdecl*)(std::size_t size);
    using MessageFn = cIGZMessage2Standard*(__thiscall*)(void* memory);
    using DoMessageFn = uint8_t(__thiscall*)(void* target, void* message);
    void* const memory = reinterpret_cast<NewFn>(addr::RZ_operator_new)(0x2C);
    cIGZMessage2Standard* const message =
        memory ? reinterpret_cast<MessageFn>(addr::cGZMessage_cGZMessage)(memory) : nullptr;
    if (!message)
        return 0xFFFFFFFFu;
    message->AddRef();
    message->SetType(type);
    switch (with) {
    case With::Data1: message->SetData1(static_cast<intptr_t>(data)); break;
    case With::Data2: message->SetData2(static_cast<intptr_t>(data)); break;
    case With::Void2: message->SetVoid2(pointer); break;
    case With::Nothing: break;
    }
    void* const target = terrain + 0x4;
    const uint8_t answer = reinterpret_cast<DoMessageFn>((*static_cast<void***>(target))[3])(target, message);
    message->Release();
    return answer;
}

uint32_t g_dialogs = 0;

uint32_t AnswerDialog(void* window, int depth)
{
    ++g_dialogs;
    log::Write("terrain messages test: a dialog opened (window %s, depth %d); answered 0", window ? "set" : "null",
               depth);
    return 0;
}

void Reconcile(void* context)
{
    char* const terrain = static_cast<char*>(context);
    log::Write("terrain messages test: about to send the reconcile message");
    const uint32_t answer = Send(terrain, kReconcileNow, With::Nothing, 0, nullptr);
    Note("message 2A612643 (reconcile now)", answer, terrain);
    log::Write("terrain messages test: about to call slot 89 (0, 1, 1)");
    using ReconcileFn = void(__thiscall*)(void*, uint32_t, uint32_t, uint32_t);
    reinterpret_cast<ReconcileFn>((*reinterpret_cast<void***>(terrain))[kReconcileSlot])(terrain, 0u, 1u, 1u);
    Note("ReconcileEdgesWithNeighboringCities(0, 1, 1)", 0, terrain);
}

void Messages(char* terrain)
{
    Gathered& g = g_messages;
    g.ran = true;
    log::Write("terrain messages test: %u x %u cells, zoom %u, ticks %u, pending %d", U32(terrain, field::kCellsX),
               U32(terrain, field::kCellsZ), U32(terrain, field::kBrushStep), U32(terrain, kTicks),
               PendingCount(terrain));
    Note("before", 0, terrain);

    uint32_t answer = Send(terrain, kNothing1, With::Nothing, 0, nullptr);
    Note("message 66956816 (nothing)", answer, terrain);
    answer = Send(terrain, kNothing2, With::Nothing, 0, nullptr);
    Note("message 6716A4E6 (nothing)", answer, terrain);
    answer = Send(terrain, kPostCityInit, With::Nothing, 0, nullptr);
    Note("message 26D31EC1 (post city init)", answer, terrain);

    // The zoom: void 2 points at the new value.
    const uint32_t zoom = U32(terrain, field::kBrushStep);
    uint32_t value = zoom;
    answer = Send(terrain, kZoomChanged, With::Void2, 0, &value);
    Note("message A6B79602 (zoom, the same)", answer, terrain);
    value = zoom == 0 ? 1 : zoom - 1;
    answer = Send(terrain, kZoomChanged, With::Void2, 0, &value);
    Note("message A6B79602 (zoom, another)", answer, terrain);
    value = zoom;
    answer = Send(terrain, kZoomChanged, With::Void2, 0, &value);
    Note("message A6B79602 (zoom, back)", answer, terrain);

    // The timer: data 2 is the elapsed time.
    answer = Send(terrain, kTimer, With::Data2, 1000, nullptr);
    Note("message 86AD10EE (timer, 1000)", answer, terrain);

    // Erosion: data 1 is the kind.
    answer = Send(terrain, kDoErosion, With::Data1, 2, nullptr);
    Note("message 470BD214 (erosion kind 2: redisplay only)", answer, terrain);
    answer = Send(terrain, kDoErosion, With::Data1, 1, nullptr);
    Note("message 470BD214 (erosion kind 1)", answer, terrain);
    answer = Send(terrain, kDoErosion, With::Data1, 0, nullptr);
    Note("message 470BD214 (erosion kind 0)", answer, terrain);

    if (EnvInt("OPENSC4_TEST_TERRAIN_RECONCILE", 1) != 0) {
        if (!RunModalPlayer(&AnswerDialog, &Reconcile, terrain))
            log::Write("terrain messages test: no window manager; the reconcile calls were not made");
        g.hash.Mix(g_dialogs);
    }
    log::Write("terrain messages test: %u notes, checksum %08X, %u dialogs", g.calls, g.hash.h, g_dialogs);
}

void Day_(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_TERRAIN_MESSAGES"))
        return;
    char* const terrain = LiveTerrain();
    if (!terrain) {
        log::Write("terrain messages test: no terrain; nothing done");
        return;
    }
    Messages(terrain);
}

// --- The fingerprint part ------------------------------------------------------

void Fingerprint(cISC4City*, std::string& line)
{
    char* const terrain = Terrain();
    if (!terrain) {
        line += " | terrainobj none";
        return;
    }
    char text[512];
    Hash neighbours;
    for (uint32_t i = 0; i < 8; ++i)
        neighbours.Mix(U32(terrain, kNeighbours + 4 * i));
    Hash phases;
    for (uint32_t i = 0; i < 8; ++i)
        phases.Mix(U32(terrain, kPhases + 4 * i));
    int32_t rows = 0;
    uint32_t rowBytes = 0, rowChecksum = 0;
    Rows(terrain, rows, rowBytes, rowChecksum);
    std::snprintf(
        text, sizeof text,
        " | terrainobj i%u,%u,%u g%u,%u,%u,%u,%u,%u,%u r%d,%d,%d,%d m%08X,%08X y%u,%08X n%08X b%u,%u,%u,%u z%u o%03X "
        "v%d,%d,%d q%u,%u w%d,%u h%08X c%u B%u",
        static_cast<unsigned>(At<uint8_t>(terrain, field::kInitialized)),
        static_cast<unsigned>(At<uint8_t>(terrain, kPostCityInitDone)),
        static_cast<unsigned>(At<uint8_t>(terrain, field::kLoaded)), U32(terrain, field::kLastCellX),
        U32(terrain, field::kLastCellZ), U32(terrain, field::kVertsX), U32(terrain, field::kVertsZ),
        U32(terrain, field::kVertexCount), U32(terrain, kTwiceCellsX), U32(terrain, kTwiceCellsX + 4),
        At<int32_t>(terrain, field::kCellRect), At<int32_t>(terrain, field::kCellRect + 4),
        At<int32_t>(terrain, field::kCellRect + 8), At<int32_t>(terrain, field::kCellRect + 12),
        U32(terrain, field::kSizeX), U32(terrain, field::kSizeZ), U32(terrain, kCitySizeType),
        U32(terrain, kSizeTypeFloat), neighbours.h, static_cast<unsigned>(At<uint8_t>(terrain, kInitByte)),
        static_cast<unsigned>(At<uint8_t>(terrain, kByte80)), static_cast<unsigned>(At<uint8_t>(terrain, kByteDC)),
        static_cast<unsigned>(At<uint8_t>(terrain, field::kViewActive)), U32(terrain, field::kBrushStep),
        PointerBits(terrain), VectorCount(terrain + kVectorA, 1), VectorCount(terrain + kVectorB, 1),
        VectorCount(terrain + kVectorC, 1), static_cast<unsigned>(At<uint8_t>(terrain, kPart130)),
        U32(terrain, kPart130 + 4), rows, rowBytes, phases.h, U32(terrain, kScrubbedCount),
        Global<uint32_t>(addr::cSTETerrain_Brushes + 4));
    line += text;
    std::snprintf(text, sizeof text, " F%u", static_cast<unsigned>(At<uint8_t>(terrain, kFlooding)));
    line += text;
    if (EnvInt("OPENSC4_TEST_TERRAIN_TIMERS", 0) != 0) {
        std::snprintf(text, sizeof text, " T%u,%u,%u,%d,%08X", U32(terrain, kElapsed),
                      static_cast<unsigned>(At<uint8_t>(terrain, kFlooding)), U32(terrain, kTicks),
                      PendingCount(terrain), rowChecksum);
        line += text;
    }
    if (g_messages.ran) {
        std::snprintf(text, sizeof text, " M%08X/%u/%u", g_messages.hash.h, g_messages.calls, g_dialogs);
        line += text;
    }
}

const DayHook kMessages{"terrainobj", 115, &Day_};
const FingerprintPart kFingerprint{"terrainobj", 116, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
