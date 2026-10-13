// The obliterate tool of the city view (its functions between 0x0049C790
// and 0x0049D8A8; the tract iterator's code lies between them and is not
// part of it).
// Replacement, off by default:
//
//   ui.tools.obliterate: the view input control of 0xA8 bytes (vtable
//     0x00A8F6D8, message target 0x00A8F6C4 at +0x28, ID 0xCA58C969, cursor
//     0xF) the view makes current (0x007E9150) when the player answers its
//     question window 0x2A41436C with 0x6A41BBB5, and the occupant filter of
//     its demolitions (0xC bytes, vtables 0x00A8F694 and 0x00A8F67C).
//
// The tool is the scene in which a city is wiped: it takes no clicks (the
// mouse slots are a shared stub that answers true; Escape puts the tool
// away) and works from the frame service's message (0x86AD10EC under 0xC0),
// which carries the microseconds since the one before. Init captures the
// mouse, stops the city from being saved (city slot 104), pauses the
// simulator (HiddenPause), turns the situation manager's switch off (what
// it was is kept at +0xA4) and lays blocks of cells over the city (the
// tuning's size, 8 as shipped) in a random order: a tract iterator in its
// list mode, shuffled with the C runtime's rand().
//
// State 1, the sweep: a curve of the tuning gives, for the seconds since
// Init, how many blocks should be gone; each message takes that many more
// blocks from the iterator and has the demolition service remove what the
// filter lets through in each (DemolishRegion with demolish 1, privilege 0,
// flags 0x2000), with the effect "sparkles_on_demolish" in the middle of a
// block. After the first block the effect is left out for a block without
// occupants, up to six such blocks in a row. When the curve's last time is
// reached or the iterator runs out:
// state 2, the finale: the effect "obliterate_finale" in the middle of the
// city, and after the tuning's time (0.25 s as shipped)
// state 3: every occupant the filter lets through is removed (occupant
// manager slot 22 with 1, 0), the whole city demolished and dezoned
// (PlaceZone with type 0 and the funds, custom and size tests off). When the
// effect says it is over (message 0xEA623590), or 20 seconds later,
// state 4: the tool puts itself away (slot 27) and message 0xEA65224E is
// posted.
// The filter refuses flora (0x74758926) and occupants of type 0x69D25C5F;
// the ground is not touched. Shutdown undoes what Init did.
//
// Time: everything is counted in the frame message's microseconds, which
// are real time; how many blocks a message takes depends on it, the end
// state does not. No stopwatch is read here.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::tool_obliterate {
namespace {

// The base class's fields.
namespace base {
constexpr std::size_t kInited = 0x04; // byte
constexpr std::size_t kId = 0x0C;
constexpr std::size_t kCursor = 0x10; // cursor ID
} // namespace base

// The tool's fields.
namespace field {
constexpr std::size_t kTarget = 0x28;        // the message target part (vtable 0x00A8F6C4)
constexpr std::size_t kFilter = 0x2C;        // the occupant filter (referenced)
constexpr std::size_t kOccupants = 0x30;     // the occupant manager (referenced)
constexpr std::size_t kZones = 0x34;         // the zone manager (referenced)
constexpr std::size_t kDemolition = 0x38;    // the demolition service (referenced)
constexpr std::size_t kLastX = 0x3C;         // the city's last cell along x
constexpr std::size_t kLastZ = 0x40;         // and along z
constexpr std::size_t kState = 0x44;         // 0 made, 1 sweep, 2 finale, 3 wiped, 4 over
constexpr std::size_t kBlocks = 0x48;        // a tract iterator of 0x30 bytes over the blocks
constexpr std::size_t kElapsed = 0x78;       // microseconds since Init
constexpr std::size_t kFinaleTime = 0x7C;    // microseconds of the finale
constexpr std::size_t kCity = 0x80;          // the city's rectangle of cells (x0, z0, x1, z1)
constexpr std::size_t kAsked = 0x90;         // blocks the curve has asked for so far
constexpr std::size_t kWipedTime = 0x94;     // microseconds since the wipe
constexpr std::size_t kFinale = 0x98;        // the finale's effect (referenced)
constexpr std::size_t kDemolished = 0x9C;    // blocks demolished
constexpr std::size_t kEmptyRun = 0xA0;      // blocks without occupants in a row
constexpr std::size_t kSituations = 0xA4;    // byte: the situation manager's switch before Init
} // namespace field

// The tract iterator (TractIterator, 0x30 bytes) as the tool sets it up.
namespace walk {
constexpr std::size_t kState = 0x00;   // byte
constexpr std::size_t kStride = 0x01;  // byte: 0 walks a shuffled list of the cells
constexpr std::size_t kChanged = 0x02; // byte
constexpr std::size_t kRect = 0x04;    // x0, z0, x1, z1
constexpr std::size_t kCells = 0x18;   // the list's storage
constexpr std::size_t kStep = 0x2C;
} // namespace walk

namespace state {
constexpr int32_t kSweep = 1;
constexpr int32_t kFinale = 2;
constexpr int32_t kWiped = 3;
constexpr int32_t kOver = 4;
} // namespace state

constexpr uint32_t kToolId = 0xCA58C969;
constexpr uint32_t kMessageFrame = 0x86AD10EE;       // the frame service's
constexpr uint32_t kMessageFinaleOver = 0xEA623590;  // the finale's effect ended
constexpr uint32_t kMessageSceneOver = 0xEA65224E;   // posted when the tool is done
constexpr uint32_t kFrameCode = 0xC0;                // what the tool asks the frame service under
constexpr uint32_t kTypeFlora = 0x74758926;
constexpr uint32_t kTypeKept = 0x69D25C5F;
constexpr uint32_t kWipedLimit = 20000000;           // microseconds

// What an effect is told where it is: 0x38 bytes on the stack of 0x0049CC60.
// A count of changes, flags (1 the position), a rotation, the position, the
// scale.
struct EffectTransform {
    uint8_t changes;
    uint8_t flags;
    uint8_t unused[2];
    float rotation[9];
    float position[3];
    float scale;
};
static_assert(sizeof(EffectTransform) == 0x38);

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

void AddRef(void* object)
{
    Virtual<uint32_t>(object, 0x4);
}

void Release(void* object)
{
    Virtual<uint32_t>(object, 0x8);
}

// The truncating float-to-int conversion (_ftol, 0x009EEF04): the low dword
// of the int64.
int32_t Ftol(double value)
{
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

// FLD float, FISTP dword (the rounding of the control word in force), as at
// 0x0049D5FE.
__declspec(naked) int32_t __cdecl X87Fistp(float /*value*/)
{
    __asm fld dword ptr [esp + 4]
    __asm fistp dword ptr [esp + 4]
    __asm mov eax, [esp + 4]
    __asm ret
}

// The middle of a run of cells in metres: the two end cells' sum times 16,
// halved towards zero.
float Middle(int32_t first, int32_t last)
{
    return static_cast<float>(((first + last) * 16) / 2);
}

// A reference the tool holds, let go and cleared.
void Drop(char* self, std::size_t offset)
{
    if (void* const held = At<void*>(self, offset)) {
        Release(held);
        At<void*>(self, offset) = nullptr;
    }
}

// A service of the city kept by the tool, referenced when there is one.
void Hold(char* self, std::size_t offset, std::uintptr_t global)
{
    void* const service = Global<void*>(global);
    At<void*>(self, offset) = service;
    if (service)
        AddRef(service);
}

// --- Small slots -----------------------------------------------------------------------

// The game's 0x0049C790 (slot 14, OnKeyDown, ret 8): Escape (0x1B) has the
// tool put itself away (slot 27). True.
uint8_t __fastcall OnKeyDown(char* self, void*, uint32_t key, uint32_t)
{
    if (key == 0x1B)
        Virtual<void>(self, 0x6C);
    return 1;
}

// The game's 0x0049C7C0 (cdecl): what the application calls back (its slot
// 69): the tuning is to be loaded again by the next Init. True.
uint8_t __cdecl TuningReset()
{
    if (Global<uint8_t>(addr::SC4ObliterateTool_TuningLoaded))
        Global<uint8_t>(addr::SC4ObliterateTool_TuningLoaded) = 0;
    return 1;
}

// The game's 0x0049C7E0 (slot 4 of the filter, IsOccupantTypeIncluded, ret
// 4, this not read): every type but flora and 0x69D25C5F.
uint8_t __fastcall FilterIncludesType(char*, void*, uint32_t type)
{
    return (type == kTypeFlora || type == kTypeKept) ? 0 : 1;
}

// --- Tuning, messages, effects ---------------------------------------------------------

// The game's 0x0049C9B0 (cdecl): the tuning (group 0x6534284A, instance
// 0x7A4A8458 and 0xCA5A576F, asked for as a property holder): the block's
// side in cells, the curve of blocks over seconds with its last time, and
// the finale's seconds as microseconds. The seconds are not set before the
// read in the game; they start as 0 here.
void __cdecl LoadTuning()
{
    void* holder = nullptr;
    uint32_t key[3] = {0x6534284A, 0x7A4A8458, 0xCA5A576F};
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, &key[0], uint32_t{0x25216283}, &holder,
                          uint32_t{0}, uint32_t{0}))
        return;
    Cdecl<uint8_t>(addr::SC4_GetPropertySint32, holder, uint32_t{0x6A5A6700},
                   &Global<int32_t>(addr::SC4ObliterateTool_BlockSize));
    if (void* const property = Virtual<void*>(holder, 0x24, uint32_t{0x6A5A6701})) {
        void* const curve = reinterpret_cast<void*>(addr::SC4ObliterateTool_SweepCurve);
        This<uint8_t>(addr::SC4Curve_LoadFromProperty, curve, property);
        This<uint8_t>(addr::SC4Curve_LastX, curve, &Global<float>(addr::SC4ObliterateTool_SweepSeconds));
    }
    float seconds = 0.0f;
    Cdecl<uint8_t>(addr::SC4_GetPropertyFloat, holder, uint32_t{0x6A5A6702}, &seconds);
    // 1000000.0f is the float at 0x00A867AC.
    Global<int32_t>(addr::SC4ObliterateTool_FinaleTime) = Ftol(static_cast<double>(seconds) * 1000000.0);
    Release(holder);
}

