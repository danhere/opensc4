// Our cSC4LotBaseTextureOccupant (build 641; the class map, the vtables slot
// by slot and the fields are in texocc_sim.h): the class's own slots in its
// five vtables (the entries, the cell coverage, the box, the usage, the
// position, Shutdown), the save record's Write and Read, and our own object
// (the constructor, the factory and the pool allocation by detour, the
// deleting destructor and identity from copies of the vtables).
//
// The occupant base (cSC4Occupant: its constructor, destructor,
// QueryInterface, AddRef, Release, Init, Shutdown, Write, Read) is every
// occupant class's (src/core/occbase_sim.cpp): it is called at its address.
// The model and drawing code stays the game's (texocc_sim.h lists it):
// LoadModels and UnloadModels are called through the vtable, the update of
// the models (0x005DF640) and the texture's picture count (0x005E08F0) at
// their addresses, in the game's order. Generic container code other
// classes share is called at its address too.
//
// Every virtual call below has its argument count from the callee's own
// `ret N`:
//   - the streams' vtables 0x00AA2310 (out; 0x005D1D80, 0x005D1DB0,
//     0x005D1E30, 0x005D1EB0) and 0x00AA2280 (in; 0x005D1930, 0x005D1960,
//     0x005D19C0, 0x005D1A20): slots 0x10, 0x14, 0x1C, 0x24 one argument,
//     0x48 (0x005D16B0) none;
//   - the object's own slots: 0x4 AddRef (0x005E28F0) none, 0x28 LoadModels
//     (0x005E0050) one, 0x2C UnloadModels (0x005DEE00) none; the occupant
//     part's 0x14 IsInitialized (0x00458A40) none;
//   - the lot manager (0x00AB37C0) slot 0x18 (0x0072B410) three; the lot
//     (0x00AB19B0) slots 0x30 GetFlags (0x006C2030) and 0x40 GetAltitude
//     (0x006C2060; a float in ST0) none;
//   - the terrain (0x00AB3D18) slot 0x38 (0x007413E0; a float in ST0) two;
//   - the occupant manager (0x00A8F7F0) slot 0x60 MoveOccupant (0x0049F3C0)
//     two;
//   - the city view (0x00ABCE40) slot 0x48 (0x007E57C0: its occupant filter,
//     referenced, into the pointer) one; an occupant filter's slot 0xC
//     (0x006AFD10 in 0x00AB1374) one, 0x8 Release none.
// The plain functions called at their addresses: SC4BitVector::Erase
// 0x004780D0 ret 0x14, SC4BitVector::Initialize 0x004A46F0 ret 4,
// SC4BitVector::PushBack 0x005DFFE0 ret 4, SC4PointVector::InsertN
// 0x007C61A0 ret 0x14, SC4Vector16::InsertFill 0x005E0700 ret 0xC, the
// generator's draw 0x009136B1 ret 4, the update of the models 0x005DF640
// ret 4; cdecl: 0x005E08F0 (one argument), SC4::stl_allocate, SC4::Free,
// the streams' vector helpers 0x005DA580 and 0x005DA550 (two).
#include "core/texocc_sim.h"

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

namespace opensc4::texocc_sim {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("texocc: first %s", name);
}

