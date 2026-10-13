// The city view (cSC4View3DWin), sixth part: 0x007F1640 to 0x007F3C10. Four
// functions, all of them the view's own:
//
// - 0x007F1640 looks in the game's Plugins folder for a background picture
//   ("Background2D" or "Background3D" and the zoom's digit, as .bmp, .jpg,
//   .png, .gif or .tga; square with a power of two as its side) and hands
//   the first one that loads to the 3D render. A developer's feature: the
//   shipped game has no such file, so the search ends at the first test.
// - 0x007F1C10 is ProcessCommand, slot 7 of the view's interface (vtable
//   0x00ABCE40): one big switch over the command IDs the view answers
//   (zoom, rotation, scrolling by key, the speed buttons, the tools of the
//   mayor and god menus, a few dialogs, saving and leaving). It always
//   returns false.
// - 0x007F3510 (a start the splitter missed, after the command handler's
//   jump tables) takes the cheat codes the view registered: the window's
//   message handler (0x007F3EA8 onward) calls it with the cheat's ID and
//   the typed line cut into words.
// - 0x007F39D0 answers three of the menu bar's buttons (IDs 0x35, 0x39 and
//   0x40) by building the pop-up menu of the buildings of one kind, and a
//   fourth (0x32) by taking up a tool.
//
// The view, as far as this file touches it: +0 the interface (vtable
// 0x00ABCE40), +0xC the window, +0xF0 the camera control, +0xF4 the 3D
// render, +0x124 a byte: the timer cheats are on, +0x126 a byte a cheat
// sets, +0x134 and +0x138 the tools in use (a vector's begin and end),
// +0x150 the picture loader (service 0xA2FFB5D3), +0x154 the simulator,
// +0x158 the background kind (0 none yet, 1 flat, 2 by zoom), +0x18C the
// menu bar's window, +0x1F8 and +0x1FC the second and third menus, +0x204
// the button the third menu belongs to, +0x25C, +0x264 and +0x26C three
// lists the menus keep their items in, +0x320 the view's occupant filter,
// +0x348 and +0x34C the key scroll's direction (x, y: -1, 0 or 1), +0x446 a
// byte a cheat sets.
//
// Drawing stays the game's: the render's SetBackgroundPicture, PickTerrain
// and ForceFullRedraw are called through its vtable, the camera control by
// address.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace opensc4::ui {
namespace {

namespace view {
constexpr std::size_t kWindow = 0xC;
constexpr std::size_t kCamera = 0xF0;
constexpr std::size_t kRender = 0xF4;
constexpr std::size_t kTimerCheats = 0x124;
constexpr std::size_t kCheat126 = 0x126;
constexpr std::size_t kToolsBegin = 0x134;
constexpr std::size_t kToolsEnd = 0x138;
constexpr std::size_t kPictures = 0x150;
constexpr std::size_t kSimulator = 0x154;
constexpr std::size_t kBackground = 0x158;
constexpr std::size_t kMenuBar = 0x18C;
constexpr std::size_t kSecondMenu = 0x1F8;
constexpr std::size_t kThirdMenu = 0x1FC;
constexpr std::size_t kThirdMenuButton = 0x204;
constexpr std::size_t kItems25C = 0x25C;
constexpr std::size_t kItems264 = 0x264;
constexpr std::size_t kItems26C = 0x26C;
constexpr std::size_t kFilter = 0x320;
constexpr std::size_t kScrollX = 0x348;
constexpr std::size_t kScrollY = 0x34C;
constexpr std::size_t kCheat446 = 0x446;
} // namespace view

constexpr std::size_t kCameraZoom = 0x108;
constexpr std::size_t kOptionsF0A = 0xF0A;

constexpr uint32_t kButtonIid = 0x8810;
constexpr uint32_t kFilterIid = 0xE4FDA3D4;
constexpr uint32_t kFlagFilterClass = 0x8A7A1C2C;
constexpr uint32_t kMenuCommand = 0x6A2B245F;

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

// A set of numbers (a red-black tree with a head node of 0x14 bytes).
struct SetNode {
    uint8_t colour;
    SetNode* parent;
    SetNode* left;
    SetNode* right;
    uint32_t value;
};
static_assert(sizeof(SetNode) == 0x14);

struct NumberSet {
    SetNode* head;
    uint32_t count;
};

struct InsertResult {
    void* node;
    uint8_t inserted;
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

void* New(uint32_t size)
{
    return Cdecl<void*>(addr::SC4_operator_new, size);
}

// The game's reference holder made from an object: one reference taken.
void* Held(void* object)
{
    void* holder;
    This<void**>(addr::SC4RefHolder_SC4RefHolder, &holder, object);
    return holder;
}

// An empty string as the game's inlined constructor makes it.
void InitString(GameString& text)
{
    text.body.begin = nullptr;
    text.body.end = nullptr;
    text.body.capacity = nullptr;
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    text.body.begin = buffer;
    text.body.end = buffer;
    text.body.capacity = buffer + 8;
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

void AssignRange(StringBody& body, const char* begin, const char* end, uint8_t* tag)
{
    This<void>(addr::SC4String_AssignRange, &body, begin, end, tag);
}

void AppendRange(StringBody& body, const char* begin, const char* end, uint8_t* tag)
{
    This<void*>(addr::cSC4Advisor_F40EBC0, &body, begin, end, tag);
}

// The inlined push_back of one character.
void PushBack(StringBody& body, uint8_t character)
{
    char* end = body.end;
    if (end + 1 == body.capacity) {
        const uint32_t size = static_cast<uint32_t>(end - body.begin);
        const uint32_t more = size < 1 ? uint32_t{1} : size;
        This<void>(addr::cSC4Advisor_F40F1B0, &body, more + size);
        end = body.end;
    }
    end[1] = 0;
    *body.end = static_cast<char>(character);
    ++body.end;
}

// pattern = folder + text, through the two temporaries the original makes.
void AssignJoined(StringBody& pattern, StringBody& folder, const char* text, uint8_t* tag)
{
    StringBody joined;
    StringBody* const result = Cdecl<StringBody*>(addr::SC4String_ConcatBodyText, &joined, &folder, text);
    char* const end = result->end;
    char* const begin = result->begin;
    StringBody copy = {nullptr, nullptr, nullptr};
    This<void*>(addr::cSC4Advice_F408420, &copy, begin, end, tag);
    char* const copied = copy.begin;
    AssignRange(pattern, copied, copy.end, tag);
    if (copied)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(copied));
    if (joined.begin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(joined.begin));
}

// Tries one file: loads it and keeps it only when it is square with a power
// of two as its side.
void TryPicture(char* self, GameString& path, void*& picture)
{
    if (!Cdecl<uint8_t>(addr::RZFile_Exists, &path))
        return;
    if (!Virtual<uint8_t>(At<void*>(self, view::kPictures), 0x10, &path, &picture, uint32_t{8}))
        return;
    const uint32_t width = Virtual<uint32_t>(picture, 0x24);
    const uint32_t height = Virtual<uint32_t>(picture, 0x28);
    if (width != height || (width & (width - 1)) != 0) {
        Release(picture);
        picture = nullptr;
    }
}

// The game's 0x007F1640. this = the view. Returns whether a background
// picture was given to the render.
//
// The two loops over the file types have no end of their own in the
// original (the test after `add edi,6` is on the pointer, never zero): they
// stop when a picture is kept. When a file matches the pattern and none of
// the five types gives a square picture, the original walks on up its stack
// in steps of six bytes; ours walks on from its own table the same way.
uint8_t __fastcall LoadBackground(char* self, void*)
{
    static const char kFlat[] = "Background2D";
    static const char kByZoom[] = "Background3D";
    static const char kAny[] = ".*";

    GameString folder;
    InitString(folder);
    GameString path;
    InitString(path);
    GameString pattern;
    InitString(pattern);
    char extensions[36] = {'.', 'b', 'm', 'p', 0, 0, '.', 'j', 'p', 'g', 0, 0, '.', 'p', 'n', 'g', 0, 0,
                           '.', 'g', 'i', 'f', 0, 0, '.', 't', 'g', 'a', 0, 0, 0,   0,   0,   0,   0, 0};
    void* picture = nullptr;
    uint8_t done = 0;
    uint8_t tag = 0;

    // The application's GetPluginDirectory.
    Virtual<uint8_t>(Global<void*>(addr::SC4_Service43C94), 0xDC, &folder);

    if (At<int32_t>(self, view::kBackground) != 2) {
        AssignJoined(pattern.body, folder.body, "Background2D.*", &tag);
        if (Cdecl<uint8_t>(addr::RZFile_PatternExists, &pattern)) {
            for (const char* extension = extensions; !picture; extension += 6) {
                AssignRange(path.body, folder.body.begin, folder.body.end, &tag);
                AppendRange(path.body, kFlat, kFlat + 12, &tag);
                const char* last = extension;
                while (*last)
                    ++last;
                AppendRange(path.body, extension, last, &tag);
                TryPicture(self, path, picture);
            }
        }
        if (picture && At<int32_t>(self, view::kBackground) == 0)
            At<int32_t>(self, view::kBackground) = 1;
    }

    if (At<int32_t>(self, view::kBackground) != 1) {
        // fild and ftol of the camera control's zoom: the integer itself.
        const int32_t zoom = At<int32_t>(At<void*>(self, view::kCamera), kCameraZoom);
        AssignJoined(pattern.body, folder.body, kByZoom, &tag);
        const uint8_t digit = static_cast<uint8_t>(static_cast<uint8_t>(zoom) + 0x30);
        tag = digit;
        PushBack(pattern.body, digit);
        AppendRange(pattern.body, kAny, kAny + 2, &tag);
        if (Cdecl<uint8_t>(addr::RZFile_PatternExists, &pattern)) {
            for (const char* extension = extensions; !picture; extension += 6) {
                AssignRange(path.body, folder.body.begin, folder.body.end, &tag);
                AppendRange(path.body, kByZoom, kByZoom + 12, &tag);
                PushBack(path.body, tag);
                const char* last = extension;
                while (*last)
                    ++last;
                AppendRange(path.body, extension, last, &tag);
                TryPicture(self, path, picture);
            }
        }
        if (picture && At<int32_t>(self, view::kBackground) == 0)
            At<int32_t>(self, view::kBackground) = 2;
    }

    if (picture) {
        // The render's SetBackgroundPicture(picture, by zoom). The original
        // pushes the kind with its low byte replaced by the flag.
        const uint32_t kind = At<uint32_t>(self, view::kBackground);
        const uint32_t byZoom = (kind & 0xFFFFFF00u) | (kind == 2 ? 1u : 0u);
        Virtual<uint8_t>(At<void*>(self, view::kRender), 0xD8, picture, byZoom);
        Release(picture);
        done = 1;
    }

    DestroyString(pattern);
    DestroyString(path);
    DestroyString(folder);
    return done;
}

bool Established()
{
    return Virtual<uint8_t>(Global<void*>(addr::SC4_City), 0x58) != 0;
}

// SetCurrentViewInputControl(control, operation), then our reference goes.
void SetTool(char* self, void* control, int32_t operation)
{
    Virtual<uint8_t>(self, 0x64, control, operation);
    if (control)
        Release(control);
}

// One of the thirteen tools of the class made by 0x006620A0, in an
// established city only.
void SetKindTool(char* self, uint32_t kind)
{
    if (!Established())
        return;
    void* const memory = New(0x58);
    SetTool(self, Held(memory ? This<void*>(addr::SC4ViewInputControls_F6620A0, memory, kind) : nullptr), 2);
}

// The tool made by 0x004BA7D0.
void SetModeTool(char* self, uint32_t mode)
{
    void* const memory = New(0x90);
    SetTool(self, Held(memory ? This<void*>(addr::SC4DispatchTool_Construct, memory, mode) : nullptr), 2);
}

// The camera control's scroll at full strength, when no key scroll runs.
void ScrollOnce(char* self, uint32_t angleBits)
{
    if (At<int32_t>(self, view::kScrollX) != 0 || At<int32_t>(self, view::kScrollY) != 0)
        return;
    This<void>(addr::cSC4CameraControl_Scroll, At<void*>(self, view::kCamera), std::bit_cast<float>(angleBits),
               1.0f);
}

// A key scroll begins in one direction, when none runs.
void StartKeyScroll(char* self, std::size_t field, int32_t direction)
{
    if (At<int32_t>(self, view::kScrollX) != 0 || At<int32_t>(self, view::kScrollY) != 0)
        return;
    At<int32_t>(self, field) = direction;
    This<void>(addr::SC4UI_F7E6C60, self);
}

// Presses a toggle button of the menu bar: off when it is on, else on.
void ToggleBarButton(char* self, uint32_t id)
{
    void* button = nullptr;
    void* const bar = At<void*>(self, view::kMenuBar);
    if (Virtual<uint8_t>(bar, 0x94, id, kButtonIid, This<void**>(addr::SC4RefPtr_Reset, &button))) {
        if (Virtual<uint8_t>(button, 0x10, uint32_t{1}))
            Virtual<uint8_t>(button, 0x1C);
        else
            Virtual<uint8_t>(button, 0x18);
    }
    if (button)
        Release(button);
}

// The simulator's speed, running again when it was paused.
void SetSpeed(char* self, int32_t speed)
{
    void* const simulator = At<void*>(self, view::kSimulator);
    if (!simulator)
        return;
    Virtual<uint8_t>(simulator, 0x70, speed);
    if (Virtual<uint8_t>(At<void*>(self, view::kSimulator), 0x3C))
        Virtual<uint8_t>(At<void*>(self, view::kSimulator), 0x30);
    This<void>(addr::SC4UI_F7E59B0, self);
}

// The clock's day and night mode, kept in the preferences' first byte.
void SetDayNight(char* self, int32_t mode)
{
    void* const clock = Global<void*>(addr::SC4_Clock43DBC);
    uint8_t* const preferences = Virtual<uint8_t*>(Global<void*>(addr::SC4_Service43C94), 0x98);
    Virtual<uint8_t>(clock, 0x74, uint32_t{1});
    Virtual<uint8_t>(clock, 0x14, mode);
    *preferences = Virtual<uint8_t>(clock, 0x18);
    This<void>(addr::SC4UI_F7E6EE0, self);
}

void OpenMenu(char* self, uint32_t menu)
{
    This<void>(addr::SC4UI_F7E9FA0, self, kMenuCommand, menu, uint32_t{0});
}

// A command's parameter as a bool.
uint8_t BoolParameter(void* parameters, uint32_t index)
{
    return Virtual<uint8_t>(Virtual<void*>(parameters, 0x14, index), 0x24);
}

// The game's 0x007F1C10: cISC4View3DWin::ProcessCommand, slot 7 of the
// view's interface. this = the view. Returns false for every command.
uint8_t __fastcall ProcessCommand(char* self, void*, uint32_t id, void* parameters, void* /*results*/)
{
    switch (id) {
    // Key scrolling ends.
    case 0x2A94826B:
    case 0x2A94826E:
    case 0x2A948272:
    case 0x2A948275:
        At<int32_t>(self, view::kScrollX) = 0;
        At<int32_t>(self, view::kScrollY) = 0;
        This<void>(addr::SC4UI_F7E6C60, self);
        break;

    case 0x0B980587: {
        void* const memory = New(0x44);
        SetTool(self, Held(memory ? This<void*>(addr::SC4LabelTool_Construct, memory) : nullptr), 2);
        break;
    }
    case 0x0B980581: {
        void* const memory = New(0x80);
        SetTool(self, Held(memory ? This<void*>(addr::SC4SignPlaceTool_Construct, memory) : nullptr), 2);
        break;
    }

    // One scroll step: the angles are pi, pi/2, 0 and 3 pi/2.
    case 0x2A9484DA:
        ScrollOnce(self, 0x40490FDB);
        break;
    case 0x2A94850B:
        ScrollOnce(self, 0x3FC90FDB);
        break;
    case 0x2A9484FF:
        ScrollOnce(self, 0x00000000);
        break;
    case 0x2A9484F2:
        ScrollOnce(self, 0x4096CBE4);
        break;

    case 0x2A94A04B:
        This<void>(addr::SC4UI_F7E5FB0, self);
        break;
    case 0x2A94A5B6:
        if (Established())
            This<void>(addr::SC4UI_F7E9150, self);
        break;

    case 0x2B79E72B:
        SetKindTool(self, 8);
        break;
    case 0x4BE098F7:
        SetKindTool(self, 0xA);
        break;
    case 0x4BE098FD:
        SetKindTool(self, 0xC);
        break;
    case 0x6A935DC0:
        SetKindTool(self, 0);
        break;
    case 0x6A935DDA:
        SetKindTool(self, 1);
        break;
    case 0x6A935DC5:
        SetKindTool(self, 2);
        break;
    case 0x6A935DC2:
        SetKindTool(self, 3);
        break;
    case 0x6A935DEE:
        SetKindTool(self, 4);
        break;
    case 0x6A935DEB:
        SetKindTool(self, 5);
        break;
    case 0x6B730F78:
        SetKindTool(self, 6);
        break;
    case 0x6A935DE3:
        SetKindTool(self, 7);
        break;
    case 0x8BE098F4:
        SetKindTool(self, 9);
        break;
    case 0x6BE098FA:
        SetKindTool(self, 0xB);
        break;

    case 0x2BCBD5F1:
        Virtual<int32_t>(self, 0x20, int32_t{5});
        break;

    // The menus the menu bar opens.
    case 0x2BFE0BEA:
        OpenMenu(self, 0x6BB942D7);
        break;
    case 0x2BFE1095:
        OpenMenu(self, 0x0B96AE64);
        break;
    case 0x2BFE11C9:
        OpenMenu(self, 0x4BAFBA9F);
        break;
    case 0x6A935DC9:
        OpenMenu(self, 0x05000001);
        break;
    case 0x6A935DDD:
        OpenMenu(self, 0x6A8A358F);
        break;
    case 0x6A935DDF:
        OpenMenu(self, 0x6A3855D4);
        break;
    case 0x6A935DE6:
        OpenMenu(self, 0x05000002);
        break;
    case 0x6A935DF1:
        OpenMenu(self, 0x69DE426B);
        break;
    case 0x6A935DF4:
        OpenMenu(self, 0x01000005);
        break;
    case 0x6A935DF7:
        OpenMenu(self, 0x01000003);
        break;
    case 0x6A935DFA:
        OpenMenu(self, 0xA9DD1E4C);
        break;
    case 0x6A935DFD:
        OpenMenu(self, 0x01000007);
        break;
    case 0x6A935E0A:
        OpenMenu(self, 0x2A2735A0);
        break;
    case 0x6A935E0D:
        OpenMenu(self, 0xEA273867);
        break;
    case 0x6A935E14:
        OpenMenu(self, 0x89DD094E);
        break;
    case 0x6A935E11:
        OpenMenu(self, 0x4A2AF441);
        break;
    case 0x6A935E17:
        OpenMenu(self, 0x4A2829D1);
        break;
    case 0xEC3E82F8: {
        const uint32_t menu = *Virtual<uint32_t*>(Virtual<void*>(parameters, 0x14, uint32_t{0}), 0x124);
        if (menu)
            OpenMenu(self, menu);
        break;
    }

    // Toggle buttons of the menu bar.
    case 0x6A935C34:
        ToggleBarButton(self, 0x2988BC85);
        break;
    case 0x6A935CA4:
        ToggleBarButton(self, 0xC988BC79);
        break;
    case 0x6A935CA8:
        ToggleBarButton(self, 0x4988BC6A);
        break;
    case 0x6A935CAB:
        ToggleBarButton(self, 0x8988BC94);
        break;

    // Pause or go on.
    case 0x6A935CAD: {
        void* const simulator = At<void*>(self, view::kSimulator);
        if (!simulator)
            break;
        if (Virtual<uint8_t>(simulator, 0x3C))
            Virtual<uint8_t>(At<void*>(self, view::kSimulator), 0x30);
        else
            Virtual<uint8_t>(At<void*>(self, view::kSimulator), 0x24);
        break;
    }
    case 0x6A935CB0:
        SetSpeed(self, 0);
        break;
    case 0x6A935CB2:
        SetSpeed(self, 1);
        break;
    case 0x6A935CB5:
        SetSpeed(self, 2);
        break;

    case 0x6A935CB7:
        Virtual<int32_t>(self, 0x24);
        break;
    case 0x6A935CBA:
        Virtual<int32_t>(self, 0x28);
        break;
    case 0x6A935CBD:
        Virtual<int32_t>(self, 0x20, int32_t{0});
        break;
    case 0x6A935CBF:
        Virtual<int32_t>(self, 0x20, int32_t{1});
        break;
    case 0x6A935CC2:
        Virtual<int32_t>(self, 0x20, int32_t{2});
        break;
    case 0x6A935CC4:
        Virtual<int32_t>(self, 0x20, int32_t{3});
        break;
    case 0x6A935CC7:
        Virtual<int32_t>(self, 0x20, int32_t{4});
        break;
    case 0x6A935CC9:
        Virtual<int32_t>(self, 0x3C);
        break;
    case 0x6A935CCC:
        Virtual<int32_t>(self, 0x40);
        break;

    // Key scrolling begins.
    case 0x6A935CD8:
        StartKeyScroll(self, view::kScrollX, -1);
        break;
    case 0x6A935CDD:
        StartKeyScroll(self, view::kScrollY, -1);
        break;
    case 0x6A935CE0:
        StartKeyScroll(self, view::kScrollX, 1);
        break;
    case 0x6A935CE2:
        StartKeyScroll(self, view::kScrollY, 1);
        break;

    // The view goes to the place under the cursor.
    case 0x6A935CE5: {
        const uint32_t unset = Global<uint32_t>(addr::SC4View3DWin_UnsetCoordinate);
        uint32_t place[3] = {unset, unset, unset};
        void* const manager = Cdecl<void*>(addr::SC4_GetWinManager);
        void* const window = self ? self + view::kWindow : nullptr;
        // The original keeps the cursor's place in its own argument slots.
        int32_t x = static_cast<int32_t>(id);
        int32_t y = static_cast<int32_t>(reinterpret_cast<std::uintptr_t>(parameters));
        Virtual<uint8_t>(manager, 0x88, window, &x, &y);
        if (Virtual<uint8_t>(At<void*>(self, view::kRender), 0xF0, x, y, static_cast<void*>(place), uint32_t{0},
                             uint32_t{1}))
            This<void>(addr::cSC4CameraControl_SetCenter, At<void*>(self, view::kCamera), static_cast<void*>(place));
        break;
    }

    case 0x6A935CE8: {
        void* const server = Global<void*>(addr::SC4_MessageServer);
        if (!server)
            break;
        void* const memory = Cdecl<void*>(addr::RZ_operator_new, uint32_t{0x2C});
        void* const message = memory ? This<void*>(addr::cGZMessage_cGZMessage, memory) : nullptr;
        AddRef(message);
        Virtual<void>(message, 0x14, uint32_t{0x277A9364});
        Virtual<uint8_t>(server, 0x10, message, uint32_t{0});
        Release(message);
        break;
    }

    // Escape: the tools go, else the third menu, else the second.
    case 0x6A935CF1:
        if (At<void*>(self, view::kToolsBegin) != At<void*>(self, view::kToolsEnd))
            Virtual<uint8_t>(self, 0x6C, uint32_t{0});
        else if (At<void*>(self, view::kThirdMenu))
            Virtual<void>(self, 0xF0);
        else if (At<void*>(self, view::kSecondMenu))
            Virtual<uint8_t>(self, 0xEC);
        break;

    // Three tools that are not taken up twice.
    case 0x6A935CF3:
    case 0x6A935CF4:
    case 0x6A935D36: {
        if (!Established())
            break;
        void* current = Virtual<void*>(self, 0x60);
        if (current && !Virtual<uint8_t>(current, 0x30)) {
            Virtual<uint8_t>(self, 0x68, uint32_t{0});
            current = Virtual<void*>(self, 0x60);
        }
        if (id == 0x6A935CF3) {
            if (current && Virtual<uint32_t>(current, 0x14) == 0xC7AF928E)
                break;
            void* const memory = New(0xA0);
            SetTool(self,
                    Held(memory ? This<void*>(addr::SC4ViewInputControls_F4C4590, memory, uint32_t{0}, uint32_t{0x32})
                                : nullptr),
                    0);
        } else if (id == 0x6A935CF4) {
            if (current && Virtual<uint32_t>(current, 0x14) == 0xC7AF928F)
                break;
            void* const memory = New(0xA0);
            SetTool(self,
                    Held(memory ? This<void*>(addr::SC4ViewInputControls_F4C4590, memory, uint32_t{1}, uint32_t{0x32})
                                : nullptr),
                    0);
        } else {
            if (current && Virtual<uint32_t>(current, 0x14) == 0x46DDB5F1)
                break;
            void* const memory = New(0xD8);
            SetTool(self, Held(memory ? This<void*>(addr::SC4ViewInputControls_F4B9070, memory) : nullptr), 0);
        }
        break;
    }

    case 0x6A935CF7:
        This<void>(addr::SC4UI_F7E6DA0, self, uint32_t{1});
        break;
    case 0x6A935D0B:
        This<void>(addr::SC4UI_F7E6DA0, self, uint32_t{2});
        break;
    case 0x6A935D0E:
        This<void>(addr::SC4UI_F7E6DA0, self, uint32_t{3});
        break;
    case 0x6A935D11:
        This<void>(addr::SC4UI_F7E6DA0, self, uint32_t{4});
        break;
    case 0x6A935D17:
        This<void>(addr::SC4UI_F7E6DA0, self, uint32_t{5});
        break;
    case 0x6A935D1A:
        This<void>(addr::SC4UI_F7E6DA0, self, uint32_t{6});
        break;
    case 0x6A935D1D:
        This<void>(addr::SC4UI_F7E6DA0, self, uint32_t{7});
        break;
    case 0x6A935D25:
        This<void>(addr::SC4UI_F7E6DA0, self, uint32_t{8});
        break;
    case 0x6A935D28:
        This<void>(addr::SC4UI_F7E6DA0, self, uint32_t{0xE});
        break;
    case 0x6A935D2B:
        This<void>(addr::SC4UI_F7E6DA0, self, uint32_t{9});
        break;
    case 0x6A935D2E:
        This<void>(addr::SC4UI_F7E6DA0, self, uint32_t{0});
        break;

    case 0x6A935D39:
        if (Established())
            SetModeTool(self, 1);
        break;
    case 0xCA935DBD:
        if (Established())
            SetModeTool(self, 0);
        break;
    case 0x8C0B0E46:
    case 0xEC0B0E6A: {
        void* const memory = New(0x90);
        void* control = nullptr;
        if (memory) {
            control = This<void*>(addr::SC4DispatchTool_Construct, memory,
                                  uint32_t{id == 0xEC0B0E6A ? 6u : 5u});
            if (control)
                AddRef(control);
        }
        SetTool(self, control, 2);
        break;
    }
    case 0xABCDEE11: {
        void* const memory = New(0x30);
        SetTool(self, Held(memory ? This<void*>(addr::SC4SignRemoveTool_Construct, memory) : nullptr), 2);
        break;
    }

    case 0x6A935E1E:
        SetDayNight(self, 1);
        break;
    case 0x6A935E21:
        SetDayNight(self, 2);
        break;
    case 0x6A935E27:
        This<void>(addr::SC4UI_F7E70C0, self, uint32_t{0xCA35CB74});
        break;

    // Three dialogs, each made, started, shown and let go.
    case 0x6A935E2A: {
        void* const memory = New(0x10);
        void* const dialog = Held(memory ? This<void*>(addr::SC4UI_F76AB30, memory) : nullptr);
        Virtual<uint8_t>(dialog, 0x14, uint32_t{0});
        This<void>(addr::SC4UI_F76AB50, dialog);
        This<void>(addr::SC4Win_F99BDE4, self + view::kWindow);
        Release(dialog);
        break;
    }
    case 0x6A935E2D: {
        void* const memory = New(0x20);
        void* const dialog = Held(memory ? This<void*>(addr::SC4UI_F791360, memory) : nullptr);
        Virtual<uint8_t>(dialog, 0x14, uint32_t{0});
        This<void>(addr::SC4UI_F7913A0, dialog);
        This<void>(addr::SC4Win_F99BDE4, self + view::kWindow);
        Release(dialog);
        break;
    }
    case 0x6A935E34: {
        void* const memory = New(0x80);
        void* const dialog = Held(memory ? This<void*>(addr::SC4UI_F4F2450, memory) : nullptr);
        Virtual<uint8_t>(dialog, 0x14);
        This<void>(addr::SC4UI_F4F25F0, dialog);
        This<void>(addr::SC4Win_F99BDE4, self + view::kWindow);
        Release(dialog);
        break;
    }

    // The application's RequestSaveCity, RequestGoToRegionView and
    // RequestQuit. The original's second flag is a byte written over the
    // low byte of its parameters argument and pushed as a dword, and the
    // first is pushed from the same register.
    case 0x6A935E36:
    case 0x6A935E37: {
        const uint8_t quiet = BoolParameter(parameters, 0);
        const uint32_t fast = (id == 0x6A935E37 || Cdecl<uint8_t>(addr::SC4_IsKeyDown, uint32_t{0x12})) ? 1u : 0u;
        const uint32_t high = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(parameters)) & 0xFFFFFF00u;
        Virtual<uint8_t>(Global<void*>(addr::SC4_Service43C94), 0x24, high | (quiet == 0 ? 1u : 0u), high | fast);
        break;
    }
    case 0x6A935E39: {
        const uint8_t quiet = BoolParameter(parameters, 0);
        Virtual<uint8_t>(Global<void*>(addr::SC4_Service43C94), 0x30, uint32_t{quiet == 0 ? 1u : 0u});
        break;
    }
    case 0x6A935E3C: {
        const uint8_t quiet = BoolParameter(parameters, 0);
        const uint32_t save = BoolParameter(parameters, 1);
        const uint32_t high = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(parameters)) & 0xFFFFFF00u;
        Virtual<uint8_t>(Global<void*>(addr::SC4_Service43C94), 0x28, high | (quiet == 0 ? 1u : 0u), high | save);
        break;
    }

