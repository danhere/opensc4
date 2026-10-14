// Our cSC4PropManager, second half (build 641; 0x004A3040 to 0x004A44E0):
// making a prop, the prop lists by area, lot, street and building, the
// message handler (the text decal command), the exemplar loader that fills
// the type, family and profile tables, and Init. The first half of the
// class (0x004A1D20 to 0x004A3040: identity, lifetime, the add and get
// slots, the filter, the table queries) is propmgr_1.cpp.
//
// The object (0x28 bytes; the first half's header has the full layout):
//   +0x00 cISC4PropManager, vtable 0x00A8F928
//   +0x04 cIGZMessageTarget2, vtable 0x00A8F918 (DoMessage gets this part)
//   +0x08 cIGZSerializable, vtable 0x00A8F900
//   +0x0C uint8 initialized; +0x10 reference count
//   +0x14 the occupant manager (global 0x00B43D0C), +0x18 the resource
//   manager (0x00B43CA8), +0x1C the prop filter, +0x20 the lot manager
//   (0x00B43D08), +0x24 an object of the service 0x00B43DC4 (replaced by
//   message 0x06CDB65B)
//
// The prop filter (0x14 bytes, made by Init; mode at +0xC and the building
// at +0x10): the mode picks what the occupant manager's query takes: 1 the
// city, 2 a lot, 3 a building, 4 a street. The queries here set it and give
// the filter to the occupant manager's query by city cells (slot 40, +0xA0).
//
// Three tables, all hash tables of the STLport kind (+4 the bucket array's
// begin, +8 its end, +0x10 the element count; a node is next, key, value):
//   0x00B21760 prop type -> its resource key (3 dwords)
//   0x00B21774 family -> vector of the types in it (begin, end, capacity)
//   0x00B2179C prop type -> profile (28 bytes, the layout below)
//   0x00B21788 prop type -> name (read by GetPropName; nothing in this half
//   writes it: the loader reads the exemplar's name into a string and drops
//   it)
// The profile (cISC4PropOccupant::PropProfile), seven dwords: the occupant
// class (property 0xA977A86B, else 0x2977AA47), the size (property
// 0x27812810, three dwords, zero when absent), requester satisfaction
// (0x49A1E05A), wealth (0xE9A316EB, one byte) and crime incident ID
// (0x8AA864D5).
//
// A building's props: the building's property holder keeps a counted
// attachment under the property 0xA8322B7F: a 0x14-byte object (vtable
// 0x00A8F9CC: QueryInterface, AddRef 0x004F7BA0, Release 0x004A3CE0; a vector
// of the props at +4/+8/+0xC and the count at +0x10).
//
// Start-up. Init (slot 3) runs when the city starts, which is after every
// replacement is installed (the coverage run saw it in 19 of 20 runs and the
// loader in all 20), so this file's loader is reachable as ours in a run.
// But it runs once per process: the flag 0x00B21758 (set by Init, cleared by
// the clean-up 0x004A2A30 at the application's shutdown) keeps a second city
// from loading the exemplars again, and the tables then stay as the first
// city made them. A test reaches the loader by calling it on a manager with
// the flag cleared (which also needs the tables cleared by the clean-up).
//
// The order the exemplars are read and stored: the resource manager's list of
// exemplar resources (slot 18 of 0x00B43CA8, type 0x6534284A) walks them in
// its own order and calls 0x004A42A0 for each; the callback loads the
// exemplar and keeps it when its property 0x10 (exemplar type) is 0x1E (a
// prop). The tables are keyed by the instance ID (a number, not a pointer):
// a new key goes at the front of its bucket (index = key mod bucket count),
// the table grows by 0x004F0940 before an insert; the type table is the
// game's 0x004A3470 (template code shared with other classes), the family
// table the game's 0x004A3D10. A family's vector holds its types in the
// order the exemplars come (a type already there is not added again).
//
// Random numbers: none. Clocks: none.
//
// Replacement, off by default:
//
//   simulators.propmgr.part2: everything named here but the two table
//   helpers below.
//
// Still the game's: 0x004A3470 and 0x004A3D10 (the hash table operator[] and
// insert, template code three other classes use with their own tables),
// 0x0065FAD0 (the profile table's operator[], same kind), 0x0049FC30 and
// 0x0049FB10 (vector push_back and destructor, shared with the occupant
// manager); and the allocators and constructors of the prop classes.
//
// Oddities kept:
//   GetStreetProps (slot 31) always answers false, even when it leaves a
//   list: it only ever returns from its last line. The one caller that
//   matters, RemoveStreetPropsFromTile, ignores the answer.
//   AddBuildingProp does not release the building's list it got when the
//   building already had one (the lookup took a reference); a new list is
//   released after the building keeps it.
//   RemoveBuildingProps empties the vector but never frees it.
//   The loader copies as many dwords of property 0x27812810 as it says it
//   has into a three dword local (the original's overflow, not copied: we
//   take at most three), and the profile's wealth dword has the original's
//   stack garbage in its three high bytes (we store zero).
//   The text decal gets one reference too many (nothing releases it after
//   the city's AddCityProp took its own).
//   The loader's name string (property 0x20) is read and thrown away.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace opensc4::propmgr {
namespace {

namespace field {
constexpr std::size_t kInitialized = 0x0C;
constexpr std::size_t kOccupants = 0x14;
constexpr std::size_t kResources = 0x18;
constexpr std::size_t kFilter = 0x1C;
constexpr std::size_t kLots = 0x20;
constexpr std::size_t kService = 0x24;
} // namespace field

namespace filter {
constexpr std::size_t kMode = 0xC;
constexpr uint32_t kStreet = 4;
constexpr uint32_t kLot = 2;
} // namespace filter

namespace table { // the game's hash tables
constexpr std::size_t kBuckets = 0x4;
constexpr std::size_t kBucketsEnd = 0x8;
constexpr std::size_t kCount = 0x10;
constexpr std::size_t kKey = 0x4;
constexpr std::size_t kValue = 0x8;
} // namespace table

namespace list { // a building's prop list object
constexpr std::size_t kVector = 0x4;
constexpr std::size_t kVectorEnd = 0x8;
constexpr std::size_t kReferences = 0x10;
} // namespace list

// The occupant manager (vtable 0x00A8F7F0) slots.
namespace occmgr {
constexpr std::size_t kInsert = 0x50;
constexpr std::size_t kRemove = 0x54;
constexpr std::size_t kGetByCityCells = 0xA0;
} // namespace occmgr

// The occupant (cISC4Occupant) slots.
namespace occupant {
constexpr std::size_t kInit = 0x0C;
constexpr std::size_t kShutdown = 0x10;
constexpr std::size_t kAsPropertyHolder = 0x18;
constexpr std::size_t kGetPosition = 0x20;
constexpr std::size_t kSetPosition = 0x24;
constexpr std::size_t kIsFlagSet = 0x80;
} // namespace occupant

// The prop (cISC4PropOccupant, vtable 0x00AA4900) slots.
namespace prop {
constexpr std::size_t kSetPropType = 0x20;
} // namespace prop

// The property holder (vtable 0x00AA47E8) slots.
namespace holder {
constexpr std::size_t kGetAttachment = 0x18; // (property, interface, out)
constexpr std::size_t kGetProperty = 0x24;   // (property): the property or null
constexpr std::size_t kSetAttachment = 0x2C; // (property, object, 0)
constexpr std::size_t kRemoveAttachment = 0x44; // (property, 0)
} // namespace holder

// A property (as the holder gives it) and its value object.
namespace property {
constexpr std::size_t kValue = 0x14;
} // namespace property
namespace value {
constexpr std::size_t kCount = 0x10;
constexpr std::size_t kGetString = 0xDC;   // (string*)
constexpr std::size_t kGetFloat = 0x90;    // the float in ST0
constexpr std::size_t kUint32Array = 0x19C;
constexpr std::size_t kFloatArray = 0x1BC; // read as raw dwords
} // namespace value

// The main object's own slots (vtable 0x00A8F928).
namespace slot {
constexpr std::size_t kIsPropTypeValid = 0x20;
constexpr std::size_t kGetPropProfile = 0x34;
constexpr std::size_t kAddCityProp = 0x4C; // (occupant)
constexpr std::size_t kGetCityPropsInRectangle = 0x44;
constexpr std::size_t kGetCityPropsInArea = 0x48;
constexpr std::size_t kGetLotPropsAt = 0x5C;
constexpr std::size_t kGetLotPropsOfLot = 0x60;
constexpr std::size_t kGetStreetProps = 0x7C;
} // namespace slot

constexpr uint32_t kIidProp = 0xE9793A65;
constexpr uint32_t kIidExemplar = 0xA52160F5;
constexpr uint32_t kPropClassDefault = 0x2977AA47;
constexpr uint32_t kPropClassDecal = 0x2977AA48;
constexpr uint32_t kPropClassEffect = 0x2977AA49;
constexpr uint32_t kBuildingPropList = 0xA8322B7F;
constexpr uint32_t kExemplarType = 0x6534284A;
constexpr uint32_t kPropExemplarType = 0x1E;
constexpr uint32_t kPropertyExemplarType = 0x10;
constexpr uint32_t kPropertySize = 0x27812810;
constexpr uint32_t kPropertyFamilies = 0x27812870;
constexpr uint32_t kPropertyClass = 0xA977A86B;
constexpr uint32_t kPropertySatisfaction = 0x49A1E05A;
constexpr uint32_t kPropertyWealth = 0xE9A316EB;
constexpr uint32_t kPropertyCrime = 0x8AA864D5;
constexpr uint32_t kPropertyName = 0x20;
constexpr uint32_t kMessageCityPropsChanged = 0xA6B79602;
constexpr uint32_t kMessageService = 0x06CDB65B;
constexpr uint32_t kMessageCityInited = 0x26D31EC1;
constexpr uint32_t kMessageTextDecal = 0x0BEAF1A2;
constexpr uint32_t kNotifications[] = {kMessageCityPropsChanged, kMessageService, kMessageCityInited,
                                       kMessageTextDecal};
constexpr float kOne = 1.0f; // 0x3F800000

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

uint32_t AddRef(void* object)
{
    return Virtual<uint32_t>(object, 0x4);
}

uint32_t Release(void* object)
{
    return Virtual<uint32_t>(object, 0x8);
}

// A list (the game's SC4List): the head node pointer; nodes are next,
// previous, value; the head node is the sentinel.
struct ListNode {
    ListNode* next;
    ListNode* previous;
    void* value;
};

// A new empty list: the sentinel from the list node allocator (0xC bytes),
// linked to itself.
ListNode* NewList()
{
    void* const allocator = ListNodeAllocator();
    ListNode* const head = Virtual<ListNode*>(allocator, 0xC, uint32_t{0xC});
    head->next = head;
    head->previous = head;
    return head;
}

// --- Hash tables ----------------------------------------------------------------------

uint32_t BucketCount(std::uintptr_t t)
{
    return static_cast<uint32_t>(static_cast<int32_t>(Global<char*>(t + table::kBucketsEnd) -
                                                      Global<char*>(t + table::kBuckets)) >>
                                 2);
}

// The node of `key` or null, the game's inline find: the bucket is the key
// modulo the bucket count (a divide error when there are none, as in the
// game).
char* FindNode(std::uintptr_t t, uint32_t key)
{
    char* node = reinterpret_cast<char**>(Global<char*>(t + table::kBuckets))[key % BucketCount(t)];
    while (node && At<uint32_t>(node, table::kKey) != key)
        node = At<char*>(node, 0);
    return node;
}

struct InsertResult { // pair<iterator, bool>: 9 bytes written by the game
    char* node;
    char* table;
    uint8_t inserted;
    uint8_t padding[3];
};

// --- The profile -----------------------------------------------------------------------

struct Profile {
    uint32_t occupantClass;
    uint32_t size[3];
    uint32_t satisfaction;
    uint32_t wealth;
    uint32_t crime;
};

struct Iterator { // the game's hash iterator for erase: node, table
    char* node;
    char* table;
};

// --- Queries by area --------------------------------------------------------------------

// 0x004A3750 (slot 13): the profile of a prop type. The map's operator[]
// gives (and makes when missing) the 28 byte record.
void* __fastcall GetPropProfile(char*, void*, uint32_t type)
{
    return This<void*>(addr::SC4Uint32Map28_Index, reinterpret_cast<void*>(addr::cSC4PropManager_ProfileTable),
                       &type);
}

// 0x004A4360 (slot 12): the name of a prop type from the name table.
void* __fastcall GetPropName(char*, void*, uint32_t type)
{
    return This<void*>(addr::SC4NameHashMap_Index, reinterpret_cast<void*>(addr::cSC4PropManager_NameTable),
                       &type);
}

// 0x004A3040 (slot 15): makes the prop of a type. The class comes from the
// type's profile; the game's class factory is asked first and the three
// classes that are known by their pools are made by hand when it says no.
// The prop gets its type (slot 8 of the prop) and is set up by 0x004A2A70
// (the exemplar's properties); a failure releases it (the out pointer is
// left pointing at it).
uint8_t __fastcall CreateProp(char* self, void*, uint32_t type, void** out)
{
    if (!Virtual<uint8_t>(self, slot::kIsPropTypeValid, type))
        return 0;
    uint32_t classId = kPropClassDefault;
    if (const uint32_t cls = *Virtual<uint32_t*>(self, slot::kGetPropProfile, type))
        classId = cls;
    void* const com = Cdecl<void*>(addr::RZGetCOM);
    if (!Virtual<uint8_t>(com, 0xC, classId, kIidProp, out)) {
        void* made = nullptr;
        if (classId == kPropClassDecal) {
            if (void* const memory = Cdecl<void*>(addr::cSC4PropOccupantTerrainDecal_Allocate, std::size_t{0xA0})) {
                if (void* const object = This<void*>(addr::cSC4PropOccupantTerrainDecal_Construct, memory))
                    made = static_cast<char*>(object) + 4;
            }
        } else if (classId == kPropClassEffect) {
            if (void* const memory = Cdecl<void*>(addr::cSC4PropOccupantVisualEffect_Allocate, std::size_t{0x94})) {
                if (void* const object = This<void*>(addr::cSC4PropOccupantVisualEffect_Construct, memory))
                    made = static_cast<char*>(object) + 4;
            }
        } else {
            if (void* const memory = Cdecl<void*>(addr::cSC4PropOccupant_Allocate, std::size_t{0x68}))
                made = This<void*>(addr::cSC4PropOccupant_cSC4PropOccupant, memory);
            else
                made = nullptr;
        }
        *out = made;
        AddRef(made);
    }
    Virtual<uint32_t>(*out, prop::kSetPropType, type);
    if (This<uint8_t>(addr::cSC4PropManager_SetPropExemplar, self, *out, type)) {
        AddRef(*out);
        Release(*out);
        return 1;
    }
    Release(*out);
    return 0;
}

// Slot 31 (0x004A3150): the props on a street tile, into the list. The
// occupant manager's query by city cells takes everything in the tile with
// the street mode of the filter (4); the ones whose own position lies in
// another tile are released and dropped. Answers false whatever happens (the
// original has no other return).
uint8_t __fastcall GetStreetProps(char* self, void*, ListNode** list, int32_t x, int32_t z)
{
    void* const manager = At<void*>(self, field::kOccupants);
    if (!manager)
        return 0;
    void* const filterObject = At<void*>(self, field::kFilter);
    int32_t low[2] = {x, z};
    int32_t high[2] = {x, z};
    At<uint32_t>(filterObject, filter::kMode) = filter::kStreet;
    if (!Virtual<uint8_t>(manager, occmgr::kGetByCityCells, list, low, high, filterObject, int32_t{-1}))
        return 0;
    ListNode* const head = *list;
    void* const city = Global<void*>(addr::SC4_City);
    for (ListNode* node = head->next; node != head;) {
        void* const occupantObject = node->value;
        float position[3];
        Virtual<void>(occupantObject, occupant::kGetPosition, position);
        int32_t cellX = 0;
        int32_t cellZ = 0;
        Virtual<void>(city, 0x17C, position[0], position[2], &cellX, &cellZ);
        if (x == cellX && z == cellZ) {
            node = node->next;
        } else {
            Release(occupantObject);
            ListNode* next = nullptr;
            This<void*>(addr::SC4List_Erase, list, &next, node);
            node = next;
        }
    }
    return 0;
}

// Slot 24 (0x004A3240): the props on a lot. The lot's cell rectangle (four
// ints) goes to the occupant manager with the lot mode (2); the props whose
// position lies outside the lot's rectangle (in metres, from the cell
// corners; the far corner is one cell on) are released and dropped. A NaN
// position drops the prop.
uint8_t __fastcall GetLotPropsOfLot(char* self, void*, ListNode** list, void* lot)
{
    if (!lot)
        return 0;
    void* const manager = At<void*>(self, field::kOccupants);
    if (!manager)
        return 0;
    int32_t bounds[4];
    Virtual<void>(lot, 0x38, bounds);
    void* const filterObject = At<void*>(self, field::kFilter);
    At<uint32_t>(filterObject, filter::kMode) = filter::kLot;
    int32_t low[2] = {bounds[0], bounds[1]};
    int32_t high[2] = {bounds[2], bounds[3]};
    if (!Virtual<uint8_t>(manager, occmgr::kGetByCityCells, list, low, high, filterObject, int32_t{-1}))
        return 0;
    void* const city = Global<void*>(addr::SC4_City);
    float lowX = 0, lowZ = 0, highX = 0, highZ = 0;
    Virtual<void>(city, 0x180, bounds[0], bounds[1], &lowX, &lowZ);
    Virtual<void>(city, 0x180, bounds[2] + 1, bounds[3] + 1, &highX, &highZ);
    ListNode* const head = *list;
    for (ListNode* node = head->next; node != head;) {
        void* const occupantObject = node->value;
        float position[3];
        Virtual<void>(occupantObject, occupant::kGetPosition, position);
        // The game's x87 compares: "less" is also true for a NaN, "greater"
        // likewise.
        const bool outside = !(position[0] >= lowX) || !(position[2] >= lowZ) || !(position[0] <= highX) ||
                             !(position[2] <= highZ);
        if (!outside) {
            node = node->next;
        } else {
            Release(occupantObject);
            ListNode* next = nullptr;
            This<void*>(addr::SC4List_Erase, list, &next, node);
            node = next;
        }
    }
    return 1;
}

// Slot 35 (0x004A3770): the props of a building, appended to the list, each
// referenced. False when the building has no property holder or no prop
// list.
uint8_t __fastcall GetBuildingPropsOf(char*, void*, ListNode** list, void* building)
{
    void* const holderObject = Virtual<void*>(building, occupant::kAsPropertyHolder);
    if (!holderObject)
        return 0;
    char* propList = nullptr;
    if (!Virtual<uint8_t>(holderObject, holder::kGetAttachment, kBuildingPropList, uint32_t{1}, &propList))
        return 0;
    void** const end = At<void**>(propList, list::kVectorEnd);
    for (void** p = At<void**>(propList, list::kVector); p != end; ++p) {
        void* const propObject = *p;
        AddRef(propObject);
        ListNode* const head = *list;
        void* const allocator = ListNodeAllocator();
        ListNode* const node = Virtual<ListNode*>(allocator, 0xC, uint32_t{0xC});
        node->value = propObject;
        ListNode* const last = head->previous;
        node->next = head;
        node->previous = last;
        last->next = node;
        head->previous = node;
    }
    Release(propList);
    return 1;
}

// Slot 38 (0x004A3390): takes the props off a building. Each one that is in
// the occupant manager (flag 1) is removed from it, and each is shut down;
// the building's list is emptied (not freed) and the attachment removed.
uint8_t __fastcall RemoveBuildingPropsOf(char* self, void*, void* building)
{
    void* const holderObject = Virtual<void*>(building, occupant::kAsPropertyHolder);
    if (!holderObject)
        return 0;
    char* propList = nullptr;
    if (!Virtual<uint8_t>(holderObject, holder::kGetAttachment, kBuildingPropList, uint32_t{1}, &propList))
        return 0;
    void** const end = At<void**>(propList, list::kVectorEnd);
    for (void** p = At<void**>(propList, list::kVector); p != end; ++p) {
        void* const propObject = *p;
        if (At<void*>(self, field::kOccupants) && Virtual<uint8_t>(propObject, occupant::kIsFlagSet, uint32_t{1}))
            Virtual<uint8_t>(At<void*>(self, field::kOccupants), occmgr::kRemove, propObject, uint32_t{0},
                             uint32_t{0});
        Virtual<void>(propObject, occupant::kShutdown);
    }
    At<void*>(propList, list::kVectorEnd) = At<void*>(propList, list::kVector);
    Release(propList);
    Virtual<uint8_t>(holderObject, holder::kRemoveAttachment, kBuildingPropList, uint32_t{0});
    return 1;
}

// Gets a list through one of the manager's own slots and removes every prop
// in it through 0x004A28A0 (each removed and released); the list is freed.
// Slots 21 and 22 (by rectangle, by city cells) and 27 and 28 (by cell, by
// lot) differ only in the getter.
template <typename... Args>
void RemoveThrough(char* self, std::size_t getter, Args... args)
{
    ListNode* head = NewList();
    if (Virtual<uint8_t>(self, getter, &head, args...))
        This<void>(addr::cSC4PropManager_RemoveProps, self, &head);
    This<void>(addr::SC4List_Destroy2, &head);
}

// Slot 22 (0x004A3850): the props in a city cell rectangle.
uint8_t __fastcall RemoveCityPropsInArea(char* self, void*, void* area)
{
    RemoveThrough(self, slot::kGetCityPropsInArea, area);
    return 1;
}

// Slot 21 (0x004A38F0): the props in a rectangle (floats).
uint8_t __fastcall RemoveCityPropsInRectangle(char* self, void*, void* rectangle)
{
    RemoveThrough(self, slot::kGetCityPropsInRectangle, rectangle);
    return 1;
}

// Slot 28 (0x004A3A50): the props of a lot.
uint8_t __fastcall RemoveLotPropsOfLot(char* self, void*, void* lot)
{
    RemoveThrough(self, slot::kGetLotPropsOfLot, lot);
    return 1;
}

// Slot 27 (0x004A3AF0): the props on a lot cell.
uint8_t __fastcall RemoveLotPropsAt(char* self, void*, int32_t x, int32_t z)
{
    RemoveThrough(self, slot::kGetLotPropsAt, x, z);
    return 1;
}

// Slot 33 (0x004A3990): the props on a street tile, each taken out of the
// city by 0x004A2690 and released. Returns nothing (the original leaves EAX
// as it is).
void __fastcall RemoveStreetPropsFromTile(char* self, void*, int32_t x, int32_t z)
{
    ListNode* head = NewList();
    Virtual<uint8_t>(self, slot::kGetStreetProps, &head, x, z);
    for (ListNode* node = head->next; node != head; node = node->next) {
        void* const propObject = node->value;
        This<void>(addr::cSC4PropManager_RemoveProp, self, propObject);
        Release(propObject);
    }
    This<void>(addr::SC4List_Destroy2, &head);
}

// Slot 36 (0x004A3B90): adds a prop to a building. A prop not yet in the
// occupant manager is initialised and inserted (and given usage 3 by
// 0x004A1F00); the building's list is found (or made and attached) and the
// prop appended unless it is there; the prop's usage is set to 3.
uint8_t __fastcall AddBuildingProp(char* self, void*, void* building, void* propObject)
{
    void* const manager = At<void*>(self, field::kOccupants);
    if (!manager)
        return 0;
    if (!Virtual<uint8_t>(propObject, occupant::kIsFlagSet, uint32_t{1})) {
        if (!Virtual<uint8_t>(propObject, occupant::kInit))
            return 0;
        if (!Virtual<uint8_t>(At<void*>(self, field::kOccupants), occmgr::kInsert, propObject, uint32_t{0}))
            return 0;
        This<void>(addr::cSC4PropManager_SetUsage, self, propObject, uint32_t{3});
    }
    void* const holderObject = Virtual<void*>(building, occupant::kAsPropertyHolder);
    if (!holderObject)
        return 1;
    void* value = propObject;
    char* propList = nullptr;
    if (Virtual<uint8_t>(holderObject, holder::kGetAttachment, kBuildingPropList, uint32_t{1}, &propList)) {
        // The reference the lookup took is not given back.
        void** const end = At<void**>(propList, list::kVectorEnd);
        void** const found = Cdecl<void**>(addr::SC4_FindUint32, At<void**>(propList, list::kVector), end, &value,
                                           &value);
        if (found == end)
            This<void>(addr::SC4OccupantVector_PushBack, propList + list::kVector, &value);
    } else {
        char* made = nullptr;
        if (char* const memory = static_cast<char*>(Cdecl<void*>(addr::SC4_operator_new, std::size_t{0x14}))) {
            At<std::uintptr_t>(memory, 0x0) = addr::cBuildingPropList_Vftable;
            At<uint32_t>(memory, 0x4) = 0;
            At<uint32_t>(memory, 0x8) = 0;
            At<uint32_t>(memory, 0xC) = 0;
            At<uint32_t>(memory, list::kReferences) = 0;
            made = memory;
        }
        AddRef(made);
        This<void>(addr::SC4OccupantVector_PushBack, made + list::kVector, &value);
        Virtual<uint32_t>(holderObject, holder::kSetAttachment, kBuildingPropList, made, uint32_t{0});
        Release(made);
    }
    void* usage = nullptr;
    if (Virtual<uint8_t>(propObject, 0x0, kIidProp, &usage)) {
        Virtual<void>(usage, 0x5C, uint32_t{3});
        Release(usage);
    }
    return 1;
}

// The building's prop list's Release (vtable 0x00A8F9CC slot 2).
uint32_t __fastcall PropListRelease(char* self, void*)
{
    const uint32_t count = At<uint32_t>(self, list::kReferences);
    if (count > 1) {
        At<uint32_t>(self, list::kReferences) = count - 1;
        return count - 1;
    }
    This<void>(addr::SC4OccupantVector_Destroy, self + list::kVector);
    Cdecl<void>(addr::SC4_operator_delete, self);
    return 0;
}

// --- The text decal command -------------------------------------------------------------

// A game string (0x14 bytes, made by 0x00578390, as the original's frame
// shows), destroyed the way the original does it inline: the vtable and the
// buffer freed.
struct GameString {
    alignas(4) char bytes[0x14];
    GameString() { This<void*>(addr::SC4String_SC4String, bytes); }
    ~GameString()
    {
        At<std::uintptr_t>(bytes, 0) = addr::SC4String_Vtable;
        if (void* const buffer = At<void*>(bytes, 4))
            Cdecl<void>(addr::SC4_Free, buffer);
    }
};

// 0x004A34E0 (the message target part's slot 3). Three messages:
//   0xA6B79602: the message's two getters are called and nothing else;
//   0x06CDB65B: the message's data is an object of the service 0x00B43DC4,
//     which replaces the one at +0x24 (referenced, the old one released);
//   0x0BEAF1A2 (the TextDecal command): more than two arguments make a text
//     decal: text, x and z (and a scale, then four colour numbers, 1 each
//     when missing) at the terrain height, added to the city. Everything
//     else: nothing.
// Answers true.
uint8_t __fastcall DoMessage(char* target, void*, void* message)
{
    char* const manager = target - 4;
    switch (Virtual<uint32_t>(message, 0x10)) {
    case kMessageCityPropsChanged:
        Virtual<void*>(message, 0x28);
        Virtual<void*>(message, 0x3C);
        return 1;
    case kMessageService: {
        Virtual<void*>(message, 0x28);
        void** const data = Virtual<void**>(message, 0x3C);
        if (*data == At<void*>(target, 0x20))
            return 1;
        if (*data)
            AddRef(*data);
        if (void* const old = At<void*>(target, 0x20))
            Release(old);
        At<void*>(target, 0x20) = *data;
        return 1;
    }
    case kMessageTextDecal: {
        void* const arguments = Virtual<void*>(message, 0x28);
        const int32_t count = Virtual<int32_t>(arguments, 0xC);
        if (count <= 2)
            return 1;
        GameString text;
        Virtual<void>(Virtual<void*>(arguments, 0x14, uint32_t{0}), value::kGetString, text.bytes);
        const float x = Virtual<float>(Virtual<void*>(arguments, 0x14, uint32_t{1}), value::kGetFloat);
        const float z = Virtual<float>(Virtual<void*>(arguments, 0x14, uint32_t{2}), value::kGetFloat);
        float scale = kOne;
        if (count > 3)
            scale = Virtual<float>(Virtual<void*>(arguments, 0x14, uint32_t{3}), value::kGetFloat);
        float colour[4] = {kOne, kOne, kOne, kOne};
        if (count > 7) {
            for (uint32_t i = 0; i < 4; ++i)
                colour[i] = Virtual<float>(Virtual<void*>(arguments, 0x14, 4 + i), value::kGetFloat);
        }
        void* decal = nullptr;
        if (void* const memory = Cdecl<void*>(addr::cSC4PropOccupantTextDecal_Allocate, std::size_t{0xBC}))
            decal = This<void*>(addr::cSC4PropOccupantTextDecal_Construct, memory);
        AddRef(decal);
        Virtual<uint8_t>(manager, slot::kAddCityProp, Virtual<void*>(decal, 0xC));
        const float height = Virtual<float>(Global<void*>(addr::SC4_Terrain), 0x34, x, z);
        const float position[3] = {x, height, z};
        Virtual<void>(static_cast<char*>(decal) + 0xC, occupant::kSetPosition, position);
        Virtual<void>(decal, 0x10, text.bytes);
        Virtual<void>(decal, 0x20, colour[0], colour[1], colour[2]);
        Virtual<void>(decal, 0x28, colour[3]);
        Virtual<void>(decal, 0x18, scale);
        return 1;
    }
    default:
        return 1;
    }
}

// --- The exemplar loader ------------------------------------------------------------------

// 0x004A3DB0: stores one prop exemplar in the tables. (holder, type, isNew),
// ret 0xC; the caller always passes 1. The profile (see the header) goes in
// the profile table (a type already there has its record overwritten); the
// families (property 0x27812870) each get the type appended unless it is in
// the vector. With isNew false (never used) the type is first taken out of
// the profile table and out of every family's vector.
void __fastcall LoadPropExemplar(char*, void*, void* exemplarHolder, uint32_t type, uint32_t isNew)
{
    Profile profile = {};
    profile.occupantClass = kPropClassDefault;
    if (void* const sizeProperty = Virtual<void*>(exemplarHolder, holder::kGetProperty, kPropertySize)) {
        void* const sizeValue = Virtual<void*>(sizeProperty, property::kValue);
        const uint32_t count = Virtual<uint32_t>(sizeValue, value::kCount);
        const uint32_t* const numbers = Virtual<const uint32_t*>(sizeValue, value::kFloatArray);
        for (uint32_t i = 0; i < count && i < 3; ++i)
            profile.size[i] = numbers[i];
    }
    Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, exemplarHolder, kPropertyClass, &profile.occupantClass);
    Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, exemplarHolder, kPropertySatisfaction, &profile.satisfaction);
    uint8_t wealth = 0;
    Cdecl<uint8_t>(addr::SC4_GetPropertyUint8, exemplarHolder, kPropertyWealth, &wealth);
    profile.wealth = wealth;
    Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, exemplarHolder, kPropertyCrime, &profile.crime);
    {
        GameString name;
        if (void* const nameProperty = Virtual<void*>(exemplarHolder, holder::kGetProperty, kPropertyName))
            Virtual<void>(Virtual<void*>(nameProperty, property::kValue), value::kGetString, name.bytes);
        if (!static_cast<uint8_t>(isNew)) {
            if (char* const node = FindNode(addr::cSC4PropManager_ProfileTable, type)) {
                Iterator it = {node, reinterpret_cast<char*>(addr::cSC4PropManager_ProfileTable)};
                This<void>(addr::SC4Uint32HashMap_EraseAt, reinterpret_cast<void*>(addr::cSC4PropManager_ProfileTable),
                           &it);
            }
            // Every family's vector, bucket by bucket.
            const uint32_t buckets = BucketCount(addr::cSC4PropManager_FamilyTable);
            for (uint32_t bucket = 0; bucket < buckets; ++bucket) {
                char* node = reinterpret_cast<char**>(Global<char*>(addr::cSC4PropManager_FamilyTable +
                                                                    table::kBuckets))[bucket];
                for (; node; node = At<char*>(node, 0)) {
                    uint32_t* const begin = At<uint32_t*>(node, table::kValue);
                    uint32_t* const end = At<uint32_t*>(node, table::kValue + 4);
                    uint32_t* const found = Cdecl<uint32_t*>(addr::SC4_FindUint32, begin, end, &type, &isNew);
                    if (found != end) {
                        uint32_t* const next = found + 1;
                        if (next != end)
                            Cdecl<void*>(addr::SC4_memmove, found, next,
                                         static_cast<std::size_t>(reinterpret_cast<char*>(end) -
                                                                  reinterpret_cast<char*>(next)));
                        At<char*>(node, table::kValue + 4) -= 4;
                    }
                }
            }
        }
        *This<Profile*>(addr::SC4Uint32Map28_Index, reinterpret_cast<void*>(addr::cSC4PropManager_ProfileTable),
                        &type) = profile;
        void* const familyProperty = Virtual<void*>(exemplarHolder, holder::kGetProperty, kPropertyFamilies);
        if (!familyProperty)
            return;
        void* const familyValue = Virtual<void*>(familyProperty, property::kValue);
        const uint32_t familyCount = Virtual<uint32_t>(familyValue, value::kCount);
        const uint32_t* const families = Virtual<const uint32_t*>(familyValue, value::kUint32Array);
        for (uint32_t i = 0; i < familyCount; ++i) {
            const uint32_t family = families[i];
            char* node = FindNode(addr::cSC4PropManager_FamilyTable, family);
            if (!node) {
                // A new family: an empty vector inserted under its key. The
                // table is grown for one more element first, as the game's
                // insert helper expects.
                struct {
                    uint32_t key;
                    uint32_t vector[3];
                } entry;
                const uint32_t empty[3] = {0, 0, 0};
                entry.key = family;
                This<void*>(addr::SC4Uint32Vector_Copy, entry.vector, empty);
                This<void>(addr::SC4Uint32HashSet_Resize, reinterpret_cast<void*>(addr::cSC4PropManager_FamilyTable),
                           Global<uint32_t>(addr::cSC4PropManager_FamilyTable + table::kCount) + 1);
                InsertResult result;
                This<void>(addr::SC4Uint32VectorMap_Insert, reinterpret_cast<void*>(addr::cSC4PropManager_FamilyTable),
                           &result, &entry);
                node = FindNode(addr::cSC4PropManager_FamilyTable, family);
                if (entry.vector[0])
                    Cdecl<void>(addr::SC4_Free, reinterpret_cast<void*>(entry.vector[0]));
            }
            uint32_t* const begin = At<uint32_t*>(node, table::kValue);
            uint32_t* const end = At<uint32_t*>(node, table::kValue + 4);
            uint32_t tag = i;
            if (Cdecl<uint32_t*>(addr::SC4_FindUint32, begin, end, &type, &tag) != end)
                continue;
            if (end != At<uint32_t*>(node, table::kValue + 8)) {
                if (end)
                    *end = type;
                At<uint32_t*>(node, table::kValue + 4) = end + 1;
            } else {
                // The vector is full: room for its size again (at least
                // one more), the old elements moved, the old block freed.
                const uint32_t size = static_cast<uint32_t>(end - begin);
                const uint32_t grow = size < 1 ? 1 : size;
                const uint32_t capacity = grow + size;
                uint32_t* block = nullptr;
                if (capacity)
                    block = static_cast<uint32_t*>(Cdecl<void*>(addr::SC4_stl_allocate, std::size_t{capacity} * 4));
                uint32_t* cursor = block;
                if (end != At<uint32_t*>(node, table::kValue)) {
                    const std::size_t bytes = static_cast<std::size_t>(reinterpret_cast<char*>(end) -
                                                                       reinterpret_cast<char*>(begin));
                    Cdecl<void*>(addr::SC4_memmove, block, At<uint32_t*>(node, table::kValue), bytes);
                    cursor = reinterpret_cast<uint32_t*>(reinterpret_cast<char*>(block) + bytes);
                }
                *cursor = type;
                if (At<void*>(node, table::kValue))
                    Cdecl<void>(addr::SC4_Free, At<void*>(node, table::kValue));
                At<uint32_t*>(node, table::kValue) = block;
                At<uint32_t*>(node, table::kValue + 4) = cursor + 1;
                At<uint32_t*>(node, table::kValue + 8) = block + capacity;
            }
        }
    }
}

