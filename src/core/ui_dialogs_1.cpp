// The first part of the city view's budget panel (0x00764820 to 0x00765C30):
// a window object of 0x1A0 bytes (vtables 0x00AB4A40, 0x00AB4A2C at +8,
// 0x00AB4A1C at +0xC) that the city view makes with every city and keeps at
// +0x2E8. It loads the layout 0xCBC3C2B9 into the main window: the panel
// 0xAA3AC000 and the dialogs 0xAA3AC001, 0xAA3AC002 (the tax rates) and
// 0xCA4C332D (the bonds).
//
// This part holds the constructor and destructor, the fixed texts of the
// tax rate rows, the registration for the panel's messages, the two dialogs'
// accept paths (twelve tax rates given to the budget simulator, new bonds
// issued) and small helpers. Its window procedure and the rest are in the
// parts after it. Replacement, off by default:
//
//   ui.dialogs.part1: everything above.
//
// Windows and texts are the game's, called through their vtables.
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

namespace panel {
constexpr std::size_t kWinProc = 0x08;    // the window procedure part
constexpr std::size_t kTarget = 0x0C;     // the message target part
constexpr std::size_t kWindow = 0x10;     // the layout's cIGZWinGen
constexpr std::size_t kBudget = 0x14;     // the budget simulator
constexpr std::size_t kThird = 0x18;      // a third reference, not used here
constexpr std::size_t kBonds = 0x1C;      // a vector of 0x20-byte bond rows (begin, end, capacity)
constexpr std::size_t kZeroed = 0x28;     // dwords cleared by the constructor, to 0x150
constexpr std::size_t kRates = 0x150;     // twelve floats: the tax rates shown, in tenths of a percent
constexpr std::size_t kCount180 = 0x180;
constexpr std::size_t kBondsWanted = 0x184;
constexpr std::size_t kBondsIssued = 0x188;
constexpr std::size_t kField18C = 0x18C;
constexpr std::size_t kField190 = 0x190;
constexpr std::size_t kFlag194 = 0x194;
constexpr std::size_t kFlag195 = 0x195;
constexpr std::size_t kMode = 0x198;      // picks one of two sets of row texts
constexpr std::size_t kRegistered = 0x19C;
constexpr std::size_t kShown = 0x19D;     // answered by slot 9, cleared by slot 7
constexpr std::size_t kDirty = 0x19E;     // set by slot 8 while kShown is set
} // namespace panel

constexpr std::size_t kRateCount = 12;
constexpr std::size_t kBondRowSize = 0x20;
constexpr std::size_t kBondAmount = 0x08; // in a bond row

constexpr uint32_t kPanelWindow = 0xAA3AC000;
constexpr uint32_t kDialog1 = 0xAA3AC001;
constexpr uint32_t kTaxDialog = 0xAA3AC002;
constexpr uint32_t kBondDialog = 0xCA4C332D;
constexpr uint32_t kOptionalRows = 0xAA3AD200;

constexpr uint32_t kWinGenIid = 0x5386D516;
constexpr uint32_t kTextIid = 0x212CDC1F;
constexpr uint32_t kUiGroup = 0x96A006B0;
constexpr uint32_t kLayout = 0xCBC3C2B9;
constexpr uint32_t kTextGroup = 0x6A3FF01C;

constexpr uint32_t kMessages[3] = {0x08DAC3E8, 0x08DAC3E7, 0x86AD10EE};

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

// A tree as the game's window helpers fill it on the caller's stack.
struct Tree {
    char* header;
    uint32_t count;
    uint32_t spare[3];
};

// One fixed text: a child window and the string it gets.
struct RowText {
    uint32_t window;
    uint32_t text;
};

constexpr std::size_t kRowTexts = 18;
constexpr std::size_t kRowTextsAlways = 14; // the rest only when the window 0xAA3AD200 exists

