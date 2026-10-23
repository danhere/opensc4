// The rest of the UI script's root holder 0x5A354595 and the LEGACY reader
// 0xFA450242 (docs/gzui.md, part u11), on the game's own objects. The root
// holder keeps the windows a UI script made in an STLport vector of
// pointers at +8 (begin, end, capacity; a counted allocator at +0x10); part
// u10 has its slots, this part its constructor, slot 4 (a window added),
// destructors and the vector's push, constructor and erase. The LEGACY
// reader (0x20 bytes; the class the `LEGACY` tag makes, interface
// 0x1A356083, constructor 0x0095B8C4) holds the window it reads for at
// +0x10, a value of the parent's interface 0x9A4527CB at +0x14, another
// object at +0x18 and a flag at +0x1C; its faces 0x5A356159 at +4 and
// 0x9A4527CB at +8. Replacements, each off by default:
//
//   framework.uiscript.rootlife  the root holder's constructor, slot 4,
//                                destructor and deleting destructor; the
//                                vector's constructor, push and erase
//   framework.uiscript.legacy    the LEGACY reader's QueryInterface, AddRef,
//                                Release, destructors, the variable table's
//                                getter, the setters and the calls it
//                                passes to its window
//
// Still the game's: the adjustors 0x0094C0DC, 0x0094C0E4 and 0x0094C4B8 to
// 0x0094C4D8 (each a jump into one of ours), the root holder's interim
// vtables' stub 0x005D4A10, and the rest of the range (helpers with
// registers for arguments, other classes' counts and trees; see the doc).
// Ours call each other directly where the game does; a virtual call stays
// one.
//
// Every detour is entered through a thunk that keeps ECX and EDX. The
// groups go in at Attach, with the UI script loader's.
#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/thunk.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::gzui {
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

template <typename R, typename... Args>
inline R Virtual(void* object, std::size_t byteOffset, Args... args)
{
    void** const vtable = *reinterpret_cast<void***>(object);
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(vtable[byteOffset / 4])(object, args...);
}

inline uint32_t Bits(const void* pointer) { return static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(pointer)); }

// A byte answer over what the game leaves in the rest of EAX.
inline uint32_t Answer(uint32_t eax, bool value) { return (eax & ~0xFFu) | (value ? 1u : 0u); }
inline bool Yes(uint32_t eax) { return (eax & 0xFF) != 0; }

inline uint32_t AddRef(void* object) { return Virtual<uint32_t>(object, 0x4); }
inline uint32_t Release(void* object) { return Virtual<uint32_t>(object, 0x8); }

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("gzui: legacy: first %s", name);
}

#define OPENSC4_FIRST(name)                                                                                    \
    do {                                                                                                       \
        static bool logged_ = false;                                                                           \
        if (!logged_)                                                                                          \
            FirstCall(name, logged_);                                                                          \
    } while (0)

// ====================================================================================
// The root holder 0x5A354595 (0x24 bytes): the vtable 0x00AD5248 at +0, the
// interface 0xFA35615F at +4 (0x00AD5234), the vector at +8 (begin, end,
// capacity, +0xC unused, the allocator +0x10, +0x14 unused), the count
// +0x20.

// The game's 0x0094C037 (`ret 4`; `this` the vector): with room the value
// put at the end (not through a null end) and the end moved on, EAX the
// old end; else the insert 0x0094BF3B at the end (its EAX).
std::uintptr_t __fastcall VectorPush(char* self, void*, void* const* value)
{
    OPENSC4_FIRST("VectorPush");
    void** const end = At<void**>(self, 4);
    if (end == At<void**>(self, 8))
        return CallThis<std::uintptr_t>(addr::cGZUIScriptRoot_VectorInsert, self, end, value);
    if (end)
        *end = *value;
    At<void**>(self, 4) = end + 1;
    return reinterpret_cast<std::uintptr_t>(end);
}

// The game's 0x0094C05E (`this` the vector): the fields zero, a counted
// allocator (8 bytes, SC4::operator_new: the vtable 0x00AD1DC0, its count
// zero) held (its slot 1) at +0x10, a block of 8 pointers from its slot 3.
// Without the memory the game calls through null; so does ours.
void* __fastcall VectorConstruct(char* self, void*)
{
    OPENSC4_FIRST("VectorConstruct");
    At<uint32_t>(self, 0) = 0;
    At<uint32_t>(self, 4) = 0;
    At<uint32_t>(self, 8) = 0;
    At<uint32_t>(self, 0xC) = 0;
    At<uint32_t>(self, 0x14) = 0;
    char* const allocator = CallCdecl<char*>(addr::SC4_operator_new, 8u);
    if (allocator) {
        At<std::uintptr_t>(allocator, 0) = addr::cGZUIScriptRoot_AllocatorVtable;
        At<uint32_t>(allocator, 4) = 0;
    }
    At<char*>(self, 0x10) = allocator;
    AddRef(allocator);
    char* const block = Virtual<char*>(At<void*>(self, 0x10), 0xC, 0x20u);
    At<char*>(self, 0) = block;
    At<char*>(self, 4) = block;
    At<char*>(self, 8) = block + 0x20;
    return self;
}

