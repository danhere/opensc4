// Our automata controllers, part a3a (build 641): the object (the
// constructors, the destructor bodies, the deleting destructors and the two
// factories; QueryInterface and Release) and the queries (the accessors, the
// flag tests and the small overrides the two derived classes make). The class
// map is in src/core/automata_controller.h and the survey in docs/automata.md,
// "Part a3". Calls between functions of the part go to the game's addresses,
// so each detour stands alone and OPENSC4_AUTOMATA_SKIP (function addresses,
// hex, eight digits) bisects them.
//
// Group, off by default:
//   simulators.automata.controller: the 29 detours listed in
//   kController below.
//
// What it does. cSC4AutomataController is the base of the attractor and the
// generator (both held by the controller manager, parts a1 and a2). The base
// constructor 0x00517D40 fixes the layout (src/core/automata_controller.h):
// two pointers to its vtables, the occupant and the template, the position,
// the reference count, an inline cRZBaseString name (8 bytes from
// SC4::stl_allocate), the flags (2), the automaton list and its count, the
// script system and three floats. The attractor's constructor adds an
// SC4Curve at +0x5C (SC4Curve::Init 0x007D0410); the generator's adds a
// cRZRandom at +0x60 (SC4Random::Construct 0x00913A5C, seed -1) and zeroes
// +0x78..+0x98. QueryInterface answers 1 and 0x6A1857F8 (the controller
// interface) with the object and 0xE4FDA3D4 with the serializable part
// (object + 4), AddRefing through slot 1; a null object writes a null and
// still calls through it (never reached). Release decrements +0x20 and, at
// 1 or below, calls the deleting destructor (slot 0x9C). The queries read
// the template (0x00517CE0 GetTemplate answers the controller's own name
// when there is none, the template's name at +4 otherwise), the template's
// curve (+0x20, or +0xA8 for the generator), the flags, the position (the
// occupant's, unless flag 0x20), and the finished and saturated tests.
//
// Oddities kept: QueryInterface's null branches write a null and then call
// through the object; the generator's IsSaturated tests its limit (+0x8C)
// against +0x88 unsigned after the two float tests fail; the destructors
// free the name's buffer and the attractor's curve but do not run the
// string's or the curve's own destructor; the deleting destructors run the
// shutdown (slot 0x10) before the body. A stale register is never read.
//
// Still the game's, at their addresses: the shared AddRef 0x008AE800; the
// folded helpers 0x0040CE60, 0x00477810, 0x00648F00; the stub 0x005D4A10;
// the adjustor thunks 0x005183A0 (QueryInterface), 0x005183B0 (AddRef) and
// 0x005183C0 (Release), and the plain jump 0x005151F0 (the attractor's and
// generator's slot 0), which reach our detours; SC4PairList::Destroy
// 0x005650A0; SC4Curve::Init 0x007D0410; SC4Random::Construct 0x00913A5C;
// cRZRandom::Between 0x009136CA; SC4::stl_allocate 0x0090CF54, SC4::Free
// 0x0090CF63, SC4::operator_new 0x005E55E0 and SC4::operator_delete
// 0x005E5620; and the constructor flags' and template's own code.
#include "core/automata_controller.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace opensc4::automata_controller {
namespace {

// ====================================================================================
// Helpers

template <typename R, typename... Args>
inline R CallThis(std::uintptr_t address, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(address)(self, args...);
}

template <typename R, typename... Args>
inline R CallCdecl(std::uintptr_t address, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(address)(args...);
}

template <typename T>
inline T& At(void* base, std::size_t offset)
{
    return *reinterpret_cast<T*>(static_cast<char*>(base) + offset);
}

template <typename T>
inline T Global(std::uintptr_t address)
{
    return *reinterpret_cast<const T*>(address);
}

// A virtual call through the object's first vtable, `byteOffset` in bytes.
template <typename R, typename... Args>
inline R Virtual(void* object, std::size_t byteOffset, Args... args)
{
    void** const vtable = *reinterpret_cast<void***>(object);
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(vtable[byteOffset / 4])(object, args...);
}

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("automata controller: first %s", name);
}

