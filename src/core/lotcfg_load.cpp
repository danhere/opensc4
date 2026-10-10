// Our cSC4LotConfiguration (build 641; the class map is in lotcfg_sim.h),
// the exemplar part: Load (slot 31) with the objects read from the
// exemplar's properties, Save (slot 32) with every property written back.
//
// The exemplar properties, in the order Load reads them:
//   0x00000020 string         the name (+0xC)
//   0x88EDC789 uint8          the version; Load fails below 1 and saves the
//                             configuration again when it is not 2
//   0x88EDC790 uint8 array    width and depth in cells (+0x20, +0x21)
//   0x88EDC791 uint32 array   the family IDs (+0x24)
//   0x88EDC792 float          the maximum slope before a foundation (+0x38)
//   0xE99B068C float          the maximum slope (+0x30)
//   0x699B08A4 float          the minimum slope (+0x34)
//   0x27812837 uint8          the growth stage (+0x3C)
//   0x89136440 uint8          compatible with water (+0x42: 1 when present
//                             and not 0)
//   0x88EDC793 uint8 array    the zone types whose bits are set (+0x44)
//   0x88EDC795 uint8 array    the wealth types (+0x58)
//   0x88EDC796 uint8 array    the purpose types (+0x6C)
//   0xE99B068D uint8          do construction (+0x8D, as 0x89136440)
//   0x88EDC798 uint32 array   the retaining wall types (+0x9C)
//   0x88EDC797 uint32 array   the fences: a count, that many types (+0xA8),
//                             a count, that many times four values whose
//                             low bytes are a segment's position (+0xB4)
//   0x4A4A88F0 uint8          the required roads (+0x8E); bit 8 is forced
//   0x88EDC900 and following  uint32 arrays, one object each, up to the
//                             first missing one (below 0x88EDCE00): kind,
//                             flags, orientation (bit 31: mirrored), x, y,
//                             z, least x, least z, greatest x, greatest z
//                             (16.16 fixed point), +0x20, the object's ID
//                             (from version 2), then its type or family IDs
//   0x88FCD877 uint32         a foundation type: one more object of kind 3
// Save writes the same, with 0x00000010 (the exemplar type) = 0x10 first
// and version 2; the foundation object goes back into 0x88FCD877.
//
// Every virtual call below has its argument count from the callee's own
// `ret N`: the resource manager 0x00AD89D0 (slots 3: five, 13: three, 32:
// two), the exemplar 0x00AAF0D0 (slots 1, 2, 7: none, 12: one), its
// property holder part 0x00AAF038 (slots 0: two, 2: none, 18: one), its
// resource part 0x00AAF09C (slot 2), the configuration 0x00A8CEB8 (slots 3,
// 32). Only the COM object's slot 7 (CreateGuid, one argument) is taken
// from gzcom-dll's cIGZCOM and from the stack offsets the game uses after
// the call (0x00479E48): its class was not pinned down.
#include "core/lotcfg_sim.h"

