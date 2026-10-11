// The budget panel's department dialog, the neighbour deals page
// (0x0077DAB0 to 0x00781C8F).
//
// The dialog (0x140 bytes, vtable 0x00AB5494, made by 0x007862B0 for the
// budget panel's department buttons) builds its page out of child windows it
// makes itself. For department 2 its Start (0x007876B0) calls the function
// here that lists the city's deals with its neighbours: the ones that pay on
// the left, the ones that cost on the right, each with a heading, the other
// city's name, the monthly amount and a slider. This file holds:
//
//   - SetUpSlider (0x0077DAB0): a row's slider given its labels and position;
//   - the deleting destructor (0x0077E420, slot 5);
//   - Stop (0x0077E440): the dialog's lists emptied and its windows let go;
//   - two vector appends (0x0077E550, 0x0077E5A0);
//   - FillDeals (0x0077E600, which runs to 0x00781C8F).
//
// Replacement, off by default:
//
//   ui.windows.part5: everything above.
//
// Windows, texts, sliders and buttons are the game's, called through their
// vtables; the dialog's own window makers (0x00779660, 0x007798C0,
// 0x0077A250, 0x0077A6F0, 0x0077B960, 0x0077BEC0) are in the neighbouring
// parts and are called by address.
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
constexpr std::size_t kStarted = 0x08;      // byte
constexpr std::size_t kHolder = 0x0C;       // a reference dropped by Stop
constexpr std::size_t kWindow = 0x10;       // the window the rows' texts go into
constexpr std::size_t kPairsA = 0x2C;       // vector of {id, object}
constexpr std::size_t kPairsB = 0x38;       // vector of {id, object}
constexpr std::size_t kTriples = 0x44;      // vector of 12-byte entries
constexpr std::size_t kRecords = 0x50;      // vector of 0x18-byte records: a slider and its object
constexpr std::size_t kCities = 0x5C;       // vector of {city, deal type}: the active deals
constexpr std::size_t kDeals = 0x68;        // vector of {deal type, deal}
constexpr std::size_t kBytes = 0x74;        // a vector emptied by Stop
constexpr std::size_t kHeight = 0x80;
constexpr std::size_t kWidth = 0x84;
constexpr std::size_t kRowHeight = 0x88;
constexpr std::size_t kTotal = 0x90;        // int64
constexpr std::size_t kLeftY = 0x98;        // where the next row of the left column goes
constexpr std::size_t kRightY = 0x9C;
constexpr std::size_t kArrayA = 0xA4;
constexpr std::size_t kArrayB = 0xA8;
constexpr std::size_t kLeftTitle = 0xB8;    // strings
constexpr std::size_t kLeftAmount = 0xCC;
constexpr std::size_t kRightTitle = 0xE0;
constexpr std::size_t kRightAmount = 0xF4;
constexpr std::size_t kTotalTitle = 0x108;
constexpr std::size_t kLeftRows = 0x124;    // rows wanted on the left, headings included
constexpr std::size_t kRightRows = 0x128;
constexpr std::size_t kList = 0x130;        // a list the neighbours simulator fills
} // namespace dialog

constexpr uint32_t kTextGroup = 0x6A3FF01C;
constexpr uint32_t kSliderIid = 0x412CE496;
constexpr uint32_t kExemplarIid = 0xA52160F5;

constexpr uint32_t kTitleStyle = 0xEA85D304;
constexpr uint32_t kItemStyle = 0xEA85D305;
constexpr uint32_t kHeadingStyle = 0xEA85D306;

constexpr uint32_t kMaxRows = 9;

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

// A string from the text resources: 0x20 bytes, a string first.
struct LocalizedText {
    GameString text;
    uint32_t spare[3];
};

struct ListNode {
    ListNode* next;
    ListNode* previous;
    void* value;
};

struct DealEntry {
    uint32_t type;
    void* deal;
};

struct CityEntry {
    uint32_t city;
    uint32_t type;
};

// One entry of the vector at +0x50. The appends copy the id, the eight bytes
// at +8 and the object. This page never sets those eight bytes: the original
// appends whatever its stack holds there, ours zeros.
struct Record {
    uint32_t id;
    uint32_t spare0;
    uint32_t low;
    uint32_t high;
    void* object;
    uint32_t spare1;
};
static_assert(sizeof(Record) == 0x18);

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

void* MakeText(LocalizedText& text, uint32_t group, uint32_t instance)
{
    return This<void*>(addr::SC4LocalizedString_SC4LocalizedString, &text, group, instance, uint32_t{1});
}

// A string made from the buffer of a text resource, as the original inlines
// it.
void CopyText(GameString& text, void* made)
{
    uint8_t tag = 0;
    void* const source = made ? static_cast<void*>(static_cast<char*>(made) + 4) : nullptr;
    This<void>(addr::SC4String_CopyBuffer, &text.body, source, uint32_t{0}, uint32_t{0xFFFFFFFF}, &tag);
    text.vtable = addr::SC4String_Vtable;
    text.zero = 0;
}

// The number of 8-byte entries of the vector at `offset`.
uint32_t Count8(char* self, std::size_t offset)
{
    const int32_t bytes = static_cast<int32_t>(At<char*>(self, offset + 4) - At<char*>(self, offset));
    return static_cast<uint32_t>(bytes >> 3);
}

// The dialog's 0x00779660: a text window made at (x, y) in `window`.
void AddText(char* self, void* window, uint32_t id, int32_t x, int32_t y, void* text, uint32_t align, uint32_t style,
             uint32_t red, uint32_t green, uint32_t blue)
{
    This<void>(addr::SC4UI_F779660, self, window, id, x, y, text, align, style, red, green, blue);
}