void* Allocate(std::uint32_t size) { return CallCdecl<void*>(addr::SC4_stl_allocate, size); }
void Free(void* pointer) { CallCdecl<void>(addr::SC4_Free, pointer); }

float FloatZero() { return Global<float>(addr::SC4_FloatZero); }

void* Template(char* self) { return reinterpret_cast<void*>(At<std::uintptr_t>(self, field::kTemplate)); }

// ====================================================================================
// The object

// 0x00517D40, the base constructor both derived constructors call.
char* __fastcall Construct(char* self, void*)
{
    static bool logged;
    FirstCall("base Construct", logged);
    At<std::uintptr_t>(self, field::kVtable) = addr::cSC4AutomataController_Vtable;
    At<std::uintptr_t>(self, field::kSerializable) = addr::cSC4AutomataController_SerializableVtable;
    At<std::uint8_t>(self, field::kInitialized) = 0;
    At<std::uint32_t>(self, field::kOccupant) = 0;
    At<std::uint32_t>(self, field::kTemplate) = 0;
    const std::uint32_t zero = Global<std::uint32_t>(addr::SC4_ZeroWord);
    At<std::uint32_t>(self, field::kX) = zero;
    At<std::uint32_t>(self, field::kZ) = zero;
    At<std::uint32_t>(self, field::kY) = zero;
    At<std::uint32_t>(self, field::kReferences) = 0;
    At<std::uint8_t>(self, field::kByte24) = 0;
    At<std::uint8_t>(self, field::kByte25) = 0;
    At<std::uint8_t>(self, field::kByte26) = 0;
    At<std::uint8_t>(self, field::kByte27) = 0;
    At<std::uint32_t>(self, field::kNameBegin) = 0;
    At<std::uint32_t>(self, field::kNameEnd) = 0;
    At<std::uint32_t>(self, field::kNameCapacity) = 0;
    char* const name = static_cast<char*>(Allocate(8));
    At<std::uintptr_t>(self, field::kNameBegin) = reinterpret_cast<std::uintptr_t>(name);
    At<std::uintptr_t>(self, field::kNameEnd) = reinterpret_cast<std::uintptr_t>(name);
    At<std::uintptr_t>(self, field::kNameCapacity) = reinterpret_cast<std::uintptr_t>(name + 8);
    name[0] = 0;
    At<std::uintptr_t>(self, field::kName) = addr::SC4String_Vtable;
    At<std::uint32_t>(self, field::kNameReferences) = 0;
    At<std::uint32_t>(self, field::k3C) = 0;
    At<std::uint32_t>(self, field::kFlags) = kConstructorFlags;
    At<std::uint32_t>(self, field::kAutomata) = 0;
    char* const head = static_cast<char*>(Allocate(0xC));
    *reinterpret_cast<char**>(head) = head;
    *reinterpret_cast<char**>(head + 4) = head;
    At<std::uintptr_t>(self, field::kAutomata) = reinterpret_cast<std::uintptr_t>(head);
    At<std::uint32_t>(self, field::kAutomataCount) = 0;
    At<std::uint32_t>(self, field::kScripts) = 0;
    At<std::uint32_t>(self, field::k50) = 0;
    At<std::uint32_t>(self, field::k54) = 0;
    At<std::uint32_t>(self, field::k58) = 0;
    return self;
}

// 0x005181F0, the attractor's constructor: the base's, its vtables, then its
// SC4Curve at +0x5C.
char* __fastcall AttractorConstruct(char* self, void*)
{
    static bool logged;
    FirstCall("attractor Construct", logged);
    CallThis<char*>(addr::cSC4AutomataController_Construct, self);
    At<std::uintptr_t>(self, field::kVtable) = addr::cSC4AutomataAttractor_Vtable;
    At<std::uintptr_t>(self, field::kSerializable) = addr::cSC4AutomataAttractor_SerializableVtable;
    CallThis<void*>(addr::SC4Curve_Init, self + field::kCurve);
    return self;
}

