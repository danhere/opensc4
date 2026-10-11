// The rest of the loading screen's picture class (0x00777220 to 0x00777BB0)
// and the region view's five small modal dialogs (0x00777BB0 to
// 0x00778E20).
//
// The picture class (0xC0 bytes, vtables 0x00AB52D4 and 0x00AB52E4, no
// class ID) is what the city view shows while a city loads: the city's two
// saved pictures blended into each other, a status text, and under it one
// line after another of a shuffled list of texts (group 0x4A5CB171,
// instances from 0x0A6DEB35 up). 0x007E5BB0 makes it and keeps it at its
// maker's +0x404; its constructor, QueryInterface and message handler are
// in ui_mysim_dialog_2.cpp. Here are Init, Shutdown, the destructors, the
// shuffle of the lines and Update, which chooses the texts and then has the
// picture drawn.
//
// The dialogs share a base (vtables 0x00AB5314 and 0x00AB5300, 0x1C bytes):
// a window procedure with a reference count that loads a window layout,
// runs it as a modal window and hands back the number the modal run ended
// with. The region view's button handler (0x007AFxxx to 0x007B0xxx) makes
// each one through a factory, runs it and lets it go:
//
//   new region (0x00777FF0): a name field (0x2000) and two buttons that
//     work as a pair (0x1000, 0x1001); OK is greyed while the name is no
//     file name, and a name that a region has already brings a message box
//     up. Ends with the new region's index.
//   load region (0x007780C0): a list (0x1000) of the regions' names; ends
//     with the chosen index.
//   delete region (0x00778130): a question whose text (0x1000) has tokens
//     the app's detokenizer fills in; ends with 1 or 0.
//   a plain question (0x00778220): nothing of its own; ends with 1 or 0.
//   pick one (0x00778D80, 0x68 bytes): a list (0x1000) of records the next
//     part of the range fills in (0x00779060); ends with 1 and the chosen
//     record, or 0.
//
// Replacement, off by default:
//
//   ui.windows.part1: everything named above.
//
// Still the game's: the picture's drawing (0x00777220 and 0x007773B0, which
// set the driver's state, blend the pictures, draw the texts into the
// surface and copy it to the screen), and the string's insert of one
// character (0x007789C0, which runs to 0x00778AAB and has a caller in
// unrelated code).
//
// The picture: +0 vtable (a message target), +4 the counted part, +0xC the
// surface, +0x10 and +0x14 the two pictures, +0x18 and +0x1C two fonts,
// +0x20 the lines still to show (a vector of strings), +0x2C three strings
// (the status, the line, a third), +0x68 four colours, +0x78 the area,
// +0x88 the inner area, +0x94 a height, +0x98 an image, +0xA0 a stopwatch,
// +0xB8 set between Init and Shutdown, +0xB9 the constructor's flag.
//
// A dialog: +0 vtable (the window procedure), +4 the counted part, +0xC
// the window while it is up, +0x10 the layout's instance, +0x14 the
// window's ID in the layout, +0x18 the child that gets the keyboard. The
// pick-one dialog adds +0x1C and +0x20 (the constructor's two numbers),
// +0x24 a list of records, +0x28 the chosen record (three numbers and a
// string), +0x48 eight numbers.
#include "core/address_check.h"
#include "core/fixed_seed.h"
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

namespace picture {
constexpr std::size_t kCounted = 0x4;
constexpr std::size_t kSurface = 0xC;
constexpr std::size_t kFirst = 0x10;
constexpr std::size_t kSecond = 0x14;
constexpr std::size_t kStatusFont = 0x18;
constexpr std::size_t kLineFont = 0x1C;
constexpr std::size_t kLines = 0x20;
constexpr std::size_t kTexts = 0x2C;
constexpr std::size_t kColors = 0x68;
constexpr std::size_t kArea = 0x78;
constexpr std::size_t kInner = 0x88;
constexpr std::size_t kHeight = 0x94;
constexpr std::size_t kImage = 0x98;
constexpr std::size_t kWatch = 0xA0;
constexpr std::size_t kStarted = 0xB8;
constexpr std::size_t kFlag = 0xB9;
} // namespace picture

namespace dialog {
constexpr std::size_t kCounted = 0x4;
constexpr std::size_t kWindow = 0xC;
constexpr std::size_t kLayout = 0x10;
constexpr std::size_t kId = 0x14;
constexpr std::size_t kFocus = 0x18;
// The pick-one dialog.
constexpr std::size_t kFirstNumber = 0x1C;
constexpr std::size_t kSecondNumber = 0x20;
constexpr std::size_t kRecords = 0x24;
constexpr std::size_t kChosen = 0x28;
constexpr std::size_t kNumbers = 0x48;
} // namespace dialog

constexpr uint32_t kLoadingMessage = 0x26D31EC0;
constexpr uint32_t kCommand = 3;
constexpr uint32_t kButtonClicked = 0x287259F6;
constexpr uint32_t kOkButton = 0x8A5B9959;
constexpr uint32_t kCancelButton = 0xCA5B9966;
constexpr uint32_t kEndModal = 0xF;
constexpr uint32_t kTextGroup = 0x6A231EAA;

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
struct LocalizedString {
    GameString text;
    uint32_t rest[5];
};

// An image by resource key or by file name (0x30 bytes, vtable 0x00A856CC).
struct ImageLoader {
    std::uintptr_t vtable;
    void* image;
    uint32_t key[3];
    GameString name;
    uint32_t mode;
    uint8_t flag;
};
static_assert(sizeof(ImageLoader) == 0x30);

// What the pick-one dialog's list holds and hands back.
struct Record {
    uint32_t numbers[3];
    GameString name;
};
static_assert(sizeof(Record) == 0x20);

// A node of the pick-one dialog's list (0x28 bytes).
struct RecordNode {
    RecordNode* next;
    RecordNode* previous;
    Record record;
};
static_assert(sizeof(RecordNode) == 0x28);

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

