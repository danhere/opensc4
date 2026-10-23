// The GZ framework's sound mixer 0xBDDA72DD (docs/gzui.md, part u8), on the
// game's own objects: one of the five classes the framework's sound
// director registers. It holds several scores (sound services 0x3C1CF59E,
// part u6) by key, turns them on and off, and plays their instances itself:
// every few seconds one instance of the scores that are on, drawn by its
// description's weight, placed by the neighbour grids and played. It answers
// the sound service's interface 0xDC1CF595 (its part at +4, 37 slots) and
// has a message target at +8 that the framework's sound service calls.
// Replacements, each off by default:
//
//   framework.soundmix.object   constructor, QueryInterface, Release, the
//                               destructor and the deleting destructor
//   framework.soundmix.play     Prepare (slot 4), add a score (5), a score's
//                               table (6), Play, IsPlaying and Stop (8 to
//                               10), a score on or off, the pool rebuilt,
//                               the message target's slot 3 (the tick), the
//                               neighbour pick
//   framework.soundmix.control  slots 11 to 16, 20, 21, 26 and 27: the level,
//                               the row and column and the other settings
//                               every score gets, Reseed; the primary
//                               vtable's slots 6 to 8
//   framework.soundmix.grid     slots 28 to 35: the neighbour grids, their
//                               size, the distances, the key
//
// Still the game's: the adjustors 0x00949B83, 0x00949B8B, 0x00949B93,
// 0x00949B9B and 0x00949BA3; the AddRef 0x0094D34F and the slots 0x009CFA36
// and 0x0093878E (outside the range); the shared getters 0x00949B2B (slot
// 19) and 0x00949B32 (18), the shared stubs 0x00949ADE (7), 0x00949B47 (22
// to 25) and 0x00949B73 (36); and the STLport map and list code the class
// uses (0x009490C9, 0x00949188, 0x009491A5 to 0x009495CE but 0x00949222
// and 0x00949348, 0x00949873 to 0x009498A7). Ours call each other
// directly where the game does; a virtual call stays one.
//
// The game's methods answer a byte (AL) over whatever its last call or
// value left in EAX; ours give the same EAX where it comes from a call or a
// value and 0 above AL where the game leaves its caller's EAX.
//
// Every detour is entered through a thunk that keeps ECX and EDX. The
// groups go in at Attach, with the rest of the framework's sound layer.
#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/thunk.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::gzui {
namespace {

// ====================================================================================
// Helpers

template <typename R, typename... Args>
inline R CallThis(std::uintptr_t address, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(address)(self, args...);
}

template <typename R, typename... Args>
inline R CallCdecl(std::uintptr_t address, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(address)(args...);
}

template <typename T>
inline T& At(void* base, std::size_t offset)
{
    return *reinterpret_cast<T*>(static_cast<char*>(base) + offset);
}

template <typename R, typename... Args>
inline R Virtual(void* object, std::size_t byteOffset, Args... args)
{
    void** const vtable = *reinterpret_cast<void***>(object);
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(vtable[byteOffset / 4])(object, args...);
}

inline uint32_t Bits(const void* pointer) { return static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(pointer)); }

// A byte answer over what the game leaves in the rest of EAX.
inline uint32_t Answer(uint32_t eax, bool value) { return (eax & ~0xFFu) | (value ? 1u : 0u); }
inline bool Yes(uint32_t eax) { return (eax & 0xFF) != 0; }

inline uint32_t Release(void* object) { return Virtual<uint32_t>(object, 0x8); }

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("gzui: soundmix: first %s", name);
}

#define OPENSC4_FIRST(name)                                                                                    \
    do {                                                                                                       \
        static bool logged_ = false;                                                                           \
        if (!logged_)                                                                                          \
            FirstCall(name, logged_);                                                                          \
    } while (0)

// The STLport red-black tree's next node (0x0040D4C0, cdecl).
char* Next(char* node) { return CallCdecl<char*>(addr::SC4Tree_Next, node); }

constexpr uint32_t kIID_Mixer = 0xDDDA72F1;
constexpr uint32_t kIID_Service = 0xDC1CF595;
constexpr uint32_t kCLSID_Service = 0x3C1CF59E;

// ====================================================================================
// The mixer (0x190 bytes; the sound service's interface at +4, the message
// target at +8). Maps and lists are STLport's: a map's header node at its
// +0 (the leftmost node at the header's +8), a node's key at +0x10 and its
// value at +0x14; a list's header node at its +0, a node's next at +0 and
// its value at +8.

constexpr std::size_t kInterface = 0x4;
constexpr std::size_t kTarget = 0x8;
constexpr std::size_t kRefs = 0x10;
constexpr std::size_t kSound = 0x14;      // the framework's sound service (counted)
constexpr std::size_t kScores = 0x18;     // map: key to sound service (counted)
constexpr std::size_t kTables = 0x24;     // map: key to sound table (counted)
constexpr std::size_t kOn = 0x30;         // map: key to bool
constexpr std::size_t kPool = 0x3C;       // map: index to instance
constexpr std::size_t kPoolDescs = 0x48;  // map: index to description
constexpr std::size_t kSounding = 0x54;   // list of instances
constexpr std::size_t kGridRows = 0x58;
constexpr std::size_t kGridColumns = 0x5C;
constexpr std::size_t kPlace = 0x60;      // byte: the instance's slot 15
constexpr std::size_t kGrid = 0x61;       // 3 by 3 bytes
constexpr std::size_t kGridA = 0x6C;      // 3 by 3 dwords, three of them
constexpr std::size_t kGridB = 0x90;
constexpr std::size_t kGridC = 0xB4;
constexpr std::size_t kDistances = 0xD8;  // two dwords: the instance's slot 17
constexpr std::size_t kPick = 0xE0;       // the last pick: row, column
constexpr std::size_t kWatch = 0xE8;      // a stopwatch (0x18 bytes)
constexpr std::size_t kStartTime = 0x100;
constexpr std::size_t kLastTime = 0x104;
constexpr std::size_t kPoolSize = 0x108;
constexpr std::size_t kWeights = 0x10C;   // the pool's descriptions' weights
constexpr std::size_t kScoresOn = 0x110;
constexpr std::size_t kRandom = 0x118;    // a generator (0x18 bytes)
constexpr std::size_t kRunning = 0x134;   // byte
constexpr std::size_t kLevel = 0x138;
constexpr std::size_t kColumn = 0x13C;
constexpr std::size_t kRow = 0x140;
constexpr std::size_t kKey = 0x180;       // type, group, instance

