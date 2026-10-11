// Three small interface classes that sit back to back (0x00762B20 to
// 0x00764820), and two functions of a gauge control before them:
//
//   - the advice popup (0x18 bytes, vtables 0x00AB4968 and 0x00AB4954 at +4):
//     queues urgent advices as the advisors announce them (message
//     0x8A09FA64) and shows them one at a time in a modal window with the
//     advisor's head, the title and the message as HTML;
//   - the small budget window (0x28 bytes, vtables 0x00AB49B0, 0x00AB499C at
//     +8, 0x00AB498C at +0xC): seven department funding sliders over the
//     table at 0x00B0D8B8, applied to the budget simulator when it closes;
//   - the large budget window (0x60 bytes, vtables 0x00AB49F4, 0x00AB49E0,
//     0x00AB49D0): three tax rate spinners, applied at once.
//
// The two budget windows share the six thunks at 0x00763790 with other
// dialogs of the same shape (base class cRZBaseUnknown, a window procedure at
// +8 and a four-slot part at +0xC). Replacement, off by default:
//
//   ui.dialogs.base: everything above.
//
// Windows, texts, sliders and the HTML control are the game's, called through
// their vtables.
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

namespace gauge { // the control before the popup (vtable 0x00AB4900)
constexpr std::size_t kLow = 0xF0;
constexpr std::size_t kHigh = 0xF4;
constexpr std::size_t kValue = 0xF8;
} // namespace gauge

namespace popup {
constexpr std::size_t kWinProc = 0x04;    // the window procedure part
constexpr std::size_t kReferences = 0x08;
constexpr std::size_t kRegistered = 0x0C; // notifications registered
constexpr std::size_t kList = 0x10;       // advices waiting (a list of references)
constexpr std::size_t kHead = 0x14;       // the head view of the popup on screen
} // namespace popup

namespace small {
constexpr std::size_t kWinProc = 0x08;
constexpr std::size_t kFilter = 0x0C;
constexpr std::size_t kWindow = 0x10;  // the window's cIGZWinGen
constexpr std::size_t kBudget = 0x14;  // the budget simulator
constexpr std::size_t kFlag = 0x18;    // 1, never read here
constexpr std::size_t kOther = 0x20;   // int64: a monthly expense from the service at 0x00B43D90 (slot 22)
} // namespace small

namespace large {
constexpr std::size_t kWinProc = 0x08;
constexpr std::size_t kFilter = 0x0C;
constexpr std::size_t kFlag = 0x10;       // 1, never read here
constexpr std::size_t kWindow = 0x14;     // the window's cIGZWinGen
constexpr std::size_t kBudget = 0x18;     // the budget simulator
constexpr std::size_t kOther = 0x20;      // int64: a monthly income from the service at 0x00B43D90 (slot 23)
constexpr std::size_t kOtherYear = 0x28;
constexpr std::size_t kFirst = 0x30;      // int64 pairs (month, year): tax group 1,
constexpr std::size_t kSecond = 0x40;     // tax group 4,
constexpr std::size_t kThird = 0x50;      // tax group 10
} // namespace large

constexpr uint32_t kAdviceParam = 0xEA9F13C0; // the window parameter that holds the popup's advice
constexpr uint32_t kAdviceIid = 0x8A09F5F5;
constexpr uint32_t kWinGenIid = 0x5386D516;
constexpr uint32_t kHtmlIid = 0x4A11FD4A;
constexpr uint32_t kTextIid = 0x212CDC1F;
constexpr uint32_t kSliderIid = 0x21325207;
constexpr uint32_t kSpinnerIid = 0x612CE0C3;
constexpr uint32_t kUiGroup = 0x96A006B0;

constexpr uint32_t kAdviceAnnounced = 0x8A09FA64;
constexpr uint32_t kShowNext = 0xEA83607B;
constexpr uint32_t kCityClosing = 0x26D31EC2;
constexpr uint32_t kBudgetCommand = 0x29222F02; // the budget simulator opens a window on it

// A window message as window procedures get it.
struct WinMessage {
    uint32_t type;
    uint32_t a;
    uint32_t b;
};

// One department of the small budget window (seven at 0x00B0D8B8).
struct BudgetRow {
    uint32_t department;
    uint32_t slider;
    uint32_t monthText;
    uint32_t yearText;
    int32_t percent;
    uint32_t pad;
    int64_t month;
    uint8_t changed;
    uint8_t pad2[7];
};
static_assert(sizeof(BudgetRow) == 0x28);
constexpr std::size_t kBudgetRows = 7;

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

// The game's inlined reference holder: assign.
void Assign(void*& holder, void* object)
{
    void* const old = holder;
    if (object != old) {
        if (object)
            AddRef(object);
        holder = object;
        if (old)
            Release(old);
    }
}

void DestroyString(GameString& text)
{
    char* const begin = text.body.begin;
    text.vtable = addr::SC4String_Vtable;
    if (begin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(begin));
}

// The runtime's 64-bit multiply (0x009F0D10) by twelve.
int64_t Year(int64_t month)
{
    return static_cast<int64_t>(static_cast<uint64_t>(month) * 12u);
}

int64_t Times(int64_t value, uint32_t factor)
{
    return static_cast<int64_t>(static_cast<uint64_t>(value) * factor);
}

void* NewMessage()
{
    void* const memory = Cdecl<void*>(addr::RZ_operator_new, uint32_t{0x2C});
    return memory ? This<void*>(addr::cGZMessage_cGZMessage, memory) : nullptr;
}

// Tells the budget simulator to open the other budget window.
void SendBudgetCommand(uint32_t which)
{
    void* const server = Global<void*>(addr::SC4_MessageServer);
    void* const message = NewMessage();
    AddRef(message);
    Virtual<void>(message, 0x14, kBudgetCommand);
    Virtual<void>(message, 0x2C, which);
    Virtual<void>(server, 0xC, message);
    Release(message);
}

