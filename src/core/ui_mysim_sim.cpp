// What was left of cSC4MySim's interface side, and the paced advisor head
// view. Replacement, off by default:
//
//   ui.mysim_panel.sim: the cISC4MySim methods the MySim panel calls (Find,
//     ShowResidence, ShowLocation, GetBitmapBuffer), the makers of the home
//     and job tools, the building filter of TestForResidence, the two script
//     setters with a register convention, the MySim's advice class (factory
//     and the script runner), and the head view class at 0x0054A5F0.
//
// The cISC4MySim methods take that part (object +0xB8) as `this`, as in
// mysim_agents.cpp; the tool makers take the object's start.
//
// The MySim's advice (0xC0 bytes, vtables 0x00A84B90, 0x00A84B78 at +4 and
// 0x00A84B60 at +0xC; class ID from 0x00424AE0) is cSC4Advice with one slot
// changed: before a script runs, the MySim that owns the advice makes its
// own script context the current one.
//
// The paced head view (0x110 bytes, vtables 0x00A9B768 and 0x00A9B758 at
// +4) is the advisor head view of ui_advice_text_2.cpp with a stopwatch at
// +0xF8. It does not ask for frame ticks: while active its stopwatch runs,
// and each draw that comes 25 or more after the last advances the model by
// the time passed before drawing. The drawing itself (0x004209D0) stays the
// game's.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace opensc4::ui {
namespace {

namespace field { // from the cISC4MySim part
constexpr std::size_t kState = 0x8;
constexpr std::size_t kResidence = 0xC;
constexpr std::size_t kJob = 0x28;
constexpr std::size_t kJobPending = 0x2C;
constexpr std::size_t kBitmap = 0x74;        // the picture's ID
constexpr std::size_t kBitmapFile = 0x78;    // SC4String: the picture's file
constexpr std::size_t kResidenceView = 0x8C; // the info tip over the home
constexpr std::size_t kResidenceShown = 0x90;
constexpr std::size_t kPendingTitle = 0xBC;  // a string, read by Find's message
constexpr std::size_t kDispatch = 0x154;
constexpr std::size_t kLocationShown = 0x158;
} // namespace field

namespace view { // the head view
constexpr std::size_t kSetUp = 0xC;
constexpr std::size_t kActive = 0xD;
constexpr std::size_t kWindow = 0x28;
constexpr std::size_t kWatch = 0xF8;
} // namespace view

constexpr std::size_t kAdvisorPart = 0xB0; // the advisor interface, below the cISC4MySim part
constexpr std::size_t kDispatchHere = 0x5C; // the dispatch's current place

constexpr uint32_t kBuildingType = 0x278128A0;
constexpr uint32_t kBuildingIid = 0x87DDA3A9;
constexpr uint32_t kPng = 0x856DDBAC;
constexpr uint32_t kUiGroup = 0x46A006B0;
constexpr uint32_t kTextGroup = 0xEA231E96;

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

// A string from the text resources (0x20 bytes; the larger of the frame
// slots the original gives one is 0x28).
struct LocalizedString {
    GameString text;
    uint32_t rest[5];
};
static_assert(sizeof(LocalizedString) == 0x28);

// An image by resource key, loaded on demand (0x30 bytes).
struct ImageLoader {
    std::uintptr_t vtable;
    void* image;
    uint32_t key[3];
    GameString name;
    uint32_t mode;
    uint8_t flag;
};
static_assert(sizeof(ImageLoader) == 0x30);

// SC4MySimPlace: where a MySim is or goes to; the occupant first.
struct Place {
    void* object;
    uint32_t rest[9];
};
static_assert(sizeof(Place) == 0x28);

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

void Free(void* memory)
{
    Cdecl<void>(addr::SC4_Free, memory);
}

// An empty string, as the game builds one inline.
void InitString(GameString& text)
{
    text.body = {nullptr, nullptr, nullptr};
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    text.body.begin = buffer;
    text.body.end = buffer;
    text.body.capacity = buffer + 8;
    *buffer = 0;
    text.vtable = addr::SC4String_Vtable;
    text.zero = 0;
}

void DestroyString(GameString& text)
{
    char* const buffer = text.body.begin;
    text.vtable = addr::SC4String_Vtable;
    if (buffer)
        Free(buffer);
}

void MakeLocalized(LocalizedString& text, uint32_t group, uint32_t instance)
{
    This<void*>(addr::SC4LocalizedString_SC4LocalizedString, &text, group, instance, uint32_t{1});
}

// --- The building filter --------------------------------------------------------------

// The game's 0x00424F30. `this` is the filter TestForResidence makes
// (0x10 bytes, vtable 0x00A84B44, slot 3; +0xC the flag its constructor
// 0x00424AB0 was given). True for a building occupant whose lot's slot
// 0x8C value is at most 3 (or that has no lot), that is complete (its
// percentage at least 1) and, with the flag, whose profile starts with 1.
uint8_t __fastcall HomeFilter(char* self, void*, void* occupant)
{
    uint8_t result = 0;
    if (Virtual<uint32_t>(occupant, 0x1C) != kBuildingType)
        return result;
    void* building = nullptr;
    if (Virtual<uint8_t>(occupant, 0x0, kBuildingIid, &building)) {
        void* const lots = Global<void*>(addr::SC4_LotManager);
        void* const lot = Virtual<void*>(lots, 0x38, Virtual<void*>(building, 0xC));
        if (!lot || Virtual<uint8_t>(lot, 0x8C) <= 3) {
            if (*Virtual<const float*>(building, 0x24) >= 1.0f) {
                const int32_t* const profile = Virtual<const int32_t*>(building, 0x2C);
                if (!At<uint8_t>(self, 0xC) || *profile == 1)
                    result = 1;
            }
        }
    }
    if (building)
        Release(building);
    return result;
}

// --- The MySim's advice ---------------------------------------------------------------

// The game's 0x004250C0 (cdecl, no arguments): the class factory of the
// MySim's advice.
void* __cdecl MakeAdvice()
{
    char* const advice = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0xC0});
    if (!advice)
        return nullptr;
    This<void*>(addr::SC4Advice_Construct, advice);
    At<std::uintptr_t>(advice, 0x0) = addr::SC4MySimAdvice_Vtable;
    At<std::uintptr_t>(advice, 0x4) = addr::SC4MySimAdvice_Vtable4;
    At<std::uintptr_t>(advice, 0xC) = addr::SC4MySimAdvice_VtableC;
    return advice;
}