// 0x004A4210: one exemplar by key. The exemplar comes with the call or from
// the resource manager (referenced either way); the key is copied into the
// type table under its instance and the exemplar's properties are loaded.
// (key, exemplar), ret 8; answers false.
uint8_t __fastcall AddPropExemplar(char* self, void*, const uint32_t* key, void* exemplar)
{
    if (exemplar)
        AddRef(exemplar);
    else
        Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, key, kIidExemplar, &exemplar, uint32_t{0},
                         uint32_t{0});
    if (exemplar) {
        void* const exemplarHolder = Virtual<void*>(exemplar, 0x1C);
        uint32_t instance = key[2];
        uint32_t* const record = This<uint32_t*>(addr::SC4Uint32Map12_Index,
                                                 reinterpret_cast<void*>(addr::cSC4PropManager_TypeTable), &instance);
        record[0] = key[0];
        record[1] = key[1];
        record[2] = key[2];
        LoadPropExemplar(self, nullptr, exemplarHolder, instance, 1);
        Release(exemplar);
    }
    return 0;
}

// 0x004A42A0: the callback of the resource manager's list of exemplars
// (key, the manager as context; cdecl). Loads the exemplar and keeps it
// when its exemplar type (property 0x10) is 0x1E.
void __cdecl LoadPropExemplarCallback(const uint32_t* key, void* context)
{
    void* exemplar = nullptr;
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, key, kIidExemplar, &exemplar, uint32_t{0},
                          uint32_t{0}))
        return;
    void* const exemplarHolder = Virtual<void*>(exemplar, 0x1C);
    uint32_t kind = 0;
    if (Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, exemplarHolder, kPropertyExemplarType, &kind) &&
        kind == kPropExemplarType)
        AddPropExemplar(static_cast<char*>(context), nullptr, key, exemplar);
    Release(exemplar);
}

