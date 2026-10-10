// The first two MySim tools (0x00437AF0 to 0x00439380): view input controls
// the MySim panel puts on the city view to pick a building for a MySim. The
// home tool (vtable 0x00A85800, made by cSC4MySim 0x004268C0) offers houses
// the MySim could move to and changes the residence on a click; the job tool
// (vtable 0x00A858D0, made by 0x00426970) offers open workplaces and changes
// the job. Both are 0x4C bytes over the view input control base class and
// share Shutdown and the mouse-exit handler. Replacement, off by default:
//
//   ui.mysim_tools.part1: the constructors, destructors, Shutdown, the setup
//     with the MySim, the mouse handlers and the icon chosen for a building.
//
// While the cursor moves, the tool highlights the building under it, moves
// one of two small windows with the MySim's picture to the cursor (one for
// "yes", one for "no") and fills an info window next to it. Windows, images
// and highlights are the game's services, called through their vtables.
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

namespace field { // from the tool's start; up to +0x24 is the base class
constexpr std::size_t kView = 0x1C;       // the city view (cISC4View3DWin)
constexpr std::size_t kMySim = 0x28;      // the cSC4MySim object
constexpr std::size_t kBuilding = 0x2C;   // the building under the cursor, highlighted
constexpr std::size_t kHighlight = 0x30;  // that building's highlight before ours
constexpr std::size_t kAgents = 0x34;     // the MySim agent simulator (city slot 0x11C)
constexpr std::size_t kTip = 0x38;        // the info window, made on first use
constexpr std::size_t kYesWindow = 0x3C;  // picture window 0x27DF05BE, shown over a good building
constexpr std::size_t kNoWindow = 0x40;   // its sibling 0x27DF05BF, shown elsewhere
constexpr std::size_t kHeight = 0x44;     // of the picture window
constexpr std::size_t kHalfWidth = 0x48;
} // namespace field

constexpr std::size_t kMySimPart = 0xB8; // cISC4MySim inside cSC4MySim

constexpr uint32_t kBuildingType = 0x278128A0;
constexpr uint32_t kBuildingIid = 0x87DDA3A9;
constexpr uint32_t kPng = 0x856DDBAC;
constexpr uint32_t kUiGroup = 0x46A006B0;

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

// An image by resource key or by file name, loaded on demand (0x30 bytes,
// vtable 0x00A856CC).
struct ImageLoader {
    std::uintptr_t vtable;
    void* image;
    uint32_t key[3];
    GameString name;
    uint32_t mode;
    uint8_t flag;
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
    text.vtable = addr::SC4String_Vtable;
    if (text.body.begin)
        Free(text.body.begin);
}

char* MySimPart(char* self)
{
    return At<char*>(self, field::kMySim) + kMySimPart;
}

// The base class and the nine fields of either tool.
char* Construct(char* self, std::uintptr_t vtable)
{
    This<void*>(addr::SC4ViewInputControl_Construct, self);
    At<std::uintptr_t>(self, 0) = vtable;
    At<void*>(self, field::kMySim) = nullptr;
    At<void*>(self, field::kBuilding) = nullptr;
    At<uint32_t>(self, field::kHighlight) = 0;
    At<void*>(self, field::kAgents) = nullptr;
    At<void*>(self, field::kTip) = nullptr;
    At<void*>(self, field::kYesWindow) = nullptr;
    At<void*>(self, field::kNoWindow) = nullptr;
    At<int32_t>(self, field::kHeight) = 0;
    At<int32_t>(self, field::kHalfWidth) = 0;
    return self;
}

void Destruct(char* self, std::uintptr_t vtable)
{
    At<std::uintptr_t>(self, 0) = vtable;
    for (const std::size_t at : {field::kNoWindow, field::kYesWindow, field::kTip, field::kAgents,
                                 field::kBuilding, field::kMySim}) {
        if (void* const object = At<void*>(self, at))
            Release(object);
    }
    This<void>(addr::SC4ViewInputControl_Destruct, self);
}