// A child window's text set to an amount of money (0x007639D0, 0x00764150).
void SetMoney(void* windowGen, uint32_t id, int64_t amount)
{
    void* const window = Virtual<void*>(windowGen, 0xC);
    if (!window)
        return;
    void* const child = Virtual<void*>(window, 0x88, id);
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

// A window taken off the screen through its window manager.
void DestroyWindow(void* windowGen)
{
    void* const manager = Virtual<void*>(Virtual<void*>(windowGen, 0xC), 0x18);
    if (manager) {
        void* const window = Virtual<void*>(windowGen, 0xC);
        Virtual<void>(manager, 0x5C, window);
    }
}

// The gauge.

// The game's 0x00762B20. `this` is the gauge (slot 12 of vtable 0x00AB4900).
// Sets the value, kept within the range, and updates the picture when it
// changed.
void __fastcall GaugeSetValue(char* self, void*, float value)
{
    const float low = At<float>(self, gauge::kLow);
    const float high = At<float>(self, gauge::kHigh);
    if (value < low)
        value = low;
    else if (value > high)
        value = high;
    if (!(At<float>(self, gauge::kValue) == value)) {
        At<float>(self, gauge::kValue) = value;
        This<void>(addr::SC4UI_GaugeUpdate, self);
    }
}

// The game's 0x00762B80. `this` is the gauge. The deleting destructor.
char* __fastcall GaugeDelete(char* self, void*, uint32_t flags)
{
    This<void>(addr::SC4UI_GaugeDestruct, self);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// The advice popup.

// The game's 0x00762BA0. `this` is the popup (slot 0).
uint8_t __fastcall PopupQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    if (iid != 1 && iid != 0x2A5A754D && iid != 0x452294AA)
        return 0;
    *out = self;
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// The game's 0x00762BD0. `this` is the popup (slot 2).
uint32_t __fastcall PopupRelease(char* self, void*)
{
    const uint32_t references = At<uint32_t>(self, popup::kReferences);
    if (references > 1) {
        At<uint32_t>(self, popup::kReferences) = references - 1;
        return references - 1;
    }
    Virtual<void*>(self, 0x10, uint32_t{1});
    return 0;
}

// The game's 0x00762BF0. `this` is the popup (slot 5). Registers for the
// three messages and asks itself for the first popup.
uint8_t __fastcall PopupInit(char* self, void*)
{
    if (!At<uint8_t>(self, popup::kRegistered)) {
        void* const server = Global<void*>(addr::SC4_MessageServer);
        if (server) {
            Virtual<void>(server, 0x14, static_cast<void*>(self), kAdviceAnnounced);
            Virtual<void>(server, 0x14, static_cast<void*>(self), kShowNext);
            Virtual<void>(server, 0x14, static_cast<void*>(self), kCityClosing);
        }
        At<uint8_t>(self, popup::kRegistered) = 1;
        void* const message = NewMessage();
        AddRef(message);
        Virtual<void>(message, 0x14, kShowNext);
        Virtual<void>(Global<void*>(addr::SC4_MessageServer), 0x1C, message, static_cast<void*>(self));
        Release(message);
    }
    return At<uint8_t>(self, popup::kRegistered);
}

// The game's 0x00762C80. `this` is the popup (slot 6).
uint8_t __fastcall PopupShutdown(char* self, void*)
{
    if (At<uint8_t>(self, popup::kRegistered)) {
        void* const server = Global<void*>(addr::SC4_MessageServer);
        if (server) {
            Virtual<void>(server, 0x18, static_cast<void*>(self), kAdviceAnnounced);
            Virtual<void>(server, 0x18, static_cast<void*>(self), kShowNext);
            Virtual<void>(server, 0x18, static_cast<void*>(self), kCityClosing);
        }
        At<uint8_t>(self, popup::kRegistered) = 0;
    }
    return 1;
}

// The game's 0x00762CD0. `this` is the popup. Closes the popup window: stops
// the head, drops the window's advice, ends the modal state and destroys the
// window. The head is not checked for null, as in the original.
void __fastcall PopupClose(char* self, void*, void* window)
{
    Virtual<void>(At<void*>(self, popup::kHead), 0x18, uint32_t{0});
    Clear(At<void*>(self, popup::kHead));
    Virtual<void>(window, 0x1C8, kAdviceParam, static_cast<void*>(nullptr));
    void* manager = Cdecl<void*>(addr::SC4_GetWinManager);
    Virtual<void>(manager, 0x24, uint32_t{0xF}, window, uint32_t{0});
    manager = Cdecl<void*>(addr::SC4_GetWinManager);
    Virtual<void>(manager, 0x5C, window);
}

// The game's 0x00762D30. `this` is the popup's window procedure part (+4;
// slot 3 of vtable 0x00AB4954). The popup closes when its advice is no longer
// current, on its button, and on a link in the text, which is followed first.
uint8_t __fastcall PopupWinMessage(char* part, void*, void* window, const WinMessage* message)
{
    char* const self = part - popup::kWinProc;
    void* variant = nullptr;
    if (Virtual<uint8_t>(window, 0x1C4, kAdviceParam, &variant)) {
        void* advice = nullptr;
        if (Virtual<uint8_t>(variant, 0xF0, kAdviceIid, &advice)) {
            if (!Virtual<uint8_t>(advice, 0x3C, int32_t{2}) || Virtual<uint8_t>(advice, 0x3C, int32_t{8})) {
                PopupClose(self, nullptr, window);
                if (advice)
                    Release(advice);
                if (variant)
                    Release(variant);
                return 1;
            }
        }
        if (advice)
            Release(advice);
    }

    uint8_t result = 0;
    const uint32_t type = message->type;
    if (type == 0xA2BF8AD6 || type == 0xA2BF8AD5) {
        result = 1;
    } else if (type == 3) {
        if (message->a == 0x287259F6) {
            PopupClose(self, nullptr, window);
            result = 1;
        }
    } else if (type == 0xE4A85D91) {
        uint32_t handler = 0; // an object without fields
        const uint8_t followed = This<uint8_t>(addr::SC4UI_FollowLink, &handler, message->b, uint32_t{0});
        result = followed ? 0 : 1;
        PopupClose(self, nullptr, window);
    }
    if (variant)
        Release(variant);
    return result;
}

// The game's 0x00762E70. `this` is the popup's memory (0x18 bytes).
char* __fastcall PopupConstruct(char* self, void*)
{
    At<uint32_t>(self, popup::kWinProc) = addr::SC4NewsTicker_WinProcBaseVtable;
    At<uint32_t>(self, 0x0) = addr::SC4UI_AdvicePopupVtable;
    At<uint32_t>(self, popup::kWinProc) = addr::SC4UI_AdvicePopupVtable4;
    At<uint32_t>(self, popup::kReferences) = 0;
    At<uint8_t>(self, popup::kRegistered) = 0;
    At<void*>(self, popup::kList) = nullptr;
    void** const node = Cdecl<void**>(addr::SC4_stl_allocate, uint32_t{0xC});
    node[0] = node;
    node[1] = node;
    At<void*>(self, popup::kList) = node;
    At<void*>(self, popup::kHead) = nullptr;
    return self;
}

// The game's 0x00762EB0. `this` is the window procedure part (+4).
uint8_t __fastcall PopupQueryInterface4(char* part, void*, uint32_t iid, void** out)
{
    return PopupQueryInterface(part - popup::kWinProc, nullptr, iid, out);
}

// The game's 0x00762EC0. `this` is the window procedure part (+4).
uint32_t __fastcall PopupRelease4(char* part, void*)
{
    return PopupRelease(part - popup::kWinProc, nullptr);
}

// The game's 0x00762ED0. `this` is the popup (slot 4). The deleting
// destructor.
char* __fastcall PopupDelete(char* self, void*, uint32_t flags)
{
    At<uint32_t>(self, 0x0) = addr::SC4UI_AdvicePopupVtable;
    At<uint32_t>(self, popup::kWinProc) = addr::SC4UI_AdvicePopupVtable4;
    if (void* const head = At<void*>(self, popup::kHead))
        Release(head);
    void** const list = &At<void*>(self, popup::kList);
    This<void>(addr::SC4RefList_Clear, list);
    if (void* const node = *list)
        Cdecl<void>(addr::SC4_Free, node);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// The game's 0x00762F20. `this` is the popup. Fills a popup window for an
// advice: the HTML of title and message in the two text styles 0x4A809914 and
// 0x4A809915 (colours by mood), the window grown to the text up to 450 high
// and centred in its parent, the advice kept as a window parameter, and the
// advisor's head started on pane 4.
void __fastcall PopupFill(char* self, void*, void* window, void* advice)
{
    if (!window || !advice)
        return;

    void* html = nullptr;
    if (Virtual<uint8_t>(window, 0x94, uint32_t{2}, kHtmlIid, &html)) {
        GameString page;
        This<void*>(addr::SC4String_SC4String, &page);
        void* const titleStyle = Virtual<void*>(Cdecl<void*>(addr::SC4_Singleton913C72), 0x14, 0x4A809914u);
        void* const messageStyle = Virtual<void*>(Cdecl<void*>(addr::SC4_Singleton913C72), 0x14, 0x4A809915u);
        GameString titleFace;
        This<void*>(addr::SC4String_FromText, &titleFace, "Arta");
        GameString messageFace;
        This<void*>(addr::SC4String_FromText, &messageFace, "Arta");
        uint8_t titleBold = 0;
        uint8_t messageBold = 0;
        uint32_t titleSize = 4;
        uint32_t messageSize = 3;
        if (titleStyle) {
            Virtual<void>(titleStyle, 0x14, &titleFace);
            titleBold = Virtual<uint8_t>(titleStyle, 0x18, uint32_t{1});
            titleSize = (Virtual<uint32_t>(titleStyle, 0x28) * 4 + 8) / 18;
        }
        if (messageStyle) {
            Virtual<void>(messageStyle, 0x14, &messageFace);
            messageBold = Virtual<uint8_t>(messageStyle, 0x18, uint32_t{1});
            messageSize = (Virtual<uint32_t>(messageStyle, 0x28) * 4 + 8) / 18;
        }

        // The text window's fill colour: blue, green, red at rising addresses.
        uint8_t fill[8] = {};
        Virtual<void>(Virtual<void*>(html, 0xC), 0x1A0, &fill[2], &fill[1], &fill[0]);
        Virtual<void>(html, 0x70, uint32_t{0x40});

        uint32_t text[3];
        uint32_t link[3];
        if (Virtual<int32_t>(advice, 0x18) < 3) {
            text[0] = 0x72;
            text[1] = 0x39;
            text[2] = 0x39;
            link[0] = 0xFF;
            link[2] = 0x00;
        } else {
            text[0] = 0x39;
            text[1] = 0x44;
            text[2] = 0x72;
            link[0] = 0x00;
            link[2] = 0xFF;
        }
        link[1] = 0;
        const char* const messageOpen = messageBold ? "<B>" : "";
        const char* const messageClose = messageBold ? "</B>" : "";
        const char* const titleOpen = titleBold ? "<B>" : "";
        const char* const titleClose = titleBold ? "</B>" : "";

        void* const messageText = Virtual<void*>(advice, 0x50);
        void* const titleText = Virtual<void*>(advice, 0x48);
        const char* const messageChars = Virtual<const char*>(messageText, 0x14);
        const char* const messageFaceChars = This<const char*>(addr::cRZBaseString_Data, &messageFace);
        const char* const titleChars = Virtual<const char*>(titleText, 0x14);
        const char* const titleFaceChars = This<const char*>(addr::cRZBaseString_Data, &titleFace);
        Cdecl<void*>(addr::cRZBaseString_Sprintf, &page,
                     "<HTML><HEAD></HEAD><BODY TEXT=\"#%02x%02x%02x\" BGCOLOR=\"#%02x%02x%02x\" "
                     "LINK=\"#%02x%02x%02x\"><P><FONT FACE=\"%s\" SIZE=%d>%s%s%s</FONT></P>"
                     "<P><FONT FACE=\"%s\" SIZE=%d>%s%s%s</FONT></P></BODY></HTML>",
                     text[0], text[1], text[2], uint32_t{fill[2]}, uint32_t{fill[1]}, uint32_t{fill[0]},
                     link[0], link[1], link[2], titleFaceChars, titleSize, titleOpen, titleChars, titleClose,
                     messageFaceChars, messageSize, messageOpen, messageChars, messageClose);

        // The control takes the string's body, not the string.
        GameString base;
        void* const basePointer = This<void*>(addr::SC4String_FromText, &base, "sc4://");
        Virtual<void>(html, 0x58, &page.body, basePointer);
        DestroyString(base);

        // The text's size; the frame leaves room for more than two numbers.
        int32_t size[6];
        Virtual<void>(html, 0x28, &size[0]);
        const int32_t needed = size[1] + 0x14;
        const int32_t* const area = Virtual<const int32_t*>(Virtual<void*>(html, 0xC), 0xC0);
        const int32_t left = area[0];
        const int32_t top = area[1];
        const int32_t right = area[2];
        const int32_t bottom = area[3];
        const int32_t height = bottom - top;
        if (needed > height) {
            int32_t grow = top - bottom + needed;
            if (Virtual<int32_t>(window, 0xA8) + grow > 0x1C2)
                grow = 0x1C2 - Virtual<int32_t>(window, 0xA8);
            const int32_t windowHeight = Virtual<int32_t>(window, 0xA8) + grow;
            const int32_t windowWidth = Virtual<int32_t>(window, 0xA4);
            Virtual<void>(window, 0xD4, windowWidth, windowHeight);
            void* const htmlWindow = Virtual<void*>(html, 0xC);
            Virtual<void>(htmlWindow, 0xD4, right - left, height + grow);
            void* const button = Virtual<void*>(window, 0x8C, uint32_t{5});
            if (button) {
                const int32_t buttonTop = Virtual<int32_t>(button, 0xB0) + grow;
                const int32_t buttonLeft = Virtual<int32_t>(button, 0xAC);
                Virtual<void>(button, 0xE0, buttonLeft, buttonTop);
            }
        }

        DestroyString(messageFace);
        DestroyString(titleFace);
        DestroyString(page);
    }

    void* const parent = Virtual<void*>(window, 0x2C);
    if (parent) {
        const int32_t parentHeight = Virtual<int32_t>(parent, 0xA8);
        const int32_t y = (parentHeight - Virtual<int32_t>(window, 0xA8)) / 2;
        const int32_t parentWidth = Virtual<int32_t>(parent, 0xA4);
        const int32_t x = (parentWidth - Virtual<int32_t>(window, 0xA4)) / 2;
        Virtual<void>(window, 0xE0, x, y);
    }

    void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x14});
    void* const variant = memory ? This<void*>(addr::SC4ScriptNumber_FromObject, memory, advice) : nullptr;
    Virtual<void>(window, 0x1C8, kAdviceParam, variant);

    void* pane = nullptr;
    if (Virtual<uint8_t>(window, 0x94, uint32_t{4}, kWinGenIid, &pane)) {
        void* const headMemory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0xF8});
        void* made = nullptr;
        if (headMemory)
            made = This<void*>(addr::SC4UI_F420B70, headMemory, Virtual<uint32_t>(advice, 0x2C));
        void*& head = At<void*>(self, popup::kHead);
        This<void>(addr::SC4RefPtr_Assign, &head, made);
        Virtual<void>(head, 0x18, uint32_t{1});
        void* const shown = head;
        const int32_t mood = Virtual<int32_t>(advice, 0x18);
        Virtual<void>(shown, 0x24, mood);
        Virtual<void>(pane, 0x1C, head);
    }
    if (pane)
        Release(pane);
    if (html)
        Release(html);
}