    case 0x6A935E3F: {
        const uint32_t shown = Virtual<uint32_t>(At<void*>(self, view::kMenuBar), 0x11C);
        Virtual<uint8_t>(self, 0xDC, shown);
        break;
    }

    // A window of the region view's family, run and let go; the focus goes
    // back to whoever had it.
    case 0x6A935E45: {
        void* const window = self ? self + view::kWindow : nullptr;
        void* const dialog = Cdecl<void*>(addr::SC4UI_F7BDD60, uint32_t{1}, uint32_t{1}, window, uint32_t{0});
        if (!dialog)
            break;
        void* const manager = Cdecl<void*>(addr::SC4_GetWinManager);
        void* const focus = Virtual<void*>(manager, 0x90);
        This<void>(addr::SC4UI_F7BDC90, dialog, uint32_t{0});
        This<void>(addr::SC4UI_F7B69E0, dialog);
        Release(dialog);
        if (Virtual<uint8_t>(manager, 0x60, focus))
            Virtual<uint8_t>(manager, 0x94, focus);
        break;
    }
    case 0x6A935E4B:
        if (self)
            Cdecl<uint8_t>(addr::SC4UI_F7B7530, static_cast<void*>(self + view::kWindow), uint32_t{0});
        else
            Cdecl<uint8_t>(addr::SC4UI_F7B7530, static_cast<void*>(nullptr), uint32_t{0});
        break;

