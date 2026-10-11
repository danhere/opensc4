// The MySim picker dialog, second half (0x00776560 to 0x00776BF0), and the
// first functions of the class that follows it (0x00776BF0 to 0x00777220).
// The first half, with Run (0x00775CC0, which ends at 0x00776550), is in
// ui_mysim_dialog_1.cpp.
//
// The dialog (0x64 bytes, no class ID) is made by the MySim panel
// (0x0076E830, which keeps it at panel +0x30) when the player clicks an
// empty MySim place: the panel constructs it with the place's number, calls
// Run, and when Run comes back calls Close and lets it go. Run shows the
// window as a modal one, so it returns when the dialog ends. The window
// procedure handles the buttons: six page numbers, the page strip's arrows,
// the 21 pictures of a page, three sort buttons that read the Control key,
// the import from The Sims, accept and cancel.
//
// The second class (0xC0 bytes, vtables 0x00AB52D4 and 0x00AB52E4) is made
// by 0x007E5BB0 and kept at its maker's +0x404; most of its code lies past
// 0x00777220 and is still the game's. Here are its constructor, its
// QueryInterface, a destructor thunk and its message handler, which loads
// the city's two saved pictures again when message 0x26D31EC0 arrives.
// Replacement, off by default:
//
//   ui.mysim_dialog.part2: the constructor, the destructors, Close and the
//     window procedure of the dialog; the four functions of the second
//     class.
//
// Still the game's: 0x00776C30, which blends one picture into another pixel
// by pixel for the second class's drawing (and takes arguments in EAX and
// ESI), and 0x00776F50, a rectangle helper that window code all over the
// exe calls.
//
// The dialog: +0 vtable (reference count at +4), +8 the window procedure,
// +0xC a message target, +0x10 the window, +0x14 the order of the Sims
// shown (indices into the records), +0x24 set while the window is modal,
// +0x28 a string, +0x3C the first Sim of the page, +0x40 the number of
// Sims, +0x44 the page of the first page button, +0x48 the page, +0x4C the
// picker's list, +0x50 its records (a vector of 0x30-byte records), +0x5C
// the MySim place, +0x60 a message.
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

namespace field {
constexpr std::size_t kWinProc = 0x8;
constexpr std::size_t kTarget = 0xC;
constexpr std::size_t kWindow = 0x10;
constexpr std::size_t kOrder = 0x14;
constexpr std::size_t kModal = 0x24;
constexpr std::size_t kText = 0x28;
constexpr std::size_t kFirst = 0x3C;
constexpr std::size_t kCount = 0x40;
constexpr std::size_t kFirstPage = 0x44;
constexpr std::size_t kPage = 0x48;
constexpr std::size_t kList = 0x4C;
constexpr std::size_t kSims = 0x50;
constexpr std::size_t kPlace = 0x5C;
constexpr std::size_t kMessage = 0x60;
} // namespace field

// The second class.
namespace picture {
constexpr std::size_t kCounted = 0x4;
constexpr std::size_t kFirst = 0x10;  // the two pictures
constexpr std::size_t kSecond = 0x14;
constexpr std::size_t kTexts = 0x2C;  // three strings
constexpr std::size_t kArea = 0x78;   // two rectangles
constexpr std::size_t kInner = 0x88;
constexpr std::size_t kWatch = 0xA0;
constexpr std::size_t kStarted = 0xB8;
constexpr std::size_t kFlag = 0xB9;
} // namespace picture

constexpr int32_t kPerPage = 0x15;
constexpr uint32_t kCommand = 3;
constexpr uint32_t kButtonClicked = 0x287259F6;
constexpr uint32_t kControlKey = 0x11;

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

