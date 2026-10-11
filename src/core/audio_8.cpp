// The audio code, part 8 (0x004F5120 to 0x004F7300): one function of the
// audio options dialog and most of cSC4AudioScapeManager.
//
// cSC4AudioScapeManager (class 0x29E2797F, interface 0x29E39DB4, 0x98 bytes,
// made by the factory 0x004EB120, which hands out its system service part)
// keeps "scapes": named sets of sounding points in the city. Points that lie
// near each other are gathered into clusters, each cluster sits at the
// average of its points, and every cluster plays the scape's emitters while
// its number of points is inside an emitter's range. The sounds themselves
// are the framework sound service's (0x23CECBA0): the manager only sends it
// messages through its slot 11 (+0x2C; five arguments: message, a, b, c, d):
//
//   0xEA451903 (sound ID, instance)        start an instance
//   0x4A451915 (instance, level)           its level
//   0xCA45190F (instance, x, y, z)         its place, whole numbers
//   0x6A451908 (instance)                  stop it
//
// The object: vtable 0x00A94340 at +0 (19 slots), the system service at +4
// (0x00A94308, service ID 0x49E50BA1 at +0x80, running byte at +0x84) and a
// message target at +8 (0x00A942F8, which takes every message and does
// nothing). The scapes hang in the list at +0x54, clusters on their way out
// in the list at +0x58 (filled by part 9), and +0x88 is a vector of 16-byte
// cells that Collect uses to thin out a scape's clusters.
//
// What this part holds:
//
//   - the object's making and unmaking, QueryInterface and Release, Init
//     (asks the framework for the sound service and for service 0x1AF85BFB,
//     joins the framework's services and its tick) and Shutdown;
//   - AddScape, RemoveScape, ClearScape, ClearScapes, AddEmitter,
//     RemoveEmitter, RemovePoint, GetClusters (interface slots 5 to 8, 11,
//     13, 14, 16, 17);
//   - OnTick of the system service: counts the leaving clusters down on a
//     stopwatch (real time) and walks every cluster's two levels a seventh
//     of the way to their targets;
//   - the cluster's own methods (a 0x4C-byte item of the pool 0x00B098A8):
//     its running average, its points, its emitter instances;
//   - AddPoint and Collect, which part 9's interface methods call;
//   - the options dialog's slot 5 (vtable 0x00A94264, part 7's class),
//     which loads the dialog's window.
//
// Replacement, off by default:
//
//   audio.part8: everything named above.
//
// Not functions: 0x004F4C20 is the middle of part 7's 0x004F44C0, 0x004F5230
// and 0x004F5260 are the middle of QueryInterface (0x004F5210), 0x004F58F0 is
// the end of FindScape (0x004F58B0).
//
// Still the game's: the pool's own code (0x004F5290, 0x004F52D0, 0x004F5660,
// 0x004F5670, 0x004F59B0, 0x004F59C0: block builders and item frees of two
// pool templates), the vector code (0x004F5310 and 0x004F5D50, which the
// renderer's code calls too; 0x004F63B0 and 0x004F6DF0, fill and resize of
// the cell vector) and SC4RefList::Construct (0x004F5A00, called from all
// over the game).
//
// Time: OnTick reads a stopwatch, so when a leaving cluster goes is real
// time; everything else here is plain state.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace opensc4::audio {
namespace {

namespace mgr { // offsets from the object
constexpr std::size_t kService = 0x04;
constexpr std::size_t kTarget = 0x08;
constexpr std::size_t kSound = 0x0C;      // the sound service
constexpr std::size_t kHelper = 0x10;     // service 0x1AF85BFB
constexpr std::size_t kStarted = 0x14;    // byte
constexpr std::size_t kReferences = 0x18;
constexpr std::size_t kStopwatch = 0x20;  // 0x18 bytes
constexpr std::size_t kLastTime = 0x38;   // milliseconds at the last tick
constexpr std::size_t kLock3C = 0x3C;     // critical sections, 8 bytes each
constexpr std::size_t kLock = 0x44;
constexpr std::size_t kTickLock = 0x4C;
constexpr std::size_t kScapes = 0x54;     // lists: a pointer to the end node
constexpr std::size_t kLeaving = 0x58;
constexpr std::size_t kList5C = 0x5C;
constexpr std::size_t kFloats = 0x60;     // six floats
constexpr std::size_t kServiceId = 0x80;
constexpr std::size_t kRunning = 0x84;    // byte
constexpr std::size_t kCells = 0x88;      // vector of Cell: begin, end, capacity
} // namespace mgr

namespace scape { // a scape: 0x34 bytes, a game string first
constexpr std::size_t kNameBegin = 0x04;
constexpr std::size_t kNameEnd = 0x08;
constexpr std::size_t kNameCapacity = 0x0C;
constexpr std::size_t kNameReferences = 0x10;
constexpr std::size_t kId = 0x18;
constexpr std::size_t kClusters = 0x1C;   // list
constexpr std::size_t kClusterCount = 0x20;
constexpr std::size_t kFlags = 0x24;      // 1: its own emitter, 4: points not kept, 0x10: clusters kept
constexpr std::size_t kEmitters = 0x28;   // list of emitters
constexpr std::size_t kRadius = 0x2C;     // float, given to each new cluster
constexpr std::size_t kPointCount = 0x30;
constexpr std::size_t kSize = 0x34;
} // namespace scape

namespace cluster { // a cluster: 0x4C bytes of the pool
constexpr std::size_t kPlace = 0x00;      // three floats: the average
constexpr std::size_t kSum = 0x0C;        // three floats
constexpr std::size_t kSummed = 0x18;     // how many places are in the sum
constexpr std::size_t kAdded = 0x1C;      // how many were ever added
constexpr std::size_t kRadius = 0x20;     // float
constexpr std::size_t kPoints = 0x24;     // list of points
constexpr std::size_t kInstances = 0x28;  // list of emitter instances
constexpr std::size_t kTimeLeft = 0x2C;   // milliseconds, once it is leaving
constexpr std::size_t kPointCount = 0x30;
constexpr std::size_t kInstanceCount = 0x34;
constexpr std::size_t kLevels = 0x38;     // two levels, then their two targets
constexpr std::size_t kLevel = 0x48;      // the product of the two, over 1024
} // namespace cluster

namespace emitter { // an emitter and an instance of one: 0x18 bytes
constexpr std::size_t kMinimum = 0x00;
constexpr std::size_t kMaximum = 0x04;
constexpr std::size_t kValue = 0x08;
constexpr std::size_t kSoundId = 0x0C;
constexpr std::size_t kFlag = 0x10;       // byte
constexpr std::size_t kFloat = 0x14;      // 50.0
constexpr std::size_t kSize = 0x18;
} // namespace emitter

constexpr uint32_t kScapeIid = 0x29E39DB4;
constexpr uint32_t kSystemServiceIid = 0x287FB697;
constexpr uint32_t kSoundService = 0x23CECBA0;
constexpr uint32_t kSoundIid = 0x106077D2;
constexpr uint32_t kHelperService = 0x1AF85BFB;
constexpr uint32_t kHelperIid = 0x3AF85BB7;
constexpr uint32_t kServiceId = 0x49E50BA1;

constexpr uint32_t kStartMessage = 0xEA451903;
constexpr uint32_t kLevelMessage = 0x4A451915;
constexpr uint32_t kPlaceMessage = 0xCA45190F;
constexpr uint32_t kStopMessage = 0x6A451908;

// The game's list node.
struct Node {
    Node* next;
    Node* prev;
    void* value;
};
static_assert(sizeof(Node) == 0xC);

// A cell of the manager's vector.
struct Cell {
    float x;
    float y;
    float z;
    int32_t count;
};
static_assert(sizeof(Cell) == 0x10);

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

void Lock(void* section)
{
    Virtual<void>(section, 0x4);
}

void Unlock(void* section)
{
    Virtual<void>(section, 0x8);
}

void Free(void* memory)
{
    Cdecl<void>(addr::SC4_Free, memory);
}

void*& SoundService()
{
    return Global<void*>(addr::SC4AudioScape_Sound);
}

void ReleaseField(void*& field)
{
    if (void* const object = field) {
        field = nullptr;
        Virtual<uint32_t>(object, 0x8);
    }
}

Node* NewList()
{
    Node* const end = static_cast<Node*>(Cdecl<void*>(addr::SC4_stl_allocate, uint32_t{0xC}));
    end->next = end;
    end->prev = end;
    return end;
}

void PushBack(Node* end, void* value)
{
    Node* const node = static_cast<Node*>(Cdecl<void*>(addr::SC4_stl_allocate, uint32_t{0xC}));
    node->value = value;
    Node* const last = end->prev;
    node->prev = last;
    node->next = end;
    last->next = node;
    end->prev = node;
}

// Takes a node out of its list and frees it; returns the one after it.
Node* Erase(Node* node)
{
    Node* const next = node->next;
    Node* const prev = node->prev;
    prev->next = next;
    next->prev = prev;
    Free(node);
    return next;
}

// The list's end as the original has it inline (SC4PairList::Destroy does
// the same out of line): the nodes freed, then the end node.
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

void DestroyList(char* holder)
{
    This<void>(addr::SC4PairList_Destroy, holder);
}

// The pool of clusters and points: 8 KB blocks of 0x4C-byte items.
char* Pool()
{
    return reinterpret_cast<char*>(std::uintptr_t{addr::SC4AudioScape_Pool});
}

void* PoolAllocate()
{
    char* const pool = Pool();
    void* item = At<void*>(At<char*>(pool, 4), 8);
    if (!item)
        item = This<void*>(addr::SC4EdgePool_Grow, pool);
    At<void*>(At<char*>(pool, 4), 8) = *static_cast<void**>(item);
    ++At<uint32_t>(At<char*>(pool, 4), 0xC);
    return item;
}

void PoolFree(void* item)
{
    This<void>(addr::SC4Audio_F4F5670, Pool(), item);
}

// The same where the original has it inline (no test for null).
void PoolFreeInline(void* item)
{
    char* const block = reinterpret_cast<char*>(reinterpret_cast<std::uintptr_t>(item) & 0xFFFFE000u);
    *static_cast<void**>(item) = At<void*>(block, 8);
    At<void*>(block, 8) = item;
    const uint32_t left = --At<uint32_t>(block, 0xC);
    if (left == 0)
        This<void>(addr::SC4Pool_ReleaseBlock, Pool(), block);
    else if (left == 0x40000050)
        This<void>(addr::SC4Pool_ReopenBlock, Pool(), block);
}

void CopyPlace(void* to, const void* from)
{
    static_cast<uint32_t*>(to)[0] = static_cast<const uint32_t*>(from)[0];
    static_cast<uint32_t*>(to)[1] = static_cast<const uint32_t*>(from)[1];
    static_cast<uint32_t*>(to)[2] = static_cast<const uint32_t*>(from)[2];
}

// A new emitter with the values both makers give it first.
char* NewEmitter()
{
    char* const made = static_cast<char*>(Cdecl<void*>(addr::SC4_operator_new, uint32_t{emitter::kSize}));
    if (!made)
        return nullptr;
    At<uint32_t>(made, emitter::kMinimum) = 1;
    At<uint32_t>(made, emitter::kMaximum) = 0x7FFFFFFF;
    At<uint32_t>(made, emitter::kValue) = 0;
    At<uint32_t>(made, emitter::kSoundId) = 0;
    At<uint8_t>(made, emitter::kFlag) = 0;
    At<uint32_t>(made, emitter::kFloat) = 0x42480000; // 50.0
    return made;
}

// ---------------------------------------------------------------------------
// The options dialog (part 7's class).

// The game's 0x004F5120: slot 5 of the dialog's vtable 0x00A94264. Loads the
// dialog's window (layout 0xCA53F06E of group 0x96A006B0, window 0xEA53F5DB)
// into +4, fills it and makes the dialog its window procedure.
uint8_t __fastcall OptionsLoad(char* self, void*)
{
    if (!Global<uint8_t>(addr::SC4AudioOptions_TablesMade)) {
        Global<uint8_t>(addr::SC4AudioOptions_TablesMade) = 1;
        Global<uint8_t>(addr::SC4AudioOptions_Filled) = 0;
        Cdecl<void>(addr::SC4Audio_F4F2B70);
        Cdecl<void>(addr::SC4Audio_F4F2910, int32_t{0});
        Cdecl<void>(addr::SC4Audio_F4F2910, int32_t{1});
    }
    if (Global<uint8_t>(addr::SC4AudioOptions_Filled))
        return 1;
    Cdecl<void>(addr::SC4Audio_F4F22C0);
    uint32_t key[3] = {0, 0x96A006B0, 0xCA53F06E};
    void*& window = At<void*>(self, 0x4);
    ReleaseField(window);
    const uint8_t loaded = Cdecl<uint8_t>(addr::SC4UI_LoadWindow, &key[0], static_cast<void*>(nullptr),
                                          uint32_t{0xEA53F5DB}, uint32_t{0x5386D516}, &window);
    if (loaded) {
        if (!At<void*>(self, 0x10))
            At<void*>(self, 0x10) = Global<void*>(addr::SC4_Options43CD8);
        This<void>(addr::SC4Audio_F4F44C0, self);
        Virtual<uint8_t>(At<void*>(self, 0x4), 0x1C, self);
    }
    return loaded;
}

// ---------------------------------------------------------------------------
// A cluster.

// The game's 0x004F5CD0: a cluster's constructor (the item is the pool's).
char* __fastcall ClusterConstruct(char* self, void*)
{
    At<uint32_t>(self, cluster::kPlace) = 0;
    At<uint32_t>(self, cluster::kPlace + 4) = 0;
    At<uint32_t>(self, cluster::kPlace + 8) = 0;
    At<uint32_t>(self, cluster::kSum) = Global<uint32_t>(addr::SC4AudioScape_Zero);
    At<uint32_t>(self, cluster::kSum + 4) = Global<uint32_t>(addr::SC4AudioScape_Zero);
    At<uint32_t>(self, cluster::kSum + 8) = Global<uint32_t>(addr::SC4AudioScape_Zero);
    At<uint32_t>(self, cluster::kSummed) = 0;
    At<uint32_t>(self, cluster::kAdded) = 0;
    At<uint32_t>(self, cluster::kRadius) = 0;
    At<Node*>(self, cluster::kPoints) = nullptr;
    At<Node*>(self, cluster::kPoints) = NewList();
    At<Node*>(self, cluster::kInstances) = nullptr;
    At<Node*>(self, cluster::kInstances) = NewList();
    At<uint32_t>(self, cluster::kTimeLeft) = 0;
    At<uint32_t>(self, cluster::kPointCount) = 0;
    At<uint32_t>(self, cluster::kInstanceCount) = 0;
    At<uint32_t>(self, cluster::kLevel) = 0;
    At<uint32_t>(self, cluster::kLevels) = 0;
    At<uint32_t>(self, cluster::kLevels + 4) = 0;
    At<uint32_t>(self, cluster::kLevels + 8) = 0x400;
    At<uint32_t>(self, cluster::kLevels + 0xC) = 0x400;
    return self;
}

// The average from the sum: sum times (1 / count), each a float.
void Average(char* self, int32_t count)
{
    CopyPlace(self + cluster::kPlace, self + cluster::kSum);
    const double share = static_cast<double>(Global<float>(addr::SC4AudioScape_One)) / static_cast<double>(count);
    for (std::size_t i = 0; i < 3; ++i) {
        float& part = At<float>(self, cluster::kPlace + 4 * i);
        part = static_cast<float>(share * static_cast<double>(part));
    }
}

// The game's 0x004F5520: one more place in the cluster's average.
void __fastcall ClusterAddPlace(char* self, void*, const float* place)
{
    if (At<int32_t>(self, cluster::kSummed) == 0) {
        CopyPlace(self + cluster::kSum, place);
    } else {
        for (std::size_t i = 0; i < 3; ++i) {
            float& sum = At<float>(self, cluster::kSum + 4 * i);
            sum = static_cast<float>(static_cast<double>(sum) + static_cast<double>(place[i]));
        }
    }
    const int32_t count = ++At<int32_t>(self, cluster::kSummed);
    Average(self, count);
}

// The game's 0x004F55A0: one place less. The original divides by the count
// it had before the place went, not by the new one.
void __fastcall ClusterRemovePlace(char* self, void*, const float* place)
{
    if (At<int32_t>(self, cluster::kSummed) == 0)
        return;
    for (std::size_t i = 0; i < 3; ++i) {
        float& sum = At<float>(self, cluster::kSum + 4 * i);
        sum = static_cast<float>(static_cast<double>(sum) - static_cast<double>(place[i]));
    }
    const int32_t count = At<int32_t>(self, cluster::kSummed);
    At<int32_t>(self, cluster::kSummed) = count - 1;
    Average(self, count);
}

// The game's 0x004F5610 (cdecl): starts an instance of a sound and gives it
// its level. The third argument is not used.
uint8_t __cdecl StartSound(uint32_t soundId, void* instance, uint32_t, int32_t level)
{
    if (!SoundService())
        return 0;
    Virtual<uint8_t>(SoundService(), 0x2C, kStartMessage, soundId, instance, uint32_t{0}, uint32_t{0});
    Virtual<uint8_t>(SoundService(), 0x2C, kLevelMessage, instance, level, uint32_t{0}, uint32_t{0});
    return 1;
}

// The game's 0x004F56B0: sets one of the cluster's two levels and tells
// every instance the value; returns the cluster's level.
int32_t __fastcall ClusterSetLevel(char* self, void*, uint32_t which, int32_t value)
{
    At<int32_t>(self, cluster::kLevels + 4 * which) = value;
    Node* const end = At<Node*>(self, cluster::kInstances);
    for (Node* node = end->next; node != end; node = node->next) {
        if (void* const sound = SoundService())
            Virtual<uint8_t>(sound, 0x2C, kLevelMessage, node->value, value, uint32_t{0}, uint32_t{0});
    }
    return At<int32_t>(self, cluster::kLevel);
}

// The game's 0x004F5900: tells every instance where the cluster is.
void __fastcall ClusterSendPlace(char* self, void*)
{
    Node* const end = At<Node*>(self, cluster::kInstances);
    for (Node* node = end->next; node != end; node = node->next) {
        void* const sound = SoundService();
        if (!sound)
            continue;
        const int32_t z = Ftol(static_cast<double>(At<float>(self, cluster::kPlace + 8)));
        const int32_t y = Ftol(static_cast<double>(At<float>(self, cluster::kPlace + 4)));
        const int32_t x = Ftol(static_cast<double>(At<float>(self, cluster::kPlace)));
        Virtual<uint8_t>(sound, 0x2C, kPlaceMessage, node->value, x, y, z);
    }
}

// The game's 0x004F5970: whether the cluster plays this emitter's sound.
uint8_t __fastcall ClusterHasEmitter(char* self, void*, char* which)
{
    if (!which)
        return 0;
    Node* const end = At<Node*>(self, cluster::kInstances);
    for (Node* node = end->next; node != end; node = node->next) {
        if (At<uint32_t>(node->value, emitter::kSoundId) == At<uint32_t>(which, emitter::kSoundId))
            return 1;
    }
    return 0;
}

// The game's 0x004F5AF0: stops the cluster's instance of this emitter.
uint8_t __fastcall ClusterStopEmitter(char* self, void*, char* which)
{
    if (!which)
        return 0;
    if (!ClusterHasEmitter(self, nullptr, which))
        return 1;
    Node* const end = At<Node*>(self, cluster::kInstances);
    const uint32_t soundId = At<uint32_t>(which, emitter::kSoundId);
    for (Node* node = end->next; node != end; node = node->next) {
        void* const instance = node->value;
        if (At<uint32_t>(instance, emitter::kSoundId) != soundId)
            continue;
        if (void* const sound = SoundService())
            Virtual<uint8_t>(sound, 0x2C, kStopMessage, instance, uint32_t{0}, uint32_t{0}, uint32_t{0});
        Erase(node);
        --At<uint32_t>(self, cluster::kInstanceCount);
        Cdecl<void>(addr::SC4_operator_delete, instance);
        break;
    }
    return 1;
}

// The game's 0x004F5B80: stops every instance of the cluster.
uint8_t __fastcall ClusterStopAll(char* self, void*)
{
    Node* const end = At<Node*>(self, cluster::kInstances);
    Node* node = end->next;
    while (node != end) {
        void* const instance = node->value;
        if (void* const sound = SoundService())
            Virtual<uint8_t>(sound, 0x2C, kStopMessage, instance, uint32_t{0}, uint32_t{0}, uint32_t{0});
        node = Erase(node);
        --At<uint32_t>(self, cluster::kInstanceCount);
        if (instance)
            Cdecl<void>(addr::SC4_operator_delete, instance);
    }
    return 1;
}

// The game's 0x004F5BF0: gives the cluster's points back to the pool and
// empties its sum's count (the sum and the average stay).
uint8_t __fastcall ClusterDropPoints(char* self, void*)
{
    Node* const end = At<Node*>(self, cluster::kPoints);
    Node* node = end->next;
    while (node != end) {
        if (void* const point = node->value)
            PoolFreeInline(point);
        node = Erase(node);
        --At<uint32_t>(self, cluster::kPointCount);
    }
    At<uint32_t>(self, cluster::kSummed) = 0;
    return 1;
}

// The game's 0x004F5C70: takes the point with this ID out of the cluster.
uint8_t __fastcall ClusterRemovePoint(char* self, void*, uint32_t pointId)
{
    Node* const end = At<Node*>(self, cluster::kPoints);
    for (Node* node = end->next; node != end; node = node->next) {
        char* const point = static_cast<char*>(node->value);
        if (At<uint32_t>(point, 0) != pointId)
            continue;
        ClusterRemovePlace(self, nullptr, reinterpret_cast<const float*>(point + 4));
        PoolFree(point);
        Erase(node);
        --At<uint32_t>(self, cluster::kPointCount);
        return 1;
    }
    return 0;
}

// The game's 0x004F6050: the cluster starts playing an emitter: a copy of
// it joins the instances and the sound is started at the cluster's level.
uint8_t __fastcall ClusterStartEmitter(char* self, void*, char* which)
{
    if (!which)
        return 0;
    if (ClusterHasEmitter(self, nullptr, which))
        return 1;
    char* const instance = NewEmitter();
    for (std::size_t i = 0; i < emitter::kSize; i += 4)
        At<uint32_t>(instance, i) = At<uint32_t>(which, i);
    PushBack(At<Node*>(self, cluster::kInstances), instance);
    const int32_t first = At<int32_t>(self, cluster::kLevels);
    At<int32_t>(self, cluster::kLevel) = first;
    ++At<uint32_t>(self, cluster::kInstanceCount);
    const int32_t product =
        static_cast<int32_t>(At<uint32_t>(self, cluster::kLevels + 4) * static_cast<uint32_t>(first));
    At<int32_t>(self, cluster::kLevel) = product;
    const int32_t level = product / 1024;
    At<int32_t>(self, cluster::kLevel) = level;
    StartSound(At<uint32_t>(instance, emitter::kSoundId), instance, At<uint8_t>(instance, emitter::kFlag), level);
    return 1;
}

// Starts or stops one emitter by the cluster's number of points.
void ApplyEmitter(char* self, char* which)
{
    const uint32_t points = At<uint32_t>(self, cluster::kPointCount);
    if (points >= At<uint32_t>(which, emitter::kMinimum) && points <= At<uint32_t>(which, emitter::kMaximum))
        ClusterStartEmitter(self, nullptr, which);
    else
        ClusterStopEmitter(self, nullptr, which);
}

// A cluster's end where the original has it inline: both lists, then the
// item goes back to the pool.
void ClusterDestroyInline(char* self)
{
    DestroyListInline(self + cluster::kInstances);
    DestroyListInline(self + cluster::kPoints);
}

// ---------------------------------------------------------------------------
// A scape.

// The game's 0x004F6340: a scape's constructor: an empty game string, then
// the two lists. The flags at +0x24 are left as they are.
char* __fastcall ScapeConstruct(char* self, void*)
{
    At<char*>(self, scape::kNameBegin) = nullptr;
    At<char*>(self, scape::kNameEnd) = nullptr;
    At<char*>(self, scape::kNameCapacity) = nullptr;
    char* const text = static_cast<char*>(Cdecl<void*>(addr::SC4_stl_allocate, uint32_t{8}));
    At<char*>(self, scape::kNameBegin) = text;
    At<char*>(self, scape::kNameEnd) = text;
    At<char*>(self, scape::kNameCapacity) = text + 8;
    *text = 0;
    At<std::uintptr_t>(self, 0) = addr::SC4String_Vtable;
    At<uint32_t>(self, scape::kNameReferences) = 0;
    At<uint32_t>(self, 0x14) = 0;
    At<uint32_t>(self, scape::kId) = 0;
    At<Node*>(self, scape::kClusters) = nullptr;
    At<Node*>(self, scape::kClusters) = NewList();
    At<uint32_t>(self, scape::kClusterCount) = 0;
    At<Node*>(self, scape::kEmitters) = nullptr;
    At<Node*>(self, scape::kEmitters) = NewList();
    At<uint32_t>(self, scape::kRadius) = 0;
    At<uint32_t>(self, scape::kPointCount) = 0;
    return self;
}

// The game's 0x004F5AB0: frees the scape's emitters and their nodes (the
// end node stays).
uint8_t __fastcall ScapeDropEmitters(char* self, void*)
{
    Node* const end = At<Node*>(self, scape::kEmitters);
    Node* node = end->next;
    while (node != end) {
        if (void* const which = node->value)
            Cdecl<void>(addr::SC4_operator_delete, which);
        node = Erase(node);
    }
    return 1;
}

// The game's 0x004F5F20: empties the scape. With flag 0x10 a cluster that
// still counts places only loses its points and stays; every other cluster
// goes back to the pool.
uint8_t __fastcall ScapeClear(char* self, void*)
{
    Node* const end = At<Node*>(self, scape::kClusters);
    Node* node = end->next;
    while (node != end) {
        char* const item = static_cast<char*>(node->value);
        if (At<uint8_t>(self, scape::kFlags) & 0x10) {
            if (At<uint32_t>(item, cluster::kSummed) != 0) {
                ClusterDropPoints(item, nullptr);
                node = node->next;
                continue;
            }
            ClusterStopAll(item, nullptr);
            DestroyList(item + cluster::kInstances);
            DestroyList(item + cluster::kPoints);
            PoolFree(item);
        } else {
            ClusterDropPoints(item, nullptr);
            ClusterStopAll(item, nullptr);
            if (item) {
                ClusterDestroyInline(item);
                PoolFree(item);
            }
        }
        --At<uint32_t>(self, scape::kClusterCount);
        node = Erase(node);
    }
    At<uint32_t>(self, scape::kPointCount) = 0;
    return 1;
}

// The game's 0x004F5860: whether no cluster of the scape has a point with
// this ID. `this` is not used.
uint8_t __fastcall ScapeLacksPoint(char*, void*, char* which, uint32_t pointId)
{
    Node* const end = At<Node*>(which, scape::kClusters);
    for (Node* node = end->next; node != end; node = node->next) {
        Node* const points = At<Node*>(node->value, cluster::kPoints);
        for (Node* point = points->next; point != points; point = point->next) {
            if (At<uint32_t>(point->value, 0) == pointId)
                return 0;
        }
    }
    return 1;
}

// ---------------------------------------------------------------------------
// The manager.

// The game's 0x004F6140: the constructor. +0x78, +0x7C and +0x94 are left
// as they are.
char* __fastcall Construct(char* self, void*)
{
    At<std::uintptr_t>(self, mgr::kService) = addr::GZSystemService_PureVtable;
    At<std::uintptr_t>(self, mgr::kTarget) = addr::cSC4TrafficSimulator_VtableBase4;
    At<std::uintptr_t>(self, 0) = addr::SC4AudioScape_Vtable;
    At<std::uintptr_t>(self, mgr::kService) = addr::SC4AudioScape_ServiceVtable;
    At<std::uintptr_t>(self, mgr::kTarget) = addr::SC4AudioScape_TargetVtable;
    At<void*>(self, mgr::kSound) = nullptr;
    At<void*>(self, mgr::kHelper) = nullptr;
    At<uint8_t>(self, mgr::kStarted) = 0;
    At<uint32_t>(self, mgr::kReferences) = 0;
    This<void*>(addr::SC4Stopwatch_Construct, self + mgr::kStopwatch, uint32_t{4});
    At<uint32_t>(self, mgr::kLastTime) = 0;
    This<void*>(addr::RZCriticalSection_Construct, self + mgr::kLock3C);
    This<void*>(addr::RZCriticalSection_Construct, self + mgr::kLock);
    This<void*>(addr::RZCriticalSection_Construct, self + mgr::kTickLock);
    At<Node*>(self, mgr::kScapes) = nullptr;
    At<Node*>(self, mgr::kScapes) = NewList();
    At<Node*>(self, mgr::kLeaving) = nullptr;
    At<Node*>(self, mgr::kLeaving) = NewList();
    At<Node*>(self, mgr::kList5C) = nullptr;
    At<Node*>(self, mgr::kList5C) = NewList();
    for (std::size_t i = 0; i < 6; ++i)
        At<uint32_t>(self, mgr::kFloats + 4 * i) = Global<uint32_t>(addr::SC4AudioScape_Zero);
    At<uint8_t>(self, mgr::kRunning) = 0;
    At<uint32_t>(self, mgr::kServiceId) = kServiceId;
    At<void*>(self, mgr::kCells) = nullptr;
    At<void*>(self, mgr::kCells + 4) = nullptr;
    At<void*>(self, mgr::kCells + 8) = nullptr;
    return self;
}

// The game's 0x004F62C0: the destructor.
void __fastcall Destruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0) = addr::SC4AudioScape_Vtable;
    At<std::uintptr_t>(self, mgr::kService) = addr::SC4AudioScape_ServiceVtable;
    At<std::uintptr_t>(self, mgr::kTarget) = addr::SC4AudioScape_TargetVtable;
    if (void* const cells = At<void*>(self, mgr::kCells))
        Free(cells);
    DestroyList(self + mgr::kList5C);
    DestroyList(self + mgr::kLeaving);
    DestroyList(self + mgr::kScapes);
    This<void>(addr::RZCriticalSection_Destroy, self + mgr::kTickLock);
    This<void>(addr::RZCriticalSection_Destroy, self + mgr::kLock);
    This<void>(addr::RZCriticalSection_Destroy, self + mgr::kLock3C);
    This<void>(addr::SC4Stopwatch_Destruct, self + mgr::kStopwatch);
    if (void* const helper = At<void*>(self, mgr::kHelper))
        Virtual<uint32_t>(helper, 0x8);
    if (void* const sound = At<void*>(self, mgr::kSound))
        Virtual<uint32_t>(sound, 0x8);
}

