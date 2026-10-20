// The label and sign tools' part of the autotest (src/core/autotest_hooks.h;
// tool_decal.cpp, tool_signtext.cpp): drives the two tools and hashes what
// comes out, so that a run with ui.tools.decal and ui.tools.signtext on and
// one with them off can be compared. Only the game's addresses and vtable
// slots are called, never our own functions.
//
//   OPENSC4_TEST_TOOLS4=<day>    token "tools4", sub-hashes on the log lines
//     "autotest: tools4: ...". Everything here leaves the city as it was:
//
//   1. label: the label tool made as the view's command makes it
//      (constructor 0x004BD230 on 0x44 new bytes filled with 0x5A, the
//      view's slot 0x64 with 2): the fields after the constructor and after
//      Init; the filter's function over every prop and the first trees; the
//      window procedure with no window (the three buttons, another button,
//      another command, Enter, another key, an unknown message);
//      RoadPlacement and RailPlacement by address on every cell; LabelAt on
//      the grid's terrain points; the keys with no label; the handlers at a
//      point off the ground and over a grid of screen points. Then a label
//      of the test's own, added to the city: Place by address, SetLabel,
//      Home and End, LabelAt where it stands, a drag (state 1 set by hand,
//      OnMouseMove twice, OnMouseUpL), the hover over it, Delete, which
//      removes it. A second one is under the pointer when the view puts the
//      tool away (Shutdown), and is removed by the test.
//   2. sign: the sign tool (constructor 0x004D7F00 on 0x80 bytes, the view's
//      slot 0x64 with 2): the fields, QueryInterface, the filter, GroundHeight
//      by address on every fourth cell, the notice with nothing edited, the
//      keys and a character while idle, the handlers off the ground and over
//      the grid. Then signs that are gone again when the test ends:
//      a. OnMouseDownL on the ground: a new sign, edited. A text-less
//         dragged sign let go (state 2 by hand). Typing: 'A', 'b', the two
//         bytes of U+00E9 one at a time, Backspace, a tab, the three bytes
//         of U+20AC, a lead byte followed by a letter, Escape as a
//         character, Backspace until nothing is left and once more, 40
//         letters (33 stay), two more bytes, the notice (only that it is
//         answered: the point is the camera's), a key that is not Escape, V
//         without a modifier, AppendText by address with a line feed in the
//         text, Escape (the text the edit began with: none), Enter with no
//         text: the sign is removed.
//      b. A sign typed "D" and ended with Enter stays; SetSign by address
//         with it and Delete remove it.
//      c. A sign typed "Hi" and ended with a click: dragged (state 1 by
//         hand, OnMouseMove, OnMouseUpL), pressed and let go (edited again
//         from "Hi"), '!', Escape ("Hi" again), Backspace twice; the view
//         puts the tool away while it is edited with no text (Shutdown):
//         removed.
//
//   The labels and signs that come and go do so at terrain points under the
//   grid whose 64 m cell of the occupant manager is empty: the flammability
//   simulator recalculates where occupants come and go, and anywhere else
//   the original's daily fingerprint (its `flam` part) is no longer that of
//   a run without the test. A city with fewer than two such points under the
//   grid has no round trips without the commit (the first log line says).
//   OPENSC4_TEST_TOOLS4_PARTS: 1 label, 2 sign, 4 and 8 their round trips.
//
//   OPENSC4_TEST_TOOLS4_COMMIT=1 adds what changes the city (a tutorial city
//   may hold it back: OPENSC4_TEST_TUTORIAL_OFF=1). The text window of the
//   label tool runs under the modal stand-in (RunModalPlayer): the player
//   reads the edit field, sets its text, sends the tool's window procedure
//   the keyboard message with the real window and answers a button.
//
//   3. label: Place by address with the test's own label on every third
//      cell (what it sets on water, rail, road and open ground: the label
//      comes and goes all over the city); OnMouseDownL on the ground answered with cancel (no label
//      stays), with OK and no text (none stays), with OK and "Label" (it
//      stays); the same point pressed again and let go with the close
//      button (unchanged), then state 1 by hand and OnMouseUpL answered OK
//      with "Label 2". One label is left in the city.
//   4. sign (between b and c of step 2): OnMouseDownL on the ground, "Sign "
//      and U+00E9 typed, Enter. One sign is left in the city.
//
//   OPENSC4_TEST_TOOLS4_REMOVE=1 (with the commit) runs the sign removal
//   tool (tool_signpick.cpp, constructor 0x004D76F0) on a label and a sign
//   the test makes for it by hand at two terrain points: OnMouseMove at the
//   screen point (whether the tool finds it there), SetSign by address when
//   it did not, OnMouseUpL. What was removed is on the log line.
//
// Not reached: a sign picked on the screen (the renderer's pick reads what
// the last frame drew), so the press of OnMouseDownL and the hover of
// OnMouseMove finding a sign; the paste (the clipboard is not the run's);
// the single-byte encodings; the window procedure ending a real modal run.
// Kept out of the hashes: every pointer (only whether it is set), the
// notice's screen point, the sign's caret, the sounds.
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <GZServPtrs.h>
#include <cIGZWin.h>
#include <cISC4App.h>
#include <cISC4City.h>
#include <cISC4View3DWin.h>
#include <cRZAutoRefCount.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

