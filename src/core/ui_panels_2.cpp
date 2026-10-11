// The graphics options dialog (0x0078FF40 to 0x00792F08): a modal window
// with the choices of the settings' video part (the game's settings
// 0x00B43CD8 from +0xF0C on, SC4VideoPreferences in gzcom-dll).
//
// The dialog is 0x20 bytes, vtable 0x00AB55F8 (8 slots: QueryInterface
// 0x004F1F30, AddRef 0x004664A0, Release 0x00792F10, the window procedure
// 0x00792F50, 0x0041D4C0, Init 0x00791490, 0x0076AAA0 which forgets the
// settings, and 0x0078F550 which reads the window's buttons back into the
// settings), no class ID. Two places make one, call Init with key 0 and
// then Run: the region view's window on its button 0x098F4F6C (0x007AFBD5)
// and the city view on its command 0x6A935E2D (0x007F2C7A).
//
// Init loads the window (resource 0x8A7E052F of group 0x96A006B0, window
// 0x2A57CB82) at its parent's size, shows the settings in it and makes 35
// buttons report to the window. Run runs it modally. The window procedure
// hands the buttons' clicks to the command handler here: the groups of
// buttons where one is pressed at a time, Cancel, Accept, the second Accept
// of the page that says a restart is needed, and the button that asks the
// application for the choices it would make itself.
//
// Replacement, off by default:
//
//   ui.panels.part2: Close 0x0078FF40, ShowSettings 0x0078FFC0, the
//                    constructor 0x00791360, Run 0x007913A0, Init
//                    0x00791490 and the command handler 0x00791D70.
//
// Not functions: 0x00790FC0 is a label inside 0x0078FFC0 (the 1600 by 1200
// case), and 0x00792480, 0x00792BF0, 0x00792DF0 and 0x00792E80 are labels
// inside the command handler, which runs to 0x00792EC8 with its two jump
// tables after it (0x00792EC8, six entries; 0x00792EE0, ten).
//
// Still the game's: Release 0x00792F10, the window procedure 0x00792F50 and
// slot 7 (0x0078F550), which are other parts' and are called here through
// the vtable only; the check boxes and windows themselves.
//
// The dialog: +0 vtable (a window procedure), +4 the window's cIGZWinGen,
// +8 the game's settings, +0xC the reference count. From +0x10 on, what the
// settings said when the window was first filled, for slot 7 to compare
// with at Accept: +0x10 the cursor choice (word, 1 or 0), +0x12 the driver
// choice (word), +0x14 the colour depth (word), +0x16 the city detail less
// one (word), +0x18 the screen size's index (word, 0 to 3), +0x1A a byte
// slot 7 sets when the new choices need the second page, +0x1C a word that
// is 0 when translucency is on and 1 when not, +0x1E a byte: the window has
// not been filled yet.
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

namespace graphics { // the dialog
constexpr std::size_t kWindow = 0x04;       // the window's cIGZWinGen
constexpr std::size_t kSettings = 0x08;     // the game's settings (0x00B43CD8)
constexpr std::size_t kReferences = 0x0C;
constexpr std::size_t kWasCursor = 0x10;    // word
constexpr std::size_t kWasDriver = 0x12;    // word
constexpr std::size_t kWasDepth = 0x14;     // word
constexpr std::size_t kWasDetail = 0x16;    // word
constexpr std::size_t kWasSize = 0x18;      // word
constexpr std::size_t kSecondPage = 0x1A;   // byte, set by slot 7
constexpr std::size_t kWasOpaque = 0x1C;    // word
constexpr std::size_t kFirstFill = 0x1E;    // byte
} // namespace graphics

namespace video { // the settings' video part
constexpr std::size_t kStart = 0xF0C;
constexpr std::size_t kEffects = 0xF0D;      // 1 to 3
constexpr std::size_t kCarsAndSims = 0xF0E;  // 1 to 3
constexpr std::size_t kShadows = 0xF0F;      // 1 to 3
constexpr std::size_t kTextures = 0xF10;     // 1 to 3
constexpr std::size_t kAtmosphere = 0xF11;   // byte, 1 is on
constexpr std::size_t kWater = 0xF12;        // byte, 1 is on
constexpr std::size_t kDriver = 0xF13;       // 0 or 1
constexpr std::size_t kCursor = 0xF14;       // byte, 1 is on
constexpr std::size_t kTranslucency = 0xF15; // byte, 1 is on
constexpr std::size_t kDetail = 0xF16;       // 1 to 3
constexpr std::size_t kAutomata = 0xF18;     // byte, 1 is on
constexpr std::size_t kWidth = 0xF1C;
constexpr std::size_t kHeight = 0xF20;
constexpr std::size_t kDepth = 0xF24;        // 16 or 32
constexpr std::size_t kLoader = 0xF28;       // 1 to 3
} // namespace video

