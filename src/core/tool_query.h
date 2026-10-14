// The query tool of the city view (cSC4ViewInputControlQuery, 0x004C3730 to
// 0x004D767F): the layouts its pieces share. The tool is a view input
// control of 0xA0 bytes (main vtable 0x00A90A88, 30 slots; message target
// 0x00A90A78 at +0x28), made by the city view's commands 0x6A935CF3 (kind 0,
// ID 0xC7AF928E) and 0x6A935CF4 (kind 1, ID 0xC7AF928F) with a second
// argument of 0x32 (docs/tools.md).
#pragma once

#include <cstddef>
#include <cstdint>

namespace opensc4::tool_query {

// The base class's fields (src/core/tool_base.cpp).
namespace base {
constexpr std::size_t kInited = 0x04; // byte
constexpr std::size_t kId = 0x0C;
constexpr std::size_t kCursor = 0x10; // cursor ID
constexpr std::size_t kView = 0x1C;   // the city view
} // namespace base

// The tool's fields as far as the first piece (0x004C3730 to 0x004C56F0)
// shows them. +0x30 to +0x3F are not set by the constructor; +0x79 to +0x7B,
// +0x8A, +0x8B and +0x95 to +0x97 are padding.
namespace field {
constexpr std::size_t kTarget = 0x28;    // the message target part (vtable 0x00A90A78)
constexpr std::size_t kOccupant = 0x2C;  // the occupant asked about (0x004C3810 reads its property holder)
constexpr std::size_t kBox = 0x30;       // 16 bytes handed to 0x004CA460; not set by the constructor
constexpr std::size_t kField40 = 0x40;   // a dword, 0; not let go by the destructor
constexpr std::size_t kTip = 0x44;       // an object (referenced) whose window OnMouseUpL hides
constexpr std::size_t kPanel = 0x48;     // a window (referenced)
constexpr std::size_t kPanelOther = 0x4C; // a window (referenced): 0x004C4630 shows one for the other
constexpr std::size_t kTimer = 0x50;     // a cRZTimer of 0x28 bytes (vtable 0x00AD1F14)
constexpr std::size_t kFlag78 = 0x78;    // byte, 1 from the constructor and from Init
constexpr std::size_t kPlace = 0x7C;     // three dwords, each the dword at 0x00B21858
constexpr std::size_t kFlag88 = 0x88;    // byte, 0
constexpr std::size_t kPressed = 0x89;   // byte, 0; cleared by OnMouseUpL
constexpr std::size_t kKind = 0x8C;      // the constructor's first argument: 0 or 1
constexpr std::size_t kSecond = 0x90;    // the constructor's second argument: 0x32 from the view
constexpr std::size_t kFlag94 = 0x94;    // byte, 0; Init stores the view's slot 39 here
constexpr std::size_t kRouteMode = 0x98; // 3; 1 = all route cells but kind 6, 2 = only kind 6 (0x004C4AE0)
constexpr std::size_t kObject9C = 0x9C;  // an object (referenced), let go by the destructor
constexpr std::size_t kSize = 0xA0;
} // namespace field

// The object the tool gives a window of its own as target (vtable
// 0x00A90980, 6 slots, 0xC bytes, made in line at 0x004C5982): slot 3 is the
// window's message handler 0x004CB900, slot 5 the deleting destructor
// 0x004C4570.
namespace panel_target {
constexpr std::size_t kCount = 0x04; // the reference count
constexpr std::size_t kTool = 0x08;  // the tool (not referenced)
constexpr std::size_t kSize = 0x0C;
} // namespace panel_target

// The object that runs a window of the query for the tool (vtable
// 0x00A90B6C, 0x18 bytes, constructor 0x004C5620, made at 0x004CCA95,
// 0x004CCD6C and in 0x004D3C30): a counted object with a window procedure
// part (+0x8, IID 0x22E85D8E, vtable 0x00A90B58, its slot 3 0x004D3F40) and
// a message target part (+0xC, IID 0x452294AA, vtable 0x00A90B48).
namespace window_runner {
constexpr std::size_t kCount = 0x04;    // the reference count
constexpr std::size_t kProcedure = 0x08;
constexpr std::size_t kTarget = 0x0C;
constexpr std::size_t kWindow = 0x10;   // the window (referenced twice by the constructor)
constexpr std::size_t kObject = 0x14;   // an object (referenced) set by slot 6, 0x004C4530
constexpr std::size_t kSize = 0x18;
} // namespace window_runner

constexpr uint32_t kToolIdFirst = 0xC7AF928E;  // kind 0
constexpr uint32_t kToolIdSecond = 0xC7AF928F; // kind 1

// The table at 0x00A92488: 0x7F functions (or null), one for each control of
// the query's windows whose ID has the bit 0x1000; the index is the ID's low
// 12 bits. Each is cdecl (window, control ID, fill, what is asked about):
// with `fill` it puts the control's value into the window, without it it is
// to read the control back (only 0x004C55A0 does). The readers are
// 0x004D3A90, 0x004D3AE0 and 0x004D3BD0.
constexpr uint32_t kFillerCount = 0x7F;

} // namespace opensc4::tool_query