namespace {

template <typename R, typename... Args>
R CallThis(std::uintptr_t address, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(address)(self, args...);
}

template <typename R, typename... Args>
R CallCdecl(std::uintptr_t address, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(address)(args...);
}

// The game's std::vector of cells (pairs of int32) and std::vector<bool>
// (STLport: two bit iterators and the end of the storage), as
// GetCellCoverage's caller hands them in.
struct CellVector {
    int32_t* begin;
    int32_t* end;
    int32_t* capacity;
};

struct BitVector {
    uint32_t* startWord;
    int32_t startBit;
    uint32_t* finishWord;
    int32_t finishBit;
    uint32_t* endOfStorage;
};

Entry*& Begin(char* self) { return At<Entry*>(self, field::kEntries); }
Entry*& End(char* self) { return At<Entry*>(self, field::kEntriesEnd); }
Entry*& Capacity(char* self) { return At<Entry*>(self, field::kEntriesCapacity); }

// The number of entries as the game computes it everywhere: the byte
// distance shifted right by four, signed.
int32_t Count(char* self)
{
    return static_cast<int32_t>(reinterpret_cast<std::uintptr_t>(End(self)) -
                                reinterpret_cast<std::uintptr_t>(Begin(self))) >>
           4;
}

// push_back on the entry vector, as inlined twice in
// SetTextureSpecification (0x005E0BC3, 0x005E0EB0): in place when there is
// room; else new storage of the size plus the larger of the size and 1 from
// the STL allocator, the old entries and the new one copied, the old
// storage freed after.
void PushBack(char* self, const Entry* entry)
{
    Entry* const end = End(self);
    if (end != Capacity(self)) {
        if (end)
            *end = *entry;
        End(self) = end + 1;
        return;
    }
    const uint32_t size = static_cast<uint32_t>(Count(self));
    const uint32_t capacity = size + (size < 1 ? 1 : size);
    Entry* const fresh = capacity ? CallCdecl<Entry*, uint32_t>(addr::SC4_stl_allocate, capacity << 4) : nullptr;
    Entry* out = fresh;
    for (const Entry* from = Begin(self); from != end; ++from, ++out) {
        if (out)
            *out = *from;
    }
    if (out)
        *out = *entry;
    ++out;
    if (Begin(self))
        CallCdecl<void, void*>(addr::SC4_Free, Begin(self));
    Begin(self) = fresh;
    End(self) = out;
    Capacity(self) = fresh + capacity;
}

// 0x005DF370 (`self` is the object): the box around the entries.
//
// The lot at the first entry's cell (the lot manager's slot 6; the entry is
// read without a look at the count, as in the game) gives a level when it
// has flag 1: its altitude. Then per entry the smallest and largest cell
// in x and z, and the heights: with a level (not below zero) the low height
// is the level and the high one the level + 0.1; without one the four
// corners of the cell are asked of the terrain (slot 14, (x, z), (x, z + 1),
// (x + 1, z), (x + 1, z + 1)), each lowering the low height or raising the
// high one, and a high height not above the low one becomes the low + 0.1.
// The heights carry over from entry to entry. The terrain's answer is
// compared as it comes back on the FPU and stored as a float.
//
// The box: the cells times 16 drawn in by 0.1 (0x00AA2BF8) on each side;
// the sums are made on the FPU from the integers and stored as floats.
// *changed (when asked for) is whether one of the six coordinates moved by
// 0.0001 (0x00AA2C14) or more: cleared first, set unless every difference
// is below that.
void UpdateBounds(char* self, uint8_t* changed)
{
    static bool logged = false;
    FirstCall("UpdateBounds", logged);
    float old[6];
    std::memcpy(old, self + field::kBoxLow, sizeof(old));
    const Entry* const first = Begin(self);
    const uint32_t firstZ = first->z;
    const uint32_t firstX = first->x;
    void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x18, firstX, firstZ, 0u);
    float level = -1.0f;
    if (lot && (Virtual<uint8_t>(lot, 0x30) & 1))
        level = Virtual<float>(lot, 0x40);
    void* const terrain = Global<void*>(addr::SC4_Terrain);
    const double inset = static_cast<double>(Global<float>(addr::cSC4LotBaseTextureOccupant_BoxInset));
    int32_t minX = 0x7FFFFFFF, minZ = 0x7FFFFFFF;
    int32_t maxX = INT32_MIN, maxZ = INT32_MIN;
    float low = 3.40282347e+38f; // 0x7F7FFFFF
    float high = -2147483648.0f; // 0xCF000000
    const int32_t count = Count(self);
    for (int32_t i = 0; i < count; ++i) {
        const Entry* const entry = Begin(self) + i;
        const int32_t x = entry->x;
        if (minX > x)
            minX = x;
        const int32_t z = entry->z;
        if (minZ > z)
            minZ = z;
        if (maxX < x)
            maxX = x;
        if (maxZ < z)
            maxZ = z;
        // fcomp against 0.0 (0x00A81054): the terrain is asked when the
        // level is below zero (or not a number).
        const float zero = Global<float>(addr::SC4_FloatZero);
        if (!(level < zero || level != level)) {
            low = level;
            high = static_cast<float>(static_cast<double>(level) + inset);
            continue;
        }
        for (int32_t vx = x; vx <= static_cast<int32_t>(entry->x) + 1; ++vx) {
            for (int32_t vz = z; vz <= static_cast<int32_t>(entry->z) + 1; ++vz) {
                const double height = Virtual<double>(terrain, 0x38, vx, vz);
                if (static_cast<double>(low) > height)
                    low = static_cast<float>(height);
                if (static_cast<double>(high) < height)
                    high = static_cast<float>(height);
            }
        }
        if (high <= low)
            high = static_cast<float>(static_cast<double>(low) + inset);
    }
    // With no entry the limits are still the starting ones; the shifts wrap
    // as the game's 32-bit ones do.
    const auto times16 = [](int32_t value) {
        return static_cast<double>(static_cast<int32_t>(static_cast<uint32_t>(value) << 4));
    };
    const auto next16 = [](int32_t value) {
        return static_cast<double>(static_cast<int32_t>((static_cast<uint32_t>(value) + 1u) << 4));
    };
    At<float>(self, field::kBoxLow) = static_cast<float>(times16(minX) + inset);
    At<float>(self, field::kBoxLow + 4) = low;
    At<float>(self, field::kBoxLow + 8) = static_cast<float>(times16(minZ) + inset);
    At<float>(self, field::kBoxHigh) = static_cast<float>(next16(maxX) - inset);
    At<float>(self, field::kBoxHigh + 4) = high;
    At<float>(self, field::kBoxHigh + 8) = static_cast<float>(next16(maxZ) - inset);
    if (!changed)
        return;
    *changed = 0;
    const double epsilon = static_cast<double>(Global<float>(addr::cSC4LotBaseTextureOccupant_BoxEpsilon));
    for (std::size_t i = 0; i < 6; ++i) {
        double difference =
            static_cast<double>(old[i]) - static_cast<double>(At<float>(self, field::kBoxLow + 4 * i));
        if (difference < 0)
            difference = -difference;
        if (!(difference < epsilon)) {
            *changed = 1;
            return;
        }
    }
}

