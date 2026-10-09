// Our cSC4FloraSimulator (build 641), sixth part: Init and Shutdown, the save
// record, saved obliterated flora, the "Flora off" switch, the occupant
// filter, and our own object under the game's class ID (docs/flora.md).
#include "core/flora_sim.h"

#include "core/address_check.h"
#include "core/director.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cIGZUnknown.h>

#include <windows.h>

#include <cstdint>
#include <cstring>

namespace opensc4::flora_sim {
namespace {

constexpr uint32_t kFloraOccupant = 0x74758926;
constexpr uint32_t kCheatCode = 0x095BDC9C;
constexpr uint32_t kPlaceFlora = 0xAB99A64B;
constexpr uint32_t kSegmentType = 0xC9BB3FC6;
constexpr uint32_t kSegmentCountGroup = 0xC9BB3FC7;
constexpr uint32_t kSegmentOccupantGroup = 0x4A664F83;
constexpr uint32_t kVersion = 2;
constexpr uint32_t kInitMessages[] = {0x278E6003, 0x99EF1142, 0x99EF1143, 0xC53D10AA,
                                      0x26D31EC4, 0x26D31EC5, kPlaceFlora};
constexpr uint32_t kShutdownMessages[] = {0x26D31EC5, 0x26D31EC4, 0xC53D10AA, 0x278E6003,
                                          0x99EF1142, 0x99EF1143, kPlaceFlora};

using SelfFn = void(__thiscall*)(void* self);
using LoadOccupantsFn = uint8_t(__thiscall*)(void* self, void* segment);
using TextFn = void*(__thiscall*)(void* string, const char* text);
using RectFn = void(__thiscall*)(void* iterator, const int32_t* rect);
using RestartFn = void(__thiscall*)(void* iterator, int32_t mode);
using MapInsertFn = void*(__thiscall*)(std::uintptr_t map, void* result, const void* pair);
using FilterCtorFn = void*(__thiscall*)(void* filter);
using NewFn = void*(__cdecl*)(std::size_t size);
using DeleteFn = void(__cdecl*)(void* block);
using StreamValueFn = uint8_t(__cdecl*)(void* stream, uint32_t value);
using StreamDataFn = uint8_t(__cdecl*)(void* stream, void* data);
using SegmentInFn = void*(__thiscall*)(void* wrapper, void* segment, uint32_t type, uint32_t group,
                                       uint32_t instance);
using SegmentOutFn = void*(__thiscall*)(void* wrapper, void* segment, uint32_t type, uint32_t group,
                                        uint32_t instance, uint32_t flags);
using WrapperFn = uint32_t(__thiscall*)(void* wrapper);
using ListFn = void(__thiscall*)(void* list);
using AllocateNodesFn = void**(__thiscall*)(void* list, uint32_t count, uint32_t zero);
using RandomCtorFn = void*(__thiscall*)(void* random, uint32_t seed);
using AllocateFn = void*(__cdecl*)(std::size_t size);
using CtorFn = void*(__thiscall*)(void* object);

const auto Free = reinterpret_cast<DeleteFn>(addr::SC4_Free);

// The game's cIGZString over an STL-style buffer.
struct GameString {
    const void* vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};

void DropString(GameString& string)
{
    string.vtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
    if (string.begin)
        Free(string.begin);
}

// The game's 16-byte record stream over a database segment.
struct SegmentStream {
    const void* vtable;
    void* segment;
    void* stream;
    uint16_t word;
};

void TakeReference(void* self, std::size_t offset, void* object)
{
    At<void*>(self, offset) = object;
    Virtual<uint32_t>(object, 0x4);
}

void ReleaseField(void* self, std::size_t offset)
{
    if (void* const object = At<void*>(self, offset)) {
        Virtual<uint32_t>(object, 0x8);
        At<void*>(self, offset) = nullptr;
    }
}

// --- Init and Shutdown ----------------------------------------------------------

// 0x006B2960 (slot 3): the types once (FreeTypes registered with the app),
// the messages and the "PlaceFlora" command, registration with the simulator
// service under the class name (4 and 6), the services, the occupant filter,
// the city's size and the tracts a month; unless loaded, the tract walk over
// a quarter of the cells; a type 0 record (a leak when one is there, as in
// the game); the tuning; whether trees spread; the cheat code; flora saved
// by slot 24.
uint8_t __fastcall Init(void* self, void*)
{
    static bool logged = false;
    if (!logged) {
        logged = true;
        log::Write("flora: first Init (%s)", At<uint8_t>(self, field::kLoaded) ? "loaded from a save" : "new");
    }
    if (At<uint8_t>(self, field::kInitialized))
        return 1;
    At<uint8_t>(self, field::kInitialized) = 1;
    uint8_t& typesLoaded = Global<uint8_t>(addr::cSC4FloraSimulator_TypesLoaded);
    if (!typesLoaded) {
        typesLoaded = 1;
        Virtual<uint32_t>(Global<void*>(addr::SC4_Service43C94), 0x114,
                          reinterpret_cast<void*>(addr::cSC4FloraSimulator_FreeTypes), 0u);
        reinterpret_cast<SelfFn>(addr::cSC4FloraSimulator_LoadTypes)(self);
    }
    void* const target = static_cast<char*>(self) + 0x4;
    if (void* const messages = Global<void*>(addr::SC4_MessageServer)) {
        for (const uint32_t type : kInitMessages)
            Virtual<uint8_t>(messages, 0x14, target, type);
        Virtual<uint8_t>(Global<void*>(addr::SC4_Commands), 0x18, kPlaceFlora,
                         reinterpret_cast<const char*>(addr::cSC4FloraSimulator_PlaceFloraCommand), 0u);
    }
    if (!At<void*>(self, field::kSimulator)) {
        void* const simulators = Global<void*>(addr::SC4_SimulatorService);
        TakeReference(self, field::kSimulator, simulators);
        GameString name{};
        reinterpret_cast<TextFn>(addr::SC4String_FromText)(&name,
                                                           reinterpret_cast<const char*>(addr::cSC4FloraSimulator_Name));
        Virtual<uint32_t>(simulators, 0x4C, target, 4u, &name, 3u);
        Virtual<uint32_t>(simulators, 0x4C, target, 6u, &name, 3u);
        DropString(name);
    }
    if (!At<void*>(self, field::kOccupants))
        TakeReference(self, field::kOccupants, Global<void*>(addr::SC4_NetworkService));
    if (!At<void*>(self, field::kCellService)) {
        void* const service = Global<void*>(addr::SC4_Service43DAC);
        At<void*>(self, field::kCellService) = service;
        if (service)
            Virtual<uint32_t>(service, 0x4);
    }
    if (!At<void*>(self, field::kTerrain))
        TakeReference(self, field::kTerrain, Global<void*>(addr::SC4_Terrain));
    if (!At<void*>(self, field::kLots))
        TakeReference(self, field::kLots, Global<void*>(addr::SC4_LotManager));
    if (!At<void*>(self, field::kFilter)) {
        void* filter = reinterpret_cast<NewFn>(addr::SC4_operator_new)(0xC);
        if (filter) {
            reinterpret_cast<FilterCtorFn>(addr::SC4_OccupantFilterBase)(filter);
            At<std::uintptr_t>(filter, 0x0) = addr::cSC4FloraSimulator_FilterVtable;
            At<std::uintptr_t>(filter, 0x4) = addr::cSC4FloraSimulator_FilterVtable2;
        }
        TakeReference(self, field::kFilter, filter);
    }
    void* const city = Global<void*>(addr::SC4_City);
    if (city) {
        At<float>(self, field::kSizeX) = static_cast<float>(Virtual<double>(city, 0x164));
        At<float>(self, field::kSizeZ) = static_cast<float>(Virtual<double>(city, 0x168));
        At<uint32_t>(self, field::kCellsX) = Virtual<uint32_t>(city, 0x174);
        const uint32_t cellsZ = Virtual<uint32_t>(city, 0x178);
        const uint32_t cells = At<uint32_t>(self, field::kCellsX) * cellsZ;
        At<uint32_t>(self, field::kCellsZ) = cellsZ;
        At<uint32_t>(self, field::kTractsPerMonth) = cells / At<uint32_t>(self, 0x44) >> 2;
    }
    if (!At<uint8_t>(self, field::kLoaded)) {
        const int32_t rect[4] = {0, 0, At<int32_t>(self, field::kCellsX) / 2 - 1,
                                 At<int32_t>(self, field::kCellsZ) / 2 - 1};
        char* const walk = static_cast<char*>(self) + field::kWalk;
        reinterpret_cast<RectFn>(addr::TractIterator_SetBounds)(walk, rect);
        if (walk[1]) {
            walk[1] = 0;
            walk[2] = 1;
        }
        reinterpret_cast<RestartFn>(addr::TractIterator_Restart)(walk, 1);
    }
    char* const data = NewData();
    struct {
        uint32_t key;
        char* value;
    } pair{At<uint32_t>(data, 0), data};
    char result[8];
    reinterpret_cast<MapInsertFn>(addr::SC4FloraMap_Insert)(addr::cSC4FloraSimulator_TypeMap, result, &pair);
    reinterpret_cast<SelfFn>(addr::cSC4FloraSimulator_LoadTuning)(self);
    At<uint8_t>(self, field::kSeeding) = Virtual<uint8_t>(city, 0x58) ? 0 : 1;
    if (At<uint8_t>(self, 0x2E))
        At<uint8_t>(self, field::kSeeding) = 1;
    if (void* const cheats = Global<void*>(addr::SC4_CheatCodes)) {
        GameString name{};
        void* const text = reinterpret_cast<TextFn>(addr::SC4String_FromText)(
            &name, Global<const char*>(addr::cSC4FloraSimulator_CheatName));
        Virtual<uint8_t>(cheats, 0xC, kCheatCode, text);
        DropString(name);
        Virtual<uint8_t>(cheats, 0x1C, target, kCheatCode);
    }
    void* const owner = Virtual<void*>(Global<void*>(addr::SC4_Service43C94), 0x9C);
    if (void* const segment = At<void*>(owner, 0x204))
        reinterpret_cast<LoadOccupantsFn>(addr::cSC4FloraSimulator_LoadOccupants)(self, segment);
    return 1;
}

// 0x006AFDA0 (slot 4): dying trees removed, the cheat code dropped, the
// services released, unregistered from the simulator service, the messages
// and the command dropped.
uint8_t __fastcall Shutdown(void* self, void*)
{
    static bool logged = false;
    FirstCall("Shutdown", logged);
    if (!At<uint8_t>(self, field::kInitialized))
        return 1;
    At<uint8_t>(self, field::kInitialized) = 0;
    void** const dying = At<void**>(self, field::kDying);
    if (dying[0] != dying)
        Virtual<uint32_t>(At<void*>(self, field::kOccupants), 0xC0, static_cast<char*>(self) + field::kDying);
    void* const target = static_cast<char*>(self) + 0x4;
    if (void* const cheats = Global<void*>(addr::SC4_CheatCodes)) {
        Virtual<uint8_t>(cheats, 0x20, target, kCheatCode);
        Virtual<uint8_t>(cheats, 0x10, kCheatCode);
    }
    ReleaseField(self, field::kFilter);
    ReleaseField(self, field::kLots);
    ReleaseField(self, field::kTerrain);
    ReleaseField(self, field::kOccupants);
    ReleaseField(self, field::kCellService);
    if (void* const simulators = At<void*>(self, field::kSimulator)) {
        Virtual<uint32_t>(simulators, 0x50, target);
        Virtual<uint32_t>(At<void*>(self, field::kSimulator), 0x8);
        At<void*>(self, field::kSimulator) = nullptr;
    }
    if (void* const messages = Global<void*>(addr::SC4_MessageServer)) {
        for (const uint32_t type : kShutdownMessages)
            Virtual<uint8_t>(messages, 0x18, target, type);
        Virtual<uint8_t>(Global<void*>(addr::SC4_Commands), 0x24, kPlaceFlora);
    }
    return 1;
}

// 0x006B0660: flora saved by slot 24 back into the occupant manager.
uint8_t __fastcall LoadOccupants(void* self, void*, void* segment)
{
    static bool logged = false;
    FirstCall("LoadOccupants", logged);
    const auto In = reinterpret_cast<SegmentInFn>(addr::SC4SegmentIStream_SC4SegmentIStream);
    const auto IsOpen = reinterpret_cast<WrapperFn>(addr::SC4SegmentStream_IsOpen);
    const auto Close = reinterpret_cast<WrapperFn>(addr::SC4SegmentIStream_Close);
    uint32_t count = 0;
    SegmentStream stream;
    In(&stream, segment, kSegmentType, kSegmentCountGroup, 1);
    if (IsOpen(&stream))
        Virtual<uint8_t>(stream.stream, 0x24, &count);
    Close(&stream);
    if (!At<void*>(self, field::kOccupants))
        return 0;
    for (uint32_t i = 0; i < count; ++i) {
        In(&stream, segment, kSegmentType, kSegmentOccupantGroup, i);
        if (IsOpen(&stream)) {
            void* flora = reinterpret_cast<AllocateFn>(addr::cSC4FloraOccupant_Allocate)(0x5C);
            if (flora)
                flora = reinterpret_cast<CtorFn>(addr::cSC4FloraOccupant_cSC4FloraOccupant)(flora);
            Virtual<uint32_t>(flora, 0x4);
            Virtual<uint8_t>(static_cast<char*>(flora) + 8, 0x10, stream.stream);
            void* const occupant = static_cast<char*>(flora) + 4;
            Virtual<uint8_t>(occupant, 0xC);
            Virtual<uint8_t>(At<void*>(self, field::kOccupants), 0x50, occupant, 0u);
            Virtual<uint32_t>(flora, 0x8);
        }
        Close(&stream);
    }
    return 1;
}

// --- The save record (version 2) and saved flora --------------------------------

// cIGZSerializable slot 3 (0x006AFEE0), on the part at +8. Like the game's,
// the result is a whole word (0 or 1).
uint32_t __fastcall Write(void* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    char* const self = static_cast<char*>(part) - 8;
    const auto WriteBool = reinterpret_cast<StreamValueFn>(addr::SC4Stream_WriteBool);
    Virtual<uint8_t>(stream, 0x1C, static_cast<uint16_t>(kVersion));
    for (std::size_t offset = 0x2C; offset < 0x30; ++offset)
        WriteBool(stream, At<uint8_t>(self, offset));
    Virtual<uint8_t>(stream, 0x30, At<float>(self, field::kSizeX));
    Virtual<uint8_t>(stream, 0x30, At<float>(self, field::kSizeZ));
    Virtual<uint8_t>(stream, 0x20, At<uint32_t>(self, field::kCellsX));
    Virtual<uint8_t>(stream, 0x20, At<uint32_t>(self, field::kCellsZ));
    for (std::size_t offset = 0x40; offset < 0x50; offset += 4)
        Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, offset));
    Virtual<uint8_t>(stream, 0x14, At<uint8_t>(self, field::kLoaded));
    reinterpret_cast<StreamDataFn>(addr::SC4Stream_WritePollutionStruct)(stream, self + field::kWalk);
    reinterpret_cast<StreamDataFn>(addr::SC4Stream_WriteRandom)(stream, self + field::kRandom);
    reinterpret_cast<StreamDataFn>(addr::SC4Stream_WriteReferenceList)(stream, self + field::kDying);
    Virtual<uint8_t>(stream, 0x30, At<float>(self, field::kDeathTimer));
    Virtual<uint8_t>(stream, 0x30, At<float>(self, field::kDeaths));
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// cIGZSerializable slot 4 (0x006AFFE0): the same fields; another version
// fails (the low byte cleared, the rest of the word as the stream left it).
uint32_t __fastcall Read(void* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Read", logged);
    char* const self = static_cast<char*>(part) - 8;
    uint16_t version = 0;
    const uint32_t read = Virtual<uint32_t>(stream, 0x1C, &version);
    if (version != kVersion)
        return read & 0xFFFFFF00u;
    for (std::size_t offset = 0x2C; offset < 0x30; ++offset)
        reinterpret_cast<StreamDataFn>(addr::SC4Stream_ReadBool)(stream, self + offset);
    Virtual<uint8_t>(stream, 0x30, &At<float>(self, field::kSizeX));
    Virtual<uint8_t>(stream, 0x30, &At<float>(self, field::kSizeZ));
    Virtual<uint8_t>(stream, 0x20, &At<uint32_t>(self, field::kCellsX));
    Virtual<uint8_t>(stream, 0x20, &At<uint32_t>(self, field::kCellsZ));
    for (std::size_t offset = 0x40; offset < 0x50; offset += 4)
        Virtual<uint8_t>(stream, 0x24, &At<uint32_t>(self, offset));
    Virtual<uint8_t>(stream, 0x14, &At<uint8_t>(self, field::kLoaded));
    reinterpret_cast<StreamDataFn>(addr::SC4Stream_ReadPollutionStruct)(stream, self + field::kWalk);
    reinterpret_cast<StreamDataFn>(addr::SC4Stream_ReadRandom)(stream, self + field::kRandom);
    reinterpret_cast<StreamDataFn>(addr::SC4Stream_ReadReferenceList)(stream, self + field::kDying);
    Virtual<uint8_t>(stream, 0x30, &At<float>(self, field::kDeathTimer));
    Virtual<uint8_t>(stream, 0x30, &At<float>(self, field::kDeaths));
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// Slot 24 (0x006B2760): every flora occupant into the segment (records
// C9BB3FC6-C9BB3FC7-1 count, C9BB3FC6-4A664F83-i occupants).
uint8_t __fastcall SaveObliterated(void* self, void*, void* segment)
{
    static bool logged = false;
    FirstCall("SaveObliterated", logged);
    const auto Out = reinterpret_cast<SegmentOutFn>(addr::SC4SegmentOStream_SC4SegmentOStream);
    const auto IsOpen = reinterpret_cast<WrapperFn>(addr::SC4SegmentStream_IsOpen);
    const auto Close = reinterpret_cast<WrapperFn>(addr::SC4SegmentOStream_Close);
    void** const list = static_cast<void**>(Virtual<void*>(ListNodeAllocator(), 0xC, 0xCu));
    list[0] = list;
    list[1] = list;
    void* head = list;
    Virtual<uint8_t>(At<void*>(self, field::kOccupants), 0x9C, &head, 0u, 0u, kFloraOccupant, -1);
    uint32_t count = 0;
    for (void** node = static_cast<void**>(list[0]); node != list; node = static_cast<void**>(node[0]))
        ++count;
    SegmentStream stream;
    Out(&stream, segment, kSegmentType, kSegmentCountGroup, 1, 0);
    if (IsOpen(&stream))
        Virtual<uint8_t>(stream.stream, 0x24, count);
    Close(&stream);
    uint32_t index = 0;
    for (void** node = static_cast<void**>(list[0]); node != list; node = static_cast<void**>(node[0])) {
        void* const occupant = node[2];
        char* const flora = occupant ? static_cast<char*>(occupant) - 4 : nullptr;
        Out(&stream, segment, kSegmentType, kSegmentOccupantGroup, index, 0);
        if (IsOpen(&stream)) {
            Virtual<uint8_t>(flora + 4, 0x7C, 1u);
            Virtual<uint8_t>(flora + 8, 0xC, stream.stream);
            Virtual<uint8_t>(flora + 4, 0x74, 1u);
        }
        Virtual<uint32_t>(occupant, 0x8);
        Close(&stream);
        ++index;
    }
    reinterpret_cast<ListFn>(addr::SC4List_Clear2)(&head);
    Virtual<void>(ListNodeAllocator(), 0x10, head);
    return 1;
}

// --- Helpers ----------------------------------------------------------------------

// 0x006B19E0: +0x2C; turning flora off demolishes every tree.
void __fastcall SetEnabled(void* self, void*, uint8_t enabled)
{
    static bool logged = false;
    FirstCall("SetEnabled", logged);
    if (At<uint8_t>(self, field::kEnabled) == enabled)
        return;
    At<uint8_t>(self, field::kEnabled) = enabled;
    if (enabled)
        return;
    void** const list = static_cast<void**>(Virtual<void*>(ListNodeAllocator(), 0xC, 0xCu));
    list[0] = list;
    list[1] = list;
    void* head = list;
    if (Virtual<uint8_t>(At<void*>(self, field::kOccupants), 0xA4, &head, nullptr, nullptr, kFloraOccupant, -1)) {
        for (void** node = static_cast<void**>(list[0]); node != list; node = static_cast<void**>(node[0])) {
            void* const occupant = node[2];
            Virtual<uint8_t>(self, 0x4C, occupant, 0);
            Virtual<uint32_t>(occupant, 0x8);
        }
    }
    reinterpret_cast<ListFn>(addr::SC4List_Destroy2)(&head);
}

// 0x006AFD10 (the occupant filter's slot 3): flora, three other occupant
// types, and props (0xC772BF98) whose kind (interface 0xA821EF94, slot 12)
// has a bit of 0x1F4F.
uint8_t __fastcall FilterIncludes(void*, void*, void* occupant)
{
    static bool logged = false;
    FirstCall("FilterIncludes", logged);
    const uint32_t type = Virtual<uint32_t>(occupant, 0x1C);
    if (type == kFloraOccupant || type == 0x278128A0 || type == 0x6A11BD2B || type == 0xA823821E)
        return 1;
    if (type != 0xC772BF98)
        return 0;
    void* prop = nullptr;
    Virtual<uint8_t>(occupant, 0x0, 0xA821EF94u, &prop);
    if (!prop)
        return 0;
    const bool included = (Virtual<uint32_t>(prop, 0x30) & 0x1F4F) != 0;
    Virtual<uint32_t>(prop, 0x8);
    return included ? 1 : 0;
}

// --- The cheat code ---------------------------------------------------------------

using TokenizerFn = void*(__thiscall*)(void* words, const char* text);
using CountFn = int32_t(__thiscall*)(void* words);
using SubstringFn = void*(__thiscall*)(GameString* string, const void* source, uint32_t start, uint32_t count);
using StringFn = void(__thiscall*)(GameString* string);
using EqualsFn = uint32_t(__cdecl*)(const void* buffer, const char* text);
using NumberFn = uint32_t(__cdecl*)(const char* text);
using DrawFn = double(__thiscall*)(void* random);
using EnabledFn = void(__thiscall*)(void* self, uint8_t enabled);
using SeedFn = uint8_t(__thiscall*)(void* self, uint32_t type, float x, float z);

// Word i of the tokenizer (its vtable slot 16), as a new lower-case string.
void LowerWord(GameString* word, void* words, uint32_t index)
{
    void* const source = Virtual<void*>(words, 0x40, index);
    reinterpret_cast<SubstringFn>(addr::SC4String_Substring)(word, source, 0, 0xFFFFFFFFu);
    reinterpret_cast<StringFn>(addr::cRZBaseString_MakeLower)(word);
}

bool Is(const GameString& word, const char* text)
{
    return reinterpret_cast<EqualsFn>(addr::SC4String_Equals)(&word.begin, text) != 0;
}

// A word's number (SC4::TextToUint32 on its text).
uint32_t Number(void* words, uint32_t index)
{
    void* const source = Virtual<void*>(words, 0x40, index);
    return reinterpret_cast<NumberFn>(addr::SC4_TextToUint32)(At<const char*>(source, 0x4));
}

// "Flora blast <places> <trees> <spread>": at each of `places` random places
// in the city, `trees` seeds scattered normally by `spread` meters, each of
// the best type for its place.
void Blast(void* self, void* words)
{
    const uint32_t places = Number(words, 2);
    const uint32_t trees = Number(words, 3);
    const uint32_t spread = Number(words, 4);
    void* const random = static_cast<char*>(self) + field::kRandom;
    const auto NextFloat = reinterpret_cast<DrawFn>(addr::cRZRandom_NextFloat);
    const auto Gaussian = reinterpret_cast<DrawFn>(addr::cRZRandom_Gaussian);
    for (uint32_t place = places; place != 0; --place) {
        const float centerX = static_cast<float>(NextFloat(random) * At<float>(self, field::kSizeX));
        const float centerZ = static_cast<float>(NextFloat(random) * At<float>(self, field::kSizeZ));
        if (!trees)
            continue;
        const double scale = static_cast<double>(spread);
        for (uint32_t tree = trees; tree != 0; --tree) {
            const float x = static_cast<float>(Gaussian(random) * scale + centerX);
            const float z = static_cast<float>(Gaussian(random) * scale + centerZ);
            if (!(x >= 0.0f) || !(z >= 0.0f) || !(x < At<float>(self, field::kSizeX)) ||
                !(z <= At<float>(self, field::kSizeZ)))
                continue;
            float score = 0;
            const uint32_t type = Virtual<uint32_t>(self, 0x40, x, z, &score);
            reinterpret_cast<SeedFn>(addr::cSC4FloraSimulator_Seed)(self, type, x, z);
        }
    }
}

} // namespace

// DoMessage, message 0x230E27AC (0x006B2FD9): the cheat code "Flora" with
// on, off, preserve [off], propogation [off] or blast. The words go through
// the game's tokenizer and strings, in the game's order.
bool CheatMessage(void* self, void* message)
{
    static bool logged = false;
    FirstCall("CheatMessage", logged);
    if (Virtual<uint32_t>(message, 0x24) != kCheatCode)
        return false;
    const char* const text = Virtual<const char*>(Virtual<void*>(message, 0x3C), 0x14);
    alignas(4) char words[0x40];
    reinterpret_cast<TokenizerFn>(addr::SC4Tokenizer_SC4Tokenizer)(words, text);
    const auto Count = reinterpret_cast<CountFn>(addr::SC4Tokenizer_Count);
    const auto DropWords = reinterpret_cast<SelfFn>(addr::SC4Tokenizer_Destroy);
    const auto Drop = reinterpret_cast<StringFn>(addr::SC4String_Destroy);
    if (Count(words) < 2) {
        DropWords(words);
        return false;
    }
    GameString word;
    LowerWord(&word, words, 1);
    if (Is(word, "on")) {
        At<uint8_t>(self, field::kEnabled) = 1;
    } else if (Is(word, "off")) {
        reinterpret_cast<EnabledFn>(addr::cSC4FloraSimulator_SetEnabled)(self, 0);
    } else if (Is(word, "preserve")) {
        At<uint8_t>(self, 0x2F) = 1;
        if (Count(words) >= 3) {
            GameString option;
            LowerWord(&option, words, 2);
            if (Is(option, "off"))
                At<uint8_t>(self, 0x2F) = 0;
            Drop(&option);
        }
    } else if (Is(word, "propogation")) {
        At<uint8_t>(self, 0x2E) = 1;
        At<uint8_t>(self, field::kSeeding) = 1;
        if (Count(words) >= 3) {
            GameString option;
            LowerWord(&option, words, 2);
            if (Is(option, "off")) {
                At<uint8_t>(self, 0x2E) = 0;
                At<uint8_t>(self, field::kSeeding) = 0;
            }
            Drop(&option);
        }
    } else if (Is(word, "blast")) {
        Blast(self, words);
    }
    Drop(&word);
    DropWords(words);
    return true;
}

namespace {

// --- Installation of the above ----------------------------------------------

template <typename Fn>
bool Install(std::uintptr_t address, Fn ours, Fn* original)
{
    const addr::Entry* entry = addr_check::Find(address);
    return entry && hook::Install(*entry, ours, original);
}

// Swaps a slot of a game vtable if it still holds the game's function.
bool SwapSlot(std::uintptr_t vtableAddress, int index, std::uintptr_t original, void* ours, const char* what)
{
    auto* const slot = reinterpret_cast<std::uintptr_t*>(vtableAddress) + index;
    if (*slot != original) {
        log::Write("flora %s: slot %d holds %08X, expected %08X; not installing", what, index,
                   static_cast<unsigned>(*slot), static_cast<unsigned>(original));
        return false;
    }
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old))
        return false;
    *slot = reinterpret_cast<std::uintptr_t>(ours);
    VirtualProtect(slot, sizeof(*slot), old, &old);
    return true;
}