#include "core/game.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::lotcfg_sim {
namespace {

constexpr uint32_t kPropertyExemplarType = 0x00000010;
constexpr uint32_t kPropertyName = 0x00000020;
constexpr uint32_t kPropertyVersion = 0x88EDC789;
constexpr uint32_t kPropertySize = 0x88EDC790;
constexpr uint32_t kPropertyFamilies = 0x88EDC791;
constexpr uint32_t kPropertyFoundationSlope = 0x88EDC792;
constexpr uint32_t kPropertyZoneTypes = 0x88EDC793;
constexpr uint32_t kPropertyWealthTypes = 0x88EDC795;
constexpr uint32_t kPropertyPurposeTypes = 0x88EDC796;
constexpr uint32_t kPropertyFences = 0x88EDC797;
constexpr uint32_t kPropertyRetainingWalls = 0x88EDC798;
constexpr uint32_t kPropertyFirstObject = 0x88EDC900;
constexpr uint32_t kPropertyObjectsEnd = 0x88EDCE00;
constexpr uint32_t kPropertyMaxSlope = 0xE99B068C;
constexpr uint32_t kPropertyMinSlope = 0x699B08A4;
constexpr uint32_t kPropertyGrowthStage = 0x27812837;
constexpr uint32_t kPropertyWater = 0x89136440;
constexpr uint32_t kPropertyDoConstruction = 0xE99B068D;
constexpr uint32_t kPropertyRoads = 0x4A4A88F0;
constexpr uint32_t kPropertyFoundation = 0x88FCD877;

using GetFn = uint8_t(__cdecl*)(void* holder, uint32_t property, void* out);
using SetFn = void(__cdecl*)(void* holder, uint32_t property, uint32_t value);
using SetPointerFn = void(__cdecl*)(void* holder, uint32_t property, const void* value);
using PtrInsertFn = void(__thiscall*)(void* vector, void* where, const void* value, void* tag, uint32_t count,
                                      uint32_t atEnd);
using InsertNFn = void(__thiscall*)(void* vector, void* where, uint32_t count, const void* value);
using SprintfFn = void(__cdecl*)(GameString* string, const char* format, ...);

const auto GetString = reinterpret_cast<GetFn>(addr::SC4_GetPropertyString);
const auto GetByte = reinterpret_cast<GetFn>(addr::SC4_GetPropertyUint8);
const auto GetUint = reinterpret_cast<GetFn>(addr::SC4_GetPropertyUint32);
const auto GetFloat = reinterpret_cast<GetFn>(addr::SC4_GetPropertyFloat);
const auto GetBytes = reinterpret_cast<GetFn>(addr::SC4_GetPropertyUint8Vector);
const auto GetUints = reinterpret_cast<GetFn>(addr::SC4_GetPropertyUint32Vector);
const auto SetByte = reinterpret_cast<SetFn>(addr::SC4_SetPropertyUint8);
const auto SetUint = reinterpret_cast<SetFn>(addr::SC4_SetPropertyUint32);
// The float's bits are pushed as the game pushes them.
const auto SetFloatBits = reinterpret_cast<SetFn>(addr::SC4_SetPropertyFloat);
const auto SetString = reinterpret_cast<SetPointerFn>(addr::SC4_SetPropertyString);
const auto SetBytes = reinterpret_cast<SetPointerFn>(addr::SC4_SetPropertyUint8Vector);
const auto SetUints = reinterpret_cast<SetPointerFn>(addr::SC4_SetPropertyUint32Vector);
const auto PtrInsertN = reinterpret_cast<PtrInsertFn>(addr::SC4PtrVector_InsertN);
const auto ByteGrow = reinterpret_cast<PtrInsertFn>(addr::SC4ByteVector_Grow);
const auto UintInsertN = reinterpret_cast<InsertNFn>(addr::SC4Uint32Vector_InsertN);
const auto ByteInsertN = reinterpret_cast<InsertNFn>(addr::SC4ByteVector_InsertN);

// __ftol (0x009EEF04): truncation; the low dword of its int64.
int32_t Ftol(double value) { return static_cast<int32_t>(static_cast<int64_t>(value)); }

ObjectVector* Objects(char* self) { return reinterpret_cast<ObjectVector*>(self + field::kObjects); }

// push_back of a uint32 vector as the game's inline code where it grows
// through the shared insert (0x0051CA60).
void PushBackShared(Vector32* vector, const uint32_t* value)
{
    if (vector->end != vector->capacity) {
        if (vector->end)
            *vector->end = *value;
        ++vector->end;
        return;
    }
    uint8_t tag = 0;
    PtrInsertN(vector, vector->end, value, &tag, 1u, 1u);
}

// push_back as the game's fully inline code: twice the size (1 for an empty
// vector) from the STL allocator, the values moved, the old storage freed
// after.
void PushBackInline(Vector32* vector, uint32_t value)
{
    if (vector->end != vector->capacity) {
        if (vector->end)
            *vector->end = value;
        ++vector->end;
        return;
    }
    const uint32_t size = static_cast<uint32_t>(vector->end - vector->begin);
    const uint32_t capacity = size + (size < 1 ? 1 : size);
    uint32_t* const grown = capacity ? static_cast<uint32_t*>(Allocate(capacity * 4)) : nullptr;
    uint32_t* at = grown;
    if (vector->end != vector->begin) {
        std::memmove(grown, vector->begin, size * 4);
        at = grown + size;
    }
    *at = value;
    if (vector->begin)
        Free(vector->begin);
    vector->begin = grown;
    vector->end = at + 1;
    vector->capacity = grown + capacity;
}

// --- Loading -----------------------------------------------------------------------

// 0x00479D10: the object vector emptied and filled from the properties
// 0x88EDC900 and following, up to the first missing one. A property of
// fewer than 10 values is skipped. An exemplar older than version 2 has no
// object IDs: its objects get the values of a counter (0x00B2166C, seeded
// from the COM object's CreateGuid when it is 0). An object that is no
// building is left out when its detail bits (flags & 0x30) are not below
// detail << 4. As in the game, the 11th and 12th values are read without a
// test of the count, and a count below the header's size is not caught.
void __cdecl LoadObjects(void* holder, ObjectVector* objects, uint32_t versionSlot, uint32_t detailSlot)
{
    static bool logged = false;
    FirstCall("LoadObjects", logged);
    const uint32_t threshold = static_cast<uint32_t>(static_cast<uint8_t>(detailSlot)) << 4;
    EraseObjects(objects, nullptr, objects->begin, objects->end);
    const bool old = static_cast<uint8_t>(versionSlot) < 2;
    const double scale = Global<double>(addr::cSC4LotConfiguration_DoubleFixedPointInverse);
    Vector32 values{};
    for (uint32_t property = kPropertyFirstObject; property < kPropertyObjectsEnd; ++property) {
        if (!GetUints(holder, property, &values))
            break;
        if (static_cast<uint32_t>(values.end - values.begin) < 10)
            continue;
        const uint32_t* const v = values.begin;
        alignas(4) char made[kObjectSize] = {};
        At<uint8_t>(made, object::kKind) = static_cast<uint8_t>(v[0]);
        At<uint8_t>(made, object::kFlags) = static_cast<uint8_t>(v[1]);
        At<uint8_t>(made, object::kOrientation) = static_cast<uint8_t>(v[2]);
        At<uint8_t>(made, object::kMirrored) = static_cast<uint8_t>((v[2] >> 31) & 1);
        const auto Fixed = [scale](uint32_t value) {
            return static_cast<float>(static_cast<double>(static_cast<int32_t>(value)) * scale);
        };
        At<float>(made, object::kX) = Fixed(v[3]);
        At<float>(made, object::kY) = Fixed(v[4]);
        At<float>(made, object::kZ) = Fixed(v[5]);
        At<uint32_t>(made, object::kFoundation) = v[10];
        At<float>(made, object::kMinX) = Fixed(v[6]);
        At<float>(made, object::kMinZ) = Fixed(v[7]);
        At<float>(made, object::kMaxX) = Fixed(v[8]);
        At<float>(made, object::kMaxZ) = Fixed(v[9]);
        uint32_t header;
        uint32_t id;
        if (old) {
            header = 0xB;
            uint32_t& next = Global<uint32_t>(addr::cSC4LotConfiguration_NextObjectId);
            if (next == 0) {
                void* const com = reinterpret_cast<void*(__cdecl*)()>(addr::RZGetCOM)();
                Virtual<uint8_t>(com, 0x1C, &next);
            }
            id = ++next;
        } else {
            id = v[11];
            header = 0xC;
        }
        At<uint32_t>(made, object::kId) = id;
        Vector32* const ids = reinterpret_cast<Vector32*>(made + object::kIds);
        uint32_t zero = 0;
        UintInsertN(ids, nullptr, static_cast<uint32_t>(values.end - values.begin) - header, &zero);
        const uint32_t* from = values.begin + header;
        uint32_t* to = ids->begin;
        for (int32_t left = static_cast<int32_t>(values.end - from); left > 0; --left)
            *to++ = *from++;
        if (At<uint8_t>(made, object::kKind) == 0 || (At<uint8_t>(made, object::kFlags) & 0x30u) < threshold)
            PushBackObject(objects, made);
        if (ids->begin)
            Free(ids->begin);
    }
    if (values.begin)
        Free(values.begin);
}

// Sets the bits a byte array property names (indexes at or past the
// vector's size are skipped).
void LoadBits(void* holder, uint32_t property, ByteVector* bytes, BitVector* bits)
{
    if (!GetBytes(holder, property, bytes))
        return;
    const uint32_t size = BitCount(bits);
    const int32_t count = static_cast<int32_t>(bytes->end - bytes->begin);
    for (int32_t i = 0; i < count; ++i) {
        const uint32_t index = bytes->begin[i];
        if (index >= size)
            continue;
        uint32_t bit = 0;
        uint32_t* const word = BitWord(bits, index, bit);
        *word |= 1u << bit;
    }
}

} // namespace