// --- The primary interface (`self` is the object) ---------------------------------

// Slot 0 (0x005DEA80): the base texture interface here (the object, counted
// through its slot 1), everything else from the occupant base (a jump, with
// the object + 4). The class ID is not answered here.
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    static bool logged = false;
    FirstCall("QueryInterface", logged);
    if (iid == kIidBaseTexture) {
        *object = self;
        Virtual<uint32_t>(self, slot::kAddRef);
        return 1;
    }
    return CallThis<uint8_t, uint32_t, void**>(addr::cSC4Occupant_QueryInterface, self + field::kOccupant, iid, object);
}

// The other parts' slot 0 (0x005E0650, 0x005E0660, 0x005E0670, 0x005E0690).
template <std::size_t Offset>
uint8_t __fastcall QueryInterfaceAt(char* part, void*, uint32_t iid, void** object)
{
    return QueryInterface(part - Offset, nullptr, iid, object);
}

// Slot 4 (0x005E0630): the byte, sign extended.
int32_t __fastcall GetUsage(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetUsage", logged);
    return At<int8_t>(self, field::kUsage);
}

// Slot 5 (0x005E0640): the argument's low byte stored. The game leaves it
// in AL and the rest of EAX as it was.
uint32_t __fastcall SetUsage(char* self, void*, uint32_t usage)
{
    static bool logged = false;
    FirstCall("SetUsage", logged);
    At<uint8_t>(self, field::kUsage) = static_cast<uint8_t>(usage);
    return usage;
}

// Slot 6 (0x005E0A00): the caller's vector of cells emptied (its end set to
// its begin) and, when given, its bit vector (SC4BitVector::Erase from its
// start to its finish; the iterator that returns is not used); then per
// entry the cell appended (in place when there is room, else through
// SC4PointVector::InsertN: one copy at the end) and whether the entry's
// alpha is 0xFF pushed on the bit vector. The result is the number of
// entries, counted before anything is touched.
int32_t __fastcall GetCellCoverage(char* self, void*, CellVector* cells, BitVector* covered)
{
    static bool logged = false;
    FirstCall("GetCellCoverage", logged);
    const int32_t count = Count(self);
    cells->end = cells->begin;
    if (covered) {
        uint32_t result[2] = {};
        CallThis<void*, uint32_t*, uint32_t*, int32_t, uint32_t*, int32_t>(
            addr::SC4BitVector_Erase, covered, static_cast<uint32_t*>(result), covered->startWord, covered->startBit,
            covered->finishWord, covered->finishBit);
    }
    const int32_t entries = Count(self);
    for (int32_t i = 0; i < entries; ++i) {
        const Entry* const entry = Begin(self) + i;
        int32_t cell[2] = {entry->x, entry->z};
        int32_t* const end = cells->end;
        if (end != cells->capacity) {
            if (end) {
                end[0] = cell[0];
                end[1] = cell[1];
            }
            cells->end += 2;
        } else {
            uint32_t unused = 0; // the game passes the address of an argument's slot; the callee does not read it
            CallThis<void, int32_t*, int32_t*, uint32_t*, uint32_t, uint32_t>(
                addr::SC4PointVector_InsertN, cells, end, static_cast<int32_t*>(cell), &unused, 1u, 1u);
        }
        if (covered)
            CallThis<void, uint32_t>(addr::SC4BitVector_PushBack, covered, entry->alpha == 0xFF ? 1u : 0u);
    }
    return count;
}

// Slot 7 (0x005DF310): whether an entry lies on the cell. With a pointer:
// *fully is cleared first and set when such an entry has alpha 0xFF, where
// the search ends; without one every entry is looked at. The cell's
// coordinates are compared whole with the entries' bytes.
uint8_t __fastcall IsCellCovered(char* self, void*, uint32_t x, uint32_t z, uint8_t* fully)
{
    static bool logged = false;
    FirstCall("IsCellCovered", logged);
    uint8_t found = 0;
    if (fully)
        *fully = 0;
    const int32_t count = Count(self);
    for (int32_t i = 0; i < count; ++i) {
        const Entry* const entry = Begin(self) + i;
        if (entry->x != x || entry->z != z)
            continue;
        found = 1;
        if (fully && entry->alpha == 0xFF) {
            *fully = 1;
            return found;
        }
    }
    return found;
}

// Slot 9 (0x005DF820): the box worked out again; when `always` is set, or
// the box changed and `whenChanged` is set, an occupant with flag 1 (in the
// occupant manager) is moved there (slot 24) and the models are updated
// (the game's 0x005DF640 with 0). *changed (when asked for) gets whether
// the box changed. EAX is left holding that pointer.
uint8_t* __fastcall UpdateVertexAltitudes(char* self, void*, uint32_t whenChanged, uint32_t always, uint8_t* changed)
{
    static bool logged = false;
    FirstCall("UpdateVertexAltitudes", logged);
    uint8_t moved = 0;
    UpdateBounds(self, &moved);
    if (static_cast<uint8_t>(always) || (moved && static_cast<uint8_t>(whenChanged))) {
        if (At<uint8_t>(self, field::kFlags) & 1)
            Virtual<uint8_t>(Global<void*>(addr::SC4_NetworkService), 0x60, self + field::kOccupant, 0u);
        CallThis<void, uint32_t>(addr::cSC4LotBaseTextureOccupant_UpdateModels, self, 0u);
    }
    if (changed)
        *changed = moved;
    return changed;
}

