// Shared between the parts of our cSC4NetworkLotManager (build 641): the
// class maps of the manager and of the two small classes that only its code
// makes, the network lot and the network lot configuration; their vtables
// slot by slot, the fields, and the containers the manager keeps. The
// manager is the city's desk for what stands beside a network piece: for
// every cell with a piece of a surface network it keeps one network lot,
// which ties the piece to one configuration (a list of props and flora with
// their places on the cell), chosen from the configurations loaded for the
// piece's ID by the zone and the wealth around the cell. It also keeps the
// potholes. It has no grid: every city size it needs it asks the city for
// (slots 93 and 94).
//
// Written in parts: netlot_state.cpp (the first part, state and queries:
// the identity, GetNetworkLot, the two configuration queries, the zone and
// the wealth of a cell, IterateAllNetworkLots, SetLotConfigurationForNextPiece,
// ConfigurationFits, the two maps' operator[] with the configuration map's
// node insert, the cell iterator's two helpers, and the small methods of the
// lot and of the configuration); netlot_life.cpp (the second part, life and
// save: the factory, the constructor, the destructors, Init, Shutdown,
// Write, Read and the loading of the configurations, with the
// configuration's constructor and deleting destructor); the third (the
// lots' work: DoMessage, UpdateCell, UpdateLot, CreateLot, RemoveLot,
// BuildLotObjects, ObjectPosition, CreateProp, RedevelopLot, Pothole,
// UpdateConnectedPieces, WriteLotConfiguration) is to come. UpdatePieceStyle
// 0x0060F510 stays the
// game's for good: the plugin sc4-network-texture-test patches it in the
// middle (a jump at 0x0060F580 that comes back at 0x0060F5E7).
//
// === cSC4NetworkLotManager ================================================
// Class ID 0xE9A6CD3B (registered at 0x00460443 with the factory 0x0060F4F0),
// 0x88 bytes, constructor 0x0060EE50, destructor 0x0060E480, deleting
// destructor 0x0060EF20. Code 0x0060C720 to 0x006104CE. One per city: the
// city makes it (0x0045AC5C) unless the save brought one, keeps it at +0x150
// and gives it by its slot 42 (0x0043D230). No global pointer.
//
// QueryInterface (0x0060C760): IIDs 0x00000001 and 0x89A5BA3F
// (cISC4NetworkLotManager) the object; 0x452294AA (cIGZMessageTarget2) +0x4;
// 0xE4FDA3D4 (cIGZSerializable) +0x8.
//
// --- +0x0: cISC4NetworkLotManager, vtable 0x00AA7958, 16 slots -------------
// gzcom-dll's cISC4NetworkLotManager.h has the slots in the right order.
// Argument counts are each function's own `ret N`.
//    0 QueryInterface(iid, out) 0x0060C760
//    1 AddRef 0x0060C730                 2 Release 0x0060C740
//    3 Init 0x00610120                   4 Shutdown 0x0060E570
//    5 GetNetworkLot(x, z) 0x0060D650
//    6 GetNetworkLotConfigurationSet(set) 0x0060DBF0
//    7 GetNetworkLotConfigurationById(id, a word that is never read)
//      0x0060D9A0
//    8 CreateProp(lot, index, type) 0x0060CF80
//    9 GetZoneTypeForNetwork(x, z) 0x0060D030
//   10 GetWealthForNetwork(x, z) 0x0060CBB0
//   11 IterateAllNetworkLots(callback, context) 0x0060D820
//   12 RedevelopLot(x, z, lot) 0x0060D5E0
//   13 SetLotConfigurationForNextPiece(configuration) 0x0060CB80: code
//      FOLDED with slot 10 of cSC4Lot's vtable (0x00AB19D8 holds the same
//      address), so it is replaced by its slot, never by a detour
//   14 WriteLotConfiguration(configuration, segment) 0x0060E810
//   15 the deleting destructor 0x0060EF20
// --- +0x4: cIGZMessageTarget2, vtable 0x00AA7948, 4 slots -----------------
//    thunks 0x0060E510, 0x0060E520, 0x0060E530 (sub ecx, 4; jmp to slots 0
//    to 2); 3 DoMessage 0x0060FC30
// --- +0x8: cIGZSerializable, vtable 0x00AA7930, 6 slots -------------------
//    thunks 0x0060E540, 0x0060E550, 0x0060E560 (sub ecx, 8; jmp); 3 Write
//    0x0060D6B0; 4 Read 0x0060DAC0; 5 GetGZCLSID 0x0060C720
//
// Not virtual, in the second part: LoadConfigurations 0x006100D0 (no
// arguments; Init calls it), its resource callback 0x0060FB90 (cdecl: a
// resource key and the manager), AddConfiguration 0x0060E7A0 (ret 4) and
// LoadLotConfiguration 0x0060F5F0 (ret 8: a property holder and a
// configuration; the lot editor window's 0x0049C570 calls it too).
//
// Not virtual, in the first part: ConfigurationFits 0x0060CD30 (ret 0x1C;
// `this` is not read; CreateLot and UpdateLot call it), the lot map's
// operator[] 0x0060DA50 (this the table; also used for the pothole map,
// which has the same shape), the configuration map's operator[] 0x0060E3C0
// and its node insert 0x0060E250, and the cell iterator's reset 0x0060C930
// and rectangle 0x0060C960 (this the iterator; DoMessage only).
//
// In the class's range but shared container code of other classes too, and
// left the game's by every part: 0x0060C860, 0x0060C890, 0x0060C8D0,
// 0x0060CE90, 0x0060CEE0 (cell iterators), 0x0060D170 to 0x0060D270 and
// 0x0060E210 (the hash set of uint16), 0x0060D310 (a bit vector's resize),
// 0x0060EE30.
//
// Fields (offsets from the object's start; DoMessage gets object + 4 and
// Write/Read object + 8). Evidence: the constructor 0x0060EE50 and the
// functions named.
//   +0x0C the lot map: an STLport hash table (0x14 bytes: buckets begin +4,
//         end +8, capacity +0xC; count +0x10; the first word is never
//         written; made with 100 buckets by 0x00829BC0), nodes of 0xC bytes:
//         next, the uint16 cell key at +4, the lot at +8 (one reference)
//   +0x20 the configuration map: the same table, nodes of 0x14 bytes: next,
//         the network piece ID at +4, a vector of configurations at +8
//         (begin, end, capacity; each entry one reference)
//   +0x34 the configuration forced for the next piece (referenced), or null
//   +0x38 a cell iterator (0x20 bytes, see below); Init sets its rectangle
//         to the city
//   +0x58 the pothole map: as the lot map, the value an occupant
//   +0x6C uint32 reference count (unsigned compare in Release)
//   +0x70 uint8  initialized (Init sets it, Shutdown tests it)
//   +0x74 the set of cells waiting for UpdateCell: the same table, nodes of
//         8 bytes: next, the uint16 cell key
//
// THE CELL KEY. A cell is packed into 16 bits, (x << 8) | z, in the lot map,
// the pothole map, the waiting set, the save record and the callback of
// IterateAllNetworkLots. GetNetworkLot builds it as ((x & 0xFF) << 8) | z
// with the whole of z, cut to 16 bits: nothing checks x or z against 256.
// This is the game's format (game code and the save share these tables), so
// it is kept exactly; it limits a city to 256 cells a side for everything
// this class does, whatever the city's slots 93 and 94 say.
//
// The cell iterator (0x20 bytes; the manager's at +0x38, and one on
// DoMessage's stack): +0 a state byte (0; 2 when the start is outside the
// rectangle), +1 a byte, +4 and +8 the current cell, +0xC the step (1),
// +0x10 to +0x1C the rectangle: low x, low z, high x, high z (reset:
// 0x80000000 twice and 0x7FFFFFFF twice).
//
// The save record (type = the class ID; Write 0x0060D6B0, Read 0x0060DAC0;
// tools/re/save_schemas/netlot.py): uint16 version 2; uint32 the number of
// lots; per lot the uint16 cell key, a reference to the lot's network piece
// and the uint32 ID of its configuration; uint32 the number of potholes;
// per pothole the uint16 cell key and a reference to the occupant.
//
// === The network lot (cISC4NetworkLot) ====================================
// 0x18 bytes, no class ID, no factory, no constructor function: built in
// place at 0x0060DB12 (Read) and 0x0060DE9D (CreateLot). IID 0x89A5BA66.
// One vtable, 0x00AA78E0, 7 slots (the header's order is right):
//    0 QueryInterface(iid, out) 0x0060C800: IIDs 1 and 0x89A5BA66
//    1 AddRef 0x004F7BA0 (shared code: ++[+0x10])
//    2 Release 0x0060C7E0
//    3 GetNetworkOccupant 0x0060CE80 (the four bytes "mov eax, [ecx+4];
//      ret", FOLDED into about 150 vtables: replaced by its slot only)
//    4 GetLotConfigurationObjectCount 0x0060CE50
//    5 GetLotConfigurationObjectArray 0x00610020
//    6 the deleting destructor 0x0060D130
// Fields: +0x4 the network piece (referenced), +0x8 the configuration
// (referenced; null between Read and Init), +0xC the configuration's ID as
// read from the save (only Read writes it), +0x10 the reference count,
// +0x14 a byte: the configuration was forced (such a lot is never chosen
// again).
//
// === The network lot configuration (cISC4NetworkLotConfiguration) =========
// 0x6C bytes, no class ID; constructor 0x0060FA90 (new at 0x0060FBE1, in the
// resource callback, and in the lot editor window's constructor
// 0x00499E00), deleting destructor 0x00610070. One vtable, 0x00AA7998, 8
// slots (the header's order is right):
//    0 QueryInterface 0x0048C940 (shared code: IID 1 only)
//    1 AddRef 0x0060C830                 2 Release 0x0060C840
//    3 GetObjectArray 0x0060FB50 ("lea eax, [ecx+4]; ret", FOLDED, 44
//      vtables)
//    4 GetID 0x0040CE60 ("mov eax, [ecx+0x10]; ret", shared)
//    5 GetName(text) 0x0060FB60 (the text's slot 13 with the string at
//      +0x14, by a jump; FOLDED with one other vtable, 0x00AB0EC0)
//    6 GetNetworkPieceId 0x0060FB80 ("mov eax, [ecx+0x28]; ret", FOLDED, 13
//      vtables; the address is named cSTETerrain::CellCountX)
//    7 the deleting destructor 0x00610070
// Fields: +0x4 a vector of lot configuration objects (0x34 bytes each, the
// lot configuration's kind: src/core/lotcfg_sim.h), +0x10 the ID, +0x14 the
// name (a cRZBaseString, vtable 0x00A80810: the characters' begin, end and
// capacity at +0x18 to +0x20 and the string's own reference count at +0x24,
// which nothing here counts), +0x28 the network
// piece ID (the configuration map's key), +0x2C four mask bytes (by z modulo
// the period: a bit per x modulo the period), +0x30 a byte: the period,
// +0x34 a bit vector of 16 (the zone types; 0x14 bytes: the words at +0x34,
// the first bit's offset at +0x38, the end's word and offset at +0x3C and
// +0x40, the capacity at +0x44), +0x48 a bit vector of 4 (the wealths), +0x5C and
// +0x60 floats: the least and the greatest slope, +0x64 a byte mask over
// the piece's slot 0x60 value, +0x65 a byte (0 any; 1 needs, 2 forbids the
// piece's slot 0x64), +0x68 the reference count.
#pragma once

