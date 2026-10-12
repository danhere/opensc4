// Our cSTETerrain table loaders (build 641), piece 8 of the city's terrain
// object: two of the three loaders the terrain's one-time set-up 0x0074FE40
// runs besides the brush loader (the third, the INI resource, is in
// terrain_tables_ini.cpp).
//
//   0x0074F410  LoadTextureExemplars  lists the texture exemplars, reads each
//   0x0074F180  LoadTextureExemplar   one exemplar into its record of the map
//   0x0074B6A0  LoadTuning            the tuning exemplar into globals
//   0x0074B460  FillNoiseTables       five tables of random words
//
// Where they run. The terrain's Init (slot 3) calls 0x0074FE40 while a city
// loads; that function does its work once (the byte 0x00B4BC3C) and calls,
// in this order, 0x0074F410, 0x0074DC30 (the INI resource), 0x0074B6A0 and
// 0x0074FDC0 (the brushes). Detours installed at PostAppInit are in place
// before the first city loads.
//
// --- The texture exemplars (0x0074F410, 0x0074F180) ---------------------------
//
// Every exemplar of type 0x6534284A, group 0x89C2A517 the terrain's resource
// manager (0x00B4BC40) lists (its slot 17 with a key filter; the list's slot
// 7 calls LoadTextureExemplar per key, in the list's order). Nothing at all
// without the resource manager.
//
// Destinations:
//   0x00B4BC44  uint32     the greatest instance listed, plus 1 (zeroed first)
//   0x00B4BC48  uint32[5]  per curve the greatest "reach" of all records
//                          (never reset: only ever grows)
//   0x00B4BCDC  hash map   instance -> record of 0x7C bytes (never emptied
//                          by these loaders: a second run finds the records
//                          and reads into them again)
// A record:
//   +0x00  float      property 0x09C2AEF0 (left as it is without it)
//   +0x04  curve[5]   0x10 bytes each: property 0x09C2AEF3 + i, an array of
//                     (x, y) float pairs, through SC4Curve::SetPoints
//   +0x54  uint32[5]  the "reach": ceil(last x / 16) as a whole number
//   +0x68  float[5]   the last pair's x
// (+0x54 and +0x68 of a curve whose property is missing are not written.)
// The game builds a new record on its stack and copies it into the map; of
// that stack record it initialises only the curves, so +0x00, +0x54 and
// +0x68 of a new record hold stack memory until a property overwrites them.
// Ours has zeros there. With the game's own data every exemplar has all six
// properties, so nothing of it is left.
//
// --- The tuning exemplar (0x0074B6A0, 0x0074B460) ----------------------------
//
// The exemplar of type 0x6534284A, group 0x88CD66E9, instance 1 from the
// application's resource manager (0x00B43CA8; not the terrain's copy).
// Nothing without it. A getter leaves its destination alone when the
// property is missing, so the exe's values (in brackets) stay. In this
// order:
//   0x89A312C2  uint32[]  vector 0x00B4BC90 [empty]; then FillNoiseTables
//   0x48CD7B20  float[]   vector 0x00B4BCF8 [empty]
//   0x48CD7B26  float     0x00B0D5DC [200]
//   0x48CD7B27  float     0x00B0D5E0 [5]
//   0x48CD7B21  float     0x00B4B404 [0]; raised to 0x00B0D5E0 when below
//   0x48CD7B2A  float     0x00B0D5E4 [50]; raised to 0x00B0D5E0 when below
//   0x48CD7B22 to 25  float[]  vectors 0x00B4BD04, 10, 1C, 28 [empty]
//   0x48CD7B28  float     0x00B0D5E8 [1.5]
//   0x48CD7B30  float     0x00B4B400 [0]
//   0x48CD7B31  float     0x00B4B3FC [0]
//   0x48CD7B40 to 42  uint32  0x00B4B3F8, 0x00B4B3F4, 0x00B4B3F0 [0]
//   0x48CD7B43  float[]   vector 0x00B4BD34 [empty]
//   0x48CD7B50  uint32    0x00B0D5F0 [0x191]
//   0x48CD7B51  float     0x00B0D5F4 [0.1]
//   0x48CD7B52  float     0x00B0D5FC [0.6]
//   0x48CD7B53  float     0x00B0D5F8 [0.1]
//   0x48CD7B54  uint32    0x00B0D604 [4]
//   0x69AC5640  float     0x00B0D5D4 [15]
//   0x69AC5641  float     0x00B0D5D8 [30]
//   0x48CD7B60  uint32    0x00B0D60C [4]
//   0x48CD7B64  uint32    0x00B0D610 [0x46]
//   0x48CD7B65  float     0x00B0D620 [0.3]
//   0x48CD7B61  float     0x00B0D614 [15]; then times 6 (the float
//                         0x00A90900), also when the property is missing
//   0x48CD7B62  float     0x00B0D618 [40.1]
//   0x48CD7B63  float     0x00B0D61C [50.1]
//   0x48CD7B70 to 73  float  0x00B0D57C [-10.1], 0x00B0D580 [200.1],
//                         0x00B0D584 [20], 0x00B0D588 [20]
//   0xA9C283D0  uint8     0x00B0D58C [4]
//   0xA9C283D1  float     0x00B0D590 [7]
//   0x48CD7B80  uint8     into a byte of the stack; 0x00B4BC5D [0] = whether
//                         that byte is not 0
//   0x48CD7B81  uint8     into the same byte; 0x00B0D608 [1] = whether it is
//                         not 0
//   0x48CD7BA0  float[]   vector 0x00B4BD40 [empty]; its first five values
//                         each times the double 0x00A9E108 (pi / 180)
//   0x89861864  uint32    0x00B0D624 [10000000]; 0x00B0D628 = that / 20
//   0x48CD7BF1  uint32[]  vector 0x00B4BD4C [empty]
//   0x48CD7BF3  uint32    0x00B0D5EC [1]
//   0x48CD7C00  curve     0x00B4BD64 (SC4Curve::LoadFromProperty; untouched
//                         without the property)
//   0x48CD7C01  uint32    0x00B0D67C [5]
//   0x48CD7C02  float     0x00B0D680 [1.2]
// The byte of the stack is never initialised by the game: without property
// 0x48CD7B80 both flags come from stack memory. Ours starts it at 0.
// The game reads five values of the vectors 0x00B4BC90 and 0x00B4BD40
// whatever their length (a missing property is a read at address 0, a step
// of 0 a division by zero); so does this.
//
// Random numbers (FillNoiseTables): for k = 0 to 4 the side is 0x101 / step k
// + 1 (0x00B4BC20 + 4 k) and the uint16 vector 0x00B4BC9C + 0xC k gets side
// x side elements, each cRZRandom::Between(0, 0xFFFF) (0x009136CA) of the
// terrain's generator 0x00B4BC78, row by row. With the game's steps (32, 16,
// 8, 4, 2) the sides are 9, 17, 33, 65 and 129: 22,325 draws.
//
// Still the game's and called at their addresses: the record's copy and the
// two destructors, the map's growth and insert (0x00748E90, 0x00748920,
// 0x007488F0, 0x004F0940, 0x0074BFB0), the curve functions, the vector
// inserts, the property getters, the key filter, ceil, operator new and
// every virtual call.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/terrain_sim.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::terrain_sim {