namespace opensc4::autotest {
namespace {

struct Fnv {
    uint32_t h = 2166136261u;
    void Bytes(const void* data, std::size_t size)
    {
        for (std::size_t i = 0; i < size; ++i)
            h = (h ^ static_cast<const uint8_t*>(data)[i]) * 16777619u;
    }
    template <typename T>
    void Add(const T& value)
    {
        Bytes(&value, sizeof value);
    }
    void Text(const std::string& text)
    {
        Add(static_cast<uint32_t>(text.size()));
        Bytes(text.data(), text.size());
    }
};

char g_token[40];

namespace field { // the base class (tool_base.cpp)
constexpr std::size_t kInited = 0x04, kCount = 0x08, kId = 0x0C, kCursorId = 0x10, kCursor = 0x14;
constexpr std::size_t kWindow = 0x18, kView = 0x1C;
} // namespace field

namespace label { // tool_decal.cpp
constexpr std::size_t kProc = 0x28, kState = 0x2C, kLabel = 0x30, kGrab = 0x34, kDirection = 0x40, kSize = 0x44;
} // namespace label

namespace decal { // the text decal, from its label interface
constexpr std::size_t kSize = 0x80, kMoving = 0x8C, kColour = 0x9C, kLevel = 0xA8, kDirection = 0xAC, kBytes = 0xBC;
} // namespace decal

namespace sign { // tool_signtext.cpp
constexpr std::size_t kTarget = 0x28, kState = 0x2C, kSign = 0x30, kGrabX = 0x34, kGrabZ = 0x38, kText = 0x3C;
constexpr std::size_t kSaved = 0x50, kEncoding = 0x64, kPending = 0x68, kClipboard = 0x7C, kSize = 0x80;
} // namespace sign

namespace remover { // tool_signpick.cpp
constexpr std::size_t kSign = 0x2C, kSize = 0x30;
} // namespace remover

constexpr uint32_t kTypeProp = 0xA823821E, kTypeFlora = 0x74758926;
constexpr uint32_t kIidProp = 0xE9793A65, kIidLabel = 0x2B3B7D86, kIidSign = 0x4B44FBE2, kIidTarget = 0xE98B2F57;
constexpr uint32_t kKindSign = 0xAB72FBB3, kKindLabel = 0xCB79919B;
constexpr uint32_t kTextEdit = 0xAA4149BF, kEditIid = 0x231A1C57, kClicked = 0x287259F6;
constexpr uint32_t kOkButton = 0x8A8DFD4E, kCancelButton = 0x8A8DFD4A, kCloseButton = 0x6BA18178;
constexpr uint32_t kNoticePlace = 0x0BE10A20;
constexpr int32_t kOff = -1000; // a screen point that picks nothing

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

// The city view, as tool_bulldoze_autotest.cpp finds it.
char* CityView()
{
    constexpr uint32_t kWinSC4App = 0x6104489A;
    constexpr uint32_t kWinView3D = 0x9A47B417;
    cISC4AppPtr app;
    cIGZWin* const main = app ? app->GetMainWindow() : nullptr;
    cIGZWin* const parent = main ? main->GetChildWindowFromID(kWinSC4App) : nullptr;
    cRZAutoRefCount<cISC4View3DWin> view;
    if (parent)
        parent->GetChildAs(kWinView3D, kGZIID_cISC4View3DWin, view.AsPPVoid());
    return reinterpret_cast<char*>(static_cast<cISC4View3DWin*>(view));
}

// New memory filled with 0x5A, so that what a constructor leaves alone shows.
char* NewBlock(std::size_t size)
{
    char* const block = static_cast<char*>(Cdecl<void*>(addr::SC4_operator_new, static_cast<uint32_t>(size)));
    if (block)
        std::memset(block, 0x5A, size);
    return block;
}

// A game string (0x14 bytes) made and freed as the game does.
struct OwnString {
    char bytes[0x14];
    OwnString() { This<void*>(addr::SC4String_SC4String, &bytes[0]); }
    explicit OwnString(const char* text) { This<void*>(addr::SC4String_FromText, &bytes[0], text); }
    ~OwnString()
    {
        if (void* const buffer = At<void*>(&bytes[0], 0x4))
            Cdecl<void>(addr::SC4_Free, buffer);
    }
    OwnString(const OwnString&) = delete;
    OwnString& operator=(const OwnString&) = delete;
    void* Pointer() { return &bytes[0]; }
    std::string Text()
    {
        const char* const begin = At<const char*>(&bytes[0], 0x4);
        const char* const end = At<const char*>(&bytes[0], 0x8);
        return begin && end >= begin ? std::string(begin, end) : std::string("?");
    }
};

// The text of a game string inside an object.
std::string Body(char* object, std::size_t offset)
{
    const char* const begin = At<const char*>(object, offset + 4);
    const char* const end = At<const char*>(object, offset + 8);
    return begin && end >= begin ? std::string(begin, end) : std::string("?");
}

// A label's or a sign's text (slot 5 of either interface).
std::string TextOf(void* object)
{
    OwnString text;
    Virtual<void>(object, 0x14, text.Pointer());
    return text.Text();
}

// Bytes as hexadecimal, for the log.
std::string Hex(const std::string& text)
{
    std::string out;
    char two[4];
    for (const char c : text) {
        std::snprintf(two, sizeof two, "%02X", static_cast<unsigned>(static_cast<uint8_t>(c)));
        out += two;
    }
    return out;
}

// The base fields of a tool: the plain ones, and of the pointers whether
// they are set.
void HashTool(Fnv& f, char* tool)
{
    f.Add(At<uint8_t>(tool, field::kInited));
    f.Add(At<uint32_t>(tool, field::kCount));
    f.Add(At<uint32_t>(tool, field::kId));
    f.Add(At<uint32_t>(tool, field::kCursorId));
    for (const std::size_t offset : {field::kCursor, field::kWindow, field::kView})
        f.Add(uint8_t{At<void*>(tool, offset) != nullptr});
}

// The handlers: (x, z, modifiers) in the view window's pixels.
uint8_t MouseDownL(char* tool, int32_t x, int32_t z)
{
    return Virtual<uint8_t>(tool, 0x40, x, z, uint32_t{0});
}

uint8_t MouseUpL(char* tool, int32_t x, int32_t z)
{
    return Virtual<uint8_t>(tool, 0x48, x, z, uint32_t{0});
}

uint8_t MouseMove(char* tool, int32_t x, int32_t z)
{
    return Virtual<uint8_t>(tool, 0x50, x, z, uint32_t{0});
}

uint8_t KeyDown(char* tool, uint32_t key, uint32_t modifiers = 0)
{
    return Virtual<uint8_t>(tool, 0x38, key, modifiers);
}

uint8_t Character(char* tool, uint32_t character)
{
    return Virtual<uint8_t>(tool, 0x34, character);
}

// A tool given to the view as its makers do: held, the view's slot 0x64.
// The reference stays ours until the caller lets go of it.
bool GiveToView(char* view, char* tool, uint32_t operation)
{
    Virtual<uint32_t>(tool, 0x4);
    Virtual<uint8_t>(view, 0x64, static_cast<void*>(tool), operation);
    return Virtual<void*>(view, 0x60) == tool;
}

// The screen points the tests try, row by row.
struct Point {
    int32_t x, z;
};

template <typename Fn>
void ForGrid(Fn&& fn)
{
    for (int32_t z = 60; z <= 700; z += 40)
        for (int32_t x = 40; x <= 980; x += 60)
            fn(Point{x, z});
}

// The terrain point under a screen point, as both tools ask for it (the
// renderer's PickTerrain, slot 0xF0, with 0 and 1).
bool PickGround(int32_t x, int32_t z, float* point)
{
    point[0] = point[1] = point[2] = 0.0f;
    return Virtual<uint8_t>(Global<void*>(addr::SC4_View43DD0), 0xF0, x, z, point, uint32_t{0}, uint32_t{1}) != 0;
}

// A grid point with ground under it inside the city.
struct Ground {
    Point at{};
    float point[3] = {};
};

// Whether the occupant manager's 64 m cell around a place is empty (its slot
// 25, as propmgr_autotest.cpp asks): the flammability simulator recalculates
// where occupants come and go, so only there does a round trip leave the
// city's daily fingerprint alone.
bool Quiet(void* city, const float* point)
{
    void* const occupants = Virtual<void*>(city, 0x88);
    const int32_t x = static_cast<int32_t>(point[0]) / 16, z = static_cast<int32_t>(point[2]) / 16;
    return occupants && (Virtual<uint8_t>(occupants, 0x64, x / 4 + 64, z / 4 + 64) & 1) != 0;
}

std::vector<Ground> GroundPoints(cISC4City* city)
{
    std::vector<Ground> found;
    const float sizeX = static_cast<float>(city->CellCountX()) * 16.0f;
    const float sizeZ = static_cast<float>(city->CellCountZ()) * 16.0f;
    ForGrid([&](Point p) {
        Ground g;
        g.at = p;
        if (PickGround(p.x, p.z, g.point) && g.point[0] >= 16.0f && g.point[2] >= 16.0f && g.point[0] < sizeX - 16.0f &&
            g.point[2] < sizeZ - 16.0f)
            found.push_back(g);
    });
    return found;
}

// Every occupant of a type (the occupant manager's list by type, as
// flora_autotest.cpp walks it).
template <typename Fn>
void ForOccupants(void* city, uint32_t type, Fn&& fn)
{
    void* const occupants = Virtual<void*>(city, 0x88);
    if (!occupants)
        return;
    void** const sentinel = static_cast<void**>(Virtual<void*>(ListNodeAllocator(), 0xC, 0xCu));
    sentinel[0] = sentinel;
    sentinel[1] = sentinel;
    void* head = sentinel;
    Virtual<uint8_t>(occupants, 0x9C, &head, 0u, 0u, type, -1);
    for (void** node = static_cast<void**>(sentinel[0]); node != sentinel; node = static_cast<void**>(node[0]))
        fn(node[2]);
    Virtual<uint8_t>(occupants, 0xC0, &head);
    reinterpret_cast<void(__thiscall*)(void*)>(addr::SC4List_Destroy2)(&head);
}

// The city's labels and signs: how many of each, and a hash of their kinds,
// places and texts in a fixed order.
struct Marks {
    int32_t props = 0, labels = 0, signs = 0;
    uint32_t hash = 0;
};

Marks CityMarks(void* city)
{
    struct Mark {
        uint32_t kind = 0;
        std::array<uint32_t, 3> place{};
        std::string text;
        bool operator<(const Mark& other) const
        {
            if (kind != other.kind)
                return kind < other.kind;
            return place != other.place ? place < other.place : text < other.text;
        }
    };
    Marks marks;
    std::vector<Mark> found;
    ForOccupants(city, kTypeProp, [&](void* occupant) {
        ++marks.props;
        void* prop = nullptr;
        if (!Virtual<uint8_t>(occupant, 0x0, kIidProp, &prop) || !prop)
            return;
        const uint32_t kind = Virtual<uint32_t>(prop, 0x1C);
        Virtual<uint32_t>(prop, 0x8);
        if (kind != kKindSign && kind != kKindLabel)
            return;
        Mark mark;
        mark.kind = kind;
        Virtual<void>(occupant, 0x20, &mark.place[0]);
        void* part = nullptr;
        if (Virtual<uint8_t>(occupant, 0x0, kind == kKindLabel ? kIidLabel : kIidSign, &part) && part) {
            mark.text = TextOf(part);
            Virtual<uint32_t>(part, 0x8);
        }
        ++(kind == kKindLabel ? marks.labels : marks.signs);
        found.push_back(mark);
    });
    std::sort(found.begin(), found.end());
    Fnv f;
    for (const Mark& mark : found) {
        f.Add(mark.kind);
        f.Add(mark.place);
        f.Text(mark.text);
    }
    marks.hash = f.h;
    return marks;
}

// Whether the city's signs and labels are shown (the prop developer's slot
// 10), and put back to what they were (its slot 8).
uint8_t Shown()
{
    return Virtual<uint8_t>(Global<void*>(addr::SC4_Service43DB8), 0x28);
}

void ShowAgain(uint8_t shown)
{
    if (Shown() != shown)
        Virtual<void>(Global<void*>(addr::SC4_Service43DB8), 0x20, uint32_t{shown});
}

void* PropManager()
{
    return Global<void*>(addr::cSC4PropManager_Global);
}

// --- 1 and 3. The label tool ---------------------------------------------------------------

void HashLabelTool(Fnv& f, char* tool)
{
    HashTool(f, tool);
    f.Add(At<int32_t>(tool, label::kState));
    f.Add(uint8_t{At<void*>(tool, label::kLabel) != nullptr});
    for (std::size_t i = 0; i < 3; ++i)
        f.Add(At<uint32_t>(tool, label::kGrab + i * 4));
    f.Add(At<int32_t>(tool, label::kDirection));
}

// A label: what Place and the tool set, its place and its text.
void HashDecal(Fnv& f, void* decal)
{
    f.Add(At<uint32_t>(decal, decal::kSize));
    f.Add(At<uint8_t>(decal, decal::kMoving));
    for (std::size_t i = 0; i < 3; ++i)
        f.Add(At<uint32_t>(decal, decal::kColour + i * 4));
    f.Add(At<uint32_t>(decal, decal::kLevel));
    f.Add(At<int32_t>(decal, decal::kDirection));
    uint32_t place[3] = {};
    Virtual<void>(Virtual<void*>(decal, 0xC), 0x20, &place[0]);
    f.Add(place);
    f.Text(TextOf(decal));
}

// A new label as OnMouseDownL makes one: from its pool, referenced, added to
// the city; but put at its place first (the tool adds it at the city's
// corner and moves it), so that no occupant comes or goes anywhere else.
// Null when the pool gives nothing.
void* NewDecal(const float* point)
{
    void* const memory = Cdecl<void*>(addr::cSC4PropOccupantTextDecal_Allocate, std::size_t{decal::kBytes});
    void* const made = memory ? This<void*>(addr::cSC4PropOccupantTextDecal_Construct, memory) : nullptr;
    if (!made)
        return nullptr;
    Virtual<uint32_t>(made, 0x4);
    float place[3] = {point[0], point[1], point[2]};
    Virtual<void>(Virtual<void*>(made, 0xC), 0x24, &place[0]);
    Virtual<uint8_t>(PropManager(), 0x4C, Virtual<void*>(made, 0xC));
    return made;
}

// The window procedure of the part at +0x28 (its slot 3).
uint8_t Proc(char* tool, void* window, uint32_t type, uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t message[4] = {type, a, b, c};
    return Virtual<uint8_t>(static_cast<void*>(tool + label::kProc), 0xC, window, &message[0]);
}

// The player of the text window: what it finds in the edit field, the text
// it sets, the answer it gives.
struct Play {
    char* tool = nullptr;
    uint32_t answer = 0;
    const char* text = nullptr; // null: the field is left as it is
    int32_t calls = 0;
    Fnv seen;
    std::string last;
};
Play g_play;

uint32_t PlayText(void* window, int depth)
{
    ++g_play.calls;
    g_play.seen.Add(depth);
    g_play.seen.Add(uint8_t{window != nullptr});
    if (!window)
        return g_play.answer;
    void* edit = nullptr;
    const uint8_t has = Virtual<uint8_t>(window, 0x94, kTextEdit, kEditIid, &edit);
    g_play.seen.Add(has);
    if (has && edit) {
        OwnString before;
        Virtual<void>(edit, 0xB4, before.Pointer());
        g_play.last = before.Text();
        g_play.seen.Text(g_play.last);
        if (g_play.text) {
            OwnString text(g_play.text);
            Virtual<void>(edit, 0xC0, text.Pointer(), uint32_t{0});
        }
        Virtual<uint32_t>(edit, 0x8);
    }
    // The keyboard message with the real window: the field gets the keys.
    g_play.seen.Add(Proc(g_play.tool, window, 0x11, 0, 0, 0));
    g_play.seen.Add(Proc(g_play.tool, window, 3, kClicked, 0x12345678, 0));
    g_play.seen.Add(Proc(g_play.tool, window, 0x12345, 0, 0, 0));
    return g_play.answer;
}

struct Click {
    char* tool;
    Point at;
    bool up;
    uint8_t answer;
};

void RunClick(void* context)
{
    Click* const click = static_cast<Click*>(context);
    click->answer = click->up ? MouseUpL(click->tool, click->at.x, click->at.z)
                              : MouseDownL(click->tool, click->at.x, click->at.z);
}

// A click of the label tool with the text window played: the handler's
// answer, 0xEE without a window manager.
uint8_t PlayedClick(char* tool, Point at, bool up, uint32_t answer, const char* text)
{
    g_play.tool = tool;
    g_play.answer = answer;
    g_play.text = text;
    Click click{tool, at, up, 0xEE};
    if (!RunModalPlayer(&PlayText, &RunClick, &click))
        return 0xEE;
    return click.answer;
}

uint32_t DriveLabel(char* view, cISC4City* cityObject, void* city, const std::vector<Ground>& ground,
                    const std::vector<Ground>& spots, bool commit)
{
    Fnv f;
    const bool trip = spots.size() >= 2;
    const Ground nowhere{};
    const Ground& a = trip ? spots[(spots.size() - 1) / 2] : nowhere;
    const Ground& b = trip ? spots[(spots.size() - 1) / 2 + 1] : nowhere;
    const uint8_t shown = Shown();
    const Marks before = CityMarks(city);
    f.Add(uint8_t{Virtual<void*>(view, 0x60) != nullptr});
    f.Add(before.labels);
    f.Add(before.signs);
    f.Add(before.hash);
    char* const tool = NewBlock(label::kSize);
    if (!tool || !This<void*>(addr::SC4LabelTool_Construct, tool))
        return 0;
    HashLabelTool(f, tool);
    f.Add(uint8_t{At<std::uintptr_t>(tool, 0) == addr::SC4LabelTool_Vtable});
    f.Add(uint8_t{At<std::uintptr_t>(tool, label::kProc) == addr::SC4LabelTool_ProcVtable});
    if (!GiveToView(view, tool, 2)) {
        log::Write("autotest: tools4: the view did not take the label tool");
        return 0;
    }
    HashLabelTool(f, tool);
    const uint8_t shownInTool = Shown();
    f.Add(shownInTool);
    const uint32_t made = f.h;

    // The filter's function (it does not read `this`).
    int32_t taken = 0, trees = 0;
    ForOccupants(city, kTypeProp, [&](void* occupant) {
        taken += This<uint8_t>(addr::SC4LabelTool_FilterIncludes, static_cast<void*>(nullptr), occupant);
    });
    ForOccupants(city, kTypeFlora, [&](void* occupant) {
        if (trees++ < 20)
            f.Add(This<uint8_t>(addr::SC4LabelTool_FilterIncludes, static_cast<void*>(nullptr), occupant));
    });
    f.Add(taken);

    // The window procedure without a window.
    uint32_t record[2] = {0, 0x0D};
    const uint32_t enter = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(&record[0]));
    for (const uint32_t button : {kOkButton, kCancelButton, kCloseButton, 0x12345678u})
        f.Add(Proc(tool, nullptr, 3, kClicked, button, 0));
    f.Add(Proc(tool, nullptr, 3, 0x12345678, kOkButton, 0));
    f.Add(Proc(tool, nullptr, 0x1A, 0, 4, enter));
    record[1] = 0x41;
    f.Add(Proc(tool, nullptr, 0x1A, 0, 4, enter));
    f.Add(Proc(tool, nullptr, 0x1A, 0, 3, 0));
    f.Add(Proc(tool, nullptr, 0x12345, 0, 0, 0));
    HashLabelTool(f, tool);
    const uint32_t proc = f.h;

