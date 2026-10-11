// The city view's animation recorder (0x007946A0 to 0x00797200): a small
// window of the developers that copies a part of the screen at a steady
// rate and writes the pictures out as numbered files. The city view brings
// it up (0x007F0840) on its command 0x6976AFA0 and from its key handler at
// 0x007F31D4.
//
// Two window classes share the range, both made in code and without class
// IDs:
//
// The recorder (0x218 bytes, vtable 0x00AB6010, constructor 0x00796380) is
// the panel: three edit fields (name, rate in milliseconds, time in
// seconds), a list of file types (bmp, png, jpg), the buttons Start, Close
// and Snap and a status text. Its settings live in the section "Animation
// Recorder" of the application's ini file. Its slot 4 (0x00796630) builds
// all of that and the frame below; slots 3, 5 and 143 (the window
// procedure, the shutdown and the timer, from 0x00797200 on) are the next
// file's.
//
// The frame (0xFC bytes, vtable 0x00AB5DA8, constructor 0x00794430) is the
// rectangle on the city view that marks what is recorded. The player drags
// it by its inside and sizes it by its edges and corners: the hit test
// 0x007944C0 gives a mode (1 left, 2 right, 3 top, 4 bottom, 5 top left,
// 6 top right, 7 bottom left, 8 bottom right) and a cursor. Its first
// methods are the previous file's; its mouse handlers and its Init are here.
//
// Replacement, off by default:
//
//   ui.panels.part4: the recorder's constructor, destructors, Init, its
//                    settings, frame buffers, file writing, status text and
//                    helpers, and the frame's Init and mouse handlers.
//
// Left the game's as drawing: 0x00794BF0, the frame's paint (slot 0x160):
// four lines on the window's surface and, while it is dragged, its size as
// a text.
//
// Not functions: 0x00794820 is the first case of the switch in 0x007947D0,
// 0x00794A00 that switch's table, and 0x007950A0 the second branch of
// 0x00795020. A function the list does not have starts at 0x00794A20.
//
// The recorder, after the window base (0xD8 bytes): +0xD8 the frame, +0xDC
// the status text, +0xE0 the name field and +0xE4 the name, +0xF8 the rate
// field and +0xFC the rate's text, +0x110 the time field and +0x114 the
// time's text, +0x128 Start, +0x12C Close, +0x130 Snap, +0x134 a check box
// that nothing here makes, +0x138 "delete bitmaps when finished" (a byte),
// +0x13C the list of file types, +0x140 the background colour, +0x144 and
// +0x148 where the mouse took hold of the panel, +0x14C an interface
// (0xE69BFE2A) of the service at 0x00B43CA4 (the next file knows it as the
// driver's screen capture), +0x150 the ini file's path,
// +0x164 a vector of the files written (strings), +0x170 the most frames
// (0x80), +0x174 the most pixels in all (0x8000000), +0x178 the output
// folder, +0x18C the file name, +0x1A0 the file type (1 bmp, 2 png, 3 jpg),
// +0x1A4 whether gx.exe was found, +0x1A8 its folder, +0x1BC a byte (1:
// the recorder steps the game's clock), +0x1C0 the run's number, +0x1C4
// the frames taken, +0x1C8, +0x1CC, +0x208 and +0x20C the next file's
// (interval, length, frames left, countdown), +0x1D0 the frames wanted,
// +0x1D4 the rectangle recorded (left, top, right, bottom), +0x1E4 a vector
// of the frames' buffers, +0x1F0 a stopwatch (0x18 bytes, mode 4), +0x210
// the number of the next file. ui_panels_5.cpp calls +0xD8 the view
// recorded, +0xE0 the folder's edit box and +0x18C the folder's text; by
// the code here they are the frame window, the name's field (key "Name")
// and the name that goes into the file names.
//
// The frame, after the window base: +0xD8 whether it may be moved, +0xD9
// whether it may be sized (bytes, both set by 0x00794260), +0xDA a byte:
// the mouse holds it, +0xDC the mode, +0xE0, +0xE4 and +0xE8 three colours
// (0xFFFFFF01 until made), +0xEC the cursor shown, +0xF0 and +0xF4 the
// mouse's last place, +0xF8 the font of the size text. The window base
// keeps the area at +0xA8 and the window manager at +4.
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

namespace recorder {
constexpr std::size_t kFrame = 0xD8;
constexpr std::size_t kStatus = 0xDC;
constexpr std::size_t kNameEdit = 0xE0;
constexpr std::size_t kName = 0xE4;
constexpr std::size_t kRateEdit = 0xF8;
constexpr std::size_t kRate = 0xFC;
constexpr std::size_t kTimeEdit = 0x110;
constexpr std::size_t kTime = 0x114;
constexpr std::size_t kStart = 0x128;
constexpr std::size_t kClose = 0x12C;
constexpr std::size_t kSnap = 0x130;
constexpr std::size_t kDeleteCheck = 0x134;
constexpr std::size_t kDeleteBitmaps = 0x138;
constexpr std::size_t kTypeList = 0x13C;
constexpr std::size_t kBackground = 0x140;
constexpr std::size_t kHoldX = 0x144;
constexpr std::size_t kHoldY = 0x148;
constexpr std::size_t kCapture = 0x14C;
constexpr std::size_t kIniPath = 0x150;
constexpr std::size_t kFiles = 0x164;
constexpr std::size_t kFilesEnd = 0x168;
constexpr std::size_t kFilesCapacity = 0x16C;
constexpr std::size_t kMaxFrames = 0x170;
constexpr std::size_t kMaxPixels = 0x174;
constexpr std::size_t kOutputDir = 0x178;
constexpr std::size_t kFileName = 0x18C;
constexpr std::size_t kType = 0x1A0;
constexpr std::size_t kHasGx = 0x1A4;
constexpr std::size_t kGxDir = 0x1A8;
constexpr std::size_t kStepped = 0x1BC;
constexpr std::size_t kRun = 0x1C0;
constexpr std::size_t kFrameCount = 0x1C4;
constexpr std::size_t kInterval = 0x1C8;
constexpr std::size_t kSeconds = 0x1CC;
constexpr std::size_t kWanted = 0x1D0;
constexpr std::size_t kLeft = 0x1D4;
constexpr std::size_t kTop = 0x1D8;
constexpr std::size_t kRight = 0x1DC;
constexpr std::size_t kBottom = 0x1E0;
constexpr std::size_t kBuffers = 0x1E4;
constexpr std::size_t kBuffersEnd = 0x1E8;
constexpr std::size_t kBuffersCapacity = 0x1EC;
constexpr std::size_t kWatch = 0x1F0;
constexpr std::size_t kFramesLeft = 0x208;
constexpr std::size_t kCountdown = 0x20C;
constexpr std::size_t kFileIndex = 0x210;
} // namespace recorder

namespace frame {
constexpr std::size_t kSizable = 0xD9;
constexpr std::size_t kHeld = 0xDA;
constexpr std::size_t kMode = 0xDC;
constexpr std::size_t kColour1 = 0xE0;
constexpr std::size_t kColour2 = 0xE4;
constexpr std::size_t kColour3 = 0xE8;
constexpr std::size_t kCursor = 0xEC;
constexpr std::size_t kLastX = 0xF0;
constexpr std::size_t kLastY = 0xF4;
constexpr std::size_t kFont = 0xF8;
} // namespace frame

// The window base.
constexpr std::size_t kWinManager = 0x4;
constexpr std::size_t kWinArea = 0xA8;
// A text or an edit field made in code: the control's interface inside the
// window object.
constexpr std::size_t kControlPart = 0xD8;

constexpr uint32_t kColourUnset = 0xFFFFFF01;
constexpr uint32_t kRecorderId = 0x6ACB1AF1;
constexpr uint32_t kFrameId = 0x6ACB1C40;
constexpr uint32_t kStartId = 0x080293A3;
constexpr uint32_t kCloseId = 0x080293A4;
constexpr uint32_t kSnapId = 0x080293A5;
constexpr uint32_t kCaptureIid = 0xE69BFE2A;

