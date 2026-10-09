// Our cSC4GrowthDeveloper (build 641; docs/growth.md), the parts around the
// simulation: Init with its tuning, Shutdown, the save record and our own
// object (constructor, destructor, identity). The queries and the messages
// are in other files of the simulators.growth groups.
//
// Fields this file verified (offsets from the object's start; the save
// functions get `this` = object + 8, so their disassembly reads 8 less):
//   +0x0C uint8   initialized (Init sets it last, Shutdown clears it first)
//   +0x10 int32   reference count (signed compare in Release)
//   +0x14 city (0x00B43CEC); +0x18 message server (0x00B43CCC); +0x1C the
//         building development simulator (0x00B43D68); +0x20 0x00B43D88;
//   +0x24 the city's slot 34 (+0x88); +0x28 0x00B43DA8; +0x2C 0x00B43D14
//   +0x30 the tuning property holder Init was given (referenced)
//   +0x34 a demand from service 0x00B43D74 slot 6 (property 0x87CD6301,
//         0x20000), referenced; 0x006B6B85 sends it changes of +0x104
//   +0x38 uint32  property 0x87CD6332 (uint8); +0x3C property 0x87CD6330
//   +0x44 uint8   tract size of the six grids (64 at first; property 0x87CD6310)
//   +0x45, +0x46  uint8 pair, property 0x87CD6333; +0x47 bool 0x87CD633F
//   +0x48, +0x4C, +0x5C-+0x8C  SC4Curve* (16 bytes each, operator new), one
//         per property (list in LoadTuning); +0x50 vector<SC4Curve*> of 4
//         (properties 0x87CD634B-0x87CD634E)
//   +0x90, +0x94, +0x98 int32 (80 at first; 0x87CD6357, 6355, 6354)
//   +0x9C bool 0x87CD6356; +0xA0 vector<SC4Curve> (count 0x87CD6360, curves
//         0x87CD6361 + i); +0xAC-+0xAF uint8 (0, 3, 3, 6 at first;
//         0x87CD6373, 6370, 6371, 6372)
//   +0xB0 map<int32, float> (head, size +0xB4): 1, 2, 3 -> 0.2 or 0x87CD6380
//   +0xBC uint32  property 0x87CD6381 (uint8, default 1)
//   +0xC0 map<uint32, float> (head, size +0xC4): pairs of 0x87CD63A0, value * 0.01
//   +0xCC uint32  property 0x21 of the holder; the grids' IDs add it
//   +0xD0 uint32  property 0x87CD6300 (uint8)
//   +0xD4 uint32  the map update's state (Init sets 0)
//   +0xDC a cell iterator (0x20 bytes, operator new; not reference counted)
//   +0xE4 reference list (head); +0xEC, +0xF8 vectors (freed by the destructor)
//   +0x104 int32  saved; the total last sent to the demand +0x34 (0x006B6B68)
//   +0x108 uint32 saved; 0x006B51C0 adds to it (the constructor leaves it unset)
//   +0x10C a grid from 0x0045DDB0 (interface 0x49B9E602, SimGridUint8 by the
//         name table; ID +0xCC + 0x1000000), +0x110,
//         +0x114, +0x118 SimGridSint16 (+0x2100000, +0x2000000, +0x3000000),
//         +0x11C, +0x120 SimGridUint16 (+0x5000000, +0x6000000); all but
//         +0x110 saved
//   +0x124, +0x130 vector<int32> (properties 0xCAB1CE3A, 0xCAB1CE3C)
// growth_sim.h's kSavedFC/kSaved100/kDemand were read without the +8 of
// the save functions: they are +0x104, +0x108 and the grid +0x10C.
#include "core/growth_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::growth_sim {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("growth: first %s", name);
}

namespace {

constexpr uint32_t kClassId = 0x0990C138;
constexpr uint32_t kIidUnknown = 0x00000001;
constexpr uint32_t kIidDeveloper = 0xE7CDD662;
constexpr uint32_t kIidMessageTarget2 = 0x452294AA;
constexpr uint32_t kIidSerializable = 0xE4FDA3D4;
constexpr uint32_t kIidGrid = 0xC9BD939A;
constexpr uint32_t kMessages[2] = {0xE8BDA2E7, 0x07699455};

using NewFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* block);
using SelfFn = void*(__thiscall*)(void* self);
using CtorIdFn = void*(__thiscall*)(void* self, uint32_t id);
using CurveFn = void*(__thiscall*)(void* curve, const void* other);
using LoadFn = uint8_t(__thiscall*)(void* curve, void* property);
using GetFn = uint8_t(__cdecl*)(void* holder, uint32_t property, void* value);
using RefFn = uint8_t(__cdecl*)(void* stream, void* object);
using ReadRefFn = uint8_t(__cdecl*)(void* stream, uint32_t iid, void* out, uint32_t zero);
using IndexFn = float*(__thiscall*)(void* map, const void* key);
using FillFn = void(__thiscall*)(void* vector, uint32_t count, const void* value);