// The game's 0x0049CA70 (cdecl): posts a message of a type with three
// numbers. A message that could not be allocated is used as it is, as in
// the game.
void __cdecl Post(uint32_t type, uint32_t first, uint32_t second, uint32_t third)
{
    void* const server = Global<void*>(addr::SC4_MessageServer);
    if (!server)
        return;
    void* const memory = Cdecl<void*>(addr::RZ_operator_new, std::size_t{0x2C});
    void* const message = memory ? This<void*>(addr::cGZMessage_cGZMessage, memory) : nullptr;
    AddRef(message);
    Virtual<void>(message, 0x14, type);
    Virtual<void>(message, 0x2C, first);
    Virtual<void>(message, 0x40, second);
    Virtual<void>(message, 0x54, third);
    Virtual<uint8_t>(server, 0x10, message, uint32_t{0});
    Release(message);
}

// The game's 0x0049CC30: the scene is over, once: state 4, the tool puts
// itself away (slot 27) and message 0xEA65224E is posted.
void __fastcall End(char* self, void*)
{
    if (At<int32_t>(self, field::kState) == state::kOver)
        return;
    At<int32_t>(self, field::kState) = state::kOver;
    Virtual<void>(self, 0x6C);
    Post(kMessageSceneOver, 0, 0, 0);
}