const char kSection[] = "Animation Recorder";
const char kKeyName[] = "Name";
const char kKeyRate[] = "Rate";
const char kKeyTime[] = "Time";
const char kKeyRun[] = "Run";
const char kKeyDelete[] = "Delete bitmaps when finished";
const char kKeyOutput[] = "Output Directory";
const char kKeyGx[] = "GX Directory";
const char kKeyType[] = "Output Type";

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

// The game's ini file reader on the stack.
struct IniFile {
    alignas(4) char bytes[0x3C];
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

void Free(void* memory)
{
    Cdecl<void>(addr::SC4_Free, memory);
}

int32_t Add(int32_t a, int32_t b)
{
    return static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b));
}

int32_t Sub(int32_t a, int32_t b)
{
    return static_cast<int32_t>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b));
}

// The game's inlined string constructor.
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

void DestroyString(GameString& text)
{
    char* const begin = text.body.begin;
    text.vtable = addr::SC4String_Vtable;
    if (begin)
        Free(begin);
}

// A string made of a range of characters.
void MakeCopy(GameString& text, const char* begin, const char* end, uint8_t* tag)
{
    text.body.begin = nullptr;
    text.body.end = nullptr;
    text.body.capacity = nullptr;
    This<void>(addr::cSC4Advice_F408420, &text.body, begin, end, tag);
    text.vtable = addr::SC4String_Vtable;
    text.zero = 0;
}

// A copy of a string's buffer made by the game's substring constructor.
void CopyBody(StringBody& body, char* source, uint8_t* tag)
{
    This<void>(addr::SC4String_CopyBuffer, &body, static_cast<void*>(source ? source + 4 : nullptr), uint32_t{0},
               uint32_t{0xFFFFFFFF}, tag);
}

// An ini key set from a text: the text made into a buffer of its own
// (0x00407FA0), assigned, and the buffer freed.
void KeyFromRange(GameString& key, const char* text, uint8_t* tag)
{
    StringBody made;
    made.begin = nullptr;
    made.end = nullptr;
    made.capacity = nullptr;
    This<void>(addr::SC4String_FromRange, &made, text, text + std::strlen(text), tag);
    char* const begin = made.begin;
    This<void*>(addr::SC4String_AssignRange, &key.body, begin, made.end, tag);
    if (begin)
        Free(begin);
}

// The same through a whole string (0x00408480).
void KeyFromText(GameString& key, const char* text, uint8_t* tag)
{
    GameString made;
    This<void*>(addr::SC4String_FromText, &made, text);
    char* const begin = made.body.begin;
    This<void*>(addr::SC4String_AssignRange, &key.body, begin, made.body.end, tag);
    if (begin)
        Free(begin);
}

// The mouse cursors of the window's manager.
void* Cursors(char* window)
{
    return This<void*>(addr::SC4Win_Cursors, window);
}

// The game's 0x007946A0 (slot 0x228 of the recorder; thiscall, ret 0xC):
// the mouse has moved. While the panel holds the mouse (taken by slot 0x218,
// 0x00794360) the panel follows it: it moves by the distance from where it
// was taken hold of. The two places are written back as they were.
uint8_t __fastcall RecorderMouseMove(char* self, void*, int32_t x, int32_t y, uint32_t)
{
    if (This<uint8_t>(addr::SC4Win_F99BE0A, self)) {
        const int32_t dy = Sub(y, At<int32_t>(self, recorder::kHoldY));
        const int32_t dx = Sub(x, At<int32_t>(self, recorder::kHoldX));
        Virtual<void>(self, 0xE4, dx, dy);
        At<int32_t>(self, recorder::kHoldX) = Sub(x, dx);
        At<int32_t>(self, recorder::kHoldY) = Sub(y, dy);
    }
    return 1;
}

// The game's 0x00794700 (thiscall, ret 4). `this` is the recorder. The
// rectangle to record: the frame's area less one pixel on every side, and
// at least one pixel wide and high.
void __fastcall GetRecordedRect(char* self, void*, int32_t* out)
{
    const int32_t* const area = Virtual<const int32_t*>(At<void*>(self, recorder::kFrame), 0xC0);
    out[0] = area[0];
    out[1] = area[1];
    out[2] = area[2];
    out[3] = area[3];
    const int32_t left = Add(out[0], 1);
    const int32_t right = Sub(out[2], 1);
    const int32_t top = Add(out[1], 1);
    const int32_t bottom = Sub(out[3], 1);
    out[2] = right;
    out[0] = left;
    out[1] = top;
    out[3] = bottom;
    if (!(Sub(right, left) > 0))
        out[2] = Add(left, 1);
    if (!(Sub(bottom, top) > 0))
        out[3] = Add(top, 1);
}

// The game's 0x00794760 (slot 0x218 of the frame; thiscall, ret 0xC): a
// mouse button went down. If the hit test finds the frame's inside or an
// edge the frame takes the mouse and notes the mode and the place. The hit
// test's cursor goes to the stack slot of the second argument, which holds
// y when it is called.
uint8_t __fastcall FrameMouseDown(char* self, void*, int32_t x, int32_t y, uint32_t)
{
    uint32_t mode = 0;
    int32_t cursor = y;
    if (This<uint8_t>(addr::SC4UI_F7944C0, self, x, y, &mode, &cursor)) {
        At<uint32_t>(self, frame::kMode) = mode;
        At<uint8_t>(self, frame::kHeld) = 1;
        This<void>(addr::SC4Win_F99BDFE, self);
        At<int32_t>(self, frame::kLastX) = x;
        At<int32_t>(self, frame::kLastY) = y;
        Virtual<void>(self, 0x170);
    }
    return 1;
}

// The game's 0x007947D0 (slot 0x228 of the frame; thiscall, ret 0xC): the
// mouse has moved. Holding the mouse, the frame moves (mode 0) or the edges
// of its mode follow the mouse, never closer than 8 pixels to the opposite
// edge; otherwise the cursor is set to what the hit test gives for the
// place. The switch's table is at 0x00794A00.
uint8_t __fastcall FrameMouseMove(char* self, void*, int32_t x, int32_t y, uint32_t)
{
    if (This<uint8_t>(addr::SC4Win_F99BE0A, self)) {
        int32_t* const area = &At<int32_t>(self, kWinArea);
        int32_t left = area[0];
        int32_t top = area[1];
        int32_t right = area[2];
        int32_t bottom = area[3];
        int32_t rect[4] = {left, top, right, bottom};
        switch (At<uint32_t>(self, frame::kMode)) {
        case 1:
            left = Add(area[0], x);
            rect[0] = left;
            break;
        case 2:
            right = Add(area[0], x);
            rect[2] = right;
            break;
        case 3:
            top = Add(area[1], y);
            rect[1] = top;
            break;
        case 4:
            bottom = Add(area[1], y);
            rect[3] = bottom;
            break;
        case 5:
            left = Add(area[0], x);
            rect[0] = left;
            top = Add(area[1], y);
            rect[1] = top;
            break;
        case 6:
            right = Add(area[0], x);
            top = Add(area[1], y);
            rect[2] = right;
            rect[1] = top;
            break;
        case 7:
            left = Add(area[0], x);
            rect[0] = left;
            bottom = Add(area[1], y);
            rect[3] = bottom;
            break;
        case 8:
            right = Add(area[0], x);
            bottom = Add(area[1], y);
            rect[2] = right;
            rect[3] = bottom;
            break;
        default:
            if (At<uint8_t>(self, frame::kHeld)) {
                const int32_t dx = Sub(x, At<int32_t>(self, frame::kLastX));
                const int32_t dy = Sub(y, At<int32_t>(self, frame::kLastY));
                Virtual<void>(self, 0xE4, dx, dy);
                x = Sub(x, dx);
                y = Sub(y, dy);
            }
            break;
        }
        if (At<uint32_t>(self, frame::kMode) != 0) {
            right = Add(right, -8);
            if (left > right)
                rect[0] = right;
            bottom = Add(bottom, -8);
            if (top > bottom)
                rect[1] = bottom;
            if (Cdecl<uint8_t>(addr::SC4Rect_Differs, &rect[0], area)) {
                Virtual<void>(self, 0xD8, &rect[0]);
                Virtual<void>(self, 0x170);
            }
        }
    } else {
        uint32_t mode = 0;
        uint32_t cursor = 0;
        This<uint8_t>(addr::SC4UI_F7944C0, self, x, y, &mode, &cursor);
        if (At<uint32_t>(self, frame::kCursor) != cursor) {
            At<uint32_t>(self, frame::kCursor) = cursor;
            if (cursor != 0) {
                void* const cursors = Cursors(self);
                void* const shape = Virtual<void*>(cursors, 0x24, At<uint32_t>(self, frame::kCursor));
                Virtual<void>(self, 0x154, shape, uint32_t{0});
            } else {
                Virtual<void>(self, 0x154, uint32_t{0}, uint32_t{0});
            }
        }
        if (At<uint8_t>(self, frame::kHeld)) {
            At<uint8_t>(self, frame::kHeld) = 0;
            Virtual<void>(self, 0x170);
        }
    }
    At<int32_t>(self, frame::kLastX) = x;
    At<int32_t>(self, frame::kLastY) = y;
    return 1;
}

