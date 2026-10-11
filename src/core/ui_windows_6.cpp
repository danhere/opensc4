// The budget panel's department dialog, the neighbour deals page filled
// again (0x00781C90 to 0x00785737).
//
// The dialog (0x140 bytes, vtable 0x00AB5494, made by 0x007862B0 and run by
// the budget panel's 0x00768460) has two kinds of page (+0x20): 1, the line
// items of a department, and 2, the city's neighbour deals. The deals page
// shows two lists of at most nine rows each, the deals that pay (types
// 0x4773FFE1, E3, E5: power, water, garbage) and the deals that cost (types
// E0, E2, E4), each list with a heading row per commodity and its sum, a row
// per deal with the neighbour's name, the deal's cost and a row of controls,
// and the list's total from the month end service. This file holds the one
// function that fills the page's windows from the dialog's deal list; the
// window message handler (0x0078BCA0) and the command handler (0x0078B120)
// call it after a scroll or a change.
//
// The range this part was given (0x0077EF40 to 0x00782000) holds no other
// function start: everything before 0x00781C90 is the tail of 0x0077E600,
// the page's first fill, which starts in the part before and is that part's.
//
// Replacement, off by default:
//
//   ui.windows.part6: the function above.
//
// Windows, texts, fonts, the row maker (0x0077DAB0), the scroll bar maker
// (0x0077B960) and the row vector's helpers are the game's, called through
// their vtables or by address.
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
constexpr std::size_t kWindow = 0x10;        // the dialog's window
constexpr std::size_t kRows = 0x50;          // a vector of 0x18-byte row records
constexpr std::size_t kPartners = 0x5C;      // a vector of {neighbour, deal type} of the deals that count
constexpr std::size_t kDeals = 0x68;         // a vector of {deal type, deal}
constexpr std::size_t kWidth = 0x80;
constexpr std::size_t kHeight = 0x84;
constexpr std::size_t kRowHeight = 0x88;
constexpr std::size_t kTotal = 0x90;         // int64: the total of the list filled last
constexpr std::size_t kPayingHeight = 0x98;  // grows by a row's height with every row of either list
constexpr std::size_t kCostingHeight = 0x9C;
constexpr std::size_t kPayingScroll = 0x11C; // the first row on show of the list of deals that pay
constexpr std::size_t kCostingScroll = 0x120;
constexpr std::size_t kPayingCount = 0x124;  // the rows of the list of deals that pay
constexpr std::size_t kCostingCount = 0x128;
constexpr std::size_t kList = 0x130;         // a list, cleared here
} // namespace dialog

constexpr uint32_t kTextIid = 0x212CDC1F;
constexpr uint32_t kTextGroup = 0x6A3FF01C;
constexpr uint32_t kHeadingFont = 0xEA85D306;
constexpr uint32_t kRowFont = 0xEA85D305;
constexpr uint32_t kVisibleRows = 9;

constexpr uint32_t kSellPower = 0x4773FFE1;
constexpr uint32_t kBuyPower = 0x4773FFE0;
constexpr uint32_t kSellWater = 0x4773FFE3;
constexpr uint32_t kBuyWater = 0x4773FFE2;
constexpr uint32_t kSellGarbage = 0x4773FFE5;
constexpr uint32_t kBuyGarbage = 0x4773FFE4;

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

struct LocalizedString {
    GameString text;
    uint32_t rest[3];
};
static_assert(sizeof(LocalizedString) == 0x20);

// One entry of the dialog's deal list.
struct DealEntry {
    uint32_t type;
    void* deal;
};

struct DealVector {
    DealEntry* begin;
    DealEntry* end;
    DealEntry* capacity;
};

// A deal that counts: the neighbour's ID and the deal's type.
struct Partner {
    uint32_t city;
    uint32_t type;
};

struct PartnerVector {
    Partner* begin;
    Partner* end;
    Partner* capacity;
};

