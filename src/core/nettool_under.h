// The underground network tools' layout (build 641; part u1 of
// docs/nettool.md), shared by src/core/nettool_under.cpp (the first half)
// and src/core/nettool_under_2.cpp (the second half). Both halves are one
// group, simulators.nettool.underground: nettool_under.cpp registers it and
// its install also installs the second half's list (InstallUnderground2).
#pragma once

#include <cstddef>
#include <cstdint>

namespace opensc4::nettool_sim {

namespace under {
inline constexpr std::uintptr_t kVtable = 0x00AAA950, kVtable4 = 0x00AAA93C;
inline constexpr std::uintptr_t kPipeVtable = 0x00AA99A8, kPipeVtable4 = 0x00AA9994;
inline constexpr std::uintptr_t kSubwayVtable = 0x00AAA5C0, kSubwayVtable4 = 0x00AAA5AC;
inline constexpr std::size_t kInited = 0x368, kDepth = 0x36C, kListA = 0x384, kListB = 0x388, kByte390 = 0x390;
// What ReloadNetworkSpecificInfo (slot 79) reads from the exemplar, which
// slot 82 hands to the new occupant's slot 0x184: two floats and a bool.
inline constexpr std::size_t kInfo370 = 0x370, kInfo374 = 0x374, kInfo378 = 0x378;
inline constexpr std::size_t kCellsBegin = 0x98, kCellsEnd = 0x9C, kCellSize = 0xC, kCellKey = 0x8;
inline constexpr uint32_t kPipeType = 4, kSubwayType = 7;
inline constexpr uint32_t kInfoType = 0x6534284A, kInfoGroup = 0x084344E0, kResourceIid = 0xA52160F5;
} // namespace under

// The second half's detours (nettool_under_2.cpp); called by the group's
// install in nettool_under.cpp.
bool InstallUnderground2();

} // namespace opensc4::nettool_sim
