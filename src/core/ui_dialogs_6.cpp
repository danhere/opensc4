// The graphs window of the city view, functions between 0x0076CC80 and
// 0x0076E500. The object (the owner allocates 0xDC bytes and keeps it at
// +0x2F8; the code here uses the first 0x5C) is a message target (vtable
// 0x00AB4FF4) with a window procedure part at +4 (vtable 0x00AB4FE0). It
// shows one graph at a time: a description resource (type 0x6534284A, group
// 0xCA4AD545, instance = the graph's number) names the kind (1 lines, 2 bars,
// 3 a kind that is only shown) and one row per series: the series' ID, name,
// colour and scale. Replacement, off by default:
//
//   ui.dialogs.part6: the constructor and destructor, the message handler,
//       showing, hiding and choosing a graph, the builder that reads the
//       description and sets up the graph control with its legend, and the
//       row vector's helpers (copy, fill, insert, resize, clear).
//
// The class's earlier code (0x0076B5F0 to 0x0076CC80: start, stop, the
// buttons, feeding the figures) and its window procedure (0x0076E500) are in
// other parts and called by address. The graph controls, their fonts and
// colours are the game's, called through their vtables.
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

namespace graphs {
constexpr std::size_t kWinProc = 0x04;    // the window procedure part
constexpr std::size_t kRegistered = 0x0C; // byte: started (message 0x66956816 registered)
constexpr std::size_t kKind = 0x10;       // the kind of the graph built: 1 lines, 2 bars, 3
constexpr std::size_t kNumber = 0x14;     // the number of the graph chosen (1 at first)
constexpr std::size_t kWindow = 0x18;     // the window given to the constructor
constexpr std::size_t kSmall = 0x1C;      // its child 0x8A8B5B72
constexpr std::size_t kBig = 0x20;        // its child 0x8A8B5B71: the graph control's parent
constexpr std::size_t kGraph = 0x24;      // the graph control
constexpr std::size_t kShown = 0x28;      // byte
constexpr std::size_t kUseBig = 0x29;     // byte: show the big child, not the small one
constexpr std::size_t kRows = 0x2C;       // a vector of Row
constexpr std::size_t kHidden = 0x38;     // a map from series ID to a "shown" byte (header node, count)
constexpr std::size_t kHiddenCount = 0x3C;
constexpr std::size_t kSeries = 0x44;     // how many rows the description has
constexpr std::size_t kBars = 0x48;       // bars: how many groups, 16 at most
constexpr std::size_t kHigh = 0x50;       // float, property 0x6A4AEDED
constexpr std::size_t kLow = 0x54;        // float, property 0xEA4AEDC6
constexpr std::size_t kThird = 0x58;      // float, property 0x6A4AEDEF
} // namespace graphs

constexpr uint32_t kChanged = 0x66956816; // the message the window redraws on

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

// One series of a graph.
struct Row {
    uint32_t id;       // property 0x6A4AEE40
    uint32_t second;   // property 0x6A4AEE5F (lines only)
    uint32_t group;    // property 0x6A4AEEE3 (bars only)
    uint32_t type;     // property 0x6A4AEE68
    uint8_t colour[3]; // property 0x6A4AEEDF (lines only)
    uint8_t pad;
    uint32_t scale;    // a float, copied as it is: property 0x6A4AEEE2, 1.0 without
    GameString name;   // the text 0x6A231EAA / property 0x6A4AEEDC
    uint8_t shown;
    uint8_t pad2[3];
};
static_assert(sizeof(Row) == 0x30);
static_assert(offsetof(Row, name) == 0x18);
static_assert(offsetof(Row, shown) == 0x2C);

struct RowVector {
    Row* begin;
    Row* end;
    Row* capacity;
};

// The game's image holder (0x30 bytes); the image at +4.
struct ImageLoader {
    void* first;
    void* image;
    char rest[0x28];
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

void Free(void* memory)
{
    Cdecl<void>(addr::SC4_Free, memory);
}

// The game's inlined empty string: eight bytes of buffer.
void MakeEmpty(GameString& text)
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
        Free(begin);
}

void AssignBody(StringBody* to, const StringBody* from)
{
    if (from != to) {
        uint8_t tag = 0;
        This<void*>(addr::SC4String_AssignRange, to, from->begin, from->end, &tag);
    }
}

// The assignment of one row, as the three copy loops have it inlined.
void AssignRow(Row* to, const Row* from)
{
    to->id = from->id;
    to->second = from->second;
    to->group = from->group;
    to->type = from->type;
    to->colour[0] = from->colour[0];
    to->colour[1] = from->colour[1];
    to->colour[2] = from->colour[2];
    to->scale = from->scale;
    AssignBody(&to->name.body, &from->name.body);
    to->shown = from->shown;
}

int32_t RowsBetween(const Row* first, const Row* last)
{
    return static_cast<int32_t>(reinterpret_cast<const char*>(last) - reinterpret_cast<const char*>(first)) / 0x30;
}