// The game's 0x00437AF0: the home tool's constructor.
char* __fastcall HomeConstruct(char* self, void*)
{
    return Construct(self, addr::SC4UI_MySimHomeToolVtable);
}

// The game's 0x00437B20: the home tool's destructor.
void __fastcall HomeDestruct(char* self, void*)
{
    Destruct(self, addr::SC4UI_MySimHomeToolVtable);
}

// The game's 0x00437CF0: the home tool's deleting destructor (slot 26).
char* __fastcall HomeDelete(char* self, void*, uint32_t flags)
{
    HomeDestruct(self, nullptr);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// The game's 0x00438740: the job tool's constructor.
char* __fastcall JobConstruct(char* self, void*)
{
    return Construct(self, addr::SC4UI_MySimJobToolVtable);
}

// The game's 0x00438770: the job tool's destructor.
void __fastcall JobDestruct(char* self, void*)
{
    Destruct(self, addr::SC4UI_MySimJobToolVtable);
}

// The game's 0x00438840: the job tool's deleting destructor (slot 26).
char* __fastcall JobDelete(char* self, void*, uint32_t flags)
{
    JobDestruct(self, nullptr);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// The highlighted building gets its old highlight back and is let go.
void DropBuilding(char* self)
{
    if (void* const building = At<void*>(self, field::kBuilding)) {
        Virtual<void>(Virtual<void*>(building, 0xC), 0x44, At<uint32_t>(self, field::kHighlight), uint32_t{0});
        Clear(At<void*>(self, field::kBuilding));
    }
}

// The game's 0x00437B80: Shutdown (slot 4) of both tools and of the tool with
// vtable 0x00A85A20.
uint8_t __fastcall Shutdown(char* self, void*)
{
    Clear(At<void*>(self, field::kMySim));
    DropBuilding(self);
    Clear(At<void*>(self, field::kAgents));
    if (void* const tip = At<void*>(self, field::kTip)) {
        Virtual<void>(Virtual<void*>(tip, 0xC), 0x118);
        void* const held = At<void*>(self, field::kTip);
        void* const parent = Virtual<void*>(At<void*>(self, field::kView), 0xC);
        Virtual<void>(parent, 0x40, Virtual<void*>(held, 0xC));
        Clear(At<void*>(self, field::kTip));
    }
    if (void* const yes = At<void*>(self, field::kYesWindow)) {
        void* const parent = Virtual<void*>(At<void*>(self, field::kView), 0xC);
        Virtual<void>(parent, 0x40, Virtual<void*>(yes, 0xC));
        Clear(At<void*>(self, field::kYesWindow));
    }
    if (At<void*>(self, field::kNoWindow)) {
        void* const parent = Virtual<void*>(At<void*>(self, field::kView), 0xC);
        Virtual<void>(parent, 0x40, At<void*>(self, field::kNoWindow));
        Clear(At<void*>(self, field::kNoWindow));
    }
    This<uint8_t>(addr::SC4ViewInputControl_Shutdown, self);
    return 1;
}

// The game's 0x00438810: OnMouseExit (slot 22) of the same three tools: both
// picture windows hidden.
uint8_t __fastcall MouseExit(char* self, void*)
{
    if (void* const yes = At<void*>(self, field::kYesWindow))
        Virtual<void>(Virtual<void*>(yes, 0xC), 0x118);
    if (void* const no = At<void*>(self, field::kNoWindow))
        Virtual<void>(no, 0x118);
    return 1;
}

// The game's 0x00437C90: the home tool's OnMouseDownL (slot 16). With a
// building under the cursor that the MySim can live in, the MySim moves there
// and the tool ends.
uint8_t __fastcall HomeMouseDown(char* self, void*, int32_t, int32_t, uint32_t)
{
    if (Virtual<char*>(At<void*>(self, field::kView), 0x60) != self)
        return 0;
    if (void* const building = At<void*>(self, field::kBuilding)) {
        if (Virtual<uint8_t>(MySimPart(self), 0x1C, building, uint32_t{1}, uint32_t{0}) // TestForResidence
            && Virtual<uint8_t>(MySimPart(self), 0x18, At<void*>(self, field::kBuilding))) // ChangeResidence
            Virtual<void>(self, 0x6C);
    }
    return 1;
}

// The game's 0x004387D0: the job tool's OnMouseDownL (slot 16).
uint8_t __fastcall JobMouseDown(char* self, void*, int32_t, int32_t, uint32_t)
{
    if (Virtual<char*>(At<void*>(self, field::kView), 0x60) != self)
        return 0;
    if (void* const building = At<void*>(self, field::kBuilding)) {
        Virtual<void>(MySimPart(self), 0x20, building); // ChangeJob
        Virtual<void>(self, 0x6C);
    }
    return 1;
}

// The setup both tools share: the base Init, the MySim and the agent
// simulator, the two picture windows from the UI resource with the MySim's
// picture in each, and for the home tool the wealth marks.
uint8_t Setup(char* self, char* mysim, bool wealthMarks)
{
    if (!Virtual<uint8_t>(self, 0xC))
        return 0;
    Assign(At<void*>(self, field::kMySim), mysim);
    Assign(At<void*>(self, field::kAgents), Virtual<void*>(Global<void*>(addr::SC4_City), 0x11C));
    void* const windows = Cdecl<void*>(addr::SC4_GetWinManager);
    Virtual<void>(windows, 0x94, Virtual<void*>(At<void*>(self, field::kView), 0xC));

    void*& yes = At<void*>(self, field::kYesWindow);
    if (yes)
        return 1;
    uint32_t layout[3] = {0, 0x96A006B0, 0x6A9455C9};
    void* const parent = Virtual<void*>(At<void*>(self, field::kView), 0xC);
    if (!Cdecl<uint8_t>(addr::SC4UI_LoadWindow, static_cast<uint32_t*>(layout), parent, uint32_t{0x27DF05BE},
                        uint32_t{0x5386D516}, &yes))
        return 1;
    At<int32_t>(self, field::kHeight) = Virtual<int32_t>(Virtual<void*>(yes, 0xC), 0xA8);
    At<int32_t>(self, field::kHalfWidth) = Virtual<int32_t>(Virtual<void*>(yes, 0xC), 0xA4) / 2;
    Virtual<void>(Virtual<void*>(yes, 0xC), 0x110, uint32_t{0x200000}, uint32_t{1});
    void*& no = At<void*>(self, field::kNoWindow);
    void* const owner = Virtual<void*>(Virtual<void*>(yes, 0xC), 0x2C);
    This<void*>(addr::SC4RefPtr_Assign, &no, Virtual<void*>(owner, 0x8C, uint32_t{0x27DF05BF}));
    Virtual<void>(no, 0x110, uint32_t{0x200000}, uint32_t{1});

    // The MySim's picture: a resource of the UI group, or a file in the
    // folder the service gives.
    char* const sim = mysim + kMySimPart;
    const uint32_t bitmap = Virtual<uint32_t>(sim, 0x58);
    ImageLoader image;
    This<void*>(addr::SC4ImageLoader_Construct, &image, uint32_t{0});
    if (bitmap) {
        This<void>(addr::SC4ImageLoader_SetKey, &image, uint32_t{0}, kUiGroup, bitmap);
    } else {
        void* const file = Virtual<void*>(sim, 0x5C);
        GameString name;
        This<void*>(addr::SC4String_Substring, &name, file, uint32_t{0}, uint32_t{0xFFFFFFFF});
        GameString path;
        This<void*>(addr::SC4String_SC4String, &path);
        Virtual<void>(Global<void*>(addr::SC4_Service43C94), 0xF4, &path);
        char* const begin = name.body.begin;
        uint32_t allocator = 0; // the game passes the address of its argument slot
        This<void>(addr::SC4String_AppendRange, &path.body, begin, name.body.end, &allocator);
        This<void>(addr::SC4ImageLoader_SetName, &image, &path);
        This<void>(addr::SC4ImageLoader_SetMode, &image, uint32_t{8});
        DestroyString(path);
        if (begin)
            Free(begin);
    }
    if (This<uint8_t>(addr::SC4ImageLoader_Load, &image)) {
        void* picture = nullptr;
        void* const window = Virtual<void*>(yes, 0xC);
        if (Virtual<uint8_t>(window, 0x94, uint32_t{0xEA9457BA}, uint32_t{0xC12CEA13}, &picture))
            Virtual<void>(picture, 0x10, image.image);
        void* const other = no;
        Clear(picture);
        if (Virtual<uint8_t>(other, 0x94, uint32_t{0xEA9457BB}, uint32_t{0xC12CEA13}, &picture))
            Virtual<void>(picture, 0x10, image.image);
        if (picture)
            Release(picture);
    }

    if (wealthMarks) {
        // The picture window has two marks; a poor MySim hides both, a
        // middle one the second.
        void* mark = nullptr;
        const int32_t wealth = Virtual<int32_t>(MySimPart(self), 0x4C);
        if (wealth == 1) {
            void* const window = Virtual<void*>(yes, 0xC);
            if (Virtual<uint8_t>(window, 0x94, uint32_t{0xCBF33D81}, uint32_t{0x22BA0121},
                                 This<void**>(addr::SC4RefPtr_Reset, &mark)))
                Virtual<void>(mark, 0x118);
        }
        if (wealth == 1 || wealth == 2) {
            void* const window = Virtual<void*>(yes, 0xC);
            Clear(mark);
            if (Virtual<uint8_t>(window, 0x94, uint32_t{0xCBF33D8C}, uint32_t{0x22BA0121}, &mark))
                Virtual<void>(mark, 0x118);
        }
        if (mark)
            Release(mark);
    }
    This<void>(addr::SC4ImageLoader_Destruct, &image);
    return 1;
}

// The game's 0x00438390: the home tool's setup with its MySim (the cSC4MySim
// object), called by the maker 0x004268C0.
uint8_t __fastcall HomeSetup(char* self, void*, char* mysim)
{
    return Setup(self, mysim, true);
}

// The game's 0x00438860: the job tool's setup, called by the maker 0x00426970.
uint8_t __fastcall JobSetup(char* self, void*, char* mysim)
{
    return Setup(self, mysim, false);
}

// The cursor is not over a building the tool offers: the "no" picture
// follows it, the rest is hidden and the highlight dropped.
void ShowNo(char* self, int32_t x, int32_t z)
{
    Virtual<void>(Virtual<void*>(At<void*>(self, field::kYesWindow), 0xC), 0x118);
    void* const no = At<void*>(self, field::kNoWindow);
    Virtual<void>(no, 0xE0, static_cast<int32_t>(static_cast<uint32_t>(x) - At<uint32_t>(self, field::kHalfWidth)),
                  static_cast<int32_t>(static_cast<uint32_t>(z) - At<uint32_t>(self, field::kHeight)));
    Virtual<void>(At<void*>(self, field::kNoWindow), 0x114);
    if (void* const tip = At<void*>(self, field::kTip))
        Virtual<void>(Virtual<void*>(tip, 0xC), 0x118);
    DropBuilding(self);
}

// The cursor is over a good building: the "yes" picture follows it and the
// building takes the highlight.
void ShowYes(char* self, int32_t x, int32_t z, void* building)
{
    Virtual<void>(At<void*>(self, field::kNoWindow), 0x118);
    void* const yes = Virtual<void*>(At<void*>(self, field::kYesWindow), 0xC);
    Virtual<void>(yes, 0xE0, static_cast<int32_t>(static_cast<uint32_t>(x) - At<uint32_t>(self, field::kHalfWidth)),
                  static_cast<int32_t>(static_cast<uint32_t>(z) - At<uint32_t>(self, field::kHeight)));
    Virtual<void>(Virtual<void*>(At<void*>(self, field::kYesWindow), 0xC), 0x114);
    DropBuilding(self);
    Assign(At<void*>(self, field::kBuilding), building);
    At<uint32_t>(self, field::kHighlight) = Virtual<uint32_t>(Virtual<void*>(building, 0xC), 0x40);
    Virtual<void>(Virtual<void*>(building, 0xC), 0x44, uint32_t{1}, uint32_t{0});
}

// A new info window's identity, its registration with the service and its
// rectangle.
void SetUpTip(char* self, void* tip)
{
    void* const service = Global<void*>(addr::SC4_Service43DC8);
    Virtual<void>(service, 0x38, Virtual<void*>(tip, 0xC));
    const int32_t half = At<int32_t>(self, field::kHalfWidth);
    int32_t area[4] = {static_cast<int32_t>(0u - static_cast<uint32_t>(half)), -1000, half, 0x1C};
    Virtual<void>(tip, 0x44, static_cast<int32_t*>(area));
}

// The info window placed right of the cursor and shown.
void PlaceTip(void* tip, int32_t x, int32_t z)
{
    int32_t point[2] = {static_cast<int32_t>(static_cast<uint32_t>(x) + 1), z};
    Virtual<void>(tip, 0x3C, static_cast<int32_t*>(point));
    Virtual<void>(Virtual<void*>(tip, 0xC), 0x114);
}

// The game's 0x00437D10: the home tool's OnMouseMove (slot 20). A good
// building is a residential one no MySim lives in, that passes the MySim's
// test and whose lot is in state 3; the info window then shows its name, a
// bar for the lot's wealth and two lines about the cell.
uint8_t __fastcall HomeMouseMove(char* self, void*, int32_t x, int32_t z, uint32_t)
{
    void* building = nullptr;
    void* occupant = nullptr;
    GameString name;
    InitString(name);

    bool good = false;
    int32_t wealth = 0;
    do {
        if (!Virtual<uint8_t>(At<void*>(self, field::kView), 0x94, x, z, &occupant)) // PickOccupant
            break;
        if (Virtual<uint32_t>(occupant, 0x1C) != kBuildingType)
            break;
        Clear(building);
        if (!Virtual<uint8_t>(occupant, 0x0, kBuildingIid, &building))
            break;
        void* const agents = At<void*>(self, field::kAgents);
        if (!agents)
            break;
        if (Virtual<void*>(agents, 0x24, building)) // MySimLivesHere
            break;
        if (!This<uint8_t>(addr::SC4UI_FloatAtLeast, Virtual<float*>(building, 0x24),
                           &Global<float>(addr::SC4UI_HomeToolMinimum)))
            break;
        if (*Virtual<int32_t*>(building, 0x2C) != 1)
            break;
        if (!Virtual<uint8_t>(MySimPart(self), 0x1C, building, uint32_t{1}, uint32_t{1})) // TestForResidence
            break;
        void* const lots = Global<void*>(addr::SC4_LotManager);
        void* lot = Virtual<void*>(lots, 0x38, Virtual<void*>(building, 0xC));
        wealth = Virtual<int32_t>(lot, 0xA4);
        Virtual<void>(building, 0x4C, &name);
        void* const asOccupant = Virtual<void*>(building, 0xC);
        lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x38, asOccupant);
        if (!lot || Virtual<int32_t>(lot, 0x84) != 3)
            break;
        good = true;
    } while (false);

    if (good) {
        ShowYes(self, x, z, building);
        void*& tip = At<void*>(self, field::kTip);
        if (!tip) {
            void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x1B4});
            This<void*>(addr::SC4RefPtr_Assign, &tip,
                        memory ? This<void*>(addr::SC4InfoTip_Construct, memory) : nullptr);
            Virtual<void>(tip, 0x2C, uint32_t{0xA85F1A83});
            Virtual<void>(tip, 0x28, uint32_t{0x0A52F5C6});
            Virtual<void>(tip, 0x30, uint32_t{0});
            uint32_t key[3] = {kPng, kUiGroup, 0x14416208};
            ImageLoader icon;
            This<void*>(addr::SC4ImageLoader_ConstructKey, &icon, static_cast<uint32_t*>(key), uint32_t{1},
                        uint32_t{0});
            Virtual<void>(tip, 0x10, icon.image, uint32_t{0xC}, uint32_t{0xA});
            SetUpTip(self, tip);
            This<void>(addr::SC4ImageLoader_Destruct, &icon);
        }

        // The bar's fill by the lot's wealth, as the bits of a float; any
        // other wealth leaves the game's stack slot, which holds zero.
        uint32_t level = 0;
        if (wealth == 1)
            level = 0x3EA8F5C3; // 0.33
        else if (wealth == 2)
            level = 0x3F28F5C3; // 0.66
        else if (wealth == 3)
            level = 0x3F800000;
        uint32_t key[3] = {kPng, kUiGroup, 0x1441621F};
        ImageLoader bar;
        This<void*>(addr::SC4ImageLoader_ConstructKey, &bar, static_cast<uint32_t*>(key), uint32_t{1}, uint32_t{0});
        Virtual<void>(tip, 0x38, bar.image, uint32_t{3}, level);
        Virtual<void>(tip, 0x18, &name);

        uint32_t position[4] = {};
        Virtual<void>(Virtual<void*>(building, 0xC), 0x20, static_cast<uint32_t*>(position));
        // The game's cell slots are not cleared first: the x slot still holds
        // the occupant's address, the z slot was never written.
        int32_t cellX = static_cast<int32_t>(reinterpret_cast<std::uintptr_t>(occupant));
        int32_t cellZ = 0;
        Virtual<void>(Global<void*>(addr::SC4_City), 0x17C, position[0], position[2], &cellX, &cellZ);
        void* const grids = Global<void*>(addr::SC4_Service43DA4);
        const float first = static_cast<float>(Virtual<double>(grids, 0x94, cellX, cellZ, wealth));
        const float second = static_cast<float>(Virtual<double>(grids, 0x98, cellX, cellZ, wealth));
        GameString firstText;
        InitString(firstText);
        GameString secondText;
        InitString(secondText);
        GameString both;
        InitString(both);
        Cdecl<void>(addr::SC4UI_FirstGradeText, first, &firstText);
        Cdecl<void>(addr::SC4UI_SecondGradeText, second, &secondText);
        reinterpret_cast<int(__cdecl*)(void*, const char*, ...)>(addr::cRZBaseString_Sprintf)(
            &both, "%s\n%s", firstText.body.begin, secondText.body.begin);
        Virtual<void>(tip, 0x20, &both);
        PlaceTip(tip, x, z);
        DestroyString(both);
        DestroyString(secondText);
        DestroyString(firstText);
        This<void>(addr::SC4ImageLoader_Destruct, &bar);
    } else {
        ShowNo(self, x, z);
    }

    DestroyString(name);
    if (building)
        Release(building);
    if (occupant)
        Release(occupant);
    return 1;
}

