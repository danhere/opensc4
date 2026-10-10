// The MySim tools, second part (0x00439380 to 0x0043B200; docs/ui.md): the
// dispatch tool, the move-in tool and helpers of the MySim import that
// follow them.
//
//   The dispatch tool (0x90 bytes, cSC4MySim makes it on message 0x0BB04126
//     and in CreateDispatchTool): a view input control (vtable 0x00A85998)
//     with a message target at +0x28 (0x00A85988). The player picks the
//     MySim's figure in the city up and puts it down somewhere else. Fields:
//     +0x30 whether the place under the cursor is allowed, +0x34 and +0x38
//     the cursor's offset from the figure when it was picked up, +0x40 a
//     stopwatch between clicks (500 ms: a second click inside it counts as a
//     double click), +0x58 the cursor to go back to, +0x5C the figure under
//     the cursor (highlighted, counted twice), +0x60 a string, +0x74 a byte
//     set by Init, +0x78 the occupant filter of its picks (0xC bytes, vtables
//     0x00A8596C and 0x00A85954), +0x7C the MySim (the object's start,
//     counted), +0x80 the figure's position and +0x8C its state when it was
//     picked up, put back when the tool goes away.
//   The move-in tool (0x4C bytes, vtable 0x00A85A20; cSC4MySim::MoveIn makes
//     it): the player chooses a house. +0x28 the MySim (cISC4MySim), +0x2C
//     the house under the cursor and +0x30 its highlight before, +0x34 the
//     MySim simulator, +0x38 the info tip, +0x3C the cursor picture shown
//     over a house and +0x40 the one shown elsewhere (windows), +0x44 and
//     +0x48 the pictures' height and half width.
//   The base class (the game's, 0x005FB320): +4 inited, +8 the reference
//     count, +0xC the ID, +0x10 the cursor's ID, +0x14 the cursor, +0x18 a
//     window, +0x1C the city view (cISC4View3DWin), +0x20 the window manager.
//
// Replacement, off by default:
//
//   ui.mysim_tools.part2: the 27 functions below.
//
// Still the game's: 0x0043AB70, 0x0043ABA0, 0x0043AEF0, 0x0043AF10,
// 0x0043AF20, 0x0043AF80 (generic string and list code the whole game
// calls) and 0x0043B180 (destroys a range of import records; the code at
// 0x00776631 calls it too).
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace opensc4::ui {
namespace {

// Fields of the view input control base class.
namespace base {
constexpr std::size_t kInited = 0x4;
constexpr std::size_t kId = 0xC;
constexpr std::size_t kCursorId = 0x10;
constexpr std::size_t kView = 0x1C;
} // namespace base

// Fields of the dispatch tool.
namespace dispatch {
constexpr std::size_t kTarget = 0x28;     // the message target part
constexpr std::size_t kAllowed = 0x30;
constexpr std::size_t kGrabX = 0x34;
constexpr std::size_t kGrabZ = 0x38;
constexpr std::size_t kClickWatch = 0x40;
constexpr std::size_t kIdleCursor = 0x58;
constexpr std::size_t kHighlighted = 0x5C;
constexpr std::size_t kText = 0x60;
constexpr std::size_t kActive = 0x74;
constexpr std::size_t kFilter = 0x78;
constexpr std::size_t kMySim = 0x7C;
constexpr std::size_t kStartPosition = 0x80;
constexpr std::size_t kStartState = 0x8C;
} // namespace dispatch

// Fields of the move-in tool.
namespace movein {
constexpr std::size_t kMySim = 0x28;
constexpr std::size_t kHouse = 0x2C;
constexpr std::size_t kHouseHighlight = 0x30;
constexpr std::size_t kSimulator = 0x34;
constexpr std::size_t kInfoTip = 0x38;
constexpr std::size_t kHouseCursor = 0x3C;
constexpr std::size_t kOtherCursor = 0x40;
constexpr std::size_t kCursorHeight = 0x44;
constexpr std::size_t kCursorHalfWidth = 0x48;
} // namespace movein

// Slots of a view input control.
namespace control {
constexpr std::size_t kInit = 0xC;
constexpr std::size_t kSetCursor = 0x20; // by ID
constexpr std::size_t kEndInput = 0x6C;
constexpr std::size_t kSlot28 = 0x70; // the base class's: (0) does nothing
} // namespace control

constexpr std::size_t kMySimPart = 0xB8; // cISC4MySim inside the MySim object

struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};
static_assert(sizeof(GameString) == 0x14);

// The game's image holder (SC4ImageLoader, 0x30 bytes); the image at +4.
struct ImageLoader {
    alignas(4) char bytes[0x30];
};

// The game's file object (0x80 bytes).
struct GameFile {
    alignas(4) char bytes[0x80];
};

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

uint32_t AddRef(void* object)
{
    return Virtual<uint32_t>(object, 0x4);
}

uint32_t Release(void* object)
{
    return Virtual<uint32_t>(object, 0x8);
}

void Free(void* memory)
{
    Cdecl<void>(addr::SC4_Free, memory);
}

// An empty string with the game's 8-byte first buffer.
void EmptyString(GameString& text)
{
    text.begin = nullptr;
    text.end = nullptr;
    text.capacity = nullptr;
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, std::size_t{8});
    text.begin = buffer;
    text.end = buffer;
    text.capacity = buffer + 8;
    *buffer = 0;
    text.vtable = addr::SC4String_Vtable;
    text.zero = 0;
}

void DestroyString(GameString& text)
{
    text.vtable = addr::SC4String_Vtable;
    if (text.begin)
        Free(text.begin);
}

// Puts `value` in a counted reference as the game's inlined assignments do.
void AssignRef(void*& slot, void* value)
{
    void* const old = slot;
    if (value != old) {
        if (value)
            AddRef(value);
        slot = value;
        if (old)
            Release(old);
    }
}

// Empties a counted reference.
void ResetRef(void*& slot)
{
    if (void* const old = slot) {
        slot = nullptr;
        Release(old);
    }
}

// The four bytes of a float as the game pushes them.
uint32_t Bits(float& value)
{
    return At<uint32_t>(&value, 0);
}

// --- The dispatch tool ------------------------------------------------------------------

// The game's 0x00439380. OnKeyDown (slot 14): the base answer is asked and
// dropped; never handled.
uint8_t __fastcall DispatchOnKeyDown(char* self, void*, int32_t key, uint32_t modifiers)
{
    This<uint8_t>(addr::SC4ViewInputControl_OnKeyDefault, self, key, modifiers);
    return 0;
}

