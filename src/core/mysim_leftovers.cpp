// The small functions left over in the MySim classes (docs/mysim-agents.md).
// Replacement, off by default:
//
//   simulators.mysim_agents.leftovers:
//     cSC4MySim: SetSubject (0x00425A80, registers), ResidenceScreenPoint
//       (0x00425030, registers), the script callback for a MySim's name
//       (0x00424E30);
//     cSC4MySimDispatch: ScriptEvent (0x00432860), HasFlag (0x00431260),
//       the three place getters (0x00431290, 0x004312A0, 0x004312B0),
//       SC4MySimPlace::SetWork (0x004311E0) and the class factory Create
//       (0x00434620);
//     the MySim tools' small functions: the two grade texts (0x00437A30,
//       0x00437A80).
//
// The tiny functions are called from code that may keep ECX and EDX across
// the call, so their thunks keep both. The two register-convention
// functions get thunks that move their registers to the stack. The grade
// text functions take their float on the stack (cdecl).
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace opensc4::mysim_leftovers {
namespace {

namespace field { // cSC4MySimDispatch
constexpr std::size_t kFlags = 0x48;
constexpr std::size_t kHome = 0x84;
constexpr std::size_t kTarget = 0xAC;
constexpr std::size_t kWork = 0xD4;
constexpr std::size_t kVehicle = 0x118;
constexpr std::size_t kModels = 0x150; // map: kind -> model key (node value at +0x20)
} // namespace field

constexpr uint32_t kIidAutomaton = 0xA9B40F05;
constexpr uint32_t kLocationClass = 0x6A26D049;
constexpr uint32_t kLocationInterface = 0x6A26D050;
constexpr std::size_t kNodeIndex = 0x20;

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
    if (object)
        Virtual<uint32_t>(object, 0x8);
}

// The runtime's float to integer conversion (0x009EEF04): truncation, the
// low dword of its int64.
int32_t Truncate(double value)
{
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

// --- cSC4MySim --------------------------------------------------------------------------

// The game's 0x00425A80: the script in ESI, the name in EAX, the subject on
// the stack (the caller pops it). Makes the script global `name` a location
// object (class 0x6A26D049) holding the subject: the script's current
// value when there is one, else a new object from the class factory. The
// script is the cSCLua script part (vtable 0x00AC4148), the object the
// first slot of the advisor system (service 0x00B43D34) is asked for gives
// the table kind.
void __cdecl SetSubject(void* script, const char* name, uint32_t subject)
{
    void* location = nullptr;
    Virtual<void>(script, 0x84, name);
    Virtual<void>(script, 0x9C, int32_t{-2});
    if (!Virtual<uint32_t>(script, 0x6C, int32_t{-1})) {
        Virtual<void>(script, 0x84, name);
        void* const com = Cdecl<void*>(addr::RZGetCOM);
        // The original releases an earlier reference here; it is null at
        // this point.
        Virtual<uint8_t>(com, 0xC, kLocationClass, kLocationInterface, &location);
        AddRef(location); // faults on a failed lookup, as the original does
        const uint32_t kind = Virtual<uint32_t>(Global<void*>(addr::SC4_Service43D34), 0x58, uint32_t{0});
        Virtual<void>(script, 0x8C, location, kind);
        Virtual<void>(script, 0xBC, int32_t{-4});
    } else {
        void* const before = location;
        void* const found = Virtual<void*>(script, 0x70, int32_t{-1});
        if (found != before) {
            if (found)
                AddRef(found);
            location = found;
            Release(before);
        }
    }
    Virtual<void>(script, 0x2C, uint32_t{1});
    if (location) {
        Virtual<void>(location, 0x1C, subject);
        Release(location);
    }
}

__declspec(naked) void SetSubjectThunk()
{
    __asm push dword ptr [esp + 4]
    __asm push eax
    __asm push esi
    __asm call SetSubject
    __asm add esp, 12
    __asm ret
}

// The game's 0x00425030: the residence in ECX, one output in EDI and the
// other on the stack (the caller pops it). Takes the residence's bounds,
// turns both corners into view positions and gives the middle (stack
// output) and the left edge less 5 of the corner that is higher up
// (EDI output); the value of the second corner when the first is not
// above it, NaN included.
void __cdecl ResidenceScreenPoint(void* residence, int32_t* leftEdge, int32_t* middle)
{
    void* const view = Global<void*>(addr::SC4_CameraView);
    float low[3];
    float high[3];
    void* const occupant = Virtual<void*>(residence, 0xC);
    Virtual<void>(occupant, 0x28, low, high);
    This<void>(addr::SC4CameraView_WorldToView, view, low);
    This<void>(addr::SC4CameraView_WorldToView, view, high);
    *middle = Truncate((static_cast<double>(high[0]) + low[0]) * Global<float>(addr::SC4_FloatHalf));
    if (low[1] < high[1])
        *leftEdge = Truncate(static_cast<double>(low[0]) - Global<float>(addr::SC4_FloatFive));
    else
        *leftEdge = Truncate(static_cast<double>(high[0]) - Global<float>(addr::SC4_FloatFive));
}

__declspec(naked) void ResidenceScreenPointThunk()
{
    __asm push dword ptr [esp + 4]
    __asm push edi
    __asm push ecx
    __asm call ResidenceScreenPoint
    __asm add esp, 12
    __asm ret
}

// The game's 0x00424E30, a script variable callback (value, the script,
// "reading", the context, all pushed by the script glue; cdecl). It asks
// the context whether the script holds a name at the value and, when it
// does and the script is being written, tells the script to go on (slot
// 0x2C with 1). Always true.
uint8_t __cdecl MySimName(void* value, void* script, uint8_t reading, void* context)
{
    const uint32_t index = Virtual<uint32_t>(context, 0x20, value);
    if (Virtual<uint32_t>(context, 0x48, index) && !reading)
        Virtual<void>(script, 0x2C, uint32_t{1});
    return 1;
}

// --- cSC4MySimDispatch ------------------------------------------------------------------

// The game's 0x00432860 (this, kind, value, notify; ret 0xC): stores the
// value in the model map's node for the kind (false when there is none),
// and with `notify` hands it to the vehicle's automaton interface
// (0xA9B40F05) slot 0x4C; its answer is the result. True when there is no
// vehicle or no interface.
uint8_t __fastcall ScriptEvent(char* self, void*, int32_t kind, int32_t value, uint8_t notify)
{
    void* node;
    This<void>(addr::SC4Sint32Map_Find, self + field::kModels, &node, &kind);
    if (node == At<void*>(self, field::kModels))
        return 0;
    At<int32_t>(node, kNodeIndex) = value;
    if (!notify)
        return 1;
    void* const vehicle = At<void*>(self, field::kVehicle);
    if (!vehicle)
        return 1;
    void* automaton = nullptr;
    if (!Virtual<uint8_t>(vehicle, 0x0, kIidAutomaton, &automaton)) {
        Release(automaton);
        return 1;
    }
    const uint8_t result = Virtual<uint8_t>(automaton, 0x4C, value);
    Release(automaton);
    return result;
}

uint32_t __fastcall HasFlag(char* self, void*, uint32_t flag)
{
    return (At<uint32_t>(self, field::kFlags) & flag) != 0 ? 1 : 0;
}

char* __fastcall TargetPlace(char* self, void*)
{
    return self + field::kTarget;
}

char* __fastcall HomePlace(char* self, void*)
{
    return self + field::kHome;
}

char* __fastcall WorkPlace(char* self, void*)
{
    return self + field::kWork;
}

// SC4MySimPlace::SetWork (0x004311E0): the "work" byte at +0x1D, and AL is
// left 1 (nothing reads it).
uint8_t __fastcall SetWork(char* self, void*)
{
    At<uint8_t>(self, 0x1D) = 1;
    return 1;
}

// Thunks that keep ECX and EDX, for the callers that rely on them.
__declspec(naked) void HasFlagThunk()
{
    __asm push ecx
    __asm push edx
    __asm push dword ptr [esp + 12]
    __asm call HasFlag
    __asm pop edx
    __asm pop ecx
    __asm ret 4
}

__declspec(naked) void TargetPlaceThunk()
{
    __asm push ecx
    __asm push edx
    __asm call TargetPlace
    __asm pop edx
    __asm pop ecx
    __asm ret
}

__declspec(naked) void HomePlaceThunk()
{
    __asm push ecx
    __asm push edx
    __asm call HomePlace
    __asm pop edx
    __asm pop ecx
    __asm ret
}

__declspec(naked) void WorkPlaceThunk()
{
    __asm push ecx
    __asm push edx
    __asm call WorkPlace
    __asm pop edx
    __asm pop ecx
    __asm ret
}

__declspec(naked) void SetWorkThunk()
{
    __asm push ecx
    __asm push edx
    __asm call SetWork
    __asm pop edx
    __asm pop ecx
    __asm ret
}

// The class factory registered for the dispatch class (0x00460D97): an
// object of 0x18C bytes made with a null owner; the pointer to its
// serializable part (+0xC), null when the allocation fails.
void* __cdecl Create()
{
    void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x18C});
    if (!memory)
        return nullptr;
    char* const dispatch = This<char*>(addr::cSC4MySimDispatch_cSC4MySimDispatch, memory, static_cast<void*>(nullptr));
    return dispatch ? dispatch + 0xC : nullptr;
}