// The message target's part from the interface's: (this - 4) ? this + 4.
void* TargetOf(char* part) { return part - kInterface ? part + 4 : nullptr; }

// The game's 0x00949902.
char* __fastcall MixerConstruct(char* self, void*)
{
    OPENSC4_FIRST("MixerConstruct");
    At<std::uintptr_t>(self, kInterface) = addr::cGZSoundMixer_ServiceBaseVtable;
    At<std::uintptr_t>(self, kTarget) = addr::cGZSoundService_TargetBaseVtable;
    At<std::uintptr_t>(self, 0) = addr::cGZSoundMixer_Vtable;
    At<std::uintptr_t>(self, kInterface) = addr::cGZSoundMixer_ServiceVtable;
    At<std::uintptr_t>(self, kTarget) = addr::cGZSoundMixer_TargetVtable;
    At<uint8_t>(self, 0xC) = 0;
    At<uint32_t>(self, kRefs) = 0;
    At<uint32_t>(self, kSound) = 0;
    CallThis<void>(addr::cGZDBSegmentPackedFile_HolesMapConstruct, self + kScores);
    CallThis<void>(addr::cGZDBSegmentPackedFile_HolesMapConstruct, self + kTables);
    CallThis<void>(addr::SC4TrafficZone_Construct, self + kOn);
    CallThis<void>(addr::cGZDBSegmentPackedFile_HolesMapConstruct, self + kPool);
    CallThis<void>(addr::cGZDBSegmentPackedFile_HolesMapConstruct, self + kPoolDescs);
    uint8_t allocator;
    CallThis<void>(addr::cGZDBSegmentPackedFile_ListConstruct, self + kSounding, &allocator);
    At<uint32_t>(self, kGridRows) = 2;
    At<uint32_t>(self, kGridColumns) = 2;
    At<uint8_t>(self, kPlace) = 1;
    At<uint32_t>(self, kDistances) = 100;
    At<uint32_t>(self, kDistances + 4) = 1000;
    At<uint32_t>(self, kPick) = 0;
    At<uint32_t>(self, kPick + 4) = 0;
    CallThis<void*>(addr::SC4Stopwatch_Construct, self + kWatch, 4u);
    At<uint32_t>(self, kPoolSize) = 0;
    At<uint32_t>(self, kWeights) = 0;
    At<uint32_t>(self, kScoresOn) = 0;
    CallThis<void*>(addr::SC4Random_Construct, self + kRandom, -1);
    At<uint32_t>(self, 0x130) = 0;
    At<uint8_t>(self, kRunning) = 0;
    At<uint8_t>(self, 0x135) = 0;
    for (std::size_t offset = kLevel; offset <= 0x168; offset += 4)
        At<uint32_t>(self, offset) = 0;
    At<uint8_t>(self, 0x16C) = 0;
    At<uint8_t>(self, 0x16D) = 0;
    At<uint32_t>(self, 0x170) = 0;
    At<uint32_t>(self, 0x174) = 0;
    At<uint32_t>(self, 0x178) = 0;
    At<uint8_t>(self, 0x17C) = 0;
    At<uint32_t>(self, kKey) = 0;
    At<uint32_t>(self, kKey + 4) = 0;
    At<uint32_t>(self, kKey + 8) = 0;
    std::memset(self + kGrid, 0, 9);
    At<uint8_t>(self, kGrid + 3) = 1;
    At<uint8_t>(self, kGrid + 1) = 1;
    At<uint8_t>(self, kGrid + 7) = 1;
    At<uint8_t>(self, kGrid + 5) = 1;
    // The three grids: by row, 0 and by column, in steps of +0xD8.
    for (uint32_t row = 0; row < At<uint32_t>(self, kGridRows) + 1; ++row) {
        int32_t down = 1;
        for (uint32_t column = 0; column < At<uint32_t>(self, kGridColumns) + 1; ++column, --down) {
            const std::size_t at = row * 0xC + column * 4;
            At<int32_t>(self, kGridA + at) = At<int32_t>(self, kDistances) * static_cast<int32_t>(row - 1);
            At<uint32_t>(self, kGridB + at) = 0;
            At<int32_t>(self, kGridC + at) = At<int32_t>(self, kDistances) * down;
        }
    }
    return self;
}

// The game's 0x00948E7A (slot 0, also through the adjustors 0x00949B83 and
// 0x00949B93; `ret 8`): 1 and 0xDDDA72F1 give the mixer, 0xDC1CF595 its
// part at +4; AddRef through the primary vtable.
uint32_t __fastcall MixerQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    OPENSC4_FIRST("MixerQueryInterface");
    if (iid == 1 || iid == kIID_Mixer)
        *out = self;
    else if (iid == kIID_Service)
        *out = self ? self + kInterface : nullptr;
    else
        return 0;
    return Answer(Virtual<uint32_t>(self, 0x4), true);
}

// The game's 0x00948E63 (slot 2, also through the adjustors 0x00949B8B and
// 0x00949BA3): a count of 1 or less deletes it through slot 5 (signed).
uint32_t __fastcall MixerRelease(char* self, void*)
{
    OPENSC4_FIRST("MixerRelease");
    const int32_t count = At<int32_t>(self, kRefs);
    if (count <= 1) {
        Virtual<uint32_t>(self, 0x14, 1u);
        return 0;
    }
    At<int32_t>(self, kRefs) = count - 1;
    return static_cast<uint32_t>(count - 1);
}