// The game's 0x00794A20 (thiscall, no arguments). `this` is the recorder.
// Lets every frame buffer go and empties their vector. The original's erase
// of the empty range at the end does nothing.
uint8_t __fastcall ReleaseFrames(char* self, void*)
{
    void** const begin = At<void**>(self, recorder::kBuffers);
    void** const end = At<void**>(self, recorder::kBuffersEnd);
    if (begin == end)
        return 1;
    const int32_t count = static_cast<int32_t>(end - begin);
    for (int32_t i = 0; i < count; ++i)
        Release(At<void**>(self, recorder::kBuffers)[i]);
    At<void**>(self, recorder::kBuffersEnd) = At<void**>(self, recorder::kBuffers);
    return 1;
}

// The game's 0x00794AA0 (slot 4 of the frame; thiscall, no arguments): Init.
// The three colours not set by 0x007942F0 become black, white and a dark
// red; the font of the size text is Arial MT at 14; the cursor is the hand
// (0x11) when the frame may be sized.
uint8_t __fastcall FrameInit(char* self, void*)
{
    if (This<uint8_t>(addr::GZWin_IsInitialized, self))
        return 1;
    if (Virtual<uint8_t>(self, 0x10C, uint32_t{0x10000})) {
        Virtual<void>(self, 0x110, uint32_t{0x20000}, uint32_t{1});
        Virtual<void>(self, 0x110, uint32_t{0x40000}, uint32_t{1});
    }
    This<uint8_t>(addr::GZWin_Init, self);
    if (At<uint32_t>(self, frame::kColour1) == kColourUnset)
        At<uint32_t>(self, frame::kColour1) = Virtual<uint32_t>(self, 0x1B0, uint32_t{0}, uint32_t{0}, uint32_t{0});
    if (At<uint32_t>(self, frame::kColour2) == kColourUnset)
        At<uint32_t>(self, frame::kColour2) =
            Virtual<uint32_t>(self, 0x1B0, uint32_t{0xFF}, uint32_t{0xFF}, uint32_t{0xFF});
    if (At<uint32_t>(self, frame::kColour3) == kColourUnset)
        At<uint32_t>(self, frame::kColour3) =
            Virtual<uint32_t>(self, 0x1B0, uint32_t{0xC8}, uint32_t{0}, uint32_t{0});
    if (!At<void*>(self, frame::kFont)) {
        void* const fonts = Cdecl<void*>(addr::SC4_Singleton913D22);
        GameString face;
        void* const made = This<void*>(addr::SC4String_FromText, &face, "Arial MT");
        At<void*>(self, frame::kFont) =
            Virtual<void*>(fonts, 0x2C, made, uint32_t{0}, uint32_t{0xE}, uint32_t{0}, uint32_t{1});
        DestroyString(face);
    }
    if (At<uint8_t>(self, frame::kSizable)) {
        void* const cursors = Cursors(self);
        void* const shape = Virtual<void*>(cursors, 0x24, uint32_t{0x11});
        Virtual<void>(self, 0x154, shape, uint32_t{0});
    } else {
        Virtual<void>(self, 0x154, uint32_t{0}, uint32_t{0});
    }
    return 1;
}

// The game's 0x00794DA0 (thiscall, no arguments): the recorder's
// destructor. It frees the buffers of its strings and vectors and ends in
// the window base's destructor; the references it holds are let go by the
// shutdown (slot 5), not here.
void __fastcall RecorderDestruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4AnimationRecorder_Vtable;
    This<void>(addr::SC4Stopwatch_Destruct, self + recorder::kWatch);
    if (void* const buffers = At<void*>(self, recorder::kBuffers))
        Free(buffers);
    DestroyString(At<GameString>(self, recorder::kGxDir));
    DestroyString(At<GameString>(self, recorder::kFileName));
    DestroyString(At<GameString>(self, recorder::kOutputDir));
    char* const filesEnd = At<char*>(self, recorder::kFilesEnd);
    for (char* file = At<char*>(self, recorder::kFiles); file != filesEnd; file += sizeof(GameString))
        Virtual<void>(file, 0x70, uint32_t{0});
    if (void* const files = At<void*>(self, recorder::kFiles))
        Free(files);
    DestroyString(At<GameString>(self, recorder::kIniPath));
    DestroyString(At<GameString>(self, recorder::kTime));
    DestroyString(At<GameString>(self, recorder::kRate));
    DestroyString(At<GameString>(self, recorder::kName));
    This<void>(addr::GZWin_Destruct, self);
}

// The game's 0x00794ED0 (thiscall, no arguments). `this` is the recorder.
// Makes the frame buffers for a recording: as many as wanted, at most
// +0x170 and at most as many as hold +0x174 pixels together, each of the
// recorded rectangle's size, colour type 9, 32 bits. If one cannot be made
// all are let go; false then.
uint8_t __fastcall AllocateFrames(char* self, void*)
{
    uint8_t ok = 1;
    void* const graphics = Cdecl<void*>(addr::SC4_Singleton913C1A);
    if (!graphics) {
        ReleaseFrames(self, nullptr);
        return 0;
    }
    uint32_t count = At<uint32_t>(self, recorder::kWanted);
    if (count > At<uint32_t>(self, recorder::kMaxFrames))
        count = At<uint32_t>(self, recorder::kMaxFrames);
    const uint32_t pixels = (At<uint32_t>(self, recorder::kBottom) - At<uint32_t>(self, recorder::kTop)) *
                            (At<uint32_t>(self, recorder::kRight) - At<uint32_t>(self, recorder::kLeft));
    if (pixels * count > At<uint32_t>(self, recorder::kMaxPixels))
        count = At<uint32_t>(self, recorder::kMaxPixels) / pixels;
    if (count == 0)
        return ok;
    uint32_t index = 0;
    do {
        if (!ok)
            break;
        void* buffer; // set by the call below, as in the original
        if (Virtual<uint8_t>(graphics, 0xC, &buffer)) {
            const uint32_t height = At<uint32_t>(self, recorder::kBottom) - At<uint32_t>(self, recorder::kTop);
            const uint32_t width = At<uint32_t>(self, recorder::kRight) - At<uint32_t>(self, recorder::kLeft);
            ok = Virtual<uint8_t>(buffer, 0xC, width, height, uint32_t{9}, uint32_t{0x20});
            if (ok) {
                void** const end = At<void**>(self, recorder::kBuffersEnd);
                if (end != At<void**>(self, recorder::kBuffersCapacity)) {
                    if (end)
                        *end = buffer;
                    At<void**>(self, recorder::kBuffersEnd) += 1;
                } else {
                    uint8_t tag;
                    This<void>(addr::SC4PtrVector_InsertN, self + recorder::kBuffers, end, &buffer, &tag,
                               uint32_t{1}, uint32_t{1});
                }
            } else {
                Release(buffer);
            }
        } else {
            ok = 0;
        }
        ++index;
    } while (index < count);
    if (!ok)
        ReleaseFrames(self, nullptr);
    return ok;
}