// An interface asked for into a holder, the holder copied and both let go:
// the interface comes back not held, as the game uses it.
void* QueryUnheld(void* object, uint32_t iid)
{
    void* held = nullptr;
    Virtual<uint8_t>(object, 0x0, iid, &held);
    void* const copy = held;
    if (copy) {
        AddRef(copy);
        if (held)
            Release(held);
        Release(copy);
    }
    return copy;
}

void Free(void* memory)
{
    Cdecl<void>(addr::SC4_Free, memory);
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
        Free(buffer);
}

// The inlined copy of a cIGZString's text (its length, then its data).
void CopyText(GameString& text, void* source, void* tag)
{
    const uint32_t length = Virtual<uint32_t>(source, 0x1C);
    char* const data = Virtual<char*>(source, 0x18);
    text.body.begin = nullptr;
    text.body.end = nullptr;
    text.body.capacity = nullptr;
    This<void>(addr::SC4String_FromRange, &text.body, data, data + length, tag);
    text.vtable = addr::SC4String_Vtable;
    text.zero = 0;
}

// The modal run ended with a number.
void EndModal(char* self, uint32_t result)
{
    void* const manager = Virtual<void*>(At<void*>(self, dialog::kWindow), 0x18);
    Virtual<void>(manager, 0x24, kEndModal, At<void*>(self, dialog::kWindow), result); // GZWinMsgPost
}

// The app's nation, which keeps the regions.
void* Nation()
{
    return Virtual<void*>(Global<void*>(addr::SC4_Service43C94), 0x88);
}

// --- The loading screen's picture -----------------------------------------------------

GameString& TextOf(char* self, std::size_t index)
{
    return At<GameString>(self, picture::kTexts + index * sizeof(GameString));
}

// The game's 0x00777640: a step of the loading (0 for a new status text,
// which also drops the line under it). A second after the last one, or at
// once when there is none, the next of the shuffled lines is taken. Then
// the picture is drawn (0x007773B0, the game's).
void __fastcall Update(char* self, void*, uint32_t kind, void* status)
{
    void* const watch = self + picture::kWatch;
    const uint32_t elapsed = This<uint32_t>(addr::GZStopwatch_ElapsedMilliseconds, watch);
    GameString& line = TextOf(self, 1);
    if (!(static_cast<double>(elapsed) < 1000.0) || line.body.begin == line.body.end) {
        StringVector& lines = At<StringVector>(self, picture::kLines);
        if (lines.begin != lines.end) {
            This<void>(addr::SC4Stopwatch_Elapsed5, watch, 0.0f);
            const StringBody* const last = &(lines.end - 1)->body;
            if (last != &line.body)
                This<void>(addr::SC4String_AssignRange, &line.body, last->begin, last->end, &kind);
            --lines.end;
            Virtual<void*>(lines.end, 0x70, uint32_t{0});
        }
    }
    if (kind == 0) {
        Virtual<void>(&TextOf(self, 0), 0x34, status);
        for (std::size_t index = 1; index < 3; ++index)
            Virtual<void>(&TextOf(self, index), 0x58, uint32_t{0}, uint32_t{0xFFFFFFFF});
    }
    This<void>(addr::SC4UI_F7773B0, self);
}

// The game's 0x00777700: the strings from `begin` to `end` shuffled, each
// swapped with one drawn among those up to it.
void __cdecl ShuffleLines(GameString* begin, GameString* end, void* random)
{
    if (begin == end)
        return;
    for (GameString* item = begin + 1; item != end; ++item) {
        const int32_t count = static_cast<int32_t>(item - begin) + 1;
        const uint32_t drawn = This<uint32_t>(addr::cRZRandom_Below, random, count);
        Cdecl<void>(addr::SC4String_Swap, item, begin + drawn);
    }
}

// The game's 0x00777760: Shutdown.
void __fastcall PictureShutdown(char* self, void*)
{
    if (!At<uint8_t>(self, picture::kStarted))
        return;
    At<uint8_t>(self, picture::kStarted) = 0;
    void*& surface = At<void*>(self, picture::kSurface);
    if (surface) {
        Virtual<void>(surface, 0x10);
        Clear(surface);
    }
    StringVector& lines = At<StringVector>(self, picture::kLines);
    This<void*>(addr::SC4StringVector_Erase, &lines, lines.begin, lines.end);
    This<void>(addr::SC4Stopwatch_Stop, self + picture::kWatch);
    Clear(At<void*>(self, picture::kImage));
    Clear(At<void*>(self, picture::kStatusFont));
    Clear(At<void*>(self, picture::kLineFont));
    Virtual<void>(Global<void*>(addr::SC4_MessageServer), 0x18, self, kLoadingMessage); // RemoveNotification
}

// The game's 0x00777800: the destructor.
void __fastcall PictureDestruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4CityPicture_Vtable;
    At<std::uintptr_t>(self, picture::kCounted) = addr::SC4CityPicture_CountedVtable;
    PictureShutdown(self, nullptr);
    This<void>(addr::SC4Stopwatch_Destruct, self + picture::kWatch);
    if (void* const image = At<void*>(self, picture::kImage))
        Release(image);
    for (std::size_t index = 3; index-- > 0;)
        DestroyString(TextOf(self, index));
    StringVector& lines = At<StringVector>(self, picture::kLines);
    GameString* const end = lines.end;
    for (GameString* item = lines.begin; item != end; ++item)
        Virtual<void*>(item, 0x70, uint32_t{0});
    if (GameString* const items = lines.begin)
        Free(items);
    if (void* const font = At<void*>(self, picture::kLineFont))
        Release(font);
    if (void* const font = At<void*>(self, picture::kStatusFont))
        Release(font);
    if (void* const second = At<void*>(self, picture::kSecond))
        Release(second);
    if (void* const first = At<void*>(self, picture::kFirst))
        Release(first);
    if (void* const surface = At<void*>(self, picture::kSurface))
        Release(surface);
    This<void>(addr::cRZBaseRefCount_Destruct, self + picture::kCounted);
}

