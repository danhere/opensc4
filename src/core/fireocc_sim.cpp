// Our cSC4FireOccupant (build 641; the class map, the vtables slot by slot
// and the fields are in fireocc_sim.h): the class's own slots in its five
// vtables, the end time's function by detour, the save record's Write and
// Read with the flames', and our own object (the constructor and the
// factory by detour, the destructor and identity from copies of the
// vtables).
//
// The occupant base (cSC4Occupant: its constructor, destructor,
// QueryInterface, AddRef, Release, Init, Shutdown, Write, Read, GetPosition,
// GetBoundingBox) is every occupant class's (src/core/occbase_sim.cpp): it
// is called at its address. The class's getters and setters of one
// instruction (the target, the stage, the stage count, the heat, the hosed
// count, the family) are code the linker folded with other classes'
// vtables; they stay the game's.
//
// Another thread owns all drawing: the class's effect and model code stays
// the game's (the list is in fireocc_sim.h). Of it this file calls, at
// their addresses, the placing of the flames (0x00472380, from SetStage)
// and a flame's effect for a stage (0x004710D0, from Init and SetStage).
// Shutdown stops and releases the flames' effects itself, through the
// effect's own slots, as the game's does: that is the occupant's end, not
// drawing.
//
// Every virtual call below has its argument count from the callee's own
// `ret N`:
//   - the streams' vtables 0x00AA2310 (out) and 0x00AA2280 (in): slots
//     0x1C and 0x24 one argument, 0x48 none (as src/core/foundocc_sim.cpp
//     found them);
//   - an occupant's part (this object's vtable 0x00A883D8; the target's
//     has the same interface): 0x4 AddRef and 0x8 Release none, 0x14
//     IsInitialized none (0x00458A40), 0x18 AsPropertyHolder none
//     (0x006C2040), 0x1C GetType none (0x006BDD80), 0x20 GetPosition one
//     (0x005E8A20), 0x28 GetBoundingBox two (0x005E8A40), 0x30
//     SetRemovalFlags one (0x005E8A60), 0x34 UnsetRemovalFlags one
//     (0x005E8AA0), 0x5C AddOccupantGroup one (0x005E9350);
//   - the automata controller manager (the city's slot 47; vtable
//     0x00A96058): 0x28 AddControllersFromOccupantGroup three (0x00517150),
//     0x38 RemoveControllers one (0x0051A520);
//   - a visual effect (vtable 0x00AA1170): 0x8 Release none (0x005BE840),
//     0x10 one (0x005BEBA0), 0x14 none (0x007D6680);
//   - the city's slot 0xBC (a getter of +0x1E0): none.
#include "core/fireocc_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::fireocc_sim {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("fireocc: first %s", name);
}

namespace {

constexpr uint32_t kBuildingType = 0x278128A0;
constexpr uint32_t kPropertyStages = 0x49BEDA31;
constexpr uint32_t kPropertyBurning = 0x69DBEBE1;
constexpr uint32_t kIidOccupant = 0x26BB767E; // what Read asks the target's reference for
constexpr uint32_t kFireGroup = 0x5005;       // the occupant group every fire is in

template <typename R, typename... Args>
R CallThis(std::uintptr_t address, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(address)(self, args...);
}

template <typename R, typename... Args>
R CallCdecl(std::uintptr_t address, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(address)(args...);
}

// The game's string object (0x14 bytes), as 0x00408480 makes it.
struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};

// The game's fast rounding of a value on the FPU: 12582912.0 (the float at
// 0x00A882AC, 1.5 * 2^23) added, stored as a float, and 0xB4C00000 added to
// the float's bits, which leaves the nearest integer (halves to even).
uint32_t MagicRound(double value)
{
    const float rounded = static_cast<float>(value + 12582912.0);
    uint32_t bits;
    std::memcpy(&bits, &rounded, sizeof bits);
    return bits + 0xB4C00000u;
}

// SC4Curve::Evaluate (0x005F4250) leaves its result on the FPU without
// rounding it to a float.
double Evaluate(void* curve, float x)
{
    return reinterpret_cast<double(__thiscall*)(void*, float)>(addr::SC4Curve_Evaluate)(curve, x);
}

char**& FlamesBegin(char* self) { return At<char**>(self, field::kFlames); }
char**& FlamesEnd(char* self) { return At<char**>(self, field::kFlames + 4); }

// --- The primary interface (`self` is the object) ---------------------------------

// Slot 0 (0x0046FE30): the fire interface here (the object, counted through
// its slot 1), everything else from the occupant base (a jump, with the
// object + 4). The class ID is not answered here.
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    static bool logged = false;
    FirstCall("QueryInterface", logged);
    if (iid == kIidFire) {
        *object = self;
        Virtual<uint32_t>(self, 0x4);
        return 1;
    }
    return CallThis<uint8_t, uint32_t, void**>(addr::cSC4Occupant_QueryInterface, self + field::kOccupant, iid, object);
}

