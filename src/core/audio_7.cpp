// The seventh part of the game's audio code (0x004F1A80 to 0x004F511D):
//
//   - the last three functions of the class before it (object of 0x7C bytes,
//     one vtable 0x00A9421C of four slots: a message target; by what it does,
//     the listener, cSC4AudioListener: it keeps the place the sounds are
//     heard from and hands it to the sound service). Here: its tuning reader
//     0x004F1A80, its city message handler 0x004F1D60 and its DoMessage
//     0x004F1E50. Its constructor, destructor, Init, Shutdown, the camera
//     update 0x004F14B0 and the volume table 0x004F18C0 are in part 6.
//   - the music lists: two static records of 0x30 bytes at 0x00B21A88, list 0
//     for the region view's music and list 1 for the city's ("Radio\Stations\
//     Region\Music\" and "...\Mayor\Music\" under the data folder), each with
//     up to 30 tracks read from an exemplar. Free functions: 0x004F1F70 (a
//     list by index), 0x004F2910 (reads a list's tracks), 0x004F2B70 (sets
//     the two records up), 0x004F2D40 (does both once), 0x004F2580 (writes
//     the tracks' switches to the settings), 0x004F3410 (posts a list's file
//     names to the music player: message 0x0A611B34), 0x004F1F90 (a slider's
//     position as a volume), 0x004F22C0 (reads the volume limits), 0x004F20E0
//     (posts a message with two numbers).
//   - the audio options dialog (0x80 bytes, vtable 0x00A94264 of seven slots,
//     a window procedure with Init and Shutdown; made by 0x004F2450 and run
//     by 0x004F25F0 from the region view at 0x007B0218 and the city view at
//     0x007F2CDE): two volume sliders, switches for sound and music, a list
//     box with one row and one check box per track, play and stop buttons,
//     OK and cancel.
//
// Replacement, off by default:
//
//   audio.part7: everything named above.
//
// Not functions: 0x004F1E00 (the second half of 0x004F1D60), 0x004F36B0 (a
// case of the jump table in 0x004F35F0), 0x004F3A30 (a label in 0x004F35F0)
// and 0x004F43A0 (a label in 0x004F4280). 0x004F44C0 runs past the part's end
// to 0x004F511D and is here whole.
//
// Still the game's: 0x004F1F30 (QueryInterface: code shared with nine other
// window procedures), 0x004F2020, 0x004F20A0 and 0x004F20C0 (the reference
// holders' helpers, called from everywhere), and 0x004F2720 (the constructor
// of one music list record: only the static initialiser at 0x00A72520 calls
// it, before any hook is in place). Four functions take an argument in a
// register and are called only from functions that are ours here; they are
// written out for those callers and not hooked: 0x004F2140 and 0x004F21B0
// (a child window's flag 2 read and set; the window in ECX, the rest on the
// stack, cleaned by the caller), 0x004F27A0 (the files of a folder; the list
// in EDI) and 0x004F31C0 (whether a folder has files of its own; the index
// in ECX).
//
// The listener, as far as this file touches it: +0x3C, +0x48, +0x54, +0x60
// four vectors of five floats (one per zoom), +0x6C and +0x70 two floats,
// +0x74 the sound service (0x00B43CB8, held while a city is loaded), +0x78
// the message server.
//
// A music list: +0 index, +4 an ID, +8 the vector of tracks, +0x14 the
// instance of its exemplar (group 0xCA25875D), +0x18 a list of file names
// (what 0x004F3410 posts), +0x1C the folder (a string).
// A track (16 bytes): +0 the ID of its check box, +4 the text instance of its
// file name, +8 the text instance of its title (both in group 0x8A635D24),
// +0xC its switch as the exemplar gives it.
//
// The dialog: +0 vtable, +4 the window (cIGZWinGen), +8 the list box, +0xC
// byte: the music player was told that a preview is on (message 0xAA650861),
// +0x10 the settings (0x00B43CD8, set by Init), +0x14 the audio settings as
// they were when the dialog opened (0x68 bytes), +0x7C the reference count.
//
// The audio settings (the settings' +4, SC4AudioPreferences in gzcom-dll):
// +0 byte and +4: the switch and volume sent to the sound service (command
// 0x2A5B69C0); +8 and +0xC: the volume and switch posted as message
// 0xAA650860; +0x28 the city list's 30 switches, +0x46 the region list's;
// +0x64 byte: the switch of button 0x8A550C5A (when on, a list is posted
// with the tracks that are switched on left out instead of kept).
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace opensc4::audio {
namespace {

namespace listener {
constexpr std::size_t kHeights = 0x3C;   // vector of five floats (property 0xCA359F17)
constexpr std::size_t kVolumes = 0x48;   // vector of five floats (property 0x6A359F22)
constexpr std::size_t kFades = 0x54;     // vector of five floats (property 0xEA4A7C55)
constexpr std::size_t kDistances = 0x60; // vector of five floats (property 0x2AA40CCC), squared times 256
constexpr std::size_t kScale = 0x6C;     // float (property 0x2A35A25F)
constexpr std::size_t kRolloff = 0x70;   // float (property 0x2A36E3CB), handed to the sound service's device
constexpr std::size_t kSound = 0x74;
constexpr std::size_t kServer = 0x78;
} // namespace listener

namespace dialog {
constexpr std::size_t kWindow = 0x04;
constexpr std::size_t kList = 0x08;
constexpr std::size_t kPreview = 0x0C; // byte
constexpr std::size_t kOptions = 0x10;
constexpr std::size_t kSaved = 0x14;   // the audio settings, 0x68 bytes
constexpr std::size_t kRefs = 0x7C;
} // namespace dialog

namespace prefs { // the audio settings
constexpr std::size_t kSoundOn = 0x00;      // byte
constexpr std::size_t kSoundVolume = 0x04;
constexpr std::size_t kMusicVolume = 0x08;
constexpr std::size_t kMusicOn = 0x0C;
constexpr std::size_t kCityTracks = 0x28;   // 30 bytes
constexpr std::size_t kRegionTracks = 0x46; // 30 bytes
constexpr std::size_t kLeaveOut = 0x64;     // byte
} // namespace prefs

constexpr std::size_t kAudio = 0x4; // the audio settings in the settings

constexpr uint32_t kWinIid = 0x22BA0121;
constexpr uint32_t kButtonIid = 0x8810;
constexpr uint32_t kSliderIid = 0x21325207;
constexpr uint32_t kListBoxIid = 0xDAA6B9BE;
constexpr uint32_t kMessageIid = 0x65297976;
constexpr uint32_t kExemplarIid = 0xA52160F5;
constexpr uint32_t kExemplarType = 0x6534284A;
constexpr uint32_t kAudioGroup = 0xCA25875D;
constexpr uint32_t kMusicTexts = 0x8A635D24;
constexpr uint32_t kClickSound = 0x8A5C324A;
constexpr uint32_t kEndModal = 0xF;

// The dialog's controls.
constexpr uint32_t kMusicSwitch = 0x8A550C50;
constexpr uint32_t kMusicSlider = 0x8A550C51;
constexpr uint32_t kSoundSwitch = 0x8A550C52;
constexpr uint32_t kSoundSlider = 0x8A550C53;
constexpr uint32_t kTrackList = 0x8A550C56;
constexpr uint32_t kPlayButton = 0x8A550C57;
constexpr uint32_t kStopButton = 0x8A550C58;
constexpr uint32_t kLeaveOutSwitch = 0x8A550C5A;
constexpr uint32_t kCancelButton = 0xEA5509E3;
constexpr uint32_t kOkButton = 0xEA5509E4;

// Messages.
constexpr uint32_t kCityInited = 0x26D31EC1;
constexpr uint32_t kCityShutdown = 0x26D31EC2;
constexpr uint32_t kViewMoved = 0xA6B79602;
constexpr uint32_t kViewZoomed = 0xA6B79612;
constexpr uint32_t kMusicSettings = 0xAA650860;
constexpr uint32_t kPreviewSwitch = 0xAA650861;
constexpr uint32_t kPlayList = 0x0A611B34;
constexpr uint32_t kPlayFile = 0xAA611B6E;
constexpr uint32_t kStopPreview = 0x2A611B51;
constexpr uint32_t kOptionsClosed = 0x231BBF91;

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

// A string from the text resources: 0x20 bytes, a string first. Sized from
// the frames: 0x004F2A70 keeps it at the last 0x20 bytes of its 0x40.
struct LocalizedString {
    GameString text;
    uint32_t rest[3];
};
static_assert(sizeof(LocalizedString) == 0x20);

// The game's image holder (0x30 bytes, the last 0x30 of 0x004F44C0's frame).
struct ImageLoader {
    std::uintptr_t vtable;
    void* image;
    char rest[0x28];
};
static_assert(sizeof(ImageLoader) == 0x30);

// The folder listing object (0x34 bytes, the last 0x34 of 0x004F27A0's frame).
struct Directory {
    alignas(4) char bytes[0x34];
};

struct FloatVector {
    float* begin;
    float* end;
    float* capacity;
};

struct StringNode {
    StringNode* next;
    StringNode* previous;
    GameString text;
};
static_assert(sizeof(StringNode) == 0x1C);

struct StringList {
    StringNode* head;
};

struct Track {
    uint32_t id;
    uint32_t file;
    uint32_t title;
    uint8_t on;
    uint8_t pad[3];
};
static_assert(sizeof(Track) == 0x10);

struct Playlist {
    uint32_t index;
    uint32_t id;
    Track* begin;
    Track* end;
    Track* capacity;
    uint32_t exemplar;
    StringList files;
    GameString folder;
};
static_assert(sizeof(Playlist) == 0x30);

// What the list box writes for one selected cell: 8 bytes.
struct Cell {
    int32_t column;
    int32_t row;
};

// A window message as a window procedure gets it.
struct WinMessage {
    uint32_t type;
    uint32_t a;
    uint32_t b;
    uint32_t c;
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

void AddRef(void* object)
{
    Virtual<uint32_t>(object, 0x4);
}

void Release(void* object)
{
    Virtual<uint32_t>(object, 0x8);
}

void Free(void* memory)
{
    Cdecl<void>(addr::SC4_Free, memory);
}

// The runtime's ftol (0x009EEF04), the low half of its result.
uint32_t Ftol(double value)
{
    return static_cast<uint32_t>(static_cast<int64_t>(value));
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
    text.zero = 0;
}

void DestroyString(GameString& text)
{
    char* const buffer = text.body.begin;
    text.vtable = addr::SC4String_Vtable;
    if (buffer)
        Free(buffer);
}

// out = a + b (the game's 0x00408AE0); returns out.
StringBody* Join(StringBody* out, StringBody* a, StringBody* b)
{
    return Cdecl<StringBody*>(addr::cSC4Advice_F408AE0, out, a, b);
}

// A new body with a copy of a range (the game's 0x00408420).
void BodyFromRange(StringBody* body, char* begin, char* end)
{
    uint8_t tag = 0;
    body->begin = nullptr;
    body->end = nullptr;
    body->capacity = nullptr;
    This<void>(addr::cSC4Advice_F408420, body, begin, end, &tag);
}

void AssignRange(StringBody* body, char* begin, char* end)
{
    uint8_t tag = 0;
    This<void*>(addr::SC4String_AssignRange, body, begin, end, &tag);
}

// The text of a music text instance: the localized string's constructor and
// the body of what it gives back.
StringBody* MusicText(LocalizedString& text, uint32_t instance)
{
    GameString* const got =
        This<GameString*>(addr::SC4LocalizedString_SC4LocalizedString, &text, kMusicTexts, instance, uint32_t{1});
    return got ? &got->body : nullptr;
}

StringNode* NewListHead()
{
    StringNode* const head = Cdecl<StringNode*>(addr::SC4_stl_allocate, uint32_t{0x1C});
    head->next = head;
    head->previous = head;
    return head;
}

// A list of strings emptied as the game does inline (its own copy is
// 0x0043ABA0).
void ClearList(StringNode* head)
{
    StringNode* node = head->next;
    while (node != head) {
        StringNode* const gone = node;
        node = node->next;
        Virtual<void>(&gone->text, 0x70, uint32_t{0});
        Free(gone);
    }
    head->next = head;
    head->previous = head;
}

Playlist* Playlists()
{
    return reinterpret_cast<Playlist*>(addr::SC4AudioMusic_Playlists);
}

StringBody* FolderBody(std::uintptr_t text)
{
    return &reinterpret_cast<GameString*>(text)->body;
}

// The ID of a list's first check box.
uint32_t BoxBase(uint32_t index)
{
    return Global<uint32_t>(addr::SC4AudioMusic_BoxBases + index * 4);
}

void* NewMessage()
{
    void* const memory = Cdecl<void*>(addr::RZ_operator_new, uint32_t{0x2C});
    return memory ? This<void*>(addr::cGZMessage_cGZMessage, memory) : nullptr;
}

// --- The listener ---------------------------------------------------------------------

void SetFive(FloatVector& values, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    const uint32_t bits[5] = {a, b, c, d, e};
    std::memcpy(values.begin, bits, sizeof(bits));
}

// A vector made five long: cut, or filled with zeros.
void MakeFive(FloatVector& values)
{
    float zero = 0.0f;
    const uint32_t count = static_cast<uint32_t>(values.end - values.begin);
    if (count > 5)
        values.end = values.begin + 5;
    else
        This<void>(addr::SC4FloatVector_InsertN, &values, values.end, 5u - count, &zero);
}

// The game's 0x004F1A80. No arguments; `this` is the listener. Reads the
// listener's tuning from exemplar 0x6A359C5C; without a property its five
// defaults.
void __fastcall ListenerReadTuning(char* self, void*)
{
    void* const resources = Global<void*>(addr::SC4_ResourceManager);
    if (resources)
        AddRef(resources);
    void* exemplar = nullptr;
    uint32_t key[3] = {kExemplarType, kAudioGroup, 0x6A359C5C};
    if (Virtual<uint8_t>(resources, 0xC, static_cast<uint32_t*>(key), kExemplarIid, &exemplar, uint32_t{0},
                         uint32_t{0})) {
        void* const holder = Virtual<void*>(exemplar, 0x1C);
        Cdecl<uint8_t>(addr::SC4_GetPropertyFloat, holder, uint32_t{0x2A36E3CB}, &At<float>(self, listener::kRolloff));
        if (void* const sound = At<void*>(self, listener::kSound)) {
            void* const device = Virtual<void*>(sound, 0x10);
            if (device)
                AddRef(device);
            Virtual<void>(device, 0x28, At<uint32_t>(self, listener::kRolloff)); // the float's four bytes
            Release(device);
        }
        FloatVector& heights = At<FloatVector>(self, listener::kHeights);
        if (!Cdecl<uint8_t>(addr::SC4_GetPropertyFloatVector, holder, uint32_t{0xCA359F17}, &heights))
            SetFive(heights, 0x44A70000, 0x4426C000, 0x43A68000, 0x43110000, 0x42920000); // 1336 667 333 145 73
        FloatVector& volumes = At<FloatVector>(self, listener::kVolumes);
        if (!Cdecl<uint8_t>(addr::SC4_GetPropertyFloatVector, holder, uint32_t{0x6A359F22}, &volumes))
            SetFive(volumes, 0x3F800000, 0x3F800000, 0x3F800000, 0x3F800000, 0x3F800000);
        Cdecl<uint8_t>(addr::SC4_GetPropertyFloat, holder, uint32_t{0x2A35A25F}, &At<float>(self, listener::kScale));
        FloatVector& fades = At<FloatVector>(self, listener::kFades);
        if (!Cdecl<uint8_t>(addr::SC4_GetPropertyFloatVector, holder, uint32_t{0xEA4A7C55}, &fades)) {
            MakeFive(fades);
            SetFive(fades, 0x3F800000, 0x3F400000, 0x3F000000, 0x3E800000, 0x00000000); // 1 0.75 0.5 0.25 0
        }
        FloatVector& distances = At<FloatVector>(self, listener::kDistances);
        if (!Cdecl<uint8_t>(addr::SC4_GetPropertyFloatVector, holder, uint32_t{0x2AA40CCC}, &distances)) {
            MakeFive(distances);
            SetFive(distances, 0x41D00000, 0x41500000, 0x40D00000, 0x40500000, 0x3FD00000); // 26 13 6.5 3.25 1.625
        }
        for (int i = 0; i < 5; ++i) {
            float* const values = distances.begin;
            const double value = static_cast<double>(values[i]);
            values[i] = static_cast<float>(value * value * 256.0);
        }
    }
    if (exemplar)
        Release(exemplar);
    Release(resources);
}

// The game's 0x004F1D60 (with its second half at 0x004F1E00). (message),
// ret 4. The city's start and end: the sound service taken and given back,
// the view's two messages asked for and no longer.
uint8_t __fastcall ListenerCityMessage(char* self, void*, void* message)
{
    if (!message)
        return 0;
    void* standard = nullptr;
    Virtual<uint8_t>(message, 0x0, kMessageIid, &standard);
    if (!standard)
        return 0;
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    if (type == kCityInited) {
        This<void*>(addr::SC4RefPtr_Assign, self + listener::kSound, Global<void*>(addr::SC4_Sound43CB8));
        if (!At<void*>(self, listener::kSound)) {
            if (standard)
                Release(standard);
            return 0;
        }
        Virtual<void>(At<void*>(self, listener::kServer), 0x14, self, kViewMoved);
        Virtual<void>(At<void*>(self, listener::kServer), 0x14, self, kViewZoomed);
        ListenerReadTuning(self, nullptr);
        This<void>(addr::SC4Audio_F4F18C0, self);
        // The original does not load ECX again here: 0x004F18C0 leaves it.
        This<uint8_t>(addr::SC4Audio_F4F14B0, self);
    } else if (type == kCityShutdown) {
        Virtual<void>(At<void*>(self, listener::kServer), 0x18, self, kViewMoved);
        Virtual<void>(At<void*>(self, listener::kServer), 0x18, self, kViewZoomed);
        if (void* const sound = At<void*>(self, listener::kSound)) {
            At<void*>(self, listener::kSound) = nullptr;
            Release(sound);
        }
    }
    if (standard)
        Release(standard);
    return 1;
}

// The game's 0x004F1E50 (slot 3 of 0x00A9421C). (message), ret 4.
uint8_t __fastcall ListenerMessage(char* self, void*, void* message)
{
    if (!message)
        return 0;
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    switch (type) {
    case kViewMoved:
    case kViewZoomed:
        break;
    case 0x230E27AC:
        This<uint8_t>(addr::SC4Audio_F4F1470, self, message);
        return 1;
    case kCityInited:
    case kCityShutdown:
        ListenerCityMessage(self, nullptr, message);
        return 1;
    case 0xC53D10AA: {
        ListenerReadTuning(self, nullptr);
        This<void>(addr::SC4Audio_F4F18C0, self);
        void* const sound = At<void*>(self, listener::kSound);
        if (!sound)
            break;
        void* got = nullptr;
        void** const holder = This<void**>(addr::SC4RefPtr_Reset, &got);
        Virtual<void>(sound, 0x34, uint32_t{0xAA36D9B6}, uint32_t{0x5B523BA4}, holder, uint32_t{0}, uint32_t{0},
                      uint32_t{0}, uint32_t{0});
        Virtual<void>(At<void*>(self, listener::kSound), 0x30, got);
        if (got)
            Release(got);
        break;
    }
    default:
        return 1;
    }
    This<uint8_t>(addr::SC4Audio_F4F14B0, self);
    return 1;
}

// --- The music lists ------------------------------------------------------------------

// The game's 0x004F1F70. cdecl (index): the list, or null.
Playlist* __cdecl PlaylistAt(int32_t index)
{
    if (index < 0 || index >= 2)
        return nullptr;
    return Playlists() + index;
}

// The game's 0x004F1F90. cdecl (position, which): a slider's position (0 to
// 1000) as a volume between the limits; which 0 is the sound's, else the
// music's.
uint32_t __cdecl VolumeFromSlider(uint32_t position, uint32_t which)
{
    uint32_t low;
    uint32_t high;
    if (which == 0) {
        low = Global<uint32_t>(addr::SC4AudioMusic_SoundVolumeMin);
        high = Global<uint32_t>(addr::SC4AudioMusic_SoundVolumeMax);
    } else {
        low = Global<uint32_t>(addr::SC4AudioMusic_MusicVolumeMin);
        high = Global<uint32_t>(addr::SC4AudioMusic_MusicVolumeMax);
    }
    if (position == 1000)
        return high;
    if (position == 0)
        return low;
    const double range = static_cast<double>(high - low);
    const double scaled = range * static_cast<double>(position) * static_cast<double>(-0.001f);
    const uint32_t volume = low - Ftol(scaled);
    if (volume > high)
        return high;
    if (volume < low)
        return low;
    return volume;
}

// The game's 0x004F20E0. cdecl (type, a, b): a message with two numbers,
// posted. Ends in a jump to the message's Release.
uint32_t __cdecl PostNumbers(uint32_t type, uint32_t a, uint32_t b)
{
    void* const message = NewMessage();
    AddRef(message);
    Virtual<void>(message, 0x14, type);
    Virtual<void>(message, 0x2C, a);
    Virtual<void>(message, 0x40, b);
    Virtual<void>(Global<void*>(addr::SC4_MessageServer), 0x10, message, uint32_t{0});
    return Virtual<uint32_t>(message, 0x8);
}

// The game's 0x004F22C0. cdecl, no arguments: the volume limits and one more
// number from exemplar 0x6A359C5D. The original does not clear the pointer
// it hands the resource manager.
void __cdecl ReadVolumeTuning()
{
    void* exemplar;
    uint32_t key[3] = {kExemplarType, kAudioGroup, 0x6A359C5D};
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, static_cast<uint32_t*>(key), kExemplarIid,
                          &exemplar, uint32_t{0}, uint32_t{0}))
        return;
    void* const holder = Virtual<void*>(exemplar, 0x1C);
    Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, uint32_t{0x4A7CB3BA},
                   &Global<uint32_t>(addr::SC4AudioMusic_MusicVolumeMin));
    Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, uint32_t{0x4A7CB3BB},
                   &Global<uint32_t>(addr::SC4AudioMusic_SoundVolumeMin));
    Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, uint32_t{0x4A7CB3BC},
                   &Global<uint32_t>(addr::SC4AudioMusic_MusicVolumeMax));
    Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, uint32_t{0x4A7CB3BD},
                   &Global<uint32_t>(addr::SC4AudioMusic_SoundVolumeMax));
    Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, uint32_t{0x4A7CB3BF},
                   &Global<uint32_t>(addr::SC4Audio_MusicFadeTime));
    Release(exemplar);
}