// The game's 0x004393A0. The pick filter's slot 3: an occupant of type
// 0xA9B6D7DE whose 0xC9B6D7D6 interface (a figure in the city) is of a
// kind (slot 7 not 0), not picked up (slot 16) and belongs to a MySim.
uint8_t __fastcall FilterIncludes(char*, void*, void* occupant)
{
    uint8_t included = 0;
    if (Virtual<uint32_t>(occupant, 0x1C) != 0xA9B6D7DE)
        return 0;
    void* figure = nullptr;
    if (Virtual<uint8_t>(occupant, 0x0, 0xC9B6D7D6u, &figure) && Virtual<uint32_t>(figure, 0x1C) != 0 &&
        !Virtual<uint8_t>(figure, 0x40)) {
        void* mysim = nullptr;
        if (void* const agent = Virtual<void*>(figure, 0x50)) {
            if (Virtual<uint8_t>(agent, 0x0, 0x0A1DA42Fu, This<void**>(addr::SC4RefPtr_Reset, &mysim)))
                included = 1;
        }
        if (mysim)
            Release(mysim);
    }
    if (figure)
        Release(figure);
    return included;
}

// The game's 0x00439450. Init (slot 3).
uint8_t __fastcall DispatchInit(char* self, void*)
{
    if (At<uint8_t>(self, base::kInited))
        return 1;
    At<uint32_t>(self, base::kId) = 0xEBAB04A9;
    At<uint32_t>(self, dispatch::kIdleCursor) = 0x2BAC4970;
    At<uint32_t>(self, base::kCursorId) = 0x2BAC4970;
    This<void>(addr::SC4Stopwatch_ConstructMode, self + dispatch::kClickWatch, uint32_t{4});
    This<uint8_t>(addr::SC4ViewInputControl_Init, self);
    if (void* const server = Global<void*>(addr::SC4_MessageServer)) {
        void* const target = self + dispatch::kTarget;
        Virtual<uint8_t>(server, 0x14, target, 0x09C8B8EAu);
        Virtual<uint8_t>(server, 0x14, target, 0xE94671EBu);
        Virtual<uint8_t>(server, 0x14, target, 0xE94671ECu);
        Virtual<uint8_t>(server, 0x14, target, 0xE94671EDu);
        Virtual<uint8_t>(server, 0x14, target, 0x894671F6u);
        Virtual<uint8_t>(server, 0x14, target, 0xAA25827Cu);
    }
    At<uint8_t>(self, dispatch::kActive) = 1;
    char* const filter = Cdecl<char*>(addr::SC4_operator_new, std::size_t{0xC});
    if (filter) {
        This<void*>(addr::SC4_OccupantFilterBase, filter);
        At<std::uintptr_t>(filter, 0x0) = addr::SC4UI_MySimDispatchFilterVtable;
        At<std::uintptr_t>(filter, 0x4) = addr::SC4UI_MySimDispatchFilterVtable4;
    }
    AssignRef(At<void*>(self, dispatch::kFilter), filter);
    return 1;
}

// The game's 0x00439660. Changes the figure shown as under the cursor: the
// old one's mark goes back to 1, the new one's becomes 3.
void __fastcall SetHighlighted(char* self, void*, void* figure)
{
    void*& current = At<void*>(self, dispatch::kHighlighted);
    if (figure == current)
        return;
    if (current) {
        Virtual<void>(current, 0x18, uint32_t{1});
        Release(current);
        ResetRef(current);
    }
    AssignRef(current, figure);
    if (current) {
        AddRef(current);
        Virtual<void>(current, 0x18, uint32_t{3});
    }
}

// Puts the MySim's figure back where it was picked up, when it is still
// held (the shared tail of Shutdown and the tool-changed message).
void* PutBack(char* self)
{
    void* const figure = Virtual<void*>(At<void*>(self, dispatch::kMySim), 0x34);
    if (figure && Virtual<uint8_t>(figure, 0x40)) {
        void* const occupant = Virtual<void*>(figure, 0xC);
        Virtual<void>(occupant, 0x24, self + dispatch::kStartPosition);
        Virtual<void>(figure, 0x2C, At<uint32_t>(self, dispatch::kStartState));
    }
    return figure;
}

// The game's 0x00439540. Shutdown (slot 4).
uint8_t __fastcall DispatchShutdown(char* self, void*)
{
    if (!At<uint8_t>(self, base::kInited))
        return 1;
    if (void* const view = At<void*>(self, base::kView))
        Virtual<void>(view, 0xA4, 0x49D02C4Du); // ClearCursorText
    if (void* const server = Global<void*>(addr::SC4_MessageServer)) {
        void* const target = self + dispatch::kTarget;
        Virtual<uint8_t>(server, 0x18, target, 0xAA25827Cu);
        Virtual<uint8_t>(server, 0x18, target, 0x09C8B8EAu);
        Virtual<uint8_t>(server, 0x18, target, 0xE94671EBu);
        Virtual<uint8_t>(server, 0x18, target, 0xE94671ECu);
        Virtual<uint8_t>(server, 0x18, target, 0xE94671EDu);
        Virtual<uint8_t>(server, 0x18, target, 0x894671F6u);
    }
    void*& highlighted = At<void*>(self, dispatch::kHighlighted);
    if (highlighted)
        Virtual<void>(highlighted, 0x18, uint32_t{1});
    ResetRef(highlighted);
    void* const figure = PutBack(self);
    Virtual<void>(At<void*>(self, dispatch::kMySim), 0x28);
    if (figure)
        Virtual<void>(figure, 0x3C, uint32_t{0});
    ResetRef(At<void*>(self, dispatch::kFilter));
    This<uint8_t>(addr::SC4ViewInputControl_Shutdown, self);
    return 1;
}

// The game's 0x004396E0. DoMessage of the message target at +0x28.
uint8_t __fastcall DispatchDoMessage(char* target, void*, void* message)
{
    char* const self = target - dispatch::kTarget;
    const uint32_t id = Virtual<uint32_t>(message, 0x10);
    switch (id) {
    case 0x09C8B8EA: { // the view's tool changed
        void* const first = Virtual<void*>(message, 0x28);
        void* const second = Virtual<void*>(message, 0x3C);
        if (first == self) {
            if (second == self)
                return 1;
            if (second && Virtual<uint32_t>(second, 0x14) == 0x48E2A1F8)
                return 1;
            void* const view = At<void*>(self, base::kView);
            At<uint8_t>(self, dispatch::kActive) = 0;
            if (view)
                Virtual<void>(view, 0xA4, 0x49D02C4Du);
            SetHighlighted(self, nullptr, nullptr);
            PutBack(self);
            return 1;
        }
        if (second == self)
            At<uint8_t>(self, dispatch::kActive) = 1;
        return 1;
    }
    case 0x894671F6:
        return 1;
    case 0x99EF1143: { // an occupant goes: the highlighted figure's is dropped
        void* const occupant = Virtual<void*>(message, 0x28);
        void*& highlighted = At<void*>(self, dispatch::kHighlighted);
        if (occupant == Virtual<void*>(highlighted, 0xC))
            ResetRef(highlighted);
        return 1;
    }
    case 0xAA25827C: {
        char* const mysim = At<char*>(self, dispatch::kMySim);
        if (mysim && !Virtual<uint8_t>(mysim + kMySimPart, 0x50)) // IsActive
            Virtual<void>(Global<void*>(addr::SC4_Service43DC4), 0x6C, uint32_t{0});
        return 0;
    }
    default:
        break;
    }
    if (id > 0x99EF1143 && id > 0xE94671EA && id <= 0xE94671ED)
        return 1;
    return 0;
}