// The other parts' slot 0 (0x00471230, 0x00471240, 0x00471250, 0x00471270).
template <std::size_t Offset>
uint8_t __fastcall QueryInterfaceAt(char* part, void*, uint32_t iid, void** object)
{
    return QueryInterface(part - Offset, nullptr, iid, object);
}

// Slot 4 (0x00471900): the target taken (the new one referenced before the
// old one is released; nothing when it is the same), then, whether it
// changed or not:
//   - the stage count from the target's property 0x49BEDA31, 2 without it;
//   - this fire's box (its own slot, so the target's box), the differences
//     high less low as floats (dx, dy, dz; dz also kept on the FPU);
//   - the first flame number: the first curve of 0x00B21638 at dz (as on
//     the FPU) times dx, stored as a float, rounded as MagicRound;
//   - the second: the second curve at (dz * dy + dy * dx) * 2 (floats, the
//     products and the sum on the FPU), stored as a float;
//   - the target's property 0x69DBEBE1 (burning) set, its removal flag 2
//     set;
//   - for a building (type 0x278128A0) the city's automata controller
//     manager is asked for the "fire_occupant" controllers around the
//     target (slot 0x28: name, occupant, 0). The game does not test the
//     manager for null, nor the target: neither do we.
// AL is 1.
uint8_t __fastcall SetTarget(char* self, void*, void* target)
{
    static bool logged = false;
    FirstCall("SetTarget", logged);
    void* const old = At<void*>(self, field::kTarget);
    if (target != old) {
        if (target)
            Virtual<uint32_t>(target, 0x4);
        At<void*>(self, field::kTarget) = target;
        if (old)
            Virtual<uint32_t>(old, 0x8);
    }
    if (!CallCdecl<uint8_t, void*, uint32_t, uint32_t*>(addr::SC4_GetPropertyUint32,
                                                         Virtual<void*>(target, occupant::kAsPropertyHolder),
                                                         kPropertyStages, &At<uint32_t>(self, field::kStageCount)))
        At<uint32_t>(self, field::kStageCount) = 2;

    // The game's two buffers are not cleared before the call; every
    // GetBoundingBox writes all six floats.
    float low[3] = {}, high[3] = {};
    Virtual<void>(self + field::kOccupant, occupant::kGetBoundingBox, static_cast<float*>(low),
                  static_cast<float*>(high));
    const float dx = static_cast<float>(static_cast<double>(high[0]) - static_cast<double>(low[0]));
    const float dy = static_cast<float>(static_cast<double>(high[1]) - static_cast<double>(low[1]));
    const double dzWide = static_cast<double>(high[2]) - static_cast<double>(low[2]);
    const float dz = static_cast<float>(dzWide);

    void* const* const curves = reinterpret_cast<void* const*>(addr::SC4_FireSpreadCurves);
    const float floorArea = static_cast<float>(dzWide * static_cast<double>(dx));
    At<uint32_t>(self, field::kFlamesLow) = MagicRound(Evaluate(curves[0], floorArea));
    const float wallArea = static_cast<float>(
        (static_cast<double>(dz) * static_cast<double>(dy) + static_cast<double>(dy) * static_cast<double>(dx)) * 2.0);
    At<uint32_t>(self, field::kFlamesHigh) = MagicRound(Evaluate(curves[1], wallArea));

    void* burning = At<void*>(self, field::kTarget);
    CallCdecl<uint8_t, void*, uint32_t, uint32_t>(addr::SC4_SetPropertyBool,
                                                   Virtual<void*>(burning, occupant::kAsPropertyHolder),
                                                   kPropertyBurning, 1u);
    Virtual<void>(At<void*>(self, field::kTarget), occupant::kSetRemovalFlags, 2u);
    if (Virtual<uint32_t>(At<void*>(self, field::kTarget), occupant::kGetType) == kBuildingType) {
        void* const controllers = Virtual<void*>(Global<void*>(addr::SC4_City), 0xBC);
        GameString name;
        CallThis<void*, const char*>(addr::SC4String_FromText, &name,
                                     reinterpret_cast<const char*>(addr::cSC4FireOccupant_ControllerName));
        Virtual<uint32_t>(controllers, 0x28, &name, At<void*>(self, field::kTarget), 0u);
        name.vtable = addr::SC4String_Vtable;
        if (name.begin)
            CallCdecl<void, void*>(addr::SC4_Free, name.begin);
    }
    return 1;
}