// The game's 0x004F2580. cdecl, no arguments: the tracks' switches into the
// settings, the city list's first.
void __cdecl StoreTrackSwitches()
{
    Playlist* const lists = Playlists();
    {
        Track* track = lists[1].begin;
        Track* const end = lists[1].end;
        char* const switches = Global<char*>(addr::SC4_Options43CD8) + kAudio + prefs::kCityTracks;
        for (int32_t i = 0; track != end && i < 30; ++i, ++track)
            switches[i] = static_cast<char>(track->on);
    }
    {
        char* const switches = Global<char*>(addr::SC4_Options43CD8) + kAudio + prefs::kRegionTracks;
        Track* const end = lists[0].end;
        Track* track = lists[0].begin;
        for (int32_t i = 0; track != end && i < 30; ++i, ++track)
            switches[i] = static_cast<char>(track->on);
    }
}

// The game's 0x004F27A0 (the list in EDI, the folder on the stack): the
// names, in lower case, of the folder's files that match one of the patterns
// "*.mp3,*.wav,*.xa,*.m3u,*.pls,*.lnk", added to the list. Not hooked.
void ListFiles(StringList* out, GameString* folder)
{
    GameString empty;
    MakeEmpty(empty);
    Directory finder;
    This<void*>(addr::cRZDirectory_Construct, &finder, &empty, uint32_t{0}, uint32_t{0}, uint32_t{0});
    DestroyString(empty);

    char* const patterns = Global<char*>(addr::SC4AudioMusic_FilePatterns);
    char* end = patterns;
    while (*end)
        ++end;
    GameString rest;
    uint8_t tag = 0;
    rest.body.begin = nullptr;
    rest.body.end = nullptr;
    rest.body.capacity = nullptr;
    This<void>(addr::SC4String_FromRange, &rest.body, patterns, end, &tag);
    rest.vtable = addr::SC4String_Vtable;
    rest.zero = 0;
    GameString pattern;
    MakeEmpty(pattern);

    uint8_t more;
    do {
        more = This<uint8_t>(addr::SC4String_SplitAtChar, &rest, uint32_t{','}, &pattern);
        This<void>(addr::cRZDirectory_SetPath, &finder, folder, uint32_t{0});
        This<void>(addr::cRZDirectory_SetPattern, &finder, &pattern, uint32_t{0});
        This<void>(addr::cRZDirectory_GetFiles, &finder, out);
    } while (more);

    for (StringNode* node = out->head->next; node != out->head; node = node->next)
        This<void>(addr::cRZBaseString_MakeLower, &node->text);

    DestroyString(pattern);
    DestroyString(rest);
    This<void>(addr::cRZDirectory_Destruct, &finder);
}

