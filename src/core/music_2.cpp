// The second part of GZSound's music player (docs/audio.md, "Music player"):
// the rest of the player service. Part 1 (music_1.cpp) has its life, COM
// methods, Shutdown and simple methods; the object's layout is in that
// file's header.
//
// Replacement, off by default:
//
//   audio.music.part2: Init 0x008D4687 (service slot 8), the station loader
//   0x008D49F8 (slot 16), the file filters 0x008D3D5D and 0x008D3D33 (slots
//   17 and 18), DoMessage 0x008D319B with its helper 0x008D3132, and the
//   player interface's slots 5 (0x008D30D1, with the post 0x008D2C7D), 7
//   (0x008D3DC6, with the post 0x008D3BF8), 11 (0x008D484A), 13 (0x008D490B,
//   with the post 0x008D2BFF), 14 (0x008D3066), 19 (0x008D38E5), 21
//   (0x008D33FF, with the find 0x008D335B and its loop 0x008D2C96), 22
//   (0x008D3733) and 23 (0x008D2F21). 20 functions.
//
// As part 1, the group goes in when core.dll is injected (phase Attach):
// Init runs while the application starts.
//
// Still the game's, called by address: 0x008D3333 (a vector's erase at one
// place; ten callers outside the player), 0x009A1ADD (a vector's erase of a
// range) and 0x008FF785 (a vector's push_back), both called from all over
// the game; the framework's string, folder and file code; the station's
// constructor 0x008D376E (part 3).
//
// What part 2 found out:
//   - The object at +0x6C is not a station. Init makes it through the COM
//     director (RZGetCOM, slot 3) with the class ID that service 0x1AF85BFB
//     gives (its slot 9) and interface 0x206077D2: the framework's sound
//     data source player. DoMessage drives it.
//   - The stations are the 0x100-byte class of 0x008D376E (part 3). Slot 16
//     makes one for each folder under "<data>\Res\Music\Radio\Stations\"
//     that holds a "*.id" file, and gives it its folders: the station's own,
//     "Music\", "DJ\<language>\", the shared "Commercials\<language>\" and
//     "Jingle\<language>\". The language folder comes from service
//     0x441E5070 (kept weakly at +0x64, slot 13): its slot 15 says how many
//     there are and its slot 15 (offset 0x3C) names one; when a station's
//     folder for the first has no music files and option 3 is set, the
//     second is used, otherwise none.
//   - A station's 4-character ID (its slot 9 writes it) is what slots 11
//     and 13 look stations up by.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace opensc4::audio {
namespace {

namespace player { // offsets from the object; the interface is at +0x1C
constexpr std::size_t kInterface = 0x1C;
constexpr std::size_t kTarget = 0x20;
constexpr std::size_t kStarted = 0x24;   // byte
constexpr std::size_t kSetting7 = 0x2C;
constexpr std::size_t kStations = 0x30;  // begin, end, capacity
constexpr std::size_t kStationsEnd = 0x34;
constexpr std::size_t kVolume = 0x3C;
constexpr std::size_t kOn = 0x44;        // byte
constexpr std::size_t kStation = 0x48;
constexpr std::size_t kFolder = 0x50;    // SC4String
constexpr std::size_t kLanguages = 0x64; // service 0x441E5070, not held
constexpr std::size_t kServer = 0x68;
constexpr std::size_t kStream = 0x6C;
constexpr std::size_t kQuiet = 0x70;     // byte
constexpr std::size_t kWasPlaying = 0x71; // byte
constexpr std::size_t kPlaying = 0x88;   // byte
constexpr std::size_t kHeld = 0x89;      // byte
} // namespace player

namespace station { // the 0x100-byte station (part 3)
constexpr std::size_t kHalted = 0x24;    // byte
constexpr std::size_t kFolder = 0x70;    // SC4String
constexpr std::size_t kMusic = 0x84;
constexpr std::size_t kDj = 0xA0;
constexpr std::size_t kCommercials = 0xBC;
constexpr std::size_t kJingle = 0xD8;
} // namespace station

constexpr std::size_t kBody = 0x4; // an SC4String's body (begin, end, capacity)

constexpr uint32_t kSoundDataSourceIid = 0x206077D2;
constexpr uint32_t kPlayerIid = 0x09D002D2;
constexpr uint32_t kMessageDataIid = 0x089B7DC8;

constexpr uint32_t kMsgStop = 0x0AB186E5;
constexpr uint32_t kMsgVolume = 0x0AB1A61E;
constexpr uint32_t kMsgSelect = 0x4AB186E9;
constexpr uint32_t kMsgPause = 0x6AB1A480;
constexpr uint32_t kMsgPlayFile = 0xEAB186DF;
constexpr uint32_t kMsgUpdate = 0xEAB1A483;

// The game's string body and string (SC4String, 0x14 bytes).
struct Body {
    char* begin;
    char* end;
    char* capacity;
};
static_assert(sizeof(Body) == 0xC);

struct GameString {
    std::uintptr_t vtable;
    Body body;
    uint32_t zero;
};
static_assert(sizeof(GameString) == 0x14);

struct StringNode {
    StringNode* next;
    StringNode* previous;
    GameString text;
};

// The folder listing object (0x34 bytes; 0x008D49F8's frame).
struct Directory {
    alignas(4) char bytes[0x34];
};

// A reference holder the game fills from the framework (one pointer).
struct Holder {
    void* object;
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

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("music player: first %s", name);
}

char* Player(char* face)
{
    return face - player::kInterface;
}

void FreeText(char* begin)
{
    if (begin)
        Cdecl<void>(addr::SC4_Free, begin);
}

void Construct(GameString& text)
{
    This<void*>(addr::SC4String_SC4String, &text);
}

// What the game does at the end of a string's life on its own stack: the
// vtable put back and the text freed.
void Drop(GameString& text)
{
    text.vtable = addr::SC4String_Vtable;
    FreeText(text.body.begin);
}

// The body of a string, or null for no string.
Body* BodyOf(void* text)
{
    return text ? reinterpret_cast<Body*>(static_cast<char*>(text) + kBody) : nullptr;
}

// `target` = `body` followed by `tail`: the game's concatenation into a
// temporary body (0x0041E270), a string made from it (0x00408B90),
// assigned (0x00424380); both temporaries freed.
void AssignJoinText(void* target, Body* body, const char* tail)
{
    Body joined{};
    Body* made = Cdecl<Body*>(addr::SC4String_ConcatBodyText, &joined, body, tail);
    GameString copy;
    This<void*>(addr::cSC4Advice_F408B90, &copy, made);
    This<void*>(addr::cRZBaseString_Assign, target, &copy);
    Drop(copy);
    FreeText(joined.begin);
}

// The same with two bodies (0x00408AE0).
void AssignJoin(void* target, Body* first, Body* second)
{
    Body joined{};
    Body* made = Cdecl<Body*>(addr::cSC4Advice_F408AE0, &joined, first, second);
    GameString copy;
    This<void*>(addr::cSC4Advice_F408B90, &copy, made);
    This<void*>(addr::cRZBaseString_Assign, target, &copy);
    Drop(copy);
    FreeText(joined.begin);
}

void** Stations(char* self)
{
    return At<void**>(self, player::kStations);
}

void** StationsEnd(char* self)
{
    return At<void**>(self, player::kStationsEnd);
}

void* CurrentStation(char* self)
{
    return Stations(self)[At<uint32_t>(self, player::kStation)];
}

uint32_t Post(char* self, uint32_t type, uint32_t a, uint32_t b)
{
    void* server = At<void*>(self, player::kServer);
    if (!server)
        return 0;
    return Virtual<uint32_t>(server, 0x1C, type, a, b, uint32_t{0}, uint32_t{0});
}

// ---- Small posts -----------------------------------------------------------

// The game's 0x008D2C7D (thiscall on the object).
uint32_t __fastcall PostUpdate(char* self, void*)
{
    static bool logged = false;
    FirstCall("PostUpdate", logged);
    return Post(self, kMsgUpdate, 0, 0);
}

// The game's 0x008D2BFF (thiscall on the object, ret 8).
uint32_t __fastcall PostSelect(char* self, void*, uint32_t a, uint32_t b)
{
    static bool logged = false;
    FirstCall("PostSelect", logged);
    return Post(self, kMsgSelect, a, b);
}

// The game's 0x008D3BF8 (thiscall on the object, ret 4): a copy of the file
// name is sent (slot 5, not posted) with message 0xEAB186DF.
void __fastcall PostPlayFile(char* self, void*, void* name)
{
    static bool logged = false;
    FirstCall("PostPlayFile", logged);
    void* server = At<void*>(self, player::kServer);
    if (!server)
        return;
    void* copy = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x14});
    if (copy)
        copy = This<void*>(addr::SC4String_Substring, copy, name, uint32_t{0}, uint32_t{0xFFFFFFFF});
    Virtual<uint32_t>(At<void*>(self, player::kServer), 0x14, kMsgPlayFile, copy, uint32_t{0}, uint32_t{0},
                      uint32_t{0}, uint32_t{0});
}