constexpr uint32_t kWinGenIid = 0x5386D516;
constexpr uint32_t kCheckIid = 0x8810;
constexpr uint32_t kUiGroup = 0x96A006B0;
constexpr uint32_t kSettingsChanged = 0x231BBF91;

constexpr uint32_t kCancel = 0x6A57DA48;
constexpr uint32_t kAccept = 0xEA57DA59;
constexpr uint32_t kAcceptAnyway = 0xEA57DA6F;
constexpr uint32_t kAutomatic = 0xEA5E99D9;
constexpr uint32_t kRestartPage = 0x2A57CB83; // child window: flag 1 off at Init, on when a restart is needed
constexpr uint32_t kChoicesPage = 0x2A57CB84; // child window: flag 1 on at Init, off when a restart is needed

constexpr uint32_t kFirstEffects = 0x6A57DA49;     // three buttons
constexpr uint32_t kFirstCarsAndSims = 0x6A57DA4C; // three
constexpr uint32_t kFirstShadows = 0x6A57DA4F;     // three
constexpr uint32_t kFirstDetail = 0x2AB6C373;      // three
constexpr uint32_t kFirstDepth = 0x2AB6C376;       // 16 bit, 32 bit
constexpr uint32_t kAtmosphere = 0x6A57DA54;
constexpr uint32_t kWater = 0x6A57DA55;
constexpr uint32_t kTranslucency = 0x6A57DA56;
constexpr uint32_t kFirstDriver = 0x6A57DA57;      // two
constexpr uint32_t kFirstSize = 0x6A57DA5A;        // four
constexpr uint32_t kCursor = 0x6A57DA60;
constexpr uint32_t kAutomata = 0x0A8B7E00;
constexpr uint32_t kFirstLoader = 0xAB81DED5;      // three

constexpr uint32_t kEffectsButtons[] = {0x6A57DA49, 0x6A57DA4A, 0x6A57DA4B};
constexpr uint32_t kCarsAndSimsButtons[] = {0x6A57DA4C, 0x6A57DA4D, 0x6A57DA4E};
constexpr uint32_t kShadowsButtons[] = {0x6A57DA4F, 0x6A57DA50, 0x6A57DA51};
constexpr uint32_t kDetailButtons[] = {0x2AB6C373, 0x2AB6C374, 0x2AB6C375};
constexpr uint32_t kDepthButtons[] = {0x2AB6C376, 0x2AB6C377};
constexpr uint32_t kDriverButtons[] = {0x6A57DA57, 0x6A57DA58};
constexpr uint32_t kSizeButtons[] = {0x6A57DA5A, 0x6A57DA5B, 0x6A57DA5C, 0x6A57DA5D};
constexpr uint32_t kLoaderButtons[] = {0xAB81DED5, 0xAB81DED6, 0xAB81DED7};
constexpr uint32_t kTextureButtons[] = {0x0B85CF92, 0x2B85CF9F, 0x2B85CFAA};

// The buttons Init makes report to the window, in its order.
constexpr uint32_t kReporting[] = {
    0xEA57DA6F, 0xEA57DA59, 0x6A57DA48, 0x6A57DA49, 0x6A57DA4A, 0x6A57DA4B, 0x6A57DA4C, 0x6A57DA4D, 0x6A57DA4E,
    0x6A57DA4F, 0x6A57DA50, 0x6A57DA51, 0x2AB6C373, 0x2AB6C374, 0x2AB6C375, 0x2AB6C376, 0x2AB6C377, 0x6A57DA54,
    0x6A57DA55, 0x6A57DA56, 0x6A57DA57, 0x6A57DA58, 0x6A57DA5A, 0x6A57DA5B, 0x6A57DA5C, 0x6A57DA5D, 0xEA5E99D9,
    0x6A57DA60, 0x0A8B7E00, 0xAB81DED5, 0xAB81DED6, 0xAB81DED7, 0x0B85CF92, 0x2B85CF9F, 0x2B85CFAA,
};
static_assert(sizeof(kReporting) / sizeof(kReporting[0]) == 35);