struct RecordVector {
    char* begin;
    char* end;
    char* capacity;
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

int32_t CountOf(const RecordVector& sims)
{
    return static_cast<int32_t>(sims.end - sims.begin) / 0x30;
}

// The records dropped, as the game's inlined vector clear does it.
void ClearSims(RecordVector& sims)
{
    uint8_t tag; // never set by the game either
    char* const end =
        Cdecl<char*>(addr::SC4UI_F43B360, sims.end, sims.end, sims.begin, &tag, uint32_t{0});
    Cdecl<void>(addr::SC4UI_F43B180, end, sims.end, &tag);
    sims.end = end;
}

// A fresh order: every Sim, as the list has them.
void FillOrder(char* self)
{
    At<uint32_t*>(self, field::kOrder) =
        Cdecl<uint32_t*>(addr::SC4_operator_new, At<uint32_t>(self, field::kCount) << 2);
    for (uint32_t index = 0; index < At<uint32_t>(self, field::kCount); ++index)
        At<uint32_t*>(self, field::kOrder)[index] = index;
}

// Whether the Control key is up, as a button handler's argument.
uint32_t ControlUp()
{
    return Cdecl<uint32_t>(addr::SC4_IsKeyDown, kControlKey) ? 0 : 1;
}

// The game's 0x00775BE0 takes the string in EAX and the number and a zero
// on the stack (caller cleans): it puts the number where the text has '#'.
void* g_putNumber = reinterpret_cast<void*>(addr::SC4UI_F775BE0);

__declspec(naked) uint8_t __cdecl PutNumber(GameString* /*text*/, int32_t /*number*/, uint32_t /*zero*/)
{
    __asm mov eax, [esp + 4]
    __asm push dword ptr [esp + 12]
    __asm push dword ptr [esp + 12]
    __asm call dword ptr [g_putNumber]
    __asm add esp, 8
    __asm ret
}

// The caption of a text window with its '#' replaced by a number.
void PutNumberInCaption(void* window, int32_t number)
{
    GameString caption;
    This<void*>(addr::SC4String_FromIGZString, &caption, Virtual<void*>(window, 0x124));
    PutNumber(&caption, number, 0);
    Virtual<uint8_t>(window, 0x128, &caption);
    This<void>(addr::SC4String_Destroy, &caption);
}

// A window shown as a modal one.
void RunModal(void* manager, void* window)
{
    Virtual<void>(Global<void*>(addr::SC4_Service43CE0), 0x28, window, uint32_t{1});
    Virtual<int32_t>(manager, 0xA4, window); // DoModalWin
    Virtual<void>(Global<void*>(addr::SC4_Service43CE0), 0x18, window);
}

// The game's 0x00776560: the constructor (the MySim place).
char* __fastcall Construct(char* self, void*, uint32_t place)
{
    This<void*>(addr::cRZBaseUnknown_Construct, self);
    At<std::uintptr_t>(self, field::kWinProc) = addr::SC4NewsTicker_WinProcBaseVtable;
    At<std::uintptr_t>(self, field::kTarget) = addr::cSC4TrafficSimulator_VtableBase4;
    At<std::uintptr_t>(self, 0x0) = addr::SC4MySimDialog_Vtable;
    At<std::uintptr_t>(self, field::kWinProc) = addr::SC4MySimDialog_WinProcVtable;
    At<std::uintptr_t>(self, field::kTarget) = addr::SC4MySimDialog_TargetVtable;
    At<void*>(self, field::kWindow) = nullptr;
    At<uint32_t*>(self, field::kOrder) = nullptr;
    At<uint32_t>(self, 0x18) = 0;
    At<uint8_t>(self, 0x1C) = 1;
    At<uint32_t>(self, 0x20) = 0;
    At<uint8_t>(self, field::kModal) = 0;
    InitString(At<GameString>(self, field::kText));
    At<uint32_t>(self, field::kFirst) = 0;
    At<uint32_t>(self, field::kCount) = 0;
    At<uint32_t>(self, field::kFirstPage) = 0;
    At<uint32_t>(self, field::kPage) = 0;
    At<void*>(self, field::kList) = nullptr;
    RecordVector& sims = At<RecordVector>(self, field::kSims);
    sims.begin = nullptr;
    sims.end = nullptr;
    sims.capacity = nullptr;
    At<uint32_t>(self, field::kPlace) = place;
    At<void*>(self, field::kMessage) = nullptr;
    return self;
}

// The game's 0x00776600: the destructor.
void __fastcall Destruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4MySimDialog_Vtable;
    At<std::uintptr_t>(self, field::kWinProc) = addr::SC4MySimDialog_WinProcVtable;
    At<std::uintptr_t>(self, field::kTarget) = addr::SC4MySimDialog_TargetVtable;
    if (void* const message = At<void*>(self, field::kMessage))
        Release(message);
    RecordVector& sims = At<RecordVector>(self, field::kSims);
    uint8_t tag; // never set by the game either
    Cdecl<void>(addr::SC4UI_F43B180, sims.begin, sims.end, &tag);
    if (char* const records = sims.begin)
        Free(records);
    DestroyString(At<GameString>(self, field::kText));
    if (void* const window = At<void*>(self, field::kWindow))
        Release(window);
    This<void>(addr::cRZBaseUnknown_Destruct, self);
}

