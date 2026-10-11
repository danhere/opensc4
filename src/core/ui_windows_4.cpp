// The budget panel's department dialog, fourth part of the range after the
// city view's picture class (0x0077BEC0 to 0x0077DAB0).
//
// The dialog (0x140 bytes, vtable 0x00AB5494, reference counted; made by
// 0x007862B0 and run as a modal window by the budget panel's 0x00768460)
// builds its controls in code on a window it makes itself. This file holds:
//
//   - BuildMessagePage (0x0077BEC0): two plain windows made and nested, a
//     background picture, two texts and two buttons: the page shown when
//     there is nothing to list;
//   - Destruct (0x0077C4C0): the destructor's body;
//   - WriteFunding (0x0077C5E0): the kept funding figures written back to the
//     buildings or to the departments;
//   - BuildOrdinancePage (0x0077C660): the ordinances that are available, the
//     ones that pay and the ones that cost in two columns of up to nine rows,
//     each with a check box, an information button, a name and an amount, the
//     two totals, and the buttons at the bottom.
//
// Replacement, off by default:
//
//   ui.windows.part4: everything above.
//
// The windows, the ordinance simulator and the dialog's other methods (label,
// button and check box makers, the layout) are the game's, called through
// their vtables or by address. The container helpers in this range (vectors
// of 8, 12 and 24-byte records) stay the game's.
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

namespace dialog {
constexpr std::size_t kWindow = 0x0C;        // the dialog's window
constexpr std::size_t kPage = 0x10;          // the window the labels are made on
constexpr std::size_t kPane = 0x14;          // the message page's outer window
constexpr std::size_t kInner = 0x18;         // the message page's inner window
constexpr std::size_t kHeld = 0x1C;          // one more reference
constexpr std::size_t kByBuilding = 0x24;    // nonzero: the funding is kept by building
constexpr std::size_t kFirstList = 0x2C;     // vector of {dword, reference}
constexpr std::size_t kShares = 0x38;        // vector of {float share, department}
constexpr std::size_t kBuildings = 0x44;     // vector of {building, float, float}
constexpr std::size_t kRecords = 0x50;       // vector of 0x18-byte records, a reference at +0x10
constexpr std::size_t kBuffer = 0x5C;
constexpr std::size_t kLastList = 0x68;      // vector of {dword, reference}
constexpr std::size_t kRows = 0x74;          // vector of dwords: the ordinance's index of each row
constexpr std::size_t kBottom = 0x80;
constexpr std::size_t kRight = 0x84;
constexpr std::size_t kRowHeight = 0x88;
constexpr std::size_t kTotal = 0x90;         // int64
constexpr std::size_t kIncomeTop = 0x98;     // where the next row of the left column goes
constexpr std::size_t kExpenseTop = 0x9C;    // and of the right column
constexpr std::size_t kCount = 0xA0;         // the ordinances that are available
constexpr std::size_t kIds = 0xA4;           // their IDs
constexpr std::size_t kStates = 0xA8;        // two bytes each: on when the page was built, on now
constexpr std::size_t kFactor = 0xB0;        // int64: 1, or the months of the period shown
constexpr std::size_t kIncomeHeading = 0xB8; // game strings from here on
constexpr std::size_t kIncomeAmount = 0xCC;
constexpr std::size_t kExpenseHeading = 0xE0;
constexpr std::size_t kExpenseAmount = 0xF4;
constexpr std::size_t kTotalLabel = 0x108;
constexpr std::size_t kIncomeCount = 0x124;
constexpr std::size_t kExpenseCount = 0x128;
constexpr std::size_t kList = 0x130;
} // namespace dialog

constexpr uint32_t kWinGenClass = 0x4386D516;
constexpr uint32_t kWinGenIid = 0x5386D516;
constexpr uint32_t kButtonIid = 0x8810;

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
struct LocalizedString {
    GameString text;
    uint32_t rest[3];
};
static_assert(sizeof(LocalizedString) == 0x20);

// The holder of an image found by key.
struct ImageLoader {
    std::uintptr_t vtable;
    void* image;
    uint32_t key[3];
    GameString name;
    uint32_t mode;
    uint32_t flag;
};
static_assert(sizeof(ImageLoader) == 0x30);

// One building whose funding the dialog keeps.
struct BuildingFunding {
    void* building;
    uint32_t first;  // a float, passed on as it is
    uint32_t second;
};
static_assert(sizeof(BuildingFunding) == 0xC);

// One department's share.
struct DepartmentShare {
    float share;
    void* department;
};
static_assert(sizeof(DepartmentShare) == 0x8);

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

void* LoadText(LocalizedString& text, uint32_t group, uint32_t instance)
{
    return This<void*>(addr::SC4LocalizedString_SC4LocalizedString, &text, group, instance, uint32_t{1});
}

// The string body of a loaded text, as the original takes it.
StringBody* Body(void* made)
{
    return made ? reinterpret_cast<StringBody*>(static_cast<char*>(made) + 4) : nullptr;
}

