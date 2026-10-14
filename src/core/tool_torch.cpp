// The fire (torch) disaster tool of the city view (0x00580510 to
// 0x00580B46).
// Replacement, off by default:
//
//   ui.tools.disaster_fire: the view input control of 0x80 bytes (vtable
//     0x00A9EB40, message target 0x00A9EB30 at +0x28, ID 0xE9C24AC4, cursor
//     0x4A08C57E) the view makes for the disaster 0xA9DE8784 (StartDisaster,
//     0x007E9DB0).
//
// The tool is a torch at the pointer: the effect "torch_inactive" follows
// the pointer over the ground (the view's pick, slot 0x90 with 0) and is
// changed for "torch_active" while the tool burns. Fires are started
// through the fire manager's part (interface 0x49B43F57, kept at +0x2C
// from Init; docs/disasters.md):
//
//   A press (OnMouseDownL) over ground lights the torch, starts a fire at
//   the pointer's cell (the part's slot 5: the cell, no chance draw,
//   forced, group 0), tells the part an owner value of 1 (slot 9), sets the
//   cursor 0x0A08C54E and asks the frame service (0x86AD10EC under 0xC0)
//   for its message.
//   A move with the button held (modifier 0x10) lights the torch when it
//   is out; while it burns the pointer's cell is kept at +0x40, +0x44.
//   Every frame message while the torch burns starts a fire at the kept
//   cell with the chance draw, forced (and the owner value again). The
//   press does not set the kept cell: until the first move it is the cell
//   of the last drag (0, 0 in a new tool).
//   The release (OnMouseUpL) gives the frame message up, puts the torch
//   out and sets the tool's cursor again.
//
// The tool does not capture the mouse. Six sounds go to the sound service
// (0x00B43CB8) with numbers: 0x2A106113 at Init, 0x8A10611A at Shutdown,
// 0x8A10611D at a press, 0x0A106122 at a release and when the torch goes
// out, 0x4A106126 at every move, 0xAA96FE8F when the torch changes (with
// whether it burns).
//
// Time and chance: the stopwatch at +0x68 (mode 4, started by Init, stopped
// by Shutdown) runs on real time (0x00580510 to 0x00580B46 is not one of
// sim_timers' ranges); its milliseconds are read at every press, release
// and move and go nowhere but into those three sounds' third number. The
// frame message comes on frame time, so how many chance fires a held
// torch starts depends on real time; the draws are the fire manager's. The
// generator at +0x48 is made by the constructor, seeded from the clock
// (seed -1), and never drawn from by the tool's own code.
#include "core/address_check.h"
#include "core/fixed_seed.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::tool_torch {
namespace {

// The base class's fields.
namespace base {
constexpr std::size_t kInited = 0x04; // byte
constexpr std::size_t kId = 0x0C;
constexpr std::size_t kCursor = 0x10; // cursor ID
constexpr std::size_t kView = 0x1C;   // the city view
} // namespace base

// The tool's fields. +0x61 to +0x67 are padding.
namespace field {
constexpr std::size_t kTarget = 0x28;    // the message target part (vtable 0x00A9EB30)
constexpr std::size_t kFires = 0x2C;     // the fire manager's part (referenced)
constexpr std::size_t kTorch = 0x30;     // the torch's effect (referenced)
constexpr std::size_t kPlace = 0x34;     // float[3]: the pointer's place on the ground
constexpr std::size_t kCellX = 0x40;     // the cell the frame message burns
constexpr std::size_t kCellZ = 0x44;
constexpr std::size_t kGenerator = 0x48; // a random generator of 0x18 bytes, not drawn from
constexpr std::size_t kBurning = 0x60;   // byte: the torch is the active one
constexpr std::size_t kWatch = 0x68;     // a stopwatch of 0x18 bytes, mode 4
} // namespace field

constexpr uint32_t kToolId = 0xE9C24AC4;
constexpr uint32_t kToolCursor = 0x4A08C57E;
constexpr uint32_t kBurningCursor = 0x0A08C54E;
constexpr uint32_t kFireDisaster = 0x621CDA33;
constexpr uint32_t kFirePart = 0x49B43F57;       // the fire manager's part
constexpr uint32_t kMessageFrame = 0x86AD10EE;   // the frame service's
constexpr uint32_t kFrameCode = 0xC0;            // what the tool asks the frame service under
constexpr uint32_t kSoundMessage = 0x5B523BA4;   // the kind of object the sound service makes
constexpr uint32_t kModifierButton = 0x10;       // the left button is held

constexpr uint32_t kSoundInit = 0x2A106113;
constexpr uint32_t kSoundShutdown = 0x8A10611A;
constexpr uint32_t kSoundPress = 0x8A10611D;
constexpr uint32_t kSoundOut = 0x0A106122;
constexpr uint32_t kSoundMove = 0x4A106126;
constexpr uint32_t kSoundTorch = 0xAA96FE8F;

// What an effect is told where it is: 0x38 bytes on the stack of
// 0x00580760. A count of changes, flags (1 the position), a rotation, the
// position, the scale.
struct EffectTransform {
    uint8_t changes;
    uint8_t flags;
    uint8_t unused[2];
    float rotation[9];
    float position[3];
    float scale;
};
static_assert(sizeof(EffectTransform) == 0x38);

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

// --- Sounds and the torch --------------------------------------------------------------

// The game's 0x00580550 (ret 0x10; this is not read): a sound with three
// numbers. The sound service (0x00B43CB8), held for the call, makes the
// sound's object (slot 13: the sound, 0x5B523BA4, the object out, the
// numbers, 0) and is given it (slot 12); the object is let go when there is
// one. Nothing without the service.
void __fastcall PlaySound(char*, void*, uint32_t sound, uint32_t first, uint32_t second, uint32_t third)
{
    void* const sounds = Global<void*>(addr::SC4_Sound43CB8);
    if (!sounds)
        return;
    AddRef(sounds);
    void* made = nullptr;
    Virtual<uint8_t>(sounds, 0x34, sound, kSoundMessage, &made, first, second, third, uint32_t{0});
    Virtual<uint8_t>(sounds, 0x30, made);
    if (made)
        Release(made);
    Release(sounds);
}

// The torch's effect is stopped (slot 4 with 0), let go and made again by
// name (the effects manager's slot 7 into the same field, which is not
// cleared in between). The effects manager is not checked.
void Remake(char* self, const char* name)
{
    void** const torch = &At<void*>(self, field::kTorch);
    Virtual<void>(*torch, 0x10, uint32_t{0});
    Release(*torch);
    Virtual<uint8_t>(Global<void*>(addr::SC4_Service43D1C), 0x1C, name, torch);
}

// The game's 0x005806D0 (ret 4): the torch burns or not. Nothing without a
// torch or when it is so already. Else the effect is changed for
// "torch_active" or "torch_inactive" (with the sound 0x0A106122) and
// started (slot 3 with 0) when there is one; the sound 0xAA96FE8F says
// which.
void __fastcall SetBurning(char* self, void*, uint8_t burning)
{
    void** const torch = &At<void*>(self, field::kTorch);
    if (!*torch)
        return;
    if (At<uint8_t>(self, field::kBurning) == burning)
        return;
    At<uint8_t>(self, field::kBurning) = burning;
    if (burning) {
        Remake(self, "torch_active");
    } else {
        Remake(self, "torch_inactive");
        PlaySound(self, nullptr, kSoundOut, 0, 0, 0);
    }
    if (*torch)
        Virtual<void>(*torch, 0xC, uint32_t{0});
    PlaySound(self, nullptr, kSoundTorch, burning, 0, 0);
}

// The game's 0x00580760: the torch, when there is one, is told the place:
// a block of one change, flag 1, the unit rotation, the place, scale 1.
void __fastcall MoveTorch(char* self, void*)
{
    void* const torch = At<void*>(self, field::kTorch);
    if (!torch)
        return;
    EffectTransform transform;
    transform.position[0] = At<float>(self, field::kPlace);
    transform.position[1] = At<float>(self, field::kPlace + 4);
    transform.position[2] = At<float>(self, field::kPlace + 8);
    transform.flags = 1;
    transform.changes = 1;
    transform.rotation[0] = 1.0f;
    transform.rotation[1] = 0.0f;
    transform.rotation[2] = 0.0f;
    transform.rotation[3] = 0.0f;
    transform.rotation[4] = 1.0f;
    transform.rotation[5] = 0.0f;
    transform.rotation[6] = 0.0f;
    transform.rotation[7] = 0.0f;
    transform.rotation[8] = 1.0f;
    transform.scale = 1.0f;
    Virtual<void>(torch, 0x20, &transform);
}

// The game's 0x005807F0: the torch is taken away: stopped (slot 4 with 1)
// when it runs (slot 5), let go, the field cleared; it does not burn; the
// sound 0xAA96FE8F with 0.
void __fastcall DropTorch(char* self, void*)
{
    if (void* const torch = At<void*>(self, field::kTorch)) {
        if (Virtual<uint8_t>(torch, 0x14))
            Virtual<void>(At<void*>(self, field::kTorch), 0x10, uint32_t{1});
        Release(At<void*>(self, field::kTorch));
        At<void*>(self, field::kTorch) = nullptr;
    }
    At<uint8_t>(self, field::kBurning) = 0;
    PlaySound(self, nullptr, kSoundTorch, 0, 0, 0);
}

// The game's 0x00580B00 (slot 22, the pointer left the view): as
// 0x005807F0, in line. True.
uint8_t __fastcall OnMouseExit(char* self, void*)
{
    DropTorch(self, nullptr);
    return 1;
}

// --- Messages --------------------------------------------------------------------------

// The game's 0x00580510 (slot 3 of the message target at +0x28, ret 4).
// `this` is the target part. At the frame service's message while the torch
// burns: a fire at the kept cell (the part's slot 5 with the cell, the
// chance draw, forced, group 0) and the owner value 1 (slot 9). The part is
// not checked. True.
uint8_t __fastcall DoMessage(char* target, void*, void* message)
{
    char* const self = target - field::kTarget;
    if (Virtual<uint32_t>(message, 0x10) == kMessageFrame && At<uint8_t>(self, field::kBurning)) {
        Virtual<uint8_t>(At<void*>(self, field::kFires), 0x14, At<int32_t>(self, field::kCellX),
                         At<int32_t>(self, field::kCellZ), uint32_t{1}, uint32_t{1}, uint32_t{0});
        Virtual<void>(At<void*>(self, field::kFires), 0x24, uint32_t{1});
    }
    return 1;
}

// --- The mouse -------------------------------------------------------------------------

// The game's 0x005808B0 (slot 16, OnMouseDownL, ret 0xC). The stopwatch's
// milliseconds go into the press's sound with the screen point. The view is
// not checked; off the ground nothing more. Else the torch, when there is
// one that does not burn, is changed for the active one (as 0x005806D0
// with 1, in line); the torch told the place; the place's cell (the city's
// slot 95); the frame service's message asked for the target; a fire at
// the cell (the part's slot 5: no chance draw, forced, group 0) and the
// owner value 1; the cursor 0x0A08C54E (slot 8). The part is not checked.
// True.
uint8_t __fastcall OnMouseDownL(char* self, void*, int32_t x, int32_t z, uint32_t)
{
    const uint32_t elapsed = This<uint32_t>(addr::GZStopwatch_ElapsedMilliseconds, self + field::kWatch);
    PlaySound(self, nullptr, kSoundPress, static_cast<uint32_t>(x), static_cast<uint32_t>(z), elapsed);
    float* const place = &At<float>(self, field::kPlace);
    if (!Virtual<uint8_t>(At<void*>(self, base::kView), 0x90, x, z, place, uint32_t{0}))
        return 1;
    void** const torch = &At<void*>(self, field::kTorch);
    if (*torch && At<uint8_t>(self, field::kBurning) != 1) {
        At<uint8_t>(self, field::kBurning) = 1;
        Remake(self, "torch_active");
        if (*torch)
            Virtual<void>(*torch, 0xC, uint32_t{0});
        PlaySound(self, nullptr, kSoundTorch, 1, 0, 0);
    }
    MoveTorch(self, nullptr);
    // The game's outputs are the stack slots of the two screen arguments.
    int32_t cellX = x, cellZ = z;
    Virtual<void>(Global<void*>(addr::SC4_City), 0x17C, place[0], place[2], &cellX, &cellZ);
    if (void* const frames = Cdecl<void*>(addr::SC4_GetService86AD10EC))
        Virtual<uint8_t>(frames, 0x14, static_cast<void*>(self + field::kTarget), kFrameCode);
    Virtual<uint8_t>(At<void*>(self, field::kFires), 0x14, cellX, cellZ, uint32_t{0}, uint32_t{1}, uint32_t{0});
    Virtual<void>(At<void*>(self, field::kFires), 0x24, uint32_t{1});
    Virtual<void>(self, 0x20, kBurningCursor);
    return 1;
}

// The game's 0x005809D0 (slot 18, OnMouseUpL, ret 0xC). The stopwatch's
// milliseconds go into the sound 0x0A106122 with the screen point; the
// frame service's message is given up; the torch is put out; the tool's
// cursor is set again (slot 8). True.
uint8_t __fastcall OnMouseUpL(char* self, void*, int32_t x, int32_t z, uint32_t)
{
    const uint32_t elapsed = This<uint32_t>(addr::GZStopwatch_ElapsedMilliseconds, self + field::kWatch);
    PlaySound(self, nullptr, kSoundOut, static_cast<uint32_t>(x), static_cast<uint32_t>(z), elapsed);
    if (void* const frames = Cdecl<void*>(addr::SC4_GetService86AD10EC))
        Virtual<uint8_t>(frames, 0x18, static_cast<void*>(self + field::kTarget), kFrameCode);
    SetBurning(self, nullptr, 0);
    Virtual<void>(self, 0x20, At<uint32_t>(self, base::kCursor));
    return 1;
}

// The game's 0x00580A30 (slot 20, OnMouseMove, ret 0xC). The stopwatch is
// read first. The view is not checked. Over ground: without a torch,
// "torch_inactive" is made (the effects manager's slot 7; started when
// made) and the torch does not burn; a torch that does not burn is lit
// when the button is held (modifier 0x10); the torch is told the place;
// while it burns the place's cell is kept. Off the ground the torch is
// taken away. The move's sound with the screen point and the milliseconds.
// True.
uint8_t __fastcall OnMouseMove(char* self, void*, int32_t x, int32_t z, uint32_t modifiers)
{
    const uint32_t elapsed = This<uint32_t>(addr::GZStopwatch_ElapsedMilliseconds, self + field::kWatch);
    float* const place = &At<float>(self, field::kPlace);
    if (Virtual<uint8_t>(At<void*>(self, base::kView), 0x90, x, z, place, uint32_t{0})) {
        void** const torch = &At<void*>(self, field::kTorch);
        if (!*torch) {
            if (Virtual<uint8_t>(Global<void*>(addr::SC4_Service43D1C), 0x1C, "torch_inactive", torch))
                Virtual<void>(*torch, 0xC, uint32_t{0});
            At<uint8_t>(self, field::kBurning) = 0;
        }
        if (!At<uint8_t>(self, field::kBurning) && (modifiers & kModifierButton))
            SetBurning(self, nullptr, 1);
        MoveTorch(self, nullptr);
        if (At<uint8_t>(self, field::kBurning))
            Virtual<void>(Global<void*>(addr::SC4_City), 0x17C, place[0], place[2], &At<int32_t>(self, field::kCellX),
                          &At<int32_t>(self, field::kCellZ));
    } else {
        DropTorch(self, nullptr);
    }
    PlaySound(self, nullptr, kSoundMove, static_cast<uint32_t>(x), static_cast<uint32_t>(z), elapsed);
    return 1;
}

// --- The object ------------------------------------------------------------------------

// The game's 0x005805C0: the constructor (no arguments, returns this). No
// part and no torch; the place is the dword at 0x00B22BC0 three times; the
// kept cell is 0, 0; the generator is seeded from the clock (seed -1); the
// torch does not burn; the stopwatch is made with mode 4. +0x61 to +0x67
// are left as they come.
char* __fastcall Construct(char* self, void*)
{
    This<void*>(addr::SC4ViewInputControl_Construct, self);
    At<std::uintptr_t>(self, field::kTarget) = addr::cSC4TrafficSimulator_VtableBase4;
    At<std::uintptr_t>(self, 0) = addr::SC4TorchTool_Vtable;
    At<std::uintptr_t>(self, field::kTarget) = addr::SC4TorchTool_TargetVtable;
    At<void*>(self, field::kFires) = nullptr;
    At<void*>(self, field::kTorch) = nullptr;
    const uint32_t filler = Global<uint32_t>(addr::SC4_Global22BC0);
    At<uint32_t>(self, field::kPlace) = filler;
    At<uint32_t>(self, field::kPlace + 4) = filler;
    At<uint32_t>(self, field::kPlace + 8) = filler;
    At<int32_t>(self, field::kCellX) = 0;
    At<int32_t>(self, field::kCellZ) = 0;
    {
        // Clock seeds count under the original's call site.
        const fixed_seed::SiteOverride site(0x0058060F);
        This<void*>(addr::SC4Random_Construct, self + field::kGenerator, uint32_t{0xFFFFFFFF});
    }
    At<uint8_t>(self, field::kBurning) = 0;
    This<void*>(addr::SC4Stopwatch_Construct, self + field::kWatch, uint32_t{4});
    return self;
}

// The game's 0x00580630 (slot 26, ret 4): the deleting destructor: the
// stopwatch's (a bare return), the base's. The vtables are not set again
// and nothing the tool holds is let go here.
char* __fastcall DeletingDestruct(char* self, void*, uint32_t flags)
{
    This<void>(addr::SC4Stopwatch_Destruct, self + field::kWatch);
    This<void>(addr::SC4ViewInputControl_Destruct, self);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// The game's 0x00580660 (slot 3): Init. Unless inited: the ID and the
// cursor; the torch does not burn; the fire manager (the city's disaster
// layer, city slot 72, the layer's slot 7 with 0x621CDA33) is asked for its
// part 0x49B43F57 into +0x2C; the base Init; the stopwatch started; the
// sound 0x2A106113. True. The city and the layer are not checked.
uint8_t __fastcall Init(char* self, void*)
{
    if (At<uint8_t>(self, base::kInited))
        return 1;
    At<uint32_t>(self, base::kId) = kToolId;
    At<uint32_t>(self, base::kCursor) = kToolCursor;
    At<uint8_t>(self, field::kBurning) = 0;
    void* const layer = Virtual<void*>(Global<void*>(addr::SC4_City), 0x120);
    if (void* const manager = Virtual<void*>(layer, 0x1C, kFireDisaster))
        Virtual<uint8_t>(manager, 0x0, kFirePart, &At<void*>(self, field::kFires));
    This<uint8_t>(addr::SC4ViewInputControl_Init, self);
    This<void>(addr::SC4Stopwatch_Start, self + field::kWatch);
    PlaySound(self, nullptr, kSoundInit, 0, 0, 0);
    return 1;
}

// The game's 0x00580840 (slot 4): Shutdown. When inited: the stopwatch
// stopped; the torch taken away; the sound 0x8A10611A; the fire manager's
// part let go; the frame service's message given up; the base Shutdown.
// True.
uint8_t __fastcall Shutdown(char* self, void*)
{
    if (!At<uint8_t>(self, base::kInited))
        return 1;
    This<void>(addr::SC4Stopwatch_Stop, self + field::kWatch);
    DropTorch(self, nullptr);
    PlaySound(self, nullptr, kSoundShutdown, 0, 0, 0);
    if (void* const fires = At<void*>(self, field::kFires)) {
        Release(fires);
        At<void*>(self, field::kFires) = nullptr;
    }
    if (void* const frames = Cdecl<void*>(addr::SC4_GetService86AD10EC))
        Virtual<uint8_t>(frames, 0x18, static_cast<void*>(self + field::kTarget), kFrameCode);
    This<uint8_t>(addr::SC4ViewInputControl_Shutdown, self);
    return 1;
}

// --- Registration ----------------------------------------------------------------------

bool InstallTorchTool(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[16] = {};
    static const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::SC4TorchTool_DoMessage, reinterpret_cast<void*>(&DoMessage)},
        {addr::SC4TorchTool_PlaySound, reinterpret_cast<void*>(&PlaySound)},
        {addr::SC4ViewInputControls_F5805C0, reinterpret_cast<void*>(&Construct)},
        {addr::SC4TorchTool_DeletingDestructor, reinterpret_cast<void*>(&DeletingDestruct)},
        {addr::SC4TorchTool_Init, reinterpret_cast<void*>(&Init)},
        {addr::SC4TorchTool_SetBurning, reinterpret_cast<void*>(&SetBurning)},
        {addr::SC4TorchTool_MoveTorch, reinterpret_cast<void*>(&MoveTorch)},
        {addr::SC4TorchTool_DropTorch, reinterpret_cast<void*>(&DropTorch)},
        {addr::SC4TorchTool_Shutdown, reinterpret_cast<void*>(&Shutdown)},
        {addr::SC4TorchTool_OnMouseDownL, reinterpret_cast<void*>(&OnMouseDownL)},
        {addr::SC4TorchTool_OnMouseUpL, reinterpret_cast<void*>(&OnMouseUpL)},
        {addr::SC4TorchTool_OnMouseMove, reinterpret_cast<void*>(&OnMouseMove)},
        {addr::SC4TorchTool_OnMouseExit, reinterpret_cast<void*>(&OnMouseExit)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    static_assert(count <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < count; ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].address);
        if (!found || !hook::Install(*found, hooks[i].ours, &originals[i]))
            return false;
    }
    return true;
}

const bool g_registered = registry::Register({
    "ui.tools.disaster_fire",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4TorchTool_Init,
    "Our fire (torch) disaster tool of the city view (docs/tools.md)",
    false,
    InstallTorchTool,
});

} // namespace
} // namespace opensc4::tool_torch
