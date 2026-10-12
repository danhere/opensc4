// The plop tool (cSC4ViewInputControlPlaceLot), first part: the class's
// small methods and helpers, 0x004BDF70 to 0x004C0680.
//
// The tool is the view input control the city view makes current while the
// player holds a building or lot chosen from a menu. The object is 0x158
// bytes on the view input control base class (0x005FB320):
//
//   +0x00 main vtable 0x00A90738 (QueryInterface 0x004BDF10 shared with the
//         network lot tool, AddRef 0x004BDF00 and Release 0x004B9E20
//         shared, Init 0x004C13E0, Shutdown 0x004C0300, then base class
//         slots, OnMouseDownL 0x004C3120 and OnMouseMove 0x004C3300 in the
//         mouse part, deleting destructor 0x004C0070 at slot 26)
//   +0x28 message target, vtable 0x00A90728 (4 slots: three adjustor thunks
//         0x004BFB00, 0x004BFB10, 0x004BFB20 that subtract 0x28 and jump,
//         and DoMessage 0x004C2F20)
//
// The fields, with what the code in this part says about them (names are
// ours, a guess where the comment says so):
//
//   +0x04 byte    the base class's "inited" flag (Shutdown does nothing
//                 while it is 0)
//   +0x1C ptr     the city view (set by the base Init)
//   +0x2C uint32  constructor argument 1: 0 when the tool places a lot
//                 configuration (Init then reads +0x30), else a building
//                 (Init reads +0x34)
//   +0x30 uint32  constructor argument 2: the lot configuration ID (or a
//                 cursor ID chosen by the dialog 0x004BEEF0)
//   +0x34 uint32  constructor argument 3: the building exemplar ID
//   +0x38 int32   the rotation the player last chose; FitRotation prefers
//                 it among equal fits
//   +0x3C uint32  the rotation now, 0 to 3 (MovePreview switches on it)
//   +0x40 int32   -1 at construction
//   +0x44, +0x48  0 at construction
//   +0x4C ptr     the lot configuration manager (referenced; global
//                 0x00B43D18), given back by Shutdown
//   +0x50 byte    constructor argument 4; handed to the lot configuration
//                 filter, where a non-zero byte lets every configuration pass
//   +0x54 0x14    a hash set of uint32 (SC4Uint32HashSet): the lot
//                 configuration IDs the filter let through (count at +0x64)
//   +0x68 ptr     the service at 0x00B43D68 (referenced), given back by
//                 Shutdown
//   +0x6C 3 ptrs  vector of uint32: the building exemplar IDs of the tool's
//                 choice (the destructor frees it)
//   +0x80, +0x84  -1, -1 at construction (the cell under the cursor, a guess)
//   +0x88         0 at construction
//   +0x8C float   -FLT_MAX at construction
//   +0x90         0; +0x94, +0x95 bytes 0
//   +0x98 0x14    a game string (vtable 0x00A80810) with its characters at
//                 +0x9C (8 bytes at construction); +0xA8 0
//   +0xAC ptr     a window holder (0x005FB290 makes it, 0x005FB2A0 and
//                 0x00456A20 let it go)
//   +0xB0 0x14    a game string; +0xC4 a third game string; +0xC0, +0xD4 0
//   +0xD8, +0xDC, +0xE0  three windows (referenced); ClearPreview shuts
//                 down and releases each
//   +0xE4 ptr     the preview: an object whose slot 0xC gives its occupant
//                 and whose slots 0x24 / 0x28 read and set its rotation
//                 (referenced)
//   +0xE8 3 ptrs  vector of referenced preview objects (SC4RefVector)
//   +0xF4 3 ptrs  second such vector
//   +0x100 ptr    a texture binding (released with SC4TextureBinding::Release)
//   +0x104 uint32 0
//   +0x108 byte   a rotation offset added to +0x3C by MovePreview
//   +0x10C, +0x110, +0x114 floats  the preview's offset (a at +0x10C, c at
//                 +0x114); all three start as the game's "unset" float
//                 (0x00B2183C)
//   +0x118 byte   the cursor overlay is shown (Shutdown and ClearOverlay
//                 clear it and tell the view's overlay)
//   +0x11C, +0x120, +0x124  0
//   +0x128 -100000 and +0x12C -1; +0x138 -100000 and +0x13C -1
//   +0x140 uint32 the cursor ID now in use (0 for none); Shutdown gives it
//                 back to the cursor service (0x00B43DC4); FitRotation's
//                 demolition price depends on it being 0xF2
//   +0x144, +0x148, +0x14C  vector of 0x14-byte records (byte 0 a turn to
//                 add, +4 and +0xC the floats a and c, +0x10 a preview
//                 object); MovePreview moves each, ClearPreview empties it
//   +0x150 byte   set by Init when the lot configuration ID is the cursor
//                 0xEBF9A440
//   +0x154 uint32 1000 at construction
//
// What is in this part:
//
//   - the tool's occupant filter (vtables 0x00A906A4 / 0x00A9068C): the
//     constructor and its type test; the two lot configuration filters
//     (vtables 0x00A906C0 and 0x00A906D4) with constructors and tests;
//   - MovePreview (0x004BE180): puts the preview (and every record of the
//     vector at +0x144) at a position, in the city or out of it;
//   - the fitting code: SnapFootprint (0x004BE750), ScoreSide (0x004BE5D0),
//     PriceDemolition (0x004BEAB0), FitRotation (0x004BEB70), FloodCheck
//     (0x004BF6B0) and FitNetworkPiece (0x004BFB30);
//   - the vector of 0x14-byte records and its helpers (0x004BE8F0,
//     0x004BE950, 0x004BE9A0, 0x004BF8E0, 0x004BFED0);
//   - the constructor (0x004C0430), the destructor body (0x004BF9E0), the
//     deleting destructor (0x004C0070), Shutdown (0x004C0300) and
//     ClearPreview (0x004C0090).
//
// Left the game's: QueryInterface 0x004BDF10 (the network lot tool's vtable
// 0x00A90A88 shares it), the filter slot 0x004BDF90 (shared), RotateRectangle
// 0x004BDFD0 and SC4RefVector::FillInsert 0x004BFF30 (generic code with
// callers elsewhere), the three adjustor thunks 0x004BFB00 to 0x004BFB20
// (the network lot tool's message vtable shares them), and the two window
// builders 0x004BEEF0 and 0x004BF2F0 (never ran; modal windows made from
// code; see the report).
//
// Replacement, off by default: ui.tools.plop1.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace opensc4::ui {
namespace {

// Object fields, from the object's start (see the table above).
namespace field {
constexpr std::size_t kActive = 0x04;
constexpr std::size_t kView = 0x1C;
constexpr std::size_t kMessagePart = 0x28;
constexpr std::size_t kMode = 0x2C;
constexpr std::size_t kLotConfigId = 0x30;
constexpr std::size_t kBuildingId = 0x34;
constexpr std::size_t kPreferredTurn = 0x38;
constexpr std::size_t kTurn = 0x3C;
constexpr std::size_t kCellFlags = 0x40;
constexpr std::size_t kLotConfigs = 0x4C;
constexpr std::size_t kAcceptAll = 0x50;
constexpr std::size_t kConfigSet = 0x54;
constexpr std::size_t kZoneService = 0x68;
constexpr std::size_t kBuildingIds = 0x6C;
constexpr std::size_t kCellX = 0x80;
constexpr std::size_t kCellZ = 0x84;
constexpr std::size_t kHeight = 0x8C;
constexpr std::size_t kName = 0x98;
constexpr std::size_t kNameHolder = 0xAC;
constexpr std::size_t kLabel = 0xB0;
constexpr std::size_t kText = 0xC4;
constexpr std::size_t kWindowA = 0xD8;
constexpr std::size_t kWindowB = 0xDC;
constexpr std::size_t kWindowC = 0xE0;
constexpr std::size_t kPreview = 0xE4;
constexpr std::size_t kPreviewsA = 0xE8;
constexpr std::size_t kPreviewsB = 0xF4;
constexpr std::size_t kTexture = 0x100;
constexpr std::size_t kTurnOffset = 0x108;
constexpr std::size_t kOffsetA = 0x10C;
constexpr std::size_t kOffsetC = 0x114;
constexpr std::size_t kOverlayShown = 0x118;
constexpr std::size_t kCursor = 0x140;
constexpr std::size_t kRecords = 0x144;
constexpr std::size_t kAtCursorOnly = 0x150;
constexpr std::size_t kTuning = 0x154;
} // namespace field

// Message IDs the tool listens to (Init adds them, Shutdown takes them away).
constexpr uint32_t kMessages[] = {0x277A9364, 0x99EF1142, 0x99EF1143, 0x49D5B83F, 0xA6B79602, 0xC9F775BB};

constexpr uint32_t kCursorNone = 0xEBF9A440;
constexpr uint32_t kMessageRefresh = 0x99EF1145;

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

// _ftol2: truncation toward zero.
int32_t Ftol(double value)
{
    return static_cast<int32_t>(value);
}

// The game's signed remainder by 4 (and with 0x80000003, then the fix for
// negative numbers).
int32_t Modulo4(int32_t value)
{
    int32_t rest = value & static_cast<int32_t>(0x80000003);
    if (rest < 0)
        rest = ((rest - 1) | -4) + 1;
    return rest;
}

// Half of a size, rounded toward zero (cdq, sub, sar).
int32_t HalfOf(int32_t size)
{
    return (size - (size >> 31)) >> 1;
}

// --- The tool's occupant filter -----------------------------------------------------------

// The game's 0x004BDF70: constructor of the occupant filter (vtables
// 0x00A906A4 and 0x00A9068C) that PlaceLot makes on its stack.
char* __fastcall FilterConstruct(char* self, void*)
{
    This<void>(addr::SC4_OccupantFilterBase, self);
    At<std::uintptr_t>(self, 0) = addr::cSC4PlaceLotOccupantFilter_Vtable;
    At<std::uintptr_t>(self, 4) = addr::cSC4PlaceLotOccupantFilter_Vtable4;
    return self;
}

// The game's 0x004BDFB0: slot 4 of that filter (ret 4), the type test: every
// type but 0x2890D4DE and 0x0990B2EA.
uint8_t __fastcall FilterIsTypeIncluded(char*, void*, uint32_t type)
{
    return type != 0x2890D4DE && type != 0x0990B2EA;
}

// --- The lot configuration filters ---------------------------------------------------------

// The game's 0x004BE0C0 (ret 4): constructor of the first lot configuration
// filter (vtable 0x00A906C0, 0x1C bytes): a byte at +0xC (non-zero lets every
// configuration pass), and the services 0x00B43D18 (+0x10), 0x00B43CA8
// (+0x14, the resource manager) and 0x00B43D68 (+0x18).
char* __fastcall ConfigFilterConstruct(char* self, void*, uint32_t acceptAll)
{
    This<void>(addr::cSC4LotConfigurationFilter_Construct, self);
    At<uint8_t>(self, 0xC) = static_cast<uint8_t>(acceptAll);
    At<void*>(self, 0x10) = nullptr;
    At<void*>(self, 0x14) = nullptr;
    At<void*>(self, 0x18) = nullptr;
    At<std::uintptr_t>(self, 0) = addr::cSC4PlaceLotConfigFilter_Vtable;
    At<void*>(self, 0x10) = Global<void*>(addr::SC4_Service43D18);
    At<void*>(self, 0x14) = Global<void*>(addr::SC4_ResourceManager);
    At<void*>(self, 0x18) = Global<void*>(addr::SC4_Service43D68);
    return self;
}

// The game's 0x004BE100 (ret 4): constructor of the second filter (vtable
// 0x00A906D4): a building type at +8 and the service 0x00B43D18 at +0xC.
char* __fastcall TypeFilterConstruct(char* self, void*, uint32_t buildingType)
{
    This<void>(addr::cSC4LotConfigurationFilter_Construct, self);
    At<uint32_t>(self, 0x8) = buildingType;
    At<void*>(self, 0xC) = nullptr;
    At<std::uintptr_t>(self, 0) = addr::cSC4PlaceLotTypeFilter_Vtable;
    At<void*>(self, 0xC) = Global<void*>(addr::SC4_Service43D18);
    return self;
}

// The game's 0x004BE130: the second filter's test (slot 3, ret 4): the lot
// configuration manager's LotConfigurationHasBuildingType (slot 17: the
// configuration, the type, 0).
uint8_t __fastcall TypeFilterTest(char* self, void*, uint32_t configuration)
{
    return Virtual<uint8_t>(At<void*>(self, 0xC), 0x44, configuration, At<uint32_t>(self, 0x8), uint32_t{0});
}

// The game's 0x004BEDD0: the first filter's test (slot 3, ret 4). With the
// byte at +0xC set, or without a primary building configuration object
// (configuration slot 0x60), every configuration passes. Otherwise the
// building IDs of that object (the manager's slot 27 into a vector) are each
// looked up (service +0x18 slot 0x38 gives a resource key, the resource
// manager's slot 3 the exemplar, interface 0xA52160F5); the configuration is
// refused when one of those exemplars has the bool property 0x2781283F set.
// The byte of the 0x14-byte key structure that the property is read into is
// the key's own byte at +0x10.
uint8_t __fastcall ConfigFilterTest(char* self, void*, void* configuration)
{
    if (At<uint8_t>(self, 0xC) != 0)
        return 1;
    void* const object = Virtual<void*>(configuration, 0x60);
    if (!object)
        return 1;
    struct IdVector {
        uint32_t* begin;
        uint32_t* end;
        uint32_t* capacity;
    } ids = {nullptr, nullptr, nullptr};
    Virtual<void>(At<void*>(self, 0x10), 0x6C, object, &ids);
    const int32_t count = static_cast<int32_t>((reinterpret_cast<char*>(ids.end) - reinterpret_cast<char*>(ids.begin)) >> 2);
    for (int32_t i = 0; i < count; ++i) {
        uint32_t key[5];
        key[0] = 0;
        key[1] = 0;
        key[2] = 0;
        uint8_t* const flag = reinterpret_cast<uint8_t*>(&key[4]);
        *flag = 0;
        void* exemplar = nullptr;
        if (Virtual<uint8_t>(At<void*>(self, 0x18), 0x38, ids.begin[i], key)) {
            if (Virtual<uint8_t>(At<void*>(self, 0x14), 0xC, key, uint32_t{0xA52160F5}, &exemplar, uint32_t{0}, uint32_t{0})) {
                void* const holder = Virtual<void*>(exemplar, 0x1C);
                Cdecl<void>(addr::SC4_GetPropertyBool, holder, uint32_t{0x2781283F}, flag);
                Virtual<void>(exemplar, 0x8);
            }
        }
        if (*flag != 0) {
            if (ids.begin)
                Cdecl<void>(addr::SC4_Free, ids.begin);
            return 0;
        }
    }
    if (ids.begin)
        Cdecl<void>(addr::SC4_Free, ids.begin);
    return 1;
}

// --- Small helpers -------------------------------------------------------------------------

// The game's 0x004BE0A0: clears the overlay flag and tells the view's
// overlay object (view slot 0xC4) through 0x007D20E0. The game jumps to that
// function, so the result is whatever it returns; no caller uses it.
void __fastcall ClearOverlay(char* self, void*)
{
    At<uint8_t>(self, field::kOverlayShown) = 0;
    void* const view = At<void*>(self, field::kView);
    if (!view)
        return;
    void* const overlay = Virtual<void*>(view, 0xC4);
    This<void>(addr::SC4ViewMarkers_F7D20E0, overlay);
}

// The game's 0x004BE150 (ret 4, no this): the position of an occupant (its
// slot 0x20) is handed to the service at 0x00B43D60 (slot 6: x, z, 2).
void __stdcall GroundEffect(void* occupant)
{
    float position[3];
    Virtual<void>(occupant, 0x20, position);
    Virtual<void>(Global<void*>(addr::SC4_Service43D60), 0x18, position[0], position[2], int32_t{2});
}

// --- Moving the preview --------------------------------------------------------------------

// Moves a point by the preview's offsets a and c, as the turn says:
//   turn 0: x - a, z - c;  1: x + c, z - a;  2: x + a, z + c;  3: x - c, z + a.
// Each result is rounded to float where the game stores it.
void Shift(float* point, uint32_t turn, float a, float c)
{
    const double x = point[0];
    const double z = point[2];
    switch (turn) {
    case 0:
        point[0] = static_cast<float>(x - a);
        point[2] = static_cast<float>(z - c);
        break;
    case 1:
        point[0] = static_cast<float>(static_cast<double>(c) + x);
        point[2] = static_cast<float>(z - a);
        break;
    case 2:
        point[0] = static_cast<float>(x + a);
        point[2] = static_cast<float>(z + c);
        break;
    case 3:
        point[0] = static_cast<float>(x - c);
        point[2] = static_cast<float>(z + a);
        break;
    default:
        break;
    }
}

// The parts of a placeable object get state 5 (their slot 0x38) and the
// value (slot 0x40). The array of up to ten pointers is on the stack, a
// larger one comes from the allocator (0x0048EE50 and the inline copy of it
// in the second loop of the game's function).
void SetParts(void* occupant, uint32_t value)
{
    void* const placeable = Virtual<void*>(occupant, 0x4C);
    if (!placeable)
        return;
    Virtual<void>(placeable, 0x4);
    uint32_t count = Virtual<uint32_t>(placeable, 0xC);
    if (count) {
        void* inlineBuffer[10];
        void** buffer = inlineBuffer;
        void* heap = nullptr;
        if (count >= 10) {
            heap = Cdecl<void*>(addr::SC4_operator_new, static_cast<std::size_t>(count) * 4);
            buffer = static_cast<void**>(heap);
        }
        Virtual<void>(placeable, 0x10, buffer, &count);
        for (uint32_t i = 0; i < count; ++i) {
            void* const part = buffer[i];
            Virtual<void>(part, 0x38, uint32_t{5});
            Virtual<void>(part, 0x40, value);
        }
        if (heap)
            Cdecl<void>(addr::SC4_operator_delete, heap);
    }
    Virtual<void>(placeable, 0x8);
}

// Puts one preview object (a holder whose slot 0xC gives the occupant, slots
// 0x24 / 0x28 its rotation) at `point` with rotation `turn`, in the city
// (flag) or takes the occupant out again. The occupant is added to the prop
// manager (0x00B43D10 slot 19) when its flag 1 is clear, else moved in the
// occupant manager (slot 0x60) and told to refresh when the rotation changed.
void PlaceOne(void* holder, void* occupant, float* point, int32_t turn, uint32_t value, bool shown)
{
    void* const occupants = Global<void*>(addr::SC4_NetworkService);
    void* const props = Global<void*>(addr::SC4_Service43D10);
    if (shown) {
        Virtual<void>(occupant, 0x24, point);
        if (!Virtual<uint8_t>(occupant, 0x80, uint32_t{1})) {
            Virtual<void>(holder, 0x28, turn);
            Virtual<void>(props, 0x4C, occupant);
        } else {
            if (turn != Virtual<int32_t>(holder, 0x24)) {
                Virtual<void>(holder, 0x28, turn);
                Virtual<void>(occupant, 0x3C, kMessageRefresh, uint32_t{0});
            }
            Virtual<void>(occupants, 0x60, occupant, uint32_t{1});
        }
        SetParts(occupant, value);
    } else if (Virtual<uint8_t>(occupant, 0x80, uint32_t{1})) {
        Virtual<void>(props, 0x74, occupant);
    }
}

// The game's 0x004BE180 (ret 0xC): (position: three floats, value, flag). The
// preview at +0xE4 goes to the position moved by the offsets at +0x10C and
// +0x114 for the turn at +0x3C, with the rotation (+0x108 + +0x3C) mod 4;
// then every record of the vector at +0x144 goes there too, its own byte 0
// added to the turn and its own a and c (+4, +0xC) as offsets. With the flag
// clear the occupants are taken out of the prop manager instead.
void __fastcall MovePreview(char* self, void*, const float* position, uint32_t value, uint32_t flag)
{
    const bool shown = (flag & 0xFF) != 0;
    void* const preview = At<void*>(self, field::kPreview);
    void* const main = Virtual<void*>(preview, 0xC);
    const uint32_t turn = At<uint32_t>(self, field::kTurn);
    const int32_t rotation = Modulo4(static_cast<int32_t>(At<uint8_t>(self, field::kTurnOffset)) + static_cast<int32_t>(turn));
    float point[3];
    std::memcpy(point, position, sizeof(point));
    Shift(point, turn, At<float>(self, field::kOffsetA), At<float>(self, field::kOffsetC));
    PlaceOne(preview, main, point, rotation, value, shown);
    for (char* record = At<char*>(self, field::kRecords); record != At<char*>(self, field::kRecords + 4);) {
        void* const holder = At<void*>(record, 0x10);
        void* const occupant = Virtual<void*>(holder, 0xC);
        const int32_t recordTurn = Modulo4(static_cast<int32_t>(static_cast<uint8_t>(record[0])) + static_cast<int32_t>(turn));
        float other[3];
        std::memcpy(other, position, sizeof(other));
        Shift(other, turn, At<float>(record, 4), At<float>(record, 0xC));
        PlaceOne(holder, occupant, other, recordTurn, value, shown);
        record += 0x14;
    }
}

// --- The fitting code ----------------------------------------------------------------------

// The game's 0x004BE5D0 (ret 0x10; ECX unused): (rectangle: x1 z1 x2 z2,
// lot configuration, unused, rotation). Counts along the side of the
// rectangle that faces the rotation (north for 0, east 1, south 2, west 3,
// as many cells as the configuration is wide or deep for that rotation)
// what the city's slot 0xA0 object has in those cells (its slot 0x20 with
// the group 0x449): flags 0x40 give 4, else flag 1 gives 2, else 0x408 gives
// 1. Cells outside the terrain count nothing.
uint32_t __stdcall ScoreSide(const int32_t* rect, void* configuration, void*, int32_t rotation)
{
    void* const cityMap = Virtual<void*>(Global<void*>(addr::SC4_City), 0xA0);
    void* const terrain = Global<void*>(addr::SC4_Terrain);
    uint32_t score = 0;
    const int32_t width = Virtual<int32_t>(terrain, 0x1C);
    const int32_t depth = Virtual<int32_t>(terrain, 0x20);
    uint32_t sizeX = 0;
    uint32_t sizeZ = 0;
    Virtual<void>(configuration, 0x1C, &sizeX, &sizeZ);
    if (rotation == 1 || rotation == 3)
        std::swap(sizeX, sizeZ);
    int32_t x;
    int32_t z;
    int32_t stepX = 0;
    int32_t stepZ = 0;
    uint32_t count;
    switch (static_cast<uint32_t>(rotation)) {
    case 0:
        z = rect[1] - 1;
        x = rect[0];
        stepX = 1;
        count = sizeX;
        break;
    case 1:
        x = rect[2] + 1;
        z = rect[1];
        stepZ = 1;
        count = sizeZ;
        break;
    case 2:
        z = rect[3] + 1;
        x = rect[0];
        stepX = 1;
        count = sizeX;
        break;
    case 3:
        x = rect[0] - 1;
        z = rect[1];
        stepZ = 1;
        count = sizeZ;
        break;
    default:
        // The game uses the pointer to the rectangle for all three.
        x = static_cast<int32_t>(reinterpret_cast<std::uintptr_t>(rect));
        z = x;
        count = static_cast<uint32_t>(x);
        break;
    }
    if (count == 0)
        return score;
    do {
        if (x >= 0 && z >= 0 && x < width && z < depth) {
            void* const object = Virtual<void*>(cityMap, 0x20, x, z, uint32_t{0x449}, uint32_t{1});
            if (object) {
                const uint32_t flags = Virtual<uint32_t>(object, 0x30);
                if (flags & 0x40)
                    score += 4;
                else if (flags & 1)
                    score += 2;
                else if (flags & 0x408)
                    score += 1;
            }
        }
        x += stepX;
        z += stepZ;
    } while (--count != 0);
    return score;
}

// The game's 0x004BE750 (ret 0x18): (rotation, x, z, float* snapped x,
// float* snapped z, int32_t rect[4]). Takes the lot configuration at +0x30
// (the manager at +0x4C, slot 8), its size swapped for rotations 1 and 3,
// and works out the rectangle of cells (x1 z1 x2 z2) round the point: with
// an odd size the cell the point is in and half the size either side (the
// point snapped to the middle of the cell), with an even size the cell
// nearest the point (snapped to its corner). False when the configuration
// is unknown or the rectangle is not wholly inside the terrain. The float
// arithmetic is done in double and rounded where the game stores a float.
uint8_t __fastcall SnapFootprint(char* self, void*, int32_t rotation, float x, float z, float* snappedX, float* snappedZ,
                                 int32_t* rect)
{
    void* const terrain = Global<void*>(addr::SC4_Terrain);
    void* const configuration =
        Virtual<void*>(At<void*>(self, field::kLotConfigs), 0x20, At<uint32_t>(self, field::kLotConfigId));
    if (!configuration)
        return 0;
    uint32_t sizeX = 0;
    uint32_t sizeZ = 0;
    Virtual<void>(configuration, 0x1C, &sizeX, &sizeZ);
    if (rotation == 1 || rotation == 3)
        std::swap(sizeX, sizeZ);
    const double cell = Virtual<float>(terrain, 0x18);
    const double inverse = static_cast<double>(Global<float>(addr::SC4_FloatOneA81228)) / cell;
    const double half = Global<float>(addr::SC4_FloatHalf);
    {
        const int32_t size = static_cast<int32_t>(sizeX);
        const int32_t step = HalfOf(size);
        int32_t index;
        if (size % 2 != 0) {
            index = Ftol(static_cast<double>(x) * inverse);
            *snappedX = static_cast<float>((static_cast<double>(index) + half) * cell);
            rect[0] = index - step;
            rect[2] = step + index;
        } else {
            index = Ftol((cell * half + static_cast<double>(x)) * inverse);
            *snappedX = static_cast<float>(static_cast<double>(index) * cell);
            rect[0] = index - step;
            rect[2] = step + index - 1;
        }
    }
    {
        const int32_t size = static_cast<int32_t>(sizeZ);
        const int32_t step = HalfOf(size);
        int32_t index;
        if (size % 2 != 0) {
            index = Ftol(static_cast<double>(z) * inverse);
            *snappedZ = static_cast<float>((static_cast<double>(index) + half) * cell);
            rect[1] = index - step;
            rect[3] = step + index;
        } else {
            index = Ftol((cell * half + static_cast<double>(z)) * inverse);
            *snappedZ = static_cast<float>(static_cast<double>(index) * cell);
            rect[1] = index - step;
            rect[3] = step + index - 1;
        }
    }
    if (rect[0] < 0)
        return 0;
    if (rect[2] >= Virtual<int32_t>(terrain, 0x1C))
        return 0;
    if (rect[1] < 0)
        return 0;
    if (rect[3] >= Virtual<int32_t>(terrain, 0x20))
        return 0;
    return 1;
}

// A cell region as DemolishRegion takes it: the rectangle, then the cells
// object (5 dwords).
struct Region {
    int32_t rect[4];
    uint32_t cells[5];
};

// The game's 0x004BEAB0 (ret 0x14; three of the five arguments unused):
// (configuration, box, rotation, rectangle, int64* price). Asks the
// demolition service (0x00B43D00) for the price of clearing the rectangle:
// DemolishRegion with demolish false, privilege 1, flags 0x2800 (0x800 when
// the cursor at +0x140 is 0xF2), clearZoned 1, no filter. Returns its result.
uint8_t __fastcall PriceDemolition(char* self, void*, void*, void*, int32_t, const int32_t* rect, int64_t* price)
{
    void* const demolition = Global<void*>(addr::SC4_Service43D00);
    Region region;
    for (int i = 0; i < 4; ++i)
        region.rect[i] = rect[i];
    uint8_t fill = 1;
    This<void*>(addr::SC4CellRegion_ConstructCells, region.cells, static_cast<uint32_t>(rect[2] - rect[0] + 1),
                static_cast<uint32_t>(rect[3] - rect[1] + 1), &fill);
    const uint32_t flags = At<uint32_t>(self, field::kCursor) == 0xF2 ? 0x800 : 0x2800;
    void* const none = nullptr;
    const uint8_t result = Virtual<uint8_t>(demolition, 0x18, uint32_t{0}, &region, int32_t{1}, flags, uint32_t{1}, none, price,
                                            none, none, int32_t{0}, int32_t{0});
    region.cells[0] = static_cast<uint32_t>(addr::SC4CellRegion_CellsVtable);
    This<void>(addr::SC4CellRegion_DestroyCells, region.cells);
    return result;
}

// The game's 0x004BEB70 (ret 0x10): (configuration, box, int32* rotation,
// int32 rect[4]). The box is two floats at [0] and [2] (the point). Tries
// the four rotations: SnapFootprint, then the lot manager's slot 21 (the
// rectangle, the rotation, the configuration; 8 means it fits), then
// PriceDemolition, then ScoreSide. The best score above -1000 wins and
// the one with the rotation at +0x38 is the preferred one; the preferred
// one is taken unless its score is lower than the best by more than 3 (or
// it is 0 and the best is above 0). Writes the rotation, the rectangle and
// the snapped point back into box [0] and [2]. True when some rotation fitted.
uint8_t __fastcall FitRotation(char* self, void*, void* configuration, float* box, int32_t* outRotation, int32_t* outRect)
{
    void* const lotManager = Global<void*>(addr::SC4_LotManager);
    uint32_t unset;
    std::memcpy(&unset, &Global<float>(addr::SC4_UnsetFloat), sizeof(unset));
    uint32_t boxBits[3];
    std::memcpy(boxBits, box, sizeof(boxBits));
    uint8_t result = 0;
    int32_t bestScore = -1000;
    int32_t bestRotation = 0;
    int32_t bestRect[4] = {-1000, -1000, -1000, -1000};
    uint32_t bestX = boxBits[0];
    uint32_t bestZ = boxBits[2];
    int32_t preferredScore = 0;
    int32_t preferredRect[4] = {-1000, -1000, -1000, -1000};
    uint32_t preferredX = boxBits[0];
    uint32_t preferredZ = boxBits[2];
    for (int32_t rotation = 0; rotation <= 3; ++rotation) {
        uint32_t point[3] = {unset, unset, unset};
        int32_t rect[4] = {0, 0, 0, 0};
        int32_t score = -8;
        const uint8_t fits = SnapFootprint(self, nullptr, rotation, box[0], box[2], reinterpret_cast<float*>(&point[0]),
                                           reinterpret_cast<float*>(&point[2]), rect);
        const int32_t lastZ = rect[3];
        if (fits) {
            const uint32_t state = Virtual<uint32_t>(lotManager, 0x54, rect[0], rect[1], rect[2], lastZ, rotation, configuration);
            if (state == 8) {
                if (PriceDemolition(self, nullptr, configuration, box, rotation, rect, nullptr)) {
                    score = static_cast<int32_t>(ScoreSide(rect, configuration, &point[0], rotation));
                    result = 1;
                }
            }
        }
        if (score > bestScore) {
            bestRect[0] = rect[0];
            bestRect[1] = rect[1];
            bestRect[2] = rect[2];
            bestScore = score;
            bestRotation = rotation;
            bestRect[3] = lastZ;
            bestX = point[0];
            bestZ = point[2];
        }
        if (rotation == At<int32_t>(self, field::kPreferredTurn)) {
            preferredRect[0] = rect[0];
            preferredRect[1] = rect[1];
            preferredRect[2] = rect[2];
            preferredScore = score;
            preferredRect[3] = lastZ;
            preferredX = point[0];
            preferredZ = point[2];
        }
    }
    bool takeBest = false;
    if (preferredScore == 0 && bestScore > 0)
        takeBest = true;
    else
        takeBest = !(preferredScore >= bestScore + -4);
    if (takeBest) {
        *outRotation = bestRotation;
        for (int i = 0; i < 4; ++i)
            outRect[i] = bestRect[i];
        std::memcpy(&box[0], &bestX, sizeof(uint32_t));
        std::memcpy(&box[2], &bestZ, sizeof(uint32_t));
        return result;
    }
    *outRotation = At<int32_t>(self, field::kPreferredTurn);
    for (int i = 0; i < 4; ++i)
        outRect[i] = preferredRect[i];
    std::memcpy(&box[0], &preferredX, sizeof(uint32_t));
    std::memcpy(&box[2], &preferredZ, sizeof(uint32_t));
    return result;
}

// A node of the list FloodCheck walks: next, previous, then x, z, depth.
struct FloodNode {
    FloodNode* next;
    FloodNode* previous;
    int32_t x;
    int32_t z;
    int32_t depth;
};

FloodNode* NewFloodNode(int32_t x, int32_t z, int32_t depth)
{
    FloodNode* const node = static_cast<FloodNode*>(Cdecl<void*>(addr::SC4_stl_allocate, std::size_t{0x14}));
    node->x = x;
    node->z = z;
    node->depth = depth;
    return node;
}

void LinkLast(FloodNode* sentinel, FloodNode* node)
{
    FloodNode* const last = sentinel->previous;
    node->next = sentinel;
    node->previous = last;
    last->next = node;
    sentinel->previous = node;
}

void FreeFloodList(FloodNode* sentinel)
{
    FloodNode* node = sentinel->next;
    while (node != sentinel) {
        FloodNode* const current = node;
        node = node->next;
        Cdecl<void>(addr::SC4_Free, current);
    }
    sentinel->next = sentinel;
    sentinel->previous = sentinel;
    Cdecl<void>(addr::SC4_Free, sentinel);
}

// The game's 0x004BF6B0 (ret 0xC, ECX unused): (rectangle x1 z1 x2 z2, depth,
// side). For every cell of the rectangle that holds a network object
// (0x00B43D54 slot 8: the cell, group 0x1045) a walk follows the connections
// (the object's slot 0xA0 gives four bits: west, north, east, south) up to
// `depth` steps; false as soon as a cell reached has the object's slot 9
// answering true for the mask (every bit of 0x1FFF except the one of `side`,
// plus 0x400000), true when none does.
uint8_t __stdcall FloodCheck(const int32_t* rect, int32_t depthLimit, uint32_t side)
{
    const uint32_t mask = (~(uint32_t{1} << (side & 31)) & 0x1FFF) | 0x400000;
    for (int32_t x = rect[0]; x <= rect[2]; ++x) {
        for (int32_t z = rect[1]; z <= rect[3]; ++z) {
            void* const first = Virtual<void*>(Global<void*>(addr::SC4_Service43D54), 0x20, x, z, uint32_t{0x1045}, uint32_t{1});
            if (!first)
                continue;
            FloodNode* const sentinel = static_cast<FloodNode*>(Cdecl<void*>(addr::SC4_stl_allocate, std::size_t{0x14}));
            sentinel->next = sentinel;
            sentinel->previous = sentinel;
            LinkLast(sentinel, NewFloodNode(x, z, 0));
            while (sentinel->next != sentinel) {
                FloodNode* const node = sentinel->next;
                const int32_t cellX = node->x;
                const int32_t cellZ = node->z;
                const int32_t depth = node->depth;
                node->previous->next = node->next;
                node->next->previous = node->previous;
                Cdecl<void>(addr::SC4_Free, node);
                void* const object = Virtual<void*>(Global<void*>(addr::SC4_Service43D54), 0x20, cellX, cellZ, uint32_t{0x1045},
                                                     uint32_t{1});
                if (!object)
                    continue;
                if (Virtual<uint8_t>(object, 0x24, mask)) {
                    FreeFloodList(sentinel);
                    return 0;
                }
                if (depth >= depthLimit)
                    continue;
                uint32_t links = Virtual<uint8_t>(object, 0xA0);
                for (int k = 0; k < 4; ++k) {
                    if (links & 1) {
                        const int32_t stepX = Global<int32_t>(addr::cSC4ViewInputControlPlaceLot_StepX + 4 * k);
                        const int32_t stepZ = Global<int32_t>(addr::cSC4ViewInputControlPlaceLot_StepZ + 4 * k);
                        LinkLast(sentinel, NewFloodNode(cellX + stepX, cellZ + stepZ, depth + 1));
                    }
                    links = static_cast<uint32_t>(static_cast<int32_t>(links) >> 1);
                }
            }
            FreeFloodList(sentinel);
        }
    }
    return 1;
}

// The game's 0x004BFB30 (ret 0x10): (cursor out, width out, point[3] in and
// out, rectangle out). The point is a position: the city's cell of it
// (slot 0x17C) and the cell's position back (slot 0x184) make it the cell's
// corner. The network object in that cell (0x00B43D54 slot 8, group 0x1045)
// must be of one kind (slot 0x58 true for 0, 2, 6 or 0xC) and its flags (slot
// 0x30) within the bits 0x1F4F that kind allows; its connections (slot 0x98,
// a byte per direction: bit 0 refuses, value 2 means connected, bit 2 a
// side) must connect it in exactly two directions. A straight piece
// (directions 0 and 2 connected) gives a 2 by 1 rectangle along x, a
// piece connected in directions 1 and 3 a 1 by 2 rectangle along z; for the
// kinds 2, 6 and 0xC the side bits move the rectangle by a cell and the
// point by 8 or 16 metres. The cursor out is the one for the kind (table at
// 0x00A90650), the width 1 or 2. The result is FloodCheck of the rectangle
// with depth 2.
uint8_t __fastcall FitNetworkPiece(char* self, void*, uint32_t* cursor, uint32_t* width, float* point, int32_t* rect)
{
    (void)self;
    *cursor = kCursorNone;
    *width = 2;
    void* const city = Global<void*>(addr::SC4_City);
    int32_t cellX = 0;
    int32_t cellZ = 0;
    Virtual<void>(city, 0x17C, point[0], point[2], &cellX, &cellZ);
    Virtual<void>(city, 0x184, cellX, cellZ, &point[0], &point[2]);
    rect[0] = cellX;
    rect[1] = cellZ;
    rect[2] = cellX;
    rect[3] = cellZ + 1;
    const float eight = Global<float>(addr::SC4_FloatEight);
    const float sixteen = Global<float>(addr::SC4_FloatSixteen);
    point[2] = static_cast<float>(static_cast<double>(point[2]) + eight);
    void* const object = Virtual<void*>(Global<void*>(addr::SC4_Service43D54), 0x20, cellX, cellZ, uint32_t{0x1045}, uint32_t{1});
    if (!object)
        return 0;
    uint32_t kind;
    if (Virtual<uint8_t>(object, 0x58, uint32_t{0}))
        kind = 0;
    else if (Virtual<uint8_t>(object, 0x58, uint32_t{2}))
        kind = 2;
    else if (Virtual<uint8_t>(object, 0x58, uint32_t{6}))
        kind = 6;
    else if (Virtual<uint8_t>(object, 0x58, uint32_t{0xC}))
        kind = 0xC;
    else
        return 0;
    const uint32_t flags = Virtual<uint32_t>(object, 0x30) & 0x1F4F;
    if (flags != (flags & (uint32_t{1} << (kind & 31))))
        return 0;
    uint32_t bits = Virtual<uint32_t>(object, 0x98, kind);
    bool connected[4];
    bool side[4];
    int linked = 0;
    for (int d = 0; d < 4; ++d) {
        const uint32_t pair = bits & 3;
        if (pair == 1 || pair == 3)
            return 0;
        connected[d] = pair == 2;
        if (connected[d])
            ++linked;
        side[d] = ((bits >> 2) & 1) != 0;
        bits >>= 8;
    }
    if (linked != 2)
        return 0;
    if (kind > 0xC)
        return 0;
    const uint32_t cursorOfKind = Global<uint32_t>(addr::cSC4ViewInputControlPlaceLot_CursorByKind + 4 * kind);
    if (kind == 0) {
        if (connected[0] && connected[2]) {
            *width = 1;
            *cursor = cursorOfKind;
            rect[0] = cellX;
            rect[1] = cellZ;
            rect[2] = cellX + 1;
            rect[3] = cellZ;
            point[0] = static_cast<float>(static_cast<double>(point[0]) + eight);
            point[2] = static_cast<float>(static_cast<double>(point[2]) - eight);
            return FloodCheck(rect, 2, kind);
        }
        if (connected[1] && connected[3]) {
            *width = 2;
            *cursor = cursorOfKind;
            rect[0] = cellX;
            rect[1] = cellZ;
            rect[2] = cellX;
            rect[3] = cellZ + 1;
            return FloodCheck(rect, 2, kind);
        }
        return 0;
    }
    if (connected[0] && connected[2]) {
        *width = 1;
        rect[0] = cellX;
        if (side[1]) {
            rect[3] = cellZ;
            rect[1] = cellZ - 1;
            rect[2] = cellX + 1;
            point[0] = static_cast<float>(static_cast<double>(point[0]) + eight);
            point[2] = static_cast<float>(static_cast<double>(point[2]) - sixteen);
        } else {
            rect[1] = cellZ;
            rect[2] = cellX + 1;
            rect[3] = cellZ + 1;
            point[0] = static_cast<float>(static_cast<double>(point[0]) + eight);
        }
        *cursor = cursorOfKind;
        return FloodCheck(rect, 2, kind);
    }
    if (connected[1] && connected[3]) {
        *width = 2;
        if (side[0]) {
            rect[0] = cellX - 1;
            rect[1] = cellZ;
            rect[2] = cellX;
            rect[3] = cellZ + 1;
            point[0] = static_cast<float>(static_cast<double>(point[0]) - eight);
        } else {
            rect[0] = cellX;
            rect[1] = cellZ;
            rect[2] = cellX + 1;
            rect[3] = cellZ + 1;
            point[0] = static_cast<float>(static_cast<double>(point[0]) + eight);
            point[2] = static_cast<float>(static_cast<double>(point[2]) - sixteen);
        }
        *cursor = cursorOfKind;
        return FloodCheck(rect, 2, kind);
    }
    return 0;
}

// --- The vector of 0x14-byte records ---------------------------------------------------------

// A record is a byte at 0, then dwords at 4, 8, 0xC and 0x10; a copy leaves
// bytes 1 to 3 alone.
void CopyRecord(char* to, const char* from)
{
    to[0] = from[0];
    At<uint32_t>(to, 4) = At<uint32_t>(const_cast<char*>(from), 4);
    At<uint32_t>(to, 8) = At<uint32_t>(const_cast<char*>(from), 8);
    At<uint32_t>(to, 0xC) = At<uint32_t>(const_cast<char*>(from), 0xC);
    At<uint32_t>(to, 0x10) = At<uint32_t>(const_cast<char*>(from), 0x10);
}

// The game's 0x004BE8F0 (cdecl; the fourth argument is a tag nothing reads):
// copies [first, last) to `dest`, skipping a null destination, and returns
// the destination's end.
char* __cdecl CopyRange(const char* first, const char* last, char* dest, void*)
{
    while (first != last) {
        if (dest)
            CopyRecord(dest, first);
        first += 0x14;
        dest += 0x14;
    }
    return dest;
}

// The game's 0x004BE950 (cdecl): `count` copies of the record `source` at
// `dest`; returns the destination's end.
char* __cdecl FillRecords(char* dest, uint32_t count, const char* source, void*)
{
    if (count == 0)
        return dest;
    for (uint32_t i = 0; i < count; ++i) {
        if (dest)
            CopyRecord(dest, source);
        dest += 0x14;
    }
    return dest;
}

// The game's 0x004BE9A0 (cdecl, five arguments, the last two unused):
// copies [first, last) forward to `dest`; returns the destination's end.
char* __cdecl CopyForward(const char* first, const char* last, char* dest, void*, int32_t)
{
    const int32_t count = static_cast<int32_t>((last - first) / 0x14);
    for (int32_t i = 0; i < count; ++i) {
        CopyRecord(dest, first);
        first += 0x14;
        dest += 0x14;
    }
    return dest;
}

// The game's 0x004BF8E0 (ret 0x14), the vector's reallocating insert (this =
// {start, finish, end of storage}): (position, record, tag, fill count, at
// end). The new store holds the old size plus the larger of the old size and
// the fill count; the part before the position, the fill, and unless it is
// at the end the part after the position are copied in; the old store is
// freed.
void __fastcall InsertOverflow(char* self, void*, char* position, const char* record, void* tag, uint32_t fillCount, uint32_t atEnd)
{
    char* const start = At<char*>(self, 0);
    const uint32_t size = static_cast<uint32_t>((At<char*>(self, 4) - start) / 0x14);
    const uint32_t larger = size < fillCount ? fillCount : size;
    const uint32_t length = larger + size;
    char* fresh = nullptr;
    if (length != 0)
        fresh = static_cast<char*>(Cdecl<void*>(addr::SC4_stl_allocate, static_cast<std::size_t>(length) * 0x14));
    char* end = CopyRange(start, position, fresh, tag);
    if (fillCount == 1) {
        if (end)
            CopyRecord(end, record);
        end += 0x14;
    } else {
        end = FillRecords(end, fillCount, record, tag);
    }
    if (!(atEnd & 0xFF))
        end = CopyRange(position, At<char*>(self, 4), end, tag);
    if (At<char*>(self, 0))
        Cdecl<void>(addr::SC4_Free, At<char*>(self, 0));
    At<char*>(self, 0) = fresh;
    At<char*>(self, 4) = end;
    At<char*>(self, 8) = fresh + static_cast<std::size_t>(length) * 0x14;
}

// The game's 0x004BFED0 (ret 4): push_back of a record. (The game's result
// register is whatever the reallocation leaves; no caller reads it.)
void __fastcall PushBack(char* self, void*, const char* record)
{
    char* const finish = At<char*>(self, 4);
    if (finish != At<char*>(self, 8)) {
        if (finish)
            CopyRecord(finish, record);
        At<char*>(self, 4) += 0x14;
        return;
    }
    InsertOverflow(self, nullptr, finish, record, nullptr, 1, 1);
}

// --- The two choosing windows ----------------------------------------------------------------

// A game string as the game keeps it on the stack: the vtable, the text
// pointers (begin, end, capacity) and a word.
struct Text {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t extra;
};

// The text's buffer let go, the vtable put back (the game's inline code).
void DropText(Text& text)
{
    char* const buffer = text.begin;
    text.vtable = addr::SC4String_Vtable;
    if (buffer)
        Cdecl<void>(addr::SC4_Free, buffer);
}

// What differs between the two windows: the window's ID, the caption (a
// range of characters), the window's width, the list's width and its
// column's width.
struct Shape {
    uint32_t windowId;
    std::uintptr_t captionFirst;
    std::uintptr_t captionLast;
    int32_t width;
    int32_t listWidth;
    uint32_t columnWidth;
};

// Makes a window of 600 by 300 (or 350 by 300) with a caption, one list
// control inside it, lets `fill` add the rows, shows it modally and answers
// the ID of the row picked: the number in the row's text, or `defaultId`
// when none was picked. Everything is the game's two functions' common code;
// `fill(list, text, index, defaultId)` is the one part that differs. The
// window manager is the framework's (GetWinManager), the modal run goes
// through the service at 0x00B43CE0 (slot 10 before, slot 6 after).
template <typename Fill>
uint32_t RunChooser(const Shape& shape, uint32_t defaultId, Fill fill)
{
    void* const manager = Cdecl<void*>(addr::SC4_GetWinManager);
    void* const memory = Cdecl<void*>(addr::SC4_operator_new, std::size_t{0x128});
    void* const window = memory ? This<void*>(addr::cGZWin_cGZWin, memory) : nullptr;
    Virtual<void>(window, 0x4);
    Virtual<void>(window, 0x100, shape.windowId);
    Text caption = {0, nullptr, nullptr, nullptr, 0};
    uint8_t tag = 0;
    This<void>(addr::SC4String_FromRange, &caption.begin, reinterpret_cast<const char*>(shape.captionFirst),
               reinterpret_cast<const char*>(shape.captionLast), &tag);
    caption.vtable = addr::SC4String_Vtable;
    caption.extra = 0;
    Virtual<void>(window, 0x128, &caption);
    DropText(caption);
    Virtual<void>(window, 0xE0, int32_t{0x64}, int32_t{0x64});
    Virtual<void>(window, 0xD4, shape.width, int32_t{0x12C});
    Virtual<void>(window, 0x1AC, uint32_t{0x80}, uint32_t{0x80}, uint32_t{0xFF});
    char* const part = static_cast<char*>(window) + 0xD8;
    Virtual<void>(part, 0x18, uint32_t{1}, uint32_t{1});
    Virtual<void>(part, 0x18, uint32_t{2}, uint32_t{0});
    Virtual<void>(part, 0x18, uint32_t{0x10}, uint32_t{1});
    Virtual<void>(window, 0x190, uint32_t{1});
    Text name = {0, nullptr, nullptr, nullptr, 0};
    char* const block = static_cast<char*>(Cdecl<void*>(addr::SC4_stl_allocate, std::size_t{8}));
    name.begin = block;
    name.end = block;
    name.capacity = block + 8;
    block[0] = 0;
    void* const listMemory = Cdecl<void*>(addr::SC4_operator_new, std::size_t{0x2E8});
    void* const list = listMemory ? This<void*>(addr::SC4UI_ListControlConstruct, listMemory, uint32_t{0x19BFF}) : nullptr;
    name.vtable = addr::SC4String_Vtable;
    name.extra = 0;
    Virtual<void>(list, 0x4);
    char* const inner = static_cast<char*>(list) + 4;
    Virtual<void>(inner, 0xE0, int32_t{4}, int32_t{0x26});
    Virtual<void>(inner, 0xD4, shape.listWidth, int32_t{0x102});
    Virtual<void>(Virtual<void*>(list, 0x14), 0x1AC, uint32_t{0xFF}, uint32_t{0xFF}, uint32_t{0xFF});
    Virtual<void>(list, 0x78, uint32_t{2}, uint32_t{1}, uint32_t{0});
    Virtual<void>(list, 0x104, uint32_t{0}, uint32_t{1}, uint32_t{0x64});
    Virtual<void>(list, 0x104, uint32_t{1}, uint32_t{1}, shape.columnWidth);
    uint32_t index = 0;
    fill(list, name, index, defaultId);
    Virtual<void>(list, 0x20C, uint32_t{1}, uint32_t{0}, uint32_t{1}, uint32_t{0});
    uint32_t selected = 0;
    Virtual<void>(list, 0xA8, &selected, &index);
    Virtual<void>(list, 0x90, index);
    Virtual<void>(window, 0x38, inner);
    {
        void* const service = Global<void*>(addr::SC4_Service43CE0);
        Virtual<void>(service, 0x28, Virtual<void*>(part, 0xC), uint32_t{1});
    }
    Virtual<int32_t>(manager, 0xA4, window);
    {
        void* const service = Global<void*>(addr::SC4_Service43CE0);
        Virtual<void>(service, 0x18, Virtual<void*>(part, 0xC));
    }
    Virtual<void>(list, 0xA8, &selected, &index);
    uint32_t result;
    if (Virtual<uint8_t>(list, 0x1BC, uint32_t{0}, index, &name))
        result = static_cast<uint32_t>(Cdecl<int32_t>(addr::SC4_TextToInt, name.begin));
    else
        result = defaultId;
    Virtual<void>(window, 0x8);
    Virtual<void>(list, 0x8);
    Virtual<void>(manager, 0x5C, window);
    DropText(name);
    return result;
}

// Adds a row to the list control: the text, as column 0 or 1.
void AddRow(void* list, uint32_t column, const char* text)
{
    Virtual<void>(list, 0x1C0, column, int32_t{0x7FFFFFFF}, text, int32_t{-1}, uint32_t{1}, uint32_t{0});
}

// The game's 0x004BEEF0 (ret 4): the window that lists the lot
// configurations the tool may place (the hash set at +0x54), each row the
// configuration's ID as "0x%08x" and its name; the row of `defaultId` is
// selected. Answers the ID picked.
uint32_t __fastcall ChooseLotConfiguration(char* self, void*, uint32_t defaultId)
{
    static const Shape shape = {0x4A974BF3, addr::cSC4ViewInputControlPlaceLot_LotCaption, addr::cSC4ViewInputControlPlaceLot_LotCaption + 8, 0x258, 0x250,
                                0x1E0};
    return RunChooser(shape, defaultId, [&](void* list, Text& name, uint32_t& index, uint32_t chosen) {
        struct Iterator {
            char* node;
            char* table;
        } iterator;
        This<void>(addr::SC4HashTable_Begin, self + field::kConfigSet, &iterator);
        index = 0;
        while (iterator.node) {
            const uint32_t id = At<uint32_t>(iterator.node, 4);
            void* const configuration = Virtual<void*>(At<void*>(self, field::kLotConfigs), 0x20, id);
            Cdecl<void>(addr::cRZBaseString_Sprintf, &name, reinterpret_cast<const char*>(addr::Ordinance_IdFormat), id);
            AddRow(list, 0, name.begin);
            if (configuration)
                Virtual<void>(configuration, 0x10, &name);
            AddRow(list, 1, name.begin);
            if (id == chosen)
                Virtual<void>(list, 0xAC, uint32_t{0}, index);
            char* next = At<char*>(iterator.node, 0);
            if (!next) {
                char* const table = iterator.table;
                const uint32_t buckets = static_cast<uint32_t>((At<char*>(table, 8) - At<char*>(table, 4)) >> 2);
                uint32_t position = id % buckets;
                const uint32_t total = static_cast<uint32_t>((At<char*>(table, 8) - At<char*>(table, 4)) >> 2);
                next = nullptr;
                for (;;) {
                    ++position;
                    if (position >= total)
                        break;
                    next = At<char**>(table, 4)[position];
                    if (next)
                        break;
                }
            }
            iterator.node = next;
            ++index;
        }
    });
}

// The game's 0x004BF2F0 (ret 4): the same for the building IDs of the
// vector at +0x6C; each row is the ID as "0x%08x" and the name the service
// at +0x68 (slot 0x94) gives for it, set on the string through its slot 13.
uint32_t __fastcall ChooseBuilding(char* self, void*, uint32_t defaultId)
{
    static const Shape shape = {0x4A974B9B, addr::cSC4ViewInputControlPlaceLot_BuildingCaption,
                                addr::cSC4ViewInputControlPlaceLot_BuildingCaption + 0x12, 0x15E, 0x156, 0xE6};
    return RunChooser(shape, defaultId, [&](void* list, Text& name, uint32_t& index, uint32_t chosen) {
        uint32_t* const first = At<uint32_t*>(self, field::kBuildingIds);
        uint32_t* const last = At<uint32_t*>(self, field::kBuildingIds + 4);
        index = 0;
        for (uint32_t* id = first; id != last; ++id) {
            const uint32_t building = *id;
            Cdecl<void>(addr::cRZBaseString_Sprintf, &name, reinterpret_cast<const char*>(addr::Ordinance_IdFormat), building);
            AddRow(list, 0, name.begin);
            const char* const text = Virtual<const char*>(At<void*>(self, field::kZoneService), 0x94, building);
            Virtual<void>(&name, 0x34, text);
            AddRow(list, 1, name.begin);
            if (building == chosen)
                Virtual<void>(list, 0xAC, uint32_t{0}, index);
            ++index;
        }
    });
}

// --- Construction, destruction ------------------------------------------------------------

// The game's 0x004C0430 (ret 0x10): the tool's constructor. (mode, lot
// configuration ID, building ID, accept-all byte.) Returns this.
char* __fastcall Construct(char* self, void*, uint32_t mode, uint32_t configId, uint32_t buildingId, uint32_t acceptAll)
{
    This<void>(addr::SC4ViewInputControl_Construct, self);
    At<std::uintptr_t>(self, field::kMessagePart) = 0x00A81174;
    At<uint32_t>(self, field::kMode) = mode;
    At<uint32_t>(self, field::kLotConfigId) = configId;
    At<std::uintptr_t>(self, 0) = addr::cSC4ViewInputControlPlaceLot_Vtable;
    At<std::uintptr_t>(self, field::kMessagePart) = addr::cSC4ViewInputControlPlaceLot_MessageVtable;
    At<uint32_t>(self, field::kBuildingId) = buildingId;
    At<uint32_t>(self, 0x38) = 0;
    At<uint32_t>(self, field::kTurn) = 0;
    At<uint32_t>(self, 0x44) = 0;
    At<uint32_t>(self, 0x48) = 0;
    At<void*>(self, field::kLotConfigs) = nullptr;
    At<uint8_t>(self, field::kAcceptAll) = static_cast<uint8_t>(acceptAll);
    At<int32_t>(self, field::kCellFlags) = -1;
    char* const set = self + field::kConfigSet;
    At<uint32_t>(set, 4) = 0;
    At<uint32_t>(set, 8) = 0;
    At<uint32_t>(set, 0xC) = 0;
    At<uint32_t>(set, 0x10) = 0;
    This<void>(addr::SC4Uint32HashSet_Construct, set, uint32_t{0x64});
    At<uint32_t>(self, 0x68) = 0;
    At<uint32_t>(self, 0x6C) = 0;
    At<uint32_t>(self, 0x70) = 0;
    At<uint32_t>(self, 0x74) = 0;
    At<uint32_t>(self, 0x78) = 0;
    At<uint32_t>(self, 0x7C) = 0;
    At<int32_t>(self, field::kCellX) = -1;
    At<int32_t>(self, field::kCellZ) = -1;
    At<uint32_t>(self, 0x88) = 0;
    At<uint32_t>(self, field::kHeight) = 0xFF7FFFFF;
    At<uint32_t>(self, 0x90) = 0;
    At<uint8_t>(self, 0x94) = 0;
    At<uint8_t>(self, 0x95) = 0;
    At<uint32_t>(self, 0x9C) = 0;
    At<uint32_t>(self, 0xA0) = 0;
    At<uint32_t>(self, 0xA4) = 0;
    // Three game strings, each an 8-byte block holding a terminator.
    char* block = static_cast<char*>(Cdecl<void*>(addr::SC4_stl_allocate, std::size_t{8}));
    At<char*>(self, 0xA4) = block + 8;
    At<char*>(self, 0x9C) = block;
    At<char*>(self, 0xA0) = block;
    block[0] = 0;
    This<void>(addr::SC4ViewModel_Construct, self + field::kNameHolder);
    At<std::uintptr_t>(self, field::kName) = addr::SC4String_Vtable;
    At<uint32_t>(self, 0xA8) = 0;
    At<uint32_t>(self, 0xB4) = 0;
    At<uint32_t>(self, 0xB8) = 0;
    At<uint32_t>(self, 0xBC) = 0;
    block = static_cast<char*>(Cdecl<void*>(addr::SC4_stl_allocate, std::size_t{8}));
    At<char*>(self, 0xB4) = block;
    At<char*>(self, 0xB8) = block;
    At<char*>(self, 0xBC) = block + 8;
    block[0] = 0;
    At<std::uintptr_t>(self, field::kLabel) = addr::SC4String_Vtable;
    At<uint32_t>(self, 0xC0) = 0;
    At<uint32_t>(self, 0xC8) = 0;
    At<uint32_t>(self, 0xCC) = 0;
    At<uint32_t>(self, 0xD0) = 0;
    block = static_cast<char*>(Cdecl<void*>(addr::SC4_stl_allocate, std::size_t{8}));
    At<char*>(self, 0xC8) = block;
    At<char*>(self, 0xCC) = block;
    At<char*>(self, 0xD0) = block + 8;
    block[0] = 0;
    At<uint32_t>(self, 0xD4) = 0;
    At<std::uintptr_t>(self, field::kText) = addr::SC4String_Vtable;
    At<uint32_t>(self, field::kWindowA) = 0;
    At<uint32_t>(self, field::kWindowB) = 0;
    At<uint32_t>(self, field::kWindowC) = 0;
    At<uint32_t>(self, field::kPreview) = 0;
    At<uint32_t>(self, field::kPreviewsA) = 0;
    At<uint32_t>(self, field::kPreviewsA + 4) = 0;
    At<uint32_t>(self, 0xF0) = 0;
    At<uint32_t>(self, field::kPreviewsB) = 0;
    At<uint32_t>(self, field::kPreviewsB + 4) = 0;
    At<uint32_t>(self, 0xFC) = 0;
    At<uint32_t>(self, field::kTexture) = 0;
    At<uint32_t>(self, 0x104) = 0;
    At<uint8_t>(self, field::kTurnOffset) = 0;
    At<uint32_t>(self, 0x10C) = Global<uint32_t>(addr::SC4_UnsetFloat);
    At<uint32_t>(self, 0x110) = Global<uint32_t>(addr::SC4_UnsetFloat);
    At<uint32_t>(self, 0x114) = Global<uint32_t>(addr::SC4_UnsetFloat);
    At<uint8_t>(self, field::kOverlayShown) = 0;
    At<uint32_t>(self, 0x11C) = 0;
    At<uint32_t>(self, 0x120) = 0;
    At<uint32_t>(self, 0x124) = 0;
    At<int32_t>(self, 0x128) = -100000;
    At<int32_t>(self, 0x12C) = -1;
    At<int32_t>(self, 0x138) = -100000;
    At<int32_t>(self, 0x13C) = -1;
    At<uint32_t>(self, field::kCursor) = 0;
    At<uint32_t>(self, field::kRecords) = 0;
    At<uint32_t>(self, field::kRecords + 4) = 0;
    At<uint32_t>(self, field::kRecords + 8) = 0;
    At<uint8_t>(self, field::kAtCursorOnly) = 0;
    At<uint32_t>(self, field::kTuning) = 0x3E8;
    return self;
}

// The game's 0x004BF9E0: the destructor's body (no deleting). Puts the
// vtables back, frees the record vector and the buffers, releases the
// three windows and the preview, and ends in the base class's destructor.
void __fastcall Destruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0) = addr::cSC4ViewInputControlPlaceLot_Vtable;
    At<std::uintptr_t>(self, field::kMessagePart) = addr::cSC4ViewInputControlPlaceLot_MessageVtable;
    if (At<void*>(self, field::kRecords))
        Cdecl<void>(addr::SC4_Free, At<void*>(self, field::kRecords));
    if (At<void*>(self, field::kTexture))
        This<void>(addr::SC4TextureBinding_Release, At<void*>(self, field::kTexture));
    This<void>(addr::SC4RefVector_Destroy, self + field::kPreviewsB);
    This<void>(addr::SC4RefVector_Destroy, self + field::kPreviewsA);
    if (void* const preview = At<void*>(self, field::kPreview))
        Virtual<void>(preview, 0x8);
    if (void* const window = At<void*>(self, field::kWindowC))
        Virtual<void>(window, 0x8);
    if (void* const window = At<void*>(self, field::kWindowB))
        Virtual<void>(window, 0x8);
    if (void* const window = At<void*>(self, field::kWindowA))
        Virtual<void>(window, 0x8);
    At<std::uintptr_t>(self, field::kText) = addr::SC4String_Vtable;
    if (At<void*>(self, 0xC8))
        Cdecl<void>(addr::SC4_Free, At<void*>(self, 0xC8));
    At<std::uintptr_t>(self, field::kLabel) = addr::SC4String_Vtable;
    if (At<void*>(self, 0xB4))
        Cdecl<void>(addr::SC4_Free, At<void*>(self, 0xB4));
    This<void>(addr::SC4RefHolder_Destruct, self + field::kNameHolder);
    At<std::uintptr_t>(self, field::kName) = addr::SC4String_Vtable;
    if (At<void*>(self, 0x9C))
        Cdecl<void>(addr::SC4_Free, At<void*>(self, 0x9C));
    if (At<void*>(self, field::kBuildingIds))
        Cdecl<void>(addr::SC4_Free, At<void*>(self, field::kBuildingIds));
    char* const set = self + field::kConfigSet;
    This<void>(addr::SC4Uint32HashSet_Clear, set);
    if (At<void*>(set, 4))
        Cdecl<void>(addr::SC4_Free, At<void*>(set, 4));
    This<void>(addr::SC4ViewInputControl_Destruct, self);
}

