// OPENSC4_TEST_T8=<day>: the U-Drive-It control's second piece
// (ui.tools.udrive2 and ui.tools.udrive3, docs/tools.md). It calls only the
// game's addresses and the control's vtable slots, never our functions, so a
// run with the groups on and one with them off compare. The control is made
// on its own (as the park automaton makes it, without Init); its driven
// automaton, the occupant behind it, the view, the panel windows and their
// children, the simulator, the automata managers, the script system, the
// overlay manager and the marker are stand-ins that record what they are
// told (pointers never go into the hash: only which stand-in was meant). The
// control gets a copy of its vtable whose slot 8 (SetCursorId) and slot 27
// (put away: the disaster layer and the view's current tool) only record.
// It is destroyed through slot 26 (not Release, which would run Shutdown).
// Token ` udrive2`, sub-hashes on the log lines `autotest: udrive2: ...`.
//
// OPENSC4_TEST_T8_PARTS (decimal mask, default 63):
//   1   made; the filter test (0x00562C90) on stand-in occupants; the panel
//       command handler (0x00562E20) on clicks it does not take; the list
//       (0x00565FA0)
//   2   letting go without a driven automaton (0x00562FD0), the automaton
//       gone (0x00563310) with nothing driven, the panel windows closed
//       (0x00563330), stop and pause (0x00564450) on stand-ins
//   4   DoMessage (0x00566890) with every message it handles; kind 0xB of
//       0x09D92FC3 restarts the stopwatch (real time: its state is stopped
//       again at once and not hashed); kinds 0xB and 0xD stop the sound
//       system's event 0 (sound only)
//   8   the panel's update (0x00566000): stand-in state, panel and view; the
//       terrain, the city's cells, the region terrain and the budget's funds
//       are the city's (read only); the two engine volumes go to the sound
//       system (sound only, not observable here)
//   16  the keys (slots 14 and 15) with a driven automaton that cannot be
//       driven (stopped): the panel's buttons, the two toggles, Escape
//   32  the mouse move (slot 20) with a driven automaton in state 0x10: the
//       view's pick (stand-in) and the city's cell into the marker
//   64  (asked only) the keys with a driven automaton that can be driven:
//       commands to the stand-in and the sound system's events (sound only)
//   128 (asked only) the left button (slot 16) at (-1000, -1000) with real
//       filters (the renderer's pick, slot 0x104 of 0x00B43DD0); letting go
//       of a driven stand-in (0x00562FD0: the sound system's events stopped
//       and an unknown bank unloaded) and the automaton gone for it
//   256 (only with OPENSC4_TEST_T8_COMMIT=1) the panel commands posted
//       (0x6A935CB7 and 0x6A935CBA are the view's: they move the camera);
//       Init, stop and pause, Shutdown on a second control with the city's
//       real services (pauses and resumes the simulator and the automata
//       managers, shows and takes away the driving panel, 0x00506910)
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <utility>

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
};

char g_token[32];
Fnv* g_log = nullptr;
int g_unexpected = 0;

template <typename T>
void Note(const T& value)
{
    if (g_log)
        g_log->Add(value);
}

// --- Stand-ins --------------------------------------------------------------------------

// Every stand-in: a vtable, the fields game code reads (below 0x300), then
// our bookkeeping. The role byte goes into each record.
struct Fake {
    void** vtable;
    uint8_t raw[0x2FC];
    uint8_t role;
    uint32_t releases;
    uint32_t addrefs;
    uint32_t answer[0x50]; // per slot (offset / 4)
    Fake* child[4];        // what GetChildAs gives: 0 dial, 1 text, 2 button, 3 other
};

enum Role : uint8_t {
    kDriven = 1,
    kOccupant,
    kView,
    kViewWindow,
    kPanel,
    kChild,
    kSim,
    kManager,
    kScript,
    kMessage,
    kPlain,
    kHolder,
    kProperty,
    kVariant,
};

void Record(const Fake* self, uint32_t slot)
{
    Note(self->role);
    Note(slot);
}

template <std::size_t Slot>
uint32_t __fastcall Unexpected(Fake* self, void*)
{
    ++g_unexpected;
    Note(uint8_t{0xEE});
    Note(self->role);
    Note(static_cast<uint32_t>(Slot * 4));
    return 0;
}

template <std::size_t... I>
void FillUnexpected(void** table, std::index_sequence<I...>)
{
    ((table[I] = reinterpret_cast<void*>(&Unexpected<I>)), ...);
}

constexpr std::size_t kSlots = 0x50;

struct Table {
    void* slot[kSlots];
    Table() { FillUnexpected(slot, std::make_index_sequence<kSlots>{}); }
    template <typename F>
    void Set(std::size_t offset, F function)
    {
        slot[offset / 4] = reinterpret_cast<void*>(function);
    }
};

uint32_t __fastcall FakeAddRef(Fake* self, void*)
{
    return ++self->addrefs;
}

uint32_t __fastcall FakeRelease(Fake* self, void*)
{
    ++self->releases;
    return 1;
}

// Records the slot and gives the scripted answer.
template <uint32_t Slot>
uint32_t __fastcall Answer0(Fake* self, void*)
{
    Record(self, Slot);
    return self->answer[Slot / 4];
}

template <uint32_t Slot>
uint32_t __fastcall Answer1(Fake* self, void*, uint32_t a)
{
    Record(self, Slot);
    Note(a);
    return self->answer[Slot / 4];
}

