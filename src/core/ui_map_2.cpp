// The second part of the map window's range (0x007A0280 to 0x007A2150). It
// holds pieces of two classes:
//
//   - the map window of the city view (0x9E8 bytes, GZCOM class 0x28C5A41F,
//     made by the factory 0x00466080; vtables 0x00AB8150 at +0, 0x00AB8140 at
//     +4 (the message target), 0x00AB7EE0 at +8 (the framework window it is
//     built on) and 0x00AB7E60 at +0xE0): its constructor and destructor, the
//     thunks of its three secondary vtables, the legend under the picture,
//     the three rows of mode buttons, the messages it listens for, and the
//     marks it keeps on buildings in the city while a data view is up (the
//     buildings highlighted, an effect at a point), with the merge of the
//     newly wanted marks into the ones shown;
//   - the occupant filter the map registers with GZCOM (0x30 bytes, vtables
//     0x00AB7E44 and 0x00AB7E2C, factory 0x007A0420): the factory, the
//     destructor and the push onto its second vector.
//
// Replacement, off by default:
//
//   ui.map.part2: everything above.
//
// Left the game's (docs/ui.md): the picture's own code, which is another
// thread's (0x007A1120, 0x007A1310 and their twin and helper 0x007A1020 and
// 0x007A2080), and the two insert functions of the marks' maps (0x007A0AE0,
// 0x007A0B30), which are container code. Windows, buttons, texts, effects
// and the simulators are the game's, called through their vtables.
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

namespace map {
constexpr std::size_t kTarget = 0x04;      // the message target part
constexpr std::size_t kWindow = 0x08;      // the framework window part
constexpr std::size_t kButtons = 0x50;     // the window whose children the buttons and legend rows are
constexpr std::size_t kLast = 0xE0;        // the part the factory hands out
constexpr std::size_t kFlag130 = 0x130;
constexpr std::size_t kField134 = 0x134;
constexpr std::size_t kMessages = 0x138;   // the message server
constexpr std::size_t kField13C = 0x13C;
constexpr std::size_t kFlag140 = 0x140;
constexpr std::size_t kFlag141 = 0x141;
constexpr std::size_t kFlag142 = 0x142;
constexpr std::size_t kMode = 0x144;       // which data the picture shows; decides the messages listened for
constexpr std::size_t kField148 = 0x148;
constexpr std::size_t kShown = 0x968;      // map of the marks in the city: header, count
constexpr std::size_t kWanted = 0x974;     // the same, being collected; empty between updates
constexpr std::size_t kMarkKind = 0x980;   // which buildings get marks (1 to 9)
constexpr std::size_t kEffectName = 0x984; // string: the effect of the mark at a point (kind 5)
constexpr std::size_t kThird = 0x9A0;      // the button down in the row 0x6001 (0x63 at first)
constexpr std::size_t kFourth = 0x9A4;     // the button down in the row 0x8003 (0x4C at first)
constexpr std::size_t kSecond = 0x9A8;     // the button down in the row 0x5001
constexpr std::size_t kFlag9AC = 0x9AC;
constexpr std::size_t kFlag9B5 = 0x9B5;
constexpr std::size_t kKeyShown = 0x9B6;   // legend 3 is up: the child 0x8A2871C4 may show
constexpr std::size_t kMapper = 0x9B8;     // made by 0x007A2080 for the picture
constexpr std::size_t kMessageIds = 0x9BC; // vector of the message IDs registered for
constexpr std::size_t kList9C8 = 0x9C8;    // a list (sentinel, count)
constexpr std::size_t kHeld9D0 = 0x9D0;    // a reference
constexpr std::size_t kLegend = 0x9D4;     // stored by slot 3 (0x007A6270)
constexpr std::size_t kFlag9D8 = 0x9D8;
constexpr std::size_t kFlag9D9 = 0x9D9;
constexpr std::size_t kMadeAt = 0x9DC;     // the time in milliseconds at construction
constexpr std::size_t kFlag9E0 = 0x9E0;
} // namespace map

namespace filter {
constexpr std::size_t kFirst = 0x0C;   // vector: begin
constexpr std::size_t kSecond = 0x18;  // vector of uint32: begin, end, capacity
constexpr std::size_t kHeld24 = 0x24;
constexpr std::size_t kHeld28 = 0x28;
} // namespace filter

constexpr uint32_t kTextIid = 0x212CDC1F;
constexpr uint32_t kSwatchIid = 0xC2AFA76F;
constexpr uint32_t kExemplarIid = 0xA52160F5;
constexpr uint32_t kTextGroup = 0x6A231EAA;

// The legend's children: nine rows of a text (+0) and a colour swatch (+0x10).
constexpr uint32_t kLegendRow = 0x8A909E00;
constexpr uint32_t kLegendSwatch = 0x8A909E10;
constexpr uint32_t kLegendTitle = 0x8A909D6A;
constexpr uint32_t kLegendKey = 0x8A2871C4;
constexpr uint32_t kNoLegendTitle = 0x2A5E854F;

constexpr uint32_t kLandfillOccupant = 0x6A11BD2B;
constexpr uint8_t kLandfillZone = 0xE;