// A child window shown or, here, hidden.
void Hide(void* window, uint32_t id)
{
    Virtual<void>(Virtual<void*>(window, 0x8C, id), 0x110, uint32_t{1}, uint32_t{0});
}

// One label of a slider: the value as text by the row's kind (0 and 1 format
// it with a text resource, 2 prints it times `scale`), added unless the value
// is negative.
void AddLabel(void* slider, int32_t kind, int32_t value, float scale)
{
    GameString label;
    InitString(label);
    if (kind == 0 || kind == 1) {
        LocalizedText format;
        void* const made = MakeText(format, kTextGroup, kind == 0 ? 0xAA579B3Au : 0x6A579C00u);
        Cdecl<void>(addr::cRZBaseString_Sprintf, &label, Virtual<const char*>(made, 0x14), value);
        DestroyString(format.text);
    } else if (kind == 2) {
        const double scaled = static_cast<double>(value) * static_cast<double>(scale);
        Cdecl<void>(addr::cRZBaseString_Sprintf, &label, "%d", static_cast<int32_t>(static_cast<int64_t>(scaled)));
    }
    if (value >= 0)
        Virtual<void>(slider, 0x6C, &label, uint32_t{0xFFFFFFFE});
    DestroyString(label);
}

// A slider's only label set from a text resource through the string's
// substring constructor.
void SetOnlyLabel(void* slider, uint32_t instance, bool destroyByCall)
{
    LocalizedText text;
    void* const made = MakeText(text, kTextGroup, instance);
    GameString label;
    void* const copy = This<void*>(addr::SC4String_Substring, &label, made, uint32_t{0}, uint32_t{0xFFFFFFFF});
    Virtual<void>(slider, 0x6C, copy, uint32_t{0xFFFFFFFE});
    if (destroyByCall) {
        This<void>(addr::SC4String_Destroy, &label);
        This<void>(addr::SC4String_Destroy, &text);
    } else {
        DestroyString(label);
        DestroyString(text.text);
    }
}

int32_t Add(int32_t a, int32_t b)
{
    return static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b));
}