// The game's 0x004F69E0: slot 18, the deleting destructor.
char* __fastcall DeletingDestruct(char* self, void*, uint32_t flags)
{
    Destruct(self, nullptr);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// The game's 0x004F5210: slot 0.
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** out)
{
    if (iid == 1 || iid == kScapeIid) {
        *out = self;
        Virtual<uint32_t>(self, 0x4);
        return 1;
    }
    if (iid == kSystemServiceIid) {
        *out = self ? self + mgr::kService : nullptr;
        Virtual<uint32_t>(self, 0x4);
        return 1;
    }
    return 0;
}

// The game's 0x004F5270: slot 2.
uint32_t __fastcall Release(char* self, void*)
{
    const int32_t count = At<int32_t>(self, mgr::kReferences);
    if (count > 1) {
        At<int32_t>(self, mgr::kReferences) = count - 1;
        return static_cast<uint32_t>(count - 1);
    }
    Virtual<void*>(self, 0x48, uint32_t{1});
    return 0;
}

// The game's 0x004F5200: the message target's DoMessage.
uint8_t __fastcall TargetDoMessage(char*, void*, void* message)
{
    return message != nullptr;
}

// The game's 0x004F6230: the service's priority.
uint32_t __fastcall ServicePriority(char*, void*)
{
    return 0x80000000;
}