// The game's 0x00776680 (slot 3): the deleting destructor.
char* __fastcall DeletingDestruct(char* self, void*, uint32_t flags)
{
    Destruct(self, nullptr);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// The game's 0x007766A0: the modal window ended if it still runs, the
// window destroyed, the records, the list and the order freed.
void __fastcall Close(char* self, void*)
{
    Virtual<uint32_t>(self, 0x4);
    if (At<uint8_t>(self, field::kModal)) {
        void* const manager = Cdecl<void*>(addr::SC4_GetWinManager);
        void* const window = Virtual<void*>(At<void*>(self, field::kWindow), 0xC);
        Virtual<void>(manager, 0x24, uint32_t{0xF}, window, uint32_t{0}); // GZWinMsgPost
        At<uint8_t>(self, field::kModal) = 0;
    }
    if (void* const manager = Virtual<void*>(Virtual<void*>(At<void*>(self, field::kWindow), 0xC), 0x18))
        Virtual<void>(manager, 0x5C, Virtual<void*>(At<void*>(self, field::kWindow), 0xC)); // DestroyWindow
    Clear(At<void*>(self, field::kWindow));
    ClearSims(At<RecordVector>(self, field::kSims));
    if (void* const list = At<void*>(self, field::kList))
        Virtual<void>(list, 0xC, uint32_t{1});
    uint32_t* const order = At<uint32_t*>(self, field::kOrder);
    At<void*>(self, field::kList) = nullptr;
    Cdecl<void>(addr::SC4_operator_delete, order);
    At<uint32_t*>(self, field::kOrder) = nullptr;
    Virtual<uint32_t>(self, 0x8);
}

// A page button: the page `step` after the strip's first.
void ShowPage(char* self, int32_t step)
{
    const int32_t page = At<int32_t>(self, field::kFirstPage) + step;
    At<int32_t>(self, field::kPage) = page;
    const int32_t first = page * kPerPage;
    At<int32_t>(self, field::kFirst) = first;
    This<void>(addr::SC4UI_F7751A0, self, first);
}

// The import button: the families The Sims exported are imported, the
// result box shows how many Sims came and how many the list gained, and the
// order starts anew. `imported` is the handler's window argument, whose
// stack slot the game uses for the import's count.
void ImportSims(char* self, uint32_t imported)
{
    void* const manager = Virtual<void*>(Virtual<void*>(At<void*>(self, field::kWindow), 0xC), 0x18);
    RecordVector& sims = At<RecordVector>(self, field::kSims);
    This<void>(addr::SC4UI_F43BB90, &sims);
    Virtual<void>(At<void*>(self, field::kList), 0x18, &sims);
    const int32_t before = CountOf(sims);
    if (Virtual<uint8_t>(At<void*>(self, field::kList), 0x1C, &imported)) {
        void* const parent = Virtual<void*>(Virtual<void*>(At<void*>(self, field::kWindow), 0xC), 0x2C);
        This<void>(addr::SC4UI_F43BB90, &sims);
        Virtual<void>(At<void*>(self, field::kList), 0x18, &sims);
        At<int32_t>(self, field::kCount) = CountOf(sims);
        if (void* const text = Virtual<void*>(parent, 0x8C, uint32_t{0x8A9FF482}))
            PutNumberInCaption(text, static_cast<int32_t>(imported));
        if (void* const text = Virtual<void*>(parent, 0x8C, uint32_t{0x8A9FF484}))
            PutNumberInCaption(text, At<int32_t>(self, field::kCount) - before);
        void* const box = Virtual<void*>(parent, 0x8C, uint32_t{0x6A243D9F});
        Virtual<void>(box, 0x110, uint32_t{1}, uint32_t{1});
        void* const focus = Virtual<void*>(manager, 0x90);
        RunModal(manager, box);
        if (Virtual<uint8_t>(manager, 0x60, focus))
            Virtual<void>(manager, 0x94, focus);
        Cdecl<void>(addr::SC4_operator_delete, At<uint32_t*>(self, field::kOrder));
        FillOrder(self);
    }
    This<void>(addr::SC4UI_F774690, self);
}

// The game's 0x00776760 (the window procedure's slot 3; `part` is the
// dialog +8; one function up to 0x00776BE3): a message of the dialog's
// window. Window message 0x9B1552D8 goes to 0x00774FC0; a button click is
// handled by its ID, and a click on one of the 21 pictures chooses it.
uint8_t __fastcall WinProcMessage(char* part, void*, void* window, uint32_t* message)
{
    char* const self = part - field::kWinProc;
    if (window != Virtual<void*>(At<void*>(self, field::kWindow), 0xC))
        return 0;
    if (message[0] != kCommand) {
        if (message[0] == 0x9B1552D8)
            This<void>(addr::SC4UI_F774FC0, self);
        return 0;
    }
    if (message[1] != kButtonClicked)
        return 0;
    switch (message[2]) {
    case 0x12345000:
        ShowPage(self, 0);
        break;
    case 0x12345001:
        ShowPage(self, 1);
        break;
    case 0x12345002:
        ShowPage(self, 2);
        break;
    case 0x12345003:
        ShowPage(self, 3);
        break;
    case 0x12345004:
        ShowPage(self, 4);
        break;
    case 0x12345005:
        ShowPage(self, 5);
        break;
    case 0x0A243DD1:
        This<void>(addr::SC4UI_F7745C0, self);
        return 1;
    case 0x0A243DDA:
        This<void>(addr::SC4UI_F7753A0, self, ControlUp());
        break;
    case 0x4A243DE9:
        This<void>(addr::SC4UI_F775410, self, ControlUp());
        break;
    case 0x4A243DF1:
        This<void>(addr::SC4UI_F7754B0, self, ControlUp());
        break;
    case 0x2A243DF9:
        ImportSims(self, static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(window)));
        break;
    case 0x2A243DFA:
        if (const int32_t page = At<int32_t>(self, field::kFirstPage))
            At<int32_t>(self, field::kFirstPage) = page - 1;
        This<void>(addr::SC4UI_F774690, self);
        break;
    case 0xCA243E04:
        ++At<int32_t>(self, field::kFirstPage);
        This<void>(addr::SC4UI_F774690, self);
        break;
    case 0xCA243E0C:
        This<void>(addr::SC4UI_F7755A0, self);
        return 1;
    case 0xEA57DA77: {
        // The result box's button: its modal run ends and it hides.
        void* const parent = Virtual<void*>(Virtual<void*>(At<void*>(self, field::kWindow), 0xC), 0x2C);
        void* const box = Virtual<void*>(parent, 0x8C, uint32_t{0x6A243D9F});
        void* const manager = Cdecl<void*>(addr::SC4_GetWinManager);
        Virtual<void>(manager, 0x24, uint32_t{0xF}, box, uint32_t{0});
        Virtual<void>(box, 0x110, uint32_t{1}, uint32_t{0});
        break;
    }
    default:
        break;
    }
    const uint32_t* const pictures = reinterpret_cast<const uint32_t*>(addr::SC4MySimDialog_PictureButtons);
    for (int32_t index = 0; index < kPerPage; ++index) {
        if (message[2] == pictures[index])
            This<void>(addr::SC4UI_F7757F0, self, index);
    }
    return 0;
}