// The game's 0x0094C113 (`ret 8`; `this` the vector): the pointers from
// `last` to the end moved down to `first` (0x00407A50), the end down as
// many; `first` answered.
void** __fastcall VectorErase(char* self, void*, void** first, void** last)
{
    OPENSC4_FIRST("VectorErase");
    CallCdecl<void**>(addr::STLport_CopyPointers, last, At<void**>(self, 4), first);
    At<void**>(self, 4) -= last - first;
    return first;
}

// The game's 0x0094C0AF: the interim vtables 0x00AD51F0 and 0x00AD5220,
// the vector (0x0094C05E), the count zero, the vtables 0x00AD5248 and
// 0x00AD5234.
void* __fastcall RootConstruct(char* self, void*)
{
    OPENSC4_FIRST("RootConstruct");
    At<std::uintptr_t>(self, 0) = addr::cGZUIScriptRoot_InterimVtable;
    At<std::uintptr_t>(self, 4) = addr::cGZUIScriptRoot_InterimPartVtable;
    VectorConstruct(self + 8, nullptr);
    At<uint32_t>(self, 0x20) = 0;
    At<std::uintptr_t>(self, 0) = addr::cGZUIScriptRoot_Vtable;
    At<std::uintptr_t>(self, 4) = addr::cGZUIScriptRoot_PartVtable;
    return self;
}

// The game's 0x0094C0EC (slot 4, `ret 4`): a window held (its slot 1) and
// pushed (0x0094C037), AL 1 over the push's EAX; AL 0 for none, over the
// caller's EAX (ours 0).
uint32_t __fastcall RootAdd(char* self, void*, void* window)
{
    OPENSC4_FIRST("RootAdd");
    if (!window)
        return 0;
    AddRef(window);
    void* const value = window;
    return Answer(static_cast<uint32_t>(VectorPush(self + 8, nullptr, &value)), true);
}

// The game's 0x0094C144: the vtables back, every window released (slot 2;
// the count taken once), the vector emptied (0x0094C113) and a tail jump
// to its destructor SC4AttrVector::Destruct (its EAX).
uint32_t __fastcall RootDestruct(char* self, void*)
{
    OPENSC4_FIRST("RootDestruct");
    char* const vector = self + 8;
    At<std::uintptr_t>(self, 0) = addr::cGZUIScriptRoot_Vtable;
    At<std::uintptr_t>(self, 4) = addr::cGZUIScriptRoot_PartVtable;
    const uint32_t count = static_cast<uint32_t>((At<int32_t>(vector, 4) - At<int32_t>(vector, 0)) >> 2);
    for (uint32_t i = 0; i < count; ++i)
        Release(At<void**>(vector, 0)[i]);
    VectorErase(vector, nullptr, At<void**>(vector, 0), At<void**>(vector, 4));
    return CallThis<uint32_t>(addr::SC4AttrVector_Destruct, vector);
}

// The game's 0x0094C188 (slot 12, `ret 4`): the destructor, freed
// (SC4::operator_delete) with flag 1; `this` answered.
void* __fastcall RootDeletingDestructor(char* self, void*, uint32_t flags)
{
    OPENSC4_FIRST("RootDeletingDestructor");
    RootDestruct(self, nullptr);
    if (flags & 1)
        CallCdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// ====================================================================================
// The LEGACY reader 0xFA450242 (0x20 bytes): the vtable 0x00AD5360 at +0,
// the face 0x5A356159 at +4 (0x00AD5344), the face 0x9A4527CB at +8
// (0x00AD5324), the count +0xC, the window +0x10, the parent's value
// +0x14, an object +0x18, the flag byte +0x1C. The variable table it
// shares with every reader is at 0x00B63588.

constexpr std::size_t kLgCount = 0xC;
constexpr std::size_t kLgWindow = 0x10;
constexpr std::size_t kLgValue = 0x14;
constexpr std::size_t kLgObject = 0x18;
constexpr std::size_t kLgFlag = 0x1C;

inline void*& LgTable() { return *reinterpret_cast<void**>(addr::cGZUIScriptLegacy_Table); }

// The game's 0x0094C431 (slot 0, `ret 8`; also through the adjustors
// 0x0094C4B8 and 0x0094C4C8): 1 and 0x1A356083 give the reader,
// 0x5A356159 its +4 and 0x9A4527CB its +8 (0 for null); AddRef through the
// primary vtable, AL 1 over its EAX. Otherwise AL 0 over the iid.
uint32_t __fastcall LgQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    OPENSC4_FIRST("LgQueryInterface");
    if (iid == 1 || iid == 0x1A356083u)
        *out = self;
    else if (iid == 0x5A356159u)
        *out = self ? self + 4 : nullptr;
    else if (iid == 0x9A4527CBu)
        *out = self ? self + 8 : nullptr;
    else
        return Answer(iid, false);
    return Answer(AddRef(self), true);
}