// One window layout of a dialog: the key Init is given, the resource and
// the window in it.
struct Layout {
    uint32_t key;
    uint32_t instance;
    uint32_t window;
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

// The game's inlined reference holder: empty it.
void Clear(void*& holder)
{
    if (void* const object = holder) {
        holder = nullptr;
        Release(object);
    }
}

// How the original empties the holder before it asks the window for a
// check box: inline, or with the game's helper 0x004F20A0.
enum class Emptied { Inline, ByHelper };

// A check box of the dialog's window pressed in (slot 6) or let out
// (slot 7). Returns whether the window has it.
bool SetCheck(char* self, void*& check, uint32_t id, bool on, Emptied how)
{
    void* const window = Virtual<void*>(At<void*>(self, graphics::kWindow), 0xC);
    void** out = &check;
    if (how == Emptied::ByHelper)
        out = This<void**>(addr::SC4RefPtr_Reset, &check);
    else
        Clear(check);
    if (!Virtual<uint8_t>(window, 0x94, id, kCheckIid, out))
        return false;
    if (on)
        Virtual<void>(check, 0x18);
    else
        Virtual<void>(check, 0x1C);
    return true;
}

// Three buttons in a row for a setting of 1 to 3: the one of the setting
// pressed in, the others let out. Any other value leaves them alone.
void ShowLevel(char* self, void*& check, uint32_t first, uint8_t level)
{
    if (level < 1 || level > 3)
        return;
    for (uint32_t i = 0; i < 3; ++i)
        SetCheck(self, check, first + i, uint32_t{level} == i + 1, Emptied::Inline);
}

void CheckChild(void* window, uint32_t id, bool on)
{
    Cdecl<uint8_t>(addr::SC4Win_CheckChild, window, id, uint32_t{on});
}

// The game's 0x0078FF40. `this` is the dialog. When the dialog is up: ends
// the modal state (with 1) when the window is modal, takes the window off
// its window manager, lets go of it and marks the dialog as down.
uint32_t __fastcall GraphicsClose(char* self, void*)
{
    if (!Global<uint8_t>(addr::SC4UI_GraphicsDialogUp))
        return 0; // AL is the flag's byte, 0, in the original
    Virtual<uint32_t>(self, 0x4);
    if (void* const gen = At<void*>(self, graphics::kWindow)) {
        void* const window = Virtual<void*>(gen, 0xC);
        void* const manager = Virtual<void*>(window, 0x18);
        if (manager) {
            if (Virtual<uint8_t>(window, 0x10C, uint32_t{0x1000}))
                Virtual<void>(manager, 0x24, uint32_t{0xF}, window, uint32_t{1});
            Virtual<void>(manager, 0x5C, window);
        }
    }
    Clear(At<void*>(self, graphics::kWindow));
    Global<uint8_t>(addr::SC4UI_GraphicsDialogUp) = 0;
    return Virtual<uint32_t>(self, 0x8);
}

// The game's 0x0078FFC0. `this` is the dialog. Shows the settings' video
// part in the window's buttons. The first time (byte +0x1E) it also keeps
// what five of them said, in the fields from +0x10 on. With no settings it
// only clears that byte.
void __fastcall GraphicsShowSettings(char* self, void*)
{
    if (At<void*>(self, graphics::kSettings)) {
        void* check = nullptr;
        const auto settings = [self] { return At<char*>(self, graphics::kSettings); };
        const auto first = [self] { return At<uint8_t>(self, graphics::kFirstFill) != 0; };
        const auto window = [self] { return Virtual<void*>(At<void*>(self, graphics::kWindow), 0xC); };

        ShowLevel(self, check, kFirstEffects, At<uint8_t>(settings(), video::kEffects));
        ShowLevel(self, check, kFirstCarsAndSims, At<uint8_t>(settings(), video::kCarsAndSims));

        const uint8_t detail = At<uint8_t>(settings(), video::kDetail);
        if (detail >= 1 && detail <= 3) {
            ShowLevel(self, check, kFirstDetail, detail);
            if (first())
                At<uint16_t>(self, graphics::kWasDetail) = static_cast<uint16_t>(detail - 1);
        }

        ShowLevel(self, check, kFirstShadows, At<uint8_t>(settings(), video::kShadows));

        {
            const bool on = At<uint8_t>(settings(), video::kAtmosphere) == 1;
            CheckChild(window(), kAtmosphere, on);
        }
        {
            const bool on = At<uint8_t>(settings(), video::kWater) == 1;
            CheckChild(window(), kWater, on);
        }

        // The word kept for this box is the other way round: 0 when on.
        {
            void* const box = window();
            Clear(check);
            if (Virtual<uint8_t>(box, 0x94, kTranslucency, kCheckIid, &check)) {
                if (At<uint8_t>(settings(), video::kTranslucency) == 1) {
                    Virtual<void>(check, 0x18);
                    if (first())
                        At<uint16_t>(self, graphics::kWasOpaque) = 0;
                } else {
                    Virtual<void>(check, 0x1C);
                    if (first())
                        At<uint16_t>(self, graphics::kWasOpaque) = 1;
                }
            }
        }
        {
            void* const box = window();
            Clear(check);
            if (Virtual<uint8_t>(box, 0x94, kCursor, kCheckIid, &check)) {
                if (At<uint8_t>(settings(), video::kCursor) == 1) {
                    Virtual<void>(check, 0x18);
                    if (first())
                        At<uint16_t>(self, graphics::kWasCursor) = 1;
                } else {
                    Virtual<void>(check, 0x1C);
                    if (first())
                        At<uint16_t>(self, graphics::kWasCursor) = 0;
                }
            }
        }
        {
            const bool on = At<uint8_t>(settings(), video::kAutomata) == 1;
            CheckChild(window(), kAutomata, on);
        }

        const uint8_t driver = At<uint8_t>(settings(), video::kDriver);
        if (driver == 1) {
            SetCheck(self, check, kFirstDriver, true, Emptied::Inline);
            SetCheck(self, check, kFirstDriver + 1, false, Emptied::Inline);
            if (first())
                At<uint16_t>(self, graphics::kWasDriver) = 1;
        } else if (driver == 0) {
            SetCheck(self, check, kFirstDriver, false, Emptied::Inline);
            SetCheck(self, check, kFirstDriver + 1, true, Emptied::Inline);
            if (first())
                At<uint16_t>(self, graphics::kWasDriver) = 0;
        }

        for (int32_t i = 0; i <= 2; ++i) {
            void* const box = window();
            Clear(check);
            if (Virtual<uint8_t>(box, 0x94, kFirstLoader + static_cast<uint32_t>(i), kCheckIid, &check)) {
                const int32_t group = At<uint8_t>(settings(), video::kLoader);
                if (group - 1 == i)
                    Virtual<void>(check, 0x18);
                else
                    Virtual<void>(check, 0x1C);
            }
        }

        for (uint32_t i = 0; i < 3; ++i) {
            void* const box = window();
            Clear(check);
            if (Virtual<uint8_t>(box, 0x94, kTextureButtons[i], kCheckIid, &check)) {
                if (uint32_t{At<uint8_t>(settings(), video::kTextures)} == i + 1)
                    Virtual<void>(check, 0x18);
                else
                    Virtual<void>(check, 0x1C);
            }
        }

        // The screen size: one of four, or none pressed for any other.
        {
            const uint32_t width = At<uint32_t>(settings(), video::kWidth);
            const uint32_t height = At<uint32_t>(settings(), video::kHeight);
            int32_t size = -1;
            if (width == 800 && height == 600)
                size = 0;
            else if (width == 1024 && height == 768)
                size = 1;
            else if (width == 1280 && height == 1024)
                size = 2;
            else if (width == 1600 && height == 1200)
                size = 3;
            for (int32_t i = 0; i < 4; ++i)
                SetCheck(self, check, kFirstSize + static_cast<uint32_t>(i), i == size, Emptied::Inline);
            if (first())
                At<uint16_t>(self, graphics::kWasSize) = static_cast<uint16_t>(size < 0 ? 0 : size);
        }

        const uint32_t depth = At<uint32_t>(settings(), video::kDepth);
        if (depth == 16) {
            SetCheck(self, check, kFirstDepth, true, Emptied::Inline);
            SetCheck(self, check, kFirstDepth + 1, false, Emptied::Inline);
        } else if (depth == 32) {
            SetCheck(self, check, kFirstDepth, false, Emptied::Inline);
            SetCheck(self, check, kFirstDepth + 1, true, Emptied::Inline);
        }
        if (first())
            At<uint16_t>(self, graphics::kWasDepth) = At<uint16_t>(settings(), video::kDepth);
        if (check)
            Release(check);
    }
    At<uint8_t>(self, graphics::kFirstFill) = 0;
}

// The game's 0x00791360. `this` is the dialog's memory (0x20 bytes).
char* __fastcall GraphicsConstruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0) = addr::SC4UI_GraphicsDialogVtable;
    At<void*>(self, graphics::kWindow) = nullptr;
    At<void*>(self, graphics::kSettings) = nullptr;
    At<uint32_t>(self, graphics::kReferences) = 0;
    At<uint16_t>(self, graphics::kWasCursor) = 0;
    At<uint16_t>(self, graphics::kWasDriver) = 0;
    At<uint16_t>(self, graphics::kWasDepth) = 0;
    At<uint16_t>(self, graphics::kWasDetail) = 0;
    At<uint16_t>(self, graphics::kWasSize) = 0;
    At<uint8_t>(self, graphics::kSecondPage) = 0;
    At<uint16_t>(self, graphics::kWasOpaque) = 0;
    At<uint8_t>(self, graphics::kFirstFill) = 1;
    return self;
}

