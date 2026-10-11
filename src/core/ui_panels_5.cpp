// Three things of the interface between the department dialog's neighbours
// and the map window (0x00797200 to 0x00799DD0).
//
// The animation recorder's tail (0x00797200 to 0x00797B00). The recorder is
// a developer's window (main vtable 0x00AB6010, a window of the framework
// with 151 slots; its first part, the loader and the drawing are at
// 0x007946A0 to 0x00797200): it grabs the picture of a view at a steady
// pace into a row of buffers and writes them out as numbered frames. Here:
// writing the frames (0x00797200), Stop (0x00797290), grabbing one frame
// (0x00797430), Shutdown (slot 5), the tick (slot 3, message 0x8802D2FF),
// Start (0x007977D0) and the buttons (slot 143: start or stop 0x080293A3,
// close 0x080293A4, one frame 0x080293A5).
//
// The progress bar (0x00797BB0 to 0x00797F50): a window class of its own
// (class ID 0xAA5D16A9, 0xF8 bytes, main vtable 0x00AB64B8, a counted part
// at +0xD8 with vtable 0x00AB64A0, the bar's interface 0x4A5D1208 at +0xE0
// with vtable 0x00AB6488). The interface sets a picture (slot 3), sets the
// filled share from 0 to 1 (slot 4) and gives it back (slot 5). Its paint
// (0x00797CC0, slot 88) is drawing and stays the game's.
//
// The info tip's first part (0x00797F50 to 0x00799DD0): the bubble the
// MySim tools show next to the cursor (0x1B4 bytes, interface 0xC9B432CF,
// vtable 0x00AB69D0, its window at +4 with vtable 0x00AB6770; constructor
// 0x00799DD0). Here its setters and getters, the window's Init and
// Shutdown. Every setter that changes something marks what has to be made
// again (+0x12C: 1 the contents, 2 the place) and asks for a repaint; the
// paint (0x00798710, the window's slot 88) measures the texts, builds the
// bubble's picture and draws it, and stays the game's.
//
// The recorder: +0x4 the window manager (the window base's field), +0xD8
// the view recorded, +0xDC to +0x13C controls (+0xE0 the folder's edit box,
// +0xF8 the interval's, +0x110 the length's, +0x128 the start button,
// +0x130 a control switched with it), +0x14C the driver's screen capture
// extension, +0x164 a vector of strings (the frames' names), +0x170 the
// most frames the buffers hold, +0x178 the folder's path (a string),
// +0x18C the folder's text, +0x1BC a byte: the game's clock is stepped by
// the recorder (the timer service 0x86AD10EC), +0x1C0 the take's number,
// +0x1C4 the next frame, +0x1C8 the interval in milliseconds, +0x1CC the
// length in seconds (float), +0x1D0 the frames to take, +0x1D4 the area
// grabbed (a rectangle), +0x1E4 a vector of buffers, +0x1F0 a stopwatch,
// +0x208 the frames still to take, +0x20C ticks until the next frame when
// stepped, +0x210 a dword Start clears.
//
// The progress bar: +0 the window (0xD8 bytes), +0xD8 the counted part
// (vtable, count), +0xE0 the bar's interface, +0xE8 the share (double),
// +0xF0 the picture.
//
// The info tip: +0 vtable, +4 the window, +0xDC the rectangle to stay
// inside, +0xEC the point it points at, +0xF4 the title, +0x108 the text
// (strings), +0x11C the rectangle to keep clear, +0x12C what is stale,
// +0x130 the bubble's picture, +0x134 the icon, +0x138 the title's font,
// +0x13C the text's font, +0x140 the meter's picture, +0x144 and +0x148 the
// two surfaces the paint builds (the contents, the bubble), +0x14C and
// +0x150 the margins, +0x1A8 the meter's count, +0x1B0 the meter's filled
// share (float).
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

namespace recorder {
constexpr std::size_t kManager = 0x4;
constexpr std::size_t kView = 0xD8;
constexpr std::size_t kControlDC = 0xDC;
constexpr std::size_t kFolderEdit = 0xE0;
constexpr std::size_t kIntervalEdit = 0xF8;
constexpr std::size_t kLengthEdit = 0x110;
constexpr std::size_t kStartButton = 0x128;
constexpr std::size_t kControl12C = 0x12C;
constexpr std::size_t kSwitched = 0x130;
constexpr std::size_t kControl134 = 0x134;
constexpr std::size_t kControl13C = 0x13C;
constexpr std::size_t kCapture = 0x14C;
constexpr std::size_t kNames = 0x164;
constexpr std::size_t kMostFrames = 0x170;
constexpr std::size_t kFolder = 0x178;
constexpr std::size_t kFolderText = 0x18C;
constexpr std::size_t kStepped = 0x1BC;
constexpr std::size_t kTake = 0x1C0;
constexpr std::size_t kFrame = 0x1C4;
constexpr std::size_t kInterval = 0x1C8;
constexpr std::size_t kSeconds = 0x1CC;
constexpr std::size_t kFrames = 0x1D0;
constexpr std::size_t kArea = 0x1D4;
constexpr std::size_t kBuffers = 0x1E4;
constexpr std::size_t kBuffersEnd = 0x1E8;
constexpr std::size_t kWatch = 0x1F0;
constexpr std::size_t kLeft = 0x208;
constexpr std::size_t kCountdown = 0x20C;
constexpr std::size_t kSpare = 0x210;
} // namespace recorder