// Slot 8 (0x004728B0): nothing when the stage is the same. Otherwise the
// stage is stored, a fire without flames gets them placed (the game's
// 0x00472380: drawing), and every flame gets the stage's effect (the
// game's 0x004710D0: drawing). The end of the list is read again after
// each flame.
void __fastcall SetStage(char* self, void*, int32_t stage)
{
    static bool logged = false;
    FirstCall("SetStage", logged);
    if (At<int32_t>(self, field::kStage) == stage)
        return;
    At<int32_t>(self, field::kStage) = stage;
    if (FlamesBegin(self) == FlamesEnd(self))
        CallThis<void>(addr::cSC4FireOccupant_PlaceFlames, self);
    for (char** item = FlamesBegin(self); item != FlamesEnd(self); ++item)
        CallThis<void, int32_t>(addr::cSC4FireOccupant_StartFlame, *item, stage);
}

// Slot 12 (0x0046FDD0): the argument added to the hosed count; a negative
// one takes off at most what there is (the count compared unsigned with the
// argument negated). EAX is the new count, but the argument when it was not
// negative.
uint32_t __fastcall SetHosed(char* self, void*, int32_t change)
{
    static bool logged = false;
    FirstCall("SetHosed", logged);
    uint32_t& hosed = At<uint32_t>(self, field::kHosed);
    if (change < 0) {
        if (hosed > static_cast<uint32_t>(-change))
            hosed += static_cast<uint32_t>(change);
        else
            hosed = 0;
        return hosed;
    }
    hosed += static_cast<uint32_t>(change);
    return static_cast<uint32_t>(change);
}

// 0x0046FE00 (not a slot; the fire instance calls it at 0x00579280 and
// 0x0057BBF0, and src/core/disaster_fire.cpp at its address): without an
// end time the argument becomes it; with one, an argument past it (unsigned)
// clears it. The end time after is returned: 0 says the fire is out.
uint32_t __fastcall CheckBurnOut(char* self, void*, uint32_t time)
{
    static bool logged = false;
    FirstCall("CheckBurnOut", logged);
    uint32_t& end = At<uint32_t>(self, field::kEndTime);
    if (end == 0) {
        end = time;
        return time;
    }
    if (time > end)
        end = 0;
    return end;
}

// --- The occupant part (`part` is the object + 4) ---------------------------------

// Slot 3 (0x00471170): once only (the part's own slot 5 asked). The base's
// Init, the fire group 0x5005 (the part's slot 23), then every flame the
// fire has already (a fire read from a save) marked started and given the
// current stage's effect (the game's 0x004710D0: drawing). AL is 1.
uint8_t __fastcall Init(char* part, void*)
{
    static bool logged = false;
    FirstCall("Init", logged);
    if (Virtual<uint8_t>(part, occupant::kIsInitialized))
        return 1;
    char* const self = part - field::kOccupant;
    CallThis<uint8_t>(addr::cSC4Occupant_Init, part);
    Virtual<void>(part, occupant::kAddOccupantGroup, kFireGroup);
    for (char** item = FlamesBegin(self); item != FlamesEnd(self); ++item) {
        char* const one = *item;
        if (!At<uint8_t>(one, flame::kStarted))
            At<uint8_t>(one, flame::kStarted) = 1;
        CallThis<void, int32_t>(addr::cSC4FireOccupant_StartFlame, one, At<int32_t>(self, field::kStage));
    }
    return 1;
}

// Slot 4 (0x00470DC0): nothing unless initialized. Every flame, from the
// first: a started one has its effect stopped if the effect says it runs
// (slot 0x14, then slot 0x10 with 0) and released, and is marked not
// started; an effect still held is released; the flame is deleted and its
// pointer taken out of the list (the rest moved down). Then the target's
// burning property is cleared, its removal flag 2 taken off, the automata
// controller manager told to remove the target's controllers (when the
// city has the manager), the target released and the base shut down. The
// game does not test the target for null: neither do we. AL is 1.
uint8_t __fastcall Shutdown(char* part, void*)
{
    static bool logged = false;
    FirstCall("Shutdown", logged);
    if (!Virtual<uint8_t>(part, occupant::kIsInitialized))
        return 1;
    char* const self = part - field::kOccupant;
    while (FlamesBegin(self) != FlamesEnd(self)) {
        char** const first = FlamesBegin(self);
        char* const one = *first;
        if (At<uint8_t>(one, flame::kStarted)) {
            if (void* effect = At<void*>(one, flame::kEffect)) {
                if (Virtual<uint8_t>(effect, 0x14))
                    Virtual<void>(At<void*>(one, flame::kEffect), 0x10, 0u);
                effect = At<void*>(one, flame::kEffect);
                if (effect) {
                    At<void*>(one, flame::kEffect) = nullptr;
                    Virtual<uint32_t>(effect, 0x8);
                }
            }
            At<uint8_t>(one, flame::kStarted) = 0;
        }
        if (void* const effect = At<void*>(one, flame::kEffect))
            Virtual<uint32_t>(effect, 0x8);
        CallCdecl<void, void*>(addr::SC4_operator_delete, one);
        char** const end = FlamesEnd(self);
        char** const next = first + 1;
        if (end != next)
            CallCdecl<void*, void*, void*, std::size_t>(addr::SC4_memmove, first, next,
                                                        static_cast<std::size_t>(reinterpret_cast<char*>(end) -
                                                                                 reinterpret_cast<char*>(next)));
        FlamesEnd(self) -= 1;
    }
    CallCdecl<uint8_t, void*, uint32_t, uint32_t>(
        addr::SC4_SetPropertyBool, Virtual<void*>(At<void*>(self, field::kTarget), occupant::kAsPropertyHolder),
        kPropertyBurning, 0u);
    Virtual<void>(At<void*>(self, field::kTarget), occupant::kUnsetRemovalFlags, 2u);
    if (void* const controllers = Virtual<void*>(Global<void*>(addr::SC4_City), 0xBC))
        Virtual<void>(controllers, 0x38, At<void*>(self, field::kTarget));
    if (void* const target = At<void*>(self, field::kTarget)) {
        At<void*>(self, field::kTarget) = nullptr;
        Virtual<uint32_t>(target, 0x8);
    }
    CallThis<uint8_t>(addr::cSC4Occupant_Shutdown, part);
    return 1;
}

