// The MySim's balloon object (SC4MySimBalloon, 0x58 bytes, kept at
// cSC4MySim+0x238; 0x00430250 to 0x004311B0): it shows the advice a MySim
// speaks as a balloon over its head. Replacement, off by default:
//
//   simulators.mysim_agents.balloon: QueryInterface, the sound start and
//     stop, the balloon's shutdown (DropTripObject), the heartbeat post, the
//     active/hidden sync, Create, the constructor, the destructors, the queue
//     update and the message handler.
//
// The object: three interfaces at +0 (vtable 0x00A8576C: IID 0x22E85D8E, the
// window procedure 0x00430A40 is its slot 3), +4 (0x00A8575C, IID 0x452294AA,
// the message target: DoMessage 0x00430F70) and +8 (0x00A85744, the counted
// base; the reference count is at +0xC). Fields, from the object's start:
//   +0x10 state: 5 hidden, 4 waiting for the next advice, 1 advice shown but
//         not yet spoken, 3 spoken, 2 taken down
//   +0x14 the current balloon advice (its +0xC0 part, counted)
//   +0x18 the MySim's cISC4MySim part (counted)
//   +0x1C the window generator (cIGZWinGen, class 0x4386D516, counted)
//   +0x20 vector of counted icon pictures (begin, end, capacity)
//   +0x2C byte: set when the queue gave an advice
//   +0x30 stopwatch (mode 4, 0x18 bytes), +0x48, +0x4C and +0x50 three delays
//         in milliseconds (300, 2500, 300; the setters 0x004302D0 and
//         0x004302C0 write the last two)
//   +0x54 byte: the window is made, +0x55 byte: the sound plays
//
// Time: the stopwatch at +0x30 is read and started only from this range,
// which is not one of the owner ranges of src/core/sim_timers.cpp, and no
// MySim code reads it, so under determinism.sim_timers it stays on the
// ordinary (virtual) clock, as the original's. Calling the game's stopwatch
// methods from our code (an address outside the owner ranges too) does the
// same, so nothing here calls sim_timers::Claim: a claim would put the
// balloon on simulation time, which the original does not.
//
// Left the game's: the picture composition 0x00430800 and the window
// procedure 0x00430A40 that paints (drawing), the adjustor thunks 0x00430C30,
// 0x00430C40 and 0x00430C50, and the shared or folded 0x004302A0, 0x004302B0,
// 0x004302C0, 0x004302D0, 0x004302E0, 0x00430310, 0x004309F0, 0x00430A90
// and 0x00431180.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::mysim_agents {
namespace {

namespace field { // from the object's start
constexpr std::size_t kTarget = 0x4;
constexpr std::size_t kBase = 0x8;
constexpr std::size_t kState = 0x10;
constexpr std::size_t kAdvice = 0x14;
constexpr std::size_t kOwner = 0x18;
constexpr std::size_t kWindow = 0x1C;
constexpr std::size_t kIcons = 0x20;
constexpr std::size_t kIconsEnd = 0x24;
constexpr std::size_t kIconsCapacity = 0x28;
constexpr std::size_t kHasAdvice = 0x2C;
constexpr std::size_t kClock = 0x30;
constexpr std::size_t kDelayA = 0x48;
constexpr std::size_t kDelayShown = 0x4C;
constexpr std::size_t kDelayB = 0x50;
constexpr std::size_t kCreated = 0x54;
constexpr std::size_t kSound = 0x55;
} // namespace field

constexpr uint32_t kHeartbeat = 0xCB7CB509;
constexpr uint32_t kMessageIid = 0x65297976;
constexpr uint32_t kSoundServiceId = 0x23CECBA0;
constexpr uint32_t kSoundServiceIid = 0x106077D2;
constexpr uint32_t kInterfaceIid = 0x22E85D8E;
constexpr uint32_t kTargetIid = 0x452294AA;
constexpr uint32_t kWinGenClass = 0x4386D516;
constexpr uint32_t kWinGenIid = 0x5386D516;
constexpr uint32_t kSpeakEvent = 0xCA45190F;

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

// Slots 1 and 2 of a counted object.
void AddRef(void* object)
{
    Virtual<void>(object, 0x4);
}

void Release(void* object)
{
    Virtual<void>(object, 0x8);
}

// The sound service into `service` (empty when there is none), as the
// game's inlined code asks the framework.
void GetSoundService(void** service)
{
    *service = nullptr;
    if (void* const framework = Cdecl<void*>(addr::RZGetFramework))
        Virtual<uint8_t>(framework, 0x14, kSoundServiceId, kSoundServiceIid, service);
}

void DropSoundService(void** service)
{
    This<void>(addr::SC4RefPtr_DestructGuarded, service);
}

// A heartbeat message with no reference taken, as the game makes it.
void* NewHeartbeat()
{
    void* const memory = Cdecl<void*>(addr::RZ_operator_new, std::size_t{0x2C});
    void* const message = memory ? This<void*>(addr::cGZMessage_cGZMessage, memory) : nullptr;
    Virtual<void>(message, 0x14, kHeartbeat); // SetType
    return message;
}

// The game's 0x00430590 (ret 4): queues the heartbeat (a new message when
// none is given) for the balloon's message target, or for nobody.
void __fastcall PostHeartbeat(char* self, void*, void* message)
{
    if (!message)
        message = NewHeartbeat();
    void* const server = Global<void*>(addr::SC4_MessageServer);
    Virtual<void>(server, 0x1C, message, self ? static_cast<void*>(self + field::kTarget) : nullptr);
}

// The game's 0x00430250 (ret 8). The object (+0) and the message target
// (+4) answer for themselves, the rest goes to the counted base at +8.
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** out)
{
    *out = nullptr;
    if (iid == kInterfaceIid)
        *out = self;
    else if (iid == kTargetIid)
        *out = self ? self + field::kTarget : nullptr;
    if (*out) {
        Virtual<void>(self, 0x4);
        return 1;
    }
    return This<uint8_t>(addr::cRZBaseUnknown_QueryInterface1, self + field::kBase, iid, out);
}

