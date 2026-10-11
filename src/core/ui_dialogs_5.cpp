// The fifth part of the dialog range (0x0076ADF0 to 0x0076CC80). It holds
// pieces of three classes:
//
//   - the game options dialog (0x10 bytes, vtable 0x00AB4B64; its start is
//     in the part before): its command handler, Release and window
//     procedure. Five check boxes map to bytes of the options record at
//     0x00B43CD8; Accept writes them, posts message 0x231BBF91 and closes;
//   - the line graph control the graphs window makes (0x260 bytes, vtables
//     0x00AB4D08, 0x00AB4C28 at +0xD8, 0x00AB4B98 at +0x22C; a thin class
//     over the framework's line graph, constructor 0x009BA988): its constructor
//     and deleting destructor;
//   - the graphs window of the city view (vtables 0x00AB4FF4 and 0x00AB4FE0
//     at +4; constructor, message handlers and the series table are in the
//     part after): Init, Shutdown, the two panes, the tab buttons, the time
//     range and the function that hands the chart its numbers from the
//     city's history warehouse, with the copy and range helpers of its
//     series records.
//
// Replacement, off by default:
//
//   ui.dialogs.part5: everything above.
//
// Windows, buttons, texts and the chart controls are the game's, called
// through their vtables.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <utility>