// A copy of a string made by the game's range constructor.
void CopyByRange(GameString& text, const StringBody* source, uint8_t* tag)
{
    text.body.begin = nullptr;
    text.body.end = nullptr;
    text.body.capacity = nullptr;
    char* const begin = source->begin;
    const uint32_t length = static_cast<uint32_t>(source->end - begin);
    This<void>(addr::cSC4Advice_F408420, &text.body, begin, begin + length, tag);
    text.vtable = addr::SC4String_Vtable;
    text.zero = 0;
}

// A copy of a string made by the inlined constructor: a buffer of the exact
// size and a byte loop.
void CopyInline(GameString& text, const StringBody* source)
{
    text.body.begin = nullptr;
    text.body.end = nullptr;
    text.body.capacity = nullptr;
    const char* from = source->begin;
    uint32_t length = static_cast<uint32_t>(source->end - from);
    char* to;
    if (length + 1 != 0) {
        char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, length + 1);
        text.body.begin = buffer;
        text.body.end = buffer;
        text.body.capacity = buffer + length + 1;
        to = buffer;
    } else {
        Cdecl<void>(addr::STL_ThrowLengthError, "basic_string");
        to = text.body.begin;
    }
    if (static_cast<int32_t>(length) > 0) {
        do {
            *to++ = *from++;
        } while (--length != 0);
    }
    text.body.end = to;
    *to = 0;
    text.vtable = addr::SC4String_Vtable;
    text.zero = 0;
}

void* Child(void* parent, uint32_t id)
{
    return Virtual<void*>(parent, 0x8C, id);
}

// The dialog's label maker (0x00779660): a text window made on a window.
void Label(char* self, void* window, uint32_t id, uint32_t x, uint32_t y, void* text, uint32_t style, uint32_t font,
           uint32_t red, uint32_t green, uint32_t blue)
{
    This<void>(addr::SC4UI_F779660, static_cast<void*>(self), window, id, x, y, text, style, font, red, green, blue);
}

// The dialog's button maker (0x0077B960).
void Button(char* self, void* window, uint32_t id, uint32_t x, uint32_t y, uint32_t picture, uint32_t a, uint32_t b)
{
    This<void>(addr::SC4UI_F77B960, static_cast<void*>(self), window, id, x, y, picture, a, b);
}

// The dialog's check box maker (0x0077B7B0).
void CheckBox(char* self, void* window, uint32_t id, uint32_t x, uint32_t y, uint32_t picture, uint32_t a,
              uint32_t checked)
{
    This<void>(addr::SC4UI_F77B7B0, static_cast<void*>(self), window, id, x, y, picture, a, checked);
}

// The game's inlined push_back on the vector of row indices.
void PushRow(char* self, uint32_t index, uint8_t* tag)
{
    uint32_t* const end = At<uint32_t*>(self, dialog::kRows + 4);
    uint32_t value = index;
    if (end != At<uint32_t*>(self, dialog::kRows + 8)) {
        if (end)
            *end = value;
        At<uint32_t*>(self, dialog::kRows + 4) = At<uint32_t*>(self, dialog::kRows + 4) + 1;
    } else {
        This<void>(addr::SC4PtrVector_InsertN, static_cast<void*>(self + dialog::kRows), end, &value, tag,
                   uint32_t{1}, uint32_t{1});
    }
}

// The runtime's 64-bit multiply (0x009F0D10) of the dialog's factor and an
// amount.
int64_t Scaled(char* self, int64_t amount)
{
    return static_cast<int64_t>(static_cast<uint64_t>(At<int64_t>(self, dialog::kFactor)) *
                                static_cast<uint64_t>(amount));
}