// ---- The vector find ---------------------------------------------------------

// The game's 0x008D2C96 (cdecl): std::find over pointers, four at a time;
// the fourth argument is the iterator tag.
void** __cdecl FindPointerLoop(void** first, void** last, void* const* key, void*)
{
    static bool logged = false;
    FirstCall("FindPointerLoop", logged);
    for (int32_t trips = static_cast<int32_t>((last - first) >> 2); trips > 0; --trips) {
        for (int i = 0; i < 4; ++i, ++first) {
            if (*first == *key)
                return first;
        }
    }
    switch (last - first) {
    case 3:
        if (*first == *key)
            return first;
        ++first;
        [[fallthrough]];
    case 2:
        if (*first == *key)
            return first;
        ++first;
        [[fallthrough]];
    case 1:
        if (*first == *key)
            return first;
        break;
    default:
        break;
    }
    return last;
}

// The game's 0x008D335B (cdecl).
void** __cdecl FindPointer(void** first, void** last, void* const* key)
{
    static bool logged = false;
    FirstCall("FindPointer", logged);
    uint8_t tag = 0;
    return Cdecl<void**>(addr::GZMusicPlayer_FindPointerLoop, first, last, key, &tag);
}

// ---- The service's slots -----------------------------------------------------

// The game's 0x008D3D5D (thiscall, ret 4; service slot 17): whether a folder
// holds music files (the patterns "*.mp3,*.wav,*.xa,*.m3u,*.pls"). The
// object is not used.
uint8_t __fastcall IsMusicFolder(char*, void*, void* folder)
{
    static bool logged = false;
    FirstCall("IsMusicFolder", logged);
    Body joined{};
    Body* made = Cdecl<Body*>(addr::SC4String_ConcatBodyText, &joined, BodyOf(folder),
                              Global<const char*>(addr::GZMusicPlayer_MusicPatterns));
    GameString pattern;
    This<void*>(addr::cSC4Advice_F408B90, &pattern, made);
    const uint8_t found = Cdecl<uint8_t>(addr::RZFile_PatternExists, &pattern);
    Drop(pattern);
    FreeText(joined.begin);
    return found;
}