bool HasGroup(char* set, uint32_t group)
{
    void* found[2] = {nullptr, nullptr};
    This<void*>(addr::SC4Uint32HashTable_Find, set, static_cast<void**>(found), &group);
    return found[0] != nullptr;
}

// The game's 0x00438B50: the icon (a picture's instance ID) for an occupant
// by its occupant groups (property 0xAA1DD396). `this` is the occupant.
uint32_t __fastcall OccupantIcon(char* occupant, void*)
{
    constexpr uint32_t kPlain = 0x14416200;
    void* const property = Virtual<void*>(Virtual<void*>(occupant, 0x18), 0x24, uint32_t{0xAA1DD396});
    if (!property)
        return kPlain;
    void* groups = nullptr;
    if (!Virtual<uint8_t>(property, 0x0, uint32_t{0x60FD4D2B}, &groups))
        return kPlain;
    // The game never releases this reference.
    if (Virtual<uint32_t>(groups, 0xC) != 0x8006) // an array of uint32
        return kPlain;

    char* const set = reinterpret_cast<char*>(addr::SC4UI_OccupantGroupSet);
    if (!(Global<uint8_t>(addr::SC4UI_OccupantGroupSetFlags) & 1)) {
        Global<uint32_t>(addr::SC4UI_OccupantGroupSetFlags) |= 1;
        At<uint32_t>(set, 0x4) = 0;
        At<uint32_t>(set, 0x8) = 0;
        At<uint32_t>(set, 0xC) = 0;
        At<uint32_t>(set, 0x10) = 0;
        This<void>(addr::SC4Uint32HashSet_Construct, set, uint32_t{0x14});
        reinterpret_cast<int(__cdecl*)(std::uintptr_t)>(addr::SC4_atexit)(addr::SC4UI_OccupantGroupSetAtExit);
    }
    This<void>(addr::SC4Uint32HashSet_Clear, set);
    const int32_t count = Virtual<int32_t>(groups, 0x10);
    const uint32_t* const values = Virtual<const uint32_t*>(groups, 0x19C);
    for (int32_t i = 0; i < count; ++i) {
        uint32_t value = values[i];
        This<void>(addr::SC4Uint32HashSet_Resize, set, At<uint32_t>(set, 0x10) + 1);
        uint32_t inserted[4];
        This<void>(addr::SC4Uint32HashSet_Insert, set, static_cast<uint32_t*>(inserted), &value);
    }

    // Residential, looked up inline.
    char** const buckets = At<char**>(set, 0x4);
    const uint32_t bucketCount = static_cast<uint32_t>((At<char*>(set, 0x8) - reinterpret_cast<char*>(buckets)) >> 2);
    for (char* node = buckets[0x1000u % bucketCount]; node; node = At<char*>(node, 0x0)) {
        if (At<uint32_t>(node, 0x4) == 0x1000)
            return 0x14416208;
    }
    if (HasGroup(set, 0x1001)) // commercial
        return 0x14416202;
    if (HasGroup(set, 0x1002)) // industrial
        return 0x14416206;
    if (HasGroup(set, 0x1003))
        return 0x14416209;
    if (HasGroup(set, 0x1004) || HasGroup(set, 0x1004)) // the game asks twice
        return 0x1441620A;
    if (HasGroup(set, 0x1501) || HasGroup(set, 0x1500))
        return 0x14416207;
    if (HasGroup(set, 0x1502))
        return 0x14416203;
    if (HasGroup(set, 0x1503) || HasGroup(set, 0x1504) || HasGroup(set, 0x1505) || HasGroup(set, 0x1506)
        || HasGroup(set, 0x1507))
        return 0x14416201;
    if (HasGroup(set, 0x150A) || HasGroup(set, 0x150B))
        return 0x1441620C;
    if (HasGroup(set, 0x1006))
        return 0x14416204;
    return kPlain;
}

