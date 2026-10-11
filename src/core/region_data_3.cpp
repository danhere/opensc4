// The region, third part (0x004A6330 to 0x004A74E0): what cSC4Region does
// with its folder. Reading the settings in `region.ini`, loading the layout
// picture `config.bmp` (or the built-in one), matching the cities found in
// the folder to the layout's tiles, deleting the region with its folder,
// moving a city to another tile, and the two functions that make a new
// region with a folder of its own. All file work goes through the game's own
// classes. Replacement, off by default:
//
//   region.data.part3: MatchCities, Delete, Create, CreateUnnamed,
//     LoadLayout, Init, LoadConfig and MoveCity.
//
// The object as this part uses it: +0x04 "initialised" (byte), +0x0C the
// name (a string), +0x20 the folder's name (a string), +0x34 the terrain
// type (0 or 1), +0x38 and +0x3C the two water settings (0 to 254), +0x40
// the list of cities without a tile (the head node), +0x44 the layout's
// tiles (a vector of {x, y, log2 of the side}), +0x50 the map from a cell to
// the city there (buckets at +0x54/+0x58, the count at +0x60; nodes {next,
// x, y, holder}, a holder's first dword being the city).
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
constexpr std::size_t kName = 0xC;        // string
constexpr std::size_t kFolderName = 0x20; // string
constexpr std::size_t kTerrainType = 0x34;
constexpr std::size_t kWaterMin = 0x38;
constexpr std::size_t kWaterMax = 0x3C;
constexpr std::size_t kCities = 0x40;
constexpr std::size_t kTiles = 0x44;
constexpr std::size_t kTilesEnd = 0x48;
constexpr std::size_t kMap = 0x50;
constexpr std::size_t kBuckets = 0x54;
constexpr std::size_t kBucketsEnd = 0x58;
constexpr std::size_t kCount = 0x60;
} // namespace field

// The key of the layout pictures in the game's resources: the instance
// below is the first of the pictures a new region picks from, the other the
// one a region without a `config.bmp` shows.
constexpr uint32_t kLayoutType = 0x856DDBAC;
constexpr uint32_t kLayoutGroup = 0x6A1EED2C;
constexpr uint32_t kFirstNewLayout = 0x6C3568C4;
constexpr uint32_t kDefaultLayout = 0x2A1F0276;
constexpr uint32_t kRegionNamesGroup = 0x6A231EAA;

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

// A string from the text resources: a string first, 0x20 bytes.
struct LocalizedText {
    GameString text;
    char rest[0xC];
};
static_assert(sizeof(LocalizedText) == 0x20);

// The game's image holder (SC4ImageLoader, 0x30 bytes); the image at +4.
struct ImageLoader {
    alignas(4) char bytes[0x30];
};

// The game's ini file reader (0x3C bytes).
struct IniFile {
    alignas(4) char bytes[0x3C];
};

struct ResourceKey {
    uint32_t type;
    uint32_t group;
    uint32_t instance;
};

struct ListNode {
    ListNode* next;
    ListNode* previous;
    void* city;
};

struct Tile {
    int32_t x;
    int32_t y;
    uint32_t log;
};

struct MapNode {
    MapNode* next;
    uint32_t x;
    uint32_t y;
    void** holder;
};

