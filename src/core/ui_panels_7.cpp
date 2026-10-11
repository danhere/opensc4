// The last part of the range between the department dialog and the map
// window (0x0079BBE0 to 0x0079E400). Five small window classes of the
// application and the map window's own small methods:
//
//   - the cheat box (0x120 bytes, no class ID, vtables 0x00AB7078 at +0,
//     0x00AB7020 at +0xD8 and 0x00AB7010 at +0x10C): the one-line edit box
//     the main window brings up on the cheat key (0x004E0BB0, command
//     0x2A9496C9). It is a framework window with a generic part
//     (cIGZWinGen, constructor 0x009C2CEC) whose text edit child takes the
//     line. Return hands the line to the cheat code service and, when that
//     does not know it, to the command service; the up and down keys walk
//     the lines entered before (a vector of strings at 0x00B4DFDC, kept for
//     the run), F1 offers every cheat string in a menu, Escape closes. Its
//     QueryInterface, Shutdown and sound helper (0x0079BAB0, 0x0079BAF0,
//     0x0079BB70) lie before this range;
//   - a window that the mouse passes through except over its visible
//     children (0x128 bytes, GZCOM class 0x89E1567C, factory 0x004661D0,
//     vtables 0x00AB7358 and 0x00AB72D8 at +0xD8);
//   - the game's candidate window for input methods (0x15C bytes, GZCOM
//     class 0x2AC45173, factory 0x004661F0, vtables 0x00AB78A0 at +0,
//     0x00AB7640 at +4, 0x00AB75C0 at +0xDC), derived from the framework's
//     (constructor 0x00994D12): its start, which dresses the framework's
//     list in the game's colours and picture, and the list filled with the
//     numbered candidates and sized to them;
//   - the intro window (0x108 bytes, no class ID, vtable 0x00AB78F0), which
//     the main window makes first (0x004E0C20): a movie window, two logo
//     windows out of the layout 0xAAA9C9D9 and a stopwatch;
//   - the picture file dialog (0xF0 bytes, no class ID, vtable 0x00AB7B58):
//     a window around the framework's file browser with six picture file
//     patterns, made by 0x00455182 and 0x007AEE4F and run as a modal
//     window;
//   - of the map window (src/core/ui_map_1.cpp onward): QueryInterface,
//     AddRef and Release of its main vtable, the "is shown" slot of its
//     window part and the four functions that show and hide its rows of
//     buttons.
//
// Replacement, off by default:
//
//   ui.panels.part7: everything named above.
//
// Still the game's in this range (docs/ui.md): the intro window's paint
// (0x0079D490, which clears the screen through the graphics driver and
// there runs the intro's timing) and the file dialog's paint (0x0079D6B0);
// the pixel blend 0x0079DC00, the blended fill 0x0079E0C0, the ring
// 0x0079E130 and the two coordinate setters 0x0079DDE0 and 0x0079DDF0,
// which only the map's fenced picture drawing uses; the rectangle
// intersection 0x0079DD60, the wide string code 0x0079C6E0, 0x0079C7C0,
// 0x0079C9B0, 0x0079C9D0 and 0x0079CA50, which the framework calls from
// all over; and the thunks and getters that the framework's own classes
// have in their vtables too (0x0079BBE0, 0x0079BC10, 0x0079C580, 0x0079C6F0
// to 0x0079C720, 0x0079C740 to 0x0079C770).
//
// The cheat box: +0 window vtable, +4 the window manager, +8 the keyboard,
// +0x48 the parent, +0xD8 the generic part, +0x10C the key filter given to
// the edit's window, +0x110 the text edit, +0x114 and +0x118 two sounds
// that nothing sets, +0x11C the place in the history.
//
// The candidate window: +4 the framework window, +0xDC its generic part,
// +0x14C the list, +0x150 the font, +0x154 and +0x158 two colours.
//
// The intro window: +0xD8 the movie window, +0xDC and +0xE0 the two logos,
// +0xE8 a stopwatch (mode 4), +0x100 a countdown of paints.
//
// The picture file dialog: +0x10 the window ID 0xEA9660F3, +0xD8 the file
// browser, +0xDC a string, the path.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <utility>

