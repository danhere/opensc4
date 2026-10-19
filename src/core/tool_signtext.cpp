// The sign tool of the city view (0x004D7AF0 to 0x004D8ABA).
// Replacement, off by default:
//
//   ui.tools.signtext: the view input control of 0x80 bytes (vtable
//     0x00A92968, notice target 0x00A92958 at +0x28, ID 0x8B4CE5A0) the view
//     makes on its command 0x0B980581, and the occupant filter of its picks
//     (0xC bytes, vtables 0x00A9293C and 0x00A92924).
//
// The tool puts signs on the terrain and edits their text. A click on empty
// ground makes a sign (a prop of 0x590 bytes, kind 0xAB72FBB3, interface
// 0x4B44FBE2) at the terrain point, adds it to the city through the prop
// manager (AddCityProp) and starts editing its text. A click on a sign picks
// it up: moving the pointer then drags it, letting go without moving (or
// after dragging a sign that has no text) starts editing. While editing,
// characters come in through OnCharacter (slot 13) one byte at a time in the
// game's text encoding (0x00940DA8 answers 8, UTF-8): the bytes of one
// character are gathered at +0x68 and appended when the character is whole;
// Backspace takes one character off, Enter ends the edit, Ctrl+V appends
// the clipboard's text, Escape puts back the text the edit started with and
// goes on editing. A click ends the edit too. The text is at most 33
// characters (the count is checked before each character, against 32); line
// feeds are left out. Every change is handed to the sign (its slot 4), which
// keeps the text in its string at +0x98. A sign whose text is empty when the
// tool lets go of it is removed from the city (RemoveProp). Delete removes
// the sign under the pointer when no edit runs.
//
// The caret and its blinking are the sign's own (its slot 7, 0x005F7320,
// asks the service 0x86AD10EC for notice 0x80): nothing here reads a clock
// or draws a random number.
//
// Fields: +0x2C the state (0 idle, 1 a sign pressed, 2 dragging it, 3
// editing), +0x30 the sign (referenced; while idle the one under the
// pointer, marked through its slot 6), +0x34 and +0x38 the sign's place on
// the screen less the pointer when it was pressed, +0x3C the text being
// edited, +0x50 the text the edit started with, +0x64 the text encoding,
// +0x68 the bytes of a character not yet whole, +0x7C the clipboard service
// (0x27EBFFFD, interface 0x07EC0010; referenced). +0x34, +0x38 and +0x64
// are not set by the constructor.
//
// The part at +0x28 takes notice 0x0BE10A20 of the service 0x64063387
// (interface 0xC1085722): while a text is edited it answers with the screen
// point over the sign's middle (where the input method's window goes).
//
// Left the game's: the target's QueryInterface thunk 0x004D7FB0, the jumps
// to AddRef and Release, and the shared helpers lying in the range
// (0x004D7B30, 0x004D7B60, 0x004D7BB0 reference holders; 0x004D7E50,
// 0x004D7ED0, 0x004D8650 string code), all called from elsewhere too.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::tool_signtext {
namespace {

// The base class's fields.
namespace base {
constexpr std::size_t kInited = 0x04; // byte
constexpr std::size_t kId = 0x0C;
constexpr std::size_t kCursor = 0x10; // cursor ID
} // namespace base

// The tool's fields.
namespace field {
constexpr std::size_t kTarget = 0x28;    // the notice target part (vtable 0x00A92958)
constexpr std::size_t kState = 0x2C;     // 0 idle, 1 pressed, 2 dragging, 3 editing
constexpr std::size_t kSign = 0x30;      // the sign (interface 0x4B44FBE2, referenced)
constexpr std::size_t kGrabX = 0x34;     // the sign on the screen less the pointer
constexpr std::size_t kGrabZ = 0x38;
constexpr std::size_t kText = 0x3C;      // the text being edited (a string of 0x14 bytes)
constexpr std::size_t kSaved = 0x50;     // the text the edit started with
constexpr std::size_t kEncoding = 0x64;  // the text encoding (8)
constexpr std::size_t kPending = 0x68;   // the bytes of a character not yet whole
constexpr std::size_t kClipboard = 0x7C; // the clipboard service (referenced)
} // namespace field

constexpr int32_t kIdle = 0, kPressed = 1, kDragging = 2, kEditing = 3;

constexpr uint32_t kToolId = 0x8B4CE5A0;
constexpr uint32_t kCursorId = 0x6B72FECF;
constexpr uint32_t kIidTarget = 0xE98B2F57;
constexpr uint32_t kTypeProp = 0xA823821E;
constexpr uint32_t kIidTerrainSign = 0x4B44FBE2;
constexpr uint32_t kClipboardService = 0x27EBFFFD;
constexpr uint32_t kIidClipboard = 0x07EC0010;
constexpr uint32_t kNoticeService = 0x64063387;
constexpr uint32_t kIidNotices = 0xC1085722;
constexpr uint32_t kNoticePlace = 0x0BE10A20;
constexpr uint32_t kSoundPlay = 0xEA451903;
constexpr uint32_t kSoundDown = 0x2C16DA29; // a sign made or put down
constexpr uint32_t kSoundUp = 0x2C16DA45;   // a sign picked up
constexpr uint32_t kMaxCharacters = 0x20;

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

// The game's _ftol2 (0x009EEF04): truncation toward zero to 64 bits, the low
// 32 kept; what does not fit in 64 bits (a NaN among them) is the x87
// "integer indefinite", whose low word is 0.
int32_t Truncate(double value)
{
    if (!(value > -9223372036854775808.0 && value < 9223372036854775808.0))
        return 0;
    return static_cast<int32_t>(static_cast<uint32_t>(static_cast<uint64_t>(static_cast<int64_t>(value))));
}

// Puts an object into a counted field: the new one referenced first, the old
// one released after. Nothing when they are the same.
void Hold(char* self, std::size_t field, void* object)
{
    void* const old = At<void*>(self, field);
    if (object == old)
        return;
    if (object)
        AddRef(object);
    At<void*>(self, field) = object;
    if (old)
        Release(old);
}

GameString& StringAt(char* self, std::size_t field)
{
    return At<GameString>(self, field);
}

// The game's inlined string constructor: an empty string with a buffer of 8
// bytes.
void InitString(GameString& text)
{
    text.body.begin = nullptr;
    text.body.end = nullptr;
    text.body.capacity = nullptr;
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, std::size_t{8});
    text.body.begin = buffer;
    text.body.end = buffer;
    text.body.capacity = buffer + 8;
    *buffer = 0;
    text.vtable = addr::SC4String_Vtable;
    text.zero = 0;
}