const auto OperatorNew = reinterpret_cast<NewFn>(addr::SC4_operator_new);
const auto OperatorDelete = reinterpret_cast<FreeFn>(addr::SC4_operator_delete);
const auto Allocate = reinterpret_cast<NewFn>(addr::SC4_stl_allocate);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);
const auto GetUint8 = reinterpret_cast<GetFn>(addr::SC4_GetPropertyUint8);
const auto GetUint32 = reinterpret_cast<GetFn>(addr::SC4_GetPropertyUint32);
const auto GetSint32 = reinterpret_cast<GetFn>(addr::SC4_GetPropertySint32);
const auto GetBool = reinterpret_cast<GetFn>(addr::SC4_GetPropertyBool);
const auto GetSint32Vector = reinterpret_cast<GetFn>(addr::SC4_GetPropertySint32Vector);
const auto CurveInit = reinterpret_cast<SelfFn>(addr::SC4Curve_Init);
const auto CurveCopy = reinterpret_cast<CurveFn>(addr::SC4Curve_Copy);
const auto CurveAssign = reinterpret_cast<CurveFn>(addr::SC4Curve_Assign);
const auto CurveLoad = reinterpret_cast<LoadFn>(addr::SC4Curve_LoadFromProperty);
const auto CurveSetPoints = reinterpret_cast<LoadFn>(addr::SC4Curve_SetPoints);
const auto WriteRef = reinterpret_cast<RefFn>(addr::SC4Stream_WriteReference);
const auto ReadRef = reinterpret_cast<ReadRefFn>(addr::SC4Stream_ReadReference);
// The game's map operator[]s (generic container code, shared with other
// classes: 0x006B5840 also from 0x0074AF00/0x0074B000, 0x006B58C0 also
// from GetOccupancyByType); left the game's.
const auto IndexSint32 = reinterpret_cast<IndexFn>(addr::SC4Sint32FloatMap_Index);
const auto IndexUint32 = reinterpret_cast<IndexFn>(addr::SC4Uint32FloatMap_Index);

// --- The game's red-black trees (head: color, parent = root, left most, right most) ---

char* Left(char* node) { return At<char*>(node, 0x8); }
char* Right(char* node) { return At<char*>(node, 0xC); }
char* Parent(char* node) { return At<char*>(node, 0x4); }

// 0x006B3E00 and 0x004C56F0 (the same code for two node types whose values
// own nothing): the right subtrees first, then the node, down the left spine.
void EraseNodes(char* node)
{
    while (node) {
        EraseNodes(Right(node));
        char* const left = Left(node);
        Free(node);
        node = left;
    }
}

// The inline clear: the nodes freed when the size is not 0, the head reset.
void ClearTree(char* tree)
{
    if (!At<uint32_t>(tree, 0x4))
        return;
    EraseNodes(Parent(At<char*>(tree, 0x0)));
    char* const head = At<char*>(tree, 0x0);
    At<char*>(head, 0x8) = head;
    At<char*>(head, 0x4) = nullptr;
    At<char*>(head, 0xC) = head;
    At<uint32_t>(tree, 0x4) = 0;
}

// A new empty tree head (0x18 bytes from the STL allocator) into `tree`.
void NewTree(char* tree)
{
    At<char*>(tree, 0x0) = nullptr;
    char* const head = static_cast<char*>(Allocate(0x18));
    At<char*>(tree, 0x0) = head;
    At<uint32_t>(tree, 0x4) = 0;
    At<uint8_t>(head, 0x0) = 0;
    At<char*>(head, 0x4) = nullptr;
    At<char*>(head, 0x8) = head;
    At<char*>(head, 0xC) = head;
}

// --- vector<SC4Curve> (+0xA0: begin, end, capacity; 16-byte curves whose
// first word is their point buffer) ---------------------------------------

struct CurveVector {
    char* begin;
    char* end;
    char* capacity;
};

constexpr std::ptrdiff_t kCurve = 0x10;

std::ptrdiff_t Count(const char* first, const char* last) { return (last - first) >> 4; }

// A curve's point buffer freed (the element's destructor).
void DestroyCurve(char* curve)
{
    if (void* const points = At<void*>(curve, 0x0))
        Free(points);
}

// 0x006B7180: every curve destroyed, then the storage freed.
void DestroyCurves(CurveVector* v)
{
    for (char* p = v->begin; p != v->end; p += kCurve)
        DestroyCurve(p);
    if (v->begin)
        Free(v->begin);
}

// 0x006B49F0: copy-constructs [first, last) at dest (skipping a null dest
// as the game's code does); the end of the copies.
char* UninitializedCopy(char* first, char* last, char* dest)
{
    for (; first != last; first += kCurve, dest += kCurve)
        if (dest)
            CurveCopy(dest, first);
    return dest;
}

// 0x006B4A20: `count` copies of value constructed at dest.
char* UninitializedFill(char* dest, std::ptrdiff_t count, const char* value)
{
    for (; count > 0; --count, dest += kCurve)
        if (dest)
            CurveCopy(dest, value);
    return dest;
}

// 0x006B3B60: value assigned to [first, last).
void Fill(char* first, char* last, const char* value)
{
    for (; first != last; first += kCurve)
        CurveAssign(first, value);
}

// 0x006B71C0: erase(first, last): [last, end) assigned down from first
// (0x006B3B90), the left-over curves destroyed.
char* EraseCurves(CurveVector* v, char* first, char* last)
{
    char* const end = v->end;
    char* dest = first;
    for (std::ptrdiff_t n = Count(last, end); n > 0; --n, last += kCurve, dest += kCurve)
        CurveAssign(dest, last);
    for (char* p = dest; p != end; p += kCurve)
        DestroyCurve(p);
    v->end = dest;
    return first;
}

