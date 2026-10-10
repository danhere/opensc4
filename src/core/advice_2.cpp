// The city situation advice (cISC4CitySituationAdvice, the missions of Rush
// Hour): the advice class derived from cSC4Advice, functions between
// 0x00422FF0 and 0x00424B00. Replacement, off by default:
//
//   scripts.advisors.advice2: the object (construction, destruction, the
//       interface thunks), Init (the advice's entry in the script's
//       "advices" table, kept as a reference), the fields read from that
//       table, the getters, the texts and images read from the table on
//       demand, and three small helpers after the class.
//
// The object is 0x19C bytes: the situation interface's vtable at +0, the
// cSC4Advice base at +4 (its vtables at +4, +8 and +0x10). Offsets below are
// from the object's start unless a function says its `this` is the advice.
// The base class's functions, the script table readers (0x005FBxxx) and the
// string code stay the game's and are called by address. Getters that the
// linker folded with other classes' code, and functions with callers outside
// the advisor code, are not hooked.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace opensc4::advice {
namespace {

namespace field {
constexpr std::size_t kCondition = 0xC4;
constexpr std::size_t kTimeLimit = 0xC8;
constexpr std::size_t kSuccessDistance = 0xCC;
constexpr std::size_t kMinTargetDistance = 0xD0;
constexpr std::size_t kMaxTargetDistance = 0xD4;
constexpr std::size_t kSuccessTimeout = 0xD8;
constexpr std::size_t kDelayAfterDisabled = 0xDC;
constexpr std::size_t kFlags = 0xE0;
constexpr std::size_t kAutomata = 0xE4;       // vector of uint32
constexpr std::size_t kTargets = 0xF0;        // vector of uint32
constexpr std::size_t kSequence = 0xFC;       // vector of uint32
constexpr std::size_t kEvilTwin = 0x108;
constexpr std::size_t kRewardGuid = 0x110;
constexpr std::size_t kActiveRadius = 0x114;
constexpr std::size_t kEvadeList = 0x118;     // vector of uint32
constexpr std::size_t kEvadeDistance = 0x124;
constexpr std::size_t kEvadeTimeout = 0x128;
constexpr std::size_t kCoverageType = 0x12C;
constexpr std::size_t kCoverageZone = 0x130;
constexpr std::size_t kCoverageLot = 0x134;
constexpr std::size_t kCoverageNetwork = 0x138;
constexpr std::size_t kCoverageRect = 0x13C;  // four int16
constexpr std::size_t kCoverageCells = 0x144; // list of two uint16
constexpr std::size_t kCellsMin = 0x148;
constexpr std::size_t kCellsMax = 0x14C;
constexpr std::size_t kSuccessCount = 0x150;
constexpr std::size_t kSuccessPct = 0x154;
constexpr std::size_t kSuccessAuraRadius = 0x158;
constexpr std::size_t kSuccessAuraMagnitude = 0x15C;
constexpr std::size_t kSuccessEffect = 0x160; // string
constexpr std::size_t kSuccessMoney = 0x174;
constexpr std::size_t kFailureAuraRadius = 0x178;
constexpr std::size_t kFailureAuraMagnitude = 0x17C;
constexpr std::size_t kFailureEffect = 0x180; // string
constexpr std::size_t kFailureMoney = 0x194;
constexpr std::size_t kTableRef = 0x198;      // script reference of the table, -2 none
} // namespace field

constexpr int32_t kNoRef = -2;

void* Allocate(uint32_t size)
{
    return reinterpret_cast<void*(__cdecl*)(uint32_t)>(addr::SC4_stl_allocate)(size);
}

void Free(void* memory)
{
    reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(memory);
}

void FreeIfSet(void* memory)
{
    if (memory)
        Free(memory);
}

// The game's readers of a field of the table at a stack index of the script
// context; each leaves the stack as it found it.
using FieldFn = uint8_t(__cdecl*)(void* context, const char* name, void* out, int32_t table);

void FloatField(void* context, const char* name, float* out, int32_t table)
{
    reinterpret_cast<FieldFn>(addr::SC4ScriptField_Float)(context, name, out, table);
}

void Uint32Field(void* context, const char* name, uint32_t* out, int32_t table)
{
    reinterpret_cast<FieldFn>(addr::SC4ScriptField_Uint32)(context, name, out, table);
}

void Sint32Field(void* context, const char* name, int32_t* out, int32_t table)
{
    reinterpret_cast<FieldFn>(addr::SC4ScriptField_Sint32)(context, name, out, table);
}

void BoolField(void* context, const char* name, uint8_t* out, int32_t table)
{
    reinterpret_cast<FieldFn>(addr::SC4ScriptField_Bool)(context, name, out, table);
}

void StringField(void* context, const char* name, void* out, int32_t table)
{
    reinterpret_cast<FieldFn>(addr::SC4ScriptField_String)(context, name, out, table);
}

// Pushes the field; false (and nothing pushed) when it is nil.
uint8_t PushField(void* context, const char* name, int32_t table)
{
    using Fn = uint8_t(__cdecl*)(void*, const char*, int32_t);
    return reinterpret_cast<Fn>(addr::SC4Script_GetField)(context, name, table);
}

// The table at a stack index as a vector of uint32.
void Uint32Vector(void* context, void* vector, int32_t index)
{
    using Fn = void(__cdecl*)(void*, void*, int32_t);
    reinterpret_cast<Fn>(addr::SC4ScriptValue_Uint32Vector)(context, vector, index);
}

void PushBack(void* vector, const uint32_t* value)
{
    reinterpret_cast<void(__thiscall*)(void*, const uint32_t*)>(addr::SC4PtrVector_PushBack)(vector, value);
}

void AssignRange(void* range, char* begin, char* end, void* tag)
{
    using Fn = void(__thiscall*)(void*, char*, char*, void*);
    reinterpret_cast<Fn>(addr::SC4String_AssignRange)(range, begin, end, tag);
}

// The truncating float-to-int conversion (0x009EEF04).
int32_t Truncate(double value)
{
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

// The static localised strings' first use: construction and the exit handler.
void EnsureStatic(std::uintptr_t guard, std::uintptr_t text, std::uintptr_t atExit)
{
    if (Global<uint8_t>(guard) & 1)
        return;
    Global<uint32_t>(guard) |= 1;
    reinterpret_cast<void(__thiscall*)(void*)>(addr::SC4LocalizedString_ConstructBase)(reinterpret_cast<void*>(text));
    Global<uint32_t>(text) = addr::SC4LocalizedString_Vtable;
    reinterpret_cast<int(__cdecl*)(void*)>(addr::SC4_atexit)(reinterpret_cast<void*>(atExit));
}

// Empties a static string's text as the original does: an empty 8-byte
// buffer assigned and freed.
void ClearRange(void* range, void* tag)
{
    char* const empty = static_cast<char*>(Allocate(8));
    empty[0] = 0;
    AssignRange(range, empty, empty, tag);
    Free(empty);
}

// The key of the localised string from a text, the text's translation
// loaded, or the text itself when there is none.
void SetText(char* text, const char* key, uint32_t length, void* fallback)
{
    reinterpret_cast<void(__thiscall*)(void*, const char*, uint32_t)>(addr::cSC4Advisors_F4179F0)(text, key, length);
    using LoadFn = uint8_t(__thiscall*)(void*, uint32_t, uint32_t);
    if (!reinterpret_cast<LoadFn>(addr::SC4LocalizedString_Load)(text, 1, 1)) {
        reinterpret_cast<void(__thiscall*)(void*, void*)>(addr::cRZBaseString_Assign)(text, fallback);
        using ExpandFn = uint8_t(__thiscall*)(void*, void*, uint32_t);
        reinterpret_cast<ExpandFn>(addr::SC4LocalizedString_Substitute)(text, nullptr, 1);
    }
}

// The game's 0x00423020. QueryInterface; `this` is the object.
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** out)
{
    if (iid == 0x6BEB1E8A) {
        *out = self;
        Virtual<uint32_t>(self, 0x4);
        return 1;
    }
    using Fn = uint8_t(__thiscall*)(void*, uint32_t, void**);
    return reinterpret_cast<Fn>(addr::SC4Advice_QueryInterface)(self + 4, iid, out);
}

// The game's 0x00423050. AddRef: the advice's, which is a jump to the count
// at its +4.
uint32_t __fastcall AddRef(char* self, void*)
{
    return reinterpret_cast<uint32_t(__thiscall*)(void*)>(addr::SC4AdviceBase_AddRefThunk)(self + 4);
}

// The game's 0x00423060. Release.
uint32_t __fastcall Release(char* self, void*)
{
    return reinterpret_cast<uint32_t(__thiscall*)(void*)>(addr::SC4AdviceBase_ReleaseThunk)(self + 4);
}

// The game's 0x00423090. The advice's slot 4 (Shutdown); `this` is the
// advice. Gives the table's reference back.
uint8_t __fastcall Shutdown(char* advice, void*)
{
    reinterpret_cast<uint8_t(__thiscall*)(void*)>(addr::cSC4Advice_F407E90)(advice);
    void* const context = Global<void*>(addr::SC4_ScriptContext);
    if (context) {
        const int32_t ref = At<int32_t>(advice, field::kTableRef - 4);
        if (ref != kNoRef) {
            Virtual<void>(context, 0xF8, ref); // Unref
            At<int32_t>(advice, field::kTableRef - 4) = kNoRef;
        }
    }
    return 1;
}

// The game's 0x004230D0. A uint32 field of the advice's table by name, 0
// without the table.
uint32_t __fastcall ScriptUint(char* self, void*, const char* name)
{
    uint32_t value = 0;
    if (At<int32_t>(self, field::kTableRef) == kNoRef)
        return 0;
    void* const context = Global<void*>(addr::SC4_ScriptContext);
    if (!context)
        return 0;
    const int32_t top = Virtual<int32_t>(context, 0x18);       // GetTop
    Virtual<void>(context, 0xAC, At<int32_t>(self, field::kTableRef)); // GetRef
    const int32_t table = Virtual<int32_t>(context, 0x18);
    if (table != top)
        Uint32Field(context, name, &value, table);
    Virtual<void>(context, 0x1C, top); // SetTop
    return value;
}

// The game's 0x00423140. Slot 9, GetMaxTargetDistance: the field, or the
// city's width when it is not positive.
double __fastcall MaxTargetDistance(char* self, void*)
{
    if (At<float>(self, field::kMaxTargetDistance) > 0.0f)
        return static_cast<double>(At<float>(self, field::kMaxTargetDistance));
    void* const city = Global<void*>(addr::SC4_City);
    const float cells = static_cast<float>(static_cast<double>(Virtual<int32_t>(city, 0x174))); // CellCountX
    return Virtual<double>(city, 0x16C) * static_cast<double>(cells); // CellWidthX
}

// The game's 0x00423190. Slot 53, IsAutomatonRelevant: whether the
// automaton's slot 5 object answers its slot 22 for one of the automata
// groups.
uint8_t __fastcall IsAutomatonRelevant(char* self, void*, void* automaton)
{
    if (!automaton)
        return 0;
    for (uint32_t* group = At<uint32_t*>(self, field::kAutomata); group != At<uint32_t*>(self, field::kAutomata + 4);
         ++group) {
        void* const holder = Virtual<void*>(automaton, 0x14);
        if (Virtual<uint8_t>(holder, 0x58, *group))
            return 1;
    }
    return 0;
}

// The game's 0x004231F0. Slot 54, IsGroupRelevant: whether one of the
// automata groups is in the vector.
uint8_t __fastcall IsGroupRelevant(char* self, void*, char* groups)
{
    uint32_t* const list = At<uint32_t*>(groups, 0);
    const uint32_t count = static_cast<uint32_t>((At<char*>(groups, 4) - At<char*>(groups, 0)) >> 2);
    uint8_t found = 0;
    if (count == 0)
        return found;
    uint32_t* group = At<uint32_t*>(self, field::kAutomata);
    uint32_t* const end = At<uint32_t*>(self, field::kAutomata + 4);
    for (; group != end; ++group) {
        if (found)
            break;
        const uint32_t wanted = *group;
        for (uint32_t i = 0; i < count; ++i) {
            if (list[i] == wanted) {
                found = 1;
                break;
            }
        }
    }
    return found;
}

// The game's 0x00423270. Slot 25, GetSituationImageId.
uint32_t __fastcall ImageId(char* self, void*)
{
    return ScriptUint(self, nullptr, "image");
}

// The game's 0x00423280. Slot 26, GetSuccessImageId.
uint32_t __fastcall SuccessImageId(char* self, void*)
{
    const uint32_t value = ScriptUint(self, nullptr, "success_image");
    if (value == 0)
        return Virtual<uint32_t>(self, 0x64);
    return value;
}

// The game's 0x004232A0. Slot 35, GetRewardUnlockedImageId.
uint32_t __fastcall RewardUnlockedImageId(char* self, void*)
{
    const uint32_t value = ScriptUint(self, nullptr, "reward_unlocked_image");
    if (value == 0)
        return Virtual<uint32_t>(self, 0x68);
    return value;
}

// The game's 0x004232C0. Slot 27, GetSuccessMood: the field, or the advice's
// slot 6.
uint32_t __fastcall SuccessMood(char* self, void*)
{
    const uint32_t value = ScriptUint(self, nullptr, "success_mood");
    if (value == 0)
        return Virtual<uint32_t>(self + 4, 0x18);
    return value;
}

// The game's 0x004232E0. Slot 28, GetFailureImageId.
uint32_t __fastcall FailureImageId(char* self, void*)
{
    const uint32_t value = ScriptUint(self, nullptr, "failure_image");
    if (value == 0)
        return Virtual<uint32_t>(self, 0x64);
    return value;
}

// The game's 0x00423300. Slot 29, GetFailureMood.
uint32_t __fastcall FailureMood(char* self, void*)
{
    const uint32_t value = ScriptUint(self, nullptr, "failure_mood");
    if (value == 0)
        return Virtual<uint32_t>(self + 4, 0x18);
    return value;
}

// The game's 0x00423430. Appends a cell (two uint16) to the list whose
// sentinel `list` points at.
char* __fastcall CellListPushBack(char* list, void*, const uint16_t* cell)
{
    char* const sentinel = At<char*>(list, 0);
    char* const node = static_cast<char*>(Allocate(0xC));
    At<uint16_t>(node, 8) = cell[0];
    At<uint16_t>(node, 0xA) = cell[1];
    char* const last = At<char*>(sentinel, 4);
    At<char*>(node, 0) = sentinel;
    At<char*>(node, 4) = last;
    At<char*>(last, 0) = node;
    At<char*>(sentinel, 4) = node;
    return node;
}

// The game's 0x00423470. The constructor.
char* __fastcall Construct(char* self, void*)
{
    At<uint32_t>(self, 0) = addr::SC4SituationAdvice_InterfaceVtable;
    reinterpret_cast<void*(__thiscall*)(void*)>(addr::SC4Advice_Construct)(self + 4);
    At<uint32_t>(self, 0) = addr::SC4SituationAdvice_Vtable;
    At<uint32_t>(self, 4) = addr::SC4SituationAdvice_Vtable4;
    At<uint32_t>(self, 8) = addr::SC4SituationAdvice_Vtable8;
    At<uint32_t>(self, 0x10) = addr::SC4SituationAdvice_Vtable10;
    At<uint32_t>(self, field::kCondition) = 1;
    At<uint32_t>(self, field::kTimeLimit) = 0;
    At<float>(self, field::kSuccessDistance) = 16.0f;
    At<uint32_t>(self, field::kMinTargetDistance) = 0;
    At<uint32_t>(self, field::kMaxTargetDistance) = 0;
    At<uint32_t>(self, field::kFlags) = 0;
    At<uint32_t>(self, field::kAutomata) = 0;
    At<uint32_t>(self, field::kAutomata + 4) = 0;
    At<uint32_t>(self, field::kAutomata + 8) = 0;
    At<uint32_t>(self, field::kTargets) = 0;
    At<uint32_t>(self, field::kTargets + 4) = 0;
    At<uint32_t>(self, field::kTargets + 8) = 0;
    At<uint32_t>(self, field::kSequence) = 0;
    At<uint32_t>(self, field::kSequence + 4) = 0;
    At<uint32_t>(self, field::kSequence + 8) = 0;
    At<uint32_t>(self, field::kEvilTwin) = 0;
    At<uint32_t>(self, field::kRewardGuid) = 0;
    At<uint32_t>(self, field::kActiveRadius) = 0;
    At<uint32_t>(self, field::kEvadeList) = 0;
    At<uint32_t>(self, field::kEvadeList + 4) = 0;
    At<uint32_t>(self, field::kEvadeList + 8) = 0;
    At<uint32_t>(self, field::kEvadeDistance) = 0;
    At<uint32_t>(self, field::kEvadeTimeout) = 0;
    At<uint32_t>(self, field::kCoverageCells) = 0;
    char* const sentinel = static_cast<char*>(Allocate(0xC));
    At<char*>(sentinel, 0) = sentinel;
    At<char*>(sentinel, 4) = sentinel;
    At<char*>(self, field::kCoverageCells) = sentinel;
    At<uint32_t>(self, field::kCellsMin) = 0;
    At<uint32_t>(self, field::kCellsMax) = 0;
    At<uint32_t>(self, field::kSuccessPct) = 0;
    At<uint32_t>(self, field::kSuccessEffect + 4) = 0;
    At<uint32_t>(self, field::kSuccessEffect + 8) = 0;
    At<uint32_t>(self, field::kSuccessEffect + 0xC) = 0;
    char* const success = static_cast<char*>(Allocate(8));
    At<char*>(self, field::kSuccessEffect + 4) = success;
    At<char*>(self, field::kSuccessEffect + 8) = success;
    At<char*>(self, field::kSuccessEffect + 0xC) = success + 8;
    success[0] = 0;
    At<uint32_t>(self, field::kSuccessEffect + 0x10) = 0;
    At<uint32_t>(self, field::kSuccessEffect) = addr::SC4String_Vtable;
    At<uint32_t>(self, field::kFailureEffect + 4) = 0;
    At<uint32_t>(self, field::kFailureEffect + 8) = 0;
    At<uint32_t>(self, field::kFailureEffect + 0xC) = 0;
    char* const failure = static_cast<char*>(Allocate(8));
    At<char*>(self, field::kFailureEffect + 4) = failure;
    At<char*>(self, field::kFailureEffect + 8) = failure;
    At<char*>(self, field::kFailureEffect + 0xC) = failure + 8;
    failure[0] = 0;
    At<uint32_t>(self, field::kFailureEffect) = addr::SC4String_Vtable;
    At<uint32_t>(self, field::kFailureEffect + 0x10) = 0;
    At<int32_t>(self, field::kTableRef) = kNoRef;
    return self;
}

// The game's 0x004235F0. Slot 4, GetSuccessCondition.
uint32_t __fastcall SuccessCondition(char* self, void*)
{
    return At<uint32_t>(self, field::kCondition);
}

// The game's 0x00423600. Slot 6, GetTimeLimit.
double __fastcall TimeLimit(char* self, void*)
{
    return static_cast<double>(At<float>(self, field::kTimeLimit));
}

// The game's 0x00423610. Slot 7, GetSuccessDistance.
double __fastcall SuccessDistance(char* self, void*)
{
    return static_cast<double>(At<float>(self, field::kSuccessDistance));
}

// The game's 0x00423620. Slot 8, GetMinTargetDistance.
double __fastcall MinTargetDistance(char* self, void*)
{
    return static_cast<double>(At<float>(self, field::kMinTargetDistance));
}

// The game's 0x00423630. Slot 10, GetSuccessTimeout.
double __fastcall SuccessTimeout(char* self, void*)
{
    return static_cast<double>(At<float>(self, field::kSuccessTimeout));
}

// The game's 0x00423640. Slot 11, GetDelayAfterDisabled.
double __fastcall DelayAfterDisabled(char* self, void*)
{
    return static_cast<double>(At<float>(self, field::kDelayAfterDisabled));
}

// The game's 0x00423660. Slot 15, CheckSituationFlag.
uint32_t __fastcall CheckSituationFlag(char* self, void*, uint32_t flags)
{
    return (At<uint32_t>(self, field::kFlags) & flags) != 0 ? 1 : 0;
}

// The game's 0x00423680. Slot 12, GetAutomataGroups.
char* __fastcall AutomataGroups(char* self, void*)
{
    return self + field::kAutomata;
}

// The game's 0x00423690. Slot 13, GetTargetGroups.
char* __fastcall TargetGroups(char* self, void*)
{
    return self + field::kTargets;
}

// The game's 0x004236A0. Slot 16, GetTargetSequence.
char* __fastcall TargetSequence(char* self, void*)
{
    return self + field::kSequence;
}

// The game's 0x004236B0. Slot 17, HasTargetSequence.
uint8_t __fastcall HasTargetSequence(char* self, void*)
{
    return static_cast<uint8_t>((At<uint32_t>(self, field::kFlags) >> 2) & 1);
}

// The game's 0x004236C0. Slot 20, GetEvadeDistance.
double __fastcall EvadeDistance(char* self, void*)
{
    return static_cast<double>(At<float>(self, field::kEvadeDistance));
}

// The game's 0x004236D0. Slot 21, GetEvadeTimeout.
double __fastcall EvadeTimeout(char* self, void*)
{
    return static_cast<double>(At<float>(self, field::kEvadeTimeout));
}

// The game's 0x004236E0. Slot 52, GetActiveRadius.
double __fastcall ActiveRadius(char* self, void*)
{
    return static_cast<double>(At<float>(self, field::kActiveRadius));
}

// The game's 0x00423730. Slot 40, GetCoverageRect.
char* __fastcall CoverageRect(char* self, void*)
{
    return self + field::kCoverageRect;
}

// The game's 0x00423740. Slot 41, GetCellCoverageList.
char* __fastcall CellCoverageList(char* self, void*)
{
    return self + field::kCoverageCells;
}

// The game's 0x00423760. Slot 45, GetCoverageSuccessPct: the float's bits
// through the pointer.
uint32_t* __fastcall CoverageSuccessPct(char* self, void*, uint32_t* out)
{
    *out = At<uint32_t>(self, field::kSuccessPct);
    return out;
}

// The game's 0x00423770. Slot 46, GetSuccessAura.
uint32_t __fastcall SuccessAura(char* self, void*, char* radius, char* magnitude)
{
    At<uint32_t>(radius, 0) = At<uint32_t>(self, field::kSuccessAuraRadius);
    At<uint32_t>(magnitude, 0) = At<uint32_t>(self, field::kSuccessAuraMagnitude);
    return At<float>(radius, 0) > 0.0f ? 1 : 0;
}

// The game's 0x004237B0. Slot 48, GetSuccessMoney.
uint32_t __fastcall SuccessMoney(char* self, void*, int32_t* out)
{
    const int32_t value = At<int32_t>(self, field::kSuccessMoney);
    const int32_t high = value >> 31;
    out[0] = value;
    out[1] = high;
    return (value | high) != 0 ? 1 : 0;
}

// The game's 0x004237E0. Slot 49, GetFailureAura.
uint32_t __fastcall FailureAura(char* self, void*, char* radius, char* magnitude)
{
    At<uint32_t>(radius, 0) = At<uint32_t>(self, field::kFailureAuraRadius);
    At<uint32_t>(magnitude, 0) = At<uint32_t>(self, field::kFailureAuraMagnitude);
    return At<float>(radius, 0) > 0.0f ? 1 : 0;
}

// The game's 0x00423820. Slot 51, GetFailureMoney.
uint32_t __fastcall FailureMoney(char* self, void*, int32_t* out)
{
    const int32_t value = At<int32_t>(self, field::kFailureMoney);
    const int32_t high = value >> 31;
    out[0] = value;
    out[1] = high;
    return (value | high) != 0 ? 1 : 0;
}

// The game's 0x00423900. Slot 19, HasEvadeList.
uint8_t __fastcall HasEvadeList(char* self, void*)
{
    return At<uint32_t>(self, field::kEvadeList) != At<uint32_t>(self, field::kEvadeList + 4);
}

// The game's 0x00423910. The destructor.
void __fastcall Destruct(char* self, void*)
{
    At<uint32_t>(self, 0) = addr::SC4SituationAdvice_Vtable;
    At<uint32_t>(self, 4) = addr::SC4SituationAdvice_Vtable4;
    At<uint32_t>(self, 8) = addr::SC4SituationAdvice_Vtable8;
    At<uint32_t>(self, 0x10) = addr::SC4SituationAdvice_Vtable10;
    At<uint32_t>(self, field::kFailureEffect) = addr::SC4String_Vtable;
    FreeIfSet(At<void*>(self, field::kFailureEffect + 4));
    At<uint32_t>(self, field::kSuccessEffect) = addr::SC4String_Vtable;
    FreeIfSet(At<void*>(self, field::kSuccessEffect + 4));
    reinterpret_cast<void(__thiscall*)(void*)>(addr::SC4PairList_Destroy)(self + field::kCoverageCells);
    FreeIfSet(At<void*>(self, field::kEvadeList));
    FreeIfSet(At<void*>(self, field::kSequence));
    FreeIfSet(At<void*>(self, field::kTargets));
    FreeIfSet(At<void*>(self, field::kAutomata));
    reinterpret_cast<void(__thiscall*)(void*)>(addr::SC4Advice_Destruct)(self + 4);
}

// The game's 0x00423E00. The deleting destructor.
char* __fastcall DeletingDestruct(char* self, void*, uint32_t flags)
{
    Destruct(self, nullptr);
    if (flags & 1)
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_operator_delete)(self);
    return self;
}

