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

// The tool's fields as far as the first four pieces (0x004C3730 to
// 0x004CC6E0) show them. +0x30 to +0x3F are not set by the constructor;
// +0x79 to +0x7B, +0x8A, +0x8B and +0x95 to +0x97 are padding. Init
// (0x004C57A0) reads +0x4, +0x1C and +0x8C and sets +0xC, +0x10, +0x44 to
// +0x4C (the two windows only for kind 1), +0x78 and +0x94; it gives the
// timer at +0x50 its callback and registers the target part at +0x28.
// OnMouseMove (0x004CBC70) keeps the occupant under the pointer at +0x2C
// (highlighted 7, its own highlight saved at +0x40), the last ground point
// at +0x7C and whether something was picked at +0x88; it starts the timer
// with +0x90 milliseconds. Shutdown (0x004CBA80) lets everything go.
namespace field {
constexpr std::size_t kTarget = 0x28;    // the message target part (vtable 0x00A90A78)
constexpr std::size_t kOccupant = 0x2C;  // the occupant asked about, referenced (0x004C3810 reads its holder)
constexpr std::size_t kBox = 0x30;       // four cells (min x, min z, max x, max z) the routes are drawn for
constexpr std::size_t kField40 = 0x40;   // the occupant's own highlight (its slot 16), given back when it is let go
constexpr std::size_t kTip = 0x44;       // the tool's info tip (0x1B4 bytes, made by Init; referenced)
constexpr std::size_t kPanel = 0x48;     // kind 1: the layout's child 0x0BB0F5E7, moved to the view (referenced)
constexpr std::size_t kPanelOther = 0x4C; // kind 1: the layout's child 0x6BB92BCA, moved to the view (referenced)
constexpr std::size_t kTimer = 0x50;     // a cRZTimer of 0x28 bytes (vtable 0x00AD1F14); Init gives it 0x004C4E70
constexpr std::size_t kFlag78 = 0x78;    // byte, 1 from the constructor and from Init
constexpr std::size_t kPlace = 0x7C;     // three floats: the ground point under the pointer (at first 0x00B21858's)
constexpr std::size_t kFlag88 = 0x88;    // byte: OnMouseMove found something under the pointer
constexpr std::size_t kPressed = 0x89;   // byte, 0; cleared by OnMouseUpL
constexpr std::size_t kKind = 0x8C;      // the constructor's first argument: 0 or 1
constexpr std::size_t kSecond = 0x90;    // the constructor's second argument, 0x32 from the view: the timer's ms
constexpr std::size_t kFlag94 = 0x94;    // byte, 0; Init stores the view's slot 39: ground point also for a pick
constexpr std::size_t kRouteMode = 0x98; // 3; 1 = all route cells but kind 6, 2 = only kind 6 (0x004C4AE0)
constexpr std::size_t kObject9C = 0x9C;  // the route line (referenced; route_line below) 0x004C5E20 adds points to
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

// The line the tool's routes are drawn with (0x2C bytes, vtable 0x00ABB648,
// constructor 0x007DDD50; the Renderer's code). 0x004CA460 makes it when
// there is none, sets it up (0x007DCD90) and gives it to the render service
// (0x00B43DD0, slot 32: layer 5, order 1000); the service's slot 33 takes
// it off again.
namespace route_line {
constexpr std::size_t kPrepared = 0x0C; // byte: set up (0x007DCD90 sets it, 0x0053C940 clears it)
constexpr std::size_t kWidth = 0x10;    // float, 2 from the constructor; set by zoom (0x008B6DE0)
constexpr std::size_t kPoints = 0x14;   // the points (0x007DDC30 empties them)
constexpr std::size_t kSize = 0x2C;
} // namespace route_line

// A trip as a traffic source keeps it (the source's slot 3 gives the list):
// a list node (next, previous) with the trip record from +0x8. The
// record's byte at +0x1E (the node's +0x26) has flags: the routes drawn are
// those with 8 set and 2 clear, the way back (0x004C4AE0) when 1 is clear.
namespace trip_node {
constexpr std::size_t kNext = 0x00;
constexpr std::size_t kRecord = 0x08;
constexpr std::size_t kFlags = 0x26;
} // namespace trip_node

// The node of the query's text map (a red-black tree of key and game
// string; 0x004CB8B0 frees a subtree): the left child at +0x8, the right at
// +0xC, the key at +0x10 and the string (vtable, buffer, ...) from +0x14.
namespace text_node {
constexpr std::size_t kLeft = 0x08;
constexpr std::size_t kRight = 0x0C;
constexpr std::size_t kText = 0x14;
constexpr std::size_t kTextBuffer = 0x18;
} // namespace text_node

// What the fillers by cell (0x004CC240) are given: a cell, a number added
// to the control's ID for the six-argument ones, and their sixth argument
// (a neighbour connection's occupant type). The walks 0x004CC660 and
// 0x004CC6A0 pass it on to each child.
namespace cell_query {
constexpr std::size_t kX = 0x00;
constexpr std::size_t kZ = 0x04;
constexpr std::size_t kIdOffset = 0x08;
constexpr std::size_t kType = 0x0C;
constexpr std::size_t kSize = 0x10;
} // namespace cell_query

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

// A pair the query's texts are kept in (key, string; made by 0x004CCF20,
// put into a map by 0x004D4F90, destroyed by 0x004C6AF0).
namespace text_entry {
constexpr std::size_t kKey = 0x00;
constexpr std::size_t kText = 0x04; // an SC4String (vtable 0x00A80810, body +0x8 to +0x13, a zero dword)
constexpr std::size_t kSize = 0x18;
} // namespace text_entry

// What the query's windows by cell (0x004CC6E0, 0x004CCCB0) keep for
// DoMessage (0x004D75B8) while their window runs: the walk with fill
// (0x004CC660) at 0x00B21848, the window at 0x00B2184C and up to two cell
// queries at 0x00B21840 and 0x00B21844; all cleared when the window ends.
namespace window_cell {
constexpr std::size_t kQueries = 2;
} // namespace window_cell

// A tract developer's record of one development attempt as the
// development log (0x004CE440) reads it (the tract developer's map at
// +0x17C, 0x00705FA0). +0x0 is an object whose slot 2 (string, 0) writes
// the day it was made.
namespace development_record {
constexpr std::size_t kDeveloper = 0x08;    // the growth developer (its slot 6: the developer type)
constexpr std::size_t kTractX = 0x0C;
constexpr std::size_t kTractZ = 0x10;
constexpr std::size_t kPopulation = 0x14;
constexpr std::size_t kCapacity = 0x1C;
constexpr std::size_t kMaxCapacity = 0x20;
constexpr std::size_t kDesirability = 0x24;
constexpr std::size_t kResources = 0x2C;   // construction resources
constexpr std::size_t kAttempts = 0x30;    // a list (its sentinel) of build attempts (development_attempt)
constexpr std::size_t kFailures = 0x34;    // 13 vectors (begin, end, end of buffer) of 28 counts, by outcome
constexpr std::size_t kFailureRows = 13;
} // namespace development_record

// A build attempt: a list node of the record's +0x30 list.
namespace development_attempt {
constexpr std::size_t kNotFussy = 0x08;   // byte
constexpr std::size_t kLotSizes = 0x0C;   // a red-black tree's header; each node's +0x10 object gives a size (slot 7)
constexpr std::size_t kCandidates = 0x18; // a vector of candidate pointers (begin; end at +0x1C)
constexpr std::size_t kCandidatesEnd = 0x1C;
constexpr std::size_t kLotConfig = 0x28;  // the lot configuration chosen (0: none)
constexpr std::size_t kWantMin = 0x30;
constexpr std::size_t kWantMax = 0x34;
constexpr std::size_t kOutcome = 0x38;    // 0 to 27 (0x004CDF80's names)
} // namespace development_attempt

// A candidate of a build attempt.
namespace development_candidate {
constexpr std::size_t kKind = 0x00;      // 1 refurbish, 2 build new, 3 aggregate, 4 subdivide
constexpr std::size_t kX = 0x08;
constexpr std::size_t kZ = 0x0C;
constexpr std::size_t kSizeX = 0x14;     // byte
constexpr std::size_t kSizeZ = 0x15;     // byte
constexpr std::size_t kCapacity = 0x24;
constexpr std::size_t kAbandoned = 0x28;
constexpr std::size_t kScore = 0x34;     // float
} // namespace development_candidate

constexpr uint32_t kOutcomeCount = 28;

// What Init loads for kind 1: the layout {0, 0x96A006B0, 0xABB0120F},
// window 0x6BB92BCB (interface 0x22BA0121), and its two children that
// become the tool's windows.
constexpr uint32_t kLayoutGroup = 0x96A006B0;
constexpr uint32_t kLayoutInstance = 0xABB0120F;
constexpr uint32_t kLayoutWindow = 0x6BB92BCB;
constexpr uint32_t kPanelId = 0x0BB0F5E7;
constexpr uint32_t kPanelOtherId = 0x6BB92BCA;

constexpr uint32_t kToolIdFirst = 0xC7AF928E;  // kind 0
constexpr uint32_t kToolIdSecond = 0xC7AF928F; // kind 1

// The table at 0x00A92488: 0x7F functions (or null), one for each control of
// the query's windows whose ID has the bit 0x1000; the index is the ID's low
// 12 bits. Each is cdecl (window, control ID, fill, what is asked about):
// with `fill` it puts the control's value into the window, without it it is
// to read the control back (0x004C55A0, 0x004C70C0, 0x004C7200,
// 0x004C7410, 0x004C7A70, 0x004C7BB0, 0x004C7F00, 0x004C8030 and
// 0x004C8760 do: they change the city; most others answer true). The
// readers are 0x004D3A90, 0x004D3AE0 and 0x004D3BD0. Some entries share a
// function (Q3: 0x004C7CE0 is 27, 29, 31 and 36) and two are jumps to
// another entry's function (16 to 0x004C9310, 46 to 0x004C8660).
constexpr uint32_t kFillerCount = 0x7F;

// The record the residential simulator fills for a school (its slot 26) or
// a hospital (slot 27): 0x40 bytes, a game string first. The fillers set
// the string, +0x28, +0x2C and +0x34 before the call; the rest comes from
// the simulator. The two shares and their costs go back through the
// simulator's slot 19 (the occupant, the first share, the second share, 1
// for a school or 0 for a hospital).
namespace school_record {
constexpr std::size_t kText = 0x00;        // a game string (0x14 bytes)
constexpr std::size_t kNumber = 0x24;      // a whole number (fillers 28, 30 and 37)
constexpr std::size_t kValue = 0x28;       // a float shown as a control's value (fillers 23 and 32)
constexpr std::size_t kShare = 0x2C;       // float, the first funding share
constexpr std::size_t kCost = 0x30;        // uint32, the first share's cost
constexpr std::size_t kShareSecond = 0x34; // float, the second funding share
constexpr std::size_t kCostSecond = 0x38;  // uint32, the second share's cost
constexpr std::size_t kSize = 0x40;
} // namespace school_record

// What the traffic simulator's slot 35 (GetTransitSwitchQueryData: cell x,
// cell z, this record) fills for a transit switch: five dwords. The query
// sets only +0x8 before the call.
namespace transit_record {
constexpr std::size_t kSecond = 0x04;  // fillers 66 (the divisor) and 69
constexpr std::size_t kThird = 0x08;   // 0 before the call; not read
constexpr std::size_t kFourth = 0x0C;  // fillers 66 (the dividend) and 68
constexpr std::size_t kCost = 0x10;    // filler 67, as money (taken unsigned)
constexpr std::size_t kSize = 0x14;
} // namespace transit_record

// A growth developer as the building development service (0x00B43D68)
// gives it for a type (its slot 32) and as 0x004D32D0 gives it for a lot:
// what the desirability log (0x004C9520) and the map levels read. Each
// curve is evaluated (0x005F4250) at what a map holds at the cell; a null
// curve is an effect that is off.
namespace developer {
constexpr std::size_t kLandValueGrid = 0x47; // byte: the land value simulator's slot 7 map when set, slot 8 when not
constexpr std::size_t kLandValueCurve = 0x48;
constexpr std::size_t kSlopeCurve = 0x4C;     // over the land value simulator's slot 9 map (floats)
constexpr std::size_t kPollutionCurves = 0x50; // a pointer to four curves, by pollution type
constexpr std::size_t kParkCurve = 0x5C;       // over the aura simulator's slot 14 map (16 bits, signed)
constexpr std::size_t kProximityCurves = 0x60; // three: the residential simulator's slot 5 maps for wealth 1 to 3
constexpr std::size_t kCommercialCurves = 0x6C; // two: the commercial simulator's slot 5 maps 0x3320 and 0x3330
constexpr std::size_t kSchoolCurve = 0x74;      // the residential simulator's slot 39 map (floats)
constexpr std::size_t kHospitalCurve = 0x78;    // its slot 40 map (floats)
constexpr std::size_t kCrimeCurve = 0x7C;       // the crime simulator's slot 5 map
constexpr std::size_t kTripCurve = 0x80;        // the traffic simulator's slot 13 map (0 read as 0xFF)
constexpr std::size_t kTrafficCurve = 0x84;     // the traffic simulator's slot 9 map
constexpr std::size_t kLandmarkCurve = 0x88;    // the aura simulator's slot 15 map (16 bits, signed)
constexpr std::size_t kTransientCurve = 0x8C;   // the aura simulator's slot 13 map
constexpr std::size_t kBaseValue = 0x90;        // int, where the sum starts
constexpr std::size_t kDesirability = 0x114;    // tract map (slot 11: the shift from cells; slot 22: a tract's value)
constexpr std::size_t kPopulation = 0x118;      // tract map, signed
constexpr std::size_t kCapacity = 0x11C;        // tract map, unsigned
constexpr std::size_t kMaxCapacity = 0x120;     // tract map, unsigned
} // namespace developer

// The building development service's fields the desirability log reads: a
// hash of tracts where growth was locked down (key: developer, tract x,
// tract z, a byte 1 to 8; value at the node's +0x14, the day it was locked;
// 0x004C6920 finds) and how many days a lock lasts.
namespace development {
constexpr std::size_t kLocks = 0x9C;
constexpr std::size_t kLockDays = 0xBC;
} // namespace development

} // namespace opensc4::tool_query