// The game's inlined string destructor.
void DestroyString(GameString& text)
{
    text.vtable = addr::SC4String_Vtable;
    if (text.body.begin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(text.body.begin));
}

// The game's inlined clear of a string body.
void Empty(StringBody& body)
{
    if (body.begin != body.end) {
        *body.begin = 0;
        body.end = body.begin;
    }
}

// The sound service, when there is one, plays a sound of the tool.
void Play(uint32_t sound)
{
    if (void* const service = Global<void*>(addr::SC4_Sound43CB8))
        Virtual<void>(service, 0x2C, kSoundPlay, sound, uint32_t{0}, uint32_t{0}, uint32_t{0});
}

// A new occupant filter of the tool, referenced; null when the allocation
// fails.
void* NewFilter()
{
    char* const filter = Cdecl<char*>(addr::SC4_operator_new, std::size_t{0xC});
    if (!filter)
        return nullptr;
    This<void*>(addr::SC4_OccupantFilterBase, filter);
    At<std::uintptr_t>(filter, 0x0) = addr::SC4SignPlaceTool_FilterVtable;
    At<std::uintptr_t>(filter, 0x4) = addr::SC4SignPlaceTool_FilterVtable4;
    AddRef(filter);
    return filter;
}

// The service of notices (0x64063387), referenced, or null.
void* NoticeService()
{
    void* service = nullptr;
    if (void* const framework = Cdecl<void*>(addr::RZGetFramework))
        Virtual<uint8_t>(framework, 0x14, kNoticeService, kIidNotices, &service);
    return service;
}

// The game's 0x004D7AF0 (slot 0, ret 8): the target's ID gives the part at
// +0x28 (null for a null object, which the reference then faults on, as in
// the game); the rest is the base's.
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t id, void** out)
{
    if (id != kIidTarget)
        return This<uint8_t>(addr::SC4ViewInputControl_QueryInterface, self, id, out);
    *out = self ? self + field::kTarget : nullptr;
    AddRef(self);
    return 1;
}