// The game's 0x00439840. Takes another MySim (the old one's slot 10 is told
// first).
uint8_t __fastcall SetMySim(char* self, void*, void* mysim)
{
    void*& current = At<void*>(self, dispatch::kMySim);
    if (mysim == current)
        return 1;
    Virtual<void>(current, 0x28);
    AssignRef(current, mysim);
    return 1;
}

// The game's 0x00439890. The MySim figure under a screen point, counted;
// null and false when there is none.
uint8_t __fastcall PickFigure(char* self, void*, int32_t x, int32_t z, void** out)
{
    void* const picker = Global<void*>(addr::SC4_Lighting43DD0);
    *out = nullptr;
    void* const filter = At<void*>(self, dispatch::kFilter);
    void* hit = nullptr;
    uint8_t found = 0;
    if (Virtual<uint8_t>(picker, 0x104, x, z, filter, &hit)) {
        void* const occupant = Virtual<void*>(hit, 0xC);
        void* figure = nullptr;
        if (occupant) {
            if (Virtual<uint8_t>(occupant, 0x0, 0xC9B6D7D6u, &figure)) {
                void* mysim = nullptr;
                if (void* const agent = Virtual<void*>(figure, 0x50)) {
                    ResetRef(mysim);
                    if (Virtual<uint8_t>(agent, 0x0, 0x0A1DA42Fu, &mysim)) {
                        *out = figure;
                        AddRef(figure);
                        found = 1;
                    }
                }
                if (mysim)
                    Release(mysim);
            }
            if (figure)
                Release(figure);
        }
    }
    if (hit)
        Release(hit);
    return found;
}

// The terrain point under a screen point, with the terrain's height.
bool PickGround(char* self, int32_t x, int32_t z, float* position)
{
    if (!Virtual<uint8_t>(At<void*>(self, base::kView), 0x90, x, z, position, uint32_t{0})) // PickTerrain
        return false;
    void* const terrain = Virtual<void*>(Global<void*>(addr::SC4_City), 0x150);
    position[1] = static_cast<float>(Virtual<double>(terrain, 0x34, Bits(position[0]), Bits(position[2])));
    return true;
}

// The game's 0x00439980. OnMouseDownL (slot 16): with the figure held it is
// put down (or, on a double click, message 0x8BBBD22A is posted); a click
// on a MySim's figure picks it up, a double click selects it; with no
// figure in the city the MySim's slot 9 makes one at the clicked place.
uint8_t __fastcall DispatchOnMouseDownL(char* self, void*, int32_t x, int32_t z, uint32_t)
{
    uint8_t handled = 0;
    if (At<void*>(self, base::kView)) {
        void* figure = Virtual<void*>(At<void*>(self, dispatch::kMySim), 0x34);
        if (figure)
            AddRef(figure);
        void* picked = nullptr;
        uint8_t second = 0;
        char* const watch = self + dispatch::kClickWatch;
        if (This<uint8_t>(addr::GZStopwatch_IsRunning, watch) &&
            This<uint32_t>(addr::GZStopwatch_ElapsedMilliseconds, watch) < 500) {
            second = 1;
            This<void>(addr::SC4Stopwatch_Reset, watch);
        } else {
            This<void>(addr::GZStopwatch_Start, watch);
        }

        bool pickAgain = false;
        if (!figure) {
            if (PickFigure(self, nullptr, x, z, &picked)) {
                pickAgain = picked == nullptr;
            } else {
                This<void*>(addr::SC4RefPtr_Assign, &figure, Virtual<void*>(At<void*>(self, dispatch::kMySim), 0x24));
                float position[3];
                if (PickGround(self, x, z, position) &&
                    Virtual<uint8_t>(At<void*>(self, dispatch::kMySim), 0x3C, Bits(position[0]), Bits(position[2]))) {
                    Virtual<void>(Virtual<void*>(figure, 0xC), 0x24, position);
                    Virtual<void>(At<void*>(self, dispatch::kMySim), 0x2C, uint32_t{1});
                }
                handled = 1;
            }
        } else if (Virtual<uint8_t>(figure, 0x40)) {
            if (!second) {
                float position[3];
                if (PickGround(self, x + At<int32_t>(self, dispatch::kGrabX), z + At<int32_t>(self, dispatch::kGrabZ),
                               position))
                    Virtual<void>(Virtual<void*>(figure, 0xC), 0x24, position);
            } else {
                char* const mysim = At<char*>(self, dispatch::kMySim);
                Cdecl<void>(addr::SC4_PostMessage, 0x8BBBD22Au, uint32_t{1}, uint32_t{0}, uint32_t{0}, uint32_t{0},
                            mysim ? mysim + kMySimPart : nullptr);
            }
            Virtual<void>(figure, 0x3C, uint32_t{0});
            Virtual<void>(At<void*>(self, dispatch::kMySim), 0x2C, uint32_t{1});
            handled = 1;
        } else {
            pickAgain = true;
        }
        if (pickAgain)
            PickFigure(self, nullptr, x, z, &picked);

        if (picked && !handled) {
            void* mysimPart = nullptr;
            void* const agent = Virtual<void*>(picked, 0x50);
            ResetRef(mysimPart);
            Virtual<uint8_t>(agent, 0x0, 0x0A1DA42Fu, &mysimPart);
            if (SetMySim(self, nullptr, mysimPart ? static_cast<char*>(mysimPart) - kMySimPart : nullptr)) {
                Virtual<void>(At<void*>(self, dispatch::kMySim), 0x2C, uint32_t{second});
                Virtual<void>(picked, 0x3C, uint32_t{second == 0});
                char* const mysim = At<char*>(self, dispatch::kMySim);
                Cdecl<void>(addr::SC4_PostMessage, 0x8BBBD22Au, uint32_t{second}, uint32_t{0}, uint32_t{0},
                            uint32_t{0}, mysim ? mysim + kMySimPart : nullptr);
                if (!second) {
                    This<void>(addr::SC4ViewInputControl_SetGrabOffset, self, picked, x, z);
                    Virtual<void>(Virtual<void*>(picked, 0xC), 0x20, self + dispatch::kStartPosition);
                    At<uint32_t>(self, dispatch::kStartState) = Virtual<uint32_t>(picked, 0x30);
                }
            }
            handled = 1;
            if (mysimPart)
                Release(mysimPart);
        }
        if (picked)
            Release(picked);
        if (figure)
            Release(figure);
    }
    Virtual<void>(self, control::kSlot28, uint32_t{0});
    return handled;
}

