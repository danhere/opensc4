// OPENSC4_TEST_T9=<day>: the U-Drive-It control's third piece
// (ui.tools.udrive4, docs/tools.md). As T7a's and T8's tests it calls only the
// game's addresses and the class's vtable slots, never our functions. The
// marker is made on its own; its renderer, the framework, the framework's
// service, the six model objects, the city, the traffic network and the
// terrain are stand-ins that record what they are told (only which stand-in
// was meant goes into the hash, never a pointer). Every stand-in slot the
// game does not ask is counted: `unexpected slots` must be 0.
//
// OPENSC4_TEST_T9_PARTS (decimal mask, default 63):
//   1  the marker's constructor and destructor
//   2  its Init and Shutdown with stand-in services (the direction table is
//      hashed and put back)
//   4  BuildQuad on a stand-in terrain (heights on both sides of the point,
//      the piece flag's extra 3.0)
//   8  ChooseModel over the direction table's branches
//  16  Draw with no city, no map, an empty rectangle, no records and a
//      record whose four sides have no pieces
//  32  the third interface's three thunks and the overlay manager's keyed
//      flag on a one-node map
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <bit>
#include <utility>

namespace opensc4::autotest {
namespace {

struct Fnv {
    uint32_t h = 2166136261u;
    void Bytes(const void* data, std::size_t size)
    {
        for (std::size_t i = 0; i < size; ++i)
            h = (h ^ static_cast<const uint8_t*>(data)[i]) * 16777619u;
    }
    template <typename T>
    void Add(const T& value)
    {
        Bytes(&value, sizeof value);
    }
};

char g_token[32];
Fnv* g_log = nullptr;
int g_unexpected = 0;

template <typename T>
void Note(const T& value)
{
    if (g_log)
        g_log->Add(value);
}

template <typename R, typename... Args>
R Game(std::uintptr_t function, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(function)(self, args...);
}

// --- Stand-ins --------------------------------------------------------------------------

constexpr std::size_t kSlots = 0x50;

struct Fake {
    void** vtable;
    uint8_t raw[0x300];
    void* slots[kSlots];
};

uint32_t __fastcall Unexpected(Fake* self, void*)
{
    ++g_unexpected;
    Note(uint8_t{0xEE});
    Note(self ? self->raw[0x2FE] : uint8_t{0});
    return 0;
}

void Make(Fake& fake, uint8_t role)
{
    std::memset(&fake, 0, sizeof fake);
    fake.vtable = fake.slots;
    for (void*& slot : fake.slots)
        slot = reinterpret_cast<void*>(&Unexpected);
    fake.raw[0x2FE] = role;
}

template <typename T>
T& Field(Fake* fake, std::size_t offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<char*>(fake) + offset);
}

template <typename T>
T& Field(Fake& fake, std::size_t offset)
{
    return Field<T>(&fake, offset);
}

const void* g_known[8];

uint8_t Which(const void* pointer)
{
    if (!pointer)
        return 0;
    for (uint8_t i = 0; i < 8; ++i)
        if (g_known[i] == pointer)
            return static_cast<uint8_t>(i + 1);
    return 0xFF;
}

// The model objects the service hands to Init (and the resource objects).
Fake g_models[6];

uint8_t __fastcall ServiceQuery(Fake* self, void*, const uint32_t* iid, void** out)
{
    (void)self;
    Note(uint8_t{0x51});
    Note(iid[0]);
    Note(iid[1]);
    Note(iid[2]);
    const uint32_t index = iid[2] - 0x6BE09921u;
    if (index < 6) {
        *out = &g_models[index];
        return 1;
    }
    return 0;
}

uint32_t __fastcall ServiceRelease(Fake* self, void*)
{
    Note(uint8_t{0x52});
    ++self->raw[0x2FF];
    return 0;
}

Fake g_framework, g_service, g_renderer, g_city, g_map, g_terrain, g_resource;


uint8_t __fastcall FrameworkQuery(Fake* self, void*, uint32_t first, uint32_t second, void** out)
{
    (void)self;
    Note(uint8_t{0x50});
    Note(first);
    Note(second);
    *out = &g_service;
    return 1;
}

void __fastcall RendererBegin(Fake*, void*)
{
    Note(uint8_t{0x53});
}

void* __fastcall CityNetwork(Fake* self, void*)
{
    Note(uint8_t{0x54});
    return Field<void*>(self, 0x100);
}

const int32_t* __fastcall MapRectangle(Fake* self, void*)
{
    Note(uint8_t{0x55});
    return &Field<int32_t>(self, 0x60);
}

void* __fastcall MapRecords(Fake* self, void*, int32_t, int32_t)
{
    Note(uint8_t{0x56});
    return Field<void*>(self, 0x100);
}

uint32_t __fastcall MapPieces(Fake* self, void*, void* occupant, uint32_t side, void* ids, void* selected)
{
    Note(uint8_t{0x57});
    Note(Which(occupant));
    Note(static_cast<uint8_t>(side));
    const uint32_t count = Field<uint32_t>(self, 0x104);
    if (count) {
        auto* vector = static_cast<void**>(ids);
        vector[0] = Field<void*>(self, 0x108);
        vector[1] = Field<void*>(self, 0x10C);
        vector[2] = Field<void*>(self, 0x10C);
        *static_cast<uint16_t*>(selected) = static_cast<uint16_t>(Field<uint32_t>(self, 0x110));
    }
    return count;
}

double __fastcall TerrainHeight(Fake* self, void*, float x, float z)
{
    Note(uint8_t{0x58});
    Note(x);
    Note(z);
    return Field<double>(self, 0x100);
}

uint8_t __fastcall ResourceQuery(Fake*, void*, void*, void*, void*, void*, void*)
{
    Note(uint8_t{0x59});
    return 0;
}

void SetUpFakes()
{
    static bool done = false;
    if (done) {
        return;
    }
    done = true;
    for (Fake* fake : {&g_framework, &g_service, &g_renderer, &g_city, &g_map, &g_terrain, &g_resource})
        Make(*fake, 0);
    for (Fake& model : g_models) {
        Make(model, 1);
        Field<uint32_t>(model, 0x8) = 0;  // no factory
        Field<uint32_t>(model, 0xC) = 2;  // one release leaves it alive
    }
    g_framework.slots[0x14 / 4] = reinterpret_cast<void*>(&FrameworkQuery);
    g_service.slots[0x18 / 4] = reinterpret_cast<void*>(&ServiceQuery);
    g_service.slots[0x8 / 4] = reinterpret_cast<void*>(&ServiceRelease);
    g_renderer.slots[0x28 / 4] = reinterpret_cast<void*>(&RendererBegin);
    g_city.slots[0xA0 / 4] = reinterpret_cast<void*>(&CityNetwork);
    g_map.slots[0x44 / 4] = reinterpret_cast<void*>(&MapRectangle);
    g_map.slots[0x34 / 4] = reinterpret_cast<void*>(&MapRecords);
    g_map.slots[0xA4 / 4] = reinterpret_cast<void*>(&MapPieces);
    g_terrain.slots[0x34 / 4] = reinterpret_cast<void*>(&TerrainHeight);
    g_resource.slots[0xC / 4] = reinterpret_cast<void*>(&ResourceQuery);
    int32_t* const rect = &Field<int32_t>(g_map, 0x60);
    rect[0] = rect[1] = rect[2] = rect[3] = 0;
}

// --- The marker -------------------------------------------------------------------------

char* NewMarker()
{
    char* const marker = static_cast<char*>(
        reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0x4C));
    std::memset(marker, 0x5A, 0x4C);
    reinterpret_cast<char*(__thiscall*)(void*)>(addr::SC4UDriveMarker_Construct)(marker);
    return marker;
}

