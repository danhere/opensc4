// The effect trigger tool of the city view (0x004BABE0 to 0x004BB8FD).
// Replacement, off by default:
//
//   ui.tools.disaster_effect: the view input control (vtable 0x00A902E0, 31
//     slots, 0xA4 bytes of its own) the common disaster tool is built on. It
//     is never made by itself: the constructor's only caller is the common
//     disaster tool's (0x00580220).
//
// The tool is a set of effects around the pointer. It is given either a name
// (0x004BB590) or a number (0x004BB160) and takes its effects from the
// effects manager (0x00B43D1C) by the name with a suffix (slot 7) or by the
// number with a kind (slot 6):
//
//   +0x90  "_button"    kind 5  started as soon as the tool has it; told the
//                               place and the press while a press lasts
//   +0x94  "_control"   kind 7  what slot 30 starts and stops
//   +0x98  "_single"    kind 4  started again at every press
//   +0x9C  "_active"    kind 1  runs while a press lasts, at the pointer
//   +0xA0  "_inactive"  kind 2  runs at the pointer between presses
//   (none) "_trigger"   kind 3  made, started and let go at every press, when
//                               the manager knows the name (slot 5)
//
// The pointer's place on the ground is the view's pick (slot 0x90 with the
// byte at +0x8F as its last argument); every effect is told through a block
// of 0x38 bytes at +0x38 (a count of changes, flags, a rotation, the
// position, the scale).
//
// A press (OnMouseDownL) captures the mouse; the release (OnMouseUpL) lets
// it go. When the "_control" effect was not running at the press, the
// release calls slot 30 with 1; when it was, the press and the drag tell it
// the place (its slot 11 with 0xA) and a value of 1 (slot 12 with 0xB), the
// release a value of 0, and a release with the modifier bit 2 calls slot 30
// with 0. Slot 30 here starts the "_control" effect at the place or stops
// it; the common disaster tool has its own.
//
// Nothing here touches the city: every call is on an effect, the effects
// manager, the view's pick or the mouse capture. No clock and no random
// number is read.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::tool_effect {
namespace {

// The base class's fields.
namespace base {
constexpr std::size_t kInited = 0x04; // byte
constexpr std::size_t kCursor = 0x10; // cursor ID
constexpr std::size_t kView = 0x1C;   // the city view
} // namespace base

// The tool's fields. +0x2B is padding; +0x29, +0x2A, +0x8D and +0x8E are not
// set by the constructor.
namespace field {
constexpr std::size_t kPressed = 0x28;       // byte: a press lasts (the mouse is captured)
constexpr std::size_t kStarted = 0x29;       // byte: set by a press, cleared by a release and by Init
constexpr std::size_t kIdle = 0x2A;          // byte: the control effect was not running at the press
constexpr std::size_t kPlace = 0x2C;         // float[3]: the pointer's place on the ground
constexpr std::size_t kTransform = 0x38;     // 0x38 bytes: what the effects are told
constexpr std::size_t kEffects = 0x70;       // the effects manager (referenced)
constexpr std::size_t kName = 0x74;          // a string of the game's, 0x14 bytes
constexpr std::size_t kNameBody = 0x78;      // its begin, end, capacity
constexpr std::size_t kNameZero = 0x84;
constexpr std::size_t kNumber = 0x88;        // the effects' number, when not by name
constexpr std::size_t kHasAny = 0x8C;        // byte: any of the seven was found
constexpr std::size_t kHasButtonTrigger = 0x8D; // byte: the manager knows name + "_button_trigger"
constexpr std::size_t kHasTrigger = 0x8E;    // byte: the manager knows name + "_trigger"
constexpr std::size_t kPickFlag = 0x8F;      // byte: the pick's last argument
constexpr std::size_t kButton = 0x90;        // the effects (referenced)
constexpr std::size_t kControl = 0x94;
constexpr std::size_t kSingle = 0x98;
constexpr std::size_t kActive = 0x9C;
constexpr std::size_t kInactive = 0xA0;
} // namespace field

// The kinds the effects manager's slot 6 takes with a number.
namespace kind {
constexpr uint32_t kActive = 1;
constexpr uint32_t kInactive = 2;
constexpr uint32_t kTrigger = 3;
constexpr uint32_t kSingle = 4;
constexpr uint32_t kButton = 5;
constexpr uint32_t kControl = 7;
} // namespace kind

constexpr uint32_t kEffectPlace = 0xA; // what an effect's slot 11 is told the place under
constexpr uint32_t kEffectValue = 0xB; // what an effect's slot 12 is told a value under

// What an effect is told where it is: a count of changes, flags (1 the
// position), a rotation, the position, the scale.
struct EffectTransform {
    uint8_t changes;
    uint8_t flags;
    uint8_t unused[2];
    float rotation[9];
    float position[3];
    float scale;
};
static_assert(sizeof(EffectTransform) == 0x38);

// The storage of a string of the game's: 0xC bytes on the stacks of
// 0x004BB160, 0x004BB360 and 0x004BB590.
struct StringBody {
    char* begin;
    char* end;
    char* capacity;
};
static_assert(sizeof(StringBody) == 0xC);

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

EffectTransform* Transform(char* self)
{
    return &At<EffectTransform>(self, field::kTransform);
}

// The pick both mouse handlers begin with: the view's slot 0x90 (screen x,
// screen z, the place out, the tool's flag). When it finds ground the place
// goes into the block: the position, flag 1, one more change.
bool Pick(char* self, int32_t x, int32_t z)
{
    float* const place = &At<float>(self, field::kPlace);
    const uint32_t flag = At<uint8_t>(self, field::kPickFlag);
    if (!Virtual<uint8_t>(At<void*>(self, base::kView), 0x90, x, z, place, flag))
        return false;
    EffectTransform* const transform = Transform(self);
    transform->position[0] = place[0];
    transform->position[1] = place[1];
    transform->position[2] = place[2];
    transform->flags |= 0x01;
    transform->changes = static_cast<uint8_t>(transform->changes + 1);
    return true;
}

// Whether the effects manager knows the tool's name with a suffix (its slot
// 5). The joined name is the game's (0x0041E270) and is freed after the call
// when it has storage.
uint8_t KnowsName(char* self, const char* suffix)
{
    void* const effects = At<void*>(self, field::kEffects);
    StringBody joined;
    const StringBody* const made =
        Cdecl<StringBody*>(addr::SC4String_ConcatBodyText, &joined, self + field::kNameBody, suffix);
    const uint8_t known = Virtual<uint8_t>(effects, 0x14, made->begin);
    if (joined.begin)
        Cdecl<void>(addr::SC4_Free, joined.begin);
    return known;
}

// An effect the tool holds is let go and taken again from the manager by
// the tool's name with a suffix (slot 7). The manager is read before the
// old effect is let go, as in the game.
uint8_t TakeByName(char* self, std::size_t offset, const char* suffix)
{
    void* const effects = At<void*>(self, field::kEffects);
    if (void* const old = At<void*>(self, offset)) {
        At<void*>(self, offset) = nullptr;
        Release(old);
    }
    StringBody joined;
    const StringBody* const made =
        Cdecl<StringBody*>(addr::SC4String_ConcatBodyText, &joined, self + field::kNameBody, suffix);
    const uint8_t found = Virtual<uint8_t>(effects, 0x1C, made->begin, &At<void*>(self, offset));
    if (joined.begin)
        Cdecl<void>(addr::SC4_Free, joined.begin);
    return found;
}

// The same by the tool's number and a kind (slot 6).
uint8_t TakeByNumber(char* self, std::size_t offset, uint32_t effectKind)
{
    void* const effects = At<void*>(self, field::kEffects);
    if (void* const old = At<void*>(self, offset)) {
        At<void*>(self, offset) = nullptr;
        Release(old);
    }
    return Virtual<uint8_t>(effects, 0x18, At<uint32_t>(self, field::kNumber), effectKind, &At<void*>(self, offset));
}

// --- Init and Shutdown -----------------------------------------------------------------

// The game's 0x004BABE0 (slot 3): Init. Unless inited: the base Init, the
// effects manager kept (referenced; what was kept before let go), +0x29
// cleared. True.
uint8_t __fastcall Init(char* self, void*)
{
    if (At<uint8_t>(self, base::kInited))
        return 1;
    This<uint8_t>(addr::SC4ViewInputControl_Init, self);
    void* const effects = Global<void*>(addr::SC4_Service43D1C);
    void* const old = At<void*>(self, field::kEffects);
    if (effects != old) {
        if (effects)
            AddRef(effects);
        At<void*>(self, field::kEffects) = effects;
        if (old)
            Release(old);
    }
    At<uint8_t>(self, field::kStarted) = 0;
    return 1;
}

// An effect the tool holds is stopped (its slot 4 with 0) and let go. The
// field is read again after the stop, as in the game.
void StopAndDrop(char* self, std::size_t offset)
{
    if (void* const effect = At<void*>(self, offset)) {
        Virtual<void>(effect, 0x10, uint32_t{0});
        if (void* const still = At<void*>(self, offset)) {
            At<void*>(self, offset) = nullptr;
            Release(still);
        }
    }
}

// The game's 0x004BAC30 (slot 4): Shutdown. When inited: a press ended (the
// mouse let go), the effects manager let go, the five effects stopped and
// let go (button, active, inactive, single, control), the base Shutdown.
// True.
uint8_t __fastcall Shutdown(char* self, void*)
{
    if (!At<uint8_t>(self, base::kInited))
        return 1;
    if (At<uint8_t>(self, field::kPressed)) {
        At<uint8_t>(self, field::kPressed) = 0;
        This<uint8_t>(addr::SC4ViewInputControl_EndCapture, self);
    }
    if (void* const effects = At<void*>(self, field::kEffects)) {
        At<void*>(self, field::kEffects) = nullptr;
        Release(effects);
    }
    StopAndDrop(self, field::kButton);
    StopAndDrop(self, field::kActive);
    StopAndDrop(self, field::kInactive);
    StopAndDrop(self, field::kSingle);
    StopAndDrop(self, field::kControl);
    This<uint8_t>(addr::SC4ViewInputControl_Shutdown, self);
    return 1;
}

// --- The mouse -------------------------------------------------------------------------

// The game's 0x004BAD30 (slot 20, OnMouseMove, ret 0xC). The view is not
// checked. Over ground: during a press that found the control effect
// running, the control and button effects are told the place (slot 11 with
// 0xA, the place, 1); the active effect is told the block; the inactive
// effect is started (slot 3 with 0) when no press has begun and it does not
// run (slot 5), and told the block. Off the ground the inactive effect is
// stopped. True.
uint8_t __fastcall OnMouseMove(char* self, void*, int32_t x, int32_t z, uint32_t)
{
    if (!Pick(self, x, z)) {
        if (void* const inactive = At<void*>(self, field::kInactive))
            Virtual<void>(inactive, 0x10, uint32_t{0});
        return 1;
    }
    float* const place = &At<float>(self, field::kPlace);
    if (At<uint8_t>(self, field::kPressed) && !At<uint8_t>(self, field::kIdle)) {
        if (void* const control = At<void*>(self, field::kControl))
            Virtual<void>(control, 0x2C, kEffectPlace, place, uint32_t{1});
        if (void* const button = At<void*>(self, field::kButton))
            Virtual<void>(button, 0x2C, kEffectPlace, place, uint32_t{1});
    }
    if (void* const active = At<void*>(self, field::kActive))
        Virtual<void>(active, 0x20, Transform(self));
    if (void* const inactive = At<void*>(self, field::kInactive)) {
        if (!At<uint8_t>(self, field::kStarted) && !Virtual<uint8_t>(inactive, 0x14))
            Virtual<void>(At<void*>(self, field::kInactive), 0xC, uint32_t{0});
        Virtual<void>(At<void*>(self, field::kInactive), 0x20, Transform(self));
    }
    return 1;
}

// The game's 0x004BAE20 (slot 18, OnMouseUpL, ret 0xC). False without a
// press. Else: +0x29 cleared; the active effect stopped; the inactive
// effect told the block and started; the control and button effects told a
// value of 0 (slot 12 with 0xB, the value, 1). With a control effect: when
// it was not running at the press, slot 30 with 1; else, with the modifier
// bit 2, slot 30 with 0. The mouse let go, the press over. True.
uint8_t __fastcall OnMouseUpL(char* self, void*, int32_t, int32_t, uint32_t modifiers)
{
    if (!At<uint8_t>(self, field::kPressed))
        return 0;
    const bool modifier = ((modifiers >> 1) & 1) != 0;
    At<uint8_t>(self, field::kStarted) = 0;
    if (void* const active = At<void*>(self, field::kActive))
        Virtual<void>(active, 0x10, uint32_t{0});
    if (void* const inactive = At<void*>(self, field::kInactive)) {
        Virtual<void>(inactive, 0x20, Transform(self));
        Virtual<void>(At<void*>(self, field::kInactive), 0xC, uint32_t{0});
    }
    float value = 0.0f;
    if (void* const control = At<void*>(self, field::kControl)) {
        value = 0.0f;
        Virtual<void>(control, 0x30, kEffectValue, &value, uint32_t{1});
    }
    if (void* const button = At<void*>(self, field::kButton)) {
        value = 0.0f;
        Virtual<void>(button, 0x30, kEffectValue, &value, uint32_t{1});
    }
    if (At<void*>(self, field::kControl)) {
        if (At<uint8_t>(self, field::kIdle))
            Virtual<void>(self, 0x78, uint32_t{1});
        else if (modifier)
            Virtual<void>(self, 0x78, uint32_t{0});
    }
    This<uint8_t>(addr::SC4ViewInputControl_EndCapture, self);
    At<uint8_t>(self, field::kPressed) = 0;
    return 1;
}

// The game's 0x004BAF00 (slot 22, the pointer left the view): the inactive
// effect stopped; then the base's slot (0x00648EF0), which is false.
uint8_t __fastcall OnMouseExit(char* self, void*)
{
    if (void* const inactive = At<void*>(self, field::kInactive))
        Virtual<void>(inactive, 0x10, uint32_t{0});
    return 0;
}

// The game's 0x004BAF20 (slot 30, one argument): with it set, the control
// effect is told the block and started (slot 3 with 0); else stopped (slot
// 4 with 0). The effect is not checked; the function ends in a jump to the
// effect's slot, so what that leaves in EAX is what the caller gets.
uint32_t __fastcall Switch(char* self, void*, uint8_t on)
{
    void* const control = At<void*>(self, field::kControl);
    if (on) {
        Virtual<void>(control, 0x20, Transform(self));
        return Virtual<uint32_t>(At<void*>(self, field::kControl), 0xC, uint32_t{0});
    }
    return Virtual<uint32_t>(control, 0x10, uint32_t{0});
}

// The game's 0x004BB360 (ret 4; the argument is not read): the trigger
// effect. By name when the tool has one (the manager's slot 7 with name +
// "_trigger"), else by the number with kind 3 (slot 6; nothing without a
// number). The effect is told the block, started and let go.
void __fastcall Trigger(char* self, void*, void*)
{
    void* effect = nullptr;
    if (At<char*>(self, field::kNameBody) != At<char*>(self, field::kNameBody + 4)) {
        void* const effects = At<void*>(self, field::kEffects);
        StringBody joined;
        const StringBody* const made =
            Cdecl<StringBody*>(addr::SC4String_ConcatBodyText, &joined, self + field::kNameBody, "_trigger");
        Virtual<uint8_t>(effects, 0x1C, made->begin, &effect);
        if (joined.begin)
            Cdecl<void>(addr::SC4_Free, joined.begin);
    } else {
        const uint32_t number = At<uint32_t>(self, field::kNumber);
        if (number == 0)
            return;
        Virtual<uint8_t>(At<void*>(self, field::kEffects), 0x18, number, kind::kTrigger, &effect);
    }
    if (!effect)
        return;
    Virtual<void>(effect, 0x20, Transform(self));
    Virtual<void>(effect, 0xC, uint32_t{0});
    if (effect)
        Release(effect);
}

// The game's 0x004BB410 (slot 16, OnMouseDownL, ret 0xC). False without a
// view; true and nothing more off the ground. Else: the trigger effect when
// the manager knows its name (+0x8E); +0x29 set; the active effect told the
// block and started; the inactive effect stopped; the single effect told the
// block, stopped if it runs, and started. +0x2A is whether there is a
// control effect that does not run; when it is not so, the control and
// button effects are told a value of 1 and the place. The press begins: the
// mouse captured, the cursor set again (slot 8 with the cursor ID). True.
uint8_t __fastcall OnMouseDownL(char* self, void*, int32_t x, int32_t z, uint32_t)
{
    if (!At<void*>(self, base::kView))
        return 0;
    if (!Pick(self, x, z))
        return 1;
    float* const place = &At<float>(self, field::kPlace);
    if (At<uint8_t>(self, field::kHasTrigger))
        Trigger(self, nullptr, Transform(self));
    At<uint8_t>(self, field::kStarted) = 1;
    if (void* const active = At<void*>(self, field::kActive)) {
        Virtual<void>(active, 0x20, Transform(self));
        Virtual<void>(At<void*>(self, field::kActive), 0xC, uint32_t{0});
    }
    if (void* const inactive = At<void*>(self, field::kInactive))
        Virtual<void>(inactive, 0x10, uint32_t{0});
    if (void* const single = At<void*>(self, field::kSingle)) {
        Virtual<void>(single, 0x20, Transform(self));
        if (Virtual<uint8_t>(At<void*>(self, field::kSingle), 0x14))
            Virtual<void>(At<void*>(self, field::kSingle), 0x10, uint32_t{0});
        Virtual<void>(At<void*>(self, field::kSingle), 0xC, uint32_t{0});
    }
    uint8_t idle = 0;
    if (void* const control = At<void*>(self, field::kControl)) {
        if (!Virtual<uint8_t>(control, 0x14))
            idle = 1;
    }
    At<uint8_t>(self, field::kIdle) = idle;
    if (!idle) {
        float value = 1.0f;
        if (void* const control = At<void*>(self, field::kControl)) {
            Virtual<void>(control, 0x30, kEffectValue, &value, uint32_t{1});
            Virtual<void>(At<void*>(self, field::kControl), 0x2C, kEffectPlace, place, uint32_t{1});
        }
        if (void* const button = At<void*>(self, field::kButton)) {
            Virtual<void>(button, 0x30, kEffectValue, &value, uint32_t{1});
            Virtual<void>(At<void*>(self, field::kButton), 0x2C, kEffectPlace, place, uint32_t{1});
        }
    }
    At<uint8_t>(self, field::kPressed) = 1;
    This<uint8_t>(addr::SC4ViewInputControl_BeginCapture, self);
    Virtual<void>(self, 0x20, At<uint32_t>(self, base::kCursor));
    return 1;
}

// --- What the view sets the tool up with -----------------------------------------------

// The game's 0x004BB160 (ret 4): the effects by number. The number is
// kept; +0x8D and +0x8E are whether the manager knows the name (as it is,
// usually empty) + "_button_trigger" and + "_trigger"; the five effects are
// let go and taken by the number and their kinds (button, single, control,
// active, inactive); +0x8C is whether any of the seven answers was true;
// the button effect, when there is one, is started. The effects manager is
// not checked.
void __fastcall SetEffectNumber(char* self, void*, uint32_t number)
{
    At<uint32_t>(self, field::kNumber) = number;
    At<uint8_t>(self, field::kHasButtonTrigger) = KnowsName(self, "_button_trigger");
    At<uint8_t>(self, field::kHasTrigger) = KnowsName(self, "_trigger");
    bool any = At<uint8_t>(self, field::kHasButtonTrigger) || At<uint8_t>(self, field::kHasTrigger);
    any = TakeByNumber(self, field::kButton, kind::kButton) || any;
    any = TakeByNumber(self, field::kSingle, kind::kSingle) || any;
    any = TakeByNumber(self, field::kControl, kind::kControl) || any;
    any = TakeByNumber(self, field::kActive, kind::kActive) || any;
    any = TakeByNumber(self, field::kInactive, kind::kInactive) || any;
    At<uint8_t>(self, field::kHasAny) = any ? 1 : 0;
    if (void* const button = At<void*>(self, field::kButton))
        Virtual<void>(button, 0xC, uint32_t{0});
}

// The game's 0x004BB590 (ret 4): the effects by name. The name is copied
// into the tool's string (a string made of the text, assigned unless it is
// the tool's own, freed); then as by number, with the manager's slot 7 and
// the suffixes "_button", "_single", "_control", "_active", "_inactive".
void __fastcall SetEffectName(char* self, void*, const char* name)
{
    StringBody text = {nullptr, nullptr, nullptr};
    uint8_t allocator = 0;
    This<void>(addr::SC4String_FromRange, &text, name, name + std::strlen(name), &allocator);
    char* const storage = text.begin;
    if (reinterpret_cast<char*>(&text) != self + field::kNameBody)
        This<void>(addr::SC4String_AssignRange, self + field::kNameBody, storage, text.end, &allocator);
    if (storage)
        Cdecl<void>(addr::SC4_Free, storage);
    At<uint8_t>(self, field::kHasButtonTrigger) = KnowsName(self, "_button_trigger");
    At<uint8_t>(self, field::kHasTrigger) = KnowsName(self, "_trigger");
    bool any = At<uint8_t>(self, field::kHasButtonTrigger) || At<uint8_t>(self, field::kHasTrigger);
    any = TakeByName(self, field::kButton, "_button") || any;
    any = TakeByName(self, field::kSingle, "_single") || any;
    any = TakeByName(self, field::kControl, "_control") || any;
    any = TakeByName(self, field::kActive, "_active") || any;
    any = TakeByName(self, field::kInactive, "_inactive") || any;
    At<uint8_t>(self, field::kHasAny) = any ? 1 : 0;
    if (void* const button = At<void*>(self, field::kButton))
        Virtual<void>(button, 0xC, uint32_t{0});
}

// --- The object ------------------------------------------------------------------------

// The game's 0x004BAF60: the constructor (no arguments, returns this). The
// place is the dword at 0x00B21804 three times; the block is no change, no
// flags, the unit rotation, position 0, scale 1; the name is an empty string
// with 8 bytes of storage; the number, the flags at +0x28, +0x8C and +0x8F
// and the five effects are 0.
char* __fastcall Construct(char* self, void*)
{
    This<void*>(addr::SC4ViewInputControl_Construct, self);
    At<std::uintptr_t>(self, 0) = addr::SC4EffectTool_Vtable;
    At<uint8_t>(self, field::kPressed) = 0;
    const uint32_t filler = Global<uint32_t>(addr::SC4_Global21804);
    At<uint32_t>(self, field::kPlace) = filler;
    At<uint32_t>(self, field::kPlace + 4) = filler;
    At<uint32_t>(self, field::kPlace + 8) = filler;
    EffectTransform* const transform = Transform(self);
    transform->rotation[0] = 1.0f;
    transform->rotation[1] = 0.0f;
    transform->rotation[2] = 0.0f;
    transform->rotation[3] = 0.0f;
    transform->rotation[4] = 1.0f;
    transform->rotation[5] = 0.0f;
    transform->rotation[6] = 0.0f;
    transform->rotation[7] = 0.0f;
    transform->rotation[8] = 1.0f;
    transform->scale = 1.0f;
    transform->position[0] = 0.0f;
    transform->position[1] = 0.0f;
    transform->position[2] = 0.0f;
    transform->flags = 0;
    transform->changes = 0;
    At<void*>(self, field::kEffects) = nullptr;
    At<char*>(self, field::kNameBody) = nullptr;
    At<char*>(self, field::kNameBody + 4) = nullptr;
    At<char*>(self, field::kNameBody + 8) = nullptr;
    char* const storage = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    At<char*>(self, field::kNameBody) = storage;
    At<char*>(self, field::kNameBody + 4) = storage;
    At<char*>(self, field::kNameBody + 8) = storage + 8;
    *storage = 0;
    At<std::uintptr_t>(self, field::kName) = addr::SC4String_Vtable;
    At<uint32_t>(self, field::kNameZero) = 0;
    At<uint32_t>(self, field::kNumber) = 0;
    At<uint8_t>(self, field::kHasAny) = 0;
    At<uint8_t>(self, field::kPickFlag) = 0;
    At<void*>(self, field::kButton) = nullptr;
    At<void*>(self, field::kControl) = nullptr;
    At<void*>(self, field::kSingle) = nullptr;
    At<void*>(self, field::kActive) = nullptr;
    At<void*>(self, field::kInactive) = nullptr;
    return self;
}

// The game's 0x004BB0B0: the destructor (the common disaster tool's
// deleting destructor calls it too). Shutdown when still inited; the five
// effects let go (the fields are left); the name's storage freed; the
// effects manager let go; the base.
void __fastcall Destruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0) = addr::SC4EffectTool_Vtable;
    if (At<uint8_t>(self, base::kInited))
        Shutdown(self, nullptr);
    if (void* const effect = At<void*>(self, field::kInactive))
        Release(effect);
    if (void* const effect = At<void*>(self, field::kActive))
        Release(effect);
    if (void* const effect = At<void*>(self, field::kSingle))
        Release(effect);
    if (void* const effect = At<void*>(self, field::kControl))
        Release(effect);
    if (void* const effect = At<void*>(self, field::kButton))
        Release(effect);
    At<std::uintptr_t>(self, field::kName) = addr::SC4String_Vtable;
    if (char* const storage = At<char*>(self, field::kNameBody))
        Cdecl<void>(addr::SC4_Free, storage);
    if (void* const effects = At<void*>(self, field::kEffects))
        Release(effects);
    This<void>(addr::SC4ViewInputControl_Destruct, self);
}

