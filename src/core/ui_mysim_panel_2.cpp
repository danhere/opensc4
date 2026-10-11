// The second half of the code listed as the MySim panel (0x00772B60 to
// 0x00774612). Most of it is not about MySims: it is the city view's news
// ticker, an object of 0x84 bytes (vtables 0x00AB5164 and 0x00AB5150 at +4)
// that the city view's window owner makes with operator new (0x007EE1FE) and
// keeps at its +0x2DC. It listens for the advisors' advice messages, keeps
// the advices sorted by priority, fills the news list control with one HTML
// row per advice and shows up to three of them in the ticker, which a timer
// scrolls. Then come three small pieces: a window procedure of 8 bytes
// (vtable 0x00AB51E4, at the owner's +0x2D8) that turns five button clicks
// into commands, the QueryInterface of the dialog family's base, and the
// function that closes such a dialog. None of it draws. Replacement, off by
// default:
//
//   ui.mysim_panel.part2: the ticker's message handler, list keeping, row
//     building, constructor and destructor; the button procedure; the
//     dialog's QueryInterface and Close.
//
// The ticker: +0 main vtable (a message target), +4 window procedure vtable,
// +8 reference count, +0xC started, +0xD listening, +0x10 timer (0x28 bytes),
// +0x38 news list control, +0x3C ticker control, +0x40 list of advices,
// +0x44 the panel's window, +0x48 and +0x49 flag bytes, +0x4C the three
// advices in the ticker, +0x5C scroll step, +0x64 the ticker's rectangle,
// +0x74 advices in the ticker, +0x78 ticker moved up, +0x7C redraw wanted,
// +0x80 steps of one scroll. The 30 HTML pieces are static strings of the
// game (0x00B4DD6C on, 0x14 bytes each), built before main.
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
constexpr std::size_t kWinProc = 0x4;
constexpr std::size_t kTimer = 0x10;
constexpr std::size_t kList = 0x38;
constexpr std::size_t kTicker = 0x3C;
constexpr std::size_t kAdvices = 0x40;
constexpr std::size_t kWindow = 0x44;
constexpr std::size_t kTops = 0x4C;
constexpr std::size_t kScrollStep = 0x5C;
constexpr std::size_t kTickerArea = 0x64;
constexpr std::size_t kTopCount = 0x74;
constexpr std::size_t kMovedUp = 0x78;
constexpr std::size_t kRedraw = 0x7C;
} // namespace field

// The static strings at 0x00B4DD6C, in order.
namespace piece {
constexpr int32_t kGrey = 0;         // <FONT COLOR="#8e9499"
constexpr int32_t kBlue = 2;         // <FONT COLOR="#3f4967"
constexpr int32_t kGreen = 3;        // <FONT COLOR="#095a22"
constexpr int32_t kRed = 4;          // <FONT COLOR="#b00000"
constexpr int32_t kBlinkRed = 5;     // <BLINK><FONT COLOR="#f70a0b"
constexpr int32_t kTitleClose = 7;   // </B></FONT>
constexpr int32_t kMessageClose = 8; // </I></FONT>
constexpr int32_t kBlinkClose = 9;   // </B></FONT></BLINK>
constexpr int32_t kFace = 10;        //  FACE="StocletITC TT" SIZE=3><B>
constexpr int32_t kFaceSmall = 11;   //  FACE="StocletITC TT" SIZE=2><B>
constexpr int32_t kMessageBlue = 12; // <FONT COLOR="#3f4967" FACE="Arta" SIZE=3><I>
constexpr int32_t kMessageGrey = 13; // <FONT COLOR="#8e9499" FACE="Arta" SIZE=3><I>
constexpr int32_t kImages = 14;      // sixteen <IMG SRC="sc4://HTML/46a006b0/144162xx">
} // namespace piece

constexpr uint32_t kListStyle = 0x14416264;
constexpr uint32_t kTickerStyle = 0x14416265;
constexpr uint32_t kTopSlots = 3;
constexpr uint32_t kUrgent = 0x3B;


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

struct ListNode {
    ListNode* next;
    ListNode* previous;
    void* value;
};