// Slot 8 (0x00470210): the target's position (a jump to its slot 8);
// without a target the base's (zeros).
uint32_t __fastcall GetPosition(char* part, void*, float* position)
{
    static bool logged = false;
    FirstCall("GetPosition", logged);
    if (void* const target = At<void*>(part, field::kTarget - field::kOccupant))
        return Virtual<uint32_t>(target, occupant::kGetPosition, position);
    return CallThis<uint32_t, float*>(addr::cSC4Occupant_GetPosition, part, position);
}

// Slot 10 (0x00470230): the target's box (a jump to its slot 10); without
// a target the base's (zeros).
uint32_t __fastcall GetBoundingBox(char* part, void*, float* low, float* high)
{
    static bool logged = false;
    FirstCall("GetBoundingBox", logged);
    if (void* const target = At<void*>(part, field::kTarget - field::kOccupant))
        return Virtual<uint32_t>(target, occupant::kGetBoundingBox, low, high);
    return CallThis<uint32_t, float*, float*>(addr::cSC4Occupant_GetBoundingBox, part, low, high);
}

// --- The other parts ----------------------------------------------------------------

// +0x8 slot 5 (0x0046FDC0).
uint32_t __fastcall GetGZCLSID(void*, void*)
{
    static bool logged = false;
    FirstCall("GetGZCLSID", logged);
    return kClassId;
}

// --- Save ---------------------------------------------------------------------------

// A flame's constructor (0x00470BC0; the game's placing calls its own):
// shown, not started, kind 0, no turn, the spot three copies of the float
// at 0x00B21640 (read as the game does, not assumed zero), the transform
// identity (no flags, unit matrix, no translation, scale 1), no effect.
// +0x3, +0x16 and +0x17 are left as the heap's memory holds them.
char* ConstructFlame(char* one)
{
    At<uint8_t>(one, flame::kShown) = 1;
    At<uint8_t>(one, flame::kStarted) = 0;
    At<uint8_t>(one, flame::kKind) = 0;
    At<uint32_t>(one, flame::kTurn) = 0;
    for (std::size_t offset = flame::kSpot; offset < flame::kTransform; offset += 4)
        At<uint32_t>(one, offset) = Global<uint32_t>(addr::cSC4FireOccupant_ZeroFloat);
    At<uint8_t>(one, flame::kTransform) = 0;
    At<uint8_t>(one, flame::kTransformFlags) = 0;
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 3; ++column)
            At<float>(one, flame::kMatrix + static_cast<std::size_t>(12 * row + 4 * column)) = row == column ? 1.0f : 0.0f;
    for (std::size_t offset = flame::kTranslation; offset < flame::kScale; offset += 4)
        At<uint32_t>(one, offset) = 0;
    At<float>(one, flame::kScale) = 1.0f;
    At<void*>(one, flame::kEffect) = nullptr;
    return one;
}