// Slot 31 (0x00479F70). False without the exemplar (nothing touched) or
// for a version below 1 (the configuration reset, with the name and the ID).
// The order of the allocations is the game's: 8 bytes of an unused string
// right after the exemplar is found, freed last.
uint8_t __fastcall Load(char* self, void*, const uint32_t* key, uint32_t detail)
{
    static bool logged = false;
    FirstCall("Load", logged);
    void* resource = nullptr;
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, key, kIidExemplar, &resource, 0u, 0u))
        return 0;
    void* const holder = Virtual<void*>(resource, 0x1C);
    char* const scratch = static_cast<char*>(Allocate(8));
    *scratch = 0;
    uint8_t result = 0;
    uint8_t flag = 0;
    uint8_t version = 0;
    uint32_t foundation = 0;
    ByteVector bytes{};
    Vector32 fence{};
    Reset(self);
    GetString(holder, kPropertyName, self + field::kName);
    At<uint32_t>(self, field::kId) = key[2];
    GetByte(holder, kPropertyVersion, &version);
    if (version >= 1) {
        const bool resave = version != 2;
        if (GetBytes(holder, kPropertySize, &bytes) && static_cast<uint32_t>(bytes.end - bytes.begin) >= 2) {
            At<uint8_t>(self, field::kWidth) = bytes.begin[0];
            At<uint8_t>(self, field::kDepth) = bytes.begin[1];
        }
        GetUints(holder, kPropertyFamilies, self + field::kFamilies);
        GetFloat(holder, kPropertyFoundationSlope, self + field::kFoundationSlope);
        GetFloat(holder, kPropertyMaxSlope, self + field::kMaxSlope);
        GetFloat(holder, kPropertyMinSlope, self + field::kMinSlope);
        GetByte(holder, kPropertyGrowthStage, self + field::kGrowthStage);
        At<uint8_t>(self, field::kWater) = (GetByte(holder, kPropertyWater, &flag) && flag) ? 1 : 0;
        LoadBits(holder, kPropertyZoneTypes, &bytes, reinterpret_cast<BitVector*>(self + field::kZoneTypes));
        LoadBits(holder, kPropertyWealthTypes, &bytes, reinterpret_cast<BitVector*>(self + field::kWealthTypes));
        LoadBits(holder, kPropertyPurposeTypes, &bytes, reinterpret_cast<BitVector*>(self + field::kPurposeTypes));
        At<uint8_t>(self, field::kDoConstruction) = (GetByte(holder, kPropertyDoConstruction, &flag) && flag) ? 1 : 0;
        GetUints(holder, kPropertyRetainingWalls, self + field::kRetainingWalls);
        if (GetUints(holder, kPropertyFences, &fence) && static_cast<uint32_t>(fence.end - fence.begin) >= 2) {
            // The type count is not tested against the array's size, as in
            // the game.
            uint32_t index = 2;
            Vector32* const types = reinterpret_cast<Vector32*>(self + field::kFences);
            for (uint32_t left = fence.begin[0]; left != 0; --left) {
                PushBackShared(types, fence.begin + index);
                ++index;
            }
            if (index < static_cast<uint32_t>(fence.end - fence.begin)) {
                uint32_t segments = fence.begin[index];
                ++index;
                Vector32* const positions = reinterpret_cast<Vector32*>(self + field::kFencePositions);
                for (; segments != 0; --segments) {
                    // Four values, the low byte of each.
                    const uint32_t packed = (fence.begin[index] & 0xFF) | ((fence.begin[index + 1] & 0xFF) << 8) |
                                            ((fence.begin[index + 2] & 0xFF) << 16) |
                                            ((fence.begin[index + 3] & 0xFF) << 24);
                    index += 4;
                    PushBackInline(positions, packed);
                }
            }
        }
        uint8_t& roads = At<uint8_t>(self, field::kRoads);
        GetByte(holder, kPropertyRoads, &roads);
        if (!(roads & 8)) {
            // A warning text is formatted and dropped; the bit is set.
            GameString text{};
            char* const buffer = static_cast<char*>(Allocate(8));
            text.capacity = buffer + 8;
            text.begin = buffer;
            text.end = buffer;
            *buffer = 0;
            text.vtable = addr::SC4String_Vtable;
            text.references = 0;
            const uint32_t id = Virtual<uint32_t>(self, 0xC);
            reinterpret_cast<SprintfFn>(addr::cRZBaseString_Sprintf)(
                &text, reinterpret_cast<const char*>(addr::cSC4LotConfiguration_RoadWarningFormat), id);
            roads = static_cast<uint8_t>(roads | 8);
            text.vtable = addr::SC4String_Vtable;
            if (text.begin)
                Free(text.begin);
        }
        ObjectVector* const objects = Objects(self);
        LoadObjects(holder, objects, version, detail);
        float rect[4] = {static_cast<float>(static_cast<int32_t>(uint32_t{At<uint8_t>(self, field::kWidth)} << 4)),
                         static_cast<float>(static_cast<int32_t>(uint32_t{At<uint8_t>(self, field::kDepth)} << 4)),
                         0.0f, 0.0f};
        ClampObjects(objects, rect);
        if (GetUint(holder, kPropertyFoundation, &foundation)) {
            // The foundation object. The game leaves its +0x24 (the
            // object's ID) as the stack had it; 0 here.
            alignas(4) char made[kObjectSize] = {};
            At<uint8_t>(made, object::kKind) = 3;
            At<uint32_t>(made, object::kX) = 0x3F000000;    // 0.5
            At<uint32_t>(made, object::kY) = 0;
            At<uint32_t>(made, object::kZ) = 0x3F000000;
            At<uint32_t>(made, object::kMinX) = 0x3ECCCCCD; // 0.4
            At<uint32_t>(made, object::kMinZ) = 0x3ECCCCCD;
            At<uint32_t>(made, object::kMaxX) = 0x3F19999A; // 0.6
            At<uint32_t>(made, object::kMaxZ) = 0x3F19999A;
            At<uint32_t>(made, object::kFoundation) = foundation;
            PushBackObject(objects, made);
            if (void* const ids = At<void*>(made, object::kIds))
                Free(ids);
        }
        // The vector shrunk to its size: a copy swapped in, the old one
        // destroyed.
        if (static_cast<int32_t>(objects->end - objects->begin) / static_cast<int32_t>(kObjectSize) !=
            static_cast<int32_t>(objects->capacity - objects->begin) / static_cast<int32_t>(kObjectSize)) {
            ObjectVector copy{};
            CopyObjectVector(&copy, nullptr, objects);
            const ObjectVector old = *objects;
            *objects = copy;
            copy = old;
            DestroyObjects(&copy, nullptr);
        }
        if (resave) {
            // The segment holding the key (the resource manager's slot 32
            // clears the pointer first; the reference it adds is never
            // released, as in the game), then Save through the vtable.
            void* segment = nullptr;
            Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0x80, key, &segment);
            Virtual<uint8_t>(self, 0x80, key, segment);
        }
        result = 1;
    }
    Virtual<uint32_t>(resource, 0x8);
    if (fence.begin)
        Free(fence.begin);
    if (bytes.begin)
        Free(bytes.begin);
    Free(scratch);
    return result;
}

