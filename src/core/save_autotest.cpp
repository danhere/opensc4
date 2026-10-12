// Tests of the city save code (docs/save-format.md). The record stream
// helpers (0x005D9430 to 0x005DBC10) are the library every serialisable
// class writes and reads its record with; a real save and load runs them
// with whatever the city holds, and this test feeds each of them fixed data
// through streams of its own, so that the bytes a writer produces and what a
// reader leaves in memory can be compared exactly between the game's code
// and ours.
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <cIGZIStream.h>
#include <cIGZOStream.h>
#include <cIGZString.h>
#include <cISC4City.h>
#include <cRZBaseString.h>

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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

// An output stream that keeps the bytes written to it. It answers no
// interface, so the helpers that need a save file's segment stream fail
// their query on it.
class ByteOStream final : public cIGZOStream {
public:
    bool QueryInterface(uint32_t, void**) override { return false; }
    uint32_t AddRef() override { return 1; }
    uint32_t Release() override { return 1; }
    void Flush() override {}
    bool SetSint8(int8_t v) override { return Put(v); }
    bool SetUint8(uint8_t v) override { return Put(v); }
    bool SetSint16(int16_t v) override { return Put(v); }
    bool SetUint16(uint16_t v) override { return Put(v); }
    bool SetSint32(int32_t v) override { return Put(v); }
    bool SetUint32(uint32_t v) override { return Put(v); }
    bool SetSint64(int64_t v) override { return Put(v); }
    bool SetUint64(uint64_t v) override { return Put(v); }
    bool SetFloat32(float v) override { return Put(v); }
    bool SetFloat64(double v) override { return Put(v); }
    bool SetRZCharStr(char const* text) override { return SetVoid(text, static_cast<uint32_t>(std::strlen(text))); }
    bool SetGZStr(cIGZString const& text) override
    {
        const uint32_t length = text.Strlen();
        Put(length);
        return SetVoid(text.ToChar(), length);
    }
    bool SetGZSerializable(cIGZSerializable const&) override { return false; }
    bool SetVoid(void const* data, uint32_t size) override
    {
        const uint8_t* const from = static_cast<const uint8_t*>(data);
        bytes.insert(bytes.end(), from, from + size);
        return true;
    }
    int32_t GetError() override { return 0; }
    int32_t SetUserData(cIGZVariant*) override { return 0; }
    int32_t GetUserData() override { return 0; }

    std::vector<uint8_t> bytes;

private:
    template <typename T>
    bool Put(const T& value)
    {
        return SetVoid(&value, sizeof value);
    }
};

// An input stream over bytes. A read past the end stores nothing, fails and
// leaves the stream in error.
class ByteIStream final : public cIGZIStream {
public:
    explicit ByteIStream(std::vector<uint8_t> data) : bytes(std::move(data)) {}
    bool QueryInterface(uint32_t, void**) override { return false; }
    uint32_t AddRef() override { return 1; }
    uint32_t Release() override { return 1; }
    bool Skip(uint32_t count) override
    {
        if (bytes.size() - at < count)
            return Fail();
        at += count;
        return true;
    }
    bool GetSint8(int8_t& v) override { return Get(v); }
    bool GetUint8(uint8_t& v) override { return Get(v); }
    bool GetSint16(int16_t& v) override { return Get(v); }
    bool GetUint16(uint16_t& v) override { return Get(v); }
    bool GetSint32(int32_t& v) override { return Get(v); }
    bool GetUint32(uint32_t& v) override { return Get(v); }
    bool GetSint64(int64_t& v) override { return Get(v); }
    bool GetUint64(uint64_t& v) override { return Get(v); }
    bool GetFloat32(float& v) override { return GetVoid(&v, sizeof v); }
    bool GetFloat64(double& v) override { return GetVoid(&v, sizeof v); }
    bool GetRZCharStr(char*, uint32_t) override { return Fail(); }
    bool GetGZStr(cIGZString&) override { return Fail(); }
    bool GetGZSerializable(cIGZSerializable&) override { return Fail(); }
    bool GetVoid(void* out, uint32_t size) override
    {
        if (bytes.size() - at < size)
            return Fail();
        std::memcpy(out, bytes.data() + at, size);
        at += size;
        return true;
    }
    int32_t GetError() override { return error; }
    int32_t SetUserData(cIGZVariant*) override { return 0; }
    int32_t GetUserData() override { return 0; }

    std::vector<uint8_t> bytes;
    std::size_t at = 0;
    int32_t error = 0;

private:
    bool Fail()
    {
        error = 1;
        return false;
    }
    template <typename T>
    bool Get(T& value)
    {
        return GetVoid(&value, sizeof value);
    }
};

using StreamFn = void*(__cdecl*)(void* stream, void* value);

void Write(std::uintptr_t writer, ByteOStream& out, const void* value)
{
    reinterpret_cast<StreamFn>(writer)(&out, const_cast<void*>(value));
}
void Read(std::uintptr_t reader, ByteIStream& in, void* value)
{
    reinterpret_cast<StreamFn>(reader)(&in, value);
}

// The game's vector: begin, end, end of storage.
struct GameVector {
    char* begin;
    char* end;
    char* capacity;
};
void Free(void* memory)
{
    if (memory)
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(memory);
}

struct StreamTest {
    Fnv f;
    int steps = 0;

    // A plain value of `size` bytes: the writer's bytes, what the reader
    // makes of them in memory filled with 0xCC before, and what it leaves
    // when the stream ends after `shortBy` bytes less (0: not tried).
    void Plain(std::uintptr_t writer, std::uintptr_t reader, const void* value, std::size_t size, std::size_t shortBy = 0)
    {
        ByteOStream out;
        Write(writer, out, value);
        f.Add(static_cast<uint32_t>(out.bytes.size()));
        f.Bytes(out.bytes.data(), out.bytes.size());
        uint8_t back[0x40];
        for (int pass = 0; pass < (shortBy ? 2 : 1); ++pass) {
            std::vector<uint8_t> bytes = out.bytes;
            if (pass)
                bytes.resize(bytes.size() - shortBy);
            ByteIStream in(std::move(bytes));
            std::memset(back, 0xCC, sizeof back);
            Read(reader, in, &back[0]);
            f.Bytes(&back[0], size);
            f.Add(static_cast<uint32_t>(in.at));
            f.Add(in.error);
        }
        ++steps;
    }