// The game's 0x0077BEC0. `this` is the department dialog. Makes the page
// that only says something: an outer window (kept at +0x14) on the given
// parent, an inner one (kept at +0x18) on it with a background picture, the
// two texts on the inner window and two buttons on the outer one, and the
// parent centred in its own parent.
void __fastcall BuildMessagePage(char* self, void*, void* parent, void* first, void* second)
{
    void* outerGen = nullptr;
    void* com = Cdecl<void*>(addr::RZGetCOM);
    Clear(outerGen);
    if (Virtual<uint8_t>(com, 0xC, kWinGenClass, kWinGenIid, &outerGen)) {
        Assign(At<void*>(self, dialog::kPane), Virtual<void*>(outerGen, 0xC));
        Virtual<void>(outerGen, 0x18, uint32_t{1}, uint32_t{0});
        Virtual<void>(outerGen, 0x18, uint32_t{2}, uint32_t{0});
        Virtual<void>(outerGen, 0x18, uint32_t{0x10000}, uint32_t{0});
        Virtual<void>(outerGen, 0x18, uint32_t{0x20}, uint32_t{0});
        Virtual<void>(outerGen, 0x18, uint32_t{0x40}, uint32_t{0});
        Virtual<void>(outerGen, 0x18, uint32_t{0x1000}, uint32_t{1});
        Virtual<void>(outerGen, 0x18, uint32_t{0x2000}, uint32_t{1});
        Virtual<void>(outerGen, 0x18, uint32_t{0x4000}, uint32_t{1});
        Virtual<void>(At<void*>(self, dialog::kPane), 0x110, uint32_t{0x10000}, uint32_t{0});
        Virtual<void>(At<void*>(self, dialog::kPane), 0x110, uint32_t{0x200000}, uint32_t{0});
        Virtual<void>(At<void*>(self, dialog::kPane), 0x110, uint32_t{0x800}, uint32_t{1});
        Virtual<void>(At<void*>(self, dialog::kPane), 0x5C);
        Virtual<void>(At<void*>(self, dialog::kPane), 0x110, uint32_t{0x800}, uint32_t{0});
        Virtual<void>(At<void*>(self, dialog::kPane), 0x100, uint32_t{0x0423278D});
        Virtual<void>(parent, 0x38, At<void*>(self, dialog::kPane));

        void* innerGen = nullptr;
        com = Cdecl<void*>(addr::RZGetCOM);
        Clear(innerGen);
        if (Virtual<uint8_t>(com, 0xC, kWinGenClass, kWinGenIid, &innerGen)) {
            Assign(At<void*>(self, dialog::kInner), Virtual<void*>(innerGen, 0xC));
            Virtual<void>(innerGen, 0x18, uint32_t{1}, uint32_t{0});
            Virtual<void>(innerGen, 0x18, uint32_t{2}, uint32_t{0});
            Virtual<void>(innerGen, 0x18, uint32_t{0x10000}, uint32_t{0});
            Virtual<void>(innerGen, 0x18, uint32_t{0x20}, uint32_t{0});
            Virtual<void>(innerGen, 0x18, uint32_t{0x40}, uint32_t{0});
            Virtual<void>(innerGen, 0x18, uint32_t{0x1000}, uint32_t{1});
            Virtual<void>(innerGen, 0x18, uint32_t{0x2000}, uint32_t{0});
            Virtual<void>(innerGen, 0x18, uint32_t{0x4000}, uint32_t{1});
            Virtual<void>(At<void*>(self, dialog::kInner), 0x110, uint32_t{0x100}, uint32_t{1});
            Virtual<void>(At<void*>(self, dialog::kInner), 0x110, uint32_t{0x10000}, uint32_t{1});
            Virtual<void>(At<void*>(self, dialog::kInner), 0x110, uint32_t{0x20000}, uint32_t{1});
            Virtual<void>(At<void*>(self, dialog::kInner), 0x110, uint32_t{0x200000}, uint32_t{1});
            Virtual<void>(At<void*>(self, dialog::kInner), 0x110, uint32_t{0x800}, uint32_t{0});
            Virtual<void>(At<void*>(self, dialog::kInner), 0x110, uint32_t{4}, uint32_t{1});
            Virtual<void>(At<void*>(self, dialog::kInner), 0x100, uint32_t{0x0423278F});
            Virtual<void>(At<void*>(self, dialog::kPane), 0x38, At<void*>(self, dialog::kInner));
        }

        // The background picture. The original uses the inner window's maker
        // here whether or not it was made.
        ImageLoader loader;
        This<void*>(addr::SC4ImageLoader_ConstructParts, &loader, uint32_t{0x856DDBAC}, uint32_t{0x46A006B0},
                    uint32_t{0x144161E4}, uint32_t{1}, uint32_t{0});
        Virtual<void>(innerGen, 0x34, uint32_t{2}, loader.image);

        Virtual<void>(At<void*>(self, dialog::kPane), 0xD4, uint32_t{0x12C}, uint32_t{0x64});
        Virtual<void>(At<void*>(self, dialog::kInner), 0xD4, uint32_t{0x12C}, uint32_t{0x64});
        Virtual<void>(At<void*>(self, dialog::kPane), 0xE0, uint32_t{0}, uint32_t{0});
        Virtual<void>(At<void*>(self, dialog::kInner), 0xE0, uint32_t{0}, uint32_t{0});
        Virtual<void>(parent, 0xD4, uint32_t{0x12C}, uint32_t{0x64});

        // The parent centred in its own parent.
        void* const outerA = Virtual<void*>(parent, 0x2C);
        void* const outerB = Virtual<void*>(parent, 0x2C);
        const int32_t height = Virtual<int32_t>(parent, 0xA8);
        const int32_t y = (Virtual<int32_t>(outerA, 0xA8) - height) / 2;
        const int32_t width = Virtual<int32_t>(parent, 0xA4);
        const int32_t x = (Virtual<int32_t>(outerB, 0xA4) - width) / 2;
        Virtual<void>(parent, 0xE0, x, y);

        Label(self, At<void*>(self, dialog::kInner), 0x0ABCE000, 0xA, 5, first, 0, 0xEA85D307, 0x44, 0x55, 0x66);
        Label(self, At<void*>(self, dialog::kInner), 0x0ABCE001, 0xF, 0x19, second, 0x63, 0xEA85D308, 0x44, 0x55,
              0x66);
        Button(self, At<void*>(self, dialog::kPane), 0x385, 0, 0, 0x14416242, 0, 0);
        Button(self, At<void*>(self, dialog::kPane), 0xCC, 0x10D, 0xB, 0x140155B4, 0, 0);
        Virtual<void>(Child(At<void*>(self, dialog::kPane), 0x385), 0xD4, uint32_t{0x12C}, uint32_t{0x64});
        Virtual<void>(Child(At<void*>(self, dialog::kPane), 0x485), 0xD4, uint32_t{0x12C}, uint32_t{0x64});
        Virtual<void>(Child(At<void*>(self, dialog::kPane), 0x385), 0x158, parent);
        Virtual<void>(Child(At<void*>(self, dialog::kPane), 0xCC), 0x158, parent);

        This<void>(addr::SC4ImageLoader_Destruct, &loader);
        if (innerGen)
            Release(innerGen);
    }
    if (outerGen)
        Release(outerGen);
}