struct MapIterator {
    MapNode* node;
    char* table;
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

// The game's free function; what it leaves in EAX comes back, because three
// functions here end on it and return nothing of their own.
uint32_t Free(void* memory)
{
    return Cdecl<uint32_t>(addr::SC4_Free, memory);
}

void* Allocate(uint32_t size)
{
    return Cdecl<void*>(addr::SC4_stl_allocate, size);
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

// The inlined destructor. Returns what the original has in EAX after it:
// the buffer when there is none, else the free function's result.
uint32_t DestroyString(GameString& text)
{
    char* const buffer = text.body.begin;
    text.vtable = addr::SC4String_Vtable;
    if (buffer)
        return Free(buffer);
    return 0;
}

// Frees a string body made by one of the game's concatenating helpers.
void FreeBody(StringBody& body)
{
    if (body.begin)
        Free(body.begin);
}

// The body of a string, as the game's casts work it out.
StringBody* Body(void* text)
{
    return text ? reinterpret_cast<StringBody*>(static_cast<char*>(text) + 0x4) : nullptr;
}

uint32_t Length(const char* chars)
{
    const char* end = chars;
    while (*end)
        ++end;
    return static_cast<uint32_t>(end - chars);
}

// The string's constructor from characters, inlined in the original.
void InitFromText(GameString& text, const char* chars)
{
    char tag = 0;
    text.body.begin = nullptr;
    text.body.end = nullptr;
    text.body.capacity = nullptr;
    This<void>(addr::SC4String_FromRange, &text.body, chars, chars + Length(chars), &tag);
    text.vtable = addr::SC4String_Vtable;
    text.zero = 0;
}

// A new string holding the folder all regions are in: the application's
// nation (slot 0x88) gives it (slot 0x14, GetRegionDirectory).
void InitFromRegionsFolder(GameString& text)
{
    char tag = 0;
    void* const nation = Virtual<void*>(Global<void*>(addr::SC4_Service43C94), 0x88);
    void* const folder = Virtual<void*>(nation, 0x14);
    const uint32_t length = Virtual<uint32_t>(folder, 0x1C); // Strlen
    const char* const chars = Virtual<const char*>(folder, 0x18);
    text.body.begin = nullptr;
    text.body.end = nullptr;
    text.body.capacity = nullptr;
    This<void>(addr::SC4String_FromRange, &text.body, chars, chars + length, &tag);
    text.vtable = addr::SC4String_Vtable;
    text.zero = 0;
}

void AppendRange(StringBody& body, const char* first, const char* last)
{
    char tag = 0;
    This<void*>(addr::SC4String_AppendRange, &body, first, last, &tag);
}

// A backslash appended, as the string's push_back is inlined in the
// original: room for it is made by doubling.
void AppendBackslash(StringBody& body)
{
    char* end = body.end;
    if (end + 1 == body.capacity) {
        const uint32_t size = static_cast<uint32_t>(end - body.begin);
        const uint32_t more = size < 1 ? 1 : size;
        This<void>(addr::cSC4Advisor_F40F1B0, &body, more + size);
        end = body.end;
    }
    end[1] = 0;
    *body.end = '\\';
    ++body.end;
}

// The names the code reads through pointers in the game's data.
const char* IniFileName()
{
    return Global<const char*>(addr::cSC4Region_IniFileName);
}

const char* LayoutFileName()
{
    return Global<const char*>(addr::cSC4Region_LayoutFileName);
}

// The game's 0x004A6330: lists the cities in the region's folder and gives
// each one that is not in the region yet to the region when it sits exactly
// on a layout tile (same corner, same size). The first tile that contains
// the city's corner decides. The cities left over are appended to the list
// at +0x40.
uint32_t __fastcall MatchCities(char* self, void*)
{
    using EraseFn = ListNode**(__thiscall*)(ListNode** list, ListNode** out, ListNode* node);
    GameString path;
    InitFromRegionsFolder(path);
    StringBody* const folderName = Body(self + field::kFolderName);
    AppendRange(path.body, folderName->begin, folderName->end);
    AppendBackslash(path.body);

    void* const allocator = NodeAllocatorAt(addr::SC4List_NodeAllocator4);
    ListNode* const head = Virtual<ListNode*>(allocator, 0xC, uint32_t{0xC});
    head->next = head;
    head->previous = head;
    ListNode* list = head;
    Cdecl<void>(addr::cSC4Region_ScanFolderForCities, &list, &path);

    ListNode* node = head->next;
    while (node != head) {
        void* const city = node->city;
        if (!This<uint8_t>(addr::cSC4Region_CityCellsTaken, self, city)) {
            int32_t x;
            int32_t y;
            int32_t width;
            int32_t height;
            Virtual<void>(city, 0x14, &x, &y);          // GetPosition
            Virtual<void>(city, 0x1C, &width, &height); // GetCitySize
            Tile* tile = At<Tile*>(self, field::kTiles);
            if (tile != At<Tile*>(self, field::kTilesEnd)) {
                bool matched = false;
                do {
                    const uint32_t off = static_cast<uint32_t>(y - tile->y) | static_cast<uint32_t>(x - tile->x);
                    if (off < (uint32_t{1} << (tile->log & 0x1F))) {
                        matched = tile->x == x && tile->y == y
                            && (uint32_t{1} << (tile->log & 0x1F)) == static_cast<uint32_t>(width);
                        break;
                    }
                    ++tile;
                } while (tile != At<Tile*>(self, field::kTilesEnd));
                if (matched) {
                    Virtual<void>(self, 0x30, city); // InsertCity
                    ListNode* next;
                    node = *reinterpret_cast<EraseFn>(addr::SC4List_EraseReleasing4)(&list, &next, node);
                    continue;
                }
            }
        }
        node = node->next;
    }

    // The rest goes to the end of the region's own list.
    ListNode* const first = head->next;
    ListNode* const cities = At<ListNode*>(self, field::kCities);
    if (first != head && cities != head) {
        head->previous->next = cities;
        first->previous->next = head;
        cities->previous->next = first;
        ListNode* const last = head->previous;
        ListNode* const before = cities->previous;
        cities->previous = last;
        head->previous = first->previous;
        first->previous = before;
    }
    This<void>(addr::SC4List_Destroy4, &list);
    return DestroyString(path);
}

// The game's 0x004A65A0, slot 10 (Delete): deletes every city of the
// region (slot 14 on the first node of the cell map until the map is
// empty), then `region.ini`, `config.bmp` and the folder itself with what
// is left in it, and shuts the region down.
uint32_t __fastcall Delete(char* self, void*)
{
    while (At<uint32_t>(self, field::kCount) != 0) {
        MapNode** const buckets = At<MapNode**>(self, field::kBuckets);
        MapNode* node = nullptr;
        if (((At<char*>(self, field::kBucketsEnd) - reinterpret_cast<char*>(buckets)) >> 2) != 0) {
            uint32_t index = 0;
            MapNode** at = buckets;
            do {
                if (*at) {
                    node = buckets[index];
                    break;
                }
                ++index;
                ++at;
            } while (index < static_cast<uint32_t>((At<char*>(self, field::kBucketsEnd)
                                                    - At<char*>(self, field::kBuckets)) >> 2));
        }
        void** holder = node->holder;
        Virtual<uint8_t>(self, 0x38, &holder); // DeleteCity
    }

    char tag = 0;
    GameString folder;
    InitFromRegionsFolder(folder);
    StringBody part;
    StringBody* joined =
        Cdecl<StringBody*>(addr::SC4String_ConcatChar, &part, Body(self + field::kFolderName), int32_t{0x5C});
    AppendRange(folder.body, joined->begin, joined->end);
    FreeBody(part);

    joined = Cdecl<StringBody*>(addr::SC4String_ConcatBodyText, &part, &folder.body, IniFileName());
    GameString iniPath;
    iniPath.body.begin = nullptr;
    iniPath.body.end = nullptr;
    iniPath.body.capacity = nullptr;
    This<void>(addr::cSC4Advice_F408420, &iniPath.body, joined->begin, joined->end, &tag);
    iniPath.vtable = addr::SC4String_Vtable;
    iniPath.zero = 0;
    FreeBody(part);

    joined = Cdecl<StringBody*>(addr::SC4String_ConcatBodyText, &part, &folder.body, LayoutFileName());
    GameString layoutPath;
    layoutPath.body.begin = nullptr;
    layoutPath.body.end = nullptr;
    layoutPath.body.capacity = nullptr;
    This<void>(addr::cSC4Advice_F408420, &layoutPath.body, joined->begin, joined->end, &tag);
    layoutPath.vtable = addr::SC4String_Vtable;
    layoutPath.zero = 0;
    FreeBody(part);

    Cdecl<uint8_t>(addr::RZFile_Remove, &iniPath);
    Cdecl<uint8_t>(addr::RZFile_Remove, &layoutPath);
    Cdecl<uint8_t>(addr::RZDirectory_Remove, &folder, uint32_t{1});
    Virtual<uint8_t>(self, 0x24); // Shutdown
    DestroyString(layoutPath);
    DestroyString(iniPath);
    return DestroyString(folder);
}

// The game's 0x004A67B0 (cdecl, one argument): makes a new region named
// `name`. Its folder is the name with runs of spaces closed up; when a
// folder of that name exists, " (2)", " (3)" and so on is added until one
// does not. The folder is created and one of the built-in layout pictures,
// drawn at random, is written into it as `config.bmp`. Returns the region
// (not referenced yet).
void* __cdecl Create(void* name)
{
    char tag = 0;
    Virtual<void*>(Global<void*>(addr::SC4_Service43C94), 0x88);
    void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x64});
    void* const region = memory ? This<void*>(addr::cSC4Region_Construct, memory) : nullptr;