// The game's 0x004F2910. cdecl (index): the list's tracks from its exemplar:
// thirty properties from 0x8A5E7B50 on, each with the title's and the file
// name's text instances and, as a third number, the switch. A track without
// a third number gets the switch of the one before it (the first, on); the
// three bytes after the switch are whatever the stack held.
void __cdecl LoadPlaylist(int32_t index)
{
    if (index < 0 || index >= 2)
        return;
    Playlist* const list = Playlists() + index;
    uint32_t id = BoxBase(static_cast<uint32_t>(index));
    uint32_t key[3] = {kExemplarType, kAudioGroup, list->exemplar};
    void* exemplar = nullptr;
    Track entry;
    entry.id = 0;
    entry.file = 0;
    entry.title = 0;
    entry.on = 1;
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, static_cast<uint32_t*>(key), kExemplarIid,
                          &exemplar, uint32_t{0}, uint32_t{0})) {
        if (exemplar)
            Release(exemplar);
        return;
    }
    void* const holder = Virtual<void*>(exemplar, 0x1C);
    uint32_t property = 0x8A5E7B50;
    for (int32_t left = 30; left != 0; --left, ++property, ++id) {
        void* const found = Virtual<void*>(holder, 0x24, property);
        if (!found)
            continue;
        void* const value = Virtual<void*>(found, 0x14);
        const uint32_t* const numbers = Virtual<const uint32_t*>(value, 0x19C);
        const uint32_t count = Virtual<uint32_t>(value, 0x10);
        if (count < 2)
            continue;
        entry.id = id;
        entry.title = numbers[0];
        entry.file = numbers[1];
        if (count >= 3)
            entry.on = static_cast<uint8_t>(numbers[2]);
        This<void>(addr::SC4LaneEntryVector_PushBack, &list->begin, &entry);
    }
    if (exemplar)
        Release(exemplar);
}