template <uint32_t Slot>
uint32_t __fastcall Answer2(Fake* self, void*, uint32_t a, uint32_t b)
{
    Record(self, Slot);
    Note(a);
    Note(b);
    return self->answer[Slot / 4];
}

// A pointer argument: only which of the known objects it is.
const void* g_known[8];

uint8_t Which(const void* pointer)
{
    if (!pointer)
        return 0;
    for (uint8_t i = 0; i < 8; ++i) {
        if (g_known[i] == pointer)
            return static_cast<uint8_t>(i + 1);
    }
    return 0xFF;
}

template <uint32_t Slot>
uint32_t __fastcall Pointer1(Fake* self, void*, const void* a)
{
    Record(self, Slot);
    Note(Which(a));
    return self->answer[Slot / 4];
}

// The driven automaton.
float g_position[3];
float g_destination[3];

void __fastcall DrivenPosition(Fake* self, void*, float* out)
{
    Record(self, 0x1C);
    std::memcpy(out, g_position, sizeof g_position);
}

void __fastcall DrivenDestination(Fake* self, void*, float* out)
{
    Record(self, 0x2C);
    std::memcpy(out, g_destination, sizeof g_destination);
}

float g_speed = 0.0f;

float __fastcall DrivenSpeed(Fake* self, void*)
{
    Record(self, 0x88);
    return g_speed;
}

// Slot 0x7C: answer[0x7C / 4] bit 0 for 0x8000000, bit 1 for 1.
uint8_t __fastcall DrivenFlag(Fake* self, void*, uint32_t flag)
{
    Record(self, 0x7C);
    Note(flag);
    const uint32_t bits = self->answer[0x7C / 4];
    if (flag == 0x8000000)
        return bits & 1;
    if (flag == 1)
        return (bits >> 1) & 1;
    return 0;
}

// The occupant: slot 0x58 answers answer[0x58 / 4] for the group in raw.
uint8_t __fastcall OccupantGroup(Fake* self, void*, uint32_t group)
{
    Record(self, 0x58);
    Note(group);
    return group == *reinterpret_cast<const uint32_t*>(self->raw) ? static_cast<uint8_t>(self->answer[0x58 / 4]) : 0;
}

// The view's pick (slot 0x90: x, z, out vector, flag).
float g_pick[3];

uint8_t __fastcall ViewPick(Fake* self, void*, int32_t x, int32_t z, float* out, uint32_t flag)
{
    Record(self, 0x90);
    Note(x);
    Note(z);
    Note(flag);
    if (self->answer[0x90 / 4])
        std::memcpy(out, g_pick, sizeof g_pick);
    return static_cast<uint8_t>(self->answer[0x90 / 4]);
}

// The panel's children (slots 0x90 and 0x94: id, interface, out).
uint8_t PanelChild(Fake* self, uint32_t slot, uint32_t id, uint32_t iid, Fake** out)
{
    Record(self, slot);
    Note(id);
    Note(iid);
    Fake* child = self->child[3];
    if (iid == 0x0BCBF1DF)
        child = self->child[0];
    else if (iid == 0x212CDC1F)
        child = self->child[1];
    else if (iid == 0x8810)
        child = self->child[2];
    if (!self->answer[slot / 4] || !child)
        return 0;
    *out = child;
    return 1;
}

uint8_t __fastcall PanelChild90(Fake* self, void*, uint32_t id, uint32_t iid, Fake** out)
{
    return PanelChild(self, 0x90, id, iid, out);
}

uint8_t __fastcall PanelChild94(Fake* self, void*, uint32_t id, uint32_t iid, Fake** out)
{
    return PanelChild(self, 0x94, id, iid, out);
}

Fake* __fastcall PanelFind(Fake* self, void*, uint32_t id)
{
    Record(self, 0x8C);
    Note(id);
    return self->child[3];
}

// A child's caption: the string's text.
uint8_t __fastcall ChildCaption(Fake* self, void*, const char* const* text)
{
    Record(self, 0x128);
    const char* begin = text[1];
    const char* end = text[2];
    if (g_log && begin && end >= begin)
        g_log->Bytes(begin, static_cast<std::size_t>(end - begin));
    return 1;
}

Fake* __fastcall ChildSelf(Fake* self, void*)
{
    Record(self, 0xC);
    return self;
}

// A listener's slot 3 (NotifyListeners 0x005644E0): the message's type,
// whether its void 1 is the control, data 2 and data 3.
uint8_t __fastcall ListenerMessage(Fake* self, void*, void* message)
{
    Record(self, 0xC);
    Note(Virtual<uint32_t>(message, 0x10));
    Note(Which(Virtual<void*>(message, 0x28)));
    Note(Virtual<uint32_t>(message, 0x38));
    Note(Virtual<uint32_t>(message, 0x4C));
    return 1;
}

// The property chain for the sound bank (0x005FD480): an unknown bank ID.
uint8_t __fastcall VariantValue(Fake* self, void*, uint32_t* out)
{
    Record(self, 0x64);
    *out = 0x0B7D0001;
    return 1;
}

Table g_driven, g_occupant, g_view, g_viewWindow, g_panel, g_child, g_sim, g_manager, g_script, g_message, g_plain,
    g_holder, g_property, g_variant;