// The game's 0x004C0070 (ret 4): the deleting destructor, slot 26.
char* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    Destruct(self, nullptr);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// Shuts a held window down (when it says it is set up) and lets it go.
void LetGoOfWindow(char* self, std::size_t offset)
{
    void* const window = At<void*>(self, offset);
    if (!window)
        return;
    if (Virtual<uint8_t>(window, 0x14))
        Virtual<void>(At<void*>(self, offset), 0x10, uint32_t{0});
    void* const again = At<void*>(self, offset);
    if (!again)
        return;
    At<void*>(self, offset) = nullptr;
    Virtual<void>(again, 0x8);
}

// Shuts the objects of a vector of references down (slot 0x10 with 0). The
// count is read again after each object, as the game does.
void ShutDownAll(char* self, std::size_t offset)
{
    const auto count = [&]() { return static_cast<uint32_t>((At<char*>(self, offset + 4) - At<char*>(self, offset)) >> 2); };
    if (count() == 0)
        return;
    uint32_t i = 0;
    do {
        void* const object = At<void**>(self, offset)[i];
        if (object)
            Virtual<void>(object, 0x10, uint32_t{0});
        ++i;
    } while (i < count());
}

// The game's 0x004C0090: lets go of everything the preview holds: the three
// windows, the texture, both vectors of preview objects, the preview itself
// (taken out of the prop manager first when its flag 1 is set), and the
// records (each preview object taken out and released, the vector emptied).
void __fastcall ClearPreview(char* self, void*)
{
    LetGoOfWindow(self, field::kWindowA);
    LetGoOfWindow(self, field::kWindowB);
    LetGoOfWindow(self, field::kWindowC);
    if (void* const texture = At<void*>(self, field::kTexture)) {
        At<void*>(self, field::kTexture) = nullptr;
        This<void>(addr::SC4TextureBinding_Release, texture);
    }
    char* const previewsA = self + field::kPreviewsA;
    ShutDownAll(self, field::kPreviewsA);
    This<void>(addr::SC4RefVector_Erase, previewsA, At<void*>(previewsA, 0), At<void*>(previewsA, 4));
    ShutDownAll(self, field::kPreviewsB);
    {
        char* const previewsB = self + field::kPreviewsB;
        void** const begin = At<void**>(previewsB, 0);
        void** const end = At<void**>(previewsB, 4);
        uint8_t tag;
        void** const kept = Cdecl<void**>(addr::SC4PtrVector_CopyForward, end, end, begin, &tag, 0);
        void** const oldEnd = At<void**>(previewsB, 4);
        for (void** p = kept; p != oldEnd; ++p) {
            if (*p)
                Virtual<void>(*p, 0x8);
        }
        At<void**>(previewsB, 4) = kept;
    }
    void* const props = Global<void*>(addr::SC4_Service43D10);
    if (void* const preview = At<void*>(self, field::kPreview)) {
        void* const occupant = Virtual<void*>(preview, 0xC);
        if (Virtual<uint8_t>(occupant, 0x80, uint32_t{1})) {
            void* const again = Virtual<void*>(At<void*>(self, field::kPreview), 0xC);
            Virtual<void>(props, 0x74, again);
        }
        if (void* const held = At<void*>(self, field::kPreview)) {
            At<void*>(self, field::kPreview) = nullptr;
            Virtual<void>(held, 0x8);
        }
    }
    for (char* record = At<char*>(self, field::kRecords); record != At<char*>(self, field::kRecords + 4); record += 0x14) {
        void* const holder = At<void*>(record, 0x10);
        void* const occupant = Virtual<void*>(holder, 0xC);
        if (Virtual<uint8_t>(occupant, 0x80, uint32_t{1})) {
            void* const again = Virtual<void*>(holder, 0xC);
            Virtual<void>(props, 0x74, again);
        }
        Virtual<void>(holder, 0x8);
    }
    char* const records = self + field::kRecords;
    uint8_t tag;
    char* const kept = Cdecl<char*>(addr::cSC4ViewInputControlPlaceLot_CopyForward, At<char*>(records, 4), At<char*>(records, 4),
                                    At<char*>(records, 0), &tag, 0);
    At<char*>(records, 4) = kept;
}

