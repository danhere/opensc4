// The regional city, third part (0x004AA0E0 to 0x004ABA60): cSC4RegionalCity
// is what the region knows about one city without loading it (main vtable
// 0x00A8FB20, interface cISC4RegionalCity). This part has the import and
// export totals worked out from the city's deals, the setup of a new city
// from the new city preferences, the pass that carries the neighbours'
// connection changes into the open city (network stubs at the city's edge,
// the connections themselves through the neighbors simulator) and the
// destructor. Replacement, off by default:
//
//   region.city.part3: the five functions below.
//
// The object as this part shows it: +0x00 main vtable, +0x04 message target
// vtable, +0x08 save record vtable, +0x38 a dword taken from the new city
// preferences (+0x400), +0x3C, +0x48 and +0x54 three maps with 4-byte keys
// and values (header pointer, count), +0x90 a list, +0x94 a vector of
// 16-byte connection changes (type, x, z, a byte: 1 made, 0 removed; the
// cell is outside the city on the side of the neighbour), +0xBC the save
// file's path, +0xD0 the city's name, +0xE4, +0x10C, +0x120, +0x134 and
// +0x154 more strings, +0xF8 the mayor's name (strings are 0x14 bytes),
// +0x180 a map from a commodity (1 power, 2 water, 3 garbage) to five
// doubles (the third exported, the fourth imported; header pointer, count),
// +0x18C and +0x190 two lists of deals, +0x194 another map (header pointer,
// count).
//
// The sort helpers, the maps' and lists' out-of-line code, the strings and
// the path splitter stay the game's and are called by address.
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

namespace field {
constexpr std::size_t kVtableMessageTarget = 0x4;
constexpr std::size_t kVtableSerializable = 0x8;
constexpr std::size_t kPreference = 0x38;
constexpr std::size_t kMap3C = 0x3C;
constexpr std::size_t kMap48 = 0x48;
constexpr std::size_t kMap54 = 0x54;
constexpr std::size_t kList90 = 0x90;
constexpr std::size_t kChanges = 0x94; // vector of 16-byte records
constexpr std::size_t kSavePath = 0xBC;
constexpr std::size_t kCityName = 0xD0;
constexpr std::size_t kString0E4 = 0xE4;
constexpr std::size_t kMayorName = 0xF8;
constexpr std::size_t kString10C = 0x10C;
constexpr std::size_t kString120 = 0x120;
constexpr std::size_t kString134 = 0x134;
constexpr std::size_t kString154 = 0x154;
constexpr std::size_t kCommodities = 0x180; // map: commodity to five doubles
constexpr std::size_t kDeals18C = 0x18C;    // list
constexpr std::size_t kDeals190 = 0x190;    // list
constexpr std::size_t kMap194 = 0x194;
} // namespace field

// The new city preferences as SetupCity reads them.
namespace preferences {
constexpr std::size_t kCityName = 0x0;    // text
constexpr std::size_t kMayorName = 0x200; // text
constexpr std::size_t kValue = 0x400;
} // namespace preferences

// The value of a commodity's entry: five doubles.
namespace commodity {
constexpr std::size_t kExported = 0x10;
constexpr std::size_t kImported = 0x18;
} // namespace commodity

constexpr uint32_t kDealTypeFirst = 0x4773FFE0; // to 0x4773FFE5
constexpr uint32_t kDealFlagsIdle = 0xBC;
constexpr uint32_t kOccupantTypeNetwork = 0xC772BF98;
constexpr uint32_t kOccupantTypePowerPole = 0x2890D4DE;
constexpr uint32_t kOccupantTypePowerLine = 0x0990B2EA;
constexpr uint32_t kIidNetworkOccupant = 0xA821EF94;
constexpr uint32_t kMessageConnectionsChanged = 0x6AC284F3;

// The game's string body and string.
struct StringBody {
    char* begin;
    char* end;
    char* capacity;
};

struct GameString {
    std::uintptr_t vtable;
    StringBody body;
    uint32_t zero;
};
static_assert(sizeof(GameString) == 0x14);

struct ListNode {
    ListNode* next;
    ListNode* previous;
    void* value;
};