bool ListEmpty(char* self)
{
    void** const sentinel = At<void**>(self, popup::kList);
    return sentinel[0] == sentinel;
}

// The game's 0x00763450. `this` is the popup (slot 3). An announced advice
// without flags 0x6000 joins the list (flag 0x4000 set while it waits);
// "show next" takes the first one and, when popups are allowed and its
// priority reaches the advisor system's threshold, runs its window as a modal
// one; the city closing empties the list.
uint8_t __fastcall PopupDoMessage(char* self, void*, void* message)
{
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    if (type == kCityClosing) {
        This<void>(addr::SC4RefList_Clear, self + popup::kList);
        return 1;
    }

    if (type == kAdviceAnnounced) {
        void* const subject = Virtual<void*>(message, 0x80);
        if (!subject)
            return 1;
        void* advice = nullptr;
        if (Virtual<uint8_t>(subject, 0x0, kAdviceIid, &advice)) {
            if (!Virtual<uint8_t>(advice, 0x3C, int32_t{0x6000})) {
                This<void>(addr::SC4RefList_PushBack2, self + popup::kList, &advice);
                Virtual<void>(advice, 0x38, int32_t{0x4000}, uint32_t{1});
                void* next = nullptr;
                This<void*>(addr::SC4RefHolder_SC4RefHolder, &next, NewMessage());
                Virtual<void>(next, 0x14, kShowNext);
                Virtual<void>(Global<void*>(addr::SC4_MessageServer), 0x1C, next, static_cast<void*>(self));
                Release(next);
            }
        }
        if (advice)
            Release(advice);
        return 1;
    }

    if (type != kShowNext)
        return 1;
    if (ListEmpty(self))
        return 1;
    void* const city = Global<void*>(addr::SC4_City);
    if (!city)
        return 1;
    void* const advisors = Virtual<void*>(city, 0x144);
    if (!advisors)
        return 1;

    if (Cdecl<uint8_t>(addr::SC4UI_CanShowPopup)) {
        void** const first = static_cast<void**>(At<void**>(self, popup::kList)[0]);
        void* advice = nullptr;
        This<void*>(addr::SC4RefPtr_Copy, &advice, &first[2]);
        This<void>(addr::SC4RefList_PopFront, self + popup::kList);
        void* const tutorial = Virtual<void*>(city, 0x148);
        void* const service = Global<void*>(addr::SC4_Service43D44);
        do {
            if (!Cdecl<uint8_t>(addr::SC4UI_Service43C94Slot23))
                break;
            if (!At<uint8_t>(Global<void*>(addr::SC4_Options43CD8), 0xF0B))
                break;
            const int32_t priority = Virtual<int32_t>(advice, 0x1C);
            if (priority < Virtual<int32_t>(advisors, 0x54))
                break;
            if (tutorial && Virtual<uint8_t>(tutorial, 0x20))
                break;
            if (service && Virtual<uint8_t>(service, 0x34))
                break;

            void* gen = nullptr;
            uint32_t key[3] = {0, kUiGroup, 0};
            key[2] = 0x4A5A89D4u + (Virtual<int32_t>(advice, 0x18) >= 3 ? 1u : 0u);
            void** const out = This<void**>(addr::SC4RefPtr_Reset, &gen);
            if (Cdecl<uint8_t>(addr::SC4UI_LoadWindow, &key[0], static_cast<void*>(nullptr), 0x4A9DB60Cu, kWinGenIid,
                               out)) {
                uint8_t guard[4];
                This<void*>(addr::SC4ModalGuard_Construct, &guard[0], uint32_t{0}, uint32_t{0}, uint32_t{1},
                            uint32_t{1});
                Virtual<void>(gen, 0x1C, static_cast<void*>(self + popup::kWinProc));
                PopupFill(self, nullptr, Virtual<void*>(gen, 0xC), advice);
                {
                    void* const target = Global<void*>(addr::SC4_Service43CE0);
                    void* const window = Virtual<void*>(gen, 0xC);
                    Virtual<void>(target, 0x28, window, uint32_t{1});
                }
                {
                    void* const manager = Cdecl<void*>(addr::SC4_GetWinManager);
                    void* const window = Virtual<void*>(gen, 0xC);
                    Virtual<void>(manager, 0xA4, window); // modal: returns when the popup closes
                }
                {
                    void* const target = Global<void*>(addr::SC4_Service43CE0);
                    void* const window = Virtual<void*>(gen, 0xC);
                    Virtual<void>(target, 0x18, window);
                }
                {
                    void* const manager = Cdecl<void*>(addr::SC4_GetWinManager);
                    Virtual<void>(manager, 0x94, Global<void*>(addr::SC4_Service43DC8));
                }
                if (Virtual<uint8_t>(advice, 0x3C, int32_t{0x1000}))
                    Virtual<void>(advice, 0x38, int32_t{8}, uint32_t{1});
                This<void>(addr::SC4ModalGuard_Destruct, &guard[0]);
            }
            if (gen)
                Release(gen);
        } while (false);
        Virtual<void>(advice, 0x38, int32_t{0x4000}, uint32_t{0});
        Release(advice);
    }

    if (!ListEmpty(self))
        Virtual<void>(Global<void*>(addr::SC4_MessageServer), 0x1C, message, static_cast<void*>(self));
    return 1;
}