// The game's 0x00425100. `this` is the advice (main slot 22). The advisor
// with the advice's slot 11 ID is taken for a MySim and told to make its
// script context current (cISC4MySim slot 0x110); then the base class runs
// the script text. An ID the advisor system does not know is not checked:
// the original then reads a vtable from address 0xB8.
uint8_t __fastcall AdviceRunScript(char* self, void*, void* text)
{
    void* const advisors = Virtual<void*>(Global<void*>(addr::SC4_City), 0x144);
    char* const advisor = Virtual<char*>(advisors, 0x1C, Virtual<uint32_t>(self, 0x2C));
    const std::uintptr_t object = advisor ? reinterpret_cast<std::uintptr_t>(advisor) - 8 : 0;
    Virtual<void>(reinterpret_cast<void*>(object + 0xB8), 0x110);
    return This<uint8_t>(addr::cSC4Advice_F407ED0, self, text);
}

// --- The script setters ---------------------------------------------------------------

// The game's 0x00425B80: script in ESI, name in ECX, object in EDI, no
// stack arguments. Sets a script global to an object (one more reference
// on it, taken before the script gets it).
void __cdecl SetScriptObject(void* script, const char* name, void* object)
{
    Virtual<void>(script, 0x84, name);
    AddRef(object);
    void* const advisors = Virtual<void*>(Global<void*>(addr::SC4_City), 0x144);
    const uint32_t kind = Virtual<uint32_t>(advisors, 0x58, uint32_t{1});
    Virtual<void>(script, 0x114, object, kind);
    Virtual<void>(script, 0xB8, int32_t{-3});
}

__declspec(naked) void SetScriptObjectThunk()
{
    __asm push edi
    __asm push ecx
    __asm push esi
    __asm call SetScriptObject
    __asm add esp, 12
    __asm ret
}