// One row of the page: the ID of its control window and its deal. The game
// copies the two dwords at +8 but this function never sets them.
struct RowRecord {
    uint32_t id;
    uint32_t unused;
    uint32_t low;
    uint32_t high;
    void* deal;
    uint32_t unusedToo;
};
static_assert(sizeof(RowRecord) == 0x18);

struct RowVector {
    RowRecord* begin;
    RowRecord* end;
    RowRecord* capacity;
};

// The original builds its first localized string where the rows' record is
// kept later, so the record's unset dwords hold what that string left there.
union SharedStorage {
    LocalizedString text;
    RowRecord row;
};

// What the page's sections share while it is filled.
struct Fill {
    char* self;
    void* parent;      // the window the row controls are children of
    uint32_t rowId;    // the next row's control window, from 0x0ABCE100
    uint32_t markId;   // the next row's second window, from 0x0ABCE200
    uint32_t textId;   // the next text window, from 0x0ABCDE01
    uint32_t shown;    // rows filled in the list so far
    uint32_t index;    // rows passed in the list so far, the ones scrolled away included
    GameString money;
};

// One section of a list: a commodity bought or sold.
struct Section {
    uint32_t type;
    uint32_t partnerType;    // the opposite deal in the same commodity
    uint32_t kind;           // 0 power, 1 water, 2 garbage
    bool costFirst;          // the deal's cost is read before the row's second text is set up
    uint32_t titleGroup;
    uint32_t titleInstance;
    uint32_t labelInstance;
    bool inlinePush;         // the heading's record is appended by inlined code
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

// The game's inlined reference holder: empty it.
void Clear(void*& holder)
{
    if (void* const object = holder) {
        holder = nullptr;
        Release(object);
    }
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
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(begin));
}

int64_t Add(int64_t a, int64_t b)
{
    return static_cast<int64_t>(static_cast<uint64_t>(a) + static_cast<uint64_t>(b));
}

void* Window(char* self)
{
    return At<void*>(self, dialog::kWindow);
}

void* Child(void* window, uint32_t id)
{
    return Virtual<void*>(window, 0x8C, id);
}

void SetText(void* window, const void* text)
{
    Virtual<void>(window, 0x128, text);
}

void SetFlag(void* window, uint32_t flag, uint32_t on)
{
    Virtual<void>(window, 0x110, flag, on);
}

uint32_t DealCount(char* self)
{
    const DealVector& deals = At<DealVector>(self, dialog::kDeals);
    return static_cast<uint32_t>(deals.end - deals.begin);
}

uint32_t PartnerCount(char* self)
{
    const PartnerVector& partners = At<PartnerVector>(self, dialog::kPartners);
    return static_cast<uint32_t>(partners.end - partners.begin);
}

// A localized text made and set as a window's text.
void SetLocalized(void* window, uint32_t group, uint32_t instance)
{
    LocalizedString text;
    void* const made =
        This<void*>(addr::SC4LocalizedString_SC4LocalizedString, &text, group, instance, uint32_t{1});
    SetText(window, made);
    DestroyString(text.text);
}

// A localized text copied to a plain string and set as a window's text.
void SetLocalizedCopy(void* window, uint32_t group, uint32_t instance)
{
    LocalizedString source;
    char* const made =
        This<char*>(addr::SC4LocalizedString_SC4LocalizedString, &source, group, instance, uint32_t{1});
    GameString text;
    uint8_t tag;
    This<void>(addr::SC4String_CopyBuffer, &text.body, made ? reinterpret_cast<StringBody*>(made + 4) : nullptr,
               uint32_t{0}, uint32_t{0xFFFFFFFF}, &tag);
    text.vtable = addr::SC4String_Vtable;
    text.zero = 0;
    SetText(window, &text);
    DestroyString(text);
    DestroyString(source.text);
}