// The game's 0x004D7BE0 (slot 3): Init. The ID and the cursor, the text
// encoding, the clipboard service kept at +0x7C, the notice 0x0BE10A20 asked
// of the service 0x64063387 for the part at +0x28, the city's signs and
// labels shown (the prop developer's slot 8 with true), then the base Init.
uint8_t __fastcall Init(char* self, void*)
{
    if (At<uint8_t>(self, base::kInited))
        return 1;
    At<uint32_t>(self, base::kId) = kToolId;
    At<uint32_t>(self, base::kCursor) = kCursorId;
    At<uint32_t>(self, field::kEncoding) = Cdecl<uint32_t>(addr::RZText_DefaultEncoding);
    void* clipboard = nullptr;
    if (void* const framework = Cdecl<void*>(addr::RZGetFramework))
        Virtual<uint8_t>(framework, 0x14, kClipboardService, kIidClipboard, &clipboard);
    Hold(self, field::kClipboard, clipboard);
    void* notices = NoticeService();
    if (notices)
        Virtual<uint8_t>(notices, 0x14, static_cast<void*>(self + field::kTarget), kNoticePlace);
    Virtual<void>(Global<void*>(addr::SC4_Service43DB8), 0x20, uint32_t{1}); // ShowSignsAndLabels
    This<uint8_t>(addr::SC4ViewInputControl_Init, self);
    This<void>(addr::SC4RefPtr_ReleaseGuarded, &notices);
    This<void>(addr::SC4RefPtr_ReleaseGuarded, &clipboard);
    return 1;
}

// The game's 0x004D7CC0 (slot 3 of the filter, ret 4): whether the picks
// take an occupant. A prop occupant (type 0xA823821E) that answers the
// terrain sign interface. `this` is not read.
uint8_t __fastcall FilterIncludes(char*, void*, void* occupant)
{
    if (Virtual<uint32_t>(occupant, 0x1C) != kTypeProp)
        return 0;
    void* sign = nullptr;
    const uint8_t found = Virtual<uint8_t>(occupant, 0x0, kIidTerrainSign, &sign);
    if (sign)
        Release(sign);
    return found ? 1 : 0;
}

// The game's 0x004D7D20 (ret 8): the height a sign stands at, as a float in
// st(0): the higher of the terrain's height at the place (slot 13) and its
// slot 29 (the water's level); the terrain's height when the two do not
// compare. `this` is not read.
float __fastcall GroundHeight(char*, void*, float x, float z)
{
    const float ground = static_cast<float>(Virtual<double>(Global<void*>(addr::SC4_Terrain), 0x34, x, z));
    const float water = static_cast<float>(Virtual<double>(Global<void*>(addr::SC4_Terrain), 0x74));
    return ground < water ? water : ground;
}

// The game's 0x004D7D70 (slot 3 of the part at +0x28, ret 4). `this` is the
// part. A notice is four dwords: its number, whether it was answered, and
// two screen coordinates. For 0x0BE10A20 while a text is edited: answered,
// with the point over the middle of the sign's box (the occupant's slot 10
// fills two corners, which start as 1, 1, 1 and 0, 0, 0; x and z are the
// corners' mean, y the second corner's) as the camera view puts it on the
// screen, truncated. False always.
uint8_t __fastcall DoNotice(char* target, void*, uint32_t* notice)
{
    char* const self = target - field::kTarget;
    if (notice[0] != kNoticePlace || At<int32_t>(self, field::kState) != kEditing || !At<void*>(self, field::kSign))
        return 0;
    notice[1] = 1;
    float low[3] = {1.0f, 1.0f, 1.0f};
    float high[3] = {0.0f, 0.0f, 0.0f};
    void* const occupant = Virtual<void*>(At<void*>(self, field::kSign), 0xC);
    Virtual<void>(occupant, 0x28, &low[0], &high[0]);
    const double half = Global<float>(addr::SC4_FloatHalf);
    float point[3];
    point[0] = static_cast<float>((static_cast<double>(high[0]) + static_cast<double>(low[0])) * half);
    point[1] = high[1];
    point[2] = static_cast<float>((static_cast<double>(high[2]) + static_cast<double>(low[2])) * half);
    This<void>(addr::SC4CameraView_WorldToView, Global<void*>(addr::SC4_CameraView), &point[0]);
    notice[2] = static_cast<uint32_t>(Truncate(point[0]));
    notice[3] = static_cast<uint32_t>(Truncate(point[1]));
    return 0;
}