// The game's 0x00439C90. OnMouseMove (slot 20): the held figure follows the
// cursor when the MySim's slot 15 allows the place (state 3, else 1), not
// within 500 ms of a click; otherwise the figure under the cursor is
// highlighted.
uint8_t __fastcall DispatchOnMouseMove(char* self, void*, int32_t x, int32_t z, uint32_t)
{
    if (!At<void*>(self, base::kView))
        return 1;
    void* const figure = Virtual<void*>(At<void*>(self, dispatch::kMySim), 0x34);
    if (figure)
        AddRef(figure);
    void* picked = nullptr;
    if (!figure)
        return 1;
    if (Virtual<uint8_t>(figure, 0x40)) {
        Virtual<void>(self, control::kSetCursor, 0x13B138D0u);
        char* const watch = self + dispatch::kClickWatch;
        bool wait = false;
        if (This<uint8_t>(addr::GZStopwatch_IsRunning, watch)) {
            if (This<uint32_t>(addr::GZStopwatch_ElapsedMilliseconds, watch) < 500)
                wait = true;
            else
                This<void>(addr::SC4Stopwatch_Reset, watch);
        }
        float position[3];
        if (!wait && PickGround(self, x + At<int32_t>(self, dispatch::kGrabX),
                                z + At<int32_t>(self, dispatch::kGrabZ), position)) {
            const uint8_t allowed =
                Virtual<uint8_t>(At<void*>(self, dispatch::kMySim), 0x3C, Bits(position[0]), Bits(position[2]));
            At<uint8_t>(self, dispatch::kAllowed) = allowed;
            if (allowed) {
                Virtual<void>(Virtual<void*>(figure, 0xC), 0x24, position);
                Virtual<void>(figure, 0x2C, uint32_t{3});
            } else {
                Virtual<void>(figure, 0x2C, uint32_t{1});
            }
        }
        Release(figure);
        return 1;
    }
    if (PickFigure(self, nullptr, x, z, &picked)) {
        if (Virtual<uint32_t>(picked, 0x1C) != 0) {
            SetHighlighted(self, nullptr, picked);
            Virtual<void>(self, control::kSetCursor, 0x13B138D0u);
        } else {
            SetHighlighted(self, nullptr, nullptr);
            Virtual<void>(self, control::kSetCursor, At<uint32_t>(self, dispatch::kIdleCursor));
        }
    } else {
        Virtual<void>(self, control::kSetCursor, At<uint32_t>(self, dispatch::kIdleCursor));
        SetHighlighted(self, nullptr, nullptr);
    }
    if (picked)
        Release(picked);
    Release(figure);
    return 1;
}

// The game's 0x00439E50. The dispatch tool's constructor.
void* __fastcall DispatchConstruct(char* self, void*, void* mysim)
{
    This<void*>(addr::SC4ViewInputControl_Construct, self);
    At<std::uintptr_t>(self, dispatch::kTarget) = addr::cSC4TrafficSimulator_VtableBase4;
    At<std::uintptr_t>(self, 0x0) = addr::SC4UI_MySimDispatchToolVtable;
    At<std::uintptr_t>(self, dispatch::kTarget) = addr::SC4UI_MySimDispatchToolMessageVtable;
    At<uint32_t>(self, 0x2C) = 4;
    At<uint8_t>(self, dispatch::kAllowed) = 0;
    At<int32_t>(self, dispatch::kGrabX) = 0;
    At<int32_t>(self, dispatch::kGrabZ) = 0;
    This<void*>(addr::SC4Stopwatch_Construct, self + dispatch::kClickWatch, uint32_t{4});
    At<void*>(self, dispatch::kHighlighted) = nullptr;
    At<char*>(self, dispatch::kText + 0x4) = nullptr;
    At<char*>(self, dispatch::kText + 0x8) = nullptr;
    At<char*>(self, dispatch::kText + 0xC) = nullptr;
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, std::size_t{8});
    At<char*>(self, dispatch::kText + 0x4) = buffer;
    At<char*>(self, dispatch::kText + 0x8) = buffer;
    At<char*>(self, dispatch::kText + 0xC) = buffer + 8;
    *buffer = 0;
    At<std::uintptr_t>(self, dispatch::kText) = addr::SC4String_Vtable;
    At<uint32_t>(self, dispatch::kText + 0x10) = 0;
    At<uint8_t>(self, dispatch::kActive) = 0;
    At<void*>(self, dispatch::kFilter) = nullptr;
    At<void*>(self, dispatch::kMySim) = mysim;
    if (mysim)
        AddRef(mysim);
    At<uint32_t>(self, dispatch::kStartPosition) = Global<uint32_t>(addr::SC4UI_DispatchToolZero);
    At<uint32_t>(self, dispatch::kStartPosition + 0x4) = Global<uint32_t>(addr::SC4UI_DispatchToolZero);
    At<uint32_t>(self, dispatch::kStartPosition + 0x8) = Global<uint32_t>(addr::SC4UI_DispatchToolZero);
    At<uint32_t>(self, dispatch::kStartState) = 0;
    return self;
}

// The game's 0x00439F00. The dispatch tool's destructor.
void __fastcall DispatchDestruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4UI_MySimDispatchToolVtable;
    At<std::uintptr_t>(self, dispatch::kTarget) = addr::SC4UI_MySimDispatchToolMessageVtable;
    if (void* const mysim = At<void*>(self, dispatch::kMySim))
        Release(mysim);
    if (void* const filter = At<void*>(self, dispatch::kFilter))
        Release(filter);
    At<std::uintptr_t>(self, dispatch::kText) = addr::SC4String_Vtable;
    if (char* const text = At<char*>(self, dispatch::kText + 0x4))
        Free(text);
    if (void* const highlighted = At<void*>(self, dispatch::kHighlighted))
        Release(highlighted);
    This<void>(addr::SC4Stopwatch_Destruct, self + dispatch::kClickWatch);
    This<void>(addr::SC4ViewInputControl_Destruct, self);
}

