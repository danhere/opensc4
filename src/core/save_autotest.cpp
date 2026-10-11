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

void Fingerprint(cISC4City*, std::string& line)
{
    line += g_streamsToken;
}

const FingerprintPart kFingerprint{"saves.streams", 107, &Fingerprint};
const DayHook kStreams{"saves.streams", 107, &DriveStreams};

} // namespace
} // namespace opensc4::autotest