// The game's 0x007913A0. `this` is the dialog. With a window loaded: runs
// it modally with the keyboard in it, gives the keyboard back to the window
// that had it and closes. The window manager the framework gives is not
// checked for null, as in the original. Always returns true.
uint8_t __fastcall GraphicsRun(char* self, void*)
{
    if (!At<void*>(self, graphics::kWindow))
        return 1;
    uint8_t guard[4];
    This<void*>(addr::SC4ModalGuard_Construct, &guard[0], uint32_t{0}, uint32_t{0}, uint32_t{1}, uint32_t{1});
    void* manager = nullptr;
    if (void* const framework = Cdecl<void*>(addr::RZGetFramework))
        Virtual<uint8_t>(framework, 0x14, uint32_t{0xA417445E}, uint32_t{0x5A4}, &manager);
    void* const focus = Virtual<void*>(manager, 0x90);
    void* const window = Virtual<void*>(At<void*>(self, graphics::kWindow), 0xC);
    if (window)
        AddRef(window);
    Virtual<void>(Global<void*>(addr::SC4_Service43CE0), 0x28, window, uint32_t{1});
    Virtual<void>(manager, 0x94, window);
    Virtual<void>(Cdecl<void*>(addr::SC4_GetWinManager), 0xA4, window); // modal: returns when the dialog closes
    Virtual<void>(Global<void*>(addr::SC4_Service43CE0), 0x18, window);
    if (Virtual<uint8_t>(manager, 0x60, focus))
        Virtual<void>(manager, 0x94, focus);
    GraphicsClose(self, nullptr);
    if (window)
        Release(window);
    This<void>(addr::SC4RefPtr_Release, &manager);
    This<void>(addr::SC4ModalGuard_Destruct, &guard[0]);
    return 1;
}

