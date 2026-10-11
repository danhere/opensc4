// City files as the region sees them (docs/region.md), part 1: 0x005DBC10 to
// 0x005DCA10, free functions used by the application, the region classes and
// the region screen. Replacement, off by default:
//
//   region.files.part1: opening and closing a city file (a packed file
//     segment), its version record, copying a record out to a plain file,
//     the text records, reading and writing the regional city's record, new
//     serial numbers, file names and paths for cities, loading a picture
//     file into a buffer, and the two methods of the small focus watcher the
//     picture maker (0x005DDEC0) uses.
//
// A city file is a cSC4DBSegmentPackedFile (0x1DC bytes, constructor
// 0x005D3740); its main slot 3 gives the cIGZPersistDBSegment part at +8
// (vtable 0x00AA24F0), which is what every function here calls "segment".
// The version is the dword in record (0xEA3FF4F5, 0xEA3FF506, 0xEA3FF4F5):
// 0x41 is what this build writes, 0x3E to 0x41 is what it reads. The
// regional city's record is (0xCA027EDB, 0xCA027EE1, 0).
//
// The focus watcher (0xC bytes, vtable 0x00AA2B5C, a message target): +4 the
// reference count, +8 whether the application has the focus, +9 whether it
// lost it at any time since the watcher was made.
//
// Still the game's: 0x005DC8A0 (the watcher's QueryInterface, folded with
// two unrelated classes), 0x005DC950 and 0x005DC980 (rectangle equality and
// union, called only by framework code). 0x005DC600 is not a function: it is
// a label inside 0x005DC400.
#include "core/address_check.h"
#include "core/fixed_seed.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace opensc4::region {
namespace {

namespace segment { // cIGZPersistDBSegment, by vtable offset
constexpr std::size_t kAddRef = 0x4;
constexpr std::size_t kRelease = 0x8;
constexpr std::size_t kInit = 0x10;
constexpr std::size_t kShutdown = 0x14;
constexpr std::size_t kOpen = 0x18;          // (read, write)
constexpr std::size_t kIsOpen = 0x1C;
constexpr std::size_t kClose = 0x20;
constexpr std::size_t kSetPath = 0x2C;       // (const cIGZString&)
constexpr std::size_t kTestForRecord = 0x4C; // (key)
constexpr std::size_t kGetRecordSize = 0x50; // (key)
constexpr std::size_t kOpenRecord = 0x54;    // (key, record**, access)
constexpr std::size_t kReadRecord = 0x70;    // (key, buffer, size&)
constexpr std::size_t kWriteRecord = 0x74;   // (key, buffer, size)
} // namespace segment

namespace text { // cIGZString, by vtable offset
constexpr std::size_t kData = 0x18;
constexpr std::size_t kStrlen = 0x1C;
constexpr std::size_t kInsert = 0x48; // (position, const cIGZString&)
constexpr std::size_t kRFind = 0x68;  // (const char*, position, case sensitive)
} // namespace text

namespace watcher { // the focus watcher
constexpr std::size_t kCount = 0x4;
constexpr std::size_t kFocused = 0x8;
constexpr std::size_t kLostFocus = 0x9;
} // namespace watcher

constexpr uint32_t kVersionType = 0xEA3FF4F5; // also the instance
constexpr uint32_t kVersionGroup = 0xEA3FF506;
constexpr uint32_t kCurrentVersion = 0x41;
constexpr uint32_t kOldestVersion = 0x3E;

constexpr uint32_t kTextType = 0x2026960B;
constexpr uint32_t kSerialRecordIid = 0xC56B8F08; // cIGZPersistDBSerialRecord
constexpr std::size_t kGetFieldString = 0x60;
constexpr std::size_t kSetFieldString = 0xC0;

constexpr uint32_t kCityRecordType = 0xCA027EDB;
constexpr uint32_t kCityRecordGroup = 0xCA027EE1;
constexpr uint32_t kRegionalCityClass = 0x6A037C24;
constexpr uint32_t kRegionalCityIid = 0x69D2BE1D;
constexpr uint32_t kSerializableIid = 0xE4FDA3D4;

constexpr uint32_t kFocusMessage = 0x4348B111;

struct ResourceKey {
    uint32_t type;
    uint32_t group;
    uint32_t instance;
};
static_assert(sizeof(ResourceKey) == 0xC);

// The game's string body and string.
struct StringBody {
    char* begin;
    char* end;
    char* capacity;
};

struct GameString {
    std::uintptr_t vtable;
    StringBody body;
    uint32_t zero;
};
static_assert(sizeof(GameString) == 0x14);

// The stream object the game puts on the stack for one record of a segment
// (SC4SegmentIStream, SC4SegmentOStream).
struct SegmentStream {
    std::uintptr_t vtable;
    void* segment;
    void* stream;
    uint32_t word;
};
static_assert(sizeof(SegmentStream) == 0x10);

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

void Free(void* memory)
{
    Cdecl<void>(addr::SC4_Free, memory);
}

void FreeBody(StringBody& body)
{
    if (body.begin)
        Free(body.begin);
}

// An empty string as the game's inlined constructor makes it.
void InitString(GameString& string)
{
    string.body.begin = nullptr;
    string.body.end = nullptr;
    string.body.capacity = nullptr;
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    string.body.capacity = buffer + 8;
    string.body.begin = buffer;
    string.body.end = buffer;
    *buffer = 0;
    string.vtable = addr::SC4String_Vtable;
    string.zero = 0;
}

// A new string with a copy of another string's text (its length is asked
// first, then its characters).
void InitFromString(GameString& string, void* source, char* tag)
{
    const uint32_t length = Virtual<uint32_t>(source, text::kStrlen);
    const char* const chars = Virtual<const char*>(source, text::kData);
    string.body.begin = nullptr;
    string.body.end = nullptr;
    string.body.capacity = nullptr;
    This<void>(addr::SC4String_FromRange, &string.body, chars, chars + length, tag);
    string.vtable = addr::SC4String_Vtable;
    string.zero = 0;
}

// A new packed file object and its segment part, counted once. As in the
// original, a failed allocation is not survived.
void* NewSegment()
{
    void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x1DC});
    void* const file = memory ? This<void*>(addr::cSC4DBSegmentPackedFile_Construct, memory) : nullptr;
    void* const part = Virtual<void*>(file, 0xC);
    Virtual<uint32_t>(part, segment::kAddRef);
    return part;
}