namespace opensc4::ui {
namespace {

namespace cheat {
constexpr std::size_t kManager = 0x4;
constexpr std::size_t kKeyboard = 0x8;
constexpr std::size_t kParent = 0x48;
constexpr std::size_t kGen = 0xD8;
constexpr std::size_t kFilter = 0x10C;
constexpr std::size_t kEdit = 0x110;
constexpr std::size_t kSoundA = 0x114;
constexpr std::size_t kSoundB = 0x118;
constexpr std::size_t kPlace = 0x11C;
} // namespace cheat

namespace hollow {
constexpr std::size_t kLeft = 0xA8;
constexpr std::size_t kTop = 0xAC;
constexpr std::size_t kRight = 0xB0;
constexpr std::size_t kBottom = 0xB4;
constexpr std::size_t kGen = 0xD8;
} // namespace hollow

namespace candidates {
constexpr std::size_t kWindow = 0x4;
constexpr std::size_t kParent = 0x4C;   // the window part's +0x48
constexpr std::size_t kGen = 0xDC;
constexpr std::size_t kList = 0x14C;
constexpr std::size_t kFont = 0x150;
constexpr std::size_t kDark = 0x154;
constexpr std::size_t kWhite = 0x158;
} // namespace candidates

namespace intro {
constexpr std::size_t kMovie = 0xD8;
constexpr std::size_t kLogoA = 0xDC;
constexpr std::size_t kLogoB = 0xE0;
constexpr std::size_t kWatch = 0xE8;
constexpr std::size_t kPaints = 0x100;
} // namespace intro

namespace picture {
constexpr std::size_t kId = 0x10;
constexpr std::size_t kBrowser = 0xD8;
constexpr std::size_t kPath = 0xDC;
} // namespace picture

namespace map {
constexpr std::size_t kTarget = 0x4;
constexpr std::size_t kWindow = 0x8;
constexpr std::size_t kKeyShown = 0x9B6;
} // namespace map

constexpr uint32_t kEditIid = 0x231A1C57;
constexpr uint32_t kFileBrowserClass = 0x1AA52EA4;
constexpr uint32_t kFileBrowserIid = 0x3AA52E64;
constexpr uint32_t kUiGroup = 0x96A006B0;
constexpr uint32_t kTextGroup = 0x6A231EAA;

constexpr uint32_t kCommand = 3;
constexpr uint32_t kButtonClicked = 0x287259F6;
constexpr uint32_t kEndModal = 0xF;
constexpr uint32_t kFocusLost = 0x11;
constexpr uint32_t kEditReturn = 0x9B1552D5;

constexpr uint32_t kKeyEscape = 0x1B;
constexpr uint32_t kKeyUp = 0x26;
constexpr uint32_t kKeyDown = 0x28;
constexpr uint32_t kKeyF1 = 0x70;
constexpr uint32_t kKeyF4 = 0x73;

// The intro window's messages to itself.
constexpr uint32_t kFirstMovieDone = 0x8AAAFD43;
constexpr uint32_t kSecondMovieDone = 0x4A384F52;
constexpr uint32_t kFirstLogoDone = 0xEAAC1A96;
constexpr uint32_t kSecondLogoDone = 0xCAAC6A52;
constexpr uint32_t kSkipped = 0xEAAD620A;

constexpr uint32_t kLogoLayout = 0xAAA9C9D9;
constexpr uint32_t kLogoWindowA = 0xAAAAF3D1;
constexpr uint32_t kLogoWindowB = 0x8AA9AA14;

// The file dialog's buttons and its browser's message.
constexpr uint32_t kPictureOk = 0xBAA55D58;
constexpr uint32_t kPictureCancel = 0xBAA55D59;
constexpr uint32_t kBrowserChose = 0x7ABE7204;

// A window message as window procedures get it.
struct WinMessage {
    uint32_t type;
    uint32_t a;
    uint32_t b;
    uint32_t c;
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

struct StringVector {
    GameString* begin;
    GameString* end;
    GameString* capacity;
};

// A localized text: the string first (the constructor writes 0x20 bytes).
struct LocalizedText {
    GameString text;
    char rest[0xC];
};
static_assert(sizeof(LocalizedText) == 0x20);

struct ImageLoader {
    void* first;
    void* image;
    char rest[0x28];
};
static_assert(sizeof(ImageLoader) == 0x30);

struct ListNode {
    ListNode* next;
    ListNode* previous;
    void* value;
};

// What the candidate window is filled from.
struct Candidates {
    const uint16_t* const* texts;
    uint32_t count;
    uint32_t selected;
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

// --- The cheat box --------------------------------------------------------------------

GameString* HistoryBegin()
{
    return Global<GameString*>(addr::SC4CheatBox_History);
}

// The number of lines kept: the game's signed division of the byte length
// by 20.
int32_t HistoryCount()
{
    return static_cast<int32_t>(Global<char*>(addr::SC4CheatBox_History + 4) -
                                Global<char*>(addr::SC4CheatBox_History)) /
           20;
}

// The game's 0x0079BC30 and 0x0079BC40 (ret 8): QueryInterface on the parts
// at +0xD8 and +0x10C.
uint8_t __fastcall CheatQueryInterfaceGen(char* part, void*, uint32_t iid, void** out)
{
    return This<uint8_t>(addr::SC4UI_F79BAB0, part - cheat::kGen, iid, out);
}

uint8_t __fastcall CheatQueryInterfaceFilter(char* part, void*, uint32_t iid, void** out)
{
    return This<uint8_t>(addr::SC4UI_F79BAB0, part - cheat::kFilter, iid, out);
}

// The game's 0x0079BC50 and 0x0079BC60: AddRef and Release on the part at
// +0x10C, the window's own count.
uint32_t __fastcall CheatAddRefFilter(char* part, void*)
{
    return This<uint32_t>(addr::SC4Win_AddRefJump8AE960, part - cheat::kFilter);
}

uint32_t __fastcall CheatReleaseFilter(char* part, void*)
{
    return This<uint32_t>(addr::SC4Win_ReleaseJump8AE970, part - cheat::kFilter);
}

// The game's 0x0079BC70 (slot 3 of the part at +0x10C, ret 8): what the
// edit's window asks its key filter. A key going down (5) and event 8 go to
// the window's own slots 0x208 and 0x21C; the first argument is not used.
uint8_t __fastcall CheatFilter(char* part, void*, void*, const char* event)
{
    char* const self = part - cheat::kFilter;
    const uint32_t type = *reinterpret_cast<const uint32_t*>(event);
    if (type == 5) {
        const uint32_t modifiers = *reinterpret_cast<const uint32_t*>(event + 8);
        const uint32_t key = *reinterpret_cast<const uint32_t*>(event + 4);
        return Virtual<uint8_t>(self, 0x208, key, modifiers);
    }
    if (type == 8) {
        const uint32_t third = *reinterpret_cast<const uint16_t*>(event + 0xE);
        const int32_t second = *reinterpret_cast<const int16_t*>(event + 8);
        const int32_t first = *reinterpret_cast<const int16_t*>(event + 4);
        return Virtual<uint8_t>(self, 0x21C, first, second, third);
    }
    return 0;
}

// The game's 0x0079BCD0 (ret 4). `this` is the cheat box. Ends the modal
// run with `result` and gives the keyboard to the city view (0x00B43DC8)
// when there is one (the original ends in a jump to the manager's slot
// 0x94).
void __fastcall CheatClose(char* self, void*, uint32_t result)
{
    Virtual<void>(At<void*>(self, cheat::kManager), 0x24, kEndModal, static_cast<void*>(self), result);
    void* const view = Global<void*>(addr::SC4_Service43DC8);
    if (view)
        Virtual<void>(At<void*>(self, cheat::kManager), 0x94, view);
}

// The game's 0x0079BD00: the constructor. The place in the history starts
// after its last line.
char* __fastcall CheatConstruct(char* self, void*)
{
    This<void*>(addr::cGZWinGen_Construct, self, uint32_t{0});
    At<std::uintptr_t>(self, cheat::kFilter) = addr::SC4KeyFilter_BaseVtable;
    At<std::uintptr_t>(self, 0x0) = addr::SC4CheatBox_Vtable;
    At<std::uintptr_t>(self, cheat::kGen) = addr::SC4CheatBox_GenVtable;
    At<std::uintptr_t>(self, cheat::kFilter) = addr::SC4CheatBox_FilterVtable;
    At<void*>(self, cheat::kEdit) = nullptr;
    At<int32_t>(self, cheat::kPlace) = HistoryCount();
    At<uint32_t>(self, cheat::kSoundA) = 0;
    At<uint32_t>(self, cheat::kSoundB) = 0;
    return self;
}

// The game's 0x0079BD80 (slot 148, ret 4): the deleting destructor.
char* __fastcall CheatDelete(char* self, void*, uint32_t flags)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4CheatBox_Vtable;
    At<std::uintptr_t>(self, cheat::kGen) = addr::SC4CheatBox_GenVtable;
    At<std::uintptr_t>(self, cheat::kFilter) = addr::SC4CheatBox_FilterVtable;
    This<uint8_t>(addr::SC4UI_F79BAF0, self);
    This<void>(addr::cGZWinGen_Destruct, self);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// The game's 0x0079BDC0 (slot 4): Init. Gives the window a size when it has
// none, has the generic part make the edit box (its slot 0x34), limits the
// line to 0x17C1 characters, sets the font 0x68963C4C and a black text
// colour, puts the key filter on the edit's window, gives it the keyboard
// and sizes the window around it. The reference taken on the edit's window
// is given back only when it answers as a text edit, as in the original.
uint8_t __fastcall CheatInit(char* self, void*)
{
    if (This<uint8_t>(addr::GZWin_IsInitialized, self))
        return 1;
    if (Virtual<int32_t>(self, 0xA4) <= 0x10 || Virtual<int32_t>(self, 0xA8) <= 0x10)
        Virtual<void>(self, 0xDC, int32_t{8}, int32_t{8}, int32_t{0xF8}, int32_t{0x1C});
    This<uint8_t>(addr::cGZWinGen_Init, self);
    Virtual<void>(self, 0x190, uint32_t{1});
    This<uint8_t>(addr::GZWin_F99BA3E, self);
    This<uint8_t>(addr::GZWin_F99BA92, self, uint32_t{1});

    int32_t area[4] = {4, 6, 0x134, 0x1A};
    GameString text;
    InitString(text);
    void* const made = Virtual<void*>(self + cheat::kGen, 0x34, &area[0], &text);
    DestroyString(text);
    AddRef(made);
    void*& edit = At<void*>(self, cheat::kEdit);
    if (Virtual<uint8_t>(made, 0x0, kEditIid, &edit))
        Release(made);
    if (edit) {
        Virtual<void>(edit, 0x134, uint32_t{0x17C1});
        void* const styles = Cdecl<void*>(addr::SC4_Singleton913C72);
        if (void* const font = Virtual<void*>(styles, 0x14, uint32_t{0x68963C4C}))
            Virtual<void>(edit, 0x24, font);
        const uint32_t black = Virtual<uint32_t>(self, 0x1B0, uint32_t{0}, uint32_t{0}, uint32_t{0});
        Virtual<void>(edit, 0x30, uint32_t{0}, black);
        void* const window = Virtual<void*>(edit, 0x14);
        Virtual<void>(window, 0x1D0, static_cast<void*>(self + cheat::kFilter));
    }
    Virtual<void>(At<void*>(self, cheat::kManager), 0x94, made);
    Virtual<void>(self, 0xCC, area[2] - area[0] + 8);
    Virtual<void>(self, 0xD0, area[3] - area[1] + 8);
    const uint32_t colour = Virtual<uint32_t>(self, 0x1B0, uint32_t{0xFF}, uint32_t{0}, uint32_t{0xFF});
    Virtual<void>(self, 0x1A8, colour);
    return 1;
}

// The game's 0x0079BFC0 (slot 3, ret 4): a message of the window. Return in
// the edit box: the line goes to the cheat code service (slot 22) and, when
// that gives no code, to the command service as a command line (slot 19);
// an answer of 2 there with no code plays the second sound and leaves the
// box up. Otherwise the line is added to the history and the box closes
// with 1, or, with Control or Shift held, stays up with the edit emptied.
// An empty line closes with 0. With no cheat code service the box closes
// and false comes back. Message 0x11 gives the keyboard back to the edit's
// window and goes on to the base class with everything else.
uint8_t __fastcall CheatMessage(char* self, void*, WinMessage* message)
{
    if (message->type == kFocusLost) {
        void* const manager = At<void*>(self, cheat::kManager);
        void* const window = Virtual<void*>(At<void*>(self, cheat::kEdit), 0x14);
        Virtual<void>(manager, 0x94, window);
    } else if (message->type == kEditReturn) {
        GameString line;
        InitString(line);
        void* const cheats = Global<void*>(addr::SC4_CheatCodes);
        uint32_t code = 0;
        if (!cheats) {
            CheatClose(self, nullptr, 0);
            DestroyString(line);
            return 0;
        }
        Virtual<void>(At<void*>(self, cheat::kEdit), 0xB4, &line);
        if (line.body.end == line.body.begin) {
            CheatClose(self, nullptr, 0);
            DestroyString(line);
            return 1;
        }
        Virtual<uint8_t>(cheats, 0x58, &line, &code);
        if (code == 0) {
            void* const commands = Global<void*>(addr::SC4_Commands);
            const int32_t answer =
                Virtual<int32_t>(commands, 0x4C, static_cast<const char*>(line.body.begin),
                                 static_cast<uint32_t>(line.body.end - line.body.begin), uint32_t{0});
            if (answer == 2) {
                if (code == 0) {
                    This<uint8_t>(addr::SC4UI_F79BB70, self, uint32_t{1});
                    DestroyString(line);
                    return 1;
                }
            } else {
                code = 0xFFFFFFFF;
            }
        }
        void* const keyboard = At<void*>(self, cheat::kKeyboard);
        if (Virtual<uint8_t>(keyboard, 0xC, uint32_t{0x11}) ||
            Virtual<uint8_t>(At<void*>(self, cheat::kKeyboard), 0xC, uint32_t{0x10})) {
            This<void>(addr::SC4StringVector_PushBack, reinterpret_cast<void*>(addr::SC4CheatBox_History), &line);
            At<int32_t>(self, cheat::kPlace) = HistoryCount();
            Virtual<void>(At<void*>(self, cheat::kEdit), 0xC4, uint32_t{0}, uint32_t{0}, uint32_t{0});
            This<uint8_t>(addr::SC4UI_F79BB70, self, uint32_t{0});
        } else {
            This<void>(addr::SC4StringVector_PushBack, reinterpret_cast<void*>(addr::SC4CheatBox_History), &line);
            At<int32_t>(self, cheat::kPlace) = HistoryCount();
            This<uint8_t>(addr::SC4UI_F79BB70, self, uint32_t{0});
            CheatClose(self, nullptr, 1);
        }
        DestroyString(line);
        return 1;
    }
    return This<uint8_t>(addr::SC4Win_F99CCF0, self, message);
}

// The game's 0x0079C1D0. `this` is the cheat box. When the application says
// so (its slot 0x54) and there is a cheat code service: every cheat string
// (the service's slot 13, asked twice, first for the count), sorted, offered
// in a menu at the mouse; the one picked goes into the edit box with the
// caret at its end. The strings in the vector are destroyed through their
// slot 0x70.
uint8_t __fastcall CheatOffer(char* self, void*)
{
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_Service43C94), 0x54))
        return 0;
    void* const cheats = Global<void*>(addr::SC4_CheatCodes);
    if (!cheats)
        return 0;

