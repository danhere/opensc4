// The view input control base class's part of the autotest
// (src/core/autotest_hooks.h; tool_base.cpp): drives the base class, the
// model holder and the tools' shared small functions and hashes what comes
// out, so that a run with ui.tools.base on and one with it off can be
// compared. Only the game's addresses and vtable slots are called, never
// our own functions.
//
//   OPENSC4_TEST_TOOL_BASE=<day>    token "toolbase", sub-hashes on the log
//     lines "autotest: toolbase: ...":
//
//   1. object: a bare base object (0x24 bytes of new memory filled with
//      0x5A, the constructor 0x005FB320): what the constructor writes and
//      leaves; AddRef and QueryInterface (slot 0, and the three shared
//      variants 0x004B8480, 0x004B9DE0 and 0x00661D70 by address) with good
//      and bad IDs, each good one released again (slot 2); the key slot
//      0x004BDBE0; the capture functions without a window; Init (slot 3)
//      twice, IsCurrent, SetCursor by ID (slot 8) with an unknown and a
//      known ID, SetCursor (slot 9) with null, the old cursor and the same
//      again, PlaySound (slot 28) with 0, PutAway (0x005FB0C0, only while
//      the view has no tool), Shutdown (slot 4) twice; then SetWindow (slot
//      10) with the view's window, Init, AmCapturing (slot 25) around
//      BeginCapture and EndCapture, a cursor change with the window set,
//      and the last Release, which destroys through slot 29 and slot 26;
//   2. destructors: the destructor 0x005FAFC0 and the deleting destructors
//      0x005FB1B0 and 0x004B7ED0 without and with the free bit, on base
//      objects whose vtable word was cleared; 0x004D7AA0 on a 0x38 byte
//      object holding two stand-in models that count their releases;
//   3. model: the holder's Construct, Show and Reset while empty, Make
//      (twice), Show 1, 1, 0, 0, 1, Reset; whether a model is held and
//      whether it says it is shown after each step;
//   4. grab: SetGrabOffset 0x004BA070 on a scratch block with a stand-in
//      figure whose occupant stands at the middle of the city; the two
//      offsets it stores;
//   5. tool: a bulldoze tool made the way the city view makes it (the
//      view's slot 0x64): IsCurrent, the base fields, the capture functions,
//      the cursor slots and PlaySound with 0 through the tool's vtable;
//      PutAway (0x005FB0C0) takes it off the view; the last Release
//      destroys it; two more tools that never saw the view go by Release at
//      a count of 0 and of 1.
//
// PlaySound is only called with 0 (no sound). Every pointer is left out of
// the hashes: only whether it is set, or an offset from the object.
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

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>

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

char g_token[40];

namespace field { // the base class (tool_base.cpp)
constexpr std::size_t kInited = 0x04, kCount = 0x08, kId = 0x0C, kCursorId = 0x10, kCursor = 0x14;
constexpr std::size_t kWindow = 0x18, kView = 0x1C, kManager = 0x20;
} // namespace field

constexpr std::size_t kViewWindow = 0xC; // the city view's window part
constexpr uint32_t kIdUnknown = 1, kIdControl = 0xC6BB8345, kIdTarget = 0x452294AA, kIdBad = 0x12345678;
constexpr uint32_t kArrowCursor = 0xC16C4E1B, kOtherCursor = 0xEBF9A440;
constexpr std::uintptr_t kFill = 0x5A5A5A5A;

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

// The base class's fields: the plain ones, and of the pointers whether they
// are set.
void HashBase(Fnv& f, char* object)
{
    f.Add(uint8_t{At<std::uintptr_t>(object, 0) == addr::SC4ViewInputControl_Vtable});
    f.Add(At<uint8_t>(object, field::kInited));
    f.Add(At<uint32_t>(object, field::kCount));
    f.Add(At<uint32_t>(object, field::kId));
    f.Add(At<uint32_t>(object, field::kCursorId));
    for (const std::size_t offset : {field::kCursor, field::kWindow, field::kView, field::kManager})
        f.Add(uint8_t{At<void*>(object, offset) != nullptr});
}

// A base object in 0x24 bytes of new memory filled with 0x5A.
char* NewBase(std::size_t size)
{
    char* const object = static_cast<char*>(Cdecl<void*>(addr::SC4_operator_new, static_cast<uint32_t>(size)));
    if (!object)
        return nullptr;
    std::memset(object, 0x5A, size);
    This<char*>(addr::SC4ViewInputControl_Construct, object);
    return object;
}