namespace bar {
constexpr std::size_t kCounted = 0xD8;
constexpr std::size_t kBar = 0xE0;
constexpr std::size_t kShare = 0xE8;
constexpr std::size_t kPicture = 0xF0;
constexpr uint32_t kSize = 0xF8;
} // namespace bar

namespace tip {
constexpr std::size_t kWindow = 0x4;
constexpr std::size_t kBounds = 0xDC;
constexpr std::size_t kAnchor = 0xEC;
constexpr std::size_t kTitle = 0xF4;
constexpr std::size_t kText = 0x108;
constexpr std::size_t kClear = 0x11C;
constexpr std::size_t kStale = 0x12C;
constexpr std::size_t kFrame = 0x130;
constexpr std::size_t kIcon = 0x134;
constexpr std::size_t kTitleFont = 0x138;
constexpr std::size_t kTextFont = 0x13C;
constexpr std::size_t kMeter = 0x140;
constexpr std::size_t kSurface = 0x144;
constexpr std::size_t kBubble = 0x148;
constexpr std::size_t kMarginX = 0x14C;
constexpr std::size_t kMarginY = 0x150;
constexpr std::size_t kMeterCount = 0x1A8;
constexpr std::size_t kMeterShare = 0x1B0;
} // namespace tip

constexpr uint32_t kButtonClicked = 0x287259F6;
constexpr uint32_t kRecorderTick = 0x8802D2FF;
constexpr uint32_t kStartStop = 0x080293A3;
constexpr uint32_t kClose = 0x080293A4;
constexpr uint32_t kOneFrame = 0x080293A5;
constexpr uint32_t kAnswerYes = 0x5301814C;
constexpr uint32_t kTimerService = 0x86AD10EC;
constexpr uint32_t kTimerIid = 0x86AD10ED;

constexpr uint32_t kBarClass = 0xAA5D16A9;
constexpr uint32_t kBarIid = 0x4A5D1208;
constexpr uint32_t kBarWindowIid = 0x22BA0121;

constexpr uint32_t kTipIid = 0xC9B432CF;
constexpr uint32_t kTipWindowId = 0x2AAB8CC1;
constexpr uint32_t kTipFont = 0x68963C4C;

struct Point {
    int32_t x;
    int32_t y;
};

struct Rect {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
};
static_assert(sizeof(Rect) == 0x10);

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

// The game's inlined reference holder: empty it (cleared first, then let go).
void Clear(void*& holder)
{
    if (void* const object = holder) {
        holder = nullptr;
        Release(object);
    }
}