#include "core/game_access.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::netlot_sim {

inline constexpr uint32_t kClassId = 0xE9A6CD3B;
inline constexpr uint32_t kIidUnknown = 0x00000001;
inline constexpr uint32_t kIidManager = 0x89A5BA3F;
inline constexpr uint32_t kIidMessageTarget = 0x452294AA;
inline constexpr uint32_t kIidSerializable = 0xE4FDA3D4;
inline constexpr uint32_t kIidLot = 0x89A5BA66;
inline constexpr uint32_t kIidNetworkOccupant = 0xA821EF94; // Read asks a lot's reference for it
inline constexpr uint32_t kIidPothole = 0x09820792;         // and a pothole's for this

inline constexpr std::size_t kObjectSize = 0x88;
inline constexpr std::size_t kMessagePart = 0x4;
inline constexpr std::size_t kSerializablePart = 0x8;
inline constexpr std::size_t kSlots = 16;
inline constexpr std::size_t kMessageSlots = 4;
inline constexpr std::size_t kSerializableSlots = 6;
inline constexpr std::size_t kLotSize = 0x18, kLotSlots = 7;
inline constexpr std::size_t kConfigurationSize = 0x6C, kConfigurationSlots = 8;

// The vtables of the secondary parts (the primary ones are address
// entries: the slot swaps check them).
inline constexpr std::uintptr_t kMessageVtable = 0x00AA7948, kSerializableVtable = 0x00AA7930;