    GameString clean;
    InitString(clean);
    Cdecl<void>(addr::SC4String_CollapseSpaces, name, &clean);
    GameString path;
    InitString(path);
    GameString folderName;
    This<void*>(addr::SC4String_CopyBuffer, &folderName.body, &clean.body, uint32_t{0}, uint32_t{0xFFFFFFFF}, &tag);
    folderName.vtable = addr::SC4String_Vtable;
    folderName.zero = 0;

    int32_t number = 1;
    for (;;) {
        void* const nation = Virtual<void*>(Global<void*>(addr::SC4_Service43C94), 0x88);
        Virtual<void*>(&path, 0x34, Virtual<void*>(nation, 0x14)); // path = the regions' folder
        AppendRange(path.body, folderName.body.begin, folderName.body.end);
        AppendBackslash(path.body);
        if (!Cdecl<uint8_t>(addr::RZDirectory_Exists, &path))
            break;
        ++number;
        Cdecl<void>(addr::cRZBaseString_Sprintf, &folderName, "%s (%d)", clean.body.begin, number);
    }

    Virtual<uint8_t>(region, 0x10, name);        // SetName
    Virtual<uint8_t>(region, 0x18, &folderName); // SetDirectoryName
    if (Cdecl<uint8_t>(addr::RZDirectory_Create, &path)) {
        void* const resources = Global<void*>(addr::SC4_ResourceManager);
        ResourceKey key = {kLayoutType, kLayoutGroup, kFirstNewLayout};
        int32_t count = 0;
        if (Virtual<uint8_t>(resources, 0x38, &key)) { // TestForKey
            do {
                ++count;
                ++key.instance;
            } while (Virtual<uint8_t>(resources, 0x38, &key));
            if (count > 0) {
                const uint32_t pick = This<uint32_t>(addr::SC4Random_BelowLcg,
                                                     reinterpret_cast<void*>(addr::cSC4Region_Random), count);
                const char* const fileName = LayoutFileName();
                key.instance = pick + kFirstNewLayout;
                StringBody withSlash;
                StringBody withName;
                StringBody* joined =
                    Cdecl<StringBody*>(addr::SC4String_ConcatChar, &withSlash, &path.body, int32_t{0x5C});
                joined = Cdecl<StringBody*>(addr::SC4String_ConcatBodyText, &withName, joined, fileName);
                GameString layoutPath;
                This<void*>(addr::cSC4Advice_F408B90, &layoutPath, joined);
                FreeBody(withName);
                FreeBody(withSlash);
                Cdecl<int32_t>(addr::SC4Resource_WriteToFile, &key, &layoutPath);
                DestroyString(layoutPath);
            }
        }
    }
    DestroyString(folderName);
    DestroyString(path);
    DestroyString(clean);
    return region;
}