// 0x004A4310: every exemplar of the game's resources through the callback
// above, then 0x004A2730 (the first half's: tells the world the types are
// loaded).
void __fastcall LoadAllPropExemplars(char* self, void*)
{
    if (void* const resources = Global<void*>(addr::SC4_ResourceManager)) {
        void* exemplars = nullptr;
        if (Virtual<std::uintptr_t>(resources, 0x48, &exemplars, kExemplarType)) {
            Virtual<void>(exemplars, 0x1C, static_cast<std::uintptr_t>(addr::cSC4PropManager_LoadPropExemplarCallback),
                          self);
            Release(exemplars);
        }
    }
    This<void>(addr::cSC4PropManager_PostTypesLoaded, self);
}

// --- Init -------------------------------------------------------------------------------

// 0x004A4380 (slot 3): once per manager. The first manager of the process
// registers the clean-up with the application and loads the exemplars; every
// one registers the TextDecal command, asks for the four messages, and takes
// its references (occupant manager, resource manager, a new prop filter, lot
// manager, and the service 0x00B43DC4's object when it gives one).
uint8_t __fastcall Init(char* self, void*)
{
    if (At<uint8_t>(self, field::kInitialized))
        return 1;
    At<uint8_t>(self, field::kInitialized) = 1;
    if (!Global<uint8_t>(addr::cSC4PropManager_CleanUpRegistered)) {
        void* const application = Global<void*>(addr::SC4_Service43C94);
        Global<uint8_t>(addr::cSC4PropManager_CleanUpRegistered) = 1;
        Virtual<void>(application, 0x114, static_cast<std::uintptr_t>(addr::cSC4PropManager_CleanUp), uint32_t{0});
        LoadAllPropExemplars(self, nullptr);
    }
    Virtual<void>(Global<void*>(addr::SC4_Commands), 0x18, kMessageTextDecal,
                  reinterpret_cast<const char*>(addr::cSC4PropManager_TextDecalName), int32_t{0});
    if (void* const server = Global<void*>(addr::SC4_MessageServer)) {
        char* const target = self + 4;
        for (const uint32_t message : kNotifications)
            Virtual<uint8_t>(server, 0x14, target, message);
    }
    if (!At<void*>(self, field::kOccupants)) {
        void* const occupants = Global<void*>(addr::SC4_NetworkService);
        At<void*>(self, field::kOccupants) = occupants;
        AddRef(occupants);
    }
    if (!At<void*>(self, field::kResources)) {
        void* const resources = Global<void*>(addr::SC4_ResourceManager);
        At<void*>(self, field::kResources) = resources;
        AddRef(resources);
    }
    if (!At<void*>(self, field::kFilter)) {
        char* made = nullptr;
        if (char* const memory = static_cast<char*>(Cdecl<void*>(addr::SC4_operator_new, std::size_t{0x14}))) {
            This<void*>(addr::SC4_OccupantFilterBase, memory);
            At<std::uintptr_t>(memory, 0x0) = addr::cSC4PropFilter_Vftable;
            At<std::uintptr_t>(memory, 0x4) = addr::cSC4PropFilter_Vftable_4;
            At<uint32_t>(memory, 0xC) = 0;
            At<uint32_t>(memory, 0x10) = 0;
            made = memory;
        }
        At<void*>(self, field::kFilter) = made;
        AddRef(made);
    }
    if (!At<void*>(self, field::kLots)) {
        void* const lots = Global<void*>(addr::SC4_LotManager);
        At<void*>(self, field::kLots) = lots;
        AddRef(lots);
    }
    if (!At<void*>(self, field::kService)) {
        void* object = nullptr;
        Virtual<void>(Global<void*>(addr::SC4_Service43DC4), 0x48, &object);
        if (object)
            At<void*>(self, field::kService) = object;
    }
    return 1;
}