    StringVector all = {nullptr, nullptr, nullptr};
    GameString picked;
    InitString(picked);
    uint32_t count = 0;
    count = Virtual<uint32_t>(cheats, 0x34, static_cast<void**>(nullptr), &count);
    void** const strings = Cdecl<void**>(addr::SC4_operator_new, count * 4);
    Virtual<uint32_t>(cheats, 0x34, strings, &count);
    for (uint32_t i = 0; i < count; ++i) {
        void* const string = strings[i];
        const uint32_t length = Virtual<uint32_t>(string, 0x1C);
        const char* const chars = Virtual<const char*>(string, 0x18);
        uint8_t tag;
        GameString copy;
        copy.body.begin = nullptr;
        copy.body.end = nullptr;
        copy.body.capacity = nullptr;
        This<void>(addr::SC4String_FromRange, &copy.body, chars, chars + length, static_cast<void*>(&tag));
        copy.vtable = addr::SC4String_Vtable;
        copy.zero = 0;
        This<void>(addr::SC4StringVector_PushBack, &all, &copy);
        DestroyString(copy);
    }
    Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(strings));
    Cdecl<void>(addr::SC4StringVector_Sort, all.begin, all.end);

    int32_t y;
    int32_t x;
    Virtual<uint8_t>(At<void*>(self, cheat::kManager), 0x88, At<void*>(self, cheat::kParent), &x, &y);
    const uint32_t offered = static_cast<uint32_t>(
        static_cast<int32_t>(reinterpret_cast<char*>(all.end) - reinterpret_cast<char*>(all.begin)) / 20);
    const uint32_t choice = Cdecl<uint32_t>(addr::SC4UI_PopupMenu, At<void*>(self, cheat::kParent), x, y, &all,
                                            &picked, uint32_t{0});
    if (choice < offered) {
        Virtual<void>(At<void*>(self, cheat::kEdit), 0xC0, &picked, uint32_t{0});
        Virtual<void>(At<void*>(self, cheat::kEdit), 0x108, static_cast<uint32_t>(picked.body.end - picked.body.begin));
    }
    DestroyString(picked);
    for (GameString* string = all.begin; string != all.end; ++string)
        Virtual<void>(string, 0x70, uint32_t{0});
    if (all.begin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(all.begin));
    return 1;
}

// The game's 0x0079C400 (slot 0x208, ret 8): a key going down. F1 offers
// the cheats; Escape and Alt+F4 close with 0; up and down walk the history
// (the line into the edit, the caret at its end), down past the last line
// empties the edit, and a step that cannot be made plays the second sound.
uint8_t __fastcall CheatKeyDown(char* self, void*, uint32_t key, uint32_t modifiers)
{
    if (key == kKeyF1) {
        CheatOffer(self, nullptr);
        return 1;
    }
    if (key != kKeyUp && key != kKeyDown) {
        if (key == kKeyEscape || (key == kKeyF4 && (modifiers & 4))) {
            CheatClose(self, nullptr, 0);
            return 1;
        }
        return 0;
    }
    if (Global<char*>(addr::SC4CheatBox_History) == Global<char*>(addr::SC4CheatBox_History + 4))
        return 1;
    if (key == kKeyUp) {
        const int32_t place = At<int32_t>(self, cheat::kPlace);
        if (place <= 0) {
            This<uint8_t>(addr::SC4UI_F79BB70, self, uint32_t{1});
            return 1;
        }
        void* const edit = At<void*>(self, cheat::kEdit);
        At<int32_t>(self, cheat::kPlace) = place - 1;
        Virtual<void>(edit, 0xC0, HistoryBegin() + (place - 1), uint32_t{0});
    } else {
        const uint32_t place = At<uint32_t>(self, cheat::kPlace);
        if (place >= static_cast<uint32_t>(HistoryCount())) {
            This<uint8_t>(addr::SC4UI_F79BB70, self, uint32_t{1});
            return 1;
        }
        const uint32_t next = place + 1;
        At<uint32_t>(self, cheat::kPlace) = next;
        const uint32_t count = static_cast<uint32_t>(HistoryCount());
        void* const edit = At<void*>(self, cheat::kEdit);
        if (next >= count) {
            Virtual<void>(edit, 0xC4, uint32_t{0}, uint32_t{0}, uint32_t{0});
            return 1;
        }
        Virtual<void>(edit, 0xC0, HistoryBegin() + next, uint32_t{0});
    }
    const GameString* const line = HistoryBegin() + At<uint32_t>(self, cheat::kPlace);
    Virtual<void>(At<void*>(self, cheat::kEdit), 0x108, static_cast<uint32_t>(line->body.end - line->body.begin));
    return 1;
}

// The game's 0x0079C550 (slot 0x21C, ret 0xC): event 8 of the key filter
// offers the cheats.
uint8_t __fastcall CheatEvent8(char* self, void*, int32_t, int32_t, uint32_t)
{
    CheatOffer(self, nullptr);
    return 1;
}

// --- The window the mouse passes through ----------------------------------------------

// The game's 0x0079C560: the constructor (the GZCOM factory 0x004661D0 ends
// in a jump to it).
char* __fastcall HollowConstruct(char* self, void*)
{
    This<void*>(addr::cGZWin_cGZWin, self);
    At<std::uintptr_t>(self, 0x0) = addr::SC4HollowWindow_Vtable;
    At<std::uintptr_t>(self, hollow::kGen) = addr::SC4HollowWindow_GenVtable;
    return self;
}

// The game's 0x0079C590 (slot 148, ret 4): the deleting destructor.
char* __fastcall HollowDelete(char* self, void*, uint32_t flags)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4HollowWindow_Vtable;
    At<std::uintptr_t>(self, hollow::kGen) = addr::SC4HollowWindow_GenVtable;
    This<void>(addr::cGZWin_Destroy, self);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// The game's 0x0079C5C0 (slot 0x1E4, ret 8): whether a point of the window