// The game's 0x004D7F00: the constructor; the caller allocated 0x80 bytes.
char* __fastcall Construct(char* self, void*)
{
    This<void*>(addr::SC4ViewInputControl_Construct, self);
    At<std::uintptr_t>(self, field::kTarget) = addr::SC4Audio_OldTargetBaseVtable;
    At<std::uintptr_t>(self, 0) = addr::SC4SignPlaceTool_Vtable;
    At<std::uintptr_t>(self, field::kTarget) = addr::SC4SignPlaceTool_TargetVtable;
    At<int32_t>(self, field::kState) = kIdle;
    At<void*>(self, field::kSign) = nullptr;
    InitString(StringAt(self, field::kText));
    InitString(StringAt(self, field::kSaved));
    InitString(StringAt(self, field::kPending));
    At<void*>(self, field::kClipboard) = nullptr;
    return self;
}

// The game's 0x004D7FC0 (ret 4): the tool's sign changes. The one it had is
// let go: with an empty text it is removed from the city (the prop manager's
// RemoveProp, slot 29, with its occupant), else it is unmarked (its slot 6
// with false). The new one is kept (referenced; the old one released) and
// marked. Nothing when it is the same sign.
void __fastcall SetSign(char* self, void*, void* sign)
{
    if (sign == At<void*>(self, field::kSign))
        return;
    if (At<void*>(self, field::kSign)) {
        GameString text;
        InitString(text);
        Virtual<void>(At<void*>(self, field::kSign), 0x14, &text);
        if (text.body.begin == text.body.end) {
            void* const props = Global<void*>(addr::cSC4PropManager_Global);
            Virtual<uint8_t>(props, 0x74, Virtual<void*>(At<void*>(self, field::kSign), 0xC));
        } else {
            Virtual<void>(At<void*>(self, field::kSign), 0x18, uint32_t{0});
        }
        DestroyString(text);
    }
    Hold(self, field::kSign, sign);
    if (sign)
        Virtual<void>(sign, 0x18, uint32_t{1});
}

// The game's 0x004D80C0: the destructor. The clipboard service, the three
// strings, the sign, then the base.
void __fastcall Destruct(char* self, void*)
{
    if (void* const clipboard = At<void*>(self, field::kClipboard))
        Release(clipboard);
    DestroyString(StringAt(self, field::kPending));
    DestroyString(StringAt(self, field::kSaved));
    DestroyString(StringAt(self, field::kText));
    if (void* const sign = At<void*>(self, field::kSign))
        Release(sign);
    This<void>(addr::SC4ViewInputControl_Destruct, self);
}