// The game's 0x00438EA0: the job tool's OnMouseMove (slot 20). A good
// building is a non-residential one whose lot (state 3, condition under 4)
// has work for the MySim's wealth; the info window shows its icon, its name
// and the job title the MySim would get.
uint8_t __fastcall JobMouseMove(char* self, void*, int32_t x, int32_t z, uint32_t)
{
    void* building = nullptr;
    void* occupant = nullptr;
    GameString name;
    InitString(name);

    bool good = false;
    do {
        if (!Virtual<uint8_t>(At<void*>(self, field::kView), 0x94, x, z, &occupant)) // PickOccupant
            break;
        if (Virtual<uint32_t>(occupant, 0x1C) != kBuildingType)
            break;
        Clear(building);
        if (!Virtual<uint8_t>(occupant, 0x0, kBuildingIid, &building))
            break;
        if (!At<void*>(self, field::kAgents))
            break;
        if (!(*Virtual<float*>(building, 0x24) >= Global<float>(addr::SC4UI_JobToolMinimum)))
            break;
        if (*Virtual<int32_t*>(building, 0x2C) == 1)
            break;
        const uint32_t zero = Global<uint32_t>(addr::SC4UI_JobToolZero);
        uint32_t position[4] = {zero, zero, zero, 0};
        Virtual<void>(occupant, 0x20, static_cast<uint32_t*>(position));
        void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x14, static_cast<uint32_t*>(position));
        const int32_t wealth = Virtual<int32_t>(MySimPart(self), 0x4C);
        const bool open = Virtual<double>(lot, 0xFC, wealth) > 0.0 && Virtual<int32_t>(lot, 0x8C) < 4
            && Virtual<int32_t>(lot, 0x84) == 3;
        Virtual<void>(building, 0x4C, &name);
        if (!open)
            break;
        good = true;
    } while (false);

    if (good) {
        ShowYes(self, x, z, building);
        void*& tip = At<void*>(self, field::kTip);
        if (!tip) {
            void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x1B4});
            Assign(tip, memory ? This<void*>(addr::SC4InfoTip_Construct, memory) : nullptr);
            Virtual<void>(tip, 0x2C, uint32_t{0xA85F1A83});
            Virtual<void>(tip, 0x28, uint32_t{0x0A52F5C6});
            Virtual<void>(tip, 0x30, uint32_t{0});
            SetUpTip(self, tip);
        }
        uint32_t key[3] = {kPng, kUiGroup, 0};
        key[2] = OccupantIcon(Virtual<char*>(building, 0xC), nullptr);
        ImageLoader icon;
        This<void*>(addr::SC4ImageLoader_ConstructKey, &icon, static_cast<uint32_t*>(key), uint32_t{1}, uint32_t{0});
        Virtual<void>(tip, 0x10, icon.image, uint32_t{0xC}, uint32_t{0xA});
        Virtual<void>(tip, 0x18, &name);
        GameString title;
        InitString(title);
        Virtual<void>(MySimPart(self), 0x6C, building, &title); // the job title at that building
        Virtual<void>(tip, 0x20, &title);
        PlaceTip(tip, x, z);
        DestroyString(title);
        This<void>(addr::SC4ImageLoader_Destruct, &icon);
    } else {
        ShowNo(self, x, z);
    }

    DestroyString(name);
    if (building)
        Release(building);
    if (occupant)
        Release(occupant);
    return 1;
}