// The game's 0x0049CC60 (cdecl): an effect by name (the effects manager
// 0x00B43D1C, slot 7) at a place (x, z) on the ground (the terrain's
// GetAltitude plus `lift`), started (slot 3 with 0). `out` gets the effect,
// or null when it is not kept or not made.
void __cdecl StartEffect(const char* name, const float* place, float lift, uint8_t keep, void** out)
{
    void* const effects = Global<void*>(addr::SC4_Service43D1C);
    void* const terrain = Global<void*>(addr::SC4_Terrain);
    *out = nullptr;
    if (!effects || !terrain)
        return;
    if (!Virtual<uint8_t>(effects, 0x1C, name, out))
        return;
    const float x = place[0], z = place[1];
    EffectTransform transform = {};
    transform.rotation[0] = 1.0f;
    transform.rotation[4] = 1.0f;
    transform.rotation[8] = 1.0f;
    transform.scale = 1.0f;
    transform.position[1] = static_cast<float>(Virtual<double>(terrain, 0x34, x, z) + static_cast<double>(lift));
    transform.position[2] = z;
    transform.position[0] = x;
    transform.flags |= 0x01;
    transform.changes = static_cast<uint8_t>(transform.changes + 1);
    Virtual<void>(*out, 0x20, &transform);
    Virtual<void>(*out, 0xC, uint32_t{0});
    if (!keep) {
        Release(*out);
        *out = nullptr;
    }
}

// --- The scene's steps -----------------------------------------------------------------