// The game's 0x004D80A0 (slot 26, ret 4): the deleting destructor.
char* __fastcall DeletingDestruct(char* self, void*, uint32_t flags)
{
    Destruct(self, nullptr);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// The edit ends: the sign's caret goes (its slot 7 with false) and it is
// marked again. The sign is not checked for null, as in the game.
void EndEditing(char* self)
{
    Virtual<void>(At<void*>(self, field::kSign), 0x1C, uint32_t{0});
    Virtual<void>(At<void*>(self, field::kSign), 0x18, uint32_t{1});
}

// The game's 0x004D8130 (slot 4): Shutdown. An edit ends; the sign is let
// go (and removed when its text is empty); the clipboard service released
// (the field cleared first); the notice given back; then the base.
uint8_t __fastcall Shutdown(char* self, void*)
{
    if (!At<uint8_t>(self, base::kInited))
        return 1;
    if (At<int32_t>(self, field::kState) == kEditing)
        EndEditing(self);
    SetSign(self, nullptr, nullptr);
    if (void* const clipboard = At<void*>(self, field::kClipboard)) {
        At<void*>(self, field::kClipboard) = nullptr;
        Release(clipboard);
    }
    void* notices = NoticeService();
    if (notices)
        Virtual<uint8_t>(notices, 0x18, static_cast<void*>(self + field::kTarget), kNoticePlace);
    This<uint8_t>(addr::SC4ViewInputControl_Shutdown, self);
    This<void>(addr::SC4RefPtr_ReleaseGuarded, &notices);
    return 1;
}

// A click on a sign the renderer's pick found: unless it is the one being
// edited, it becomes the tool's sign in state 1, with its place on the
// screen less the pointer at +0x34 and +0x38 (truncated before the
// subtraction), and the pick-up sound.
void PressSign(char* self, void* model, int32_t x, int32_t z)
{
    void* sign = nullptr;
    void* const owner = Virtual<void*>(model, 0xC);
    if (Virtual<uint8_t>(owner, 0x0, kIidTerrainSign, This<void**>(addr::SC4RefPtr_Reset, &sign))) {
        if (!(At<void*>(self, field::kSign) == sign && At<int32_t>(self, field::kState) == kEditing)) {
            At<int32_t>(self, field::kState) = kPressed;
            SetSign(self, nullptr, sign);
            float point[3];
            point[0] = point[1] = point[2] = Global<float>(addr::SC4SignPlaceTool_Zero);
            Virtual<void>(Virtual<void*>(sign, 0xC), 0x20, &point[0]); // GetPosition
            This<void>(addr::SC4CameraView_WorldToView, Global<void*>(addr::SC4_CameraView), &point[0]);
            const uint32_t screenX = static_cast<uint32_t>(Truncate(point[0]));
            const uint32_t screenZ = static_cast<uint32_t>(Truncate(point[1]));
            At<uint32_t>(self, field::kGrabX) = screenX - static_cast<uint32_t>(x);
            At<uint32_t>(self, field::kGrabZ) = screenZ - static_cast<uint32_t>(z);
            Play(kSoundUp);
        }
    }
    if (sign)
        Release(sign);
}

// A click on empty ground at a terrain point: a new sign (0x590 bytes from
// its pool, constructed as the most derived object) is added to the city
// (the prop manager's AddCityProp, slot 19), becomes the tool's sign, is put
// at the point with GroundHeight as its y, and its text is edited from
// empty. A failed allocation is not checked, as in the game.
void MakeSign(char* self, float* point)
{
    void* const memory = Cdecl<void*>(addr::SC4TerrainSignProp_Allocate, std::size_t{0x590});
    void* const sign = memory ? This<void*>(addr::SC4TerrainSignProp_Construct, memory, uint32_t{1}) : nullptr;
    if (sign)
        AddRef(sign);
    void* const props = Global<void*>(addr::cSC4PropManager_Global);
    Virtual<uint8_t>(props, 0x4C, Virtual<void*>(sign, 0xC));
    SetSign(self, nullptr, sign);
    point[1] = GroundHeight(self, nullptr, point[0], point[2]);
    Virtual<void>(Virtual<void*>(sign, 0xC), 0x24, &point[0]); // SetPosition
    At<int32_t>(self, field::kState) = kEditing;
    Empty(StringAt(self, field::kText).body);
    Empty(StringAt(self, field::kSaved).body);
    Virtual<void>(sign, 0x10, static_cast<void*>(self + field::kText));
    Virtual<void>(sign, 0x1C, uint32_t{1});
    Play(kSoundDown);
    Release(sign);
}

// The game's 0x004D81D0 (slot 16, OnMouseDownL, ret 0xC). While a text is
// edited the click ends the edit (state 0) and nothing more. Otherwise,
// with a renderer: a sign under the pointer (the renderer's pick with a new
// filter, slot 0x104; the model's owner, slot 3, asked for the sign
// interface) is pressed; with none, the terrain point under the pointer
// (PickTerrain, slot 0xF0, with 0 and 1) gets a new sign. False only when
// there is neither a sign nor a terrain point; true without a renderer. The
// model a successful pick gave is not released, as in the game.
uint8_t __fastcall OnMouseDownL(char* self, void*, int32_t x, int32_t z, uint32_t)
{
    if (At<int32_t>(self, field::kState) == kEditing) {
        At<int32_t>(self, field::kState) = kIdle;
        EndEditing(self);
        return 1;
    }
    void* const renderer = Global<void*>(addr::SC4_View43DD0);
    if (!renderer)
        return 1;
    void* const filter = NewFilter();
    void* model = nullptr;
    if (Virtual<uint8_t>(renderer, 0x104, x, z, filter, &model)) {
        if (model && Virtual<void*>(model, 0xC))
            PressSign(self, model, x, z);
    } else {
        float point[3];
        point[0] = point[1] = point[2] = Global<float>(addr::SC4SignPlaceTool_Zero);
        if (!Virtual<uint8_t>(renderer, 0xF0, x, z, &point[0], uint32_t{0}, uint32_t{1})) {
            if (filter)
                Release(filter);
            return 0;
        }
        MakeSign(self, point);
    }
    if (filter)
        Release(filter);
    return 1;
}

// The game's 0x004D84B0 (slot 20, OnMouseMove, ret 0xC). A pressed sign
// starts to be dragged; a dragged one goes to the terrain point under the
// pointer plus the grab offset (its y is the picked point's, not
// GroundHeight's). While idle, the sign under the pointer (the renderer's
// pick with a new filter) becomes the tool's sign, or none; the renderer is
// not checked for null there. True always.
uint8_t __fastcall OnMouseMove(char* self, void*, int32_t x, int32_t z, uint32_t)
{
    if (At<int32_t>(self, field::kState) == kPressed && At<void*>(self, field::kSign))
        At<int32_t>(self, field::kState) = kDragging;
    if (At<int32_t>(self, field::kState) == kDragging) {
        if (void* const renderer = Global<void*>(addr::SC4_View43DD0)) {
            float point[3];
            point[0] = point[1] = point[2] = Global<float>(addr::SC4SignPlaceTool_Zero);
            const int32_t pointX = At<int32_t>(self, field::kGrabX) + x;
            const int32_t pointZ = At<int32_t>(self, field::kGrabZ) + z;
            if (Virtual<uint8_t>(renderer, 0xF0, pointX, pointZ, &point[0], uint32_t{0}, uint32_t{1})) {
                if (void* const sign = At<void*>(self, field::kSign))
                    Virtual<void>(Virtual<void*>(sign, 0xC), 0x24, &point[0]);
            }
        }
    }
    if (At<int32_t>(self, field::kState) != kIdle)
        return 1;

    void* const renderer = Global<void*>(addr::SC4_View43DD0);
    void* const filter = NewFilter();
    void* model = nullptr;
    if (!Virtual<uint8_t>(renderer, 0x104, x, z, filter, &model)) {
        SetSign(self, nullptr, nullptr);
    } else if (model && Virtual<void*>(model, 0xC)) {
        void* sign = nullptr;
        void* const owner = Virtual<void*>(model, 0xC);
        if (Virtual<uint8_t>(owner, 0x0, kIidTerrainSign, &sign))
            SetSign(self, nullptr, sign);
        if (sign)
            Release(sign);
    }
    if (model)
        Release(model);
    if (filter)
        Release(filter);
    return 1;
}

// The game's 0x004D8670 (ret 4): appends a string's characters to the text
// and hands the text to the sign (its slot 4; the sign is not checked for
// null). The text's characters are counted first (0x009107D8); before each
// character the count must not be over 32, so a 33rd still goes in. A
// character's bytes are sized by 0x009106A9 from what is left of the string;
// a line feed is skipped but counted.
void __fastcall AppendText(char* self, void*, void* added)
{
    GameString* const text = &StringAt(self, field::kText);
    const uint32_t encoding = At<uint32_t>(self, field::kEncoding);
    const uint32_t length = Virtual<uint32_t>(text, 0x1C);
    uint32_t count = Cdecl<uint32_t>(addr::RZText_CountCharacters, Virtual<const char*>(text, 0x18), length, encoding);
    uint32_t done = 0;
    const uint32_t size = Virtual<uint32_t>(added, 0x1C);
    char* const characters = Virtual<char*>(added, 0x18);
    char dummy = 0;
    if (size != 0) {
        do {
            if (count > kMaxCharacters)
                break;
            char* const character = characters + done;
            const uint32_t bytes = Cdecl<uint32_t>(addr::RZText_CharacterSize, character, size - done,
                                                   At<uint32_t>(self, field::kEncoding));
            if (*character != '\n')
                This<void>(addr::cSC4Advisor_F40EBC0, &text->body, character, character + bytes, &dummy);
            done += bytes;
            ++count;
        } while (done < size);
    }
    Virtual<void>(At<void*>(self, field::kSign), 0x10, static_cast<void*>(text));
}

// The game's 0x004D8730 (slot 18, OnMouseUpL, ret 0xC). A sign pressed and
// not moved gets its text edited; so does a dragged one whose text is empty
// (the put-down sound plays for a dragged one either way). Editing starts
// from the sign's text, which is also kept at +0x50 for Escape. Whatever is
// not editing afterwards is idle. True always.
uint8_t __fastcall OnMouseUpL(char* self, void*, int32_t, int32_t, uint32_t)
{
    uint8_t edit = At<int32_t>(self, field::kState) == kPressed ? 1 : 0;
    if (At<int32_t>(self, field::kState) == kDragging) {
        GameString text;
        InitString(text);
        Virtual<void>(At<void*>(self, field::kSign), 0x14, &text);
        if (This<uint32_t>(addr::cRZBaseString_Strlen, &text) == 0)
            edit = 1;
        Play(kSoundDown);
        DestroyString(text);
    }
    if (edit) {
        GameString* const text = &StringAt(self, field::kText);
        At<int32_t>(self, field::kState) = kEditing;
        Virtual<void>(At<void*>(self, field::kSign), 0x14, static_cast<void*>(text));
        Virtual<void>(At<void*>(self, field::kSign), 0x1C, uint32_t{1});
        // The string's inlined assignment; its allocator argument is the
        // flag's address.
        This<void>(addr::SC4String_AssignRange, &StringAt(self, field::kSaved).body, text->body.begin, text->body.end,
                   &edit);
    }
    if (At<int32_t>(self, field::kState) != kEditing)
        At<int32_t>(self, field::kState) = kIdle;
    return 1;
}

// The pending bytes become one byte `character` (a temporary string of that
// one byte, 0x004D7ED0, assigned to the string at +0x68).
void SetPending(char* self, uint32_t character)
{
    GameString one;
    This<void*>(addr::SC4String_ConstructRepeated, &one, character, uint32_t{1});
    This<void>(addr::cRZBaseString_Assign, self + field::kPending, static_cast<void*>(&one));
    This<void>(addr::SC4String_Destroy, &one);
}

// The game's 0x004D8830 (slot 13, OnCharacter, ret 4): one byte of typed
// text. False unless a text is edited. Escape (0x1B) is taken and left to
// OnKeyDown. Enter (0x0D) ends the edit and lets the sign go (an empty one
// is removed). Backspace (8) takes the last character off (its bytes sized
// by 0x0091073C) and hands the text to the sign. Other bytes below 0x20 are
// taken and dropped. Anything else: in a multi-byte encoding (8, or 100 and
// up) a lead byte (0x009108B4) with nothing pending starts a character and
// waits; else the byte joins the pending ones, and when they are fewer than
// the first byte asks for (0x009108ED plus one) it waits too; a whole
// character is appended (AppendText) and the pending bytes cleared. The
// dword the view passes goes on as it comes.
uint8_t __fastcall OnCharacter(char* self, void*, uint32_t character)
{
    if (At<int32_t>(self, field::kState) != kEditing)
        return 0;
    const uint32_t encoding = At<uint32_t>(self, field::kEncoding);
    const bool multiByte = encoding == 8 || static_cast<int32_t>(encoding) >= 0x64;
    const uint8_t byte = static_cast<uint8_t>(character);
    if (byte == 0x1B)
        return 1;
    if (byte == 0x0D) {
        EndEditing(self);
        SetSign(self, nullptr, nullptr);
        At<int32_t>(self, field::kState) = kIdle;
        return 1;
    }
    if (byte == 8) {
        GameString* const text = &StringAt(self, field::kText);
        int32_t length = Virtual<int32_t>(text, 0x1C);
        if (length <= 0)
            return 1;
        if (multiByte)
            length -= Cdecl<int32_t>(addr::RZText_PreviousCharacterSize, text->body.begin + length, length,
                                     At<uint32_t>(self, field::kEncoding));
        else
            --length;
        Virtual<void>(text, 0x3C, length);
        Virtual<void>(At<void*>(self, field::kSign), 0x10, static_cast<void*>(text));
        return 1;
    }
    if (byte < 0x20)
        return 1;
    StringBody* const pending = &StringAt(self, field::kPending).body;
    if (multiByte) {
        if (pending->begin == pending->end && Cdecl<uint8_t>(addr::RZText_IsLeadByte, character, encoding)) {
            SetPending(self, character);
            return 1;
        }
        This<void*>(addr::cSC4Advisors_F417AD0, pending, character);
        const uint32_t needed = Cdecl<uint32_t>(addr::RZText_TrailingBytes,
                                                uint32_t{static_cast<uint8_t>(*pending->begin)},
                                                At<uint32_t>(self, field::kEncoding)) + 1;
        if (static_cast<uint32_t>(pending->end - pending->begin) < needed)
            return 1;
    } else {
        SetPending(self, character);
    }
    AppendText(self, nullptr, self + field::kPending);
    Empty(*pending);
    return 1;
}

// The game's 0x004D89D0 (slot 14, OnKeyDown, ret 8). Not editing: Delete
// (0x2E) with a sign lets it go, removes it from the city (RemoveProp) and
// is taken; any other key is not. Editing: Escape (0x1B) puts the text the
// edit started with back in the sign and in +0x3C, goes on editing and is
// not taken; V (0x56) with the modifier bit 1 set and bits 0 and 2 clear
// appends the clipboard's text (the service's slot 18 with a string and 0);
// every key but Escape is taken.
uint8_t __fastcall OnKeyDown(char* self, void*, uint32_t key, uint32_t modifiers)
{
    if (At<int32_t>(self, field::kState) != kEditing) {
        if (key != 0x2E || !At<void*>(self, field::kSign))
            return 0;
        void* const occupant = Virtual<void*>(At<void*>(self, field::kSign), 0xC);
        if (occupant)
            AddRef(occupant);
        SetSign(self, nullptr, nullptr);
        Virtual<uint8_t>(Global<void*>(addr::cSC4PropManager_Global), 0x74, occupant);
        At<int32_t>(self, field::kState) = kIdle;
        if (occupant)
            Release(occupant);
        return 1;
    }
    if (key == 0x1B) {
        if (void* const sign = At<void*>(self, field::kSign)) {
            Virtual<void>(sign, 0x10, static_cast<void*>(self + field::kSaved));
            This<void>(addr::cRZBaseString_Assign, self + field::kText, static_cast<void*>(self + field::kSaved));
        }
        return 0;
    }
    if (((modifiers >> 1) & 1) && !(modifiers & 1) && !((modifiers >> 2) & 1) && key == 0x56) {
        GameString pasted;
        This<void*>(addr::SC4String_SC4String, &pasted);
        if (void* const clipboard = At<void*>(self, field::kClipboard)) {
            if (Virtual<uint8_t>(clipboard, 0x48, static_cast<void*>(&pasted), uint32_t{0}))
                AppendText(self, nullptr, &pasted);
        }
        This<void>(addr::SC4String_Destroy, &pasted);
    }
    return 1;
}

// --- Registration ----------------------------------------------------------------------

bool InstallSignText(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[16] = {};
    static const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::SC4SignPlaceTool_QueryInterface, reinterpret_cast<void*>(&QueryInterface)},
        {addr::SC4SignPlaceTool_Init, reinterpret_cast<void*>(&Init)},
        {addr::SC4SignPlaceTool_FilterIncludes, reinterpret_cast<void*>(&FilterIncludes)},
        {addr::SC4SignPlaceTool_GroundHeight, reinterpret_cast<void*>(&GroundHeight)},
        {addr::SC4SignPlaceTool_DoNotice, reinterpret_cast<void*>(&DoNotice)},
        {addr::SC4SignPlaceTool_Construct, reinterpret_cast<void*>(&Construct)},
        {addr::SC4SignPlaceTool_SetSign, reinterpret_cast<void*>(&SetSign)},
        {addr::SC4SignPlaceTool_DeletingDestructor, reinterpret_cast<void*>(&DeletingDestruct)},
        {addr::SC4SignPlaceTool_Destruct, reinterpret_cast<void*>(&Destruct)},
        {addr::SC4SignPlaceTool_Shutdown, reinterpret_cast<void*>(&Shutdown)},
        {addr::SC4SignPlaceTool_OnMouseDownL, reinterpret_cast<void*>(&OnMouseDownL)},
        {addr::SC4SignPlaceTool_OnMouseMove, reinterpret_cast<void*>(&OnMouseMove)},
        {addr::SC4SignPlaceTool_AppendText, reinterpret_cast<void*>(&AppendText)},
        {addr::SC4SignPlaceTool_OnMouseUpL, reinterpret_cast<void*>(&OnMouseUpL)},
        {addr::SC4SignPlaceTool_OnCharacter, reinterpret_cast<void*>(&OnCharacter)},
        {addr::SC4SignPlaceTool_OnKeyDown, reinterpret_cast<void*>(&OnKeyDown)},
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
    "ui.tools.signtext",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4SignPlaceTool_OnCharacter,
    "Our sign tool of the city view: placing signs and typing their text (docs/tools.md)",
    false,
    InstallSignText,
});

} // namespace
} // namespace opensc4::tool_signtext
