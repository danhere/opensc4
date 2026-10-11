// The city save code's serializer and segment streams (docs/save-format.md),
// part 1: 0x005D1520 to 0x005D2740. Replacement, off by default:
//
//   saves.serializer.part1: the segment input and output streams (every
//     slot but the two string ones and the constructors), the small slots
//     of the save file's segment class and of the serializer, the segment's
//     destructor and the forwarding thunks of its three vtables.
//
// Four classes meet in this range.
//
// The segment input stream, cISC4DBSegmentIStream (interface 0x4999A8F5),
// and output stream, cISC4DBSegmentOStream (0x6999A8FB): what a saved
// class's Read and Write get. 0x1C bytes each:
//   +0x00 main vtable (0x00AA22F4 in, 0x00AA2384 out: QueryInterface,
//         AddRef, Release, deleting destructor)
//   +0x04 reference count
//   +0x08 the stream interface (vtable 0x00AA2280 in, 0x00AA2310 out; 29
//         slots). `this` of every stream slot is here, and the offsets
//         below are from here:
//   +0x04 'good' byte: 1 until a call fails, then 0; GetError answers its
//         inverse
//   +0x08 the open record, a cIGZPersistDBSerialRecord (counted)
//   +0x0C the segment, a cISC4DBSegment (counted)
//   +0x10 user data (counted)
// Every typed slot does the same: when the stream is still good it forwards
// to the record's field call, and the stream is good afterwards only if
// that call answered true. Once bad, nothing reaches the record any more.
//
// The save file's segment, cSC4DBSegmentPackedFile (0x1DC bytes):
//   +0x000 cISC4DBSegment (vtable 0x00AA2654, 14 slots)
//   +0x004 the framework's packed file (vtable 0x00AA2598; its 'initialised'
//          byte is at object +0x18, its open flags at +0x20)
//   +0x008 the packed file's cIGZPersistDBSegment (vtable 0x00AA24F0)
//   +0x1CC the serializer, a cISC4COMSerializer (counted)
//   +0x1D0 where progress messages go (counted; slot 0xC takes the message)
//   +0x1D4 last error
//   +0x1D8 float, percentage done
//
// The serializer (main vtable 0x00AA26B4, interface 0x699899EA with vtable
// 0x00AA268C at +8, whose slots get `this` = object + 8): interface +0x20
// is the mode (1 reading, 2 writing), +0x2C the segment (counted).
//
// Not functions: 0x005D18F0, 0x005D2300, 0x005D2420 and 0x005D24D0 are
// jump targets inside ReadVariant and the two Opens. Left the game's:
// 0x005D17C0 (the shared tree iterator step) and four slots folded with
// unrelated classes (0x005D2070, 0x005D25F0, 0x005D2680, 0x005D2690).
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace opensc4::saves {
namespace {

namespace unknown { // cIGZUnknown, by vtable offset
constexpr std::size_t kQueryInterface = 0x0; // (iid, void** out)
constexpr std::size_t kAddRef = 0x4;
constexpr std::size_t kRelease = 0x8;
} // namespace unknown

namespace serializable { // cIGZSerializable, by vtable offset
constexpr std::size_t kWrite = 0xC; // (stream)
constexpr std::size_t kRead = 0x10; // (stream)
} // namespace serializable

namespace record { // cIGZPersistDBSerialRecord, by vtable offset
constexpr std::size_t kAsRecord = 0xC;
constexpr std::size_t kGetUint8 = 0x14;
constexpr std::size_t kGetSint8 = 0x1C;
constexpr std::size_t kGetUint16 = 0x24;
constexpr std::size_t kGetSint16 = 0x2C;
constexpr std::size_t kGetUint32 = 0x34;
constexpr std::size_t kGetSint32 = 0x3C;
constexpr std::size_t kGetUint64 = 0x44;
constexpr std::size_t kGetSint64 = 0x4C;
constexpr std::size_t kGetFloat32 = 0x54;
constexpr std::size_t kGetFloat64 = 0x5C;
constexpr std::size_t kGetString = 0x60;
constexpr std::size_t kGetResKey = 0x64;
constexpr std::size_t kGetVariant = 0x68; // (variant** out)
constexpr std::size_t kGetVoid = 0x6C;    // (data, size)
constexpr std::size_t kSetUint8 = 0x74;
constexpr std::size_t kSetSint8 = 0x7C;
constexpr std::size_t kSetUint16 = 0x84;
constexpr std::size_t kSetSint16 = 0x8C;
constexpr std::size_t kSetUint32 = 0x94;
constexpr std::size_t kSetSint32 = 0x9C;
constexpr std::size_t kSetUint64 = 0xA4;
constexpr std::size_t kSetSint64 = 0xAC;
constexpr std::size_t kSetFloat32 = 0xB4;
constexpr std::size_t kSetFloat64 = 0xBC;
constexpr std::size_t kSetString = 0xC0;
constexpr std::size_t kSetResKey = 0xC4;
constexpr std::size_t kSetVariant = 0xC8;
constexpr std::size_t kSetVoid = 0xCC; // (data, size)
constexpr std::size_t kGetSize = 0xD0;
constexpr std::size_t kSeekAbsolute = 0xDC; // (position)
} // namespace record

namespace dbsegment { // cIGZPersistDBSegment, by vtable offset
constexpr std::size_t kIsOpen = 0x1C;
constexpr std::size_t kClose = 0x20;
constexpr std::size_t kOpenRecord = 0x54;      // (key, record**, access)
constexpr std::size_t kCreateNewRecord = 0x58; // (key, record**)
constexpr std::size_t kCloseRecord = 0x60;     // (record*)
} // namespace dbsegment

namespace sc4segment { // cISC4DBSegment, by vtable offset
constexpr std::size_t kAsDBSegment = 0xC;
constexpr std::size_t kGetSerializer = 0x10;
} // namespace sc4segment

namespace comserializer { // cISC4COMSerializer, by vtable offset
constexpr std::size_t kInit = 0xC;       // (segment, mode)
constexpr std::size_t kShutdown = 0x10;  // 0x005D3500
constexpr std::size_t kLoadAll = 0x18;   // 0x005D38D0
constexpr std::size_t kSaveAll = 0x1C;   // 0x005D3DE0
constexpr std::size_t kWriteObject = 0x20; // (stream, serializable), 0x005D4810
constexpr std::size_t kReadObject = 0x24;  // (stream, out, iid), 0x005D4880
// Fields, from the interface.
constexpr std::size_t kMode = 0x20;
constexpr std::size_t kSegment = 0x2C;
} // namespace comserializer

namespace variant { // cIGZVariant, by vtable offset
constexpr std::size_t kGetType = 0xC;
constexpr std::size_t kCopyFrom = 0x18;    // (variant)
constexpr std::size_t kGetUnknown = 0xF0;  // (iid, void** out)
constexpr std::size_t kSetUnknown = 0xF4;  // (object)
} // namespace variant

namespace message { // cIGZMessage2Standard, by vtable offset
constexpr std::size_t kSetType = 0x14;
constexpr std::size_t kSetData1 = 0x2C;
} // namespace message

namespace stream { // both segment streams, from the stream interface
constexpr std::size_t kGood = 0x4;
constexpr std::size_t kRecord = 0x8;
constexpr std::size_t kSegment = 0xC;
constexpr std::size_t kUserData = 0x10;
// Slots of the interface itself.
constexpr std::size_t kGetSint8 = 0x10;
constexpr std::size_t kUint32 = 0x24;       // GetUint32 in, SetUint32 out
constexpr std::size_t kGetVoid = 0x44;      // (data, size)
constexpr std::size_t kClose = 0x58;
constexpr std::size_t kSerializable = 0x68; // in: (out, iid); out: (serializable)
// The whole object, from its start.
constexpr std::size_t kMainVtable = 0x0;
constexpr std::size_t kInterface = 0x8;
constexpr std::size_t kObjectUserData = 0x18;
constexpr std::size_t kObjectSegment = 0x14;
constexpr std::size_t kObjectRecord = 0x10;
} // namespace stream

namespace segment { // cSC4DBSegmentPackedFile, from the object's start
constexpr std::size_t kPackedFile = 0x4;
constexpr std::size_t kDBSegment = 0x8;
constexpr std::size_t kSerializer = 0x1CC;
constexpr std::size_t kProgressTarget = 0x1D0;
constexpr std::size_t kPercentage = 0x1D8;
constexpr std::size_t kSendMessage = 0xC; // slot of the progress target: (message)
} // namespace segment

constexpr uint32_t kIStreamIid = 0x051B11BD;        // cIGZIStream
constexpr uint32_t kSegmentIStreamIid = 0x4999A8F5; // cISC4DBSegmentIStream
constexpr uint32_t kOStreamIid = 0xC51B11CB;        // cIGZOStream
constexpr uint32_t kSegmentOStreamIid = 0x6999A8FB; // cISC4DBSegmentOStream
constexpr uint32_t kSC4SegmentIid = 0x499899DA;     // cISC4DBSegment
constexpr uint32_t kDBSegmentIid = 0x656B8F0C;      // cIGZPersistDBSegment
constexpr uint32_t kSerializerIid = 0x699899EA;     // cISC4COMSerializer
constexpr uint32_t kSerialRecordIid = 0xC56B8F08;   // cIGZPersistDBSerialRecord
constexpr uint32_t kSerializableIid = 0xE4FDA3D4;   // cIGZSerializable
constexpr uint32_t kReferenceMarker = 0xA9BEE9AC;   // a variant that holds an object reference
constexpr uint32_t kVariantUnknown = 0x10;          // the variant type that holds an object
constexpr uint32_t kProgressMessage = 0xC9E30725;

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

uint8_t QueryInterface(void* object, uint32_t iid, void** out)
{
    return Virtual<uint8_t>(object, unknown::kQueryInterface, iid, out);
}

void AddRef(void* object)
{
    Virtual<uint32_t>(object, unknown::kAddRef);
}

void Release(void* object)
{
    Virtual<uint32_t>(object, unknown::kRelease);
}

// Takes the pointer out of a counted member and releases it.
void Clear(void*& member)
{
    if (void* const old = member) {
        member = nullptr;
        Release(old);
    }
}

// Stores a counted pointer: the new one is counted first, then stored, then
// the old one released. Nothing happens when they are the same.
void Assign(void*& member, void* value)
{
    void* const old = member;
    if (value == old)
        return;
    if (value)
        AddRef(value);
    member = value;
    if (old)
        Release(old);
}

// The stream interface as the original passes it on: the object's interface
// part, or null for a null object.
void* AsStream(char* self)
{
    return reinterpret_cast<std::uintptr_t>(self) != stream::kInterface ? self : nullptr;
}

// What every typed slot of both streams does. Returns 1 or 0 in all of EAX.
template <typename... Args>
uint32_t Forward(char* self, std::size_t slot, Args... args)
{
    if (At<uint8_t>(self, stream::kGood) != 0 &&
        Virtual<uint8_t>(At<void*>(self, stream::kRecord), slot, args...) != 0) {
        At<uint8_t>(self, stream::kGood) = 1;
        return 1;
    }
    At<uint8_t>(self, stream::kGood) = 0;
    return 0;
}

// x87 truncation as the game's ftol (0x009EEF04) does it.
int32_t Truncate(double value)
{
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

// Both streams.

// The game's 0x005D16B0, GetError of both streams (slot 0x48): 1 when a
// call has failed, else 0.
uint32_t __fastcall StreamGetError(char* self, void*)
{
    return At<uint8_t>(self, stream::kGood) == 0 ? 1 : 0;
}

// The game's 0x005D1B80, SetUserData of both streams (slot 0x4C).
void __fastcall StreamSetUserData(char* self, void*, void* data)
{
    Assign(At<void*>(self, stream::kUserData), data);
}

// The game's 0x005D1BC0, Close of both streams (slot 0x58). With a record
// and a segment: the record is closed through the segment; when that worked
// both are let go and the answer is true, else the stream turns bad and the
// answer is false with both kept. Without either: the 'good' byte.
uint8_t __fastcall StreamClose(char* self, void*)
{
    void* const segment = At<void*>(self, stream::kSegment);
    if (!segment || !At<void*>(self, stream::kRecord))
        return At<uint8_t>(self, stream::kGood);
    void* const db = Virtual<void*>(segment, sc4segment::kAsDBSegment);
    void* const dbRecord = Virtual<void*>(At<void*>(self, stream::kRecord), record::kAsRecord);
    if (!Virtual<uint8_t>(db, dbsegment::kCloseRecord, dbRecord)) {
        At<uint8_t>(self, stream::kGood) = 0;
        return 0;
    }
    Clear(At<void*>(self, stream::kRecord));
    Clear(At<void*>(self, stream::kSegment));
    return 1;
}

// The game's 0x005D1C30, IsOpen of both streams (slot 0x5C).
uint32_t __fastcall StreamIsOpen(char* self, void*)
{
    return At<void*>(self, stream::kRecord) && At<void*>(self, stream::kSegment) ? 1 : 0;
}

// What both streams' destructors do after the vtable stores. `self` is the
// whole object here.
void StreamDestroy(char* self)
{
    if (void* const data = At<void*>(self, stream::kObjectUserData))
        Release(data);
    if (void* const segment = At<void*>(self, stream::kObjectSegment))
        Release(segment);
    if (void* const serial = At<void*>(self, stream::kObjectRecord))
        Release(serial);
    This<void>(addr::cRZBaseUnknown_Destruct, self);
}

// The input stream.

// The game's 0x005D1520, QueryInterface of the input stream object.
uint8_t __fastcall IStreamQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    *out = nullptr;
    if (iid == kIStreamIid || iid == kSegmentIStreamIid) {
        void* const found = self ? self + stream::kInterface : nullptr;
        *out = found;
        if (found) {
            AddRef(self);
            return 1;
        }
    }
    return This<uint8_t>(addr::cRZBaseUnknown_QueryInterface1, self, iid, out);
}

// The game's 0x005D1800, slot 0 of the stream interface.
uint8_t __fastcall IStreamQueryInterfaceThunk(char* self, void*, uint32_t iid, void** out)
{
    return IStreamQueryInterface(self - stream::kInterface, nullptr, iid, out);
}

// The game's 0x005D1570, Skip (slot 0xC): reads `count` bytes into a buffer
// on the stack and drops them, 0x400 at a time through GetVoid and the rest
// byte by byte through GetSint8, both called through the vtable. Answers
// the 'good' byte. A count with the top bit set takes the byte-by-byte loop
// for the whole count (the size test is signed).
uint8_t __fastcall IStreamSkip(char* self, void*, uint32_t count)
{
    uint8_t buffer[0x400];
    uint32_t rest = count;
    if (static_cast<int32_t>(count) >= 0x400) {
        uint32_t blocks = count >> 10;
        rest = count - (blocks << 10);
        do {
            Virtual<uint8_t>(self, stream::kGetVoid, static_cast<void*>(buffer), uint32_t{0x400});
        } while (--blocks != 0);
    }
    if (rest != 0) {
        do {
            Virtual<uint8_t>(self, stream::kGetSint8, static_cast<void*>(buffer));
        } while (--rest != 0);
    }
    return At<uint8_t>(self, stream::kGood);
}

#define OPENSC4_IN(name, slot) \
    uint32_t __fastcall name(char* self, void*, void* out) \
    { \
        return Forward(self, slot, out); \
    }

OPENSC4_IN(IStreamGetSint8, record::kGetSint8)     // 0x005D1930, slot 0x10
OPENSC4_IN(IStreamGetUint8, record::kGetUint8)     // 0x005D1960, slot 0x14
OPENSC4_IN(IStreamGetSint16, record::kGetSint16)   // 0x005D1990, slot 0x18
OPENSC4_IN(IStreamGetUint16, record::kGetUint16)   // 0x005D19C0, slot 0x1C
OPENSC4_IN(IStreamGetSint32, record::kGetSint32)   // 0x005D19F0, slot 0x20
OPENSC4_IN(IStreamGetUint32, record::kGetUint32)   // 0x005D1A20, slot 0x24
OPENSC4_IN(IStreamGetSint64, record::kGetSint64)   // 0x005D1A50, slot 0x28
OPENSC4_IN(IStreamGetUint64, record::kGetUint64)   // 0x005D1A80, slot 0x2C
OPENSC4_IN(IStreamGetFloat32, record::kGetFloat32) // 0x005D1AB0, slot 0x30
OPENSC4_IN(IStreamGetFloat64, record::kGetFloat64) // 0x005D1AE0, slot 0x34
OPENSC4_IN(IStreamGetString, record::kGetString)   // 0x005D1B10, slot 0x3C
OPENSC4_IN(IStreamReadResKey, record::kGetResKey)  // 0x005D1860, slot 0x6C

#undef OPENSC4_IN

// The game's 0x005D15E0, GetGZSerializable (slot 0x40): the object's own
// Read on this stream.
uint32_t __fastcall IStreamGetSerializable(char* self, void*, void* object)
{
    if (At<uint8_t>(self, stream::kGood) != 0 &&
        Virtual<uint8_t>(object, serializable::kRead, AsStream(self)) != 0) {
        At<uint8_t>(self, stream::kGood) = 1;
        return 1;
    }
    At<uint8_t>(self, stream::kGood) = 0;
    return 0;
}

// The game's 0x005D1B40, GetVoid (slot 0x44).
uint32_t __fastcall IStreamGetVoid(char* self, void*, void* data, uint32_t size)
{
    return Forward(self, record::kGetVoid, data, size);
}

// The game's 0x005D22F0, Open (slot 0x54; its tail is listed as 0x005D2300):
// closes what is open (and stops, answering false, when that fails), takes
// the segment, opens the record `key` for reading and asks it for its
// serial interface. A record without one is closed again. Answers whether a
// record is held, which is the 'good' byte too. The segment stays held when
// the record did not open.
uint8_t __fastcall IStreamOpen(char* self, void*, void* segmentIn, void* key)
{
    const uint8_t closed = Virtual<uint8_t>(self, stream::kClose);
    if (!closed)
        return closed;
    if (segmentIn) {
        Assign(At<void*>(self, stream::kSegment), segmentIn);
        void* const db = Virtual<void*>(At<void*>(self, stream::kSegment), sc4segment::kAsDBSegment);
        void* opened = nullptr;
        if (Virtual<uint8_t>(db, dbsegment::kOpenRecord, key, &opened, uint32_t{1})) {
            void* const found = opened;
            Clear(At<void*>(self, stream::kRecord));
            if (!QueryInterface(found, kSerialRecordIid, &At<void*>(self, stream::kRecord)))
                Virtual<uint8_t>(db, dbsegment::kCloseRecord, opened);
        }
        if (opened)
            Release(opened);
    }
    const uint8_t good = At<void*>(self, stream::kRecord) != nullptr;
    At<uint8_t>(self, stream::kGood) = good;
    return good;
}

// The game's 0x005D1810, ReadGZSerializable (slot 0x68): the serializer
// reads a reference and answers the object's interface `iid` in `out`. The
// serializer is asked for before the 'good' byte is looked at.
uint32_t __fastcall IStreamReadSerializable(char* self, void*, void** out, uint32_t iid)
{
    void* const serializer = Virtual<void*>(At<void*>(self, stream::kSegment), sc4segment::kGetSerializer);
    if (At<uint8_t>(self, stream::kGood) != 0 &&
        Virtual<uint8_t>(serializer, comserializer::kReadObject, AsStream(self), out, iid) != 0) {
        At<uint8_t>(self, stream::kGood) = 1;
        return 1;
    }
    At<uint8_t>(self, stream::kGood) = 0;
    return 0;
}

// The game's 0x005D1890, ReadVariant (slot 0x70; its second half is listed
// as 0x005D18F0). A dword comes first: the marker 0xA9BEE9AC means an object
// reference follows, which is read and put into the variant (a null
// reference leaves the variant alone and still answers true); anything else
// means the record's own variant field follows, which is read and copied
// into the variant. The dword is read whatever the stream's state, and its
// result is not looked at.
uint8_t __fastcall IStreamReadVariant(char* self, void*, void* target)
{
    uint32_t marker = 0;
    Virtual<uint8_t>(self, stream::kUint32, &marker);
    void* object = nullptr;
    if (marker == kReferenceMarker) {
        if (Virtual<uint8_t>(self, stream::kSerializable, &object, uint32_t{0})) {
            if (object) {
                Virtual<void>(target, variant::kSetUnknown, object);
                Release(object);
            }
            return 1;
        }
    } else if (Virtual<uint8_t>(At<void*>(self, stream::kRecord), record::kGetVariant, &object)) {
        Virtual<uint8_t>(target, variant::kCopyFrom, object);
        Release(object);
        return 1;
    }
    At<uint8_t>(self, stream::kGood) = 0;
    return 0;
}

// The game's 0x005D2290, the input stream's deleting destructor.
void* __fastcall IStreamDelete(char* self, void*, uint32_t flags)
{
    At<std::uintptr_t>(self, stream::kMainVtable) = addr::cSC4DBSegmentIStream_Vtable;
    At<std::uintptr_t>(self, stream::kInterface) = addr::cSC4DBSegmentIStream_StreamVtable;
    StreamDestroy(self);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// The output stream.

// The game's 0x005D1620, QueryInterface of the output stream object.
uint8_t __fastcall OStreamQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    *out = nullptr;
    if (iid == kSegmentOStreamIid || iid == kOStreamIid) {
        void* const found = self ? self + stream::kInterface : nullptr;
        *out = found;
        if (found) {
            AddRef(self);
            return 1;
        }
    }
    return This<uint8_t>(addr::cRZBaseUnknown_QueryInterface1, self, iid, out);
}

// The game's 0x005D1BB0, slot 0 of the stream interface.
uint8_t __fastcall OStreamQueryInterfaceThunk(char* self, void*, uint32_t iid, void** out)
{
    return OStreamQueryInterface(self - stream::kInterface, nullptr, iid, out);
}

#define OPENSC4_OUT(name, A, slot) \
    uint32_t __fastcall name(char* self, void*, A value) \
    { \
        return Forward(self, slot, value); \
    }

OPENSC4_OUT(OStreamSetSint8, uint32_t, record::kSetSint8)     // 0x005D1D80, slot 0x10
OPENSC4_OUT(OStreamSetSint16, uint32_t, record::kSetSint16)   // 0x005D1DF0, slot 0x18
OPENSC4_OUT(OStreamSetUint16, uint32_t, record::kSetUint16)   // 0x005D1E30, slot 0x1C
OPENSC4_OUT(OStreamSetSint32, uint32_t, record::kSetSint32)   // 0x005D1E70, slot 0x20
OPENSC4_OUT(OStreamSetUint32, uint32_t, record::kSetUint32)   // 0x005D1EB0, slot 0x24
OPENSC4_OUT(OStreamSetSint64, uint64_t, record::kSetSint64)   // 0x005D1EF0, slot 0x28
OPENSC4_OUT(OStreamSetUint64, uint64_t, record::kSetUint64)   // 0x005D1F30, slot 0x2C
OPENSC4_OUT(OStreamSetFloat32, uint32_t, record::kSetFloat32) // 0x005D1F70, slot 0x30 (the float's bits)
OPENSC4_OUT(OStreamSetFloat64, double, record::kSetFloat64)   // 0x005D1FB0, slot 0x34
OPENSC4_OUT(OStreamSetString, void*, record::kSetString)      // 0x005D1FF0, slot 0x3C
OPENSC4_OUT(OStreamWriteResKey, void*, record::kSetResKey)    // 0x005D1CA0, slot 0x6C

#undef OPENSC4_OUT

// The game's 0x005D1DB0, SetUint8 (slot 0x14). The byte is written and the
// 'good' byte kept like everywhere else, but the answer is always false:
// the original clears AL after setting the state.
uint32_t __fastcall OStreamSetUint8(char* self, void*, uint32_t value)
{
    Forward(self, record::kSetUint8, value);
    return 0;
}

// The game's 0x005D1670, SetGZSerializable (slot 0x40): the object's own
// Write on this stream.
uint32_t __fastcall OStreamSetSerializable(char* self, void*, void* object)
{
    if (At<uint8_t>(self, stream::kGood) != 0 &&
        Virtual<uint8_t>(object, serializable::kWrite, AsStream(self)) != 0) {
        At<uint8_t>(self, stream::kGood) = 1;
        return 1;
    }
    At<uint8_t>(self, stream::kGood) = 0;
    return 0;
}

// The game's 0x005D2030, SetVoid (slot 0x44).
uint32_t __fastcall OStreamSetVoid(char* self, void*, void* data, uint32_t size)
{
    return Forward(self, record::kSetVoid, data, size);
}

// The game's 0x005D2410, Open (slot 0x54; its tail is listed as 0x005D2420
// and 0x005D24D0): closes what is open (and stops, answering false, when
// that fails), takes the segment and opens the record `key` for writing: an
// existing record first, a new one when there is none. The record is never
// truncated: `fromStart` true seeks to 0, false to the record's size
// (append). With no record at all, or one without the serial interface
// (which is closed again), the answer is false with the segment kept and
// the 'good' byte untouched. Otherwise the answer is whether a record is
// held, which becomes the 'good' byte; when none is, the segment is let go.
uint8_t __fastcall OStreamOpen(char* self, void*, void* segmentIn, void* key, uint32_t fromStart)
{
    const uint8_t closed = Virtual<uint8_t>(self, stream::kClose);
    if (!closed)
        return closed;
    if (segmentIn) {
        Assign(At<void*>(self, stream::kSegment), segmentIn);
        void* const db = Virtual<void*>(At<void*>(self, stream::kSegment), sc4segment::kAsDBSegment);
        void* opened = nullptr;
        if (!Virtual<uint8_t>(db, dbsegment::kOpenRecord, key, &opened, uint32_t{2})) {
            Clear(opened);
            Virtual<uint8_t>(db, dbsegment::kCreateNewRecord, key, &opened);
        }
        void* const found = opened;
        if (!found)
            return 0;
        void*& serial = At<void*>(self, stream::kRecord);
        Clear(serial);
        if (!QueryInterface(found, kSerialRecordIid, &serial)) {
            if (opened) {
                Virtual<uint8_t>(db, dbsegment::kCloseRecord, opened);
                if (opened)
                    Release(opened);
            }
            return 0;
        }
        const uint32_t position = (fromStart & 0xFF) != 0 ? 0 : Virtual<uint32_t>(serial, record::kGetSize);
        Virtual<uint8_t>(serial, record::kSeekAbsolute, position);
        if (opened)
            Release(opened);
    }
    const uint8_t good = At<void*>(self, stream::kRecord) != nullptr;
    At<uint8_t>(self, stream::kGood) = good;
    if (!good)
        Clear(At<void*>(self, stream::kSegment));
    return At<uint8_t>(self, stream::kGood);
}

// The game's 0x005D1C50, WriteGZSerializable (slot 0x68): the serializer
// writes the reference. The serializer is only asked for while the stream
// is good.
uint32_t __fastcall OStreamWriteSerializable(char* self, void*, void* object)
{
    if (At<uint8_t>(self, stream::kGood) != 0) {
        void* const streamPart = AsStream(self);
        void* const serializer = Virtual<void*>(At<void*>(self, stream::kSegment), sc4segment::kGetSerializer);
        if (Virtual<uint8_t>(serializer, comserializer::kWriteObject, streamPart, object) != 0) {
            At<uint8_t>(self, stream::kGood) = 1;
            return 1;
        }
    }
    At<uint8_t>(self, stream::kGood) = 0;
    return 0;
}

// The game's 0x005D1CE0, WriteVariant (slot 0x70). A variant that holds an
// object (type 0x10) is written as the marker 0xA9BEE9AC and a reference to
// the object's serialisable part (a null reference when it has none); any
// other as a zero dword and the record's variant field. The type is
// compared as the whole of EAX. The out pointer for the object starts as
// the variant's address, as the original reuses its argument slot.
uint8_t __fastcall OStreamWriteVariant(char* self, void*, void* source)
{
    const uint32_t type = Virtual<uint32_t>(source, variant::kGetType);
    uint8_t result = type == kVariantUnknown ? Virtual<uint8_t>(self, stream::kUint32, kReferenceMarker)
                                             : Virtual<uint8_t>(self, stream::kUint32, uint32_t{0});
    if (result) {
        if (type == kVariantUnknown) {
            void* object = source;
            if (Virtual<uint8_t>(source, variant::kGetUnknown, kSerializableIid, &object)) {
                result = Virtual<uint8_t>(self, stream::kSerializable, object);
                Release(object);
            } else {
                result = Virtual<uint8_t>(self, stream::kSerializable, static_cast<void*>(nullptr));
            }
        } else {
            result = Virtual<uint8_t>(At<void*>(self, stream::kRecord), record::kSetVariant, source);
        }
    }
    if (!result)
        At<uint8_t>(self, stream::kGood) = 0;
    return result;
}

// The game's 0x005D23B0, the output stream's deleting destructor.
void* __fastcall OStreamDelete(char* self, void*, uint32_t flags)
{
    At<std::uintptr_t>(self, stream::kMainVtable) = addr::cSC4DBSegmentOStream_Vtable;
    At<std::uintptr_t>(self, stream::kInterface) = addr::cSC4DBSegmentOStream_StreamVtable;
    StreamDestroy(self);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// The segment. `self` is the object's start unless a comment says otherwise.

// The game's 0x005D16D0, QueryInterface (slot 0 of the main vtable).
uint8_t __fastcall SegmentQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    if (iid == kSC4SegmentIid) {
        *out = self;
        AddRef(self);
        return 1;
    }
    if (iid == kDBSegmentIid) {
        *out = Virtual<void*>(self, sc4segment::kAsDBSegment);
        AddRef(self);
        return 1;
    }
    return This<uint8_t>(addr::cGZDBSegmentPackedFile_QueryInterface, self + segment::kPackedFile, iid, out);
}

// The game's 0x005D1730, AddRef.
uint32_t __fastcall SegmentAddRef(char* self, void*)
{
    return This<uint32_t>(addr::cGZDBSegmentPackedFile_AddRef, self + segment::kPackedFile);
}

// The game's 0x005D1740, Release.
uint32_t __fastcall SegmentRelease(char* self, void*)
{
    return This<uint32_t>(addr::cGZDBSegmentPackedFile_Release, self + segment::kPackedFile);
}

// The game's 0x005D2600 and 0x005D2640: QueryInterface from the packed file
// part (+4) and from the cIGZPersistDBSegment part (+8).
uint8_t __fastcall SegmentQueryInterfaceFrom4(char* self, void*, uint32_t iid, void** out)
{
    return SegmentQueryInterface(self - 0x4, nullptr, iid, out);
}

uint8_t __fastcall SegmentQueryInterfaceFrom8(char* self, void*, uint32_t iid, void** out)
{
    return SegmentQueryInterface(self - 0x8, nullptr, iid, out);
}

// The game's 0x005D2610 and 0x005D2650: AddRef from +4 and +8.
uint32_t __fastcall SegmentAddRefFrom4(char* self, void*)
{
    return SegmentAddRef(self - 0x4, nullptr);
}

uint32_t __fastcall SegmentAddRefFrom8(char* self, void*)
{
    return SegmentAddRef(self - 0x8, nullptr);
}

// The game's 0x005D2620 and 0x005D2660: Release from +4 and +8.
uint32_t __fastcall SegmentReleaseFrom4(char* self, void*)
{
    return SegmentRelease(self - 0x4, nullptr);
}

uint32_t __fastcall SegmentReleaseFrom8(char* self, void*)
{
    return SegmentRelease(self - 0x8, nullptr);
}

// The game's 0x005D2630, AsIGZPersistDBSegment of the packed file part
// (`self` is +4): a jump to the shared 'this + 8 or null' getter with the
// object's start.
void* __fastcall SegmentAsDBSegmentFrom4(char* self, void*)
{
    return This<void*>(addr::cSC4Occupant_AsPropertyHolder, self - 0x4);
}

// The game's 0x005D1750, Init of the packed file part (`self` is +4): a
// bare jump to the framework's 'mark initialised, answer true'.
uint8_t __fastcall SegmentInit(char* self, void*)
{
    return This<uint8_t>(addr::cGZDBSegmentPackedFile_Init, self);
}

// The game's 0x005D2670, Init from the cIGZPersistDBSegment part (+8).
uint8_t __fastcall SegmentInitFrom8(char* self, void*)
{
    return SegmentInit(self - 0x4, nullptr);
}

// The game's 0x005D1760, IsOpen of the cIGZPersistDBSegment part (`self`
// is +8): a bare jump to the framework's. 1 or 0 in all of EAX.
uint32_t __fastcall SegmentIsOpen(char* self, void*)
{
    return This<uint32_t>(addr::cGZDBSegmentPackedFile_IsOpen, self);
}

// The game's 0x005D16C0, CloseIStream and CloseOStream (slots 0x18 and 0x24
// of the main vtable): the stream's own Close; the segment is not touched.
uint8_t __fastcall SegmentCloseStream(char*, void*, void* opened)
{
    return Virtual<uint8_t>(opened, stream::kClose);
}

// The game's 0x005D2080, LoadClassObjects (slot 0x1C): the serializer's,
// false without a serializer.
uint8_t __fastcall SegmentLoadClassObjects(char* self, void*)
{
    void* const serializer = At<void*>(self, segment::kSerializer);
    return serializer ? Virtual<uint8_t>(serializer, comserializer::kLoadAll) : 0;
}

// The game's 0x005D20A0, SaveClassObjects (slot 0x28).
uint8_t __fastcall SegmentSaveClassObjects(char* self, void*)
{
    void* const serializer = At<void*>(self, segment::kSerializer);
    return serializer ? Virtual<uint8_t>(serializer, comserializer::kSaveAll) : 0;
}

// The game's 0x005D1770, GetPercentageCompletion (slot 0x30).
double __fastcall SegmentGetPercentage(char* self, void*)
{
    return At<float>(self, segment::kPercentage);
}

// The game's 0x005D21C0, SetPercentageCompletion (slot 0x34). The value is
// held to 0 to 100 and only ever raises the stored percentage. When it
// reaches a new even whole number (the truncated value with its lowest bit
// dropped, compared with the same of the old percentage), message
// 0xC9E30725 goes to the progress target with the stored percentage,
// truncated, as its first data.
void __fastcall SegmentSetPercentage(char* self, void*, float value)
{
    double clamped = value;
    if (clamped < 0.0)
        clamped = 0.0;
    else if (clamped > 100.0)
        clamped = 100.0;
    const float old = At<float>(self, segment::kPercentage);
    const int32_t oldStep = Truncate(old) / 2 * 2;
    const int32_t newStep = Truncate(clamped) / 2 * 2;
    if (clamped > static_cast<double>(old))
        At<float>(self, segment::kPercentage) = static_cast<float>(clamped);
    if (newStep <= oldStep)
        return;
    void* const memory = Cdecl<void*>(addr::RZ_operator_new, uint32_t{0x2C});
    void* const made = memory ? This<void*>(addr::cGZMessage_cGZMessage, memory) : nullptr;
    AddRef(made);
    Virtual<void>(made, message::kSetType, kProgressMessage);
    Virtual<void>(made, message::kSetData1, Truncate(At<float>(self, segment::kPercentage)));
    Virtual<uint8_t>(At<void*>(self, segment::kProgressTarget), segment::kSendMessage, made);
    Release(made);
}

// The game's 0x005D2110, Open of the cIGZPersistDBSegment part (`self` is
// +8; slot 0x18): zeroes the percentage, opens the packed file and sets the
// serializer up on this segment, for reading (mode 1) when the first
// argument's low byte is set, else for writing (mode 2). When either fails
// the segment is closed again if it is open, and the answer is false.
uint8_t __fastcall SegmentOpen(char* self, void*, uint32_t openRead, uint32_t openWrite)
{
    char* const object = self - segment::kDBSegment;
    At<uint32_t>(object, segment::kPercentage) = 0;
    if (This<uint8_t>(addr::cGZDBSegmentPackedFile_Open, self, openRead, openWrite)) {
        const uint32_t mode = (openRead & 0xFF) == 0 ? 2 : 1;
        if (Virtual<uint8_t>(At<void*>(object, segment::kSerializer), comserializer::kInit,
                             static_cast<void*>(object), mode))
            return 1;
    }
    if (Virtual<uint8_t>(self, dbsegment::kIsOpen))
        Virtual<uint8_t>(self, dbsegment::kClose);
    return 0;
}

// The game's 0x005D2170, Close of the cIGZPersistDBSegment part (`self` is
// +8; slot 0x20). An open segment has the serializer save every class's
// objects (its slot 0x1C) and shut down (slot 0x10), then the packed file
// closes; the answer is true only when all three were. A segment that is
// not open answers true.
uint8_t __fastcall SegmentClose(char* self, void*)
{
    char* const object = self - segment::kDBSegment;
    uint8_t result = 1;
    if (!Virtual<uint8_t>(self, dbsegment::kIsOpen))
        return result;
    if (!Virtual<uint8_t>(At<void*>(object, segment::kSerializer), comserializer::kSaveAll))
        result = 0;
    if (!Virtual<uint8_t>(At<void*>(object, segment::kSerializer), comserializer::kShutdown))
        result = 0;
    if (!This<uint8_t>(addr::cGZDBSegmentPackedFile_Close, self))
        return 0;
    return result;
}

// The game's 0x005D20C0, Shutdown of the packed file part (`self` is +4):
// closes the segment through its vtable, lets the serializer and the
// progress target go, then the framework's Shutdown.
uint8_t __fastcall SegmentShutdown(char* self, void*)
{
    char* const object = self - segment::kPackedFile;
    Virtual<uint8_t>(self + 0x4, dbsegment::kClose);
    Clear(At<void*>(object, segment::kSerializer));
    Clear(At<void*>(object, segment::kProgressTarget));
    return This<uint8_t>(addr::cGZDBSegmentPackedFile_Shutdown, self);
}

// The game's 0x005D26B0, Shutdown from the cIGZPersistDBSegment part (+8).
uint8_t __fastcall SegmentShutdownFrom8(char* self, void*)
{
    return SegmentShutdown(self - 0x4, nullptr);
}

// The game's 0x005D2550, the segment's destructor. An open segment is
// closed as Close does, with the results dropped; the two counted members
// are let go, serializer first; then the packed file's destructor.
void __fastcall SegmentDestruct(char* self, void*)
{
    char* const db = self + segment::kDBSegment;
    At<std::uintptr_t>(self, 0x0) = addr::cSC4DBSegmentPackedFile_Vtable;
    At<std::uintptr_t>(self, segment::kPackedFile) = addr::cSC4DBSegmentPackedFile_PackedFileVtable;
    At<std::uintptr_t>(self, segment::kDBSegment) = addr::cSC4DBSegmentPackedFile_SegmentVtable;
    if (Virtual<uint8_t>(db, dbsegment::kIsOpen)) {
        Virtual<uint8_t>(At<void*>(self, segment::kSerializer), comserializer::kSaveAll);
        Virtual<uint8_t>(At<void*>(self, segment::kSerializer), comserializer::kShutdown);
        This<uint8_t>(addr::cGZDBSegmentPackedFile_Close, db);
    }
    Clear(At<void*>(self, segment::kSerializer));
    Clear(At<void*>(self, segment::kProgressTarget));
    // The members' own destructors; both are null by now.
    if (void* const target = At<void*>(self, segment::kProgressTarget))
        Release(target);
    if (void* const serializer = At<void*>(self, segment::kSerializer))
        Release(serializer);
    This<void>(addr::cGZDBSegmentPackedFile_Destruct, self + segment::kPackedFile);
}

// The game's 0x005D2720, the segment's deleting destructor.
void* __fastcall SegmentDelete(char* self, void*, uint32_t flags)
{
    SegmentDestruct(self, nullptr);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// The game's 0x005D26A0, the deleting destructor from the
// cIGZPersistDBSegment part (+8).
void* __fastcall SegmentDeleteFrom8(char* self, void*, uint32_t flags)
{
    return SegmentDelete(self - 0x8, nullptr, flags);
}

// The serializer.

// The game's 0x005D1780, QueryInterface of the serializer object.
uint8_t __fastcall SerializerQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    if (iid != kSerializerIid)
        return This<uint8_t>(addr::cRZBaseUnknown_QueryInterface1, self, iid, out);
    *out = self ? self + 0x8 : nullptr;
    AddRef(self);
    return 1;
}

// The game's 0x005D26C0, slot 0xC of the serializer's interface (`self` is
// the interface, object + 8): attaches the serializer to a segment in mode
// 1 (reading) or 2 (writing). One that is attached already is shut down
// first (slot 0x10). Refused, with nothing stored, without a segment, for a
// segment that is not open and for mode 0.
uint8_t __fastcall SerializerInit(char* self, void*, void* segmentIn, uint32_t mode)
{
    if (At<void*>(self, comserializer::kSegment) && At<uint32_t>(self, comserializer::kMode) != 0)
        Virtual<uint8_t>(self, comserializer::kShutdown);
    if (!segmentIn)
        return 0;
    void* const db = Virtual<void*>(segmentIn, sc4segment::kAsDBSegment);
    if (!Virtual<uint8_t>(db, dbsegment::kIsOpen))
        return 0;
    if (mode == 0)
        return 0;
    This<void*>(addr::SC4RefPtr_Assign, self + comserializer::kSegment, segmentIn);
    At<uint32_t>(self, comserializer::kMode) = mode;
    return 1;
}

bool InstallSaveSerializer1(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[72] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4Save_F5D1520, reinterpret_cast<void*>(&IStreamQueryInterface)},
        {addr::SC4Save_F5D1570, reinterpret_cast<void*>(&IStreamSkip)},
        {addr::SC4Save_F5D15E0, reinterpret_cast<void*>(&IStreamGetSerializable)},
        {addr::SC4Save_F5D1620, reinterpret_cast<void*>(&OStreamQueryInterface)},
        {addr::SC4Save_F5D1670, reinterpret_cast<void*>(&OStreamSetSerializable)},
        {addr::SC4Save_F5D16B0, reinterpret_cast<void*>(&StreamGetError)},
        {addr::SC4Save_F5D16C0, reinterpret_cast<void*>(&SegmentCloseStream)},
        {addr::SC4Save_F5D16D0, reinterpret_cast<void*>(&SegmentQueryInterface)},
        {addr::SC4Save_F5D1730, reinterpret_cast<void*>(&SegmentAddRef)},
        {addr::SC4Save_F5D1740, reinterpret_cast<void*>(&SegmentRelease)},
        {addr::SC4Save_F5D1750, reinterpret_cast<void*>(&SegmentInit)},
        {addr::SC4Save_F5D1760, reinterpret_cast<void*>(&SegmentIsOpen)},
        {addr::SC4Save_F5D1770, reinterpret_cast<void*>(&SegmentGetPercentage)},
        {addr::SC4Save_F5D1780, reinterpret_cast<void*>(&SerializerQueryInterface)},
        {addr::SC4Save_F5D1800, reinterpret_cast<void*>(&IStreamQueryInterfaceThunk)},
        {addr::SC4Save_F5D1810, reinterpret_cast<void*>(&IStreamReadSerializable)},
        {addr::SC4Save_F5D1860, reinterpret_cast<void*>(&IStreamReadResKey)},
        {addr::SC4Save_F5D1890, reinterpret_cast<void*>(&IStreamReadVariant)},
        {addr::SC4Save_F5D1930, reinterpret_cast<void*>(&IStreamGetSint8)},
        {addr::SC4Save_F5D1960, reinterpret_cast<void*>(&IStreamGetUint8)},
        {addr::SC4Save_F5D1990, reinterpret_cast<void*>(&IStreamGetSint16)},
        {addr::SC4Save_F5D19C0, reinterpret_cast<void*>(&IStreamGetUint16)},
        {addr::SC4Save_F5D19F0, reinterpret_cast<void*>(&IStreamGetSint32)},
        {addr::SC4Save_F5D1A20, reinterpret_cast<void*>(&IStreamGetUint32)},
        {addr::SC4Save_F5D1A50, reinterpret_cast<void*>(&IStreamGetSint64)},
        {addr::SC4Save_F5D1A80, reinterpret_cast<void*>(&IStreamGetUint64)},
        {addr::SC4Save_F5D1AB0, reinterpret_cast<void*>(&IStreamGetFloat32)},
        {addr::SC4Save_F5D1AE0, reinterpret_cast<void*>(&IStreamGetFloat64)},
        {addr::SC4Save_F5D1B10, reinterpret_cast<void*>(&IStreamGetString)},
        {addr::SC4Save_F5D1B40, reinterpret_cast<void*>(&IStreamGetVoid)},
        {addr::SC4Save_F5D1B80, reinterpret_cast<void*>(&StreamSetUserData)},
        {addr::SC4Save_F5D1BB0, reinterpret_cast<void*>(&OStreamQueryInterfaceThunk)},
        {addr::SC4Save_F5D1BC0, reinterpret_cast<void*>(&StreamClose)},
        {addr::SC4Save_F5D1C30, reinterpret_cast<void*>(&StreamIsOpen)},
        {addr::SC4Save_F5D1C50, reinterpret_cast<void*>(&OStreamWriteSerializable)},
        {addr::SC4Save_F5D1CA0, reinterpret_cast<void*>(&OStreamWriteResKey)},
        {addr::SC4Save_F5D1CE0, reinterpret_cast<void*>(&OStreamWriteVariant)},
        {addr::SC4Save_F5D1D80, reinterpret_cast<void*>(&OStreamSetSint8)},
        {addr::SC4Save_F5D1DB0, reinterpret_cast<void*>(&OStreamSetUint8)},
        {addr::SC4Save_F5D1DF0, reinterpret_cast<void*>(&OStreamSetSint16)},
        {addr::SC4Save_F5D1E30, reinterpret_cast<void*>(&OStreamSetUint16)},
        {addr::SC4Save_F5D1E70, reinterpret_cast<void*>(&OStreamSetSint32)},
        {addr::SC4Save_F5D1EB0, reinterpret_cast<void*>(&OStreamSetUint32)},
        {addr::SC4Save_F5D1EF0, reinterpret_cast<void*>(&OStreamSetSint64)},
        {addr::SC4Save_F5D1F30, reinterpret_cast<void*>(&OStreamSetUint64)},
        {addr::SC4Save_F5D1F70, reinterpret_cast<void*>(&OStreamSetFloat32)},
        {addr::SC4Save_F5D1FB0, reinterpret_cast<void*>(&OStreamSetFloat64)},
        {addr::SC4Save_F5D1FF0, reinterpret_cast<void*>(&OStreamSetString)},
        {addr::SC4Save_F5D2030, reinterpret_cast<void*>(&OStreamSetVoid)},
        {addr::SC4Save_F5D2080, reinterpret_cast<void*>(&SegmentLoadClassObjects)},
        {addr::SC4Save_F5D20A0, reinterpret_cast<void*>(&SegmentSaveClassObjects)},
        {addr::SC4Save_F5D20C0, reinterpret_cast<void*>(&SegmentShutdown)},
        {addr::SC4Save_F5D2110, reinterpret_cast<void*>(&SegmentOpen)},
        {addr::SC4Save_F5D2170, reinterpret_cast<void*>(&SegmentClose)},
        {addr::SC4Save_F5D21C0, reinterpret_cast<void*>(&SegmentSetPercentage)},
        {addr::SC4Save_F5D2290, reinterpret_cast<void*>(&IStreamDelete)},
        {addr::SC4Save_F5D22F0, reinterpret_cast<void*>(&IStreamOpen)},
        {addr::SC4Save_F5D23B0, reinterpret_cast<void*>(&OStreamDelete)},
        {addr::cSC4COMSerializer_StreamOpen, reinterpret_cast<void*>(&OStreamOpen)},
        {addr::SC4Save_F5D2550, reinterpret_cast<void*>(&SegmentDestruct)},
        {addr::SC4Save_F5D2600, reinterpret_cast<void*>(&SegmentQueryInterfaceFrom4)},
        {addr::SC4Save_F5D2610, reinterpret_cast<void*>(&SegmentAddRefFrom4)},
        {addr::SC4Save_F5D2620, reinterpret_cast<void*>(&SegmentReleaseFrom4)},
        {addr::SC4Save_F5D2630, reinterpret_cast<void*>(&SegmentAsDBSegmentFrom4)},
        {addr::SC4Save_F5D2640, reinterpret_cast<void*>(&SegmentQueryInterfaceFrom8)},
        {addr::SC4Save_F5D2650, reinterpret_cast<void*>(&SegmentAddRefFrom8)},
        {addr::SC4Save_F5D2660, reinterpret_cast<void*>(&SegmentReleaseFrom8)},
        {addr::SC4Save_F5D2670, reinterpret_cast<void*>(&SegmentInitFrom8)},
        {addr::SC4Save_F5D26A0, reinterpret_cast<void*>(&SegmentDeleteFrom8)},
        {addr::SC4Save_F5D26B0, reinterpret_cast<void*>(&SegmentShutdownFrom8)},
        {addr::SC4Save_F5D26C0, reinterpret_cast<void*>(&SerializerInit)},
        {addr::SC4Save_F5D2720, reinterpret_cast<void*>(&SegmentDelete)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    static_assert(count <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < count; ++i) {
        // diagnostics.prop_pox watches this function with a detour of its
        // own, and one address takes one detour: with the diagnostic on (or
        // its jump already in place), this one function stays the game's.
        if (hooks[i].first == addr::cSC4COMSerializer_StreamOpen
            && (registry::ModeOf("diagnostics.prop_pox") != registry::Mode::Original || *reinterpret_cast<const uint8_t*>(hooks[i].first) == 0xE9)) {
            log::Write("saves: 0x%08X left to the game while diagnostics.prop_pox is on", static_cast<unsigned>(hooks[i].first));
            continue;
        }
        const addr::Entry* found = addr_check::Find(hooks[i].first);
        if (!found || !hook::Install(*found, hooks[i].second, &originals[i]))
            return false;
    }
    return true;
}

const bool g_saveSerializer1Registered = registry::Register({
    "saves.serializer.part1",
    "saves",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Save_F5D1520,
    "Our save serializer, part 1: the segment input and output streams and the save segment's small slots",
    false,
    InstallSaveSerializer1,
});

} // namespace
} // namespace opensc4::saves