    // A vector of plain elements: the writer's bytes, then read back into an
    // empty vector, with the stream cut short by `shortBy` bytes on a second
    // pass, and with a count over the reader's limit.
    void Vector(std::uintptr_t writer, std::uintptr_t reader, const void* values, std::size_t count, std::size_t each,
                std::size_t shortBy)
    {
        GameVector from{const_cast<char*>(static_cast<const char*>(values)),
                        const_cast<char*>(static_cast<const char*>(values)) + count * each,
                        const_cast<char*>(static_cast<const char*>(values)) + count * each};
        ByteOStream out;
        Write(writer, out, &from);
        f.Add(static_cast<uint32_t>(out.bytes.size()));
        f.Bytes(out.bytes.data(), out.bytes.size());
        for (int pass = 0; pass < 3; ++pass) {
            std::vector<uint8_t> bytes = out.bytes;
            if (pass == 1)
                bytes.resize(bytes.size() - shortBy);
            if (pass == 2) {
                const uint32_t tooMany = 2000000;
                std::memcpy(bytes.data(), &tooMany, 4);
            }
            ByteIStream in(std::move(bytes));
            GameVector to{};
            Read(reader, in, &to);
            f.Add(static_cast<uint32_t>(to.end - to.begin));
            f.Bytes(to.begin, static_cast<std::size_t>(to.end - to.begin));
            f.Add(static_cast<uint32_t>(in.at));
            Free(to.begin);
        }
        ++steps;
    }
};

std::string g_streamsToken;

