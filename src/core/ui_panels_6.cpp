// The city view's pop-up menus and the end of the info tip (0x00799DD0 to
// 0x0079BBE0): four small window classes and three functions of a fifth.
//
// The info tip (0x1B4 bytes, vtables 0x00AB69D0 and 0x00AB6770 at +4, no
// class ID) is the small text window shown next to the cursor or next to a
// menu row. Its methods lie before this range; here are its constructor
// (0x00799DD0), its destructors and the two thunks of its window part.
//
// The menu (0x140 bytes, vtables 0x00AB6D28 and 0x00AB6D88 at +4, interface
// 0x49D98264, no class ID) is a column of rows drawn into a window: the
// city view makes one for each of its pop-up menus (0x007EDD27, 0x007F06F7;
// 0x0078F100 makes one for the interface's own list), fills it with items
// and puts it into a frame with scroll arrows. It keeps the items in a
// vector, the first row shown, the row under the mouse and the chosen row.
// A press on an enabled row sends the window message 3 with 0x49D993D4,
// the item's value and its ID; the mouse over a row puts the row's label,
// with " - " and its price when it has one, into the menu's info tip.
//
// A menu item (0x44 bytes, vtable 0x00AB6FE4, interface 0x49D98263) is a
// counted record: two strings (the label and the tip's second text), a
// price, a picture, a value, an ID and a byte "enabled".
//
// The menu's arrows (0x150 bytes, vtables 0x00AB6D04 and 0x00AB6AA8 at +4,
// interface 0x8A25CE2C, no class ID; made at 0x007E7474 and 0x007EB0FB)
// are the menu's frame: a strip at the right edge with an arrow above and
// one below a pointer. The menu, its child, tells it which arrows can be
// used (slot 7); a press on an arrow sends the step message 0xCA35E4BA
// with -1 or 1, at once and then on a timer (300 ms, then every 100 ms,
// from the two numbers at 0x00B0DB30), and the menu moves its first row.
//
// The last three functions (0x0079BAB0, 0x0079BAF0, 0x0079BB70) belong to
// the control that starts at 0x0079BBE0 (vtables 0x00AB7078, 0x00AB7020 at
// +0xD8, 0x00AB7010 at +0x10C): its QueryInterface, its Shutdown and the
// helper that plays one of its two sounds.
//
// Replacement, off by default:
//
//   ui.panels.part6: everything named above.
//
// Left the game's: the two paint handlers (the menu's 0x0079AA70, the
// arrows' 0x0079B0E0: they blit rows and picture pieces), 0x0079B580 (the
// menu's item count, code shared with a vtable at 0x00A9E610) and
// 0x0079B730 (the item's "enabled" getter, four bytes). Not functions:
// 0x0079A060, 0x0079A840, 0x0079AB40 and 0x0079BB60 are labels inside
// 0x0079A040, 0x0079A820, 0x0079AA70 and 0x0079BAF0.
//
// The menu: +0 vtable, +4 the window (0xD8 bytes), +0xDC the items (begin,
// end, capacity), +0xE8 their count, +0xEC the first row shown, +0xF0 the
// row under the mouse (-1), +0xF4 the chosen row (-1), +0xF8 the rows'
// width, +0xFC their height, +0x100 the gap between them (the constructor
// leaves these three unset), +0x104 the font, +0x108 the info tip, +0x10C
// a reference nothing here sets, +0x110 a timer (0x28 bytes), +0x138 a
// countdown of timer ticks, +0x13C the row the timer was started for.
//
// The arrows: +0 vtable, +4 the window, +0xDC the picture, +0xE0 the
// painted strip, +0xE4 the strip's width, +0xE8 an arrow's height, +0xEC
// the picture's height less two arrows, +0xF0 to +0x100 five numbers of
// the layout, +0x104 the pointer's place in the picture, +0x108 the
// pointer's rectangle (left, top, right, bottom), +0x118 flags (1: paint
// again), +0x11C and +0x11D the upper and the lower arrow can be used,
// +0x120 the arrow under the mouse (0 none, 1 upper, 2 lower), +0x124 the
// arrow held down, +0x128 a timer.
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

// The window part of each class, from its own start.
namespace win {
constexpr std::size_t kId = 0x4C;
constexpr std::size_t kLeft = 0xA8;
constexpr std::size_t kTop = 0xAC;
constexpr std::size_t kRight = 0xB0;
constexpr std::size_t kBottom = 0xB4;
constexpr std::size_t kFlags = 0xC8;
} // namespace win

constexpr std::size_t kWindow = 0x4;

namespace tip {
constexpr std::size_t kTitle = 0xF4;
constexpr std::size_t kText = 0x108;
} // namespace tip

namespace item {
constexpr std::size_t kLabel = 0x4;
constexpr std::size_t kTipText = 0x18;
constexpr std::size_t kPrice = 0x2C;
constexpr std::size_t kPicture = 0x30;
constexpr std::size_t kValue = 0x34;
constexpr std::size_t kId = 0x38;
constexpr std::size_t kCount = 0x3C;
constexpr std::size_t kEnabled = 0x40;
} // namespace item

namespace menu {
constexpr std::size_t kItems = 0xDC;
constexpr std::size_t kItemsEnd = 0xE0;
constexpr std::size_t kItemsCapacity = 0xE4;
constexpr std::size_t kCount = 0xE8;
constexpr std::size_t kFirst = 0xEC;
constexpr std::size_t kHover = 0xF0;
constexpr std::size_t kChosen = 0xF4;
constexpr std::size_t kWidth = 0xF8;
constexpr std::size_t kRowHeight = 0xFC;
constexpr std::size_t kGap = 0x100;
constexpr std::size_t kFont = 0x104;
constexpr std::size_t kTip = 0x108;
constexpr std::size_t kSpare = 0x10C;
constexpr std::size_t kTimer = 0x110;
constexpr std::size_t kTicks = 0x138;
constexpr std::size_t kTimedRow = 0x13C;
} // namespace menu

namespace arrows {
constexpr std::size_t kPicture = 0xDC;
constexpr std::size_t kStrip = 0xE0;
constexpr std::size_t kStripWidth = 0xE4;
constexpr std::size_t kArrowHeight = 0xE8;
constexpr std::size_t kMiddle = 0xEC;
constexpr std::size_t kA = 0xF0;
constexpr std::size_t kMinHeight = 0xF4;
constexpr std::size_t kB = 0xF8;
constexpr std::size_t kC = 0xFC;
constexpr std::size_t kD = 0x100;
constexpr std::size_t kPointerAt = 0x104;
constexpr std::size_t kPointerLeft = 0x108;
constexpr std::size_t kPointerTop = 0x10C;
constexpr std::size_t kPointerRight = 0x110;
constexpr std::size_t kPointerBottom = 0x114;
constexpr std::size_t kFlags = 0x118;
constexpr std::size_t kUpEnabled = 0x11C;
constexpr std::size_t kDownEnabled = 0x11D;
constexpr std::size_t kHover = 0x120;
constexpr std::size_t kPressed = 0x124;
constexpr std::size_t kTimer = 0x128;
} // namespace arrows

namespace sounder {
constexpr std::size_t kHeld = 0xE8;
constexpr std::size_t kFilter = 0x10C;
constexpr std::size_t kHost = 0x110;
constexpr std::size_t kSounds = 0x114;
} // namespace sounder

constexpr uint32_t kItemIid = 0x49D98263;
constexpr uint32_t kMenuIid = 0x49D98264;
constexpr uint32_t kArrowsIid = 0x8A25CE2C;
constexpr uint32_t kFilterIid = 0xC6AE7085;