// The texts for mode 0 and for any other mode.
constexpr RowText kTexts[2][kRowTexts] = {
    {
        {0xAA3AC108, 0x0A569E48}, {0xAA3AC109, 0x0A569E48}, {0xAA3AC104, 0x4A569EF8}, {0xAA3AC10A, 0x4A569EF8},
        {0xAA3AC105, 0xAA5690B0}, {0xAA3AC10B, 0xAA5690B0}, {0xAA3AC106, 0x4A569F44}, {0xAA3AC10C, 0x4A569F44},
        {0xAA3AC107, 0xCA569FA8}, {0xAA3AC10D, 0xCA569FA8}, {0xAA3AD120, 0xAA5690B0}, {0xAA3AD121, 0xCA569B75},
        {0xAA3AD122, 0x4A569F44}, {0xAA3AD123, 0xCA569B75}, {0xAA3AD200, 0xAA5690B0}, {0xAA3AD201, 0xCA569B75},
        {0xAA3AD202, 0x4A569F44}, {0xAA3AD203, 0xCA569B75},
    },
    {
        {0xAA3AC108, 0x6A569FFD}, {0xAA3AC109, 0x6A569FFD}, {0xAA3AC104, 0x4A569EF8}, {0xAA3AC10A, 0x4A569EF8},
        {0xAA3AC105, 0xAA569DCD}, {0xAA3AC10B, 0xAA569DCD}, {0xAA3AC106, 0xAA56A0F8}, {0xAA3AC10C, 0xAA56A0F8},
        {0xAA3AC107, 0x4A56A15B}, {0xAA3AC10D, 0x4A56A15B}, {0xAA3AD120, 0xAA569DCD}, {0xAA3AD121, 0x0A569C81},
        {0xAA3AD122, 0xAA56A0F8}, {0xAA3AD123, 0x0A569C81}, {0xAA3AD200, 0xAA569DCD}, {0xAA3AD201, 0x0A569C81},
        {0xAA3AD202, 0xAA56A0F8}, {0xAA3AD203, 0x0A569C81},
    },
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

using SprintfFn = void*(__cdecl*)(void*, const char*, ...);

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

void DestroyString(GameString& text)
{
    char* const begin = text.body.begin;
    text.vtable = addr::SC4String_Vtable;
    if (begin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(begin));
}

// The window that holds the layout's windows: the parent of the layout's own.
void* Parent(char* self)
{
    void* const window = Virtual<void*>(At<void*>(self, panel::kWindow), 0xC);
    return Virtual<void*>(window, 0x2C);
}

// A window of the layout by ID, searched in depth.
void* Child(void* parent, uint32_t id)
{
    return Virtual<void*>(parent, 0x8C, id);
}

void Hide(void* window)
{
    Virtual<void>(window, 0x110, uint32_t{1}, uint32_t{0});
}

// Takes the panel's three notifications and its entry at the service
// 0x86AD10EC back. Callers have checked and cleared the flag.
void Unregister(char* self, void* server, void* service)
{
    void* const target = self + panel::kTarget;
    for (const uint32_t message : kMessages)
        Virtual<void>(server, 0x18, target, message);
    Virtual<void>(service, 0x18, target, uint32_t{0xFFFFFFFF});
}

// The game's 0x00764820. `this` is the panel (slot 9).
uint8_t __fastcall IsShown(char* self, void*)
{
    return At<uint8_t>(self, panel::kShown);
}

// The game's 0x00764830. `this` is the panel (slot 8). Marks the panel for
// a refresh while it is shown. AL keeps the shown flag, as in the original.
uint8_t __fastcall MarkDirty(char* self, void*)
{
    const uint8_t shown = At<uint8_t>(self, panel::kShown);
    if (shown)
        At<uint8_t>(self, panel::kDirty) = 1;
    return shown;
}

// The game's 0x00764850. `this` is the panel. The tax dialog accepted: gives
// the twelve rates to the budget simulator (a tenth of each stored figure),
// hides the dialog and ends its modal state. Nothing is checked for null.
void __fastcall ApplyTaxRates(char* self, void*)
{
    void* const dialog = Child(Parent(self), kTaxDialog);
    for (uint32_t group = 0; group < kRateCount; ++group) {
        const float rate = static_cast<float>(
            static_cast<double>(At<float>(self, panel::kRates + 4 * group)) * static_cast<double>(0.1f));
        Virtual<void>(At<void*>(self, panel::kBudget), 0x94, group, rate);
    }
    Hide(dialog);
    void* const manager = Cdecl<void*>(addr::SC4_GetWinManager);
    Virtual<void>(manager, 0x24, uint32_t{0xF}, dialog, uint32_t{0});
}

// The game's 0x007648D0. `this` is the panel. Registers the message target
// part for three messages and with the service 0x86AD10EC, or takes that
// back.
void __fastcall Register(char* self, void*, uint8_t on)
{
    void* const server = Global<void*>(addr::SC4_MessageServer);
    void* const service = Cdecl<void*>(addr::SC4_GetService86AD10EC);
    if (on) {
        if (At<uint8_t>(self, panel::kRegistered))
            return;
        At<uint8_t>(self, panel::kRegistered) = 1;
        void* const target = self + panel::kTarget;
        for (const uint32_t message : kMessages)
            Virtual<void>(server, 0x14, target, message);
        Virtual<void>(service, 0x14, target, uint32_t{0xC0});
    } else {
        if (!At<uint8_t>(self, panel::kRegistered))
            return;
        At<uint8_t>(self, panel::kRegistered) = 0;
        Unregister(self, server, service);
    }
}

// The game's 0x00764990. `this` is the panel. Takes the layout's windows off
// the screen, lets go of the three references and of the notifications. The
// result is what the last Release returned.
uint32_t __fastcall Close(char* self, void*)
{
    Virtual<uint32_t>(self, 0x4);
    Virtual<void>(At<void*>(self, panel::kWindow), 0x1C, static_cast<void*>(nullptr));
    void* const manager = Virtual<void*>(Virtual<void*>(At<void*>(self, panel::kWindow), 0xC), 0x18);
    if (manager) {
        void* const parent = Parent(self);
        Virtual<void>(manager, 0x5C, Child(parent, kPanelWindow));
        Virtual<void>(manager, 0x5C, Child(parent, kDialog1));
        Virtual<void>(manager, 0x5C, Child(parent, kTaxDialog));
        Virtual<void>(manager, 0x5C, Child(parent, kBondDialog));
        Virtual<void>(manager, 0x5C, Virtual<void*>(At<void*>(self, panel::kWindow), 0xC));
    }
    Clear(At<void*>(self, panel::kWindow));
    Clear(At<void*>(self, panel::kBudget));
    Clear(At<void*>(self, panel::kThird));
    void* const server = Global<void*>(addr::SC4_MessageServer);
    void* const service = Cdecl<void*>(addr::SC4_GetService86AD10EC);
    if (At<uint8_t>(self, panel::kRegistered)) {
        At<uint8_t>(self, panel::kRegistered) = 0;
        Unregister(self, server, service);
    }
    return Virtual<uint32_t>(self, 0x8);
}

// The game's 0x00764AD0. `this` is the panel (slot 7). Hides the panel and
// its three dialogs. Nothing is checked for null.
void __fastcall HideAll(char* self, void*)
{
    void* const parent = Parent(self);
    Hide(Child(parent, kDialog1));
    Hide(Child(parent, kPanelWindow));
    Hide(Child(parent, kTaxDialog));
    Hide(Child(parent, kBondDialog));
    At<uint8_t>(self, panel::kShown) = 0;
}

// The game's 0x00764B70. `this` is the panel. What the city view calls
// before it lets go of the panel: unregisters and closes.
uint32_t __fastcall Shutdown(char* self, void*)
{
    if (At<uint8_t>(self, panel::kRegistered)) {
        void* const server = Global<void*>(addr::SC4_MessageServer);
        void* const service = Cdecl<void*>(addr::SC4_GetService86AD10EC);
        if (At<uint8_t>(self, panel::kRegistered)) {
            At<uint8_t>(self, panel::kRegistered) = 0;
            Unregister(self, server, service);
        }
    }
    return Close(self, nullptr);
}

// The game's 0x00764BE0. `this` is the panel. The bond dialog accepted with
// more bonds wanted than issued: asks the budget simulator (taken from the
// city when the panel has none) for each new row's amount.
void __fastcall IssueBonds(char* self, void*)
{
    if (At<uint32_t>(self, panel::kBondsWanted) <= At<uint32_t>(self, panel::kBondsIssued))
        return;
    if (!At<void*>(self, panel::kBudget)) {
        void* const budget = Virtual<void*>(Global<void*>(addr::SC4_City), 0xD0);
        void* const old = At<void*>(self, panel::kBudget);
        if (budget != old) {
            if (budget)
                AddRef(budget);
            At<void*>(self, panel::kBudget) = budget;
            if (old)
                Release(old);
        }
    }
    uint32_t row = At<uint32_t>(self, panel::kBondsIssued);
    for (; row < At<uint32_t>(self, panel::kBondsWanted); ++row) {
        char* const rows = At<char*>(self, panel::kBonds);
        const uint32_t amount = At<uint32_t>(rows, row * kBondRowSize + kBondAmount);
        Virtual<void>(At<void*>(self, panel::kBudget), 0xA0, amount);
    }
}

// The game's 0x00764C70. `this` is the panel. The destructor.
void __fastcall Destruct(char* self, void*)
{
    At<uint32_t>(self, 0x0) = addr::SC4UI_BudgetPanelVtable;
    At<uint32_t>(self, panel::kWinProc) = addr::SC4UI_BudgetPanelVtable8;
    At<uint32_t>(self, panel::kTarget) = addr::SC4UI_BudgetPanelVtableC;
    if (char* const rows = At<char*>(self, panel::kBonds))
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(rows));
    if (void* const third = At<void*>(self, panel::kThird))
        Release(third);
    if (void* const budget = At<void*>(self, panel::kBudget))
        Release(budget);
    if (void* const gen = At<void*>(self, panel::kWindow))
        Release(gen);
    This<void>(addr::cRZBaseUnknown_Destruct, self);
}