// is the window's. Only where a child that is shown (slot 0x11C) and has
// not the flag 0x200000 says the point is its own (slot 0xF8, after the
// window's slot 0xF0 has converted the point). The children are walked
// under the base class's lock (0x0099B94A, 0x0099D3D2).
uint8_t __fastcall HollowHit(char* self, void*, int32_t x, int32_t y)
{
    if (x < 0 || y < 0 || x >= At<int32_t>(self, hollow::kRight) - At<int32_t>(self, hollow::kLeft) ||
        y >= At<int32_t>(self, hollow::kBottom) - At<int32_t>(self, hollow::kTop))
        return 0;
    Virtual<void>(self, 0xF0, &x, &y);
    ListNode** const children = This<ListNode**>(addr::GZWin_LockChildren, self);
    uint8_t hit = 0;
    for (ListNode* node = (*children)->next; node != *children; node = node->next) {
        void* const child = node->value;
        if (Virtual<uint8_t>(child, 0x11C) && !Virtual<uint8_t>(child, 0x10C, uint32_t{0x200000}) &&
            Virtual<uint8_t>(child, 0xF8, x, y)) {
            hit = 1;
            break;
        }
    }
    This<void>(addr::GZWin_UnlockChildren, self, children);
    return hit;
}

// --- The candidate window -------------------------------------------------------------

// The game's 0x0079C6A0: the constructor.
char* __fastcall CandidatesConstruct(char* self, void*)
{
    This<void*>(addr::GZCandidateWindow_Construct, self);
    At<uint32_t>(self, candidates::kDark) = 0;
    At<uint32_t>(self, candidates::kWhite) = 0;
    At<std::uintptr_t>(self, 0x0) = addr::SC4CandidateWindow_Vtable;
    At<std::uintptr_t>(self, candidates::kWindow) = addr::SC4CandidateWindow_WindowVtable;
    At<std::uintptr_t>(self, candidates::kGen) = addr::SC4CandidateWindow_GenVtable;
    return self;
}

// The game's 0x0079C780 (ret 4): the deleting destructor.
char* __fastcall CandidatesDelete(char* self, void*, uint32_t flags)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4CandidateWindow_Vtable;
    At<std::uintptr_t>(self, candidates::kWindow) = addr::SC4CandidateWindow_WindowVtable;
    At<std::uintptr_t>(self, candidates::kGen) = addr::SC4CandidateWindow_GenVtable;
    This<void>(addr::GZCandidateWindow_Destruct, self);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// The game's 0x0079C730 (slot 148 of the window part, ret 4).
char* __fastcall CandidatesDeleteWindow(char* part, void*, uint32_t flags)
{
    return CandidatesDelete(part - candidates::kWindow, nullptr, flags);
}

// The game's 0x0079C800 (slot 4): Init. After the framework's own start the
// window gets the picture 0x144161C0 as its background and the list its
// looks: flags, margins, and six colours, three of them the dark blue
// (0x30, 0x3E, 0x59) and three 0x40000000.
uint8_t __fastcall CandidatesInit(char* self, void*)
{
    This<uint8_t>(addr::GZCandidateWindow_Init, self);
    char* const window = self + candidates::kWindow;
    Virtual<void>(window, 0x110, uint32_t{4}, uint32_t{1});
    ImageLoader loader;
    This<void*>(addr::SC4ImageLoader_ConstructParts, &loader, uint32_t{0x856DDBAC}, uint32_t{0x46A006B0},
                uint32_t{0x144161C0}, uint32_t{1}, uint32_t{0});
    Virtual<void>(self + candidates::kGen, 0x34, uint32_t{2}, loader.image);
    Virtual<void>(At<void*>(self, candidates::kList), 0x78, uint32_t{4}, uint32_t{1}, uint32_t{1});
    Virtual<void>(At<void*>(self, candidates::kList), 0x30, uint32_t{2});
    Virtual<void>(At<void*>(self, candidates::kList), 0x30, uint32_t{4});
    Virtual<void>(At<void*>(self, candidates::kList), 0x30, uint32_t{0x200});
    Virtual<void>(At<void*>(self, candidates::kList), 0x30, uint32_t{0x20});
    Virtual<void>(At<void*>(self, candidates::kList), 0x30, uint32_t{0x40});
    Virtual<void>(At<void*>(self, candidates::kList), 0x30, uint32_t{0x100});
    Virtual<void>(At<void*>(self, candidates::kList), 0x28, uint32_t{0x1000});
    Virtual<void>(At<void*>(self, candidates::kList), 0x30, uint32_t{1});
    Virtual<void>(At<void*>(self, candidates::kList), 0x28, uint32_t{0x80000});
    Virtual<void>(At<void*>(self, candidates::kList), 0x68, int32_t{4}, int32_t{4}, int32_t{4}, int32_t{8});
    At<uint32_t>(self, candidates::kDark) =
        Virtual<uint32_t>(window, 0x1B0, uint32_t{0x30}, uint32_t{0x3E}, uint32_t{0x59});
    At<uint32_t>(self, candidates::kWhite) =
        Virtual<uint32_t>(window, 0x1B0, uint32_t{0xFF}, uint32_t{0xFF}, uint32_t{0xFF});
    Virtual<void>(At<void*>(self, candidates::kList), 0x44, At<uint32_t>(self, candidates::kDark), uint32_t{0});
    Virtual<void>(At<void*>(self, candidates::kList), 0x44, uint32_t{0x40000000}, uint32_t{1});
    Virtual<void>(At<void*>(self, candidates::kList), 0x44, At<uint32_t>(self, candidates::kDark), uint32_t{2});
    Virtual<void>(At<void*>(self, candidates::kList), 0x44, uint32_t{0x40000000}, uint32_t{3});
    Virtual<void>(At<void*>(self, candidates::kList), 0x44, At<uint32_t>(self, candidates::kDark), uint32_t{4});
    Virtual<void>(At<void*>(self, candidates::kList), 0x44, uint32_t{0x40000000}, uint32_t{5});
    This<void>(addr::SC4ImageLoader_Destruct, &loader);
    return 1;
}

// The game's 0x0079CA40 (slot 4 of the window part).
uint8_t __fastcall CandidatesInitWindow(char* part, void*)
{
    return CandidatesInit(part - candidates::kWindow, nullptr);
}