// A red-black tree's node as far as it is used here, and a map: the header
// node and the number of entries.
struct TreeNode {
    uint32_t colour;
    TreeNode* parent;
    TreeNode* left;
    TreeNode* right;
};

struct Map {
    TreeNode* header;
    uint32_t count;
};

// One connection change.
struct Change {
    int32_t type;
    int32_t x;
    int32_t z;
    uint8_t made;
    uint8_t unused[3];
};
static_assert(sizeof(Change) == 0x10);

struct ChangeVector {
    Change* begin;
    Change* end;
    Change* capacity;
};

// The game's vector of bits: where it starts and ends (word and bit in the
// word) and the end of its words.
struct BitVector {
    uint32_t* begin;
    int32_t beginBit;
    uint32_t* end;
    int32_t endBit;
    uint32_t* storageEnd;
};
static_assert(sizeof(BitVector) == 0x14);

// The game's cell region: a rectangle of cells and one bit a cell, in rows
// by x.
struct CellRegion {
    int32_t x0;
    int32_t z0;
    int32_t x1;
    int32_t z1;
    std::uintptr_t cellsVtable;
    uint32_t width;
    uint32_t height;
    uint32_t wordsPerRow;
    uint32_t** rows;
};
static_assert(sizeof(CellRegion) == 0x24);

// The game's path splitter, made on the stack.
struct FileSpec {
    alignas(4) char bytes[0x64];
};

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

void AddRef(void* object)
{
    Virtual<uint32_t>(object, 0x4);
}

void Release(void* object)
{
    Virtual<uint32_t>(object, 0x8);
}

void Free(void* memory)
{
    Cdecl<void>(addr::SC4_Free, memory);
}

uint32_t Length(const char* chars)
{
    const char* end = chars;
    while (*end)
        ++end;
    return static_cast<uint32_t>(end - chars);
}

// The body of a string, as the game's casts work it out.
StringBody* Body(void* text)
{
    return text ? reinterpret_cast<StringBody*>(static_cast<char*>(text) + 0x4) : nullptr;
}

// The inlined destructor of a string member.
void DestroyString(char* self, std::size_t offset)
{
    GameString& text = At<GameString>(self, offset);
    text.vtable = addr::SC4String_Vtable;
    if (text.body.begin)
        Free(text.body.begin);
}

// The inlined destructor of a map: the entries erased when there are any,
// the header freed.
void DestroyMap(Map& map, std::uintptr_t eraseTree)
{
    if (map.count != 0) {
        This<void>(eraseTree, &map, map.header->parent);
        map.header->left = map.header;
        map.header->parent = nullptr;
        map.header->right = map.header;
        map.count = 0;
    }
    if (map.header)
        Free(map.header);
}

// The word of entry `index` of a vector of bits and the bit's mask in it, as
// the game's inlined iterator arithmetic finds them.
uint32_t* BitWord(const BitVector& bits, int32_t index, uint32_t& mask)
{
    const int32_t position = bits.beginBit + index;
    uint32_t* word = bits.begin + position / 32;
    int32_t bit = position % 32;
    if (bit < 0) {
        bit += 32;
        --word;
    }
    mask = uint32_t{1} << bit;
    return word;
}

bool TestBit(const BitVector& bits, int32_t index, uint32_t& mask)
{
    const uint32_t* const word = BitWord(bits, index, mask);
    return (*word & mask) != 0;
}

void SetBit(const BitVector& bits, int32_t index, bool on)
{
    uint32_t mask;
    uint32_t* const word = BitWord(bits, index, mask);
    if (on)
        *word |= mask;
    else
        *word &= ~mask;
}

// A cell taken out of a cell region.
void ClearCell(const CellRegion& region, int32_t x, int32_t z)
{
    const uint32_t column = static_cast<uint32_t>(z - region.z0);
    uint32_t* const word = region.rows[x - region.x0] + (column >> 5);
    *word &= ~(uint32_t{1} << (column & 0x1F));
}

// An empty list of the game's second node allocator, and its end.
ListNode* NewList()
{
    auto* const head = Virtual<ListNode*>(ListNodeAllocator(), 0xC, uint32_t{0xC});
    head->next = head;
    head->previous = head;
    return head;
}