void SetUpTables()
{
    static bool done = false;
    if (done)
        return;
    done = true;
    for (Table* table : {&g_driven, &g_occupant, &g_view, &g_viewWindow, &g_panel, &g_child, &g_sim, &g_manager,
                         &g_script, &g_message, &g_plain, &g_holder, &g_property, &g_variant}) {
        table->Set(0x4, &FakeAddRef);
        table->Set(0x8, &FakeRelease);
    }
    g_driven.Set(0x14, &Answer0<0x14>);
    g_driven.Set(0x1C, &DrivenPosition);
    g_driven.Set(0x2C, &DrivenDestination);
    g_driven.Set(0x34, &Answer1<0x34>);
    g_driven.Set(0x48, &Answer0<0x48>);
    g_driven.Set(0x7C, &DrivenFlag);
    g_driven.Set(0x88, &DrivenSpeed);
    g_driven.Set(0xA0, &Pointer1<0xA0>);
    g_driven.Set(0xD4, &Answer0<0xD4>);
    g_driven.Set(0xE4, &Answer1<0xE4>);
    g_driven.Set(0xEC, &Answer0<0xEC>);
    g_driven.Set(0xF0, &Answer2<0xF0>);
    g_driven.Set(0x10C, &Answer0<0x10C>);
    g_occupant.Set(0x18, &Answer0<0x18>);
    g_occupant.Set(0x1C, &Answer0<0x1C>);
    g_occupant.Set(0x58, &OccupantGroup);
    g_view.Set(0xC, &Answer0<0xC>);
    g_view.Set(0x2C, &Answer2<0x2C>);
    g_view.Set(0x30, &Answer0<0x30>);
    g_view.Set(0x34, &Answer0<0x34>);
    g_view.Set(0x68, &Answer1<0x68>);
    g_view.Set(0x90, &ViewPick);
    g_view.Set(0xA4, &Answer1<0xA4>);
    g_view.Set(0xDC, &Answer1<0xDC>);
    g_view.Set(0xE4, &Answer0<0xE4>);
    g_view.Set(0xE8, &Answer0<0xE8>);
    g_viewWindow.Set(0x3C, &Pointer1<0x3C>);
    g_panel.Set(0x8C, &PanelFind);
    g_panel.Set(0x90, &PanelChild90);
    g_panel.Set(0x94, &PanelChild94);
    g_panel.Set(0x114, &Answer0<0x114>);
    g_panel.Set(0x118, &Answer0<0x118>);
    g_panel.Set(0x11C, &Answer0<0x11C>);
    g_child.Set(0xC, &ChildSelf);
    g_child.Set(0x14, &Answer2<0x14>);
    g_child.Set(0x20, &Answer1<0x20>);
    g_child.Set(0x24, &Answer1<0x24>);
    g_child.Set(0x28, &Answer1<0x28>);
    g_child.Set(0x30, &Answer1<0x30>);
    g_child.Set(0x110, &Answer2<0x110>);
    g_child.Set(0x128, &ChildCaption);
    g_sim.Set(0x24, &Answer0<0x24>);
    g_sim.Set(0x30, &Answer0<0x30>);
    g_sim.Set(0x3C, &Answer0<0x3C>);
    g_manager.Set(0x24, &Answer0<0x24>);
    g_manager.Set(0x28, &Answer0<0x28>);
    g_manager.Set(0x88, &Pointer1<0x88>);
    g_script.Set(0x38, &Answer1<0x38>);
    g_message.Set(0x10, &Answer0<0x10>);
    g_message.Set(0x28, &Answer0<0x28>);
    g_message.Set(0x38, &Answer0<0x38>);
    g_message.Set(0x4C, &Answer0<0x4C>);
    g_plain.Set(0xC, &ListenerMessage);
    g_holder.Set(0x24, &Answer1<0x24>);
    g_property.Set(0x14, &Answer0<0x14>);
    g_variant.Set(0x64, &VariantValue);
}

void Make(Fake& fake, Table& table, Role role)
{
    std::memset(&fake, 0, sizeof fake);
    fake.vtable = table.slot;
    fake.role = role;
}

uint32_t Ptr(const void* pointer)
{
    return static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(pointer));
}

// --- The control ------------------------------------------------------------------------

// The control's vtable with slot 8 (SetCursorId) and slot 27 (put away)
// recording instead.
void* g_controlVtable[30];

void __fastcall ControlCursor(char*, void*, uint32_t id)
{
    Note(uint8_t{0xC8});
    Note(id);
}

uint32_t __fastcall ControlPutAway(char*, void*)
{
    Note(uint8_t{0xC7});
    return 0;
}

char* MakeControl()
{
    char* const control = static_cast<char*>(
        reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0xC0));
    std::memset(control, 0x5A, 0xC0);
    reinterpret_cast<void*(__thiscall*)(void*)>(addr::cSC4UDriveControl_Construct)(control);
    std::memcpy(g_controlVtable, reinterpret_cast<const void*>(addr::cSC4UDriveControl_Vtable), sizeof g_controlVtable);
    g_controlVtable[8] = reinterpret_cast<void*>(&ControlCursor);
    g_controlVtable[27] = reinterpret_cast<void*>(&ControlPutAway);
    At<void**>(control, 0) = g_controlVtable;
    return control;
}