// The game's 0x0077DAB0. `this` is not used (the callers pass the dialog);
// neither is the last argument, the row's record. Readies the slider `id` of
// `window` for a deal of `type`: its range is the most the deal can carry
// (the neighbours simulator's slot 36, or the deal's amount when that is not
// above zero), its step a tenth of that but no less than the tuning
// exemplar's smallest amount for the commodity. `state` 0 is a deal not
// agreed (one "none" label, position 0), 2 a deal the other way exists with
// the same city (one label, the slider disabled), anything else a running
// deal: its step is the deal's own, its position amount / step. Past sixteen
// steps the labels thin out above the deal's amount to a tenth of the range.
// The second exemplar's reference is never released, as in the original.
void __fastcall SetUpSlider(char*, void*, void* window, uint32_t id, uint32_t type, void* deal, int32_t kind,
                            int32_t state, void*)
{
    void* const resources = Global<void*>(addr::SC4_ResourceManager);
    void* const city = Global<void*>(addr::SC4_City);

    void* tuning = nullptr;
    int32_t position = 0;
    uint32_t tuningKey[3] = {0x6534284A, 0x7A4A8458, 0x6A469021};
    Virtual<uint8_t>(resources, 0xC, &tuningKey[0], kExemplarIid, &tuning, uint32_t{0}, uint32_t{0});

    float scale = 1.0f;
    uint32_t scaleKey[3] = {0x6534284A, 0xE7E2C2DB, 0xC911E35B};
    void* scaling = nullptr;
    if (Virtual<uint8_t>(resources, 0xC, &scaleKey[0], kExemplarIid, &scaling, uint32_t{0}, uint32_t{0}))
        Cdecl<uint8_t>(addr::SC4_GetPropertyFloat, Virtual<void*>(scaling, 0x1C), uint32_t{0x09135482}, &scale);

    void* slider = nullptr;
    if (Virtual<uint8_t>(window, 0x94, id, kSliderIid, &slider)) {
        void* const neighbours = Virtual<void*>(city, 0xF8);
        void* const other = Virtual<void*>(deal, 0x1C);
        int32_t most = static_cast<int32_t>(static_cast<int64_t>(Virtual<double>(neighbours, 0x90, type, other)));
        if (most <= 0)
            most = Virtual<int32_t>(deal, 0x28);
        const int32_t tenth = most / 10;

        int32_t least = 0;
        void* const properties = Virtual<void*>(tuning, 0x1C);
        if (properties)
            AddRef(properties);
        switch (type) {
        case 0x4773FFE0:
        case 0x4773FFE1:
            Cdecl<uint8_t>(addr::SC4_GetPropertySint32, properties, uint32_t{0xEA46932C}, &least);
            break;
        case 0x4773FFE2:
        case 0x4773FFE3:
            Cdecl<uint8_t>(addr::SC4_GetPropertySint32, properties, uint32_t{0x6A469337}, &least);
            break;
        case 0x4773FFE4:
        case 0x4773FFE5:
            Cdecl<uint8_t>(addr::SC4_GetPropertySint32, properties, uint32_t{0x8A46933D}, &least);
            break;
        default:
            break;
        }
        if (properties)
            Release(properties);

        int32_t step = tenth;
        if (step < least)
            step = least;

        Virtual<void>(Virtual<void*>(slider, 0xC), 0x110, uint32_t{2}, uint32_t{1});
        Virtual<void>(slider, 0x68);
        Virtual<void>(deal, 0x4C, int32_t{0});

        bool disabled = false;
        if (state == 0) {
            LocalizedText text;
            void* const made = MakeText(text, kTextGroup, 0xCA5793C0);
            GameString label;
            CopyText(label, made);
            Virtual<void>(slider, 0x6C, &label, uint32_t{0xFFFFFFFE});
            DestroyString(label);
            DestroyString(text.text);
            position = 0;
            Virtual<void>(deal, 0x44, step);
        } else if (state == 2) {
            SetOnlyLabel(slider, 0xCA5793C1, false);
            Virtual<void>(slider, 0x70, int32_t{0}, int32_t{0});
            disabled = true;
        } else if (!Virtual<uint8_t>(deal, 0x9C, uint32_t{0x300})) {
            SetOnlyLabel(slider, 0x2A5794A0, false);
            int32_t own = Virtual<int32_t>(deal, 0x40);
            const int32_t amount = Virtual<int32_t>(deal, 0x28);
            if ((own == amount && own > least) || own == 0) {
                own = tenth;
                Virtual<void>(deal, 0x44, own);
                if (own == 0) {
                    SetOnlyLabel(slider, 0xCA5793C1, true);
                    Virtual<void>(slider, 0x70, int32_t{0}, int32_t{0});
                    disabled = true;
                }
            }
            if (!disabled) {
                step = own;
                position = Virtual<int32_t>(deal, 0x28) / step;
            }
        } else {
            SetOnlyLabel(slider, 0x2A5794A0, false);
            step = Virtual<int32_t>(deal, 0x28);
            most = Virtual<int32_t>(deal, 0x28);
            Virtual<void>(deal, 0x44, step);
            Virtual<void>(deal, 0x4C, step);
            position = 1;
        }

        if (disabled) {
            Virtual<void>(Virtual<void*>(slider, 0xC), 0x110, uint32_t{2}, uint32_t{0});
            if (slider)
                Release(slider);
            if (tuning)
                Release(tuning);
            return;
        }

        const uint32_t reach = static_cast<uint32_t>(step) * static_cast<uint32_t>(position);
        if (static_cast<uint32_t>(most) < reach)
            most = static_cast<int32_t>(reach);
        if (most < step)
            most = step;

        bool coarse = false;
        if (Virtual<int32_t>(deal, 0x28) != 0)
            coarse = static_cast<uint32_t>(most / Virtual<int32_t>(deal, 0x40)) > 0xF;

        if (coarse) {
            const int32_t wide = most / 10;
            Virtual<void>(deal, 0x4C, wide);
            int32_t value = step;
            if (!(step > Virtual<int32_t>(deal, 0x28))) {
                do {
                    AddLabel(slider, kind, value, scale);
                    value = Add(value, step);
                } while (value <= Virtual<int32_t>(deal, 0x28));
            }
            value = wide;
            if (!(value > most)) {
                do {
                    if (value > Virtual<int32_t>(deal, 0x28))
                        AddLabel(slider, kind, value, scale);
                    value = Add(value, wide);
                } while (value <= most);
            }
        } else {
            int32_t value = step;
            if (!(step > most)) {
                do {
                    AddLabel(slider, kind, value, scale);
                    value = Add(value, step);
                } while (value <= most);
            }
        }
    }

    // Without the slider the original calls through a null pointer here too.
    Virtual<void>(slider, 0x70, position, int32_t{0});
    if (slider)
        Release(slider);
    if (tuning)
        Release(tuning);
}

// The game's 0x0077E420. `this` is the dialog (slot 5 of vtable 0x00AB5494).
// The deleting destructor.
void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    This<void>(addr::SC4UI_F77C4C0, self);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// The game's 0x0077E440. `this` is the dialog; the argument is not used.
// Stops a started dialog: its vectors are emptied, its two arrays freed (the
// second pointer is left as it was), and its two references dropped, with a
// reference on itself held meanwhile. Always answers true.
uint8_t __fastcall Stop(char* self, void*, uint32_t)
{
    if (!At<uint8_t>(self, dialog::kStarted))
        return 1;
    At<uint8_t>(self, dialog::kStarted) = 0;

    This<void>(addr::SC4UI_F77C420, self + dialog::kPairsA, At<void*>(self, dialog::kPairsA),
               At<void*>(self, dialog::kPairsA + 4));
    This<void>(addr::SC4UI_F77C420, self + dialog::kPairsB, At<void*>(self, dialog::kPairsB),
               At<void*>(self, dialog::kPairsB + 4));
    This<void>(addr::SC4MySimAdviceVector_EraseRange, self + dialog::kTriples, At<void*>(self, dialog::kTriples),
               At<void*>(self, dialog::kTriples + 4));
    This<void>(addr::SC4UI_F77C3C0, self + dialog::kRecords, At<void*>(self, dialog::kRecords),
               At<void*>(self, dialog::kRecords + 4));
    At<void*>(self, dialog::kCities + 4) = At<void*>(self, dialog::kCities);
    This<void>(addr::SC4UI_F77C420, self + dialog::kDeals, At<void*>(self, dialog::kDeals),
               At<void*>(self, dialog::kDeals + 4));
    At<void*>(self, dialog::kBytes + 4) = At<void*>(self, dialog::kBytes);

    if (void* const array = At<void*>(self, dialog::kArrayA))
        Cdecl<void>(addr::SC4_operator_delete, array);
    if (void* const array = At<void*>(self, dialog::kArrayB))
        Cdecl<void>(addr::SC4_operator_delete, array);
    At<void*>(self, dialog::kArrayA) = nullptr;

    Virtual<uint32_t>(self, 0x4);
    if (void* const held = At<void*>(self, dialog::kHolder)) {
        At<void*>(self, dialog::kHolder) = nullptr;
        Release(held);
    }
    if (void* const held = At<void*>(self, dialog::kWindow)) {
        At<void*>(self, dialog::kWindow) = nullptr;
        Release(held);
    }
    Virtual<uint32_t>(self, 0x8);
    return 1;
}