struct Area {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
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

void* Allocate(uint32_t size)
{
    return Cdecl<void*>(addr::SC4_stl_allocate, size);
}

void Free(void* memory)
{
    Cdecl<void>(addr::SC4_Free, memory);
}

// The game's smart pointer assignment, as inlined.
void AssignRef(void*& slot, void* value)
{
    void* const old = slot;
    if (value == old)
        return;
    if (value)
        AddRef(value);
    slot = value;
    if (old)
        Release(old);
}

GameString* Piece(int32_t index)
{
    return reinterpret_cast<GameString*>(addr::SC4NewsTicker_Strings + 0x14 * static_cast<std::uintptr_t>(index));
}

void MakeEmpty(GameString& text)
{
    char* const memory = static_cast<char*>(Allocate(8));
    *memory = 0;
    text.body.begin = memory;
    text.body.end = memory;
    text.body.capacity = memory + 8;
    text.vtable = addr::SC4String_Vtable;
    text.zero = 0;
}

void MakeCopy(GameString& text, const char* begin, const char* end, uint8_t* tag)
{
    text.body.begin = nullptr;
    text.body.end = nullptr;
    text.body.capacity = nullptr;
    This<void>(addr::cSC4Advice_F408420, &text.body, begin, end, tag);
    text.vtable = addr::SC4String_Vtable;
    text.zero = 0;
}

// A copy of a piece made by the game's substring constructor.
void MakeCopyOfPiece(GameString& text, int32_t index, uint8_t* tag)
{
    This<void>(addr::SC4String_CopyBuffer, &text.body, &Piece(index)->body, uint32_t{0}, uint32_t{0xFFFFFFFF}, tag);
    text.vtable = addr::SC4String_Vtable;
    text.zero = 0;
}

void Assign(GameString& text, int32_t index, uint8_t* tag)
{
    const StringBody& from = Piece(index)->body;
    This<void*>(addr::SC4String_AssignRange, &text.body, from.begin, from.end, tag);
}

void AssignString(GameString& text, int32_t index)
{
    This<void*>(addr::cRZBaseString_Assign, &text, Piece(index));
}

void DestroyString(GameString& text)
{
    text.vtable = addr::SC4String_Vtable;
    if (text.body.begin)
        Free(text.body.begin);
}

void FreeBody(StringBody& body)
{
    if (body.begin)
        Free(body.begin);
}

// The body of a string, as the game's casts work it out.
StringBody* Body(void* text)
{
    return text ? reinterpret_cast<StringBody*>(static_cast<char*>(text) + 0x4) : nullptr;
}

void*& Top(char* self, uint32_t index)
{
    return At<void*>(self, field::kTops + 4 * index);
}

// Takes a node out of the advice list: unlinked, its advice released, freed.
void EraseNode(ListNode* node)
{
    ListNode* const next = node->next;
    ListNode* const previous = node->previous;
    previous->next = next;
    next->previous = previous;
    if (node->value)
        Release(node->value);
    Free(node);
}

// A new node holding the advice, linked in before `position`.
void InsertBefore(ListNode* position, void* advice)
{
    ListNode* const node = static_cast<ListNode*>(Allocate(0xC));
    node->value = advice;
    if (advice)
        AddRef(advice);
    ListNode* const previous = position->previous;
    node->next = position;
    node->previous = previous;
    previous->next = node;
    position->previous = node;
}

// The game's 0x00772E90 (slot 7): takes the advice out of the list.
void __fastcall Remove(char* self, void*, void* advice)
{
    ListNode* const end = At<ListNode*>(self, field::kAdvices);
    for (ListNode* node = end->next; node != end; node = node->next) {
        if (node->value == advice) {
            EraseNode(node);
            return;
        }
    }
}

// The game's 0x00772EE0 (slot 11): drops every advice that is no longer
// active.
void __fastcall Purge(char* self, void*)
{
    ListNode* const end = At<ListNode*>(self, field::kAdvices);
    ListNode* node = end->next;
    while (node != end) {
        if (!Virtual<uint8_t>(node->value, 0x3C, uint32_t{2})) { // CheckFlags
            ListNode* const next = node->next;
            EraseNode(node);
            node = next;
        } else {
            node = node->next;
        }
    }
}

// The game's 0x00772F30 (slot 8): adds an advice the list does not hold yet,
// before the first one whose priority is not above its own.
void __fastcall Add(char* self, void*, void* advice)
{
    ListNode* const end = At<ListNode*>(self, field::kAdvices);
    for (ListNode* node = end->next; node != end; node = node->next) {
        void* const held = node->value;
        if (held)
            AddRef(held);
        if (advice == held) {
            if (held)
                Release(held);
            return;
        }
        if (held)
            Release(held);
    }
    for (ListNode* node = At<ListNode*>(self, field::kAdvices)->next; node != end; node = node->next) {
        void* const held = node->value;
        const int32_t priority = Virtual<int32_t>(advice, 0x1C);
        if (priority >= Virtual<int32_t>(held, 0x1C)) {
            AddRef(advice);
            InsertBefore(node, advice);
            Release(advice);
            return;
        }
    }
    if (advice)
        AddRef(advice);
    InsertBefore(At<ListNode*>(self, field::kAdvices), advice);
    if (advice)
        Release(advice);
}

// The game's 0x00773050: the constructor.
char* __fastcall Construct(char* self, void*)
{
    At<std::uintptr_t>(self, field::kWinProc) = addr::SC4NewsTicker_WinProcBaseVtable;
    At<std::uintptr_t>(self, 0x0) = addr::SC4NewsTicker_Vtable;
    At<std::uintptr_t>(self, field::kWinProc) = addr::SC4NewsTicker_WinProcVtable;
    At<uint32_t>(self, 0x8) = 0;
    At<uint8_t>(self, 0xC) = 0;
    At<uint8_t>(self, 0xD) = 0;
    This<void*>(addr::cRZTimer_Construct, self + field::kTimer);
    At<void*>(self, field::kList) = nullptr;
    At<void*>(self, field::kTicker) = nullptr;
    At<void*>(self, field::kAdvices) = nullptr;
    ListNode* const end = static_cast<ListNode*>(Allocate(0xC));
    end->next = end;
    end->previous = end;
    At<ListNode*>(self, field::kAdvices) = end;
    At<void*>(self, field::kWindow) = nullptr;
    At<uint8_t>(self, 0x49) = 0;
    At<uint8_t>(self, 0x48) = 1;
    for (uint32_t index = 0; index < kTopSlots; ++index)
        Top(self, index) = nullptr;
    At<uint8_t>(self, 0x58) = 0;
    At<uint32_t>(self, field::kScrollStep) = 0;
    At<uint32_t>(self, 0x60) = 0;
    At<Area>(self, field::kTickerArea) = Area{0, 0, 0, 0};
    At<uint32_t>(self, field::kTopCount) = 0;
    At<uint32_t>(self, field::kMovedUp) = 0;
    At<uint32_t>(self, field::kRedraw) = 1;
    for (uint32_t index = 0; index < kTopSlots; ++index) {
        if (void* const top = Top(self, index)) {
            Top(self, index) = nullptr;
            Release(top);
        }
    }
    return self;
}

// The game's 0x00773100: the destructor.
void __fastcall Destruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4NewsTicker_Vtable;
    At<std::uintptr_t>(self, field::kWinProc) = addr::SC4NewsTicker_WinProcVtable;
    for (uint32_t index = kTopSlots; index-- > 0;) {
        if (void* const top = Top(self, index))
            Release(top);
    }
    if (void* const window = At<void*>(self, field::kWindow))
        Release(window);
    This<void>(addr::SC4RefList_Clear, self + field::kAdvices);
    if (void* const end = At<void*>(self, field::kAdvices))
        Free(end);
    if (void* const ticker = At<void*>(self, field::kTicker))
        Release(ticker);
    if (void* const list = At<void*>(self, field::kList))
        Release(list);
    This<void>(addr::cRZTimer_Destruct, self + field::kTimer);
}