// The interface thunks of the other vtables. The game's 0x00423860,
// 0x00423870, 0x00423880 (the advice at +4), 0x00423890, 0x004238A0,
// 0x004238B0, 0x004238C0 (+8), 0x004238D0, 0x004238E0, 0x004238F0 (+0x10).
uint8_t __fastcall QueryInterface4(char* part, void*, uint32_t iid, void** out)
{
    return QueryInterface(part - 4, nullptr, iid, out);
}

uint32_t __fastcall AddRef4(char* part, void*)
{
    return AddRef(part - 4, nullptr);
}

uint32_t __fastcall Release4(char* part, void*)
{
    return Release(part - 4, nullptr);
}

uint8_t __fastcall QueryInterface8(char* part, void*, uint32_t iid, void** out)
{
    return QueryInterface(part - 8, nullptr, iid, out);
}

uint32_t __fastcall AddRef8(char* part, void*)
{
    return AddRef(part - 8, nullptr);
}

uint32_t __fastcall Release8(char* part, void*)
{
    return Release(part - 8, nullptr);
}

char* __fastcall DeletingDestruct8(char* part, void*, uint32_t flags)
{
    return DeletingDestruct(part - 8, nullptr, flags);
}

uint8_t __fastcall QueryInterface10(char* part, void*, uint32_t iid, void** out)
{
    return QueryInterface(part - 0x10, nullptr, iid, out);
}

