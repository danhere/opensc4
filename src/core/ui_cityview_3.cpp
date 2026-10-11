// The city view (cSC4View3DWin), third part: 0x007EA5B0 to 0x007EC980.
//
// The view object has its interface (cISC4View3DWin, vtable 0x00ABCE40) at
// +0, a message target at +4, a command target at +8 and its window (vtable
// 0x00ABCBC0) at +0xC. `this` is the object's start in every function here.
//
// What this part holds:
//
//   - the view's record in the city file: written at 0x007EA5B0 (message
//     0x26C63344), read at 0x007EBFA0 (message 0x26C63341). The record
//     (type 0xA9C8647C, group 0xE9C8647E, instance 1, version 4) holds four
//     references and the table of global effect triggers;
//   - the texts of the control panel: the date (0x007EA6E0), the clock
//     (0x007EA810, 0x007EC0B0), the funds (0x007EA970), the population
//     (0x007EAAA0), the city's name (0x007EAB60), and the loading picture's
//     text emptied (0x007EABF0);
//   - the pop-up menus next to the panel's buttons: 0x007EAEB0 puts a menu
//     into a frame with arrows and an info tip and shows it; 0x007EC620,
//     0x007EC680 and 0x007EC6E0 make the menu from a list of items first;
//     0x007EC740 opens the three menus of one panel by the button pressed;
//   - the mode buttons (0x007EC2D0: god, mayor, MySim and a fourth) and the
//     dialog that names a new city (0x007EB320), which the mayor button
//     runs while the city is not yet established;
//   - interface methods: SetCurrentViewInputControl (slot 25, 0x007EBD70),
//     AddPersistentViewInputControl (slot 28, 0x007EBEA0), the cost
//     indicator's two setters (slot 42, 0x007EC800, which is the overload
//     with two numbers, and slot 44, 0x007EC900), the global effect
//     triggers' getter and Retract (slots 46 and 48, 0x007EB850 and
//     0x007EB8A0), UpdateMissionStatusPanel (slot 58, 0x007EB970) and the
//     mission panel's texts (0x007EBB20);
//   - what the view does when the city is ready (0x007EBF30).
//
// Replacement, off by default:
//
//   ui.cityview.part3: everything named above.
//
// Left the game's: 0x007EAC70, which draws the cost indicator's amount
// into its picture (the text measured, drawn nine times for an outline;
// drawing), called by address from the two setters; 0x007EBD30
// (SC4PtrVector::PushBack, shared container code). Not functions: the list
// entries 0x007EADB0, 0x007EB360, 0x007EBEC0 and 0x007EC590 lie inside
// 0x007EAC70, 0x007EB320, 0x007EBEA0 and 0x007EC2D0 (the last is that
// function's jump table). 0x007EC620 is a start the list did not have.
//
// The clock (0x007EC0B0) is mixed: beside the texts it copies one frame of
// the day and night strip into the clock button's picture with one call of
// the image's slot 29. That call is kept as an ordinary virtual call.
//
// The view's fields used here: +0x10 the window manager and +0x14 the key
// state (the window's own fields), +0xF0, +0xF4 (the renderer), +0xF8 (the
// model maker) and +0x154 (the simulator) the four saved references,
// +0x125 a byte "developer", +0x134 the tools (view input controls) in use
// (a vector), +0x140 the persistent tools, +0x18C the control panel's
// window, +0x1F4 the mode (0 god, 1 mayor, 2 MySim, 3, 4 god once the city
// is established; -1 none), +0x1FC the open menu's frame, +0x204 the open
// menu's ID, +0x208 where the open menu's two numbers are kept, +0x20C,
// +0x214 and +0x21C three such pairs, +0x2D4 to +0x2D6 three bytes of the
// news ticker's state, +0x2DC the news ticker, +0x2E8 the budget panel,
// +0x310, +0x314 and +0x318 the date's, the funds' and the population's
// text controls, +0x3B0 the cost shown, +0x3B4 the indicator's state (0
// off, 1 a cost, 2 fading), +0x3B8 its kind (0 affordable, 1 too dear, 2
// fading), +0x3BC a float (the fade's time), +0x3C0 to +0x3CC two points,
// +0x3D0 the indicator's picture, +0x3D4 its window, +0x3D8 the menus' art,
// +0x404 the loading picture, +0x408 a byte Read clears, +0x40C the mission
// panel, +0x428 the triggers (a hash map from ID to count; +0x438 its
// count).
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

namespace view {
constexpr std::size_t kWindow = 0xC;
constexpr std::size_t kManager = 0x10;
constexpr std::size_t kKeys = 0x14;
constexpr std::size_t kSavedF0 = 0xF0;
constexpr std::size_t kRenderer = 0xF4;
constexpr std::size_t kModelMaker = 0xF8;
constexpr std::size_t kDeveloper = 0x125;
constexpr std::size_t kControls = 0x134;
constexpr std::size_t kPersistent = 0x140;
constexpr std::size_t kSimulator = 0x154;
constexpr std::size_t kPanel = 0x18C;
constexpr std::size_t kMode = 0x1F4;
constexpr std::size_t kMenuFrame = 0x1FC;
constexpr std::size_t kMenuId = 0x204;
constexpr std::size_t kMenuState = 0x208;
constexpr std::size_t kMenuState20C = 0x20C;
constexpr std::size_t kMenuState214 = 0x214;
constexpr std::size_t kMenuState21C = 0x21C;
constexpr std::size_t kTickerByte2D4 = 0x2D4;
constexpr std::size_t kTickerByte2D5 = 0x2D5;
constexpr std::size_t kTickerByte2D6 = 0x2D6;
constexpr std::size_t kTicker = 0x2DC;
constexpr std::size_t kBudgetPanel = 0x2E8;
constexpr std::size_t kDateText = 0x310;
constexpr std::size_t kFundsText = 0x314;
constexpr std::size_t kPopulationText = 0x318;
constexpr std::size_t kCost = 0x3B0;
constexpr std::size_t kCostState = 0x3B4;
constexpr std::size_t kCostKind = 0x3B8;
constexpr std::size_t kCostTime = 0x3BC;
constexpr std::size_t kPoint3C0 = 0x3C0;
constexpr std::size_t kPoint3C8 = 0x3C8;
constexpr std::size_t kCostPicture = 0x3D0;
constexpr std::size_t kCostWindow = 0x3D4;
constexpr std::size_t kMenuArt = 0x3D8;
constexpr std::size_t kLoading = 0x404;
constexpr std::size_t kByte408 = 0x408;
constexpr std::size_t kMission = 0x40C;
constexpr std::size_t kTriggers = 0x428;
constexpr std::size_t kTriggerCount = 0x438;
} // namespace view

// The preferences (cISC4App slot 38): the last city name, mayor name and
// difficulty typed into the naming dialog.
namespace prefs {
constexpr std::size_t kCityName = 0x6C;
constexpr std::size_t kMayorName = 0x26C;
constexpr std::size_t kDifficulty = 0x46C;
} // namespace prefs

constexpr uint32_t kCheckIid = 0x8810;
constexpr uint32_t kWinGenIid = 0x5386D516;
constexpr uint32_t kEditIid = 0x231A1C57;
constexpr uint32_t kTextIid = 0x212CDC1F;
constexpr uint32_t kPictureIid = 0xC12CEA13;
constexpr uint32_t kTintIid = 0x86D72B57;

constexpr uint32_t kViewRecordType = 0xA9C8647C;
constexpr uint32_t kViewRecordGroup = 0xE9C8647E;