namespace opensc4::ui {
namespace {

namespace options {
constexpr std::size_t kWindow = 0x04;     // the dialog's cIGZWinGen
constexpr std::size_t kRecord = 0x08;     // the options record (the global at 0x00B43CD8)
constexpr std::size_t kReferences = 0x0C;
} // namespace options

namespace chart {
constexpr std::size_t kAxes = 0xD8;   // the part with the scale's setters
constexpr std::size_t kData = 0x22C;  // the part QueryInterface gives for 0x12CF2351
} // namespace chart

namespace graphs {
constexpr std::size_t kWinProc = 0x04;     // the window procedure part
constexpr std::size_t kInited = 0x0C;
constexpr std::size_t kKind = 0x10;        // 1: lines over time, 2: bars of today's values by group
constexpr std::size_t kTab = 0x14;         // the tab button that is down
constexpr std::size_t kWindow = 0x18;
constexpr std::size_t kFirstPane = 0x1C;   // child 0x8A8B5B72 of the window's parent
constexpr std::size_t kSecondPane = 0x20;  // child 0x8A8B5B71
constexpr std::size_t kChart = 0x24;       // the chart control
constexpr std::size_t kOnFirst = 0x29;     // the first pane is the one shown
constexpr std::size_t kSeries = 0x2C;      // vector of Series: begin
constexpr std::size_t kShown = 0x38;       // map of history ID to "shown"
constexpr std::size_t kSeriesCount = 0x44;
constexpr std::size_t kGroupCount = 0x48;
constexpr std::size_t kRange = 0x4C;       // row of the range table
constexpr std::size_t kTop = 0x50;         // float: a fixed top of the scale, or -FLT_MAX
constexpr std::size_t kBottom = 0x54;      // float: a fixed bottom, or FLT_MAX
constexpr std::size_t kStep = 0x58;        // float: a fixed step, or 0
} // namespace graphs

constexpr uint32_t kWinGenIid = 0x5386D516;
constexpr uint32_t kButtonIid = 0x8810;
constexpr uint32_t kTextIid = 0x212CDC1F;
constexpr uint32_t kLineGraphIid = 0x12CF2351;
constexpr uint32_t kBarGraphIid = 0x22CF2351;
constexpr uint32_t kTextGroup = 0x6A231EAA;

constexpr uint32_t kNoTop = 0xFF7FFFFF;    // -FLT_MAX
constexpr uint32_t kNoBottom = 0x7F7FFFFF; // FLT_MAX

constexpr uint32_t kGraphsMessage = 0x66956816; // the message the graphs window listens for
constexpr uint32_t kOptionsChanged = 0x231BBF91;

// The options dialog's buttons.
constexpr uint32_t kAccept = 0xEA57DA59;
constexpr uint32_t kCancel = 0x6A57DA48;
constexpr uint32_t kMore = 0x0A57DA65;        // opens the dialog of class 0x00AB4AFC
constexpr uint32_t kFirstPage = 0xEA57DB7E;
constexpr uint32_t kLeave = 0xEA57DB7F;       // sends message 0xAC3B3FA6 and closes
constexpr uint32_t kSecondPage = 0xEA5E99D9;
constexpr uint32_t kPageA = 0x2A57DC83;
constexpr uint32_t kPageB = 0x2A57DB84;

// A window message as window procedures get it.
struct WinMessage {
    uint32_t type;
    uint32_t a;
    uint32_t b;
};

// A check box of the options dialog and the byte of the options record it sets.
struct OptionCheck {
    uint32_t button;
    uint32_t offset;
};
constexpr OptionCheck kOptionChecks[] = {
    {0x4A7F19F2, 0xF08}, {0x4A7F19F3, 0xF09}, {0x4A7F19F4, 0x4EC}, {0x4A7F19F5, 0xF0B}, {0x4A7F19F6, 0xF0A},
};

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

// A text loaded by resource key (the game's object is 0x20 bytes).
struct LocalizedString {
    GameString text;
    uint32_t rest[5];
};

// The game's date (0x10 bytes); its cIGZDate part is at +8.
struct Date {
    std::uintptr_t vtable;
    uint32_t day;
    std::uintptr_t part;
    uint32_t rest;
};
static_assert(sizeof(Date) == 0x10);

struct FloatVector {
    float* begin;
    float* end;
    float* capacity;
};

// One line of a graph (0x30 bytes).
struct Series {
    uint32_t history;   // the history warehouse's type
    uint32_t second;    // a second type, combined with the first
    uint32_t group;     // kind 2: which vector the value goes into
    uint32_t combine;   // 0 none, 1 add, 2 subtract, 3 multiply, 4 divide; kind 2: 5 = curved scale
    uint8_t colour[3];
    uint8_t pad;
    uint32_t scale;     // float bits
    GameString name;
    uint8_t shown;
    uint8_t pad2[3];
};
static_assert(sizeof(Series) == 0x30);

// What the line graph is told about a line (built by 0x0076BD20).
struct LineStyle {
    uint8_t a;
    uint8_t b;
    uint8_t pad[2];
    uint32_t c;
    uint8_t d;
    uint8_t pad2[3];
    uint32_t colour;
    uint32_t e;
    GameString name;
};
static_assert(sizeof(LineStyle) == 0x28);

// A row of the range table at 0x00B0DA50.
struct Range {
    uint32_t points;
    uint32_t days;   // per point
    uint32_t label;
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

float AsFloat(uint32_t bits)
{
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

uint32_t Bits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

// The runtime's ftol (0x009EEF04), the low half of its result.
uint32_t Truncate(double value)
{
    return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int64_t>(value)));
}

void* DatePart(Date& date)
{
    return &date.part;
}

void MakeDate(Date& date, uint32_t day)
{
    This<void*>(addr::cRZDate_cRZDate, &date, day);
}

uint32_t DayOf(Date& date)
{
    return This<uint32_t>(addr::cRZDate_Day, DatePart(date));
}

void DestroyDate(Date& date)
{
    This<void>(addr::cRZDate_Destroy, &date);
}

// The curved scale of a kind 2 series, as the game's x87 code computes it:
// (ln(value) / ln(limit) + value / limit) * limit * 0.5, stored as a float.
// The logarithms are fyl2x with fldln2 and keep the extended precision the
// divide then rounds. The two emitted bytes are fdivp st(1), st (ST1 / ST0).
float CurveUp(float value, float limit)
{
    const double half = 0.5;
    float result;
    __asm {
        fld value
        fldln2
        fxch st(1)
        fyl2x
        fld limit
        fldln2
        fxch st(1)
        fyl2x
        _emit 0xDE
        _emit 0xF9
        fld value
        fdiv limit
        faddp st(1), st(0)
        fmul limit
        fmul half
        fstp result
    }
    return result;
}

// The same for a value below zero against the bottom limit:
// (ln(-value) / ln(-limit) + value / limit) * limit * 0.5.
float CurveDown(float value, float limit)
{
    const double half = 0.5;
    float result;
    __asm {
        fld value
        fchs
        fldln2
        fxch st(1)
        fyl2x
        fld limit
        fchs
        fldln2
        fxch st(1)
        fyl2x
        _emit 0xDE
        _emit 0xF9
        fld value
        fdiv limit
        faddp st(1), st(0)
        fmul limit
        fmul half
        fstp result
    }
    return result;
}

// --- The game options dialog ----------------------------------------------------------

// The game's 0x0076B110 (ret 8). `this` is the dialog. A button of the
// dialog was pressed; the second argument, the window, is not used. Always
// returns true.
uint8_t __fastcall OptionsCommand(char* self, void*, uint32_t id, void*)
{
    switch (id) {
    case kAccept: {
        if (At<void*>(self, options::kRecord)) {
            void* button = nullptr;
            for (const OptionCheck& check : kOptionChecks) {
                void* const window = Virtual<void*>(At<void*>(self, options::kWindow), 0xC);
                void** const out = This<void**>(addr::SC4RefPtr_Reset, &button);
                if (Virtual<uint8_t>(window, 0x94, check.button, kButtonIid, out)) {
                    if (Virtual<uint8_t>(button, 0x20))
                        At<uint8_t>(At<void*>(self, options::kRecord), check.offset) = 1;
                    else
                        At<uint8_t>(At<void*>(self, options::kRecord), check.offset) = 0;
                }
            }
            if (button)
                Release(button);
        }
        void* const server = Global<void*>(addr::SC4_MessageServer);
        void* const memory = Cdecl<void*>(addr::RZ_operator_new, uint32_t{0x2C});
        void* message;
        This<void*>(addr::SC4RefHolder_SC4RefHolder, &message,
                    memory ? This<void*>(addr::cGZMessage_cGZMessage, memory) : nullptr);
        Virtual<void>(message, 0x14, kOptionsChanged);
        Virtual<void>(server, 0x10, message, uint32_t{0});
        // Not checked for null, as in the original.
        void* service;
        This<void*>(addr::SC4RefHolder_SC4RefHolder, &service, Global<void*>(addr::SC4_Service43C94));
        Virtual<void>(service, 0x44);
        This<void>(addr::SC4UI_F76AAB0, self);
        Release(service);
        Release(message);
        return 1;
    }
    case kMore: {
        void* manager = nullptr;
        if (void* const framework = Cdecl<void*>(addr::RZGetFramework))
            Virtual<uint8_t>(framework, 0x14, uint32_t{0xA417445E}, uint32_t{0x5A4}, &manager);
        // The original gives its reference back here and goes on using the pointer.
        void* const windows = manager;
        This<void>(addr::SC4RefPtr_Release, &manager);
        void* const saved = Virtual<void*>(windows, 0x90);
        void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x10});
        void* other = nullptr;
        if (memory) {
            other = This<void*>(addr::SC4UI_F76A060, memory);
            if (other)
                AddRef(other);
        }
        This<void>(addr::SC4UI_F76A2C0, other, uint32_t{0});
        This<void>(addr::SC4UI_F769F70, other);
        if (Virtual<uint8_t>(windows, 0x60, saved))
            Virtual<void>(windows, 0x94, saved);
        if (other)
            Release(other);
        return 1;
    }
    case kFirstPage: {
        void* window = Virtual<void*>(At<void*>(self, options::kWindow), 0xC);
        Virtual<void>(Virtual<void*>(window, 0x8C, kPageA), 0x110, uint32_t{1}, uint32_t{0});
        window = Virtual<void*>(At<void*>(self, options::kWindow), 0xC);
        Virtual<void>(Virtual<void*>(window, 0x8C, kPageB), 0x110, uint32_t{1}, uint32_t{1});
        return 1;
    }
    case kSecondPage: {
        void* window = Virtual<void*>(At<void*>(self, options::kWindow), 0xC);
        Virtual<void>(Virtual<void*>(window, 0x8C, kPageA), 0x110, uint32_t{1}, uint32_t{1});
        window = Virtual<void*>(At<void*>(self, options::kWindow), 0xC);
        Virtual<void>(Virtual<void*>(window, 0x8C, kPageB), 0x110, uint32_t{1}, uint32_t{0});
        return 1;
    }
    case kLeave:
        if (Global<void*>(addr::SC4_Service43C94)) {
            Cdecl<uint8_t>(addr::SC4UI_SendMessageNow, uint32_t{0xAC3B3FA6}, uint32_t{0}, uint32_t{0}, uint32_t{0},
                           uint32_t{0}, uint32_t{0});
            This<void>(addr::SC4UI_F76AAB0, self);
        }
        return 1;
    case kCancel:
        This<void>(addr::SC4UI_F76AAB0, self);
        return 1;
    default:
        return 1;
    }
}