// The game's 0x00949BC7: the vtables back, the stopwatch, the list and the
// maps destroyed (the generator is not), the sound service released (a
// tail jump: its EAX; else the last map's).
uint32_t __fastcall MixerDestruct(char* self, void*)
{
    OPENSC4_FIRST("MixerDestruct");
    At<std::uintptr_t>(self, 0) = addr::cGZSoundMixer_Vtable;
    At<std::uintptr_t>(self, kInterface) = addr::cGZSoundMixer_ServiceVtable;
    At<std::uintptr_t>(self, kTarget) = addr::cGZSoundMixer_TargetVtable;
    CallThis<void>(addr::SC4Stopwatch_Destruct, self + kWatch);
    CallThis<void>(addr::cGZDBSegmentPackedFile_ListDestruct, self + kSounding);
    CallThis<uint32_t>(addr::cGZSoundMixer_DescMapDestroy, self + kPoolDescs);
    CallThis<uint32_t>(addr::cGZSoundMixer_PoolDestroy, self + kPool);
    CallThis<uint32_t>(addr::SC4UI_F76B9F0, self + kOn);
    CallThis<uint32_t>(addr::cGZSoundMixer_TablesDestroy, self + kTables);
    const uint32_t eax = CallThis<uint32_t>(addr::cGZSoundMixer_ScoresDestroy, self + kScores);
    if (void* const sound = At<void*>(self, kSound))
        return Release(sound);
    return eax;
}

// The game's 0x00949BAB (slot 5, `ret 4`).
char* __fastcall MixerDeletingDestructor(char* self, void*, uint32_t flags)
{
    OPENSC4_FIRST("MixerDeletingDestructor");
    MixerDestruct(self, nullptr);
    if (flags & 1)
        CallCdecl<uint32_t>(addr::SC4_operator_delete, self);
    return self;
}

// The game's 0x00949D19: the pool made again from the scores that are on.
// Nothing while the framework's sound service answers its slot 49. Else
// every instance in the window of each score that is on (key not 0), by
// row then column (signed), goes into the pool under the next index, its
// description too, whose weight (its slot 14) adds up. AL 1 over the maps'
// header (the last step's).
uint32_t __fastcall MixerRebuild(char* self, void*)
{
    OPENSC4_FIRST("MixerRebuild");
    if (void* const sound = At<void*>(self, kSound)) {
        const uint32_t eax = Virtual<uint32_t>(sound, 0xC4);
        if (Yes(eax))
            return Answer(eax, true);
    }
    CallThis<void>(addr::cGZSoundMixer_PoolClear, self + kPool);
    At<uint32_t>(self, kPoolSize) = 0;
    At<uint32_t>(self, kWeights) = 0;
    At<uint32_t>(self, kScoresOn) = 0;
    char* node = At<char*>(At<char*>(self, kScores), 8);
    while (node != At<char*>(self, kScores)) {
        uint32_t key = At<uint32_t>(node, 0x10);
        if (key != 0) {
            char* const on = CallThis<char*>(addr::SC4FlagMap_Find, self + kOn, &key);
            if (on != At<char*>(self, kOn) && At<uint8_t>(on, 0x14) != 0) {
                ++At<uint32_t>(self, kScoresOn);
                if (void* const score = At<void*>(node, 0x14)) {
                    Virtual<uint32_t>(score, 0x18, 0u);
                    const int32_t firstRow = Virtual<int32_t>(score, 0x58);
                    const int32_t lastRow = Virtual<int32_t>(score, 0x5C);
                    const int32_t firstColumn = Virtual<int32_t>(score, 0x60);
                    const int32_t lastColumn = Virtual<int32_t>(score, 0x64);
                    for (int32_t row = firstRow; row <= lastRow; ++row) {
                        for (int32_t column = firstColumn; column <= lastColumn; ++column) {
                            void* const instance = Virtual<void*>(score, 0x68, row, column, 0u);
                            if (!instance)
                                continue;
                            uint32_t index = At<uint32_t>(self, kPoolSize);
                            *CallThis<void**>(addr::SC4PtrMap_Index, self + kPool, &index) = instance;
                            if (void* const desc = Virtual<void*>(instance, 0xC)) {
                                index = At<uint32_t>(self, kPoolSize);
                                *CallThis<void**>(addr::SC4PtrMap_Index, self + kPoolDescs, &index) = desc;
                                At<uint32_t>(self, kWeights) += Virtual<uint32_t>(desc, 0x38);
                            }
                            ++At<uint32_t>(self, kPoolSize);
                        }
                    }
                }
            }
        }
        node = Next(node);
    }
    return Answer(Bits(node), true);
}

// The game's 0x00949EA4 (slot 4, `ret 8`): the score's flag set; turned
// off, the instances in its window get their slot 7 (signed rows and
// columns). Then the pool rebuilt; AL 1 over its EAX.
uint32_t __fastcall MixerEnable(char* self, void*, uint32_t key, uint32_t on)
{
    OPENSC4_FIRST("MixerEnable");
    *CallThis<uint8_t*>(addr::SC4UI_F76BD80, self + kOn, &key) = static_cast<uint8_t>(on);
    if (static_cast<uint8_t>(on) == 0) {
        char* const node = CallThis<char*>(addr::cGZCOM_ClassMapFind, self + kScores, &key);
        if (node != At<char*>(self, kScores)) {
            void* score = nullptr;
            CallThis<void*>(addr::SC4RefPtr_AssignFrom, &score, node + 0x14);
            if (score) {
                Virtual<uint32_t>(score, 0x18, 0u);
                const int32_t firstRow = Virtual<int32_t>(score, 0x58);
                const int32_t lastRow = Virtual<int32_t>(score, 0x5C);
                const int32_t firstColumn = Virtual<int32_t>(score, 0x60);
                const int32_t lastColumn = Virtual<int32_t>(score, 0x64);
                for (int32_t row = firstRow; row <= lastRow; ++row) {
                    for (int32_t column = firstColumn; column <= lastColumn; ++column) {
                        if (void* const instance = Virtual<void*>(score, 0x68, row, column, 0u))
                            Virtual<uint32_t>(instance, 0x1C);
                    }
                }
                Release(score);
            }
        }
    }
    return Answer(MixerRebuild(self, nullptr), true);
}