// The game's 0x0077E550. `this` is a vector of {id, object} pairs (the
// dialog's +0x2C). Appends a pair with a reference on its object; a full
// vector grows through 0x0077D7E0.
void __fastcall PushPair(char* vector, void*, const DealEntry* value)
{
    DealEntry* const end = At<DealEntry*>(vector, 4);
    if (end != At<DealEntry*>(vector, 8)) {
        if (end) {
            end->type = value->type;
            void* const object = value->deal;
            end->deal = object;
            if (object)
                AddRef(object);
        }
        At<char*>(vector, 4) += 8;
        return;
    }
    uint8_t tag = 0;
    This<void>(addr::SC4UI_F77D7E0, vector, end, value, &tag, uint32_t{1}, uint32_t{1});
}

// The game's 0x0077E5A0. `this` is a vector of 0x18-byte records (the
// dialog's +0x50). Appends a record (its id, the eight bytes at +8 and its
// object, with a reference); a full vector grows through 0x0077D9B0.
void __fastcall PushRecord(char* vector, void*, const Record* value)
{
    Record* const end = At<Record*>(vector, 4);
    if (end != At<Record*>(vector, 8)) {
        if (end) {
            end->id = value->id;
            end->low = value->low;
            end->high = value->high;
            void* const object = value->object;
            end->object = object;
            if (object)
                AddRef(object);
        }
        At<char*>(vector, 4) += 0x18;
        return;
    }
    uint8_t tag = 0;
    This<void>(addr::SC4UI_F77D9B0, vector, end, value, &tag, uint32_t{1}, uint32_t{1});
}

// A deal appended to the vector at +0x68. The first three of the six passes
// have the growing of a full vector inlined, the others call 0x0077D7E0.
void PushDeal(char* self, uint32_t type, void* deal, bool inlined)
{
    char* const vector = self + dialog::kDeals;
    DealEntry* const end = At<DealEntry*>(vector, 4);
    if (end != At<DealEntry*>(vector, 8)) {
        if (end) {
            end->type = type;
            end->deal = deal;
            if (deal)
                AddRef(deal);
        }
        At<char*>(vector, 4) += 8;
        return;
    }
    if (!inlined) {
        DealEntry value = {type, deal};
        uint8_t tag = 0;
        This<void>(addr::SC4UI_F77D7E0, vector, end, &value, &tag, uint32_t{1}, uint32_t{1});
        return;
    }

    const uint32_t size = static_cast<uint32_t>(
        static_cast<int32_t>(reinterpret_cast<char*>(end) - At<char*>(vector, 0)) >> 3);
    const uint32_t capacity = (size < 1 ? 1 : size) + size;
    DealEntry* const buffer = capacity ? Cdecl<DealEntry*>(addr::SC4_stl_allocate, capacity << 3) : nullptr;
    DealEntry* to = buffer;
    for (DealEntry* from = At<DealEntry*>(vector, 0); from != end; ++from, ++to) {
        if (to) {
            to->type = from->type;
            void* const object = from->deal;
            to->deal = object;
            if (object)
                AddRef(object);
        }
    }
    if (to) {
        to->type = type;
        to->deal = deal;
        if (deal)
            AddRef(deal);
    }
    DealEntry* const newEnd = to + 1;
    DealEntry* const oldEnd = At<DealEntry*>(vector, 4);
    for (DealEntry* old = At<DealEntry*>(vector, 0); old != oldEnd; ++old) {
        if (old->deal)
            Release(old->deal);
    }
    if (void* const old = At<void*>(vector, 0))
        Cdecl<void>(addr::SC4_Free, old);
    At<DealEntry*>(vector, 0) = buffer;
    At<DealEntry*>(vector, 4) = newEnd;
    At<DealEntry*>(vector, 8) = buffer + capacity;
}

// An active deal's city appended to the vector at +0x5C, likewise inlined in
// the first three passes; the others call the game's 8-byte vector insert.
void PushCity(char* self, uint32_t city, uint32_t type, bool inlined)
{
    char* const vector = self + dialog::kCities;
    CityEntry* const end = At<CityEntry*>(vector, 4);
    if (end != At<CityEntry*>(vector, 8)) {
        if (end) {
            end->city = city;
            end->type = type;
        }
        At<char*>(vector, 4) += 8;
        return;
    }
    if (!inlined) {
        CityEntry value = {city, type};
        uint8_t tag = 0;
        This<void>(addr::SC4PointVector_InsertN, vector, end, &value, &tag, uint32_t{1}, uint32_t{1});
        return;
    }

    const uint32_t size = static_cast<uint32_t>(
        static_cast<int32_t>(reinterpret_cast<char*>(end) - At<char*>(vector, 0)) >> 3);
    const uint32_t capacity = (size < 1 ? 1 : size) + size;
    CityEntry* const buffer = capacity ? Cdecl<CityEntry*>(addr::SC4_stl_allocate, capacity * 8) : nullptr;
    CityEntry* to = buffer;
    for (CityEntry* from = At<CityEntry*>(vector, 0); from != end; ++from, ++to) {
        if (to) {
            to->city = from->city;
            to->type = from->type;
        }
    }
    if (to) {
        to->city = city;
        to->type = type;
    }
    CityEntry* const newEnd = to + 1;
    if (void* const old = At<void*>(vector, 0))
        Cdecl<void>(addr::SC4_Free, old);
    At<CityEntry*>(vector, 0) = buffer;
    At<CityEntry*>(vector, 4) = newEnd;
    At<CityEntry*>(vector, 8) = buffer + capacity;
}