// The game's 0x004F6240 and 0x004F6250: the service's running byte.
uint8_t __fastcall ServiceIsRunning(char* service, void*)
{
    return At<uint8_t>(service, mgr::kRunning - mgr::kService);
}

void __fastcall ServiceSetRunning(char* service, void*, uint32_t running)
{
    At<uint8_t>(service, mgr::kRunning - mgr::kService) = static_cast<uint8_t>(running);
}

// The game's 0x004F53E0: slot 3. Asks the framework for the sound service
// (kept at +0xC and in the global 0x00B21AEC) and for service 0x1AF85BFB
// (kept at +0x10), adds the manager to the framework's services and to its
// tick, and starts the stopwatch.
uint8_t __fastcall Init(char* self, void*)
{
    if (At<uint8_t>(self, mgr::kStarted))
        return 1;
    void* sound = nullptr;
    if (void* const framework = Cdecl<void*>(addr::RZGetFramework))
        Virtual<uint8_t>(framework, 0x14, kSoundService, kSoundIid, &sound);
    if (!sound) {
        This<void>(addr::SC4RefPtr_DestructGuarded, &sound);
        return 0;
    }
    void* const mine = At<void*>(self, mgr::kSound);
    if (sound != mine) {
        Virtual<uint32_t>(sound, 0x4);
        At<void*>(self, mgr::kSound) = sound;
        if (mine)
            Virtual<uint32_t>(mine, 0x8);
    }
    void* const shared = SoundService();
    if (sound != shared) {
        if (sound)
            Virtual<uint32_t>(sound, 0x4);
        SoundService() = sound;
        if (shared)
            Virtual<uint32_t>(shared, 0x8);
    }
    void* helper = nullptr;
    if (void* const framework = Cdecl<void*>(addr::RZGetFramework))
        Virtual<uint8_t>(framework, 0x14, kHelperService, kHelperIid, &helper);
    uint8_t started = 0;
    if (helper) {
        This<void*>(addr::SC4RefPtr_Assign, self + mgr::kHelper, helper);
        if (void* const framework = Cdecl<void*>(addr::RZGetFramework)) {
            Virtual<uint8_t>(framework, 0xC, self + mgr::kService);
            Virtual<uint8_t>(framework, 0x24, self + mgr::kService);
            This<void>(addr::SC4Stopwatch_Start, self + mgr::kStopwatch);
            At<uint8_t>(self, mgr::kStarted) = 1;
            started = 1;
        }
    }
    This<void>(addr::SC4Audio_F4EDF00, &helper);
    This<void>(addr::SC4RefPtr_DestructGuarded, &sound);
    return started;
}

