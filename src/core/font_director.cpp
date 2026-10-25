// Our director of the framework's fonts, f1 of docs/fonts.md ("f1 and f5
// are ours"), 0x00895F36 to 0x0089604D (less 0x0089603D, which is f5's):
// the COM director (vtables 0x00AC5548 and 0x00AC5518) that registers the
// font system, the font and the glyph blitter, its three factories, its
// static getter and its slot 4, which makes the font system and gives it to
// the framework; and the one-byte static next to it. Each is a detour on the
// game's function, in one group, off by default and installed at Attach:
//
//   framework.font.director  the factories of the font system (0x00895F36),
//                            the font (0x00895F4C) and the blitter
//                            (0x00895F65), the director's constructor
//                            (0x00895F7B), its ID (slot 13, 0x00895FC7), its
//                            start (slot 4, 0x00895FCD), the static getter
//                            (0x00896012) and the byte's constructor
//                            (0x0089604D)
//
// The classes' constructors, the director base (cRZCOMDllDirector) and the
// framework are the game's, called at the game's addresses; the factories
// the director registers are the game's addresses too, as the game's
// constructor registers them.
//
// Compare mode runs ours and then the game's where both can run: the ID,
// the getter once the director is made, and the byte. The factories make
// objects, the constructor registers classes and the start adds a service:
// those run the game's alone.
#include "core/address_check.h"
#include "core/compare.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/thunk.h"

#include <opensc4/addresses.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace opensc4::fontdirector {
namespace {

// ====================================================================================
// Helpers

using Address = uint32_t;

inline Address At(const void* p) { return static_cast<Address>(reinterpret_cast<uintptr_t>(p)); }
inline void* Ptr(Address at) { return reinterpret_cast<void*>(static_cast<uintptr_t>(at)); }

template <typename T>
inline T Load(Address at)
{
    T value;
    std::memcpy(&value, Ptr(at), sizeof value);
    return value;
}

template <typename T>
inline void Store(Address at, T value)
{
    std::memcpy(Ptr(at), &value, sizeof value);
}

template <typename R, typename... Args>
inline R Virtual(void* object, std::size_t byteOffset, Args... args)
{
    void** const vtable = *reinterpret_cast<void***>(object);
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(vtable[byteOffset / 4])(object, args...);
}

template <typename Fn>
inline Fn Game(std::uintptr_t address)
{
    return reinterpret_cast<Fn>(address);
}

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("fontdirector: first %s", name);
}

#define OPENSC4_FIRST(name)                                                                                    \
    do {                                                                                                       \
        static bool logged_ = false;                                                                           \
        if (!logged_)                                                                                          \
            FirstCall(name, logged_);                                                                          \
    } while (0)

using ThisOnly = void*(__fastcall*)(void*, void*);

// ====================================================================================
// The director

// The three classes: ID, size and constructor.
constexpr uint32_t kFontSystemClass = 0x20673960;
constexpr uint32_t kFontClass = 0x1AA404AC;
constexpr uint32_t kBlitterClass = 0x08CDA30B;
constexpr uint32_t kFontSystemSize = 0x70;
constexpr uint32_t kFontSize = 0x15C;
constexpr uint32_t kBlitterSize = 8;

// The interface the font system is asked for when the director starts.
constexpr uint32_t kFontSystemInterface = 0x287FB697;
constexpr uint32_t kDirectorId = 0x5AA40BD3;

// The director's two vtables (the COM director's and the framework hooks').
constexpr Address kVtable = 0x00AC5548;
constexpr Address kHooksVtable = 0x00AC5518;

// The static director (0x4C bytes), the word whose bit 0 says it is made,
// and its teardown at exit.
constexpr Address kDirector = 0x00B543C8;
constexpr Address kMade = 0x00B54414;
constexpr Address kTeardown = 0x00A7F355;

// The director's slot 12 (+0x30) is its COM object; cIGZCOM's slot 3
// (+0xC) makes an object of a class with an interface; the framework's slot
// 3 (+0xC) adds a system service.
constexpr std::size_t kDirectorCom = 0x30;
constexpr std::size_t kComGetClassObject = 0x0C;
constexpr std::size_t kFrameworkAddService = 0x0C;
constexpr std::size_t kRelease = 0x08;