// The game's 0x00949B1C (slot 6, `ret 4`): +0x144; AL 1 over the value.
uint32_t __fastcall MixerSet144(char* self, void*, uint32_t value)
{
    OPENSC4_FIRST("MixerSet144");
    At<uint32_t>(self, 0x144) = value;
    return Answer(value, true);
}

// The game's 0x00949B39 (slot 7).
uint32_t __fastcall MixerGet144(char* self, void*)
{
    OPENSC4_FIRST("MixerGet144");
    return At<uint32_t>(self, 0x144);
}

// The game's 0x00949B76 (slot 8, `ret 4`): the byte +0x16C; AL the byte over
// the caller's EAX.
uint32_t __fastcall MixerSet16C(char* self, void*, uint32_t value)
{
    OPENSC4_FIRST("MixerSet16C");
    At<uint8_t>(self, 0x16C) = static_cast<uint8_t>(value);
    return value & 0xFF;
}

// The game's 0x00948F62 (`ret 4`): as the sound service's 0x00947AD9, on the
// mixer's grids: a cell of the byte grid (rows +0x58 + 1, columns +0x5C +
// 1, unsigned) drawn (cRZRandom::Below of 1000 a cell set, the generator at
// +0x118) among those set but the last pick +0xE0 (kept when the draw
// passes them all); the instance's slot 15 the byte +0x60, 17 +0xD8 and
// +0xDC, 16 the three dword grids' cells. AL 0 (EAX 0) with no cell set;
// else AL 1 over slot 16's EAX.
uint32_t __fastcall MixerPlaceInstance(char* self, void*, void* instance)
{
    OPENSC4_FIRST("MixerPlaceInstance");
    uint32_t marked = 0;
    if (At<uint32_t>(self, kGridRows) + 1 == 0)
        return 0;
    const uint32_t columns = At<uint32_t>(self, kGridColumns) + 1;
    const uint8_t* cells = reinterpret_cast<const uint8_t*>(self + kGrid);
    for (uint32_t left = At<uint32_t>(self, kGridRows) + 1; left != 0; --left, cells += 3) {
        if (columns == 0)
            continue;
        const uint32_t limit = At<uint32_t>(self, kGridColumns) + 1;
        uint32_t column = 0;
        do {
            if (cells[column])
                ++marked;
            ++column;
        } while (column < limit);
    }
    if (marked == 0)
        return 0;
    uint32_t pickRow = At<uint32_t>(self, kPick);
    uint32_t pickColumn = At<uint32_t>(self, kPick + 4);
    uint32_t draw = CallThis<uint32_t>(addr::cRZRandom_Below, self + kRandom, marked * 1000);
    bool found = false;
    if (At<uint32_t>(self, kGridRows) + 1 != 0) {
        cells = reinterpret_cast<const uint8_t*>(self + kGrid);
        uint32_t row = 0;
        do {
            if (found)
                break;
            if (At<uint32_t>(self, kGridColumns) + 1 != 0) {
                uint32_t column = 0;
                do {
                    if (cells[column] && !(row == At<uint32_t>(self, kPick) && column == At<uint32_t>(self, kPick + 4))) {
                        if (draw < 1000) {
                            pickRow = row;
                            pickColumn = column;
                            found = true;
                            break;
                        }
                        draw -= 1000;
                    }
                    ++column;
                } while (column < At<uint32_t>(self, kGridColumns) + 1);
            }
            cells += 3;
            ++row;
        } while (row < At<uint32_t>(self, kGridRows) + 1);
    }
    At<uint32_t>(self, kPick) = pickRow;
    At<uint32_t>(self, kPick + 4) = pickColumn;
    Virtual<uint32_t>(instance, 0x3C, static_cast<uint32_t>(At<uint8_t>(self, kPlace)));
    Virtual<uint32_t>(instance, 0x44, At<uint32_t>(self, kDistances), At<uint32_t>(self, kDistances + 4));
    const std::size_t at = (pickColumn + pickRow * 3) * 4;
    return Answer(Virtual<uint32_t>(instance, 0x40, At<uint32_t>(self, kGridA + at), At<uint32_t>(self, kGridB + at),
                                    At<uint32_t>(self, kGridC + at)),
                  true);
}

