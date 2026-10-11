// The application's requests, second half (0x00454E50 to 0x00456000): what
// cSC4App does when the player obliterates the city or asks to save it, how
// it closes a city, and the terrain file dialog of the new city window.
//
// The application object has its main vtable at +0 (0x00A86C2C), a message
// target at +4 (0x00A86C1C), a window procedure at +8 (0x00A86C0C, four
// slots) and the interface cISC4App at +0xC (0x00A86AF0); the global
// 0x00B43C94 holds the interface. Interface methods get object + 0xC as
// `this`, helpers the object itself (see ui_app_4.cpp for the first half).
//
// What this part holds:
//
//   - Obliterate 0x00454E50, which the application's message handler
//     (0x00451ED0) calls for message 0xEA65224E: writes the city as an
//     obliterated one to a temporary file (0x00454510), destroys the city,
//     DELETES THE CITY'S FILE, makes the city again from the temporary file
//     (main slot 13 with the open file at +0x5B8), saves it under the old
//     path (interface slot 15, the game's SaveCity) and puts the view's zoom
//     and rotation back. Asks nothing;
//   - the terrain file dialog 0x00455060 (cdecl): runs the picture file
//     dialog of ui_panels_7.cpp as a modal window, starting from the folder
//     kept in the application's ini file ("Settings", "Terrain Load/Save
//     Location") and writing the folder back when a file was chosen;
//   - CloseCity 0x00455330 (interface slot 14): destroys the city
//     (0x00454A60) and tells the region: a city that was never saved is
//     taken out of the region, a saved one is read again from its file
//     together with every city that touches it;
//   - the window procedure 0x004559E0 (slot 3 of the part at +8) for the
//     window 0x27DF05BE: button 0x20 ends the modal run with 0x20, button
//     0x30 runs the terrain file dialog and copies the chosen path into the
//     new city specification (interface slot 39), cut to 0x1FF characters;
//   - RequestSaveCity 0x00455AE0 (interface slot 9): saves the city under
//     its path (interface slot 15) and tells the player how it went with a
//     message window.
//
// Replacement, off by default:
//
//   ui.app.part5: the five functions named above.
//
// Not functions: 0x004554D0 (the middle of CloseCity's loop) and 0x00455BA0
// (the middle of RequestSaveCity).
//
// Still the game's: cSC4App::LoadCity 0x004555F0 and cSC4App::SaveCity
// 0x00455E20 (the tests and other work depend on them as they are), and
// SC4String::ConcatTextBody 0x00455540 (string code the script context
// calls too).
//
// The application, as far as this part touches it (offsets from the
// object): +0x0C the interface, +0x58 the holder of the regional city (a
// pointer to a pointer, the caller's), +0x5C the city, +0x5B8 the file a
// city is being made from.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace opensc4::ui {
namespace {

namespace app { // offsets from the object
} // namespace app

namespace iface { // the same fields from the interface (object + 0xC)
constexpr std::size_t kCity = 0x50;
} // namespace iface

constexpr uint32_t kTextGroup = 0x6A231EAA;
constexpr uint32_t kNewCityWindow = 0x27DF05BE;
constexpr uint32_t kCommand = 3;
constexpr uint32_t kButtonClicked = 0x287259F6;
constexpr uint32_t kEndModal = 0xF;
constexpr uint32_t kCancelButton = 0x20;
constexpr uint32_t kBrowseButton = 0x30;

// The game's string body and string.
struct StringBody {
    char* begin;
    char* end;
    char* capacity;
};

struct GameString {
    std::uintptr_t vtable;
    StringBody body;
    uint32_t references;
};
static_assert(sizeof(GameString) == 0x14);

// The game's localized string. 0x20 bytes: RequestSaveCity's frame has
// three of them at +0x28, +0x60 and +0x80 of its locals (which end at
// +0xA0), with two 12-byte string bodies between the first and the second.
struct LocalizedString {
    GameString text;
    uint32_t rest[3];
};
static_assert(sizeof(LocalizedString) == 0x20);

// A window message as a window procedure gets it.
struct WinMessage {
    uint32_t type;
    uint32_t a;
    uint32_t b;
    uint32_t c;
};

// A node of the game's list of pointers.
struct ListNode {
    ListNode* next;
    ListNode* previous;
    void* value;
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

// An empty string as the game makes one inline: eight bytes of buffer.
void MakeEmpty(GameString& text)
{
    text.body.begin = nullptr;
    text.body.end = nullptr;
    text.body.capacity = nullptr;
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    text.body.begin = buffer;
    text.body.end = buffer;
    text.body.capacity = buffer + 8;
    *buffer = 0;
    text.vtable = addr::SC4String_Vtable;
    text.references = 0;
}

// A string from a range of characters, as the game makes one inline.
void MakeFromRange(GameString& text, const char* begin, const char* end, void* tag)
{
    text.body.begin = nullptr;
    text.body.end = nullptr;
    text.body.capacity = nullptr;
    This<void>(addr::SC4String_FromRange, &text.body, begin, end, tag);
    text.vtable = addr::SC4String_Vtable;
    text.references = 0;
}

void DestroyString(GameString& text)
{
    char* const buffer = text.body.begin;
    text.vtable = addr::SC4String_Vtable;
    if (buffer)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(buffer));
}

void FreeBody(StringBody& body)
{
    if (body.begin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(body.begin));
}

// The game's 0x00455060, cdecl (cIGZString* out). The picture file dialog
// run as a modal window in the middle of the main window, starting at the
// folder the application's ini file keeps. True, with the chosen file's
// path copied into `out`, when the run ended with something other than 0
// and the dialog's browser has a file; the dialog's folder goes back into
// the ini file whenever the run ended with something other than 0.
uint8_t __cdecl BrowseTerrainFile(void* out)
{
    static const char kSection[] = "Settings";
    static const char kKey[] = "Terrain Load/Save Location";

    uint8_t result = 0;
    alignas(4) char ini[0x3C];
    This<void*>(addr::cRZIniFile_ConstructEmpty, &ini[0]);
    GameString iniPath;
    MakeEmpty(iniPath);
    GameString section;
    MakeFromRange(section, &kSection[0], &kSection[8], static_cast<void*>(&result));
    GameString key;
    MakeFromRange(key, &kKey[0], &kKey[26], static_cast<void*>(&result));
    GameString folder;
    MakeEmpty(folder);

    if (void* const application = Global<void*>(addr::SC4_Service43C94)) {
        Virtual<uint8_t>(application, 0x74, &iniPath);
        This<void>(addr::cRZIniFile_SetPath, &ini[0], &iniPath);
        This<uint8_t>(addr::cRZIniFile_GetString, &ini[0], &section, &key, &folder);
    }

    char* const memory = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0xF0});
    void* const dialog = memory ? This<void*>(addr::SC4UI_F79D810, memory) : nullptr;
    Virtual<uint32_t>(dialog, 0x4);
    Virtual<void>(dialog, 0x110, uint32_t{0x10000}, uint32_t{1});
    Virtual<void>(dialog, 0x110, uint32_t{0x100}, uint32_t{1});
    This<void>(addr::SC4UI_F79DBB0, dialog, &folder);
    Virtual<uint8_t>(dialog, 0x10);

    void* const manager = Cdecl<void*>(addr::SC4_GetWinManager);
    if (void* const main = Virtual<void*>(manager, 0xC)) {
        const int32_t width = Virtual<int32_t>(dialog, 0xA4);
        const int32_t x = (Virtual<int32_t>(main, 0xA4) - width) / 2;
        const int32_t height = Virtual<int32_t>(dialog, 0xA8);
        const int32_t y = (Virtual<int32_t>(main, 0xA8) - height) / 2;
        Virtual<void>(dialog, 0xE0, x, y);
    }

    Virtual<void>(Global<void*>(addr::SC4_Service43CE0), 0x28, dialog, uint32_t{1});
    if (Virtual<uint32_t>(manager, 0xA4, dialog) != 0) {
        This<void>(addr::SC4UI_F79DB60, dialog, &folder);
        This<uint8_t>(addr::cRZIniFile_SetString, &ini[0], &section, &key, &folder);
        // 0x14 bytes that the browser fills (the frame leaves that much
        // before the ini file); only the third dword is read here. Not set
        // before the call in the original either.
        uint32_t chosen[5];
        if (This<uint8_t>(addr::SC4UI_F79D650, dialog, &chosen[0])) {
            Virtual<void>(out, 0x34, chosen[2]);
            result = 1;
        }
    }
    Virtual<void>(Global<void*>(addr::SC4_Service43CE0), 0x18, dialog);
    Virtual<void>(manager, 0x5C, dialog);
    Virtual<uint32_t>(dialog, 0x8);

    DestroyString(folder);
    DestroyString(key);
    DestroyString(section);
    DestroyString(iniPath);
    This<void>(addr::cRZIniFile_Destruct, &ini[0]);
    return result;
}