// The other inlined emptying: let go first, then cleared.
void Drop(void*& holder)
{
    if (void* const object = holder) {
        Release(object);
        holder = nullptr;
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

// An empty string as the game's inlined constructor makes it.
void InitString(GameString& text)
{
    text.body.begin = nullptr;
    text.body.end = nullptr;
    text.body.capacity = nullptr;
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    text.body.capacity = buffer + 8;
    text.body.begin = buffer;
    text.body.end = buffer;
    *buffer = 0;
    text.vtable = addr::SC4String_Vtable;
    text.zero = 0;
}

// The inlined destructor.
void DestroyString(GameString& text)
{
    char* const buffer = text.body.begin;
    text.vtable = addr::SC4String_Vtable;
    if (buffer)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(buffer));
}

// The game's ftol (0x009EEF04).
int32_t ToInt(double value)
{
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

// The timer service (0x86AD10EC) as the recorder fetches it each time; the
// caller lets it go with SC4RefPtr::Destruct5F8F20.
void FetchTimer(void*& timer)
{
    timer = nullptr;
    if (void* const framework = Cdecl<void*>(addr::RZGetFramework))
        Virtual<uint8_t>(framework, 0x14, kTimerService, kTimerIid, &timer);
}

// --- The animation recorder -----------------------------------------------------------

// The recorder's message box: the window manager's slot 44 with the text,
// the title "Animation Recorder", the buttons, 1 and 0. The title is made
// first and destroyed last.
uint32_t RecorderAsk(char* self, const char* message, uint32_t buttons)
{
    GameString title;
    GameString text;
    void* const titleText = This<void*>(addr::SC4String_FromText, &title, "Animation Recorder");
    void* const messageText = This<void*>(addr::SC4String_FromText, &text, message);
    const uint32_t answer = Virtual<uint32_t>(At<void*>(self, recorder::kManager), 0xB0, messageText, titleText,
                                              buttons, uint32_t{1}, uint32_t{0});
    DestroyString(text);
    DestroyString(title);
    return answer;
}

// The start button's caption.
void RecorderCaption(char* self, const char* caption)
{
    void* const button = Virtual<void*>(At<void*>(self, recorder::kStartButton), 0xC);
    GameString text;
    Virtual<void>(button, 0x128, This<void*>(addr::SC4String_FromText, &text, caption));
    DestroyString(text);
}

// How many buffers the recorder holds.
uint32_t RecorderHeld(char* self)
{
    const int32_t bytes = At<int32_t>(self, recorder::kBuffersEnd) - At<int32_t>(self, recorder::kBuffers);
    return static_cast<uint32_t>(bytes >> 2);
}

// The game's 0x00797200: writes the grabbed frames out (0x00795E50, then
// 0x00796040) and says so when either fails.
uint8_t __fastcall RecorderWrite(char* self, void*)
{
    uint8_t written = 0;
    if (This<uint8_t>(addr::SC4UI_F795E50, self)) {
        written = This<uint8_t>(addr::SC4UI_F796040, self);
        if (written)
            return written;
    }
    RecorderAsk(self, "Animation write failed.", 0);
    return written;
}

// The game's 0x00797290: Stop. Offers to write the frames when any were
// taken, lets the buffers go, turns the controls back and, when the clock
// was stepped, hands it back to the timer service.
uint8_t __fastcall RecorderStop(char* self, void*)
{
    void* const watch = self + recorder::kWatch;
    if (!This<uint8_t>(addr::GZStopwatch_IsRunning, watch))
        return 0;
    This<void>(addr::SC4Stopwatch_Reset, watch);
    const uint32_t taken = At<uint32_t>(self, recorder::kFrame);
    At<uint32_t>(self, recorder::kLeft) = 0;
    if (taken != 0) {
        if (RecorderAsk(self, "Would you like to write out the animation frames?", 3) == kAnswerYes)
            RecorderWrite(self, nullptr);
    }
    This<uint8_t>(addr::SC4UI_F794A20, self);
    RecorderCaption(self, "Start");
    Virtual<void>(Virtual<void*>(At<void*>(self, recorder::kSwitched), 0xC), 0x110, uint32_t{2}, uint32_t{0});
    This<void>(addr::SC4UI_F795020, self);
    This<void>(addr::SC4UI_F794260, At<void*>(self, recorder::kView), uint32_t{1}, uint32_t{1});
    Virtual<void>(At<void*>(self, recorder::kView), 0x110, uint32_t{0x200000}, uint32_t{0});
    void* timer;
    FetchTimer(timer);
    if (At<uint8_t>(self, recorder::kStepped) && timer)
        Virtual<void>(timer, 0x30, uint32_t{0});
    This<void>(addr::SC4RefPtr_Destruct5F8F20, &timer);
    return 1;
}

// The game's 0x00797430: grabs one frame into the next buffer. When the
// buffers are full a stepped recording writes them out and starts over at
// the first; any other recording stops.
uint8_t __fastcall RecorderGrab(char* self, void*)
{
    if (!At<void*>(self, recorder::kCapture))
        return 0;
    if (At<uint32_t>(self, recorder::kFrame) >= RecorderHeld(self)) {
        if (!At<uint8_t>(self, recorder::kStepped)) {
            RecorderStop(self, nullptr);
            return 0;
        }
        This<uint8_t>(addr::SC4UI_F795E50, self);
        At<uint32_t>(self, recorder::kFrame) = 0;
    }
    const uint32_t frame = At<uint32_t>(self, recorder::kFrame);
    if (frame >= RecorderHeld(self))
        return 0;
    void* const buffer = At<void**>(self, recorder::kBuffers)[frame];
    if (!buffer)
        return 0;
    Rect area;
    This<void>(addr::SC4UI_F794700, self, &area);
    Rect& kept = At<Rect>(self, recorder::kArea);
    kept = area;
    const int32_t height = kept.bottom - kept.top;
    const int32_t width = kept.right - kept.left;
    if (!Virtual<uint32_t>(At<void*>(self, recorder::kCapture), 0xC, kept.left, kept.top, width, height, buffer))
        return 0;
    At<uint32_t>(self, recorder::kFrame) += 1;
    This<void>(addr::SC4UI_F795020, self);
    return 1;
}

// The game's 0x00797530 (slot 5): Shutdown. A running recording is stopped,
// the service at 0x00B43CE0 is told about the view and the window, the
// buffers and the controls are let go, and the window base's shutdown ends
// it.
uint8_t __fastcall RecorderShutdown(char* self, void*)
{
    if (This<uint8_t>(addr::GZWin_IsInitialized, self)) {
        if (This<uint8_t>(addr::GZStopwatch_IsRunning, self + recorder::kWatch))
            RecorderStop(self, nullptr);
        if (void* const target = Global<void*>(addr::SC4_Service43CE0)) {
            const uint32_t second = Virtual<uint32_t>(At<void*>(self, recorder::kView), 0xA8);
            const uint32_t first = Virtual<uint32_t>(At<void*>(self, recorder::kView), 0xA4);
            Virtual<void>(target, 0x18, At<void*>(self, recorder::kView));
            const uint32_t id = Virtual<uint32_t>(At<void*>(self, recorder::kView), 0xFC);
            Virtual<void>(target, 0x14, id + 1, first, second);
            Virtual<void>(target, 0x18, static_cast<void*>(self));
        }
        This<uint8_t>(addr::SC4UI_F794A20, self);
        This<void>(addr::SC4UI_F795A00, self);
        Drop(At<void*>(self, recorder::kCapture));
        if (void* const view = At<void*>(self, recorder::kView)) {
            if (void* const mainWindow = Global<void*>(addr::SC4_Service43DC8))
                Virtual<void>(mainWindow, 0x3C, view);
            Release(At<void*>(self, recorder::kView));
            At<void*>(self, recorder::kView) = nullptr;
        }
        Drop(At<void*>(self, recorder::kControlDC));
        Drop(At<void*>(self, recorder::kFolderEdit));
        Drop(At<void*>(self, recorder::kIntervalEdit));
        Drop(At<void*>(self, recorder::kLengthEdit));
        Drop(At<void*>(self, recorder::kStartButton));
        Drop(At<void*>(self, recorder::kControl12C));
        Drop(At<void*>(self, recorder::kSwitched));
        Drop(At<void*>(self, recorder::kControl134));
        Drop(At<void*>(self, recorder::kControl13C));
    }
    return This<uint8_t>(addr::SC4Win_F99D2FE, self);
}

// The game's 0x007976E0 (slot 3): the window's message handler. Only the
// recorder's own tick (0x8802D2FF) is handled here: a stepped recording
// grabs a frame every fourth tick and has the timer service step the
// clock; any other grabs one when the interval has passed on the
// stopwatch. The last frame stops the recording. The tick is posted again
// every time, also after the stop.
uint8_t __fastcall RecorderMessage(char* self, void*, uint32_t* message)
{
    if (*message != kRecorderTick)
        return This<uint8_t>(addr::SC4Win_F99CCF0, self, message);
    void* const watch = self + recorder::kWatch;
    if (!This<uint8_t>(addr::GZStopwatch_IsRunning, watch))
        return 1;
    void* timer;
    FetchTimer(timer);
    if (At<uint8_t>(self, recorder::kStepped) && timer) {
        if (--At<int32_t>(self, recorder::kCountdown) == 0) {
            RecorderGrab(self, nullptr);
            Virtual<void>(timer, 0x40);
            if (--At<int32_t>(self, recorder::kLeft) == 0)
                RecorderStop(self, nullptr);
            else
                At<uint32_t>(self, recorder::kCountdown) = 4;
        }
    } else {
        const uint32_t passed = This<uint32_t>(addr::GZStopwatch_ElapsedMilliseconds, watch);
        if (passed >= At<uint32_t>(self, recorder::kInterval)) {
            RecorderGrab(self, nullptr);
            This<void>(addr::GZStopwatch_Start, watch);
            if (--At<int32_t>(self, recorder::kLeft) == 0)
                RecorderStop(self, nullptr);
        }
    }
    Virtual<void>(At<void*>(self, recorder::kManager), 0x38, static_cast<void*>(self), kRecorderTick, uint32_t{0},
                  uint32_t{0}, uint32_t{0});
    This<void>(addr::SC4RefPtr_Destruct5F8F20, &timer);
    return 1;
}

// The game's 0x007977D0: Start. Needs the driver's screen capture
// extension. Makes the folder, reads the three edit boxes (the folder, the
// interval in milliseconds, the length in seconds), works out the number of
// frames, readies the buffers (0x00794ED0) and posts the first tick.
uint8_t __fastcall RecorderStart(char* self, void*)
{
    if (This<uint8_t>(addr::GZStopwatch_IsRunning, self + recorder::kWatch))
        return 0;
    if (!At<void*>(self, recorder::kCapture)) {
        RecorderAsk(self, "Screencapture cIGZGDriver extension not present.", 0);
        return 0;
    }
    if (At<char*>(self, recorder::kFolder + 0x4) != At<char*>(self, recorder::kFolder + 0x8)) {
        void* const folder = self + recorder::kFolder;
        if (!Cdecl<uint8_t>(addr::RZDirectory_Exists, folder)) {
            Cdecl<void>(addr::RZDirectory_Create, folder);
            if (!Cdecl<uint8_t>(addr::RZDirectory_Exists, folder))
                Cdecl<void>(addr::SC4_F602210, folder);
        }
    }
    GameString text;
    InitString(text);
    void* const folderText = self + recorder::kFolderText;
    Virtual<void>(At<void*>(self, recorder::kFolderEdit), 0xB4, folderText);
    This<void>(addr::SC4UI_F796210, self, folderText);
    At<uint32_t>(self, recorder::kTake) += 1;
    At<uint32_t>(self, recorder::kFrame) = 0;
    Virtual<void>(At<void*>(self, recorder::kIntervalEdit), 0xB4, &text);
    At<int32_t>(self, recorder::kInterval) = ToInt(Cdecl<double>(addr::SC4_ParseDouble910991, text.body.begin));
    Virtual<void>(At<void*>(self, recorder::kLengthEdit), 0xB4, &text);
    const double seconds = Cdecl<double>(addr::SC4_ParseDouble910991, text.body.begin);
    At<float>(self, recorder::kSeconds) = static_cast<float>(seconds);
    const double perSecond = 1000.0 / static_cast<double>(At<int32_t>(self, recorder::kInterval));
    At<int32_t>(self, recorder::kFrames) = ToInt(perSecond * seconds);
    void* const names = self + recorder::kNames;
    This<void*>(addr::SC4StringVector_Erase, names, At<void*>(names, 0x0), At<void*>(names, 0x4));
    if (!At<uint8_t>(self, recorder::kStepped)) {
        const int32_t most = At<int32_t>(self, recorder::kMostFrames);
        if (At<int32_t>(self, recorder::kFrames) > most)
            At<int32_t>(self, recorder::kFrames) = most;
    }
    At<int32_t>(self, recorder::kLeft) = At<int32_t>(self, recorder::kFrames);
    This<void>(addr::SC4UI_F794700, self, static_cast<void*>(self + recorder::kArea));
    if (!This<uint8_t>(addr::SC4UI_F794ED0, self)) {
        DestroyString(text);
        return 0;
    }
    This<void>(addr::GZStopwatch_Start, self + recorder::kWatch);
    RecorderCaption(self, "Stop");
    Virtual<void>(Virtual<void*>(At<void*>(self, recorder::kSwitched), 0xC), 0x110, uint32_t{2}, uint32_t{1});
    Virtual<void>(At<void*>(self, recorder::kManager), 0x38, static_cast<void*>(self), kRecorderTick, uint32_t{0},
                  uint32_t{0}, uint32_t{0});
    This<void>(addr::SC4UI_F795020, self);
    This<void>(addr::SC4UI_F794260, At<void*>(self, recorder::kView), uint32_t{0}, uint32_t{1});
    Virtual<void>(At<void*>(self, recorder::kView), 0x110, uint32_t{0x200000}, uint32_t{1});
    void* timer;
    FetchTimer(timer);
    if (At<uint8_t>(self, recorder::kStepped) && timer) {
        Virtual<void>(timer, 0x30, uint32_t{1});
        Virtual<void>(timer, 0x3C, At<uint32_t>(self, recorder::kInterval) * 1000u);
        At<uint32_t>(self, recorder::kCountdown) = 4;
    }
    At<uint32_t>(self, recorder::kSpare) = 0;
    This<void>(addr::SC4RefPtr_Destruct5F8F20, &timer);
    DestroyString(text);
    return 1;
}

// The game's 0x00797B00 (slot 143): a button of the window. Start or stop;
// close (a running recording stopped first, then the window base's slot
// 146 with the parent, 1, the window, 0 and 0); one frame by hand while a
// recording runs.
uint8_t __fastcall RecorderCommand(char* self, void*, uint32_t type, uint32_t id)
{
    if (type != kButtonClicked)
        return 1;
    if (id == kStartStop) {
        if (This<uint8_t>(addr::GZStopwatch_IsRunning, self + recorder::kWatch))
            RecorderStop(self, nullptr);
        else
            RecorderStart(self, nullptr);
        return 1;
    }
    if (id == kClose) {
        if (This<uint8_t>(addr::GZStopwatch_IsRunning, self + recorder::kWatch))
            RecorderStop(self, nullptr);
        void* const parent = Virtual<void*>(self, 0x2C);
        Virtual<void>(self, 0x248, parent, uint32_t{1}, static_cast<void*>(self), uint32_t{0}, uint32_t{0});
        return 1;
    }
    if (id == kOneFrame) {
        if (This<uint8_t>(addr::GZStopwatch_IsRunning, self + recorder::kWatch))
            RecorderGrab(self, nullptr);
    }
    return 1;
}

// --- The progress bar -----------------------------------------------------------------

// The game's 0x00797BB0: the class ID, for the registration at 0x004662EE.
uint32_t __cdecl BarClassId()
{
    return kBarClass;
}

// The game's 0x00797BC0 (slot 0): interfaces 1 and 0x4A5D1208 (the bar, the
// part at +0xE0), 0x22BA0121 (the window itself), the rest the window
// base's. The original's cast of a null window gives a null pointer and
// then counts through it; kept.
uint8_t __fastcall BarQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    if (iid == 1 || iid == kBarIid) {
        *out = self ? self + bar::kBar : nullptr;
        AddRef(self);
        return 1;
    }
    if (iid == kBarWindowIid) {
        *out = self;
        AddRef(self);
        return 1;
    }
    return This<uint8_t>(addr::SC4Win_QueryInterface, self, iid, out);
}

// The game's 0x00797C20 (slot 4 of the part at +0xE0): the filled share,
// held between 0 and 1, then a repaint.
void __fastcall BarSetShare(char* part, void*, double share)
{
    char* const self = part - bar::kBar;
    if (share < 0.0)
        share = 0.0;
    else if (share > 1.0)
        share = 1.0;
    At<double>(self, bar::kShare) = share;
    Virtual<void>(self, 0x170);
}

// The game's 0x00797CA0 (slot 0 of the part at +0xD8): the destructor
// through the counted part.
char* __fastcall BarDelete(char* self, void*, uint32_t flags);
char* __fastcall BarDeleteCounted(char* part, void*, uint32_t flags)
{
    return BarDelete(part - bar::kCounted, nullptr, flags);
}

// The game's 0x00797CB0 (slot 0 of the part at +0xE0).
uint8_t __fastcall BarQueryInterfaceBar(char* part, void*, uint32_t iid, void** out)
{
    return BarQueryInterface(part - bar::kBar, nullptr, iid, out);
}

// The game's 0x00797E10 (slot 3 of the part at +0xE0): the picture, then a
// repaint.
void __fastcall BarSetPicture(char* part, void*, void* picture)
{
    Hold(At<void*>(part, bar::kPicture - bar::kBar), picture);
    Virtual<void>(part - bar::kBar, 0x170);
}

// The game's 0x00797E60: the constructor. The share starts at one half.
char* __fastcall BarConstruct(char* self, void*)
{
    This<void*>(addr::GZWin_Construct, self);
    This<void*>(addr::cRZBaseRefCount_Construct, self + bar::kCounted);
    At<std::uintptr_t>(self, bar::kBar) = addr::SC4ProgressBar_BarBaseVtable;
    At<double>(self, bar::kShare) = 0.5;
    At<std::uintptr_t>(self, bar::kCounted) = addr::SC4ProgressBar_CountedVtable;
    At<std::uintptr_t>(self, 0x0) = addr::SC4ProgressBar_Vtable;
    At<std::uintptr_t>(self, bar::kBar) = addr::SC4ProgressBar_BarVtable;
    At<void*>(self, bar::kPicture) = nullptr;
    return self;
}

// The game's 0x00797EC0 (slot 148): the deleting destructor.
char* __fastcall BarDelete(char* self, void*, uint32_t flags)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4ProgressBar_Vtable;
    At<std::uintptr_t>(self, bar::kCounted) = addr::SC4ProgressBar_CountedVtable;
    At<std::uintptr_t>(self, bar::kBar) = addr::SC4ProgressBar_BarVtable;
    if (void* const picture = At<void*>(self, bar::kPicture))
        Release(picture);
    This<void>(addr::cRZBaseRefCount_Destruct, self + bar::kCounted);
    This<void>(addr::GZWin_Destruct, self);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// The game's 0x00797F20: the class's factory; gives the bar's interface.
void* __cdecl BarCreate()
{
    char* const memory = Cdecl<char*>(addr::SC4_operator_new, bar::kSize);
    if (memory) {
        if (char* const made = BarConstruct(memory, nullptr))
            return made + bar::kBar;
    }
    return nullptr;
}

// --- The info tip ---------------------------------------------------------------------

// The inlined colour key of a picture just set: the colour service's slot
// 34 makes magenta, the picture's slot 23 takes it.
void TipKeyOut(void*& holder)
{
    void* const colours = Cdecl<void*>(addr::SC4_Singleton913C1A);
    void* const picture = holder;
    Virtual<void>(picture, 0x5C, Virtual<uint32_t>(colours, 0x88, uint32_t{0xFF}, uint32_t{0}, uint32_t{0xFF}));
}

void TipRepaint(char* self)
{
    Virtual<void>(self + tip::kWindow, 0x170);
}

// The game's 0x00797F50 (slot 0): interface 0xC9B432CF is the tip, the
// rest the window's.
uint8_t __fastcall TipQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    if (iid == kTipIid) {
        *out = self;
        AddRef(self);
        return 1;
    }
    return This<uint8_t>(addr::SC4Win_QueryInterface, self + tip::kWindow, iid, out);
}

