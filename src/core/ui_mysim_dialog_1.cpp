// The MySim move-in dialog, first half (0x00774620 to 0x00776560): the
// modal dialog in which the player picks a Sim from a paged list of 21
// portraits, types a name and a biography and chooses a zodiac sign. A
// reference-counted object (0x64 bytes, vtables 0x00AB5284 at +0, 0x00AB5270
// at +8 for the window procedure, 0x00AB5260 at +0xC for the message target)
// that the MySim panel makes with operator new, runs and drops again
// (0x0076E88E). Its constructor, destructor, window procedure and closing
// code are the second half (ui.mysim_dialog.part2). None of this draws.
// Replacement, off by default:
//
//   ui.mysim_dialog.part1: running the dialog, the page buttons, the list's
//     rows and its three orders, the selection, the name check and its
//     recurring message, accepting, and the number put into a text.
//
// The object: +0x10 the dialog's panel (a cIGZWinGen; the controls are
// children of its window's parent), +0x14 the order of the records (an
// array of indices), +0x18 the picked position, +0x1C the picked Sim's flag,
// +0x20 its picture's instance (0: a file), +0x24 modal (byte), +0x28 the
// picture's file name (a string), +0x3C the first position shown, +0x40 the
// number of records, +0x44 the page on the first of the six page buttons,
// +0x48 the page shown, +0x4C the picker's list (ui_mysim_tools_3.cpp),
// +0x50 the records (a vector of 0x30-byte records: +0 picture instance, +4
// picture file name, +0x18 name, +0x2C flag, +0x2D zodiac sign), +0x5C the
// MySim's place (the constructor's argument), +0x60 the recurring message.
// Still the game's: the two string replace helpers 0x00775100 and
// 0x00775B50 (generic string code).
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
constexpr std::size_t kPanel = 0x10;
constexpr std::size_t kOrder = 0x14;
constexpr std::size_t kPicked = 0x18;
constexpr std::size_t kPickedFlag = 0x1C;
constexpr std::size_t kPictureId = 0x20;
constexpr std::size_t kModal = 0x24;
constexpr std::size_t kPictureName = 0x28; // string
constexpr std::size_t kFirst = 0x3C;
constexpr std::size_t kCount = 0x40;
constexpr std::size_t kFirstPage = 0x44;
constexpr std::size_t kPage = 0x48;
constexpr std::size_t kSims = 0x4C;
constexpr std::size_t kRecords = 0x50; // vector of records
constexpr std::size_t kPlace = 0x5C;
constexpr std::size_t kMessage = 0x60;
} // namespace field

constexpr uint32_t kRowsPerPage = 0x15;
constexpr uint32_t kPollMessage = 0xCC327B60;
constexpr uint32_t kUiGroup = 0x46A006B0;
constexpr uint32_t kTextGroup = 0xEA231E96;
constexpr uint32_t kZodiacGroup = 0x6A231EAA;

// Windows of the dialog.
constexpr uint32_t kPanelWindow = 0x6A243D9E;
constexpr uint32_t kWaitWindow = 0x6A243D9F;
constexpr uint32_t kRowPictureFirst = 0x12340000; // 21 of them
constexpr uint32_t kPickedPicture = 0x12340015;
constexpr uint32_t kPageButtonFirst = 0x12345000; // six
constexpr uint32_t kPageMarkFirst = 0x12345100;   // six
constexpr uint32_t kImportButton = 0x2A243DF9;
constexpr uint32_t kPagesBack = 0x2A243DFA;
constexpr uint32_t kPagesOn = 0xCA243E04;
constexpr uint32_t kAcceptButton = 0xCA243E0C;
constexpr uint32_t kNameEdit = 0x8A243F1F;
constexpr uint32_t kBioEdit = 0x8A243F6E;
constexpr uint32_t kZodiacCombo = 0xAA243F5E;
constexpr uint32_t kFlagText = 0xEA243F57;
constexpr uint32_t kNotifier = 0xEA57DA77;

// Interfaces asked of the children.
constexpr uint32_t kWinGenIid = 0x5386D516;
constexpr uint32_t kEditIid = 0x231A1C57;
constexpr uint32_t kComboIid = 0x412CE496;
constexpr uint32_t kPictureIid = 0xC12CEA13;

