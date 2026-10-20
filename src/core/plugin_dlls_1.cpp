// How the game loads GZCOM plugin DLLs (docs/plugin-dlls.md), part 1:
// 0x0087B23D to 0x0087C114, the framework's COM object and its library
// records. Replacement, off by default:
//
//   resources.plugin_dlls.libraries: the four guarded Windows calls, the
//     library record (constructor, destructor, QueryInterface, GetPath, Load,
//     Free, the path order), the library tree (find, insert), the class map
//     insert and the version rule, and the COM object's AddLibrary,
//     GetClassObject, FreeUnusedLibraries, the shutdown loop and the two
//     EnumerateLibraries.
//
// The COM object (cGZCOM, main vtable 0x00AC3368, 14 slots: +0x0C
// GetClassObject, +0x14 AddLibrary, +0x18 FreeUnusedLibraries, +0x30 and
// +0x34 EnumerateLibraries):
//   +0x24 the lock (slot +4 locks, slot +8 unlocks)
//   +0x34 the class map {header, count}: nodes of 0x1C bytes, +0x10 class
//         ID (the key), +0x14 version, +0x18 the library record
//   +0x40 the libraries {header, count}: a tree ordered by path, nodes of
//         0x38 bytes with the library record at +0x10
//   +0x4C the library whose classes are being registered
//   +0x50 the framework (answers class 0x3E8)
//   +0x54 the root director (answers every class no library registered)
//
// The library record (cGZCOMLibrary, 0x28 bytes, vtable 0x00AC33A0, 8 slots:
// QueryInterface, AddRef, Release, +0x0C GetPath, +0x10 GetDirector, +0x14
// GetHandle, +0x18 IsLoaded, +0x1C the deleting destructor):
//   +0x04 loaded byte   +0x08 reference count   +0x0C the DLL's director
//   +0x10 the path (a game string: vtable, begin +0x14, end +0x18)
//   +0x24 the module handle
//
// AddLibrary: a record is built on the stack; under the lock the path is
// looked up and, when new, a copy goes into the tree BEFORE anything is
// loaded; then, outside the lock, Load, the director's EnumClassObjects
// under the lock, and its OnStart. A library whose Load fails stays in the
// tree, not loaded, and is not tried again by AddLibrary (GetClassObject
// would try). Load builds an error text on each failure and throws it away.
//
// The class map's rule (RegisterClassObject, the callback the directors
// get): an entry of the same class ID with a LOWER version is erased, then
// the new one is inserted only if the ID is absent. So the higher version
// wins and of two equal versions the first.
//
// The call at 0x0087B3D1 (Free's call to the guarded FreeLibrary) is one a
// mod (sc4fix) replaces by NOPs to keep plugin DLLs loaded. LibraryFree
// reads that byte every time: a call instruction there is followed to its
// target, anything else means no call.
//
// The four Windows calls sit in structured exception frames in the original
// (__SEH_prolog 0x009F1F74; scope tables 0x00AC32A8, 0x00AC32B8, 0x00AC32C8,
// 0x00AC32D8, one level each: the filter answers 1, 'run the handler', for
// every exception; the handlers answer false, FreeLibrary's just returns).
// Here: __try around a call of a function that is not inlined (the clang-cl
// rule in crash.h).
//
// Still the game's: the COM object's constructor, destructor, RealInit
// 0x0087BCDD and RealShutdown 0x0087B8EC (it calls our shutdown loop), the
// GUID makers, the tree destroy helpers, the record's copy constructor
// 0x0087BEE4, the wide buffer 0x0087B772, the thunks, and IsLoaded
// 0x0087B357 (4 bytes, too short for a detour).
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace opensc4::plugin_dlls {
namespace {

namespace unknown { // cIGZUnknown, by vtable offset
constexpr std::size_t kQueryInterface = 0x0; // (iid, out)
constexpr std::size_t kAddRef = 0x4;
} // namespace unknown

namespace lock { // the COM object's lock, by vtable offset
constexpr std::size_t kLock = 0x4;
constexpr std::size_t kUnlock = 0x8;
} // namespace lock

namespace string { // the game's string, by vtable offset
constexpr std::size_t kToChar = 0x14;
constexpr std::size_t kCopy = 0x38; // (string)
} // namespace string

namespace library { // cGZCOMLibrary
// Vtable offsets.
constexpr std::size_t kGetPath = 0xC; // (string out)
constexpr std::size_t kGetDirector = 0x10;
constexpr std::size_t kIsLoaded = 0x18;
// Fields.
constexpr std::size_t kVtable = 0x0;
constexpr std::size_t kLoaded = 0x4;
constexpr std::size_t kRefCount = 0x8;
constexpr std::size_t kDirector = 0xC;
constexpr std::size_t kPath = 0x10;
constexpr std::size_t kPathBegin = 0x14;
constexpr std::size_t kPathEnd = 0x18;
constexpr std::size_t kHandle = 0x24;
constexpr std::size_t kSize = 0x28;
} // namespace library

namespace director { // a DLL's cIGZCOMDirector, by vtable offset
constexpr std::size_t kInitializeCOM = 0xC;     // (COM object, path string)
constexpr std::size_t kOnStart = 0x10;          // (COM object)
constexpr std::size_t kEnumClassObjects = 0x14; // (callback, context)
constexpr std::size_t kGetClassObject = 0x18;   // (class ID, interface ID, out)
constexpr std::size_t kCanUnloadNow = 0x1C;
} // namespace director

namespace com { // cGZCOM, fields
constexpr std::size_t kLock = 0x24;
constexpr std::size_t kClassMap = 0x34;
constexpr std::size_t kLibraries = 0x40;
constexpr std::size_t kLibraryCount = 0x44;
constexpr std::size_t kLoading = 0x4C;
constexpr std::size_t kFramework = 0x50;
constexpr std::size_t kRootDirector = 0x54;
} // namespace com

constexpr uint32_t kLibraryIid = 0x07D4922E;
constexpr uint32_t kUnknownIid = 1;
constexpr uint32_t kFrameworkClass = 0x3E8;
constexpr uint32_t kLibraryNodeSize = 0x38;
constexpr int32_t kDefaultEncoding = -1;
constexpr int32_t kUtf16Encoding = 0x10;
// Free's call to the guarded FreeLibrary, which a mod may have removed.
constexpr std::uintptr_t kFreeCallSite = 0x0087B3D1;

struct TreeNode {
    uint8_t color;
    TreeNode* parent;
    TreeNode* left;
    TreeNode* right;
};
static_assert(sizeof(TreeNode) == 0x10);

struct Tree {
    TreeNode* header;
    uint32_t count;
};

// A class map value: what a DLL's director registered.
struct ClassEntry {
    uint32_t classId;
    uint32_t version;
    char* record;
};

struct ClassNode : TreeNode {
    ClassEntry entry;
};
static_assert(sizeof(ClassNode) == 0x1C);

struct LibraryNode : TreeNode {
    char record[library::kSize];
};
static_assert(sizeof(LibraryNode) == kLibraryNodeSize);

// What a tree insert writes through its result pointer: the node and, for
// the unique insert, whether it is new (5 bytes; the original frames leave 8).
struct TreeInsertResult {
    TreeNode* node;
    uint8_t inserted;
};
static_assert(sizeof(TreeInsertResult) == 8);

struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t field10;
};
static_assert(sizeof(GameString) == 0x14);

// What 0x0087B772 builds: a vector of UTF-16 units.
struct WideBuffer {
    uint16_t* begin;
    uint16_t* end;
    uint16_t* capacity;
};
static_assert(sizeof(WideBuffer) == 0xC);

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

using SprintfFn = void(__cdecl*)(GameString*, const char*, ...);

void Free(void* memory)
{
    Cdecl<void>(addr::SC4_Free, memory);
}

// A string's destructor as the original inlines it.
void DestroyString(GameString& text)
{
    text.vtable = addr::SC4String_Vtable;
    if (text.begin)
        Free(text.begin);
}

// The game's 0x0040D4C0 and 0x0040D500: the next and the previous node of a
// tree in order (cdecl, whatever their entries say).
TreeNode* Next(TreeNode* node)
{
    return Cdecl<TreeNode*>(addr::cSC4Advisor_F40D4C0, node);
}

TreeNode* Previous(TreeNode* node)
{
    return Cdecl<TreeNode*>(addr::cSC4Advisor_F40D500, node);
}

char* LibraryOf(TreeNode* node)
{
    return static_cast<LibraryNode*>(node)->record;
}

// --- The guarded Windows calls --------------------------------------------------

__declspec(noinline) void CallLoadLibraryA(HMODULE* handle, const char* path)
{
    *handle = LoadLibraryA(path);
}

__declspec(noinline) void CallLoadLibraryW(HMODULE* handle, const wchar_t* path)
{
    *handle = LoadLibraryW(path);
}

__declspec(noinline) BOOL CallFreeLibrary(HMODULE handle)
{
    return FreeLibrary(handle);
}

__declspec(noinline) void CallGetProcAddress(FARPROC* entry, const HMODULE* handle, const char* name)
{
    *entry = GetProcAddress(*handle, name);
}

// The game's 0x0087B23D (cdecl): LoadLibraryA into *handle. False, with
// *handle untouched, when the call raised an exception.
uint8_t __cdecl GuardedLoadLibraryA(HMODULE* handle, const char* path)
{
    __try {
        CallLoadLibraryA(handle, path);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    return 1;
}

// The game's 0x0087B276 (cdecl): the same with LoadLibraryW.
uint8_t __cdecl GuardedLoadLibraryW(HMODULE* handle, const wchar_t* path)
{
    __try {
        CallLoadLibraryW(handle, path);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    return 1;
}

// The game's 0x0087B2AF (cdecl): FreeLibrary, exceptions swallowed. The
// original leaves FreeLibrary's answer in EAX (whatever is there after an
// exception); its one caller does not read it.
BOOL __cdecl GuardedFreeLibrary(HMODULE handle)
{
    BOOL freed = 0;
    __try {
        freed = CallFreeLibrary(handle);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return freed;
}

// The game's 0x0087B2DB (cdecl): GetProcAddress(*handle, name) into *entry.
// False, with *entry untouched, when the call raised an exception.
uint8_t __cdecl GuardedGetProcAddress(FARPROC* entry, const HMODULE* handle, const char* name)
{
    __try {
        CallGetProcAddress(entry, handle, name);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    return 1;
}

// --- The library record ---------------------------------------------------------

// The game's 0x0087B319 (record slot 0).
uint8_t __fastcall LibraryQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    if (iid != kLibraryIid && iid != kUnknownIid)
        return 0;
    *out = self;
    Virtual<uint32_t>(self, unknown::kAddRef);
    return 1;
}

// The game's 0x0087B345 (record slot 3): copies the path into the caller's
// string and answers what the string's copy answered.
uint32_t __fastcall LibraryGetPath(char* self, void*, void* out)
{
    return Virtual<uint32_t>(out, string::kCopy, self + library::kPath);
}

// The game's 0x0087B3B3: forgets the director (no call into the DLL), frees
// the module and clears the handle and the loaded byte. Always true.
uint8_t __fastcall LibraryFree(char* self, void*)
{
    if (At<uint8_t>(self, library::kLoaded)) {
        if (At<void*>(self, library::kDirector))
            At<void*>(self, library::kDirector) = nullptr;
        Virtual<const char*>(self + library::kPath, string::kToChar);
        const HMODULE handle = At<HMODULE>(self, library::kHandle);
        // The call at 0x0087B3D1, as it is in memory now.
        const auto* const site = reinterpret_cast<const uint8_t*>(kFreeCallSite);
        if (site[0] == 0xE8) {
            int32_t relative;
            std::memcpy(&relative, site + 1, sizeof(relative));
            Cdecl<BOOL>(kFreeCallSite + 5 + static_cast<std::uintptr_t>(relative), handle);
        }
        At<HMODULE>(self, library::kHandle) = nullptr;
        At<uint8_t>(self, library::kLoaded) = 0;
    }
    return 1;
}

// The game's 0x0087BA2E (record slot 7): the deleting destructor.
char* __fastcall LibraryDelete(char* self, void*, uint32_t flags)
{
    At<std::uintptr_t>(self, library::kVtable) = addr::cGZCOMLibrary_Vtable;
    This<void>(addr::SC4String_Destroy, self + library::kPath);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// The game's 0x0087BA53: the constructor, from a path string.
char* __fastcall LibraryConstruct(char* self, void*, void* path)
{
    At<uint32_t>(self, library::kRefCount) = 0;
    At<void*>(self, library::kDirector) = nullptr;
    At<std::uintptr_t>(self, library::kVtable) = addr::cGZCOMLibrary_Vtable;
    At<uint8_t>(self, library::kLoaded) = 0;
    This<void*>(addr::SC4String_FromIGZString, self + library::kPath, path);
    At<HMODULE>(self, library::kHandle) = nullptr;
    return self;
}

// The game's 0x0087BA7E: loads the DLL, asks it for its director and
// initialises that. True when the library is loaded afterwards.
uint8_t __fastcall LibraryLoad(char* self, void*)
{
    if (At<uint8_t>(self, library::kLoaded))
        return 1;
    GameString systemPath;
    This<void*>(addr::SC4String_SC4String, &systemPath);
    Cdecl<uint8_t>(addr::RZString_ConvertEncoding, self + library::kPath, &systemPath, kDefaultEncoding);
    HMODULE* const handle = &At<HMODULE>(self, library::kHandle);
    // The original asks GetVersion, as code of its time did.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    const int32_t version = static_cast<int32_t>(GetVersion());
#pragma clang diagnostic pop
    if (version < 0) {
        // Windows 9x.
        GuardedLoadLibraryA(handle, systemPath.begin);
    } else {
        uint32_t length =
            static_cast<uint32_t>(At<char*>(self, library::kPathEnd) - At<char*>(self, library::kPathBegin));
        WideBuffer wide;
        This<void*>(addr::cGZCOMLibrary_WideBufferConstruct, &wide, length + 1);
        uint16_t* const text = wide.begin;
        Cdecl<uint32_t>(addr::RZString_ConvertToBuffer, self + library::kPath, text, &length, kUtf16Encoding);
        text[length] = 0;
        GuardedLoadLibraryW(handle, reinterpret_cast<const wchar_t*>(text));
        if (text)
            Free(text);
    }
    const HMODULE module = *handle;
    if (!module) {
        GameString message;
        This<void*>(addr::SC4String_SC4String, &message);
        char* systemText = nullptr;
        const DWORD error = GetLastError();
        FormatMessageA(0x1300, nullptr, error, 0x400, reinterpret_cast<LPSTR>(&systemText), 0, nullptr);
        if (systemText) {
            reinterpret_cast<SprintfFn>(addr::cRZBaseString_Sprintf)(
                &message, "Failed to load the requested dll \"%s\"\nWindows error message - \"%s\"", systemPath.begin,
                systemText);
            LocalFree(systemText);
        }
        DestroyString(message);
        DestroyString(systemPath);
        return 0;
    }

    FARPROC entry = nullptr;
    At<uint8_t>(self, library::kLoaded) = 1;
    HMODULE moduleCopy = module;
    GuardedGetProcAddress(&entry, &moduleCopy, "GZDllGetGZCOMDirector");
    if (!entry) {
        LibraryFree(self, nullptr);
        DestroyString(systemPath);
        return 0;
    }
    void* const dllDirector = reinterpret_cast<void*(__cdecl*)()>(entry)();
    if (!dllDirector) {
        GameString message;
        This<void*>(addr::SC4String_SC4String, &message);
        reinterpret_cast<SprintfFn>(addr::cRZBaseString_Sprintf)(
            &message, "cGZCOMLibrary::Load: Failed to aquire GZCOM director from library: \"%s\"\n",
            At<char*>(self, library::kPathBegin));
        LibraryFree(self, nullptr);
        DestroyString(message);
        DestroyString(systemPath);
        return 0;
    }
    At<void*>(self, library::kDirector) = dllDirector;
    // The original reads the director's vtable pointer before it asks for
    // the COM object.
    void** const vtable = *static_cast<void***>(dllDirector);
    void* const comObject = Cdecl<void*>(addr::RZGetCOM);
    using InitializeFn = uint8_t(__thiscall*)(void*, void*, void*);
    const auto initialize = reinterpret_cast<InitializeFn>(vtable[director::kInitializeCOM / sizeof(void*)]);
    if (!initialize(dllDirector, comObject, self + library::kPath)) {
        GameString message;
        This<void*>(addr::SC4String_SC4String, &message);
        reinterpret_cast<SprintfFn>(addr::cRZBaseString_Sprintf)(
            &message, "cGZCOMLibrary::Load: GZCOM Director failed initialization in library: \"%s\"\n",
            At<char*>(self, library::kPathBegin));
        LibraryFree(self, nullptr);
        DestroyString(message);
        DestroyString(systemPath);
        return 0;
    }
    DestroyString(systemPath);
    return 1;
}

// The game's 0x0087BC87, the order of the library tree: whether this
// record's path sorts before the other's (bytes compared as unsigned, a
// prefix first). The other's path is fetched through its GetPath slot. The
// original passes null for this path when the string's address is null,
// which it never is.
uint8_t __fastcall LibraryPathLess(char* self, void*, char* other)
{
    GameString path;
    This<void*>(addr::SC4String_SC4String, &path);
    Virtual<uint32_t>(other, library::kGetPath, &path);
    const uint8_t less = Cdecl<uint8_t>(addr::SC4String_RangeLess, self + library::kPathBegin, &path.begin);
    DestroyString(path);
    return less;
}

// --- The class map --------------------------------------------------------------

// The game's 0x0087B7F9: links a new node for `entry` under `parent`.
TreeInsertResult* __fastcall ClassMapInsertAt(Tree* self, void*, TreeInsertResult* result, TreeNode* x,
                                              ClassNode* parent, const ClassEntry* entry, TreeNode* w)
{
    ClassNode* node;
    if (parent != self->header && (w || (!x && !(entry->classId < parent->entry.classId)))) {
        node = This<ClassNode*>(addr::cGZCOM_ClassMapNewNode, self, entry);
        parent->right = node;
        if (parent == self->header->right)
            self->header->right = node;
    } else {
        node = This<ClassNode*>(addr::cGZCOM_ClassMapNewNode, self, entry);
        parent->left = node;
        if (parent == self->header) {
            self->header->parent = node;
            self->header->right = node;
        } else if (parent == self->header->left) {
            self->header->left = node;
        }
    }
    node->left = nullptr;
    node->right = nullptr;
    node->parent = parent;
    Cdecl<void>(addr::SC4Tree_RebalanceForInsert, static_cast<TreeNode*>(node), &self->header->parent);
    ++self->count;
    result->node = node;
    return result;
}

// The game's 0x0087B91D: inserts `entry` unless its class ID is there.
TreeInsertResult* __fastcall ClassMapInsertUnique(Tree* self, void*, TreeInsertResult* result,
                                                  const ClassEntry* entry)
{
    TreeNode* const header = self->header;
    TreeNode* parent = header;
    TreeNode* x = header->parent;
    bool less = true;
    while (x) {
        parent = x;
        less = entry->classId < static_cast<ClassNode*>(x)->entry.classId;
        x = less ? x->left : x->right;
    }
    TreeNode* previous = parent;
    bool insert = false;
    if (less) {
        if (parent == header->left)
            insert = true;
        else
            previous = Previous(parent);
    }
    if (!insert && static_cast<ClassNode*>(previous)->entry.classId >= entry->classId) {
        result->node = previous;
        result->inserted = 0;
        return result;
    }
    TreeInsertResult linked;
    result->node =
        ClassMapInsertAt(self, nullptr, &linked, x, static_cast<ClassNode*>(parent), entry, nullptr)->node;
    result->inserted = 1;
    return result;
}

// The game's 0x0087BD51 (cdecl), the callback a director's EnumClassObjects
// gets, with the COM object as its context: an entry of a lower version
// goes, then the class is entered for the library being loaded if its ID is
// not there.
void __cdecl RegisterClassObject(uint32_t classId, uint32_t version, char* comObject)
{
    Tree* const classes = &At<Tree>(comObject, com::kClassMap);
    ClassNode* const found = This<ClassNode*>(addr::cGZCOM_ClassMapFind, classes, &classId);
    if (found != classes->header && found->entry.version < version)
        This<void>(addr::cGZDBSegmentPackedFile_RecordMapErase, classes, static_cast<TreeNode*>(found));
    ClassEntry entry;
    entry.record = At<char*>(comObject, com::kLoading);
    entry.classId = classId;
    entry.version = version;
    TreeInsertResult result;
    ClassMapInsertUnique(classes, nullptr, &result, &entry);
}

// --- The library tree -----------------------------------------------------------

// The game's 0x0087BE2C: the node whose path equals the record's, or the
// header.
TreeNode* __fastcall LibraryFind(Tree* self, void*, char* key)
{
    TreeNode* found = self->header;
    TreeNode* node = found->parent;
    while (node) {
        if (!LibraryPathLess(LibraryOf(node), nullptr, key)) {
            found = node;
            node = node->left;
        } else {
            node = node->right;
        }
    }
    if (found != self->header && !LibraryPathLess(key, nullptr, LibraryOf(found)))
        return found;
    return self->header;
}

// The game's 0x0087BF21: a new tree node holding a copy of the record (the
// copy constructor is 0x0087BEE4; the original skips it when the node's
// value address is null, which it never is).
LibraryNode* __fastcall LibraryNewNode(Tree*, void*, char* record)
{
    auto* const node = Cdecl<LibraryNode*>(addr::SC4_stl_allocate, kLibraryNodeSize);
    This<void*>(addr::cGZCOMLibrary_CopyConstruct, node->record, record);
    return node;
}

// The game's 0x0087BF42: links a new node for `record` under `parent`.
TreeInsertResult* __fastcall LibraryInsertAt(Tree* self, void*, TreeInsertResult* result, TreeNode* x,
                                             TreeNode* parent, char* record, TreeNode* w)
{
    LibraryNode* node;
    if (parent != self->header && (w || (!x && !LibraryPathLess(record, nullptr, LibraryOf(parent))))) {
        node = LibraryNewNode(self, nullptr, record);
        parent->right = node;
        if (parent == self->header->right)
            self->header->right = node;
    } else {
        node = LibraryNewNode(self, nullptr, record);
        parent->left = node;
        if (parent == self->header) {
            self->header->parent = node;
            self->header->right = node;
        } else if (parent == self->header->left) {
            self->header->left = node;
        }
    }
    node->left = nullptr;
    node->right = nullptr;
    node->parent = parent;
    Cdecl<void>(addr::SC4Tree_RebalanceForInsert, static_cast<TreeNode*>(node), &self->header->parent);
    ++self->count;
    result->node = node;
    return result;
}

// The game's 0x0087BFD4: inserts a copy of `record` unless its path is
// there.
TreeInsertResult* __fastcall LibraryInsertUnique(Tree* self, void*, TreeInsertResult* result, char* record)
{
    TreeNode* parent = self->header;
    TreeNode* x = parent->parent;
    uint8_t less = 1;
    while (x) {
        parent = x;
        less = LibraryPathLess(record, nullptr, LibraryOf(x));
        x = less ? x->left : x->right;
    }
    TreeNode* previous = parent;
    bool insert = false;
    if (less) {
        if (parent == self->header->left)
            insert = true;
        else
            previous = Previous(parent);
    }
    if (!insert && !LibraryPathLess(LibraryOf(previous), nullptr, record)) {
        result->inserted = 0;
        result->node = previous;
        return result;
    }
    TreeInsertResult linked;
    TreeNode* const node = LibraryInsertAt(self, nullptr, &linked, x, parent, record, nullptr)->node;
    result->inserted = 1;
    result->node = node;
    return result;
}

// The game's 0x0087C05E: the unique insert through a temporary result.
TreeInsertResult* __fastcall LibraryInsert(Tree* self, void*, TreeInsertResult* result, char* record)
{
    TreeInsertResult inserted;
    LibraryInsertUnique(self, nullptr, &inserted, record);
    result->node = inserted.node;
    result->inserted = inserted.inserted;
    return result;
}

// --- The COM object -------------------------------------------------------------

// The game's 0x0087B38D: whether a library may be unloaded. One that is not
// loaded may; a loaded one's director decides (CanUnloadNow).
uint8_t __fastcall ComCanUnload(char*, void*, char* record)
{
    if (!Virtual<uint8_t>(record, library::kIsLoaded))
        return 1;
    void* const dllDirector = Virtual<void*>(record, library::kGetDirector);
    return Virtual<uint8_t>(dllDirector, director::kCanUnloadNow);
}

// The game's 0x0087B626 (COM slot 6, no arguments): under the lock, frees
// every loaded library whose director agrees. Ends in a jump to the unlock,
// so it answers what that answers.
uint32_t __fastcall ComFreeUnusedLibraries(char* self, void*)
{
    void* const lockObject = self + com::kLock;
    Virtual<void>(lockObject, lock::kLock);
    TreeNode* const header = At<Tree>(self, com::kLibraries).header;
    TreeNode* node = header->left;
    if (node != header) {
        do {
            char* const record = LibraryOf(node);
            if (Virtual<uint8_t>(record, library::kIsLoaded) && ComCanUnload(self, nullptr, record))
                LibraryFree(record, nullptr);
            node = Next(node);
        } while (node != At<Tree>(self, com::kLibraries).header);
    }
    return Virtual<uint32_t>(lockObject, lock::kUnlock);
}

// The game's 0x0087B67B, called by RealShutdown: under the lock, frees every
// loaded library, in the tree's (path) order.
uint32_t __fastcall ComFreeAllLibraries(char* self, void*)
{
    void* const lockObject = self + com::kLock;
    Virtual<void>(lockObject, lock::kLock);
    TreeNode* const header = At<Tree>(self, com::kLibraries).header;
    TreeNode* node = header->left;
    if (node != header) {
        do {
            char* const record = LibraryOf(node);
            if (Virtual<uint8_t>(record, library::kIsLoaded))
                LibraryFree(record, nullptr);
            node = Next(node);
        } while (node != At<Tree>(self, com::kLibraries).header);
    }
    return Virtual<uint32_t>(lockObject, lock::kUnlock);
}

// The game's 0x0087B6C4 (COM slot 13): the library records, each with a
// reference added, into the caller's array; without an array, the count.
// *count is clamped to the number of libraries. No lock.
uint32_t __fastcall ComListLibraries(char* self, void*, char** list, uint32_t* count)
{
    const uint32_t total = At<uint32_t>(self, com::kLibraryCount);
    if (!list) {
        *count = total;
        return *count;
    }
    TreeNode* node = At<Tree>(self, com::kLibraries).header->left;
    if (*count > total)
        *count = total;
    for (uint32_t i = 0; i < *count;) {
        char* const record = LibraryOf(node);
        list[i] = record;
        Virtual<uint32_t>(record, unknown::kAddRef);
        ++i;
        node = Next(node);
    }
    return *count;
}

// The game's 0x0087B70F (COM slot 12): calls callback(record, context)
// (cdecl) for each library in order until it answers false. No lock. The
// original leaves whatever is in EAX.
void __fastcall ComEnumerateLibraries(char* self, void*, uint8_t(__cdecl* callback)(char*, void*), void* context)
{
    if (!callback)
        return;
    TreeNode* const header = At<Tree>(self, com::kLibraries).header;
    TreeNode* node = header->left;
    while (node != header) {
        if (!callback(LibraryOf(node), context))
            break;
        node = Next(node);
    }
}

// The game's 0x0087BDA6: a class object from a library, which is loaded
// first when it is not.
uint8_t __fastcall ComGetClassObjectFrom(char*, void*, char* record, uint32_t classId, uint32_t iid, void** out)
{
    if (!Virtual<uint8_t>(record, library::kIsLoaded)) {
        if (!LibraryLoad(record, nullptr))
            return 0;
    }
    void* const dllDirector = Virtual<void*>(record, library::kGetDirector);
    return Virtual<uint8_t>(dllDirector, director::kGetClassObject, classId, iid, out);
}

// The game's 0x0087BDDF: loads a library and, under the lock, lets its
// director register its classes.
uint8_t __fastcall ComLoadLibrary(char* self, void*, char* record)
{
    if (!LibraryLoad(record, nullptr))
        return 0;
    void* const dllDirector = Virtual<void*>(record, library::kGetDirector);
    void* const lockObject = self + com::kLock;
    Virtual<void>(lockObject, lock::kLock);
    At<char*>(self, com::kLoading) = record;
    // The callback is the game's address, so a DLL sees the pointer it
    // always saw; with this group on that address runs RegisterClassObject.
    Virtual<void>(dllDirector, director::kEnumClassObjects, reinterpret_cast<void*>(addr::cGZCOM_RegisterClassObject), self);
    Virtual<void>(lockObject, lock::kUnlock);
    return 1;
}

// The game's 0x0087BE75 (COM slot 3): a class from the library that
// registered it, else the framework itself for class 0x3E8, else from the
// root director.
uint8_t __fastcall ComGetClassObject(char* self, void*, uint32_t classId, uint32_t iid, void** out)
{
    void* const lockObject = self + com::kLock;
    Virtual<void>(lockObject, lock::kLock);
    Tree* const classes = &At<Tree>(self, com::kClassMap);
    ClassNode* const found = This<ClassNode*>(addr::cGZCOM_ClassMapFind, classes, &classId);
    if (found == classes->header) {
        Virtual<void>(lockObject, lock::kUnlock);
        if (classId == kFrameworkClass)
            return Virtual<uint8_t>(At<void*>(self, com::kFramework), unknown::kQueryInterface, iid, out);
        return Virtual<uint8_t>(At<void*>(self, com::kRootDirector), director::kGetClassObject, classId, iid, out);
    }
    char* const record = found->entry.record;
    Virtual<void>(lockObject, lock::kUnlock);
    return ComGetClassObjectFrom(self, nullptr, record, classId, iid, out);
}

// The game's 0x0087C081 (COM slot 5): adds a library by path. True when the
// path was known already or the library loaded; the director's OnStart
// answer is not looked at.
uint8_t __fastcall ComAddLibrary(char* self, void*, void* path)
{
    char key[library::kSize];
    LibraryConstruct(key, nullptr, path);
    void* const lockObject = self + com::kLock;
    Virtual<void>(lockObject, lock::kLock);
    Tree* const libraries = &At<Tree>(self, com::kLibraries);
    uint8_t added;
    if (LibraryFind(libraries, nullptr, key) != libraries->header) {
        Virtual<void>(lockObject, lock::kUnlock);
        added = 1;
    } else {
        TreeInsertResult inserted;
        TreeNode* const node = LibraryInsert(libraries, nullptr, &inserted, key)->node;
        Virtual<void>(lockObject, lock::kUnlock);
        char* const record = LibraryOf(node);
        if (ComLoadLibrary(self, nullptr, record)) {
            void* const dllDirector = Virtual<void*>(record, library::kGetDirector);
            if (dllDirector)
                Virtual<uint8_t>(dllDirector, director::kOnStart, self);
            added = 1;
        } else {
            added = 0;
        }
    }
    At<std::uintptr_t>(key, library::kVtable) = addr::cGZCOMLibrary_Vtable;
    This<void>(addr::SC4String_Destroy, key + library::kPath);
    return added;
}

bool InstallPluginDlls1(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[28] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cGZCOMLibrary_GuardedLoadLibraryA, reinterpret_cast<void*>(&GuardedLoadLibraryA)},
        {addr::cGZCOMLibrary_GuardedLoadLibraryW, reinterpret_cast<void*>(&GuardedLoadLibraryW)},
        {addr::cGZCOMLibrary_GuardedFreeLibrary, reinterpret_cast<void*>(&GuardedFreeLibrary)},
        {addr::cGZCOMLibrary_GuardedGetProcAddress, reinterpret_cast<void*>(&GuardedGetProcAddress)},
        {addr::cGZCOMLibrary_QueryInterface, reinterpret_cast<void*>(&LibraryQueryInterface)},
        {addr::cGZCOMLibrary_GetPath, reinterpret_cast<void*>(&LibraryGetPath)},
        {addr::cGZCOM_CanUnloadLibrary, reinterpret_cast<void*>(&ComCanUnload)},
        {addr::cGZCOMLibrary_Free, reinterpret_cast<void*>(&LibraryFree)},
        {addr::cGZCOM_FreeUnusedLibraries, reinterpret_cast<void*>(&ComFreeUnusedLibraries)},
        {addr::cGZCOM_FreeAllLibraries, reinterpret_cast<void*>(&ComFreeAllLibraries)},
        {addr::cGZCOM_EnumerateLibraries_List, reinterpret_cast<void*>(&ComListLibraries)},
        {addr::cGZCOM_EnumerateLibraries_Callback, reinterpret_cast<void*>(&ComEnumerateLibraries)},
        {addr::cGZCOM_ClassMapInsertAt, reinterpret_cast<void*>(&ClassMapInsertAt)},
        {addr::cGZCOM_ClassMapInsertUnique, reinterpret_cast<void*>(&ClassMapInsertUnique)},
        {addr::cGZCOMLibrary_DeletingDestructor, reinterpret_cast<void*>(&LibraryDelete)},
        {addr::cGZCOMLibrary_cGZCOMLibrary, reinterpret_cast<void*>(&LibraryConstruct)},
        {addr::cGZCOMLibrary_Load, reinterpret_cast<void*>(&LibraryLoad)},
        {addr::cGZCOMLibrary_PathLess, reinterpret_cast<void*>(&LibraryPathLess)},
        {addr::cGZCOM_RegisterClassObject, reinterpret_cast<void*>(&RegisterClassObject)},
        {addr::cGZCOM_GetClassObjectFromLibrary, reinterpret_cast<void*>(&ComGetClassObjectFrom)},
        {addr::cGZCOM_LoadLibraryAndRegister, reinterpret_cast<void*>(&ComLoadLibrary)},
        {addr::cGZCOM_LibraryFind, reinterpret_cast<void*>(&LibraryFind)},
        {addr::cGZCOM_GetClassObject, reinterpret_cast<void*>(&ComGetClassObject)},
        {addr::cGZCOM_LibraryNewNode, reinterpret_cast<void*>(&LibraryNewNode)},
        {addr::cGZCOM_LibraryInsertAt, reinterpret_cast<void*>(&LibraryInsertAt)},
        {addr::cGZCOM_LibraryInsertUnique, reinterpret_cast<void*>(&LibraryInsertUnique)},
        {addr::cGZCOM_LibraryInsert, reinterpret_cast<void*>(&LibraryInsert)},
        {addr::cGZCOM_AddLibrary, reinterpret_cast<void*>(&ComAddLibrary)},
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

const bool g_pluginDlls1Registered = registry::Register({
    "resources.plugin_dlls.libraries",
    "resources",
    registry::Kind::Detour,
    registry::Phase::Attach,
    addr::cGZCOM_AddLibrary,
    "Our GZCOM library code: loading plugin DLLs, their class registration and unloading (docs/plugin-dlls.md)",
    false,
    InstallPluginDlls1,
});

} // namespace
} // namespace opensc4::plugin_dlls