// The rows' helpers of the part before this one: copy construction of one
// row (0x0076BDF0), of a range (0x0076C9C0), of `count` copies (0x0076C9F0),
// and the destruction of a range (0x0076CA20).
void ConstructRow(Row* to, const Row* from)
{
    This<void*>(addr::SC4UI_F76BDF0, to, from);
}

Row* ConstructRange(Row* first, Row* last, Row* to)
{
    uint8_t tag = 0;
    return Cdecl<Row*>(addr::SC4UI_F76C9C0, first, last, to, &tag);
}

void DestroyRange(Row* first, Row* last)
{
    uint8_t tag = 0;
    Cdecl<void>(addr::SC4UI_F76CA20, first, last, &tag);
}

// The game's 0x0076CCC0 (cdecl, five arguments). Assigns the rows of
// [first, last) to the rows that end at `toEnd`, last one first; returns
// where the copies begin.
Row* __cdecl RowsCopyBackward(Row* first, Row* last, Row* toEnd, void*, uint32_t)
{
    int32_t count = RowsBetween(first, last);
    if (count <= 0)
        return toEnd;
    do {
        --last;
        --toEnd;
        AssignRow(toEnd, last);
    } while (--count != 0);
    return toEnd;
}

// The game's 0x0076CD70 (cdecl, three arguments). Assigns `value` to every
// row of [first, last).
Row* __cdecl RowsFill(Row* first, Row* last, const Row* value)
{
    for (; first != last; ++first)
        AssignRow(first, value);
    return last;
}

// The game's 0x0076CE00 (cdecl, five arguments). Assigns the rows of
// [first, last) to the rows from `to` on; returns the end of the copies.
Row* __cdecl RowsCopy(Row* first, Row* last, Row* to, void*, uint32_t)
{
    int32_t count = RowsBetween(first, last);
    if (count <= 0)
        return to;
    do {
        AssignRow(to, first);
        ++first;
        ++to;
    } while (--count != 0);
    return to;
}

// The game's 0x0076CEB0. `this` is the row vector. Inserts `count` copies of
// `value` at `where` into new memory: the size grows by the larger of itself
// and `count`.
void __fastcall RowsInsertGrow(RowVector* self, void*, Row* where, const Row* value, void*, uint32_t count,
                               uint8_t atEnd)
{
    const uint32_t size = static_cast<uint32_t>(RowsBetween(self->begin, self->end));
    const uint32_t capacity = (size < count ? count : size) + size;
    Row* const memory =
        capacity != 0 ? Cdecl<Row*>(addr::SC4_stl_allocate, static_cast<uint32_t>(capacity * 0x30)) : nullptr;
    Row* to = memory;
    for (Row* from = self->begin; from != where; ++from, ++to) {
        if (to)
            ConstructRow(to, from);
    }
    if (count == 1) {
        if (to)
            ConstructRow(to, value);
        ++to;
    } else {
        for (uint32_t left = count; left != 0; --left) {
            if (to)
                ConstructRow(to, value);
            ++to;
        }
    }
    if (!atEnd)
        to = ConstructRange(where, self->end, to);
    DestroyRange(self->begin, self->end);
    if (self->begin)
        Free(self->begin);
    self->end = to;
    self->begin = memory;
    self->capacity = memory + capacity;
}

// The game's 0x0076D150. `this` is the row vector. Inserts `count` copies of
// `value` at `where`.
void __fastcall RowsInsert(RowVector* self, void*, Row* where, uint32_t count, const Row* value)
{
    if (count == 0)
        return;
    if (static_cast<uint32_t>(RowsBetween(self->end, self->capacity)) < count) {
        uint8_t tag = 0;
        RowsInsertGrow(self, nullptr, where, value, &tag, count, 0);
        return;
    }

    Row copy;
    ConstructRow(&copy, value);
    Row* const oldEnd = self->end;
    const uint32_t after = static_cast<uint32_t>(RowsBetween(where, self->end));
    uint8_t tag = 0;
    if (after > count) {
        Row* const tail = oldEnd - count;
        ConstructRange(tail, oldEnd, oldEnd);
        self->end = self->end + count;
        RowsCopyBackward(where, tail, oldEnd, &tag, 0);
        RowsFill(where, where + count, &copy);
    } else {
        const uint32_t more = count - after;
        Cdecl<Row*>(addr::SC4UI_F76C9F0, oldEnd, more, &copy);
        Row* const middle = self->end + more;
        self->end = middle;
        ConstructRange(where, oldEnd, middle);
        self->end = self->end + after;
        RowsFill(where, oldEnd, &copy);
    }
    if (copy.name.body.begin)
        Free(copy.name.body.begin);
}

// The game's 0x0076D2B0. `this` is the row vector. Empties it.
void __fastcall RowsClear(RowVector* self, void*)
{
    uint8_t tag = 0;
    Row* const end = RowsCopy(self->end, self->end, self->begin, &tag, 0);
    DestroyRange(end, self->end);
    self->end = end;
}