    // Where a label goes on a road and on rail, cell by cell.
    const int32_t cellsX = static_cast<int32_t>(cityObject->CellCountX());
    const int32_t cellsZ = static_cast<int32_t>(cityObject->CellCountZ());
    int32_t roadTurned = 0, railTurned = 0, moved = 0;
    for (int32_t z = 0; z < cellsZ; ++z)
        for (int32_t x = 0; x < cellsX; ++x) {
            const float centre[3] = {static_cast<float>(x) * 16.0f + 5.0f, 1.0f, static_cast<float>(z) * 16.0f + 11.0f};
            float position[3] = {centre[0], centre[1], centre[2]};
            int32_t direction = 9;
            This<void>(addr::SC4LabelTool_RoadPlacement, tool, &position[0], &direction);
            f.Add(position);
            f.Add(direction);
            roadTurned += direction != 1;
            moved += position[0] != static_cast<float>(x) * 16.0f + 8.0f ||
                     position[2] != static_cast<float>(z) * 16.0f + 8.0f;
            float onRail[3] = {centre[0], centre[1], centre[2]};
            direction = 9;
            This<void>(addr::SC4LabelTool_RailPlacement, tool, &onRail[0], &direction);
            f.Add(onRail);
            f.Add(direction);
            railTurned += direction != 1;
        }
    const uint32_t placement = f.h;