// The text control with this ID fetched into `holder`, and given a font when
// it is there.
void Style(void* window, uint32_t id, void*& holder, uint32_t font)
{
    if (Virtual<uint8_t>(window, 0x90, id, kTextIid, &holder)) {
        void* const fonts = Cdecl<void*>(addr::SC4_Singleton913C72);
        void* const control = holder;
        Virtual<void>(control, 0x48, Virtual<void*>(fonts, 0x14, font));
    }
}

// The same with a holder that is in use: it is emptied first.
void Restyle(char* self, uint32_t id, void*& holder, uint32_t font)
{
    void* const window = Window(self);
    Clear(holder);
    Style(window, id, holder, font);
}

// A partner appended through the vector's insert helper.
void PushPartner(char* self, uint32_t city, uint32_t type)
{
    PartnerVector& partners = At<PartnerVector>(self, dialog::kPartners);
    Partner* const end = partners.end;
    Partner value;
    value.city = city;
    value.type = type;
    if (end != partners.capacity) {
        if (end) {
            end->city = city;
            end->type = type;
        }
        partners.end = partners.end + 1;
        return;
    }
    uint8_t tag;
    This<void>(addr::SC4PointVector_InsertN, &partners, end, &value, &tag, uint32_t{1}, uint32_t{1});
}

// A partner appended by the inlined code: the storage doubles when full.
void PushPartnerInline(char* self, uint32_t city, uint32_t type)
{
    PartnerVector& partners = At<PartnerVector>(self, dialog::kPartners);
    Partner* const end = partners.end;
    if (end != partners.capacity) {
        if (end) {
            end->city = city;
            end->type = type;
        }
        partners.end = partners.end + 1;
        return;
    }
    const uint32_t size = static_cast<uint32_t>(end - partners.begin);
    const uint32_t capacity = (size < 1 ? 1 : size) + size;
    Partner* const fresh =
        capacity ? Cdecl<Partner*>(addr::SC4_stl_allocate, uint32_t{capacity * 8}) : nullptr;
    Partner* to = fresh;
    for (const Partner* from = partners.begin; from != end; ++from, ++to) {
        if (to) {
            to->city = from->city;
            to->type = from->type;
        }
    }
    if (to) {
        to->city = city;
        to->type = type;
    }
    Partner* const newEnd = to + 1;
    if (Partner* const old = partners.begin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(old));
    partners.begin = fresh;
    partners.end = newEnd;
    partners.capacity = fresh + capacity;
}

// The deals of one type added up: every deal with flag 2 counts, and its
// neighbour is noted with the type.
void SumDeals(char* self, uint32_t type, int64_t& sum, bool inlinePush)
{
    if (DealCount(self) == 0)
        return;
    uint32_t i = 0;
    do {
        void* const deal = At<DealVector>(self, dialog::kDeals).begin[i].deal;
        if (deal)
            AddRef(deal);
        if (At<DealVector>(self, dialog::kDeals).begin[i].type == type && (Virtual<uint32_t>(deal, 0x90) & 2)) {
            sum = Add(sum, Virtual<int64_t>(deal, 0x58));
            const uint32_t city = Virtual<uint32_t>(Virtual<void*>(deal, 0x1C), 0x6C);
            if (inlinePush)
                PushPartnerInline(self, city, type);
            else
                PushPartner(self, city, type);
        }
        if (deal)
            Release(deal);
        ++i;
    } while (i < DealCount(self));
}