    case 0x6A9391A0:
        if (void* const render = At<void*>(self, view::kRender))
            Virtual<uint8_t>(render, 0x50);
        break;

    // The timer service's two cheats' keys: stepping on or off, one step.
    case 0x6A9753EA: {
        if (!At<uint8_t>(self, view::kTimerCheats))
            break;
        void* const timer = Cdecl<void*>(addr::SC4_GetService86AD10EC);
        if (!timer)
            break;
        const uint8_t stepped = Virtual<uint8_t>(Cdecl<void*>(addr::SC4_GetService86AD10EC), 0x34);
        Virtual<uint8_t>(timer, 0x30, uint32_t{stepped ? 0u : 1u});
        break;
    }
    case 0x6A9753ED:
        if (At<uint8_t>(self, view::kTimerCheats)) {
            if (void* const timer = Cdecl<void*>(addr::SC4_GetService86AD10EC))
                Virtual<uint8_t>(timer, 0x40);
        }
        break;

    case 0x6AA9FE51: {
        char* const options = Global<char*>(addr::SC4_Options43CD8);
        At<uint8_t>(options, kOptionsF0A) = At<uint8_t>(options, kOptionsF0A) == 0 ? 1 : 0;
        break;
    }

    // Signs and labels on or off; three tools go first when they are on.
    case 0x6B98058A: {
        if (void* const current = Virtual<void*>(self, 0x60)) {
            const uint32_t tool = Virtual<uint32_t>(current, 0x14);
            if (Virtual<uint8_t>(Global<void*>(addr::SC4_Service43DB8), 0x28)) {
                if (tool == 0x8B3CE416 || tool == 0x8B4CE5A0 || tool == 0xCBCC987E)
                    Virtual<uint8_t>(self, 0x6C, uint32_t{0});
            }
        }
        Virtual<uint8_t>(Global<void*>(addr::SC4_Service43DB8), 0x24);
        break;
    }