constexpr uint32_t kCommand = 3;
constexpr uint32_t kRowChosen = 0x49D993D4;
constexpr uint32_t kMenuTick = 0xAA5187A4;
constexpr uint32_t kStep = 0xCA35E4BA;
constexpr uint32_t kMenuFont = 0x68963C4C;

constexpr uint32_t kMoveSound = 0x2A5C322B;
constexpr uint32_t kRefuseSound = 0xAA62C9D5;
constexpr uint32_t kSoundType = 0x5B523BA4;

// The game's string body and string.
struct StringBody {
    char* begin;
    char* end;
    char* capacity;
};

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

uint32_t AddRef(void* object)
{
    return Virtual<uint32_t>(object, 0x4);
}

uint32_t Release(void* object)
{
    return Virtual<uint32_t>(object, 0x8);
}

// The game's inlined reference holder: empty it.
void Clear(void*& holder)
{
    if (void* const object = holder) {
        holder = nullptr;
        Release(object);
    }
}

// The inlined assignment to a reference holder.
void Hold(void*& holder, void* object)
{
    void* const old = holder;
    if (object == old)
        return;
    if (object)
        AddRef(object);
    holder = object;
    if (old)
        Release(old);
}

// A destructor's inlined end of a reference holder.
void Drop(void* object)
{
    if (object)
        Release(object);
}

// An empty string's body as the game's inlined constructor makes it.
void InitBody(StringBody& body)
{
    body.begin = nullptr;
    body.end = nullptr;
    body.capacity = nullptr;
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    body.begin = buffer;
    body.end = buffer;
    body.capacity = buffer + 8;
    *buffer = 0;
}

// The inlined destructor.
void DestroyString(GameString& text)
{
    text.vtable = addr::SC4String_Vtable;
    if (char* const buffer = text.body.begin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(buffer));
}

// Half of a number as the game's code has it (toward zero).
int32_t Half(int32_t value)
{
    return value / 2;
}

void Sound(uint32_t id)
{
    if (void* const sounds = Global<void*>(addr::SC4_Sound43CB8))
        Virtual<void>(sounds, 0x2C, uint32_t{1}, id, uint32_t{0}, uint32_t{0}, uint32_t{0});
}

// A timer stopped when it runs.
void StopIfRunning(void* timer)
{
    if (Virtual<uint8_t>(timer, 0x20))
        Virtual<void>(timer, 0x1C);
}

// --- The info tip ---------------------------------------------------------------------

// The game's 0x00799DD0: the constructor. The window's flag 0x200000 is
// set; the fields the methods before this range use are cleared.
char* __fastcall InfoTipConstruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4InfoTip_BaseVtable;
    This<void*>(addr::GZWin_Construct, self + kWindow);
    At<std::uintptr_t>(self, 0x0) = addr::SC4InfoTip_Vtable;
    At<std::uintptr_t>(self, kWindow) = addr::SC4InfoTip_WindowVtable;
    At<uint32_t>(self, 0xDC) = 0;
    At<uint32_t>(self, 0xE0) = 0;
    At<uint32_t>(self, 0xE4) = 0;
    At<uint32_t>(self, 0xE8) = 0;
    At<uint32_t>(self, 0xEC) = 0;
    At<uint32_t>(self, 0xF0) = 0;
    GameString& title = At<GameString>(self, tip::kTitle);
    InitBody(title.body);
    title.zero = 0;
    title.vtable = addr::SC4String_Vtable;
    GameString& text = At<GameString>(self, tip::kText);
    InitBody(text.body);
    text.vtable = addr::SC4String_Vtable;
    text.zero = 0;
    At<int32_t>(self, 0x11C) = -0x1C;
    At<int32_t>(self, 0x120) = -0x1C;
    At<int32_t>(self, 0x124) = 0x1C;
    At<int32_t>(self, 0x128) = 0x1C;
    At<uint32_t>(self, 0x12C) = 0;
    At<uint32_t>(self, 0x130) = 0;
    At<uint32_t>(self, 0x134) = 0;
    At<uint32_t>(self, 0x138) = 0;
    At<uint32_t>(self, 0x13C) = 0;
    At<uint32_t>(self, 0x140) = 0;
    At<uint32_t>(self, 0x144) = 0;
    At<uint32_t>(self, 0x148) = 0;
    At<uint8_t>(self, 0x164) = 4;
    At<uint32_t>(self, 0x188) = 0;
    At<uint32_t>(self, 0x18C) = 0;
    At<uint32_t>(self, 0x190) = 0;
    At<uint32_t>(self, 0x194) = 0;
    At<uint32_t>(self, kWindow + win::kFlags) |= 0x200000;
    At<uint32_t>(self, 0x1A8) = 0;
    At<uint32_t>(self, 0x1B0) = 0;
    return self;
}

// The game's 0x00799F50: the destructor. The window part's Shutdown
// (0x00798220), the seven references, the two strings, the window.
void __fastcall InfoTipDestruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4InfoTip_Vtable;
    At<std::uintptr_t>(self, kWindow) = addr::SC4InfoTip_WindowVtable;
    This<uint8_t>(addr::SC4UI_F798220, self + kWindow);
    Drop(At<void*>(self, 0x148));
    Drop(At<void*>(self, 0x144));
    Drop(At<void*>(self, 0x140));
    Drop(At<void*>(self, 0x13C));
    Drop(At<void*>(self, 0x138));
    Drop(At<void*>(self, 0x134));
    Drop(At<void*>(self, 0x130));
    DestroyString(At<GameString>(self, tip::kText));
    DestroyString(At<GameString>(self, tip::kTitle));
    This<void>(addr::GZWin_Destruct, self + kWindow);
}

// The game's 0x0079A020 (reached through the window part's slot 148): the
// deleting destructor.
char* __fastcall InfoTipDelete(char* self, void*, uint32_t flags)
{
    InfoTipDestruct(self, nullptr);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// The game's 0x00799F30 (slot 0 of the window part): QueryInterface of the
// whole object, 0x00797F50.
uint8_t __fastcall InfoTipQueryThunk(char* part, void*, uint32_t iid, void** out)
{
    return This<uint8_t>(addr::SC4UI_F797F50, part - kWindow, iid, out);
}

// The game's 0x00799F40 (slot 148 of the window part).
char* __fastcall InfoTipDeleteThunk(char* part, void*, uint32_t flags)
{
    return InfoTipDelete(part - kWindow, nullptr, flags);
}

// --- A menu item ----------------------------------------------------------------------

// The game's 0x0079A040 (slot 0): interfaces 1 and 0x49D98263.
uint8_t __fastcall ItemQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    if (iid != kItemIid && iid != 1)
        return 0;
    *out = self;
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// The game's 0x0079A070 (slot 1): AddRef.
uint32_t __fastcall ItemAddRef(char* self, void*)
{
    return ++At<uint32_t>(self, item::kCount);
}

// The game's 0x0079B6B0: the constructor. An item starts enabled, with
// no reference.
char* __fastcall ItemConstruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4MenuItem_Vtable;
    GameString& label = At<GameString>(self, item::kLabel);
    InitBody(label.body);
    label.vtable = addr::SC4String_Vtable;
    label.zero = 0;
    GameString& text = At<GameString>(self, item::kTipText);
    InitBody(text.body);
    text.zero = 0;
    text.vtable = addr::SC4String_Vtable;
    At<uint32_t>(self, item::kPrice) = 0;
    At<void*>(self, item::kPicture) = nullptr;
    At<uint32_t>(self, item::kValue) = 0;
    At<uint32_t>(self, item::kId) = 0;
    At<uint32_t>(self, item::kCount) = 0;
    At<uint8_t>(self, item::kEnabled) = 1;
    return self;
}

