// Our cSTETerrain save and load (build 641): how the city's terrain object
// (vtable 0x00AB3D18, the service 0x00B43CF4) is written into a saved city
// and read back. Group simulators.terrain.save.
//
// The terrain is saved in two layers:
// - the object's own record through cIGZSerializable (the part at +8, vtable
//   0x00AB3CF0): Write 0x00745300 and Read 0x00745380. It holds a version (2)
//   and a reference to the terrain view only; then Write gets the database
//   segment behind the stream and hands it to slot 97, Read to slot 96;
// - the data, as records of their own in that segment, each opened with the
//   game's record streams (0x005D9F00 to write, 0x005D9E70 to read; a uint16
//   version first, then the fields):
//     0x8A91E7E3 / 0xE98F9525 / 3   version 2: uint32 vertices per row,
//         uint32 rows, int32 terrain type, floats +0x70 (least height),
//         +0x74 (greatest height), +0x78;
//     0xA9DD6FF4 / 0xE98F9525 / 1   version 2: every vertex's altitude;
//     0x8A91E7E0 / 0xE98F9525 / 2   version 1: every vertex's flags word;
//     0xA9DD6FF4 / 0xE98F9525 / 0x64 to 0x67   version 1: the four edges
//         (uint32 count, then the altitudes), read by the neighbouring
//         cities' ReconcileEdges; written, never read back by this city.
//
// Slots of the primary vtable: 97 saves to a segment (0x007477C0), 96 loads
// from one (0x00747800) and records at +0x68 whether all three reads
// succeeded, 95 (0x00742170) is a jump to 96, and 90 (0x00742150) cuts every
// vertex's flags down to 0x18 and then saves (cSC4City::SaveObliterated,
// 0x00459970, calls it). No code outside the class calls any of the
// functions here directly (scan of every direct call in the exe; slots 97
// and 96 call the record functions, nothing else does), so there are no
// detours: six vtable slots.
//
// The record streams, the reference helpers and the stream's own methods are
// the game's shared save code, called at their addresses. The altitudes pass
// to and from the stream as their bits.
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/terrain_sim.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::terrain_sim {

// --- Additions for terrain_sim.h (to be merged there) --------------------------

namespace field {
constexpr std::size_t kSizeTypeFloat = 0x78; // float by the city's size type (Init); saved in the header record
} // namespace field

// The save records (type, group = the class ID, instance).
namespace record {
constexpr uint32_t kGroup = 0xE98F9525;
constexpr uint32_t kHeaderType = 0x8A91E7E3; // instance 3, version 2
constexpr uint32_t kAltitudeType = 0xA9DD6FF4; // instance 1, version 2; the edges are instances 0x64 to 0x67, version 1
constexpr uint32_t kFlagsType = 0x8A91E7E0;  // instance 2, version 1
constexpr uint32_t kEdgeInstance = 0x64;
} // namespace record

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

inline uint32_t& U32(void* self, std::size_t offset) { return At<uint32_t>(self, offset); }

constexpr uint32_t kIidView = 0xC9B84E10;          // the class the view reference is read as
constexpr uint32_t kIidPersistStream = 0x6999A8FB; // asked of the output stream for its segment
constexpr std::size_t kViewSerializable = 0x14;    // the view's part that is written as the reference

// The stream's methods (cIGZOStream and cIGZIStream have the same offsets:
// the writers take the value, the readers a pointer).
constexpr std::size_t kUint16 = 0x1C, kSint32 = 0x20, kUint32 = 0x24, kFloat32 = 0x30;

// A record stream as the game keeps it on its stack (0x10 bytes): the
// stream at +8 and, for a read record, its version at +0xC.
struct RecordStream {
    uint32_t words[2];
    void* stream;
    uint16_t version;
    uint16_t unused;
};
static_assert(sizeof(RecordStream) == 0x10);

bool OpenForWrite(RecordStream& record, void* segment, uint32_t type, uint32_t instance, uint32_t version)
{
    This<void*>(addr::SC4SegmentOStream_SC4SegmentOStream, &record, segment, type, record::kGroup, instance, version);
    return This<uint8_t>(addr::SC4SegmentStream_IsOpen, &record) != 0;
}

void CloseWritten(RecordStream& record) { This<void>(addr::SC4SegmentOStream_Close, &record); }

bool OpenForRead(RecordStream& record, void* segment, uint32_t type, uint32_t instance)
{
    This<void*>(addr::SC4SegmentIStream_SC4SegmentIStream, &record, segment, type, record::kGroup, instance);
    return This<uint8_t>(addr::SC4SegmentStream_IsOpen, &record) != 0;
}

void CloseRead(RecordStream& record) { This<void>(addr::SC4SegmentIStream_Close, &record); }

// 0x007453E0: the header record.
uint8_t WriteHeader(char* self, void* segment)
{
    static bool logged = false;
    FirstCall("WriteHeader", logged);
    RecordStream record;
    if (!OpenForWrite(record, segment, record::kHeaderType, 3, 2)) {
        CloseWritten(record);
        return 0;
    }
    Virtual<uint8_t>(record.stream, kUint32, U32(self, field::kVertsX));
    Virtual<uint8_t>(record.stream, kUint32, U32(self, field::kVertsZ));
    Virtual<uint8_t>(record.stream, kSint32, At<int32_t>(self, field::kTerrainType));
    Virtual<uint8_t>(record.stream, kFloat32, U32(self, field::kMinHeight));
    Virtual<uint8_t>(record.stream, kFloat32, U32(self, field::kMaxHeight));
    Virtual<uint8_t>(record.stream, kFloat32, U32(self, field::kSizeTypeFloat));
    CloseWritten(record);
    return 1;
}

// 0x00745490: the header read back. A record older than version 2 with a
// terrain type of 1 or more gets type 0. The vertex count follows from the
// two sizes read (nothing compares them with the city's).
uint8_t ReadHeader(char* self, void* segment)
{
    static bool logged = false;
    FirstCall("ReadHeader", logged);
    RecordStream record;
    if (!OpenForRead(record, segment, record::kHeaderType, 3)) {
        CloseRead(record);
        return 0;
    }
    Virtual<uint8_t>(record.stream, kUint32, self + field::kVertsX);
    Virtual<uint8_t>(record.stream, kUint32, self + field::kVertsZ);
    Virtual<uint8_t>(record.stream, kSint32, self + field::kTerrainType);
    Virtual<uint8_t>(record.stream, kFloat32, self + field::kMinHeight);
    Virtual<uint8_t>(record.stream, kFloat32, self + field::kMaxHeight);
    Virtual<uint8_t>(record.stream, kFloat32, self + field::kSizeTypeFloat);
    if (record.version < 2 && At<int32_t>(self, field::kTerrainType) >= 1)
        At<int32_t>(self, field::kTerrainType) = 0;
    U32(self, field::kVertexCount) = U32(self, field::kVertsZ) * U32(self, field::kVertsX);
    CloseRead(record);
    return 1;
}

// 0x00745560: every altitude, in the array's order; the count is read anew
// for each vertex, as the game does.
uint8_t WriteAltitudes(char* self, void* segment)
{
    static bool logged = false;
    FirstCall("WriteAltitudes", logged);
    RecordStream record;
    if (!OpenForWrite(record, segment, record::kAltitudeType, 1, 2)) {
        CloseWritten(record);
        return 0;
    }
    uint32_t offset = 0;
    for (uint32_t i = 0; i < U32(self, field::kVertexCount); ++i, offset += vertex::kSize)
        Virtual<uint8_t>(record.stream, kFloat32, AtAddress<uint32_t>(VertexBase(self) + offset));
    CloseWritten(record);
    return 1;
}

// 0x007455F0: version 2 only. Without a vertex array one is made first:
// operator new of count x 36 bytes, every record constructed (0x00741950
// through the array constructor 0x0041D490: the default word into +4, +8,
// +0xC; inline here) and then the whole array zeroed, so the constructor's
// work does not show. As the game, a failed allocation is stored and then
// zeroed through (a fault there as in the game).
uint8_t ReadAltitudes(char* self, void* segment)
{
    static bool logged = false;
    FirstCall("ReadAltitudes", logged);
    RecordStream record;
    if (!OpenForRead(record, segment, record::kAltitudeType, 1) || record.version != 2) {
        CloseRead(record);
        return 0;
    }
    if (!U32(self, field::kVertices)) {
        const uint32_t count = U32(self, field::kVertexCount);
        char* const records = Cdecl<char*>(addr::SC4_operator_new, count * 9u * 4u);
        if (records) {
            for (uint32_t i = 0; i < count; ++i) {
                char* const one = records + static_cast<std::size_t>(i) * vertex::kSize;
                At<uint32_t>(one, 0x4) = Global<uint32_t>(addr::cSTETerrain_DefaultVertexWord);
                At<uint32_t>(one, 0x8) = Global<uint32_t>(addr::cSTETerrain_DefaultVertexWord);
                At<uint32_t>(one, 0xC) = Global<uint32_t>(addr::cSTETerrain_DefaultVertexWord);
            }
        }
        At<char*>(self, field::kVertices) = records;
        std::memset(records, 0, U32(self, field::kVertexCount) * 9u * 4u);
    }
    uint32_t offset = 0;
    for (uint32_t i = 0; i < U32(self, field::kVertexCount); ++i, offset += vertex::kSize)
        Virtual<uint8_t>(record.stream, kFloat32, reinterpret_cast<void*>(static_cast<std::uintptr_t>(VertexBase(self) + offset)));
    CloseRead(record);
    return 1;
}

// 0x007456D0: every flags word, as a uint16.
uint8_t WriteFlags(char* self, void* segment)
{
    static bool logged = false;
    FirstCall("WriteFlags", logged);
    RecordStream record;
    if (!OpenForWrite(record, segment, record::kFlagsType, 2, 1)) {
        CloseWritten(record);
        return 0;
    }
    uint32_t offset = 0;
    for (uint32_t i = 0; i < U32(self, field::kVertexCount); ++i, offset += vertex::kSize)
        Virtual<uint8_t>(record.stream, kUint16,
                         static_cast<uint32_t>(AtAddress<uint16_t>(VertexBase(self) + offset + vertex::kFlags)));
    CloseWritten(record);
    return 1;
}

// 0x00745760: version 1 only, and only into an existing vertex array.
uint8_t ReadFlags(char* self, void* segment)
{
    static bool logged = false;
    FirstCall("ReadFlags", logged);
    RecordStream record;
    if (!OpenForRead(record, segment, record::kFlagsType, 2) || record.version != 1 || !U32(self, field::kVertices)) {
        CloseRead(record);
        return 0;
    }
    uint32_t offset = 0;
    for (uint32_t i = 0; i < U32(self, field::kVertexCount); ++i, offset += vertex::kSize)
        Virtual<uint8_t>(record.stream, kUint16,
                         reinterpret_cast<void*>(static_cast<std::uintptr_t>(VertexBase(self) + offset + vertex::kFlags)));
    CloseRead(record);
    return 1;
}

// 0x00745800: one edge's altitudes for the neighbouring city on that side.
// Edges 0 and 2 are columns (x = 0 and x = cells x): the row count, then one
// altitude per row. Any other number is a row (3: z = cells z; else z = 0):
// the row length, then its altitudes. Signed compares, as the game.
uint8_t WriteEdge(char* self, uint32_t edge, void* segment)
{
    RecordStream record;
    if (!OpenForWrite(record, segment, record::kAltitudeType, edge + record::kEdgeInstance, 1)) {
        CloseWritten(record);
        return 0;
    }
    if (edge == 0 || edge == 2) {
        int32_t last = static_cast<int32_t>(U32(self, field::kCellsZ) * U32(self, field::kVertsX));
        int32_t index = 0;
        if (edge == 2) {
            index = static_cast<int32_t>(U32(self, field::kCellsX));
            last += index;
        }
        Virtual<uint8_t>(record.stream, kUint32, U32(self, field::kVertsZ));
        if (index <= last) {
            do {
                Virtual<uint8_t>(record.stream, kFloat32,
                                 AtAddress<uint32_t>(VertexBase(self) + static_cast<uint32_t>(index) * 9u * 4u));
                index += static_cast<int32_t>(U32(self, field::kVertsX));
            } while (index <= last);
        }
    } else {
        int32_t last = static_cast<int32_t>(U32(self, field::kCellsX));
        int32_t first = 0;
        if (edge == 3) {
            first = static_cast<int32_t>(U32(self, field::kVertsX) * U32(self, field::kCellsZ));
            last += first;
        }
        Virtual<uint8_t>(record.stream, kUint32, U32(self, field::kVertsX));
        if (first <= last) {
            uint32_t offset = static_cast<uint32_t>(first) * 9u * 4u;
            uint32_t left = static_cast<uint32_t>(last - first) + 1u;
            do {
                Virtual<uint8_t>(record.stream, kFloat32, AtAddress<uint32_t>(VertexBase(self) + offset));
                offset += vertex::kSize;
            } while (--left != 0);
        }
    }
    CloseWritten(record);
    return 1;
}

// Slot 97 (0x007477C0): the three records and the four edges. Always true;
// a record that does not open is skipped silently.
uint8_t __fastcall SaveToSegment(char* self, void*, void* segment)
{
    static bool logged = false;
    FirstCall("SaveToSegment", logged);
    WriteHeader(self, segment);
    WriteAltitudes(self, segment);
    WriteFlags(self, segment);
    for (uint32_t edge = 0; edge < 4; ++edge)
        WriteEdge(self, edge, segment);
    return 1;
}

// Slot 96 (0x00747800): header, altitudes, flags, each only after the one
// before succeeded; +0x68 tells Init whether a terrain was loaded. Always
// true.
uint8_t __fastcall LoadFromSegment(char* self, void*, void* segment)
{
    static bool logged = false;
    FirstCall("LoadFromSegment", logged);
    uint8_t loaded = ReadHeader(self, segment);
    if (loaded) {
        loaded = ReadAltitudes(self, segment);
        if (loaded)
            loaded = ReadFlags(self, segment);
    }
    At<uint8_t>(self, field::kLoaded) = loaded;
    return 1;
}

// Slot 95 (0x00742170): a jump to slot 96 through the vtable.
uint8_t __fastcall Load(char* self, void*, void* segment)
{
    static bool logged = false;
    FirstCall("Load", logged);
    return Virtual<uint8_t>(self, 0x180, segment);
}

// Slot 90 (0x00742150): every vertex keeps only flags 0x08 and 0x10, then
// slot 97. The flag walker is the brushes' (ours with
// simulators.terrain.brush, through its thunk; the game's otherwise).
uint8_t __fastcall SaveObliterated(char* self, void*, void* segment)
{
    static bool logged = false;
    FirstCall("SaveObliterated", logged);
    This<void>(addr::cSTETerrain_ClearFlagsInRect, self, static_cast<void*>(self + field::kCellRect), 0xFFE7u);
    return Virtual<uint8_t>(self, 0x184, segment);
}

// cIGZSerializable slot 3 (0x00745300), on the part at +8: version 2, the
// view's serializable part as a reference (none without a view), then the
// segment the stream writes into: the stream's interface 0x6999A8FB
// (released at once, used after, as the game), its slot 25, that object's
// slot 3. Slot 97 gets the segment (null when any step gives none) and its
// answer is returned.
uint8_t __fastcall Write(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    char* const self = part - 0x8;
    Virtual<uint8_t>(stream, kUint16, 2u);
    char* const view = At<char*>(self, field::kView);
    Cdecl<uint8_t>(addr::SC4Stream_WriteReference, stream, static_cast<void*>(view ? view + kViewSerializable : nullptr));
    void* persist = nullptr;
    Virtual<uint8_t>(stream, 0x0, kIidPersistStream, &persist);
    void* segment = nullptr;
    if (persist) {
        Virtual<uint32_t>(persist, 0x8);
        if (void* const file = Virtual<void*>(persist, 0x64))
            segment = Virtual<void*>(file, 0xC);
    }
    return Virtual<uint8_t>(self, 0x184, segment);
}

// cIGZSerializable slot 4 (0x00745380): versions up to 2 (false above,
// nothing else read), the view reference into +0xE8, the stream's segment
// (0x0045BA70) to slot 96, whose answer is returned.
uint8_t __fastcall Read(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Read", logged);
    char* const self = part - 0x8;
    // The game reads the version into the stack slot of its stream
    // argument: when the read writes nothing, the pointer's low 16 bits are
    // compared.
    uint16_t version = static_cast<uint16_t>(reinterpret_cast<std::uintptr_t>(stream));
    Virtual<uint8_t>(stream, kUint16, &version);
    if (version > 2)
        return 0;
    Cdecl<uint8_t>(addr::SC4Stream_ReadReference, stream, kIidView, static_cast<void*>(self + field::kView), 0u);
    void* const segment = Cdecl<void*>(addr::SC4Stream_GetSegment, stream);
    return Virtual<uint8_t>(self, 0x180, segment);
}

// --- Installation ------------------------------------------------------------

struct Slot {
    std::uintptr_t vtable;
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(vtable, index, original, fn) {vtable, index, original, reinterpret_cast<void*>(&fn)}

const Slot kSlots[] = {
    OPENSC4_SLOT(addr::cSTETerrain_vftable, 90, addr::cSTETerrain_SaveObliterated, SaveObliterated),
    OPENSC4_SLOT(addr::cSTETerrain_vftable, 95, addr::cSTETerrain_Load, Load),
    OPENSC4_SLOT(addr::cSTETerrain_vftable, 96, addr::cSTETerrain_LoadFromSegment, LoadFromSegment),
    OPENSC4_SLOT(addr::cSTETerrain_vftable, 97, addr::cSTETerrain_SaveToSegment, SaveToSegment),
    OPENSC4_SLOT(addr::cSTETerrain_vftable_8, 3, addr::cSTETerrain_Write, Write),
    OPENSC4_SLOT(addr::cSTETerrain_vftable_8, 4, addr::cSTETerrain_Read, Read),
};

#undef OPENSC4_SLOT

// Checks that every slot still holds the original, then swaps them all.
bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    for (const Slot& slot : kSlots) {
        const auto* const vtable = reinterpret_cast<const std::uintptr_t*>(slot.vtable);
        if (vtable[slot.index] != slot.original) {
            log::Write("terrain save: slot %d of %08X holds %08X, expected %08X; not installing", slot.index,
                       static_cast<unsigned>(slot.vtable), static_cast<unsigned>(vtable[slot.index]),
                       static_cast<unsigned>(slot.original));
            return false;
        }
    }
    for (const Slot& slot : kSlots) {
        auto* const entry = reinterpret_cast<std::uintptr_t*>(slot.vtable) + slot.index;
        DWORD old = 0;
        if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &old))
            return false;
        *entry = reinterpret_cast<std::uintptr_t>(slot.ours);
        VirtualProtect(entry, sizeof(*entry), old, &old);
    }
    log::Write("terrain save: 4 slots of %08X (90, 95, 96, 97) and Write and Read of %08X are ours",
               static_cast<unsigned>(addr::cSTETerrain_vftable), static_cast<unsigned>(addr::cSTETerrain_vftable_8));
    return true;
}

const bool g_registered = registry::Register(
    {"simulators.terrain.save", "simulators/terrain", registry::Kind::Vtable, registry::Phase::PostAppInit,
     addr::cSTETerrain_vftable_8,
     "Our cSTETerrain save and load: Write and Read of the object's record and the header, altitude, flags and "
     "edge records in the city's segment (slots 90, 95, 96, 97 of 0x00AB3D18; slots 3 and 4 of 0x00AB3CF0)",
     false, Install});

} // namespace
} // namespace opensc4::terrain_sim