using SelfOursFn = uint8_t(__fastcall*)(void*, void*);
using SegmentOursFn = uint8_t(__fastcall*)(void*, void*, void*);
using EnabledOursFn = void(__fastcall*)(void*, void*, uint8_t);
SelfOursFn g_originalInit = nullptr, g_originalShutdown = nullptr;
SegmentOursFn g_originalLoadOccupants = nullptr;
EnabledOursFn g_originalEnabled = nullptr;

bool InstallInit(registry::Mode)
{
    return game::IsBuild641() && Install(addr::cSC4FloraSimulator_Init, &Init, &g_originalInit) &&
           Install(addr::cSC4FloraSimulator_LoadOccupants, &LoadOccupants, &g_originalLoadOccupants);
}

bool InstallShutdown(registry::Mode)
{
    return game::IsBuild641() && Install(addr::cSC4FloraSimulator_Shutdown, &Shutdown, &g_originalShutdown);
}

bool InstallSave(registry::Mode)
{
    return game::IsBuild641() &&
           SwapSlot(addr::cSC4FloraSimulator_vftable_8, 3, 0x006AFEE0, reinterpret_cast<void*>(&Write), "save") &&
           SwapSlot(addr::cSC4FloraSimulator_vftable, 24, 0x006B2760, reinterpret_cast<void*>(&SaveObliterated),
                    "save");
}