// The game's 0x0076B500, slot 2. Returns the count left; at the last
// reference it runs Shutdown (slot 6), releases the window and frees the
// object.
uint32_t __fastcall OptionsRelease(char* self, void*)
{
    const uint32_t count = At<uint32_t>(self, options::kReferences);
    if (count > 1) {
        At<uint32_t>(self, options::kReferences) = count - 1;
        return count - 1;
    }
    Virtual<uint8_t>(self, 0x18);
    At<std::uintptr_t>(self, 0x0) = addr::SC4UI_OptionsDialogVtable;
    if (void* const window = At<void*>(self, options::kWindow))
        Release(window);
    Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return 0;
}

// The game's 0x0076B540 (ret 8), slot 3: the window procedure. Only for the
// dialog's own window while it is enabled (slot 0x11C); of the commands
// (type 3 with 0x287259F6) the dialog's seven buttons go to the handler.
uint8_t __fastcall OptionsWinMessage(char* self, void*, void* window, const WinMessage* message)
{
    if (window != Virtual<void*>(At<void*>(self, options::kWindow), 0xC))
        return 0;
    if (!Virtual<uint8_t>(window, 0x11C))
        return 0;
    if (message->type != 3)
        return 0;
    if (message->a != 0x287259F6)
        return 1;
    const uint32_t id = message->b;
    if (id > kAccept) {
        if (id < kFirstPage)
            return 1;
        if (id > kLeave && id != kSecondPage)
            return 1;
    } else if (id != kAccept && id != kMore && id != kCancel) {
        return 1;
    }
    OptionsCommand(self, nullptr, id, window);
    return 1;
}

// --- The line graph control -----------------------------------------------------------

// The game's 0x0076B5F0: the framework line graph's constructor, then this
// class's three vtables.
void* __fastcall ChartConstruct(char* self, void*)
{
    This<void*>(addr::SC4UI_LineGraphConstruct, self);
    At<std::uintptr_t>(self, 0x0) = addr::SC4UI_GraphWindowLineVtable;
    At<std::uintptr_t>(self, chart::kAxes) = addr::SC4UI_GraphWindowLineVtableD8;
    At<std::uintptr_t>(self, chart::kData) = addr::SC4UI_GraphWindowLineVtable22C;
    return self;
}