// --- Additions for terrain_sim.h (to be merged there) --------------------------

// A texture record (0x7C bytes; the value of a node of the hash map
// 0x00B4BCDC, at node + 8, the key at node + 4, the next node at node + 0).
namespace texture_record {
constexpr std::size_t kFloat = 0x0;   // float: property 0x09C2AEF0
constexpr std::size_t kCurves = 0x4;  // SC4Curve[5]: properties 0x09C2AEF3 to 0x09C2AEF7
constexpr std::size_t kReach = 0x54;  // uint32[5]: ceil(last x / 16)
constexpr std::size_t kLastX = 0x68;  // float[5]: the last pair's x
constexpr std::size_t kSize = 0x7C;
} // namespace texture_record

// The hash map 0x00B4BCDC (and any of the game's hash tables of this kind).
namespace hash_map {
constexpr std::size_t kBuckets = 0x4;    // vector of node pointers: begin
constexpr std::size_t kBucketsEnd = 0x8; // its end
constexpr std::size_t kCount = 0x10;     // uint32: elements
} // namespace hash_map

namespace {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("terrain: first %s", name);
}

template <typename R, typename... Args>
inline R This(std::uintptr_t function, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(function)(self, args...);
}

template <typename R, typename... Args>
inline R Cdecl(std::uintptr_t function, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(function)(args...);
}