// The game's 0x00795020 (thiscall, no arguments; 0x007950A0 is its second
// branch). `this` is the recorder. Sets the status text: recording, with
// the count of frames, while the stopwatch runs, else stopped.
void __fastcall UpdateStatus(char* self, void*)
{
    if (This<uint8_t>(addr::GZStopwatch_IsRunning, self + recorder::kWatch)) {
        GameString text;
        InitString(text);
        Cdecl<void>(addr::cRZBaseString_Sprintf, &text, "Status: Recording.\nFrame count: %d",
                    At<uint32_t>(self, recorder::kFrameCount));
        Virtual<void>(At<void*>(self, recorder::kStatus), 0x10, &text);
        DestroyString(text);
    } else {
        GameString text;
        void* const made = This<void*>(addr::SC4String_FromText, &text, "Status: Stopped.\nFrame count: 0");
        Virtual<void>(At<void*>(self, recorder::kStatus), 0x10, made);
        DestroyString(text);
    }
}

// The game's 0x007950E0 (slot 0x250 of the recorder; thiscall, ret 4): the
// deleting destructor.
char* __fastcall RecorderDelete(char* self, void*, uint32_t flags)
{
    RecorderDestruct(self, nullptr);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// The game's 0x00795100 (thiscall, ret 8). `this` is the recorder. Sets the
// output folder and makes it end in a separator. With `keep` set, a folder
// the recorder has already (from its settings) stays if it exists or can be
// created.
void __fastcall SetOutputDir(char* self, void*, void* folder, uint8_t keep)
{
    if (keep) {
        if (At<char*>(self, recorder::kOutputDir + 4) != At<char*>(self, recorder::kOutputDir + 8)) {
            void* const current = self + recorder::kOutputDir;
            if (Cdecl<uint8_t>(addr::RZDirectory_Exists, current))
                return;
            if (Cdecl<uint8_t>(addr::RZDirectory_Create, current))
                return;
        }
    }
    Virtual<void>(self + recorder::kOutputDir, 0x34, folder);
    char* const begin = At<char*>(self, recorder::kOutputDir + 4);
    char* const end = At<char*>(self, recorder::kOutputDir + 8);
    if (begin != end) {
        const char last = begin[(end - begin) - 1];
        if (last == '\\' || last == '/')
            return;
    }
    This<void>(addr::SC4String_PushBack, self + recorder::kOutputDir + 4, uint32_t{0x5C});
}

// The game's 0x00795180 (thiscall, no arguments). `this` is the recorder.
// Finds the folder of the GX tool: unless the folder from the settings
// exists it becomes "<unknown>" and then the first folder Bin\GX\ found
// beside the application's folder or beside one of the folders above it.
// Returns, and keeps at +0x1A4, whether gx.exe is in the folder.
uint8_t __fastcall FindGx(char* self, void*)
{
    uint8_t tag;
    StringBody* const gxBody = &At<StringBody>(self, recorder::kGxDir + 4);
    if (!(gxBody->begin != gxBody->end && Cdecl<uint8_t>(addr::RZDirectory_Exists, self + recorder::kGxDir))) {
        static const char kUnknown[] = "<unknown>";
        StringBody unknown;
        unknown.begin = nullptr;
        unknown.end = nullptr;
        unknown.capacity = nullptr;
        This<void>(addr::SC4String_FromRange, &unknown, &kUnknown[0], &kUnknown[9], &tag);
        char* const unknownBegin = unknown.begin;
        if (&unknown != gxBody)
            This<void*>(addr::SC4String_AssignRange, gxBody, unknownBegin, unknown.end, &tag);
        if (unknownBegin)
            Free(unknownBegin);

        GameString folder;
        InitString(folder);
        if (void* const app = Global<void*>(addr::SC4_Service43C94)) {
            Virtual<uint8_t>(app, 0xCC, &folder); // GetAppDirectory
            GameString parts[2];
            InitString(parts[0]);
            InitString(parts[1]);
            int32_t count = 2;
            if (Cdecl<uint8_t>(addr::RZFile_SplitPath, &folder, &parts[0], &count)) {
                while (count == 2) {
                    StringBody joined1;
                    StringBody joined2;
                    StringBody joined3;
                    StringBody joined4;
                    StringBody* whole =
                        Cdecl<StringBody*>(addr::SC4String_ConcatBodyText, &joined1, &parts[1].body, "Bin");
                    whole = Cdecl<StringBody*>(addr::SC4String_ConcatChar, &joined2, whole, uint32_t{0x5C});
                    whole = Cdecl<StringBody*>(addr::SC4String_ConcatBodyText, &joined3, whole, "GX");
                    whole = Cdecl<StringBody*>(addr::SC4String_ConcatChar, &joined4, whole, uint32_t{0x5C});
                    GameString candidate;
                    MakeCopy(candidate, whole->begin, whole->end, &tag);
                    if (joined4.begin)
                        Free(joined4.begin);
                    if (joined3.begin)
                        Free(joined3.begin);
                    if (joined2.begin)
                        Free(joined2.begin);
                    if (joined1.begin)
                        Free(joined1.begin);
                    if (Cdecl<uint8_t>(addr::RZDirectory_Exists, &candidate)) {
                        This<void*>(addr::cRZBaseString_Assign, self + recorder::kGxDir, &candidate);
                        DestroyString(candidate);
                        break;
                    }
                    This<void*>(addr::SC4String_AssignRange, &folder.body, parts[1].body.begin, parts[1].body.end,
                                &tag);
                    DestroyString(candidate);
                    if (!Cdecl<uint8_t>(addr::RZFile_SplitPath, &folder, &parts[0], &count))
                        break;
                }
            }
            DestroyString(parts[1]);
            DestroyString(parts[0]);
        }
        DestroyString(folder);
    }

    StringBody joined;
    StringBody* const whole = Cdecl<StringBody*>(addr::SC4String_ConcatBodyText, &joined, gxBody, "gx.exe");
    GameString tool;
    MakeCopy(tool, whole->begin, whole->end, &tag);
    if (joined.begin)
        Free(joined.begin);
    const uint8_t found = Cdecl<uint8_t>(addr::RZFile_Exists, &tool);
    At<uint8_t>(self, recorder::kHasGx) = found;
    DestroyString(tool);
    return found;
}

// A text of the settings read into one of the recorder's strings.
void LoadText(IniFile& ini, GameString& section, GameString& key, GameString& value, char* self, std::size_t field,
              uint8_t* tag)
{
    if (This<uint8_t>(addr::cRZIniFile_GetString, &ini, &section, &key, &value)) {
        StringBody* const target = &At<StringBody>(self, field + 4);
        if (&value.body != target)
            This<void*>(addr::SC4String_AssignRange, target, value.body.begin, value.body.end, tag);
    }
}

// The game's 0x007954F0 (thiscall, no arguments). `this` is the recorder.
// Reads the settings from the section "Animation Recorder" of the ini file
// at +0x150: name, rate, time, run number, whether to delete the bitmaps,
// the two folders and the file type (kept within 0 to 3).
uint8_t __fastcall LoadSettings(char* self, void*)
{
    uint8_t tag;
    IniFile ini;
    This<void*>(addr::cRZIniFile_Construct, &ini, self + recorder::kIniPath);
    GameString section;
    section.body.begin = nullptr;
    section.body.end = nullptr;
    section.body.capacity = nullptr;
    This<void>(addr::SC4String_FromRange, &section.body, &kSection[0], &kSection[sizeof(kSection) - 1], &tag);
    section.vtable = addr::SC4String_Vtable;
    section.zero = 0;
    GameString key;
    InitString(key);
    GameString value;
    InitString(value);
    int32_t number; // set by the reader when it finds the key

    KeyFromRange(key, kKeyName, &tag);
    LoadText(ini, section, key, value, self, recorder::kName, &tag);
    KeyFromRange(key, kKeyRate, &tag);
    LoadText(ini, section, key, value, self, recorder::kRate, &tag);
    KeyFromRange(key, kKeyTime, &tag);
    LoadText(ini, section, key, value, self, recorder::kTime, &tag);

    KeyFromRange(key, kKeyRun, &tag);
    if (This<uint8_t>(addr::cRZIniFile_GetInt, &ini, &section, &key, &number))
        At<int32_t>(self, recorder::kRun) = number;

    KeyFromRange(key, kKeyDelete, &tag);
    if (This<uint8_t>(addr::cRZIniFile_GetInt, &ini, &section, &key, &number))
        At<uint8_t>(self, recorder::kDeleteBitmaps) = number != 0;

    KeyFromRange(key, kKeyOutput, &tag);
    LoadText(ini, section, key, value, self, recorder::kOutputDir, &tag);
    KeyFromRange(key, kKeyGx, &tag);
    LoadText(ini, section, key, value, self, recorder::kGxDir, &tag);

    KeyFromRange(key, kKeyType, &tag);
    if (This<uint8_t>(addr::cRZIniFile_GetInt, &ini, &section, &key, &number)) {
        At<int32_t>(self, recorder::kType) = number;
        if (number < 0)
            At<int32_t>(self, recorder::kType) = 0;
        if (At<int32_t>(self, recorder::kType) >= 4)
            At<int32_t>(self, recorder::kType) = 3;
    }

    DestroyString(value);
    DestroyString(key);
    DestroyString(section);
    This<void>(addr::cRZIniFile_Destruct, &ini);
    return 1;
}

// An edit field's text written to the settings.
void SaveEdit(IniFile& ini, GameString& section, GameString& key, GameString& value, void* edit, const char* name,
              uint8_t* tag)
{
    KeyFromText(key, name, tag);
    void* const window = Virtual<void*>(edit, 0x14);
    void* const caption = Virtual<void*>(window, 0x124);
    Virtual<void>(&value, 0x34, caption);
    This<uint8_t>(addr::cRZIniFile_SetString, &ini, &section, &key, &value);
}

// The game's 0x00795A00 (thiscall, no arguments). `this` is the recorder.
// Writes the settings: the three fields' texts, the check box, the run
// number, the two folders and the list's choice, each control only if it
// exists.
uint8_t __fastcall SaveSettings(char* self, void*)
{
    uint8_t tag;
    IniFile ini;
    This<void*>(addr::cRZIniFile_Construct, &ini, self + recorder::kIniPath);
    GameString section;
    section.body.begin = nullptr;
    section.body.end = nullptr;
    section.body.capacity = nullptr;
    This<void>(addr::SC4String_FromRange, &section.body, &kSection[0], &kSection[sizeof(kSection) - 1], &tag);
    section.vtable = addr::SC4String_Vtable;
    section.zero = 0;
    GameString key;
    InitString(key);
    GameString value;
    InitString(value);

    if (At<void*>(self, recorder::kNameEdit))
        SaveEdit(ini, section, key, value, At<void*>(self, recorder::kNameEdit), kKeyName, &tag);
    if (At<void*>(self, recorder::kRateEdit))
        SaveEdit(ini, section, key, value, At<void*>(self, recorder::kRateEdit), kKeyRate, &tag);
    if (At<void*>(self, recorder::kTimeEdit))
        SaveEdit(ini, section, key, value, At<void*>(self, recorder::kTimeEdit), kKeyTime, &tag);

    if (At<void*>(self, recorder::kDeleteCheck)) {
        KeyFromText(key, kKeyDelete, &tag);
        const uint8_t checked = Virtual<uint8_t>(At<void*>(self, recorder::kDeleteCheck), 0x28);
        This<uint8_t>(addr::cRZIniFile_SetNumber, &ini, &section, &key, uint32_t{checked ? 1u : 0u});
    }

    KeyFromRange(key, kKeyRun, &tag);
    This<uint8_t>(addr::cRZIniFile_SetNumber, &ini, &section, &key, At<uint32_t>(self, recorder::kRun));
    KeyFromRange(key, kKeyOutput, &tag);
    This<uint8_t>(addr::cRZIniFile_SetString, &ini, &section, &key, self + recorder::kOutputDir);
    KeyFromRange(key, kKeyGx, &tag);
    This<uint8_t>(addr::cRZIniFile_SetString, &ini, &section, &key, self + recorder::kGxDir);

    if (At<void*>(self, recorder::kTypeList)) {
        KeyFromText(key, kKeyType, &tag);
        const uint32_t type = Virtual<uint32_t>(At<void*>(self, recorder::kTypeList), 0x34);
        This<uint8_t>(addr::cRZIniFile_SetNumber, &ini, &section, &key, type);
    }

    DestroyString(value);
    DestroyString(key);
    DestroyString(section);
    This<void>(addr::cRZIniFile_Destruct, &ini);
    return 1;
}

// The game's 0x00795E50 (thiscall, no arguments). `this` is the recorder.
// Writes the frames taken as files "<folder><name> <run>_<number>" with the
// extension of the list's choice (1 bmp, 2 png, else jpg) through the
// application's picture writer (slot 0xA4 of the application; its slot 0x20
// with the path, the buffer and 100), replacing files that exist, and notes
// each file written at +0x164. It stops at the first frame that is missing
// or cannot be written; the file number at +0x210 moves on by the count of
// frames either way. False then, or without the application or the writer.
uint8_t __fastcall WriteFrames(char* self, void*)
{
    uint8_t ok = 1;
    void* const app = Global<void*>(addr::SC4_Service43C94);
    if (!app)
        return 0;
    void* const writer = Virtual<void*>(app, 0xA4);
    if (!writer)
        return 0;
    uint8_t tag;
    StringBody name;
    CopyBody(name, self + recorder::kFileName, &tag);
    for (int32_t index = 0; index < At<int32_t>(self, recorder::kFrameCount) && ok; ++index) {
        void* const buffer = At<void**>(self, recorder::kBuffers)[index];
        if (!buffer) {
            ok = 0;
            continue;
        }
        GameString path;
        InitString(path);
        const int32_t type = Virtual<int32_t>(At<void*>(self, recorder::kTypeList), 0x34);
        At<int32_t>(self, recorder::kType) = type;
        const char* format = "%s%s %03d_%03d.jpg";
        if (type == 1)
            format = "%s%s %03d_%03d.bmp";
        else if (type == 2)
            format = "%s%s %03d_%03d.png";
        Cdecl<void>(addr::cRZBaseString_Sprintf, &path, format, At<char*>(self, recorder::kOutputDir + 4), name.begin,
                    At<uint32_t>(self, recorder::kRun),
                    At<uint32_t>(self, recorder::kFileIndex) + static_cast<uint32_t>(index));
        if (Cdecl<uint8_t>(addr::RZFile_Exists, &path))
            Cdecl<uint8_t>(addr::RZFile_Remove, &path);
        ok = Virtual<uint8_t>(writer, 0x20, &path, buffer, uint32_t{0x64});
        if (ok)
            This<void>(addr::SC4StringVector_PushBack, self + recorder::kFiles, &path);
        DestroyString(path);
    }
    At<uint32_t>(self, recorder::kFileIndex) += At<uint32_t>(self, recorder::kFrameCount);
    if (name.begin)
        Free(name.begin);
    return ok;
}

// The game's 0x00796040 (thiscall, no arguments). `this` is the recorder.
// Tells the player that the files are written: a message box of the window
// manager (slot 0xB0) with the output folder. False, and no box, when the
// list's choice is not 1 to 3. One string is made and never used.
uint8_t __fastcall ReportWritten(char* self, void*)
{
    uint8_t tag;
    StringBody name;
    CopyBody(name, self + recorder::kFileName, &tag);
    GameString folder;
    CopyBody(folder.body, self + recorder::kOutputDir, &tag);
    folder.vtable = addr::SC4String_Vtable;
    folder.zero = 0;
    GameString spare;
    InitString(spare);
    const int32_t type = Virtual<int32_t>(At<void*>(self, recorder::kTypeList), 0x34);
    At<int32_t>(self, recorder::kType) = type;
    if (type <= 0 || type > 3) {
        DestroyString(spare);
        DestroyString(folder);
        if (name.begin)
            Free(name.begin);
        return 0;
    }
    GameString message;
    InitString(message);
    Cdecl<void>(addr::cRZBaseString_Sprintf, &message, "AnimationFrames written OK.\n\n Output is in %s",
                At<char*>(self, recorder::kOutputDir + 4));
    void* const manager = At<void*>(self, kWinManager);
    GameString title;
    void* const made = This<void*>(addr::SC4String_FromText, &title, kSection);
    Virtual<void>(manager, 0xB0, &message, made, uint32_t{0}, uint32_t{1}, uint32_t{0});
    DestroyString(title);
    DestroyString(message);
    DestroyString(spare);
    DestroyString(folder);
    if (name.begin)
        Free(name.begin);
    return 1;
}

// The game's 0x00796210 (thiscall that does not use `this`, ret 4). Cuts a
// string to 0x80 characters and keeps only its letters, digits and white
// space. The branch that would lengthen a shorter string cannot be reached.
void __fastcall CleanName(char*, void*, char* text)
{
    uint8_t tag;
    char* buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    StringBody* const source = reinterpret_cast<StringBody*>(text + 4);
    *buffer = 0;
    char* out = buffer;
    GameString kept;
    kept.body.begin = buffer;
    kept.body.end = out;
    kept.body.capacity = buffer + 8;
    kept.vtable = addr::SC4String_Vtable;
    kept.zero = 0;
    if (static_cast<uint32_t>(source->end - source->begin) > 0x80) {
        char* const end = source->end;
        char* const begin = source->begin;
        if (static_cast<uint32_t>(end - begin) >= 0x80) {
            char* const cut = begin + 0x80;
            if (cut != end) {
                Cdecl<void*>(addr::SC4_memmove, static_cast<void*>(cut), static_cast<void*>(end), uint32_t{1});
                source->end = source->end + (cut - end);
            }
        } else {
            This<void>(addr::SC4String_AppendFill, source, static_cast<uint32_t>(begin - end) + 0x80u, uint32_t{0});
        }
    }
    const int32_t length = static_cast<int32_t>(source->end - source->begin);
    for (int32_t index = 0; index < length; ++index) {
        if (Cdecl<int32_t>(addr::SC4_isalnum, static_cast<int32_t>(static_cast<int8_t>(source->begin[index]))) == 0 &&
            Cdecl<int32_t>(addr::SC4_isspace, static_cast<int32_t>(static_cast<int8_t>(source->begin[index]))) == 0)
            continue;
        const char character = source->begin[index];
        if (out + 1 == kept.body.capacity) {
            const uint32_t size = static_cast<uint32_t>(out - buffer);
            const uint32_t more = size < 1 ? 1 : size;
            This<void>(addr::cSC4Advisor_F40F1B0, &kept.body, more + size);
            out = kept.body.end;
            buffer = kept.body.begin;
        }
        out[1] = 0;
        out[0] = character;
        ++out;
        kept.body.end = out;
    }
    if (&kept.body != source)
        This<void*>(addr::SC4String_AssignRange, source, buffer, out, &tag);
    if (buffer)
        Free(buffer);
}

// A string member built from a text by 0x00407FA0.
void ConstructText(char* self, std::size_t field, const char* text, uint8_t* tag)
{
    StringBody* const body = &At<StringBody>(self, field + 4);
    body->begin = nullptr;
    body->end = nullptr;
    body->capacity = nullptr;
    This<void>(addr::SC4String_FromRange, body, text, text + std::strlen(text), tag);
    At<uint32_t>(self, field + 0x10) = 0;
    At<std::uintptr_t>(self, field) = addr::SC4String_Vtable;
}

// The game's 0x00796380 (thiscall, no arguments): the recorder's
// constructor. The name starts as "Animation", the rate as "100", the time
// as "5", the file type as 3; the ini file's path is the application's
// (slot 0x74), and the settings are read at once.
char* __fastcall RecorderConstruct(char* self, void*)
{
    uint8_t tag;
    This<void*>(addr::GZWin_Construct, self);
    At<std::uintptr_t>(self, 0x0) = addr::SC4AnimationRecorder_Vtable;
    At<void*>(self, recorder::kFrame) = nullptr;
    At<void*>(self, recorder::kStatus) = nullptr;
    At<void*>(self, recorder::kNameEdit) = nullptr;
    ConstructText(self, recorder::kName, "Animation", &tag);
    At<void*>(self, recorder::kRateEdit) = nullptr;
    ConstructText(self, recorder::kRate, "100", &tag);
    At<void*>(self, recorder::kTimeEdit) = nullptr;
    ConstructText(self, recorder::kTime, "5", &tag);
    At<void*>(self, recorder::kStart) = nullptr;
    At<void*>(self, recorder::kClose) = nullptr;
    At<void*>(self, recorder::kSnap) = nullptr;
    At<void*>(self, recorder::kDeleteCheck) = nullptr;
    At<uint8_t>(self, recorder::kDeleteBitmaps) = 1;
    At<void*>(self, recorder::kTypeList) = nullptr;
    At<uint32_t>(self, recorder::kBackground) = 0;
    At<int32_t>(self, recorder::kHoldX) = 0;
    At<int32_t>(self, recorder::kHoldY) = 0;
    At<void*>(self, recorder::kCapture) = nullptr;
    InitString(At<GameString>(self, recorder::kIniPath));
    At<void*>(self, recorder::kFiles) = nullptr;
    At<void*>(self, recorder::kFilesEnd) = nullptr;
    At<void*>(self, recorder::kFilesCapacity) = nullptr;
    At<uint32_t>(self, recorder::kMaxFrames) = 0x80;
    At<uint32_t>(self, recorder::kMaxPixels) = 0x8000000;
    InitString(At<GameString>(self, recorder::kOutputDir));
    InitString(At<GameString>(self, recorder::kFileName));
    At<uint32_t>(self, recorder::kType) = 3;
    At<uint8_t>(self, recorder::kHasGx) = 0;
    InitString(At<GameString>(self, recorder::kGxDir));
    At<uint8_t>(self, recorder::kStepped) = 1;
    At<uint32_t>(self, recorder::kRun) = 0;
    At<uint32_t>(self, recorder::kFrameCount) = 0;
    At<uint32_t>(self, recorder::kInterval) = 0;
    At<uint32_t>(self, recorder::kSeconds) = 0;
    At<uint32_t>(self, recorder::kWanted) = 0;
    At<int32_t>(self, recorder::kLeft) = 0;
    At<int32_t>(self, recorder::kTop) = 0;
    At<int32_t>(self, recorder::kRight) = 0;
    At<int32_t>(self, recorder::kBottom) = 0;
    At<void*>(self, recorder::kBuffers) = nullptr;
    At<void*>(self, recorder::kBuffersEnd) = nullptr;
    At<void*>(self, recorder::kBuffersCapacity) = nullptr;
    This<void*>(addr::SC4Stopwatch_Construct, self + recorder::kWatch, uint32_t{4});
    At<uint32_t>(self, recorder::kFramesLeft) = 0;
    At<uint32_t>(self, recorder::kCountdown) = 0;
    At<uint32_t>(self, recorder::kFileIndex) = 0;
    if (void* const app = Global<void*>(addr::SC4_Service43C94))
        Virtual<uint8_t>(app, 0x74, self + recorder::kIniPath); // GetAppIniFilePath
    LoadSettings(self, nullptr);
    return self;
}

// A text control made in code (0x114 bytes). If the memory cannot be had the
// original goes on with a null pointer; so does this.
char* NewText()
{
    void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x114});
    return memory ? This<char*>(addr::GZWinText_Construct, memory) : nullptr;
}