// The game's 0x007778D0: Init. The loading message asked for, the two
// fonts, a surface of the area's size and its four colours, the image, the
// lines loaded and shuffled, the stopwatch started a second ahead.
void __fastcall PictureInit(char* self, void*)
{
    if (At<uint8_t>(self, picture::kStarted))
        return;
    At<uint8_t>(self, picture::kStarted) = 1;
    Virtual<void>(Global<void*>(addr::SC4_MessageServer), 0x14, self, kLoadingMessage); // AddNotification
    Hold(At<void*>(self, picture::kStatusFont),
         Virtual<void*>(Cdecl<void*>(addr::SC4_Singleton913C72), 0x14, uint32_t{0x0A9C7973}));
    Hold(At<void*>(self, picture::kLineFont),
         Virtual<void*>(Cdecl<void*>(addr::SC4_Singleton913C72), 0x14, uint32_t{0x4A9C7970}));
    void* const statusFont = At<void*>(self, picture::kStatusFont);
    if (!statusFont)
        return;
    void* const lineFont = At<void*>(self, picture::kLineFont);
    if (!lineFont)
        return;
    const int32_t statusHeight = Virtual<int32_t>(statusFont, 0x8C);
    const int32_t lineHeight = Virtual<int32_t>(lineFont, 0x8C);
    At<int32_t>(self, picture::kHeight) =
        lineHeight + statusHeight * 2 + At<int32_t>(self, picture::kInner + 0x4) + 0xF;
    void* const buffers = Global<void*>(addr::SC4_Service43C9C);
    void*& surface = At<void*>(self, picture::kSurface);
    Clear(surface);
    if (!Virtual<uint8_t>(buffers, 0xC, &surface)) {
        PictureShutdown(self, nullptr);
        return;
    }
    const int32_t height = At<int32_t>(self, picture::kArea + 0xC) - At<int32_t>(self, picture::kArea + 0x4);
    const int32_t width = At<int32_t>(self, picture::kArea + 0x8) - At<int32_t>(self, picture::kArea);
    if (!Virtual<uint8_t>(surface, 0xC, width, height, uint32_t{1}, uint32_t{0})) {
        PictureShutdown(self, nullptr);
        return;
    }
    if (At<uint8_t>(self, picture::kFlag)) {
        At<uint32_t>(self, picture::kColors) =
            Virtual<uint32_t>(surface, 0x78, uint32_t{0xFF}, uint32_t{0}, uint32_t{0});
        At<uint32_t>(self, picture::kColors + 0xC) =
            Virtual<uint32_t>(surface, 0x78, uint32_t{0xFF}, uint32_t{0xFF}, uint32_t{0xFF});
    } else {
        At<uint32_t>(self, picture::kColors) =
            Virtual<uint32_t>(surface, 0x78, uint32_t{0xFF}, uint32_t{0xFF}, uint32_t{0xFF});
        At<uint32_t>(self, picture::kColors + 0x4) =
            Virtual<uint32_t>(surface, 0x78, uint32_t{0x8A}, uint32_t{0x96}, uint32_t{0xBE});
        At<uint32_t>(self, picture::kColors + 0x8) =
            Virtual<uint32_t>(surface, 0x78, uint32_t{0x76}, uint32_t{0x81}, uint32_t{0xA3});
        At<uint32_t>(self, picture::kColors + 0xC) =
            Virtual<uint32_t>(surface, 0x78, uint32_t{0xBB}, uint32_t{0xCC}, uint32_t{0xCC});
    }
    ImageLoader loader;
    This<void*>(addr::SC4ImageLoader_ConstructParts, &loader, uint32_t{0x856DDBAC}, uint32_t{0x6A1EED2C},
                uint32_t{0x4A2805FF}, uint32_t{1}, uint32_t{0});
    if (!loader.image) {
        PictureShutdown(self, nullptr);
        This<void>(addr::SC4ImageLoader_Destruct, &loader);
        return;
    }
    This<void*>(addr::SC4RefPtr_Assign, &At<void*>(self, picture::kImage), loader.image);
    This<void>(addr::SC4ImageLoader_Destruct, &loader);
    GameString text;
    This<void*>(addr::SC4String_SC4String, &text);
    StringVector& lines = At<StringVector>(self, picture::kLines);
    for (uint32_t instance = 0x0A6DEB35;
         Cdecl<uint8_t>(addr::SC4_LoadLocalizedText, &text, uint32_t{0x4A5CB171}, instance, uint32_t{0}); ++instance)
        This<void>(addr::SC4StringVector_PushBack, &lines, &text);
    uint32_t random[6];
    {
        const fixed_seed::SiteOverride site(0x00777B41);
        This<void*>(addr::SC4Random_Construct, random, int32_t{-1});
    }
    ShuffleLines(lines.begin, lines.end, random);
    void* const watch = self + picture::kWatch;
    This<void>(addr::SC4Stopwatch_Start, watch);
    This<void>(addr::SC4Stopwatch_Elapsed5, watch, 1000.0f);
    DestroyString(text);
}

// The game's 0x00777B90: the deleting destructor.
char* __fastcall PictureDeletingDestruct(char* self, void*, uint32_t flags)
{
    PictureDestruct(self, nullptr);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// --- The dialogs' base ----------------------------------------------------------------

// The game's 0x00777BB0: the destructor.
void __fastcall DialogDestruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4RegionDialog_Vtable;
    At<std::uintptr_t>(self, dialog::kCounted) = addr::SC4RegionDialog_CountedVtable;
    This<void>(addr::cRZBaseRefCount_Destruct, self + dialog::kCounted);
}

