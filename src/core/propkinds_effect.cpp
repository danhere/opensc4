// Our cSC4PropOccupantVisualEffect (build 641; the class map, the vtables
// and the fields are in propkinds.h): a prop that is an effect of the
// effects manager (smoke, a fountain, a flare). It keeps the effect's name
// or ID, either set by a caller or taken from its exemplar's properties by
// the prop's state, makes the effect when it is initialized or the name,
// ID or state changes, places it with the prop's position and orientation,
// and starts and stops it as the prop is shown and hidden. The prop
// underneath (cSC4PropOccupant) is called at its addresses.
//
// Nothing here draws: the effect and the effects manager (other threads')
// are told through their interfaces.
//
// Every virtual call below has its argument count from the callee's own
// `ret N`:
//   - the object's primary vtable 0x00AA4ED8: 0x04 none, 0x1C StartEffect
//     one (0x005EDA50), 0x20 StopEffect one (0x005EDAA0);
//   - the prop part 0x00AA4E78: 0x24 GetOrientation none (0x005EC9D0);
//   - the occupant part 0x00AA4DE0: 0x14 IsInitialized none (0x00458A40),
//     0x18 AsPropertyHolder none (0x006C2040), 0x20 GetPosition one
//     (0x005ED1F0);
//   - the property holder 0x00AA4D60: 0x0C one (0x005FEC20);
//   - the effect 0x00AA1170: 0x08 none, 0x0C Start one (0x005BE920), 0x10
//     Stop one (0x005BEBA0), 0x1C SetEffectTransform one (0x005BE860);
//   - the effects manager 0x00A9F264: 0x18 three (0x00593AE0), 0x1C two
//     (0x005939B0);
//   - the streams 0x00AA2310 (out) and 0x00AA2280 (in): 0x14, 0x1C, 0x24,
//     0x3C one, 0x48 none.
// Direct callees: 0x005E89B0 (ret), 0x005EC960 (ret 8), 0x005ECE90 and
// 0x005EF360 (ret 4), 0x005ED220 (ret 4), 0x005EE050 (ret), 0x005E8C90 and
// 0x005E8980 (ret), 0x005E8B90 (ret 4), 0x005E8C40 (ret), 0x00408480 (ret
// 4), 0x00408A70 (ret 0xC), 0x009128BF (ret), 0x0090CF9A (ret), 0x0090D031
// and 0x0090CFF9 (ret 4); cdecl: 0x005FD570, 0x005FD480, 0x005FFCC0
// (three), 0x00418A40 (two), 0x0090CF54, 0x0090CF63, 0x005E5620 (one).
//
// Callers from outside the class: the class registration (0x0046079D,
// which calls GetGZCLSID and keeps the factory's address, before any
// replacement is installed), the prop manager's 0x004A256A / 0x004A2578 and
// 0x004A30B7 / 0x004A30C5, and 0x005CBC1E / 0x005CBC2C and 0x005CECC5 /
// 0x005CECD3 (allocator, then constructor: each reads only EAX after
// either call and keeps nothing in ECX or EDX across them). UpdateTransform
// and MakeEffect are called from the class's own functions only. Everything
// else is reached through the vtables and their thunks. The detours are
// plain functions.
#include "core/propkinds.h"

#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::propkinds {
namespace {

constexpr float kHalf = 0.5f;                // 0x00A84D2C
constexpr float kQuarterTurn = 1.5707964f;   // 0x3FC90FDB at 0x00AA44BC
constexpr std::uintptr_t kPool = addr::cSC4PropOccupantVisualEffect_Pool;
constexpr uint32_t kPoolReopenAt = 0x40000052;

// The game takes the cosine and the sine of the double at 0x00A80990, which
// is 0.0, with fcos and fsin, rounds both to floats and uses them as a
// second rotation: 1 and 0.
constexpr float kCosZero = 1.0f;
constexpr float kSinZero = 0.0f;

// The game's string object: 0x14 bytes (vtable, begin, end, capacity, a
// zero), as the frames of 0x005F03B0 and 0x005F0780 leave for it.
struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};
static_assert(sizeof(GameString) == 0x14, "game string size");

// What an effect is told where it is: 0x38 bytes on the stack of
// 0x005EE800. A count of changes, flags (1 the position, 4 the rotation), a
// rotation, the position, the scale. The game leaves the two bytes after
// the flags as the stack was; ours zeroes them.
struct EffectTransform {
    uint8_t changes;
    uint8_t flags;
    uint8_t unused[2];
    float rotation[9];
    uint32_t position[3];
    float scale;
};
static_assert(sizeof(EffectTransform) == 0x38, "effect transform size");

// A vector of floats as 0x005FFCC0 fills it.
struct FloatVector {
    uint32_t* begin;
    uint32_t* end;
    uint32_t* capacity;
};