bool InstallLoad(registry::Mode)
{
    return game::IsBuild641() &&
           SwapSlot(addr::cSC4FloraSimulator_vftable_8, 4, 0x006AFFE0, reinterpret_cast<void*>(&Read), "load");
}

bool InstallHelpers(registry::Mode)
{
    return game::IsBuild641() && Install(addr::cSC4FloraSimulator_SetEnabled, &SetEnabled, &g_originalEnabled) &&
           SwapSlot(addr::cSC4FloraSimulator_FilterVtable, 3, 0x006AFD10, reinterpret_cast<void*>(&FilterIncludes),
                    "filter");
}

const bool g_registeredInit = registry::Register({
    "simulators.flora.init",
    "simulators/flora",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4FloraSimulator_Init,
    "Our flora Init and the loading of saved obliterated flora (docs/flora.md)",
    false,
    InstallInit,
});

const bool g_registeredShutdown = registry::Register({
    "simulators.flora.shutdown",
    "simulators/flora",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4FloraSimulator_Shutdown,
    "Our flora Shutdown (docs/flora.md)",
    false,
    InstallShutdown,
});

const bool g_registeredSave = registry::Register({
    "simulators.flora.save",
    "simulators/flora",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4FloraSimulator_vftable_8,
    "Our flora Write (the save record, version 2) and SaveObliterated (slot 24) (docs/flora.md)",
    false,
    InstallSave,
});