// The game's 0x0094970D (the message target's slot 3, `this` the part at
// +8; `ret 4`, the argument not read): nothing while the framework's sound
// service answers its slot 49 (AL over its EAX). Else the first sounding
// instance that no longer plays (slot 10; unchecked) leaves the list. With
// a level, once the stopwatch has passed (7 less the scores on, at least 2)
// seconds since +0x104 (unsigned) and fewer than 4 sound: a draw below the
// pool's weights picks the first instance whose running sum of weights
// reaches it (instances or descriptions missing skipped); it joins the
// list, gets the level (slot 9), a place (0x00948F62) and plays (slot 5);
// the stopwatch restarts either way and +0x104 is 0. Then every sounding
// instance's Update (slot 6). Answers the last Update's EAX (the list's
// header with none).
uint32_t __fastcall MixerTick(char* target, void*, uint32_t)
{
    OPENSC4_FIRST("MixerTick");
    char* const self = target - kTarget;
    if (void* const sound = At<void*>(self, kSound)) {
        const uint32_t eax = Virtual<uint32_t>(sound, 0xC4);
        if (Yes(eax))
            return eax;
    }
    char* const list = self + kSounding;
    for (char* node = At<char*>(At<char*>(list, 0), 0); node != At<char*>(list, 0); node = At<char*>(node, 0)) {
        if (!Yes(Virtual<uint32_t>(At<void*>(node, 8), 0x28))) {
            char* after = nullptr;
            CallThis<void*>(addr::SC4PtrList_Erase, list, &after, node);
            break;
        }
    }
    int32_t wait = 7 - At<int32_t>(self, kScoresOn);
    if (wait < 2)
        wait = 2;
    if (At<uint32_t>(self, kLevel) != 0) {
        const uint32_t elapsed = CallThis<uint32_t>(addr::GZStopwatch_ElapsedMilliseconds, self + kWatch);
        if (elapsed - At<uint32_t>(self, kLastTime) > static_cast<uint32_t>(wait * 1000)) {
            uint32_t sounding = 0;
            for (char* node = At<char*>(At<char*>(list, 0), 0); node != At<char*>(list, 0); node = At<char*>(node, 0))
                ++sounding;
            if (sounding < 4) {
                const uint32_t draw = CallThis<uint32_t>(addr::cRZRandom_Below, self + kRandom, At<uint32_t>(self, kWeights));
                uint32_t sum = 0;
                for (char* node = At<char*>(At<char*>(self, kPool), 8); node != At<char*>(self, kPool); node = Next(node)) {
                    void* instance = At<void*>(node, 0x14);
                    if (!instance)
                        continue;
                    void* const desc = Virtual<void*>(instance, 0xC);
                    if (!desc)
                        continue;
                    sum += Virtual<uint32_t>(desc, 0x38);
                    if (sum < draw)
                        continue;
                    CallThis<void>(addr::SC4PtrList_PushBack, list, &instance);
                    Virtual<uint32_t>(instance, 0x24, At<uint32_t>(self, kLevel));
                    MixerPlaceInstance(self, nullptr, instance);
                    Virtual<uint32_t>(instance, 0x14);
                    break;
                }
            }
            CallThis<void>(addr::SC4Stopwatch_Reset, self + kWatch);
            CallThis<void>(addr::GZStopwatch_Start, self + kWatch);
            At<uint32_t>(self, kLastTime) = 0;
        }
    }
    uint32_t eax = Bits(At<char*>(list, 0));
    for (char* node = At<char*>(At<char*>(list, 0), 0); node != At<char*>(list, 0); node = At<char*>(node, 0)) {
        if (void* const instance = At<void*>(node, 8))
            eax = Virtual<uint32_t>(instance, 0x18);
    }
    return eax;
}

// ====================================================================================
// The sound service's interface (the part at +4, `self` below; the mixer is
// self - 4)

// The game's 0x009490E6 (slot 4): the framework's sound service
// (SC4::Singleton913C9E) held at +0x14; EAX whether there is one.
uint32_t __fastcall MixerPrepare(char* part, void*)
{
    OPENSC4_FIRST("MixerPrepare");
    char* const self = part - kInterface;
    CallThis<void*>(addr::SC4RefPtr_Assign, self + kSound, CallCdecl<void*>(addr::SC4_Singleton913C9E));
    return At<void*>(self, kSound) != nullptr ? 1u : 0u;
}

// The game's 0x00949C27 (slot 5, `ret 8`): a new sound service (0x3C1CF59E
// through the COM) loads the name (its slot 5, with 0); it is kept under
// the key with its table (slot 6), the key turned off (the primary slot 4),
// and it gets the mixer's level, column, row and +0x14C (its slots 11, 14,
// 13 and 15). AL 1 over the release of ours; AL 0 over the COM's EAX when
// none is made, over the release when it does not load.
uint32_t __fastcall MixerAdd(char* part, void*, const void* name, uint32_t key)
{
    OPENSC4_FIRST("MixerAdd");
    char* const self = part - kInterface;
    void* score = nullptr;
    void* const com = CallCdecl<void*>(addr::RZGetCOM);
    const uint32_t made = Virtual<uint32_t>(com, 0xC, kCLSID_Service, kIID_Service, &score);
    if (!score)
        return Answer(made, false);
    if (!Yes(Virtual<uint32_t>(score, 0x14, name, 0u)))
        return Answer(Release(score), false);
    void* const kept = CallThis<void*>(addr::cGZSoundMixer_ScoresIndex, self + kScores, &key);
    CallThis<void*>(addr::SC4RefPtr_AssignFrom, kept, &score);
    void* const table = Virtual<void*>(score, 0x18, 0u);
    void** const slot = CallThis<void**>(addr::SC4RefMap_Index, self + kTables, &key);
    CallThis<void**>(addr::GZRefPtr_Assign, slot, table);
    Virtual<uint32_t>(self, 0x10, key, 0u);
    Virtual<uint32_t>(score, 0x2C, At<uint32_t>(self, kLevel));
    Virtual<uint32_t>(score, 0x38, At<uint32_t>(self, kColumn));
    Virtual<uint32_t>(score, 0x34, At<uint32_t>(self, kRow));
    Virtual<uint32_t>(score, 0x3C, At<uint32_t>(self, 0x14C));
    return Answer(Release(score), true);
}

// The game's 0x00949348 (slot 6, `ret 4`): the table kept under the key,
// found by walking the map; 0 when none.
uint32_t __fastcall MixerTable(char* part, void*, uint32_t key)
{
    OPENSC4_FIRST("MixerTable");
    char* const self = part - kInterface;
    char* const header = At<char*>(self, kTables);
    for (char* node = At<char*>(header, 8); node != header; node = Next(node)) {
        if (At<uint32_t>(node, 0x10) == key)
            return At<uint32_t>(node, 0x14);
    }
    return 0;
}

// The game's 0x00949100 (slot 8): once, the message target handed to the
// framework's sound service (its slot 30, with 250; unchecked) and the
// stopwatch started, its time at +0x100 and +0x104. AL 1 over the time;
// over the caller's EAX when already running (ours 1).
uint32_t __fastcall MixerPlay(char* part, void*)
{
    OPENSC4_FIRST("MixerPlay");
    char* const self = part - kInterface;
    if (At<uint8_t>(self, kRunning) != 0)
        return 1;
    Virtual<uint32_t>(At<void*>(self, kSound), 0x78, TargetOf(part), 0xFAu);
    CallThis<void>(addr::SC4Stopwatch_Reset, self + kWatch);
    CallThis<void>(addr::GZStopwatch_Start, self + kWatch);
    const uint32_t time = CallThis<uint32_t>(addr::GZStopwatch_ElapsedMilliseconds, self + kWatch);
    At<uint32_t>(self, kStartTime) = time;
    At<uint32_t>(self, kLastTime) = time;
    At<uint8_t>(self, kRunning) = 1;
    return Answer(time, true);
}

