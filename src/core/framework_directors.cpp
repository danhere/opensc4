// The COM director base class (cRZCOMDllDirector: what every director of
// the exe is built on) on the game's own objects (docs/framework.md).
// Replacements, each off by default:
//
//   framework.directors.lookup        GetClassObject (0x0090DFAC),
//                                     EnumClassObjects (0x0090E02B),
//                                     QueryInterface (0x0090DE02), RemoveRef
//                                     (0x0090DE43), CanUnloadNow
//                                     (0x0090DEA2), GetLibraryPath
//                                     (0x0090DECD)
//   framework.directors.registration  AddCls (0x0090E133, and 0x0090E160 for
//                                     factories that take the interface
//                                     ID), AddDirector (0x0090E0D4),
//                                     InitializeCOM (0x0090DE57)
//
// The exe's directors register their classes and each other once, while
// the app starts and before a plugin's director is called, so the
// registration group goes in when core.dll is injected (phase Attach).
//
// The object (0x4C bytes): +0x00 vtable, +0x04 the framework hooks' vtable,
// +0x08 reference count, +0x10 the library's path (a string: characters
// from +0x14 to +0x18), +0x24 the COM object, +0x28 the framework, +0x2C
// the child directors (a vector of pointers, not counted), +0x38 the
// classes: a hash table whose nodes are {next, class ID, factory, type}.
// A type 1 factory makes an object that is then asked for the interface; a
// type 2 factory takes the interface ID itself.
#include "core/address_check.h"
#include "core/framework_containers.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::framework {
namespace {

constexpr std::size_t kHooksPart = 0x04;
constexpr std::size_t kRefs = 0x08;
constexpr std::size_t kPath = 0x10;
constexpr std::size_t kPathBegin = 0x14;
constexpr std::size_t kPathEnd = 0x18;
constexpr std::size_t kCom = 0x24;
constexpr std::size_t kFramework = 0x28;
constexpr std::size_t kChildren = 0x2C;
constexpr std::size_t kChildrenEnd = 0x30;
constexpr std::size_t kChildrenCapacity = 0x34;
constexpr std::size_t kClasses = 0x38;

constexpr uint32_t kIID_cIGZUnknown = 1;
constexpr uint32_t kIID_cIGZCOMDirector = 0xA21EE941;
constexpr uint32_t kIID_cIGZFrameWorkHooks = 0x03FA40BF;

// The class table: {?, buckets, buckets' end, capacity's end, count}.
constexpr std::size_t kBuckets = 0x4;
constexpr std::size_t kBucketsEnd = 0x8;
constexpr std::size_t kNodeClass = 0x4;
constexpr std::size_t kNodeFactory = 0x8;
constexpr std::size_t kNodeType = 0xC;

char* FindClass(char* table, uint32_t classId)
{
    char** const buckets = At<char**>(table, kBuckets);
    const uint32_t count = static_cast<uint32_t>(At<char**>(table, kBucketsEnd) - buckets);
    for (char* node = buckets[classId % count]; node; node = At<char*>(node, kNext))
        if (At<uint32_t>(node, kNodeClass) == classId)
            return node;
    return nullptr;
}

// The game's 0x0090DFAC: the children in order, the first that makes the
// object wins; then the director's own table. An object a type 1 factory
// made that does not have the interface is given a reference and released,
// which ends it.
bool __fastcall GetClassObject(char* self, void*, uint32_t classId, uint32_t interfaceId, void** out)
{
    for (void** child = At<void**>(self, kChildren); child != At<void**>(self, kChildrenEnd); ++child)
        if (Virtual<bool>(*child, 0x18, classId, interfaceId, out))
            return true;
    char* const node = FindClass(self + kClasses, classId);
    if (!node)
        return false;
    void* const factory = At<void*>(node, kNodeFactory);
    switch (At<uint32_t>(node, kNodeType)) {
    case 1: {
        void* const object = reinterpret_cast<void*(__cdecl*)()>(factory)();
        if (Virtual<bool>(object, 0x0, interfaceId, out))
            return true;
        Virtual<uint32_t>(object, 0x4);
        Virtual<uint32_t>(object, 0x8);
        return false;
    }
    case 2:
        return reinterpret_cast<bool(__cdecl*)(uint32_t, void**)>(factory)(interfaceId, out);
    default:
        return false;
    }
}

// The game's 0x0090E02B: the children's classes, then the director's own,
// bucket by bucket, each to the callback with version 0.
void __fastcall EnumClassObjects(char* self, void*, void* callback, void* context)
{
    for (void** child = At<void**>(self, kChildren); child != At<void**>(self, kChildrenEnd); ++child)
        Virtual<void>(*child, 0x14, callback, context);
    char* const table = self + kClasses;
    const auto call = reinterpret_cast<void(__cdecl*)(uint32_t, uint32_t, void*)>(callback);
    for (char** bucket = At<char**>(table, kBuckets); bucket != At<char**>(table, kBucketsEnd); ++bucket)
        for (char* node = *bucket; node; node = At<char*>(node, kNext))
            call(At<uint32_t>(node, kNodeClass), 0, context);
}

// The game's 0x0090DE02.
bool __fastcall QueryInterface(char* self, void*, uint32_t interfaceId, void** out)
{
    if (interfaceId == kIID_cIGZUnknown || interfaceId == kIID_cIGZCOMDirector)
        *out = self;
    else if (interfaceId == kIID_cIGZFrameWorkHooks)
        *out = self + kHooksPart;
    else
        return false;
    Virtual<uint32_t>(self, 0x4);
    return true;
}

// The game's 0x0090DE43.
uint32_t __fastcall RemoveRef(char* self, void*)
{
    uint32_t& count = At<uint32_t>(self, kRefs);
    if (count > 0)
        --count;
    return count;
}

// The game's 0x0090DEA2: yes while the director is referenced; without a
// reference, only when every child says yes.
bool __fastcall CanUnloadNow(char* self, void*)
{
    if (At<uint32_t>(self, kRefs) != 0)
        return true;
    for (void** child = At<void**>(self, kChildren); child != At<void**>(self, kChildrenEnd); ++child)
        if (!Virtual<bool>(*child, 0x1C))
            return false;
    return true;
}

// The game's 0x0090DECD: the path's characters into the string (its slot 3).
bool __fastcall GetLibraryPath(char* self, void*, void* string)
{
    const char* const begin = At<const char*>(self, kPathBegin);
    const uint32_t length = static_cast<uint32_t>(At<const char*>(self, kPathEnd) - begin);
    Virtual<uint32_t>(string, 0xC, begin, length);
    return true;
}

// The game's 0x0090E133 and 0x0090E160: the class joins the table unless
// its ID is there. The result is the address of a local, as in the game.
void* AddClass(char* self, uint32_t classId, void* factory, uint32_t type)
{
    struct {
        uint32_t classId;
        void* factory;
        uint32_t type;
    } value{classId, factory, type};
    // {node, table, inserted}: 9 bytes written.
    uint32_t result[3];
    using InsertFn = void*(__thiscall*)(char* table, void* result, void* value);
    return reinterpret_cast<InsertFn>(addr::cRZCOMDllDirector_ClassTable_Insert)(self + kClasses, result, &value);
}

void* __fastcall AddCls(char* self, void*, uint32_t classId, void* factory)
{
    return AddClass(self, classId, factory, 1);
}

void* __fastcall AddClsType2(char* self, void*, uint32_t classId, void* factory)
{
    return AddClass(self, classId, factory, 2);
}

// The game's 0x0090DE57.
bool __fastcall InitializeCOM(char* self, void*, void* com, void* path)
{
    if (!com)
        return false;
    At<void*>(self, kCom) = com;
    At<void*>(self, kFramework) = Virtual<void*>(com, 0x10);
    Virtual<uint32_t>(self + kPath, 0x34, path);
    for (void** child = At<void**>(self, kChildren); child != At<void**>(self, kChildrenEnd); ++child)
        Virtual<bool>(*child, 0xC, com, path);
    return true;
}

// The game's 0x0090E0D4: the child is given this director's COM object
// (through slot 12) and path, the child's own children are added to this
// director first, then the child itself: the list stays flat.
void __fastcall AddDirector(char* self, void*, char* child)
{
    Virtual<bool>(child, 0xC, Virtual<void*>(self, 0x30), static_cast<void*>(self + kPath));
    for (char** grandchild = At<char**>(child, kChildren); grandchild != At<char**>(child, kChildrenEnd);
         ++grandchild)
        AddDirector(self, nullptr, *grandchild);

    char**& begin = At<char**>(self, kChildren);
    char**& end = At<char**>(self, kChildrenEnd);
    char**& capacityEnd = At<char**>(self, kChildrenCapacity);
    if (end == capacityEnd) {
        const uint32_t size = static_cast<uint32_t>(end - begin);
        const uint32_t capacity = size + (size ? size : 1);
        auto** const block = static_cast<char**>(Allocate(capacity * 4));
        for (uint32_t i = 0; i < size; ++i)
            block[i] = begin[i];
        if (begin)
            Free(begin);
        begin = block;
        end = block + size;
        capacityEnd = block + capacity;
    }
    *end++ = child;
}

struct Detour {
    std::uintptr_t address;
    void* ours;
};

template <std::size_t N>
bool InstallAll(const Detour (&detours)[N])
{
    if (!game::IsBuild641())
        return false;
    for (const Detour& detour : detours) {
        const addr::Entry* entry = addr_check::Find(detour.address);
        void* original = nullptr;
        if (!entry || !hook::Install(*entry, detour.ours, &original))
            return false;
    }
    return true;
}

bool InstallLookup(registry::Mode)
{
    const Detour detours[] = {
        {addr::cRZCOMDllDirector_GetClassObject, reinterpret_cast<void*>(&GetClassObject)},
        {addr::cRZCOMDllDirector_EnumClassObjects, reinterpret_cast<void*>(&EnumClassObjects)},
        {addr::cRZCOMDllDirector_QueryInterface, reinterpret_cast<void*>(&QueryInterface)},
        {addr::cRZCOMDllDirector_RemoveRef, reinterpret_cast<void*>(&RemoveRef)},
        {addr::cRZCOMDllDirector_CanUnloadNow, reinterpret_cast<void*>(&CanUnloadNow)},
        {addr::cRZCOMDllDirector_GetLibraryPath, reinterpret_cast<void*>(&GetLibraryPath)},
    };
    return InstallAll(detours);
}

bool InstallRegistration(registry::Mode)
{
    const Detour detours[] = {
        {addr::cRZCOMDllDirector_AddCls, reinterpret_cast<void*>(&AddCls)},
        {addr::cRZCOMDllDirector_AddCls_FactoryType2, reinterpret_cast<void*>(&AddClsType2)},
        {addr::cRZCOMDllDirector_AddDirector, reinterpret_cast<void*>(&AddDirector)},
        {addr::cRZCOMDllDirector_InitializeCOM, reinterpret_cast<void*>(&InitializeCOM)},
    };
    return InstallAll(detours);
}

const bool g_registeredLookup = registry::Register({
    "framework.directors.lookup",
    "framework/directors",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cRZCOMDllDirector_GetClassObject,
    "Our class lookup of the exe's directors: GetClassObject, EnumClassObjects and four small methods "
    "(docs/framework.md)",
    false,
    InstallLookup,
});

const bool g_registeredRegistration = registry::Register({
    "framework.directors.registration",
    "framework/directors",
    registry::Kind::Detour,
    registry::Phase::Attach,
    addr::cRZCOMDllDirector_AddCls,
    "Our class and child director registration of the exe's directors: AddCls, AddDirector, InitializeCOM "
    "(docs/framework.md)",
    false,
    InstallRegistration,
});

} // namespace
} // namespace opensc4::framework
