// The layout of the automata controllers, part a3 (build 641; class IDs and
// vtables in data/addresses/automata.toml, the survey in docs/automata.md,
// "Part a3"): the base cSC4AutomataController and the two derived classes
// cSC4AutomataAttractor (0x6A1628BA, factory 0x00514860, constructor
// 0x005181F0, 0x6C bytes) and cSC4AutomataGenerator (0x4A1628C5, factory
// 0x00514880, constructor 0x005182B0, 0xA0 bytes), as
// src/core/automata_controller.cpp reads them.
//
// Part a3a (this file): the object (the constructors 0x00517D40, 0x005181F0
// and 0x005182B0, the destructor bodies 0x00517F80 and 0x005192E0, the
// deleting destructors 0x005192C0 and 0x00519340, the two factories,
// QueryInterface 0x00514F60 and Release 0x00517F10), the queries (the
// accessors, the flag tests and the small overrides the two classes make),
// and the +4 serializable part's identity and Write (0x00517F30, 0x00517F00,
// 0x00517FD0, 0x00518230, 0x005183D0). The serializable functions take
// object + 4 as this, so their fields are the object's less 4. Read is a
// later part.
//
// The base constructor fixes the layout, shared by both derived classes:
//   +0x00 the controller vtable, 0x00A95D90 (42 slots)
//   +0x04 the serializable interface, 0x00A95D74
//   +0x08 initialized byte
//   +0x0C occupant (referenced)
//   +0x10 controller template (referenced)
//   +0x14/+0x18/+0x1C position x, z, y (floats)
//   +0x20 reference count
//   +0x24..+0x27 four bytes
//   +0x28 a cRZBaseString (its vtable 0x00A80810), +0x2C/+0x30/+0x34 the
//         byte vector (8 bytes from SC4::stl_allocate) and +0x38 its
//         reference count -- the controller's own name; GetTemplate answers
//         this object when there is no template
//   +0x3C a word, +0x40 the flags (2 from the constructor)
//   +0x44/+0x48 the list of automata (head and count)
//   +0x4C the automata script system (referenced)
//   +0x50/+0x54/+0x58 floats (the saturation limit, an accumulator and a
//         third)
//   +0x5C attractor only: its SC4Curve, initialized by 0x007D0410
//   +0x60 generator only: its cRZRandom, seeded by 0x00913A5C
//   +0x78..+0x98 generator fields
#pragma once

#include <cstddef>
#include <cstdint>

namespace opensc4::automata_controller {

namespace field {
inline constexpr std::size_t kVtable = 0x00, kSerializable = 0x04;
inline constexpr std::size_t kInitialized = 0x08, kOccupant = 0x0C, kTemplate = 0x10;
inline constexpr std::size_t kX = 0x14, kZ = 0x18, kY = 0x1C;
inline constexpr std::size_t kReferences = 0x20;
inline constexpr std::size_t kByte24 = 0x24, kByte25 = 0x25, kByte26 = 0x26, kByte27 = 0x27;
inline constexpr std::size_t kName = 0x28;
inline constexpr std::size_t kNameBegin = 0x2C, kNameEnd = 0x30, kNameCapacity = 0x34, kNameReferences = 0x38;
inline constexpr std::size_t k3C = 0x3C, kFlags = 0x40;
inline constexpr std::size_t kAutomata = 0x44, kAutomataCount = 0x48;
inline constexpr std::size_t kScripts = 0x4C;
inline constexpr std::size_t k50 = 0x50, k54 = 0x54, k58 = 0x58;
inline constexpr std::size_t kCurve = 0x5C;  // attractor
inline constexpr std::size_t kRandom = 0x60; // generator
inline constexpr std::size_t kGen78 = 0x78, kGen7C = 0x7C, kGen80 = 0x80, kGen84 = 0x84;
inline constexpr std::size_t kGen88 = 0x88, kGen8C = 0x8C, kGen90 = 0x90, kGen94 = 0x94, kGen98 = 0x98;
} // namespace field

// A controller template (tControllerTemplate) as the two SetTemplate
// functions read it.
namespace tmpl {
inline constexpr std::size_t kClassId = 0x18, kSaturationLimit = 0x4C, kFlags = 0x58;
inline constexpr std::size_t kRandomLow = 0x90, kRandomHigh = 0x94, kLimit = 0xAC;
} // namespace tmpl

// Slots the object and its queries use, in bytes.
namespace slot {
inline constexpr std::size_t kAddRef = 0x04, kRelease = 0x08, kShutdown = 0x10;
inline constexpr std::size_t kGetPosition = 0x20, kTestFlag = 0x30, kDeletingDestructor = 0x9C;
} // namespace slot

inline constexpr std::uint32_t kFlag1 = 1, kFlag2 = 2, kFlag4 = 4, kFlag20 = 0x20, kFlag400 = 0x400;
inline constexpr std::uint32_t kConstructorFlags = 2;
inline constexpr std::uint32_t kAttractorClassId = 0x6A1628BA;
inline constexpr std::uint32_t kGeneratorClassId = 0x4A1628C5;
inline constexpr std::uint32_t kControllerSaveClassId = 0x4A18672B;
inline constexpr std::uint32_t kIidAutomataController = 0x6A1857F8;
inline constexpr std::uint32_t kIidSerializable = 0xE4FDA3D4;
inline constexpr std::uint32_t kIidUnknown = 1;

} // namespace opensc4::automata_controller