// The game's 0x00777BD0 (slot 3): a message of the dialog's window. The
// window coming up (0xA2BF8AD5) and going (0xA2BF8AD6) are kept and passed
// to slots 6 and 7, a button click goes to slot 8 with its ID, and message
// 0x11 gives the keyboard to the dialog's child unless the window has flag
// 0x1000.
uint8_t __fastcall DialogMessage(char* self, void*, void* window, uint32_t* message)
{
    switch (message[0]) {
    case 0x11: {
        if (Virtual<uint8_t>(window, 0x10C, uint32_t{0x1000}))
            return 0;
        const uint32_t focus = At<uint32_t>(self, dialog::kFocus);
        if (!focus)
            return 0;
        void* const child = Virtual<void*>(window, 0x8C, focus);
        if (!child)
            return 0;
        Virtual<void>(Virtual<void*>(child, 0x18), 0x94, child);
        return 1;
    }
    case kCommand:
        if (message[1] == kButtonClicked)
            Virtual<uint8_t>(self, 0x20, message[2]);
        return 0;
    case 0xA2BF8AD5:
        At<void*>(self, dialog::kWindow) = window;
        Virtual<void>(self, 0x18);
        return 1;
    case 0xA2BF8AD6:
        At<void*>(self, dialog::kWindow) = nullptr;
        Virtual<void>(self, 0x1C);
        return 1;
    default:
        return 0;
    }
}

// The game's 0x00777C90 (slot 8): OK goes to slot 10, cancel to slot 9.
uint8_t __fastcall DialogButton(char* self, void*, uint32_t id)
{
    if (id == kOkButton) {
        Virtual<void>(self, 0x28);
        return 1;
    }
    if (id == kCancelButton) {
        Virtual<void>(self, 0x24);
        return 1;
    }
    return 0;
}

// The game's 0x00777CC0 (slot 9 of the two dialogs that end with an
// index): the modal run ends with -1.
void __fastcall DialogCancelIndex(char* self, void*)
{
    EndModal(self, 0xFFFFFFFF);
}

// The game's 0x00777CF0 (slot 9): the modal run ends with 0.
void __fastcall DialogCancel(char* self, void*)
{
    EndModal(self, 0);
}

// The game's 0x00777D10 (slot 10): the modal run ends with 1.
void __fastcall DialogOk(char* self, void*)
{
    EndModal(self, 1);
}

// The game's 0x00777D30: the base's deleting destructor.
char* __fastcall DialogDeletingDestruct(char* self, void*, uint32_t flags)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4RegionDialog_Vtable;
    At<std::uintptr_t>(self, dialog::kCounted) = addr::SC4RegionDialog_CountedVtable;
    This<void>(addr::cRZBaseRefCount_Destruct, self + dialog::kCounted);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// The game's 0x00777CE0 (slot 0 of the base's counted part at +4).
char* __fastcall DialogDeleteThunk(char* part, void*, uint32_t flags)
{
    return DialogDeletingDestruct(part - dialog::kCounted, nullptr, flags);
}

// The game's 0x00777D60 (slot 5): the window loaded from the layout under
// `parent`, given this window procedure, run as a modal window and
// destroyed; the number the run ended with, 0 when nothing was shown.
int32_t __fastcall DialogRun(char* self, void*, void* parent)
{
    int32_t result = 0;
    AddRef(self);
    uint32_t key[3] = {0, 0x96A006B0, At<uint32_t>(self, dialog::kLayout)};
    void* const window = Cdecl<void*>(addr::SC4UI_LoadWindowChild, key, parent, At<uint32_t>(self, dialog::kId));
    if (window) {
        void* proc = nullptr;
        if (Virtual<uint8_t>(window, 0x0, uint32_t{0x5386D516}, &proc)) {
            void* const manager = Virtual<void*>(window, 0x18);
            uint32_t scope;
            This<void*>(addr::SC4Scope891D9A_Construct, &scope, uint32_t{0xF});
            Virtual<void>(proc, 0x1C, self);
            This<void>(addr::SC4Scope891D9A_Destruct, &scope);
            Virtual<void>(Global<void*>(addr::SC4_Service43CE0), 0x28, window, uint32_t{1});
            void* const top = Virtual<void*>(manager, 0xC);
            Virtual<void>(manager, 0x94, top);
            result = Virtual<int32_t>(manager, 0xA4, window); // DoModalWin
            Virtual<void>(Global<void*>(addr::SC4_Service43CE0), 0x18, window);
        }
        Virtual<void>(Virtual<void*>(window, 0x18), 0x5C, window); // DestroyWindow
        Release(window);
        if (proc)
            Release(proc);
    }
    Release(self);
    return result;
}

// The game's 0x00777E50 (`this` is not used): whether a region has this
// name already.
uint8_t __fastcall RegionNameTaken(char*, void*, GameString* name)
{
    void* const nation = Nation();
    const uint32_t count = Virtual<uint32_t>(nation, 0x18);
    for (uint32_t index = 0; index < count; ++index) {
        void* const regionName = Virtual<void*>(Virtual<void*>(nation, 0x2C, index), 0xC);
        if (Virtual<uint8_t>(regionName, 0x28, name, uint32_t{0}))
            return 1;
    }
    return 0;
}

// The game's 0x00777EB0: a child's button interface (0x8810), not held.
void* __fastcall DialogChildButton(char* self, void*, uint32_t id)
{
    return QueryUnheld(Virtual<void*>(At<void*>(self, dialog::kWindow), 0x8C, id), 0x8810);
}

// The game's 0x00777F20: the deleting destructor of the dialogs that add
// nothing to the base.
char* __fastcall DerivedDeletingDestruct(char* self, void*, uint32_t flags)
{
    DialogDestruct(self, nullptr);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// The game's 0x00777F10 (slot 0 of their counted parts).
char* __fastcall DerivedDeleteThunk(char* part, void*, uint32_t flags)
{
    return DerivedDeletingDestruct(part - dialog::kCounted, nullptr, flags);
}

// The dialog object the factories make inline (0x1C bytes); null when
// there is no memory, which the game does not survive either.
char* MakeDialog(uint32_t layout, uint32_t id, uint32_t focus, std::uintptr_t vtable, std::uintptr_t countedVtable)
{
    char* const self = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0x1C});
    if (!self)
        return nullptr;
    At<std::uintptr_t>(self, 0x0) = addr::SC4NewsTicker_WinProcBaseVtable;
    This<void*>(addr::cRZBaseRefCount_Construct, self + dialog::kCounted);
    At<void*>(self, dialog::kWindow) = nullptr;
    At<uint32_t>(self, dialog::kLayout) = layout;
    At<uint32_t>(self, dialog::kId) = id;
    At<uint32_t>(self, dialog::kFocus) = focus;
    At<std::uintptr_t>(self, 0x0) = vtable;
    At<std::uintptr_t>(self, dialog::kCounted) = countedVtable;
    AddRef(self);
    return self;
}