// The game's 0x00425BD0: script in ESI, name in ECX, occupant in EAX, no
// stack arguments. Sets a script global to a location object (class
// 0x6A26D049) holding the occupant, or to nil without one.
void __cdecl SetScriptLocation(void* script, const char* name, void* occupant)
{
    Virtual<void>(script, 0x84, name);
    if (!occupant) {
        Virtual<void>(script, 0x114, nullptr, uint32_t{0});
        Virtual<void>(script, 0xB8, int32_t{-3});
        return;
    }
    void* location = nullptr;
    void* const com = Cdecl<void*>(addr::RZGetCOM);
    if (Virtual<uint8_t>(com, 0xC, uint32_t{0x6A26D049}, uint32_t{0x6A26D050}, &location)) {
        Virtual<void>(location, 0x1C, occupant);
        AddRef(location);
        void* const advisors = Virtual<void*>(Global<void*>(addr::SC4_City), 0x144);
        void* const held = location;
        const uint32_t kind = Virtual<uint32_t>(advisors, 0x58, uint32_t{0});
        Virtual<void>(script, 0x114, held, kind);
    }
    if (location)
        Release(location);
    Virtual<void>(script, 0xB8, int32_t{-3});
}

__declspec(naked) void SetScriptLocationThunk()
{
    __asm push eax
    __asm push ecx
    __asm push esi
    __asm call SetScriptLocation
    __asm add esp, 12
    __asm ret
}

// --- The tool makers ------------------------------------------------------------------

// A tool of 0x4C bytes made, set up with the MySim, handed to the city view
// (0x00B43DC4 slot 0x64 with 2) and kept in the global 0x00B20E0C. When the
// allocation fails the original still calls the setup, with no tool.
uint8_t MakeTool(char* mysim, std::uintptr_t construct, std::uintptr_t setup)
{
    void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x4C});
    void* const tool = memory ? This<void*>(construct, memory) : nullptr;
    if (tool)
        AddRef(tool);
    if (!This<uint8_t>(setup, tool, mysim)) {
        if (tool)
            Release(tool);
        return 0;
    }
    Virtual<void>(Global<void*>(addr::SC4_Service43DC4), 0x64, tool, uint32_t{2});
    Assign(Global<void*>(addr::SC4_DispatchTool), tool);
    if (tool)
        Release(tool);
    return 1;
}

// The game's 0x004268C0. `this` is the cSC4MySim object's start.
uint8_t __fastcall MakeHomeTool(char* mysim, void*)
{
    return MakeTool(mysim, addr::SC4UI_F437AF0, addr::SC4UI_F438390);
}

// The game's 0x00426970. `this` is the cSC4MySim object's start.
uint8_t __fastcall MakeJobTool(char* mysim, void*)
{
    return MakeTool(mysim, addr::SC4UI_F438740, addr::SC4UI_F438860);
}

// --- cISC4MySim: the panel's methods --------------------------------------------------

// The game's 0x004293B0 (slot 31, Find): moves the camera to the MySim. At
// work (state 3) that is the job's building, at home (2) the residence,
// otherwise the occupant of the dispatch's current place. At work with no
// job but one pending, a message box says so instead and the camera stays.
// With the preference at +0x4BC the camera first zooms in to 4. The
// dispatch's byte +0x10 is set in every case that got as far as a camera.
void __fastcall Find(char* self, void*)
{
    const uint32_t origin = Global<uint32_t>(addr::cSC4MySim_FindOrigin);
    uint32_t position[3] = {origin, origin, origin}; // three floats
    void* const cityView = Global<void*>(addr::SC4_View43DD0);
    if (!cityView)
        return;
    void* const camera = Virtual<void*>(cityView, 0x40);
    if (!camera)
        return;
    const int32_t state = At<int32_t>(self, field::kState);
    char* const app = Global<char*>(addr::SC4_App43CC4);

    void* building = nullptr;
    bool lookUp = true; // false: at work, no job and none pending
    if (state == 3) {
        building = At<void*>(self, field::kJob);
        if (!building) {
            if (At<void*>(self, field::kJobPending)) {
                GameString name;
                This<void*>(addr::SC4String_FromIGZString, &name, Virtual<void*>(self - kAdvisorPart, 0x20));
                LocalizedString titleFormat;
                MakeLocalized(titleFormat, kTextGroup, 0x2A7877E7);
                LocalizedString textFormat;
                MakeLocalized(textFormat, kTextGroup, 0x0A7877F2);
                GameString title;
                This<void*>(addr::SC4String_SC4String, &title);
                GameString text;
                This<void*>(addr::SC4String_SC4String, &text);
                char* const nameText = name.body.begin;
                Cdecl<void>(addr::cRZBaseString_Sprintf, &title, titleFormat.text.body.begin, nameText);
                Cdecl<void>(addr::cRZBaseString_Sprintf, &text, textFormat.text.body.begin, nameText,
                            At<char*>(At<char*>(self, field::kPendingTitle), 4));
                Cdecl<void>(addr::SC4UI_MessageDialog, &title, &text);
                This<void>(addr::SC4String_Destroy, &text);
                This<void>(addr::SC4String_Destroy, &title);
                This<void>(addr::SC4String_Destroy, &textFormat);
                This<void>(addr::SC4String_Destroy, &titleFormat);
                This<void>(addr::SC4String_Destroy, &name);
                At<uint8_t>(At<char*>(self, field::kDispatch), 0x10) = 1;
                return;
            }
            lookUp = false;
        }
    } else if (state == 2) {
        building = At<void*>(self, field::kResidence);
    }

    if (building) {
        void* const occupant = Virtual<void*>(building, 0xC);
        Virtual<void>(occupant, 0x20, static_cast<void*>(position));
    } else if (lookUp) {
        Place place;
        This<void*>(addr::SC4MySimPlace_Copy, &place,
                    static_cast<void*>(At<char*>(self, field::kDispatch) + kDispatchHere));
        if (void* const occupant = place.object) {
            Virtual<void>(occupant, 0x20, static_cast<void*>(position));
            Release(occupant);
        }
    }

    if (app && At<uint8_t>(At<char*>(app, 0xC), 0x4BC) && static_cast<double>(At<int32_t>(camera, 0x108)) < 4.0)
        This<void>(addr::cSC4CameraControl_SetZoom, camera, 4.0f);
    This<void>(addr::cSC4CameraControl_SetCenter, camera, static_cast<void*>(position));
    At<uint8_t>(At<char*>(self, field::kDispatch), 0x10) = 1;
}