// The game's 0x008D3D33 (thiscall, ret 8; service slot 18): the name of
// language folder `index` from service 0x441E5070 (its slot 15), with a
// backslash after it.
uint8_t __fastcall LanguageFolder(char* self, void*, uint32_t index, GameString* out)
{
    static bool logged = false;
    FirstCall("LanguageFolder", logged);
    if (!Virtual<uint8_t>(At<void*>(self, player::kLanguages), 0x3C, out, index))
        return 0;
    This<void>(addr::SC4String_PushBack, &out->body, uint32_t{'\\'});
    return 1;
}

// The game's 0x008D4687 (thiscall; service slot 8, Init).
uint8_t __fastcall Init(char* self, void*)
{
    static bool logged = false;
    FirstCall("Init", logged);
    Holder sound;
    This<void*>(addr::SC4RefPtr_ConstructSoundService, &sound);
    if (!sound.object) {
        This<void>(addr::SC4RefPtr_DestructGuarded, &sound);
        return 0;
    }
    Virtual<void>(self, 0x38, sound.object);
    Holder languages;
    This<void*>(addr::SC4RefPtr_FromFramework441E5070, &languages);
    Virtual<void>(self, 0x34, languages.object);

    uint8_t result = 1;
    if (!At<uint8_t>(self, player::kStarted)) {
        bool go = true;
        if (!At<void*>(self, player::kStream) && At<void*>(self, player::kServer)) {
            Holder config;
            This<void*>(addr::SC4Audio_F4EDFD0, &config);
            uint32_t classId = 0;
            if (!config.object || !Virtual<uint8_t>(config.object, 0x24, &classId)) {
                This<void>(addr::SC4Audio_F4EDF00, &config);
                go = false;
            } else {
                void* com = Cdecl<void*>(addr::RZGetCOM);
                if (Virtual<uint8_t>(com, 0xC, classId, kSoundDataSourceIid, &At<void*>(self, player::kStream)))
                    Virtual<void>(At<void*>(self, player::kStream), 0xC);
                This<void>(addr::SC4Audio_F4EDF00, &config);
            }
        }
        if (!go) {
            result = 0;
        } else {
            Holder data;
            This<void*>(addr::SC4RefPtr_FromFramework2073215D, &data);
            if (data.object) {
                const char* root = Virtual<const char*>(data.object, 0x1C);
                GameString folder;
                This<void*>(addr::SC4String_FromText, &folder, root);
                GameString radio;
                This<void*>(addr::SC4String_FromText, &radio, reinterpret_cast<const char*>(addr::GZMusicPlayer_RadioFolderText));
                AssignJoin(&folder, &folder.body, &radio.body);
                Drop(radio);
                Virtual<void>(self + player::kInterface, 0x74, &folder);
                Virtual<uint8_t>(self, 0x40);
                At<uint8_t>(self, player::kStarted) = 1;
                if (At<uint8_t>(self, player::kOn)) {
                    At<uint8_t>(self, player::kOn) = 0;
                    Virtual<void>(self + player::kInterface, 0x5C, uint32_t{1});
                }
                Drop(folder);
            }
            if (void* server = At<void*>(self, player::kServer))
                Virtual<void>(server, 0x3C, self + player::kTarget);
            This<void>(addr::SC4Audio_RefPtrDestruct4DE460, &data);
        }
    }
    if (result) {
        void* framework = Cdecl<void*>(addr::RZGetFramework);
        Virtual<void>(framework, 0x24, self);
        framework = Cdecl<void*>(addr::RZGetFramework);
        Virtual<void>(framework, 0x2C, self);
    }
    This<void>(addr::SC4_ReleaseRef2, &languages);
    This<void>(addr::SC4RefPtr_DestructGuarded, &sound);
    return result;
}