const bool g_registeredLoad = registry::Register({
    "simulators.flora.load",
    "simulators/flora",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4FloraSimulator_vftable_8,
    "Our flora Read: the save record, version 2 (docs/flora.md)",
    false,
    InstallLoad,
});

const bool g_registeredHelpers = registry::Register({
    "simulators.flora.helpers",
    "simulators/flora",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4FloraSimulator_SetEnabled,
    "Our flora on/off switch and occupant filter (docs/flora.md)",
    false,
    InstallHelpers,
});

// --- Our object -------------------------------------------------------------

constexpr uint32_t kClassId = 0xC990C034;
constexpr uint32_t kIidUnknown = 0x00000001;
constexpr uint32_t kIidFlora = 0x06C66177;
constexpr uint32_t kIidMessageTarget = 0x452294AA;
constexpr uint32_t kIidSerializable = 0xE4FDA3D4;
constexpr std::size_t kSize = 0xB8;

// The three vtables (the game's: 0x00AB13C0 with the deleting destructor in
// slot 25, 0x00AB13AC, 0x00AB1394).
constexpr int kSlots = 26, kSlots4 = 4, kSlots8 = 6;
void* g_vtable[kSlots];
void* g_vtable4[kSlots4];
void* g_vtable8[kSlots8];

uint32_t __fastcall AddRef(char* self, void*)
{
    return ++At<uint32_t>(self, field::kRefCount);
}