struct Rect {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
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

// A text loaded by resource key (the game's object is 0x20 bytes).
struct LocalizedString {
    GameString text;
    uint32_t rest[5];
};

struct Uint32Vector {
    uint32_t* begin;
    uint32_t* end;
    uint32_t* capacity;
};

struct ResourceKey {
    uint32_t type;
    uint32_t group;
    uint32_t instance;
};

// A mark in the city: the value of a node of the two maps (0x18 bytes). The
// key is the occupant's address, compared as a signed number; the mark at a
// point has none.
struct Mark {
    void* occupant;
    void* effect;
    uint32_t size;      // float bits: what the simulator gave for the building, 100 for the point, else 0
    float position[3];
};
static_assert(sizeof(Mark) == 0x18);

struct MarkNode {
    uint8_t colour;
    uint8_t pad[3];
    MarkNode* parent;
    MarkNode* left;
    MarkNode* right;
    Mark value;
};
static_assert(sizeof(MarkNode) == 0x28);

struct MarkMap {
    MarkNode* header;
    uint32_t count;
};

struct ListNode {
    ListNode* next;
    ListNode* previous;
    void* value;
};

// What an effect is told about its place (0x38 bytes).
struct Placement {
    uint8_t a;
    uint8_t b;
    uint8_t pad[2];
    float matrix[9];
    float position[3];
    float scale;
};
static_assert(sizeof(Placement) == 0x38);

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

void DestroyString(GameString& text)
{
    char* const begin = text.body.begin;
    text.vtable = addr::SC4String_Vtable;
    if (begin)
        Free(begin);
}

void ShowChild(void* window, uint32_t id, uint32_t show)
{
    Cdecl<uint8_t>(addr::SC4Win_ShowChild, window, id, show, uint32_t{0});
}

// The window the map's own window hangs in (slot 11 of the window part).
void* Parent(char* self)
{
    return Virtual<void*>(self + map::kWindow, 0x2C);
}

// The game's 0x0079E540 takes the first ID in ECX, the count in EAX, the
// window in EDI and the chosen ID on the stack, which the caller removes:
// of a row of buttons the chosen one gets slot 6, the others slot 7.
void* g_pressOneOf = reinterpret_cast<void*>(addr::SC4UI_F79E540);

__declspec(naked) void __cdecl PressOneOf(void* /*window*/, uint32_t /*first*/, uint32_t /*count*/,
                                          uint32_t /*chosen*/)
{
    __asm push edi
    __asm mov edi, [esp + 8]
    __asm mov ecx, [esp + 12]
    __asm mov eax, [esp + 16]
    __asm push dword ptr [esp + 20]
    __asm call dword ptr [g_pressOneOf]
    __asm add esp, 4
    __asm pop edi
    __asm ret
}

// The next node of a map in key order, as the game inlines it.
MarkNode* Next(MarkNode* node)
{
    if (node->right) {
        node = node->right;
        while (node->left)
            node = node->left;
        return node;
    }
    MarkNode* parent = node->parent;
    while (node == parent->right) {
        node = parent;
        parent = parent->parent;
    }
    if (node->right != parent)
        node = parent;
    return node;
}

// Empties a map of marks: the nodes dropped by 0x0079FAD0, the header reset.
void ClearMarks(MarkMap* marks)
{
    if (marks->count != 0) {
        This<void>(addr::SC4UI_F79FAD0, marks, marks->header->parent);
        marks->header->left = marks->header;
        marks->header->parent = nullptr;
        marks->header->right = marks->header;
        marks->count = 0;
    }
}

// Takes a node out of the map of the marks shown and frees it.
void EraseShown(char* self, MarkNode* node)
{
    MarkMap* const shown = &At<MarkMap>(self, map::kShown);
    MarkNode* const header = shown->header;
    MarkNode* const dead =
        Cdecl<MarkNode*>(addr::SC4Tree_RebalanceForErase, node, &header->parent, &header->left, &header->right);
    if (dead->value.effect)
        Release(dead->value.effect);
    if (dead->value.occupant)
        Release(dead->value.occupant);
    if (dead)
        Free(dead);
    --shown->count;
}

// --- The map window: construction -----------------------------------------------------

// The game's 0x007A0280, no arguments: the destructor. Ends in the framework
// window's destructor on the part at +8.
void __fastcall MapDestroy(char* self, void*)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4UI_MapWindowVtable;
    At<std::uintptr_t>(self, map::kTarget) = addr::SC4UI_MapWindowVtable4;
    At<std::uintptr_t>(self, map::kWindow) = addr::SC4UI_MapWindowVtable8;
    At<std::uintptr_t>(self, map::kLast) = addr::SC4UI_MapWindowVtableE0;
    if (void* const held = At<void*>(self, map::kHeld9D0))
        Release(held);
    This<void>(addr::SC4PairList_Destroy, self + map::kList9C8);
    if (void* const ids = At<void*>(self, map::kMessageIds))
        Free(ids);
    DestroyString(At<GameString>(self, map::kEffectName));
    for (const std::size_t field : {map::kWanted, map::kShown}) {
        MarkMap* const marks = &At<MarkMap>(self, field);
        ClearMarks(marks);
        if (marks->header)
            Free(marks->header);
    }
    This<void>(addr::cGZWin_Destroy, self + map::kWindow);
}