// The mode buttons: their commands and their windows in the panel.
constexpr uint32_t kGodCommand = 0x6A935C34;
constexpr uint32_t kMayorCommand = 0x6A935CA4;
constexpr uint32_t kMySimCommand = 0x6A935CA8;
constexpr uint32_t kFourthCommand = 0x6A935CAB;
constexpr uint32_t kGodButton = 0x2988BC85;
constexpr uint32_t kMayorButton = 0xC988BC79;
constexpr uint32_t kMySimButton = 0x4988BC6A;
constexpr uint32_t kFourthButton = 0x8988BC94;

constexpr uint32_t kNone = 0xFFFFFFFF;
constexpr uint32_t kTriggerRetracted = 0xAA375FE8;

struct Point {
    int32_t x;
    int32_t y;
};

struct Rect {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
};
static_assert(sizeof(Rect) == 0x10);

// A window message as window procedures get it.
struct WinMessage {
    uint32_t type;
    void* a;
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

// A string the game's constructors fill, with the room the original frames
// leave after it.
struct RoomyString {
    GameString text;
    char room[0xC];
};

// The game's image holder (0x30 bytes); the image at +4.
struct ImageLoader {
    void* first;
    void* image;
    char rest[0x28];
};
static_assert(sizeof(ImageLoader) == 0x30);

// The stream object the game puts on the stack for one record of a segment
// (SC4SegmentIStream, SC4SegmentOStream).
struct SegmentStream {
    std::uintptr_t vtable;
    void* record;
    void* stream;
    uint16_t version;
    uint16_t unused;
};
static_assert(sizeof(SegmentStream) == 0x10);

// A node of the triggers' hash map and an iterator into it.
struct MapNode {
    MapNode* next;
    uint32_t key;
    uint32_t value;
};

struct MapIterator {
    MapNode* node;
    char* table;
};

// A vector of pointers.
struct PtrVector {
    void** begin;
    void** end;
    void** capacity;
};

using SprintfFn = void(__cdecl*)(void*, const char*, ...);

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

// The game's inlined reference holder: empty it (cleared first, then let go).
void Clear(void*& holder)
{
    if (void* const object = holder) {
        holder = nullptr;
        Release(object);
    }
}

// An empty string as the game's inlined constructor makes it.
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

// The inlined destructor of a string.
void DestroyString(GameString& text)
{
    char* const buffer = text.body.begin;
    text.vtable = addr::SC4String_Vtable;
    if (buffer)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(buffer));
}

// The game's ftol (0x009EEF04).
int32_t ToInt(double value)
{
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

// A new message, or null (the callers do not check).
void* NewMessage()
{
    void* const memory = Cdecl<void*>(addr::RZ_operator_new, uint32_t{0x2C});
    return memory ? This<void*>(addr::cGZMessage_cGZMessage, memory) : nullptr;
}

// The language utility of the application (its slot 38 with 0).
void* Language()
{
    return Virtual<void*>(Cdecl<void*>(addr::SC4_CurrentApp), 0x98, uint32_t{0});
}

void* City()
{
    return Global<void*>(addr::SC4_City);
}

void* Panel(char* self)
{
    return At<void*>(self, view::kPanel);
}

// The inlined push_back of a pointer vector; the grown case goes through
// the game's helper with the value's address given twice.
void PushBack(PtrVector& vector, void*& value)
{
    void** const end = vector.end;
    if (end != vector.capacity) {
        if (end)
            *end = value;
        vector.end += 1;
    } else {
        This<void>(addr::SC4PtrVector_InsertN, &vector, end, &value, &value, uint32_t{1}, uint32_t{1});
    }
}

// The inlined search of the triggers' map: the node of a key or null.
MapNode* FindTrigger(char* table, uint32_t id)
{
    MapNode** const buckets = At<MapNode**>(table, 0x4);
    const uint32_t count =
        static_cast<uint32_t>((At<char*>(table, 0x8) - reinterpret_cast<char*>(buckets)) >> 2);
    MapNode* node = buckets[id % count];
    while (node && node->key != id)
        node = node->next;
    return node;
}

// --- The view's record ----------------------------------------------------------------

// The game's 0x007EA5B0 (thiscall, one argument). Writes the view's record
// into a city file's segment: the four references, the number of triggers
// and each trigger's ID and count.
void __fastcall Save(char* self, void*, void* segment)
{
    SegmentStream out;
    This<void*>(addr::SC4SegmentOStream_SC4SegmentOStream, &out, segment, kViewRecordType, kViewRecordGroup,
                uint32_t{1}, uint32_t{4});
    if (This<uint8_t>(addr::SC4SegmentStream_IsOpen, &out)) {
        Cdecl<void>(addr::SC4Stream_WriteReference, out.stream, At<void*>(self, view::kSavedF0));
        char* const renderer = At<char*>(self, view::kRenderer);
        Cdecl<void>(addr::SC4Stream_WriteReference, out.stream,
                    static_cast<void*>(renderer ? renderer + 8 : nullptr));
        char* const maker = At<char*>(self, view::kModelMaker);
        Cdecl<void>(addr::SC4Stream_WriteReference, out.stream, static_cast<void*>(maker ? maker + 4 : nullptr));
        Cdecl<void>(addr::SC4Stream_WriteReference, out.stream, At<void*>(self, view::kSimulator));
        Virtual<void>(out.stream, 0x24, At<uint32_t>(self, view::kTriggerCount));

        MapIterator at;
        This<void>(addr::SC4HashTable_Begin, self + view::kTriggers, &at);
        MapNode* node = at.node;
        if (node) {
            char* const table = at.table;
            do {
                Virtual<void>(out.stream, 0x24, node->key);
                Virtual<void>(out.stream, 0x24, node->value);
                if (MapNode* const next = node->next) {
                    node = next;
                } else {
                    // The next bucket that is not empty.
                    const uint32_t count =
                        static_cast<uint32_t>((At<char*>(table, 0x8) - At<char*>(table, 0x4)) >> 2);
                    uint32_t index = node->key % count;
                    node = nullptr;
                    while (++index < count) {
                        node = At<MapNode**>(table, 0x4)[index];
                        if (node)
                            break;
                    }
                }
            } while (node);
        }
    }
    This<void>(addr::SC4SegmentOStream_Close, &out);
}

// The game's 0x007EBFA0 (thiscall, one argument). Reads the view's record
// (versions up to 4) and clears the byte +0x408. The original keeps the
// count in the argument's place on the stack.
void __fastcall Load(char* self, void*, void* segment)
{
    SegmentStream in;
    This<void*>(addr::SC4SegmentIStream_SC4SegmentIStream, &in, segment, kViewRecordType, kViewRecordGroup,
                uint32_t{1});
    if (This<uint8_t>(addr::SC4SegmentStream_IsOpen, &in) && in.version <= 4) {
        Cdecl<void>(addr::SC4Stream_ReadReference, in.stream, 0xC9C628ECu, &At<void*>(self, view::kSavedF0),
                    uint32_t{0});
        Cdecl<void>(addr::SC4Stream_ReadReference, in.stream, 0xE9C622D8u, &At<void*>(self, view::kRenderer),
                    uint32_t{0});
        Cdecl<void>(addr::SC4Stream_ReadReference, in.stream, 0xA9C741B3u, &At<void*>(self, view::kModelMaker),
                    uint32_t{0});
        Cdecl<void>(addr::SC4Stream_ReadReference, in.stream, 0x8695664Eu, &At<void*>(self, view::kSimulator),
                    uint32_t{0});
        // The original's count starts as the argument's value.
        uint32_t count = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(segment));
        Virtual<void>(in.stream, 0x24, &count);
        if (count) {
            char* const table = self + view::kTriggers;
            uint32_t index = 0;
            // Neither is set before the stream fills it.
            uint32_t key;
            uint32_t value;
            do {
                Virtual<void>(in.stream, 0x24, &key);
                Virtual<void>(in.stream, 0x24, &value);
                *This<uint32_t*>(addr::SC4Uint32HashMap_Lookup, table, &key) = value;
                ++index;
            } while (index < count);
        }
        At<uint8_t>(self, view::kByte408) = 0;
    }
    This<void>(addr::SC4SegmentIStream_Close, &in);
}