// The save record's version, the bucket count the constructor asks its four
// tables for, the mask of the surface networks (every type but pipe, power
// line and subway) the manager asks the traffic network for, and the
// resource type of a configuration's exemplar.
inline constexpr uint16_t kRecordVersion = 2;
inline constexpr uint32_t kBuckets = 100;
inline constexpr uint32_t kSurfaceMask = 0x1F4F;
inline constexpr uint32_t kExemplarType = 0x6534284A;

// The numbers of the two queries: the cells looked at around a cell (the
// four neighbours, then the four diagonals; the offset tables at 0x00AA7858
// and 0x00AA7878 have eight entries each), the zone types the neighbour
// pass leaves the diagonals for (none, and 7 and 14: in the rank table at
// 0x00AA76E8 type 7 has rank 8, the worst of types 1 to 9, and type 14 rank
// 17, behind the 16 of types 10 to 13 and 15), and the number of wealths.
inline constexpr uint32_t kNeighbours = 4, kNeighboursAndDiagonals = 8;
inline constexpr int32_t kZoneNone = 0, kZoneWeakA = 7, kZoneWeakB = 14;
inline constexpr uint32_t kZoneTypes = 16, kWealths = 4;

namespace field {
inline constexpr std::size_t kLots = 0x0C;
inline constexpr std::size_t kConfigurations = 0x20;
inline constexpr std::size_t kForced = 0x34;
inline constexpr std::size_t kIterator = 0x38;
inline constexpr std::size_t kPotholes = 0x58;
inline constexpr std::size_t kRefCount = 0x6C;
inline constexpr std::size_t kInitialized = 0x70;
inline constexpr std::size_t kPending = 0x74;
} // namespace field