// 0x005182B0, the generator's constructor: the base's, its vtables, its
// cRZRandom at +0x60 (seed -1), then +0x78..+0x98 zeroed.
char* __fastcall GeneratorConstruct(char* self, void*)
{
    static bool logged;
    FirstCall("generator Construct", logged);
    CallThis<char*>(addr::cSC4AutomataController_Construct, self);
    At<std::uintptr_t>(self, field::kVtable) = addr::cSC4AutomataGenerator_Vtable;
    At<std::uintptr_t>(self, field::kSerializable) = addr::cSC4AutomataGenerator_SerializableVtable;
    CallThis<void>(addr::SC4Random_Construct, self + field::kRandom, static_cast<std::uint32_t>(0xFFFFFFFF));
    At<std::uint32_t>(self, field::kGen78) = 0;
    At<std::uint16_t>(self, field::kGen7C) = 0;
    At<std::uint32_t>(self, field::kGen80) = 0;
    At<std::uint16_t>(self, field::kGen84) = 0;
    At<std::uint32_t>(self, field::kGen88) = 0;
    At<std::uint32_t>(self, field::kGen8C) = 0;
    At<std::uint32_t>(self, field::kGen90) = 0;
    At<std::uint32_t>(self, field::kGen94) = 0;
    At<std::uint32_t>(self, field::kGen98) = 0;
    return self;
}

// 0x00514860: 0x6C bytes from SC4::operator_new, then the constructor; the
// game tail-jumps, ours calls.
void* __cdecl AttractorCreate()
{
    static bool logged;
    FirstCall("attractor Create", logged);
    char* const self = static_cast<char*>(CallCdecl<void*>(addr::SC4_operator_new, static_cast<std::size_t>(0x6C)));
    if (!self)
        return nullptr;
    return CallThis<char*>(addr::cSC4AutomataAttractor_Construct, self);
}

// 0x00514880: 0xA0 bytes, then the constructor.
void* __cdecl GeneratorCreate()
{
    static bool logged;
    FirstCall("generator Create", logged);
    char* const self = static_cast<char*>(CallCdecl<void*>(addr::SC4_operator_new, static_cast<std::size_t>(0xA0)));
    if (!self)
        return nullptr;
    return CallThis<char*>(addr::cSC4AutomataGenerator_Construct, self);
}

// 0x00514F60, slot 0 (iid, out), ret 8.
std::uint8_t __fastcall QueryInterface(char* self, void*, std::uint32_t iid, void** out)
{
    static bool logged;
    FirstCall("QueryInterface", logged);
    char* volatile object = self; // the null branch calls through it, as the game
    if (iid == kIidUnknown || iid == kIidAutomataController) {
        *out = object;
        Virtual<std::uint32_t>(object, slot::kAddRef);
        return 1;
    }
    if (iid == kIidSerializable) {
        *out = object ? object + field::kSerializable : nullptr;
        Virtual<std::uint32_t>(object, slot::kAddRef);
        return 1;
    }
    return 0;
}

// 0x00517F10, slot 2.
std::uint32_t __fastcall Release(char* self, void*)
{
    static bool logged;
    FirstCall("Release", logged);
    std::uint32_t references = At<std::uint32_t>(self, field::kReferences);
    if (references > 1) {
        --references;
        At<std::uint32_t>(self, field::kReferences) = references;
        return references;
    }
    Virtual<void>(self, slot::kDeletingDestructor);
    return 0;
}

// 0x00518220, the attractor's slot 5.
std::uint32_t __fastcall AttractorGetGZCLSID(void*, void*)
{
    static bool logged;
    FirstCall("attractor GetGZCLSID", logged);
    return kAttractorClassId;
}

