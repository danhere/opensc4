// The label tool of the city view (0x004BC980 to 0x004BDAFC).
// Replacement, off by default:
//
//   ui.tools.decal: the view input control of 0x44 bytes (vtable 0x00A90528,
//     window procedure part 0x00A90518 at +0x28, ID 0x8B3CE416) the view
//     makes on its command 0x0B980587, and the occupant filter of its picks
//     (0xC bytes, vtables 0x00A904FC and 0x00A904E4).
//
// The tool writes labels on the ground: text decals (cSC4PropOccupantTextDecal,
// 0xBC bytes, kind 0xCB79919B, interface 0x2B3B7D86). A click on a cell
// without a label makes one, adds it to the city (the prop manager's
// AddCityProp) and asks for its text in a modal window (layout 0x6B704690,
// window 0x8A8DFCF5, edit field 0xAA4149BF, at most 32 characters); with no
// text, or when the window is cancelled, the label is removed again. A
// click on a label and letting go without moving asks for its text again;
// moving the pointer with the button down drags the label. Wherever a label
// is put, Place sets its colour, size, position and direction from what is
// in the cell: on water blue, on a road, street, avenue or highway yellow
// and turned and shifted along the piece (RoadPlacement), on rail the same
// by RailPlacement, elsewhere green. Home and End turn the label under the
// pointer; Delete removes it.
//
// Nothing here draws: the label's picture is the decal's own. Nothing reads
// a clock or draws a random number; the text window runs the window
// manager's modal loop.
//
// Fields: +0x2C the state (0 idle, 1 a label pressed, 2 dragging it), +0x30
// the label (referenced; while idle the one under the pointer, shown at 0.7
// through its slot 10, 0.5 again when let go), +0x34 three floats: the
// label's position less the terrain point when it was pressed (written, not
// read), +0x40 the direction a label is placed with (1 from the
// constructor).
//
// Left the game's: the part's QueryInterface thunk 0x004BD2A0 and the jumps
// 0x004BD280 and 0x004BD290 to AddRef and Release.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::tool_decal {
namespace {

// The base class's fields.
namespace base {
constexpr std::size_t kInited = 0x04; // byte
constexpr std::size_t kId = 0x0C;
constexpr std::size_t kCursor = 0x10; // cursor ID
} // namespace base

// The tool's fields.
namespace field {
constexpr std::size_t kProc = 0x28;      // the window procedure part (vtable 0x00A90518)
constexpr std::size_t kState = 0x2C;     // 0 idle, 1 pressed, 2 dragging
constexpr std::size_t kLabel = 0x30;     // the label (interface 0x2B3B7D86, referenced)
constexpr std::size_t kGrab = 0x34;      // three floats: the label less the picked point
constexpr std::size_t kDirection = 0x40; // the direction labels are placed with
} // namespace field

constexpr int32_t kIdle = 0, kPressed = 1, kDragging = 2;

constexpr uint32_t kToolId = 0x8B3CE416;
constexpr uint32_t kCursorId = 0xEB72FEF4;
constexpr uint32_t kTypeProp = 0xA823821E;
constexpr uint32_t kIidLabel = 0x2B3B7D86;
constexpr uint32_t kEditIid = 0x231A1C57;
constexpr uint32_t kTextWindow = 0x8A8DFCF5;
constexpr uint32_t kTextEdit = 0xAA4149BF;
constexpr uint32_t kOkButton = 0x8A8DFD4E;
constexpr uint32_t kCancelButton = 0x8A8DFD4A;
constexpr uint32_t kCloseButton = 0x6BA18178;
constexpr uint32_t kClicked = 0x287259F6;
constexpr uint32_t kSoundPlay = 0xEA451903;
constexpr uint32_t kSoundDown = 0x2C16DA29; // a label put down
constexpr uint32_t kSoundUp = 0x2C16DA45;   // a label picked up
constexpr float kShown = 0.5f;              // a label's level away from the pointer
constexpr float kLit = 0.7f;                // and under it

struct StringBody {
    char* begin;
    char* end;
    char* capacity;
};

// A game string: 0x14 bytes.
struct GameString {
    std::uintptr_t vtable;
    StringBody body;
    uint32_t zero;
};
static_assert(sizeof(GameString) == 0x14);

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

// The four bytes of a float as the game pushes them.
uint32_t Bits(float& value)
{
    return At<uint32_t>(&value, 0);
}

// The game's inlined string destructor.
void DestroyString(GameString& text)
{
    text.vtable = addr::SC4String_Vtable;
    if (text.body.begin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(text.body.begin));
}

// The sound service, when there is one, plays a sound of the tool.
void Play(uint32_t sound)
{
    if (void* const service = Global<void*>(addr::SC4_Sound43CB8))
        Virtual<void>(service, 0x2C, kSoundPlay, sound, uint32_t{0}, uint32_t{0}, uint32_t{0});
}

// The tool lets go of its label: the field cleared first.
void DropLabel(char* self)
{
    if (void* const label = At<void*>(self, field::kLabel)) {
        At<void*>(self, field::kLabel) = nullptr;
        Release(label);
    }
}

// The terrain point under a screen point (the renderer's PickTerrain, slot
// 0xF0, with 0 and 1) into three floats that start as the zero at
// 0x00B21830.
bool PickGround(void* renderer, int32_t x, int32_t z, float* point)
{
    point[0] = point[1] = point[2] = Global<float>(addr::SC4LabelTool_Zero);
    return Virtual<uint8_t>(renderer, 0xF0, x, z, point, uint32_t{0}, uint32_t{1}) != 0;
}

// The game's 0x004BC980 (slot 3): Init. The ID and the cursor, the city's
// signs and labels shown (the prop developer's slot 8 with true), the base
// Init.
uint8_t __fastcall Init(char* self, void*)
{
    if (At<uint8_t>(self, base::kInited))
        return 1;
    At<uint32_t>(self, base::kId) = kToolId;
    At<uint32_t>(self, base::kCursor) = kCursorId;
    Virtual<void>(Global<void*>(addr::SC4_Service43DB8), 0x20, uint32_t{1}); // ShowSignsAndLabels
    This<uint8_t>(addr::SC4ViewInputControl_Init, self);
    return 1;
}

// The game's 0x004BC9B0 (slot 3 of the filter, ret 4): whether the picks
// take an occupant. A prop occupant (type 0xA823821E) that answers the
// label interface. `this` is not read.
uint8_t __fastcall FilterIncludes(char*, void*, void* occupant)
{
    if (Virtual<uint32_t>(occupant, 0x1C) != kTypeProp)
        return 0;
    void* label = nullptr;
    const uint8_t found = Virtual<uint8_t>(occupant, 0x0, kIidLabel, &label);
    if (label)
        Release(label);
    return found ? 1 : 0;
}

// The game's 0x004BCA10 (slot 3 of the part at +0x28, ret 8): the window
// procedure of the text window. `this` is not read. A message is its type
// and three dwords.
//   3, a command 0x287259F6 (a button clicked) of the OK, cancel or close
//     button: the window's manager (slot 6) ends the modal run with the
//     button's ID (its slot 9 with 0xF, the window and the ID); true.
//   0x11: the keyboard goes to the edit field (the manager's slot 37 with
//     the field's window); true when the window has the field. The window
//     is not checked for null here.
//   0x1A with 4 in the second dword and key 0x0D (Enter) in the record the
//     third points to: the run ends as with the OK button; false.
// False for anything else.
uint8_t __fastcall DoWinProc(char*, void*, void* window, uint32_t* message)
{
    const uint32_t type = message[0];
    if (type == 3) {
        if (message[1] != kClicked)
            return 0;
        const uint32_t button = message[2];
        if (button != kCloseButton && button != kCancelButton && button != kOkButton)
            return 0;
        if (window) {
            void* const manager = Virtual<void*>(window, 0x18);
            Virtual<void>(manager, 0x24, uint32_t{0xF}, window, message[2]);
        }
        return 1;
    }
    if (type == 0x11) {
        void* edit; // not set before the call, as in the game
        if (!Virtual<uint8_t>(window, 0x94, kTextEdit, kEditIid, &edit))
            return 0;
        void* const manager = Virtual<void*>(window, 0x18);
        Virtual<void>(manager, 0x94, Virtual<void*>(edit, 0x14));
        Release(edit);
        return 1;
    }
    if (type == 0x1A) {
        if (message[2] == 4 && At<uint32_t>(reinterpret_cast<void*>(message[3]), 0x4) == 0x0D && window) {
            void* const manager = Virtual<void*>(window, 0x18);
            Virtual<void>(manager, 0x24, uint32_t{0xF}, window, kOkButton);
        }
        return 0;
    }
    return 0;
}

// The game's 0x004BCB00 (ret 8): where and which way a label goes on a
// road. `this` is not read. The direction starts as 1. The position's cell
// (the city's slot 95) is asked of the traffic network (slot 8, mask 0x40B,
// 0) for its network piece; the piece's kind is 1, else 0xA, else 0, else 3
// (its slot 22, the later test winning), and its edge value for that kind
// (slot 38) has one byte a side, the low three bits saying what leaves
// there (1 and 3 count as slanted). One side: direction 1 when sides 1 and
// 3 are both empty, else 0. Two or three sides: straight through sides 0
// and 2 gives 1, through 1 and 3 gives 0; a slanted bend gives 2 or 3 and a
// shift of 4 each way toward the bend. Then the position becomes the cell's
// centre (slot 97; y is left) plus the shift. Without a piece only the
// centring happens.
void __fastcall RoadPlacement(char*, void*, float* position, int32_t* direction)
{
    *direction = 1;
    int32_t cellX;
    int32_t cellZ;
    Virtual<void>(Global<void*>(addr::SC4_City), 0x17C, Bits(position[0]), Bits(position[2]), &cellX, &cellZ);
    float shiftX = 0.0f;
    float shiftZ = 0.0f;
    void* const network = Global<void*>(addr::SC4_Service43D54);
    void* const piece = Virtual<void*>(network, 0x20, cellX, cellZ, uint32_t{0x40B}, uint32_t{0});
    if (piece) {
        uint32_t kind = 3;
        if (Virtual<uint8_t>(piece, 0x58, uint32_t{0}))
            kind = 0;
        if (Virtual<uint8_t>(piece, 0x58, uint32_t{0xA}))
            kind = 0xA;
        if (Virtual<uint8_t>(piece, 0x58, uint32_t{1}))
            kind = 1;
        const uint32_t edges = Virtual<uint32_t>(piece, 0x98, kind);
        bool used[4];
        bool slanted[4];
        int32_t count = 0;
        for (int side = 0; side < 4; ++side) {
            const uint32_t edge = (edges >> (8 * side)) & 7;
            used[side] = edge != 0;
            slanted[side] = edge == 1 || edge == 3;
            if (used[side])
                ++count;
        }
        if (count == 1) {
            *direction = (!used[1] && !used[3]) ? 1 : 0;
        } else if (count == 2 || count == 3) {
            if (used[0] && used[2] && !slanted[2]) {
                *direction = 1;
            } else if (used[1] && used[3] && !slanted[1]) {
                *direction = 0;
            } else if (used[0] && used[1] && slanted[1]) {
                *direction = 2;
                shiftX = -4.0f;
                shiftZ = -4.0f;
            } else if (used[2] && used[3] && slanted[3]) {
                *direction = 2;
                shiftX = 4.0f;
                shiftZ = 4.0f;
            } else if (used[0] && used[3] && slanted[3]) {
                *direction = 3;
                shiftX = -4.0f;
                shiftZ = 4.0f;
            } else if (used[1] && used[2] && slanted[2]) {
                *direction = 3;
                shiftX = 4.0f;
                shiftZ = -4.0f;
            }
        }
    }
    Virtual<void>(Global<void*>(addr::SC4_City), 0x184, cellX, cellZ, &position[0], &position[2]);
    position[0] = static_cast<float>(static_cast<double>(shiftX) + static_cast<double>(position[0]));
    position[2] = static_cast<float>(static_cast<double>(shiftZ) + static_cast<double>(position[2]));
}

// The game's 0x004BCDC0 (ret 4): the label standing at a terrain point, or
// null: the occupant manager's first occupant at the point that a new
// filter takes (slot 28), asked for the label interface. The pointer comes
// back already released: whoever keeps it references it again. `this` is
// not read.
void* __fastcall LabelAt(char*, void*, float* point)
{
    void* occupant = nullptr;
    void* label = nullptr;
    char* const filter = Cdecl<char*>(addr::SC4_operator_new, std::size_t{0xC});
    if (filter) {
        This<void*>(addr::SC4_OccupantFilterBase, filter);
        At<std::uintptr_t>(filter, 0x0) = addr::SC4LabelTool_FilterVtable;
        At<std::uintptr_t>(filter, 0x4) = addr::SC4LabelTool_FilterVtable4;
        AddRef(filter);
    }
    Virtual<void>(Global<void*>(addr::SC4_NetworkService), 0x70, &occupant, Bits(point[0]), Bits(point[2]),
                  static_cast<void*>(filter));
    if (occupant)
        Virtual<uint8_t>(occupant, 0x0, kIidLabel, &label);
    if (label)
        Release(label);
    if (filter)
        Release(filter);
    if (occupant)
        Release(occupant);
    return label;
}

// The game's 0x004BCE80 (ret 4): the tool's label changes. The new one is
// shown at 0.7 (its slot 10), the old one at 0.5 again, and the new one is
// kept (referenced; the old one released). Nothing when it is the same
// label.
void __fastcall SetLabel(char* self, void*, void* label)
{
    if (label == At<void*>(self, field::kLabel))
        return;
    if (label)
        Virtual<void>(label, 0x28, kLit);
    if (void* const shown = At<void*>(self, field::kLabel))
        Virtual<void>(shown, 0x28, kShown);
    void* const old = At<void*>(self, field::kLabel);
    if (label == old)
        return;
    if (label)
        AddRef(label);
    At<void*>(self, field::kLabel) = label;
    if (old)
        Release(old);
}

// The four shifts RailPlacement adds up, one a side, in the game's own
// table at 0x00B2180C (pairs of x and z), filled on first use under bit 0 of
// 0x00B2182C.
const float* RailShifts()
{
    float* const table = &Global<float>(addr::SC4LabelTool_RailShifts);
    uint32_t& guard = Global<uint32_t>(addr::SC4LabelTool_RailShiftsGuard);
    if (!(guard & 1)) {
        guard |= 1;
        table[0] = -8.0f;
        table[1] = 0.0f;
        table[2] = 0.0f;
        table[3] = -8.0f;
        table[4] = 8.0f;
        table[5] = 0.0f;
        table[6] = 0.0f;
        table[7] = 8.0f;
    }
    return table;
}

// The game's 0x004BCEE0 (ret 8): where and which way a label goes on rail.
// `this` is not read. The direction starts as 1. The traffic network's
// piece at the position (slot 7, mask 0x40, 1) gives its edge value for
// kind 6 (slot 38): a byte a side, the low two bits saying what leaves
// there (1 and 3 count as slanted, 2 as the other kind) and bit 2 a flag.
// When every used side is slanted: all four used gives direction 2 or 3 by
// side 0; else the first of side 0 and side 2 that is slanted gives the
// direction and a shift of 8 each way. Otherwise, with two slanted sides of
// four used, sides 0 or 2 being of the other kind pick direction 2 or 3 and
// the shift by side 3; in every other case the flagged sides' table shifts
// are added up (side 0 sets, the others add) and the direction is 1 when
// side 0 or 2 is of the other kind, else 0. Then the position becomes its
// cell's centre (the city's slots 95 and 97; y is left) plus the shift.
// Without a piece nothing but the direction is touched.
void __fastcall RailPlacement(char*, void*, float* position, int32_t* direction)
{
    *direction = 1;
    int32_t cellX;
    int32_t cellZ;
    Virtual<void>(Global<void*>(addr::SC4_City), 0x17C, Bits(position[0]), Bits(position[2]), &cellX, &cellZ);
    void* const network = Global<void*>(addr::SC4_Service43D54);
    void* const piece = Virtual<void*>(network, 0x1C, position, uint32_t{0x40}, uint32_t{1});
    if (!piece)
        return;
    float shiftX = 0.0f;
    float shiftZ = 0.0f;
    const uint32_t edges = Virtual<uint32_t>(piece, 0x98, uint32_t{6});
    uint32_t edge[4];
    bool flagged[4];
    int32_t slanted = 0;
    int32_t used = 0;
    for (int side = 0; side < 4; ++side) {
        const uint32_t value = edges >> (8 * side);
        edge[side] = value & 3;
        flagged[side] = ((value >> 2) & 1) != 0;
        if (edge[side] == 1 || edge[side] == 3)
            ++slanted;
        if (edge[side] != 0)
            ++used;
    }
    if (slanted != used) {
        if (slanted == 2 && used == 4) {
            if (edge[0] == 2) {
                shiftX = -8.0f;
                if (edge[3] == 2) {
                    *direction = 2;
                    shiftZ = 8.0f;
                } else {
                    *direction = 3;
                    shiftZ = -8.0f;
                }
            } else if (edge[2] == 2) {
                shiftX = 8.0f;
                if (edge[3] == 2) {
                    *direction = 3;
                    shiftZ = 8.0f;
                } else {
                    *direction = 2;
                    shiftZ = -8.0f;
                }
            }
        } else {
            const float* const table = RailShifts();
            if (flagged[0]) {
                shiftX = table[0];
                shiftZ = table[1];
            }
            for (int side = 1; side < 4; ++side) {
                if (flagged[side]) {
                    shiftX = static_cast<float>(static_cast<double>(table[2 * side]) + static_cast<double>(shiftX));
                    shiftZ = static_cast<float>(static_cast<double>(table[2 * side + 1]) + static_cast<double>(shiftZ));
                }
            }
            *direction = (edge[0] == 2 || edge[2] == 2) ? 1 : 0;
        }
    } else if (used == 4) {
        *direction = edge[0] != 1 ? 3 : 2;
    } else if (edge[0] == 1) {
        *direction = 2;
        shiftX = -8.0f;
        shiftZ = -8.0f;
    } else if (edge[0] == 3) {
        *direction = 3;
        shiftX = -8.0f;
        shiftZ = 8.0f;
    } else if (edge[2] == 1) {
        *direction = 2;
        shiftX = 8.0f;
        shiftZ = 8.0f;
    } else if (edge[2] == 3) {
        *direction = 3;
        shiftX = 8.0f;
        shiftZ = -8.0f;
    }
    Virtual<void>(Global<void*>(addr::SC4_City), 0x184, cellX, cellZ, &position[0], &position[2]);
    position[0] = static_cast<float>(static_cast<double>(shiftX) + static_cast<double>(position[0]));
    position[2] = static_cast<float>(static_cast<double>(shiftZ) + static_cast<double>(position[2]));
}

// The game's 0x004BD230: the constructor; the caller allocated 0x44 bytes.
char* __fastcall Construct(char* self, void*)
{
    This<void*>(addr::SC4ViewInputControl_Construct, self);
    At<std::uintptr_t>(self, field::kProc) = addr::SC4KeyFilter_BaseVtable;
    At<std::uintptr_t>(self, 0) = addr::SC4LabelTool_Vtable;
    At<std::uintptr_t>(self, field::kProc) = addr::SC4LabelTool_ProcVtable;
    At<int32_t>(self, field::kState) = kIdle;
    At<void*>(self, field::kLabel) = nullptr;
    At<uint32_t>(self, field::kGrab) = Global<uint32_t>(addr::SC4LabelTool_Zero);
    At<uint32_t>(self, field::kGrab + 0x4) = Global<uint32_t>(addr::SC4LabelTool_Zero);
    At<uint32_t>(self, field::kGrab + 0x8) = Global<uint32_t>(addr::SC4LabelTool_Zero);
    At<int32_t>(self, field::kDirection) = 1;
    return self;
}

// The game's 0x004BD2B0 (slot 26, ret 4): the deleting destructor. The
// label released, then the base.
char* __fastcall DeletingDestruct(char* self, void*, uint32_t flags)
{
    if (void* const label = At<void*>(self, field::kLabel))
        Release(label);
    This<void>(addr::SC4ViewInputControl_Destruct, self);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// The tool's label is shown at 0.5 again and let go.
void UnlightAndDrop(char* self)
{
    if (void* const label = At<void*>(self, field::kLabel)) {
        Virtual<void>(label, 0x28, kShown);
        DropLabel(self);
    }
}

// The game's 0x004BD2E0 (slot 4): Shutdown. The label goes back to 0.5 and
// is let go, then the base.
uint8_t __fastcall Shutdown(char* self, void*)
{
    if (!At<uint8_t>(self, base::kInited))
        return 1;
    UnlightAndDrop(self);
    This<uint8_t>(addr::SC4ViewInputControl_Shutdown, self);
    return 1;
}

// The game's 0x004BD320 (ret 0x10): puts a label at a position (three
// floats by value) with the look of what is in its cell. Nothing without a
// label. The cell is the city's slot 95; the terrain's slot 34 says whether
// it is water; the traffic network's slot 8 (mask 0x44B, 0) gives the
// network piece there.
//   water: colour 0.5, 0.5, 1; size 2; the tool's direction.
//   a piece with flag 0x40 (its slot 8; rail): colour 1, 0.9 (0x3F66E6E7),
//     0; size 2; position and direction from RailPlacement.
//   another piece: the same colour; size 0.8 when it has any of the flags
//     0x403 (slot 9), else 0.64; position and direction from RoadPlacement.
//   nothing: colour 0.5, 1, 0.5; size 2; the tool's direction.
// The label gets the colour (slot 8), the size (slot 6), its occupant the
// position (slot 9) and the label the direction (slot 9).
void __fastcall Place(char* self, void*, float x, float y, float z, void* label)
{
    if (!label)
        return;
    float position[3] = {x, y, z};
    int32_t cellX;
    int32_t cellZ;
    Virtual<void>(Global<void*>(addr::SC4_City), 0x17C, Bits(position[0]), Bits(position[2]), &cellX, &cellZ);
    const uint8_t water = Virtual<uint8_t>(Global<void*>(addr::SC4_Terrain), 0x88, cellX, cellZ);
    void* const network = Global<void*>(addr::SC4_Service43D54);
    void* const piece = Virtual<void*>(network, 0x20, cellX, cellZ, uint32_t{0x44B}, uint32_t{0});
    int32_t direction = At<int32_t>(self, field::kDirection);
    float size = 2.0f;
    float colour[3];
    if (water) {
        colour[0] = Global<float>(addr::SC4LabelTool_Half);
        colour[1] = 0.5f;
        colour[2] = 1.0f;
    } else if (piece) {
        colour[0] = 1.0f;
        At<uint32_t>(&colour[1], 0) = 0x3F66E6E7;
        colour[2] = 0.0f;
        if (Virtual<uint8_t>(piece, 0x20, uint32_t{0x40})) {
            RailPlacement(self, nullptr, &position[0], &direction);
        } else {
            size = Virtual<uint8_t>(piece, 0x24, uint32_t{0x403}) ? 0.8f : 0.64f;
            RoadPlacement(self, nullptr, &position[0], &direction);
        }
    } else {
        colour[0] = Global<float>(addr::SC4LabelTool_Half);
        colour[1] = 1.0f;
        colour[2] = 0.5f;
    }
    Virtual<void>(label, 0x20, colour[0], colour[1], colour[2]);
    Virtual<void>(label, 0x18, size);
    Virtual<void>(Virtual<void*>(label, 0xC), 0x24, &position[0]);
    Virtual<void>(label, 0x24, direction);
}

// The game's 0x004BD4B0 (slot 14, OnKeyDown, ret 8). Delete (0x2E) with a
// label and no button down: the tool lets go of it, the prop manager
// removes it (RemoveProp, slot 29); true. Home (0x24) and End (0x23) with a
// label: the tool's direction steps through the table at 0x00A905A0 (two
// entries a direction: Home's and End's) and the label is placed again
// where it stands; false. False for anything else.
uint8_t __fastcall OnKeyDown(char* self, void*, uint32_t key, uint32_t)
{
    if (key == 0x2E) {
        void* const label = At<void*>(self, field::kLabel);
        if (!label || At<int32_t>(self, field::kState) != kIdle)
            return 0;
        AddRef(label);
        DropLabel(self);
        Virtual<uint8_t>(Global<void*>(addr::cSC4PropManager_Global), 0x74, Virtual<void*>(label, 0xC));
        Release(label);
        return 1;
    }
    if (key == 0x24 || key == 0x23) {
        void* const label = At<void*>(self, field::kLabel);
        if (!label)
            return 0;
        const int32_t index = (key != 0x24 ? 1 : 0) + At<int32_t>(self, field::kDirection) * 2;
        At<int32_t>(self, field::kDirection) = Global<int32_t>(addr::SC4LabelTool_Turns + 4 * index);
        float position[3];
        position[0] = position[1] = position[2] = Global<float>(addr::SC4LabelTool_Zero);
        Virtual<void>(Virtual<void*>(label, 0xC), 0x20, &position[0]); // GetPosition
        Place(self, nullptr, position[0], position[1], position[2], At<void*>(self, field::kLabel));
    }
    return 0;
}

// The game's 0x004BD5A0 (slot 20, OnMouseMove, ret 0xC). A pressed label
// starts to be dragged (its slot 7 with true); a dragged one is placed at
// the terrain point under the pointer (the offset at +0x34 is not used).
// While idle, with a renderer, the label at the terrain point under the
// pointer becomes the tool's label, or none. True always.
uint8_t __fastcall OnMouseMove(char* self, void*, int32_t x, int32_t z, uint32_t)
{
    if (At<int32_t>(self, field::kState) == kPressed && At<void*>(self, field::kLabel)) {
        At<int32_t>(self, field::kState) = kDragging;
        Virtual<void>(At<void*>(self, field::kLabel), 0x1C, uint32_t{1});
    }
    if (At<int32_t>(self, field::kState) == kDragging) {
        if (void* const renderer = Global<void*>(addr::SC4_View43DD0)) {
            float point[3];
            if (PickGround(renderer, x, z, point))
                Place(self, nullptr, point[0], point[1], point[2], At<void*>(self, field::kLabel));
        }
    }
    void* const renderer = Global<void*>(addr::SC4_View43DD0);
    if (At<int32_t>(self, field::kState) == kIdle && renderer) {
        float point[3];
        void* found = nullptr;
        if (PickGround(renderer, x, z, point))
            This<void>(addr::SC4RefPtr_Assign, &found, LabelAt(self, nullptr, point));
        SetLabel(self, nullptr, found);
        if (found)
            Release(found);
    }
    return 1;
}

// The game's 0x004BD6B0 (ret 4): asks for a label's text in a modal window
// and says whether the label stays. True at once without a label or when
// the window does not load. The window 0x8A8DFCF5 of the layout
// 0x6B704690 (group 0x96A006B0) is loaded under the window at 0x00B43DC8
// with the tool's part at +0x28 as its procedure; its edit field
// 0xAA4149BF gets the label's text, all selected, and a limit of 32. The
// keyboard goes to the manager's slot 3 window, then the manager runs the
// window (slot 41). Ended with the OK button: the field's text goes to the
// label (its slot 4) and the label stays unless the text is empty. Ended
// with the cancel button: the label does not stay. Anything else: it stays
// as it was. After that the keyboard goes back to the window at 0x00B43DC8
// and the text window is closed (its slot 5) and released.
uint8_t __fastcall EditText(char* self, void*, void* label)
{
    if (!label)
        return 1;
    uint8_t keep = 1;
    uint32_t key[3] = {0, 0x96A006B0, 0x6B704690};
    void* const parent = Global<void*>(addr::SC4_Service43DC8);
    void* const window = Cdecl<void*>(addr::SC4UI_LoadWindowChild, &key[0], parent, kTextWindow);
    if (!window)
        return keep;
    void* const manager = Cdecl<void*>(addr::SC4_GetWinManager);
    Virtual<void>(window, 0x1D0, static_cast<void*>(self ? self + field::kProc : nullptr));
    void* edit; // not set before the calls, as in the game
    if (Virtual<uint8_t>(window, 0x94, kTextEdit, kEditIid, &edit)) {
        GameString text;
        This<void*>(addr::SC4String_SC4String, &text);
        Virtual<void>(label, 0x14, &text);
        Virtual<void>(edit, 0xC0, &text, uint32_t{0});
        Virtual<void>(edit, 0x12C, uint32_t{0}, This<uint32_t>(addr::cRZBaseString_Strlen, &text));
        Virtual<void>(edit, 0x84, uint32_t{0x20});
        Release(edit);
        DestroyString(text);
    }
    Virtual<void>(manager, 0x94, Virtual<void*>(manager, 0xC));
    const uint32_t ended = Virtual<uint32_t>(manager, 0xA4, window); // modal: returns when the window closes
    if (ended == kOkButton) {
        if (Virtual<uint8_t>(window, 0x94, kTextEdit, kEditIid, &edit)) {
            GameString text;
            This<void*>(addr::SC4String_SC4String, &text);
            Virtual<void>(edit, 0xB4, &text);
            if (text.body.end == text.body.begin)
                keep = 0;
            Virtual<void>(label, 0x10, &text);
            Release(edit);
            DestroyString(text);
        }
    } else if (ended == kCancelButton) {
        keep = 0;
    }
    Virtual<void>(manager, 0x94, Global<void*>(addr::SC4_Service43DC8));
    Virtual<void>(window, 0x14);
    Release(window);
    return keep;
}

// The game's 0x004BD890 (slot 16, OnMouseDownL, ret 0xC). True without a
// renderer; false without a terrain point under the pointer. A label at the
// point is pressed: it becomes the tool's label, +0x34 takes its position
// less the point, and the pick-up sound plays. With none a new label (0xBC
// bytes from its pool) is added to the city (AddCityProp, slot 19) and its
// text asked for: without a text it is removed again; with one the tool's
// direction becomes 1 when the camera's rotation (the renderer's slot 10
// object, +0x18) is 0 or 2, else 0, the label is placed at the point and
// becomes the tool's label. A failed allocation is not checked, as in the
// game.
uint8_t __fastcall OnMouseDownL(char* self, void*, int32_t x, int32_t z, uint32_t)
{
    void* const renderer = Global<void*>(addr::SC4_View43DD0);
    if (!renderer)
        return 1;
    float point[3];
    if (!PickGround(renderer, x, z, point))
        return 0;
    if (void* const label = LabelAt(self, nullptr, point)) {
        AddRef(label);
        At<int32_t>(self, field::kState) = kPressed;
        SetLabel(self, nullptr, label);
        float* const grab = &At<float>(self, field::kGrab);
        Virtual<void>(Virtual<void*>(At<void*>(self, field::kLabel), 0xC), 0x20, grab); // GetPosition
        for (int i = 0; i < 3; ++i)
            grab[i] = static_cast<float>(static_cast<double>(grab[i]) - static_cast<double>(point[i]));
        Play(kSoundUp);
        Release(label);
        return 1;
    }

    void* const memory = Cdecl<void*>(addr::cSC4PropOccupantTextDecal_Allocate, std::size_t{0xBC});
    void* const label = memory ? This<void*>(addr::cSC4PropOccupantTextDecal_Construct, memory) : nullptr;
    if (label)
        AddRef(label);
    Virtual<uint8_t>(Global<void*>(addr::cSC4PropManager_Global), 0x4C, Virtual<void*>(label, 0xC));
    if (!EditText(self, nullptr, label)) {
        Virtual<uint8_t>(Global<void*>(addr::cSC4PropManager_Global), 0x74, Virtual<void*>(label, 0xC));
        Release(label);
        return 1;
    }
    void* const camera = Virtual<void*>(Global<void*>(addr::SC4_View43DD0), 0x28);
    const int32_t rotation = At<int32_t>(camera, 0x18);
    At<int32_t>(self, field::kDirection) = (rotation == 0 || rotation == 2) ? 1 : 0;
    Place(self, nullptr, point[0], point[1], point[2], label);
    SetLabel(self, nullptr, label);
    Release(label);
    return 1;
}

// The game's 0x004BDA40 (slot 18, OnMouseUpL, ret 0xC). A label pressed
// and not moved has its text asked for; when it does not stay, the tool
// lets go of it (0.5 again) and the prop manager removes it (the label is
// not checked for null, as in the game). A dragged label stops being
// dragged (its slot 7 with false) and the put-down sound plays. Idle after,
// true always.
uint8_t __fastcall OnMouseUpL(char* self, void*, int32_t, int32_t, uint32_t)
{
    if (At<int32_t>(self, field::kState) == kPressed) {
        if (!EditText(self, nullptr, At<void*>(self, field::kLabel))) {
            void* const label = At<void*>(self, field::kLabel);
            if (label)
                AddRef(label);
            UnlightAndDrop(self);
            Virtual<uint8_t>(Global<void*>(addr::cSC4PropManager_Global), 0x74, Virtual<void*>(label, 0xC));
            Release(label);
        }
    } else if (At<int32_t>(self, field::kState) == kDragging) {
        if (void* const label = At<void*>(self, field::kLabel))
            Virtual<void>(label, 0x1C, uint32_t{0});
        Play(kSoundDown);
    }
    At<int32_t>(self, field::kState) = kIdle;
    return 1;
}

// --- Registration ----------------------------------------------------------------------

bool InstallLabelTool(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[16] = {};
    static const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::SC4LabelTool_Init, reinterpret_cast<void*>(&Init)},
        {addr::SC4LabelTool_FilterIncludes, reinterpret_cast<void*>(&FilterIncludes)},
        {addr::SC4LabelTool_DoWinProc, reinterpret_cast<void*>(&DoWinProc)},
        {addr::SC4LabelTool_RoadPlacement, reinterpret_cast<void*>(&RoadPlacement)},
        {addr::SC4LabelTool_LabelAt, reinterpret_cast<void*>(&LabelAt)},
        {addr::SC4LabelTool_SetLabel, reinterpret_cast<void*>(&SetLabel)},
        {addr::SC4LabelTool_RailPlacement, reinterpret_cast<void*>(&RailPlacement)},
        {addr::SC4LabelTool_Construct, reinterpret_cast<void*>(&Construct)},
        {addr::SC4LabelTool_DeletingDestructor, reinterpret_cast<void*>(&DeletingDestruct)},
        {addr::SC4LabelTool_Shutdown, reinterpret_cast<void*>(&Shutdown)},
        {addr::SC4LabelTool_Place, reinterpret_cast<void*>(&Place)},
        {addr::SC4LabelTool_OnKeyDown, reinterpret_cast<void*>(&OnKeyDown)},
        {addr::SC4LabelTool_OnMouseMove, reinterpret_cast<void*>(&OnMouseMove)},
        {addr::SC4LabelTool_EditText, reinterpret_cast<void*>(&EditText)},
        {addr::SC4LabelTool_OnMouseDownL, reinterpret_cast<void*>(&OnMouseDownL)},
        {addr::SC4LabelTool_OnMouseUpL, reinterpret_cast<void*>(&OnMouseUpL)},
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
    "ui.tools.decal",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4LabelTool_Place,
    "Our label tool of the city view: text decals on the ground (docs/tools.md)",
    false,
    InstallLabelTool,
});

} // namespace
} // namespace opensc4::tool_decal