// The game's 0x0079B740 (slot 10): the item can be chosen or not.
void __fastcall ItemSetEnabled(char* self, void*, uint8_t enabled)
{
    At<uint8_t>(self, item::kEnabled) = enabled;
}

// The game's 0x0079B750: the destructor. The picture is let go by the
// inlined clearing and then asked about once more, as in the original.
void __fastcall ItemDestruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4MenuItem_Vtable;
    Clear(At<void*>(self, item::kPicture));
    Drop(At<void*>(self, item::kPicture));
    DestroyString(At<GameString>(self, item::kTipText));
    DestroyString(At<GameString>(self, item::kLabel));
}

// The game's 0x0079B820 (slot 2): Release.
uint32_t __fastcall ItemRelease(char* self, void*)
{
    const uint32_t count = At<uint32_t>(self, item::kCount);
    if (count > 1) {
        At<uint32_t>(self, item::kCount) = count - 1;
        return count - 1;
    }
    ItemDestruct(self, nullptr);
    Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return 0;
}

// --- The menu -------------------------------------------------------------------------

// The rows that fit into the window: its height and one gap over a row
// and a gap.
int32_t VisibleRows(char* self)
{
    const int32_t gap = At<int32_t>(self, menu::kGap);
    const int32_t height = At<int32_t>(self, kWindow + win::kBottom) - At<int32_t>(self, kWindow + win::kTop);
    return (height + gap) / (At<int32_t>(self, menu::kRowHeight) + gap);
}

// The game's 0x0079A080 (slot 0): interface 0x49D98264, or the window's.
uint8_t __fastcall MenuQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    if (iid == kMenuIid) {
        *out = self;
        Virtual<uint32_t>(self, 0x4);
        return 1;
    }
    return This<uint8_t>(addr::SC4Win_QueryInterface, self + kWindow, iid, out);
}

// The game's 0x0079A0B0 (cdecl, given to the menu's timer by 0x0079A510):
// sends the menu's window the tick message. The first argument of the send
// is the window itself here, where the arrows give their window's ID.
uint32_t __cdecl MenuTimerTick(char* self)
{
    void* const window = self ? self + kWindow : nullptr;
    return Virtual<uint32_t>(self + kWindow, 0x248, window, kMenuTick, uint32_t{0}, uint32_t{0}, uint32_t{0});
}

// The game's 0x0079A0E0 (slot 12): the rows' width, height and gap.
void __fastcall MenuSetMetrics(char* self, void*, int32_t width, int32_t rowHeight, int32_t gap)
{
    At<int32_t>(self, menu::kWidth) = width;
    At<int32_t>(self, menu::kRowHeight) = rowHeight;
    At<int32_t>(self, menu::kGap) = gap;
}

// The game's 0x0079A280 (slot 4 of the window part): Init. The font
// manager is asked for and not used; the menu takes its usual font when it
// has none and shows its first row.
uint8_t __fastcall MenuInit(char* part, void*)
{
    if (!This<uint8_t>(addr::GZWin_IsInitialized, part)) {
        Virtual<void>(part, 0x110, uint32_t{0x10000}, uint32_t{0});
        Virtual<void>(part, 0x110, uint32_t{4}, uint32_t{1});
        This<uint8_t>(addr::GZWin_Init, part);
        Cdecl<void*>(addr::SC4_Singleton913C72);
        char* const self = part - kWindow;
        if (!At<void*>(self, menu::kFont))
            Virtual<void>(self, 0x3C, kMenuFont);
        Virtual<void>(self, 0x40, int32_t{0});
    }
    return 1;
}

// The game's 0x0079A2E0 (slot 5 of the window part): Shutdown. Stops the
// timer, drops the items, the font, the info tip (taken out of the window
// first) and the spare reference.
uint8_t __fastcall MenuShutdown(char* part, void*)
{
    if (!This<uint8_t>(addr::GZWin_IsInitialized, part))
        return 1;
    char* const self = part - kWindow;
    StopIfRunning(self + menu::kTimer);
    Virtual<void>(self, 0x2C);
    Clear(At<void*>(self, menu::kFont));
    if (void* const tip = At<void*>(self, menu::kTip))
        Virtual<void>(part, 0x40, Virtual<void*>(tip, 0xC));
    Clear(At<void*>(self, menu::kTip));
    Clear(At<void*>(self, menu::kSpare));
    return This<uint8_t>(addr::SC4Win_F99D2FE, part);
}

// The game's 0x0079A380 (slot 22): the info tip to use. The old one's
// window leaves the menu's window, the new one's joins it.
void __fastcall MenuSetTip(char* self, void*, void* tip)
{
    if (void* const old = At<void*>(self, menu::kTip))
        Virtual<void>(self + kWindow, 0x40, Virtual<void*>(old, 0xC));
    Hold(At<void*>(self, menu::kTip), tip);
    if (void* const now = At<void*>(self, menu::kTip))
        Virtual<void>(self + kWindow, 0x38, Virtual<void*>(now, 0xC));
}

// The game's 0x0079A3F0 (slot 6): the item with an ID, or null.
void* __fastcall MenuFindItem(char* self, void*, uint32_t id)
{
    void** const end = At<void**>(self, menu::kItemsEnd);
    for (void** it = At<void**>(self, menu::kItems); it != end; ++it) {
        if (Virtual<uint32_t>(*it, 0x20) == id)
            return *it;
    }
    return nullptr;
}

// The game's 0x0079A430 (slot 18): every item but the one with the ID is
// given the byte (the whole argument is passed on); says whether the ID was
// found.
uint8_t __fastcall MenuEnableOthers(char* self, void*, uint32_t id, uint32_t enabled)
{
    uint8_t found = 0;
    void** const end = At<void**>(self, menu::kItemsEnd);
    for (void** it = At<void**>(self, menu::kItems); it != end; ++it) {
        if (Virtual<uint32_t>(*it, 0x20) == id)
            found = 1;
        else
            Virtual<void>(*it, 0x28, enabled);
    }
    return found;
}

// The game's 0x0079A480 (slot 19): where the row of an item is, as the
// window's slot 0xF0 converts the point (16 right of the rows, the middle
// of the row).
uint8_t __fastcall MenuRowPlace(char* self, void*, uint32_t id, int32_t* x, int32_t* y)
{
    int32_t index = 0;
    void** const end = At<void**>(self, menu::kItemsEnd);
    for (void** it = At<void**>(self, menu::kItems);; ++it, ++index) {
        if (it == end)
            return 0;
        if (Virtual<uint32_t>(*it, 0x20) == id)
            break;
    }
    *x = At<int32_t>(self, menu::kWidth) + 0x10;
    const int32_t rowHeight = At<int32_t>(self, menu::kRowHeight);
    *y = (index - At<int32_t>(self, menu::kFirst)) * (At<int32_t>(self, menu::kGap) + rowHeight) + Half(rowHeight);
    Virtual<void>(self + kWindow, 0xF0, x, y);
    return 1;
}

// The game's 0x0079A510 (slot 21): starts the menu's timer for a row (the
// ticks to wait and the timer's time are given), or with -1 only stops it.
uint8_t __fastcall MenuStartTimer(char* self, void*, uint32_t id, uint32_t ticks, uint32_t time)
{
    char* const timer = self + menu::kTimer;
    if (id == 0xFFFFFFFF) {
        StopIfRunning(timer);
        At<uint32_t>(self, menu::kTimedRow) = 0;
        At<uint32_t>(self, menu::kTicks) = ticks;
        return 0;
    }
    uint32_t index = 0;
    void** const end = At<void**>(self, menu::kItemsEnd);
    for (void** it = At<void**>(self, menu::kItems);; ++it, ++index) {
        if (it == end)
            return 0;
        if (Virtual<uint32_t>(*it, 0x20) == id)
            break;
    }
    StopIfRunning(timer);
    At<uint32_t>(self, menu::kTimedRow) = index;
    At<uint32_t>(self, menu::kTicks) = ticks;
    Virtual<void>(timer, 0x24, addr::SC4UI_F79A0B0, self);
    Virtual<void>(timer, 0x14, time, uint32_t{0});
    return 1;
}