// The game's 0x00439F60. The dispatch tool's deleting destructor (slot 26).
void* __fastcall DispatchDelete(char* self, void*, uint32_t flags)
{
    DispatchDestruct(self, nullptr);
    if (static_cast<uint8_t>(flags) & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// --- Shared by the MySim tools ----------------------------------------------------------

// The game's 0x00439F80. Init (slot 3) of the home, job and move-in tools:
// cursor 0x43401C71, then the base class's Init.
uint8_t __fastcall ToolInit(char* self, void*)
{
    Virtual<void>(self, control::kSetCursor, 0x43401C71u);
    return This<uint8_t>(addr::SC4ViewInputControl_Init, self);
}

// The game's 0x00439FA0. OnKeyDown (slot 14) of the same tools and of the
// one with vtable 0x00A85F40: Escape ends the tool.
uint8_t __fastcall ToolOnKeyDown(char* self, void*, int32_t key, uint32_t)
{
    if (key != 0x1B)
        return 0;
    Virtual<void>(self, control::kEndInput);
    return 1;
}

// --- The move-in tool -------------------------------------------------------------------

// The game's 0x00439FC0. Constructor.
void* __fastcall MoveInConstruct(char* self, void*)
{
    This<void*>(addr::SC4ViewInputControl_Construct, self);
    At<std::uintptr_t>(self, 0x0) = addr::SC4UI_MySimMoveInToolVtable;
    At<void*>(self, movein::kMySim) = nullptr;
    At<void*>(self, movein::kHouse) = nullptr;
    At<uint32_t>(self, movein::kHouseHighlight) = 0;
    At<void*>(self, movein::kSimulator) = nullptr;
    At<void*>(self, movein::kInfoTip) = nullptr;
    At<void*>(self, movein::kHouseCursor) = nullptr;
    At<void*>(self, movein::kOtherCursor) = nullptr;
    At<int32_t>(self, movein::kCursorHeight) = 0;
    At<int32_t>(self, movein::kCursorHalfWidth) = 0;
    return self;
}

// The game's 0x00439FF0. Destructor.
void __fastcall MoveInDestruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4UI_MySimMoveInToolVtable;
    if (void* const window = At<void*>(self, movein::kOtherCursor))
        Release(window);
    if (void* const window = At<void*>(self, movein::kHouseCursor))
        Release(window);
    if (void* const tip = At<void*>(self, movein::kInfoTip))
        Release(tip);
    if (void* const simulator = At<void*>(self, movein::kSimulator))
        Release(simulator);
    if (void* const house = At<void*>(self, movein::kHouse))
        Release(house);
    if (void* const mysim = At<void*>(self, movein::kMySim))
        Release(mysim);
    This<void>(addr::SC4ViewInputControl_Destruct, self);
}

// The game's 0x0043A050. OnMouseDownL (slot 16): while this is the view's
// tool, a click on the house under the cursor moves the MySim in (slot 13
// of cISC4MySim) and ends the tool.
uint8_t __fastcall MoveInOnMouseDownL(char* self, void*, int32_t, int32_t, uint32_t)
{
    if (Virtual<void*>(At<void*>(self, base::kView), 0x60) != self) // GetCurrentViewInputControl
        return 0;
    if (void* const house = At<void*>(self, movein::kHouse)) {
        Virtual<void>(At<void*>(self, movein::kMySim), 0x34, house);
        Virtual<void>(self, control::kEndInput);
    }
    return 1;
}

// The game's 0x0043A090. Deleting destructor (slot 26).
void* __fastcall MoveInDelete(char* self, void*, uint32_t flags)
{
    MoveInDestruct(self, nullptr);
    if (static_cast<uint8_t>(flags) & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// Takes the highlight off the house the tool marked and forgets it.
void DropHouse(char* self)
{
    void*& house = At<void*>(self, movein::kHouse);
    if (house) {
        void* const occupant = Virtual<void*>(house, 0xC);
        Virtual<void>(occupant, 0x44, At<uint32_t>(self, movein::kHouseHighlight), uint32_t{0}); // SetHighlight
        ResetRef(house);
    }
}

// The game's 0x0043A0B0. OnMouseMove (slot 20). Over a finished house with
// one family that nobody's MySim lives in, that the MySim may take and whose
// lot is in state 3: the house cursor, the house highlighted, and the info
// tip with the house's name and two graded figures of its cell (service
// 0x00B43DA4, by the lot's wealth). Elsewhere the other cursor.
uint8_t __fastcall MoveInOnMouseMove(char* self, void*, int32_t x, int32_t z, uint32_t)
{
    void* occupant = nullptr;
    void* building = nullptr;
    GameString name;
    EmptyString(name);

    bool house = false;
    uint32_t wealth = 0;
    do {
        if (!Virtual<uint8_t>(At<void*>(self, base::kView), 0x94, x, z, &occupant)) // PickOccupant
            break;
        if (Virtual<uint32_t>(occupant, 0x1C) != 0x278128A0)
            break;
        ResetRef(building);
        if (!Virtual<uint8_t>(occupant, 0x0, 0x87DDA3A9u, &building))
            break;
        void* const simulator = At<void*>(self, movein::kSimulator);
        if (!simulator)
            break;
        if (Virtual<void*>(simulator, 0x24, building)) // MySimLivesHere
            break;
        void* const completion = Virtual<void*>(building, 0x24);
        if (!This<uint32_t>(addr::SC4UI_FloatAtLeast, completion, &Global<float>(addr::SC4UI_MoveInToolMinimum)))
            break;
        if (*Virtual<int32_t*>(building, 0x2C) != 1)
            break;
        if (!Virtual<uint8_t>(At<void*>(self, movein::kMySim), 0x1C, building, uint32_t{1}, uint32_t{0}))
            break;
        void* const lots = Global<void*>(addr::SC4_LotManager);
        void* const asOccupant = Virtual<void*>(building, 0xC);
        void* const lot = Virtual<void*>(lots, 0x38, asOccupant);
        wealth = Virtual<uint32_t>(lot, 0xA4);
        Virtual<uint8_t>(building, 0x4C, &name);
        void* const again = Virtual<void*>(building, 0xC);
        void* const lot2 = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x38, again);
        if (!lot2 || Virtual<uint32_t>(lot2, 0x84) != 3)
            break;
        house = true;
    } while (false);

    if (house) {
        Virtual<void>(At<void*>(self, movein::kOtherCursor), 0x118);
        void* window = Virtual<void*>(At<void*>(self, movein::kHouseCursor), 0xC);
        const int32_t top = z - At<int32_t>(self, movein::kCursorHeight);
        const int32_t left = x - At<int32_t>(self, movein::kCursorHalfWidth);
        Virtual<void>(window, 0xE0, left, top);
        window = Virtual<void*>(At<void*>(self, movein::kHouseCursor), 0xC);
        Virtual<void>(window, 0x114);

        DropHouse(self);
        void*& marked = At<void*>(self, movein::kHouse);
        void* const old = marked;
        // The original keeps the fade below in the stack slot that held this
        // pointer (null by now).
        uint32_t fade = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(old));
        if (building != old) {
            if (building)
                AddRef(building);
            marked = building;
            if (old)
                Release(old);
        }
        void* asOccupant = Virtual<void*>(building, 0xC);
        At<uint32_t>(self, movein::kHouseHighlight) = Virtual<uint32_t>(asOccupant, 0x40);
        asOccupant = Virtual<void*>(building, 0xC);
        Virtual<void>(asOccupant, 0x44, uint32_t{1}, uint32_t{0});

        void*& tip = At<void*>(self, movein::kInfoTip);
        if (!tip) {
            void* const memory = Cdecl<void*>(addr::SC4_operator_new, std::size_t{0x1B4});
            void* const made = memory ? This<void*>(addr::SC4InfoTip_Construct, memory) : nullptr;
            This<void*>(addr::SC4RefPtr_Assign, &tip, made);
            Virtual<void>(tip, 0x2C, 0xA85F1A83u);
            Virtual<void>(tip, 0x28, 0x0A52F5C6u);
            Virtual<void>(tip, 0x30, uint32_t{0});
            uint32_t key[3] = {0x856DDBAC, 0x46A006B0, 0x14416208};
            ImageLoader frame;
            This<void*>(addr::SC4ImageLoader_ConstructKey, &frame, key, uint32_t{1}, uint32_t{0});
            Virtual<void>(tip, 0x10, At<void*>(&frame, 0x4), uint32_t{0xC}, uint32_t{0xA});
            void* const service = Global<void*>(addr::SC4_Service43DC8);
            void* const tipWindow = Virtual<void*>(tip, 0xC);
            Virtual<void>(service, 0x38, tipWindow);
            const int32_t half = At<int32_t>(self, movein::kCursorHalfWidth);
            int32_t area[4] = {-half, -1000, half, 0x1C};
            Virtual<void>(tip, 0x44, area);
            This<void>(addr::SC4ImageLoader_Destruct, &frame);
        }
        switch (wealth) {
        case 1:
            fade = 0x3EA8F5C3; // 0.33
            break;
        case 2:
            fade = 0x3F28F5C3; // 0.66
            break;
        case 3:
            fade = 0x3F800000;
            break;
        default:
            break;
        }
        uint32_t key[3] = {0x856DDBAC, 0x46A006B0, 0x1441621F};
        ImageLoader icon;
        This<void*>(addr::SC4ImageLoader_ConstructKey, &icon, key, uint32_t{1}, uint32_t{0});
        Virtual<void>(tip, 0x38, At<void*>(&icon, 0x4), uint32_t{3}, fade);
        Virtual<void>(tip, 0x18, &name);
        float position[3];
        Virtual<void>(Virtual<void*>(building, 0xC), 0x20, position);
        int32_t cellX;
        int32_t cellZ;
        Virtual<void>(Global<void*>(addr::SC4_City), 0x17C, Bits(position[0]), Bits(position[2]), &cellX, &cellZ);
        void* const grids = Global<void*>(addr::SC4_Service43DA4);
        float first = static_cast<float>(Virtual<double>(grids, 0x94, cellX, cellZ, wealth));
        float second = static_cast<float>(Virtual<double>(grids, 0x98, cellX, cellZ, wealth));
        GameString firstText;
        EmptyString(firstText);
        GameString secondText;
        EmptyString(secondText);
        GameString text;
        EmptyString(text);
        Cdecl<void>(addr::SC4UI_FirstGradeText, Bits(first), &firstText);
        Cdecl<void>(addr::SC4UI_SecondGradeText, Bits(second), &secondText);
        Cdecl<void>(addr::cRZBaseString_Sprintf, &text, "%s\n%s", firstText.begin, secondText.begin);
        Virtual<void>(tip, 0x20, &text);
        int32_t point[2] = {x + 1, z};
        Virtual<void>(tip, 0x3C, point);
        Virtual<void>(Virtual<void*>(tip, 0xC), 0x114);
        DestroyString(text);
        DestroyString(secondText);
        DestroyString(firstText);
        This<void>(addr::SC4ImageLoader_Destruct, &icon);
    } else {
        Virtual<void>(Virtual<void*>(At<void*>(self, movein::kHouseCursor), 0xC), 0x118);
        void* const other = At<void*>(self, movein::kOtherCursor);
        const int32_t top = z - At<int32_t>(self, movein::kCursorHeight);
        const int32_t left = x - At<int32_t>(self, movein::kCursorHalfWidth);
        Virtual<void>(other, 0xE0, left, top);
        Virtual<void>(At<void*>(self, movein::kOtherCursor), 0x114);
        if (void* const tip = At<void*>(self, movein::kInfoTip))
            Virtual<void>(Virtual<void*>(tip, 0xC), 0x118);
        DropHouse(self);
    }
    DestroyString(name);
    if (building)
        Release(building);
    if (occupant)
        Release(occupant);
    return 1;
}

// The game's 0x0043A730. Sets the tool up for a MySim (the object's start):
// the MySim simulator, the two cursor pictures (window 0x27DF05BE of UI
// resource 0x96A006B0/0x6A9455C9 and its sibling 0x27DF05BF) with the
// MySim's portrait in both, from its bitmap ID or else its picture file.
uint8_t __fastcall MoveInInit(char* self, void*, char* mysim)
{
    if (!Virtual<uint8_t>(self, control::kInit))
        return 0;
    AssignRef(At<void*>(self, movein::kMySim), mysim ? mysim + kMySimPart : nullptr);
    AssignRef(At<void*>(self, movein::kSimulator), Virtual<void*>(Global<void*>(addr::SC4_City), 0x11C));
    void* const manager = Cdecl<void*>(addr::SC4_GetWinManager);
    Virtual<void>(manager, 0x94, Virtual<void*>(At<void*>(self, base::kView), 0xC));

    void*& houseCursor = At<void*>(self, movein::kHouseCursor);
    if (houseCursor)
        return 1;
    uint32_t key[3] = {0, 0x96A006B0, 0x6A9455C9};
    ResetRef(houseCursor);
    void* const parent = Virtual<void*>(At<void*>(self, base::kView), 0xC);
    if (!Cdecl<uint8_t>(addr::SC4UI_LoadWindow, key, parent, 0x27DF05BEu, 0x5386D516u, &houseCursor))
        return 1;
    At<int32_t>(self, movein::kCursorHeight) = Virtual<int32_t>(Virtual<void*>(houseCursor, 0xC), 0xA8);
    At<int32_t>(self, movein::kCursorHalfWidth) = Virtual<int32_t>(Virtual<void*>(houseCursor, 0xC), 0xA4) / 2;
    Virtual<void>(Virtual<void*>(houseCursor, 0xC), 0x110, 0x200000u, uint32_t{1});
    void* const container = Virtual<void*>(Virtual<void*>(houseCursor, 0xC), 0x2C);
    void*& otherCursor = At<void*>(self, movein::kOtherCursor);
    This<void*>(addr::SC4RefPtr_Assign, &otherCursor, Virtual<void*>(container, 0x8C, 0x27DF05BFu));
    Virtual<void>(otherCursor, 0x110, 0x200000u, uint32_t{1});

    char* const part = mysim + kMySimPart;
    const uint32_t bitmap = Virtual<uint32_t>(part, 0x58);
    ImageLoader portrait;
    This<void*>(addr::SC4ImageLoader_Construct, &portrait, uint32_t{0});
    if (bitmap) {
        This<void>(addr::SC4ImageLoader_SetKey, &portrait, uint32_t{0}, 0x46A006B0u, bitmap);
    } else {
        GameString file;
        This<void*>(addr::SC4String_Substring, &file, Virtual<void*>(part, 0x5C), uint32_t{0}, 0xFFFFFFFFu);
        GameString path;
        This<void*>(addr::SC4String_SC4String, &path);
        Virtual<void>(Global<void*>(addr::SC4_Service43C94), 0xF4, &path);
        char* const end = file.end;
        char* const begin = file.begin;
        uint32_t tag = reinterpret_cast<uint32_t>(mysim);
        This<void>(addr::SC4String_AppendRange, &path.begin, begin, end, &tag);
        This<void>(addr::SC4ImageLoader_SetName, &portrait, &path);
        This<void>(addr::SC4ImageLoader_SetMode, &portrait, uint32_t{8});
        DestroyString(path);
        if (begin)
            Free(begin);
    }
    if (This<uint8_t>(addr::SC4ImageLoader_Load, &portrait)) {
        void* picture = nullptr;
        void* window = Virtual<void*>(houseCursor, 0xC);
        ResetRef(picture);
        if (Virtual<uint8_t>(window, 0x94, 0xEA9457BAu, 0xC12CEA13u, &picture))
            Virtual<void>(picture, 0x10, At<void*>(&portrait, 0x4));
        window = otherCursor;
        ResetRef(picture);
        if (Virtual<uint8_t>(window, 0x94, 0xEA9457BBu, 0xC12CEA13u, &picture))
            Virtual<void>(picture, 0x10, At<void*>(&portrait, 0x4));
        if (picture)
            Release(picture);
    }
    This<void>(addr::SC4ImageLoader_Destruct, &portrait);
    return 1;
}

// --- Helpers of the MySim import --------------------------------------------------------

// The game's 0x0043AA30 (the source's name in EAX, the target's on the
// stack, caller cleans): copies a file without its last four bytes, each
// byte XORed with the coded byte before it (0xBF before the first).
[[maybe_unused]] uint8_t __cdecl DecodeFile(void* sourceName, void* targetName)
{
    GameFile source;
    This<void*>(addr::cRZFile_Construct, &source, sourceName);
    GameFile target;
    This<void*>(addr::cRZFile_Construct, &target, targetName);
    if (!This<uint8_t>(addr::cRZFile_Open, &source, uint32_t{1}, uint32_t{2}, uint32_t{1})) {
        This<void>(addr::cRZFile_Destruct, &target);
        This<void>(addr::cRZFile_Destruct, &source);
        return 0;
    }
    const uint32_t size = This<uint32_t>(addr::cRZFile_Length, &source) - 4;
    uint8_t* const data = Cdecl<uint8_t*>(addr::SC4_operator_new, std::size_t{size});
    uint8_t ok = This<uint8_t>(addr::cRZFile_Read, &source, data, size);
    This<uint8_t>(addr::cRZFile_Close, &source);
    if (!ok) {
        Cdecl<void>(addr::SC4_operator_delete, data);
        This<void>(addr::cRZFile_Destruct, &target);
        This<void>(addr::cRZFile_Destruct, &source);
        return 0;
    }
    uint8_t previous = 0xBF;
    for (uint32_t i = 0; i < size; ++i) {
        const uint8_t coded = data[i];
        data[i] = static_cast<uint8_t>(coded ^ previous);
        previous = coded;
    }
    if (This<uint8_t>(addr::cRZFile_Open, &target, uint32_t{2}, uint32_t{1}, uint32_t{1})) {
        ok = This<uint8_t>(addr::cRZFile_Write, &target, data, size);
        This<uint8_t>(addr::cRZFile_Close, &target);
    } else {
        ok = 0;
    }
    Cdecl<void>(addr::SC4_operator_delete, data);
    This<void>(addr::cRZFile_Destruct, &target);
    This<void>(addr::cRZFile_Destruct, &source);
    return ok;
}

__declspec(naked) void DecodeFileThunk()
{
    __asm push dword ptr [esp + 4]
    __asm push eax
    __asm call DecodeFile
    __asm add esp, 8
    __asm ret
}

// The game's 0x0043ABE0 (the image codec in EDI, the source in EDX, the
// target on the stack, caller cleans): loads a portrait, refuses one whose
// pixels are all within 10 of the first in red, green and blue, and saves
// its 36 by 41 part (from 5, 2) with the corners in magenta.
[[maybe_unused]] uint8_t __cdecl MaskPortrait(void* codec, void* sourceName, void* targetName)
{
    int32_t whole[4] = {0, 0, 0x24, 0x29};
    int32_t part[4] = {5, 2, 0x29, 0x2B};
    void* loaded = nullptr;
    Virtual<void>(codec, 0x10, sourceName, &loaded, uint32_t{9});
    void* made = nullptr;
    Virtual<void>(Global<void*>(addr::SC4_Service43C9C), 0xC, &made);
    if (!loaded) {
        if (made)
            Release(made);
        return 0;
    }
    if (!made) {
        Release(loaded);
        return 0;
    }
    const int32_t height = whole[3] - whole[1];
    const int32_t width = whole[2] - whole[0];
    int32_t colorType[2];
    const int32_t* const type = Virtual<int32_t*>(loaded, 0x3C, colorType);
    const int32_t depth = type[1];
    const int32_t kind = type[0];
    if (!Virtual<uint8_t>(made, 0xC, width, height, kind, depth)) {
        if (made)
            Release(made);
        if (loaded)
            Release(loaded);
        return 0;
    }

    uint8_t uniform = 1;
    uint8_t firstRed;
    uint8_t firstGreen;
    uint8_t firstBlue;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    const uint32_t corner = Virtual<uint32_t>(loaded, 0x54, uint32_t{0}, uint32_t{0});
    Virtual<void>(loaded, 0x7C, corner, &firstRed, &firstGreen, &firstBlue);
    uint32_t column = 0;
    if (Virtual<uint32_t>(loaded, 0x24) > 0) {
        for (;;) {
            uint32_t row = 0;
            if (Virtual<uint32_t>(loaded, 0x28) > 0) {
                for (;;) {
                    const uint32_t pixel = Virtual<uint32_t>(loaded, 0x54, column, row);
                    Virtual<void>(loaded, 0x7C, pixel, &red, &green, &blue);
                    int32_t difference = static_cast<int32_t>(red) - static_cast<int32_t>(firstRed);
                    if ((difference < 0 ? -difference : difference) > 10) {
                        uniform = 0;
                        break;
                    }
                    difference = static_cast<int32_t>(green) - static_cast<int32_t>(firstGreen);
                    if ((difference < 0 ? -difference : difference) > 10) {
                        uniform = 0;
                        break;
                    }
                    difference = static_cast<int32_t>(blue) - static_cast<int32_t>(firstBlue);
                    if ((difference < 0 ? -difference : difference) > 10) {
                        uniform = 0;
                        break;
                    }
                    ++row;
                    if (!(row < Virtual<uint32_t>(loaded, 0x28)))
                        break;
                }
            }
            if (row < Virtual<uint32_t>(loaded, 0x28))
                break;
            ++column;
            if (!(column < Virtual<uint32_t>(loaded, 0x24)))
                break;
        }
    }
    if (uniform) {
        if (made)
            Release(made);
        if (loaded)
            Release(loaded);
        return 0;
    }

    Virtual<void>(made, 0x74, loaded, part, whole, uint32_t{0});
    const uint32_t color = Virtual<uint32_t>(made, 0x78, uint32_t{0xFF}, uint32_t{0}, uint32_t{0xFF});
    static const uint32_t corners[12][2] = {
        {0, 0}, {1, 0}, {0, 1}, {0x22, 0}, {0x23, 0}, {0x23, 1},
        {0, 0x27}, {0, 0x28}, {1, 0x28}, {0x23, 0x27}, {0x23, 0x28}, {0x22, 0x28},
    };
    for (const auto& point : corners)
        Virtual<void>(made, 0x58, point[0], point[1], color);
    const uint8_t saved = Virtual<uint8_t>(codec, 0x24, targetName, made, whole, uint32_t{0x64});
    if (made)
        Release(made);
    if (loaded)
        Release(loaded);
    return saved;
}

__declspec(naked) void MaskPortraitThunk()
{
    __asm push dword ptr [esp + 4]
    __asm push edx
    __asm push edi
    __asm call MaskPortrait
    __asm add esp, 12
    __asm ret
}

// The game's 0x0043AF40. Destructor of a 0x30-byte import record: the
// strings at +0x18 and +0x4.
void __fastcall RecordDestruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0x18) = addr::SC4String_Vtable;
    if (char* const text = At<char*>(self, 0x1C))
        Free(text);
    At<std::uintptr_t>(self, 0x4) = addr::SC4String_Vtable;
    if (char* const text = At<char*>(self, 0x8))
        Free(text);
}

