// The audio code, part 6 (0x004EF010 to 0x004F1A80): the end of the audio
// event handler, the fire sounds' handler, the cluster sets of the ambience
// layers and most of the listener.
//
// What this part holds:
//
//   - cSC4AudioEventHandler (class 0xBDB6171E, 0x50 bytes, made by
//     0x004EB0A0), its second half: the pointer sounds (three looping
//     sounds whose pitch follows the pointer's speed), the handler of the
//     sound service's events (main slot 3), the click outside the front
//     window, the constructor and destructor, the two message handlers
//     (DoMessage of the +8 part with its 24 message types, and the old
//     style target at +4 that keeps the list of open windows) and Init
//     (slot 3 of the +0xC part). Its first half (QueryInterface, Release,
//     Shutdown, the music and the other message cases) is part 5's;
//   - cSC4AudioEventHandlerDisasterFire (class 0x69E500BC, 0x34 bytes, made
//     by 0x004EB100, which hands out the +4 part): all of it. It tells the
//     ambience manager (service 0x49E50BA1) of every burning occupant, under
//     the group "Fires";
//   - the cluster set (0x28 bytes, vtable 0x00A94200, no class ID), three of
//     which every ambience layer keeps at +0x28, +0x2C and +0x30 (part 2):
//     points gathered into clusters by distance. All of it but the two
//     shared slots of its vtable;
//   - cSC4AudioListener (class 0xCA4A66B9, 0x7C bytes, made by 0x004EB0E0):
//     QueryInterface, Release, constructor, destructor, Init, Shutdown, the
//     update from the camera and the height table. Its DoMessage
//     (0x004F1E50) and tuning reader (0x004F1A80) are part 7's.
//
// Replacement, off by default:
//
//   audio.part6: everything named above.
//
// Not functions: 0x004EEF80 is the end of part 5's 0x004EEC10; 0x004EFA20
// is inside DoMessage (0x004EF810), 0x004EFF10 and 0x004F01C0 inside Init
// (0x004EFE80), 0x004F0700 inside the fire handler's DoMessage (0x004F05D0)
// and 0x004F0B00 inside 0x004F0AC0.
//
// Still the game's: 0x004EF7F0 (the AddRef thunk of the event handler's
// +0xC part, which another class's vtable 0x00A87594 holds too), 0x004F0240
// (the fire handler's AddRef: a count at +0x1C, shared with three other
// classes' vtables) and 0x004F0940 (SC4Uint32HashSet::Resize, the whole
// game's).
//
// The event handler (offsets from the object): +0x00 the main vtable
// 0x00A94164, which is the sound service's hook (interface 0xBB523BB2:
// slot 3 gets the service's events); +0x04 an old style message target
// (0x00A94154); +0x08 the message target (0x00A94144); +0x0C the system
// service part (0x00A94130); +0x10 the music player (service 0x1D07B39F,
// interface 0xE0510260); +0x14 the sound service (0x23CECBA0, interface
// 0x106077D2); +0x18 the ambience manager; +0x1C a byte set by Init; +0x20
// the reference count; +0x24 and +0x28 the view's size (part 5); +0x2C a list of the open windows that asked for
// clicks outside them, newest first; +0x30 a stopwatch; +0x48 a dword.
//
// The fire handler: +0x00 the message target (0x00A941E8); +0x04 the system
// service part (0x00A941D4); +0x08 an occupant filter (0x00A941B8 and
// 0x00A941A0, 12 bytes) that takes fire occupants (interface 0x69D25C60);
// +0x14 a sound service hook that does nothing (0x00A94190); +0x18 a byte
// set by Init; +0x1C the reference count; +0x20 the ambience manager.
//
// The cluster set: +0x04 the reference count; +0x08 a hash map from a
// point's key to its record; +0x1C the list of clusters, newest first;
// +0x20 the number of clusters; +0x24 the radius a new cluster gets. A
// point's record (0x1C bytes): +0 the key, +4 its cluster, +8 the place
// (three floats), +0x14 its radius, +0x18 the map. A cluster (0x20 bytes):
// +0 the list of its points, +4 their number, +8 the highest that number
// has been, +0xC the place, +0x18 the radius, +0x1C a byte used while two
// sets are merged.
//
// The listener: +0x04 the reference count; +0x08 a byte set by Init; +0x0C
// where it is (three floats); +0x18 a point behind it; +0x24 what the
// camera looks at; +0x30 the camera's place when the last message went
// out; +0x3C, +0x48, +0x54 and +0x60 four arrays of floats by zoom (part 7
// fills them: two factors of the height, an unused one and the squared
// distance the camera must move); +0x6C a factor of the heights (1); +0x70
// 4; +0x74 the sound library's listener; +0x78 the message server.
//
// Time: nothing here reads a clock or the sound library's playing state.
// The pointer sounds use the time stamp the event carries (real time, as
// the window system gives it); the listener follows the camera, frame by
// frame; the rest follows messages and the city.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace opensc4::audio {
namespace {

namespace handler { // the event handler, offsets from the object
constexpr std::size_t kTarget = 0x04;
constexpr std::size_t kTarget2 = 0x08;
constexpr std::size_t kService = 0x0C;
constexpr std::size_t kMusic = 0x10;
constexpr std::size_t kSound = 0x14;
constexpr std::size_t kEmitters = 0x18;
constexpr std::size_t kStarted = 0x1C;
constexpr std::size_t kWindows = 0x2C;
constexpr std::size_t kWatch = 0x30;
} // namespace handler

namespace fire { // the fire handler, offsets from the object
constexpr std::size_t kService = 0x04;
constexpr std::size_t kFilter = 0x08;
constexpr std::size_t kFilter2 = 0x0C;
constexpr std::size_t kHook = 0x14;
constexpr std::size_t kStarted = 0x18;
constexpr std::size_t kReferences = 0x1C;
constexpr std::size_t kEmitters = 0x20;
} // namespace fire

namespace set { // the cluster set
constexpr std::size_t kReferences = 0x04;
constexpr std::size_t kMap = 0x08;
constexpr std::size_t kClusters = 0x1C;
constexpr std::size_t kClusterCount = 0x20;
constexpr std::size_t kRadius = 0x24;
} // namespace set

namespace map { // the hash map inside the set
constexpr std::size_t kBuckets = 0x04;
constexpr std::size_t kBucketsEnd = 0x08;
constexpr std::size_t kCapacity = 0x0C;
constexpr std::size_t kCount = 0x10;
} // namespace map

namespace point { // a point's record
constexpr std::size_t kKey = 0x00;
constexpr std::size_t kCluster = 0x04;
constexpr std::size_t kPlace = 0x08;
constexpr std::size_t kRadius = 0x14;
constexpr std::size_t kMap = 0x18;
} // namespace point

namespace cluster {
constexpr std::size_t kPoints = 0x00;
constexpr std::size_t kCount = 0x04;
constexpr std::size_t kPeak = 0x08;
constexpr std::size_t kPlace = 0x0C;
constexpr std::size_t kRadius = 0x18;
constexpr std::size_t kTaken = 0x1C;
} // namespace cluster

namespace listener {
constexpr std::size_t kReferences = 0x04;
constexpr std::size_t kStarted = 0x08;
constexpr std::size_t kPlace = 0x0C;
constexpr std::size_t kBehind = 0x18;
constexpr std::size_t kLookAt = 0x24;
constexpr std::size_t kLastCamera = 0x30;
constexpr std::size_t kFactorsA = 0x3C;
constexpr std::size_t kFactorsB = 0x48;
constexpr std::size_t kArrayC = 0x54;
constexpr std::size_t kMoveLimits = 0x60;
constexpr std::size_t kHeightFactor = 0x6C;
constexpr std::size_t kFour = 0x70;
constexpr std::size_t kLibraryListener = 0x74;
constexpr std::size_t kServer = 0x78;
} // namespace listener

namespace camera { // the camera view (0x00B43DD8), as far as the listener reads it
constexpr std::size_t kZoom = 0x0C;
constexpr std::size_t kEye = 0x3C;
constexpr std::size_t kTarget = 0x48;
constexpr std::size_t kUp = 0x6C;
} // namespace camera

// Interfaces and services.
constexpr uint32_t kStandardMessage = 0x65297976;
constexpr uint32_t kMessageServerService = 0x04FA845B;
constexpr uint32_t kMessageServerInterface = 0x652294C7;
constexpr uint32_t kSoundService = 0x23CECBA0;
constexpr uint32_t kSoundInterface = 0x106077D2;
constexpr uint32_t kEmitterService = 0x49E50BA1;
constexpr uint32_t kEmitterInterface = 0x29E39DB4;
constexpr uint32_t kSoundHookInterface = 0xBB523BB2;
constexpr uint32_t kMessageTargetInterface = 0x452294AA;
constexpr uint32_t kServiceInterface = 0x89E51472;

// What the sound service's slot 0x2C is told to do.
constexpr uint32_t kStartOnChannel = 0xEA451903;
constexpr uint32_t kSetChannelValue = 0x4A451915;
constexpr uint32_t kStopChannel = 0x6A451908;

// The pointer sounds' events.
constexpr uint32_t kPointerStopAll = 0x8A10611A;
constexpr uint32_t kPointerPitchOff = 0x0A106122;
constexpr uint32_t kPointerOneShot = 0x2A106113;
constexpr uint32_t kPointerMoved = 0x4A106126;
constexpr uint32_t kPointerPitchOn = 0x8A10611D;
constexpr uint32_t kPointerButton = 0xAA96FE8F;

constexpr uint32_t kClickOutside = 0xFA0C6390;
constexpr uint32_t kWindowOpened = 0x62BA7B42;
constexpr uint32_t kWindowClosed = 0x62BA7B43;
constexpr uint32_t kMusicChoice = 0x2A3AD653;
constexpr uint32_t kFireGroup = 0x49E4CE85;
constexpr uint32_t kCityInited = 0x26D31EC1;
constexpr uint32_t kOccupantInserted = 0x99EF1142;
constexpr uint32_t kOccupantRemoved = 0x99EF1143;
constexpr uint32_t kListenerMoved = 0x8A4A69C7;

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

uint32_t AddRef(void* object)
{
    return Virtual<uint32_t>(object, 0x4);
}

uint32_t Release(void* object)
{
    return Virtual<uint32_t>(object, 0x8);
}

// __ftol2: truncation toward zero, the low 32 bits.
int32_t Ftol(double value)
{
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

uint32_t Bits(const float& value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

// The inlined request for a system service: nothing when there is no
// framework.
void GetService(uint32_t service, uint32_t interfaceId, void** out)
{
    if (void* const framework = Cdecl<void*>(addr::RZGetFramework))
        Virtual<uint8_t>(framework, 0x14, service, interfaceId, out);
}

// A new framework message with one reference taken. The original calls
// through the null pointer when there is no memory, and so does this.
void* NewMessage()
{
    void* const memory = Cdecl<void*>(addr::RZ_operator_new, uint32_t{0x2C});
    return memory ? This<void*>(addr::cGZMessage_cGZMessage, memory) : nullptr;
}

// The game's lists: a sentinel node, each node next, previous, value.
struct ListNode {
    ListNode* next;
    ListNode* previous;
    void* value;
};

ListNode* NewSentinel()
{
    auto* const node = Cdecl<ListNode*>(addr::SC4_stl_allocate, uint32_t{0xC});
    node->next = node;
    node->previous = node;
    return node;
}

// Puts a value before the first node. The first node is read before the
// new one is made.
void PushFront(ListNode* sentinel, void* value)
{
    ListNode* const first = sentinel->next;
    auto* const node = Cdecl<ListNode*>(addr::SC4_stl_allocate, uint32_t{0xC});
    node->value = value;
    ListNode* const before = first->previous;
    node->next = first;
    node->previous = before;
    before->next = node;
    first->previous = node;
}

// Takes a node out and frees it; returns the one after it.
ListNode* Unlink(ListNode* node)
{
    ListNode* const next = node->next;
    ListNode* const before = node->previous;
    before->next = next;
    next->previous = before;
    Cdecl<void>(addr::SC4_Free, node);
    return next;
}

// The game's string: vtable, the text's begin, end and capacity, a count.
// 0x14 bytes: in both frames that hold one it ends where the frame ends.
struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t references;
};
static_assert(sizeof(GameString) == 0x14);

void DestroyString(GameString& string)
{
    string.vtable = addr::SC4String_Vtable;
    if (string.begin)
        Cdecl<void>(addr::SC4_Free, string.begin);
}

// ---------------------------------------------------------------------
// cSC4AudioEventHandler
// ---------------------------------------------------------------------

// The game's 0x004EF0C0. (sound, channel, unused, value, unused), ret 0x14;
// `this` is the handler. Starts a sound on a channel of the sound service
// and gives the channel a value.
void __fastcall StartChannel(char* self, void*, uint32_t key, uint32_t channel, uint32_t, uint32_t value, uint32_t)
{
    Virtual<void>(At<void*>(self, handler::kSound), 0x2C, kStartOnChannel, key, channel, uint32_t{0}, uint32_t{0});
    Virtual<void>(At<void*>(self, handler::kSound), 0x2C, kSetChannelValue, channel, value, uint32_t{0},
                  uint32_t{0});
}

// The game's 0x004EF100. (channel), ret 4. Stops a channel.
void __fastcall StopChannel(char* self, void*, uint32_t channel)
{
    Virtual<void>(At<void*>(self, handler::kSound), 0x2C, kStopChannel, channel, uint32_t{0}, uint32_t{0},
                  uint32_t{0});
}

// The game's 0x004EF010. (message), ret 4; `this` is the handler. Message
// 0x2A3AD653: plays the music of the region (first number 1) or of the city
// (0), with the fade time the message carries or 4000.
uint8_t __fastcall OnMusicChoice(char* self, void*, void* message)
{
    if (!message)
        return 0;
    void* standard = nullptr;
    if (!Virtual<uint8_t>(message, 0x0, kStandardMessage, &standard) ||
        Virtual<uint32_t>(standard, 0x10) != kMusicChoice) {
        if (standard)
            Release(standard);
        return 0;
    }
    const int32_t which = Virtual<int32_t>(standard, 0x24);
    uint32_t fade = Global<uint32_t>(addr::SC4Audio_MusicFadeTime);
    if (Virtual<uint8_t>(standard, 0x34))
        fade = Virtual<uint32_t>(standard, 0x38);
    if (which == 0)
        This<void>(addr::SC4Audio_F4EE270, self, Global<const char*>(addr::SC4Audio_MusicNames + 4), fade,
                   uint32_t{1});
    else if (which == 1)
        This<void>(addr::SC4Audio_F4EE270, self, Global<const char*>(addr::SC4Audio_MusicNames), fade,
                   uint32_t{1});
    if (standard)
        Release(standard);
    return 1;
}

// The game's 0x004EF120, cdecl: what the sound service's slot 0x80 calls
// for each of its sounds. Asks the sound for interface 0xFD584467 and gives
// its value 0x20 back to it with a zero.
uint8_t __cdecl ResetSound(void* sound)
{
    void* object = nullptr;
    if (Virtual<uint8_t>(sound, 0x0, uint32_t{0xFD584467}, &object)) {
        const uint32_t value = Virtual<uint32_t>(object, 0x10, uint32_t{0x20});
        Virtual<void>(object, 0xC, uint32_t{0x20}, value, uint32_t{0});
    }
    if (object)
        Release(object);
    return 1;
}

// The game's 0x004EF170. (event), ret 4; `this` is the handler. The pointer
// sounds: channel 0x600 loops while a button is held, 0x602 and 0x603 are
// pitched by the pointer's speed, 0x601 is the one-shot that ends it. The
// speed is smoothed with the last one; place, time and speed are kept in
// globals. Returns 0 always.
uint8_t __fastcall PointerSounds(char* self, void*, void* event)
{
    if (!At<void*>(self, handler::kEmitters))
        return 0;
    void* const sound = Cdecl<void*>(addr::SC4_Singleton913C9E);
    if (!sound)
        return 0;
    AddRef(sound);
    if (!event) {
        Release(sound);
        return 0;
    }
    const int32_t x = Virtual<int32_t>(Virtual<void*>(event, 0x18), 0x24);
    const int32_t y = Virtual<int32_t>(Virtual<void*>(event, 0x18), 0x38);
    const uint32_t time = Virtual<uint32_t>(Virtual<void*>(event, 0x18), 0x4C);
    const double elapsed = static_cast<double>(time - Global<uint32_t>(addr::SC4Audio_PointerState + 0x10));
    const float lastY = static_cast<float>(static_cast<double>(Global<int32_t>(addr::SC4Audio_PointerState + 0x8)));
    const float dx =
        static_cast<float>(static_cast<double>(x) -
                           static_cast<double>(Global<int32_t>(addr::SC4Audio_PointerState + 0xC)));
    const double dy = static_cast<double>(y) - static_cast<double>(lastY);
    float speed;
    if (elapsed == 0.0) {
        speed = 0.0f;
    } else {
        const double distance = std::sqrt(dy * dy + static_cast<double>(dx) * static_cast<double>(dx));
        speed = static_cast<float>(distance / (elapsed * static_cast<double>(0.01f)));
    }
    const float capped = (60.0 < static_cast<double>(speed)) ? 60.0f : speed;
    const double mean = (static_cast<double>(Global<float>(addr::SC4Audio_PointerState + 0x4)) +
                         static_cast<double>(capped)) *
                        static_cast<double>(0.5f);
    const float smoothed = static_cast<float>(mean);
    const int32_t pitch = Ftol(mean * static_cast<double>(424.0f) * static_cast<double>(0.016666668f));

    uint8_t& held = Global<uint8_t>(addr::SC4Audio_PointerState);
    uint8_t& pitched = Global<uint8_t>(addr::SC4Audio_PointerState + 0x1);
    switch (Virtual<uint32_t>(event, 0x10)) {
    case kPointerStopAll:
        StopChannel(self, nullptr, 0x601);
        StopChannel(self, nullptr, 0x602);
        StopChannel(self, nullptr, 0x603);
        break;
    case kPointerPitchOff:
        pitched = 0;
        break;
    case kPointerOneShot:
        StartChannel(self, nullptr, 0x6A10AC51, 0x601, 1, 0x400, 0x200);
        break;
    case kPointerMoved:
        if (pitched) {
            const uint32_t value = static_cast<uint32_t>(pitch + 0x258);
            Virtual<void>(At<void*>(self, handler::kSound), 0x2C, kSetChannelValue, uint32_t{0x602}, value,
                          uint32_t{0}, uint32_t{0});
            Virtual<void>(At<void*>(self, handler::kSound), 0x2C, kSetChannelValue, uint32_t{0x603}, value,
                          uint32_t{0}, uint32_t{0});
        }
        break;
    case kPointerPitchOn:
        pitched = 1;
        break;
    case kPointerButton:
        if (Virtual<int32_t>(Virtual<void*>(event, 0x18), 0x24) != 0) {
            if (!held) {
                held = 1;
                StartChannel(self, nullptr, 0x6A10ABCF, 0x600, 0, 0x400, 0x200);
                StartChannel(self, nullptr, 0x0A10AD41, 0x602, 1, static_cast<uint32_t>(pitch + 0x258), 0x200);
                StartChannel(self, nullptr, 0x2AA73C50, 0x603, 1, static_cast<uint32_t>(0x400 - pitch), 0x200);
                StopChannel(self, nullptr, 0x601);
            }
        } else if (held) {
            held = 0;
            StopChannel(self, nullptr, 0x602);
            StopChannel(self, nullptr, 0x603);
            StartChannel(self, nullptr, 0x6A10AC51, 0x601, 1, 0x400, 0x200);
        }
        break;
    default:
        break;
    }

    Global<uint32_t>(addr::SC4Audio_PointerState + 0x4) = Bits(smoothed);
    Global<int32_t>(addr::SC4Audio_PointerState + 0xC) = x;
    Global<int32_t>(addr::SC4Audio_PointerState + 0x8) = y;
    Global<uint32_t>(addr::SC4Audio_PointerState + 0x10) = time;
    Release(sound);
    return 0;
}

// The game's 0x004EF490, main slot 3: an event of the sound service.
// (event), ret 4. The pointer events go to PointerSounds; 0x0BAAB7A4 sets
// value 0x7F of the service's object (its slot 0xAC) to the event's first
// number; 0xAA36D9B6 resets every sound. Returns 1 before Init, else 0.
uint8_t __fastcall OnSoundEvent(char* self, void*, void* event)
{
    if (!At<void*>(self, handler::kSound))
        return 1;
    void* unused = nullptr;
    GetService(0x1AF85BFB, 0x3AF85BB7, &unused);
    void* standard = nullptr;
    Virtual<uint8_t>(event, 0x0, kStandardMessage, &standard);
    switch (Virtual<uint32_t>(event, 0x10)) {
    case kPointerStopAll:
    case kPointerOneShot:
    case kPointerPitchOff:
    case kPointerMoved:
    case kPointerPitchOn:
    case kPointerButton:
        PointerSounds(self, nullptr, event);
        break;
    case 0x0BAAB7A4: {
        const uint32_t value = Virtual<uint32_t>(Virtual<void*>(event, 0x18), 0x24);
        void* object = nullptr;
        void* const sound = Cdecl<void*>(addr::SC4_Singleton913C9E);
        void** const holder = This<void**>(addr::SC4RefPtr_Reset, &object);
        Virtual<void>(sound, 0xAC, holder);
        Virtual<void>(object, 0x2C, uint32_t{0x7F}, value);
        if (object)
            Release(object);
        break;
    }
    case 0xAA36D9B6:
        Virtual<void>(At<void*>(self, handler::kSound), 0x80, reinterpret_cast<void*>(addr::SC4Audio_F4EF120),
                      uint32_t{0}, uint32_t{0});
        break;
    default:
        break;
    }
    if (standard)
        Release(standard);
    This<void>(addr::SC4Audio_F4EDF00, &unused);
    return 0;
}

// The game's 0x004EF5D0. (message), ret 4; `this` is the handler. Message
// 0xFA0C6390, a pointer message: a button event (kinds 7, 8, 9) outside the
// newest window of the list makes the sound 0xAA62C9D5. With an empty list
// the window is whatever the sentinel node holds, as in the original.
uint8_t __fastcall OnClickOutside(char* self, void*, void* message)
{
    void* const window = At<ListNode*>(self, handler::kWindows)->next->value;
    if (window)
        AddRef(window);
    void* pointer = nullptr;
    if (Virtual<uint8_t>(message, 0x0, uint32_t{0x1A0C615F}, &pointer) &&
        (Virtual<int32_t>(pointer, 0x24) == 7 || Virtual<int32_t>(pointer, 0x24) == 9 ||
         Virtual<int32_t>(pointer, 0x24) == 8)) {
        int32_t area[4];
        int32_t first[2];
        int32_t second[2];
        Virtual<void>(window, 0xBC, &area[0]);
        const int32_t y = Virtual<int32_t*>(pointer, 0x28, &first[0])[1];
        const int32_t x = Virtual<int32_t*>(pointer, 0x28, &second[0])[0];
        if (x < area[0] || y < area[1] || x > area[2] || y > area[3]) {
            void* const sound = Global<void*>(addr::SC4_Sound43CB8);
            if (!sound) {
                if (pointer)
                    Release(pointer);
                Release(window);
                return 0;
            }
            AddRef(sound);
            Virtual<void>(sound, 0x2C, uint32_t{1}, uint32_t{0xAA62C9D5}, uint32_t{0}, uint32_t{0}, uint32_t{0});
            Release(sound);
        }
    }
    if (pointer)
        Release(pointer);
    if (window)
        Release(window);
    return 1;
}

// The game's 0x004EF6F0, the constructor; returns this.
char* __fastcall HandlerConstruct(char* self, void*)
{
    At<std::uintptr_t>(self, handler::kTarget) = addr::SC4Audio_OldTargetBaseVtable;
    At<std::uintptr_t>(self, handler::kTarget2) = addr::cSC4TrafficSimulator_VtableBase4;
    At<std::uintptr_t>(self, handler::kService) = addr::SC4Audio_ServiceBaseVtable;
    At<std::uintptr_t>(self, 0x0) = addr::SC4Audio_EventHandlerVtable;
    At<std::uintptr_t>(self, handler::kTarget) = addr::SC4Audio_EventHandlerOldTargetVtable;
    At<std::uintptr_t>(self, handler::kTarget2) = addr::SC4Audio_EventHandlerTargetVtable;
    At<std::uintptr_t>(self, handler::kService) = addr::SC4Audio_EventHandlerServiceVtable;
    At<void*>(self, handler::kMusic) = nullptr;
    GetService(0x1D07B39F, 0xE0510260, &At<void*>(self, handler::kMusic));
    At<void*>(self, handler::kSound) = nullptr;
    GetService(kSoundService, kSoundInterface, &At<void*>(self, handler::kSound));
    At<void*>(self, handler::kEmitters) = nullptr;
    At<uint8_t>(self, handler::kStarted) = 0;
    At<uint32_t>(self, 0x20) = 0;
    At<uint32_t>(self, 0x24) = 0;
    At<uint32_t>(self, 0x28) = 0;
    At<ListNode*>(self, handler::kWindows) = nullptr;
    At<ListNode*>(self, handler::kWindows) = NewSentinel();
    This<void*>(addr::SC4Stopwatch_Construct, self + handler::kWatch, uint32_t{4});
    At<uint32_t>(self, 0x48) = 0;
    return self;
}

// The game's 0x004EF7A0, 0x004EF7C0 and 0x004EF7E0: QueryInterface of the
// +4, +8 and +0xC parts.
uint8_t __fastcall HandlerQueryInterface4(char* self, void*, uint32_t id, void** out)
{
    return This<uint8_t>(addr::SC4Audio_F4EDE80, self - 0x4, id, out);
}

uint8_t __fastcall HandlerQueryInterface8(char* self, void*, uint32_t id, void** out)
{
    return This<uint8_t>(addr::SC4Audio_F4EDE80, self - 0x8, id, out);
}

uint8_t __fastcall HandlerQueryInterfaceC(char* self, void*, uint32_t id, void** out)
{
    return This<uint8_t>(addr::SC4Audio_F4EDE80, self - 0xC, id, out);
}

// The game's 0x004EF7B0, 0x004EF7D0 and 0x004EF800: Release of the three
// parts.
uint32_t __fastcall HandlerRelease4(char* self, void*)
{
    return This<uint32_t>(addr::SC4Audio_F4EDE60, self - 0x4);
}

uint32_t __fastcall HandlerRelease8(char* self, void*)
{
    return This<uint32_t>(addr::SC4Audio_F4EDE60, self - 0x8);
}

uint32_t __fastcall HandlerReleaseC(char* self, void*)
{
    return This<uint32_t>(addr::SC4Audio_F4EDE60, self - 0xC);
}

// Posts message 0x2A3AD653 with a first number: the music choice.
void PostMusicChoice(uint32_t which)
{
    void* const message = NewMessage();
    AddRef(message);
    Virtual<void>(message, 0x14, kMusicChoice);
    Virtual<void>(message, 0x2C, which);
    Virtual<uint8_t>(Global<void*>(addr::SC4_MessageServer), 0x10, message, uint32_t{0});
    Release(message);
}

// The game's 0x004EF810, DoMessage of the +8 part. (message), ret 4; `this`
// is object + 8. Returns 0 for no message, what OnClickOutside returns for
// 0xFA0C6390, else 1.
uint8_t __fastcall HandlerDoMessage(char* self, void*, void* message)
{
    if (!message)
        return 0;
    char* const object = self - handler::kTarget2;
    void*& music = At<void*>(object, handler::kMusic);
    switch (Virtual<uint32_t>(message, 0x10)) {
    case 0x26D31EC1:
    case 0x26D31EC2:
        This<uint8_t>(addr::SC4Audio_F4EE970, object, message);
        return 1;
    case 0x0A611B34: {
        // Play a station: 0 the city's, 1 the region's, anything else none.
        const char* name = nullptr;
        const int32_t which = Virtual<int32_t>(message, 0x24);
        if (which == 0)
            name = Global<const char*>(addr::SC4Audio_MusicNames + 4);
        else if (which == 1)
            name = Global<const char*>(addr::SC4Audio_MusicNames);
        const uint32_t track = Virtual<uint32_t>(message, 0x3C);
        uint32_t third = 0;
        if (Virtual<uint8_t>(message, 0x48))
            third = Virtual<uint32_t>(message, 0x4C);
        if (!music)
            return 1;
        void* const station = Virtual<void*>(music, 0x2C, name);
        if (!station)
            return 1;
        Virtual<void>(station, 0x38, track);
        Virtual<void>(station, 0x30, uint32_t{third == 0 ? 2u : 1u});
        return 1;
    }
    case 0x26D31EC4:
        PostMusicChoice(0);
        return 1;
    case 0x26D31EC5:
        PostMusicChoice(1);
        return 1;
    case kMusicChoice:
        OnMusicChoice(object, nullptr, message);
        return 1;
    case 0x2A611B51:
        if (music)
            Virtual<void>(music, 0x7C);
        return 1;
    case 0x4348B111: {
        void* const preferences = Virtual<void*>(Global<void*>(addr::SC4_Service43C94), 0x98);
        if (At<uint32_t>(preferences, 0x10) == 0)
            return 1;
        void* standard = nullptr;
        void** const holder = This<void**>(addr::SC4RefPtr_Reset, &standard);
        if (Virtual<uint8_t>(message, 0x0, kStandardMessage, holder)) {
            if (void* const player = music) {
                // The whole number goes on the stack with its low byte made 0
                // or 1.
                const uint32_t number = Virtual<uint32_t>(standard, 0x24);
                Virtual<void>(player, 0x5C, (number & 0xFFFFFF00u) | (number != 0 ? 1u : 0u));
            }
        }
        if (standard)
            Release(standard);
        return 1;
    }
    case 0x49E64801:
    case 0x8A385DD8:
    case 0xA9E3B6E3:
    case 0xAA396960:
    case 0xEA3ADA33:
        This<uint8_t>(addr::SC4Audio_F4EE560, object, message);
        return 1;
    case 0x49F10111:
    case 0x894671F6:
    case 0xE94671EB:
    case 0xC9F1011A:
        This<uint8_t>(addr::SC4Audio_F4EE300, object, message);
        return 1;
    case 0x4A7FB7E9: {
        const uint32_t value = Virtual<uint32_t>(message, 0x24);
        void* const sound = Cdecl<void*>(addr::SC4_Singleton913C9E);
        Virtual<void>(sound, 0x1C, uint32_t{0x0BAAB7A4}, value, uint32_t{0}, uint32_t{0}, uint32_t{0});
        return 1;
    }
    case 0x89F10866:
    case 0xA9F10E45:
        This<uint8_t>(addr::SC4Audio_F4EEC10, object, message);
        return 1;
    case 0xAA611B6E: {
        void* const thing = Virtual<void*>(message, 0x28);
        if (!thing)
            return 1;
        if (music)
            Virtual<void>(music, 0x1C, thing);
        Release(thing);
        return 1;
    }
    case 0xAA650860: {
        // Music volume (second number) and on or off (first number 1 = on).
        if (!music)
            return 1;
        if (Virtual<uint8_t>(message, 0x34)) {
            const uint32_t volume = Virtual<uint32_t>(message, 0x38);
            Virtual<void>(music, 0x40, volume);
        }
        if (!Virtual<uint8_t>(message, 0x20))
            return 1;
        const uint8_t on = Virtual<int32_t>(message, 0x24) == 1 ? 1 : 0;
        const uint8_t playing = Virtual<uint8_t>(music, 0x60);
        if (on ? playing != 0 : playing == 0)
            return 1;
        // The byte is stored over the low byte of the message argument and
        // the whole dword is pushed.
        const auto pushed = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(message));
        Virtual<void>(music, 0x5C, (pushed & 0xFFFFFF00u) | on);
        if (!on)
            return 1;
        Virtual<void>(music, 0x20, uint32_t{0});
        return 1;
    }
    case 0xAA650861: {
        const uint32_t number = Virtual<uint32_t>(message, 0x24);
        if (!music)
            return 1;
        Virtual<void>(music, 0x78, (number & 0xFFFFFF00u) | (number != 0 ? 1u : 0u));
        return 1;
    }
    case kClickOutside:
        return OnClickOutside(object, nullptr, message);
    default:
        return 1;
    }
}

// The game's 0x004EFC60, main slot 4: the destructor, freeing the object
// when bit 0 of the argument is set. ret 4.
char* __fastcall HandlerDestroy(char* self, void*, uint32_t flags)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4Audio_EventHandlerVtable;
    At<std::uintptr_t>(self, handler::kTarget) = addr::SC4Audio_EventHandlerOldTargetVtable;
    At<std::uintptr_t>(self, handler::kTarget2) = addr::SC4Audio_EventHandlerTargetVtable;
    At<std::uintptr_t>(self, handler::kService) = addr::SC4Audio_EventHandlerServiceVtable;
    This<void>(addr::SC4Stopwatch_Destruct, self + handler::kWatch);
    This<void>(addr::SC4PairList_Destroy, self + handler::kWindows);
    if (void* const emitters = At<void*>(self, handler::kEmitters))
        Release(emitters);
    This<void>(addr::SC4RefPtr_DestructGuarded, self + handler::kSound);
    This<void>(addr::SC4Audio_F4EDF00, self + handler::kMusic);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// Asks the window system (the singleton 0x00913C1A, its slot 0x38, then
// interface 0x3AC7A210 and its slot 0xC) for its message server and adds or
// removes the handler's target for the click message.
void WatchClicks(void* target, bool on)
{
    void* const system = Virtual<void*>(Cdecl<void*>(addr::SC4_Singleton913C1A), 0x38);
    if (!system)
        return;
    AddRef(system);
    void* manager = nullptr;
    if (Virtual<uint8_t>(system, 0x0, uint32_t{0x3AC7A210}, &manager)) {
        if (void* const server = Virtual<void*>(manager, 0xC))
            Virtual<uint8_t>(server, on ? 0x14 : 0x18, target, kClickOutside);
    }
    if (manager)
        Release(manager);
    Release(system);
}

// The game's 0x004EFCC0. (old style message: a dword type, then three
// dwords), ret 4; `this` is the handler. 0x62BA7B42 puts the message's
// second dword, a window, at the front of the list; 0x62BA7B43 takes the
// front one off. The click message is watched while the list has members.
void __fastcall OnWindowMessage(char* self, void*, uint32_t* message)
{
    void* const server = Global<void*>(addr::SC4_MessageServer);
    if (server)
        AddRef(server);
    if (message[0] == kWindowOpened) {
        ListNode* const sentinel = At<ListNode*>(self, handler::kWindows);
        if (sentinel->next == sentinel)
            WatchClicks(self ? self + handler::kTarget2 : nullptr, true);
        void* const window = reinterpret_cast<void*>(static_cast<std::uintptr_t>(message[2]));
        AddRef(window);
        PushFront(At<ListNode*>(self, handler::kWindows), window);
    } else if (message[0] == kWindowClosed) {
        ListNode* const first = At<ListNode*>(self, handler::kWindows)->next;
        void* const window = first->value;
        Unlink(first);
        Release(window);
        ListNode* const sentinel = At<ListNode*>(self, handler::kWindows);
        if (sentinel->next == sentinel)
            WatchClicks(self + handler::kTarget2, false);
    }
    if (server)
        Release(server);
}

// The game's 0x004EFE50, DoMessage of the +4 part. (old style message),
// ret 4; `this` is object + 4.
uint8_t __fastcall HandlerDoOldMessage(char* self, void*, uint32_t* message)
{
    if (message[0] >= kWindowOpened && message[0] <= kWindowClosed)
        OnWindowMessage(self - handler::kTarget, nullptr, message);
    return 1;
}

// The game's 0x004EFE80, Init: slot 3 of the +0xC part; `this` is object +
// 0xC. Hooks the object into the sound service, takes the ambience manager,
// gives the music player its folder, asks for the messages, sets up the
// sound preferences and starts the music player with them.
uint8_t __fastcall HandlerInit(char* self, void*)
{
    char* const object = self - handler::kService;
    if (At<uint8_t>(object, handler::kStarted))
        return 1;
    void* sound = nullptr;
    GetService(kSoundService, kSoundInterface, &sound);
    if (!sound) {
        This<void>(addr::SC4RefPtr_DestructGuarded, &sound);
        return 0;
    }
    Virtual<void>(sound, 0x3C, object);
    void* emitters = nullptr;
    GetService(kEmitterService, kEmitterInterface, &emitters);
    if (!emitters) {
        This<void>(addr::SC4Audio_F4E8440, &emitters);
        This<void>(addr::SC4RefPtr_DestructGuarded, &sound);
        return 0;
    }
    This<void>(addr::SC4RefPtr_Assign, object + handler::kEmitters, emitters);

    void* folders = nullptr;
    GetService(0x2073215D, 0xE0203660, &folders);
    if (folders) {
        GameString folder;
        const char* const base = Virtual<const char*>(folders, 0x1C);
        This<void*>(addr::SC4String_FromText, &folder, base);
        char allocator;
        This<void*>(addr::SC4String_AppendRange, &folder.begin, Global<const char*>(addr::SC4Audio_MusicFolder),
                    Global<const char*>(addr::SC4Audio_MusicFolder + 4), &allocator);
        if (At<void*>(object, handler::kMusic)) {
            Virtual<void>(At<void*>(object, handler::kMusic), 0x74, &folder);
            Virtual<void>(At<void*>(object, handler::kMusic), 0x5C, uint32_t{0});
            Virtual<void>(At<void*>(object, handler::kMusic), 0x40, uint32_t{0});
        }
        DestroyString(folder);
    }

    void* const oldServer = Global<void*>(addr::SC4_ServiceB43CC8);
    if (oldServer)
        AddRef(oldServer);
    char* const oldTarget = object ? object + handler::kTarget : nullptr;
    Virtual<uint8_t>(oldServer, 0x14, oldTarget, kWindowOpened);
    Virtual<uint8_t>(oldServer, 0x14, oldTarget, kWindowClosed);

    void* server = nullptr;
    GetService(kMessageServerService, kMessageServerInterface, &server);
    char* const target = object ? object + handler::kTarget2 : nullptr;
    static constexpr uint32_t kMessages[] = {
        0x26D31EC1, 0x26D31EC2, 0xC53D10AA, 0x26D31EC4, 0x26D31EC5, 0x4A7FB7E9, kMusicChoice,
        0x0A611B34, 0x2A611B51, 0xAA611B6E, 0xAA650860, 0xAA650861, 0x4348B111,
    };
    for (const uint32_t id : kMessages)
        Virtual<uint8_t>(server, 0x14, target, id);

    Cdecl<void>(addr::SC4Audio_F4F2D40);
    char* const preferences = Virtual<char*>(Global<void*>(addr::SC4_Service43C94), 0x98);
    char* const audio = preferences + 0x4;
    if (At<uint8_t>(preferences, 0x6A)) {
        At<uint8_t>(audio, 0x66) = 0;
        At<uint32_t>(audio, 0x8) = Cdecl<uint32_t>(addr::SC4Audio_F4F1F90, uint32_t{0x2D0}, uint32_t{1});
        At<uint32_t>(audio, 0x4) = Cdecl<uint32_t>(addr::SC4Audio_F4F1F90, uint32_t{0x400}, uint32_t{0});
        At<uint32_t>(audio, 0x24) |= 1;
        Cdecl<void>(addr::SC4Audio_F4F2580);
    } else {
        At<uint32_t>(audio, 0x24) &= 0xFFFFFFFEu;
    }
    Cdecl<void>(addr::SC4Audio_F4F3410, uint32_t{0});
    Cdecl<void>(addr::SC4Audio_F4F3410, uint32_t{1});
    if (At<void*>(object, handler::kMusic)) {
        Virtual<void>(At<void*>(object, handler::kMusic), 0x78, uint32_t{1});
        if (At<void*>(object, handler::kMusic)) {
            const uint32_t on = At<uint32_t>(audio, 0xC);
            const uint32_t volume = At<uint32_t>(audio, 0x8);
            Virtual<void>(At<void*>(object, handler::kMusic), 0x5C, (on & 0xFFFFFF00u) | (on != 0 ? 1u : 0u));
            Virtual<void>(At<void*>(object, handler::kMusic), 0x40, volume);
        }
    }
    At<uint8_t>(object, handler::kStarted) = 1;
    This<void>(addr::SC4RefPtr_Destruct, &server);
    Release(oldServer);
    This<void>(addr::SC4Audio_RefPtrDestruct4DE460, &folders);
    This<void>(addr::SC4Audio_F4E8440, &emitters);
    This<void>(addr::SC4RefPtr_DestructGuarded, &sound);
    return 1;
}

// ---------------------------------------------------------------------
// cSC4AudioEventHandlerDisasterFire
// ---------------------------------------------------------------------

char* __fastcall FireDestroy(char* self, void*, uint32_t flags);

// The game's 0x004F0250, Release. Frees the object through the filter
// part's slot 6 when the count is 1 or less.
uint32_t __fastcall FireRelease(char* self, void*)
{
    const int32_t count = At<int32_t>(self, fire::kReferences);
    if (count > 1) {
        At<int32_t>(self, fire::kReferences) = count - 1;
        return static_cast<uint32_t>(count - 1);
    }
    Virtual<void*>(self + fire::kFilter, 0x18, uint32_t{1});
    return 0;
}

// The game's 0x004F0270, QueryInterface. (interface, out), ret 8.
uint8_t __fastcall FireQueryInterface(char* self, void*, uint32_t id, void** out)
{
    if (id == kServiceInterface || id == 1) {
        *out = self ? self + fire::kService : nullptr;
        Virtual<uint32_t>(self, 0x4);
        return 1;
    }
    if (id == kMessageTargetInterface) {
        *out = self;
        Virtual<uint32_t>(self, 0x4);
        return 1;
    }
    if (id == kSoundHookInterface) {
        *out = self ? self + fire::kHook : nullptr;
        Virtual<uint32_t>(self, 0x4);
        return 1;
    }
    return 0;
}

// The thunks of the other parts: 0x004F02F0, 0x004F0310, 0x004F0340 and
// 0x004F0370 (QueryInterface), 0x004F0300, 0x004F0320, 0x004F0360 and
// 0x004F0390 (Release), 0x004F0350 and 0x004F0380 (AddRef, the game's
// shared 0x004F0240) and 0x004F0330 (the filter part's destructor).
uint8_t __fastcall FireQueryInterface4(char* self, void*, uint32_t id, void** out)
{
    return FireQueryInterface(self - 0x4, nullptr, id, out);
}

uint8_t __fastcall FireQueryInterface8(char* self, void*, uint32_t id, void** out)
{
    return FireQueryInterface(self - 0x8, nullptr, id, out);
}

uint8_t __fastcall FireQueryInterfaceC(char* self, void*, uint32_t id, void** out)
{
    return FireQueryInterface(self - 0xC, nullptr, id, out);
}

uint8_t __fastcall FireQueryInterface14(char* self, void*, uint32_t id, void** out)
{
    return FireQueryInterface(self - 0x14, nullptr, id, out);
}

uint32_t __fastcall FireRelease4(char* self, void*)
{
    return FireRelease(self - 0x4, nullptr);
}

uint32_t __fastcall FireRelease8(char* self, void*)
{
    return FireRelease(self - 0x8, nullptr);
}

uint32_t __fastcall FireReleaseC(char* self, void*)
{
    return FireRelease(self - 0xC, nullptr);
}

uint32_t __fastcall FireRelease14(char* self, void*)
{
    return FireRelease(self - 0x14, nullptr);
}

uint32_t __fastcall FireAddRefC(char* self, void*)
{
    return This<uint32_t>(addr::SC4Audio_F4F0240, self - 0xC);
}

uint32_t __fastcall FireAddRef14(char* self, void*)
{
    return This<uint32_t>(addr::SC4Audio_F4F0240, self - 0x14);
}

char* __fastcall FireDestroy8(char* self, void*, uint32_t flags)
{
    return FireDestroy(self - 0x8, nullptr, flags);
}

// The game's 0x004F03A0, Shutdown: slot 4 of the +4 part; `this` is object
// + 4.
uint8_t __fastcall FireShutdown(char* self, void*)
{
    char* const object = self - fire::kService;
    if (!At<uint8_t>(object, fire::kStarted))
        return 1;
    void* server = nullptr;
    GetService(kMessageServerService, kMessageServerInterface, &server);
    if (server) {
        Virtual<uint8_t>(server, 0x18, object, kCityInited);
        Virtual<uint8_t>(server, 0x18, object, kOccupantInserted);
        Virtual<uint8_t>(server, 0x18, object, kOccupantRemoved);
    }
    if (void* const emitters = At<void*>(object, fire::kEmitters)) {
        At<void*>(object, fire::kEmitters) = nullptr;
        Release(emitters);
    }
    At<uint8_t>(object, fire::kStarted) = 0;
    This<void>(addr::SC4RefPtr_Destruct, &server);
    return 1;
}

// The game's 0x004F0440, slot 3 of the filter part: is the occupant a fire?
// (occupant), ret 4; `this` (object + 8) is not used.
uint8_t __fastcall FireFilter(char*, void*, void* occupant)
{
    void* fireOccupant = nullptr;
    uint8_t answer = 0;
    if (occupant && Virtual<uint8_t>(occupant, 0x0, uint32_t{0x69D25C60}, &fireOccupant))
        answer = 1;
    if (fireOccupant)
        Release(fireOccupant);
    return answer;
}

// The game's 0x004F0480. (occupant), ret 4; `this` is the object. Gives the
// ambience manager the occupant and its place under the fire group.
void __fastcall FireAdd(char* self, void*, void* occupant)
{
    if (!occupant)
        return;
    uint32_t place[3];
    place[0] = place[1] = place[2] = Global<uint32_t>(addr::SC4Audio_FirePlaceDefault);
    Virtual<void>(occupant, 0x20, &place[0]);
    Virtual<uint8_t>(At<void*>(self, fire::kEmitters), 0x34, kFireGroup, occupant, &place[0], uint32_t{0});
}

// The game's 0x004F04D0, cdecl: what the occupant manager calls for every
// occupant the filter takes. (occupant, the handler).
uint8_t __cdecl FireEachOccupant(void* occupant, char* handlerObject)
{
    if (!occupant || !handlerObject)
        return 0;
    AddRef(handlerObject);
    FireAdd(handlerObject, nullptr, occupant);
    Release(handlerObject);
    return 1;
}

// The game's 0x004F0510, the constructor; returns this.
char* __fastcall FireConstruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0x0) = addr::cSC4TrafficSimulator_VtableBase4;
    At<std::uintptr_t>(self, fire::kService) = addr::SC4Audio_ServiceBaseVtable;
    This<void*>(addr::SC4_OccupantFilterBase, self + fire::kFilter);
    At<std::uintptr_t>(self, fire::kHook) = addr::cSC4AudioAEMSEventSystem_BaseVtable0;
    At<uint8_t>(self, fire::kStarted) = 0;
    At<uint32_t>(self, fire::kReferences) = 0;
    At<std::uintptr_t>(self, 0x0) = addr::SC4Audio_FireHandlerVtable;
    At<std::uintptr_t>(self, fire::kService) = addr::SC4Audio_FireHandlerServiceVtable;
    At<std::uintptr_t>(self, fire::kFilter) = addr::SC4Audio_FireHandlerFilterVtable;
    At<std::uintptr_t>(self, fire::kFilter2) = addr::SC4Audio_FireHandlerFilter2Vtable;
    At<std::uintptr_t>(self, fire::kHook) = addr::SC4Audio_FireHandlerHookVtable;
    At<uint32_t>(self, fire::kEmitters) = 0;
    At<uint32_t>(self, 0x24) = 0;
    At<uint32_t>(self, 0x28) = 0;
    At<uint32_t>(self, 0x2C) = 0;
    At<uint32_t>(self, 0x30) = 0;
    return self;
}