// The game's 0x00895F36, 0x00895F4C and 0x00895F65 (cdecl): the object
// allocated and constructed; null if the allocation fails.
void* Make(uint32_t size, std::uintptr_t construct)
{
    void* const space = Game<void*(__cdecl*)(uint32_t)>(addr::SC4_operator_new)(size);
    if (!space)
        return nullptr;
    return Game<ThisOnly>(construct)(space, nullptr);
}

void* MakeFontSystem() { return Make(kFontSystemSize, addr::GZFontSystem_Construct); }
void* MakeFont() { return Make(kFontSize, addr::GZFont_Construct); }
void* MakeBlitter() { return Make(kBlitterSize, addr::GZFontBlitter_Construct); }

// The game's 0x00895F7B (thiscall): the director base, the two vtables, then
// the three classes with the game's factories. Returns this.
void* Construct(void* self)
{
    Game<ThisOnly>(addr::cRZCOMDllDirector_Construct)(self, nullptr);
    Store<Address>(At(self), kVtable);
    Store<Address>(At(self) + 4, kHooksVtable);
    const auto addClass = Game<uint32_t(__fastcall*)(void*, void*, uint32_t, Address)>(addr::cRZCOMDllDirector_AddCls);
    addClass(self, nullptr, kFontSystemClass, static_cast<Address>(addr::GZFontDirector_MakeFontSystem));
    addClass(self, nullptr, kFontClass, static_cast<Address>(addr::GZFontDirector_MakeFont));
    addClass(self, nullptr, kBlitterClass, static_cast<Address>(addr::GZFontDirector_MakeBlitter));
    return self;
}

// The game's 0x00895FC7 (slot 13, GetDirectorID): the director's ID.
uint32_t DirectorId() { return kDirectorId; }

// The game's 0x00895FCD (slot 4, ret 4): the font system made through the
// director's COM object (its slot 12; the COM object it is handed is not
// used) and, if there is one, given to the framework as a
// service and released. Returns true.
bool OnStart(void* self)
{
    void* system = nullptr;
    void* const com = Virtual<void*>(self, kDirectorCom);
    Virtual<uint8_t>(com, kComGetClassObject, kFontSystemClass, kFontSystemInterface, &system);
    if (system) {
        void* const framework = Game<void*(__cdecl*)()>(addr::RZGetFramework)();
        Virtual<uint8_t>(framework, kFrameworkAddService, system);
        Virtual<uint32_t>(system, kRelease);
    }
    return true;
}

// The game's 0x00896012 (cdecl): the static director, made the first time
// with its teardown left to atexit.
void* Get()
{
    if ((Load<uint8_t>(kMade) & 1) == 0) {
        Store<uint32_t>(kMade, Load<uint32_t>(kMade) | 1);
        Construct(Ptr(kDirector));
        Game<int(__cdecl*)(Address)>(addr::SC4_atexit)(kTeardown);
    }
    return Ptr(kDirector);
}

// The game's 0x0089604D (thiscall): the byte set to 1; returns this. Its
// only caller is the static initializer 0x00A78FA5, for the byte at
// 0x00B543C2.
void* SetByte(void* self)
{
    Store<uint8_t>(At(self), 1);
    return self;
}

// ====================================================================================
// Detours

bool g_compare = false;

using MakeFn = void*(__cdecl*)();
using ConstructFn = void*(__fastcall*)(void*, void*);
using IdFn = uint32_t(__fastcall*)(void*, void*);
using OnStartFn = uint32_t(__fastcall*)(void*, void*, void*);
using GetFn = void*(__cdecl*)();
MakeFn g_makeFontSystem = nullptr;
MakeFn g_makeFont = nullptr;
MakeFn g_makeBlitter = nullptr;
ConstructFn g_construct = nullptr;
IdFn g_directorId = nullptr;
OnStartFn g_onStart = nullptr;
GetFn g_get = nullptr;
ConstructFn g_setByte = nullptr;

void* __cdecl DMakeFontSystem()
{
    OPENSC4_FIRST("MakeFontSystem 0x00895F36");
    return g_compare ? g_makeFontSystem() : MakeFontSystem();
}

void* __cdecl DMakeFont()
{
    OPENSC4_FIRST("MakeFont 0x00895F4C");
    return g_compare ? g_makeFont() : MakeFont();
}

void* __cdecl DMakeBlitter()
{
    OPENSC4_FIRST("MakeBlitter 0x00895F65");
    return g_compare ? g_makeBlitter() : MakeBlitter();
}

void* __fastcall DConstruct(void* self, void*)
{
    OPENSC4_FIRST("Construct 0x00895F7B");
    return g_compare ? g_construct(self, nullptr) : Construct(self);
}