// One pass of FillDeals over the neighbours simulator's deals of `type`:
// each joins the vector at +0x68 and counts as a row (the first one as two,
// for its heading). An active deal (flag 2) adds its cost to `sum`, has its
// last amount and last step set to the current ones, and puts its city in
// the vector at +0x5C.
void CollectDeals(char* self, void* city, uint32_t type, std::size_t rows, int64_t& sum, bool inlined)
{
    char* const list = self + dialog::kList;
    This<void>(addr::SC4List_Clear, list);
    bool counted = false;
    Virtual<void>(Virtual<void*>(city, 0xF8), 0x78, type, list);

    ListNode* node = At<ListNode*>(list, 0)->next;
    if (node == At<ListNode*>(list, 0))
        return;
    do {
        void* const deal = node->value;
        if (!counted) {
            ++At<uint32_t>(self, rows);
            counted = true;
        }
        ++At<uint32_t>(self, rows);

        void* held = nullptr;
        if (deal) {
            AddRef(deal);
            held = deal;
        }
        PushDeal(self, type, held, inlined);

        if (Virtual<uint32_t>(deal, 0x90) & 2) {
            const uint64_t cost = static_cast<uint64_t>(Virtual<int64_t>(deal, 0x58));
            sum = static_cast<int64_t>(static_cast<uint64_t>(sum) + cost);
            Virtual<void>(deal, 0x34, Virtual<int32_t>(deal, 0x28));
            Virtual<void>(deal, 0x3C, Virtual<int32_t>(deal, 0x40));
            const uint32_t other = Virtual<uint32_t>(Virtual<void*>(deal, 0x1C), 0x6C);
            PushCity(self, other, type, inlined);
        }
        if (held)
            Release(held);
        node = node->next;
    } while (node != At<ListNode*>(self, dialog::kList));
}

// A column of the page: where its rows go and how its amounts look.
struct Column {
    std::size_t y;
    uint32_t picture;
    uint32_t red;
    uint32_t green;
    uint32_t blue;
};

// A deal type's rows in a column.
struct Kind {
    uint32_t type;
    uint32_t opposite;      // the deal type that goes the other way
    uint32_t headingGroup;
    uint32_t heading;
    uint32_t unit;          // the heading's second text
    int32_t kind;           // how the slider's labels are written
    uint32_t sliderFlag;    // the last argument of 0x007798C0 for a deal's row
};

// The running numbers of FillDeals.
struct Ids {
    uint32_t text;          // the next text window
    uint32_t slider;        // the next slider, from 0x0ABCE100
    uint32_t picture;       // the next window of 0x0077A250, from 0x0ABCE200
    uint32_t rows;          // rows made in the column
};

constexpr Column kLeft = {dialog::kLeftY, 0x140155B9, 0x44, 0x55, 0x66};
constexpr Column kRight = {dialog::kRightY, 0x140155B8, 0x77, 0x00, 0x00};

constexpr Kind kLeftKinds[3] = {
    {0x4773FFE1, 0x4773FFE0, kTextGroup, 0x4A579570, 0xCA579B63, 0, 1},
    {0x4773FFE3, 0x4773FFE2, 0x0A554AE8, 0x0A6CD8F9, 0xCA579B63, 1, 0},
    {0x4773FFE5, 0x4773FFE4, kTextGroup, 0x4A579C5B, 0x6A579CA4, 2, 0},
};

constexpr Kind kRightKinds[3] = {
    {0x4773FFE0, 0x4773FFE1, kTextGroup, 0x4A579570, 0xEA579E1A, 0, 0},
    {0x4773FFE2, 0x4773FFE3, 0x0A554AE8, 0x0A6CD8F9, 0xEA579E1A, 1, 0},
    {0x4773FFE4, 0x4773FFE5, kTextGroup, 0x4A579C5B, 0x8A579E78, 2, 0},
};

// The slider and the picture window of a row made at the column's place.
void AddControls(char* self, void* window, const Column& column, const Ids& ids, uint32_t sliderFlag)
{
    This<void>(addr::SC4UI_F77A250, self, column.picture, int32_t{0xCE}, At<int32_t>(self, column.y), window,
               ids.picture);
    This<void>(addr::SC4UI_F7798C0, self, window, ids.slider, int32_t{0xDA}, At<int32_t>(self, column.y) + 1,
               sliderFlag);
}