// 0x006B7220 (atEnd always 0 from InsertCurves): new storage of size +
// max(size, count) curves, [begin, pos), count copies of value and [pos,
// end) copy-constructed into it, the old vector destroyed.
void ReallocInsert(CurveVector* v, char* pos, const char* value, uint32_t count)
{
    const uint32_t size = static_cast<uint32_t>(Count(v->begin, v->end));
    const uint32_t capacity = size + (size < count ? count : size);
    char* const storage = capacity ? static_cast<char*>(Allocate(capacity * kCurve)) : nullptr;
    char* dest = UninitializedCopy(v->begin, pos, storage);
    dest = UninitializedFill(dest, count, value);
    dest = UninitializedCopy(pos, v->end, dest);
    DestroyCurves(v);
    v->end = dest;
    v->capacity = storage + capacity * kCurve;
    v->begin = storage;
}

// 0x006B7760: insert(pos, count, value); in place through a copy of the
// value when the capacity suffices.
void InsertCurves(CurveVector* v, char* pos, uint32_t count, const char* value)
{
    if (!count)
        return;
    if (static_cast<uint32_t>(Count(v->end, v->capacity)) < count) {
        ReallocInsert(v, pos, value, count);
        return;
    }
    char copy[kCurve];
    CurveCopy(copy, value);
    char* const oldEnd = v->end;
    const uint32_t after = static_cast<uint32_t>(Count(pos, oldEnd));
    const std::ptrdiff_t bytes = static_cast<std::ptrdiff_t>(count) * kCurve;
    if (after > count) {
        UninitializedCopy(oldEnd - bytes, oldEnd, oldEnd);
        v->end = oldEnd + bytes;
        // 0x006B3BD0: [pos, oldEnd - bytes) assigned backwards to end at oldEnd.
        char* source = oldEnd - bytes;
        char* dest = oldEnd;
        for (std::ptrdiff_t n = Count(pos, source); n > 0; --n) {
            source -= kCurve;
            dest -= kCurve;
            CurveAssign(dest, source);
        }
        Fill(pos, pos + bytes, copy);
    } else {
        UninitializedFill(oldEnd, static_cast<std::ptrdiff_t>(count - after), copy);
        v->end = oldEnd + static_cast<std::ptrdiff_t>(count - after) * kCurve;
        UninitializedCopy(pos, oldEnd, v->end);
        v->end += static_cast<std::ptrdiff_t>(after) * kCurve;
        Fill(pos, oldEnd, copy);
    }
    DestroyCurve(copy);
}

// --- Init ------------------------------------------------------------------------

struct GameString {
    const void* vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};

void* Holder(char* self) { return At<void*>(self, 0x30); }

// One SC4Curve* field from a property: loaded (made first when missing)
// when the holder has the property, else freed and cleared.
void LoadCurve(char* self, void*& curve, uint32_t property)
{
    if (void* const value = Virtual<void*>(Holder(self), 0x24, property)) {
        if (!curve) {
            void* fresh = OperatorNew(0x10);
            if (fresh)
                fresh = CurveInit(fresh);
            curve = fresh;
        }
        CurveLoad(curve, value);
    } else if (void* const old = curve) {
        DestroyCurve(static_cast<char*>(old));
        OperatorDelete(old);
        curve = nullptr;
    }
}

// The curves owned through a pointer, in Shutdown's order.
constexpr std::size_t kCurveFields[15] = {0x48, 0x4C, 0x60, 0x64, 0x68, 0x6C, 0x70, 0x5C,
                                          0x74, 0x78, 0x7C, 0x80, 0x84, 0x88, 0x8C};

// 0x006B3D30: the tract size of the six grids; the cell iterator's
// rectangle to grid +0x110's tracts.
void SetTractSize(char* self, uint8_t size)
{
    if (At<uint8_t>(self, 0x44) == size)
        return;
    At<uint8_t>(self, 0x44) = size;
    for (std::size_t offset = 0x10C; offset <= 0x120; offset += 4)
        Virtual<uint8_t>(At<void*>(self, offset), 0x24, static_cast<int32_t>(At<uint8_t>(self, 0x44)));
    const int32_t countZ = Virtual<int32_t>(At<void*>(self, 0x110), 0x34);
    const int32_t countX = Virtual<int32_t>(At<void*>(self, 0x110), 0x30);
    char* const iterator = At<char*>(self, 0xDC);
    At<int32_t>(iterator, 0x10) = 0;
    At<int32_t>(iterator, 0x14) = 0;
    At<int32_t>(iterator, 0x18) = countX - 1;
    At<int32_t>(iterator, 0x1C) = countZ - 1;
    if (At<int32_t>(iterator, 0x18) < At<int32_t>(iterator, 0x10))
        At<int32_t>(iterator, 0x18) = At<int32_t>(iterator, 0x10);
    if (At<int32_t>(iterator, 0x1C) < At<int32_t>(iterator, 0x14))
        At<int32_t>(iterator, 0x1C) = At<int32_t>(iterator, 0x14);
}