// The game's 0x004A6AC0 (no arguments): a new region with an empty name.
void* __cdecl CreateUnnamed()
{
    LocalizedText name;
    This<void*>(addr::SC4LocalizedString_ConstructBase, &name);
    void* const region = Create(&name);
    DestroyString(name.text);
    return region;
}

// The game's 0x004A6B00: loads the layout picture, `config.bmp` in the
// region's folder or, when there is none or it does not load, the built-in
// one, and has 0x004A5640 work out the tiles from it.
uint32_t __fastcall LoadLayout(char* self, void*)
{
    GameString path;
    InitFromRegionsFolder(path);
    const char* const fileName = LayoutFileName();
    StringBody withSlash;
    StringBody withName;
    StringBody* joined =
        Cdecl<StringBody*>(addr::SC4String_ConcatChar, &withSlash, Body(self + field::kFolderName), int32_t{0x5C});
    joined = Cdecl<StringBody*>(addr::SC4String_ConcatBodyText, &withName, joined, fileName);
    AppendRange(path.body, joined->begin, joined->end);
    FreeBody(withName);
    FreeBody(withSlash);

    void* image = nullptr;
    if (Cdecl<uint8_t>(addr::RZFile_Exists, &path)) {
        ImageLoader loader;
        This<void*>(addr::SC4ImageLoader_ConstructName, &loader, &path, uint32_t{9}, uint32_t{0});
        void* const loaded = At<void*>(&loader, 0x4);
        if (loaded) {
            Virtual<uint32_t>(loaded, 0x4);
            image = loaded;
        }
        This<void>(addr::SC4ImageLoader_Destruct, &loader);
    }
    if (!image) {
        ImageLoader loader;
        This<void*>(addr::SC4ImageLoader_ConstructParts, &loader, kLayoutType, kLayoutGroup, kDefaultLayout,
                    uint32_t{9}, uint32_t{0});
        void* const loaded = At<void*>(&loader, 0x4);
        if (loaded) {
            image = loaded;
            Virtual<uint32_t>(loaded, 0x4);
        }
        This<void>(addr::SC4ImageLoader_Destruct, &loader);
    }
    if (image) {
        This<void>(addr::cSC4Region_DecodeLayout, self, image);
        Virtual<uint32_t>(image, 0x8);
    }
    return DestroyString(path);
}