// The game's 0x00949AE1 (slot 9): the byte +0x134 over the caller's EAX.
uint32_t __fastcall MixerIsPlaying(char* part, void*)
{
    OPENSC4_FIRST("MixerIsPlaying");
    return At<uint8_t>(part - kInterface, kRunning);
}

// The game's 0x0094915A (slot 10): while running, the message target taken
// back (slot 31). AL 1 over its EAX; over the caller's when not running
// (ours 1).
uint32_t __fastcall MixerStop(char* part, void*)
{
    OPENSC4_FIRST("MixerStop");
    char* const self = part - kInterface;
    if (At<uint8_t>(self, kRunning) == 0)
        return 1;
    const uint32_t eax = Virtual<uint32_t>(At<void*>(self, kSound), 0x7C, TargetOf(part));
    At<uint8_t>(self, kRunning) = 0;
    return Answer(eax, true);
}

// The game's 0x00949222 (slot 11, `ret 4`): the level, and every sounding
// instance's (its slot 9). AL 1 over the last call's EAX (the list's header
// with none).
uint32_t __fastcall MixerSetLevel(char* part, void*, uint32_t level)
{
    OPENSC4_FIRST("MixerSetLevel");
    char* const self = part - kInterface;
    At<uint32_t>(self, kLevel) = level;
    char* const header = At<char*>(self, kSounding);
    uint32_t eax = Bits(header);
    for (char* node = At<char*>(header, 0); node != At<char*>(self, kSounding); node = At<char*>(node, 0)) {
        if (void* const instance = At<void*>(node, 8))
            eax = Virtual<uint32_t>(instance, 0x24, At<uint32_t>(self, kLevel));
    }
    return Answer(eax, true);
}

// The game's 0x00948EBB (slot 12, the sound service's Fade; `ret 0xC`): the
// byte +0x17C the third argument's, +0x16D 1. AL 1 over the caller's EAX.
uint32_t __fastcall MixerSet17C(char* part, void*, uint32_t, uint32_t, uint32_t value)
{
    OPENSC4_FIRST("MixerSet17C");
    char* const self = part - kInterface;
    At<uint8_t>(self, 0x17C) = static_cast<uint8_t>(value);
    At<uint8_t>(self, 0x16D) = 1;
    return 1;
}

// Slots 13 to 15 (`ret 4`): the value kept and given to every score
// (unchecked) by the same slot, the pool rebuilt; AL 1 over its EAX.
uint32_t EveryScore(char* part, std::size_t field, std::size_t slot, uint32_t value)
{
    char* const self = part - kInterface;
    At<uint32_t>(self, field) = value;
    for (char* node = At<char*>(At<char*>(self, kScores), 8); node != At<char*>(self, kScores); node = Next(node))
        Virtual<uint32_t>(At<void*>(node, 0x14), slot, value);
    return Answer(MixerRebuild(self, nullptr), true);
}

// The game's 0x00949FF9 (slot 13, SetRow).
uint32_t __fastcall MixerSetRow(char* part, void*, uint32_t row)
{
    OPENSC4_FIRST("MixerSetRow");
    return EveryScore(part, kRow, 0x34, row);
}

// The game's 0x00949FB9 (slot 14, SetColumn).
uint32_t __fastcall MixerSetColumn(char* part, void*, uint32_t column)
{
    OPENSC4_FIRST("MixerSetColumn");
    return EveryScore(part, kColumn, 0x38, column);
}

// The game's 0x0094A039 (slot 15).
uint32_t __fastcall MixerSet14C(char* part, void*, uint32_t value)
{
    OPENSC4_FIRST("MixerSet14C");
    return EveryScore(part, 0x14C, 0x3C, value);
}

// The game's 0x00949E8D (slot 16, `ret 4`): +0x164, the pool rebuilt; AL 1
// over its EAX.
uint32_t __fastcall MixerSet164(char* part, void*, uint32_t value)
{
    OPENSC4_FIRST("MixerSet164");
    char* const self = part - kInterface;
    At<uint32_t>(self, 0x164) = value;
    return Answer(MixerRebuild(self, nullptr), true);
}

// The game's 0x00949B40 (slot 20).
uint32_t __fastcall MixerGet14C(char* part, void*)
{
    OPENSC4_FIRST("MixerGet14C");
    return At<uint32_t>(part - kInterface, 0x14C);
}

// The game's 0x00949AE8 (slot 21, `ret 4`): the byte +0x135; AL 1 over the
// caller's EAX.
uint32_t __fastcall MixerSet135(char* part, void*, uint32_t value)
{
    OPENSC4_FIRST("MixerSet135");
    At<uint8_t>(part - kInterface, 0x135) = static_cast<uint8_t>(value);
    return 1;
}

// The game's 0x00949B4A (slot 26, the instance at a row and column; `ret
// 0xC`): none.
uint32_t __fastcall MixerNoInstance(char*, void*, uint32_t, uint32_t, uint32_t)
{
    OPENSC4_FIRST("MixerNoInstance");
    return 0;
}

// The game's 0x00949F6F (slot 27, `ret 4`): every score reseeded (slot 27;
// unchecked), and the mixer's generator with each; the pool rebuilt. AL 1
// over its EAX.
uint32_t __fastcall MixerReseed(char* part, void*, uint32_t seed)
{
    OPENSC4_FIRST("MixerReseed");
    char* const self = part - kInterface;
    for (char* node = At<char*>(At<char*>(self, kScores), 8); node != At<char*>(self, kScores); node = Next(node)) {
        Virtual<uint32_t>(At<void*>(node, 0x14), 0x6C, seed);
        CallThis<void>(addr::cRZRandom_Seed, self + kRandom, seed);
    }
    return Answer(MixerRebuild(self, nullptr), true);
}

// The game's 0x00949AF7 (slot 28, `ret 4`): the byte +0x60; AL 1 over the
// caller's EAX.
uint32_t __fastcall MixerSetPlace(char* part, void*, uint32_t value)
{
    OPENSC4_FIRST("MixerSetPlace");
    At<uint8_t>(part - kInterface, kPlace) = static_cast<uint8_t>(value);
    return 1;
}