// One of the station's three language folders in 0x008D49F8: `folder` is
// the station's string for it, already set to the folder without the
// language. The first language's folder is tried; when it has no music
// files (and there is more than one language), the second is taken if
// option 3 is set, otherwise none.
void ChooseLanguage(char* self, char* folder, GameString& path, GameString& language)
{
    Virtual<uint8_t>(self, 0x48, uint32_t{0}, &language);
    AssignJoin(&path, BodyOf(folder), &language.body);
    if (Virtual<int32_t>(At<void*>(self, player::kLanguages), 0x1C) != 1 && !Virtual<uint8_t>(self, 0x44, &path)) {
        if (Virtual<uint8_t>(self + player::kInterface, 0x68, uint32_t{3}))
            Virtual<uint8_t>(self, 0x48, uint32_t{1}, &language);
        else
            This<void>(addr::SC4String_EraseBody, &language.body, uint32_t{0}, uint32_t{0xFFFFFFFF});
    }
    This<void>(addr::SC4String_AppendStd, BodyOf(folder), &language.body);
}

// The game's 0x008D49F8 (thiscall; service slot 16): the stations, made
// again from the folders. Returns whether there is one.
uint8_t __fastcall LoadStations(char* self, void*)
{
    static bool logged = false;
    FirstCall("LoadStations", logged);
    GameString stations;
    GameString commercials;
    GameString path;
    GameString language;
    Construct(stations);
    Construct(commercials);
    Construct(path);
    Construct(language);
    StringNode* folders = nullptr;
    uint8_t listTag = 0;
    This<void*>(addr::SC4UI_F43AEF0, &folders, &listTag);
    GameString empty;
    Construct(empty);
    Directory finder;
    This<void*>(addr::cRZDirectory_Construct, &finder, &empty, uint32_t{0}, uint32_t{0}, uint32_t{0});
    Drop(empty);

    uint32_t made = 0;
    Body* const root = BodyOf(self + player::kFolder);
    AssignJoinText(&stations, root, Global<const char*>(addr::GZMusicPlayer_StationsText));
    AssignJoinText(&commercials, root, Global<const char*>(addr::GZMusicPlayer_CommercialsText));
    This<void>(addr::cRZDirectory_SetPath, &finder, &stations, uint32_t{0});
    This<void>(addr::cRZDirectory_GetFolders, &finder, &folders, uint32_t{0});

    if (folders->next != folders) {
        Virtual<uint8_t>(self + player::kInterface, 0x58, uint32_t{1});
        made = 0;
        for (StringNode* node = folders->next; node != folders;) {
            This<void*>(addr::cRZBaseString_Assign, &path, &stations);
            Body* name = &node->text.body;
            This<void>(addr::SC4String_AppendStd, &path.body, name);
            This<void>(addr::SC4String_PushBack, &path.body, uint32_t{'\\'});
            This<void>(addr::cSC4Advisor_F410F00, &path.body, Global<const char*>(addr::GZMusicPlayer_IdPatternText));
            if (!Cdecl<uint8_t>(addr::RZFile_PatternExists, &path)) {
                node = node->next;
                continue;
            }
            char* added = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0x100});
            added = added ? This<char*>(addr::GZSound_Construct8D376E, added) : nullptr;
            Virtual<uint32_t>(added, 0x4);
            Virtual<uint8_t>(self + player::kInterface, 0x4C, added);
            ++made;
            Virtual<void>(added, 0x34, uint32_t{(At<uint8_t>(self, player::kSetting7) & 1) != 0});
            This<void>(addr::SC4String_PushBack, name, uint32_t{'\\'});
            AssignJoin(added + station::kFolder, &stations.body, name);
            Body* const own = BodyOf(added + station::kFolder);
            AssignJoinText(added + station::kMusic, own, Global<const char*>(addr::GZMusicPlayer_MusicText));
            AssignJoinText(added + station::kDj, own, Global<const char*>(addr::GZMusicPlayer_DjText));
            ChooseLanguage(self, added + station::kDj, path, language);
            AssignJoinText(added + station::kCommercials, root, Global<const char*>(addr::GZMusicPlayer_CommercialsText));
            ChooseLanguage(self, added + station::kCommercials, path, language);
            AssignJoinText(added + station::kJingle, own, Global<const char*>(addr::GZMusicPlayer_JingleText));
            ChooseLanguage(self, added + station::kJingle, path, language);
            Virtual<void>(added, 0x58);
            node = node->next;
        }
        const uint32_t count = static_cast<uint32_t>(StationsEnd(self) - Stations(self));
        if (At<uint32_t>(self, player::kStation) >= count)
            At<uint32_t>(self, player::kStation) = 0;
    }

    const uint8_t any = made != 0;
    This<void>(addr::cRZDirectory_Destruct, &finder);
    This<void>(addr::SC4UI_F43AF20, &folders);
    Drop(language);
    Drop(path);
    Drop(commercials);
    Drop(stations);
    return any;
}