// 0x006B78B0: the tuning from the property holder +0x30. Properties the
// holder lacks leave their fields as they were unless noted.
void LoadTuning(char* self)
{
    static bool logged = false;
    FirstCall("LoadTuning", logged);
    uint32_t subType = 0;
    if (GetUint32(Holder(self), 0x21, &subType))
        At<uint32_t>(self, 0xCC) = subType;
    uint8_t zoneKind = 0;
    if (GetUint8(Holder(self), 0x87CD6300, &zoneKind))
        At<uint32_t>(self, 0xD0) = zoneKind;
    uint32_t demandId = 0;
    if (GetUint32(Holder(self), 0x87CD6301, &demandId)) {
        // Released but not cleared before the new one is stored, as the game.
        if (void* const old = At<void*>(self, 0x34))
            Virtual<uint32_t>(old, 0x8);
        void* const demand = Virtual<void*>(Global<void*>(addr::SC4_Service43D74), 0x18, demandId, 0x20000u);
        At<void*>(self, 0x34) = demand;
        if (demand)
            Virtual<uint32_t>(demand, 0x4);
    }
    // A byte read into a 4-byte local whose other bytes the game never
    // writes; only the byte is used.
    uint8_t tractSize = 0;
    if (GetUint8(Holder(self), 0x87CD6310, &tractSize))
        SetTractSize(self, tractSize);
    uint8_t value = 0;
    if (GetUint8(Holder(self), 0x87CD6330, &value))
        At<uint32_t>(self, 0x3C) = value;
    if (GetUint8(Holder(self), 0x87CD6332, &value))
        At<uint32_t>(self, 0x38) = value;
    if (void* const property = Virtual<void*>(Holder(self), 0x24, 0x87CD6333u)) {
        void* const variant = Virtual<void*>(property, 0x14);
        const uint8_t* const pair = Virtual<const uint8_t*>(variant, 0x17C);
        At<uint8_t>(self, 0x45) = pair[0];
        At<uint8_t>(self, 0x46) = pair[1];
    }
    At<uint8_t>(self, 0x47) = 0;
    GetBool(Holder(self), 0x87CD633F, self + 0x47);

    LoadCurve(self, At<void*>(self, 0x48), 0x87CD6340);
    LoadCurve(self, At<void*>(self, 0x4C), 0x87CD6398);
    LoadCurve(self, At<void*>(self, 0x60), 0x87CD6343);
    LoadCurve(self, At<void*>(self, 0x64), 0x87CD6344);
    LoadCurve(self, At<void*>(self, 0x68), 0x87CD6345);
    LoadCurve(self, At<void*>(self, 0x6C), 0x87CD6391);
    LoadCurve(self, At<void*>(self, 0x70), 0x87CD6392);
    LoadCurve(self, At<void*>(self, 0x5C), 0x87CD6341);
    for (uint32_t i = 0; i < 4; ++i)
        LoadCurve(self, At<void**>(self, 0x50)[i], 0x87CD634B + i);
    LoadCurve(self, At<void*>(self, 0x74), 0x87CD6348);
    LoadCurve(self, At<void*>(self, 0x78), 0x87CD6349);
    LoadCurve(self, At<void*>(self, 0x7C), 0x87CD634A);
    LoadCurve(self, At<void*>(self, 0x80), 0x87CD6347);
    LoadCurve(self, At<void*>(self, 0x84), 0x87CD6346);
    LoadCurve(self, At<void*>(self, 0x88), 0x87CD6399);
    LoadCurve(self, At<void*>(self, 0x8C), 0x87CD6397);

    At<int32_t>(self, 0x90) = 0x50;
    GetSint32(Holder(self), 0x87CD6357, self + 0x90);
    At<int32_t>(self, 0x94) = 0x50;
    GetSint32(Holder(self), 0x87CD6355, self + 0x94);
    if (!GetSint32(Holder(self), 0x87CD6354, self + 0x98))
        At<int32_t>(self, 0x98) = At<int32_t>(self, 0x94);
    At<uint8_t>(self, 0x9C) = 0;
    GetBool(Holder(self), 0x87CD6356, self + 0x9C);

    auto* const curves = reinterpret_cast<CurveVector*>(self + 0xA0);
    EraseCurves(curves, curves->begin, curves->end);
    uint8_t count = 0;
    GetUint8(Holder(self), 0x87CD6360, &count);
    char empty[kCurve];
    CurveInit(empty);
    const uint32_t size = static_cast<uint32_t>(Count(curves->begin, curves->end));
    if (count < size)
        EraseCurves(curves, curves->begin + static_cast<std::ptrdiff_t>(count) * kCurve, curves->end);
    else
        InsertCurves(curves, curves->end, count - size, empty);
    DestroyCurve(empty);
    for (uint32_t i = 0; i < count; ++i) {
        if (void* const property = Virtual<void*>(Holder(self), 0x24, 0x87CD6361 + i)) {
            CurveLoad(curves->begin + static_cast<std::ptrdiff_t>(i) * kCurve, property);
            continue;
        }
        // No property: the curve set to the single point (0, 0), from a
        // vector<cS3DVector2> grown by one element.
        auto* const point = static_cast<float*>(Allocate(8));
        if (point) {
            point[0] = 0.0f;
            point[1] = 0.0f;
        }
        CurveVector points{reinterpret_cast<char*>(point), reinterpret_cast<char*>(point) + 8,
                           reinterpret_cast<char*>(point) + 8};
        CurveSetPoints(curves->begin + static_cast<std::ptrdiff_t>(i) * kCurve, &points);
        if (points.begin)
            Free(points.begin);
    }

    GetUint8(Holder(self), 0x87CD6373, self + 0xAC);
    GetUint8(Holder(self), 0x87CD6370, self + 0xAD);
    GetUint8(Holder(self), 0x87CD6371, self + 0xAE);
    GetUint8(Holder(self), 0x87CD6372, self + 0xAF);
    char* const thresholds = self + 0xB0;
    for (int32_t key = 1; key <= 3; ++key)
        *IndexSint32(thresholds, &key) = 0.2f;
    if (void* const property = Virtual<void*>(Holder(self), 0x24, 0x87CD6380u)) {
        void* const variant = Virtual<void*>(property, 0x14);
        const float* const values = Virtual<const float*>(variant, 0x1BC);
        for (int32_t key = 1; key <= 3; ++key)
            *IndexSint32(thresholds, &key) = values[key - 1];
    }
    uint8_t flag = 1;
    GetUint8(Holder(self), 0x87CD6381, &flag);
    At<uint32_t>(self, 0xBC) = flag;
    char* const scales = self + 0xC0;
    ClearTree(scales);
    if (void* const property = Virtual<void*>(Holder(self), 0x24, 0x87CD63A0u)) {
        void* const variant = Virtual<void*>(property, 0x14);
        const uint32_t* const pairs = Virtual<const uint32_t*>(variant, 0x19C);
        const uint32_t n = Virtual<uint32_t>(variant, 0x10);
        // As the game, pairs[n] is read when n is odd.
        for (uint32_t i = 0; i < n; i += 2) {
            const uint32_t key = pairs[i];
            // x87: fild (+ 2^32 when negative), fmul by the float 0.01 at
            // 0x00A823A4, stored as float.
            const float scaled = static_cast<float>(static_cast<double>(pairs[i + 1]) *
                                                    static_cast<double>(Global<float>(addr::SC4Traffic_FerryCostScale)));
            *IndexUint32(scales, &key) = scaled;
        }
    }
    GetSint32Vector(Holder(self), 0xCAB1CE3A, self + 0x124);
    GetSint32Vector(Holder(self), 0xCAB1CE3C, self + 0x130);
}