// The fields a hash may see: the scalars, and for each pointer field (and
// +0x78, which the constructor leaves as it was) only whether it is set.
void AddFields(Fnv& f, const char* control)
{
    for (std::size_t offset : {std::size_t{0x4}, std::size_t{0x8}, std::size_t{0xC}, std::size_t{0x10},
                               std::size_t{0x38}, std::size_t{0x3C}, std::size_t{0x40}, std::size_t{0x44},
                               std::size_t{0x6C}, std::size_t{0x70}})
        f.Add(*reinterpret_cast<const uint32_t*>(control + offset));
    uint32_t present = 0;
    uint32_t bit = 1;
    for (std::size_t offset = 0x14; offset < 0xC0; offset += 4, bit <<= 1) {
        if (offset == 0x28 || offset == 0x2C || (offset >= 0x48 && offset < 0x60))
            continue; // the vtables; the stopwatch
        if (*reinterpret_cast<void* const*>(control + offset))
            present |= bit;
    }
    f.Add(present);
}

template <typename R, typename... Args>
R Game(std::uintptr_t function, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(function)(self, args...);
}

uint8_t DoMessage(char* control, Fake& message)
{
    return Game<uint8_t>(addr::cSC4UDriveControl_DoMessage, control + 0x28, static_cast<void*>(&message));
}

// The state 0x10 the driven stand-in gives (a plain block).
alignas(8) uint8_t g_state[0x270];

void SetState()
{
    std::memset(g_state, 0, sizeof g_state);
    auto put = [](std::size_t offset, float value) { std::memcpy(g_state + offset, &value, 4); };
    auto word = [](std::size_t offset, uint32_t value) { std::memcpy(g_state + offset, &value, 4); };
    put(0x2C, 0.5f);
    put(0x30, 0.25f);
    put(0x34, 2.0f);
    put(0x9C, 3.0f);
    word(0x114, 0xC16C4E1B);
    word(0x144, 17);
    word(0x178, 5);
    g_state[0x1FF] = 1;
    for (std::size_t offset = 0x238; offset <= 0x268; offset += 4)
        put(offset, static_cast<float>(offset - 0x230) / 8.0f);
}

void PutFloat(std::size_t offset, float value)
{
    std::memcpy(g_state + offset, &value, 4);
}

// --- The parts --------------------------------------------------------------------------

struct Stage {
    Fake driven, occupant, view, viewWindow, panel, panelSmall, dial, text, button, enable, sim, managers[4], script,
        message, plain[3], marker, overlays, holder, property, variant;
    uint32_t header[8];
};

void SetUpStage(Stage& s, char* control)
{
    Make(s.driven, g_driven, kDriven);
    Make(s.occupant, g_occupant, kOccupant);
    Make(s.view, g_view, kView);
    Make(s.viewWindow, g_viewWindow, kViewWindow);
    Make(s.panel, g_panel, kPanel);
    Make(s.panelSmall, g_panel, kPanel);
    Make(s.dial, g_child, kChild);
    Make(s.text, g_child, kChild);
    Make(s.button, g_child, kChild);
    Make(s.enable, g_child, kChild);
    Make(s.sim, g_sim, kSim);
    for (Fake& manager : s.managers)
        Make(manager, g_manager, kManager);
    Make(s.script, g_script, kScript);
    Make(s.message, g_message, kMessage);
    for (Fake& plain : s.plain)
        Make(plain, g_plain, kPlain);
    Make(s.marker, g_plain, kPlain);
    Make(s.overlays, g_plain, kPlain);
    Make(s.holder, g_holder, kHolder);
    Make(s.property, g_property, kProperty);
    Make(s.variant, g_variant, kVariant);
    std::memset(s.header, 0, sizeof s.header);
    s.dial.raw[0] = 1;
    s.text.raw[0] = 2;
    s.button.raw[0] = 3;
    s.enable.raw[0] = 4;
    s.panel.child[0] = &s.dial;
    s.panel.child[1] = &s.text;
    s.panel.child[2] = &s.button;
    s.panel.child[3] = &s.enable;
    s.panel.answer[0x90 / 4] = 1;
    s.panel.answer[0x94 / 4] = 1;
    s.view.answer[0xC / 4] = Ptr(&s.viewWindow);
    s.driven.answer[0x14 / 4] = Ptr(&s.occupant);
    s.occupant.answer[0x18 / 4] = Ptr(&s.holder);
    s.holder.answer[0x24 / 4] = Ptr(&s.property);
    s.property.answer[0x14 / 4] = Ptr(&s.variant);
    // The overlay manager's map at +0x48: an empty tree (header with no root).
    *reinterpret_cast<uint32_t**>(s.overlays.raw + 0x44) = s.header;
    // The marker's points.
    for (std::size_t offset = 0x18; offset < 0x28; offset += 4)
        *reinterpret_cast<int32_t*>(s.marker.raw + offset) = -1;
    g_known[0] = control + 0x28;
    g_known[1] = &s.panel;
    g_known[2] = &s.panelSmall;
    g_known[3] = &s.driven;
    g_known[4] = control;
}

// Takes the stand-ins out of the control again (the destructor would release
// them: they only count).
void ClearStage(char* control)
{
    for (std::size_t offset : {std::size_t{0x1C}, std::size_t{0x30}, std::size_t{0x34}, std::size_t{0x60},
                               std::size_t{0x8C}, std::size_t{0x90}, std::size_t{0x94}, std::size_t{0x98},
                               std::size_t{0x9C}, std::size_t{0xA0}, std::size_t{0xA4}, std::size_t{0xA8},
                               std::size_t{0xAC}, std::size_t{0xB0}})
        At<void*>(control, offset) = nullptr;
}