// 0x00518390, the generator's slot 5.
std::uint32_t __fastcall GeneratorGetGZCLSID(void*, void*)
{
    static bool logged;
    FirstCall("generator GetGZCLSID", logged);
    return kGeneratorClassId;
}

// 0x00517F80, the base destructor body: the vtables, the script system, the
// automaton list, the name's buffer and the occupant. The string's and the
// curve's own destructors are not run, as in the game.
void __fastcall Destroy(char* self, void*)
{
    static bool logged;
    FirstCall("Destroy", logged);
    At<std::uintptr_t>(self, field::kVtable) = addr::cSC4AutomataController_Vtable;
    At<std::uintptr_t>(self, field::kSerializable) = addr::cSC4AutomataController_SerializableVtable;
    if (void* const scripts = reinterpret_cast<void*>(At<std::uintptr_t>(self, field::kScripts)))
        Virtual<std::uint32_t>(scripts, slot::kRelease);
    CallThis<void>(addr::SC4PairList_Destroy, self + field::kAutomata);
    At<std::uintptr_t>(self, field::kName) = addr::SC4String_Vtable;
    if (void* const name = reinterpret_cast<void*>(At<std::uintptr_t>(self, field::kNameBegin)))
        Free(name);
    if (void* const occupant = reinterpret_cast<void*>(At<std::uintptr_t>(self, field::kOccupant)))
        Virtual<std::uint32_t>(occupant, slot::kRelease);
}

// 0x005192E0, the attractor's destructor body: its curve first, then the base
// body.
void __fastcall AttractorDestroy(char* self, void*)
{
    static bool logged;
    FirstCall("attractor Destroy", logged);
    if (void* const curve = reinterpret_cast<void*>(At<std::uintptr_t>(self, field::kCurve)))
        Free(curve);
    Destroy(self, nullptr);
}

// 0x005192C0, the attractor's deleting destructor (slot 0x9C): Shutdown
// (slot 4), the body, SC4::operator_delete.
void __fastcall AttractorDeletingDestructor(char* self, void*)
{
    static bool logged;
    FirstCall("attractor DeletingDestructor", logged);
    Virtual<void>(self, slot::kShutdown);
    AttractorDestroy(self, nullptr);
    CallCdecl<void>(addr::SC4_operator_delete, self);
}

// 0x00519340, the base's and the generator's deleting destructor.
void __fastcall DeletingDestructor(char* self, void*)
{
    static bool logged;
    FirstCall("DeletingDestructor", logged);
    Virtual<void>(self, slot::kShutdown);
    Destroy(self, nullptr);
    CallCdecl<void>(addr::SC4_operator_delete, self);
}

// ====================================================================================
// The queries

// 0x00517F70, slot 6.
void* __fastcall GetOccupant(char* self, void*)
{
    return reinterpret_cast<void*>(At<std::uintptr_t>(self, field::kOccupant));
}

// 0x00517DF0, slot 7: the template's name (template + 4), or the controller's
// own name when there is no template.
void* __fastcall GetTemplate(char* self, void*)
{
    void* const tmpl = Template(self);
    return tmpl ? static_cast<char*>(tmpl) + 4 : self + field::kName;
}

// 0x00517E00, slot 9 (base and attractor): the template's +0x20 as a float,
// else the game's zero.
float __fastcall GetTemplateCurve(char* self, void*)
{
    void* const tmpl = Template(self);
    return tmpl ? At<float>(tmpl, 0x20) : FloatZero();
}

// 0x00518310, slot 9 (generator): the template's +0xA8.
float __fastcall GeneratorGetTemplateCurve(char* self, void*)
{
    void* const tmpl = Template(self);
    return tmpl ? At<float>(tmpl, 0xA8) : FloatZero();
}

