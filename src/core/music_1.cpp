// The first part of GZSound's music player (0x008D2994 to 0x008D7FFF;
// docs/audio.md, "Music player"): the player service's life, its COM
// methods, Shutdown and its simple methods.
//
// The player is a system service (ID 0x1D07B39F, class 0x5CFBDBBF), 0x90
// bytes, made by the audio director's factory 0x004EB150 (audio part 4),
// which hands out the part at +0x1C: the player interface 0xE0510260 that
// the audio code holds (the event handler's +0x10). Four vtables:
//   +0x00 0x00ACC3C0 the system service (20 slots: 0 QueryInterface, 1
//         AddRef, 2 Release, 8 Init, 9 Shutdown, 10 OnTick, 13 a setter of
//         +0x64, 15 a getter of +0x89, 16 0x008D49F8, 17 and 18 the file
//         filters, 19 the last Release's Shutdown and delete; 3 to 7, 11, 12
//         and 14 are the base's)
//   +0x04 0x00ACC3A4 the base's unknown part (thunks; slot 3 the deleting
//         destructor)
//   +0x1C 0x00ACC310 the player interface (37 slots, below)
//   +0x20 0x00ACC2FC a message target (DoMessage 0x008D319B, slot 3)
// The player does not play by itself: most of its methods post a message
// to the message server at +0x68, and DoMessage, which gets it back, works
// on the stream object at +0x6C (the 0x100-byte class of 0x008D376E).
//
// Replacement, off by default:
//
//   audio.music.part1: the constructor 0x008D35AB, the destructor 0x008D36EF
//   and the deleting destructor 0x008D38C9; QueryInterface, AddRef, Release
//   and their twelve this-adjustor thunks (including the Init thunk
//   0x008D51DB, which calls the game's Init); service slots 9, 10, 13, 15
//   and 19; the volume post 0x008D2C5F; the player interface's slots 8, 9,
//   10, 12, 16, 17, 18, 20, 25, 26, 27, 28, 29, 31, 32, 33, 35 and 36. 42
//   functions.
//
// The player is made while the application starts, before PostAppInit, so
// the group goes in when core.dll is injected (phase Attach), as the
// framework's service base does; the constructor then runs as ours.
//
// In part 2 (music_2.cpp): Init 0x008D4687, slot 16 0x008D49F8 (the
// stations), DoMessage 0x008D319B and the rest of the player interface
// (slots 5, 7, 11, 13, 14, 19, 21, 22, 23). Still the game's: the station
// class, the playlists and the container code; slots 24 (0x008D2B70,
// the byte +0x44) and 34 (0x008D3668, the byte +0x88), which are 4 bytes
// with the next function right after them, too short for the entry hook.
// Slots 6, 15 and 30 of the interface and the base's slots are framework
// functions shared with other classes.
//
// Not functions: 0x008D2C20 and 0x008D2C39, the bodies the adjustors of
// slots 31 (0x008D3122) and 8 (0x008D312A) jump to; they are written out in
// those two.
//
// The object (offsets from the object; the interface's code sees them less
// 0x1C):
//   +0x00 .. +0x1B the system service base (+0x0C the ID, +0x10 and +0x14
//   the priorities, 0x7A116)
//   +0x24 byte: started (Init done)          +0x28 the reference count
//   +0x2C a dword of the settings (the eighth)
//   +0x30 .. +0x38 the vector of stations (pointers)
//   +0x3C the volume (0..0x400)              +0x40 the mute count
//   +0x44 byte: on                           +0x48 the current station
//   +0x4C, +0x4D, +0x4E three option bytes (numbers 0, 1 and 3)
//   +0x50 the folder (SC4String)             +0x64 a pointer (slot 13)
//   +0x68 the message server                 +0x6C the stream object
//   +0x70, +0x71 bytes                       +0x74 a file name (SC4String)
//   +0x88, +0x89 bytes                       +0x8C the volume again (0x400)
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