// The game's 0x004F2A70. (index, file name's text instance), ret 8; `this`
// is not used: whether the track's file is in the list's folder.
uint8_t __fastcall TrackFileExists(char*, void*, int32_t index, uint32_t file)
{
    if (index < 0 || index >= 2)
        return 0;
    Playlist* const list = Playlists() + index;
    LocalizedString name;
    StringBody* const nameBody = MusicText(name, file);
    StringBody joined;
    StringBody* const whole = Join(&joined, &list->folder.body, nameBody);
    GameString path;
    BodyFromRange(&path.body, whole->begin, whole->end);
    path.vtable = addr::SC4String_Vtable;
    path.zero = 0;
    if (joined.begin)
        Free(joined.begin);
    DestroyString(name.text);
    const uint8_t exists = Cdecl<uint8_t>(addr::RZFile_Exists, &path);
    DestroyString(path);
    return exists;
}

// One half of 0x004F2B70: the list's folder, the data folder with
// "Radio\Stations\" and the list's own part.
void SetFolder(Playlist& list, StringBody* data, StringBody* part)
{
    StringBody first;
    StringBody second;
    StringBody* const radio = Join(&first, data, FolderBody(addr::SC4AudioMusic_RadioFolder));
    StringBody* const whole = Join(&second, radio, part);
    StringBody copy;
    BodyFromRange(&copy, whole->begin, whole->end);
    AssignRange(&list.folder.body, copy.begin, copy.end);
    if (copy.begin)
        Free(copy.begin);
    if (second.begin)
        Free(second.begin);
    if (first.begin)
        Free(first.begin);
}

// The game's 0x004F2B70. cdecl, no arguments: the two records' numbers and
// folders, the city's first.
void __cdecl InitPlaylists()
{
    GameString data;
    MakeEmpty(data);
    Virtual<uint8_t>(Global<void*>(addr::SC4_Service43C94), 0xD4, &data, int32_t{-1}); // GetDataDirectory
    Playlist* const lists = Playlists();
    lists[1].index = 1;
    lists[1].exemplar = 0x2A5E5855;
    lists[1].id = 0x2A5FC8DD;
    SetFolder(lists[1], &data.body, FolderBody(addr::SC4AudioMusic_CityFolder));
    lists[0].index = 0;
    lists[0].exemplar = 0x2A5E5854;
    lists[0].id = 0xAA5FC8D5;
    SetFolder(lists[0], &data.body, FolderBody(addr::SC4AudioMusic_RegionFolder));
    DestroyString(data);
}

// The game's 0x004F2D40. cdecl, no arguments: the lists set up and read,
// once. The game has this inline in six more places.
void __cdecl EnsurePlaylists()
{
    if (Global<uint8_t>(addr::SC4AudioOptions_TablesMade))
        return;
    Global<uint8_t>(addr::SC4AudioOptions_TablesMade) = 1;
    Global<uint8_t>(addr::SC4AudioOptions_Filled) = 0;
    InitPlaylists();
    LoadPlaylist(0);
    LoadPlaylist(1);
}

// A list's folder for 0x004F2D70: the same three parts as SetFolder, with
// the temporaries freed in that function's order.
void JoinFolder(GameString& folder, StringBody* data, StringBody* part)
{
    StringBody first;
    StringBody second;
    StringBody* const radio = Join(&first, data, FolderBody(addr::SC4AudioMusic_RadioFolder));
    StringBody* const whole = Join(&second, radio, part);
    BodyFromRange(&folder.body, whole->begin, whole->end);
    folder.vtable = addr::SC4String_Vtable;
    folder.zero = 0;
    if (second.begin)
        Free(second.begin);
    if (first.begin)
        Free(first.begin);
}

// The candidate set to a folder and a track's file name, as 0x004F2D70 does
// it inline, and compared with the path.
uint8_t PathIsTrack(void* path, GameString& candidate, GameString& folder, const Track* track)
{
    LocalizedString name;
    StringBody* const nameBody = MusicText(name, track->file);
    StringBody joined;
    StringBody* const whole = Join(&joined, &folder.body, nameBody);
    int32_t length = static_cast<int32_t>(whole->end - whole->begin);
    const char* from = whole->begin;
    char* buffer = nullptr;
    if (static_cast<uint32_t>(length) + 1 == 0)
        Cdecl<void>(addr::STL_ThrowLengthError, "basic_string");
    else
        buffer = Cdecl<char*>(addr::SC4_stl_allocate, static_cast<uint32_t>(length) + 1);
    char* end = buffer;
    for (; length > 0; --length)
        *end++ = *from++;
    *end = 0;
    AssignRange(&candidate.body, buffer, end);
    if (buffer)
        Free(buffer);
    if (joined.begin)
        Free(joined.begin);
    DestroyString(name.text);
    return Virtual<uint8_t>(path, 0x28, &candidate, uint32_t{0});
}

// The game's 0x004F2D70. (path), ret 4; `this` is not used: the switch of
// the track whose file the path names, looked for in the region's list and,
// when it is not there, in the city's; true when it is in neither.
uint8_t __fastcall TrackSwitchOfPath(char*, void*, void* path)
{
    uint8_t on = 1;
    uint8_t found = 0;
    GameString candidate;
    MakeEmpty(candidate);
    GameString data;
    MakeEmpty(data);
    Virtual<uint8_t>(Global<void*>(addr::SC4_Service43C94), 0xD4, &data, int32_t{-1});
    Playlist* const lists = Playlists();
    {
        const char* flag = Global<char*>(addr::SC4_Options43CD8) + kAudio + prefs::kRegionTracks;
        Track* const end = lists[0].end;
        GameString folder;
        JoinFolder(folder, &data.body, FolderBody(addr::SC4AudioMusic_RegionFolder));
        for (Track* track = lists[0].begin; track != end; ++track, ++flag) {
            if (PathIsTrack(path, candidate, folder, track)) {
                on = *flag != 0;
                found = 1;
            }
        }
        if (folder.body.begin)
            Free(folder.body.begin);
    }
    if (!found) {
        const char* flag = Global<char*>(addr::SC4_Options43CD8) + kAudio + prefs::kCityTracks;
        GameString folder;
        JoinFolder(folder, &data.body, FolderBody(addr::SC4AudioMusic_CityFolder));
        Track* const end = lists[1].end;
        for (Track* track = lists[1].begin; track != end; ++track, ++flag) {
            if (PathIsTrack(path, candidate, folder, track))
                on = *flag != 0;
        }
        if (folder.body.begin)
            Free(folder.body.begin);
    }
    DestroyString(data);
    DestroyString(candidate);
    return on;
}

// The game's 0x004F31C0 (the index in ECX): whether the list's folder holds
// a music file that is not one of the list's tracks. Not hooked.
uint8_t FolderHasOtherFiles(int32_t index)
{
    uint8_t other = 0;
    if (index < 0 || index >= 2)
        return 0;
    Playlist* const list = Playlists() + index;
    StringNode* const known = NewListHead();
    StringList files;
    files.head = nullptr;
    files.head = NewListHead();
    GameString name;
    MakeEmpty(name);
    uint8_t tag = 0;

    Track* const end = list->end;
    for (Track* track = list->begin; track != end; ++track) {
        LocalizedString text;
        StringBody* const body = MusicText(text, track->file);
        if (body != &name.body)
            AssignRange(&name.body, body->begin, body->end);
        DestroyString(text.text);
        This<void>(addr::cRZBaseString_MakeLower, &name);
        StringNode* const node = Cdecl<StringNode*>(addr::SC4_stl_allocate, uint32_t{0x1C});
        This<void*>(addr::SC4String_CopyBuffer, &node->text.body, &name.body, uint32_t{0}, uint32_t{0xFFFFFFFF}, &tag);
        node->text.vtable = addr::SC4String_Vtable;
        node->text.zero = 0;
        StringNode* const last = known->previous;
        node->previous = last;
        node->next = known;
        last->next = node;
        known->previous = node;
    }

    ListFiles(&files, &list->folder);
    for (StringNode* file = files.head->next; file != files.head; file = file->next) {
        StringNode* at = nullptr;
        Cdecl<void>(addr::SC4StringList_Find, &at, known->next, known, &file->text, &tag);
        if (at == known) {
            other = 1;
            break;
        }
    }

    DestroyString(name);
    ClearList(files.head);
    Free(files.head);
    ClearList(known);
    Free(known);
    return other;
}