// The game's 0x00430360 (thiscall, no arguments). The balloon's sound: for
// the current advice, a draw from the advisor system's generator against the
// advice's chance, then the sound service plays the advice's sound for the
// MySim's sex and is told the MySim's screen position. Draws from the
// generator at the advisor system +0x30 (city slot 81, its slot 9), a range
// of 2 to 100, once for every call that has an advice.
void __fastcall StartSound(char* self, void*)
{
    void* service;
    GetSoundService(&service);
    if (service) {
        void* const advice = At<void*>(self, field::kAdvice);
        if (advice) {
            void* owner = At<void*>(self, field::kOwner);
            const uint8_t male = Virtual<uint8_t>(owner, 0x48);
            const uint32_t key = Virtual<uint32_t>(advice, 0x28, uint32_t{male});
            void* const system = Virtual<void*>(Global<void*>(addr::SC4_City), 0x144);
            void* const advice2 = At<void*>(self, field::kAdvice);
            void* const random = Virtual<void*>(system, 0x24);
            const double draw = This<double>(addr::SC4Random_Uniform, random, 2.0, 100.0);
            const int32_t chance = Virtual<int32_t>(advice2, 0x2C);
            if (static_cast<double>(chance) > draw && key != 0 && Virtual<uint8_t>(At<void*>(self, field::kOwner), 0xE0)) {
                owner = At<void*>(self, field::kOwner);
                void* const current = At<void*>(self, field::kAdvice);
                const uint8_t sex = Virtual<uint8_t>(owner, 0x48);
                const uint32_t sound = Virtual<uint32_t>(current, 0x28, uint32_t{sex});
                Virtual<void>(service, 0x2C, uint32_t{1}, sound, owner, uint32_t{0}, uint32_t{0});
                float position[3];
                if (Virtual<uint8_t>(At<void*>(self, field::kOwner), 0x38, position)) {
                    Virtual<void>(service, 0x2C, kSpeakEvent, owner, static_cast<int32_t>(position[0]),
                                  static_cast<int32_t>(position[1]), static_cast<int32_t>(position[2]));
                    At<uint8_t>(self, field::kSound) = 1;
                }
            }
        }
    }
    DropSoundService(&service);
}