// The game's 0x004F0570: the destructor, freeing the object when bit 0 of
// the argument is set. ret 4.
char* __fastcall FireDestroy(char* self, void*, uint32_t flags)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4Audio_FireHandlerVtable;
    At<std::uintptr_t>(self, fire::kService) = addr::SC4Audio_FireHandlerServiceVtable;
    At<std::uintptr_t>(self, fire::kFilter) = addr::SC4Audio_FireHandlerFilterVtable;
    At<std::uintptr_t>(self, fire::kFilter2) = addr::SC4Audio_FireHandlerFilter2Vtable;
    At<std::uintptr_t>(self, fire::kHook) = addr::SC4Audio_FireHandlerHookVtable;
    if (void* const emitters = At<void*>(self, fire::kEmitters))
        Release(emitters);
    This<void>(addr::SC4OccupantFilterBase_Destroy, self + fire::kFilter);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// The game's 0x004F05D0, DoMessage. (message), ret 4. When the city is
// ready every fire occupant is reported; after that each fire that is put
// in or taken out.
uint8_t __fastcall FireDoMessage(char* self, void*, void* message)
{
    if (!message)
        return 1;
    void* standard = nullptr;
    if (Virtual<uint8_t>(message, 0x0, kStandardMessage, &standard)) {
        void* const subject = Virtual<void*>(standard, 0x80);
        if (subject)
            AddRef(subject);
        const uint32_t type = Virtual<uint32_t>(message, 0x10);
        if (type == kCityInited) {
            Virtual<uint8_t>(Global<void*>(addr::SC4_NetworkService), 0xB0,
                             reinterpret_cast<void*>(addr::SC4Audio_F4F04D0), self, uint32_t{0}, uint32_t{0},
                             self ? self + fire::kFilter : nullptr);
        } else if ((type == kOccupantInserted || type == kOccupantRemoved) && subject) {
            void* occupant = nullptr;
            void** const holder = This<void**>(addr::SC4RefPtr_Reset, &occupant);
            if (Virtual<uint8_t>(subject, 0x0, uint32_t{0x26BB767E}, holder) &&
                Virtual<uint8_t>(self + fire::kFilter, 0xC, occupant)) {
                uint32_t place[3];
                place[0] = place[1] = place[2] = Global<uint32_t>(addr::SC4Audio_FirePlaceDefault);
                Virtual<void>(occupant, 0x20, &place[0]);
                const uint32_t again = Virtual<uint32_t>(message, 0x10);
                if (again == kOccupantInserted)
                    Virtual<uint8_t>(At<void*>(self, fire::kEmitters), 0x34, kFireGroup, occupant, &place[0],
                                     uint32_t{0});
                else if (again == kOccupantRemoved)
                    Virtual<uint8_t>(At<void*>(self, fire::kEmitters), 0x38, kFireGroup, occupant);
            }
            if (occupant)
                Release(occupant);
        }
        if (subject)
            Release(subject);
    }
    if (standard)
        Release(standard);
    return 1;
}