// A service into `offset` when there is none: stored even when null, then
// referenced; false when null.
bool Take(char* self, std::size_t offset, void* service)
{
    if (At<void*>(self, offset))
        return true;
    At<void*>(self, offset) = service;
    if (!service)
        return false;
    Virtual<uint32_t>(service, 0x4);
    return true;
}

struct GridSpec {
    std::size_t offset;
    std::uintptr_t construct;
    uint32_t id;
};

constexpr GridSpec kGrids[6] = {
    {0x10C, addr::SC4SimGridBool8_SC4SimGridBool8, 0x1000000},
    {0x110, addr::cSC4SimGridSint16_Construct, 0x2100000},
    {0x114, addr::cSC4SimGridSint16_Construct, 0x2000000},
    {0x118, addr::cSC4SimGridSint16_Construct, 0x3000000},
    {0x11C, addr::cSC4SimGridUint16_Construct, 0x5000000},
    {0x120, addr::cSC4SimGridUint16_Construct, 0x6000000},
};

// Slot 3 (0x006B8510): Init(tuning property holder). False (initialized
// left 0) when a service is missing; true once done or when already done.
uint8_t __fastcall Init(char* self, void*, void* holder)
{
    using TextFn = void*(__thiscall*)(void* string, const char* text);
    static bool logged = false;
    FirstCall("Init", logged);
    if (At<uint8_t>(self, field::kInitialized))
        return 1;
    uint8_t& once = Global<uint8_t>(addr::cSC4GrowthDeveloper_InitOnce);
    if (!once)
        once = 1;
    if (!Take(self, 0x14, Global<void*>(addr::SC4_City)))
        return 0;
    if (!Take(self, 0x1C, Global<void*>(addr::SC4_Service43D68)))
        return 0;
    if (!At<void*>(self, 0x24) && !Take(self, 0x24, Virtual<void*>(Global<void*>(addr::SC4_City), 0x88)))
        return 0;
    if (!Take(self, 0x2C, Global<void*>(addr::SC4_Service43D14)))
        return 0;
    if (!Take(self, 0x20, Global<void*>(addr::SC4_Service43D88)))
        return 0;
    if (!Take(self, 0x28, Global<void*>(addr::SC4_Service43DA8)))
        return 0;
    At<void*>(self, 0x30) = holder;
    Virtual<uint32_t>(holder, 0x4);
    uint32_t subType = 0;
    if (GetUint32(At<void*>(self, 0x30), 0x21, &subType))
        At<uint32_t>(self, 0xCC) = subType;
    for (const GridSpec& spec : kGrids) {
        if (At<void*>(self, spec.offset))
            continue;
        void* grid = OperatorNew(0x44);
        if (grid)
            grid = reinterpret_cast<CtorIdFn>(spec.construct)(grid, 0x49B9E600);
        At<void*>(self, spec.offset) = grid;
        Virtual<uint32_t>(grid, 0x4);
        Virtual<uint8_t>(At<void*>(self, spec.offset), 0xC);
        Virtual<uint8_t>(At<void*>(self, spec.offset), 0x24, static_cast<int32_t>(At<uint8_t>(self, 0x44)));
        Virtual<uint8_t>(At<void*>(self, spec.offset), 0x18, At<uint32_t>(self, 0xCC) + spec.id);
    }
    At<uint32_t>(self, 0xD4) = 0;
    if (!At<void*>(self, 0xDC)) {
        void* iterator = OperatorNew(0x20);
        if (iterator)
            iterator = reinterpret_cast<SelfFn>(addr::SC4CellIterator_Construct)(iterator);
        At<void*>(self, 0xDC) = iterator;
    }
    LoadTuning(self);
    void* const target = self + 0x4;
    if (!At<void*>(self, 0x18)) {
        if (!Take(self, 0x18, Global<void*>(addr::SC4_MessageServer)))
            return 0;
        for (const uint32_t message : kMessages)
            Virtual<uint8_t>(At<void*>(self, 0x18), 0x14, target, message);
    }
    void* const simulators = Global<void*>(addr::SC4_SimulatorService);
    if (!simulators)
        return 0;
    GameString name;
    reinterpret_cast<TextFn>(addr::SC4String_FromText)(&name,
                                                       reinterpret_cast<const char*>(addr::cSC4GrowthDeveloper_Name));
    Virtual<uint32_t>(simulators, 0x4C, target, 10u, &name, 2u);
    Virtual<uint32_t>(simulators, 0x4C, target, 4u, &name, 2u);
    At<uint8_t>(self, field::kInitialized) = 1;
    reinterpret_cast<SelfFn>(addr::SC4String_Destroy)(&name);
    return 1;
}