// The game's 0x0049CD90: the finale begins: state 2, its time from 0, the
// effect "obliterate_finale" in the middle of the city's rectangle, kept at
// +0x98 and told to say when it is over (its slot 10 with the message and
// the tool).
void __fastcall StartFinale(char* self, void*)
{
    At<int32_t>(self, field::kState) = state::kFinale;
    At<uint32_t>(self, field::kFinaleTime) = 0;
    const int32_t* const city = &At<int32_t>(self, field::kCity);
    float middle[2];
    middle[0] = Middle(city[0], city[2]);
    middle[1] = Middle(city[1], city[3]);
    void** const finale = &At<void*>(self, field::kFinale);
    StartEffect("obliterate_finale", &middle[0], 0.0f, 1, finale);
    if (*finale)
        Virtual<void>(*finale, 0x28, kMessageFinaleOver, static_cast<void*>(self));
}

// The game's 0x0049CE20 (ret 4): state 3, with a message's microseconds.
// Past 20 seconds since the wipe an effect still there is stopped (slot 4
// with 0) and let go; with no effect left the scene is over (as End).
void __fastcall AfterWipe(char* self, void*, uint32_t elapsed)
{
    const uint32_t time = At<uint32_t>(self, field::kWipedTime) + elapsed;
    At<uint32_t>(self, field::kWipedTime) = time;
    if (time > kWipedLimit) {
        if (void* const finale = At<void*>(self, field::kFinale)) {
            Virtual<void>(finale, 0x10, uint32_t{0});
            Release(At<void*>(self, field::kFinale));
            At<void*>(self, field::kFinale) = nullptr;
        }
    }
    if (!At<void*>(self, field::kFinale))
        End(self, nullptr);
}

// A cell region (0x24 bytes: the rectangle, then the part with a bit a cell
// that 0x004422A0 builds) of a rectangle with every cell set.
void MakeRegion(char* region, const int32_t* rect)
{
    int32_t* const cells = reinterpret_cast<int32_t*>(region);
    cells[0] = rect[0];
    cells[1] = rect[1];
    cells[2] = rect[2];
    cells[3] = rect[3];
    uint8_t fill = 1;
    This<void*>(addr::SC4CellRegion_ConstructCells, region + 0x10, static_cast<uint32_t>(rect[2] - rect[0] + 1),
                static_cast<uint32_t>(rect[3] - rect[1] + 1), &fill);
}

// DemolishRegion (the demolition service's slot 6) as both callers call it:
// demolish 1, no privilege, flags 0x2000, the tool's filter, nothing else.
void DemolishRegion(char* self, char* region)
{
    Virtual<uint8_t>(At<void*>(self, field::kDemolition), 0x18, uint32_t{1}, static_cast<void*>(region), int32_t{0},
                     uint32_t{0x2000}, uint32_t{0}, At<void*>(self, field::kFilter), static_cast<void*>(nullptr),
                     static_cast<void*>(nullptr), static_cast<void*>(nullptr), int32_t{0}, int32_t{0});
}

// The game's 0x0049CEA0 (ret 8): everything the filter lets through in a
// rectangle of cells is demolished, and with `dezone` the rectangle is
// dezoned: the zone manager's PlaceZone (slot 11) with type 0, place 1, the
// funds test, custom zoning and the size test off, whole lots 0 and no
// outputs.
void __fastcall DemolishRect(char* self, void*, const int32_t* rect, uint8_t dezone)
{
    alignas(4) char region[0x24];
    MakeRegion(region, rect);
    DemolishRegion(self, region);
    if (dezone)
        Virtual<uint8_t>(At<void*>(self, field::kZones), 0x2C, static_cast<void*>(region), uint32_t{0}, uint32_t{1},
                         uint32_t{1}, uint32_t{1}, uint32_t{1}, uint32_t{0}, static_cast<void*>(nullptr),
                         static_cast<void*>(nullptr), static_cast<void*>(nullptr));
    At<std::uintptr_t>(region, 0x10) = addr::SC4CellRegion_CellsVtable;
    This<void>(addr::SC4CellRegion_DestroyCells, region + 0x10);
}

// The game's 0x0049D070 (ret 4): state 2, with a message's microseconds.
// When the finale has lasted the tuning's time: state 3, every occupant the
// filter lets through removed (occupant manager slot 22 with 1 and 0), the
// whole city demolished and dezoned, the time since the wipe from 0.
void __fastcall FinaleTick(char* self, void*, uint32_t elapsed)
{
    const uint32_t time = At<uint32_t>(self, field::kFinaleTime) + elapsed;
    At<uint32_t>(self, field::kFinaleTime) = time;
    if (time < Global<uint32_t>(addr::SC4ObliterateTool_FinaleTime))
        return;
    At<int32_t>(self, field::kState) = state::kWiped;
    Virtual<uint8_t>(At<void*>(self, field::kOccupants), 0x58, At<void*>(self, field::kFilter), uint32_t{1},
                     uint32_t{0});
    DemolishRect(self, nullptr, &At<int32_t>(self, field::kCity), 1);
    At<uint32_t>(self, field::kWipedTime) = 0;
}