// One of the panel's four captions: a text in the panel's font, black, at
// the given place, added to the panel and let go.
void AddCaption(char* self, void* font, uint32_t left, uint32_t top, uint32_t right, uint32_t bottom,
                const char* caption)
{
    char* const text = NewText();
    AddRef(text);
    void* const part = text + kControlPart;
    Virtual<void>(Virtual<void*>(part, 0xC), 0xDC, left, top, right, bottom);
    {
        GameString name;
        void* const made = This<void*>(addr::SC4String_FromText, &name, caption);
        Virtual<void>(text, 0x128, made);
        DestroyString(name);
    }
    Virtual<void>(Virtual<void*>(part, 0xC), 0x110, uint32_t{0x200000}, uint32_t{1});
    Virtual<void>(part, 0x40, uint32_t{0});
    Virtual<void>(part, 0x48, font);
    const uint32_t black = Virtual<uint32_t>(self, 0x1B0, uint32_t{0}, uint32_t{0}, uint32_t{0});
    Virtual<void>(part, 0x24, uint32_t{0}, black);
    Virtual<void>(self, 0x38, Virtual<void*>(part, 0xC));
    Release(text);
}

// One of the panel's three edit fields (0x290 bytes, made with 0x11FF): at
// most 0x200 characters, showing one of the recorder's strings; the
// recorder keeps a reference.
void AddEdit(char* self, std::size_t field, uint32_t left, uint32_t top, uint32_t right, uint32_t bottom,
             std::size_t text)
{
    void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x290});
    char* const made = memory ? This<char*>(addr::GZWinTextEdit_Construct, memory, uint32_t{0x11FF}) : nullptr;
    void* const edit = made ? made + kControlPart : nullptr;
    At<void*>(self, field) = edit;
    AddRef(edit);
    Virtual<void>(Virtual<void*>(At<void*>(self, field), 0x14), 0xDC, left, top, right, bottom);
    Virtual<void>(At<void*>(self, field), 0x130, uint32_t{0x200});
    Virtual<void>(At<void*>(self, field), 0xC0, self + text, uint32_t{0});
    Virtual<void>(self, 0x38, Virtual<void*>(At<void*>(self, field), 0x14));
}