void DeleteMarker(char* marker)
{
    reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_operator_delete)(marker);
}

void AddFields(Fnv& f, char* marker)
{
    f.Add(At<std::uintptr_t>(marker, 0));
    f.Add(At<std::uintptr_t>(marker, 4));
    f.Bytes(marker + 8, 0x24);
    f.Add(At<std::uintptr_t>(marker, 0x44));
    f.Add(At<std::uintptr_t>(marker, 0x48));
}

void PartLifetime(Fnv& out)
{
    Fnv f;
    g_log = &f;
    char* const marker = NewMarker();
    AddFields(f, marker);
    void* const savedRenderer = Global<void*>(addr::SC4_View43DD0);
    void* const savedFramework = Global<void*>(addr::GZFrameWork_Instance);
    void* const savedResources = Global<void*>(addr::SC4_ResourceManager);
    Global<void*>(addr::SC4_View43DD0) = &g_renderer;
    Global<void*>(addr::GZFrameWork_Instance) = &g_framework;
    Global<void*>(addr::SC4_ResourceManager) = &g_resource;
    Game<uint8_t>(addr::SC4UDriveMarker_Init, marker);
    Global<void*>(addr::SC4_View43DD0) = savedRenderer;
    Global<void*>(addr::GZFrameWork_Instance) = savedFramework;
    Global<void*>(addr::SC4_ResourceManager) = savedResources;
    f.Add(At<uint8_t>(marker, 0xC));
    f.Add(At<uint32_t>(marker, 0x10));
    f.Add(At<uint32_t>(marker, 0x14));
    for (std::size_t i = 0; i < 6; ++i) {
        int index = -1;
        for (int j = 0; j < 6; ++j)
            if (At<void*>(marker, 0x2C + 4 * i) == &g_models[j])
                index = j;
        f.Add(static_cast<uint32_t>(index));
    }
    f.Add(At<void*>(marker, 0x44) != nullptr);
    f.Add(At<void*>(marker, 0x48) != nullptr);
    const float* const table = reinterpret_cast<const float*>(addr::SC4UDriveMarker_DirectionTable);
    for (int i = 0; i < 48; ++i)
        f.Add(reinterpret_cast<const uint32_t*>(table)[i]);
    for (const Fake& model : g_models)
        f.Add(Field<uint32_t>(const_cast<Fake&>(model), 0xC));
    Game<void>(addr::SC4UDriveMarker_Shutdown, marker);
    f.Add(At<uint8_t>(marker, 0xC));
    for (std::size_t offset : {std::size_t{0x2C}, std::size_t{0x30}, std::size_t{0x34}, std::size_t{0x38},
                               std::size_t{0x3C}, std::size_t{0x40}})
        f.Add(At<void*>(marker, offset) != nullptr);
    for (const Fake& model : g_models)
        f.Add(Field<uint32_t>(const_cast<Fake&>(model), 0xC));
    Game<void>(addr::SC4UDriveMarker_Shutdown, marker);
    f.Add(At<uint8_t>(marker, 0xC));
    // The deleting destructor with no bit 0 (the marker stays allocated).
    const uint32_t returned = static_cast<uint32_t>(
        reinterpret_cast<std::uintptr_t>(Game<char*>(addr::SC4UDriveMarker_DeletingDestructor, marker,
                                                    uint32_t{0})));
    f.Add(returned == static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(marker)));
    f.Add(At<std::uintptr_t>(marker, 4));
    DeleteMarker(marker);
    // The adjustor (this = marker + 4).
    char* const adjusted = NewMarker();
    const uint32_t adjustor = static_cast<uint32_t>(
        reinterpret_cast<std::uintptr_t>(Game<char*>(addr::SC4UDriveMarker_DeletingDestructorAdjustor,
                                                    adjusted + 4, uint32_t{0})));
    f.Add(adjustor == static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(adjusted)));
    f.Add(At<std::uintptr_t>(adjusted, 4));
    DeleteMarker(adjusted);
    f.Add(g_unexpected);
    g_log = nullptr;
    log::Write("autotest: udrive4: lifetime %08X", f.h);
    out.Add(f.h);
}