// The game's 0x00791490 (ret 4). `this` is the dialog (slot 5). Loads the
// window of the layout `key` (the table has one, key 0) at the size of its
// parent, becomes its window procedure, shows the settings, makes the
// buttons report to the window and sets flag 1 of the two pages (off for
// 0x2A57CB83, on for 0x2A57CB84). Returns true only when the dialog is up already; a fresh
// load returns false, like a failed one.
uint8_t __fastcall GraphicsInit(char* self, void*, uint32_t key)
{
    if (!Global<uint8_t>(addr::SC4UI_GraphicsDialogGuard)) {
        Global<uint8_t>(addr::SC4UI_GraphicsDialogGuard) = 1;
        Global<uint8_t>(addr::SC4UI_GraphicsDialogUp) = 0;
    } else if (Global<uint8_t>(addr::SC4UI_GraphicsDialogUp)) {
        return 1;
    }
    const Layout* const layouts = reinterpret_cast<const Layout*>(addr::SC4UI_GraphicsDialogLayouts);
    const Layout* layout = nullptr;
    for (std::size_t i = 0; i < 1; ++i) {
        if (layouts[i].key == key) {
            layout = &layouts[i];
            break;
        }
    }
    if (!layout)
        return 0;
    uint32_t resource[3] = {0, kUiGroup, layout->instance};
    void*& gen = At<void*>(self, graphics::kWindow);
    Clear(gen);
    if (!Cdecl<uint8_t>(addr::SC4UI_LoadWindow, &resource[0], static_cast<void*>(nullptr), layout->window, kWinGenIid,
                        &gen))
        return 0;

    int32_t width;
    int32_t height;
    if (void* const parent = Virtual<void*>(Virtual<void*>(gen, 0xC), 0x2C)) {
        width = Virtual<int32_t>(parent, 0xA4);
        height = Virtual<int32_t>(parent, 0xA8);
    } else {
        width = 0x280;
        height = 0x1E0;
    }
    Virtual<void>(Virtual<void*>(gen, 0xC), 0xCC, width);
    Virtual<void>(Virtual<void*>(gen, 0xC), 0xD0, height);

    if (!At<void*>(self, graphics::kSettings))
        At<void*>(self, graphics::kSettings) = Global<void*>(addr::SC4_Options43CD8);
    Global<uint8_t>(addr::SC4UI_GraphicsDialogUp) = 1;
    Virtual<void*>(gen, 0xC);
    Virtual<void>(gen, 0x1C, static_cast<void*>(self));
    GraphicsShowSettings(self, nullptr);

    for (const uint32_t id : kReporting) {
        void* const holder = gen;
        void* const button = Virtual<void*>(Virtual<void*>(holder, 0xC), 0x8C, id);
        Virtual<void>(button, 0x158, Virtual<void*>(holder, 0xC));
    }
    Virtual<void>(Virtual<void*>(Virtual<void*>(gen, 0xC), 0x8C, kRestartPage), 0x110, uint32_t{1}, uint32_t{0});
    Virtual<void>(Virtual<void*>(Virtual<void*>(gen, 0xC), 0x8C, kChoicesPage), 0x110, uint32_t{1}, uint32_t{1});
    return 0;
}