// --- The new region dialog ------------------------------------------------------------

// The game's 0x00777F40 (slot 8): the two buttons 0x1000 and 0x1001 work
// as a pair; OK and cancel as in the base.
uint8_t __fastcall NewRegionButton(char* self, void*, uint32_t id)
{
    if (id == 0x1000) {
        Virtual<void>(DialogChildButton(self, nullptr, 0x1000), 0x24, uint32_t{1});
        Virtual<void>(DialogChildButton(self, nullptr, 0x1001), 0x24, uint32_t{0});
        return 1;
    }
    if (id == 0x1001) {
        Virtual<void>(DialogChildButton(self, nullptr, 0x1000), 0x24, uint32_t{0});
        Virtual<void>(DialogChildButton(self, nullptr, 0x1001), 0x24, uint32_t{1});
        return 1;
    }
    if (id == kOkButton) {
        Virtual<void>(self, 0x28);
        return 1;
    }
    if (id == kCancelButton) {
        Virtual<void>(self, 0x24);
        return 1;
    }
    return 0;
}

// The game's 0x00777FF0: the new region dialog made and run; the new
// region's index, or -1.
int32_t __cdecl RunNewRegionDialog(void* parent)
{
    char* const self = MakeDialog(0x8A5AB1CB, 0xEA5BA0D1, 0x2000, addr::SC4NewRegionDialog_Vtable,
                                  addr::SC4NewRegionDialog_CountedVtable);
    const int32_t result = Virtual<int32_t>(self, 0x14, parent);
    Release(self);
    return result;
}

// The game's 0x00778390: OK shows whether the name field holds a file
// name. The game passes the answer as the low byte of a stack word whose
// other three bytes it never sets; here they are zero.
void __fastcall NewRegionCheckName(char* self, void*)
{
    void* const ok = Virtual<void*>(At<void*>(self, dialog::kWindow), 0x8C, kOkButton);
    void* const field = Virtual<void*>(At<void*>(self, dialog::kWindow), 0x8C, uint32_t{0x2000});
    if (!ok || !field)
        return;
    GameString name;
    This<void*>(addr::SC4String_FromIGZString, &name, Virtual<void*>(field, 0x124));
    const uint8_t valid = Cdecl<uint8_t>(addr::SC4_IsValidFileName, &name, uint32_t{0});
    Virtual<void>(ok, 0x110, uint32_t{2}, uint32_t{valid});
    DestroyString(name);
}

// The game's 0x00778450 (slot 3): message 0x9B1552D8 (the name field
// changed) checks the name; the rest as in the base.
uint8_t __fastcall NewRegionMessage(char* self, void*, void* window, uint32_t* message)
{
    if (message[0] == 0x9B1552D8) {
        NewRegionCheckName(self, nullptr);
        return 1;
    }
    return DialogMessage(self, nullptr, window, message);
}

// The game's 0x00778470 (slot 10): OK. Nothing for a name that is no file
// name; a message box for a name some region has; else the region is made,
// told which of the two buttons is down, and the run ends with its index.
void __fastcall NewRegionOk(char* self, void*)
{
    uint8_t tag; // never set by the game either
    void* const field = Virtual<void*>(At<void*>(self, dialog::kWindow), 0x8C, uint32_t{0x2000});
    GameString name;
    CopyText(name, Virtual<void*>(field, 0x124), &tag);
    if (Cdecl<uint8_t>(addr::SC4_IsValidFileName, &name, uint32_t{0})) {
        void* const nation = Nation();
        if (RegionNameTaken(self, nullptr, &name)) {
            void* service = nullptr;
            if (void* const framework = Cdecl<void*>(addr::RZGetFramework))
                Virtual<uint8_t>(framework, 0x14, uint32_t{0xA417445E}, uint32_t{0x5A4}, &service);
            This<void>(addr::SC4RefPtr_Release, &service);
            LocalizedString title;
            This<void*>(addr::SC4LocalizedString_SC4LocalizedString, &title, kTextGroup, uint32_t{0x4A78B7E8},
                        uint32_t{1});
            LocalizedString text;
            This<void*>(addr::SC4LocalizedString_SC4LocalizedString, &text, kTextGroup, uint32_t{0x8A78B80E},
                        uint32_t{1});
            Cdecl<void>(addr::SC4UI_MessageDialog, &title, &text);
            DestroyString(text.text);
            DestroyString(title.text);
        } else {
            const uint32_t index = Virtual<uint32_t>(nation, 0x34, &name);
            void* const region = Virtual<void*>(nation, 0x2C, index);
            Virtual<void>(region, 0x20);
            const uint8_t second = Virtual<uint8_t>(DialogChildButton(self, nullptr, 0x1001), 0x28);
            Virtual<void>(region, 0x4C, uint32_t{second ? 1u : 0u});
            Virtual<void>(region, 0x24);
            EndModal(self, index);
        }
    }
    DestroyString(name);
}

