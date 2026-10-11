// The region screen window (cSC4WinRegionScreen, main vtable 0x00AB92A0),
// sixth part, 0x007AFAA0 to 0x007B0AF0: two functions.
//
//   - 0x007AFAA0, slot 127 of the main vtable: what the screen does when one
//     of its controls is used (message type 0x287259F6 with the control's ID;
//     0x287259F7 for the one check box that reports a change that way): the
//     region menu (open, new, delete region), the city menu (open, import or
//     move, delete city), the options (names, borders, the three map modes),
//     the two halves of the bottom bar and the dialogs behind the top bar's
//     buttons;
//   - 0x007B0470, called once by the screen's region loader (0x007B1900)
//     when the window's layout is in: loads the two side panels, places them,
//     makes the screen the target of its buttons and sets the option
//     controls from the saved preferences.
//
// Replacement, off by default:
//
//   region.screen.part6: both.
//
// Windows, controls, dialogs, the region map window (at +0xE0) and the
// application's region services are the game's, called through their
// vtables or by address.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace opensc4::region {
namespace {

namespace screen {
constexpr std::size_t kParent = 0x48;      // a window the map dialogs are opened under
constexpr std::size_t kMap = 0xE0;         // the region map window (cSC4WinRegionView)
constexpr std::size_t kModeButtons = 0xEC; // three counted windows
constexpr std::size_t kModeTwins = 0xF8;   // three more, the windows with the next ID
constexpr std::size_t kRegion = 0x1A4;     // what the regions service knows the open region by
constexpr std::size_t kBorders = 0x1A8;    // dword, 0 or 1: the second option
constexpr std::size_t kBarSide = 0x1FD;    // byte: which half of the bottom bar is out
} // namespace screen

namespace prefs { // the application's preferences (application slot 0x98)
constexpr std::size_t kNames = 0xF04;   // byte: the first option
constexpr std::size_t kBorders = 0xF05; // byte
constexpr std::size_t kMode = 0xF06;    // byte, 0 to 2
} // namespace prefs

namespace tile { // what the map window's +0xE4 points at: the tile picked on the map
constexpr std::size_t kX = 0x08;
constexpr std::size_t kY = 0x0C;
constexpr std::size_t kSize = 0x18; // byte, 0 to 2: log2 of the side in km
} // namespace tile

constexpr uint32_t kControlUsed = 0x287259F6;
constexpr uint32_t kControlChanged = 0x287259F7;
constexpr uint32_t kButtonIid = 0x8810;
constexpr uint32_t kTargetIid = 0x22BA0121; // also the ID the target callback is registered under
constexpr uint32_t kTargetCallback = static_cast<uint32_t>(addr::cSC4WinRegionScreen_ButtonTargetCallback); // cdecl, the game's
constexpr uint32_t kUiGroup = 0x96A006B0;
constexpr uint32_t kTextGroup = 0x6A231EAA;

constexpr uint32_t kNamesBox = 0xEA5A96E6;
constexpr uint32_t kBordersBox = 0xCA5CFEE2;
constexpr uint32_t kBarLeft = 0x0BB0F5E7;
constexpr uint32_t kBarRight = 0x6BB92BCA;
constexpr uint32_t kLeftPanel = 0x09EBE9EE;
constexpr uint32_t kRegionName = 0xEA5BD179;

// The game's string.
struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};
static_assert(sizeof(GameString) == 0x14);

// A text from the game's text resources: a string first, 0x20 bytes.
struct LocalizedString {
    GameString string;
    uint32_t spare[3];
};
static_assert(sizeof(LocalizedString) == 0x20);