// The game's 0x007A0D50, no arguments: the constructor.
void* __fastcall MapConstruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4UI_MapWindowBaseVtable;
    At<std::uintptr_t>(self, map::kTarget) = addr::cSC4TrafficSimulator_VtableBase4;
    This<void*>(addr::cGZWin_cGZWin, self + map::kWindow);
    At<std::uintptr_t>(self, 0x0) = addr::SC4UI_MapWindowVtable;
    At<std::uintptr_t>(self, map::kTarget) = addr::SC4UI_MapWindowVtable4;
    At<std::uintptr_t>(self, map::kWindow) = addr::SC4UI_MapWindowVtable8;
    At<std::uintptr_t>(self, map::kLast) = addr::SC4UI_MapWindowVtableE0;
    At<uint8_t>(self, map::kFlag130) = 0;
    At<uint32_t>(self, map::kField134) = 0;
    At<void*>(self, map::kMessages) = nullptr;
    At<uint32_t>(self, map::kField13C) = 0;
    At<uint8_t>(self, map::kFlag140) = 1;
    At<uint8_t>(self, map::kFlag141) = 0;
    At<uint8_t>(self, map::kFlag142) = 0;
    At<uint32_t>(self, map::kMode) = 0;
    At<uint32_t>(self, map::kField148) = 3;
    for (const std::size_t field : {map::kShown, map::kWanted}) {
        MarkMap* const marks = &At<MarkMap>(self, field);
        marks->header = nullptr;
        marks->header = Cdecl<MarkNode*>(addr::SC4_stl_allocate, uint32_t{0x28});
        marks->count = 0;
        marks->header->colour = 0;
        marks->header->parent = nullptr;
        marks->header->left = marks->header;
        marks->header->right = marks->header;
    }
    GameString* const name = &At<GameString>(self, map::kEffectName);
    name->body.begin = nullptr;
    name->body.end = nullptr;
    name->body.capacity = nullptr;
    char* const text = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    name->body.begin = text;
    name->body.end = text;
    name->body.capacity = text + 8;
    *text = 0;
    name->vtable = addr::SC4String_Vtable;
    name->zero = 0;
    At<uint32_t>(self, map::kThird) = 0x63;
    At<uint32_t>(self, map::kFourth) = 0x4C;
    At<uint32_t>(self, map::kSecond) = 0;
    At<uint8_t>(self, map::kFlag9AC) = 0;
    At<uint8_t>(self, map::kFlag9B5) = 0;
    At<uint8_t>(self, map::kKeyShown) = 0;
    At<void*>(self, map::kMapper) = nullptr;
    Uint32Vector* const ids = &At<Uint32Vector>(self, map::kMessageIds);
    ids->begin = nullptr;
    ids->end = nullptr;
    ids->capacity = nullptr;
    At<void*>(self, map::kList9C8) = nullptr;
    void** const sentinel = Cdecl<void**>(addr::SC4_stl_allocate, uint32_t{0xC});
    sentinel[0] = sentinel;
    sentinel[1] = sentinel;
    At<void*>(self, map::kList9C8) = sentinel;
    At<uint32_t>(self, map::kList9C8 + 4) = 0;
    At<void*>(self, map::kHeld9D0) = nullptr;
    At<uint32_t>(self, map::kLegend) = 0;
    At<uint8_t>(self, map::kFlag9D8) = 0;
    At<uint8_t>(self, map::kFlag9D9) = 0;
    At<uint32_t>(self, map::kMadeAt) = Cdecl<uint32_t>(addr::RZ_TimeMilliseconds);
    At<uint8_t>(self, map::kFlag9E0) = 0;
    return self;
}

// The game's 0x007A0F10 (ret 4): the deleting destructor.
void* __fastcall MapDelete(char* self, void*, uint32_t flags)
{
    MapDestroy(self, nullptr);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// --- The map window: the thunks of its secondary vtables ------------------------------

// The game's 0x007A0380, 0x007A03B0 and 0x007A03F0 (ret 8): QueryInterface
// on the parts at +4, +8 and +0xE0.
uint8_t __fastcall QueryInterface4(char* self, void*, uint32_t iid, void** out)
{
    return This<uint8_t>(addr::SC4UI_MapWindowQueryInterface, self - map::kTarget, iid, out);
}

uint8_t __fastcall QueryInterface8(char* self, void*, uint32_t iid, void** out)
{
    return This<uint8_t>(addr::SC4UI_MapWindowQueryInterface, self - map::kWindow, iid, out);
}

uint8_t __fastcall QueryInterfaceE0(char* self, void*, uint32_t iid, void** out)
{
    return This<uint8_t>(addr::SC4UI_MapWindowQueryInterface, self - map::kLast, iid, out);
}

// The game's 0x007A0390, 0x007A03C0 and 0x007A0400: AddRef on the same parts.
uint32_t __fastcall AddRef4(char* self, void*)
{
    return This<uint32_t>(addr::SC4UI_MapWindowAddRef, self - map::kTarget);
}

uint32_t __fastcall AddRef8(char* self, void*)
{
    return This<uint32_t>(addr::SC4UI_MapWindowAddRef, self - map::kWindow);
}

uint32_t __fastcall AddRefE0(char* self, void*)
{
    return This<uint32_t>(addr::SC4UI_MapWindowAddRef, self - map::kLast);
}

// The game's 0x007A03A0, 0x007A03D0 and 0x007A0410: Release on the same parts.
uint32_t __fastcall Release4(char* self, void*)
{
    return This<uint32_t>(addr::SC4UI_MapWindowRelease, self - map::kTarget);
}

uint32_t __fastcall Release8(char* self, void*)
{
    return This<uint32_t>(addr::SC4UI_MapWindowRelease, self - map::kWindow);
}

uint32_t __fastcall ReleaseE0(char* self, void*)
{
    return This<uint32_t>(addr::SC4UI_MapWindowRelease, self - map::kLast);
}

// The game's 0x007A03E0 (ret 4): the window part's deleting destructor.
void* __fastcall MapDelete8(char* self, void*, uint32_t flags)
{
    return MapDelete(self - map::kWindow, nullptr, flags);
}

// --- The map's occupant filter --------------------------------------------------------

// The game's 0x007A0420 (cdecl, no arguments): the GZCOM factory of the
// filter. Null when there is no memory.
void* __cdecl FilterMake()
{
    char* const made = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0x30});
    if (!made)
        return nullptr;
    This<void*>(addr::SC4_OccupantFilterBase, made);
    At<std::uintptr_t>(made, 0x0) = addr::SC4UI_MapFilter_Vtable;
    At<std::uintptr_t>(made, 0x4) = addr::SC4UI_MapFilter_SerialVtable;
    for (std::size_t field = 0xC; field <= 0x28; field += 4)
        At<uint32_t>(made, field) = 0;
    At<uint8_t>(made, 0x2C) = 0;
    At<uint8_t>(made, 0x2D) = 0;
    At<uint8_t>(made, 0x2E) = 1;
    return made;
}