// The game's 0x00429590 (slot 32, ShowResidence): shows the info tip over
// the MySim's home, made on first use (with the picture 0x14416190, as a
// child of 0x00B43DC8's window), placed by UpdateResidence (slot 37) and
// titled with the MySim's name in the text 0xEA4018D3. Nothing for a MySim
// that has not moved in or has no home.
void __fastcall ShowResidence(char* self, void*)
{
    GameString title;
    InitString(title);
    if (!At<int32_t>(self, field::kState) || !At<void*>(self, field::kResidence)) {
        char* const buffer = title.body.begin;
        title.vtable = addr::SC4String_Vtable;
        Free(buffer);
        return;
    }

    void*& tip = At<void*>(self, field::kResidenceView);
    if (!tip) {
        void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x1B4});
        This<void*>(addr::SC4RefPtr_Assign, &tip, memory ? This<void*>(addr::SC4InfoTip_Construct, memory) : nullptr);
        Virtual<void>(tip, 0x2C, uint32_t{0xA85F1A83});
        Virtual<void>(tip, 0x30, uint32_t{0});
        uint32_t key[3] = {kPng, kUiGroup, 0x14416190};
        ImageLoader icon;
        This<void*>(addr::SC4ImageLoader_ConstructKey, &icon, static_cast<uint32_t*>(key), uint32_t{1}, uint32_t{0});
        if (icon.image)
            Virtual<void>(tip, 0x10, icon.image, int32_t{-1}, int32_t{-1});
        void* const parent = Global<void*>(addr::SC4_Service43DC8);
        Virtual<void>(parent, 0x38, Virtual<void*>(tip, 0xC));
        This<void>(addr::SC4ImageLoader_Destruct, &icon);
    }

    Virtual<void>(self, 0x94); // UpdateResidence
    LocalizedString format;
    MakeLocalized(format, kTextGroup, 0xEA4018D3);
    void* const name = Virtual<void*>(self - kAdvisorPart, 0x20);
    const char* const nameText = Virtual<const char*>(name, 0x14);
    const char* const formatText = This<const char*>(addr::cRZBaseString_Data, &format);
    Cdecl<void>(addr::cRZBaseString_Sprintf, &title, formatText, nameText);
    Virtual<void>(tip, 0x20, &title);
    Virtual<void>(Virtual<void*>(tip, 0xC), 0x114);
    Virtual<void>(Virtual<void*>(tip, 0xC), 0x60);
    At<uint8_t>(self, field::kResidenceShown) = 1;
    DestroyString(format.text);
    DestroyString(title);
}