// The game's 0x0076D2F0. `this` is the row vector. Makes it `count` rows
// long, the new ones copies of `value`.
void __fastcall RowsResize(RowVector* self, void*, uint32_t count, const Row* value)
{
    Row* const begin = self->begin;
    Row* const end = self->end;
    const uint32_t size = static_cast<uint32_t>(RowsBetween(begin, end));
    if (count < size) {
        uint8_t tag = 0;
        Row* const newEnd = RowsCopy(end, end, begin + count, &tag, 0);
        DestroyRange(newEnd, self->end);
        self->end = newEnd;
        return;
    }
    RowsInsert(self, nullptr, end, count - size, value);
}

// The game's 0x0076D370. `this` is the row vector. Makes it `count` rows long
// with an empty name in the new ones. The original sets nothing but the name
// in the row it copies from, so the other fields of new rows are whatever its
// stack held; here they are zero.
void __fastcall RowsResizeDefault(RowVector* self, void*, uint32_t count)
{
    Row row = {};
    MakeEmpty(row.name);
    RowsResize(self, nullptr, count, &row);
    if (row.name.body.begin)
        Free(row.name.body.begin);
}

// The game's 0x0076CC80. `this` is the window object (slot 3 of vtable
// 0x00AB4FF4). On the "changed" message the figures are fed again and the
// graph redrawn.
uint8_t __fastcall DoMessage(char* self, void*, void* message)
{
    if (Virtual<uint32_t>(message, 0x10) != kChanged)
        return 0;
    if (At<void*>(self, graphs::kGraph)) {
        This<void>(addr::SC4UI_F76BE70, self);
        Virtual<void>(At<void*>(self, graphs::kGraph), 0x170);
    }
    return 1;
}

// The game's 0x0076CFE0. `this` is the object's memory; `window` is kept
// with a reference.
char* __fastcall Construct(char* self, void*, void* window)
{
    At<uint32_t>(self, graphs::kWinProc) = addr::SC4NewsTicker_WinProcBaseVtable;
    At<uint32_t>(self, 0x0) = addr::SC4UI_GraphWindowVtable;
    At<uint32_t>(self, graphs::kWinProc) = addr::SC4UI_GraphWindowVtable4;
    At<uint32_t>(self, 0x8) = 0;
    At<uint8_t>(self, graphs::kRegistered) = 0;
    At<uint32_t>(self, graphs::kKind) = 0;
    At<uint32_t>(self, graphs::kNumber) = 1;
    At<void*>(self, graphs::kWindow) = window;
    if (window)
        AddRef(window);
    At<void*>(self, graphs::kSmall) = nullptr;
    At<void*>(self, graphs::kBig) = nullptr;
    At<void*>(self, graphs::kGraph) = nullptr;
    At<uint8_t>(self, graphs::kShown) = 0;
    At<uint8_t>(self, graphs::kUseBig) = 1;
    At<void*>(self, graphs::kRows) = nullptr;
    At<void*>(self, graphs::kRows + 4) = nullptr;
    At<void*>(self, graphs::kRows + 8) = nullptr;
    At<void*>(self, graphs::kHidden) = nullptr;
    char* const header = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{0x18});
    At<void*>(self, graphs::kHidden) = header;
    At<uint32_t>(self, graphs::kHiddenCount) = 0;
    At<uint8_t>(header, 0x0) = 0;
    At<void*>(header, 0x4) = nullptr;
    At<void*>(header, 0x8) = header;
    At<void*>(header, 0xC) = header;
    At<uint32_t>(self, 0x4C) = 0;
    return self;
}

// The game's 0x0076D070. `this` is the window procedure part (+4) of this
// class and of the news ticker, which share the thunk.
uint8_t __fastcall QueryInterface4(char* part, void*, uint32_t iid, void** out)
{
    return This<uint8_t>(addr::SC4UI_F7723F0, part - graphs::kWinProc, iid, out);
}

// The game's 0x0076D080. `this` is the window object. The destructor: stops
// (0x0076B750), then frees the map, the rows and the references.
void __fastcall Destruct(char* self, void*)
{
    At<uint32_t>(self, 0x0) = addr::SC4UI_GraphWindowVtable;
    At<uint32_t>(self, graphs::kWinProc) = addr::SC4UI_GraphWindowVtable4;
    This<uint8_t>(addr::SC4UI_F76B750, self);
    Clear(At<void*>(self, graphs::kWindow));

    char* const map = self + graphs::kHidden;
    if (At<uint32_t>(self, graphs::kHiddenCount) != 0) {
        This<void>(addr::SC4UI_F76B9B0, map, At<void*>(At<char*>(map, 0x0), 0x4));
        At<void*>(At<char*>(map, 0x0), 0x8) = At<void*>(map, 0x0);
        At<void*>(At<char*>(map, 0x0), 0x4) = nullptr;
        At<void*>(At<char*>(map, 0x0), 0xC) = At<void*>(map, 0x0);
        At<uint32_t>(map, 0x4) = 0;
    }
    if (void* const header = At<void*>(map, 0x0))
        Free(header);

    RowVector* const rows = reinterpret_cast<RowVector*>(self + graphs::kRows);
    DestroyRange(rows->begin, rows->end);
    if (rows->begin)
        Free(rows->begin);

    if (void* const graph = At<void*>(self, graphs::kGraph))
        Release(graph);
    if (void* const big = At<void*>(self, graphs::kBig))
        Release(big);
    if (void* const small = At<void*>(self, graphs::kSmall))
        Release(small);
    if (void* const window = At<void*>(self, graphs::kWindow))
        Release(window);
}