// The game's 0x00778630 (slot 6): the window is up. The name field's own
// text, with a number after it while a region has that name, becomes the
// suggestion, all selected; the field and the two buttons report to the
// window; OK is set.
void __fastcall NewRegionInit(char* self, void*)
{
    uint8_t tag; // never set by the game either
    void* const field = Virtual<void*>(At<void*>(self, dialog::kWindow), 0x8C, uint32_t{0x2000});
    void* const edit = QueryUnheld(field, 0x231A1C57);
    Nation();
    GameString base;
    CopyText(base, Virtual<void*>(field, 0x124), &tag);
    GameString name;
    This<void>(addr::SC4String_CopyBuffer, &name.body, &base.body, uint32_t{0}, uint32_t{0xFFFFFFFF}, &tag);
    name.vtable = addr::SC4String_Vtable;
    name.zero = 0;
    for (int32_t number = 0; RegionNameTaken(self, nullptr, &name);) {
        ++number;
        Cdecl<void>(addr::cRZBaseString_Sprintf, &name, "%s %d", base.body.begin, number);
    }
    Virtual<void>(field, 0x128, &name);
    Virtual<void>(edit, 0x84, uint32_t{0x80});
    Virtual<void>(field, 0x158, At<void*>(self, dialog::kWindow));
    Virtual<void>(edit, 0x130, uint32_t{0x800});
    const uint32_t length = Virtual<uint32_t>(edit, 0xBC);
    Virtual<void>(edit, 0x12C, uint32_t{0}, length);
    void* const first = Virtual<void*>(At<void*>(self, dialog::kWindow), 0x8C, uint32_t{0x1000});
    Virtual<void>(first, 0x158, At<void*>(self, dialog::kWindow));
    void* const second = Virtual<void*>(At<void*>(self, dialog::kWindow), 0x8C, uint32_t{0x1001});
    Virtual<void>(second, 0x158, At<void*>(self, dialog::kWindow));
    NewRegionCheckName(self, nullptr);
    DestroyString(name);
    if (char* const buffer = base.body.begin)
        Free(buffer);
}

// --- The load region dialog -----------------------------------------------------------

// The game's 0x00778060 (slot 10): OK ends the run with the list's choice.
void __fastcall LoadRegionOk(char* self, void*)
{
    void* const child = Virtual<void*>(At<void*>(self, dialog::kWindow), 0x8C, uint32_t{0x1000});
    void* held;
    void* const list = *Cdecl<void**>(addr::SC4_QueryHeld, &held, child, uint32_t{0x4132242B}, uint32_t{0});
    if (held)
        Release(held);
    const uint32_t chosen = Virtual<uint32_t>(list, 0x34);
    EndModal(self, chosen);
}

// The game's 0x007780C0: the load region dialog made and run; the chosen
// region's index, or -1.
int32_t __cdecl RunLoadRegionDialog(void* parent)
{
    char* const self = MakeDialog(0x8A5AB1CC, 0x4A5BA0E7, 0x1000, addr::SC4LoadRegionDialog_Vtable,
                                  addr::SC4LoadRegionDialog_CountedVtable);
    const int32_t result = Virtual<int32_t>(self, 0x14, parent);
    Release(self);
    return result;
}

// The game's 0x00778810 (slot 6): the window is up. The list gets every
// region's name (one that starts with '#' gets a second '#' in front), the
// nation's slot 7 answer (the current region) is chosen.
void __fastcall LoadRegionInit(char* self, void*)
{
    void* const nation = Nation();
    void* const child = Virtual<void*>(At<void*>(self, dialog::kWindow), 0x8C, uint32_t{0x1000});
    void* held = nullptr;
    Virtual<uint8_t>(child, 0x0, uint32_t{0x4132242B}, This<void**>(addr::SC4RefPtr_Reset, &held));
    void* const list = held;
    if (list) {
        AddRef(list);
        if (held)
            Release(held);
        Release(list);
    }
    const uint32_t count = Virtual<uint32_t>(nation, 0x18);
    for (uint32_t index = 0; index < count; ++index) {
        void* const regionName = Virtual<void*>(Virtual<void*>(nation, 0x2C, index), 0xC);
        const uint32_t length = Virtual<uint32_t>(regionName, 0x1C);
        char* const data = Virtual<char*>(regionName, 0x18);
        // The inlined string from a range.
        GameString text;
        text.body.begin = nullptr;
        text.body.end = nullptr;
        text.body.capacity = nullptr;
        const uint32_t size = length + 1;
        char* end;
        if (size == 0) {
            Cdecl<void>(addr::STL_ThrowLengthError, "basic_string");
            end = text.body.begin;
        } else {
            end = Cdecl<char*>(addr::SC4_stl_allocate, size);
            text.body.begin = end;
            text.body.end = end;
            text.body.capacity = end + size;
        }
        if (length != 0)
            end = Cdecl<char*>(addr::SC4_memmove, end, data, length) + length;
        text.body.end = end;
        *end = 0;
        text.vtable = addr::SC4String_Vtable;
        text.zero = 0;
        if (text.body.begin != text.body.end && *text.body.begin == '#')
            This<char*>(addr::SC4UI_F7789C0, &text.body, text.body.begin, uint32_t{'#'});
        Virtual<void>(list, 0x1C, &text, uint32_t{0xFFFFFFFE}, index);
        DestroyString(text);
    }
    const uint32_t current = Virtual<uint32_t>(nation, 0x1C);
    Virtual<void>(list, 0x2C, current, uint32_t{1});
    Virtual<void>(list, 0x40);
}

// --- The two questions ----------------------------------------------------------------

// The game's 0x00778130: the delete region question made and run; whether
// it ended with something other than 0.
uint8_t __cdecl RunDeleteRegionDialog(void* parent)
{
    char* const self = MakeDialog(0x8A5AB1CE, 0x6A5BA20C, 0, addr::SC4DeleteRegionDialog_Vtable,
                                  addr::SC4DeleteRegionDialog_CountedVtable);
    const uint8_t result = Virtual<int32_t>(self, 0x14, parent) != 0;
    Release(self);
    return result;
}

// The game's 0x00778220: the plain question made and run.
uint8_t __cdecl RunQuestionDialog(void* parent)
{
    char* const self = MakeDialog(0x8A5AB1D0, 0x8A5AB1D0, 0, addr::SC4QuestionDialog_Vtable,
                                  addr::SC4QuestionDialog_CountedVtable);
    const uint8_t result = Virtual<int32_t>(self, 0x14, parent) != 0;
    Release(self);
    return result;
}