// The game's 0x004A6C80, slot 8 (Init): on first use reads the settings and
// the layout, matches the folder's cities and runs 0x004A51C0. Returns
// whether the region has cities on tiles.
uint32_t __fastcall Init(char* self, void*)
{
    if (!At<uint8_t>(self, field::kInitialised)) {
        At<uint8_t>(self, field::kInitialised) = 1;
        if (!Global<uint8_t>(addr::cSC4Region_AnyInitialised))
            Global<uint8_t>(addr::cSC4Region_AnyInitialised) = 1;
        Virtual<uint32_t>(self, 0x1C); // LoadConfig
        LoadLayout(self, nullptr);
        MatchCities(self, nullptr);
        This<void>(addr::cSC4Region_MoveCitiesAway, self);
    }
    return At<uint32_t>(self, field::kCount) != 0 ? 1 : 0;
}

// One number of `region.ini`'s "Regional Settings" section; the key's name
// is read through the pointer at `keyName`.
bool ReadSetting(IniFile& ini, std::uintptr_t keyName, int32_t& value)
{
    GameString key;
    InitFromText(key, Global<const char*>(keyName));
    GameString section;
    InitFromText(section, Global<const char*>(addr::cSC4Region_IniSection));
    const uint8_t found = This<uint8_t>(addr::cRZIniFile_GetInt, &ini, &section, &key, &value);
    DestroyString(section);
    DestroyString(key);
    return found != 0;
}