void DestroyList(ListNode*& list)
{
    This<void>(addr::SC4List_Clear2, &list);
    ListNode* const head = list;
    Virtual<void>(ListNodeAllocator(), 0x10, head);
}

// The game's 0x004AA0E0: adds one deal's amount to the commodity totals.
// Nothing for a deal without a city on the other side or with any of the
// flags 0xBC. The deal's type (0x4773FFE0 to E5: power, water, garbage; even
// and odd the two directions) picks the commodity; an even type counts as
// exported when the city on the other side is this one and as imported
// otherwise, an odd type the other way round.
void __fastcall AddDeal(char* self, void*, void* deal)
{
    void* const other = Virtual<void*>(deal, 0x1C); // GetCityOnOtherSideOfConnection
    if (!other)
        return;
    if (Virtual<uint8_t>(deal, 0x9C, kDealFlagsIdle)) // CheckFlagsOr
        return;
    const bool own = self == static_cast<char*>(other);
    const double amount = static_cast<double>(Virtual<int32_t>(deal, 0x28)); // GetAmount
    const uint32_t kind = Virtual<uint32_t>(deal, 0x20) - kDealTypeFirst;     // GetType
    if (kind > 5)
        return;
    int32_t key = static_cast<int32_t>(kind / 2) + 1;
    char* const totals = This<char*>(addr::cSC4RegionalCity_CommodityAt, self + field::kCommodities, &key);
    const bool exported = (kind & 1) == 0 ? own : !own;
    double& total = At<double>(totals, exported ? commodity::kExported : commodity::kImported);
    total = amount + total;
}

// The game's 0x004AA200 (slot 71, UpdateImportExport): sets the imported
// and exported totals of power, water and garbage to zero and adds the deals
// of both lists. Returns 1.
uint8_t __fastcall UpdateImportExport(char* self, void*)
{
    for (int32_t key = 1; key <= 3; ++key) {
        Virtual<void>(self, 0xE4, key, Global<double>(addr::SC4_DoubleZero)); // SetImported
        Virtual<void>(self, 0xDC, key, Global<double>(addr::SC4_DoubleZero)); // SetExported
    }
    for (ListNode* node = At<ListNode*>(self, field::kDeals18C)->next; node != At<ListNode*>(self, field::kDeals18C);
         node = node->next)
        AddDeal(self, nullptr, node->value);
    for (ListNode* node = At<ListNode*>(self, field::kDeals190)->next; node != At<ListNode*>(self, field::kDeals190);
         node = node->next)
        AddDeal(self, nullptr, node->value);
    return 1;
}

// A string member given the text.
void AssignText(char* self, std::size_t offset, const char* text, char* tag)
{
    StringBody copy = {};
    This<void>(addr::SC4String_FromRange, &copy, text, text + Length(text), tag);
    char* const buffer = copy.begin;
    StringBody* const target = &At<GameString>(self, offset).body;
    if (&copy != target)
        This<void>(addr::SC4String_AssignRange, target, buffer, copy.end, tag);
    if (buffer)
        Free(buffer);
}