// The game's 0x004F0740, Init: slot 3 of the +4 part; `this` is object + 4.
// Asks for the three messages, takes the ambience manager and makes the
// group "Fires" in it with three sounds by the number of fires near each
// other: 1 to 9, 10 to 49, 50 and more.
uint8_t __fastcall FireInit(char* self, void*)
{
    char* const object = self - fire::kService;
    if (At<uint8_t>(object, fire::kStarted))
        return 1;
    void* server = nullptr;
    GetService(kMessageServerService, kMessageServerInterface, &server);
    if (!server) {
        This<void>(addr::SC4RefPtr_Destruct, &server);
        return 0;
    }
    Virtual<uint8_t>(server, 0x14, object, kOccupantInserted);
    Virtual<uint8_t>(server, 0x14, object, kOccupantRemoved);
    Virtual<uint8_t>(server, 0x14, object, kCityInited);

    void* emitters = nullptr;
    GetService(kEmitterService, kEmitterInterface, &emitters);
    void* const before = At<void*>(object, fire::kEmitters);
    if (emitters != before) {
        if (emitters)
            AddRef(emitters);
        At<void*>(object, fire::kEmitters) = emitters;
        if (before)
            Release(before);
    }
    void* const manager = At<void*>(object, fire::kEmitters);
    bool failed = !manager;
    if (!failed) {
        GameString name;
        void* const text = This<void*>(addr::SC4String_FromText, &name, "Fires");
        failed = Virtual<uint8_t>(manager, 0x14, text, kFireGroup, uint32_t{0}, 100.0f) == 0;
        DestroyString(name);
    }
    if (failed) {
        This<void>(addr::SC4Audio_F4E8440, &emitters);
        This<void>(addr::SC4RefPtr_Destruct, &server);
        return 0;
    }
    Virtual<void>(At<void*>(object, fire::kEmitters), 0x40, kFireGroup, uint32_t{0x29DC19EF}, uint32_t{1},
                  uint32_t{0}, uint32_t{1}, uint32_t{9});
    Virtual<void>(At<void*>(object, fire::kEmitters), 0x40, kFireGroup, uint32_t{0x09DC19A2}, uint32_t{1},
                  uint32_t{0}, uint32_t{0xA}, uint32_t{0x31});
    Virtual<void>(At<void*>(object, fire::kEmitters), 0x40, kFireGroup, uint32_t{0x09DC1982}, uint32_t{1},
                  uint32_t{0}, uint32_t{0x32}, uint32_t{0x7FFFFFFF});
    At<uint8_t>(object, fire::kStarted) = 1;
    This<void>(addr::SC4Audio_F4E8440, &emitters);
    This<void>(addr::SC4RefPtr_Destruct, &server);
    return 1;
}