// --- The panel's texts ----------------------------------------------------------------

// The game's 0x007EA6E0 (thiscall, no arguments). The date's text: the
// simulator's date with the years counted from the city's start; past
// 10000 years the count starts again at 1.
void __fastcall UpdateDate(char* self, void*)
{
    if (!At<void*>(self, view::kDateText) || !At<void*>(self, view::kSimulator))
        return;
    void* const language = Language();
    void* const date = Virtual<void*>(At<void*>(self, view::kSimulator), 0x1C);
    GameString text;
    InitString(text);
    // A date object with its date interface at +8, which the simulator
    // service fills with the city's start date.
    alignas(8) char start[16];
    This<void*>(addr::cRZDate_Default, static_cast<void*>(start));
    Virtual<void>(Global<void*>(addr::SC4_SimulatorService), 0x14, static_cast<void*>(start + 8));
    uint32_t years = Virtual<uint32_t>(date, 0x34);
    years -= This<uint32_t>(addr::cRZDate_Year2, static_cast<void*>(start + 8));
    if (static_cast<int32_t>(years) > 10000)
        years += 0xFFFFD8F0u - (years - 0x2711u) / 10000u * 10000u;
    const uint32_t second = Virtual<uint32_t>(date, 0x3C);
    const uint32_t first = Virtual<uint32_t>(date, 0x5C);
    Virtual<void>(language, 0x28, first, second, years, &text, uint32_t{0});
    Virtual<void>(At<void*>(self, view::kDateText), 0x10, &text);
    This<void>(addr::cRZDate_Destroy, static_cast<void*>(start));
    DestroyString(text);
}

// The game's 0x007EA810 (thiscall, no arguments). The clock's text (the
// hour alone) when the panel's button 0xAA75CA06 has its state 0x2000,
// else the clock's window 0xAA771FD7 hidden.
void __fastcall UpdateClock(char* self, void*)
{
    void* const clock = Global<void*>(addr::SC4_Clock43DBC);
    void* const language = Language();
    GameString text;
    InitString(text);
    const uint32_t hour = Virtual<uint32_t>(clock, 0x28);
    Virtual<void>(language, 0x24, hour, int32_t{-1}, int32_t{-1}, &text);

    void* button = nullptr;
    if (Virtual<uint8_t>(Panel(self), 0x94, 0xAA75CA06u, kCheckIid, &button)) {
        if (Virtual<uint8_t>(button, 0x10, uint32_t{0x2000})) {
            void* const face = Virtual<void*>(Panel(self), 0x8C, 0xAA771FD7u);
            Virtual<void>(face, 0x110, uint32_t{0x800}, uint32_t{1});
            Virtual<void>(face, 0x5C);
            Virtual<void>(face, 0x110, uint32_t{0x800}, uint32_t{0});
            Virtual<void>(face, 0x110, uint32_t{1}, uint32_t{1});
            Cdecl<uint8_t>(addr::SC4Win_SetChildText, Panel(self), 0xAA771FC7u, &text);
        } else {
            Cdecl<uint8_t>(addr::SC4Win_ShowChild, Panel(self), 0xAA771FD7u, uint32_t{0}, uint32_t{0});
        }
    }
    if (button)
        Release(button);
    DestroyString(text);
}

// The game's 0x007EA970 (thiscall, no arguments). The funds' text and its
// colour (one for debts, one for the rest), the cost indicator judged
// again when it shows a cost that is too dear, and the budget panel
// refreshed when it is open. Nothing at all when the text control is there
// and the city is not.
void __fastcall UpdateFunds(char* self, void*)
{
    if (At<void*>(self, view::kFundsText)) {
        void* const city = City();
        if (!city)
            return;
        const int64_t funds = Virtual<int64_t>(Virtual<void*>(city, 0xD0), 0x18);
        GameString text;
        InitString(text);
        Cdecl<void>(addr::SC4_FormatMoney, funds, &text);
        Virtual<void>(At<void*>(self, view::kFundsText), 0x10, &text);
        void* const colours = Global<void*>(addr::SC4_Service43C9C);
        const uint32_t colour = funds < 0
            ? Virtual<uint32_t>(colours, 0x88, uint32_t{0x77}, uint32_t{0}, uint32_t{0})
            : Virtual<uint32_t>(colours, 0x88, uint32_t{0x44}, uint32_t{0x55}, uint32_t{0x66});
        Virtual<void>(At<void*>(self, view::kFundsText), 0x24, uint32_t{0}, colour);
        if (At<uint32_t>(self, view::kCostState) == 1 && At<uint32_t>(self, view::kCostKind) == 1)
            Virtual<void>(self, 0xAC, At<uint32_t>(self, view::kCost));
        DestroyString(text);
    }
    if (Virtual<uint8_t>(At<void*>(self, view::kBudgetPanel), 0x24))
        Virtual<void>(At<void*>(self, view::kBudgetPanel), 0x20);
}

// The game's 0x007EAAA0 (thiscall, no arguments). The population's text.
void __fastcall UpdatePopulation(char* self, void*)
{
    if (!At<void*>(self, view::kPopulationText))
        return;
    int32_t population = 0;
    if (void* const residential = Global<void*>(addr::SC4_Service43DA4))
        population = Virtual<int32_t>(residential, 0x40);
    GameString text;
    InitString(text);
    Virtual<void>(Language(), 0x48, static_cast<int64_t>(population), &text);
    Virtual<void>(At<void*>(self, view::kPopulationText), 0x10, &text);
    DestroyString(text);
}

// The game's 0x007EAB60 (thiscall, no arguments). The city's name into the
// panel's window 2. The city is not checked.
void __fastcall UpdateCityName(char* self, void*)
{
    void* const city = City();
    GameString text;
    InitString(text);
    Virtual<void>(city, 0x30, &text);
    Cdecl<uint8_t>(addr::SC4Win_SetChildText, Panel(self), uint32_t{2}, &text);
    DestroyString(text);
}

// The game's 0x007EABF0 (thiscall, no arguments). Gives the loading picture
// an empty text for its step 1.
void __fastcall ClearLoadingText(char* self, void*)
{
    if (!At<void*>(self, view::kLoading))
        return;
    GameString text;
    InitString(text);
    This<void>(addr::SC4UI_F777640, At<void*>(self, view::kLoading), uint32_t{1}, &text);
    DestroyString(text);
}