// The game's 0x00764CD0. `this` is the panel (slot 3). The deleting
// destructor.
char* __fastcall Delete(char* self, void*, uint32_t flags)
{
    Destruct(self, nullptr);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// The game's 0x00764CF0. `this` is the panel. Hides the bond dialog, ends
// its modal state, empties the bond rows and ends with slot 13 of the service
// at 0x00B43D5C, whose result it returns.
uint32_t __fastcall CloseBondDialog(char* self, void*)
{
    void* const dialog = Child(Parent(self), kBondDialog);
    Hide(dialog);
    void* const manager = Cdecl<void*>(addr::SC4_GetWinManager);
    Virtual<void>(manager, 0x24, uint32_t{0xF}, dialog, uint32_t{0});
    char* const vector = self + panel::kBonds;
    This<void>(addr::LoanVector_Erase, vector, At<char*>(vector, 0x0), At<char*>(vector, 0x4));
    return Virtual<uint32_t>(Global<void*>(addr::SC4_SimulatorService), 0x34);
}

// The game's 0x00764D50. `this` is not used. Writes two numbers with the
// language's separator between them into a string ("%d%s%d").
void __fastcall FormatParts(char*, void*, int32_t whole, int32_t part, void* out)
{
    GameString separator;
    separator.body.begin = nullptr;
    separator.body.end = nullptr;
    separator.body.capacity = nullptr;
    char* const memory = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    separator.body.begin = memory;
    separator.body.end = memory;
    separator.body.capacity = memory + 8;
    *memory = 0;
    separator.vtable = addr::SC4String_Vtable;
    separator.zero = 0;

    void* const app = Cdecl<void*>(addr::SC4_CurrentApp);
    void* const language = Virtual<void*>(app, 0x98, uint32_t{0});
    Virtual<void>(language, 0x3C, &separator);
    const auto format = reinterpret_cast<SprintfFn>((*static_cast<void***>(out))[0x6C / sizeof(void*)]);
    format(out, "%d%s%d", whole, separator.body.begin, part);
    DestroyString(separator);
}

// The game's 0x00764DF0. `this` is the panel. Gives the rows' fixed texts to
// their windows; the last four only when the window 0xAA3AD200 exists. The
// other windows are not checked for null, as in the original.
void __fastcall SetRowTexts(char* self, void*)
{
    void* const parent = Parent(self);
    const RowText* const texts = kTexts[At<uint32_t>(self, panel::kMode) != 0 ? 1 : 0];
    for (std::size_t i = 0; i < kRowTexts; ++i) {
        if (i == kRowTextsAlways && !Child(parent, kOptionalRows))
            return;
        void* const window = Child(parent, texts[i].window);
        struct {
            GameString string;
            uint32_t spare[3];
        } text;
        void* const made = This<void*>(addr::SC4LocalizedString_SC4LocalizedString, &text, kTextGroup, texts[i].text,
                                       uint32_t{1});
        Virtual<void>(window, 0x128, made);
        DestroyString(text.string);
    }
}

// The game's 0x00765800. `this` is the panel. A child window's text set to
// an amount of money.
void __fastcall SetMoney(char* self, void*, uint32_t id, int64_t amount)
{
    void* const parent = Parent(self);
    if (!parent)
        return;
    void* const child = Child(parent, id);
    if (!child)
        return;
    void* text = nullptr;
    if (Virtual<uint8_t>(child, 0x0, kTextIid, &text)) {
        GameString amountText;
        This<void*>(addr::SC4String_SC4String, &amountText);
        Cdecl<void>(addr::SC4_FormatMoney, amount, &amountText);
        Virtual<void>(text, 0x10, &amountText);
        DestroyString(amountText);
    }
    if (text)
        Release(text);
}

// The game's tree eraser 0x0041D7F0 with its first level written out, as the
// constructor has it inlined.
void EraseTree(Tree& tree, char* node)
{
    while (node) {
        This<void>(addr::SC4UI_F41D7F0, &tree, At<char*>(node, 0xC));
        char* const next = At<char*>(node, 0x8);
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(node));
        node = next;
    }
}