// The game's 0x00949B03 (slot 29, `ret 8`): +0xD8 and +0xDC; AL 1 over the
// second.
uint32_t __fastcall MixerSetDistances(char* part, void*, uint32_t near, uint32_t far)
{
    OPENSC4_FIRST("MixerSetDistances");
    char* const self = part - kInterface;
    At<uint32_t>(self, kDistances) = near;
    At<uint32_t>(self, kDistances + 4) = far;
    return Answer(far, true);
}

// The game's 0x00948F38 (slot 30, `ret 4`): the grids' last row, below 3;
// AL whether, over the value.
uint32_t __fastcall MixerSetGridRows(char* part, void*, uint32_t rows)
{
    OPENSC4_FIRST("MixerSetGridRows");
    if (rows >= 3)
        return Answer(rows, false);
    At<uint32_t>(part - kInterface, kGridRows) = rows;
    return Answer(rows, true);
}

// The game's 0x00948F4D (slot 31, `ret 4`): the grids' last column.
uint32_t __fastcall MixerSetGridColumns(char* part, void*, uint32_t columns)
{
    OPENSC4_FIRST("MixerSetGridColumns");
    if (columns >= 3)
        return Answer(columns, false);
    At<uint32_t>(part - kInterface, kGridColumns) = columns;
    return Answer(columns, true);
}

// The game's 0x00948ED1 (slot 32, `ret 0xC`): a cell of the byte grid. AL 0
// over the row outside 3 by 3; else AL 1 over the row + 0x1F.
uint32_t __fastcall MixerSetGrid(char* part, void*, uint32_t row, uint32_t column, uint32_t value)
{
    OPENSC4_FIRST("MixerSetGrid");
    if (row >= 3 || column >= 3)
        return Answer(row, false);
    At<uint8_t>(part - kInterface, kGrid + row * 3 + column) = static_cast<uint8_t>(value);
    return Answer(row + 0x1F, true);
}

// The game's 0x00948EFB (slot 33, `ret 0x14`): a cell of the three dword
// grids. AL 0 over the row outside 3 by 3; else AL 1 over the cell's index.
uint32_t __fastcall MixerSetGridCell(char* part, void*, uint32_t row, uint32_t column, uint32_t a, uint32_t b,
                                     uint32_t c)
{
    OPENSC4_FIRST("MixerSetGridCell");
    if (row >= 3 || column >= 3)
        return Answer(row, false);
    char* const self = part - kInterface;
    const uint32_t index = row * 3 + column;
    At<uint32_t>(self, kGridA + index * 4) = a;
    At<uint32_t>(self, kGridB + index * 4) = b;
    At<uint32_t>(self, kGridC + index * 4) = c;
    return Answer(index, true);
}

// The game's 0x00949B4F (slot 34, `ret 4`): the key copied to +0x180; EAX
// its address.
uint32_t __fastcall MixerSetKey(char* part, void*, const uint32_t* key)
{
    OPENSC4_FIRST("MixerSetKey");
    char* const self = part - kInterface;
    At<uint32_t>(self, kKey) = key[0];
    At<uint32_t>(self, kKey + 4) = key[1];
    At<uint32_t>(self, kKey + 8) = key[2];
    return Bits(self + kKey);
}

// The game's 0x00949B6C (slot 35): the key's address.
uint32_t __fastcall MixerKey(char* part, void*)
{
    OPENSC4_FIRST("MixerKey");
    return Bits(part - kInterface + kKey);
}

// ====================================================================================
// Thunks and installation

OPENSC4_THUNK(MixerConstructThunk, MixerConstruct, 0)
OPENSC4_THUNK(MixerQueryInterfaceThunk, MixerQueryInterface, 8)
OPENSC4_THUNK(MixerReleaseThunk, MixerRelease, 0)
OPENSC4_THUNK(MixerDestructThunk, MixerDestruct, 0)
OPENSC4_THUNK(MixerDeletingDestructorThunk, MixerDeletingDestructor, 4)
OPENSC4_THUNK(MixerRebuildThunk, MixerRebuild, 0)
OPENSC4_THUNK(MixerEnableThunk, MixerEnable, 8)
OPENSC4_THUNK(MixerSet144Thunk, MixerSet144, 4)
OPENSC4_THUNK(MixerGet144Thunk, MixerGet144, 0)
OPENSC4_THUNK(MixerSet16CThunk, MixerSet16C, 4)
OPENSC4_THUNK(MixerPlaceInstanceThunk, MixerPlaceInstance, 4)
OPENSC4_THUNK(MixerTickThunk, MixerTick, 4)
OPENSC4_THUNK(MixerPrepareThunk, MixerPrepare, 0)
OPENSC4_THUNK(MixerAddThunk, MixerAdd, 8)
OPENSC4_THUNK(MixerTableThunk, MixerTable, 4)
OPENSC4_THUNK(MixerPlayThunk, MixerPlay, 0)
OPENSC4_THUNK(MixerIsPlayingThunk, MixerIsPlaying, 0)
OPENSC4_THUNK(MixerStopThunk, MixerStop, 0)
OPENSC4_THUNK(MixerSetLevelThunk, MixerSetLevel, 4)
OPENSC4_THUNK(MixerSet17CThunk, MixerSet17C, 12)
OPENSC4_THUNK(MixerSetRowThunk, MixerSetRow, 4)
OPENSC4_THUNK(MixerSetColumnThunk, MixerSetColumn, 4)
OPENSC4_THUNK(MixerSet14CThunk, MixerSet14C, 4)
OPENSC4_THUNK(MixerSet164Thunk, MixerSet164, 4)
OPENSC4_THUNK(MixerGet14CThunk, MixerGet14C, 0)
OPENSC4_THUNK(MixerSet135Thunk, MixerSet135, 4)
OPENSC4_THUNK(MixerNoInstanceThunk, MixerNoInstance, 12)
OPENSC4_THUNK(MixerReseedThunk, MixerReseed, 4)
OPENSC4_THUNK(MixerSetPlaceThunk, MixerSetPlace, 4)
OPENSC4_THUNK(MixerSetDistancesThunk, MixerSetDistances, 8)
OPENSC4_THUNK(MixerSetGridRowsThunk, MixerSetGridRows, 4)
OPENSC4_THUNK(MixerSetGridColumnsThunk, MixerSetGridColumns, 4)
OPENSC4_THUNK(MixerSetGridThunk, MixerSetGrid, 12)
OPENSC4_THUNK(MixerSetGridCellThunk, MixerSetGridCell, 20)
OPENSC4_THUNK(MixerSetKeyThunk, MixerSetKey, 4)
OPENSC4_THUNK(MixerKeyThunk, MixerKey, 0)