// A hash table of the game (the four the manager keeps), a node of the lot
// map and of the pothole map, a node of the configuration map and a node of
// the waiting set.
namespace hashtable {
inline constexpr std::size_t kBuckets = 0x4, kBucketsEnd = 0x8, kBucketsCapacity = 0xC, kCount = 0x10, kSize = 0x14;
} // namespace hashtable
namespace hashnode {
inline constexpr std::size_t kNext = 0x0, kKey = 0x4, kValue = 0x8;
inline constexpr std::size_t kLotNodeSize = 0xC, kConfigurationNodeSize = 0x14, kPendingNodeSize = 0x8;
} // namespace hashnode

// The cell iterator.
namespace iterator {
inline constexpr std::size_t kState = 0x0, kByte1 = 0x1, kX = 0x4, kZ = 0x8, kStep = 0xC;
inline constexpr std::size_t kLowX = 0x10, kLowZ = 0x14, kHighX = 0x18, kHighZ = 0x1C, kSize = 0x20;
inline constexpr int32_t kResetLow = INT32_MIN, kResetHigh = INT32_MAX;
} // namespace iterator

// The manager's slots (byte offsets in the vtable 0x00AA7958).
namespace slot {
inline constexpr std::size_t kQueryInterface = 0x00, kAddRef = 0x04, kRelease = 0x08, kInit = 0x0C, kShutdown = 0x10;
inline constexpr std::size_t kGetNetworkLot = 0x14, kGetNetworkLotConfigurationSet = 0x18;
inline constexpr std::size_t kGetNetworkLotConfigurationById = 0x1C, kCreateProp = 0x20;
inline constexpr std::size_t kGetZoneTypeForNetwork = 0x24, kGetWealthForNetwork = 0x28;
inline constexpr std::size_t kIterateAllNetworkLots = 0x2C, kRedevelopLot = 0x30;
inline constexpr std::size_t kSetLotConfigurationForNextPiece = 0x34, kWriteLotConfiguration = 0x38;
inline constexpr std::size_t kDeletingDestructor = 0x3C;
// The message part's (+0x4) and the serializable part's (+0x8).
inline constexpr std::size_t kDoMessage = 0x0C;
inline constexpr std::size_t kWrite = 0x0C, kRead = 0x10, kGetGZCLSID = 0x14;
} // namespace slot