// The game's 0x007EC0B0 (thiscall, two arguments). The time of day: the
// text of the view's window 0x29EE06D8 (hour and minute; an hour of 24 or
// more means the clock's own), the day and night picture of the panel's
// button 0x6A64E353 (one 37 by 37 frame of a strip of 48, two for each
// hour), then the clock's text.
void __fastcall ShowTime(char* self, void*, uint32_t hour, uint32_t minute)
{
    void* const clock = Global<void*>(addr::SC4_Clock43DBC);
    if (hour >= 24)
        hour = Virtual<uint32_t>(clock, 0x28);
    char* const window = self + view::kWindow;
    if (void* const label = Virtual<void*>(window, 0x8C, 0x29EE06D8u)) {
        GameString text;
        InitString(text);
        Virtual<void>(Language(), 0x24, hour, minute, int32_t{-1}, &text);
        Virtual<void>(label, 0x128, &text);
        DestroyString(text);
    }

    void* gen = nullptr;
    void* picture = nullptr;
    if (Virtual<uint8_t>(window, 0x94, 0xE9889775u, kWinGenIid, &gen)) {
        Clear(picture);
        if (Virtual<uint8_t>(Panel(self), 0x94, 0x6A64E353u, kPictureIid, &picture)) {
            // What the picture's slot 9 fills; the original frame leaves
            // 0x14 bytes for it.
            uint32_t place[5];
            Virtual<void>(picture, 0x24, &place[0]);
            ImageLoader strip;
            This<void*>(addr::SC4ImageLoader_ConstructParts, &strip, uint32_t{0}, 0x46A006B0u, 0x13D14CA3u,
                        uint32_t{1}, uint32_t{0});
            ImageLoader frame;
            This<void*>(addr::SC4ImageLoader_ConstructParts, &frame, uint32_t{0}, 0x46A006B0u, 0x13D14CA3u,
                        uint32_t{1}, uint32_t{0});
            int32_t index = Virtual<int32_t>(clock, 0x28) << 1;
            if (Virtual<uint32_t>(clock, 0x34) > 0x1D)
                ++index;
            index *= 0x25;
            Rect part;
            part.left = index;
            part.top = 0;
            part.right = index + 0x25;
            part.bottom = 0x25;
            Virtual<void>(frame.image, 0x74, strip.image, &part, &place[0], uint32_t{0});
            Virtual<void>(picture, 0x10, frame.image);
            This<void>(addr::SC4ImageLoader_Destruct, &frame);
            This<void>(addr::SC4ImageLoader_Destruct, &strip);
        }
    }
    UpdateClock(self, nullptr);
    if (gen)
        Release(gen);
    if (picture)
        Release(picture);
}

// The game's 0x007EBF30 (thiscall, no arguments). The city is ready: three
// of the panel's buttons enabled, the tool in use taken off, the funds and
// the city's name shown.
void __fastcall CityReady(char* self, void*)
{
    Cdecl<uint8_t>(addr::SC4Win_EnableChild, Panel(self), 0x99887766u, uint32_t{1});
    Cdecl<uint8_t>(addr::SC4Win_EnableChild, Panel(self), 0x8B96B73Eu, uint32_t{1});
    Cdecl<uint8_t>(addr::SC4Win_EnableChild, Panel(self), kMySimButton, uint32_t{1});
    if (Virtual<void*>(self, 0x60))
        Virtual<void>(self, 0x68, uint32_t{0});
    UpdateFunds(self, nullptr);
    UpdateCityName(self, nullptr);
}

// --- The pop-up menus -----------------------------------------------------------------

// The game's 0x007EAEB0 (thiscall, six arguments). Shows `menu` in a frame
// with arrows next to the button `child` of `parent`. A menu that is open
// is closed first (CloseTertiaryMenu, slot 60), and when it is this one
// nothing else happens. The menu gets an info tip; the tools are all taken
// off; the frame becomes the view's child, the menu the frame's. `state`
// (two numbers the menu's slots 16 and 10 take) is remembered at +0x208;
// when its second number is set and the tutorial does not take the menu,
// the chosen item is started again (0x007E9FA0) unless it is disabled or
// 0x007E5960 says no, in which case the choice is cleared. The new objects
// are not checked for null, as in the original.
void __fastcall OpenMenu(char* self, void*, uint32_t id, void* parent, uint32_t child, void* menu,
                         uint32_t frameImage, uint32_t* state)
{
    if (const uint32_t open = At<uint32_t>(self, view::kMenuId)) {
        if (open == id) {
            Virtual<void>(self, 0xF0);
            return;
        }
        Virtual<void>(self, 0xF0);
    }

    Virtual<void>(menu, 0x30, uint32_t{0x2C}, uint32_t{0x2C}, uint32_t{5});
    char* const window = self + view::kWindow;
    Virtual<void>(Virtual<void*>(menu, 0xC), 0x158, static_cast<void*>(window));
    uint32_t rows = Virtual<uint32_t>(menu, 0x38);
    if (rows < 1)
        rows = 1;
    else if (rows > 8)
        rows = 8;
    if (Virtual<int32_t>(window, 0xA8) <= 600 && rows > 6)
        rows = 6;
    Point scratch;
    const Point size = *Virtual<const Point*>(menu, 0x34, &scratch, rows);

    char* const tipMemory = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0x1B4});
    void* tip = nullptr;
    if (tipMemory) {
        tip = This<void*>(addr::SC4InfoTip_Construct, tipMemory);
        if (tip)
            AddRef(tip);
    }
    Virtual<void>(tip, 0x2C, 0xA85F1A83u);
    Virtual<void>(tip, 0x28, 0xE9C86C9Du);
    Virtual<void>(Virtual<void*>(tip, 0xC), 0x118);
    Virtual<void>(tip, 0x10, Virtual<void*>(At<void*>(self, view::kMenuArt), 0x14), uint32_t{8}, uint32_t{5});
    Virtual<void>(tip, 0x4C, Virtual<void*>(At<void*>(self, view::kMenuArt), 0x50));
    Virtual<void>(menu, 0x58, tip);
    Virtual<void>(self, 0x6C, uint32_t{0});

    void* const button = Virtual<void*>(parent, 0x88, child);
    int32_t x = Virtual<int32_t>(button, 0xA4) / 2;
    x += Virtual<int32_t>(button, 0xAC);
    x += Virtual<int32_t>(parent, 0xAC);
    int32_t y = Virtual<int32_t>(button, 0xA8) / 2;
    y += Virtual<int32_t>(button, 0xB0);
    y += Virtual<int32_t>(parent, 0xB0);
    const int32_t panelTop = Virtual<int32_t>(Panel(self), 0xB0);

    uint32_t key[3] = {0x856DDBAC, 0x46A006B0, frameImage};
    ImageLoader loader;
    This<void*>(addr::SC4ImageLoader_ConstructKey, &loader, &key[0], uint32_t{1}, uint32_t{0});
    void* const image = loader.image;

    char* const arrowsMemory = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0x150});
    char* arrows = nullptr;
    if (arrowsMemory) {
        arrows = This<char*>(addr::SC4UI_F79AFF0, arrowsMemory);
        if (arrows)
            AddRef(arrows);
    }
    char* const frame = arrows + 4;
    Virtual<void>(frame, 0x100, 0x8A6E61E0u);
    Virtual<void>(window, 0x38, Virtual<void*>(arrows, 0xC));
    Virtual<void>(arrows, 0x10, image, uint32_t{0x35}, uint32_t{0x19}, uint32_t{0x50}, uint32_t{0x35}, uint32_t{4},
                  uint32_t{0x1B}, uint32_t{0x1D});
    Virtual<void>(arrows, 0x14, size.x, size.y, x, y, int32_t{10}, panelTop - 10);
    Virtual<void>(frame, 0x5C);
    Virtual<void>(frame, 0x114);
    At<void*>(self, view::kMenuFrame) = frame;
    AddRef(frame);
    At<uint32_t>(self, view::kMenuId) = id;

    Rect area;
    Virtual<void>(arrows, 0x18, &area);
    Virtual<void>(Virtual<void*>(menu, 0xC), 0xD8, &area);
    Virtual<void>(Virtual<void*>(menu, 0xC), 0x100, 0x8A2CAD8Bu);
    Virtual<void>(frame, 0x38, Virtual<void*>(menu, 0xC));
    Virtual<void>(frame, 0x158, Virtual<void*>(menu, 0xC));

    bool taught = false;
    if (void* const city = City()) {
        void* const tutorial = Virtual<void*>(city, 0x148);
        if (tutorial && Virtual<uint8_t>(tutorial, 0x20)) {
            Virtual<void>(tutorial, 0x3C, menu);
            taught = true;
        }
    }

    At<uint32_t*>(self, view::kMenuState) = state;
    if (state) {
        Virtual<void>(menu, 0x40, state[0]);
        Virtual<void>(menu, 0x28, state[1]);
        if (!taught && state[1]) {
            void* const item = Virtual<void*>(menu, 0x20);
            const uint32_t value = Virtual<uint32_t>(item, 0x1C);
            const uint32_t itemId = Virtual<uint32_t>(item, 0x20);
            if (Virtual<uint8_t>(item, 0x24) && !This<uint8_t>(addr::SC4UI_F7E5960, self, value, itemId))
                This<void>(addr::SC4UI_F7E9FA0, self, value, itemId, uint32_t{0});
            else
                Virtual<void>(menu, 0x28, uint32_t{0});
        }
    }

    Release(arrows);
    This<void>(addr::SC4ImageLoader_Destruct, &loader);
    Release(tip);
}