    // No label anywhere yet (a city that has some shows them here).
    int32_t standing = 0;
    for (const Ground& g : ground) {
        float point[3] = {g.point[0], g.point[1], g.point[2]};
        const uint8_t found = This<void*>(addr::SC4LabelTool_LabelAt, tool, &point[0]) != nullptr;
        standing += found;
        f.Add(found);
    }

    // The keys and the handlers with no label.
    const auto step = [&](uint8_t answer) {
        f.Add(answer);
        HashLabelTool(f, tool);
    };
    for (const uint32_t key : {0x2Eu, 0x24u, 0x23u, 0x41u})
        step(KeyDown(tool, key));
    step(MouseMove(tool, kOff, kOff));
    step(MouseDownL(tool, kOff, kOff));
    step(MouseUpL(tool, kOff, kOff));
    int32_t hovered = 0;
    ForGrid([&](Point p) {
        f.Add(MouseMove(tool, p.x, p.z));
        const uint8_t over = At<void*>(tool, label::kLabel) != nullptr;
        hovered += over;
        f.Add(over);
    });
    This<void>(addr::SC4LabelTool_SetLabel, tool, static_cast<void*>(nullptr));
    HashLabelTool(f, tool);
    const uint32_t idle = f.h;

    // A label of the test's own, in the city while the test uses it.
    int32_t onWater = 0, onNetwork = 0, onGround = 0;
    uint8_t deleted = 0xEE, foundWhereItStands = 0xEE, hoverFound = 0xEE;
    uint32_t tripHash = 0;
    if (void* const own = trip ? NewDecal(a.point) : nullptr) {
        HashDecal(f, own);
        // Every third cell, with the commit only: the label comes and goes
        // all over the city.
        void* const terrain = Global<void*>(addr::SC4_Terrain);
        for (int32_t z = 0; commit && z < cellsZ; z += 3)
            for (int32_t x = 0; x < cellsX; x += 3) {
                const float px = static_cast<float>(x) * 16.0f + 5.0f, pz = static_cast<float>(z) * 16.0f + 11.0f;
                const float py = Virtual<float>(terrain, 0x34, px, pz);
                This<void>(addr::SC4LabelTool_Place, tool, px, py, pz, own);
                HashDecal(f, own);
                const uint32_t red = At<uint32_t>(own, decal::kColour), blue = At<uint32_t>(own, decal::kColour + 8);
                ++(red == 0x3F800000 ? onNetwork : blue == 0x3F800000 ? onWater : onGround);
            }
        This<void>(addr::SC4LabelTool_Place, tool, 1.0f, 2.0f, 3.0f, static_cast<void*>(nullptr));
        This<void>(addr::SC4LabelTool_Place, tool, a.point[0], a.point[1], a.point[2], own);
        HashDecal(f, own);
        float stands[3] = {};
        Virtual<void>(Virtual<void*>(own, 0xC), 0x20, &stands[0]);
        foundWhereItStands = This<void*>(addr::SC4LabelTool_LabelAt, tool, &stands[0]) == own;
        f.Add(foundWhereItStands);

        This<void>(addr::SC4LabelTool_SetLabel, tool, own);
        HashLabelTool(f, tool);
        HashDecal(f, own);
        This<void>(addr::SC4LabelTool_SetLabel, tool, own); // the same again: nothing
        HashDecal(f, own);
        for (const uint32_t key : {0x24u, 0x24u, 0x23u, 0x23u, 0x23u, 0x24u, 0x24u, 0x24u, 0x24u, 0x41u}) {
            step(KeyDown(tool, key));
            HashDecal(f, own);
        }
        // The drag: pressed by hand, moved twice, let go.
        At<int32_t>(tool, label::kState) = 1;
        step(MouseMove(tool, b.at.x, b.at.z));
        HashDecal(f, own);
        step(KeyDown(tool, 0x2E)); // not while a button is down
        step(MouseMove(tool, a.at.x, a.at.z));
        HashDecal(f, own);
        step(MouseMove(tool, kOff, kOff));
        HashDecal(f, own);
        step(MouseUpL(tool, a.at.x, a.at.z));
        HashDecal(f, own);
        // The hover: none off the ground, then the label where it was put.
        step(MouseMove(tool, kOff, kOff));
        HashDecal(f, own);
        step(MouseMove(tool, a.at.x, a.at.z));
        hoverFound = At<void*>(tool, label::kLabel) == own;
        f.Add(hoverFound);
        HashDecal(f, own);
        if (!hoverFound)
            This<void>(addr::SC4LabelTool_SetLabel, tool, own);
        deleted = KeyDown(tool, 0x2E);
        step(deleted);
        HashDecal(f, own);
        const Marks after = CityMarks(city);
        f.Add(after.labels);
        f.Add(after.hash);
        f.Add(Virtual<uint32_t>(own, 0x8)); // the test's reference: the last one
        tripHash = f.h;
    }