// The game's 0x004F5700: slot 4. Removes every scape, leaves the
// framework's tick and services and lets the services go.
uint8_t __fastcall Shutdown(char* self, void*)
{
    if (!At<uint8_t>(self, mgr::kStarted))
        return 1;
    char* const lock = self + mgr::kLock;
    Lock(lock);
    while (At<Node*>(self, mgr::kScapes)->next != At<Node*>(self, mgr::kScapes)) {
        void* const first = At<Node*>(self, mgr::kScapes)->next->value;
        Virtual<uint8_t>(self, 0x18, At<uint32_t>(first, scape::kId));
    }
    if (void* const framework = Cdecl<void*>(addr::RZGetFramework)) {
        Virtual<uint8_t>(framework, 0x28, self + mgr::kService);
        Virtual<uint8_t>(framework, 0x10, self + mgr::kService);
    }
    ReleaseField(At<void*>(self, mgr::kSound));
    ReleaseField(At<void*>(self, mgr::kHelper));
    ReleaseField(SoundService());
    At<uint8_t>(self, mgr::kStarted) = 0;
    Unlock(lock);
    return 1;
}

// The game's 0x004F6260 to 0x004F62B0: the other parts' thunks.
uint8_t __fastcall ServiceQueryInterface(char* service, void*, uint32_t iid, void** out)
{
    return QueryInterface(service - mgr::kService, nullptr, iid, out);
}