// fcos and fsin of one angle as the game executes them: the cosine stored
// as a float, the sine both stored as a float straight from the FPU's
// register and as a double (what a multiplication by 1 leaves of it at the
// game's 53-bit precision).
struct SineCosine {
    double sine;
    float sineFloat;
    float cosineFloat;
};

SineCosine X87SineCosine(double angle)
{
    double sine;
    float sineFloat;
    float cosineFloat;
    __asm {
        fld angle
        fld st(0)
        fcos
        fstp cosineFloat
        fsin
        fst sineFloat
        fstp sine
    }
    return {sine, sineFloat, cosineFloat};
}

void ReleaseEffect(char* self)
{
    if (void* const held = At<void*>(self, effect::kEffect)) {
        At<void*>(self, effect::kEffect) = nullptr;
        Virtual<uint32_t>(held, 0x8);
    }
}

// ====================================================================================
// The effect: where it is and which it is (`self` is the object)
// ====================================================================================

// 0x005EE800: nothing without an effect. The effect gets the prop's
// position (the occupant part's slot 8, into three floats that start as the
// game's zero float 0x00B231A0) and a rotation about the vertical by minus
// the orientation (the prop part's slot 9) quarter turns: the angle is the
// integer times the float pi / 2 on the FPU, its cosine is rounded to a
// float before use, its sine is not. The products and sums are in the
// game's order, each rounded to a float where it is stored. The scale is 1.
// No return value.
void __fastcall UpdateTransform(char* self, void*)
{
    static bool logged = false;
    FirstCall("effect UpdateTransform", logged);
    if (!At<void*>(self, effect::kEffect))
        return;
    const uint32_t zero = Global<uint32_t>(addr::cSC4PropOccupant_Zero);
    uint32_t position[3] = {zero, zero, zero};
    Virtual<void*>(self + layout::kOccupant, 0x20, static_cast<uint32_t*>(position));
    EffectTransform transform = {};
    transform.rotation[0] = 1.0f;
    transform.rotation[4] = 1.0f;
    transform.rotation[8] = 1.0f;
    transform.scale = 1.0f;
    const int32_t orientation = Virtual<int32_t>(self + layout::kProp, 0x24);
    const double angle = static_cast<double>(-orientation) * static_cast<double>(kQuarterTurn);
    const SineCosine turn = X87SineCosine(angle);
    const double c = static_cast<double>(kCosZero);
    const double s = static_cast<double>(kSinZero);
    const double cosine = static_cast<double>(turn.cosineFloat);
    const double sineC = turn.sine * c;
    const double sineS = turn.sine * s;
    transform.rotation[0] = static_cast<float>(c * cosine);
    transform.rotation[1] = static_cast<float>(-(s * cosine));
    transform.rotation[2] = turn.sineFloat;
    transform.rotation[3] = static_cast<float>(sineS * c + s * c);
    transform.rotation[4] = static_cast<float>(c * c - sineS * s);
    transform.rotation[5] = static_cast<float>(-(cosine * s));
    transform.rotation[6] = static_cast<float>(s * s - sineC * c);
    transform.position[0] = position[0];
    transform.position[1] = position[1];
    transform.position[2] = position[2];
    transform.flags |= 4;
    transform.flags |= 1;
    transform.changes = static_cast<uint8_t>(transform.changes + 2);
    transform.rotation[7] = static_cast<float>(sineC * s + c * s);
    transform.rotation[8] = static_cast<float>(cosine * c);
    Virtual<void>(At<void*>(self, effect::kEffect), 0x1C, &transform);
}

// The effect asked of the effects manager (0x00B43D1C, not tested for
// null), as 0x005EE9C0 chooses it; whether there is one now. With name
// properties: the name of the prop's state from the exemplar (into the
// name field), no effect for a state past the count, an empty name or the
// name "off". Else with ID properties: the ID of the state (into the ID
// field), none for a state past the count or ID 0. Else the name kept when
// it is not empty, else the ID kept when it is not 0.
bool FetchEffect(char* self)
{
    const uint32_t state = At<uint8_t>(self, layout::kState);
    void** const slot = &At<void*>(self, effect::kEffect);
    const uint32_t names = At<uint32_t>(self, effect::kNameCount);
    if (names > 0) {
        if (state >= names)
            return false;
        void* const holder = Virtual<void*>(self + layout::kOccupant, 0x18);
        Cdecl<uint8_t>(addr::SC4_GetPropertyString, holder, state + effect::kPropertyName, static_cast<void*>(self + effect::kName));
        if (At<char*>(self, effect::kNameBegin) == At<char*>(self, effect::kNameEnd))
            return false;
        if (Cdecl<uint8_t>(addr::SC4String_Equals, static_cast<void*>(self + effect::kNameBegin), "off"))
            return false;
        void* const effects = Global<void*>(addr::SC4_Service43D1C);
        ReleaseEffect(self);
        return Virtual<uint8_t>(effects, 0x1C, At<char*>(self, effect::kNameBegin), slot) != 0;
    }
    const uint32_t ids = At<uint32_t>(self, effect::kIdCount);
    if (ids > 0) {
        if (state >= ids)
            return false;
        void* const holder = Virtual<void*>(self + layout::kOccupant, 0x18);
        Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, state + effect::kPropertyId, static_cast<void*>(self + effect::kEffectId));
        if (!At<uint32_t>(self, effect::kEffectId))
            return false;
        void* const effects = Global<void*>(addr::SC4_Service43D1C);
        ReleaseEffect(self);
        return Virtual<uint8_t>(effects, 0x18, effect::kEffectGroup, At<uint32_t>(self, effect::kEffectId), slot) != 0;
    }
    if (At<char*>(self, effect::kNameBegin) != At<char*>(self, effect::kNameEnd)) {
        void* const effects = Global<void*>(addr::SC4_Service43D1C);
        ReleaseEffect(self);
        return Virtual<uint8_t>(effects, 0x1C, At<char*>(self, effect::kNameBegin), slot) != 0;
    }
    if (!At<uint32_t>(self, effect::kEffectId))
        return false;
    void* const effects = Global<void*>(addr::SC4_Service43D1C);
    ReleaseEffect(self);
    return Virtual<uint8_t>(effects, 0x18, effect::kEffectGroup, At<uint32_t>(self, effect::kEffectId), slot) != 0;
}