// The game's 0x005DBC10, cdecl (segment): closes, shuts down and releases an
// open segment; false when it was not open (nothing is done then).
uint8_t __cdecl CloseSegment(void* part)
{
    if (!Virtual<uint8_t>(part, segment::kIsOpen))
        return 0;
    Virtual<uint8_t>(part, segment::kClose);
    Virtual<uint8_t>(part, segment::kShutdown);
    Virtual<uint32_t>(part, segment::kRelease);
    return 1;
}

// The game's 0x005DBC40, cdecl (segment, key, path): writes the record's
// bytes to the file at path (opened for writing, made if missing, cut to
// nothing first). Returns the number of bytes, -1 on failure; a file that
// was not there before is removed again when nothing was written.
int32_t __cdecl CopyRecordToFile(void* part, ResourceKey* key, void* path)
{
    int32_t result = -1;
    alignas(4) char file[0x80];
    This<void*>(addr::cRZFile_Construct, file, path);
    const uint8_t existed = Cdecl<uint8_t>(addr::RZFile_Exists, path);
    if (This<uint8_t>(addr::cRZFile_Open, file, uint32_t{2}, uint32_t{3}, uint32_t{1})) {
        result = Virtual<int32_t>(part, segment::kGetRecordSize, key);
        if (result != -1) {
            void* const buffer = Cdecl<void*>(addr::SC4_operator_new, static_cast<uint32_t>(result));
            result = Virtual<int32_t>(part, segment::kReadRecord, key, buffer, &result);
            if (result != -1) {
                This<uint32_t>(addr::cRZFile_SeekToBegin, file);
                This<uint8_t>(addr::cRZFile_SetLength, file, uint32_t{0});
                if (!This<uint8_t>(addr::cRZFile_Write, file, buffer, static_cast<uint32_t>(result)))
                    result = -1;
            }
            Cdecl<void>(addr::SC4_operator_delete, buffer);
        }
        This<uint8_t>(addr::cRZFile_Close, file);
        if (result == -1 && !existed)
            Cdecl<uint8_t>(addr::RZFile_Remove, path);
    }
    This<void>(addr::cRZFile_Destruct, file);
    return result;
}