// The game's 0x0049D0E0 (ret 4): whether the filter lets any occupant of a
// rectangle of cells through (the occupant manager's slot 40 into a list of
// the game's, with 1; the list given back through slot 48).
uint8_t __fastcall BlockHasOccupants(char* self, void*, const int32_t* rect)
{
    char* list = Virtual<char*>(ListNodeAllocator(), 0xC, uint32_t{0xC});
    At<char*>(list, 0x0) = list;
    At<char*>(list, 0x4) = list;
    int32_t low[2] = {rect[0], rect[1]};
    int32_t high[2] = {rect[2], rect[3]};
    Virtual<uint8_t>(At<void*>(self, field::kOccupants), 0xA0, &list, &low[0], &high[0],
                     At<void*>(self, field::kFilter), uint32_t{1});
    const bool empty = At<char*>(list, 0x0) == list;
    Virtual<void>(At<void*>(self, field::kOccupants), 0xC0, &list);
    This<void>(addr::SC4List_Destroy2, &list);
    return empty ? 0 : 1;
}

// The game's 0x0049D5A0: state 1, at a frame message. Past the curve's last
// time the finale begins. Else the curve's blocks for the seconds since
// Init, rounded (FISTP), less those asked for before are taken from the
// iterator: each block's rectangle (held to the city) gets the sparkle
// effect in its middle (not when, after the first block, it has no occupants
// and fewer than six blocks in a row were so) and is demolished. When the
// iterator has no more blocks the finale begins.
void __fastcall SweepTick(char* self, void*)
{
    // 1e-6f is the float at 0x00A86510.
    const double seconds = static_cast<double>(At<uint32_t>(self, field::kElapsed)) * static_cast<double>(1e-6f);
    const float rounded = static_cast<float>(seconds);
    if (!(seconds < static_cast<double>(Global<float>(addr::SC4ObliterateTool_SweepSeconds)))) {
        StartFinale(self, nullptr);
        return;
    }
    const float wanted = static_cast<float>(This<double>(
        addr::SC4Curve_Evaluate, reinterpret_cast<void*>(addr::SC4ObliterateTool_SweepCurve), rounded));
    const int32_t more = X87Fistp(wanted) - At<int32_t>(self, field::kAsked);
    if (more <= 0)
        return;
    At<int32_t>(self, field::kAsked) += more;
    bool out = false;
    int32_t taken = 0;
    do {
        int32_t blockX = 0, blockZ = 0;
        if (!This<uint8_t>(addr::TractIterator_Next, self + field::kBlocks, &blockX, &blockZ)) {
            out = true;
        } else {
            const int32_t side = Global<int32_t>(addr::SC4ObliterateTool_BlockSize);
            int32_t rect[4] = {side * blockX, side * blockZ, side + side * blockX - 1, side + side * blockZ - 1};
            if (rect[2] > At<int32_t>(self, field::kLastX))
                rect[2] = At<int32_t>(self, field::kLastX);
            if (rect[3] > At<int32_t>(self, field::kLastZ))
                rect[3] = At<int32_t>(self, field::kLastZ);
            bool sparkle = true;
            if (At<int32_t>(self, field::kDemolished) != 0 && At<int32_t>(self, field::kEmptyRun) < 6 &&
                !BlockHasOccupants(self, nullptr, &rect[0])) {
                ++At<int32_t>(self, field::kEmptyRun);
                sparkle = false;
            }
            if (sparkle) {
                float middle[2];
                middle[0] = Middle(rect[0], rect[2]);
                middle[1] = Middle(rect[1], rect[3]);
                At<int32_t>(self, field::kEmptyRun) = 0;
                void* effect = nullptr;
                StartEffect("sparkles_on_demolish", &middle[0], 0.0f, 0, &effect);
            }
            alignas(4) char region[0x24];
            MakeRegion(region, &rect[0]);
            DemolishRegion(self, region);
            // The region's part is taken apart in line here: its blocks,
            // then their table.
            At<std::uintptr_t>(region, 0x10) = addr::SC4CellRegion_CellsVtable;
            if (At<void**>(region, 0x20)) {
                for (uint32_t row = 0; row < At<uint32_t>(region, 0x14); ++row)
                    Cdecl<void>(addr::SC4_operator_delete, At<void**>(region, 0x20)[row]);
                Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(At<void**>(region, 0x20)));
            }
            ++At<int32_t>(self, field::kDemolished);
        }
        ++taken;
    } while (!out && taken < more);
    if (out)
        StartFinale(self, nullptr);
}