// One QueryInterface call (`function` 0: the object's slot 0): the answer,
// where the pointer handed out lies from the object (-1: not written), the
// count after; a reference handed out is given back through slot 2.
void Query(Fnv& f, std::uintptr_t function, char* object, uint32_t id)
{
    void* out = reinterpret_cast<void*>(kFill);
    const uint8_t answer =
        function ? This<uint8_t>(function, object, id, &out) : Virtual<uint8_t>(object, 0x0, id, &out);
    f.Add(answer);
    f.Add(out == reinterpret_cast<void*>(kFill) ? int32_t{-1}
                                                 : static_cast<int32_t>(static_cast<char*>(out) - object));
    f.Add(At<uint32_t>(object, field::kCount));
    if (answer)
        f.Add(Virtual<uint32_t>(object, 0x8));
}

// --- Stand-ins --------------------------------------------------------------------------

// A model that only counts its releases (the cheat tool's destructor body
// releases the two it holds).
uint32_t g_released = 0;
uint32_t __fastcall StandInNothing(void*, void*)
{
    return 0;
}
uint32_t __fastcall StandInRelease(void*, void*)
{
    return ++g_released;
}
void* const g_modelVtable[3] = {reinterpret_cast<void*>(&StandInNothing), reinterpret_cast<void*>(&StandInNothing),
                                reinterpret_cast<void*>(&StandInRelease)};
struct StandIn {
    void* const* vtable;
};

// A figure (slot 3: its occupant) and the occupant (slot 8: its position).
float g_position[3];
StandIn g_occupant;
void* __fastcall FigureOccupant(void*, void*)
{
    return &g_occupant;
}
void __fastcall OccupantPosition(void*, void*, float* out)
{
    out[0] = g_position[0];
    out[1] = g_position[1];
    out[2] = g_position[2];
}
void* const g_figureVtable[4] = {reinterpret_cast<void*>(&StandInNothing), reinterpret_cast<void*>(&StandInNothing),
                                 reinterpret_cast<void*>(&StandInNothing), reinterpret_cast<void*>(&FigureOccupant)};
void* const g_occupantVtable[9] = {
    reinterpret_cast<void*>(&StandInNothing), reinterpret_cast<void*>(&StandInNothing),
    reinterpret_cast<void*>(&StandInNothing), reinterpret_cast<void*>(&StandInNothing),
    reinterpret_cast<void*>(&StandInNothing), reinterpret_cast<void*>(&StandInNothing),
    reinterpret_cast<void*>(&StandInNothing), reinterpret_cast<void*>(&StandInNothing),
    reinterpret_cast<void*>(&OccupantPosition)};

// --- The sections -------------------------------------------------------------------------