// The game's 0x007A04A0, no arguments: the filter's destructor. Ends in the
// filter base's (0x005E95C0).
void __fastcall FilterDestroy(char* self, void*)
{
    if (void* const held = At<void*>(self, filter::kHeld28))
        Release(held);
    if (void* const held = At<void*>(self, filter::kHeld24))
        Release(held);
    if (void* const begin = At<void*>(self, filter::kSecond))
        Free(begin);
    if (void* const begin = At<void*>(self, filter::kFirst))
        Free(begin);
    This<void>(addr::SC4OccupantFilterBase_Destroy, self);
}

// The game's 0x007A0480 (ret 4), slot 6 of the filter: the deleting destructor.
void* __fastcall FilterDelete(char* self, void*, uint32_t flags)
{
    FilterDestroy(self, nullptr);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// The game's 0x007A12D0 (ret 4): a value pushed onto the filter's second vector.
void __fastcall FilterAdd(char* self, void*, uint32_t value)
{
    Uint32Vector* const values = &At<Uint32Vector>(self, filter::kSecond);
    uint32_t* const end = values->end;
    if (end != values->capacity) {
        if (end)
            *end = value;
        ++values->end;
        return;
    }
    // The original passes the address of its argument twice: as the value
    // and as the tag nobody reads.
    This<void>(addr::SC4Uint32Vector_InsertN2, values, end, &value, &value, uint32_t{1}, uint32_t{1});
}

// --- The map window: the legend and the mode buttons ----------------------------------

// The text of one of the legend's windows, from the text group.
void SetLegendText(void* textWindow, uint32_t instance)
{
    LocalizedString text;
    This<void*>(addr::SC4LocalizedString_SC4LocalizedString, &text, kTextGroup, instance, uint32_t{1});
    Virtual<void>(textWindow, 0x10, &text);
    DestroyString(text.text);
}

// The legend's rows from an exemplar: a colour swatch and a text each,
// stacked downwards. Property 0x4A0B47F2 has the colours, 0x4A0B47F3 the
// texts' instances, 0x4A0B47F4 the title's.
void FillLegend(char* self, void* exemplar)
{
    char* const window = self + map::kWindow;
    void* const properties = Virtual<void*>(exemplar, 0x1C);
    Uint32Vector colours = {};
    Uint32Vector texts = {};
    uint32_t titleInstance = kNoLegendTitle;
    Cdecl<uint8_t>(addr::SC4_GetPropertyUint32Vector, properties, uint32_t{0x4A0B47F2}, &colours);
    Cdecl<uint8_t>(addr::SC4_GetPropertyUint32Vector, properties, uint32_t{0x4A0B47F3}, &texts);
    Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, properties, uint32_t{0x4A0B47F4}, &titleInstance);

    void* title = nullptr;
    void* const titleWindow = Virtual<void*>(Virtual<void*>(window, 0x2C), 0x8C, kLegendTitle);
    Clear(title);
    if (Virtual<uint8_t>(titleWindow, 0x0, kTextIid, &title))
        SetLegendText(title, titleInstance);

    int32_t y = 0;
    void* const font = Virtual<void*>(Cdecl<void*>(addr::SC4_Singleton913C72), 0x14, uint32_t{0xE9C86B5F});
    if (colours.end != colours.begin) {
        void* swatch = nullptr;
        void* label = nullptr;
        for (uint32_t i = 0; i < static_cast<uint32_t>(colours.end - colours.begin); ++i) {
            if (colours.begin[i] == 0)
                continue;
            void* const swatchWindow = Virtual<void*>(Virtual<void*>(window, 0x2C), 0x8C, kLegendSwatch + i);
            Clear(swatch);
            if (Virtual<uint8_t>(swatchWindow, 0x0, kSwatchIid, &swatch)) {
                Rect area;
                area.left = 0x173;
                area.top = y + 0x3D;
                area.right = Virtual<int32_t>(swatchWindow, 0xA4) + 0x173;
                area.bottom = Virtual<int32_t>(swatchWindow, 0xA8) + y + 0x3D;
                Virtual<void>(swatchWindow, 0x110, uint32_t{1}, uint32_t{1});
                Virtual<void>(swatchWindow, 0xD8, &area);
                const uint8_t* const bytes = reinterpret_cast<const uint8_t*>(&colours.begin[i]);
                const uint32_t high = bytes[2];
                const uint32_t low = colours.begin[i] & 0xFF;
                const uint32_t middle = bytes[1];
                void* const maker = Virtual<void*>(swatch, 0xC);
                const uint32_t colour = Virtual<uint32_t>(maker, 0x1B0, high, middle, low);
                Virtual<void>(swatch, 0x10, colour);
            }
            // One past the end of the texts is let through, as in the original.
            if (i > static_cast<uint32_t>(texts.end - texts.begin))
                continue;
            void* const labelWindow = Virtual<void*>(Virtual<void*>(window, 0x2C), 0x8C, kLegendRow + i);
            Clear(label);
            if (!Virtual<uint8_t>(labelWindow, 0x0, kTextIid, &label))
                continue;
            int8_t marginX;
            int8_t marginY;
            Virtual<void>(label, 0x60, &marginX, &marginY);
            Rect area;
            area.top = y + 0x18;
            area.left = 0x116;
            area.right = Virtual<int32_t>(labelWindow, 0xA4) + 0x116;
            area.bottom = Virtual<int32_t>(labelWindow, 0xA8) + y + 0x18;
            LocalizedString text;
            This<void*>(addr::SC4LocalizedString_SC4LocalizedString, &text, kTextGroup, texts.begin[i], uint32_t{1});
            Virtual<void>(labelWindow, 0x110, uint32_t{1}, uint32_t{1});
            Virtual<void>(labelWindow, 0xD8, &area);
            Virtual<void>(label, 0x10, &text);
            area.right += -static_cast<int32_t>(marginX) * 2;
            area.bottom += -static_cast<int32_t>(marginY) * 2;
            This<void>(addr::cRZBaseString_Trim, &text);
            // The font measures the text into the rectangle; the next row
            // starts a whole number of 18-pixel lines further down.
            const uint32_t length = static_cast<uint32_t>(text.text.body.end - text.text.body.begin);
            const char* const characters = This<const char*>(addr::cRZBaseString_Data, &text);
            Virtual<uint32_t>(font, 0xB8, characters, length, &area, uint32_t{1}, uint32_t{1});
            y += static_cast<int32_t>(static_cast<uint32_t>(area.bottom - area.top + 0x11) / 18 * 18);
            DestroyString(text.text);
        }
        if (label)
            Release(label);
        if (swatch)
            Release(swatch);
    }
    if (title)
        Release(title);
    if (texts.begin)
        Free(texts.begin);
    if (colours.begin)
        Free(colours.begin);
}