// A std::vector of the game (begin, end, capacity).
struct GameVector {
    uint32_t start, finish, capacity;
};

// __ftol2 (0x009EEF04): truncation toward zero to 64 bits; the x87 "integer
// indefinite" for what does not fit and for a NaN. The caller takes the low
// dword.
int64_t Ftol64(double value)
{
    if (!(value > -9.2233720368547758e18 && value < 9.2233720368547758e18))
        return INT64_MIN;
    return static_cast<int64_t>(value);
}

inline bool GetFloat(void* exemplar, uint32_t property, std::uintptr_t out)
{
    return (Cdecl<uint32_t>(addr::SC4_GetPropertyFloat, exemplar, property, reinterpret_cast<void*>(out)) & 0xFFu) != 0;
}

inline bool GetUint32(void* exemplar, uint32_t property, std::uintptr_t out)
{
    return (Cdecl<uint32_t>(addr::SC4_GetPropertyUint32, exemplar, property, reinterpret_cast<void*>(out)) & 0xFFu) != 0;
}

inline bool GetUint8(void* exemplar, uint32_t property, void* out)
{
    return (Cdecl<uint32_t>(addr::SC4_GetPropertyUint8, exemplar, property, out) & 0xFFu) != 0;
}

inline bool GetFloats(void* exemplar, uint32_t property, std::uintptr_t out)
{
    return (Cdecl<uint32_t>(addr::SC4_GetPropertyFloatVector, exemplar, property, reinterpret_cast<void*>(out)) & 0xFFu) != 0;
}

inline bool GetUint32s(void* exemplar, uint32_t property, std::uintptr_t out)
{
    return (Cdecl<uint32_t>(addr::SC4_GetPropertyUint32Vector, exemplar, property, reinterpret_cast<void*>(out)) & 0xFFu) !=
           0;
}

// --- The texture exemplars ---------------------------------------------------