// The game's 0x00778290: a child's caption put through the app's string
// detokenizer. The child is looked up before the ID is tested.
void __fastcall DetokenizeCaption(char* self, void*, uint32_t id)
{
    void* const child = Virtual<void*>(At<void*>(self, dialog::kWindow), 0x8C, id);
    if (!id)
        return;
    GameString format;
    CopyText(format, Virtual<void*>(child, 0x124), &id);
    GameString text;
    InitString(text);
    void* const detokenizer = Virtual<void*>(Global<void*>(addr::SC4_Service43C94), 0xA8);
    Virtual<uint8_t>(detokenizer, 0x24, &format, &text);
    Virtual<void>(child, 0x128, &text);
    DestroyString(text);
    DestroyString(format);
}

// The game's 0x00778420 (slot 6 of the delete region question): the
// window is up.
void __fastcall DeleteRegionInit(char* self, void*)
{
    DetokenizeCaption(self, nullptr, 0x1000);
}

// --- The pick-one dialog --------------------------------------------------------------

// The game's 0x00778430: a record's string let go.
void __fastcall RecordDestruct(Record* self, void*)
{
    DestroyString(self->name);
}

// The game's 0x00778AB0: the list emptied (`this` holds the head node).
void __fastcall RecordListClear(RecordNode** self, void*)
{
    for (RecordNode* node = (*self)->next; node != *self;) {
        RecordNode* const next = node->next;
        DestroyString(node->record.name);
        Free(node);
        node = next;
    }
    (*self)->next = *self;
    (*self)->previous = *self;
}

// The game's 0x00778B00: a record made as a copy.
Record* __fastcall RecordCopy(Record* self, void*, const Record* source)
{
    self->numbers[0] = source->numbers[0];
    self->numbers[1] = source->numbers[1];
    self->numbers[2] = source->numbers[2];
    This<void>(addr::SC4String_CopyBuffer, &self->name.body, &source->name.body, uint32_t{0}, uint32_t{0xFFFFFFFF},
               &source);
    self->name.vtable = addr::SC4String_Vtable;
    self->name.zero = 0;
    return self;
}

// The game's 0x00778BA0: a record assigned.
Record* __fastcall RecordAssign(Record* self, void*, const Record* source)
{
    self->numbers[0] = source->numbers[0];
    self->numbers[1] = source->numbers[1];
    self->numbers[2] = source->numbers[2];
    const StringBody* const from = &source->name.body;
    if (from != &self->name.body)
        This<void>(addr::SC4String_AssignRange, &self->name.body, from->begin, from->end, &source);
    return self;
}

// The game's 0x00778B50: the chosen record handed out.
void __fastcall PickResult(char* self, void*, uint32_t* first, uint32_t* second, uint32_t* third, GameString* name)
{
    const Record& chosen = At<Record>(self, dialog::kChosen);
    *first = chosen.numbers[0];
    *second = chosen.numbers[1];
    *third = chosen.numbers[2];
    This<void>(addr::SC4String_AssignRange, &name->body, chosen.name.body.begin, chosen.name.body.end, &first);
}

// The game's 0x00778BF0: the list's destructor.
void __fastcall RecordListDestruct(RecordNode** self, void*)
{
    RecordListClear(self, nullptr);
    if (RecordNode* const head = *self)
        Free(head);
}

// The game's 0x00778C10: the constructor (two numbers the list's filling
// uses). The chosen record's three numbers are left as they come.
char* __fastcall PickConstruct(char* self, void*, uint32_t first, uint32_t second)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4NewsTicker_WinProcBaseVtable;
    This<void*>(addr::cRZBaseRefCount_Construct, self + dialog::kCounted);
    At<void*>(self, dialog::kWindow) = nullptr;
    At<uint32_t>(self, dialog::kLayout) = 0x8A5AB1CD;
    At<uint32_t>(self, dialog::kId) = 0x0A5BA192;
    At<uint32_t>(self, dialog::kFocus) = 0x1000;
    At<std::uintptr_t>(self, 0x0) = addr::SC4PickDialog_Vtable;
    At<std::uintptr_t>(self, dialog::kCounted) = addr::SC4PickDialog_CountedVtable;
    At<uint32_t>(self, dialog::kFirstNumber) = first;
    At<uint32_t>(self, dialog::kSecondNumber) = second;
    At<RecordNode*>(self, dialog::kRecords) = nullptr;
    RecordNode* const head = Cdecl<RecordNode*>(addr::SC4_stl_allocate, uint32_t{0x28});
    head->next = head;
    head->previous = head;
    At<RecordNode*>(self, dialog::kRecords) = head;
    InitString(At<Record>(self, dialog::kChosen).name);
    for (std::size_t index = 0; index < 8; ++index)
        At<uint32_t>(self, dialog::kNumbers + index * 4) = 0xFFFFFFFF;
    return self;
}

// The game's 0x007781A0 (slot 7): the window went. The list control is
// emptied (its slot 22) and the eight numbers are set to -1.
void __fastcall PickWindowGone(char* self, void*)
{
    void* const window = At<void*>(self, dialog::kWindow);
    if (!window)
        return;
    void* const child = Virtual<void*>(window, 0x8C, uint32_t{0x1000});
    if (!child)
        return;
    void* const list = QueryUnheld(child, 0x3AE8BAD2);
    if (!list)
        return;
    Virtual<void>(list, 0x58);
    for (std::size_t index = 0; index < 8; ++index)
        At<uint32_t>(self, dialog::kNumbers + index * 4) = 0xFFFFFFFF;
}

// The game's 0x00778CD0 (slot 10): OK. The record behind the list's
// choice is kept and the run ends with 1; with no choice it ends with 0.
void __fastcall PickOk(char* self, void*)
{
    void* const child = Virtual<void*>(At<void*>(self, dialog::kWindow), 0x8C, uint32_t{0x1000});
    void* const list = QueryUnheld(child, 0x3AE8BAD2);
    if (const uint32_t chosen = Virtual<uint32_t>(list, 0xD8)) {
        if (const Record* const record = Virtual<const Record*>(list, 0x84, chosen)) {
            RecordAssign(&At<Record>(self, dialog::kChosen), nullptr, record);
            EndModal(self, 1);
            return;
        }
    }
    EndModal(self, 0);
}