void PartSmall(char* control, Stage& s, bool commit, Fnv& out)
{
    Fnv f;
    g_log = &f;
    AddFields(f, control);
    // The filter test: ECX is not used.
    for (uint32_t type : {0xC7CC75F1u, 0x496E5783u, 0x480E6151u, 0x897F7EC2u, 0x12345678u, 0u}) {
        s.occupant.answer[0x1C / 4] = type;
        f.Add(Game<uint8_t>(addr::cSC4UDriveControl_FilterTest, control, static_cast<void*>(&s.occupant)));
    }
    // The panel commands: clicks it does not take.
    const uint32_t clicks[][3] = {
        {3, 0x287259F6, 0x11111111}, {2, 0x287259F6, 0x4992046A}, {3, 0x11111111, 0x4992046A}, {0, 0, 0}};
    for (const auto& click : clicks)
        f.Add(Game<uint8_t>(addr::cSC4UDriveControl_OnPanelCommand, control + 0x2C, static_cast<void*>(nullptr),
                            static_cast<const void*>(click)));
    if (commit) {
        for (uint32_t button : {0x4992046Au, 0x2C16C9CBu, 0x4C1A5CC9u, 0xE9920494u}) {
            const uint32_t click[3] = {3, 0x287259F6, button};
            f.Add(Game<uint8_t>(addr::cSC4UDriveControl_OnPanelCommand, control + 0x2C, static_cast<void*>(nullptr),
                                static_cast<const void*>(click)));
        }
    }
    // The list: nothing, A, A again, B.
    for (Fake* object : {static_cast<Fake*>(nullptr), &s.plain[0], &s.plain[0], &s.plain[1]})
        Game<void>(addr::cSC4UDriveControl_AddListed, control, static_cast<void*>(object));
    f.Add(s.plain[0].addrefs);
    f.Add(s.plain[1].addrefs);
    void** const head = At<void**>(control, 0x64);
    for (void** node = static_cast<void**>(head[0]); node != head; node = static_cast<void**>(node[0])) {
        f.Add(node[2] == &s.plain[0] ? uint8_t{1} : node[2] == &s.plain[1] ? uint8_t{2} : uint8_t{0});
        f.Add(static_cast<void**>(static_cast<void**>(node[1])[0]) == node);
    }
    // The listeners told (B's 0x005644E0): a real cGZMessage 0x0BB324BD.
    Game<void>(addr::cSC4UDriveControl_NotifyListeners, control, uint32_t{1}, uint32_t{0});
    Game<void>(addr::cSC4UDriveControl_NotifyListeners, control, uint32_t{0}, uint32_t{5});
    // The windows (B's 0x00564570) for an instance no exemplar has: the
    // resource manager says no; with a window already there nothing at all.
    Game<void>(addr::cSC4UDriveControl_MakeWindows, control, uint32_t{0x12345678});
    AddFields(f, control);
    At<void*>(control, 0xA4) = &s.panel;
    Game<void>(addr::cSC4UDriveControl_MakeWindows, control, uint32_t{0x12345678});
    At<void*>(control, 0xA4) = nullptr;
    g_log = nullptr;
    log::Write("autotest: udrive2: small %08X", f.h);
    out.Add(f.h);
}

void PartRelease(char* control, Stage& s, Fnv& out)
{
    Fnv f;
    g_log = &f;
    At<void*>(control, 0x1C) = &s.view;
    Game<void>(addr::cSC4UDriveControl_F562FD0, control);
    Game<void>(addr::cSC4UDriveControl_OnAutomatonGone, control, static_cast<void*>(&s.plain[2]));
    Game<void>(addr::cSC4UDriveControl_OnAutomatonGone, control, static_cast<void*>(nullptr));
    Game<void>(addr::cSC4UDriveControl_CloseWindows, control);
    At<void*>(control, 0xA4) = &s.panel;
    At<void*>(control, 0xA8) = &s.panelSmall;
    Game<void>(addr::cSC4UDriveControl_CloseWindows, control);
    f.Add(s.panel.releases);
    f.Add(s.panelSmall.releases);
    AddFields(f, control);
    At<void*>(control, 0x60) = &s.plain[2];
    At<void*>(control, 0xA0) = &s.sim;
    for (std::size_t i = 0; i < 4; ++i)
        At<void*>(control, 0x8C + 4 * i) = &s.managers[i];
    for (uint32_t paused : {0u, 1u}) {
        s.sim.answer[0x3C / 4] = paused;
        Game<void>(addr::cSC4UDriveControl_StopAndPause, control);
        AddFields(f, control);
    }
    // B's pause (0x00562F00) both ways, then with two managers missing.
    for (int round = 0; round < 2; ++round) {
        for (uint32_t paused : {0u, 1u}) {
            s.sim.answer[0x3C / 4] = paused;
            for (uint8_t pause : {uint8_t{1}, uint8_t{0}})
                Game<void>(addr::cSC4UDriveControl_F562F00, control, pause);
        }
        At<void*>(control, 0x90) = nullptr;
        At<void*>(control, 0x98) = nullptr;
    }
    f.Add(s.plain[2].releases);
    g_log = nullptr;
    log::Write("autotest: udrive2: release %08X", f.h);
    out.Add(f.h);
}