// ---- The message target (this is the object + 0x20) ---------------------------

// The game's 0x008D3132 (thiscall on the object, ret 4): a file played on
// the stream (its slot 36 with the name, slot 6 with the volume), and the
// name kept (the interface's slot 33).
void __fastcall PlayFile(char* self, void*, void* name)
{
    static bool logged = false;
    FirstCall("PlayFile", logged);
    void* player = nullptr;
    if (Virtual<uint8_t>(At<void*>(self, player::kStream), 0x0, kPlayerIid, &player))
        Virtual<void>(player, 0xC, uint32_t{0}, uint32_t{2});
    Virtual<void>(At<void*>(self, player::kStream), 0x90, name, uint32_t{1});
    Virtual<void>(At<void*>(self, player::kStream), 0x18, At<uint32_t>(self, player::kVolume), uint32_t{0}, uint32_t{1});
    Virtual<void>(self + player::kInterface, 0x84, name);
    if (player)
        Virtual<uint32_t>(player, 0x8);
}

// The game's 0x008D319B (thiscall, ret 4; the message target's slot 3).
uint8_t __fastcall DoMessage(char* target, void*, void* message)
{
    static bool logged = false;
    FirstCall("DoMessage", logged);
    char* const self = target - player::kTarget;
    void* const data = Virtual<void*>(message, 0x18);
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    void* const stream = At<void*>(self, player::kStream);
    switch (type) {
    case kMsgStop:
        Virtual<void>(stream, 0x20);
        return 1;
    case kMsgVolume: {
        const uint32_t volume = Virtual<uint32_t>(data, 0x24);
        At<uint32_t>(self, player::kVolume) = volume;
        if (stream && !At<uint8_t>(self, player::kHeld))
            Virtual<void>(stream, 0x54, volume);
        return 1;
    }
    case kMsgSelect: {
        if (!stream || !Virtual<uint8_t>(stream, 0x1C))
            return 1;
        const uint32_t position = Virtual<uint32_t>(At<void*>(self, player::kStream), 0x50);
        using Seek = void(__thiscall*)(void*, uint32_t, uint32_t, uint32_t);
        const Seek seek = reinterpret_cast<Seek>((*static_cast<void***>(At<void*>(self, player::kStream)))[0x58 / 4]);
        const uint32_t second = Virtual<uint32_t>(data, 0x38, uint32_t{1});
        const uint32_t first = Virtual<uint32_t>(data, 0x24);
        seek(At<void*>(self, player::kStream), position, first, second);
        At<uint8_t>(self, player::kWasPlaying) = 1;
        return 1;
    }
    case kMsgPause: {
        if (!stream)
            return 0;
        if (Virtual<uint32_t>(data, 0x24) == 0 && Virtual<uint8_t>(At<void*>(self, player::kStream), 0x1C))
            return 0;
        Virtual<void>(At<void*>(self, player::kStream), 0x20);
        if (Stations(self) != StationsEnd(self)) {
            char* current = static_cast<char*>(CurrentStation(self));
            At<uint8_t>(current, station::kHalted) = 0;
            Virtual<void>(current, 0x5C, uint32_t{0});
        }
        return 1;
    }
    case kMsgPlayFile: {
        if (!Virtual<uint8_t>(data, 0x70))
            return 1;
        Virtual<void>(At<void*>(self, player::kStream), 0x20);
        void* name = nullptr;
        Virtual<void>(data, 0x74, kMessageDataIid, &name);
        PlayFile(self, nullptr, name);
        if (name)
            Virtual<uint32_t>(name, 0x8);
        return 1;
    }
    case kMsgUpdate:
        if (stream) {
            At<uint8_t>(self, player::kPlaying) = Virtual<uint8_t>(stream, 0x1C);
            At<uint8_t>(self, player::kHeld) = Virtual<uint8_t>(At<void*>(self, player::kStream), 0x5C);
        }
        return 1;
    default:
        return 0;
    }
}