namespace player {
constexpr std::size_t kUnknownPart = 0x04;
constexpr std::size_t kInterface = 0x1C;
constexpr std::size_t kTarget = 0x20;
constexpr std::size_t kStarted = 0x24;    // byte
constexpr std::size_t kRefs = 0x28;
constexpr std::size_t kSetting7 = 0x2C;
constexpr std::size_t kStations = 0x30;   // begin, end, capacity
constexpr std::size_t kStationsEnd = 0x34;
constexpr std::size_t kStationsCap = 0x38;
constexpr std::size_t kVolume = 0x3C;
constexpr std::size_t kMuted = 0x40;
constexpr std::size_t kOn = 0x44;         // byte
constexpr std::size_t kStation = 0x48;
constexpr std::size_t kOption0 = 0x4C;    // byte
constexpr std::size_t kOption1 = 0x4D;    // byte
constexpr std::size_t kOption3 = 0x4E;    // byte
constexpr std::size_t kFolder = 0x50;     // SC4String
constexpr std::size_t kHelper = 0x64;
constexpr std::size_t kServer = 0x68;
constexpr std::size_t kStream = 0x6C;
constexpr std::size_t kByte70 = 0x70;
constexpr std::size_t kByte71 = 0x71;
constexpr std::size_t kTrack = 0x74;      // SC4String
constexpr std::size_t kHasTrack = 0x88;   // byte
constexpr std::size_t kByte89 = 0x89;
constexpr std::size_t kVolumeKept = 0x8C;
} // namespace player

constexpr std::size_t kStringBegin = 0x4; // an SC4String's text: begin, end
constexpr std::size_t kStringEnd = 0x8;

constexpr uint32_t kServiceId = 0x1D07B39F;
constexpr int32_t kPriority = 0x7A116;
constexpr uint32_t kUnknownIid = 1;
constexpr uint32_t kSystemServiceIid = 0x287FB697;
constexpr uint32_t kPlayerIid = 0xE0510260;
constexpr uint32_t kFullVolume = 0x400;

// Messages the player posts to itself.
constexpr uint32_t kMsgVolume = 0x0AB1A61E;
constexpr uint32_t kMsgStop = 0x0AB186E5;
constexpr uint32_t kMsgPause = 0x6AB1A480;

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

void* StationAtIndex(char* self, uint32_t index)
{
    return At<void**>(self, player::kStations)[index];
}

bool HasStations(char* self)
{
    return At<void**>(self, player::kStations) != At<void**>(self, player::kStationsEnd);
}

// ---- Life ---------------------------------------------------------------

// The game's 0x008D35AB (thiscall, no arguments, returns this).
char* __fastcall Construct(char* self, void*)
{
    static bool logged = false;
    FirstCall("Construct", logged);
    This<void*>(addr::cRZBaseSystemService_cRZBaseSystemService, self, kServiceId, kPriority);
    At<std::uintptr_t>(self, player::kInterface) = addr::GZMusicPlayer_PlayerBaseVtable;
    At<std::uintptr_t>(self, player::kTarget) = addr::GZMusicPlayer_TargetBaseVtable;
    At<std::uintptr_t>(self, 0) = addr::GZMusicPlayer_ServiceVtable;
    At<std::uintptr_t>(self, player::kUnknownPart) = addr::GZMusicPlayer_UnknownVtable;
    At<std::uintptr_t>(self, player::kInterface) = addr::GZMusicPlayer_PlayerVtable;
    At<std::uintptr_t>(self, player::kTarget) = addr::GZMusicPlayer_TargetVtable;
    At<uint8_t>(self, player::kStarted) = 0;
    At<uint32_t>(self, player::kRefs) = 0;
    At<uint32_t>(self, player::kSetting7) = 0;
    At<uint32_t>(self, player::kStations) = 0;
    At<uint32_t>(self, player::kStationsEnd) = 0;
    At<uint32_t>(self, player::kStationsCap) = 0;
    At<uint32_t>(self, player::kVolume) = kFullVolume;
    At<uint32_t>(self, player::kMuted) = 0;
    At<uint8_t>(self, player::kOn) = 0;
    At<uint32_t>(self, player::kStation) = 0;
    At<uint8_t>(self, player::kOption0) = 1;
    At<uint8_t>(self, player::kOption1) = 1;
    At<uint8_t>(self, player::kOption3) = 1;
    This<void*>(addr::SC4String_SC4String, self + player::kFolder);
    At<uint32_t>(self, player::kHelper) = 0;
    At<uint32_t>(self, player::kServer) = 0;
    At<uint32_t>(self, player::kStream) = 0;
    At<uint8_t>(self, player::kByte70) = 0;
    At<uint8_t>(self, player::kByte71) = 0;
    This<void*>(addr::SC4String_SC4String, self + player::kTrack);
    At<uint8_t>(self, player::kHasTrack) = 0;
    At<uint8_t>(self, player::kByte89) = 0;
    At<uint32_t>(self, player::kVolumeKept) = kFullVolume;
    return self;
}