// The game's 0x004304C0 (ret 4). Stops the sound when it plays.
void __fastcall StopSound(char* self, void*, uint8_t flag)
{
    void* service;
    GetSoundService(&service);
    if (service && At<uint8_t>(self, field::kSound)) {
        Virtual<void>(service, 0x2C, uint32_t{2}, At<void*>(self, field::kOwner), uint32_t{flag != 0 ? 1u : 0u},
                      uint32_t{0}, uint32_t{0});
        At<uint8_t>(self, field::kSound) = 0;
    }
    DropSoundService(&service);
}

// The game's 0x00430530 (cSC4MySim::DropTripObject, called by the MySim's
// Shutdown): lets go of the advice, the MySim and the window.
uint8_t __fastcall DropTripObject(char* self, void*)
{
    if (At<uint8_t>(self, field::kCreated)) {
        if (void* const advice = At<void*>(self, field::kAdvice)) {
            At<void*>(self, field::kAdvice) = nullptr;
            Release(advice);
        }
        if (void* const owner = At<void*>(self, field::kOwner)) {
            At<void*>(self, field::kOwner) = nullptr;
            Release(owner);
        }
        if (void* const generator = At<void*>(self, field::kWindow)) {
            Virtual<void>(Virtual<void*>(generator, 0xC), 0x14);
            if (void* const again = At<void*>(self, field::kWindow)) {
                At<void*>(self, field::kWindow) = nullptr;
                Release(again);
            }
        }
        At<uint8_t>(self, field::kCreated) = 0;
    }
    return 1;
}

// The game's 0x004305F0. Follows the MySim: when it is active and the state
// is hidden the state becomes 4 and the clock restarts; when it is not
// active and the state is not hidden the window is hidden, the state becomes
// 5 and the clock stops. True when the state is not hidden afterwards.
uint8_t __fastcall Sync(char* self, void*)
{
    if (!At<uint8_t>(self, field::kCreated))
        return 0;
    void* const owner = At<void*>(self, field::kOwner);
    if (owner) {
        const uint8_t active = Virtual<uint8_t>(owner, 0x50);
        const int32_t state = At<int32_t>(self, field::kState);
        if (active) {
            if (state == 5) {
                At<int32_t>(self, field::kState) = 4;
                This<void>(addr::GZStopwatch_Start, self + field::kClock);
            }
        } else if (state != 5) {
            void* window = Virtual<void*>(At<void*>(self, field::kWindow), 0xC);
            if (Virtual<uint8_t>(window, 0x11C)) {
                window = Virtual<void*>(At<void*>(self, field::kWindow), 0xC);
                Virtual<void>(window, 0x118);
            }
            At<int32_t>(self, field::kState) = 5;
            This<void>(addr::SC4Stopwatch_Stop, self + field::kClock);
        }
    }
    return At<int32_t>(self, field::kState) != 5 ? 1 : 0;
}