// The shared body of the two text setters: nothing when the text is the
// same (the string's slot 10 with 1), else copied (slot 13).
void TipSetString(char* self, std::size_t field, void* text)
{
    void* const mine = self + field;
    if (Virtual<uint8_t>(mine, 0x28, text, uint32_t{1}))
        return;
    Virtual<void>(mine, 0x34, text);
    At<uint32_t>(self, tip::kStale) |= 1;
    TipRepaint(self);
}

// The game's 0x00797F80 (slot 6): the title.
void __fastcall TipSetTitle(char* self, void*, void* text)
{
    TipSetString(self, tip::kTitle, text);
}

// The game's 0x00797FD0 (slot 7).
void* __fastcall TipTitle(char* self, void*)
{
    return self + tip::kTitle;
}

// The game's 0x00797FE0 (slot 8): the text.
void __fastcall TipSetText(char* self, void*, void* text)
{
    TipSetString(self, tip::kText, text);
}

// The game's 0x00798050 (slot 18).
void* __fastcall TipClear(char* self, void*)
{
    return self + tip::kClear;
}

// The game's 0x00798060 (slot 20).
void* __fastcall TipBounds(char* self, void*)
{
    return self + tip::kBounds;
}

// The game's 0x007980D0 (slot 4 of the window at +4): Init. The window's
// ID and flags, the default font for either text that has none, and, while
// the rectangle to stay inside is empty, the window laid over the whole of
// the manager's window with that rectangle 16 pixels inside it.
uint8_t __fastcall TipInit(char* window, void*)
{
    if (This<uint8_t>(addr::GZWin_IsInitialized, window))
        return 1;
    Virtual<void>(window, 0x100, kTipWindowId);
    Virtual<void>(window, 0x110, uint32_t{0x10000}, uint32_t{0});
    Virtual<void>(window, 0x110, uint32_t{4}, uint32_t{1});
    This<uint8_t>(addr::GZWin_Init, window);
    Cdecl<void*>(addr::SC4_Singleton913C72);
    char* const self = window - tip::kWindow;
    if (!At<void*>(self, tip::kTextFont))
        Virtual<void>(self, 0x2C, kTipFont);
    if (!At<void*>(self, tip::kTitleFont))
        Virtual<void>(self, 0x28, kTipFont);
    Rect& bounds = At<Rect>(self, tip::kBounds);
    if (bounds.right != bounds.left)
        return 1;
    Rect whole;
    Virtual<void>(Virtual<void*>(At<void*>(window, 0x4), 0xC), 0xBC, &whole);
    const Point* const origin = Virtual<const Point*>(Virtual<void*>(window, 0x2C), 0xC8);
    const int32_t y = origin->y;
    const int32_t x = origin->x;
    Rect area;
    area.left = whole.left - x;
    area.top = whole.top - y;
    area.right = whole.right - x;
    area.bottom = whole.bottom - y;
    Virtual<void>(window, 0xD8, &area);
    bounds.left = whole.left + 0x10;
    bounds.top = whole.top + 0x10;
    bounds.right = whole.right - 0x10;
    bounds.bottom = whole.bottom - 0x10;
    return 1;
}