uint32_t DriveObject(char* view)
{
    Fnv f;
    char* const object = NewBase(0x24);
    if (!object)
        return 0;
    HashBase(f, object);
    f.Add(At<uint32_t>(object, field::kManager)); // left as it was: the fill
    At<uint32_t>(object, field::kId) = 0x11112222;
    At<uint32_t>(object, field::kCursorId) = kArrowCursor;
    At<void*>(object, field::kManager) = nullptr;

    f.Add(Virtual<uint32_t>(object, 0x4)); // AddRef: ours
    for (const uint32_t id : {kIdUnknown, kIdControl, kIdTarget, uint32_t{0}})
        Query(f, 0, object, id);
    for (const uint32_t id : {kIdTarget, kIdUnknown, kIdControl, kIdBad})
        Query(f, addr::SC4ViewInputControl_QueryInterfaceSelfTarget, object, id);
    for (const uint32_t id : {kIdUnknown, kIdTarget, kIdControl, kIdBad})
        Query(f, addr::SC4ViewInputControl_QueryInterfaceTargetOnly, object, id);
    for (const uint32_t id : {kIdTarget, kIdUnknown, kIdControl, kIdBad})
        Query(f, addr::SC4ViewInputControl_QueryInterfaceTarget, object, id);
    f.Add(This<uint8_t>(addr::SC4ViewInputControl_OnKeyUnhandled, object, uint32_t{0x1B}, uint32_t{0}));
    f.Add(This<uint8_t>(addr::SC4ViewInputControl_OnKeyUnhandled, object, uint32_t{0x41}, uint32_t{3}));

    // No window yet: nothing to capture with.
    f.Add(Virtual<uint32_t>(object, 0x64));
    f.Add(This<uint32_t>(addr::SC4ViewInputControl_BeginCapture, object));
    f.Add(This<uint32_t>(addr::SC4ViewInputControl_EndCapture, object));

    f.Add(Virtual<uint8_t>(object, 0xC)); // Init
    HashBase(f, object);
    f.Add(uint8_t{At<void*>(object, field::kView) == Global<void*>(addr::SC4_Service43DC4)});
    f.Add(Virtual<uint8_t>(object, 0xC)); // Init again: nothing
    HashBase(f, object);
    f.Add(This<uint8_t>(addr::SC4ViewInputControl_IsCurrent, object));

    void* cursor = At<void*>(object, field::kCursor);
    Virtual<void>(object, 0x20, kIdBad); // SetCursor by ID
    HashBase(f, object);
    f.Add(uint8_t{At<void*>(object, field::kCursor) != cursor});
    Virtual<void>(object, 0x20, kOtherCursor);
    HashBase(f, object);
    f.Add(uint8_t{At<void*>(object, field::kCursor) != cursor});

    cursor = At<void*>(object, field::kCursor);
    if (cursor) {
        Virtual<uint32_t>(cursor, 0x4);
        Virtual<void>(object, 0x24, static_cast<void*>(nullptr)); // SetCursor
        HashBase(f, object);
        Virtual<void>(object, 0x24, cursor);
        HashBase(f, object);
        Virtual<void>(object, 0x24, cursor);
        f.Add(uint8_t{At<void*>(object, field::kCursor) == cursor});
        Virtual<uint32_t>(cursor, 0x8);
    }
    Virtual<void>(object, 0x70, uint32_t{0}); // PlaySound: no sound for 0
    const bool empty = Virtual<void*>(view, 0x60) == nullptr;
    f.Add(uint8_t{empty});
    if (empty)
        f.Add(This<uint8_t>(addr::SC4ViewInputControl_PutAway, object)); // the view has nothing to put away

    f.Add(Virtual<uint8_t>(object, 0x10)); // Shutdown
    HashBase(f, object);
    f.Add(Virtual<uint8_t>(object, 0x10));
    HashBase(f, object);
    f.Add(This<uint8_t>(addr::SC4ViewInputControl_PutAway, object)); // no view now: 0
    const uint32_t first = f.h;

    // With the view's window.
    Virtual<void>(object, 0x28, static_cast<void*>(view + kViewWindow)); // SetWindow
    f.Add(uint8_t{At<void*>(object, field::kWindow) == view + kViewWindow});
    f.Add(Virtual<uint8_t>(object, 0xC));
    HashBase(f, object);
    uint32_t capture[5];
    capture[0] = Virtual<uint32_t>(object, 0x64);
    capture[1] = This<uint8_t>(addr::SC4ViewInputControl_BeginCapture, object);
    capture[2] = Virtual<uint32_t>(object, 0x64);
    capture[3] = This<uint8_t>(addr::SC4ViewInputControl_EndCapture, object);
    capture[4] = Virtual<uint32_t>(object, 0x64);
    f.Add(capture);
    Virtual<void>(object, 0x20, kArrowCursor);
    HashBase(f, object);
    const uint32_t last = Virtual<uint32_t>(object, 0x8); // the last reference: Shutdown and the free
    f.Add(last);
    log::Write("autotest: toolbase: object %08X (no window %08X; capturing %u, begin %u, capturing %u, end %u, "
               "capturing %u; last release %u)",
               f.h, first, capture[0], capture[1], capture[2], capture[3], capture[4], last);
    return f.h;
}

uint32_t DriveDestructors()
{
    Fnv f;
    const auto restored = [&](char* object, bool same) {
        f.Add(uint8_t{same});
        f.Add(uint8_t{At<std::uintptr_t>(object, 0) == addr::SC4ViewInputControl_Vtable});
        At<std::uintptr_t>(object, 0) = 0;
    };
    char* const a = NewBase(0x24);
    char* const b = NewBase(0x24);
    char* const c = NewBase(0x38);
    if (!a || !b || !c)
        return 0;
    At<std::uintptr_t>(a, 0) = 0;
    restored(a, This<char*>(addr::SC4ViewInputControl_DeletingDestructor, a, uint32_t{0}) == a);
    f.Add(uint8_t{This<char*>(addr::SC4ViewInputControl_PlainDeletingDestructor, a, uint32_t{1}) == a});

    At<std::uintptr_t>(b, 0) = 0;
    This<void>(addr::SC4ViewInputControl_Destruct, b);
    restored(b, true);
    restored(b, This<char*>(addr::SC4ViewInputControl_PlainDeletingDestructor, b, uint32_t{0}) == b);
    f.Add(uint8_t{This<char*>(addr::SC4ViewInputControl_DeletingDestructor, b, uint32_t{1}) == b});

    StandIn first{g_modelVtable}, second{g_modelVtable};
    g_released = 0;
    At<std::uintptr_t>(c, 0) = 0;
    At<void*>(c, 0x28) = &first;
    At<void*>(c, 0x2C) = &second;
    restored(c, This<char*>(addr::SC4CheatTool_DeletingDestructor, c, uint32_t{0}) == c);
    f.Add(g_released);
    f.Add(uint8_t{At<void*>(c, 0x28) == &first && At<void*>(c, 0x2C) == &second});
    At<void*>(c, 0x28) = nullptr;
    At<void*>(c, 0x2C) = nullptr;
    f.Add(uint8_t{This<char*>(addr::SC4CheatTool_DeletingDestructor, c, uint32_t{1}) == c});
    f.Add(g_released);
    log::Write("autotest: toolbase: destructors %08X (stand-in releases %u)", f.h, g_released);
    return f.h;
}