// ---- The player interface (this is the object + 0x1C) --------------------------

// The game's 0x008D30D1 (slot 5, what OnTick calls): while on and with
// stations, an update is posted, and unless the player is quiet or held the
// current station's slot 5 runs (by a jump: its result is returned).
uint32_t __fastcall Update(char* face, void*)
{
    static bool logged = false;
    FirstCall("Update", logged);
    char* const self = Player(face);
    if (!At<uint8_t>(self, player::kOn) || Stations(self) == StationsEnd(self))
        return 0;
    PostUpdate(self, nullptr);
    if (At<uint8_t>(self, player::kQuiet))
        return 0;
    if (Virtual<uint8_t>(self, 0x3C))
        return 0;
    if (At<uint8_t>(self, player::kWasPlaying)) {
        At<uint8_t>(self, player::kWasPlaying) = 0;
        Virtual<uint32_t>(face, 0x7C);
    }
    return Virtual<uint32_t>(CurrentStation(self), 0x14);
}

// The game's 0x008D3DC6 (slot 7, ret 4): a file played if it exists.
uint8_t __fastcall PlayFileIfThere(char* face, void*, void* name)
{
    static bool logged = false;
    FirstCall("PlayFileIfThere", logged);
    if (!Cdecl<uint8_t>(addr::RZFile_Exists, name))
        return 0;
    PostPlayFile(Player(face), nullptr, name);
    return 1;
}

// The game's 0x008D484A (slot 11, ret 4): the station whose 4-character ID
// is the four bytes at `id`, or null.
void* __fastcall FindStation(char* face, void*, const char* id)
{
    static bool logged = false;
    FirstCall("FindStation", logged);
    char* const self = Player(face);
    GameString wanted;
    GameString got;
    Construct(wanted);
    Construct(got);
    This<void>(addr::SC4StringBody_AssignChars, &wanted.body, id, id + 4);
    const char* const blank = reinterpret_cast<const char*>(addr::GZMusicPlayer_BlankIdText);
    This<void>(addr::SC4StringBody_AssignChars, &got.body, blank, blank + 4);
    void* found = nullptr;
    const int32_t count = static_cast<int32_t>(StationsEnd(self) - Stations(self));
    for (int32_t i = 0; i < count; ++i) {
        void* candidate = Stations(self)[i];
        Virtual<void>(candidate, 0x24, got.body.begin);
        if (Cdecl<uint8_t>(addr::SC4UI_F43AB70, &wanted.body, &got.body)) {
            found = Stations(self)[i];
            break;
        }
    }
    FreeText(got.body.begin);
    FreeText(wanted.body.begin);
    return found;
}