// The thunks the dialogs share: QueryInterface, AddRef and Release from the
// parts at +8 (0x00763790, 0x007637A0, 0x007637B0) and at +0xC (0x007637C0,
// 0x007637D0, 0x007637E0).

template <std::size_t Offset>
uint8_t __fastcall DialogQueryInterface(char* part, void*, uint32_t iid, void** out)
{
    return This<uint8_t>(addr::SC4UI_F774560, part - Offset, iid, out);
}

template <std::size_t Offset>
uint32_t __fastcall DialogAddRef(char* part, void*)
{
    return This<uint32_t>(addr::cRZBaseUnknown_AddRefJump, part - Offset);
}

template <std::size_t Offset>
uint32_t __fastcall DialogRelease(char* part, void*)
{
    return This<uint32_t>(addr::cRZBaseUnknown_ReleaseJump, part - Offset);
}

// The small budget window.

BudgetRow* Rows()
{
    return reinterpret_cast<BudgetRow*>(addr::SC4UI_BudgetRows);
}

// The game's 0x007639D0. `this` is the small budget window.
void __fastcall SmallSetMoney(char* self, void*, uint32_t id, int64_t amount)
{
    SetMoney(At<void*>(self, small::kWindow), id, amount);
}

// The game's 0x00763A70. `this` is the small budget window. The totals under
// the sliders: expenses by month and year, what is left of the income, and
// the funds at the end of the year.
void __fastcall SmallTotals(char* self, void*)
{
    const BudgetRow* const rows = Rows();
    int64_t month = 0;
    for (std::size_t i = 0; i < kBudgetRows; ++i)
        month += rows[i].month;
    month += At<int64_t>(self, small::kOther);
    const int64_t year = Year(month);
    SmallSetMoney(self, nullptr, 0x242, month);
    SmallSetMoney(self, nullptr, 0x243, year);

    int64_t monthLeft = 0;
    if (void* const budget = At<void*>(self, small::kBudget))
        monthLeft = Virtual<int64_t>(budget, 0x40) - month;
    SmallSetMoney(self, nullptr, 0x244, monthLeft);

    int64_t yearLeft = 0;
    if (void* const budget = At<void*>(self, small::kBudget))
        yearLeft = Virtual<int64_t>(budget, 0x44) - year;
    SmallSetMoney(self, nullptr, 0x245, yearLeft);

    int64_t funds = 0;
    if (void* const budget = At<void*>(self, small::kBudget))
        funds = Virtual<int64_t>(budget, 0x18) + yearLeft;
    SmallSetMoney(self, nullptr, 0x247, funds);
}

