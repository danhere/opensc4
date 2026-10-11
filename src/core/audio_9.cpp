// The audio code, part 9 (0x004F7300 to 0x004FA6A0, where the audio code
// ends): the last four interface methods of cSC4AudioScapeManager and all
// of cSC4AudioSoundManager with the small classes it is built from.
//
// cSC4AudioSoundManager (GZCOM class and interface 0x4A47DC0D, 0x80 bytes,
// constructor 0x004F9830 reached from the factory at 0x004EB093; the audio
// system's static object 0x00B21978 keeps it at +0x60 and 0x004BB9E0 hands
// it out) plays the game's one-off and looping sounds through the
// framework's sound service (0x23CECBA0, interface 0x106077D2, kept at
// +0x5C) and its sound factory (service 0x1AF85BFB, interface 0x3AF85BB7,
// kept at +0x60). Callers never touch a sound: they ask the manager, the
// manager makes a message of the sound service (slot 13) and posts it to the
// service (slot 8), and the service hands it back to the manager's slot 3
// (0x004FA590), where the work is done:
//
//   1, 0xEA451903 (sound ID, instance, flags)   start a voice
//   2, 0x6A451908 (instance, how)               stop it
//   0x4A451915 (instance, level)                its level
//   0xCA45190F (instance, x, y, z)              its place, whole numbers
//   0x2A5B69C0 (level)                          the master level
//   0x4A8C52C8                                  every voice set again
//   0xCAA44CA7 (how)                            every voice stopped
//   0x6A95CBA2 (on)                             all muted (0) or let go
//   0x4A4D35E0 (instance)                       a voice started over
//   0x4A4FE6F6 (visitor)                        every voice shown to a visitor
//
// The object: vtable 0x00A94428 at +0 (QueryInterface, AddRef, Release,
// DoMessage), the sound service's tick at +4 (0x00A94418, slot 3 gets the
// milliseconds gone by), its "sound ended" callback at +8 (0x00A94408) and a
// message target at +0xC (0x00A943F8) for seven messages of the game's
// message server; +0x10 references, +0x14 the master level (0x400 is full),
// +0x18 a clock of the tick's milliseconds, +0x1C the tick's depth, +0x30 a
// countdown of ticks after sounds are let go, +0x34 a hash map of voices by
// instance (buckets at +0x38 and +0x3C), +0x48 the list of voices, +0x4C
// their number, +0x54 how many plain voices play, +0x58 how many grouped
// ones lead a group, +0x64, +0x68 and +0x6C three limits (3, 10, 30; the
// third is read and never used), +0x70 a curve from the number of voices of
// one sound to their level.
//
// A voice (0x60 bytes of the pool 0x00B098D0) is one playing sound: +0
// references, +4 the sound ID, +8 the instance number, +0xC the framework's
// sound, +0x10 its parameters (interface 0xFD584467: slot 4 gets one, slot 3
// sets one), +0x14 the manager, +0x18 the level, +0x1C a scale on it, +0x20
// the place, +0x2C flags (1 level set, 2 place set, 8 out of earshot, 0x10
// held by the paused simulation, 0x20 stops with the simulation), +0x30 the
// clock at its start, +0x34 and +0x38 the sound's near and far distances,
// +0x3C how many reasons mute it, +0x40 a fade (now, target, rate), +0x4C
// the voices grouped under it, +0x50 the voice it is grouped under, +0x54
// the group's place. Voices of a sound whose parameter 0x33 is set are
// grouped: only the first plays, placed at an average of the group that
// 0x004F8920 works out from the listener or the camera.
//
// Two small classes the ambience layers (parts 2 and 3) use too: a mover
// (0x3C bytes: a place and a level that walk to their targets at given
// rates) and the fade above.
//
// Of cSC4AudioScapeManager (part 8; vtable 0x00A94340): slot 12 (a point
// added, with a scape made for it where there is none), slot 15 (a point
// moved), slots 9 and 10 (a scape's clusters collected, alone or where they
// lie near another scape's).
//
// Replacement, off by default:
//
//   audio.part9: everything named above.
//
// Not functions: 0x004F75B0 is the middle of 0x004F7510, 0x004F78D0 of
// 0x004F7850, 0x004F7B90 of 0x004F7B70, 0x004F9080 of 0x004F9050 and
// 0x004F9BE0 of 0x004F9B90. The last function, 0x004FA590, ends at
// 0x004FA696; what follows from 0x004FA6A0 is not audio code.
//
// Still the game's: the pool's two functions 0x004F7C10 and 0x004F82A0
// (another class's pool at 0x00AA2CE0 has them in its vtable too), the
// AddRef thunk 0x004F88C0 (also slot 1 of the vtable 0x00A93758), and the
// three that carry other threads' names: SC4::AddRefAt10 0x004F7BA0,
// SC4Sound::NextInstance 0x004F7BB0 and SC4Uint32HashMap::Lookup
// 0x004F8D30.
//
// Time: the manager's clock is the sound service's tick, which is real
// time, and so are the fades, the 50 millisecond rule for a sound started
// twice and whether the framework says a sound still plays. What voices
// exist and with which IDs, levels, places and flags follows from the
// messages sent, which come from the city, the camera and the player.
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

namespace scapes { // cSC4AudioScapeManager (part 8)
constexpr std::size_t kSound = 0x0C;
constexpr std::size_t kLock = 0x44;
constexpr std::size_t kScapes = 0x54;
} // namespace scapes

namespace scape { // a scape: 0x34 bytes, a game string first
constexpr std::size_t kNameBegin = 0x04;
constexpr std::size_t kId = 0x18;
constexpr std::size_t kClusters = 0x1C;
constexpr std::size_t kClusterCount = 0x20;
constexpr std::size_t kFlags = 0x24;
constexpr std::size_t kEmitters = 0x28;
constexpr std::size_t kRadius = 0x2C;
constexpr std::size_t kPointCount = 0x30;
constexpr std::size_t kSize = 0x34;
} // namespace scape

namespace cluster { // a cluster: an item of the pool 0x00B098A8
constexpr std::size_t kPlace = 0x00;
constexpr std::size_t kSum = 0x0C;
constexpr std::size_t kSummed = 0x18;
constexpr std::size_t kAdded = 0x1C;
constexpr std::size_t kRadius = 0x20;
constexpr std::size_t kPoints = 0x24;
constexpr std::size_t kInstances = 0x28;
constexpr std::size_t kPointCount = 0x30;
} // namespace cluster

namespace snd { // cSC4AudioSoundManager
constexpr std::size_t kTick = 0x04;
constexpr std::size_t kSoundEnded = 0x08;
constexpr std::size_t kTarget = 0x0C;
constexpr std::size_t kReferences = 0x10;
constexpr std::size_t kLevel = 0x14;
constexpr std::size_t kClock = 0x18;
constexpr std::size_t kDepth = 0x1C;
constexpr std::size_t kCountdown = 0x30;
constexpr std::size_t kMap = 0x34;
constexpr std::size_t kBuckets = 0x38;
constexpr std::size_t kBucketsEnd = 0x3C;
constexpr std::size_t kVoices = 0x48;
constexpr std::size_t kVoiceCount = 0x4C;
constexpr std::size_t kPlain = 0x54;
constexpr std::size_t kGroups = 0x58;
constexpr std::size_t kService = 0x5C;
constexpr std::size_t kFactory = 0x60;
constexpr std::size_t kMostOfOne = 0x64;
constexpr std::size_t kMostPlain = 0x68;
constexpr std::size_t kLimit6C = 0x6C;
constexpr std::size_t kCurve = 0x70;
} // namespace snd

namespace voice { // a voice: 0x60 bytes of the pool 0x00B098D0
constexpr std::size_t kReferences = 0x00;
constexpr std::size_t kSoundId = 0x04;
constexpr std::size_t kInstance = 0x08;
constexpr std::size_t kSound = 0x0C;
constexpr std::size_t kParams = 0x10;
constexpr std::size_t kManager = 0x14;
constexpr std::size_t kLevel = 0x18;
constexpr std::size_t kScale = 0x1C;
constexpr std::size_t kPlace = 0x20;
constexpr std::size_t kFlags = 0x2C;
constexpr std::size_t kStarted = 0x30;
constexpr std::size_t kNear = 0x34;
constexpr std::size_t kFar = 0x38;
constexpr std::size_t kMuted = 0x3C;
constexpr std::size_t kFade = 0x40;
constexpr std::size_t kChildren = 0x4C;
constexpr std::size_t kParent = 0x50;
constexpr std::size_t kGroupPlace = 0x54;
} // namespace voice

namespace mover { // 0x3C bytes
constexpr std::size_t kTarget = 0x00;
constexpr std::size_t kPlace = 0x0C;
constexpr std::size_t kLevelTarget = 0x18;
constexpr std::size_t kLevel = 0x1C;
constexpr std::size_t kPlaceRate = 0x20;
constexpr std::size_t kLevelRate = 0x24;
constexpr std::size_t kMoved = 0x38; // byte: it has taken a step
} // namespace mover

// Voice flags.
constexpr uint32_t kLevelSet = 0x1;
constexpr uint32_t kPlaceSet = 0x2;
constexpr uint32_t kOutOfEarshot = 0x8;
constexpr uint32_t kHeldByPause = 0x10;
constexpr uint32_t kStopsWithPause = 0x20;

// A sound's parameters.
constexpr uint32_t kParamState = 0x10;
constexpr uint32_t kParamLevel = 0x15;
constexpr uint32_t kParamX = 0x20;
constexpr uint32_t kParamY = 0x21;
constexpr uint32_t kParamZ = 0x22;
constexpr uint32_t kParamGrouped = 0x33;
constexpr uint32_t kParamInstance = 0x34;
constexpr uint32_t kParamDistance = 0x38;
constexpr uint32_t kParamKind = 0x39;

constexpr uint32_t kKindCamera = 0x0A63A543;
constexpr uint32_t kKindListener = 0x7DCDED2D;
constexpr uint32_t kKindListenerGroup = 0x7DCDED3D;

constexpr uint32_t kManagerIid = 0x4A47DC0D;
constexpr uint32_t kSoundMessageIid = 0x5B523BA4;
constexpr uint32_t kStandardMessageIid = 0x65297976;
constexpr uint32_t kSoundIid = 0x206077D2;
constexpr uint32_t kParamsIid = 0xFD584467;
constexpr uint32_t kEndedIid = 0xE9D00A9A;
constexpr uint32_t kRestartIid = 0xBB1BE367;
constexpr uint32_t kVisitorIid = 0xEA4FC425;
constexpr uint32_t kExemplarIid = 0xA52160F5;

// The messages of the sound service.
constexpr uint32_t kStart = 0xEA451903;
constexpr uint32_t kStop = 0x6A451908;
constexpr uint32_t kSetLevel = 0x4A451915;
constexpr uint32_t kSetPlace = 0xCA45190F;
constexpr uint32_t kMasterLevel = 0x2A5B69C0;
constexpr uint32_t kRefresh = 0x4A8C52C8;
constexpr uint32_t kStopAll = 0xCAA44CA7;
constexpr uint32_t kLetGo = 0x6A95CBA2;
constexpr uint32_t kStartOver = 0x4A4D35E0;
constexpr uint32_t kVisit = 0x4A4FE6F6;

// The game's messages the manager asks the message server for.
constexpr uint32_t kMessages[] = {
    0x28E9397B, 0x28E9397C, 0x26D31EC2, 0x8A4A69C7, 0x4348B111, 0xAA7FB7E0, 0x4A7FB7E9,
};

constexpr float kThousandth = 0.001f;          // 0x00A867A4
constexpr float kOver1024 = 0.0009765625f;     // 0x00A931FC
constexpr float kOne = 1.0f;                   // 0x00A943A0
constexpr float kFour = 4.0f;                  // 0x00A943A4
constexpr float kHundredth = 0.01f;            // 0x00A823A4
constexpr float kHundred = 100.0f;             // 0x00A80AA0

// The game's list node.
struct Node {
    Node* next;
    Node* prev;
    void* value;
};
static_assert(sizeof(Node) == 0xC);

// The game's string, as its inlined constructor lays it out.
struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};
static_assert(sizeof(GameString) == 0x14);

// A scape's emitter (part 8).
struct Emitter {
    uint32_t minimum;
    uint32_t maximum;
    uint32_t value;
    uint32_t soundId;
    uint8_t flag;
    uint32_t radius; // a float's bits
};
static_assert(sizeof(Emitter) == 0x18);