// The game's 0x0079CA90 (slot 17, ret 4): the list filled. A row for each
// candidate: its number within the page of nine in column 0, the text
// (16-bit characters, converted with encoding 0x10) in column 2, both in
// white. The columns are as wide as their widest text, the list as high as
// nine rows at most, the window 0x1A wider and 0x18 higher than the list,
// and pulled back inside its parent when it sticks out.
void __fastcall CandidatesFill(char* self, void*, const Candidates* shown)
{
    void* const font0 = At<void*>(self, candidates::kFont);
    int32_t lineHeight = Virtual<int32_t>(font0, 0x94, uint32_t{0}, uint32_t{0});
    int32_t area[4];
    Virtual<void>(At<void*>(self, candidates::kList), 0xC8, &area[0]);
    if (area[3] - area[1] != 0)
        Virtual<void>(At<void*>(self, candidates::kList), 0x194, uint32_t{0}, area[3] - area[1], uint32_t{3});
    Virtual<void>(At<void*>(self, candidates::kList), 0x108, lineHeight);
    Virtual<void>(At<void*>(self, candidates::kList), 0x104, uint32_t{1}, uint32_t{1}, int32_t{0});
    Virtual<void>(At<void*>(self, candidates::kList), 0x104, uint32_t{3}, uint32_t{1}, int32_t{2});

    int32_t numberWidth = 0x11;
    int32_t textWidth = 0x12;
    for (uint32_t i = 0; i < shown->count; ++i) {
        GameString text;
        InitString(text);
        Cdecl<void>(addr::cRZBaseString_Sprintf, &text, "%d", static_cast<int32_t>(i) % 9 + 1);
        Virtual<void>(At<void*>(self, candidates::kList), 0x1C0, uint32_t{0}, i, text.body.begin,
                      static_cast<uint32_t>(text.body.end - text.body.begin), uint32_t{1}, uint32_t{0});
        int32_t width = Virtual<int32_t>(At<void*>(self, candidates::kFont), 0xC0, text.body.begin,
                                         static_cast<uint32_t>(text.body.end - text.body.begin)) +
                        2;
        if (numberWidth < width)
            numberWidth = width;

        // A copy of the candidate with its end, as the game's inlined wide
        // string makes it.
        const uint16_t* const wide = shown->texts[i];
        uint32_t length = 0;
        if (wide[0] != 0) {
            do {
                ++length;
            } while (wide[length] != 0);
        }
        const uint32_t bytes = length * 2;
        const uint32_t room = static_cast<uint32_t>(static_cast<int32_t>(bytes) >> 1) + 1;
        uint16_t* copy = nullptr;
        if (room > 0x7FFFFFFF || room == 0)
            Cdecl<void>(addr::STL_ThrowLengthError, "basic_string");
        else
            copy = Cdecl<uint16_t*>(addr::SC4_stl_allocate, room * 2);
        uint16_t* end = copy;
        if (length != 0)
            end = reinterpret_cast<uint16_t*>(
                Cdecl<char*>(addr::SC4_memmove, static_cast<void*>(copy), static_cast<const void*>(wide), bytes) +
                bytes);
        *end = 0;
        Cdecl<void>(addr::SC4String_FromWide, copy, static_cast<uint32_t>(end - copy), uint32_t{0x10}, &text);
        Virtual<void>(At<void*>(self, candidates::kList), 0x1C0, uint32_t{2}, i, text.body.begin,
                      static_cast<uint32_t>(text.body.end - text.body.begin), uint32_t{1}, uint32_t{0});
        width = Virtual<int32_t>(At<void*>(self, candidates::kFont), 0xC0, text.body.begin,
                                 static_cast<uint32_t>(text.body.end - text.body.begin)) +
                2;
        if (textWidth < width)
            textWidth = width;
        Virtual<void>(At<void*>(self, candidates::kList), 0x16C, uint32_t{0}, i, uint32_t{4},
                      At<uint32_t>(self, candidates::kWhite));
        Virtual<void>(At<void*>(self, candidates::kList), 0x16C, uint32_t{2}, i, uint32_t{5},
                      At<uint32_t>(self, candidates::kWhite));
        if (copy)
            Cdecl<void>(addr::SC4_Free, static_cast<void*>(copy));
        DestroyString(text);
    }

    numberWidth += 4;
    textWidth += 4;
    const int32_t pad = lineHeight / 8;
    const uint32_t rows = shown->count > 9 ? 9 : shown->count;
    const int32_t textColumn = textWidth + pad * 2;
    const int32_t listWidth = textColumn + numberWidth + 2;
    int32_t height = (lineHeight + pad * 2) * static_cast<int32_t>(rows);
    Virtual<void>(At<void*>(self, candidates::kList), 0x68, pad, pad, pad, pad);
    Virtual<void>(At<void*>(self, candidates::kList), 0x104, uint32_t{0}, uint32_t{1}, numberWidth + pad * 2);
    Virtual<void>(At<void*>(self, candidates::kList), 0x104, uint32_t{2}, uint32_t{1}, textColumn);
    Virtual<void>(Virtual<void*>(At<void*>(self, candidates::kList), 0x14), 0xD4, listWidth, height);
    Virtual<void>(Virtual<void*>(At<void*>(self, candidates::kList), 0x14), 0xE0, int32_t{0xC}, int32_t{0xC});
    Virtual<void>(At<void*>(self, candidates::kList), 0xAC, uint32_t{0}, shown->selected);
    Virtual<void>(At<void*>(self, candidates::kList), 0x1A4, uint32_t{0}, shown->selected);

    char* const window = self + candidates::kWindow;
    height += 0x18;
    Virtual<void>(window, 0xD4, listWidth + 0x1A, height);
    int32_t parentHeight = Virtual<int32_t>(At<void*>(self, candidates::kParent), 0xA8);
    int32_t parentWidth = Virtual<int32_t>(At<void*>(self, candidates::kParent), 0xA4);
    const int32_t* const place = Virtual<const int32_t*>(window, 0xC0);
    int32_t left = place[0];
    int32_t top = place[1];
    int32_t right = place[2];
    int32_t bottom = place[3];
    bool moved = false;
    if (left <= 0) {
        left = 1;
        moved = true;
    }
    if (top <= 0) {
        top = 1;
        moved = true;
    }
    if (right >= parentWidth) {
        parentWidth = parentWidth - right - 1;
        left += parentWidth;
        right += parentWidth;
        moved = true;
    }
    if (bottom >= parentHeight) {
        parentHeight = parentHeight - bottom - 1;
        top += parentHeight;
        bottom += parentHeight;
        moved = true;
    }
    if (moved)
        Virtual<void>(window, 0xDC, left, top, right, bottom);
    Virtual<void>(window, 0x170);
}

// --- The intro window -----------------------------------------------------------------

// The game's 0x0079CED0: the constructor. The window's flag 0x10000 is
// cleared.
char* __fastcall IntroConstruct(char* self, void*)
{
    This<void*>(addr::GZWin_Construct, self);
    At<std::uintptr_t>(self, 0x0) = addr::SC4IntroWindow_Vtable;
    At<void*>(self, intro::kMovie) = nullptr;
    At<void*>(self, intro::kLogoA) = nullptr;
    At<void*>(self, intro::kLogoB) = nullptr;
    This<void*>(addr::SC4Stopwatch_Construct, self + intro::kWatch, int32_t{4});
    At<uint32_t>(self, intro::kPaints) = 0;
    This<void>(addr::GZWin_SetFlagDirect, self, uint32_t{0x10000}, uint32_t{0});
    return self;
}

// The game's 0x0079CF20 (slot 0, ret 8): interfaces 1 and 0x2A3832AA give
// the window; the rest is the base class's.
uint8_t __fastcall IntroQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    if (iid != 1 && iid != 0x2A3832AA)
        return This<uint8_t>(addr::SC4Win_QueryInterface, self, iid, out);
    *out = self;
    AddRef(self);
    return 1;
}

// The game's 0x0079CF50 (slot 5): Shutdown. The two logos and the movie
// window are taken out of the window (slot 0x40) and let go.
uint8_t __fastcall IntroShutdown(char* self, void*)
{
    if (This<uint8_t>(addr::GZWin_IsInitialized, self)) {
        for (const std::size_t field : {intro::kLogoA, intro::kLogoB, intro::kMovie}) {
            if (void* const child = At<void*>(self, field)) {
                Virtual<void>(self, 0x40, child);
                Release(At<void*>(self, field));
                At<void*>(self, field) = nullptr;
            }
        }
        This<uint8_t>(addr::SC4Win_F99D2FE, self);
    }
    return 1;
}

// The game's 0x0079CFE0 (slot 4): Init. The window covers the main window;
// a movie window of 768 by 384 is made in its middle, given the third of
// the three movie keys at 0x00B0DB48 and told to send 0x8AAAFD43 here when
// it is done; its byte +0x194 takes the settings' byte +0x69. A failed
// allocation is not checked, as in the original.
uint8_t __fastcall IntroInit(char* self, void*)
{
    if (This<uint8_t>(addr::GZWin_IsInitialized, self))
        return 1;
    This<uint8_t>(addr::GZWin_Init, self);
    void* const mainForHeight = Virtual<void*>(self, 0x28);
    void* const mainForWidth = Virtual<void*>(self, 0x28);
    const int32_t mainHeight = Virtual<int32_t>(mainForHeight, 0xA8);
    const int32_t mainWidth = Virtual<int32_t>(mainForWidth, 0xA4);
    Virtual<void>(self, 0xDC, int32_t{0}, int32_t{0}, mainWidth, mainHeight);

    char* const memory = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0x228});
    void* const movie = memory ? This<void*>(addr::SC4MovieWindow_Construct, memory, uint32_t{1}) : nullptr;
    At<void*>(self, intro::kMovie) = movie;
    AddRef(movie);
    Virtual<void>(At<void*>(self, intro::kMovie), 0xDC, int32_t{0}, int32_t{0}, int32_t{0x300}, int32_t{0x180});
    {
        void* const target = At<void*>(self, intro::kMovie);
        const int32_t y = (Virtual<int32_t>(self, 0xA8) - 0x180) / 2;
        const int32_t x = (Virtual<int32_t>(self, 0xA4) - 0x300) / 2;
        Virtual<void>(target, 0xE0, x, y);
    }
    This<uint8_t>(addr::SC4MovieWindow_Play, At<void*>(self, intro::kMovie),
                  reinterpret_cast<const uint32_t*>(addr::SC4IntroWindow_MovieKeys + 0x18), uint32_t{3});
    This<void>(addr::SC4MovieWindow_SetDoneMessage, At<void*>(self, intro::kMovie), static_cast<void*>(self),
               kFirstMovieDone);
    const char* const settings = Virtual<const char*>(Global<void*>(addr::SC4_Service43C94), 0x98);
    const uint8_t flag = *reinterpret_cast<const uint8_t*>(settings + 0x69);
    At<uint8_t>(At<void*>(self, intro::kMovie), 0x194) = flag != 0;
    Virtual<void>(self, 0x38, At<void*>(self, intro::kMovie));
    return 1;
}