// --- The MySim tools' small functions ---------------------------------------------------

// The grade texts: the localized text of group 0xEA231E96 whose instance
// depends on the value (50.0 and 100.0 are the limits, NaN counts as the
// top). The float is on the stack (cdecl).
void LoadGrade(float value, void* text, uint32_t below50, uint32_t below100, uint32_t top)
{
    const uint32_t instance = value < 50.0f ? below50 : value < 100.0f ? below100 : top;
    Cdecl<void>(addr::SC4_LoadLocalizedText, text, uint32_t{0xEA231E96}, instance, uint32_t{0});
}

void __cdecl FirstGradeText(float value, void* text)
{
    LoadGrade(value, text, 0xEA5B8C93, 0xEA5B8C94, 0xEA5B8C95);
}

void __cdecl SecondGradeText(float value, void* text)
{
    LoadGrade(value, text, 0xEA5B8C90, 0xEA5B8C91, 0xEA5B8C92);
}

bool InstallLeftovers(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[20] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSC4MySim_SetSubject, reinterpret_cast<void*>(&SetSubjectThunk)},
        {addr::cSC4MySim_ResidenceScreenPoint, reinterpret_cast<void*>(&ResidenceScreenPointThunk)},
        {addr::SC4Script_MySimName, reinterpret_cast<void*>(&MySimName)},
        {addr::cSC4MySimDispatch_ScriptEvent, reinterpret_cast<void*>(&ScriptEvent)},
        {addr::cSC4MySimDispatch_HasFlag, reinterpret_cast<void*>(&HasFlagThunk)},
        {addr::cSC4MySimDispatch_GetTargetPlace, reinterpret_cast<void*>(&TargetPlaceThunk)},
        {addr::cSC4MySimDispatch_GetHomePlace, reinterpret_cast<void*>(&HomePlaceThunk)},
        {addr::cSC4MySimDispatch_GetWorkPlace, reinterpret_cast<void*>(&WorkPlaceThunk)},
        {addr::SC4MySimPlace_SetWork, reinterpret_cast<void*>(&SetWorkThunk)},
        {addr::cSC4MySimDispatch_Create, reinterpret_cast<void*>(&Create)},
        {addr::SC4UI_FirstGradeText, reinterpret_cast<void*>(&FirstGradeText)},
        {addr::SC4UI_SecondGradeText, reinterpret_cast<void*>(&SecondGradeText)},
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

const bool g_leftoversRegistered = registry::Register({
    "simulators.mysim_agents.leftovers",
    "simulators/mysim_agents",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4MySim_SetSubject,
    "Our small MySim leftovers: script subject, residence point, name callback, dispatch getters, grade texts (docs/mysim-agents.md)",
    false,
    InstallLeftovers,
});

} // namespace
} // namespace opensc4::mysim_leftovers