// 0x005EE9C0: the effect made again. An effect there is is stopped
// (StopEffect with 1, which clears the started byte) and released. When a
// new one is made it is placed, and started (StartEffect with 1) if the
// started byte was set coming in. When none is made the started byte is
// set, whatever it was. No return value.
void __fastcall MakeEffect(char* self, void*)
{
    static bool logged = false;
    FirstCall("effect MakeEffect", logged);
    const uint8_t started = At<uint8_t>(self, effect::kStarted);
    if (At<void*>(self, effect::kEffect)) {
        Virtual<uint8_t>(self, 0x20, 1u);
        ReleaseEffect(self);
    }
    if (!FetchEffect(self)) {
        At<uint8_t>(self, effect::kStarted) = 1;
        return;
    }
    UpdateTransform(self, nullptr);
    if (started)
        Virtual<uint8_t>(self, 0x1C, 1u);
}

// ====================================================================================
// The effect's own interface (`self` is the object)
// ====================================================================================

// Slot 0 (0x005ECC10): the effect interface 0x09820792 (the reference
// taken with the base's AddRef on the object + 0xC) and the class ID
// (through the vtable) give the object; everything else is the prop's
// (object + 4).
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    static bool logged = false;
    FirstCall("effect QueryInterface", logged);
    if (iid == effect::kIid) {
        *object = self;
        This<uint32_t>(addr::cSC4Occupant_AddRef, self + layout::kOccupant);
        return 1;
    }
    if (iid == effect::kClassId) {
        *object = self;
        Virtual<uint32_t>(self, 0x4);
        return 1;
    }
    return This<uint8_t, uint32_t, void**>(addr::cSC4PropOccupant_QueryInterface, self + layout::kProp, iid, object);
}

// Slot 5 (0x005EF7D0): nothing for the ID the object has. Else the name is
// emptied, the ID kept, both property counts cleared and the effect made
// again. No return value.
void __fastcall SetEffectTypeById(char* self, void*, uint32_t id)
{
    static bool logged = false;
    FirstCall("effect SetEffectTypeById", logged);
    if (id == At<uint32_t>(self, effect::kEffectId))
        return;
    if (At<char*>(self, effect::kNameBegin) != At<char*>(self, effect::kNameEnd)) {
        *At<char*>(self, effect::kNameBegin) = 0;
        At<char*>(self, effect::kNameEnd) = At<char*>(self, effect::kNameBegin);
    }
    At<uint32_t>(self, effect::kEffectId) = id;
    At<uint32_t>(self, effect::kNameCount) = 0;
    At<uint32_t>(self, effect::kIdCount) = 0;
    MakeEffect(self, nullptr);
}

// Slot 6 (0x005F0780): nothing for the name the object has (same length
// and bytes). Else the ID is cleared, the name kept (a string made from the
// text and assigned; the allocator tag the assignment gets is the
// argument's stack slot), both property counts cleared and the effect made
// again. No return value.
void __fastcall SetEffectTypeByName(char* self, void*, const char* name)
{
    static bool logged = false;
    FirstCall("effect SetEffectTypeByName", logged);
    const std::size_t length = std::strlen(name);
    const char* const begin = At<char*>(self, effect::kNameBegin);
    if (static_cast<std::size_t>(At<char*>(self, effect::kNameEnd) - begin) == length && std::memcmp(begin, name, length) == 0)
        return;
    At<uint32_t>(self, effect::kEffectId) = 0;
    GameString made;
    This<void*>(addr::SC4String_FromText, &made, name);
    char* const text = made.begin;
    if (static_cast<void*>(&made.begin) != static_cast<void*>(self + effect::kNameBegin))
        This<void*>(addr::SC4String_AssignRange, self + effect::kNameBegin, text, made.end, static_cast<void*>(&name));
    if (text)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(text));
    At<uint32_t>(self, effect::kNameCount) = 0;
    At<uint32_t>(self, effect::kIdCount) = 0;
    MakeEffect(self, nullptr);
}