void PartBuild(Fnv& out)
{
    Fnv f;
    g_log = &f;
    char* const marker = NewMarker();
    const auto* const savedTable = reinterpret_cast<const uint32_t*>(addr::SC4UDriveMarker_DirectionTable);
    uint32_t table[48];
    std::memcpy(table, savedTable, sizeof table);
    float* const directions = reinterpret_cast<float*>(addr::SC4UDriveMarker_DirectionTable);
    for (int i = 0; i < 16; ++i) {
        directions[i * 3] = static_cast<float>((i % 3) - 1);
        directions[i * 3 + 1] = 0.0f;
        directions[i * 3 + 2] = static_cast<float>((i / 3) - 1);
    }
    At<float>(marker, 0x18) = 0.5f;
    const float positions[][3] = {
        {100.0f, 0.0f, 100.0f}, {512.0f, 40.0f, 512.0f}, {-20.0f, 200.0f, 900.0f}, {0.0f, -50.0f, 0.0f}};
    Field<double>(g_terrain, 0x100) = 12.5;
    int caseIndex = 0;
    for (const auto& position : positions) {
        for (uint32_t side : {0u, 1u, 2u, 3u}) {
            for (uint32_t way : {0u, 2u}) {
                const uint32_t pieceId = (side << 8) | way;
                const uint32_t flags[2] = {pieceId, pieceId | 0x07000000u};
                for (uint32_t flag : flags) {
                    float quad[20] = {};
                    Note(uint8_t{0xB0});
                    Note(caseIndex);
                    uint32_t widthBits = (caseIndex & 1) ? 0x3ECCCCCDu : 0x3F333333u;
                    widthBits = (widthBits & 0xFFFFFF00u) | side;
                    const float width = std::bit_cast<float>(widthBits);
                    Game<void>(addr::SC4UDriveMarker_BuildQuad, marker, static_cast<float*>(quad),
                               static_cast<const float*>(position), width, pieceId, nullptr, nullptr, flag);
                    for (float value : quad)
                        f.Add(std::bit_cast<uint32_t>(value));
                    ++caseIndex;
                }
            }
        }
    }
    std::memcpy(reinterpret_cast<void*>(addr::SC4UDriveMarker_DirectionTable), table, sizeof table);
    DeleteMarker(marker);
    f.Add(g_unexpected);
    g_log = nullptr;
    log::Write("autotest: udrive4: build %08X", f.h);
    out.Add(f.h);
}