void PartPanel(char* control, Stage& s, Fnv& out)
{
    Fnv f;
    g_log = &f;
    SetState();
    At<void*>(control, 0x1C) = &s.view;
    At<void*>(control, 0x30) = &s.driven;
    *reinterpret_cast<uint32_t*>(s.occupant.raw) = 0x4004;
    struct Case {
        uint32_t flags;
        bool state;
        bool panel;
        uint32_t group; // the answer for 0x4004
        float x, y, z;
        float speed;
    };
    const Case cases[] = {
        {1, true, true, 1, 512.0f, 300.0f, 512.0f, 0.02f},
        {2, true, true, 1, 512.0f, 300.0f, 512.0f, 0.02f},
        {0, false, false, 1, 512.0f, 300.0f, 512.0f, 0.02f},
        {0, false, true, 1, 512.0f, 300.0f, 512.0f, 0.02f},
        {0, true, false, 1, 512.0f, 300.0f, 512.0f, 0.02f},
        {0, true, true, 1, 512.0f, 300.0f, 512.0f, 0.02f},
        {0, true, true, 0, 512.0f, 300.0f, 512.0f, -0.03f},
        {0, true, true, 0, 700.0f, 1250.5f, 300.0f, 0.0f},
        {0, true, true, 1, -5000.0f, 40.0f, -5000.0f, 0.5f},
        {0, true, true, 0, -5000.0f, 40.0f, -5000.0f, 0.5f},
    };
    int index = 0;
    for (const Case& c : cases) {
        s.driven.answer[0x7C / 4] = c.flags;
        s.driven.answer[0xE4 / 4] = c.state ? Ptr(g_state) : 0;
        s.occupant.answer[0x58 / 4] = c.group;
        At<void*>(control, 0xA4) = c.panel ? &s.panel : nullptr;
        g_position[0] = c.x;
        g_position[1] = c.y;
        g_position[2] = c.z;
        g_destination[0] = c.x + 10.0f;
        g_destination[1] = c.y;
        g_destination[2] = c.z - 10.0f;
        g_speed = c.speed;
        if (index == 7) {
            PutFloat(0x2C, 0.1f); // below the bottom
            PutFloat(0x9C, -1.0f);
        } else if (index == 8) {
            PutFloat(0x2C, -9.0f); // under the bottom, large in size; a zero cursor
            PutFloat(0x9C, 3.0f);
            std::memset(g_state + 0x114, 0, 4);
            PutFloat(0x34, 0.125f); // top under the bottom
        } else if (index == 9) {
            PutFloat(0x34, -1.0f); // no top
            At<uint32_t>(control, 0x40) = 0;
        }
        Note(uint8_t{0xA0});
        Note(index);
        Game<void>(addr::cSC4UDriveControl_UpdatePanel, control);
        f.Add(At<uint32_t>(control, 0x10));
        ++index;
    }
    At<uint32_t>(control, 0x40) = 0x3D2C0831;
    f.Add(s.dial.releases);
    f.Add(s.text.releases);
    f.Add(s.button.releases);
    g_log = nullptr;
    log::Write("autotest: udrive2: panel %08X", f.h);
    out.Add(f.h);
}

void PartKeys(char* control, Stage& s, bool drivable, Fnv& out)
{
    Fnv f;
    g_log = &f;
    At<void*>(control, 0x1C) = &s.view;
    At<void*>(control, 0x30) = &s.driven;
    At<void*>(control, 0xA4) = &s.panel;
    s.driven.answer[0x7C / 4] = drivable ? 0 : 1;
    s.driven.answer[0xE4 / 4] = 0;
    void* const situations = Virtual<void*>(Global<void*>(addr::SC4_City), 0xC4);
    f.Add(Virtual<uint8_t>(situations, 0x80));
    for (int pass = 0; pass < 2; ++pass) {
        At<void*>(control, 0xB0) = pass ? &s.script : nullptr;
        for (uint32_t key = 0x2F; key <= 0x3E; ++key) {
            // '0' to '<' are the commands 0 to 12, the rest none (0xD).
            s.script.answer[0x38 / 4] = key >= 0x30 && key <= 0x3C ? key - 0x30 : 0xD;
            for (uint32_t modifiers : {0u, 0x40000000u}) {
                Note(uint8_t{0xB0});
                Note(key);
                f.Add(Virtual<uint8_t>(control, 0x38, key, modifiers));
                f.Add(Virtual<uint8_t>(control, 0x3C, key, modifiers));
                f.Add(At<uint16_t>(control, 0x44));
            }
        }
    }
    f.Add(Virtual<uint8_t>(control, 0x38, uint32_t{0x1B}, uint32_t{0}));
    f.Add(s.button.releases);
    g_log = nullptr;
    log::Write("autotest: udrive2: keys%s %08X", drivable ? " (drivable)" : "", f.h);
    out.Add(f.h);
}