uint32_t DriveModel()
{
    Fnv f;
    void* holder = reinterpret_cast<void*>(kFill);
    const auto state = [&] {
        f.Add(uint8_t{holder != nullptr});
        f.Add(uint8_t{holder ? Virtual<uint8_t>(holder, 0x14) : uint8_t{2}});
    };
    f.Add(uint8_t{This<void**>(addr::SC4ViewModel_Construct, &holder) == &holder});
    state();
    This<void>(addr::SC4ViewModel_Show, &holder, uint32_t{1}); // empty: nothing
    This<void>(addr::SC4ViewModel_Reset, &holder);
    state();
    This<void>(addr::SC4ViewModel_Make, &holder);
    state();
    void* const model = holder;
    This<void>(addr::SC4ViewModel_Make, &holder); // holds one: nothing
    f.Add(uint8_t{holder == model});
    for (const uint32_t show : {1u, 1u, 0u, 0u, 1u}) {
        This<void>(addr::SC4ViewModel_Show, &holder, show);
        state();
    }
    This<void>(addr::SC4ViewModel_Reset, &holder);
    state();
    log::Write("autotest: toolbase: model %08X (a model was made: %d)", f.h, model != nullptr);
    return f.h;
}

uint32_t DriveGrab(int32_t sizeX, int32_t sizeZ)
{
    Fnv f;
    if (!Global<void*>(addr::SC4_CameraView))
        return 0;
    char block[0x40];
    std::memset(block, 0x5A, sizeof block);
    g_occupant.vtable = g_occupantVtable;
    StandIn figure{g_figureVtable};
    g_position[0] = static_cast<float>(sizeX) * 8.0f;
    g_position[1] = 270.0f;
    g_position[2] = static_cast<float>(sizeZ) * 8.0f;
    This<void>(addr::SC4ViewInputControl_SetGrabOffset, block, static_cast<void*>(&figure), int32_t{100}, int32_t{-40});
    const int32_t x = At<int32_t>(block, 0x34), z = At<int32_t>(block, 0x38);
    f.Add(x);
    f.Add(z);
    At<uint32_t>(block, 0x34) = kFill;
    At<uint32_t>(block, 0x38) = kFill;
    for (const char byte : block)
        f.Add(byte); // nothing else written
    log::Write("autotest: toolbase: grab %08X (offset %d, %d)", f.h, x, z);
    return f.h;
}

char* NewTool()
{
    char* const tool = static_cast<char*>(Cdecl<void*>(addr::SC4_operator_new, 0xD8u));
    if (tool)
        This<void*>(addr::SC4ViewInputControls_F4B9070, tool);
    return tool;
}

// The base fields of a derived tool: as HashBase but for the vtable.
void HashTool(Fnv& f, char* tool)
{
    f.Add(At<uint8_t>(tool, field::kInited));
    f.Add(At<uint32_t>(tool, field::kCount));
    f.Add(At<uint32_t>(tool, field::kId));
    f.Add(At<uint32_t>(tool, field::kCursorId));
    for (const std::size_t offset : {field::kCursor, field::kWindow, field::kView})
        f.Add(uint8_t{At<void*>(tool, offset) != nullptr});
}