// The game's 0x004F3410. cdecl (index): the list's file names posted to the
// music player (message 0x0A611B34 with 1 for the region's list and 0 for
// the city's, the list of names, and the settings' leave-out switch). A
// track is named when its switch differs from the leave-out switch.
void __cdecl SendPlaylist(int32_t index)
{
    EnsurePlaylists();
    Playlist* const list = (index >= 0 && index < 2) ? Playlists() + index : nullptr;
    char* const options = Global<char*>(addr::SC4_Options43CD8);
    const char* switches = nullptr;
    if (index == 0)
        switches = options + kAudio + prefs::kRegionTracks;
    else if (index == 1)
        switches = options + kAudio + prefs::kCityTracks;
    const uint8_t leaveOut = At<uint8_t>(options, kAudio + prefs::kLeaveOut);
    if (!list || !switches)
        return;

    const int32_t count = static_cast<int32_t>(list->end - list->begin);
    This<void>(addr::SC4UI_F43ABA0, &list->files);
    for (int32_t i = 0; i < count; ++i) {
        if (leaveOut == static_cast<uint8_t>(switches[i]))
            continue;
        LocalizedString text;
        StringBody* const body = MusicText(text, list->begin[i].file);
        GameString name;
        uint8_t tag = 0;
        This<void*>(addr::SC4String_CopyBuffer, &name.body, body, uint32_t{0}, uint32_t{0xFFFFFFFF}, &tag);
        name.vtable = addr::SC4String_Vtable;
        name.zero = 0;
        DestroyString(text.text);
        StringNode* added = nullptr;
        This<void>(addr::SC4StringList_Insert, &list->files, &added, list->files.head, &name);
        DestroyString(name);
    }

    void* const message = NewMessage();
    AddRef(message);
    Virtual<void>(message, 0x14, kPlayList);
    if (index == 0)
        Virtual<void>(message, 0x2C, uint32_t{1});
    else if (index == 1)
        Virtual<void>(message, 0x2C, uint32_t{0});
    Virtual<void>(message, 0x44, &list->files);
    Virtual<void>(message, 0x54, uint32_t{leaveOut});
    Virtual<void>(Global<void*>(addr::SC4_MessageServer), 0x10, message, uint32_t{0});
    Release(message);
}

// --- The audio options dialog ---------------------------------------------------------

// The game's 0x004F2140 (the window in ECX, the child's ID on the stack):
// the child's flag 2. Not hooked.
uint8_t ChildEnabled(void* window, uint32_t id)
{
    uint8_t on = 0;
    if (window) {
        void* child = nullptr;
        if (Virtual<uint8_t>(window, 0x94, id, kWinIid, &child))
            on = Virtual<uint8_t>(child, 0x10C, uint32_t{2});
        if (child)
            Release(child);
    }
    return on;
}

// The game's 0x004F21B0 (the window in ECX; the child's ID and the value on
// the stack): sets the child's flag 2. Not hooked.
void EnableChild(void* window, uint32_t id, uint32_t on)
{
    if (!window)
        return;
    void* child = nullptr;
    if (Virtual<uint8_t>(window, 0x94, id, kWinIid, &child))
        Virtual<void>(child, 0x110, uint32_t{2}, on);
    if (child)
        Release(child);
}

// The game's 0x004F1F60 (slot 1).
uint32_t __fastcall DialogAddRef(char* self, void*)
{
    return ++At<uint32_t>(self, dialog::kRefs);
}

// The game's 0x004F2210 (slot 6): Shutdown.
uint8_t __fastcall DialogShutdown(char* self, void*)
{
    At<void*>(self, dialog::kOptions) = nullptr;
    if (void* const list = At<void*>(self, dialog::kList)) {
        At<void*>(self, dialog::kList) = nullptr;
        Release(list);
    }
    return 1;
}

// The game's 0x004F2230. No arguments: the settings as they are now sent to
// the sound service (its volume, or 0 when it is switched off) and posted to
// the music player.
void __fastcall DialogApply(char* self, void*)
{
    void* sound = nullptr;
    if (void* const framework = Cdecl<void*>(addr::RZGetFramework))
        Virtual<uint8_t>(framework, 0x14, uint32_t{0x23CECBA0}, uint32_t{0x106077D2}, &sound);
    if (sound) {
        char* const audio = At<char*>(self, dialog::kOptions) + kAudio;
        const uint32_t volume = At<uint8_t>(audio, prefs::kSoundOn) ? At<uint32_t>(audio, prefs::kSoundVolume) : 0;
        Virtual<void>(sound, 0x1C, uint32_t{0x2A5B69C0}, volume, uint32_t{0}, uint32_t{0}, uint32_t{0});
    }
    This<void*>(addr::SC4RefPtr_AssignGuarded, &sound, static_cast<void*>(nullptr));
    char* const audio = At<char*>(self, dialog::kOptions) + kAudio;
    PostNumbers(kMusicSettings, At<uint32_t>(audio, prefs::kMusicOn), At<uint32_t>(audio, prefs::kMusicVolume));
    This<void>(addr::SC4RefPtr_DestructGuarded, &sound);
}

// The game's 0x004F2370. (row out), ret 4: the list box's selected row.
uint8_t __fastcall DialogSelectedRow(char* self, void*, int32_t* row)
{
    void* const list = At<void*>(self, dialog::kList);
    if (!list)
        return 0;
    if (Virtual<int32_t>(list, 0xA0) <= 0)
        return 0;
    Cell cell;
    int32_t count = 1;
    Virtual<uint8_t>(At<void*>(self, dialog::kList), 0xA4, &cell, &count);
    if (count <= 0)
        return 0;
    *row = cell.row;
    return 1;
}

// The game's 0x004F23D0. (row in and out, list out), ret 8; `this` is not
// used: the track of a row of the list box, the region's tracks first; the
// row becomes the track's number in its list.
Track* __fastcall DialogTrackAt(char*, void*, int32_t* row, uint32_t* which)
{
    if (*row < 0)
        return nullptr;
    Playlist* const lists = Playlists();
    const int32_t first = static_cast<int32_t>(lists[0].end - lists[0].begin);
    if (*row < first) {
        *which = 0;
        return lists[0].begin + *row;
    }
    *row -= first;
    const uint32_t second = static_cast<uint32_t>(lists[1].end - lists[1].begin);
    if (static_cast<uint32_t>(*row) >= second)
        return nullptr;
    *which = 1;
    return lists[1].begin + *row;
}

// The game's 0x004F2450: the constructor.
char* __fastcall DialogConstruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4AudioOptionsDialog_Vtable;
    At<void*>(self, dialog::kWindow) = nullptr;
    At<void*>(self, dialog::kList) = nullptr;
    At<uint8_t>(self, dialog::kPreview) = 0;
    At<void*>(self, dialog::kOptions) = nullptr;
    This<void*>(addr::SC4AudioPreferences_Construct, self + dialog::kSaved);
    At<uint32_t>(self, dialog::kRefs) = 0;
    return self;
}

// The game's 0x004F2480. No arguments: ends the dialog: the preview switched
// off, the modal run ended, the window destroyed and let go. Ends in a jump
// to the dialog's Release.
uint32_t __fastcall DialogClose(char* self, void*)
{
    Virtual<uint32_t>(self, 0x4);
    if (At<uint8_t>(self, dialog::kPreview))
        PostNumbers(kPreviewSwitch, 0, 0);
    if (void* const holder = At<void*>(self, dialog::kWindow)) {
        void* const window = Virtual<void*>(holder, 0xC);
        void* const manager = Virtual<void*>(window, 0x18);
        if (manager) {
            if (Virtual<uint8_t>(window, 0x10C, uint32_t{0x1000})) {
                void* const again = Virtual<void*>(At<void*>(self, dialog::kWindow), 0xC);
                Virtual<void>(manager, 0x24, kEndModal, again, uint32_t{1});
            }
            Virtual<void>(manager, 0x5C, window);
        }
    }
    if (void* const holder = At<void*>(self, dialog::kWindow)) {
        At<void*>(self, dialog::kWindow) = nullptr;
        Release(holder);
    }
    Global<uint8_t>(addr::SC4AudioOptions_Filled) = 0;
    return Virtual<uint32_t>(self, 0x8);
}