// The game's 0x00776BF0 (the second class, slot 0): QueryInterface.
uint8_t __fastcall PictureQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    if (iid == 1 || iid == 0x452294AA) {
        *out = self;
        Virtual<uint32_t>(self, 0x4);
        return 1;
    }
    return 0;
}

// The game's 0x00776F90: the second class's constructor (an area of four
// numbers and a flag). +0x68 to +0x77 and +0x9C are left as they come.
char* __fastcall PictureConstruct(char* self, void*, const int32_t* area, uint32_t flag)
{
    At<std::uintptr_t>(self, 0x0) = addr::cSC4TrafficSimulator_VtableBase4;
    This<void*>(addr::cRZBaseRefCount_Construct, self + picture::kCounted);
    At<std::uintptr_t>(self, picture::kCounted) = addr::SC4CityPicture_CountedVtable;
    At<std::uintptr_t>(self, 0x0) = addr::SC4CityPicture_Vtable;
    for (std::size_t offset = 0xC; offset <= 0x28; offset += 4)
        At<uint32_t>(self, offset) = 0;
    for (std::size_t index = 0; index < 3; ++index)
        InitString(At<GameString>(self, picture::kTexts + index * sizeof(GameString)));
    for (std::size_t index = 0; index < 4; ++index)
        At<int32_t>(self, picture::kArea + index * 4) = area[index];
    for (std::size_t index = 0; index < 4; ++index)
        At<int32_t>(self, picture::kInner + index * 4) = area[index];
    At<uint32_t>(self, 0x98) = 0;
    This<void*>(addr::SC4Stopwatch_Construct, self + picture::kWatch, uint32_t{4});
    At<uint8_t>(self, picture::kStarted) = 0;
    At<uint8_t>(self, picture::kFlag) = static_cast<uint8_t>(flag);
    At<int32_t>(self, picture::kInner + 0x4) = area[1] + 0xC;
    At<int32_t>(self, picture::kInner + 0xC) = area[1] + 0x64;
    return self;
}