// The game's 0x00778DF0: the deleting destructor.
char* __fastcall PickDeletingDestruct(char* self, void*, uint32_t flags)
{
    RecordDestruct(&At<Record>(self, dialog::kChosen), nullptr);
    RecordListDestruct(&At<RecordNode*>(self, dialog::kRecords), nullptr);
    DialogDestruct(self, nullptr);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// The game's 0x00778CC0 (slot 0 of the counted part).
char* __fastcall PickDeleteThunk(char* part, void*, uint32_t flags)
{
    return PickDeletingDestruct(part - dialog::kCounted, nullptr, flags);
}

// The game's 0x00778D80: the pick-one dialog made and run; when it ended
// with something other than 0 the chosen record goes out and the answer is
// true.
uint8_t __cdecl RunPickDialog(void* parent, uint32_t firstNumber, uint32_t secondNumber, uint32_t* first,
                              uint32_t* second, uint32_t* third, GameString* name)
{
    char* const memory = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0x68});
    if (!memory)
        return 0;
    char* const self = PickConstruct(memory, nullptr, firstNumber, secondNumber);
    if (!self)
        return 0;
    AddRef(self);
    if (Virtual<int32_t>(self, 0x14, parent)) {
        PickResult(self, nullptr, first, second, third, name);
        Release(self);
        return 1;
    }
    Release(self);
    return 0;
}

bool InstallWindows1(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[48] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F777640, reinterpret_cast<void*>(&Update)},
        {addr::SC4UI_F777700, reinterpret_cast<void*>(&ShuffleLines)},
        {addr::SC4UI_F777760, reinterpret_cast<void*>(&PictureShutdown)},
        {addr::SC4UI_F777800, reinterpret_cast<void*>(&PictureDestruct)},
        {addr::SC4UI_F7778D0, reinterpret_cast<void*>(&PictureInit)},
        {addr::SC4CityPicture_DeletingDestruct, reinterpret_cast<void*>(&PictureDeletingDestruct)},
        {addr::SC4UI_F777BB0, reinterpret_cast<void*>(&DialogDestruct)},
        {addr::SC4UI_F777BD0, reinterpret_cast<void*>(&DialogMessage)},
        {addr::SC4UI_F777C90, reinterpret_cast<void*>(&DialogButton)},
        {addr::SC4UI_F777CC0, reinterpret_cast<void*>(&DialogCancelIndex)},
        {addr::SC4UI_F777CE0, reinterpret_cast<void*>(&DialogDeleteThunk)},
        {addr::SC4UI_F777CF0, reinterpret_cast<void*>(&DialogCancel)},
        {addr::SC4UI_F777D10, reinterpret_cast<void*>(&DialogOk)},
        {addr::SC4UI_F777D30, reinterpret_cast<void*>(&DialogDeletingDestruct)},
        {addr::SC4UI_F777D60, reinterpret_cast<void*>(&DialogRun)},
        {addr::SC4UI_F777E50, reinterpret_cast<void*>(&RegionNameTaken)},
        {addr::SC4UI_F777EB0, reinterpret_cast<void*>(&DialogChildButton)},
        {addr::SC4UI_F777F10, reinterpret_cast<void*>(&DerivedDeleteThunk)},
        {addr::SC4UI_F777F20, reinterpret_cast<void*>(&DerivedDeletingDestruct)},
        {addr::SC4UI_F777F40, reinterpret_cast<void*>(&NewRegionButton)},
        {addr::SC4UI_F777FF0, reinterpret_cast<void*>(&RunNewRegionDialog)},
        {addr::SC4UI_F778060, reinterpret_cast<void*>(&LoadRegionOk)},
        {addr::SC4UI_F7780C0, reinterpret_cast<void*>(&RunLoadRegionDialog)},
        {addr::SC4UI_F778130, reinterpret_cast<void*>(&RunDeleteRegionDialog)},
        {addr::SC4UI_F7781A0, reinterpret_cast<void*>(&PickWindowGone)},
        {addr::SC4UI_F778220, reinterpret_cast<void*>(&RunQuestionDialog)},
        {addr::SC4UI_F778290, reinterpret_cast<void*>(&DetokenizeCaption)},
        {addr::SC4UI_F778390, reinterpret_cast<void*>(&NewRegionCheckName)},
        {addr::SC4UI_F778420, reinterpret_cast<void*>(&DeleteRegionInit)},
        {addr::SC4UI_F778430, reinterpret_cast<void*>(&RecordDestruct)},
        {addr::SC4UI_F778450, reinterpret_cast<void*>(&NewRegionMessage)},
        {addr::SC4UI_F778470, reinterpret_cast<void*>(&NewRegionOk)},
        {addr::SC4UI_F778630, reinterpret_cast<void*>(&NewRegionInit)},
        {addr::SC4UI_F778810, reinterpret_cast<void*>(&LoadRegionInit)},
        {addr::SC4UI_F778AB0, reinterpret_cast<void*>(&RecordListClear)},
        {addr::SC4UI_F778B00, reinterpret_cast<void*>(&RecordCopy)},
        {addr::SC4UI_F778B50, reinterpret_cast<void*>(&PickResult)},
        {addr::SC4UI_F778BA0, reinterpret_cast<void*>(&RecordAssign)},
        {addr::SC4UI_F778BF0, reinterpret_cast<void*>(&RecordListDestruct)},
        {addr::SC4UI_F778C10, reinterpret_cast<void*>(&PickConstruct)},
        {addr::SC4UI_F778CC0, reinterpret_cast<void*>(&PickDeleteThunk)},
        {addr::SC4UI_F778CD0, reinterpret_cast<void*>(&PickOk)},
        {addr::SC4UI_F778D80, reinterpret_cast<void*>(&RunPickDialog)},
        {addr::SC4UI_F778DF0, reinterpret_cast<void*>(&PickDeletingDestruct)},
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

const bool g_windows1Registered = registry::Register({
    "ui.windows.part1",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F777D60,
    "Our loading screen picture (all but its drawing) and the region view's five small dialogs (docs/ui.md)",
    false,
    InstallWindows1,
});

} // namespace
} // namespace opensc4::ui