// The game's 0x0042BDB0 (slot 24, GetBitmapBuffer): the MySim's picture as
// a buffer, counted once for the caller. With a picture ID (slot 22) it is
// the PNG of that instance in the interface group, asked for its interface
// 0x20732180; otherwise the picture's file (slot 23) is read by an object
// of class 0xA2FFB5D3. False without a buffer.
uint8_t __fastcall GetBitmapBuffer(char* self, void*, void** out)
{
    void* resource = nullptr;
    void* buffer = nullptr;
    if (Virtual<uint32_t>(self, 0x58)) {
        void* const resources = Global<void*>(addr::SC4_ResourceManager);
        uint32_t key[3];
        key[2] = Virtual<uint32_t>(self, 0x58);
        key[0] = kPng;
        key[1] = kUiGroup;
        if (!Virtual<uint8_t>(resources, 0xC, static_cast<uint32_t*>(key), uint32_t{0x06F1568E}, &resource, uint32_t{0},
                              uint32_t{0})) {
            if (buffer)
                Release(buffer);
            if (resource)
                Release(resource);
            return 0;
        }
        Virtual<void>(resource, 0x10, uint32_t{0x20732180}, &buffer);
    } else {
        char* const file = Virtual<char*>(self, 0x5C);
        GameString path;
        uint32_t tag; // the original passes the address of its own argument here
        This<void*>(addr::SC4String_CopyBuffer, &path.body, static_cast<void*>(file ? file + 4 : nullptr), uint32_t{0},
                    uint32_t{0xFFFFFFFF}, &tag);
        path.vtable = addr::SC4String_Vtable;
        path.zero = 0;
        if (path.body.begin != path.body.end) {
            // Not set before the call, as in the original: what the class
            // lookup leaves here when it fails is what gets tested.
            void* reader;
            Virtual<uint8_t>(Cdecl<void*>(addr::RZGetCOM), 0xC, uint32_t{0xA2FFB5D3}, uint32_t{0x02FFBB60}, &reader);
            if (!reader) {
                DestroyString(path);
                if (buffer)
                    Release(buffer);
                if (resource)
                    Release(resource);
                return 0;
            }
            Virtual<void>(reader, 0x10, &path, &buffer, uint32_t{8});
            Release(reader);
        }
        DestroyString(path);
    }

    if (!buffer) {
        if (resource)
            Release(resource);
        return 0;
    }
    *out = buffer;
    AddRef(buffer);
    if (buffer)
        Release(buffer);
    if (resource)
        Release(resource);
    return 1;
}

// The game's 0x0042C820 (slot 34, ShowLocation): has the dispatch show the
// MySim where it is, with its picture: the picture's ID, or without one the
// text 0x00B43C94's slot 0xF4 gives followed by the picture's file name.
// Nothing for a MySim that has not moved in.
void __fastcall ShowLocation(char* self, void*)
{
    if (!At<int32_t>(self, field::kState))
        return;
    GameString file;
    InitString(file);
    if (!At<uint32_t>(self, field::kBitmap)) {
        Virtual<void>(Global<void*>(addr::SC4_Service43C94), 0xF4, &file);
        const StringBody& name = At<StringBody>(self, field::kBitmapFile + 4);
        uint8_t tag;
        This<void*>(addr::SC4String_AppendRange, &file.body, name.begin, name.end, &tag);
    }
    This<void>(addr::cSC4MySimDispatch_ShowLocation, At<void*>(self, field::kDispatch),
               At<uint32_t>(self, field::kBitmap), &file);
    At<uint8_t>(self, field::kLocationShown) = 1;
    DestroyString(file);
}

// --- The paced head view --------------------------------------------------------------

// The game's 0x0054A5F0. `this` is the view's memory (0x110 bytes). The
// constructor, for an advisor type.
char* __fastcall PacedConstruct(char* self, void*, uint32_t advisorType)
{
    This<void*>(addr::SC4UI_F420B70, self, advisorType);
    At<std::uintptr_t>(self, 0x0) = addr::SC4UI_PacedHeadViewVtable;
    At<std::uintptr_t>(self, 0x4) = addr::SC4UI_PacedHeadViewVtable4;
    This<void*>(addr::SC4Stopwatch_Construct, self + view::kWatch, uint32_t{4});
    return self;
}