// The game's 0x00430680. Makes the window generator once (flags and size
// set, added to the city view, the balloon as its window procedure), then
// restarts the clock and posts the first heartbeat. True.
uint8_t __fastcall Create(char* self, void*)
{
    if (!At<uint8_t>(self, field::kCreated)) {
        void*& generator = At<void*>(self, field::kWindow);
        if (void* const old = generator) {
            generator = nullptr;
            Release(old);
        }
        void* const com = Cdecl<void*>(addr::RZGetCOM);
        if (void* const old = generator) {
            generator = nullptr;
            Release(old);
        }
        Virtual<uint8_t>(com, 0xC, kWinGenClass, kWinGenIid, &generator);
        if (generator) {
            void* const window = Virtual<void*>(generator, 0xC);
            Virtual<void>(Global<void*>(addr::SC4_Service43DC8), 0x38, window);
            Virtual<void>(window, 0x190, uint32_t{1});
            Virtual<void>(window, 0x110, uint32_t{0x20}, uint32_t{1});
            const uint32_t size = At<uint32_t>(self, field::kDelayA);
            Virtual<void>(window, 0x1B4, size, size);
            Virtual<void>(window, 0xDC, uint32_t{0}, uint32_t{0}, uint32_t{0x64}, uint32_t{0x64});
            Virtual<void>(generator, 0x1C, self);
            Virtual<void>(generator, 0x18, uint32_t{0x1000}, uint32_t{1});
            Virtual<void>(window, 0x118);
            Virtual<void>(window, 0x110, uint32_t{0x8000}, uint32_t{0});
            Virtual<void>(window, 0x110, uint32_t{0x200000}, uint32_t{1});
            Virtual<void>(window, 0x110, uint32_t{0x20000}, uint32_t{1});
            Virtual<void>(window, 0x110, uint32_t{0x40000}, uint32_t{1});
            Virtual<void>(window, 0x110, uint32_t{4}, uint32_t{1});
            Virtual<void>(window, 0x60);
            Virtual<void>(window, 0x100, uint32_t{0x4C30E4FA});
        }
        At<uint8_t>(self, field::kCreated) = 1;
    }
    This<void>(addr::GZStopwatch_Start, self + field::kClock);
    PostHeartbeat(self, nullptr, nullptr);
    return 1;
}

// The game's 0x00430B60 (ret 4). The constructor: the owner (the MySim's
// cISC4MySim part) is kept with a reference. The sound service is asked for
// and let go again (the stop it would send never runs: the sound byte was
// just cleared). No generator is made here.
void* __fastcall Construct(char* self, void*, void* owner)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4NewsTicker_WinProcBaseVtable;
    At<std::uintptr_t>(self, 0x4) = addr::cSC4TrafficSimulator_VtableBase4;
    This<void*>(addr::cRZBaseUnknown_Construct, self + field::kBase);
    At<std::uintptr_t>(self, 0x0) = addr::SC4MySimBalloon_Vtable;
    At<std::uintptr_t>(self, 0x4) = addr::SC4MySimBalloon_TargetVtable;
    At<std::uintptr_t>(self + field::kBase, 0x0) = addr::SC4MySimBalloon_BaseVtable;
    At<int32_t>(self, field::kState) = 5;
    At<void*>(self, field::kAdvice) = nullptr;
    At<void*>(self, field::kOwner) = owner;
    if (owner)
        AddRef(owner);
    At<void*>(self, field::kWindow) = nullptr;
    At<void*>(self, field::kIcons) = nullptr;
    At<void*>(self, field::kIconsEnd) = nullptr;
    At<void*>(self, field::kIconsCapacity) = nullptr;
    At<uint8_t>(self, field::kHasAdvice) = 0;
    This<void*>(addr::SC4Stopwatch_Construct, self + field::kClock, uint32_t{4});
    At<uint32_t>(self, field::kDelayA) = 300;
    At<uint32_t>(self, field::kDelayShown) = 2500;
    At<uint32_t>(self, field::kDelayB) = 300;
    At<uint8_t>(self, field::kCreated) = 0;
    At<uint8_t>(self, field::kSound) = 0;

    void* service;
    GetSoundService(&service);
    if (service && At<uint8_t>(self, field::kSound)) {
        Virtual<void>(service, 0x2C, uint32_t{2}, At<void*>(self, field::kOwner), uint32_t{1}, uint32_t{0},
                      uint32_t{0});
        At<uint8_t>(self, field::kSound) = 0;
    }
    DropSoundService(&service);
    return self;
}

