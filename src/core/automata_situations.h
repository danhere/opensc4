// The layout of the city situation manager, parts c1 and c2 (build 641; class ID,
// vtables and functions in data/addresses/automata.toml, the survey in
// docs/automata.md, "Part c"): cSC4CitySituationManager (0x0BB14381,
// factory 0x005148C0, constructor 0x00529FD0, 0xB00 bytes, city slot 49,
// GetCitySituationManager at the city vtable's byte offset 0xC4). It runs the
// Rush Hour missions (the city situation advices of the advisors' scripts)
// and the U-Drive's joyrides.
//
// Five parts, each with its own vtable:
//   +0x00 0x00A97E58 cISC4CitySituationManager, 40 slots: the gzcom-dll
//         header's 34 (slots 19 and 20, the two IsSituationAvailable, sit in
//         the other order), then 34 the deleting destructor 0x00527AA0 and
//         35 to 39 (0x005252D0, 0x0052E750, 0x005276A0, 0x005294C0,
//         0x00529ED0; later parts)
//   +0x04 0x00A97E44 the message target (QueryInterface, AddRef and Release
//         adjustors, DoMessage 0x0052EB80)
//   +0x08 0x00A97E34 the adjustors and the shared 0x005DEB40
//   +0x0C 0x00A97E1C the serializable part (Write 0x00527040, Read
//         0x00529010, GetGZCLSID 0x005274E0)
//   +0x10 0x00A97E08 the adjustors, 0x0052CAB0 and the shared 0x0041D4C0
//
// Fields (constructor 0x00529FD0):
//   +0x14 initialized, +0x18 the reference count
//   +0x1C a cRZDate (8 bytes), +0x24 the script context (referenced)
//   +0x28 an SC4Random (seeded with -1) up to +0x9F0
//   +0x9F0 the CSI's visibility (1), +0x9F1 set by Read
//   +0x9F4 the active situation (a city situation advice, referenced)
//   +0x9F8 a vector of four referenced services (Init)
//   +0xA04 a list (0x14-byte nodes: +8 and +0xC referenced, +0x10 an
//          overlay ID)
//   +0xA08 a hash set of referenced automata (the tracked automata; count
//          +0xA18)
//   +0xA1C, +0xA20 the lot manager, +0xA24 the city's slot 71, +0xA28
//          (referenced), +0xA2C the overlay manager (the dispatch service's
//          slot 22), +0xA30
//   +0xA34 hash map ID -> uint16, the mission successes (buckets +0xA38..,
//          count +0xA44; saved)
//   +0xA48 the status (4), +0xA4C the active automaton, +0xA50 the current
//          target, +0xA54, +0xA58 (referenced), +0xA5C the target count,
//          +0xA60 the current target's index, +0xA64 a byte
//   +0xA68 a hash set of referenced objects
//   +0xA7C..+0xA88, +0xA8C the situation's time, +0xA90 the countdown,
//          +0xA94 the time limit (floats from +0xA8C)
//   +0xA98 the joyride flag, +0xA99, +0xA9C, +0xAA0, +0xAA4 (referenced)
//   +0xAA8 a hash set, +0xABC, +0xAC0
//   +0xAC4 hash map key -> referenced object
//   +0xADC..+0xAF4 seven words, -2; +0xAF8 (referenced), +0xAFC
//
// The hash maps and sets: +4 the bucket vector's begin, +8 its end, +0xC its
// capacity, +0x10 the count; nodes +0 next, +4 the key, +8 the value; the
// bucket is the key modulo the bucket count.
#pragma once

#include <cstddef>
#include <cstdint>