// The game's 0x0076D290. `this` is the window object (slot 4). The deleting
// destructor.
char* __fastcall Delete(char* self, void*, uint32_t flags)
{
    Destruct(self, nullptr);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// A property's values: the property's variant (slot 5), its count (slot 4)
// and its array as 32-bit numbers (slot 103), bytes (slot 95) or floats
// (slot 111). The original asks for the variant again before every read.
uint32_t Count(void* property)
{
    return Virtual<uint32_t>(Virtual<void*>(property, 0x14), 0x10);
}

bool Has(void* property, uint32_t index)
{
    return property && index < Count(property);
}

uint32_t NumberAt(void* property, uint32_t index)
{
    return Virtual<const uint32_t*>(Virtual<void*>(property, 0x14), 0x19C)[index];
}

uint8_t ByteAt(void* property, uint32_t index)
{
    return Virtual<const uint8_t*>(Virtual<void*>(property, 0x14), 0x17C)[index];
}

uint32_t FloatBitsAt(void* property, uint32_t index)
{
    return Virtual<const uint32_t*>(Virtual<void*>(property, 0x14), 0x1BC)[index];
}

uint32_t Colour(uint32_t red, uint32_t green, uint32_t blue)
{
    void* const system = Cdecl<void*>(addr::SC4_Singleton913C1A);
    return Virtual<uint32_t>(system, 0x88, red, green, blue);
}

// The graph control's second part, which takes the settings.
void* Part(void* graph)
{
    return static_cast<char*>(graph) + 0xD8;
}

// The row's name from the text `text` of group 0x6A231EAA.
void SetRowName(Row* row, uint32_t text)
{
    struct {
        alignas(4) char bytes[0x20];
    } localized;
    char* const made =
        This<char*>(addr::SC4LocalizedString_SC4LocalizedString, &localized, 0x6A231EAAu, text, uint32_t{1});
    AssignBody(&row->name.body, made ? reinterpret_cast<const StringBody*>(made + 4) : nullptr);
    At<std::uintptr_t>(&localized, 0x0) = addr::SC4String_Vtable;
    if (void* const begin = At<void*>(&localized, 0x4))
        Free(begin);
}

void ClearRowName(Row* row)
{
    This<void*>(addr::SC4String_EraseBody, &row->name.body, uint32_t{0}, 0xFFFFFFFFu);
}

// The three limits every description may have, then the figures fed
// (0x0076BE70).
void ReadLimitsAndFeed(char* self, void* properties)
{
    At<uint32_t>(self, graphs::kHigh) = 0xFF7FFFFF; // the lowest float
    At<uint32_t>(self, graphs::kLow) = 0x7F7FFFFF;  // the highest float
    At<uint32_t>(self, graphs::kThird) = 0;
    Cdecl<uint8_t>(addr::SC4_GetPropertyFloat, properties, 0xEA4AEDC6u, &At<float>(self, graphs::kLow));
    Cdecl<uint8_t>(addr::SC4_GetPropertyFloat, properties, 0x6A4AEDEDu, &At<float>(self, graphs::kHigh));
    Cdecl<uint8_t>(addr::SC4_GetPropertyFloat, properties, 0x6A4AEDEFu, &At<float>(self, graphs::kThird));
    This<void>(addr::SC4UI_F76BE70, self);
}

RowVector* Rows(char* self)
{
    return reinterpret_cast<RowVector*>(self + graphs::kRows);
}

// The bars' half of 0x0076D3D0 (from 0x0076D818).
void BuildBars(char* self, void* graph, void* properties, const int32_t* area)
{
    int32_t plot[4] = {0x2D, 0x14, area[2] - area[0] - 2, area[3] - area[1] - 0x14};
    Virtual<void>(Part(graph), 0x30, &plot[0]);

    void* bars = nullptr;
    if (!Virtual<uint8_t>(graph, 0x0, 0x22CF2351u, &bars))
        return;
    Virtual<void>(bars, 0x14, uint32_t{1});

    void* const ids = Virtual<void*>(properties, 0x24, 0x6A4AEE40u);
    void* const names = Virtual<void*>(properties, 0x24, 0x6A4AEEDCu);
    void* const scales = Virtual<void*>(properties, 0x24, 0x6A4AEEE2u);
    void* const groups = Virtual<void*>(properties, 0x24, 0x6A4AEEE3u);
    void* const fills = Virtual<void*>(properties, 0x24, 0x6A4AEEE4u);
    void* const edges = Virtual<void*>(properties, 0x24, 0x6A4AEEE5u);
    void* const types = Virtual<void*>(properties, 0x24, 0x6A4AEE68u);

    At<uint32_t>(self, graphs::kSeries) = ids ? Count(ids) : 0;
    RowsResizeDefault(Rows(self), nullptr, At<uint32_t>(self, graphs::kSeries));
    At<uint32_t>(self, graphs::kBars) = 1;
    for (uint32_t i = 0; i < At<uint32_t>(self, graphs::kSeries); ++i) {
        Row* const row = Rows(self)->begin + i;
        row->id = Has(ids, i) ? NumberAt(ids, i) : 0;
        if (Has(names, i)) {
            SetRowName(row, NumberAt(names, i));
            Virtual<void>(bars, 0x30, &row->name, i);
        } else {
            ClearRowName(row);
        }
        row->scale = Has(scales, i) ? FloatBitsAt(scales, i) : 0x3F800000u;
        if (Has(groups, i)) {
            const uint32_t group = NumberAt(groups, i);
            row->group = group;
            if (At<uint32_t>(self, graphs::kBars) < group + 1)
                At<uint32_t>(self, graphs::kBars) = group + 1;
        } else {
            row->group = 0;
        }
        row->type = Has(types, i) ? NumberAt(types, i) : 0;
    }

    if (At<uint32_t>(self, graphs::kBars) >= 0x10)
        At<uint32_t>(self, graphs::kBars) = 0x10;
    Virtual<void>(bars, 0x14, At<uint32_t>(self, graphs::kBars));
    for (uint32_t j = 0; j < At<uint32_t>(self, graphs::kBars); ++j) {
        const uint32_t at = j * 3;
        uint32_t red = 0xAA;
        uint32_t green = 0xBB;
        uint32_t blue = 0xC3;
        if (Has(fills, at)) {
            red = ByteAt(fills, at);
            green = ByteAt(fills, at + 1);
            blue = ByteAt(fills, at + 2);
        }
        Virtual<void>(bars, 0x3C, Colour(red, green, blue), j);

        red = 0x67;
        green = 0x71;
        blue = 0x76;
        if (Has(edges, at)) {
            red = ByteAt(edges, at);
            green = ByteAt(edges, at + 1);
            blue = ByteAt(edges, at + 2);
        }
        Virtual<void>(bars, 0x40, Colour(red, green, blue), j);
    }

    ReadLimitsAndFeed(self, properties);
    Release(bars);
}

// The lines' half of 0x0076D3D0 (from 0x0076DD2F): the rows, and for each a
// legend entry on the graph: a colour box, the name, and with more than two
// series a check box (ID 0x04000000 + the row's index, an image of
// 0x856DDBAC / 0x46A006B0 / 0x14416245) that reports to the window.
void BuildLines(char* self, void* graph, void* properties, const int32_t* area, uint32_t light)
{
    int32_t plot[4] = {0x2D, 0x14, area[2] - area[0] - 0x6E, area[3] - area[1] - 0x14};
    Virtual<void>(Part(graph), 0x30, &plot[0]);

    void* lines = nullptr;
    void* const font = Virtual<void*>(Cdecl<void*>(addr::SC4_Singleton913C72), 0x14, 0xE9C86B5Eu);
    if (!Virtual<uint8_t>(graph, 0x0, 0x12CF2351u, &lines))
        return;
    Virtual<void>(Part(graph), 0x5C, uint32_t{0}, uint32_t{1});
    Virtual<void>(Part(graph), 0x80, uint32_t{0}, uint32_t{0});

    void* const ids = Virtual<void*>(properties, 0x24, 0x6A4AEE40u);
    void* const seconds = Virtual<void*>(properties, 0x24, 0x6A4AEE5Fu);
    void* const types = Virtual<void*>(properties, 0x24, 0x6A4AEE68u);
    void* const names = Virtual<void*>(properties, 0x24, 0x6A4AEEDCu);
    void* const colours = Virtual<void*>(properties, 0x24, 0x6A4AEEDFu);
    void* const scales = Virtual<void*>(properties, 0x24, 0x6A4AEEE2u);

    At<uint32_t>(self, graphs::kSeries) = ids ? Count(ids) : 0;
    RowsResizeDefault(Rows(self), nullptr, At<uint32_t>(self, graphs::kSeries));
    int32_t y = 0x14;
    for (uint32_t i = 0; i < At<uint32_t>(self, graphs::kSeries); ++i) {
        const uint32_t at = i * 3;
        Row* const row = Rows(self)->begin + i;
        row->id = Has(ids, i) ? NumberAt(ids, i) : 0;
        row->second = Has(seconds, i) ? NumberAt(seconds, i) : 0;
        row->type = Has(types, i) ? NumberAt(types, i) : 0;
        if (Has(names, i))
            SetRowName(row, NumberAt(names, i));
        else
            ClearRowName(row);
        if (Has(colours, at)) {
            row->colour[0] = ByteAt(colours, at);
            row->colour[1] = ByteAt(colours, at + 1);
            row->colour[2] = ByteAt(colours, at + 2);
        } else {
            row->colour[0] = 0;
            row->colour[1] = 0;
            row->colour[2] = 0;
        }
        row->scale = Has(scales, i) ? FloatBitsAt(scales, i) : 0x3F800000u;

        // Shown unless the player hid this series before.
        uint32_t key = row->id;
        char* found = nullptr;
        This<void>(addr::SC4Uint32Map_Find, self + graphs::kHidden, &found, &key);
        row->shown = found != At<char*>(self, graphs::kHidden) ? At<uint8_t>(found, 0x14) : uint8_t{1};

        int32_t x = area[2] - area[0] - 0x6A;
        if (At<uint32_t>(self, graphs::kSeries) > 2) {
            ImageLoader image;
            This<void*>(addr::SC4ImageLoader_ConstructParts, &image, 0x856DDBACu, 0x46A006B0u, 0x14416245u,
                        uint32_t{1}, uint32_t{0});
            void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x1B4});
            void* const box = memory ? This<void*>(addr::SC4UI_CheckBoxConstruct, memory) : nullptr;
            void* boxWindow = Virtual<void*>(box, 0xC);
            const int32_t width = area[2] - area[0];
            Virtual<void>(boxWindow, 0xDC, width - 0x6C, y, width - 0x5C, y + 0x10);
            boxWindow = Virtual<void*>(box, 0xC);
            Virtual<void>(boxWindow, 0x100, i + 0x4000000u);
            boxWindow = Virtual<void*>(box, 0xC);
            Virtual<void>(boxWindow, 0x158, At<void*>(self, graphs::kWindow));
            Virtual<void>(box, 0x14, uint32_t{1}, uint32_t{row->shown});
            Virtual<void>(box, 0xB4, uint32_t{0}, 0x4A5C31D7u);
            Virtual<void>(box, 0x30, uint32_t{4}, image.image, uint32_t{0});
            boxWindow = Virtual<void*>(box, 0xC);
            Virtual<void>(graph, 0x38, boxWindow);
            x = area[2] - area[0] - 0x5A;
            This<void>(addr::SC4ImageLoader_Destruct, &image);
        }

        char* const label = Virtual<char*>(Part(graph), 0xC4);
        char* const mark = Virtual<char*>(Part(graph), 0xCC);
        At<int32_t>(mark, 0x8) = x;
        At<int32_t>(mark, 0x10) = x + 0xA;
        At<int32_t>(mark, 0xC) = y + 3;
        At<int32_t>(mark, 0x14) = y + 9;
        At<uint8_t>(mark, 0x18) = 1;
        At<uint8_t>(mark, 0x24) = 1;
        At<uint32_t>(mark, 0x1C) = 1;
        At<uint32_t>(mark, 0x20) = light;
        At<uint32_t>(mark, 0x28) = Colour(row->colour[0], row->colour[1], row->colour[2]);

        // The label: a string at +8, its rectangle at +0x1C.
        AssignBody(reinterpret_cast<StringBody*>(label + 0xC), &row->name.body);
        At<int32_t>(label, 0x1C) = At<int32_t>(mark, 0x10) + 4;
        At<int32_t>(label, 0x20) = y;
        At<int32_t>(label, 0x24) = area[2] - area[0] - 4;
        At<int32_t>(label, 0x28) = y + 0xA;
        const int32_t length = At<int32_t>(label, 0x10) - At<int32_t>(label, 0xC);
        const char* const chars = Virtual<const char*>(label + 0x8, 0x14);
        Virtual<void>(font, 0xB8, chars, length, static_cast<void*>(label + 0x1C), uint32_t{1}, uint32_t{1});
        At<uint32_t>(label, 0x2C) = 0;
        At<uint8_t>(label, 0x30) = 0;
        At<uint8_t>(label, 0x38) = 0;
        Virtual<void>(label, 0xC, font, 0x80000000u);
        At<uint32_t>(label, 0x44) = Colour(0x3F, 0x49, 0x67);
        y = y + (At<int32_t>(label, 0x28) - At<int32_t>(label, 0x20)) + 4;
    }

    ReadLimitsAndFeed(self, properties);
    Release(lines);
}