// The game's 0x00773180 (slot 4).
char* __fastcall DeletingDestruct(char* self, void*, uint32_t flags)
{
    Destruct(self, nullptr);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// The game's 0x007731A0 (slot 9): fills the news list with a row per advice:
// a title and a message in the colours of the advice's mood (grey when the
// advice is not active) and the mood's images.
void __fastcall FillList(char* self, void*)
{
    if (!At<void*>(self, field::kList))
        return;
    Virtual<void>(At<void*>(self, field::kList), 0x1C);
    Virtual<void>(At<void*>(self, field::kList), 0x18, kListStyle);

    ListNode* const end = At<ListNode*>(self, field::kAdvices);
    for (ListNode* node = end->next; node != end; node = node->next) {
        void* const advice = node->value;
        const int32_t mood = Virtual<int32_t>(advice, 0x18); // GetMood
        uint8_t tag = 0;
        GameString titleOpen;
        GameString titleClose;
        GameString images[4];
        GameString messageOpen;
        MakeEmpty(titleOpen);
        MakeCopy(titleClose, Piece(piece::kTitleClose)->body.begin, Piece(piece::kTitleClose)->body.end, &tag);
        MakeEmpty(images[0]);
        MakeEmpty(images[1]);
        MakeEmpty(images[2]);
        MakeEmpty(images[3]);
        MakeCopy(messageOpen, Piece(piece::kMessageBlue)->body.begin, Piece(piece::kMessageBlue)->body.end, &tag);

        int32_t firstImage;
        switch (mood) {
        case 0:
            Assign(titleOpen, piece::kRed, &tag);
            firstImage = piece::kImages;
            break;
        case 1:
            Assign(titleOpen, piece::kRed, &tag);
            firstImage = piece::kImages + 4;
            break;
        case 5:
            Assign(titleOpen, piece::kGreen, &tag);
            firstImage = piece::kImages + 12;
            break;
        default:
            Assign(titleOpen, piece::kBlue, &tag);
            firstImage = piece::kImages + 8;
            break;
        }
        for (int32_t image = 0; image < 4; ++image)
            Assign(images[image], firstImage + image, &tag);

        if (!Virtual<uint8_t>(advice, 0x3C, uint32_t{2})) { // CheckFlags
            Assign(titleOpen, piece::kGrey, &tag);
            for (int32_t image = 0; image < 4; ++image)
                Assign(images[image], piece::kImages + 8 + image, &tag);
            Assign(messageOpen, piece::kMessageGrey, &tag);
        }

        StringBody joined1;
        StringBody joined2;
        StringBody joined3;
        const char* text = Virtual<const char*>(Virtual<void*>(advice, 0x48), 0x14); // GetTextTitle, ToChar
        StringBody* whole =
            Cdecl<StringBody*>(addr::cSC4Advice_F408AE0, &joined1, &titleOpen.body, &Piece(piece::kFace)->body);
        whole = Cdecl<StringBody*>(addr::SC4String_ConcatBodyText, &joined2, whole, text);
        whole = Cdecl<StringBody*>(addr::cSC4Advice_F408AE0, &joined3, whole, &titleClose.body);
        GameString title;
        MakeCopy(title, whole->begin, whole->end, &tag);
        FreeBody(joined3);
        FreeBody(joined2);
        FreeBody(joined1);

        void* const list = At<void*>(self, field::kList);
        if (mood == 7) {
            Virtual<void>(list, 0x10, &images[0], &images[0], &title, &title, uint32_t{0}, advice, &images[3],
                          uint32_t{1}, kListStyle);
        } else {
            text = Virtual<const char*>(Virtual<void*>(advice, 0x50), 0x14); // GetTextMessage, ToChar
            whole = Cdecl<StringBody*>(addr::SC4String_ConcatBodyText, &joined1, &messageOpen.body, text);
            whole = Cdecl<StringBody*>(addr::cSC4Advice_F408AE0, &joined2, whole,
                                       &Piece(piece::kMessageClose)->body);
            GameString message;
            MakeCopy(message, whole->begin, whole->end, &tag);
            FreeBody(joined2);
            FreeBody(joined1);
            // The game pushes the whole dword its byte result sits in; the
            // other three bytes are whatever the stack held.
            const uint32_t flagged = Virtual<uint8_t>(advice, 0x3C, uint32_t{0x20});
            Virtual<void>(At<void*>(self, field::kList), 0x10, &images[2], &images[1], &message, &title, flagged,
                          advice, &images[3], uint32_t{0}, kListStyle);
            DestroyString(message);
        }

        DestroyString(title);
        DestroyString(messageOpen);
        DestroyString(images[3]);
        DestroyString(images[2]);
        DestroyString(images[1]);
        DestroyString(images[0]);
        DestroyString(titleClose);
        DestroyString(titleOpen);
    }

    Virtual<void>(At<void*>(self, field::kList), 0x14, uint32_t{1});
}

// row = colour, the title face, the text and the closing tags.
void JoinRow(GameString& row, GameString& colour, int32_t face, void* text, GameString& close, uint8_t* tag)
{
    StringBody* const textBody = Body(text);
    StringBody joined1;
    StringBody joined2;
    StringBody joined3;
    StringBody* whole = Cdecl<StringBody*>(addr::cSC4Advice_F408AE0, &joined1, &colour.body, &Piece(face)->body);
    whole = Cdecl<StringBody*>(addr::cSC4Advice_F408AE0, &joined2, whole, textBody);
    whole = Cdecl<StringBody*>(addr::cSC4Advice_F408AE0, &joined3, whole, &close.body);
    GameString joined;
    This<void*>(addr::cSC4Advice_F408B90, &joined, whole);
    char* const begin = joined.body.begin;
    This<void*>(addr::SC4String_AssignRange, &row.body, begin, joined.body.end, tag);
    if (begin)
        Free(begin);
    FreeBody(joined3);
    FreeBody(joined2);
    FreeBody(joined1);
}

// Hands the ticker control one row.
void AddTickerRow(char* self, GameString& row, void* advice)
{
    void* const ticker = At<void*>(self, field::kTicker);
    GameString second;
    GameString first;
    MakeEmpty(second);
    MakeEmpty(first);
    GameString empty;
    This<void*>(addr::SC4String_FromText, &empty, "");
    Virtual<void>(ticker, 0x10, &first, &second, &row, &row, uint32_t{0}, advice, &empty, uint32_t{0},
                  kTickerStyle);
    DestroyString(first);
    DestroyString(second);
    DestroyString(empty);
}

// The game's 0x00773980: gives the ticker its next row: the advice's title
// in the colour of its mood, or a blank row for no advice. The title is one
// size smaller when the application's slot 7 answers 15.
void __fastcall SetTickerRow(char* self, void*, void* advice)
{
    uint8_t tag = 0;
    GameString colour;
    GameString close;
    GameString row;
    GameString text;
    int32_t language;
    if (advice) {
        const int32_t mood = Virtual<int32_t>(advice, 0x18); // GetMood
        MakeEmpty(colour);
        MakeCopyOfPiece(close, piece::kTitleClose, &tag);
        switch (mood) {
        case 0:
            AssignString(close, piece::kBlinkClose);
            AssignString(colour, piece::kBlinkRed);
            break;
        case 1:
            AssignString(colour, piece::kRed);
            break;
        case 3:
            AssignString(colour, piece::kBlue);
            break;
        case 5:
            AssignString(colour, piece::kGreen);
            break;
        default:
            Assign(colour, piece::kBlue, &tag);
            break;
        }
        language = Virtual<int32_t>(Cdecl<void*>(addr::SC4_CurrentApp), 0x1C);
        MakeEmpty(row);
        void* const title = Virtual<void*>(advice, 0x48); // GetTextTitle
        This<void*>(addr::SC4String_FromIGZString, &text, title);
    } else {
        MakeCopyOfPiece(colour, piece::kBlue, &tag);
        MakeCopyOfPiece(close, piece::kTitleClose, &tag);
        language = Virtual<int32_t>(Cdecl<void*>(addr::SC4_CurrentApp), 0x1C);
        MakeEmpty(row);
        This<void*>(addr::SC4String_FromText, &text, " ");
    }
    JoinRow(row, colour, language == 0xF ? piece::kFaceSmall : piece::kFace, &text, close, &tag);
    DestroyString(text);
    AddTickerRow(self, row, advice);
    DestroyString(row);
    FreeBody(close.body);
    FreeBody(colour.body);
}

void ShiftTops(char* self)
{
    for (uint32_t index = 0; index < kTopSlots - 1; ++index) {
        AssignRef(Top(self, index), Top(self, index + 1));
        At<uint32_t>(self, field::kMovedUp) = 1;
    }
}

enum class Next { kNone, kFirst, kNew };

// An active advice of at least the threshold's priority.
bool Wanted(void* advice, uint32_t threshold)
{
    return Virtual<uint8_t>(advice, 0x3C, uint32_t{2}) && Virtual<uint32_t>(advice, 0x1C) >= threshold;
}

// Looks for the advice the ticker shows next: one after the last shown, else
// the first in the list; only urgent ones (priority 59 and up) while the list
// has an active advice above 59. `found` comes back holding one reference.
Next FindNext(char* self, void* last, uint32_t lastPriority, bool& repeated, void*& found)
{
    ListNode* end = At<ListNode*>(self, field::kAdvices);
    if (end->next == end)
        return Next::kNone;
    uint32_t threshold = 0;
    bool urgent = false;
    for (ListNode* node = end->next; node != end; node = node->next) {
        void* const advice = node->value;
        if (advice)
            AddRef(advice);
        if (Virtual<uint8_t>(advice, 0x3C, uint32_t{2}) &&
            Virtual<int32_t>(advice, 0x1C) > static_cast<int32_t>(kUrgent)) {
            threshold = kUrgent;
            if (advice)
                Release(advice);
            urgent = true;
            break;
        }
        Release(advice);
    }
    if (!urgent || lastPriority >= kUrgent) {
        end = At<ListNode*>(self, field::kAdvices);
        bool after = false;
        for (ListNode* node = end->next; node != end; node = node->next) {
            void* const advice = node->value;
            if (advice)
                AddRef(advice);
            if (after) {
                if (Wanted(advice, threshold)) {
                    const uint32_t count = At<uint32_t>(self, field::kTopCount);
                    if (count == 0) {
                        found = advice;
                        return Next::kFirst;
                    }
                    if (advice != Top(self, count - 1)) {
                        found = advice;
                        return Next::kNew;
                    }
                    repeated = true;
                }
            } else if (advice == last) {
                after = true;
            }
            if (advice)
                Release(advice);
        }
    }
    end = At<ListNode*>(self, field::kAdvices);
    for (ListNode* node = end->next; node != end; node = node->next) {
        void* const advice = node->value;
        if (advice)
            AddRef(advice);
        if (Wanted(advice, threshold)) {
            const uint32_t count = At<uint32_t>(self, field::kTopCount);
            if (count == 0 || advice != Top(self, count - 1)) {
                found = advice;
                return Next::kNew;
            }
            repeated = true;
        }
        Release(advice);
    }
    return Next::kNone;
}

// The game's 0x00774070 (slot 10): moves the ticker on by one advice and
// gives the ticker control its three rows anew.
void __fastcall UpdateTicker(char* self, void*)
{
    char* const buffer = static_cast<char*>(Allocate(8));
    *buffer = 0;
    bool repeated = false;
    void* last = nullptr;
    void* fresh = nullptr;
    uint32_t lastPriority = 0;
    At<uint32_t>(self, field::kTopCount) = 0;
    At<uint32_t>(self, field::kMovedUp) = 0;
    if (At<void*>(self, field::kTicker)) {
        Virtual<void>(At<void*>(self, field::kTicker), 0x1C);
        Virtual<void>(At<void*>(self, field::kTicker), 0x18, kTickerStyle);
        for (uint32_t index = 0; index < kTopSlots; ++index) {
            void* const top = Top(self, index);
            if (!top)
                continue;
            AssignRef(last, top);
            lastPriority = Virtual<uint32_t>(last, 0x1C);
            ++At<uint32_t>(self, field::kTopCount);
        }

        void* found = nullptr;
        Next next = FindNext(self, last, lastPriority, repeated, found);
        if (next == Next::kFirst) {
            const uint32_t count = At<uint32_t>(self, field::kTopCount);
            void*& slot = Top(self, count);
            At<uint32_t>(self, field::kTopCount) = count + 1;
            AssignRef(slot, found);
            if (found)
                Release(found);
            next = Next::kNone;
        } else if (next == Next::kNew && !found) {
            next = Next::kNone;
        }
        if (next == Next::kNew) {
            AddRef(found);
            fresh = found;
            Release(found);
            if (Top(self, 2)) {
                ShiftTops(self);
                --At<uint32_t>(self, field::kTopCount);
            }
            const uint32_t count = At<uint32_t>(self, field::kTopCount);
            void*& slot = Top(self, count);
            At<uint32_t>(self, field::kTopCount) = count + 1;
            AssignRef(slot, fresh);
        } else {
            if (Top(self, 1) || !repeated)
                ShiftTops(self);
            if (void* const third = Top(self, 2)) {
                Top(self, 2) = nullptr;
                Release(third);
            }
        }

        for (uint32_t index = 0; index < kTopSlots; ++index)
            SetTickerRow(self, nullptr, Top(self, index));
        if (At<uint32_t>(self, field::kRedraw)) {
            Virtual<void>(At<void*>(self, field::kTicker), 0x14, uint32_t{1});
            At<uint32_t>(self, field::kRedraw) = 0;
        }
        if (fresh)
            Release(fresh);
        if (last)
            Release(last);
    }
    Free(buffer);
}




// The game's 0x00774550: the button procedure's constructor.
char* __fastcall ButtonsConstruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4CommandButtons_Vtable;
    At<uint32_t>(self, 0x4) = 0;
    return self;
}