// The network lot: fields and slots (vtable 0x00AA78E0).
namespace lot {
inline constexpr std::size_t kOccupant = 0x04, kConfiguration = 0x08, kSavedConfigurationId = 0x0C;
inline constexpr std::size_t kRefCount = 0x10, kForced = 0x14;
} // namespace lot
namespace lot_slot {
inline constexpr std::size_t kQueryInterface = 0x00, kAddRef = 0x04, kRelease = 0x08, kGetNetworkOccupant = 0x0C;
inline constexpr std::size_t kGetObjectCount = 0x10, kGetObjectArray = 0x14, kDeletingDestructor = 0x18;
} // namespace lot_slot

// The network lot configuration: fields and slots (vtable 0x00AA7998).
namespace configuration {
inline constexpr std::size_t kObjects = 0x04, kId = 0x10, kName = 0x14, kWord24 = 0x24, kPieceId = 0x28;
inline constexpr std::size_t kCellMasks = 0x2C, kPeriod = 0x30, kZones = 0x34, kWealths = 0x48;
inline constexpr std::size_t kLeastSlope = 0x5C, kGreatestSlope = 0x60, kPieceMask = 0x64, kPieceFlagRule = 0x65;
inline constexpr std::size_t kRefCount = 0x68;
// The rule byte at +0x65.
inline constexpr uint8_t kNeedsPieceFlag = 1, kForbidsPieceFlag = 2;
// What the constructor leaves: the period, every mask byte and the piece
// mask, and the greatest slope (the least is 0).
inline constexpr uint8_t kDefaultPeriod = 4, kDefaultMask = 0x0F;
inline constexpr float kDefaultGreatestSlope = 64.0f;
inline constexpr std::size_t kCellMaskCount = 4;
// A lot configuration object of the vector at +0x4 (src/core/lotcfg_sim.h
// has its fields).
inline constexpr std::size_t kObjectSize = 0x34;
inline constexpr std::size_t kObjectIds = 0x28; // the start of the storage of its list of IDs
} // namespace configuration
namespace configuration_slot {
inline constexpr std::size_t kQueryInterface = 0x00, kAddRef = 0x04, kRelease = 0x08, kGetObjectArray = 0x0C;
inline constexpr std::size_t kGetId = 0x10, kGetName = 0x14, kGetNetworkPieceId = 0x18, kDeletingDestructor = 0x1C;
} // namespace configuration_slot