// The two logos are made alike (the game's 0x0079D110 and 0x0079D200): the
// window loaded from the layout as a child, put in the middle (not left of
// or above the window's corner), shown (slots 0x5C and 0x118), its flag
// 0x20 set and its slot 0x1B4 given two numbers.
uint8_t MakeLogo(char* self, std::size_t field, uint32_t instance, uint32_t first, uint32_t second)
{
    uint32_t key[3] = {0, kUiGroup, instance};
    void* const logo = Cdecl<void*>(addr::SC4UI_LoadWindowChild, &key[0], static_cast<void*>(self), kLogoLayout);
    At<void*>(self, field) = logo;
    if (!logo)
        return 0;
    const int32_t logoWidth = Virtual<int32_t>(logo, 0xA4);
    int32_t x = (Virtual<int32_t>(self, 0xA4) - logoWidth) / 2;
    const int32_t logoHeight = Virtual<int32_t>(At<void*>(self, field), 0xA8);
    int32_t y = (Virtual<int32_t>(self, 0xA8) - logoHeight) / 2;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    Virtual<void>(At<void*>(self, field), 0xE0, x, y);
    Virtual<void>(At<void*>(self, field), 0x5C);
    Virtual<void>(At<void*>(self, field), 0x118);
    Virtual<void>(At<void*>(self, field), 0x110, uint32_t{0x20}, uint32_t{1});
    Virtual<void>(At<void*>(self, field), 0x1B4, first, second);
    return 1;
}

// The game's 0x0079D110. `this` is the intro window: the first logo
// (0xAAAAF3D1, numbers 1000 and 1000).
uint8_t __fastcall IntroMakeLogoA(char* self, void*)
{
    return MakeLogo(self, intro::kLogoA, kLogoWindowA, 1000, 1000);
}

// The game's 0x0079D200: the second logo (0x8AA9AA14, numbers 2000 and 0).
uint8_t __fastcall IntroMakeLogoB(char* self, void*)
{
    return MakeLogo(self, intro::kLogoB, kLogoWindowB, 2000, 0);
}

// The game's 0x0079D2F0 (slot 3, ret 4): a message of the window. The
// steps of the intro, each posted by the paint (0x0079D490) or by the
// movie window:
//   0x8AAAFD43 the first movie is done: both logos made, the first brought
//     up (slot 0x114), the stopwatch started;
//   0xEAAC1A96 the first logo is gone: the movie window plays the first key
//     and is to send 0x4A384F52;
//   0x4A384F52 the second movie is done: the second logo shown, the
//     stopwatch started afresh, and the base class sees the message too;
//   0xCAAC6A52 the second logo has had its time: the main window drops this
//     screen and makes the next (0x004E0D40);
//   0xEAAD620A the player skipped: the main window drops this screen and
//     makes another (0x004E0CB0).
uint8_t __fastcall IntroMessage(char* self, void*, WinMessage* message)
{
    switch (message->type) {
    case kFirstMovieDone:
        IntroMakeLogoA(self, nullptr);
        IntroMakeLogoB(self, nullptr);
        Virtual<void>(At<void*>(self, intro::kLogoA), 0x114);
        This<void>(addr::SC4Stopwatch_Start, self + intro::kWatch);
        return 1;
    case kSecondMovieDone:
        Virtual<void>(At<void*>(self, intro::kLogoB), 0x5C);
        Virtual<void>(At<void*>(self, intro::kLogoB), 0x114);
        This<void>(addr::SC4Stopwatch_Reset, self + intro::kWatch);
        This<void>(addr::SC4Stopwatch_Start, self + intro::kWatch);
        break;
    case kSecondLogoDone:
        This<uint8_t>(addr::SC4MainWindow_DropScreen, Global<void*>(addr::SC4_MainWindowObject));
        This<uint8_t>(addr::SC4MainWindow_ShowScreen200, Global<void*>(addr::SC4_MainWindowObject));
        return 1;
    case kFirstLogoDone:
        This<uint8_t>(addr::SC4MovieWindow_Play, At<void*>(self, intro::kMovie),
                      reinterpret_cast<const uint32_t*>(addr::SC4IntroWindow_MovieKeys), uint32_t{0});
        This<void>(addr::SC4MovieWindow_SetDoneMessage, At<void*>(self, intro::kMovie), static_cast<void*>(self),
                   kSecondMovieDone);
        return 1;
    case kSkipped:
        This<uint8_t>(addr::SC4MainWindow_DropScreen, Global<void*>(addr::SC4_MainWindowObject));
        This<uint8_t>(addr::SC4MainWindow_ShowScreen118, Global<void*>(addr::SC4_MainWindowObject));
        return 1;
    default:
        break;
    }
    return This<uint8_t>(addr::SC4Win_F99CCF0, self, message);
}

// The game's 0x0079D400. `this` is the intro window. The player wants to
// skip: while there is no first logo, and the movie window was playing and
// has stopped (0x008103E0), the window posts itself 0xEAAD620A.
void __fastcall IntroSkip(char* self, void*)
{
    if (At<void*>(self, intro::kLogoA))
        return;
    void* const movie = At<void*>(self, intro::kMovie);
    if (!movie || !This<uint8_t>(addr::SC4MovieWindow_StopIfPlaying, movie))
        return;
    void* const poster = Cdecl<void*>(addr::SC4_Singleton913B97);
    WinMessage message = {kSkipped, 0, 0, 0};
    Virtual<void>(poster, 0x1C, &message, static_cast<void*>(self));
}

// The game's 0x0079D460 (slot 0x218, ret 0xC): a mouse button skips. False
// always.
uint8_t __fastcall IntroMouseDown(char* self, void*, int32_t, int32_t, uint32_t)
{
    IntroSkip(self, nullptr);
    return 0;
}

// The game's 0x0079D470 (slot 0x208, ret 8): Escape skips. False always.
uint8_t __fastcall IntroKeyDown(char* self, void*, uint32_t key, uint32_t)
{
    if (key == kKeyEscape)
        IntroSkip(self, nullptr);
    return 0;
}

// The game's 0x0079D610 (slot 148, ret 4): the deleting destructor.
char* __fastcall IntroDelete(char* self, void*, uint32_t flags)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4IntroWindow_Vtable;
    IntroShutdown(self, nullptr);
    This<void>(addr::SC4Stopwatch_Destruct, self + intro::kWatch);
    This<void>(addr::GZWin_Destruct, self);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// --- The picture file dialog ----------------------------------------------------------

// The game's 0x0079D650 (ret 4). `this` is the dialog: what the browser's
// slot 0x48 says about the file chosen, written through `out`; false with
// no browser (the original ends in a jump to the slot).
uint8_t __fastcall PictureChosen(char* self, void*, void* out)
{
    void* const browser = At<void*>(self, picture::kBrowser);
    if (!browser)
        return 0;
    return Virtual<uint8_t>(browser, 0x48, out);
}

// The game's 0x0079D670 (slot 5): Shutdown. The browser's path is read into
// the string (its slot 0x40) before the browser is let go. The base class's
// shutdown is not called, as in the original.
uint8_t __fastcall PictureShutdown(char* self, void*)
{
    if (void* const browser = At<void*>(self, picture::kBrowser)) {
        Virtual<void>(browser, 0x40, static_cast<void*>(self + picture::kPath));
        Release(At<void*>(self, picture::kBrowser));
        At<void*>(self, picture::kBrowser) = nullptr;
    }
    return 1;
}

// The game's 0x0079D700 (slot 0x208, ret 8): Escape ends the modal run with
// 0; any other key gets the framework's "no" (0x0093877E).
uint8_t __fastcall PictureKeyDown(char* self, void*, uint32_t key, uint32_t modifiers)
{
    if (key == kKeyEscape) {
        void* const manager = Virtual<void*>(self, 0x18);
        return Virtual<uint8_t>(manager, 0x24, kEndModal, static_cast<void*>(self), uint32_t{0});
    }
    return This<uint8_t>(addr::GZWin_ReturnFalse8, self, key, modifiers);
}