// A set of windows as the game's window helpers fill it on the caller's
// stack. The original frame leaves 0xC bytes for it before the next local.
struct Tree {
    char* header;
    uint32_t count;
    uint32_t spare[3];
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

void AddRef(void* object)
{
    Virtual<uint32_t>(object, 0x4);
}

void Release(void* object)
{
    Virtual<uint32_t>(object, 0x8);
}

void* App()
{
    return Global<void*>(addr::SC4_Service43C94);
}

// The three map mode buttons (0x00AB8B70) and the question texts of the
// import dialog by tile size (0x00AB9230).
const uint32_t* ModeButtons()
{
    return reinterpret_cast<const uint32_t*>(addr::SC4RegionScreen_ModeButtonIds);
}

const uint32_t* ImportQuestions()
{
    return reinterpret_cast<const uint32_t*>(addr::SC4RegionScreen_ImportQuestionIds);
}

// The child `id` of the screen as a button (slot 0x94), into a holder.
uint8_t ChildButton(char* self, uint32_t id, void** holder)
{
    return Virtual<uint8_t>(self, 0x94, id, kButtonIid, holder);
}

void DestroyString(void* string)
{
    This<void>(addr::SC4String_Destroy, string);
}

void ShowChild(char* self, uint32_t id, uint32_t show)
{
    Cdecl<uint8_t>(addr::SC4Win_ShowChild, static_cast<void*>(self), id, show, uint32_t{1});
}

// The two halves of the bottom bar, shown by +0x1FD and by whether a map
// mode other than the first is on.
void ShowBarHalves(char* self, bool otherMode)
{
    ShowChild(self, kBarLeft, At<uint8_t>(self, screen::kBarSide) == 0 && otherMode ? 1u : 0u);
    ShowChild(self, kBarRight, At<uint8_t>(self, screen::kBarSide) != 0 && otherMode ? 1u : 0u);
}

// The region picked in one of the two region dialogs becomes the open one.
void SwitchRegion(char* self, int32_t picked)
{
    if (picked < 0)
        return;
    void* const app = App();
    void* const regions = Virtual<void*>(app, 0x88);
    Virtual<void>(regions, 0x24, picked);
    This<void>(addr::cSC4WinRegionScreen_SaveRegionChoice, self);
    Virtual<void>(app, 0x30, uint32_t{0});
}

// A text shown through 0x005FA810.
void ShowText(uint32_t instance)
{
    LocalizedString text;
    This<void*>(addr::SC4LocalizedString_SC4LocalizedString, &text, kTextGroup, instance, uint32_t{1});
    Cdecl<void>(addr::SC4UI_F5FA810, &text, uint32_t{1});
    DestroyString(&text);
}

// Flips the bottom bar to one half.
void SetBarSide(char* self, uint8_t side)
{
    At<uint8_t>(self, screen::kBarSide) = side;
    ShowChild(self, kBarLeft, side ? 0u : 1u);
    ShowChild(self, kBarRight, uint32_t{At<uint8_t>(self, screen::kBarSide)});
}

// Control 0x4A560002: a city brought to the picked tile, from another region
// or from a file. A tile that has a city asks first.
void ImportCity(char* self)
{
    char* const picked = This<char*>(addr::SC4Folded_GetFieldE4, At<void*>(self, screen::kMap));
    GameString path;
    This<void*>(addr::SC4String_SC4String, &path);
    if (This<void*>(addr::cSC4WinRegionScreen_CityAtTile, self, picked, uint32_t{0})) {
        LocalizedString title;
        LocalizedString text;
        LocalizedString button;
        This<void*>(addr::SC4LocalizedString_SC4LocalizedString, &title, kTextGroup,
                    ImportQuestions()[At<uint8_t>(picked, tile::kSize)], uint32_t{1});
        This<void*>(addr::SC4LocalizedString_SC4LocalizedString, &text, uint32_t{0x0A554AE8}, uint32_t{0x4AA59E23},
                    uint32_t{1});
        This<void*>(addr::SC4LocalizedString_SC4LocalizedString, &button, kTextGroup, uint32_t{0xEA8DFC1B},
                    uint32_t{1});
        const uint8_t agreed = Cdecl<uint8_t>(addr::SC4UI_QuestionDialog, &title, &text, &button, uint32_t{0});
        DestroyString(&button);
        DestroyString(&text);
        DestroyString(&title);
        if (!agreed) {
            DestroyString(&path);
            return;
        }
    }

    void* const app = App();
    void* const regions = Virtual<void*>(app, 0x88);
    void* const current = Virtual<void*>(regions, 0x20);
    const uint32_t side = 1u << (At<uint8_t>(picked, tile::kSize) & 0x1F);
    uint32_t fromRegion = 0;
    int32_t fromX = 0;
    int32_t fromY = 0;
    if (Cdecl<uint8_t>(addr::SC4UI_F778D80, static_cast<void*>(self), side, side, &fromRegion, &fromX, &fromY,
                       &path)) {
        if (path.begin == path.end) {
            void* const other = Virtual<void*>(regions, 0x2C, fromRegion);
            if (other != current)
                Virtual<void>(other, 0x20);
            void** const holder = Virtual<void**>(other, 0x2C, fromX, fromY);
            void* const city = *holder;
            if (Virtual<uint8_t>(current, 0x40, other, city, At<uint32_t>(picked, tile::kX),
                                 At<uint32_t>(picked, tile::kY))) {
                This<void>(addr::cSC4WinRegionScreen_SaveViewPosition, self);
                Virtual<void>(App(), 0x30, uint32_t{0});
            }
        } else {
            void* city = nullptr;
            void** const out = This<void**>(addr::SC4RefPtr_Reset, &city);
            if (Cdecl<uint8_t>(addr::SC4RegionalCity_LoadFromFile, &path, out)) {
                Virtual<void>(city, 0x80, &path);
                const uint8_t moved = Virtual<uint8_t>(current, 0x40, static_cast<void*>(nullptr), city,
                                                       At<uint32_t>(picked, tile::kX),
                                                       At<uint32_t>(picked, tile::kY));
                Virtual<void>(city, 0x10);
                if (void* const held = city) {
                    city = nullptr;
                    Release(held);
                }
                if (moved) {
                    This<void>(addr::cSC4WinRegionScreen_SaveViewPosition, self);
                    Virtual<void>(App(), 0x30, uint32_t{0});
                }
            }
            if (city)
                Release(city);
        }
    }
    DestroyString(&path);
}

// The game's 0x007AFAA0, slot 127 of the main vtable (thiscall, ret 8).
// `this` is the screen. Always answers true.
uint8_t __fastcall OnControl(char* self, void*, uint32_t type, uint32_t id)
{
    if (type == kControlChanged) {
        if (id != kNamesBox)
            return 1;
        void* box = nullptr;
        if (ChildButton(self, kNamesBox, &box)) {
            const uint8_t on = Virtual<uint8_t>(box, 0x28);
            This<void>(addr::cSC4WinRegionView_SetNames, At<void*>(self, screen::kMap), uint32_t{on});
            At<uint8_t>(Virtual<char*>(App(), 0x98), prefs::kNames) = on;
        }
        if (box)
            Release(box);
        return 1;
    }
    if (type != kControlUsed)
        return 1;

    switch (id) {
    case 0x09EBF2BD:
        This<void>(addr::cSC4WinRegionScreen_SelectPanel, self, uint32_t{0});
        break;
    case 0x09EBF2C8:
        This<void>(addr::cSC4WinRegionScreen_SelectPanel, self, uint32_t{1});
        break;
    case 0x09EBF2C3:
        This<void>(addr::cSC4WinRegionScreen_SelectPanel, self, uint32_t{2});
        break;

    case 0x098F4F6C: {
        void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x20});
        void* const made = memory ? This<void*>(addr::SC4UI_F791360, memory) : nullptr;
        void* dialog = nullptr;
        This<void**>(addr::SC4RefHolder_SC4RefHolder, &dialog, made);
        Virtual<uint8_t>(dialog, 0x14, uint32_t{0});
        This<void>(addr::SC4UI_F7913A0, dialog);
        Release(dialog);
        break;
    }
    case 0x0A5510A9: {
        void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x10});
        void* const made = memory ? This<void*>(addr::SC4UI_F76AB30, memory) : nullptr;
        void* dialog = nullptr;
        This<void**>(addr::SC4RefHolder_SC4RefHolder, &dialog, made);
        Virtual<uint8_t>(dialog, 0x14, uint32_t{0});
        This<void>(addr::SC4UI_F76AB50, dialog);
        Release(dialog);
        break;
    }
    case 0xA98F4F88: {
        void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x80});
        void* const made = memory ? This<void*>(addr::SC4UI_F4F2450, memory) : nullptr;
        void* dialog = nullptr;
        This<void**>(addr::SC4RefHolder_SC4RefHolder, &dialog, made);
        Virtual<uint8_t>(dialog, 0x14);
        This<void>(addr::SC4UI_F4F25F0, dialog);
        Release(dialog);
        break;
    }

    case 0x26C10A3E: {
        This<void>(addr::cSC4WinRegionScreen_SaveRegionChoice, self);
        This<void>(addr::cSC4WinRegionScreen_SaveViewPosition, self);
        Virtual<void>(App(), 0x2C, uint32_t{1});
        void* button = nullptr;
        void** const out = This<void**>(addr::SC4RefPtr_Reset, &button);
        if (ChildButton(self, 0x26C10A3E, out))
            Virtual<void>(button, 0x24, uint32_t{0});
        if (button)
            Release(button);
        break;
    }

    case 0x2A5B0000:
        SwitchRegion(self, Cdecl<int32_t>(addr::SC4UI_F777FF0, static_cast<void*>(self)));
        break;
    case 0x2A5B0001:
        SwitchRegion(self, Cdecl<int32_t>(addr::SC4UI_F7780C0, static_cast<void*>(self)));
        break;
    case 0x2A5B0002: {
        if (!Cdecl<uint8_t>(addr::SC4UI_F778130, static_cast<void*>(self)))
            break;
        void* const app = App();
        void* const regions = Virtual<void*>(app, 0x88);
        Virtual<void>(regions, 0x30, At<uint32_t>(self, screen::kRegion));
        This<void>(addr::cSC4WinRegionScreen_SaveRegionChoice, self);
        Virtual<void>(app, 0x30, uint32_t{0});
        break;
    }

    case 0x4A560000: {
        void* const picked = This<void*>(addr::SC4Folded_GetFieldE4, At<void*>(self, screen::kMap));
        void* const regions = Virtual<void*>(App(), 0x88);
        Virtual<void*>(regions, 0x2C, At<uint32_t>(self, screen::kRegion));
        AddRef(self);
        if (void* const holder = This<void*>(addr::cSC4WinRegionScreen_CityAtTile, self, picked, uint32_t{1})) {
            if (!This<uint8_t>(addr::cSC4WinRegionScreen_OpenCity, self, holder))
                This<void>(addr::cSC4WinRegionScreen_ApplyMode, self);
        }
        Release(self);
        break;
    }
    case 0x4A560001:
        This<void>(addr::cSC4WinRegionScreen_ApplyMode, self);
        break;
    case 0x4A560002:
        ImportCity(self);
        break;
    case 0x4A560003: {
        if (!Cdecl<uint8_t>(addr::SC4UI_F778220, static_cast<void*>(self)))
            break;
        void* const picked = This<void*>(addr::SC4Folded_GetFieldE4, At<void*>(self, screen::kMap));
        void* const regions = Virtual<void*>(App(), 0x88);
        void* const region = Virtual<void*>(regions, 0x2C, At<uint32_t>(self, screen::kRegion));
        void* holder = This<void*>(addr::cSC4WinRegionScreen_CityAtTile, self, picked, uint32_t{1});
        Virtual<void>(region, 0x38, &holder);
        This<void>(addr::cSC4WinRegionScreen_GiveStockPictures, self, picked);
        This<void>(addr::cSC4WinRegionScreen_PrepareTilePictures, self, picked);
        This<void>(addr::cSC4WinRegionView_PlaceMarkers, At<void*>(self, screen::kMap), picked);
        This<void>(addr::cSC4WinRegionScreen_ApplyMode, self);
        This<void>(addr::cSC4WinRegionScreen_ShowPopulation, self);
        break;
    }

    case 0x4A779A1A:
        ShowText(0x4A779D9D);
        break;
    case 0x4A779A09:
        ShowText(0x4A77A0B1);
        break;

    case 0x4BB92C1F:
        SetBarSide(self, 0);
        break;
    case 0xEBB91356:
        SetBarSide(self, 1);
        break;

    case 0x8A1DA655: {
        void* const manager = Cdecl<void*>(addr::SC4_GetWinManager);
        Virtual<void>(manager, 0xC);
        Cdecl<uint8_t>(addr::SC4RegionView_F7B7530, At<void*>(self, screen::kParent), uint32_t{1});
        break;
    }
    case 0xCA1DA670: {
        void* const window = Cdecl<void*>(addr::SC4UI_F7BDD60, uint32_t{1}, uint32_t{1},
                                          At<void*>(self, screen::kParent), uint32_t{0});
        This<void>(addr::SC4UI_F7BDC90, window, uint32_t{0});
        This<void>(addr::SC4RegionView_F7B69E0, window);
        if (window)
            Release(window);
        break;
    }

    case kBordersBox: {
        void* box = nullptr;
        void** const out = This<void**>(addr::SC4RefPtr_Reset, &box);
        if (ChildButton(self, kBordersBox, out)) {
            At<uint32_t>(self, screen::kBorders) = Virtual<uint8_t>(box, 0x28);
            This<void>(addr::cSC4WinRegionScreen_ApplyMode, self);
            char* const preferences = Virtual<char*>(App(), 0x98);
            At<uint8_t>(preferences, prefs::kBorders) = At<uint8_t>(self, screen::kBorders);
        }
        if (box)
            Release(box);
        break;
    }

    case 0xABA290E1:
    case 0xCBA290EC:
    case 0xABA290F6: {
        void* button = nullptr;
        for (uint32_t i = 0; i < 3; ++i) {
            const uint8_t on = id == ModeButtons()[i] ? 1 : 0;
            if (on) {
                This<void>(addr::cSC4WinRegionView_SetMode, At<void*>(self, screen::kMap), i);
                At<uint8_t>(Virtual<char*>(App(), 0x98), prefs::kMode) = static_cast<uint8_t>(i);
            }
            if (void* const held = button) {
                button = nullptr;
                Release(held);
            }
            if (ChildButton(self, ModeButtons()[i], &button))
                Virtual<void>(button, 0x24, uint32_t{on});
        }
        ShowBarHalves(self, id != 0xABA290E1);
        if (button)
            Release(button);
        break;
    }

    default:
        break;
    }
    return 1;
}