// 0x0074F180 (key, context; cdecl, the key list's callback). The key is
// (type, group, instance).
//
// The greatest instance so far goes to 0x00B4BC44. The instance's record is
// looked up in the map (the bucket is instance mod the bucket count; a map
// without buckets is a division by zero in the game and here); when it is
// not there, a record with five empty curves is copied into a new node (the
// game's copy, growth, insert and destructors, in the game's order). Then
// the exemplar (the terrain's resource manager's slot 3, interface
// 0xA52160F5; nothing more without it): the float, and per curve the
// property's values (the variant's slot 111 is the float array, slot 4 the
// count): reach = ceil(float 0x00B0D688 (1 / 16) x the next to last value),
// taken as a whole number (__ftol2's low dword), into the record and, when
// greater (unsigned), into 0x00B4BC48 + 4 i; count / 2 points pushed onto a
// vector; the curve set from it; the next to last value into +0x68 + 4 i;
// the vector freed. The exemplar's resource is released.
//
// A property with fewer than two values makes the game read before the
// array (count x 4 - 8 from its start); so does this.
void __cdecl LoadTextureExemplar(uint32_t* key, void*)
{
    const uint32_t instance = key[2];
    if (instance > Global<uint32_t>(addr::cSTETerrain_TextureIDLimit))
        Global<uint32_t>(addr::cSTETerrain_TextureIDLimit) = instance;

    char* const map = reinterpret_cast<char*>(addr::cSTETerrain_TextureRecords);
    const uint32_t buckets = At<uint32_t>(map, hash_map::kBuckets);
    const uint32_t bucketCount =
        static_cast<uint32_t>(static_cast<int32_t>(At<uint32_t>(map, hash_map::kBucketsEnd) - buckets) >> 2);
    uint32_t record = 0;
    for (uint32_t node = AtAddress<uint32_t>(buckets + 4u * (instance % bucketCount)); node; node = AtAddress<uint32_t>(node)) {
        if (AtAddress<uint32_t>(node + 4u) == instance) {
            record = node + 8u;
            break;
        }
    }
    if (!record) {
        // The game's stack record: only the curves are initialised; the
        // rest is zero here where the game has stack memory.
        uint8_t fresh[texture_record::kSize] = {};
        for (uint32_t i = 0; i < 5; ++i)
            This<void*>(addr::SC4Curve_Init, static_cast<void*>(fresh + texture_record::kCurves + 0x10u * i));
        struct {
            uint32_t key;
            uint8_t record[texture_record::kSize];
        } pair{};
        pair.key = instance;
        This<void*>(addr::cSTETerrain_TextureRecordCopy, static_cast<void*>(pair.record), static_cast<void*>(fresh));
        This<void>(addr::SC4Uint32HashSet_Resize, static_cast<void*>(map), At<uint32_t>(map, hash_map::kCount) + 1u);
        struct {
            uint32_t node;
            uint32_t map;
            uint32_t inserted; // the game's writes a byte
        } result{0, 0, 0};
        This<void>(addr::cSTETerrain_TextureMapInsert, static_cast<void*>(map), static_cast<void*>(&result),
                   static_cast<void*>(&pair));
        This<void>(addr::cSTETerrain_TexturePairDestruct, static_cast<void*>(&pair));
        This<void>(addr::cSTETerrain_TextureRecordDestruct, static_cast<void*>(fresh));
        record = result.node + 8u;
    }

    void* resource = nullptr;
    if (!Virtual<uint8_t>(Global<void*>(addr::cSTETerrain_ResourceManager), 0xC, static_cast<void*>(key), 0xA52160F5u, &resource,
                          0u, 0u))
        return;
    void* const exemplar = Virtual<void*>(resource, 0x1C);
    GetFloat(exemplar, 0x09C2AEF0u, record + texture_record::kFloat);
    for (uint32_t i = 0; i < 5; ++i) {
        void* const property = Virtual<void*>(exemplar, 0x24, 0x09C2AEF3u + i);
        if (!property)
            continue;
        void* const variant = Virtual<void*>(property, 0x14);
        const uint32_t values = Virtual<uint32_t>(variant, 0x1BC);
        GameVector points{0, 0, 0};
        const int32_t count = Virtual<int32_t>(variant, 0x10);
        const uint32_t last = values + static_cast<uint32_t>(count) * 4u - 8u;
        // FLD float, FMUL float, FSTP double: the product at 53 bits.
        const double scaled =
            static_cast<double>(Global<float>(addr::cSTETerrain_InverseCellWidth)) * static_cast<double>(AtAddress<float>(last));
        const uint32_t reach = static_cast<uint32_t>(Ftol64(Cdecl<double>(addr::CRT_ceil, scaled)));
        AtAddress<uint32_t>(record + texture_record::kReach + 4u * i) = reach;
        uint32_t& greatest = Global<uint32_t>(addr::cSTETerrain_TextureReachMax + 4u * i);
        if (reach > greatest)
            greatest = reach;
        const int32_t pairs = count / 2;
        for (int32_t j = 0; j < pairs; ++j) {
            // The game moves the two floats through the x87 (FLD, FSTP
            // dword): the same bits for anything but a signalling NaN.
            uint32_t point[2] = {AtAddress<uint32_t>(values + 8u * static_cast<uint32_t>(j)),
                                 AtAddress<uint32_t>(values + 8u * static_cast<uint32_t>(j) + 4u)};
            if (points.finish != points.capacity) {
                if (points.finish) {
                    AtAddress<uint32_t>(points.finish) = point[0];
                    AtAddress<uint32_t>(points.finish + 4u) = point[1];
                }
                points.finish += 8u;
            } else {
                uint8_t tag = 0;
                This<void>(addr::SC4PointVector_InsertN, static_cast<void*>(&points), points.finish, static_cast<void*>(point),
                           static_cast<void*>(&tag), 1u, 1u);
            }
        }
        This<uint32_t>(addr::SC4Curve_SetPoints,
                       reinterpret_cast<void*>(static_cast<std::uintptr_t>(record + texture_record::kCurves + 0x10u * i)),
                       static_cast<void*>(&points));
        AtAddress<uint32_t>(record + texture_record::kLastX + 4u * i) = AtAddress<uint32_t>(last);
        if (points.start)
            Cdecl<void>(addr::SC4_Free, points.start);
    }
    Virtual<uint32_t>(resource, 0x8);
}