// ---------------------------------------------------------------------
// The cluster set
// ---------------------------------------------------------------------

struct HashNode {
    HashNode* next;
    uint32_t key;
    char* value;
};

// What SC4Uint32HashMap::EraseAt takes: the node and its map.
struct HashIterator {
    HashNode* node;
    char* map;
};

// The inlined search of the map: the key's node, or null.
HashNode* FindNode(char* hashMap, uint32_t key)
{
    HashNode** const buckets = At<HashNode**>(hashMap, map::kBuckets);
    const uint32_t count = static_cast<uint32_t>(
        (At<std::intptr_t>(hashMap, map::kBucketsEnd) - reinterpret_cast<std::intptr_t>(buckets)) >> 2);
    HashNode* node = buckets[key % count];
    while (node && node->key != key)
        node = node->next;
    return node;
}

// The distance of two places, as the original works it out on the x87
// stack: differences and products unrounded, the sum z, y, x.
double Distance(const float* a, const float* b)
{
    const double dx = static_cast<double>(a[0]) - static_cast<double>(b[0]);
    const double dy = static_cast<double>(a[1]) - static_cast<double>(b[1]);
    const double dz = static_cast<double>(a[2]) - static_cast<double>(b[2]);
    return std::sqrt(dz * dz + dy * dy + dx * dx);
}