// A button of the toolkit with a caption.
void* MakeButton(void* toolkit, uint32_t id, const char* caption)
{
    GameString name;
    void* const made = This<void*>(addr::SC4String_FromText, &name, caption);
    void* const button = Virtual<void*>(toolkit, 0x24, id, made, uint32_t{0}, uint32_t{0}, uint32_t{0});
    DestroyString(name);
    return button;
}

// A row of the list of file types.
void AddType(char* self, uint32_t value, const char* caption)
{
    GameString name;
    void* const made = This<void*>(addr::SC4String_FromText, &name, caption);
    Virtual<void>(At<void*>(self, recorder::kTypeList), 0x3C, value, made, uint32_t{0}, uint32_t{0}, uint32_t{0});
    DestroyString(name);
}

// The game's 0x00796630 (slot 4 of the recorder; thiscall, no arguments):
// Init. Builds the panel (id 0x6ACB1AF1, at 4, 4 to 0x120, 0x67): four
// captions, the status text, the three edit fields, the buttons Start
// (0x080293A3), Close (0x080293A4) and Snap (0x080293A5, greyed and off),
// the list of file types with jpg chosen, and, if there is none yet, the
// frame (id 0x6ACB1C40, at 100, 100 to 0x106, 0xDE) as a child of the
// window at 0x00B43DC8. The window places service (0x00B43CE0) moves the
// frame to where it was left and is told of both windows.
uint8_t __fastcall RecorderInit(char* self, void*)
{
    if (This<uint8_t>(addr::GZWin_IsInitialized, self))
        return 1;
    void* const toolkit = Cdecl<void*>(addr::SC4_Singleton913C72);
    void* const fonts = Cdecl<void*>(addr::SC4_Singleton913D22);
    void* font;
    {
        GameString face;
        void* const made = This<void*>(addr::SC4String_FromText, &face, "Arial MT");
        font = Virtual<void*>(fonts, 0x2C, made, uint32_t{0}, uint32_t{0xA}, uint32_t{0}, uint32_t{1});
        DestroyString(face);
    }
    Virtual<void>(self, 0x100, kRecorderId);
    Virtual<void>(self, 0x110, uint32_t{0x10000}, uint32_t{1});
    Virtual<void>(self, 0xDC, uint32_t{4}, uint32_t{4}, uint32_t{0x120}, uint32_t{0x67});
    const uint32_t fill = Virtual<uint32_t>(self, 0x1B0, uint32_t{0x8C}, uint32_t{0x9A}, uint32_t{0xAE});
    Virtual<void>(self, 0x1A8, fill);
    At<uint32_t>(self, recorder::kBackground) = Virtual<uint32_t>(self, 0x1B0, uint32_t{0}, uint32_t{0}, uint32_t{0});
    This<uint8_t>(addr::GZWin_Init, self);
    FindGx(self, nullptr);

    AddCaption(self, font, 3, 7, 0x36, 0x1B, "Name:");
    AddCaption(self, font, 3, 0x1B, 0x35, 0x2D, "Rate (ms):");
    AddCaption(self, font, 0x69, 0x1B, 0x98, 0x2D, "Time (s):");
    AddCaption(self, font, 3, 0x30, 0x34, 0x40, "Output:");

    // The status text, kept.
    {
        char* const made = NewText();
        void* const status = made ? made + kControlPart : nullptr;
        At<void*>(self, recorder::kStatus) = status;
        AddRef(status);
        Virtual<void>(Virtual<void*>(At<void*>(self, recorder::kStatus), 0xC), 0xDC, uint32_t{3}, uint32_t{0x43},
                      uint32_t{0x64}, uint32_t{0x62});
        Virtual<void>(Virtual<void*>(At<void*>(self, recorder::kStatus), 0xC), 0x110, uint32_t{0x200000},
                      uint32_t{1});
        Virtual<void>(At<void*>(self, recorder::kStatus), 0x40, uint32_t{0});
        Virtual<void>(At<void*>(self, recorder::kStatus), 0x48, font);
        const uint32_t black = Virtual<uint32_t>(self, 0x1B0, uint32_t{0}, uint32_t{0}, uint32_t{0});
        Virtual<void>(At<void*>(self, recorder::kStatus), 0x24, uint32_t{0}, black);
        Virtual<void>(self, 0x38, Virtual<void*>(At<void*>(self, recorder::kStatus), 0xC));
    }
    UpdateStatus(self, nullptr);

    AddEdit(self, recorder::kNameEdit, 0x39, 6, 0xB7, 0x16, recorder::kName);
    AddEdit(self, recorder::kRateEdit, 0x39, 0x1B, 0x67, 0x2C, recorder::kRate);
    AddEdit(self, recorder::kTimeEdit, 0x92, 0x1B, 0xB7, 0x2C, recorder::kTime);

    At<void*>(self, recorder::kStart) = MakeButton(toolkit, kStartId, "Start");
    Virtual<void>(At<void*>(self, recorder::kStart), 0x14, uint32_t{0x20}, uint32_t{0});
    Virtual<void>(Virtual<void*>(At<void*>(self, recorder::kStart), 0xC), 0xDC, uint32_t{0x60}, uint32_t{0x46},
                  uint32_t{0x98}, uint32_t{0x5C});
    Virtual<void>(self, 0x38, Virtual<void*>(At<void*>(self, recorder::kStart), 0xC));

    At<void*>(self, recorder::kClose) = MakeButton(toolkit, kCloseId, "Close");
    Virtual<void>(At<void*>(self, recorder::kClose), 0x14, uint32_t{0x20}, uint32_t{0});
    Virtual<void>(Virtual<void*>(At<void*>(self, recorder::kClose), 0xC), 0xDC, uint32_t{0x9F}, uint32_t{0x46},
                  uint32_t{0xD7}, uint32_t{0x5C});
    Virtual<void>(self, 0x38, Virtual<void*>(At<void*>(self, recorder::kClose), 0xC));

    At<void*>(self, recorder::kSnap) = MakeButton(toolkit, kSnapId, "Snap");
    Virtual<void>(Virtual<void*>(At<void*>(self, recorder::kSnap), 0xC), 0x110, uint32_t{2}, uint32_t{0});
    {
        const uint32_t grey = Virtual<uint32_t>(self, 0x1B0, uint32_t{0x80}, uint32_t{0x80}, uint32_t{0x80});
        Virtual<void>(At<void*>(self, recorder::kSnap), 0x5C, uint32_t{4}, grey);
    }
    Virtual<void>(At<void*>(self, recorder::kSnap), 0x14, uint32_t{0x20}, uint32_t{0});
    Virtual<void>(Virtual<void*>(At<void*>(self, recorder::kSnap), 0xC), 0xDC, uint32_t{0xDE}, uint32_t{0x46},
                  uint32_t{0x116}, uint32_t{0x5C});
    Virtual<void>(self, 0x38, Virtual<void*>(At<void*>(self, recorder::kSnap), 0xC));

    // The list of file types.
    {
        GameString empty;
        InitString(empty);
        At<void*>(self, recorder::kTypeList) = Virtual<void*>(toolkit, 0x38, uint32_t{0}, &empty, uint32_t{0});
        DestroyString(empty);
    }
    Virtual<void>(At<void*>(self, recorder::kTypeList), 0x14, uint32_t{1}, uint32_t{0});
    Virtual<void>(At<void*>(self, recorder::kTypeList), 0x14, uint32_t{2}, uint32_t{1});
    Virtual<void>(At<void*>(self, recorder::kTypeList), 0x14, uint32_t{4}, uint32_t{0});
    Virtual<void>(At<void*>(self, recorder::kTypeList), 0x14, uint32_t{8}, uint32_t{0});
    Virtual<void>(Virtual<void*>(At<void*>(self, recorder::kTypeList), 0xC), 0xDC, uint32_t{0x35}, uint32_t{0x2C},
                  uint32_t{0x116}, uint32_t{0x44});
    AddType(self, 1, "bmp");
    AddType(self, 2, "png");
    AddType(self, 3, "jpg");
    Virtual<void>(At<void*>(self, recorder::kTypeList), 0x38, uint32_t{3});
    Virtual<void>(self, 0x38, Virtual<void*>(At<void*>(self, recorder::kTypeList), 0xC));

    // The frame.
    if (!At<void*>(self, recorder::kFrame)) {
        void* const view = Global<void*>(addr::SC4_Service43DC8);
        void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0xFC});
        void* const made = memory ? This<void*>(addr::SC4UI_F794430, memory) : nullptr;
        At<void*>(self, recorder::kFrame) = made;
        AddRef(made);
        Virtual<void>(At<void*>(self, recorder::kFrame), 0x100, kFrameId);
        Virtual<void>(At<void*>(self, recorder::kFrame), 0x110, uint32_t{0x10000}, uint32_t{1});
        Virtual<void>(At<void*>(self, recorder::kFrame), 0xDC, uint32_t{0x64}, uint32_t{0x64}, uint32_t{0x106},
                      uint32_t{0xDE});
        const uint8_t* const colours = &Global<uint8_t>(addr::SC4AnimationRecorder_FrameColours);
        This<void>(addr::SC4UI_F7942F0, At<void*>(self, recorder::kFrame), uint32_t{0x8C}, uint32_t{0x9A},
                   uint32_t{0xAE}, uint32_t{colours[0]}, uint32_t{colours[1]}, uint32_t{colours[2]},
                   uint32_t{colours[3]}, uint32_t{colours[4]}, uint32_t{colours[5]});
        This<void>(addr::SC4UI_F794260, At<void*>(self, recorder::kFrame), uint32_t{1}, uint32_t{1});
        Virtual<void>(view, 0x38, At<void*>(self, recorder::kFrame));
    }
    if (font)
        Release(font);

    if (!At<void*>(self, recorder::kCapture)) {
        if (void* const service = Global<void*>(addr::SC4_Service43CA4))
            Virtual<uint8_t>(service, 0x0, kCaptureIid, &At<void*>(self, recorder::kCapture));
    }
    if (void* const places = Global<void*>(addr::SC4_Service43CE0)) {
        uint32_t x; // both set by the service when it knows the window
        uint32_t y;
        const uint32_t id = Virtual<uint32_t>(At<void*>(self, recorder::kFrame), 0xFC);
        if (Virtual<uint8_t>(places, 0x1C, id + 1, &x, &y, uint32_t{1}))
            Virtual<void>(At<void*>(self, recorder::kFrame), 0xD4, x, y);
        Virtual<void>(places, 0x28, At<void*>(self, recorder::kFrame), uint32_t{1});
        Virtual<void>(places, 0x28, static_cast<void*>(self), uint32_t{1});
    }
    Virtual<void>(At<void*>(self, recorder::kFrame), 0x5C);
    Virtual<void>(At<void*>(self, recorder::kFrame), 0x110, uint32_t{0x800}, uint32_t{0});
    Virtual<void>(self, 0x5C);
    Virtual<void>(self, 0x110, uint32_t{0x800}, uint32_t{0});
    return 1;
}