// The game's 0x008D36EF (thiscall). The stations themselves are not let go
// here: Shutdown does that.
void __fastcall Destruct(char* self, void*)
{
    static bool logged = false;
    FirstCall("Destruct", logged);
    At<std::uintptr_t>(self, 0) = addr::GZMusicPlayer_ServiceVtable;
    At<std::uintptr_t>(self, player::kUnknownPart) = addr::GZMusicPlayer_UnknownVtable;
    At<std::uintptr_t>(self, player::kInterface) = addr::GZMusicPlayer_PlayerVtable;
    At<std::uintptr_t>(self, player::kTarget) = addr::GZMusicPlayer_TargetVtable;
    This<void>(addr::SC4String_Destroy, self + player::kTrack);
    This<void>(addr::SC4String_Destroy, self + player::kFolder);
    if (void* storage = At<void*>(self, player::kStations))
        Cdecl<void>(addr::SC4_Free, storage);
    This<void>(addr::cRZBaseSystemService_Destroy, self);
}

// The game's 0x008D38C9 (thiscall, ret 4).
char* __fastcall DeletingDestruct(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("DeletingDestruct", logged);
    Destruct(self, nullptr);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// ---- COM ----------------------------------------------------------------

// The game's 0x008D2A6E (thiscall, ret 8).
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** out)
{
    static bool logged = false;
    FirstCall("QueryInterface", logged);
    if (iid == kSystemServiceIid)
        *out = self;
    else if (iid == kUnknownIid || iid == kPlayerIid)
        *out = self ? self + player::kInterface : nullptr;
    else
        return 0;
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// The game's 0x008D2AAF (thiscall).
uint32_t __fastcall AddRef(char* self, void*)
{
    static bool logged = false;
    FirstCall("AddRef", logged);
    return ++At<uint32_t>(self, player::kRefs);
}

// The game's 0x008D2AB6 (thiscall). The last reference is not counted down:
// slot 19 shuts the service down and deletes it.
uint32_t __fastcall Release(char* self, void*)
{
    static bool logged = false;
    FirstCall("Release", logged);
    uint32_t& refs = At<uint32_t>(self, player::kRefs);
    if (refs > 1)
        return --refs;
    Virtual<void>(self, 0x4C);
    return 0;
}

// The this-adjustor thunks: 0x008D367B, 0x008D3683, 0x008D368B and 0x008D3693
// (the +4 part), 0x008D369B, 0x008D36A3, 0x008D36AB, 0x008D51DB and
// 0x008D36B3 (the player interface), 0x008D36BB, 0x008D36C3 and 0x008D36CB
// (the message target).
uint8_t __fastcall QueryInterface4(char* self, void*, uint32_t iid, void** out)
{
    static bool logged = false;
    FirstCall("QueryInterface4", logged);
    return QueryInterface(self - 0x4, nullptr, iid, out);
}

uint32_t __fastcall AddRef4(char* self, void*)
{
    static bool logged = false;
    FirstCall("AddRef4", logged);
    return AddRef(self - 0x4, nullptr);
}

uint32_t __fastcall Release4(char* self, void*)
{
    static bool logged = false;
    FirstCall("Release4", logged);
    return Release(self - 0x4, nullptr);
}

char* __fastcall DeletingDestruct4(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("DeletingDestruct4", logged);
    return DeletingDestruct(self - 0x4, nullptr, flags);
}

uint8_t __fastcall QueryInterface1C(char* self, void*, uint32_t iid, void** out)
{
    static bool logged = false;
    FirstCall("QueryInterface1C", logged);
    return QueryInterface(self - 0x1C, nullptr, iid, out);
}

uint32_t __fastcall AddRef1C(char* self, void*)
{
    static bool logged = false;
    FirstCall("AddRef1C", logged);
    return AddRef(self - 0x1C, nullptr);
}

uint32_t __fastcall Release1C(char* self, void*)
{
    static bool logged = false;
    FirstCall("Release1C", logged);
    return Release(self - 0x1C, nullptr);
}

// Init is part 2's (called by address: ours when that group is on).
uint32_t __fastcall Init1C(char* self, void*)
{
    static bool logged = false;
    FirstCall("Init1C", logged);
    return This<uint32_t>(addr::GZMusicPlayer_Init, self - 0x1C);
}

uint8_t __fastcall Shutdown(char* self, void*);

uint8_t __fastcall Shutdown1C(char* self, void*)
{
    static bool logged = false;
    FirstCall("Shutdown1C", logged);
    return Shutdown(self - 0x1C, nullptr);
}

uint8_t __fastcall QueryInterface20(char* self, void*, uint32_t iid, void** out)
{
    static bool logged = false;
    FirstCall("QueryInterface20", logged);
    return QueryInterface(self - 0x20, nullptr, iid, out);
}

uint32_t __fastcall AddRef20(char* self, void*)
{
    static bool logged = false;
    FirstCall("AddRef20", logged);
    return AddRef(self - 0x20, nullptr);
}

uint32_t __fastcall Release20(char* self, void*)
{
    static bool logged = false;
    FirstCall("Release20", logged);
    return Release(self - 0x20, nullptr);
}

// ---- The service's slots -------------------------------------------------

// The game's 0x008D2A09 (thiscall; slot 9). The stream object is let go but
// its pointer is left in place.
uint8_t __fastcall Shutdown(char* self, void*)
{
    static bool logged = false;
    FirstCall("Shutdown", logged);
    if (At<uint8_t>(self, player::kStarted)) {
        Virtual<void>(self + player::kInterface, 0x58, uint32_t{1});
        if (void* stream = At<void*>(self, player::kStream)) {
            if (Virtual<uint8_t>(stream, 0x1C))
                Virtual<void>(At<void*>(self, player::kStream), 0x20);
            Virtual<uint32_t>(At<void*>(self, player::kStream), 0x8);
        }
        void* framework = Cdecl<void*>(addr::RZGetFramework);
        Virtual<void>(framework, 0x28, self);
        framework = Cdecl<void*>(addr::RZGetFramework);
        Virtual<void>(framework, 0x30, self);
        if (void* server = At<void*>(self, player::kServer))
            Virtual<void>(server, 0x40, self + player::kTarget);
        At<uint8_t>(self, player::kStarted) = 0;
    }
    return 1;
}

// The game's 0x008D365B (thiscall, ret 4; slot 10, OnTick): the interface's
// slot 5 does the work.
uint8_t __fastcall OnTick(char* self, void*, uint32_t)
{
    static bool logged = false;
    FirstCall("OnTick", logged);
    Virtual<void>(self + player::kInterface, 0x14);
    return 1;
}

// The game's 0x008D3651 (thiscall, ret 4; slot 13).
void __fastcall SetHelper(char* self, void*, void* helper)
{
    static bool logged = false;
    FirstCall("SetHelper", logged);
    At<void*>(self, player::kHelper) = helper;
}

// The game's 0x008D366C (thiscall; slot 15).
uint8_t __fastcall Byte89(char* self, void*)
{
    static bool logged = false;
    FirstCall("Byte89", logged);
    return At<uint8_t>(self, player::kByte89);
}

// The game's 0x008D2ACB (thiscall; slot 19, what the last Release calls):
// Shutdown, then the deleting destructor through the +4 part.
void __fastcall Destroy(char* self, void*)
{
    static bool logged = false;
    FirstCall("Destroy", logged);
    Virtual<uint8_t>(self, 0x24);
    Virtual<void*>(self + player::kUnknownPart, 0xC, uint32_t{1});
}

// The game's 0x008D2C5F (thiscall on the object, ret 4).
uint32_t __fastcall PostVolume(char* self, void*, uint32_t volume)
{
    static bool logged = false;
    FirstCall("PostVolume", logged);
    void* server = At<void*>(self, player::kServer);
    if (!server)
        return 0;
    return Virtual<uint32_t>(server, 0x1C, kMsgVolume, volume, uint32_t{0}, uint32_t{0}, uint32_t{0});
}

// ---- The player interface (this is the object + 0x1C) --------------------

// The game's 0x008D312A (slot 8, ret 4) and its body 0x008D2C39.
uint32_t __fastcall Pause(char* face, void*, uint8_t on)
{
    static bool logged = false;
    FirstCall("Pause", logged);
    char* self = Player(face);
    void* server = At<void*>(self, player::kServer);
    if (!server)
        return 0;
    return Virtual<uint32_t>(server, 0x1C, kMsgPause, uint32_t{on != 0}, uint32_t{0}, uint32_t{0}, uint32_t{0});
}

// The game's 0x008D3025 (slot 9, ret 4): the current station's slot 9.
uint8_t __fastcall StationCall24(char* face, void*, uint32_t value)
{
    static bool logged = false;
    FirstCall("StationCall24", logged);
    char* self = Player(face);
    if (!HasStations(self))
        return 0;
    Virtual<void>(StationAtIndex(self, At<uint32_t>(self, player::kStation)), 0x24, value);
    return 1;
}

// The game's 0x008D300B (slot 10, ret 4).
uint8_t __fastcall GetStationIndex(char* face, void*, uint32_t* out)
{
    static bool logged = false;
    FirstCall("GetStationIndex", logged);
    char* self = Player(face);
    if (!HasStations(self))
        return 0;
    *out = At<uint32_t>(self, player::kStation);
    return 1;
}

// The game's 0x008D3048 (slot 12, ret 4).
void* __fastcall StationAt(char* face, void*, uint32_t index)
{
    static bool logged = false;
    FirstCall("StationAt", logged);
    char* self = Player(face);
    const uint32_t count = static_cast<uint32_t>(At<void**>(self, player::kStationsEnd) - At<void**>(self, player::kStations));
    return index < count ? StationAtIndex(self, index) : nullptr;
}

// The game's 0x008D2FDC (slot 16, ret 4).
uint8_t __fastcall SetVolume(char* face, void*, uint32_t volume)
{
    static bool logged = false;
    FirstCall("SetVolume", logged);
    char* self = Player(face);
    if (volume > kFullVolume)
        volume = kFullVolume;
    At<uint32_t>(self, player::kVolume) = volume;
    At<uint32_t>(self, player::kVolumeKept) = volume;
    if (At<void*>(self, player::kStream) && At<uint32_t>(self, player::kMuted) == 0)
        PostVolume(self, nullptr, volume);
    return 1;
}

// The game's 0x008D30C7 (slot 17).
uint32_t __fastcall StationCount(char* face, void*)
{
    static bool logged = false;
    FirstCall("StationCount", logged);
    char* self = Player(face);
    return static_cast<uint32_t>(At<void**>(self, player::kStationsEnd) - At<void**>(self, player::kStations));
}

// The game's 0x008D2BED (slot 18, ret 4): whether slot 11 finds a station.
uint32_t __fastcall HasStation(char* face, void*, void* key)
{
    static bool logged = false;
    FirstCall("HasStation", logged);
    return Virtual<void*>(face, 0x2C, key) != nullptr;
}

// The game's 0x008D2BC9 (slot 20, ret 8): slot 11 finds the station, slot
// 21 takes it out.
uint32_t __fastcall RemoveStationBy(char* face, void*, void* key, uint32_t release)
{
    static bool logged = false;
    FirstCall("RemoveStationBy", logged);
    void* station = Virtual<void*>(face, 0x2C, key);
    if (!station)
        return 0;
    return Virtual<uint32_t>(face, 0x54, station, release);
}

// The game's 0x008D2F97 (slot 25, ret 4). Muting counts: the first mute
// posts volume 0 and the last unmute posts the kept volume, each only with
// a stream and a volume above 0.
void __fastcall Mute(char* face, void*, uint8_t mute)
{
    static bool logged = false;
    FirstCall("Mute", logged);
    char* self = Player(face);
    uint32_t& muted = At<uint32_t>(self, player::kMuted);
    if (mute) {
        ++muted;
        if (muted != 1 || At<uint32_t>(self, player::kVolume) == 0 || !At<void*>(self, player::kStream))
            return;
        PostVolume(self, nullptr, 0);
        return;
    }
    if (muted == 0)
        return;
    if (--muted != 0)
        return;
    const uint32_t volume = At<uint32_t>(self, player::kVolumeKept);
    if (volume == 0 || !At<void*>(self, player::kStream))
        return;
    PostVolume(self, nullptr, volume);
}

// The game's 0x008D2BA3 (slot 26, ret 4). EAX keeps the number's upper bytes
// as the original's does.
uint32_t __fastcall GetOption(char* face, void*, uint32_t which)
{
    static bool logged = false;
    FirstCall("GetOption", logged);
    char* self = Player(face);
    if (which == 0)
        return At<uint8_t>(self, player::kOption0);
    if (which == 1)
        return At<uint8_t>(self, player::kOption1);
    if (which == 3)
        return At<uint8_t>(self, player::kOption3);
    return which & 0xFFFFFF00u;
}

// The game's 0x008D2B74 (slot 27, ret 8).
void __fastcall SetOption(char* face, void*, uint8_t value, uint32_t which)
{
    static bool logged = false;
    FirstCall("SetOption", logged);
    char* self = Player(face);
    if (which == 0)
        At<uint8_t>(self, player::kOption0) = value;
    else if (which == 1)
        At<uint8_t>(self, player::kOption1) = value;
    else if (which == 3)
        At<uint8_t>(self, player::kOption3) = value;
}

// The game's 0x008D36D3 (slot 28, ret 4): the folder into a cIGZString
// (its slot 3, FromChar with a length).
uint8_t __fastcall GetFolder(char* face, void*, void* out)
{
    static bool logged = false;
    FirstCall("GetFolder", logged);
    char* folder = Player(face) + player::kFolder;
    const char* begin = At<const char*>(folder, kStringBegin);
    const uint32_t length = static_cast<uint32_t>(At<const char*>(folder, kStringEnd) - begin);
    Virtual<void>(out, 0xC, begin, length);
    return 1;
}

// The game's 0x008D3673 (slot 29): the folder's slot 13, the assignment
// from a cIGZString, by a jump.
void* __fastcall SetFolder(char* face, void*, void* from)
{
    static bool logged = false;
    FirstCall("SetFolder", logged);
    return Virtual<void*>(Player(face) + player::kFolder, 0x34, from);
}

// The game's 0x008D3122 (slot 31) and its body 0x008D2C20.
uint32_t __fastcall Stop(char* face, void*)
{
    static bool logged = false;
    FirstCall("Stop", logged);
    char* self = Player(face);
    void* server = At<void*>(self, player::kServer);
    if (!server)
        return 0;
    return Virtual<uint32_t>(server, 0x1C, kMsgStop, uint32_t{0}, uint32_t{0}, uint32_t{0}, uint32_t{0});
}

// The game's 0x008D5092 (slot 32, ret 4).
uint8_t __fastcall GetTrackName(char* face, void*, void* out)
{
    static bool logged = false;
    FirstCall("GetTrackName", logged);
    char* self = Player(face);
    if (At<uint8_t>(self, player::kHasTrack))
        This<void*>(addr::cRZBaseString_Assign, out, self + player::kTrack);
    return 1;
}

// The game's 0x008D50AA (slot 33, ret 4), a jump to the string's Assign.
void* __fastcall SetTrackName(char* face, void*, void* from)
{
    static bool logged = false;
    FirstCall("SetTrackName", logged);
    return This<void*>(addr::cRZBaseString_Assign, Player(face) + player::kTrack, from);
}

// The game's 0x008D2ADF (slot 35, ret 4). The settings are eight dwords:
// volume, on, station, the options 0, 1, (unused), 3, and the value at
// +0x2C. The options and that value are stored, then slots 16 (the volume),
// 14 (the station) and 23 (on) are called in that order.
void __fastcall SetSettings(char* face, void*, const uint32_t* settings)
{
    static bool logged = false;
    FirstCall("SetSettings", logged);
    char* self = Player(face);
    const uint32_t volume = settings[0];
    const uint8_t on = settings[1] != 0;
    const uint32_t station = settings[2];
    At<uint8_t>(self, player::kOption0) = settings[3] != 0;
    At<uint8_t>(self, player::kOption1) = settings[4] != 0;
    At<uint8_t>(self, player::kOption3) = settings[6] != 0;
    At<uint32_t>(self, player::kSetting7) = settings[7];
    Virtual<void>(face, 0x40, volume);
    Virtual<void>(face, 0x38, station);
    Virtual<void>(face, 0x5C, on);
}

// The game's 0x008D2B3C (slot 36, ret 4). The sixth dword is left alone.
void __fastcall GetSettings(char* face, void*, uint32_t* settings)
{
    static bool logged = false;
    FirstCall("GetSettings", logged);
    char* self = Player(face);
    settings[0] = At<uint32_t>(self, player::kVolume);
    settings[1] = At<uint8_t>(self, player::kOn);
    settings[2] = At<uint32_t>(self, player::kStation);
    settings[3] = At<uint8_t>(self, player::kOption0);
    settings[4] = At<uint8_t>(self, player::kOption1);
    settings[6] = At<uint8_t>(self, player::kOption3);
    settings[7] = At<uint32_t>(self, player::kSetting7);
}

bool InstallMusic1(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[42] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::GZMusicPlayer_Shutdown, reinterpret_cast<void*>(&Shutdown)},
        {addr::GZMusicPlayer_QueryInterface, reinterpret_cast<void*>(&QueryInterface)},
        {addr::GZMusicPlayer_AddRef, reinterpret_cast<void*>(&AddRef)},
        {addr::GZMusicPlayer_Release, reinterpret_cast<void*>(&Release)},
        {addr::GZMusicPlayer_Destroy, reinterpret_cast<void*>(&Destroy)},
        {addr::GZMusicPlayer_SetSettings, reinterpret_cast<void*>(&SetSettings)},
        {addr::GZMusicPlayer_GetSettings, reinterpret_cast<void*>(&GetSettings)},
        {addr::GZMusicPlayer_SetOption, reinterpret_cast<void*>(&SetOption)},
        {addr::GZMusicPlayer_GetOption, reinterpret_cast<void*>(&GetOption)},
        {addr::GZMusicPlayer_RemoveStationBy, reinterpret_cast<void*>(&RemoveStationBy)},
        {addr::GZMusicPlayer_HasStation, reinterpret_cast<void*>(&HasStation)},
        {addr::GZMusicPlayer_PostVolume, reinterpret_cast<void*>(&PostVolume)},
        {addr::GZMusicPlayer_Mute, reinterpret_cast<void*>(&Mute)},
        {addr::GZMusicPlayer_SetVolume, reinterpret_cast<void*>(&SetVolume)},
        {addr::GZMusicPlayer_GetStationIndex, reinterpret_cast<void*>(&GetStationIndex)},
        {addr::GZMusicPlayer_StationCall24, reinterpret_cast<void*>(&StationCall24)},
        {addr::GZMusicPlayer_StationAt, reinterpret_cast<void*>(&StationAt)},
        {addr::GZMusicPlayer_StationCount, reinterpret_cast<void*>(&StationCount)},
        {addr::GZMusicPlayer_Stop, reinterpret_cast<void*>(&Stop)},
        {addr::GZMusicPlayer_Pause, reinterpret_cast<void*>(&Pause)},
        {addr::GZSound_ConstructService, reinterpret_cast<void*>(&Construct)},
        {addr::GZMusicPlayer_SetHelper, reinterpret_cast<void*>(&SetHelper)},
        {addr::GZMusicPlayer_OnTick, reinterpret_cast<void*>(&OnTick)},
        {addr::GZMusicPlayer_Flag89, reinterpret_cast<void*>(&Byte89)},
        {addr::GZMusicPlayer_SetFolder, reinterpret_cast<void*>(&SetFolder)},
        {addr::GZMusicPlayer_QueryInterface4, reinterpret_cast<void*>(&QueryInterface4)},
        {addr::GZMusicPlayer_AddRef4, reinterpret_cast<void*>(&AddRef4)},
        {addr::GZMusicPlayer_Release4, reinterpret_cast<void*>(&Release4)},
        {addr::GZMusicPlayer_DeletingDestruct4, reinterpret_cast<void*>(&DeletingDestruct4)},
        {addr::GZMusicPlayer_QueryInterface1C, reinterpret_cast<void*>(&QueryInterface1C)},
        {addr::GZMusicPlayer_AddRef1C, reinterpret_cast<void*>(&AddRef1C)},
        {addr::GZMusicPlayer_Release1C, reinterpret_cast<void*>(&Release1C)},
        {addr::GZMusicPlayer_Shutdown1C, reinterpret_cast<void*>(&Shutdown1C)},
        {addr::GZMusicPlayer_QueryInterface20, reinterpret_cast<void*>(&QueryInterface20)},
        {addr::GZMusicPlayer_AddRef20, reinterpret_cast<void*>(&AddRef20)},
        {addr::GZMusicPlayer_Release20, reinterpret_cast<void*>(&Release20)},
        {addr::GZMusicPlayer_GetFolder, reinterpret_cast<void*>(&GetFolder)},
        {addr::GZMusicPlayer_Destruct, reinterpret_cast<void*>(&Destruct)},
        {addr::GZMusicPlayer_DeletingDestruct, reinterpret_cast<void*>(&DeletingDestruct)},
        {addr::GZMusicPlayer_GetTrackName, reinterpret_cast<void*>(&GetTrackName)},
        {addr::GZMusicPlayer_SetTrackName, reinterpret_cast<void*>(&SetTrackName)},
        {addr::GZMusicPlayer_Init1C, reinterpret_cast<void*>(&Init1C)},
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

const bool g_music1Registered = registry::Register({
    "audio.music.part1",
    "audio",
    registry::Kind::Detour,
    registry::Phase::Attach,
    addr::GZSound_ConstructService,
    "Our music player service: its life, COM methods, Shutdown and simple methods (docs/audio.md)",
    false,
    InstallMusic1,
});

} // namespace
} // namespace opensc4::audio