// The game's 0x004559E0, slot 3 of the application's part at +8: a window
// procedure (window, message), ret 8; `this` is not used. Takes the clicks
// of the window 0x27DF05BE: button 0x20 ends the modal run with 0x20,
// button 0x30 runs the terrain file dialog and copies the path it gave, cut
// to 0x1FF characters, into the buffer that interface slot 39 answers with
// (the new city specification). True for every click in that window.
uint8_t __fastcall WindowMessage(char*, void*, void* window, WinMessage* message)
{
    if (Virtual<uint32_t>(window, 0xFC) != kNewCityWindow)
        return 0;
    if (message->type != kCommand || message->a != kButtonClicked)
        return 0;

    void* const manager = Cdecl<void*>(addr::SC4_GetWinManager);
    const uint32_t button = message->b;
    if (button == kCancelButton) {
        Virtual<void>(manager, 0x24, kEndModal, window, kCancelButton);
        return 1;
    }
    if (button != kBrowseButton)
        return 1;

    GameString path;
    This<void*>(addr::SC4String_SC4String, &path);
    if (BrowseTerrainFile(&path)) {
        char* target = Virtual<char*>(Global<void*>(addr::SC4_Service43C94), 0x9C);
        // The string's own erase from the 0x1FFth character on, inline.
        if (static_cast<uint32_t>(path.body.end - path.body.begin) > 0x1FF) {
            char* const cut = path.body.begin + 0x1FF;
            char* const end = path.body.end;
            if (cut != end) {
                Cdecl<void*>(addr::SC4_memmove, static_cast<void*>(cut), static_cast<void*>(end), uint32_t{1});
                path.body.end = path.body.end + (cut - end);
            }
        }
        const char* source = path.body.begin;
        char moved;
        do {
            moved = *source++;
            *target++ = moved;
        } while (moved);
    }
    This<void>(addr::SC4String_Destroy, &path);
    return 1;
}