// The slots of other objects the first part calls. The city (0x00B43CEC):
// the cell counts, the traffic network and the network lot manager. The
// zone manager (0x00B43D14): its grid, and the grid's columns. The lot
// manager (0x00B43D08) and a lot. The building development simulator
// (0x00B43D68), the lot developer it gives, and a string.
namespace city_slot {
inline constexpr std::size_t kTrafficNetwork = 0xA0;    // slot 40
inline constexpr std::size_t kNetworkLotManager = 0xA8; // slot 42 (0x0043D230: the pointer at +0x150)
inline constexpr std::size_t kCellCountX = 0x174, kCellCountZ = 0x178; // slots 93 and 94
} // namespace city_slot
namespace zones_slot {
inline constexpr std::size_t kGetGrid = 0x14;    // slot 5 (0x00698CF0), no arguments
inline constexpr std::size_t kGetColumns = 0x64; // the grid's slot 25, no arguments
inline constexpr std::size_t kColumnSize = 12;   // a column: three words, the first its bytes by z
} // namespace zones_slot
namespace lots_slot {
inline constexpr std::size_t kGetLot = 0x18; // slot 6 (0x0072B410, ret 0xC): (x, z, a flag)
} // namespace lots_slot
namespace citylot_slot {
inline constexpr std::size_t kIsInLivableState = 0x94; // slot 37 (0x006C2150), no arguments; AL
inline constexpr std::size_t kGetOccupantWealth = 0xA4; // slot 41 (0x006C21F0), no arguments
inline constexpr std::size_t kGetBuildingType = 0xB8;   // slot 46 (0x006BFA00, ret 4): (a flag)
} // namespace citylot_slot
namespace development_slot {
inline constexpr std::size_t kGetLotDeveloper = 0x90;       // slot 36 (0x00698CE0), no arguments
inline constexpr std::size_t kGetBuildingProfilePtr = 0xA0; // slot 40 (0x006985C0, ret 4): (building type)
inline constexpr std::size_t kProfileWealth = 0x4;          // the wealth in what it gives
inline constexpr std::size_t kGetAreaSlope = 0x74; // the lot developer's slot 29 (0x006C5DC0, ret 8): (rect, 1)
} // namespace development_slot
namespace string_slot {
inline constexpr std::size_t kCopy = 0x34; // slot 13: (another string)
} // namespace string_slot
namespace traffic_slot {
inline constexpr std::size_t kGetPiece = 0x20; // slot 8 (0x004B0B20, ret 0x10): (x, z, network mask, a flag)
} // namespace traffic_slot

// What the second part calls. The message server (0x00B43CCC): slots 5 and
// 6, each (the target, the message ID). A stream: a uint16 and a uint32 (the
// value when writing, its address when reading) and whether it is in error.
// The resource manager (0x00B43CA8): a resource by its key (key, IID, out,
// 0, 0), the list of the keys of a type (out, type) and the segment that
// holds a key (key, out); the key list's walk (a cdecl callback and its
// context); a resource's property holder. A pothole's occupant, as its
// interface 0x09820792 has it: slot 8 with 1, before it is let go.
namespace server_slot {
inline constexpr std::size_t kAddNotification = 0x14, kRemoveNotification = 0x18;
} // namespace server_slot
namespace stream_slot {
inline constexpr std::size_t kUint16 = 0x1C, kUint32 = 0x24, kGetError = 0x48;
} // namespace stream_slot
namespace resources_slot {
inline constexpr std::size_t kGetResource = 0x0C, kGetKeyList = 0x48, kFindSegment = 0x80;
inline constexpr std::size_t kListEnumerate = 0x1C;    // the key list's slot 7: (callback, context)
inline constexpr std::size_t kAsPropertyHolder = 0x1C; // an exemplar resource's slot 7, no arguments
} // namespace resources_slot
namespace pothole_slot {
inline constexpr std::size_t kStop = 0x20;
} // namespace pothole_slot

// The six messages Init subscribes the part at +4 to and Shutdown takes it
// off, both in this order: a piece's usable flag changed, an occupant was
// removed, a lot's objects are to be built, cells changed, a lot's state
// changed, a pothole.
inline constexpr uint32_t kMessages[] = {0x89D3E9F3, 0x99EF1143, 0xA9B6C2D6, 0x07699455, 0xE8BDA2E7, 0x69247DC8};