// The rows of one deal of the kind: before the first one the kind's heading
// (two texts, the kind's total, and a hidden slider and picture whose record
// carries this deal), then the deal's own row: the other city's name, a
// hidden unit text, its cost (0 unless active), and its slider set up. Nine
// rows a column at most.
void AddDealRows(char* self, void* window, const Column& column, const Kind& kind, int64_t sum, Ids& ids,
                 bool& headed, GameString& money, void* deal)
{
    if (!headed) {
        headed = true;
        if (ids.rows >= kMaxRows)
            return;
        {
            void* const content = At<void*>(self, dialog::kWindow);
            LocalizedText text;
            void* const made = MakeText(text, kind.headingGroup, kind.heading);
            AddText(self, content, ids.text, 0x12, At<int32_t>(self, column.y), made, 0, kHeadingStyle, 0x44, 0x55,
                    0x66);
            ++ids.text;
            DestroyString(text.text);
        }
        {
            void* const content = At<void*>(self, dialog::kWindow);
            LocalizedText text;
            void* const made = MakeText(text, kTextGroup, kind.unit);
            AddText(self, content, ids.text, 0xDA, At<int32_t>(self, column.y), made, 0, kHeadingStyle, 0x44, 0x55,
                    0x66);
            ++ids.text;
            DestroyString(text.text);
        }
        Cdecl<void>(addr::SC4_FormatMoney, sum, &money);
        AddText(self, At<void*>(self, dialog::kWindow), ids.text, At<int32_t>(self, dialog::kWidth) - 0x26,
                At<int32_t>(self, column.y), &money, 6, kHeadingStyle, column.red, column.green, column.blue);
        ++ids.text;
        AddControls(self, window, column, ids, 0);

        Record record{};
        record.id = ids.slider;
        record.object = nullptr;
        if (deal) {
            AddRef(deal);
            record.object = deal;
        }
        PushRecord(self + dialog::kRecords, nullptr, &record);
        Hide(window, ids.picture);
        ++ids.picture;
        Hide(window, ids.slider);
        ++ids.slider;
        At<int32_t>(self, column.y) += At<int32_t>(self, dialog::kRowHeight);
        ++ids.rows;
        if (record.object)
            Release(record.object);
    }
    if (ids.rows >= kMaxRows)
        return;

    GameString name;
    InitString(name);
    Virtual<void>(Virtual<void*>(deal, 0x1C), 0x84, &name);
    AddText(self, At<void*>(self, dialog::kWindow), ids.text, 0x12, At<int32_t>(self, column.y), &name, 0, kItemStyle,
            0x44, 0x55, 0x66);
    ++ids.text;
    {
        void* const content = At<void*>(self, dialog::kWindow);
        LocalizedText text;
        void* const made = MakeText(text, kTextGroup, 0xCA579B63);
        AddText(self, content, ids.text, 0xDA, At<int32_t>(self, column.y), made, 0, kItemStyle, 0x44, 0x55, 0x66);
        DestroyString(text.text);
    }
    Hide(At<void*>(self, dialog::kWindow), ids.text);
    ++ids.text;

    int64_t cost = Virtual<int64_t>(deal, 0x58);
    if (!(Virtual<uint32_t>(deal, 0x90) & 2))
        cost = 0;
    Cdecl<void>(addr::SC4_FormatMoney, cost, &money);
    AddText(self, At<void*>(self, dialog::kWindow), ids.text, At<int32_t>(self, dialog::kWidth) - 0x26,
            At<int32_t>(self, column.y), &money, 6, kItemStyle, column.red, column.green, column.blue);
    ++ids.text;
    AddControls(self, window, column, ids, kind.sliderFlag);

    Record record{};
    record.id = ids.slider;
    AddRef(deal);
    record.object = deal;

    int32_t state = 0;
    if (Virtual<uint32_t>(deal, 0x90) & 2) {
        if (!(Virtual<uint32_t>(deal, 0x90) & 4))
            state = 1;
    }
    if (Count8(self, dialog::kCities) != 0) {
        uint32_t i = 0;
        do {
            if (At<CityEntry*>(self, dialog::kCities)[i].type == kind.opposite) {
                void* const other = Virtual<void*>(deal, 0x1C);
                const CityEntry* const cities = At<CityEntry*>(self, dialog::kCities);
                if (Virtual<uint32_t>(other, 0x6C) == cities[i].city)
                    state = 2;
            }
            ++i;
        } while (i < Count8(self, dialog::kCities));
    }

    SetUpSlider(self, nullptr, window, ids.slider, kind.type, deal, kind.kind, state, &record);
    PushRecord(self + dialog::kRecords, nullptr, &record);
    ++ids.picture;
    ++ids.slider;
    At<int32_t>(self, column.y) += At<int32_t>(self, dialog::kRowHeight);
    ++ids.rows;
    Release(deal);
    DestroyString(name);
}

// The rows of every deal of one kind, in the order of the vector at +0x68.
void AddKind(char* self, void* window, const Column& column, const Kind& kind, int64_t sum, Ids& ids,
             GameString& money)
{
    bool headed = false;
    if (Count8(self, dialog::kDeals) == 0)
        return;
    uint32_t i = 0;
    do {
        void* const deal = At<DealEntry*>(self, dialog::kDeals)[i].deal;
        if (deal)
            AddRef(deal);
        if (At<DealEntry*>(self, dialog::kDeals)[i].type == kind.type)
            AddDealRows(self, window, column, kind, sum, ids, headed, money, deal);
        if (deal)
            Release(deal);
        ++i;
    } while (i < Count8(self, dialog::kDeals));
}

// A button's caption set from a text resource.
void SetCaption(void* window, uint32_t id, uint32_t group, uint32_t instance)
{
    void* const button = Virtual<void*>(window, 0x8C, id);
    LocalizedText text;
    void* const made = MakeText(text, group, instance);
    GameString caption;
    CopyText(caption, made);
    Virtual<void>(button, 0x128, &caption);
    DestroyString(caption);
    DestroyString(text.text);
}

// One of the page's two buttons: made by 0x0077B960, it and its twin
// (id + 0x100) sized 180 by 30, captioned, and tied to the window.
void AddButton(char* self, void* window, uint32_t id, int32_t x, uint32_t group, uint32_t instance)
{
    This<void>(addr::SC4UI_F77B960, self, window, id, x, At<int32_t>(self, dialog::kHeight) - 0x28,
               uint32_t{0x144161EB}, uint32_t{0}, uint32_t{1});
    Virtual<void>(Virtual<void*>(window, 0x8C, id), 0xD4, int32_t{0xB4}, int32_t{0x1E});
    Virtual<void>(Virtual<void*>(window, 0x8C, id + 0x100), 0xD4, int32_t{0xB4}, int32_t{0x1E});
    SetCaption(window, id, group, instance);
    Virtual<void>(Virtual<void*>(window, 0x8C, id), 0x158, window);
}