// The game's 0x004F2520. (audio settings), ret 4: the settings put back and
// applied.
void __fastcall DialogRestore(char* self, void*, char* saved)
{
    char* const audio = At<char*>(self, dialog::kOptions) + kAudio;
    At<uint8_t>(audio, prefs::kSoundOn) = At<uint8_t>(saved, prefs::kSoundOn);
    At<uint32_t>(audio, prefs::kSoundVolume) = At<uint32_t>(saved, prefs::kSoundVolume);
    At<uint32_t>(audio, prefs::kMusicOn) = At<uint32_t>(saved, prefs::kMusicOn);
    At<uint32_t>(audio, prefs::kMusicVolume) = At<uint32_t>(saved, prefs::kMusicVolume);
    At<uint8_t>(audio, prefs::kLeaveOut) = At<uint8_t>(saved, prefs::kLeaveOut);
    std::memcpy(audio + prefs::kRegionTracks, saved + prefs::kRegionTracks, 30);
    std::memcpy(audio + prefs::kCityTracks, saved + prefs::kCityTracks, 30);
    DialogApply(self, nullptr);
}

// The game's 0x004F25F0. No arguments: runs the dialog as a modal window and
// closes it.
uint8_t __fastcall DialogRun(char* self, void*)
{
    uint32_t guard; // the guard is four flag bytes
    This<void*>(addr::SC4ModalGuard_Construct, &guard, uint32_t{0}, uint32_t{0}, uint32_t{1}, uint32_t{1});
    void* manager = nullptr;
    if (void* const framework = Cdecl<void*>(addr::RZGetFramework))
        Virtual<uint8_t>(framework, 0x14, uint32_t{0xA417445E}, uint32_t{0x5A4}, &manager);
    void* const focus = Virtual<void*>(manager, 0x90);
    void* const window = Virtual<void*>(At<void*>(self, dialog::kWindow), 0xC);
    if (window)
        AddRef(window);
    Virtual<void>(Global<void*>(addr::SC4_Service43CE0), 0x28, window, uint32_t{1});
    Virtual<void>(manager, 0xA4, window); // DoModalWin
    Virtual<void>(Global<void*>(addr::SC4_Service43CE0), 0x18, window);
    if (Virtual<uint8_t>(manager, 0x60, focus))
        Virtual<void>(manager, 0x94, focus);
    DialogClose(self, nullptr);
    if (window)
        Release(window);
    This<void>(addr::SC4RefPtr_Release, &manager);
    This<void>(addr::SC4ModalGuard_Destruct, &guard);
    return 1;
}

// The game's 0x004F26D0 (slot 2): Release.
uint32_t __fastcall DialogRelease(char* self, void*)
{
    const uint32_t count = At<uint32_t>(self, dialog::kRefs);
    if (count > 1) {
        At<uint32_t>(self, dialog::kRefs) = count - 1;
        return count - 1;
    }
    Virtual<uint8_t>(self, 0x18);
    At<std::uintptr_t>(self, 0x0) = addr::SC4AudioOptionsDialog_Vtable;
    if (void* const list = At<void*>(self, dialog::kList))
        Release(list);
    if (void* const window = At<void*>(self, dialog::kWindow))
        Release(window);
    Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return 0;
}

// The message that the options were closed, posted, and the dialog closed
// (the end of both OK and cancel).
void PostClosedAndClose(char* self)
{
    void* const server = Global<void*>(addr::SC4_MessageServer);
    void* const message = NewMessage();
    if (message)
        AddRef(message);
    Virtual<void>(message, 0x14, kOptionsClosed);
    Virtual<void>(server, 0x10, message, uint32_t{0});
    DialogClose(self, nullptr);
    Release(message);
}

// The game's 0x004F35F0 (with the pieces listed at 0x004F36B0 and
// 0x004F3A30). (control's ID, window, value), ret 0xC: what a control of the
// dialog does. Always true.
uint8_t __fastcall DialogCommand(char* self, void*, uint32_t id, void* window, uint32_t value)
{
    char* const options = Global<char*>(addr::SC4_Options43CD8);
    char* const audio = options + kAudio;
    switch (id) {
    case kSoundSlider:
    case kMusicSlider: {
        void* slider = nullptr;
        if (!Virtual<uint8_t>(window, 0x94, id, kSliderIid, &slider))
            return 1;
        const uint32_t position = Virtual<uint32_t>(slider, 0x10);
        Release(slider);
        if (id == kSoundSlider) {
            const uint32_t volume = VolumeFromSlider(position, 0);
            At<uint32_t>(audio, prefs::kSoundVolume) =
                Global<uint32_t>(addr::SC4AudioMusic_SoundVolumeMin) < volume ? volume : 0;
        } else {
            const uint32_t volume = VolumeFromSlider(position, 1);
            At<uint32_t>(audio, prefs::kMusicVolume) =
                Global<uint32_t>(addr::SC4AudioMusic_MusicVolumeMin) < volume ? volume : 0;
        }
        DialogApply(self, nullptr);
        return 1;
    }
    case kSoundSwitch:
        At<uint8_t>(audio, prefs::kSoundOn) = static_cast<uint8_t>(value);
        DialogApply(self, nullptr);
        return 1;
    case kMusicSwitch:
        At<uint32_t>(audio, prefs::kMusicOn) = value;
        DialogApply(self, nullptr);
        SendPlaylist(0);
        SendPlaylist(1);
        return 1;
    case kTrackList: {
        // A row chosen: the play button is for a track whose file is there.
        EnableChild(window, kPlayButton, 0);
        void* sound = nullptr;
        This<void*>(addr::SC4RefHolder_SC4RefHolder, &sound, Global<void*>(addr::SC4_Sound43CB8));
        if (sound)
            Virtual<void>(sound, 0x2C, uint32_t{1}, kClickSound, uint32_t{0}, uint32_t{0}, uint32_t{0});
        int32_t row = 0;
        if (DialogSelectedRow(self, nullptr, &row)) {
            uint32_t which = 0;
            if (DialogTrackAt(self, nullptr, &row, &which)) {
                if (ChildEnabled(window, BoxBase(which) + static_cast<uint32_t>(row)))
                    EnableChild(window, kPlayButton, 1);
            }
        }
        if (sound)
            Release(sound);
        return 1;
    }
    case kPlayButton: {
        if (!At<uint8_t>(self, dialog::kPreview)) {
            At<uint8_t>(self, dialog::kPreview) = 1;
            PostNumbers(kPreviewSwitch, 1, 0);
        }
        int32_t row = 1;
        if (!DialogSelectedRow(self, nullptr, &row))
            return 1;
        uint32_t which = 0;
        if (!DialogTrackAt(self, nullptr, &row, &which))
            return 1;
        Playlist* const list = PlaylistAt(static_cast<int32_t>(which));
        LocalizedString text;
        void* const got = This<void*>(addr::SC4LocalizedString_SC4LocalizedString, &text, kMusicTexts,
                                      list->begin[row].file, uint32_t{1});
        GameString name;
        This<void*>(addr::SC4String_Substring, &name, got, uint32_t{0}, uint32_t{0xFFFFFFFF});
        This<void>(addr::SC4String_Destroy, &text);
        void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x14});
        void* path = nullptr;
        if (memory) {
            StringBody joined;
            StringBody* const whole = Join(&joined, &list->folder.body, &name.body);
            path = This<void*>(addr::cSC4Advice_F408B90, memory, whole);
            if (joined.begin)
                Free(joined.begin);
        }
        AddRef(path);
        void* const message = NewMessage();
        AddRef(message);
        Virtual<void>(message, 0x14, kPlayFile);
        Virtual<void>(message, 0x30, path);
        Virtual<void>(Global<void*>(addr::SC4_MessageServer), 0x10, message, uint32_t{0});
        Release(message);
        This<void>(addr::SC4String_Destroy, &name);
        return 1;
    }
    case kStopButton:
        if (!At<uint8_t>(self, dialog::kPreview)) {
            At<uint8_t>(self, dialog::kPreview) = 1;
            PostNumbers(kPreviewSwitch, 1, 0);
        }
        PostNumbers(kStopPreview, 0, 0);
        return 1;
    case kLeaveOutSwitch:
        At<uint8_t>(audio, prefs::kLeaveOut) = static_cast<uint8_t>(value);
        return 1;
    case kCancelButton:
        DialogRestore(self, nullptr, self + dialog::kSaved);
        PostClosedAndClose(self);
        return 1;
    case kOkButton: {
        void* app = Global<void*>(addr::SC4_Service43C94);
        if (app) {
            AddRef(app);
            Virtual<uint8_t>(app, 0x44); // SavePreferences
            void* const held = app;
            app = nullptr;
            Release(held);
        }
        SendPlaylist(0);
        SendPlaylist(1);
        void* player = nullptr;
        if (void* const framework = Cdecl<void*>(addr::RZGetFramework))
            Virtual<uint8_t>(framework, 0x14, uint32_t{0x1D07B39F}, uint32_t{0xE0510260}, &player);
        if (player) {
            // The file being played stops when its track is switched off.
            GameString playing;
            This<void*>(addr::SC4String_SC4String, &playing);
            if (Virtual<uint8_t>(player, 0x80, &playing)) {
                if (!TrackSwitchOfPath(self, nullptr, &playing))
                    PostNumbers(kStopPreview, 0, 0);
            }
            DestroyString(playing);
        }
        PostClosedAndClose(self);
        This<void>(addr::SC4Audio_F4EDF00, &player);
        if (app)
            Release(app);
        return 1;
    }
    default:
        break;
    }
    // A track's check box. The original takes 31 IDs for each list of 30.
    const uint32_t regionBase = BoxBase(0);
    if (id >= regionBase && id <= regionBase + 0x1E) {
        At<uint8_t>(options, kAudio + prefs::kRegionTracks + (id - regionBase)) = static_cast<uint8_t>(value);
        return 1;
    }
    const uint32_t cityBase = BoxBase(1);
    if (id >= cityBase && id <= cityBase + 0x1E)
        At<uint8_t>(options, kAudio + prefs::kCityTracks + (id - cityBase)) = static_cast<uint8_t>(value);
    return 1;
}