// As the game's (0x006AFC90).
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    std::size_t offset;
    switch (iid) {
    case kIidFlora:
    case kIidUnknown: offset = 0; break;
    case kIidMessageTarget: offset = 0x4; break;
    case kIidSerializable: offset = 0x8; break;
    default: return 0;
    }
    *object = self ? self + offset : nullptr;
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// As the game's Release (0x006B0480): at the last reference, Shutdown and
// the deleting destructor; the count stays at 1.
uint32_t __fastcall Release(char* self, void*)
{
    uint32_t& refs = At<uint32_t>(self, field::kRefCount);
    if (refs > 1)
        return --refs;
    Virtual<uint8_t>(self, 0x10);
    Virtual<void*>(self, 0x64, 1u);
    return 0;
}

void SetVtables(char* self)
{
    At<void**>(self, 0x0) = g_vtable;
    At<void**>(self, 0x4) = g_vtable4;
    At<void**>(self, 0x8) = g_vtable8;
}

// As the game's destructor (0x006B1990): the dying list and the tract
// walk's buffer.
void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    static int destroyed = 0;
    if (destroyed++ < 3)
        log::Write("flora object: destroying %p", static_cast<void*>(self));
    SetVtables(self);
    reinterpret_cast<ListFn>(addr::SC4List_Destroy2)(self + field::kDying);
    if (void* const buffer = At<void*>(self, 0x6C))
        Free(buffer);
    if (flags & 1)
        reinterpret_cast<DeleteFn>(addr::SC4_operator_delete)(self);
    return self;
}