// The game's 0x0077C4C0. `this` is the department dialog. The destructor's
// body (the deleting destructor 0x0077E420 calls it).
void __fastcall Destruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0) = addr::SC4UI_DepartmentDialogVtable;
    This<void>(addr::SC4List_Destroy, static_cast<void*>(self + dialog::kList));
    DestroyString(At<GameString>(self, dialog::kTotalLabel));
    DestroyString(At<GameString>(self, dialog::kExpenseAmount));
    DestroyString(At<GameString>(self, dialog::kExpenseHeading));
    DestroyString(At<GameString>(self, dialog::kIncomeAmount));
    DestroyString(At<GameString>(self, dialog::kIncomeHeading));
    if (void* const rows = At<void*>(self, dialog::kRows))
        Cdecl<void>(addr::SC4_Free, rows);
    This<void>(addr::SC4RefPairVector_Destroy, static_cast<void*>(self + dialog::kLastList));
    if (void* const buffer = At<void*>(self, dialog::kBuffer))
        Cdecl<void>(addr::SC4_Free, buffer);
    This<void>(addr::SC4UI_F77C380, static_cast<void*>(self + dialog::kRecords));
    This<void>(addr::SC4Vector12_Destroy, static_cast<void*>(self + dialog::kBuildings));
    This<void>(addr::SC4RefPairVector_Destroy, static_cast<void*>(self + dialog::kShares));
    This<void>(addr::SC4RefPairVector_Destroy, static_cast<void*>(self + dialog::kFirstList));
    if (void* const held = At<void*>(self, dialog::kHeld))
        Release(held);
    if (void* const inner = At<void*>(self, dialog::kInner))
        Release(inner);
    if (void* const pane = At<void*>(self, dialog::kPane))
        Release(pane);
    if (void* const page = At<void*>(self, dialog::kPage))
        Release(page);
    if (void* const window = At<void*>(self, dialog::kWindow))
        Release(window);
}

// The game's 0x0077C5E0. `this` is the department dialog; the argument is not
// read. Writes the kept funding back: by building (both figures of each kept
// building through 0x0077BC90) or, when +0x24 is zero, each department's
// share through the department's slot 11.
void __fastcall WriteFunding(char* self, void*, uint32_t)
{
    if (At<uint32_t>(self, dialog::kByBuilding) != 0) {
        BuildingFunding* const end = At<BuildingFunding*>(self, dialog::kBuildings + 4);
        BuildingFunding* record = At<BuildingFunding*>(self, dialog::kBuildings);
        if (record == end)
            return;
        do {
            This<void>(addr::SC4UI_F77BC90, static_cast<void*>(self), record->building, uint32_t{0}, record->first);
            This<void>(addr::SC4UI_F77BC90, static_cast<void*>(self), record->building, uint32_t{1}, record->second);
            ++record;
        } while (record != end);
        return;
    }
    uint32_t index = 0;
    if (static_cast<uint32_t>((At<char*>(self, dialog::kShares + 4) - At<char*>(self, dialog::kShares)) >> 3) == 0)
        return;
    do {
        DepartmentShare* const share = At<DepartmentShare*>(self, dialog::kShares) + index;
        Virtual<void>(share->department, 0x2C, share, uint32_t{0});
        ++index;
    } while (index <
             static_cast<uint32_t>((At<char*>(self, dialog::kShares + 4) - At<char*>(self, dialog::kShares)) >> 3));
}