// The game's 0x0094C47C (slot 1): the count up and answered.
uint32_t __fastcall LgAddRef(char* self, void*)
{
    OPENSC4_FIRST("LgAddRef");
    return ++At<uint32_t>(self, kLgCount);
}

// The game's 0x0094C483 (slot 2; also through 0x0094C4C0 and 0x0094C4D0):
// above 1 the count down and answered; else the deleting destructor (slot
// 10, flag 1) and 0.
uint32_t __fastcall LgRelease(char* self, void*)
{
    OPENSC4_FIRST("LgRelease");
    const uint32_t count = At<uint32_t>(self, kLgCount);
    if (count > 1) {
        At<uint32_t>(self, kLgCount) = count - 1;
        return count - 1;
    }
    Virtual<void*>(self, 0x28, 1u);
    return 0;
}

// The game's 0x0094C288: the vtables back; the window, when there is one:
// with the flag its slot 11's object (when one) gets slot 15 with the
// window, then the window's slot 5; the window released. The value and the
// object released, the variable table released (and forgotten when that
// answers 0). Each released field cleared. EAX the last call's (the
// caller's with none; ours 0).
uint32_t __fastcall LgDestruct(char* self, void*)
{
    OPENSC4_FIRST("LgDestruct");
    uint32_t eax = 0;
    At<std::uintptr_t>(self, 0) = addr::cGZUIScriptLegacy_Vtable;
    At<std::uintptr_t>(self, 4) = addr::cGZUIScriptLegacy_ChildVtable;
    At<std::uintptr_t>(self, 8) = addr::cGZUIScriptLegacy_WindowVtable;
    if (void* const window = At<void*>(self, kLgWindow)) {
        if (At<uint8_t>(self, kLgFlag)) {
            void* const other = Virtual<void*>(window, 0x2C);
            if (other)
                Virtual<uint32_t>(other, 0x3C, At<void*>(self, kLgWindow));
            Virtual<uint32_t>(At<void*>(self, kLgWindow), 0x14);
        }
        eax = Release(At<void*>(self, kLgWindow));
        At<void*>(self, kLgWindow) = nullptr;
    }
    if (void* const value = At<void*>(self, kLgValue)) {
        eax = Release(value);
        At<void*>(self, kLgValue) = nullptr;
    }
    if (void* const table = LgTable()) {
        eax = Release(table);
        if (eax == 0)
            LgTable() = nullptr;
    }
    if (void* const object = At<void*>(self, kLgObject)) {
        eax = Release(object);
        At<void*>(self, kLgObject) = nullptr;
    }
    return eax;
}