// OPENSC4_TEST_SAVE_STREAMS=<day>: on that run day every record stream
// helper that works on a plain stream is given fixed data: bool, rectangle,
// resource key, random generator state, the two iterators, vectors of 2 and
// 3 floats, box, the 3 by 3 and 4 by 4 matrices, the transform with each
// combination of its three parts (read into an identity and into a full
// transform) and in its old form, the tract walk structure, vectors of
// floats, 32-bit values and bytes (whole, cut short and with a count over
// the limit), byte rows and bit rows, and the reference helpers, which must
// refuse a stream that is not a save file's. The token is a checksum of
// every byte written and of the memory every reader leaves.
void DriveStreams(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_SAVE_STREAMS") || !game::IsBuild641())
        return;
    StreamTest t;

    // Bool: the writer takes the value itself.
    for (const uint32_t value : {0u, 1u, 0x100u, 0xFFu}) {
        ByteOStream out;
        reinterpret_cast<void*(__cdecl*)(void*, uint32_t)>(addr::SC4Stream_WriteBool)(&out, value);
        t.f.Bytes(out.bytes.data(), out.bytes.size());
        ByteIStream in(out.bytes);
        uint8_t back = 0xCC;
        Read(addr::SC4Stream_ReadBool, in, &back);
        t.f.Add(back);
        ++t.steps;
    }

    const int32_t rect[4] = {-3, 7, 1000000, -2000000};
    t.Plain(addr::SC4Stream_WriteRect, addr::SC4Stream_ReadRect, &rect[0], sizeof rect, 6);
    const uint32_t key[3] = {0x6534284A, 0xBADB57F1, 0x00000123};
    t.Plain(addr::SC4Stream_WriteResourceKey, addr::SC4Stream_ReadResourceKey, &key[0], sizeof key, 5);

    uint8_t pattern[0x40];
    for (std::size_t i = 0; i < sizeof pattern; ++i)
        pattern[i] = static_cast<uint8_t>(0x11 + i * 7);
    t.Plain(addr::SC4Stream_WriteRandom, addr::SC4Stream_ReadRandom, &pattern[0], 0x18);
    t.Plain(addr::SC4Stream_WriteFlaggedIterator, addr::SC4Stream_ReadFlaggedIterator, &pattern[0], 0x20, 9);
    t.Plain(addr::SC4Stream_WriteCellIterator, addr::SC4Stream_ReadCellIterator, &pattern[0], 0x20, 9);

    float floats[16];
    for (int i = 0; i < 16; ++i)
        floats[i] = static_cast<float>(i) * 1.5f - 7.25f;
    t.Plain(addr::SC4Stream_WriteVector2, addr::SC4Stream_ReadVector2, &floats[0], 8, 3);
    t.Plain(addr::SC4Stream_WriteVector3, addr::SC4Stream_ReadVector3, &floats[0], 12, 5);
    t.Plain(addr::SC4Stream_WriteBox, addr::SC4Stream_ReadBox, &floats[0], 0x18, 10);
    t.Plain(addr::SC4Save_F5DA680, addr::SC4Save_F5DA650, &floats[0], 0x24); // 3 by 3
    t.Plain(addr::SC4Save_F5DA6E0, addr::SC4Save_F5DA6B0, &floats[0], 0x40); // 4 by 4

    // The transform (0x38 bytes): +0 a change counter, +1 flags (1 a
    // translation, 2 a scale, 4 a rotation), +4 nine floats, +0x28 three,
    // +0x34 the scale.
    for (uint8_t flags = 0; flags < 8; ++flags) {
        uint8_t transform[0x38] = {};
        transform[1] = flags;
        std::memcpy(&transform[4], &floats[0], 0x24);
        std::memcpy(&transform[0x28], &floats[9], 0xC);
        std::memcpy(&transform[0x34], &floats[12], 4);
        ByteOStream out;
        Write(addr::SC4Stream_WriteTransform, out, &transform[0]);
        t.f.Add(static_cast<uint32_t>(out.bytes.size()));
        t.f.Bytes(out.bytes.data(), out.bytes.size());
        for (const uint8_t before : {uint8_t{0}, uint8_t{7}}) {
            uint8_t back[0x38] = {};
            back[1] = before;
            for (int i = 0; i < 13; ++i) {
                const float value = before ? static_cast<float>(i) + 0.5f : (i == 0 || i == 4 || i == 8 || i == 12 ? 1.0f : 0.0f);
                std::memcpy(&back[4 + i * 4], &value, 4);
            }
            ByteIStream in(out.bytes);
            Read(addr::SC4Stream_ReadTransform, in, &back[0]);
            t.f.Bytes(&back[0], sizeof back);
            t.f.Add(static_cast<uint32_t>(in.at));
        }
        ByteOStream old;
        Write(addr::SC4Save_F5DAA80, old, &transform[0]); // as a matrix, the old form
        t.f.Add(static_cast<uint32_t>(old.bytes.size()));
        t.f.Bytes(old.bytes.data(), old.bytes.size());
        uint8_t back[0x38];
        std::memset(back, 0xCC, sizeof back);
        ByteIStream in(old.bytes);
        Read(addr::SC4Stream_ReadMatrixAsTransform, in, &back[0]);
        t.f.Bytes(&back[0], sizeof back);
        ++t.steps;
    }

    // The tract walk structure (0x30 bytes, a vector of 16-bit values at
    // +0x18).
    {
        uint16_t values[5] = {1, 0xFFFF, 300, 0, 0x8000};
        uint8_t walk[0x30];
        std::memcpy(walk, pattern, sizeof walk);
        GameVector inside{reinterpret_cast<char*>(&values[0]), reinterpret_cast<char*>(&values[5]),
                          reinterpret_cast<char*>(&values[5])};
        std::memcpy(&walk[0x18], &inside, sizeof inside);
        ByteOStream out;
        Write(addr::SC4Stream_WritePollutionStruct, out, &walk[0]);
        t.f.Add(static_cast<uint32_t>(out.bytes.size()));
        t.f.Bytes(out.bytes.data(), out.bytes.size());
        uint8_t back[0x30];
        std::memset(back, 0xCC, sizeof back);
        std::memset(&back[0x18], 0, sizeof(GameVector));
        ByteIStream in(out.bytes);
        Read(addr::SC4Stream_ReadPollutionStruct, in, &back[0]);
        GameVector read;
        std::memcpy(&read, &back[0x18], sizeof read);
        t.f.Bytes(&back[0], 0x18);
        t.f.Bytes(&back[0x24], 0xC);
        t.f.Add(static_cast<uint32_t>(read.end - read.begin));
        t.f.Bytes(read.begin, static_cast<std::size_t>(read.end - read.begin));
        Free(read.begin);
        ++t.steps;
    }

    const uint32_t numbers[6] = {0, 1, 0xFFFFFFFF, 0x80000000, 12345, 7};
    t.Vector(addr::SC4Save_F5DAD50, addr::SC4Save_F5DB4B0, &floats[0], 7, 4, 10);
    t.Vector(addr::SC4Save_F5DAEA0, addr::SC4Save_F5DB350, &numbers[0], 6, 4, 6);
    {
        // The byte vector's reader takes its count's starting value from
        // ECX (docs/save-format.md), which a call from here does not set:
        // whole streams only.
        GameVector from{reinterpret_cast<char*>(&pattern[0]), reinterpret_cast<char*>(&pattern[33]),
                        reinterpret_cast<char*>(&pattern[33])};
        ByteOStream out;
        Write(addr::SC4Stream_WriteByteVector, out, &from);
        t.f.Bytes(out.bytes.data(), out.bytes.size());
        ByteIStream in(out.bytes);
        GameVector to{};
        Read(addr::SC4Stream_ReadByteVector, in, &to);
        t.f.Add(static_cast<uint32_t>(to.end - to.begin));
        t.f.Bytes(to.begin, static_cast<std::size_t>(to.end - to.begin));
        Free(to.begin);
        ++t.steps;
    }

    // Byte rows, five rows of five (the writer writes as many bytes of a
    // row as there are rows), and bit rows, six of six, made by the reader
    // from bytes and written back.
    {
        GameVector rows[5];
        for (int row = 0; row < 5; ++row)
            rows[row] = {reinterpret_cast<char*>(&pattern[row * 5]), reinterpret_cast<char*>(&pattern[row * 5 + 5]),
                         reinterpret_cast<char*>(&pattern[row * 5 + 5])};
        GameVector outer{reinterpret_cast<char*>(&rows[0]), reinterpret_cast<char*>(&rows[5]), reinterpret_cast<char*>(&rows[5])};
        ByteOStream out;
        Write(addr::SC4Stream_WriteByteRows, out, &outer);
        t.f.Add(static_cast<uint32_t>(out.bytes.size()));
        t.f.Bytes(out.bytes.data(), out.bytes.size());
        ByteIStream in(out.bytes);
        GameVector back{};
        Read(addr::SC4Stream_ReadByteRows, in, &back);
        t.f.Add(static_cast<uint32_t>(back.end - back.begin));
        for (char* row = back.begin; row != back.end; row += sizeof(GameVector)) {
            GameVector* const r = reinterpret_cast<GameVector*>(row);
            t.f.Add(static_cast<uint32_t>(r->end - r->begin));
            t.f.Bytes(r->begin, static_cast<std::size_t>(r->end - r->begin));
            Free(r->begin);
        }
        Free(back.begin);
        ++t.steps;

        std::vector<uint8_t> bits(8 + 36);
        const uint32_t six = 6;
        std::memcpy(&bits[0], &six, 4);
        std::memcpy(&bits[4], &six, 4);
        for (std::size_t i = 0; i < 36; ++i)
            bits[8 + i] = static_cast<uint8_t>((i * 7 + i / 5) % 3 == 0);
        ByteIStream bitsIn(bits);
        GameVector bitRows{};
        Read(addr::SC4Stream_ReadBitRows, bitsIn, &bitRows);
        t.f.Add(static_cast<uint32_t>(bitRows.end - bitRows.begin));
        t.f.Add(static_cast<uint32_t>(bitsIn.at));
        ByteOStream bitsOut;
        Write(addr::SC4Stream_WriteBitRows, bitsOut, &bitRows);
        t.f.Add(static_cast<uint32_t>(bitsOut.bytes.size()));
        t.f.Bytes(bitsOut.bytes.data(), bitsOut.bytes.size());
        t.f.Add(static_cast<uint8_t>(bitsOut.bytes == bits));
        reinterpret_cast<void(__thiscall*)(void*)>(addr::SC4BitRowVector_Destroy)(&bitRows);
        ++t.steps;
    }

    // The reference helpers on a stream that is not a save file's.
    {
        ByteOStream out;
        ByteIStream in(std::vector<uint8_t>(16, 0x55));
        void* object = reinterpret_cast<void*>(std::uintptr_t{0x1234});
        using ReadRefFn = uint32_t(__cdecl*)(void*, uint32_t, void**, uint32_t);
        using WriteRefFn = uint32_t(__cdecl*)(void*, void*);
        t.f.Add(static_cast<uint8_t>(reinterpret_cast<ReadRefFn>(addr::SC4Stream_ReadReference)(&in, 0x26BB767E, &object, 0)));
        t.f.Add(object == nullptr);
        t.f.Add(static_cast<uint8_t>(reinterpret_cast<WriteRefFn>(addr::SC4Stream_WriteReference)(&out, nullptr)));
        t.f.Add(static_cast<uint32_t>(in.at));
        t.f.Add(static_cast<uint32_t>(out.bytes.size()));
        ++t.steps;
    }

    char text[64];
    std::snprintf(text, sizeof text, " | svstreams %d %08X", t.steps, t.f.h);
    g_streamsToken = text;
    log::Write("autotest: save streams driven:%s", text);
}

