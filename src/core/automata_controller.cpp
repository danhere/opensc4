// Our automata controllers, parts a3a and a3b (build 641): the object (the
// constructors, the destructor bodies, the deleting destructors and the two
// factories; QueryInterface and Release), the queries (the accessors, the
// flag tests and the small overrides the two derived classes make), CurveValue,
// and the +4 serializable part's identity (a3a); and the behaviour: Init,
// Shutdown, SetActive, the base slots the runs reach, the automaton events and
// the tick (a3b, its own group below). The class
// map is in src/core/automata_controller.h and the survey in docs/automata.md,
// "Part a3". Calls between functions of the part go to the game's addresses,
// so each detour stands alone and OPENSC4_AUTOMATA_SKIP (function addresses,
// hex, eight digits) bisects them.
//
// Groups, off by default:
//   simulators.automata.controller: the 32 detours listed in
//   kController below (part a3a).
//   simulators.automata.ctlbehaviour: the 19 detours listed in
//   kBehaviour below (part a3b).
//   simulators.automata.ctlserial and simulators.automata.ctlspawn: the 6
//   and 6 detours of kSerial and kSpawn (part a3c).
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
// The +4 serializable vtable (6 slots, not the survey's 4) has its own
// QueryInterface 0x00517F30 (ecx - 4 then the controller's), AddRef and
// Release adjustors, Write 0x00517FD0 with the attractor's 0x00518230 and
// the generator's 0x005183D0 overrides and Read (the save, part a3c below)
// and GetGZCLSID 0x00517F00 (0x4A18672B; the derived classes reuse
// their slot 5).
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
#include "core/automata_ctlmgr.h"
#include "core/fixed_seed.h"
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

#include <bit>
#include <cmath>

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
    At<std::uintptr_t>(self, field::kSerializable) = addr::SC4SimGrid_VtableBase;
    At<std::uintptr_t>(self, field::kVtable) = addr::cSC4AutomataController_Vtable;
    At<std::uintptr_t>(self, field::kSerializable) = addr::cSC4AutomataController_SerializableVtable;
    At<std::uint8_t>(self, field::kInitialized) = 0;
    At<std::uint32_t>(self, field::kOccupant) = 0;
    At<std::uint32_t>(self, field::kTemplate) = 0;
    const std::uint32_t zero = Global<std::uint32_t>(addr::SC4_ZeroWord);
    At<std::uint32_t>(self, field::kX) = zero;
    At<std::uint32_t>(self, field::kHeight) = zero;
    At<std::uint32_t>(self, field::kZ) = zero;
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
    {
        // The clock seed is counted under the original's call site (the
        // return address after its cRZRandom call), not ours: part a3c's
        // spawn draws from this generator.
        const fixed_seed::SiteOverride site(0x005182CF);
        CallThis<void>(addr::SC4Random_Construct, self + field::kRandom, static_cast<std::uint32_t>(0xFFFFFFFF));
    }
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

// 0x00517F30, the +4 serializable vtable's slot 0: ecx - 4, then the
// controller's QueryInterface. The game jumps; ours calls at the address so
// the two detours still stand alone.
std::uint8_t __fastcall SerializableQueryInterface(char* self, void*, std::uint32_t iid, void** out)
{
    static bool logged;
    FirstCall("serializable QueryInterface", logged);
    return CallThis<std::uint8_t>(addr::cSC4AutomataController_QueryInterface, self - field::kSerializable, iid, out);
}

// 0x00517F00, the +4 serializable vtable's slot 5 (base): the class ID the
// save records. The derived classes' serializable vtables use their slot 5.
std::uint32_t __fastcall SerializableGetGZCLSID(void*, void*)
{
    static bool logged;
    FirstCall("serializable GetGZCLSID", logged);
    return kControllerSaveClassId;
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
    CallThis<void>(addr::cSC4AutomataController_Destroy, self);
}

// 0x005192C0, the attractor's deleting destructor (slot 0x9C): Shutdown
// (slot 4), the body, SC4::operator_delete.
void __fastcall AttractorDeletingDestructor(char* self, void*)
{
    static bool logged;
    FirstCall("attractor DeletingDestructor", logged);
    Virtual<void>(self, slot::kShutdown);
    CallThis<void>(addr::cSC4AutomataAttractor_Destroy, self);
    CallCdecl<void>(addr::SC4_operator_delete, self);
}

// 0x00519340, the base's and the generator's deleting destructor.
void __fastcall DeletingDestructor(char* self, void*)
{
    static bool logged;
    FirstCall("DeletingDestructor", logged);
    Virtual<void>(self, slot::kShutdown);
    CallThis<void>(addr::cSC4AutomataController_Destroy, self);
    CallCdecl<void>(addr::SC4_operator_delete, self);
}

// ====================================================================================
// The queries

// 0x00514C70, not a slot: +0x28 times 1000 while above the game's zero, else
// -1.0. The answer stays in ST0 unrounded, so ours answers a double.
double __fastcall CurveValue(char* self, void*)
{
    static bool logged;
    FirstCall("CurveValue", logged);
    const float value = At<float>(self, 0x28);
    if (FloatZero() < value)
        return static_cast<double>(value) * static_cast<double>(Global<float>(addr::SC4_Float1000));
    return static_cast<double>(Global<float>(addr::cSC4AutomataController_MinusOneFloat));
}

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
    out[1] = At<float>(self, field::kHeight);
    out[2] = At<float>(self, field::kZ);
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
// Part a3b: the behaviour
//
// The object's own behaviour, in the same detour family as part a3a and in
// its own group (simulators.automata.ctlbehaviour, off by default), so a3a's
// group stays the 32 object/query detours and this one can be enabled alone:
// the base Init 0x00516850, the attractor's 0x00516FB0 and the generator's
// 0x00516FE0; the base Shutdown 0x005169F0 and the generator's 0x00517000;
// SetActive 0x00516AA0 and the generator's 0x00517240; the five base slots
// a3a left to the game that the runs reach (spot selection 0x00516EC0, cell
// bounds 0x00515BD0, the cell test 0x00515A80, the template's occupant lists
// 0x00516B40, the frame/step handler 0x00515870); the three automaton events
// 0x0051BB60 (kind 28), the attractor's 0x0051BFB0 (29) and the base
// 0x0051BCE0 (30); an automaton leaving 0x0051BD50 (31) and picking a spot
// 0x0051BAB0 (32); and the tick: the base 0x00514D90 and the generator's
// 0x00515E60.
//
// Left as the game's, because no run of the a3a parity batch reaches them
// (checked with diagnostics.coverage over the 12 corpus saves and t0000 for
// 7 and 30 days): the position from the box 0x00516C40 (slot 22), resetting
// the tick's accumulators 0x00514E30 (26), the base automaton event 29
// 0x0051BCA0 and the attractor's 30 0x0051C030, SetLimit 0x00517EF0 (19),
// the generator's spawn 0x005198A0 (41) and the time passed stub 0x006F7DC0
// (24). Part a3c below takes all of them but the stub.
//
// What it does. Init gives the controller the city's script system, and with
// flag 0x20 (a controller placed by a lot) the centre of its occupant's lot
// from the lot manager's cells, its height from the terrain (or from the
// occupant's own position off the map); then it sets itself active (slot 18),
// asks the view about its template's occupant lists (slot 25) and tells the
// view and the sound service (0x00515770). The attractor's Init assigns the
// template's curve (+0x88) to its own at +0x5C first; the generator's only
// answers the base's bool. Shutdown sets itself inactive, releases every
// automaton in its list (clearing flag 0x100 on each first) and frees the
// list, then lets go of the occupant, the script system and the template.
// SetActive keeps flag 1 in step with the answer: it tells the controller
// manager (its slot 16, MoveController) and the view/sound helper, runs the
// frame/step handler (slot 27), zeroes +0x58, clears flag 0x400 and sets it
// again when the template's +0x50 is above zero. The generator's SetActive,
// going inactive with a generator template, clears its own automaton list
// first (unsetting flag 0x100 and releasing each) and recounts it.
// Spot selection asks the occupant for its bounding box (slot 10) and, for a
// lot occupant, replaces it with the lot's cells through the lot manager,
// then the cell bounds grow the box by the template's curve (slot 9) and
// clamp it to the city, taking both corner heights from the terrain. The
// cell test asks whether a point is in that box (within the curve), rejects
// one more than 5 metres above the ground when flag 0x1000 is set, and, with
// an out pointer, answers how far it is from the controller's own position,
// or from where the line to it leaves the box. The occupant lists ask the
// city view (0x00B43DC8, interface 0xFA47B3F9 slot 46) whether each occupant
// of the template's two lists is shown: the first list must all be shown and
// the second none, and the controller is set active with that answer when
// either list is not empty. The frame/step handler, on frames, marks flag 4
// for a controller whose position or any of the four x-z corners of its cell
// box is in the camera view (0x007F7E50), otherwise clears it.
// The automaton events (kind 4): 28 finds the manager's record for the
// controller's occupant, has it set up (0x00516070, the first time), and when
// the record says so places the automaton's event object (its slot 48) at the
// record's position, scaled by the object's own size (at least 1), takes the
// height from the traffic network (slot 18) and hands the place to the
// automaton (slot 10); with no object it uses the record's own position. 29
// adds the automaton to the controller's own list (if the template's finished
// count is not zero and it is not there yet) and then to the manager's record
// (0x005184E0, which chains it to the record's last automaton through slot
// 49). 30 and 31 take the automaton out of the manager's record (0x005172D0);
// 31 also takes it out of the controller's list, releasing it. 32 answers the
// first of the template's spot records (stride 0x7C) whose +4 is at least the
// value asked, and for a record of kind 4 only when the point is within +4 of
// its place.
// The tick (38): the base adds the passed time (a millisecond count times
// 0.001) to +0x58 while flag 0x400 is set, clearing the flag at the
// template's +0x50, and to +0x50 while the flag is clear and +0x54 is above
// zero, making itself inactive at +0x54. The generator's adds the passed
// time to its +0x78/+0x80 clock pair, and once the pair is reached, its
// automata are below the limit, flag 4 is set, its weekday mask accepts the
// controller time's day and a fresh draw from its own generator is within the
// template's spawn chance (and the hour curve is not zero), it calls the
// spawn (slot 41); on the reached path only it then adds the template's spawn
// interval (1/60, 1, 24 or 168 hours, by +0x9C, over +0x98) to its +0x80 pair
// and zeroes the +0x78 one. The four early returns and the not-reached path
// leave the clock pair accumulating.
//
// Oddities kept: Init's flag-0x20 branch calls LocationIsInBounds on the
// lot's centre and stores the terrain's height into +0x18 (the position is
// x, height, z); an occupant off the map keeps its own height. Shutdown calls
// SetActive(0) *before* its initialized test. SetActive's first TestFlag(1)
// answer must differ from the byte argument (so a value of 3 never matches
// 1). The generator's SetActive recounts the list after clearing it. Spot
// selection leaves the occupant's bounding box when the lot manager has no
// lot; cell bounds clamp the low corner to zero, so a controller placed by a
// lot can report a zero low corner. The cell test's distance uses the point
// (x, z) of both, and the segment test's crossing point from 0x00514E70.
// The occupant lists release the *service* pointer they read, not the
// interface QueryInterface answered (they are the same object). The
// frame/step handler clears only the low byte of its flag dword and then
// passes the whole dword to SetFlag; the top three bytes are stack leftovers,
// but SetFlag 0x00517E70 compares only the byte, so ours passing 0 or 1 is
// exact. The automaton events only look at kind 4, and 28 answers 1 for a
// record that is not ready and 0 otherwise. The generator's tick resets the
// clock pair only on the reached path: the not-reached path returns before
// the reset, and the four early returns skip it too.
//
// Calls, each count from the asm's pushes and the callee's own ret: the
// controller's slots 3, 4, 9, 10, 11, 18, 19, 20, 21, 22, 23, 24, 25, 26,
// 27, 28, 29, 30, 31, 32, 35, 38, 41 (this part's and a3a's); the city's
// slots 0x84 GetLotManager (occupant), 0xBC GetAutomataControllerManager,
// 0xA0 GetTrafficNetwork, 0xC0 GetAutomataScriptSystem, 0x150 GetTerrain,
// 0x164 SizeX, 0x168 SizeZ, 0x188 LocationIsInBounds (x, z); the lot
// manager's slot 0x38 (occupant) and the lot's 0x38 (out 4 ints); the
// terrain's 0x34 (x, z); the occupant's slots 0x20 GetPosition (out 3
// floats), 0x28 GetBoundingBox (low, high), 0x34 UnsetRemovalFlags (flags);
// the city view service's slot 0 QueryInterface and the view's 0xB8
// (occupant); the manager's slot 0x40 MoveController (controller) and 0x60
// the controller time (a pointer). The game's helpers called at their
// addresses: 0x00515770 (byte), 0x00515D20 (), 0x00514E70 (a, b, c),
// 0x005D76A0 (a, b) cdecl, 0x007F7E50 (point), 0x0051B9D0 (occupant, byte),
// 0x00516070 (occupant), 0x005172D0 (automaton), 0x005184E0 (automaton),
// 0x0054DD00 (7) cdecl, 0x00515220 (template), 0x00515250 (a, b) cdecl,
// 0x00515080 AddHours (hours), 0x005F4250 Evaluate (value), 0x0091372E
// NextFloat (), 0x00427640 PushBack (value), 0x0050A2A0 Clear (), 0x005F4460
// Assign (curve).
//
// Registers. Every call is through a vtable or a helper that takes ECX from
// `this`; no caller of this part's functions relies on a register the game's
// leaves set (the manager's Update and the automata call them as slots, the
// same on both sides). No register thunks.
//
// Floats: the x87's 53-bit doubles; the sums and products in the order and
// association of the asm, floats stored where the game stores a float (the
// two products in slot 28's place, the scaled time in the tick, the box
// bounds), doubles where the comparison follows an unrounded x87 value.
// `test ah,5 / jp` skips a value that is not below (greater, equal or
// unordered), `test ah,0x41 / jp` a value that is greater or unordered,
// `test ah,0x41 / jne` a value that is below, equal or unordered, and
// `test ah,1 / jne` one that is below or unordered; each is written as the C
// comparison it means, NaN included.
using automata_ctlmgr::AutomatonNode;
using automata_ctlmgr::AutomatonValue;
using automata_ctlmgr::ControllerTime;
using automata_ctlmgr::HashTable;
using automata_ctlmgr::ListNode;