// The game's 0x0094C761 (slot 10, `ret 4`): the destructor, freed
// (SC4::operator_delete) with flag 1; `this` answered.
void* __fastcall LgDeletingDestructor(char* self, void*, uint32_t flags)
{
    OPENSC4_FIRST("LgDeletingDestructor");
    LgDestruct(self, nullptr);
    if (flags & 1)
        CallCdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// The game's 0x0094C30E (slot 4; `this` unused): the variable table.
void* __fastcall LgTableSlot(char*, void*)
{
    OPENSC4_FIRST("LgTableSlot");
    return LgTable();
}

// The game's 0x0094C314 (slot 7, `ret 4`; also through 0x0094C4D8): the
// flag the argument's byte; AL that byte over the argument (the game's
// EAX is the caller's above it; ours 0).
uint32_t __fastcall LgSetFlag(char* self, void*, uint32_t flag)
{
    OPENSC4_FIRST("LgSetFlag");
    At<uint8_t>(self, kLgFlag) = static_cast<uint8_t>(flag);
    return flag & 0xFF;
}

// The game's 0x0094C31E (slot 9, `ret 4`): the old value released and
// cleared; the parent's interface 0x9A4527CB (slot 0, the argument's own
// stack slot its out), its slot 3 the new value (held) and the interface
// released. AL 1 over the last call's EAX (the caller's with none; ours
// 0).
uint32_t __fastcall LgSetParent(char* self, void*, void* parent)
{
    OPENSC4_FIRST("LgSetParent");
    uint32_t eax = 0;
    if (void* const old = At<void*>(self, kLgValue)) {
        eax = Release(old);
        At<void*>(self, kLgValue) = nullptr;
    }
    if (parent) {
        void* face = nullptr;
        eax = Virtual<uint32_t>(parent, 0x0, 0x9A4527CBu, &face);
        if (Yes(eax)) {
            void* const value = Virtual<void*>(face, 0xC);
            At<void*>(self, kLgValue) = value;
            eax = Bits(value);
            if (value)
                eax = AddRef(value);
            eax = Release(face);
        }
    }
    return Answer(eax, true);
}

// A held pointer set: unless it is the same, the new one held, the old
// released, the new kept. EAX the last call's (the caller's with none).
uint32_t SetHeld(void*& field, void* value)
{
    uint32_t eax = 0;
    if (value == field)
        return eax;
    if (value)
        eax = AddRef(value);
    if (field)
        eax = Release(field);
    field = value;
    return eax;
}

// The game's 0x0094C3A1 (slot 5 of the face at +8, `ret 4`): the window
// +0x10 set (SetHeld).
uint32_t __fastcall LgSetWindow(char* face, void*, void* window)
{
    OPENSC4_FIRST("LgSetWindow");
    return SetHeld(At<void*>(face, kLgWindow - 8), window);
}

// The game's 0x0094C375 (slot 6 of the face at +8, `ret 4`): the object
// +0x18 set (SetHeld).
uint32_t __fastcall LgSetObject(char* face, void*, void* object)
{
    OPENSC4_FIRST("LgSetObject");
    return SetHeld(At<void*>(face, kLgObject - 8), object);
}

// The game's 0x0094C3CD (slot 5): a tail jump to the window's slot 63; 0
// without a window.
uint32_t __fastcall LgWindow63(char* self, void*)
{
    OPENSC4_FIRST("LgWindow63");
    void* const window = At<void*>(self, kLgWindow);
    return window ? Virtual<uint32_t>(window, 0xFC) : 0;
}

// The game's 0x0094C3E1 (slot 6, `ret 4`): a tail jump to the window's slot
// 64 with the argument; without a window the caller's EAX (ours 0).
uint32_t __fastcall LgWindow64(char* self, void*, uint32_t argument)
{
    OPENSC4_FIRST("LgWindow64");
    void* const window = At<void*>(self, kLgWindow);
    return window ? Virtual<uint32_t>(window, 0x100, argument) : 0;
}

// The game's 0x0094C3F5 (slot 4 of the face at +4, `ret 4`): the window's
// slot 54 with the argument, AL 1 over its EAX; AL 0 without a window,
// over the caller's EAX (ours 0).
uint32_t __fastcall LgWindow54(char* face, void*, uint32_t argument)
{
    OPENSC4_FIRST("LgWindow54");
    void* const window = At<void*>(face, kLgWindow - 4);
    return window ? Answer(Virtual<uint32_t>(window, 0xD8, argument), true) : 0;
}

// The game's 0x0094C413 (slot 5 of the face at +4, `ret 4`): as slot 4 with
// the window's slot 47.
uint32_t __fastcall LgWindow47(char* face, void*, uint32_t argument)
{
    OPENSC4_FIRST("LgWindow47");
    void* const window = At<void*>(face, kLgWindow - 4);
    return window ? Answer(Virtual<uint32_t>(window, 0xBC, argument), true) : 0;
}

// ====================================================================================
// Install

OPENSC4_THUNK(VectorPushThunk, VectorPush, 4)
OPENSC4_THUNK(VectorConstructThunk, VectorConstruct, 0)
OPENSC4_THUNK(VectorEraseThunk, VectorErase, 8)
OPENSC4_THUNK(RootConstructThunk, RootConstruct, 0)
OPENSC4_THUNK(RootAddThunk, RootAdd, 4)
OPENSC4_THUNK(RootDestructThunk, RootDestruct, 0)
OPENSC4_THUNK(RootDeletingDestructorThunk, RootDeletingDestructor, 4)
OPENSC4_THUNK(LgQueryInterfaceThunk, LgQueryInterface, 8)
OPENSC4_THUNK(LgAddRefThunk, LgAddRef, 0)
OPENSC4_THUNK(LgReleaseThunk, LgRelease, 0)
OPENSC4_THUNK(LgDestructThunk, LgDestruct, 0)
OPENSC4_THUNK(LgDeletingDestructorThunk, LgDeletingDestructor, 4)
OPENSC4_THUNK(LgTableSlotThunk, LgTableSlot, 0)
OPENSC4_THUNK(LgSetFlagThunk, LgSetFlag, 4)
OPENSC4_THUNK(LgSetParentThunk, LgSetParent, 4)
OPENSC4_THUNK(LgSetWindowThunk, LgSetWindow, 4)
OPENSC4_THUNK(LgSetObjectThunk, LgSetObject, 4)
OPENSC4_THUNK(LgWindow63Thunk, LgWindow63, 0)
OPENSC4_THUNK(LgWindow64Thunk, LgWindow64, 4)
OPENSC4_THUNK(LgWindow54Thunk, LgWindow54, 4)
OPENSC4_THUNK(LgWindow47Thunk, LgWindow47, 4)

struct Detour {
    std::uintptr_t address;
    void (*ours)();
};

template <std::size_t N>
bool InstallAll(const Detour (&detours)[N])
{
    if (!game::IsBuild641())
        return false;
    for (const Detour& detour : detours) {
        const addr::Entry* entry = addr_check::Find(detour.address);
        void* original = nullptr;
        if (!entry || !hook::Install(*entry, reinterpret_cast<void*>(detour.ours), &original))
            return false;
    }
    return true;
}

bool InstallRootLife(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZUIScriptRoot_VectorPush, &VectorPushThunk},
        {addr::cGZUIScriptRoot_VectorConstruct, &VectorConstructThunk},
        {addr::cGZUIScriptRoot_VectorErase, &VectorEraseThunk},
        {addr::cGZUIScriptRoot_Construct, &RootConstructThunk},
        {addr::cGZUIScriptRoot_Add, &RootAddThunk},
        {addr::cGZUIScriptRoot_Destruct, &RootDestructThunk},
        {addr::cGZUIScriptRoot_DeletingDestructor, &RootDeletingDestructorThunk},
    };
    return InstallAll(detours);
}