// A row record appended by the inlined code (the first heading only; the
// others go through 0x0077E5A0).
void PushRowInline(char* self, const RowRecord& record)
{
    RowVector& rows = At<RowVector>(self, dialog::kRows);
    RowRecord* const end = rows.end;
    if (end != rows.capacity) {
        if (end) {
            end->id = record.id;
            end->low = record.low;
            end->high = record.high;
            end->deal = record.deal;
            if (record.deal)
                AddRef(record.deal);
        }
        rows.end = rows.end + 1;
        return;
    }
    const uint32_t size = static_cast<uint32_t>(end - rows.begin);
    const uint32_t capacity = (size < 1 ? 1 : size) + size;
    RowRecord* const fresh =
        capacity ? Cdecl<RowRecord*>(addr::SC4_stl_allocate, uint32_t{capacity * 0x18}) : nullptr;
    uint8_t tag;
    RowRecord* const last = Cdecl<RowRecord*>(addr::SC4UI_F779FE0, rows.begin, end, fresh, &tag);
    if (last) {
        last->id = record.id;
        last->high = record.high;
        last->low = record.low;
        last->deal = record.deal;
        if (record.deal)
            AddRef(record.deal);
    }
    RowRecord* const newEnd = last + 1;
    RowRecord* const oldEnd = rows.end;
    for (RowRecord* old = rows.begin; old != oldEnd; ++old) {
        if (old->deal)
            Release(old->deal);
    }
    if (RowRecord* const old = rows.begin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(old));
    rows.end = newEnd;
    rows.begin = fresh;
    rows.capacity = fresh + capacity;
}

// What every row ends with: the list grows by a row.
void NextRow(Fill& fill)
{
    char* const self = fill.self;
    At<uint32_t>(self, dialog::kPayingHeight) += At<uint32_t>(self, dialog::kRowHeight);
    ++fill.shown;
}

// A section's heading row: its title, a label and the section's sum in three
// text windows, a record with the first deal of the section, and the row's
// two control windows switched off.
void Heading(Fill& fill, const Section& section, void* deal, int64_t sum)
{
    char* const self = fill.self;
    void* holder = nullptr;

    SetLocalized(Child(Window(self), fill.textId), section.titleGroup, section.titleInstance);
    Style(Window(self), fill.textId, holder, kHeadingFont);
    ++fill.textId;

    SetLocalized(Child(Window(self), fill.textId), kTextGroup, section.labelInstance);
    SetFlag(Child(Window(self), fill.textId), 1, 1);
    Restyle(self, fill.textId, holder, kHeadingFont);
    ++fill.textId;

    Cdecl<void>(addr::SC4_FormatMoney, sum, &fill.money);
    SetText(Child(Window(self), fill.textId), &fill.money);
    Restyle(self, fill.textId, holder, kHeadingFont);
    ++fill.textId;

    // The original leaves the record's dwords at +8 as they were on the stack.
    RowRecord record{};
    record.id = fill.rowId;
    if (deal) {
        AddRef(deal);
        record.deal = deal;
    }
    if (section.inlinePush)
        PushRowInline(self, record);
    else
        This<void>(addr::SC4UI_F77E5A0, self + dialog::kRows, &record);

    SetFlag(Child(fill.parent, fill.markId), 1, 0);
    ++fill.markId;
    SetFlag(Child(fill.parent, fill.rowId), 1, 0);
    ++fill.rowId;
    NextRow(fill);
    if (record.deal)
        Release(record.deal);
    if (holder)
        Release(holder);
}