// The game's 0x004C0300: slot 4, Shutdown. With the base's flag at +4 clear
// it does nothing. Else: the overlay flag cleared and the view's overlay told;
// the cursor given back to the cursor service; the view told to drop its
// handler 0xE9C877FA, its overlay told 0 and refreshed; the window holder
// at +0xAC released; ClearPreview; the services at +0x68 and +0x4C given
// back; the six messages taken off the message server (the tool at +0x28);
// then the base class's Shutdown. Always true.
uint8_t __fastcall Shutdown(char* self, void*)
{
    if (At<uint8_t>(self, field::kActive) == 0)
        return 1;
    if (At<uint8_t>(self, field::kOverlayShown) != 0) {
        void* const view = At<void*>(self, field::kView);
        At<uint8_t>(self, field::kOverlayShown) = 0;
        if (view) {
            void* const overlay = Virtual<void*>(view, 0xC4);
            This<void>(addr::SC4ViewMarkers_F7D20E0, overlay);
        }
    }
    if (At<uint32_t>(self, field::kCursor) != 0) {
        void* const service = Cdecl<void*>(addr::SC4_Service43DC4Slot6);
        Virtual<void>(service, 0x14, At<uint32_t>(self, field::kCursor));
    }
    void* const view = At<void*>(self, field::kView);
    Virtual<void>(view, 0xA4, uint32_t{0xE9C877FA});
    Virtual<void>(At<void*>(self, field::kView), 0xB0, uint32_t{0});
    {
        void* const overlay = Virtual<void*>(At<void*>(self, field::kView), 0xC4);
        This<void>(addr::SC4ViewMarkers_F7D1D10, overlay, uint32_t{0});
    }
    {
        void* const overlay = Virtual<void*>(At<void*>(self, field::kView), 0xC4);
        This<void>(addr::SC4ViewMarkers_F7D20E0, overlay);
    }
    This<void>(addr::SC4ViewModel_Reset, self + field::kNameHolder);
    ClearPreview(self, nullptr);
    if (void* const held = At<void*>(self, field::kZoneService)) {
        Virtual<void>(held, 0x8);
        At<void*>(self, field::kZoneService) = nullptr;
    }
    if (void* const held = At<void*>(self, field::kLotConfigs)) {
        Virtual<void>(held, 0x8);
        At<void*>(self, field::kLotConfigs) = nullptr;
    }
    if (void* const server = Global<void*>(addr::SC4_MessageServer)) {
        for (const uint32_t message : kMessages)
            Virtual<void>(server, 0x18, self + field::kMessagePart, message);
    }
    This<void>(addr::SC4ViewInputControl_Shutdown, self);
    return 1;
}