// The game's 0x005DBD40, cdecl with no arguments: a serial number for a new
// city: the seconds since the last of January 2000 (local midnight, the C
// runtime's time zone added) shifted left four, and four random bits from a
// generator seeded from the clock.
uint32_t __cdecl NewCitySerial()
{
    const uint32_t now = Cdecl<uint32_t>(addr::CRT_time32, static_cast<uint32_t*>(nullptr));
    alignas(4) char random[0x18];
    {
        // The clock seed counts under the original's call site.
        const fixed_seed::SiteOverride site(0x005DBD5D);
        This<void*>(addr::SC4Random_Construct, random, int32_t{-1});
    }
    // struct tm: seconds, minutes, hours, day of the month, month, year since
    // 1900, day of the week, day of the year, daylight saving flag.
    int32_t date[9];
    date[4] = 1;
    date[6] = 1;
    date[7] = 1;
    date[8] = 1;
    date[0] = 0;
    date[1] = 0;
    date[2] = 0;
    date[3] = 0;
    date[5] = 100;
    const uint32_t epoch = Cdecl<uint32_t>(addr::CRT_mktime, date);
    const uint32_t seconds = Global<uint32_t>(addr::SC4_Timezone) - epoch + now;
    const uint32_t draw = This<uint32_t>(addr::cRZRandom_Advance, random);
    return (draw & 0xFu) | (seconds << 4);
}

// The game's 0x005DBDC0, cdecl (key, path): finds the segment that holds the
// record through the resource manager and copies the record to a file.
// Returns the number of bytes, -1 on failure.
int32_t __cdecl WriteToFile(ResourceKey* key, void* path)
{
    void* found = nullptr;
    if (Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0x80, key, &found)) {
        const int32_t result = CopyRecordToFile(found, key, path);
        if (found)
            Virtual<uint32_t>(found, segment::kRelease);
        return result;
    }
    if (found)
        Virtual<uint32_t>(found, segment::kRelease);
    return -1;
}

// The game's 0x005DBE20, cdecl (segment, group, instance, string out): reads
// the text record with that group and instance into the string.
uint8_t __cdecl LoadText(void* part, uint32_t group, uint32_t instance, void* out)
{
    ResourceKey key;
    key.group = group;
    key.instance = instance;
    uint8_t result = 0;
    key.type = kTextType;
    void* record = reinterpret_cast<void*>(group);
    if (!Virtual<uint8_t>(part, segment::kOpenRecord, &key, &record, uint32_t{1}))
        return result;
    void* serial = reinterpret_cast<void*>(instance);
    if (Virtual<uint8_t>(record, 0x0, kSerialRecordIid, &serial)) {
        result = Virtual<uint8_t>(serial, kGetFieldString, out);
        Virtual<uint32_t>(serial, 0x8);
    }
    Virtual<uint8_t>(record, 0x14); // Close
    Virtual<uint32_t>(record, 0x8);
    return result;
}

// The game's 0x005DBEA0, cdecl (segment, group, instance, string): writes
// the string as the text record with that group and instance.
uint8_t __cdecl SaveText(void* part, uint32_t group, uint32_t instance, void* string)
{
    ResourceKey key;
    key.group = group;
    key.instance = instance;
    uint8_t result = 0;
    key.type = kTextType;
    void* record = reinterpret_cast<void*>(group);
    if (!Virtual<uint8_t>(part, segment::kOpenRecord, &key, &record, uint32_t{3}))
        return result;
    void* serial = reinterpret_cast<void*>(instance);
    if (Virtual<uint8_t>(record, 0x0, kSerialRecordIid, &serial)) {
        result = Virtual<uint8_t>(serial, kSetFieldString, string);
        Virtual<uint32_t>(serial, 0x8);
    }
    Virtual<uint8_t>(record, 0x14); // Close
    Virtual<uint32_t>(record, 0x8);
    return result;
}

// The game's 0x005DBF30, cdecl (segment): the file's version: the dword in
// the version record; when that cannot be read, 0 if the record is there
// and the current version (0x41) if it is not.
uint32_t __cdecl GetVersion(void* part)
{
    ResourceKey key;
    uint32_t version = 0;
    uint32_t size = 4;
    key.type = kVersionType;
    key.instance = kVersionType;
    key.group = kVersionGroup;
    if (Virtual<int32_t>(part, segment::kReadRecord, &key, &version, &size) != -1)
        return version;
    return Virtual<uint8_t>(part, segment::kTestForRecord, &key) ? 0u : kCurrentVersion;
}