// The text of a message window about a save: `second` gets " (<file>)"
// added. The two branches of RequestSaveCity build the same text in two
// ways (the string bodies in the other order in the frame, and the last
// append by another function); `saved` says which.
void AddFileName(LocalizedString& second, GameString& fileName, bool saved, uint8_t* tag)
{
    static const char kOpen[] = " (";
    static const char kClose[] = ")";
    StringBody opened;
    StringBody closed;
    StringBody* const first = Cdecl<StringBody*>(addr::SC4String_ConcatTextBody, &opened, &kOpen[0], &fileName.body);
    StringBody* const whole = Cdecl<StringBody*>(addr::SC4String_ConcatBodyText, &closed, first, &kClose[0]);
    if (saved)
        This<void>(addr::SC4String_AppendStd, &second.text.body, whole);
    else
        This<void>(addr::SC4String_AppendRange, &second.text.body, whole->begin, whole->end, static_cast<void*>(tag));
    FreeBody(closed);
    FreeBody(opened);
}

// The game's 0x00455AE0, slot 9 of the interface: RequestSaveCity (tell,
// fast), ret 8; `this` is the interface. With no city: false. A city that
// may not be saved (city slot 0x1A4) gets a message window (texts
// 0xAA666AFD and 0xCA666B15) and false. Else the city is saved under its
// path (interface slot 15 with the path and `fast`; nothing happens when
// the path is empty) and the answer is what the save said; a save that went
// well shows a message window (0x2A56CDA8, 0x2A56CE36 with the file's name)
// only when `tell` is set, one that failed always (0x2A56CDA8, 0x4A56CE71).
uint8_t __fastcall RequestSaveCity(char* self, void*, uint32_t tell, uint32_t fast)
{
    uint8_t result = 0;
    uint8_t guard[4];
    This<void*>(addr::SC4ModalGuard_Construct, &guard[0], uint32_t{0}, uint32_t{1}, uint32_t{1}, uint32_t{1});

    if (void* const city = At<void*>(self, iface::kCity)) {
        if (Virtual<uint8_t>(city, 0x1A4)) {
            LocalizedString title;
            This<void*>(addr::SC4LocalizedString_SC4LocalizedString, &title, kTextGroup, uint32_t{0xAA666AFD},
                        uint32_t{1});
            LocalizedString text;
            This<void*>(addr::SC4LocalizedString_SC4LocalizedString, &text, kTextGroup, uint32_t{0xCA666B15},
                        uint32_t{1});
            Cdecl<void>(addr::SC4UI_MessageDialog, &title, &text);
            DestroyString(text.text);
            DestroyString(title.text);
            This<void>(addr::SC4ModalGuard_Destruct, &guard[0]);
            return 0;
        }

        GameString path;
        MakeEmpty(path);
        Virtual<void>(At<void*>(self, iface::kCity), 0x28, &path);
        if (path.body.begin != path.body.end) {
            result = Virtual<uint8_t>(self, 0x3C, &path, fast);
            Virtual<void>(At<void*>(self, iface::kCity), 0x28, &path);
            GameString fileName;
            This<void*>(addr::SC4String_SC4String, &fileName);
            Cdecl<uint8_t>(addr::RZFile_GetFileName, &path, &fileName, uint32_t{1});
            if (result) {
                if (static_cast<uint8_t>(tell)) {
                    LocalizedString title;
                    This<void*>(addr::SC4LocalizedString_SC4LocalizedString, &title, kTextGroup, uint32_t{0x2A56CDA8},
                                uint32_t{1});
                    LocalizedString text;
                    This<void*>(addr::SC4LocalizedString_SC4LocalizedString, &text, kTextGroup, uint32_t{0x2A56CE36},
                                uint32_t{1});
                    AddFileName(text, fileName, true, &result);
                    Cdecl<void>(addr::SC4UI_MessageDialog, &title, &text);
                    This<void>(addr::SC4String_Destroy, &text);
                    This<void>(addr::SC4String_Destroy, &title);
                }
            } else {
                LocalizedString title;
                This<void*>(addr::SC4LocalizedString_SC4LocalizedString, &title, kTextGroup, uint32_t{0x2A56CDA8},
                            uint32_t{1});
                LocalizedString text;
                This<void*>(addr::SC4LocalizedString_SC4LocalizedString, &text, kTextGroup, uint32_t{0x4A56CE71},
                            uint32_t{1});
                AddFileName(text, fileName, false, &result);
                Cdecl<void>(addr::SC4UI_MessageDialog, &title, &text);
                DestroyString(text.text);
                DestroyString(title.text);
            }
            DestroyString(fileName);
        }
        DestroyString(path);
    }
    This<void>(addr::SC4ModalGuard_Destruct, &guard[0]);
    return result;
}

bool InstallApp5(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[5] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F455060, reinterpret_cast<void*>(&BrowseTerrainFile)},
        {addr::SC4UI_F4559E0, reinterpret_cast<void*>(&WindowMessage)},
        {addr::SC4UI_F455AE0, reinterpret_cast<void*>(&RequestSaveCity)},
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

const bool g_app5Registered = registry::Register({
    "ui.app.part5",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F455AE0,
    "Our application requests, second half: obliterating the city, the save request and its message windows, closing "
    "a city, and the terrain file dialog of the new city window (docs/ui.md)",
    false,
    InstallApp5,
});

} // namespace
} // namespace opensc4::ui