// The game's 0x0079D740 (slot 3, ret 4): a message of the window. OK
// (0xBAA55D58) and the browser's own message 0x7ABE7204 end the modal run
// with 1 when the browser's choice is of kind 2; cancel (0xBAA55D59) ends
// it with 0. The browser writes its answer into 0x14 bytes of the frame.
uint8_t __fastcall PictureMessage(char* self, void*, WinMessage* message)
{
    uint32_t choice[5];
    bool accept = false;
    if (message->type == kCommand) {
        if (message->a == kButtonClicked) {
            if (message->b == kPictureOk && At<void*>(self, picture::kBrowser)) {
                Virtual<uint8_t>(At<void*>(self, picture::kBrowser), 0x48, &choice[0]);
                accept = choice[0] == 2;
            }
            if (!accept && message->b == kPictureCancel && At<void*>(self, picture::kBrowser)) {
                void* const manager = Virtual<void*>(self, 0x18);
                return Virtual<uint8_t>(manager, 0x24, kEndModal, static_cast<void*>(self), uint32_t{0});
            }
        }
    } else if (message->type == kBrowserChose) {
        if (At<void*>(self, picture::kBrowser)) {
            Virtual<uint8_t>(At<void*>(self, picture::kBrowser), 0x48, &choice[0]);
            accept = choice[0] == 2;
        }
        if (!accept)
            return 1;
    }
    if (accept) {
        void* const manager = Virtual<void*>(self, 0x18);
        return Virtual<uint8_t>(manager, 0x24, kEndModal, static_cast<void*>(self), uint32_t{1});
    }
    return This<uint8_t>(addr::SC4Win_F99CCF0, self, message);
}

// The game's 0x0079D810: the constructor. The path starts as an empty
// string (made in place).
char* __fastcall PictureConstruct(char* self, void*)
{
    This<void*>(addr::GZWin_Construct, self);
    At<std::uintptr_t>(self, 0x0) = addr::SC4PictureDialog_Vtable;
    At<void*>(self, picture::kBrowser) = nullptr;
    GameString& path = At<GameString>(self, picture::kPath);
    path.body.begin = nullptr;
    path.body.end = nullptr;
    path.body.capacity = nullptr;
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    path.body.begin = buffer;
    path.body.end = buffer;
    path.body.capacity = buffer + 8;
    *buffer = 0;
    path.zero = 0;
    path.vtable = addr::SC4String_Vtable;
    At<uint32_t>(self, picture::kId) = 0xEA9660F3;
    return self;
}

// The game's 0x0079D880 (slot 148, ret 4): the deleting destructor.
char* __fastcall PictureDelete(char* self, void*, uint32_t flags)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4PictureDialog_Vtable;
    DestroyString(At<GameString>(self, picture::kPath));
    This<void>(addr::GZWin_Destruct, self);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// The game's 0x0079D8D0 (slot 4): Init. The window is 0x194 by 0x10C at
// (0x14, 0x14); a file browser (GZCOM class 0x1AA52EA4) fills it but for 8
// pixels at the left and top, takes the six picture file patterns (its slot
// 0x54), the title text 0x4A9F1428 and the dialog's path, and becomes the
// window's child 0x9AEDEF7C. False when the browser cannot be made.
uint8_t __fastcall PictureInit(char* self, void*)
{
    if (This<uint8_t>(addr::GZWin_IsInitialized, self))
        return 1;
    This<uint8_t>(addr::GZWin_Init, self);
    Virtual<void>(self, 0xDC, int32_t{0x14}, int32_t{0x14}, int32_t{0x1A8}, int32_t{0x120});
    Virtual<void>(self, 0x190, uint32_t{1});
    Virtual<void>(self, 0x1AC, uint32_t{0x19}, uint32_t{3}, uint32_t{0xDC});
    void* const com = Cdecl<void*>(addr::RZGetCOM);
    void*& browser = At<void*>(self, picture::kBrowser);
    if (!Virtual<uint8_t>(com, 0xC, kFileBrowserClass, kFileBrowserIid, &browser))
        return 0;

    void* const window = Virtual<void*>(browser, 0xC);
    AddRef(window);
    Virtual<void>(window, 0x100, uint32_t{0x9AEDEF7C});
    {
        const int32_t height = Virtual<int32_t>(self, 0xA8) - 8;
        const int32_t width = Virtual<int32_t>(self, 0xA4) - 8;
        Virtual<void>(window, 0xDC, int32_t{8}, int32_t{8}, width, height);
    }
    Virtual<void>(browser, 0x124, uint32_t{4}, uint32_t{4}, uint32_t{4});
    static const char* const kPatterns[] = {"*.bmp", "*.png", "*.gif", "*.jpg", "*.jpeg", "*.tga"};
    for (const char* const pattern : kPatterns) {
        GameString text;
        void* const made = This<void*>(addr::SC4String_FromText, &text, pattern);
        Virtual<void>(browser, 0x54, made, uint32_t{0}, uint32_t{0}, uint32_t{0});
        DestroyString(text);
    }
    LocalizedText title;
    This<void*>(addr::SC4LocalizedString_SC4LocalizedString, &title, kTextGroup, uint32_t{0x4A9F1428}, uint32_t{1});
    Virtual<void>(browser, 0x58, &title, uint32_t{0});
    Virtual<void>(browser, 0x68, uint32_t{0});
    Virtual<void>(browser, 0x3C, static_cast<void*>(self + picture::kPath));
    Virtual<void>(self, 0x38, window);
    Release(window);
    DestroyString(title.text);
    return 1;
}

// The game's 0x0079DB60 (ret 4). `this` is the dialog: the path, read from
// the browser when there is one (its slot 0x40), copied into `out`. The
// allocator tag is the address of the argument, as in the original.
void __fastcall PictureGetPath(char* self, void*, GameString* out)
{
    if (void* const browser = At<void*>(self, picture::kBrowser))
        Virtual<void>(browser, 0x40, static_cast<void*>(self + picture::kPath));
    StringBody* const from = &At<GameString>(self, picture::kPath).body;
    StringBody* const to = &out->body;
    if (from != to)
        This<void>(addr::SC4String_AssignRange, to, from->begin, from->end, static_cast<void*>(&out));
}

// The game's 0x0079DBB0 (ret 4): the path set and handed to the browser
// (its slot 0x44) when there is one. A null string is read all the same, as
// in the original.
void __fastcall PictureSetPath(char* self, void*, const GameString* path)
{
    char* const own = self + picture::kPath;
    const StringBody* const from = path ? &path->body : nullptr;
    StringBody* const to = &reinterpret_cast<GameString*>(own)->body;
    if (from != to)
        This<void>(addr::SC4String_AssignRange, to, from->begin, from->end, static_cast<void*>(&path));
    if (void* const browser = At<void*>(self, picture::kBrowser))
        Virtual<void>(browser, 0x44, static_cast<void*>(own));
}

// --- The map window's small methods ---------------------------------------------------

// The game's 0x0079DCE0 (slot 0 of the main vtable, ret 8): 0x67C5BEDE
// gives the object, 0x452294AA the message target at +4 (null for a null
// object, whose AddRef is called all the same, as in the original), and
// everything else is asked of the framework window at +8.
uint8_t __fastcall MapQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    if (iid == 0x452294AA) {
        *out = self ? self + map::kTarget : nullptr;
        AddRef(self);
        return 1;
    }
    if (iid == 0x67C5BEDE) {
        *out = self;
        AddRef(self);
        return 1;
    }
    return This<uint8_t>(addr::cGZWin_QueryInterface, self + map::kWindow, iid, out);
}

// The game's 0x0079DD40 and 0x0079DD50 (slots 1 and 2): the framework
// window's count.
uint32_t __fastcall MapAddRef(char* self, void*)
{
    return This<uint32_t>(addr::SC4Win_AddRefJump8FB729, self + map::kWindow);
}

uint32_t __fastcall MapRelease(char* self, void*)
{
    return This<uint32_t>(addr::SC4Win_ReleaseJump8FB72E, self + map::kWindow);
}

// The game's 0x0079DE40 (slot 0x11C of the window part): whether the map
// window is shown, which is whether its parent's child 0x8A2871B2 has the
// flag 1. `this` is the window part (object +8).
uint8_t __fastcall MapIsShown(char* window, void*)
{
    void* const parent = Virtual<void*>(window, 0x2C);
    if (!parent)
        return 0;
    void* const child = Virtual<void*>(parent, 0x8C, uint32_t{0x8A2871B2});
    if (!child)
        return 0;
    return Virtual<uint8_t>(child, 0x10C, uint32_t{1});
}