// The three makers below: a menu of the items of `group` for `command`
// (0x0078F100), held while OpenMenu shows it under the ID `id` plus a
// number.
void OpenListMenu(char* self, uint32_t group, uint32_t idOffset, uint32_t frameImage, void* parent,
                  uint32_t child, uint32_t id, uint32_t command, uint32_t* state)
{
    void* const menu = Cdecl<void*>(addr::SC4UI_F78F100, group, command, uint32_t{0}, id);
    if (menu)
        AddRef(menu);
    OpenMenu(self, nullptr, id + idOffset, parent, child, menu, frameImage, state);
    if (menu)
        Release(menu);
}

// The game's 0x007EC620 (thiscall, five arguments).
void __fastcall OpenMenu6A01FC2A(char* self, void*, void* parent, uint32_t child, uint32_t id, uint32_t command,
                                 uint32_t* state)
{
    OpenListMenu(self, 0x6A01FC2A, 0, 0x14215ED0, parent, child, id, command, state);
}

// The game's 0x007EC680 (thiscall, five arguments).
void __fastcall OpenMenuE83E0437(char* self, void*, void* parent, uint32_t child, uint32_t id, uint32_t command,
                                 uint32_t* state)
{
    OpenListMenu(self, 0xE83E0437, 0xEA5ADA53, 0x14215ED0, parent, child, id, command, state);
}

// The game's 0x007EC6E0 (thiscall, five arguments).
void __fastcall OpenMenu2A3858E4(char* self, void*, void* parent, uint32_t child, uint32_t id, uint32_t command,
                                 uint32_t* state)
{
    OpenListMenu(self, 0x2A3858E4, 0x8A3F003F, 0x14215ED1, parent, child, id, command, state);
}

// The game's 0x007EC740 (thiscall, two arguments: a window and a record
// whose third number is a button's ID). The buttons of one of the panel's
// rows: three open a menu each, one goes to 0x007E5C10 and one (0x30) to
// 0x007E6DA0.
void __fastcall RowButton(char* self, void*, void* window, const uint32_t* record)
{
    const uint32_t id = record[2];
    switch (id) {
    case 0x0991EE13:
        This<void>(addr::SC4UI_F7E5C10, self, 0xE9949936u, 0x69923479u, window, id);
        break;
    case 0x30:
        This<void>(addr::SC4UI_F7E6DA0, self, uint32_t{0});
        break;
    case 0x29920899:
        OpenMenu2A3858E4(self, nullptr, window, 0x29920899, 0x8A3F1505, 0x4A2B2466,
                         &At<uint32_t>(self, view::kMenuState20C));
        break;
    case 0xC998AF00:
        OpenMenu2A3858E4(self, nullptr, window, 0xC998AF00, 0x8A3F1502, 0x4A2B2466,
                         &At<uint32_t>(self, view::kMenuState21C));
        break;
    case 0xA998AF42:
        OpenMenu2A3858E4(self, nullptr, window, 0xA998AF42, 0x8A3F14DC, 0x4A2B2466,
                         &At<uint32_t>(self, view::kMenuState214));
        break;
    default:
        break;
    }
}

// --- The mode buttons and the naming dialog -------------------------------------------

// A key filter (0x1C bytes, 0x0076A3C0) for an edit box of `dialog`, held.
void* NewKeyFilter(void* dialog)
{
    char* const memory = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0x1C});
    void* const filter = memory ? This<void*>(addr::SC4UI_F76A3C0, memory, dialog) : nullptr;
    if (filter)
        AddRef(filter);
    return filter;
}