// The game's 0x00798220 (slot 5 of the window at +4): Shutdown.
uint8_t __fastcall TipShutdown(char* window, void*)
{
    if (!This<uint8_t>(addr::GZWin_IsInitialized, window))
        return 1;
    char* const self = window - tip::kWindow;
    Clear(At<void*>(self, tip::kFrame));
    Clear(At<void*>(self, tip::kIcon));
    Clear(At<void*>(self, tip::kSurface));
    Clear(At<void*>(self, tip::kBubble));
    Clear(At<void*>(self, tip::kTextFont));
    Clear(At<void*>(self, tip::kTitleFont));
    return This<uint8_t>(addr::SC4Win_F99D2FE, window);
}

// The game's 0x007982C0 (slot 4): the bubble's picture (five by five
// pieces, the paint cuts it up) and the two margins around the contents (a
// negative one stands for 8 and 3).
void __fastcall TipSetFrame(char* self, void*, void* picture, int32_t marginX, int32_t marginY)
{
    if (At<void*>(self, tip::kFrame) != picture) {
        Hold(At<void*>(self, tip::kFrame), picture);
        At<uint32_t>(self, tip::kStale) |= 3;
        if (At<void*>(self, tip::kFrame))
            TipKeyOut(At<void*>(self, tip::kFrame));
        TipRepaint(self);
    }
    At<int32_t>(self, tip::kMarginX) = marginX < 0 ? 8 : marginX;
    At<int32_t>(self, tip::kMarginY) = marginY < 0 ? 3 : marginY;
}