uint32_t __fastcall ServiceRelease(char* service, void*)
{
    return Release(service - mgr::kService, nullptr);
}

uint8_t __fastcall TargetQueryInterface(char* target, void*, uint32_t iid, void** out)
{
    return QueryInterface(target - mgr::kTarget, nullptr, iid, out);
}

uint32_t __fastcall TargetRelease(char* target, void*)
{
    return Release(target - mgr::kTarget, nullptr);
}

uint8_t __fastcall ServiceInit(char* service, void*)
{
    return Init(service - mgr::kService, nullptr);
}

uint8_t __fastcall ServiceShutdown(char* service, void*)
{
    return Shutdown(service - mgr::kService, nullptr);
}

// The game's 0x004F58B0: finds a scape by its ID. `found` gets every scape
// looked at, so after a miss it holds the last one.
uint8_t __fastcall FindScape(char* self, void*, uint32_t id, char** found)
{
    char* const lock = self + mgr::kLock;
    Lock(lock);
    Node* const end = At<Node*>(self, mgr::kScapes);
    for (Node* node = end->next; node != end; node = node->next) {
        char* const item = static_cast<char*>(node->value);
        *found = item;
        if (At<uint32_t>(item, scape::kId) == id) {
            Unlock(lock);
            return 1;
        }
    }
    Unlock(lock);
    return 0;
}

// The game's 0x004F64F0: slot 5, AddScape(name, id, flags, radius). With
// flag 1 the scape gets an emitter of its own: the sound with the scape's
// ID, for any number of points.
uint8_t __fastcall AddScape(char* self, void*, void* name, uint32_t id, uint32_t flags, uint32_t radius)
{
    char* const lock = self + mgr::kLock;
    Lock(lock);
    Node* const end = At<Node*>(self, mgr::kScapes);
    for (Node* node = end->next; node != end; node = node->next) {
        if (At<uint32_t>(node->value, scape::kId) == id) {
            Unlock(lock);
            return 0;
        }
    }
    char* made = static_cast<char*>(Cdecl<void*>(addr::SC4_operator_new, uint32_t{scape::kSize}));
    if (made)
        made = ScapeConstruct(made, nullptr);
    Virtual<void*>(made, 0x38, name);
    At<uint32_t>(made, scape::kId) = id;
    At<uint32_t>(made, scape::kFlags) = flags;
    At<uint32_t>(made, scape::kRadius) = radius;
    PushBack(At<Node*>(self, mgr::kScapes), made);
    if (At<uint8_t>(made, scape::kFlags) & 1)
        Virtual<uint8_t>(self, 0x40, id, id, uint32_t{0}, uint32_t{0}, uint32_t{1}, uint32_t{0x7FFFFFFF});
    Unlock(lock);
    return 1;
}

// The game's 0x004F65C0: slot 7, ClearScape(id).
uint8_t __fastcall ClearScape(char* self, void*, uint32_t id)
{
    char* const lock = self + mgr::kLock;
    Lock(lock);
    Node* const end = At<Node*>(self, mgr::kScapes);
    for (Node* node = end->next; node != end; node = node->next) {
        char* const item = static_cast<char*>(node->value);
        if (At<uint32_t>(item, scape::kId) != id)
            continue;
        const uint8_t cleared = ScapeClear(item, nullptr);
        Unlock(lock);
        return cleared;
    }
    Unlock(lock);
    return 0;
}