void PartChoose(Fnv& out)
{
    Fnv f;
    g_log = &f;
    char* const marker = NewMarker();
    Fake models[6];
    for (int i = 0; i < 6; ++i) {
        Make(models[i], static_cast<uint8_t>(0x60 + i));
        At<void*>(marker, 0x2C + 4 * i) = &models[i];
    }
    const auto* const savedTable = reinterpret_cast<const uint32_t*>(addr::SC4UDriveMarker_DirectionTable);
    uint32_t table[48];
    std::memcpy(table, savedTable, sizeof table);
    float* const directions = reinterpret_cast<float*>(addr::SC4UDriveMarker_DirectionTable);
    for (int i = 0; i < 16; ++i) {
        directions[i * 3] = 0.0f;
        directions[i * 3 + 1] = 0.0f;
        directions[i * 3 + 2] = static_cast<float>((i % 3) - 1); // -1, 0, 1
    }
    float points[15] = {};
    for (int i = 0; i < 5; ++i)
        points[i * 3] = static_cast<float>(i);
    struct Vector {
        float* begin;
        float* end;
        float* capacity;
    } vector{points, points + 15, points + 15};
    int caseIndex = 0;
    for (uint32_t side : {0u, 1u, 2u, 3u}) {
        for (uint32_t way : {0u, 1u}) {
            const uint32_t pieceId = (side << 8) | way;
            for (uint8_t highlighted : {uint8_t{0}, uint8_t{1}}) {
                Note(uint8_t{0xC0});
                Note(caseIndex);
                const uint32_t widthBits = (0x3F333333u & 0xFFFFFF00u) | side;
                void* const chosen = Game<void*>(addr::SC4UDriveMarker_ChooseModel, marker,
                                                 std::bit_cast<float>(widthBits), pieceId, nullptr, &vector,
                                                 highlighted);
                int index = -1;
                for (int i = 0; i < 6; ++i)
                    if (chosen == &models[i])
                        index = i;
                f.Add(static_cast<uint32_t>(index));
                ++caseIndex;
            }
        }
    }
    // A short path (the last point less than 0.25 from the first).
    float shortPoints[9] = {0.0f, 0.0f, 0.0f, 0.1f, 0.0f, 0.0f, 0.2f, 0.0f, 0.0f};
    Vector shortVector{shortPoints, shortPoints + 9, shortPoints + 9};
    {
        const uint32_t widthBits = (0x3F333333u & 0xFFFFFF00u) | 3u;
        void* const chosen = Game<void*>(addr::SC4UDriveMarker_ChooseModel, marker,
                                         std::bit_cast<float>(widthBits), uint32_t{3}, nullptr, &shortVector,
                                         uint8_t{0});
        int index = -1;
        for (int i = 0; i < 6; ++i)
            if (chosen == &models[i])
                index = i;
        f.Add(static_cast<uint32_t>(index));
    }
    // No models at all.
    for (std::size_t offset : {std::size_t{0x2C}, std::size_t{0x30}, std::size_t{0x34}, std::size_t{0x38},
                               std::size_t{0x3C}, std::size_t{0x40}})
        At<void*>(marker, offset) = nullptr;
    const uint32_t noModelWidth = (0x3F333333u & 0xFFFFFF00u) | 0u;
    f.Add(Game<void*>(addr::SC4UDriveMarker_ChooseModel, marker, std::bit_cast<float>(noModelWidth), uint32_t{0},
                      nullptr, &vector, uint8_t{0}) == nullptr);
    std::memcpy(reinterpret_cast<void*>(addr::SC4UDriveMarker_DirectionTable), table, sizeof table);
    DeleteMarker(marker);
    f.Add(g_unexpected);
    g_log = nullptr;
    log::Write("autotest: udrive4: choose %08X", f.h);
    out.Add(f.h);
}