    // What changes the city.
    uint8_t cancelled = 0xEE, emptied = 0xEE, kept = 0xEE, pressed = 0xEE, closed = 0xEE, edited = 0xEE;
    int32_t afterCancel = -1, afterEmpty = -1, afterKeep = -1;
    std::string finalText = "-", seenAtEdit = "-";
    if (commit && ground.size() >= 8) {
        g_play = Play{};
        const Ground& at = ground[ground.size() * 2 / 3];
        cancelled = PlayedClick(tool, at.at, false, kCancelButton, "No");
        step(cancelled);
        afterCancel = CityMarks(city).labels;
        f.Add(afterCancel);
        emptied = PlayedClick(tool, at.at, false, kOkButton, "");
        step(emptied);
        afterEmpty = CityMarks(city).labels;
        f.Add(afterEmpty);
        kept = PlayedClick(tool, at.at, false, kOkButton, "Label");
        step(kept);
        afterKeep = CityMarks(city).labels;
        f.Add(afterKeep);
        void* const stays = At<void*>(tool, label::kLabel);
        if (stays) {
            Virtual<uint32_t>(stays, 0x4);
            HashDecal(f, stays);
            // The same point pressed: the label there, or (when the occupant
            // manager does not find it at the point) a new one, cancelled.
            pressed = PlayedClick(tool, at.at, false, kCancelButton, nullptr);
            step(pressed);
            if (At<int32_t>(tool, label::kState) == 1) {
                closed = PlayedClick(tool, at.at, true, kCloseButton, "Not kept");
                step(closed);
                HashDecal(f, stays);
            }
            // Pressed by hand, let go, a new text given.
            This<void>(addr::SC4LabelTool_SetLabel, tool, stays);
            At<int32_t>(tool, label::kState) = 1;
            edited = PlayedClick(tool, at.at, true, kOkButton, "Label 2");
            step(edited);
            seenAtEdit = g_play.last;
            HashDecal(f, stays);
            finalText = TextOf(stays);
            Virtual<uint32_t>(stays, 0x8);
        }
        f.Add(g_play.calls);
        f.Add(g_play.seen.h);
    }
    const uint32_t committed = f.h;