// std::vector of (x, y) float pairs.
struct PointVector {
    float* begin;
    float* end;
    float* capacity;
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

// The truncating float-to-int conversion (0x009EEF04).
int32_t Ftol(double value)
{
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

float FromBits(uint32_t bits)
{
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

uint32_t ToBits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

uint32_t AddRef(void* object)
{
    return Virtual<uint32_t>(object, 0x4);
}

uint32_t Release(void* object)
{
    return Virtual<uint32_t>(object, 0x8);
}

uint8_t QueryInterface(void* object, uint32_t iid, void** out)
{
    return Virtual<uint8_t>(object, 0x0, iid, out);
}

void Lock(void* section)
{
    Virtual<void>(section, 0x4);
}

void Unlock(void* section)
{
    Virtual<void>(section, 0x8);
}

void* Allocate(uint32_t size)
{
    return Cdecl<void*>(addr::SC4_stl_allocate, size);
}

void Free(void* memory)
{
    Cdecl<void>(addr::SC4_Free, memory);
}

void CopyPlace(void* to, const void* from)
{
    static_cast<uint32_t*>(to)[0] = static_cast<const uint32_t*>(from)[0];
    static_cast<uint32_t*>(to)[1] = static_cast<const uint32_t*>(from)[1];
    static_cast<uint32_t*>(to)[2] = static_cast<const uint32_t*>(from)[2];
}

// The 0.0 and 1.0 the audio code keeps in writable memory.
uint32_t ZeroBits()
{
    return Global<uint32_t>(addr::SC4AudioSound_Zero);
}

double One()
{
    return static_cast<double>(Global<float>(addr::SC4AudioSound_One));
}

Node* NewList()
{
    Node* const end = static_cast<Node*>(Allocate(0xC));
    end->next = end;
    end->prev = end;
    return end;
}

void PushBack(Node* end, void* value)
{
    Node* const node = static_cast<Node*>(Allocate(0xC));
    node->value = value;
    Node* const last = end->prev;
    node->next = end;
    node->prev = last;
    last->next = node;
    end->prev = node;
}

void Unlink(Node* node)
{
    Node* const next = node->next;
    Node* const prev = node->prev;
    prev->next = next;
    next->prev = prev;
    Free(node);
}

// A list's end as the original has it inline: the nodes freed, then the
// end node.
void DestroyListInline(char* holder)
{
    Node* node = At<Node*>(holder, 0)->next;
    while (node != At<Node*>(holder, 0)) {
        Node* const gone = node;
        node = node->next;
        Free(gone);
    }
    At<Node*>(holder, 0)->next = At<Node*>(holder, 0);
    At<Node*>(holder, 0)->prev = At<Node*>(holder, 0);
    if (At<Node*>(holder, 0))
        Free(At<Node*>(holder, 0));
}

// One item out of a pool of 8 KB blocks (the pool's +4 is the block in use,
// whose +8 is its free list and +0xC its count of items out).
void* PoolTake(std::uintptr_t pool)
{
    void* item = At<void*>(Global<char*>(pool + 4), 0x8);
    if (!item)
        item = This<void*>(addr::SC4EdgePool_Grow, reinterpret_cast<void*>(pool));
    At<void*>(Global<char*>(pool + 4), 0x8) = *static_cast<void**>(item);
    ++At<int32_t>(Global<char*>(pool + 4), 0xC);
    return item;
}

// An item back into its block, where the original has it inline.
void PoolGiveInline(std::uintptr_t pool, void* item, int32_t reopenAt)
{
    char* const block = reinterpret_cast<char*>(reinterpret_cast<std::uintptr_t>(item) & 0xFFFFE000u);
    *static_cast<void**>(item) = At<void*>(block, 0x8);
    At<void*>(block, 0x8) = item;
    const int32_t left = At<int32_t>(block, 0xC) - 1;
    At<int32_t>(block, 0xC) = left;
    if (left == 0)
        This<void>(addr::SC4Pool_ReleaseBlock, reinterpret_cast<void*>(pool), block);
    else if (left == reopenAt)
        This<void>(addr::SC4Pool_ReopenBlock, reinterpret_cast<void*>(pool), block);
}

// The pool of the scape manager's clusters and points (part 8).
void* ScapePool()
{
    return reinterpret_cast<void*>(std::uintptr_t{addr::SC4AudioScape_Pool});
}

// The length of (x, y, z) as the code works it out everywhere: z and y
// squared first, then x.
double Length(double x, double y, double z)
{
    return std::sqrt((z * z + y * y) + x * x);
}

// --- cSC4AudioScapeManager's last four methods --------------------------------------

// The game's 0x004F7300 (ret 0x18): slot 12. Adds a point to the scape with
// the given ID. Where there is no such scape one named "nameless" is made
// first (slot 5) and the point goes in through slot 13; where there is, an
// emitter for the sound ID is given along (none for ID 0), and the point is
// kept as a pool item unless the scape's flag 4 says points are not kept.
uint8_t __fastcall ScapeAddPoint(char* self, void*, uint32_t scapeId, uint32_t soundId, uint32_t flag,
                                 uint32_t pointId, uint32_t* place, uint32_t radius)
{
    void* const lock = self + scapes::kLock;
    Lock(lock);

    char* found = nullptr;
    Node* const all = At<Node*>(self, scapes::kScapes);
    for (Node* node = all->next; node != all; node = node->next) {
        char* const item = static_cast<char*>(node->value);
        if (At<uint32_t>(item, scape::kId) == scapeId) {
            found = item;
            break;
        }
    }

    if (!found) {
        GameString name;
        name.begin = nullptr;
        name.end = nullptr;
        name.capacity = nullptr;
        char* const buffer = static_cast<char*>(Allocate(9));
        name.begin = buffer;
        name.end = buffer;
        name.capacity = buffer + 9;
        char* const end = Cdecl<char*>(addr::SC4_memmove, buffer, "nameless", uint32_t{8}) + 8;
        name.end = end;
        *end = 0;
        name.vtable = addr::SC4String_Vtable;
        name.zero = 0;
        const bool failed = Virtual<uint8_t>(self, 0x14, &name, scapeId, uint32_t{1}, 50.0f) == 0;
        name.vtable = addr::SC4String_Vtable;
        if (name.begin)
            Free(name.begin);
        if (failed) {
            Unlock(lock);
            return 0;
        }
        const uint8_t added = Virtual<uint8_t>(self, 0x34, scapeId, pointId, place, radius);
        Unlock(lock);
        return added;
    }

    Emitter emitter;
    emitter.minimum = 1;
    emitter.maximum = 0x7FFFFFFF;
    emitter.value = 0;
    emitter.flag = static_cast<uint8_t>(flag);
    emitter.radius = radius;
    emitter.soundId = soundId;
    Emitter* const given = soundId ? &emitter : nullptr;

    uint32_t* point = nullptr;
    if (!((At<uint32_t>(found, scape::kFlags) >> 2) & 1)) {
        if (!This<uint8_t>(addr::SC4Audio_F4F5860, self, found, pointId)) {
            Unlock(lock);
            return 0;
        }
        point = static_cast<uint32_t*>(PoolTake(addr::SC4AudioScape_Pool));
        point[0] = 0;
        point[1] = 0;
        point[2] = 0;
        point[3] = 0;
        point[0] = pointId;
        point[1] = place[0];
        point[2] = place[1];
        point[3] = place[2];
    }

    if (This<uint8_t>(addr::SC4Audio_F4F6C80, self, found, given, point, place, radius, uint32_t{1})) {
        ++At<int32_t>(found, scape::kPointCount);
        Unlock(lock);
        return 1;
    }
    if (point)
        This<void>(addr::SC4Audio_F4F5670, ScapePool(), point);
    Unlock(lock);
    return 0;
}

// The second half of 0x004F7510 (from 0x004F75B0): the point is found.
uint8_t MoveFoundPoint(char* self, void* lock, char* owner, Node* clusterNode, char* group, Node* pointNode,
                       char* point, char* place, uint32_t radius)
{
    // Out of the cluster's sum. The average is taken over the count before
    // it goes down by one, as the original has it.
    if (At<int32_t>(group, cluster::kSummed) != 0) {
        for (std::size_t i = 0; i < 12; i += 4) {
            At<float>(group, cluster::kSum + i) = static_cast<float>(
                static_cast<double>(At<float>(group, cluster::kSum + i)) -
                static_cast<double>(At<float>(point, 4 + i)));
        }
        CopyPlace(group + cluster::kPlace, group + cluster::kSum);
        const int32_t summed = At<int32_t>(group, cluster::kSummed);
        const double share =
            static_cast<double>(Global<float>(addr::SC4AudioScape_One)) / static_cast<double>(summed);
        At<int32_t>(group, cluster::kSummed) = summed - 1;
        for (std::size_t i = 0; i < 12; i += 4) {
            At<float>(group, cluster::kPlace + i) =
                static_cast<float>(share * static_cast<double>(At<float>(group, cluster::kPlace + i)));
        }
    }

    // Back in at its new place.
    This<void>(addr::SC4Audio_F4F5520, group, place);
    CopyPlace(point + 4, place);

    Node* const emitters = At<Node*>(owner, scape::kEmitters);
    for (Node* node = emitters->next; node != emitters; node = node->next) {
        uint32_t* const item = static_cast<uint32_t*>(node->value);
        if (!item)
            continue;
        const uint32_t count = At<uint32_t>(group, cluster::kPointCount);
        if (count < item[0] || count > item[1])
            This<void>(addr::SC4Audio_F4F5AF0, group, item);
        else
            This<void>(addr::SC4Audio_F4F6050, group, item);
    }
    This<void>(addr::SC4Audio_F4F5900, group);

    const double dx = static_cast<double>(At<float>(place, 0)) - static_cast<double>(At<float>(group, 0));
    const double dy = static_cast<double>(At<float>(place, 4)) - static_cast<double>(At<float>(group, 4));
    const double dz = static_cast<double>(At<float>(place, 8)) - static_cast<double>(At<float>(group, 8));
    const double distance = Length(dx, dy, dz);
    const double reach =
        static_cast<double>(FromBits(radius)) + static_cast<double>(At<float>(group, cluster::kRadius));
    if (reach >= distance) {
        Unlock(lock);
        return 1;
    }

    // Too far from its cluster now: out of it, and in again through AddPoint.
    Unlink(pointNode);
    --At<int32_t>(group, cluster::kPointCount);
    Node* const left = At<Node*>(group, cluster::kPoints);
    if (left->next == left) {
        char* const gone = static_cast<char*>(clusterNode->value);
        if (gone && At<void*>(self, scapes::kSound)) {
            This<void>(addr::SC4Audio_F4F5B80, gone);
            DestroyListInline(gone + cluster::kInstances);
            DestroyListInline(gone + cluster::kPoints);
            This<void>(addr::SC4Audio_F4F5670, ScapePool(), gone);
            Unlink(clusterNode);
            --At<int32_t>(owner, scape::kClusterCount);
        }
    }

    if (This<uint8_t>(addr::SC4Audio_F4F6C80, self, owner, static_cast<void*>(nullptr), point, place, radius,
                      uint32_t{1})) {
        Unlock(lock);
        return 1;
    }
    PoolGiveInline(addr::SC4AudioScape_Pool, point, 0x40000050);
    Unlock(lock);
    return 0;
}

// The game's 0x004F7510 (ret 0x10): slot 15. Moves the point with the given
// ID, whichever scape has it. The first argument is not read. Always true
// when no scape has the point.
uint8_t __fastcall ScapeMovePoint(char* self, void*, uint32_t, uint32_t pointId, char* place, uint32_t radius)
{
    void* const lock = self + scapes::kLock;
    Lock(lock);
    Node* const all = At<Node*>(self, scapes::kScapes);
    for (Node* scapeNode = all->next; scapeNode != all; scapeNode = scapeNode->next) {
        char* const owner = static_cast<char*>(scapeNode->value);
        Node* const clusters = At<Node*>(owner, scape::kClusters);
        for (Node* clusterNode = clusters->next; clusterNode != clusters; clusterNode = clusterNode->next) {
            char* const group = static_cast<char*>(clusterNode->value);
            Node* const points = At<Node*>(group, cluster::kPoints);
            for (Node* pointNode = points->next; pointNode != points; pointNode = pointNode->next) {
                char* const point = static_cast<char*>(pointNode->value);
                if (At<uint32_t>(point, 0) == pointId)
                    return MoveFoundPoint(self, lock, owner, clusterNode, group, pointNode, point, place, radius);
            }
        }
    }
    Unlock(lock);
    return 1;
}

// The game's 0x004F7850 (ret 0x1C): slot 9. Makes a scape for the moment
// out of copies of the first scape's clusters that lie within the two
// scapes' radii of a cluster of the second (each cluster once, as many as
// the first scape has), and collects that (part 8's 0x004F6E60, which
// writes through the fifth argument).
uint8_t __fastcall ScapeCollectNear(char* self, void*, uint32_t firstId, uint32_t secondId, uint32_t a3, uint32_t a4,
                                    uint32_t* out, uint32_t a6, uint32_t a7)
{
    void* const lock = self + scapes::kLock;
    Lock(lock);
    char* first = nullptr;
    if (!This<uint8_t>(addr::SC4Audio_F4F58B0, self, firstId, &first)) {
        *out = 0;
        Unlock(lock);
        return 0;
    }
    char* second = nullptr;
    if (!This<uint8_t>(addr::SC4Audio_F4F58B0, self, secondId, &second)) {
        *out = 0;
        Unlock(lock);
        return 0;
    }

    char* made = static_cast<char*>(Cdecl<void*>(addr::SC4_operator_new, uint32_t{scape::kSize}));
    made = made ? This<char*>(addr::SC4Audio_F4F6340, made) : nullptr;
    At<uint32_t>(made, scape::kRadius) = At<uint32_t>(first, scape::kRadius);

    Node* const marked = At<Node*>(first, scape::kClusters);
    for (Node* node = marked->next; node != marked; node = node->next)
        At<uint32_t>(node->value, cluster::kAdded) = 0;

    Node* const theirs = At<Node*>(second, scape::kClusters);
    int32_t left = At<int32_t>(first, scape::kClusterCount);
    for (Node* theirNode = theirs->next; left != 0 && theirNode != theirs; theirNode = theirNode->next) {
        char* const there = static_cast<char*>(theirNode->value);
        Node* const mine = At<Node*>(first, scape::kClusters);
        for (Node* myNode = mine->next; left != 0 && myNode != mine; myNode = myNode->next) {
            char* const here = static_cast<char*>(myNode->value);
            if (At<int32_t>(here, cluster::kAdded) != 0)
                continue;
            const double dx = static_cast<double>(At<float>(there, 0)) - static_cast<double>(At<float>(here, 0));
            const double dy = static_cast<double>(At<float>(there, 4)) - static_cast<double>(At<float>(here, 4));
            const double dz = static_cast<double>(At<float>(there, 8)) - static_cast<double>(At<float>(here, 8));
            const double distance = Length(dx, dy, dz);
            const double reach = static_cast<double>(At<float>(second, scape::kRadius)) +
                                 static_cast<double>(At<float>(first, scape::kRadius));
            if (!(reach > distance))
                continue;

            char* const copy = This<char*>(addr::SC4Audio_F4F5CD0, PoolTake(addr::SC4AudioScape_Pool));
            At<uint32_t>(copy, cluster::kRadius) = At<uint32_t>(made, scape::kRadius);
            CopyPlace(copy + cluster::kPlace, here + cluster::kPlace);
            PushBack(At<Node*>(made, scape::kClusters), copy);
            ++At<int32_t>(made, scape::kPointCount);
            ++At<int32_t>(made, scape::kClusterCount);
            ++At<int32_t>(here, cluster::kAdded);
            --left;
        }
    }

    const uint8_t result = This<uint8_t>(addr::SC4Audio_F4F6E60, self, made, a3, a4, out, a6, a7);
    This<void>(addr::SC4Audio_F4F5F20, made);
    This<void>(addr::SC4PairList_Destroy, made + scape::kEmitters);
    This<void>(addr::SC4PairList_Destroy, made + scape::kClusters);
    At<std::uintptr_t>(made, 0) = addr::SC4String_Vtable;
    if (At<void*>(made, scape::kNameBegin))
        Free(At<void*>(made, scape::kNameBegin));
    Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(made));
    Unlock(lock);
    return result;
}

// The game's 0x004F7AF0 (ret 0x18): slot 10. Collects the scape with the
// given ID.
uint8_t __fastcall ScapeCollect(char* self, void*, uint32_t scapeId, uint32_t a2, uint32_t a3, uint32_t* out,
                                uint32_t a5, uint32_t a6)
{
    void* const lock = self + scapes::kLock;
    Lock(lock);
    char* found = nullptr; // the original uses the first argument's slot
    if (This<uint8_t>(addr::SC4Audio_F4F58B0, self, scapeId, &found)) {
        const uint8_t result = This<uint8_t>(addr::SC4Audio_F4F6E60, self, found, a2, a3, out, a5, a6);
        Unlock(lock);
        return result;
    }
    *out = 0;
    Unlock(lock);
    return 0;
}

// --- The mover and the fade -------------------------------------------------------

// The game's 0x004F7BD0 (ret 8): the level to walk to; taken at once when
// asked or while the mover has not stepped yet.
void __fastcall MoverSetLevel(char* self, void*, uint32_t level, uint8_t now)
{
    At<uint32_t>(self, mover::kLevelTarget) = level;
    if (now || !At<uint8_t>(self, mover::kMoved))
        At<uint32_t>(self, mover::kLevel) = level;
}

// The game's 0x004F7BF0 (ret 8): the two rates, given per second, kept per
// millisecond.
void __fastcall MoverSetRates(char* self, void*, float place, float level)
{
    At<float>(self, mover::kPlaceRate) = static_cast<float>(static_cast<double>(place) * kThousandth);
    At<float>(self, mover::kLevelRate) = static_cast<float>(static_cast<double>(level) * kThousandth);
}

// The game's 0x004F81D0: the mover's constructor.
char* __fastcall MoverConstruct(char* self, void*)
{
    for (std::size_t i = 0; i < 0x18; i += 4)
        At<uint32_t>(self, i) = ZeroBits();
    At<uint32_t>(self, 0x18) = 0;
    At<uint32_t>(self, 0x1C) = 0;
    At<uint32_t>(self, 0x20) = 0;
    At<uint32_t>(self, 0x24) = 0;
    At<uint32_t>(self, 0x28) = ZeroBits();
    At<uint32_t>(self, 0x2C) = ZeroBits();
    At<uint32_t>(self, 0x30) = ZeroBits();
    At<uint32_t>(self, 0x34) = 0;
    At<uint8_t>(self, mover::kMoved) = 0;
    return self;
}

// The game's 0x004F8240 (ret 8): the place to walk to.
void __fastcall MoverSetPlace(char* self, void*, const uint32_t* place, uint8_t now)
{
    CopyPlace(self + mover::kTarget, place);
    if (now || !At<uint8_t>(self, mover::kMoved))
        CopyPlace(self + mover::kPlace, place);
}

// The game's 0x004F8280 (ret 4): where the mover is.
uint32_t* __fastcall MoverGetPlace(char* self, void*, uint32_t* out)
{
    CopyPlace(out, self + mover::kPlace);
    return out;
}

// The game's 0x004F8650 (ret 4): a step of the given milliseconds: the
// place goes toward its target by at most rate times the time, and so does
// the level.
void __fastcall MoverStep(char* self, void*, float time)
{
    const double dx = static_cast<double>(At<float>(self, 0x0)) - static_cast<double>(At<float>(self, 0xC));
    const float dy =
        static_cast<float>(static_cast<double>(At<float>(self, 0x4)) - static_cast<double>(At<float>(self, 0x10)));
    const double dz = static_cast<double>(At<float>(self, 0x8)) - static_cast<double>(At<float>(self, 0x14));
    const double length = std::sqrt((dz * dz + dx * dx) + static_cast<double>(dy) * static_cast<double>(dy));
    const float lengthKept = static_cast<float>(length);
    if (lengthKept != 0.0f) {
        const double inverse = One() / length;
        const float nx = static_cast<float>(dx * inverse);
        const float ny = static_cast<float>(static_cast<double>(dy) * inverse);
        const double nz = inverse * dz;
        const float most =
            static_cast<float>(static_cast<double>(time) * static_cast<double>(At<float>(self, mover::kPlaceRate)));
        const float step = lengthKept < most ? lengthKept : most;
        const double sx = static_cast<double>(nx) * static_cast<double>(step);
        const float sy = static_cast<float>(static_cast<double>(step) * static_cast<double>(ny));
        const float sz = static_cast<float>(nz * static_cast<double>(step));
        At<float>(self, 0xC) = static_cast<float>(sx + static_cast<double>(At<float>(self, 0xC)));
        At<float>(self, 0x10) =
            static_cast<float>(static_cast<double>(sy) + static_cast<double>(At<float>(self, 0x10)));
        At<float>(self, 0x14) =
            static_cast<float>(static_cast<double>(sz) + static_cast<double>(At<float>(self, 0x14)));
    }

    const int32_t level = At<int32_t>(self, mover::kLevel);
    const double apart = static_cast<double>(At<int32_t>(self, mover::kLevelTarget) - level);
    const float size = static_cast<float>(std::fabs(apart));
    if (size != 0.0f) {
        const double sign = apart / static_cast<double>(size);
        const float most =
            static_cast<float>(static_cast<double>(time) * static_cast<double>(At<float>(self, mover::kLevelRate)));
        const float step = size < most ? size : most;
        At<int32_t>(self, mover::kLevel) = Ftol(sign * static_cast<double>(step)) + level;
    }
    At<uint8_t>(self, mover::kMoved) = 1;
}

// The game's 0x004F7C50 (ret 4): a fade (now, target, rate) taken a step of
// the given time toward its target, never past it. False when it is there.
uint8_t __fastcall FadeStep(float* self, void*, float time)
{
    if (self[0] == self[1])
        return 0;
    const double step = static_cast<double>(time) * static_cast<double>(self[2]);
    const float stepKept = static_cast<float>(step);
    const double apart = static_cast<double>(self[1]) - static_cast<double>(self[0]);
    const float apartKept = static_cast<float>(apart);
    float taken;
    if (self[2] > 0.0f)
        taken = step < static_cast<double>(apartKept) ? stepKept : apartKept;
    else
        taken = apart < step ? stepKept : apartKept;
    self[0] = static_cast<float>(static_cast<double>(self[0]) + static_cast<double>(taken));
    return 1;
}

// --- A voice -------------------------------------------------------------------------

uint32_t __fastcall VoiceRelease(char* self, void*);
void __fastcall VoiceSendPlace(char* self, void*);
uint32_t __fastcall VoiceUnmute(char* self, void*);

bool HasGroup(char* self)
{
    Node* const children = At<Node*>(self, voice::kChildren);
    return children->next != children;
}

uint32_t GetParam(char* self, uint32_t id)
{
    return Virtual<uint32_t>(At<void*>(self, voice::kParams), 0x10, id);
}

// The level sent to the sound: the voice's level times its scale; then the
// place again for a voice that leads or follows a group.
void SendLevel(char* self, double level)
{
    void* const params = At<void*>(self, voice::kParams);
    Virtual<void>(params, 0xC, kParamLevel, Ftol(level), uint32_t{0});
    if (HasGroup(self) || At<void*>(self, voice::kParent))
        VoiceSendPlace(self, nullptr);
}

// The game's 0x004F8030 (ret 4): the voice's place, or its group's when it
// leads one.
uint32_t* __fastcall VoiceGetPlace(char* self, void*, uint32_t* out)
{
    if (!HasGroup(self))
        CopyPlace(out, self + voice::kPlace);
    else
        CopyPlace(out, self + voice::kGroupPlace);
    return out;
}

// The game's 0x004F8070: whether the listener is at least the sound's far
// distance from the voice. Never for a grouped follower or the two kinds of
// sound that follow the camera or the listener (what it returns then has a
// zero low byte only).
uint32_t __fastcall VoiceIsFar(char* self, void*)
{
    const uint32_t kind = GetParam(self, kParamKind);
    if (At<void*>(self, voice::kParent) || kind == kKindListener || kind == kKindCamera)
        return kind & 0xFFFFFF00u;
    uint32_t place[3] = {ZeroBits(), ZeroBits(), ZeroBits()};
    VoiceGetPlace(self, nullptr, place);
    char* const listener = Cdecl<char*>(addr::SC4Audio_F4E8490);
    const float dx =
        static_cast<float>(static_cast<double>(At<float>(listener, 0xC)) - static_cast<double>(FromBits(place[0])));
    const double dy = static_cast<double>(At<float>(listener, 0x10)) - static_cast<double>(FromBits(place[1]));
    const double dz = static_cast<double>(At<float>(listener, 0x14)) - static_cast<double>(FromBits(place[2]));
    const double distance = std::sqrt((dz * dz + dy * dy) + static_cast<double>(dx) * static_cast<double>(dx));
    return distance >= static_cast<double>(At<float>(self, voice::kFar)) ? 1u : 0u;
}

// The game's 0x004F8130 (ret 4): gives the sound the manager's "sound ended"
// part to call, or takes it away.
uint8_t __fastcall VoiceWatch(char* self, void*, uint8_t on)
{
    void* ended = nullptr;
    if (!QueryInterface(At<void*>(self, voice::kSound), kEndedIid, &ended)) {
        if (ended)
            Release(ended);
        return 0;
    }
    char* const manager = on ? At<char*>(self, voice::kManager) : nullptr;
    const uint8_t result =
        Virtual<uint8_t>(ended, 0xC, manager ? static_cast<void*>(manager + snd::kSoundEnded) : nullptr);
    if (ended)
        Release(ended);
    return result;
}

// The game's 0x004F8790: the voice's constructor.
char* __fastcall VoiceConstruct(char* self, void*)
{
    for (std::size_t i = 0; i < 0x1C; i += 4)
        At<uint32_t>(self, i) = 0;
    At<uint32_t>(self, voice::kScale) = 0x3F800000;
    At<uint32_t>(self, voice::kPlace) = ZeroBits();
    At<uint32_t>(self, voice::kPlace + 4) = ZeroBits();
    At<uint32_t>(self, voice::kPlace + 8) = ZeroBits();
    for (std::size_t i = 0x2C; i < 0x50; i += 4)
        At<uint32_t>(self, i) = 0;
    At<Node*>(self, voice::kChildren) = NewList();
    At<uint32_t>(self, voice::kParent) = 0;
    At<uint32_t>(self, voice::kGroupPlace) = ZeroBits();
    At<uint32_t>(self, voice::kGroupPlace + 4) = ZeroBits();
    At<uint32_t>(self, voice::kGroupPlace + 8) = ZeroBits();
    return self;
}

// The game's 0x004F88D0: one reference less; the last one ends the voice
// and gives it back to its pool. Returns the references left.
uint32_t __fastcall VoiceRelease(char* self, void*)
{
    const uint32_t references = At<uint32_t>(self, voice::kReferences);
    if (references != 1) {
        At<uint32_t>(self, voice::kReferences) = references - 1;
        return references - 1;
    }
    This<void>(addr::SC4PairList_Destroy, self + voice::kChildren);
    if (void* const manager = At<void*>(self, voice::kManager))
        Release(manager);
    if (void* const params = At<void*>(self, voice::kParams))
        Release(params);
    if (void* const sound = At<void*>(self, voice::kSound))
        Release(sound);
    This<void>(addr::SC4Audio_F4F82A0, reinterpret_cast<void*>(std::uintptr_t{addr::SC4AudioSound_VoicePool}), self);
    return 0;
}

// The game's 0x004F8920 (ret 8): the place of the voice's group, from where
// the listener is and the way it faces (which is made a unit vector in
// place). Each voice of the group, the leader first, weighs in with its
// level times f / (1 - f), f being how squarely it lies on the listener's
// axis; the group's place is the weighted average. When that average lies
// more squarely on the axis than the nearest voice does, it is pushed out
// along its own direction by what the nearest voice is beyond the sound's
// near distance.
void __fastcall VoiceGather(char* self, void*, char* facing, char* listener)
{
    const uint32_t zero = ZeroBits();
    float sum[3] = {0.0f, 0.0f, 0.0f};
    uint32_t nearestPlace[3] = {zero, zero, zero};
    float weight = 0.0f;
    float nearest = FromBits(0x7F7FFFFF);

    {
        const double x = At<float>(facing, 0);
        const double y = At<float>(facing, 4);
        const double z = At<float>(facing, 8);
        const double inverse = One() / std::sqrt((x * x + y * y) + z * z);
        At<float>(facing, 0) = static_cast<float>(inverse * static_cast<double>(At<float>(facing, 0)));
        At<float>(facing, 4) = static_cast<float>(inverse * static_cast<double>(At<float>(facing, 4)));
        At<float>(facing, 8) = static_cast<float>(inverse * static_cast<double>(At<float>(facing, 8)));
    }

    // The leader stands at the front of its own list for the walk.
    {
        Node* const first = At<Node*>(self, voice::kChildren)->next;
        Node* const own = static_cast<Node*>(Allocate(0xC));
        own->value = self;
        Node* const before = first->prev;
        own->next = first;
        own->prev = before;
        before->next = own;
        first->prev = own;
    }

    Node* const children = At<Node*>(self, voice::kChildren);
    for (Node* node = children->next; node != children;) {
        char* const member = static_cast<char*>(node->value);
        uint32_t place[3];
        CopyPlace(place, member + voice::kPlace);
        const float dx =
            static_cast<float>(static_cast<double>(FromBits(place[0])) - static_cast<double>(At<float>(listener, 0)));
        const float dy =
            static_cast<float>(static_cast<double>(FromBits(place[1])) - static_cast<double>(At<float>(listener, 4)));
        const double dzWide = static_cast<double>(FromBits(place[2])) - static_cast<double>(At<float>(listener, 8));
        const float dz = static_cast<float>(dzWide);
        const double distance =
            std::sqrt((dzWide * static_cast<double>(dz) + static_cast<double>(dy) * static_cast<double>(dy)) +
                      static_cast<double>(dx) * static_cast<double>(dx));
        if (distance < static_cast<double>(nearest)) {
            nearest = static_cast<float>(distance);
            nearestPlace[0] = place[0];
            nearestPlace[1] = place[1];
            nearestPlace[2] = place[2];
        }
        const double inverse = One() / distance;
        const double ax = static_cast<double>(dx) * inverse;
        const double ay = static_cast<double>(dy) * inverse;
        const double az = static_cast<double>(dz) * inverse;
        const float square = static_cast<float>(std::fabs((ax * static_cast<double>(At<float>(facing, 0)) +
                                                           az * static_cast<double>(At<float>(facing, 8))) +
                                                          ay * static_cast<double>(At<float>(facing, 4))));
        const int32_t level = static_cast<int32_t>(GetParam(member, kParamLevel));
        node = node->next;
        const double share = ((static_cast<double>(level) * static_cast<double>(kOver1024)) *
                              (static_cast<double>(kOne) / (static_cast<double>(kOne) - static_cast<double>(square)))) *
                             static_cast<double>(square);
        weight = static_cast<float>(static_cast<double>(weight) + share);
        const double wx = static_cast<double>(FromBits(place[0])) * share;
        const float wy = static_cast<float>(static_cast<double>(FromBits(place[1])) * share);
        const float wz = static_cast<float>(static_cast<double>(FromBits(place[2])) * share);
        sum[0] = static_cast<float>(wx + static_cast<double>(sum[0]));
        sum[1] = static_cast<float>(static_cast<double>(wy) + static_cast<double>(sum[1]));
        sum[2] = static_cast<float>(static_cast<double>(wz) + static_cast<double>(sum[2]));
    }

    Unlink(At<Node*>(self, voice::kChildren)->next);

    {
        const double scale = One() / static_cast<double>(weight);
        const double x = static_cast<double>(sum[0]) * scale;
        const double y = static_cast<double>(sum[1]) * scale;
        sum[2] = static_cast<float>(static_cast<double>(sum[2]) * scale);
        At<uint32_t>(self, voice::kGroupPlace + 8) = ToBits(sum[2]);
        At<float>(self, voice::kGroupPlace) = static_cast<float>(x);
        At<float>(self, voice::kGroupPlace + 4) = static_cast<float>(y);
    }

    // From the listener to the group's place, and to the nearest voice.
    const double ox =
        static_cast<double>(At<float>(self, voice::kGroupPlace)) - static_cast<double>(At<float>(listener, 0));
    const double oy =
        static_cast<double>(At<float>(self, voice::kGroupPlace + 4)) - static_cast<double>(At<float>(listener, 4));
    const float oz = static_cast<float>(static_cast<double>(At<float>(self, voice::kGroupPlace + 8)) -
                                        static_cast<double>(At<float>(listener, 8)));
    const float nx = static_cast<float>(static_cast<double>(FromBits(nearestPlace[0])) -
                                        static_cast<double>(At<float>(listener, 0)));
    const float ny = static_cast<float>(static_cast<double>(FromBits(nearestPlace[1])) -
                                        static_cast<double>(At<float>(listener, 4)));
    const float nz = static_cast<float>(static_cast<double>(FromBits(nearestPlace[2])) -
                                        static_cast<double>(At<float>(listener, 8)));

    const float inverseO = static_cast<float>(
        One() / std::sqrt((static_cast<double>(oz) * static_cast<double>(oz) + oy * oy) + ox * ox));
    float ux = static_cast<float>(ox * static_cast<double>(inverseO));
    float uy = static_cast<float>(oy * static_cast<double>(inverseO));
    const float uz = static_cast<float>(static_cast<double>(oz) * static_cast<double>(inverseO));

    const double inverseN = One() / std::sqrt((static_cast<double>(nz) * static_cast<double>(nz) +
                                               static_cast<double>(ny) * static_cast<double>(ny)) +
                                              static_cast<double>(nx) * static_cast<double>(nx));
    const double mx = static_cast<double>(nx) * inverseN;
    const double my = static_cast<double>(ny) * inverseN;
    const double mz = static_cast<double>(nz) * inverseN;

    const double squareGroup = std::fabs((static_cast<double>(ux) * static_cast<double>(At<float>(facing, 0)) +
                                          static_cast<double>(uz) * static_cast<double>(At<float>(facing, 8))) +
                                         static_cast<double>(uy) * static_cast<double>(At<float>(facing, 4)));
    const double squareNearest = std::fabs((mx * static_cast<double>(At<float>(facing, 0)) +
                                            mz * static_cast<double>(At<float>(facing, 8))) +
                                           my * static_cast<double>(At<float>(facing, 4)));
    if (!(squareGroup - squareNearest > 0.0))
        return;

    const double inverseU = One() / std::sqrt((static_cast<double>(uz) * static_cast<double>(uz) +
                                               static_cast<double>(uy) * static_cast<double>(uy)) +
                                              static_cast<double>(ux) * static_cast<double>(ux));
    ux = static_cast<float>(static_cast<double>(ux) * inverseU);
    uy = static_cast<float>(static_cast<double>(uy) * inverseU);
    const double uzWide = inverseU * static_cast<double>(uz);
    const float nearLimit = At<float>(self, voice::kNear);
    const float chosen = nearest < nearLimit ? nearLimit : nearest;
    const double beyond = static_cast<double>(chosen) - static_cast<double>(nearLimit);
    const double ex = static_cast<double>(ux) * beyond;
    const float ey = static_cast<float>(static_cast<double>(uy) * beyond);
    const float ez = static_cast<float>(uzWide * beyond);
    At<float>(self, voice::kGroupPlace) =
        static_cast<float>(ex + static_cast<double>(At<float>(self, voice::kGroupPlace)));
    At<float>(self, voice::kGroupPlace + 4) =
        static_cast<float>(static_cast<double>(ey) + static_cast<double>(At<float>(self, voice::kGroupPlace + 4)));
    At<float>(self, voice::kGroupPlace + 8) =
        static_cast<float>(static_cast<double>(ez) + static_cast<double>(At<float>(self, voice::kGroupPlace + 8)));
}

// The game's 0x004F8CF0 (ret 4): a voice grouped under this one.
uint8_t __fastcall VoiceAddChild(char* self, void*, char* child)
{
    if (!child)
        return 0;
    At<char*>(child, voice::kParent) = self;
    PushBack(At<Node*>(self, voice::kChildren), child);
    return 1;
}

// The game's 0x004F8EA0: the group's place from the camera (0x00B43DD8: its
// place at +0x3C, the place it looks at at +0x48).
void __fastcall VoiceGatherFromCamera(char* self, void*)
{
    char* const camera = Global<char*>(addr::SC4_CameraView);
    if (!camera)
        return;
    uint32_t place[3];
    float facing[3];
    CopyPlace(place, camera + 0x3C);
    facing[0] =
        static_cast<float>(static_cast<double>(At<float>(camera, 0x48)) - static_cast<double>(FromBits(place[0])));
    facing[1] =
        static_cast<float>(static_cast<double>(At<float>(camera, 0x4C)) - static_cast<double>(FromBits(place[1])));
    facing[2] =
        static_cast<float>(static_cast<double>(At<float>(camera, 0x50)) - static_cast<double>(FromBits(place[2])));
    VoiceGather(self, nullptr, reinterpret_cast<char*>(facing), reinterpret_cast<char*>(place));
}

// The game's 0x004F8F00: the group's place from the audio listener (the
// static object's +0x64: its place at +0xC, the place it faces at +0x24).
void __fastcall VoiceGatherFromListener(char* self, void*)
{
    uint32_t place[3] = {ZeroBits(), ZeroBits(), ZeroBits()};
    uint32_t ahead[3] = {ZeroBits(), ZeroBits(), ZeroBits()};
    char* const system = Cdecl<char*>(addr::SC4Sound_StaticObject);
    char* const listener = system ? At<char*>(system, 0x64) : nullptr;
    if (listener) {
        CopyPlace(ahead, listener + 0x24);
        CopyPlace(place, listener + 0xC);
    }
    float facing[3];
    facing[0] = static_cast<float>(static_cast<double>(FromBits(ahead[0])) - static_cast<double>(FromBits(place[0])));
    facing[1] = static_cast<float>(static_cast<double>(FromBits(ahead[1])) - static_cast<double>(FromBits(place[1])));
    facing[2] = static_cast<float>(static_cast<double>(FromBits(ahead[2])) - static_cast<double>(FromBits(place[2])));
    VoiceGather(self, nullptr, reinterpret_cast<char*>(facing), reinterpret_cast<char*>(place));
}

// The game's 0x004F9050: sends the sound its place (parameters 0x20 to
// 0x22, whole numbers), the group's for a leader, worked out again first
// for the two kinds of sound that have a listener. The kind compared here
// for the listener is 0x7DCDED3D, where the rest of the code has
// 0x7DCDED2D.
void __fastcall VoiceSendPlace(char* self, void*)
{
    if (HasGroup(self)) {
        const uint32_t kind = GetParam(self, kParamKind);
        if (kind == kKindCamera)
            VoiceGatherFromCamera(self, nullptr);
        else if (kind == kKindListenerGroup)
            VoiceGatherFromListener(self, nullptr);
    }
    uint32_t place[3];
    if (!HasGroup(self))
        CopyPlace(place, self + voice::kPlace);
    else
        CopyPlace(place, self + voice::kGroupPlace);
    Virtual<void>(At<void*>(self, voice::kParams), 0xC, kParamX, Ftol(FromBits(place[0])), uint32_t{1});
    Virtual<void>(At<void*>(self, voice::kParams), 0xC, kParamY, Ftol(FromBits(place[1])), uint32_t{1});
    Virtual<void>(At<void*>(self, voice::kParams), 0xC, kParamZ, Ftol(FromBits(place[2])), uint32_t{0});
}

// The game's 0x004F9100 (ret 4): a voice out of this one's group. Where it
// is not in it the place is sent again and the answer is false.
uint8_t __fastcall VoiceRemoveChild(char* self, void*, char* child)
{
    Node* const children = At<Node*>(self, voice::kChildren);
    for (Node* node = children->next; node != children; node = node->next) {
        if (node->value == child) {
            At<uint32_t>(child, voice::kParent) = 0;
            Unlink(node);
            return 1;
        }
    }
    VoiceSendPlace(self, nullptr);
    return 0;
}

// A reference taken over as the original's inlined assignment does it.
void AssignReference(void** to, void** from)
{
    if (to == from)
        return;
    void* const old = *to;
    void* const taken = *from;
    if (taken == old)
        return;
    if (taken)
        AddRef(taken);
    *to = taken;
    if (old)
        Release(old);
}

// The game's 0x004F9150: a leader that ends hands its group to the first
// voice in it: the sound, its parameters and the rest of the group.
void __fastcall VoiceHandOver(char* self, void*)
{
    Node* const first = At<Node*>(self, voice::kChildren)->next;
    char* const heir = static_cast<char*>(first->value);
    Unlink(first);
    Node* const rest = At<Node*>(self, voice::kChildren);
    for (Node* node = rest->next; node != rest; node = node->next)
        At<char*>(node->value, voice::kParent) = heir;

    AssignReference(&At<void*>(heir, voice::kSound), &At<void*>(self, voice::kSound));
    AssignReference(&At<void*>(heir, voice::kParams), &At<void*>(self, voice::kParams));

    Node* const mine = At<Node*>(self, voice::kChildren);
    At<Node*>(self, voice::kChildren) = At<Node*>(heir, voice::kChildren);
    At<Node*>(heir, voice::kChildren) = mine;
    At<uint32_t>(heir, voice::kParent) = 0;

    if (void* const sound = At<void*>(self, voice::kSound)) {
        At<void*>(self, voice::kSound) = nullptr;
        Release(sound);
    }
    if (void* const params = At<void*>(self, voice::kParams)) {
        At<void*>(self, voice::kParams) = nullptr;
        Release(params);
    }
    VoiceSendPlace(heir, nullptr);
}

// The game's 0x004F9640 (ret 4): the voice's level.
void __fastcall VoiceSetLevel(char* self, void*, int32_t level)
{
    const double sent = static_cast<double>(level) * static_cast<double>(At<float>(self, voice::kScale));
    At<int32_t>(self, voice::kLevel) = level;
    SendLevel(self, sent);
}

// The game's 0x004F9690 (ret 4): the voice's fade taken a step of the given
// milliseconds; while it moves the level follows it.
void __fastcall VoiceFade(char* self, void*, uint32_t milliseconds)
{
    float* const fade = &At<float>(self, voice::kFade);
    if (!FadeStep(fade, nullptr, static_cast<float>(static_cast<double>(milliseconds))))
        return;
    const int32_t level = Ftol(fade[0]);
    const double sent = static_cast<double>(level) * static_cast<double>(At<float>(self, voice::kScale));
    At<int32_t>(self, voice::kLevel) = level;
    SendLevel(self, sent);
}

// The game's 0x004F9700 (ret 4): the scale on the voice's level.
void __fastcall VoiceSetScale(char* self, void*, uint32_t scale)
{
    const double level = static_cast<double>(At<int32_t>(self, voice::kLevel));
    At<uint32_t>(self, voice::kScale) = scale;
    SendLevel(self, level * static_cast<double>(FromBits(scale)));
}

// The game's 0x004F9750: one reason to be mute less. With none left a voice
// that follows no leader is started over and played again. Returns the
// reasons left.
uint32_t __fastcall VoiceUnmute(char* self, void*)
{
    if (--At<int32_t>(self, voice::kMuted) != 0 || At<void*>(self, voice::kParent))
        return At<uint32_t>(self, voice::kMuted);
    VoiceWatch(self, nullptr, 1);
    void* restart = nullptr;
    if (QueryInterface(At<void*>(self, voice::kSound), kRestartIid, &restart))
        Virtual<void>(restart, 0x30);
    Virtual<void>(At<void*>(self, voice::kParams), 0xC, kParamState, uint32_t{3}, uint32_t{0});
    Virtual<void>(At<void*>(self, voice::kSound), 0x14, At<uint32_t>(self, voice::kInstance),
                  At<uint32_t>(self, voice::kLevel), uint32_t{0x200}, uint32_t{0});
    VoiceSendPlace(self, nullptr);
    VoiceSetLevel(self, nullptr, At<int32_t>(self, voice::kLevel));
    if (restart)
        Release(restart);
    return At<uint32_t>(self, voice::kMuted);
}

// One more reason to be mute; the first one stops a voice that follows no
// leader through the sound's given slot (0x20 or 0x24).
void MuteThrough(char* self, std::size_t slot)
{
    if (++At<int32_t>(self, voice::kMuted) == 1 && !At<void*>(self, voice::kParent)) {
        VoiceWatch(self, nullptr, 0);
        Virtual<void>(At<void*>(self, voice::kSound), slot);
    }
}

// The game's 0x004F97E0 (ret 4): the voice out of earshot (muted) or back
// in. Returns the reasons to be mute.
uint32_t __fastcall VoiceSetOutOfEarshot(char* self, void*, uint8_t out)
{
    if (out) {
        At<uint32_t>(self, voice::kFlags) |= kOutOfEarshot;
        MuteThrough(self, 0x20);
        return At<uint32_t>(self, voice::kMuted);
    }
    At<uint32_t>(self, voice::kFlags) &= ~kOutOfEarshot;
    return VoiceUnmute(self, nullptr);
}

// The game's 0x004F9B90: a grouped sound's leader (or a grouped sound
// alone) is muted when the listener is too far and let go when it is not.
void __fastcall VoiceCheckEarshot(char* self, void*)
{
    if (GetParam(self, kParamGrouped) == 0)
        return;
    const uint32_t kind = GetParam(self, kParamKind);
    if (At<void*>(self, voice::kParent) || kind == kKindListener || kind == kKindCamera)
        return;
    if (!((At<uint32_t>(self, voice::kFlags) >> 3) & 1) && static_cast<uint8_t>(VoiceIsFar(self, nullptr))) {
        VoiceSetOutOfEarshot(self, nullptr, 1);
        return;
    }
    if (!((At<uint32_t>(self, voice::kFlags) >> 3) & 1))
        return;
    if (static_cast<uint8_t>(VoiceIsFar(self, nullptr)))
        return;
    At<uint32_t>(self, voice::kFlags) &= ~kOutOfEarshot;
    VoiceUnmute(self, nullptr);
}

// The game's 0x004F9C10: a voice that stops with the simulation is muted
// while the simulator (0x00B43D5C, slot 15) says it is paused.
void __fastcall VoiceCheckPause(char* self, void*)
{
    void* simulator;
    const uint32_t flags = At<uint32_t>(self, voice::kFlags);
    if (!((flags >> 4) & 1) && ((flags >> 5) & 1)) {
        simulator = Global<void*>(addr::SC4_SimulatorService);
        if (simulator) {
            if (Virtual<uint8_t>(simulator, 0x3C)) {
                At<uint32_t>(self, voice::kFlags) |= kHeldByPause;
                MuteThrough(self, 0x20);
                return;
            }
            simulator = Global<void*>(addr::SC4_SimulatorService);
        }
    } else {
        simulator = Global<void*>(addr::SC4_SimulatorService);
    }
    const uint32_t now = At<uint32_t>(self, voice::kFlags);
    if (!((now >> 4) & 1))
        return;
    if (((now >> 5) & 1) && simulator && Virtual<uint8_t>(simulator, 0x3C))
        return;
    At<uint32_t>(self, voice::kFlags) &= ~kHeldByPause;
    VoiceUnmute(self, nullptr);
}

// --- cSC4AudioSoundManager: what callers ask for -------------------------------------

// A message of the sound service made and posted to it.
uint8_t Post(char* self, uint32_t message, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    void* made = nullptr;
    void* const service = At<void*>(self, snd::kService);
    if (!service)
        return 0;
    if (!Virtual<uint8_t>(service, 0x34, message, kSoundMessageIid, &made, a, b, c, d)) {
        if (made)
            Release(made);
        return 0;
    }
    Virtual<void>(At<void*>(self, snd::kService), 0x20, made);
    if (made)
        Release(made);
    return 1;
}

// The game's 0x004F7B70 (ret 8): QueryInterface of the main part.
uint8_t __fastcall QueryInterfaceMain(char* self, void*, uint32_t iid, void** out)
{
    if (iid != 1 && iid != kManagerIid)
        return 0;
    *out = self;
    AddRef(self);
    return 1;
}

// The game's 0x004F8890, 0x004F88A0 and 0x004F88B0: the same from the three
// other parts.
uint8_t __fastcall QueryInterfaceTick(char* part, void*, uint32_t iid, void** out)
{
    return QueryInterfaceMain(part - snd::kTick, nullptr, iid, out);
}

uint8_t __fastcall QueryInterfaceSoundEnded(char* part, void*, uint32_t iid, void** out)
{
    return QueryInterfaceMain(part - snd::kSoundEnded, nullptr, iid, out);
}

uint8_t __fastcall QueryInterfaceTarget(char* part, void*, uint32_t iid, void** out)
{
    return QueryInterfaceMain(part - snd::kTarget, nullptr, iid, out);
}

// The game's 0x004F7CB0 (ret 4): asks for the master level.
uint8_t __fastcall AskMasterLevel(char* self, void*, uint32_t level)
{
    return Post(self, kMasterLevel, level, 0, 0, 0);
}

// The game's 0x004F7D20 (ret 8): asks for a sound to start under an
// instance number (SC4Sound::NextInstance gives callers one).
uint8_t __fastcall AskStart(char* self, void*, uint32_t soundId, uint32_t instance)
{
    return Post(self, kStart, soundId, instance, 0, 0);
}

// The game's 0x004F7D90 (ret 0x10): asks for an instance's place, the three
// floats cut to whole numbers (z first, then y, then x).
uint8_t __fastcall AskPlace(char* self, void*, uint32_t instance, float x, float y, float z)
{
    if (!At<void*>(self, snd::kService))
        return 0;
    const int32_t wholeZ = Ftol(z);
    const int32_t wholeY = Ftol(y);
    const int32_t wholeX = Ftol(x);
    return Post(self, kSetPlace, instance, static_cast<uint32_t>(wholeX), static_cast<uint32_t>(wholeY),
                static_cast<uint32_t>(wholeZ));
}

// The game's 0x004F7E20: asks for every voice to be set again.
uint8_t __fastcall AskRefresh(char* self, void*)
{
    return Post(self, kRefresh, 0, 0, 0, 0);
}

// The game's 0x004F7E90 (ret 4): asks for every voice to stop.
uint8_t __fastcall AskStopAll(char* self, void*, uint8_t how)
{
    return Post(self, kStopAll, static_cast<uint32_t>(how), 0, 0, 0);
}

// The game's 0x004F7F00 (ret 4): the game's message 0x4348B111: asks for
// all sounds to be let go (its first number not 0) or muted.
uint8_t __fastcall AskLetGo(char* self, void*, void* message)
{
    void* standard = nullptr;
    if (!At<void*>(self, snd::kService))
        return 0;
    if (!QueryInterface(message, kStandardMessageIid, &standard)) {
        if (standard)
            Release(standard);
        return 0;
    }
    const uint32_t on = Virtual<uint32_t>(standard, 0x24) != 0 ? 1u : 0u;
    void* made = nullptr;
    if (!Virtual<uint8_t>(At<void*>(self, snd::kService), 0x34, kLetGo, kSoundMessageIid, &made, on, uint32_t{0},
                          uint32_t{0}, uint32_t{0})) {
        if (made)
            Release(made);
        if (standard)
            Release(standard);
        return 0;
    }
    Virtual<void>(At<void*>(self, snd::kService), 0x20, made);
    if (made)
        Release(made);
    if (standard)
        Release(standard);
    return 1;
}

// The game's 0x004F7FD0 (cdecl): what the tick gives the sound service's
// slot 32 to call with every sound while the countdown runs: the sound's
// value of slot 20 set to 0 and back (slot 21).
uint8_t __cdecl NudgeSound(void* object)
{
    void* sound = nullptr;
    if (!object)
        return 1;
    if (QueryInterface(object, kSoundIid, &sound)) {
        const uint32_t kept = Virtual<uint32_t>(sound, 0x50);
        Virtual<void>(sound, 0x54, uint32_t{0});
        Virtual<void>(sound, 0x54, kept);
    }
    if (sound)
        Release(sound);
    return 1;
}

// --- cSC4AudioSoundManager: the voices ---------------------------------------------------

// The node of an instance number in the map of voices (next at +0, key at
// +4, voice at +8), as every lookup has it inline.
char* FindNode(char* self, uint32_t instance)
{
    const uint32_t buckets = static_cast<uint32_t>(
        static_cast<int32_t>(At<uint32_t>(self, snd::kBucketsEnd) - At<uint32_t>(self, snd::kBuckets)) >> 2);
    char* node = At<char**>(self, snd::kBuckets)[instance % buckets];
    while (node && At<uint32_t>(node, 4) != instance)
        node = At<char*>(node, 0);
    return node;
}

// The voice of a node, with a reference for the caller.
char* Hold(char* node)
{
    char* const found = At<char*>(node, 8);
    if (found)
        ++At<uint32_t>(found, voice::kReferences);
    return found;
}

uint8_t __fastcall StopVoice(char* self, void*, uint32_t instance, uint32_t how);
uint8_t __fastcall RefreshAll(char* self, void*);
uint8_t __fastcall ReadTuning(char* self, void*);

// The game's 0x004F8510 (ret 0xC): the last voice in the list with the
// given sound ID (referenced) and the least of start minus clock over all
// of them. True when there is one.
uint32_t __fastcall FindLatest(char* self, void*, uint32_t soundId, char** out, int32_t* age)
{
    if (!out)
        return 0;
    *out = nullptr;
    *age = 0x7FFFFFFF;
    Node* const voices = At<Node*>(self, snd::kVoices);
    for (Node* node = voices->next; node != voices; node = node->next) {
        if (At<uint32_t>(node->value, voice::kSoundId) != soundId)
            continue;
        const int32_t since =
            static_cast<int32_t>(At<uint32_t>(node->value, voice::kStarted) - At<uint32_t>(self, snd::kClock));
        *age = since < *age ? since : *age;
        *out = static_cast<char*>(node->value);
    }
    if (*out)
        ++At<uint32_t>(*out, voice::kReferences);
    return *age != 0x7FFFFFFF ? 1u : 0u;
}

// The game's 0x004F8590 (ret 0xC): how many voices have the given sound ID,
// and the last of them (referenced).
uint32_t __fastcall CountOfSound(char* self, void*, uint32_t soundId, char** out, int32_t* count)
{
    if (!out)
        return 0;
    *out = nullptr;
    *count = 0;
    Node* const voices = At<Node*>(self, snd::kVoices);
    for (Node* node = voices->next; node != voices; node = node->next) {
        if (At<uint32_t>(node->value, voice::kSoundId) != soundId)
            continue;
        ++*count;
        *out = static_cast<char*>(node->value);
    }
    if (*out)
        ++At<uint32_t>(*out, voice::kReferences);
    return *count > 0 ? 1u : 0u;
}

// The game's 0x004F85F0 (ret 8): the first voice with the given sound ID
// that follows no leader (referenced).
uint8_t __fastcall FindLeader(char* self, void*, uint32_t soundId, char** out)
{
    if (!out)
        return 0;
    *out = nullptr;
    Node* const voices = At<Node*>(self, snd::kVoices);
    for (Node* node = voices->next; node != voices; node = node->next) {
        char* const item = static_cast<char*>(node->value);
        if (At<uint32_t>(item, voice::kSoundId) == soundId && At<uint32_t>(item, voice::kParent) == 0) {
            *out = item;
            ++At<uint32_t>(item, voice::kReferences);
            return 1;
        }
    }
    return 0;
}

// The game's 0x004F94C0 (ret 8): the voice of an instance number ends: out
// of the list and the map, its group handed to the next voice when it led
// one, else its sound stopped (slot 9 when `how` is set, slot 8 for a
// grouped sound otherwise; a plain sound is left to play out).
uint8_t __fastcall StopVoice(char* self, void*, uint32_t instance, uint32_t how)
{
    char* const node = FindNode(self, instance);
    if (!node)
        return 0;
    char* const ended = Hold(node);

    Node* const voices = At<Node*>(self, snd::kVoices);
    for (Node* item = voices->next; item != voices; item = item->next) {
        if (item->value == ended) {
            Unlink(item);
            break;
        }
    }
    struct {
        char* node;
        char* map;
    } where = {node, self + snd::kMap};
    This<void>(addr::SC4Uint32HashMap_EraseAt, self + snd::kMap, &where);
    --At<int32_t>(self, snd::kVoiceCount);

    if (HasGroup(ended)) {
        VoiceHandOver(ended, nullptr);
    } else {
        VoiceWatch(ended, nullptr, 0);
        if (Virtual<uint8_t>(At<void*>(ended, voice::kSound), 0x1C)) {
            if (static_cast<uint8_t>(how))
                Virtual<void>(At<void*>(ended, voice::kSound), 0x24);
            else if (GetParam(ended, kParamGrouped) != 0)
                Virtual<void>(At<void*>(ended, voice::kSound), 0x20);
        }
        if (GetParam(ended, kParamGrouped) != 0) {
            if (At<uint32_t>(ended, voice::kParent) == 0)
                --At<int32_t>(self, snd::kGroups);
        } else {
            --At<int32_t>(self, snd::kPlain);
        }
    }

    if (char* const parent = At<char*>(ended, voice::kParent))
        VoiceRemoveChild(parent, nullptr, ended);
    if (void* const params = At<void*>(ended, voice::kParams)) {
        At<void*>(ended, voice::kParams) = nullptr;
        Release(params);
    }
    if (void* const sound = At<void*>(ended, voice::kSound)) {
        At<void*>(ended, voice::kSound) = nullptr;
        Release(sound);
    }
    VoiceRelease(ended, nullptr);
    VoiceRelease(ended, nullptr);
    return 1;
}

// The game's 0x004F9610 (ret 4): every voice ends.
void __fastcall StopAll(char* self, void*, uint32_t how)
{
    while (At<Node*>(self, snd::kVoices)->next != At<Node*>(self, snd::kVoices)) {
        char* const first = static_cast<char*>(At<Node*>(self, snd::kVoices)->next->value);
        StopVoice(self, nullptr, At<uint32_t>(first, voice::kInstance), how);
    }
}

// The game's 0x004F8DD0 (ret 4): the voice of an instance number is started
// over (slot 12 of the sound's interface 0xBB1BE367) and its level sent
// again.
uint8_t __fastcall StartOver(char* self, void*, uint32_t instance)
{
    char* const node = FindNode(self, instance);
    if (!node)
        return 0;
    char* const found = Hold(node);
    void* restart = nullptr;
    if (QueryInterface(At<void*>(found, voice::kSound), kRestartIid, &restart)) {
        Virtual<void>(restart, 0x30);
        void* params = nullptr;
        if (QueryInterface(restart, kParamsIid, &params)) {
            const uint32_t level = Virtual<uint32_t>(params, 0x10, kParamLevel);
            Virtual<void>(params, 0xC, kParamLevel, level, uint32_t{0});
        }
        if (params)
            Release(params);
    }
    if (restart)
        Release(restart);
    VoiceRelease(found, nullptr);
    return 1;
}

// The game's 0x004F9AF0 (ret 8): the level of an instance.
uint8_t __fastcall SetVoiceLevel(char* self, void*, uint32_t instance, int32_t level)
{
    char* const node = FindNode(self, instance);
    if (!node)
        return 0;
    char* const found = Hold(node);
    VoiceSetLevel(found, nullptr, level);
    At<uint32_t>(found, voice::kFlags) |= kLevelSet;
    VoiceRelease(found, nullptr);
    return 1;
}

// The game's 0x004FA260 (ret 0x10): the place of an instance (three floats'
// bits).
uint8_t __fastcall SetVoicePlace(char* self, void*, uint32_t instance, uint32_t x, uint32_t y, uint32_t z)
{
    char* const node = FindNode(self, instance);
    if (!node)
        return 0;
    char* const found = Hold(node);
    At<uint32_t>(found, voice::kPlace) = x;
    At<uint32_t>(found, voice::kPlace + 4) = y;
    At<uint32_t>(found, voice::kPlace + 8) = z;
    VoiceSendPlace(found, nullptr);
    if (char* const parent = At<char*>(found, voice::kParent))
        VoiceSendPlace(parent, nullptr);
    VoiceCheckEarshot(found, nullptr);
    At<uint32_t>(found, voice::kFlags) |= kPlaceSet;
    VoiceRelease(found, nullptr);
    return 1;
}

// The game's 0x004F9D70 (ret 0xC): a voice starts. A voice already under
// the instance number ends first. A plain sound is not started again within
// 50 milliseconds of its last start, nor when as many plain voices play as
// the limit at +0x68 or as many of this sound as the limit at +0x64. The
// sound comes from the factory (slot 18); instance 0 gets the next number.
// A grouped sound joins the leader of its sound ID when there is one, else
// it is played (slot 5 of the sound: instance, the master level, 0x200, 0).
// After a plain sound starts, every voice of its ID is scaled by the curve
// at +0x70 of how many played before it.
uint8_t __fastcall StartVoice(char* self, void*, uint32_t soundId, uint32_t instance, uint32_t flags)
{
    if (FindNode(self, instance))
        StopVoice(self, nullptr, instance, 1);

    char* other = nullptr;
    int32_t number;
    if (FindLatest(self, nullptr, soundId, &other, &number) && GetParam(other, kParamGrouped) == 0 &&
        At<uint32_t>(self, snd::kClock) - At<uint32_t>(other, voice::kStarted) < 0x32) {
        VoiceRelease(other, nullptr);
        return 0;
    }
    if (other) {
        char* const held = other;
        other = nullptr;
        VoiceRelease(held, nullptr);
    }

    if (CountOfSound(self, nullptr, soundId, &other, &number) && GetParam(other, kParamGrouped) == 0) {
        if (At<uint32_t>(self, snd::kPlain) >= At<uint32_t>(self, snd::kMostPlain) ||
            static_cast<uint32_t>(number) >= At<uint32_t>(self, snd::kMostOfOne)) {
            VoiceRelease(other, nullptr);
            return 0;
        }
    }
    char* const counted = other;

    void* sound = nullptr;
    void* params = nullptr;
    if (!Virtual<uint8_t>(At<void*>(self, snd::kFactory), 0x48, soundId, uint32_t{0}, uint32_t{0}, kSoundIid,
                          &sound)) {
        if (params)
            Release(params);
        if (sound)
            Release(sound);
        if (counted)
            VoiceRelease(counted, nullptr);
        return 0;
    }
    if (!QueryInterface(sound, kParamsIid, &params)) {
        if (params)
            Release(params);
        if (sound)
            Release(sound);
        if (counted)
            VoiceRelease(counted, nullptr);
        return 0;
    }

    if (instance == 0) {
        uint32_t next = Global<uint32_t>(addr::SC4Sound_InstanceCounter);
        if (next == 0)
            next = 1;
        instance = next;
        Global<uint32_t>(addr::SC4Sound_InstanceCounter) = next + 1;
    }

    char* const made = VoiceConstruct(static_cast<char*>(PoolTake(addr::SC4AudioSound_VoicePool)), nullptr);
    if (made)
        ++At<uint32_t>(made, voice::kReferences);
    void* const before = At<void*>(made, voice::kManager);
    if (self != before) {
        AddRef(self);
        At<char*>(made, voice::kManager) = self;
        if (before)
            Release(before);
    }
    This<void*>(addr::SC4RefPtr_AssignFrom, made + voice::kSound, &sound);
    This<void*>(addr::SC4RefPtr_AssignFrom, made + voice::kParams, &params);
    At<uint32_t>(made, voice::kInstance) = instance;
    At<uint32_t>(made, voice::kSoundId) = soundId;
    At<uint32_t>(made, voice::kLevel) = 0x400;
    At<uint32_t>(made, voice::kStarted) = At<uint32_t>(self, snd::kClock);
    if (flags & 1)
        At<uint32_t>(made, voice::kFlags) |= kStopsWithPause;

    {
        const int32_t distance = static_cast<int32_t>(Virtual<uint32_t>(params, 0x10, kParamDistance));
        const double nearest =
            (static_cast<double>(distance) * static_cast<double>(kHundredth)) * static_cast<double>(kHundred);
        At<float>(made, voice::kNear) = static_cast<float>(nearest);
        At<float>(made, voice::kFar) = static_cast<float>(nearest * static_cast<double>(kFour));
    }
    VoiceWatch(made, nullptr, 1);

    if (other) {
        char* const held = other;
        other = nullptr;
        VoiceRelease(held, nullptr);
    }
    char* leader = nullptr;
    const uint8_t led = FindLeader(self, nullptr, soundId, &leader);
    if (led && GetParam(leader, kParamGrouped) != 0) {
        VoiceAddChild(leader, nullptr, made);
    } else {
        Virtual<void>(At<void*>(made, voice::kParams), 0xC, kParamState, uint32_t{3}, uint32_t{0});
        if (!Virtual<uint8_t>(sound, 0x14, At<uint32_t>(made, voice::kInstance), At<uint32_t>(self, snd::kLevel),
                              uint32_t{0x200}, uint32_t{0})) {
            VoiceRelease(made, nullptr);
            if (params)
                Release(params);
            if (sound)
                Release(sound);
            if (leader)
                VoiceRelease(leader, nullptr);
            return 0;
        }
        if (GetParam(made, kParamGrouped) != 0) {
            if (At<uint32_t>(made, voice::kParent) == 0)
                ++At<int32_t>(self, snd::kGroups);
        } else {
            ++At<int32_t>(self, snd::kPlain);
        }
    }

    ++At<int32_t>(self, snd::kVoiceCount);
    ++At<uint32_t>(made, voice::kReferences);
    *This<char**>(addr::SC4Uint32HashMap_Lookup, self + snd::kMap, &instance) = made;
    char* listed = made;
    This<void>(addr::SC4StlList_PushBack, self + snd::kVoices, &listed);

    if (number != 0 && GetParam(made, kParamGrouped) == 0) {
        const double value =
            This<double>(addr::SC4Curve_Evaluate, self + snd::kCurve, static_cast<float>(static_cast<double>(number)));
        const uint32_t scale = ToBits(static_cast<float>(value * static_cast<double>(kOver1024)));
        Node* const voices = At<Node*>(self, snd::kVoices);
        for (Node* node = voices->next; node != voices; node = node->next) {
            char* const item = static_cast<char*>(node->value);
            if (At<uint32_t>(item, voice::kSoundId) == soundId)
                VoiceSetScale(item, nullptr, scale);
        }
    }

    VoiceCheckPause(made, nullptr);
    VoiceRelease(made, nullptr);
    if (params)
        Release(params);
    if (sound)
        Release(sound);
    if (leader)
        VoiceRelease(leader, nullptr);
    return 1;
}

// The game's 0x004FA180: every voice gets its level sent again and is
// checked for earshot and for the pause.
uint8_t __fastcall RefreshAll(char* self, void*)
{
    Node* const voices = At<Node*>(self, snd::kVoices);
    for (Node* node = voices->next; node != voices; node = node->next) {
        char* const item = static_cast<char*>(node->value);
        SendLevel(item, static_cast<double>(At<int32_t>(item, voice::kLevel)) *
                            static_cast<double>(At<float>(item, voice::kScale)));
        VoiceCheckEarshot(item, nullptr);
        VoiceCheckPause(item, nullptr);
    }
    return 1;
}

// --- cSC4AudioSoundManager: the messages -----------------------------------------------

// The message's standard part (slot 6 of the sound message), referenced, or
// null.
void* StandardOf(void* message)
{
    void* const standard = Virtual<void*>(message, 0x18);
    if (standard)
        AddRef(standard);
    return standard;
}

// The game's 0x004F83E0 (ret 4): message 0x4A4FE6F6: the object the message
// carries (slot 32 of its standard part) is asked for interface 0xEA4FC425
// and shown every voice (slot 3) until it says no. The original's exception
// frame only releases the two references.
uint8_t __fastcall OnVisit(char* self, void*, void* message)
{
    void* const standard = Virtual<void*>(message, 0x18);
    void* carried = nullptr;
    This<void*>(addr::SC4RefHolder_SC4RefHolder, &carried, Virtual<void*>(standard, 0x80));
    void* visitor = nullptr;
    if (!QueryInterface(carried, kVisitorIid, This<void**>(addr::SC4RefPtr_Reset, &visitor))) {
        if (visitor)
            Release(visitor);
        Release(carried);
        return 0;
    }
    Node* const voices = At<Node*>(self, snd::kVoices);
    for (Node* node = voices->next; node != voices; node = node->next) {
        if (!Virtual<uint8_t>(visitor, 0xC, node->value))
            break;
    }
    if (visitor)
        Release(visitor);
    if (carried)
        Release(carried);
    return 1;
}

// The game's 0x004F9000 (ret 4): message 0x4A4D35E0.
uint8_t __fastcall OnStartOver(char* self, void*, void* message)
{
    if (!message)
        return 0;
    void* const standard = StandardOf(message);
    if (!standard)
        return 0;
    StartOver(self, nullptr, Virtual<uint32_t>(standard, 0x24));
    Release(standard);
    return 1;
}

// The game's 0x004F9990 (ret 4): messages 2 and 0x6A451908.
uint8_t __fastcall OnStop(char* self, void*, void* message)
{
    if (!message)
        return 0;
    void* const standard = StandardOf(message);
    if (!standard)
        return 0;
    const uint32_t how = Virtual<uint32_t>(standard, 0x38) != 0 ? 1u : 0u;
    const uint32_t instance = Virtual<uint32_t>(standard, 0x24);
    StopVoice(self, nullptr, instance, how);
    Release(standard);
    return 1;
}

// The game's 0x004F99F0 (ret 4): message 0x6A95CBA2. With 0 every voice
// gets a reason to be mute (stopped through the sound's slot 9) and the
// master level 0 is asked for; else each loses one, the master level of
// the preferences is asked for (0 with sound off) and the tick's countdown
// starts at 50.
uint8_t __fastcall OnLetGo(char* self, void*, void* message)
{
    if (!message || !At<void*>(self, snd::kService))
        return 0;
    void* const standard = StandardOf(message);
    if (!standard)
        return 0;
    const bool mute = Virtual<uint32_t>(standard, 0x24) == 0;
    Node* const voices = At<Node*>(self, snd::kVoices);
    for (Node* node = voices->next; node != voices; node = node->next) {
        char* const item = static_cast<char*>(node->value);
        if (mute)
            MuteThrough(item, 0x24);
        else
            VoiceUnmute(item, nullptr);
    }
    char* const preferences = Virtual<char*>(Global<void*>(addr::SC4_Service43C94), 0x98);
    bool count;
    if (At<uint8_t>(preferences, 4) && !mute) {
        AskMasterLevel(self, nullptr, At<uint32_t>(preferences, 8));
        count = true;
    } else {
        AskMasterLevel(self, nullptr, 0);
        count = !mute;
    }
    if (count)
        At<uint32_t>(self, snd::kCountdown) = 0x32;
    Release(standard);
    return 1;
}

// The game's 0x004F9D20 (ret 4): message 0x4A451915.
uint8_t __fastcall OnSetLevel(char* self, void*, void* message)
{
    if (!message)
        return 0;
    void* const standard = StandardOf(message);
    if (!standard)
        return 0;
    const int32_t level = Virtual<int32_t>(standard, 0x38);
    const uint32_t instance = Virtual<uint32_t>(standard, 0x24);
    SetVoiceLevel(self, nullptr, instance, level);
    Release(standard);
    return 1;
}

// The game's 0x004FA1F0 (ret 4): messages 1 and 0xEA451903.
uint8_t __fastcall OnStart(char* self, void*, void* message)
{
    if (!message)
        return 0;
    void* const standard = StandardOf(message);
    if (!standard)
        return 0;
    uint32_t flags = 0;
    if (Virtual<uint8_t>(standard, 0x48))
        flags = Virtual<uint32_t>(standard, 0x4C);
    const uint32_t instance = Virtual<uint32_t>(standard, 0x38);
    const uint32_t soundId = Virtual<uint32_t>(standard, 0x24);
    StartVoice(self, nullptr, soundId, instance, flags);
    Release(standard);
    return 1;
}

// The game's 0x004FA4C0 (ret 4): message 0xCA45190F: the three whole
// numbers made floats again.
uint8_t __fastcall OnSetPlace(char* self, void*, void* message)
{
    if (!message)
        return 0;
    void* const standard = StandardOf(message);
    if (!standard)
        return 0;
    const float z = static_cast<float>(Virtual<int32_t>(standard, 0x60));
    const float y = static_cast<float>(Virtual<int32_t>(standard, 0x4C));
    const float x = static_cast<float>(Virtual<int32_t>(standard, 0x38));
    const uint32_t instance = Virtual<uint32_t>(standard, 0x24);
    SetVoicePlace(self, nullptr, instance, ToBits(x), ToBits(y), ToBits(z));
    Release(standard);
    return 1;
}

// The game's 0x004FA550 (ret 4): message 0x2A5B69C0. Always false.
uint8_t __fastcall OnMasterLevel(char* self, void*, void* message)
{
    if (message) {
        if (void* const standard = StandardOf(message)) {
            At<uint32_t>(self, snd::kLevel) = Virtual<uint32_t>(standard, 0x24);
            RefreshAll(self, nullptr);
            Release(standard);
        }
    }
    return 0;
}

// The game's 0x004FA590 (ret 4): slot 3 of the main part: the sound
// service's messages.
uint8_t __fastcall DoMessage(char* self, void*, void* message)
{
    switch (Virtual<uint32_t>(message, 0x10)) {
    case 1:
    case kStart:
        return OnStart(self, nullptr, message);
    case 2:
    case kStop:
        return OnStop(self, nullptr, message);
    case kMasterLevel:
        return OnMasterLevel(self, nullptr, message);
    case kSetLevel:
        return OnSetLevel(self, nullptr, message);
    case kStartOver:
        return OnStartOver(self, nullptr, message);
    case kVisit:
        return OnVisit(self, nullptr, message);
    case kRefresh:
        return RefreshAll(self, nullptr);
    case kLetGo:
        OnLetGo(self, nullptr, message);
        return 0;
    case kSetPlace:
        return OnSetPlace(self, nullptr, message);
    case kStopAll: {
        const uint32_t first = Virtual<uint32_t>(Virtual<void*>(message, 0x18), 0x24);
        StopAll(self, nullptr, (first & 0xFFFFFF00u) | (first != 0 ? 1u : 0u));
        return 0;
    }
    default:
        return 0;
    }
}

// The game's 0x004F9430 (ret 4): the message target at +0xC: the game's
// messages.
uint8_t __fastcall TargetDoMessage(char* part, void*, void* message)
{
    char* const self = part - snd::kTarget;
    switch (Virtual<uint32_t>(message, 0x10)) {
    case 0x4A7FB7E9:
    case 0x8A4A69C7:
    case 0xAA7FB7E0:
        return AskRefresh(self, nullptr);
    case 0x28E9397C:
    case 0x26D31EC2:
        return AskStopAll(self, nullptr, 0);
    case 0x28E9397B:
        return 1;
    case 0x4348B111:
        return AskLetGo(self, nullptr, message);
    case 0xC53D10AA:
        return ReadTuning(self, nullptr);
    default:
        return 0;
    }
}

// The game's 0x004F98F0 (ret 4): the "sound ended" part at +8: the voice
// whose instance number the sound carries (parameter 0x34) ends.
void __fastcall SoundEnded(char* part, void*, void* sound)
{
    char* const self = part - snd::kSoundEnded;
    void* params = nullptr;
    if (QueryInterface(sound, kParamsIid, &params)) {
        if (char* const node = FindNode(self, Virtual<uint32_t>(params, 0x10, kParamInstance))) {
            char* const ended = Hold(node);
            StopVoice(self, nullptr, At<uint32_t>(ended, voice::kInstance), 1);
            VoiceRelease(ended, nullptr);
        }
    }
    if (params)
        Release(params);
}

// The game's 0x004F9CA0 (ret 4): the tick part at +4: the clock goes on by
// the milliseconds given, the countdown runs (never from above 50) with
// every sound nudged, and every voice's fade takes its step.
void __fastcall Tick(char* part, void*, uint32_t milliseconds)
{
    char* const self = part - snd::kTick;
    At<uint32_t>(self, snd::kClock) += milliseconds;
    const int32_t countdown = At<int32_t>(self, snd::kCountdown);
    if (countdown != 0) {
        At<int32_t>(self, snd::kCountdown) = (countdown > 0x32 ? 0x32 : countdown) - 1;
        Virtual<void>(At<void*>(self, snd::kService), 0x80, std::uintptr_t{addr::SC4Audio_F4F7FD0}, uint32_t{0},
                      self);
    }
    ++At<int32_t>(self, snd::kDepth);
    Node* const voices = At<Node*>(self, snd::kVoices);
    for (Node* node = voices->next; node != voices; node = node->next)
        VoiceFade(static_cast<char*>(node->value), nullptr, milliseconds);
    --At<int32_t>(self, snd::kDepth);
}

// --- cSC4AudioSoundManager: making and unmaking ------------------------------------------

// The game's 0x004F9250: cSC4AudioSoundManager::_ReadTuningValues. The
// three limits and the curve get their defaults (3, 10, 30; one point: 0
// gives 1024), then the exemplar 0x6534284A / 0xCA25875D / 0x6A359C5C gives
// the curve (property 0x6A946A50) and the limits (0xAABAA0D3, 0x0A946A57,
// 0x4A946A5D). Without the exemplar an error text is made and thrown away,
// and the answer is false.
uint8_t __fastcall ReadTuning(char* self, void*)
{
    PointVector points{};
    uint8_t allocator;
    float value[2];
    At<uint32_t>(self, snd::kLimit6C) = 0x1E;
    At<uint32_t>(self, snd::kMostOfOne) = 3;
    At<uint32_t>(self, snd::kMostPlain) = 0xA;
    value[0] = 0.0f;
    value[1] = 1024.0f;
    This<void>(addr::SC4PointVector_InsertN, &points, static_cast<float*>(nullptr), static_cast<const float*>(value),
               static_cast<void*>(&allocator), uint32_t{1}, uint32_t{1});
    char* const curve = self + snd::kCurve;
    This<uint8_t>(addr::SC4Curve_SetPoints, curve, &points);

    void* const resources = Global<void*>(addr::SC4_ResourceManager);
    if (resources)
        AddRef(resources);
    void* exemplar = nullptr;
    uint32_t key[3] = {0x6534284A, 0xCA25875D, 0x6A359C5C};
    if (!Virtual<uint8_t>(resources, 0xC, key, kExemplarIid, &exemplar, uint32_t{0}, uint32_t{0})) {
        GameString text;
        text.begin = nullptr;
        text.end = nullptr;
        text.capacity = nullptr;
        char* const buffer = static_cast<char*>(Allocate(8));
        text.begin = buffer;
        text.end = buffer;
        text.capacity = buffer + 8;
        *buffer = 0;
        text.vtable = addr::SC4String_Vtable;
        text.zero = 0;
        Cdecl<void>(addr::cRZBaseString_Sprintf, &text,
                    "Error cSC4AudioSoundManager::_ReadTuningValues : Failed to read tuning values\n");
        text.vtable = addr::SC4String_Vtable;
        if (text.begin)
            Free(text.begin);
        if (exemplar)
            Release(exemplar);
        Release(resources);
        if (points.begin)
            Free(points.begin);
        return 0;
    }

    void* const holder = Virtual<void*>(exemplar, 0x1C);
    if (holder)
        AddRef(holder);
    This<uint8_t>(addr::SC4Curve_LoadFromExemplar, curve, static_cast<const uint32_t*>(key), uint32_t{0x6A946A50});
    Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, uint32_t{0xAABAA0D3}, &At<uint32_t>(self, snd::kMostOfOne));
    Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, uint32_t{0x0A946A57}, &At<uint32_t>(self, snd::kMostPlain));
    Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, uint32_t{0x4A946A5D}, &At<uint32_t>(self, snd::kLimit6C));
    if (holder)
        Release(holder);
    if (exemplar)
        Release(exemplar);
    Release(resources);
    if (points.begin)
        Free(points.begin);
    return 1;
}