// One row of the ordinance page up to its name: the check box, the
// information button with its tip, and the name. `top` is the column's
// field; the counters are the caller's.
void OrdinanceRow(char* self, void* parent, void* ordinances, uint32_t index, std::size_t top, uint32_t& boxId,
                  uint32_t& buttonId, uint32_t& labelId, void*& button, bool tipByRange, uint8_t* tag)
{
    const uint32_t checked = At<uint8_t*>(self, dialog::kStates)[index * 2 + 1];
    CheckBox(self, parent, boxId, 0x12, At<uint32_t>(self, top), 0x144161EA, 0, checked);
    PushRow(self, index, tag);

    const uint32_t y = At<uint32_t>(self, top);
    ++boxId;
    Button(self, parent, buttonId, 0x22, y, 0x140155B7, 0, 0);
    Virtual<void>(Child(parent, buttonId), 0x158, parent);
    void* const made = Child(parent, buttonId);
    ++buttonId;
    if (Virtual<uint8_t>(made, 0x0, kButtonIid, &button)) {
        LocalizedString source;
        StringBody* const body = Body(LoadText(source, 0x6A231EAA, 0xAA8713EA));
        GameString tip;
        if (tipByRange) {
            CopyByRange(tip, body, tag);
        } else {
            This<void>(addr::SC4String_CopyBuffer, &tip.body, body, uint32_t{0}, uint32_t{0xFFFFFFFF}, tag);
            tip.vtable = addr::SC4String_Vtable;
            tip.zero = 0;
        }
        Virtual<void>(button, 0x8C, &tip);
        DestroyString(tip);
        DestroyString(source.text);
        Virtual<void>(button, 0xB4, uint32_t{0}, uint32_t{0x2A5C322B});
        Release(button);
        button = nullptr;
    }

    void* const ordinance = Virtual<void*>(ordinances, 0x50, At<uint32_t*>(self, dialog::kIds)[index]);
    void* const page = At<void*>(self, dialog::kPage);
    void* const name = Virtual<void*>(ordinance, 0x1C);
    Label(self, page, labelId, 0x44, At<uint32_t>(self, top), name, 0, 0xEA85D305, 0x44, 0x55, 0x66);
    ++labelId;
}