// --- Shutdown ---------------------------------------------------------------------

void Release(char* self, std::size_t offset)
{
    if (void* const object = At<void*>(self, offset)) {
        Virtual<uint32_t>(object, 0x8);
        At<void*>(self, offset) = nullptr;
    }
}

// A curve owned through a pointer freed (the pointer left as it is).
void DeleteCurve(void* curve)
{
    if (!curve)
        return;
    DestroyCurve(static_cast<char*>(curve));
    OperatorDelete(curve);
}

// Slot 4 (0x006B59A0): unregistered from the city's simulators and the
// messages; the cell iterator, the grids (shut down), the holder, the
// demand and the services released; the curves freed. Always true.
uint8_t __fastcall Shutdown(char* self, void*)
{
    static bool logged = false;
    FirstCall("Shutdown", logged);
    if (!At<uint8_t>(self, field::kInitialized))
        return 1;
    At<uint8_t>(self, field::kInitialized) = 0;
    void* const target = self + 0x4;
    if (void* const simulators = Virtual<void*>(Global<void*>(addr::SC4_City), 0xC8))
        Virtual<uint32_t>(simulators, 0x50, target);
    if (At<void*>(self, 0x18)) {
        for (const uint32_t message : kMessages)
            Virtual<uint8_t>(At<void*>(self, 0x18), 0x18, target, message);
        Virtual<uint32_t>(At<void*>(self, 0x18), 0x8);
        At<void*>(self, 0x18) = nullptr;
    }
    if (void* const iterator = At<void*>(self, 0xDC)) {
        OperatorDelete(iterator);
        At<void*>(self, 0xDC) = nullptr;
    }
    for (std::size_t offset = 0x10C; offset <= 0x120; offset += 4) {
        if (At<void*>(self, offset)) {
            Virtual<uint8_t>(At<void*>(self, offset), 0x10);
            Virtual<uint32_t>(At<void*>(self, offset), 0x8);
            At<void*>(self, offset) = nullptr;
        }
    }
    for (const std::size_t offset : {0x30u, 0x34u, 0x1Cu, 0x20u, 0x28u, 0x2Cu, 0x24u, 0x14u})
        Release(self, offset);
    for (const std::size_t offset : kCurveFields)
        DeleteCurve(At<void*>(self, offset));
    // The game clears every pointer but +0x4C, whose curve it has just
    // freed: the next LoadTuning loads property 0x87CD6398 into freed
    // memory when the field is still set. Copied as is.
    for (const std::size_t offset : kCurveFields)
        if (offset != 0x4C)
            At<void*>(self, offset) = nullptr;
    for (std::size_t i = 0; i < 4; ++i) {
        DeleteCurve(At<void**>(self, 0x50)[i]);
        At<void**>(self, 0x50)[i] = nullptr;
    }
    return 1;
}

// --- Save --------------------------------------------------------------------------

// cIGZSerializable slot 3 (0x006B3960), on the part at +0x8: version 1, the
// two totals and five of the six grids. False when the stream is in error
// after (the low byte of GetError, as the game tests it).
uint8_t __fastcall Write(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    char* const self = part - 0x8;
    Virtual<uint8_t>(stream, 0x1C, static_cast<uint16_t>(1));
    Virtual<uint8_t>(stream, 0x20, At<int32_t>(self, 0x104));
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, 0x108));
    for (const std::size_t offset : {0x10Cu, 0x114u, 0x118u, 0x11Cu, 0x120u})
        WriteRef(stream, At<void*>(self, offset));
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// cIGZSerializable slot 4 (0x006B39F0): version 1 only (false otherwise,
// nothing else read), then as Write. False when the stream is in error after.
uint8_t __fastcall Read(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Read", logged);
    char* const self = part - 0x8;
    // The game reads the version into the stack slot of its stream
    // argument: when the read writes nothing, the pointer's low 16 bits
    // are compared.
    uint16_t version = static_cast<uint16_t>(reinterpret_cast<std::uintptr_t>(stream));
    Virtual<uint8_t>(stream, 0x1C, &version);
    if (version != 1)
        return 0;
    Virtual<uint8_t>(stream, 0x20, self + 0x104);
    Virtual<uint8_t>(stream, 0x24, self + 0x108);
    for (const std::size_t offset : {0x10Cu, 0x114u, 0x118u, 0x11Cu, 0x120u})
        ReadRef(stream, kIidGrid, self + offset, 0u);
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// --- Installation of Init, Shutdown, Write and Read -----------------------------

struct Slot {
    std::uintptr_t vtable;
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(vtable, index, original, fn) {vtable, index, original, reinterpret_cast<void*>(&fn)}

const Slot kInitSlots[] = {
    OPENSC4_SLOT(addr::cSC4GrowthDeveloper_vftable, 3, addr::cSC4GrowthDeveloper_Init, Init)};
const Slot kShutdownSlots[] = {
    OPENSC4_SLOT(addr::cSC4GrowthDeveloper_vftable, 4, addr::cSC4GrowthDeveloper_Shutdown, Shutdown)};
const Slot kSaveSlots[] = {
    OPENSC4_SLOT(addr::cSC4GrowthDeveloper_vftable_8, 3, addr::cSC4GrowthDeveloper_Write, Write)};
const Slot kLoadSlots[] = {
    OPENSC4_SLOT(addr::cSC4GrowthDeveloper_vftable_8, 4, addr::cSC4GrowthDeveloper_Read, Read)};

#undef OPENSC4_SLOT

template <std::size_t N>
bool SwapSlots(const Slot (&slots)[N], const char* what)
{
    if (!game::IsBuild641())
        return false;
    for (const Slot& slot : slots) {
        const auto* const vtable = reinterpret_cast<const std::uintptr_t*>(slot.vtable);
        if (vtable[slot.index] != slot.original) {
            log::Write("growth %s: slot %d of %08X holds %08X, expected %08X; not installing", what, slot.index,
                       static_cast<unsigned>(slot.vtable), static_cast<unsigned>(vtable[slot.index]),
                       static_cast<unsigned>(slot.original));
            return false;
        }
    }
    for (const Slot& slot : slots) {
        auto* const entry = reinterpret_cast<std::uintptr_t*>(slot.vtable) + slot.index;
        DWORD old = 0;
        if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &old))
            return false;
        *entry = reinterpret_cast<std::uintptr_t>(slot.ours);
        VirtualProtect(entry, sizeof(*entry), old, &old);
    }
    log::Write("growth %s: %u slots are ours", what, static_cast<unsigned>(N));
    return true;
}

