// How the game loads GZCOM plugin DLLs, part 2: the application's scan of
// its two Plugins folders, cSC4App::ScanPluginDlls (0x00451260 to
// 0x00451A10). Replacement, off by default:
//
//   resources.plugin_dlls.scan: the one function below.
//
// The scan is a method of the application object (the application interface
// is the object + 0xC). With an empty folder string it runs itself for the
// install folder's Plugins (interface slot +0xDC) and then for the user
// folder's Plugins (slot +0xEC). With a folder it takes the COM object's
// library list and every library's path, lists the folder's `*.dll` files
// (plain files, no subfolders), and for each file whose full path is not a
// loaded library's path byte for byte, and whose `<name>.ini` (section
// Admin, key Enabled) does not say `false` or `0`, calls the COM object's
// AddLibrary (slot +0x14). It returns the number of AddLibrary calls.
//
// The strings, the list and the ini file are built and destroyed inline, in
// the original's order; the folder listing, the ini file and the file checks
// stay the game's, called by address.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace opensc4::plugin_dlls {
namespace {

// The application object: its interface part.
constexpr std::size_t kAppInterface = 0xC;

namespace app_slot {
constexpr std::size_t kGetPluginDirectory = 0xDC;     // (string* out)
constexpr std::size_t kGetUserPluginDirectory = 0xEC; // (string* out)
} // namespace app_slot

namespace com_slot {
constexpr std::size_t kAddLibrary = 0x14;         // (string* path)
constexpr std::size_t kEnumerateLibraries = 0x34; // (library** array, uint32_t* count)
} // namespace com_slot

namespace library_slot {
constexpr std::size_t kRelease = 0x8;
constexpr std::size_t kGetPath = 0xC; // (string* out)
} // namespace library_slot

// The string's slot for the deleting destructor (flags: 1 free, 2 array).
constexpr std::size_t kStringDelete = 0x70;

// The game's string body and string.
struct StringBody {
    char* begin;
    char* end;
    char* capacity;
};

struct GameString {
    std::uintptr_t vtable;
    StringBody body;
    uint32_t zero;
};
static_assert(sizeof(GameString) == 0x14);

// A node of the folder listing's result: a list of strings.
struct NameNode {
    NameNode* next;
    NameNode* previous;
    GameString name;
};
static_assert(sizeof(NameNode) == 0x1C);

// The game's folder listing (0x34 bytes) and its ini file (0x3C bytes), both
// made on the stack.
struct FolderListing {
    alignas(4) char bytes[0x34];
};

struct IniFile {
    alignas(4) char bytes[0x3C];
};

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

void Free(void* memory)
{
    Cdecl<void>(addr::SC4_Free, memory);
}

void* Allocate(uint32_t size)
{
    return Cdecl<void*>(addr::SC4_stl_allocate, size);
}

// The body of a string the caller holds by pointer (the original's null
// check on the pointer is kept).
StringBody* Body(GameString* text)
{
    return text ? &text->body : nullptr;
}

// An empty string as the game's inlined constructor makes it.
void InitString(GameString& text)
{
    text.body.begin = nullptr;
    text.body.end = nullptr;
    text.body.capacity = nullptr;
    char* const buffer = static_cast<char*>(Allocate(8));
    text.body.begin = buffer;
    text.body.end = buffer;
    text.body.capacity = buffer + 8;
    *buffer = 0;
    text.vtable = addr::SC4String_Vtable;
    text.zero = 0;
}

// The inlined constructor from a constant text of a known length.
void InitString(GameString& text, const char* chars, uint32_t length)
{
    text.body.begin = nullptr;
    text.body.end = nullptr;
    text.body.capacity = nullptr;
    char* const buffer = static_cast<char*>(Allocate(length + 1));
    text.body.begin = buffer;
    text.body.end = buffer;
    text.body.capacity = buffer + length + 1;
    char* const end = Cdecl<char*>(addr::SC4_memmove, buffer, chars, length) + length;
    text.body.end = end;
    *end = 0;
    text.vtable = addr::SC4String_Vtable;
    text.zero = 0;
}

// The inlined destructor.
void DestroyString(GameString& text)
{
    char* const buffer = text.body.begin;
    text.vtable = addr::SC4String_Vtable;
    if (buffer)
        Free(buffer);
}

// The game's 0x00451260 (thiscall on the application object, one argument,
// RET 4): scans a Plugins folder for DLLs and hands each new, enabled one to
// the COM object; with an empty folder string, both Plugins folders.
uint32_t __fastcall ScanPluginDlls(char* self, void*, GameString* folder)
{
    uint32_t added = 0;
    if (folder->body.begin == folder->body.end) {
        GameString path;
        InitString(path);
        char* const app = self + kAppInterface;
        Virtual<void>(app, app_slot::kGetPluginDirectory, &path);
        if (path.body.begin != path.body.end)
            added = ScanPluginDlls(self, nullptr, &path);
        Virtual<void>(app, app_slot::kGetUserPluginDirectory, &path);
        uint32_t total;
        if (path.body.begin != path.body.end)
            total = added + ScanPluginDlls(self, nullptr, &path);
        else
            total = added;
        DestroyString(path);
        return total;
    }
    if (!Cdecl<uint8_t>(addr::RZDirectory_Exists, folder))
        return 0;
    void* const com = Cdecl<void*>(addr::RZGetCOM);
    if (!com)
        return 0;

    // The libraries the COM object has, and each one's path.
    void** libraries = nullptr;
    GameString* paths = nullptr;
    uint32_t count = 0;
    Virtual<void>(com, com_slot::kEnumerateLibraries, static_cast<void**>(nullptr), &count);
    if (count != 0) {
        libraries = Cdecl<void**>(addr::SC4_operator_new, count * 4);
        const uint32_t pathCount = count;
        auto* const block = Cdecl<uint32_t*>(addr::SC4_operator_new, pathCount * 0x14 + 4);
        if (block) {
            auto* const first = reinterpret_cast<GameString*>(block + 1);
            *block = pathCount;
            // The array constructor: (first, size of one, count, constructor).
            reinterpret_cast<void(__stdcall*)(void*, uint32_t, uint32_t, std::uintptr_t)>(addr::SC4UI_F41D490)(
                first, uint32_t{0x14}, pathCount, static_cast<std::uintptr_t>(addr::SC4String_SC4String));
            paths = first;
        } else {
            paths = nullptr;
        }
        Virtual<void>(com, com_slot::kEnumerateLibraries, libraries, &count);
        if (count != 0) {
            GameString* path = paths;
            uint32_t i = 0;
            do {
                Virtual<void>(libraries[i], library_slot::kGetPath, path);
                Virtual<uint32_t>(libraries[i], library_slot::kRelease);
                ++i;
                ++path;
            } while (i < count);
        }
    }

    FolderListing listing;
    This<void*>(addr::cRZDirectory_Construct, &listing, folder, uint32_t{0}, uint32_t{0}, uint32_t{0});
    NameNode* names = nullptr;
    {
        auto* const head = static_cast<NameNode*>(Allocate(0x1C));
        head->next = head;
        head->previous = head;
        names = head;
    }
    GameString pattern;
    This<void*>(addr::SC4String_FromText, &pattern, Global<const char*>(addr::cSC4App_PluginDllPattern));
    GameString fullPath;
    InitString(fullPath);
    GameString iniPath;
    InitString(iniPath);
    GameString iniName;
    InitString(iniName);
    This<void>(addr::cRZDirectory_SetPattern, &listing, &pattern, uint32_t{0});
    This<void>(addr::cRZDirectory_GetFiles, &listing, &names);

    // One byte of the original's frame serves as the empty tag argument of
    // the string helpers and as the ini lookup's result.
    uint8_t scratch;
    NameNode* node = names->next;
    if (node != names) {
        do {
            uint8_t load = 1;
            {
                StringBody joined;
                const StringBody* const both =
                    Cdecl<StringBody*>(addr::cSC4Advice_F408AE0, &joined, Body(folder), &node->name.body);
                GameString copy;
                copy.body.begin = nullptr;
                copy.body.end = nullptr;
                copy.body.capacity = nullptr;
                This<void>(addr::cSC4Advice_F408420, &copy.body, both->begin, both->end, &scratch);
                char* const copyEnd = copy.body.end;
                char* const copyBegin = copy.body.begin;
                copy.zero = 0;
                This<void>(addr::SC4String_AssignRange, &fullPath.body, copyBegin, copyEnd, &scratch);
                copy.vtable = addr::SC4String_Vtable;
                if (copyBegin)
                    Free(copyBegin);
                if (joined.begin)
                    Free(joined.begin);
            }

            // A file that is a loaded library already is passed over.
            bool known = false;
            if (count != 0) {
                const uint32_t length = static_cast<uint32_t>(fullPath.body.end - fullPath.body.begin);
                GameString* path = paths;
                uint32_t i = 0;
                do {
                    const StringBody* const body = Body(path);
                    const char* theirs = body->begin;
                    if (static_cast<uint32_t>(body->end - theirs) == length) {
                        const char* ours = fullPath.body.begin;
                        uint32_t left = length;
                        bool same = true;
                        while (left != 0) {
                            --left;
                            if (*theirs++ != *ours++) {
                                same = false;
                                break;
                            }
                        }
                        if (same) {
                            known = true;
                            break;
                        }
                    }
                    ++i;
                    ++path;
                } while (i < count);
            }

            if (!known) {
                // The settings file: the folder, the file's name without its
                // extension, ".ini".
                StringBody* const folderBody = Body(folder);
                if (folderBody != &iniPath.body)
                    This<void>(addr::SC4String_AssignRange, &iniPath.body, folderBody->begin, folderBody->end,
                               &scratch);
                Cdecl<void>(addr::RZFile_GetFileName, &fullPath, &iniName, uint32_t{0});
                {
                    static const char kExtension[] = ".ini";
                    This<void>(addr::cSC4Advisor_F40EBC0, &iniName.body, &kExtension[0], &kExtension[4], &scratch);
                }
                This<void>(addr::SC4String_AppendRange, &iniPath.body, iniName.body.begin, iniName.body.end,
                           &scratch);
                if (Cdecl<uint8_t>(addr::RZFile_Exists, &iniPath)) {
                    IniFile ini;
                    This<void*>(addr::cRZIniFile_Construct, &ini, &iniPath);
                    GameString value;
                    InitString(value);
                    GameString key;
                    InitString(key, "Enabled", 7);
                    GameString section;
                    InitString(section, "Admin", 5);
                    scratch = This<uint8_t>(addr::cRZIniFile_GetString, &ini, &section, &key, &value);
                    DestroyString(section);
                    DestroyString(key);
                    if (scratch) {
                        GameString no;
                        InitString(no, "false", 5);
                        // Without regard to case (the last argument), or the one character 0.
                        if (This<uint8_t>(addr::cRZBaseString_IsEqual, &value, &no, uint32_t{0}) ||
                            (value.body.end - value.body.begin == 1 && *value.body.begin == '0'))
                            load = 0;
                        DestroyString(no);
                    }
                    DestroyString(value);
                    This<void>(addr::cRZIniFile_Destruct, &ini);
                }
                if (load) {
                    Virtual<uint8_t>(com, com_slot::kAddLibrary, &fullPath);
                    ++added;
                }
            }
            node = node->next;
        } while (node != names);
    }

    Cdecl<void>(addr::SC4_operator_delete, libraries);
    if (paths) {
        uint32_t* const block = reinterpret_cast<uint32_t*>(paths) - 1;
        if (*block != 0)
            Virtual<void*>(paths, kStringDelete, uint32_t{3});
        else
            Cdecl<void>(addr::SC4_operator_delete, block);
    }
    DestroyString(iniName);
    DestroyString(iniPath);
    DestroyString(fullPath);
    DestroyString(pattern);
    This<void>(addr::SC4UI_F43ABA0, &names);
    if (names)
        Free(names);
    This<void>(addr::cRZDirectory_Destruct, &listing);
    return added;
}

bool InstallPluginDlls2(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[1] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSC4App_ScanPluginDlls, reinterpret_cast<void*>(&ScanPluginDlls)},
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

const bool g_pluginDlls2Registered = registry::Register({
    "resources.plugin_dlls.scan",
    "resources",
    registry::Kind::Detour,
    registry::Phase::Attach,
    addr::cSC4App_ScanPluginDlls,
    "Our scan of the two Plugins folders for GZCOM plugin DLLs (docs/plugin-dlls.md)",
    false,
    InstallPluginDlls2,
});

} // namespace
} // namespace opensc4::plugin_dlls