// The game's 0x004F4280 (slot 3, with the piece listed at 0x004F43A0).
// (window, message), ret 8: the dialog's window procedure.
uint8_t __fastcall DialogWinMessage(char* self, void*, void* window, WinMessage* message)
{
    if (!Virtual<uint8_t>(window, 0x11C))
        return 0;
    if (window != Virtual<void*>(At<void*>(self, dialog::kWindow), 0xC)) {
        void* const list = At<void*>(self, dialog::kList);
        if (!list)
            return 0;
        if (Virtual<void*>(list, 0x14) != window)
            return 0;
    }
    if (message->type == 5) {
        if (message->a == 0x1B) // the escape key
            DialogCommand(self, nullptr, kCancelButton, window, 0);
        return 0;
    }
    if (message->type == 0x9A1552D2) {
        DialogCommand(self, nullptr, kTrackList, window, message->c);
        return 0;
    }
    if (message->type != 3)
        return 0;

    const uint32_t b = message->b;
    switch (message->a) {
    case 0x287259F6: // a button pressed
        if (b == kCancelButton || b == kOkButton || b == kPlayButton || b == kStopButton)
            DialogCommand(self, nullptr, b, window, message->c);
        break;
    case 0x287259F7:
        if (b > kStopButton) {
            if (b < kCancelButton || b > kOkButton)
                DialogCommand(self, nullptr, b, window, message->c);
        } else if (b < kPlayButton && b != kSoundSwitch) {
            DialogCommand(self, nullptr, b, window, message->c);
        }
        break;
    case 0x287259F8: {
        if (b != kSoundSwitch || message->c != 0)
            break;
        void* button = nullptr;
        void** const holder = This<void**>(addr::SC4RefPtr_Reset, &button);
        if (Virtual<uint8_t>(window, 0x94, kSoundSwitch, kButtonIid, holder)) {
            const uint32_t checked = Virtual<uint8_t>(button, 0x28) ? 1 : 0;
            if (void* const held = button) {
                button = nullptr;
                Release(held);
            }
            DialogCommand(self, nullptr, kSoundSwitch, window, checked);
        }
        if (button)
            Release(button);
        break;
    }
    case 0x887113A3: // a slider moved
        if (b == kMusicSlider || b == kSoundSlider)
            DialogCommand(self, nullptr, b, window, message->c);
        break;
    case 0xE8711255:
        DialogCommand(self, nullptr, b, window, message->c);
        break;
    default:
        break;
    }
    return 1;
}

// A control's window told that the dialog's window is the one to notify.
void Attach(char* self, void* control)
{
    void* const child = Virtual<void*>(control, 0xC);
    void* const window = Virtual<void*>(At<void*>(self, dialog::kWindow), 0xC);
    Virtual<void>(child, 0x158, window);
}

void ReleaseHolder(void*& holder)
{
    if (void* const held = holder) {
        holder = nullptr;
        Release(held);
    }
}

// A volume as a slider's position (0 to 1000); a volume under the lower
// limit is put down as 0 in the dialog's copy.
uint32_t SliderFromVolume(uint32_t& kept, uint32_t volume, uint32_t low, uint32_t high)
{
    if (volume > high) {
        volume = high;
    } else if (volume < low) {
        kept = 0;
        return 0;
    }
    if (volume <= low)
        return 0;
    if (volume >= 1000)
        return 1000;
    const double part = static_cast<double>(volume - low) / static_cast<double>(high - low);
    return Ftol(part * 1000.0);
}

// A slider of the dialog set from the dialog's copy of a volume.
void FillSlider(char* self, void* window, void*& slider, uint32_t id, std::size_t field, std::uintptr_t low,
                std::uintptr_t high)
{
    if (!Virtual<uint8_t>(window, 0x94, id, kSliderIid, &slider))
        return;
    const uint32_t volume = At<uint32_t>(self, field);
    Virtual<void>(slider, 0x18, int32_t{0});
    Virtual<void>(slider, 0x20, int32_t{1000});
    Attach(self, slider);
    const uint32_t top = Global<uint32_t>(high);
    const uint32_t bottom = Global<uint32_t>(low);
    const uint32_t position = SliderFromVolume(At<uint32_t>(self, field), volume, bottom, top);
    Virtual<void>(slider, 0x14, position);
    ReleaseHolder(slider);
}

// One list's rows of the list box: per track (30 at most) a check box with
// the track's switch, enabled when the track's file is there, and the
// track's title.
void AddRows(char* self, int32_t index, int32_t firstRow)
{
    const char* const switches = Global<char*>(addr::SC4_Options43CD8) + kAudio +
                                 (index == 0 ? prefs::kRegionTracks : prefs::kCityTracks);
    ImageLoader boxes;
    uint32_t key[3] = {0x856DDBAC, 0x46A006B0, 0x14416244};
    This<void*>(addr::SC4ImageLoader_ConstructKey, &boxes, static_cast<uint32_t*>(key), uint32_t{1}, uint32_t{0});
    void* const image = boxes.image;
    Playlist* const list = Playlists() + index;
    Track* const end = list->end;
    if (list->begin != end) {
        GameString title;
        This<void*>(addr::SC4String_SC4String, &title);
        Track* track = list->begin;
        for (int32_t i = 0; track != end && i < 30; ++i, ++track) {
            const int32_t row = firstRow + i;
            const uint8_t exists = TrackFileExists(self, nullptr, index, track->file);
            LocalizedString text;
            StringBody* const body = MusicText(text, track->title);
            if (body != &title.body)
                AssignRange(&title.body, body->begin, body->end);
            DestroyString(text.text);

            void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x1B4});
            void* const box = memory ? This<void*>(addr::SC4UI_CheckBoxConstruct, memory) : nullptr;
            if (box)
                AddRef(box);
            AddRef(box);
            Virtual<void>(box, 0x30, uint32_t{4}, uint32_t{0}, uint32_t{0});
            Virtual<void>(box, 0xBC, BoxBase(static_cast<uint32_t>(index)) + static_cast<uint32_t>(i));
            Virtual<void>(box, 0x24, uint32_t{switches[i] != 0});
            Virtual<void>(box, 0x44, image);
            Virtual<void>(box, 0x14, uint32_t{4}, uint32_t{0});
            Attach(self, box);
            Virtual<void>(Virtual<void*>(box, 0xC), 0x110, uint32_t{2}, uint32_t{exists != 0});
            Virtual<void>(box, 0xB4, uint32_t{2}, kClickSound);

            void* rows = At<void*>(self, dialog::kList);
            const uint32_t length = This<uint32_t>(addr::cRZBaseString_Strlen, &title);
            char* const data = This<char*>(addr::cRZBaseString_Data, &title);
            Virtual<void>(rows, 0x1C0, uint32_t{0}, row, data, length, uint32_t{1}, uint32_t{0});
            rows = At<void*>(self, dialog::kList);
            void* const boxWindow = Virtual<void*>(box, 0xC);
            Virtual<void>(rows, 0x1D0, uint32_t{1}, row, boxWindow, uint32_t{0}, uint32_t{1}, uint32_t{1});
            Release(box);
            if (box)
                Release(box);
        }
        DestroyString(title);
    }
    This<void>(addr::SC4ImageLoader_Destruct, &boxes);
}

