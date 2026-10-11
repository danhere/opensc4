// The region, second part (0x004A53E0 to 0x004A6330): cSC4Region is what
// the game knows about the region it has open (main vtable 0x00A8FA40,
// interface cISC4Region, 0x64 bytes). This part has its constructor and
// deleting destructor, the decoding of the layout picture (config.bmp: one
// pixel per 1 km square; a red pixel is a small city tile, a green one the
// corner of a medium tile of 2 x 2, a blue one the corner of a large tile of
// 4 x 4), the scan of a region folder for city files, the shutdown (which
// writes region.ini) and the reset of a tutorial city from the game's own
// copy. Replacement, off by default:
//
//   region.data.part2: the six functions below.
//
// The object as this part shows it: +0x00 vtable, +0x04 initialised (byte),
// +0x08 reference count, +0x0C the region's name (a string, 0x14 bytes),
// +0x20 the region's folder name (a string), +0x34 terrain type, +0x38 water
// minimum (60 when made), +0x3C water maximum (100 when made), +0x40 the list
// of cities (a pointer to the sentinel node; nodes next, previous, city),
// +0x44 the layout tiles (a vector of 12-byte records x, y, log2 of the
// side), +0x50 the map from a cell to the holder of the city there (+0x54
// and +0x58 the bucket vector, +0x60 the number of entries; nodes next, x, y,
// holder).
//
// All file work stays the game's: the folder listing, the ini file, the
// file attributes and the city file reader are called by address.
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
constexpr std::size_t kInitialised = 0x4;
constexpr std::size_t kRefCount = 0x8;
constexpr std::size_t kName = 0xC;       // string
constexpr std::size_t kFolderName = 0x20; // string
constexpr std::size_t kTerrainType = 0x34;
constexpr std::size_t kWaterMin = 0x38;
constexpr std::size_t kWaterMax = 0x3C;
constexpr std::size_t kCities = 0x40; // list
constexpr std::size_t kTiles = 0x44;  // vector of tiles
constexpr std::size_t kCells = 0x50;  // hash map
constexpr std::size_t kCellBuckets = 0x54;
constexpr std::size_t kCellBucketsEnd = 0x58;
constexpr std::size_t kCellBucketsCapacity = 0x5C;
constexpr std::size_t kCellCount = 0x60;
} // namespace field

// The tutorial city's copy among the game's resources: type and group; the
// instance is the tutorial's ID.
constexpr uint32_t kTutorialCityType = 0x6A5B7BF5;
constexpr uint32_t kTutorialCityGroup = 0x6A5B7B57;

// A folder entry's kind for a file, and the file attribute "can be written".
constexpr uint32_t kEntryIsFile = 8;
constexpr uint32_t kAttributeWritable = 2;

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

struct CellNode {
    CellNode* next;
    int32_t x;
    int32_t y;
    void* holder;
};

struct Tile {
    int32_t x;
    int32_t y;
    int32_t sizeLog2;
};
static_assert(sizeof(Tile) == 0xC);