// The game's helpers this part calls at their addresses (not ours).
constexpr std::uintptr_t kControllerSound = 0x00515770;    // (active)
constexpr std::uintptr_t kOccupantLot = 0x00515D20;        // () -> the occupant's lot
constexpr std::uintptr_t kSegmentBox = 0x00514E70;         // (a, b, crossing out)
constexpr std::uintptr_t kViewPoint = 0x007F7E50;          // (point) -> bool
constexpr std::uintptr_t kAutomatonRecord = 0x0051B9D0;    // (occupant, make) -> value
constexpr std::uintptr_t kAutomatonRecordSetUp = 0x00516070; // (occupant)
constexpr std::uintptr_t kAutomatonRemove = 0x005172D0;    // (automaton)
constexpr std::uintptr_t kAutomatonAdd = 0x005184E0;       // (automaton)
constexpr std::uintptr_t kSimulatorQuery7 = 0x0054DD00;    // (7) -> bool
constexpr std::uintptr_t kGeneratorLimit = 0x00515220;     // (template) -> uint32
constexpr std::uintptr_t kTimeReached = 0x00515250;        // (a, b) -> bool
constexpr std::uintptr_t kManagerTime = 0x0051A310;        // () -> the controller time
constexpr std::uintptr_t kMillisScale = 0x00A867A4;        // float 0.001, the tick's ms -> seconds
constexpr std::uintptr_t kSpawnHoursLong = 0x00A95D70;     // float 168, the week-long spawn interval

void* City() { return Global<void*>(addr::SC4_City); }

std::uint32_t AsBits(float value) { return std::bit_cast<std::uint32_t>(value); }
float AsFloat(std::uint32_t bits) { return std::bit_cast<float>(bits); }

// A bound brought into [0, limit], as the game's fcom chains do (a NaN stays).
float ClampBound(float value, float limit)
{
    if (value < 0.0f)
        return 0.0f;
    if (value > limit)
        return limit;
    return value;
}

// The manager's automaton value of an occupant (the controller table at
// +0x6C; the game's own walk).
AutomatonValue* FindAutomatonValue(char* manager, void* occupant)
{
    HashTable* const table = reinterpret_cast<HashTable*>(manager + automata_ctlmgr::field::kAutomata);
    const std::uint32_t buckets =
        static_cast<std::uint32_t>(reinterpret_cast<const char*>(table->end) -
                                   reinterpret_cast<const char*>(table->begin)) >> 2;
    const std::uint32_t bucket = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(occupant)) % buckets;
    for (auto* node = static_cast<AutomatonNode*>(table->begin[bucket]); node; node = node->next)
        if (node->occupant == occupant)
            return &node->value;
    return nullptr;
}

// ====================================================================================
// Init, Shutdown and SetActive

// 0x00516850, slot 3 (base; the attractor's and generator's reach it).
std::uint8_t __fastcall Init(char* self, void*)
{
    static bool logged;
    FirstCall("Init", logged);
    if (At<std::uint8_t>(self, field::kInitialized))
        return 1;
    At<std::uint8_t>(self, field::kInitialized) = 1;
    At<std::uint8_t>(self, field::kByte27) = 0;
    At<std::uint8_t>(self, field::kByte25) = 0;
    At<std::uint8_t>(self, field::kByte26) = 0;
    At<std::uint8_t>(self, field::kByte24) = 0;
    void* const scripts = Virtual<void*>(City(), 0xC0); // GetAutomataScriptSystem
    void* const oldScripts = At<void*>(self, field::kScripts);
    if (scripts != oldScripts) {
        if (scripts)
            Virtual<std::uint32_t>(scripts, slot::kAddRef);
        At<void*>(self, field::kScripts) = scripts;
        if (oldScripts)
            Virtual<std::uint32_t>(oldScripts, slot::kRelease);
    }
    if (Virtual<std::uint8_t>(self, slot::kTestFlag, kFlag20)) {
        void* const lots = Virtual<void*>(City(), 0x84); // GetLotManager
        void* lot = nullptr;
        if (lots && At<void*>(self, field::kOccupant))
            lot = Virtual<void*>(lots, 0x38, At<void*>(self, field::kOccupant));
        if (lot) {
            std::int32_t bounds[4] = {};
            Virtual<void>(lot, 0x38, bounds);
            const double x = static_cast<double>((bounds[2] - bounds[0] + 1) * 16) * 0.5 +
                             static_cast<double>(bounds[0] * 16);
            const double z = static_cast<double>((bounds[3] - bounds[1] + 1) * 16) * 0.5 +
                             static_cast<double>(bounds[1] * 16);
            At<float>(self, field::kX) = static_cast<float>(x);
            At<float>(self, field::kZ) = static_cast<float>(z);
            if (Virtual<std::uint8_t>(City(), 0x188, static_cast<float>(x), static_cast<float>(z))) {
                At<float>(self, field::kHeight) = Virtual<float>(Global<void*>(addr::SC4_Terrain), 0x34,
                                                                 static_cast<float>(x), static_cast<float>(z));
            } else if (void* const occupant = At<void*>(self, field::kOccupant)) {
                float position[3] = {};
                Virtual<void>(occupant, 0x20, position);
                At<float>(self, field::kHeight) = position[1];
            } else {
                At<float>(self, field::kHeight) = 0.0f;
            }
        } else if (void* const occupant = At<void*>(self, field::kOccupant)) {
            Virtual<void>(occupant, 0x20, self + field::kX); // in place
        }
    }
    Virtual<void>(self, slot::kSetActive, 1u);
    Virtual<void>(self, slot::kOccupantLists);
    CallThis<void>(kControllerSound, self, 1u);
    return 1;
}

// 0x00516FB0, the attractor's slot 3: the template's curve into its own, then
// the base Init.
std::uint8_t __fastcall AttractorInit(char* self, void*)
{
    static bool logged;
    FirstCall("attractor Init", logged);
    if (At<std::uint8_t>(self, field::kInitialized))
        return 1;
    CallThis<void*>(addr::SC4Curve_Assign, self + field::kCurve,
                    reinterpret_cast<char*>(At<std::uintptr_t>(self, field::kTemplate)) + tmpl::k88);
    return CallThis<std::uint8_t>(addr::cSC4AutomataController_Init, self);
}

// 0x00516FE0, the generator's slot 3.
std::uint8_t __fastcall GeneratorInit(char* self, void*)
{
    static bool logged;
    FirstCall("generator Init", logged);
    if (At<std::uint8_t>(self, field::kInitialized))
        return 1;
    return CallThis<std::uint8_t>(addr::cSC4AutomataController_Init, self) != 0 ? 1 : 0;
}

// 0x005169F0, slot 4 (base and attractor).
std::uint8_t __fastcall Shutdown(char* self, void*)
{
    static bool logged;
    FirstCall("Shutdown", logged);
    Virtual<void>(self, slot::kSetActive, 0u);
    if (!At<std::uint8_t>(self, field::kInitialized))
        return 1;
    At<std::uint8_t>(self, field::kInitialized) = 0;
    ListNode* const head = At<ListNode*>(self, field::kAutomata);
    for (ListNode* e = head->next; e != head; e = e->next) {
        void* const automaton = e->value;
        if (Virtual<std::uint8_t>(self, slot::kTestFlag, 0x100u))
            Virtual<void>(automaton, 0x34, 0u);
        Virtual<std::uint32_t>(automaton, slot::kRelease);
    }
    for (ListNode* e = head->next; e != head;) {
        ListNode* const next = e->next;
        Free(e);
        e = next;
    }
    head->next = head;
    head->prev = head;
    At<std::uint32_t>(self, field::kAutomataCount) = 0;
    if (void* const occupant = At<void*>(self, field::kOccupant)) {
        At<void*>(self, field::kOccupant) = nullptr;
        Virtual<std::uint32_t>(occupant, slot::kRelease);
    }
    if (void* const scripts = At<void*>(self, field::kScripts)) {
        At<void*>(self, field::kScripts) = nullptr;
        Virtual<std::uint32_t>(scripts, slot::kRelease);
    }
    At<void*>(self, field::kTemplate) = nullptr;
    return 1;
}