// The names of the twelve signs, in the combo's order (group 0x6A231EAA).
constexpr uint32_t kZodiacNames[12] = {
    0xEA822E90, 0xCA822E93, 0xCA822E97, 0xAA822E9B, 0xCA822EA4, 0xAA822EA8,
    0xCA822EAC, 0xCA822EAF, 0xCA822EB3, 0x6A822EB6, 0x6A822EB9, 0xEA822EBD,
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

// A string from the text resources: a string first, 0x20 bytes.
struct LocalizedText {
    GameString text;
    char rest[0xC];
};
static_assert(sizeof(LocalizedText) == 0x20);

struct Record {
    uint32_t id;
    GameString picture;
    GameString name;
    uint8_t flag;
    uint8_t zodiac;
    uint8_t unused[2];
};
static_assert(sizeof(Record) == 0x30);

struct RecordVector {
    Record* begin;
    Record* end;
    Record* capacity;
};

// The game's image holder (SC4ImageLoader, 0x30 bytes); the image at +4.
struct ImageLoader {
    alignas(4) char bytes[0x30];
};

// What the game switches off while a modal dialog runs (0x005F9320,
// 0x005F9360): four flag bytes.
struct ModalGuard {
    uint8_t flags[4];
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

// The game's counted pointer assigned: the new object referenced first, then
// the old one released.
void Hold(void*& held, void* fresh)
{
    if (fresh == held)
        return;
    void* const old = held;
    if (fresh)
        AddRef(fresh);
    held = fresh;
    if (old)
        Release(old);
}

// The game's counted pointer emptied before it is filled again.
void Drop(void*& held)
{
    if (void* const old = held) {
        held = nullptr;
        Release(old);
    }
}

void Free(void* memory)
{
    Cdecl<void>(addr::SC4_Free, memory);
}

void* Allocate(uint32_t size)
{
    return Cdecl<void*>(addr::SC4_stl_allocate, size);
}

// An empty string as the game's inlined constructor makes it.
void InitString(GameString& text)
{
    text.body.begin = nullptr;
    text.body.end = nullptr;
    text.body.capacity = nullptr;
    char* const buffer = static_cast<char*>(Allocate(8));
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

// The body of a string, as the game's casts work it out.
StringBody* Body(void* text)
{
    return text ? reinterpret_cast<StringBody*>(static_cast<char*>(text) + 0x4) : nullptr;
}

uint32_t Length(const char* chars)
{
    const char* end = chars;
    while (*end)
        ++end;
    return static_cast<uint32_t>(end - chars);
}

// The panel's window and the window that holds the controls.
void* PanelWindow(char* self)
{
    return Virtual<void*>(At<void*>(self, field::kPanel), 0xC); // AsIGZWin
}

void* Dialog(char* self)
{
    return Virtual<void*>(PanelWindow(self), 0x2C); // GetParentWin
}

void* Child(void* window, uint32_t id)
{
    return Virtual<void*>(window, 0x8C, id); // GetChildWindowFromIDRecursive
}

uint8_t ChildAs(void* window, uint32_t id, uint32_t iid, void** out)
{
    return Virtual<uint8_t>(window, 0x94, id, iid, out); // GetChildAsRecursive
}

// Flag 1 is "visible", flag 2 "enabled".
void SetFlag(void* window, uint32_t flag, uint32_t on)
{
    Virtual<uint32_t>(window, 0x110, flag, on);
}

void SetCaption(void* window, void* text)
{
    Virtual<uint32_t>(window, 0x128, text);
}

Record& RecordAt(char* self, uint32_t position)
{
    return At<Record*>(self, field::kRecords)[At<uint32_t*>(self, field::kOrder)[position]];
}

// The game's 0x00774620: posts the recurring message (0xCC327B60, made on
// first use) to the object's own message target; DoMessage answers it with
// the name check and posts it again.
void __fastcall PostPoll(char* self, void*)
{
    if (!At<void*>(self, field::kMessage)) {
        void* const memory = Cdecl<void*>(addr::RZ_operator_new, uint32_t{0x2C});
        void* const fresh = memory ? This<void*>(addr::cGZMessage_cGZMessage, memory) : nullptr;
        Hold(At<void*>(self, field::kMessage), fresh);
        Virtual<void>(At<void*>(self, field::kMessage), 0x14, kPollMessage);
    }
    // GeneralMessagePostToTarget
    Virtual<void>(Global<void*>(addr::SC4_MessageServer), 0x1C, At<void*>(self, field::kMessage),
                  self + field::kTarget);
}

// The game's 0x00774690: the six page buttons. Each shows its page number;
// the page shown has its mark (a second window with the same number) in the
// button's place. The two arrows show when there are pages before the first
// button and Sims past the sixth, and buttons past the last Sim are hidden.
void __fastcall UpdatePages(char* self, void*)
{
    char tag = 0;
    void* const dialog = Dialog(self);
    void* held = nullptr;
    for (uint32_t i = 0; i < 6; ++i) {
        Hold(held, Child(dialog, kPageMarkFirst + i));
        SetFlag(held, 1, 0);
    }
    for (uint32_t i = 0; i < 6; ++i) {
        Hold(held, Child(dialog, kPageButtonFirst + i));
        SetFlag(held, 1, 1);
    }
    for (uint32_t i = 0; i < 6; ++i) {
        const uint32_t button = i + kPageButtonFirst;
        Hold(held, Child(dialog, button));
        if (held) {
            char digits[16];
            Cdecl<char*>(addr::SC4_itoa, static_cast<int32_t>(At<uint32_t>(self, field::kFirstPage) + i + 1),
                         &digits[0], int32_t{10});
            // The string's constructor from characters, inlined here.
            GameString text;
            text.body.begin = nullptr;
            text.body.end = nullptr;
            text.body.capacity = nullptr;
            const uint32_t length = Length(digits);
            char* end = static_cast<char*>(Allocate(length + 1));
            text.body.begin = end;
            text.body.end = end;
            text.body.capacity = end + length + 1;
            if (length != 0) {
                Cdecl<void*>(addr::SC4_memmove, end, &digits[0], length);
                end += length;
            }
            text.body.end = end;
            *end = 0;
            text.vtable = addr::SC4String_Vtable;
            text.zero = 0;
            SetCaption(held, &text);
            DestroyString(text);
        }
        if (At<uint32_t>(self, field::kPage) != At<uint32_t>(self, field::kFirstPage) + i)
            continue;
        const uint32_t mark = i + kPageMarkFirst;
        Hold(held, Child(dialog, mark));
        SetFlag(held, 1, 1);
        Hold(held, Child(dialog, button));
        SetFlag(held, 1, 0);
        Hold(held, Child(dialog, mark));
        if (held) {
            char digits[16];
            Cdecl<char*>(addr::SC4_itoa, static_cast<int32_t>(At<uint32_t>(self, field::kFirstPage) + i + 1),
                         &digits[0], int32_t{10});
            GameString text;
            text.body.begin = nullptr;
            text.body.end = nullptr;
            text.body.capacity = nullptr;
            This<void>(addr::SC4String_FromRange, &text.body, &digits[0], &digits[0] + Length(digits), &tag);
            text.vtable = addr::SC4String_Vtable;
            text.zero = 0;
            SetCaption(held, &text);
            DestroyString(text);
        }
    }
    {
        const uint32_t on = At<uint32_t>(self, field::kFirstPage) != 0;
        Hold(held, Child(dialog, kPagesBack));
        SetFlag(held, 1, on);
    }
    {
        const uint32_t on =
            (At<uint32_t>(self, field::kFirstPage) + 5) * kRowsPerPage < At<uint32_t>(self, field::kCount);
        Hold(held, Child(dialog, kPagesOn));
        SetFlag(held, 1, on);
    }
    for (uint32_t i = 5; i >= 1; --i) {
        if (At<uint32_t>(self, field::kCount) > i * kRowsPerPage)
            continue;
        Hold(held, Child(dialog, kPageButtonFirst + i));
        SetFlag(held, 1, 0);
        Hold(held, Child(dialog, kPageMarkFirst + i));
        SetFlag(held, 1, 0);
    }
    Release(held);
}

// The game's 0x00774FC0: the accept button is enabled when the name has a
// character above a space; that answer is returned (true without a panel or
// a name box).
uint8_t __fastcall CheckName(char* self, void*)
{
    void* const panel = At<void*>(self, field::kPanel);
    if (!panel)
        return 1;
    void* const window = Virtual<void*>(panel, 0xC);
    void* edit = nullptr;
    ChildAs(window, kNameEdit, kEditIid, &edit);
    if (!edit)
        return 1;
    void* const accept = Child(window, kAcceptButton);
    GameString text;
    InitString(text);
    Virtual<void>(edit, 0xB4, &text); // GetText
    uint8_t named = 0;
    const uint32_t length = static_cast<uint32_t>(text.body.end - text.body.begin);
    for (uint32_t i = 0; i < length; ++i) {
        if (static_cast<uint8_t>(text.body.begin[i]) > 0x20) {
            named = 1;
            break;
        }
    }
    SetFlag(accept, 2, named);
    DestroyString(text);
    if (edit)
        Release(edit);
    return named;
}

// The game's 0x007750D0 (slot 3 of the message target at +0xC): the
// recurring message checks the name and is posted again.
uint8_t __fastcall DoMessage(char* target, void*, void* message)
{
    if (Virtual<uint32_t>(message, 0x10) == kPollMessage) {
        char* const self = target - field::kTarget;
        CheckName(self, nullptr);
        PostPoll(self, nullptr);
    }
    return 1;
}

// The picture of the record at `position` put into the picture window `id`:
// from the resources by its instance, or from a file in the MySim folder.
// `picture` is the caller's counted pointer, `tag` an unused byte.
void ShowPicture(char* self, void* dialog, uint32_t position, uint32_t id, void*& picture, void* tag)
{
    Record* const records = At<Record*>(self, field::kRecords);
    uint32_t* const slot = &At<uint32_t*>(self, field::kOrder)[position];
    if (records[*slot].id != 0) {
        ImageLoader image;
        This<void*>(addr::SC4ImageLoader_ConstructParts, &image, uint32_t{0}, kUiGroup, records[*slot].id,
                    uint32_t{1}, uint32_t{0});
        Drop(picture);
        if (ChildAs(dialog, id, kPictureIid, &picture))
            Virtual<void>(picture, 0x10, At<void*>(&image, 0x4));
        This<void>(addr::SC4ImageLoader_Destruct, &image);
    } else {
        GameString path;
        InitString(path);
        Virtual<uint8_t>(Global<void*>(addr::SC4_Service43C94), 0xF4, &path); // GetMySimDirectory
        StringBody* const name = Body(&RecordAt(self, position).picture);
        This<void*>(addr::SC4String_AppendRange, &path.body, name->begin, name->end, tag);
        ImageLoader image;
        This<void*>(addr::SC4ImageLoader_ConstructName, &image, &path, uint32_t{8}, uint32_t{0});
        Drop(picture);
        if (ChildAs(dialog, id, kPictureIid, &picture))
            Virtual<void>(picture, 0x10, At<void*>(&image, 0x4));
        This<void>(addr::SC4ImageLoader_Destruct, &image);
        DestroyString(path);
    }
}

// The game's 0x007751A0: the 21 rows' pictures from position `first` on
// (pulled back so that a full page shows; the whole list when it is shorter
// than a page). The page buttons are redone after every row.
void __fastcall FillList(char* self, void*, uint32_t first)
{
    char tag = 0;
    const uint32_t count = At<uint32_t>(self, field::kCount);
    if (first > count - kRowsPerPage) {
        first = count - kRowsPerPage;
        At<uint32_t>(self, field::kFirst) = first;
    }
    uint32_t end = first + kRowsPerPage;
    if (count < kRowsPerPage) {
        first = 0;
        At<uint32_t>(self, field::kFirst) = 0;
        end = count;
    }
    uint32_t id = kRowPictureFirst;
    for (uint32_t position = first; position < end; ++position) {
        void* picture = nullptr;
        void* const dialog = Dialog(self);
        ShowPicture(self, dialog, position, id, picture, &tag);
        ++id;
        UpdatePages(self, nullptr);
        if (picture)
            Release(picture);
    }
}

// The game's 0x007753A0: the order by flag, Sims without `flag` first, then
// the list again from where it was.
void __fastcall SortByFlag(char* self, void*, uint8_t flag)
{
    uint32_t placed = 0;
    for (uint32_t i = 0; i < At<uint32_t>(self, field::kCount); ++i) {
        if (At<Record*>(self, field::kRecords)[i].flag != flag)
            At<uint32_t*>(self, field::kOrder)[placed++] = i;
    }
    for (uint32_t i = 0; i < At<uint32_t>(self, field::kCount); ++i) {
        if (At<Record*>(self, field::kRecords)[i].flag == flag)
            At<uint32_t*>(self, field::kOrder)[placed++] = i;
    }
    FillList(self, nullptr, At<uint32_t>(self, field::kFirst));
}

// The game's 0x00775410: the order by zodiac sign, up or down (down also
// turns the Sims of one sign around).
void __fastcall SortByZodiac(char* self, void*, uint8_t ascending)
{
    uint32_t placed = 0;
    if (ascending) {
        for (uint32_t sign = 0; sign < 0xC; ++sign) {
            for (uint32_t i = 0; i < At<uint32_t>(self, field::kCount); ++i) {
                if (At<Record*>(self, field::kRecords)[i].zodiac == sign)
                    At<uint32_t*>(self, field::kOrder)[placed++] = i;
            }
        }
    } else {
        for (int32_t sign = 0xB; sign >= 0; --sign) {
            for (int32_t i = At<int32_t>(self, field::kCount) - 1; i >= 0; --i) {
                if (At<Record*>(self, field::kRecords)[i].zodiac == static_cast<uint32_t>(sign))
                    At<uint32_t*>(self, field::kOrder)[placed++] = static_cast<uint32_t>(i);
            }
        }
    }
    FillList(self, nullptr, At<uint32_t>(self, field::kFirst));
}

// The game's 0x007754B0: the order by name, up or down: a bubble sort of
// the order as it stands, with the comparison of the language service.
void __fastcall SortByName(char* self, void*, uint8_t ascending)
{
    void* service = nullptr;
    if (void* const framework = Cdecl<void*>(addr::RZGetFramework))
        Virtual<uint8_t>(framework, 0x14, uint32_t{0x441E5070}, uint32_t{0x0054B7D5}, &service);
    void* const collator = Virtual<void*>(service, 0x98, uint32_t{0});
    int32_t left = At<int32_t>(self, field::kCount);
    while (left > 0) {
        for (int32_t i = 1; i < left; ++i) {
            uint32_t* const order = At<uint32_t*>(self, field::kOrder);
            Record* const records = At<Record*>(self, field::kRecords);
            const int32_t compared =
                Virtual<int32_t>(collator, 0x58, &records[order[i]].name, &records[order[i - 1]].name, uint32_t{0});
            if (ascending ? compared >= 0 : compared <= 0)
                continue;
            uint32_t* const now = At<uint32_t*>(self, field::kOrder);
            const uint32_t before = now[i - 1];
            const uint32_t after = now[i];
            now[i] = before;
            now[i - 1] = after;
        }
        --left;
    }
    FillList(self, nullptr, At<uint32_t>(self, field::kFirst));
    This<void>(addr::SC4_ReleaseRef2, &service);
}

// The text of an edit box copied into a string.
void ReadEdit(void* edit, GameString& into, char* tag)
{
    GameString text;
    This<void*>(addr::SC4String_FromText, &text, Virtual<const char*>(edit, 0xB8));
    char* const begin = text.body.begin;
    This<void*>(addr::SC4String_AssignRange, &into.body, begin, text.body.end, tag);
    if (begin)
        Free(begin);
}

// The game's 0x007755A0: the accept button. The name, the biography and the
// sign are read from the controls and the Sim moves in through the city's
// MySim simulator; then the modal state ends and the window is destroyed.
void __fastcall Accept(char* self, void*)
{
    char tag = 0;
    GameString name;
    InitString(name);
    GameString bio;
    InitString(bio);
    void* const dialog = Dialog(self);
    void* edit = nullptr;
    if (ChildAs(dialog, kNameEdit, kEditIid, &edit))
        ReadEdit(edit, name, &tag);
    Drop(edit);
    if (ChildAs(dialog, kBioEdit, kEditIid, &edit))
        ReadEdit(edit, bio, &tag);
    void* combo = nullptr;
    uint32_t zodiac;
    if (ChildAs(dialog, kZodiacCombo, kComboIid, &combo))
        zodiac = Virtual<uint32_t>(combo, 0x78);
    else
        zodiac = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(combo));
    if (void* const city = Global<void*>(addr::SC4_City)) {
        if (void* const mySims = Virtual<void*>(city, 0x11C)) {
            Virtual<void>(mySims, 0x18, &name, uint32_t{At<uint8_t>(self, field::kPickedFlag)}, zodiac, &bio,
                          At<uint32_t>(self, field::kPictureId), self + field::kPictureName,
                          uint32_t{At<uint8_t>(self, field::kPlace)});
        }
    }
    if (At<uint8_t>(self, field::kModal)) {
        void* const manager = Cdecl<void*>(addr::SC4_GetWinManager);
        void* const window = PanelWindow(self);
        Virtual<void>(manager, 0x24, uint32_t{0xF}, window, uint32_t{0}); // GZWinMsgPost
        At<uint8_t>(self, field::kModal) = 0;
    }
    if (void* const manager = Virtual<void*>(PanelWindow(self), 0x18)) { // GetWindowManager
        void* const window = PanelWindow(self);
        Virtual<void>(manager, 0x5C, window); // DestroyWindow
    }
    if (combo)
        Release(combo);
    if (edit)
        Release(edit);
    DestroyString(bio);
    DestroyString(name);
}

// The game's 0x007757F0: the Sim in row `row` of the page picked: its
// picture in the large picture window, its name in the name box, one of two
// texts by its flag, its sign in the combo. The picture's file name at
// +0x28 changes only for a Sim whose picture is a file.
void __fastcall Select(char* self, void*, uint32_t row)
{
    const uint32_t position = At<uint32_t>(self, field::kFirst) + row;
    // The game uses the argument's stack slot for this pointer, and its
    // address as the unused tag of the string calls.
    void* picture = nullptr;
    void* const dialog = Dialog(self);
    ShowPicture(self, dialog, position, kPickedPicture, picture, &picture);
    {
        Record* const records = At<Record*>(self, field::kRecords);
        uint32_t* const slot = &At<uint32_t*>(self, field::kOrder)[position];
        const uint32_t id = records[*slot].id;
        At<uint32_t>(self, field::kPictureId) = id;
        if (id == 0) {
            StringBody* const name = Body(&records[*slot].picture);
            StringBody* const mine = reinterpret_cast<StringBody*>(self + field::kPictureName + 0x4);
            if (name != mine)
                This<void*>(addr::SC4String_AssignRange, mine, name->begin, name->end, &picture);
        }
    }
    void* edit = nullptr;
    if (ChildAs(dialog, kNameEdit, kEditIid, &edit))
        Virtual<void>(edit, 0xC0, &RecordAt(self, position).name, uint32_t{0}); // SetText
    void* const flagText = Child(dialog, kFlagText);
    LocalizedText text;
    if (RecordAt(self, position).flag != 0) {
        void* const made =
            This<void*>(addr::SC4LocalizedString_SC4LocalizedString, &text, kTextGroup, uint32_t{0x8A57920A},
                        uint32_t{1});
        SetCaption(flagText, made);
        DestroyString(text.text);
        At<uint8_t>(self, field::kPickedFlag) = 1;
    } else {
        void* const made =
            This<void*>(addr::SC4LocalizedString_SC4LocalizedString, &text, kTextGroup, uint32_t{0x8A579224},
                        uint32_t{1});
        SetCaption(flagText, made);
        DestroyString(text.text);
        At<uint8_t>(self, field::kPickedFlag) = 0;
    }
    void* combo = nullptr;
    if (ChildAs(dialog, kZodiacCombo, kComboIid, &combo))
        Virtual<void>(combo, 0x70, uint32_t{RecordAt(self, position).zodiac}, uint32_t{0}); // select
    At<uint32_t>(self, field::kPicked) = position;
    if (combo)
        Release(combo);
    if (edit)
        Release(edit);
    if (picture)
        Release(picture);
}

// The game's 0x00775BE0 (the string in EAX, two arguments on the stack,
// caller cleans): the first '#' of the text replaced by a number as the
// language service writes it. False when the text is empty or has no '#'.
[[maybe_unused]] uint8_t __cdecl PutNumber(GameString* text, uint32_t value, uint32_t option)
{
    StringBody* const body = &text->body;
    char* const begin = body->begin;
    char* const end = body->end;
    if (end == begin)
        return 0;
    char tag = 0;
    char* const found = Cdecl<char*>(addr::STL_FindChar, begin, end, uint32_t{'#'}, &tag);
    if (found == end)
        return 0;
    const uint32_t position = static_cast<uint32_t>(found - begin);
    if (position == 0xFFFFFFFF)
        return 0;
    GameString number;
    InitString(number);
    void* const app = Cdecl<void*>(addr::SC4_CurrentApp);
    void* const language = Virtual<void*>(app, 0x98, uint32_t{0});
    Virtual<void>(language, 0x48, value, option, &number);
    This<void*>(addr::SC4UI_F775B50, body, position, uint32_t{1}, &number.body); // replace
    DestroyString(number);
    return 1;
}

__declspec(naked) void PutNumberThunk()
{
    __asm push dword ptr [esp + 8]
    __asm push dword ptr [esp + 8]
    __asm push eax
    __asm call PutNumber
    __asm add esp, 12
    __asm ret
}

// The game's 0x00775CC0 (one function up to 0x00776550): runs the dialog.
// Loads the layout (resource key 0, 0x96A006B0, 0x0A243D80), becomes its
// window procedure, limits the two edit boxes to 128 and 2048 characters,
// fills the sign combo, makes the picker's list and takes its Sims, shows
// the first page with the first Sim picked, and stays in the window
// manager's modal loop until the dialog ends. The focus goes back to the
// window that had it.
void __fastcall Run(char* self, void*)
{
    char tag = 0;
    void* const manager = Cdecl<void*>(addr::SC4_GetWinManager);
    void* const focus = Virtual<void*>(manager, 0x90); // GZGetFocus
    if (focus)
        AddRef(focus);
    void*& panelSlot = At<void*>(self, field::kPanel);
    uint32_t key[3] = {0, 0x96A006B0, 0x0A243D80};
    Drop(panelSlot);
    if (Cdecl<uint8_t>(addr::SC4UI_LoadWindow, &key[0], static_cast<void*>(nullptr), kPanelWindow, kWinGenIid,
                       &panelSlot))
        Virtual<void*>(panelSlot, 0x1C, self + field::kWinProc); // SetWinProc
    void* const panel = Virtual<void*>(panelSlot, 0xC);
    if (panel)
        AddRef(panel);
    void* const dialog = Virtual<void*>(panel, 0x2C);
    if (dialog)
        AddRef(dialog);
    void* edit = nullptr;
    SetFlag(Child(Dialog(self), kWaitWindow), 1, 0);
    Drop(edit);
    if (ChildAs(dialog, kNameEdit, kEditIid, &edit))
        Virtual<void>(edit, 0x84, uint32_t{0x80}); // SetMaxLength
    Drop(edit);
    if (ChildAs(dialog, kBioEdit, kEditIid, &edit))
        Virtual<void>(edit, 0x84, uint32_t{0x800});
    Virtual<void>(Child(dialog, kNotifier), 0x158, panel); // SetNotificationTarget
    void* combo = nullptr;
    if (ChildAs(dialog, kZodiacCombo, kComboIid, &combo)) {
        Virtual<void>(combo, 0x68);
        for (const uint32_t instance : kZodiacNames) {
            LocalizedText name;
            void* const made =
                This<void*>(addr::SC4LocalizedString_SC4LocalizedString, &name, kZodiacGroup, instance, uint32_t{1});
            GameString copy;
            This<void>(addr::SC4String_CopyBuffer, &copy.body, Body(made), uint32_t{0}, uint32_t{0xFFFFFFFF}, &tag);
            copy.vtable = addr::SC4String_Vtable;
            copy.zero = 0;
            Virtual<void>(combo, 0x6C, &copy, uint32_t{0xFFFFFFFE});
            DestroyString(copy);
            DestroyString(name.text);
        }
    }
    void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x2C});
    void* const sims = memory ? This<void*>(addr::SC4UI_F43B3B0, memory) : nullptr;
    At<void*>(self, field::kSims) = sims;
    RecordVector* const records = &At<RecordVector>(self, field::kRecords);
    Virtual<void*>(sims, 0x18, records); // GetSims
    At<int32_t>(self, field::kCount) =
        static_cast<int32_t>(reinterpret_cast<char*>(records->end) - reinterpret_cast<char*>(records->begin)) / 0x30;
    At<uint32_t*>(self, field::kOrder) =
        static_cast<uint32_t*>(Cdecl<void*>(addr::SC4_operator_new, At<uint32_t>(self, field::kCount) << 2));
    for (uint32_t i = 0; i < At<uint32_t>(self, field::kCount); ++i)
        At<uint32_t*>(self, field::kOrder)[i] = i;
    if (void* const import = Child(panel, kImportButton))
        SetFlag(import, 2, Virtual<uint32_t>(At<void*>(self, field::kSims), 0x20)); // The Sims installed
    FillList(self, nullptr, At<uint32_t>(self, field::kFirst));
    Select(self, nullptr, 0);
    UpdatePages(self, nullptr);
    PostPoll(self, nullptr);
    At<uint8_t>(self, field::kModal) = 1;
    ModalGuard guard;
    This<void*>(addr::SC4ModalGuard_Construct, &guard, uint32_t{0}, uint32_t{1}, uint32_t{1}, uint32_t{1});
    Virtual<void>(Global<void*>(addr::SC4_Service43CE0), 0x28, panel, uint32_t{1});
    Virtual<void>(manager, 0xA4, panel); // DoModalWin
    Virtual<void>(Global<void*>(addr::SC4_Service43CE0), 0x18, panel);
    This<void>(addr::SC4ModalGuard_Destruct, &guard);
    if (Virtual<uint8_t>(manager, 0x60, focus)) // IsWindowValid
        Virtual<void>(manager, 0x94, focus);    // GZSetFocus
    if (combo)
        Release(combo);
    if (edit)
        Release(edit);
    Release(dialog);
    Release(panel);
    if (focus)
        Release(focus);
}