// The game's 0x00763B90. `this` is the small budget window. A slider moved:
// its department's row takes the new percentage and cost. The child window
// and the budget simulator are not checked for null, as in the original.
void __fastcall SmallOnSlider(char* self, void*, uint32_t id)
{
    void* const window = Virtual<void*>(At<void*>(self, small::kWindow), 0xC);
    void* const child = Virtual<void*>(window, 0x88, id);
    void* slider = nullptr;
    if (!Virtual<uint8_t>(child, 0x0, kSliderIid, &slider)) {
        if (slider)
            Release(slider);
        return;
    }
    const uint32_t percent = Virtual<uint32_t>(slider, 0x10);
    BudgetRow* row = nullptr;
    for (std::size_t i = 0; i < kBudgetRows; ++i) {
        if (Rows()[i].slider == id) {
            row = &Rows()[i];
            break;
        }
    }
    if (row) {
        void* const department = Virtual<void*>(At<void*>(self, small::kBudget), 0x70, row->department);
        if (department) {
            row->changed = 1;
            row->percent = static_cast<int32_t>(percent);
            const int64_t full = Virtual<int64_t>(department, 0x38);
            row->month = Times(full, percent) / 100;
            SmallSetMoney(self, nullptr, row->monthText, row->month);
            SmallSetMoney(self, nullptr, row->yearText, Year(row->month));
            SmallTotals(self, nullptr);
        }
    }
    if (slider)
        Release(slider);
}

// The game's 0x00763CB0. `this` is the small budget window. Shows a row: the
// slider's position and the two amounts. The department is looked up and not
// used, as in the original.
void __fastcall SmallShowRow(char* self, void*, BudgetRow* row)
{
    Virtual<void*>(At<void*>(self, small::kBudget), 0x70, row->department);
    void* const window = Virtual<void*>(At<void*>(self, small::kWindow), 0xC);
    void* const child = Virtual<void*>(window, 0x88, row->slider);
    if (child) {
        void* slider = nullptr;
        if (Virtual<uint8_t>(child, 0x0, kSliderIid, &slider))
            Virtual<void>(slider, 0x14, row->percent);
        if (slider)
            Release(slider);
    }
    SmallSetMoney(self, nullptr, row->monthText, row->month);
    SmallSetMoney(self, nullptr, row->yearText, Year(row->month));
    SmallTotals(self, nullptr);
}

// The game's 0x00763D50. `this` is the small budget window. Reads the budget
// simulator into the table and shows everything. A department that is
// missing is not checked for, as in the original.
void __fastcall SmallRefresh(char* self, void*)
{
    void* const budget = Virtual<void*>(Global<void*>(addr::SC4_City), 0xD0);
    Assign(At<void*>(self, small::kBudget), budget);
    At<int64_t>(self, small::kOther) = Virtual<int64_t>(Global<void*>(addr::SC4_Service43D90), 0x58);

    for (std::size_t i = 0; i < kBudgetRows; ++i) {
        BudgetRow* const row = &Rows()[i];
        void* const department = Virtual<void*>(At<void*>(self, small::kBudget), 0x70, row->department);
        const int64_t full = Virtual<int64_t>(department, 0x38);
        const float* const share = Virtual<const float*>(department, 0x28);
        row->changed = 0;
        row->percent = static_cast<int32_t>(static_cast<int64_t>(static_cast<double>(*share) * 100.0));
        row->month = static_cast<int64_t>(static_cast<double>(full) * static_cast<double>(*share));
        SmallShowRow(self, nullptr, row);
    }

    int64_t yearIncome = 0;
    if (At<void*>(self, small::kBudget)) {
        SmallSetMoney(self, nullptr, 0x246, Virtual<int64_t>(At<void*>(self, small::kBudget), 0x18));
        SmallSetMoney(self, nullptr, 0x240, Virtual<int64_t>(At<void*>(self, small::kBudget), 0x40));
        yearIncome = Virtual<int64_t>(At<void*>(self, small::kBudget), 0x44);
    } else {
        SmallSetMoney(self, nullptr, 0x246, 0);
        SmallSetMoney(self, nullptr, 0x240, 0);
    }
    SmallSetMoney(self, nullptr, 0x241, yearIncome);
    SmallSetMoney(self, nullptr, 0x229, At<int64_t>(self, small::kOther));
    SmallSetMoney(self, nullptr, 0x230, Year(At<int64_t>(self, small::kOther)));
    SmallTotals(self, nullptr);

    void* const window = Virtual<void*>(At<void*>(self, small::kWindow), 0xC);
    void* const button = Virtual<void*>(window, 0x88, uint32_t{0x101});
    Virtual<void>(button, 0x110, uint32_t{2}, uint32_t{0});
}