// Slot 8 (0x005E0B20). Nothing at all with a count of 0. Else, in the
// game's order:
//   - a copy of the entries in storage from the STL allocator (0x005E0550);
//   - in the copy, each entry whose picture is 0xFF gets one: the texture's
//     number of pictures (the game's 0x005E08F0), and with more than one a
//     draw below that number from the class's generator (0x00B23078), else
//     0;
//   - the occupant had no entry: the copies are appended (the models are
//     not touched);
//   - it had the same number: a bit vector of that size (SC4BitVector::
//     Initialize, its words cleared) marks the old entries taken; each new
//     entry replaces the first old one not yet taken on the same cell; as
//     soon as one finds none the pairing stops (the replacements made stay).
//     The bit vector's storage is freed. All paired: the models are updated
//     (the game's 0x005DF640 with 1);
//   - another number, or the pairing failed: UnloadModels (slot 11), the
//     list emptied and the copies appended, and LoadModels (slot 10) with
//     the answer of the city view's occupant filter for this occupant, when
//     the view has a filter;
//   - the box (UpdateBounds with the caller's pointer) and the copy freed.
void __fastcall SetTextureSpecification(char* self, void*, const Entry* entries, uint32_t count, uint8_t* changed)
{
    static bool logged = false;
    FirstCall("SetTextureSpecification", logged);
    const uint32_t oldCount = static_cast<uint32_t>(Count(self));
    if (count == 0)
        return;
    // 0x005E0550: the storage's size is the byte distance of the range.
    const int32_t copied = static_cast<int32_t>(count << 4) >> 4;
    Entry* const copy = copied ? CallCdecl<Entry*, uint32_t>(addr::SC4_stl_allocate, static_cast<uint32_t>(copied) << 4)
                               : nullptr;
    if (copy) {
        for (uint32_t i = 0; i < count; ++i)
            copy[i] = entries[i];
    }
    for (uint32_t i = 0; i < count; ++i) {
        if (copy[i].picture != 0xFF)
            continue;
        const int32_t pictures =
            CallCdecl<int32_t, uint32_t>(addr::cSC4LotBaseTextureOccupant_PictureCount, copy[i].texture);
        if (pictures > 1)
            copy[i].picture = static_cast<uint8_t>(CallThis<uint32_t, uint32_t>(
                addr::SC4Random_BelowLcg, reinterpret_cast<void*>(addr::cSC4LotBaseTextureOccupant_Random),
                static_cast<uint32_t>(pictures)));
        else
            copy[i].picture = 0;
    }
    bool rebuilt = false;
    if (oldCount == 0) {
        for (uint32_t i = 0; i < count; ++i)
            PushBack(self, &copy[i]);
    } else {
        bool same = count == oldCount;
        if (same) {
            BitVector taken = {};
            CallThis<void, uint32_t>(addr::SC4BitVector_Initialize, &taken, count);
            uint32_t* const words = taken.startWord;
            for (uint32_t* word = words; word != taken.endOfStorage; ++word)
                *word = 0;
            for (uint32_t i = 0; i < count && same; ++i) {
                const uint32_t x = copy[i].x;
                const uint32_t z = copy[i].z;
                uint32_t j = 0;
                for (; j < oldCount; ++j) {
                    // The bit of old entry j: the start's bit offset plus j,
                    // split into a word and a bit.
                    const int32_t index = taken.startBit + static_cast<int32_t>(j);
                    uint32_t* const word = words + (index >> 5);
                    const uint32_t bit = 1u << (index & 31);
                    if (*word & bit)
                        continue;
                    Entry* const old = Begin(self) + j;
                    if (old->x != x || old->z != z)
                        continue;
                    *old = copy[i];
                    *word |= bit;
                    break;
                }
                if (j == oldCount)
                    same = false;
            }
            if (words)
                CallCdecl<void, void*>(addr::SC4_Free, words);
        }
        if (same) {
            CallThis<void, uint32_t>(addr::cSC4LotBaseTextureOccupant_UpdateModels, self, 1u);
        } else {
            rebuilt = true;
            Virtual<void>(self, slot::kUnloadModels);
            // SC4Vector16::Copy(end, end, begin): nothing copied, the begin
            // returned and stored as the end.
            End(self) = Begin(self);
            for (uint32_t i = 0; i < count; ++i)
                PushBack(self, &copy[i]);
        }
    }
    if (rebuilt) {
        void* filter = nullptr;
        Virtual<void>(Global<void*>(addr::SC4_Service43DC4), 0x48, &filter);
        if (filter) {
            const uint32_t shown = Virtual<uint32_t>(filter, 0xC, self + field::kOccupant);
            Virtual<void>(self, slot::kLoadModels, shown);
            Virtual<uint32_t>(filter, 0x8);
        }
    }
    UpdateBounds(self, changed);
    if (copy)
        CallCdecl<void, void*>(addr::SC4_Free, copy);
}