// The exemplar of a network lot configuration: the interface a resource is
// asked for, the value of its exemplar type property (0x10), and the
// properties LoadLotConfiguration reads.
inline constexpr uint32_t kIidExemplar = 0xA52160F5;
inline constexpr uint32_t kExemplarTypeValue = 0x21;
inline constexpr uint32_t kResaveGroup = 0x89AC5643; // the group an old exemplar is written back under
namespace property {
inline constexpr uint32_t kExemplarType = 0x00000010;
inline constexpr uint32_t kName = 0x00000020;         // a string: +0x14
inline constexpr uint32_t kId = 0x00000021;           // uint32: +0x10
inline constexpr uint32_t kVersion = 0x88EDC789;      // uint8, 1 when missing; below 2 the exemplar is rewritten
inline constexpr uint32_t kZoneTypes = 0x88EDC793;    // bytes: the zone types' bits at +0x34
inline constexpr uint32_t kWealths = 0x88EDC795;      // bytes: the wealths' bits at +0x48
inline constexpr uint32_t kCellMasks = 0x49D55951;    // exactly four bytes: +0x2C
inline constexpr uint32_t kPieceId = 0xC9A5A1BE;      // uint32: +0x28; without it nothing is loaded
inline constexpr uint32_t kPeriod = 0xCA81B8D4;       // uint8: +0x30
inline constexpr uint32_t kLeastSlope = 0xAA120972;   // float: +0x5C
inline constexpr uint32_t kGreatestSlope = 0xAA120973; // float: +0x60
inline constexpr uint32_t kPieceMask = 0xEC3BD470;    // uint8: +0x64
inline constexpr uint32_t kPieceFlagRule = 0xCC3E4755; // uint8: +0x65
} // namespace property

// A bit vector of the game (the configuration's two): the first word and the
// first bit's offset in it, the end's word and offset, the capacity.
namespace bitvector {
inline constexpr std::size_t kWords = 0x0, kOffset = 0x4, kEndWords = 0x8, kEndOffset = 0xC, kCapacity = 0x10;
} // namespace bitvector

// A string of the game (cRZBaseString, 0x14 bytes): the configuration's
// name at +0x14, and the text Init formats for a lot without a
// configuration.
struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t references;
};

// A vector as the game keeps it.
struct Vector {
    char* begin;
    char* end;
    char* capacity;
};

// What the hash table's begin (0x004B0390) fills: the first node (null for
// an empty table) and the table.
struct HashIterator {
    char* node;
    char* table;
};

// The tree set GetNetworkLotConfigurationSet fills (an STLport set of
// pointers, ordered by their unsigned value): the head node and the count.
// A node is 0x14 bytes from the game's node allocator (0x0090CF54): a color
// byte, the parent (the head's: the root), the left child (the head's: the
// least node), the right child (the head's: the greatest) and the value. An
// empty set's head has no root and is its own left and right.
struct TreeSet {
    char* head;
    uint32_t count;
};
namespace tree {
inline constexpr std::size_t kColor = 0x0, kParent = 0x4, kLeft = 0x8, kRight = 0xC, kValue = 0x10, kNodeSize = 0x14;
} // namespace tree

// The callback of IterateAllNetworkLots (cdecl): the cell's x and z out of
// the key, the lot, the caller's context; false stops the walk.
using LotCallback = uint8_t(__cdecl*)(uint32_t x, uint32_t z, void* lot, void* context);

inline uint32_t AddRefOf(void* object) { return Virtual<uint32_t>(object, 0x4); }
inline uint32_t ReleaseOf(void* object) { return Virtual<uint32_t>(object, 0x8); }

// A game function at its address: thiscall with `self` in ECX, or cdecl.
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

// The cell key as GetNetworkLot builds it (see THE CELL KEY above).
inline uint16_t CellKey(uint32_t x, uint32_t z)
{
    return static_cast<uint16_t>(((x & 0xFF) << 8) | z);
}

// The number of buckets of a table (the words between the pointers at +4
// and +8; the byte difference shifted right as a signed number, then used
// unsigned, as the game's divisions do).
inline uint32_t BucketCount(char* table)
{
    const uint32_t bytes = At<uint32_t>(table, hashtable::kBucketsEnd) - At<uint32_t>(table, hashtable::kBuckets);
    return static_cast<uint32_t>(static_cast<int32_t>(bytes) >> 2);
}

// The first node of the bucket `key` falls in.
inline char* BucketHead(char* table, uint32_t key)
{
    return At<char*>(At<char*>(table, hashtable::kBuckets), (key % BucketCount(table)) * 4);
}