// The game's 0x004F6620: slot 8, ClearScapes(flags, keep): every scape that
// has all of these flags loses its clusters' points and, unless `keep`,
// the clusters. A cluster goes only while the manager has its sound
// service; without one the original never leaves the loop.
uint8_t __fastcall ClearScapes(char* self, void*, uint32_t flags, uint32_t keep)
{
    char* const lock = self + mgr::kLock;
    Lock(lock);
    Node* const end = At<Node*>(self, mgr::kScapes);
    for (Node* node = end->next; node != end; node = node->next) {
        char* const item = static_cast<char*>(node->value);
        if ((At<uint32_t>(item, scape::kFlags) & flags) != flags)
            continue;
        Node* const clusters = At<Node*>(item, scape::kClusters);
        Node* at = clusters->next;
        while (at != clusters) {
            ClusterDropPoints(static_cast<char*>(at->value), nullptr);
            if (static_cast<uint8_t>(keep)) {
                at = at->next;
                continue;
            }
            char* const gone = static_cast<char*>(at->value);
            if (!gone || !At<void*>(self, mgr::kSound))
                continue;
            ClusterStopAll(gone, nullptr);
            ClusterDestroyInline(gone);
            PoolFreeInline(gone);
            at = Erase(at);
            --At<uint32_t>(item, scape::kClusterCount);
        }
    }
    Unlock(lock);
    return 1;
}

// The game's 0x004F6A00: slot 6, RemoveScape(id). The walk to the scape
// clears the flags of every scape it passes, the found one included.
uint8_t __fastcall RemoveScape(char* self, void*, uint32_t id)
{
    char* const lock = self + mgr::kLock;
    Lock(lock);
    Node* const end = At<Node*>(self, mgr::kScapes);
    for (Node* node = end->next; node != end; node = node->next) {
        char* const item = static_cast<char*>(node->value);
        const bool found = At<uint32_t>(item, scape::kId) == id;
        At<uint32_t>(item, scape::kFlags) = 0;
        if (!found)
            continue;
        if (!Virtual<uint8_t>(self, 0x1C, id))
            break;
        ScapeDropEmitters(item, nullptr);
        DestroyList(item + scape::kEmitters);
        DestroyList(item + scape::kClusters);
        At<std::uintptr_t>(item, 0) = addr::SC4String_Vtable;
        if (char* const text = At<char*>(item, scape::kNameBegin))
            Free(text);
        Cdecl<void>(addr::SC4_operator_delete, item);
        Erase(node);
        Unlock(lock);
        return 1;
    }
    Unlock(lock);
    return 0;
}

// The game's 0x004F57B0: slot 11, GetClusters(id, places, count, points):
// the places of the scape's clusters, three floats each, when `count` has
// room for them all; `count` gets how many, `points` the scape's number of
// points.
uint8_t __fastcall GetClusters(char* self, void*, uint32_t id, uint32_t* places, int32_t* count, uint32_t* points)
{
    char* const lock = self + mgr::kLock;
    Lock(lock);
    Node* const end = At<Node*>(self, mgr::kScapes);
    for (Node* node = end->next; node != end; node = node->next) {
        char* const item = static_cast<char*>(node->value);
        if (At<uint32_t>(item, scape::kId) != id)
            continue;
        int32_t left = At<int32_t>(item, scape::kClusterCount);
        if (left > *count)
            break;
        *count = left;
        *points = At<uint32_t>(item, scape::kPointCount);
        Node* const clusters = At<Node*>(item, scape::kClusters);
        for (Node* at = clusters->next; at != clusters; at = at->next) {
            if (left-- == 0)
                break;
            CopyPlace(places, at->value);
            places += 3;
        }
        Unlock(lock);
        return 1;
    }
    Unlock(lock);
    return 0;
}

// The game's 0x004F51E0: slot 13: slot 12 (part 9's) with two zeros after
// the first argument.
uint32_t __fastcall Slot13(char* self, void*, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    return Virtual<uint32_t>(self, 0x30, a, uint32_t{0}, uint32_t{0}, b, c, d);
}

// The game's 0x004F5E30: slot 16, AddEmitter(scape ID, sound ID, flag,
// value, minimum, maximum): the scape's clusters play this sound while
// their number of points is from minimum to maximum.
uint8_t __fastcall AddEmitter(char* self, void*, uint32_t scapeId, uint32_t soundId, uint32_t flag, uint32_t value,
                              uint32_t minimum, uint32_t maximum)
{
    char* const lock = self + mgr::kLock;
    Lock(lock);
    char* found = reinterpret_cast<char*>(static_cast<std::uintptr_t>(scapeId));
    if (!FindScape(self, nullptr, scapeId, &found)) {
        Unlock(lock);
        return 0;
    }
    Node* const end = At<Node*>(found, scape::kEmitters);
    for (Node* node = end->next; node != end; node = node->next) {
        if (At<uint32_t>(node->value, emitter::kSoundId) == soundId) {
            Unlock(lock);
            return 0;
        }
    }
    char* const made = NewEmitter();
    At<uint8_t>(made, emitter::kFlag) = static_cast<uint8_t>(flag);
    At<uint32_t>(made, emitter::kValue) = value;
    At<uint32_t>(made, emitter::kSoundId) = soundId;
    At<uint32_t>(made, emitter::kMinimum) = minimum;
    At<uint32_t>(made, emitter::kMaximum) = maximum;
    PushBack(At<Node*>(found, scape::kEmitters), made);
    Unlock(lock);
    return 1;
}

// The game's 0x004F5A20: slot 17, RemoveEmitter(scape ID, sound ID). The
// clusters' instances of it are not stopped here.
uint8_t __fastcall RemoveEmitter(char* self, void*, uint32_t scapeId, uint32_t soundId)
{
    char* found = reinterpret_cast<char*>(static_cast<std::uintptr_t>(scapeId));
    if (!FindScape(self, nullptr, scapeId, &found))
        return 0;
    char* const lock = self + mgr::kLock;
    Lock(lock);
    Node* const end = At<Node*>(found, scape::kEmitters);
    for (Node* node = end->next; node != end; node = node->next) {
        void* const which = node->value;
        if (At<uint32_t>(which, emitter::kSoundId) != soundId)
            continue;
        Erase(node);
        Cdecl<void>(addr::SC4_operator_delete, which);
        Unlock(lock);
        return 1;
    }
    Unlock(lock);
    return 0;
}

// The game's 0x004F6AB0: after a cluster has changed: the given emitter and
// then each of the scape's is started or stopped by the cluster's number
// of points, and the instances are told the cluster's place. `this` is not
// used.
void __fastcall Refresh(char*, void*, char* owner, char* which, char* changed)
{
    if (!changed)
        return;
    if (which)
        ApplyEmitter(changed, which);
    Node* const end = At<Node*>(owner, scape::kEmitters);
    for (Node* node = end->next; node != end; node = node->next) {
        if (char* const each = static_cast<char*>(node->value))
            ApplyEmitter(changed, each);
    }
    ClusterSendPlace(changed, nullptr);
}

// The game's 0x004F6B30: slot 14, RemovePoint(scape ID, point ID). A
// cluster left with no points goes back to the pool at once (when the
// manager has its sound service).
uint8_t __fastcall RemovePoint(char* self, void*, uint32_t scapeId, uint32_t pointId)
{
    char* const lock = self + mgr::kLock;
    Lock(lock);
    Node* const end = At<Node*>(self, mgr::kScapes);
    for (Node* node = end->next; node != end; node = node->next) {
        char* const item = static_cast<char*>(node->value);
        if (At<uint32_t>(item, scape::kId) != scapeId)
            continue;
        Node* const clusters = At<Node*>(item, scape::kClusters);
        for (Node* at = clusters->next; at != clusters; at = at->next) {
            char* const from = static_cast<char*>(at->value);
            if (!ClusterRemovePoint(from, nullptr, pointId))
                continue;
            --At<uint32_t>(item, scape::kPointCount);
            if (from) {
                Node* const emitters = At<Node*>(item, scape::kEmitters);
                for (Node* each = emitters->next; each != emitters; each = each->next) {
                    if (char* const which = static_cast<char*>(each->value))
                        ApplyEmitter(from, which);
                }
                ClusterSendPlace(from, nullptr);
            }
            Node* const points = At<Node*>(from, cluster::kPoints);
            if (points->next == points) {
                char* const empty = static_cast<char*>(at->value);
                if (empty && At<void*>(self, mgr::kSound)) {
                    ClusterStopAll(empty, nullptr);
                    DestroyList(empty + cluster::kInstances);
                    DestroyList(empty + cluster::kPoints);
                    PoolFree(empty);
                    Erase(at);
                    --At<uint32_t>(item, scape::kClusterCount);
                }
            }
            Unlock(lock);
            return 1;
        }
    }
    Unlock(lock);
    return 0;
}