// The game's 0x007637F0. `this` is the small budget window. Gives the
// changed percentages to their departments and the expense totals to the
// budget simulator.
void __fastcall SmallApply(char* self, void*)
{
    int64_t month = 0;
    for (std::size_t i = 0; i < kBudgetRows; ++i) {
        const BudgetRow* const row = &Rows()[i];
        month += row->month;
        if (!row->changed)
            continue;
        void* const department = Virtual<void*>(At<void*>(self, small::kBudget), 0x70, row->department);
        if (department) {
            float share = static_cast<float>(static_cast<double>(row->percent) * static_cast<double>(0.01f));
            Virtual<void>(department, 0x2C, &share, uint32_t{0});
        }
    }
    month += At<int64_t>(self, small::kOther);
    if (void* const budget = At<void*>(self, small::kBudget)) {
        Virtual<void>(budget, 0x50, month);
        void* const again = At<void*>(self, small::kBudget);
        Virtual<void>(again, 0x54, Year(month));
    }
}

// The game's 0x00763880. `this` is the small budget window. Destroys the
// window and lets go of everything; the result is whatever the last Release
// returned.
uint32_t __fastcall SmallClose(char* self, void*)
{
    Virtual<uint32_t>(self, 0x4);
    DestroyWindow(At<void*>(self, small::kWindow));
    Clear(At<void*>(self, small::kWindow));
    Clear(At<void*>(self, small::kBudget));
    return Virtual<uint32_t>(self, 0x8);
}

// The game's 0x007638E0 (BudgetWindow::Small). `this` is the window object's
// memory (0x28 bytes). Loads the window layout 0x691A0FB4 and becomes its
// window procedure.
char* __fastcall SmallConstruct(char* self, void*)
{
    This<void*>(addr::cRZBaseUnknown_Construct, self);
    At<uint32_t>(self, small::kWinProc) = addr::SC4NewsTicker_WinProcBaseVtable;
    At<uint32_t>(self, small::kFilter) = addr::cSC4TrafficSimulator_VtableBase4;
    At<uint32_t>(self, 0x0) = addr::SC4UI_BudgetSmallVtable;
    At<uint32_t>(self, small::kWinProc) = addr::SC4UI_BudgetSmallVtable8;
    At<uint32_t>(self, small::kFilter) = addr::SC4UI_BudgetSmallVtableC;
    At<void*>(self, small::kWindow) = nullptr;
    At<void*>(self, small::kBudget) = nullptr;
    At<uint8_t>(self, small::kFlag) = 1;
    At<int64_t>(self, small::kOther) = 0;
    uint32_t key[3] = {0, kUiGroup, 0x691A0FB4};
    void** const gen = &At<void*>(self, small::kWindow);
    Clear(*gen);
    if (Cdecl<uint8_t>(addr::SC4UI_LoadWindow, &key[0], static_cast<void*>(nullptr), 0x10000000u, kWinGenIid, gen))
        Virtual<void>(*gen, 0x1C, static_cast<void*>(self + small::kWinProc));
    return self;
}