// --- The occupant part (`part` is the object + 4) ---------------------------------

// Slot 4 (0x005DEA60): an initialized occupant (slot 5) drops its models
// (the object's slot 11) and shuts the base down (0x005E8980); true either
// way.
uint8_t __fastcall Shutdown(char* part, void*)
{
    static bool logged = false;
    FirstCall("Shutdown", logged);
    if (Virtual<uint8_t>(part, occupant::kIsInitialized)) {
        Virtual<void>(part - field::kOccupant, slot::kUnloadModels);
        CallThis<uint8_t>(addr::cSC4Occupant_Shutdown, part);
    }
    return 1;
}

// Slot 8 (0x005DED20): the middle of the box: per axis the high and the low
// corner added on the FPU, times 0.5 (0x00A84D2C), stored as a float. EAX
// is left holding the pointer.
float* __fastcall GetPosition(char* part, void*, float* position)
{
    static bool logged = false;
    FirstCall("GetPosition", logged);
    char* const self = part - field::kOccupant;
    const double half = static_cast<double>(Global<float>(addr::SC4_FloatHalf));
    for (std::size_t i = 0; i < 3; ++i)
        position[i] = static_cast<float>((static_cast<double>(At<float>(self, field::kBoxHigh + 4 * i)) +
                                          static_cast<double>(At<float>(self, field::kBoxLow + 4 * i))) *
                                         half);
    return position;
}

// --- The other parts ----------------------------------------------------------------

// +0x8 slot 5 (0x005E0620).
uint32_t __fastcall GetGZCLSID(void*, void*)
{
    static bool logged = false;
    FirstCall("GetGZCLSID", logged);
    return kClassId;
}

// --- Save ---------------------------------------------------------------------------