struct Shown {
    uint32_t id;
    uint32_t show;
};

// The parent of the map's window part gets each child shown or hidden.
template <std::size_t N>
void ShowRows(char* self, const Shown (&rows)[N])
{
    void* const parent = Virtual<void*>(self + map::kWindow, 0x2C);
    for (const Shown& row : rows)
        Cdecl<uint8_t>(addr::SC4Win_ShowChild, parent, row.id, row.show, uint32_t{0});
}

// The game's 0x0079DE70. `this` is the map window. The children of the
// first arrangement; 0x8A2871C4 shows by the byte at +0x9B6.
void __fastcall MapShowRowsA(char* self, void*)
{
    void* const parent = Virtual<void*>(self + map::kWindow, 0x2C);
    static const Shown kRows[] = {
        {0xAA32BCD4, 1}, {0x8A2871B2, 1}, {0x8A2871C3, 1}, {0x8A2871B1, 0},
        {0xAA32BCD5, 0}, {0x8100, 0},     {0x8101, 0},
    };
    for (const Shown& row : kRows)
        Cdecl<uint8_t>(addr::SC4Win_ShowChild, parent, row.id, row.show, uint32_t{0});
    Cdecl<uint8_t>(addr::SC4Win_ShowChild, parent, uint32_t{0x8A2871C4},
                   static_cast<uint32_t>(At<uint8_t>(self, map::kKeyShown)), uint32_t{0});
}

// The game's 0x0079DF10: the second arrangement, with 0xCB72F247, 0x8100
// and 0x8101 hidden.
void __fastcall MapShowRowsB(char* self, void*)
{
    static const Shown kRows[] = {
        {0xAA32BCD5, 1}, {0x8A2871B2, 1}, {0x8A2871C3, 1}, {0x8A2871B1, 0}, {0xAA32BCD4, 0},
        {0x8A2871C4, 0}, {0xCB72F247, 0}, {0x8100, 0},     {0x8101, 0},
    };
    ShowRows(self, kRows);
}

// The game's 0x0079DFB0: the second arrangement with those three shown.
void __fastcall MapShowRowsC(char* self, void*)
{
    static const Shown kRows[] = {
        {0xAA32BCD5, 1}, {0x8A2871B2, 1}, {0x8A2871C3, 1}, {0x8A2871B1, 0}, {0xAA32BCD4, 0},
        {0x8A2871C4, 0}, {0xCB72F247, 1}, {0x8100, 1},     {0x8101, 1},
    };
    ShowRows(self, kRows);
}

// The game's 0x0079E050: only 0x8A2871B1 and 0x8A2871B2 shown.
void __fastcall MapShowRowsD(char* self, void*)
{
    static const Shown kRows[] = {
        {0x8A2871B1, 1}, {0x8A2871B2, 1}, {0xAA32BCD4, 0}, {0xAA32BCD5, 0}, {0x8A2871C3, 0}, {0x8A2871C4, 0},
    };
    ShowRows(self, kRows);
}

bool InstallPanels7(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[52] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F79BC30, reinterpret_cast<void*>(&CheatQueryInterfaceGen)},
        {addr::SC4UI_F79BC40, reinterpret_cast<void*>(&CheatQueryInterfaceFilter)},
        {addr::SC4UI_F79BC50, reinterpret_cast<void*>(&CheatAddRefFilter)},
        {addr::SC4UI_F79BC60, reinterpret_cast<void*>(&CheatReleaseFilter)},
        {addr::SC4UI_F79BC70, reinterpret_cast<void*>(&CheatFilter)},
        {addr::SC4UI_F79BCD0, reinterpret_cast<void*>(&CheatClose)},
        {addr::SC4UI_F79BD00, reinterpret_cast<void*>(&CheatConstruct)},
        {addr::SC4UI_F79BD80, reinterpret_cast<void*>(&CheatDelete)},
        {addr::SC4UI_F79BDC0, reinterpret_cast<void*>(&CheatInit)},
        {addr::SC4UI_F79BFC0, reinterpret_cast<void*>(&CheatMessage)},
        {addr::SC4UI_F79C1D0, reinterpret_cast<void*>(&CheatOffer)},
        {addr::SC4UI_F79C400, reinterpret_cast<void*>(&CheatKeyDown)},
        {addr::SC4UI_F79C550, reinterpret_cast<void*>(&CheatEvent8)},
        {addr::SC4UI_F79C560, reinterpret_cast<void*>(&HollowConstruct)},
        {addr::SC4UI_F79C590, reinterpret_cast<void*>(&HollowDelete)},
        {addr::SC4UI_F79C5C0, reinterpret_cast<void*>(&HollowHit)},
        {addr::SC4UI_F79C6A0, reinterpret_cast<void*>(&CandidatesConstruct)},
        {addr::SC4UI_F79C730, reinterpret_cast<void*>(&CandidatesDeleteWindow)},
        {addr::SC4UI_F79C780, reinterpret_cast<void*>(&CandidatesDelete)},
        {addr::SC4UI_F79C800, reinterpret_cast<void*>(&CandidatesInit)},
        {addr::SC4UI_F79CA40, reinterpret_cast<void*>(&CandidatesInitWindow)},
        {addr::SC4UI_F79CA90, reinterpret_cast<void*>(&CandidatesFill)},
        {addr::SC4UI_F79CED0, reinterpret_cast<void*>(&IntroConstruct)},
        {addr::SC4UI_F79CF20, reinterpret_cast<void*>(&IntroQueryInterface)},
        {addr::SC4UI_F79CF50, reinterpret_cast<void*>(&IntroShutdown)},
        {addr::SC4UI_F79CFE0, reinterpret_cast<void*>(&IntroInit)},
        {addr::SC4UI_F79D110, reinterpret_cast<void*>(&IntroMakeLogoA)},
        {addr::SC4UI_F79D200, reinterpret_cast<void*>(&IntroMakeLogoB)},
        {addr::SC4UI_F79D2F0, reinterpret_cast<void*>(&IntroMessage)},
        {addr::SC4UI_F79D400, reinterpret_cast<void*>(&IntroSkip)},
        {addr::SC4UI_F79D460, reinterpret_cast<void*>(&IntroMouseDown)},
        {addr::SC4UI_F79D470, reinterpret_cast<void*>(&IntroKeyDown)},
        {addr::SC4UI_F79D610, reinterpret_cast<void*>(&IntroDelete)},
        {addr::SC4UI_F79D650, reinterpret_cast<void*>(&PictureChosen)},
        {addr::SC4UI_F79D670, reinterpret_cast<void*>(&PictureShutdown)},
        {addr::SC4UI_F79D700, reinterpret_cast<void*>(&PictureKeyDown)},
        {addr::SC4UI_F79D740, reinterpret_cast<void*>(&PictureMessage)},
        {addr::SC4UI_F79D810, reinterpret_cast<void*>(&PictureConstruct)},
        {addr::SC4UI_F79D880, reinterpret_cast<void*>(&PictureDelete)},
        {addr::SC4UI_F79D8D0, reinterpret_cast<void*>(&PictureInit)},
        {addr::SC4UI_F79DB60, reinterpret_cast<void*>(&PictureGetPath)},
        {addr::SC4UI_F79DBB0, reinterpret_cast<void*>(&PictureSetPath)},
        {addr::SC4UI_MapWindowQueryInterface, reinterpret_cast<void*>(&MapQueryInterface)},
        {addr::SC4UI_MapWindowAddRef, reinterpret_cast<void*>(&MapAddRef)},
        {addr::SC4UI_MapWindowRelease, reinterpret_cast<void*>(&MapRelease)},
        {addr::SC4UI_F79DE40, reinterpret_cast<void*>(&MapIsShown)},
        {addr::SC4UI_F79DE70, reinterpret_cast<void*>(&MapShowRowsA)},
        {addr::SC4UI_F79DF10, reinterpret_cast<void*>(&MapShowRowsB)},
        {addr::SC4UI_F79DFB0, reinterpret_cast<void*>(&MapShowRowsC)},
        {addr::SC4UI_F79E050, reinterpret_cast<void*>(&MapShowRowsD)},
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

const bool g_panels7Registered = registry::Register({
    "ui.panels.part7",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F79BD00,
    "Our cheat box, candidate window, intro window, picture file dialog and map window button rows (docs/ui.md)",
    false,
    InstallPanels7,
});

} // namespace
} // namespace opensc4::ui