void* ItemAt(char* self, std::size_t field)
{
    const int32_t index = At<int32_t>(self, field);
    if (index < 0 || index >= At<int32_t>(self, menu::kCount))
        return nullptr;
    return At<void**>(self, menu::kItems)[index];
}

// The game's 0x0079A5E0 (slot 7): the item under the mouse, or null.
void* __fastcall MenuHoverItem(char* self, void*)
{
    return ItemAt(self, menu::kHover);
}

// The game's 0x0079A600 (slot 8): the chosen item, or null.
void* __fastcall MenuChosenItem(char* self, void*)
{
    return ItemAt(self, menu::kChosen);
}

// The game's 0x0079A620 (slot 13): the size a number of rows needs.
int32_t* __fastcall MenuSizeFor(char* self, void*, int32_t* out, uint32_t rows)
{
    const int32_t width = At<int32_t>(self, menu::kWidth);
    if (rows == 0) {
        out[0] = width;
        out[1] = 0;
        return out;
    }
    const uint32_t gap = At<uint32_t>(self, menu::kGap);
    const uint32_t height = (At<uint32_t>(self, menu::kRowHeight) + gap) * rows - gap;
    out[0] = width;
    out[1] = static_cast<int32_t>(height);
    return out;
}

// The game's 0x0079A660 (slot 16): the first row to show, kept within what
// the window can show. The window's parent, when it is the arrows, is told
// whether there are rows above and below. The two bytes go as the low
// bytes of the difference the original has in the register.
void __fastcall MenuSetFirst(char* self, void*, int32_t row)
{
    const int32_t visible = VisibleRows(self);
    int32_t first = row;
    const int32_t last = At<int32_t>(self, menu::kCount) - visible;
    if (first > last)
        first = last;
    if (first < 0)
        first = 0;
    if (At<int32_t>(self, menu::kFirst) != first) {
        At<int32_t>(self, menu::kFirst) = first;
        Virtual<void>(self + kWindow, 0x170);
    }
    void* const parent = Virtual<void*>(self + kWindow, 0x2C);
    void* frame = reinterpret_cast<void*>(static_cast<std::intptr_t>(row));
    if (Virtual<uint8_t>(parent, 0x0, kArrowsIid, &frame)) {
        const int32_t below = At<int32_t>(self, menu::kCount) - visible;
        const uint32_t high = static_cast<uint32_t>(below) & 0xFFFFFF00;
        const uint32_t down = high | (first < below ? 1u : 0u);
        const uint32_t up = high | (first > 0 ? 1u : 0u);
        Virtual<void>(frame, 0x1C, up, down);
        Release(frame);
    }
}

// The game's 0x0079A710: moves the first row by a number of rows, with a
// sound when it moved.
void __fastcall MenuScroll(char* self, void*, int32_t rows)
{
    const int32_t before = At<int32_t>(self, menu::kFirst);
    Virtual<void>(self, 0x40, before + rows);
    if (before != At<int32_t>(self, menu::kFirst))
        Sound(kMoveSound);
}

// The game's 0x0079A760 (slot 3 of the window part): the window's
// messages. The timer's tick counts down and then stops the timer; the
// arrows' step scrolls; the rest is the window's.
uint8_t __fastcall MenuMessage(char* part, void*, const uint32_t* message)
{
    char* const self = part - kWindow;
    const uint32_t type = message[0];
    if (type == kMenuTick) {
        const uint32_t ticks = At<uint32_t>(self, menu::kTicks);
        if (ticks > 0) {
            At<uint32_t>(self, menu::kTicks) = ticks - 1;
            return 1;
        }
        StopIfRunning(self + menu::kTimer);
        return 1;
    }
    if (type == kStep) {
        MenuScroll(self, nullptr, static_cast<int32_t>(message[1]));
        return 1;
    }
    return This<uint8_t>(addr::SC4Win_F99CCF0, part, message);
}

// The game's 0x0079A7D0 (slot 15): the font, by its ID.
void __fastcall MenuSetFont(char* self, void*, uint32_t id)
{
    void* const font = Virtual<void*>(Cdecl<void*>(addr::SC4_Singleton913C72), 0x14, id);
    Hold(At<void*>(self, menu::kFont), font);
}

// The game's 0x0079A820 (slot 9): the chosen item's ID, or 0.
uint32_t __fastcall MenuChosenId(char* self, void*)
{
    const int32_t index = At<int32_t>(self, menu::kChosen);
    if (index < 0 || index >= At<int32_t>(self, menu::kCount))
        return 0;
    return Virtual<uint32_t>(At<void**>(self, menu::kItems)[index], 0x20);
}

// The game's 0x0079A850 (slot 10): chooses the item with an ID (-1 when
// there is none).
void __fastcall MenuChooseId(char* self, void*, uint32_t id)
{
    for (int32_t index = 0; index < At<int32_t>(self, menu::kCount); ++index) {
        if (Virtual<uint32_t>(At<void**>(self, menu::kItems)[index], 0x20) == id) {
            At<int32_t>(self, menu::kChosen) = index;
            return;
        }
    }
    At<int32_t>(self, menu::kChosen) = -1;
}

// The game's 0x0079A8A0 (slot 20): moves the first row so that an item's
// row is on show.
uint8_t __fastcall MenuShowItem(char* self, void*, uint32_t id)
{
    const uint32_t visible = static_cast<uint32_t>(VisibleRows(self));
    uint32_t index = 0;
    void** const end = At<void**>(self, menu::kItemsEnd);
    for (void** it = At<void**>(self, menu::kItems);; ++it, ++index) {
        if (it == end)
            return 0;
        if (Virtual<uint32_t>(*it, 0x20) == id)
            break;
    }
    const uint32_t first = At<uint32_t>(self, menu::kFirst);
    if (index < first) {
        Virtual<void>(self, 0x40, index);
        return 1;
    }
    if (index >= first + visible)
        Virtual<void>(self, 0x40, index - visible + 1);
    return 1;
}

// The game's 0x0079A940 (slot 136 of the window part): a mouse press. An
// enabled row under the mouse is chosen: the window sends the command with
// the item's value and ID. Only "below zero" is checked for the row.
uint8_t __fastcall MenuMouseDown(char* part, void*, int32_t, int32_t, uint32_t)
{
    char* const self = part - kWindow;
    const int32_t hover = At<int32_t>(self, menu::kHover);
    if (hover >= 0 && Virtual<uint8_t>(At<void**>(self, menu::kItems)[hover], 0x24)) {
        void* const row = At<void**>(self, menu::kItems)[At<int32_t>(self, menu::kHover)];
        const uint32_t id = Virtual<uint32_t>(row, 0x20);
        const uint32_t value = Virtual<uint32_t>(row, 0x1C);
        Virtual<void>(part, 0x248, At<uint32_t>(part, win::kId), kCommand, kRowChosen, value, id);
        At<int32_t>(self, menu::kChosen) = At<int32_t>(self, menu::kHover);
        Sound(kMoveSound);
        return 1;
    }
    Sound(kRefuseSound);
    return 1;
}