// The game's 0x007A04F0 (ret 4): the legend under the picture. Hides the
// nine rows; with 0 the title gets the "no legend" text and the key window
// 0x8A2871C4 is hidden; otherwise the rows come from the exemplar
// (0x6534284A, 0x690F693F, 0x4A0B6800 + legend). Legend 3 shows the key
// window when the child 0xAA32BCD4 has its flag 1.
void __fastcall MapShowLegend(char* self, void*, uint32_t legend)
{
    for (uint32_t i = 0; i < 9; ++i) {
        ShowChild(At<void*>(self, map::kButtons), kLegendSwatch + i, 0);
        ShowChild(At<void*>(self, map::kButtons), kLegendRow + i, 0);
    }
    Virtual<void>(Virtual<void*>(Parent(self), 0x8C, kLegendRow), 0x170);
    if (legend == 0) {
        void* title = nullptr;
        void* const titleWindow = Virtual<void*>(Parent(self), 0x8C, kLegendTitle);
        Clear(title);
        if (Virtual<uint8_t>(titleWindow, 0x0, kTextIid, &title))
            SetLegendText(title, kNoLegendTitle);
        ShowChild(Parent(self), kLegendKey, 0);
        if (title)
            Release(title);
        return;
    }
    void* const resources = Global<void*>(addr::SC4_ResourceManager);
    ResourceKey key;
    key.instance = legend + 0x4A0B6800;
    void* exemplar = nullptr;
    key.type = 0x6534284A;
    key.group = 0x690F693F;
    if (Virtual<uint8_t>(resources, 0xC, &key, kExemplarIid, &exemplar, uint32_t{0}, uint32_t{0}))
        FillLegend(self, exemplar);
    if (legend == 3) {
        At<uint8_t>(self, map::kKeyShown) = 1;
        void* const toggle = Virtual<void*>(Parent(self), 0x8C, uint32_t{0xAA32BCD4});
        if (Virtual<uint8_t>(toggle, 0x10C, uint32_t{1}))
            ShowChild(Parent(self), kLegendKey, 1);
    } else {
        At<uint8_t>(self, map::kKeyShown) = 0;
        ShowChild(Parent(self), kLegendKey, 0);
    }
    if (exemplar)
        Release(exemplar);
}

// The game's 0x007A0F30 (ret 8): a button of the row 0x5001 to 0x5011 was
// chosen. Presses it alone, hands its legend to slot 3 and the legend
// builder, and lets 0x0079DE70 lay the window out again when the child
// 0xAA32BCD5 has its flag 1.
void __fastcall MapChooseSecond(char* self, void*, uint32_t button, uint32_t legend)
{
    PressOneOf(At<void*>(self, map::kButtons), 0x5001, 0x11, button);
    Virtual<void>(self, 0xC, legend);
    MapShowLegend(self, nullptr, legend);
    void* const toggle = Virtual<void*>(Parent(self), 0x8C, uint32_t{0xAA32BCD5});
    if (Virtual<uint8_t>(toggle, 0x10C, uint32_t{1}))
        This<void>(addr::SC4UI_F79DE70, self);
    At<uint32_t>(self, map::kSecond) = legend;
}

// The game's 0x007A0FA0 (ret 8): the same for the row 0x6001 to 0x600C.
void __fastcall MapChooseThird(char* self, void*, uint32_t button, uint32_t legend)
{
    PressOneOf(At<void*>(self, map::kButtons), 0x6001, 0xC, button);
    Virtual<void>(self, 0xC, legend);
    MapShowLegend(self, nullptr, legend);
    At<uint32_t>(self, map::kThird) = legend;
}

// The game's 0x007A0FE0 (ret 8): the same for the row 0x8003 to 0x800C.
void __fastcall MapChooseFourth(char* self, void*, uint32_t button, uint32_t legend)
{
    PressOneOf(At<void*>(self, map::kButtons), 0x8003, 0xA, button);
    Virtual<void>(self, 0xC, legend);
    MapShowLegend(self, nullptr, legend);
    At<uint32_t>(self, map::kFourth) = legend;
}