// The game's 0x004BB140 (slot 26, ret 4): the deleting destructor.
char* __fastcall DeletingDestruct(char* self, void*, uint32_t flags)
{
    Destruct(self, nullptr);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// --- Registration ----------------------------------------------------------------------

bool InstallEffectTool(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[16] = {};
    static const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::SC4EffectTool_Init, reinterpret_cast<void*>(&Init)},
        {addr::SC4EffectTool_Shutdown, reinterpret_cast<void*>(&Shutdown)},
        {addr::SC4EffectTool_OnMouseMove, reinterpret_cast<void*>(&OnMouseMove)},
        {addr::SC4EffectTool_OnMouseUpL, reinterpret_cast<void*>(&OnMouseUpL)},
        {addr::SC4EffectTool_OnMouseExit, reinterpret_cast<void*>(&OnMouseExit)},
        {addr::SC4EffectTool_Switch, reinterpret_cast<void*>(&Switch)},
        {addr::SC4EffectTool_Construct, reinterpret_cast<void*>(&Construct)},
        {addr::SC4EffectTool_Destruct, reinterpret_cast<void*>(&Destruct)},
        {addr::SC4EffectTool_DeletingDestructor, reinterpret_cast<void*>(&DeletingDestruct)},
        {addr::SC4ViewInputControls_F4BB160, reinterpret_cast<void*>(&SetEffectNumber)},
        {addr::SC4EffectTool_Trigger, reinterpret_cast<void*>(&Trigger)},
        {addr::SC4EffectTool_OnMouseDownL, reinterpret_cast<void*>(&OnMouseDownL)},
        {addr::SC4ViewInputControls_F4BB590, reinterpret_cast<void*>(&SetEffectName)},
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
    "ui.tools.disaster_effect",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4EffectTool_Init,
    "Our effect trigger tool, the base of the common disaster tool (docs/tools.md)",
    false,
    InstallEffectTool,
});

} // namespace
} // namespace opensc4::tool_effect