// A deal's row: the neighbour's name and the deal's cost (nothing when the
// deal lacks flag 2) in three text windows, the row's controls made by
// 0x0077DAB0 with the deal's state (1: flag 2 without flag 4; 2: the same
// neighbour also has the opposite deal that counts), and its record.
void DealRow(Fill& fill, const Section& section, void* deal, RowRecord* record)
{
    char* const self = fill.self;
    GameString name;
    InitString(name);
    Virtual<void>(Virtual<void*>(deal, 0x1C), 0x84, &name);
    SetText(Child(Window(self), fill.textId), &name);
    void* holder = nullptr;
    Style(Window(self), fill.textId, holder, kRowFont);
    ++fill.textId;

    int64_t cost = 0;
    if (section.costFirst)
        cost = Virtual<int64_t>(deal, 0x58);
    SetFlag(Child(Window(self), fill.textId), 1, 0);
    Restyle(self, fill.textId, holder, kRowFont);
    ++fill.textId;

    if (!section.costFirst)
        cost = Virtual<int64_t>(deal, 0x58);
    if (!(Virtual<uint32_t>(deal, 0x90) & 2))
        cost = 0;
    Cdecl<void>(addr::SC4_FormatMoney, cost, &fill.money);
    SetText(Child(Window(self), fill.textId), &fill.money);
    Restyle(self, fill.textId, holder, kRowFont);
    ++fill.textId;

    record->id = fill.rowId;
    AddRef(deal);
    void* const control = Child(fill.parent, fill.rowId);
    record->deal = deal;
    SetFlag(control, 1, 1);

    uint32_t state = 0;
    if ((Virtual<uint32_t>(deal, 0x90) & 2) && !(Virtual<uint32_t>(deal, 0x90) & 4))
        state = 1;
    if (PartnerCount(self) != 0) {
        uint32_t i = 0;
        do {
            if (At<PartnerVector>(self, dialog::kPartners).begin[i].type == section.partnerType) {
                void* const city = Virtual<void*>(deal, 0x1C);
                const Partner* const partners = At<PartnerVector>(self, dialog::kPartners).begin;
                if (Virtual<uint32_t>(city, 0x6C) == partners[i].city)
                    state = 2;
            }
            ++i;
        } while (i < PartnerCount(self));
    }

    This<void>(addr::SC4UI_F77DAB0, self, fill.parent, fill.rowId, section.type, deal, section.kind, state, record);
    This<void>(addr::SC4UI_F77E5A0, self + dialog::kRows, record);
    SetFlag(Child(fill.parent, fill.markId), 1, 1);
    ++fill.markId;
    ++fill.rowId;
    NextRow(fill);
    Release(deal);
    if (holder)
        Release(holder);
    DestroyString(name);
}

// One section of a list: the heading before its first deal, then a row for
// each deal of the type, both only while fewer than nine rows are filled and
// the list's scroll position is reached.
void FillSection(Fill& fill, const Section& section, std::size_t scroll, int64_t sum, RowRecord* record)
{
    char* const self = fill.self;
    bool headed = false;
    if (DealCount(self) == 0)
        return;
    uint32_t i = 0;
    do {
        void* const deal = At<DealVector>(self, dialog::kDeals).begin[i].deal;
        if (deal)
            AddRef(deal);
        if (At<DealVector>(self, dialog::kDeals).begin[i].type == section.type) {
            if (!headed) {
                headed = true;
                if (fill.shown < kVisibleRows && fill.index >= At<uint32_t>(self, scroll))
                    Heading(fill, section, deal, sum);
                ++fill.index;
            }
            if (fill.shown < kVisibleRows && fill.index >= At<uint32_t>(self, scroll))
                DealRow(fill, section, deal, record);
            ++fill.index;
        }
        if (deal)
            Release(deal);
        ++i;
    } while (i < DealCount(self));
}

// Three text windows of a row left over, emptied.
void BlankTexts(Fill& fill)
{
    for (int i = 0; i < 3; ++i) {
        void* const window = Child(Window(fill.self), fill.textId);
        ++fill.textId;
        GameString empty;
        InitString(empty);
        SetText(window, &empty);
        DestroyString(empty);
    }
}

const Section kSellPowerSection = {kSellPower, kBuyPower, 0, false, kTextGroup, 0x4A579570, 0xCA579B63, true};
const Section kSellWaterSection = {kSellWater, kBuyWater, 1, false, 0x0A554AE8, 0x0A6CD8F9, 0xCA579B63, false};
const Section kSellGarbageSection = {kSellGarbage, kBuyGarbage, 2, true, kTextGroup, 0x4A579C5B, 0x6A579CA4, false};
const Section kBuyPowerSection = {kBuyPower, kSellPower, 0, true, kTextGroup, 0x4A579570, 0xEA579E1A, false};
const Section kBuyWaterSection = {kBuyWater, kSellWater, 1, false, 0x0A554AE8, 0x0A6CD8F9, 0xEA579E1A, false};
const Section kBuyGarbageSection = {kBuyGarbage, kSellGarbage, 2, false, kTextGroup, 0x4A579C5B, 0x8A579E78, false};