struct Detour {
    std::uintptr_t address;
    void (*ours)();
};

template <std::size_t N>
bool InstallAll(const Detour (&detours)[N])
{
    if (!game::IsBuild641())
        return false;
    for (const Detour& detour : detours) {
        const addr::Entry* entry = addr_check::Find(detour.address);
        void* original = nullptr;
        if (!entry || !hook::Install(*entry, reinterpret_cast<void*>(detour.ours), &original))
            return false;
    }
    return true;
}

bool InstallObject(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZSoundMixer_Construct, &MixerConstructThunk},
        {addr::cGZSoundMixer_QueryInterface, &MixerQueryInterfaceThunk},
        {addr::cGZSoundMixer_Release, &MixerReleaseThunk},
        {addr::cGZSoundMixer_Destruct, &MixerDestructThunk},
        {addr::cGZSoundMixer_DeletingDestructor, &MixerDeletingDestructorThunk},
    };
    return InstallAll(detours);
}

bool InstallPlay(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZSoundMixer_Prepare, &MixerPrepareThunk},
        {addr::cGZSoundMixer_Add, &MixerAddThunk},
        {addr::cGZSoundMixer_Table, &MixerTableThunk},
        {addr::cGZSoundMixer_Play, &MixerPlayThunk},
        {addr::cGZSoundMixer_IsPlaying, &MixerIsPlayingThunk},
        {addr::cGZSoundMixer_Stop, &MixerStopThunk},
        {addr::cGZSoundMixer_Enable, &MixerEnableThunk},
        {addr::cGZSoundMixer_Rebuild, &MixerRebuildThunk},
        {addr::cGZSoundMixer_Tick, &MixerTickThunk},
        {addr::cGZSoundMixer_PlaceInstance, &MixerPlaceInstanceThunk},
    };
    return InstallAll(detours);
}

bool InstallControl(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZSoundMixer_SetLevel, &MixerSetLevelThunk},
        {addr::cGZSoundMixer_Set17C, &MixerSet17CThunk},
        {addr::cGZSoundMixer_SetRow, &MixerSetRowThunk},
        {addr::cGZSoundMixer_SetColumn, &MixerSetColumnThunk},
        {addr::cGZSoundMixer_Set14C, &MixerSet14CThunk},
        {addr::cGZSoundMixer_Set164, &MixerSet164Thunk},
        {addr::cGZSoundMixer_Get14C, &MixerGet14CThunk},
        {addr::cGZSoundMixer_Set135, &MixerSet135Thunk},
        {addr::cGZSoundMixer_NoInstance, &MixerNoInstanceThunk},
        {addr::cGZSoundMixer_Reseed, &MixerReseedThunk},
        {addr::cGZSoundMixer_Set144, &MixerSet144Thunk},
        {addr::cGZSoundMixer_Get144, &MixerGet144Thunk},
        {addr::cGZSoundMixer_Set16C, &MixerSet16CThunk},
    };
    return InstallAll(detours);
}

bool InstallGrid(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZSoundMixer_SetPlace, &MixerSetPlaceThunk},
        {addr::cGZSoundMixer_SetDistances, &MixerSetDistancesThunk},
        {addr::cGZSoundMixer_SetGridRows, &MixerSetGridRowsThunk},
        {addr::cGZSoundMixer_SetGridColumns, &MixerSetGridColumnsThunk},
        {addr::cGZSoundMixer_SetGrid, &MixerSetGridThunk},
        {addr::cGZSoundMixer_SetGridCell, &MixerSetGridCellThunk},
        {addr::cGZSoundMixer_SetKey, &MixerSetKeyThunk},
        {addr::cGZSoundMixer_Key, &MixerKeyThunk},
    };
    return InstallAll(detours);
}

const bool g_registered[] = {
    registry::Register({"framework.soundmix.object", "framework/sound", registry::Kind::Detour,
                        registry::Phase::Attach, addr::cGZSoundMixer_Construct,
                        "Our framework sound mixer 0xBDDA72DD: constructor, QueryInterface, Release, destructors (5 "
                        "detours; part u8, docs/gzui.md)",
                        false, InstallObject}),
    registry::Register({"framework.soundmix.play", "framework/sound", registry::Kind::Detour, registry::Phase::Attach,
                        addr::cGZSoundMixer_Tick,
                        "Our framework sound mixer's Prepare, scores added, turned on and off, their tables, Play, "
                        "IsPlaying, Stop, the pool, the tick and the neighbour pick (10 detours; part u8, "
                        "docs/gzui.md)",
                        false, InstallPlay}),
    registry::Register({"framework.soundmix.control", "framework/sound", registry::Kind::Detour,
                        registry::Phase::Attach, addr::cGZSoundMixer_SetLevel,
                        "Our framework sound mixer's level, row, column and other settings, Reseed and the primary "
                        "vtable's slots 6 to 8 (13 detours; part u8, docs/gzui.md)",
                        false, InstallControl}),
    registry::Register({"framework.soundmix.grid", "framework/sound", registry::Kind::Detour, registry::Phase::Attach,
                        addr::cGZSoundMixer_SetGrid,
                        "Our framework sound mixer's slots 28 to 35: the neighbour grids, their size, the distances, "
                        "the key (8 detours; part u8, docs/gzui.md)",
                        false, InstallGrid}),
};

} // namespace
} // namespace opensc4::gzui