// A flame's Read (0x004700A0): versions 2 and on. The game reads the
// version into its argument's stack slot (the stream pointer until the
// stream writes two bytes of it), so it starts with that value here. An
// older version: AL cleared, the rest of EAX as the stream's read left it.
// Then the kind (a bool), the turn, the spot, and the transform: nine
// floats of a matrix in version 2, a transform record from 3 on. The result
// is whether the stream is free of error after.
uint32_t ReadFlame(char* one, void* stream)
{
    uint32_t version = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(stream));
    const uint32_t read = Virtual<uint32_t>(stream, 0x1C, &version);
    if (static_cast<uint16_t>(version) < 2)
        return read & 0xFFFFFF00u;
    CallCdecl<void*, void*, void*>(addr::SC4Stream_ReadBool, stream, one + flame::kKind);
    Virtual<uint8_t>(stream, 0x24, one + flame::kTurn);
    CallCdecl<void*, void*, void*>(addr::SC4Stream_ReadVector3, stream, one + flame::kSpot);
    if (static_cast<uint16_t>(version) < 3)
        CallCdecl<void*, void*, void*>(addr::SC4Stream_ReadMatrixAsTransform, stream, one + flame::kTransform);
    else
        CallCdecl<void*, void*, void*>(addr::SC4Stream_ReadTransform, stream, one + flame::kTransform);
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// +0x8 slot 3 (0x00470130; `part` = object + 8): version 3, the occupant
// base's record, then the heat, the stage, the stage count, the target as
// a reference, the number of flames and each flame: version 3, the kind (a
// bool), the turn, the spot, the transform. The layout is in tools/re/
// save_schemas/fireocc.py. After each flame the stream is asked for an
// error: with one the result is AL cleared (the rest of EAX the stream's
// answer). When the base fails the class's own fields are left out.
// Otherwise the result is whether the stream is free of error after. The
// list's end is read again after each flame.
uint32_t __fastcall Write(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    char* const self = part - field::kSerializable;
    Virtual<uint8_t>(stream, 0x1C, 3u);
    if (CallThis<uint8_t, void*>(addr::cSC4Occupant_Write, part, stream)) {
        Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, field::kHeat));
        Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, field::kStage));
        Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, field::kStageCount));
        CallCdecl<uint8_t, void*, void*>(addr::SC4Stream_WriteReference, stream, At<void*>(self, field::kTarget));
        Virtual<uint8_t>(stream, 0x24,
                         static_cast<uint32_t>(reinterpret_cast<char*>(FlamesEnd(self)) -
                                               reinterpret_cast<char*>(FlamesBegin(self))) >>
                             2);
        for (char** item = FlamesBegin(self); item != FlamesEnd(self);) {
            char* const one = *item++;
            Virtual<uint8_t>(stream, 0x1C, 3u);
            CallCdecl<void*, void*, uint32_t>(addr::SC4Stream_WriteBool, stream, uint32_t{At<uint8_t>(one, flame::kKind)});
            Virtual<uint8_t>(stream, 0x24, At<uint32_t>(one, flame::kTurn));
            CallCdecl<void*, void*, void*>(addr::SC4Stream_WriteVector3, stream, one + flame::kSpot);
            CallCdecl<void*, void*, void*>(addr::SC4Stream_WriteTransform, stream, one + flame::kTransform);
            const uint32_t error = Virtual<uint32_t>(stream, 0x48);
            if (static_cast<uint8_t>(error))
                return error & 0xFFFFFF00u;
        }
    }
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// +0x8 slot 4 (0x00471A70; `part` = object + 8): version 3 only (the
// version read into the argument's stack slot, as in ReadFlame; another
// version: AL cleared). The base's record; when it fails the rest is
// skipped. The heat, the stage, the stage count; the target held is
// released and the saved reference read as a cISC4Occupant (0x26BB767E);
// the number of flames, then each: 0x50 bytes of operator new,
// constructed, read (a flame that fails to read is left as it is, not
// listed and not deleted, and the result is AL cleared), appended to the
// list. The result is whether the stream is free of error after.
//
// The game's count is a stack slot it does not set before the stream
// writes it: zero here should the stream not write. The flames the fire
// has already are kept (a fire read from a save has none). The two flame
// numbers, the hosed count, the end time, the on-model byte and the family
// are not in the record: they stay as they were (the constructor's zeros,
// the family the heap's memory).
uint32_t __fastcall Read(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Read", logged);
    char* const self = part - field::kSerializable;
    uint32_t version = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(stream));
    const uint32_t read = Virtual<uint32_t>(stream, 0x1C, &version);
    if (static_cast<uint16_t>(version) != 3)
        return read & 0xFFFFFF00u;
    if (CallThis<uint8_t, void*>(addr::cSC4Occupant_Read, part, stream)) {
        Virtual<uint8_t>(stream, 0x24, self + field::kHeat);
        Virtual<uint8_t>(stream, 0x24, self + field::kStage);
        Virtual<uint8_t>(stream, 0x24, self + field::kStageCount);
        if (void* const old = At<void*>(self, field::kTarget)) {
            At<void*>(self, field::kTarget) = nullptr;
            Virtual<uint32_t>(old, 0x8);
        }
        CallCdecl<uint8_t, void*, uint32_t, void*, uint32_t>(addr::SC4Stream_ReadReference, stream, kIidOccupant,
                                                             self + field::kTarget, 0u);
        uint32_t count = 0;
        Virtual<uint8_t>(stream, 0x24, &count);
        for (uint32_t index = 0; index < count; ++index) {
            char* one = CallCdecl<char*, std::size_t>(addr::SC4_operator_new, kFlameSize);
            if (one)
                ConstructFlame(one);
            const uint32_t ok = ReadFlame(one, stream);
            if (!static_cast<uint8_t>(ok))
                return ok & 0xFFFFFF00u;
            char**& end = FlamesEnd(self);
            if (end != At<char**>(self, field::kFlames + 8)) {
                if (end)
                    *end = one;
                ++end;
            } else {
                // The game passes the address of a stack slot nothing reads
                // as the vector's allocator argument.
                uint32_t unused = 0;
                using InsertFn = void(__thiscall*)(void*, char**, char**, void*, uint32_t, uint32_t);
                reinterpret_cast<InsertFn>(addr::SC4PtrVector_InsertN)(self + field::kFlames, end, &one, &unused, 1, 1);
            }
        }
    }
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// The holder part's slots 23 and 24 (0x00471280, 0x00471B90): this - 4.
uint32_t __fastcall WriteThunk(char* holder, void*, void* stream) { return Write(holder - 0x4, nullptr, stream); }
uint32_t __fastcall ReadThunk(char* holder, void*, void* stream) { return Read(holder - 0x4, nullptr, stream); }