void PartDraw(Fnv& out)
{
    Fnv f;
    g_log = &f;
    char* const marker = NewMarker();
    At<float>(marker, 0x18) = 0.0f;
    // No city.
    f.Add(Game<uint8_t>(addr::SC4UDriveMarker_Draw, marker, static_cast<void*>(nullptr)));
    // A city whose traffic network is missing.
    Field<void*>(g_city, 0x100) = nullptr;
    f.Add(Game<uint8_t>(addr::SC4UDriveMarker_Draw, marker, static_cast<void*>(nullptr)));
    // An empty rectangle.
    Field<void*>(g_city, 0x100) = &g_map;
    int32_t* const rect = &Field<int32_t>(g_map, 0x60);
    rect[0] = 4;
    rect[1] = 4;
    rect[2] = 3;
    rect[3] = 5;
    f.Add(Game<uint8_t>(addr::SC4UDriveMarker_Draw, marker, static_cast<void*>(nullptr)));
    f.Add(At<float>(marker, 0x18));
    // One cell with no records.
    rect[2] = 4;
    Field<void*>(g_map, 0x100) = nullptr;
    f.Add(Game<uint8_t>(addr::SC4UDriveMarker_Draw, marker, static_cast<void*>(nullptr)));
    // One record whose four sides have no pieces.
    char record[0x60] = {};
    Field<void*>(g_map, 0x100) = record;
    Field<uint32_t>(g_map, 0x104) = 0;
    f.Add(Game<uint8_t>(addr::SC4UDriveMarker_Draw, marker, static_cast<void*>(nullptr)));
    f.Add(At<float>(marker, 0x18));

    f.Add(g_unexpected);
    DeleteMarker(marker);
    g_log = nullptr;
    log::Write("autotest: udrive4: draw %08X", f.h);
    out.Add(f.h);
}