// Slot 7 (0x005EDA50): with an effect: started (the effect's Start with 0
// for a true argument, 1 for false) when base flag 2 says the prop is
// shown, the started byte set either way, true. Without one: the started
// byte set, false.
uint8_t __fastcall StartEffect(char* self, void*, uint32_t hard)
{
    static bool logged = false;
    FirstCall("effect StartEffect", logged);
    if (void* const visual = At<void*>(self, effect::kEffect)) {
        if (At<uint8_t>(self, layout::kFlags) & 2)
            Virtual<void>(visual, 0xC, static_cast<uint8_t>(hard) ? 0u : 1u);
        At<uint8_t>(self, effect::kStarted) = 1;
        return 1;
    }
    if (!At<uint8_t>(self, effect::kStarted))
        At<uint8_t>(self, effect::kStarted) = 1;
    return 0;
}

// Slot 8 (0x005EDAA0): false without an effect or when not started. Else
// stopped (the effect's Stop, the same way) when the prop is shown, the
// started byte cleared, true.
uint8_t __fastcall StopEffect(char* self, void*, uint32_t hard)
{
    static bool logged = false;
    FirstCall("effect StopEffect", logged);
    void* const visual = At<void*>(self, effect::kEffect);
    if (!visual || !At<uint8_t>(self, effect::kStarted))
        return 0;
    if (At<uint8_t>(self, layout::kFlags) & 2)
        Virtual<void>(visual, 0x10, static_cast<uint8_t>(hard) ? 0u : 1u);
    At<uint8_t>(self, effect::kStarted) = 0;
    return 1;
}

// ====================================================================================
// The prop part (`prop` is the object + 4)
// ====================================================================================

// Slot 10 (0x005EF280): the prop's SetOrientation (clamped to 0-3), then
// the effect placed again. True.
uint32_t __fastcall SetOrientation(char* prop, void*, int32_t orientation)
{
    static bool logged = false;
    FirstCall("effect SetOrientation", logged);
    char* const self = prop - layout::kProp;
    At<int8_t>(self, layout::kOrientation) = static_cast<int8_t>(orientation < 0 ? 0 : orientation > 3 ? 3 : orientation);
    UpdateTransform(self, nullptr);
    return 1;
}

// Slot 11 (0x005EF2C0): nothing when the state is the same (the argument
// compared whole with the byte). Else the byte is stored and the effect
// made again. Unlike the prop's SetState: no message, and the models are
// not told. No return value.
void __fastcall SetState(char* prop, void*, uint32_t state)
{
    static bool logged = false;
    FirstCall("effect SetState", logged);
    char* const self = prop - layout::kProp;
    if (state == At<uint8_t>(self, layout::kState))
        return;
    At<uint8_t>(self, layout::kState) = static_cast<uint8_t>(state);
    MakeEffect(self, nullptr);
}

// ====================================================================================
// The occupant part (`occupant` is the object + 0xC)
// ====================================================================================

// The properties first to first + 15 counted from the first on, which is
// there, until one is missing (the holder's slot 3).
uint32_t CountProperties(void* holder, uint32_t first)
{
    uint32_t count = 1;
    uint32_t id = first + 1;
    do {
        if (!Virtual<uint8_t>(holder, 0xC, id++))
            break;
        ++count;
    } while (id < first + 0x10);
    return count;
}

