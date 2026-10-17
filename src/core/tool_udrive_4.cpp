// The U-Drive-It driving control, third piece (T9): the marker's drawing, the
// control's three this-adjusting thunks and the overlay manager method that
// flags the marker of a byte key. Replacement, off by default:
//
//   ui.tools.udrive4: SC4UDriveMarker (0x4C bytes, vtables 0x00A9D974 and
//     0x00A9D95C): its constructor, set-up, take-down, deleting destructor and
//     its adjustor, the draw and the two helpers that build the pieces; the
//     control's third interface (vtable 0x00A9D98C) thunks; and
//     SC4OverlayManager::F5658D0.
//
// The marker is made by the control's Init (ui.tools.udrive2) and given to the
// renderer at +0x9C; Draw is slot 3 of its main vtable. The draw walks the
// city's traffic network (city slot 0xA0) over the view's cell rectangle, and
// for each record's occupant asks the four sides' piece IDs through slot 0xA4
// and draws them. The marker's six model objects at +0x2C to +0x40 and the
// two at +0x44/+0x48 come from the framework's resource service in Init.
//
// Every function was written from the disassembly. Direct calls to the game's
// helpers stay calls by address (the render context, the render state, the
// terrain's height, the resource lookup and the marker's map).
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::tool_udrive_4 {
namespace {

// The marker's fields.
namespace field {
constexpr std::size_t kInited = 0x0C;  // byte
constexpr std::size_t kState = 0x10;   // a packed render state (two dwords)
constexpr std::size_t kHeight = 0x18;  // float added to the terrain height
constexpr std::size_t kPointer = 0x1C; // int32 x, z of the pointer's cell
constexpr std::size_t kClicked = 0x24; // int32 x, z of the clicked piece's cell
constexpr std::size_t kModels = 0x2C;  // six model objects, +0x2C..+0x40
constexpr std::size_t kModelA = 0x44;  // the two reference holders at the end
constexpr std::size_t kModelB = 0x48;
} // namespace field

constexpr uint32_t kBaseVtable = 0x00ACE638; // cRZBaseUnknown's
constexpr uint32_t kTemporaryVtable = 0x00A881C0;

constexpr float kZero = 0.0f;
constexpr float kQuarter = 0.25f;
constexpr float kMinusQuarter = -0.25f;
constexpr float kThree = 3.0f;
constexpr float kPointTwo = 0.2f;
constexpr float kPointZeroOne = 0.01f;
constexpr float kTwelve = 12.0f;
constexpr float kSix = 6.0f;

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

uint32_t Bits(float value)
{
    return std::bit_cast<uint32_t>(value);
}

float Float(uint32_t bits)
{
    return std::bit_cast<float>(bits);
}


void Release(void* object)
{
    Virtual<uint32_t>(object, 0x8);
}


// The game's reference holder whose object's unknown interface sits at +4:
// the reference calls go through the adjustor kept in the interface's slot 1.
void* AdjustedThis(void* object)
{
    const auto vtable = At<std::uintptr_t>(object, 0x4);
    const auto offset = static_cast<std::intptr_t>(*reinterpret_cast<std::uintptr_t*>(vtable + 0x4));
    return reinterpret_cast<char*>(object) + 0x4 + offset;
}

void AddRefAdjusted(void* object)
{
    Virtual<uint32_t>(AdjustedThis(object), 0x4);
}

void ReleaseAdjusted(void* object)
{
    Virtual<uint32_t>(AdjustedThis(object), 0x8);
}

void DropAdjusted(void* holder)
{
    if (void* const object = At<void*>(holder, 0)) {
        At<void*>(holder, 0) = nullptr;
        ReleaseAdjusted(object);
    }
}

void AssignAdjusted(void* holder, void* object)
{
    void* const old = At<void*>(holder, 0);
    if (object == old)
        return;
    if (object)
        AddRefAdjusted(object);
    At<void*>(holder, 0) = object;
    if (old)
        ReleaseAdjusted(old);
}

// The game's counted release of a texture/model object (0x008090E0).
void ReleaseCounted(void* object)
{
    This<uint32_t>(addr::SC4TextureBinding_Release, object);
}

// The traffic network's record list node starts with the occupant; the next
// record is at +0x54 (a cSC4TrafficNetworkMap::CellRecord).
constexpr std::size_t kRecordNext = 0x54;

// The game's vector (begin, end, capacity).
struct GameVector {
    void* begin;
    void* end;
    void* capacity;
};
static_assert(sizeof(GameVector) == 0x0C);

// --- The marker -------------------------------------------------------------------------

// The game's 0x005649D0: the constructor; the caller allocated 0x4C bytes.
char* __fastcall Construct(char* self, void*)
{
    At<std::uintptr_t>(self, 0) = kTemporaryVtable;
    At<std::uintptr_t>(self, 0x4) = kBaseVtable;
    At<uint32_t>(self, 0x8) = 0;
    At<std::uintptr_t>(self, 0) = addr::SC4UDriveMarker_Vtable;
    At<std::uintptr_t>(self, 0x4) = addr::SC4UDriveMarker_SecondVtable;
    At<uint8_t>(self, field::kInited) = 0;
    At<uint32_t>(self, field::kState) = 0;
    At<uint32_t>(self, field::kState + 4) = 0;
    At<int32_t>(self, field::kPointer) = -1;
    At<int32_t>(self, field::kPointer + 4) = -1;
    At<int32_t>(self, field::kClicked) = -1;
    At<int32_t>(self, field::kClicked + 4) = -1;
    At<uint32_t>(self, field::kModelA) = 0;
    At<uint32_t>(self, field::kModelB) = 0;
    return self;
}

// The game's 0x005633C0: the set-up, once (byte +0xC). Builds the 16-entry
// direction table from four points, then takes six model interfaces and two
// resource objects.
uint8_t __fastcall Init(char* self, void*)
{
    if (At<uint8_t>(self, field::kInited))
        return 1;

    if (void* const renderer = Global<void*>(addr::SC4_View43DD0)) {
        Virtual<void>(renderer, 0x28);
        This<void*>(addr::SC4RenderState_Init, self + field::kState, uint32_t{1}, uint32_t{1}, uint32_t{1},
                    uint32_t{1}, uint32_t{1}, uint32_t{1}, uint32_t{1}, uint32_t{3}, uint32_t{4}, uint32_t{5},
                    uint32_t{4}, uint32_t{0}, uint32_t{1}, uint32_t{0xFFFFFFFF});
    }
    At<uint8_t>(self, field::kInited) = 1;

    // The four points (0, 0, 8), (8, 0, 0), (16, 0, 8), (8, 0, 16), and the
    // 4 x 4 table of normalized directions between them (zero on the
    // diagonal).
    const float points[4][3] = {{0.0f, 0.0f, 8.0f}, {8.0f, 0.0f, 0.0f}, {16.0f, 0.0f, 8.0f}, {8.0f, 0.0f, 16.0f}};
    float* const table = reinterpret_cast<float*>(addr::SC4UDriveMarker_DirectionTable);
    for (int outer = 0; outer < 4; ++outer) {
        for (int inner = 0; inner < 4; ++inner) {
            const long double dx = static_cast<long double>(points[inner][0]) - points[outer][0];
            const long double dy = static_cast<long double>(points[inner][1]) - points[outer][1];
            const long double dz = static_cast<long double>(points[inner][2]) - points[outer][2];
            long double x = dx;
            long double y = dy;
            long double z = dz;
            if (outer != inner) {
                const long double length = std::sqrt(dx * dx + dy * dy + dz * dz);
                const long double scale = 1.0L / length;
                x *= scale;
                y *= scale;
                z *= scale;
            }
            const std::size_t index = static_cast<std::size_t>(outer * 4 + inner) * 3;
            table[index] = static_cast<float>(x);
            table[index + 1] = static_cast<float>(y);
            table[index + 2] = static_cast<float>(z);
        }
    }

    // The framework's service, then its six model interfaces.
    void* service = nullptr;
    if (void* const framework = Cdecl<void*>(addr::RZGetFramework))
        Virtual<void>(framework, 0x14, uint32_t{0x1AC0E11A}, uint32_t{0xFAC0E219}, &service);

    static const uint32_t kIds[6][3] = {
        {0x7AB50E44, 0x1ABE787D, 0x6BE09921}, {0x7AB50E44, 0x1ABE787D, 0x6BE09922},
        {0x7AB50E44, 0x1ABE787D, 0x6BE09923}, {0x7AB50E44, 0x1ABE787D, 0x6BE09924},
        {0x7AB50E44, 0x1ABE787D, 0x6BE09925}, {0x7AB50E44, 0x1ABE787D, 0x6BE09926},
    };
    if (service) {
        for (std::size_t i = 0; i < 6; ++i) {
            void* object = nullptr;
            Virtual<void>(service, 0x18, const_cast<uint32_t*>(kIds[i]), &object);
            At<void*>(self, field::kModels + i * 4) = object;
        }
    }

    // The two model resources: {type 0x5AD0E817, group 0xBADB57F1,
    // instance 0x8BB70000 / 0x8BB80000}.
    for (std::size_t i = 0; i < 2; ++i) {
        const uint32_t key[3] = {0x5AD0E817, 0xBADB57F1, i ? 0x8BB80000u : 0x8BB70000u};
        void* object = nullptr;
        if (Cdecl<uint8_t>(addr::SC4_GetKeyedResource, const_cast<uint32_t*>(key), &object))
            AssignAdjusted(self + (i ? field::kModelB : field::kModelA), object);
    }

    if (service)
        Release(service);
    return 1;
}

// The game's 0x00563730: the take-down.
void __fastcall Shutdown(char* self, void*)
{
    if (!At<uint8_t>(self, field::kInited))
        return;
    At<uint8_t>(self, field::kInited) = 0;
    for (std::size_t offset : {std::size_t{0x34}, std::size_t{0x38}, std::size_t{0x2C}, std::size_t{0x30},
                               std::size_t{0x3C}, std::size_t{0x40}}) {
        if (void* const object = At<void*>(self, offset)) {
            ReleaseCounted(object);
            At<void*>(self, offset) = nullptr;
        }
    }
    DropAdjusted(self + field::kModelA);
    DropAdjusted(self + field::kModelB);
}

// The game's 0x00564A30 (and 0x00564A20 through the adjustor): the deleting
// destructor.
char* MarkerDeletingDestruct(char* self, uint32_t flags)
{
    DropAdjusted(self + field::kModelB);
    DropAdjusted(self + field::kModelA);
    At<std::uintptr_t>(self, 0x4) = kBaseVtable;
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

uint32_t __fastcall DeletingDestruct(char* self, void*, uint32_t flags)
{
    return static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(MarkerDeletingDestruct(self, flags)));
}

uint32_t __fastcall DeletingDestructorAdjustor(char* self, void*, uint32_t flags)
{
    return static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(MarkerDeletingDestruct(self - 4, flags)));
}

// The game's 0x00564A60: the model object for a piece's direction.
void* __fastcall ChooseModel(char* self, void*, float sideWidth, uint32_t pieceId, void* model, void* path,
                             uint8_t highlighted)
{
    (void)model;
    const auto* const points = static_cast<const float*>(At<void*>(path, 0));
    const auto* const end = static_cast<const float*>(At<void*>(path, 4));
    const std::size_t count = static_cast<std::size_t>(reinterpret_cast<const char*>(end) -
                                                       reinterpret_cast<const char*>(points)) /
                              12;
    std::size_t index = count < 4 ? 0 : count - 3;
    long double dx = static_cast<long double>(end[-3]) - points[index * 3];
    long double dz = static_cast<long double>(end[-1]) - points[index * 3 + 2];
    if (std::sqrt(dx * dx + dz * dz) < static_cast<long double>(kQuarter)) {
        dx = static_cast<long double>(end[-3]) - points[0];
        dz = static_cast<long double>(end[-1]) - points[2];
    }
    const long double length = std::sqrt(dx * dx + dz * dz);
    const long double scale = length != 0.0L ? 1.0L / length : 0.0L;
    const uint32_t side = Bits(sideWidth) & 0xFF;
    const std::size_t tableIndex = static_cast<std::size_t>((pieceId & 0xFF) + side * 4);
    const float* const table = reinterpret_cast<const float*>(addr::SC4UDriveMarker_DirectionTable);
    const long double cross = dx * scale * table[tableIndex * 3 + 2] - dz * scale * table[tableIndex * 3];
    if (cross < static_cast<long double>(kMinusQuarter))
        return At<void*>(self, highlighted ? 0x38 : 0x34);
    if (cross <= static_cast<long double>(kQuarter))
        return At<void*>(self, highlighted ? 0x40 : 0x3C);
    return At<void*>(self, highlighted ? 0x30 : 0x2C);
}

// The game's 0x005637E0: the four textured vertices of a piece marker.
void __fastcall BuildQuad(char* self, void*, float* out, const float* position, float width, uint32_t pieceId,
                          void* model, void* path, uint32_t flags)
{
    (void)model;
    (void)path;
    (void)width;
    const uint32_t side = Bits(width) & 0xFF;
    const uint32_t way = pieceId & 0xFF;
    const std::size_t index = static_cast<std::size_t>(way + side * 4);
    const float* const table = reinterpret_cast<const float*>(addr::SC4UDriveMarker_DirectionTable);
    const long double vx = table[index * 3];
    const long double vz = table[index * 3 + 2];
    const long double x = position[0];
    const long double y = position[1];
    const long double z = position[2];
    const long double queryX = x + vx * kTwelve;
    const long double queryZ = z + vz * kTwelve;

    long double height = 0.0L;
    if (void* const terrain = Global<void*>(addr::SC4_Terrain))
        height = static_cast<long double>(Virtual<double>(terrain, 0x34, static_cast<float>(queryX),
                                                          static_cast<float>(queryZ)));
    long double top = std::max(static_cast<long double>(y), height) + At<float>(self, field::kHeight);
    if ((flags & 0x0F000000u) == 0x07000000u)
        top += kThree;

    // The game reuses the caller's stack slots for the two direction
    // components, so the offsets are the table's vector times 12, not the
    // width argument the caller passed.
    const long double leftX = x - vz * kSix;
    const long double rightX = x + vz * kSix;
    const long double nearZ = z + vx * kSix;
    const long double alongX = vx * kTwelve;
    const long double alongZ = vz * kTwelve;
    out[0] = static_cast<float>(leftX);
    out[1] = static_cast<float>(top);
    out[2] = static_cast<float>(nearZ);
    out[3] = kZero;
    out[4] = kZero;
    out[5] = static_cast<float>(leftX + alongX);
    out[6] = static_cast<float>(top);
    out[7] = static_cast<float>(nearZ + alongZ);
    out[8] = kZero;
    out[9] = 1.0f;
    out[10] = static_cast<float>(rightX + alongX);
    out[11] = static_cast<float>(top);
    out[12] = static_cast<float>(z - vx * kSix + alongZ);
    out[13] = 1.0f;
    out[14] = 1.0f;
    out[15] = static_cast<float>(rightX);
    out[16] = static_cast<float>(top);
    out[17] = static_cast<float>(z - vx * kSix);
    out[18] = 1.0f;
    out[19] = kZero;
}

// The game's 0x00564BE0: the pieces of one occupant on one cell.
// The two bytes and thirteen floats the marker object's second interface
// takes at slot 0x28: the rotation matrix, the place and a scale.
struct MarkerBlock {
    uint8_t changes;
    uint8_t flags;
    uint16_t padding;
    float matrix[9];
    float position[3];
    float scale;
};
static_assert(sizeof(MarkerBlock) == 0x38);

void __fastcall DrawCell(char* self, void*, void* context, int32_t x, int32_t z, void* occupant, uint32_t count,
                         GameVector* ids, uint16_t selected)
{
    void* const model = Virtual<void*>(occupant, 0xC4);
    const auto* const pieces = static_cast<const uint32_t*>(ids->begin);
    if (!model || !pieces || !count)
        return;

    float position[3] = {0.0f, 0.0f, 0.0f};
    void* const firstPath = Virtual<void*>(model, 0xC, pieces[0], uint32_t{0});
    if (!firstPath)
        return;
    Virtual<void>(model, 0x58, firstPath, position);

    const float* const table = reinterpret_cast<const float*>(addr::SC4UDriveMarker_DirectionTable);
    const uint32_t firstId = pieces[0];
    const std::size_t directionIndex = static_cast<std::size_t>((firstId & 0xFF) + ((firstId >> 8) & 0xFF) * 4);
    const float direction[3] = {table[directionIndex * 3], table[directionIndex * 3 + 1],
                                table[directionIndex * 3 + 2]};

    uint8_t matched = 0;
    if (x == At<int32_t>(self, field::kClicked) && z == At<int32_t>(self, field::kClicked + 4))
        matched = 1;
    else if (x == At<int32_t>(self, field::kPointer) && z == At<int32_t>(self, field::kPointer + 4))
        matched = 1;

    const float baseWidth = matched ? 0.7f : 0.4f;
    uint32_t widthBits = Bits(baseWidth);
    for (uint32_t i = 0; i < count; ++i) {
        if (i == selected)
            continue;
        const uint32_t pieceId = pieces[i];
        void* const path = Virtual<void*>(model, 0xC, pieceId, uint32_t{0});
        if (!path)
            continue;
        const uint32_t side = (pieceId >> 8) & 0xFF;
        widthBits = (widthBits & 0xFFFFFF00u) | side;
        const float width = Float(widthBits);
        void* const chosen = ChooseModel(self, nullptr, width, pieceId, model, path, 0);
        if (!chosen)
            continue;
        float quad[20];
        BuildQuad(self, nullptr, quad, position, width, pieceId, model, path, pieceId);
        At<float>(self, field::kHeight) += kPointZeroOne;
        void* const vtable = At<void*>(chosen, 0);
        void* const first = static_cast<void**>(vtable)[0];
        This<void>(addr::SC4RenderContext_BindTexture, context, first, uint32_t{0});
        This<void>(addr::SC4RenderContext_DrawArrays, context, uint32_t{6}, uint32_t{2}, uint32_t{4}, quad);
    }
    if (selected < count && pieces[selected]) {
        const uint32_t pieceId = pieces[selected];
        void* const path = Virtual<void*>(model, 0xC, pieceId, uint32_t{0});
        if (path) {
            const float width = Float(widthBits);
            void* const chosen = ChooseModel(self, nullptr, width, pieceId, model, path, 1);
            if (chosen) {
                float quad[20];
                BuildQuad(self, nullptr, quad, position, width, pieceId, model, path, pieceId);
                At<float>(self, field::kHeight) += kPointZeroOne;
                void* const vtable = At<void*>(chosen, 0);
                void* const first = static_cast<void**>(vtable)[0];
                This<void>(addr::SC4RenderContext_BindTexture, context, first, uint32_t{0});
                This<void>(addr::SC4RenderContext_DrawArrays, context, uint32_t{6}, uint32_t{2}, uint32_t{4},
                           quad);
            }
        }
    }

    // The cell's marker object: the game's direction matrix, place and
    // scale block, drawn through the object's second interface.
    if (void* const markerModel = At<void*>(self, matched ? field::kModelB : field::kModelA)) {
        AddRefAdjusted(markerModel);
        float matrix[16] = {};
        Cdecl<void>(addr::SC4Matrix_FromDirection, const_cast<float*>(direction), matrix);
        MarkerBlock block{};
        block.changes = 2;
        block.flags = 5;
        for (std::size_t i = 0; i < 9; ++i)
            block.matrix[i] = matrix[i];
        long double height = static_cast<long double>(position[1]) + At<float>(self, field::kHeight) + kPointTwo;
        const uint32_t pieceId = selected < count ? pieces[selected] : firstId;
        if ((pieceId & 0x0F000000u) == 0x07000000u)
            height += kThree;
        block.position[0] = position[0];
        block.position[1] = static_cast<float>(height);
        block.position[2] = position[2];
        block.scale = 1.0f;
        void* const second = AdjustedThis(markerModel);
        Virtual<void>(second, 0x28, &block);
        Virtual<void>(markerModel, 0x48, uint32_t{1}, static_cast<uint32_t>(selected));
        Virtual<void>(second, 0x30, context, uint32_t{0});
        ReleaseAdjusted(markerModel);
    }
}

// The game's 0x005657A0: the marker's draw (slot 3).
uint8_t __fastcall Draw(char* self, void*, void* context)
{
    void* const city = Global<void*>(addr::SC4_City);
    void* const map = city ? Virtual<void*>(city, 0xA0) : nullptr;
    if (!map)
        return 1;

    At<float>(self, field::kHeight) = 0.1f;
    const auto* const rect = Virtual<const int32_t*>(map, 0x44);
    if (rect[2] < rect[0])
        return 1;
    for (int32_t x = rect[0]; x <= rect[2]; ++x) {
        for (int32_t z = rect[1]; z <= rect[3]; ++z) {
            void* record = Virtual<void*>(map, 0x34, x, z);
            while (record) {
                void* const occupant = At<void*>(record, 0);
                GameVector ids{};
                uint16_t selected = 0;
                for (uint32_t side = 0; side < 4; ++side) {
                    const uint32_t count = Virtual<uint32_t>(map, 0xA4, occupant, side, &ids, &selected);
                    if (count)
                        DrawCell(self, nullptr, context, x, z, occupant, count, &ids, selected);
                }
                if (ids.begin)
                    Cdecl<void>(addr::SC4_Free, ids.begin);
                record = At<void*>(record, kRecordNext);
            }
        }
    }
    return 1;
}

// --- The control's third interface ------------------------------------------------------

uint8_t __fastcall QueryInterfaceThird(char* self, void*, uint32_t id, void** object)
{
    return This<uint8_t>(addr::SC4ViewInputControl_QueryInterfaceSelfTarget, self - 0x2C, id, object);
}

uint32_t __fastcall AddRefThird(char* self, void*)
{
    return This<uint32_t>(addr::SC4ViewInputControl_AddRef, self - 0x2C);
}

uint32_t __fastcall ReleaseThird(char* self, void*)
{
    return This<uint32_t>(addr::SC4ViewInputControl_Release, self - 0x2C);
}

// --- The overlay manager ----------------------------------------------------------------

// The game's 0x005658D0: sets 0x400 in the marker of a byte key. (byte key,
// unused), ret 8.
uint8_t __fastcall FlagKeyedMarker(char* self, void*, uint8_t key, uint32_t)
{
    void* const map = At<void*>(self, 0x48);
    void* found = map;
    uint8_t probe = key;
    This<void>(addr::SC4UDriveMarker_FindKey, self + 0x48, &found, &probe);
    if (found == map)
        return 0;
    At<uint32_t>(found, 0x110) |= 0x400;
    return 1;
}

// --- Registration ------------------------------------------------------------------------

bool InstallUDrive4(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[13] = {};
    static const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::SC4UDriveMarker_Construct, reinterpret_cast<void*>(&Construct)},
        {addr::SC4UDriveMarker_Init, reinterpret_cast<void*>(&Init)},
        {addr::SC4UDriveMarker_Shutdown, reinterpret_cast<void*>(&Shutdown)},
        {addr::SC4UDriveMarker_BuildQuad, reinterpret_cast<void*>(&BuildQuad)},
        {addr::SC4UDriveMarker_ChooseModel, reinterpret_cast<void*>(&ChooseModel)},
        {addr::SC4UDriveMarker_DrawCell, reinterpret_cast<void*>(&DrawCell)},
        {addr::SC4UDriveMarker_Draw, reinterpret_cast<void*>(&Draw)},
        {addr::SC4UDriveMarker_DeletingDestructor, reinterpret_cast<void*>(&DeletingDestruct)},
        {addr::SC4UDriveMarker_DeletingDestructorAdjustor, reinterpret_cast<void*>(&DeletingDestructorAdjustor)},
        {addr::cSC4UDriveControl_QueryInterfaceThird, reinterpret_cast<void*>(&QueryInterfaceThird)},
        {addr::cSC4UDriveControl_AddRefThird, reinterpret_cast<void*>(&AddRefThird)},
        {addr::cSC4UDriveControl_ReleaseThird, reinterpret_cast<void*>(&ReleaseThird)},
        {addr::SC4OverlayManager_F5658D0, reinterpret_cast<void*>(&FlagKeyedMarker)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    static_assert(count <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < count; ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].address);
        if (!found || !hook::Install(*found, hooks[i].ours, &originals[i]))
            return false;
    }
    return true;
}

const bool g_registered = registry::Register({
    "ui.tools.udrive4",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UDriveMarker_Draw,
    "Our U-Drive-It marker's drawing, the control's third interface and the overlay manager's keyed flag (docs/tools.md)",
    false,
    InstallUDrive4,
});

} // namespace
} // namespace opensc4::tool_udrive_4