// 0x00517000, the generator's slot 4: the base Shutdown when it is initialized.
std::uint8_t __fastcall GeneratorShutdown(char* self, void*)
{
    static bool logged;
    FirstCall("generator Shutdown", logged);
    if (At<std::uint8_t>(self, field::kInitialized))
        CallThis<void>(addr::cSC4AutomataController_Shutdown, self);
    return 1;
}

// 0x00516AA0, slot 18 (active), ret 4.
void __fastcall SetActive(char* self, void*, std::uint32_t value)
{
    static bool logged;
    FirstCall("SetActive", logged);
    if (!At<std::uint8_t>(self, field::kInitialized))
        return;
    if (Virtual<std::uint8_t>(self, slot::kTestFlag, kFlag1) == static_cast<std::uint8_t>(value))
        return;
    Virtual<void>(self, slot::kSetFlag, kFlag1, value);
    void* const manager = Virtual<void*>(City(), 0xBC); // GetAutomataControllerManager
    if (static_cast<std::uint8_t>(value))
        Virtual<void>(manager, 0x40, self); // MoveController
    CallThis<void>(kControllerSound, self, value);
    Virtual<void>(self, slot::kFrameStep);
    At<std::uint32_t>(self, field::k58) = 0;
    Virtual<void>(self, slot::kSetFlag, kFlag400, 0u);
    void* const t = At<void*>(self, field::kTemplate);
    if (static_cast<std::uint8_t>(value) && t && At<float>(t, tmpl::k50) > 0.0f)
        Virtual<void>(self, slot::kSetFlag, kFlag400, 1u);
}

// 0x00517240, the generator's slot 18, ret 4: going inactive with a generator
// template empties its own automaton list first.
void __fastcall GeneratorSetActive(char* self, void*, std::uint32_t value)
{
    static bool logged;
    FirstCall("generator SetActive", logged);
    void* const t = At<void*>(self, field::kTemplate);
    if (At<std::uint8_t>(self, field::kInitialized) && static_cast<std::uint8_t>(value) == 0 && t &&
        At<std::uint32_t>(t, tmpl::kClassId) == kGeneratorClassId) {
        ListNode* const head = At<ListNode*>(self, field::kAutomata);
        if (Virtual<std::uint8_t>(self, slot::kTestFlag, 0x100u)) {
            for (ListNode* e = head->next; e != head; e = e->next) {
                void* const automaton = e->value;
                Virtual<void>(automaton, 0x34, 0u);
                Virtual<std::uint32_t>(automaton, slot::kRelease);
            }
            CallThis<void>(addr::SC4StlList_Clear, self + field::kAutomata);
        }
        std::uint32_t count = 0;
        for (ListNode* e = head->next; e != head; e = e->next)
            ++count;
        At<std::uint32_t>(self, field::kAutomataCount) = count;
    }
    CallThis<void>(addr::cSC4AutomataController_SetActive, self, value);
}

// ====================================================================================
// The base slots a3a left to the game

// 0x00516EC0, slot 20 (low out, high out), ret 8: the box the controller
// occupies, from the occupant's own bounding box or its lot's cells.
void __fastcall SpotSelection(char* self, void*, float* low, float* high)
{
    static bool logged;
    FirstCall("SpotSelection", logged);
    if (Virtual<std::uint8_t>(self, slot::kTestFlag, kFlag20) ||
        Virtual<std::uint8_t>(self, slot::kTestFlag, 0x40u)) {
        Virtual<void>(self, slot::kPosition, low);
        Virtual<void>(self, slot::kPosition, high);
        return;
    }
    void* const occupant = At<void*>(self, field::kOccupant);
    if (!occupant) {
        Virtual<void>(self, slot::kPosition, low);
        Virtual<void>(self, slot::kPosition, high);
        return;
    }
    Virtual<void>(occupant, 0x28, low, high); // GetBoundingBox
    void* const lot = CallThis<void*>(kOccupantLot, self);
    if (!lot)
        return;
    std::int32_t bounds[4] = {};
    Virtual<void>(lot, 0x38, bounds);
    low[0] = static_cast<float>(bounds[0] * 16);
    low[2] = static_cast<float>(bounds[1] * 16);
    high[0] = static_cast<float>((bounds[2] + 1) * 16);
    high[2] = static_cast<float>((bounds[3] + 1) * 16);
}

// 0x00515BD0, slot 21 (low out, high out), ret 8: the box grown by the
// template's curve, clamped to the city, the corner heights from the terrain.
void __fastcall CellBounds(char* self, void*, float* low, float* high)
{
    static bool logged;
    FirstCall("CellBounds", logged);
    Virtual<void>(self, slot::kSpotSelection, low, high);
    const float radius = Virtual<float>(self, 0x24); // slot 9, the template's curve
    low[0] -= radius;
    high[0] += radius;
    low[2] -= radius;
    high[2] += radius;
    const float sizeX = Virtual<float>(City(), 0x164); // SizeX
    const float sizeZ = Virtual<float>(City(), 0x168); // SizeZ
    low[0] = ClampBound(low[0], sizeX);
    low[2] = ClampBound(low[2], sizeZ);
    high[0] = ClampBound(high[0], sizeX);
    high[2] = ClampBound(high[2], sizeZ);
    void* const terrain = Virtual<void*>(City(), 0x150); // GetTerrain
    low[1] = Virtual<float>(terrain, 0x34, low[0], low[2]);
    high[1] = Virtual<float>(terrain, 0x34, high[0], high[2]);
}

// 0x00515A80, slot 23 (point, distance out), ret 8: is a point in the box,
// and how far is it from the controller (or where the line leaves the box).
std::uint8_t __fastcall CellTest(char* self, void*, const float* point, float* distanceOut)
{
    static bool logged;
    FirstCall("CellTest", logged);
    float low[3] = {}, high[3] = {};
    Virtual<void>(self, slot::kCellBounds, low, high);
    const float radius = Virtual<float>(self, 0x24); // slot 9
    if (!((static_cast<double>(low[0]) - static_cast<double>(radius)) <= static_cast<double>(point[0])))
        return 0;
    if (!((static_cast<double>(low[2]) - static_cast<double>(radius)) <= static_cast<double>(point[2])))
        return 0;
    if (!((static_cast<double>(high[0]) + static_cast<double>(radius)) >= static_cast<double>(point[0])))
        return 0;
    if (!((static_cast<double>(high[2]) + static_cast<double>(radius)) >= static_cast<double>(point[2])))
        return 0;
    if (Virtual<std::uint8_t>(self, slot::kTestFlag, 0x1000u)) {
        const float height = Virtual<float>(Global<void*>(addr::SC4_Terrain), 0x34, point[0], point[2]);
        if (static_cast<double>(point[1]) >
            static_cast<double>(height) + static_cast<double>(Global<float>(addr::SC4_FloatFive)))
            return 0;
    }
    if (distanceOut) {
        float position[3] = {};
        if (Virtual<std::uint8_t>(self, slot::kTestFlag, 0x10u))
            Virtual<void>(self, slot::kPositionFromBox, position);
        else
            Virtual<void>(self, slot::kPosition, position);
        float from[2] = {position[0], position[2]};
        float to[2] = {point[0], point[2]};
        float crossing[2] = {0.0f, 0.0f};
        if (CallThis<std::uint8_t>(kSegmentBox, self, from, to, crossing))
            *distanceOut = CallCdecl<float>(addr::SC4_Distance2D, crossing, to);
        else
            *distanceOut = CallCdecl<float>(addr::SC4_Distance2D, from, to);
    }
    return 1;
}

// 0x00516B40, slot 25: the template's two occupant lists, as the city view
// shows them, set the controller active.
void __fastcall OccupantLists(char* self, void*)
{
    static bool logged;
    FirstCall("OccupantLists", logged);
    char* const t = static_cast<char*>(Template(self));
    if (!t)
        return;
    void* const service = Global<void*>(addr::SC4_Service43DC8);
    if (!service)
        return;
    void* view = nullptr;
    if (!Virtual<std::uint8_t>(service, 0x0, 0xFA47B3F9u, &view))
        return;
    bool first = false, second = false;
    std::uint8_t flag = 1;
    ListNode* const head1 = At<ListNode*>(t, tmpl::kList1);
    if (head1->next != head1) {
        first = true;
        for (ListNode* e = head1->next; e != head1; e = e->next) {
            if (!flag)
                break;
            flag = Virtual<std::uint8_t>(view, 0xB8, e->value) ? 1 : 0;
        }
    }
    ListNode* const head2 = At<ListNode*>(t, tmpl::kList2);
    if (head2->next != head2) {
        second = true;
        for (ListNode* e = head2->next; e != head2; e = e->next) {
            if (!flag)
                break;
            flag = Virtual<std::uint8_t>(view, 0xB8, e->value) ? 0 : 1;
        }
    }
    if (first || second)
        Virtual<void>(self, slot::kSetActive, static_cast<std::uint32_t>(flag));
    // The game releases the service it read (the same object the interface
    // answered).
    Virtual<std::uint32_t>(service, slot::kRelease);
}

// 0x00515870, slot 27: flag 4 says whether the controller is in the camera
// view, from its position and the four x-z corners of its cell box, each corner
// carrying the box's own low or high height (not the terrain height at that
// corner's x and z).
std::uint8_t __fastcall FrameStep(char* self, void*)
{
    static bool logged;
    FirstCall("FrameStep", logged);
    void* const view = Global<void*>(addr::SC4_CameraView);
    if (!view)
        return 1;
    void* const terrain = Global<void*>(addr::SC4_Terrain);
    if (!terrain)
        return 1;
    // The game clears only the flag dword's low byte and passes the whole
    // dword to SetFlag; its top three bytes are stack leftovers, but SetFlag
    // 0x00517E70 tests only the low byte, so 0 or 1 is exact.
    std::uint8_t flag = 0;
    if (Virtual<std::uint8_t>(self, slot::kTestFlag, 0x200u)) {
        flag = 1;
    } else {
        float low[3] = {}, high[3] = {};
        Virtual<void>(self, slot::kCellBounds, low, high);
        float position[3] = {};
        Virtual<void>(self, slot::kPosition, position);
        if (Virtual<std::uint8_t>(City(), 0x188, low[0], low[2]))
            low[1] = Virtual<float>(terrain, 0x34, low[0], low[2]);
        else
            low[1] = position[1];
        if (Virtual<std::uint8_t>(City(), 0x188, high[0], high[2]))
            high[1] = Virtual<float>(terrain, 0x34, high[0], high[2]);
        else
            high[1] = position[1];
        float point[3] = {};
        Virtual<void>(self, slot::kPosition, point); // the controller's position
        if (CallThis<std::uint8_t>(kViewPoint, view, point)) {
            flag = 1;
        } else {
            point[0] = low[0];
            point[1] = low[1];
            point[2] = low[2]; // the low corner
            if (CallThis<std::uint8_t>(kViewPoint, view, point)) {
                flag = 1;
            } else {
                point[0] = high[0]; // high x, the low corner's height and z
                if (CallThis<std::uint8_t>(kViewPoint, view, point)) {
                    flag = 1;
                } else {
                    point[0] = high[0];
                    point[1] = high[1];
                    point[2] = high[2]; // the high corner
                    if (CallThis<std::uint8_t>(kViewPoint, view, point)) {
                        flag = 1;
                    } else {
                        point[0] = low[0]; // low x, the high corner's height and z
                        if (CallThis<std::uint8_t>(kViewPoint, view, point))
                            flag = 1;
                    }
                }
            }
        }
    }
    Virtual<void>(self, slot::kSetFlag, kFlag4, static_cast<std::uint32_t>(flag));
    return 1;
}