void PartMouse(char* control, Stage& s, Fnv& out)
{
    Fnv f;
    g_log = &f;
    SetState();
    At<void*>(control, 0x1C) = &s.view;
    At<void*>(control, 0x30) = &s.driven;
    At<void*>(control, 0x9C) = &s.marker;
    s.driven.answer[0x7C / 4] = 0;
    s.driven.answer[0xE4 / 4] = Ptr(g_state);
    g_pick[0] = 600.0f;
    g_pick[1] = 250.0f;
    g_pick[2] = 420.0f;
    for (uint32_t picked : {1u, 1u, 0u, 0u}) {
        s.view.answer[0x90 / 4] = picked;
        f.Add(Virtual<uint8_t>(control, 0x50, int32_t{320}, int32_t{240}, uint32_t{0}));
        f.Bytes(s.marker.raw + 0x18, 0x10);
        f.Add(At<uint32_t>(g_state, 0x214));
        f.Add(At<uint32_t>(g_state, 0x218));
        g_pick[0] = 4000.0f;
    }
    g_log = nullptr;
    log::Write("autotest: udrive2: mouse %08X", f.h);
    out.Add(f.h);
}

void PartMessages(char* control, Stage& s, Fnv& out)
{
    Fnv f;
    g_log = &f;
    At<void*>(control, 0x1C) = &s.view;
    s.message.answer[0x10 / 4] = 0x12345678;
    f.Add(DoMessage(control, s.message));
    // 0xA6B79602: the zoom buttons, without and with the panel; the view's
    // answers go on as whole words.
    s.view.answer[0x30 / 4] = 0x00000101;
    s.view.answer[0x34 / 4] = 0x00000200;
    s.message.answer[0x10 / 4] = 0xA6B79602;
    f.Add(DoMessage(control, s.message));
    At<void*>(control, 0xA4) = &s.panel;
    At<void*>(control, 0xA8) = &s.panelSmall;
    f.Add(DoMessage(control, s.message));
    // A tick with a stopped driven automaton.
    At<void*>(control, 0x30) = &s.driven;
    s.driven.answer[0x7C / 4] = 1;
    s.message.answer[0x10 / 4] = 0x86AD10EE;
    f.Add(DoMessage(control, s.message));
    // The panel made small, big, switched.
    for (uint32_t type : {0xAC1F0449u, 0xCC1F0457u, 0xAC1F045Du}) {
        s.message.answer[0x10 / 4] = type;
        for (uint32_t visible : {1u, 0u}) {
            s.panel.answer[0x11C / 4] = visible;
            f.Add(DoMessage(control, s.message));
        }
    }
    At<void*>(control, 0xA4) = nullptr;
    At<void*>(control, 0xA8) = nullptr;
    for (uint32_t type : {0xAC1F0449u, 0xCC1F0457u, 0xAC1F045Du}) {
        s.message.answer[0x10 / 4] = type;
        f.Add(DoMessage(control, s.message));
    }
    // 0x09D92FC3: gone (another automaton, then nothing with nothing driven),
    // an unknown kind, 0xD.
    s.message.answer[0x10 / 4] = 0x09D92FC3;
    s.message.answer[0x38 / 4] = 3;
    s.message.answer[0x28 / 4] = Ptr(&s.plain[2]);
    f.Add(DoMessage(control, s.message));
    At<void*>(control, 0x30) = nullptr;
    s.message.answer[0x28 / 4] = 0;
    f.Add(DoMessage(control, s.message));
    s.message.answer[0x38 / 4] = 7;
    f.Add(DoMessage(control, s.message));
    s.message.answer[0x38 / 4] = 0xD;
    s.message.answer[0x28 / 4] = Ptr(&s.driven);
    for (uint32_t data3 : {5u, 0x10u}) {
        s.message.answer[0x4C / 4] = data3;
        for (uint32_t state : {Ptr(g_state), 0u}) {
            s.driven.answer[0xE4 / 4] = state;
            f.Add(DoMessage(control, s.message));
        }
    }
    // 0xB: the marker flagged on the overlay manager (an empty map here) and
    // the stopwatch restarted; stopped again right after (real time).
    At<void*>(control, 0x34) = &s.overlays;
    At<uint32_t>(control, 0x38) = 7;
    s.message.answer[0x38 / 4] = 0xB;
    f.Add(DoMessage(control, s.message));
    char* const stopwatch = control + 0x48;
    f.Add(Game<uint8_t>(addr::GZStopwatch_IsRunning, stopwatch));
    Game<void*>(addr::SC4Stopwatch_ConstructMode, stopwatch, uint32_t{4});
    f.Add(At<uint32_t>(stopwatch, 0));
    At<uint32_t>(control, 0x38) = 0;
    g_log = nullptr;
    log::Write("autotest: udrive2: messages %08X", f.h);
    out.Add(f.h);
}