// The game's 0x00430C60. The destructor: the clock, the icons and the three
// references (the counted base last).
void __fastcall Destruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4MySimBalloon_Vtable;
    At<std::uintptr_t>(self, 0x4) = addr::SC4MySimBalloon_TargetVtable;
    At<std::uintptr_t>(self + field::kBase, 0x0) = addr::SC4MySimBalloon_BaseVtable;
    This<void>(addr::SC4Stopwatch_Destruct, self + field::kClock);
    This<void>(addr::SC4RefVector_Destroy, self + field::kIcons);
    if (void* const window = At<void*>(self, field::kWindow))
        Release(window);
    if (void* const owner = At<void*>(self, field::kOwner))
        Release(owner);
    if (void* const advice = At<void*>(self, field::kAdvice))
        Release(advice);
    This<void>(addr::cRZBaseUnknown_Destruct, self + field::kBase);
}

// The game's 0x00430CC0 (ret 4), the scalar deleting destructor.
void* __fastcall DeletingDestruct(char* self, void*, uint32_t flags)
{
    Destruct(self, nullptr);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// Replaces the icon at `index` of the vector with the advice part's icon:
// let go of the old one, ask the part for the new one and keep it counted.
void TakeIcon(char* self, void* part, int32_t index)
{
    void** const slot = reinterpret_cast<void**>(At<char*>(self, field::kIcons) + static_cast<std::size_t>(index) * 4);
    if (void* const old = *slot) {
        *slot = nullptr;
        Release(old);
    }
    void* const item = Virtual<void*>(part, 0x20, index);
    if (item) {
        void* const existing = *slot;
        if (item != existing) {
            AddRef(item);
            *slot = item;
            if (existing)
                Release(existing);
        }
    }
}

// The game's 0x00430CE0. The queue update: drops the current advice (its
// part told 4/1 and 2/0 on its slot 0x34) and the icons, then asks the MySim
// for the next event advice (slot 0xF4) and, when that gave no icon, the next
// ordinary advice (slot 0xF0), takes the advice and its icons, and sizes the
// window to the largest icon. True when there are icons.
uint8_t __fastcall UpdateQueue(char* self, void*)
{
    if (At<void*>(self, field::kAdvice)) {
        Virtual<void>(At<void*>(self, field::kAdvice), 0x34, uint32_t{4}, uint32_t{1});
        Virtual<void>(At<void*>(self, field::kAdvice), 0x34, uint32_t{2}, uint32_t{0});
        if (void* const advice = At<void*>(self, field::kAdvice)) {
            At<void*>(self, field::kAdvice) = nullptr;
            Release(advice);
        }
        This<void*>(addr::SC4RefVector_Erase, self + field::kIcons, At<void*>(self, field::kIcons),
                    At<void*>(self, field::kIconsEnd));
    }

    void* part = Virtual<void*>(At<void*>(self, field::kOwner), 0xF4);
    bool found = false;
    if (part) {
        const int32_t count = Virtual<int32_t>(part, 0x1C);
        This<void>(addr::SC4RefVector_Resize, self + field::kIcons, count);
        for (int32_t i = 0; i < count; ++i) {
            void** const slot =
                reinterpret_cast<void**>(At<char*>(self, field::kIcons) + static_cast<std::size_t>(i) * 4);
            if (void* const old = *slot) {
                *slot = nullptr;
                Release(old);
            }
            void* const item = Virtual<void*>(part, 0x20, i);
            if (item) {
                void* const existing = *slot;
                if (item != existing) {
                    AddRef(item);
                    *slot = item;
                    if (existing)
                        Release(existing);
                }
                found = true;
            }
        }
        if (found) {
            void* const kept = At<void*>(self, field::kAdvice);
            if (part != kept) {
                AddRef(part);
                At<void*>(self, field::kAdvice) = part;
                if (kept)
                    Release(kept);
            }
            Virtual<void>(At<void*>(self, field::kAdvice), 0x34, uint32_t{2}, uint32_t{1});
        } else {
            Virtual<void>(part, 0x34, uint32_t{4}, uint32_t{1});
        }
    }

    part = Virtual<void*>(At<void*>(self, field::kOwner), 0xF0);
    if (!At<void*>(self, field::kAdvice) && part) {
        const int32_t count = Virtual<int32_t>(part, 0x1C);
        This<void>(addr::SC4RefVector_Resize, self + field::kIcons, count);
        for (int32_t i = 0; i < count; ++i)
            TakeIcon(self, part, i);
        void* const kept = At<void*>(self, field::kAdvice);
        if (part != kept) {
            AddRef(part);
            At<void*>(self, field::kAdvice) = part;
            if (kept)
                Release(kept);
        }
        Virtual<void>(At<void*>(self, field::kAdvice), 0x34, uint32_t{2}, uint32_t{1});
    }

    const int32_t icons = (At<std::intptr_t>(self, field::kIconsEnd) - At<std::intptr_t>(self, field::kIcons)) >> 2;
    int32_t width = 0;
    int32_t height = 0;
    for (int32_t i = 0; i < icons; ++i) {
        void* icon = reinterpret_cast<void**>(At<char*>(self, field::kIcons))[i];
        const int32_t w = Virtual<int32_t>(icon, 0x24);
        if (w > width)
            width = w;
        icon = reinterpret_cast<void**>(At<char*>(self, field::kIcons))[i];
        const int32_t h = Virtual<int32_t>(icon, 0x28);
        if (h > height)
            height = h;
    }
    void* const window = Virtual<void*>(At<void*>(self, field::kWindow), 0xC);
    if (Virtual<int32_t>(window, 0xA4) < width || Virtual<int32_t>(window, 0xA8) < height)
        Virtual<void>(window, 0xD4, width, height);
    return At<char*>(self, field::kIcons) != At<char*>(self, field::kIconsEnd) ? 1 : 0;
}

// The game's 0x00430F70 (ret 4), slot 3 of the message target (`target` is
// the object plus 4). Only the heartbeat 0xCB7CB509 is handled: it is queued
// again at once, the state follows the MySim (Sync), and when the balloon is
// not hidden the window goes over the MySim's screen position (centred on it,
// its height above it) and the state moves on by the clock:
//   1 after delay A: start the sound, state 3
//   2 after delay A: stop the sound, state 4
//   3 after the shown delay: hide the window, state 2
//   4 after delay B: update the queue; with icons: stop the sound, show the
//     window, state 1, set +0x2C
// Each step restarts the clock. True for the heartbeat.
uint8_t __fastcall DoMessage(char* target, void*, void* message)
{
    char* const self = target - field::kTarget;
    uint8_t handled = 0;
    void* typed = nullptr;
    Virtual<uint8_t>(message, 0x0, kMessageIid, &typed);
    if (!typed)
        return 0;
    if (Virtual<uint32_t>(typed, 0x10) == kHeartbeat) {
        PostHeartbeat(self, nullptr, message);
        const uint8_t shown = Sync(self, nullptr);
        handled = 1;
        if (shown) {
            void* const window = Virtual<void*>(At<void*>(self, field::kWindow), 0xC);
            int32_t position[2];
            position[0] = -Virtual<int32_t>(window, 0xA4);
            position[1] = -Virtual<int32_t>(window, 0xA8);
            if (Virtual<uint8_t>(At<void*>(self, field::kOwner), 0xDC, &position[0], &position[1])) {
                position[0] -= Virtual<int32_t>(window, 0xA4) / 2;
                position[1] += -8 - Virtual<int32_t>(window, 0xA8);
            }
            Virtual<void>(window, 0xE0, position[0], position[1]);

            char* const clock = self + field::kClock;
            const uint32_t state = static_cast<uint32_t>(At<int32_t>(self, field::kState)) - 1;
            if (state <= 3) {
                switch (state) {
                case 0:
                    if (This<uint32_t>(addr::GZStopwatch_ElapsedMilliseconds, clock) > At<uint32_t>(self, field::kDelayA)) {
                        StartSound(self, nullptr);
                        At<int32_t>(self, field::kState) = 3;
                        This<void>(addr::GZStopwatch_Start, clock);
                    }
                    break;
                case 1:
                    if (This<uint32_t>(addr::GZStopwatch_ElapsedMilliseconds, clock) > At<uint32_t>(self, field::kDelayA)) {
                        StopSound(self, nullptr, 0);
                        At<int32_t>(self, field::kState) = 4;
                        This<void>(addr::GZStopwatch_Start, clock);
                    }
                    break;
                case 2:
                    if (This<uint32_t>(addr::GZStopwatch_ElapsedMilliseconds, clock) >
                        At<uint32_t>(self, field::kDelayShown)) {
                        Virtual<void>(Virtual<void*>(At<void*>(self, field::kWindow), 0xC), 0x118);
                        At<int32_t>(self, field::kState) = 2;
                        This<void>(addr::GZStopwatch_Start, clock);
                    }
                    break;
                default:
                    if (This<uint32_t>(addr::GZStopwatch_ElapsedMilliseconds, clock) > At<uint32_t>(self, field::kDelayB)) {
                        if (UpdateQueue(self, nullptr)) {
                            StopSound(self, nullptr, 0);
                            Virtual<void>(Virtual<void*>(At<void*>(self, field::kWindow), 0xC), 0x114);
                            At<int32_t>(self, field::kState) = 1;
                            This<void>(addr::GZStopwatch_Start, clock);
                            Virtual<void>(Virtual<void*>(At<void*>(self, field::kWindow), 0xC), 0x170);
                            At<uint8_t>(self, field::kHasAdvice) = 1;
                        }
                    }
                    break;
                }
            }
        }
    }
    Release(typed);
    return handled;
}

bool InstallBalloon(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[16] = {};
    static const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::SC4MySimBalloon_QueryInterface, reinterpret_cast<void*>(&QueryInterface)},
        {addr::SC4MySimBalloon_StartSound, reinterpret_cast<void*>(&StartSound)},
        {addr::SC4MySimBalloon_StopSound, reinterpret_cast<void*>(&StopSound)},
        {addr::cSC4MySim_DropTripObject, reinterpret_cast<void*>(&DropTripObject)},
        {addr::SC4MySimBalloon_PostHeartbeat, reinterpret_cast<void*>(&PostHeartbeat)},
        {addr::SC4MySimBalloon_Sync, reinterpret_cast<void*>(&Sync)},
        {addr::SC4MySimBalloon_Create, reinterpret_cast<void*>(&Create)},
        {addr::SC4MySimBalloon_Construct, reinterpret_cast<void*>(&Construct)},
        {addr::SC4MySimBalloon_Destruct, reinterpret_cast<void*>(&Destruct)},
        {addr::SC4MySimBalloon_DeletingDestruct, reinterpret_cast<void*>(&DeletingDestruct)},
        {addr::SC4MySimBalloon_UpdateQueue, reinterpret_cast<void*>(&UpdateQueue)},
        {addr::SC4MySimBalloon_DoMessage, reinterpret_cast<void*>(&DoMessage)},
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

const bool g_balloonRegistered = registry::Register({
    "simulators.mysim_agents.balloon",
    "simulators/mysim_agents",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4MySimBalloon_DoMessage,
    "Our MySim balloon object: the heartbeat state machine, queue update, sound, window and lifetime (docs/mysim-agents.md)",
    false,
    InstallBalloon,
});

} // namespace
} // namespace opensc4::mysim_agents