// A reference kept in a field, as the original's inlined assignment.
void KeepReference(void*& field, void* object)
{
    void* const old = field;
    if (object == old)
        return;
    AddRef(object);
    field = object;
    if (old)
        Release(old);
}

// The game's 0x004FA2F0: the manager's start: the sound service and the
// sound factory from the framework (false without either), the tuning, the
// manager given to the service as the taker of its messages (slot 15) and
// its tick part as a tick every 0x14 (slot 30), seven of the game's
// messages asked for, the master level from the preferences (0 with sound
// off) and every voice set.
uint8_t __fastcall Init(char* self, void*)
{
    void* service = nullptr;
    if (void* const framework = Cdecl<void*>(addr::RZGetFramework))
        Virtual<uint8_t>(framework, 0x14, uint32_t{0x23CECBA0}, uint32_t{0x106077D2}, &service);
    if (!service) {
        This<void>(addr::SC4RefPtr_DestructGuarded, &service);
        return 0;
    }
    KeepReference(At<void*>(self, snd::kService), service);

    void* factory = nullptr;
    if (void* const framework = Cdecl<void*>(addr::RZGetFramework))
        Virtual<uint8_t>(framework, 0x14, uint32_t{0x1AF85BFB}, uint32_t{0x3AF85BB7}, &factory);
    if (!factory) {
        This<void>(addr::SC4Audio_F4EDF00, &factory);
        This<void>(addr::SC4RefPtr_DestructGuarded, &service);
        return 0;
    }
    KeepReference(At<void*>(self, snd::kFactory), factory);

    ReadTuning(self, nullptr);
    if (void* const kept = At<void*>(self, snd::kService)) {
        Virtual<void>(kept, 0x3C, self);
        Virtual<void>(At<void*>(self, snd::kService), 0x78, self + snd::kTick, uint32_t{0x14});
    }

    void* server = nullptr;
    if (void* const framework = Cdecl<void*>(addr::RZGetFramework))
        Virtual<uint8_t>(framework, 0x14, uint32_t{0x04FA845B}, uint32_t{0x652294C7}, &server);
    if (server) {
        for (const uint32_t id : kMessages)
            Virtual<uint8_t>(server, 0x14, self + snd::kTarget, id);
    }

    char* const preferences = Virtual<char*>(Global<void*>(addr::SC4_Service43C94), 0x98);
    if (At<uint8_t>(preferences, 4))
        At<uint32_t>(self, snd::kLevel) = At<uint32_t>(preferences, 8);
    else
        At<uint32_t>(self, snd::kLevel) = 0;
    RefreshAll(self, nullptr);

    This<void>(addr::SC4RefPtr_Destruct, &server);
    This<void>(addr::SC4Audio_F4EDF00, &factory);
    This<void>(addr::SC4RefPtr_DestructGuarded, &service);
    return 1;
}