// One of the two side panels, its position set and the screen made its
// target; `second` also brings it to the front (slot 0x118).
void PlacePanel(char* self, void* panel, uint32_t x, uint32_t y, bool second)
{
    Virtual<void>(panel, 0xE0, x, y);
    Virtual<void>(panel, 0x5C);
    Virtual<void>(panel, 0x80, kTargetIid, kTargetCallback, static_cast<void*>(self));
    Virtual<void>(panel, 0x110, uint32_t{0x800}, uint32_t{0});
    if (second)
        Virtual<void>(panel, 0x118);
}

// A counted window kept in one of the screen's holders.
void Keep(void*& holder, void* window)
{
    void* const old = holder;
    if (window != old) {
        AddRef(window);
        holder = window;
        if (old)
            Release(old);
    }
}

// The game's 0x007B0470 (thiscall, no arguments). `this` is the screen.
void __fastcall SetUpControls(char* self, void*)
{
    Tree before;
    before.header = nullptr;
    before.header = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{0x14});
    before.count = 0;
    At<uint8_t>(before.header, 0x0) = 0;
    At<char*>(before.header, 0x4) = nullptr;
    At<char*>(before.header, 0x8) = before.header;
    At<char*>(before.header, 0xC) = before.header;
    Cdecl<uint32_t>(addr::SC4UIWindows_Collect, static_cast<void*>(self), &before);

    uint32_t key[3] = {0, kUiGroup, 0xAA920991};
    void* panel = Cdecl<void*>(addr::SC4UI_LoadWindowChild, &key[0], static_cast<void*>(self), kLeftPanel);
    if (panel) {
        const uint32_t width = Virtual<uint32_t>(self, 0xA4);
        const uint32_t height = Virtual<uint32_t>(self, 0xA8);
        Virtual<uint32_t>(panel, 0xA4);
        const uint32_t panelHeight = Virtual<uint32_t>(panel, 0xA8);
        const uint32_t third = width > height * 3 ? width / 3 : 0;
        PlacePanel(self, panel, third + 5, height - panelHeight + 2, false);
        Release(panel);
    }
    Cdecl<void>(addr::SC4UIWindows_NewSince, static_cast<void*>(self), &before, &before);
    Cdecl<uint8_t>(addr::SC4UIWindows_Prepare, &before, static_cast<void*>(self));

    if (void* const button = Virtual<void*>(self, 0x8C, uint32_t{0x6A91DC16}))
        Virtual<void>(button, 0x80, kTargetIid, kTargetCallback, static_cast<void*>(self));
    if (void* const button = Virtual<void*>(self, 0x8C, uint32_t{0x6A91DC15}))
        Virtual<void>(button, 0x80, kTargetIid, kTargetCallback, static_cast<void*>(self));

    // The original leaves the ID as it was for an index outside 0 to 2, which
    // the loop never has.
    uint32_t modeId = 0;
    for (uint32_t i = 0; i < 3; ++i) {
        switch (i) {
        case 0:
            modeId = 0x09EBEE45;
            break;
        case 1:
            modeId = 0xEA54DF28;
            break;
        case 2:
            modeId = 0x09EBEE60;
            break;
        }
        void* const first = Virtual<void*>(self, 0x88, modeId);
        if (!first)
            continue;
        Keep(At<void*>(self, screen::kModeButtons + 4 * i), first);
        Virtual<void>(first, 0x118);
        Virtual<void>(first, 0x80, kTargetIid, kTargetCallback, static_cast<void*>(self));
        void* const twin = Virtual<void*>(self, 0x88, modeId + 1);
        if (!twin)
            continue;
        Keep(At<void*>(self, screen::kModeTwins + 4 * i), twin);
        Virtual<void>(twin, 0x118);
    }

    // Not checked for null, as in the original.
    void* const left = Virtual<void*>(self, 0x88, kLeftPanel);
    Virtual<void>(left, 0x110, uint32_t{0x800}, uint32_t{1});
    Virtual<void>(left, 0x5C);
    Virtual<void>(left, 0x110, uint32_t{0x800}, uint32_t{0});

    void* const regions = Virtual<void*>(App(), 0x88);
    if (Virtual<uint32_t>(regions, 0x18) <= 1) {
        void* const deleteRegion = Virtual<void*>(self, 0x8C, uint32_t{0x2A5B0002});
        Virtual<void>(deleteRegion, 0x110, uint32_t{2}, uint32_t{0});
    }

    // The dword the original keeps at [esp+0x14]: later pushed whole as a
    // check box state with only its low byte set. Stack leftovers when
    // neither of the two stores below runs; zero here.
    uint32_t scratch = 0;

    key[0] = 0;
    key[1] = kUiGroup;
    key[2] = 0xABC0ED33;
    panel = Cdecl<void*>(addr::SC4UI_LoadWindowChild, &key[0], static_cast<void*>(self), kBarLeft);
    if (panel) {
        scratch = At<uint32_t>(panel, 0x0);
        uint32_t y = uint32_t{0xFFFFFFFB} - Virtual<uint32_t>(panel, 0xA8);
        y += Virtual<uint32_t>(self, 0xA8);
        uint32_t x = uint32_t{0xFFFFFFFB} - Virtual<uint32_t>(panel, 0xA4);
        x += Virtual<uint32_t>(self, 0xA4);
        PlacePanel(self, panel, x, y, true);
        Release(panel);
    }

    // `panel` is used after its release, and when it is null, as in the
    // original.
    void* other = nullptr;
    if (Cdecl<uint8_t>(addr::SC4Win_GetSiblingAs, panel, kBarRight, kTargetIid, &other)) {
        scratch = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(other));
        uint32_t y = uint32_t{0xFFFFFFF6} - Virtual<uint32_t>(other, 0xA8);
        y += Virtual<uint32_t>(self, 0xA8);
        uint32_t x = uint32_t{0xFFFFFFF6} - Virtual<uint32_t>(other, 0xA4);
        x += Virtual<uint32_t>(self, 0xA4);
        PlacePanel(self, other, x, y, true);
    }
    if (other)
        Release(other);

    This<void>(addr::cSC4WinRegionScreen_ShowPopulation, self);

    void* const region = Virtual<void*>(regions, 0x2C, At<uint32_t>(self, screen::kRegion));
    void* const nameText = Virtual<void*>(self, 0x8C, kRegionName);
    const uint32_t nameTextVtable = At<uint32_t>(nameText, 0x0);
    Virtual<void>(nameText, 0x128, Virtual<void*>(region, 0xC));

    char* const preferences = Virtual<char*>(App(), 0x98);
    const uint8_t names = At<uint8_t>(preferences, prefs::kNames) != 0 ? 1 : 0;
    const uint8_t borders = At<uint8_t>(preferences, prefs::kBorders) != 0 ? 1 : 0;
    // Both states travel as dwords whose upper bytes are what the slot held
    // before: the name text's vtable for the first.
    const uint32_t namesWord = (nameTextVtable & 0xFFFFFF00) | names;
    scratch = (scratch & 0xFFFFFF00) | borders;
    other = nullptr;
    At<uint32_t>(self, screen::kBorders) = borders;
    This<void>(addr::cSC4WinRegionView_SetNames, At<void*>(self, screen::kMap), namesWord);

    void* box = nullptr;
    if (ChildButton(self, kNamesBox, &box))
        Virtual<void>(box, 0x24, namesWord);
    if (void* const held = box) {
        box = nullptr;
        Release(held);
    }
    if (ChildButton(self, kBordersBox, &box))
        Virtual<void>(box, 0x24, scratch);

    const uint8_t saved = At<uint8_t>(preferences, prefs::kMode);
    const uint32_t mode = saved < 3 ? saved : 0;
    for (uint32_t i = 0; i < 3; ++i) {
        if (void* const held = box) {
            box = nullptr;
            Release(held);
        }
        const uint32_t buttonId = ModeButtons()[i];
        if (ChildButton(self, buttonId, &box))
            Virtual<void>(box, 0x24, (buttonId & 0xFFFFFF00) | (mode == i ? 1u : 0u));
    }
    This<void>(addr::cSC4WinRegionView_SetMode, At<void*>(self, screen::kMap), mode);
    ShowBarHalves(self, mode != 0);
    if (box)
        Release(box);

    This<void>(addr::cSC4WinRegionScreen_ApplyMode, self);

    if (before.count != 0) {
        char* node = At<char*>(before.header, 0x4);
        while (node) {
            This<void>(addr::SC4UI_F41D7F0, &before, At<char*>(node, 0xC));
            char* const next = At<char*>(node, 0x8);
            Cdecl<void>(addr::SC4_Free, static_cast<void*>(node));
            node = next;
        }
        At<char*>(before.header, 0x8) = before.header;
        At<char*>(before.header, 0x4) = nullptr;
        At<char*>(before.header, 0xC) = before.header;
        before.count = 0;
    }
    if (before.header)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(before.header));
}

bool InstallRegionScreen6(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[2] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSC4WinRegionScreen_OnControl, reinterpret_cast<void*>(&OnControl)},
        {addr::cSC4WinRegionScreen_SetUpControls, reinterpret_cast<void*>(&SetUpControls)},
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

const bool g_regionScreen6Registered = registry::Register({
    "region.screen.part6",
    "region",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4WinRegionScreen_OnControl,
    "Our region screen, part 6: what its controls do, and the set-up of its panels and option controls",
    false,
    InstallRegionScreen6,
});

} // namespace
} // namespace opensc4::region