// The game's 0x00774450 (slot 5).
char* __fastcall ButtonsDeletingDestruct(char* self, void*, uint32_t flags)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4CommandButtons_Vtable;
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// The game's 0x00774470 (slot 3): a click on one of five buttons runs that
// button's command.
uint8_t __fastcall ButtonsDoWinMsg(char*, void*, uint32_t, uint32_t* message)
{
    if (message[0] != 3 || message[1] != 0x287259F6)
        return 0;
    if (!Global<void*>(addr::SC4_Service43DC4))
        return 0;
    uint32_t command;
    switch (message[2]) {
    case 0x26C10A3D:
        command = 0x6A935E36;
        break;
    case 0x26C10A3E:
        command = 0x6A935E3C;
        break;
    case 0x8A1DA655:
        command = 0x6A935E4B;
        break;
    case 0xC98F4F48:
        command = 0x6A935E39;
        break;
    case 0xCA1DA670:
        command = 0x6A935E45;
        break;
    default:
        return 0;
    }
    Virtual<void>(Global<void*>(addr::SC4_Commands), 0x50, command, uint32_t{0}, uint32_t{0});
    return 1;
}

// The game's 0x00774560 (to 0x007745BB): QueryInterface of the dialog
// family's base: its two interfaces at +8 and +0xC, else the base object's.
uint8_t __fastcall DialogQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    if (iid == 0x22E85D8E) {
        *out = self ? self + 0x8 : nullptr;
        AddRef(self);
        return 1;
    }
    if (iid == 0x452294AA) {
        *out = self ? self + 0xC : nullptr;
        AddRef(self);
        return 1;
    }
    return This<uint8_t>(addr::cRZBaseUnknown_QueryInterface1, self, iid, out);
}

