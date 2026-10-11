// The city save code's record streams (docs/save-format.md), part 1:
// 0x005D9430 to 0x005DA0D0, the library every serialisable class of the game
// uses in its Read and Write. Replacement, off by default:
//
//   saves.streams.part1: the four small stream holders (below), the
//     reference reader and writer, the resource reference reader and writer,
//     and the first typed helpers (bool, rectangle, resource key).
//
// The four holders are 0x10 bytes each and live on the caller's stack:
//   +0x0 vtable, +0x4 the segment (counted), +0x8 the record or stream
//   (counted), +0xC the record's 16-bit version (0xFFFF until a record is
//   open).
// - Record reader (vtable 0x00AA2AF4) and record writer (0x00AA2B00): +4 is
//   a cIGZPersistDBSegment, +8 the record's cIGZPersistDBSerialRecord. The
//   reader opens the record for reading and reads the version; the writer
//   opens it for reading and writing and writes the version it was given.
// - Segment input stream (0x00AA2B0C) and output stream (0x00AA2B10): +4 is
//   a cISC4DBSegment, +8 the cISC4DBSegmentIStream or OStream the segment
//   opened for one record; the version is read from or written to that
//   stream. These are what the simulators' save and load code uses.
//
// The typed helpers are stream operators: they return the stream, not a
// result. The reference helpers return a bool in AL.
//
// Not functions: 0x005D9A10 is the failure tail of WriteReference and
// 0x005D9E60 the 'not open' tail of the output stream's Open. 0x00449550
// (a slot of both record vtables) is shared with unrelated classes.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
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

namespace istream { // cIGZIStream, by vtable offset
constexpr std::size_t kGetUint8 = 0x14;
constexpr std::size_t kGetUint16 = 0x1C;
constexpr std::size_t kGetSint32 = 0x20;
constexpr std::size_t kGetUint32 = 0x24;
constexpr std::size_t kGetError = 0x48;
// cISC4DBSegmentIStream adds:
constexpr std::size_t kGetSegment = 0x64;
constexpr std::size_t kReadSerializable = 0x68; // (cIGZSerializable** out, flags)
} // namespace istream

namespace ostream { // cIGZOStream, by vtable offset
constexpr std::size_t kSetUint8 = 0x14;
constexpr std::size_t kSetUint16 = 0x1C;
constexpr std::size_t kSetSint32 = 0x20;
constexpr std::size_t kSetUint32 = 0x24;
constexpr std::size_t kGetError = 0x48;
// cISC4DBSegmentOStream adds:
constexpr std::size_t kGetSegment = 0x64;
constexpr std::size_t kWriteSerializable = 0x68; // (cIGZSerializable*)
} // namespace ostream

namespace dbsegment { // cIGZPersistDBSegment, by vtable offset
constexpr std::size_t kOpenRecord = 0x54;  // (key, record**, access)
constexpr std::size_t kCloseRecord = 0x60; // (record*)
} // namespace dbsegment

namespace record { // cIGZPersistDBSerialRecord, by vtable offset
constexpr std::size_t kAsRecord = 0xC;
constexpr std::size_t kGetFieldUint16 = 0x24; // (uint16_t& value)
constexpr std::size_t kSetFieldUint16 = 0x84; // (value)
} // namespace record

namespace sc4segment { // cISC4DBSegment, by vtable offset
constexpr std::size_t kAsDBSegment = 0xC;
constexpr std::size_t kOpenIStream = 0x14;  // (key, stream** out)
constexpr std::size_t kCloseIStream = 0x18; // (stream)
constexpr std::size_t kOpenOStream = 0x20;  // (key, stream** out, 1)
constexpr std::size_t kCloseOStream = 0x24; // (stream)
} // namespace sc4segment

namespace resource { // cIGZPersistResource, by vtable offset
constexpr std::size_t kGetType = 0x14;
constexpr std::size_t kGetGroup = 0x18;
constexpr std::size_t kGetInstance = 0x1C;
} // namespace resource

namespace holder { // the four stream holders
constexpr std::size_t kVtable = 0x0;
constexpr std::size_t kSegment = 0x4;
constexpr std::size_t kStream = 0x8;
constexpr std::size_t kVersion = 0xC;
} // namespace holder