bool InstallMySimDialog1(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[16] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F774620, reinterpret_cast<void*>(&PostPoll)},
        {addr::SC4UI_F774690, reinterpret_cast<void*>(&UpdatePages)},
        {addr::SC4UI_F774FC0, reinterpret_cast<void*>(&CheckName)},
        {addr::SC4UI_F7750D0, reinterpret_cast<void*>(&DoMessage)},
        {addr::SC4UI_F7751A0, reinterpret_cast<void*>(&FillList)},
        {addr::SC4UI_F7753A0, reinterpret_cast<void*>(&SortByFlag)},
        {addr::SC4UI_F775410, reinterpret_cast<void*>(&SortByZodiac)},
        {addr::SC4UI_F7754B0, reinterpret_cast<void*>(&SortByName)},
        {addr::SC4UI_F7755A0, reinterpret_cast<void*>(&Accept)},
        {addr::SC4UI_F7757F0, reinterpret_cast<void*>(&Select)},
        {addr::SC4UI_F775BE0, reinterpret_cast<void*>(&PutNumberThunk)},
        {addr::SC4UI_F775CC0, reinterpret_cast<void*>(&Run)},
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

const bool g_mySimDialog1Registered = registry::Register({
    "ui.mysim_dialog.part1",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F775CC0,
    "Our MySim move-in dialog: its run, list, page buttons, selection and accept (docs/ui.md)",
    false,
    InstallMySimDialog1,
});

} // namespace
} // namespace opensc4::ui