// The game's 0x007EB320 (thiscall, no arguments; the list entry 0x007EB360
// is inside it). The dialog that names a new city: the window 0x6A414973
// is loaded as the view's child and centred (a third down), its naming
// object (0x20 bytes, 0x0076A840) made, the two edit boxes prepared (the
// mayor's with the name the preferences remember), and the dialog run by
// the window manager's modal loop. When that returns 0 the chosen
// difficulty, the city's name and the mayor's name go to the city and the
// preferences, the preferences are saved and the city is established.
// Returns whether it was accepted. Objects are not checked for null where
// the original does not.
uint8_t __fastcall NameCity(char* self, void*)
{
    char* const settings = Virtual<char*>(Global<void*>(addr::SC4_Service43C94), 0x98);
    uint32_t key[3] = {0, 0x96A006B0, 0x2A41436B};
    void* const dialog = Cdecl<void*>(addr::SC4UI_LoadWindowChild, &key[0],
                                      static_cast<void*>(self ? self + view::kWindow : nullptr), 0x6A414973u);
    void* gen = nullptr;
    void* naming = nullptr;
    if (!dialog)
        return 0;

    Virtual<void>(City(), 0x64, At<uint32_t>(settings, prefs::kDifficulty));
    Virtual<void>(dialog, 0x5C);
    char* const window = self + view::kWindow;
    const int32_t height = Virtual<int32_t>(dialog, 0xA8);
    const int32_t y = (Virtual<int32_t>(window, 0xA8) - height) / 3;
    const int32_t width = Virtual<int32_t>(dialog, 0xA4);
    const int32_t x = (Virtual<int32_t>(window, 0xA4) - width) / 2;
    Virtual<void>(dialog, 0xE0, x, y);

    Clear(gen);
    if (Virtual<uint8_t>(dialog, 0x0, kWinGenIid, &gen)) {
        if (char* const memory = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0x20})) {
            if (void* const made = This<void*>(addr::SC4UI_F76A840, memory, gen)) {
                AddRef(made);
                naming = made;
            }
        }
        Virtual<void>(gen, 0x1C, naming);
    }

    // The original does not set this before the first question.
    void* edit = nullptr;
    if (Virtual<uint8_t>(dialog, 0x94, 0xAA4149BFu, kEditIid, &edit)) {
        Virtual<void>(edit, 0x130, uint32_t{0x800});
        void* const filter = NewKeyFilter(dialog);
        Virtual<void>(Virtual<void*>(edit, 0x14), 0x1D0, filter);
        Virtual<void>(edit, 0x84, uint32_t{0x80});
        const uint32_t length = Virtual<uint32_t>(edit, 0xBC);
        Virtual<void>(edit, 0x12C, uint32_t{0}, length);
        This<uint8_t>(addr::SC4Win_F99C9DF, window, Virtual<void*>(edit, 0x14));
        Release(edit);
        if (filter)
            Release(filter);
    }
    if (Virtual<uint8_t>(dialog, 0x94, 0xAA4149D2u, kEditIid, &edit)) {
        if (settings[prefs::kMayorName]) {
            GameString mayor;
            void* const made = This<void*>(addr::SC4String_FromText, &mayor, settings + prefs::kMayorName);
            Virtual<void>(edit, 0xC0, made, uint32_t{0});
            DestroyString(mayor);
        }
        Virtual<void>(edit, 0x130, uint32_t{0x800});
        void* const filter = NewKeyFilter(dialog);
        Virtual<void>(Virtual<void*>(edit, 0x14), 0x1D0, filter);
        Virtual<void>(edit, 0x84, uint32_t{0x80});
        Release(edit);
        WinMessage message;
        message.b = 0;
        message.c = 0;
        message.type = 0x9B1552D8;
        message.a = edit;
        Virtual<void>(Virtual<void*>(gen, 0xC), 0xC, &message);
        if (filter)
            Release(filter);
    }

    Virtual<void>(Global<void*>(addr::SC4_Service43CE0), 0x28, dialog, uint32_t{1});
    const uint32_t result = Virtual<uint32_t>(At<void*>(self, view::kManager), 0xA4, dialog);
    Virtual<void>(Global<void*>(addr::SC4_Service43CE0), 0x18, dialog);
    const uint8_t accepted = result == 0;
    if (accepted) {
        GameString text;
        InitString(text);
        const uint32_t difficulty = At<uint32_t>(naming, 0x18);
        Virtual<void>(City(), 0x64, difficulty);
        At<uint32_t>(settings, prefs::kDifficulty) = difficulty;

        // The original's holder here starts with the preferences' address.
        void* field = settings;
        if (Virtual<uint8_t>(dialog, 0x94, 0xAA4149BFu, kEditIid, &field)) {
            Virtual<void>(field, 0xB4, &text);
            Cdecl<uint8_t>(addr::SC4_F5F88D0, &text, uint32_t{0x1FF});
            const char* from = text.body.begin;
            char* to = settings + prefs::kCityName;
            char letter;
            do {
                letter = *from++;
                *to++ = letter;
            } while (letter);
            Virtual<void>(City(), 0x34, &text);
            Virtual<void>(City(), 0x3C, uint32_t{1});
            Release(field);
        }
        if (Virtual<uint8_t>(dialog, 0x94, 0xAA4149D2u, kEditIid, &field)) {
            Virtual<void>(field, 0xB4, &text);
            Cdecl<uint8_t>(addr::SC4_F5F88D0, &text, uint32_t{0x1FF});
            const char* from = text.body.begin;
            char* to = settings + prefs::kMayorName;
            char letter;
            do {
                letter = *from++;
                *to++ = letter;
            } while (letter);
            Virtual<void>(City(), 0x44, &text);
            Release(field);
        }

        void* const app = Global<void*>(addr::SC4_Service43C94);
        if (app)
            AddRef(app);
        Virtual<void>(app, 0x44);
        Virtual<void>(City(), 0x5C, uint32_t{1});
        Release(app);
        DestroyString(text);
    }

    This<uint8_t>(addr::SC4UI_F76A4E0, naming);
    Virtual<void>(At<void*>(self, view::kManager), 0x5C, dialog);
    This<void>(addr::SC4Win_F99BDE4, window);
    Release(dialog);
    if (naming)
        Release(naming);
    if (gen)
        Release(gen);
    return accepted;
}

// Asks the panel for a mode button and whether it is pressed.
uint8_t ModePressed(char* self, uint32_t id, void** button)
{
    if (!Virtual<uint8_t>(Panel(self), 0x94, id, kCheckIid, button))
        return 0;
    return Virtual<uint8_t>(*button, 0x10, uint32_t{1});
}

// The game's 0x007EC2D0 (thiscall, one argument; the list entry 0x007EC590
// is its jump table). A mode button was pressed. God: mode 0, or 4 once the
// city is established (0 again for a developer holding shift, control and
// alt). Mayor: mode 1; a city that is not established is named first, and
// when that is refused the button is let up and nothing changes. MySim:
// mode 2; the fourth button: mode 3. The secondary menu is closed; a
// pressed button leaves the old mode (0x007E8CA0, its button unchecked)
// and enters the new one (0x007E8B90); a button let up leaves its mode.
// The news ticker is told when one of its three bytes is set.
void __fastcall ModeButton(char* self, void*, uint32_t command)
{
    if (command != kGodCommand && command != kMayorCommand && command != kMySimCommand &&
        command != kFourthCommand)
        return;

    uint8_t pressed = 0;
    uint32_t mode;
    void* button = nullptr;
    if (command == kGodCommand) {
        mode = 0;
        pressed = ModePressed(self, kGodButton, &button);
        void* const keys = At<void*>(self, view::kKeys);
        const bool all = Virtual<uint8_t>(keys, 0xC, uint32_t{0x10}) &&
                         Virtual<uint8_t>(At<void*>(self, view::kKeys), 0xC, uint32_t{0x11}) &&
                         Virtual<uint8_t>(At<void*>(self, view::kKeys), 0xC, uint32_t{0x12});
        if (!(At<uint8_t>(self, view::kDeveloper) && all))
            mode = Virtual<uint8_t>(City(), 0x58) ? 4 : 0;
    } else if (command == kMayorCommand) {
        if (Virtual<uint8_t>(Panel(self), 0x94, kMayorButton, kCheckIid, &button)) {
            pressed = Virtual<uint8_t>(button, 0x10, uint32_t{1});
            if (pressed && !Virtual<uint8_t>(City(), 0x58) && !NameCity(self, nullptr)) {
                Virtual<void>(button, 0x14, uint32_t{1}, uint32_t{0});
                if (button)
                    Release(button);
                return;
            }
        }
        mode = 1;
    } else if (command == kMySimCommand) {
        pressed = ModePressed(self, kMySimButton, &button);
        mode = 2;
    } else {
        pressed = ModePressed(self, kFourthButton, This<void**>(addr::SC4RefPtr_Reset, &button));
        mode = 3;
    }
    if (button)
        Release(button);

    Virtual<void>(self, 0xEC);
    if (pressed) {
        const uint32_t current = At<uint32_t>(self, view::kMode);
        if (current != kNone) {
            This<void>(addr::SC4UI_F7E8CA0, self, current);
            uint32_t old = kNone;
            switch (At<uint32_t>(self, view::kMode)) {
            case 0:
            case 4:
                old = kGodButton;
                break;
            case 1:
                old = kMayorButton;
                break;
            case 2:
                old = kMySimButton;
                break;
            case 3:
                old = kFourthButton;
                break;
            default:
                break;
            }
            Cdecl<uint8_t>(addr::SC4Win_CheckChild, Panel(self), old, uint32_t{0});
        }
        At<uint32_t>(self, view::kMode) = mode;
        This<void>(addr::SC4UI_F7E8B90, self, mode);
    } else if (mode == At<uint32_t>(self, view::kMode)) {
        At<uint32_t>(self, view::kMode) = kNone;
        This<void>(addr::SC4UI_F7E8CA0, self, mode);
    }

    if (At<uint8_t>(self, view::kTickerByte2D5) || At<uint8_t>(self, view::kTickerByte2D4) ||
        At<uint8_t>(self, view::kTickerByte2D6))
        Virtual<void>(At<void*>(self, view::kTicker), 0x14);
}