// The shared body of the two font setters: the font service's slot 5 by
// the font's ID.
void TipSetFont(char* self, std::size_t field, uint32_t id)
{
    void* const fonts = Cdecl<void*>(addr::SC4_Singleton913C72);
    void* const font = Virtual<void*>(fonts, 0x14, id);
    Hold(At<void*>(self, field), font);
    At<uint32_t>(self, tip::kStale) |= 1;
    TipRepaint(self);
}

// The game's 0x00798380 (slot 10): the title's font.
void __fastcall TipSetTitleFont(char* self, void*, uint32_t id)
{
    TipSetFont(self, tip::kTitleFont, id);
}

// The game's 0x007983E0 (slot 11): the text's font.
void __fastcall TipSetTextFont(char* self, void*, uint32_t id)
{
    TipSetFont(self, tip::kTextFont, id);
}

// The game's 0x00798440 (slot 12): the icon beside the texts.
void __fastcall TipSetIcon(char* self, void*, void* picture)
{
    if (At<void*>(self, tip::kIcon) == picture)
        return;
    Hold(At<void*>(self, tip::kIcon), picture);
    At<uint32_t>(self, tip::kStale) |= 1;
    if (At<void*>(self, tip::kIcon))
        TipKeyOut(At<void*>(self, tip::kIcon));
    TipRepaint(self);
}