// The game's 0x007770A0 (slot 0 of the counted part at +4): on to the
// deleting destructor 0x00777B90.
char* __fastcall PictureDeleteThunk(char* part, void*, uint32_t flags)
{
    return This<char*>(addr::SC4CityPicture_DeletingDestruct, part - picture::kCounted, flags);
}

// The game's 0x007770B0 (slot 3; it runs to 0x00777217): on message
// 0x26D31EC0 the two pictures are loaded again, from the city's save file
// or, when that fails, the stock ones for the city's size and the region's
// number (slot 18).
uint8_t __fastcall PictureMessage(char* self, void*, void* message)
{
    if (Virtual<uint32_t>(message, 0x10) != 0x26D31EC0)
        return 0;
    GameString path;
    InitString(path);
    Virtual<void>(Global<void*>(addr::SC4_City), 0x28, &path); // GetCitySaveFilePath
    void*& second = At<void*>(self, picture::kSecond);
    Clear(second);
    void*& first = At<void*>(self, picture::kFirst);
    Clear(first);
    if (!Cdecl<uint8_t>(addr::SC4_LoadSavePictures, &path, &first, &second, uint32_t{0}, uint32_t{0})) {
        void* const region = Virtual<void*>(Global<void*>(addr::SC4_Service43C94), 0x8C);
        const int32_t number = Virtual<int32_t>(region, 0x48);
        const int32_t size = Virtual<int32_t>(Global<void*>(addr::SC4_City), 0x15C);
        const uint32_t* const pair =
            reinterpret_cast<const uint32_t*>(addr::SC4CityPicture_StockPictures) + (size + number * 3) * 2;
        ImageLoader one;
        This<void*>(addr::SC4ImageLoader_ConstructParts, &one, uint32_t{0x856DDBAC}, uint32_t{0x6A1EED2C}, pair[0],
                    uint32_t{9}, uint32_t{0});
        ImageLoader two;
        This<void*>(addr::SC4ImageLoader_ConstructParts, &two, uint32_t{0x856DDBAC}, uint32_t{0x6A1EED2C}, pair[1],
                    uint32_t{9}, uint32_t{0});
        if (one.image)
            This<void>(addr::SC4RefPtr_Assign, &first, one.image);
        if (two.image)
            This<void>(addr::SC4RefPtr_Assign, &second, two.image);
        This<void>(addr::SC4ImageLoader_Destruct, &two);
        This<void>(addr::SC4ImageLoader_Destruct, &one);
    }
    DestroyString(path);
    return 1;
}

bool InstallMySimDialog2(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[16] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F776560, reinterpret_cast<void*>(&Construct)},
        {addr::SC4UI_F776600, reinterpret_cast<void*>(&Destruct)},
        {addr::SC4UI_F776680, reinterpret_cast<void*>(&DeletingDestruct)},
        {addr::SC4UI_F7766A0, reinterpret_cast<void*>(&Close)},
        {addr::SC4UI_F776760, reinterpret_cast<void*>(&WinProcMessage)},
        {addr::SC4UI_F776BF0, reinterpret_cast<void*>(&PictureQueryInterface)},
        {addr::SC4UI_F776F90, reinterpret_cast<void*>(&PictureConstruct)},
        {addr::SC4UI_F7770A0, reinterpret_cast<void*>(&PictureDeleteThunk)},
        {addr::SC4UI_F7770B0, reinterpret_cast<void*>(&PictureMessage)},
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

const bool g_mySimDialog2Registered = registry::Register({
    "ui.mysim_dialog.part2",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F776760,
    "Our MySim picker dialog (run, close, window procedure) and the city picture's first functions (docs/ui.md)",
    false,
    InstallMySimDialog2,
});

} // namespace
} // namespace opensc4::ui