// One more point in a cluster; the peak follows.
void CountPoint(char* group)
{
    const uint32_t count = ++At<uint32_t>(group, cluster::kCount);
    if (At<uint32_t>(group, cluster::kPeak) < count)
        At<uint32_t>(group, cluster::kPeak) = count;
}

// A new, empty cluster at a place. Null when there is no memory, which the
// original's callers do not check either.
char* NewCluster(const void* place, uint32_t radius)
{
    char* const group = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0x20});
    if (!group)
        return nullptr;
    At<ListNode*>(group, cluster::kPoints) = nullptr;
    At<ListNode*>(group, cluster::kPoints) = NewSentinel();
    At<uint32_t>(group, cluster::kCount) = 0;
    At<uint32_t>(group, cluster::kPeak) = 0;
    std::memcpy(group + cluster::kPlace, place, 3 * sizeof(uint32_t));
    At<uint32_t>(group, cluster::kRadius) = radius;
    At<uint8_t>(group, cluster::kTaken) = 0;
    return group;
}

// The inlined end of a cluster: its list's nodes, the sentinel, the cluster.
void FreeCluster(char* group)
{
    ListNode* node = At<ListNode*>(group, cluster::kPoints)->next;
    while (node != At<ListNode*>(group, cluster::kPoints)) {
        ListNode* const next = node->next;
        Cdecl<void>(addr::SC4_Free, node);
        node = next;
    }
    At<ListNode*>(group, cluster::kPoints)->next = At<ListNode*>(group, cluster::kPoints);
    At<ListNode*>(group, cluster::kPoints)->previous = At<ListNode*>(group, cluster::kPoints);
    if (ListNode* const sentinel = At<ListNode*>(group, cluster::kPoints))
        Cdecl<void>(addr::SC4_Free, sentinel);
    Cdecl<void>(addr::SC4_operator_delete, group);
}

