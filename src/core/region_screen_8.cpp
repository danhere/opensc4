// The region screen (docs/region.md), part 8: 0x007B1900 to 0x007B2440, the
// end of cSC4WinRegionScreen's code. Replacement, off by default:
//
//   region.screen.part8: the window's Init (slot 4 of the main vtable
//     0x00AB9260), which builds everything the region screen shows, its
//     deleting destructor (slot 148), the callback that points every button
//     under a window at one notification target, and three small functions
//     of the region map window's classes that sit before that window's code
//     (two setters the screen calls on the map window, and the
//     QueryInterface of a helper class with the vtable 0x00AB9630).
//
// Init, in order: posts kSC4MessagePreRegionInit (0xABB5BB44), starts the
// window base class, takes the window ID 0xEA659793, loads the key
// accelerators (resource 0xA2E3D533 / 0x6A231EAA / 0x6A9362F0), asks the
// nation whether the open region is the one the preferences name, sends
// message 0x2A3AD653, fills the main window, makes the terrain and its
// services (0x007ACC90), a plain backdrop window, the map window, twelve
// edge pictures (1 by 1 black ones when a picture is missing), the window
// with the vtable 0x00AB8D10, the drifting overlay window, the cities and
// the view (0x007B13C0, 0x007ABF10), centres the view or restores the saved
// position, registers for its messages and fourteen commands, loads the
// panels (0x007B0470), starts the effect 'region_global' in the middle of
// the terrain and posts kSC4MessagePostRegionInit (0xCBB5BB45).
//
// Still the game's: 0x007B23A0 (a QueryInterface for the interfaces 1 and
// 0x22E85D8E that seven vtables of unrelated classes share). 0x007B1D50 is
// not a function: it is inside Init.
//
// Windows, the window manager, pictures, the effect, the terrain and the
// command service are the game's, called through their vtables.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace opensc4::region {
namespace {

namespace screen { // cSC4WinRegionScreen, as far as this part shows it
constexpr std::size_t kMessageTarget = 0xDC; // the message target part (vtable 0x00AB923C)
constexpr std::size_t kMapWin = 0xE0;        // the region map window (0x128 bytes)
constexpr std::size_t kOverlayWin = 0xE4;    // the drifting overlay window (0x140 bytes)
constexpr std::size_t kBackWin = 0xE8;       // a plain window behind the others (0xDC bytes)
constexpr std::size_t kAccelerator = 0x108;  // the key accelerator
constexpr std::size_t kMessageIds = 0x10C;   // the messages the screen listens to
constexpr std::size_t kEdgePictures = 0x124; // twelve pictures, in pairs
constexpr std::size_t kService = 0x154;      // the service at 0x00B43C9C
constexpr std::size_t kEffects = 0x15C;      // the effects manager
constexpr std::size_t kEffect = 0x170;       // the effect 'region_global'
constexpr std::size_t kToolWin = 0x174;      // the window with the vtable 0x00AB8D10 (0x138 bytes)
constexpr std::size_t kViewX = 0x178;        // the view's position (floats)
constexpr std::size_t kViewY = 0x17C;
constexpr std::size_t kLeft = 0x180;         // the region's bounds on screen
constexpr std::size_t kTop = 0x184;
constexpr std::size_t kRight = 0x188;
constexpr std::size_t kBottom = 0x18C;
constexpr std::size_t kRegionIndex = 0x1A4;  // the nation's index of the open region
constexpr std::size_t kColourA = 0x1B0;      // two colours handed to the map window
constexpr std::size_t kColourB = 0x1B4;
constexpr std::size_t kState = 0x1E8;
} // namespace screen

namespace map { // the region map window
constexpr std::size_t kRegionIndex = 0xDC;
constexpr std::size_t kColourA = 0xF8;
constexpr std::size_t kColourB = 0xFC;
constexpr std::size_t kFlagA = 0x111;
constexpr std::size_t kFlagB = 0x112;
constexpr std::size_t kFlagC = 0x116;
} // namespace map

constexpr uint32_t kScreenWinId = 0xEA659793;
constexpr uint32_t kPreRegionInit = 0xABB5BB44;
constexpr uint32_t kPostRegionInit = 0xCBB5BB45;
constexpr uint32_t kScreenOpening = 0x2A3AD653;

constexpr uint32_t kAcceleratorResIid = 0x42E3EA4B;
constexpr uint32_t kAcceleratorClass = 0x42E967BE;
constexpr uint32_t kAcceleratorIid = 0xE2C1B3C4;
constexpr uint32_t kGraphicSystem = 0xC416025C;
constexpr uint32_t kGraphicSystemIid = 0x0073283C;
constexpr uint32_t kWinIid = 0x22BA0121;
constexpr uint32_t kButtonIid = 0x8810;
constexpr uint32_t kViewHelperIid = 0xC989F960;

constexpr uint32_t kPictureType = 0x856DDBAC;
constexpr uint32_t kPictureGroup = 0x6A1EED2C;
constexpr std::size_t kEdgePairs = 6;

// The game's string.
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

// The game's picture loader (0x00602B70 fills it, 0x00602BE0 empties it).
struct ImageLoader {
    std::uintptr_t vtable;
    void* image;
    uint32_t key[3];
    GameString name;
    uint32_t mode;
    uint8_t flag;
};
static_assert(sizeof(ImageLoader) == 0x30);

// What the effect is placed with: two flag bytes, a rotation, a position and
// a scale.
struct Placement {
    uint8_t flagA;
    uint8_t flagB;
    uint8_t pad[2];
    float rotation[9];
    float position[3];
    float scale;
};
static_assert(sizeof(Placement) == 0x38);

struct Command {
    uint32_t id;
    const char* name;
};

// The commands of the region screen, in the order Init registers them.
constexpr Command kCommands[] = {
    {0x6A935E3C, "QuitGame"},
    {0x6A9757C2, "RegionBitmapLoad"},
    {0x6A935CD8, "ScrollLeft"},
    {0x2A948275, "ScrollLeftStop"},
    {0x2A94826E, "ScrollRightStop"},
    {0x6A935CE0, "ScrollRight"},
    {0x6A935CDD, "ScrollUp"},
    {0x2A948272, "ScrollUpStop"},
    {0x2A94826B, "ScrollDownStop"},
    {0x6A935CE2, "ScrollDown"},
    {0x6AA9FE51, "SetExpandedToolTips"},
    {0x0BB3C277, "LoadRegion"},
    {0x0BB2747D, "LoadCity"},
    {0x6A935CF1, "Cancel"},
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

// The game's inlined reference holder: take another object.
void Assign(void*& holder, void* fresh)
{
    void* const old = holder;
    if (fresh == old)
        return;
    if (fresh)
        AddRef(fresh);
    holder = fresh;
    if (old)
        Release(old);
}

// A new message with one reference. Like the original this does not survive
// a failed allocation (the type is set through a null pointer).
void* NewMessage(uint32_t type)
{
    void* const memory = Cdecl<void*>(addr::RZ_operator_new, uint32_t{0x2C});
    void* const message = memory ? This<void*>(addr::cGZMessage_cGZMessage, memory) : nullptr;
    if (message)
        AddRef(message);
    Virtual<void>(message, 0x14, type); // SetType
    return message;
}

// Window flags (slot 0x110 of the window base class: flag, on).
void SetFlag(void* window, uint32_t flag, uint32_t on)
{
    Virtual<uint32_t>(window, 0x110, flag, on);
}

// Gives a window the screen's size at (0, 0). The height is asked first.
void FillScreen(char* self, void* window)
{
    const int32_t height = Virtual<int32_t>(self, 0xA8);
    const int32_t width = Virtual<int32_t>(self, 0xA4);
    Virtual<uint8_t>(window, 0xDC, int32_t{0}, int32_t{0}, width, height); // SetArea
}

// The game's 0x007B2340, cdecl, given to a window's EnumChildren (slot 0x80)
// for the interface 0x22BA0121: (the window enumerated, the child's ID, the
// child, a notification target). Clears flag 0x8000 of the child, makes the
// target the notification target of a child that is a button (interface
// 0x8810), and goes on into the child's own children. Returns 1.
uint8_t __cdecl AimButtons(void* /*parent*/, uint32_t /*id*/, void* child, void* target)
{
    SetFlag(child, 0x8000, 0);
    void* button = child; // the original uses the argument's own stack slot for the answer
    if (Virtual<uint8_t>(child, 0x0, kButtonIid, &button)) {
        Virtual<uint8_t>(child, 0x158, target); // SetNotificationTarget
        Release(button);
    }
    Virtual<uint8_t>(child, 0x80, kWinIid, reinterpret_cast<void*>(addr::cSC4WinRegionScreen_AimButtonsAt), target);
    return 1;
}

// The game's 0x007B1900, slot 4 of the region screen's main vtable (Init).
// Returns 1, also when the window was started before.
uint8_t __fastcall Init(char* self, void*)
{
    if (This<uint8_t>(addr::GZWin_IsInitialized, self))
        return 1;
    This<void>(addr::cSC4WinRegionScreen_PostPlainMessage, self, kPreRegionInit); // posts the message
    This<uint8_t>(addr::GZWin_Init, self);
    Virtual<uint8_t>(self, 0x100, kScreenWinId); // SetID

    // The key accelerators of the region screen.
    uint32_t key[3] = {0xA2E3D533, 0x6A231EAA, 0x6A9362F0};
    void* acceleratorRes = nullptr;
    if (Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, static_cast<void*>(key), kAcceleratorResIid,
                         &acceleratorRes, uint32_t{0}, uint32_t{0})) {
        void* const com = Cdecl<void*>(addr::RZGetCOM);
        void*& accelerator = At<void*>(self, screen::kAccelerator);
        Clear(accelerator);
        if (Virtual<uint8_t>(com, 0xC, kAcceleratorClass, kAcceleratorIid, &accelerator)) // GetClassObject
            Virtual<uint8_t>(acceleratorRes, 0xC, accelerator); // RegisterResources
    }

    // Is the open region the one the preferences name?
    void* const nation = Virtual<void*>(Global<void*>(addr::SC4_Service43C94), 0x88); // GetNation
    Virtual<int32_t>(nation, 0x18); // GetRegionCount
    int32_t preferred; // not set here, as in the original
    uint8_t restoreView = Virtual<uint8_t>(nation, 0x28, &preferred);
    const int32_t active = Virtual<int32_t>(nation, 0x1C); // GetActiveRegionIndex
    At<int32_t>(self, screen::kRegionIndex) = active;
    if (restoreView && active != preferred)
        restoreView = 0;

    void* const opening = NewMessage(kScreenOpening);
    Virtual<void>(opening, 0x2C, uint32_t{1}); // SetData1
    Virtual<uint8_t>(Global<void*>(addr::SC4_MessageServer), 0x10, opening, uint32_t{0});

    // The screen fills the main window.
    {
        void* const mainForHeight = Virtual<void*>(self, 0x28); // GetMainWindow
        void* const mainForWidth = Virtual<void*>(self, 0x28);
        const int32_t height = Virtual<int32_t>(mainForHeight, 0xA8);
        const int32_t width = Virtual<int32_t>(mainForWidth, 0xA4);
        Virtual<uint8_t>(self, 0xDC, int32_t{0}, int32_t{0}, width, height);
    }

    Assign(At<void*>(self, screen::kService), Global<void*>(addr::SC4_Service43C9C));
    This<void>(addr::cSC4WinRegionScreen_SetUpView, self); // the terrain and its services

    // The backdrop: a plain window, constructed in place.
    {
        char* const memory = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0xDC});
        if (memory) {
            This<void*>(addr::GZWin_Construct, memory);
            At<std::uintptr_t>(memory, 0) = addr::SC4RegionScreen_BackWin_Vtable;
            At<uint32_t>(memory, 0xD8) = 0;
        }
        Assign(At<void*>(self, screen::kBackWin), memory);
    }
    FillScreen(self, At<void*>(self, screen::kBackWin));
    SetFlag(At<void*>(self, screen::kBackWin), 0x10000, 0);
    Virtual<uint8_t>(self, 0x38, At<void*>(self, screen::kBackWin)); // ChildAdd
    Virtual<uint8_t>(At<void*>(self, screen::kBackWin), 0x60);       // SendToBack
    SetFlag(At<void*>(self, screen::kBackWin), 0x800, 0);
    SetFlag(At<void*>(self, screen::kBackWin), 0x200000, 1);
    SetFlag(At<void*>(self, screen::kBackWin), 1, 0);

    // The region map window.
    {
        void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x128});
        Assign(At<void*>(self, screen::kMapWin),
               memory ? This<void*>(addr::cSC4WinRegionView_Construct, memory) : nullptr);
    }
    Virtual<uint8_t>(self, 0x38, At<void*>(self, screen::kMapWin));
    FillScreen(self, At<void*>(self, screen::kMapWin));
    Virtual<uint8_t>(At<void*>(self, screen::kMapWin), 0x5C); // PullToFront
    SetFlag(At<void*>(self, screen::kMapWin), 0x800, 0);
    SetFlag(At<void*>(self, screen::kMapWin), 0x80000, 1);
    SetFlag(At<void*>(self, screen::kMapWin), 1, 1);
    At<int32_t>(At<void*>(self, screen::kMapWin), map::kRegionIndex) = At<int32_t>(self, screen::kRegionIndex);
    {
        void* const view = At<void*>(self, screen::kMapWin);
        const uint32_t colourB = At<uint32_t>(self, screen::kColourB);
        const uint32_t colourA = At<uint32_t>(self, screen::kColourA);
        At<uint32_t>(view, map::kColourA) = colourA;
        At<uint32_t>(view, map::kColourB) = colourB;
    }

    // The edge pictures, six pairs. A pair that cannot be loaded whole is
    // replaced by two black pictures of one pixel.
    {
        const uint32_t* const ids = reinterpret_cast<const uint32_t*>(addr::SC4RegionScreen_EdgePictures);
        void** const pictures = &At<void*>(self, screen::kEdgePictures);
        for (std::size_t pair = 0; pair < kEdgePairs; ++pair) {
            void** const slot = pictures + pair * 2;
            ImageLoader first;
            This<void*>(addr::SC4ImageLoader_ConstructParts, &first, kPictureType, kPictureGroup, ids[pair * 2],
                        uint32_t{9}, uint32_t{0});
            ImageLoader second;
            This<void*>(addr::SC4ImageLoader_ConstructParts, &second, kPictureType, kPictureGroup,
                        ids[pair * 2 + 1], uint32_t{9}, uint32_t{0});
            if (first.image && second.image) {
                Assign(slot[0], first.image);
                Assign(slot[1], second.image);
            } else {
                void* graphics = nullptr;
                if (void* const framework = Cdecl<void*>(addr::RZGetFramework))
                    Virtual<uint8_t>(framework, 0x14, kGraphicSystem, kGraphicSystemIid, &graphics);
                for (std::size_t i = 0; i < 2; ++i) {
                    void*& picture = slot[i];
                    Clear(picture);
                    Virtual<uint8_t>(graphics, 0xC, &picture); // CreateBuffer
                    // Init: 1 by 1, colour type 9, 32 bits
                    Virtual<uint8_t>(picture, 0xC, uint32_t{1}, uint32_t{1}, uint32_t{9}, uint32_t{0x20});
                    if (Virtual<uint8_t>(picture, 0x18, uint32_t{0x8001})) { // Lock
                        Virtual<uint8_t>(picture, 0x48, uint32_t{0}, uint32_t{0}); // Fill
                        Virtual<uint8_t>(picture, 0x1C, uint32_t{0x8001});         // Unlock
                    }
                }
                This<void>(addr::SC4RefPtr_Destruct, &graphics);
            }
            This<void>(addr::SC4ImageLoader_Destruct, &second);
            This<void>(addr::SC4ImageLoader_Destruct, &first);
        }
    }

    // The window with the vtable 0x00AB8D10.
    {
        void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x138});
        Assign(At<void*>(self, screen::kToolWin),
               memory ? This<void*>(addr::SC4RegionScroller_Construct, memory) : nullptr);
    }
    FillScreen(self, At<void*>(self, screen::kToolWin));
    SetFlag(At<void*>(self, screen::kToolWin), 0x10000, 0);
    Virtual<uint8_t>(self, 0x38, At<void*>(self, screen::kToolWin));
    Virtual<uint8_t>(At<void*>(self, screen::kToolWin), 0x5C);
    SetFlag(At<void*>(self, screen::kToolWin), 0x800, 0);
    SetFlag(At<void*>(self, screen::kToolWin), 0x200000, 1);

    // The drifting overlay, shown when the preferences say so.
    {
        void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x140});
        Assign(At<void*>(self, screen::kOverlayWin),
               memory ? This<void*>(addr::SC4RegionOverlayWin_Construct, memory) : nullptr);
    }
    Virtual<uint8_t>(self, 0x38, At<void*>(self, screen::kOverlayWin));
    Virtual<uint8_t>(At<void*>(self, screen::kOverlayWin), 0x5C);
    SetFlag(At<void*>(self, screen::kOverlayWin), 0x800, 0);
    {
        void* const overlay = At<void*>(self, screen::kOverlayWin);
        char* const preferences = Virtual<char*>(Global<void*>(addr::SC4_Service43C94), 0x98); // GetPreferences
        // setne al on the pointer: the upper three bytes of the argument are the pointer's.
        const uint32_t shown = (static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(preferences)) & 0xFFFFFF00u) |
                               (At<uint8_t>(preferences, 0xF11) != 0 ? 1u : 0u);
        SetFlag(overlay, 1, shown);
    }
    {
        // Two floats, passed on as they are.
        const uint32_t* const direction = reinterpret_cast<const uint32_t*>(addr::SC4RegionScreen_DriftDirection);
        const uint32_t second = direction[1];
        const uint32_t first = direction[0];
        This<void>(addr::SC4RegionOverlayWin_SetDirection, At<void*>(self, screen::kOverlayWin), first, second);
    }

    This<void>(addr::cSC4WinRegionScreen_BuildCityRecords, self);
    This<void>(addr::cSC4WinRegionScreen_LoadTuning, self);

    // The view starts at the middle of the region's bounds, or where the
    // preferences left it (1/256 units, less half the screen).
    {
        const uint32_t left = At<uint32_t>(self, screen::kLeft);
        const uint32_t right = At<uint32_t>(self, screen::kRight);
        const uint32_t top = At<uint32_t>(self, screen::kTop);
        const int32_t middleX = static_cast<int32_t>(right + left) / 2;
        const uint32_t bottom = At<uint32_t>(self, screen::kBottom);
        At<float>(self, screen::kViewX) = static_cast<float>(static_cast<double>(middleX));
        const int32_t middleY = static_cast<int32_t>(bottom + top) / 2;
        At<float>(self, screen::kViewY) = static_cast<float>(static_cast<double>(middleY));
    }
    if (restoreView) {
        char* const options = Global<char*>(addr::SC4_Options43CD8);
        const int32_t savedX = At<int32_t>(options, 0xEFC);
        if (static_cast<uint32_t>(savedX) != 0x80000000u) {
            const float x = static_cast<float>(static_cast<double>(savedX) * static_cast<double>(0.00390625f));
            const float y =
                static_cast<float>(static_cast<double>(At<int32_t>(options, 0xF00)) * static_cast<double>(0.00390625f));
            const int32_t width = Virtual<int32_t>(self, 0xA4);
            At<float>(self, screen::kViewX) =
                static_cast<float>(static_cast<double>(x) - static_cast<double>(width) * static_cast<double>(0.5f));
            const int32_t height = Virtual<int32_t>(self, 0xA8);
            At<float>(self, screen::kViewY) =
                static_cast<float>(static_cast<double>(y) - static_cast<double>(height) * static_cast<double>(0.5f));
        }
    }

    // Listen for the screen's messages on the default message server.
    This<void>(addr::SC4MessageIdSet_AddNotifications, self + screen::kMessageIds,
               static_cast<void*>(self + screen::kMessageTarget), static_cast<void*>(nullptr));

    if (void* const commands = Global<void*>(addr::SC4_Commands)) {
        for (const Command& command : kCommands)
            Virtual<uint8_t>(commands, 0x18, command.id, command.name, static_cast<const char*>(nullptr));
    }

    This<void>(addr::cSC4WinRegionScreen_SetUpControls, self); // the panels

    // The effect over the whole region, at the middle of the terrain.
    {
        void* const effects = At<void*>(self, screen::kEffects);
        void*& effect = At<void*>(self, screen::kEffect);
        At<uint32_t>(self, screen::kState) = 2;
        Clear(effect);
        if (Virtual<uint8_t>(effects, 0x1C, "region_global", &effect)) {
            void* const terrain = Global<void*>(addr::SC4_RegionTerrain);
            const float cell = static_cast<float>(Virtual<double>(terrain, 0x18));
            const int32_t cellsZ = Virtual<int32_t>(terrain, 0x20);
            const float z = static_cast<float>(static_cast<double>(cellsZ) * static_cast<double>(cell) *
                                               static_cast<double>(0.5f));
            const int32_t cellsX = Virtual<int32_t>(terrain, 0x1C);
            void* const made = effect;
            Placement placement;
            // The two bytes after the flags are never set in the original;
            // they hold the upper half of the picture loader's vtable address,
            // which the last loader left at this place on the stack.
            placement.pad[0] = 0xA8;
            placement.pad[1] = 0x00;
            placement.position[2] = z;
            placement.rotation[0] = 1.0f;
            placement.rotation[3] = 0.0f;
            placement.rotation[6] = 0.0f;
            placement.rotation[1] = 0.0f;
            placement.rotation[4] = 1.0f;
            placement.rotation[7] = 0.0f;
            placement.position[0] = static_cast<float>(static_cast<double>(cellsX) * static_cast<double>(cell) *
                                                       static_cast<double>(0.5f));
            placement.rotation[2] = 0.0f;
            placement.rotation[5] = 0.0f;
            placement.rotation[8] = 1.0f;
            placement.scale = 1.0f;
            placement.position[1] = 0.0f;
            placement.flagB = 1;
            placement.flagA = 1;
            Virtual<void>(made, 0x1C, &placement);
            Virtual<void>(effect, 0xC, uint32_t{0});
        }
    }

    if (void* const server = Global<void*>(addr::SC4_MessageServer)) {
        void* const done = NewMessage(kPostRegionInit);
        Virtual<uint8_t>(server, 0x10, done, uint32_t{0});
        Release(done);
    }

    Release(opening);
    if (acceleratorRes)
        Release(acceleratorRes);
    return 1;
}