uint32_t __fastcall AddRef10(char* part, void*)
{
    return AddRef(part - 0x10, nullptr);
}

uint32_t __fastcall Release10(char* part, void*)
{
    return Release(part - 0x10, nullptr);
}

// The value on top of the script's stack into a vector of groups: a table
// as a whole, a number as one more item.
void ReadGroups(void* context, char* vector)
{
    if (Virtual<uint8_t>(context, 0x120, int32_t{-1})) { // IsTable
        Uint32Vector(context, vector, -1);
    } else if (Virtual<uint8_t>(context, 0x38, int32_t{-1})) { // IsNumber
        const uint32_t value = static_cast<uint32_t>(Truncate(Virtual<double>(context, 0x5C, int32_t{-1})));
        PushBack(vector, &value);
    }
}

// The game's 0x004239D0. The advice's slot 3 (Init); `this` is the advice.
// After the base's Init: finds advices[index], keeps a reference to it, and
// reads the groups and the creation flags; the rest through slot 55.
uint8_t __fastcall Init(char* advice, void*, void* script)
{
    char* const self = advice - 4;
    const uint8_t result = reinterpret_cast<uint8_t(__thiscall*)(void*, void*)>(addr::SC4Advice_Init)(advice, script);
    Virtual<uint8_t>(advice, 0x38, uint32_t{0x2000}, uint32_t{1}); // SetFlag
    void* const context = Global<void*>(addr::SC4_ScriptContext);
    if (!context)
        return 0;
    char* const buffer = static_cast<char*>(Allocate(8));
    buffer[0] = 0;
    void* const system = Global<void*>(addr::cSC4AdvisorSystem_Instance);
    char* const asAdvice = reinterpret_cast<std::uintptr_t>(self) != 0 ? advice : nullptr;
    Virtual<void>(system, 0x48, asAdvice); // PushContextAdvice
    const int32_t top = Virtual<int32_t>(context, 0x18);
    Virtual<void>(context, 0x94, Global<const char*>(addr::SC4Advice_AdvicesTableName)); // GetGlobal
    Virtual<void>(context, 0x7C, static_cast<double>(At<int32_t>(advice, 0x88)));          // PushNumber
    Virtual<void>(context, 0x98, int32_t{-2});                                             // GetTable
    if (!Virtual<uint8_t>(context, 0x120, int32_t{-1})) {
        Virtual<void>(context, 0x1C, top);
        Virtual<void>(system, 0x4C); // PopContextAdvice
        Free(buffer);
        return 0;
    }
    const int32_t table = Virtual<int32_t>(context, 0x18);
    Virtual<void>(context, 0x20, table);                                             // PushValue
    At<int32_t>(self, field::kTableRef) = Virtual<int32_t>(context, 0xC8, int32_t{1}); // Ref

    if (PushField(context, "automata_list", table)) {
        ReadGroups(context, self + field::kAutomata);
        Virtual<void>(context, 0x2C, int32_t{1}); // Pop
    }
    if (PushField(context, "target_list", table)) {
        ReadGroups(context, self + field::kTargets);
        Virtual<void>(context, 0x2C, int32_t{1});
    }
    if (PushField(context, "target_sequence", table)) {
        At<uint32_t>(self, field::kFlags) |= 4;
        if (Virtual<uint8_t>(context, 0x120, int32_t{-1})) {
            char* const sequence = self + field::kSequence;
            Uint32Vector(context, sequence, -1);
            // The zeros are taken out.
            uint32_t* at = At<uint32_t*>(sequence, 0);
            if (at != At<uint32_t*>(sequence, 4)) {
                uint32_t* next = at + 1;
                do {
                    if (*at == 0) {
                        uint32_t* const end = At<uint32_t*>(sequence, 4);
                        if (next != end) {
                            using MoveFn = void*(__cdecl*)(void*, const void*, uint32_t);
                            const auto bytes =
                                static_cast<uint32_t>(reinterpret_cast<char*>(end) - reinterpret_cast<char*>(next));
                            reinterpret_cast<MoveFn>(addr::SC4_memmove)(at, next, bytes);
                        }
                        At<uint32_t*>(sequence, 4) -= 1;
                    } else {
                        ++at;
                        ++next;
                    }
                } while (at != At<uint32_t*>(sequence, 4));
            }
        } else if (Virtual<uint8_t>(context, 0x38, int32_t{-1})) {
            // A single number goes to the target list, as in the original.
            const uint32_t value = static_cast<uint32_t>(Truncate(Virtual<double>(context, 0x5C, int32_t{-1})));
            PushBack(self + field::kTargets, &value);
        }
        // The original does not pop the value here; SetTop below drops it.
    }

    uint8_t createTarget = 0;
    BoolField(context, "create_target", &createTarget, table);
    if (createTarget)
        At<uint32_t>(self, field::kFlags) |= 1;
    uint8_t alwaysCreate = 0;
    BoolField(context, "always_create_target", &alwaysCreate, table);
    if (alwaysCreate)
        At<uint32_t>(self, field::kFlags) |= 0x100;
    uint8_t training = 0;
    BoolField(context, "training_mission", &training, table);
    if (training)
        At<uint32_t>(self, field::kFlags) |= 0x40;

    Virtual<void>(self, 0xDC); // UpdateSituationFields
    Virtual<void>(context, 0x1C, top);
    Free(buffer);
    return result;
}