bool InstallLegacy(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZUIScriptLegacy_QueryInterface, &LgQueryInterfaceThunk},
        {addr::cGZUIScriptLegacy_AddRef, &LgAddRefThunk},
        {addr::cGZUIScriptLegacy_Release, &LgReleaseThunk},
        {addr::cGZUIScriptLegacy_Destruct, &LgDestructThunk},
        {addr::cGZUIScriptLegacy_DeletingDestructor, &LgDeletingDestructorThunk},
        {addr::cGZUIScriptLegacy_TableSlot, &LgTableSlotThunk},
        {addr::cGZUIScriptLegacy_SetFlag, &LgSetFlagThunk},
        {addr::cGZUIScriptLegacy_SetParent, &LgSetParentThunk},
        {addr::cGZUIScriptLegacy_SetWindow, &LgSetWindowThunk},
        {addr::cGZUIScriptLegacy_SetObject, &LgSetObjectThunk},
        {addr::cGZUIScriptLegacy_Window63, &LgWindow63Thunk},
        {addr::cGZUIScriptLegacy_Window64, &LgWindow64Thunk},
        {addr::cGZUIScriptLegacy_Window54, &LgWindow54Thunk},
        {addr::cGZUIScriptLegacy_Window47, &LgWindow47Thunk},
    };
    return InstallAll(detours);
}

const bool g_registered[] = {
    registry::Register({"framework.uiscript.rootlife", "framework/uiscript", registry::Kind::Detour,
                        registry::Phase::Attach, addr::cGZUIScriptRoot_Add,
                        "Our UI script root holder 0x5A354595's constructor, slot 4 and destructors, and its "
                        "vector's constructor, push and erase (7 detours; part u11, docs/gzui.md)",
                        false, InstallRootLife}),
    registry::Register({"framework.uiscript.legacy", "framework/uiscript", registry::Kind::Detour,
                        registry::Phase::Attach, addr::cGZUIScriptLegacy_QueryInterface,
                        "Our LEGACY reader 0xFA450242: QueryInterface, AddRef, Release, destructors, the variable "
                        "table's getter, the setters and the calls to its window (14 detours; part u11, "
                        "docs/gzui.md)",
                        false, InstallLegacy}),
};

} // namespace
} // namespace opensc4::gzui