// The game's 0x004F08F0. (point), ret 4; `this` is a cluster. Takes a point
// out of the cluster's list and clears the point's cluster.
void __fastcall ClusterRemovePoint(char* self, void*, char* record)
{
    ListNode* const sentinel = At<ListNode*>(self, cluster::kPoints);
    for (ListNode* node = sentinel->next; node != sentinel; node = node->next) {
        if (node->value != record)
            continue;
        At<char*>(record, point::kCluster) = nullptr;
        Unlink(node);
        --At<uint32_t>(self, cluster::kCount);
        return;
    }
}

// The game's 0x004F0A30. No arguments; `this` is the set. Frees the
// clusters that have no points.
void __fastcall SetDropEmptyClusters(char* self, void*)
{
    ListNode* const sentinel = At<ListNode*>(self, set::kClusters);
    ListNode* node = sentinel->next;
    while (node != sentinel) {
        char* const group = static_cast<char*>(node->value);
        if (At<uint32_t>(group, cluster::kCount) != 0) {
            node = node->next;
            continue;
        }
        node = Unlink(node);
        FreeCluster(group);
        --At<uint32_t>(self, set::kClusterCount);
    }
}

// The game's 0x004F0AC0. (key), ret 4; `this` is the set. Takes a point out
// of the map and of its cluster and frees it; false when the key is not
// there. A point that began its own cluster has no cluster recorded, and
// the original goes through that null pointer here; so does this.
uint8_t __fastcall SetRemovePoint(char* self, void*, uint32_t key)
{
    char* const hashMap = self + set::kMap;
    HashNode* const node = FindNode(hashMap, key);
    if (!node)
        return 0;
    char* const record = node->value;
    HashIterator at = {node, hashMap};
    This<void>(addr::SC4Uint32HashMap_EraseAt, hashMap, &at);
    ClusterRemovePoint(At<char*>(record, point::kCluster), nullptr, record);
    Cdecl<void>(addr::SC4_operator_delete, record);
    return 1;
}

// The game's 0x004F0B40. (area, list, count out, total out, use the area),
// ret 0x14; `this` is the set. Puts the place of every cluster (inside the
// area of four floats: least x, least z, most x, most z, when asked) at
// the front of a list of 0x14-byte nodes, counts them and adds up their
// peaks.
void __fastcall SetCollect(char* self, void*, const float* area, ListNode** list, uint32_t* count, uint32_t* total,
                           uint32_t useArea)
{
    *count = 0;
    *total = 0;
    ListNode* const sentinel = At<ListNode*>(self, set::kClusters);
    for (ListNode* node = sentinel->next; node != sentinel; node = node->next) {
        char* const group = static_cast<char*>(node->value);
        if (static_cast<uint8_t>(useArea)) {
            const float x = At<float>(group, cluster::kPlace);
            const float z = At<float>(group, cluster::kPlace + 8);
            if (!(x >= area[0]) || !(z >= area[1]) || !(x <= area[2]) || !(z <= area[3]))
                continue;
        }
        ListNode* const first = (*list)->next;
        char* const made = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{0x14});
        std::memcpy(made + 0x8, group + cluster::kPlace, 3 * sizeof(uint32_t));
        auto* const link = reinterpret_cast<ListNode*>(made);
        ListNode* const before = first->previous;
        link->next = first;
        link->previous = before;
        before->next = link;
        first->previous = link;
        ++*count;
        *total += At<uint32_t>(group, cluster::kPeak);
    }
}

// The game's 0x004F0C20. (place, radius), ret 8; `this` is the set. Counts
// a place into the first cluster it reaches (the two radii together more
// than the distance), or makes a cluster for it with the set's radius.
void __fastcall SetCountPlace(char* self, void*, const float* place, float radius)
{
    ListNode* const sentinel = At<ListNode*>(self, set::kClusters);
    for (ListNode* node = sentinel->next; node != sentinel; node = node->next) {
        char* const group = static_cast<char*>(node->value);
        const double distance = Distance(&At<float>(group, cluster::kPlace), place);
        if (static_cast<double>(radius) + static_cast<double>(At<float>(group, cluster::kRadius)) > distance) {
            CountPoint(group);
            return;
        }
    }
    char* const group = NewCluster(place, At<uint32_t>(self, set::kRadius));
    CountPoint(group);
    PushFront(At<ListNode*>(self, set::kClusters), group);
    ++At<uint32_t>(self, set::kClusterCount);
}

// The game's 0x004F0D40. No arguments; `this` is a cluster. Frees its
// points and takes them out of their map; the peak becomes the count and
// the count 0.
void __fastcall ClusterClear(char* self, void*)
{
    ListNode* const sentinel = At<ListNode*>(self, cluster::kPoints);
    ListNode* node = sentinel->next;
    while (node != sentinel) {
        char* const record = static_cast<char*>(node->value);
        char* const hashMap = At<char*>(record, point::kMap);
        HashIterator at = {FindNode(hashMap, At<uint32_t>(record, point::kKey)), hashMap};
        This<void>(addr::SC4Uint32HashMap_EraseAt, hashMap, &at);
        Cdecl<void>(addr::SC4_operator_delete, record);
        node = Unlink(node);
    }
    At<uint32_t>(self, cluster::kPeak) = At<uint32_t>(self, cluster::kCount);
    At<uint32_t>(self, cluster::kCount) = 0;
}

// The game's 0x004F0DE0. (keep the clusters), ret 4; `this` is the set.
// Clears every cluster and, unless told to keep them, frees them.
void __fastcall SetClear(char* self, void*, uint32_t keep)
{
    ListNode* const sentinel = At<ListNode*>(self, set::kClusters);
    ListNode* node = sentinel->next;
    while (node != sentinel) {
        char* const group = static_cast<char*>(node->value);
        ClusterClear(group, nullptr);
        if (static_cast<uint8_t>(keep)) {
            node = node->next;
            continue;
        }
        node = Unlink(node);
        if (group)
            FreeCluster(group);
        --At<uint32_t>(self, set::kClusterCount);
    }
}

// The game's 0x004F0E80. (place, radius), ret 8.
uint8_t __fastcall SetCountPlaceTrue(char* self, void*, const float* place, float radius)
{
    SetCountPlace(self, nullptr, place, radius);
    return 1;
}

// The game's 0x004F0EA0. (set, set, swap them), ret 0xC; `this` is the set
// to fill. Empties this set and gives it a one-point cluster for every
// cluster of the first set that reaches a cluster of the second, each
// taken once. The number of clusters at +0x20 is not raised, as in the
// original.
void __fastcall SetMerge(char* self, void*, char* first, char* second, uint32_t swap)
{
    char* const outer = static_cast<uint8_t>(swap) ? first : second;
    char* const inner = static_cast<uint8_t>(swap) ? second : first;
    SetClear(self, nullptr, 0);
    ListNode* const innerSentinel = At<ListNode*>(inner, set::kClusters);
    for (ListNode* node = innerSentinel->next; node != innerSentinel; node = node->next)
        At<uint8_t>(node->value, cluster::kTaken) = 0;

    ListNode* const outerSentinel = At<ListNode*>(outer, set::kClusters);
    uint32_t left = At<uint32_t>(inner, set::kClusterCount);
    ListNode* outerNode = outerSentinel->next;
    if (left == 0)
        return;
    while (outerNode != outerSentinel) {
        ListNode* const sentinel = At<ListNode*>(inner, set::kClusters);
        char* const other = static_cast<char*>(outerNode->value);
        ListNode* node = sentinel->next;
        if (left != 0) {
            while (node != sentinel) {
                char* const group = static_cast<char*>(node->value);
                if (At<uint8_t>(group, cluster::kTaken) == 0) {
                    const double distance =
                        Distance(&At<float>(group, cluster::kPlace), &At<float>(other, cluster::kPlace));
                    if (static_cast<double>(At<float>(other, cluster::kRadius)) +
                            static_cast<double>(At<float>(group, cluster::kRadius)) >
                        distance) {
                        char* const made =
                            NewCluster(group + cluster::kPlace, At<uint32_t>(group, cluster::kRadius));
                        CountPoint(made);
                        PushFront(At<ListNode*>(self, set::kClusters), made);
                        --left;
                        At<uint8_t>(group, cluster::kTaken) = 1;
                    }
                }
                node = node->next;
                if (left == 0)
                    break;
            }
        }
        outerNode = outerNode->next;
        if (left == 0)
            break;
    }
}

// The game's 0x004F1070, Release (slot 2). The destructor frees the list's
// nodes and the map's, not the clusters and points: the owner clears the
// set first.
uint32_t __fastcall SetRelease(char* self, void*)
{
    const int32_t count = At<int32_t>(self, set::kReferences);
    if (count > 1) {
        At<int32_t>(self, set::kReferences) = count - 1;
        return static_cast<uint32_t>(count - 1);
    }
    At<std::uintptr_t>(self, 0x0) = addr::SC4Audio_ClusterSetVtable;
    This<void>(addr::SC4PairList_Destroy, self + set::kClusters);
    This<void>(addr::SC4Uint32HashSet_Clear, self + set::kMap);
    if (void* const buckets = At<void*>(self + set::kMap, map::kBuckets))
        Cdecl<void>(addr::SC4_Free, buckets);
    Cdecl<void>(addr::SC4_operator_delete, self);
    return 0;
}

// The game's 0x004F10C0. (point), ret 4; `this` is the set. Puts a point
// into the first cluster it reaches, or makes a cluster at its place. The
// point that begins a cluster is counted in it but is not put in its list
// and gets no cluster recorded, as in the original.
void __fastcall SetPlacePoint(char* self, void*, char* record)
{
    ListNode* const sentinel = At<ListNode*>(self, set::kClusters);
    for (ListNode* node = sentinel->next; node != sentinel; node = node->next) {
        char* const group = static_cast<char*>(node->value);
        const double distance = Distance(&At<float>(group, cluster::kPlace), &At<float>(record, point::kPlace));
        if (static_cast<double>(At<float>(group, cluster::kRadius)) +
                static_cast<double>(At<float>(record, point::kRadius)) >
            distance) {
            At<char*>(record, point::kCluster) = group;
            PushFront(At<ListNode*>(group, cluster::kPoints), record);
            CountPoint(group);
            return;
        }
    }
    char* const group = NewCluster(record + point::kPlace, At<uint32_t>(self, set::kRadius));
    CountPoint(group);
    PushFront(At<ListNode*>(self, set::kClusters), group);
    ++At<uint32_t>(self, set::kClusterCount);
}

// The game's 0x004F1200. No arguments: frees every cluster and point.
uint8_t __fastcall SetClearAll(char* self, void*)
{
    SetClear(self, nullptr, 0);
    return 1;
}

// The game's 0x004F1210. (key, place, radius), ret 0xC; `this` is the set.
// A new point under a key; false when the key is there already.
uint8_t __fastcall SetAddPoint(char* self, void*, uint32_t key, const uint32_t* place, uint32_t radius)
{
    char* const hashMap = self + set::kMap;
    if (FindNode(hashMap, key))
        return 0;
    char* const record = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0x1C});
    if (record) {
        At<uint32_t>(record, point::kKey) = key;
        At<char*>(record, point::kCluster) = nullptr;
        At<uint32_t>(record, point::kPlace) = place[0];
        At<uint32_t>(record, point::kPlace + 4) = place[1];
        At<uint32_t>(record, point::kPlace + 8) = place[2];
        At<uint32_t>(record, point::kRadius) = radius;
        At<char*>(record, point::kMap) = hashMap;
    }
    *This<char**>(addr::SC4Uint32HashMap_Lookup, hashMap, &key) = record;
    SetPlacePoint(self, nullptr, record);
    return 1;
}