// ====================================================================================
// The automaton events

// 0x0051BB60, slot 28 (automaton, kind, unused), ret 0xC.
std::uint32_t __fastcall AutomatonEventA(char* self, void*, void* automaton, std::uint32_t kind,
                                         std::uint32_t)
{
    static bool logged;
    FirstCall("AutomatonEventA", logged);
    if (kind != 4)
        return 0;
    char* const manager = Virtual<char*>(City(), 0xBC);
    AutomatonValue* const value =
        CallThis<AutomatonValue*>(kAutomatonRecord, manager, At<void*>(self, field::kOccupant), 1u);
    CallThis<void>(kAutomatonRecordSetUp, value, At<void*>(self, field::kOccupant));
    if (!At<std::uint8_t>(value, 0x20)) {
        Virtual<void>(automaton, 0x34, 0u);
        return 1;
    }
    void* const object = Virtual<void*>(automaton, 0xC0);
    float place[3] = {};
    if (!object) {
        place[0] = At<float>(value, 0x08);
        place[1] = At<float>(value, 0x0C);
        place[2] = At<float>(value, 0x10);
    } else {
        Virtual<void>(object, 0x2C, place); // GetPosition
        float scale = Virtual<float>(object, 0x44);
        if (!(scale > 1.0f))
            scale = 1.0f;
        const float product1 = static_cast<float>(static_cast<double>(scale) * At<float>(value, 0x18));
        const float product2 = static_cast<float>(static_cast<double>(scale) * At<float>(value, 0x1C));
        place[0] = static_cast<float>(static_cast<double>(scale) * At<float>(value, 0x14) +
                                      static_cast<double>(place[0]));
        place[1] = static_cast<float>(static_cast<double>(product1) + static_cast<double>(place[1]));
        place[2] = static_cast<float>(static_cast<double>(product2) + static_cast<double>(place[2]));
    }
    void* const network = Virtual<void*>(City(), 0xA0); // GetTrafficNetwork
    place[1] = Virtual<float>(network, 0x48, place[1], place[2]);
    Virtual<void>(automaton, 0x28, place);
    return 0;
}

// 0x0051BFB0, the attractor's slot 29 (automaton, kind), ret 8.
std::uint32_t __fastcall AttractorEventB(char* self, void*, void* automaton, std::uint32_t kind)
{
    static bool logged;
    FirstCall("attractor AutomatonEventB", logged);
    char* const t = static_cast<char*>(Template(self));
    if (t && At<std::uint32_t>(t, tmpl::k54) > 0) {
        ListNode* const head = At<ListNode*>(self, field::kAutomata);
        bool found = false;
        for (ListNode* e = head->next; e != head; e = e->next)
            if (e->value == automaton) {
                found = true;
                break;
            }
        if (!found) {
            CallThis<void>(addr::SC4StlList_PushBack, self + field::kAutomata, &automaton);
            ++At<std::uint32_t>(self, field::kAutomataCount);
            Virtual<std::uint32_t>(automaton, slot::kAddRef);
        }
    }
    if (kind == 4) {
        char* const manager = Virtual<char*>(City(), 0xBC);
        AutomatonValue* const value =
            CallThis<AutomatonValue*>(kAutomatonRecord, manager, At<void*>(self, field::kOccupant), 1u);
        if (value)
            CallThis<void>(kAutomatonAdd, value, automaton);
    }
    return 0;
}

// 0x0051BCE0, slot 30 (automaton, kind), ret 8.
std::uint32_t __fastcall AutomatonEventC(char* self, void*, void* automaton, std::uint32_t kind)
{
    static bool logged;
    FirstCall("AutomatonEventC", logged);
    if (kind == 4) {
        char* const manager = Virtual<char*>(City(), 0xBC);
        if (AutomatonValue* const value = FindAutomatonValue(manager, At<void*>(self, field::kOccupant)))
            CallThis<void>(kAutomatonRemove, value, automaton);
    }
    return 0;
}

// 0x0051BD50, slot 31 (automaton), ret 4: it leaves the manager's record and
// the controller's list.
std::uint32_t __fastcall AutomatonLeaves(char* self, void*, void* automaton)
{
    static bool logged;
    FirstCall("AutomatonLeaves", logged);
    char* const manager = Virtual<char*>(City(), 0xBC);
    if (AutomatonValue* const value = FindAutomatonValue(manager, At<void*>(self, field::kOccupant)))
        CallThis<void>(kAutomatonRemove, value, automaton);
    ListNode* const head = At<ListNode*>(self, field::kAutomata);
    ListNode* found = head->next;
    while (found != head && found->value != automaton)
        found = found->next;
    if (found != head) {
        Virtual<std::uint32_t>(automaton, slot::kRelease);
        ListNode* const next = found->next;
        ListNode* const prev = found->prev;
        prev->next = next;
        next->prev = prev;
        Free(found);
        --At<std::uint32_t>(self, field::kAutomataCount);
    }
    return 1;
}

// 0x0051BAB0, slot 32 (value, point), ret 8: the template's spot record that
// takes the value, the first of kind 4 also within its distance of the point.
void* __fastcall PickSpot(char* self, void*, std::uint32_t valueBits, const float* point)
{
    static bool logged;
    FirstCall("PickSpot", logged);
    // The game reads the template's spot vector without testing the template.
    char* const t = static_cast<char*>(Template(self));
    const float wanted = AsFloat(valueBits);
    char* const begin = reinterpret_cast<char*>(At<std::uintptr_t>(t, tmpl::kSpots));
    char* const end = reinterpret_cast<char*>(At<std::uintptr_t>(t, tmpl::kSpotsEnd));
    for (char* record = begin; record != end; record += 0x7C) {
        const float radius = At<float>(record, 0x4);
        if (!(radius >= wanted))
            continue;
        if (At<std::uint32_t>(record, 0x0) != 4)
            return record;
        char* const manager = Virtual<char*>(City(), 0xBC);
        AutomatonValue* const automatonValue =
            CallThis<AutomatonValue*>(kAutomatonRecord, manager, At<void*>(self, field::kOccupant), 1u);
        CallThis<void>(kAutomatonRecordSetUp, automatonValue, At<void*>(self, field::kOccupant));
        if (!At<std::uint8_t>(automatonValue, 0x20))
            continue;
        const double dx = static_cast<double>(point[0]) - static_cast<double>(At<float>(automatonValue, 0x08));
        const double dz = static_cast<double>(point[2]) - static_cast<double>(At<float>(automatonValue, 0x10));
        const double distance = std::sqrt(dz * dz + dx * dx);
        if (!(distance > static_cast<double>(radius)))
            return record;
    }
    return nullptr;
}

// ====================================================================================
// The tick

// 0x00514D90, slot 38 (time, second, passed), ret 0xC.
void __fastcall Tick(char* self, void*, std::uint32_t timeBits, std::uint32_t, void*)
{
    static bool logged;
    FirstCall("Tick", logged);
    const float scaled = static_cast<float>(static_cast<double>(AsFloat(timeBits)) *
                                            static_cast<double>(Global<float>(kMillisScale)));
    if (!Virtual<std::uint8_t>(self, slot::kTestFlag, kFlag1))
        return;
    if (Virtual<std::uint8_t>(self, slot::kTestFlag, kFlag400)) {
        const double sum = static_cast<double>(scaled) + static_cast<double>(At<float>(self, field::k58));
        At<float>(self, field::k58) = static_cast<float>(sum);
        void* const t = At<void*>(self, field::kTemplate);
        if (t && sum >= static_cast<double>(At<float>(t, tmpl::k50)))
            Virtual<void>(self, slot::kSetFlag, kFlag400, 0u);
    }
    if (Virtual<std::uint8_t>(self, slot::kTestFlag, kFlag400))
        return;
    if (!(At<float>(self, field::k54) > 0.0f))
        return;
    const double sum = static_cast<double>(scaled) + static_cast<double>(At<float>(self, field::k50));
    At<float>(self, field::k50) = static_cast<float>(sum);
    if (sum > static_cast<double>(At<float>(self, field::k54)))
        Virtual<void>(self, slot::kSetActive, 0u);
}

// 0x00515E60, the generator's slot 38, ret 0xC.
void __fastcall GeneratorTick(char* self, void*, std::uint32_t timeBits, std::uint32_t second, void* passed)
{
    static bool logged;
    FirstCall("generator Tick", logged);
    CallThis<void>(addr::cSC4AutomataController_Tick, self, timeBits, second, passed);
    if (Virtual<std::uint8_t>(self, slot::kSaturated))
        return;
    if (Virtual<std::uint8_t>(self, slot::kTestFlag, kFlag400))
        return;
    if (!Virtual<std::uint8_t>(self, slot::kTestFlag, kFlag4))
        return;
    if (!CallCdecl<std::uint8_t>(kSimulatorQuery7, 7u))
        return;
    const ControllerTime* const at = static_cast<const ControllerTime*>(passed);
    CallThis<void*>(addr::tControllerTime_AddHours, self + 0x78, AsBits(at->hour));
    At<std::uint16_t>(self, 0x7C) = static_cast<std::uint16_t>(At<std::uint16_t>(self, 0x7C) +
                                                              static_cast<std::uint16_t>(at->day));
    if (!CallCdecl<std::uint8_t>(kTimeReached, self + 0x78, self + 0x80))
        return; // not reached: the clock pair keeps accumulating
    char* const t = static_cast<char*>(At<void*>(self, field::kTemplate));
    const std::uint32_t limit = CallThis<std::uint32_t>(kGeneratorLimit, self, t);
    if (At<std::uint32_t>(self, field::kAutomataCount) < limit) {
        char* const manager = Virtual<char*>(City(), 0xBC);
        char* const now = CallThis<char*>(kManagerTime, manager);
        const std::uint32_t hour = At<std::uint32_t>(now, 0x0);
        const std::uint16_t day = At<std::uint16_t>(now, 0x4);
        if (At<std::uint32_t>(t, tmpl::kWeekdays) & (1u << (day & 0x1Fu))) {
            const float curve = static_cast<float>(CallThis<double>(addr::SC4Curve_Evaluate, t + tmpl::kHourCurve,
                                                                    hour));
            const float draw = CallThis<float>(addr::cRZRandom_NextFloat, self + field::kRandom);
            if (std::fabs(curve) > 0.0f && draw <= At<float>(t, tmpl::kSpawnChance)) {
                std::uint32_t daySlot = day;
                Virtual<void>(self, slot::kSpawn, &daySlot, AsBits(curve));
            }
        }
    }
    // Reached: the spawn interval becomes the new target and the accumulator
    // is cleared.
    float hours = 0.0f;
    switch (At<std::uint32_t>(t, tmpl::kSpawnKind)) {
    case 0:
        hours = static_cast<float>(static_cast<double>(Global<float>(addr::SC4_FloatOneSixtieth)) /
                                   static_cast<double>(At<float>(t, tmpl::kSpawnHours)));
        break;
    case 1:
        hours = static_cast<float>(1.0 / static_cast<double>(At<float>(t, tmpl::kSpawnHours)));
        break;
    case 2:
        hours = static_cast<float>(24.0 / static_cast<double>(At<float>(t, tmpl::kSpawnHours)));
        break;
    case 3:
        hours = static_cast<float>(static_cast<double>(Global<float>(kSpawnHoursLong)) /
                                   static_cast<double>(At<float>(t, tmpl::kSpawnHours)));
        break;
    default: break;
    }
    At<std::uint32_t>(self, 0x80) = 0;
    At<std::uint16_t>(self, 0x84) = 0;
    CallThis<void*>(addr::tControllerTime_AddHours, self + 0x80, AsBits(hours));
    At<std::uint32_t>(self, 0x78) = 0;
    At<std::uint16_t>(self, 0x7C) = 0;
}