// The game's 0x007B2320, slot 148 of the main vtable: the deleting
// destructor.
void* __fastcall DeletingDestruct(char* self, void*, uint32_t flags)
{
    This<void>(addr::cSC4WinRegionScreen_Destruct, self);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// The game's 0x007B23E0: QueryInterface of a helper class of the region map
// window (main vtable 0x00AB9630, a second one at +4 whose slot is the thunk
// 0x007B3E60). The class's own interface is 0xC989F960; the rest is the base
// class's, at +4.
uint8_t __fastcall ViewHelperQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    if (iid == kViewHelperIid) {
        *out = self;
        AddRef(self);
        return 1;
    }
    return This<uint8_t>(addr::cRZBaseUnknown_QueryInterface1, self + 0x4, iid, out);
}

// The game's 0x007B2410: two flag bytes of the region map window. The
// original returns the first flag in AL.
uint8_t __fastcall MapSetFlags(char* self, void*, uint8_t first, uint8_t second)
{
    At<uint8_t>(self, map::kFlagA) = first;
    At<uint8_t>(self, map::kFlagB) = second;
    return first;
}

// The game's 0x007B2430: another flag byte of the region map window.
uint8_t __fastcall MapSetFlag(char* self, void*, uint8_t value)
{
    At<uint8_t>(self, map::kFlagC) = value;
    return value;
}

bool InstallRegionScreen8(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[8] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSC4WinRegionScreen_Init, reinterpret_cast<void*>(&Init)},
        {addr::cSC4WinRegionScreen_DeletingDestructor, reinterpret_cast<void*>(&DeletingDestruct)},
        {addr::cSC4WinRegionScreen_AimButtonsAt, reinterpret_cast<void*>(&AimButtons)},
        {addr::SC4RegionViewHelper_QueryInterface, reinterpret_cast<void*>(&ViewHelperQueryInterface)},
        {addr::cSC4WinRegionView_SetFlags111, reinterpret_cast<void*>(&MapSetFlags)},
        {addr::cSC4WinRegionView_SetFlag116, reinterpret_cast<void*>(&MapSetFlag)},
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

const bool g_regionScreen8Registered = registry::Register({
    "region.screen.part8",
    "region",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4WinRegionScreen_Init,
    "Our region screen, part 8: the window's Init, its deleting destructor, the button callback and three small "
    "functions of the region map window",
    false,
    InstallRegionScreen8,
});

} // namespace
} // namespace opensc4::region