// 0x00517E60, slot 12 (mask), ret 4: (flags & mask) != 0, as 0 or 1 in EAX.
std::uint32_t __fastcall TestFlag(char* self, void*, std::uint32_t mask)
{
    return (At<std::uint32_t>(self, field::kFlags) & mask) != 0 ? 1u : 0u;
}

// 0x00517E70, slot 13 (mask, value), ret 8.
void __fastcall SetFlag(char* self, void*, std::uint32_t mask, std::uint8_t value)
{
    if (value == 1)
        At<std::uint32_t>(self, field::kFlags) |= mask;
    else
        At<std::uint32_t>(self, field::kFlags) &= ~mask;
}

// 0x00517F40, slot 14 (occupant), ret 4: the usual reference swap on +0x0C.
void __fastcall SetOccupant(char* self, void*, void* occupant)
{
    void* const old = reinterpret_cast<void*>(At<std::uintptr_t>(self, field::kOccupant));
    if (occupant == old)
        return;
    if (occupant)
        Virtual<std::uint32_t>(occupant, slot::kAddRef);
    At<std::uintptr_t>(self, field::kOccupant) = reinterpret_cast<std::uintptr_t>(occupant);
    if (old)
        Virtual<std::uint32_t>(old, slot::kRelease);
}

// The name-clearing and template-copying common to the two SetTemplate
// functions.
void SetTemplateCommon(char* self, void* tmpl)
{
    At<std::uintptr_t>(self, field::kTemplate) = reinterpret_cast<std::uintptr_t>(tmpl);
    if (!tmpl)
        return;
    const std::uintptr_t begin = At<std::uintptr_t>(self, field::kNameBegin);
    if (begin != At<std::uintptr_t>(self, field::kNameEnd)) {
        *reinterpret_cast<char*>(begin) = 0;
        At<std::uintptr_t>(self, field::kNameEnd) = begin;
    }
    At<std::uint32_t>(self, field::k54) = At<std::uint32_t>(tmpl, tmpl::kSaturationLimit);
    At<std::uint32_t>(self, field::kFlags) = At<std::uint32_t>(tmpl, tmpl::kFlags);
}

// 0x00517430, slot 15 (base and attractor), ret 4.
void __fastcall SetTemplate(char* self, void*, void* tmpl)
{
    static bool logged;
    FirstCall("SetTemplate", logged);
    SetTemplateCommon(self, tmpl);
}

// 0x00517460, slot 15 (generator): the base's, then, for a generator
// template, its limit (+0xAC to +0x8C) and a fresh random (+0x98).
void __fastcall GeneratorSetTemplate(char* self, void*, void* tmpl)
{
    static bool logged;
    FirstCall("generator SetTemplate", logged);
    SetTemplateCommon(self, tmpl);
    void* const t = Template(self);
    if (!t || At<std::uint32_t>(t, tmpl::kClassId) != kGeneratorClassId)
        return;
    At<std::uint32_t>(self, field::kGen8C) = At<std::uint32_t>(t, tmpl::kLimit);
    const std::uint32_t random = CallThis<std::uint32_t>(addr::cRZRandom_Between, self + field::kRandom,
                                                         At<std::uint32_t>(t, tmpl::kRandomLow),
                                                         At<std::uint32_t>(t, tmpl::kRandomHigh));
    At<std::uint32_t>(self, field::kGen98) = random;
}

// 0x00517E20, slot 10: flags 1, 2 and 4 set and flag 0x400 clear, each
// through slot 0x30.
std::uint8_t __fastcall IsActive(char* self, void*)
{
    return Virtual<std::uint8_t>(self, slot::kTestFlag, kFlag1) &&
           Virtual<std::uint8_t>(self, slot::kTestFlag, kFlag2) &&
           Virtual<std::uint8_t>(self, slot::kTestFlag, kFlag4) &&
           !Virtual<std::uint8_t>(self, slot::kTestFlag, kFlag400);
}