// The game's 0x004AA4F0 (slot 25, SetupCity; its argument is the new city
// preferences, not a city): takes the city's name, the mayor's name and one
// dword from the preferences and names the save file after the city: the
// drive and folder of the present path, the city's name and ".sc4".
void __fastcall SetupCity(char* self, void*, char* settings)
{
    char tag = 0;
    AssignText(self, field::kCityName, settings + preferences::kCityName, &tag);
    AssignText(self, field::kMayorName, settings + preferences::kMayorName, &tag);
    At<uint32_t>(self, field::kPreference) = At<uint32_t>(settings, preferences::kValue);

    FileSpec spec;
    This<void*>(addr::cRZFileSpec_Construct, &spec, self + field::kSavePath);
    StringBody* const name = Body(self + field::kCityName);
    GameString folder;
    StringBody* const folderBody = Body(This<void*>(addr::cRZFileSpec_GetDriveAndDirectory, &spec, &folder));
    StringBody joined;
    StringBody* const head = Cdecl<StringBody*>(addr::cSC4Advice_F408AE0, &joined, folderBody, name);
    StringBody whole;
    const StringBody* const all = Cdecl<StringBody*>(addr::SC4String_ConcatBodyText, &whole, head,
                                                     Global<const char*>(addr::cSC4RegionalCity_CityFileExtension));
    StringBody path = {};
    This<void>(addr::cSC4Advice_F408420, &path, all->begin, all->end, &tag);
    char* const buffer = path.begin;
    StringBody* const target = &At<GameString>(self, field::kSavePath).body;
    if (&path != target)
        This<void>(addr::SC4String_AssignRange, target, buffer, path.end, &tag);
    if (buffer)
        Free(buffer);
    if (whole.begin)
        Free(whole.begin);
    if (joined.begin)
        Free(joined.begin);
    char* const folderBuffer = folder.body.begin;
    folder.vtable = addr::SC4String_Vtable;
    if (folderBuffer)
        Free(folderBuffer);
    This<void>(addr::SC4UI_F43AF80, &spec);
}

// The network pieces of the type in a cell get the side's byte of their edge
// flags replaced by `sideBits`. Returns the last piece's flags from before,
// bit 2 of each side only.
uint32_t MarkEdge(ListNode*& list, int32_t* cell, uint32_t networkType, uint32_t sideMask, uint32_t sideBits)
{
    uint32_t open = 0;
    // GetOccupantsByStandardCityCells
    Virtual<uint8_t>(Global<void*>(addr::SC4_NetworkService), 0xA4, &list, cell, cell, kOccupantTypeNetwork,
                     uint32_t{0xFFFFFFFF});
    for (ListNode* node = list->next; node != list; node = node->next) {
        void* network = nullptr;
        if (Virtual<uint8_t>(node->value, 0x0, kIidNetworkOccupant, &network) &&
            Virtual<uint8_t>(network, 0x58, networkType)) { // IsOfType
            const uint32_t flags = Virtual<uint32_t>(network, 0x98, networkType); // GetEdgeTypesFlag
            open = flags & 0x04040404;
            Virtual<void>(network, 0x94, networkType, (~sideMask & flags) | sideBits); // SetEdgeTypesFlag
        }
        if (network)
            Release(network);
    }
    Virtual<uint8_t>(Global<void*>(addr::SC4_NetworkService), 0xC0, &list); // ReleaseOccupantList
    return open;
}

// The cell next to `cell` on the first side whose bit is in `open`.
void StepCell(int32_t* cell, uint32_t open)
{
    if (open & 0x4)
        --cell[0];
    else if (open & 0x400)
        --cell[1];
    else if (open & 0x40000)
        ++cell[0];
    else if (open & 0x4000000)
        ++cell[1];
}

// MarkEdge on the cell and, when a piece there had one of the bits, on the
// cell next to it on that side.
void MarkEdgeAndNext(ListNode*& list, int32_t* cell, uint32_t networkType, uint32_t sideMask, uint32_t sideBits)
{
    const uint32_t open = MarkEdge(list, cell, networkType, sideMask, sideBits);
    if (open != 0) {
        StepCell(cell, open);
        MarkEdge(list, cell, networkType, sideMask, sideBits);
    }
}

// A network tool's slot 7 (PlaceNetworkOccupants): a line from one cell to
// another.
void DrawLine(void* tool, int32_t x0, int32_t z0, int32_t x1, int32_t z1)
{
    Virtual<uint32_t>(tool, 0x1C, x0, z0, x1, z1, uint32_t{0}, uint32_t{0}, uint32_t{1}, int32_t{-1});
}