// --- The map window: the marks in the city --------------------------------------------

// The game's 0x007A1220 (ret 8): a building wanted with a mark. Its place
// is its lot's centre, or its own without a lot; it is highlighted
// (occupant slot 17 with 1, 0) and goes into the map of the wanted marks.
void __fastcall MapWantMark(char* self, void*, void* occupant, uint32_t size)
{
    Mark mark;
    mark.occupant = nullptr;
    mark.effect = nullptr;
    if (void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x38, occupant))
        Virtual<void>(lot, 0x3C, mark.position);
    else
        Virtual<void>(occupant, 0x20, mark.position);
    Virtual<void>(occupant, 0x44, uint32_t{1}, uint32_t{0});
    if (occupant != mark.occupant) {
        AddRef(occupant);
        mark.occupant = occupant;
    }
    mark.size = size;
    MarkNode* where;
    This<void>(addr::SC4UI_F7A0AE0, self + map::kWanted, &where, &mark);
    if (mark.effect)
        Release(mark.effect);
    if (mark.occupant)
        Release(mark.occupant);
}

// The game's 0x007A1390, no arguments: the wanted marks become the ones
// shown. Both maps are walked in key order: a mark shown and no longer
// wanted loses its highlight and its effect and goes; a new one is set up
// by 0x0079F620 and copied in; the mark at a point (no occupant) is always
// renewed. The wanted map is left empty. True when anything changed.
uint8_t __fastcall MapUpdateMarks(char* self, void*)
{
    MarkMap* const shown = &At<MarkMap>(self, map::kShown);
    MarkMap* const wanted = &At<MarkMap>(self, map::kWanted);
    MarkNode* const shownEnd = shown->header;
    MarkNode* old = shownEnd->left;
    MarkNode* const wantedEnd = wanted->header;
    MarkNode* fresh = wantedEnd->left;
    uint8_t changed = 0;
    MarkNode* where;
    while (old != shownEnd && fresh != wantedEnd) {
        const int32_t oldKey = static_cast<int32_t>(reinterpret_cast<std::intptr_t>(old->value.occupant));
        const int32_t freshKey = static_cast<int32_t>(reinterpret_cast<std::intptr_t>(fresh->value.occupant));
        if (freshKey > oldKey) {
            if (old->value.occupant)
                Virtual<void>(old->value.occupant, 0x44, uint32_t{0}, uint32_t{0});
            if (void* const effect = old->value.effect)
                Virtual<void>(effect, 0x10, uint32_t{0});
            MarkNode* const gone = old;
            old = Next(old);
            EraseShown(self, gone);
            changed = 1;
        } else if (freshKey < oldKey) {
            This<void>(addr::SC4UI_F79F620, self, &fresh->value);
            This<void>(addr::SC4UI_F7A0B30, shown, &where, old, &fresh->value);
            fresh = Next(fresh);
            changed = 1;
        } else if (old->value.occupant) {
            old = Next(old);
            fresh = Next(fresh);
        } else {
            if (void* const effect = old->value.effect)
                Virtual<void>(effect, 0x10, uint32_t{0});
            This<void>(addr::SC4UI_F79F620, self, &fresh->value);
            This<void>(addr::SC4UI_F79F760, &old->value, &fresh->value);
            old = Next(old);
            fresh = Next(fresh);
            changed = 1;
        }
    }
    if (old != shownEnd || fresh != wantedEnd)
        changed = 1;
    while (old != shownEnd) {
        if (old->value.occupant)
            Virtual<void>(old->value.occupant, 0x44, uint32_t{0}, uint32_t{0});
        if (void* const effect = old->value.effect)
            Virtual<void>(effect, 0x10, uint32_t{0});
        MarkNode* const gone = old;
        old = Next(old);
        EraseShown(self, gone);
    }
    while (fresh != wantedEnd) {
        This<void>(addr::SC4UI_F79F620, self, &fresh->value);
        This<void>(addr::SC4UI_F7A0B30, shown, &where, shownEnd, &fresh->value);
        fresh = Next(fresh);
    }
    ClearMarks(wanted);
    return changed;
}

// What a simulator says of one of its buildings (slot `range`): the last
// value is the mark's size. The first is 0.5 going in.
struct Coverage {
    uint32_t size;
    uint32_t other[4];
};

// Marks for the buildings a simulator lists (slot `lister`).
void WantCovered(char* self, void* simulator, std::size_t lister, bool withZero, std::size_t range,
                 ListNode*& list)
{
    if (withZero)
        Virtual<void>(simulator, lister, &list, uint32_t{0});
    else
        Virtual<void>(simulator, lister, &list);
    ListNode* const end = list;
    for (ListNode* node = end->next; node != end; node = node->next) {
        void* const occupant = node->value;
        Coverage coverage;
        float half[4];
        half[0] = 0.5f;
        Virtual<void>(simulator, range, occupant, &half[0], &coverage.other[0], &coverage.size);
        MapWantMark(self, nullptr, occupant, coverage.size);
    }
}

// Marks without a size for the buildings a simulator lists.
void WantListed(char* self, void* simulator, std::size_t lister, ListNode*& list)
{
    Virtual<void>(simulator, lister, &list);
    ListNode* const end = list;
    for (ListNode* node = end->next; node != end; node = node->next)
        MapWantMark(self, nullptr, node->value, uint32_t{0});
}