// The game's 0x007984D0 (slot 14): the meter: its picture, how many of it
// in a row, and the share of the row that is filled (held between 0 and
// 1). A new share alone asks for a repaint without marking anything stale.
void __fastcall TipSetMeter(char* self, void*, void* picture, uint32_t count, float share)
{
    if (static_cast<double>(share) < 0.0)
        share = 0.0f;
    if (static_cast<double>(share) > 1.0)
        share = 1.0f;
    if (At<void*>(self, tip::kMeter) != picture) {
        Hold(At<void*>(self, tip::kMeter), picture);
        At<uint32_t>(self, tip::kStale) |= 1;
        if (At<void*>(self, tip::kMeter))
            TipKeyOut(At<void*>(self, tip::kMeter));
        TipRepaint(self);
    }
    if (count != At<uint32_t>(self, tip::kMeterCount)) {
        At<uint32_t>(self, tip::kMeterCount) = count;
        At<uint32_t>(self, tip::kStale) |= 1;
        TipRepaint(self);
    }
    if (!(At<float>(self, tip::kMeterShare) == share)) {
        At<float>(self, tip::kMeterShare) = share;
        TipRepaint(self);
    }
}

// The game's 0x007985F0 (slot 15): the point the tip points at.
void __fastcall TipSetAnchor(char* self, void*, const Point* point)
{
    Point& mine = At<Point>(self, tip::kAnchor);
    if (mine.x == point->x && mine.y == point->y)
        return;
    mine.x = point->x;
    mine.y = point->y;
    At<uint32_t>(self, tip::kStale) |= 2;
    TipRepaint(self);
}