// A button of a group where one is pressed at a time: every button of the
// group set, the pressed one in and the others out.
template <std::size_t N>
uint8_t PressOne(char* self, const uint32_t (&group)[N], uint32_t pressed, Emptied how)
{
    void* check = nullptr;
    for (const uint32_t id : group)
        SetCheck(self, check, id, id == pressed, how);
    if (check)
        Release(check);
    return 1;
}

// The settings' video part saved and applied by the application (its slots
// 17, SavePreferences, and 19, ApplyVideoPreferences).
void ApplySettings(char* self, void* app)
{
    Virtual<uint8_t>(app, 0x44);
    Virtual<uint8_t>(app, 0x4C, static_cast<void*>(At<char*>(self, graphics::kSettings) + video::kStart));
}

// The game's 0x00791D70 (ret 8). `this` is the dialog. A button of the
// window was clicked; the second argument, the window, is not used. Always
// returns true.
uint8_t __fastcall GraphicsCommand(char* self, void*, uint32_t id, void*)
{
    switch (id) {
    case 0x6A57DA49:
    case 0x6A57DA4A:
    case 0x6A57DA4B:
        return PressOne(self, kEffectsButtons, id, Emptied::ByHelper);
    case 0x6A57DA4C:
    case 0x6A57DA4D:
        return PressOne(self, kCarsAndSimsButtons, id, Emptied::ByHelper);
    case 0x6A57DA4E:
        return PressOne(self, kCarsAndSimsButtons, id, Emptied::Inline);
    case 0x6A57DA4F:
    case 0x6A57DA50:
    case 0x6A57DA51:
        return PressOne(self, kShadowsButtons, id, Emptied::ByHelper);
    case 0x2AB6C373:
    case 0x2AB6C374:
    case 0x2AB6C375:
        return PressOne(self, kDetailButtons, id, Emptied::ByHelper);
    case 0x2AB6C376:
    case 0x2AB6C377:
        return PressOne(self, kDepthButtons, id, Emptied::ByHelper);
    case 0x6A57DA57:
    case 0x6A57DA58:
        return PressOne(self, kDriverButtons, id, Emptied::ByHelper);
    case 0x6A57DA5A:
    case 0x6A57DA5B:
    case 0x6A57DA5C:
    case 0x6A57DA5D:
        return PressOne(self, kSizeButtons, id, Emptied::ByHelper);
    case 0xAB81DED5:
    case 0xAB81DED6:
    case 0xAB81DED7:
        return PressOne(self, kLoaderButtons, id, Emptied::ByHelper);
    case 0x0B85CF92:
    case 0x2B85CF9F:
    case 0x2B85CFAA:
        return PressOne(self, kTextureButtons, id, Emptied::Inline);
    case kCancel:
        GraphicsClose(self, nullptr);
        return 1;
    case kAccept: {
        Virtual<void>(self, 0x1C); // the buttons read back into the settings
        if (At<uint8_t>(self, graphics::kSecondPage)) {
            // The two pages change places; here they are looked for in the
            // window's parent, in Init in the window itself.
            void* parent = Virtual<void*>(Virtual<void*>(At<void*>(self, graphics::kWindow), 0xC), 0x2C);
            Virtual<void>(Virtual<void*>(parent, 0x8C, kRestartPage), 0x110, uint32_t{1}, uint32_t{1});
            parent = Virtual<void*>(Virtual<void*>(At<void*>(self, graphics::kWindow), 0xC), 0x2C);
            Virtual<void>(Virtual<void*>(parent, 0x8C, kChoicesPage), 0x110, uint32_t{1}, uint32_t{0});
            return 1;
        }
        // The application is not checked for null before its calls, as in
        // the original.
        void* app;
        This<void*>(addr::SC4RefHolder_SC4RefHolder, &app, Global<void*>(addr::SC4_Service43C94));
        ApplySettings(self, app);
        if (app)
            Release(app);
        void* const server = Global<void*>(addr::SC4_MessageServer);
        void* const memory = Cdecl<void*>(addr::RZ_operator_new, uint32_t{0x2C});
        void* message;
        This<void*>(addr::SC4RefHolder_SC4RefHolder, &message,
                    memory ? This<void*>(addr::cGZMessage_cGZMessage, memory) : nullptr);
        Virtual<void>(message, 0x14, kSettingsChanged);
        Virtual<void>(server, 0x10, message, uint32_t{0});
        GraphicsClose(self, nullptr);
        Release(message);
        return 1;
    }
    case kAcceptAnyway: {
        Virtual<void>(self, 0x1C);
        void* const app = Global<void*>(addr::SC4_Service43C94);
        if (app)
            AddRef(app);
        ApplySettings(self, app);
        if (app)
            Release(app);
        void* const server = Global<void*>(addr::SC4_MessageServer);
        void* const memory = Cdecl<void*>(addr::RZ_operator_new, uint32_t{0x2C});
        void* message = nullptr;
        if (memory) {
            message = This<void*>(addr::cGZMessage_cGZMessage, memory);
            if (message)
                AddRef(message);
        }
        Virtual<void>(message, 0x14, kSettingsChanged);
        Virtual<void>(server, 0x10, message, uint32_t{0});
        GraphicsClose(self, nullptr);
        Release(message);
        return 1;
    }
    case kAutomatic: {
        // The application's own choices (its slot 20,
        // GetAutoVideoPreferences) written into the settings and shown.
        void* const app = Global<void*>(addr::SC4_Service43C94);
        if (!app)
            return 1;
        AddRef(app);
        if (char* const settings = At<char*>(self, graphics::kSettings)) {
            Virtual<uint8_t>(app, 0x50, static_cast<void*>(settings + video::kStart));
            GraphicsShowSettings(self, nullptr);
        }
        Release(app);
        return 1;
    }
    default:
        return 1;
    }
}

bool InstallPanels2(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[8] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F78FF40, reinterpret_cast<void*>(&GraphicsClose)},
        {addr::SC4UI_F78FFC0, reinterpret_cast<void*>(&GraphicsShowSettings)},
        {addr::SC4UI_F791360, reinterpret_cast<void*>(&GraphicsConstruct)},
        {addr::SC4UI_F7913A0, reinterpret_cast<void*>(&GraphicsRun)},
        {addr::SC4UI_F791490, reinterpret_cast<void*>(&GraphicsInit)},
        {addr::SC4UI_F791D70, reinterpret_cast<void*>(&GraphicsCommand)},
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

const bool g_panels2Registered = registry::Register({
    "ui.panels.part2",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F791490,
    "Our graphics options dialog: fill, run, close and the buttons' commands (docs/ui.md)",
    false,
    InstallPanels2,
});

} // namespace
} // namespace opensc4::ui