constexpr uint32_t kSerialRecordIid = 0xC56B8F08;  // cIGZPersistDBSerialRecord
constexpr uint32_t kSerializableIid = 0xE4FDA3D4;  // cIGZSerializable
constexpr uint32_t kSegmentIStreamIid = 0x4999A8F5; // cISC4DBSegmentIStream
constexpr uint32_t kSegmentOStreamIid = 0x6999A8FB; // cISC4DBSegmentOStream
constexpr uint32_t kSC4SegmentIid = 0x499899DA;     // cISC4DBSegment
constexpr uint32_t kResourceIid = 0x456B8F1D;       // cIGZPersistResource
constexpr std::size_t kGetResource = 0xC;           // resource manager: (key, iid, out, 0, 0)

struct ResourceKey {
    uint32_t type;
    uint32_t group;
    uint32_t instance;
};
static_assert(sizeof(ResourceKey) == 0xC);

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

// The game's 0x005D9430, the record reader's and writer's Close (slot 1 of
// the reader's vtable, slot 2 of the writer's). With a segment: the record
// is closed through it and, when that worked, released; then the segment is
// released and both members cleared. Without one the record is released.
void __fastcall RecordClose(char* self, void*)
{
    void* const segment = At<void*>(self, holder::kSegment);
    void* const serial = At<void*>(self, holder::kStream);
    if (segment) {
        if (serial) {
            void* const dbRecord = Virtual<void*>(serial, record::kAsRecord);
            if (Virtual<uint8_t>(At<void*>(self, holder::kSegment), dbsegment::kCloseRecord, dbRecord))
                Release(dbRecord);
        }
        Release(At<void*>(self, holder::kSegment));
        At<void*>(self, holder::kSegment) = nullptr;
        At<void*>(self, holder::kStream) = nullptr;
        return;
    }
    if (serial)
        Release(serial);
    At<void*>(self, holder::kStream) = nullptr;
}

// What the record reader's and writer's destructors share after the vtable
// store.
void RecordDestroy(char* self)
{
    RecordClose(self, nullptr);
    if (void* const segment = At<void*>(self, holder::kSegment))
        Release(segment);
    if (void* const serial = At<void*>(self, holder::kStream))
        Release(serial);
}

// The game's 0x005D9490, the record reader's destructor.
void __fastcall RecordReaderDestruct(char* self, void*)
{
    At<std::uintptr_t>(self, holder::kVtable) = addr::SC4RecordReader_Vtable;
    RecordDestroy(self);
}

// The game's 0x005D94C0: opens the record (type, group, instance) of a
// cIGZPersistDBSegment for reading and reads its version. Returns what the
// version read returned; 0 when there is no segment, no such record or the
// record has no serial interface. When the version cannot be read the
// record is closed and the segment let go; when the record has no serial
// interface the segment stays at +4 (the original's behaviour).
uint8_t __fastcall RecordReaderOpen(char* self, void*, void* segment, uint32_t type, uint32_t group,
                                    uint32_t instance)
{
    uint8_t result = 0;
    if (!segment)
        return result;
    At<void*>(self, holder::kSegment) = segment;
    Virtual<uint32_t>(segment, unknown::kAddRef);
    ResourceKey key{type, group, instance};
    // The original takes the record in the segment argument's own stack slot.
    void* opened = segment;
    if (!Virtual<uint8_t>(At<void*>(self, holder::kSegment), dbsegment::kOpenRecord, &key, &opened, uint32_t{1}))
        return result;
    void*& serial = At<void*>(self, holder::kStream);
    if (QueryInterface(opened, kSerialRecordIid, &serial)) {
        result = Virtual<uint8_t>(serial, record::kGetFieldUint16, &At<uint16_t>(self, holder::kVersion));
        if (!result) {
            Release(serial);
            void* const owner = At<void*>(self, holder::kSegment);
            serial = nullptr;
            Virtual<uint8_t>(owner, dbsegment::kCloseRecord, opened);
            Release(At<void*>(self, holder::kSegment));
            At<void*>(self, holder::kSegment) = nullptr;
        }
    }
    Release(opened);
    return result;
}

// The game's 0x005D9580, the record writer's destructor.
void __fastcall RecordWriterDestruct(char* self, void*)
{
    At<std::uintptr_t>(self, holder::kVtable) = addr::SC4RecordWriter_Vtable;
    RecordDestroy(self);
}