// The shared body of the two rectangle setters.
void TipSetRect(char* self, std::size_t field, const Rect* rect)
{
    Rect& mine = At<Rect>(self, field);
    if (mine.left == rect->left && mine.top == rect->top && mine.right == rect->right && mine.bottom == rect->bottom)
        return;
    mine.left = rect->left;
    mine.top = rect->top;
    mine.right = rect->right;
    mine.bottom = rect->bottom;
    At<uint32_t>(self, tip::kStale) |= 2;
    TipRepaint(self);
}

// The game's 0x00798630 (slot 17): the rectangle to keep clear.
void __fastcall TipSetClear(char* self, void*, const Rect* rect)
{
    TipSetRect(self, tip::kClear, rect);
}

// The game's 0x007986A0 (slot 19): the rectangle to stay inside.
void __fastcall TipSetBounds(char* self, void*, const Rect* rect)
{
    TipSetRect(self, tip::kBounds, rect);
}

bool InstallPanels5(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[32] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F797200, reinterpret_cast<void*>(&RecorderWrite)},
        {addr::SC4UI_F797290, reinterpret_cast<void*>(&RecorderStop)},
        {addr::SC4UI_F797430, reinterpret_cast<void*>(&RecorderGrab)},
        {addr::SC4UI_F797530, reinterpret_cast<void*>(&RecorderShutdown)},
        {addr::SC4UI_F7976E0, reinterpret_cast<void*>(&RecorderMessage)},
        {addr::SC4UI_F7977D0, reinterpret_cast<void*>(&RecorderStart)},
        {addr::SC4UI_F797B00, reinterpret_cast<void*>(&RecorderCommand)},
        {addr::SC4UI_F797BB0, reinterpret_cast<void*>(&BarClassId)},
        {addr::SC4UI_F797BC0, reinterpret_cast<void*>(&BarQueryInterface)},
        {addr::SC4UI_F797C20, reinterpret_cast<void*>(&BarSetShare)},
        {addr::SC4UI_F797CA0, reinterpret_cast<void*>(&BarDeleteCounted)},
        {addr::SC4UI_F797CB0, reinterpret_cast<void*>(&BarQueryInterfaceBar)},
        {addr::SC4UI_F797E10, reinterpret_cast<void*>(&BarSetPicture)},
        {addr::SC4UI_F797E60, reinterpret_cast<void*>(&BarConstruct)},
        {addr::SC4UI_F797EC0, reinterpret_cast<void*>(&BarDelete)},
        {addr::SC4UI_F797F20, reinterpret_cast<void*>(&BarCreate)},
        {addr::SC4UI_F797F50, reinterpret_cast<void*>(&TipQueryInterface)},
        {addr::SC4UI_F797F80, reinterpret_cast<void*>(&TipSetTitle)},
        {addr::SC4UI_F797FD0, reinterpret_cast<void*>(&TipTitle)},
        {addr::SC4UI_F797FE0, reinterpret_cast<void*>(&TipSetText)},
        {addr::SC4UI_F798050, reinterpret_cast<void*>(&TipClear)},
        {addr::SC4UI_F798060, reinterpret_cast<void*>(&TipBounds)},
        {addr::SC4UI_F7980D0, reinterpret_cast<void*>(&TipInit)},
        {addr::SC4UI_F798220, reinterpret_cast<void*>(&TipShutdown)},
        {addr::SC4UI_F7982C0, reinterpret_cast<void*>(&TipSetFrame)},
        {addr::SC4UI_F798380, reinterpret_cast<void*>(&TipSetTitleFont)},
        {addr::SC4UI_F7983E0, reinterpret_cast<void*>(&TipSetTextFont)},
        {addr::SC4UI_F798440, reinterpret_cast<void*>(&TipSetIcon)},
        {addr::SC4UI_F7984D0, reinterpret_cast<void*>(&TipSetMeter)},
        {addr::SC4UI_F7985F0, reinterpret_cast<void*>(&TipSetAnchor)},
        {addr::SC4UI_F798630, reinterpret_cast<void*>(&TipSetClear)},
        {addr::SC4UI_F7986A0, reinterpret_cast<void*>(&TipSetBounds)},
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

const bool g_panels5Registered = registry::Register({
    "ui.panels.part5",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F7977D0,
    "Our animation recorder controls, progress bar and the info tip's setters (docs/ui.md)",
    false,
    InstallPanels5,
});

} // namespace
} // namespace opensc4::ui