// The game's 0x0076D3D0 (it ends at 0x0076E416; the list's 0x0076DEE0 is
// inside it). `this` is the window object. Builds the graph `number`: drops
// the control on show, loads the description, makes the control of its kind
// as a child of the big pane, sets fonts and colours, and fills the rows and
// the legend (bars: from 0x0076D818, lines: from 0x0076DD2F). Number 0 has no
// description, so it only drops the control.
void __fastcall Build(char* self, void*, uint32_t number)
{
    if (!At<void*>(self, graphs::kBig))
        return;
    void*& graph = At<void*>(self, graphs::kGraph);
    if (graph)
        Virtual<void>(At<void*>(self, graphs::kBig), 0x40, graph);
    Clear(graph);

    uint32_t key[3] = {0x6534284A, 0xCA4AD545, number};
    void* description = nullptr;
    void* const resources = Global<void*>(addr::SC4_ResourceManager);
    if (Virtual<uint8_t>(resources, 0xC, &key[0], 0xA52160F5u, &description, uint32_t{0}, uint32_t{0})) {
        uint32_t kind = 0;
        GameString title;
        MakeEmpty(title);
        void* const properties = Virtual<void*>(description, 0x1C);
        Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, properties, 0xAA4C0D1Bu, &kind);
        if (!Cdecl<uint8_t>(addr::SC4_GetPropertyKeyString, &title, properties, 0x8A416A99u, uint32_t{0}))
            Cdecl<uint8_t>(addr::SC4_GetPropertyString, properties, uint32_t{0x20}, &title);

        if (At<uint32_t>(self, graphs::kKind) != kind) {
            RowsClear(Rows(self), nullptr);
            if (graph)
                Virtual<void>(At<void*>(self, graphs::kBig), 0x40, graph);
            Clear(graph);
        }
        At<uint32_t>(self, graphs::kKind) = kind;

        void* made = nullptr;
        bool known = true;
        switch (kind) {
        case 1:
            if (void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x260}))
                made = This<void*>(addr::SC4UI_F76B5F0, memory);
            break;
        case 2:
            if (void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x268}))
                made = This<void*>(addr::SC4UI_BarGraphConstruct, memory);
            break;
        case 3:
            if (void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x28C}))
                made = This<void*>(addr::SC4UI_ThirdGraphConstruct, memory);
            break;
        default:
            known = false;
            break;
        }

        if (!known) {
            DestroyString(title);
        } else {
            This<void*>(addr::SC4RefPtr_Assign, &graph, made);

            // The three controls only the line graph has.
            for (std::size_t n = 0; n < 3; ++n) {
                const uint32_t id = Global<uint32_t>(addr::SC4UI_GraphWindowLineControls + 4 * n);
                void* const child = Virtual<void*>(At<void*>(self, graphs::kBig), 0x8C, id);
                if (child) {
                    if (At<uint32_t>(self, graphs::kKind) == 1)
                        Virtual<void>(child, 0x114);
                    else
                        Virtual<void>(child, 0x118);
                    Virtual<void>(child, 0x170);
                }
            }

            // The place holder's rectangle. The original leaves it as its
            // stack was when the child is missing; here it is zero then.
            int32_t area[4] = {};
            void* place = nullptr;
            if (Virtual<uint8_t>(At<void*>(self, graphs::kBig), 0x90, 0x8A4B0AA0u, 0x22BA0121u, &place)) {
                Virtual<void>(place, 0xBC, &area[0]);
                Release(place);
            }
            This<void>(addr::SC4Win_SetAreaRect, graph, &area[0]);
            Virtual<void>(At<void*>(self, graphs::kBig), 0x38, graph);

            const uint32_t dark = Colour(0x3F, 0x49, 0x67);
            void* const titleFont = Virtual<void*>(Cdecl<void*>(addr::SC4_Singleton913C72), 0x14, 0xE9C86B5Fu);
            void* const axisFont = Virtual<void*>(Cdecl<void*>(addr::SC4_Singleton913C72), 0x14, 0xE9C86B6Eu);
            Virtual<void>(Part(graph), 0x9C, titleFont, dark);
            Virtual<void>(Part(graph), 0x54, uint32_t{0}, axisFont, dark);
            Virtual<void>(Part(graph), 0x54, uint32_t{1}, axisFont, dark);
            Virtual<void>(Part(graph), 0x74, uint32_t{0}, axisFont, dark);
            Virtual<void>(Part(graph), 0x74, uint32_t{1}, axisFont, dark);
            const uint32_t light = Colour(0xAA, 0xBB, 0xC3);
            Virtual<void>(Part(graph), 0x10, uint32_t{0});
            {
                const uint32_t colour = Colour(0xDA, 0xE0, 0xE5);
                Virtual<void>(Part(graph), 0x14, colour);
            }
            Virtual<void>(Part(graph), 0x20, uint32_t{1}, uint32_t{1});
            {
                const uint32_t colour = Colour(0xAE, 0xBF, 0xC0);
                Virtual<void>(Part(graph), 0x24, colour);
            }
            {
                const uint32_t colour = Colour(0xEF, 0xF3, 0xF7);
                Virtual<void>(Part(graph), 0x28, colour);
            }
            Virtual<void>(Part(graph), 0x48, light);
            Virtual<void>(Part(graph), 0x7C, light);
            Virtual<void>(Part(graph), 0x78, uint32_t{0}, uint32_t{1});
            Virtual<void>(Part(graph), 0x94, &title, uint32_t{0});

            if (At<uint32_t>(self, graphs::kKind) == 1)
                BuildLines(self, graph, properties, &area[0], light);
            else if (At<uint32_t>(self, graphs::kKind) == 2)
                BuildBars(self, graph, properties, &area[0]);

            Virtual<void>(graph, 0x114);
            DestroyString(title);
        }
    }
    if (description)
        Release(description);
}