// The game's 0x00763980. `this` is the small budget window (slot 3). The
// deleting destructor.
char* __fastcall SmallDelete(char* self, void*, uint32_t flags)
{
    At<uint32_t>(self, 0x0) = addr::SC4UI_BudgetSmallVtable;
    At<uint32_t>(self, small::kWinProc) = addr::SC4UI_BudgetSmallVtable8;
    At<uint32_t>(self, small::kFilter) = addr::SC4UI_BudgetSmallVtableC;
    if (void* const budget = At<void*>(self, small::kBudget))
        Release(budget);
    if (void* const gen = At<void*>(self, small::kWindow))
        Release(gen);
    This<void>(addr::cRZBaseUnknown_Destruct, self);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// The game's 0x00763EE0. `this` is the window procedure part (+8; slot 3 of
// vtable 0x00AB499C). Button 0x111 applies and closes, 0x102 asks for the
// large window first; a slider message updates its row; 0xA2BF8AD5 (the
// window is up) fills it.
uint8_t __fastcall SmallWinMessage(char* part, void*, void* window, const WinMessage* message)
{
    char* const self = part - small::kWinProc;
    if (window != Virtual<void*>(At<void*>(self, small::kWindow), 0xC))
        return 0;
    if (message->type == 0xA2BF8AD5) {
        SmallRefresh(self, nullptr);
        return 0;
    }
    if (message->type != 3)
        return 0;
    if (message->a == 0x887113A3) {
        SmallOnSlider(self, nullptr, message->b);
        return 0;
    }
    if (message->a != 0x287259F6)
        return 0;
    if (message->b == 0x102) {
        SendBudgetCommand(0xC92230EE);
    } else if (message->b != 0x111) {
        return 0;
    }
    SmallApply(self, nullptr);
    SmallClose(self, nullptr);
    return 1;
}

// The large budget window.

// The game's 0x00764150. `this` is the large budget window.
void __fastcall LargeSetMoney(char* self, void*, uint32_t id, int64_t amount)
{
    SetMoney(At<void*>(self, large::kWindow), id, amount);
}

// The game's 0x007641F0. `this` is the large budget window. Gives the income
// totals to the budget simulator and shows them with what is left after the
// expenses and the funds at the end of the year.
void __fastcall LargeTotals(char* self, void*)
{
    const int64_t month = At<int64_t>(self, large::kThird) + At<int64_t>(self, large::kSecond)
        + At<int64_t>(self, large::kFirst) + At<int64_t>(self, large::kOther);
    if (void* const budget = At<void*>(self, large::kBudget))
        Virtual<void>(budget, 0x58, month);
    LargeSetMoney(self, nullptr, 0x32, month);
    const int64_t year = Year(month);
    if (void* const budget = At<void*>(self, large::kBudget))
        Virtual<void>(budget, 0x5C, year);
    LargeSetMoney(self, nullptr, 0x33, year);

    int64_t monthLeft = 0;
    if (void* const budget = At<void*>(self, large::kBudget)) {
        const int64_t expense = Virtual<int64_t>(budget, 0x38);
        monthLeft = Virtual<int64_t>(budget, 0x40) - expense;
    }
    LargeSetMoney(self, nullptr, 0x36, monthLeft);

    int64_t yearLeft = 0;
    if (void* const budget = At<void*>(self, large::kBudget)) {
        const int64_t expense = Virtual<int64_t>(budget, 0x3C);
        yearLeft = Virtual<int64_t>(budget, 0x44) - expense;
    }
    LargeSetMoney(self, nullptr, 0x37, yearLeft);

    int64_t funds = 0;
    if (void* const budget = At<void*>(self, large::kBudget))
        funds = Virtual<int64_t>(budget, 0x18) + yearLeft;
    LargeSetMoney(self, nullptr, 0x39, funds);
}

// What belongs to one of the three spinners.
struct TaxLine {
    int32_t group;
    uint32_t spinner;
    uint32_t monthText;
    uint32_t yearText;
    std::size_t field;
};

const TaxLine* LineBySpinner(uint32_t id)
{
    static const TaxLine lines[3] = {
        {1, 0x501, 0x15, 0x16, large::kFirst},
        {4, 0x502, 0x17, 0x18, large::kSecond},
        {10, 0x503, 0x19, 0x20, large::kThird},
    };
    for (const TaxLine& line : lines) {
        if (line.spinner == id)
            return &line;
    }
    return nullptr;
}

const TaxLine* LineByGroup(int32_t group)
{
    for (uint32_t id = 0x501; id <= 0x503; ++id) {
        const TaxLine* const line = LineBySpinner(id);
        if (line->group == group)
            return line;
    }
    return nullptr;
}

// The game's 0x00764300. `this` is the large budget window. A spinner moved:
// the group's tax rate is set at once and its income shown. The child window
// is not checked for null, as in the original.
void __fastcall LargeOnSpinner(char* self, void*, uint32_t id)
{
    void* const window = Virtual<void*>(At<void*>(self, large::kWindow), 0xC);
    void* const child = Virtual<void*>(window, 0x88, id);
    void* spinner = nullptr;
    if (Virtual<uint8_t>(child, 0x0, kSpinnerIid, &spinner)) {
        const uint32_t value = Virtual<uint32_t>(spinner, 0x20);
        const TaxLine* const line = LineBySpinner(id);
        if (!line) {
            if (spinner)
                Release(spinner);
            return;
        }
        void* budget = At<void*>(self, large::kBudget);
        Virtual<void>(budget, 0x94, line->group, static_cast<float>(static_cast<double>(value)));
        budget = At<void*>(self, large::kBudget);
        const int64_t month = Times(Virtual<int64_t>(budget, 0x98, line->group), value) / 12 / 100;
        At<int64_t>(self, line->field) = month;
        LargeSetMoney(self, nullptr, line->monthText, month);
        budget = At<void*>(self, large::kBudget);
        const int64_t year = Times(Virtual<int64_t>(budget, 0x98, line->group), value) / 100;
        At<int64_t>(self, line->field + 8) = year;
        LargeSetMoney(self, nullptr, line->yearText, year);
        LargeTotals(self, nullptr);
    }
    if (spinner)
        Release(spinner);
}

// The game's 0x007644B0. `this` is the large budget window. Shows a tax
// group: the spinner at the rate and the two amounts. The amounts are shown
// and not kept (the fields are set by the spinner handler only).
void __fastcall LargeShowGroup(char* self, void*, int32_t group)
{
    const TaxLine* const line = LineByGroup(group);
    if (!line)
        return;
    void* const window = Virtual<void*>(At<void*>(self, large::kWindow), 0xC);
    const float rate = static_cast<float>(Virtual<double>(At<void*>(self, large::kBudget), 0x90, group));
    void* const child = Virtual<void*>(window, 0x88, line->spinner);
    if (child) {
        void* spinner = nullptr;
        if (Virtual<uint8_t>(child, 0x0, kSpinnerIid, &spinner))
            Virtual<void>(spinner, 0x24, static_cast<int32_t>(static_cast<int64_t>(rate)));
        if (spinner)
            Release(spinner);
    }
    double value = static_cast<double>(Virtual<int64_t>(At<void*>(self, large::kBudget), 0x98, group));
    value *= static_cast<double>(rate);
    value *= static_cast<double>(0.01f);
    LargeSetMoney(self, nullptr, line->monthText, static_cast<int64_t>(value));
    value = static_cast<double>(Virtual<int64_t>(At<void*>(self, large::kBudget), 0x98, group));
    value *= static_cast<double>(rate);
    value *= static_cast<double>(0.12f);
    LargeSetMoney(self, nullptr, line->yearText, static_cast<int64_t>(value));
    LargeTotals(self, nullptr);
}

// The game's 0x00764600. `this` is the large budget window. Clears the
// amounts and shows everything.
void __fastcall LargeRefresh(char* self, void*)
{
    for (std::size_t at = large::kOther; at < 0x60; at += 8)
        At<int64_t>(self, at) = 0;
    LargeShowGroup(self, nullptr, 1);
    LargeShowGroup(self, nullptr, 4);
    LargeShowGroup(self, nullptr, 10);

    int64_t yearExpense = 0;
    if (At<void*>(self, large::kBudget)) {
        LargeSetMoney(self, nullptr, 0x38, Virtual<int64_t>(At<void*>(self, large::kBudget), 0x18));
        LargeSetMoney(self, nullptr, 0x34, Virtual<int64_t>(At<void*>(self, large::kBudget), 0x38));
        yearExpense = Virtual<int64_t>(At<void*>(self, large::kBudget), 0x3C);
    } else {
        LargeSetMoney(self, nullptr, 0x38, 0);
        LargeSetMoney(self, nullptr, 0x34, 0);
    }
    LargeSetMoney(self, nullptr, 0x35, yearExpense);

    if (void* const service = Global<void*>(addr::SC4_Service43D90)) {
        At<int64_t>(self, large::kOther) = Virtual<int64_t>(service, 0x5C);
        At<int64_t>(self, large::kOtherYear) = Year(At<int64_t>(self, large::kOther));
    }
    LargeSetMoney(self, nullptr, 0x21, At<int64_t>(self, large::kOther));
    LargeSetMoney(self, nullptr, 0x22, At<int64_t>(self, large::kOtherYear));
    LargeTotals(self, nullptr);
}

// The game's 0x00763FD0. `this` is the large budget window. Destroys the
// window and lets go of everything; the result is whatever the last Release
// returned.
uint32_t __fastcall LargeClose(char* self, void*)
{
    Virtual<uint32_t>(self, 0x4);
    DestroyWindow(At<void*>(self, large::kWindow));
    Clear(At<void*>(self, large::kBudget));
    Clear(At<void*>(self, large::kWindow));
    return Virtual<uint32_t>(self, 0x8);
}

// The game's 0x00764030 (BudgetWindow::Large). `this` is the window object's
// memory (0x60 bytes). Loads the window layout 0xE91A0FA3 and becomes its
// window procedure.
char* __fastcall LargeConstruct(char* self, void*)
{
    This<void*>(addr::cRZBaseUnknown_Construct, self);
    At<uint32_t>(self, large::kWinProc) = addr::SC4NewsTicker_WinProcBaseVtable;
    At<uint32_t>(self, large::kFilter) = addr::cSC4TrafficSimulator_VtableBase4;
    At<uint32_t>(self, 0x0) = addr::SC4UI_BudgetLargeVtable;
    At<uint32_t>(self, large::kWinProc) = addr::SC4UI_BudgetLargeVtable8;
    At<uint32_t>(self, large::kFilter) = addr::SC4UI_BudgetLargeVtableC;
    At<uint8_t>(self, large::kFlag) = 1;
    At<void*>(self, large::kWindow) = nullptr;
    At<void*>(self, large::kBudget) = nullptr;
    for (std::size_t at = large::kOther; at < 0x60; at += 8)
        At<int64_t>(self, at) = 0;
    uint32_t key[3] = {0, kUiGroup, 0xE91A0FA3};
    void** const gen = &At<void*>(self, large::kWindow);
    Clear(*gen);
    if (Cdecl<uint8_t>(addr::SC4UI_LoadWindow, &key[0], static_cast<void*>(nullptr), 0x10000000u, kWinGenIid, gen))
        Virtual<void>(*gen, 0x1C, static_cast<void*>(self + large::kWinProc));
    return self;
}

// The game's 0x00764100. `this` is the large budget window (slot 3). The
// deleting destructor.
char* __fastcall LargeDelete(char* self, void*, uint32_t flags)
{
    At<uint32_t>(self, 0x0) = addr::SC4UI_BudgetLargeVtable;
    At<uint32_t>(self, large::kWinProc) = addr::SC4UI_BudgetLargeVtable8;
    At<uint32_t>(self, large::kFilter) = addr::SC4UI_BudgetLargeVtableC;
    if (void* const budget = At<void*>(self, large::kBudget))
        Release(budget);
    if (void* const gen = At<void*>(self, large::kWindow))
        Release(gen);
    This<void>(addr::cRZBaseUnknown_Destruct, self);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// The game's 0x00764700 (it ends at 0x0076481D). `this` is the window
// procedure part (+8; slot 3 of vtable 0x00AB49E0). Button 0x107 closes,
// 0x101 asks for the small window and closes; message 0x682876C9 from a
// spinner (0x501 to 0x503) updates its tax group; 0xA2BF8AD5 (the window is
// up) takes the city's budget simulator and fills the window.
uint8_t __fastcall LargeWinMessage(char* part, void*, void* window, const WinMessage* message)
{
    char* const self = part - large::kWinProc;
    if (window != Virtual<void*>(At<void*>(self, large::kWindow), 0xC))
        return 0;
    if (message->type == 3) {
        if (message->a != 0x287259F6)
            return 0;
        if (message->b == 0x101)
            SendBudgetCommand(0x892230F2);
        else if (message->b != 0x107)
            return 0;
        LargeClose(self, nullptr);
        return 1;
    }
    if (message->type == 0x682876C9) {
        if (message->a >= 0x501 && message->a <= 0x503)
            LargeOnSpinner(self, nullptr, message->a);
        return 0;
    }
    if (message->type == 0xA2BF8AD5) {
        void* const mine = Virtual<void*>(At<void*>(self, large::kWindow), 0xC);
        void* const button = Virtual<void*>(mine, 0x88, uint32_t{0x102});
        Virtual<void>(button, 0x110, uint32_t{2}, uint32_t{0});
        void* const budget = Virtual<void*>(Global<void*>(addr::SC4_City), 0xD0);
        This<void>(addr::SC4RefPtr_Assign, &At<void*>(self, large::kBudget), budget);
        LargeRefresh(self, nullptr);
    }
    return 0;
}

bool InstallDialogsBase(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[40] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F762B20, reinterpret_cast<void*>(&GaugeSetValue)},
        {addr::SC4UI_F762B80, reinterpret_cast<void*>(&GaugeDelete)},
        {addr::SC4UI_F762BA0, reinterpret_cast<void*>(&PopupQueryInterface)},
        {addr::SC4UI_F762BD0, reinterpret_cast<void*>(&PopupRelease)},
        {addr::SC4UI_F762BF0, reinterpret_cast<void*>(&PopupInit)},
        {addr::SC4UI_F762C80, reinterpret_cast<void*>(&PopupShutdown)},
        {addr::SC4UI_F762CD0, reinterpret_cast<void*>(&PopupClose)},
        {addr::SC4UI_F762D30, reinterpret_cast<void*>(&PopupWinMessage)},
        {addr::SC4UI_F762E70, reinterpret_cast<void*>(&PopupConstruct)},
        {addr::SC4UI_F762EB0, reinterpret_cast<void*>(&PopupQueryInterface4)},
        {addr::SC4UI_F762EC0, reinterpret_cast<void*>(&PopupRelease4)},
        {addr::SC4UI_F762ED0, reinterpret_cast<void*>(&PopupDelete)},
        {addr::SC4UI_F762F20, reinterpret_cast<void*>(&PopupFill)},
        {addr::SC4UI_F763450, reinterpret_cast<void*>(&PopupDoMessage)},
        {addr::SC4UI_F763790, reinterpret_cast<void*>(&DialogQueryInterface<0x8>)},
        {addr::SC4UI_F7637A0, reinterpret_cast<void*>(&DialogAddRef<0x8>)},
        {addr::SC4UI_F7637B0, reinterpret_cast<void*>(&DialogRelease<0x8>)},
        {addr::SC4UI_F7637C0, reinterpret_cast<void*>(&DialogQueryInterface<0xC>)},
        {addr::SC4UI_F7637D0, reinterpret_cast<void*>(&DialogAddRef<0xC>)},
        {addr::SC4UI_F7637E0, reinterpret_cast<void*>(&DialogRelease<0xC>)},
        {addr::SC4UI_F7637F0, reinterpret_cast<void*>(&SmallApply)},
        {addr::SC4UI_F763880, reinterpret_cast<void*>(&SmallClose)},
        {addr::BudgetWindow_Small, reinterpret_cast<void*>(&SmallConstruct)},
        {addr::SC4UI_F763980, reinterpret_cast<void*>(&SmallDelete)},
        {addr::SC4UI_F7639D0, reinterpret_cast<void*>(&SmallSetMoney)},
        {addr::SC4UI_F763A70, reinterpret_cast<void*>(&SmallTotals)},
        {addr::SC4UI_F763B90, reinterpret_cast<void*>(&SmallOnSlider)},
        {addr::SC4UI_F763CB0, reinterpret_cast<void*>(&SmallShowRow)},
        {addr::SC4UI_F763D50, reinterpret_cast<void*>(&SmallRefresh)},
        {addr::SC4UI_F763EE0, reinterpret_cast<void*>(&SmallWinMessage)},
        {addr::SC4UI_F763FD0, reinterpret_cast<void*>(&LargeClose)},
        {addr::BudgetWindow_Large, reinterpret_cast<void*>(&LargeConstruct)},
        {addr::SC4UI_F764100, reinterpret_cast<void*>(&LargeDelete)},
        {addr::SC4UI_F764150, reinterpret_cast<void*>(&LargeSetMoney)},
        {addr::SC4UI_F7641F0, reinterpret_cast<void*>(&LargeTotals)},
        {addr::SC4UI_F764300, reinterpret_cast<void*>(&LargeOnSpinner)},
        {addr::SC4UI_F7644B0, reinterpret_cast<void*>(&LargeShowGroup)},
        {addr::SC4UI_F764600, reinterpret_cast<void*>(&LargeRefresh)},
        {addr::SC4UI_F764700, reinterpret_cast<void*>(&LargeWinMessage)},
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

const bool g_dialogsBaseRegistered = registry::Register({
    "ui.dialogs.base",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F763450,
    "Our advice popup and the small and large budget windows (docs/ui.md)",
    false,
    InstallDialogsBase,
});

} // namespace
} // namespace opensc4::ui