// The game's 0x0079A9F0 (slot 139 of the window part): the mouse wheel.
// A notch of 300 is a row; then the same as a mouse move.
uint8_t __fastcall MenuMouseWheel(char* part, void*, int32_t x, int32_t y, uint32_t flags, int32_t turn)
{
    char* const self = part - kWindow;
    const int32_t before = At<int32_t>(self, menu::kFirst);
    Virtual<void>(self, 0x40, before - turn / 300);
    if (before != At<int32_t>(self, menu::kFirst))
        Sound(kMoveSound);
    return Virtual<uint8_t>(part, 0x228, x, y, flags);
}

// The game's 0x0079B500: the constructor.
char* __fastcall MenuConstruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4Menu_BaseVtable;
    This<void*>(addr::GZWin_Construct, self + kWindow);
    At<std::uintptr_t>(self, kWindow) = addr::SC4Menu_WindowVtable;
    At<std::uintptr_t>(self, 0x0) = addr::SC4Menu_Vtable;
    At<void**>(self, menu::kItems) = nullptr;
    At<void**>(self, menu::kItemsEnd) = nullptr;
    At<void**>(self, menu::kItemsCapacity) = nullptr;
    At<int32_t>(self, menu::kCount) = 0;
    At<int32_t>(self, menu::kFirst) = 0;
    At<int32_t>(self, menu::kHover) = -1;
    At<int32_t>(self, menu::kChosen) = -1;
    At<void*>(self, menu::kFont) = nullptr;
    At<void*>(self, menu::kTip) = nullptr;
    At<void*>(self, menu::kSpare) = nullptr;
    This<void*>(addr::cRZTimer_Construct, self + menu::kTimer);
    At<uint32_t>(self, menu::kTicks) = 0;
    At<uint32_t>(self, menu::kTimedRow) = 0;
    return self;
}

// The game's 0x0079B5B0: the destructor.
void __fastcall MenuDestruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4Menu_Vtable;
    At<std::uintptr_t>(self, kWindow) = addr::SC4Menu_WindowVtable;
    MenuShutdown(self + kWindow, nullptr);
    This<void>(addr::cRZTimer_Destruct, self + menu::kTimer);
    Drop(At<void*>(self, menu::kSpare));
    Drop(At<void*>(self, menu::kTip));
    Drop(At<void*>(self, menu::kFont));
    if (void** const items = At<void**>(self, menu::kItems))
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(items));
    This<void>(addr::GZWin_Destruct, self + kWindow);
}

// The game's 0x0079B620 (reached through the window part's slot 148): the
// deleting destructor.
char* __fastcall MenuDelete(char* self, void*, uint32_t flags)
{
    MenuDestruct(self, nullptr);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// The game's 0x0079B590 (slot 0 of the window part).
uint8_t __fastcall MenuQueryThunk(char* part, void*, uint32_t iid, void** out)
{
    return MenuQueryInterface(part - kWindow, nullptr, iid, out);
}

// The game's 0x0079B5A0 (slot 148 of the window part).
char* __fastcall MenuDeleteThunk(char* part, void*, uint32_t flags)
{
    return MenuDelete(part - kWindow, nullptr, flags);
}

// The game's 0x0079B640 (slot 11): lets every item go and empties the
// vector. The original's erase compares the end with itself, so its
// branch that would move elements is never taken.
void __fastcall MenuClear(char* self, void*)
{
    void** const end = At<void**>(self, menu::kItemsEnd);
    for (void** it = At<void**>(self, menu::kItems); it != end; ++it)
        Release(*it);
    At<void**>(self, menu::kItemsEnd) = At<void**>(self, menu::kItems);
    At<int32_t>(self, menu::kCount) = 0;
}

// The game's 0x0079B7B0 (slot 4): adds an item at the end, with a
// reference. The vector's allocator tag is the address of the argument, as
// in the original.
void __fastcall MenuAdd(char* self, void*, void* added)
{
    AddRef(added);
    void** const end = At<void**>(self, menu::kItemsEnd);
    if (end != At<void**>(self, menu::kItemsCapacity)) {
        if (end)
            *end = added;
        At<void**>(self, menu::kItemsEnd) += 1;
    } else {
        This<void>(addr::SC4PtrVector_InsertN, self + menu::kItems, end, &added, static_cast<void*>(&added),
                   uint32_t{1}, uint32_t{1});
    }
    ++At<int32_t>(self, menu::kCount);
}

// The game's 0x0079B850: the row under the mouse. When it changes the
// window is painted again and the info tip, when there is one, shows the
// row's label (with " - " and the price when the item has a price and the
// menu a font) next to the row, or hides for no row.
void __fastcall MenuSetHover(char* self, void*, int32_t row)
{
    if (row == At<int32_t>(self, menu::kHover))
        return;
    char* const window = self + kWindow;
    At<int32_t>(self, menu::kHover) = row;
    Virtual<void>(window, 0x170);
    void* tip = At<void*>(self, menu::kTip);
    if (!tip)
        return;
    const int32_t hover = At<int32_t>(self, menu::kHover);
    if (hover < 0) {
        Virtual<void>(Virtual<void*>(tip, 0xC), 0x118);
        return;
    }
    void* const shown = At<void**>(self, menu::kItems)[hover];
    const int32_t* const area = Virtual<const int32_t*>(tip, 0x50);
    int32_t place[4] = {area[0], area[1], area[2], area[3]};
    GameString text;
    This<void*>(addr::SC4String_FromIGZString, &text, Virtual<void*>(shown, 0xC));
    if (Virtual<uint32_t>(shown, 0x14) > 0 && At<void*>(self, menu::kFont)) {
        GameString price;
        This<void*>(addr::SC4String_SC4String, &price);
        Cdecl<void>(addr::SC4_FormatMoney, static_cast<int64_t>(uint64_t{Virtual<uint32_t>(shown, 0x14)}), &price);
        This<void>(addr::cSC4Advisor_F410F00, &text.body, " - ");
        This<void>(addr::SC4String_AppendStd, &text.body, &price.body);
        This<void>(addr::SC4String_Destroy, &price);
    }
    int32_t point[2];
    point[0] = At<int32_t>(self, menu::kWidth) + 0x10;
    const int32_t rowHeight = At<int32_t>(self, menu::kRowHeight);
    point[1] = (At<int32_t>(self, menu::kHover) - At<int32_t>(self, menu::kFirst)) *
                   (At<int32_t>(self, menu::kGap) + rowHeight) +
               Half(rowHeight);
    Virtual<void>(window, 0xF0, &point[0], &point[1]);
    place[0] = point[0];
    Virtual<void>(At<void*>(self, menu::kTip), 0x18, &text);
    tip = At<void*>(self, menu::kTip);
    Virtual<void>(tip, 0x20, Virtual<void*>(shown, 0x10));
    Virtual<void>(At<void*>(self, menu::kTip), 0x3C, &point[0]);
    int32_t none[4] = {0, 0, 0, 0};
    Virtual<void>(At<void*>(self, menu::kTip), 0x44, &none[0]);
    Virtual<void>(At<void*>(self, menu::kTip), 0x4C, &place[0]);
    Virtual<void>(Virtual<void*>(At<void*>(self, menu::kTip), 0xC), 0x114);
    DestroyString(text);
}

// The game's 0x0079BA40 (slot 138 of the window part): a mouse move. The
// row under the point, when there is an item for it.
uint8_t __fastcall MenuMouseMove(char* part, void*, int32_t, int32_t y, uint32_t)
{
    char* const self = part - kWindow;
    const int32_t gap = At<int32_t>(self, menu::kGap);
    int32_t row = (y - Half(gap)) / (At<int32_t>(self, menu::kRowHeight) + gap);
    if (row >= 0) {
        row += At<int32_t>(self, menu::kFirst);
        if (row < At<int32_t>(self, menu::kCount)) {
            MenuSetHover(self, nullptr, row);
            return 1;
        }
    }
    MenuSetHover(self, nullptr, -1);
    return 1;
}

// The game's 0x0079BAA0 (slot 142 of the window part): the mouse has
// left.
uint8_t __fastcall MenuMouseLeave(char* part, void*, uint32_t)
{
    MenuSetHover(part - kWindow, nullptr, -1);
    return 1;
}

// --- The menu's arrows ----------------------------------------------------------------

// The inlined "paint again".
void ArrowsChanged(char* self)
{
    At<uint32_t>(self, arrows::kFlags) |= 1;
    Virtual<void>(self + kWindow, 0x170);
}

// The step message for the arrow held down: -1 for the upper, 1 for the
// lower.
void ArrowsSendStep(char* self)
{
    const uint32_t pressed = At<uint32_t>(self, arrows::kPressed);
    if (!pressed)
        return;
    Virtual<void>(self + kWindow, 0x248, At<uint32_t>(self, kWindow + win::kId), kStep,
                  pressed != 1 ? int32_t{1} : int32_t{-1}, uint32_t{0}, uint32_t{0});
}

// The game's 0x0079A110 (slot 0): interface 0x8A25CE2C, or the window's.
uint8_t __fastcall ArrowsQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    if (iid == kArrowsIid) {
        *out = self;
        Virtual<uint32_t>(self, 0x4);
        return 1;
    }
    return This<uint8_t>(addr::SC4Win_QueryInterface, self + kWindow, iid, out);
}