// The game's 0x005DBFA0, cdecl (segment): writes the version record (0x41).
uint8_t __cdecl SetVersion(void* part)
{
    ResourceKey key;
    uint32_t version = kCurrentVersion;
    key.type = kVersionType;
    key.instance = kVersionType;
    key.group = kVersionGroup;
    return Virtual<uint8_t>(part, segment::kWriteRecord, &key, &version, uint32_t{4});
}

// The game's 0x005DBFE0, cdecl (path, write, check version): opens a city
// file, for writing when `write` is set (its low byte counts), else for
// reading. With the check on, a file of a version outside 0x3E to 0x41 is
// refused. Returns the open segment, counted once, or null.
void* __cdecl OpenSegment(void* path, uint32_t write, uint32_t checkVersion)
{
    void* const part = NewSegment();
    if (Virtual<uint8_t>(part, segment::kInit)) {
        Virtual<uint8_t>(part, segment::kSetPath, path);
        // The original makes the read flag in the low byte of a copy of the
        // write argument.
        const uint32_t read = (write & 0xFFFFFF00u) | ((write & 0xFFu) == 0 ? 1u : 0u);
        if (Virtual<uint8_t>(part, segment::kOpen, read, write)) {
            const uint32_t version = GetVersion(part);
            if ((checkVersion & 0xFFu) == 0)
                return part;
            if (version >= kOldestVersion && version <= kCurrentVersion)
                return part;
        }
        Virtual<uint8_t>(part, segment::kShutdown);
    }
    Virtual<uint32_t>(part, segment::kRelease);
    return nullptr;
}

// The game's 0x005DC070, cdecl (segment, city**): makes a regional city
// (through the class factory, or directly when that fails), has it read its
// record from the segment and, when that worked, gives it out counted and
// initialised (main slot 3).
uint8_t __cdecl ReadCity(void* part, void** out)
{
    uint8_t result = 0;
    SegmentStream stream;
    This<void*>(addr::SC4SegmentIStream_SC4SegmentIStream, &stream, part, kCityRecordType, kCityRecordGroup,
                uint32_t{0});
    if (This<uint8_t>(addr::SC4SegmentStream_IsOpen, &stream)) {
        void* city = nullptr;
        void* const com = Cdecl<void*>(addr::RZGetCOM);
        if (!Virtual<uint8_t>(com, 0xC, kRegionalCityClass, kRegionalCityIid, &city)) {
            void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x1A0});
            void* const made = memory ? This<void*>(addr::cSC4RegionalCity_Construct, memory) : nullptr;
            if (made != city) {
                void* const old = city;
                if (made)
                    Virtual<uint32_t>(made, 0x4);
                city = made;
                if (old)
                    Virtual<uint32_t>(old, 0x8);
            }
        }
        void* record = nullptr;
        if (Virtual<uint8_t>(city, 0x0, kSerializableIid, &record)) {
            result = Virtual<uint8_t>(record, 0x10, stream.stream); // Read
            if (result) {
                *out = city;
                Virtual<uint32_t>(city, 0x4);
                Virtual<uint8_t>(*out, 0xC); // Init
            }
        }
        if (record)
            Virtual<uint32_t>(record, 0x8);
        if (city)
            Virtual<uint32_t>(city, 0x8);
    }
    This<void>(addr::SC4SegmentIStream_Close, &stream);
    return result;
}

// The game's 0x005DC1A0, cdecl (segment, city): has the regional city write
// its record to the segment.
uint8_t __cdecl WriteCity(void* part, void* city)
{
    SegmentStream stream;
    uint8_t result = 0;
    This<void*>(addr::SC4SegmentOStream_SC4SegmentOStream, &stream, part, kCityRecordType, kCityRecordGroup,
                uint32_t{0}, uint32_t{1});
    if (This<uint8_t>(addr::SC4SegmentStream_IsOpen, &stream)) {
        void* record = nullptr;
        if (Virtual<uint8_t>(city, 0x0, kSerializableIid, &record))
            result = Virtual<uint8_t>(record, 0xC, stream.stream); // Write
        if (record)
            Virtual<uint32_t>(record, 0x8);
    }
    This<void>(addr::SC4SegmentOStream_Close, &stream);
    return result;
}