// The game's 0x007745C0 (to 0x00774612): closes the dialog: ends its modal
// state and takes its window out of the parent window.
void __fastcall DialogClose(char* self, void*)
{
    if (At<uint8_t>(self, 0x24)) {
        void* const manager = Cdecl<void*>(addr::SC4_GetWinManager);
        void* const window = Virtual<void*>(At<void*>(self, 0x10), 0xC);
        Virtual<void>(manager, 0x24, uint32_t{0xF}, window, uint32_t{0});
        At<uint8_t>(self, 0x24) = 0;
    }
    void* const parent = Virtual<void*>(Virtual<void*>(At<void*>(self, 0x10), 0xC), 0x18);
    if (parent)
        Virtual<void>(parent, 0x5C, Virtual<void*>(At<void*>(self, 0x10), 0xC));
}

bool InstallMySimPanel2(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[16] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F772E90, reinterpret_cast<void*>(&Remove)},
        {addr::SC4UI_F772EE0, reinterpret_cast<void*>(&Purge)},
        {addr::SC4UI_F772F30, reinterpret_cast<void*>(&Add)},
        {addr::SC4UI_F773050, reinterpret_cast<void*>(&Construct)},
        {addr::SC4UI_F773100, reinterpret_cast<void*>(&Destruct)},
        {addr::SC4UI_F773180, reinterpret_cast<void*>(&DeletingDestruct)},
        {addr::SC4UI_F7731A0, reinterpret_cast<void*>(&FillList)},
        {addr::SC4UI_F773980, reinterpret_cast<void*>(&SetTickerRow)},
        {addr::SC4UI_F774070, reinterpret_cast<void*>(&UpdateTicker)},
        {addr::SC4UI_F774450, reinterpret_cast<void*>(&ButtonsDeletingDestruct)},
        {addr::SC4UI_F774470, reinterpret_cast<void*>(&ButtonsDoWinMsg)},
        {addr::SC4UI_F774550, reinterpret_cast<void*>(&ButtonsConstruct)},
        {addr::SC4UI_F774560, reinterpret_cast<void*>(&DialogQueryInterface)},
        {addr::SC4UI_F7745C0, reinterpret_cast<void*>(&DialogClose)},
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

const bool g_mySimPanel2Registered = registry::Register({
    "ui.mysim_panel.part2",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F773050,
    "Our news ticker (advice list rows, ticker rows, timer), the command buttons and the dialog base's Close",
    false,
    InstallMySimPanel2,
});

} // namespace
} // namespace opensc4::ui