// The game's 0x004F6C80: AddPoint(scape, emitter, point, place, unused,
// add): the place joins the nearest cluster whose radius reaches it, or a
// new cluster at the place. The point (a pool item: ID and place) is kept
// in the cluster's list unless the scape has flag 4. Without `add` only
// the search is done and nothing changes.
uint8_t __fastcall AddPoint(char* self, void*, char* owner, char* which, void* point, const float* place, uint32_t,
                            uint32_t add)
{
    if (!owner)
        return 0;
    Node* const clusters = At<Node*>(owner, scape::kClusters);
    double nearest = static_cast<double>(3.4028234663852886e38f); // the float at 0x00A942B0
    char* into = nullptr;
    for (Node* at = clusters->next; at != clusters; at = at->next) {
        char* const each = static_cast<char*>(at->value);
        const double dx = static_cast<double>(place[0]) - static_cast<double>(At<float>(each, cluster::kPlace));
        const double dy = static_cast<double>(place[1]) - static_cast<double>(At<float>(each, cluster::kPlace + 4));
        const double dz = static_cast<double>(place[2]) - static_cast<double>(At<float>(each, cluster::kPlace + 8));
        const double distance = std::sqrt(dz * dz + dy * dy + dx * dx);
        const float rounded = static_cast<float>(distance);
        if (distance > static_cast<double>(At<float>(each, cluster::kRadius)))
            continue;
        if (!(static_cast<double>(rounded) < nearest))
            continue;
        into = each;
        nearest = static_cast<double>(rounded);
    }
    if (!static_cast<uint8_t>(add))
        return 0;
    if (!into) {
        into = ClusterConstruct(static_cast<char*>(PoolAllocate()), nullptr);
        At<uint32_t>(into, cluster::kRadius) = At<uint32_t>(owner, scape::kRadius);
        CopyPlace(into + cluster::kPlace, place);
        PushBack(At<Node*>(owner, scape::kClusters), into);
        ++At<uint32_t>(owner, scape::kClusterCount);
    }
    if (!((At<uint32_t>(owner, scape::kFlags) >> 2) & 1)) {
        PushBack(At<Node*>(into, cluster::kPoints), point);
        ++At<uint32_t>(into, cluster::kPointCount);
    }
    ClusterAddPlace(into, nullptr, place);
    ++At<uint32_t>(into, cluster::kAdded);
    Refresh(self, nullptr, owner, which, into);
    return 1;
}

// Whether a cluster's place is inside (min x, min z, max x, max z).
bool Inside(const char* each, const float* bounds)
{
    const float x = *reinterpret_cast<const float*>(each + cluster::kPlace);
    const float z = *reinterpret_cast<const float*>(each + cluster::kPlace + 8);
    if (!(x >= bounds[0]))
        return false;
    if (!(z >= bounds[1]))
        return false;
    if (!(x <= bounds[2]))
        return false;
    return z <= bounds[3];
}

// The game's 0x004F6E60: Collect(scape, bounds, places, count, points,
// bounded): the places of the scape's clusters (those inside the bounds
// when `bounded`). When the scape has more clusters than `count` has room
// for, the bounds are cut into a square grid of at most that many cells and
// each cell gives the average of the clusters in it. The original writes a
// cell's average at the cell's own index, not at the next free place, while
// `count` gets the number of cells that had clusters.
uint8_t __fastcall Collect(char* self, void*, char* owner, const float* bounds, uint32_t* places, int32_t* count,
                           uint32_t* points, uint32_t bounded)
{
    char* const lock = self + mgr::kLock;
    Lock(lock);
    const int32_t have = At<int32_t>(owner, scape::kClusterCount);
    if (have == 0) {
        *count = 0;
        *points = 0;
        Unlock(lock);
        return 0;
    }
    *points = At<uint32_t>(owner, scape::kPointCount);
    const int32_t room = *count;
    const bool check = static_cast<uint8_t>(bounded) != 0;
    if (have <= room) {
        *count = 0;
        Node* const clusters = At<Node*>(owner, scape::kClusters);
        for (Node* at = clusters->next; at != clusters; at = at->next) {
            const char* const each = static_cast<const char*>(at->value);
            if (check && !Inside(each, bounds))
                continue;
            const int32_t index = *count;
            *count = index + 1;
            CopyPlace(places + 3 * index, each);
        }
        Unlock(lock);
        return 1;
    }

    const int32_t side = Ftol(std::sqrt(static_cast<double>(room)));
    const double sideValue = static_cast<double>(side);
    const int32_t cellWidth = Ftol((static_cast<double>(bounds[2]) - static_cast<double>(bounds[0])) / sideValue);
    const int32_t cellDepth = Ftol((static_cast<double>(bounds[3]) - static_cast<double>(bounds[1])) / sideValue);
    const int32_t cells = static_cast<int32_t>(static_cast<uint32_t>(side) * static_cast<uint32_t>(side));
    char* const vector = self + mgr::kCells;
    const int32_t size = static_cast<int32_t>(At<char*>(vector, 4) - At<char*>(vector, 0)) >> 4;
    if (static_cast<uint32_t>(cells) > static_cast<uint32_t>(size))
        This<void>(addr::SC4Audio_F4F6DF0, vector, static_cast<uint32_t>(cells));
    for (int32_t i = 0; i < cells; ++i) {
        Cell* const cell = At<Cell*>(vector, 0) + i;
        cell->x = 0.0f;
        cell->y = 0.0f;
        cell->z = 0.0f;
        cell->count = 0;
    }

    Node* const clusters = At<Node*>(owner, scape::kClusters);
    for (Node* at = clusters->next; at != clusters; at = at->next) {
        const char* const each = static_cast<const char*>(at->value);
        const float x = *reinterpret_cast<const float*>(each + cluster::kPlace);
        const float y = *reinterpret_cast<const float*>(each + cluster::kPlace + 4);
        const float z = *reinterpret_cast<const float*>(each + cluster::kPlace + 8);
        if (check && !Inside(each, bounds))
            continue;
        const int32_t row =
            Ftol((static_cast<double>(z) - static_cast<double>(bounds[1])) / static_cast<double>(cellDepth));
        const int32_t column =
            Ftol((static_cast<double>(x) - static_cast<double>(bounds[0])) / static_cast<double>(cellWidth));
        const int32_t index = static_cast<int32_t>(static_cast<uint32_t>(row) * static_cast<uint32_t>(side) +
                                                   static_cast<uint32_t>(column));
        if (index >= cells)
            continue;
        Cell* const cell = reinterpret_cast<Cell*>(At<char*>(vector, 0) + (static_cast<uint32_t>(index) << 4));
        cell->x = static_cast<float>(static_cast<double>(x) + static_cast<double>(cell->x));
        cell->y = static_cast<float>(static_cast<double>(y) + static_cast<double>(cell->y));
        cell->z = static_cast<float>(static_cast<double>(cell->z) + static_cast<double>(z));
        ++cell->count;
    }

    *count = 0;
    for (int32_t i = 0; i < cells; ++i) {
        const Cell* const cell = At<Cell*>(vector, 0) + i;
        if (cell->count == 0)
            continue;
        const double share =
            static_cast<double>(Global<float>(addr::SC4AudioScape_One)) / static_cast<double>(cell->count);
        float* const to = reinterpret_cast<float*>(places + 3 * i);
        const float ax = static_cast<float>(share * static_cast<double>(cell->x));
        const float ay = static_cast<float>(share * static_cast<double>(cell->y));
        const float az = static_cast<float>(share * static_cast<double>(cell->z));
        to[2] = az;
        to[0] = ax;
        to[1] = ay;
        ++*count;
    }
    Unlock(lock);
    return 1;
}