// --- Installation ---------------------------------------------------------------------------

bool InstallPropMgr2(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[24] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSC4PropManager_CreateProp, reinterpret_cast<void*>(&CreateProp)},
        {addr::cSC4PropManager_GetStreetProps, reinterpret_cast<void*>(&GetStreetProps)},
        {addr::cSC4PropManager_GetLotPropsOfLot, reinterpret_cast<void*>(&GetLotPropsOfLot)},
        {addr::cSC4PropManager_RemoveBuildingPropsOf, reinterpret_cast<void*>(&RemoveBuildingPropsOf)},
        {addr::cSC4PropManager_DoMessage, reinterpret_cast<void*>(&DoMessage)},
        {addr::cSC4PropManager_GetPropProfile, reinterpret_cast<void*>(&GetPropProfile)},
        {addr::cSC4PropManager_GetBuildingPropsOf, reinterpret_cast<void*>(&GetBuildingPropsOf)},
        {addr::cSC4PropManager_RemoveCityPropsInArea, reinterpret_cast<void*>(&RemoveCityPropsInArea)},
        {addr::cSC4PropManager_RemoveCityPropsInRectangle, reinterpret_cast<void*>(&RemoveCityPropsInRectangle)},
        {addr::cSC4PropManager_RemoveStreetPropsFromTile, reinterpret_cast<void*>(&RemoveStreetPropsFromTile)},
        {addr::cSC4PropManager_RemoveLotPropsOfLot, reinterpret_cast<void*>(&RemoveLotPropsOfLot)},
        {addr::cSC4PropManager_RemoveLotPropsAt, reinterpret_cast<void*>(&RemoveLotPropsAt)},
        {addr::cSC4PropManager_AddBuildingProp, reinterpret_cast<void*>(&AddBuildingProp)},
        {addr::cBuildingPropList_Release, reinterpret_cast<void*>(&PropListRelease)},
        {addr::cSC4PropManager_LoadPropExemplar, reinterpret_cast<void*>(&LoadPropExemplar)},
        {addr::cSC4PropManager_AddPropExemplar, reinterpret_cast<void*>(&AddPropExemplar)},
        {addr::cSC4PropManager_LoadPropExemplarCallback, reinterpret_cast<void*>(&LoadPropExemplarCallback)},
        {addr::cSC4PropManager_LoadAllPropExemplars, reinterpret_cast<void*>(&LoadAllPropExemplars)},
        {addr::cSC4PropManager_GetPropName, reinterpret_cast<void*>(&GetPropName)},
        {addr::cSC4PropManager_Init, reinterpret_cast<void*>(&Init)},
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

const bool g_propMgr2Registered = registry::Register({
    "simulators.propmgr.part2",
    "simulators/propmgr",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PropManager_Init,
    "Our prop manager, second half: CreateProp, the prop lists by area, lot, street and building, the text decal "
    "command, the exemplar loader and Init (src/core/propmgr_2.cpp)",
    false,
    InstallPropMgr2,
});

} // namespace
} // namespace opensc4::propmgr