struct TileVector {
    Tile* begin;
    Tile* end;
    Tile* capacity;
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

struct Rect {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
};

// The game's folder listing (0x34 bytes; the number of entries at +4) and
// its ini file (0x3C bytes), both made on the stack.
struct FolderListing {
    alignas(4) char bytes[0x34];
};

struct IniFile {
    alignas(4) char bytes[0x3C];
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

void* Allocate(uint32_t size)
{
    return Cdecl<void*>(addr::SC4_stl_allocate, size);
}

uint32_t Length(const char* chars)
{
    const char* end = chars;
    while (*end)
        ++end;
    return static_cast<uint32_t>(end - chars);
}

// An empty string as the game's inlined constructor makes it.
void InitString(GameString& text)
{
    text.body.begin = nullptr;
    text.body.end = nullptr;
    text.body.capacity = nullptr;
    char* const buffer = static_cast<char*>(Allocate(8));
    text.body.begin = buffer;
    text.body.end = buffer;
    text.body.capacity = buffer + 8;
    *buffer = 0;
    text.vtable = addr::SC4String_Vtable;
    text.zero = 0;
}

// The inlined constructor from characters. (The original's length check
// before the allocation cannot fail: length + 1 is never 0.)
void InitString(GameString& text, const char* chars, uint32_t length)
{
    text.body.begin = nullptr;
    text.body.end = nullptr;
    text.body.capacity = nullptr;
    char* end = static_cast<char*>(Allocate(length + 1));
    text.body.begin = end;
    text.body.end = end;
    text.body.capacity = end + length + 1;
    if (length != 0) {
        Cdecl<void*>(addr::SC4_memmove, end, chars, length);
        end += length;
    }
    text.body.end = end;
    *end = 0;
    text.vtable = addr::SC4String_Vtable;
    text.zero = 0;
}

void InitString(GameString& text, const char* chars)
{
    InitString(text, chars, Length(chars));
}

// The inlined destructor.
void DestroyString(GameString& text)
{
    char* const buffer = text.body.begin;
    text.vtable = addr::SC4String_Vtable;
    if (buffer)
        Free(buffer);
}

// The body of a string, as the game's casts work it out.
StringBody* Body(void* text)
{
    return text ? reinterpret_cast<StringBody*>(static_cast<char*>(text) + 0x4) : nullptr;
}

// The word and the bit of entry `index` of a vector of bits, as the game's
// inlined iterator arithmetic finds them.
uint32_t* BitWord(const BitVector& bits, int32_t index, int32_t& bit)
{
    uint32_t* word = bits.begin + index / 32;
    bit = index % 32;
    if (bit < 0) {
        bit += 32;
        --word;
    }
    return word;
}

// A file that cannot be written is made writable.
void MakeWritable(GameString& path)
{
    if (!Cdecl<uint8_t>(addr::RZFile_HasAttributes, &path, kAttributeWritable))
        Cdecl<uint8_t>(addr::RZFile_SetAttributes, &path, kAttributeWritable, uint32_t{1});
}

// The game's 0x004A53E0 (slot 25): the deleting destructor.
void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    This<void>(addr::cSC4Region_Destruct, self);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// The game's 0x004A5400 (slot 22, ResetTutorialCity): finds the city whose
// tutorial ID is `id`, writes the game's copy of that tutorial city over the
// city's file, has the region read the city again, gives the fresh city the
// ID and tells the tutorial registry. Returns 1 only when the file could
// not be written, as the original does.
uint8_t __fastcall ResetTutorialCity(char* self, void*, uint32_t id)
{
    if (id == 0xFFFFFFFF)
        return 0;
    GameString path;
    InitString(path);
    // A second string of which only the buffer is left in the original.
    char* const spare = static_cast<char*>(Allocate(8));
    *spare = 0;
    int32_t written = 0;
    auto* const fresh = static_cast<ListNode*>(
        Virtual<void*>(NodeAllocatorAt(addr::SC4List_NodeAllocator4), 0xC, uint32_t{0xC}));
    fresh->next = fresh;
    fresh->previous = fresh;
    ListNode* cities = fresh;
    Virtual<uint8_t>(self, 0x44, &cities); // GetAllCities
    ListNode* const end = cities;
    for (ListNode* node = end->next; node != end; node = node->next) {
        void* const city = node->value;
        if (Virtual<uint32_t>(city, 0x104) != id) // GetTutorialGUID
            continue;
        Virtual<void>(city, 0x7C, &path); // GetCitySaveFilePath
        uint32_t key[3];
        key[0] = kTutorialCityType;
        key[1] = kTutorialCityGroup;
        key[2] = id;
        written = Cdecl<int32_t>(addr::SC4Resource_WriteToFile, &key[0], &path);
        if (written == -1)
            break;
        int32_t x;
        int32_t z;
        Virtual<void>(city, 0x14, &x, &z);               // GetPosition
        void* holder = Virtual<void*>(self, 0x2C, x, z); // GetCity
        if (holder) {
            Virtual<uint8_t>(self, 0x3C, &holder);            // ReloadCity
            Virtual<void>(At<void*>(holder, 0), 0x108, id); // SetTutorialGUID
        }
        void* const registry = Virtual<void*>(Global<void*>(addr::SC4_Service43C94), 0xC4); // GetTutorialRegistry
        This<uint8_t>(addr::cSC4TutorialRegistry_SetFlag, registry, id, uint32_t{1});
        break;
    }
    const uint8_t result = written == -1;
    This<void>(addr::SC4List_ClearReleasing4, &cities);
    ListNode* const sentinel = cities;
    Virtual<void>(NodeAllocatorAt(addr::SC4List_NodeAllocator4), 0x10, sentinel);
    Free(spare);
    DestroyString(path);
    return result;
}

// The game's 0x004A5640: reads the layout tiles from the layout picture.
// Walks the picture row by row; a pixel not yet covered whose red, green or
// blue is 0xFF (exactly one of them, counted 1, 2 and 4) starts a tile of
// that many squares a side if it fits in the picture; the tile's squares are
// marked covered and the tile (x, y, log2 of the side) is added.
void __fastcall DecodeLayout(char* self, void*, void* image)
{
    const Rect* const area = Virtual<const Rect*>(image, 0x30); // GetBufferArea
    const int32_t width = area->right - area->left;
    const int32_t height = area->bottom - area->top;
    BitVector covered = {};
    This<void>(addr::SC4BitVector_Initialize, &covered,
               static_cast<uint32_t>(height) * static_cast<uint32_t>(width));
    for (uint32_t* word = covered.begin; word != covered.storageEnd; ++word)
        *word = 0;
    TileVector& tiles = At<TileVector>(self, field::kTiles);
    // The vector emptied: its erase of everything. The fourth argument is
    // not used by the callee (the original passes its own argument's address).
    tiles.end = Cdecl<Tile*>(addr::SC4Vector12_Copy, tiles.end, tiles.end, tiles.begin, &image, uint32_t{0});
    int32_t row = 0;
    for (int32_t y = 0; y < height; ++y, row += width) {
        for (int32_t x = 0; x < width; ++x) {
            int32_t bit;
            const uint32_t* const seen = BitWord(covered, row + covered.beginBit + x, bit);
            if (*seen & (uint32_t{1} << bit))
                continue;
            uint8_t red;
            uint8_t green;
            uint8_t blue;
            const uint32_t pixel = Virtual<uint32_t>(image, 0x54, x, y); // GetPixel
            Virtual<uint8_t>(image, 0x7C, pixel, &red, &green, &blue);   // ConvertNativeValueToRGB
            const int32_t size = (red == 0xFF ? 1 : 0) + (green == 0xFF ? 2 : 0) + (blue == 0xFF ? 4 : 0);
            if (size == 0 || (size & (size - 1)) != 0)
                continue;
            if (x + size > width)
                continue;
            if (y + size > height)
                continue;
            int32_t base = row;
            for (int32_t rows = size; rows != 0; --rows, base += width) {
                for (int32_t i = 0; i < size; ++i) {
                    uint32_t* const word = BitWord(covered, base + covered.beginBit + i + x, bit);
                    *word |= uint32_t{1} << bit;
                }
            }
            Tile tile;
            tile.x = x;
            tile.y = y;
            tile.sizeLog2 = size >> 1;
            if (tiles.end != tiles.capacity) {
                if (tiles.end)
                    *tiles.end = tile;
                ++tiles.end;
            } else {
                This<void>(addr::SC4Uint32x3Vector_Insert, &tiles, tiles.end, &tile, &image, uint32_t{1},
                           uint32_t{1});
            }
        }
    }
    if (covered.begin)
        Free(covered.begin);
}

// The game's 0x004A58D0 (cdecl): lists the folder and, for every file whose
// extension is the city files' (".sc4"), reads the regional city from the
// file (made writable first when it is not), puts the city at the end of
// `list` and tells it its file's path.
void __cdecl ScanFolder(ListNode** list, void* folder)
{
    char tag = 0;
    if (!Cdecl<uint8_t>(addr::RZDirectory_Exists, folder))
        return;
    FolderListing listing;
    This<void*>(addr::cRZDirectory_Construct, &listing, folder, uint32_t{1}, uint32_t{0}, uint32_t{0});
    const uint32_t count = This<uint32_t>(addr::cRZBaseString_Data, &listing); // the number of entries, +4
    for (uint32_t i = 0; i < count; ++i) {
        char* const entry = This<char*>(addr::cRZDirectory_EntryAt, &listing, i);
        if (At<uint32_t>(entry, 0x18) != kEntryIsFile)
            continue;
        GameString extension;
        InitString(extension);
        char* const name = entry + 0x4;
        Cdecl<uint8_t>(addr::RZFile_Extension, name, &extension);
        const char* const wanted = Global<const char*>(addr::SC4_CityFileExtension);
        const uint32_t length = Length(wanted);
        bool same = static_cast<uint32_t>(extension.body.end - extension.body.begin) == length;
        for (uint32_t c = 0; same && c < length; ++c)
            same = extension.body.begin[c] == wanted[c];
        if (same) {
            StringBody* const nameBody = Body(name);
            StringBody* const folderBody = Body(folder);
            StringBody joined;
            const StringBody* const whole =
                Cdecl<StringBody*>(addr::cSC4Advice_F408AE0, &joined, folderBody, nameBody);
            GameString path;
            path.body.begin = nullptr;
            path.body.end = nullptr;
            path.body.capacity = nullptr;
            This<void>(addr::cSC4Advice_F408420, &path.body, whole->begin, whole->end, &tag);
            path.vtable = addr::SC4String_Vtable;
            path.zero = 0;
            if (joined.begin)
                Free(joined.begin);
            void* city = nullptr;
            if (Cdecl<uint8_t>(addr::SC4RegionalCity_LoadFromFile, &path, &city)) {
                MakeWritable(path);
                // A counted pointer to the city for the time of the insert.
                void* const held = city;
                if (held)
                    AddRef(held);
                ListNode* const head = *list;
                auto* const node =
                    This<ListNode*>(addr::SC4List_AllocateNodes4, list, uint32_t{1}, uint32_t{0});
                node->value = held;
                if (held)
                    AddRef(held);
                ListNode* const last = head->previous;
                node->next = head;
                node->previous = last;
                last->next = node;
                head->previous = node;
                if (held)
                    Release(held);
                Virtual<void>(city, 0x80, &path); // SetCitySaveFilePath
                Release(city);
            }
            DestroyString(path);
        }
        DestroyString(extension);
    }
    This<void>(addr::cRZDirectory_Destruct, &listing);
}

// The game's 0x004A5B30: the constructor. Two empty strings, terrain type 0,
// water 60 to 100, an empty list of cities, no tiles and an empty cell map
// asked for 100 buckets.
void* __fastcall Construct(char* self, void*)
{
    At<std::uintptr_t>(self, 0) = addr::cSC4Region_vftable;
    At<uint8_t>(self, field::kInitialised) = 0;
    At<uint32_t>(self, field::kRefCount) = 0;
    InitString(At<GameString>(self, field::kName));
    InitString(At<GameString>(self, field::kFolderName));
    At<uint32_t>(self, field::kTerrainType) = 0;
    At<uint32_t>(self, field::kWaterMin) = 0x3C;
    At<uint32_t>(self, field::kWaterMax) = 0x64;
    ListNode*& cities = At<ListNode*>(self, field::kCities);
    cities = nullptr;
    auto* const head = This<ListNode*>(addr::SC4List_AllocateNodes4, &cities, uint32_t{1}, uint32_t{0});
    head->next = head;
    head->previous = head;
    cities = head;
    TileVector& tiles = At<TileVector>(self, field::kTiles);
    tiles.begin = nullptr;
    tiles.end = nullptr;
    tiles.capacity = nullptr;
    At<uint32_t>(self, field::kCellBuckets) = 0;
    At<uint32_t>(self, field::kCellBucketsEnd) = 0;
    At<uint32_t>(self, field::kCellBucketsCapacity) = 0;
    At<uint32_t>(self, field::kCellCount) = 0;
    This<void>(addr::SC4Uint32HashSet_Construct, self + field::kCells, uint32_t{0x64});
    return self;
}

// One line of the ini file with a number.
void WriteNumber(IniFile& ini, std::uintptr_t keyText, uint32_t value)
{
    GameString key;
    InitString(key, Global<const char*>(keyText));
    GameString section;
    InitString(section, Global<const char*>(addr::cSC4Region_IniSection));
    This<uint8_t>(addr::cRZIniFile_SetNumber, &ini, &section, &key, value);
    DestroyString(section);
    DestroyString(key);
}

// The game's 0x004A5BE0 (slot 9, Shutdown): does nothing unless the region
// is initialised. Takes every city out of the cell map (RemoveCity on the
// first entry until none is left), shuts down the cities of the list and
// empties it, then writes the region's name, terrain type and water limits
// to region.ini in the region's folder (the regions' folder from the nation,
// the folder's name, a backslash, the file's name), making the file
// writable first.
uint8_t __fastcall Shutdown(char* self, void*)
{
    char tag = 0;
    if (!At<uint8_t>(self, field::kInitialised))
        return 1;
    At<uint8_t>(self, field::kInitialised) = 0;
    while (At<uint32_t>(self, field::kCellCount) != 0) {
        CellNode** const buckets = At<CellNode**>(self, field::kCellBuckets);
        CellNode* first = nullptr;
        for (uint32_t i = 0; i < static_cast<uint32_t>(At<CellNode**>(self, field::kCellBucketsEnd) -
                                                        At<CellNode**>(self, field::kCellBuckets));
             ++i) {
            if (buckets[i]) {
                first = buckets[i];
                break;
            }
        }
        void* holder = first->holder;
        Virtual<uint8_t>(self, 0x34, &holder); // RemoveCity
    }
    ListNode* const head = At<ListNode*>(self, field::kCities);
    for (ListNode* node = head->next; node != head; node = node->next)
        Virtual<uint8_t>(node->value, 0x10); // the city's Shutdown
    This<void>(addr::SC4List_ClearReleasing4, self + field::kCities);

    void* const nation = Virtual<void*>(Global<void*>(addr::SC4_Service43C94), 0x88); // GetNation
    void* const root = Virtual<void*>(nation, 0x14);
    const uint32_t rootLength = Virtual<uint32_t>(root, 0x1C); // Strlen
    const char* const rootChars = Virtual<const char*>(root, 0x18);
    GameString file;
    InitString(file, rootChars, rootLength);
    {
        StringBody slashed;
        const StringBody* const part =
            Cdecl<StringBody*>(addr::SC4String_ConcatChar, &slashed, Body(self + field::kFolderName), uint32_t{0x5C});
        This<void>(addr::SC4String_AppendRange, &file.body, part->begin, part->end, &tag);
        if (slashed.begin)
            Free(slashed.begin);
    }
    {
        const char* const name = Global<const char*>(addr::cSC4Region_IniFileName);
        This<void>(addr::cSC4Advisor_F40EBC0, &file.body, name, name + Length(name), &tag);
    }
    MakeWritable(file);
    IniFile ini;
    This<void*>(addr::cRZIniFile_Construct, &ini, &file);
    {
        GameString key;
        InitString(key, Global<const char*>(addr::cSC4Region_IniNameKey));
        GameString section;
        InitString(section, Global<const char*>(addr::cSC4Region_IniSection));
        This<uint8_t>(addr::cRZIniFile_SetString, &ini, &section, &key, self + field::kName);
        DestroyString(section);
        DestroyString(key);
    }
    WriteNumber(ini, addr::cSC4Region_IniTerrainKey, At<uint32_t>(self, field::kTerrainType));
    WriteNumber(ini, addr::cSC4Region_IniWaterMinKey, At<uint32_t>(self, field::kWaterMin));
    WriteNumber(ini, addr::cSC4Region_IniWaterMaxKey, At<uint32_t>(self, field::kWaterMax));
    This<void>(addr::cRZIniFile_Destruct, &ini);
    DestroyString(file);
    return 1;
}

bool InstallRegionData2(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[8] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSC4Region_DeletingDestructor, reinterpret_cast<void*>(&DeletingDestructor)},
        {addr::cSC4Region_ResetTutorialCity, reinterpret_cast<void*>(&ResetTutorialCity)},
        {addr::cSC4Region_DecodeLayout, reinterpret_cast<void*>(&DecodeLayout)},
        {addr::cSC4Region_ScanFolderForCities, reinterpret_cast<void*>(&ScanFolder)},
        {addr::cSC4Region_Construct, reinterpret_cast<void*>(&Construct)},
        {addr::cSC4Region_Shutdown, reinterpret_cast<void*>(&Shutdown)},
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

const bool g_regionData2Registered = registry::Register({
    "region.data.part2",
    "region",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4Region_Shutdown,
    "Our region: constructor, layout picture decode, folder scan, shutdown, tutorial city reset (docs/region.md)",
    false,
    InstallRegionData2,
});

} // namespace
} // namespace opensc4::region