// Slot 3 (0x005EF5D0): nothing when initialized. The prop's size from the
// exemplar's property 0x27812810 when that has exactly three floats, else
// 16 m a side (the prop's SetSize, which moves the box back to where it
// was). Then the effect's source: the name property 0xC98204B8 (into the
// name field) and how many of the 15 after it follow without a gap, or
// else the ID property 0x2A6DEF88 (into the ID field) the same way; with
// either the started byte is set. The base's Init. Then the effect is made
// when there is a name or an ID. True.
uint8_t __fastcall Init(char* occupant, void*)
{
    static bool logged = false;
    FirstCall("effect Init", logged);
    char* const self = occupant - layout::kOccupant;
    if (Virtual<uint8_t>(occupant, 0x14))
        return 1;
    void* const holder = Virtual<void*>(occupant, 0x18);
    FloatVector values = {nullptr, nullptr, nullptr};
    uint32_t size[3] = {0x41800000, 0x41800000, 0x41800000};
    if (Cdecl<uint8_t>(addr::SC4_GetPropertyFloatVector, holder, effect::kPropertySize, &values) &&
        ((reinterpret_cast<std::uintptr_t>(values.end) - reinterpret_cast<std::uintptr_t>(values.begin)) & 0xFFFFFFFCu) == 0xC) {
        size[0] = values.begin[0];
        size[1] = values.begin[1];
        size[2] = values.begin[2];
    }
    This<void>(addr::cSC4PropOccupant_SetSize, self + layout::kProp, static_cast<const uint32_t*>(size));
    At<uint32_t>(self, effect::kNameCount) = 0;
    At<uint32_t>(self, effect::kIdCount) = 0;
    if (Cdecl<uint8_t>(addr::SC4_GetPropertyString, holder, effect::kPropertyName, static_cast<void*>(self + effect::kName))) {
        At<uint32_t>(self, effect::kNameCount) = CountProperties(holder, effect::kPropertyName);
        At<uint8_t>(self, effect::kStarted) = 1;
    } else if (Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, effect::kPropertyId,
                              static_cast<void*>(self + effect::kEffectId))) {
        At<uint32_t>(self, effect::kIdCount) = CountProperties(holder, effect::kPropertyId);
        At<uint8_t>(self, effect::kStarted) = 1;
    }
    if (!Virtual<uint8_t>(occupant, 0x14))
        This<uint8_t>(addr::cSC4Occupant_Init, occupant);
    if (At<char*>(self, effect::kNameBegin) != At<char*>(self, effect::kNameEnd) || At<uint32_t>(self, effect::kEffectId))
        MakeEffect(self, nullptr);
    if (values.begin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(values.begin));
    return 1;
}

// Slot 4 (0x005EE790): nothing unless initialized. The effect stopped (its
// Stop with 0) and released, the prop's date range freed, the base's
// Shutdown (the prop's own is not called). The started byte stays. True.
uint8_t __fastcall Shutdown(char* occupant, void*)
{
    static bool logged = false;
    FirstCall("effect Shutdown", logged);
    char* const self = occupant - layout::kOccupant;
    if (!Virtual<uint8_t>(occupant, 0x14))
        return 1;
    if (void* const visual = At<void*>(self, effect::kEffect)) {
        Virtual<void>(visual, 0x10, 0u);
        ReleaseEffect(self);
    }
    if (char* const range = At<char*>(self, layout::kDates)) {
        This<void>(addr::cRZDate_Destroy, range + 0x18);
        This<void>(addr::cRZDate_Destroy, range + 0x8);
        Cdecl<void, void*>(addr::SC4_operator_delete, range);
        At<char*>(self, layout::kDates) = nullptr;
    }
    This<uint8_t>(addr::cSC4Occupant_Shutdown, occupant);
    return 1;
}

// Slot 9 (0x005EF220): the box moved as the prop's SetPosition does it
// (half the width and the height on the FPU, half the depth stored as a
// float first), then the effect placed again. No return value.
void __fastcall SetPosition(char* occupant, void*, const float* position)
{
    static bool logged = false;
    FirstCall("effect SetPosition", logged);
    char* const self = occupant - layout::kOccupant;
    const double halfX =
        (static_cast<double>(At<float>(self, layout::kMax)) - static_cast<double>(At<float>(self, layout::kMin))) *
        static_cast<double>(kHalf);
    const double height =
        static_cast<double>(At<float>(self, layout::kMax + 4)) - static_cast<double>(At<float>(self, layout::kMin + 4));
    const float halfZ = static_cast<float>(
        (static_cast<double>(At<float>(self, layout::kMax + 8)) - static_cast<double>(At<float>(self, layout::kMin + 8))) *
        static_cast<double>(kHalf));
    At<float>(self, layout::kMin) = static_cast<float>(static_cast<double>(position[0]) - halfX);
    At<float>(self, layout::kMax) = static_cast<float>(halfX + static_cast<double>(position[0]));
    uint32_t y = 0;
    std::memcpy(&y, &position[1], 4);
    At<uint32_t>(self, layout::kMin + 4) = y;
    At<float>(self, layout::kMax + 4) = static_cast<float>(height + static_cast<double>(position[1]));
    At<float>(self, layout::kMin + 8) = static_cast<float>(static_cast<double>(position[2]) - static_cast<double>(halfZ));
    At<float>(self, layout::kMax + 8) = static_cast<float>(static_cast<double>(halfZ) + static_cast<double>(position[2]));
    UpdateTransform(self, nullptr);
}

// Slot 30 (0x005ED9D0): when there is an effect, the started byte is set
// and base flag 2 changes, the effect is started (its Start with 0 the
// first time the prop is shown, 1 after; the fresh byte cleared) or
// stopped (its Stop with 1); then the base's SetAllFlags. AL is the flags'
// byte, from the base.
uint32_t __fastcall SetAllFlags(char* occupant, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("effect SetAllFlags", logged);
    char* const self = occupant - layout::kOccupant;
    void* const visual = At<void*>(self, effect::kEffect);
    if (visual && At<uint8_t>(self, effect::kStarted) && ((At<uint8_t>(self, layout::kFlags) ^ flags) & 2)) {
        if (flags & 2) {
            Virtual<void>(visual, 0xC, At<uint8_t>(self, effect::kFresh) ? 0u : 1u);
            At<uint8_t>(self, effect::kFresh) = 0;
        } else {
            Virtual<void>(visual, 0x10, 1u);
        }
    }
    return This<uint32_t>(addr::cSC4Occupant_SetAllFlags, occupant, flags);
}