// ====================================================================================
// Part a3c: the save and the rest of the behaviour
//
// Two more groups, off by default, so a3a's and a3b's stay as they were:
//   simulators.automata.ctlserial (kSerial, 6): the +4 serializable part's
//   Write (base 0x00517FD0, attractor 0x00518230, generator 0x005183D0) and
//   Read (base 0x005180E0, attractor 0x00518270, generator 0x00518450).
//   simulators.automata.ctlspawn (kSpawn, 6): the generator's spawn
//   0x005198A0 (slot 41), the position from the occupant's lot 0x00516C40
//   (slot 22), resetting the tick's accumulators 0x00514E30 (26), the base
//   automaton event 29 0x0051BCA0 (the generator's), the attractor's event 30
//   0x0051C030 and SetLimit 0x00517EF0 (19).
// Still the game's: the time-passed stub 0x006F7DC0 (slot 24 of the base and
// the attractor), `RET 8`, three bytes, shared with other classes' vtables.
//
// What it does. The serializable functions take object + 4 as this. Write
// saves a version (5), the occupant as a reference, the template's name (or
// an empty string the function makes on its stack), the position, +0x50 and
// +0x54, +0x3C, the flags and the automaton count, then a reference per
// automaton of the list, and answers 1 when the stream reports no error. The
// attractor adds version 1 and its curve (0x005DB190); the generator writes
// version 4 first, then the base, then its two clock pairs (+0x78/+0x7C,
// +0x80/+0x84) and +0x90, +0x94. Read mirrors it: it releases the occupant
// it has before reading the reference, reads the name into the controller's
// own name (+0x28; the manager finds the template by that name after a load,
// part a2's ResolveTemplates), and appends one list node per automaton
// reference to the list it has, without emptying it, the count read straight
// into +0x48. The generator's Read answers 0 after its version unless that
// is 4; nothing else checks a version.
// The spawn makes automata at the generator: how many from the template's
// +0x8C kind (0: the curve value times +0x88; 1: for a building, its
// capacity or jobs from the simulator at city slot 0x110, or a value of its
// lot; 2: a count from that simulator), rounded with the game's 1.5 x 2^23
// trick, at least 1 for kind 0 and at most 25. For each, until the list
// passes its limit (+0x98, else the template's +0x54), a place: with flag
// 0x80 or 0x800 a random cell from 0x00518DC0's list (network kinds 0xC49
// and 2), a random point in it pulled to between the template's +0x20 and
// +0xA8 of the box edge when the line from the controller leaves the box,
// kept only when the traffic network takes it (slot 0xAC) and set on its
// height (slot 0x48); otherwise a random angle and radius around the
// controller's position (0x005D77A0 turns the point), from the box edge when
// the line leaves it, on the terrain. Then an automaton class (0x00516010
// when it has none or has made its count), its manager (0x0050CCA0), and the
// automaton (the manager's slot 0x3C) is appended, referenced, counted
// (+0x48, +0x94), told the controller manager (its slot 0x9C) and given a
// spot (slot 32, PickSpot) or none; +0x88 counts to +0x8C.
// The position from the box is the middle of the lot's front edge, by the
// lot's facing (slot 0x5C), one metre out, else the occupant's own position
// (no lot) or the controller's (no occupant). Resetting the accumulators sets
// the template's flags and flag 2 and takes +0x54 from the template's +0x4C,
// zeroing +0x58. Event 29 (base, kind 4) adds the automaton to the manager's
// record of the occupant (0x0051B9D0, 0x005184E0); the attractor's event 30
// takes the automaton out of its own list (releasing it) and then runs the
// base event 30.
//
// Oddities kept: Write saves the template's name, Read reads it into the
// controller's own name, so a controller read back writes an empty name
// until its template is set again; Read appends to the list it has; the
// versions are read into the stack slot of the stream argument (the
// generator compares that slot's low word with 4, so a failed read compares
// the low word of the stream's address; ours starts the slot the same way).
// The generator's Read leaves EAX's top three bytes from the stream call when
// it answers 0 (its callers test AL; ours answers 0). The position from the
// box seeds a cRZRandom on its stack and never draws from it: the clock seed
// still counts at the game's site (0x00516C51), so ours seeds one too. The
// spawn reads the template's +0x8C without testing the template; its
// rounding subtracts 0x4B400000 from the float's bits, so a negative value
// gives a huge count, capped at 25; with no cells the place's radius (for
// PickSpot) stays the random radius, with cells it is the distance from the
// controller. Base event 29 answers AL 0 and leaves EAX's top bytes; ours
// answers 0.
//
// Calls, each count from the asm's pushes and the callee's own ret: the
// stream's slots 0x18, 0x1C, 0x24, 0x30, 0x3C, 0x48 (cIGZOStream/cIGZIStream
// SetSint16/GetSint16, SetUint16/GetUint16, SetUint32/GetUint32,
// SetFloat32/GetFloat32, SetGZStr/GetGZStr, GetError); SC4Stream's
// WriteReference (stream, object), ReadReference (stream, iid, out, 0),
// WriteVector3 / ReadVector3 (stream, vector), the curve's 0x005DB190 /
// 0x005DB5D0 (stream, curve), all cdecl; the controller's slots 11, 12, 13,
// 22, 32 and the city's 0x84, 0xA0, 0xBC, 0x110, 0x188; the occupant's 0x1C,
// 0x20, 0x2C, 0x58; the lot's 0x38 and 0x5C; the simulator's 0x40, 0x68,
// 0x6C (occupant, info); the lot service 0x00B43D08's 0x18 (x, z, 0) and its
// lot's 0xF4 (out); the traffic network's 0x48 (x, z) and 0xAC (point,
// kind); the terrain's 0x34 (x, z); the automaton manager's 0x3C (class,
// point, 0x2000002, rotation, 0, byte, 0); the automaton's 0x04, 0x9C
// (target), 0xD0 (0, controller, -1.0, 0, 0, 0, 0), 0xDC (spot, controller).
// The game's helpers at their addresses: 0x00414210 (the info's constructor),
// 0x00518DC0 (cells out, kind), 0x00913761 Between (lo, hi doubles),
// 0x009136B1 (n), 0x0091372E NextFloat, 0x005D77A0 (centre, point, angle,
// out) cdecl, 0x00514E70 (a, b, crossing out), 0x005D76A0 (a, b) cdecl,
// 0x00516010, 0x0050CCA0 (class) cdecl, 0x00427640 PushBack (value),
// 0x0051B9D0 (occupant, 1), 0x005184E0 (automaton), the base event 30
// 0x0051BCE0 (automaton, kind), SC4Random::Construct (seed).
//
// Floats as in part a3b: 53-bit doubles, a float where the game stores one,
// a double where it keeps ST0 (the random values, the distance, the sums
// before their store). Both FCOM tests are written as the comparison they
// mean, NaN included.

constexpr std::uintptr_t kInfoConstruct = 0x00414210;   // (this)
constexpr std::uintptr_t kSpawnCells = 0x00518DC0;      // (cells out, kind), ret 8
constexpr std::uintptr_t kRandomBetween = 0x00913761;   // (lo, hi doubles) -> double
constexpr std::uintptr_t kRandomBelow = 0x009136B1;     // (n) -> [0, n)
constexpr std::uintptr_t kTurnPoint = 0x005D77A0;       // (centre, point, angle, out) cdecl
constexpr std::uintptr_t kPickClass = 0x00516010;       // ()
constexpr std::uintptr_t kAutomatonManager = 0x0050CCA0; // (class) cdecl
constexpr std::uintptr_t kLotService = 0x00B43D08;      // the lot manager service
constexpr std::uintptr_t kRoundMagic = 0x00A95D58;      // float 1.5 x 2^23
constexpr std::uintptr_t kSpawnMost = 0x00A95D50;       // uint32 25
constexpr std::uintptr_t kTwoToThe32 = 0x00A80AA8;      // float 2^32
constexpr std::uintptr_t kZeroDouble = 0x00A80990;      // double 0
constexpr std::uintptr_t kTwoPi = 0x00A96018;           // double 2 pi
constexpr std::uintptr_t kCellSize = 0x00A96010;        // double 16
constexpr std::uintptr_t kFarPoint = 0x00A949E4;        // float 10000
constexpr std::uintptr_t kOne = 0x00B09A14;             // float 1
constexpr std::uintptr_t kDirectionY = 0x00A81054;      // float 0
constexpr std::uintptr_t kFrontOffset = 0x00A81228;     // float 1
constexpr std::uintptr_t kHalf = 0x00A84D2C;            // float 0.5
constexpr std::uint32_t kIidOccupant = 0x26BB767E, kIidAutomaton = 0xA9B40F05;

// The stream's slots.
namespace stream {
inline constexpr std::size_t kInt16 = 0x18, kUint16 = 0x1C, kUint32 = 0x24, kFloat32 = 0x30, kString = 0x3C;
inline constexpr std::size_t kError = 0x48;
} // namespace stream

std::uint32_t NoError(void* s) { return Virtual<std::uint8_t>(s, stream::kError) == 0 ? 1u : 0u; }

// A version read into the stack slot of the stream argument, as the game
// does: a failed read leaves the slot's low word as the stream's address.
std::uint16_t ReadVersion(void* s)
{
    std::uint32_t slot = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(s));
    Virtual<std::uint8_t>(s, stream::kUint16, &slot);
    return static_cast<std::uint16_t>(slot);
}

// ------------------------------------------------------------------------------------
// The save