// --- Installation -------------------------------------------------------------------

struct Slot {
    std::uintptr_t vtable;
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(vtable, index, original, fn) {vtable, index, original, reinterpret_cast<void*>(&fn)}

const Slot kSlots[] = {
    OPENSC4_SLOT(addr::cSC4FireOccupant_vftable, 0, addr::cSC4FireOccupant_QueryInterface, QueryInterface),
    OPENSC4_SLOT(addr::cSC4FireOccupant_vftable, 4, addr::cSC4FireOccupant_SetTarget, SetTarget),
    OPENSC4_SLOT(addr::cSC4FireOccupant_vftable, 8, addr::cSC4FireOccupant_SetStage, SetStage),
    OPENSC4_SLOT(addr::cSC4FireOccupant_vftable, 12, addr::cSC4FireOccupant_SetHosed, SetHosed),
    OPENSC4_SLOT(addr::cSC4FireOccupant_vftable_4, 0, addr::cSC4FireOccupant_QueryInterfaceThunk4,
                 QueryInterfaceAt<0x4>),
    OPENSC4_SLOT(addr::cSC4FireOccupant_vftable_4, 3, addr::cSC4FireOccupant_Init, Init),
    OPENSC4_SLOT(addr::cSC4FireOccupant_vftable_4, 4, addr::cSC4FireOccupant_Shutdown, Shutdown),
    OPENSC4_SLOT(addr::cSC4FireOccupant_vftable_4, 8, addr::cSC4FireOccupant_GetPosition, GetPosition),
    OPENSC4_SLOT(addr::cSC4FireOccupant_vftable_4, 10, addr::cSC4FireOccupant_GetBoundingBox, GetBoundingBox),
    OPENSC4_SLOT(addr::cSC4FireOccupant_vftable_8, 0, addr::cSC4FireOccupant_QueryInterfaceThunk8,
                 QueryInterfaceAt<0x8>),
    OPENSC4_SLOT(addr::cSC4FireOccupant_vftable_8, 5, addr::cSC4FireOccupant_GetGZCLSID, GetGZCLSID),
    OPENSC4_SLOT(addr::cSC4FireOccupant_vftable_C, 0, addr::cSC4FireOccupant_QueryInterfaceThunkC,
                 QueryInterfaceAt<0xC>),
    OPENSC4_SLOT(addr::cSC4FireOccupant_vftable_20, 0, addr::cSC4FireOccupant_QueryInterfaceThunk20,
                 QueryInterfaceAt<0x20>),
};
const Slot kSaveSlots[] = {
    OPENSC4_SLOT(addr::cSC4FireOccupant_vftable_8, 3, addr::cSC4FireOccupant_Write, Write),
    OPENSC4_SLOT(addr::cSC4FireOccupant_vftable_8, 4, addr::cSC4FireOccupant_Read, Read),
    OPENSC4_SLOT(addr::cSC4FireOccupant_vftable_C, 23, addr::cSC4FireOccupant_WriteThunk, WriteThunk),
    OPENSC4_SLOT(addr::cSC4FireOccupant_vftable_C, 24, addr::cSC4FireOccupant_ReadThunk, ReadThunk),
};

#undef OPENSC4_SLOT

template <std::size_t N>
bool SlotsHoldOriginals(const Slot (&slots)[N], const char* what)
{
    for (const Slot& slot : slots) {
        const auto* const vtable = reinterpret_cast<const std::uintptr_t*>(slot.vtable);
        if (vtable[slot.index] != slot.original) {
            log::Write("fireocc %s: slot %d of %08X holds %08X, expected %08X; not installing", what, slot.index,
                       static_cast<unsigned>(slot.vtable), static_cast<unsigned>(vtable[slot.index]),
                       static_cast<unsigned>(slot.original));
            return false;
        }
    }
    return true;
}

template <std::size_t N>
bool SwapSlots(const Slot (&slots)[N], const char* what)
{
    if (!game::IsBuild641() || !SlotsHoldOriginals(slots, what))
        return false;
    for (const Slot& slot : slots) {
        auto* const entry = reinterpret_cast<std::uintptr_t*>(slot.vtable) + slot.index;
        DWORD old = 0;
        if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &old))
            return false;
        *entry = reinterpret_cast<std::uintptr_t>(slot.ours);
        VirtualProtect(entry, sizeof(*entry), old, &old);
    }
    log::Write("fireocc %s: %u slots are ours", what, static_cast<unsigned>(N));
    return true;
}