namespace {

// --- Saving ------------------------------------------------------------------------

// 0x004783A0: every object but the foundations as a uint32 array property,
// 0x88EDC900 and following. The height of an object that is no prop, or of
// a prop below 0, is set to 0 in the object itself first.
void __cdecl WriteObjects(void* holder, ObjectVector* objects)
{
    static bool logged = false;
    FirstCall("WriteObjects", logged);
    int32_t count = ObjectCount(objects);
    if (count <= 0)
        return;
    const double scale = Global<double>(addr::cSC4LotConfiguration_DoubleFixedPoint);
    uint32_t property = kPropertyFirstObject;
    std::ptrdiff_t offset = 0;
    do {
        char* const at = objects->begin + offset;
        Vector32 values{};
        if (At<uint8_t>(at, object::kKind) != 3) {
            uint8_t tag = 0;
            uint32_t value = At<uint8_t>(at, object::kKind);
            PtrInsertN(&values, nullptr, &value, &tag, 1u, 1u);
            value = At<uint8_t>(at, object::kFlags);
            PushBackShared(&values, &value);
            value = At<uint8_t>(at, object::kOrientation);
            if (At<uint8_t>(at, object::kMirrored))
                value |= 0x80000000u;
            PushBackShared(&values, &value);
            if (At<uint8_t>(at, object::kKind) != 1 || At<float>(at, object::kY) < Global<float>(addr::SC4_FloatZero))
                At<uint32_t>(at, object::kY) = 0;
            for (const std::size_t part : {object::kX, object::kY, object::kZ, object::kMinX, object::kMinZ,
                                            object::kMaxX, object::kMaxZ}) {
                value = static_cast<uint32_t>(Ftol(static_cast<double>(At<float>(at, part)) * scale));
                PushBackShared(&values, &value);
            }
            PushBackShared(&values, &At<uint32_t>(at, object::kFoundation));
            PushBackShared(&values, &At<uint32_t>(at, object::kId));
            const Vector32* const ids = reinterpret_cast<const Vector32*>(at + object::kIds);
            const int32_t idCount = static_cast<int32_t>(ids->end - ids->begin);
            for (int32_t i = 0; i < idCount; ++i)
                PushBackInline(&values, ids->begin[i]);
            SetUints(holder, property, &values);
            ++property;
            if (values.begin)
                Free(values.begin);
        }
        offset += static_cast<std::ptrdiff_t>(kObjectSize);
    } while (--count != 0);
}

// push_back of a byte through the game's shared growth (0x00461A10).
void PushBackByteShared(ByteVector* bytes, uint8_t value)
{
    if (bytes->end != bytes->capacity) {
        if (bytes->end)
            *bytes->end = value;
        ++bytes->end;
        return;
    }
    uint8_t tag = 0;
    ByteGrow(bytes, bytes->end, &value, &tag, 1u, 1u);
}

// push_back of a byte as the game's fully inline code.
void PushBackByteInline(ByteVector* bytes, uint8_t value)
{
    if (bytes->end != bytes->capacity) {
        if (bytes->end)
            *bytes->end = value;
        ++bytes->end;
        return;
    }
    const uint32_t size = static_cast<uint32_t>(bytes->end - bytes->begin);
    const uint32_t capacity = size + (size < 1 ? 1 : size);
    uint8_t* const grown = capacity ? static_cast<uint8_t*>(Allocate(capacity)) : nullptr;
    uint8_t* at = grown;
    if (bytes->end != bytes->begin) {
        std::memmove(grown, bytes->begin, size);
        at = grown + size;
    }
    *at = value;
    if (bytes->begin)
        Free(bytes->begin);
    bytes->begin = grown;
    bytes->end = at + 1;
    bytes->capacity = grown + capacity;
}

// The indexes of a bit vector's set bits as a byte array property; nothing
// for an empty vector.
void WriteBits(void* holder, uint32_t property, ByteVector* bytes, const BitVector* bits, bool shared)
{
    if (bits->endWord == bits->beginWord && bits->endBit == bits->beginBit)
        return;
    bytes->end = bytes->begin;
    const uint32_t count = BitCount(bits);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t bit = 0;
        const uint32_t* const word = BitWord(bits, i, bit);
        if (!(*word & (1u << bit)))
            continue;
        if (shared)
            PushBackByteShared(bytes, static_cast<uint8_t>(i));
        else
            PushBackByteInline(bytes, static_cast<uint8_t>(i));
    }
    SetBytes(holder, property, bytes);
}