// The game's 0x0076E420. `this` is the window object. Shows the window with
// the graph chosen built again, its button pressed (0x0076B8B0), and the big
// or the small pane. What the original leaves in EAX (the last call's
// result) is not kept.
void __fastcall Show(char* self, void*)
{
    Build(self, nullptr, At<uint32_t>(self, graphs::kNumber));
    This<void>(addr::SC4UI_F76B8B0, self, At<uint32_t>(self, graphs::kNumber));
    void* const window = At<void*>(self, graphs::kWindow);
    At<uint8_t>(self, graphs::kShown) = 1;
    Virtual<void>(window, 0x114);
    if (At<uint8_t>(self, graphs::kUseBig)) {
        if (void* const big = At<void*>(self, graphs::kBig))
            Virtual<void>(big, 0x114);
    } else {
        if (void* const small = At<void*>(self, graphs::kSmall))
            Virtual<void>(small, 0x114);
    }
}

// The game's 0x0076E470. `this` is the window object. Hides the window and
// both panes and drops the graph control.
void __fastcall Hide(char* self, void*)
{
    if (At<void*>(self, graphs::kWindow)) {
        Build(self, nullptr, 0);
        Virtual<void>(At<void*>(self, graphs::kWindow), 0x118);
        At<uint8_t>(self, graphs::kShown) = 0;
    }
    if (void* const big = At<void*>(self, graphs::kBig))
        Virtual<void>(big, 0x118);
    if (void* const small = At<void*>(self, graphs::kSmall))
        Virtual<void>(small, 0x118);
}