// OPENSC4_TEST_SAVE_BLOCK=<day>: on that run day something is put in the way
// of the next save, to reach a row of SaveCity's file moves that a normal
// save never takes (docs/save-format.md, "The file moves"): a folder with a
// file in it where the backup goes (the city's file name with an
// underscore), so the old backup cannot be removed. The save must fail and
// the old file must stay; the run's own end reports the save's result. The
// folder stays in the test's region afterwards: remove it by hand.
void BlockSave(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_SAVE_BLOCK") || !game::IsBuild641())
        return;
    cRZBaseString path;
    day.city->GetCitySaveFilePath(path);
    const std::string file = path.ToChar() ? path.ToChar() : "";
    if (file.empty()) {
        log::Write("autotest: save block: the city has no file");
        return;
    }
    const std::string folder = file + "_";
    const bool made = CreateDirectoryA(folder.c_str(), nullptr) != 0 || GetLastError() == ERROR_ALREADY_EXISTS;
    const HANDLE inside = CreateFileA((folder + "\\inside.txt").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (inside != INVALID_HANDLE_VALUE)
        CloseHandle(inside);
    log::Write("autotest: save block %s for %s", made ? "set" : "NOT set", file.c_str());
}

std::string g_fileToken;

// A whole file from disk (up to 16 MB), read by the test itself; empty when
// it cannot be read.
std::vector<uint8_t> ReadWholeFile(const char* name)
{
    std::vector<uint8_t> file;
    const HANDLE handle = CreateFileA(name, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (handle != INVALID_HANDLE_VALUE) {
        const DWORD size = GetFileSize(handle, nullptr);
        if (size != INVALID_FILE_SIZE && size <= 16u * 1024 * 1024) {
            file.resize(size);
            DWORD got = 0;
            if (!ReadFile(handle, file.data(), size, &got, nullptr) || got != size)
                file.clear();
        }
        CloseHandle(handle);
    }
    return file;
}

// Fixed bytes for a test record: `runs` in a row of one value, then one
// value from a small generator, over and over. A high `runs` compresses
// well, 0 does not compress at all.
std::vector<uint8_t> TestBytes(std::size_t size, uint32_t seed, unsigned runs)
{
    std::vector<uint8_t> bytes(size);
    uint32_t state = seed;
    std::size_t at = 0;
    while (at < size) {
        state = state * 1664525u + 1013904223u;
        const uint8_t value = static_cast<uint8_t>(state >> 24);
        for (unsigned i = 0; i <= runs && at < size; ++i)
            bytes[at++] = value;
    }
    return bytes;
}

// Bytes that compress to a chosen share: the first `noisy` of every `of`
// bytes come from the generator, the rest are zero. A ladder of these
// brackets the writer's rule that a record is stored compressed only when
// that saves enough.
std::vector<uint8_t> LadderBytes(std::size_t size, uint32_t seed, unsigned noisy, unsigned of = 100)
{
    std::vector<uint8_t> bytes(size);
    uint32_t state = seed;
    for (std::size_t at = 0; at < size; ++at) {
        state = state * 1664525u + 1013904223u;
        bytes[at] = at % of < noisy ? static_cast<uint8_t>(state >> 24) : uint8_t{0};
    }
    return bytes;
}

// OPENSC4_TEST_SAVE_FILE=<day>: on that run day a packed file is written
// through the game's own segment class from fixed records, and the finished
// file goes into the fingerprint byte for byte, so that the game's writer
// and ours can be compared exactly (a real save cannot be: its records hold
// addresses and frame-driven state). The records are chosen for the
// writer's branches (docs/save-format.md, "The file container"): one of 10
// bytes, and one each of 49 and 50, around the size below which nothing is
// compressed; one that compresses well; noise, which does not; a mixed one;
// one over the 250,000 bytes where the compressor changes mode; a ladder of
// sixteen from mostly zero to mostly noise, to have some on each side of
// the rule that decides whether the compressed form is kept, a finer one
// of twelve across the place where that changes, and four that land on it
// exactly, written once as the processor is set and once in single
// precision. Then the
// compressible record is deleted, which leaves a small hole; 100 bytes of
// noise are written, which must go into that hole and leave the rest of it
// a hole; the noise record is written again larger (its old place freed,
// the file lengthened) and the smallest again at its old size. The file is
// closed, read back from disk by the test, its two date fields (offsets 24
// and 28, from the clock) taken out, and summed whole and by its parts.
// Then the game opens it for reading and every record is read back. The
// file is made in the system's temporary folder and removed
// (OPENSC4_TEST_SAVE_FILE_KEEP=1 keeps it). Before the file is closed, four
// records are written piece by piece through the game's record writer, as
// an object's Write does it: typed values with one patched afterwards; a
// block and single bytes; a record opened for writing a second time; and
// one of 2,100,000 bytes, which moves into a temporary file on the way.
// A second file is written in three stages, opened for writing again each
// time: made, one record replaced, then most of it deleted so that the
// writer rebuilds it (see below). Not covered: a record of no bytes, one
// over 16,000,000, opening for writing a record that is already over
// 2,000,000 bytes, and copying or importing records between files.
void DriveFile(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_SAVE_FILE") || !game::IsBuild641())
        return;
    char folder[MAX_PATH] = {};
    if (!GetTempPathA(sizeof folder, folder)) {
        g_fileToken = " | svfile none";
        return;
    }
    char name[MAX_PATH + 64];
    std::snprintf(name, sizeof name, "%sopensc4-savefile-%lu.dat", folder, GetCurrentProcessId());
    DeleteFileA(name);

    struct Key {
        uint32_t type, group, instance;
    };
    constexpr std::size_t kRelease = 0x8, kShutdown = 0x14, kClose = 0x20, kTest = 0x4C, kDelete = 0x6C, kRead = 0x70,
                          kWrite = 0x74;
    using OpenFn = void*(__cdecl*)(void* path, uint32_t write, uint32_t checkVersion);
    char path[0x14];
    reinterpret_cast<void*(__thiscall*)(void*, const char*)>(addr::SC4String_FromText)(&path[0], name);
    const auto freePath = [&] {
        if (void* const buffer = At<void*>(&path[0], 0x4))
            reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(buffer);
    };

    Fnv answers;
    int steps = 0;
    void* part = reinterpret_cast<OpenFn>(addr::SC4CityFiles_OpenSegment)(&path[0], 1, 0);
    if (!part) {
        freePath();
        g_fileToken = " | svfile noopen";
        log::Write("autotest: save file: the game did not open %s for writing", name);
        return;
    }
    constexpr uint32_t kGroup = 0x7E57F11E;
    const Key small{0x7E570001, kGroup, 1}, good{0x7E570002, kGroup, 2}, raw{0x7E570003, kGroup, 3},
        mixed{0x7E570004, kGroup, 4}, large{0x7E570005, kGroup, 5}, filler{0x7E570006, kGroup, 6},
        under{0x7E570007, kGroup, 7}, floor{0x7E570008, kGroup, 8};
    const auto write = [&](const Key& key, const std::vector<uint8_t>& bytes) {
        Key copy = key;
        answers.Add(Virtual<uint8_t>(part, kWrite, &copy, static_cast<const void*>(bytes.data()),
                                     static_cast<uint32_t>(bytes.size())));
        ++steps;
    };
    const std::vector<uint8_t> smallBytes = TestBytes(10, 1, 0), goodBytes = TestBytes(4000, 2, 40),
                               rawBytes = TestBytes(4000, 3, 0), mixedBytes = TestBytes(60000, 4, 3),
                               largeBytes = TestBytes(300000, 5, 12), fillerBytes = TestBytes(100, 6, 0),
                               rawAgain = TestBytes(9000, 7, 0);
    write(small, smallBytes);
    write(good, goodBytes);
    write(raw, rawBytes);
    write(mixed, mixedBytes);
    write(large, largeBytes);
    // Each side of the smallest size that is compressed at all: 49 bytes
    // and 50, both of one value.
    write(under, std::vector<uint8_t>(49, 0x41));
    write(floor, std::vector<uint8_t>(50, 0x41));
    // The ladder: sixteen records of 3,000 bytes from mostly zero to mostly
    // noise, so that some are stored compressed and some are not.
    for (uint32_t step = 0; step < 16; ++step) {
        const Key rung{0x7E571000 + step, kGroup, 100 + step};
        write(rung, LadderBytes(3000, 100 + step, 50 + step * 3));
    }
    // A finer ladder across the share at which the compressed form stops
    // being kept: twelve records of 8,000 bytes, 770 to 803 noisy bytes in
    // every 1,000, which the game's compressor takes to 78 to 82 percent.
    for (uint32_t step = 0; step < 12; ++step) {
        const Key rung{0x7E572000 + step, kGroup, 200 + step};
        write(rung, LadderBytes(8000, 200 + step, 770 + step * 3, 1000));
    }
    // Four records that the game's compressor takes to exactly four fifths
    // of their size (5,000 bytes to 4,000; found offline with the encoder
    // checked against the game's own files). The rule's comparison is made
    // in floating point against a single-precision 0.8, so what happens on
    // the line itself depends on the precision the processor is set to.
    for (uint32_t step = 0; step < 4; ++step) {
        static const uint32_t kSeed[4] = {20, 24, 26, 34};
        const Key edge{0x7E573000 + step, kGroup, 300 + step};
        write(edge, step < 3 ? LadderBytes(5000, kSeed[step], 76) : LadderBytes(5000, kSeed[step], 787, 1000));
    }
    // The same four again with the processor set to single precision for
    // the length of the calls, through the game's own switch (0x008929E0;
    // 0x00892A0A reads the setting back from the processor): there the
    // quotient is rounded to exactly the 0.8 it is compared with.
    {
        const int32_t before = reinterpret_cast<int32_t(__cdecl*)()>(addr::SC4Precision_Get)();
        reinterpret_cast<void(__cdecl*)(int32_t)>(addr::SC4Precision_Set)(0);
        answers.Add(reinterpret_cast<int32_t(__cdecl*)()>(addr::SC4Precision_Get)());
        for (uint32_t step = 0; step < 4; ++step) {
            static const uint32_t kSeed[4] = {20, 24, 26, 34};
            const Key edge{0x7E574000 + step, kGroup, 400 + step};
            write(edge, step < 3 ? LadderBytes(5000, kSeed[step], 76) : LadderBytes(5000, kSeed[step], 787, 1000));
        }
        reinterpret_cast<void(__cdecl*)(int32_t)>(addr::SC4Precision_Set)(before);
        answers.Add(before);
    }
    {
        // The compressible record goes: its place, a small one, is a hole.
        Key copy = good;
        answers.Add(Virtual<uint8_t>(part, kDelete, &copy));
        answers.Add(Virtual<uint8_t>(part, kTest, &copy));
        ++steps;
    }
    write(filler, fillerBytes); // 100 bytes that do not compress: into the hole, the rest of it a hole again
    write(raw, rawAgain);       // larger than before: the old place freed
    write(small, smallBytes);   // the same size again
    // Records written piece by piece through a stream, as every object's
    // Write does it: the game's record writer (a holder of 0x10 bytes:
    // vtable, segment, the serial record at +8, a version) opens the record
    // for writing and writes the version as 16 bits; then the record's own
    // typed sets, a seek back to patch a value written earlier and a seek to
    // the end again, as the serializer fills in an object's size and
    // checksum.
    constexpr std::size_t kSetUint8 = 0x74, kSetSint8 = 0x7C, kSetUint16 = 0x84, kSetSint16 = 0x8C, kSetUint32 = 0x94,
                          kSetSint32 = 0x9C, kSetUint64 = 0xA4, kSetSint64 = 0xAC, kSetFloat32 = 0xB4, kSetFloat64 = 0xBC,
                          kSetVoid = 0xCC, kGetSize = 0xD0, kSeekAbsolute = 0xDC;
    using WriterFn = char*(__thiscall*)(char* holder, void* segment, uint32_t type, uint32_t group, uint32_t instance,
                                        uint32_t version);
    using CloseFn = void(__thiscall*)(char* holder);
    const auto stream = [&](const Key& key, uint32_t version, auto&& body) {
        char holder[0x10] = {};
        reinterpret_cast<WriterFn>(addr::SC4Save_F5D9B20)(&holder[0], part, key.type, key.group, key.instance, version);
        void* const record = At<void*>(&holder[0], 0x8);
        answers.Add(record != nullptr);
        if (record) {
            body(record);
            answers.Add(Virtual<uint32_t>(record, kGetSize));
        }
        reinterpret_cast<CloseFn>(addr::SC4Save_F5D9430)(&holder[0]);
        ++steps;
    };
    const Key typed{0x7E575001, kGroup, 501}, block{0x7E575002, kGroup, 502}, again{0x7E575003, kGroup, 503},
        spill{0x7E575004, kGroup, 504};
    const auto values = [&](void* record) {
        answers.Add(Virtual<uint8_t>(record, kSetUint8, uint32_t{0xA1}));
        answers.Add(Virtual<uint8_t>(record, kSetSint8, uint32_t{0xB2}));
        answers.Add(Virtual<uint8_t>(record, kSetUint16, uint32_t{0xC3D4}));
        answers.Add(Virtual<uint8_t>(record, kSetSint16, uint32_t{0xE5F6}));
        answers.Add(Virtual<uint8_t>(record, kSetUint32, uint32_t{0x01020304}));
        answers.Add(Virtual<uint8_t>(record, kSetSint32, uint32_t{0x85868788}));
        answers.Add(Virtual<uint8_t>(record, kSetUint64, uint64_t{0x1122334455667788ull}));
        answers.Add(Virtual<uint8_t>(record, kSetSint64, uint64_t{0x99AABBCCDDEEFF00ull}));
        answers.Add(Virtual<uint8_t>(record, kSetFloat32, uint32_t{0x40490FDB})); // the bits of a float
        answers.Add(Virtual<uint8_t>(record, kSetFloat64, 2.718281828459045));
    };
    // Under 50 bytes in all: stored as it is. A value in the middle is
    // written over afterwards.
    stream(typed, 7, [&](void* record) {
        values(record);
        const uint32_t end = Virtual<uint32_t>(record, kGetSize);
        answers.Add(Virtual<uint8_t>(record, kSeekAbsolute, uint32_t{4}));
        answers.Add(Virtual<uint8_t>(record, kSetUint32, uint32_t{0xCAFEF00D}));
        answers.Add(Virtual<uint8_t>(record, kSeekAbsolute, end));
        answers.Add(Virtual<uint8_t>(record, kSetUint8, uint32_t{0x5A}));
    });
    // A block in one call and then seventy single bytes (a small write at
    // the end of the record takes a path of its own): stored compressed.
    stream(block, 8, [&](void* record) {
        const std::vector<uint8_t> bytes = TestBytes(3000, 21, 9);
        answers.Add(Virtual<uint8_t>(record, kSetVoid, static_cast<const void*>(bytes.data()), uint32_t{3000}));
        for (uint32_t i = 0; i < 70; ++i)
            answers.Add(Virtual<uint8_t>(record, kSetUint8, uint32_t{0x30 + i % 5}));
    });
    // A record written, closed, opened for writing a second time (its old
    // bytes come back from the file) and written into again.
    stream(again, 9, values);
    stream(again, 10, [&](void* record) {
        const uint32_t end = Virtual<uint32_t>(record, kGetSize);
        answers.Add(end);
        answers.Add(Virtual<uint8_t>(record, kSeekAbsolute, end));
        const std::vector<uint8_t> bytes = TestBytes(400, 22, 5);
        answers.Add(Virtual<uint8_t>(record, kSetVoid, static_cast<const void*>(bytes.data()), uint32_t{400}));
    });
    // A record of 2,100,000 bytes, in pieces of 64,000 and a last one of
    // 52,000: past 2,000,000 bytes a record being written moves from memory
    // into a temporary file. Then the patch of an early value, which now
    // lands in that file, and one more byte at the end. A large city's
    // biggest classes are saved this way.
    stream(spill, 11, [&](void* record) {
        const std::vector<uint8_t> piece = TestBytes(64000, 23, 14);
        uint32_t left = 2100000;
        while (left) {
            const uint32_t now = left < 64000 ? left : 64000;
            answers.Add(Virtual<uint8_t>(record, kSetVoid, static_cast<const void*>(piece.data()), now));
            left -= now;
        }
        const uint32_t end = Virtual<uint32_t>(record, kGetSize);
        answers.Add(end);
        answers.Add(Virtual<uint8_t>(record, kSeekAbsolute, uint32_t{6}));
        answers.Add(Virtual<uint8_t>(record, kSetUint32, uint32_t{0xFEEDBEEF}));
        answers.Add(Virtual<uint8_t>(record, kSeekAbsolute, end));
        answers.Add(Virtual<uint8_t>(record, kSetUint8, uint32_t{0x77}));
    });

    answers.Add(Virtual<uint8_t>(part, kClose));
    Virtual<uint8_t>(part, kShutdown);
    Virtual<uint32_t>(part, kRelease);

    // The file as it is on disk.
    std::vector<uint8_t> file = ReadWholeFile(name);
    // Summed whole, and by the file's own structure: the header, the
    // records in index order (each with its place and size), the index
    // table, the hole table, and the bytes nothing owns (old contents of
    // freed places). `packed` counts the records the directory lists as
    // stored compressed.
    uint32_t dates[2] = {};
    uint32_t indexAt = 0, holes = 0, packed = 0, entries = 0;
    Fnv whole, header, records, tables, stale;
    if (file.size() >= 0x60) {
        std::memcpy(&dates[0], &file[24], sizeof dates);
        std::memset(&file[24], 0, sizeof dates);
        uint32_t fields[7]; // count, index offset, index size, hole count, hole offset, hole size, index minor
        std::memcpy(&fields[0], &file[36], sizeof fields);
        entries = fields[0];
        indexAt = fields[1];
        holes = fields[3];
        whole.Bytes(file.data(), file.size());
        header.Bytes(file.data(), 0x60);
        std::vector<uint8_t> owned(file.size(), 0);
        const auto own = [&](std::size_t from, std::size_t size) {
            for (std::size_t at = from; at < from + size && at < owned.size(); ++at)
                owned[at] = 1;
        };
        own(0, 0x60);
        const bool indexFits = fields[2] == entries * 20u && std::size_t{indexAt} + fields[2] <= file.size();
        if (indexFits) {
            tables.Bytes(file.data() + indexAt, fields[2]);
            own(indexAt, fields[2]);
            for (uint32_t i = 0; i < entries; ++i) {
                uint32_t entry[5]; // type, group, instance, offset, size
                std::memcpy(&entry[0], file.data() + indexAt + i * 20u, sizeof entry);
                if (std::size_t{entry[3]} + entry[4] > file.size())
                    continue;
                records.Bytes(&entry[0], sizeof entry);
                records.Bytes(file.data() + entry[3], entry[4]);
                own(entry[3], entry[4]);
                if (entry[0] == 0xE86B1EEF)
                    packed = entry[4] / 16;
            }
        }
        if (std::size_t{fields[4]} + fields[5] <= file.size()) {
            tables.Bytes(file.data() + fields[4], fields[5]);
            own(fields[4], fields[5]);
        }
        for (std::size_t at = 0; at < file.size(); ++at)
            if (!owned[at])
                stale.Add(file[at]);
    }

    // The game reads its own file back.
    Fnv back;
    int read = 0;
    part = reinterpret_cast<OpenFn>(addr::SC4CityFiles_OpenSegment)(&path[0], 0, 0);
    if (part) {
        std::vector<Key> keys = {small, good, raw, mixed, large, filler, under, floor};
        for (uint32_t step = 0; step < 16; ++step)
            keys.push_back(Key{0x7E571000 + step, kGroup, 100 + step});
        for (uint32_t step = 0; step < 12; ++step)
            keys.push_back(Key{0x7E572000 + step, kGroup, 200 + step});
        for (uint32_t step = 0; step < 4; ++step)
            keys.push_back(Key{0x7E573000 + step, kGroup, 300 + step});
        for (uint32_t step = 0; step < 4; ++step)
            keys.push_back(Key{0x7E574000 + step, kGroup, 400 + step});
        for (const Key& key : {typed, block, again, spill})
            keys.push_back(key);
        for (const Key& key : keys) {
            Key copy = key;
            std::vector<uint8_t> buffer(2200000);
            uint32_t size = static_cast<uint32_t>(buffer.size());
            const uint8_t there = Virtual<uint8_t>(part, kTest, &copy);
            back.Add(there);
            if (!there)
                continue;
            const int32_t answer = Virtual<int32_t>(part, kRead, &copy, static_cast<void*>(buffer.data()), &size);
            back.Add(static_cast<uint8_t>(answer != -1));
            back.Add(size);
            if (answer != -1 && size <= buffer.size()) {
                back.Bytes(buffer.data(), size);
                ++read;
            }
        }
        Virtual<uint8_t>(part, kClose);
        Virtual<uint8_t>(part, kShutdown);
        Virtual<uint32_t>(part, kRelease);
    }
    freePath();
    // OPENSC4_TEST_SAVE_FILE_KEEP=1 leaves the file for a look at it.
    if (EnvInt("OPENSC4_TEST_SAVE_FILE_KEEP", 0))
        log::Write("autotest: save file kept: %s", name);
    else
        DeleteFileA(name);

    // A second file, for what happens to a file that is opened for writing
    // again, as a neighbouring city's file is at every save. Stage 1: four
    // records, closed. Stage 2: opened again, one record written over with
    // other bytes of another size, closed (its old place is a hole; the
    // tables are written again). Stage 3: opened again, the two large
    // records deleted, closed: now the holes are many times the bytes still
    // in use, far over the share (15 percent) at which the writer rebuilds
    // the file through a temporary one. The file is read from disk after
    // each stage; `holes` is the hole count the last one's header gives,
    // which a rebuilt file has at 0.
    char second[MAX_PATH + 64];
    std::snprintf(second, sizeof second, "%sopensc4-savefile-%lu-b.dat", folder, GetCurrentProcessId());
    DeleteFileA(second);
    char secondPath[0x14];
    reinterpret_cast<void*(__thiscall*)(void*, const char*)>(addr::SC4String_FromText)(&secondPath[0], second);
    Fnv stages, stageAnswers;
    uint32_t stageSize[3] = {}, lastHoles = 0xFFFFFFFF, lastEntries = 0;
    int stagesDone = 0;
    {
        const Key big1{0x7E576001, kGroup, 601}, big2{0x7E576002, kGroup, 602}, kept{0x7E576003, kGroup, 603},
            changed{0x7E576004, kGroup, 604};
        for (int stage = 0; stage < 3; ++stage) {
            void* const again = reinterpret_cast<OpenFn>(addr::SC4CityFiles_OpenSegment)(&secondPath[0], 1, 0);
            stageAnswers.Add(again != nullptr);
            if (!again)
                break;
            const auto put = [&](const Key& key, const std::vector<uint8_t>& bytes) {
                Key copy = key;
                stageAnswers.Add(Virtual<uint8_t>(again, kWrite, &copy, static_cast<const void*>(bytes.data()),
                                                  static_cast<uint32_t>(bytes.size())));
            };
            if (stage == 0) {
                put(big1, TestBytes(40000, 31, 0));
                put(big2, TestBytes(40000, 32, 0));
                put(kept, TestBytes(3000, 33, 20));
                put(changed, TestBytes(500, 34, 0));
            } else if (stage == 1) {
                put(changed, TestBytes(900, 35, 0));
            } else {
                for (const Key& key : {big1, big2}) {
                    Key copy = key;
                    stageAnswers.Add(Virtual<uint8_t>(again, kDelete, &copy));
                }
            }
            stageAnswers.Add(Virtual<uint8_t>(again, kClose));
            Virtual<uint8_t>(again, kShutdown);
            Virtual<uint32_t>(again, kRelease);
            std::vector<uint8_t> bytes = ReadWholeFile(second);
            stageSize[stage] = static_cast<uint32_t>(bytes.size());
            if (bytes.size() >= 0x60) {
                std::memset(&bytes[24], 0, 8); // the two dates
                std::memcpy(&lastEntries, &bytes[36], 4);
                std::memcpy(&lastHoles, &bytes[48], 4);
            }
            stages.Add(stageSize[stage]);
            stages.Bytes(bytes.data(), bytes.size());
            ++stagesDone;
            if (EnvInt("OPENSC4_TEST_SAVE_FILE_KEEP", 0)) {
                char copyName[MAX_PATH + 80];
                std::snprintf(copyName, sizeof copyName, "%sopensc4-savefile-%lu-b%d.dat", folder, GetCurrentProcessId(),
                              stage + 1);
                CopyFileA(second, copyName, FALSE);
            }
        }
        // The game reads the last stage back.
        void* const reader = reinterpret_cast<OpenFn>(addr::SC4CityFiles_OpenSegment)(&secondPath[0], 0, 0);
        stageAnswers.Add(reader != nullptr);
        if (reader) {
            for (const Key& key : {big1, big2, kept, changed}) {
                Key copy = key;
                std::vector<uint8_t> buffer(50000);
                uint32_t size = static_cast<uint32_t>(buffer.size());
                const uint8_t there = Virtual<uint8_t>(reader, kTest, &copy);
                stageAnswers.Add(there);
                if (there && Virtual<int32_t>(reader, kRead, &copy, static_cast<void*>(buffer.data()), &size) != -1 &&
                    size <= buffer.size()) {
                    stageAnswers.Add(size);
                    stageAnswers.Bytes(buffer.data(), size);
                }
            }
            Virtual<uint8_t>(reader, kClose);
            Virtual<uint8_t>(reader, kShutdown);
            Virtual<uint32_t>(reader, kRelease);
        }
    }
    if (void* const buffer = At<void*>(&secondPath[0], 0x4))
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(buffer);
    DeleteFileA(second);
    char more[160];
    std::snprintf(more, sizeof more, " again %d %08X sizes %u %u %u file %08X records %u holes %u", stagesDone, stageAnswers.h,
                  stageSize[0], stageSize[1], stageSize[2], stages.h, lastEntries, lastHoles);

    // OPENSC4_TEST_SAVE_FILE_BIG=1 adds a third file for two things the
    // record code is suspected of doing wrong with large records
    // (docs/crashes.md, "Prop pox"; docs/save-format.md, "The file
    // container"), to see what the game really writes:
    //  - two records of 2,100,000 bytes that do not compress, so they are
    //    stored as they are, one at the start of the file and one after it;
    //    each is then opened for writing again, which loads its old bytes
    //    into a temporary file, has 100 bytes added at its end and is
    //    closed. `sizes` are what each record says its size is on opening.
    //  - a record that grows past 2,000,000 bytes, where it moves into a
    //    temporary file, by a write that starts 5,000 bytes short of its
    //    end.
    // It is a switch of its own because it walks into the suspected faults
    // on purpose.
    std::string big;
    if (EnvInt("OPENSC4_TEST_SAVE_FILE_BIG", 0)) {
        char third[MAX_PATH + 64];
        std::snprintf(third, sizeof third, "%sopensc4-savefile-%lu-c.dat", folder, GetCurrentProcessId());
        DeleteFileA(third);
        char thirdPath[0x14];
        reinterpret_cast<void*(__thiscall*)(void*, const char*)>(addr::SC4String_FromText)(&thirdPath[0], third);
        Fnv bigAnswers, bigFile;
        uint32_t opened[2] = {}, grown = 0, bigSize = 0;
        void* const large = reinterpret_cast<OpenFn>(addr::SC4CityFiles_OpenSegment)(&thirdPath[0], 1, 0);
        if (large) {
            const Key raw1{0x7E577001, kGroup, 701}, raw2{0x7E577002, kGroup, 702}, grow{0x7E577003, kGroup, 703};
            const auto streamOn = [&](const Key& key, uint32_t version, auto&& body) {
                char holder[0x10] = {};
                reinterpret_cast<WriterFn>(addr::SC4Save_F5D9B20)(&holder[0], large, key.type, key.group, key.instance, version);
                void* const record = At<void*>(&holder[0], 0x8);
                bigAnswers.Add(record != nullptr);
                if (record)
                    body(record);
                reinterpret_cast<CloseFn>(addr::SC4Save_F5D9430)(&holder[0]);
            };
            int index = 0;
            for (const Key& key : {raw1, raw2}) {
                Key copy = key;
                const std::vector<uint8_t> bytes = TestBytes(2100000, 41 + static_cast<uint32_t>(index), 0);
                bigAnswers.Add(Virtual<uint8_t>(large, kWrite, &copy, static_cast<const void*>(bytes.data()),
                                                static_cast<uint32_t>(bytes.size())));
                ++index;
            }
            index = 0;
            for (const Key& key : {raw1, raw2}) {
                streamOn(key, 12, [&](void* record) {
                    opened[index] = Virtual<uint32_t>(record, kGetSize);
                    bigAnswers.Add(Virtual<uint8_t>(record, kSeekAbsolute, opened[index]));
                    const std::vector<uint8_t> tail = TestBytes(100, 51 + static_cast<uint32_t>(index), 0);
                    bigAnswers.Add(Virtual<uint8_t>(record, kSetVoid, static_cast<const void*>(tail.data()), uint32_t{100}));
                    bigAnswers.Add(Virtual<uint32_t>(record, kGetSize));
                });
                ++index;
            }
            streamOn(grow, 13, [&](void* record) {
                const std::vector<uint8_t> piece = TestBytes(10000, 61, 14);
                for (int i = 0; i < 199; ++i) // 2 bytes of version and 1,990,000 more
                    bigAnswers.Add(Virtual<uint8_t>(record, kSetVoid, static_cast<const void*>(piece.data()), uint32_t{10000}));
                const uint32_t end = Virtual<uint32_t>(record, kGetSize);
                bigAnswers.Add(end);
                bigAnswers.Add(Virtual<uint8_t>(record, kSeekAbsolute, end - 5000));
                const std::vector<uint8_t> across = TestBytes(30000, 62, 7);
                bigAnswers.Add(Virtual<uint8_t>(record, kSetVoid, static_cast<const void*>(across.data()), uint32_t{30000}));
                grown = Virtual<uint32_t>(record, kGetSize);
                bigAnswers.Add(grown);
                bigAnswers.Add(Virtual<uint8_t>(record, kSetUint8, uint32_t{0x99}));
                bigAnswers.Add(Virtual<uint32_t>(record, kGetSize));
            });
            bigAnswers.Add(Virtual<uint8_t>(large, kClose));
            Virtual<uint8_t>(large, kShutdown);
            Virtual<uint32_t>(large, kRelease);
            std::vector<uint8_t> bytes = ReadWholeFile(third);
            bigSize = static_cast<uint32_t>(bytes.size());
            if (bytes.size() >= 0x60)
                std::memset(&bytes[24], 0, 8);
            bigFile.Bytes(bytes.data(), bytes.size());
        }
        if (void* const buffer = At<void*>(&thirdPath[0], 0x4))
            reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(buffer);
        if (EnvInt("OPENSC4_TEST_SAVE_FILE_KEEP", 0))
            log::Write("autotest: save file kept: %s", third);
        else
            DeleteFileA(third);
        char bigText[160];
        std::snprintf(bigText, sizeof bigText, " big %08X opened %u %u grown %u size %u file %08X", bigAnswers.h, opened[0],
                      opened[1], grown, bigSize, bigFile.h);
        big = bigText;
    }

    char text[240];
    std::snprintf(text, sizeof text,
                  " | svfile %d %08X size %u file %08X header %08X records %u %08X packed %u tables %08X holes %u stale %08X back %d %08X",
                  steps, answers.h, static_cast<unsigned>(file.size()), whole.h, header.h, entries, records.h, packed, tables.h, holes,
                  stale.h, read, back.h);
    g_fileToken = std::string(text) + more + big;
    log::Write("autotest: save file driven:%s%s%s (index at %u, dates %u %u)", text, more, big.c_str(), indexAt, dates[0],
               dates[1]);
}

void Fingerprint(cISC4City*, std::string& line)
{
    line += g_streamsToken;
    line += g_fileToken;
}

const FingerprintPart kFingerprint{"saves.streams", 107, &Fingerprint};
const DayHook kStreams{"saves.streams", 107, &DriveStreams};
const DayHook kBlock{"saves.block", 108, &BlockSave};
const DayHook kFile{"saves.file", 109, &DriveFile};

} // namespace
} // namespace opensc4::autotest