// The game's 0x0076B840 (ret 4): the deleting destructor.
void* __fastcall ChartDelete(char* self, void*, uint32_t flags)
{
    This<void>(addr::SC4UI_LineGraphDestruct, self);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// --- The graphs window ----------------------------------------------------------------

// The game's 0x0076B5D0 (cdecl, four arguments): the callback Init gives the
// window's child 0xAA9244B6 under the key 0x22BA0121. Hands the fourth
// argument to slot 0x158 of the third, the graphs window's own window.
uint8_t __cdecl GraphsCallback(void*, void*, void* window, void* what)
{
    Virtual<void>(window, 0x158, what);
    return 1;
}

// The game's 0x0076B750, no arguments. Undoes Init: the notification, the
// chart, the window procedure of the three windows, the two panes.
uint8_t __fastcall GraphsShutdown(char* self, void*)
{
    if (!At<uint8_t>(self, graphs::kInited))
        return 1;
    At<uint8_t>(self, graphs::kInited) = 0;
    Virtual<void>(Global<void*>(addr::SC4_MessageServer), 0x18, static_cast<void*>(self), kGraphsMessage);
    if (void* const pane = At<void*>(self, graphs::kSecondPane))
        Virtual<void>(pane, 0x40, At<void*>(self, graphs::kChart));
    Clear(At<void*>(self, graphs::kChart));
    for (const std::size_t field : {graphs::kWindow, graphs::kFirstPane, graphs::kSecondPane}) {
        void* gen;
        if (Virtual<uint8_t>(At<void*>(self, field), 0x0, kWinGenIid, &gen)) {
            Virtual<void>(gen, 0x1C, uint32_t{0});
            Release(gen);
        }
    }
    Clear(At<void*>(self, graphs::kFirstPane));
    Clear(At<void*>(self, graphs::kSecondPane));
    return 1;
}

// The game's 0x0076B860 (ret 4): shows one pane and hides the other (slot
// 0x118 on the one that goes, 0x114 on the one that comes).
void __fastcall GraphsShowPane(char* self, void*, uint8_t first)
{
    if (first) {
        Virtual<void>(At<void*>(self, graphs::kFirstPane), 0x118);
        Virtual<void>(At<void*>(self, graphs::kSecondPane), 0x114);
        At<uint8_t>(self, graphs::kOnFirst) = 1;
    } else {
        Virtual<void>(At<void*>(self, graphs::kSecondPane), 0x118);
        Virtual<void>(At<void*>(self, graphs::kFirstPane), 0x114);
        At<uint8_t>(self, graphs::kOnFirst) = 0;
    }
}

// One of a tab's buttons, by ID: slot 0x14 with (1, down).
void SetTabButton(char* self, uint32_t id, uint32_t down)
{
    void* button;
    if (Virtual<uint8_t>(At<void*>(self, graphs::kWindow), 0x94, id, kButtonIid, &button)) {
        Virtual<void>(button, 0x14, uint32_t{1}, down);
        Release(button);
    }
}

// The game's 0x0076B8B0 (ret 4): lets the two buttons of the tab that was
// down (IDs +0x4000 and +0x4100) up and puts the new tab's two down. The
// caller stores the new tab.
void __fastcall GraphsPressTab(char* self, void*, uint32_t tab)
{
    if (!At<void*>(self, graphs::kWindow))
        return;
    SetTabButton(self, At<uint32_t>(self, graphs::kTab) + 0x4000, 0);
    SetTabButton(self, At<uint32_t>(self, graphs::kTab) + 0x4100, 0);
    SetTabButton(self, tab + 0x4000, 1);
    SetTabButton(self, tab + 0x4100, 1);
}

// The game's 0x0076BDF0 (ret 4): a series copied from another.
Series* __fastcall SeriesCopy(Series* self, void*, const Series* source)
{
    self->history = source->history;
    self->second = source->second;
    self->group = source->group;
    self->combine = source->combine;
    self->colour[0] = source->colour[0];
    self->colour[1] = source->colour[1];
    self->colour[2] = source->colour[2];
    self->scale = source->scale;
    uint32_t tag; // the original passes the address of its own argument here
    This<void*>(addr::SC4String_CopyBuffer, &self->name.body, const_cast<StringBody*>(&source->name.body), uint32_t{0},
                uint32_t{0xFFFFFFFF}, &tag);
    self->name.vtable = addr::SC4String_Vtable;
    self->name.zero = 0;
    self->shown = source->shown;
    return self;
}

// The game's 0x0076C9C0 (cdecl): copies the series [first, last) into raw
// memory at `to`; returns the end of the copies.
Series* __cdecl SeriesCopyRange(const Series* first, const Series* last, Series* to)
{
    for (; first != last; ++first, ++to) {
        if (to)
            SeriesCopy(to, nullptr, first);
    }
    return to;
}

// The game's 0x0076C9F0 (cdecl): `count` copies of one series into raw
// memory at `to`; returns the end of the copies.
Series* __cdecl SeriesFill(Series* to, uint32_t count, const Series* value)
{
    for (; count != 0; --count, ++to) {
        if (to)
            SeriesCopy(to, nullptr, value);
    }
    return to;
}

// The game's 0x0076CA20 (cdecl): destroys the series [first, last), that is
// their names.
void __cdecl SeriesDestroyRange(Series* first, Series* last)
{
    for (; first != last; ++first)
        DestroyString(first->name);
}

// The end of both branches of the fill: the top of the scale and the step
// between its lines from the span of the values, told to the chart's axis
// part with the limits. `top`, `bottom` and `step` are float bits.
void SetScale(char* self, uint32_t& top, uint32_t bottom, uint32_t& step)
{
    uint32_t span = Truncate(static_cast<double>(AsFloat(top)) - static_cast<double>(AsFloat(bottom)));
    if (span < 1) {
        top = 0x3F800000;  // 1
        step = 0x3E4CCCCD; // 0.2
        Virtual<void>(At<char*>(self, graphs::kChart) + chart::kAxes, 0x6C, uint32_t{1}, uint32_t{1});
    } else if (span < 3) {
        const double ceiling = Cdecl<double>(addr::CRT_ceil, static_cast<double>(AsFloat(top)));
        top = Bits(static_cast<float>(ceiling));
        step = 0x3F000000; // 0.5
        Virtual<void>(At<char*>(self, graphs::kChart) + chart::kAxes, 0x6C, uint32_t{1}, uint32_t{1});
    } else {
        uint32_t unit = 1;
        while (span >= 100) {
            unit = Truncate(static_cast<double>(unit) * 10.0);
            span = (span + 9) / 10;
        }
        double lines;
        if (span > 50)
            lines = static_cast<double>(unit) * 10.0;
        else if (span > 20)
            lines = static_cast<double>(unit) * 5.0;
        else if (span > 10)
            lines = static_cast<double>(unit) * 4.0;
        else if (span > 5)
            lines = static_cast<double>(unit) + static_cast<double>(unit);
        else
            lines = static_cast<double>(unit);
        step = Bits(static_cast<float>(lines));
        if (At<uint32_t>(self, graphs::kTop) == kNoTop)
            top = Bits(static_cast<float>(static_cast<double>(AsFloat(top)) * 0x1.0d79435e50d79p+0)); // 20 / 19
        Virtual<void>(At<char*>(self, graphs::kChart) + chart::kAxes, 0x6C, uint32_t{1}, uint32_t{0});
    }
    if (At<float>(self, graphs::kStep) != 0.0f)
        step = At<uint32_t>(self, graphs::kStep);
}

// Kind 2: one value of today per series, sorted into a vector per group.
void FillGroups(char* self)
{
    void* data = nullptr;
    if (!Virtual<uint8_t>(At<void*>(self, graphs::kChart), 0x0, kBarGraphIid, &data))
        return;
    Date today;
    MakeDate(today, Virtual<uint32_t>(Global<void*>(addr::SC4_SimulatorService), 0x20));
    Date earlier; // made and destroyed, never read
    MakeDate(earlier, DayOf(today) - 0x3C);
    void* const history = Virtual<void*>(Global<void*>(addr::SC4_City), 0x80);
    // Sixteen vectors on the stack; the group count is not checked against it.
    FloatVector groups[16] = {};
    for (uint32_t i = 0; i < At<uint32_t>(self, graphs::kGroupCount); ++i) {
        uint32_t fill = 0;
        This<void>(addr::SC4FloatVector_Resize2, &groups[i], At<uint32_t>(self, graphs::kSeriesCount), &fill);
    }
    for (uint32_t i = 0; i < At<uint32_t>(self, graphs::kSeriesCount); ++i) {
        Series* const series = At<Series*>(self, graphs::kSeries) + i;
        float value;
        if (!Virtual<uint8_t>(history, 0x20, series->history, DatePart(today), &value))
            value = 0.0f;
        if (series->combine == 5 && At<uint32_t>(self, graphs::kTop) != kNoTop &&
            At<uint32_t>(self, graphs::kBottom) != kNoBottom) {
            if (value > 0.0f)
                value = CurveUp(value, At<float>(self, graphs::kTop));
            else if (value < 0.0f)
                value = CurveDown(value, At<float>(self, graphs::kBottom));
        }
        const double scaled = static_cast<double>(value) * static_cast<double>(AsFloat(series->scale));
        groups[series->group].begin[i] = static_cast<float>(scaled);
    }
    for (uint32_t i = 0; i < At<uint32_t>(self, graphs::kGroupCount); ++i)
        Virtual<void>(data, 0x54, &groups[i], i);

    uint32_t top = Bits(static_cast<float>(Virtual<double>(data, 0x88, uint32_t{1})));
    uint32_t bottom = 0;
    uint32_t step;
    if (At<uint32_t>(self, graphs::kTop) != kNoTop)
        top = At<uint32_t>(self, graphs::kTop);
    if (At<uint32_t>(self, graphs::kBottom) != kNoBottom)
        bottom = At<uint32_t>(self, graphs::kBottom);
    SetScale(self, top, bottom, step);
    Virtual<void>(At<char*>(self, graphs::kChart) + chart::kAxes, 0x3C, uint32_t{1}, bottom);
    Virtual<void>(At<char*>(self, graphs::kChart) + chart::kAxes, 0x40, uint32_t{1}, top);
    Virtual<void>(At<char*>(self, graphs::kChart) + chart::kAxes, 0x60, uint32_t{1}, step);
    Release(data);
    for (std::size_t i = 16; i-- > 0;) {
        if (groups[i].begin)
            Cdecl<void>(addr::SC4_Free, static_cast<void*>(groups[i].begin));
    }
    DestroyDate(earlier);
    DestroyDate(today);
}

// Kind 1: for each series that is shown, a line of values over the range.
void FillTime(char* self)
{
    void* data = nullptr;
    if (!Virtual<uint8_t>(At<void*>(self, graphs::kChart), 0x0, kLineGraphIid, &data))
        return;
    const Range* const ranges = &Global<Range>(addr::SC4UI_GraphRanges);
    const uint32_t points = ranges[At<uint32_t>(self, graphs::kRange)].points;
    const uint32_t days = ranges[At<uint32_t>(self, graphs::kRange)].days;
    Virtual<void>(data, 0x20);
    Virtual<void>(data, 0x14, At<uint32_t>(self, graphs::kSeriesCount));

    // The lines: a series is shown unless its check box in the chart
    // (ID 0x4000000 + index) says no. The check box's reference is not
    // given back, as in the original.
    uint32_t line = 0;
    for (uint32_t i = 0; i < At<uint32_t>(self, graphs::kSeriesCount); ++i) {
        Series* const series = At<Series*>(self, graphs::kSeries) + i;
        void* box;
        if (Virtual<uint8_t>(At<void*>(self, graphs::kChart), 0x90, i + 0x4000000, kButtonIid, &box)) {
            const uint8_t on = Virtual<uint8_t>(box, 0x10, uint32_t{1});
            uint32_t key = series->history;
            series->shown = on;
            *This<uint8_t*>(addr::SC4UI_F76BD80, self + graphs::kShown, &key) = on;
        } else {
            series->shown = 1;
        }
        if (series->shown) {
            LineStyle style;
            This<void*>(addr::SC4UI_F76BD20, &style);
            style.a = 0;
            style.d = 1;
            void* const colours = Cdecl<void*>(addr::SC4_Singleton913C1A);
            style.colour = Virtual<uint32_t>(colours, 0x88, uint32_t{series->colour[0]}, uint32_t{series->colour[1]},
                                             uint32_t{series->colour[2]});
            style.e = 1;
            This<void*>(addr::SC4String_EraseBody, &style.name.body, uint32_t{0}, uint32_t{0xFFFFFFFF});
            Virtual<void>(data, 0x38, &style, line);
            ++line;
            DestroyString(style.name);
        }
    }

    void* const simulator = Global<void*>(addr::SC4_SimulatorService);
    Date sample;
    MakeDate(sample, Virtual<uint32_t>(simulator, 0x20) - 4);
    Date start;
    This<void*>(addr::cRZDate_Default, &start);
    Virtual<void>(Global<void*>(addr::SC4_SimulatorService), 0x14, DatePart(start));
    void* const history = Virtual<void*>(Global<void*>(addr::SC4_City), 0x80);
    // The last sample day rounded up to a whole step, then back by the
    // number of points and one.
    This<void>(addr::cRZDate_SetDay, DatePart(sample), (DayOf(sample) + days - 1) / days * days);
    uint32_t left = points + 1;
    This<void>(addr::cRZDate_SetDay, DatePart(sample), DayOf(sample) - left * days);
    float value = 0.0f;
    float other = 0.0f;
    do {
        Date next;
        MakeDate(next, DayOf(sample) + days);
        Date before; // made and destroyed, never read
        MakeDate(before, DayOf(sample) - 1);
        uint32_t shown = 0;
        for (uint32_t i = 0; i < At<uint32_t>(self, graphs::kSeriesCount); ++i) {
            Series* const series = At<Series*>(self, graphs::kSeries) + i;
            if (!series->shown)
                continue;
            // Nothing before the city's first day.
            if (Virtual<uint32_t>(DatePart(start), 0x6C, DatePart(sample)) == 0) {
                value = 0.0f;
                other = 0.0f;
            } else {
                if (!Virtual<uint8_t>(history, 0x20, series->history, DatePart(next), &value))
                    value = 0.0f;
                if (series->combine != 0) {
                    if (!Virtual<uint8_t>(history, 0x20, series->second, DatePart(next), &other))
                        other = 0.0f;
                }
            }
            switch (series->combine) {
            case 1:
                value = static_cast<float>(static_cast<double>(other) + static_cast<double>(value));
                break;
            case 2:
                value = static_cast<float>(static_cast<double>(value) - static_cast<double>(other));
                break;
            case 3:
                value = static_cast<float>(static_cast<double>(other) * static_cast<double>(value));
                break;
            case 4: {
                double size = static_cast<double>(other);
                if (size < 0.0)
                    size = -size;
                if (size > 1e-7)
                    value = static_cast<float>(static_cast<double>(value) / static_cast<double>(other));
                else
                    value = 0.0f;
                break;
            }
            default:
                break;
            }
            value = static_cast<float>(static_cast<double>(value) * static_cast<double>(AsFloat(series->scale)));
            Virtual<void>(data, 0x50, Bits(value), shown, uint32_t{0xFFFFFFFF});
            ++shown;
        }
        sample.day = next.day;
        sample.rest = next.rest;
        DestroyDate(before);
        DestroyDate(next);
    } while (--left != 0);
    Virtual<void>(data, 0x4C);

    // The two texts under the chart: the range's own (label + 1) and the
    // number of points.
    {
        LocalizedString text;
        void* const made = This<void*>(addr::SC4LocalizedString_SC4LocalizedString, &text, kTextGroup,
                                       ranges[At<uint32_t>(self, graphs::kRange)].label + 1, uint32_t{1});
        Virtual<void>(data, 0x44, made, uint32_t{0});
        DestroyString(text.text);
    }
    {
        LocalizedString text;
        void* const made = This<void*>(addr::SC4LocalizedString_SC4LocalizedString, &text, kTextGroup,
                                       uint32_t{0x8A5D420D}, uint32_t{1});
        Virtual<void>(data, 0x44, made, points);
        DestroyString(text.text);
    }

    uint32_t top = Bits(static_cast<float>(Virtual<double>(data, 0x64, uint32_t{1})));
    const double lowest = Virtual<double>(data, 0x68, uint32_t{1});
    uint32_t bottom = Bits(static_cast<float>(lowest));
    if (lowest > 0.0)
        bottom = 0;
    uint32_t step;
    if (At<uint32_t>(self, graphs::kTop) != kNoTop)
        top = At<uint32_t>(self, graphs::kTop);
    if (At<uint32_t>(self, graphs::kBottom) != kNoBottom)
        bottom = At<uint32_t>(self, graphs::kBottom);
    SetScale(self, top, bottom, step);
    Virtual<void>(At<char*>(self, graphs::kChart) + chart::kAxes, 0x3C, uint32_t{0}, uint32_t{0});
    Virtual<void>(At<char*>(self, graphs::kChart) + chart::kAxes, 0x3C, uint32_t{1}, bottom);
    Virtual<void>(At<char*>(self, graphs::kChart) + chart::kAxes, 0x40, uint32_t{1}, top);
    Virtual<void>(At<char*>(self, graphs::kChart) + chart::kAxes, 0x60, uint32_t{1}, step);
    Release(data);
    DestroyDate(start);
    DestroyDate(sample);
}

// The game's 0x0076BE70, no arguments: hands the chart its numbers from the
// city's history warehouse (city slot 32, GetPoint at +0x20) and sets its
// scale. Nothing without a chart or for a kind other than 1 and 2.
void __fastcall GraphsFill(char* self, void*)
{
    if (!At<void*>(self, graphs::kChart))
        return;
    if (At<uint32_t>(self, graphs::kKind) == 1)
        FillTime(self);
    else if (At<uint32_t>(self, graphs::kKind) == 2)
        FillGroups(self);
}

// The game's 0x0076C910 (ret 4): the time range (a row of the table at
// 0x00B0DA50). Stores it, shows its name in the second pane's text
// 0x8A2871D9, and with a chart fills it again and has it redrawn.
void __fastcall GraphsSetRange(char* self, void*, uint32_t range)
{
    At<uint32_t>(self, graphs::kRange) = range;
    void* const pane = At<void*>(self, graphs::kSecondPane);
    if (!pane)
        return;
    void* label;
    if (Virtual<uint8_t>(pane, 0x94, uint32_t{0x8A2871D9}, kTextIid, &label)) {
        const Range* const ranges = &Global<Range>(addr::SC4UI_GraphRanges);
        LocalizedString text;
        void* const made = This<void*>(addr::SC4LocalizedString_SC4LocalizedString, &text, kTextGroup,
                                       ranges[At<uint32_t>(self, graphs::kRange)].label, uint32_t{1});
        Virtual<void>(label, 0x10, made);
        DestroyString(text.text);
        Release(label);
    }
    if (At<void*>(self, graphs::kChart)) {
        GraphsFill(self, nullptr);
        Virtual<void>(At<void*>(self, graphs::kChart), 0x170);
    }
}

// The game's 0x0076CA60, no arguments. The first time: finds the two panes
// under the window's parent, sets the first range, hides both panes, becomes
// the window procedure of the three windows, sets flag 0x800 off on them and
// registers for message 0x66956816. Every time: points four buttons of the
// panes and the window's child 0xAA9244B6 at the window.
uint8_t __fastcall GraphsInit(char* self, void*)
{
    if (!At<uint8_t>(self, graphs::kInited)) {
        void* const parent = Virtual<void*>(At<void*>(self, graphs::kWindow), 0x2C);
        Assign(At<void*>(self, graphs::kFirstPane), Virtual<void*>(parent, 0x88, uint32_t{0x8A8B5B72}));
        Assign(At<void*>(self, graphs::kSecondPane), Virtual<void*>(parent, 0x88, uint32_t{0x8A8B5B71}));
        GraphsSetRange(self, nullptr, 0);
        Virtual<void>(At<void*>(self, graphs::kFirstPane), 0x118);
        Virtual<void>(At<void*>(self, graphs::kSecondPane), 0x118);
        for (const std::size_t field : {graphs::kWindow, graphs::kFirstPane, graphs::kSecondPane}) {
            void* gen;
            if (Virtual<uint8_t>(At<void*>(self, field), 0x0, kWinGenIid, &gen)) {
                Virtual<void>(gen, 0x1C, static_cast<void*>(self + graphs::kWinProc));
                Release(gen);
            }
        }
        Virtual<void>(At<void*>(self, graphs::kWindow), 0x110, uint32_t{0x800}, uint32_t{0});
        Virtual<void>(At<void*>(self, graphs::kFirstPane), 0x110, uint32_t{0x800}, uint32_t{0});
        Virtual<void>(At<void*>(self, graphs::kSecondPane), 0x110, uint32_t{0x800}, uint32_t{0});
        Virtual<void>(Global<void*>(addr::SC4_MessageServer), 0x14, static_cast<void*>(self), kGraphsMessage);
        At<uint8_t>(self, graphs::kInited) = 1;
    }
    void* child = Virtual<void*>(At<void*>(self, graphs::kFirstPane), 0x8C, uint32_t{0x0A8B7E01});
    Virtual<void>(child, 0x158, At<void*>(self, graphs::kWindow));
    child = Virtual<void*>(At<void*>(self, graphs::kSecondPane), 0x8C, uint32_t{0x0A8B7E00});
    Virtual<void>(child, 0x158, At<void*>(self, graphs::kWindow));
    child = Virtual<void*>(At<void*>(self, graphs::kFirstPane), 0x8C, uint32_t{0xCA8B82F9});
    Virtual<void>(child, 0x158, At<void*>(self, graphs::kWindow));
    child = Virtual<void*>(At<void*>(self, graphs::kSecondPane), 0x8C, uint32_t{0xCA8B82F9});
    Virtual<void>(child, 0x158, At<void*>(self, graphs::kWindow));
    child = Virtual<void*>(At<void*>(self, graphs::kWindow), 0x8C, uint32_t{0xAA9244B6});
    if (child) {
        Virtual<void>(child, 0x80, uint32_t{0x22BA0121}, reinterpret_cast<void*>(addr::SC4UI_F76B5D0),
                      At<void*>(self, graphs::kWindow));
    }
    return 1;
}

bool InstallDialogs5(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[20] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F76B110, reinterpret_cast<void*>(&OptionsCommand)},
        {addr::SC4UI_F76B500, reinterpret_cast<void*>(&OptionsRelease)},
        {addr::SC4UI_F76B540, reinterpret_cast<void*>(&OptionsWinMessage)},
        {addr::SC4UI_F76B5D0, reinterpret_cast<void*>(&GraphsCallback)},
        {addr::SC4UI_F76B5F0, reinterpret_cast<void*>(&ChartConstruct)},
        {addr::SC4UI_F76B750, reinterpret_cast<void*>(&GraphsShutdown)},
        {addr::SC4UI_F76B840, reinterpret_cast<void*>(&ChartDelete)},
        {addr::SC4UI_F76B860, reinterpret_cast<void*>(&GraphsShowPane)},
        {addr::SC4UI_F76B8B0, reinterpret_cast<void*>(&GraphsPressTab)},
        {addr::SC4UI_F76BDF0, reinterpret_cast<void*>(&SeriesCopy)},
        {addr::SC4UI_F76BE70, reinterpret_cast<void*>(&GraphsFill)},
        {addr::SC4UI_F76C910, reinterpret_cast<void*>(&GraphsSetRange)},
        {addr::SC4UI_F76C9C0, reinterpret_cast<void*>(&SeriesCopyRange)},
        {addr::SC4UI_F76C9F0, reinterpret_cast<void*>(&SeriesFill)},
        {addr::SC4UI_F76CA20, reinterpret_cast<void*>(&SeriesDestroyRange)},
        {addr::SC4UI_F76CA60, reinterpret_cast<void*>(&GraphsInit)},
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

const bool g_dialogs5Registered = registry::Register({
    "ui.dialogs.part5",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F76BE70,
    "Our options dialog commands, line graph control and graphs window fill (docs/ui.md)",
    false,
    InstallDialogs5,
});

} // namespace
} // namespace opensc4::ui