    // The view puts the tool away with a label under the pointer (the
    // committed one, or one of the test's own, removed after).
    void* last = nullptr;
    if (!At<void*>(tool, label::kLabel) && trip) {
        last = NewDecal(a.point);
        if (last) {
            This<void>(addr::SC4LabelTool_Place, tool, a.point[0], a.point[1], a.point[2], last);
            This<void>(addr::SC4LabelTool_SetLabel, tool, last);
            HashDecal(f, last);
        }
    }
    void* const lit = At<void*>(tool, label::kLabel);
    if (lit)
        Virtual<uint32_t>(lit, 0x4);
    f.Add(Virtual<uint8_t>(view, 0x68, uint32_t{0}));
    const bool away = Virtual<void*>(view, 0x60) == nullptr;
    f.Add(uint8_t{away});
    HashLabelTool(f, tool);
    if (lit) {
        HashDecal(f, lit);
        Virtual<uint32_t>(lit, 0x8);
    }
    if (last) {
        Virtual<uint8_t>(PropManager(), 0x74, Virtual<void*>(last, 0xC));
        f.Add(Virtual<uint32_t>(last, 0x8));
    }
    const uint32_t lastRelease = Virtual<uint32_t>(tool, 0x8);
    f.Add(lastRelease);
    ShowAgain(shown);
    const Marks end = CityMarks(city);
    f.Add(end.labels);
    f.Add(end.signs);
    f.Add(end.hash);
    log::Write("autotest: tools4: label %08X (made %08X, signs and labels shown %u, in the tool %u, put back to %u; "
               "%d props, %d taken by the filter; procedure %08X; placement %08X over %d x %d cells: %d moved by the "
               "road one, %d turned by it, %d by the rail one; %d of %d terrain points with a label; idle %08X, %d "
               "grid points over a label)",
               f.h, made, shown, shownInTool, Shown(), before.props, taken, proc, placement, cellsX, cellsZ, moved,
               roadTurned, railTurned, standing, static_cast<int32_t>(ground.size()), idle, hovered);
    log::Write("autotest: tools4: label round trip %08X (placed on water %d, on a network %d, on open ground %d; found "
               "where it stands %u, by the hover %u, Delete answered %u); commit %d %08X (cancel answered %u: %d "
               "labels, no text %u: %d, text %u: %d, pressed again %u, closed %u, edited %u from '%s' to '%s', the "
               "window ran %d times, seen %08X); away %d, last release %u; labels %d to %d, signs %d to %d, marks "
               "%08X to %08X",
               tripHash, onWater, onNetwork, onGround, foundWhereItStands, hoverFound, deleted, commit, committed,
               cancelled, afterCancel, emptied, afterEmpty, kept, afterKeep, pressed, closed, edited,
               seenAtEdit.c_str(), finalText.c_str(), g_play.calls, g_play.seen.h, away, lastRelease, before.labels,
               end.labels, before.signs, end.signs, before.hash, end.hash);
    return f.h;
}

// --- 2 and 4. The sign tool ----------------------------------------------------------------

// The tool's fields and, of the sign it has, the text and the place.
void HashSignTool(Fnv& f, char* tool)
{
    HashTool(f, tool);
    f.Add(At<int32_t>(tool, sign::kState));
    void* const held = At<void*>(tool, sign::kSign);
    f.Add(uint8_t{held != nullptr});
    f.Add(At<uint32_t>(tool, sign::kGrabX));
    f.Add(At<uint32_t>(tool, sign::kGrabZ));
    f.Text(Body(tool, sign::kText));
    f.Text(Body(tool, sign::kSaved));
    f.Text(Body(tool, sign::kPending));
    f.Add(At<uint32_t>(tool, sign::kEncoding));
    f.Add(uint8_t{At<void*>(tool, sign::kClipboard) != nullptr});
    if (held) {
        f.Text(TextOf(held));
        uint32_t place[3] = {};
        Virtual<void>(Virtual<void*>(held, 0xC), 0x20, &place[0]);
        f.Add(place);
    }
}

// The notice of the part at +0x28 (its slot 3): the answer and whether the
// notice was marked answered; the screen point only on the log.
uint8_t Notice(char* tool, uint32_t number, uint32_t* answered, uint32_t* point)
{
    uint32_t notice[4] = {number, 0, 0x11111111, 0x22222222};
    const uint8_t answer = Virtual<uint8_t>(static_cast<void*>(tool + sign::kTarget), 0xC, &notice[0]);
    *answered = notice[1];
    point[0] = notice[2];
    point[1] = notice[3];
    return answer;
}

uint32_t DriveSign(char* view, cISC4City* cityObject, void* city, const std::vector<Ground>& ground,
                   const std::vector<Ground>& spots, bool commit)
{
    Fnv f;
    const bool trip = spots.size() >= 2;
    const Ground nowhere{};
    const Ground& a = trip ? spots[(spots.size() - 1) / 2] : nowhere;
    const Ground& b = trip ? spots[(spots.size() - 1) / 2 + 1] : nowhere;
    const uint8_t shown = Shown();
    const Marks before = CityMarks(city);
    f.Add(uint8_t{Virtual<void*>(view, 0x60) != nullptr});
    f.Add(before.labels);
    f.Add(before.signs);
    f.Add(before.hash);
    char* const tool = NewBlock(sign::kSize);
    if (!tool || !This<void*>(addr::SC4SignPlaceTool_Construct, tool))
        return 0;
    HashSignTool(f, tool);
    f.Add(uint8_t{At<std::uintptr_t>(tool, 0) == addr::SC4SignPlaceTool_Vtable});
    f.Add(uint8_t{At<std::uintptr_t>(tool, sign::kTarget) == addr::SC4SignPlaceTool_TargetVtable});
    if (!GiveToView(view, tool, 2)) {
        log::Write("autotest: tools4: the view did not take the sign tool");
        return 0;
    }
    HashSignTool(f, tool);
    const uint8_t shownInTool = Shown();
    f.Add(shownInTool);

    // QueryInterface: the notice target, and an ID nobody answers.
    void* part = reinterpret_cast<void*>(std::uintptr_t{0x5A5A5A5A});
    const uint8_t asked = Virtual<uint8_t>(tool, 0x0, kIidTarget, &part);
    f.Add(asked);
    f.Add(uint8_t{part == static_cast<void*>(tool + sign::kTarget)});
    if (asked && part == static_cast<void*>(tool + sign::kTarget))
        Virtual<uint32_t>(tool, 0x8);
    void* none = nullptr;
    f.Add(Virtual<uint8_t>(tool, 0x0, uint32_t{0x12345678}, &none));
    f.Add(uint8_t{none != nullptr});
    HashSignTool(f, tool);
    const uint32_t made = f.h;

    // The filter's function (it does not read `this`), and the height a
    // sign stands at.
    int32_t taken = 0, trees = 0;
    ForOccupants(city, kTypeProp, [&](void* occupant) {
        taken += This<uint8_t>(addr::SC4SignPlaceTool_FilterIncludes, static_cast<void*>(nullptr), occupant);
    });
    ForOccupants(city, kTypeFlora, [&](void* occupant) {
        if (trees++ < 20)
            f.Add(This<uint8_t>(addr::SC4SignPlaceTool_FilterIncludes, static_cast<void*>(nullptr), occupant));
    });
    f.Add(taken);
    const int32_t cellsX = static_cast<int32_t>(cityObject->CellCountX());
    const int32_t cellsZ = static_cast<int32_t>(cityObject->CellCountZ());
    int32_t heights = 0;
    for (int32_t z = 0; z < cellsZ; z += 4)
        for (int32_t x = 0; x < cellsX; x += 4) {
            f.Add(This<float>(addr::SC4SignPlaceTool_GroundHeight, tool, static_cast<float>(x) * 16.0f + 3.0f,
                              static_cast<float>(z) * 16.0f + 9.0f));
            ++heights;
        }
    const uint32_t direct = f.h;

    // Idle: the notice, the keys, a character, the handlers.
    uint32_t answered = 0, screen[2] = {};
    const auto step = [&](uint8_t answer) {
        f.Add(answer);
        HashSignTool(f, tool);
    };
    f.Add(Notice(tool, kNoticePlace, &answered, &screen[0]));
    f.Add(answered);
    f.Add(screen);
    f.Add(Notice(tool, 0x12345678, &answered, &screen[0]));
    f.Add(answered);
    for (const uint32_t key : {0x2Eu, 0x1Bu, 0x56u, 0x41u})
        step(KeyDown(tool, key));
    step(Character(tool, 'A'));
    step(Character(tool, 0x0D));
    step(MouseMove(tool, kOff, kOff));
    step(MouseDownL(tool, kOff, kOff));
    step(MouseUpL(tool, kOff, kOff));
    int32_t hovered = 0;
    ForGrid([&](Point p) {
        f.Add(MouseMove(tool, p.x, p.z));
        const uint8_t over = At<void*>(tool, sign::kSign) != nullptr;
        hovered += over;
        f.Add(over);
    });
    This<void>(addr::SC4SignPlaceTool_SetSign, tool, static_cast<void*>(nullptr));
    HashSignTool(f, tool);
    const uint32_t idle = f.h;

    const auto type = [&](std::initializer_list<uint32_t> bytes) {
        for (const uint32_t byte : bytes)
            step(Character(tool, byte));
    };
    const auto signs = [&] {
        const Marks marks = CityMarks(city);
        f.Add(marks.signs);
        f.Add(marks.hash);
        return marks.signs;
    };

    uint32_t typing = 0, second = 0, third = 0, committed = 0;
    uint8_t down = 0xEE, deleted = 0xEE, noticed = 0xEE;
    int32_t withNew = -1, afterEnter = -1, afterDelete = -1, withThird = -1, atShutdown = -1, afterCommit = -1;
    std::string afterLead = "-", afterWhole = "-", afterBack = "-", longest = "-", appended = "-", restored = "-";
    std::string committedText = "-";
    uint32_t noticePoint[2] = {};
    bool editingAtEnd = false;
    if (trip) {
        // a. A new sign, typed into, ended with no text.
        down = MouseDownL(tool, a.at.x, a.at.z);
        step(down);
        withNew = signs();
        if (At<int32_t>(tool, sign::kState) == 3 && At<void*>(tool, sign::kSign)) {
            At<int32_t>(tool, sign::kState) = 2; // dragged with no text: let go, it is edited
            step(MouseUpL(tool, a.at.x, a.at.z));
            type({'A', 'b', 0xC3});
            afterLead = Body(tool, sign::kText);
            type({0xA9});
            afterWhole = TextOf(At<void*>(tool, sign::kSign));
            type({8});
            afterBack = Body(tool, sign::kText);
            type({9, 0xE2, 0x82, 0xAC, 0xC3, 'A', 0x1B, 8, 8, 8, 8, 8, 8});
            for (int32_t i = 0; i < 40; ++i)
                step(Character(tool, static_cast<uint32_t>('a' + i % 26)));
            type({0xC3, 0xA9});
            longest = Body(tool, sign::kText);
            noticed = Notice(tool, kNoticePlace, &answered, &noticePoint[0]);
            f.Add(noticed);
            f.Add(answered);
            step(KeyDown(tool, 0x41));
            step(KeyDown(tool, 0x56)); // V without a modifier: no paste
            step(KeyDown(tool, 0x56, 5));
            for (int32_t i = 0; i < 30; ++i)
                step(Character(tool, 8));
            {
                OwnString more("x\ny\xC3\xA9");
                This<void>(addr::SC4SignPlaceTool_AppendText, tool, more.Pointer());
            }
            HashSignTool(f, tool);
            appended = Body(tool, sign::kText);
            step(KeyDown(tool, 0x1B)); // the text the edit began with: none
            restored = Body(tool, sign::kText);
            step(Character(tool, 0x0D));
        }
        afterEnter = signs();
        typing = f.h;

        // b. A sign with a text stays; Delete removes it.
        step(MouseDownL(tool, a.at.x, a.at.z));
        if (At<int32_t>(tool, sign::kState) == 3 && At<void*>(tool, sign::kSign)) {
            void* const made2 = At<void*>(tool, sign::kSign);
            Virtual<uint32_t>(made2, 0x4);
            type({'D', 0x0D});
            signs();
            step(KeyDown(tool, 0x2E)); // no sign under the pointer
            This<void>(addr::SC4SignPlaceTool_SetSign, tool, made2);
            HashSignTool(f, tool);
            This<void>(addr::SC4SignPlaceTool_SetSign, tool, made2); // the same again: nothing
            deleted = KeyDown(tool, 0x2E);
            step(deleted);
            f.Add(Virtual<uint32_t>(made2, 0x8));
        }
        afterDelete = signs();
        second = f.h;
    }

    // 4. The sign that stays.
    if (commit && ground.size() >= 8) {
        const Ground& c = ground[ground.size() / 3];
        step(MouseDownL(tool, c.at.x, c.at.z));
        if (At<int32_t>(tool, sign::kState) == 3 && At<void*>(tool, sign::kSign)) {
            type({'S', 'i', 'g', 'n', ' ', 0xC3, 0xA9});
            committedText = TextOf(At<void*>(tool, sign::kSign));
            type({0x0D});
        }
        afterCommit = signs();
        committed = f.h;
    }

    if (trip) {
        // c. A sign with a text, ended by a click, dragged, edited again.
        step(MouseDownL(tool, a.at.x, a.at.z));
        if (At<int32_t>(tool, sign::kState) == 3 && At<void*>(tool, sign::kSign)) {
            void* const made3 = At<void*>(tool, sign::kSign);
            Virtual<uint32_t>(made3, 0x4);
            type({'H', 'i'});
            step(MouseDownL(tool, b.at.x, b.at.z)); // the click ends the edit and no more
            withThird = signs();
            At<int32_t>(tool, sign::kState) = 1;
            At<int32_t>(tool, sign::kGrabX) = 0;
            At<int32_t>(tool, sign::kGrabZ) = 0;
            step(MouseMove(tool, b.at.x, b.at.z));
            step(MouseMove(tool, kOff, kOff));
            step(MouseUpL(tool, b.at.x, b.at.z)); // dragged with a text: idle
            At<int32_t>(tool, sign::kState) = 1;
            step(MouseUpL(tool, b.at.x, b.at.z)); // pressed and let go: edited
            type({'!'});
            step(KeyDown(tool, 0x1B));
            type({8, 8});
            editingAtEnd = At<int32_t>(tool, sign::kState) == 3;
            f.Add(Virtual<uint32_t>(made3, 0x8));
        }
        third = f.h;
    }

    // The view puts the tool away while the third sign is edited with no
    // text.
    f.Add(Virtual<uint8_t>(view, 0x68, uint32_t{0}));
    const bool away = Virtual<void*>(view, 0x60) == nullptr;
    f.Add(uint8_t{away});
    HashSignTool(f, tool);
    atShutdown = signs();
    const uint32_t lastRelease = Virtual<uint32_t>(tool, 0x8);
    f.Add(lastRelease);
    ShowAgain(shown);
    const Marks end = CityMarks(city);
    f.Add(end.labels);
    f.Add(end.signs);
    f.Add(end.hash);
    log::Write("autotest: tools4: sign %08X (made %08X, signs and labels shown %u, in the tool %u, put back to %u; %d "
               "props, %d taken by the filter; by address %08X with %d heights; idle %08X, %d grid points over a "
               "sign; %d terrain points)",
               f.h, made, shown, shownInTool, Shown(), before.props, taken, direct, heights, idle, hovered,
               static_cast<int32_t>(ground.size()));
    log::Write("autotest: tools4: sign typing %08X (click answered %u: %d signs; text after the lead byte %s, after "
               "the whole character %s, after Backspace %s; after 40 letters and two bytes %u bytes; notice answered "
               "%u with %u at %d,%d; appended %s; after Escape %u bytes; after Enter %d signs)",
               typing, down, withNew, Hex(afterLead).c_str(), Hex(afterWhole).c_str(), Hex(afterBack).c_str(),
               static_cast<uint32_t>(longest.size()), noticed, answered, static_cast<int32_t>(noticePoint[0]),
               static_cast<int32_t>(noticePoint[1]), Hex(appended).c_str(), static_cast<uint32_t>(restored.size()),
               afterEnter);
    log::Write("autotest: tools4: sign kept and deleted %08X (Delete answered %u: %d signs); dragged and edited %08X "
               "(%d signs with it, edited at the end %d, %d signs after the tool went); commit %d %08X (text %s, %d "
               "signs); away %d, last release %u; labels %d to %d, signs %d to %d, marks %08X to %08X",
               second, deleted, afterDelete, third, withThird, editingAtEnd, atShutdown, commit, committed,
               Hex(committedText).c_str(), afterCommit, away, lastRelease, before.labels, end.labels, before.signs,
               end.signs, before.hash, end.hash);
    return f.h;
}

// --- The sign removal tool on a label and a sign of the test's -----------------------------

// The prop interface of an occupant, referenced; null without one.
void* PropOf(void* occupant)
{
    void* prop = nullptr;
    Virtual<uint8_t>(occupant, 0x0, kIidProp, &prop);
    return prop;
}

uint32_t DriveRemove(char* view, void* city, const std::vector<Ground>& ground)
{
    Fnv f;
    if (ground.size() < 8) {
        log::Write("autotest: tools4: remove: too few terrain points");
        return f.h;
    }
    const uint8_t shown = Shown();
    const Marks before = CityMarks(city);
    f.Add(before.labels);
    f.Add(before.signs);
    char* const tool = NewBlock(remover::kSize);
    if (!tool || !This<void*>(addr::SC4SignRemoveTool_Construct, tool))
        return 0;
    if (!GiveToView(view, tool, 2)) {
        log::Write("autotest: tools4: the view did not take the sign removal tool");
        return 0;
    }

    // A label and a sign made by hand at two terrain points.
    const Ground& first = ground[ground.size() / 4];
    const Ground& second = ground[ground.size() * 3 / 4];
    void* const madeLabel = NewDecal(first.point);
    void* const memory = Cdecl<void*>(addr::cSC4PropOccupantSign_Allocate, std::size_t{0x590});
    void* const madeSign = memory ? This<void*>(addr::cSC4PropOccupantSign_Construct, memory, uint32_t{1}) : nullptr;
    if (!madeLabel || !madeSign) {
        log::Write("autotest: tools4: remove: nothing made");
        return 0;
    }
    Virtual<uint32_t>(madeSign, 0x4);
    Virtual<uint8_t>(PropManager(), 0x4C, Virtual<void*>(madeSign, 0xC));
    float other[3] = {second.point[0], second.point[1], second.point[2]};
    Virtual<void>(Virtual<void*>(madeSign, 0xC), 0x24, &other[0]);
    {
        OwnString text("Gone");
        Virtual<void>(madeLabel, 0x10, text.Pointer());
        Virtual<void>(madeSign, 0x10, text.Pointer());
    }
    const Marks with = CityMarks(city);
    f.Add(with.labels);
    f.Add(with.signs);

    struct Outcome {
        uint8_t hover = 0, set = 0, up = 0;
        int32_t labels = 0, signs = 0;
    };
    const auto remove = [&](void* made, const Ground& at) {
        Outcome o;
        void* const prop = PropOf(Virtual<void*>(made, 0xC));
        f.Add(MouseMove(tool, at.at.x, at.at.z));
        o.hover = prop && At<void*>(tool, remover::kSign) == prop;
        if (!o.hover && prop) {
            This<void>(addr::SC4SignRemoveTool_SetSign, tool, prop);
            o.set = At<void*>(tool, remover::kSign) == prop;
        }
        o.up = MouseUpL(tool, at.at.x, at.at.z);
        const Marks marks = CityMarks(city);
        o.labels = marks.labels;
        o.signs = marks.signs;
        f.Add(o);
        f.Add(uint8_t{At<void*>(tool, remover::kSign) != nullptr});
        if (prop)
            Virtual<uint32_t>(prop, 0x8);
        return o;
    };
    const Outcome l = remove(madeLabel, first);
    const Outcome s = remove(madeSign, second);
    Virtual<uint32_t>(madeLabel, 0x8);
    Virtual<uint32_t>(madeSign, 0x8);

    f.Add(Virtual<uint8_t>(view, 0x68, uint32_t{0}));
    const bool away = Virtual<void*>(view, 0x60) == nullptr;
    f.Add(uint8_t{away});
    const uint32_t lastRelease = Virtual<uint32_t>(tool, 0x8);
    f.Add(lastRelease);
    ShowAgain(shown);
    const Marks end = CityMarks(city);
    f.Add(end.labels);
    f.Add(end.signs);
    f.Add(end.hash);
    log::Write("autotest: tools4: remove %08X (labels %d, signs %d; with the two made %d, %d; the label: found by the "
               "pointer %u, set by address %u, OnMouseUpL answered %u, then labels %d, signs %d; the sign: found by "
               "the pointer %u, set by address %u, OnMouseUpL answered %u, then labels %d, signs %d; away %d, last "
               "release %u; at the end labels %d, signs %d, marks %08X)",
               f.h, before.labels, before.signs, with.labels, with.signs, l.hover, l.set, l.up, l.labels, l.signs,
               s.hover, s.set, s.up, s.labels, s.signs, away, lastRelease, end.labels, end.signs, end.hash);
    return f.h;
}

void Drive(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_TOOLS4") || !game::IsBuild641())
        return;
    char* const view = CityView();
    void* const city = Global<void*>(addr::SC4_City);
    if (!view || !city || !day.city || !Global<void*>(addr::SC4_Terrain) || !Global<void*>(addr::SC4_View43DD0) ||
        !Global<void*>(addr::SC4_Service43DB8) || !Global<void*>(addr::SC4_Service43D54) || !PropManager()) {
        log::Write("autotest: tools4: no city view");
        return;
    }
    const bool commit = EnvInt("OPENSC4_TEST_TOOLS4_COMMIT", 0) != 0;
    const bool remove = commit && EnvInt("OPENSC4_TEST_TOOLS4_REMOVE", 0) != 0;
    // 1 label, 2 sign, 4 and 8 their round trips when nothing is committed
    const int parts = EnvInt("OPENSC4_TEST_TOOLS4_PARTS", 15);

    // Where the round trips happen: at terrain points whose 64 m cell is
    // empty; with the commit, when there are none, anywhere.
    const std::vector<Ground> ground = GroundPoints(day.city);
    std::vector<Ground> quiet;
    for (const Ground& g : ground)
        if (Quiet(city, g.point))
            quiet.push_back(g);
    const std::vector<Ground> none;
    const std::vector<Ground>& spots = quiet.size() >= 2 ? quiet : commit && ground.size() >= 8 ? ground : none;
    log::Write("autotest: tools4: %d terrain points under the grid, %d in an empty 64 m cell; round trips at %d",
               static_cast<int32_t>(ground.size()), static_cast<int32_t>(quiet.size()),
               static_cast<int32_t>(spots.size()));
    Fnv all;
    all.Add(static_cast<int32_t>(ground.size()));
    all.Add(static_cast<int32_t>(quiet.size()));
    if (parts & 1)
        all.Add(DriveLabel(view, day.city, city, ground, commit || (parts & 4) ? spots : none, commit));
    if (parts & 2)
        all.Add(DriveSign(view, day.city, city, ground, commit || (parts & 8) ? spots : none, commit));
    if (remove)
        all.Add(DriveRemove(view, city, ground));
    std::snprintf(g_token, sizeof g_token, " tools4 %08X", all.h);
}

const DayHook kTools4{"tools4", 57, &Drive};

void Fingerprint(cISC4City*, std::string& line)
{
    line += g_token;
}

const FingerprintPart kFingerprint{"tools4", 18, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