// 0x00517FD0, the +4 serializable slot 3 (base; the two Writes call it),
// ret 4.
std::uint32_t __fastcall SerializableWrite(char* part, void*, void* s)
{
    static bool logged;
    FirstCall("serializable Write", logged);
    char* const self = part - field::kSerializable;
    Virtual<std::uint8_t>(s, stream::kUint16, 5u);
    CallCdecl<std::uint8_t>(addr::SC4Stream_WriteReference, s, At<void*>(self, field::kOccupant));
    if (char* const t = static_cast<char*>(Template(self))) {
        Virtual<std::uint8_t>(s, stream::kString, t + 4);
    } else {
        // An empty cRZBaseString on the stack: its vtable, the byte vector
        // (8 bytes) and its reference count.
        struct {
            std::uintptr_t vtable;
            char* begin;
            char* end;
            char* capacity;
            std::uint32_t references;
        } empty{};
        char* const buffer = static_cast<char*>(Allocate(8));
        empty.begin = buffer;
        empty.end = buffer;
        empty.capacity = buffer + 8;
        buffer[0] = 0;
        empty.vtable = addr::SC4String_Vtable;
        empty.references = 0;
        Virtual<std::uint8_t>(s, stream::kString, &empty);
        empty.vtable = addr::SC4String_Vtable;
        if (empty.begin)
            Free(empty.begin);
    }
    CallCdecl<void*>(addr::SC4Stream_WriteVector3, s, self + field::kX);
    Virtual<std::uint8_t>(s, stream::kFloat32, At<std::uint32_t>(self, field::k50));
    Virtual<std::uint8_t>(s, stream::kFloat32, At<std::uint32_t>(self, field::k54));
    Virtual<std::uint8_t>(s, stream::kUint32, At<std::uint32_t>(self, field::k3C));
    Virtual<std::uint8_t>(s, stream::kUint32, At<std::uint32_t>(self, field::kFlags));
    Virtual<std::uint8_t>(s, stream::kUint32, At<std::uint32_t>(self, field::kAutomataCount));
    for (ListNode* e = At<ListNode*>(self, field::kAutomata)->next; e != At<ListNode*>(self, field::kAutomata);
         e = e->next)
        CallCdecl<std::uint8_t>(addr::SC4Stream_WriteReference, s, e->value);
    return NoError(s);
}

// 0x00518230, the attractor's: the base Write, version 1, its curve.
std::uint32_t __fastcall AttractorSerializableWrite(char* part, void*, void* s)
{
    static bool logged;
    FirstCall("attractor serializable Write", logged);
    CallThis<std::uint32_t>(addr::cSC4AutomataController_SerializableWrite, part, s);
    Virtual<std::uint8_t>(s, stream::kUint16, 1u);
    CallCdecl<void*>(addr::SC4Save_F5DB190, s, part - field::kSerializable + field::kCurve);
    return NoError(s);
}

// 0x005183D0, the generator's: version 4, the base Write, its clock pairs and
// +0x90, +0x94.
std::uint32_t __fastcall GeneratorSerializableWrite(char* part, void*, void* s)
{
    static bool logged;
    FirstCall("generator serializable Write", logged);
    char* const self = part - field::kSerializable;
    Virtual<std::uint8_t>(s, stream::kUint16, 4u);
    CallThis<std::uint32_t>(addr::cSC4AutomataController_SerializableWrite, part, s);
    Virtual<std::uint8_t>(s, stream::kFloat32, At<std::uint32_t>(self, field::kGen78));
    Virtual<std::uint8_t>(s, stream::kInt16, static_cast<std::uint32_t>(At<std::uint16_t>(self, field::kGen7C)));
    Virtual<std::uint8_t>(s, stream::kFloat32, At<std::uint32_t>(self, field::kGen80));
    Virtual<std::uint8_t>(s, stream::kInt16, static_cast<std::uint32_t>(At<std::uint16_t>(self, field::kGen84)));
    Virtual<std::uint8_t>(s, stream::kUint32, At<std::uint32_t>(self, field::kGen90));
    Virtual<std::uint8_t>(s, stream::kUint32, At<std::uint32_t>(self, field::kGen94));
    return NoError(s);
}

// 0x005180E0, the +4 serializable slot 4 (base), ret 4.
std::uint32_t __fastcall SerializableRead(char* part, void*, void* s)
{
    static bool logged;
    FirstCall("serializable Read", logged);
    char* const self = part - field::kSerializable;
    ReadVersion(s);
    if (void* const occupant = At<void*>(self, field::kOccupant)) {
        At<void*>(self, field::kOccupant) = nullptr;
        Virtual<std::uint32_t>(occupant, slot::kRelease);
    }
    CallCdecl<std::uint8_t>(addr::SC4Stream_ReadReference, s, kIidOccupant, self + field::kOccupant, 0u);
    Virtual<std::uint8_t>(s, stream::kString, self + field::kName);
    CallCdecl<void*>(addr::SC4Stream_ReadVector3, s, self + field::kX);
    Virtual<std::uint8_t>(s, stream::kFloat32, self + field::k50);
    Virtual<std::uint8_t>(s, stream::kFloat32, self + field::k54);
    Virtual<std::uint8_t>(s, stream::kUint32, self + field::k3C);
    Virtual<std::uint8_t>(s, stream::kUint32, self + field::kFlags);
    Virtual<std::uint8_t>(s, stream::kUint32, self + field::kAutomataCount);
    for (std::uint32_t i = 0; i < At<std::uint32_t>(self, field::kAutomataCount); ++i) {
        void* automaton = nullptr;
        CallCdecl<std::uint8_t>(addr::SC4Stream_ReadReference, s, kIidAutomaton, &automaton, 0u);
        ListNode* const head = At<ListNode*>(self, field::kAutomata);
        ListNode* const node = static_cast<ListNode*>(Allocate(sizeof(ListNode)));
        node->value = automaton;
        ListNode* const last = head->prev;
        node->next = head;
        node->prev = last;
        last->next = node;
        head->prev = node;
    }
    return NoError(s);
}

// 0x00518270, the attractor's: the base Read, a version, its curve.
std::uint32_t __fastcall AttractorSerializableRead(char* part, void*, void* s)
{
    static bool logged;
    FirstCall("attractor serializable Read", logged);
    CallThis<std::uint32_t>(addr::cSC4AutomataController_SerializableRead, part, s);
    ReadVersion(s);
    CallCdecl<void*>(addr::SC4Save_F5DB5D0, s, part - field::kSerializable + field::kCurve);
    return NoError(s);
}

// 0x00518450, the generator's: version 4 or 0, the base Read, its clock pairs
// and +0x90, +0x94.
std::uint32_t __fastcall GeneratorSerializableRead(char* part, void*, void* s)
{
    static bool logged;
    FirstCall("generator serializable Read", logged);
    char* const self = part - field::kSerializable;
    if (ReadVersion(s) != 4)
        return 0;
    CallThis<std::uint32_t>(addr::cSC4AutomataController_SerializableRead, part, s);
    Virtual<std::uint8_t>(s, stream::kFloat32, self + field::kGen78);
    Virtual<std::uint8_t>(s, stream::kInt16, self + field::kGen7C);
    Virtual<std::uint8_t>(s, stream::kFloat32, self + field::kGen80);
    Virtual<std::uint8_t>(s, stream::kInt16, self + field::kGen84);
    Virtual<std::uint8_t>(s, stream::kUint32, self + field::kGen90);
    Virtual<std::uint8_t>(s, stream::kUint32, self + field::kGen94);
    return NoError(s);
}

// ------------------------------------------------------------------------------------
// The rest of the behaviour

// 0x00517EF0, slot 19 (value), ret 4.
void __fastcall SetLimit(char* self, void*, std::uint32_t value)
{
    static bool logged;
    FirstCall("SetLimit", logged);
    At<std::uint32_t>(self, field::k54) = value;
}

// 0x00514E30, slot 26: the template's flags and flag 2 set, +0x54 from the
// template's +0x4C, +0x58 zeroed.
void __fastcall ResetAccumulators(char* self, void*)
{
    static bool logged;
    FirstCall("ResetAccumulators", logged);
    if (!Template(self))
        return;
    Virtual<void>(self, slot::kSetFlag, At<std::uint32_t>(Template(self), tmpl::kFlags), 1u);
    Virtual<void>(self, slot::kSetFlag, kFlag2, 1u);
    At<std::uint32_t>(self, field::k54) = At<std::uint32_t>(Template(self), tmpl::kSaturationLimit);
    At<std::uint32_t>(self, field::k58) = 0;
}

// 0x0051BCA0, slot 29 (base, the generator's; automaton, kind), ret 8: kind 4
// adds the automaton to the manager's record of the occupant. AL 0.
std::uint32_t __fastcall AutomatonEventB(char* self, void*, void* automaton, std::uint32_t kind)
{
    static bool logged;
    FirstCall("AutomatonEventB", logged);
    if (kind == 4) {
        char* const manager = Virtual<char*>(City(), 0xBC);
        AutomatonValue* const value =
            CallThis<AutomatonValue*>(kAutomatonRecord, manager, At<void*>(self, field::kOccupant), 1u);
        if (value)
            CallThis<void>(kAutomatonAdd, value, automaton);
    }
    return 0;
}

// 0x0051C030, the attractor's slot 30 (automaton, kind), ret 8: out of its
// own list (released), then the base event 30.
std::uint32_t __fastcall AttractorEventC(char* self, void*, void* automaton, std::uint32_t kind)
{
    static bool logged;
    FirstCall("attractor AutomatonEventC", logged);
    ListNode* const head = At<ListNode*>(self, field::kAutomata);
    if (head->next != head) {
        ListNode* e = head->next;
        while (e != head && e->value != automaton)
            e = e->next;
        if (e != head) {
            ListNode* const next = e->next;
            ListNode* const prev = e->prev;
            prev->next = next;
            next->prev = prev;
            Free(e);
            --At<std::uint32_t>(self, field::kAutomataCount);
            Virtual<std::uint32_t>(automaton, slot::kRelease);
        }
    }
    return CallThis<std::uint32_t>(addr::cSC4AutomataController_AutomatonEventC, self, automaton, kind);
}