// 0x00515A40, slot 11 (out), ret 4: the occupant's own position when it has
// one and flag 0x20 is clear, else +0x14, +0x18, +0x1C.
void __fastcall GetPosition(char* self, void*, float* out)
{
    void* const occupant = reinterpret_cast<void*>(At<std::uintptr_t>(self, field::kOccupant));
    if (occupant && !Virtual<std::uint8_t>(self, slot::kTestFlag, kFlag20)) {
        Virtual<void>(occupant, slot::kGetPosition, out);
        return;
    }
    out[0] = At<float>(self, field::kX);
    out[1] = At<float>(self, field::kZ);
    out[2] = At<float>(self, field::kY);
}

// 0x00517EA0, slot 35 (base and attractor): 0 < +0x54 <= +0x50.
std::uint32_t __fastcall IsSaturated(char* self, void*)
{
    const float limit = At<float>(self, field::k54);
    const float bound = At<float>(self, field::k50);
    return (FloatZero() < limit && limit <= bound) ? 1u : 0u;
}

// 0x00518350, slot 35 (generator): the base's test, else its limit +0x8C
// against +0x88 (unsigned).
std::uint32_t __fastcall GeneratorIsSaturated(char* self, void*)
{
    const float limit = At<float>(self, field::k54);
    const float bound = At<float>(self, field::k50);
    if (FloatZero() < limit && limit <= bound)
        return 1;
    const std::uint32_t cap = At<std::uint32_t>(self, field::kGen8C);
    if (cap != 0)
        return At<std::uint32_t>(self, field::kGen88) >= cap ? 1u : 0u;
    return 0;
}

// 0x00517ED0, slot 33: the template's +0x54 against the automaton count.
std::uint32_t __fastcall IsFinished(char* self, void*)
{
    void* const tmpl = Template(self);
    if (!tmpl)
        return 0;
    const std::uint32_t limit = At<std::uint32_t>(tmpl, 0x54);
    if (!limit)
        return 0;
    return At<std::uint32_t>(self, field::kAutomataCount) >= limit ? 1u : 0u;
}

// 0x00518330 (the generator's slot 37): slot 0x30 with flag 1.
std::uint8_t __fastcall GeneratorCallTestFlag1(char* self, void*)
{
    return Virtual<std::uint8_t>(self, slot::kTestFlag, kFlag1);
}

// 0x00518340 (the generator's slot 40): +0x8C = value.
void __fastcall GeneratorSetLimit(char* self, void*, std::uint32_t value)
{
    At<std::uint32_t>(self, field::kGen8C) = value;
}

// ====================================================================================
// Installation

bool Skipped(std::uintptr_t address)
{
    char list[1024] = {};
    if (!GetEnvironmentVariableA("OPENSC4_AUTOMATA_SKIP", list, sizeof list))
        return false;
    char wanted[16];
    std::snprintf(wanted, sizeof wanted, "%08X", static_cast<unsigned>(address));
    return std::strstr(list, wanted) != nullptr;
}

struct Detour {
    std::uintptr_t address;
    void* ours;
};

bool InstallList(const Detour* detours, std::size_t count, const char* what)
{
    if (!game::IsBuild641())
        return false;
    for (std::size_t i = 0; i < count; ++i)
        if (!addr_check::Find(detours[i].address)) {
            log::Write("automata controller %s: no address entry for 0x%08X", what,
                       static_cast<unsigned>(detours[i].address));
            return false;
        }
    unsigned installed = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (Skipped(detours[i].address))
            continue;
        void* original = nullptr;
        if (!hook::Install(*addr_check::Find(detours[i].address), detours[i].ours, &original))
            return false;
        ++installed;
    }
    log::Write("automata controller %s: %u detours", what, installed);
    return true;
}

#define OPENSC4_DETOUR(address, fn) {address, reinterpret_cast<void*>(&fn)}