// Kind 5: one mark at the cell the service 0x00B43D70 names (slot 8), on
// the ground, with the effect named at +0x984 when there is a name.
void WantPoint(char* self)
{
    uint8_t flag;
    int32_t x;
    int32_t z;
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_Service43D70), 0x20, &flag, &x, &z))
        return;
    void* const terrain = Global<void*>(addr::SC4_Terrain);
    void* effect = nullptr;
    Mark mark;
    mark.occupant = nullptr;
    mark.effect = nullptr;
    mark.position[0] = static_cast<float>((static_cast<double>(x) + 0.5) * 16.0);
    mark.position[2] = static_cast<float>((static_cast<double>(z) + 0.5) * 16.0);
    mark.position[1] = static_cast<float>(Virtual<double>(terrain, 0x34, mark.position[0], mark.position[2]));
    const GameString* const name = &At<GameString>(self, map::kEffectName);
    if (name->body.begin != name->body.end) {
        void* const effects = Global<void*>(addr::SC4_Service43D1C);
        const char* const text = name->body.begin;
        void** const out = This<void**>(addr::SC4RefPtr_Reset, &effect);
        if (Virtual<uint8_t>(effects, 0x1C, text, out)) {
            Placement placement = {};
            placement.matrix[0] = 1.0f;
            placement.matrix[4] = 1.0f;
            placement.matrix[8] = 1.0f;
            placement.position[0] = mark.position[0];
            placement.position[1] = mark.position[1];
            placement.position[2] = mark.position[2];
            placement.scale = 100.0f;
            placement.b = 3;
            placement.a = 2;
            Virtual<void>(effect, 0x20, &placement);
            Virtual<void>(effect, 0xC, uint32_t{0});
            This<void>(addr::SC4RefPtr_AssignFrom, &mark.effect, &effect);
            Clear(mark.occupant);
        }
    }
    mark.size = 0x42C80000; // 100
    MarkNode* where;
    This<void>(addr::SC4UI_F7A0AE0, self + map::kWanted, &where, &mark);
    if (effect)
        Release(effect);
    This<void>(addr::SC4RefPair_Destroy, &mark);
}

// Kind 6: the pollution simulator's buildings (slot 46) and the landfill
// occupant of every cell zoned as landfill.
void WantGarbage(char* self, ListNode*& list)
{
    WantListed(self, Global<void*>(addr::SC4_PollutionSimulator), 0xB8, list);
    void* const occupants = Global<void*>(addr::SC4_NetworkService);
    void* const zones = Global<void*>(addr::SC4_Service43D14);
    char* const type = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0x10});
    if (type) {
        This<void*>(addr::SC4_OccupantFilterBase, type);
        At<std::uintptr_t>(type, 0x0) = addr::SC4OccupantTypeFilter_Vtable;
        At<std::uintptr_t>(type, 0x4) = addr::SC4OccupantTypeFilter_Vtable2;
        At<uint32_t>(type, 0xC) = 0;
        AddRef(type);
    }
    // Not checked for null, as in the original.
    At<uint32_t>(type, 0xC) = kLandfillOccupant;
    void* const grid = Virtual<void*>(zones, 0x14);
    const int32_t width = Virtual<int32_t>(grid, 0x30);
    const int32_t height = Virtual<int32_t>(grid, 0x34);
    for (int32_t x = 0; x < width; ++x) {
        char* const columns = *Virtual<char**>(grid, 0x64);
        const uint8_t* const column = At<const uint8_t*>(columns, static_cast<std::size_t>(x) * 0xC);
        for (int32_t z = 0; z < height; ++z) {
            if (column[z] != kLandfillZone)
                continue;
            void* found = nullptr;
            Virtual<void>(occupants, 0x80, &found, x, z, static_cast<void*>(type));
            if (found) {
                MapWantMark(self, nullptr, found, uint32_t{0});
                if (found)
                    Release(found);
            }
        }
    }
    Release(type);
}

// The game's 0x007A1A00, no arguments (the list has it inside 0x007A17B0):
// collects the marks the kind at +0x980 wants and makes them the ones shown.
// True when the marks changed. Its callers draw the picture again after it.
uint8_t __fastcall MapCollectMarks(char* self, void*)
{
    ListNode* list = nullptr;
    list = static_cast<ListNode*>(Virtual<void*>(NodeAllocatorAt(addr::SC4List_NodeAllocator2), 0xC, uint32_t{0xC}));
    list->next = list;
    list->previous = list;
    switch (At<uint32_t>(self, map::kMarkKind)) {
    case 1:
        WantCovered(self, Global<void*>(addr::SC4_Service43DA4), 0xBC, true, 0xB4, list);
        break;
    case 2:
        WantCovered(self, Global<void*>(addr::SC4_Service43DA4), 0xC4, true, 0xB4, list);
        break;
    case 3:
        WantCovered(self, Global<void*>(addr::SC4_Service43D78), 0x58, false, 0x54, list);
        break;
    case 4:
        WantCovered(self, Global<void*>(addr::SC4_PoliceSimulator), 0x54, false, 0x50, list);
        break;
    case 5:
        WantPoint(self);
        break;
    case 6:
        WantGarbage(self, list);
        break;
    case 7:
        WantListed(self, Global<void*>(addr::SC4_Service43DA0), 0x64, list);
        break;
    case 8:
        WantListed(self, Global<void*>(addr::SC4_Service43D94), 0x70, list);
        break;
    case 9:
        WantListed(self, Global<void*>(addr::SC4_Service43DA8), 0x70, list);
        break;
    default:
        break;
    }
    // The occupant manager's slot 48 gives the listed occupants back.
    Virtual<void>(Global<void*>(addr::SC4_NetworkService), 0xC0, &list);
    const uint8_t changed = MapUpdateMarks(self, nullptr);
    This<void>(addr::SC4List_Clear2, &list);
    ListNode* const sentinel = list;
    Virtual<void>(NodeAllocatorAt(addr::SC4List_NodeAllocator2), 0x10, static_cast<void*>(sentinel));
    return changed;
}

