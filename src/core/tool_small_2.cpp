// Two small tools of the city view: the flora tool (0x004BB900 to
// 0x004BBE41) and the level terrain tool (0x004BDB00 to 0x004BDEBC).
// Replacement, off by default:
//
//   ui.tools.small2: the flora tool (vtable 0x00A903B8, 0x50 bytes, made by
//     the view's menu command 0xEA5AD08D with the flora type) and the level
//     terrain tool (vtable 0x00A905C0, 0x58 bytes, ID 0x88CEA620, made by the
//     view's terrain command 7).
//
// The flora tool plants one tree of its type where the button goes down and
// one more at every pointer move while the button is held, as long as the
// city has the money: the flora simulator's AddNewFloraOccupant (slot 17)
// at the picked point, the price (the type's 64 bits at +0x160) withdrawn
// from the budget, a sound, and the aura simulator's transient effect 1 at
// the point. The cost indicator shows the sum of the drag. With the button
// up, a move only asks the simulator whether the spot would take a tree.
//
// The level terrain tool drags a rectangle of cells over the terrain (shown
// through the terrain view's selection) and, when the button is let go, sets
// the altitude of every vertex of it to the altitude of the vertex the drag
// began at (the terrain's SetAltitude for a rectangle, slot 49).
//
// The shared slots (the deleting destructor 0x004B7ED0, the key slot
// 0x004BDBE0) are the base's (tool_base.cpp).
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::tool_small {
namespace {

// The base class's fields.
namespace base {
constexpr std::size_t kInited = 0x04; // byte
constexpr std::size_t kId = 0x0C;
constexpr std::size_t kCursor = 0x10; // cursor ID
constexpr std::size_t kView = 0x1C;   // the city view
} // namespace base

// The flora tool's fields.
namespace flora {
constexpr std::size_t kType = 0x28;      // the flora type (the constructor's argument)
constexpr std::size_t kSimulator = 0x2C; // the flora simulator (referenced)
constexpr std::size_t kSpotOk = 0x30;    // byte: the simulator would take a tree under the pointer
constexpr std::size_t kPlanting = 0x31;  // byte: the button is down
constexpr std::size_t kPrice = 0x38;     // int64: one tree
constexpr std::size_t kSpent = 0x40;     // int64: the sum of this drag
constexpr std::size_t kNoFunds = 0x48;   // byte: a move found the funds below the price
} // namespace flora

// The level terrain tool's fields.
namespace level {
constexpr std::size_t kSaved = 0x24;    // byte: the terrain view's cell cursor state before the drag
constexpr std::size_t kTerrain = 0x28;  // the terrain (referenced)
constexpr std::size_t kOverlay = 0x2C;  // the terrain view (terrain slot 66, referenced)
constexpr std::size_t kDragging = 0x30; // byte
constexpr std::size_t kStartX = 0x34;   // the cell the drag began on
constexpr std::size_t kStartZ = 0x38;
constexpr std::size_t kCurX = 0x3C;     // the cell under the pointer
constexpr std::size_t kCurZ = 0x40;
constexpr std::size_t kRect = 0x44;     // the drag's rectangle, ordered (x0, z0, x1, z1)
constexpr std::size_t kCell = 0x54;     // float, the terrain's slot 6 (16)
} // namespace level

constexpr uint32_t kSoundRefused = 0xAA62C9D5;

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

// The view's PickTerrain (slot 36) with the flag 0: the ground under the
// screen point into three floats (x, y, z).
bool PickGround(void* view, int32_t x, int32_t z, float* position)
{
    return Virtual<uint8_t>(view, 0x90, x, z, position, uint32_t{0}) != 0;
}

// --- The flora tool --------------------------------------------------------------------

// The game's 0x004BB900 (ret 4): the constructor; the caller allocated 0x50
// bytes. The ID and the cursor are Init's.
char* __fastcall FloraConstruct(char* self, void*, uint32_t type)
{
    This<void*>(addr::SC4ViewInputControl_Construct, self);
    At<uint32_t>(self, flora::kType) = type;
    At<void*>(self, flora::kSimulator) = nullptr;
    At<uint8_t>(self, flora::kSpotOk) = 0;
    At<uint8_t>(self, flora::kPlanting) = 0;
    At<int64_t>(self, flora::kPrice) = 0;
    At<int64_t>(self, flora::kSpent) = 0;
    At<uint8_t>(self, flora::kNoFunds) = 0;
    At<std::uintptr_t>(self, 0) = addr::SC4FloraTool_Vtable;
    return self;
}

// The game's 0x004BB940 (slot 4): Shutdown. Lets go of the simulator, clears
// the cost indicator (with 1), then the base.
uint8_t __fastcall FloraShutdown(char* self, void*)
{
    if (!At<uint8_t>(self, base::kInited))
        return 1;
    if (void* const simulator = At<void*>(self, flora::kSimulator)) {
        Release(simulator);
        At<void*>(self, flora::kSimulator) = nullptr;
    }
    Virtual<void>(At<void*>(self, base::kView), 0xB0, uint32_t{1}); // ClearCostIndicator
    This<uint8_t>(addr::SC4ViewInputControl_Shutdown, self);
    return 1;
}

// The game's 0x004BB980 (slot 18, OnMouseUpL, ret 0xC): the planting ends.
// After a drag that spent something the cost indicator is cleared (with 1);
// otherwise it shows the price again (both numbers). The sum and the funds
// flag start over. True.
uint8_t __fastcall FloraOnMouseUpL(char* self, void*, int32_t, int32_t, uint32_t)
{
    At<uint8_t>(self, flora::kPlanting) = 0;
    void* const view = At<void*>(self, base::kView);
    if (At<int64_t>(self, flora::kSpent) > 0) {
        Virtual<void>(view, 0xB0, uint32_t{1});
    } else {
        const uint32_t price = At<uint32_t>(self, flora::kPrice);
        Virtual<void>(view, 0xA8, price, price); // SetCostIndicator, two numbers
    }
    At<uint8_t>(self, flora::kNoFunds) = 0;
    At<int64_t>(self, flora::kSpent) = 0;
    return 1;
}

// The game's 0x004BBA20 (slot 3): Init. The ID and the cursor, the base
// Init, the flora simulator and from the type's record (slot 7) the price,
// which goes on the cost indicator (slot 43, the low dword). A type the
// simulator does not know is not tested for, as in the game.
uint8_t __fastcall FloraInit(char* self, void*)
{
    if (At<uint8_t>(self, base::kInited))
        return 1;
    At<uint32_t>(self, base::kId) = 0x26D7881C;
    At<uint32_t>(self, base::kCursor) = 0x66DB057F;
    This<uint8_t>(addr::SC4ViewInputControl_Init, self);
    void* const simulator = Global<void*>(addr::SC4_FloraSimulator43D80);
    At<void*>(self, flora::kSimulator) = simulator;
    if (simulator) {
        AddRef(simulator);
        char* const record =
            Virtual<char*>(At<void*>(self, flora::kSimulator), 0x1C, At<uint32_t>(self, flora::kType));
        At<uint32_t>(self, flora::kPrice) = At<uint32_t>(record, 0x160);
        At<uint32_t>(self, flora::kPrice + 4) = At<uint32_t>(record, 0x164);
        At<int64_t>(self, flora::kSpent) = 0;
        Virtual<void>(At<void*>(self, base::kView), 0xAC, At<uint32_t>(self, flora::kPrice));
    }
    At<uint8_t>(self, flora::kPlanting) = 0;
    return 1;
}

// The game's 0x004BBAA0 (cdecl, two floats): whether the terrain's vertex of
// the cell at (x, z) is under water (terrain slot 37 with x / 16 and z / 16
// truncated). The terrain is held for the call and not tested for null.
uint8_t __cdecl FloraIsWaterAt(float x, float z)
{
    void* const terrain = Global<void*>(addr::SC4_Terrain);
    AddRef(terrain);
    const int32_t cellZ = Ftol(static_cast<double>(z) * 0.0625); // the float at 0x00A903B0
    const int32_t cellX = Ftol(static_cast<double>(x) * 0.0625);
    const uint8_t water = Virtual<uint8_t>(terrain, 0x94, cellX, cellZ) ? 1 : 0;
    Release(terrain);
    return water;
}

// The two sounds of a planted tree: the sound service's slot 11 with the
// tree sound and a fresh instance number, then with the instance and the
// truncated position. The audio director's object is asked for and not used.
void PlantSounds(void* sound, const float* position)
{
    Cdecl<void*>(addr::SC4Sound_DirectorObject60);
    const uint32_t instance = Cdecl<uint32_t>(addr::SC4Sound_NextInstance);
    Virtual<void>(sound, 0x2C, uint32_t{0xEA451903}, uint32_t{0x49DC0C90}, instance, uint32_t{0}, uint32_t{0});
    const int32_t z = Ftol(static_cast<double>(position[2]));
    const int32_t y = Ftol(static_cast<double>(position[1]));
    const int32_t x = Ftol(static_cast<double>(position[0]));
    Virtual<void>(sound, 0x2C, uint32_t{0xCA45190F}, instance, x, y, z);
}

// Whether the budget has the price of a tree.
bool HasFunds(char* self, void* budget)
{
    return Virtual<int64_t>(budget, 0x18) >= At<int64_t>(self, flora::kPrice); // GetTotalFunds
}

// One more tree on the sum and the cost indicator: the sum and the price
// (the low dwords).
void AddToSum(char* self)
{
    At<int64_t>(self, flora::kSpent) += At<int64_t>(self, flora::kPrice);
    Virtual<void>(At<void*>(self, base::kView), 0xA8, At<uint32_t>(self, flora::kSpent),
                  At<uint32_t>(self, flora::kPrice));
}

// The aura simulator's transient effect 1 at the point (slot 6).
void AddAura(const float* position)
{
    Virtual<void>(Global<void*>(addr::SC4_Service43D60), 0x18, position[0], position[2], uint32_t{1});
}

// The end of both handlers: the refused sound when a move found no funds,
// else the tool's sound slot with 0 (which plays nothing).
void FundsSound(char* self)
{
    Virtual<void>(self, 0x70, At<uint8_t>(self, flora::kNoFunds) ? kSoundRefused : uint32_t{0});
}

// The game's 0x004BBAF0 (slot 16, OnMouseDownL, ret 0xC). When the ground is
// picked and the funds reach the price: a tree of the type at the point
// (flora simulator slot 17 with 0). When there is one: the sounds, the price
// withdrawn (the sum and the cost indicator only when the budget agrees),
// the aura effect. When there is none and the point is over water: the
// refused sound. A picked ground starts the planting whatever came of it.
// True.
uint8_t __fastcall FloraOnMouseDownL(char* self, void*, int32_t x, int32_t z, uint32_t)
{
    float position[3];
    void* const view = At<void*>(self, base::kView);
    if (view && PickGround(view, x, z, position)) {
        void* const budget = Global<void*>(addr::SC4_BudgetSimulator);
        if (HasFunds(self, budget)) {
            void* const planted = Virtual<void*>(At<void*>(self, flora::kSimulator), 0x44,
                                                 At<uint32_t>(self, flora::kType), &position[0], uint32_t{0});
            if (planted) {
                void* const sound = Global<void*>(addr::SC4_Sound43CB8);
                if (sound) {
                    AddRef(sound);
                    PlantSounds(sound, position);
                }
                if (Virtual<uint8_t>(budget, 0x20, At<int64_t>(self, flora::kPrice))) // WithdrawFunds
                    AddToSum(self);
                AddAura(position);
                if (sound)
                    Release(sound);
            } else if (FloraIsWaterAt(position[0], position[2])) {
                Virtual<void>(self, 0x70, kSoundRefused);
            }
        }
        At<uint8_t>(self, flora::kPlanting) = 1;
    }
    FundsSound(self);
    return 1;
}

// The game's 0x004BBC80 (slot 20, OnMouseMove, ret 0xC). While planting, as
// OnMouseDownL at the new point, with two differences: funds below the price
// set the flag at +0x48 (the refused sound at every move from then on), and
// the sum and the cost indicator move on whether the budget agreed to the
// withdrawal or not. Not planting, the simulator is asked whether the spot
// would take a tree of the type (slot 15) and the answer kept at +0x30.
// True.
uint8_t __fastcall FloraOnMouseMove(char* self, void*, int32_t x, int32_t z, uint32_t)
{
    float position[3];
    uint8_t spotOk = 0;
    void* const view = At<void*>(self, base::kView);
    if (view && PickGround(view, x, z, position)) {
        if (!At<uint8_t>(self, flora::kPlanting)) {
            if (Virtual<uint8_t>(At<void*>(self, flora::kSimulator), 0x3C, At<uint32_t>(self, flora::kType),
                                 position[0], position[2], uint32_t{0}))
                spotOk = 1;
        } else if (!HasFunds(self, Global<void*>(addr::SC4_BudgetSimulator))) {
            At<uint8_t>(self, flora::kNoFunds) = 1;
        } else {
            void* const planted = Virtual<void*>(At<void*>(self, flora::kSimulator), 0x44,
                                                 At<uint32_t>(self, flora::kType), &position[0], uint32_t{0});
            if (planted) {
                void* sound;
                This<void*>(addr::SC4RefHolder_SC4RefHolder, &sound, Global<void*>(addr::SC4_Sound43CB8));
                if (sound)
                    PlantSounds(sound, position);
                Virtual<uint8_t>(Global<void*>(addr::SC4_BudgetSimulator), 0x20, At<int64_t>(self, flora::kPrice));
                AddToSum(self);
                AddAura(position);
                if (sound)
                    Release(sound);
            } else if (FloraIsWaterAt(position[0], position[2])) {
                Virtual<void>(self, 0x70, kSoundRefused);
            }
        }
    }
    FundsSound(self);
    At<uint8_t>(self, flora::kSpotOk) = spotOk;
    return 1;
}

// --- The level terrain tool ------------------------------------------------------------

// The game's 0x004BDC60: the constructor; the caller allocated 0x58 bytes.
char* __fastcall LevelConstruct(char* self, void*)
{
    This<void*>(addr::SC4ViewInputControl_Construct, self);
    At<void*>(self, level::kTerrain) = nullptr;
    At<void*>(self, level::kOverlay) = nullptr;
    At<uint8_t>(self, level::kDragging) = 0;
    At<std::uintptr_t>(self, 0) = addr::SC4TerrainLevelTool_Vtable;
    At<uint32_t>(self, base::kId) = 0x88CEA620;
    At<uint32_t>(self, base::kCursor) = 0x071E5703;
    return self;
}

// The game's 0x004BDB00: orders the drag's two cells into the rectangle at
// +0x44 and has the terrain view mark it (slot 7 with colour 2, clearing the
// marks before).
void __fastcall LevelUpdateRect(char* self, void*)
{
    int32_t* const rect = &At<int32_t>(self, level::kRect);
    const int32_t startX = At<int32_t>(self, level::kStartX), curX = At<int32_t>(self, level::kCurX);
    rect[0] = startX < curX ? startX : curX;
    rect[2] = startX < curX ? curX : startX;
    const int32_t startZ = At<int32_t>(self, level::kStartZ), curZ = At<int32_t>(self, level::kCurZ);
    rect[1] = startZ < curZ ? startZ : curZ;
    rect[3] = startZ < curZ ? curZ : startZ;
    if (void* const overlay = At<void*>(self, level::kOverlay))
        Virtual<void>(overlay, 0x1C, rect, uint32_t{2}, uint32_t{1}); // MarkSelected
}

// The game's 0x004BDB50: levels the rectangle. The altitude of the vertex
// the drag began at (terrain slot 12 by index: the row is the cells along x
// plus one) goes to every vertex of the rectangle, the far side one past the
// last cell and held to the terrain's size (terrain slot 49 with 1, 1).
void __fastcall LevelApply(char* self, void*)
{
    const int32_t cellsX = Virtual<int32_t>(At<void*>(self, level::kTerrain), 0x1C);
    const int32_t cellsZ = Virtual<int32_t>(At<void*>(self, level::kTerrain), 0x20);
    const uint32_t index = static_cast<uint32_t>(cellsX + 1) * At<uint32_t>(self, level::kStartZ) +
                           At<uint32_t>(self, level::kStartX);
    const float altitude = Virtual<float>(At<void*>(self, level::kTerrain), 0x30, index);
    const int32_t* const drag = &At<int32_t>(self, level::kRect);
    int32_t rect[4] = {drag[0], drag[1], drag[2] + 1, drag[3] + 1};
    if (rect[2] > cellsX)
        rect[2] = cellsX;
    if (rect[3] > cellsZ)
        rect[3] = cellsZ;
    Virtual<void>(At<void*>(self, level::kTerrain), 0xC4, &rect[0], altitude, uint32_t{1}, uint32_t{1});
}

// The game's 0x004BDC00 (slot 4): Shutdown. Lets go of the mouse when a drag
// is on, clears the terrain view's marks and lets go of it and the terrain,
// then the base.
uint8_t __fastcall LevelShutdown(char* self, void*)
{
    if (!At<uint8_t>(self, base::kInited))
        return 1;
    if (At<uint8_t>(self, level::kDragging)) {
        At<uint8_t>(self, level::kDragging) = 0;
        This<uint8_t>(addr::SC4ViewInputControl_EndCapture, self);
    }
    if (void* const overlay = At<void*>(self, level::kOverlay)) {
        Virtual<void>(overlay, 0x24); // ClearCurrentSelections
        Release(At<void*>(self, level::kOverlay));
        At<void*>(self, level::kOverlay) = nullptr;
    }
    if (void* const terrain = At<void*>(self, level::kTerrain)) {
        Release(terrain);
        At<void*>(self, level::kTerrain) = nullptr;
    }
    This<uint8_t>(addr::SC4ViewInputControl_Shutdown, self);
    return 1;
}

// The game's 0x004BDC90 (slot 3): Init. The base Init, the terrain, its cell
// size (slot 6) and its view (slot 66).
uint8_t __fastcall LevelInit(char* self, void*)
{
    if (At<uint8_t>(self, base::kInited))
        return 1;
    This<uint8_t>(addr::SC4ViewInputControl_Init, self);
    void* const terrain = Global<void*>(addr::SC4_Terrain);
    At<void*>(self, level::kTerrain) = terrain;
    if (!terrain)
        return 1;
    AddRef(terrain);
    At<float>(self, level::kCell) = Virtual<float>(At<void*>(self, level::kTerrain), 0x18);
    void* const overlay = Virtual<void*>(At<void*>(self, level::kTerrain), 0x108);
    At<void*>(self, level::kOverlay) = overlay;
    if (overlay)
        AddRef(overlay);
    return 1;
}

// The three floats the handlers hand PickTerrain, set to the float at
// 0x00B21834 first.
void UnsetPoint(float* position)
{
    const float unset = Global<float>(addr::SC4TerrainLevelTool_UnsetPoint);
    position[0] = unset;
    position[1] = unset;
    position[2] = unset;
}

// The cell of a picked coordinate: divided by the cell size and truncated.
int32_t CellOf(char* self, float coordinate)
{
    return Ftol(static_cast<double>(coordinate) / static_cast<double>(At<float>(self, level::kCell)));
}

// The game's 0x004BDCE0 (slot 16, OnMouseDownL, ret 0xC). False without a
// view. When the ground is picked the drag begins: the mouse is captured,
// the cell is both ends of the drag, and the terrain view's cell cursor is
// turned off (slot 10 with 0; what it was is kept at +0x24). True.
uint8_t __fastcall LevelOnMouseDownL(char* self, void*, int32_t x, int32_t z, uint32_t)
{
    float position[3];
    UnsetPoint(position);
    void* const view = At<void*>(self, base::kView);
    if (!view)
        return 0;
    if (PickGround(view, x, z, position)) {
        At<uint8_t>(self, level::kDragging) = 1;
        This<uint8_t>(addr::SC4ViewInputControl_BeginCapture, self);
        At<int32_t>(self, level::kStartX) = CellOf(self, position[0]);
        At<int32_t>(self, level::kStartZ) = CellOf(self, position[2]);
        At<int32_t>(self, level::kCurZ) = At<int32_t>(self, level::kStartZ);
        At<int32_t>(self, level::kCurX) = At<int32_t>(self, level::kStartX);
        At<uint8_t>(self, level::kSaved) = Virtual<uint8_t>(At<void*>(self, level::kOverlay), 0x28, uint32_t{0});
    }
    return 1;
}

// The game's 0x004BDD80 (slot 18, OnMouseUpL, ret 0xC). False when no drag
// is on. The drag's end moves to the cell under the pointer when the ground
// is picked; then the rectangle is ordered and levelled, the marks cleared,
// the cell cursor put back as it was and the mouse let go. True.
uint8_t __fastcall LevelOnMouseUpL(char* self, void*, int32_t x, int32_t z, uint32_t)
{
    float position[3];
    UnsetPoint(position);
    if (!At<uint8_t>(self, level::kDragging))
        return 0;
    if (PickGround(At<void*>(self, base::kView), x, z, position)) {
        At<int32_t>(self, level::kCurX) = CellOf(self, position[0]);
        At<int32_t>(self, level::kCurZ) = CellOf(self, position[2]);
    }
    LevelUpdateRect(self, nullptr);
    LevelApply(self, nullptr);
    Virtual<void>(At<void*>(self, level::kOverlay), 0x24);
    Virtual<void>(At<void*>(self, level::kOverlay), 0x28, uint32_t{At<uint8_t>(self, level::kSaved)});
    This<uint8_t>(addr::SC4ViewInputControl_EndCapture, self);
    At<uint8_t>(self, level::kDragging) = 0;
    return 1;
}

// The game's 0x004BDE30 (slot 20, OnMouseMove, ret 0xC). False when no drag
// is on. When the ground is picked and its cell is not the drag's end, the
// end moves there and the rectangle is ordered and marked again. The cell is
// the coordinate times one over the cell size here (a division in the other
// two handlers). True.
uint8_t __fastcall LevelOnMouseMove(char* self, void*, int32_t x, int32_t z, uint32_t)
{
    float position[3];
    UnsetPoint(position);
    if (!At<uint8_t>(self, level::kDragging))
        return 0;
    if (PickGround(At<void*>(self, base::kView), x, z, position)) {
        const double perCell = 1.0 / static_cast<double>(At<float>(self, level::kCell));
        const int32_t cellX = Ftol(static_cast<double>(position[0]) * perCell);
        const int32_t cellZ = Ftol(perCell * static_cast<double>(position[2]));
        if (cellX != At<int32_t>(self, level::kCurX) || cellZ != At<int32_t>(self, level::kCurZ)) {
            At<int32_t>(self, level::kCurX) = cellX;
            At<int32_t>(self, level::kCurZ) = cellZ;
            LevelUpdateRect(self, nullptr);
        }
    }
    return 1;
}

// --- Registration ----------------------------------------------------------------------

bool InstallSmallTools2(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[24] = {};
    static const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::SC4FloraTool_Construct, reinterpret_cast<void*>(&FloraConstruct)},
        {addr::SC4FloraTool_Shutdown, reinterpret_cast<void*>(&FloraShutdown)},
        {addr::SC4FloraTool_OnMouseUpL, reinterpret_cast<void*>(&FloraOnMouseUpL)},
        {addr::SC4FloraTool_Init, reinterpret_cast<void*>(&FloraInit)},
        {addr::SC4FloraTool_IsWaterAt, reinterpret_cast<void*>(&FloraIsWaterAt)},
        {addr::SC4FloraTool_OnMouseDownL, reinterpret_cast<void*>(&FloraOnMouseDownL)},
        {addr::SC4FloraTool_OnMouseMove, reinterpret_cast<void*>(&FloraOnMouseMove)},
        {addr::SC4TerrainLevelTool_UpdateRect, reinterpret_cast<void*>(&LevelUpdateRect)},
        {addr::SC4TerrainLevelTool_Apply, reinterpret_cast<void*>(&LevelApply)},
        {addr::SC4TerrainLevelTool_Shutdown, reinterpret_cast<void*>(&LevelShutdown)},
        {addr::SC4TerrainLevelTool_Construct, reinterpret_cast<void*>(&LevelConstruct)},
        {addr::SC4TerrainLevelTool_Init, reinterpret_cast<void*>(&LevelInit)},
        {addr::SC4TerrainLevelTool_OnMouseDownL, reinterpret_cast<void*>(&LevelOnMouseDownL)},
        {addr::SC4TerrainLevelTool_OnMouseUpL, reinterpret_cast<void*>(&LevelOnMouseUpL)},
        {addr::SC4TerrainLevelTool_OnMouseMove, reinterpret_cast<void*>(&LevelOnMouseMove)},
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
    "ui.tools.small2",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4FloraTool_OnMouseDownL,
    "Our flora tool and level terrain tool (docs/tools.md)",
    false,
    InstallSmallTools2,
});

} // namespace
} // namespace opensc4::tool_small