// The game's 0x00423D20. Slot 5, GetSuccessConditionText: the condition's
// name from the text resources into the string.
uint8_t __fastcall SuccessConditionText(char* self, void*, void* out)
{
    uint32_t instance = 0;
    switch (Virtual<uint32_t>(self, 0x10)) {
    case 1:
        instance = 0x0C2BD5FB;
        break;
    case 2:
        instance = 0x2C2BD613;
        break;
    case 3:
        instance = 0x0C2BD605;
        break;
    case 5:
        instance = 0x8C2BD60A;
        break;
    default:
        return 0;
    }
    alignas(4) char text[0x20];
    using ConstructFn = void*(__thiscall*)(void*, uint32_t, uint32_t, uint32_t);
    void* const made =
        reinterpret_cast<ConstructFn>(addr::SC4LocalizedString_SC4LocalizedString)(text, 0x6A231EA4, instance, 1);
    Virtual<void*>(out, 0x34, made); // cIGZString slot 13: copy
    void* const buffer = At<void*>(text, 4);
    At<uint32_t>(text, 0) = addr::SC4String_Vtable;
    FreeIfSet(buffer);
    return 1;
}

// The game's 0x00423DD0. The class factory: the advice part of a new object.
char* __cdecl Create()
{
    void* const memory = reinterpret_cast<void*(__cdecl*)(uint32_t)>(addr::SC4_operator_new)(0x19C);
    if (!memory)
        return nullptr;
    char* const made = Construct(static_cast<char*>(memory), nullptr);
    if (!made)
        return nullptr;
    return made + 4;
}