// The game's 0x004F44C0 (to 0x004F511D). No arguments: keeps the settings as
// they are and fills the window: the buttons, the switches, the two sliders
// and the list box with its rows. Init (0x004F5120, part 8) calls it.
void __fastcall DialogFill(char* self, void*)
{
    Global<uint8_t>(addr::SC4AudioOptions_Filled) = 1;
    char* const audio = Global<char*>(addr::SC4_Options43CD8) + kAudio;
    char* const saved = self + dialog::kSaved;
    At<uint8_t>(saved, prefs::kSoundOn) = At<uint8_t>(audio, prefs::kSoundOn);
    At<uint32_t>(saved, prefs::kSoundVolume) = At<uint32_t>(audio, prefs::kSoundVolume);
    At<uint32_t>(saved, prefs::kMusicOn) = At<uint32_t>(audio, prefs::kMusicOn);
    At<uint32_t>(saved, prefs::kMusicVolume) = At<uint32_t>(audio, prefs::kMusicVolume);
    At<uint8_t>(saved, prefs::kLeaveOut) = At<uint8_t>(audio, prefs::kLeaveOut);
    std::memcpy(saved + prefs::kRegionTracks, audio + prefs::kRegionTracks, 30);
    std::memcpy(saved + prefs::kCityTracks, audio + prefs::kCityTracks, 30);

    void* const window = Virtual<void*>(At<void*>(self, dialog::kWindow), 0xC);
    void* button = nullptr;
    void* slider = nullptr;
    EnableChild(window, kPlayButton, 0);
    ReleaseHolder(button);

    if (Virtual<uint8_t>(window, 0x94, kPlayButton, kButtonIid, &button))
        Attach(self, button);
    ReleaseHolder(button);

    if (Virtual<uint8_t>(window, 0x94, kStopButton, kButtonIid, &button))
        Attach(self, button);
    ReleaseHolder(button);

    if (Virtual<uint8_t>(window, 0x94, kLeaveOutSwitch, kButtonIid, &button)) {
        Virtual<void>(button, 0x24, uint32_t{At<uint8_t>(saved, prefs::kLeaveOut) != 0});
        void* const buttonWindow = Virtual<void*>(button, 0xC);
        uint8_t other = FolderHasOtherFiles(1);
        if (!other)
            other = FolderHasOtherFiles(0);
        Virtual<void>(buttonWindow, 0x110, uint32_t{2}, uint32_t{other});
        Attach(self, button);
    }
    ReleaseHolder(button);

    if (Virtual<uint8_t>(window, 0x94, kSoundSwitch, kButtonIid, &button)) {
        Virtual<void>(button, 0x24, uint32_t{At<uint8_t>(saved, prefs::kSoundOn) != 0});
        Attach(self, button);
    }
    ReleaseHolder(button);

    if (Virtual<uint8_t>(window, 0x94, kMusicSwitch, kButtonIid, &button)) {
        Virtual<void>(button, 0x24, uint32_t{At<uint32_t>(saved, prefs::kMusicOn) != 0});
        Attach(self, button);
        ReleaseHolder(button);
    }

    ReleaseHolder(slider);
    FillSlider(self, window, slider, kMusicSlider, dialog::kSaved + prefs::kMusicVolume,
               addr::SC4AudioMusic_MusicVolumeMin, addr::SC4AudioMusic_MusicVolumeMax);
    ReleaseHolder(slider);
    FillSlider(self, window, slider, kSoundSlider, dialog::kSaved + prefs::kSoundVolume,
               addr::SC4AudioMusic_SoundVolumeMin, addr::SC4AudioMusic_SoundVolumeMax);

    ReleaseHolder(At<void*>(self, dialog::kList));
    if (Virtual<uint8_t>(window, 0x94, kTrackList, kListBoxIid, &At<void*>(self, dialog::kList))) {
        void* rows = At<void*>(self, dialog::kList);
        Virtual<void>(rows, 0xB8);
        Virtual<void>(At<void*>(self, dialog::kList), 0x188);
        Virtual<void>(At<void*>(self, dialog::kList), 0x30, uint32_t{2});
        Virtual<void>(At<void*>(self, dialog::kList), 0x30, uint32_t{0x8000});
        Virtual<void>(At<void*>(self, dialog::kList), 0x28, uint32_t{0x2000});
        Virtual<void>(At<void*>(self, dialog::kList), 0x28, uint32_t{0x80000});
        Virtual<void>(At<void*>(self, dialog::kList), 0x9C, uint32_t{2}, uint32_t{1});
        void* rowsWindow = Virtual<void*>(At<void*>(self, dialog::kList), 0x14);
        Virtual<void>(rowsWindow, 0x1AC, uint32_t{0xED}, uint32_t{0xF3}, uint32_t{0xF0});
        void* colours = Cdecl<void*>(addr::SC4_Singleton913C1A);
        const uint32_t pale = Virtual<uint32_t>(colours, 0x88, uint32_t{0xED}, uint32_t{0xF3}, uint32_t{0xF0});
        colours = Cdecl<void*>(addr::SC4_Singleton913C1A);
        const uint32_t blue = Virtual<uint32_t>(colours, 0x88, uint32_t{0}, uint32_t{0}, uint32_t{0xFF});
        colours = Cdecl<void*>(addr::SC4_Singleton913C1A);
        const uint32_t white = Virtual<uint32_t>(colours, 0x88, uint32_t{0xFF}, uint32_t{0xFF}, uint32_t{0xFF});
        rowsWindow = Virtual<void*>(At<void*>(self, dialog::kList), 0x14);
        Virtual<void>(rowsWindow, 0x110, uint32_t{0x20000}, uint32_t{1});
        Virtual<void>(At<void*>(self, dialog::kList), 0x44, pale, uint32_t{1});
        Virtual<void>(At<void*>(self, dialog::kList), 0x44, blue, uint32_t{5});
        Virtual<void>(At<void*>(self, dialog::kList), 0x44, white, uint32_t{4});
        Virtual<void>(At<void*>(self, dialog::kList), 0x104, uint32_t{0}, uint32_t{1}, uint32_t{0xFF});

        AddRows(self, 0, 0);
        Playlist* const lists = Playlists();
        const int32_t regionRows = static_cast<int32_t>(lists[0].end - lists[0].begin);
        AddRows(self, 1, regionRows);

        rows = At<void*>(self, dialog::kList);
        Virtual<void>(rows, 0xB8);
        void* const listWindow = Virtual<void*>(At<void*>(self, dialog::kList), 0x14);
        void* const dialogWindow = Virtual<void*>(At<void*>(self, dialog::kWindow), 0xC);
        Virtual<void>(listWindow, 0x158, dialogWindow);
    }
    if (slider)
        Release(slider);
    if (button)
        Release(button);
}

bool InstallAudio7(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[28] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4Audio_F4F1A80, reinterpret_cast<void*>(&ListenerReadTuning)},
        {addr::SC4Audio_F4F1D60, reinterpret_cast<void*>(&ListenerCityMessage)},
        {addr::SC4Audio_F4F1E50, reinterpret_cast<void*>(&ListenerMessage)},
        {addr::SC4Audio_F4F1F60, reinterpret_cast<void*>(&DialogAddRef)},
        {addr::SC4Audio_F4F1F70, reinterpret_cast<void*>(&PlaylistAt)},
        {addr::SC4Audio_F4F1F90, reinterpret_cast<void*>(&VolumeFromSlider)},
        {addr::SC4Audio_F4F20E0, reinterpret_cast<void*>(&PostNumbers)},
        {addr::SC4Audio_F4F2210, reinterpret_cast<void*>(&DialogShutdown)},
        {addr::SC4Audio_F4F2230, reinterpret_cast<void*>(&DialogApply)},
        {addr::SC4Audio_F4F22C0, reinterpret_cast<void*>(&ReadVolumeTuning)},
        {addr::SC4Audio_F4F2370, reinterpret_cast<void*>(&DialogSelectedRow)},
        {addr::SC4Audio_F4F23D0, reinterpret_cast<void*>(&DialogTrackAt)},
        {addr::SC4UI_F4F2450, reinterpret_cast<void*>(&DialogConstruct)},
        {addr::SC4Audio_F4F2480, reinterpret_cast<void*>(&DialogClose)},
        {addr::SC4Audio_F4F2520, reinterpret_cast<void*>(&DialogRestore)},
        {addr::SC4Audio_F4F2580, reinterpret_cast<void*>(&StoreTrackSwitches)},
        {addr::SC4UI_F4F25F0, reinterpret_cast<void*>(&DialogRun)},
        {addr::SC4Audio_F4F26D0, reinterpret_cast<void*>(&DialogRelease)},
        {addr::SC4Audio_F4F2910, reinterpret_cast<void*>(&LoadPlaylist)},
        {addr::SC4Audio_F4F2A70, reinterpret_cast<void*>(&TrackFileExists)},
        {addr::SC4Audio_F4F2B70, reinterpret_cast<void*>(&InitPlaylists)},
        {addr::SC4Audio_F4F2D40, reinterpret_cast<void*>(&EnsurePlaylists)},
        {addr::SC4Audio_F4F2D70, reinterpret_cast<void*>(&TrackSwitchOfPath)},
        {addr::SC4Audio_F4F3410, reinterpret_cast<void*>(&SendPlaylist)},
        {addr::SC4Audio_F4F35F0, reinterpret_cast<void*>(&DialogCommand)},
        {addr::SC4Audio_F4F4280, reinterpret_cast<void*>(&DialogWinMessage)},
        {addr::SC4Audio_F4F44C0, reinterpret_cast<void*>(&DialogFill)},
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

const bool g_audio7Registered = registry::Register({
    "audio.part7",
    "audio",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F4F25F0,
    "Our listener tuning and messages, music lists and audio options dialog (docs/audio.md)",
    false,
    InstallAudio7,
});

} // namespace
} // namespace opensc4::audio