// --- Registration --------------------------------------------------------------------------

bool InstallPlop1(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[32] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSC4PlaceLotOccupantFilter_cSC4PlaceLotOccupantFilter, reinterpret_cast<void*>(&FilterConstruct)},
        {addr::cSC4PlaceLotOccupantFilter_IsOccupantTypeIncluded, reinterpret_cast<void*>(&FilterIsTypeIncluded)},
        {addr::cSC4ViewInputControlPlaceLot_ClearOverlay, reinterpret_cast<void*>(&ClearOverlay)},
        {addr::cSC4PlaceLotConfigFilter_cSC4PlaceLotConfigFilter, reinterpret_cast<void*>(&ConfigFilterConstruct)},
        {addr::cSC4PlaceLotTypeFilter_cSC4PlaceLotTypeFilter, reinterpret_cast<void*>(&TypeFilterConstruct)},
        {addr::cSC4PlaceLotTypeFilter_Test, reinterpret_cast<void*>(&TypeFilterTest)},
        {addr::cSC4ViewInputControlPlaceLot_GroundEffect, reinterpret_cast<void*>(&GroundEffect)},
        {addr::cSC4ViewInputControlPlaceLot_MovePreview, reinterpret_cast<void*>(&MovePreview)},
        {addr::cSC4ViewInputControlPlaceLot_ScoreSide, reinterpret_cast<void*>(&ScoreSide)},
        {addr::cSC4ViewInputControlPlaceLot_SnapFootprint, reinterpret_cast<void*>(&SnapFootprint)},
        {addr::cSC4ViewInputControlPlaceLot_CopyRange, reinterpret_cast<void*>(&CopyRange)},
        {addr::cSC4ViewInputControlPlaceLot_FillRecords, reinterpret_cast<void*>(&FillRecords)},
        {addr::cSC4ViewInputControlPlaceLot_CopyForward, reinterpret_cast<void*>(&CopyForward)},
        {addr::cSC4ViewInputControlPlaceLot_PriceDemolition, reinterpret_cast<void*>(&PriceDemolition)},
        {addr::cSC4ViewInputControlPlaceLot_FitRotation, reinterpret_cast<void*>(&FitRotation)},
        {addr::cSC4PlaceLotConfigFilter_Test, reinterpret_cast<void*>(&ConfigFilterTest)},
        {addr::cSC4ViewInputControlPlaceLot_FloodCheck, reinterpret_cast<void*>(&FloodCheck)},
        {addr::cSC4ViewInputControlPlaceLot_InsertOverflow, reinterpret_cast<void*>(&InsertOverflow)},
        {addr::cSC4ViewInputControlPlaceLot_Destruct, reinterpret_cast<void*>(&Destruct)},
        {addr::cSC4ViewInputControlPlaceLot_FitNetworkPiece, reinterpret_cast<void*>(&FitNetworkPiece)},
        {addr::cSC4ViewInputControlPlaceLot_PushBack, reinterpret_cast<void*>(&PushBack)},
        {addr::cSC4ViewInputControlPlaceLot_DeletingDestructor, reinterpret_cast<void*>(&DeletingDestructor)},
        {addr::cSC4ViewInputControlPlaceLot_ClearPreview, reinterpret_cast<void*>(&ClearPreview)},
        {addr::cSC4ViewInputControlPlaceLot_Shutdown, reinterpret_cast<void*>(&Shutdown)},
        {addr::cSC4ViewInputControlPlaceLot_ChooseLotConfiguration, reinterpret_cast<void*>(&ChooseLotConfiguration)},
        {addr::cSC4ViewInputControlPlaceLot_ChooseBuilding, reinterpret_cast<void*>(&ChooseBuilding)},
        {addr::SC4ViewInputControls_F4C0430, reinterpret_cast<void*>(&Construct)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    static_assert(count <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < count; ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].first);
        if (!found || !hook::Install(*found, hooks[i].second, &originals[i]))
            return false;
    }
    return true;
}

const bool g_plop1Registered = registry::Register({
    "ui.tools.plop1",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4ViewInputControlPlaceLot_Shutdown,
    "Our plop tool, first part: its filters, the preview's moving and fitting, the record vector, construction, "
    "destruction and Shutdown (docs/ui.md)",
    false,
    InstallPlop1,
});

} // namespace
} // namespace opensc4::ui
