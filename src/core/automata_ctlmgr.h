// The layout of cSC4AutomataControllerManager (build 641; class ID
// 0xCA14CA03, IID 0xCA14C9FC, 0x80 bytes, made by the factory 0x00514840
// through the constructor 0x0051AA50; the city keeps it at +0x1E0, city slot
// 47 GetAutomataControllerManager) and of the records it keeps, as
// src/core/automata_ctlmgr.cpp and src/core/automata_autotest.cpp read them.
//
// Parts and vtables:
//   +0x00 cISC4AutomataControllerManager, 0x00A96058 (25 slots: 0 QI, 1
//         AddRef (0x00695330, shared), 2 Release, 3 Init, 4 Shutdown, 5-23
//         as the gzcom-dll header with each overloaded pair in the other
//         order, 24 GetControllerTime 0x0051A310 (folded); the 26th word
//         0x00A960BC is the next vtable, the controller template base's)
//   +0x04 the cheat code target, 0x00A96048 (slot 3 0x005187F0)
//   +0x08 the message target, 0x00A96038 (slot 3 0x00519360 DoMessage)
//   +0x0C cIGZSerializable, 0x00A96020 (3 Write, 4 Read, 5 GetGZCLSID; Write
//         and Read get the part, object + 0xC)
// Fields:
//   +0x10 uint8  initialized (Init, Shutdown)
//   +0x11 uint8  loaded (Read sets it)
//   +0x14 uint32 reference count
//   +0x18 the 24-hour clock (city slot 86), referenced; saved (IID 0x681BD53A)
//   +0x1C the controller grid (0x18 bytes, Grid below)
//   +0x34 the controller time (GetControllerTime answers +0x34): a float,
//         a 16-bit word, a float, a 16-bit word (+0x34, +0x38, +0x3C,
//         +0x40); saved
//   +0x44 the automata script system (city slot 48), referenced
//   +0x48 .. +0x50 three words, +0x54 a byte (0), +0x55 a byte (1): only the
//         constructor writes them in this part
//   +0x58 the controller table: a hash table (HashTable) of ControllerNode,
//         an occupant (referenced) to the list of its controllers
//   +0x6C the automaton table: a hash table of AutomatonNode, an occupant
//         (referenced) to a list of automata (referenced) with a count, two
//         vectors and a byte (written by the attractor code, 0x0051B9D0)
#pragma once

#include <cstddef>
#include <cstdint>