bool InstallInit(registry::Mode) { return SwapSlots(kInitSlots, "init"); }
bool InstallShutdown(registry::Mode) { return SwapSlots(kShutdownSlots, "shutdown"); }
bool InstallSave(registry::Mode) { return SwapSlots(kSaveSlots, "save"); }
bool InstallLoad(registry::Mode) { return SwapSlots(kLoadSlots, "load"); }

const bool g_registered[] = {
    registry::Register({"simulators.growth.init", "simulators/growth", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4GrowthDeveloper_vftable,
                        "Our growth developer Init, its tuning load and helpers (docs/growth.md)", false,
                        InstallInit}),
    registry::Register({"simulators.growth.shutdown", "simulators/growth", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4GrowthDeveloper_vftable,
                        "Our growth developer Shutdown (docs/growth.md)", false, InstallShutdown}),
    registry::Register({"simulators.growth.save", "simulators/growth", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4GrowthDeveloper_vftable_8,
                        "Our growth developer Write: the save record, version 1 (docs/growth.md)", false,
                        InstallSave}),
    registry::Register({"simulators.growth.load", "simulators/growth", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4GrowthDeveloper_vftable_8,
                        "Our growth developer Read, version 1 (docs/growth.md)", false, InstallLoad}),
};

// --- Our object -------------------------------------------------------------------

// Copies of the game's 0x00AB1488 (32 slots, deleting destructor last),
// 0x00AB1474 (at +4) and 0x00AB145C (at +8), taken at the first
// construction, after every PostAppInit slot swap of the other groups.
constexpr int kSlots = 32, kSlots4 = 4, kSlots8 = 6;
void* g_vtable[kSlots];
void* g_vtable4[kSlots4];
void* g_vtable8[kSlots8];

// As the game's (0x004F7BA0, shared with other classes).
uint32_t __fastcall AddRef(char* self, void*) { return ++At<uint32_t>(self, field::kRefCount); }

// As the game's (0x006B37A0).
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    std::size_t offset;
    switch (iid) {
    case kIidUnknown:
    case kIidDeveloper: offset = 0; break;
    case kIidMessageTarget2: offset = 0x4; break;
    case kIidSerializable: offset = 0x8; break;
    default: return 0;
    }
    *object = self ? self + offset : nullptr;
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// As the game's (0x006B3D00): Shutdown and the deleting destructor (slot
// 31) at the last reference.
uint32_t __fastcall ReleaseObject(char* self, void*)
{
    int32_t& refs = At<int32_t>(self, field::kRefCount);
    if (refs > 1)
        return static_cast<uint32_t>(--refs);
    Virtual<uint8_t>(self, 0x10);
    Virtual<void*>(self, 0x7C, 1u);
    return 0;
}

// The destructor (0x006B7640): only what it frees; the curves owned
// through pointers and the services are Shutdown's.
void Destroy(char* self)
{
    At<void**>(self, 0x0) = g_vtable;
    At<void**>(self, 0x4) = g_vtable4;
    At<void**>(self, 0x8) = g_vtable8;
    for (const std::size_t offset : {0x130u, 0x124u, 0xF8u, 0xECu})
        if (void* const block = At<void*>(self, offset))
            Free(block);
    // The reference list +0xE4 (generic, shared: left the game's), then its head.
    reinterpret_cast<SelfFn>(addr::SC4RefList_Clear)(self + 0xE4);
    if (void* const head = At<void*>(self, 0xE4))
        Free(head);
    for (const std::size_t offset : {0xC0u, 0xB0u}) {
        ClearTree(self + offset);
        if (void* const head = At<void*>(self, offset))
            Free(head);
    }
    DestroyCurves(reinterpret_cast<CurveVector*>(self + 0xA0));
    if (void* const pointers = At<void*>(self, 0x50))
        Free(pointers);
}