// The game's 0x005D95B0: opens the record of a cIGZPersistDBSegment for
// reading and writing (access 3) and writes the version, which is stored at
// +0xC first, segment or not. Returns what the version write returned, 0 on
// the same failures as the reader's Open.
uint8_t __fastcall RecordWriterOpen(char* self, void*, void* segment, uint32_t type, uint32_t group, uint32_t instance,
                                    uint32_t version)
{
    uint8_t result = 0;
    At<uint16_t>(self, holder::kVersion) = static_cast<uint16_t>(version);
    if (!segment)
        return result;
    At<void*>(self, holder::kSegment) = segment;
    Virtual<uint32_t>(segment, unknown::kAddRef);
    ResourceKey key{type, group, instance};
    // The original takes the record in the version argument's stack slot.
    void* opened = reinterpret_cast<void*>(static_cast<std::uintptr_t>(version));
    if (!Virtual<uint8_t>(At<void*>(self, holder::kSegment), dbsegment::kOpenRecord, &key, &opened, uint32_t{3}))
        return result;
    void*& serial = At<void*>(self, holder::kStream);
    if (QueryInterface(opened, kSerialRecordIid, &serial)) {
        result = Virtual<uint8_t>(serial, record::kSetFieldUint16, uint32_t{At<uint16_t>(self, holder::kVersion)});
        if (!result) {
            Release(serial);
            void* const owner = At<void*>(self, holder::kSegment);
            serial = nullptr;
            Virtual<uint8_t>(owner, dbsegment::kCloseRecord, opened);
            Release(At<void*>(self, holder::kSegment));
            At<void*>(self, holder::kSegment) = nullptr;
        }
    }
    Release(opened);
    return result;
}

// The game's 0x005D9670 (all four holders): 1 when both the segment and the
// stream are set. The whole of EAX is set; callers test AL.
uint32_t __fastcall HolderIsOpen(char* self, void*)
{
    if (At<void*>(self, holder::kSegment) && At<void*>(self, holder::kStream))
        return 1;
    return 0;
}

// The game's 0x005D9690: whether an object has the cIGZSerializable
// interface. The object is not checked for null.
uint8_t __cdecl IsSerializable(void* object)
{
    void* serializable = nullptr;
    const uint8_t result = QueryInterface(object, kSerializableIid, &serializable);
    if (serializable)
        Release(serializable);
    return result;
}

// The game's 0x005D96C0: reads one reference from a segment input stream
// (the serializer makes or finds the object) and asks it for an interface.
// A null reference is a success that leaves *out null.
uint8_t __cdecl ReadSerializable(void* segmentStream, uint32_t iid, void** out, uint32_t flags)
{
    if (!out)
        return 0;
    *out = nullptr;
    void* serializable = nullptr;
    if (!Virtual<uint8_t>(segmentStream, istream::kReadSerializable, &serializable, flags)) {
        if (serializable)
            Release(serializable);
        return 0;
    }
    if (!serializable)
        return 1;
    if (!QueryInterface(serializable, iid, out)) {
        if (serializable)
            Release(serializable);
        return 0;
    }
    if (serializable)
        Release(serializable);
    return 1;
}

// The game's 0x005D9730, the reference reader: the stream must be a segment
// input stream (cISC4DBSegmentIStream).
uint8_t __cdecl ReadReference(void* stream, uint32_t iid, void** out, uint32_t flags)
{
    if (!out)
        return 0;
    *out = nullptr;
    void* segmentStream = nullptr;
    if (!QueryInterface(stream, kSegmentIStreamIid, &segmentStream)) {
        if (segmentStream)
            Release(segmentStream);
        return 0;
    }
    const uint8_t result = ReadSerializable(segmentStream, iid, out, flags);
    if (segmentStream)
        Release(segmentStream);
    return result;
}