// The node after `node` in a walk over a whole table, as the game has it
// inline in every such walk: the node's link, or at the end of a chain the
// first node of the next bucket that has one after the bucket of the node's
// key (`key` is the node's own, 16 or 32 bits wide by the table); null at
// the end.
inline char* NextNode(char* table, char* node, uint32_t key)
{
    if (char* const next = At<char*>(node, hashnode::kNext))
        return next;
    uint32_t index = key % BucketCount(table);
    const uint32_t count = BucketCount(table);
    for (;;) {
        ++index;
        if (index >= count)
            return nullptr;
        if (char* const found = At<char*>(At<char*>(table, hashtable::kBuckets), index * 4))
            return found;
    }
}
inline char* NextNode16(char* table, char* node) { return NextNode(table, node, At<uint16_t>(node, hashnode::kKey)); }
inline char* NextNode32(char* table, char* node) { return NextNode(table, node, At<uint32_t>(node, hashnode::kKey)); }

// The first node of a table (null for an empty one), as the game has it
// inline where it does not call its begin 0x004B0390 (the pothole map in
// Write and Shutdown): the first bucket that holds a node; the bucket count
// is read again after every empty bucket.
inline char* FirstNode(char* table)
{
    if (BucketCount(table) == 0)
        return nullptr;
    char* const buckets = At<char*>(table, hashtable::kBuckets);
    uint32_t index = 0;
    while (!At<char*>(buckets, index * 4))
        if (++index >= BucketCount(table))
            return nullptr;
    return At<char*>(buckets, index * 4);
}

// The number of bits of a bit vector.
inline uint32_t BitCount(char* vector)
{
    const int32_t words = static_cast<int32_t>(At<uint32_t>(vector, bitvector::kEndWords) -
                                               At<uint32_t>(vector, bitvector::kWords)) >> 2;
    return (static_cast<uint32_t>(words) << 5) - At<uint32_t>(vector, bitvector::kOffset) +
           At<uint32_t>(vector, bitvector::kEndOffset);
}

// The number of configurations in a node of the configuration map.
inline uint32_t ConfigurationCount(char* mapNode)
{
    const Vector* const v = reinterpret_cast<const Vector*>(mapNode + hashnode::kValue);
    return static_cast<uint32_t>(static_cast<int32_t>(v->end - v->begin) >> 2);
}

// Logs "netlot: first <name>" once per flag (netlot_state.cpp).
void FirstCall(const char* name, bool& logged);

// A detour of a group: the game's address and our function. Skipped says
// whether OPENSC4_NETLOT_SKIP (function addresses, hex, eight digits) lists
// the address; InstallDetours installs a group's list but for those and logs
// "netlot <what>: <n> detours".
struct Detour {
    std::uintptr_t address;
    void* ours;
};
#define OPENSC4_NETLOT_DETOUR(address, fn) {address, reinterpret_cast<void*>(&fn)}
bool Skipped(std::uintptr_t address);
bool InstallDetourList(const Detour* detours, std::size_t count, const char* what);
template <std::size_t N>
inline bool InstallDetours(const Detour (&detours)[N], const char* what)
{
    return InstallDetourList(detours, N, what);
}

// A vtable slot of a group: the vtable, the slot's number, the address the
// game has there and our function. SwapSlots stores ours in every slot of
// the list (but for the skipped ones, by the original's address) after it
// has checked that each still holds the game's address, and logs
// "netlot <what>: <n> slots are ours". For code that is folded with other
// classes: only this class's vtable changes.
struct Slot {
    std::uintptr_t vtable;
    int index;
    std::uintptr_t original;
    void* ours;
};
#define OPENSC4_NETLOT_SLOT(vtable, index, original, fn) {vtable, index, original, reinterpret_cast<void*>(&fn)}
bool SwapSlotList(const Slot* slots, std::size_t count, const char* what);
template <std::size_t N>
inline bool SwapSlots(const Slot (&slots)[N], const char* what)
{
    return SwapSlotList(slots, N, what);
}

} // namespace opensc4::netlot_sim