// The game's 0x00781C90. `this` is the department dialog showing the
// neighbour deals; `parent` is the window the rows' controls belong to. The
// rows, the partner list and the list at +0x130 are emptied, the deals added
// up by type, both lists filled from their scroll positions, and the two
// scroll bars set up again.
void __fastcall FillDeals(char* self, void*, void* parent)
{
    Fill fill;
    fill.self = self;
    fill.parent = parent;
    fill.rowId = 0x0ABCE100;
    fill.markId = 0x0ABCE200;
    int64_t sellPower = 0;
    int64_t buyPower = 0;
    int64_t sellWater = 0;
    int64_t buyWater = 0;
    int64_t buyGarbage = 0;
    int64_t sellGarbage = 0;
    InitString(fill.money);

    RowVector& rows = At<RowVector>(self, dialog::kRows);
    This<void>(addr::SC4UI_F77C3C0, &rows, rows.begin, rows.end);
    PartnerVector& partners = At<PartnerVector>(self, dialog::kPartners);
    partners.end = partners.begin;
    This<void>(addr::SC4List_Clear, self + dialog::kList);

    SumDeals(self, kSellPower, sellPower, false);
    SumDeals(self, kBuyPower, buyPower, false);
    SumDeals(self, kSellWater, sellWater, true);
    SumDeals(self, kBuyWater, buyWater, true);
    SumDeals(self, kSellGarbage, sellGarbage, true);
    SumDeals(self, kBuyGarbage, buyGarbage, true);

    uint32_t payingRows = At<uint32_t>(self, dialog::kPayingCount);
    uint32_t costingRows = At<uint32_t>(self, dialog::kCostingCount);
    if (payingRows > kVisibleRows)
        payingRows = kVisibleRows;
    if (costingRows > kVisibleRows)
        costingRows = kVisibleRows;

    // A text is loaded and copied, and the copy is never used.
    SharedStorage shared;
    char* const made =
        This<char*>(addr::SC4LocalizedString_SC4LocalizedString, &shared.text, kTextGroup, 0x2A579AD2u, uint32_t{1});
    const StringBody* const body = made ? reinterpret_cast<const StringBody*>(made + 4) : nullptr;
    const char* const source = body->begin;
    const uint32_t length = static_cast<uint32_t>(body->end - source);
    char* copy = nullptr;
    if (length + 1 == 0)
        Cdecl<void>(addr::STL_ThrowLengthError, "basic_string");
    else
        copy = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{length + 1});
    char* out = copy;
    if (static_cast<int32_t>(length) > 0) {
        for (uint32_t i = 0; i < length; ++i)
            *out++ = source[i];
    }
    *out = 0;
    DestroyString(shared.text.text);

    fill.textId = 0x0ABCDE01;
    if (At<uint32_t>(self, dialog::kPayingCount) != 0) {
        At<uint32_t>(self, dialog::kTotal) = 0;
        At<uint32_t>(self, dialog::kTotal + 4) = 0;
        fill.shown = 0;
        fill.index = 0;
        fill.textId = 0x0ABCDE03;
        FillSection(fill, kSellPowerSection, dialog::kPayingScroll, sellPower, &shared.row);
        FillSection(fill, kSellWaterSection, dialog::kPayingScroll, sellWater, &shared.row);
        FillSection(fill, kSellGarbageSection, dialog::kPayingScroll, sellGarbage, &shared.row);

        if (!(payingRows & 1))
            At<uint32_t>(self, dialog::kPayingHeight) += At<uint32_t>(self, dialog::kRowHeight);
        if (At<uint32_t>(self, dialog::kPayingCount) >= kVisibleRows && fill.shown < kVisibleRows) {
            BlankTexts(fill);
            SetFlag(Child(parent, fill.markId), 1, 0);
            ++fill.markId;
            void* const control = Child(parent, fill.rowId);
            ++fill.rowId;
            SetFlag(control, 1, 0);
        }

        void* const service = Global<void*>(addr::SC4_MonthEndService);
        ++fill.textId;
        const int64_t total = Virtual<int64_t>(service, 0x80);
        At<int64_t>(self, dialog::kTotal) = total;
        Cdecl<void>(addr::SC4_FormatMoney, total, &fill.money);
        void* const window = Child(Window(self), fill.textId);
        ++fill.textId;
        SetText(window, &fill.money);
    }

    fill.shown = 0;
    fill.index = 0;
    if (At<uint32_t>(self, dialog::kCostingCount) != 0) {
        At<uint32_t>(self, dialog::kTotal) = 0;
        At<uint32_t>(self, dialog::kTotal + 4) = 0;
        fill.textId += 2;
        FillSection(fill, kBuyPowerSection, dialog::kCostingScroll, buyPower, &shared.row);
        FillSection(fill, kBuyWaterSection, dialog::kCostingScroll, buyWater, &shared.row);
        FillSection(fill, kBuyGarbageSection, dialog::kCostingScroll, buyGarbage, &shared.row);

        if (At<uint32_t>(self, dialog::kCostingCount) >= kVisibleRows && fill.shown < kVisibleRows) {
            BlankTexts(fill);
            SetFlag(Child(parent, fill.markId), 1, 0);
            SetFlag(Child(parent, fill.rowId), 1, 0);
        }
        if (!(costingRows & 1))
            At<uint32_t>(self, dialog::kCostingHeight) += At<uint32_t>(self, dialog::kRowHeight);

        void* const service = Global<void*>(addr::SC4_MonthEndService);
        const int64_t total = Virtual<int64_t>(service, 0x84);
        At<int64_t>(self, dialog::kTotal) = total;
        Cdecl<void>(addr::SC4_FormatMoney, total, &fill.money);
        ++fill.textId;
        SetText(Child(Window(self), fill.textId), &fill.money);
    }

    // The two scroll bars and their captions.
    This<void>(addr::SC4UI_F77B960, self, parent, uint32_t{0x67}, uint32_t{0xE},
               At<uint32_t>(self, dialog::kWidth) - 0x28, uint32_t{0x144161EB}, uint32_t{0}, uint32_t{1});
    Virtual<void>(Child(parent, 0x67), 0xD4, uint32_t{0xB4}, uint32_t{0x1E});
    Virtual<void>(Child(parent, 0x167), 0xD4, uint32_t{0xB4}, uint32_t{0x1E});
    SetLocalizedCopy(Child(parent, 0x67), 0x0A554AE8, 0x2A6CD8FD);
    Virtual<void>(Child(parent, 0x67), 0x158, parent);

    const uint32_t width = At<uint32_t>(self, dialog::kWidth) - 0x28;
    const uint32_t height = At<uint32_t>(self, dialog::kHeight) - 0xC3;
    This<void>(addr::SC4UI_F77B960, self, parent, uint32_t{0xCF}, height, width, uint32_t{0x144161EB}, uint32_t{0},
               uint32_t{1});
    Virtual<void>(Child(parent, 0xCF), 0xD4, uint32_t{0xB4}, uint32_t{0x1E});
    Virtual<void>(Child(parent, 0x1CF), 0xD4, uint32_t{0xB4}, uint32_t{0x1E});
    SetLocalizedCopy(Child(parent, 0xCF), 0x6A231EAA, 0x6A56B0BB);
    Virtual<void>(Child(parent, 0xCF), 0x158, parent);

    if (copy)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(copy));
    DestroyString(fill.money);
}

bool InstallWindows6(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[1] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F781C90, reinterpret_cast<void*>(&FillDeals)},
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

const bool g_windows6Registered = registry::Register({
    "ui.windows.part6",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F781C90,
    "Our department dialog, sixth part: the neighbour deals page filled again from the dialog's deal list "
    "(docs/ui.md)",
    false,
    InstallWindows6,
});

} // namespace
} // namespace opensc4::ui