// The game's 0x005D97A0: reads a resource key (type, group, instance) and
// asks the resource manager for that resource. Returns 0 without asking
// when the stream reports an error, else what the resource manager returned.
// The type is read into the stream argument's own stack slot; the other two
// into locals the original does not set first (zero here).
uint8_t __cdecl ReadResource(void* stream, uint32_t iid, void** out)
{
    uint32_t type = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(stream));
    uint32_t group = 0;
    uint32_t instance = 0;
    Virtual<uint8_t>(stream, istream::kGetUint32, &type);
    Virtual<uint8_t>(stream, istream::kGetUint32, &group);
    Virtual<uint8_t>(stream, istream::kGetUint32, &instance);
    if (Virtual<uint8_t>(stream, istream::kGetError))
        return 0;
    ResourceKey key{type, group, instance};
    return Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), kGetResource, &key, iid, out, uint32_t{0},
                            uint32_t{0});
}

// The game's 0x005D9820, the resource reference reader: like ReadReference,
// the stream must be a segment input stream, and it is that interface the
// key is read through.
uint8_t __cdecl ReadResourceReference(void* stream, uint32_t iid, void** out)
{
    if (!out)
        return 0;
    *out = nullptr;
    void* segmentStream = nullptr;
    if (!QueryInterface(stream, kSegmentIStreamIid, &segmentStream)) {
        if (segmentStream)
            Release(segmentStream);
        return 0;
    }
    const uint8_t result = ReadResource(segmentStream, iid, out);
    if (segmentStream)
        Release(segmentStream);
    return result;
}

// The game's 0x005D9890: writes an object's resource key (three dwords) and
// returns whether the stream is free of errors; for a null object or one
// that is not a resource it writes three zeros and returns 0.
uint8_t __cdecl WriteResource(void* stream, void* object)
{
    void* found = nullptr;
    if (object && QueryInterface(object, kResourceIid, &found)) {
        const uint32_t type = Virtual<uint32_t>(found, resource::kGetType);
        Virtual<uint8_t>(stream, ostream::kSetUint32, type);
        const uint32_t group = Virtual<uint32_t>(found, resource::kGetGroup);
        Virtual<uint8_t>(stream, ostream::kSetUint32, group);
        const uint32_t instance = Virtual<uint32_t>(found, resource::kGetInstance);
        Virtual<uint8_t>(stream, ostream::kSetUint32, instance);
        const uint8_t error = Virtual<uint8_t>(stream, ostream::kGetError);
        const uint8_t result = error ? 0 : 1;
        if (found)
            Release(found);
        return result;
    }
    Virtual<uint8_t>(stream, ostream::kSetUint32, uint32_t{0});
    Virtual<uint8_t>(stream, ostream::kSetUint32, uint32_t{0});
    Virtual<uint8_t>(stream, ostream::kSetUint32, uint32_t{0});
    if (found)
        Release(found);
    return 0;
}

// The game's 0x005D9940, the resource reference writer: the stream must be
// a segment output stream, and it is that interface the key is written to.
uint8_t __cdecl WriteResourceReference(void* stream, void* object)
{
    void* segmentStream = nullptr;
    if (!QueryInterface(stream, kSegmentOStreamIid, &segmentStream)) {
        if (segmentStream)
            Release(segmentStream);
        return 0;
    }
    const uint8_t result = WriteResource(segmentStream, object);
    if (segmentStream)
        Release(segmentStream);
    return result;
}

// The game's 0x005D99A0, the reference writer: hands the object's
// cIGZSerializable (null for no object or an object without one) to the
// segment output stream, which writes the 8 bytes and queues the object.
uint8_t __cdecl WriteReference(void* stream, void* object)
{
    void* segmentStream = nullptr;
    if (!QueryInterface(stream, kSegmentOStreamIid, &segmentStream)) {
        if (segmentStream)
            Release(segmentStream);
        return 0;
    }
    void* serializable = nullptr;
    if (object)
        QueryInterface(object, kSerializableIid, &serializable);
    const uint8_t result = Virtual<uint8_t>(segmentStream, ostream::kWriteSerializable, serializable);
    if (serializable)
        Release(serializable);
    if (segmentStream)
        Release(segmentStream);
    return result;
}

// The game's 0x005D9A20: the record reader's constructor from a
// cIGZPersistDBSegment.
char* __fastcall RecordReaderConstruct(char* self, void*, void* segment, uint32_t type, uint32_t group,
                                       uint32_t instance)
{
    At<void*>(self, holder::kSegment) = nullptr;
    At<void*>(self, holder::kStream) = nullptr;
    At<std::uintptr_t>(self, holder::kVtable) = addr::SC4RecordReader_Vtable;
    At<uint16_t>(self, holder::kVersion) = 0xFFFF;
    RecordReaderOpen(self, nullptr, segment, type, group, instance);
    return self;
}