// The game's 0x004F67C0: the system service's OnTick (`this` is the
// service part). Leaving clusters lose the milliseconds since the last
// tick and go back to the pool at zero; then each cluster's two levels move
// a seventh of the way to their targets (the whole way when that is less
// than one).
uint8_t __fastcall OnTick(char* service, void*, uint32_t)
{
    char* const self = service - mgr::kService;
    char* const tickLock = self + mgr::kTickLock;
    Lock(tickLock);
    const uint32_t now = This<uint32_t>(addr::GZStopwatch_ElapsedMilliseconds, self + mgr::kStopwatch);
    Node* const leaving = At<Node*>(self, mgr::kLeaving);
    const uint32_t passed = now - At<uint32_t>(self, mgr::kLastTime);
    At<uint32_t>(self, mgr::kLastTime) = now;
    Node* node = leaving->next;
    while (node != leaving) {
        char* const gone = static_cast<char*>(node->value);
        if (!gone) {
            Unlock(tickLock);
            return 0;
        }
        uint32_t& left = At<uint32_t>(gone, cluster::kTimeLeft);
        left -= passed < left ? passed : left;
        if (left != 0) {
            node = node->next;
            continue;
        }
        ClusterStopAll(gone, nullptr);
        node = Erase(node);
        ClusterDestroyInline(gone);
        PoolFree(gone);
    }

    char* const lock = self + mgr::kLock;
    Lock(lock);
    Node* const scapes = At<Node*>(self, mgr::kScapes);
    for (Node* item = scapes->next; item != scapes; item = item->next) {
        Node* const clusters = At<Node*>(item->value, scape::kClusters);
        for (Node* at = clusters->next; at != clusters; at = at->next) {
            char* const each = static_cast<char*>(at->value);
            int32_t* const levels = &At<int32_t>(each, cluster::kLevels);
            if (levels[0] == levels[2] && levels[1] == levels[3])
                continue;
            for (uint32_t which = 0; which < 2; ++which) {
                const int32_t level = levels[which];
                const int32_t gap =
                    static_cast<int32_t>(static_cast<uint32_t>(levels[which + 2]) - static_cast<uint32_t>(level));
                int32_t step = gap / 7;
                if (step == 0) {
                    if (gap == 0)
                        continue;
                    step = gap;
                }
                ClusterSetLevel(each, nullptr, which,
                                static_cast<int32_t>(static_cast<uint32_t>(level) + static_cast<uint32_t>(step)));
            }
        }
    }
    Unlock(lock);
    Unlock(tickLock);
    return 1;
}

bool InstallPart8(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[48] = {};
    const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4Audio_F4F5120, reinterpret_cast<void*>(&OptionsLoad)},
        {addr::SC4Audio_F4F51E0, reinterpret_cast<void*>(&Slot13)},
        {addr::SC4Audio_F4F5200, reinterpret_cast<void*>(&TargetDoMessage)},
        {addr::SC4Audio_F4F5210, reinterpret_cast<void*>(&QueryInterface)},
        {addr::SC4Audio_F4F5270, reinterpret_cast<void*>(&Release)},
        {addr::SC4Audio_F4F53E0, reinterpret_cast<void*>(&Init)},
        {addr::SC4Audio_F4F5520, reinterpret_cast<void*>(&ClusterAddPlace)},
        {addr::SC4Audio_F4F55A0, reinterpret_cast<void*>(&ClusterRemovePlace)},
        {addr::SC4Audio_F4F5610, reinterpret_cast<void*>(&StartSound)},
        {addr::SC4Audio_F4F56B0, reinterpret_cast<void*>(&ClusterSetLevel)},
        {addr::SC4Audio_F4F5700, reinterpret_cast<void*>(&Shutdown)},
        {addr::SC4Audio_F4F57B0, reinterpret_cast<void*>(&GetClusters)},
        {addr::SC4Audio_F4F5860, reinterpret_cast<void*>(&ScapeLacksPoint)},
        {addr::SC4Audio_F4F58B0, reinterpret_cast<void*>(&FindScape)},
        {addr::SC4Audio_F4F5900, reinterpret_cast<void*>(&ClusterSendPlace)},
        {addr::SC4Audio_F4F5970, reinterpret_cast<void*>(&ClusterHasEmitter)},
        {addr::SC4Audio_F4F5A20, reinterpret_cast<void*>(&RemoveEmitter)},
        {addr::SC4Audio_F4F5AB0, reinterpret_cast<void*>(&ScapeDropEmitters)},
        {addr::SC4Audio_F4F5AF0, reinterpret_cast<void*>(&ClusterStopEmitter)},
        {addr::SC4Audio_F4F5B80, reinterpret_cast<void*>(&ClusterStopAll)},
        {addr::SC4Audio_F4F5BF0, reinterpret_cast<void*>(&ClusterDropPoints)},
        {addr::SC4Audio_F4F5C70, reinterpret_cast<void*>(&ClusterRemovePoint)},
        {addr::SC4Audio_F4F5CD0, reinterpret_cast<void*>(&ClusterConstruct)},
        {addr::SC4Audio_F4F5E30, reinterpret_cast<void*>(&AddEmitter)},
        {addr::SC4Audio_F4F5F20, reinterpret_cast<void*>(&ScapeClear)},
        {addr::SC4Audio_F4F6050, reinterpret_cast<void*>(&ClusterStartEmitter)},
        {addr::SC4Audio_F4F6140, reinterpret_cast<void*>(&Construct)},
        {addr::SC4Audio_F4F6230, reinterpret_cast<void*>(&ServicePriority)},
        {addr::SC4Audio_F4F6240, reinterpret_cast<void*>(&ServiceIsRunning)},
        {addr::SC4Audio_F4F6250, reinterpret_cast<void*>(&ServiceSetRunning)},
        {addr::SC4Audio_F4F6260, reinterpret_cast<void*>(&ServiceQueryInterface)},
        {addr::SC4Audio_F4F6270, reinterpret_cast<void*>(&ServiceRelease)},
        {addr::SC4Audio_F4F6280, reinterpret_cast<void*>(&TargetQueryInterface)},
        {addr::SC4Audio_F4F6290, reinterpret_cast<void*>(&TargetRelease)},
        {addr::SC4Audio_F4F62A0, reinterpret_cast<void*>(&ServiceInit)},
        {addr::SC4Audio_F4F62B0, reinterpret_cast<void*>(&ServiceShutdown)},
        {addr::SC4Audio_F4F62C0, reinterpret_cast<void*>(&Destruct)},
        {addr::SC4Audio_F4F6340, reinterpret_cast<void*>(&ScapeConstruct)},
        {addr::SC4Audio_F4F64F0, reinterpret_cast<void*>(&AddScape)},
        {addr::SC4Audio_F4F65C0, reinterpret_cast<void*>(&ClearScape)},
        {addr::SC4Audio_F4F6620, reinterpret_cast<void*>(&ClearScapes)},
        {addr::SC4Audio_F4F67C0, reinterpret_cast<void*>(&OnTick)},
        {addr::SC4Audio_F4F69E0, reinterpret_cast<void*>(&DeletingDestruct)},
        {addr::SC4Audio_F4F6A00, reinterpret_cast<void*>(&RemoveScape)},
        {addr::SC4Audio_F4F6AB0, reinterpret_cast<void*>(&Refresh)},
        {addr::SC4Audio_F4F6B30, reinterpret_cast<void*>(&RemovePoint)},
        {addr::SC4Audio_F4F6C80, reinterpret_cast<void*>(&AddPoint)},
        {addr::SC4Audio_F4F6E60, reinterpret_cast<void*>(&Collect)},
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

const bool g_part8Registered = registry::Register({
    "audio.part8",
    "audio",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Audio_F4F5120,
    "Our audio scape manager (scapes, clusters, emitters, its tick) and the options dialog's window loader "
    "(docs/audio.md)",
    false,
    InstallPart8,
});

} // namespace
} // namespace opensc4::audio