// --- Tools ----------------------------------------------------------------------------

// The game's 0x007EBD70 (slot 25, SetCurrentViewInputControl; two
// arguments). The new tool gets the view's window; the change is announced
// (0x007E8090, with the tool in use and the new one); `operation` 1 takes
// the tool in use off, 2 all of them; the tool then on top is told the
// mouse left and that it is no longer in use; the new one, when its Init
// agrees, is held, put on top, told it is in use, and its cursor becomes
// the window's. Returns 1.
uint8_t __fastcall SetCurrentViewInputControl(char* self, void*, void* control, uint32_t operation)
{
    if (control)
        Virtual<void>(control, 0x28, static_cast<void*>(self ? self + view::kWindow : nullptr));
    void* top = Virtual<void*>(self, 0x60);
    if (top)
        AddRef(top);
    This<void>(addr::SC4UI_F7E8090, self, top, control);
    if (operation == 1)
        Virtual<void>(self, 0x68, uint32_t{0});
    else if (operation == 2)
        Virtual<void>(self, 0x6C, uint32_t{0});

    void* const now = Virtual<void*>(self, 0x60);
    if (now != top) {
        void* const before = top;
        if (now)
            AddRef(now);
        top = now;
        if (before)
            Release(before);
    }
    if (top) {
        Virtual<void>(top, 0x58);
        Virtual<void>(top, 0x60);
    }

    if (control && Virtual<uint8_t>(control, 0xC)) {
        AddRef(control);
        PushBack(At<PtrVector>(self, view::kControls), control);
        Virtual<void>(control, 0x5C);
        if (void* const cursor = Virtual<void*>(control, 0x1C))
            Virtual<void>(self + view::kWindow, 0x154, cursor, uint32_t{1});
    }
    if (top)
        Release(top);
    return 1;
}

// The game's 0x007EBEA0 (slot 28, AddPersistentViewInputControl; one
// argument; the list entry 0x007EBEC0 is inside it). A tool whose Init
// agrees gets the view's window, is held, added to the persistent tools and
// told it is in use. Returns 1 always.
uint8_t __fastcall AddPersistentViewInputControl(char* self, void*, void* control)
{
    if (control && Virtual<uint8_t>(control, 0xC)) {
        Virtual<void>(control, 0x28, static_cast<void*>(self ? self + view::kWindow : nullptr));
        AddRef(control);
        PushBack(At<PtrVector>(self, view::kPersistent), control);
        Virtual<void>(control, 0x5C);
    }
    return 1;
}

// --- The global effect triggers -------------------------------------------------------

// The game's 0x007EB850 (slot 46, GetGlobalEffectTriggerState; one
// argument): 1 when the trigger is in the table.
int32_t __fastcall GetGlobalEffectTriggerState(char* self, void*, uint32_t id)
{
    return FindTrigger(self + view::kTriggers, id) != nullptr;
}

// The game's 0x007EB8A0 (slot 48, RetractGlobalEffectTrigger; one
// argument). Counts the trigger down; the last count takes it out of the
// table and sends the message 0xAA375FE8 with the ID. What it returns is
// what the original leaves in EAX: the buckets' address when the trigger is
// not there, the new count, or the message's last count of references.
uint32_t __fastcall RetractGlobalEffectTrigger(char* self, void*, uint32_t id)
{
    char* const table = self + view::kTriggers;
    MapNode** const buckets = At<MapNode**>(table, 0x4);
    MapNode* const node = FindTrigger(table, id);
    if (!node)
        return static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(buckets));
    if (node->value > 1) {
        node->value -= 1;
        return node->value;
    }
    MapIterator at;
    at.node = node;
    at.table = table;
    This<void>(addr::SC4Uint32HashMap_EraseAt, table, &at);

    void* const server = Global<void*>(addr::SC4_MessageServer);
    void* const message = NewMessage();
    AddRef(message);
    Virtual<void>(message, 0x14, kTriggerRetracted);
    Virtual<void>(message, 0x2C, id);
    Virtual<void>(message, 0x40, uint32_t{0});
    Virtual<void>(server, 0xC, message);
    return Release(message);
}

// --- The mission panel ----------------------------------------------------------------

// The game's 0x007EB970 (slot 58, UpdateMissionStatusPanel; no arguments).
// The time left of the running mission as minutes and seconds in the
// panel's text 0x4C081A8E; in the last minute the text is red, and in the
// last twenty seconds red on odd seconds only. With no mission running the
// text is emptied.
void __fastcall UpdateMissionStatusPanel(char* self, void*)
{
    if (!At<void*>(self, view::kMission))
        return;
    GameString text;
    InitString(text);
    uint32_t colour = 0xFFFFFFFF;
    void* const situations = Virtual<void*>(City(), 0xC4);
    void* label = nullptr;
    if (Virtual<void*>(situations, 0x18)) {
        int32_t left = ToInt(Virtual<double>(situations, 0x3C));
        if (ToInt(Virtual<double>(situations, 0x34)) > 0) {
            const int32_t whole = left;
            if (left < 0)
                left = 0;
            left %= 3600;
            const int32_t minutes = left / 60;
            const int32_t seconds = left - minutes * 60;
            reinterpret_cast<SprintfFn>(addr::cRZBaseString_Sprintf)(&text, "%02.2d:%02.2d", minutes, seconds);
            if (whole < 60 && (whole > 20 || (whole & 1)))
                colour = 0xFFFF0000;
        }
    }
    void* const panel = At<void*>(self, view::kMission);
    Clear(label);
    if (Virtual<uint8_t>(panel, 0x94, 0x4C081A8Eu, kTextIid, &label)) {
        Virtual<void>(Virtual<void*>(label, 0xC), 0x128, &text);
        Virtual<void>(label, 0x20, uint32_t{0}, colour);
    }
    if (label)
        Release(label);
    DestroyString(text);
}

// The game's 0x007EBB20 (thiscall, two arguments). The mission panel's
// texts. With `show` 0 the title 0x8C087E51 becomes a fixed text; with both
// arguments set the running mission gives the title (its slot 5) and, for
// the step the situation manager is at, the goal 0x6C3CB626. The panel's
// inner window 0x0C3CB614 is shown and the outer one 0x10000005 sized down
// to its lower edge when there is a goal, else hidden and the outer one
// sized to its upper edge. The two windows are not checked for null.
void __fastcall SetMissionTexts(char* self, void*, uint32_t show, uint32_t second)
{
    if (!At<void*>(self, view::kMission))
        return;
    void* const situations = Virtual<void*>(City(), 0xC4);
    if (!situations)
        return;
    void* const mission = Virtual<void*>(situations, 0x18);
    void* const outer = Virtual<void*>(At<void*>(self, view::kMission), 0x88, 0x10000005u);
    if (outer)
        AddRef(outer);
    void* const inner = Virtual<void*>(outer, 0x88, 0x0C3CB614u);
    if (inner)
        AddRef(inner);
    GameString goal;
    InitString(goal);

    if (show == 0) {
        void* const panel = At<void*>(self, view::kMission);
        RoomyString title;
        void* const made = This<void*>(addr::SC4LocalizedString_SC4LocalizedString, &title, 0x6A231EA4u,
                                       0xAC2BD618u, uint32_t{1});
        Cdecl<uint8_t>(addr::SC4Win_SetChildText, panel, 0x8C087E51u, made);
        DestroyString(title.text);
    } else if (second != 0) {
        RoomyString title;
        This<void*>(addr::SC4String_SC4String, &title);
        if (Virtual<uint8_t>(mission, 0x14, &title)) {
            Cdecl<uint8_t>(addr::SC4Win_SetChildText, At<void*>(self, view::kMission), 0x8C087E51u, &title);
            const uint32_t step = Virtual<uint32_t>(situations, 0x28);
            void* const words = Virtual<void*>(mission, 0x60, step);
            Virtual<void>(&goal, 0x34, words);
            Cdecl<uint8_t>(addr::SC4Win_SetChildText, At<void*>(self, view::kMission), 0x6C3CB626u, &goal);
        }
        DestroyString(title.text);
    }

    if (goal.body.begin != goal.body.end) {
        Virtual<void>(outer, 0xD0, Virtual<int32_t>(inner, 0xB8) + 8);
        Virtual<void>(inner, 0x114);
    } else {
        Virtual<void>(outer, 0xD0, Virtual<int32_t>(inner, 0xB0) + 8);
        Virtual<void>(inner, 0x118);
    }
    DestroyString(goal);
    Release(inner);
    Release(outer);
}