// One of the four arrows beside the columns.
void AddArrow(char* self, void* window, uint32_t id, int32_t y, uint32_t picture)
{
    This<void>(addr::SC4UI_F77B960, self, window, id, At<int32_t>(self, dialog::kWidth) - 0x21, y, picture,
               uint32_t{0}, uint32_t{0});
    Virtual<void>(Virtual<void*>(window, 0x8C, id), 0x158, window);
}

// The game's 0x0077E600 (it runs to 0x00781C8F). `this` is the dialog,
// `window` the window its Start was given. Builds the neighbour deals page.
// The neighbours simulator's interface is switched off (slot 27) and asked
// to propose deals; then its deals are collected in six passes by type, the
// paying types 0x4773FFE1, E3, E5 counted at +0x124 and the costing ones E0,
// E2, E4 at +0x128. With none of either the page is a single message
// (0x0077BEC0). Otherwise the window is sized for up to nine rows a side
// (0x0077A6F0), and each side gets its title, the rows of its three kinds, a
// total line with the simulator's monthly income or expense (also left at
// +0x90), and under them two buttons and four arrows; the upper arrows are
// hidden, the lower ones too unless the side wanted more than nine rows.
void __fastcall FillDeals(char* self, void*, void* window)
{
    void* const city = Global<void*>(addr::SC4_City);
    int64_t income[3] = {0, 0, 0};
    int64_t expense[3] = {0, 0, 0};
    GameString money;
    InitString(money);

    At<uint32_t>(self, dialog::kLeftRows) = 0;
    At<uint32_t>(self, dialog::kRightRows) = 0;
    Virtual<void>(Virtual<void*>(city, 0xF8), 0x6C, uint32_t{1});

    This<void>(addr::SC4UI_F77C3C0, self + dialog::kRecords, At<void*>(self, dialog::kRecords),
               At<void*>(self, dialog::kRecords + 4));
    At<void*>(self, dialog::kCities + 4) = At<void*>(self, dialog::kCities);
    This<void>(addr::SC4UI_F77C420, self + dialog::kDeals, At<void*>(self, dialog::kDeals),
               At<void*>(self, dialog::kDeals + 4));

    char* const list = self + dialog::kList;
    This<void>(addr::SC4List_Clear, list);
    Virtual<uint8_t>(Virtual<void*>(city, 0xF8), 0x64, list, uint32_t{1});
    for (ListNode* node = At<ListNode*>(list, 0)->next; node != At<ListNode*>(list, 0); node = node->next)
        Virtual<uint32_t>(node->value, 0x20);

    CollectDeals(self, city, 0x4773FFE1, dialog::kLeftRows, income[0], true);
    CollectDeals(self, city, 0x4773FFE0, dialog::kRightRows, expense[0], true);
    CollectDeals(self, city, 0x4773FFE3, dialog::kLeftRows, income[1], true);
    CollectDeals(self, city, 0x4773FFE2, dialog::kRightRows, expense[1], false);
    CollectDeals(self, city, 0x4773FFE5, dialog::kLeftRows, income[2], false);
    CollectDeals(self, city, 0x4773FFE4, dialog::kRightRows, expense[2], false);
    This<void>(addr::SC4List_Clear, list);

    uint32_t leftRows = At<uint32_t>(self, dialog::kLeftRows);
    uint32_t rightRows = At<uint32_t>(self, dialog::kRightRows);

    GameString title;
    {
        LocalizedText text;
        void* const made = MakeText(text, kTextGroup, 0x2A579AD2);
        CopyText(title, made);
        DestroyString(text.text);
    }

    if (At<uint32_t>(self, dialog::kLeftRows) == 0 && At<uint32_t>(self, dialog::kRightRows) == 0) {
        LocalizedText text;
        MakeText(text, kTextGroup, 0xCA5CB52D);
        This<void>(addr::SC4UI_F77BEC0, self, window, &title, &text);
        DestroyString(text.text);
        DestroyString(title);
        DestroyString(money);
        return;
    }

    if (leftRows > kMaxRows)
        leftRows = kMaxRows;
    if (rightRows > kMaxRows)
        rightRows = kMaxRows;
    This<void>(addr::SC4UI_F77A6F0, self, window, leftRows, rightRows);
    AddText(self, At<void*>(self, dialog::kWindow), 0x0ABCDE00, 0x14, 8, &title, 0, 0x4A809916, 0x44, 0x55, 0x66);

    const int32_t leftTop = At<int32_t>(self, dialog::kLeftY);
    const int32_t rightTop = At<int32_t>(self, dialog::kRightY);
    Ids ids = {0x0ABCDE01, 0x0ABCE100, 0x0ABCE200, 0};

    if (At<uint32_t>(self, dialog::kLeftRows) != 0) {
        {
            const int32_t y = leftTop - At<int32_t>(self, dialog::kRowHeight) - 2;
            void* const content = At<void*>(self, dialog::kWindow);
            At<int64_t>(self, dialog::kTotal) = 0;
            AddText(self, content, 0x0ABCDE01, 0x12, y, self + dialog::kLeftTitle, 0, kTitleStyle, 0x44, 0x55, 0x66);
        }
        AddText(self, At<void*>(self, dialog::kWindow), 0x0ABCDE02, At<int32_t>(self, dialog::kWidth) - 0x26,
                At<int32_t>(self, dialog::kLeftY) - At<int32_t>(self, dialog::kRowHeight) - 2,
                self + dialog::kLeftAmount, 6, kTitleStyle, 0x44, 0x55, 0x66);
        ids.text = 0x0ABCDE03;
        ids.rows = 0;
        for (std::size_t i = 0; i < 3; ++i)
            AddKind(self, window, kLeft, kLeftKinds[i], income[i], ids, money);

        if (!(leftRows & 1))
            At<int32_t>(self, dialog::kLeftY) += At<int32_t>(self, dialog::kRowHeight);
        AddText(self, At<void*>(self, dialog::kWindow), ids.text, 0xFA, At<int32_t>(self, dialog::kLeftY),
                self + dialog::kTotalTitle, 0, kTitleStyle, 0x44, 0x55, 0x66);
        ++ids.text;
        const int64_t total = Virtual<int64_t>(Global<void*>(addr::SC4_MonthEndService), 0x80);
        At<int64_t>(self, dialog::kTotal) = total;
        Cdecl<void>(addr::SC4_FormatMoney, total, &money);
        AddText(self, At<void*>(self, dialog::kWindow), ids.text, At<int32_t>(self, dialog::kWidth) - 0x26,
                At<int32_t>(self, dialog::kLeftY), &money, 6, kTitleStyle, 0x00, 0x66, 0x00);
        ++ids.text;
    }

    ids.rows = 0;
    if (At<uint32_t>(self, dialog::kRightRows) != 0) {
        {
            const int32_t y = At<int32_t>(self, dialog::kRightY) - At<int32_t>(self, dialog::kRowHeight) - 2;
            void* const content = At<void*>(self, dialog::kWindow);
            At<int64_t>(self, dialog::kTotal) = 0;
            AddText(self, content, ids.text, 0x12, y, self + dialog::kRightTitle, 0, kTitleStyle, 0x44, 0x55, 0x66);
        }
        ++ids.text;
        AddText(self, At<void*>(self, dialog::kWindow), ids.text, At<int32_t>(self, dialog::kWidth) - 0x26,
                At<int32_t>(self, dialog::kRightY) - At<int32_t>(self, dialog::kRowHeight) - 2,
                self + dialog::kRightAmount, 6, kTitleStyle, 0x44, 0x55, 0x66);
        ++ids.text;
        for (std::size_t i = 0; i < 3; ++i)
            AddKind(self, window, kRight, kRightKinds[i], expense[i], ids, money);

        if (!(rightRows & 1))
            At<int32_t>(self, dialog::kRightY) += At<int32_t>(self, dialog::kRowHeight);
        AddText(self, At<void*>(self, dialog::kWindow), ids.text, 0xFA, At<int32_t>(self, dialog::kRightY),
                self + dialog::kTotalTitle, 0, kTitleStyle, 0x44, 0x55, 0x66);
        const int64_t total = Virtual<int64_t>(Global<void*>(addr::SC4_MonthEndService), 0x84);
        At<int64_t>(self, dialog::kTotal) = total;
        Cdecl<void>(addr::SC4_FormatMoney, total, &money);
        ++ids.text;
        AddText(self, At<void*>(self, dialog::kWindow), ids.text, At<int32_t>(self, dialog::kWidth) - 0x26,
                At<int32_t>(self, dialog::kRightY), &money, 6, kTitleStyle, 0x77, 0x00, 0x00);
    }

    AddButton(self, window, 0x67, 0xE, 0x0A554AE8, 0x2A6CD8FD);
    AddButton(self, window, 0xCF, At<int32_t>(self, dialog::kWidth) - 0xC3, 0x6A231EAA, 0x6A56B0BB);

    AddArrow(self, window, 0x451, leftTop + 4, 0x140155CB);
    AddArrow(self, window, 0x452, leftTop + At<int32_t>(self, dialog::kRowHeight) * 8 + 4, 0x140155CC);
    AddArrow(self, window, 0x453, rightTop + 4, 0x140155CB);
    AddArrow(self, window, 0x454, rightTop + At<int32_t>(self, dialog::kRowHeight) * 8 + 4, 0x140155CC);

    Hide(window, 0x451);
    if (At<uint32_t>(self, dialog::kLeftRows) <= kMaxRows)
        Hide(window, 0x452);
    Hide(window, 0x453);
    if (At<uint32_t>(self, dialog::kRightRows) <= kMaxRows)
        Hide(window, 0x454);

    DestroyString(title);
    DestroyString(money);
}

bool InstallWindows5(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[8] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F77DAB0, reinterpret_cast<void*>(&SetUpSlider)},
        {addr::SC4UI_F77E420, reinterpret_cast<void*>(&DeletingDestructor)},
        {addr::SC4UI_F77E440, reinterpret_cast<void*>(&Stop)},
        {addr::SC4UI_F77E550, reinterpret_cast<void*>(&PushPair)},
        {addr::SC4UI_F77E5A0, reinterpret_cast<void*>(&PushRecord)},
        {addr::SC4UI_F77E600, reinterpret_cast<void*>(&FillDeals)},
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

const bool g_windows5Registered = registry::Register({
    "ui.windows.part5",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F77E600,
    "Our department dialog, the neighbour deals page: the deals collected and laid out in rows, the rows' "
    "sliders set up, the dialog stopped (docs/ui.md)",
    false,
    InstallWindows5,
});

} // namespace
} // namespace opensc4::ui