uint32_t __fastcall GetGZCLSID(void*, void*) { return kClassId; }

template <std::size_t Offset>
uint8_t __fastcall QueryInterfaceAt(char* part, void*, uint32_t iid, void** object)
{
    return QueryInterface(part - Offset, nullptr, iid, object);
}

template <std::size_t Offset>
uint32_t __fastcall AddRefAt(char* part, void*)
{
    return AddRef(part - Offset, nullptr);
}

template <std::size_t Offset>
uint32_t __fastcall ReleaseAt(char* part, void*)
{
    return Release(part - Offset, nullptr);
}

// As the game's constructor (0x006B1880): only the fields it sets.
void Construct(char* self)
{
    SetVtables(self);
    At<uint8_t>(self, field::kInitialized) = 0;
    std::memset(self + field::kRefCount, 0, 0x2C - 0x10);
    At<uint8_t>(self, 0x2C) = 1;
    At<uint8_t>(self, 0x2D) = 1;
    At<uint8_t>(self, 0x2E) = 0;
    At<uint8_t>(self, 0x2F) = 0;
    At<uint32_t>(self, field::kDate) = 0;
    At<uint32_t>(self, 0x44) = 6;
    At<uint32_t>(self, field::kSeedsPerMonth) = 5;
    At<uint8_t>(self, field::kLoaded) = 0;
    At<uint32_t>(self, field::kCellsX) = 1;
    At<uint32_t>(self, field::kCellsZ) = 1;
    At<uint32_t>(self, field::kTractsPerMonth) = 1;
    At<float>(self, field::kSizeX) = 1.0f;
    At<float>(self, field::kSizeZ) = 1.0f;
    At<uint8_t>(self, 0x54) = 1;
    At<uint8_t>(self, 0x55) = 1;
    At<uint8_t>(self, 0x56) = 1;
    std::memset(self + 0x58, 0, 0x80 - 0x58);
    At<uint32_t>(self, 0x80) = 1;
    reinterpret_cast<RandomCtorFn>(addr::SC4Random_Construct)(self + field::kRandom, 0xFFFFFFFFu);
    At<uint32_t>(self, field::kSlopeScale) = 0x3DA3D70A; // 0.08
    At<void*>(self, field::kDying) = nullptr;
    void** const node =
        reinterpret_cast<AllocateNodesFn>(addr::SC4List_AllocateNodes)(self + field::kDying, 1, 0);
    node[0] = node;
    node[1] = node;
    At<void**>(self, field::kDying) = node;
    At<float>(self, field::kDeathTimer) = 0.0f;
    At<float>(self, field::kDeaths) = 0.0f;
    At<float>(self, field::kDeathRate) = 1.0f;
    At<float>(self, field::kDeathsPerStep) = 4.0f;
}