using CheckBurnOutFn = uint32_t(__fastcall*)(char*, void*, uint32_t);
CheckBurnOutFn g_originalCheckBurnOut = nullptr;

// The slots are reached through the vtables only (the QueryInterface thunks
// jump to 0x0046FE30 and the save thunks to Write and Read inside the
// class; the class registration's call of GetGZCLSID at 0x004606EE runs
// before any replacement is installed), and no other class's vtable holds
// one: slot swaps. The end time's function is not a slot: a detour. Its two
// game call sites (0x00579280: pops and `mov al, 1` after; 0x0057BBF0:
// `test eax, eax`, then ESI and EBX) keep nothing in ECX or EDX across it
// and pass the one argument on the stack: a plain detour, no thunk. The
// detour goes in first, so that a failure leaves the vtables as they were.
bool InstallSlots(registry::Mode)
{
    if (!game::IsBuild641() || !SlotsHoldOriginals(kSlots, "slots"))
        return false;
    const addr::Entry* const check = addr_check::Find(addr::cSC4FireOccupant_CheckBurnOut);
    if (!check || !hook::Install(*check, &CheckBurnOut, &g_originalCheckBurnOut))
        return false;
    return SwapSlots(kSlots, "slots");
}

bool InstallSave(registry::Mode) { return SwapSlots(kSaveSlots, "save"); }

const bool g_registered[] = {
    registry::Register({"simulators.fireocc.slots", "simulators/fireocc", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4FireOccupant_vftable,
                        "Our cSC4FireOccupant slots: QueryInterface and the parts' thunks, SetTarget, SetStage, "
                        "SetHosed, Init, Shutdown, GetPosition, GetBoundingBox, GetGZCLSID, and the end time's "
                        "function by detour (src/core/fireocc_sim.h)",
                        false, InstallSlots}),
    registry::Register({"simulators.fireocc.save", "simulators/fireocc", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4FireOccupant_vftable_8,
                        "Our cSC4FireOccupant Write and Read (the save record, version 3, with the flames; "
                        "src/core/fireocc_sim.h)",
                        false, InstallSave}),
};

// --- Our object ---------------------------------------------------------------------

// Copies of the game's five vtables, taken at the first construction, after
// every PostAppInit slot swap of the other groups.
constexpr int kSlots0 = 15, kSlots4 = 34, kSlots8 = 6, kSlotsC = 25, kSlots20 = 5;
void* g_vtable[kSlots0];
void* g_vtable4[kSlots4];
void* g_vtable8[kSlots8];
void* g_vtableC[kSlotsC];
void* g_vtable20[kSlots20];

void SetVtables(char* self)
{
    At<void**>(self, 0x0) = g_vtable;
    At<void**>(self, field::kOccupant) = g_vtable4;
    At<void**>(self, field::kSerializable) = g_vtable8;
    At<void**>(self, field::kHolder) = g_vtableC;
    At<void**>(self, field::kExemplarPart) = g_vtable20;
}

// The destructor (0x00471290) and the deleting destructor (0x00471600):
// the class's vtables, the flame list's memory freed (the flames
// themselves are Shutdown's to delete), the target released (the pointer
// is left), the occupant base destroyed, and with flag 1 the memory back
// to the game's operator delete.
void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("DeletingDestructor", logged);
    SetVtables(self);
    if (void* const flames = FlamesBegin(self))
        CallCdecl<void, void*>(addr::SC4_Free, flames);
    if (void* const target = At<void*>(self, field::kTarget))
        Virtual<uint32_t>(target, 0x8);
    CallThis<void>(addr::cSC4Occupant_Destroy, self + field::kOccupant);
    if (flags & 1)
        CallCdecl<void, void*>(addr::SC4_operator_delete, self);
    return self;
}

// The holder part's slot 22 (0x00471260).
void* __fastcall DeletingDestructorAtHolder(char* part, void*, uint32_t flags)
{
    return DeletingDestructor(part - field::kHolder, nullptr, flags);
}