uint32_t DriveTool(char* view)
{
    Fnv f;
    const bool before = Virtual<void*>(view, 0x60) != nullptr;
    f.Add(uint8_t{before});
    char* const tool = NewTool();
    if (!tool)
        return 0;
    HashTool(f, tool);
    f.Add(Virtual<uint32_t>(tool, 0x4)); // AddRef: ours, kept until the end
    Virtual<uint8_t>(view, 0x64, static_cast<void*>(tool), int32_t{0});
    const bool current = Virtual<void*>(view, 0x60) == tool;
    f.Add(uint8_t{current});
    if (!current) {
        log::Write("autotest: toolbase: the view did not take the tool");
        return 0;
    }
    HashTool(f, tool);
    f.Add(This<uint8_t>(addr::SC4ViewInputControl_IsCurrent, tool));
    f.Add(uint8_t{At<void*>(tool, field::kWindow) == view + kViewWindow});
    f.Add(uint8_t{At<void*>(tool, field::kView) == Global<void*>(addr::SC4_Service43DC4)});
    f.Add(uint8_t{At<void*>(tool, field::kManager) != nullptr});
    for (const uint32_t id : {kIdUnknown, kIdControl, kIdTarget, kIdBad})
        Query(f, 0, tool, id);

    uint32_t capture[5];
    capture[0] = Virtual<uint32_t>(tool, 0x64);
    capture[1] = This<uint8_t>(addr::SC4ViewInputControl_BeginCapture, tool);
    capture[2] = Virtual<uint32_t>(tool, 0x64);
    capture[3] = This<uint8_t>(addr::SC4ViewInputControl_EndCapture, tool);
    capture[4] = Virtual<uint32_t>(tool, 0x64);
    f.Add(capture);

    void* const cursor = At<void*>(tool, field::kCursor);
    Virtual<void>(tool, 0x20, kIdBad);
    HashTool(f, tool);
    f.Add(uint8_t{At<void*>(tool, field::kCursor) != cursor});
    Virtual<void>(tool, 0x20, kOtherCursor);
    HashTool(f, tool);
    f.Add(uint8_t{At<void*>(tool, field::kCursor) != cursor});
    Virtual<void>(tool, 0x24, static_cast<void*>(nullptr));
    HashTool(f, tool);
    Virtual<void>(tool, 0x20, kArrowCursor);
    HashTool(f, tool);
    Virtual<void>(tool, 0x70, uint32_t{0}); // PlaySound: no sound for 0

    // Away, by the base's slot 27 (the bulldoze tool has its own there).
    const uint8_t away = This<uint8_t>(addr::SC4ViewInputControl_PutAway, tool);
    f.Add(away);
    const bool gone = Virtual<void*>(view, 0x60) != tool;
    f.Add(uint8_t{gone});
    f.Add(uint8_t{(Virtual<void*>(view, 0x60) != nullptr) == before});
    HashTool(f, tool);
    f.Add(uint8_t{At<void*>(tool, field::kManager) != nullptr});
    const uint32_t count = At<uint32_t>(tool, field::kCount);
    const uint32_t last = Virtual<uint32_t>(tool, 0x8); // ours was the last: destroyed
    f.Add(last);

    // Tools the view never saw: Release at a count of 0, and down from 2.
    uint32_t loose[3] = {9, 9, 9};
    if (char* const second = NewTool())
        loose[0] = Virtual<uint32_t>(second, 0x8);
    if (char* const third = NewTool()) {
        Virtual<uint32_t>(third, 0x4);
        Virtual<uint32_t>(third, 0x4);
        loose[1] = Virtual<uint32_t>(third, 0x8);
        loose[2] = Virtual<uint32_t>(third, 0x8);
    }
    f.Add(loose);
    log::Write("autotest: toolbase: tool %08X (view had a tool: %d; capturing %u, begin %u, capturing %u, end %u, "
               "capturing %u; put away %u, gone %d, count then %u, last release %u; loose %u, %u, %u)",
               f.h, before, capture[0], capture[1], capture[2], capture[3], capture[4], away, gone, count, last,
               loose[0], loose[1], loose[2]);
    return f.h;
}

void Drive(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_TOOL_BASE") || !game::IsBuild641())
        return;
    char* const view = CityView();
    void* const city = Global<void*>(addr::SC4_City);
    if (!view || !city) {
        log::Write("autotest: toolbase: no city view");
        return;
    }
    const int32_t sizeX = Virtual<int32_t>(city, 0x174), sizeZ = Virtual<int32_t>(city, 0x178);
    Fnv all;
    all.Add(DriveObject(view));
    all.Add(DriveDestructors());
    all.Add(DriveModel());
    all.Add(DriveGrab(sizeX, sizeZ));
    all.Add(DriveTool(view));
    std::snprintf(g_token, sizeof g_token, " toolbase %08X", all.h);
}

const DayHook kToolBase{"toolbase", 57, &Drive};

void Fingerprint(cISC4City*, std::string& line)
{
    line += g_token;
}

const FingerprintPart kFingerprint{"toolbase", 15, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