// 0x00478AC0: the holder emptied (its slot 18) and every property set.
// False for no holder.
uint8_t WriteToExemplar(char* self, void* holder)
{
    static bool logged = false;
    FirstCall("WriteToExemplar", logged);
    if (!holder)
        return 0;
    ByteVector bytes{};
    Vector32 values{};
    Virtual<void>(holder, 0x48, 0u);
    SetUint(holder, kPropertyExemplarType, 0x10);
    SetString(holder, kPropertyName, self + field::kName);
    SetByte(holder, kPropertyVersion, 2);
    const uint8_t zero = 0;
    const uint32_t size = static_cast<uint32_t>(bytes.end - bytes.begin);
    if (size > 2)
        bytes.end = bytes.begin + 2;
    else
        ByteInsertN(&bytes, bytes.end, 2 - size, &zero);
    bytes.begin[0] = At<uint8_t>(self, field::kWidth);
    bytes.begin[1] = At<uint8_t>(self, field::kDepth);
    SetBytes(holder, kPropertySize, &bytes);
    const Vector32* const families = reinterpret_cast<const Vector32*>(self + field::kFamilies);
    if (families->begin != families->end)
        SetUints(holder, kPropertyFamilies, families);
    SetFloatBits(holder, kPropertyFoundationSlope, At<uint32_t>(self, field::kFoundationSlope));
    SetFloatBits(holder, kPropertyMaxSlope, At<uint32_t>(self, field::kMaxSlope));
    SetFloatBits(holder, kPropertyMinSlope, At<uint32_t>(self, field::kMinSlope));
    SetByte(holder, kPropertyGrowthStage, At<uint8_t>(self, field::kGrowthStage));
    if (const uint8_t water = At<uint8_t>(self, field::kWater))
        SetByte(holder, kPropertyWater, water);
    WriteBits(holder, kPropertyZoneTypes, &bytes, reinterpret_cast<const BitVector*>(self + field::kZoneTypes), true);
    WriteBits(holder, kPropertyWealthTypes, &bytes, reinterpret_cast<const BitVector*>(self + field::kWealthTypes),
              true);
    WriteBits(holder, kPropertyPurposeTypes, &bytes, reinterpret_cast<const BitVector*>(self + field::kPurposeTypes),
              false);
    if (const uint8_t construct = At<uint8_t>(self, field::kDoConstruction))
        SetByte(holder, kPropertyDoConstruction, construct);
    // The first foundation with a type (+0x20) or an ID.
    ObjectVector* const objects = Objects(self);
    const uint32_t count = static_cast<uint32_t>(ObjectCount(objects));
    for (uint32_t i = 0; i < count; ++i) {
        char* const at = objects->begin + static_cast<std::size_t>(i) * kObjectSize;
        if (At<uint8_t>(at, object::kKind) != 3)
            continue;
        if (At<uint32_t>(at, object::kFoundation) != 0) {
            SetUint(holder, kPropertyFoundation, At<uint32_t>(at, object::kFoundation));
            break;
        }
        const Vector32* const ids = reinterpret_cast<const Vector32*>(at + object::kIds);
        if (ids->begin != ids->end) {
            SetUint(holder, kPropertyFoundation, ids->begin[0]);
            break;
        }
    }
    const Vector32* const walls = reinterpret_cast<const Vector32*>(self + field::kRetainingWalls);
    if (walls->begin != walls->end)
        SetUints(holder, kPropertyRetainingWalls, walls);
    const Vector32* const fences = reinterpret_cast<const Vector32*>(self + field::kFences);
    if (fences->begin != fences->end) {
        values.end = values.begin;
        PushBackInline(&values, static_cast<uint32_t>(fences->end - fences->begin));
        const uint32_t fenceCount = static_cast<uint32_t>(fences->end - fences->begin);
        for (uint32_t i = 0; i < fenceCount; ++i)
            PushBackInline(&values, fences->begin[i]);
        const Vector32* const positions = reinterpret_cast<const Vector32*>(self + field::kFencePositions);
        PushBackInline(&values, static_cast<uint32_t>(positions->end - positions->begin));
        const uint32_t positionCount = static_cast<uint32_t>(positions->end - positions->begin);
        for (uint32_t i = 0; i < positionCount; ++i) {
            const int8_t* const position =
                reinterpret_cast<const int8_t*>(At<const char*>(self, field::kFencePositions)) + i * 4;
            for (int part = 0; part < 4; ++part)
                PushBackInline(&values, static_cast<uint32_t>(static_cast<int32_t>(position[part])));
        }
        SetUints(holder, kPropertyFences, &values);
    }
    SetByte(holder, kPropertyRoads, At<uint8_t>(self, field::kRoads));
    WriteObjects(holder, objects);
    if (values.begin)
        Free(values.begin);
    if (bytes.begin)
        Free(bytes.begin);
    return 1;
}

} // namespace