// The game's 0x00423E20. Slot 55, UpdateSituationFields: the fields of the
// advice's table.
void __fastcall UpdateSituationFields(char* self, void*)
{
    void* const context = Global<void*>(addr::SC4_ScriptContext);
    if (!context)
        return;
    if (At<int32_t>(self, field::kTableRef) == kNoRef)
        return;
    const int32_t top = Virtual<int32_t>(context, 0x18);
    Virtual<void>(context, 0xAC, At<int32_t>(self, field::kTableRef));
    const int32_t table = Virtual<int32_t>(context, 0x18);

    uint32_t condition;
    Uint32Field(context, "condition", &condition, table);
    At<uint32_t>(self, field::kCondition) = condition;
    FloatField(context, "time_limit", &At<float>(self, field::kTimeLimit), table);
    FloatField(context, "success_distance", &At<float>(self, field::kSuccessDistance), table);
    FloatField(context, "min_target_distance", &At<float>(self, field::kMinTargetDistance), table);
    FloatField(context, "max_target_distance", &At<float>(self, field::kMaxTargetDistance), table);
    FloatField(context, "success_timeout", &At<float>(self, field::kSuccessTimeout), table);
    FloatField(context, "delay_after_disabled", &At<float>(self, field::kDelayAfterDisabled), table);
    FloatField(context, "active_radius", &At<float>(self, field::kActiveRadius), table);

    uint8_t service = 0;
    BoolField(context, "service_mission", &service, table);
    if (service)
        At<uint32_t>(self, field::kFlags) |= 8;
    uint8_t lotBoundary = 0;
    BoolField(context, "use_lot_boundary", &lotBoundary, table);
    if (lotBoundary)
        At<uint32_t>(self, field::kFlags) |= 0x10;
    uint8_t mySim = 0;
    BoolField(context, "mysim_mission", &mySim, table);
    if (mySim)
        At<uint32_t>(self, field::kFlags) |= 0x20;
    uint8_t pursuit = 0;
    BoolField(context, "pursuit_mission", &pursuit, table);
    if (pursuit)
        At<uint32_t>(self, field::kFlags) |= 0x80;

    if (PushField(context, "evade_list", table)) {
        if (!pursuit)
            At<uint32_t>(self, field::kFlags) |= 2;
        Uint32Vector(context, self + field::kEvadeList, -1);
        FloatField(context, "evade_distance", &At<float>(self, field::kEvadeDistance), table);
        FloatField(context, "evade_timeout", &At<float>(self, field::kEvadeTimeout), table);
        // Not popped in the original; SetTop at the end drops it.
    }

    uint32_t coverage;
    Uint32Field(context, "coverage_type", &coverage, table);
    At<uint32_t>(self, field::kCoverageType) = coverage;
    switch (coverage) {
    case 1: {
        uint32_t zone;
        Uint32Field(context, "coverage_zone", &zone, table);
        At<uint32_t>(self, field::kCoverageZone) = zone;
        break;
    }
    case 2:
        Uint32Field(context, "coverage_lot", &At<uint32_t>(self, field::kCoverageLot), table);
        break;
    case 3:
        Uint32Field(context, "coverage_network", &At<uint32_t>(self, field::kCoverageNetwork), table);
        break;
    case 4: {
        At<uint32_t>(self, field::kCoverageRect) = 0;
        At<uint32_t>(self, field::kCoverageRect + 4) = 0;
        char* numbers[3] = {nullptr, nullptr, nullptr}; // a vector of uint32
        if (PushField(context, "coverage_rect", table)) {
            Uint32Vector(context, numbers, -1);
            if (static_cast<uint32_t>((numbers[1] - numbers[0]) >> 2) < 4) {
                using ResizeFn = void(__thiscall*)(void*, uint32_t);
                reinterpret_cast<ResizeFn>(addr::cSC4Advice_F423DB0)(numbers, 4);
            }
            const char* const begin = numbers[0];
            At<uint16_t>(self, field::kCoverageRect) = *reinterpret_cast<const uint16_t*>(begin);
            At<uint16_t>(self, field::kCoverageRect + 2) = *reinterpret_cast<const uint16_t*>(begin + 4);
            At<uint16_t>(self, field::kCoverageRect + 4) = *reinterpret_cast<const uint16_t*>(begin + 8);
            At<uint16_t>(self, field::kCoverageRect + 6) = *reinterpret_cast<const uint16_t*>(begin + 0xC);
            // Not popped in the original.
        }
        FreeIfSet(numbers[0]);
        break;
    }
    case 7: {
        reinterpret_cast<void(__thiscall*)(void*)>(addr::SC4StlList_Clear)(self + field::kCoverageCells);
        if (PushField(context, "coverage_cells", table)) {
            // A walk over the table on top of the stack (0x20 bytes: the
            // context, a string at +4, the table's index at +0x18, the end
            // flag at +0x1C).
            alignas(4) char walk[0x20];
            using WalkFn = void*(__thiscall*)(void*, void*, int32_t);
            reinterpret_cast<WalkFn>(addr::SC4ScriptTableWalk_Construct)(walk, context, -1);
            reinterpret_cast<uint8_t(__thiscall*)(void*)>(addr::SC4ScriptTableWalk_First)(walk);
            while (!At<uint8_t>(walk, 0x1C)) {
                if (Virtual<uint8_t>(context, 0x120, int32_t{-1})) {
                    char* pair[3] = {nullptr, nullptr, nullptr};
                    Uint32Vector(context, pair, -1);
                    if (static_cast<uint32_t>((pair[1] - pair[0]) >> 2) >= 2) {
                        uint16_t cell[2];
                        cell[0] = *reinterpret_cast<const uint16_t*>(pair[0]);
                        cell[1] = *reinterpret_cast<const uint16_t*>(pair[0] + 4);
                        CellListPushBack(self + field::kCoverageCells, nullptr, cell);
                    }
                    FreeIfSet(pair[0]);
                }
                reinterpret_cast<uint8_t(__thiscall*)(void*)>(addr::SC4ScriptTableWalk_Next)(walk);
            }
            Virtual<void>(context, 0x2C, int32_t{1});
            void* const buffer = At<void*>(walk, 8);
            At<uint32_t>(walk, 4) = addr::SC4String_Vtable;
            FreeIfSet(buffer);
        }
        break;
    }
    default:
        break;
    }

    Uint32Field(context, "coverage_cells_min", &At<uint32_t>(self, field::kCellsMin), table);
    Uint32Field(context, "coverage_cells_max", &At<uint32_t>(self, field::kCellsMax), table);
    Uint32Field(context, "coverage_success_count", &At<uint32_t>(self, field::kSuccessCount), table);
    float percent = 100.0f;
    FloatField(context, "coverage_success_pct", &percent, table);
    // The constant is the float 0.01 at 0x00A823A4.
    At<float>(self, field::kSuccessPct) = static_cast<float>(static_cast<double>(percent) * static_cast<double>(0.01f));
    Uint32Field(context, "evil_twin", &At<uint32_t>(self, field::kEvilTwin), table);
    Uint32Field(context, "reward_guid", &At<uint32_t>(self, field::kRewardGuid), table);
    FloatField(context, "success_aura_radius", &At<float>(self, field::kSuccessAuraRadius), table);
    FloatField(context, "success_aura_magnitude", &At<float>(self, field::kSuccessAuraMagnitude), table);
    StringField(context, "success_effect", self + field::kSuccessEffect, table);
    Sint32Field(context, "success_money", &At<int32_t>(self, field::kSuccessMoney), table);
    FloatField(context, "failure_aura_radius", &At<float>(self, field::kFailureAuraRadius), table);
    FloatField(context, "failure_aura_magnitude", &At<float>(self, field::kFailureAuraMagnitude), table);
    StringField(context, "failure_effect", self + field::kFailureEffect, table);
    Sint32Field(context, "failure_money", &At<int32_t>(self, field::kFailureMoney), table);
    Virtual<void>(context, 0x1C, top);
}