// The game's 0x005D9A60, the record reader's deleting destructor (slot 0).
char* __fastcall RecordReaderDelete(char* self, void*, uint32_t flags)
{
    At<std::uintptr_t>(self, holder::kVtable) = addr::SC4RecordReader_Vtable;
    RecordDestroy(self);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// The segment behind a segment stream, as the two 'from a stream'
// constructors find it. The stream interface is released before it is used,
// as in the original (the caller's stream keeps the object alive).
void* SegmentOfStream(void* stream, uint32_t iid, std::size_t getSegment)
{
    void* segmentStream = nullptr;
    QueryInterface(stream, iid, &segmentStream);
    if (!segmentStream)
        return nullptr;
    Release(segmentStream);
    void* const sc4Segment = Virtual<void*>(segmentStream, getSegment);
    if (!sc4Segment)
        return nullptr;
    return Virtual<void*>(sc4Segment, sc4segment::kAsDBSegment);
}

// The game's 0x005D9AA0: the record reader's constructor from a segment
// input stream: the record is opened in that stream's segment. The stream
// is not checked for null.
char* __fastcall RecordReaderConstructFromStream(char* self, void*, void* stream, uint32_t type, uint32_t group,
                                                 uint32_t instance)
{
    At<std::uintptr_t>(self, holder::kVtable) = addr::SC4RecordReader_Vtable;
    At<void*>(self, holder::kSegment) = nullptr;
    At<void*>(self, holder::kStream) = nullptr;
    At<uint16_t>(self, holder::kVersion) = 0xFFFF;
    void* const segment = SegmentOfStream(stream, kSegmentIStreamIid, istream::kGetSegment);
    RecordReaderOpen(self, nullptr, segment, type, group, instance);
    return self;
}

// The game's 0x005D9B20: the record writer's constructor from a
// cIGZPersistDBSegment.
char* __fastcall RecordWriterConstruct(char* self, void*, void* segment, uint32_t type, uint32_t group,
                                       uint32_t instance, uint32_t version)
{
    At<void*>(self, holder::kSegment) = nullptr;
    At<void*>(self, holder::kStream) = nullptr;
    At<std::uintptr_t>(self, holder::kVtable) = addr::SC4RecordWriter_Vtable;
    At<uint16_t>(self, holder::kVersion) = 0xFFFF;
    RecordWriterOpen(self, nullptr, segment, type, group, instance, version);
    return self;
}

// The game's 0x005D9B60, the record writer's deleting destructor (slot 0).
char* __fastcall RecordWriterDelete(char* self, void*, uint32_t flags)
{
    At<std::uintptr_t>(self, holder::kVtable) = addr::SC4RecordWriter_Vtable;
    RecordDestroy(self);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// The game's 0x005D9BA0: the record writer's constructor from a segment
// output stream.
char* __fastcall RecordWriterConstructFromStream(char* self, void*, void* stream, uint32_t type, uint32_t group,
                                                 uint32_t instance, uint32_t version)
{
    At<std::uintptr_t>(self, holder::kVtable) = addr::SC4RecordWriter_Vtable;
    At<void*>(self, holder::kSegment) = nullptr;
    At<void*>(self, holder::kStream) = nullptr;
    At<uint16_t>(self, holder::kVersion) = 0xFFFF;
    void* const segment = SegmentOfStream(stream, kSegmentOStreamIid, ostream::kGetSegment);
    RecordWriterOpen(self, nullptr, segment, type, group, instance, version);
    return self;
}

// What the two segment streams' destructors share after the vtable store
// and the close call: both members are cleared and released, then looked at
// once more (as the original's inlined member destructors do).
void SegmentStreamDestroy(char* self)
{
    Clear(At<void*>(self, holder::kSegment));
    Clear(At<void*>(self, holder::kStream));
    if (void* const stream = At<void*>(self, holder::kStream))
        Release(stream);
    if (void* const segment = At<void*>(self, holder::kSegment))
        Release(segment);
}

// The game's 0x005D9C30, the segment input stream's destructor (callers use
// it as Close): the segment closes the stream when both are set.
void __fastcall SegmentIStreamClose(char* self, void*)
{
    void* const segment = At<void*>(self, holder::kSegment);
    At<std::uintptr_t>(self, holder::kVtable) = addr::SC4SegmentIStream_Vtable;
    if (segment) {
        if (void* const stream = At<void*>(self, holder::kStream))
            Virtual<uint8_t>(segment, sc4segment::kCloseIStream, stream);
    }
    SegmentStreamDestroy(self);
}

// The game's 0x005D9C90: has a cISC4DBSegment open an input stream on the
// record and reads the version from it. Returns 1 with the segment kept at
// +4 when the stream reports no error; else the stream is closed and
// released, the segment is not kept, and 0 is returned.
uint8_t __fastcall SegmentIStreamOpen(char* self, void*, void* segment, uint32_t type, uint32_t group,
                                      uint32_t instance)
{
    if (!segment)
        return 0;
    ResourceKey key{type, group, instance};
    void*& stream = At<void*>(self, holder::kStream);
    Clear(stream);
    if (!Virtual<uint8_t>(segment, sc4segment::kOpenIStream, &key, &stream))
        return 0;
    Virtual<uint8_t>(stream, istream::kGetUint16, &At<uint16_t>(self, holder::kVersion));
    if (!Virtual<uint8_t>(stream, istream::kGetError)) {
        This<void*>(addr::SC4RefPtr_Assign, self + holder::kSegment, segment);
        return 1;
    }
    Virtual<uint8_t>(segment, sc4segment::kCloseIStream, stream);
    Clear(stream);
    return 0;
}

// The game's 0x005D9D50, the segment output stream's destructor (callers
// use it as Close).
void __fastcall SegmentOStreamClose(char* self, void*)
{
    void* const segment = At<void*>(self, holder::kSegment);
    At<std::uintptr_t>(self, holder::kVtable) = addr::SC4SegmentOStream_Vtable;
    if (segment) {
        if (void* const stream = At<void*>(self, holder::kStream))
            Virtual<uint8_t>(segment, sc4segment::kCloseOStream, stream);
    }
    SegmentStreamDestroy(self);
}

// The game's 0x005D9DB0: has a cISC4DBSegment open an output stream on the
// record and writes the version to it (the caller's whole dword is pushed;
// its low word is kept at +0xC). Returns whether the holder is open (1 or 0
// in EAX), which also covers a null segment.
uint32_t __fastcall SegmentOStreamOpen(char* self, void*, void* segment, uint32_t type, uint32_t group,
                                       uint32_t instance, uint32_t version)
{
    if (segment) {
        ResourceKey key{type, group, instance};
        void*& stream = At<void*>(self, holder::kStream);
        Clear(stream);
        if (Virtual<uint8_t>(segment, sc4segment::kOpenOStream, &key, &stream, uint32_t{1})) {
            At<uint16_t>(self, holder::kVersion) = static_cast<uint16_t>(version);
            Virtual<uint8_t>(stream, ostream::kSetUint16, version);
            if (Virtual<uint8_t>(stream, ostream::kGetError)) {
                Virtual<uint8_t>(segment, sc4segment::kCloseOStream, stream);
                Clear(stream);
            } else {
                This<void*>(addr::SC4RefPtr_Assign, self + holder::kSegment, segment);
            }
        }
    }
    if (At<void*>(self, holder::kSegment) && At<void*>(self, holder::kStream))
        return 1;
    return 0;
}

// The game's 0x005D9E70: the segment input stream's constructor. The first
// argument is any interface of the save file's segment object; nothing is
// opened when it is null or has no cISC4DBSegment.
char* __fastcall SegmentIStreamConstruct(char* self, void*, void* segmentObject, uint32_t type, uint32_t group,
                                         uint32_t instance)
{
    At<std::uintptr_t>(self, holder::kVtable) = addr::SC4SegmentIStream_Vtable;
    At<void*>(self, holder::kSegment) = nullptr;
    At<void*>(self, holder::kStream) = nullptr;
    At<uint16_t>(self, holder::kVersion) = 0xFFFF;
    void* segment = nullptr;
    if (segmentObject) {
        if (QueryInterface(segmentObject, kSC4SegmentIid, &segment))
            SegmentIStreamOpen(self, nullptr, segment, type, group, instance);
        if (segment)
            Release(segment);
    }
    return self;
}

// The game's 0x005D9EE0, the segment input stream's deleting destructor
// (its vtable's only slot).
char* __fastcall SegmentIStreamDelete(char* self, void*, uint32_t flags)
{
    SegmentIStreamClose(self, nullptr);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// The game's 0x005D9F00: the segment output stream's constructor.
char* __fastcall SegmentOStreamConstruct(char* self, void*, void* segmentObject, uint32_t type, uint32_t group,
                                         uint32_t instance, uint32_t version)
{
    At<std::uintptr_t>(self, holder::kVtable) = addr::SC4SegmentOStream_Vtable;
    At<void*>(self, holder::kSegment) = nullptr;
    At<void*>(self, holder::kStream) = nullptr;
    At<uint16_t>(self, holder::kVersion) = 0xFFFF;
    void* segment = nullptr;
    if (segmentObject) {
        if (QueryInterface(segmentObject, kSC4SegmentIid, &segment))
            SegmentOStreamOpen(self, nullptr, segment, type, group, instance, version);
        if (segment)
            Release(segment);
    }
    return self;
}

// The game's 0x005D9F80, the segment output stream's deleting destructor
// (its vtable's only slot).
char* __fastcall SegmentOStreamDelete(char* self, void*, uint32_t flags)
{
    SegmentOStreamClose(self, nullptr);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// The game's 0x005D9FA0: reads a byte and stores whether it is not zero.
// The byte is read into the low byte of the stream argument's own stack
// slot, so a stream that stores nothing leaves 'the stream's address has a
// low byte' as the answer. Returns the stream.
void* __cdecl ReadBool(void* stream, uint8_t* value)
{
    std::uintptr_t slot = reinterpret_cast<std::uintptr_t>(stream);
    Virtual<uint8_t>(stream, istream::kGetUint8, reinterpret_cast<uint8_t*>(&slot));
    *value = static_cast<uint8_t>(slot) != 0 ? 1 : 0;
    return stream;
}

// The game's 0x005D9FD0: writes a bool as one byte (the caller's whole
// dword is pushed). Returns the stream.
void* __cdecl WriteBool(void* stream, uint32_t value)
{
    Virtual<uint8_t>(stream, ostream::kSetUint8, value);
    return stream;
}

// The game's 0x005D9FF0: reads a rectangle, four signed dwords in memory
// order. Returns the stream.
void* __cdecl ReadRect(void* stream, int32_t* rect)
{
    Virtual<uint8_t>(stream, istream::kGetSint32, rect);
    Virtual<uint8_t>(stream, istream::kGetSint32, rect + 1);
    Virtual<uint8_t>(stream, istream::kGetSint32, rect + 2);
    Virtual<uint8_t>(stream, istream::kGetSint32, rect + 3);
    return stream;
}

// The game's 0x005DA030: writes a rectangle. Returns the stream.
void* __cdecl WriteRect(void* stream, const int32_t* rect)
{
    Virtual<uint8_t>(stream, ostream::kSetSint32, rect[0]);
    Virtual<uint8_t>(stream, ostream::kSetSint32, rect[1]);
    Virtual<uint8_t>(stream, ostream::kSetSint32, rect[2]);
    Virtual<uint8_t>(stream, ostream::kSetSint32, rect[3]);
    return stream;
}

// The game's 0x005DA070: reads a resource key, three dwords (type, group,
// instance). Returns the stream.
void* __cdecl ReadResourceKey(void* stream, uint32_t* key)
{
    Virtual<uint8_t>(stream, istream::kGetUint32, key);
    Virtual<uint8_t>(stream, istream::kGetUint32, key + 1);
    Virtual<uint8_t>(stream, istream::kGetUint32, key + 2);
    return stream;
}

// The game's 0x005DA0A0: writes a resource key. Returns the stream.
void* __cdecl WriteResourceKey(void* stream, const uint32_t* key)
{
    Virtual<uint8_t>(stream, ostream::kSetUint32, key[0]);
    Virtual<uint8_t>(stream, ostream::kSetUint32, key[1]);
    Virtual<uint8_t>(stream, ostream::kSetUint32, key[2]);
    return stream;
}

bool InstallSaveStreams1(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[34] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4Save_F5D9430, reinterpret_cast<void*>(&RecordClose)},
        {addr::SC4Save_F5D9490, reinterpret_cast<void*>(&RecordReaderDestruct)},
        {addr::SC4Save_F5D94C0, reinterpret_cast<void*>(&RecordReaderOpen)},
        {addr::SC4Save_F5D9580, reinterpret_cast<void*>(&RecordWriterDestruct)},
        {addr::SC4Save_F5D95B0, reinterpret_cast<void*>(&RecordWriterOpen)},
        {addr::SC4SegmentStream_IsOpen, reinterpret_cast<void*>(&HolderIsOpen)},
        {addr::SC4Save_F5D9690, reinterpret_cast<void*>(&IsSerializable)},
        {addr::SC4Save_F5D96C0, reinterpret_cast<void*>(&ReadSerializable)},
        {addr::SC4Stream_ReadReference, reinterpret_cast<void*>(&ReadReference)},
        {addr::SC4Save_F5D97A0, reinterpret_cast<void*>(&ReadResource)},
        {addr::SC4Save_F5D9820, reinterpret_cast<void*>(&ReadResourceReference)},
        {addr::SC4Save_F5D9890, reinterpret_cast<void*>(&WriteResource)},
        {addr::SC4Save_F5D9940, reinterpret_cast<void*>(&WriteResourceReference)},
        {addr::SC4Stream_WriteReference, reinterpret_cast<void*>(&WriteReference)},
        {addr::SC4Save_F5D9A20, reinterpret_cast<void*>(&RecordReaderConstruct)},
        {addr::SC4Save_F5D9A60, reinterpret_cast<void*>(&RecordReaderDelete)},
        {addr::SC4Save_F5D9AA0, reinterpret_cast<void*>(&RecordReaderConstructFromStream)},
        {addr::SC4Save_F5D9B20, reinterpret_cast<void*>(&RecordWriterConstruct)},
        {addr::SC4Save_F5D9B60, reinterpret_cast<void*>(&RecordWriterDelete)},
        {addr::SC4Save_F5D9BA0, reinterpret_cast<void*>(&RecordWriterConstructFromStream)},
        {addr::SC4SegmentIStream_Close, reinterpret_cast<void*>(&SegmentIStreamClose)},
        {addr::SC4Save_F5D9C90, reinterpret_cast<void*>(&SegmentIStreamOpen)},
        {addr::SC4SegmentOStream_Close, reinterpret_cast<void*>(&SegmentOStreamClose)},
        {addr::SC4Save_F5D9DB0, reinterpret_cast<void*>(&SegmentOStreamOpen)},
        {addr::SC4SegmentIStream_SC4SegmentIStream, reinterpret_cast<void*>(&SegmentIStreamConstruct)},
        {addr::SC4Save_F5D9EE0, reinterpret_cast<void*>(&SegmentIStreamDelete)},
        {addr::SC4SegmentOStream_SC4SegmentOStream, reinterpret_cast<void*>(&SegmentOStreamConstruct)},
        {addr::SC4Save_F5D9F80, reinterpret_cast<void*>(&SegmentOStreamDelete)},
        {addr::SC4Stream_ReadBool, reinterpret_cast<void*>(&ReadBool)},
        {addr::SC4Stream_WriteBool, reinterpret_cast<void*>(&WriteBool)},
        {addr::SC4Stream_ReadRect, reinterpret_cast<void*>(&ReadRect)},
        {addr::SC4Stream_WriteRect, reinterpret_cast<void*>(&WriteRect)},
        {addr::SC4Stream_ReadResourceKey, reinterpret_cast<void*>(&ReadResourceKey)},
        {addr::SC4Stream_WriteResourceKey, reinterpret_cast<void*>(&WriteResourceKey)},
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

const bool g_saveStreams1Registered = registry::Register({
    "saves.streams.part1",
    "saves",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Stream_ReadReference,
    "Our save record streams, part 1: record and segment stream holders, references, bool, rectangle, resource key",
    false,
    InstallSaveStreams1,
});

} // namespace
} // namespace opensc4::saves