// What LoadFromFile and SaveToFile do after their work when the version was
// right: an open segment is closed, shut down and released (one that is not
// open is left as it is, counted).
void Finish(void* part)
{
    if (Virtual<uint8_t>(part, segment::kIsOpen)) {
        Virtual<uint8_t>(part, segment::kClose);
        Virtual<uint8_t>(part, segment::kShutdown);
        Virtual<uint32_t>(part, segment::kRelease);
    }
}

// The game's 0x005DC220, cdecl (path, city**): opens the city file for
// reading, checks its version and reads the regional city from it.
uint8_t __cdecl LoadFromFile(void* path, void** out)
{
    void* const part = NewSegment();
    if (Virtual<uint8_t>(part, segment::kInit)) {
        Virtual<uint8_t>(part, segment::kSetPath, path);
        if (Virtual<uint8_t>(part, segment::kOpen, uint32_t{1}, uint32_t{0})) {
            const uint32_t version = GetVersion(part);
            if (version >= kOldestVersion && version <= kCurrentVersion) {
                const uint8_t result = ReadCity(part, out);
                Finish(part);
                return result;
            }
        }
        Virtual<uint8_t>(part, segment::kShutdown);
    }
    Virtual<uint32_t>(part, segment::kRelease);
    return 0;
}

// The game's 0x005DC2E0, cdecl (path, city): opens the city file for writing,
// checks its version (a file without the record counts as current) and
// writes the regional city's record.
uint8_t __cdecl SaveToFile(void* path, void* city)
{
    void* const part = NewSegment();
    if (Virtual<uint8_t>(part, segment::kInit)) {
        Virtual<uint8_t>(part, segment::kSetPath, path);
        if (Virtual<uint8_t>(part, segment::kOpen, uint32_t{0}, uint32_t{1})) {
            const uint32_t version = GetVersion(part);
            if (version >= kOldestVersion && version <= kCurrentVersion) {
                const uint8_t result = WriteCity(part, city);
                Finish(part);
                return result;
            }
        }
        Virtual<uint8_t>(part, segment::kShutdown);
    }
    Virtual<uint32_t>(part, segment::kRelease);
    return 0;
}

// The game's 0x005DC3A0, cdecl (segment, key, byte vector, largest size):
// reads a record of at most that size into the vector, resized to the
// record's size; true when the whole record was read.
uint8_t __cdecl ReadRecordToVector(void* part, ResourceKey* key, void* bytes, uint32_t largest)
{
    uint32_t size = Virtual<uint32_t>(part, segment::kGetRecordSize, key);
    if (size > largest)
        return 0;
    uint8_t fill = 0;
    This<void>(addr::SC4ByteVector_Resize2, bytes, size, &fill);
    void* const buffer = At<void*>(bytes, 0);
    return Virtual<uint32_t>(part, segment::kReadRecord, key, buffer, &size) == size ? 1 : 0;
}