// The game's 0x004F82E0: the manager's end: every voice asked to stop, the
// service's slot 47, the seven messages given back, the tick and the
// manager taken off the service (slots 31 and 16), the two services let go.
uint8_t __fastcall Shutdown(char* self, void*)
{
    AskStopAll(self, nullptr, 1);
    if (void* const service = At<void*>(self, snd::kService))
        Virtual<void>(service, 0xBC);

    void* server = nullptr;
    if (void* const framework = Cdecl<void*>(addr::RZGetFramework))
        Virtual<uint8_t>(framework, 0x14, uint32_t{0x04FA845B}, uint32_t{0x652294C7}, &server);
    if (server) {
        for (std::size_t i = sizeof(kMessages) / sizeof(kMessages[0]); i-- > 0;)
            Virtual<uint8_t>(server, 0x18, self + snd::kTarget, kMessages[i]);
    }

    if (void* const service = At<void*>(self, snd::kService)) {
        Virtual<void>(service, 0x7C, self + snd::kTick);
        Virtual<void>(At<void*>(self, snd::kService), 0x40, self);
    }
    if (void* const service = At<void*>(self, snd::kService)) {
        At<void*>(self, snd::kService) = nullptr;
        Release(service);
    }
    if (void* const factory = At<void*>(self, snd::kFactory)) {
        At<void*>(self, snd::kFactory) = nullptr;
        Release(factory);
    }
    This<void>(addr::SC4RefPtr_Destruct, &server);
    return 1;
}