namespace opensc4::automata_situations {

inline constexpr std::uint32_t kClassId = 0x0BB14381u;
inline constexpr std::uint32_t kManagerIid = 0x6BB1437Au;
inline constexpr std::size_t kObjectSize = 0xB00;

namespace field {
inline constexpr std::size_t kVtable = 0x00, kVtable4 = 0x04, kVtable8 = 0x08, kSerializable = 0x0C;
inline constexpr std::size_t kVtable10 = 0x10;
inline constexpr std::size_t kInitialized = 0x14, kRefCount = 0x18, kDate = 0x1C, kScriptContext = 0x24;
inline constexpr std::size_t kRandom = 0x28;
inline constexpr std::size_t kCsiVisible = 0x9F0, kRead = 0x9F1, kSituation = 0x9F4, kServices = 0x9F8;
inline constexpr std::size_t kList = 0xA04, kTracked = 0xA08, kRefA1C = 0xA1C, kLotManager = 0xA20;
inline constexpr std::size_t kCity71 = 0xA24, kRefA28 = 0xA28, kOverlay = 0xA2C, kZeroA30 = 0xA30;
inline constexpr std::size_t kSuccesses = 0xA34, kStatus = 0xA48, kAuto = 0xA4C, kTarget = 0xA50;
inline constexpr std::size_t kRefA54 = 0xA54, kRefA58 = 0xA58, kNumTargets = 0xA5C, kTargetIndex = 0xA60;
inline constexpr std::size_t kByteA64 = 0xA64, kRefSet = 0xA68, kZeroA7C = 0xA7C, kSituationTime = 0xA8C;
inline constexpr std::size_t kCountdown = 0xA90, kTimeLimit = 0xA94, kJoyride = 0xA98, kByteA99 = 0xA99;
inline constexpr std::size_t kRefA9C = 0xA9C, kRefAA0 = 0xAA0, kRefAA4 = 0xAA4, kSetAA8 = 0xAA8;
inline constexpr std::size_t kZeroABC = 0xABC, kZeroAC0 = 0xAC0, kObjects = 0xAC4, kWordsADC = 0xADC;
inline constexpr std::size_t kRefAF8 = 0xAF8, kZeroAFC = 0xAFC;
} // namespace field

// A hash map's parts, from its field.
namespace map {
inline constexpr std::size_t kBegin = 0x04, kEnd = 0x08, kCapacity = 0x0C, kCount = 0x10;
} // namespace map

namespace slot {
inline constexpr std::size_t kQueryInterface = 0x00, kAddRef = 0x04, kRelease = 0x08, kInit = 0x0C;
inline constexpr std::size_t kShutdown = 0x10, kGetMissionSuccessCount = 0x14, kGetActiveSituation = 0x18;
inline constexpr std::size_t kGetCurrentStatus = 0x1C, kGetActiveAuto = 0x20, kGetCurrentTarget = 0x24;
inline constexpr std::size_t kGetCurrentTargetIndex = 0x28, kGetNumTargets = 0x2C, kGetNumTargetsRemaining = 0x30;
inline constexpr std::size_t kGetCurrentTimeLimitSeconds = 0x34, kGetCurrentSituationTime = 0x38;
inline constexpr std::size_t kGetCurrentTimeRemainingSeconds = 0x3C, kGetCurrentTargetName = 0x40;
inline constexpr std::size_t kGetSituationAura = 0x44, kGetSituationMoney = 0x48;
inline constexpr std::size_t kSetMaxCsi = 0x68, kGetMaxCsi = 0x6C, kGetTrackedAutomataCount = 0x70;
inline constexpr std::size_t kSetCsiVisible = 0x74, kGetCsiVisible = 0x78, kHandleTuningParametersChanged = 0x7C;
inline constexpr std::size_t kCountdownIsInProgress = 0x80, kResetTutorials = 0x84, kDeletingDestructor = 0x88;
inline constexpr std::size_t kEndSituation = 0x98;
// Part c2 (src/core/automata_situations_2.cpp): slots 19 to 25, 35 to 39.
inline constexpr std::size_t kIsSituationAvailableById = 0x4C, kIsSituationAvailable = 0x50;
inline constexpr std::size_t kInitiateSituationByGuid = 0x54, kInitiateSituationForOccupant = 0x58;
inline constexpr std::size_t kInitiateSituationByAutomataGroup = 0x5C, kStartJoyride = 0x60;
inline constexpr std::size_t kTreatAutomatonAsGroup = 0x64, kSetUDrive = 0x8C, kStartSituation = 0x90;
inline constexpr std::size_t kBeginDrive = 0x94, kStopSituation = 0x9C;
} // namespace slot

// Part c2's fields (the rest of the layout above).
namespace field {
inline constexpr std::size_t kDriveTime = 0xA88, kSetAA8Count = 0xAB8, kRefAF0 = 0xAF0, kTableRef = 0xAF4;
// Part c3 (src/core/automata_situations_3.cpp): the target check's clock, the
// tracked automata's buckets and count, the success and failure scripts'
// table references (two of the seven words from +0xADC).
inline constexpr std::size_t kCheckTime = 0xA84, kTrackedBuckets = 0xA0C, kTrackedCount = 0xA18;
inline constexpr std::size_t kSuccessRef = 0xADC, kFailureRef = 0xAE0, kTargetRef = 0xAE4;
} // namespace field

} // namespace opensc4::automata_situations