// The game's 0x004A6CD0, slot 7 (LoadConfig): reads `region.ini` in the
// region's folder. "Name" becomes the region's name ("#" and a number
// stands for a text of group 0x6A231EAA), "Terrain type" (0 or 1) goes to
// +0x34, "Water Min" and "Water Max" (0 to 254) to +0x38 and +0x3C. What is
// missing or out of range leaves the member as it was.
uint32_t __fastcall LoadConfig(char* self, void*)
{
    char tag = 0;
    GameString path;
    InitFromRegionsFolder(path);
    StringBody part;
    StringBody* const joined =
        Cdecl<StringBody*>(addr::SC4String_ConcatChar, &part, Body(self + field::kFolderName), int32_t{0x5C});
    AppendRange(path.body, joined->begin, joined->end);
    FreeBody(part);
    const char* const iniName = IniFileName();
    This<void*>(addr::cSC4Advisor_F40EBC0, &path.body, iniName, iniName + Length(iniName), &tag);

    IniFile ini;
    This<void*>(addr::cRZIniFile_Construct, &ini, &path);

    GameString value;
    InitString(value);
    GameString key;
    InitFromText(key, Global<const char*>(addr::cSC4Region_IniNameKey));
    GameString section;
    InitFromText(section, Global<const char*>(addr::cSC4Region_IniSection));
    const uint8_t found = This<uint8_t>(addr::cRZIniFile_GetString, &ini, &section, &key, &value);
    DestroyString(section);
    DestroyString(key);
    if (found && value.body.begin != value.body.end) {
        const char* const chars = value.body.begin;
        int32_t instance = 0;
        if (*chars == '#')
            instance = Cdecl<int32_t>(addr::SC4_TextToInt, chars + 1);
        if (instance != 0) {
            LocalizedText text;
            StringBody* const body = Body(This<void*>(addr::SC4LocalizedString_SC4LocalizedString, &text,
                                                      kRegionNamesGroup, instance, uint32_t{1}));
            This<void>(addr::SC4String_AssignRange, self + field::kName + 0x4, body->begin, body->end, &tag);
            DestroyString(text.text);
        } else {
            This<void*>(addr::cRZBaseString_Assign, self + field::kName, &value);
        }
    }

    int32_t number;
    if (ReadSetting(ini, addr::cSC4Region_IniTerrainKey, number) && number >= 0 && number < 2)
        At<int32_t>(self, field::kTerrainType) = number;
    if (ReadSetting(ini, addr::cSC4Region_IniWaterMinKey, number) && number >= 0 && number < 0xFF)
        At<int32_t>(self, field::kWaterMin) = number;
    if (ReadSetting(ini, addr::cSC4Region_IniWaterMaxKey, number) && number >= 0 && number < 0xFF)
        At<int32_t>(self, field::kWaterMax) = number;

    DestroyString(value);
    This<void>(addr::cRZIniFile_Destruct, &ini);
    return DestroyString(path);
}