// A string's text into the caller's string; whether it is not empty.
uint32_t CopyEffectName(char* effect, char*& out)
{
    char* const from = effect ? effect + 4 : nullptr;
    char* const to = out + 4;
    if (from != to)
        AssignRange(to, At<char*>(from, 0), At<char*>(from, 4), &out);
    return At<char*>(to, 4) != At<char*>(to, 0) ? 1 : 0;
}

// The game's 0x004243B0. Slot 47, GetSuccessEffectName.
uint32_t __fastcall SuccessEffectName(char* self, void*, char* out)
{
    return CopyEffectName(self + field::kSuccessEffect, out);
}

// The game's 0x00424400. Slot 50, GetFailureEffectName.
uint32_t __fastcall FailureEffectName(char* self, void*, char* out)
{
    return CopyEffectName(self + field::kFailureEffect, out);
}

// The game's 0x00424450. Slot 24, GetProgressText: entry `index` (from 1) of
// the table's "progress_text" list, the first when there are not that many,
// in a static localised string.
char* __fastcall ProgressText(char* self, void*, uint32_t index)
{
    char* const text = reinterpret_cast<char*>(addr::SC4SituationAdvice_ProgressText);
    EnsureStatic(addr::SC4SituationAdvice_ProgressTextGuard, addr::SC4SituationAdvice_ProgressText,
                 addr::SC4SituationAdvice_ProgressTextAtExit);
    ClearRange(text + 4, &index);
    if (static_cast<uint16_t>(index) == 0)
        return text;
    uint32_t wanted = index + 0xFFFF;
    if (At<int32_t>(self, field::kTableRef) == kNoRef)
        return text;
    void* const context = Global<void*>(addr::SC4_ScriptContext);
    if (!context)
        return text;
    const int32_t top = Virtual<int32_t>(context, 0x18);
    Virtual<void>(context, 0xAC, At<int32_t>(self, field::kTableRef));
    const int32_t table = Virtual<int32_t>(context, 0x18);
    if (table != top && PushField(context, "progress_text", table)) {
        char* strings[3] = {nullptr, nullptr, nullptr}; // a vector of 0x14-byte strings
        using Fn = void(__cdecl*)(void*, void*, int32_t);
        reinterpret_cast<Fn>(addr::SC4ScriptValue_StringVector)(context, strings, -1);
        char* const begin = strings[0];
        const uint32_t count = static_cast<uint32_t>(static_cast<int32_t>(strings[1] - begin) / 20);
        if (!(count > static_cast<uint16_t>(wanted)))
            wanted = 0;
        if (static_cast<uint16_t>(wanted) < count) {
            const uint32_t offset = static_cast<uint32_t>(static_cast<uint16_t>(wanted)) * 20;
            char* const entry = begin + offset;
            const uint32_t length = static_cast<uint32_t>(At<char*>(entry, 8) - At<char*>(entry, 4));
            const char* const key = Virtual<const char*>(entry, 0x14);
            SetText(text, key, length, strings[0] + offset);
        }
        reinterpret_cast<void(__thiscall*)(void*)>(addr::cSC4Advisors_F419140)(strings);
        // "progress_text" is not popped in the original.
    }
    Virtual<void>(context, 0x1C, top);
    return text;
}

// The game's 0x00424600. A text field of the advice's table by name, in a
// static localised string (empty without the table).
char* __fastcall ScriptText(char* self, void*, const char* name)
{
    char* const text = reinterpret_cast<char*>(addr::SC4SituationAdvice_ScriptText);
    EnsureStatic(addr::SC4SituationAdvice_ScriptTextGuard, addr::SC4SituationAdvice_ScriptText,
                 addr::SC4SituationAdvice_ScriptTextAtExit);
    ClearRange(text + 4, &name);
    if (At<int32_t>(self, field::kTableRef) == kNoRef)
        return text;
    void* const context = Global<void*>(addr::SC4_ScriptContext);
    if (!context)
        return text;
    const int32_t top = Virtual<int32_t>(context, 0x18);
    Virtual<void>(context, 0xAC, At<int32_t>(self, field::kTableRef));
    const int32_t table = Virtual<int32_t>(context, 0x18);
    if (table != top) {
        alignas(4) char value[0x14];
        reinterpret_cast<void*(__thiscall*)(void*)>(addr::SC4String_SC4String)(value);
        StringField(context, name, value, table);
        const uint32_t length = reinterpret_cast<uint32_t(__thiscall*)(void*)>(addr::cRZBaseString_Strlen)(value);
        const char* const key = reinterpret_cast<const char*(__thiscall*)(void*)>(addr::cRZBaseString_Data)(value);
        SetText(text, key, length, value);
        void* const buffer = At<void*>(value, 4);
        At<uint32_t>(value, 0) = addr::SC4String_Vtable;
        FreeIfSet(buffer);
    }
    Virtual<void>(context, 0x1C, top);
    return text;
}