// The game's 0x005DC400, cdecl (directory, city name, serial, string out):
// out = the directory joined with 'City - <name>.sc4' (double spaces made
// single). While a file of that name is there and it is not this city's (a
// serial is given and the file does not load as a regional city, or loads
// with another serial that is not 0), ' (2)', ' (3)' and so on is put before
// the last full stop of the path. `out` has to be the game's own string
// class: its body at +4 is used directly.
void __cdecl MakeFilePath(void* directory, void* cityName, uint32_t serial, void* out)
{
    static const char kPrefix[] = "City - ";
    char tag = 0; // the allocator argument of the string helpers (not set in the original, never read)
    uint32_t number = 1;

    GameString name;
    InitString(name);
    GameString folder;
    InitFromString(folder, directory, &tag);
    GameString city;
    InitFromString(city, cityName, &tag);
    GameString prefix;
    prefix.body.begin = nullptr;
    prefix.body.end = nullptr;
    prefix.body.capacity = nullptr;
    This<void>(addr::SC4String_FromRange, &prefix.body, kPrefix, kPrefix + 7, &tag);
    prefix.vtable = addr::SC4String_Vtable;
    prefix.zero = 0;

    // 'City - ' + name, then + '.sc4'; each helper fills a body we own.
    uint32_t joinedRoom[5];
    StringBody* const joined = reinterpret_cast<StringBody*>(joinedRoom);
    StringBody* const first = Cdecl<StringBody*>(addr::cSC4Advice_F408AE0, joined, &prefix.body, &city.body);
    StringBody withExtension;
    StringBody* const second = Cdecl<StringBody*>(addr::SC4String_ConcatBodyText, &withExtension, first,
                                                  Global<const char*>(addr::SC4_CityFileExtension2));
    StringBody copy;
    copy.begin = nullptr;
    copy.end = nullptr;
    copy.capacity = nullptr;
    This<void>(addr::cSC4Advice_F408420, &copy, second->begin, second->end, &tag);
    char* const copied = copy.begin;
    This<void>(addr::SC4String_AssignRange, &name.body, copied, copy.end, &tag);
    if (copied)
        Free(copied);
    FreeBody(withExtension);
    FreeBody(*joined);
    FreeBody(prefix.body);
    FreeBody(city.body);

    Cdecl<void>(addr::SC4String_CollapseSpaces, &name, &name);
    Cdecl<uint8_t>(addr::RZFile_CombinePath, out, &name, &folder);

    // The path without a number, and where the number goes.
    StringBody base;
    This<void>(addr::SC4String_CopyBuffer, &base,
               out ? reinterpret_cast<StringBody*>(static_cast<char*>(out) + 0x4) : nullptr, uint32_t{0},
               uint32_t{0xFFFFFFFF}, &tag);
    const uint32_t dot = Virtual<uint32_t>(out, text::kRFind, ".", uint32_t{0xFFFFFFFF}, uint32_t{1});

    if (Cdecl<uint8_t>(addr::RZFile_Exists, out)) {
        do {
            if (serial != 0) {
                void* found = nullptr;
                if (LoadFromFile(out, &found)) {
                    if (Virtual<uint32_t>(found, 0x6C) == serial || Virtual<uint32_t>(found, 0x6C) == 0) {
                        Virtual<uint8_t>(found, 0x10); // Shutdown
                        Virtual<uint32_t>(found, 0x8);
                        break;
                    }
                    Virtual<uint8_t>(found, 0x10);
                    Virtual<uint32_t>(found, 0x8);
                }
            }
            StringBody* const target = reinterpret_cast<StringBody*>(static_cast<char*>(out) + 0x4);
            if (&base != target)
                This<void>(addr::SC4String_AssignRange, target, base.begin, base.end, &tag);
            GameString suffix;
            suffix.body.begin = nullptr;
            suffix.body.end = nullptr;
            suffix.body.capacity = nullptr;
            char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
            ++number;
            suffix.body.begin = buffer;
            suffix.body.end = buffer;
            suffix.body.capacity = buffer + 8;
            *buffer = 0;
            suffix.vtable = addr::SC4String_Vtable;
            suffix.zero = 0;
            Cdecl<int32_t>(addr::cRZBaseString_Sprintf, static_cast<void*>(&suffix), " (%u)", number);
            Virtual<void*>(out, text::kInsert, dot, static_cast<void*>(&suffix));
            FreeBody(suffix.body);
        } while (Cdecl<uint8_t>(addr::RZFile_Exists, out));
    }

    FreeBody(base);
    FreeBody(folder.body);
    FreeBody(name.body);
}

// The game's 0x005DC790, cdecl (city name, serial, string out): the path for
// a city's file in the folder of the nation's current region: the regions
// folder (nation slot 5) joined with the region's folder name (region slot
// 5), then MakeFilePath.
void __cdecl MakeSavePath(void* cityName, uint32_t serial, void* out)
{
    char tag = 0;
    void* const nation = Virtual<void*>(Global<void*>(addr::SC4_Service43C94), 0x88);
    void* const region = Virtual<void*>(nation, 0x20);
    GameString path;
    InitFromString(path, Virtual<void*>(nation, 0x14), &tag);
    GameString regionFolder;
    InitFromString(regionFolder, Virtual<void*>(region, 0x14), &tag);
    Cdecl<uint8_t>(addr::RZFile_CombinePath, static_cast<void*>(&path), static_cast<void*>(&regionFolder),
                   static_cast<void*>(&path));
    MakeFilePath(&path, cityName, serial, out);
    FreeBody(regionFolder.body);
    FreeBody(path.body);
}