// The game's 0x0043AFF0. The number (0 to 11) of the zodiac sign whose
// localized name (group 0x6A231EAA, instances at 0x00A85AA4), in lower
// case, is `name`; 0 with a message built and dropped when there is none.
uint8_t __cdecl ZodiacFromName(char* name)
{
    for (int32_t index = 0; index < 12; ++index) {
        struct {
            alignas(4) char bytes[0x20];
        } sign;
        This<void*>(addr::SC4LocalizedString_SC4LocalizedString, &sign, 0x6A231EAAu,
                    Global<uint32_t>(addr::SC4UI_ZodiacNames + 4 * static_cast<std::uintptr_t>(index)), uint32_t{1});
        This<void>(addr::cRZBaseString_MakeLower, &sign);
        char* const body = name ? name + 4 : nullptr;
        const char* const wanted = At<const char*>(body, 0x0);
        const std::size_t wantedSize = static_cast<std::size_t>(At<const char*>(body, 0x4) - wanted);
        char* const text = At<char*>(&sign, 0x4);
        const std::size_t size = static_cast<std::size_t>(At<char*>(&sign, 0x8) - text);
        bool same = wantedSize == size;
        for (std::size_t i = 0; same && i < size; ++i)
            same = wanted[i] == text[i];
        At<std::uintptr_t>(&sign, 0x0) = addr::SC4String_Vtable;
        if (text)
            Free(text);
        if (same)
            return static_cast<uint8_t>(index);
    }
    GameString message;
    EmptyString(message);
    const char* const text = Virtual<const char*>(name, 0x14);
    Cdecl<void>(addr::cRZBaseString_Sprintf, &message, "Unknown zodiac symbol \"%s\" encountered during import!\n\n",
                text);
    DestroyString(message);
    return 0;
}