void PartThunks(Fnv& out)
{
    Fnv f;
    g_log = &f;
    char* const control = static_cast<char*>(
        reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0xC0));
    std::memset(control, 0, 0xC0);
    At<uint32_t>(control, 8) = 5;
    void* object = nullptr;
    f.Add(Game<uint8_t>(addr::cSC4UDriveControl_QueryInterfaceThird, control + 0x2C, uint32_t{0x452294AA},
                        &object));
    f.Add(object == control);
    f.Add(At<uint32_t>(control, 8));
    f.Add(Game<uint32_t>(addr::cSC4UDriveControl_AddRefThird, control + 0x2C));
    f.Add(At<uint32_t>(control, 8));
    f.Add(Game<uint32_t>(addr::cSC4UDriveControl_ReleaseThird, control + 0x2C));
    f.Add(At<uint32_t>(control, 8));
    reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_operator_delete)(control);

    // The overlay manager's keyed flag on a one-node map.
    char* const overlay = static_cast<char*>(
        reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0x200));
    std::memset(overlay, 0, 0x200);
    struct Map {
        void* unused;
        void* root;
    } map{nullptr, nullptr};
    char node[0x120] = {};
    node[0x10] = 0x42;
    reinterpret_cast<uint32_t*>(node)[0x110 / 4] = 0;
    map.root = node;
    At<void*>(overlay, 0x48) = &map;
    f.Add(Game<uint8_t>(addr::SC4OverlayManager_F5658D0, overlay, uint8_t{0x42}, uint32_t{0}));
    f.Add(reinterpret_cast<uint32_t*>(node)[0x110 / 4]);
    f.Add(Game<uint8_t>(addr::SC4OverlayManager_F5658D0, overlay, uint8_t{0x42}, uint32_t{0}));
    f.Add(Game<uint8_t>(addr::SC4OverlayManager_F5658D0, overlay, uint8_t{0x43}, uint32_t{0}));
    map.root = nullptr;
    f.Add(Game<uint8_t>(addr::SC4OverlayManager_F5658D0, overlay, uint8_t{0x42}, uint32_t{0}));
    reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_operator_delete)(overlay);
    f.Add(g_unexpected);
    g_log = nullptr;
    log::Write("autotest: udrive4: thunks %08X", f.h);
    out.Add(f.h);
}

// --- The stand-in slots DrawCell asks ---------------------------------------------------

void Drive(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_T9") || !game::IsBuild641())
        return;
    if (!Global<void*>(addr::SC4_City)) {
        log::Write("autotest: udrive4: no city");
        return;
    }
    const int parts = EnvInt("OPENSC4_TEST_T9_PARTS", 63);
    SetUpFakes();
    g_unexpected = 0;
    Fnv total;

    void* const savedCity = Global<void*>(addr::SC4_City);
    void* const savedTerrain = Global<void*>(addr::SC4_Terrain);
    uint32_t savedTable[48];
    std::memcpy(savedTable, reinterpret_cast<const void*>(addr::SC4UDriveMarker_DirectionTable), sizeof savedTable);

    if (parts & 3)
        PartLifetime(total);
    std::memcpy(reinterpret_cast<void*>(addr::SC4UDriveMarker_DirectionTable), savedTable, sizeof savedTable);

    if (parts & 4) {
        Global<void*>(addr::SC4_Terrain) = &g_terrain;
        PartBuild(total);
        Global<void*>(addr::SC4_Terrain) = savedTerrain;
    }
    if (parts & 8) {
        PartChoose(total);
    }
    if (parts & 16) {
        Global<void*>(addr::SC4_City) = &g_city;
        PartDraw(total);
        Global<void*>(addr::SC4_City) = savedCity;
    }
    if (parts & 32) {
        PartThunks(total);
    }

    Global<void*>(addr::SC4_City) = savedCity;
    Global<void*>(addr::SC4_Terrain) = savedTerrain;
    std::memcpy(reinterpret_cast<void*>(addr::SC4UDriveMarker_DirectionTable), savedTable, sizeof savedTable);

    total.Add(g_unexpected);
    std::snprintf(g_token, sizeof g_token, " udrive4 %08X", total.h);
    log::Write("autotest: udrive4: total %08X (parts %d, unexpected slots %d)", total.h, parts, g_unexpected);
}

const DayHook kUDrive4{"udrive4", 70, &Drive};

void Fingerprint(cISC4City*, std::string& line)
{
    line += g_token;
}

const FingerprintPart kFingerprint{"udrive4", 32, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