// The game's 0x0079A140 (slot 4 of the window part): Init.
uint8_t __fastcall ArrowsInit(char* part, void*)
{
    if (!This<uint8_t>(addr::GZWin_IsInitialized, part)) {
        Virtual<void>(part, 0x110, uint32_t{4}, uint32_t{1});
        Virtual<void>(part, 0x110, uint32_t{0x10000}, uint32_t{0});
        This<uint8_t>(addr::GZWin_Init, part);
    }
    return 1;
}

// The game's 0x0079A180 (slot 62 of the window part): the point is
// converted by slot 0xEC in place and then asked about (slot 0x1E4).
uint8_t __fastcall ArrowsHitScreen(char* part, void*, int32_t x, int32_t y)
{
    Virtual<void>(part, 0xEC, &x, &y);
    return Virtual<uint8_t>(part, 0x1E4, x, y);
}

// The game's 0x0079A1B0 (slot 142 of the window part): the mouse has
// left.
uint8_t __fastcall ArrowsMouseLeave(char* part, void*, uint32_t)
{
    char* const self = part - kWindow;
    if (At<uint32_t>(self, arrows::kHover)) {
        At<uint32_t>(self, arrows::kFlags) |= 1;
        At<uint32_t>(self, arrows::kHover) = 0;
        Virtual<void>(part, 0x170);
    }
    return 1;
}

// The game's 0x0079A1E0 (slot 136 of the window part): the mouse button
// released. Gives the mouse back, lets the arrow go and stops the timer.
uint8_t __fastcall ArrowsMouseUp(char* part, void*, int32_t, int32_t, uint32_t)
{
    char* const self = part - kWindow;
    This<void>(addr::SC4Win_F99BE1E, part);
    if (At<uint32_t>(self, arrows::kPressed)) {
        At<uint32_t>(self, arrows::kFlags) |= 1;
        At<uint32_t>(self, arrows::kPressed) = 0;
        Virtual<void>(part, 0x170);
    }
    Virtual<void>(self + arrows::kTimer, 0x1C);
    return 1;
}

// The game's 0x0079A230 (cdecl, given to the arrows' timer by 0x0079AF40):
// another step for the arrow held down, and the timer set to the repeat
// time.
void __cdecl ArrowsTimerTick(char* self)
{
    ArrowsSendStep(self);
    Virtual<void>(self + arrows::kTimer, 0xC, Global<uint32_t>(addr::SC4MenuArrows_RepeatTime));
}

// The game's 0x0079AC00 (slot 5 of the window part): Shutdown.
uint8_t __fastcall ArrowsShutdown(char* part, void*)
{
    if (!This<uint8_t>(addr::GZWin_IsInitialized, part))
        return 1;
    char* const self = part - kWindow;
    Clear(At<void*>(self, arrows::kPicture));
    Clear(At<void*>(self, arrows::kStrip));
    Virtual<void>(self + arrows::kTimer, 0x1C);
    return This<uint8_t>(addr::SC4Win_F99D2FE, part);
}

// The game's 0x0079AC60 (slot 4): the picture and seven numbers of the
// frame. The picture's height (its slot 0x28) less two arrows is the
// stretch between them.
void __fastcall ArrowsSetup(char* self, void*, void* picture, int32_t stripWidth, int32_t arrowHeight, int32_t a,
                            int32_t minHeight, int32_t b, int32_t c, int32_t d)
{
    Hold(At<void*>(self, arrows::kPicture), picture);
    At<int32_t>(self, arrows::kStripWidth) = stripWidth;
    At<int32_t>(self, arrows::kArrowHeight) = arrowHeight;
    const int32_t height = Virtual<int32_t>(At<void*>(self, arrows::kPicture), 0x28);
    At<int32_t>(self, arrows::kMiddle) = height - arrowHeight * 2;
    At<int32_t>(self, arrows::kMinHeight) = minHeight;
    At<int32_t>(self, arrows::kD) = d;
    At<int32_t>(self, arrows::kA) = a;
    At<int32_t>(self, arrows::kB) = b;
    At<int32_t>(self, arrows::kC) = c;
    At<uint32_t>(self, arrows::kFlags) |= 1;
}

// The game's 0x0079AD00 (slot 5): places the frame for a menu of a given
// size next to a point, kept between an upper and a lower limit, and
// works out where the pointer and the two arrows are.
void __fastcall ArrowsPlace(char* self, void*, int32_t width, int32_t height, int32_t x, int32_t y, int32_t upper,
                            int32_t lower)
{
    int32_t right =
        At<int32_t>(self, arrows::kA) - At<int32_t>(self, arrows::kB) + At<int32_t>(self, arrows::kStripWidth);
    const int32_t minHeight = At<int32_t>(self, arrows::kMinHeight);
    const int32_t inner = height < minHeight ? minHeight : height;
    const int32_t whole = inner + At<int32_t>(self, arrows::kArrowHeight) * 2;
    const int32_t left = x - At<int32_t>(self, arrows::kC);
    const int32_t line = y - At<int32_t>(self, arrows::kD);
    int32_t top = Half(minHeight) - Half(whole) + line;
    if (top < upper)
        top = upper;
    if (top > lower - whole)
        top = lower - whole;
    if (top > line - At<int32_t>(self, arrows::kArrowHeight))
        top = line - At<int32_t>(self, arrows::kArrowHeight);
    const int32_t least = minHeight - whole + At<int32_t>(self, arrows::kArrowHeight) + line;
    if (top < least)
        top = least;
    Virtual<void>(self + kWindow, 0xDC, left, top, left + right, whole + top);
    right -= Half(At<int32_t>(self, arrows::kStripWidth) + width);
    --right;
    At<int32_t>(self, arrows::kPointerLeft) = right;
    const int32_t pointerTop = Half(whole - height);
    At<int32_t>(self, arrows::kPointerTop) = pointerTop;
    At<int32_t>(self, arrows::kPointerBottom) = pointerTop + height;
    At<int32_t>(self, arrows::kPointerRight) = right + width;
    At<int32_t>(self, arrows::kPointerAt) = line - top;
    At<uint32_t>(self, arrows::kFlags) |= 1;
}