// The game's 0x004AA6D0: carries the connection changes the neighbours left
// for this city (+0x94) into the open city, then empties the list and posts
// message 0x6AC284F3.
//
// Changes that repeat a later one (same type and cell) are dropped and the
// rest sorted. Of the made changes of highways, ground highways and avenues
// every second one in the order is drawn, and their one-way direction
// alternates. For each change the side of the city is worked out from the
// cell (x < 0 west, z < 0 north, x past the last column east, else south).
// A made change that the neighbors simulator does not have yet gets its stub
// (three cells cleared and a line of the network drawn from the edge) and
// the connection; a removed one that the simulator has loses the connection
// and the edge mark of its network piece. Ferries and unknown types only get
// the connection.
void __fastcall ApplyConnectionChanges(char* self, void*)
{
    void* const occupants = Global<void*>(addr::SC4_NetworkService);
    void* const city = Global<void*>(addr::SC4_City);
    void* const neighbors = Global<void*>(addr::SC4_MonthEndService);
    const int32_t maxX = Virtual<int32_t>(city, 0x174) - 1; // CellCountX
    const int32_t maxZ = Virtual<int32_t>(city, 0x178) - 1; // CellCountZ
    ChangeVector& changes = At<ChangeVector>(self, field::kChanges);
    uint8_t fill = 0; // the original's is not set before its first use (its address only is passed)

    // A change followed by one of the same type and cell goes.
    {
        Change* it = changes.begin;
        Change* end = changes.end;
        if (it != end) {
            Change* resume;
            do {
                Change* const next = it + 1;
                resume = next;
                if (next != end) {
                    const int32_t type = it->type;
                    bool repeated = false;
                    Change* other = next;
                    do {
                        if (other->type == type && other->x == it->x && other->z == it->z) {
                            repeated = true;
                            break;
                        }
                        ++other;
                    } while (other != end);
                    if (repeated) {
                        Change* const last = changes.end;
                        if (next != last)
                            Cdecl<Change*>(addr::SC4Vector16_Copy, next, last, it, &fill, uint32_t{0});
                        changes.end -= 1;
                        resume = it;
                    }
                }
                end = changes.end;
                it = resume;
            } while (resume != end);
        }
    }

    // The sort (the game's introsort and its two insertion sorts).
    {
        Change* const first = changes.begin;
        Change* const last = changes.end;
        if (first != last) {
            const int32_t count = static_cast<int32_t>(last - first);
            int32_t depth = 0;
            for (int32_t n = count; n != 1; n >>= 1)
                ++depth;
            const std::uintptr_t before = addr::cSC4RegionalCity_ConnectionRecordBefore;
            Cdecl<void>(addr::cSC4RegionalCity_SortIntroLoop, first, last, static_cast<Change*>(nullptr), depth * 2, before);
            if (count > 16) {
                Change* const middle = first + 16;
                Cdecl<void>(addr::cSC4RegionalCity_SortInsertionRange, first, middle, before);
                Cdecl<void>(addr::cSC4RegionalCity_SortInsertion, middle, last, static_cast<Change*>(nullptr), before);
            } else {
                Cdecl<void>(addr::cSC4RegionalCity_SortInsertionRange, first, last, before);
            }
        }
    }

    const int32_t count = static_cast<int32_t>(changes.end - changes.begin);
    BitVector drawn = {};
    This<void>(addr::SC4BitVector_Resize, &drawn, count, uint32_t{1});
    BitVector direction = {};
    This<void>(addr::SC4BitVector_Resize, &direction, count, uint32_t{0});

    {
        int32_t wide = 0;
        int32_t index = 0;
        for (Change* it = changes.begin; it != changes.end; ++it, ++index) {
            if (!it->made)
                continue;
            if (it->type != 3 && it->type != 12 && it->type != 8)
                continue;
            ++wide;
            bool on = wide % 2 == 0;
            SetBit(drawn, index, on);
            if (it->x >= 0 && it->z <= maxZ)
                on = !on;
            if (!Global<uint8_t>(addr::SC4_TuningFlag49BE9565))
                on = !on;
            SetBit(direction, index, on);
        }
    }

    int32_t index = 0;
    for (Change* it = changes.begin; it != changes.end; ++it, ++index) {
        const int32_t x = it->x;
        const int32_t z = it->z;
        int32_t edgeX;
        int32_t edgeZ;
        int32_t innerX;
        int32_t innerZ;
        uint32_t side;
        if (x < 0) {
            edgeX = 0;
            edgeZ = z;
            innerX = 2;
            innerZ = z;
            side = 0;
        } else if (z < 0) {
            edgeX = x;
            edgeZ = 0;
            innerX = x;
            innerZ = 2;
            side = 1;
        } else if (x > maxX) {
            edgeX = maxX;
            edgeZ = z;
            innerX = maxX - 2;
            innerZ = z;
            side = 2;
        } else {
            edgeX = x;
            edgeZ = maxZ;
            innerX = x;
            innerZ = maxZ - 2;
            side = 3;
        }
        const int32_t type = it->type;
        const uint32_t sideMask = uint32_t{0xFF} << (side * 8);
        const uint32_t sideBits = uint32_t{2} << (side * 8);
        uint32_t filterKind = 1;
        uint32_t networkType;
        bool known = true;
        switch (type) {
        case 1: networkType = 1; break;
        case 2: networkType = 0; break;
        case 3: networkType = 2; break;
        case 4: networkType = 5; break;
        case 5:
            networkType = 4;
            filterKind = 3;
            break;
        case 6:
            networkType = 7;
            filterKind = 2;
            break;
        case 7: networkType = 8; break;
        case 8: networkType = 6; break;
        case 10: networkType = 0xB; break;
        case 11: networkType = 9; break;
        case 12: networkType = 0xC; break;
        default:
            networkType = 0;
            known = false;
            break;
        }
        if (!known) {
            uint32_t mask;
            const bool way = TestBit(direction, index, mask);
            // CreateConnection; the last argument is a byte, pushed with the
            // rest of the register as the original has it.
            Virtual<void*>(neighbors, 0x18, type, x, z, (mask & 0xFFFFFF00) | (way ? 1u : 0u));
            continue;
        }
        void* const manager = Cdecl<void*>(addr::SC4_GetNetworkManager);
        void* const tool = Virtual<void*>(manager, 0x14, networkType, uint32_t{0}); // GetNetworkTool
        if (!tool)
            continue;
        AddRef(tool);
        if (it->made) {
            if (!Virtual<void*>(neighbors, 0x2C, it->type, it->x, it->z)) { // FindConnection
                uint32_t mask;
                if (TestBit(drawn, index, mask)) {
                    CellRegion region;
                    void* filter;
                    {
                        // The three cells from the edge inwards are cleared for
                        // the stub: cells that hold something (for a power
                        // connection: a power pole or a power line) are left
                        // alone, the rest is demolished through a filter of
                        // the kind and, for power, dezoned.
                        void* const network = Virtual<void*>(Global<void*>(addr::SC4_City), 0xA0); // GetTrafficNetwork
                        int32_t x0 = edgeX;
                        int32_t x1 = innerX;
                        int32_t z0 = edgeZ;
                        int32_t z1 = innerZ;
                        if (edgeX > innerX) {
                            x0 = innerX;
                            x1 = edgeX;
                        }
                        if (edgeZ > innerZ) {
                            z0 = innerZ;
                            z1 = edgeZ;
                        }
                        fill = 1;
                        region.x0 = x0;
                        region.z0 = z0;
                        region.x1 = x1;
                        region.z1 = z1;
                        This<void*>(addr::SC4CellRegion_ConstructCells, &region.cellsVtable, x1 - x0 + 1, z1 - z0 + 1,
                                    &fill);
                        for (int32_t cx = x0; cx <= x1; ++cx) {
                            for (int32_t cz = z0; cz <= z1; ++cz) {
                                if (it->type == 4) {
                                    void* pole = nullptr;
                                    // GetFirstOccupantByStandardCityCell
                                    Virtual<uint8_t>(occupants, 0x84, &pole, cx, cz, kOccupantTypePowerPole);
                                    if (pole) {
                                        ClearCell(region, cx, cz);
                                        Release(pole);
                                    } else {
                                        void* line = nullptr;
                                        Virtual<uint8_t>(occupants, 0x84, &line, cx, cz, kOccupantTypePowerLine);
                                        if (line) {
                                            ClearCell(region, cx, cz);
                                            Release(line);
                                        }
                                    }
                                } else if (Virtual<uint32_t>(network, 0x2C, cx, cz)) {
                                    ClearCell(region, cx, cz);
                                }
                            }
                        }
                        filter = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x10});
                        if (filter) {
                            This<void*>(addr::SC4_OccupantFilterBase, filter);
                            At<std::uintptr_t>(filter, 0x0) = addr::SC4OccupantFilterByKind_vftable;
                            At<std::uintptr_t>(filter, 0x4) = addr::SC4OccupantFilterByKind_vftable_4;
                            At<uint32_t>(filter, 0xC) = filterKind;
                            AddRef(filter);
                        }
                    }
                    // DemolishRegion
                    Virtual<uint8_t>(Global<void*>(addr::SC4_Service43D00), 0x18, uint32_t{1}, &region, uint32_t{0},
                                     uint32_t{1}, uint32_t{0}, filter, uint32_t{0}, uint32_t{0}, uint32_t{0},
                                     uint32_t{0}, uint32_t{0});
                    Virtual<uint8_t>(tool, 0xC); // Init
                    if (it->type == 4) {
                        // PlaceZone: no zone
                        Virtual<uint8_t>(Global<void*>(addr::SC4_ZoneManager), 0x2C, &region, uint32_t{0}, uint32_t{1},
                                         uint32_t{1}, uint32_t{0}, uint32_t{1}, uint32_t{0}, uint32_t{0}, uint32_t{0},
                                         uint32_t{0});
                    }
                    if (filter)
                        Release(filter);
                    // The cell region's inlined destructor.
                    region.cellsVtable = addr::SC4CellRegion_CellsVtable;
                    if (region.rows) {
                        for (uint32_t row = 0; row < region.width; ++row)
                            Cdecl<void>(addr::SC4_operator_delete, region.rows[row]);
                        Cdecl<void>(addr::SC4_operator_delete, region.rows);
                    }
                }
                {
                    const bool way = TestBit(direction, index, mask);
                    // CreateConnection
                    Virtual<void*>(neighbors, 0x18, it->type, it->x, it->z, (mask & 0xFFFFFF00) | (way ? 1u : 0u));
                }
                if (TestBit(drawn, index, mask)) {
                    DrawLine(tool, edgeX, edgeZ, innerX, innerZ);
                    // Is the network at the edge cell now?
                    bool found = false;
                    ListNode* pieces = NewList();
                    int32_t cell[2];
                    cell[0] = edgeX;
                    cell[1] = edgeZ;
                    // GetOccupantsByStandardCityCells
                    Virtual<uint8_t>(occupants, 0xA4, &pieces, &cell[0], &cell[0],
                                     it->type == 4 ? kOccupantTypePowerPole : kOccupantTypeNetwork,
                                     uint32_t{0xFFFFFFFF});
                    for (ListNode* node = pieces->next; node != pieces; node = node->next) {
                        if (it->type == 4) {
                            found = true;
                            break;
                        }
                        void* network = nullptr;
                        if (Virtual<uint8_t>(node->value, 0x0, kIidNetworkOccupant, &network) &&
                            Virtual<uint8_t>(network, 0x58, networkType)) { // IsOfType
                            found = true;
                            if (network)
                                Release(network);
                            break;
                        }
                        if (network)
                            Release(network);
                    }
                    Virtual<uint8_t>(occupants, 0xC0, &pieces); // ReleaseOccupantList
                    if (!found)
                        Virtual<void>(neighbors, 0x1C, it->type, it->x, it->z); // DeleteConnection
                    ListNode* marked = NewList();
                    MarkEdgeAndNext(marked, &cell[0], networkType, sideMask, sideBits);
                    DrawLine(tool, edgeX, edgeZ, innerX, innerZ);
                    DestroyList(marked);
                    DestroyList(pieces);
                }
            }
        } else if (Virtual<void*>(neighbors, 0x2C, it->type, it->x, it->z)) { // FindConnection
            Virtual<void>(neighbors, 0x1C, it->type, it->x, it->z);          // DeleteConnection
            ListNode* marked = NewList();
            int32_t cell[2];
            cell[0] = edgeX;
            cell[1] = edgeZ;
            MarkEdgeAndNext(marked, &cell[0], networkType, sideMask, 0);
            Virtual<uint8_t>(tool, 0xC); // Init
            DrawLine(tool, edgeX, edgeZ, edgeX, edgeZ);
            DestroyList(marked);
        }
        Virtual<uint8_t>(tool, 0x10); // Shutdown
        Release(tool);
    }

    // The vector emptied (the original's erase copies nothing).
    changes.end = changes.begin;

    void* message = Cdecl<void*>(addr::RZ_operator_new, uint32_t{0x2C});
    if (message) {
        message = This<void*>(addr::cGZMessage_cGZMessage, message);
        if (message)
            AddRef(message);
    }
    Virtual<void*>(message, 0x14, kMessageConnectionsChanged);
    Virtual<uint8_t>(Global<void*>(addr::SC4_MessageServer), 0x10, message, uint32_t{0});
    Release(message);
    if (direction.begin)
        Free(direction.begin);
    if (drawn.begin)
        Free(drawn.begin);
}