// The game's 0x004F9830: the constructor.
char* __fastcall Construct(char* self, void*)
{
    At<std::uintptr_t>(self, snd::kTick) = addr::SC4AudioSound_TickBaseVtable;
    At<std::uintptr_t>(self, snd::kSoundEnded) = addr::SC4AudioSound_SoundEndedBaseVtable;
    At<std::uintptr_t>(self, snd::kTarget) = addr::cSC4TrafficSimulator_VtableBase4;
    At<std::uintptr_t>(self, 0) = addr::SC4AudioSound_Vtable;
    At<std::uintptr_t>(self, snd::kTick) = addr::SC4AudioSound_TickVtable;
    At<std::uintptr_t>(self, snd::kSoundEnded) = addr::SC4AudioSound_SoundEndedVtable;
    At<std::uintptr_t>(self, snd::kTarget) = addr::SC4AudioSound_TargetVtable;
    At<uint32_t>(self, snd::kReferences) = 0;
    At<uint32_t>(self, snd::kLevel) = 0x400;
    At<uint32_t>(self, snd::kClock) = 0;
    At<uint32_t>(self, snd::kDepth) = 0;
    At<uint32_t>(self, 0x20) = 0;
    At<uint32_t>(self, 0x24) = 0;
    At<uint32_t>(self, 0x28) = 0;
    At<uint8_t>(self, 0x2C) = 0;
    At<uint32_t>(self, snd::kCountdown) = 0;
    char* const map = self + snd::kMap;
    At<uint32_t>(map, 0x4) = 0;
    At<uint32_t>(map, 0x8) = 0;
    At<uint32_t>(map, 0xC) = 0;
    At<uint32_t>(map, 0x10) = 0;
    This<void>(addr::SC4Uint32HashSet_Construct, map, uint32_t{0x64});
    At<uint32_t>(self, snd::kVoices) = 0;
    At<Node*>(self, snd::kVoices) = NewList();
    At<uint32_t>(self, snd::kVoiceCount) = 0;
    At<uint32_t>(self, 0x50) = 0;
    At<uint32_t>(self, snd::kPlain) = 0;
    At<uint32_t>(self, snd::kGroups) = 0;
    At<uint32_t>(self, snd::kService) = 0;
    At<uint32_t>(self, snd::kFactory) = 0;
    At<uint32_t>(self, snd::kMostOfOne) = 3;
    At<uint32_t>(self, snd::kMostPlain) = 0xA;
    At<uint32_t>(self, snd::kLimit6C) = 0x1E;
    This<void>(addr::SC4Curve_Init, self + snd::kCurve);
    return self;
}