// Slot 32 (0x00479580). False without the resource manager, when the
// holder has no resource interface or when the manager cannot store the
// exemplar (its slot 13).
uint8_t __fastcall Save(char* self, void*, const uint32_t* key, void* segment)
{
    static bool logged = false;
    FirstCall("Save", logged);
    void* const resources = Global<void*>(addr::SC4_ResourceManager);
    uint8_t saved = 0;
    if (!resources)
        return 0;
    void* existing = nullptr;
    void* holder = nullptr;
    if (Virtual<uint8_t>(resources, 0xC, key, kIidExemplar, &existing, 0u, 0u)) {
        holder = Virtual<void*>(existing, 0x1C);
    } else {
        // A new exemplar under the key. The game does not test the
        // allocation.
        void* exemplar = reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0x40);
        if (exemplar)
            exemplar = reinterpret_cast<void*(__thiscall*)(void*)>(addr::cSCResExemplar_cSCResExemplar)(exemplar);
        Virtual<uint32_t>(exemplar, 0x4);
        Virtual<uint8_t>(exemplar, 0x30, key);
        holder = Virtual<void*>(exemplar, 0x1C);
    }
    if (WriteToExemplar(self, holder)) {
        // The game uses the key argument's stack slot for the interface.
        void* resource = const_cast<uint32_t*>(key);
        if (Virtual<uint8_t>(holder, 0x0, kIidResource, &resource)) {
            saved = Virtual<uint8_t>(resources, 0x34, resource, key, segment);
            Virtual<uint32_t>(resource, 0x8);
        }
    }
    // The exemplar the manager gave, or the new one through its holder part.
    if (existing)
        Virtual<uint32_t>(existing, 0x8);
    else
        Virtual<uint32_t>(holder, 0x8);
    return saved;
}