// The game's 0x0079AE30 (slot 121 of the window part): whether a point of
// the window is on the strip.
uint8_t __fastcall ArrowsHit(char* part, void*, int32_t x, int32_t y)
{
    char* const self = part - kWindow;
    const int32_t width = At<int32_t>(part, win::kRight) - At<int32_t>(part, win::kLeft);
    if (x < 0 || y < 0 || x >= width || y >= At<int32_t>(part, win::kBottom) - At<int32_t>(part, win::kTop))
        return 0;
    return x >= width - At<int32_t>(self, arrows::kStripWidth);
}

// The game's 0x0079AE90 (slot 138 of the window part): a mouse move. On
// the strip, above the pointer is the upper arrow and below it the lower.
uint8_t __fastcall ArrowsMouseMove(char* part, void*, int32_t x, int32_t y, uint32_t)
{
    char* const self = part - kWindow;
    const int32_t strip = At<int32_t>(part, win::kRight) - At<int32_t>(self, arrows::kStripWidth) -
                          At<int32_t>(part, win::kLeft);
    uint32_t hover = 0;
    if (x >= strip) {
        if (y < At<int32_t>(self, arrows::kPointerTop))
            hover = 1;
        else if (y > At<int32_t>(self, arrows::kPointerBottom))
            hover = 2;
    }
    if (At<uint32_t>(self, arrows::kHover) == hover)
        return 1;
    At<uint32_t>(self, arrows::kHover) = hover;
    ArrowsChanged(self);
    return 1;
}

// The game's 0x0079AF40 (slot 134 of the window part): a mouse press on
// an arrow. One step at once, the timer started with the first wait, the
// mouse kept.
uint8_t __fastcall ArrowsMouseDown(char* part, void*, int32_t, int32_t, uint32_t)
{
    char* const self = part - kWindow;
    const uint32_t hover = At<uint32_t>(self, arrows::kHover);
    if (!hover)
        return 1;
    At<uint32_t>(self, arrows::kPressed) = hover;
    ArrowsChanged(self);
    ArrowsSendStep(self);
    char* const timer = self + arrows::kTimer;
    Virtual<void>(timer, 0x24, addr::SC4UI_F79A230, self);
    void* const target = Virtual<void*>(self, 0xC);
    Virtual<void>(timer, 0x14, Global<uint32_t>(addr::SC4MenuArrows_FirstTime), target);
    This<void>(addr::SC4Win_F99BDFE, part);
    return 1;
}

// The game's 0x0079AFF0: the constructor. The numbers of the frame are
// left unset.
char* __fastcall ArrowsConstruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4MenuArrows_BaseVtable;
    This<void*>(addr::GZWin_Construct, self + kWindow);
    At<std::uintptr_t>(self, 0x0) = addr::SC4MenuArrows_Vtable;
    At<std::uintptr_t>(self, kWindow) = addr::SC4MenuArrows_WindowVtable;
    At<void*>(self, arrows::kPicture) = nullptr;
    At<void*>(self, arrows::kStrip) = nullptr;
    At<uint32_t>(self, arrows::kFlags) = 0;
    At<uint8_t>(self, arrows::kUpEnabled) = 0;
    At<uint8_t>(self, arrows::kDownEnabled) = 0;
    At<uint32_t>(self, arrows::kHover) = 0;
    At<uint32_t>(self, arrows::kPressed) = 0;
    This<void*>(addr::cRZTimer_Construct, self + arrows::kTimer);
    return self;
}

// The game's 0x0079B050 (slot 6): the pointer's rectangle.
int32_t* __fastcall ArrowsPointer(char* self, void*, int32_t* out)
{
    out[0] = At<int32_t>(self, arrows::kPointerLeft);
    out[1] = At<int32_t>(self, arrows::kPointerTop);
    out[2] = At<int32_t>(self, arrows::kPointerRight);
    out[3] = At<int32_t>(self, arrows::kPointerBottom);
    return out;
}

// The game's 0x0079B080 (slot 7): which arrows can be used.
void __fastcall ArrowsSetEnabled(char* self, void*, uint8_t up, uint8_t down)
{
    if (At<uint8_t>(self, arrows::kUpEnabled) == up && At<uint8_t>(self, arrows::kDownEnabled) == down)
        return;
    At<uint8_t>(self, arrows::kUpEnabled) = up;
    At<uint32_t>(self, arrows::kFlags) |= 1;
    At<uint8_t>(self, arrows::kDownEnabled) = down;
    Virtual<void>(self + kWindow, 0x170);
}

// The game's 0x0079B490 (reached through the window part's slot 148): the
// deleting destructor, with the destructor inlined.
char* __fastcall ArrowsDelete(char* self, void*, uint32_t flags)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4MenuArrows_Vtable;
    At<std::uintptr_t>(self, kWindow) = addr::SC4MenuArrows_WindowVtable;
    ArrowsShutdown(self + kWindow, nullptr);
    This<void>(addr::cRZTimer_Destruct, self + arrows::kTimer);
    Drop(At<void*>(self, arrows::kStrip));
    Drop(At<void*>(self, arrows::kPicture));
    This<void>(addr::GZWin_Destruct, self + kWindow);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// The game's 0x0079B0C0 (slot 0 of the window part).
uint8_t __fastcall ArrowsQueryThunk(char* part, void*, uint32_t iid, void** out)
{
    return ArrowsQueryInterface(part - kWindow, nullptr, iid, out);
}

// The game's 0x0079B0D0 (slot 148 of the window part).
char* __fastcall ArrowsDeleteThunk(char* part, void*, uint32_t flags)
{
    return ArrowsDelete(part - kWindow, nullptr, flags);
}

// --- The control that follows (0x0079BBE0) --------------------------------------------

// The game's 0x0079BAB0 (slot 0): the message filter interface is the
// part at +0x10C; the rest is the base control's (0x009C218F).
uint8_t __fastcall SounderQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    if (iid == kFilterIid) {
        *out = self ? self + sounder::kFilter : nullptr;
        Virtual<uint32_t>(self, 0x4);
        return 1;
    }
    return This<uint8_t>(addr::SC4Win_F9C218F, self, iid, out);
}

// The game's 0x0079BAF0 (slot 5): Shutdown. Lets the reference at +0xE8
// go, takes its message filter out of the host's window (slot 0x1D4 of
// what the host's slot 0x14 gives), clears the window's flag 1 and goes on
// with the base control's Shutdown (0x009C229F).
uint8_t __fastcall SounderShutdown(char* self, void*)
{
    if (!This<uint8_t>(addr::GZWin_IsInitialized, self))
        return 1;
    if (void* const held = At<void*>(self, sounder::kHeld)) {
        Release(held);
        At<void*>(self, sounder::kHeld) = nullptr;
    }
    if (void* const host = At<void*>(self, sounder::kHost)) {
        Virtual<void>(Virtual<void*>(host, 0x14), 0x1D4, static_cast<void*>(self + sounder::kFilter));
        Release(At<void*>(self, sounder::kHost));
        At<void*>(self, sounder::kHost) = nullptr;
    }
    Virtual<void>(self, 0x110, uint32_t{1}, uint32_t{0});
    return This<uint8_t>(addr::SC4Win_F9C229F, self);
}