// The game's 0x004F8820: the destructor.
void __fastcall Destruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0) = addr::SC4AudioSound_Vtable;
    At<std::uintptr_t>(self, snd::kTick) = addr::SC4AudioSound_TickVtable;
    At<std::uintptr_t>(self, snd::kSoundEnded) = addr::SC4AudioSound_SoundEndedVtable;
    At<std::uintptr_t>(self, snd::kTarget) = addr::SC4AudioSound_TargetVtable;
    if (void* const points = At<void*>(self, snd::kCurve))
        Free(points);
    if (void* const factory = At<void*>(self, snd::kFactory))
        Release(factory);
    if (void* const service = At<void*>(self, snd::kService))
        Release(service);
    This<void>(addr::SC4PairList_Destroy, self + snd::kVoices);
    char* const map = self + snd::kMap;
    This<void>(addr::SC4Uint32HashSet_Clear, map);
    if (void* const buckets = At<void*>(map, 0x4))
        Free(buckets);
}

// The game's 0x004F8DA0: Release of the main part.
uint32_t __fastcall ReleaseMain(char* self, void*)
{
    const int32_t references = At<int32_t>(self, snd::kReferences);
    if (references > 1) {
        At<int32_t>(self, snd::kReferences) = references - 1;
        return static_cast<uint32_t>(references - 1);
    }
    Destruct(self, nullptr);
    Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return 0;
}