// Slot 31, as the game's (0x006B7890).
void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    static int destroyed = 0;
    if (destroyed++ < 3)
        log::Write("growth object: destroying %p", static_cast<void*>(self));
    Destroy(self);
    if (flags & 1)
        OperatorDelete(self);
    return self;
}

// As the game's (0x006B3790).
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
    return ReleaseObject(part - Offset, nullptr);
}

void BuildVtables()
{
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4GrowthDeveloper_vftable), sizeof(g_vtable));
    std::memcpy(g_vtable4, reinterpret_cast<const void*>(addr::cSC4GrowthDeveloper_vftable_4), sizeof(g_vtable4));
    std::memcpy(g_vtable8, reinterpret_cast<const void*>(addr::cSC4GrowthDeveloper_vftable_8), sizeof(g_vtable8));
    g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
    g_vtable[1] = reinterpret_cast<void*>(&AddRef);
    g_vtable[2] = reinterpret_cast<void*>(&ReleaseObject);
    g_vtable[31] = reinterpret_cast<void*>(&DeletingDestructor);
    g_vtable4[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x4>);
    g_vtable4[1] = reinterpret_cast<void*>(&AddRefAt<0x4>);
    g_vtable4[2] = reinterpret_cast<void*>(&ReleaseAt<0x4>);
    g_vtable8[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x8>);
    g_vtable8[1] = reinterpret_cast<void*>(&AddRefAt<0x8>);
    g_vtable8[2] = reinterpret_cast<void*>(&ReleaseAt<0x8>);
    g_vtable8[5] = reinterpret_cast<void*>(&GetGZCLSID);
    log::Write("growth object: vtables copied (slot 3 %p, +4 slot 3 %p, +8 slots 3/4 %p %p)", g_vtable[3],
               g_vtable4[3], g_vtable8[3], g_vtable8[4]);
}

// The constructor (detour of 0x006B7310, reached from the factory 0x006B7870
// and from the building development simulator's 0x006991A0): the fields
// the game's sets, in its order of allocations. Fields it leaves unset
// (+0xB8, +0xC8, +0xE0, +0xE8, +0x108 among others) stay unset.
void* __fastcall Construct(char* self, void*)
{
    static bool built = false;
    if (!built) {
        built = true;
        BuildVtables();
    }
    At<void**>(self, 0x0) = g_vtable;
    At<void**>(self, 0x4) = g_vtable4;
    At<void**>(self, 0x8) = g_vtable8;
    At<uint8_t>(self, 0xC) = 0;
    for (std::size_t offset = 0x10; offset <= 0x40; offset += 4)
        At<uint32_t>(self, offset) = 0;
    At<uint8_t>(self, 0x44) = 0x40;
    At<uint8_t>(self, 0x45) = 0;
    At<uint8_t>(self, 0x46) = 0;
    At<uint8_t>(self, 0x47) = 0;
    for (std::size_t offset = 0x48; offset <= 0x58; offset += 4)
        At<uint32_t>(self, offset) = 0;
    At<int32_t>(self, 0x90) = 0x50;
    At<int32_t>(self, 0x94) = 0x50;
    At<int32_t>(self, 0x98) = 0x50;
    for (std::size_t offset = 0x5C; offset <= 0x8C; offset += 4)
        At<uint32_t>(self, offset) = 0;
    At<uint8_t>(self, 0x9C) = 1;
    At<uint32_t>(self, 0xA0) = 0;
    At<uint32_t>(self, 0xA4) = 0;
    At<uint32_t>(self, 0xA8) = 0;
    At<uint8_t>(self, 0xAC) = 0;
    At<uint8_t>(self, 0xAD) = 3;
    At<uint8_t>(self, 0xAE) = 3;
    At<uint8_t>(self, 0xAF) = 6;
    NewTree(self + 0xB0);
    At<uint32_t>(self, 0xBC) = 1;
    NewTree(self + 0xC0);
    At<void*>(self, 0xE4) = nullptr;
    void** const head = static_cast<void**>(Allocate(0xC));
    head[0] = head;
    head[1] = head;
    At<void**>(self, 0xE4) = head;
    for (std::size_t offset = 0xEC; offset <= 0x104; offset += 4)
        At<uint32_t>(self, offset) = 0;
    for (std::size_t offset = 0x10C; offset <= 0x138; offset += 4)
        At<uint32_t>(self, offset) = 0;
    const uint32_t none = 0;
    reinterpret_cast<FillFn>(addr::SC4PtrVector_Fill)(self + 0x50, 4, &none);
    At<uint32_t>(self, 0xD4) = 0;
    At<uint32_t>(self, 0xD8) = 0;
    At<uint32_t>(self, 0xDC) = 0;
    static int created = 0;
    if (created++ < 3)
        log::Write("growth object: constructed %p", static_cast<void*>(self));
    return self;
}

using ConstructFn = void*(__fastcall*)(char*, void*);
ConstructFn g_originalConstruct = nullptr;

bool ApplyObject(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    const addr::Entry* const entry = addr_check::Find(addr::cSC4GrowthDeveloper_cSC4GrowthDeveloper);
    return entry && hook::Install(*entry, &Construct, &g_originalConstruct);
}

const bool g_registeredObject = registry::Register({
    "simulators.growth.object",
    "simulators/growth",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4GrowthDeveloper_cSC4GrowthDeveloper,
    "Our cSC4GrowthDeveloper object: constructor (detour, both creation paths), destructor, identity; methods "
    "from copies of the (patched) game vtables (docs/growth.md)",
    false,
    ApplyObject,
});

} // namespace
} // namespace opensc4::growth_sim