const Detour kController[] = {
    OPENSC4_DETOUR(addr::cSC4AutomataController_Construct, Construct),
    OPENSC4_DETOUR(addr::cSC4AutomataAttractor_Construct, AttractorConstruct),
    OPENSC4_DETOUR(addr::cSC4AutomataGenerator_Construct, GeneratorConstruct),
    OPENSC4_DETOUR(addr::cSC4AutomataAttractor_Create, AttractorCreate),
    OPENSC4_DETOUR(addr::cSC4AutomataGenerator_Create, GeneratorCreate),
    OPENSC4_DETOUR(addr::cSC4AutomataController_QueryInterface, QueryInterface),
    OPENSC4_DETOUR(addr::cSC4AutomataController_Release, Release),
    OPENSC4_DETOUR(addr::cSC4AutomataAttractor_GetGZCLSID, AttractorGetGZCLSID),
    OPENSC4_DETOUR(addr::cSC4AutomataGenerator_GetGZCLSID, GeneratorGetGZCLSID),
    OPENSC4_DETOUR(addr::cSC4AutomataController_Destroy, Destroy),
    OPENSC4_DETOUR(addr::cSC4AutomataAttractor_Destroy, AttractorDestroy),
    OPENSC4_DETOUR(addr::cSC4AutomataAttractor_DeletingDestructor, AttractorDeletingDestructor),
    OPENSC4_DETOUR(addr::cSC4AutomataController_DeletingDestructor, DeletingDestructor),
    OPENSC4_DETOUR(addr::cSC4AutomataController_GetOccupant, GetOccupant),
    OPENSC4_DETOUR(addr::cSC4AutomataController_GetTemplate, GetTemplate),
    OPENSC4_DETOUR(addr::cSC4AutomataController_GetTemplateCurve, GetTemplateCurve),
    OPENSC4_DETOUR(addr::cSC4AutomataGenerator_GetTemplateCurve, GeneratorGetTemplateCurve),
    OPENSC4_DETOUR(addr::cSC4AutomataController_TestFlag, TestFlag),
    OPENSC4_DETOUR(addr::cSC4AutomataController_SetFlag, SetFlag),
    OPENSC4_DETOUR(addr::cSC4AutomataController_SetOccupant, SetOccupant),
    OPENSC4_DETOUR(addr::cSC4AutomataController_SetTemplate, SetTemplate),
    OPENSC4_DETOUR(addr::cSC4AutomataGenerator_SetTemplate, GeneratorSetTemplate),
    OPENSC4_DETOUR(addr::cSC4AutomataController_IsActive, IsActive),
    OPENSC4_DETOUR(addr::cSC4AutomataController_GetPosition, GetPosition),
    OPENSC4_DETOUR(addr::cSC4AutomataController_IsSaturated, IsSaturated),
    OPENSC4_DETOUR(addr::cSC4AutomataGenerator_IsSaturated, GeneratorIsSaturated),
    OPENSC4_DETOUR(addr::cSC4AutomataController_IsFinished, IsFinished),
    OPENSC4_DETOUR(addr::cSC4AutomataGenerator_CallTestFlag1, GeneratorCallTestFlag1),
    OPENSC4_DETOUR(addr::cSC4AutomataGenerator_SetLimit, GeneratorSetLimit),
};

#undef OPENSC4_DETOUR

bool InstallController(registry::Mode)
{
    return InstallList(kController, sizeof kController / sizeof kController[0], "controller");
}

const bool g_registered[] = {
    registry::Register({"simulators.automata.controller", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4AutomataController_Construct,
                        "Our automata controllers: the object (the constructors, the destructor bodies, the deleting "
                        "destructors and the two factories; QueryInterface and Release) and the queries (the "
                        "accessors, the flag tests, IsActive, IsFinished, IsSaturated and GetPosition) of "
                        "cSC4AutomataController, cSC4AutomataAttractor and cSC4AutomataGenerator (29 detours; "
                        "src/core/automata_controller.cpp)",
                        false, InstallController}),
};

} // namespace
} // namespace opensc4::automata_controller