// +0x8 slot 3 (0x005DF1E0; `part` = object + 8): version 2, the occupant
// base's record, then the box's low and high corner, the usage (the byte
// zero extended, through slot 0x10), the number of entries (through slot
// 0x24) and per entry the texture (slot 0x24) and its bytes +4 to +0xD
// (each zero extended, through slot 0x14); the entry's last two bytes are
// not saved. The layout is in tools/re/save_schemas/texocc.py. When the
// base fails the class's own fields are left out, and the result is in
// both cases whether the stream is free of error after.
uint32_t __fastcall Write(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    char* const self = part - field::kSerializable;
    Virtual<uint8_t>(stream, 0x1C, 2u);
    if (CallThis<uint8_t, void*>(addr::cSC4Occupant_Write, part, stream)) {
        CallCdecl<void*, void*, void*>(addr::SC4Stream_WriteVector3, stream, self + field::kBoxLow);
        CallCdecl<void*, void*, void*>(addr::SC4Stream_WriteVector3, stream, self + field::kBoxHigh);
        Virtual<uint8_t>(stream, 0x10, uint32_t{At<uint8_t>(self, field::kUsage)});
        Virtual<uint8_t>(stream, 0x24, static_cast<uint32_t>(Count(self)));
        const int32_t count = Count(self);
        for (int32_t i = 0; i < count; ++i) {
            const Entry* const entry = Begin(self) + i;
            Virtual<uint8_t>(stream, 0x24, entry->texture);
            const auto* const bytes = reinterpret_cast<const uint8_t*>(entry);
            for (std::size_t offset = 0x4; offset <= 0xD; ++offset)
                Virtual<uint8_t>(stream, 0x14, uint32_t{bytes[offset]});
        }
    }
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// 0x005E1020 (`vector` is the entry vector, begin/end/capacity): cut to a
// count (SC4Vector16::Copy of nothing, which returns the new end) or grown
// by copies of a new entry through the game's SC4Vector16::InsertFill
// (also with nothing to add, as the game). The new entry: zeros, 0xFF in
// its bytes 8 to 0xD. Its last two bytes are stack the game never writes:
// zeros here.
void ResizeEntries(char* vector, uint32_t count)
{
    static bool logged = false;
    FirstCall("ResizeEntries", logged);
    Entry*& begin = At<Entry*>(vector, 0x0);
    Entry*& end = At<Entry*>(vector, 0x4);
    const uint32_t size = static_cast<uint32_t>(
        static_cast<int32_t>(reinterpret_cast<std::uintptr_t>(end) - reinterpret_cast<std::uintptr_t>(begin)) >> 4);
    if (count < size) {
        end = reinterpret_cast<Entry*>(reinterpret_cast<char*>(begin) + (count << 4));
        return;
    }
    Entry fresh = {};
    std::memset(reinterpret_cast<uint8_t*>(&fresh) + 8, 0xFF, 6);
    CallThis<void, Entry*, uint32_t, Entry*>(addr::SC4Vector16_InsertFill, vector, end, count - size, &fresh);
}

// +0x8 slot 4 (0x005E10B0; `part` = object + 8): version 2 only. The game
// reads the version into its argument's stack slot (the stream pointer
// until the stream writes two bytes of it), so it starts with that value
// here. Another version: AL cleared, the rest of EAX as the stream's read
// left it. As in Write, a failing base skips the fields and the result is
// the stream's state. The entry count is read into stack the game never
// wrote (a stream that fails leaves it): zero here. The entries are resized
// to it and each one's first 14 bytes read.
uint32_t __fastcall Read(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Read", logged);
    char* const self = part - field::kSerializable;
    uint32_t version = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(stream));
    const uint32_t read = Virtual<uint32_t>(stream, 0x1C, &version);
    if (static_cast<uint16_t>(version) != 2)
        return read & 0xFFFFFF00u;
    if (CallThis<uint8_t, void*>(addr::cSC4Occupant_Read, part, stream)) {
        CallCdecl<void*, void*, void*>(addr::SC4Stream_ReadVector3, stream, self + field::kBoxLow);
        CallCdecl<void*, void*, void*>(addr::SC4Stream_ReadVector3, stream, self + field::kBoxHigh);
        Virtual<uint8_t>(stream, 0x10, self + field::kUsage);
        uint32_t count = 0;
        Virtual<uint8_t>(stream, 0x24, &count);
        ResizeEntries(self + field::kEntries, count);
        const int32_t entries = Count(self);
        for (int32_t i = 0; i < entries; ++i) {
            auto* const bytes = reinterpret_cast<uint8_t*>(Begin(self) + i);
            Virtual<uint8_t>(stream, 0x24, bytes);
            for (std::size_t offset = 0x4; offset <= 0xD; ++offset)
                Virtual<uint8_t>(stream, 0x14, bytes + offset);
        }
    }
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// The holder part's slots 23 and 24 (0x005E06C0, 0x005E11F0): this - 4.
uint32_t __fastcall WriteThunk(char* holder, void*, void* stream) { return Write(holder - 0x4, nullptr, stream); }
uint32_t __fastcall ReadThunk(char* holder, void*, void* stream) { return Read(holder - 0x4, nullptr, stream); }

// --- Installation -------------------------------------------------------------------

struct Slot {
    std::uintptr_t vtable;
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(vtable, index, original, fn)                                                                    \
    {addr::cSC4LotBaseTextureOccupant_##vtable, index, addr::cSC4LotBaseTextureOccupant_##original,                  \
     reinterpret_cast<void*>(&fn)}

const Slot kSlots[] = {
    OPENSC4_SLOT(vftable, 0, QueryInterface, QueryInterface),
    OPENSC4_SLOT(vftable, 4, GetUsage, GetUsage),
    OPENSC4_SLOT(vftable, 5, SetUsage, SetUsage),
    OPENSC4_SLOT(vftable, 6, GetCellCoverage, GetCellCoverage),
    OPENSC4_SLOT(vftable, 7, IsCellCovered, IsCellCovered),
    OPENSC4_SLOT(vftable, 8, SetTextureSpecification, SetTextureSpecification),
    OPENSC4_SLOT(vftable, 9, UpdateVertexAltitudes, UpdateVertexAltitudes),
    OPENSC4_SLOT(vftable_4, 0, QueryInterfaceThunk4, QueryInterfaceAt<0x4>),
    OPENSC4_SLOT(vftable_4, 4, Shutdown, Shutdown),
    OPENSC4_SLOT(vftable_4, 8, GetPosition, GetPosition),
    OPENSC4_SLOT(vftable_8, 0, QueryInterfaceThunk8, QueryInterfaceAt<0x8>),
    OPENSC4_SLOT(vftable_8, 5, GetGZCLSID, GetGZCLSID),
    OPENSC4_SLOT(vftable_C, 0, QueryInterfaceThunkC, QueryInterfaceAt<0xC>),
    OPENSC4_SLOT(vftable_20, 0, QueryInterfaceThunk20, QueryInterfaceAt<0x20>),
};
const Slot kSaveSlots[] = {
    OPENSC4_SLOT(vftable_8, 3, Write, Write),
    OPENSC4_SLOT(vftable_8, 4, Read, Read),
    OPENSC4_SLOT(vftable_C, 23, WriteThunk, WriteThunk),
    OPENSC4_SLOT(vftable_C, 24, ReadThunk, ReadThunk),
};

#undef OPENSC4_SLOT

template <std::size_t N>
bool SlotsHoldOriginals(const Slot (&slots)[N], const char* what)
{
    for (const Slot& slot : slots) {
        const auto* const vtable = reinterpret_cast<const std::uintptr_t*>(slot.vtable);
        if (vtable[slot.index] != slot.original) {
            log::Write("texocc %s: slot %d of %08X holds %08X, expected %08X; not installing", what, slot.index,
                       static_cast<unsigned>(slot.vtable), static_cast<unsigned>(vtable[slot.index]),
                       static_cast<unsigned>(slot.original));
            return false;
        }
    }
    return true;
}

template <std::size_t N>
bool SwapSlots(const Slot (&slots)[N], const char* what)
{
    if (!game::IsBuild641() || !SlotsHoldOriginals(slots, what))
        return false;
    for (const Slot& slot : slots) {
        auto* const entry = reinterpret_cast<std::uintptr_t*>(slot.vtable) + slot.index;
        DWORD old = 0;
        if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &old))
            return false;
        *entry = reinterpret_cast<std::uintptr_t>(slot.ours);
        VirtualProtect(entry, sizeof(*entry), old, &old);
    }
    log::Write("texocc %s: %u slots are ours", what, static_cast<unsigned>(N));
    return true;
}