// The game's 0x004F12B0. (key, place, radius), ret 0xC: the point anew.
uint8_t __fastcall SetMovePoint(char* self, void*, uint32_t key, const uint32_t* place, uint32_t radius)
{
    SetRemovePoint(self, nullptr, key);
    return SetAddPoint(self, nullptr, key, place, radius);
}

// The game's 0x004F12E0, the constructor. (radius), ret 4; returns this.
char* __fastcall SetConstruct(char* self, void*, uint32_t radius)
{
    char* const hashMap = self + set::kMap;
    At<std::uintptr_t>(self, 0x0) = addr::SC4Audio_ClusterSetVtable;
    At<uint32_t>(self, set::kReferences) = 0;
    At<uint32_t>(hashMap, map::kBuckets) = 0;
    At<uint32_t>(hashMap, map::kBucketsEnd) = 0;
    At<uint32_t>(hashMap, map::kCapacity) = 0;
    At<uint32_t>(hashMap, map::kCount) = 0;
    This<void>(addr::SC4Uint32HashSet_Construct, hashMap, uint32_t{0x64});
    At<ListNode*>(self, set::kClusters) = nullptr;
    At<ListNode*>(self, set::kClusters) = NewSentinel();
    At<uint32_t>(self, set::kClusterCount) = 0;
    At<uint32_t>(self, set::kRadius) = radius;
    return self;
}

// ---------------------------------------------------------------------
// cSC4AudioListener
// ---------------------------------------------------------------------

// The game's 0x004F1330, QueryInterface. (interface, out), ret 8.
uint8_t __fastcall ListenerQueryInterface(char* self, void*, uint32_t id, void** out)
{
    if (id != 1 && id != kMessageTargetInterface && id != 0xCA4A66B9)
        return 0;
    *out = self;
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// The game's 0x004F1360, Init. Takes the message server and asks for three
// messages.
uint8_t __fastcall ListenerInit(char* self, void*)
{
    if (At<uint8_t>(self, listener::kStarted))
        return 1;
    void* server = nullptr;
    GetService(kMessageServerService, kMessageServerInterface, &server);
    void* const before = At<void*>(self, listener::kServer);
    if (server != before) {
        if (server)
            AddRef(server);
        At<void*>(self, listener::kServer) = server;
        if (before)
            Release(before);
    }
    Virtual<uint8_t>(At<void*>(self, listener::kServer), 0x14, self, uint32_t{0x26D31EC1});
    Virtual<uint8_t>(At<void*>(self, listener::kServer), 0x14, self, uint32_t{0x26D31EC2});
    Virtual<uint8_t>(At<void*>(self, listener::kServer), 0x14, self, uint32_t{0xC53D10AA});
    At<uint8_t>(self, listener::kStarted) = 1;
    This<void>(addr::SC4RefPtr_Destruct, &server);
    return 1;
}

// The game's 0x004F1400, Shutdown.
uint8_t __fastcall ListenerShutdown(char* self, void*)
{
    if (At<uint8_t>(self, listener::kStarted)) {
        Virtual<uint8_t>(At<void*>(self, listener::kServer), 0x18, self, uint32_t{0x26D31EC1});
        Virtual<uint8_t>(At<void*>(self, listener::kServer), 0x18, self, uint32_t{0x26D31EC2});
        Virtual<uint8_t>(At<void*>(self, listener::kServer), 0x18, self, uint32_t{0xC53D10AA});
        if (void* const server = At<void*>(self, listener::kServer)) {
            At<void*>(self, listener::kServer) = nullptr;
            Release(server);
        }
        if (void* const library = At<void*>(self, listener::kLibraryListener)) {
            At<void*>(self, listener::kLibraryListener) = nullptr;
            Release(library);
        }
        At<uint8_t>(self, listener::kStarted) = 0;
    }
    return 1;
}

// The game's 0x004F1470. (message), ret 4; `this` is not used. Is the
// message a standard one?
uint8_t __fastcall ListenerIsStandard(char*, void*, void* message)
{
    if (!message)
        return 0;
    void* standard = nullptr;
    Virtual<uint8_t>(message, 0x0, kStandardMessage, &standard);
    if (!standard)
        return 0;
    Release(standard);
    return 1;
}

// The game's 0x004F14B0. No arguments. Works out from the camera where the
// listener is (what the camera looks at, moved to the water's surface when
// it is below it, then raised by the zoom's height), tells the sound
// library, and posts message 0x8A4A69C7 when the camera has moved far
// enough since the last one. False when there is no city.
uint8_t __fastcall ListenerUpdate(char* self, void*)
{
    void* const occupants = Global<void*>(addr::SC4_NetworkService);
    if (!occupants)
        return 0;
    char* const view = Global<char*>(addr::SC4_CameraView);
    if (!view)
        return 1;
    float* const eyeNow = &At<float>(view, camera::kEye);
    const int32_t zoom = At<int32_t>(view, camera::kZoom);
    float eye[3];
    float up[3];
    float target[3];
    std::memcpy(&eye[0], eyeNow, sizeof(eye));
    std::memcpy(&up[0], &At<float>(view, camera::kUp), sizeof(up));
    std::memcpy(&target[0], &At<float>(view, camera::kTarget), sizeof(target));
    if (zoom < 2) {
        // The two far zooms listen at the middle of the city.
        float farX;
        float farZ;
        void* const city = Global<void*>(addr::SC4_City);
        const uint32_t cellsZ = Virtual<uint32_t>(city, 0x178);
        const uint32_t cellsX = Virtual<uint32_t>(city, 0x174);
        Virtual<uint8_t>(occupants, 0x48, cellsX, cellsZ, &farX, &farZ);
        target[0] = static_cast<float>(static_cast<double>(farX) * static_cast<double>(0.5f));
        target[2] = static_cast<float>(static_cast<double>(farZ) * static_cast<double>(0.5f));
    }
    void* const terrain = Global<void*>(addr::SC4_Terrain);
    const float one = Global<float>(addr::SC4_FloatOneB0986C);

    // The direction the camera looks in. The z difference is rounded to a
    // float before it is squared with itself unrounded.
    float forward[3];
    {
        const double dx = static_cast<double>(target[0]) - static_cast<double>(eye[0]);
        const double dy = static_cast<double>(target[1]) - static_cast<double>(eye[1]);
        const double dz = static_cast<double>(target[2]) - static_cast<double>(eye[2]);
        forward[2] = static_cast<float>(dz);
        const float inverse = static_cast<float>(
            static_cast<double>(one) / std::sqrt(dz * static_cast<double>(forward[2]) + dy * dy + dx * dx));
        forward[0] = static_cast<float>(dx * static_cast<double>(inverse));
        forward[1] = static_cast<float>(dy * static_cast<double>(inverse));
        forward[2] = static_cast<float>(static_cast<double>(forward[2]) * static_cast<double>(inverse));
    }
    float side[3];
    side[0] = static_cast<float>(static_cast<double>(forward[1]) * static_cast<double>(up[2]) -
                                 static_cast<double>(forward[2]) * static_cast<double>(up[1]));
    side[1] = static_cast<float>(static_cast<double>(forward[2]) * static_cast<double>(up[0]) -
                                 static_cast<double>(up[2]) * static_cast<double>(forward[0]));
    side[2] = static_cast<float>(static_cast<double>(up[1]) * static_cast<double>(forward[0]) -
                                 static_cast<double>(forward[1]) * static_cast<double>(up[0]));

    const double water = Virtual<double>(terrain, 0x74);
    if (static_cast<double>(target[1]) < water) {
        float plane[4] = {0.0f, 1.0f, 0.0f, static_cast<float>(-water)};
        Cdecl<uint8_t>(addr::SC4_RayPlane, &eye[0], &forward[0], &plane[0], &target[0]);
    }

    const double height =
        static_cast<double>(Global<float>(addr::SC4Audio_ListenerHeights + static_cast<std::uintptr_t>(zoom) * 4));
    At<uint32_t>(self, listener::kPlace) = Bits(target[0]);
    At<uint32_t>(self, listener::kPlace + 8) = Bits(target[2]);
    At<float>(self, listener::kPlace + 4) = static_cast<float>(static_cast<double>(target[1]) + height);
    {
        const float backX = static_cast<float>(static_cast<double>(forward[0]) * height);
        const float backY = static_cast<float>(static_cast<double>(forward[1]) * height);
        const double backZ = height * static_cast<double>(forward[2]);
        At<float>(self, listener::kBehind + 8) = static_cast<float>(static_cast<double>(target[2]) - backZ);
        At<float>(self, listener::kBehind) =
            static_cast<float>(static_cast<double>(target[0]) - static_cast<double>(backX));
        At<float>(self, listener::kBehind + 4) =
            static_cast<float>(static_cast<double>(target[1]) - static_cast<double>(backY));
    }

    // From the listener to what the camera looks at: straight down.
    {
        const double dx = static_cast<double>(target[0]) - static_cast<double>(At<float>(self, listener::kPlace));
        const double dy =
            static_cast<double>(target[1]) - static_cast<double>(At<float>(self, listener::kPlace + 4));
        const float dz = static_cast<float>(static_cast<double>(target[2]) -
                                            static_cast<double>(At<float>(self, listener::kPlace + 8)));
        const float inverse = static_cast<float>(
            static_cast<double>(one) /
            std::sqrt(dx * dx + static_cast<double>(dz) * static_cast<double>(dz) + dy * dy));
        forward[0] = static_cast<float>(dx * static_cast<double>(inverse));
        forward[1] = static_cast<float>(dy * static_cast<double>(inverse));
        forward[2] = static_cast<float>(static_cast<double>(dz) * static_cast<double>(inverse));
    }
    float top[3];
    float place[3];
    {
        const double x = static_cast<double>(side[1]) * static_cast<double>(forward[2]) -
                         static_cast<double>(side[2]) * static_cast<double>(forward[1]);
        const double y = static_cast<double>(side[2]) * static_cast<double>(forward[0]) -
                         static_cast<double>(forward[2]) * static_cast<double>(side[0]);
        const float z = static_cast<float>(static_cast<double>(forward[1]) * static_cast<double>(side[0]) -
                                           static_cast<double>(side[1]) * static_cast<double>(forward[0]));
        const double unrounded =
            static_cast<double>(one) / std::sqrt(x * x + static_cast<double>(z) * static_cast<double>(z) + y * y);
        At<uint32_t>(self, listener::kLookAt) = Bits(target[0]);
        At<uint32_t>(self, listener::kLookAt + 4) = Bits(target[1]);
        At<uint32_t>(self, listener::kLookAt + 8) = Bits(target[2]);
        std::memcpy(&place[0], &At<float>(self, listener::kPlace), sizeof(place));
        const float inverse = static_cast<float>(unrounded);
        const float topX = static_cast<float>(x * static_cast<double>(inverse));
        const double topY = y * static_cast<double>(inverse);
        const double topZ = static_cast<double>(z) * static_cast<double>(inverse);
        top[0] = -topX;
        top[1] = static_cast<float>(-topY);
        top[2] = static_cast<float>(-topZ);
    }
    void* library = Virtual<void*>(At<void*>(self, listener::kLibraryListener), 0xC);
    Virtual<void>(library, 0x24, Bits(forward[0]), Bits(forward[1]), Bits(forward[2]), Bits(top[0]), Bits(top[1]),
                  Bits(top[2]));
    library = Virtual<void*>(At<void*>(self, listener::kLibraryListener), 0xC);
    Virtual<void>(library, 0x1C, Bits(place[0]), Bits(place[1]), Bits(place[2]));

    float* const last = &At<float>(self, listener::kLastCamera);
    const double moved = Cdecl<double>(addr::SC4_DistanceSquared3, last, eyeNow);
    const float limit = At<float*>(self, listener::kMoveLimits)[At<int32_t>(view, camera::kZoom)];
    if (moved > static_cast<double>(limit)) {
        std::memcpy(last, eyeNow, 3 * sizeof(float));
        void* const message = NewMessage();
        if (message)
            AddRef(message);
        Virtual<void>(message, 0x14, kListenerMoved);
        Virtual<void>(message, 0x84, self);
        Virtual<void>(message, 0x2C, self + listener::kPlace);
        Virtual<uint8_t>(At<void*>(self, listener::kServer), 0x10, message, uint32_t{0});
        Release(message);
    }
    return 1;
}

// The game's 0x004F18C0. No arguments. The five heights by zoom: the
// product of the two factor arrays and the listener's factor.
void __fastcall ListenerSetHeightsBody(char* self)
{
    for (int32_t zoom = 4; zoom >= 0; --zoom) {
        const double height = static_cast<double>(At<float*>(self, listener::kFactorsB)[zoom]) *
                              static_cast<double>(At<float*>(self, listener::kFactorsA)[zoom]) *
                              static_cast<double>(At<float>(self, listener::kHeightFactor));
        Global<float>(addr::SC4Audio_ListenerHeights + static_cast<std::uintptr_t>(zoom) * 4) =
            static_cast<float>(height);
    }
}

// The listener's DoMessage (0x004F1E25) calls 0x004F14B0 right after this
// with ECX as this left: the thunk keeps it.
__declspec(naked) void ListenerSetHeights()
{
    __asm push ecx
    __asm call ListenerSetHeightsBody
    __asm pop ecx
    __asm ret
}

// The game's 0x004F1930, the constructor; returns this.
char* __fastcall ListenerConstruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4Audio_ListenerVtable;
    At<uint32_t>(self, listener::kReferences) = 0;
    At<uint8_t>(self, listener::kStarted) = 0;
    for (std::size_t offset = listener::kPlace; offset < listener::kLastCamera; offset += 4)
        At<uint32_t>(self, offset) = Global<uint32_t>(addr::SC4Audio_ListenerPlaceDefault);
    for (std::size_t offset = listener::kLastCamera; offset < listener::kHeightFactor; offset += 4)
        At<uint32_t>(self, offset) = 0;
    At<float>(self, listener::kHeightFactor) = 1.0f;
    At<float>(self, listener::kFour) = 4.0f;
    At<uint32_t>(self, listener::kLibraryListener) = 0;
    At<uint32_t>(self, listener::kServer) = 0;
    return self;
}