// The game's 0x007658A0. `this` is the panel's memory (0x1A0 bytes). Loads
// the layout 0xCBC3C2B9 into the main window; when that worked it becomes
// the layout's window procedure and sets the rows' texts.
char* __fastcall Construct(char* self, void*)
{
    This<void*>(addr::cRZBaseUnknown_Construct, self);
    At<uint32_t>(self, panel::kWinProc) = addr::SC4NewsTicker_WinProcBaseVtable;
    At<uint32_t>(self, panel::kTarget) = addr::cSC4TrafficSimulator_VtableBase4;
    At<uint32_t>(self, 0x0) = addr::SC4UI_BudgetPanelVtable;
    At<uint32_t>(self, panel::kWinProc) = addr::SC4UI_BudgetPanelVtable8;
    At<uint32_t>(self, panel::kTarget) = addr::SC4UI_BudgetPanelVtableC;
    At<void*>(self, panel::kWindow) = nullptr;
    At<void*>(self, panel::kBudget) = nullptr;
    At<void*>(self, panel::kThird) = nullptr;
    At<char*>(self, panel::kBonds + 0x8) = nullptr;
    At<char*>(self, panel::kBonds) = nullptr;
    At<char*>(self, panel::kBonds + 0x8) = nullptr;
    At<char*>(self, panel::kBonds + 0x4) = nullptr;
    for (std::size_t at = panel::kZeroed; at < panel::kRates; at += 4)
        At<uint32_t>(self, at) = 0;
    At<uint32_t>(self, panel::kCount180) = 0;
    At<uint32_t>(self, panel::kBondsWanted) = 0;
    At<uint32_t>(self, panel::kBondsIssued) = 0;
    At<uint32_t>(self, panel::kField18C) = 0;
    At<uint32_t>(self, panel::kField190) = 0;
    At<uint8_t>(self, panel::kFlag194) = 0;
    At<uint8_t>(self, panel::kFlag195) = 0;
    At<uint32_t>(self, panel::kMode) = 0;
    At<uint8_t>(self, panel::kRegistered) = 0;
    At<uint8_t>(self, panel::kShown) = 0;
    At<uint8_t>(self, panel::kDirty) = 0;

    uint32_t key[3] = {0, kUiGroup, kLayout};
    void* const mainWindow = Virtual<void*>(Global<void*>(addr::SC4_Service43DC4), 0xC);
    Tree before;
    before.header = nullptr;
    before.header = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{0x14});
    before.count = 0;
    At<uint8_t>(before.header, 0x0) = 0;
    At<char*>(before.header, 0x4) = nullptr;
    At<char*>(before.header, 0x8) = before.header;
    At<char*>(before.header, 0xC) = before.header;
    Cdecl<uint32_t>(addr::SC4UIWindows_Collect, mainWindow, &before);

    void** const gen = &At<void*>(self, panel::kWindow);
    Clear(*gen);
    if (Cdecl<uint8_t>(addr::SC4UI_LoadWindow, &key[0], mainWindow, kPanelWindow, kWinGenIid, gen)) {
        Cdecl<void>(addr::SC4UIWindows_NewSince, mainWindow, &before, &before);
        Cdecl<uint8_t>(addr::SC4UIWindows_Prepare, &before, uint32_t{0});
        Virtual<void>(*gen, 0x1C, static_cast<void*>(self + panel::kWinProc));
        Cdecl<void>(addr::SC4UIWindows_Unpaint, Virtual<void*>(*gen, 0xC));
        SetRowTexts(self, nullptr);
    }
    for (std::size_t i = 0; i < kRateCount; ++i)
        At<uint32_t>(self, panel::kRates + 4 * i) = 0;

    if (before.count != 0) {
        EraseTree(before, At<char*>(before.header, 0x4));
        At<char*>(before.header, 0x8) = before.header;
        At<char*>(before.header, 0x4) = nullptr;
        At<char*>(before.header, 0xC) = before.header;
        before.count = 0;
    }
    if (before.header)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(before.header));
    return self;
}