bool InstallPanels4(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[20] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F7946A0, reinterpret_cast<void*>(&RecorderMouseMove)},
        {addr::SC4UI_F794700, reinterpret_cast<void*>(&GetRecordedRect)},
        {addr::SC4UI_F794760, reinterpret_cast<void*>(&FrameMouseDown)},
        {addr::SC4UI_F7947D0, reinterpret_cast<void*>(&FrameMouseMove)},
        {addr::SC4UI_F794A20, reinterpret_cast<void*>(&ReleaseFrames)},
        {addr::SC4UI_F794AA0, reinterpret_cast<void*>(&FrameInit)},
        {addr::SC4UI_F794DA0, reinterpret_cast<void*>(&RecorderDestruct)},
        {addr::SC4UI_F794ED0, reinterpret_cast<void*>(&AllocateFrames)},
        {addr::SC4UI_F795020, reinterpret_cast<void*>(&UpdateStatus)},
        {addr::SC4UI_F7950E0, reinterpret_cast<void*>(&RecorderDelete)},
        {addr::SC4UI_F795100, reinterpret_cast<void*>(&SetOutputDir)},
        {addr::SC4UI_F795180, reinterpret_cast<void*>(&FindGx)},
        {addr::SC4UI_F7954F0, reinterpret_cast<void*>(&LoadSettings)},
        {addr::SC4UI_F795A00, reinterpret_cast<void*>(&SaveSettings)},
        {addr::SC4UI_F795E50, reinterpret_cast<void*>(&WriteFrames)},
        {addr::SC4UI_F796040, reinterpret_cast<void*>(&ReportWritten)},
        {addr::SC4UI_F796210, reinterpret_cast<void*>(&CleanName)},
        {addr::SC4UI_F796380, reinterpret_cast<void*>(&RecorderConstruct)},
        {addr::SC4UI_F796630, reinterpret_cast<void*>(&RecorderInit)},
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

const bool g_panels4Registered = registry::Register({
    "ui.panels.part4",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F796630,
    "Our animation recorder: its construction, settings, frame buffers and files, and its frame's mouse handling "
    "(docs/ui.md)",
    false,
    InstallPanels4,
});

} // namespace
} // namespace opensc4::ui