// The game's 0x004F19E0, the destructor.
void __fastcall ListenerDestruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4Audio_ListenerVtable;
    if (void* const server = At<void*>(self, listener::kServer))
        Release(server);
    if (void* const library = At<void*>(self, listener::kLibraryListener))
        Release(library);
    if (void* const array = At<void*>(self, listener::kMoveLimits))
        Cdecl<void>(addr::SC4_Free, array);
    if (void* const array = At<void*>(self, listener::kArrayC))
        Cdecl<void>(addr::SC4_Free, array);
    if (void* const array = At<void*>(self, listener::kFactorsB))
        Cdecl<void>(addr::SC4_Free, array);
    if (void* const array = At<void*>(self, listener::kFactorsA))
        Cdecl<void>(addr::SC4_Free, array);
}

// The game's 0x004F1A50, Release (slot 2).
uint32_t __fastcall ListenerRelease(char* self, void*)
{
    const int32_t count = At<int32_t>(self, listener::kReferences);
    if (count > 1) {
        At<int32_t>(self, listener::kReferences) = count - 1;
        return static_cast<uint32_t>(count - 1);
    }
    ListenerDestruct(self, nullptr);
    Cdecl<void>(addr::SC4_operator_delete, self);
    return 0;
}

bool InstallAudio6(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[72] = {};
    const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4Audio_F4EF010, reinterpret_cast<void*>(&OnMusicChoice)},
        {addr::SC4Audio_F4EF0C0, reinterpret_cast<void*>(&StartChannel)},
        {addr::SC4Audio_F4EF100, reinterpret_cast<void*>(&StopChannel)},
        {addr::SC4Audio_F4EF120, reinterpret_cast<void*>(&ResetSound)},
        {addr::SC4Audio_F4EF170, reinterpret_cast<void*>(&PointerSounds)},
        {addr::SC4Audio_F4EF490, reinterpret_cast<void*>(&OnSoundEvent)},
        {addr::SC4Audio_F4EF5D0, reinterpret_cast<void*>(&OnClickOutside)},
        {addr::SC4Audio_F4EF6F0, reinterpret_cast<void*>(&HandlerConstruct)},
        {addr::SC4Audio_F4EF7A0, reinterpret_cast<void*>(&HandlerQueryInterface4)},
        {addr::SC4Audio_F4EF7B0, reinterpret_cast<void*>(&HandlerRelease4)},
        {addr::SC4Audio_F4EF7C0, reinterpret_cast<void*>(&HandlerQueryInterface8)},
        {addr::SC4Audio_F4EF7D0, reinterpret_cast<void*>(&HandlerRelease8)},
        {addr::SC4Audio_F4EF7E0, reinterpret_cast<void*>(&HandlerQueryInterfaceC)},
        {addr::SC4Audio_F4EF800, reinterpret_cast<void*>(&HandlerReleaseC)},
        {addr::SC4Audio_F4EF810, reinterpret_cast<void*>(&HandlerDoMessage)},
        {addr::SC4Audio_F4EFC60, reinterpret_cast<void*>(&HandlerDestroy)},
        {addr::SC4Audio_F4EFCC0, reinterpret_cast<void*>(&OnWindowMessage)},
        {addr::SC4Audio_F4EFE50, reinterpret_cast<void*>(&HandlerDoOldMessage)},
        {addr::SC4Audio_F4EFE80, reinterpret_cast<void*>(&HandlerInit)},
        {addr::SC4Audio_F4F0250, reinterpret_cast<void*>(&FireRelease)},
        {addr::SC4Audio_F4F0270, reinterpret_cast<void*>(&FireQueryInterface)},
        {addr::SC4Audio_F4F02F0, reinterpret_cast<void*>(&FireQueryInterface4)},
        {addr::SC4Audio_F4F0300, reinterpret_cast<void*>(&FireRelease4)},
        {addr::SC4Audio_F4F0310, reinterpret_cast<void*>(&FireQueryInterface8)},
        {addr::SC4Audio_F4F0320, reinterpret_cast<void*>(&FireRelease8)},
        {addr::SC4Audio_F4F0330, reinterpret_cast<void*>(&FireDestroy8)},
        {addr::SC4Audio_F4F0340, reinterpret_cast<void*>(&FireQueryInterfaceC)},
        {addr::SC4Audio_F4F0350, reinterpret_cast<void*>(&FireAddRefC)},
        {addr::SC4Audio_F4F0360, reinterpret_cast<void*>(&FireReleaseC)},
        {addr::SC4Audio_F4F0370, reinterpret_cast<void*>(&FireQueryInterface14)},
        {addr::SC4Audio_F4F0380, reinterpret_cast<void*>(&FireAddRef14)},
        {addr::SC4Audio_F4F0390, reinterpret_cast<void*>(&FireRelease14)},
        {addr::SC4Audio_F4F03A0, reinterpret_cast<void*>(&FireShutdown)},
        {addr::SC4Audio_F4F0440, reinterpret_cast<void*>(&FireFilter)},
        {addr::SC4Audio_F4F0480, reinterpret_cast<void*>(&FireAdd)},
        {addr::SC4Audio_F4F04D0, reinterpret_cast<void*>(&FireEachOccupant)},
        {addr::SC4Audio_F4F0510, reinterpret_cast<void*>(&FireConstruct)},
        {addr::SC4Audio_F4F0570, reinterpret_cast<void*>(&FireDestroy)},
        {addr::SC4Audio_F4F05D0, reinterpret_cast<void*>(&FireDoMessage)},
        {addr::SC4Audio_F4F0740, reinterpret_cast<void*>(&FireInit)},
        {addr::SC4Audio_F4F08F0, reinterpret_cast<void*>(&ClusterRemovePoint)},
        {addr::SC4Audio_F4F0A30, reinterpret_cast<void*>(&SetDropEmptyClusters)},
        {addr::SC4Audio_F4F0AC0, reinterpret_cast<void*>(&SetRemovePoint)},
        {addr::SC4Audio_F4F0B40, reinterpret_cast<void*>(&SetCollect)},
        {addr::SC4Audio_F4F0C20, reinterpret_cast<void*>(&SetCountPlace)},
        {addr::SC4Audio_F4F0D40, reinterpret_cast<void*>(&ClusterClear)},
        {addr::SC4Audio_F4F0DE0, reinterpret_cast<void*>(&SetClear)},
        {addr::SC4Audio_F4F0E80, reinterpret_cast<void*>(&SetCountPlaceTrue)},
        {addr::SC4Audio_F4F0EA0, reinterpret_cast<void*>(&SetMerge)},
        {addr::SC4Audio_F4F1070, reinterpret_cast<void*>(&SetRelease)},
        {addr::SC4Audio_F4F10C0, reinterpret_cast<void*>(&SetPlacePoint)},
        {addr::SC4Audio_F4F1200, reinterpret_cast<void*>(&SetClearAll)},
        {addr::SC4Audio_F4F1210, reinterpret_cast<void*>(&SetAddPoint)},
        {addr::SC4Audio_F4F12B0, reinterpret_cast<void*>(&SetMovePoint)},
        {addr::SC4Audio_F4F12E0, reinterpret_cast<void*>(&SetConstruct)},
        {addr::SC4Audio_F4F1330, reinterpret_cast<void*>(&ListenerQueryInterface)},
        {addr::SC4Audio_F4F1360, reinterpret_cast<void*>(&ListenerInit)},
        {addr::SC4Audio_F4F1400, reinterpret_cast<void*>(&ListenerShutdown)},
        {addr::SC4Audio_F4F1470, reinterpret_cast<void*>(&ListenerIsStandard)},
        {addr::SC4Audio_F4F14B0, reinterpret_cast<void*>(&ListenerUpdate)},
        {addr::SC4Audio_F4F18C0, reinterpret_cast<void*>(&ListenerSetHeights)},
        {addr::SC4Audio_F4F1930, reinterpret_cast<void*>(&ListenerConstruct)},
        {addr::SC4Audio_F4F19E0, reinterpret_cast<void*>(&ListenerDestruct)},
        {addr::SC4Audio_F4F1A50, reinterpret_cast<void*>(&ListenerRelease)},
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

const bool g_audio6Registered = registry::Register({
    "audio.part6",
    "audio",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Audio_F4EF810,
    "Our audio event handler's second half, fire sounds' handler, cluster sets and listener (docs/audio.md)",
    false,
    InstallAudio6,
});

} // namespace
} // namespace opensc4::audio