bool InstallMySimTools1(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[16] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F437AF0, reinterpret_cast<void*>(&HomeConstruct)},
        {addr::SC4UI_F437B20, reinterpret_cast<void*>(&HomeDestruct)},
        {addr::SC4UI_F437B80, reinterpret_cast<void*>(&Shutdown)},
        {addr::SC4UI_F437C90, reinterpret_cast<void*>(&HomeMouseDown)},
        {addr::SC4UI_F437CF0, reinterpret_cast<void*>(&HomeDelete)},
        {addr::SC4UI_F437D10, reinterpret_cast<void*>(&HomeMouseMove)},
        {addr::SC4UI_F438390, reinterpret_cast<void*>(&HomeSetup)},
        {addr::SC4UI_F438740, reinterpret_cast<void*>(&JobConstruct)},
        {addr::SC4UI_F438770, reinterpret_cast<void*>(&JobDestruct)},
        {addr::SC4UI_F4387D0, reinterpret_cast<void*>(&JobMouseDown)},
        {addr::SC4UI_F438810, reinterpret_cast<void*>(&MouseExit)},
        {addr::SC4UI_F438840, reinterpret_cast<void*>(&JobDelete)},
        {addr::SC4UI_F438860, reinterpret_cast<void*>(&JobSetup)},
        {addr::SC4UI_F438B50, reinterpret_cast<void*>(&OccupantIcon)},
        {addr::SC4UI_F438EA0, reinterpret_cast<void*>(&JobMouseMove)},
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

const bool g_mySimTools1Registered = registry::Register({
    "ui.mysim_tools.part1",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F437D10,
    "Our MySim home and job tools: setup, mouse handlers, the building icon (docs/ui.md)",
    false,
    InstallMySimTools1,
});

} // namespace
} // namespace opensc4::ui