// The game's 0x00424750. The advice's slot 18 (the title); `this` is the
// advice.
char* __fastcall Title(char* advice, void*)
{
    return ScriptText(advice - 4, nullptr, "title");
}

// The game's 0x00424760. The advice's slot 20 (the message); `this` is the
// advice. The table's "message"; when the evil twin's advice is one its
// advisor has and that advice's slot 8 says yes, a line from the text
// resources with the twin's number is added after a break.
char* __fastcall Message(char* advice, void*)
{
    char* const result = reinterpret_cast<char*>(addr::SC4SituationAdvice_MessageText);
    EnsureStatic(addr::SC4SituationAdvice_MessageTextGuard, addr::SC4SituationAdvice_MessageText,
                 addr::SC4SituationAdvice_MessageTextAtExit);
    char* const message = ScriptText(advice - 4, nullptr, "message");
    const uint32_t length = Virtual<uint32_t>(message, 0x1C);
    char* const chars = Virtual<char*>(message, 0x18);
    uint8_t tag; // never set in the original either
    alignas(4) char text[0x14];
    At<uint32_t>(text, 4) = 0;
    At<uint32_t>(text, 8) = 0;
    At<uint32_t>(text, 0xC) = 0;
    using RangeFn = void(__thiscall*)(void*, char*, char*, void*);
    reinterpret_cast<RangeFn>(addr::SC4String_FromRange)(text + 4, chars, chars + length, &tag);
    At<uint32_t>(text, 0) = addr::SC4String_Vtable;
    At<uint32_t>(text, 0x10) = 0;

    bool added = false;
    const std::size_t kTwin = field::kEvilTwin - 4; // from the advice
    if (At<uint32_t>(advice, kTwin) != 0) {
        const uint32_t advisorId = Virtual<uint32_t>(advice, 0x2C);
        void* const system = Global<void*>(addr::SC4_Service43D34);
        void* const advisor = system ? Virtual<void*>(system, 0x1C, advisorId) : nullptr;
        void* const twinAdvice = advisor ? Virtual<void*>(advisor, 0x30, At<uint32_t>(advice, kTwin)) : nullptr;
        if (twinAdvice && Virtual<uint8_t>(twinAdvice, 0x20)) {
            const uint32_t twin = At<uint32_t>(advice, kTwin);
            if (twin != 0) {
                added = true;
                alignas(4) char format[0x20];
                using ConstructFn = void*(__thiscall*)(void*, uint32_t, uint32_t, uint32_t);
                reinterpret_cast<ConstructFn>(addr::SC4LocalizedString_SC4LocalizedString)(format, 0x6A231EA4,
                                                                                         0x2C104D8C, 0);
                alignas(4) char line[0x20];
                reinterpret_cast<void(__thiscall*)(void*)>(addr::SC4LocalizedString_ConstructBase)(line);
                At<uint32_t>(line, 0) = addr::SC4LocalizedString_Vtable;
                const char* const formatText =
                    reinterpret_cast<const char*(__thiscall*)(void*)>(addr::cRZBaseString_Data)(format);
                reinterpret_cast<void(__cdecl*)(void*, const char*, ...)>(addr::cRZBaseString_Sprintf)(line, formatText,
                                                                                                  twin);
                using ExpandFn = uint8_t(__thiscall*)(void*, void*, uint32_t);
                reinterpret_cast<ExpandFn>(addr::SC4LocalizedString_Substitute)(line, nullptr, 1);
                alignas(4) char separator[0x14];
                char* const made = reinterpret_cast<char*(__thiscall*)(void*, const char*)>(addr::SC4String_FromText)(
                    separator, "&nbsp;<BR>&nbsp;");
                char* const separatorRange = made ? made + 4 : nullptr;
                using ConcatFn = char*(__cdecl*)(void*, void*, void*);
                alignas(4) char first[0xC];
                alignas(4) char second[0xC];
                char* const withBreak =
                    reinterpret_cast<ConcatFn>(addr::cSC4Advice_F408AE0)(first, text + 4, separatorRange);
                char* const whole = reinterpret_cast<ConcatFn>(addr::cSC4Advice_F408AE0)(second, withBreak, line + 4);
                char* copy[3] = {nullptr, nullptr, nullptr};
                reinterpret_cast<RangeFn>(addr::cSC4Advice_F408420)(copy, At<char*>(whole, 0), At<char*>(whole, 4),
                                                                   &tag);
                char* const copied = copy[0];
                AssignRange(result + 4, copied, copy[1], &tag);
                FreeIfSet(copied);
                FreeIfSet(At<void*>(second, 0));
                FreeIfSet(At<void*>(first, 0));
                void* const separatorBuffer = At<void*>(separator, 4);
                At<uint32_t>(separator, 0) = addr::SC4String_Vtable;
                FreeIfSet(separatorBuffer);
                void* const lineBuffer = At<void*>(line, 4);
                At<uint32_t>(line, 0) = addr::SC4String_Vtable;
                FreeIfSet(lineBuffer);
                void* const formatBuffer = At<void*>(format, 4);
                At<uint32_t>(format, 0) = addr::SC4String_Vtable;
                FreeIfSet(formatBuffer);
            }
        }
    }
    if (!added)
        AssignRange(result + 4, At<char*>(text, 4), At<char*>(text, 8), &tag);
    FreeIfSet(At<void*>(text, 4));
    return result;
}

// The game's 0x004249D0. Slot 22, GetSuccessText.
char* __fastcall SuccessText(char* self, void*)
{
    return ScriptText(self, nullptr, "success_text");
}

// The game's 0x004249E0. Slot 23, GetFailureText.
char* __fastcall FailureText(char* self, void*)
{
    return ScriptText(self, nullptr, "failure_text");
}

// The game's 0x004249F0. Slot 33, GetRewardProgressText.
char* __fastcall RewardProgressText(char* self, void*)
{
    return ScriptText(self, nullptr, "reward_progress_text");
}

// The game's 0x00424A00. Slot 34, GetRewardUnlockedText.
char* __fastcall RewardUnlockedText(char* self, void*)
{
    return ScriptText(self, nullptr, "reward_unlocked_text");
}

// The game's 0x00424A10. A byte through a stream: read (slot 13, by
// pointer) or written (slot 14, by value).
uint8_t __cdecl StreamByte(uint8_t* value, void* stream, uint8_t reading)
{
    if (reading)
        return Virtual<uint8_t>(stream, 0x34, value);
    Virtual<uint8_t>(stream, 0x38, static_cast<uint32_t>(*value));
    return 1;
}

// The game's 0x00424A40. Whether the open script table (context at +8, open
// flag at +0xC) has a number or a string under the name.
uint8_t __fastcall TableHas(char* self, void*, const char* name)
{
    if (!At<uint8_t>(self, 0xC))
        return 0;
    Virtual<void>(At<void*>(self, 8), 0x84, name);        // PushString
    Virtual<void>(At<void*>(self, 8), 0x98, int32_t{-2}); // GetTable
    uint8_t found;
    if (Virtual<int32_t>(At<void*>(self, 8), 0x30, int32_t{-1}) == 3) // Type
        found = 1;
    else if (Virtual<int32_t>(At<void*>(self, 8), 0x30, int32_t{-1}) == 4)
        found = 1;
    else
        found = 0;
    Virtual<void>(At<void*>(self, 8), 0x2C, int32_t{1});
    return found;
}