// 0x00516C40, slot 22 (out), ret 4: the middle of the occupant's lot's front
// edge, one metre out, by the lot's facing.
void __fastcall PositionFromBox(char* self, void*, float* out)
{
    static bool logged;
    FirstCall("PositionFromBox", logged);
    // A generator the game seeds and never draws from; the seed still counts
    // at the game's site.
    alignas(4) char unused[12];
    {
        const fixed_seed::SiteOverride site(0x00516C51);
        CallThis<void>(addr::SC4Random_Construct, unused, static_cast<std::uint32_t>(0xFFFFFFFF));
    }
    if (!At<void*>(self, field::kOccupant)) {
        out[0] = At<float>(self, field::kX);
        out[1] = At<float>(self, field::kHeight);
        out[2] = At<float>(self, field::kZ);
        return;
    }
    void* const lots = Virtual<void*>(City(), 0x84); // GetLotManager
    void* lot = nullptr;
    if (lots && At<void*>(self, field::kOccupant))
        lot = Virtual<void*>(lots, 0x38, At<void*>(self, field::kOccupant));
    if (!lot) {
        Virtual<void>(At<void*>(self, field::kOccupant), 0x20, out); // GetPosition
        return;
    }
    std::int32_t bounds[4] = {};
    Virtual<void>(lot, 0x38, bounds);
    const auto metres = [](std::int32_t cell) {
        return static_cast<float>(static_cast<std::int32_t>(static_cast<std::uint32_t>(cell) << 4));
    };
    const float x0 = metres(bounds[0]), z0 = metres(bounds[1]);
    const float x1 = metres(bounds[2] + 1), z1 = metres(bounds[3] + 1);
    const double offset = static_cast<double>(Global<float>(kFrontOffset));
    const float zero = AsFloat(Global<std::uint32_t>(addr::SC4_ZeroWord)); // 0x00B227CC, as the game
    // The x87 stack's two values (st1 loaded first) and the two y/z pairs.
    double st1 = zero, st0 = zero;
    float ay = zero, az = zero, by = zero, bz = zero;
    switch (Virtual<std::uint32_t>(lot, 0x5C)) {
    case 0:
        st1 = x0;
        st0 = x1;
        ay = by = 0.0f;
        az = bz = static_cast<float>(static_cast<double>(z0) - offset);
        break;
    case 1: {
        const double x = static_cast<double>(x1) + offset;
        st1 = x;
        st0 = static_cast<float>(x);
        ay = by = 0.0f;
        az = z0;
        bz = z1;
        break;
    }
    case 2:
        st1 = x1;
        st0 = x0;
        ay = by = 0.0f;
        az = bz = static_cast<float>(static_cast<double>(z1) + offset);
        break;
    case 3: {
        const double x = static_cast<double>(x0) - offset;
        st1 = x;
        st0 = static_cast<float>(x);
        ay = by = 0.0f;
        az = z1;
        bz = z0;
        break;
    }
    default: break;
    }
    const double half = static_cast<double>(Global<float>(kHalf));
    const float dy = static_cast<float>(static_cast<double>(by) - static_cast<double>(ay));
    const float dz = static_cast<float>(static_cast<double>(bz) - static_cast<double>(az));
    const float hy = static_cast<float>(static_cast<double>(dy) * half);
    const float hz = static_cast<float>(static_cast<double>(dz) * half);
    out[0] = static_cast<float>((st0 - st1) * half + st1);
    out[1] = static_cast<float>(static_cast<double>(hy) + static_cast<double>(ay));
    out[2] = static_cast<float>(static_cast<double>(hz) + static_cast<double>(az));
}

// The spawn's count rounding: the float sum's bits less 0x4B400000.
std::uint32_t RoundCount(double value)
{
    return AsBits(static_cast<float>(value + static_cast<double>(Global<float>(kRoundMagic)))) + 0xB4C00000u;
}