// The game's 0x0079BB70: plays the control's first or second sound. The
// sound service makes a sound of type 0x5B523BA4 from the key and is given
// it to play.
uint8_t __fastcall SounderPlay(char* self, void*, uint32_t which)
{
    if (!At<uint32_t>(self, sounder::kSounds + which * 4))
        return 0;
    void* const sounds = Cdecl<void*>(addr::SC4_Singleton913C9E);
    if (!sounds)
        return 0;
    void* made = reinterpret_cast<void*>(static_cast<std::uintptr_t>(which));
    const uint32_t key = At<uint32_t>(self, sounder::kSounds + which * 4);
    if (!Virtual<uint8_t>(sounds, 0x34, uint32_t{1}, kSoundType, &made, key, uint32_t{0}, uint32_t{0}, uint32_t{0}))
        return 0;
    Virtual<void>(sounds, 0x20, made);
    Release(made);
    return 1;
}

bool InstallPanels6(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[64] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4InfoTip_Construct, reinterpret_cast<void*>(&InfoTipConstruct)},
        {addr::SC4UI_F799F30, reinterpret_cast<void*>(&InfoTipQueryThunk)},
        {addr::SC4UI_F799F40, reinterpret_cast<void*>(&InfoTipDeleteThunk)},
        {addr::SC4UI_F799F50, reinterpret_cast<void*>(&InfoTipDestruct)},
        {addr::SC4UI_F79A020, reinterpret_cast<void*>(&InfoTipDelete)},
        {addr::SC4UI_F79A040, reinterpret_cast<void*>(&ItemQueryInterface)},
        {addr::SC4UI_F79A070, reinterpret_cast<void*>(&ItemAddRef)},
        {addr::SC4UI_F79A080, reinterpret_cast<void*>(&MenuQueryInterface)},
        {addr::SC4UI_F79A0B0, reinterpret_cast<void*>(&MenuTimerTick)},
        {addr::SC4UI_F79A0E0, reinterpret_cast<void*>(&MenuSetMetrics)},
        {addr::SC4UI_F79A110, reinterpret_cast<void*>(&ArrowsQueryInterface)},
        {addr::SC4UI_F79A140, reinterpret_cast<void*>(&ArrowsInit)},
        {addr::SC4UI_F79A180, reinterpret_cast<void*>(&ArrowsHitScreen)},
        {addr::SC4UI_F79A1B0, reinterpret_cast<void*>(&ArrowsMouseLeave)},
        {addr::SC4UI_F79A1E0, reinterpret_cast<void*>(&ArrowsMouseUp)},
        {addr::SC4UI_F79A230, reinterpret_cast<void*>(&ArrowsTimerTick)},
        {addr::SC4UI_F79A280, reinterpret_cast<void*>(&MenuInit)},
        {addr::SC4UI_F79A2E0, reinterpret_cast<void*>(&MenuShutdown)},
        {addr::SC4UI_F79A380, reinterpret_cast<void*>(&MenuSetTip)},
        {addr::SC4UI_F79A3F0, reinterpret_cast<void*>(&MenuFindItem)},
        {addr::SC4UI_F79A430, reinterpret_cast<void*>(&MenuEnableOthers)},
        {addr::SC4UI_F79A480, reinterpret_cast<void*>(&MenuRowPlace)},
        {addr::SC4UI_F79A510, reinterpret_cast<void*>(&MenuStartTimer)},
        {addr::SC4UI_F79A5E0, reinterpret_cast<void*>(&MenuHoverItem)},
        {addr::SC4UI_F79A600, reinterpret_cast<void*>(&MenuChosenItem)},
        {addr::SC4UI_F79A620, reinterpret_cast<void*>(&MenuSizeFor)},
        {addr::SC4UI_F79A660, reinterpret_cast<void*>(&MenuSetFirst)},
        {addr::SC4UI_F79A710, reinterpret_cast<void*>(&MenuScroll)},
        {addr::SC4UI_F79A760, reinterpret_cast<void*>(&MenuMessage)},
        {addr::SC4UI_F79A7D0, reinterpret_cast<void*>(&MenuSetFont)},
        {addr::SC4UI_F79A820, reinterpret_cast<void*>(&MenuChosenId)},
        {addr::SC4UI_F79A850, reinterpret_cast<void*>(&MenuChooseId)},
        {addr::SC4UI_F79A8A0, reinterpret_cast<void*>(&MenuShowItem)},
        {addr::SC4UI_F79A940, reinterpret_cast<void*>(&MenuMouseDown)},
        {addr::SC4UI_F79A9F0, reinterpret_cast<void*>(&MenuMouseWheel)},
        {addr::SC4UI_F79AC00, reinterpret_cast<void*>(&ArrowsShutdown)},
        {addr::SC4UI_F79AC60, reinterpret_cast<void*>(&ArrowsSetup)},
        {addr::SC4UI_F79AD00, reinterpret_cast<void*>(&ArrowsPlace)},
        {addr::SC4UI_F79AE30, reinterpret_cast<void*>(&ArrowsHit)},
        {addr::SC4UI_F79AE90, reinterpret_cast<void*>(&ArrowsMouseMove)},
        {addr::SC4UI_F79AF40, reinterpret_cast<void*>(&ArrowsMouseDown)},
        {addr::SC4UI_F79AFF0, reinterpret_cast<void*>(&ArrowsConstruct)},
        {addr::SC4UI_F79B050, reinterpret_cast<void*>(&ArrowsPointer)},
        {addr::SC4UI_F79B080, reinterpret_cast<void*>(&ArrowsSetEnabled)},
        {addr::SC4UI_F79B0C0, reinterpret_cast<void*>(&ArrowsQueryThunk)},
        {addr::SC4UI_F79B0D0, reinterpret_cast<void*>(&ArrowsDeleteThunk)},
        {addr::SC4UI_F79B490, reinterpret_cast<void*>(&ArrowsDelete)},
        {addr::SC4UI_F79B500, reinterpret_cast<void*>(&MenuConstruct)},
        {addr::SC4UI_F79B590, reinterpret_cast<void*>(&MenuQueryThunk)},
        {addr::SC4UI_F79B5A0, reinterpret_cast<void*>(&MenuDeleteThunk)},
        {addr::SC4UI_F79B5B0, reinterpret_cast<void*>(&MenuDestruct)},
        {addr::SC4UI_F79B620, reinterpret_cast<void*>(&MenuDelete)},
        {addr::SC4UI_F79B640, reinterpret_cast<void*>(&MenuClear)},
        {addr::SC4UI_F79B6B0, reinterpret_cast<void*>(&ItemConstruct)},
        {addr::SC4UI_F79B740, reinterpret_cast<void*>(&ItemSetEnabled)},
        {addr::SC4UI_F79B750, reinterpret_cast<void*>(&ItemDestruct)},
        {addr::SC4UI_F79B7B0, reinterpret_cast<void*>(&MenuAdd)},
        {addr::SC4UI_F79B820, reinterpret_cast<void*>(&ItemRelease)},
        {addr::SC4UI_F79B850, reinterpret_cast<void*>(&MenuSetHover)},
        {addr::SC4UI_F79BA40, reinterpret_cast<void*>(&MenuMouseMove)},
        {addr::SC4UI_F79BAA0, reinterpret_cast<void*>(&MenuMouseLeave)},
        {addr::SC4UI_F79BAB0, reinterpret_cast<void*>(&SounderQueryInterface)},
        {addr::SC4UI_F79BAF0, reinterpret_cast<void*>(&SounderShutdown)},
        {addr::SC4UI_F79BB70, reinterpret_cast<void*>(&SounderPlay)},
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

const bool g_panels6Registered = registry::Register({
    "ui.panels.part6",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4InfoTip_Construct,
    "Our pop-up menu, its items and arrows, and the info tip's constructor and destructors (docs/ui.md)",
    false,
    InstallPanels6,
});

} // namespace
} // namespace opensc4::ui