// The game's 0x004F8FD0, 0x004F8FE0 and 0x004F8FF0: Release from the three
// other parts.
uint32_t __fastcall ReleaseTick(char* part, void*)
{
    return ReleaseMain(part - snd::kTick, nullptr);
}

uint32_t __fastcall ReleaseSoundEnded(char* part, void*)
{
    return ReleaseMain(part - snd::kSoundEnded, nullptr);
}

uint32_t __fastcall ReleaseTarget(char* part, void*)
{
    return ReleaseMain(part - snd::kTarget, nullptr);
}

bool InstallPart9(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[76] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4Audio_F4F7300, reinterpret_cast<void*>(&ScapeAddPoint)},
        {addr::SC4Audio_F4F7510, reinterpret_cast<void*>(&ScapeMovePoint)},
        {addr::SC4Audio_F4F7850, reinterpret_cast<void*>(&ScapeCollectNear)},
        {addr::SC4Audio_F4F7AF0, reinterpret_cast<void*>(&ScapeCollect)},
        {addr::SC4Audio_F4F7B70, reinterpret_cast<void*>(&QueryInterfaceMain)},
        {addr::SC4Audio_F4F7BD0, reinterpret_cast<void*>(&MoverSetLevel)},
        {addr::SC4Audio_F4F7BF0, reinterpret_cast<void*>(&MoverSetRates)},
        {addr::SC4Audio_F4F7C50, reinterpret_cast<void*>(&FadeStep)},
        {addr::SC4Audio_F4F7CB0, reinterpret_cast<void*>(&AskMasterLevel)},
        {addr::SC4Audio_F4F7D20, reinterpret_cast<void*>(&AskStart)},
        {addr::SC4Audio_F4F7D90, reinterpret_cast<void*>(&AskPlace)},
        {addr::SC4Audio_F4F7E20, reinterpret_cast<void*>(&AskRefresh)},
        {addr::SC4Audio_F4F7E90, reinterpret_cast<void*>(&AskStopAll)},
        {addr::SC4Audio_F4F7F00, reinterpret_cast<void*>(&AskLetGo)},
        {addr::SC4Audio_F4F7FD0, reinterpret_cast<void*>(&NudgeSound)},
        {addr::SC4Audio_F4F8030, reinterpret_cast<void*>(&VoiceGetPlace)},
        {addr::SC4Audio_F4F8070, reinterpret_cast<void*>(&VoiceIsFar)},
        {addr::SC4Audio_F4F8130, reinterpret_cast<void*>(&VoiceWatch)},
        {addr::SC4Audio_F4F81D0, reinterpret_cast<void*>(&MoverConstruct)},
        {addr::SC4Audio_F4F8240, reinterpret_cast<void*>(&MoverSetPlace)},
        {addr::SC4Audio_F4F8280, reinterpret_cast<void*>(&MoverGetPlace)},
        {addr::SC4Audio_F4F82E0, reinterpret_cast<void*>(&Shutdown)},
        {addr::SC4Audio_F4F83E0, reinterpret_cast<void*>(&OnVisit)},
        {addr::SC4Audio_F4F8510, reinterpret_cast<void*>(&FindLatest)},
        {addr::SC4Audio_F4F8590, reinterpret_cast<void*>(&CountOfSound)},
        {addr::SC4Audio_F4F85F0, reinterpret_cast<void*>(&FindLeader)},
        {addr::SC4Audio_F4F8650, reinterpret_cast<void*>(&MoverStep)},
        {addr::SC4Audio_F4F8790, reinterpret_cast<void*>(&VoiceConstruct)},
        {addr::SC4Audio_F4F8820, reinterpret_cast<void*>(&Destruct)},
        {addr::SC4Audio_F4F8890, reinterpret_cast<void*>(&QueryInterfaceTick)},
        {addr::SC4Audio_F4F88A0, reinterpret_cast<void*>(&QueryInterfaceSoundEnded)},
        {addr::SC4Audio_F4F88B0, reinterpret_cast<void*>(&QueryInterfaceTarget)},
        {addr::SC4Audio_F4F88D0, reinterpret_cast<void*>(&VoiceRelease)},
        {addr::SC4Audio_F4F8920, reinterpret_cast<void*>(&VoiceGather)},
        {addr::SC4Audio_F4F8CF0, reinterpret_cast<void*>(&VoiceAddChild)},
        {addr::SC4Audio_F4F8DA0, reinterpret_cast<void*>(&ReleaseMain)},
        {addr::SC4Audio_F4F8DD0, reinterpret_cast<void*>(&StartOver)},
        {addr::SC4Audio_F4F8EA0, reinterpret_cast<void*>(&VoiceGatherFromCamera)},
        {addr::SC4Audio_F4F8F00, reinterpret_cast<void*>(&VoiceGatherFromListener)},
        {addr::SC4Audio_F4F8FD0, reinterpret_cast<void*>(&ReleaseTick)},
        {addr::SC4Audio_F4F8FE0, reinterpret_cast<void*>(&ReleaseSoundEnded)},
        {addr::SC4Audio_F4F8FF0, reinterpret_cast<void*>(&ReleaseTarget)},
        {addr::SC4Audio_F4F9000, reinterpret_cast<void*>(&OnStartOver)},
        {addr::SC4Audio_F4F9050, reinterpret_cast<void*>(&VoiceSendPlace)},
        {addr::SC4Audio_F4F9100, reinterpret_cast<void*>(&VoiceRemoveChild)},
        {addr::SC4Audio_F4F9150, reinterpret_cast<void*>(&VoiceHandOver)},
        {addr::SC4Audio_F4F9250, reinterpret_cast<void*>(&ReadTuning)},
        {addr::SC4Audio_F4F9430, reinterpret_cast<void*>(&TargetDoMessage)},
        {addr::SC4Audio_F4F94C0, reinterpret_cast<void*>(&StopVoice)},
        {addr::SC4Audio_F4F9610, reinterpret_cast<void*>(&StopAll)},
        {addr::SC4Audio_F4F9640, reinterpret_cast<void*>(&VoiceSetLevel)},
        {addr::SC4Audio_F4F9690, reinterpret_cast<void*>(&VoiceFade)},
        {addr::SC4Audio_F4F9700, reinterpret_cast<void*>(&VoiceSetScale)},
        {addr::SC4Audio_F4F9750, reinterpret_cast<void*>(&VoiceUnmute)},
        {addr::SC4Audio_F4F97E0, reinterpret_cast<void*>(&VoiceSetOutOfEarshot)},
        {addr::SC4Audio_F4F9830, reinterpret_cast<void*>(&Construct)},
        {addr::SC4Audio_F4F98F0, reinterpret_cast<void*>(&SoundEnded)},
        {addr::SC4Audio_F4F9990, reinterpret_cast<void*>(&OnStop)},
        {addr::SC4Audio_F4F99F0, reinterpret_cast<void*>(&OnLetGo)},
        {addr::SC4Audio_F4F9AF0, reinterpret_cast<void*>(&SetVoiceLevel)},
        {addr::SC4Audio_F4F9B90, reinterpret_cast<void*>(&VoiceCheckEarshot)},
        {addr::SC4Audio_F4F9C10, reinterpret_cast<void*>(&VoiceCheckPause)},
        {addr::SC4Audio_F4F9CA0, reinterpret_cast<void*>(&Tick)},
        {addr::SC4Audio_F4F9D20, reinterpret_cast<void*>(&OnSetLevel)},
        {addr::SC4Audio_F4F9D70, reinterpret_cast<void*>(&StartVoice)},
        {addr::SC4Audio_F4FA180, reinterpret_cast<void*>(&RefreshAll)},
        {addr::SC4Audio_F4FA1F0, reinterpret_cast<void*>(&OnStart)},
        {addr::SC4Audio_F4FA260, reinterpret_cast<void*>(&SetVoicePlace)},
        {addr::SC4Audio_F4FA2F0, reinterpret_cast<void*>(&Init)},
        {addr::SC4Audio_F4FA4C0, reinterpret_cast<void*>(&OnSetPlace)},
        {addr::SC4Audio_F4FA550, reinterpret_cast<void*>(&OnMasterLevel)},
        {addr::SC4Audio_F4FA590, reinterpret_cast<void*>(&DoMessage)},
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

const bool g_part9Registered = registry::Register({
    "audio.part9",
    "audio",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Audio_F4F9830,
    "Our audio sound manager (voices, groups, levels, places, its messages and tick), the mover and the fade, "
    "and the scape manager's point and collect methods (docs/audio.md)",
    false,
    InstallPart9,
});

} // namespace
} // namespace opensc4::audio