    case 0xCAB22C64:
        This<void>(addr::SC4UI_F7F0840, self, uint32_t{1});
        break;

    default:
        break;
    }
    return 0;
}

// The flag filter of the two filter cheats: the view's filter read through
// its interface, a new one given to the view (SetCurrentViewFilterInfo),
// both let go.
void GiveFilter(char* self, char* current, char* fresh, bool second)
{
    void* asFilter = nullptr;
    if (Virtual<uint8_t>(current, 0x0, kFilterIid, &asFilter)) {
        if (Virtual<uint32_t>(asFilter, 0x14) == kFlagFilterClass) {
            if (second) {
                At<uint8_t>(fresh, 0xD) = At<uint8_t>(current, 0xD) == 0 ? 1 : 0;
                At<uint8_t>(fresh, 0xE) = At<uint8_t>(current, 0xE);
                At<uint8_t>(fresh, 0xC) = At<uint8_t>(current, 0xC);
            } else {
                At<uint8_t>(fresh, 0xD) = At<uint8_t>(current, 0xD);
                At<uint8_t>(fresh, 0xC) = At<uint8_t>(current, 0xC) == 0 ? 1 : 0;
            }
        }
    }
    void* given = fresh;
    Virtual<uint8_t>(self, 0x4C, &given);
    if (asFilter)
        Release(asFilter);
    if (fresh)
        Release(fresh);
}