// ====================================================================================
// Save (`part` is the object + 0x10)
// ====================================================================================

// Slot 3 (0x005F03B0): version 3, the prop's record (its Write, with the
// same `this`), the started and the fresh byte, then the name and the ID:
// as they are when no property gave them, else an empty string (made on
// the stack for it, 8 bytes from the game's allocator) and ID 0. When the
// prop's Write fails nothing more is written; the answer is in both cases
// whether the stream is free of errors.
uint32_t __fastcall Write(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("effect Write", logged);
    char* const self = part - layout::kSerializable;
    Virtual<uint8_t>(stream, 0x1C, 3u);
    if (This<uint8_t>(addr::cSC4PropOccupant_Write, part, stream)) {
        Virtual<uint8_t>(stream, 0x14, uint32_t{At<uint8_t>(self, effect::kStarted)});
        Virtual<uint8_t>(stream, 0x14, uint32_t{At<uint8_t>(self, effect::kFresh)});
        if (!At<uint32_t>(self, effect::kNameCount) && !At<uint32_t>(self, effect::kIdCount)) {
            Virtual<uint8_t>(stream, 0x3C, static_cast<void*>(self + effect::kName));
            Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, effect::kEffectId));
        } else {
            GameString empty;
            empty.begin = nullptr;
            empty.end = nullptr;
            empty.capacity = nullptr;
            char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
            empty.begin = buffer;
            empty.end = buffer;
            empty.capacity = buffer + 8;
            *buffer = 0;
            empty.vtable = addr::SC4String_Vtable;
            empty.zero = 0;
            Virtual<uint8_t>(stream, 0x3C, static_cast<void*>(&empty));
            empty.vtable = addr::SC4String_Vtable;
            if (empty.begin)
                Cdecl<void>(addr::SC4_Free, static_cast<void*>(empty.begin));
            Virtual<uint8_t>(stream, 0x24, 0u);
        }
    }
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// Slot 4 (0x005EF750): false at once for a version other than 3 (read into
// the argument's stack slot, the stream pointer until then). Else the
// prop's Read and, when that works, the two bytes, the name and the ID;
// the answer is whether the stream is free of errors. The property counts
// are not saved: Init counts them again.
uint32_t __fastcall Read(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("effect Read", logged);
    char* const self = part - layout::kSerializable;
    uint32_t version = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(stream));
    Virtual<uint8_t>(stream, 0x1C, &version);
    if (static_cast<uint16_t>(version) != 3)
        return 0;
    if (This<uint8_t>(addr::cSC4PropOccupant_Read, part, stream)) {
        Virtual<uint8_t>(stream, 0x14, self + effect::kStarted);
        Virtual<uint8_t>(stream, 0x14, self + effect::kFresh);
        Virtual<uint8_t>(stream, 0x3C, self + effect::kName);
        Virtual<uint8_t>(stream, 0x24, self + effect::kEffectId);
    }
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// Slot 5 (0x005F02B0).
uint32_t __fastcall GetGZCLSID(void*, void*)
{
    static bool logged = false;
    FirstCall("effect GetGZCLSID", logged);
    return effect::kClassId;
}

// ====================================================================================
// Installation
// ====================================================================================

// GetGZCLSID's code is six bytes (mov eax, the ID; ret): the detour's jump
// covers the first instruction exactly.
const Detour kEffectDetours[] = {
    {addr::cSC4PropOccupantVisualEffect_QueryInterface, reinterpret_cast<void*>(&QueryInterface)},
    {addr::cSC4PropOccupantVisualEffect_SetEffectTypeById, reinterpret_cast<void*>(&SetEffectTypeById)},
    {addr::cSC4PropOccupantVisualEffect_SetEffectTypeByName, reinterpret_cast<void*>(&SetEffectTypeByName)},
    {addr::cSC4PropOccupantVisualEffect_StartEffect, reinterpret_cast<void*>(&StartEffect)},
    {addr::cSC4PropOccupantVisualEffect_StopEffect, reinterpret_cast<void*>(&StopEffect)},
    {addr::cSC4PropOccupantVisualEffect_UpdateTransform, reinterpret_cast<void*>(&UpdateTransform)},
    {addr::cSC4PropOccupantVisualEffect_MakeEffect, reinterpret_cast<void*>(&MakeEffect)},
    {addr::cSC4PropOccupantVisualEffect_SetOrientation, reinterpret_cast<void*>(&SetOrientation)},
    {addr::cSC4PropOccupantVisualEffect_SetState, reinterpret_cast<void*>(&SetState)},
    {addr::cSC4PropOccupantVisualEffect_Init, reinterpret_cast<void*>(&Init)},
    {addr::cSC4PropOccupantVisualEffect_Shutdown, reinterpret_cast<void*>(&Shutdown)},
    {addr::cSC4PropOccupantVisualEffect_SetPosition, reinterpret_cast<void*>(&SetPosition)},
    {addr::cSC4PropOccupantVisualEffect_SetAllFlags, reinterpret_cast<void*>(&SetAllFlags)},
    {addr::cSC4PropOccupantVisualEffect_Write, reinterpret_cast<void*>(&Write)},
    {addr::cSC4PropOccupantVisualEffect_Read, reinterpret_cast<void*>(&Read)},
    {addr::cSC4PropOccupantVisualEffect_GetGZCLSID, reinterpret_cast<void*>(&GetGZCLSID)},
};