// None of the slots is called directly from outside the class (the
// QueryInterface thunks jump to 0x005DEA80 and the save thunks to Write and
// Read inside it; the class registration's call of GetGZCLSID at 0x0046080E
// runs before any replacement is installed; the box's 0x005DF370 is called
// by two functions that are both ours here) and no other class's vtable
// holds one, so they are all slot swaps: no detour, no thunk. The game's
// model code calls none of them directly either.
bool InstallSlots(registry::Mode) { return SwapSlots(kSlots, "slots"); }

bool InstallSave(registry::Mode) { return SwapSlots(kSaveSlots, "save"); }

const bool g_registered[] = {
    registry::Register({"simulators.texocc.slots", "simulators/texocc", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4LotBaseTextureOccupant_vftable,
                        "Our cSC4LotBaseTextureOccupant slots: QueryInterface and the parts' thunks, GetUsage, "
                        "SetUsage, GetCellCoverage, IsCellCovered, SetTextureSpecification, UpdateVertexAltitudes "
                        "(with the box), Shutdown, GetPosition, GetGZCLSID (src/core/texocc_sim.h)",
                        false, InstallSlots}),
    registry::Register({"simulators.texocc.save", "simulators/texocc", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4LotBaseTextureOccupant_vftable_8,
                        "Our cSC4LotBaseTextureOccupant Write and Read (the save record, version 2; "
                        "src/core/texocc_sim.h)",
                        false, InstallSave}),
};

// --- Our object ---------------------------------------------------------------------

// Copies of the game's five vtables, taken at the first construction, after
// every PostAppInit slot swap of the other groups.
constexpr int kSlots0 = 12, kSlots4 = 34, kSlots8 = 6, kSlotsC = 25, kSlots20 = 5;
void* g_vtable[kSlots0];
void* g_vtable4[kSlots4];
void* g_vtable8[kSlots8];
void* g_vtableC[kSlotsC];
void* g_vtable20[kSlots20];

void SetVtables(char* self)
{
    At<void**>(self, 0x0) = g_vtable;
    At<void**>(self, field::kOccupant) = g_vtable4;
    At<void**>(self, field::kSerializable) = g_vtable8;
    At<void**>(self, field::kHolder) = g_vtableC;
    At<void**>(self, field::kExemplarPart) = g_vtable20;
}

// An item of the base texture pool (0x005DECF0, and inline in the factory):
// the current page's free head, the pool refilled when the page has none
// (0x0090CF9A returns the new head); the page is read again after. The
// item's memory is not cleared.
void* PoolAllocate()
{
    char* const pool = reinterpret_cast<char*>(addr::cSC4LotBaseTextureOccupant_Pool);
    void* item = At<void*>(At<char*>(pool, 0x4), 0x8);
    if (!item)
        item = CallThis<void*>(addr::SC4EdgePool_Grow, pool);
    At<void*>(At<char*>(pool, 0x4), 0x8) = *static_cast<void**>(item);
    ++At<uint32_t>(At<char*>(pool, 0x4), 0xC);
    return item;
}

// The deleting destructor (0x005E0860): the class's vtables, the entries'
// storage freed (the vector's fields are left as they are), the occupant
// base destroyed, and with flag 1 the memory back to the pool (the page
// header is at the address with its low 13 bits clear).
void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("DeletingDestructor", logged);
    SetVtables(self);
    if (Begin(self))
        CallCdecl<void, void*>(addr::SC4_Free, Begin(self));
    CallThis<void>(addr::cSC4Occupant_Destroy, self + field::kOccupant);
    if (flags & 1) {
        char* const page = reinterpret_cast<char*>(reinterpret_cast<std::uintptr_t>(self) & 0xFFFFE000u);
        At<void*>(self, 0x0) = At<void*>(page, 0x8);
        const uint32_t used = At<uint32_t>(page, 0xC) - 1;
        At<void*>(page, 0x8) = self;
        At<uint32_t>(page, 0xC) = used;
        void* const pool = reinterpret_cast<void*>(addr::cSC4LotBaseTextureOccupant_Pool);
        if (used == 0)
            CallThis<void, void*>(addr::SC4Pool_ReleaseBlock, pool, page);
        else if (used == 0x4000003F)
            CallThis<void, void*>(addr::SC4Pool_ReopenBlock, pool, page);
    }
    return self;
}

// The holder part's slot 22 (0x005E0680).
void* __fastcall DeletingDestructorAtHolder(char* part, void*, uint32_t flags)
{
    return DeletingDestructor(part - field::kHolder, nullptr, flags);
}