// The game's 0x008D490B (slot 13, ret 8): the station by its ID made the
// current one (slot 14); with `position`, a seek to it is posted first and
// the player held (+0x89). The stations are loaded if there are none.
uint8_t __fastcall SelectStation(char* face, void*, const char* id, const uint32_t* position)
{
    static bool logged = false;
    FirstCall("SelectStation", logged);
    char* const self = Player(face);
    GameString wanted;
    GameString got;
    Construct(wanted);
    Construct(got);
    if (position) {
        At<uint8_t>(self, player::kHeld) = 1;
        PostSelect(self, nullptr, position[0], position[1]);
    }
    if (Stations(self) == StationsEnd(self))
        Virtual<uint8_t>(self, 0x40);
    This<void>(addr::SC4StringBody_AssignChars, &wanted.body, id, id + 4);
    const char* const blank = reinterpret_cast<const char*>(addr::GZMusicPlayer_BlankIdText);
    This<void>(addr::SC4StringBody_AssignChars, &got.body, blank, blank + 4);
    uint8_t result = 0;
    const int32_t count = static_cast<int32_t>(StationsEnd(self) - Stations(self));
    for (int32_t i = 0; i < count; ++i) {
        Virtual<void>(Stations(self)[i], 0x24, got.body.begin);
        if (Cdecl<uint8_t>(addr::SC4UI_F43AB70, &wanted.body, &got.body)) {
            result = Virtual<uint8_t>(face, 0x38, static_cast<uint32_t>(i));
            break;
        }
    }
    FreeText(got.body.begin);
    FreeText(wanted.body.begin);
    return result;
}

// The game's 0x008D3066 (slot 14, ret 4): another station made the current
// one. The old one stops (its slot 8) and, unless the player is held, the
// player stops (slot 31); the index is kept within the stations, and the new
// one starts (its slot 7) when the player is on.
uint8_t __fastcall SetStation(char* face, void*, uint32_t index)
{
    static bool logged = false;
    FirstCall("SetStation", logged);
    char* const self = Player(face);
    if (index == At<uint32_t>(self, player::kStation))
        return 1;
    Virtual<void>(CurrentStation(self), 0x20);
    if (!At<uint8_t>(self, player::kHeld))
        Virtual<uint32_t>(face, 0x7C);
    if (Stations(self) == StationsEnd(self))
        return 1;
    const uint32_t count = static_cast<uint32_t>(StationsEnd(self) - Stations(self));
    if (index >= count)
        index = count - 1;
    At<uint32_t>(self, player::kStation) = index;
    if (At<uint8_t>(self, player::kOn))
        Virtual<void>(Stations(self)[index], 0x1C);
    return 1;
}

// The game's 0x008D38E5 (slot 19, ret 4): a station added once. It gets the
// interface (its slot 6) and is held (its slot 3); the first one added
// while the player is on is started through slots 12 and 5.
uint8_t __fastcall AddStation(char* face, void*, void* added)
{
    static bool logged = false;
    FirstCall("AddStation", logged);
    char* const self = Player(face);
    void** const end = StationsEnd(self);
    if (FindPointer(Stations(self), end, &added) != end)
        return 0;
    Virtual<void>(added, 0x18, self ? face : nullptr);
    Virtual<void>(added, 0xC);
    This<void>(addr::SC4PtrVector_PushBack8FF785, self + player::kStations, &added);
    const uint32_t bytes = static_cast<uint32_t>(reinterpret_cast<char*>(StationsEnd(self)) -
                                                 reinterpret_cast<char*>(Stations(self)));
    if ((bytes & ~3u) == 4 && Virtual<uint8_t>(face, 0x60)) {
        Virtual<void*>(face, 0x30, uint32_t{0});
        Virtual<uint32_t>(face, 0x14);
    }
    return 1;
}

// The game's 0x008D33FF (slot 21, ret 8): a station taken out, released
// first when asked.
uint8_t __fastcall RemoveStation(char* face, void*, void* removed, uint8_t release)
{
    static bool logged = false;
    FirstCall("RemoveStation", logged);
    char* const self = Player(face);
    void** const end = StationsEnd(self);
    void** const at = FindPointer(Stations(self), end, &removed);
    if (at == end)
        return 0;
    if (release)
        Virtual<uint32_t>(removed, 0x8);
    This<void*>(addr::SC4PtrVector_EraseAt, self + player::kStations, at);
    return 1;
}