// 0x0074F410 (no arguments): nothing without the terrain's resource manager.
// Else 0x00B4BC44 is zeroed, a key filter for type 0x6534284A, group
// 0x89C2A517 is made (0x14 bytes; the game goes on with a null one, and so
// would this), the resource manager's slot 17 lists the keys it lets
// through, the list's slot 7 calls LoadTextureExemplar for each, and
// 0x00B4BC44 is raised by 1.
void __cdecl LoadTextureExemplars()
{
    static bool logged = false;
    FirstCall("LoadTextureExemplars", logged);
    if (!Global<void*>(addr::cSTETerrain_ResourceManager))
        return;
    Global<uint32_t>(addr::cSTETerrain_TextureIDLimit) = 0;
    void* list = nullptr;
    void* const memory = Cdecl<void*>(addr::SC4_operator_new, 0x14u);
    char* const filter = memory ? This<char*>(addr::SC4ResourceKeyFilter_ctor, memory, 0x6534284Au, 0x89C2A517u) : nullptr;
    Virtual<uint32_t>(filter, 0x4);
    if (Virtual<uint32_t>(Global<void*>(addr::cSTETerrain_ResourceManager), 0x44, &list, static_cast<void*>(filter + 8))) {
        Virtual<void>(list, 0x1C, reinterpret_cast<void*>(&LoadTextureExemplar), 0u);
        Virtual<uint32_t>(list, 0x8);
    }
    Virtual<uint32_t>(filter, 0x8);
    ++Global<uint32_t>(addr::cSTETerrain_TextureIDLimit);
}

// --- The tuning exemplar -----------------------------------------------------

// 0x0074B460 (no arguments): the five random tables, as the list at the top
// of the file says. A table longer than side x side is cut (its end moved);
// a shorter one, or one of just that length, goes through the game's insert
// of (side x side - length) zeros at its end. The side is read back from
// 0x00B4BC20 for every row and every element, as the game does; the
// comparisons with it are signed.
void FillNoiseTables()
{
    uint32_t zero = 0; // the game's dword on its stack, set once
    for (uint32_t k = 0; k < 5; ++k) {
        const uint32_t step = AtAddress<uint32_t>(Global<uint32_t>(addr::cSTETerrain_NoiseSteps) + 4u * k);
        int32_t& side = Global<int32_t>(addr::cSTETerrain_NoiseSides + 4u * k);
        side = static_cast<int32_t>(0x101u / step + 1u);
        const int32_t rows = static_cast<int32_t>(0x101u / step + 1u);
        GameVector* const table = &Global<GameVector>(addr::cSTETerrain_NoiseTables + 0xCu * k);
        const uint32_t wanted = static_cast<uint32_t>(side) * static_cast<uint32_t>(rows);
        const uint32_t length = static_cast<uint32_t>(static_cast<int32_t>(table->finish - table->start) >> 1);
        if (wanted < length)
            table->finish = table->start + wanted * 2u;
        else
            This<void>(addr::SC4Uint16Vector_InsertN, static_cast<void*>(table), table->finish, wanted - length,
                       static_cast<void*>(&zero));
        for (int32_t row = 0; row < rows; ++row) {
            if (side <= 0)
                continue;
            uint32_t offset = static_cast<uint32_t>(side) * static_cast<uint32_t>(row) * 2u;
            int32_t column = 0;
            do {
                const uint32_t drawn =
                    This<uint32_t>(addr::cRZRandom_Between, reinterpret_cast<void*>(addr::cSTETerrain_Random), 0u, 0xFFFFu);
                AtAddress<uint16_t>(table->start + offset) = static_cast<uint16_t>(drawn);
                ++column;
                offset += 2u;
            } while (column < side);
        }
    }
}