void BuildVtables()
{
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4FireOccupant_vftable), sizeof(g_vtable));
    std::memcpy(g_vtable4, reinterpret_cast<const void*>(addr::cSC4FireOccupant_vftable_4), sizeof(g_vtable4));
    std::memcpy(g_vtable8, reinterpret_cast<const void*>(addr::cSC4FireOccupant_vftable_8), sizeof(g_vtable8));
    std::memcpy(g_vtableC, reinterpret_cast<const void*>(addr::cSC4FireOccupant_vftable_C), sizeof(g_vtableC));
    std::memcpy(g_vtable20, reinterpret_cast<const void*>(addr::cSC4FireOccupant_vftable_20), sizeof(g_vtable20));
    g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
    g_vtable4[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x4>);
    g_vtable8[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x8>);
    g_vtable8[5] = reinterpret_cast<void*>(&GetGZCLSID);
    g_vtableC[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0xC>);
    g_vtableC[22] = reinterpret_cast<void*>(&DeletingDestructorAtHolder);
    g_vtable20[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x20>);
    log::Write("fireocc object: vtables copied (slots 4/8/12 %p %p %p, +4 slots 3/4/8/10 %p %p %p %p, +8 slots 3/4 "
               "%p %p)",
               g_vtable[4], g_vtable[8], g_vtable[12], g_vtable4[3], g_vtable4[4], g_vtable4[8], g_vtable4[10],
               g_vtable8[3], g_vtable8[4]);
}

// The constructor (detour of 0x004711C0): the occupant base constructed at
// its address on the object + 4, the two flame numbers, the heat, the
// stage count, the hosed count and the end time zeroed, our vtables, the
// stage -1, the target, the flame list and the on-model byte zeroed, and
// last the occupant type. +0x65 to +0x67 and the family at +0x68 are left
// as the heap's memory holds them, as in the game.
void* __fastcall Construct(char* self, void*)
{
    static bool logged = false;
    FirstCall("Construct", logged);
    static bool built = false;
    if (!built) {
        built = true;
        BuildVtables();
    }
    CallThis<void*>(addr::cSC4Occupant_cSC4Occupant, self + field::kOccupant);
    At<uint32_t>(self, field::kFlamesLow) = 0;
    At<uint32_t>(self, field::kFlamesHigh) = 0;
    At<uint32_t>(self, field::kHeat) = 0;
    At<uint32_t>(self, field::kStageCount) = 0;
    At<uint32_t>(self, field::kHosed) = 0;
    At<uint32_t>(self, field::kEndTime) = 0;
    SetVtables(self);
    At<int32_t>(self, field::kStage) = -1;
    At<void*>(self, field::kTarget) = nullptr;
    for (std::size_t offset = field::kFlames; offset < field::kOnModel; offset += 4)
        At<uint32_t>(self, offset) = 0;
    At<uint8_t>(self, field::kOnModel) = 0;
    At<uint32_t>(self, field::kType) = kOccupantType;
    static int created = 0;
    if (created++ < 3)
        log::Write("fireocc object: constructed %p", static_cast<void*>(self));
    return self;
}

// The factory (detour of 0x004715E0): 0x6C bytes of the game's operator
// new, constructed; null when there is no memory. The result is the object
// itself (the game jumps to the constructor), not its occupant part.
void* __cdecl Create()
{
    static bool logged = false;
    FirstCall("Create", logged);
    char* const memory = CallCdecl<char*, std::size_t>(addr::SC4_operator_new, kObjectSize);
    return memory ? Construct(memory, nullptr) : nullptr;
}

using ConstructFn = void*(__fastcall*)(char*, void*);
using CreateFn = void*(__cdecl*)();
ConstructFn g_originalConstruct = nullptr;
CreateFn g_originalCreate = nullptr;

// Every construction path: the factory (called through the class
// registration's pointer when a save is read; it ends in a jump to the
// constructor, so its caller reads only EAX) and the fire instance's start,
// 0x0057A985 (operator new; `add esp, 4`, `cmp eax, edi` after) and
// 0x0057A993 (the constructor; `mov edi, eax` after), which keeps nothing
// in ECX or EDX across the call; our own start (src/core/disaster_fire.cpp)
// calls both at their addresses. There is no allocator of the class's own:
// the memory is the game's operator new's, left alone. The deleting
// destructor is reached through the holder part's slot 22 only (0x00471260
// jumps to it), so the vtable copy replaces it without a detour; fires
// made before this group is installed (there are none at PostAppInit)
// would keep the game's.
bool ApplyObject(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    const addr::Entry* const construct = addr_check::Find(addr::cSC4FireOccupant_Construct);
    const addr::Entry* const create = addr_check::Find(addr::cSC4FireOccupant_Create);
    return construct && create && hook::Install(*construct, &Construct, &g_originalConstruct) &&
           hook::Install(*create, &Create, &g_originalCreate);
}

const bool g_registeredObject = registry::Register({
    "simulators.fireocc.object",
    "simulators/fireocc",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4FireOccupant_Construct,
    "Our cSC4FireOccupant object: the constructor and the factory (detours), the destructor, identity; methods "
    "from copies of the (patched) game vtables taken once (src/core/fireocc_sim.h)",
    false,
    ApplyObject,
});

} // namespace
} // namespace opensc4::fireocc_sim