// The game's 0x0077C660. `this` is the department dialog; the argument is the
// window the controls are made on. Builds the ordinance page.
void __fastcall BuildOrdinancePage(char* self, void*, void* parent)
{
    uint32_t boxId = 0x12C;
    uint32_t buttonId = 0x1F4;
    uint8_t tag;
    void* button = nullptr; // not set in the original; only written by the lookup below

    GameString amountText;
    amountText.body.begin = nullptr;
    amountText.body.end = nullptr;
    amountText.body.capacity = nullptr;
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    amountText.body.capacity = buffer + 8;
    amountText.body.begin = buffer;
    amountText.body.end = buffer;
    *buffer = 0;
    void* const ordinances = Global<void*>(addr::SC4_Service43D90);
    amountText.vtable = addr::SC4String_Vtable;
    amountText.zero = 0;

    // The ordinances that are available: counted by kind, and whether each is
    // on, twice.
    At<uint32_t>(self, dialog::kCount) = Virtual<uint32_t>(ordinances, 0x2C);
    At<uint32_t*>(self, dialog::kIds) =
        Cdecl<uint32_t*>(addr::SC4_operator_new, At<uint32_t>(self, dialog::kCount) << 2);
    At<uint8_t*>(self, dialog::kStates) =
        Cdecl<uint8_t*>(addr::SC4_operator_new, At<uint32_t>(self, dialog::kCount) << 1);
    Virtual<void>(ordinances, 0x30, At<uint32_t*>(self, dialog::kIds), &At<uint32_t>(self, dialog::kCount));
    if (At<uint32_t>(self, dialog::kCount) != 0) {
        uint32_t i = 0;
        do {
            Virtual<int64_t>(Virtual<void*>(ordinances, 0x50, At<uint32_t*>(self, dialog::kIds)[i]), 0x14);
            if (Virtual<uint8_t>(Virtual<void*>(ordinances, 0x50, At<uint32_t*>(self, dialog::kIds)[i]), 0x58))
                ++At<uint32_t>(self, dialog::kIncomeCount);
            else
                ++At<uint32_t>(self, dialog::kExpenseCount);
            At<uint8_t*>(self, dialog::kStates)[i * 2] =
                Virtual<uint8_t>(ordinances, 0x20, At<uint32_t*>(self, dialog::kIds)[i]);
            At<uint8_t*>(self, dialog::kStates)[i * 2 + 1] =
                Virtual<uint8_t>(ordinances, 0x20, At<uint32_t*>(self, dialog::kIds)[i]);
            ++i;
        } while (i < At<uint32_t>(self, dialog::kCount));
    }

    uint32_t incomeRows = At<uint32_t>(self, dialog::kIncomeCount);
    uint32_t expenseRows = At<uint32_t>(self, dialog::kExpenseCount);
    if (incomeRows == 0 && expenseRows == 0) {
        // No ordinance yet: the message page.
        LocalizedString first;
        LoadText(first, 0x6A3FF01C, 0x8A579F31);
        LocalizedString second;
        LoadText(second, 0x6A3FF01C, 0x0A5CB1A1);
        BuildMessagePage(self, nullptr, parent, &first, &second);
        DestroyString(second.text);
        DestroyString(first.text);
        DestroyString(amountText);
        return;
    }

    if (incomeRows > 9)
        incomeRows = 9;
    if (expenseRows > 9)
        expenseRows = 9;
    This<void>(addr::SC4UI_F77A480, static_cast<void*>(self), parent, incomeRows, expenseRows);
    At<uint32_t*>(self, dialog::kRows + 4) = At<uint32_t*>(self, dialog::kRows);

    // The title.
    GameString title;
    {
        LocalizedString source;
        StringBody* const body = Body(LoadText(source, 0x6A3FF01C, 0x8A579F31));
        CopyByRange(title, body, &tag);
        DestroyString(source.text);
    }
    Label(self, At<void*>(self, dialog::kPage), 0x0ABCDE00, 0x14, 8, &title, 0, 0x4A809916, 0x44, 0x55, 0x66);

    const uint32_t incomeTop = At<uint32_t>(self, dialog::kIncomeTop);
    const uint32_t expenseTop = At<uint32_t>(self, dialog::kExpenseTop);
    uint32_t labelId = 0x0ABCDE01;
    uint32_t shown;

    // The ordinances that pay.
    if (At<uint32_t>(self, dialog::kIncomeCount) != 0) {
        {
            const uint32_t rowHeight = At<uint32_t>(self, dialog::kRowHeight);
            void* const page = At<void*>(self, dialog::kPage);
            At<int64_t>(self, dialog::kTotal) = 0;
            Label(self, page, 0x0ABCDE01, 0x12, incomeTop - rowHeight - 2, self + dialog::kIncomeHeading, 0,
                  0xEA85D304, 0x44, 0x55, 0x66);
        }
        Label(self, At<void*>(self, dialog::kPage), 0x0ABCDE02, At<uint32_t>(self, dialog::kRight) - 0x26,
              At<uint32_t>(self, dialog::kIncomeTop) - At<uint32_t>(self, dialog::kRowHeight) - 2,
              self + dialog::kIncomeAmount, 6, 0xEA85D304, 0x44, 0x55, 0x66);
        labelId = 0x0ABCDE03;
        shown = 0;
        if (At<uint32_t>(self, dialog::kCount) != 0) {
            uint32_t i = 0;
            do {
                const int64_t income =
                    Virtual<int64_t>(Virtual<void*>(ordinances, 0x50, At<uint32_t*>(self, dialog::kIds)[i]), 0x14);
                if (Virtual<uint8_t>(Virtual<void*>(ordinances, 0x50, At<uint32_t*>(self, dialog::kIds)[i]), 0x58)) {
                    if (shown < 9) {
                        OrdinanceRow(self, parent, ordinances, i, dialog::kIncomeTop, boxId, buttonId, labelId,
                                     button, false, &tag);
                        if (At<uint8_t*>(self, dialog::kStates)[i * 2 + 1] != 0) {
                            Cdecl<void>(addr::SC4_FormatMoney, Scaled(self, income), &amountText);
                            At<int64_t>(self, dialog::kTotal) =
                                static_cast<int64_t>(static_cast<uint64_t>(At<int64_t>(self, dialog::kTotal)) +
                                                     static_cast<uint64_t>(Scaled(self, income)));
                        } else {
                            This<void*>(addr::SC4String_EraseBody, &amountText.body, uint32_t{0},
                                        uint32_t{0xFFFFFFFF});
                        }
                        {
                            const uint32_t y = At<uint32_t>(self, dialog::kIncomeTop);
                            const uint32_t x = At<uint32_t>(self, dialog::kRight) - 0x26;
                            Label(self, At<void*>(self, dialog::kPage), labelId, x, y, &amountText, 6, 0xEA85D305, 0,
                                  0x66, 0);
                        }
                        At<uint32_t>(self, dialog::kIncomeTop) =
                            At<uint32_t>(self, dialog::kIncomeTop) + At<uint32_t>(self, dialog::kRowHeight);
                        ++labelId;
                    } else if (At<uint8_t*>(self, dialog::kStates)[i * 2 + 1] != 0) {
                        At<int64_t>(self, dialog::kTotal) =
                            static_cast<int64_t>(static_cast<uint64_t>(At<int64_t>(self, dialog::kTotal)) +
                                                 static_cast<uint64_t>(Scaled(self, income)));
                    }
                    if (shown < 9)
                        ++shown;
                }
                ++i;
            } while (i < At<uint32_t>(self, dialog::kCount));
        }
        if ((incomeRows & 1) == 0) {
            At<uint32_t>(self, dialog::kIncomeTop) =
                At<uint32_t>(self, dialog::kIncomeTop) + At<uint32_t>(self, dialog::kRowHeight);
        }
        Label(self, At<void*>(self, dialog::kPage), labelId, 0x96, At<uint32_t>(self, dialog::kIncomeTop),
              self + dialog::kTotalLabel, 0, 0xEA85D304, 0x44, 0x55, 0x66);
        ++labelId;
        Cdecl<void>(addr::SC4_FormatMoney, At<int64_t>(self, dialog::kTotal), &amountText);
        {
            const uint32_t y = At<uint32_t>(self, dialog::kIncomeTop);
            const uint32_t x = At<uint32_t>(self, dialog::kRight) - 0x26;
            Label(self, At<void*>(self, dialog::kPage), labelId, x, y, &amountText, 6, 0xEA85D304, 0, 0x66, 0);
        }
        ++labelId;
    }

    // The ordinances that cost.
    shown = 0;
    if (At<uint32_t>(self, dialog::kExpenseCount) != 0) {
        {
            void* const page = At<void*>(self, dialog::kPage);
            At<int64_t>(self, dialog::kTotal) = 0;
            Label(self, page, labelId, 0x12,
                  At<uint32_t>(self, dialog::kExpenseTop) - At<uint32_t>(self, dialog::kRowHeight) - 2,
                  self + dialog::kExpenseHeading, 0, 0xAA0F4A9E, 0x44, 0x55, 0x66);
        }
        ++labelId;
        Label(self, At<void*>(self, dialog::kPage), labelId, At<uint32_t>(self, dialog::kRight) - 0x26,
              At<uint32_t>(self, dialog::kExpenseTop) - At<uint32_t>(self, dialog::kRowHeight) - 2,
              self + dialog::kExpenseAmount, 6, 0xAA0F4A9E, 0x44, 0x55, 0x66);
        ++labelId;
        if (At<uint32_t>(self, dialog::kCount) != 0) {
            uint32_t i = 0;
            do {
                const int64_t income =
                    Virtual<int64_t>(Virtual<void*>(ordinances, 0x50, At<uint32_t*>(self, dialog::kIds)[i]), 0x14);
                if (!Virtual<uint8_t>(Virtual<void*>(ordinances, 0x50, At<uint32_t*>(self, dialog::kIds)[i]),
                                      0x58)) {
                    if (shown < 9) {
                        OrdinanceRow(self, parent, ordinances, i, dialog::kExpenseTop, boxId, buttonId, labelId,
                                     button, true, &tag);
                        if (At<uint8_t*>(self, dialog::kStates)[i * 2 + 1] != 0) {
                            const int64_t cost =
                                static_cast<int64_t>(0u - static_cast<uint64_t>(Scaled(self, income)));
                            Cdecl<void>(addr::SC4_FormatMoney, cost, &amountText);
                            At<int64_t>(self, dialog::kTotal) =
                                static_cast<int64_t>(static_cast<uint64_t>(At<int64_t>(self, dialog::kTotal)) -
                                                     static_cast<uint64_t>(Scaled(self, income)));
                        } else {
                            // The inlined erase of the whole text.
                            char* const begin = amountText.body.begin;
                            char* const last =
                                begin + static_cast<uint32_t>(amountText.body.end - amountText.body.begin);
                            if (begin != last) {
                                const uint32_t count = static_cast<uint32_t>(amountText.body.end - last) + 1;
                                if (count != 0)
                                    Cdecl<void*>(addr::SC4_memmove, begin, last, count);
                                amountText.body.end += begin - last;
                            }
                        }
                        {
                            const uint32_t y = At<uint32_t>(self, dialog::kExpenseTop);
                            const uint32_t x = At<uint32_t>(self, dialog::kRight) - 0x26;
                            Label(self, At<void*>(self, dialog::kPage), labelId, x, y, &amountText, 6, 0xEA85D305,
                                  0x77, 0, 0);
                        }
                        At<uint32_t>(self, dialog::kExpenseTop) =
                            At<uint32_t>(self, dialog::kExpenseTop) + At<uint32_t>(self, dialog::kRowHeight);
                        ++labelId;
                    } else if (At<uint8_t*>(self, dialog::kStates)[i * 2 + 1] != 0) {
                        At<int64_t>(self, dialog::kTotal) =
                            static_cast<int64_t>(static_cast<uint64_t>(At<int64_t>(self, dialog::kTotal)) -
                                                 static_cast<uint64_t>(Scaled(self, income)));
                    }
                    if (shown < 9)
                        ++shown;
                }
                ++i;
            } while (i < At<uint32_t>(self, dialog::kCount));
        }
        if ((expenseRows & 1) == 0) {
            At<uint32_t>(self, dialog::kExpenseTop) =
                At<uint32_t>(self, dialog::kExpenseTop) + At<uint32_t>(self, dialog::kRowHeight);
        }
        Label(self, At<void*>(self, dialog::kPage), labelId, 0x96, At<uint32_t>(self, dialog::kExpenseTop),
              self + dialog::kTotalLabel, 0, 0xEA85D304, 0x44, 0x55, 0x66);
        Cdecl<void>(addr::SC4_FormatMoney, At<int64_t>(self, dialog::kTotal), &amountText);
        ++labelId;
        {
            const uint32_t y = At<uint32_t>(self, dialog::kExpenseTop);
            const uint32_t x = At<uint32_t>(self, dialog::kRight) - 0x26;
            Label(self, At<void*>(self, dialog::kPage), labelId, x, y, &amountText, 6, 0xEA85D304, 0x77, 0, 0);
        }
    }

    // The two buttons at the bottom, each with its caption.
    Button(self, parent, 0xCD, 0xE, At<uint32_t>(self, dialog::kBottom) - 0x28, 0x144161EB, 0, 1);
    Virtual<void>(Child(parent, 0xCD), 0xD4, uint32_t{0xB4}, uint32_t{0x1E});
    Virtual<void>(Child(parent, 0x1CD), 0xD4, uint32_t{0xB4}, uint32_t{0x1E});
    {
        void* const made = Child(parent, 0xCD);
        LocalizedString source;
        StringBody* const body = Body(LoadText(source, 0x0A554AE8, 0x2A6CD8FD));
        GameString caption;
        CopyInline(caption, body);
        Virtual<void>(made, 0x128, &caption);
        DestroyString(caption);
        DestroyString(source.text);
    }
    Virtual<void>(Child(parent, 0xCD), 0x158, parent);

    {
        const uint32_t y = At<uint32_t>(self, dialog::kBottom) - 0x28;
        const uint32_t x = At<uint32_t>(self, dialog::kRight) - 0xC3;
        Button(self, parent, 0x6D, x, y, 0x144161EB, 0, 1);
    }
    Virtual<void>(Child(parent, 0x6D), 0xD4, uint32_t{0xB4}, uint32_t{0x1E});
    Virtual<void>(Child(parent, 0x16D), 0xD4, uint32_t{0xB4}, uint32_t{0x1E});
    {
        void* const made = Child(parent, 0x6D);
        LocalizedString source;
        StringBody* const body = Body(LoadText(source, 0x6A231EAA, 0x6A56B0BB));
        GameString caption;
        CopyInline(caption, body);
        Virtual<void>(made, 0x128, &caption);
        DestroyString(caption);
        DestroyString(source.text);
    }
    Virtual<void>(Child(parent, 0x6D), 0x158, parent);

    // The scroll buttons of the two columns: up and down, shown, the lower
    // ones only when the column has fewer than nine rows.
    Button(self, parent, 0x451, At<uint32_t>(self, dialog::kRight) - 0x21, incomeTop + 4, 0x140155CB, 0, 0);
    Virtual<void>(Child(parent, 0x451), 0x158, parent);
    {
        const uint32_t y = incomeTop + At<uint32_t>(self, dialog::kRowHeight) * 8 + 4;
        const uint32_t x = At<uint32_t>(self, dialog::kRight) - 0x21;
        Button(self, parent, 0x452, x, y, 0x140155CC, 0, 0);
    }
    Virtual<void>(Child(parent, 0x452), 0x158, parent);
    Button(self, parent, 0x453, At<uint32_t>(self, dialog::kRight) - 0x21, expenseTop + 4, 0x140155CB, 0, 0);
    Virtual<void>(Child(parent, 0x453), 0x158, parent);
    {
        const uint32_t y = expenseTop + At<uint32_t>(self, dialog::kRowHeight) * 8 + 4;
        const uint32_t x = At<uint32_t>(self, dialog::kRight) - 0x21;
        Button(self, parent, 0x454, x, y, 0x140155CC, 0, 0);
    }
    Virtual<void>(Child(parent, 0x454), 0x158, parent);
    Virtual<void>(Child(parent, 0x451), 0x110, uint32_t{1}, uint32_t{0});
    if (At<uint32_t>(self, dialog::kIncomeCount) < 9)
        Virtual<void>(Child(parent, 0x452), 0x110, uint32_t{1}, uint32_t{0});
    Virtual<void>(Child(parent, 0x453), 0x110, uint32_t{1}, uint32_t{0});
    if (At<uint32_t>(self, dialog::kExpenseCount) < 9)
        Virtual<void>(Child(parent, 0x454), 0x110, uint32_t{1}, uint32_t{0});

    DestroyString(title);
    DestroyString(amountText);
}

bool InstallWindows4(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[4] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F77BEC0, reinterpret_cast<void*>(&BuildMessagePage)},
        {addr::SC4UI_F77C4C0, reinterpret_cast<void*>(&Destruct)},
        {addr::SC4UI_F77C5E0, reinterpret_cast<void*>(&WriteFunding)},
        {addr::SC4UI_F77C660, reinterpret_cast<void*>(&BuildOrdinancePage)},
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

const bool g_windows4Registered = registry::Register({
    "ui.windows.part4",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F77C660,
    "Our department dialog, fourth part: the message page, the ordinance page, the funding written back and "
    "the destructor's body (docs/ui.md)",
    false,
    InstallWindows4,
});

} // namespace
} // namespace opensc4::ui