// The game's 0x004AB7F0: the destructor. The commodity map is emptied, the
// map at +0x194 destroyed, the two deal lists, the commodity map, the eight
// strings from the last to the first, the vector of changes, the list at
// +0x90 and the three small maps.
void __fastcall Destruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0) = addr::cSC4RegionalCity_Vtable;
    At<std::uintptr_t>(self, field::kVtableMessageTarget) = addr::cSC4RegionalCity_VtableMessageTarget;
    At<std::uintptr_t>(self, field::kVtableSerializable) = addr::cSC4RegionalCity_VtableSerializable;
    Map& commodities = At<Map>(self, field::kCommodities);
    if (commodities.count != 0) {
        This<void>(addr::cSC4RegionalCity_CommodityEraseTree, &commodities, commodities.header->parent);
        commodities.header->left = commodities.header;
        commodities.header->parent = nullptr;
        commodities.header->right = commodities.header;
        commodities.count = 0;
    }
    DestroyMap(At<Map>(self, field::kMap194), addr::cSC4RegionalCity_PointsEraseTree);
    This<void>(addr::SC4List_Destroy, self + field::kDeals190);
    This<void>(addr::SC4List_Destroy, self + field::kDeals18C);
    DestroyMap(commodities, addr::cSC4RegionalCity_CommodityEraseTree);
    DestroyString(self, field::kString154);
    DestroyString(self, field::kString134);
    DestroyString(self, field::kString120);
    DestroyString(self, field::kString10C);
    DestroyString(self, field::kMayorName);
    DestroyString(self, field::kString0E4);
    DestroyString(self, field::kCityName);
    DestroyString(self, field::kSavePath);
    if (At<void*>(self, field::kChanges))
        Free(At<void*>(self, field::kChanges));
    This<void>(addr::SC4List_Destroy3, self + field::kList90);
    DestroyMap(At<Map>(self, field::kMap54), addr::SC4Uint32IntMap_EraseTree);
    DestroyMap(At<Map>(self, field::kMap48), addr::SC4Uint32IntMap_EraseTree);
    DestroyMap(At<Map>(self, field::kMap3C), addr::SC4Uint32IntMap_EraseTree);
}

bool InstallRegionCity3(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[8] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSC4RegionalCity_AddDealToCommodityTotals, reinterpret_cast<void*>(&AddDeal)},
        {addr::cSC4RegionalCity_UpdateImportExport, reinterpret_cast<void*>(&UpdateImportExport)},
        {addr::cSC4RegionalCity_ApplyPreferences, reinterpret_cast<void*>(&SetupCity)},
        {addr::cSC4RegionalCity_ApplyConnectionChanges, reinterpret_cast<void*>(&ApplyConnectionChanges)},
        {addr::cSC4RegionalCity_Destruct, reinterpret_cast<void*>(&Destruct)},
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

const bool g_regionCity3Registered = registry::Register({
    "region.city.part3",
    "region",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4RegionalCity_UpdateImportExport,
    "Our regional city: import and export totals, new city setup, connection changes, destructor (docs/region.md)",
    false,
    InstallRegionCity3,
});

} // namespace
} // namespace opensc4::region