// The game's 0x004A7200, slot 16 (MoveCity): moves `city` to the tile at
// (x, y), in this region's folder or (a `target` other than this region)
// through a new save file path. A city already on that tile is deleted
// first. The city gets a serial number no city of this region has, both
// regions are shut down, the save file is moved, loaded, given its number
// and position and saved again, and this region is started again. Returns
// whether that worked (1 at once when the city is on the tile already).
uint8_t __fastcall MoveCity(char* self, void*, void* target, void* city, int32_t x, int32_t y)
{
    char tag = 0;
    if (target == self)
        target = nullptr;
    void** holder = Virtual<void**>(self, 0x2C, x, y); // GetCity
    if (holder) {
        if (*holder == city)
            return 1;
        Virtual<uint8_t>(self, 0x38, &holder); // DeleteCity
    }

    GameString oldPath;
    InitString(oldPath);
    GameString newPath;
    InitString(newPath);
    GameString name;
    InitString(name);
    Virtual<void>(city, 0x7C, &oldPath); // GetCitySaveFilePath
    Virtual<void>(city, 0x84, &name);    // GetCityName
    uint32_t serial = Virtual<uint32_t>(city, 0x6C);

    // A number none of the region's cities has: on a clash a fresh one is
    // taken (counting down from a made-up start) and the walk starts over.
    MapIterator first;
    This<void>(addr::SC4HashTable_Begin, self + field::kMap, &first);
    uint32_t fresh = Cdecl<uint32_t>(addr::SC4_NewCitySerial);
    MapNode* node = first.node;
    char* table = first.table;
    while (node) {
        if (Virtual<uint32_t>(*node->holder, 0x6C) == serial) {
            serial = fresh;
            --fresh;
            node = first.node;
            table = first.table;
            continue;
        }
        if (MapNode* const next = node->next) {
            node = next;
            continue;
        }
        MapNode** const buckets = At<MapNode**>(table, 0x4);
        const uint32_t hash = ((node->x & 0xF) << 4) | (node->y & 0xF);
        uint32_t index = hash % static_cast<uint32_t>((At<char*>(table, 0x8) - reinterpret_cast<char*>(buckets)) >> 2);
        const uint32_t count = static_cast<uint32_t>((At<char*>(table, 0x8) - reinterpret_cast<char*>(buckets)) >> 2);
        node = nullptr;
        while (++index < count) {
            node = At<MapNode**>(table, 0x4)[index];
            if (node)
                break;
        }
    }

    if (target)
        Virtual<uint8_t>(target, 0x24); // Shutdown
    Virtual<uint8_t>(self, 0x24);
    uint8_t result = 1;
    bool moved;
    if (target == self) {
        This<void>(addr::SC4String_AssignRange, &newPath.body, oldPath.body.begin, oldPath.body.end, &tag);
        moved = true;
    } else {
        Cdecl<void>(addr::SC4RegionalCity_MakeSavePath, &name, serial, &newPath);
        moved = Cdecl<uint8_t>(addr::RZFile_Move, &oldPath, &newPath) != 0;
    }
    void* loaded = nullptr;
    if (moved) {
        Cdecl<uint8_t>(addr::RZFile_SetAttributes, &newPath, uint32_t{2}, uint32_t{1});
        moved = Cdecl<uint8_t>(addr::SC4RegionalCity_LoadFromFile, &newPath, &loaded) != 0;
    }
    if (moved) {
        Virtual<void>(loaded, 0x70, serial);            // SetCitySerialNumber
        Virtual<void>(loaded, 0x18, x, y, uint32_t{1}); // SetPosition
        Cdecl<uint8_t>(addr::SC4RegionalCity_SaveToFile, &newPath, loaded);
        Virtual<uint8_t>(loaded, 0x10); // Shutdown
        Virtual<uint32_t>(loaded, 0x8);
    } else {
        result = 0;
    }
    Virtual<uint32_t>(self, 0x20); // Init
    DestroyString(name);
    DestroyString(newPath);
    DestroyString(oldPath);
    return result;
}

bool InstallRegionData3(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[8] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSC4Region_MatchCities, reinterpret_cast<void*>(&MatchCities)},
        {addr::cSC4Region_Delete, reinterpret_cast<void*>(&Delete)},
        {addr::cSC4Region_Create, reinterpret_cast<void*>(&Create)},
        {addr::cSC4Region_CreateUnnamed, reinterpret_cast<void*>(&CreateUnnamed)},
        {addr::cSC4Region_LoadLayout, reinterpret_cast<void*>(&LoadLayout)},
        {addr::cSC4Region_Init, reinterpret_cast<void*>(&Init)},
        {addr::cSC4Region_LoadConfig, reinterpret_cast<void*>(&LoadConfig)},
        {addr::cSC4Region_MoveCity, reinterpret_cast<void*>(&MoveCity)},
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

const bool g_regionData3Registered = registry::Register({
    "region.data.part3",
    "region",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4Region_MoveCity,
    "Our region folder code: settings, layout, city matching, delete, create and move (docs/region.md)",
    false,
    InstallRegionData3,
});

} // namespace
} // namespace opensc4::region