// 0x005198A0, the generator's slot 41 (unused, curve), ret 8.
void __fastcall Spawn(char* self, void*, void*, std::uint32_t curveBits)
{
    static bool logged;
    FirstCall("generator Spawn", logged);
    void* const city = City();
    char* const t = static_cast<char*>(Template(self)); // not tested, as in the game
    const double curve = static_cast<double>(AsFloat(curveBits));
    const double perUnit = static_cast<double>(At<float>(t, 0x88));
    std::uint32_t count = 0;
    switch (At<std::uint32_t>(t, 0x8C)) {
    case 0: {
        const std::uint32_t rounded = RoundCount(curve * perUnit);
        count = rounded < 1 ? 1 : rounded;
        break;
    }
    case 1: {
        void* const occupant = At<void*>(self, field::kOccupant);
        if (!occupant || Virtual<std::uint32_t>(occupant, 0x1C) != 0x278128A0)
            return;
        void* const simulator = Virtual<void*>(City(), 0x110);
        // A record the simulator fills (0x40 bytes in the game's frame); its
        // first 0x14 bytes are a cRZBaseString, its +0x24 the value.
        alignas(8) char info[0x40];
        CallThis<void*>(kInfoConstruct, info);
        const auto unsignedValue = [&info] {
            const std::int32_t value = At<std::int32_t>(info, 0x24);
            double d = static_cast<double>(value);
            if (value < 0)
                d += static_cast<double>(Global<float>(kTwoToThe32));
            return d;
        };
        if (Virtual<std::uint8_t>(At<void*>(self, field::kOccupant), 0x58, 0x1503u) ||
            Virtual<std::uint8_t>(At<void*>(self, field::kOccupant), 0x58, 0x1504u)) {
            Virtual<void>(simulator, 0x68, At<void*>(self, field::kOccupant), static_cast<void*>(info));
            count = RoundCount(unsignedValue() * perUnit);
        } else if (Virtual<std::uint8_t>(At<void*>(self, field::kOccupant), 0x58, 0x1507u)) {
            Virtual<void>(simulator, 0x6C, At<void*>(self, field::kOccupant), static_cast<void*>(info));
            count = RoundCount(unsignedValue() * perUnit);
        } else {
            std::uint32_t cell[4] = {};
            Virtual<void>(At<void*>(self, field::kOccupant), 0x2C, static_cast<void*>(cell));
            void* const lot = Virtual<void*>(Global<void*>(kLotService), 0x18, cell[0], cell[1], 0u);
            if (lot) {
                std::uint32_t scratch[4] = {};
                const float* const value = Virtual<const float*>(lot, 0xF4, static_cast<void*>(scratch));
                count = RoundCount(perUnit * static_cast<double>(*value));
            }
        }
        At<std::uintptr_t>(info, 0) = addr::SC4String_Vtable;
        if (void* const begin = At<void*>(info, 4))
            Free(begin);
        break;
    }
    case 2: {
        void* const simulator = Virtual<void*>(city, 0x110);
        const std::int32_t value = Virtual<std::int32_t>(simulator, 0x40);
        count = RoundCount(static_cast<double>(value) * perUnit);
        break;
    }
    default: return;
    }
    if (count == 0)
        return;
    if (count > 0x19)
        count = Global<std::uint32_t>(kSpawnMost);
    float position[3], point[3];
    const float zero = AsFloat(Global<std::uint32_t>(addr::SC4_ZeroWord)); // 0x00B227CC, as the game
    position[0] = position[1] = position[2] = zero;
    point[0] = point[1] = point[2] = zero;
    char* const manager = Virtual<char*>(city, 0xBC); // GetAutomataControllerManager
    if (!manager)
        return;
    if (Virtual<std::uint8_t>(self, slot::kTestFlag, 0x10u))
        Virtual<void>(self, slot::kPositionFromBox, position);
    else
        Virtual<void>(self, slot::kPosition, position);
    float centre[2] = {position[0], position[2]};
    struct {
        char* begin;
        char* end;
        char* capacity;
    } cells{};
    std::uint32_t networkKind = 0;
    if (Virtual<std::uint8_t>(self, slot::kTestFlag, 0x80u)) {
        networkKind = 0xC49;
        CallThis<void>(kSpawnCells, self, static_cast<void*>(&cells), 0xC49u);
        if (cells.begin == cells.end) {
            if (cells.begin)
                Free(cells.begin);
            return;
        }
    } else if (Virtual<std::uint8_t>(self, slot::kTestFlag, 0x800u)) {
        networkKind = 2;
        CallThis<void>(kSpawnCells, self, static_cast<void*>(&cells), 2u);
        if (cells.begin == cells.end) {
            if (cells.begin)
                Free(cells.begin);
            return;
        }
    }
    std::uint32_t limit = 0xFFFFFFFF;
    const std::uint32_t own = At<std::uint32_t>(self, field::kGen98);
    if (own > 1)
        limit = own;
    else if (const std::uint32_t finished = At<std::uint32_t>(t, 0x54))
        limit = finished;
    char* const random = self + field::kRandom;
    const double one = static_cast<double>(Global<float>(kOne));
    float radius = 0.0f; // the game's slot is unset until the first place
    for (std::uint32_t i = 0; i < count; ++i) {
        if (At<std::uint32_t>(self, field::kAutomataCount) > limit)
            break;
        if (cells.begin == cells.end) {
            const float angle = static_cast<float>(
                CallThis<double>(kRandomBetween, random, Global<double>(kZeroDouble), Global<double>(kTwoPi)));
            radius = static_cast<float>(CallThis<double>(kRandomBetween, random, static_cast<double>(At<float>(t, 0x20)),
                                                         static_cast<double>(At<float>(t, 0xA8))));
            float turned[2] = {
                static_cast<float>(static_cast<double>(centre[0]) + static_cast<double>(Global<float>(kFarPoint))),
                centre[1]};
            CallCdecl<void>(kTurnPoint, centre, turned, AsBits(angle), turned);
            const double dx = static_cast<double>(turned[0]) - static_cast<double>(centre[0]);
            const double dz = static_cast<double>(turned[1]) - static_cast<double>(centre[1]);
            const double inverse = one / std::sqrt(dz * dz + dx * dx);
            const float direction[3] = {static_cast<float>(inverse * dx),
                                        static_cast<float>(static_cast<double>(Global<float>(kDirectionY)) * inverse),
                                        static_cast<float>(dz * inverse)};
            float crossing[2] = {};
            const bool crossed = CallThis<std::uint8_t>(kSegmentBox, self, centre, turned, crossing) != 0;
            const double rx = static_cast<double>(direction[0]) * static_cast<double>(radius);
            const float ry = static_cast<float>(static_cast<double>(direction[1]) * static_cast<double>(radius));
            const float rz = static_cast<float>(static_cast<double>(direction[2]) * static_cast<double>(radius));
            double x, y;
            if (crossed) {
                x = rx + static_cast<double>(crossing[0]);
                y = static_cast<double>(ry) + static_cast<double>(position[1]);
                point[2] = static_cast<float>(static_cast<double>(rz) + static_cast<double>(crossing[1]));
            } else {
                x = rx + static_cast<double>(position[0]);
                y = static_cast<double>(ry) + static_cast<double>(position[1]);
                point[2] = static_cast<float>(static_cast<double>(rz) + static_cast<double>(position[2]));
            }
            point[0] = static_cast<float>(x);
            point[1] = static_cast<float>(y);
            if (Virtual<std::uint8_t>(city, 0x188, point[0], point[2])) // LocationIsInBounds
                point[1] = Virtual<float>(Global<void*>(addr::SC4_Terrain), 0x34, point[0], point[2]);
            else
                point[1] = position[1];
        } else {
            const std::uint32_t n =
                static_cast<std::uint32_t>(static_cast<std::int32_t>(cells.end - cells.begin) >> 3);
            const std::uint32_t pick = CallThis<std::uint32_t>(kRandomBelow, random, n);
            const std::int32_t* const cell = reinterpret_cast<const std::int32_t*>(cells.begin + pick * 8);
            const std::int32_t cellX = cell[0], cellZ = cell[1];
            const double size = Global<double>(kCellSize);
            const double first = CallThis<double>(addr::cRZRandom_NextFloat, random);
            const float x = static_cast<float>(
                first * size + static_cast<double>(static_cast<std::int32_t>(static_cast<std::uint32_t>(cellX) << 4)));
            point[0] = x;
            const double second = CallThis<double>(addr::cRZRandom_NextFloat, random);
            const double z =
                second * size + static_cast<double>(static_cast<std::int32_t>(static_cast<std::uint32_t>(cellZ) << 4));
            point[2] = static_cast<float>(z);
            float inside[2] = {x, static_cast<float>(z)};
            float crossing[2] = {0.0f, 0.0f};
            if (CallThis<std::uint8_t>(kSegmentBox, self, centre, inside, crossing)) {
                const double distance = CallCdecl<double>(addr::SC4_Distance2D, inside, crossing);
                const float nearest = At<float>(t, 0x20);
                const float farthest = At<float>(t, 0xA8);
                const float* bound = nullptr;
                if (distance < static_cast<double>(nearest))
                    bound = &At<float>(t, 0x20);
                else if (distance > static_cast<double>(farthest))
                    bound = &At<float>(t, 0xA8);
                if (bound) {
                    const double ddx = static_cast<double>(inside[0]) - static_cast<double>(crossing[0]);
                    const double ddz = static_cast<double>(inside[1]) - static_cast<double>(crossing[1]);
                    const double inverse = one / std::sqrt(ddz * ddz + ddx * ddx);
                    const float ux = static_cast<float>(ddx * inverse);
                    const double uz = ddz * inverse;
                    const double reach = static_cast<double>(*bound);
                    const float sx = static_cast<float>(static_cast<double>(ux) * reach);
                    const double sz = uz * reach;
                    inside[0] = sx;
                    inside[1] = static_cast<float>(sz);
                    point[0] = static_cast<float>(static_cast<double>(sx) + static_cast<double>(crossing[0]));
                    point[2] = static_cast<float>(sz + static_cast<double>(crossing[1]));
                }
            }
            void* const network = Virtual<void*>(City(), 0xA0); // GetTrafficNetwork
            if (!Virtual<std::uint8_t>(network, 0xAC, point, networkKind))
                continue;
            point[1] = Virtual<float>(Virtual<void*>(City(), 0xA0), 0x48, point[0], point[2]);
            const double ex = static_cast<double>(position[0]) - static_cast<double>(point[0]);
            const double ey = static_cast<double>(position[1]) - static_cast<double>(point[1]);
            const double ez = static_cast<double>(position[2]) - static_cast<double>(point[2]);
            radius = static_cast<float>(std::sqrt((ez * ez + ey * ey) + ex * ex));
        }
        if (At<std::uint32_t>(self, field::kGen90) == 0 ||
            !(At<std::uint32_t>(self, field::kGen94) < At<std::uint32_t>(self, field::kGen98)))
            CallThis<void>(kPickClass, self);
        void* const automata = CallCdecl<void*>(kAutomatonManager, At<std::uint32_t>(self, field::kGen90));
        if (!automata)
            break;
        std::uint32_t rotation[3] = {0, 0, 0};
        void* const automaton = Virtual<void*>(automata, 0x3C, At<std::uint32_t>(self, field::kGen90),
                                               static_cast<float*>(point), 0x2000002u, static_cast<void*>(rotation),
                                               0u, static_cast<std::uint32_t>(At<std::uint8_t>(t, 0xB0)), 0u);
        if (!automaton)
            continue;
        void* held = automaton;
        CallThis<void>(addr::SC4StlList_PushBack, self + field::kAutomata, &held);
        Virtual<std::uint32_t>(automaton, slot::kAddRef);
        const std::uint32_t automataCount = At<std::uint32_t>(self, field::kAutomataCount) + 1;
        ++At<std::uint32_t>(self, field::kGen94);
        At<std::uint32_t>(self, field::kAutomataCount) = automataCount;
        Virtual<void>(automaton, 0x9C, manager + automata_ctlmgr::field::kTargetPart);
        void* const spot = Virtual<void*>(self, slot::kPickSpot, AsBits(radius), static_cast<float*>(point));
        if (spot)
            Virtual<void>(automaton, 0xDC, spot, self);
        else
            Virtual<void>(automaton, 0xD0, 0u, self, 0xBF800000u, 0u, 0u, 0u, 0u);
        if (const std::uint32_t cap = At<std::uint32_t>(self, field::kGen8C)) {
            const std::uint32_t made = ++At<std::uint32_t>(self, field::kGen88);
            if (made >= cap)
                break;
        }
    }
    if (cells.begin)
        Free(cells.begin);
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
    OPENSC4_DETOUR(addr::cSC4AutomataController_SerializableQueryInterface, SerializableQueryInterface),
    OPENSC4_DETOUR(addr::cSC4AutomataController_SerializableGetGZCLSID, SerializableGetGZCLSID),
    OPENSC4_DETOUR(addr::cSC4AutomataController_Release, Release),
    OPENSC4_DETOUR(addr::cSC4AutomataAttractor_GetGZCLSID, AttractorGetGZCLSID),
    OPENSC4_DETOUR(addr::cSC4AutomataGenerator_GetGZCLSID, GeneratorGetGZCLSID),
    OPENSC4_DETOUR(addr::cSC4AutomataController_Destroy, Destroy),
    OPENSC4_DETOUR(addr::cSC4AutomataAttractor_Destroy, AttractorDestroy),
    OPENSC4_DETOUR(addr::cSC4AutomataAttractor_DeletingDestructor, AttractorDeletingDestructor),
    OPENSC4_DETOUR(addr::cSC4AutomataController_DeletingDestructor, DeletingDestructor),
    OPENSC4_DETOUR(addr::cSC4AutomataController_CurveValue, CurveValue),
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

const Detour kBehaviour[] = {
    OPENSC4_DETOUR(addr::cSC4AutomataController_Init, Init),
    OPENSC4_DETOUR(addr::cSC4AutomataAttractor_Init, AttractorInit),
    OPENSC4_DETOUR(addr::cSC4AutomataGenerator_Init, GeneratorInit),
    OPENSC4_DETOUR(addr::cSC4AutomataController_Shutdown, Shutdown),
    OPENSC4_DETOUR(addr::cSC4AutomataGenerator_Shutdown, GeneratorShutdown),
    OPENSC4_DETOUR(addr::cSC4AutomataController_SetActive, SetActive),
    OPENSC4_DETOUR(addr::cSC4AutomataGenerator_SetActive, GeneratorSetActive),
    OPENSC4_DETOUR(addr::cSC4AutomataController_SpotSelection, SpotSelection),
    OPENSC4_DETOUR(addr::cSC4AutomataController_CellBounds, CellBounds),
    OPENSC4_DETOUR(addr::cSC4AutomataController_CellTest, CellTest),
    OPENSC4_DETOUR(addr::cSC4AutomataController_OccupantLists, OccupantLists),
    OPENSC4_DETOUR(addr::cSC4AutomataController_FrameStep, FrameStep),
    OPENSC4_DETOUR(addr::cSC4AutomataController_AutomatonEventA, AutomatonEventA),
    OPENSC4_DETOUR(addr::cSC4AutomataAttractor_AutomatonEventB, AttractorEventB),
    OPENSC4_DETOUR(addr::cSC4AutomataController_AutomatonEventC, AutomatonEventC),
    OPENSC4_DETOUR(addr::cSC4AutomataController_AutomatonLeaves, AutomatonLeaves),
    OPENSC4_DETOUR(addr::cSC4AutomataController_PickSpot, PickSpot),
    OPENSC4_DETOUR(addr::cSC4AutomataController_Tick, Tick),
    OPENSC4_DETOUR(addr::cSC4AutomataGenerator_Tick, GeneratorTick),
};

const Detour kSerial[] = {
    OPENSC4_DETOUR(addr::cSC4AutomataController_SerializableWrite, SerializableWrite),
    OPENSC4_DETOUR(addr::cSC4AutomataAttractor_SerializableWrite, AttractorSerializableWrite),
    OPENSC4_DETOUR(addr::cSC4AutomataGenerator_SerializableWrite, GeneratorSerializableWrite),
    OPENSC4_DETOUR(addr::cSC4AutomataController_SerializableRead, SerializableRead),
    OPENSC4_DETOUR(addr::cSC4AutomataAttractor_SerializableRead, AttractorSerializableRead),
    OPENSC4_DETOUR(addr::cSC4AutomataGenerator_SerializableRead, GeneratorSerializableRead),
};

const Detour kSpawn[] = {
    OPENSC4_DETOUR(addr::cSC4AutomataGenerator_Spawn, Spawn),
    OPENSC4_DETOUR(addr::cSC4AutomataController_PositionFromBox, PositionFromBox),
    OPENSC4_DETOUR(addr::cSC4AutomataController_ResetAccumulators, ResetAccumulators),
    OPENSC4_DETOUR(addr::cSC4AutomataController_AutomatonEventB, AutomatonEventB),
    OPENSC4_DETOUR(addr::cSC4AutomataAttractor_AutomatonEventC, AttractorEventC),
    OPENSC4_DETOUR(addr::cSC4AutomataController_SetLimit, SetLimit),
};

#undef OPENSC4_DETOUR

bool InstallController(registry::Mode)
{
    return InstallList(kController, sizeof kController / sizeof kController[0], "controller");
}

bool InstallBehaviour(registry::Mode)
{
    return InstallList(kBehaviour, sizeof kBehaviour / sizeof kBehaviour[0], "behaviour");
}

bool InstallSerial(registry::Mode)
{
    return InstallList(kSerial, sizeof kSerial / sizeof kSerial[0], "serial");
}

bool InstallSpawn(registry::Mode)
{
    return InstallList(kSpawn, sizeof kSpawn / sizeof kSpawn[0], "spawn");
}

const bool g_registered[] = {
    registry::Register({"simulators.automata.controller", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4AutomataController_Construct,
                        "Our automata controllers: the object (the constructors, the destructor bodies, the deleting "
                        "destructors and the two factories; QueryInterface and Release), the queries (the "
                        "accessors, the flag tests, IsActive, IsFinished, IsSaturated and GetPosition), CurveValue, "
                        "and the +4 serializable part's identity (QueryInterface and GetGZCLSID) of cSC4AutomataController, cSC4AutomataAttractor and "
                        "cSC4AutomataGenerator (32 detours; src/core/automata_controller.cpp)",
                        false, InstallController}),
    registry::Register({"simulators.automata.ctlbehaviour", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4AutomataController_Init,
                        "Our automata controllers' behaviour: Init, Shutdown and SetActive (base, attractor and "
                        "generator), the base slots the runs reach (spot selection, cell bounds, the cell test, the "
                        "template's occupant lists, the frame/step handler), the automaton events (28, the "
                        "attractor's 29, 30, 31, 32) and the tick (base and generator) of cSC4AutomataController, "
                        "cSC4AutomataAttractor and cSC4AutomataGenerator (19 detours; part a3b, "
                        "src/core/automata_controller.cpp)",
                        false, InstallBehaviour}),
    registry::Register({"simulators.automata.ctlserial", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4AutomataController_SerializableWrite,
                        "Our automata controllers' save: the +4 serializable part's Write and Read (base, attractor "
                        "and generator) of cSC4AutomataController, cSC4AutomataAttractor and cSC4AutomataGenerator "
                        "(6 detours; part a3c, src/core/automata_controller.cpp)",
                        false, InstallSerial}),
    registry::Register({"simulators.automata.ctlspawn", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4AutomataGenerator_Spawn,
                        "Our automata controllers' remaining behaviour: the generator's spawn, the position from "
                        "the occupant's lot, resetting the tick's accumulators, the base automaton event 29, the "
                        "attractor's event 30 and SetLimit of cSC4AutomataController, cSC4AutomataAttractor and "
                        "cSC4AutomataGenerator (6 detours; part a3c, src/core/automata_controller.cpp)",
                        false, InstallSpawn}),
};

} // namespace
} // namespace opensc4::automata_controller