// --- The cost indicator ---------------------------------------------------------------

// The game's 0x007EC800 (slot 42: SetCostIndicator with two numbers).
// Shows `cost`; it is too dear when `price` is not 0 and more than the
// city's funds. When anything changed the amount is drawn again (the
// game's 0x007EAC70), the picture's tint set (opaque when too dear, 0x96
// otherwise) and the indicator's window shown. Returns what the original
// leaves in EAX.
uint32_t __fastcall SetCostIndicator(char* self, void*, uint32_t cost, uint32_t price)
{
    int64_t funds = 0;
    if (void* const city = City()) {
        if (void* const budget = Virtual<void*>(city, 0xD0))
            funds = Virtual<int64_t>(budget, 0x18);
    }
    const uint32_t low = static_cast<uint32_t>(funds);
    const int32_t high = static_cast<int32_t>(funds >> 32);
    uint32_t kind = 0;
    if (price > 0 && !(0 < high) && (0 > high || price > low))
        kind = 1;

    At<uint32_t>(self, view::kCostTime) = 0;
    if (At<uint32_t>(self, view::kCostState) == 1 && At<uint32_t>(self, view::kCostKind) == kind &&
        At<uint32_t>(self, view::kCost) == cost)
        return cost;
    At<uint32_t>(self, view::kCostState) = 1;
    At<uint32_t>(self, view::kCostKind) = kind;
    At<uint32_t>(self, view::kCost) = cost;
    This<void>(addr::SC4UI_F7EAC70, self);

    void* tint = nullptr;
    uint32_t result = Virtual<uint32_t>(At<void*>(self, view::kCostPicture), 0x0, kTintIid, &tint);
    if (result & 0xFF) {
        // The original pushes a register whose low byte alone is set.
        const uint32_t alpha = kind != 1 ? 0x96 : 0xFF;
        Virtual<void>(tint, 0xC, uint32_t{0xFF}, uint32_t{0xFF}, uint32_t{0xFF}, alpha);
        result = Release(tint);
    }
    if (void* const indicator = At<void*>(self, view::kCostWindow))
        result = Virtual<uint32_t>(Virtual<void*>(indicator, 0xC), 0x114);
    return result;
}

// The game's 0x007EC900 (slot 44, ClearCostIndicator; one argument). With
// the flag the indicator starts to fade (state and kind 2, the point +0x3C0
// copied to +0x3C8, the time 1500) and is drawn again; without it it is
// switched off and its window hidden. Returns what the original leaves in
// EAX.
uint32_t __fastcall ClearCostIndicator(char* self, void*, uint32_t fade)
{
    const uint32_t on = static_cast<uint8_t>(fade) != 0;
    At<uint32_t>(self, view::kCostState) = on;
    if (on) {
        const uint32_t second = At<uint32_t>(self, view::kPoint3C0 + 4);
        At<uint32_t>(self, view::kCostKind) = 2;
        At<uint32_t>(self, view::kCostState) = 2;
        At<uint32_t>(self, view::kPoint3C8) = At<uint32_t>(self, view::kPoint3C0);
        At<uint32_t>(self, view::kPoint3C8 + 4) = second;
        At<float>(self, view::kCostTime) = 1500.0f;
        return This<uint32_t>(addr::SC4UI_F7EAC70, self);
    }
    At<uint32_t>(self, view::kCostTime) = 0;
    At<uint32_t>(self, view::kCostState) = 0;
    return Virtual<uint32_t>(Virtual<void*>(At<void*>(self, view::kCostWindow), 0xC), 0x118);
}

bool InstallCityView3(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[32] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F7EA5B0, reinterpret_cast<void*>(&Save)},
        {addr::SC4UI_F7EA6E0, reinterpret_cast<void*>(&UpdateDate)},
        {addr::SC4UI_F7EA810, reinterpret_cast<void*>(&UpdateClock)},
        {addr::SC4UI_F7EA970, reinterpret_cast<void*>(&UpdateFunds)},
        {addr::SC4UI_F7EAAA0, reinterpret_cast<void*>(&UpdatePopulation)},
        {addr::SC4UI_F7EAB60, reinterpret_cast<void*>(&UpdateCityName)},
        {addr::SC4UI_F7EABF0, reinterpret_cast<void*>(&ClearLoadingText)},
        {addr::SC4UI_F7EAEB0, reinterpret_cast<void*>(&OpenMenu)},
        {addr::SC4UI_F7EB320, reinterpret_cast<void*>(&NameCity)},
        {addr::SC4UI_F7EB850, reinterpret_cast<void*>(&GetGlobalEffectTriggerState)},
        {addr::SC4UI_F7EB8A0, reinterpret_cast<void*>(&RetractGlobalEffectTrigger)},
        {addr::SC4UI_F7EB970, reinterpret_cast<void*>(&UpdateMissionStatusPanel)},
        {addr::SC4UI_F7EBB20, reinterpret_cast<void*>(&SetMissionTexts)},
        {addr::SC4UI_F7EBD70, reinterpret_cast<void*>(&SetCurrentViewInputControl)},
        {addr::SC4UI_F7EBEA0, reinterpret_cast<void*>(&AddPersistentViewInputControl)},
        {addr::SC4UI_F7EBF30, reinterpret_cast<void*>(&CityReady)},
        {addr::SC4UI_F7EBFA0, reinterpret_cast<void*>(&Load)},
        {addr::SC4UI_F7EC0B0, reinterpret_cast<void*>(&ShowTime)},
        {addr::SC4UI_F7EC2D0, reinterpret_cast<void*>(&ModeButton)},
        {addr::SC4UI_F7EC620, reinterpret_cast<void*>(&OpenMenu6A01FC2A)},
        {addr::SC4UI_F7EC680, reinterpret_cast<void*>(&OpenMenuE83E0437)},
        {addr::SC4UI_F7EC6E0, reinterpret_cast<void*>(&OpenMenu2A3858E4)},
        {addr::SC4UI_F7EC740, reinterpret_cast<void*>(&RowButton)},
        {addr::SC4UI_F7EC800, reinterpret_cast<void*>(&SetCostIndicator)},
        {addr::SC4UI_F7EC900, reinterpret_cast<void*>(&ClearCostIndicator)},
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

const bool g_cityView3Registered = registry::Register({
    "ui.cityview.part3",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F7EAEB0,
    "Our city view texts, pop-up menus, mode buttons, tools and cost indicator setters (docs/ui.md)",
    false,
    InstallCityView3,
});

} // namespace
} // namespace opensc4::ui