// The game's 0x0049D820 (slot 3 of the message target at +0x28, ret 4).
// `this` is the target part. The frame service's message: its microseconds
// (slot 14) are added to the time since Init and the state's step is done;
// nothing in state 4. The finale effect's message: the effect is let go and
// the scene is over. True.
uint8_t __fastcall DoMessage(char* target, void*, void* message)
{
    char* const self = target - field::kTarget;
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    if (type == kMessageFrame) {
        const uint32_t elapsed = Virtual<uint32_t>(message, 0x38);
        const int32_t now = At<int32_t>(self, field::kState);
        if (now == state::kOver)
            return 1;
        At<uint32_t>(self, field::kElapsed) += elapsed;
        if (now == state::kSweep)
            SweepTick(self, nullptr);
        else if (now == state::kFinale)
            FinaleTick(self, nullptr, elapsed);
        else if (now == state::kWiped)
            AfterWipe(self, nullptr, elapsed);
        return 1;
    }
    if (type == kMessageFinaleOver && At<void*>(self, field::kFinale)) {
        Release(At<void*>(self, field::kFinale));
        At<void*>(self, field::kFinale) = nullptr;
        End(self, nullptr);
    }
    return 1;
}

// --- The object ------------------------------------------------------------------------

// The game's 0x0049CF90: the constructor; the caller allocated 0xA8 bytes.
// The iterator at +0x48 is set as its own constructor (0x0049CF50) sets it.
// +0x3C, +0x40, +0x7C to +0x8C and +0x9C to +0xA4 are left as they come.
char* __fastcall Construct(char* self, void*)
{
    This<void*>(addr::SC4ViewInputControl_Construct, self);
    At<std::uintptr_t>(self, field::kTarget) = addr::cSC4TrafficSimulator_VtableBase4;
    At<void*>(self, field::kFilter) = nullptr;
    At<void*>(self, field::kOccupants) = nullptr;
    At<void*>(self, field::kZones) = nullptr;
    At<void*>(self, field::kDemolition) = nullptr;
    At<int32_t>(self, field::kState) = 0;
    At<std::uintptr_t>(self, 0) = addr::SC4ObliterateTool_Vtable;
    At<std::uintptr_t>(self, field::kTarget) = addr::SC4ObliterateTool_TargetVtable;
    char* const blocks = self + field::kBlocks;
    At<uint8_t>(blocks, walk::kState) = 1;
    At<uint8_t>(blocks, walk::kStride) = 1;
    At<uint8_t>(blocks, walk::kChanged) = 1;
    for (std::size_t offset = walk::kRect; offset < walk::kStep; offset += 4)
        At<uint32_t>(blocks, offset) = 0;
    At<uint32_t>(blocks, walk::kStep) = 1;
    At<uint32_t>(self, field::kElapsed) = 0;
    At<int32_t>(self, field::kAsked) = 0;
    At<uint32_t>(self, field::kWipedTime) = 0;
    At<void*>(self, field::kFinale) = nullptr;
    At<uint32_t>(self, base::kId) = kToolId;
    At<uint32_t>(self, base::kCursor) = 0xF;
    return self;
}

// The game's 0x0049C8B0 (slot 4): Shutdown. The effect message and the
// frame service given up; the finale's effect stopped (slot 4 with 1) and
// let go; the three services let go; the simulator resumed (HiddenResume);
// the filter let go; the city may be saved again (slot 103) and the
// situation manager's switch is put back; the mouse let go; then the base.
uint8_t __fastcall Shutdown(char* self, void*)
{
    if (!At<uint8_t>(self, base::kInited))
        return 1;
    if (void* const server = Global<void*>(addr::SC4_MessageServer))
        Virtual<uint8_t>(server, 0x18, static_cast<void*>(self + field::kTarget), kMessageFinaleOver);
    if (void* const frames = Cdecl<void*>(addr::SC4_GetService86AD10EC))
        Virtual<uint8_t>(frames, 0x18, static_cast<void*>(self + field::kTarget), kFrameCode);
    if (void* const finale = At<void*>(self, field::kFinale)) {
        Virtual<void>(finale, 0x10, uint32_t{1});
        Release(At<void*>(self, field::kFinale));
        At<void*>(self, field::kFinale) = nullptr;
    }
    Drop(self, field::kZones);
    Drop(self, field::kOccupants);
    Drop(self, field::kDemolition);
    if (void* const simulator = Global<void*>(addr::SC4_SimulatorService))
        Virtual<void>(simulator, 0x34);
    Drop(self, field::kFilter);
    if (void* const city = Global<void*>(addr::SC4_City)) {
        Virtual<void>(city, 0x19C);
        if (void* const situations = Virtual<void*>(city, 0xC4))
            Virtual<void>(situations, 0x74, uint32_t{At<uint8_t>(self, field::kSituations)});
    }
    This<uint8_t>(addr::SC4ViewInputControl_EndCapture, self);
    This<uint8_t>(addr::SC4ViewInputControl_Shutdown, self);
    return 1;
}