bool InstallDialogs1(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[16] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F764820, reinterpret_cast<void*>(&IsShown)},
        {addr::SC4UI_F764830, reinterpret_cast<void*>(&MarkDirty)},
        {addr::SC4UI_F764850, reinterpret_cast<void*>(&ApplyTaxRates)},
        {addr::SC4UI_F7648D0, reinterpret_cast<void*>(&Register)},
        {addr::SC4UI_F764990, reinterpret_cast<void*>(&Close)},
        {addr::SC4UI_F764AD0, reinterpret_cast<void*>(&HideAll)},
        {addr::SC4UI_F764B70, reinterpret_cast<void*>(&Shutdown)},
        {addr::SC4UI_F764BE0, reinterpret_cast<void*>(&IssueBonds)},
        {addr::SC4UI_F764C70, reinterpret_cast<void*>(&Destruct)},
        {addr::SC4UI_F764CD0, reinterpret_cast<void*>(&Delete)},
        {addr::SC4UI_F764CF0, reinterpret_cast<void*>(&CloseBondDialog)},
        {addr::SC4UI_F764D50, reinterpret_cast<void*>(&FormatParts)},
        {addr::SC4UI_F764DF0, reinterpret_cast<void*>(&SetRowTexts)},
        {addr::SC4UI_F765800, reinterpret_cast<void*>(&SetMoney)},
        {addr::SC4UI_F7658A0, reinterpret_cast<void*>(&Construct)},
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

const bool g_dialogs1Registered = registry::Register({
    "ui.dialogs.part1",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F7658A0,
    "Our budget panel of the city view, first part: constructor, texts, tax and bond accept paths (docs/ui.md)",
    false,
    InstallDialogs1,
});

} // namespace
} // namespace opensc4::ui