uint32_t __fastcall DDirectorId(void* self, void*)
{
    OPENSC4_FIRST("DirectorId 0x00895FC7");
    if (!g_compare)
        return DirectorId();
    return compare::Call("fontdirector.id", [&] { return g_directorId(self, nullptr); }, [&] { return DirectorId(); });
}

uint32_t __fastcall DOnStart(void* self, void*, void* unused)
{
    OPENSC4_FIRST("OnStart 0x00895FCD");
    return g_compare ? g_onStart(self, nullptr, unused) : (OnStart(self) ? 1 : 0);
}

void* __cdecl DGet()
{
    OPENSC4_FIRST("Get 0x00896012");
    if (!g_compare)
        return Get();
    if ((Load<uint8_t>(kMade) & 1) == 0)
        return g_get();
    return compare::Call("fontdirector.get", [&] { return g_get(); }, [&] { return Get(); });
}

void* __fastcall DSetByte(void* self, void*)
{
    OPENSC4_FIRST("SetByte 0x0089604D");
    if (!g_compare)
        return SetByte(self);
    const uint8_t before = Load<uint8_t>(At(self));
    void* const result = SetByte(self);
    const uint8_t mine = Load<uint8_t>(At(self));
    Store<uint8_t>(At(self), before);
    void* const game = g_setByte(self, nullptr);
    char theirs[24], ours[24];
    std::snprintf(theirs, sizeof theirs, "%p %02X", game, Load<uint8_t>(At(self)));
    std::snprintf(ours, sizeof ours, "%p %02X", result, mine);
    compare::Record("fontdirector.setbyte", game == result && Load<uint8_t>(At(self)) == mine, theirs, ours);
    return game;
}

OPENSC4_CDECL_THUNK(TMakeFontSystem, DMakeFontSystem, 0)
OPENSC4_CDECL_THUNK(TMakeFont, DMakeFont, 0)
OPENSC4_CDECL_THUNK(TMakeBlitter, DMakeBlitter, 0)
OPENSC4_THUNK(TConstruct, DConstruct, 0)
OPENSC4_THUNK(TDirectorId, DDirectorId, 0)
OPENSC4_THUNK(TOnStart, DOnStart, 4)
OPENSC4_CDECL_THUNK(TGet, DGet, 0)
OPENSC4_THUNK(TSetByte, DSetByte, 0)

// ====================================================================================
// Installation

struct Detour {
    std::uintptr_t address;
    void (*thunk)();
    void** original;
};

template <typename Fn>
void** Slot(Fn* fn)
{
    return reinterpret_cast<void**>(fn);
}

bool Install(registry::Mode mode)
{
    if (!game::IsBuild641())
        return false;
    g_compare = mode == registry::Mode::Compare;
    const Detour detours[] = {
        {addr::GZFontDirector_MakeFontSystem, &TMakeFontSystem, Slot(&g_makeFontSystem)},
        {addr::GZFontDirector_MakeFont, &TMakeFont, Slot(&g_makeFont)},
        {addr::GZFontDirector_MakeBlitter, &TMakeBlitter, Slot(&g_makeBlitter)},
        {addr::GZFontDirector_Construct, &TConstruct, Slot(&g_construct)},
        {addr::GZFontDirector_DirectorId, &TDirectorId, Slot(&g_directorId)},
        {addr::GZFontDirector_OnStart, &TOnStart, Slot(&g_onStart)},
        {addr::GZFontDirector_Get, &TGet, Slot(&g_get)},
        {addr::GZFontStatic_SetByte, &TSetByte, Slot(&g_setByte)},
    };
    for (const Detour& detour : detours) {
        const addr::Entry* entry = addr_check::Find(detour.address);
        if (!entry || !hook::Install(*entry, reinterpret_cast<void*>(detour.thunk), detour.original))
            return false;
    }
    log::Write("fontdirector: %zu functions %s", sizeof detours / sizeof detours[0],
               g_compare ? "compared with the game's" : "ours");
    return true;
}

const bool g_registered = registry::Register(
    {"framework.font.director", "framework/font", registry::Kind::Detour, registry::Phase::Attach,
     addr::GZFontDirector_Construct,
     "Our fonts' director: its three factories, constructor, ID, start, static getter and the byte next to it "
     "(8 detours; docs/fonts.md)",
     true, Install});

} // namespace
} // namespace opensc4::fontdirector