// The game's 0x008D3733 (slot 22, ret 4): every station taken out, released
// first when asked. Returns 0.
uint8_t __fastcall ClearStations(char* face, void*, uint8_t release)
{
    static bool logged = false;
    FirstCall("ClearStations", logged);
    char* const self = Player(face);
    if (release) {
        void** const end = StationsEnd(self);
        for (void** at = Stations(self); at != end; ++at) {
            if (*at)
                Virtual<uint32_t>(*at, 0x8);
        }
    }
    This<void*>(addr::SC4PtrVector_EraseRange, self + player::kStations, Stations(self), StationsEnd(self));
    return 0;
}

// The game's 0x008D2F21 (slot 23, ret 4): the player on or off. On loads the
// stations if there are none and, with a stream, starts the current one
// (its slots 7 and 5) and gives the stream the volume; off stops the
// player (slot 31) when there is a stream.
uint8_t __fastcall SetOn(char* face, void*, uint8_t on)
{
    static bool logged = false;
    FirstCall("SetOn", logged);
    char* const self = Player(face);
    if (on) {
        if (At<uint8_t>(self, player::kOn))
            return 1;
        At<uint8_t>(self, player::kOn) = 1;
        if (Stations(self) == StationsEnd(self))
            Virtual<uint8_t>(self, 0x40);
        if (Stations(self) == StationsEnd(self) || !At<void*>(self, player::kStream))
            return 1;
        Virtual<void>(CurrentStation(self), 0x1C);
        Virtual<uint32_t>(CurrentStation(self), 0x14);
        Virtual<void>(At<void*>(self, player::kStream), 0x54, At<uint32_t>(self, player::kVolume));
        return 1;
    }
    if (!At<uint8_t>(self, player::kOn))
        return 1;
    At<uint8_t>(self, player::kOn) = 0;
    if (At<void*>(self, player::kStream))
        Virtual<uint32_t>(face, 0x7C);
    return 1;
}

bool InstallMusic2(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[20] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::GZMusicPlayer_PostSelect, reinterpret_cast<void*>(&PostSelect)},
        {addr::GZMusicPlayer_PostUpdate, reinterpret_cast<void*>(&PostUpdate)},
        {addr::GZMusicPlayer_FindPointerLoop, reinterpret_cast<void*>(&FindPointerLoop)},
        {addr::GZMusicPlayer_SetOn, reinterpret_cast<void*>(&SetOn)},
        {addr::GZMusicPlayer_SetStation, reinterpret_cast<void*>(&SetStation)},
        {addr::GZMusicPlayer_Update, reinterpret_cast<void*>(&Update)},
        {addr::GZMusicPlayer_PlayFile, reinterpret_cast<void*>(&PlayFile)},
        {addr::GZMusicPlayer_DoMessage, reinterpret_cast<void*>(&DoMessage)},
        {addr::GZMusicPlayer_FindPointer, reinterpret_cast<void*>(&FindPointer)},
        {addr::GZMusicPlayer_RemoveStation, reinterpret_cast<void*>(&RemoveStation)},
        {addr::GZMusicPlayer_ClearStations, reinterpret_cast<void*>(&ClearStations)},
        {addr::GZMusicPlayer_AddStation, reinterpret_cast<void*>(&AddStation)},
        {addr::GZMusicPlayer_PostPlayFile, reinterpret_cast<void*>(&PostPlayFile)},
        {addr::GZMusicPlayer_LanguageFolder, reinterpret_cast<void*>(&LanguageFolder)},
        {addr::GZMusicPlayer_IsMusicFolder, reinterpret_cast<void*>(&IsMusicFolder)},
        {addr::GZMusicPlayer_PlayFileIfThere, reinterpret_cast<void*>(&PlayFileIfThere)},
        {addr::GZMusicPlayer_Init, reinterpret_cast<void*>(&Init)},
        {addr::GZMusicPlayer_FindStation, reinterpret_cast<void*>(&FindStation)},
        {addr::GZMusicPlayer_SelectStation, reinterpret_cast<void*>(&SelectStation)},
        {addr::GZMusicPlayer_LoadStations, reinterpret_cast<void*>(&LoadStations)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    static_assert(count == sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < count; ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].first);
        if (!found || !hook::Install(*found, hooks[i].second, &originals[i]))
            return false;
    }
    return true;
}

const bool g_music2Registered = registry::Register({
    "audio.music.part2",
    "audio",
    registry::Kind::Detour,
    registry::Phase::Attach,
    addr::GZMusicPlayer_Init,
    "Our music player service: Init, the stations, DoMessage and the playing methods (docs/audio.md)",
    false,
    InstallMusic2,
});

} // namespace
} // namespace opensc4::audio