bool InstallEffect(registry::Mode) { return InstallDetours(kEffectDetours, "effect"); }

const bool g_registered = registry::Register({
    "simulators.propkinds.effect",
    "simulators/propocc",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PropOccupantVisualEffect_QueryInterface,
    "Our cSC4PropOccupantVisualEffect: QueryInterface, the two SetEffectType, StartEffect, StopEffect, the effect made "
    "and placed, SetOrientation, SetState, Init, Shutdown, SetPosition, SetAllFlags, Write, Read and GetGZCLSID (16 "
    "detours; src/core/propkinds.h)",
    false,
    InstallEffect,
});

// ====================================================================================
// Our object
// ====================================================================================

// Copies of the game's seven vtables, taken at the first construction,
// after every PostAppInit slot swap of the other groups (the prop's
// GetRandomChance in 0x00AA4E78).
constexpr int kSlots0 = 9, kSlots4 = 24, kSlots8 = 4, kSlotsC = 34, kSlots10 = 6, kSlots14 = 25, kSlots28 = 5;
void* g_vtable[kSlots0];
void* g_vtable4[kSlots4];
void* g_vtable8[kSlots8];
void* g_vtableC[kSlotsC];
void* g_vtable10[kSlots10];
void* g_vtable14[kSlots14];
void* g_vtable28[kSlots28];

void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags);

// The holder part's slot 22 (0x005F0310).
void* __fastcall DeletingDestructorAtHolder(char* part, void*, uint32_t flags)
{
    return DeletingDestructor(part - layout::kHolder, nullptr, flags);
}

void BuildVtables()
{
    static bool built = false;
    if (built)
        return;
    built = true;
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4PropOccupantVisualEffect_vftable), sizeof(g_vtable));
    std::memcpy(g_vtable4, reinterpret_cast<const void*>(addr::cSC4PropOccupant_DerivedVftable_AA4E78), sizeof(g_vtable4));
    std::memcpy(g_vtable8, reinterpret_cast<const void*>(addr::cSC4PropOccupantVisualEffect_vftable_8), sizeof(g_vtable8));
    std::memcpy(g_vtableC, reinterpret_cast<const void*>(addr::cSC4PropOccupantVisualEffect_vftable_C), sizeof(g_vtableC));
    std::memcpy(g_vtable10, reinterpret_cast<const void*>(addr::cSC4PropOccupantVisualEffect_vftable_10), sizeof(g_vtable10));
    std::memcpy(g_vtable14, reinterpret_cast<const void*>(addr::cSC4PropOccupantVisualEffect_vftable_14), sizeof(g_vtable14));
    std::memcpy(g_vtable28, reinterpret_cast<const void*>(addr::cSC4PropOccupantVisualEffect_vftable_28), sizeof(g_vtable28));
    g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
    g_vtable10[5] = reinterpret_cast<void*>(&GetGZCLSID);
    g_vtable14[22] = reinterpret_cast<void*>(&DeletingDestructorAtHolder);
    log::Write("propkinds effect object: vtables copied (+0 slots 5/7 %p %p, +4 slots 10/11 %p %p, +C slots 3/4/9/30 %p %p %p %p)",
               g_vtable[5], g_vtable[7], g_vtable4[10], g_vtable4[11], g_vtableC[3], g_vtableC[4], g_vtableC[9],
               g_vtableC[30]);
}

void SetVtables(char* self)
{
    BuildVtables();
    At<void**>(self, 0x0) = g_vtable;
    At<void**>(self, layout::kProp) = g_vtable4;
    At<void**>(self, layout::kMessageTarget) = g_vtable8;
    At<void**>(self, layout::kOccupant) = g_vtableC;
    At<void**>(self, layout::kSerializable) = g_vtable10;
    At<void**>(self, layout::kHolder) = g_vtable14;
    At<void**>(self, layout::kExemplarPart) = g_vtable28;
}