// 0x0074B6A0 (no arguments): the list at the top of the file, in its order.
// FCOMP of two floats, raise when below: an unordered pair changes nothing.
void __cdecl LoadTuning()
{
    static bool logged = false;
    FirstCall("LoadTuning", logged);
    uint32_t key[3] = {0x6534284Au, 0x88CD66E9u, 1u};
    void* resource = nullptr;
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, static_cast<void*>(key), 0xA52160F5u, &resource, 0u, 0u))
        return;
    void* const exemplar = Virtual<void*>(resource, 0x1C);

    GetUint32s(exemplar, 0x89A312C2u, addr::cSTETerrain_NoiseSteps);
    FillNoiseTables();
    GetFloats(exemplar, 0x48CD7B20u, addr::cSTETerrain_SizeTypeFloats);
    GetFloat(exemplar, 0x48CD7B26u, addr::cSTETerrain_MaxAllowedHeightValue);
    GetFloat(exemplar, 0x48CD7B27u, addr::cSTETerrain_MinAllowedHeightValue);
    GetFloat(exemplar, 0x48CD7B21u, addr::cSTETerrain_DefaultSeaLevel);
    if (Global<float>(addr::cSTETerrain_DefaultSeaLevel) < Global<float>(addr::cSTETerrain_MinAllowedHeightValue))
        Global<uint32_t>(addr::cSTETerrain_DefaultSeaLevel) = Global<uint32_t>(addr::cSTETerrain_MinAllowedHeightValue);
    GetFloat(exemplar, 0x48CD7B2Au, addr::cSTETerrain_DefaultNewHeight);
    if (Global<float>(addr::cSTETerrain_DefaultNewHeight) < Global<float>(addr::cSTETerrain_MinAllowedHeightValue))
        Global<uint32_t>(addr::cSTETerrain_DefaultNewHeight) = Global<uint32_t>(addr::cSTETerrain_MinAllowedHeightValue);
    GetFloats(exemplar, 0x48CD7B22u, addr::cSTETerrain_Tuning7B22);
    GetFloats(exemplar, 0x48CD7B23u, addr::cSTETerrain_Tuning7B23);
    GetFloats(exemplar, 0x48CD7B24u, addr::cSTETerrain_Tuning7B24);
    GetFloats(exemplar, 0x48CD7B25u, addr::cSTETerrain_Tuning7B25);
    GetFloat(exemplar, 0x48CD7B28u, addr::cSTETerrain_HeightUnit);
    GetFloat(exemplar, 0x48CD7B30u, addr::cSTETerrain_Tuning7B30);
    GetFloat(exemplar, 0x48CD7B31u, addr::cSTETerrain_InitScale);
    GetUint32(exemplar, 0x48CD7B40u, addr::cSTETerrain_Tuning7B40);
    GetUint32(exemplar, 0x48CD7B41u, addr::cSTETerrain_Tuning7B41);
    GetUint32(exemplar, 0x48CD7B42u, addr::cSTETerrain_Tuning7B42);
    GetFloats(exemplar, 0x48CD7B43u, addr::cSTETerrain_Tuning7B43);
    GetUint32(exemplar, 0x48CD7B50u, addr::cSTETerrain_FlatMinCount);
    GetFloat(exemplar, 0x48CD7B51u, addr::cSTETerrain_FlatReachFactor);
    GetFloat(exemplar, 0x48CD7B52u, addr::cSTETerrain_FlatBandHigh);
    GetFloat(exemplar, 0x48CD7B53u, addr::cSTETerrain_FlatBandLow);
    GetUint32(exemplar, 0x48CD7B54u, addr::cSTETerrain_FlatSpread);
    GetFloat(exemplar, 0x69AC5640u, addr::cSTETerrain_Tuning5640);
    GetFloat(exemplar, 0x69AC5641u, addr::cSTETerrain_Tuning5641);
    GetUint32(exemplar, 0x48CD7B60u, addr::cSTETerrain_ShoreMinRun);
    GetUint32(exemplar, 0x48CD7B64u, addr::cSTETerrain_ShoreMaxRun);
    GetFloat(exemplar, 0x48CD7B65u, addr::cSTETerrain_SteepNormalY);
    GetFloat(exemplar, 0x48CD7B61u, addr::cSTETerrain_ShoreMinHeightSum);
    // FLD float, FMUL float 6.0, FSTP float (0x0074B8FF to 0x0074B916).
    Global<float>(addr::cSTETerrain_ShoreMinHeightSum) =
        static_cast<float>(static_cast<double>(Global<float>(addr::cSTETerrain_ShoreMinHeightSum)) *
                           static_cast<double>(Global<float>(addr::SC4_FloatSix)));
    GetFloat(exemplar, 0x48CD7B62u, addr::cSTETerrain_ShoreCliffLow);
    GetFloat(exemplar, 0x48CD7B63u, addr::cSTETerrain_ShoreCliffHigh);
    GetFloat(exemplar, 0x48CD7B70u, addr::cSTETerrain_MinSeaLevel);
    GetFloat(exemplar, 0x48CD7B71u, addr::cSTETerrain_MaxSeaLevel);
    GetFloat(exemplar, 0x48CD7B72u, addr::cSTETerrain_SeaLevelStep);
    GetFloat(exemplar, 0x48CD7B73u, addr::cSTETerrain_DefaultRaiseLowerAmount);
    GetUint8(exemplar, 0xA9C283D0u, reinterpret_cast<void*>(addr::cSTETerrain_WaterDistanceLimit));
    GetFloat(exemplar, 0xA9C283D1u, addr::cSTETerrain_Tuning83D1);
    // The game's byte at [ESP + 7] is stack memory until a getter writes
    // it; 0 here.
    uint8_t flag = 0;
    GetUint8(exemplar, 0x48CD7B80u, &flag);
    Global<uint8_t>(addr::cSTETerrain_Tuning7B80) = flag != 0 ? 1 : 0;
    GetUint8(exemplar, 0x48CD7B81u, &flag);
    Global<uint8_t>(addr::cSTETerrain_Tuning7B81) = flag != 0 ? 1 : 0;
    GetFloats(exemplar, 0x48CD7BA0u, addr::cSTETerrain_Tuning7BA0);
    // FLD float, FMUL double, FSTP float, the vector's start read again for
    // each of the five (0x0074B9E4 to 0x0074BA3F).
    for (uint32_t i = 0; i < 5; ++i) {
        float& angle = AtAddress<float>(Global<uint32_t>(addr::cSTETerrain_Tuning7BA0) + 4u * i);
        angle = static_cast<float>(static_cast<double>(angle) * Global<double>(addr::SC4_DoubleDegree));
    }
    GetUint32(exemplar, 0x89861864u, addr::cSTETerrain_PendingExpiry);
    Global<uint32_t>(addr::cSTETerrain_PendingRepeat) = Global<uint32_t>(addr::cSTETerrain_PendingExpiry) / 20u;
    GetUint32s(exemplar, 0x48CD7BF1u, addr::cSTETerrain_ErodeIterationsPerStep);
    GetUint32(exemplar, 0x48CD7BF3u, addr::cSTETerrain_ErodeSmoothPasses);
    void* const reach = Virtual<void*>(exemplar, 0x24, 0x48CD7C00u);
    if (reach)
        This<uint32_t>(addr::SC4Curve_LoadFromProperty, reinterpret_cast<void*>(addr::cSTETerrain_EdgeReachCurve), reach);
    GetUint32(exemplar, 0x48CD7C01u, addr::cSTETerrain_EdgeSmoothPasses);
    GetFloat(exemplar, 0x48CD7C02u, addr::cSTETerrain_EdgeTolerance);
    Virtual<uint32_t>(resource, 0x8);
}

