// The regional city (cSC4RegionalCity, docs/region.md), part 4: 0x004ABA60 to
// 0x004AC5B0. Replacement, off by default:
//
//   region.city.part4: the small getters and setters of the main vtable
//     (position, size, serial number, languages, the five text slots, birth
//     date, "established", world position and base elevation, hemisphere,
//     tutorial ID), the save record's class ID, the QueryInterface and
//     Release thunks of the two secondary vtables, the message handler, the
//     lookup-or-insert of the map at +0x194, the constructor and the deleting
//     destructor.
//
// The object as this part shows it (0x19C bytes are written; offsets from
// the object's start):
//
//   +0x00 main vtable (0x00A8FB20), +0x04 message target (0x00A8FB10),
//   +0x08 save record (0x00A8FAF8), +0x0C a byte, +0x10 reference count,
//   +0x14, +0x18 position in the region, +0x1C, +0x20 size (1, 1 when made),
//   +0x24, +0x28, +0x2C dwords, +0x30 a float (0.5 when made), +0x34 a byte,
//   +0x38 a dword copied from the options object at construction,
//   +0x3C, +0x48, +0x54 three trees {header, count} with 0x18-byte nodes,
//   +0x60 twelve floats (7.0 when made),
//   +0x90 a list (nodes from the allocator cached at 0x00B207DC),
//   +0x94 a vector (begin, end, capacity), +0xA0 a byte "refresh wanted",
//   +0xA4 serial number, +0xA8, +0xAC original language and country,
//   +0xB0, +0xB4 last language and country, +0xB8 birth date,
//   +0xBC file path, +0xD0 city name, +0xE4 a string, +0xF8 mayor name,
//   +0x10C a string, +0x120 description, +0x134 a string (0x14 bytes each),
//   +0x148, +0x14C world position (floats), +0x150 world base elevation,
//   +0x154 utility advisor name (string), +0x168 "established" byte,
//   +0x16C tutorial ID (-1 when made), +0x170, +0x178 two doubles,
//   +0x180 a tree {header, count} from a key to five doubles (0x40-byte
//   nodes; keys 2, 1 and 3 are put in at construction),
//   +0x18C and +0x190 two lists of counted objects (allocator 0x00B20D58),
//   +0x194 a tree {header, count} from a key to a vector of 12-byte items
//   (0x20-byte nodes).
//
// Still the game's: the two getters folded with other classes (0x004ABAD0,
// slot 12, and 0x004ABCE0, slot 65). 0x004ABE60 and 0x004ABED0 in the list
// are not functions: they are inside the message handler 0x004ABD40.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace opensc4::region {
namespace {

namespace city { // the regional city, from the object's start
constexpr std::size_t kMessageTarget = 0x4;
constexpr std::size_t kSaveRecord = 0x8;
constexpr std::size_t kX = 0x14;
constexpr std::size_t kZ = 0x18;
constexpr std::size_t kSizeX = 0x1C;
constexpr std::size_t kSizeZ = 0x20;
constexpr std::size_t kOption = 0x38;
constexpr std::size_t kTreeA = 0x3C;      // {header, count}, 0x18-byte nodes
constexpr std::size_t kTreeB = 0x48;
constexpr std::size_t kTreeC = 0x54;
constexpr std::size_t kSevens = 0x60;     // twelve floats
constexpr std::size_t kList = 0x90;       // list: pointer to the sentinel node
constexpr std::size_t kVector = 0x94;     // begin, end, capacity
constexpr std::size_t kRefresh = 0xA0;    // byte
constexpr std::size_t kSerial = 0xA4;
constexpr std::size_t kOriginalLanguage = 0xA8;
constexpr std::size_t kOriginalCountry = 0xAC;
constexpr std::size_t kLastLanguage = 0xB0;
constexpr std::size_t kLastCountry = 0xB4;
constexpr std::size_t kBirthDate = 0xB8;
constexpr std::size_t kFilePath = 0xBC;   // strings, 0x14 bytes each
constexpr std::size_t kName = 0xD0;
constexpr std::size_t kStringE4 = 0xE4;
constexpr std::size_t kMayorName = 0xF8;
constexpr std::size_t kString10C = 0x10C;
constexpr std::size_t kDescription = 0x120;
constexpr std::size_t kString134 = 0x134;
constexpr std::size_t kWorldX = 0x148;    // floats
constexpr std::size_t kWorldZ = 0x14C;
constexpr std::size_t kWorldElevation = 0x150;
constexpr std::size_t kAdvisorName = 0x154; // string
constexpr std::size_t kEstablished = 0x168; // byte
constexpr std::size_t kTutorial = 0x16C;
constexpr std::size_t kDouble170 = 0x170;
constexpr std::size_t kDouble178 = 0x178;
constexpr std::size_t kFigures = 0x180;   // tree key -> five doubles
constexpr std::size_t kKept = 0x18C;      // list of counted objects
constexpr std::size_t kPending = 0x190;   // list of counted objects
constexpr std::size_t kVectors = 0x194;   // tree key -> vector of 12-byte items
} // namespace city

constexpr uint32_t kClassId = 0x6A037C24;

// A node of the game's lists.
struct ListNode {
    ListNode* next;
    ListNode* previous;
    char* value;
};
static_assert(sizeof(ListNode) == 0xC);

// The header node of the game's trees (red-black, STL style).
struct TreeHeader {
    uint8_t colour;
    TreeHeader* parent;
    TreeHeader* left;
    TreeHeader* right;
};
static_assert(sizeof(TreeHeader) == 0x10);

// A vector of 12-byte items.
struct Vector12 {
    char* begin;
    char* end;
    char* capacity;
};
static_assert(sizeof(Vector12) == 0xC);

// A node of the tree at +0x194.
struct VectorNode {
    uint8_t colour;
    VectorNode* parent;
    VectorNode* left;
    VectorNode* right;
    int32_t key;
    Vector12 value;
};
static_assert(sizeof(VectorNode) == 0x20);
static_assert(offsetof(VectorNode, value) == 0x14);

// What the tree at +0x194 is given to insert.
struct VectorEntry {
    int32_t key;
    Vector12 value;
};
static_assert(sizeof(VectorEntry) == 0x10);

// What the tree at +0x180 is given to insert: a key and five doubles.
struct FiguresEntry {
    int32_t key;
    uint32_t padding;
    double values[5];
};
static_assert(sizeof(FiguresEntry) == 0x30);
static_assert(offsetof(FiguresEntry, values) == 0x8);

// What the insert of the tree at +0x180 answers with.
struct InsertResult {
    void* node;
    uint8_t inserted;
};
static_assert(sizeof(InsertResult) == 0x8);

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

// ---------------------------------------------------------------------------
// The small slots.

// The game's 0x004ABA60 (save record slot 5, `this` is the object + 8): the
// class ID.
uint32_t __fastcall GetClassId(char*, void*)
{
    return kClassId;
}

// The game's 0x004ABA70 (slot 5, ret 8): GetPosition.
void __fastcall GetPosition(char* self, void*, int32_t* x, int32_t* z)
{
    *x = At<int32_t>(self, city::kX);
    *z = At<int32_t>(self, city::kZ);
}

// The game's 0x004ABA90 (slot 7, ret 8): GetCitySize.
void __fastcall GetCitySize(char* self, void*, int32_t* x, int32_t* z)
{
    *x = At<int32_t>(self, city::kSizeX);
    *z = At<int32_t>(self, city::kSizeZ);
}

// The game's 0x004ABAB0 (slot 8, ret 8): SetCitySize.
void __fastcall SetCitySize(char* self, void*, int32_t x, int32_t z)
{
    At<int32_t>(self, city::kSizeX) = x;
    At<int32_t>(self, city::kSizeZ) = z;
}

// The game's 0x004ABAE0 (slot 28, ret 4): SetCitySerialNumber.
void __fastcall SetCitySerialNumber(char* self, void*, uint32_t serial)
{
    At<uint32_t>(self, city::kSerial) = serial;
}

// The game's 0x004ABAF0 (slot 29, ret 8): GetOriginalLanguageAndCountry.
uint8_t __fastcall GetOriginalLanguageAndCountry(char* self, void*, int32_t* language, int32_t* country)
{
    *language = At<int32_t>(self, city::kOriginalLanguage);
    *country = At<int32_t>(self, city::kOriginalCountry);
    return 1;
}

// The game's 0x004ABB10 (slot 30, ret 8): GetLastLanguageAndCountry.
uint8_t __fastcall GetLastLanguageAndCountry(char* self, void*, int32_t* language, int32_t* country)
{
    *language = At<int32_t>(self, city::kLastLanguage);
    *country = At<int32_t>(self, city::kLastCountry);
    return 1;
}

// The five text getters and setters end in a jump into the string's own
// slot 13 (copy from another string, 0x0090E230 for the game's string, ret 4),
// whose result comes back unchanged. A getter copies ours into the caller's
// string, a setter the caller's into ours.
void* GetText(char* self, std::size_t at, char* text)
{
    return Virtual<void*>(text, 0x34, self + at);
}

void* SetText(char* self, std::size_t at, char* text)
{
    return Virtual<void*>(self + at, 0x34, text);
}

// The game's 0x004ABB30 (slot 31, ret 4): GetCitySaveFilePath.
void* __fastcall GetCitySaveFilePath(char* self, void*, char* text)
{
    return GetText(self, city::kFilePath, text);
}

// The game's 0x004ABB50 (slot 32, ret 4): SetCitySaveFilePath.
void* __fastcall SetCitySaveFilePath(char* self, void*, char* text)
{
    return SetText(self, city::kFilePath, text);
}

// The game's 0x004ABB60 (slot 33, ret 4): GetCityName.
void* __fastcall GetCityName(char* self, void*, char* text)
{
    return GetText(self, city::kName, text);
}

// The game's 0x004ABB80 (slot 34, ret 4): SetCityName.
void* __fastcall SetCityName(char* self, void*, char* text)
{
    return SetText(self, city::kName, text);
}

// The game's 0x004ABB90 (slot 35, ret 4): GetMayorName.
void* __fastcall GetMayorName(char* self, void*, char* text)
{
    return GetText(self, city::kMayorName, text);
}

// The game's 0x004ABBB0 (slot 36, ret 4): SetMayorName.
void* __fastcall SetMayorName(char* self, void*, char* text)
{
    return SetText(self, city::kMayorName, text);
}

// The game's 0x004ABBC0 (slot 37, ret 4): GetUtilityAdvisorName.
void* __fastcall GetUtilityAdvisorName(char* self, void*, char* text)
{
    return GetText(self, city::kAdvisorName, text);
}

// The game's 0x004ABBE0 (slot 38, ret 4): SetUtilityAdvisorName.
void* __fastcall SetUtilityAdvisorName(char* self, void*, char* text)
{
    return SetText(self, city::kAdvisorName, text);
}

// The game's 0x004ABBF0 (slot 39, ret 4): GetCityDescription.
void* __fastcall GetCityDescription(char* self, void*, char* text)
{
    return GetText(self, city::kDescription, text);
}

// The game's 0x004ABC10 (slot 40, ret 4): SetCityDescription.
void* __fastcall SetCityDescription(char* self, void*, char* text)
{
    return SetText(self, city::kDescription, text);
}

// The game's 0x004ABC20 (slot 41): GetBirthDate.
uint32_t __fastcall GetBirthDate(char* self, void*)
{
    return At<uint32_t>(self, city::kBirthDate);
}

// The game's 0x004ABC30 (slot 42, ret 4): SetBirthDate.
void __fastcall SetBirthDate(char* self, void*, uint32_t date)
{
    At<uint32_t>(self, city::kBirthDate) = date;
}

// The game's 0x004ABC40 (slot 43): GetEstablished, 0 or 1 in all of EAX.
uint32_t __fastcall GetEstablished(char* self, void*)
{
    return At<uint8_t>(self, city::kEstablished) != 0 ? 1u : 0u;
}

// The game's 0x004ABC50 (slot 44, ret 4): SetEstablished.
void __fastcall SetEstablished(char* self, void*, uint8_t established)
{
    At<uint8_t>(self, city::kEstablished) = established;
}

// The game's 0x004ABC60 (slot 45, ret 8): GetWorldPosition. The two floats
// are moved as dwords.
void __fastcall GetWorldPosition(char* self, void*, uint32_t* x, uint32_t* z)
{
    *x = At<uint32_t>(self, city::kWorldX);
    *z = At<uint32_t>(self, city::kWorldZ);
}

// The game's 0x004ABC80 (slot 46, ret 8): SetWorldPosition.
void __fastcall SetWorldPosition(char* self, void*, uint32_t x, uint32_t z)
{
    At<uint32_t>(self, city::kWorldX) = x;
    At<uint32_t>(self, city::kWorldZ) = z;
}

// The game's 0x004ABCA0 (slot 47): GetWorldBaseElevation, on the x87 stack.
double __fastcall GetWorldBaseElevation(char* self, void*)
{
    return At<float>(self, city::kWorldElevation);
}

// The game's 0x004ABCB0 (slot 48, ret 4): SetWorldBaseElevation; the float
// is moved as a dword.
void __fastcall SetWorldBaseElevation(char* self, void*, uint32_t elevation)
{
    At<uint32_t>(self, city::kWorldElevation) = elevation;
}

// The game's 0x004ABCC0 (slot 49): GetWorldHemisphere. 1 when the second
// world coordinate is above zero, else 2 (also when it is not a number).
uint32_t __fastcall GetWorldHemisphere(char* self, void*)
{
    return At<float>(self, city::kWorldZ) > 0.0f ? 1u : 2u;
}

// The game's 0x004ABCF0 (slot 66, ret 4): SetTutorialGUID.
void __fastcall SetTutorialGuid(char* self, void*, uint32_t guid)
{
    At<uint32_t>(self, city::kTutorial) = guid;
}

// ---------------------------------------------------------------------------
// The thunks of the secondary vtables: `this` goes back to the object's
// start and the main vtable's function (part 1) is jumped to.

// The game's 0x004ABD00 (message target slot 0).
uint8_t __fastcall QueryInterfaceOfTarget(char* self, void*, uint32_t iid, void** object)
{
    return This<uint8_t>(addr::cSC4RegionalCity_QueryInterface, self - city::kMessageTarget, iid, object);
}

// The game's 0x004ABD10 (save record slot 0).
uint8_t __fastcall QueryInterfaceOfRecord(char* self, void*, uint32_t iid, void** object)
{
    return This<uint8_t>(addr::cSC4RegionalCity_QueryInterface, self - city::kSaveRecord, iid, object);
}

// The game's 0x004ABD20 (message target slot 2).
uint32_t __fastcall ReleaseOfTarget(char* self, void*)
{
    return This<uint32_t>(addr::cSC4RegionalCity_Release, self - city::kMessageTarget);
}

// The game's 0x004ABD30 (save record slot 2).
uint32_t __fastcall ReleaseOfRecord(char* self, void*)
{
    return This<uint32_t>(addr::cSC4RegionalCity_Release, self - city::kSaveRecord);
}

// ---------------------------------------------------------------------------
// The message handler.

// The game's 0x004ABD40 (message target slot 3, ret 4; `this` is the object
// + 4; the list entries 0x004ABE60 and 0x004ABED0 are inside it). Always
// answers 1.
//
// 0x26C63342, or 0x26D31EC3 whose first pointer is the open city: every
// object of the list at +0x190 is moved, last first, to the end of the list
// at +0x18C. The other three act only when this object is the open city's
// regional city (application slot 0x90) and a city is open: 0x26C63343 and
// 0x26D31EC1 hand the city to slot 24, the first then also to 0x004A9890 and
// 0x004A9970; 0xCA63D517 refreshes what the object keeps of the city.
uint8_t __fastcall DoMessage(char* target, void*, char* message)
{
    char* const self = target - city::kMessageTarget;
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    char* const open = Global<char*>(addr::SC4_City);

    if (type == 0x26C63342u || (type == 0x26D31EC3u && open == Virtual<char*>(message, 0x28))) {
        ListNode*& pending = At<ListNode*>(self, city::kPending);
        if (pending->next == pending)
            return 1;
        do {
            ListNode* const last = pending->previous;
            ListNode* const kept = At<ListNode*>(self, city::kKept);
            ListNode* const node =
                Virtual<ListNode*>(NodeAllocatorAt(addr::SC4List_NodeAllocator), 0xC, uint32_t{0xC});
            char* const value = last->value;
            node->value = value;
            if (value)
                Virtual<uint32_t>(value, 0x4);
            ListNode* const tail = kept->previous;
            node->previous = tail;
            node->next = kept;
            tail->next = node;
            kept->previous = node;

            ListNode* after;
            This<ListNode**>(addr::SC4List_EraseReleasing1, &pending, &after, pending->previous);
        } while (pending->next != pending);
        return 1;
    }

    char* const ours = Virtual<char*>(Global<void*>(addr::SC4_Service43C94), 0x90);

    if (type == 0x26C63343u) {
        if (ours != self || !open)
            return 1;
        Virtual<void>(self, 0x60, open);
        This<void>(addr::cSC4RegionalCity_SetAllowableGrowth, self, open);
        This<void>(addr::cSC4RegionalCity_ResetExtrapolation, self, open);
        return 1;
    }
    if (type == 0x26D31EC1u) {
        if (ours != self || !open)
            return 1;
        Virtual<void>(self, 0x60, open);
        return 1;
    }
    if (type != 0xCA63D517u || ours != self || !open)
        return 1;

    Virtual<uint32_t>(message, 0x24);
    Virtual<uint32_t>(message, 0x38);
    const bool third = Virtual<uint32_t>(message, 0x4C) != 0;
    const uint8_t state = Virtual<uint8_t>(open, 0x58);

    if (!state || third || At<uint8_t>(self, city::kRefresh)) {
        This<void>(addr::cSC4RegionalCity_RecordsClear, self + city::kVector);
        if (At<uint8_t>(self, city::kRefresh))
            This<void>(addr::cSC4RegionalCity_CollectOwnConnections, self);
    }
    if (!state || third || At<uint8_t>(self, city::kRefresh))
        This<void>(addr::cSC4RegionalCity_CollectNeighbourConnections, self);
    This<void>(addr::cSC4RegionalCity_ApplyConnectionChanges, self);
    At<uint8_t>(self, city::kRefresh) = 0;
    return 1;
}

// ---------------------------------------------------------------------------
// The tree at +0x194.

// The game's 0x004ABF90 (thiscall on the tree, ret 4): the vector kept under
// `*key`; an empty one is put in when the key is new. Answers the address of
// the vector in its node.
Vector12* __fastcall VectorsAt(char* tree, void*, const int32_t* key)
{
    VectorNode* const header = At<VectorNode*>(tree, 0);
    VectorNode* found = header;
    for (VectorNode* node = header->parent; node;) {
        if (node->key < *key) {
            node = node->right;
        } else {
            found = node;
            node = node->left;
        }
    }
    if (found == header || *key < found->key) {
        Vector12 empty;
        VectorEntry entry;
        entry.key = *key;
        empty.begin = nullptr;
        empty.end = nullptr;
        empty.capacity = nullptr;
        This<Vector12*>(addr::SC4Vector12_CopyConstruct, &entry.value, &empty);
        VectorNode* placed[2] = {};
        This<VectorNode**>(addr::cSC4RegionalCity_CommodityInsertUniqueHint, tree, &placed[0], found, &entry);
        char* const buffer = entry.value.begin;
        found = placed[0];
        if (buffer)
            Cdecl<void>(addr::SC4_Free, buffer);
    }
    return &found->value;
}

// ---------------------------------------------------------------------------
// Construction and destruction.

// An empty tree as the constructor makes it inline: a header node of `size`
// bytes that is its own leftmost and rightmost, and a count of zero.
void InitTree(char* self, std::size_t at, uint32_t size)
{
    At<TreeHeader*>(self, at) = nullptr;
    TreeHeader* const header = Cdecl<TreeHeader*>(addr::SC4_stl_allocate, size);
    At<TreeHeader*>(self, at) = header;
    At<uint32_t>(self, at + 4) = 0;
    header->colour = 0;
    header->parent = nullptr;
    header->left = header;
    header->right = header;
}

// An empty string member as the constructor makes it inline: an 8-byte
// buffer holding the terminator.
void InitString(char* self, std::size_t at)
{
    At<char*>(self, at + 0x4) = nullptr;
    At<char*>(self, at + 0x8) = nullptr;
    At<char*>(self, at + 0xC) = nullptr;
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    At<char*>(self, at + 0x4) = buffer;
    At<char*>(self, at + 0x8) = buffer;
    At<char*>(self, at + 0xC) = buffer + 8;
    *buffer = 0;
    At<std::uintptr_t>(self, at) = addr::SC4String_Vtable;
    At<uint32_t>(self, at + 0x10) = 0;
}

// An empty list member: one sentinel node from the list's own allocator
// helper, linked to itself.
void InitList(char* self, std::size_t at, std::uintptr_t allocate)
{
    At<ListNode*>(self, at) = nullptr;
    ListNode* const sentinel = This<ListNode*>(allocate, self + at, uint32_t{1}, uint32_t{0});
    sentinel->next = sentinel;
    sentinel->previous = sentinel;
    At<ListNode*>(self, at) = sentinel;
}

// One entry of five zeros under `key` for the tree at +0x180.
void AddFigures(char* self, int32_t key)
{
    FiguresEntry entry;
    entry.key = key;
    entry.padding = 0;
    for (double& value : entry.values)
        value = 0.0;
    InsertResult result;
    This<InsertResult*>(addr::cSC4RegionalCity_CommodityInsertUnique, self + city::kFigures, &result, &entry);
}

// The game's 0x004AC040 (thiscall, no arguments; answers the object). Called
// by the factory 0x004AD9A0 and directly at 0x005DC0EF and 0x007AEAD4.
char* __fastcall Construct(char* self, void*)
{
    At<std::uintptr_t>(self, city::kMessageTarget) = addr::cSC4TrafficSimulator_VtableBase4;
    At<std::uintptr_t>(self, city::kSaveRecord) = addr::SC4SimGrid_VtableBase;
    At<std::uintptr_t>(self, 0x0) = addr::cSC4RegionalCity_Vtable;
    At<std::uintptr_t>(self, city::kMessageTarget) = addr::cSC4RegionalCity_VtableMessageTarget;
    At<std::uintptr_t>(self, city::kSaveRecord) = addr::cSC4RegionalCity_VtableSerializable;
    At<uint8_t>(self, 0xC) = 0;
    At<uint32_t>(self, 0x10) = 0;
    At<int32_t>(self, city::kX) = 0;
    At<int32_t>(self, city::kZ) = 0;
    At<int32_t>(self, city::kSizeX) = 1;
    At<int32_t>(self, city::kSizeZ) = 1;
    At<uint32_t>(self, 0x24) = 0;
    At<uint32_t>(self, 0x28) = 0;
    At<uint32_t>(self, 0x2C) = 0;
    At<uint32_t>(self, 0x30) = 0x3F000000u; // 0.5f
    At<uint8_t>(self, 0x34) = 0;

    InitTree(self, city::kTreeA, 0x18);
    InitTree(self, city::kTreeB, 0x18);
    InitTree(self, city::kTreeC, 0x18);

    InitList(self, city::kList, addr::SC4List_AllocateNodes3);
    At<uint32_t>(self, city::kVector) = 0;
    At<uint32_t>(self, city::kVector + 4) = 0;
    At<uint32_t>(self, city::kVector + 8) = 0;
    At<uint8_t>(self, city::kRefresh) = 0;
    At<uint32_t>(self, city::kSerial) = 0;
    At<int32_t>(self, city::kOriginalLanguage) = 0;
    At<int32_t>(self, city::kOriginalCountry) = 0;
    At<int32_t>(self, city::kLastLanguage) = 0;
    At<int32_t>(self, city::kLastCountry) = 0;
    At<uint32_t>(self, city::kBirthDate) = 0;

    InitString(self, city::kFilePath);
    InitString(self, city::kName);
    InitString(self, city::kStringE4);
    InitString(self, city::kMayorName);
    InitString(self, city::kString10C);
    InitString(self, city::kDescription);
    InitString(self, city::kString134);
    At<uint32_t>(self, city::kWorldX) = 0;
    At<uint32_t>(self, city::kWorldZ) = 0;
    At<uint32_t>(self, city::kWorldElevation) = 0;
    InitString(self, city::kAdvisorName);
    At<double>(self, city::kDouble170) = 0.0;
    At<uint8_t>(self, city::kEstablished) = 0;
    At<double>(self, city::kDouble178) = 0.0;
    At<uint32_t>(self, city::kTutorial) = 0xFFFFFFFFu;

    InitTree(self, city::kFigures, 0x40);
    InitList(self, city::kKept, addr::SC4List_AllocateNodes1);
    InitList(self, city::kPending, addr::SC4List_AllocateNodes1);
    InitTree(self, city::kVectors, 0x20);

    for (std::size_t index = 0; index < 12; ++index)
        At<uint32_t>(self, city::kSevens + 4 * index) = 0x40E00000u; // 7.0f

    AddFigures(self, 2);
    AddFigures(self, 1);
    AddFigures(self, 3);

    At<uint32_t>(self, city::kOption) = At<uint32_t>(Global<char*>(addr::SC4_Options43CD8), 0x46C);
    return self;
}

// The game's 0x004AC590 (slot 73, ret 4): the deleting destructor.
char* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    This<void>(addr::cSC4RegionalCity_Destruct, self);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

bool InstallPart4(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSC4RegionalCity_GetGZCLSID, reinterpret_cast<void*>(&GetClassId)},
        {addr::cSC4RegionalCity_GetPosition, reinterpret_cast<void*>(&GetPosition)},
        {addr::cSC4RegionalCity_GetCitySize, reinterpret_cast<void*>(&GetCitySize)},
        {addr::cSC4RegionalCity_SetCitySize, reinterpret_cast<void*>(&SetCitySize)},
        {addr::cSC4RegionalCity_SetCitySerialNumber, reinterpret_cast<void*>(&SetCitySerialNumber)},
        {addr::cSC4RegionalCity_GetOriginalLanguageAndCountry,
         reinterpret_cast<void*>(&GetOriginalLanguageAndCountry)},
        {addr::cSC4RegionalCity_GetLastLanguageAndCountry, reinterpret_cast<void*>(&GetLastLanguageAndCountry)},
        {addr::cSC4RegionalCity_GetCitySaveFilePath, reinterpret_cast<void*>(&GetCitySaveFilePath)},
        {addr::cSC4RegionalCity_SetCitySaveFilePath, reinterpret_cast<void*>(&SetCitySaveFilePath)},
        {addr::cSC4RegionalCity_GetCityName, reinterpret_cast<void*>(&GetCityName)},
        {addr::cSC4RegionalCity_SetCityName, reinterpret_cast<void*>(&SetCityName)},
        {addr::cSC4RegionalCity_GetMayorName, reinterpret_cast<void*>(&GetMayorName)},
        {addr::cSC4RegionalCity_SetMayorName, reinterpret_cast<void*>(&SetMayorName)},
        {addr::cSC4RegionalCity_GetUtilityAdvisorName, reinterpret_cast<void*>(&GetUtilityAdvisorName)},
        {addr::cSC4RegionalCity_SetUtilityAdvisorName, reinterpret_cast<void*>(&SetUtilityAdvisorName)},
        {addr::cSC4RegionalCity_GetCityDescription, reinterpret_cast<void*>(&GetCityDescription)},
        {addr::cSC4RegionalCity_SetCityDescription, reinterpret_cast<void*>(&SetCityDescription)},
        {addr::cSC4RegionalCity_GetBirthDate, reinterpret_cast<void*>(&GetBirthDate)},
        {addr::cSC4RegionalCity_SetBirthDate, reinterpret_cast<void*>(&SetBirthDate)},
        {addr::cSC4RegionalCity_GetEstablished, reinterpret_cast<void*>(&GetEstablished)},
        {addr::cSC4RegionalCity_SetEstablished, reinterpret_cast<void*>(&SetEstablished)},
        {addr::cSC4RegionalCity_GetWorldPosition, reinterpret_cast<void*>(&GetWorldPosition)},
        {addr::cSC4RegionalCity_SetWorldPosition, reinterpret_cast<void*>(&SetWorldPosition)},
        {addr::cSC4RegionalCity_GetWorldBaseElevation, reinterpret_cast<void*>(&GetWorldBaseElevation)},
        {addr::cSC4RegionalCity_SetWorldBaseElevation, reinterpret_cast<void*>(&SetWorldBaseElevation)},
        {addr::cSC4RegionalCity_GetWorldHemisphere, reinterpret_cast<void*>(&GetWorldHemisphere)},
        {addr::cSC4RegionalCity_SetTutorialGUID, reinterpret_cast<void*>(&SetTutorialGuid)},
        {addr::cSC4RegionalCity_QueryInterfaceFromTarget, reinterpret_cast<void*>(&QueryInterfaceOfTarget)},
        {addr::cSC4RegionalCity_QueryInterfaceFromRecord, reinterpret_cast<void*>(&QueryInterfaceOfRecord)},
        {addr::cSC4RegionalCity_ReleaseFromTarget, reinterpret_cast<void*>(&ReleaseOfTarget)},
        {addr::cSC4RegionalCity_ReleaseFromRecord, reinterpret_cast<void*>(&ReleaseOfRecord)},
        {addr::cSC4RegionalCity_DoMessage, reinterpret_cast<void*>(&DoMessage)},
        {addr::cSC4RegionalCity_PointsAt, reinterpret_cast<void*>(&VectorsAt)},
        {addr::cSC4RegionalCity_Construct, reinterpret_cast<void*>(&Construct)},
        {addr::cSC4RegionalCity_DeletingDestructor, reinterpret_cast<void*>(&DeletingDestructor)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    static void* originals[count] = {};
    for (std::size_t i = 0; i < count; ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].first);
        if (!found || !hook::Install(*found, hooks[i].second, &originals[i]))
            return false;
    }
    return true;
}

const bool g_part4Registered = registry::Register({
    "region.city.part4",
    "region",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4RegionalCity_Construct,
    "Our regional city, part 4: the small slots, the message handler, the constructor (docs/region.md)",
    false,
    InstallPart4,
});

} // namespace
} // namespace opensc4::region