// The game's 0x0076E4B0. `this` is the window object. Chooses the graph
// `number` and shows the window with the big pane. The panes are not checked
// for null, as in the original.
void __fastcall ShowGraph(char* self, void*, uint32_t number)
{
    if (At<uint32_t>(self, graphs::kNumber) != number) {
        This<void>(addr::SC4UI_F76B8B0, self, number);
        Build(self, nullptr, number);
        At<uint32_t>(self, graphs::kNumber) = number;
    }
    Show(self, nullptr);
    Virtual<void>(At<void*>(self, graphs::kSmall), 0x118);
    Virtual<void>(At<void*>(self, graphs::kBig), 0x114);
    At<uint8_t>(self, graphs::kUseBig) = 1;
}

bool InstallDialogs6(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[20] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F76CC80, reinterpret_cast<void*>(&DoMessage)},
        {addr::SC4UI_F76CCC0, reinterpret_cast<void*>(&RowsCopyBackward)},
        {addr::SC4UI_F76CD70, reinterpret_cast<void*>(&RowsFill)},
        {addr::SC4UI_F76CE00, reinterpret_cast<void*>(&RowsCopy)},
        {addr::SC4UI_F76CEB0, reinterpret_cast<void*>(&RowsInsertGrow)},
        {addr::SC4UI_F76CFE0, reinterpret_cast<void*>(&Construct)},
        {addr::SC4UI_F76D070, reinterpret_cast<void*>(&QueryInterface4)},
        {addr::SC4UI_F76D080, reinterpret_cast<void*>(&Destruct)},
        {addr::SC4UI_F76D150, reinterpret_cast<void*>(&RowsInsert)},
        {addr::SC4UI_F76D290, reinterpret_cast<void*>(&Delete)},
        {addr::SC4UI_F76D2B0, reinterpret_cast<void*>(&RowsClear)},
        {addr::SC4UI_F76D2F0, reinterpret_cast<void*>(&RowsResize)},
        {addr::SC4UI_F76D370, reinterpret_cast<void*>(&RowsResizeDefault)},
        {addr::SC4UI_F76D3D0, reinterpret_cast<void*>(&Build)},
        {addr::SC4UI_F76E420, reinterpret_cast<void*>(&Show)},
        {addr::SC4UI_F76E470, reinterpret_cast<void*>(&Hide)},
        {addr::SC4UI_F76E4B0, reinterpret_cast<void*>(&ShowGraph)},
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

const bool g_dialogs6Registered = registry::Register({
    "ui.dialogs.part6",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F76D3D0,
    "Our graphs window: construction, showing and hiding, and the builder of a graph's rows and legend (docs/ui.md)",
    false,
    InstallDialogs6,
});

} // namespace
} // namespace opensc4::ui