// The game's 0x0043B100. Copy constructor of an import record: a dword, two
// strings (+0x4, +0x18) and two bytes (+0x2C, +0x2D).
void* __fastcall RecordCopy(char* self, void*, char* source)
{
    At<uint32_t>(self, 0x0) = At<uint32_t>(source, 0x0);
    uint32_t tag = reinterpret_cast<uint32_t>(source);
    This<void*>(addr::SC4String_CopyBuffer, self + 0x8, source + 0x8, uint32_t{0}, 0xFFFFFFFFu, &tag);
    At<std::uintptr_t>(self, 0x4) = addr::SC4String_Vtable;
    At<uint32_t>(self, 0x14) = 0;
    This<void*>(addr::SC4String_CopyBuffer, self + 0x1C, source + 0x1C, uint32_t{0}, 0xFFFFFFFFu, &tag);
    At<std::uintptr_t>(self, 0x18) = addr::SC4String_Vtable;
    At<uint32_t>(self, 0x28) = 0;
    At<uint8_t>(self, 0x2C) = At<uint8_t>(source, 0x2C);
    At<uint8_t>(self, 0x2D) = At<uint8_t>(source, 0x2D);
    return self;
}

// The game's 0x0043B1D0. Copies a range of import records into raw memory;
// returns the end of the copies.
char* __cdecl RecordCopyRange(char* first, char* last, char* target)
{
    for (; first != last; first += 0x30, target += 0x30) {
        if (target)
            RecordCopy(target, nullptr, first);
    }
    return target;
}