// Asked only: the left button at a point off the screen, with the control's
// two filters made as Init makes them; then letting go of a driven
// stand-in.
void PartPicks(char* control, Stage& s, Fnv& out)
{
    Fnv f;
    g_log = &f;
    At<void*>(control, 0x1C) = &s.view;
    At<void*>(control, 0x9C) = &s.marker;
    void* filters[2] = {};
    const std::uintptr_t vtables[2][2] = {{addr::cSC4UDriveControl_FilterVtable, addr::cSC4UDriveControl_FilterVtable4},
                                          {0x00A8FE14u, 0x00A8FDFCu}};
    for (int i = 0; i < 2; ++i) {
        void* const filter = reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0xC);
        Game<void*>(addr::SC4_OccupantFilterBase, filter);
        At<std::uintptr_t>(filter, 0) = vtables[i][0];
        At<std::uintptr_t>(filter, 4) = vtables[i][1];
        Virtual<uint32_t>(filter, 0x4);
        filters[i] = filter;
        At<void*>(control, 0x84 + 4 * i) = filter;
    }
    At<void*>(control, 0x30) = nullptr;
    f.Add(Virtual<uint8_t>(control, 0x40, int32_t{-1000}, int32_t{-1000}, uint32_t{0}));
    At<void*>(control, 0x30) = &s.driven;
    s.driven.answer[0x7C / 4] = 1;
    f.Add(Virtual<uint8_t>(control, 0x40, int32_t{-1000}, int32_t{-1000}, uint32_t{0}));
    f.Bytes(s.marker.raw + 0x18, 0x10);
    for (int i = 0; i < 2; ++i) {
        At<void*>(control, 0x84 + 4 * i) = nullptr;
        Virtual<uint32_t>(filters[i], 0x8);
    }
    // Letting go of the driven stand-in, then the automaton gone for it.
    At<void*>(control, 0x8C) = &s.managers[0];
    s.driven.answer[0xEC / 4] = 0x10;
    for (uint32_t group : {0u, 1u}) {
        s.occupant.answer[0x58 / 4] = group;
        *reinterpret_cast<uint32_t*>(s.occupant.raw) = 0x4003;
        s.driven.answer[0x7C / 4] = 0;
        At<void*>(control, 0x30) = &s.driven;
        Game<void>(addr::cSC4UDriveControl_F562FD0, control);
        f.Add(At<void*>(control, 0x30) == nullptr);
    }
    At<void*>(control, 0x30) = &s.driven;
    Game<void>(addr::cSC4UDriveControl_OnAutomatonGone, control, static_cast<void*>(&s.driven));
    f.Add(s.driven.releases);
    g_log = nullptr;
    log::Write("autotest: udrive2: picks %08X", f.h);
    out.Add(f.h);
}

// Asked only, with OPENSC4_TEST_T8_COMMIT=1: Init, stop and pause, Shutdown
// on a second control with the city's real services.
void PartLife(Fnv& out)
{
    Fnv f;
    char* const control = MakeControl();
    At<void**>(control, 0) = reinterpret_cast<void**>(addr::cSC4UDriveControl_Vtable);
    f.Add(Virtual<uint8_t>(control, 0xC));
    AddFields(f, control);
    f.Add(Virtual<uint8_t>(control, 0xC)); // the second time does nothing
    Game<void>(addr::cSC4UDriveControl_StopAndPause, control);
    AddFields(f, control);
    f.Add(Virtual<uint8_t>(control, 0x10));
    AddFields(f, control);
    // The base's view, window manager and cursor stay referenced after our
    // Shutdown (the original clears the inited byte first): let them go.
    for (std::size_t offset : {std::size_t{0x20}, std::size_t{0x1C}, std::size_t{0x14}}) {
        if (void* const held = At<void*>(control, offset)) {
            Virtual<uint32_t>(held, 0x8);
            At<void*>(control, offset) = nullptr;
        }
    }
    Virtual<void*>(control, 0x68, uint32_t{1});
    log::Write("autotest: udrive2: life %08X", f.h);
    out.Add(f.h);
}

void Drive(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_T8") || !game::IsBuild641())
        return;
    if (!Global<void*>(addr::SC4_City)) {
        log::Write("autotest: udrive2: no city");
        return;
    }
    const bool commit = EnvInt("OPENSC4_TEST_T8_COMMIT", 0) != 0;
    int parts = EnvInt("OPENSC4_TEST_T8_PARTS", 63);
    if (!commit)
        parts &= 255;
    SetUpTables();
    g_unexpected = 0;
    Fnv total;

    char* const control = MakeControl();
    static Stage stage;
    SetUpStage(stage, control);
    if (parts & 1)
        PartSmall(control, stage, commit && (parts & 256), total);
    if (parts & 2) {
        PartRelease(control, stage, total);
        ClearStage(control);
    }
    if (parts & 8) {
        PartPanel(control, stage, total);
        ClearStage(control);
    }
    if (parts & 16) {
        PartKeys(control, stage, false, total);
        ClearStage(control);
    }
    if (parts & 64) {
        PartKeys(control, stage, true, total);
        ClearStage(control);
    }
    if (parts & 32) {
        PartMouse(control, stage, total);
        ClearStage(control);
    }
    if (parts & 128) {
        PartPicks(control, stage, total);
        ClearStage(control);
    }
    if (parts & 4) {
        PartMessages(control, stage, total);
        ClearStage(control);
    }
    // The cursor the base may hold is not ours to keep (slot 8 records here,
    // so none is expected).
    total.Add(At<void*>(control, 0x14) != nullptr);
    Virtual<void*>(control, 0x68, uint32_t{1});
    if (parts & 256)
        PartLife(total);
    total.Add(g_unexpected);
    std::snprintf(g_token, sizeof g_token, " udrive2 %08X", total.h);
    log::Write("autotest: udrive2: total %08X (parts %d, unexpected slots %d)", total.h, parts, g_unexpected);
}

const DayHook kUDrive2{"udrive2", 69, &Drive};

void Fingerprint(cISC4City*, std::string& line)
{
    line += g_token;
}

const FingerprintPart kFingerprint{"udrive2", 31, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