// --- Installation ------------------------------------------------------------

void* g_originalExemplars = nullptr;
void* g_originalTuning = nullptr;

bool InstallDetour(const char* group, std::uintptr_t address, void* ours, void** original, const char* what)
{
    if (!game::IsBuild641())
        return false;
    if (!addr_check::Find(address)) {
        log::Write("terrain %s: no address entry for %08X; not installing", group, static_cast<unsigned>(address));
        return false;
    }
    if (!hook::Install(*addr_check::Find(address), ours, original)) {
        log::Write("terrain %s: the detour failed (%08X)", group, static_cast<unsigned>(address));
        return false;
    }
    log::Write("terrain %s: %s %08X (detour) is ours%s", group, what, static_cast<unsigned>(address),
               Global<uint8_t>(addr::cSTETerrain_StaticsDone) ? "; the game has run its loaders already" : "");
    return true;
}

// The one direct call site of 0x0074F410 is 0x0074FE7A and that of
// 0x0074B6A0 is 0x0074FE84, both in 0x0074FE40: no arguments, nothing kept
// in a register across them (calls follow one another; MOV AL, 1 and RET at
// the end), no use of a result. Neither address occurs anywhere else in the
// image. 0x0074F180 is reached only through the pointer 0x0074F410 hands the
// key list (its address occurs only at 0x0074F47A) and 0x0074B460 only from
// 0x0074B6A0 (0x0074B6FD): plain functions here.
bool InstallExemplars(registry::Mode)
{
    return InstallDetour("tables_exemplars", addr::cSTETerrain_LoadTextureExemplars, reinterpret_cast<void*>(&LoadTextureExemplars),
                         &g_originalExemplars, "the texture exemplar loader");
}