bool InstallPart2(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[27] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F439380, reinterpret_cast<void*>(&DispatchOnKeyDown)},
        {addr::SC4UI_F4393A0, reinterpret_cast<void*>(&FilterIncludes)},
        {addr::SC4UI_F439450, reinterpret_cast<void*>(&DispatchInit)},
        {addr::SC4UI_F439540, reinterpret_cast<void*>(&DispatchShutdown)},
        {addr::SC4UI_F439660, reinterpret_cast<void*>(&SetHighlighted)},
        {addr::SC4UI_F4396E0, reinterpret_cast<void*>(&DispatchDoMessage)},
        {addr::SC4UI_F439840, reinterpret_cast<void*>(&SetMySim)},
        {addr::SC4UI_F439890, reinterpret_cast<void*>(&PickFigure)},
        {addr::SC4UI_F439980, reinterpret_cast<void*>(&DispatchOnMouseDownL)},
        {addr::SC4UI_F439C90, reinterpret_cast<void*>(&DispatchOnMouseMove)},
        {addr::SC4MySimHelper90_Construct, reinterpret_cast<void*>(&DispatchConstruct)},
        {addr::SC4UI_F439F00, reinterpret_cast<void*>(&DispatchDestruct)},
        {addr::SC4UI_F439F60, reinterpret_cast<void*>(&DispatchDelete)},
        {addr::SC4UI_F439F80, reinterpret_cast<void*>(&ToolInit)},
        {addr::SC4UI_F439FA0, reinterpret_cast<void*>(&ToolOnKeyDown)},
        {addr::SC4MySimMoveInTool_Construct, reinterpret_cast<void*>(&MoveInConstruct)},
        {addr::SC4UI_F439FF0, reinterpret_cast<void*>(&MoveInDestruct)},
        {addr::SC4UI_F43A050, reinterpret_cast<void*>(&MoveInOnMouseDownL)},
        {addr::SC4UI_F43A090, reinterpret_cast<void*>(&MoveInDelete)},
        {addr::SC4UI_F43A0B0, reinterpret_cast<void*>(&MoveInOnMouseMove)},
        {addr::SC4MySimMoveInTool_Init, reinterpret_cast<void*>(&MoveInInit)},
        {addr::SC4UI_F43AA30, reinterpret_cast<void*>(&DecodeFileThunk)},
        {addr::SC4UI_F43ABE0, reinterpret_cast<void*>(&MaskPortraitThunk)},
        {addr::SC4UI_F43AF40, reinterpret_cast<void*>(&RecordDestruct)},
        {addr::SC4UI_F43AFF0, reinterpret_cast<void*>(&ZodiacFromName)},
        {addr::SC4UI_F43B100, reinterpret_cast<void*>(&RecordCopy)},
        {addr::SC4UI_F43B1D0, reinterpret_cast<void*>(&RecordCopyRange)},
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

const bool g_part2Registered = registry::Register({
    "ui.mysim_tools.part2",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F439980,
    "Our MySim dispatch tool and move-in tool (the view input controls with which the player moves the MySim's "
    "figure and chooses its house) and helpers of the MySim import (docs/ui.md)",
    false,
    InstallPart2,
});

} // namespace
} // namespace opensc4::ui