void BuildVtables()
{
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4FloraSimulator_vftable), sizeof(g_vtable));
    std::memcpy(g_vtable4, reinterpret_cast<const void*>(addr::cSC4FloraSimulator_vftable_4), sizeof(g_vtable4));
    std::memcpy(g_vtable8, reinterpret_cast<const void*>(addr::cSC4FloraSimulator_vftable_8), sizeof(g_vtable8));
    g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
    g_vtable[1] = reinterpret_cast<void*>(&AddRef);
    g_vtable[2] = reinterpret_cast<void*>(&Release);
    g_vtable[25] = reinterpret_cast<void*>(&DeletingDestructor);
    g_vtable4[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x4>);
    g_vtable4[1] = reinterpret_cast<void*>(&AddRefAt<0x4>);
    g_vtable4[2] = reinterpret_cast<void*>(&ReleaseAt<0x4>);
    g_vtable8[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x8>);
    g_vtable8[1] = reinterpret_cast<void*>(&AddRefAt<0x8>);
    g_vtable8[2] = reinterpret_cast<void*>(&ReleaseAt<0x8>);
    g_vtable8[5] = reinterpret_cast<void*>(&GetGZCLSID);
}

// The factory, as the game's (0x006B2940): operator new(0xB8), then the
// constructor.
cIGZUnknown* Create()
{
    static bool built = false;
    if (!built) {
        built = true;
        BuildVtables();
    }
    auto* const self = static_cast<char*>(reinterpret_cast<NewFn>(addr::SC4_operator_new)(kSize));
    if (!self)
        return nullptr;
    Construct(self);
    static int created = 0;
    if (created++ < 3)
        log::Write("flora object: created %p", static_cast<void*>(self));
    return reinterpret_cast<cIGZUnknown*>(self);
}

bool ApplyObject(registry::Mode)
{
    return game::IsBuild641() && director::AddClass(kClassId, Create);
}

const bool g_registeredObject = registry::Register({
    "simulators.flora.object",
    "simulators/flora",
    registry::Kind::Gzcom,
    registry::Phase::DirectorInit,
    kClassId,
    "Our cSC4FloraSimulator object under the game's class ID: factory, constructor, destructor, identity; "
    "methods from the (patched) vtables (docs/flora.md)",
    false,
    ApplyObject,
});

} // namespace
} // namespace opensc4::flora_sim