bool InstallTuning(registry::Mode)
{
    return InstallDetour("tables_tuning", addr::cSTETerrain_LoadTuning, reinterpret_cast<void*>(&LoadTuning), &g_originalTuning,
                         "the tuning exemplar loader");
}

const bool g_registeredExemplars = registry::Register(
    {"simulators.terrain.tables_exemplars", "simulators/terrain", registry::Kind::Detour, registry::Phase::PostAppInit,
     addr::cSTETerrain_LoadTextureExemplars,
     "Our cSTETerrain texture exemplar loader: the exemplars of type 0x6534284A, group 0x89C2A517 read into the hash map "
     "0x00B4BCDC (a float, five curves, their reach and last x), the greatest instance and the greatest reaches, by a "
     "detour",
     false, InstallExemplars});

const bool g_registeredTuning = registry::Register(
    {"simulators.terrain.tables_tuning", "simulators/terrain", registry::Kind::Detour, registry::Phase::PostAppInit,
     addr::cSTETerrain_LoadTuning,
     "Our cSTETerrain tuning loader: the tuning exemplar (group 0x88CD66E9, instance 1) read into the terrain's globals "
     "and the five tables of random words drawn from the terrain's generator, by a detour",
     false, InstallTuning});

} // namespace
} // namespace opensc4::terrain_sim