// The game's 0x0049D020 (slot 26, ret 4): the deleting destructor. Shutdown
// when still inited, the iterator's list, the base.
char* __fastcall DeletingDestruct(char* self, void*, uint32_t flags)
{
    At<std::uintptr_t>(self, 0) = addr::SC4ObliterateTool_Vtable;
    At<std::uintptr_t>(self, field::kTarget) = addr::SC4ObliterateTool_TargetVtable;
    if (At<uint8_t>(self, base::kInited))
        Shutdown(self, nullptr);
    if (void* const cells = At<void*>(self, field::kBlocks + walk::kCells))
        Cdecl<void>(addr::SC4_Free, cells);
    This<void>(addr::SC4ViewInputControl_Destruct, self);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// The game's 0x0049D390 (slot 3): Init. The tuning on the first Init since
// the application last asked for it again; the base Init; the mouse
// captured; the city not to be saved (slot 104); the filter; the city's
// size; the simulator paused (HiddenPause); the three services; the frame
// service; the iterator over the blocks (the city's last cell over the
// block's side, each way) in its list mode, restarted with 1, which
// shuffles the list with rand(); the effect's message; state 1; the
// situation manager's switch kept and turned off; messages 0xAA650861 (with
// 1) and 0x2A611B51 posted. The city, the filter and the situation
// manager's answer are used as they come, as in the game.
uint8_t __fastcall Init(char* self, void*)
{
    if (At<uint8_t>(self, base::kInited))
        return 1;
    if (!Global<uint8_t>(addr::SC4ObliterateTool_TuningLoaded)) {
        Global<uint8_t>(addr::SC4ObliterateTool_TuningLoaded) = 1;
        LoadTuning();
        Virtual<void>(Global<void*>(addr::SC4_Service43C94), 0x114,
                      reinterpret_cast<void*>(addr::SC4ObliterateTool_TuningReset), uint32_t{0});
    }
    This<uint8_t>(addr::SC4ViewInputControl_Init, self);
    This<uint8_t>(addr::SC4ViewInputControl_BeginCapture, self);
    void* const city = Global<void*>(addr::SC4_City);
    Virtual<void>(city, 0x1A0);
    char* const filter = Cdecl<char*>(addr::SC4_operator_new, std::size_t{0xC});
    if (filter) {
        This<void*>(addr::SC4_OccupantFilterBase, filter);
        At<std::uintptr_t>(filter, 0x0) = addr::SC4ObliterateTool_FilterVtable;
        At<std::uintptr_t>(filter, 0x4) = addr::SC4ObliterateTool_FilterVtable4;
    }
    At<void*>(self, field::kFilter) = filter;
    AddRef(filter);
    At<int32_t>(self, field::kLastX) = Virtual<int32_t>(city, 0x174) - 1;
    const int32_t lastZ = Virtual<int32_t>(city, 0x178) - 1;
    At<int32_t>(self, field::kLastZ) = lastZ;
    int32_t* const rect = &At<int32_t>(self, field::kCity);
    rect[0] = 0;
    rect[1] = 0;
    rect[2] = At<int32_t>(self, field::kLastX);
    rect[3] = lastZ;
    if (void* const simulator = Global<void*>(addr::SC4_SimulatorService))
        Virtual<void>(simulator, 0x28);
    Hold(self, field::kDemolition, addr::SC4_Service43D00);
    Hold(self, field::kOccupants, addr::SC4_NetworkService);
    Hold(self, field::kZones, addr::SC4_ZoneManager);
    if (void* const frames = Cdecl<void*>(addr::SC4_GetService86AD10EC))
        Virtual<uint8_t>(frames, 0x14, static_cast<void*>(self + field::kTarget), kFrameCode);
    const int32_t side = Global<int32_t>(addr::SC4ObliterateTool_BlockSize);
    const int32_t blocksX = At<int32_t>(self, field::kLastX) / side;
    const int32_t blocksZ = At<int32_t>(self, field::kLastZ) / side;
    char* const blocks = self + field::kBlocks;
    if (At<uint8_t>(blocks, walk::kStride)) {
        At<uint8_t>(blocks, walk::kStride) = 0;
        At<uint8_t>(blocks, walk::kChanged) = 1;
    }
    int32_t* const bounds = &At<int32_t>(blocks, walk::kRect);
    bounds[0] = 0;
    bounds[1] = 0;
    bounds[2] = blocksX;
    bounds[3] = blocksZ;
    if (bounds[2] < bounds[0])
        bounds[2] = bounds[0];
    if (bounds[3] < bounds[1])
        bounds[3] = bounds[1];
    At<uint8_t>(blocks, walk::kChanged) = 1;
    This<void>(addr::TractIterator_Restart, blocks, uint32_t{1});
    if (void* const server = Global<void*>(addr::SC4_MessageServer))
        Virtual<uint8_t>(server, 0x14, static_cast<void*>(self + field::kTarget), kMessageFinaleOver);
    At<uint32_t>(self, field::kElapsed) = 0;
    At<int32_t>(self, field::kState) = state::kSweep;
    At<int32_t>(self, field::kEmptyRun) = 0;
    At<int32_t>(self, field::kDemolished) = 0;
    At<uint8_t>(self, field::kSituations) = 1;
    if (void* const situations = Virtual<void*>(Global<void*>(addr::SC4_City), 0xC4)) {
        At<uint8_t>(self, field::kSituations) = Virtual<uint8_t>(situations, 0x78);
        Virtual<void>(situations, 0x74, uint32_t{0});
    }
    Post(0xAA650861, 1, 0, 0);
    Post(0x2A611B51, 0, 0, 0);
    return 1;
}

// --- Registration ----------------------------------------------------------------------

bool InstallObliterateTool(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[24] = {};
    static const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::SC4ObliterateTool_OnKeyDown, reinterpret_cast<void*>(&OnKeyDown)},
        {addr::SC4ObliterateTool_TuningReset, reinterpret_cast<void*>(&TuningReset)},
        {addr::SC4ObliterateTool_FilterIncludesType, reinterpret_cast<void*>(&FilterIncludesType)},
        {addr::SC4ObliterateTool_Shutdown, reinterpret_cast<void*>(&Shutdown)},
        {addr::SC4ObliterateTool_LoadTuning, reinterpret_cast<void*>(&LoadTuning)},
        {addr::SC4ObliterateTool_Post, reinterpret_cast<void*>(&Post)},
        {addr::SC4ObliterateTool_End, reinterpret_cast<void*>(&End)},
        {addr::SC4ObliterateTool_StartEffect, reinterpret_cast<void*>(&StartEffect)},
        {addr::SC4ObliterateTool_StartFinale, reinterpret_cast<void*>(&StartFinale)},
        {addr::SC4ObliterateTool_AfterWipe, reinterpret_cast<void*>(&AfterWipe)},
        {addr::SC4ObliterateTool_DemolishRect, reinterpret_cast<void*>(&DemolishRect)},
        {addr::SC4ObliterateTool_Construct, reinterpret_cast<void*>(&Construct)},
        {addr::SC4ObliterateTool_DeletingDestructor, reinterpret_cast<void*>(&DeletingDestruct)},
        {addr::SC4ObliterateTool_FinaleTick, reinterpret_cast<void*>(&FinaleTick)},
        {addr::SC4ObliterateTool_BlockHasOccupants, reinterpret_cast<void*>(&BlockHasOccupants)},
        {addr::SC4ObliterateTool_Init, reinterpret_cast<void*>(&Init)},
        {addr::SC4ObliterateTool_SweepTick, reinterpret_cast<void*>(&SweepTick)},
        {addr::SC4ObliterateTool_DoMessage, reinterpret_cast<void*>(&DoMessage)},
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
    "ui.tools.obliterate",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4ObliterateTool_Init,
    "Our obliterate tool of the city view: the scene that wipes a city (docs/tools.md)",
    false,
    InstallObliterateTool,
});

} // namespace
} // namespace opensc4::tool_obliterate