namespace {

const Slot kLoadSlots[] = {
    {31, addr::cSC4LotConfiguration_Load, reinterpret_cast<void*>(&Load)},
    {32, addr::cSC4LotConfiguration_Save, reinterpret_cast<void*>(&Save)},
};

// The two functions other game code calls directly (0x0060F816 and
// 0x0060EBF1): cdecl with the caller cleaning the stack, nothing kept in
// ECX, EDX or EAX across the calls (the imul after 0x0060F816 sets EDX
// itself): plain detours.
Detour g_loadDetours[] = {
    {addr::cSC4LotConfiguration_LoadObjects, reinterpret_cast<void*>(&LoadObjects), nullptr},
    {addr::cSC4LotConfiguration_WriteObjects, reinterpret_cast<void*>(&WriteObjects), nullptr},
};

bool InstallLoad(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    const auto* const vtable = reinterpret_cast<const std::uintptr_t*>(addr::cSC4LotConfiguration_vftable);
    for (const Slot& slot : kLoadSlots) {
        if (vtable[slot.index] != slot.original) {
            log::Write("lotcfg load: slot %d holds %08X, expected %08X; not installing", slot.index,
                       static_cast<unsigned>(vtable[slot.index]), static_cast<unsigned>(slot.original));
            return false;
        }
    }
    if (!InstallDetours(g_loadDetours, sizeof(g_loadDetours) / sizeof(g_loadDetours[0]), "load"))
        return false;
    return SwapSlots(kLoadSlots, sizeof(kLoadSlots) / sizeof(kLoadSlots[0]), "load");
}

const bool g_registered = registry::Register({
    "simulators.lotcfg.load",
    "simulators/lotcfg",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4LotConfiguration_vftable,
    "Our lot configuration Load and Save (slots 31 and 32) with the objects read from and written to an exemplar's "
    "properties; detours of the two functions called directly (src/core/lotcfg_load.cpp)",
    false,
    InstallLoad,
});

} // namespace
} // namespace opensc4::lotcfg_sim