// The constructor (detour of 0x005F0210): the prop constructed at its
// address on the object + 4, our vtables, no effect, an empty name (8
// bytes from the game's allocator, not tested for null), no ID, no
// property counts, not started, fresh. +0x92 and +0x93 are left as they
// are. (The game stores an abstract vtable, 0x00AA47AC, at +0 before the
// prop's constructor; nothing reads it.)
void* __fastcall Construct(char* self, void*)
{
    static bool logged = false;
    FirstCall("effect Construct", logged);
    This<void*>(addr::cSC4PropOccupant_cSC4PropOccupant, self + layout::kProp);
    SetVtables(self);
    At<void*>(self, effect::kEffect) = nullptr;
    At<char*>(self, effect::kNameBegin) = nullptr;
    At<char*>(self, effect::kNameEnd) = nullptr;
    At<char*>(self, effect::kNameCapacity) = nullptr;
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    At<char*>(self, effect::kNameBegin) = buffer;
    At<char*>(self, effect::kNameEnd) = buffer;
    At<char*>(self, effect::kNameCapacity) = buffer + 8;
    *buffer = 0;
    At<uint32_t>(self, effect::kNameZero) = 0;
    At<std::uintptr_t>(self, effect::kName) = addr::SC4String_Vtable;
    At<uint32_t>(self, effect::kEffectId) = 0;
    At<uint32_t>(self, effect::kNameCount) = 0;
    At<uint32_t>(self, effect::kIdCount) = 0;
    At<uint8_t>(self, effect::kStarted) = 0;
    At<uint8_t>(self, effect::kFresh) = 1;
    static int created = 0;
    if (created++ < 3)
        log::Write("propkinds effect object: constructed %p", static_cast<void*>(self));
    return self;
}

// The destructor's body (detour of 0x005F0340; the deleting destructor is
// its one caller): the class's vtables, the name's buffer freed, the
// effect released (it is not stopped here: Shutdown does that), then the
// occupant base destroyed.
void __fastcall Destroy(char* self, void*)
{
    static bool logged = false;
    FirstCall("effect Destroy", logged);
    SetVtables(self);
    At<std::uintptr_t>(self, effect::kName) = addr::SC4String_Vtable;
    if (char* const text = At<char*>(self, effect::kNameBegin))
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(text));
    if (void* const visual = At<void*>(self, effect::kEffect))
        Virtual<uint32_t>(visual, 0x8);
    This<void>(addr::cSC4Occupant_Destroy, self + layout::kOccupant);
}

// The deleting destructor (detour of 0x005F05A0): the body, and with flag 1
// the memory back to the pool. A null object is still destroyed first, as
// in the game.
void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("effect DeletingDestructor", logged);
    Destroy(self, nullptr);
    if ((flags & 1) && self)
        PoolFree(kPool, self, kPoolReopenAt);
    return self;
}

// The pool allocator (detour of 0x005ED9A0; the size is not used).
void* __cdecl Allocate(std::size_t)
{
    static bool logged = false;
    FirstCall("effect Allocate", logged);
    return PoolAllocate(kPool);
}

// The factory (detour of 0x005F0560): an item of the pool, constructed; the
// prop interface (object + 4) is what it hands out.
void* __cdecl Create()
{
    static bool logged = false;
    FirstCall("effect Create", logged);
    char* const self = static_cast<char*>(Construct(static_cast<char*>(PoolAllocate(kPool)), nullptr));
    return self ? self + layout::kProp : nullptr;
}

const Detour kEffectObjectDetours[] = {
    {addr::cSC4PropOccupantVisualEffect_Construct, reinterpret_cast<void*>(&Construct)},
    {addr::cSC4PropOccupantVisualEffect_Allocate, reinterpret_cast<void*>(&Allocate)},
    {addr::cSC4PropOccupantVisualEffect_Create, reinterpret_cast<void*>(&Create)},
    {addr::cSC4PropOccupantVisualEffect_DeletingDestructor, reinterpret_cast<void*>(&DeletingDestructor)},
    {addr::cSC4PropOccupantVisualEffect_Destroy, reinterpret_cast<void*>(&Destroy)},
};

// Every construction path: the factory (through the class registration's
// pointer when a save is read), the prop manager's and the two other
// allocator and constructor call pairs. The deleting destructor is reached
// through the holder part's slot 22: ours in the copies, and by the detour
// for the game's thunk.
bool InstallEffectObject(registry::Mode) { return InstallDetours(kEffectObjectDetours, "effect object"); }

const bool g_registeredObject = registry::Register({
    "simulators.propkinds.effect_object",
    "simulators/propocc",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PropOccupantVisualEffect_Construct,
    "Our cSC4PropOccupantVisualEffect object: the constructor, the pool allocation, the factory, the destructor and "
    "the deleting destructor (5 detours), identity; methods from copies of the game vtables taken once "
    "(src/core/propkinds.h)",
    false,
    InstallEffectObject,
});

} // namespace
} // namespace opensc4::propkinds