void BuildVtables()
{
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4LotBaseTextureOccupant_vftable), sizeof(g_vtable));
    std::memcpy(g_vtable4, reinterpret_cast<const void*>(addr::cSC4LotBaseTextureOccupant_vftable_4),
                sizeof(g_vtable4));
    std::memcpy(g_vtable8, reinterpret_cast<const void*>(addr::cSC4LotBaseTextureOccupant_vftable_8),
                sizeof(g_vtable8));
    std::memcpy(g_vtableC, reinterpret_cast<const void*>(addr::cSC4LotBaseTextureOccupant_vftable_C),
                sizeof(g_vtableC));
    std::memcpy(g_vtable20, reinterpret_cast<const void*>(addr::cSC4LotBaseTextureOccupant_vftable_20),
                sizeof(g_vtable20));
    g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
    g_vtable4[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x4>);
    g_vtable8[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x8>);
    g_vtable8[5] = reinterpret_cast<void*>(&GetGZCLSID);
    g_vtableC[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0xC>);
    g_vtableC[22] = reinterpret_cast<void*>(&DeletingDestructorAtHolder);
    g_vtable20[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x20>);
    log::Write("texocc object: vtables copied (slots 6/7/8/9 %p %p %p %p, +4 slots 4/8 %p %p, +8 slots 3/4 %p %p)",
               g_vtable[6], g_vtable[7], g_vtable[8], g_vtable[9], g_vtable4[4], g_vtable4[8], g_vtable8[3],
               g_vtable8[4]);
}

// The constructor (detour of 0x005E05B0): the occupant base constructed at
// its address on the object + 4, our vtables, the box, the usage and the
// entry vector zeroed, and last the occupant type. The bytes +0x51 to +0x53
// are left as the pool's memory holds them, as in the game (nothing reads
// or saves them).
void* __fastcall Construct(char* self, void*)
{
    static bool logged = false;
    FirstCall("Construct", logged);
    static bool built = false;
    if (!built) {
        built = true;
        BuildVtables();
    }
    CallThis<void*>(addr::cSC4Occupant_cSC4Occupant, self + field::kOccupant);
    SetVtables(self);
    for (std::size_t offset = field::kBoxLow; offset < field::kUsage; offset += 4)
        At<uint32_t>(self, offset) = 0;
    At<uint8_t>(self, field::kUsage) = 0;
    Begin(self) = nullptr;
    End(self) = nullptr;
    Capacity(self) = nullptr;
    At<uint32_t>(self, field::kType) = kOccupantType;
    static int created = 0;
    if (created++ < 3)
        log::Write("texocc object: constructed %p", static_cast<void*>(self));
    return self;
}

// The pool allocator (detour of 0x005DECF0; the size is not used).
void* __cdecl Allocate(std::size_t)
{
    static bool logged = false;
    FirstCall("Allocate", logged);
    return PoolAllocate();
}

// The factory (detour of 0x005E0820): an item of the pool, constructed (the
// game jumps to the constructor, so the result is the object itself, not
// its occupant part).
void* __cdecl Create()
{
    static bool logged = false;
    FirstCall("Create", logged);
    return Construct(static_cast<char*>(PoolAllocate()), nullptr);
}

using ConstructFn = void*(__fastcall*)(char*, void*);
using AllocateFn = void*(__cdecl*)(std::size_t);
using CreateFn = void*(__cdecl*)();
ConstructFn g_originalConstruct = nullptr;
AllocateFn g_originalAllocate = nullptr;
CreateFn g_originalCreate = nullptr;

// Every construction path: the factory (called through the class
// registration's pointer, by the framework for a save's reader, the lot
// developer and the landfill zone; it ends in a jump to the constructor) and
// the two fallbacks when the framework gives no object: the landfill zone's
// PlaceBaseTexture, 0x006BA503 (the allocator; add esp, cmp eax, ebx after)
// and 0x006BA511 (the constructor; mov esi, eax after), and the lot
// developer's GetLotBaseTextureOccupant, 0x006C423D (add esp, test eax, eax
// after) and 0x006C424B (EAX stored and used after). Our own versions of
// both (src/core/landfillzone_cells.cpp, lot_object.cpp) call the two at
// their addresses. None keeps anything in ECX or EDX across the calls. The
// deleting destructor is reached through the holder part's slot 22 only
// (0x005E0680 jumps to it), so the vtable copy replaces it without a
// detour; base textures made before this group is installed (there are
// none at PostAppInit) keep the game's.
bool ApplyObject(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    const addr::Entry* const construct = addr_check::Find(addr::cSC4LotBaseTextureOccupant_cSC4LotBaseTextureOccupant);
    const addr::Entry* const allocate = addr_check::Find(addr::cSC4LotBaseTextureOccupant_Allocate);
    const addr::Entry* const create = addr_check::Find(addr::cSC4LotBaseTextureOccupant_Create);
    return construct && allocate && create && hook::Install(*construct, &Construct, &g_originalConstruct) &&
           hook::Install(*allocate, &Allocate, &g_originalAllocate) &&
           hook::Install(*create, &Create, &g_originalCreate);
}

const bool g_registeredObject = registry::Register({
    "simulators.texocc.object",
    "simulators/texocc",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4LotBaseTextureOccupant_cSC4LotBaseTextureOccupant,
    "Our cSC4LotBaseTextureOccupant object: the constructor, the factory and the pool allocation (detours), the "
    "deleting destructor, identity; methods from copies of the (patched) game vtables taken once "
    "(src/core/texocc_sim.h)",
    false,
    ApplyObject,
});

} // namespace
} // namespace opensc4::texocc_sim