// --- The map window: its messages -----------------------------------------------------

// The message a mode listens for; 0 for the modes with none of their own.
uint32_t ModeMessage(uint32_t mode)
{
    switch (mode) {
    case 1:
        return 0x2A09B21A;
    case 2:
        return 0x87CC75B3;
    case 4:
        return 0x88EA4DFF;
    case 7:
        return 0x6A07C2EE;
    case 8:
        return 0xFA6DD0BB;
    case 9:
    case 0x4C:
        return 0x69247DC7;
    case 10:
        return 0x6946DC34;
    case 0x18:
        return 0x5A6DD1AA;
    case 0x19:
        return 0xDA6DD2CB;
    case 0x1A:
        return 0xDA6DD35D;
    case 0x1D:
        return 0x09B6D58E;
    default:
        return 0;
    }
}

// The game's 0x007A17B0, no arguments: registers the message target for
// the mode at +0x144. The service 0x86AD10EC is told (slot 8) 0xC0 for mode
// 0x4C and 0 otherwise; the mode's message, or 0x86AD10EE for a mode with
// none, is added to the vector at +0x9BC, which is sorted, and the message
// server is asked for every ID in it. The vector is never emptied, so the
// IDs of earlier modes are asked for again.
void __fastcall MapListen(char* self, void*)
{
    void* const target = self + map::kTarget;
    void* const ticks = Cdecl<void*>(addr::SC4_GetService86AD10EC);
    if (At<uint32_t>(self, map::kMode) == 0x4C)
        Virtual<void>(ticks, 0x20, target, uint32_t{0xC0});
    else
        Virtual<void>(ticks, 0x20, target, uint32_t{0});
    Uint32Vector* const ids = &At<Uint32Vector>(self, map::kMessageIds);
    if (const uint32_t id = ModeMessage(At<uint32_t>(self, map::kMode))) {
        This<void>(addr::SC4PtrVector_PushBack, ids, &id);
    } else {
        static const uint32_t kEveryTick = 0x86AD10EE;
        uint32_t* const end = ids->end;
        if (end != ids->capacity) {
            if (end)
                *end = kEveryTick;
            ++ids->end;
        } else {
            uint8_t tag = 0;
            This<void>(addr::SC4PtrVector_InsertN, ids, end, &kEveryTick, &tag, uint32_t{1}, uint32_t{1});
        }
    }
    Cdecl<void>(addr::SC4_SortUint32Range, ids->begin, ids->end);
    uint32_t* const last = ids->end;
    for (uint32_t* id = ids->begin; id != last; ++id)
        Virtual<void>(At<void*>(self, map::kMessages), 0x14, target, *id);
}

bool InstallMap2(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[32] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F7A0280, reinterpret_cast<void*>(&MapDestroy)},
        {addr::SC4UI_F7A0380, reinterpret_cast<void*>(&QueryInterface4)},
        {addr::SC4UI_F7A0390, reinterpret_cast<void*>(&AddRef4)},
        {addr::SC4UI_F7A03A0, reinterpret_cast<void*>(&Release4)},
        {addr::SC4UI_F7A03B0, reinterpret_cast<void*>(&QueryInterface8)},
        {addr::SC4UI_F7A03C0, reinterpret_cast<void*>(&AddRef8)},
        {addr::SC4UI_F7A03D0, reinterpret_cast<void*>(&Release8)},
        {addr::SC4UI_F7A03E0, reinterpret_cast<void*>(&MapDelete8)},
        {addr::SC4UI_F7A03F0, reinterpret_cast<void*>(&QueryInterfaceE0)},
        {addr::SC4UI_F7A0400, reinterpret_cast<void*>(&AddRefE0)},
        {addr::SC4UI_F7A0410, reinterpret_cast<void*>(&ReleaseE0)},
        {addr::SC4UI_F7A0420, reinterpret_cast<void*>(&FilterMake)},
        {addr::SC4UI_F7A0480, reinterpret_cast<void*>(&FilterDelete)},
        {addr::SC4UI_F7A04A0, reinterpret_cast<void*>(&FilterDestroy)},
        {addr::SC4UI_F7A04F0, reinterpret_cast<void*>(&MapShowLegend)},
        {addr::SC4UI_F7A0D50, reinterpret_cast<void*>(&MapConstruct)},
        {addr::SC4UI_F7A0F10, reinterpret_cast<void*>(&MapDelete)},
        {addr::SC4UI_F7A0F30, reinterpret_cast<void*>(&MapChooseSecond)},
        {addr::SC4UI_F7A0FA0, reinterpret_cast<void*>(&MapChooseThird)},
        {addr::SC4UI_F7A0FE0, reinterpret_cast<void*>(&MapChooseFourth)},
        {addr::SC4UI_F7A1220, reinterpret_cast<void*>(&MapWantMark)},
        {addr::SC4UI_F7A12D0, reinterpret_cast<void*>(&FilterAdd)},
        {addr::SC4UI_F7A1390, reinterpret_cast<void*>(&MapUpdateMarks)},
        {addr::SC4UI_F7A17B0, reinterpret_cast<void*>(&MapListen)},
        {addr::SC4UI_F7A1A00, reinterpret_cast<void*>(&MapCollectMarks)},
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

const bool g_map2Registered = registry::Register({
    "ui.map.part2",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F7A04F0,
    "Our map window construction, legend, mode buttons, messages and marks in the city (docs/ui.md)",
    false,
    InstallMap2,
});

} // namespace
} // namespace opensc4::ui