// The game's 0x00424AB0. The constructor of a small object on the occupant
// filter base with a flag byte at +0xC.
char* __fastcall FilterConstruct(char* self, void*, uint8_t flag)
{
    reinterpret_cast<void*(__thiscall*)(void*)>(addr::SC4_OccupantFilterBase)(self);
    At<uint8_t>(self, 0xC) = flag;
    At<uint32_t>(self, 0) = addr::SC4MySim_ResidenceFilterVtable;
    At<uint32_t>(self, 4) = addr::SC4MySim_ResidenceFilterVtable4;
    return self;
}

bool InstallAdvice2(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[72] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSC4Advice_F423020, reinterpret_cast<void*>(&QueryInterface)},
        {addr::cSC4Advice_F423050, reinterpret_cast<void*>(&AddRef)},
        {addr::cSC4Advice_F423060, reinterpret_cast<void*>(&Release)},
        {addr::cSC4Advice_F423090, reinterpret_cast<void*>(&Shutdown)},
        {addr::cSC4Advice_F4230D0, reinterpret_cast<void*>(&ScriptUint)},
        {addr::cSC4Advice_F423140, reinterpret_cast<void*>(&MaxTargetDistance)},
        {addr::cSC4Advice_F423190, reinterpret_cast<void*>(&IsAutomatonRelevant)},
        {addr::cSC4Advice_F4231F0, reinterpret_cast<void*>(&IsGroupRelevant)},
        {addr::cSC4Advice_F423270, reinterpret_cast<void*>(&ImageId)},
        {addr::cSC4Advice_F423280, reinterpret_cast<void*>(&SuccessImageId)},
        {addr::cSC4Advice_F4232A0, reinterpret_cast<void*>(&RewardUnlockedImageId)},
        {addr::cSC4Advice_F4232C0, reinterpret_cast<void*>(&SuccessMood)},
        {addr::cSC4Advice_F4232E0, reinterpret_cast<void*>(&FailureImageId)},
        {addr::cSC4Advice_F423300, reinterpret_cast<void*>(&FailureMood)},
        {addr::cSC4Advice_F423430, reinterpret_cast<void*>(&CellListPushBack)},
        {addr::cSC4Advice_F423470, reinterpret_cast<void*>(&Construct)},
        {addr::cSC4Advice_F4235F0, reinterpret_cast<void*>(&SuccessCondition)},
        {addr::cSC4Advice_F423600, reinterpret_cast<void*>(&TimeLimit)},
        {addr::cSC4Advice_F423610, reinterpret_cast<void*>(&SuccessDistance)},
        {addr::cSC4Advice_F423620, reinterpret_cast<void*>(&MinTargetDistance)},
        {addr::cSC4Advice_F423630, reinterpret_cast<void*>(&SuccessTimeout)},
        {addr::cSC4Advice_F423640, reinterpret_cast<void*>(&DelayAfterDisabled)},
        {addr::cSC4Advice_F423660, reinterpret_cast<void*>(&CheckSituationFlag)},
        {addr::cSC4Advice_F423680, reinterpret_cast<void*>(&AutomataGroups)},
        {addr::cSC4Advice_F423690, reinterpret_cast<void*>(&TargetGroups)},
        {addr::cSC4Advice_F4236A0, reinterpret_cast<void*>(&TargetSequence)},
        {addr::cSC4Advice_F4236B0, reinterpret_cast<void*>(&HasTargetSequence)},
        {addr::cSC4Advice_F4236C0, reinterpret_cast<void*>(&EvadeDistance)},
        {addr::cSC4Advice_F4236D0, reinterpret_cast<void*>(&EvadeTimeout)},
        {addr::cSC4Advice_F4236E0, reinterpret_cast<void*>(&ActiveRadius)},
        {addr::cSC4Advice_F423730, reinterpret_cast<void*>(&CoverageRect)},
        {addr::cSC4Advice_F423740, reinterpret_cast<void*>(&CellCoverageList)},
        {addr::cSC4Advice_F423760, reinterpret_cast<void*>(&CoverageSuccessPct)},
        {addr::cSC4Advice_F423770, reinterpret_cast<void*>(&SuccessAura)},
        {addr::cSC4Advice_F4237B0, reinterpret_cast<void*>(&SuccessMoney)},
        {addr::cSC4Advice_F4237E0, reinterpret_cast<void*>(&FailureAura)},
        {addr::cSC4Advice_F423820, reinterpret_cast<void*>(&FailureMoney)},
        {addr::cSC4Advice_F423860, reinterpret_cast<void*>(&QueryInterface4)},
        {addr::cSC4Advice_F423870, reinterpret_cast<void*>(&AddRef4)},
        {addr::cSC4Advice_F423880, reinterpret_cast<void*>(&Release4)},
        {addr::cSC4Advice_F423890, reinterpret_cast<void*>(&QueryInterface8)},
        {addr::cSC4Advice_F4238A0, reinterpret_cast<void*>(&AddRef8)},
        {addr::cSC4Advice_F4238B0, reinterpret_cast<void*>(&Release8)},
        {addr::cSC4Advice_F4238C0, reinterpret_cast<void*>(&DeletingDestruct8)},
        {addr::cSC4Advice_F4238D0, reinterpret_cast<void*>(&QueryInterface10)},
        {addr::cSC4Advice_F4238E0, reinterpret_cast<void*>(&AddRef10)},
        {addr::cSC4Advice_F4238F0, reinterpret_cast<void*>(&Release10)},
        {addr::cSC4Advice_F423900, reinterpret_cast<void*>(&HasEvadeList)},
        {addr::cSC4Advice_F423910, reinterpret_cast<void*>(&Destruct)},
        {addr::cSC4Advice_F4239D0, reinterpret_cast<void*>(&Init)},
        {addr::cSC4Advice_F423D20, reinterpret_cast<void*>(&SuccessConditionText)},
        {addr::cSC4Advice_F423DD0, reinterpret_cast<void*>(&Create)},
        {addr::cSC4Advice_F423E00, reinterpret_cast<void*>(&DeletingDestruct)},
        {addr::cSC4Advice_F423E20, reinterpret_cast<void*>(&UpdateSituationFields)},
        {addr::cSC4Advice_F4243B0, reinterpret_cast<void*>(&SuccessEffectName)},
        {addr::cSC4Advice_F424400, reinterpret_cast<void*>(&FailureEffectName)},
        {addr::cSC4Advice_F424450, reinterpret_cast<void*>(&ProgressText)},
        {addr::cSC4Advice_F424600, reinterpret_cast<void*>(&ScriptText)},
        {addr::cSC4Advice_F424750, reinterpret_cast<void*>(&Title)},
        {addr::cSC4Advice_F424760, reinterpret_cast<void*>(&Message)},
        {addr::cSC4Advice_F4249D0, reinterpret_cast<void*>(&SuccessText)},
        {addr::cSC4Advice_F4249E0, reinterpret_cast<void*>(&FailureText)},
        {addr::cSC4Advice_F4249F0, reinterpret_cast<void*>(&RewardProgressText)},
        {addr::cSC4Advice_F424A00, reinterpret_cast<void*>(&RewardUnlockedText)},
        {addr::SC4Script_MySimAge, reinterpret_cast<void*>(&StreamByte)},
        {addr::SC4ScriptTable_Has, reinterpret_cast<void*>(&TableHas)},
        {addr::SC4MySimHelper10_Construct, reinterpret_cast<void*>(&FilterConstruct)},
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

const bool g_advice2Registered = registry::Register({
    "scripts.advisors.advice2",
    "scripts/advisors",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4Advice_F423470,
    "Our city situation advice (the missions' advice class) and its helpers",
    false,
    InstallAdvice2,
});

} // namespace
} // namespace opensc4::advice