// The game's 0x007F3510: a cheat code the view registered was typed.
// this = the view; words = the typed line cut into words (slot 3 their
// count, slot 16 one of them as a string). Returns whether it was taken.
uint8_t __fastcall OnCheat(char* self, void*, uint32_t id, void* words)
{
    switch (id) {
    case 0x4A972F37:
        At<uint8_t>(self, view::kTimerCheats) = 1;
        return 1;
    case 0x4A82253E:
        At<uint8_t>(self, view::kCheat446) = 1;
        return 1;

    case 0x0BFD8064: {
        void* const memory = New(0x38);
        SetTool(self, Held(memory ? This<void*>(addr::SC4ViewInputControls_F4D9E80, memory) : nullptr), 2);
        return 1;
    }
    case 0x4AC096C6:
    case 0x4AC096CA: {
        void* const memory = New(0x158);
        void* control = nullptr;
        if (memory) {
            control = This<void*>(addr::SC4ViewInputControls_F4C0430, memory, uint32_t{id == 0x4AC096CA ? 1u : 0u},
                                  uint32_t{0}, uint32_t{0}, uint32_t{0});
            if (control)
                AddRef(control);
        }
        SetTool(self, control, 2);
        return 1;
    }
    case 0x8973A69C: {
        void* const memory = New(0x88);
        SetTool(self, Held(memory ? This<void*>(addr::SC4LifeTool_Construct, memory) : nullptr), 2);
        return 1;
    }
    case 0x6976AFA0:
        This<void>(addr::SC4UI_F7F0840, self, uint32_t{1});
        return 1;

    // The view's filter with one flag turned over. Both answer false.
    case 0x6A7A276E: {
        char* const current = At<char*>(self, view::kFilter);
        char* fresh = static_cast<char*>(New(0x10));
        if (fresh) {
            This<void*>(addr::SC4_OccupantFilterBase, fresh);
            At<std::uintptr_t>(fresh, 0x0) = addr::SC4CityFiles_PictureFilterVtable;
            At<std::uintptr_t>(fresh, 0x4) = addr::SC4CityFiles_PictureFilterVtable4;
            At<uint8_t>(fresh, 0xC) = 0;
            At<uint8_t>(fresh, 0xD) = 0;
            At<uint8_t>(fresh, 0xE) = 0;
            AddRef(fresh);
        }
        GiveFilter(self, current, fresh, false);
        return 0;
    }
    case 0xAA7A2746: {
        char* const current = At<char*>(self, view::kFilter);
        char* fresh = nullptr;
        if (void* const memory = New(0x10)) {
            fresh = This<char*>(addr::SC4ViewFilter_Construct, memory);
            if (fresh)
                AddRef(fresh);
        }
        GiveFilter(self, current, fresh, true);
        return 0;
    }

    // renderprop <property name> [0|1] [true|false]
    case 0x88F3D4BB:
    case 0x88F3D4BC: {
        if (Virtual<int32_t>(words, 0xC) >= 3) {
            void* const value = Virtual<void*>(words, 0x40, uint32_t{2});
            void* const name = Virtual<void*>(words, 0x40, uint32_t{1});
            This<void>(addr::SC4UI_F7ECDA0, self, name, value);
            return 1;
        }
        if (Virtual<int32_t>(words, 0xC) >= 2) {
            GameString none;
            void* const value = This<void*>(addr::SC4String_SC4String, &none);
            void* const name = Virtual<void*>(words, 0x40, uint32_t{1});
            This<void>(addr::SC4UI_F7ECDA0, self, name, value);
            This<void>(addr::SC4String_Destroy, &none);
            return 1;
        }
        Virtual<uint8_t>(self, 0xB4, "Cheat code usage: renderprop <property name> [0|1] [true|false]");
        return 0;
    }

    // A flag that is on unless the word after the cheat is "off".
    case 0x8AB730AE: {
        At<uint8_t>(self, view::kCheat126) = 1;
        if (Virtual<int32_t>(words, 0xC) <= 1)
            return 1;
        GameString word;
        This<void*>(addr::SC4String_Substring, &word, Virtual<void*>(words, 0x40, uint32_t{1}), uint32_t{0},
                    uint32_t{0xFFFFFFFF});
        This<void>(addr::cRZBaseString_MakeLower, &word);
        if (Cdecl<uint8_t>(addr::SC4String_Equals, &word.body, "off"))
            At<uint8_t>(self, view::kCheat126) = 0;
        DestroyString(word);
        return 1;
    }

    // The fine zoom, kept between 0.9 and 64. The lower bound is tested on
    // the number as read, the upper on it as a float.
    case 0xCA7BC5B5: {
        if (Virtual<int32_t>(words, 0xC) < 2)
            return 0;
        char* const word = Virtual<char*>(words, 0x40, uint32_t{1});
        const double read = Cdecl<double>(addr::SC4_ParseDouble90FC5B, At<const char*>(word, 0x4));
        float factor = static_cast<float>(read);
        if (read < static_cast<double>(std::bit_cast<float>(0x3F666666u)))
            factor = std::bit_cast<float>(0x3F666666u);
        else if (factor > 64.0f)
            factor = 64.0f;
        This<void>(addr::cSC4CameraControl_SetZoomFactor, At<void*>(self, view::kCamera), factor);
        return 1;
    }

    default:
        return 0;
    }
}