// The game's 0x005DC8D0, the focus watcher's message handler (slot 3,
// thiscall, ret 4): on message 0x4348B111 notes whether the application has
// the focus (the message's first data word) and, when it has not, that it
// lost it. Always answers true.
uint8_t __fastcall WatcherDoMessage(char* self, void*, void* message)
{
    if (Virtual<uint32_t>(message, 0x10) == kFocusMessage) {
        const uint8_t focused = Virtual<uint32_t>(message, 0x24) != 0 ? 1 : 0;
        At<uint8_t>(self, watcher::kFocused) = focused;
        if (!focused)
            At<uint8_t>(self, watcher::kLostFocus) = 1;
    }
    return 1;
}

// The game's 0x005DC910, cdecl (path, buffer**): loads the picture file at
// path into a new buffer through the game's image reader (format 9).
uint8_t __cdecl LoadPictureFile(void* path, void** buffer)
{
    alignas(4) char reader[0x38];
    This<void*>(addr::cGZImageReader_Construct, reader);
    const uint8_t result = This<uint8_t>(addr::cGZImageReader_LoadToNewBuffer, reader, path, buffer, uint32_t{9});
    This<void>(addr::cGZImageReader_Destruct, reader);
    return result;
}

// The game's 0x005DC9F0, the focus watcher's Release (slot 2): a count above
// one is lowered and returned; otherwise the object is freed (the count is
// left as it is) and 0 returned.
uint32_t __fastcall WatcherRelease(char* self, void*)
{
    uint32_t count = At<uint32_t>(self, watcher::kCount);
    if (count > 1) {
        --count;
        At<uint32_t>(self, watcher::kCount) = count;
        return count;
    }
    At<std::uintptr_t>(self, 0) = addr::SC4FocusWatcher_Vtable;
    Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return 0;
}

bool InstallRegionFiles1(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[20] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4CityFiles_CloseSegment, reinterpret_cast<void*>(&CloseSegment)},
        {addr::SC4Resource_CopyRecordToFile, reinterpret_cast<void*>(&CopyRecordToFile)},
        {addr::SC4_NewCitySerial, reinterpret_cast<void*>(&NewCitySerial)},
        {addr::SC4Resource_WriteToFile, reinterpret_cast<void*>(&WriteToFile)},
        {addr::SC4Resource_LoadText, reinterpret_cast<void*>(&LoadText)},
        {addr::SC4Resource_SaveText, reinterpret_cast<void*>(&SaveText)},
        {addr::SC4CityFiles_GetVersion, reinterpret_cast<void*>(&GetVersion)},
        {addr::SC4CityFiles_SetVersion, reinterpret_cast<void*>(&SetVersion)},
        {addr::SC4CityFiles_OpenSegment, reinterpret_cast<void*>(&OpenSegment)},
        {addr::SC4RegionalCity_ReadFromSegment, reinterpret_cast<void*>(&ReadCity)},
        {addr::SC4RegionalCity_WriteToSegment, reinterpret_cast<void*>(&WriteCity)},
        {addr::SC4RegionalCity_LoadFromFile, reinterpret_cast<void*>(&LoadFromFile)},
        {addr::SC4RegionalCity_SaveToFile, reinterpret_cast<void*>(&SaveToFile)},
        {addr::SC4CityFiles_ReadRecordToVector, reinterpret_cast<void*>(&ReadRecordToVector)},
        {addr::SC4RegionalCity_MakeFilePath, reinterpret_cast<void*>(&MakeFilePath)},
        {addr::SC4RegionalCity_MakeSavePath, reinterpret_cast<void*>(&MakeSavePath)},
        {addr::SC4FocusWatcher_DoMessage, reinterpret_cast<void*>(&WatcherDoMessage)},
        {addr::SC4CityFiles_LoadPictureFile, reinterpret_cast<void*>(&LoadPictureFile)},
        {addr::SC4FocusWatcher_Release, reinterpret_cast<void*>(&WatcherRelease)},
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

const bool g_regionFiles1Registered = registry::Register({
    "region.files.part1",
    "region",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4RegionalCity_LoadFromFile,
    "Our city file helpers, part 1: opening, versions, text and city records, serials, file names and paths",
    false,
    InstallRegionFiles1,
});

} // namespace
} // namespace opensc4::region