namespace opensc4::automata_ctlmgr {

namespace field {
inline constexpr std::size_t kMessagePart = 0x04, kTargetPart = 0x08, kSerializable = 0x0C;
inline constexpr std::size_t kInitialized = 0x10, kLoaded = 0x11, kReferences = 0x14;
inline constexpr std::size_t kClock = 0x18, kGrid = 0x1C, kTime = 0x34;
inline constexpr std::size_t kTimeA = 0x34, kTimeB = 0x38, kTimeC = 0x3C, kTimeD = 0x40;
inline constexpr std::size_t kScripts = 0x44;
inline constexpr std::size_t kWord48 = 0x48, kWord4C = 0x4C, kWord50 = 0x50, kByte54 = 0x54, kByte55 = 0x55;
inline constexpr std::size_t kControllers = 0x58, kAutomata = 0x6C;
inline constexpr std::size_t kSize = 0x80;
} // namespace field

// A list node of the game's STL (12 bytes); a list object is its head
// pointer (4 bytes, no size kept).
struct ListNode {
    ListNode* next;
    ListNode* prev;
    void* value;
};
static_assert(sizeof(ListNode) == 0xC);

// An STLport hash table (0x14 bytes): +0 the empty hash functor, +4/+8/+0xC
// the bucket vector, +0x10 the element count. Buckets are node pointers; a
// pointer key hashes to itself (bucket = key % bucket count).
struct HashTable {
    uint32_t functor;
    void** begin;
    void** end;
    void** capacity;
    uint32_t count;
};
static_assert(sizeof(HashTable) == 0x14);

// What SC4HashTable::Begin and SC4Uint32HashTable::Find answer.
struct Iterator {
    void* node;
    HashTable* table;
};

// What the inserts answer (9 bytes written: keep room for 12).
struct InsertResult {
    void* node;
    HashTable* table;
    uint8_t inserted;
    uint8_t padding[3];
};
static_assert(sizeof(InsertResult) == 0xC);

// A node of the controller table (+0x58).
struct ControllerNode {
    ControllerNode* next;
    void* occupant;
    ListNode* head; // the controllers
};

// A value of the automaton table (0x24 bytes; 0x00518A40 copies it).
struct AutomatonValue {
    ListNode* head; // the automata (referenced)
    uint32_t count;
    uint32_t first[3];  // a vector (three floats)
    uint32_t second[3]; // another
    uint8_t flag;
    uint8_t padding[3];
};
static_assert(sizeof(AutomatonValue) == 0x24);

// A node of the automaton table (+0x6C, 0x2C bytes).
struct AutomatonNode {
    AutomatonNode* next;
    void* occupant;
    AutomatonValue value;
};
static_assert(sizeof(AutomatonNode) == 0x2C);

// The controller grid (+0x1C): a vector of lists of controllers, one list
// per grid cell (cell index = x << shift + z), the cells per side (the
// city's cells in x over 8, for both sides), the shift (log2 of the side,
// 32 when the side is no power of two) and the city's size in metres (cells
// times 16; z from the city's cells in z).
struct Grid {
    ListNode** begin;
    ListNode** end;
    ListNode** capacity;
    uint8_t cellsX;
    uint8_t cellsZ;
    uint8_t shift;
    uint8_t padding;
    float sizeX;
    float sizeZ;
};
static_assert(sizeof(Grid) == 0x18);

// A controller template (tControllerTemplate, made by the script system):
// +0x4 its name (a cRZBaseString: +0x8 begin, +0xC end), +0x1C the
// controller's class ID, +0x58 flags (0x40: not refused for an occupant of
// the same lot), +0x5C the radius (metres) within which a second controller
// of the template is refused.
namespace tmpl {
inline constexpr std::size_t kName = 0x04, kNameBegin = 0x08, kNameEnd = 0x0C;
inline constexpr std::size_t kClassId = 0x1C, kFlags = 0x58, kRadius = 0x5C;
inline constexpr uint8_t kAnyLot = 0x40;
} // namespace tmpl

// A controller (cISC4AutomataController; attractor vtable 0x00A95E48,
// generator 0x00A95F00, base 0x00A95D90): the slots this part calls.
namespace controller {
inline constexpr std::size_t kAddRef = 0x04, kRelease = 0x08, kInit = 0x0C, kShutdown = 0x10;
inline constexpr std::size_t kName = 0x1C, kTemplate = 0x20, kTestFlag = 0x30;
inline constexpr std::size_t kSetOccupant = 0x38, kSetTemplate = 0x3C, kSetPosition = 0x44, kMoved = 0x6C;
inline constexpr std::uintptr_t kAttractorVtable = 0x00A95E48, kGeneratorVtable = 0x00A95F00;
} // namespace controller

inline constexpr uint32_t kClassId = 0xCA14CA03;
inline constexpr uint32_t kIid = 0xCA14C9FC;
inline constexpr uint32_t kIidSerializable = 0xE4FDA3D4;
inline constexpr uint32_t kIidMessageTarget = 0x452294AA;
inline constexpr uint32_t kIidController = 0x6A1857F8;
inline constexpr uint32_t kIidOccupant = 0x26BB767E;
inline constexpr uint32_t kIidAutomaton = 0xA9B40F05;
inline constexpr uint32_t kIidClock = 0x681BD53A;
inline constexpr std::size_t kCitySlotManager = 0xBC; // cSC4City::GetAutomataControllerManager

} // namespace opensc4::automata_ctlmgr