void InitSet(NumberSet& set)
{
    set.head = nullptr;
    set.head = Cdecl<SetNode*>(addr::SC4_stl_allocate, uint32_t{0x14});
    set.count = 0;
    set.head->colour = 0;
    set.head->parent = nullptr;
    set.head->left = set.head;
    set.head->right = set.head;
}

// The game's 0x007F39D0: one of the menu bar's numbered buttons was
// pressed. this = the view; message +8 = the button's number. Returns
// nothing its caller (the window's message handler, 0x007F46FF) reads.
void __fastcall OnBarButton(char* self, void*, void* button, char* message)
{
    static const uint32_t kGroup1400 = 0x1400;
    static const uint32_t kGroup1401 = 0x1401;
    static const uint32_t kGroup1402 = 0x1402;

    const uint32_t number = At<uint32_t>(message, 0x8);
    if (number - 0x32 > 0xE)
        return;

    if (number == 0x32) {
        Virtual<void>(self, 0xF0);
        void* const memory = New(0x58);
        void* control = nullptr;
        if (memory) {
            control = This<void*>(addr::SC4ViewInputControls_F6620A0, memory, uint32_t{5});
            if (control)
                AddRef(control);
        }
        SetTool(self, control, 2);
        return;
    }
    if (number != 0x35 && number != 0x39 && number != 0x40)
        return;

    NumberSet wanted;
    InitSet(wanted);
    NumberSet unwanted;
    InitSet(unwanted);
    InsertResult result;
    uint32_t flags = 0;
    uint32_t group = 0xFFFFFFFF;
    char* items = nullptr;

    switch (At<uint32_t>(message, 0x8)) {
    case 0x40:
        This<void>(addr::cSC4AdvisorTransportation_ZotInsert, &wanted, &result, &kGroup1402);
        This<void>(addr::cSC4AdvisorTransportation_ZotInsert, &unwanted, &result, &kGroup1401);
        group = 0x2A49D7CA;
        items = self + view::kItems26C;
        break;
    case 0x39:
        This<void>(addr::SC4Uint32Set_Insert2, &wanted, &result, &kGroup1401);
        flags = 0x4004;
        items = self + view::kItems264;
        break;
    case 0x35: {
        This<void>(addr::SC4Uint32Set_Insert2, &wanted, &result, &kGroup1400);
        This<void>(addr::SC4Uint32Set_Insert2, &unwanted, &result, &kGroup1402);
        // The ordinance simulator's IsOrdinanceOn.
        if (Virtual<uint8_t>(Global<void*>(addr::SC4_Service43D90), 0x20, uint32_t{0xE0D0722E})) {
            uint32_t extra = 0x6A650FFE;
            This<void>(addr::SC4Uint32Set_Insert2, &unwanted, &result, &extra);
        }
        flags = 0x4003;
        items = self + view::kItems25C;
        break;
    }
    default:
        break;
    }

    if (At<uint32_t>(self, view::kThirdMenuButton) == number) {
        Virtual<void>(self, 0xF0);
    } else {
        void* const filter = This<void*>(addr::SC4UI_F7F06B0, self, uint32_t{0xC8DBCCBA}, &wanted, &unwanted, flags,
                                         group);
        This<void>(addr::SC4UI_F7EAEB0, self, number, button, number, filter, uint32_t{0x14215ED3}, items);
        Release(filter);
    }
    This<void>(addr::SC4Uint32Set_Destroy46F9D0, &unwanted);
    This<void>(addr::SC4Uint32Set_Destroy46F9D0, &wanted);
}

bool InstallCityView6(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[4] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F7F1640, reinterpret_cast<void*>(&LoadBackground)},
        {addr::SC4UI_F7F1C10, reinterpret_cast<void*>(&ProcessCommand)},
        {addr::SC4UI_F7F3510, reinterpret_cast<void*>(&OnCheat)},
        {addr::SC4UI_F7F39D0, reinterpret_cast<void*>(&OnBarButton)},
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

const bool g_cityView6Registered = registry::Register({
    "ui.cityview.part6",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F7F1C10,
    "Our city view command handler, cheat handler, menu bar buttons and background picture search (docs/ui.md)",
    false,
    InstallCityView6,
});

} // namespace
} // namespace opensc4::ui