// The game's 0x0054A620. `this` is the view (slot 6). Turns the view on or
// off: the stopwatch started or stopped, the models loaded or dropped
// (slot 11).
void __fastcall PacedSetActive(char* self, void*, uint32_t active)
{
    const uint8_t on = static_cast<uint8_t>(active);
    if (At<uint8_t>(self, view::kActive) == on)
        return;
    At<uint8_t>(self, view::kActive) = on;
    if (on) {
        This<void>(addr::SC4Stopwatch_Start, self + view::kWatch);
        Virtual<void>(self, 0x2C, int32_t{1});
    } else {
        This<void>(addr::SC4Stopwatch_Stop, self + view::kWatch);
        Virtual<void>(self, 0x2C, int32_t{0});
    }
}

// The game's 0x0054A660. `this` is the view (slot 12, the base class's
// drawing slot). When the stopwatch shows 25 or more, the model advances
// (slot 13) by that time, given as the bits of the float, and the stopwatch
// starts again from zero after the drawing.
void __fastcall PacedDraw(char* self, void*)
{
    char* const watch = self + view::kWatch;
    float elapsed;
    This<void>(addr::SC4Stopwatch_GetElapsed, watch, &elapsed);
    if (elapsed >= 25.0f) {
        This<void>(addr::SC4Stopwatch_Stop, watch);
        uint32_t bits;
        std::memcpy(&bits, &elapsed, sizeof(bits));
        Virtual<void>(self, 0x34, bits);
        This<void>(addr::SC4UI_F4209D0, self);
        This<void>(addr::GZStopwatch_Start, watch);
    } else {
        This<void>(addr::SC4UI_F4209D0, self);
    }
}

// The game's 0x0054A6C0. `this` is the view (slot 5). The deleting
// destructor.
char* __fastcall PacedDeletingDestruct(char* self, void*, uint32_t flags)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4UI_PacedHeadViewVtable;
    At<std::uintptr_t>(self, 0x4) = addr::SC4UI_PacedHeadViewVtable4;
    This<void>(addr::SC4Stopwatch_Destruct, self + view::kWatch);
    This<void>(addr::SC4UI_F420270, self);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// The game's 0x0054A700. `this` is the view. Set up for a window, called
// by the owner instead of through a window message: keeps the window, then
// the base class's set up. True at once when set up already.
uint8_t __fastcall PacedInit(char* self, void*, void* window)
{
    if (At<uint8_t>(self, view::kSetUp))
        return 1;
    Assign(At<void*>(self, view::kWindow), window);
    return This<uint8_t>(addr::SC4UI_F4202F0, self, window);
}

bool InstallMySimSim(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[20] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F424F30, reinterpret_cast<void*>(&HomeFilter)},
        {addr::SC4UI_F4250C0, reinterpret_cast<void*>(&MakeAdvice)},
        {addr::SC4UI_F425100, reinterpret_cast<void*>(&AdviceRunScript)},
        {addr::SC4UI_F425B80, reinterpret_cast<void*>(&SetScriptObjectThunk)},
        {addr::SC4UI_F425BD0, reinterpret_cast<void*>(&SetScriptLocationThunk)},
        {addr::cSC4MySim_MakeHomeTool, reinterpret_cast<void*>(&MakeHomeTool)},
        {addr::cSC4MySim_MakeJobTool, reinterpret_cast<void*>(&MakeJobTool)},
        {addr::SC4UI_F4293B0, reinterpret_cast<void*>(&Find)},
        {addr::SC4UI_F429590, reinterpret_cast<void*>(&ShowResidence)},
        {addr::SC4UI_F42BDB0, reinterpret_cast<void*>(&GetBitmapBuffer)},
        {addr::SC4UI_F42C820, reinterpret_cast<void*>(&ShowLocation)},
        {addr::SC4UI_F54A5F0, reinterpret_cast<void*>(&PacedConstruct)},
        {addr::SC4UI_F54A620, reinterpret_cast<void*>(&PacedSetActive)},
        {addr::SC4UI_F54A660, reinterpret_cast<void*>(&PacedDraw)},
        {addr::SC4UI_F54A6C0, reinterpret_cast<void*>(&PacedDeletingDestruct)},
        {addr::SC4UI_F54A700, reinterpret_cast<void*>(&PacedInit)},
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

const bool g_mySimSimRegistered = registry::Register({
    "ui.mysim_panel.sim",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F4293B0,
    "Our MySim panel methods, tool makers, script setters, advice class and paced head view (docs/ui.md)",
    false,
    InstallMySimSim,
});

} // namespace
} // namespace opensc4::ui
