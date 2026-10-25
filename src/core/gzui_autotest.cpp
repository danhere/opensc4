// The GZ framework UI layer's part of the autotest (src/core/autotest_hooks.h,
// docs/gzui.md): the property set test (OPENSC4_TEST_PROPSETS, part u1) and
// the readers, writers and factory test (OPENSC4_TEST_PROPIO, parts u2,
// u2b and u3), the UI file parser test (OPENSC4_TEST_XMLPARSE, part u4),
// the framework sound classes' test (OPENSC4_TEST_GZSOUND, part u5), the
// sound service's test (OPENSC4_TEST_GZSOUNDSVC, part u6), the sound
// mixer's (OPENSC4_TEST_GZSOUNDMIX, part u8), the UI script loader's
// (OPENSC4_TEST_UISCRIPT, part u9), the MP3 decoder's and the UI script
// root holder's (OPENSC4_TEST_MP3, part u10) and the root holder's life and
// the LEGACY reader's (OPENSC4_TEST_LEGACY, part u11), the readers of a
// window's script properties (OPENSC4_TEST_PROPREAD, part u12) and their
// makers, loaders and lookups (OPENSC4_TEST_PROPMAKE, part u12, slice 2) and
// their handlers (OPENSC4_TEST_PROPHANDLE, part u12, slice 3).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <process.h>
#include <string>
#include <utility>
#include <vector>

// For the game's folder (part u10 reads one of its radio MP3 files).
extern "C" __declspec(dllimport) unsigned long __stdcall GetModuleFileNameA(void* module, char* path, unsigned long size);

namespace opensc4::autotest {
namespace {

template <typename R, typename... Args>
inline R CallThis(std::uintptr_t address, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(address)(self, args...);
}

template <typename R, typename... Args>
inline R Virtual(void* object, std::size_t byteOffset, Args... args)
{
    void** const vtable = *reinterpret_cast<void***>(object);
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(vtable[byteOffset / 4])(object, args...);
}

template <typename T>
inline T& At(void* base, std::size_t offset)
{
    return *reinterpret_cast<T*>(static_cast<char*>(base) + offset);
}

bool g_on = false;
uint32_t g_hash = 2166136261u;
// The hash the current test mixes into.
uint32_t* g_into = &g_hash;

void Mix(uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        *g_into = (*g_into ^ ((value >> (8 * i)) & 0xFF)) * 16777619u;
}

// A byte answer (AL); the rest of EAX can hold an address.
void MixByte(uint32_t value) { Mix(value & 0xFF); }

// A value object as the game lays it out: its vtable and 4 bytes of data.
struct Value {
    std::uintptr_t vtable;
    uint32_t data;
};

Value Uint32(uint32_t v) { return {addr::cGZPropertyUint32_Vtable, v}; }
Value Sint32(int32_t v) { return {addr::cGZPropertySint32_Vtable, static_cast<uint32_t>(v)}; }
Value Float32(float v)
{
    Value value{addr::cGZPropertyFloat32_Vtable, 0};
    std::memcpy(&value.data, &v, 4);
    return value;
}
// The bool's padding bytes as given (CompareBytes reads them).
Value Bool(uint8_t v, uint32_t padding) { return {addr::cGZPropertyBool_Vtable, v | (padding & 0xFFFFFF00u)}; }

// A string value made with the game's constructor from text.
struct StringValue {
    Value value;
    explicit StringValue(const char* text)
    {
        CallThis<void*>(addr::cGZPropertyString_ConstructFromText, &value, text);
    }
    ~StringValue() { Virtual<void*>(&value, 0x0, uint32_t{0}); }
    StringValue(const StringValue&) = delete;
    StringValue& operator=(const StringValue&) = delete;
};

// The text of a string value (the game string's body at +4 and +8).
void MixText(const Value* value)
{
    const char* const string = reinterpret_cast<const char*>(value->data);
    if (!string) {
        Mix(0xFFFFFFFFu);
        return;
    }
    const char* const begin = *reinterpret_cast<const char* const*>(string + 0x4);
    const char* const end = *reinterpret_cast<const char* const*>(string + 0x8);
    Mix(static_cast<uint32_t>(end - begin));
    Mix(BytesChecksum(begin, static_cast<std::size_t>(end - begin)));
}

// A value's type and data, by its own slots.
void MixValue(void* value)
{
    const uint32_t type = Virtual<uint32_t>(value, 0x10);
    Mix(type);
    Mix(Virtual<uint32_t>(value, 0x14));
    Mix(Virtual<uint32_t>(value, 0x18));
    if (type == 0x0B8BEA18)
        MixText(static_cast<Value*>(value));
    else if (type == 0xCBA908E1)
        Mix(static_cast<Value*>(value)->data & 0xFF);
    else
        Mix(static_cast<Value*>(value)->data);
}

// The set's keys and values in order, through GetFirst and GetNext, and its
// Count (slot 10).
void MixSet(void* set)
{
    Mix(Virtual<uint32_t>(set, 0x28));
    uint32_t key = 0;
    void* value = nullptr;
    uint32_t n = 0;
    for (bool more = Virtual<uint32_t>(set, 0x18, &key, &value) & 0xFF; more;
         more = Virtual<uint32_t>(set, 0x1C, &key, &value) & 0xFF) {
        Mix(key);
        MixValue(value);
        ++n;
    }
    Mix(n);
}

void* NewSet()
{
    void* const memory = reinterpret_cast<void*(__cdecl*)(uint32_t)>(addr::SC4_operator_new)(0x18);
    void* const set = CallThis<void*>(addr::cGZPropertySet_Construct, memory);
    Virtual<uint32_t>(set, 0x4);
    return set;
}

// The value slots on their own: CopyFrom (null, another type, the same),
// Assign, Clone (at a place and at null), the sizes, the comparisons.
void DriveValues()
{
    Value values[] = {Uint32(7),          Uint32(0x80000001u), Sint32(-5),       Sint32(5),
                      Float32(1.5f),      Float32(-0.25f),     Bool(1, 0x00ABCD00u), Bool(0, 0x7F000000u)};
    for (Value& a : values) {
        MixValue(&a);
        for (Value& b : values) {
            Mix(static_cast<uint32_t>(Virtual<int32_t>(&a, 0x1C, &b)));
            Mix(static_cast<uint32_t>(Virtual<int32_t>(&a, 0x20, &b)));
        }
    }
    for (Value& a : values) {
        for (const Value& b : values) {
            Value copy = a;
            Value other = b;
            MixByte(Virtual<uint32_t>(&copy, 0x4, &other));
            Mix(copy.data);
            copy = a;
            MixByte(Virtual<uint32_t>(&copy, 0x8, &other));
            Mix(copy.data);
        }
        Value copy = a;
        MixByte(Virtual<uint32_t>(&copy, 0x4, static_cast<void*>(nullptr)));
        Value place{0, 0xA5A5A5A5u};
        Mix(Virtual<Value*>(&a, 0xC, &place) == &place ? 1 : 0);
        Mix(place.vtable == a.vtable ? 1 : 0);
        Mix(place.data);
        Mix(Virtual<Value*>(&a, 0xC, static_cast<Value*>(nullptr)) == nullptr ? 1 : 0);
    }

    // Strings: compared, copied (from another type, from itself, from
    // another string), cloned and destroyed.
    StringValue alpha("Alpha"), alphaLower("alpha"), beta("Beta"), empty("");
    StringValue* const strings[] = {&alpha, &alphaLower, &beta, &empty};
    for (StringValue* a : strings) {
        MixValue(&a->value);
        for (StringValue* b : strings) {
            Mix(static_cast<uint32_t>(Virtual<int32_t>(&a->value, 0x1C, &b->value)));
            Mix(static_cast<uint32_t>(Virtual<int32_t>(&a->value, 0x20, &b->value)));
        }
    }
    StringValue target("target");
    Value number = Uint32(3);
    MixByte(Virtual<uint32_t>(&target.value, 0x4, &number));
    MixByte(Virtual<uint32_t>(&target.value, 0x4, static_cast<void*>(nullptr)));
    MixByte(Virtual<uint32_t>(&target.value, 0x4, &target.value));
    MixText(&target.value);
    MixByte(Virtual<uint32_t>(&target.value, 0x4, &beta.value));
    MixText(&target.value);
    MixByte(Virtual<uint32_t>(&target.value, 0x8, &alpha.value));
    MixText(&target.value);
    MixByte(Virtual<uint32_t>(&target.value, 0x8, &number));
    MixByte(Virtual<uint32_t>(&number, 0x4, &target.value));
    {
        Value place{0, 0};
        Mix(Virtual<Value*>(&alpha.value, 0xC, &place) == &place ? 1 : 0);
        Mix(place.vtable == alpha.value.vtable ? 1 : 0);
        Mix(place.data == alpha.value.data ? 1 : 0);
        MixText(&place);
        Virtual<void*>(&place, 0x0, uint32_t{0});
        Mix(place.vtable == addr::cGZPropertyValue_Vtable ? 1 : 0);
        // At null the game's answer is its caller's EAX: called, not mixed.
        Virtual<Value*>(&alpha.value, 0xC, static_cast<Value*>(nullptr));
    }
    // A string value from a game string, on the heap, deleted with flag 1.
    {
        void* const memory = reinterpret_cast<void*(__cdecl*)(uint32_t)>(addr::SC4_operator_new)(0x14);
        void* const text = CallThis<void*>(addr::SC4String_FromText, memory, "from a string");
        void* const block = reinterpret_cast<void*(__cdecl*)(uint32_t)>(addr::SC4_operator_new)(8);
        auto* const value = CallThis<Value*>(addr::cGZPropertyString_ConstructFromIGZString, block, text);
        MixValue(value);
        Mix(value->data != reinterpret_cast<std::uintptr_t>(text) ? 1 : 0);
        Mix(Virtual<void*>(value, 0x0, uint32_t{1}) == value ? 1 : 0);
        Virtual<uint32_t>(text, 0x8);
    }
    // A uint32 value on the heap, deleted through the base's slot 0.
    {
        auto* const value = static_cast<Value*>(reinterpret_cast<void*(__cdecl*)(uint32_t)>(addr::SC4_operator_new)(8));
        *value = Uint32(11);
        Mix(Virtual<void*>(value, 0x0, uint32_t{1}) == value ? 1 : 0);
    }
}

// The set: values of every type set, replaced (by the same and another
// type), read back (the same type, another, a missing key), looked for,
// enumerated (with null pointers too), removed (there and not), emptied,
// the interfaces, and the count down to the set's end.
void DriveSet()
{
    void* const set = NewSet();
    MixSet(set);
    {
        uint32_t key = 0;
        void* value = nullptr;
        MixByte(Virtual<uint32_t>(set, 0x18, &key, &value));
        MixByte(Virtual<uint32_t>(set, 0x18, static_cast<uint32_t*>(nullptr), &value));
    }

    StringValue hello("Hello"), world("World"), other("other");
    Value values[] = {Uint32(100), Sint32(-200), Float32(3.25f), Bool(1, 0), Uint32(0xFFFFFFFFu)};
    const uint32_t keys[] = {0x30, 0x10, 0xFF000001u, 0x20, 0x00000001u, 0x80000000u, 0x40};
    uint32_t key;
    for (int i = 0; i < 5; ++i) {
        key = keys[i];
        MixByte(Virtual<uint32_t>(set, 0xC, &key, &values[i]));
    }
    key = keys[5];
    MixByte(Virtual<uint32_t>(set, 0xC, &key, &hello.value));
    key = keys[6];
    MixByte(Virtual<uint32_t>(set, 0xC, &key, &world.value));
    MixSet(set);

    // Replaced: a uint32 by a string, a string by a float, a bool by a bool.
    key = keys[0];
    MixByte(Virtual<uint32_t>(set, 0xC, &key, &other.value));
    key = keys[6];
    Value replacement = Float32(-8.0f);
    MixByte(Virtual<uint32_t>(set, 0xC, &key, &replacement));
    key = keys[3];
    Value falseValue = Bool(0, 0x11223300u);
    MixByte(Virtual<uint32_t>(set, 0xC, &key, &falseValue));
    MixSet(set);

    log::Write("autotest: propsets: set %08X", g_hash);
    // Read back.
    for (const uint32_t k : keys) {
        key = k;
        MixByte(Virtual<uint32_t>(set, 0x24, &key));
        Value asUint = Uint32(0x5A5A5A5Au);
        MixByte(Virtual<uint32_t>(set, 0x20, &key, &asUint));
        Mix(asUint.data);
        Value asFloat = Float32(0.0f);
        MixByte(Virtual<uint32_t>(set, 0x20, &key, &asFloat));
        Mix(asFloat.data);
        StringValue asString("none");
        MixByte(Virtual<uint32_t>(set, 0x20, &key, &asString.value));
        MixText(&asString.value);
    }
    key = 0x12345678;
    MixByte(Virtual<uint32_t>(set, 0x24, &key));
    Value missing = Uint32(9);
    MixByte(Virtual<uint32_t>(set, 0x20, &key, &missing));
    Mix(missing.data);

    log::Write("autotest: propsets: read %08X", g_hash);
    // Enumerated by hand, with a null pointer half way.
    {
        uint32_t k = 0;
        void* value = nullptr;
        MixByte(Virtual<uint32_t>(set, 0x18, &k, &value));
        Mix(k);
        MixByte(Virtual<uint32_t>(set, 0x1C, &k, &value));
        Mix(k);
        MixByte(Virtual<uint32_t>(set, 0x1C, &k, static_cast<void**>(nullptr)));
        MixByte(Virtual<uint32_t>(set, 0x1C, &k, &value));
        Mix(k);
        MixValue(value);
    }

    log::Write("autotest: propsets: walk %08X", g_hash);
    // Removed: there, not there, there again.
    key = keys[1];
    MixByte(Virtual<uint32_t>(set, 0x10, &key));
    MixByte(Virtual<uint32_t>(set, 0x10, &key));
    key = keys[5];
    MixByte(Virtual<uint32_t>(set, 0x10, &key));
    key = 0x77777777u;
    MixByte(Virtual<uint32_t>(set, 0x10, &key));
    MixSet(set);

    log::Write("autotest: propsets: removed %08X", g_hash);
    // The interfaces.
    void* same = nullptr;
    MixByte(Virtual<uint32_t>(set, 0x0, uint32_t{1}, &same));
    Mix(same == set ? 1 : 0);
    MixByte(Virtual<uint32_t>(set, 0x0, uint32_t{0xEBB10CDDu}, &same));
    Mix(same == set ? 1 : 0);
    same = nullptr;
    MixByte(Virtual<uint32_t>(set, 0x0, uint32_t{0x7E570DFEu}, &same));
    Mix(same == nullptr ? 1 : 0);
    Mix(Virtual<uint32_t>(set, 0x8));
    Mix(Virtual<uint32_t>(set, 0x8));

    // Emptied, filled again, and the count down to its end (the last
    // Release destroys the values left in it).
    Virtual<uint32_t>(set, 0x14);
    MixSet(set);
    key = 5;
    MixByte(Virtual<uint32_t>(set, 0xC, &key, &hello.value));
    key = 6;
    MixByte(Virtual<uint32_t>(set, 0xC, &key, &values[0]));
    MixSet(set);
    Mix(Virtual<uint32_t>(set, 0x8));

    log::Write("autotest: propsets: released %08X", g_hash);
    // The strings' references: back to one each once the set is gone.
    for (StringValue* s : {&hello, &world, &other}) {
        const char* const string = reinterpret_cast<const char*>(s->value.data);
        Mix(*reinterpret_cast<const uint32_t*>(string + 0x10));
    }

    // A set on the stack: the destructor by its address.
    {
        alignas(4) char local[0x18];
        CallThis<void*>(addr::cGZPropertySet_Construct, local);
        key = 1;
        MixByte(Virtual<uint32_t>(local, 0xC, &key, &world.value));
        key = 2;
        MixByte(Virtual<uint32_t>(local, 0xC, &key, &values[2]));
        MixSet(local);
        CallThis<void>(addr::cGZPropertySet_Destruct, local);
        Mix(*reinterpret_cast<const uint32_t*>(reinterpret_cast<const char*>(world.value.data) + 0x10));
    }
}

// OPENSC4_TEST_PROPSETS=<day>: property sets made with the game's
// constructor and their values taken through every slot of part u1 (see
// DriveValues and DriveSet). Every result goes into the fingerprint's
// `pset` part; addresses never do.
void Drive(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_PROPSETS") || !game::IsBuild641())
        return;
    g_on = true;
    DriveValues();
    log::Write("autotest: propsets: values %08X", g_hash);
    DriveSet();
    log::Write("autotest: propsets driven (hash %08X)", g_hash);
}

void Fingerprint(cISC4City*, std::string& line)
{
    if (!g_on)
        return;
    char text[32];
    std::snprintf(text, sizeof text, " | pset %08X", g_hash);
    line += text;
}

const FingerprintPart kFingerprint{"propsets", 4, &Fingerprint};
const DayHook kDrive{"propsets", 4, &Drive};


// ====================================================================================
// Part u2: the property sets' readers and writers, the factory and what it
// makes (OPENSC4_TEST_PROPIO)

bool g_pioOn = false;
uint32_t g_pioHash = 2166136261u;

void* New(uint32_t size) { return reinterpret_cast<void*(__cdecl*)(uint32_t)>(addr::SC4_operator_new)(size); }

void MixBytes(const std::vector<uint8_t>& bytes)
{
    Mix(static_cast<uint32_t>(bytes.size()));
    Mix(bytes.empty() ? 0 : BytesChecksum(bytes.data(), bytes.size()));
}

// The text of a game string object (its body at +4 and +8).
void MixString(const void* string)
{
    const char* const begin = *reinterpret_cast<const char* const*>(static_cast<const char*>(string) + 0x4);
    const char* const end = *reinterpret_cast<const char* const*>(static_cast<const char*>(string) + 0x8);
    Mix(static_cast<uint32_t>(end - begin));
    Mix(BytesChecksum(begin, static_cast<std::size_t>(end - begin)));
}

// A pointer as an offset from a base (addresses never go into the hash).
void MixOffset(const void* pointer, const void* base)
{
    Mix(pointer ? static_cast<uint32_t>(static_cast<const char*>(pointer) - static_cast<const char*>(base))
                : 0xFFFFFFFFu);
}

// A stream as the readers and writers use it, in memory: slot 0x14 a byte,
// 0x1C a word, 0x24 a dword, 0x30 a float, 0x3C a game string (a dword
// length and the text), 0x44 bytes; its seekable interface (0xC56B8F08, the
// same object) 0x34 a dword, 0x6C bytes, 0xD0 the size and 0xDC the
// position. Every call
// goes into the hash. `failAt` makes the n-th read or write fail.
struct FakeStream {
    void* const* vtable;
    uint32_t refs = 0;
    std::vector<uint8_t> bytes;
    std::size_t cursor = 0;
    int ops = 0;
    int failAt = 0;
    bool refuseSeekable = false;
    bool refuseTarget = false;

    explicit FakeStream(void* const* table) : vtable(table) {}

    bool Op(uint32_t slot)
    {
        Mix(slot);
        return ++ops != failAt;
    }
    bool Take(void* out, std::size_t size)
    {
        if (size > bytes.size() - cursor)
            return false;
        std::memcpy(out, bytes.data() + cursor, size);
        cursor += size;
        return true;
    }
    void Put(const void* data, std::size_t size)
    {
        const auto* const b = static_cast<const uint8_t*>(data);
        bytes.insert(bytes.end(), b, b + size);
    }
};

uint32_t __fastcall StreamQueryInterface(FakeStream* self, void*, uint32_t iid, void** out)
{
    Mix(0x5100);
    Mix(iid);
    const bool yes = iid == 1 || (iid == 0xC56B8F08u && !self->refuseSeekable) || iid == 0x051B11BDu ||
                     (iid == 0xC51B11CBu && !self->refuseTarget);
    if (!yes)
        return 0;
    *out = self;
    ++self->refs;
    return 1;
}
uint32_t __fastcall StreamAddRef(FakeStream* self, void*) { return ++self->refs; }
uint32_t __fastcall StreamRelease(FakeStream* self, void*) { return --self->refs; }
uint32_t __fastcall StreamSize(FakeStream* self, void*)
{
    Mix(0x51D0);
    return static_cast<uint32_t>(self->bytes.size());
}
uint32_t __fastcall StreamSeek(FakeStream* self, void*, uint32_t position)
{
    Mix(0x51DC);
    Mix(position);
    self->cursor = position;
    return 1;
}
uint32_t __fastcall StreamTrap(FakeStream*, void*)
{
    log::Write("autotest: propio: unexpected stream slot");
    Mix(0xDEAD);
    return 0;
}

// Read mode.
uint32_t __fastcall ReadU8(FakeStream* self, void*, uint8_t* out)
{
    return self->Op(0x14) && self->Take(out, 1) ? (Mix(*out), 1u) : 0u;
}
uint32_t __fastcall ReadU16(FakeStream* self, void*, uint16_t* out)
{
    return self->Op(0x1C) && self->Take(out, 2) ? (Mix(*out), 1u) : 0u;
}
uint32_t __fastcall ReadU32(FakeStream* self, void*, uint32_t* out)
{
    return self->Op(0x24) && self->Take(out, 4) ? (Mix(*out), 1u) : 0u;
}
uint32_t __fastcall ReadU32Seekable(FakeStream* self, void*, uint32_t* out)
{
    return self->Op(0x34) && self->Take(out, 4) ? (Mix(*out), 1u) : 0u;
}
uint32_t __fastcall ReadF32(FakeStream* self, void*, uint32_t* out)
{
    return self->Op(0x30) && self->Take(out, 4) ? (Mix(*out), 1u) : 0u;
}
uint32_t __fastcall ReadStr(FakeStream* self, void*, void* string)
{
    uint32_t length = 0;
    if (!self->Op(0x3C) || !self->Take(&length, 4) || self->cursor + length > self->bytes.size())
        return 0;
    const char* const text = reinterpret_cast<const char*>(self->bytes.data() + self->cursor);
    self->cursor += length;
    Mix(length);
    return Virtual<uint32_t>(string, 0xC, text, length) & 0xFF;
}
uint32_t __fastcall ReadBytes(FakeStream* self, void*, void* data, uint32_t size)
{
    Mix(size);
    return self->Op(0x44) && self->Take(data, size) ? 1u : 0u;
}
uint32_t __fastcall ReadBytes2(FakeStream* self, void*, void* data, uint32_t size)
{
    Mix(size);
    return self->Op(0x6C) && self->Take(data, size) ? 1u : 0u;
}

// Write mode.
uint32_t __fastcall WriteU8(FakeStream* self, void*, uint32_t value)
{
    const uint8_t b = static_cast<uint8_t>(value);
    Mix(b);
    return self->Op(0x14) ? (self->Put(&b, 1), 1u) : 0u;
}
uint32_t __fastcall WriteU16(FakeStream* self, void*, uint32_t value)
{
    const uint16_t w = static_cast<uint16_t>(value);
    Mix(w);
    return self->Op(0x1C) ? (self->Put(&w, 2), 1u) : 0u;
}
uint32_t __fastcall WriteU32(FakeStream* self, void*, uint32_t value)
{
    Mix(value);
    return self->Op(0x24) ? (self->Put(&value, 4), 1u) : 0u;
}
uint32_t __fastcall WriteF32(FakeStream* self, void*, uint32_t bits)
{
    Mix(bits);
    return self->Op(0x30) ? (self->Put(&bits, 4), 1u) : 0u;
}
uint32_t __fastcall WriteStr(FakeStream* self, void*, const void* string)
{
    const char* const begin = *reinterpret_cast<const char* const*>(static_cast<const char*>(string) + 0x4);
    const char* const end = *reinterpret_cast<const char* const*>(static_cast<const char*>(string) + 0x8);
    const uint32_t length = static_cast<uint32_t>(end - begin);
    Mix(length);
    if (!self->Op(0x3C))
        return 0;
    self->Put(&length, 4);
    self->Put(begin, length);
    return 1;
}
uint32_t __fastcall WriteBytes(FakeStream* self, void*, const void* data, uint32_t size)
{
    Mix(size);
    return self->Op(0x44) ? (self->Put(data, size), 1u) : 0u;
}

struct StreamVtables {
    void* read[0x38];
    void* write[0x38];
    StreamVtables()
    {
        for (void*& slot : read)
            slot = reinterpret_cast<void*>(&StreamTrap);
        for (void*& slot : write)
            slot = reinterpret_cast<void*>(&StreamTrap);
        for (void** v : {read, write}) {
            v[0x00 / 4] = reinterpret_cast<void*>(&StreamQueryInterface);
            v[0x04 / 4] = reinterpret_cast<void*>(&StreamAddRef);
            v[0x08 / 4] = reinterpret_cast<void*>(&StreamRelease);
            v[0xD0 / 4] = reinterpret_cast<void*>(&StreamSize);
            v[0xDC / 4] = reinterpret_cast<void*>(&StreamSeek);
        }
        read[0x14 / 4] = reinterpret_cast<void*>(&ReadU8);
        read[0x1C / 4] = reinterpret_cast<void*>(&ReadU16);
        read[0x24 / 4] = reinterpret_cast<void*>(&ReadU32);
        read[0x30 / 4] = reinterpret_cast<void*>(&ReadF32);
        read[0x34 / 4] = reinterpret_cast<void*>(&ReadU32Seekable);
        read[0x3C / 4] = reinterpret_cast<void*>(&ReadStr);
        read[0x44 / 4] = reinterpret_cast<void*>(&ReadBytes);
        read[0x6C / 4] = reinterpret_cast<void*>(&ReadBytes2);
        write[0x14 / 4] = reinterpret_cast<void*>(&WriteU8);
        write[0x1C / 4] = reinterpret_cast<void*>(&WriteU16);
        write[0x24 / 4] = reinterpret_cast<void*>(&WriteU32);
        write[0x30 / 4] = reinterpret_cast<void*>(&WriteF32);
        write[0x3C / 4] = reinterpret_cast<void*>(&WriteStr);
        write[0x44 / 4] = reinterpret_cast<void*>(&WriteBytes);
    }
};

const StreamVtables& Vtables()
{
    static const StreamVtables vtables;
    return vtables;
}

FakeStream Writer() { return FakeStream{Vtables().write}; }
FakeStream Reader(const std::vector<uint8_t>& bytes)
{
    FakeStream stream{Vtables().read};
    stream.bytes = bytes;
    return stream;
}

// A resource for the factory's CreateFromResource: its key (slot 3) and its
// stream (interface 0x051B11BD).
struct FakeResource {
    void* const* vtable;
    uint32_t refs;
    uint32_t key[3];
    FakeStream* stream;
    bool refuse;
};

uint32_t __fastcall ResourceQueryInterface(FakeResource* self, void*, uint32_t iid, void** out)
{
    Mix(0x5200);
    Mix(iid);
    if (iid != 0x051B11BDu || self->refuse)
        return 0;
    *out = self->stream;
    ++self->stream->refs;
    return 1;
}
uint32_t __fastcall ResourceAddRef(FakeResource* self, void*) { return ++self->refs; }
uint32_t __fastcall ResourceRelease(FakeResource* self, void*) { return --self->refs; }
const uint32_t* __fastcall ResourceGetKey(FakeResource* self, void*, void*)
{
    Mix(0x520C);
    return self->key;
}
void* const kResourceVtable[] = {reinterpret_cast<void*>(&ResourceQueryInterface),
                                 reinterpret_cast<void*>(&ResourceAddRef), reinterpret_cast<void*>(&ResourceRelease),
                                 reinterpret_cast<void*>(&ResourceGetKey)};

// The object a UI script handler keeps at +0x1C: slot 16 takes a game string.
struct FakeTextSink {
    void* const* vtable;
};
uint32_t __fastcall SinkTake(FakeTextSink*, void*, const void* string)
{
    Mix(0x5340);
    MixString(string);
    return 0x00ABCD01u;
}
struct SinkVtable {
    void* slots[17];
    SinkVtable()
    {
        for (void*& slot : slots)
            slot = reinterpret_cast<void*>(&StreamTrap);
        slots[16] = reinterpret_cast<void*>(&SinkTake);
    }
};

// Calls by address with a stack address in ECX, as the game's callers do.
uint32_t CallReadSet(void* stream, void* set)
{
    uint32_t local[2] = {};
    return CallThis<uint32_t>(addr::cGZPropSetIO_ReadSet, local, stream, set);
}
uint32_t CallWriteSet(void* stream, void* set, uint32_t binary)
{
    uint32_t local[2] = {};
    return CallThis<uint32_t>(addr::cGZPropSetIO_WriteSet, local, stream, set, binary);
}

void SetUint32(void* set, uint32_t key, uint32_t v)
{
    Value value = Uint32(v);
    MixByte(Virtual<uint32_t>(set, 0xC, &key, &value));
}

// A set with one value of each type the binary form knows, and a string.
void FillSet(void* set, const char* text)
{
    SetUint32(set, 0x10, 7);
    Value b = Bool(1, 0x00AA5500u);
    uint32_t key = 0x20;
    MixByte(Virtual<uint32_t>(set, 0xC, &key, &b));
    Value f = Float32(2.5f);
    key = 0x30;
    MixByte(Virtual<uint32_t>(set, 0xC, &key, &f));
    StringValue s(text);
    key = 0x40;
    MixByte(Virtual<uint32_t>(set, 0xC, &key, &s.value));
    SetUint32(set, 0x80000050u, 0xFFFFFFFFu);
    Value f2 = Float32(-1.0e-30f);
    key = 0x60;
    MixByte(Virtual<uint32_t>(set, 0xC, &key, &f2));
}

void ReleaseSet(void* set)
{
    while (Virtual<uint32_t>(set, 0x8) != 0) {
    }
}

void PioStage(const char* name) { log::Write("autotest: propio: %s %08X", name, g_pioHash); }

// The binary form: written, read back, every truncation, the refusals.
std::vector<uint8_t> DriveBinary()
{
    void* const set = NewSet();
    FillSet(set, "Binary");
    FakeStream w = Writer();
    MixByte(CallWriteSet(&w, set, 1));
    MixBytes(w.bytes);
    const std::vector<uint8_t> bytes = w.bytes;

    FakeStream r = Reader(bytes);
    void* const back = NewSet();
    MixByte(CallReadSet(&r, back));
    MixSet(back);
    ReleaseSet(back);

    // Every truncation: the reader stops where the bytes do.
    for (std::size_t n = 0; n < bytes.size(); n += (n < 24 ? 1 : 3)) {
        FakeStream t = Reader(std::vector<uint8_t>(bytes.begin(), bytes.begin() + n));
        void* const part = NewSet();
        MixByte(CallReadSet(&t, part));
        MixSet(part);
        ReleaseSet(part);
    }
    // The other record type picks the binary reader, which refuses it; a
    // version above 1; an unknown value type, skipped with nothing read.
    {
        std::vector<uint8_t> other = bytes;
        const uint32_t record = 0xCBE750E0u;
        std::memcpy(other.data(), &record, 4);
        FakeStream t = Reader(other);
        void* const part = NewSet();
        MixByte(CallReadSet(&t, part));
        MixSet(part);
        ReleaseSet(part);
        std::vector<uint8_t> version = bytes;
        version[4] = 2;
        FakeStream v = Reader(version);
        void* const part2 = NewSet();
        MixByte(CallReadSet(&v, part2));
        ReleaseSet(part2);
        const uint32_t words[] = {0x2BEA4862u, 0, 2, 0x0C264712u, 0x60, 0xEB61E4F7u, 0x70, 5};
        std::vector<uint8_t> skip(4);
        std::memcpy(skip.data(), &words[0], 4);
        skip.push_back(1);
        skip.push_back(0);
        for (int i = 2; i < 8; ++i) {
            const auto* p = reinterpret_cast<const uint8_t*>(&words[i]);
            skip.insert(skip.end(), p, p + 4);
        }
        FakeStream s = Reader(skip);
        void* const part3 = NewSet();
        MixByte(CallReadSet(&s, part3));
        MixSet(part3);
        ReleaseSet(part3);
        FakeStream q = Reader(bytes);
        q.refuseSeekable = true;
        MixByte(CallReadSet(&q, set));
        FakeStream e = Reader({});
        MixByte(CallReadSet(&e, set));
    }
    // The writer failing at each of its writes, an empty set, a sint32.
    for (int k = 1; k < 30; k += 2) {
        FakeStream f = Writer();
        f.failAt = k;
        MixByte(CallWriteSet(&f, set, 1));
        MixBytes(f.bytes);
    }
    {
        void* const empty = NewSet();
        FakeStream f = Writer();
        MixByte(CallWriteSet(&f, empty, 1));
        MixBytes(f.bytes);
        Value s32 = Sint32(-9);
        uint32_t key = 0x18;
        Virtual<uint32_t>(empty, 0xC, &key, &s32);
        SetUint32(empty, 0x08, 1);
        SetUint32(empty, 0x28, 2);
        FakeStream g = Writer();
        MixByte(CallWriteSet(&g, empty, 1));
        MixBytes(g.bytes);
        ReleaseSet(empty);
    }
    // The object itself, on the stack: Read and Write through its slots,
    // the version kept at +8, and the deleting destructor with flag 0.
    {
        struct {
            std::uintptr_t vtable;
            uint32_t refs;
            uint16_t version;
            uint16_t pad;
        } io{addr::cGZPropSetBinaryIO_Vtable, 0, 0x7777, 0x5555};
        FakeStream r2 = Reader(bytes);
        void* const part = NewSet();
        MixByte(Virtual<uint32_t>(&io, 0xC, &r2, part));
        Mix(io.version);
        MixSet(part);
        FakeStream w2 = Writer();
        MixByte(Virtual<uint32_t>(&io, 0x10, &w2, part, uint32_t{0x99}));
        MixBytes(w2.bytes);
        ReleaseSet(part);
        Mix(Virtual<void*>(&io, 0x14, uint32_t{0}) == &io ? 1 : 0);
        Mix(io.vtable == addr::cGZPropSetBinaryIO_Vtable ? 1 : 0);
    }
    ReleaseSet(set);
    return bytes;
}

// The XML form: the game's writer, our reader with the game's parser; the
// object's slots and its handler's on their own.
std::vector<uint8_t> DriveXml()
{
    void* const set = NewSet();
    FillSet(set, "Xml & <text>");
    Value s32 = Sint32(-12345);
    uint32_t key = 0x70;
    Virtual<uint32_t>(set, 0xC, &key, &s32);
    FakeStream w = Writer();
    MixByte(CallWriteSet(&w, set, 0));
    MixBytes(w.bytes);
    const std::vector<uint8_t> bytes = w.bytes;
    FakeStream r = Reader(bytes);
    void* const back = NewSet();
    MixByte(CallReadSet(&r, back));
    MixSet(back);
    ReleaseSet(back);
    ReleaseSet(set);

    char* const xml = static_cast<char*>(CallThis<void*>(addr::cGZPropSetXmlIO_Construct, New(0x54)));
    Virtual<uint32_t>(xml, 0x4);
    for (const uint32_t iid : {1u, 0xEBE701F3u, 0x4B6B9C9Bu, 0x12345678u}) {
        void* out = nullptr;
        MixByte(Virtual<uint32_t>(xml, 0x0, iid, &out));
        MixOffset(out, xml);
    }
    char* const handler = xml + 0x4;
    {
        void* out = nullptr;
        MixByte(Virtual<uint32_t>(handler, 0x0, 1u, &out));
        MixOffset(out, xml);
        Mix(Virtual<uint32_t>(handler, 0x8));
        Mix(At<uint32_t>(xml, 0x8));
    }
    // Characters: whitespace leaves the text, anything else replaces it.
    const struct {
        const char* text;
        uint32_t length;
    } chunks[] = {{"  \t\r\n", 5}, {"ab", 2}, {" \n", 2}, {"\x80", 1}, {"\x0b", 1}, {" x ", 3}, {"zz", 0}, {"\x0c", 1}};
    for (const auto& chunk : chunks) {
        MixByte(Virtual<uint32_t>(handler, 0x14, chunk.text, chunk.length));
        MixString(xml + 0x3C);
    }
    // EndElement: the root's end, then a value of each type.
    {
        void* const target = NewSet();
        At<void*>(xml, 0x50) = target;
        At<int32_t>(xml, 0xC) = 1;
        MixByte(Virtual<uint32_t>(handler, 0x10, static_cast<const void*>(nullptr)));
        Mix(At<uint32_t>(xml, 0xC));
        const struct {
            const char* key;
            const char* type;
            const char* text;
        } elements[] = {
            {"0x00000042", "0xEB61E4F7", "0x0000ABCD"}, {"0x43", "0x0C264712", "-5"},
            {"0x44", "0xABC78708", "1.25"},             {"0x45", "0xCBA908E1", "TRUE"},
            {"0x46", "0xCBA908E1", "false"},            {"0x47", "0x0B8BEA18", "hello"},
            {"0x48", "0x12345678", "1"},                {"0x42", "0xABC78708", "-0.1"},
            {"0x49", "0xEB61E4F7", "12"},               {"0x4A", "0xCBA908E1", "true "},
        };
        for (const auto& e : elements) {
            At<int32_t>(xml, 0xC) = 3;
            Virtual<uint32_t>(xml + 0x14, 0xC, e.key, static_cast<uint32_t>(std::strlen(e.key)));
            Virtual<uint32_t>(xml + 0x28, 0xC, e.type, static_cast<uint32_t>(std::strlen(e.type)));
            Virtual<uint32_t>(xml + 0x3C, 0xC, e.text, static_cast<uint32_t>(std::strlen(e.text)));
            MixByte(Virtual<uint32_t>(handler, 0x10, static_cast<const void*>(nullptr)));
            Mix(At<uint32_t>(xml, 0xC));
        }
        MixSet(target);
        // Read on its own: no seekable interface, a failed read, the bytes.
        FakeStream q = Reader(bytes);
        q.refuseSeekable = true;
        MixByte(Virtual<uint32_t>(xml, 0xC, &q, target));
        FakeStream f = Reader(bytes);
        f.failAt = 1;
        MixByte(Virtual<uint32_t>(xml, 0xC, &f, target));
        At<int32_t>(xml, 0xC) = 0;
        FakeStream g = Reader(bytes);
        MixByte(Virtual<uint32_t>(xml, 0xC, &g, target));
        MixSet(target);
        ReleaseSet(target);
    }
    // Released to its end through the handler's thunk.
    uint32_t n = 0;
    while (Virtual<uint32_t>(handler, 0x8) != 0)
        ++n;
    Mix(n);
    // And one on the stack, destroyed by its destructor's address.
    {
        alignas(4) char local[0x54];
        CallThis<void*>(addr::cGZPropSetXmlIO_Construct, local);
        Mix(At<uint32_t>(local, 0x50));
        Mix(At<uint8_t>(local, 0x10));
        CallThis<void>(addr::cGZPropSetXmlIO_Destruct, local);
        Mix(At<std::uintptr_t>(local, 0) == addr::cGZPropSetXmlIO_Vtable ? 1 : 0);
    }
    return bytes;
}

// The Track objects, made by the factory's Create: their reads and writes
// and their key interface.
void DriveTrack(char* factory, const std::vector<uint8_t>& binary)
{
    for (const uint32_t type : {0xEBFEE349u, 0xEBFEE345u, 0x0B9EB87Eu}) {
        void* set = nullptr;
        MixByte(Virtual<uint32_t>(factory, 0x10, type, 0xEBB10CDDu, &set, uint32_t{0}, uint32_t{0}));
        MixSet(set);
        FillSet(set, "Track");
        FakeStream wb = Writer();
        FakeStream wx = Writer();
        if (type != 0x0B9EB87Eu) {
            MixByte(Virtual<uint32_t>(set, 0x3C, &wb, uint32_t{0}, uint32_t{0}));
            MixBytes(wb.bytes);
        }
        MixByte(Virtual<uint32_t>(set, 0x38, &wx, uint32_t{0}, uint32_t{0}));
        MixBytes(wx.bytes);

        void* key = nullptr;
        MixByte(Virtual<uint32_t>(set, 0x0, 0x3B93C5E7u, &key));
        MixOffset(key, set);
        uint32_t k[3] = {};
        Mix(Virtual<uint32_t*>(key, 0xC, k) == k ? 1 : 0);
        Mix(k[0]);
        Mix(k[1]);
        Mix(k[2]);
        MixByte(Virtual<uint32_t>(key, 0x20, type));
        MixByte(Virtual<uint32_t>(key, 0x28, uint32_t{0x00C0FFEE}));
        Virtual<uint32_t*>(key, 0xC, k);
        Mix(k[2]);

        // Read back: the Track B classes through their set's slot 12 and 13,
        // the Track A one through its key interface's slot 12.
        void* back = nullptr;
        Virtual<uint32_t>(factory, 0x10, type, 0xEBB10CDDu, &back, uint32_t{0}, uint32_t{0});
        if (type == 0x0B9EB87Eu) {
            void* backKey = nullptr;
            Virtual<uint32_t>(back, 0x0, 0x3B93C5E7u, &backKey);
            FakeStream r = Reader(wx.bytes);
            MixByte(Virtual<uint32_t>(backKey, 0x30, &r, uint32_t{0}, uint32_t{0}));
            FakeStream rb = Reader(binary);
            MixByte(Virtual<uint32_t>(backKey, 0x30, &rb, uint32_t{0}, uint32_t{0}));
            Virtual<uint32_t>(backKey, 0x8);
        } else {
            FakeStream r = Reader(wb.bytes);
            MixByte(Virtual<uint32_t>(back, 0x30, &r, uint32_t{0}, uint32_t{0}));
            FakeStream rx = Reader(wx.bytes);
            MixByte(Virtual<uint32_t>(back, 0x34, &rx, uint32_t{0}, uint32_t{0}));
        }
        MixSet(back);
        ReleaseSet(back);
        Virtual<uint32_t>(key, 0x8);
        ReleaseSet(set);
    }
}

// The Track objects' TKDT text form (part u2b): the writer through a Track
// object's slot 13 and by address, the reader through slot 12 and the key
// interface's slot 12, with every truncation and malformed text.
constexpr uint32_t kTkdtType = 0x0B8D821Au;
constexpr uint32_t kTkdtInstance = 0x00001234u;

constexpr uint32_t kTkdtKeys[] = {
    0xFFAEC905u, 0xFF5F15E1u, 0xFF4DCD0Du, 0xFF050A94u, 0xFFB0333Du, 0xFF706616u, 0xFFCC156Fu,
    0xFFCC3B29u, 0xFF891EAAu, 0xFFAE9DDBu, 0xFFF7B8DFu, 0xFF90AAC8u, 0xFF3C2160u, 0xFFD7EE4Du,
    0xFF59FDD8u, 0xFF2D7CD1u, 0xFF0246FEu, 0xFFFF4E79u, 0xFF595FFDu, 0xFF8DCFD8u, 0xFF69510Bu,
    0xFF099DB2u, 0xFF53DBF3u, 0xFF3EC880u, 0xFF47F685u, 0xFFCAA344u, 0xFF303B1Bu,
};

// A Track object of the text type with the given instance.
void* NewTrackText(char* factory, uint32_t instance)
{
    void* set = nullptr;
    Virtual<uint32_t>(factory, 0x10, kTkdtType, 0xEBB10CDDu, &set, uint32_t{0}, uint32_t{0});
    void* key = nullptr;
    Virtual<uint32_t>(set, 0x0, 0x3B93C5E7u, &key);
    Virtual<uint32_t>(key, 0x28, instance);
    Virtual<uint32_t>(key, 0x8);
    return set;
}

// A dword (not part of the text) and the text.
std::vector<uint8_t> TkdtBytes(uint32_t first, const std::string& text)
{
    std::vector<uint8_t> bytes(4);
    std::memcpy(bytes.data(), &first, 4);
    bytes.insert(bytes.end(), text.begin(), text.end());
    return bytes;
}

void ReadTkdt(char* factory, FakeStream& stream)
{
    void* const set = NewTrackText(factory, kTkdtInstance);
    MixByte(Virtual<uint32_t>(set, 0x30, &stream, uint32_t{0}, uint32_t{0}));
    MixSet(set);
    ReleaseSet(set);
}

void ReadTkdt(char* factory, const std::vector<uint8_t>& bytes)
{
    FakeStream stream = Reader(bytes);
    ReadTkdt(factory, stream);
}

// The text with `count` numbered fields after the header.
std::string TkdtFields(const char* header, int count, const char* end)
{
    std::string text = header;
    char number[16];
    for (int i = 0; i < count; ++i) {
        std::snprintf(number, sizeof number, "%d,", 100 + i);
        text += number;
    }
    return text + end;
}

void DriveTkdt(char* factory)
{
    // Written: a new object (no values), then one with every value (some of
    // another type, one missing), failing, and a plain set by address.
    {
        void* const set = NewTrackText(factory, kTkdtInstance);
        MixSet(set);
        FakeStream w = Writer();
        MixByte(Virtual<uint32_t>(set, 0x34, &w, uint32_t{0}, uint32_t{0}));
        MixBytes(w.bytes);
        ReleaseSet(set);
    }
    std::string written;
    {
        void* const set = NewTrackText(factory, kTkdtInstance);
        const uint32_t data[] = {0x00000042u, 0xABCDEF01u, 0u,          1u,          0xFFFFFFFFu, 0x80000000u, 7u,
                                 0x7FFFFFFFu, 0u,          123456u,     0x10u,       0x20u,       0xDEADBEEFu, 5u,
                                 0x0BADF00Du, 1u,          4000000000u, 9u,          10u,         11u,         12u,
                                 13u,         14u,         15u,         16u,         17u,         18u};
        for (std::size_t i = 0; i < std::size(kTkdtKeys); ++i)
            if (i != 12)
                SetUint32(set, kTkdtKeys[i], data[i]);
        Value f = Float32(3.5f);
        uint32_t key = kTkdtKeys[3];
        Virtual<uint32_t>(set, 0xC, &key, &f);
        Value b = Bool(1, 0x00ABCD00u);
        key = kTkdtKeys[10];
        Virtual<uint32_t>(set, 0xC, &key, &b);
        StringValue s("17");
        key = kTkdtKeys[17];
        Virtual<uint32_t>(set, 0xC, &key, &s.value);
        Value s32 = Sint32(-77);
        key = kTkdtKeys[19];
        Virtual<uint32_t>(set, 0xC, &key, &s32);
        FakeStream w = Writer();
        MixByte(Virtual<uint32_t>(set, 0x34, &w, uint32_t{0}, uint32_t{0}));
        MixBytes(w.bytes);
        written.assign(w.bytes.begin(), w.bytes.end());
        FakeStream f1 = Writer();
        f1.failAt = 1;
        MixByte(Virtual<uint32_t>(set, 0x34, &f1, uint32_t{0}, uint32_t{0}));
        MixBytes(f1.bytes);
        ReleaseSet(set);
        void* const plain = NewSet();
        SetUint32(plain, kTkdtKeys[0], 0x99u);
        SetUint32(plain, kTkdtKeys[26], 0x98u);
        FakeStream w2 = Writer();
        MixByte(CallThis<uint32_t>(addr::cGZPropSetTrack_WriteText, plain, &w2, uint32_t{0}, uint32_t{0}));
        MixBytes(w2.bytes);
        ReleaseSet(plain);
    }
    // Read back, with a leading length dword (and its reads failing in
    // turn) and as `TKD2`; every truncation; no seekable interface; through
    // the key interface's slot 12.
    const std::vector<uint8_t> bytes = TkdtBytes(0x0BADBEEFu, written);
    ReadTkdt(factory, bytes);
    {
        std::vector<uint8_t> prefixed = TkdtBytes(static_cast<uint32_t>(written.size() + 4), "");
        const uint32_t second = 0x00005555u;
        const auto* const p = reinterpret_cast<const uint8_t*>(&second);
        prefixed.insert(prefixed.end(), p, p + 4);
        prefixed.insert(prefixed.end(), written.begin(), written.end());
        ReadTkdt(factory, prefixed);
        for (int k = 1; k <= 3; ++k) {
            FakeStream f = Reader(prefixed);
            f.failAt = k;
            ReadTkdt(factory, f);
        }
        ReadTkdt(factory, TkdtBytes(0x544B4432u, "12345678" + written));
    }
    for (std::size_t n = 0; n < bytes.size(); n += (n < 48 ? 1 : 5))
        ReadTkdt(factory, std::vector<uint8_t>(bytes.begin(), bytes.begin() + n));
    {
        FakeStream q = Reader(bytes);
        q.refuseSeekable = true;
        ReadTkdt(factory, q);
        void* const set = NewTrackText(factory, kTkdtInstance);
        void* key = nullptr;
        Virtual<uint32_t>(set, 0x0, 0x3B93C5E7u, &key);
        FakeStream r = Reader(bytes);
        MixByte(Virtual<uint32_t>(key, 0x30, &r, uint32_t{0}, uint32_t{0}));
        MixSet(set);
        Virtual<uint32_t>(key, 0x8);
        ReleaseSet(set);
    }
    // The versions; the first value equal to the object's instance; the
    // 0xFF891EAA default each way; malformed text.
    const std::string texts[] = {
        TkdtFields("TKDT,0,name,", 30, "ETKD"),
        TkdtFields("TKDT,1,name,", 30, "ETKD"),
        TkdtFields("TKDT,2,name,", 30, "ETKD"),
        TkdtFields("TKDT,3,name,", 26, ""),
        "TKDT,2,n,4660,1,2,3,4,5,6,7,8,9,ETKD",
        "TKDT,2,n,0x1234,1,2,ETKD",
        "TKDT,2,n,,,,,,,0,,,1,ETKD",
        "TKDT,2,n,,,,,,,9,,0,1,ETKD",
        "TKDT,2,n,,,,,,,9,,,1,ETKD",
        "TKDT,2,n,,,,,,,,,abc,1,ETKD",
        "TKDT,2,n,-5, 7,0x,0xZZ,99999999999,,abc,-0x10,+3,0X1F,1e3,,,,,,,,,,,,,,,,,,,,,,,,,ETKD",
        "garbage",
        "",
        ",,,,,,,,,,",
        "TKDT",
        "TKDT,",
        "TKDT,2",
        "TKDT,2,",
        "TKDT,2,name",
        "TKDT,2,name,",
        "TKDT,2,name,7",
        "TKDT;2;name;7;8;",
        "TKDT,two,name,1,2,3,4,5,6,7,8,9,10,11,12,ETKD",
    };
    for (const std::string& text : texts)
        ReadTkdt(factory, TkdtBytes(0, text));
    ReadTkdt(factory, std::vector<uint8_t>{0x01, 0x02, 0x03});
}

// A bag's bytes as its write lays them out: version 1, the count, the
// entries.
std::vector<uint8_t> BagBytes()
{
    const uint32_t words[] = {1, 4, 21, 22, 23, 24};
    std::vector<uint8_t> bytes(sizeof words);
    std::memcpy(bytes.data(), words, sizeof words);
    return bytes;
}

// The factory: CreateFromResource for each class, WriteToStream.
void DriveFactory(char* factory, const std::vector<uint8_t>& binary, const std::vector<uint8_t>& xml)
{
    const struct {
        uint32_t type;
        uint32_t iid;
        const std::vector<uint8_t>* bytes;
        bool refuse;
    } cases[] = {
        {0xEBFEE349u, 0xEBB10CDDu, &binary, false}, {0xEBFEE345u, 0xEBB10CDDu, &xml, false},
        {0x0B9EB87Eu, 0xEBB10CDDu, &binary, false}, {0x0B9EB87Eu, 0x3B93C5E7u, &xml, false},
        {0x7B1ACFCDu, 0xDB1ACFB2u, nullptr, false}, {0xA3CD96CFu, 0x3B93C5E7u, &binary, false},
        {0x12345678u, 0xEBB10CDDu, &binary, false}, {0xEBFEE349u, 0xEBB10CDDu, &binary, true},
        {0xEBFEE349u, 0x77777777u, &binary, false},
    };
    const std::vector<uint8_t> bag = BagBytes();
    for (const auto& c : cases) {
        FakeStream stream = Reader(c.bytes ? *c.bytes : bag);
        FakeResource resource{kResourceVtable, 0, {c.type, 0x00001111u, 0x00002222u}, &stream, c.refuse};
        void* out = nullptr;
        MixByte(Virtual<uint32_t>(factory, 0xC, &resource, c.iid, &out, uint32_t{0}, uint32_t{0}));
        Mix(out ? 1 : 0);
        Mix(stream.refs);
        if (!out)
            continue;
        if (c.iid == 0xEBB10CDDu) {
            MixSet(out);
        } else if (c.iid == 0xDB1ACFB2u) {
            Mix(Virtual<uint32_t>(out, 0x24));
        } else {
            uint32_t k[3] = {};
            Virtual<uint32_t*>(out, 0xC, k);
            Mix(k[0]);
            Mix(k[1]);
            Mix(k[2]);
        }
        // WriteToStream: the object's key interface's write.
        if (c.type != 0x12345678u) {
            FakeStream target = Writer();
            MixByte(Virtual<uint32_t>(factory, 0x18, out, &target));
            MixBytes(target.bytes);
            target.refuseTarget = true;
            MixByte(Virtual<uint32_t>(factory, 0x18, out, &target));
        }
        uint32_t n = 0;
        while (Virtual<uint32_t>(out, 0x8) != 0)
            ++n;
        Mix(n);
    }
}

// The window of class 0xA3CD96CF: every interface's QueryInterface, AddRef
// and Release, the key interface, and released to its end.
void DriveWindow(char* factory)
{
    const uint32_t key[3] = {0xA3CD96CFu, 0x0000AAAAu, 0x0000BBBBu};
    char* const window = static_cast<char*>(CallThis<void*>(addr::cGZKeyedWin_Construct, New(0x114), key));
    const uint32_t iids[] = {0x3B93C5E7u, 0x456B8F1Du, 1u,          0xFD584467u, 0x5B34F0AFu, 0xAA11A64Fu,
                             0xBB1BE367u, 0x206077D2u, 0x306077D2u, 0xDD583E2Du, 0xE9D00A9Au, 0x0BADF00Du};
    const std::size_t parts[] = {0x0, 0x4, 0x8, 0xC, 0x10, 0x14, 0x18, 0x1C, 0x104};
    for (const std::size_t part : parts) {
        for (const uint32_t iid : iids) {
            void* out = nullptr;
            MixByte(Virtual<uint32_t>(window + part, 0x0, iid, &out));
            MixOffset(out, window);
        }
        Mix(Virtual<uint32_t>(window + part, 0x4));
        Mix(Virtual<uint32_t>(window + part, 0x8));
    }
    char* const keyPart = window + 0x104;
    Mix(Virtual<uint32_t>(keyPart, 0x2C));
    uint32_t k[3] = {};
    Virtual<uint32_t*>(keyPart, 0xC, k);
    Mix(k[0]);
    Mix(k[1]);
    Mix(k[2]);
    MixByte(Virtual<uint32_t>(keyPart, 0x20, uint32_t{0x01020304}));
    MixByte(Virtual<uint32_t>(keyPart, 0x28, uint32_t{0x05060708}));
    Virtual<uint32_t*>(keyPart, 0xC, k);
    Mix(k[0]);
    Mix(k[2]);
    uint32_t n = 0;
    while (Virtual<uint32_t>(window, 0x8) != 0)
        ++n;
    Mix(n);
    // One by the factory's Create.
    void* made = nullptr;
    MixByte(Virtual<uint32_t>(factory, 0x10, 0xA3CD96CFu, 0x3B93C5E7u, &made, uint32_t{0}, uint32_t{0}));
    Virtual<uint32_t*>(made, 0xC, k);
    Mix(k[0]);
    n = 0;
    while (Virtual<uint32_t>(made, 0x8) != 0)
        ++n;
    Mix(n);
}

// The keyed shuffle bag: entries, the count, the threshold, the interfaces,
// the key, a clone, written and read back.
void DriveBag()
{
    const uint32_t key[3] = {0x7B1ACFCDu, 3, 4};
    char* const bag = static_cast<char*>(CallThis<void*>(addr::cGZKeyedShuffleBag_Construct, New(0x28), key));
    Mix(Virtual<uint32_t>(bag, 0x4));
    for (uint32_t v = 10; v < 15; ++v)
        MixByte(Virtual<uint32_t>(bag, 0x10, v));
    Mix(Virtual<uint32_t>(bag, 0x24));
    Mix(At<uint32_t>(bag, 0x10));
    MixByte(Virtual<uint32_t>(bag, 0x1C, uint32_t{3}));
    Mix(At<uint32_t>(bag, 0x10));
    Mix(At<uint8_t>(bag, 0x14));
    for (const uint32_t iid : {0x3B93C5E7u, 0x456B8F1Du, 1u, 0xDB1ACFB2u, 0x00000999u}) {
        void* out = nullptr;
        MixByte(Virtual<uint32_t>(bag, 0x0, iid, &out));
        MixOffset(out, bag);
    }
    char* const keyPart = bag + 0x18;
    {
        void* out = nullptr;
        MixByte(Virtual<uint32_t>(keyPart, 0x0, 0xDB1ACFB2u, &out));
        MixOffset(out, bag);
        Mix(Virtual<uint32_t>(keyPart, 0x4));
        Mix(Virtual<uint32_t>(keyPart, 0x8));
        Mix(Virtual<uint32_t>(keyPart, 0x2C));
        uint32_t k[3] = {};
        Virtual<uint32_t*>(keyPart, 0xC, k);
        Mix(k[0]);
        Mix(k[1]);
        Mix(k[2]);
        MixByte(Virtual<uint32_t>(keyPart, 0x20, uint32_t{0x7B1ACFCDu}));
        MixByte(Virtual<uint32_t>(keyPart, 0x28, uint32_t{0x44}));
    }
    // A clone: the count copied, then one more for its interface.
    void* clone = nullptr;
    MixByte(Virtual<uint32_t>(keyPart, 0x38, 0xDB1ACFB2u, &clone));
    Mix(Virtual<uint32_t>(clone, 0x24));
    Mix(At<uint32_t>(clone, 0x4));
    Mix(At<uint32_t>(clone, 0x10));
    Mix(At<uint8_t>(clone, 0x14));
    Mix(At<uint32_t>(clone, 0x24));
    // Written and read into a third bag.
    FakeStream w = Writer();
    MixByte(Virtual<uint32_t>(keyPart, 0x34, &w, uint32_t{0}, uint32_t{0}));
    MixBytes(w.bytes);
    char* const third = static_cast<char*>(CallThis<void*>(addr::cGZKeyedShuffleBag_Construct, New(0x28), key));
    Virtual<uint32_t>(third, 0x4);
    FakeStream r = Reader(w.bytes);
    MixByte(Virtual<uint32_t>(third + 0x18, 0x30, &r, uint32_t{0}, uint32_t{0}));
    Mix(Virtual<uint32_t>(third, 0x24));
    for (char* b : {bag, static_cast<char*>(clone), third}) {
        uint32_t n = 0;
        while (Virtual<uint32_t>(b, 0x8) != 0)
            ++n;
        Mix(n);
    }
}

// The UI script handler's SetText, on an object laid out as it expects.
void DriveScriptHandler()
{
    static const SinkVtable vtable;
    struct {
        void* vtable;
        char pad[0x18];
        FakeTextSink sink;
    } handler{nullptr, {}, {vtable.slots}};
    for (const char* text : {"Hello script", "", "x"})
        MixByte(CallThis<uint32_t>(addr::cGZUIScriptHandler_SetText, &handler, text, uint32_t{0x55}));
}

// ====================================================================================
// Part u3

// A game string on the stack, destroyed by the game's destructor.
struct LocalString {
    alignas(4) char bytes[0x14];
    LocalString() { CallThis<void*>(addr::SC4String_SC4String, bytes); }
    explicit LocalString(const char* text) { CallThis<void*>(addr::SC4String_FromText, bytes, text); }
    ~LocalString() { CallThis<void>(addr::SC4String_Destroy, bytes); }
    LocalString(const LocalString&) = delete;
    LocalString& operator=(const LocalString&) = delete;
};

// A property set as the XML writer uses it (GetFirst, GetNext), over values
// of any type: a value object here is its vtable, its data at +4 and the
// type its slot 4 answers.
struct FakeValue {
    void* const* vtable;
    uint32_t data;
    uint32_t type;
};
uint32_t __fastcall FakeValueType(FakeValue* self, void*)
{
    Mix(0x5410);
    return self->type;
}
struct FakeValueVtable {
    void* slots[5];
    FakeValueVtable()
    {
        for (void*& slot : slots)
            slot = reinterpret_cast<void*>(&StreamTrap);
        slots[4] = reinterpret_cast<void*>(&FakeValueType);
    }
};
const FakeValueVtable& ValueVtable()
{
    static const FakeValueVtable vtable;
    return vtable;
}
FakeValue Fake(uint32_t type, uint32_t data) { return {ValueVtable().slots, data, type}; }

struct FakeSet {
    void* const* vtable;
    std::vector<std::pair<uint32_t, void*>> items;
    std::size_t at = 0;
};
uint32_t __fastcall FakeSetFirst(FakeSet* self, void*, uint32_t* key, void** value)
{
    Mix(0x5318);
    self->at = 0;
    if (self->items.empty())
        return 0x00ABCD00u;
    *key = self->items[0].first;
    *value = self->items[0].second;
    return 1;
}
uint32_t __fastcall FakeSetNext(FakeSet* self, void*, uint32_t* key, void** value)
{
    Mix(0x531C);
    if (++self->at >= self->items.size())
        return 0x00ABCE00u;
    *key = self->items[self->at].first;
    *value = self->items[self->at].second;
    return 1;
}
struct FakeSetVtable {
    void* slots[8];
    FakeSetVtable()
    {
        for (void*& slot : slots)
            slot = reinterpret_cast<void*>(&StreamTrap);
        slots[6] = reinterpret_cast<void*>(&FakeSetFirst);
        slots[7] = reinterpret_cast<void*>(&FakeSetNext);
    }
};
FakeSet NewFakeSet()
{
    static const FakeSetVtable vtable;
    return FakeSet{vtable.slots, {}, 0};
}

uint32_t FloatBits(float f)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &f, 4);
    return bits;
}

// The XML writer through the object's slot 4 and by address (every write
// failing in turn, an empty set, a type it does not know), its root name,
// and the handler's StartElement before and after the root.
void DriveXmlText()
{
    alignas(4) char xml[0x54];
    CallThis<void*>(addr::cGZPropSetXmlIO_Construct, xml);
    StringValue hello("hello");
    StringValue empty("");
    StringValue marks("a<b>&\"c\" 'd'");
    FakeValue values[] = {
        Fake(0xEB61E4F7u, 0),           Fake(0xEB61E4F7u, 0xFFFFFFFFu),         Fake(0xEB61E4F7u, 7),
        Fake(0x0C264712u, 0xFFFFCFC7u), Fake(0xABC78708u, FloatBits(2.5f)),     Fake(0xABC78708u, 0x80000000u),
        Fake(0xABC78708u, 0x7149F2CAu), Fake(0xABC78708u, 0x7F7FFFFFu),         Fake(0xABC78708u, 0x7FC00000u),
        Fake(0xABC78708u, 0x00000001u), Fake(0xABC78708u, FloatBits(-1.0f)),    Fake(0xCBA908E1u, 1),
        Fake(0xCBA908E1u, 0xABCDEF00u), Fake(0xCBA908E1u, 0x00000002u),
    };
    FakeSet set = NewFakeSet();
    uint32_t key = 0x10;
    for (FakeValue& value : values)
        set.items.push_back({key++, &value});
    set.items.push_back({0x80000000u, &hello.value});
    set.items.push_back({0xFFFFFFFFu, &empty.value});
    set.items.push_back({0x00000000u, &marks.value});
    FakeStream w = Writer();
    MixByte(Virtual<uint32_t>(xml, 0x10, &w, &set, uint32_t{0x99}));
    MixBytes(w.bytes);
    const int ops = w.ops;
    Mix(static_cast<uint32_t>(ops));
    for (int k = 1; k <= ops + 1; ++k) {
        FakeStream f = Writer();
        f.failAt = k;
        MixByte(CallThis<uint32_t>(addr::cGZPropSetXmlIO_Write, xml, &f, &set, uint32_t{0}));
        MixBytes(f.bytes);
    }
    // A type the writer does not know, last and in the middle.
    FakeValue odd = Fake(0x12345678u, 5);
    FakeSet tail = NewFakeSet();
    tail.items = {{1, &values[2]}, {2, &odd}};
    FakeStream t = Writer();
    MixByte(Virtual<uint32_t>(xml, 0x10, &t, &tail, uint32_t{0}));
    MixBytes(t.bytes);
    tail.items = {{1, &odd}, {2, &values[2]}};
    FakeStream m = Writer();
    MixByte(Virtual<uint32_t>(xml, 0x10, &m, &tail, uint32_t{0}));
    MixBytes(m.bytes);
    // An empty set: GetFirst's EAX.
    FakeSet none = NewFakeSet();
    FakeStream e = Writer();
    Mix(Virtual<uint32_t>(xml, 0x10, &e, &none, uint32_t{0}));
    MixBytes(e.bytes);
    // A real set by address.
    {
        void* const real = NewSet();
        FillSet(real, "Real & <set>");
        FakeStream r = Writer();
        MixByte(CallThis<uint32_t>(addr::cGZPropSetXmlIO_Write, xml, &r, real, uint32_t{0}));
        MixBytes(r.bytes);
        ReleaseSet(real);
    }
    // The root name into a string that has text.
    {
        LocalString out("previous text");
        uint32_t unused = 0;
        CallThis<uint32_t>(addr::cGZPropSetXmlIO_RootName, xml, out.bytes, &unused);
        MixString(out.bytes);
    }
    // StartElement: not the root, the root, then attributes.
    char* const handler = xml + 0x4;
    const auto start = [&](const char* name, const char* const* attributes) {
        Virtual<uint32_t>(xml + 0x3C, 0xC, "old text", uint32_t{8});
        MixByte(Virtual<uint32_t>(handler, 0xC, name, attributes, uint32_t{0}, uint32_t{0}));
        Mix(At<uint8_t>(xml, 0x10));
        Mix(At<uint32_t>(xml, 0xC));
        MixString(xml + 0x14);
        MixString(xml + 0x28);
        MixString(xml + 0x3C);
    };
    const char* const keyType[] = {"key", "0x10", "type", "0xEB61E4F7", nullptr};
    const char* const cased[] = {"KEY", "A", "Type", "B", "other", "C", nullptr};
    const char* const reversed[] = {"type", "T2", "key", "K2", "key", "K3", nullptr};
    const char* const nothing[] = {nullptr};
    start("AnyUint32", keyType);
    start("cGZPropertySetUint3", keyType);
    start("cGZPropertySetUint32 ", nothing);
    start("cGZPropertySetUint32", keyType);
    start("cGZPropertySetUint32", keyType);
    start("AnyUint32", nullptr);
    start("AnyUint32", cased);
    start("AnyString", reversed);
    start("AnyString", nothing);
    At<uint8_t>(xml, 0x10) = 0;
    start("cgzpropertysetuint32", keyType);
    CallThis<void>(addr::cGZPropSetXmlIO_Destruct, xml);
}

// The UI script property handler: elements with attributes, the text, each
// property type, the refusals, through its interface's slots 3 to 5.
void DriveScriptProperties()
{
    char* const handler = static_cast<char*>(CallThis<void*>(addr::cGZUIScriptHandler_Construct, New(0x30)));
    void* const set = NewSet();
    At<void*>(handler, 0xC) = set;
    Virtual<uint32_t>(handler, 0x4);
    const auto start = [&](const char* const* attributes, uint32_t count) {
        MixByte(Virtual<uint32_t>(handler, 0xC, "Property", attributes, count, uint32_t{0}));
        MixString(handler + 0x1C);
    };
    const auto end = [&](const char* element) {
        MixByte(Virtual<uint32_t>(handler, 0x10, element));
        MixSet(set);
    };
    const char* const speed[] = {"Name", "Speed", "Value", "123"};
    const char* const ratio[] = {"Name", "Ratio", "Value", "2.5"};
    const char* const title[] = {"Name", "Title", "Value", "Hello, world"};
    const char* const named[] = {"Name", "Text"};
    const char* const bad[] = {"Name", "Bad", "Value", "x12"};
    const char* const blank[] = {"Name", "Blank", "Value", ""};
    const char* const minus[] = {"Name", "Minus", "Value", "-5"};
    const char* const space[] = {"Name", "Space", "Value", " 7"};
    const char* const others[] = {"Foo", "Bar", "Name", "Other", "Value", "0x1F", "Name", "Second"};
    const char* const hex[] = {"Value", "0xFFFFFFFF", "Name", "Hex"};
    const char* const tiny[] = {"Name", "Tiny", "Value", "1e-3"};
    start(speed, 4);
    end("PropertyUint32Uint32");
    start(ratio, 4);
    end("PropertyUint32Float32");
    start(title, 4);
    end("PropertyUint32String");
    start(named, 2);
    MixByte(Virtual<uint32_t>(handler, 0x14, "from the text", uint32_t{13}));
    end("PropertyUint32String");
    start(named, 2);
    MixByte(Virtual<uint32_t>(handler, 0x14, "42", uint32_t{2}));
    end("PropertyUint32Uint32");
    start(bad, 4);
    end("PropertyUint32Uint32");
    end("PropertyUint32String"); // the map kept by the refusal
    start(blank, 4);
    end("PropertyUint32Float32");
    end("PropertyStringString");
    start(minus, 4);
    end("PropertyUint32Uint32");
    start(space, 4);
    end("PropertyUint32Float32");
    end("Unknown");
    start(nullptr, 0);
    end("PropertyUint32String");
    start(others, 8);
    end("PropertyUint32Uint32");
    start(hex, 4);
    end("PropertyUint32Uint32");
    start(tiny, 4);
    end("PropertyUint32Float32");
    start(title, 4);
    end("PropertyStringString");
    end("PropertyUint32String");
    Mix(Virtual<uint32_t>(handler, 0x8));
}

// The factory's Create for every class, an interface the object has not got,
// a type it does not know; by its slot and by address.
void DriveCreate(char* factory)
{
    const struct {
        uint32_t type;
        uint32_t iid;
    } cases[] = {
        {0x0B8D821Au, 0xEBB10CDDu}, {0x0B9EB87Eu, 0xEBB10CDDu}, {0xEBFEE345u, 0xEBB10CDDu}, {0xEBFEE349u, 0x3B93C5E7u},
        {0x7B1ACFCDu, 0xDB1ACFB2u}, {0xA3CD96CFu, 0x3B93C5E7u}, {0x0B8D821Au, 0x77777777u}, {0x7B1ACFCDu, 0x77777777u},
        {0xA3CD96CFu, 0x77777777u}, {0xEBFEE345u, 0x77777777u}, {0x12345678u, 0xEBB10CDDu}, {0u, 1u},
    };
    for (const auto& c : cases) {
        for (int byAddress = 0; byAddress < 2; ++byAddress) {
            void* out = nullptr;
            const uint32_t answer =
                byAddress ? CallThis<uint32_t>(addr::cGZPropSetFactory_Create, factory, c.type, c.iid, &out,
                                               uint32_t{0x11}, uint32_t{0x22})
                          : Virtual<uint32_t>(factory, 0x10, c.type, c.iid, &out, uint32_t{0}, uint32_t{0});
            MixByte(answer);
            if (!out) {
                if (c.type == 0x12345678u || c.type == 0u)
                    Mix(answer);
                continue;
            }
            if (c.iid == 0xEBB10CDDu)
                MixSet(out);
            else if (c.iid == 0x3B93C5E7u) {
                uint32_t k[3] = {};
                Virtual<uint32_t*>(out, 0xC, k);
                Mix(k[0]);
                Mix(k[1]);
                Mix(k[2]);
            } else
                Mix(Virtual<uint32_t>(out, 0x24));
            uint32_t n = 0;
            while (Virtual<uint32_t>(out, 0x8) != 0)
                ++n;
            Mix(n);
        }
    }
}

// A window 0xA3CD96CF's plain members (the ones the destructor leaves
// alone; the reference count at +0x50 among them), its string and its key.
void MixWindow(char* window)
{
    for (std::size_t offset = 0x38; offset <= 0x74; offset += 4)
        if (offset != 0x58 && offset != 0x60)
            Mix(At<uint32_t>(window, offset));
    for (const std::size_t offset : {0x58u, 0x60u, 0x9Cu, 0x9Du, 0x9Eu, 0xC4u, 0x100u})
        Mix(At<uint8_t>(window, offset));
    MixString(window + 0x20);
    for (std::size_t offset = 0x108; offset <= 0x110; offset += 4)
        Mix(At<uint32_t>(window, offset));
}

void ReleaseWindow(char* window)
{
    uint32_t n = 0;
    while (Virtual<uint32_t>(window, 0x8) != 0)
        ++n;
    Mix(n);
}

// The window's Clone through its key interface's slot 14, CopyAssign and
// the script window's Assign by address (into another window and into
// itself), the part's Assign.
void DriveWinCopy()
{
    const uint32_t key[3] = {0xA3CD96CFu, 0x0000ABCDu, 0x00001234u};
    char* const window = static_cast<char*>(CallThis<void*>(addr::cGZKeyedWin_Construct, New(0x114), key));
    Virtual<uint32_t>(window, 0x4);
    // Not the reference count at +0x50 (which Assign copies too).
    for (std::size_t offset = 0x38; offset <= 0x74; offset += 4)
        if (offset != 0x50 && offset != 0x58 && offset != 0x60)
            At<uint32_t>(window, offset) = static_cast<uint32_t>(offset * 3 + 1);
    for (const std::size_t offset : {0x58u, 0x60u, 0x9Cu, 0x9Du, 0x9Eu, 0xC4u, 0x100u})
        At<uint8_t>(window, offset) = static_cast<uint8_t>(offset + 1);
    Virtual<uint32_t>(window + 0x20, 0xC, "Caption", uint32_t{7});
    MixWindow(window);
    char* const keyPart = window + 0x104;
    for (const uint32_t iid : {0x3B93C5E7u, 1u, 0x456B8F1Du}) {
        void* out = nullptr;
        MixByte(Virtual<uint32_t>(keyPart, 0x38, iid, &out));
        char* const clone = static_cast<char*>(out) - (iid == 1u ? 0 : 0x104);
        MixWindow(clone);
        ReleaseWindow(clone);
    }
    const uint32_t other[3] = {0xA3CD96CFu, 7, 8};
    char* const second = static_cast<char*>(CallThis<void*>(addr::cGZKeyedWin_Construct, New(0x114), other));
    Virtual<uint32_t>(second, 0x4);
    MixWindow(second);
    Mix(CallThis<char*>(addr::cGZKeyedWin_CopyAssign, second, window) == second ? 1 : 0);
    MixWindow(second);
    Mix(CallThis<char*>(addr::cGZKeyedWin_CopyAssign, second, second) == second ? 1 : 0);
    MixWindow(second);
    At<uint32_t>(window, 0x40) = 0x5555;
    Virtual<uint32_t>(window + 0x20, 0xC, "Other", uint32_t{5});
    Mix(CallThis<char*>(addr::cGZScriptWin_Assign, second, window) == second ? 1 : 0);
    MixWindow(second);
    Mix(CallThis<char*>(addr::cGZScriptWin_Assign, window, window) == window ? 1 : 0);
    MixWindow(window);
    Mix(CallThis<char*>(addr::cGZScriptWinPart_Assign, second + 0xC8, window + 0xC8) == second + 0xC8 ? 1 : 0);
    Mix(At<uint32_t>(second, 0xE0));
    Mix(At<uint32_t>(second, 0xE4));
    Mix(At<uint8_t>(second, 0xE8));
    ReleaseWindow(second);
    ReleaseWindow(window);
}

// The registry of sound kinds, or null (the game's getter; a muted game has
// none).
void* SoundKindRegistry() { return reinterpret_cast<void*(__cdecl*)()>(addr::SC4_SoundKindRegistry)(); }

// The bag's entries as Get finds them, in order.
void MixEntries(void* bag)
{
    const uint32_t count = Virtual<uint32_t>(bag, 0x24);
    Mix(count);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t value = 0;
        MixByte(Virtual<uint32_t>(bag, 0x2C, i, &value));
        Mix(value);
    }
}

// Only what does not depend on the random draws: the count, the sum and the
// XOR of the entries.
void MixContents(void* bag)
{
    const uint32_t count = Virtual<uint32_t>(bag, 0x24);
    uint32_t sum = 0;
    uint32_t bits = 0;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t value = 0;
        Virtual<uint32_t>(bag, 0x2C, i, &value);
        sum += value;
        bits ^= value;
    }
    Mix(count);
    Mix(sum);
    Mix(bits);
}

uint32_t ListSize(void* list)
{
    char* const head = *static_cast<char**>(list);
    uint32_t n = 0;
    for (char* node = *reinterpret_cast<char**>(head); node != head; node = *reinterpret_cast<char**>(node))
        ++n;
    return n;
}

void MixLists(char* bag)
{
    Mix(ListSize(bag + 0x8));
    Mix(ListSize(bag + 0xC));
    Mix(At<uint32_t>(bag, 0x10));
    Mix(At<uint8_t>(bag, 0x14));
}

char* NewBag()
{
    char* const bag = static_cast<char*>(CallThis<void*>(addr::cGZShuffleBag_Construct, New(0x18)));
    Virtual<uint32_t>(bag, 0x4);
    return bag;
}

void ReleaseBag(char* bag)
{
    uint32_t n = 0;
    while (Virtual<uint32_t>(bag, 0x8) != 0)
        ++n;
    Mix(n);
}

std::vector<uint8_t> Words(std::initializer_list<uint32_t> words)
{
    std::vector<uint8_t> bytes(words.size() * 4);
    std::memcpy(bytes.data(), words.begin(), bytes.size());
    return bytes;
}

std::vector<uint8_t> TextForm(const std::string& text, int32_t lengthDelta = 0)
{
    std::vector<uint8_t> bytes = Words({static_cast<uint32_t>(text.size()) + static_cast<uint32_t>(lengthDelta)});
    bytes.insert(bytes.end(), text.begin(), text.end());
    return bytes;
}

// The shuffle bag through its slots and by address: entries added, found,
// replaced, erased and removed; draws (only what does not depend on the
// random numbers goes into the hash); its three read forms and the bad
// ones; the write; the interfaces; the destructors.
void DriveShuffleBag()
{
    const uint32_t live = At<uint32_t>(reinterpret_cast<void*>(addr::cGZShuffleBag_LiveCount), 0);
    char* const bag = NewBag();
    Mix(At<uint32_t>(reinterpret_cast<void*>(addr::cGZShuffleBag_LiveCount), 0) - live);
    MixLists(bag);
    for (uint32_t v = 1; v <= 9; ++v) {
        MixByte(Virtual<uint32_t>(bag, 0x10, v * 11));
        MixLists(bag);
    }
    MixByte(Virtual<uint32_t>(bag, 0x10, uint32_t{33}));
    MixEntries(bag);
    for (const uint32_t index : {0u, 3u, 9u, 10u, 11u, 0xFFFFFFFFu}) {
        uint32_t value = 0xEEEEEEEEu;
        const uint32_t answer = Virtual<uint32_t>(bag, 0x2C, index, &value);
        MixByte(answer);
        if (!(answer & 0xFF))
            Mix(answer);
        Mix(value);
    }
    for (const uint32_t index : {2u, 9u, 10u, 50u}) {
        const uint32_t answer = Virtual<uint32_t>(bag, 0x38, index, index + 1000);
        MixByte(answer);
        if (!(answer & 0xFF))
            Mix(answer);
    }
    MixEntries(bag);
    for (const uint32_t index : {0u, 4u, 8u, 8u, 20u}) {
        const uint32_t answer = Virtual<uint32_t>(bag, 0x40, index);
        MixByte(answer);
        if (!(answer & 0xFF))
            Mix(answer);
        MixEntries(bag);
        MixLists(bag);
    }
    for (const uint32_t value : {33u, 44u, 12345u}) {
        MixByte(Virtual<uint32_t>(bag, 0x14, value));
        MixEntries(bag);
        MixLists(bag);
    }
    // Written and read back before any draw.
    FakeStream w = Writer();
    MixByte(Virtual<uint32_t>(bag, 0x50, &w, uint32_t{0}, uint32_t{0}));
    MixBytes(w.bytes);
    MixEntries(bag);
    // Draws: a threshold not fixed, then fixed low and high, the first
    // list run empty. They need the registry of sound kinds (its random
    // numbers), which a muted game has not got.
    const bool sound = SoundKindRegistry() != nullptr;
    Mix(sound ? 1 : 0);
    for (int i = 0; sound && i < 12; ++i) {
        const uint32_t drawn = Virtual<uint32_t>(bag, 0x20);
        Mix(drawn == 0 ? 0 : 1);
        MixLists(bag);
        MixContents(bag);
    }
    MixByte(Virtual<uint32_t>(bag, 0x1C, uint32_t{1}));
    for (int i = 0; sound && i < 6; ++i) {
        Virtual<uint32_t>(bag, 0x20);
        MixLists(bag);
    }
    // A high threshold: the first list drawn empty, then whole rounds of
    // the second (each round sums to the total).
    MixByte(Virtual<uint32_t>(bag, 0x1C, uint32_t{100}));
    for (uint32_t pending = sound ? ListSize(bag + 0x8) : 0; pending != 0; --pending) {
        Virtual<uint32_t>(bag, 0x20);
        MixLists(bag);
    }
    const uint32_t count = Virtual<uint32_t>(bag, 0x24);
    uint32_t sum = 0;
    for (uint32_t i = 0; sound && i < 3 * count; ++i) {
        sum += Virtual<uint32_t>(bag, 0x20);
        MixLists(bag);
    }
    Mix(sum);
    MixContents(bag);
    MixByte(Virtual<uint32_t>(bag, 0x14, uint32_t{55}));
    MixLists(bag);
    MixContents(bag);
    MixByte(Virtual<uint32_t>(bag, 0x18));
    MixLists(bag);
    MixEntries(bag);
    // Random on its own, by address: one entry, and a range.
    if (sound)
        Mix(CallThis<uint32_t>(addr::cGZShuffleBag_Random, bag, uint32_t{1}));
    for (int i = 0; sound && i < 8; ++i)
        Mix(CallThis<uint32_t>(addr::cGZShuffleBag_Random, nullptr, uint32_t{7}) < 7 ? 1 : 0);
    // The interfaces and the count.
    for (const uint32_t iid : {1u, 0xDB1ACFB2u, 0x3B93C5E7u, 0u}) {
        void* out = nullptr;
        MixByte(Virtual<uint32_t>(bag, 0x0, iid, &out));
        Mix(out == bag ? 1 : (out ? 2 : 0));
    }
    Mix(At<uint32_t>(bag, 0x4));
    // Read: the three forms, the bad sizes, the text forms.
    const std::vector<uint8_t> reads[] = {
        w.bytes,
        Words({1, 3, 7, 8, 9}),
        Words({3, 7, 8, 9}),
        Words({1, 1, 5}),
        Words({0}),
        Words({1, 0}),
        Words({2, 7, 8, 9}),
        Words({1, 3, 7, 8}),
        TextForm("1,2,3"),
        TextForm("5-9"),
        TextForm("10-12,20,30-30,3-1,0x10,abc"),
        TextForm("7,"),
        TextForm(",7"),
        TextForm("1,,2"),
        TextForm(""),
        TextForm("-4,8-,-,99999999999"),
        TextForm("4", 1),
        TextForm("1,2", -1),
    };
    for (const std::vector<uint8_t>& bytes : reads) {
        char* const into = NewBag();
        Virtual<uint32_t>(into, 0x10, uint32_t{0xAAAA});
        FakeStream r = Reader(bytes);
        MixByte(Virtual<uint32_t>(into, 0x4C, &r, uint32_t{0}, uint32_t{0}));
        Mix(r.refs);
        MixEntries(into);
        MixLists(into);
        ReleaseBag(into);
    }
    // The destructors on a bag of the stack: the deleting one with flag 0,
    // the plain one by address.
    {
        alignas(4) char local[0x18];
        CallThis<void*>(addr::cGZShuffleBag_Construct, local);
        Virtual<uint32_t>(local, 0x10, uint32_t{1});
        Mix(Virtual<char*>(local, 0x44, uint32_t{0}) == local ? 1 : 0);
        Mix(At<uint32_t>(reinterpret_cast<void*>(addr::cGZShuffleBag_LiveCount), 0) - live);
        CallThis<void*>(addr::cGZShuffleBag_Construct, local);
        CallThis<void>(addr::cGZShuffleBag_Destruct, local);
        Mix(At<uint32_t>(reinterpret_cast<void*>(addr::cGZShuffleBag_LiveCount), 0) - live);
    }
    ReleaseBag(bag);
    Mix(At<uint32_t>(reinterpret_cast<void*>(addr::cGZShuffleBag_LiveCount), 0) - live);
}

// A sound's value set (slot 3 Set(number, value, flag), slot 4 Get(number)):
// every call goes into the hash.
struct FakeValues {
    void* const* vtable;
    uint32_t values[0x48];
    uint32_t priority;
};
uint32_t __fastcall ValuesSet(FakeValues* self, void*, uint32_t number, uint32_t value, uint32_t flag)
{
    Mix(0x5503);
    Mix(number);
    Mix(value);
    Mix(flag);
    if (number < 0x48)
        self->values[number] = value;
    return 0x00AB0000u | (number & 0xFF);
}
uint32_t __fastcall ValuesGet(FakeValues* self, void*, uint32_t number)
{
    Mix(0x5504);
    Mix(number);
    return number < 0x48 ? self->values[number] : number == 0xFFAE9DDBu ? self->priority : 0xDDDDDDDDu;
}
struct FakeValuesVtable {
    void* slots[5];
    FakeValuesVtable()
    {
        for (void*& slot : slots)
            slot = reinterpret_cast<void*>(&StreamTrap);
        slots[3] = reinterpret_cast<void*>(&ValuesSet);
        slots[4] = reinterpret_cast<void*>(&ValuesGet);
    }
};
FakeValues NewValues(uint32_t seed, uint32_t priority)
{
    static const FakeValuesVtable vtable;
    FakeValues values{vtable.slots, {}, priority};
    for (uint32_t i = 0; i < 0x48; ++i)
        values.values[i] = seed * (i + 1);
    return values;
}

// The framework's own 2D and 3D sound kinds' slots 5 and 6, by address
// (slot 5 only with the registry of sound kinds, which a muted game has
// not got).
void DriveKinds()
{
    void* const registry = SoundKindRegistry();
    Mix(registry ? 1 : 0);
    const uint32_t limit = registry ? Virtual<uint32_t>(registry, 0x7C, 0xFFAE9DDBu) : 0;
    Mix(limit);
    uint32_t unused = 0;
    const struct {
        uint32_t v14;
        uint32_t v15;
        uint32_t v1f;
        uint32_t v38;
        uint32_t priority;
    } cases[] = {
        {1024, 512, 1, 0, 0},     {300, 7, 0, 0, 0x7FFFFFFFu}, {0xFFFFFC00u, 3, 0, 9, 0x80000000u},
        {0x7FFFFFFFu, 2, 5, 9, limit - 1}, {0x40000, 0x40000, 0, 0xFFFFFFFFu, limit},
    };
    for (const auto& c : cases) {
        FakeValues source = NewValues(3, c.priority);
        source.values[0x14] = c.v14;
        source.values[0x15] = c.v15;
        source.values[0x1F] = c.v1f;
        source.values[0x38] = c.v38;
        FakeValues voice = NewValues(0, 0);
        if (registry) {
            MixByte(CallThis<uint32_t>(addr::cGZSoundKind2D_SetUpVoice, &unused, &source, &voice));
            Mix(CallThis<uint32_t>(addr::cGZSoundKind3D_SetUpVoice, &unused, &source, &voice));
        }
        MixByte(CallThis<uint32_t>(addr::cGZSoundKind2D_CopyValues, &unused, &source, &voice));
        MixByte(CallThis<uint32_t>(addr::cGZSoundKind3D_CopyValues, &unused, &source, &voice));
        for (const uint32_t v : voice.values)
            Mix(v);
    }
    FakeValues same = NewValues(5, 0);
    MixByte(CallThis<uint32_t>(addr::cGZSoundKind3D_SetUpVoice, &unused, &same, &same));
}

// OPENSC4_TEST_PROPIO=<day>: part u2's functions called with fake streams
// and resources, through their slots and addresses: the binary and XML
// forms written and read back (truncated, refused, failing part way), the
// factory making and writing each class, the Track objects, the window
// 0xA3CD96CF's nine interfaces, the keyed shuffle bag and its clone, the
// UI script handler's SetText; and part u2b's TKDT text writer and reader. Every result goes into the fingerprint's
// `pio` part; addresses never do.
void DrivePropio(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_PROPIO") || !game::IsBuild641())
        return;
    g_pioOn = true;
    uint32_t* const saved = g_into;
    g_into = &g_pioHash;
    const std::vector<uint8_t> binary = DriveBinary();
    PioStage("binary");
    const std::vector<uint8_t> xml = DriveXml();
    PioStage("xml");
    alignas(4) char factory[8];
    CallThis<void*>(addr::cGZPropSetFactory_Construct, factory);
    Mix(At<uint32_t>(factory, 0x4));
    DriveTrack(factory, binary);
    PioStage("track");
    DriveFactory(factory, binary, xml);
    PioStage("factory");
    DriveTkdt(factory);
    PioStage("tkdt");
    DriveWindow(factory);
    PioStage("window");
    DriveBag();
    PioStage("bag");
    DriveScriptHandler();
    DriveXmlText();
    PioStage("xmltext");
    DriveScriptProperties();
    PioStage("script");
    DriveCreate(factory);
    PioStage("create");
    DriveWinCopy();
    PioStage("wincopy");
    DriveShuffleBag();
    PioStage("shufflebag");
    DriveKinds();
    PioStage("kinds");
    g_into = saved;
    log::Write("autotest: propio driven (hash %08X)", g_pioHash);
}

void PioFingerprint(cISC4City*, std::string& line)
{
    if (!g_pioOn)
        return;
    char text[32];
    std::snprintf(text, sizeof text, " | pio %08X", g_pioHash);
    line += text;
}

const FingerprintPart kPioFingerprint{"propio", 4, &PioFingerprint};
const DayHook kPioDrive{"propio", 4, &DrivePropio};

// ====================================================================================
// Part u4: the UI file parser 0xBA2E7953, its variable table 0xBA2E7954 and
// the directors (OPENSC4_TEST_XMLPARSE)

bool g_xpOn = false;
uint32_t g_xpHash = 2166136261u;

void XpStage(const char* name) { log::Write("autotest: xmlparse: %s %08X", name, g_xpHash); }

// A game string on the stack made from a C string, destroyed at the end.
struct GameText {
    alignas(4) char bytes[0x14];
    explicit GameText(const char* text) { CallThis<void*>(addr::SC4String_FromText, bytes, text); }
    ~GameText() { CallThis<void>(addr::SC4String_Destroy, bytes); }
    GameText(const GameText&) = delete;
    GameText& operator=(const GameText&) = delete;
    void* get() { return bytes; }
};

uint32_t XpRelease(void* object) { return Virtual<uint32_t>(object, 0x8); }

// A script value (0x14 bytes): its words at +0xC and +0xE (the type), and
// its data: a text's characters, an array's count, else the dword at +4.
void MixScriptValue(void* value)
{
    if (!value) {
        Mix(0xFFFFFFFFu);
        return;
    }
    const uint16_t type = At<uint16_t>(value, 0xE);
    Mix(At<uint16_t>(value, 0xC));
    Mix(type);
    Mix(At<uint32_t>(value, 0x8));
    if (type & 0x8000) {
        const uint32_t count = At<uint32_t>(value, 0x10);
        Mix(count);
        const char* const data = At<const char*>(value, 0x4);
        if (type == 0x800E && data)
            Mix(BytesChecksum(data, count));
    } else {
        Mix(At<uint32_t>(value, 0x4));
    }
}

void* NumberValue(uint32_t number)
{
    void* const memory = New(0x14);
    return CallThis<void*>(addr::SC4ScriptNumber_FromUint, memory, number);
}

void* TextValue(const char* text)
{
    GameText string(text);
    void* const memory = New(0x14);
    return CallThis<void*>(addr::SC4ScriptNumber_FromString, memory, string.get());
}

// A listener as the UI script tree is one: slots 3 to 5 the document's
// start, end and text; its interface 0x1A2BDEB9 a tag's start (3), the end
// of the start tag (4) and an attribute (5); 0x1A2BDEBA the attribute list
// at the end of the start tag (3). Each can be refused. Every event goes
// into the hash.
struct FakeListener;
struct FakeEvents {
    void* const* vtable;
    FakeListener* owner;
};
struct FakeListener {
    void* const* vtable;
    FakeEvents tags;
    FakeEvents attributes;
    int32_t refs;
    int32_t tagRefs;
    int32_t attributeRefs;
    bool offerTags;
    bool offerAttributes;
    void* parser;
    uint32_t events;
};

void MixParser(FakeListener* self, void* parser)
{
    ++self->events;
    Mix(parser == self->parser ? 1u : 0u);
}

uint32_t __fastcall ListenerQueryInterface(FakeListener* self, void*, uint32_t iid, void** out)
{
    Mix(0x5100);
    Mix(iid);
    if (iid == 0x1A2BDEB9 && self->offerTags) {
        *out = &self->tags;
        ++self->tagRefs;
        return 1;
    }
    if (iid == 0x1A2BDEBA && self->offerAttributes) {
        *out = &self->attributes;
        ++self->attributeRefs;
        return 1;
    }
    if (iid == 1) {
        *out = self;
        ++self->refs;
        return 1;
    }
    return 0;
}
uint32_t __fastcall ListenerAddRef(FakeListener* self, void*) { return static_cast<uint32_t>(++self->refs); }
uint32_t __fastcall ListenerRelease(FakeListener* self, void*) { return static_cast<uint32_t>(--self->refs); }
uint32_t __fastcall ListenerStart(FakeListener* self, void*, void* parser)
{
    Mix(0x5103);
    MixParser(self, parser);
    return 0x00510300u;
}
uint32_t __fastcall ListenerEnd(FakeListener* self, void*, void* parser, uint32_t done)
{
    Mix(0x5104);
    MixParser(self, parser);
    Mix(done);
    return 0x00510400u;
}
uint32_t __fastcall ListenerText(FakeListener* self, void*, void* parser, void* text)
{
    Mix(0x5105);
    MixParser(self, parser);
    MixString(text);
    return 0x00510500u;
}
uint32_t __fastcall EventsAddRef(FakeEvents* self, void*)
{
    int32_t& refs = self == &self->owner->tags ? self->owner->tagRefs : self->owner->attributeRefs;
    return static_cast<uint32_t>(++refs);
}
uint32_t __fastcall EventsRelease(FakeEvents* self, void*)
{
    int32_t& refs = self == &self->owner->tags ? self->owner->tagRefs : self->owner->attributeRefs;
    return static_cast<uint32_t>(--refs);
}
uint32_t __fastcall EventsTrap(FakeEvents*, void*)
{
    log::Write("autotest: xmlparse: unexpected events slot");
    Mix(0xDEAD);
    return 0;
}
uint32_t __fastcall TagBegin(FakeEvents* self, void*, void* parser, uint32_t tag)
{
    Mix(0x5203);
    MixParser(self->owner, parser);
    Mix(tag);
    return 0x00520300u;
}
uint32_t __fastcall TagEndStart(FakeEvents* self, void*, void* parser, uint32_t tag)
{
    Mix(0x5204);
    MixParser(self->owner, parser);
    Mix(tag);
    return 0x00520400u;
}
uint32_t __fastcall TagAttribute(FakeEvents* self, void*, void* parser, uint32_t name, void* value)
{
    Mix(0x5205);
    MixParser(self->owner, parser);
    Mix(name);
    MixScriptValue(value);
    return 0x00520500u;
}
uint32_t __fastcall AttributeList(FakeEvents* self, void*, void* parser, uint32_t tag, void* list)
{
    Mix(0x5303);
    MixParser(self->owner, parser);
    Mix(tag);
    const uint32_t* const begin = At<const uint32_t*>(list, 0);
    const uint32_t* const end = At<const uint32_t*>(list, 4);
    Mix(static_cast<uint32_t>(end - begin));
    for (const uint32_t* pair = begin; pair != end; pair += 2) {
        Mix(pair[0]);
        MixScriptValue(reinterpret_cast<void*>(static_cast<std::uintptr_t>(pair[1])));
    }
    return 0x00530300u;
}

struct ListenerVtables {
    void* main[6];
    void* tags[6];
    void* attributes[4];
    ListenerVtables()
        : main{reinterpret_cast<void*>(&ListenerQueryInterface), reinterpret_cast<void*>(&ListenerAddRef),
               reinterpret_cast<void*>(&ListenerRelease),        reinterpret_cast<void*>(&ListenerStart),
               reinterpret_cast<void*>(&ListenerEnd),            reinterpret_cast<void*>(&ListenerText)},
          tags{reinterpret_cast<void*>(&EventsTrap),    reinterpret_cast<void*>(&EventsAddRef),
               reinterpret_cast<void*>(&EventsRelease), reinterpret_cast<void*>(&TagBegin),
               reinterpret_cast<void*>(&TagEndStart),   reinterpret_cast<void*>(&TagAttribute)},
          attributes{reinterpret_cast<void*>(&EventsTrap), reinterpret_cast<void*>(&EventsAddRef),
                     reinterpret_cast<void*>(&EventsRelease), reinterpret_cast<void*>(&AttributeList)}
    {
    }
};

const ListenerVtables& Listeners()
{
    static const ListenerVtables vtables;
    return vtables;
}

void InitListener(FakeListener& listener, void* parser, bool tags, bool attributes)
{
    listener.vtable = Listeners().main;
    listener.tags = {Listeners().tags, &listener};
    listener.attributes = {Listeners().attributes, &listener};
    listener.refs = listener.tagRefs = listener.attributeRefs = 0;
    listener.offerTags = tags;
    listener.offerAttributes = attributes;
    listener.parser = parser;
    listener.events = 0;
}

void MixRefs(const FakeListener& listener)
{
    Mix(static_cast<uint32_t>(listener.refs));
    Mix(static_cast<uint32_t>(listener.tagRefs));
    Mix(static_cast<uint32_t>(listener.attributeRefs));
    Mix(listener.events);
}

// A counted object for SetContext.
struct FakeCounted {
    void* const* vtable;
    int32_t refs;
};
uint32_t __fastcall CountedAddRef(FakeCounted* self, void*) { return static_cast<uint32_t>(++self->refs); }
uint32_t __fastcall CountedRelease(FakeCounted* self, void*) { return static_cast<uint32_t>(--self->refs); }
void* const kCountedVtable[] = {reinterpret_cast<void*>(&EventsTrap), reinterpret_cast<void*>(&CountedAddRef),
                                reinterpret_cast<void*>(&CountedRelease)};

char* NewParser()
{
    char* const parser = reinterpret_cast<char*(__cdecl*)()>(addr::cGZUIParser_New)();
    Virtual<uint32_t>(parser, 0x4);
    return parser;
}

uint32_t ParserDepth(char* parser) { return static_cast<uint32_t>(At<char*>(parser, 0x18) - At<char*>(parser, 0x14)) >> 2; }

void MixParserState(char* parser, bool started = true)
{
    Mix(ParserDepth(parser));
    Mix(static_cast<uint32_t>(At<char*>(parser, 0x48) - At<char*>(parser, 0x44)) >> 3);
    Mix(At<uint32_t>(parser, 0x24));
    Mix(At<uint32_t>(parser, 0x2C) ? 1u : 0u);
    // The constructor sets neither the open tag's flag nor its number: the
    // number only once a tag is open, the flag only after a parse started.
    if (started) {
        Mix(At<uint8_t>(parser, 0x30));
        if (At<uint8_t>(parser, 0x30))
            Mix(At<uint32_t>(parser, 0x34));
    }
    Mix(At<uint8_t>(parser, 0x38));
    Mix(At<uint32_t>(parser, 0x3C));
    MixScriptValue(At<void*>(parser, 0x40));
}

// The variable table through its slots and addresses.
void DriveTable()
{
    void* const table = reinterpret_cast<void*(__cdecl*)()>(addr::cGZUIParserVars_New)();
    Mix(table ? 1u : 0u);
    Mix(At<uint32_t>(table, 0x4));
    Virtual<uint32_t>(table, 0x4);
    for (uint32_t iid : {1u, 0x1A2BDEBBu, 0x1A2BDEBAu, 0x5A2C1570u, 0u}) {
        void* out = nullptr;
        const uint32_t eax = Virtual<uint32_t>(table, 0x0, iid, &out);
        Mix(eax);
        Mix(out == table ? 1u : 0u);
        if (out)
            XpRelease(out);
    }
    GameText alpha("alpha"), beta("beta"), empty(""), upper("Alpha"), missing("missing");
    MixByte(Virtual<uint32_t>(table, 0xC, alpha.get(), 7u));
    MixByte(Virtual<uint32_t>(table, 0xC, alpha.get(), 8u));
    MixByte(Virtual<uint32_t>(table, 0xC, empty.get(), 0u));
    MixByte(CallThis<uint32_t>(addr::cGZUIParserVars_SetNumber, table, upper.get(), 0xFFFFFFFFu));
    void* const first = NumberValue(0x1234);
    void* const second = TextValue("second");
    MixByte(Virtual<uint32_t>(table, 0x10, beta.get(), first));
    MixScriptValue(first);
    MixByte(CallThis<uint32_t>(addr::cGZUIParserVars_Set, table, beta.get(), second));
    MixScriptValue(second);
    Virtual<void*>(second, 0x1FC, 1u);
    for (void* name : {alpha.get(), beta.get(), empty.get(), upper.get(), missing.get()}) {
        void* out = nullptr;
        MixByte(Virtual<uint32_t>(table, 0x18, name, &out));
        MixScriptValue(out);
        if (out)
            XpRelease(out);
    }
    void* out = nullptr;
    MixByte(CallThis<uint32_t>(addr::cGZUIParserVars_Get, table, alpha.get(), &out));
    if (out)
        XpRelease(out);
    MixByte(Virtual<uint32_t>(table, 0x14, beta.get()));
    MixByte(Virtual<uint32_t>(table, 0x14, beta.get()));
    MixByte(CallThis<uint32_t>(addr::cGZUIParserVars_Remove, table, missing.get()));
    MixByte(Virtual<uint32_t>(table, 0x1C));
    out = nullptr;
    MixByte(Virtual<uint32_t>(table, 0x18, alpha.get(), &out));
    MixByte(CallThis<uint32_t>(addr::cGZUIParserVars_Clear, table));
    MixByte(Virtual<uint32_t>(table, 0xC, alpha.get(), 9u));
    MixByte(Virtual<uint32_t>(table, 0x10, beta.get(), NumberValue(10)));
    Mix(XpRelease(table));

    // One on the stack, by its constructor and destructor; one deleted by
    // the deleting destructor's address.
    alignas(4) char local[0x14];
    Mix(CallThis<char*>(addr::cGZUIParserVars_Construct, local) == local ? 1u : 0u);
    MixByte(Virtual<uint32_t>(local, 0xC, alpha.get(), 1u));
    MixByte(Virtual<uint32_t>(local, 0x10, empty.get(), TextValue("x")));
    CallThis<void>(addr::cGZUIParserVars_Destruct, local);
    Mix(At<std::uintptr_t>(local, 0) == addr::cGZUIParserVars_Vtable ? 1u : 0u);
    void* const other = reinterpret_cast<void*(__cdecl*)()>(addr::cGZUIParserVars_New)();
    MixByte(Virtual<uint32_t>(other, 0xC, beta.get(), 2u));
    Mix(CallThis<void*>(addr::cGZUIParserVars_DeletingDestructor, other, 1u) == other ? 1u : 0u);
}

// The parser's stack of tables and its interfaces.
void DriveStack()
{
    char* const parser = NewParser();
    MixParserState(parser, false);
    for (uint32_t iid : {1u, 0x5A2C1570u, 0x1A2BDEBBu, 0x5A2C1571u}) {
        void* out = nullptr;
        const uint32_t eax = Virtual<uint32_t>(parser, 0x0, iid, &out);
        Mix(eax & 0xFF);
        if (!(eax & 0xFF))
            Mix(eax);
        Mix(out == parser ? 1u : 0u);
        if (out)
            XpRelease(out);
    }
    MixByte(CallThis<uint32_t>(addr::cGZUIParser_QueryInterface, parser, 7u, nullptr));
    GameText one("one"), two("two"), text("str"), digits("42"), hex("0x1F"), mixed("7abc"), word("abc"),
        empty(""), missing("zzz");
    MixByte(Virtual<uint32_t>(parser, 0x24, one.get(), 1u));
    MixByte(CallThis<uint32_t>(addr::cGZUIParser_SetNumber, parser, one.get(), 5u));
    MixByte(Virtual<uint32_t>(parser, 0x28, two.get(), NumberValue(2)));
    MixByte(CallThis<uint32_t>(addr::cGZUIParser_SetVar, parser, text.get(), TextValue("text")));
    void* const table = reinterpret_cast<void*(__cdecl*)()>(addr::cGZUIParserVars_New)();
    Virtual<uint32_t>(table, 0x4);
    MixByte(Virtual<uint32_t>(table, 0xC, one.get(), 11u));
    MixByte(Virtual<uint32_t>(parser, 0x34, table));
    MixByte(Virtual<uint32_t>(parser, 0x34, nullptr));
    MixByte(CallThis<uint32_t>(addr::cGZUIParser_PushVars, parser, nullptr));
    Mix(ParserDepth(parser));
    for (void* name : {one.get(), two.get(), text.get(), missing.get()}) {
        void* out = nullptr;
        MixByte(CallThis<uint32_t>(addr::cGZUIParser_Lookup, parser, name, &out));
        MixScriptValue(out);
        if (out)
            XpRelease(out);
    }
    for (void* name : {one.get(), two.get(), text.get(), digits.get(), hex.get(), mixed.get(), word.get(),
                       empty.get(), missing.get()}) {
        uint32_t number = 0xA5A5A5A5u;
        MixByte(CallThis<uint32_t>(addr::cGZUIParser_ResolveNumber, parser, name, &number));
        Mix(number);
    }
    MixByte(Virtual<uint32_t>(parser, 0x2C, one.get()));
    MixByte(CallThis<uint32_t>(addr::cGZUIParser_RemoveVar, parser, one.get()));
    uint32_t number = 0;
    MixByte(CallThis<uint32_t>(addr::cGZUIParser_ResolveNumber, parser, one.get(), &number));
    Mix(number);
    MixByte(Virtual<uint32_t>(parser, 0x38));
    Mix(ParserDepth(parser));
    MixByte(CallThis<uint32_t>(addr::cGZUIParser_PopVars, parser));
    Mix(ParserDepth(parser));
    MixByte(Virtual<uint32_t>(parser, 0x30));
    MixByte(CallThis<uint32_t>(addr::cGZUIParser_ClearVars, parser));
    MixByte(CallThis<uint32_t>(addr::cGZUIParser_ResolveNumber, parser, two.get(), &number));
    MixByte(Virtual<uint32_t>(parser, 0x34, table));
    XpRelease(table);
    Mix(ParserDepth(parser));

    // The context object and the listener with both interfaces, one and none.
    FakeCounted counted{kCountedVtable, 0};
    MixByte(Virtual<uint32_t>(parser, 0x10, &counted));
    MixByte(Virtual<uint32_t>(parser, 0x10, &counted));
    Mix(static_cast<uint32_t>(counted.refs));
    MixByte(CallThis<uint32_t>(addr::cGZUIParser_SetContext, parser, nullptr));
    Mix(static_cast<uint32_t>(counted.refs));
    MixByte(Virtual<uint32_t>(parser, 0x10, &counted));
    for (int i = 0; i < 4; ++i) {
        FakeListener listener;
        InitListener(listener, parser, i & 1, (i & 2) != 0);
        MixByte(Virtual<uint32_t>(parser, 0xC, &listener));
        MixRefs(listener);
        Mix(At<void*>(parser, 0xC) ? 1u : 0u);
        Mix(At<void*>(parser, 0x10) ? 1u : 0u);
        MixByte(CallThis<uint32_t>(addr::cGZUIParser_SetListener, parser, nullptr));
        MixRefs(listener);
    }
    MixByte(Virtual<uint32_t>(parser, 0xC, nullptr));
    Mix(XpRelease(parser));
    // The destructor leaves the context held.
    Mix(static_cast<uint32_t>(counted.refs));
}

// The tokenizer by its address over texts set at the cursor.
void DriveTokens()
{
    char* const parser = NewParser();
    void* token = nullptr;
    int32_t type = -1;
    MixByte(CallThis<uint32_t>(addr::cGZUIParser_NextToken, parser, &token, &type, 1u));
    Mix(token ? 1u : 0u);
    const char* const texts[] = {
        "hello <a>",       "  \t\r\n x",     "\"a b\" c",          "\"unterminated",    "\"",
        "<>=",             "abc=def",        "a\"b",                "",                  "   ",
        "w1 w2\tw3\nw4",   "<tag n=\"v\" m=x>", "\"\"",             "x<y>z",             "text only",
        " <lead",          "\x01\x7F\xFF<",  "a=b=c",               "\"q\"\"r\"",        "<<>>",
    };
    for (const char* text : texts) {
        for (uint32_t mode : {0u, 1u, 0xFF00u}) {
            At<const char*>(parser, 0x2C) = text;
            for (int n = 0; n < 16; ++n) {
                token = reinterpret_cast<void*>(1);
                type = -1;
                const uint32_t eax = CallThis<uint32_t>(addr::cGZUIParser_NextToken, parser, &token, &type, mode);
                MixByte(eax);
                Mix(static_cast<uint32_t>(At<const char*>(parser, 0x2C) - text));
                if (!(eax & 0xFF)) {
                    Mix(token ? 1u : 0u);
                    break;
                }
                Mix(static_cast<uint32_t>(type));
                if (type == 1)
                    Mix(eax);
                MixString(token);
                XpRelease(token);
            }
        }
    }
    At<const char*>(parser, 0x2C) = nullptr;
    XpRelease(parser);
}

uint32_t ParseText(char* parser, const char* text)
{
    GameText string(text);
    const uint32_t eax = Virtual<uint32_t>(parser, 0x18, string.get());
    MixByte(eax);
    MixParserState(parser);
    return eax;
}

void DefineNames(char* parser)
{
    const struct {
        const char* name;
        uint32_t number;
    } names[] = {{"tag", 1}, {"name", 2}, {"val", 6}, {"one", 0x11}, {"LEGACY", 0x20}, {"/tag", 3}};
    for (const auto& n : names) {
        GameText name(n.name);
        Virtual<uint32_t>(parser, 0x24, name.get(), n.number);
    }
    GameText text("words");
    Virtual<uint32_t>(parser, 0x28, text.get(), TextValue("a word"));
}

const char* const kDocuments[] = {
    "hello <tag name=5 val=\"x y\"> world <tag>",
    "<tag name=one val=words><tag name=1 val=2 one=3>inner</tag>\n tail",
    "<tag name='q' val=\"\" one=\"a=b <c>\">",
    "<foo a=1 b=\"2\">skipped text<tag>",
    "<tag zzz=1 name=2><tag zzz=\"q\" name=3><tag zzz><tag zzz name=4>",
    "<1 2=3 4=\"five\" 6=0x10>",
    "<tag name=>",
    "<tag = 1>",
    "<<",
    "<tag name=\"a\"=",
    ">",
    "= x",
    "<tag name=",
    "<tag name=\"abc",
    "<tag",
    "<",
    "",
    "   ",
    "just text",
    "\r\n\t<tag>\r\n\t<tag name=7>",
    "<LEGACY name=12 val=(0,0,10,10) one=\"Hello there\">\n<tag>\n</tag>",
    "<tag name=one name=2 name=\"3\">",
    "<tag zzz=\"x\" zzz= y>",
    "<tag zzz <tag>",
    "<tag name=val>",
};

// Whole documents through the slots, with every listener shape.
void DriveParse()
{
    char* const parser = NewParser();
    DefineNames(parser);
    for (int shape = 0; shape < 5; ++shape) {
        FakeListener listener;
        InitListener(listener, parser, shape != 1 && shape != 3, shape != 2 && shape != 3);
        if (shape < 4)
            Virtual<uint32_t>(parser, 0xC, &listener);
        for (const char* document : kDocuments) {
            ParseText(parser, document);
            MixRefs(listener);
        }
        Virtual<uint32_t>(parser, 0xC, nullptr);
        MixRefs(listener);
    }
    // A parse running already is refused; the steps by their addresses.
    FakeListener listener;
    InitListener(listener, parser, true, true);
    Virtual<uint32_t>(parser, 0xC, &listener);
    At<uint32_t>(parser, 0x24) = 3;
    GameText busy("<tag>");
    At<const char*>(parser, 0x28) = "<tag>";
    MixByte(CallThis<uint32_t>(addr::cGZUIParser_Parse, parser));
    At<uint32_t>(parser, 0x24) = 0;
    MixByte(CallThis<uint32_t>(addr::cGZUIParser_ParseString, parser, busy.get()));
    MixByte(CallThis<uint32_t>(addr::cGZUIParser_StartDocument, parser));
    MixByte(CallThis<uint32_t>(addr::cGZUIParser_FinishTag, parser));
    MixByte(CallThis<uint32_t>(addr::cGZUIParser_CommitAttribute, parser));
    GameText tag("tag"), name("name"), bad("nope"), quoted("quoted text"), number("12"), words("words");
    MixByte(CallThis<uint32_t>(addr::cGZUIParser_BeginTag, parser, bad.get()));
    MixByte(CallThis<uint32_t>(addr::cGZUIParser_BeginTag, parser, tag.get()));
    MixByte(CallThis<uint32_t>(addr::cGZUIParser_SetAttributeName, parser, bad.get()));
    MixByte(CallThis<uint32_t>(addr::cGZUIParser_SetAttributeName, parser, name.get()));
    MixByte(CallThis<uint32_t>(addr::cGZUIParser_MakeStringValue, parser, quoted.get()));
    MixParserState(parser);
    MixByte(CallThis<uint32_t>(addr::cGZUIParser_CommitAttribute, parser));
    MixByte(CallThis<uint32_t>(addr::cGZUIParser_SetAttributeName, parser, name.get()));
    MixByte(CallThis<uint32_t>(addr::cGZUIParser_MakeValue, parser, number.get()));
    MixByte(CallThis<uint32_t>(addr::cGZUIParser_CommitAttribute, parser));
    MixByte(CallThis<uint32_t>(addr::cGZUIParser_SetAttributeName, parser, name.get()));
    MixByte(CallThis<uint32_t>(addr::cGZUIParser_MakeValue, parser, words.get()));
    MixByte(CallThis<uint32_t>(addr::cGZUIParser_CommitAttribute, parser));
    MixByte(CallThis<uint32_t>(addr::cGZUIParser_SetAttributeName, parser, name.get()));
    MixByte(CallThis<uint32_t>(addr::cGZUIParser_MakeValue, parser, bad.get()));
    MixParserState(parser);
    MixByte(CallThis<uint32_t>(addr::cGZUIParser_FinishTag, parser));
    MixByte(CallThis<uint32_t>(addr::cGZUIParser_ClearAttributes, parser));
    MixParserState(parser);
    MixByte(CallThis<uint32_t>(addr::cGZUIParser_Text, parser, quoted.get()));
    MixByte(CallThis<uint32_t>(addr::cGZUIParser_EndFailed, parser));
    MixByte(CallThis<uint32_t>(addr::cGZUIParser_EndDone, parser));
    MixRefs(listener);
    Virtual<uint32_t>(parser, 0xC, nullptr);
    MixByte(CallThis<uint32_t>(addr::cGZUIParser_Text, parser, quoted.get()));
    MixByte(CallThis<uint32_t>(addr::cGZUIParser_StartDocument, parser));
    XpRelease(parser);
    MixRefs(listener);
}

// A file (written to the temp folder, empty, missing) and resources (a UI
// file the game reads, a key that is not there).
void DriveSources()
{
    char* const parser = NewParser();
    DefineNames(parser);
    FakeListener listener;
    InitListener(listener, parser, true, true);
    Virtual<uint32_t>(parser, 0xC, &listener);
    const char* const folder = std::getenv("TEMP");
    const std::string base =
        std::string(folder ? folder : ".") + "\\opensc4-xmlparse-" + std::to_string(_getpid()) + "-";
    const std::string full = base + "full.txt";
    const std::string none = base + "empty.txt";
    if (std::FILE* f = std::fopen(full.c_str(), "wb")) {
        const char* const text = "<tag name=1 val=\"from a file\">\r\nline <tag name=2>";
        std::fwrite(text, 1, std::strlen(text), f);
        std::fclose(f);
    }
    if (std::FILE* f = std::fopen(none.c_str(), "wb"))
        std::fclose(f);
    for (const std::string& path : {full, none, base + "missing.txt"}) {
        GameText name(path.c_str());
        MixByte(Virtual<uint32_t>(parser, 0x1C, name.get()));
        MixRefs(listener);
    }
    GameText name(full.c_str());
    MixByte(CallThis<uint32_t>(addr::cGZUIParser_ParseFile, parser, name.get()));
    std::remove(full.c_str());
    std::remove(none.c_str());
    const uint32_t keys[][3] = {{0, 0x96A006B0, 0xCBC905CD}, {0, 0x96A006B0, 0x0BADF00D}};
    for (const auto& key : keys) {
        MixByte(Virtual<uint32_t>(parser, 0x20, key));
        MixRefs(listener);
    }
    MixByte(CallThis<uint32_t>(addr::cGZUIParser_ParseResource, parser, keys[1]));
    Virtual<uint32_t>(parser, 0xC, nullptr);
    XpRelease(parser);
}

// The objects' constructors and destructors by address.
void DriveObjects()
{
    alignas(4) char local[0x5C];
    Mix(CallThis<char*>(addr::cGZUIParser_Construct, local) == local ? 1u : 0u);
    MixParserState(local, false);
    GameText name("name");
    Virtual<uint32_t>(local, 0x24, name.get(), 3u);
    FakeListener listener;
    InitListener(listener, local, true, true);
    Virtual<uint32_t>(local, 0xC, &listener);
    ParseText(local, "<1 name=4>");
    CallThis<void>(addr::cGZUIParser_Destruct, local);
    MixRefs(listener);
    char* const heap = NewParser();
    Mix(CallThis<char*>(addr::cGZUIParser_DeletingDestructor, heap, 1u) == heap ? 1u : 0u);
    char* const kept = static_cast<char*>(reinterpret_cast<void*(__cdecl*)()>(addr::cGZUIParser_New)());
    Mix(CallThis<char*>(addr::cGZUIParser_DeletingDestructor, kept, 0u) == kept ? 1u : 0u);
    reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_operator_delete)(kept);
}

uint32_t g_xpClasses = 0;
void __cdecl CountClass(uint32_t clsid, uint32_t version, void*)
{
    ++g_xpClasses;
    Mix(clsid);
    Mix(version);
}

// The directors: one made on the stack (its registrations through the COM
// lookup), the static one, the next director and its factories.
void DriveDirectors()
{
    alignas(4) char director[0x4C];
    Mix(CallThis<char*>(addr::cGZUIParserDirector_Construct, director) == director ? 1u : 0u);
    Mix(At<std::uintptr_t>(director, 0) == addr::cGZUIParserDirector_Vtable ? 1u : 0u);
    Mix(At<std::uintptr_t>(director, 4) == addr::cGZUIParserDirector_Vtable2 ? 1u : 0u);
    Mix(Virtual<uint32_t>(director, 0x34));
    g_xpClasses = 0;
    Virtual<uint32_t>(director, 0x14, &CountClass, nullptr);
    Mix(g_xpClasses);
    const uint32_t asks[][2] = {
        {0xBA2E7953, 0x5A2C1570}, {0xBA2E7953, 1}, {0xBA2E7954, 0x1A2BDEBB}, {0xBA2E7954, 1}, {0x12345678, 1}};
    for (const auto& ask : asks) {
        void* out = nullptr;
        MixByte(Virtual<uint32_t>(director, 0x18, ask[0], ask[1], &out));
        Mix(out ? 1u : 0u);
        if (out)
            Mix(XpRelease(out));
    }
    CallThis<void>(addr::cRZCOMDllDirector_Destruct, director);
    void* const instance = reinterpret_cast<void*(__cdecl*)()>(addr::cGZUIParserDirector_Get)();
    Mix(instance == reinterpret_cast<void*>(addr::cGZUIParserDirector_Instance) ? 1u : 0u);
    Mix(*reinterpret_cast<const uint32_t*>(addr::cGZUIParserDirector_Guard) & 1);

    alignas(4) char next[0x4C];
    CallThis<char*>(addr::cGZSoundDirector_Construct, next);
    Mix(Virtual<uint32_t>(next, 0x34));
    g_xpClasses = 0;
    Virtual<uint32_t>(next, 0x14, &CountClass, nullptr);
    Mix(g_xpClasses);
    Mix(CallThis<char*>(addr::cGZSoundDirector_DeletingDestructor, next, 0u) == next ? 1u : 0u);
    for (std::uintptr_t factory : {addr::cGZSoundDirector_New68, addr::cGZSoundDirector_New4048, addr::cGZSoundDirector_New5C,
                                   addr::cGZSoundDirector_New4144, addr::cGZSoundDirector_New190}) {
        void* const object = reinterpret_cast<void*(__cdecl*)()>(factory)();
        Mix(object ? 1u : 0u);
        if (object) {
            Virtual<uint32_t>(object, 0x4);
            XpRelease(object);
        }
    }
}

// OPENSC4_TEST_XMLPARSE=<day>: part u4's functions called through their
// slots and addresses with a listener, values and texts of our own: the
// variable table, the parser's stack of tables, the tokenizer over texts
// (quotes, truncations, delimiters, bytes above 0x7F), whole documents
// (well formed, nested, unknown tags and attributes, malformed, truncated,
// empty) with every listener shape, a file and resources, the objects'
// constructors and destructors, the directors. Every result goes into the
// fingerprint's `xp` part; addresses never do.
void DriveXmlParse(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_XMLPARSE") || !game::IsBuild641())
        return;
    g_xpOn = true;
    uint32_t* const saved = g_into;
    g_into = &g_xpHash;
    DriveTable();
    XpStage("table");
    DriveStack();
    XpStage("stack");
    DriveTokens();
    XpStage("tokens");
    DriveParse();
    XpStage("parse");
    DriveSources();
    XpStage("sources");
    DriveObjects();
    XpStage("objects");
    DriveDirectors();
    XpStage("directors");
    g_into = saved;
    log::Write("autotest: xmlparse driven (hash %08X)", g_xpHash);
}

void XpFingerprint(cISC4City*, std::string& line)
{
    if (!g_xpOn)
        return;
    char text[32];
    std::snprintf(text, sizeof text, " | xp %08X", g_xpHash);
    line += text;
}

const FingerprintPart kXpFingerprint{"xmlparse", 4, &XpFingerprint};
const DayHook kXpDrive{"xmlparse", 4, &DriveXmlParse};

// ====================================================================================
// Part u5: the framework's sound director, its table, the sound instance
// 0xBDD9D7A5 and the sound description 0x7DD9C9F2 (OPENSC4_TEST_GZSOUND)

bool g_gsOn = false;
uint32_t g_gsHash = 2166136261u;

void GsStage(const char* name) { log::Write("autotest: gzsound: %s %08X", name, g_gsHash); }

// A pointer against the one it should be (addresses never go into the hash).
void MixIs(const void* pointer, const void* expected) { Mix(pointer == expected ? 1u : pointer ? 2u : 0u); }

// A method's EAX: the description's address as a marker, other addresses
// (a reference holder's, above 0x10000) by their low byte only.
void MixEax(uint32_t eax, const void* desc)
{
    if (eax == static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(desc)))
        Mix(0xD0D0D0D0u);
    else if (eax > 0x10000 && eax < 0xFFFF0000u)
        Mix(0xA0000000u | (eax & 0xFF));
    else
        Mix(eax);
}

uint32_t Bits32(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    return bits;
}

uint32_t __fastcall GsTrap(void*, void*)
{
    Mix(0xDEAD);
    return 0;
}

// The buffer an instance holds at +0x1C and its interface 0x306077D2 (slot
// 5 a place). Slot 7 whether it still plays, 20 whether it is done, 21 a
// level, 22 play (four arguments); every call goes into the hash.
struct GsBuffer;
struct GsPositioner {
    void* const* vtable;
    GsBuffer* owner;
    int32_t refs;
};
struct GsBuffer {
    void* const* vtable;
    int32_t refs;
    uint32_t playing;
    uint32_t status;
    bool offerPositioner;
    GsPositioner positioner;
};

uint32_t __fastcall GsBufferQueryInterface(GsBuffer* self, void*, uint32_t iid, void** out)
{
    Mix(0x6100);
    Mix(iid);
    if (iid == 0x306077D2 && self->offerPositioner) {
        ++self->positioner.refs;
        *out = &self->positioner;
        return 1;
    }
    return 0;
}
uint32_t __fastcall GsBufferAddRef(GsBuffer* self, void*)
{
    Mix(0x6104);
    return static_cast<uint32_t>(++self->refs);
}
uint32_t __fastcall GsBufferRelease(GsBuffer* self, void*)
{
    Mix(0x6108);
    return static_cast<uint32_t>(--self->refs);
}
uint32_t __fastcall GsBufferPlaying(GsBuffer* self, void*)
{
    Mix(0x611C);
    return self->playing;
}
uint32_t __fastcall GsBufferStatus(GsBuffer* self, void*)
{
    Mix(0x6150);
    return self->status;
}
uint32_t __fastcall GsBufferLevel(GsBuffer*, void*, int32_t level)
{
    Mix(0x6154);
    Mix(static_cast<uint32_t>(level));
    return 0x5400u + (static_cast<uint32_t>(level) & 0xFF);
}
uint32_t __fastcall GsBufferPlay(GsBuffer*, void*, uint32_t status, uint32_t a, uint32_t count, uint32_t b)
{
    Mix(0x6158);
    Mix(status);
    Mix(a);
    Mix(count);
    Mix(b);
    return 0x5801;
}

uint32_t __fastcall GsPositionerAddRef(GsPositioner* self, void*)
{
    Mix(0x6204);
    return static_cast<uint32_t>(++self->refs);
}
uint32_t __fastcall GsPositionerRelease(GsPositioner* self, void*)
{
    Mix(0x6208);
    return static_cast<uint32_t>(--self->refs);
}
uint32_t __fastcall GsPositionerPlace(GsPositioner*, void*, float x, float y, float z)
{
    Mix(0x6214);
    Mix(Bits32(x));
    Mix(Bits32(y));
    Mix(Bits32(z));
    return 0x1400;
}

void* const* GsBufferVtable()
{
    static void* table[23];
    if (!table[0]) {
        for (void*& slot : table)
            slot = reinterpret_cast<void*>(&GsTrap);
        table[0] = reinterpret_cast<void*>(&GsBufferQueryInterface);
        table[1] = reinterpret_cast<void*>(&GsBufferAddRef);
        table[2] = reinterpret_cast<void*>(&GsBufferRelease);
        table[7] = reinterpret_cast<void*>(&GsBufferPlaying);
        table[20] = reinterpret_cast<void*>(&GsBufferStatus);
        table[21] = reinterpret_cast<void*>(&GsBufferLevel);
        table[22] = reinterpret_cast<void*>(&GsBufferPlay);
    }
    return table;
}

void* const* GsPositionerVtable()
{
    static void* table[6];
    if (!table[0]) {
        for (void*& slot : table)
            slot = reinterpret_cast<void*>(&GsTrap);
        table[1] = reinterpret_cast<void*>(&GsPositionerAddRef);
        table[2] = reinterpret_cast<void*>(&GsPositionerRelease);
        table[5] = reinterpret_cast<void*>(&GsPositionerPlace);
    }
    return table;
}

void InitBuffer(GsBuffer& buffer)
{
    buffer.vtable = GsBufferVtable();
    buffer.refs = 1;
    buffer.playing = 0;
    buffer.status = 0;
    buffer.offerPositioner = true;
    buffer.positioner = {GsPositionerVtable(), &buffer, 0};
}

// The listener at +0x54: slot 9 whether to go on, slot 4 the sound finished.
struct GsListener {
    void* const* vtable;
    int32_t refs;
    uint32_t goOn;
    const void* desc;
};
uint32_t __fastcall GsListenerRelease(GsListener* self, void*)
{
    Mix(0x6308);
    return static_cast<uint32_t>(--self->refs);
}
uint32_t __fastcall GsListenerFinished(GsListener* self, void*, void* desc, float time)
{
    Mix(0x6310);
    MixIs(desc, self->desc);
    Mix(Bits32(time));
    return 0x1000;
}
uint32_t __fastcall GsListenerGoOn(GsListener* self, void*, void* desc)
{
    Mix(0x6324);
    MixIs(desc, self->desc);
    return 0x2400 | self->goOn;
}

void* const* GsListenerVtable()
{
    static void* table[10];
    if (!table[0]) {
        for (void*& slot : table)
            slot = reinterpret_cast<void*>(&GsTrap);
        table[2] = reinterpret_cast<void*>(&GsListenerRelease);
        table[4] = reinterpret_cast<void*>(&GsListenerFinished);
        table[9] = reinterpret_cast<void*>(&GsListenerGoOn);
    }
    return table;
}

void InitListener(GsListener& listener, const void* desc)
{
    listener = {GsListenerVtable(), 1, 1, desc};
}

// The sound service as an instance reads it: +0x10 a generator (the game's
// LCG on its first word), +0x14 a level, +0x1C an object whose +8 is a level.
struct GsService {
    alignas(4) char bytes[0x20];
    uint32_t random[4];
    alignas(4) char master[0xC];
};

void InitService(GsService& service, uint32_t seed, uint32_t level, uint32_t master)
{
    std::memset(&service, 0, sizeof service);
    At<void*>(service.bytes, 0x10) = service.random;
    service.random[0] = seed;
    At<uint32_t>(service.bytes, 0x14) = level;
    At<void*>(service.bytes, 0x1C) = service.master;
    At<uint32_t>(service.master, 0x8) = master;
}

void MixDesc(char* desc, bool all)
{
    Mix(At<std::uintptr_t>(desc, 0) == addr::cGZSoundDesc_Vtable ? 1u : 0u);
    Mix(At<uint32_t>(desc, 0x4));
    Mix(At<uint32_t>(desc, 0x8));
    MixString(desc + 0xC);
    for (std::size_t offset : {0x20, 0x30, 0x31, 0x44, 0x50, 0x60})
        Mix(At<uint8_t>(desc, offset));
    for (std::size_t offset : {0x24, 0x28, 0x2C, 0x34, 0x38, 0x3C, 0x48, 0x4C, 0x54, 0x58, 0x5C})
        Mix(At<uint32_t>(desc, offset));
    if (all)
        Mix(At<uint32_t>(desc, 0x40));
    char* const head = At<char*>(desc, 0x64);
    Mix(head ? 1u : 0u);
    if (head)
        Mix(At<char*>(head, 0) == head && At<char*>(head, 4) == head ? 1u : 0u);
}

void MixInstance(char* instance, bool all)
{
    Mix(At<std::uintptr_t>(instance, 0) == addr::cGZSoundInstance_Vtable ? 1u : 0u);
    Mix(At<std::uintptr_t>(instance, 4) == addr::cGZSoundInstance_Vtable2 ? 1u : 0u);
    Mix(At<uint32_t>(instance, 0x8));
    Mix(At<uint8_t>(instance, 0xC));
    Mix(At<uint8_t>(instance, 0xD));
    Mix(At<uint32_t>(instance, 0x10));
    Mix(At<uint32_t>(instance, 0x14));
    Mix(At<void*>(instance, 0x1C) ? 1u : 0u);
    Mix(At<uint32_t>(instance, 0x20));
    for (std::size_t offset : {0x2C, 0x2D, 0x3C, 0x3D, 0x58})
        Mix(At<uint8_t>(instance, offset));
    for (std::size_t offset : {0x34, 0x40, 0x44, 0x48, 0x4C, 0x50})
        Mix(At<uint32_t>(instance, offset));
    Mix(At<void*>(instance, 0x54) ? 1u : 0u);
    if (all) {
        Mix(At<uint32_t>(instance, 0x28));
        Mix(At<uint32_t>(instance, 0x30));
        Mix(At<uint32_t>(instance, 0x38));
    }
}

char* NewDesc() { return static_cast<char*>(reinterpret_cast<void*(__cdecl*)()>(addr::cGZSoundDirector_New68)()); }

// A description on the stack, made by the constructor's address.
struct StackDesc {
    alignas(4) char bytes[0x68];
    StackDesc()
    {
        std::memset(bytes, 0xCD, sizeof bytes);
        CallThis<char*>(addr::cGZSoundDesc_Construct, bytes);
    }
    ~StackDesc() { CallThis<uint32_t>(addr::cGZSoundDesc_Destruct, bytes); }
    StackDesc(const StackDesc&) = delete;
    StackDesc& operator=(const StackDesc&) = delete;
};

// The description: its constructor, interfaces, references, every setter
// by its slot (its own and the shared ones), the getters, the destructor.
void DriveDesc()
{
    alignas(4) char desc[0x68];
    std::memset(desc, 0xCD, sizeof desc);
    MixIs(CallThis<char*>(addr::cGZSoundDesc_Construct, desc), desc);
    MixDesc(desc, true);
    for (uint32_t iid : {1u, 0x1DD9C9DEu, 0x5DD9D787u, 0u}) {
        void* out = nullptr;
        Mix(Virtual<uint32_t>(desc, 0x0, iid, &out) & 0xFF);
        MixIs(out, desc);
        Mix(At<uint32_t>(desc, 0x4));
    }
    Mix(Virtual<uint32_t>(desc, 0x4));
    Mix(Virtual<uint32_t>(desc, 0x8));
    Mix(At<uint32_t>(desc, 0x4));
    GameText name("sound name");
    Mix(Virtual<uint32_t>(desc, 0x60, name.get()) & 0xFF);
    MixString(desc + 0xC);
    struct Set {
        std::size_t slot;
        uint32_t value;
        bool byte;
    };
    const Set sets[] = {{0x5C, 0x12345678, false}, {0x64, 0x1FF, true},     {0x68, 0x300, false},
                        {0x6C, 0x1C0, false},      {0x70, 0x240, false},    {0x74, 0x101, true},
                        {0x78, 0x100, true},       {0x7C, 0xFFFFFFFF, false}, {0x80, 0x55, false},
                        {0x84, 0x66, false},       {0x88, 0x77, false},     {0x8C, 0x2FE, true},
                        {0x90, 3, false},          {0x94, 0x7788, false},   {0x98, 0x100, true},
                        {0x9C, 0x88, false},       {0xA0, 0x99, false},     {0xA4, 0xAA, false},
                        {0xA8, 0x1BB, true}};
    for (const Set& set : sets) {
        const uint32_t eax = Virtual<uint32_t>(desc, set.slot, set.value);
        Mix(set.byte ? eax & 0xFF : eax);
        MixDesc(desc, true);
    }
    MixIs(Virtual<void*>(desc, 0x10), desc + 0xC);
    Mix(Virtual<uint32_t>(desc, 0x24) & 0xFF);
    Mix(Virtual<uint32_t>(desc, 0x28) & 0xFF);
    CallThis<uint32_t>(addr::cGZSoundDesc_Destruct, desc);
    Mix(At<std::uintptr_t>(desc, 0) == addr::cGZSoundDesc_Vtable ? 1u : 0u);

    // On the heap: counted down to its deletion.
    char* const heap = NewDesc();
    MixDesc(heap, false);
    Mix(Virtual<uint32_t>(heap, 0x4));
    Mix(Virtual<uint32_t>(heap, 0x4));
    Mix(Virtual<uint32_t>(heap, 0x8));
    Mix(Virtual<uint32_t>(heap, 0x8));
    char* const other = NewDesc();
    Mix(Virtual<uint32_t>(other, 0x4));
    Mix(CallThis<uint32_t>(addr::cGZSoundDesc_Release, other));
}

void MixTable(char* table)
{
    Mix(At<std::uintptr_t>(table, 0) == addr::cGZSoundTable_Vtable ? 1u : 0u);
    for (std::size_t offset = 0x4; offset < 0x48; offset += 4)
        Mix(At<uint32_t>(table, offset));
    uint32_t used = 0;
    for (uint32_t i = 0; i < 64 * 64; ++i)
        used += At<void*>(table, 0x48 + i * 4) ? 1 : 0;
    Mix(used);
}

// The table: made by its factory, cells filled with descriptions, Get by
// its slot, counted down to the game's deleting destructor (which calls
// ours); one on the stack by the constructor's and destructor's addresses.
void DriveTable5()
{
    char* const table = static_cast<char*>(reinterpret_cast<void*(__cdecl*)()>(addr::cGZSoundDirector_New4048)());
    MixTable(table);
    const uint32_t cells[][2] = {{0, 0}, {0, 63}, {63, 0}, {63, 63}, {10, 20}, {1, 2}};
    for (const auto& cell : cells) {
        char* const desc = NewDesc();
        At<uint32_t>(desc, 0x24) = cell[0] * 100 + cell[1];
        At<char*>(table, 0x48 + (cell[0] * 64 + cell[1]) * 4) = desc;
    }
    MixTable(table);
    const uint32_t asks[][2] = {{0, 0}, {0, 63}, {63, 0}, {63, 63}, {10, 20}, {1, 2}, {5, 5}, {0, 64}, {0, 66}};
    for (const auto& ask : asks) {
        void* const got = Virtual<void*>(table, 0xC, ask[0], ask[1], 0xDEADu);
        Mix(got ? At<uint32_t>(got, 0x24) : 0xFFFFFFFFu);
    }
    Mix(Virtual<uint32_t>(table, 0x4));
    Mix(Virtual<uint32_t>(table, 0x4));
    Mix(Virtual<uint32_t>(table, 0x8));
    Mix(CallThis<uint32_t>(addr::cGZSoundTable_Release, table));

    std::vector<char> memory(0x4048 + 4);
    char* const local = memory.data();
    std::memset(local, 0xCD, 0x4048);
    MixIs(CallThis<char*>(addr::cGZSoundTable_Construct, local), local);
    MixTable(local);
    At<char*>(local, 0x48) = NewDesc();
    At<char*>(local, 0x48 + 4095 * 4) = NewDesc();
    MixTable(local);
    At<std::uintptr_t>(local, 0) = 0;
    CallThis<uint32_t>(addr::cGZSoundTable_Destruct, local);
    MixTable(local);
}

// The instance's own slots: interfaces and references (also through the
// part at +4), the description, the states, the setters, the level with and
// without a buffer, worked out over edge values, Stop, the hex digit test,
// the destructor.
void DriveInstance()
{
    GsService service;
    InitService(service, 1234, 0x300, 0x2A0);
    StackDesc desc;
    alignas(4) char instance[0x5C];
    std::memset(instance, 0xCD, sizeof instance);
    MixIs(CallThis<char*>(addr::cGZSoundInstance_ConstructOwned, instance, static_cast<void*>(service.bytes)), instance);
    MixInstance(instance, true);
    MixIs(At<void*>(instance, 0x18), service.bytes);
    for (uint32_t iid : {1u, 0x5DD9D787u, 0x1DD9C9DEu}) {
        void* out = nullptr;
        Mix(Virtual<uint32_t>(instance, 0x0, iid, &out) & 0xFF);
        MixIs(out, instance);
        out = nullptr;
        Mix(Virtual<uint32_t>(instance + 4, 0x0, iid, &out) & 0xFF);
        MixIs(out, instance);
        Mix(At<uint32_t>(instance, 0x14));
    }
    Mix(Virtual<uint32_t>(instance, 0x4));
    Mix(Virtual<uint32_t>(instance + 4, 0x8));
    Mix(CallThis<uint32_t>(addr::cGZSoundInstance_Release, instance));
    Mix(CallThis<uint32_t>(addr::cGZSoundInstance_AddRef, instance));

    MixEax(Virtual<uint32_t>(instance, 0x10, static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(desc.bytes))),
           desc.bytes);
    MixIs(At<void*>(instance, 0x24), desc.bytes);
    for (uint32_t state : {0u, 1u, 2u, 3u, 7u}) {
        At<uint32_t>(instance, 0x20) = state;
        Mix(Virtual<uint32_t>(instance, 0x28));
        Mix(Virtual<uint32_t>(instance, 0x2C));
    }
    At<uint32_t>(instance, 0x20) = 0;
    Mix(Virtual<uint32_t>(instance, 0x34, 0x1A5u) & 0xFF);
    Mix(Virtual<uint32_t>(instance, 0x30) & 0xFF);
    Mix(Virtual<uint32_t>(instance, 0x3C, 0x100u) & 0xFF);
    Mix(Virtual<uint32_t>(instance, 0x40, 11u, 0xFFFFFF22u, 0x12345633u));
    Mix(Virtual<uint32_t>(instance, 0x44, 500u, 0x1FFu));
    MixInstance(instance, true);

    Mix(Virtual<uint32_t>(instance, 0x24, 0x2FFu));
    MixInstance(instance, true);
    struct Level {
        uint32_t desc, volume, service, master, level;
    };
    const Level levels[] = {{0x400, 0x400, 0x400, 0x400, 0x400},
                            {0x400, 0x300, 0x2A0, 0x380, 0x123},
                            {0x7FFFFFFF, 3, 0x10000, 7, 9},
                            {0xFFFFFF00, 0x400, 0x400, 0x400, 0x400},
                            {0, 1, 2, 3, 4},
                            {0x1000, 0x1000, 0x1000, 0x1000, 0x1000},
                            {0x400, 0xFFFFFFFF, 0x400, 0x400, 0x400},
                            {0x12345, 0x6789, 0xFFFFF, 0x3, 0x80000000}};
    for (const Level& level : levels) {
        At<uint32_t>(desc.bytes, 0x24) = level.desc;
        At<uint32_t>(instance, 0x30) = level.volume;
        At<uint32_t>(service.bytes, 0x14) = level.service;
        At<uint32_t>(service.master, 0x8) = level.master;
        At<uint32_t>(instance, 0x34) = level.level;
        Mix(CallThis<uint32_t>(addr::cGZSoundInstance_ComputeLevel, instance));
        Mix(At<uint32_t>(instance, 0x38));
    }
    At<uint32_t>(desc.bytes, 0x24) = 0x400;
    At<uint32_t>(instance, 0x30) = 0x380;
    At<uint32_t>(service.bytes, 0x14) = 0x300;
    At<uint32_t>(service.master, 0x8) = 0x2A0;

    GsBuffer buffer;
    InitBuffer(buffer);
    At<void*>(instance, 0x1C) = &buffer;
    Mix(Virtual<uint32_t>(instance, 0x24, 0x3FFu));
    Mix(At<uint32_t>(instance, 0x38));
    At<uint32_t>(instance, 0x20) = 2;
    At<uint8_t>(instance, 0x2D) = 1;
    Mix(Virtual<uint32_t>(instance, 0x20) & 0xFF);
    Mix(static_cast<uint32_t>(buffer.refs));
    MixInstance(instance, true);
    Mix(Virtual<uint32_t>(instance, 0x20) & 0xFF);

    const char* const texts[] = {"",
                                 "1234567",
                                 "12345678",
                                 "abcdefAB",
                                 "x0123456789",
                                 "0123456g89abcdef",
                                 "ABCDEFG12345678",
                                 "deadbeeX",
                                 "{12345678-aaaa}",
                                 "1234 5678",
                                 "fFfFfFfF",
                                 "abcdefgh",
                                 "0x1234567",
                                 ("\xC0\xFF"
                                  "12345678")};
    for (const char* text : texts) {
        GameText string(text);
        Mix(CallThis<uint32_t>(addr::cGZSoundInstance_HasHexRun, instance, string.get()) & 0xFF);
    }

    GsListener listener;
    InitListener(listener, desc.bytes);
    GsBuffer held;
    InitBuffer(held);
    At<void*>(instance, 0x54) = &listener;
    At<void*>(instance, 0x1C) = &held;
    CallThis<uint32_t>(addr::cGZSoundInstance_Destruct, instance);
    Mix(static_cast<uint32_t>(listener.refs));
    Mix(static_cast<uint32_t>(held.refs));
    Mix(At<uint32_t>(instance, 0x4) == 0x00AD8FA4u ? 1u : 0u);
    Mix(At<std::uintptr_t>(instance, 0) == addr::cGZSoundInstance_Vtable ? 1u : 0u);
}

// An instance on the heap, kept alive by a reference, with a fake service.
char* NewInstance(GsService& service)
{
    char* const instance = static_cast<char*>(New(0x5C));
    CallThis<char*>(addr::cGZSoundInstance_ConstructOwned, instance, static_cast<void*>(service.bytes));
    Virtual<uint32_t>(instance, 0x4);
    At<uint32_t>(instance, 0x28) = 0;
    At<uint32_t>(instance, 0x30) = 0x400;
    At<uint32_t>(instance, 0x38) = 0;
    return instance;
}

struct GsCase {
    uint32_t state;
    bool listener;
    uint32_t goOn;
    bool buffer;
    uint32_t playing;
    uint32_t status;
};

// One call of the slot at `slot` (6 Update, 7 Start) in a set-up state.
void RunCase(char* instance, char* desc, GsListener& listener, GsBuffer& buffer, std::size_t slot, const GsCase& c)
{
    At<uint32_t>(instance, 0x20) = c.state;
    listener.goOn = c.goOn;
    listener.refs = 1;
    At<void*>(instance, 0x54) = c.listener ? &listener : nullptr;
    InitBuffer(buffer);
    buffer.playing = c.playing;
    buffer.status = c.status;
    At<void*>(instance, 0x1C) = c.buffer ? &buffer : nullptr;
    MixEax(Virtual<uint32_t>(instance, slot), desc);
    Mix(At<uint32_t>(instance, 0x20));
    Mix(static_cast<uint32_t>(buffer.refs));
    MixIs(At<void*>(instance, 0x1C), c.buffer ? &buffer : nullptr);
    Mix(At<uint8_t>(instance, 0x2D));
}

// Update (slot 6) in every state: idle and unknown states, waiting with
// and without a listener (going on or not) and a buffer (done or not),
// playing with a looping description (counting down, stopping at the end
// or starting over) and without (the listener, the buffer still playing or
// not, which starts it again). Restart by its address.
void DriveUpdate()
{
    GsService service;
    InitService(service, 777, 0x400, 0x400);
    StackDesc desc;
    At<uint32_t>(desc.bytes, 0x38) = 250;
    At<uint32_t>(desc.bytes, 0x48) = 3;
    At<uint8_t>(desc.bytes, 0x50) = 0;
    char* const instance = NewInstance(service);
    At<char*>(instance, 0x24) = desc.bytes;
    GsListener listener;
    InitListener(listener, desc.bytes);
    GsBuffer buffer;
    const GsCase cases[] = {
        {0, false, 0, false, 0, 0}, {1, true, 1, true, 0, 0},   {4, false, 0, true, 0, 0},
        {0xFFFFFFFF, false, 0, false, 0, 0},
        {3, false, 0, false, 0, 0}, {3, true, 1, false, 0, 0},  {3, true, 0, true, 0, 0},
        {3, false, 0, true, 0, 7},  {3, false, 0, true, 0, 0},  {3, true, 1, true, 0, 0},
        {3, true, 1, true, 0, 0x100},
        {2, false, 0, false, 0, 0}, {2, true, 0, false, 0, 0},  {2, true, 0, true, 1, 0},
        {2, true, 1, true, 1, 0},   {2, true, 1, true, 0x200, 0}, {2, false, 0, true, 0, 0},
        {2, true, 1, true, 0, 0},   {2, true, 1, false, 0, 0},
    };
    for (const GsCase& c : cases)
        RunCase(instance, desc.bytes, listener, buffer, 0x18, c);
    At<uint32_t>(desc.bytes, 0x38) = 0;
    RunCase(instance, desc.bytes, listener, buffer, 0x18, {2, true, 1, true, 0, 0});
    RunCase(instance, desc.bytes, listener, buffer, 0x18, {2, false, 0, true, 0, 0});
    At<uint32_t>(desc.bytes, 0x38) = 250;

    At<uint8_t>(desc.bytes, 0x50) = 1;
    const uint32_t loops[][2] = {{3, 0}, {1, 0}, {2, 1}, {1, 1}, {0, 0}, {0, 1}};
    for (const auto& loop : loops) {
        At<uint32_t>(instance, 0x28) = loop[0];
        At<uint8_t>(instance, 0x2D) = static_cast<uint8_t>(loop[1]);
        RunCase(instance, desc.bytes, listener, buffer, 0x18, {2, true, 1, true, 1, 0});
        Mix(At<uint32_t>(instance, 0x28));
    }
    At<uint8_t>(desc.bytes, 0x50) = 0;

    for (uint32_t time : {0u, 250u}) {
        At<uint32_t>(desc.bytes, 0x38) = time;
        for (uint32_t state : {2u, 3u, 0u, 5u}) {
            At<uint32_t>(instance, 0x20) = state;
            listener.refs = 1;
            At<void*>(instance, 0x54) = &listener;
            Mix(CallThis<uint32_t>(addr::cGZSoundInstance_Restart, instance, desc.bytes));
            Mix(At<uint32_t>(instance, 0x20));
        }
    }
    At<void*>(instance, 0x54) = nullptr;
    At<void*>(instance, 0x1C) = nullptr;
    Mix(Virtual<uint32_t>(instance + 4, 0x8));
}

// Start (slot 7): with a listener (Restart), with a buffer only (the sound
// service asked, the buffer played or stopped), with neither; in each
// state, with the description's time 0 and not.
void DriveStart()
{
    GsService service;
    InitService(service, 4242, 0x400, 0x400);
    StackDesc desc;
    char* const instance = NewInstance(service);
    At<char*>(instance, 0x24) = desc.bytes;
    GsListener listener;
    InitListener(listener, desc.bytes);
    GsBuffer buffer;
    for (uint32_t time : {0u, 250u, 0x80000001u}) {
        At<uint32_t>(desc.bytes, 0x38) = time;
        for (uint32_t state : {2u, 3u, 0u, 6u}) {
            RunCase(instance, desc.bytes, listener, buffer, 0x1C, {state, true, 1, true, 0, 0x33});
            RunCase(instance, desc.bytes, listener, buffer, 0x1C, {state, false, 0, true, 0, 0x33});
            RunCase(instance, desc.bytes, listener, buffer, 0x1C, {state, false, 0, false, 0, 0});
        }
    }
    At<void*>(instance, 0x54) = nullptr;
    At<void*>(instance, 0x1C) = nullptr;
    Mix(Virtual<uint32_t>(instance + 4, 0x8));
}

// Place (slot 14): sides off the middle, centred ones with and without the
// random flag (corners and signs from the service's generator), the forced
// corner (0 to 3 and one past), the forced flag, no description.
void DrivePlace()
{
    GsService service;
    InitService(service, 0x5EED, 0x400, 0x400);
    StackDesc desc;
    char* const instance = NewInstance(service);
    At<char*>(instance, 0x24) = desc.bytes;
    GsBuffer buffer;
    InitBuffer(buffer);
    At<void*>(instance, 0x1C) = &buffer;
    const uint32_t sides[][3] = {{0x200, 0x200, 0}, {0x300, 0x100, 0}, {0, 0x400, 0}, {0x7FFFFFFF, 0x80000000, 0},
                                 {0x200, 0x280, 1}, {0x280, 0x200, 1}, {0x200, 0x200, 1}, {0x200, 0x200, 1},
                                 {0x200, 0x200, 1}, {0x200, 0x200, 1}, {0x200, 0x200, 1}, {0x200, 0x200, 1},
                                 {0x123, 0x456, 1}, {0x201, 0x1FF, 1}};
    auto place = [&](uint32_t x, uint32_t y, uint32_t random) {
        At<uint32_t>(desc.bytes, 0x28) = x;
        At<uint32_t>(desc.bytes, 0x2C) = y;
        At<uint8_t>(desc.bytes, 0x31) = static_cast<uint8_t>(random);
        Mix(Virtual<uint32_t>(instance, 0x38));
        Mix(At<uint8_t>(desc.bytes, 0x31));
        Mix(static_cast<uint32_t>(buffer.positioner.refs));
        Mix(service.random[0]);
    };
    for (const auto& side : sides)
        place(side[0], side[1], side[2]);
    uint8_t& forceRandom = *reinterpret_cast<uint8_t*>(addr::cGZSoundInstance_ForceRandom);
    uint8_t& forceCorner = *reinterpret_cast<uint8_t*>(addr::cGZSoundInstance_ForceCorner);
    uint32_t& corner = *reinterpret_cast<uint32_t*>(addr::cGZSoundInstance_Corner);
    const uint8_t savedRandom = forceRandom;
    const uint8_t savedForce = forceCorner;
    const uint32_t savedCorner = corner;
    forceCorner = 1;
    for (uint32_t c : {0u, 1u, 2u, 3u, 7u}) {
        corner = c;
        place(0x200, 0x200, 1);
    }
    forceCorner = 0;
    forceRandom = 1;
    place(0x200, 0x300, 0);
    place(0x100, 0x100, 0);
    forceRandom = savedRandom;
    forceCorner = savedForce;
    corner = savedCorner;
    At<char*>(instance, 0x24) = nullptr;
    Mix(Virtual<uint32_t>(instance, 0x38));
    At<void*>(instance, 0x1C) = nullptr;
    Mix(Virtual<uint32_t>(instance + 4, 0x8));
}

// The objects: an instance made without a service, destroyed by the
// deleting destructor's address with and without its flag (the listener
// and buffer released), released to its end by the address; a description
// released to its end by the address.
void DriveSoundObjects()
{
    alignas(4) char local[0x5C];
    std::memset(local, 0xCD, sizeof local);
    MixIs(CallThis<char*>(addr::cGZSoundInstance_Construct, local), local);
    MixInstance(local, true);
    MixIs(At<void*>(local, 0x18), nullptr);
    GsListener listener;
    InitListener(listener, nullptr);
    GsBuffer buffer;
    InitBuffer(buffer);
    At<void*>(local, 0x54) = &listener;
    At<void*>(local, 0x1C) = &buffer;
    MixIs(CallThis<char*>(addr::cGZSoundInstance_DeletingDestructor, local, 0u), local);
    Mix(static_cast<uint32_t>(listener.refs));
    Mix(static_cast<uint32_t>(buffer.refs));

    char* const heap = static_cast<char*>(reinterpret_cast<void*(__cdecl*)()>(addr::cGZSoundDirector_New5C)());
    MixInstance(heap, false);
    InitListener(listener, nullptr);
    InitBuffer(buffer);
    At<void*>(heap, 0x54) = &listener;
    At<void*>(heap, 0x1C) = &buffer;
    MixIs(CallThis<char*>(addr::cGZSoundInstance_DeletingDestructor, heap, 1u), heap);
    Mix(static_cast<uint32_t>(listener.refs));
    Mix(static_cast<uint32_t>(buffer.refs));

    char* const counted = static_cast<char*>(reinterpret_cast<void*(__cdecl*)()>(addr::cGZSoundDirector_New5C)());
    Mix(CallThis<uint32_t>(addr::cGZSoundInstance_AddRef, counted));
    Mix(CallThis<uint32_t>(addr::cGZSoundInstance_AddRef, counted));
    Mix(CallThis<uint32_t>(addr::cGZSoundInstance_Release, counted));
    Mix(CallThis<uint32_t>(addr::cGZSoundInstance_Release, counted));

    char* const desc = NewDesc();
    Mix(Virtual<uint32_t>(desc, 0x4));
    Mix(CallThis<uint32_t>(addr::cGZSoundDesc_Release, desc));
}

// The director: one made on the stack (its classes through the COM's
// lookup), the static one's getter.
void DriveSoundDirector()
{
    alignas(4) char director[0x4C];
    MixIs(CallThis<char*>(addr::cGZSoundDirector_Construct, director), director);
    Mix(At<std::uintptr_t>(director, 0) == addr::cGZSoundDirector_Vtable ? 1u : 0u);
    Mix(At<std::uintptr_t>(director, 4) == addr::cGZSoundDirector_Vtable2 ? 1u : 0u);
    Mix(Virtual<uint32_t>(director, 0x34));
    g_xpClasses = 0;
    Virtual<uint32_t>(director, 0x14, &CountClass, nullptr);
    Mix(g_xpClasses);
    const uint32_t asks[][2] = {{0x5C1CF5A3, 1},          {0x7DD9C9F2, 0x1DD9C9DE}, {0x7DD9C9F2, 1},
                                {0xBDD9D7A5, 0x5DD9D787}, {0xBDD9D7A5, 1},          {0xBDD9D7A5, 0x1DD9C9DE},
                                {0x3C1CF59E, 1},          {0xBDDA72DD, 1},          {0x11111111, 1}};
    for (const auto& ask : asks) {
        void* out = nullptr;
        Mix(Virtual<uint32_t>(director, 0x18, ask[0], ask[1], &out) & 0xFF);
        Mix(out ? 1u : 0u);
        if (out)
            Mix(XpRelease(out));
    }
    CallThis<void>(addr::cRZCOMDllDirector_Destruct, director);
    void* const instance = reinterpret_cast<void*(__cdecl*)()>(addr::cGZSoundDirector_Get)();
    MixIs(instance, reinterpret_cast<void*>(addr::cGZSoundDirector_Instance));
    Mix(*reinterpret_cast<const uint32_t*>(addr::cGZSoundDirector_Guard) & 1);
    Mix(Virtual<uint32_t>(instance, 0x34));
}

// OPENSC4_TEST_GZSOUND=<day>: part u5's functions called through their
// slots and addresses with a service, buffer, listener and descriptions of
// our own: the description, the table, the instance's own slots, Update,
// Start, Place, the objects' constructors and destructors, the director.
// Every result goes into the fingerprint's `gzs` part; addresses never do.
void DriveGzSound(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_GZSOUND") || !game::IsBuild641())
        return;
    g_gsOn = true;
    uint32_t* const saved = g_into;
    g_into = &g_gsHash;
    DriveDesc();
    GsStage("desc");
    DriveTable5();
    GsStage("table");
    DriveInstance();
    GsStage("instance");
    DriveUpdate();
    GsStage("update");
    DriveStart();
    GsStage("start");
    DrivePlace();
    GsStage("place");
    DriveSoundObjects();
    GsStage("objects");
    DriveSoundDirector();
    GsStage("director");
    g_into = saved;
    log::Write("autotest: gzsound driven (hash %08X)", g_gsHash);
}

void GsFingerprint(cISC4City*, std::string& line)
{
    if (!g_gsOn)
        return;
    char text[32];
    std::snprintf(text, sizeof text, " | gzs %08X", g_gsHash);
    line += text;
}

const FingerprintPart kGsFingerprint{"gzsound", 4, &GsFingerprint};
const DayHook kGsDrive{"gzsound", 4, &DriveGzSound};

// ====================================================================================
// Part u6: the framework's sound service 0x3C1CF59E and the sound table's
// loader (OPENSC4_TEST_GZSOUNDSVC)

bool g_svOn = false;
uint32_t g_svHash = 2166136261u;

void SvStage(const char* name) { log::Write("autotest: gzsoundsvc: %s %08X", name, g_svHash); }

// An EAX: addresses (above 0x10000) by their low byte only.
void MixWord(uint32_t eax)
{
    if (eax > 0x10000 && eax < 0xFFFF0000u)
        Mix(0xA0000000u | (eax & 0xFF));
    else
        Mix(eax);
}

constexpr std::size_t kSvTable = 0x1C;
constexpr std::size_t kSvInstances = 0x74;

char* NewService()
{
    char* const service = static_cast<char*>(New(0x4144));
    MixIs(CallThis<char*>(addr::cGZSoundService_Construct, service), service);
    return service;
}

char* NewTable() { return static_cast<char*>(reinterpret_cast<void*(__cdecl*)()>(addr::cGZSoundDirector_New4048)()); }

// A description's list of pairs (STLport nodes: next, previous, the pair).
void MixPairs(char* desc)
{
    char* const head = At<char*>(desc, 0x64);
    uint32_t count = 0;
    for (char* node = At<char*>(head, 0); node != head && count < 64; node = At<char*>(node, 0), ++count) {
        Mix(At<uint32_t>(node, 0x8));
        Mix(At<uint32_t>(node, 0xC));
    }
    Mix(count);
}

// The table's settings, and every cell's description (its fields, name and
// pairs) by its place.
void MixCells(char* table)
{
    MixTable(table);
    for (uint32_t i = 0; i < 64 * 64; ++i) {
        char* const desc = At<char*>(table, 0x48 + i * 4);
        if (!desc)
            continue;
        Mix(i);
        MixDesc(desc, false);
        MixPairs(desc);
    }
}

void MixService(char* service)
{
    Mix(At<std::uintptr_t>(service, 0) == addr::cGZSoundService_Vtable ? 1u : 0u);
    Mix(At<std::uintptr_t>(service, 4) == addr::cGZSoundService_TargetVtable ? 1u : 0u);
    Mix(At<uint8_t>(service, 0x8));
    Mix(At<uint32_t>(service, 0xC));
    Mix(At<void*>(service, 0x10) ? 1u : 0u);
    Mix(At<uint32_t>(service, 0x14));
    Mix(At<void*>(service, 0x18) ? 1u : 0u);
    char* const table = At<char*>(service, kSvTable);
    Mix(table ? 1u : 0u);
    Mix(At<uint8_t>(service, 0x20));
    Mix(At<uint8_t>(service, 0x21));
    // +0x50 is left unset by the constructor (the cell count writes it).
    for (std::size_t offset = 0x24; offset < 0x74; offset += 4) {
        if (offset != 0x50)
            Mix(At<uint32_t>(service, offset));
    }
    uint32_t instances = 0;
    for (uint32_t i = 0; i < 64 * 64; ++i) {
        char* const instance = At<char*>(service, kSvInstances + i * 4);
        if (!instance)
            continue;
        ++instances;
        Mix(i);
        MixIs(At<void*>(instance, 0x18), service);
        if (table)
            MixIs(At<void*>(instance, 0x24), At<void*>(table, 0x48 + i * 4));
        Mix(At<uint32_t>(instance, 0x34));
    }
    Mix(instances);
    MixString(service + 0x4074);
    Mix(At<uint32_t>(service, 0x4088));
    Mix(At<uint32_t>(service, 0x408C));
    Mix(At<uint8_t>(service, 0x4090));
    Mix(At<uint8_t>(service, 0x4091));
    Mix(At<void*>(service, 0x4094) ? 1u : 0u);
    Mix(At<uint32_t>(service, 0x4098));
    Mix(At<uint32_t>(service, 0x409C));
    // The byte +0x40A0 is left unset by the constructor too (Fade writes it).
    for (std::size_t offset = 0x40A4; offset <= 0x40B8; offset += 4)
        Mix(At<uint32_t>(service, offset));
    for (std::size_t offset = 0x40BC; offset < 0x40C6; ++offset)
        Mix(At<uint8_t>(service, offset));
    for (std::size_t offset = 0x40C8; offset < 0x4140; offset += 4)
        Mix(At<uint32_t>(service, offset));
    Mix(At<void*>(service, 0x4140) ? 1u : 0u);
}

// A file as the table's reader uses it: slot 5 a close, 7 the size, 14 a
// read; every call goes into the hash.
struct SvFile {
    void* const* vtable;
    const char* text;
    uint32_t size;
};
uint32_t __fastcall SvFileClose(SvFile*, void*)
{
    Mix(0x7514);
    return 0x14;
}
uint32_t __fastcall SvFileSize(SvFile* self, void*)
{
    Mix(0x751C);
    return self->size;
}
uint32_t __fastcall SvFileRead(SvFile* self, void*, void* to, uint32_t size)
{
    Mix(0x7538);
    Mix(size);
    std::memcpy(to, self->text, size);
    return 1;
}

void* const* SvFileVtable()
{
    static void* table[16];
    if (!table[0]) {
        for (void*& slot : table)
            slot = reinterpret_cast<void*>(&GsTrap);
        table[5] = reinterpret_cast<void*>(&SvFileClose);
        table[7] = reinterpret_cast<void*>(&SvFileSize);
        table[14] = reinterpret_cast<void*>(&SvFileRead);
    }
    return table;
}

// One cell line: tab-separated numbers, then the rest as written.
std::string CellLine(std::initializer_list<int32_t> numbers, const char* rest)
{
    std::string line;
    for (int32_t n : numbers)
        line += "\t" + std::to_string(n);
    line += "\t";
    line += rest;
    line += "\n";
    return line;
}

std::string TableHeader(const char* version, int32_t rows, int32_t columns, int32_t grid)
{
    std::string text = version;
    text += "\na comment\n";
    for (int32_t n : {0x300, grid, 3, 4, rows, columns, 120, 5, 1, 1, 7, 8, 9, 10})
        text += "\t" + std::to_string(n);
    text += "\nthe cells\n";
    return text;
}

// The text form in versions 7, 6 and 5, edge cells (NONE in either case,
// names with and without pairs, adjacent pairs, a short line keeping the
// last cell's flags), other versions, none, no cells. Each text loaded into
// a new table by the loader's address; the table and what is left of the
// text go into the hash.
void LoadText(const std::string& text)
{
    char* const table = NewTable();
    GameText input(text.c_str());
    MixWord(CallThis<uint32_t>(addr::cGZSoundTable_Load, table, input.get()) & 0xFF);
    MixCells(table);
    MixString(input.get());
    Mix(Virtual<uint32_t>(table, 0x4));
    Mix(Virtual<uint32_t>(table, 0x8));
}

std::string Version7(int32_t grid)
{
    std::string text = TableHeader("version 7", 2, 3, grid);
    text += CellLine({0x400, 1, 0x200, 0x100, 0, 11, 250, 12, 1, 3, 1, 13, 14, 15, 16, 1, 0, 1}, "Bird\t(1,2)\t(3,4)");
    text += CellLine({0x300, 0, 0x210, 0x110, 1, 21, 0, 22, 0, 4, 0, 23, 24, 25, 26, 0, 1, 0}, "NONE\t(5,6)");
    text += CellLine({0x200, 1, 0x220, 0x120, 1, 31, 100, 32, 1, 5, 1, 33, 34, 35, 36, 1, 1, 1}, "none");
    text += CellLine({0x100, 0, 0x230, 0x130, 0, 41, 200, 42, 0, 6, 0, 43, 44, 45, 46, 0, 2, 0}, "Wind(7,8)");
    text += CellLine({0x080, 1, 0x240, 0x140, 1, 51, 300, 52, 1, 7, 1, 53, 54, 55, 56, 1, 31, 1}, "Rain\t(9,10)(11,12)");
    text += "\t77\t1\n";
    return text;
}

void DriveLoad()
{
    LoadText(Version7(0));
    LoadText(Version7(1));
    std::string six = TableHeader("version\t6", 2, 2, 0);
    for (int i = 0; i < 4; ++i)
        six += CellLine({0x100 * i, i & 1, 0x200, 0x200, 1, i, 50 * i, 3, 0, 2, 1, 4, 5, 6, 7, i & 1, i, 1},
                        i == 2 ? "NONE" : "Six\t(1,1)\t(2,2)\t(3,3)");
    LoadText(six);
    std::string five = TableHeader("version5", 1, 2, 1);
    five += CellLine({1, 0, 2, 3, 0, 4, 5, 6, 0, 7, 0, 8, 9, 10, 11, 0, 12}, "Five");
    five += CellLine({1, 1, 2, 3, 1, 4, 5, 6, 1, 7, 1, 8, 9, 10, 11, 1, 12}, "fIVE\t(4,4)");
    LoadText(five);
    LoadText(TableHeader("version 8", 1, 1, 0) + CellLine({1}, "Eight"));
    LoadText(TableHeader("Version 7", 1, 1, 0) + CellLine({1}, "Upper"));
    LoadText("no version here\n");
    LoadText(TableHeader("version 7", 0, 3, 0) + CellLine({1}, "Nothing"));
    LoadText(TableHeader("version 7", 3, 0, 0) + CellLine({1}, "Nothing"));
    LoadText("");

    // The reader: an empty file, then texts through it. (A text whose first
    // cell leaves a flag unread is not tried: the game's flag is whatever its
    // stack held.)
    const std::string solo =
        TableHeader("version 6", 1, 1, 0) + CellLine({9, 1, 8, 7, 1, 6, 5, 4, 1, 3, 1, 2, 1, 0, 1, 1, 3}, "Solo");
    for (const std::string& text : {std::string(), Version7(1), solo}) {
        SvFile file{SvFileVtable(), text.c_str(), static_cast<uint32_t>(text.size())};
        char* const table = NewTable();
        MixWord(CallThis<uint32_t>(addr::cGZSoundTable_Read, table, static_cast<void*>(&file)));
        MixCells(table);
        Mix(Virtual<uint32_t>(table, 0x4));
        Mix(Virtual<uint32_t>(table, 0x8));
    }
}

// Init (slot 3) with a key that has no record and with a score the audio
// layer loads (an LTEXT record), which makes the table and prepares the
// service; then the table and the service.
char* DriveInit()
{
    char* const service = NewService();
    const uint32_t missing[3] = {0x2026960B, 0x12345678, 0x9ABCDEF0};
    MixWord(Virtual<uint32_t>(service, 0xC, missing) & 0xFF);
    MixService(service);
    const uint32_t score[3] = {0x2026960B, 0x0A5BCDA5, 0x7EC82BF0};
    MixWord(Virtual<uint32_t>(service, 0xC, score) & 0xFF);
    MixService(service);
    if (char* const table = At<char*>(service, kSvTable))
        MixCells(table);
    return service;
}

// A service over a table of our own text, prepared.
char* PreparedService(const std::string& text)
{
    char* const service = NewService();
    char* const table = NewTable();
    GameText input(text.c_str());
    CallThis<uint32_t>(addr::cGZSoundTable_Load, table, input.get());
    At<char*>(service, kSvTable) = table;
    MixWord(Virtual<uint32_t>(service, 0x10));
    MixService(service);
    MixWord(Virtual<uint32_t>(service, 0x10));
    return service;
}

std::string WideText()
{
    std::string text = TableHeader("version 7", 6, 7, 0);
    for (int32_t i = 0; i < 42; ++i) {
        const int32_t group = i % 9 == 0 ? 0 : (i % 5) + (i % 7 == 0 ? 20 : 0);
        text += CellLine({0x40 * (i % 16), i & 1, 0x200, 0x200, 0, i, 10 * i, 0, 0, 2, i % 3 == 0, 0, 0, 0, 0, 0,
                          group, 0},
                         i % 6 == 5 ? "NONE" : "Cell");
    }
    return text;
}

// Play, the tempo and the beat time, the level, the fade and its steps
// (with the lock held too), Stop, Reseed, the cell count over windows,
// Shutdown; the service counted down to its deletion.
void DrivePlay(char* service)
{
    MixWord(Virtual<uint32_t>(service, 0x20));
    MixService(service);
    MixWord(Virtual<uint32_t>(service, 0x20));
    MixService(service);

    MixWord(Virtual<uint32_t>(service, 0x44, At<uint32_t>(service, 0x24)));
    MixWord(Virtual<uint32_t>(service, 0x44, 90u));
    MixService(service);
    for (uint32_t beat : {0u, 1u, 5u, 1000u, 0x80000000u, 0xFFFFFFFFu})
        Mix(CallThis<uint32_t>(addr::cGZSoundService_BeatTime, service, beat));
    const uint32_t tempo = At<uint32_t>(service, 0x24);
    At<uint32_t>(service, 0x24) = 0;
    Mix(CallThis<uint32_t>(addr::cGZSoundService_BeatTime, service, 7u));
    At<uint32_t>(service, 0x24) = tempo;

    MixWord(Virtual<uint32_t>(service, 0x2C, 0x280u));
    MixService(service);

    // The fade: to the same level (at once), then up with a timer (a long
    // one: its first call would come after this test), a second fade while
    // it runs, its steps by the callback's address.
    const uint32_t level = At<uint32_t>(service, 0x14);
    MixWord(Virtual<uint32_t>(service, 0x30, 200000, level, 0u));
    Mix(At<uint8_t>(service, 0x40A0));
    MixService(service);
    MixWord(Virtual<uint32_t>(service, 0x30, 200000, level + 0x100, 0u));
    Mix(At<uint8_t>(service, 0x40A0));
    MixService(service);
    MixWord(Virtual<uint32_t>(service, 0x30, 400000, level + 0x40, 1u));
    Mix(At<uint8_t>(service, 0x40A0));
    MixService(service);
    struct Step {
        uint32_t level;
        int32_t step, target;
    };
    const Step steps[] = {{0x100, 0x14, 0x200}, {0x300, 0x300, 0x500}, {0x100, -0x400, -5}, {0x300, -0x10, 0x100},
                          {0x200, 0, 0x100},    {0x180, 0x100, 0x200}};
    for (const Step& s : steps) {
        At<uint32_t>(service, 0x14) = s.level;
        At<int32_t>(service, 0x4098) = s.step;
        At<int32_t>(service, 0x409C) = s.target;
        MixWord(CallThis<uint32_t>(addr::cGZSoundService_FadeStep, service));
        MixService(service);
    }
    // Not fading any more: nothing.
    MixWord(CallThis<uint32_t>(addr::cGZSoundService_FadeStep, service));
    // With the lock held: nothing, the count answered.
    {
        alignas(4) char guard[8];
        CallThis<void*>(addr::SC4LockGuard_Construct, guard, reinterpret_cast<void*>(addr::cGZSoundService_Lock));
        Mix(CallThis<uint32_t>(addr::cGZSoundService_FadeStep, service));
        Virtual<uint32_t>(At<void*>(guard, 4), 0x8);
    }
    // A fade down to a stop: the timer, then the step past the target.
    MixWord(Virtual<uint32_t>(service, 0x30, 200000, 0x100u, 1u));
    Mix(At<uint8_t>(service, 0x40A0));
    At<uint32_t>(service, 0x14) = 0x110;
    MixWord(CallThis<uint32_t>(addr::cGZSoundService_FadeStep, service));
    MixService(service);
    // Not playing: refused.
    MixWord(Virtual<uint32_t>(service, 0x30, 200000, 0x300u, 0u));
    MixService(service);

    MixWord(Virtual<uint32_t>(service, 0x20));
    MixWord(Virtual<uint32_t>(service, 0x28));
    MixService(service);
    MixWord(Virtual<uint32_t>(service, 0x6C, 12345u));
    Mix(static_cast<uint32_t>(CallThis<uint64_t>(addr::cRZRandom_Advance, At<void*>(service, 0x10))));
    MixService(service);
    MixWord(Virtual<uint32_t>(service, 0x20));
    MixWord(Virtual<uint32_t>(service, 0x6C, 777u));
    Mix(static_cast<uint32_t>(CallThis<uint64_t>(addr::cRZRandom_Advance, At<void*>(service, 0x10))));
    MixService(service);
    MixWord(Virtual<uint32_t>(service, 0x28));

    // The count over windows (column, row, spreads).
    const int32_t windows[][4] = {{1, 1, 3, 3}, {3, 2, 1, 1}, {0, 0, 0, 0},  {6, 5, 0, 0},
                                  {2, 3, 9, 9}, {4, 4, 1, 2}, {-3, 2, 1, 1}, {2, 2, -1, -1}};
    for (const auto& w : windows) {
        At<int32_t>(service, 0x28) = w[0];
        At<int32_t>(service, 0x2C) = w[1];
        At<int32_t>(service, 0x54) = w[2];
        At<int32_t>(service, 0x58) = w[3];
        MixWord(CallThis<uint32_t>(addr::cGZSoundService_CountCells, service));
        Mix(At<uint32_t>(service, 0x38));
        Mix(At<uint32_t>(service, 0x50));
    }

    MixWord(Virtual<uint32_t>(service, 0x1C));
    MixService(service);
    MixWord(Virtual<uint32_t>(service, 0x1C));
    MixService(service);
    Mix(Virtual<uint32_t>(service, 0x4));
    Mix(Virtual<uint32_t>(service, 0x4));
    Mix(Virtual<uint32_t>(service, 0x8));
    Mix(CallThis<uint32_t>(addr::cGZSoundService_Release, service));
}

// The row and column from places (with and without the grid, over
// spreads), the plain setters before and after Prepare, the window's edges,
// the instance and table getters, LoadByName with a name that is not there.
void DriveWindow()
{
    char* const fresh = NewService();
    // Not prepared: AL 1 over the caller's EAX (ours 1), so AL only.
    for (std::size_t slot : {0x34u, 0x38u, 0x3Cu, 0x40u, 0x54u})
        MixByte(Virtual<uint32_t>(fresh, slot, 0x1234u));
    MixService(fresh);

    char* const service = PreparedService(WideText());
    char* const table = At<char*>(service, kSvTable);
    MixIs(Virtual<void*>(service, 0x18, 0xDEADu), table);
    for (uint32_t grid : {0u, 1u}) {
        At<uint32_t>(table, 0xC) = grid;
        for (int32_t spread : {0, 1, 3, -1}) {
            At<int32_t>(service, 0x54) = spread;
            At<int32_t>(service, 0x58) = spread;
            for (uint32_t place : {0u, 1u, 166u, 499u, 500u, 833u, 999u, 1000u, 2000u, 0xFFFFFFFFu}) {
                MixWord(Virtual<uint32_t>(service, 0x34, place));
                Mix(At<uint32_t>(service, 0x2C));
                MixWord(Virtual<uint32_t>(service, 0x38, place));
                Mix(At<uint32_t>(service, 0x28));
                Mix(At<uint32_t>(service, 0x38));
                Mix(At<uint32_t>(service, 0x50));
            }
        }
    }
    At<uint32_t>(table, 0xC) = 0;
    MixWord(Virtual<uint32_t>(service, 0x3C, 0x77u));
    MixWord(Virtual<uint32_t>(service, 0x40, 0x88u));
    MixByte(Virtual<uint32_t>(service, 0x54, 0x1FFu));
    MixService(service);

    const int32_t edges[][4] = {{1, 1, 3, 3},
                                {5, 6, 1, 1},
                                {0, 0, 0, 0},
                                {9, 9, 2, 2},
                                {0x7FFFFFFF, 0x7FFFFFFF, 1, 1},
                                {0, 0, -0x7FFFFFFF - 1, -0x7FFFFFFF - 1},
                                {3, 2, -2, -1}};
    for (const auto& e : edges) {
        At<int32_t>(service, 0x28) = e[0];
        At<int32_t>(service, 0x2C) = e[1];
        At<int32_t>(service, 0x54) = e[2];
        At<int32_t>(service, 0x58) = e[3];
        for (std::size_t slot : {0x58u, 0x5Cu, 0x60u, 0x64u})
            Mix(Virtual<uint32_t>(service, slot));
    }
    const uint32_t cells[][2] = {{0, 0}, {0, 5}, {1, 3}, {5, 6}, {2, 0}, {63, 63}, {0, 64}};
    for (const auto& c : cells) {
        void* const got = Virtual<void*>(service, 0x68, c[0], c[1], 0xBEEFu);
        MixIs(got, At<void*>(service, kSvInstances + (c[0] * 64 + c[1]) * 4));
    }

    Virtual<uint32_t>(service, 0x1C);
    Virtual<uint32_t>(service, 0x4);
    Mix(Virtual<uint32_t>(service, 0x8));

    char* const named = NewService();
    GameText name("no such score.txt");
    MixWord(Virtual<uint32_t>(named, 0x14, name.get(), 0xDEADu) & 0xFF);
    MixService(named);
    MixWord(Virtual<uint32_t>(named, 0x18, 0u) ? 1u : 0u);
    Virtual<uint32_t>(named, 0x4);
    Mix(Virtual<uint32_t>(named, 0x8));
    Virtual<uint32_t>(fresh, 0x4);
    Mix(Virtual<uint32_t>(fresh, 0x8));
}

// Slots 28 to 35 over edge values.
void DriveSettings()
{
    char* const service = NewService();
    MixByte(Virtual<uint32_t>(service, 0x70, 0x1FEu));
    MixWord(Virtual<uint32_t>(service, 0x74, 0x11u, 0x22334455u));
    for (uint32_t value : {0u, 1u, 2u, 3u, 0x102u, 0xFFFFFFFFu}) {
        MixWord(Virtual<uint32_t>(service, 0x78, value));
        MixWord(Virtual<uint32_t>(service, 0x7C, value));
    }
    for (uint32_t row : {0u, 1u, 2u, 3u, 0x100u}) {
        for (uint32_t column : {0u, 2u, 3u, 0x201u}) {
            MixWord(Virtual<uint32_t>(service, 0x80, row, column, 0x100u + row * 3 + column));
            const uint32_t eax = Virtual<uint32_t>(service, 0x84, row, column, row, column, 0xFFFFFFF0u + row);
            MixWord(row < 3 ? eax : eax & 0xFF);
        }
    }
    const uint32_t key[3] = {0x11111111, 0x22222222, 0x33333333};
    MixOffset(Virtual<void*>(service, 0x88, key), service);
    MixOffset(Virtual<void*>(service, 0x8C), service);
    MixService(service);
    Virtual<uint32_t>(service, 0x4);
    Mix(Virtual<uint32_t>(service, 0x8));
}

// The objects: the interface (also through the part at +4), the count, the
// destructor with a reference held, the deleting destructor without its
// flag, the table's deleting destructor by its slot and its address.
void DriveSvObjects()
{
    char* const service = NewService();
    MixService(service);
    for (uint32_t iid : {0xDC1CF595u, 1u, 0x3C1CF59Eu, 0u}) {
        void* out = nullptr;
        MixWord(Virtual<uint32_t>(service, 0x0, iid, &out) & 0xFF);
        MixIs(out, service);
        out = nullptr;
        MixWord(Virtual<uint32_t>(service + 4, 0x0, iid, &out) & 0xFF);
        MixIs(out, service);
        Mix(At<uint32_t>(service, 0xC));
    }
    // Two references from the two QueryInterfaces: one released, then the
    // count past the end (0 to -1) and from 5.
    Mix(Virtual<uint32_t>(service + 4, 0x8));
    Mix(At<uint32_t>(service, 0xC));
    At<uint32_t>(service, 0xC) = 0;
    Mix(Virtual<uint32_t>(service, 0x8));
    At<uint32_t>(service, 0xC) = 5;
    Mix(Virtual<uint32_t>(service, 0x8));

    // The destructor with a reference held (Shutdown lets it go first).
    FakeCounted held{kCountedVtable, 1};
    At<void*>(service, 0x4140) = &held;
    MixWord(CallThis<uint32_t>(addr::cGZSoundService_Destruct, service));
    Mix(static_cast<uint32_t>(held.refs));
    Mix(At<void*>(service, 0x10) ? 1u : 0u);
    Mix(At<void*>(service, 0x4140) ? 1u : 0u);
    Mix(At<std::uintptr_t>(service, 0) == addr::cGZSoundService_Vtable ? 1u : 0u);
    MixIs(CallThis<char*>(addr::cGZSoundService_Construct, service), service);
    MixIs(CallThis<char*>(addr::cGZSoundService_DeletingDestructor, service, 0u), service);
    Mix(At<void*>(service, 0x10) ? 1u : 0u);
    CallThis<char*>(addr::cGZSoundService_Construct, service);
    MixIs(CallThis<char*>(addr::cGZSoundService_DeletingDestructor, service, 1u), service);

    char* const table = NewTable();
    At<char*>(table, 0x48) = NewDesc();
    MixIs(Virtual<char*>(table, 0x10, 1u), table);
    char* const other = NewTable();
    At<char*>(other, 0x48 + 77 * 4) = NewDesc();
    MixIs(CallThis<char*>(addr::cGZSoundTable_DeletingDestructor, other, 0u), other);
    MixTable(other);
    CallThis<char*>(addr::cGZSoundTable_DeletingDestructor, other, 1u);
}

// ====================================================================================
// Part u7: slots 38 to 64, the beats, the sound instance's play (the same
// test). The audio system and the sound service the game caches
// (0x00B628CC, 0x00B628C8) are ours for these stages, and so are the
// buffers, the listener and the stopwatch: nothing reaches real audio.

// A buffer and its interfaces in one object: 0xFD584467 (slot 3), the place
// 0x306077D2 (slots 5, 12, 14, 15), the handle 0x5B34F0AF; slots 6, 7, 20,
// 21, 22, 26 and 27 of the buffer. Every call goes into the hash.
struct SvSound {
    void* const* vtable;
    int32_t refs;
    bool control;
    bool handle;
    uint32_t playing;
};
uint32_t __fastcall SvSoundQueryInterface(SvSound* self, void*, uint32_t iid, void** out)
{
    Mix(0x8100);
    Mix(iid);
    const bool yes = iid == 0x306077D2 || (iid == 0xFD584467 && self->control) || (iid == 0x5B34F0AF && self->handle);
    if (!yes)
        return 0;
    ++self->refs;
    *out = self;
    return 1;
}
uint32_t __fastcall SvSoundAddRef(SvSound* self, void*) { return static_cast<uint32_t>(++self->refs); }
uint32_t __fastcall SvSoundRelease(SvSound* self, void*)
{
    Mix(0x8108);
    return static_cast<uint32_t>(--self->refs);
}
uint32_t __fastcall SvSoundControl(SvSound*, void*, uint32_t a, uint32_t b, uint32_t c)
{
    Mix(0x810C);
    Mix(a);
    Mix(b);
    Mix(c);
    return 0x0C00;
}
uint32_t __fastcall SvSoundPlace(SvSound*, void*, float x, float y, float z)
{
    Mix(0x8114);
    Mix(Bits32(x));
    Mix(Bits32(y));
    Mix(Bits32(z));
    return 0x1400;
}
uint32_t __fastcall SvSoundPlay(SvSound*, void*, uint32_t a, uint32_t b, uint32_t c)
{
    Mix(0x8118);
    Mix(a);
    Mix(b);
    Mix(c);
    return 0x1800;
}
uint32_t __fastcall SvSoundPlaying(SvSound* self, void*)
{
    Mix(0x811C);
    return self->playing;
}
uint32_t __fastcall SvSoundFloat(SvSound*, void*, float value)
{
    Mix(0x8130);
    Mix(Bits32(value));
    return 0x3000;
}
uint32_t __fastcall SvSoundByte(SvSound*, void*, uint32_t value)
{
    Mix(0x813C);
    Mix(value);
    return 0x3C00;
}
uint32_t __fastcall SvSoundStatus(SvSound*, void*)
{
    Mix(0x8150);
    return 0;
}
uint32_t __fastcall SvSoundLevel(SvSound*, void*, uint32_t level)
{
    Mix(0x8154);
    Mix(level);
    return 0x5400;
}
uint32_t __fastcall SvSoundPlay4(SvSound*, void*, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    Mix(0x8158);
    Mix(a);
    Mix(b);
    Mix(c);
    Mix(d);
    return 0x5800;
}
uint32_t __fastcall SvSoundRate(SvSound*, void*)
{
    Mix(0x8168);
    return 22050;
}
uint32_t __fastcall SvSoundSetRate(SvSound*, void*, uint32_t rate)
{
    Mix(0x816C);
    Mix(rate);
    return 0x6C00;
}

void* const* SvSoundVtable()
{
    static void* table[28];
    if (!table[0]) {
        for (void*& slot : table)
            slot = reinterpret_cast<void*>(&GsTrap);
        table[0] = reinterpret_cast<void*>(&SvSoundQueryInterface);
        table[1] = reinterpret_cast<void*>(&SvSoundAddRef);
        table[2] = reinterpret_cast<void*>(&SvSoundRelease);
        table[3] = reinterpret_cast<void*>(&SvSoundControl);
        table[5] = reinterpret_cast<void*>(&SvSoundPlace);
        table[6] = reinterpret_cast<void*>(&SvSoundPlay);
        table[7] = reinterpret_cast<void*>(&SvSoundPlaying);
        table[12] = reinterpret_cast<void*>(&SvSoundFloat);
        table[14] = reinterpret_cast<void*>(&SvSoundFloat);
        table[15] = reinterpret_cast<void*>(&SvSoundByte);
        table[20] = reinterpret_cast<void*>(&SvSoundStatus);
        table[21] = reinterpret_cast<void*>(&SvSoundLevel);
        table[22] = reinterpret_cast<void*>(&SvSoundPlay4);
        table[26] = reinterpret_cast<void*>(&SvSoundRate);
        table[27] = reinterpret_cast<void*>(&SvSoundSetRate);
    }
    return table;
}

// The audio system: slots 17 (by name) and 18 (by id) give the buffer, no
// buffer, or refuse.
struct SvAudio {
    void* const* vtable;
    uint32_t mode; // 0 refuse, 1 no buffer, 2 the buffer
    SvSound* sound;
};
uint32_t SvAudioGive(SvAudio* self, uint32_t iid, void** out)
{
    Mix(iid);
    if (self->mode == 2) {
        ++self->sound->refs;
        *out = self->sound;
    }
    return self->mode != 0 ? 1u : 0u;
}
uint32_t __fastcall SvAudioByName(SvAudio* self, void*, const void* name, uint32_t a, uint32_t b, uint32_t iid,
                                  void** out)
{
    Mix(0x8244);
    MixString(name);
    Mix(a);
    Mix(b);
    return SvAudioGive(self, iid, out);
}
uint32_t __fastcall SvAudioById(SvAudio* self, void*, uint32_t id, uint32_t a, uint32_t b, uint32_t iid, void** out)
{
    Mix(0x8248);
    Mix(id);
    Mix(a);
    Mix(b);
    return SvAudioGive(self, iid, out);
}

// The framework's sound service: slots 30 (take a target) and 31 (let it
// go).
struct SvSoundService {
    void* const* vtable;
    uint32_t taken;
};
uint32_t __fastcall SvServiceTake(SvSoundService* self, void*, void*, uint32_t period)
{
    Mix(0x8378);
    Mix(period);
    ++self->taken;
    return 0x7800;
}
uint32_t __fastcall SvServiceDrop(SvSoundService* self, void*, void*)
{
    Mix(0x837C);
    --self->taken;
    return 0x7C00;
}

// The instance's listener: slots 3 (play; refused or not), 4 (finished), 5
// (place), 6 (level), 7 (pitch), 8 (distances), 9 (go on).
struct SvListener {
    void* const* vtable;
    int32_t refs;
    uint32_t accept;
};
uint32_t __fastcall SvListenerAddRef(SvListener* self, void*) { return static_cast<uint32_t>(++self->refs); }
uint32_t __fastcall SvListenerRelease(SvListener* self, void*) { return static_cast<uint32_t>(--self->refs); }
uint32_t __fastcall SvListenerPlay(SvListener* self, void*, const char* text, uint32_t id, void*, uint32_t a,
                                   uint32_t b, float time)
{
    Mix(0x840C);
    Mix(static_cast<uint32_t>(std::strlen(text)));
    Mix(BytesChecksum(text, std::strlen(text)));
    Mix(id);
    Mix(a);
    Mix(b);
    Mix(Bits32(time));
    return 0x0C00 | self->accept;
}
uint32_t __fastcall SvListenerFinished(SvListener*, void*, void*, float time)
{
    Mix(0x8410);
    Mix(Bits32(time));
    return 0x1000;
}
uint32_t __fastcall SvListenerPlace(SvListener*, void*, void*, float x, float y, float z)
{
    Mix(0x8414);
    Mix(Bits32(x));
    Mix(Bits32(y));
    Mix(Bits32(z));
    return 0x1400;
}
uint32_t __fastcall SvListenerLevel(SvListener*, void*, void*, float level)
{
    Mix(0x8418);
    Mix(Bits32(level));
    return 0x1800;
}
uint32_t __fastcall SvListenerPitch(SvListener*, void*, void*, float pitch)
{
    Mix(0x841C);
    Mix(Bits32(pitch));
    return 0x1C00;
}
uint32_t __fastcall SvListenerDistances(SvListener*, void*, void*, float near, float far)
{
    Mix(0x8420);
    Mix(Bits32(near));
    Mix(Bits32(far));
    return 0x2000;
}
uint32_t __fastcall SvListenerGoOn(SvListener*, void*, void*)
{
    Mix(0x8424);
    return 1;
}

void* const* SvListenerVtable()
{
    static void* table[10];
    if (!table[0]) {
        for (void*& slot : table)
            slot = reinterpret_cast<void*>(&GsTrap);
        table[1] = reinterpret_cast<void*>(&SvListenerAddRef);
        table[2] = reinterpret_cast<void*>(&SvListenerRelease);
        table[3] = reinterpret_cast<void*>(&SvListenerPlay);
        table[4] = reinterpret_cast<void*>(&SvListenerFinished);
        table[5] = reinterpret_cast<void*>(&SvListenerPlace);
        table[6] = reinterpret_cast<void*>(&SvListenerLevel);
        table[7] = reinterpret_cast<void*>(&SvListenerPitch);
        table[8] = reinterpret_cast<void*>(&SvListenerDistances);
        table[9] = reinterpret_cast<void*>(&SvListenerGoOn);
    }
    return table;
}

void* const* SvAudioVtable()
{
    static void* table[19];
    if (!table[0]) {
        for (void*& slot : table)
            slot = reinterpret_cast<void*>(&GsTrap);
        table[17] = reinterpret_cast<void*>(&SvAudioByName);
        table[18] = reinterpret_cast<void*>(&SvAudioById);
    }
    return table;
}

void* const* SvServiceVtable()
{
    static void* table[32];
    if (!table[0]) {
        for (void*& slot : table)
            slot = reinterpret_cast<void*>(&GsTrap);
        table[30] = reinterpret_cast<void*>(&SvServiceTake);
        table[31] = reinterpret_cast<void*>(&SvServiceDrop);
    }
    return table;
}

SvSound g_svSound{SvSoundVtable(), 1, true, false, 1};
SvAudio g_svAudio{SvAudioVtable(), 2, &g_svSound};
SvSoundService g_svService{SvServiceVtable(), 0};
SvListener g_svListener{SvListenerVtable(), 1, 1};

// The game's two cached objects swapped for ours while a stage runs.
struct SvFakes {
    void* sound;
    void* audio;
    SvFakes()
    {
        sound = At<void*>(reinterpret_cast<void*>(addr::SC4_SoundServiceCache), 0);
        audio = At<void*>(reinterpret_cast<void*>(addr::SC4_AudioSystemCache), 0);
        At<void*>(reinterpret_cast<void*>(addr::SC4_SoundServiceCache), 0) = &g_svService;
        At<void*>(reinterpret_cast<void*>(addr::SC4_AudioSystemCache), 0) = &g_svAudio;
    }
    ~SvFakes()
    {
        At<void*>(reinterpret_cast<void*>(addr::SC4_SoundServiceCache), 0) = sound;
        At<void*>(reinterpret_cast<void*>(addr::SC4_AudioSystemCache), 0) = audio;
    }
    SvFakes(const SvFakes&) = delete;
    SvFakes& operator=(const SvFakes&) = delete;
};

// A table of rows by columns with every cell filled: weights, groups, pitch
// ranges, pairs and flags cycling.
std::string FullText(int32_t rows, int32_t columns, int32_t grid)
{
    std::string text = TableHeader("version 7", rows, columns, grid);
    const int32_t weights[] = {0, 20, 5, 12, 19, 25, -3, 11, 20, 14};
    const int32_t low[] = {0, -3, 2, 5, 0, 1};
    const int32_t high[] = {0, 2, 2, 1, 4, 12};
    for (int32_t i = 0; i < rows * columns; ++i) {
        const char* rest = i % 3 == 0 ? "Cell\t(1,2)" : i % 3 == 1 ? "a1b2c3d4.wav" : "Other(0,1)";
        text += CellLine({0x100 + 0x20 * (i % 8), i % 5 == 0, 0x200, 0x200, 0, 30 + i, 100 * (i % 4), 0, 0, 2 + i % 3,
                          i % 4 == 0, 500 + i, weights[i % 10], low[i % 6], high[i % 6], 0, i % 4, i & 1},
                         rest);
    }
    return text;
}

// The instances' states: playing (2), waiting (3) or idle, marked or not;
// the fields the constructor leaves unset set too.
void SetStates(char* service, uint32_t pattern)
{
    char* const table = At<char*>(service, kSvTable);
    for (int32_t r = 0; r < At<int32_t>(table, 0x28); ++r) {
        for (int32_t c = 0; c < At<int32_t>(table, 0x2C); ++c) {
            char* const instance = At<char*>(service, kSvInstances + (r * 64 + c) * 4);
            const uint32_t k = (static_cast<uint32_t>(r * 7 + c * 3) + pattern) % 5;
            At<uint32_t>(instance, 0x20) = k == 0 ? 0u : k == 1 ? 3u : 2u;
            At<uint8_t>(instance, 0x2D) = k == 4 ? 1 : 0;
            At<uint8_t>(instance, 0x58) = 0;
            // The constructor leaves these three unset.
            At<uint32_t>(instance, 0x28) = 0;
            At<uint32_t>(instance, 0x30) = 0x400;
            At<uint32_t>(instance, 0x38) = 0;
        }
    }
}

void MixStates(char* service)
{
    char* const table = At<char*>(service, kSvTable);
    for (int32_t r = 0; r < At<int32_t>(table, 0x28); ++r) {
        for (int32_t c = 0; c < At<int32_t>(table, 0x2C); ++c) {
            char* const instance = At<char*>(service, kSvInstances + (r * 64 + c) * 4);
            Mix(At<uint32_t>(instance, 0x20));
            Mix(At<uint8_t>(instance, 0x2D));
            Mix(At<void*>(instance, 0x1C) ? 1u : 0u);
            Mix(At<void*>(instance, 0x54) ? 1u : 0u);
        }
    }
}

char* SeededService(const std::string& text, uint32_t seed)
{
    char* const service = PreparedService(text);
    CallThis<void>(addr::cRZRandom_Seed, At<void*>(service, 0x10), seed);
    return service;
}

// The service let go: no stopwatch of ours left, the instances' buffers
// and listeners dropped, Shutdown, the count down.
void EndService(char* service)
{
    At<void*>(service, 0x18) = nullptr;
    At<uint8_t>(service, 0x20) = 0;
    for (uint32_t i = 0; i < 64 * 64; ++i) {
        if (char* const instance = At<char*>(service, kSvInstances + i * 4)) {
            At<void*>(instance, 0x1C) = nullptr;
            At<void*>(instance, 0x54) = nullptr;
            At<uint32_t>(instance, 0x20) = 0;
        }
    }
    MixWord(Virtual<uint32_t>(service, 0x1C) & 0xFF);
    Mix(Virtual<uint32_t>(service, 0x8));
}

// Slots 41 to 53, 56 and 59 to 64 on a prepared service.
void DriveU7Slots()
{
    SvFakes fakes;
    char* const service = SeededService(FullText(4, 5, 0), 101);
    // Not prepared, not playing.
    char* const fresh = NewService();
    MixByte(Virtual<uint32_t>(fresh, 0xAC, 0x200u));
    MixByte(Virtual<uint32_t>(fresh, 0xD4));
    MixByte(Virtual<uint32_t>(fresh, 0xF8));
    Virtual<uint32_t>(fresh, 0x4);
    Mix(Virtual<uint32_t>(fresh, 0x8));
    MixByte(Virtual<uint32_t>(service, 0xF8));

    // 41: the level, then Play (the fake sound service takes the target).
    MixWord(Virtual<uint32_t>(service, 0xA4, 0x300u));
    Mix(g_svService.taken);
    MixService(service);
    // 42 with a fade running (a long timer) and without.
    MixWord(Virtual<uint32_t>(service, 0x30, 400000, 0x100u, 0u));
    Mix(At<uint8_t>(service, 0x4091));
    // Slot 42 answers the timer's deletion: its address, not hashed.
    Virtual<uint32_t>(service, 0xA8);
    Mix(At<uint8_t>(service, 0x4091));
    Mix(At<void*>(service, 0x4094) ? 1u : 0u);
    Virtual<uint32_t>(service, 0xA8);
    // 43.
    MixByte(Virtual<uint32_t>(service, 0xAC, 0x280u));
    Mix(At<uint32_t>(service, 0x14));
    // 46 to 51.
    GameText name("a score");
    CallThis<void*>(addr::cRZBaseString_Assign, service + 0x4074, name.get());
    GameText out("before");
    MixWord(Virtual<uint32_t>(service, 0xB8, out.get()) & 0xFF);
    MixString(out.get());
    MixWord(Virtual<uint32_t>(service, 0xBC, 2u, 1u));
    uint32_t rows = 0, columns = 0;
    MixWord(Virtual<uint32_t>(service, 0xC0, &rows, &columns));
    Mix(rows);
    Mix(columns);
    MixWord(Virtual<uint32_t>(service, 0xC4, 0x12345u));
    MixByte(Virtual<uint32_t>(service, 0xC8, 0x101u));
    Mix(At<uint8_t>(service, 0x4090));
    Virtual<uint32_t>(service, 0xC8, 0u);
    MixWord(Virtual<uint32_t>(service, 0xCC, 7u, 9u));
    MixService(service);

    // 52 over cells and windows.
    const int32_t windows[][4] = {{2, 1, 1, 1}, {0, 0, 0, 0}, {4, 3, 2, 1}, {1, 2, -1, 3}};
    for (const auto& w : windows) {
        At<int32_t>(service, 0x28) = w[0];
        At<int32_t>(service, 0x2C) = w[1];
        At<int32_t>(service, 0x54) = w[2];
        At<int32_t>(service, 0x58) = w[3];
        for (int32_t r : {-1, 0, 1, 2, 3, 5})
            for (int32_t c : {0, 1, 3, 4, 7})
                MixWord(Virtual<uint32_t>(service, 0xD0, r, c));
        MixWord(CallThis<uint32_t>(addr::cGZSoundService_InWindow, service, 0x80000000u, 0u));
    }
    At<int32_t>(service, 0x28) = 2;
    At<int32_t>(service, 0x2C) = 1;
    At<int32_t>(service, 0x54) = 1;
    At<int32_t>(service, 0x58) = 1;

    // 59 to 64 over states, +0x4088 and the window.
    for (uint32_t pattern = 0; pattern < 5; ++pattern) {
        SetStates(service, pattern);
        for (std::size_t slot : {0xECu, 0xF0u, 0xF4u, 0xFCu, 0x100u})
            Mix(Virtual<uint32_t>(service, slot));
        MixWord(Virtual<uint32_t>(service, 0xF8));
    }
    char* const table = At<char*>(service, kSvTable);
    for (uint32_t share : {0u, 50u, 100u, 250u}) {
        At<uint32_t>(service, 0x4088) = share;
        for (int32_t groups : {0, 2, 40}) {
            At<int32_t>(service, 0x50) = groups;
            Mix(Virtual<uint32_t>(service, 0xFC));
            Mix(Virtual<uint32_t>(service, 0x100));
        }
    }
    const int32_t sizes[][2] = {{1, 1}, {1, 5}, {4, 1}, {0, 0}};
    for (const auto& s : sizes) {
        At<int32_t>(table, 0x28) = s[0];
        At<int32_t>(table, 0x2C) = s[1];
        Mix(Virtual<uint32_t>(service, 0xFC));
        Mix(Virtual<uint32_t>(service, 0xF0));
    }
    At<int32_t>(table, 0x28) = 4;
    At<int32_t>(table, 0x2C) = 5;
    At<uint32_t>(service, 0x4088) = 100;

    // 56 on cells in each state.
    for (uint32_t pattern : {0u, 2u}) {
        SetStates(service, pattern);
        for (uint32_t cell : {0u, 1u, 2u, 3u, 4u, 6u})
            MixByte(Virtual<uint32_t>(service, 0xE0, cell / 5, cell % 5));
        Mix(At<uint32_t>(service, 0x5C));
        Mix(At<uint32_t>(service, 0x60));
        MixStates(service);
    }

    // 53: the target let go, the stopwatch deleted, the active stopped.
    SetStates(service, 1);
    MixByte(Virtual<uint32_t>(service, 0xD4));
    Mix(g_svService.taken);
    MixService(service);
    MixStates(service);
    MixByte(Virtual<uint32_t>(service, 0xD4));
    EndService(service);
}

// What the play and the neighbour pick write in an instance (not its
// base part at +4, which holds the game's own counts).
void MixPlayed(char* instance)
{
    Mix(At<void*>(instance, 0x1C) ? 1u : 0u);
    for (std::size_t offset : {0x20, 0x28, 0x30, 0x38, 0x40, 0x44, 0x48, 0x4C, 0x50})
        Mix(At<uint32_t>(instance, offset));
    for (std::size_t offset : {0x2D, 0x3C, 0x3D, 0x58})
        Mix(At<uint8_t>(instance, offset));
    Mix(At<void*>(instance, 0x54) ? 1u : 0u);
}

// Slots 55, 58, 54 and 57 and the neighbour pick.
void DriveU7Pick()
{
    SvFakes fakes;
    char* const service = SeededService(FullText(5, 6, 0), 202);
    At<int32_t>(service, 0x28) = 2;
    At<int32_t>(service, 0x2C) = 2;
    At<int32_t>(service, 0x54) = 1;
    At<int32_t>(service, 0x58) = 1;
    char* const table = At<char*>(service, kSvTable);

    // 55: strays outside the window, then within it, then none.
    for (uint32_t pattern = 0; pattern < 5; ++pattern) {
        SetStates(service, pattern);
        for (int k = 0; k < 3; ++k) {
            int32_t row = -7, column = -7;
            const uint32_t eax = Virtual<uint32_t>(service, 0xDC, &row, &column);
            MixWord((eax & 0xFF) && eax > 0x10000 ? 0x1u : eax);
            Mix(static_cast<uint32_t>(row));
            Mix(static_cast<uint32_t>(column));
        }
    }
    At<int32_t>(service, 0x54) = 6;
    At<int32_t>(service, 0x58) = 6;
    for (uint32_t pattern : {0u, 3u}) {
        SetStates(service, pattern);
        int32_t row = 0, column = 0;
        const uint32_t eax = Virtual<uint32_t>(service, 0xDC, &row, &column);
        MixWord((eax & 0xFF) && eax > 0x10000 ? 0x1u : eax);
        Mix(static_cast<uint32_t>(row));
        Mix(static_cast<uint32_t>(column));
    }
    // Every cell idle: none.
    SetStates(service, 0);
    for (uint32_t i = 0; i < 30; ++i)
        At<uint32_t>(At<char*>(service, kSvInstances + ((i / 6) * 64 + i % 6) * 4), 0x20) = 0;
    {
        int32_t row = 0, column = 0;
        MixWord(Virtual<uint32_t>(service, 0xDC, &row, &column));
        Mix(static_cast<uint32_t>(row));
    }
    At<int32_t>(service, 0x54) = 1;
    At<int32_t>(service, 0x58) = 1;

    // 58: every cell, with and without a group, over the cells in the window
    // (huge: rand never under; 1: only weights 11 and up always; 0: 10).
    for (int32_t cells : {0x7FFFFFFF, 1}) {
        for (uint32_t pattern : {0u, 1u, 4u}) {
            SetStates(service, pattern);
            for (int32_t group : {0, 1, 3}) {
                for (int32_t r = 0; r < 5; ++r) {
                    for (int32_t c = 0; c < 6; ++c) {
                        At<int32_t>(service, 0x38) = cells;
                        char* const desc = At<char*>(table, 0x48 + (r * 64 + c) * 4);
                        const int32_t weight = At<int32_t>(desc, 0x40);
                        const int32_t score = Virtual<int32_t>(service, 0xE8, r, c, group);
                        // A chance between 1 and 32767 would depend on rand().
                        if (cells == 1 && weight >= 1 && weight <= 10 && score <= 1)
                            Mix(0xC4A7);
                        else
                            Mix(static_cast<uint32_t>(score));
                        Mix(static_cast<uint32_t>(At<int32_t>(desc, 0x40)));
                    }
                }
            }
        }
    }
    At<int32_t>(service, 0x38) = 0;
    At<uint8_t>(At<char*>(service, kSvInstances + (1 * 64 + 1) * 4), 0x58) = 1;
    MixWord(Virtual<uint32_t>(service, 0xE8, 1, 1, 0));
    Mix(At<uint32_t>(service, 0x38));
    At<int32_t>(service, 0x5C) = 2;
    At<int32_t>(service, 0x60) = 2;
    MixWord(Virtual<uint32_t>(service, 0xE8, 2, 2, 0));

    // 54: the best cell over states and groups (cells huge: deterministic).
    for (uint32_t pattern : {0u, 1u, 2u}) {
        SetStates(service, pattern);
        for (uint32_t group : {0u, 1u, 3u}) {
            At<int32_t>(service, 0x38) = 0x7FFFFFFF;
            int32_t row = -1, column = -1;
            MixWord(Virtual<uint32_t>(service, 0xD8, &row, &column, group) & 0xFF);
            Mix(static_cast<uint32_t>(row));
            Mix(static_cast<uint32_t>(column));
        }
    }

    // The neighbour grid's pick through slot 57 (idle cells are placed and
    // played; active ones go to slot 56): no cell set, one, all, the last
    // pick kept.
    g_svAudio.mode = 2;
    g_svSound.control = true;
    g_svSound.handle = false;
    for (uint32_t setting = 0; setting < 4; ++setting) {
        for (uint32_t r = 0; r < 3; ++r)
            for (uint32_t c = 0; c < 3; ++c)
                Virtual<uint32_t>(service, 0x80, r, c,
                                  setting == 0 ? 0u : setting == 1 ? (r == 1 && c == 2 ? 1u : 0u) : 1u);
        Virtual<uint32_t>(service, 0x78, setting == 3 ? 0u : 2u);
        Virtual<uint32_t>(service, 0x7C, setting == 3 ? 1u : 2u);
        for (uint32_t r = 0; r < 3; ++r)
            for (uint32_t c = 0; c < 3; ++c)
                Virtual<uint32_t>(service, 0x84, r, c, r * 10 + c, 100u + r, 200u + c);
        SetStates(service, setting);
        for (uint32_t cell : {0u, 7u, 8u, 13u, 29u}) {
            MixWord(Virtual<uint32_t>(service, 0xE4, cell / 6, cell % 6) & 0xFF);
            Mix(At<uint32_t>(service, 0x40AC));
            Mix(At<uint32_t>(service, 0x40B0));
            char* const instance = At<char*>(service, kSvInstances + ((cell / 6) * 64 + cell % 6) * 4);
            MixPlayed(instance);
            At<void*>(instance, 0x1C) = nullptr;
        }
        // By its address.
        char* const instance = At<char*>(service, kSvInstances + 3 * 4);
        MixWord(CallThis<uint32_t>(addr::cGZSoundService_PlaceInstance, service, static_cast<void*>(instance)) & 0xFF);
        MixPlayed(instance);
    }
    Mix(static_cast<uint32_t>(g_svSound.refs));
    g_svSound.refs = 1;
    EndService(service);
}

// A stopwatch of our own that the game's ElapsedMilliseconds reads: mode 1,
// not running, the time in its total.
struct SvWatch {
    uint32_t mode;
    uint32_t pad;
    uint32_t start[2];
    uint32_t total[2];
};

// The beats by the target's address, on a stopwatch of our own: early
// returns, beats passed and not, the bar's ends with retiring and playing
// (through the service's listener, so the plays go to it), the day's end,
// the 10 s clearing. The game's counts are set first and put back after.
void DriveU7Beat()
{
    SvFakes fakes;
    uint32_t saved[8];
    uint32_t* const stats = reinterpret_cast<uint32_t*>(addr::cGZSoundService_BeatStats);
    uint32_t& plays = *reinterpret_cast<uint32_t*>(addr::cGZSoundService_BeatPlays);
    std::memcpy(saved, stats, 7 * 4);
    saved[7] = plays;

    char* const service = SeededService(FullText(4, 6, 1), 303);
    SvWatch watch{1, 0, {0, 0}, {0, 0}};
    char* const part = service + 4;
    auto beat = [&](uint32_t time) {
        watch = SvWatch{1, 0, {0, 0}, {time, 0}};
        const uint32_t eax = CallThis<uint32_t>(addr::cGZSoundService_Beat, part, 0xBEEFu);
        MixWord(eax);
        for (uint32_t i = 0; i < 7; ++i)
            Mix(stats[i]);
        Mix(plays);
        for (std::size_t offset : {0x3Cu, 0x40u, 0x44u, 0x64u, 0x68u})
            Mix(At<uint32_t>(service, offset));
        MixStates(service);
    };
    for (uint32_t i = 0; i < 7; ++i)
        stats[i] = 0;
    plays = 0;
    // Not prepared? (prepared), not playing, paused.
    beat(0);
    At<uint8_t>(service, 0x20) = 1;
    At<void*>(service, 0x18) = &watch;
    At<uint32_t>(service, 0x24) = 120;
    At<uint32_t>(service, 0x44) = 0;
    At<uint32_t>(service, 0x3C) = 1;
    At<uint32_t>(service, 0x40) = 1;
    At<uint8_t>(service, 0x4090) = 1;
    beat(10000);
    At<uint8_t>(service, 0x4090) = 0;

    At<void*>(service, 0x4140) = &g_svListener;
    g_svListener.refs = 1;
    At<int32_t>(service, 0x28) = 2;
    At<int32_t>(service, 0x2C) = 1;
    CallThis<uint32_t>(addr::cGZSoundService_CountCells, service);
    At<int32_t>(service, 0x38) = 0x7FFFFFFF;
    for (uint32_t pattern : {2u, 3u, 0u}) {
        SetStates(service, pattern);
        for (uint32_t time : {0u, 400u, 499u, 500u, 2600u, 9000u}) {
            beat(time);
            At<int32_t>(service, 0x38) = 0x7FFFFFFF;
        }
        At<uint32_t>(service, 0x3C) = 1;
        At<uint32_t>(service, 0x40) = 1;
    }
    // The settings that stop letting go and playing.
    for (int k = 0; k < 4; ++k) {
        SetStates(service, 2);
        At<uint8_t>(service, 0x21) = k == 0;
        At<uint32_t>(service, 0x4088) = k == 1 ? 0u : 100u;
        At<uint8_t>(service, 0x4091) = k == 2;
        At<uint32_t>(At<char*>(service, kSvTable), 0xC) = k == 3 ? 0u : 1u;
        At<uint32_t>(service, 0x3C) = 3;
        At<uint32_t>(service, 0x40) = 1;
        beat(3000);
    }
    At<uint8_t>(service, 0x21) = 0;
    At<uint32_t>(service, 0x4088) = 100;
    At<uint8_t>(service, 0x4091) = 0;
    // A day passed: the beats start over and the stopwatch is restarted
    // (its time then is the clock's, not hashed).
    watch = SvWatch{1, 0, {0, 0}, {86400001u, 0}};
    MixWord(CallThis<uint32_t>(addr::cGZSoundService_Beat, part, 0u));
    for (std::size_t offset : {0x3Cu, 0x40u, 0x44u})
        Mix(At<uint32_t>(service, offset));
    // The counts cleared after 10 s.
    stats[5] = 1;
    stats[4] = 0;
    beat(20000);
    Mix(static_cast<uint32_t>(g_svListener.refs));

    EndService(service);
    std::memcpy(stats, saved, 7 * 4);
    plays = saved[7];
    g_svListener.refs = 1;
}

// A file of our own in the temporary folder: slots 38 and 39 load it (and
// one that is not there), the reader by its address, slot 40 and 46.
void DriveU7File()
{
    SvFakes fakes;
    const char* const temp = std::getenv("TEMP");
    std::string folder = temp ? temp : ".";
    folder += "\\";
    char file[64];
    std::snprintf(file, sizeof file, "opensc4-gzu7-%d.txt", _getpid());
    const std::string path = folder + file;
    const std::string text = FullText(2, 3, 0);
    if (FILE* const out = std::fopen(path.c_str(), "wb")) {
        std::fwrite(text.data(), 1, text.size(), out);
        std::fclose(out);
    }
    auto settle = [&](char* service) {
        // The name is the folder (machine dependent): only whether it is.
        char* const name = service + 0x4074;
        const std::size_t length = static_cast<std::size_t>(At<char*>(name, 8) - At<char*>(name, 4));
        Mix(length == folder.size() || length == folder.size() - 1 ? 1u : 0u);
        GameText fixed("fixed");
        CallThis<void*>(addr::cRZBaseString_Assign, name, fixed.get());
        MixService(service);
        if (char* const table = At<char*>(service, kSvTable))
            MixCells(table);
    };
    {
        char* const service = NewService();
        GameText whole(path.c_str());
        MixByte(Virtual<uint32_t>(service, 0x98, whole.get()));
        settle(service);
        EndService(service);
    }
    for (const char* name : {static_cast<const char*>(file), "opensc4-gzu7-none.txt"}) {
        char* const service = NewService();
        GameText where(folder.c_str());
        GameText what(name);
        MixByte(Virtual<uint32_t>(service, 0x9C, where.get(), what.get()));
        settle(service);
        EndService(service);
    }
    // The reader by its address (the path by value; it destroys it).
    struct ByValue {
        alignas(4) char bytes[0x14];
    };
    for (const std::string& name : {path, folder + "opensc4-gzu7-none.txt"}) {
        char* const table = NewTable();
        ByValue copy;
        CallThis<void*>(addr::SC4String_FromText, copy.bytes, name.c_str());
        MixByte(reinterpret_cast<uint32_t(__thiscall*)(void*, ByValue)>(addr::cGZSoundTable_ReadPath)(table, copy));
        MixCells(table);
        Virtual<uint32_t>(table, 0x10, 1u);
    }
    // 40 and 46.
    {
        char* const service = NewService();
        char* const table = NewTable();
        GameText input(text.c_str());
        CallThis<uint32_t>(addr::cGZSoundTable_Load, table, input.get());
        GameText name("by table");
        MixWord(Virtual<uint32_t>(service, 0xA0, static_cast<void*>(table), name.get()) & 0xFF);
        MixService(service);
        GameText out("");
        MixWord(Virtual<uint32_t>(service, 0xB8, out.get()) & 0xFF);
        MixString(out.get());
        EndService(service);
    }
    std::remove(path.c_str());
}

// The instance's play (slot 5) and its listener path by its address: hex
// names (a number, and 0), plain names, the audio system refusing, giving no
// buffer or one (with and without its 0xFD584467 and 0x5B34F0AF), busy
// instances, random levels, looping, pitch ranges, place and time.
void DriveU7Instance()
{
    SvFakes fakes;
    GsService service;
    InitService(service, 9090, 0x300, 0x380);
    StackDesc desc;
    char* const instance = NewInstance(service);
    At<char*>(instance, 0x24) = desc.bytes;
    struct Case {
        const char* name;
        uint8_t random, looping;
        int32_t low, high, time;
    };
    const Case cases[] = {
        {"a1b2c3d4.wav", 0, 0, 0, 0, 0},     {"00000000.wav", 1, 1, -3, 2, 7},  {"bird.wav", 0, 1, 2, 2, 0},
        {"x/1234abcd", 1, 0, 5, 1, 250},     {"Wind", 0, 0, -12, -1, 0},       {"deadBEEF.xml.wav", 1, 1, 0, 12, 3},
    };
    auto setUp = [&](const Case& c) {
        GameText name(c.name);
        CallThis<void*>(addr::cRZBaseString_Assign, desc.bytes + 0xC, name.get());
        At<uint8_t>(desc.bytes, 0x30) = c.random;
        At<uint8_t>(desc.bytes, 0x50) = c.looping;
        At<int32_t>(desc.bytes, 0x54) = c.low;
        At<int32_t>(desc.bytes, 0x58) = c.high;
        At<int32_t>(desc.bytes, 0x34) = c.time;
        At<int32_t>(desc.bytes, 0x38) = c.time * 3;
        At<int32_t>(desc.bytes, 0x48) = 4;
        At<uint32_t>(desc.bytes, 0x8) = 0;
        At<uint32_t>(instance, 0x20) = 0;
        At<uint8_t>(instance, 0x58) = 0;
        At<uint8_t>(instance, 0x3C) = c.time != 0;
        At<uint32_t>(instance, 0x40) = 0x10 + c.time;
        At<uint32_t>(instance, 0x44) = 0xFFFFFFF0u;
        At<uint32_t>(instance, 0x48) = 3;
        At<uint32_t>(instance, 0x4C) = 0x80000010u;
        At<uint32_t>(instance, 0x50) = 77;
        At<void*>(instance, 0x1C) = nullptr;
        At<void*>(instance, 0x54) = nullptr;
    };
    auto after = [&](uint32_t eax) {
        MixWord(eax & 0xFF);
        MixPlayed(instance);
        Mix(At<uint32_t>(desc.bytes, 0x8));
        Mix(static_cast<uint32_t>(g_svSound.refs));
        Mix(g_svService.taken);
    };
    for (const Case& c : cases) {
        for (uint32_t mode = 0; mode < 3; ++mode) {
            for (int extras = 0; extras < 2; ++extras) {
                setUp(c);
                g_svAudio.mode = mode;
                g_svSound = SvSound{SvSoundVtable(), 1, extras == 1, extras == 1, 1};
                after(Virtual<uint32_t>(instance, 0x14));
            }
        }
        // Through the listener, accepting and not; then by its address.
        for (uint32_t accept : {1u, 0u}) {
            setUp(c);
            g_svListener = SvListener{SvListenerVtable(), 1, accept};
            At<void*>(instance, 0x54) = &g_svListener;
            after(Virtual<uint32_t>(instance, 0x14));
            setUp(c);
            At<void*>(instance, 0x54) = &g_svListener;
            after(CallThis<uint32_t>(addr::cGZSoundInstance_PlayThrough, instance, static_cast<void*>(desc.bytes)));
        }
    }
    // Busy: slot 12's byte, then a state.
    setUp(cases[0]);
    At<uint8_t>(instance, 0x58) = 1;
    after(Virtual<uint32_t>(instance, 0x14));
    setUp(cases[0]);
    At<uint32_t>(instance, 0x20) = 3;
    after(Virtual<uint32_t>(instance, 0x14));
    setUp(cases[0]);
    g_svSound = SvSound{SvSoundVtable(), 1, true, false, 1};
    g_svListener = SvListener{SvListenerVtable(), 1, 1};
    Mix(Virtual<uint32_t>(instance + 4, 0x8));
}

// OPENSC4_TEST_GZSOUNDSVC=<day>: part u6's functions through their slots
// and addresses: the loader over texts and a file of our own, Init with a
// score the game has, Play and the fade, the window, the settings, the
// objects; then part u7's: slots 41 to 64, the cell picks, the beats, the
// score files, the instance's play. Every result goes into the
// fingerprint's `gzv` part; addresses never do.
void DriveGzSoundSvc(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_GZSOUNDSVC") || !game::IsBuild641())
        return;
    g_svOn = true;
    uint32_t* const saved = g_into;
    g_into = &g_svHash;
    // Ours stand in for the game's sound service and audio system all along:
    // a service played by the real one would get its beats from the audio
    // layer while the test changes it (an unmuted game died in "play").
    SvFakes fakes;
    DriveLoad();
    SvStage("load");
    char* const scored = DriveInit();
    SvStage("init");
    DrivePlay(scored);
    SvStage("play");
    DrivePlay(PreparedService(WideText()));
    SvStage("play2");
    DriveWindow();
    SvStage("window");
    DriveSettings();
    SvStage("settings");
    DriveSvObjects();
    SvStage("objects");
    DriveU7Slots();
    SvStage("slots");
    DriveU7Pick();
    SvStage("pick");
    DriveU7Beat();
    SvStage("beat");
    DriveU7File();
    SvStage("file");
    DriveU7Instance();
    SvStage("instance");
    g_into = saved;
    log::Write("autotest: gzsoundsvc driven (hash %08X)", g_svHash);
}

void SvFingerprint(cISC4City*, std::string& line)
{
    if (!g_svOn)
        return;
    char text[32];
    std::snprintf(text, sizeof text, " | gzv %08X", g_svHash);
    line += text;
}

const FingerprintPart kSvFingerprint{"gzsoundsvc", 4, &SvFingerprint};
const DayHook kSvDrive{"gzsoundsvc", 4, &DriveGzSoundSvc};

// ====================================================================================
// Part u8: the framework's sound mixer 0xBDDA72DD (OPENSC4_TEST_GZSOUNDMIX).
// The framework's sound service the game caches (0x00B628C8) is ours while
// it runs, and so are the mixer's scores, instances and descriptions but
// for the stage that adds real scores: every call goes into the hash.

uint32_t Bits(const void* pointer) { return static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(pointer)); }

bool g_mxOn = false;
uint32_t g_mxHash = 2166136261u;

void MxStage(const char* name) { log::Write("autotest: gzsoundmix: %s %08X", name, g_mxHash); }

// A description: its slot 14 the weight.
struct MxDesc {
    void* const* vtable;
    uint32_t id;
    uint32_t weight;
};
uint32_t __fastcall MxDescWeight(MxDesc* self, void*)
{
    Mix(0x9038);
    Mix(self->id);
    return self->weight;
}

void* const* MxDescVtable()
{
    static void* table[15];
    if (!table[0]) {
        for (void*& slot : table)
            slot = reinterpret_cast<void*>(&GsTrap);
        table[14] = reinterpret_cast<void*>(&MxDescWeight);
    }
    return table;
}

// An instance: slots 3 (its description), 5 (play), 6 (update), 7, 9 (a
// level), 10 (whether it plays), 15, 16 and 17 (the place).
struct MxInstance {
    void* const* vtable;
    uint32_t id;
    MxDesc* desc;
    uint32_t playing;
};
uint32_t __fastcall MxInstanceDesc(MxInstance* self, void*)
{
    Mix(0x900C);
    Mix(self->id);
    return Bits(self->desc);
}
uint32_t __fastcall MxInstancePlay(MxInstance* self, void*)
{
    Mix(0x9014);
    Mix(self->id);
    return 0x1400 | self->id;
}
uint32_t __fastcall MxInstanceUpdate(MxInstance* self, void*)
{
    Mix(0x9018);
    Mix(self->id);
    return 0x1800 | self->id;
}
uint32_t __fastcall MxInstanceStart(MxInstance* self, void*)
{
    Mix(0x901C);
    Mix(self->id);
    return 0x1C00;
}
uint32_t __fastcall MxInstanceLevel(MxInstance* self, void*, uint32_t level)
{
    Mix(0x9024);
    Mix(self->id);
    Mix(level);
    return 0x2400 | self->id;
}
uint32_t __fastcall MxInstancePlaying(MxInstance* self, void*)
{
    Mix(0x9028);
    Mix(self->id);
    return 0x2800 | self->playing;
}
uint32_t __fastcall MxInstanceByte(MxInstance* self, void*, uint32_t value)
{
    Mix(0x903C);
    Mix(self->id);
    Mix(value);
    return 0x3C00;
}
uint32_t __fastcall MxInstanceGrid(MxInstance* self, void*, uint32_t a, uint32_t b, uint32_t c)
{
    Mix(0x9040);
    Mix(self->id);
    Mix(a);
    Mix(b);
    Mix(c);
    return 0x4000 | self->id;
}
uint32_t __fastcall MxInstanceDistances(MxInstance* self, void*, uint32_t a, uint32_t b)
{
    Mix(0x9044);
    Mix(self->id);
    Mix(a);
    Mix(b);
    return 0x4400;
}

void* const* MxInstanceVtable()
{
    static void* table[18];
    if (!table[0]) {
        for (void*& slot : table)
            slot = reinterpret_cast<void*>(&GsTrap);
        table[3] = reinterpret_cast<void*>(&MxInstanceDesc);
        table[5] = reinterpret_cast<void*>(&MxInstancePlay);
        table[6] = reinterpret_cast<void*>(&MxInstanceUpdate);
        table[7] = reinterpret_cast<void*>(&MxInstanceStart);
        table[9] = reinterpret_cast<void*>(&MxInstanceLevel);
        table[10] = reinterpret_cast<void*>(&MxInstancePlaying);
        table[15] = reinterpret_cast<void*>(&MxInstanceByte);
        table[16] = reinterpret_cast<void*>(&MxInstanceGrid);
        table[17] = reinterpret_cast<void*>(&MxInstanceDistances);
    }
    return table;
}

// A score (the sound service's interface): references, the table (6), the
// settings (11, 13 to 15), the window's edges (22 to 25), the instance at a
// cell (26; a 4 by 4 grid of ours, null outside), Reseed (27).
struct MxScore {
    void* const* vtable;
    int32_t refs;
    uint32_t id;
    int32_t edges[4];
    MxInstance* cells[4][4];
};
uint32_t __fastcall MxScoreAddRef(MxScore* self, void*)
{
    Mix(0x9104);
    return static_cast<uint32_t>(++self->refs);
}
uint32_t __fastcall MxScoreRelease(MxScore* self, void*)
{
    Mix(0x9108);
    return static_cast<uint32_t>(--self->refs);
}
uint32_t __fastcall MxScoreTable(MxScore* self, void*, uint32_t value)
{
    Mix(0x9118);
    Mix(self->id);
    Mix(value);
    return 0x1800 | self->id;
}
uint32_t __fastcall MxScoreSetting(MxScore* self, uint32_t marker, uint32_t value)
{
    Mix(marker);
    Mix(self->id);
    Mix(value);
    return marker << 8;
}
uint32_t __fastcall MxScoreLevel(MxScore* self, void*, uint32_t value) { return MxScoreSetting(self, 0x912C, value); }
uint32_t __fastcall MxScoreRow(MxScore* self, void*, uint32_t value) { return MxScoreSetting(self, 0x9134, value); }
uint32_t __fastcall MxScoreColumn(MxScore* self, void*, uint32_t value) { return MxScoreSetting(self, 0x9138, value); }
uint32_t __fastcall MxScore3C(MxScore* self, void*, uint32_t value) { return MxScoreSetting(self, 0x913C, value); }
uint32_t __fastcall MxScoreReseed(MxScore* self, void*, uint32_t value) { return MxScoreSetting(self, 0x916C, value); }
int32_t MxScoreEdge(MxScore* self, uint32_t which)
{
    Mix(0x9158 + which * 4);
    Mix(self->id);
    return self->edges[which];
}
int32_t __fastcall MxScoreEdge0(MxScore* self, void*) { return MxScoreEdge(self, 0); }
int32_t __fastcall MxScoreEdge1(MxScore* self, void*) { return MxScoreEdge(self, 1); }
int32_t __fastcall MxScoreEdge2(MxScore* self, void*) { return MxScoreEdge(self, 2); }
int32_t __fastcall MxScoreEdge3(MxScore* self, void*) { return MxScoreEdge(self, 3); }
uint32_t __fastcall MxScoreInstance(MxScore* self, void*, int32_t row, int32_t column, uint32_t third)
{
    Mix(0x9168);
    Mix(self->id);
    Mix(static_cast<uint32_t>(row));
    Mix(static_cast<uint32_t>(column));
    Mix(third);
    if (row < 0 || row > 3 || column < 0 || column > 3)
        return 0;
    return Bits(self->cells[row][column]);
}

void* const* MxScoreVtable()
{
    static void* table[28];
    if (!table[0]) {
        for (void*& slot : table)
            slot = reinterpret_cast<void*>(&GsTrap);
        table[1] = reinterpret_cast<void*>(&MxScoreAddRef);
        table[2] = reinterpret_cast<void*>(&MxScoreRelease);
        table[6] = reinterpret_cast<void*>(&MxScoreTable);
        table[11] = reinterpret_cast<void*>(&MxScoreLevel);
        table[13] = reinterpret_cast<void*>(&MxScoreRow);
        table[14] = reinterpret_cast<void*>(&MxScoreColumn);
        table[15] = reinterpret_cast<void*>(&MxScore3C);
        table[22] = reinterpret_cast<void*>(&MxScoreEdge0);
        table[23] = reinterpret_cast<void*>(&MxScoreEdge1);
        table[24] = reinterpret_cast<void*>(&MxScoreEdge2);
        table[25] = reinterpret_cast<void*>(&MxScoreEdge3);
        table[26] = reinterpret_cast<void*>(&MxScoreInstance);
        table[27] = reinterpret_cast<void*>(&MxScoreReseed);
    }
    return table;
}

// The framework's sound service: references, slots 30 (take a target), 31
// (let it go) and 49 (whether everything waits).
struct MxSound {
    void* const* vtable;
    int32_t refs;
    uint32_t waiting;
    void* target;
};
uint32_t __fastcall MxSoundAddRef(MxSound* self, void*)
{
    Mix(0x9204);
    return static_cast<uint32_t>(++self->refs);
}
uint32_t __fastcall MxSoundRelease(MxSound* self, void*)
{
    Mix(0x9208);
    return static_cast<uint32_t>(--self->refs);
}
uint32_t __fastcall MxSoundTake(MxSound* self, void*, void* target, uint32_t period)
{
    Mix(0x9278);
    Mix(period);
    self->target = target;
    return 0x7800;
}
uint32_t __fastcall MxSoundDrop(MxSound* self, void*, void* target)
{
    Mix(0x927C);
    Mix(target == self->target ? 1u : 0u);
    self->target = nullptr;
    return 0x7C00;
}
uint32_t __fastcall MxSoundWaiting(MxSound* self, void*)
{
    Mix(0x92C4);
    return 0xC400 | self->waiting;
}

void* const* MxSoundVtable()
{
    static void* table[50];
    if (!table[0]) {
        for (void*& slot : table)
            slot = reinterpret_cast<void*>(&GsTrap);
        table[1] = reinterpret_cast<void*>(&MxSoundAddRef);
        table[2] = reinterpret_cast<void*>(&MxSoundRelease);
        table[30] = reinterpret_cast<void*>(&MxSoundTake);
        table[31] = reinterpret_cast<void*>(&MxSoundDrop);
        table[49] = reinterpret_cast<void*>(&MxSoundWaiting);
    }
    return table;
}

MxSound g_mxSound{MxSoundVtable(), 1, 0, nullptr};

// The game's cached sound service and audio system swapped for ours.
struct MxFakes {
    void* sound;
    void* audio;
    MxFakes()
    {
        sound = At<void*>(reinterpret_cast<void*>(addr::SC4_SoundServiceCache), 0);
        audio = At<void*>(reinterpret_cast<void*>(addr::SC4_AudioSystemCache), 0);
        At<void*>(reinterpret_cast<void*>(addr::SC4_SoundServiceCache), 0) = &g_mxSound;
        At<void*>(reinterpret_cast<void*>(addr::SC4_AudioSystemCache), 0) = &g_svAudio;
    }
    ~MxFakes()
    {
        At<void*>(reinterpret_cast<void*>(addr::SC4_SoundServiceCache), 0) = sound;
        At<void*>(reinterpret_cast<void*>(addr::SC4_AudioSystemCache), 0) = audio;
    }
    MxFakes(const MxFakes&) = delete;
    MxFakes& operator=(const MxFakes&) = delete;
};

// Ours by its id; a game object as 0xFEED; none as 0.
uint32_t MxWho(void* object)
{
    if (!object)
        return 0;
    if (*static_cast<void* const* const*>(object) == MxInstanceVtable())
        return static_cast<MxInstance*>(object)->id;
    if (*static_cast<void* const* const*>(object) == MxDescVtable())
        return static_cast<MxDesc*>(object)->id;
    return 0xFEED;
}

char* TreeNext(char* node) { return reinterpret_cast<char*(__cdecl*)(char*)>(addr::SC4Tree_Next)(node); }

// A map's keys and values (as bytes or by who they are).
void MixMap(char* mixer, std::size_t offset, bool bytes)
{
    char* const header = At<char*>(mixer, offset);
    Mix(At<uint32_t>(mixer, offset + 4));
    for (char* node = At<char*>(header, 8); node != header; node = TreeNext(node)) {
        Mix(At<uint32_t>(node, 0x10));
        Mix(bytes ? At<uint8_t>(node, 0x14) : MxWho(At<void*>(node, 0x14)));
    }
}

void MixSounding(char* mixer)
{
    char* const header = At<char*>(mixer, 0x54);
    uint32_t count = 0;
    for (char* node = At<char*>(header, 0); node != header; node = At<char*>(node, 0)) {
        Mix(MxWho(At<void*>(node, 8)));
        ++count;
    }
    Mix(count);
}

// The mixer's fields but the stopwatch and its times (+0xE8 to +0x107), the
// generator and what the constructor leaves unset.
void MixMixer(char* mixer)
{
    Mix(At<uint8_t>(mixer, 0xC));
    Mix(At<uint32_t>(mixer, 0x10));
    void* const sound = At<void*>(mixer, 0x14);
    Mix(sound == &g_mxSound ? 1u : sound ? 2u : 0u);
    Mix(At<uint32_t>(mixer, 0x18 + 4));
    Mix(At<uint32_t>(mixer, 0x24 + 4));
    MixMap(mixer, 0x30, true);
    MixMap(mixer, 0x3C, false);
    MixMap(mixer, 0x48, false);
    MixSounding(mixer);
    for (std::size_t offset = 0x58; offset < 0x60; offset += 4)
        Mix(At<uint32_t>(mixer, offset));
    for (std::size_t offset = 0x60; offset < 0x6A; ++offset)
        Mix(At<uint8_t>(mixer, offset));
    for (std::size_t offset = 0x6C; offset < 0xE8; offset += 4)
        Mix(At<uint32_t>(mixer, offset));
    for (std::size_t offset = 0x108; offset < 0x114; offset += 4)
        Mix(At<uint32_t>(mixer, offset));
    Mix(At<uint32_t>(mixer, 0x130));
    Mix(At<uint8_t>(mixer, 0x134));
    Mix(At<uint8_t>(mixer, 0x135));
    for (std::size_t offset = 0x138; offset < 0x16C; offset += 4)
        Mix(At<uint32_t>(mixer, offset));
    Mix(At<uint8_t>(mixer, 0x16C));
    Mix(At<uint8_t>(mixer, 0x16D));
    for (std::size_t offset = 0x170; offset < 0x17C; offset += 4)
        Mix(At<uint32_t>(mixer, offset));
    Mix(At<uint8_t>(mixer, 0x17C));
    for (std::size_t offset = 0x180; offset < 0x18C; offset += 4)
        Mix(At<uint32_t>(mixer, offset));
}

char* NewMixer()
{
    char* const mixer = static_cast<char*>(New(0x190));
    MixIs(CallThis<char*>(addr::cGZSoundMixer_Construct, mixer), mixer);
    return mixer;
}

// Counted down to its deletion (the maps release the scores they hold).
void EndMixer(char* mixer)
{
    At<uint32_t>(mixer, 0x10) = 1;
    Mix(Virtual<uint32_t>(mixer, 0x8));
}

// A score of ours kept under the key (as slot 5 keeps one).
void Keep(char* mixer, uint32_t key, MxScore* score)
{
    void* const kept = CallThis<void*>(addr::cGZSoundMixer_ScoresIndex, mixer + 0x18, &key);
    if (score) {
        void* held = score;
        CallThis<void*>(addr::SC4RefPtr_AssignFrom, kept, &held);
    }
}

void Sound(char* mixer, MxInstance* instance)
{
    void* held = instance;
    CallThis<void>(addr::SC4PtrList_PushBack, mixer + 0x54, &held);
}

// The stopwatch as ours: mode 1, stopped, the time in its total.
void SetWatch(char* mixer, uint32_t time)
{
    At<uint32_t>(mixer, 0xE8) = 1;
    At<uint32_t>(mixer, 0xEC) = 0;
    At<uint32_t>(mixer, 0xF0) = 0;
    At<uint32_t>(mixer, 0xF4) = 0;
    At<uint32_t>(mixer, 0xF8) = time;
    At<uint32_t>(mixer, 0xFC) = 0;
}

// The fakes every stage but the last uses: 24 descriptions, 24 instances, 4
// scores over them.
struct MxWorld {
    MxDesc descs[24];
    MxInstance instances[24];
    MxScore scores[4];
    MxWorld()
    {
        const uint32_t weights[] = {5, 0, 20, 1, 7, 100, 3, 0, 12, 9, 40, 2};
        for (uint32_t i = 0; i < 24; ++i) {
            descs[i] = MxDesc{MxDescVtable(), 0x300 + i, weights[i % 12]};
            instances[i] = MxInstance{MxInstanceVtable(), 0x100 + i, i % 7 == 6 ? nullptr : &descs[i], i % 3 != 0 ? 1u : 0u};
        }
        const int32_t edges[4][4] = {{0, 1, 0, 2}, {1, 3, 1, 1}, {2, 1, 0, 3}, {-1, 0, 3, 4}};
        for (uint32_t s = 0; s < 4; ++s) {
            scores[s] = MxScore{MxScoreVtable(), 1, 0x200 + s, {edges[s][0], edges[s][1], edges[s][2], edges[s][3]}, {}};
            for (uint32_t r = 0; r < 4; ++r)
                for (uint32_t c = 0; c < 4; ++c)
                    scores[s].cells[r][c] = (r * 4 + c + s) % 5 == 4 ? nullptr : &instances[(s * 6 + r * 4 + c) % 24];
        }
    }
};

// The constructor, the interfaces, the counts, the destructors.
void DriveMixObjects()
{
    char* const mixer = NewMixer();
    Mix(At<std::uintptr_t>(mixer, 0) == addr::cGZSoundMixer_Vtable ? 1u : 0u);
    Mix(At<std::uintptr_t>(mixer, 4) == addr::cGZSoundMixer_ServiceVtable ? 1u : 0u);
    Mix(At<std::uintptr_t>(mixer, 8) == addr::cGZSoundMixer_TargetVtable ? 1u : 0u);
    MixMixer(mixer);
    At<uint32_t>(mixer, 0x10) = 10;
    for (std::size_t part : {0u, 4u, 8u}) {
        for (uint32_t iid : {1u, 0xDDDA72F1u, 0xDC1CF595u, 0x12345678u}) {
            void* out = nullptr;
            const uint32_t eax = Virtual<uint32_t>(mixer + part, 0x0, iid, &out);
            MixByte(eax);
            Mix(out == mixer ? 1u : out == mixer + 4 ? 2u : out ? 3u : 0u);
        }
        Mix(At<uint32_t>(mixer, 0x10));
        // Down through the part's Release (not to the end).
        for (int k = 0; k < 2; ++k)
            Mix(Virtual<uint32_t>(mixer + part, 0x8));
        Mix(At<uint32_t>(mixer, 0x10));
    }
    // By address; then a count below 0 deletes it.
    {
        void* out = reinterpret_cast<void*>(1);
        At<uint32_t>(mixer, 0x10) = 5;
        MixByte(CallThis<uint32_t>(addr::cGZSoundMixer_QueryInterface, mixer, 0xDC1CF595u, &out));
        Mix(out == mixer + 4 ? 1u : 0u);
        Mix(CallThis<uint32_t>(addr::cGZSoundMixer_Release, mixer));
        At<int32_t>(mixer, 0x10) = -3;
        Mix(CallThis<uint32_t>(addr::cGZSoundMixer_Release, mixer) == 0 ? 1u : 2u);
    }
    // Counted down with a sound service held: the deleting destructor.
    char* const second = NewMixer();
    MixWord(Virtual<uint32_t>(second + 4, 0x10));
    Mix(static_cast<uint32_t>(g_mxSound.refs));
    EndMixer(second);
    Mix(static_cast<uint32_t>(g_mxSound.refs));
    // The destructor and the deleting destructor by address, flag 0.
    char* const third = NewMixer();
    MixWord(Virtual<uint32_t>(third + 4, 0x10));
    MixWord(CallThis<uint32_t>(addr::cGZSoundMixer_Destruct, third));
    Mix(static_cast<uint32_t>(g_mxSound.refs));
    reinterpret_cast<uint32_t(__cdecl*)(void*)>(addr::SC4_operator_delete)(third);
    char* const fourth = NewMixer();
    MixIs(CallThis<char*>(addr::cGZSoundMixer_DeletingDestructor, fourth, 0u), fourth);
    reinterpret_cast<uint32_t(__cdecl*)(void*)>(addr::SC4_operator_delete)(fourth);
}

// Slots 28 to 35 over edge values and the neighbour pick by address.
void DriveMixGrid()
{
    MxWorld world;
    char* const mixer = NewMixer();
    char* const part = mixer + 4;
    MixByte(Virtual<uint32_t>(part, 0x70, 0x1FFu));
    MixWord(Virtual<uint32_t>(part, 0x74, 0x12345u, 0xFFFFFFF0u));
    for (uint32_t v : {0u, 1u, 2u, 3u, 0x80000000u}) {
        MixWord(Virtual<uint32_t>(part, 0x78, v));
        MixWord(Virtual<uint32_t>(part, 0x7C, v));
    }
    for (uint32_t r : {0u, 1u, 2u, 3u, 0xFFFFFFFFu})
        for (uint32_t c : {0u, 2u, 3u})
            MixWord(Virtual<uint32_t>(part, 0x80, r, c, 0x100u + r * 3 + c));
    for (uint32_t r : {0u, 2u, 3u})
        for (uint32_t c : {0u, 1u, 2u, 5u})
            MixWord(Virtual<uint32_t>(part, 0x84, r, c, r * 10 + c, 100u + r, 200u + c));
    const uint32_t key[3] = {0x11, 0x22, 0x33};
    Mix(Virtual<uint32_t>(part, 0x88, key) == Bits(mixer + 0x180) ? 1u : 0u);
    Mix(Virtual<uint32_t>(part, 0x8C) == Bits(mixer + 0x180) ? 1u : 0u);
    MixMixer(mixer);

    // The pick: no cell set, one, all, the last pick kept; sizes 0 to 2.
    CallThis<void>(addr::cRZRandom_Seed, mixer + 0x118, 4242u);
    MxInstance& instance = world.instances[0];
    for (uint32_t setting = 0; setting < 5; ++setting) {
        for (uint32_t r = 0; r < 3; ++r)
            for (uint32_t c = 0; c < 3; ++c)
                Virtual<uint32_t>(part, 0x80, r, c,
                                  setting == 0 ? 0u : setting == 1 ? (r == 1 && c == 2 ? 1u : 0u) : 1u);
        Virtual<uint32_t>(part, 0x78, setting == 3 ? 0u : setting == 4 ? 1u : 2u);
        Virtual<uint32_t>(part, 0x7C, setting == 3 ? 1u : 2u);
        for (int k = 0; k < 4; ++k) {
            MixWord(CallThis<uint32_t>(addr::cGZSoundMixer_PlaceInstance, mixer, static_cast<void*>(&instance)));
            Mix(At<uint32_t>(mixer, 0xE0));
            Mix(At<uint32_t>(mixer, 0xE4));
        }
    }
    EndMixer(mixer);
}

// The settings every score gets, the level the sounding get, Reseed and
// the plain slots.
void DriveMixControl()
{
    MxWorld world;
    char* const mixer = NewMixer();
    char* const part = mixer + 4;
    MixWord(Virtual<uint32_t>(part, 0x10));
    // With no score and no sounding instance first.
    MixWord(Virtual<uint32_t>(part, 0x2C, 0x200u));
    MixWord(Virtual<uint32_t>(part, 0x34, 5u));
    MixWord(Virtual<uint32_t>(part, 0x6C, 77u));
    for (uint32_t s = 0; s < 3; ++s)
        Keep(mixer, 0x40 + s, &world.scores[s]);
    Keep(mixer, 0, &world.scores[3]);
    Sound(mixer, &world.instances[3]);
    Sound(mixer, &world.instances[8]);
    MixWord(Virtual<uint32_t>(part, 0x2C, 0x380u));
    MixByte(Virtual<uint32_t>(part, 0x30, 1u, 2u, 0x1ABu));
    MixWord(Virtual<uint32_t>(part, 0x34, 0x30u));
    MixWord(Virtual<uint32_t>(part, 0x38, 0x31u));
    MixWord(Virtual<uint32_t>(part, 0x3C, 0x32u));
    MixWord(Virtual<uint32_t>(part, 0x40, 0x33u));
    Mix(Virtual<uint32_t>(part, 0x50));
    MixByte(Virtual<uint32_t>(part, 0x54, 0x1FEu));
    Mix(Virtual<uint32_t>(part, 0x68, 1u, 2u, 3u));
    MixWord(Virtual<uint32_t>(part, 0x6C, 9999u));
    MixWord(Virtual<uint32_t>(mixer, 0x18, 0xCAFEu));
    Mix(Virtual<uint32_t>(mixer, 0x1C));
    MixByte(Virtual<uint32_t>(mixer, 0x20, 0x2C3u));
    MixMixer(mixer);
    // While the sound service waits, the pool is left as it is.
    g_mxSound.waiting = 1;
    MixWord(Virtual<uint32_t>(part, 0x40, 0x34u));
    MixWord(Virtual<uint32_t>(part, 0x34, 0x35u));
    g_mxSound.waiting = 0;
    MixMixer(mixer);
    EndMixer(mixer);
    for (const MxScore& score : world.scores)
        Mix(static_cast<uint32_t>(score.refs));
}

// Scores turned on and off (slot 4) and the pool by address.
void DriveMixPool()
{
    MxWorld world;
    char* const mixer = NewMixer();
    char* const part = mixer + 4;
    MixWord(Virtual<uint32_t>(part, 0x10));
    for (uint32_t s = 0; s < 4; ++s)
        Keep(mixer, 0x50 + s, &world.scores[s]);
    Keep(mixer, 0, &world.scores[0]);
    Keep(mixer, 0x58, nullptr);
    const uint32_t steps[][2] = {{0x50, 1}, {0x51, 1}, {0x58, 1}, {0, 1},    {0x99, 1},  {0x52, 0x100},
                                 {0x53, 1}, {0x51, 0}, {0x58, 0}, {0x99, 0}, {0x50, 2},  {0x53, 0}};
    for (const auto& step : steps) {
        MixWord(Virtual<uint32_t>(mixer, 0x10, step[0], step[1]));
        MixMixer(mixer);
    }
    // By address, with an empty window and descriptions missing.
    world.scores[2].edges[1] = -1;
    world.descs[0].weight = 0x10000;
    MixWord(CallThis<uint32_t>(addr::cGZSoundMixer_Rebuild, mixer));
    MixMixer(mixer);
    g_mxSound.waiting = 1;
    MixWord(CallThis<uint32_t>(addr::cGZSoundMixer_Rebuild, mixer));
    MixWord(Virtual<uint32_t>(mixer, 0x10, 0x51u, 1u));
    MixMixer(mixer);
    g_mxSound.waiting = 0;
    // Without the sound service.
    {
        char* const bare = NewMixer();
        Keep(bare, 0x60, &world.scores[1]);
        MixWord(Virtual<uint32_t>(bare, 0x10, 0x60u, 1u));
        MixMixer(bare);
        MixWord(Virtual<uint32_t>(bare, 0x10, 0x60u, 0u));
        EndMixer(bare);
    }
    EndMixer(mixer);
    for (const MxScore& score : world.scores)
        Mix(static_cast<uint32_t>(score.refs));
}

// The tick by the target's address on a stopwatch of ours.
void DriveMixTick()
{
    MxWorld world;
    char* const mixer = NewMixer();
    char* const part = mixer + 4;
    char* const target = mixer + 8;
    MixWord(Virtual<uint32_t>(part, 0x10));
    for (uint32_t s = 0; s < 3; ++s)
        Keep(mixer, 0x70 + s, &world.scores[s]);
    for (uint32_t s = 0; s < 3; ++s)
        Virtual<uint32_t>(mixer, 0x10, 0x70 + s, 1u);
    Virtual<uint32_t>(part, 0x6C, 31337u);
    MixMixer(mixer);
    auto tick = [&](uint32_t time, uint32_t last) {
        SetWatch(mixer, time);
        At<uint32_t>(mixer, 0x104) = last;
        // With none sounding the tick answers the list's header: not hashed.
        const uint32_t eax = CallThis<uint32_t>(addr::cGZSoundMixer_Tick, target, 0xBEEFu);
        MixWord(eax == Bits(At<char*>(mixer, 0x54)) ? 0xEEEEu : eax);
        Mix(At<uint32_t>(mixer, 0x104));
        Mix(At<uint32_t>(mixer, 0xE0));
        Mix(At<uint32_t>(mixer, 0xE4));
        MixSounding(mixer);
    };
    // No level: only the finished leave and the rest update.
    tick(100000, 0);
    Sound(mixer, &world.instances[0]);
    Sound(mixer, &world.instances[1]);
    tick(100000, 0);
    // A level: before and past the wait (7 less 3 scores on: 4 s).
    Virtual<uint32_t>(part, 0x2C, 0x300u);
    for (uint32_t time : {3000u, 4000u, 4001u, 9000u, 2000u, 12000u, 30000u, 50000u, 70000u})
        tick(time, time > 5000 ? 1000u : 0u);
    // The wait at its least: 2 s with 5 or more on; a time below +0x104.
    for (uint32_t on : {5u, 9u, 0xFFFFFFF0u}) {
        At<uint32_t>(mixer, 0x110) = on;
        tick(2001, 0);
        tick(2000, 0);
    }
    tick(10, 20);
    At<uint32_t>(mixer, 0x110) = 3;
    // Four sounding: no pick; none playing: one leaves at a time.
    for (MxInstance& instance : world.instances)
        instance.playing = 1;
    for (uint32_t i = 0; i < 6; ++i)
        Sound(mixer, &world.instances[10 + i]);
    tick(60000, 0);
    for (MxInstance& instance : world.instances)
        instance.playing = 0;
    for (int k = 0; k < 4; ++k)
        tick(60000, 0);
    // Draws over the whole weight: a weight that alone reaches every draw.
    world.descs[5].weight = 0xFFFFFFF0u;
    Virtual<uint32_t>(mixer, 0x10, 0x70u, 1u);
    for (int k = 0; k < 6; ++k)
        tick(80000 + k, 0);
    world.descs[5].weight = 100;
    // Waiting.
    g_mxSound.waiting = 1;
    tick(90000, 0);
    g_mxSound.waiting = 0;
    // A null instance in the list (Update skips it; the first one finished
    // leaves before the unchecked check reaches it).
    Sound(mixer, nullptr);
    for (MxInstance& instance : world.instances)
        instance.playing = 0;
    tick(1, 0);
    MixMixer(mixer);
    EndMixer(mixer);
}

// Prepare, Play, IsPlaying, Stop, the table getter.
void DriveMixPlay()
{
    char* const mixer = NewMixer();
    char* const part = mixer + 4;
    MixWord(Virtual<uint32_t>(part, 0x10));
    MixByte(Virtual<uint32_t>(part, 0x24));
    MixByte(Virtual<uint32_t>(part, 0x28));
    for (int k = 0; k < 2; ++k) {
        // Play answers the stopwatch's time: only AL.
        MixByte(Virtual<uint32_t>(part, 0x20));
        Mix(g_mxSound.target == mixer + 8 ? 1u : 0u);
        Mix(At<uint32_t>(mixer, 0x100) == At<uint32_t>(mixer, 0x104) ? 1u : 0u);
        MixByte(Virtual<uint32_t>(part, 0x24));
    }
    for (int k = 0; k < 2; ++k) {
        // Not running, Stop answers over its caller's EAX: only AL.
        MixByte(Virtual<uint32_t>(part, 0x28));
        MixByte(Virtual<uint32_t>(part, 0x24));
        Mix(g_mxSound.target ? 1u : 0u);
    }
    for (uint32_t key : {0u, 1u, 0x70u})
        Mix(Virtual<uint32_t>(part, 0x18, key));
    At<uint32_t>(mixer, 0x100) = 0;
    At<uint32_t>(mixer, 0x104) = 0;
    MixMixer(mixer);
    EndMixer(mixer);
}

// Real scores: slot 5 with a file of ours and one that is not there (the
// COM's sound service, which a muted game may not have), and a prepared
// service of part u6 kept by hand; turned on, the settings, turned off.
void DriveMixAdd()
{
    const char* const temp = std::getenv("TEMP");
    std::string folder = temp ? temp : ".";
    folder += "\\";
    char file[64];
    std::snprintf(file, sizeof file, "opensc4-gzu8-%d.txt", _getpid());
    const std::string path = folder + file;
    const std::string text = FullText(2, 3, 0);
    if (FILE* const out = std::fopen(path.c_str(), "wb")) {
        std::fwrite(text.data(), 1, text.size(), out);
        std::fclose(out);
    }
    char* const mixer = NewMixer();
    char* const part = mixer + 4;
    MixWord(Virtual<uint32_t>(part, 0x10));
    {
        GameText name(path.c_str());
        const uint32_t eax = Virtual<uint32_t>(part, 0x14, name.get(), 0x81u);
        MixByte(eax);
        const uint32_t table = Virtual<uint32_t>(part, 0x18, 0x81u);
        Mix(table != 0 ? 1u : 0u);
    }
    {
        GameText name((folder + "opensc4-gzu8-none.txt").c_str());
        MixByte(Virtual<uint32_t>(part, 0x14, name.get(), 0x82u));
    }
    char* const service = PreparedService(FullText(3, 4, 0));
    {
        uint32_t key = 0x83;
        void* const kept = CallThis<void*>(addr::cGZSoundMixer_ScoresIndex, mixer + 0x18, &key);
        void* held = service;
        CallThis<void*>(addr::SC4RefPtr_AssignFrom, kept, &held);
    }
    Mix(At<uint32_t>(mixer, 0x18 + 4));
    Mix(At<uint32_t>(mixer, 0x24 + 4));
    for (uint32_t key : {0x81u, 0x82u, 0x83u}) {
        MixWord(Virtual<uint32_t>(mixer, 0x10, key, 1u));
        Mix(At<uint32_t>(mixer, 0x108));
        Mix(At<uint32_t>(mixer, 0x10C));
        Mix(At<uint32_t>(mixer, 0x110));
    }
    MixWord(Virtual<uint32_t>(part, 0x34, 500u));
    MixWord(Virtual<uint32_t>(part, 0x38, 300u));
    MixWord(Virtual<uint32_t>(part, 0x3C, 7u));
    MixWord(Virtual<uint32_t>(part, 0x6C, 12u));
    Mix(At<uint32_t>(mixer, 0x108));
    Mix(At<uint32_t>(mixer, 0x10C));
    for (uint32_t key : {0x83u, 0x81u}) {
        MixWord(Virtual<uint32_t>(mixer, 0x10, key, 0u));
        Mix(At<uint32_t>(mixer, 0x108));
        Mix(At<uint32_t>(mixer, 0x110));
    }
    // The tables slot 5 keeps (+0x24) are let go without a release: the
    // destructor releases them before the scores, whose Shutdown deletes its
    // table again (the game's own; an unmuted game, whose COM makes the
    // score, died there on both sides).
    char* const tables = At<char*>(mixer, 0x24);
    for (char* node = At<char*>(tables, 8); node != tables; node = TreeNext(node))
        At<void*>(node, 0x14) = nullptr;
    EndMixer(mixer);
    std::remove(path.c_str());
}

// OPENSC4_TEST_GZSOUNDMIX=<day>: part u8's functions through their slots
// and addresses with scores, instances, descriptions and a sound service of
// our own, then with real scores. Every result goes into the fingerprint's
// `gzm` part; addresses never do.
void DriveGzSoundMix(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_GZSOUNDMIX") || !game::IsBuild641())
        return;
    g_mxOn = true;
    uint32_t* const saved = g_into;
    g_into = &g_mxHash;
    MxFakes fakes;
    g_mxSound = MxSound{MxSoundVtable(), 1, 0, nullptr};
    DriveMixObjects();
    MxStage("objects");
    DriveMixGrid();
    MxStage("grid");
    DriveMixControl();
    MxStage("control");
    DriveMixPool();
    MxStage("pool");
    DriveMixTick();
    MxStage("tick");
    DriveMixPlay();
    MxStage("play");
    DriveMixAdd();
    MxStage("add");
    Mix(static_cast<uint32_t>(g_mxSound.refs));
    g_into = saved;
    log::Write("autotest: gzsoundmix driven (hash %08X)", g_mxHash);
}

void MxFingerprint(cISC4City*, std::string& line)
{
    if (!g_mxOn)
        return;
    char text[32];
    std::snprintf(text, sizeof text, " | gzm %08X", g_mxHash);
    line += text;
}

const FingerprintPart kMxFingerprint{"gzsoundmix", 4, &MxFingerprint};
const DayHook kMxDrive{"gzsoundmix", 4, &DriveGzSoundMix};


// ====================================================================================
// Part u9: the UI script loader 0xFA3562FB and its director
// (OPENSC4_TEST_UISCRIPT). Real loaders, parsers, tables, root holders and
// LEGACY readers where the game makes them; readers, root holders, values
// and a parser of our own where a field or an argument can take one. Every
// call into ours goes into the hash; addresses never do.

bool g_usOn = false;
uint32_t g_usHash = 2166136261u;

void UsStage(const char* name) { log::Write("autotest: uiscript: %s %08X", name, g_usHash); }

// One of our objects: an id and a reference count.
struct UsObject {
    void* const* vtable;
    uint32_t id;
    int32_t refs;
    UsObject* table;  // a reader's slot 4
};

std::vector<UsObject*> g_usObjects;

// An object by its id when it is ours, else whether there is one.
void MixUsObject(const void* object)
{
    for (const UsObject* ours : g_usObjects) {
        if (ours == object) {
            Mix(ours->id);
            return;
        }
    }
    Mix(object ? 0xEEEEu : 0u);
}

uint32_t __fastcall UsQueryInterface(UsObject* self, void*, uint32_t iid, void** out)
{
    Mix(0x9100);
    Mix(self->id);
    Mix(iid);
    if (iid != 0x9A4527CB)
        return 0;
    *out = self;
    ++self->refs;
    return 1;
}
uint32_t __fastcall UsAddRef(UsObject* self, void*)
{
    Mix(0x9101);
    Mix(self->id);
    return static_cast<uint32_t>(++self->refs);
}
uint32_t __fastcall UsRelease(UsObject* self, void*)
{
    Mix(0x9102);
    Mix(self->id);
    return static_cast<uint32_t>(--self->refs);
}
// Slots with one argument (an object) and with none.
uint32_t __fastcall UsSlotObject(UsObject* self, void*, void* object)
{
    Mix(0x9110);
    Mix(self->id);
    MixUsObject(object);
    return 0x21;
}
void* __fastcall UsReaderTable(UsObject* self, void*)
{
    Mix(0x9114);
    Mix(self->id);
    return self->table;
}
// A root holder's slot 5: the window under an id (ours under 7).
void* __fastcall UsRootWindow(UsObject* self, void*, uint32_t id)
{
    Mix(0x9115);
    Mix(self->id);
    Mix(id);
    return id == 7 ? self->table : nullptr;
}
// A window's slot 3 (Load's): the window itself.
void* __fastcall UsWindowSelf(UsObject* self, void*)
{
    Mix(0x9113);
    Mix(self->id);
    return self;
}
// A value's slot 3 (its type: the id's high word) and slot 55 (its text).
uint32_t __fastcall UsValueType(UsObject* self, void*)
{
    Mix(0x9120);
    Mix(self->id);
    return self->id >> 16;
}
uint32_t __fastcall UsValueText(UsObject* self, void*, void* text)
{
    Mix(0x9121);
    Mix(self->id);
    Mix(text ? 1u : 0u);
    return 0x31;
}

// A reader (slots 3, 5, 6 and 9 an object, 4 its table), a root holder
// (4 an object, 5 a window by id, 9 an object), a value (3, 55).
void* const* UsReaderVtable()
{
    static void* table[12];
    if (!table[0]) {
        for (void*& slot : table)
            slot = reinterpret_cast<void*>(&GsTrap);
        table[0] = reinterpret_cast<void*>(&UsQueryInterface);
        table[1] = reinterpret_cast<void*>(&UsAddRef);
        table[2] = reinterpret_cast<void*>(&UsRelease);
        table[3] = reinterpret_cast<void*>(&UsSlotObject);
        table[4] = reinterpret_cast<void*>(&UsReaderTable);
        table[5] = reinterpret_cast<void*>(&UsSlotObject);
        table[6] = reinterpret_cast<void*>(&UsSlotObject);
        table[9] = reinterpret_cast<void*>(&UsSlotObject);
    }
    return table;
}
void* const* UsRootVtable()
{
    static void* table[12];
    if (!table[0]) {
        for (void*& slot : table)
            slot = reinterpret_cast<void*>(&GsTrap);
        table[0] = reinterpret_cast<void*>(&UsQueryInterface);
        table[1] = reinterpret_cast<void*>(&UsAddRef);
        table[2] = reinterpret_cast<void*>(&UsRelease);
        table[4] = reinterpret_cast<void*>(&UsSlotObject);
        table[5] = reinterpret_cast<void*>(&UsRootWindow);
        table[9] = reinterpret_cast<void*>(&UsSlotObject);
    }
    return table;
}
void* const* UsWindowVtable()
{
    static void* table[6];
    if (!table[0]) {
        for (void*& slot : table)
            slot = reinterpret_cast<void*>(&GsTrap);
        table[0] = reinterpret_cast<void*>(&UsQueryInterface);
        table[1] = reinterpret_cast<void*>(&UsAddRef);
        table[2] = reinterpret_cast<void*>(&UsRelease);
        table[3] = reinterpret_cast<void*>(&UsWindowSelf);
    }
    return table;
}
void* const* UsValueVtable()
{
    static void* table[56];
    if (!table[0]) {
        for (void*& slot : table)
            slot = reinterpret_cast<void*>(&GsTrap);
        table[0] = reinterpret_cast<void*>(&UsQueryInterface);
        table[1] = reinterpret_cast<void*>(&UsAddRef);
        table[2] = reinterpret_cast<void*>(&UsRelease);
        table[3] = reinterpret_cast<void*>(&UsValueType);
        table[55] = reinterpret_cast<void*>(&UsValueText);
    }
    return table;
}

// The objects a stage makes, kept until it ends.
struct UsObjects {
    std::vector<UsObject*> made;
    UsObject* Make(void* const* vtable, uint32_t id, UsObject* table = nullptr)
    {
        UsObject* const object = new UsObject{vtable, id, 1, table};
        made.push_back(object);
        g_usObjects.push_back(object);
        return object;
    }
    ~UsObjects()
    {
        for (UsObject* object : made) {
            Mix(static_cast<uint32_t>(object->refs));
            for (auto it = g_usObjects.begin(); it != g_usObjects.end(); ++it) {
                if (*it == object) {
                    g_usObjects.erase(it);
                    break;
                }
            }
            delete object;
        }
    }
};

// A parser of ours: slots 3, 6 to 11, 13 and 14 mix their arguments.
struct UsParser {
    void* const* vtable;
};
uint32_t __fastcall UsParserOne(UsParser*, void*, void* object)
{
    Mix(0x9201);
    MixUsObject(object);
    return 0x41;
}
uint32_t __fastcall UsParserTwo(UsParser*, void*, void* text, void* value)
{
    Mix(0x9202);
    Mix(text ? 1u : 0u);
    MixUsObject(value);
    return 0x42;
}
uint32_t __fastcall UsParserPush(UsParser*, void*, void* table)
{
    Mix(0x9203);
    MixUsObject(table);
    return 0x43;
}
uint32_t __fastcall UsParserPop(UsParser*, void*)
{
    Mix(0x9204);
    return 0x44;
}
void* const* UsParserVtable()
{
    static void* table[16];
    if (!table[0]) {
        for (void*& slot : table)
            slot = reinterpret_cast<void*>(&GsTrap);
        for (int slot : {3, 6, 7, 8, 11})
            table[slot] = reinterpret_cast<void*>(&UsParserOne);
        table[9] = reinterpret_cast<void*>(&UsParserTwo);
        table[10] = reinterpret_cast<void*>(&UsParserTwo);
        table[13] = reinterpret_cast<void*>(&UsParserPush);
        table[14] = reinterpret_cast<void*>(&UsParserPop);
    }
    return table;
}

void UsDelete(void* memory) { reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_operator_delete)(memory); }

char* NewLoader() { return reinterpret_cast<char*(__cdecl*)()>(addr::cGZUIScriptDirector_CreateLoader)(); }

// The loader's state: the counted fields' objects, the bytes, the tag, the
// stack's top.
void MixLoader(char* loader)
{
    Mix(At<uint8_t>(loader, 0x30));
    Mix(At<void*>(loader, 0x34) ? 1u : 0u);
    MixUsObject(At<void*>(loader, 0x38));
    Mix(At<uint8_t>(loader, 0x3C));
    MixUsObject(At<void*>(loader, 0x44));
    Mix(At<uint8_t>(loader, 0x48));
    Mix(At<uint32_t>(loader, 0x4C));
    MixUsObject(At<void*>(loader, 0x50));
    MixUsObject(At<void*>(loader, 0x54));
    MixUsObject(At<void*>(loader, 0x58));
    MixUsObject(At<void*>(loader, 0x5C));
    MixUsObject(CallThis<void*>(addr::cGZUIScriptLoader_Top, loader));
}

// The base service's AddRef and Release (the primary slots 1 and 2).
uint32_t UsLoaderAddRef(char* loader) { return Virtual<uint32_t>(loader, 0x4); }
uint32_t UsLoaderRelease(char* loader) { return Virtual<uint32_t>(loader, 0x8); }

// A director of ours for the director's start: its slot 12 a COM of ours
// whose GetClassObject gives nothing.
struct UsCom {
    void* const* vtable;
};
uint32_t __fastcall UsComGetClassObject(UsCom*, void*, uint32_t clsid, uint32_t iid, void** out)
{
    Mix(0x9300);
    Mix(clsid);
    Mix(iid);
    *out = nullptr;
    return 0;
}
UsCom g_usCom;
void* __fastcall UsDirectorCom(void*, void*)
{
    Mix(0x9301);
    return &g_usCom;
}

// The loader's objects: made by the factory, its interfaces, Init and
// Shutdown, its setters, its destructors by address; the director made on
// the stack, its start, destructor and getter; the director's other
// factories' objects made and released.
void DriveUiScriptObjects()
{
    UsObjects objects;
    char* const loader = NewLoader();
    Mix(loader ? 1u : 0u);
    if (!loader)
        return;
    Mix(At<std::uintptr_t>(loader, 0) == addr::cGZUIScriptLoader_Vtable ? 1u : 0u);
    Mix(At<std::uintptr_t>(loader, 0x18) == addr::cGZUIScriptLoader_ServiceUnknownVtable ? 1u : 0u);
    MixLoader(loader);
    Mix(UsLoaderAddRef(loader));
    const uint32_t iids[] = {1, 0xFA3562FA, 0x1A2BDEB8, 0x1A2BDEB9, 0x1A2BDEBA, 0x7A4A48ED, 0x287FB697, 0x12345678};
    for (const uint32_t iid : iids) {
        void* out = nullptr;
        MixByte(CallThis<uint32_t>(addr::cGZUIScriptLoader_QueryInterface, loader, iid, &out));
        Mix(out ? static_cast<uint32_t>(static_cast<char*>(out) - loader) : 0xFFFFFFFFu);
        if (out)
            Mix(UsLoaderRelease(loader));
        // Through the listener's adjustor too.
        out = nullptr;
        MixByte(Virtual<uint32_t>(loader + 8, 0x0, iid, &out));
        Mix(out ? static_cast<uint32_t>(static_cast<char*>(out) - loader) : 0xFFFFFFFFu);
        if (out)
            Mix(UsLoaderRelease(loader));
    }
    // Init (the service's slot 8) twice, Shutdown (9) twice.
    MixByte(Virtual<uint32_t>(loader + 0x14, 0x20));
    MixByte(Virtual<uint32_t>(loader + 0x14, 0x20));
    MixLoader(loader);
    MixByte(Virtual<uint32_t>(loader + 0x14, 0x24));
    MixByte(Virtual<uint32_t>(loader + 0x14, 0x24));
    MixLoader(loader);
    // Slot 10 and the part's slot 4.
    UsObject* const a = objects.Make(UsReaderVtable(), 0x101);
    UsObject* const b = objects.Make(UsReaderVtable(), 0x102);
    for (UsObject* object : {a, a, b, static_cast<UsObject*>(nullptr), a}) {
        Virtual<uint32_t>(loader, 0x28, object);
        MixUsObject(At<void*>(loader, 0x5C));
        MixByte(Virtual<uint32_t>(loader + 4, 0x10, object));
        MixUsObject(At<void*>(loader, 0x58));
    }
    MixByte(CallThis<uint32_t>(addr::cGZUIScriptLoader_SetContext, loader + 4, static_cast<void*>(nullptr)));
    CallThis<uint32_t>(addr::cGZUIScriptLoader_SetHeld, loader, static_cast<void*>(b));
    // Shutdown after an Init releases the held one; the last Release deletes.
    MixByte(Virtual<uint32_t>(loader + 0x14, 0x20));
    MixByte(CallThis<uint32_t>(addr::cGZUIScriptLoader_Shutdown, loader + 0x14));
    MixLoader(loader);
    Mix(UsLoaderRelease(loader));
    // The destructor and the deleting destructor by address.
    char* const second = NewLoader();
    CallThis<uint32_t>(addr::cGZUIScriptLoader_SetHeld, second, static_cast<void*>(a));
    MixByte(CallThis<uint32_t>(addr::cGZUIScriptLoader_Init, second + 0x14));
    CallThis<uint32_t>(addr::cGZUIScriptLoader_Destruct, second);
    Mix(At<std::uintptr_t>(second, 0) == addr::cGZUIScriptLoader_Vtable ? 1u : 0u);
    UsDelete(second);
    char* const third = NewLoader();
    MixIs(CallThis<char*>(addr::cGZUIScriptLoader_DeletingDestructor, third, 0u), third);
    UsDelete(third);
    char* const fourth = NewLoader();
    MixIs(CallThis<char*>(addr::cGZUIScriptLoader_DeletingDestructor, fourth, 1u), fourth);
    // The other factories' objects.
    for (const std::uintptr_t factory : {addr::cGZUIScriptDirector_CreateRoot, addr::cGZUIScriptDirector_CreateLegacy,
                                         addr::cGZUIScriptDirector_Create478D1E6F}) {
        void* const object = reinterpret_cast<void*(__cdecl*)()>(factory)();
        Mix(object ? 1u : 0u);
        if (object) {
            Mix(Virtual<uint32_t>(object, 0x4));
            Mix(Virtual<uint32_t>(object, 0x8));
        }
    }
    // The director on the stack, its start with a COM of ours, its
    // destructor; the static one's getter.
    alignas(8) char director[0x80] = {};
    MixIs(CallThis<void*>(addr::cGZUIScriptDirector_Construct, director), director);
    Mix(At<std::uintptr_t>(director, 0) == addr::cGZUIScriptDirector_Vtable ? 1u : 0u);
    static void* comTable[4];
    comTable[3] = reinterpret_cast<void*>(&UsComGetClassObject);
    g_usCom.vtable = comTable;
    static void* directorTable[16];
    directorTable[12] = reinterpret_cast<void*>(&UsDirectorCom);
    struct {
        void* const* vtable;
    } fake{directorTable};
    MixByte(CallThis<uint32_t>(addr::cGZUIScriptDirector_Start, &fake, static_cast<void*>(nullptr)));
    MixIs(CallThis<void*>(addr::cGZUIScriptDirector_DeletingDestructor, director, 0u), director);
    MixIs(reinterpret_cast<void*(__cdecl*)()>(addr::cGZUIScriptDirector_Get)(),
          reinterpret_cast<void*>(addr::cGZUIScriptDirector_Static));
}

// The stack of parents: push, top, pop, and the attribute list's
// `children` and `/children`.
void DriveUiScriptStack()
{
    UsObjects objects;
    char* const loader = NewLoader();
    Mix(UsLoaderAddRef(loader));
    UsParser parser{UsParserVtable()};
    char* const pairs[3] = {nullptr, nullptr, nullptr};
    MixByte(CallThis<uint32_t>(addr::cGZUIScriptLoader_Pop, loader));
    MixByte(CallThis<uint32_t>(addr::cGZUIScriptLoader_Push, loader));
    UsObject* readers[3];
    for (uint32_t i = 0; i < 3; ++i) {
        readers[i] = objects.Make(UsReaderVtable(), 0x201 + i);
        At<void*>(loader, 0x44) = readers[i];
        MixByte(CallThis<uint32_t>(addr::cGZUIScriptLoader_Push, loader));
        MixLoader(loader);
    }
    MixByte(CallThis<uint32_t>(addr::cGZUIScriptLoader_Pop, loader));
    MixLoader(loader);
    // `/children` (2), `children` (1) with and without a reader.
    MixByte(Virtual<uint32_t>(loader + 0x10, 0xC, &parser, 2u, pairs));
    MixLoader(loader);
    MixByte(Virtual<uint32_t>(loader + 0x10, 0xC, &parser, 1u, pairs));
    MixLoader(loader);
    MixByte(Virtual<uint32_t>(loader + 0x10, 0xC, &parser, 1u, pairs));
    MixLoader(loader);
    for (int i = 0; i < 4; ++i) {
        MixByte(CallThis<uint32_t>(addr::cGZUIScriptLoader_Attributes, loader + 0x10, static_cast<void*>(&parser),
                                   2u, pairs));
        MixLoader(loader);
    }
    // Shutdown with readers on the stack.
    At<void*>(loader, 0x44) = readers[0];
    CallThis<uint32_t>(addr::cGZUIScriptLoader_Push, loader);
    At<void*>(loader, 0x44) = readers[1];
    CallThis<uint32_t>(addr::cGZUIScriptLoader_Push, loader);
    Virtual<uint32_t>(loader + 0x14, 0x20);
    Mix(UsLoaderRelease(loader));
}

// The parser's events with a parser, readers, root holders and values of
// ours: the document's start and end (done or not, with the words being
// defined or not), a tag's start (classes the COM does not have), its end,
// the attribute list (a reader and a holder; `define` with a name and a
// value, a name only, a name that is not a text, no name, no pairs; a tag
// that is none of these).
void DriveUiScriptEvents()
{
    UsObjects objects;
    char* const loader = NewLoader();
    Mix(UsLoaderAddRef(loader));
    UsParser parser{UsParserVtable()};
    // The start makes a real table and root holder.
    MixByte(Virtual<uint32_t>(loader + 8, 0xC, &parser));
    Mix(At<void*>(loader, 0x38) ? 1u : 0u);
    Mix(At<void*>(loader, 0x50) ? 1u : 0u);
    for (const uint32_t tag : {0x12345678u, 0u, 3u}) {
        MixByte(Virtual<uint32_t>(loader + 0xC, 0xC, &parser, tag));
        MixLoader(loader);
    }
    MixByte(Virtual<uint32_t>(loader + 0xC, 0x10, &parser, 0u));
    At<uint8_t>(loader, 0x48) = 1;
    MixByte(CallThis<uint32_t>(addr::cGZUIScriptLoader_TagEnd, loader + 0xC, static_cast<void*>(&parser), 0u));
    MixLoader(loader);
    // A reader and a root holder of ours.
    Virtual<uint32_t>(At<void*>(loader, 0x50), 0x8);
    UsObject* const table = objects.Make(UsReaderVtable(), 0x310);
    UsObject* const reader = objects.Make(UsReaderVtable(), 0x301, table);
    UsObject* const window = objects.Make(UsWindowVtable(), 0x320);
    UsObject* const root = objects.Make(UsRootVtable(), 0x302, window);
    At<void*>(loader, 0x50) = root;
    At<void*>(loader, 0x54) = reader;
    char pairBytes[4][8] = {};
    char* list[3] = {pairBytes[0], pairBytes[0], pairBytes[4 - 1] + 8};
    MixByte(Virtual<uint32_t>(loader + 0x10, 0xC, &parser, 9u, list));
    MixLoader(loader);
    // `define`.
    UsObject* const text = objects.Make(UsValueVtable(), 0x800E0001);
    UsObject* const number = objects.Make(UsValueVtable(), 0x00060002);
    UsObject* const value = objects.Make(UsValueVtable(), 0x00060003);
    struct Pair {
        uint32_t number;
        void* value;
    };
    const Pair cases[][3] = {
        {{4, text}, {5, value}, {0, nullptr}},
        {{5, value}, {4, text}, {7, number}},
        {{4, text}, {0, nullptr}, {0, nullptr}},
        {{4, number}, {5, value}, {0, nullptr}},
        {{5, value}, {6, text}, {0, nullptr}},
        {{4, number}, {4, text}, {5, nullptr}},
    };
    for (const auto& pairs : cases) {
        Pair copy[3];
        std::memcpy(copy, pairs, sizeof copy);
        uint32_t n = 0;
        while (n < 3 && (copy[n].number || copy[n].value))
            ++n;
        char* const bounds[3] = {reinterpret_cast<char*>(copy), reinterpret_cast<char*>(copy + n),
                                 reinterpret_cast<char*>(copy + 3)};
        MixByte(Virtual<uint32_t>(loader + 0x10, 0xC, &parser, 3u, bounds));
    }
    MixByte(Virtual<uint32_t>(loader + 0x10, 0xC, &parser, 3u, list));
    for (const uint32_t tag : {0u, 4u, 0x12345678u})
        MixByte(Virtual<uint32_t>(loader + 0x10, 0xC, &parser, tag, list));
    // The end, done: the table released, the stack emptied.
    At<void*>(loader, 0x44) = reader;
    CallThis<uint32_t>(addr::cGZUIScriptLoader_Push, loader);
    MixByte(Virtual<uint32_t>(loader + 8, 0x10, &parser, 1u));
    MixLoader(loader);
    // Again, not done: the holder and the reader released, the table popped.
    MixByte(CallThis<uint32_t>(addr::cGZUIScriptLoader_DocumentStart, loader + 8, static_cast<void*>(&parser)));
    Virtual<uint32_t>(At<void*>(loader, 0x50), 0x8);
    At<void*>(loader, 0x50) = root;
    ++root->refs;
    At<void*>(loader, 0x54) = reader;
    ++reader->refs;
    At<uint8_t>(loader, 0x48) = 1;
    MixByte(CallThis<uint32_t>(addr::cGZUIScriptLoader_DocumentEnd, loader + 8, static_cast<void*>(&parser), 0u));
    MixLoader(loader);
    // While the words are defined: no table, nothing let go.
    At<uint8_t>(loader, 0x3C) = 1;
    MixByte(Virtual<uint32_t>(loader + 8, 0xC, &parser));
    MixLoader(loader);
    MixByte(Virtual<uint32_t>(loader + 8, 0x10, &parser, 1u));
    MixByte(Virtual<uint32_t>(loader + 8, 0x10, &parser, 0u));
    MixLoader(loader);
    At<uint8_t>(loader, 0x3C) = 0;
    // Load and Create with a parse of ours: the parser at +0x34 is ours.
    At<void*>(loader, 0x34) = &parser;
    At<void*>(loader, 0x50) = root;
    ++root->refs;
    const uint32_t key[3] = {0x0BADF00D, 0x96A006B0, 0x12345678};
    void* const loaded = Virtual<void*>(loader, 0x10, key, static_cast<void*>(nullptr), 7u);
    MixUsObject(loaded);
    MixLoader(loader);
    At<void*>(loader, 0x50) = root;
    ++root->refs;
    void* out = nullptr;
    MixByte(Virtual<uint32_t>(loader, 0xC, key, static_cast<void*>(nullptr), 7u, 0x9A4527CBu, &out));
    MixUsObject(out);
    At<void*>(loader, 0x50) = root;
    ++root->refs;
    MixByte(CallThis<uint32_t>(addr::cGZUIScriptLoader_Create, loader, key, static_cast<void*>(nullptr), 8u,
                               0x9A4527CBu, &out));
    MixUsObject(CallThis<void*>(addr::cGZUIScriptLoader_Parser, loader));
    At<void*>(loader, 0x34) = nullptr;
    At<void*>(loader, 0x50) = nullptr;
    while (CallThis<uint32_t>(addr::cGZUIScriptLoader_Pop, loader) & 0xFF) {
    }
    Mix(UsLoaderRelease(loader));
}

// Real parses: the loader's parser made and its words defined, UI scripts
// read from texts (defines, names, values, nested children, a LEGACY
// window, unknown tags, malformed, empty), a file, resources, Load and
// Create with keys that are there and not.
void DriveUiScriptParse()
{
    char* const loader = NewLoader();
    Mix(UsLoaderAddRef(loader));
    Virtual<uint32_t>(loader + 0x14, 0x20);
    MixByte(CallThis<uint32_t>(addr::cGZUIScriptLoader_MakeParser, loader));
    void* const parser = Virtual<void*>(loader, 0x20);
    Mix(parser ? 1u : 0u);
    MixIs(CallThis<void*>(addr::cGZUIScriptLoader_Parser, loader), parser);
    MixByte(CallThis<uint32_t>(addr::cGZUIScriptLoader_MakeParser, loader));
    MixLoader(loader);
    const char* const texts[] = {
        "",
        "   \r\n\t",
        "text only",
        "<define name=\"foo\" val=7>",
        "<3 4=\"bar\" 5=9>",
        "<define name=\"x\">",
        "<define name=1 val=2>",
        "<_define _name=\"z\" _val=3>",
        "<define name=\"t\" val=3><t name=\"u\" val=4><u>",
        "<children><define name=\"a\" val=1></children>",
        "</children><children><children>",
        "<none>",
        "<0x12345678 a=1>",
        "<comic9 x=1>",
        "<LEGACY",
        "<<>>",
        "<define name=\"q\" val=\"text\" name=\"r\">",
        "<LEGACY clsid=GZWinGen iid=IGZWin area=(0,0,10,10) id=0x77>",
        ("<LEGACY clsid=GZWinGen iid=IGZWin area=(0,0,40,40) id=0x10><children>"
         "<LEGACY clsid=GZWinGen iid=IGZWin area=(1,1,9,9) id=0x11></children>"),
    };
    for (const char* text : texts) {
        GameText script(text);
        void* const root = std::strlen(text) % 2 ? Virtual<void*>(loader, 0x14, script.get())
                                                  : CallThis<void*>(addr::cGZUIScriptLoader_ParseString, loader,
                                                                    script.get());
        Mix(root ? 1u : 0u);
        MixLoader(loader);
        if (root) {
            for (const uint32_t id : {0x77u, 0x10u, 0x11u, 0x12u})
                Mix(Virtual<void*>(root, 0x14, id) ? 1u : 0u);
            Virtual<uint32_t>(root, 0x24, 0u);
            Mix(Virtual<uint32_t>(root, 0x8));
        }
    }
    // A file, an empty one, a missing one.
    const char* const folder = std::getenv("TEMP");
    const std::string base =
        std::string(folder ? folder : ".") + "\\opensc4-uiscript-" + std::to_string(_getpid()) + "-";
    const std::string full = base + "full.txt";
    const std::string none = base + "empty.txt";
    if (std::FILE* f = std::fopen(full.c_str(), "wb")) {
        const char* const text = "<define name=\"f\" val=5>\r\n<f name=\"g\" val=6>";
        std::fwrite(text, 1, std::strlen(text), f);
        std::fclose(f);
    }
    if (std::FILE* f = std::fopen(none.c_str(), "wb"))
        std::fclose(f);
    for (const std::string& path : {full, none, base + "missing.txt"}) {
        GameText name(path.c_str());
        void* const root = path == none ? CallThis<void*>(addr::cGZUIScriptLoader_ParseFile, loader, name.get())
                                        : Virtual<void*>(loader, 0x18, name.get());
        Mix(root ? 1u : 0u);
        MixLoader(loader);
        if (root)
            Mix(Virtual<uint32_t>(root, 0x8));
    }
    std::remove(full.c_str());
    std::remove(none.c_str());
    // A UI file of the game's and a key that is not there.
    const uint32_t keys[][3] = {{0, 0x96A006B0, 0xCBC905CD}, {0, 0x96A006B0, 0x0BADF00D}};
    for (const auto& key : keys) {
        for (int way = 0; way < 2; ++way) {
            void* const root = way ? CallThis<void*>(addr::cGZUIScriptLoader_ParseResource, loader, key)
                                   : Virtual<void*>(loader, 0x1C, key);
            Mix(root ? 1u : 0u);
            MixLoader(loader);
            if (root) {
                Virtual<uint32_t>(root, 0x24, 0u);
                Mix(Virtual<uint32_t>(root, 0x8));
            }
        }
        void* const window = CallThis<void*>(addr::cGZUIScriptLoader_Load, loader, key, static_cast<void*>(nullptr),
                                             0u);
        Mix(window ? 1u : 0u);
        MixLoader(loader);
        if (window)
            Virtual<uint32_t>(window, 0x8);
        void* out = nullptr;
        MixByte(Virtual<uint32_t>(loader, 0xC, key, static_cast<void*>(nullptr), 0u, 0x9A4527CBu, &out));
        Mix(out ? 1u : 0u);
        if (out)
            Virtual<uint32_t>(out, 0x8);
        MixLoader(loader);
    }
    // The parent set on its own: a LEGACY reader on the stack.
    MixByte(CallThis<uint32_t>(addr::cGZUIScriptLoader_SetParent, loader + 4, static_cast<void*>(nullptr)));
    MixLoader(loader);
    MixByte(Virtual<uint32_t>(loader + 4, 0xC, static_cast<void*>(nullptr)));
    MixLoader(loader);
    MixByte(CallThis<uint32_t>(addr::cGZUIScriptLoader_Shutdown, loader + 0x14));
    MixLoader(loader);
    Mix(UsLoaderRelease(loader));
}

// OPENSC4_TEST_UISCRIPT=<day>: part u9's functions through their slots and
// addresses. Every result goes into the fingerprint's `us` part.
void DriveUiScript(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_UISCRIPT") || !game::IsBuild641())
        return;
    g_usOn = true;
    uint32_t* const saved = g_into;
    g_into = &g_usHash;
    DriveUiScriptObjects();
    UsStage("objects");
    DriveUiScriptStack();
    UsStage("stack");
    DriveUiScriptEvents();
    UsStage("events");
    DriveUiScriptParse();
    UsStage("parse");
    g_into = saved;
    log::Write("autotest: uiscript driven (hash %08X)", g_usHash);
}

void UsFingerprint(cISC4City*, std::string& line)
{
    if (!g_usOn)
        return;
    char text[32];
    std::snprintf(text, sizeof text, " | us %08X", g_usHash);
    line += text;
}

const FingerprintPart kUsFingerprint{"uiscript", 4, &UsFingerprint};
const DayHook kUsDrive{"uiscript", 4, &DriveUiScript};

// ====================================================================================
// Part u10: the MP3 decoder director 0x1CEC20BE, its stream 0x3CEC2B47 and
// maker 0x9CEC2B58, the decoder's own functions, and the UI script's root
// holder 0x5A354595 (OPENSC4_TEST_MP3). Real streams, decoders, makers and
// root holders made by the game's code, one of the game's radio MP3 files
// (and files made from it); windows, interfaces and an allocator of our own
// where the root holder takes them (each hashes every call it gets).
// Every result goes into the hash; addresses never do.

bool g_mpOn = false;
uint32_t g_mpHash = 2166136261u;

void MpStage(const char* name) { log::Write("autotest: mp3: %s %08X", name, g_mpHash); }

template <typename R, typename... Args>
inline R MpCdecl(std::uintptr_t address, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(address)(args...);
}

std::string MpGameFolder()
{
    char path[520] = {};
    GetModuleFileNameA(nullptr, path, sizeof path);
    std::string folder(path);
    for (int up = 0; up < 2; ++up) {
        const std::size_t at = folder.find_last_of("\\/");
        if (at == std::string::npos)
            return ".";
        folder.erase(at);
    }
    return folder;
}

std::vector<uint8_t> MpReadFile(const std::string& path)
{
    std::vector<uint8_t> data;
    if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
        uint8_t chunk[4096];
        std::size_t got;
        while ((got = std::fread(chunk, 1, sizeof chunk, f)) > 0)
            data.insert(data.end(), chunk, chunk + got);
        std::fclose(f);
    }
    return data;
}

void MpWriteFile(const std::string& path, const std::vector<uint8_t>& data)
{
    if (std::FILE* f = std::fopen(path.c_str(), "wb")) {
        if (!data.empty())
            std::fwrite(data.data(), 1, data.size(), f);
        std::fclose(f);
    }
}

void MixSamples(const void* data, uint32_t bytes)
{
    const uint8_t* const p = static_cast<const uint8_t*>(data);
    uint32_t word = 0;
    for (uint32_t i = 0; i < bytes; ++i) {
        word = word * 31 + p[i];
        if ((i & 63) == 63) {
            Mix(word);
            word = 0;
        }
    }
    Mix(word);
}

// The decoder's counters (not its pointers).
void MixDecoder(char* decoder)
{
    Mix(At<uint32_t>(decoder, 0x4A48));
    Mix(At<uint32_t>(decoder, 0x4A4C));
    Mix(At<uint32_t>(decoder, 0x4A50));
    Mix(At<uint32_t>(decoder, 0x4A54));
    Mix(At<uint32_t>(decoder, 0x4A58));
    Mix(At<uint8_t>(decoder, 0x4A5C));
}

// A stream's fields (not its pointers; the block +0x10F8 only when `block`,
// as a new stream's comes from what its memory held).
void MixStream(char* stream, bool block)
{
    Mix(At<uint32_t>(stream, 4));
    for (std::size_t at = 8; at < 0x20; at += 4)
        Mix(At<uint32_t>(stream, at));
    Mix(At<uint32_t>(stream, 0xA0) ? 1u : 0u);
    Mix(At<uint32_t>(stream, 0x10B8));
    Mix(At<uint32_t>(stream, 0x10BC));
    Mix(At<uint32_t>(stream, 0x10C0));
    Mix(At<uint8_t>(stream, 0x10D0));
    Mix(At<uint8_t>(stream, 0x10E8));
    Mix(At<uint8_t>(stream, 0x10E9));
    Mix(At<uint16_t>(stream, 0x10EC));
    Mix(At<uint16_t>(stream, 0x10EE));
    Mix(At<uint32_t>(stream, 0x10F0));
    Mix(At<uint32_t>(stream, 0x10F4));
    if (block)
        Mix(At<uint16_t>(stream, 0x10F8));
    Mix(At<uint16_t>(stream, 0x10FA));
    Mix(At<uint32_t>(stream, 0x10FC));
    Mix(At<uint32_t>(stream, 0x1100));
}

// Headers, ID3 tags, a decoder fed frame by frame.
void DriveMp3Decode(const std::vector<uint8_t>& music)
{
    const uint8_t headers[][4] = {
        {0xFF, 0xFB, 0x90, 0x44}, {0xFF, 0xFB, 0x92, 0xC4}, {0xFF, 0xF3, 0x64, 0x00}, {0xFF, 0xE3, 0x54, 0xC0},
        {0xFF, 0xFF, 0x10, 0x00}, {0xFF, 0xFD, 0xA8, 0x80}, {0xFF, 0xF7, 0xE6, 0x40}, {0xFF, 0xF5, 0x02, 0x00},
        {0xFE, 0xFB, 0x90, 0x44}, {0xFF, 0xCB, 0x90, 0x44}, {0xFF, 0xEB, 0x90, 0x44}, {0xFF, 0xF9, 0x90, 0x44},
        {0xFF, 0xFB, 0xF0, 0x44}, {0xFF, 0xFB, 0x9C, 0x44}, {0xFF, 0xFA, 0x93, 0xFF}, {0xFF, 0xE5, 0xEB, 0x80},
    };
    for (const auto& header : headers) {
        int32_t info[6] = {7, 7, 7, 7, 7, 7};
        Mix(static_cast<uint32_t>(MpCdecl<int32_t>(addr::cMP3Decoder_ParseHeader, header, info)));
        for (const int32_t value : info)
            Mix(static_cast<uint32_t>(value));
    }
    const uint8_t tags[][10] = {
        {'I', 'D', '3', 3, 0, 0, 0, 0, 0, 0x14}, {'I', 'D', '3', 4, 0, 0, 0x7F, 0x7F, 0x7F, 0x7F},
        {'I', 'D', '3', 3, 0, 0, 1, 2, 3, 4},    {'I', 'D', '2', 3, 0, 0, 0, 0, 0, 0x14},
        {'X', 'D', '3', 3, 0, 0, 0, 0, 0, 0x14}, {'I', 'X', '3', 3, 0, 0, 0, 0, 0, 0x14},
        {'I', 'D', '3', 3, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF},
    };
    for (const auto& tag : tags)
        Mix(MpCdecl<uint32_t>(addr::cMP3Decoder_Id3Size, tag));
    if (music.size() >= 10)
        Mix(MpCdecl<uint32_t>(addr::cMP3Decoder_Id3Size, music.data()));

    char* const decoder = MpCdecl<char*>(addr::CRT_malloc, 0x4A60u);
    std::memset(decoder, 0, 0x4A60);
    CallThis<void*>(addr::cMP3Decoder_Construct, decoder);
    CallThis<uint32_t>(addr::cMP3Decoder_Reset, decoder);
    MixDecoder(decoder);
    static int16_t left[0x1800];
    static int16_t right[0x1800];
    int16_t* both[2] = {left, right};
    int16_t* onlyRight[2] = {nullptr, right};
    const uint8_t shortFrame[3] = {0xFF, 0xFB, 0x90};
    Mix(static_cast<uint32_t>(CallThis<int32_t>(addr::cMP3Decoder_Feed, decoder, shortFrame, 3, -1)));
    MixDecoder(decoder);
    Mix(static_cast<uint32_t>(CallThis<int32_t>(addr::cMP3Decoder_Decode, decoder, both, 100)));
    MixDecoder(decoder);
    std::size_t at = music.size() >= 10 ? MpCdecl<uint32_t>(addr::cMP3Decoder_Id3Size, music.data()) : 0;
    const int32_t counts[] = {0, 100, 2000, 1152, 576, 1, 5000, -3};
    for (int frame = 0; frame < 40 && at + 4 <= music.size(); ++frame) {
        int32_t info[6];
        if (MpCdecl<int32_t>(addr::cMP3Decoder_ParseHeader, &music[at], info) < 0 || info[1] <= 4 ||
            at + static_cast<std::size_t>(info[1]) > music.size())
            break;
        const int32_t limit = frame == 5 ? 0 : frame == 12 ? 300 : -1;
        Mix(static_cast<uint32_t>(CallThis<int32_t>(addr::cMP3Decoder_Feed, decoder, &music[at], info[1], limit)));
        Mix(static_cast<uint32_t>(CallThis<int32_t>(addr::cMP3Decoder_Feed, decoder, &music[at], info[1], -1)));
        MixDecoder(decoder);
        for (int part = 0; part < 3; ++part) {
            const int32_t count = counts[(frame + part) % 8];
            std::memset(left, 0, sizeof left);
            std::memset(right, 0, sizeof right);
            const int32_t got = CallThis<int32_t>(addr::cMP3Decoder_Decode, decoder, frame % 4 == 3 ? onlyRight : both,
                                                  count);
            Mix(static_cast<uint32_t>(got));
            MixDecoder(decoder);
            if (got > 0) {
                MixSamples(left, static_cast<uint32_t>(got) * 2);
                MixSamples(right, static_cast<uint32_t>(got) * 2);
            }
        }
        if (frame == 10 || frame == 14)
            CallThis<uint32_t>(addr::cMP3Decoder_Reset, decoder);
        at += static_cast<std::size_t>(info[1]);
    }
    MixDecoder(decoder);
    CallThis<void>(addr::cMP3Decoder_Destruct, decoder);
    MpCdecl<void>(addr::CRT_free, decoder);
}

// A stream by its factory and held. The frame header's six dwords are what
// the memory held until a header is read, and a read takes the format from
// them: the test sets them (2 channels, 22050) so that both sides agree.
char* MpNewStream()
{
    char* const stream = MpCdecl<char*>(addr::cHitMP3Stream_Create);
    if (!stream)
        return nullptr;
    std::memset(stream + 8, 0, 0x18);
    At<int32_t>(stream, 0x14) = 2;
    At<int32_t>(stream, 0x18) = 22050;
    Virtual<uint32_t>(stream, 0x4);
    return stream;
}

// A stream through its slots and addresses on one file. Without `reads`
// (a file with no frame: the game's read would then divide by 0 channels)
// only opened, rewound and closed.
void DriveMp3StreamFile(const std::string& path, bool byAddress, bool reads = true)
{
    char* const stream = MpNewStream();
    Mix(stream ? 1u : 0u);
    if (!stream)
        return;
    MixStream(stream, false);
    GameText name(path.c_str());
    const char* const text = At<const char*>(name.get(), 4);
    const uint32_t opened = byAddress ? CallThis<uint32_t>(addr::cHitMP3Stream_OpenPath, stream, text)
                                      : Virtual<uint32_t>(stream, 0xC, text);
    MixByte(opened);
    MixStream(stream, false);
    MixByte(CallThis<uint32_t>(addr::cHitMP3Stream_FileSlot4, stream));
    Mix(CallThis<uint32_t>(addr::cHitMP3Stream_FileSlot6, stream));
    Mix(CallThis<uint32_t>(addr::cHitMP3Stream_Length, stream));
    MixByte(CallThis<uint32_t>(addr::cHitMP3Stream_Open, stream));
    if (!reads) {
        MixByte(CallThis<uint32_t>(addr::cHitMP3Stream_Seek, stream, 0u));
        MixStream(stream, true);
        MixByte(CallThis<uint32_t>(addr::cHitMP3Stream_Close, stream));
        Mix(Virtual<uint32_t>(stream, 0x8));
        return;
    }
    static uint8_t out[0x10000];
    const int32_t sizes[] = {4, 6, 1000, 4608, 0x4000, 3, 0xC000};
    for (const int32_t size : sizes) {
        std::memset(out, 0, sizeof out);
        const uint32_t got = byAddress ? CallThis<uint32_t>(addr::cHitMP3Stream_Read, stream, out, size)
                                       : Virtual<uint32_t>(stream, 0x38, out, size);
        Mix(got);
        MixSamples(out, got < sizeof out ? got : 0);
        MixStream(stream, true);
        if (size == 4608 && path.size() > 4 && path.compare(path.size() - 4, 4, ".mp3") == 0)
            log::Write("autotest: mp3: read %u of %d (opened %u, %u Hz, %u channels)", got, size, opened & 0xFF,
                       At<uint32_t>(stream, 0x10F0), At<uint16_t>(stream, 0x10EE));
    }
    const uint32_t seeks[] = {1000, 0, 0x7FFFFF};
    for (const uint32_t to : seeks) {
        Mix(byAddress ? CallThis<uint32_t>(addr::cHitMP3Stream_Seek, stream, to) : Virtual<uint32_t>(stream, 0x50, to));
        Mix(CallThis<uint32_t>(addr::cHitMP3Stream_FileSlot6, stream));
        MixStream(stream, true);
    }
    MixByte(CallThis<uint32_t>(addr::cHitMP3Stream_Rewind, stream));
    MixStream(stream, true);
    std::memset(out, 0, sizeof out);
    const uint32_t got = CallThis<uint32_t>(addr::cHitMP3Stream_Read, stream, out, 2000);
    Mix(got);
    MixSamples(out, got < sizeof out ? got : 0);
    Mix(At<uint8_t>(CallThis<char*>(addr::cHitMP3Stream_Format, stream), 0) == 1 ? 1u : 0u);
    MixByte(CallThis<uint32_t>(addr::cHitMP3Stream_Flag, stream));
    MixByte(CallThis<uint32_t>(addr::cHitMP3Stream_SetFlag, stream, 0x105u));
    MixByte(Virtual<uint32_t>(stream, 0x44));
    CallThis<uint32_t>(addr::cHitMP3Stream_ClearFlag, stream);
    MixByte(Virtual<uint32_t>(stream, 0x44));
    MixByte(byAddress ? CallThis<uint32_t>(addr::cHitMP3Stream_Close, stream) : Virtual<uint32_t>(stream, 0x20));
    MixStream(stream, true);
    MixByte(CallThis<uint32_t>(addr::cHitMP3Stream_Close, stream));
    Mix(byAddress ? CallThis<uint32_t>(addr::cHitMP3Stream_Release, stream) : Virtual<uint32_t>(stream, 0x8));
}

void DriveMp3Stream(const std::string& musicPath, const std::vector<uint8_t>& music)
{
    const char* const folder = std::getenv("TEMP");
    const std::string base = std::string(folder ? folder : ".") + "\\opensc4-mp3-" + std::to_string(_getpid()) + "-";
    const std::size_t head = music.size() < 0x30000 ? music.size() : 0x30000;
    std::vector<uint8_t> tagged = {'I', 'D', '3', 3, 0, 0, 0, 0, 0, 0x14};
    tagged.resize(30, 0x20);
    const std::size_t skip = music.size() >= 10 ? MpCdecl<uint32_t>(addr::cMP3Decoder_Id3Size, music.data()) : 0;
    if (skip < head)
        tagged.insert(tagged.end(), music.begin() + static_cast<std::ptrdiff_t>(skip),
                      music.begin() + static_cast<std::ptrdiff_t>(head));
    std::vector<uint8_t> garbage(3000);
    for (std::size_t i = 0; i < garbage.size(); ++i)
        garbage[i] = static_cast<uint8_t>(i * 37 + 11);
    std::vector<uint8_t> cut(music.begin(), music.begin() + static_cast<std::ptrdiff_t>(head < 2000 ? head : 2000));
    const std::string files[] = {base + "tagged.mp3", base + "garbage.mp3", base + "empty.mp3", base + "cut.MP3",
                                 base + "text.txt"};
    MpWriteFile(files[0], tagged);
    MpWriteFile(files[1], garbage);
    MpWriteFile(files[2], {});
    MpWriteFile(files[3], cut);
    MpWriteFile(files[4], garbage);
    DriveMp3StreamFile(musicPath, false);
    DriveMp3StreamFile(musicPath, true);
    for (const std::string& file : files)
        DriveMp3StreamFile(file, file == files[3], file != files[1]);
    DriveMp3StreamFile(base + "missing.mp3", false);
    for (const std::string& file : files)
        std::remove(file.c_str());

    // By resource key: one that is not there, one whose record is no path.
    const uint32_t keys[][3] = {{0, 0x96A006B0, 0x0BADF00D}, {0, 0x96A006B0, 0xCBC905CD}};
    for (int way = 0; way < 2; ++way) {
        for (const auto& key : keys) {
            char* const stream = MpNewStream();
            MixByte(way ? CallThis<uint32_t>(addr::cHitMP3Stream_OpenKey, stream, key)
                        : Virtual<uint32_t>(stream, 0x14, key));
            MixStream(stream, false);
            Mix(Virtual<uint32_t>(stream, 0x8));
        }
    }

    // On memory of our own: the block from the bits word it held.
    static char memory[2][0x1104];
    std::memset(memory[0], 0xA5, sizeof memory[0]);
    std::memset(memory[1], 0x3C, sizeof memory[1]);
    char* const first = CallThis<char*>(addr::cHitMP3Stream_Construct, memory[0]);
    Mix(first == memory[0] ? 1u : 0u);
    MixStream(first, true);
    Mix(At<uint32_t>(first, 0) == addr::cHitMP3Stream_Vtable ? 1u : 0u);
    MixByte(CallThis<uint32_t>(addr::cHitMP3Stream_SetFlag, first, 1u));
    MixByte(CallThis<uint32_t>(addr::cHitMP3Stream_Flag, first));
    CallThis<void>(addr::cHitMP3Stream_Destruct, first);
    char* const second = CallThis<char*>(addr::cHitMP3Stream_Construct, memory[1]);
    MixStream(second, true);
    Mix(CallThis<char*>(addr::cHitMP3Stream_DeletingDestructor, second, 0u) == second ? 1u : 0u);
    // Release by its slots: a count of 2 down to 0 (the deleting destructor).
    char* const counted = MpNewStream();
    Virtual<uint32_t>(counted, 0x4);
    Mix(CallThis<uint32_t>(addr::cHitMP3Stream_Release, counted));
    Mix(Virtual<uint32_t>(counted, 0x8));
}

// The director and the maker.
void DriveMp3Object(const std::string& musicPath)
{
    void* const director = MpCdecl<void*>(addr::GZDirector_Get94AC43);
    Mix(director == reinterpret_cast<void*>(addr::cHitMP3Director_Static) ? 1u : 0u);
    Mix(MpCdecl<void*>(addr::GZDirector_Get94AC43) == director ? 1u : 0u);
    Mix(At<uint32_t>(reinterpret_cast<void*>(addr::cHitMP3Director_Guard), 0) & 1);
    static char own[0x4C];
    std::memset(own, 0, sizeof own);
    void* const made = CallThis<void*>(addr::cHitMP3Director_Construct, own);
    Mix(made == own ? 1u : 0u);
    Mix(At<uint32_t>(own, 0) == addr::cHitMP3Director_Vtable ? 1u : 0u);
    Mix(At<uint32_t>(own, 4) == addr::cHitMP3Director_UnknownVtable ? 1u : 0u);
    Mix(Virtual<uint32_t>(own, 0x34));
    Mix(CallThis<void*>(addr::cHitMP3Director_DeletingDestructor, own, 0u) == own ? 1u : 0u);

    // The maker by its factory.
    char* const maker = MpCdecl<char*>(addr::cHitMP3Maker_Create);
    Mix(maker ? 1u : 0u);
    Mix(At<uint32_t>(maker, 4));
    const uint32_t iids[] = {1, 0xFCEFF549, 0x3CEC2B47, 0};
    for (int way = 0; way < 2; ++way) {
        for (const uint32_t iid : iids) {
            void* out = nullptr;
            MixByte(way ? CallThis<uint32_t>(addr::cHitMP3Maker_QueryInterface, maker, iid, &out)
                        : Virtual<uint32_t>(maker, 0x0, iid, &out));
            Mix(out == maker ? 1u : out ? 2u : 0u);
            Mix(At<uint32_t>(maker, 4));
        }
    }
    // Streams made for a path: the music, a text file, a missing MP3 (only
    // the music's frame header is read, so only its fields are mixed).
    const std::string paths[] = {musicPath, musicPath + ".txt", musicPath + ".none.mp3"};
    for (int way = 0; way < 2; ++way) {
        for (const std::string& path : paths) {
            GameText name(path.c_str());
            void* stream = nullptr;
            MixByte(way ? CallThis<uint32_t>(addr::cHitMP3Maker_MakeStream, maker, name.get(), 1u, &stream)
                        : Virtual<uint32_t>(maker, 0xC, name.get(), 1u, &stream));
            Mix(stream ? 1u : 0u);
            if (!stream)
                continue;
            char* const made = static_cast<char*>(stream);
            Mix(At<uint32_t>(made, 4));
            Mix(At<uint8_t>(made, 0x10D0));
            Mix(At<uint8_t>(made, 0x10E8));
            Mix(At<uint8_t>(made, 0x10E9));
            if (path != musicPath) {
                Mix(Virtual<uint32_t>(stream, 0x8));
                continue;
            }
            MixStream(made, true);
            static uint8_t out[4608];
            const uint32_t got = Virtual<uint32_t>(stream, 0x38, out, static_cast<int32_t>(sizeof out));
            Mix(got);
            MixSamples(out, got <= sizeof out ? got : 0);
            Mix(Virtual<uint32_t>(stream, 0x8));
        }
    }
    {
        GameText name(paths[2].c_str());
        void* stream = nullptr;
        MixByte(CallThis<uint32_t>(addr::cHitMP3Maker_MakeStream, maker, name.get(), 0xDEADu, &stream));
        Mix(stream ? 1u : 0u);
    }
    while (At<uint32_t>(maker, 4) > 1)
        Mix(Virtual<uint32_t>(maker, 0x8));
    Mix(Virtual<uint32_t>(maker, 0x8));

    static char memory[2][0xC];
    char* const first = CallThis<char*>(addr::cHitMP3Maker_Construct, memory[0]);
    Mix(first == memory[0] ? 1u : 0u);
    Mix(At<uint32_t>(first, 0) == addr::cHitMP3Maker_Vtable ? 1u : 0u);
    Mix(At<uint32_t>(first, 4));
    CallThis<uint32_t>(addr::cHitMP3Maker_Destruct, first);
    char* const second = CallThis<char*>(addr::cHitMP3Maker_Construct, memory[1]);
    Mix(CallThis<char*>(addr::cHitMP3Maker_DeletingDestructor, second, 0u) == second ? 1u : 0u);
}

// Windows of our own for a root holder: the window (slot 0 its two faces
// and itself for 0x77 when its id is odd, 5 its id, 7 and 8 calls), the
// face 0x9A4527CB (slot 3 a value) and the face 0x5A356159 (3 a call, 5 a
// rectangle, 6 a point).
struct MpObject {
    void* const* vtable;
    uint32_t id;
    int32_t refs;
    MpObject* face9A;
    MpObject* face5A;
};

uint32_t __fastcall MpQueryInterface(MpObject* self, void*, uint32_t iid, void** out)
{
    Mix(0xA100);
    Mix(self->id);
    Mix(iid);
    MpObject* answer = nullptr;
    if (iid == 0x9A4527CB)
        answer = self->face9A;
    else if (iid == 0x5A356159)
        answer = self->face5A;
    else if (iid == 0x77 && (self->id & 1))
        answer = self;
    if (!answer)
        return 0;
    ++answer->refs;
    *out = answer;
    return 1;
}
uint32_t __fastcall MpAddRef(MpObject* self, void*)
{
    Mix(0xA101);
    Mix(self->id);
    return static_cast<uint32_t>(++self->refs);
}
uint32_t __fastcall MpRelease(MpObject* self, void*)
{
    Mix(0xA102);
    Mix(self->id);
    return static_cast<uint32_t>(--self->refs);
}
uint32_t __fastcall MpWindowId(MpObject* self, void*)
{
    Mix(0xA105);
    return self->id;
}
uint32_t __fastcall MpWindowCall(MpObject* self, void*, uint32_t argument)
{
    Mix(0xA107);
    Mix(self->id);
    Mix(argument);
    return 0x70 + self->id;
}
uint32_t __fastcall MpWindowEach(MpObject* self, void*, void* holder, uint32_t index, uint32_t iid, uint32_t argument)
{
    Mix(0xA108);
    Mix(self->id);
    Mix(holder ? 1u : 0u);
    Mix(index);
    Mix(iid);
    Mix(argument);
    return self->id != 4 ? 1u : 0u;
}
uint32_t __fastcall MpFaceValue(MpObject* self, void*)
{
    Mix(0xA203);
    Mix(self->id);
    return self->id * 16;
}
uint32_t __fastcall MpFaceCall(MpObject* self, void*, uint32_t argument)
{
    Mix(0xA303);
    Mix(self->id);
    Mix(argument);
    return 0x90;
}
uint32_t __fastcall MpFaceRect(MpObject* self, void*, int32_t* rect)
{
    Mix(0xA305);
    Mix(self->id);
    const int32_t k = static_cast<int32_t>(self->id & 0xFF);
    rect[0] = k * 10;
    rect[1] = k * 10;
    rect[2] = k * 10 + 15;
    rect[3] = k * 10 + 15;
    return (self->id & 0xFF) != 2 ? 1u : 0u;
}
uint32_t __fastcall MpFaceAt(MpObject* self, void*, const int32_t* point, uint32_t argument)
{
    Mix(0xA306);
    Mix(self->id);
    Mix(static_cast<uint32_t>(point[0]));
    Mix(static_cast<uint32_t>(point[1]));
    Mix(argument);
    return (self->id & 0xFF) == 3 || (self->id & 0xFF) == 5 ? 1u : 0u;
}

void* const* MpTable(int kind)
{
    static void* tables[3][10];
    void** const table = tables[kind];
    if (!table[0]) {
        for (int slot = 0; slot < 10; ++slot)
            table[slot] = reinterpret_cast<void*>(&GsTrap);
        table[0] = reinterpret_cast<void*>(&MpQueryInterface);
        table[1] = reinterpret_cast<void*>(&MpAddRef);
        table[2] = reinterpret_cast<void*>(&MpRelease);
        if (kind == 0) {
            table[5] = reinterpret_cast<void*>(&MpWindowId);
            table[7] = reinterpret_cast<void*>(&MpWindowCall);
            table[8] = reinterpret_cast<void*>(&MpWindowEach);
        } else if (kind == 1) {
            table[3] = reinterpret_cast<void*>(&MpFaceValue);
        } else {
            table[3] = reinterpret_cast<void*>(&MpFaceCall);
            table[5] = reinterpret_cast<void*>(&MpFaceRect);
            table[6] = reinterpret_cast<void*>(&MpFaceAt);
        }
    }
    return table;
}

// An allocator of our own for a vector (slot 3 a block, 4 its free).
struct MpAllocator {
    void* const* vtable;
};
void* __fastcall MpAllocate(MpAllocator*, void*, uint32_t bytes)
{
    Mix(0xA403);
    Mix(bytes);
    return std::malloc(bytes ? bytes : 1);
}
void __fastcall MpFree(MpAllocator*, void*, void* block)
{
    Mix(0xA404);
    Mix(block ? 1u : 0u);
    std::free(block);
}

void MixPointers(void* const* begin, void* const* end)
{
    Mix(static_cast<uint32_t>(end - begin));
    for (void* const* at = begin; at != end; ++at)
        Mix(*at ? static_cast<MpObject*>(*at)->id : 0u);
}

void DriveMp3Root()
{
    std::vector<MpObject*> objects;
    auto make = [&](int kind, uint32_t id) {
        MpObject* const object = new MpObject{MpTable(kind), id, 0, nullptr, nullptr};
        objects.push_back(object);
        return object;
    };
    char* const root = MpCdecl<char*>(addr::cGZUIScriptDirector_CreateRoot);
    Mix(root ? 1u : 0u);
    if (!root)
        return;
    Mix(Virtual<uint32_t>(root, 0x4));
    const uint32_t iids[] = {1, 0x1A35608A, 0xFA35615F, 0x9A4527CB, 0};
    for (int way = 0; way < 2; ++way) {
        for (const uint32_t iid : iids) {
            void* out = nullptr;
            MixByte(way ? CallThis<uint32_t>(addr::cGZUIScriptRoot_QueryInterface, root, iid, &out)
                        : Virtual<uint32_t>(root + 4, 0x0, iid, &out));
            Mix(out == root ? 1u : out == root + 4 ? 2u : out ? 3u : 0u);
            if (out)
                Mix(CallThis<uint32_t>(addr::cGZUIScriptRoot_Release, root));
        }
    }
    auto all = [&](bool byAddress) {
        Mix(CallThis<uint32_t>(addr::cGZUIScriptRoot_Count, root));
        Mix(Virtual<uint32_t>(root, 0x28));
        for (uint32_t id = 0; id <= 7; ++id) {
            void* const window = byAddress ? CallThis<void*>(addr::cGZUIScriptRoot_ById, root, id)
                                           : Virtual<void*>(root, 0x14, id);
            Mix(window ? static_cast<MpObject*>(window)->id : 0xFFu);
            Mix(byAddress ? CallThis<uint32_t>(addr::cGZUIScriptRoot_WindowById, root, id)
                          : Virtual<uint32_t>(root, 0x1C, id));
        }
        for (uint32_t index = 0; index <= 8; ++index) {
            void* const window = byAddress ? CallThis<void*>(addr::cGZUIScriptRoot_At, root, index)
                                           : Virtual<void*>(root, 0x18, index);
            Mix(window ? static_cast<MpObject*>(window)->id : 0xFFu);
            Mix(byAddress ? CallThis<uint32_t>(addr::cGZUIScriptRoot_WindowAt, root, index)
                          : Virtual<uint32_t>(root, 0x20, index));
        }
        if (byAddress)
            CallThis<uint32_t>(addr::cGZUIScriptRoot_Each, root, 0x55u);
        else
            Virtual<uint32_t>(root, 0x24, 0x56u);
        for (const uint32_t iid : {0x77u, 0x9A4527CBu, 0x5A356159u, 0u}) {
            MixByte(byAddress ? CallThis<uint32_t>(addr::cGZUIScriptRoot_All, root, iid, 0x99u)
                              : Virtual<uint32_t>(root, 0x2C, iid, 0x98u));
        }
        MixByte(byAddress ? CallThis<uint32_t>(addr::cGZUIScriptRoot_PartEach, root + 4, 0x66u)
                          : Virtual<uint32_t>(root + 4, 0xC, 0x67u));
        const int32_t points[][2] = {{12, 12}, {31, 31}, {40, 40}, {25, 25}, {55, 54}, {100, 100}, {-1, 5}, {10, 25}};
        for (const auto& point : points) {
            MixByte(byAddress ? CallThis<uint32_t>(addr::cGZUIScriptRoot_PartAt, root + 4, point, 0x44u)
                              : Virtual<uint32_t>(root + 4, 0x10, point, 0x45u));
        }
    };
    all(true);
    all(false);
    // Windows added (the game's slot 4; the vector grows through 0x0094BF3B).
    for (uint32_t id = 1; id <= 6; ++id) {
        MpObject* const window = make(0, id);
        if (id != 6) {
            window->face9A = make(1, 0x100 + id);
            window->face5A = make(2, 0x200 + id);
        }
        MixByte(Virtual<uint32_t>(root, 0x10, window));
    }
    MixByte(Virtual<uint32_t>(root, 0x10, static_cast<void*>(nullptr)));
    MixPointers(At<void**>(root, 8), At<void**>(root, 0xC));
    all(true);
    all(false);
    // The vector's insert with room: at its second place.
    MpObject* const extra = make(0, 7);
    extra->face5A = make(2, 0x205);
    void* const value = extra;
    void** const begin = At<void**>(root, 8);
    Mix(At<void**>(root, 0x10) != At<void**>(root, 0xC) ? 1u : 0u);
    CallThis<void*>(addr::cGZUIScriptRoot_VectorInsert, root + 8, begin + 1, &value);
    MixPointers(At<void**>(root, 8), At<void**>(root, 0xC));
    all(true);

    // Rectangles.
    const int32_t rects[][4] = {{0, 0, 10, 10}, {-5, -5, 5, 5}, {10, 10, 0, 0}, {3, 4, 3, 4}};
    const int32_t probes[][2] = {{0, 0}, {10, 10}, {11, 10}, {5, -5}, {3, 4}, {-6, 0}, {4, 3}};
    for (const auto& rect : rects) {
        for (const auto& probe : probes)
            Mix(CallThis<uint32_t>(addr::GZRect_Contains, const_cast<int32_t*>(rect), probe));
    }

    // The vector's copies and insert on a vector of our own.
    MpObject* const items[5] = {make(0, 11), make(0, 12), make(0, 13), make(0, 14), make(0, 15)};
    void* source[5];
    for (int i = 0; i < 5; ++i)
        source[i] = items[i];
    void* target[8] = {};
    void** end = CallThis<void**>(addr::cGZUIScriptRoot_VectorCopy, root + 8, source, source + 5, target + 1);
    Mix(static_cast<uint32_t>(end - target));
    MixPointers(target, target + 8);
    // A null destination is skipped but still moves on (one pointer only:
    // the next would be written at 4).
    end = CallThis<void**>(addr::cGZUIScriptRoot_VectorCopy, root + 8, source, source + 1,
                           static_cast<void**>(nullptr));
    Mix(static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(end)));
    end = CallThis<void**>(addr::cGZUIScriptRoot_VectorCopy, root + 8, source, source, target);
    Mix(static_cast<uint32_t>(end - target));
    end = CallThis<void**>(addr::cGZUIScriptRoot_VectorCopyBackward, root + 8, source, source + 4, target + 8);
    Mix(static_cast<uint32_t>(end - target));
    MixPointers(target, target + 8);
    end = CallThis<void**>(addr::cGZUIScriptRoot_VectorCopyBackward, root + 8, source, source, target + 8);
    Mix(static_cast<uint32_t>(end - target));
    static void* const allocatorTable[5] = {reinterpret_cast<void*>(&GsTrap), reinterpret_cast<void*>(&GsTrap),
                                            reinterpret_cast<void*>(&GsTrap), reinterpret_cast<void*>(&MpAllocate),
                                            reinterpret_cast<void*>(&MpFree)};
    MpAllocator allocator{allocatorTable};
    struct {
        void** begin;
        void** end;
        void** capacity;
        uint32_t unused;
        MpAllocator* allocator;
    } vector{nullptr, nullptr, nullptr, 0, &allocator};
    const int places[] = {0, 0, 1, 2, 4, 0, 3, 6};
    for (int i = 0; i < 8; ++i) {
        void* const item = items[i % 5];
        void** const at = vector.begin + (places[i] <= vector.end - vector.begin ? places[i] : 0);
        CallThis<void*>(addr::cGZUIScriptRoot_VectorInsert, &vector, at, &item);
        Mix(static_cast<uint32_t>(vector.capacity - vector.begin));
        MixPointers(vector.begin, vector.end);
    }
    std::free(vector.begin);

    // Released to nothing (the game's deleting destructor releases each).
    while (At<uint32_t>(root, 0x20) > 1)
        Mix(Virtual<uint32_t>(root, 0x8));
    Mix(CallThis<uint32_t>(addr::cGZUIScriptRoot_Release, root));
    for (MpObject* object : objects) {
        Mix(static_cast<uint32_t>(object->refs));
        delete object;
    }
}

// OPENSC4_TEST_MP3=<day>: part u10's functions through their slots and
// addresses. Every result goes into the fingerprint's `mp` part.
void DriveMp3(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_MP3") || !game::IsBuild641())
        return;
    g_mpOn = true;
    uint32_t* const saved = g_into;
    g_into = &g_mpHash;
    const std::string musicPath = MpGameFolder() + "\\Radio\\Stations\\Mayor\\Music\\SC4ep1_01_M.mp3";
    const std::vector<uint8_t> music = MpReadFile(musicPath);
    Mix(music.empty() ? 0u : 1u);
    DriveMp3Decode(music);
    MpStage("decode");
    DriveMp3Stream(musicPath, music);
    MpStage("stream");
    DriveMp3Object(musicPath);
    MpStage("object");
    DriveMp3Root();
    MpStage("root");
    g_into = saved;
    log::Write("autotest: mp3 driven (hash %08X, music %u bytes)", g_mpHash, static_cast<unsigned>(music.size()));
}

void MpFingerprint(cISC4City*, std::string& line)
{
    if (!g_mpOn)
        return;
    char text[32];
    std::snprintf(text, sizeof text, " | mp %08X", g_mpHash);
    line += text;
}

const FingerprintPart kMpFingerprint{"mp3", 4, &MpFingerprint};
const DayHook kMpDrive{"mp3", 4, &DriveMp3};


// Part u11: the rest of the UI script's root holder 0x5A354595 and the
// LEGACY reader 0xFA450242 (OPENSC4_TEST_LEGACY). Real root holders and
// readers made by the game's factories and constructors (and on memory of
// our own), windows, parents and values of our own where they take them
// (each hashes every call it gets). Every result goes into the hash;
// addresses never do.

bool g_lgOn = false;
uint32_t g_lgHash = 2166136261u;

void LgStage(const char* name) { log::Write("autotest: legacy: %s %08X", name, g_lgHash); }

// The root holder's life: made, windows added (the vector grows past its
// first 8), pushed and erased by address, released to nothing; on memory
// of our own through the constructor and both destructors; a vector of our
// own through its constructor, push and erase.
void DriveLgRoot()
{
    std::vector<MpObject*> objects;
    auto make = [&](uint32_t id) {
        MpObject* const object = new MpObject{MpTable(0), id, 0, nullptr, nullptr};
        objects.push_back(object);
        return object;
    };
    char* const root = MpCdecl<char*>(addr::cGZUIScriptDirector_CreateRoot);
    Mix(root ? 1u : 0u);
    if (!root)
        return;
    Mix(At<uint32_t>(root, 0) == addr::cGZUIScriptRoot_Vtable ? 1u : 0u);
    Mix(At<uint32_t>(root, 4) == addr::cGZUIScriptRoot_PartVtable ? 1u : 0u);
    Mix(static_cast<uint32_t>(At<char*>(root, 0x10) - At<char*>(root, 8)));
    Mix(At<uint32_t>(root, 0x20));
    Mix(At<uint32_t>(At<char*>(root, 0x18), 0) == addr::cGZUIScriptRoot_AllocatorVtable ? 1u : 0u);
    Mix(At<uint32_t>(At<char*>(root, 0x18), 4));
    Virtual<uint32_t>(root, 0x4);
    for (uint32_t id = 1; id <= 12; ++id) {
        MpObject* const window = make(id);
        MixByte(id & 1 ? CallThis<uint32_t>(addr::cGZUIScriptRoot_Add, root, window)
                       : Virtual<uint32_t>(root, 0x10, window));
        Mix(static_cast<uint32_t>(At<char*>(root, 0x10) - At<char*>(root, 8)));
    }
    MixByte(CallThis<uint32_t>(addr::cGZUIScriptRoot_Add, root, static_cast<void*>(nullptr)));
    MixByte(Virtual<uint32_t>(root, 0x10, static_cast<void*>(nullptr)));
    MixPointers(At<void**>(root, 8), At<void**>(root, 0xC));
    // Pushed by address (held by us), with room and without.
    for (uint32_t id = 20; id < 26; ++id) {
        MpObject* const window = make(id);
        ++window->refs;
        void* const value = window;
        CallThis<void*>(addr::cGZUIScriptRoot_VectorPush, root + 8, &value);
        Mix(static_cast<uint32_t>(At<char*>(root, 0x10) - At<char*>(root, 8)));
    }
    MixPointers(At<void**>(root, 8), At<void**>(root, 0xC));
    // Erased: two in the middle, none, the last (each released by us).
    void** const begin = At<void**>(root, 8);
    MpObject* const gone[3] = {static_cast<MpObject*>(begin[2]), static_cast<MpObject*>(begin[3]), nullptr};
    void** at = CallThis<void**>(addr::cGZUIScriptRoot_VectorErase, root + 8, begin + 2, begin + 4);
    Mix(static_cast<uint32_t>(at - begin));
    at = CallThis<void**>(addr::cGZUIScriptRoot_VectorErase, root + 8, begin + 5, begin + 5);
    Mix(static_cast<uint32_t>(at - begin));
    void** const last = At<void**>(root, 0xC) - 1;
    MpObject* const lastOne = static_cast<MpObject*>(*last);
    at = CallThis<void**>(addr::cGZUIScriptRoot_VectorErase, root + 8, last, last + 1);
    Mix(static_cast<uint32_t>(at - begin));
    MixPointers(At<void**>(root, 8), At<void**>(root, 0xC));
    --gone[0]->refs;
    --gone[1]->refs;
    --lastOne->refs;
    Mix(Virtual<uint32_t>(root, 0x28));
    // Released to nothing: the deleting destructor releases every window.
    while (At<uint32_t>(root, 0x20) > 1)
        Mix(Virtual<uint32_t>(root, 0x8));
    Mix(Virtual<uint32_t>(root, 0x8));
    for (MpObject* object : objects)
        Mix(static_cast<uint32_t>(object->refs));

    // On memory of our own.
    static char memory[2][0x24];
    for (int way = 0; way < 2; ++way) {
        std::memset(memory[way], 0x5A, sizeof memory[way]);
        char* const own = CallThis<char*>(addr::cGZUIScriptRoot_Construct, memory[way]);
        Mix(own == memory[way] ? 1u : 0u);
        Mix(At<uint32_t>(own, 0) == addr::cGZUIScriptRoot_Vtable ? 1u : 0u);
        Mix(At<uint32_t>(own, 0x14));
        Mix(At<uint32_t>(own, 0x1C));
        Mix(At<uint32_t>(own, 0x20));
        for (uint32_t id = 30; id < 33; ++id)
            MixByte(CallThis<uint32_t>(addr::cGZUIScriptRoot_Add, own, make(id + 10 * way)));
        if (way == 0)
            CallThis<void>(addr::cGZUIScriptRoot_Destruct, own);
        else
            Mix(CallThis<char*>(addr::cGZUIScriptRoot_DeletingDestructor, own, 0u) == own ? 1u : 0u);
        Mix(At<uint32_t>(own, 0) == addr::cGZUIScriptRoot_Vtable ? 1u : 0u);
    }

    // A vector of our own.
    static char vector[0x18];
    std::memset(vector, 0x33, sizeof vector);
    Mix(CallThis<char*>(addr::cGZUIScriptRoot_VectorConstruct, vector) == vector ? 1u : 0u);
    Mix(At<uint32_t>(vector, 0xC));
    Mix(At<uint32_t>(vector, 0x14));
    Mix(static_cast<uint32_t>(At<char*>(vector, 8) - At<char*>(vector, 0)));
    MpObject* const items[3] = {make(51), make(52), make(53)};
    for (int i = 0; i < 11; ++i) {
        void* const value = items[i % 3];
        CallThis<void*>(addr::cGZUIScriptRoot_VectorPush, vector, &value);
        Mix(static_cast<uint32_t>(At<char*>(vector, 8) - At<char*>(vector, 0)));
    }
    MixPointers(At<void**>(vector, 0), At<void**>(vector, 4));
    void** const first = At<void**>(vector, 0);
    Mix(static_cast<uint32_t>(CallThis<void**>(addr::cGZUIScriptRoot_VectorErase, vector, first, first + 3) - first));
    MixPointers(At<void**>(vector, 0), At<void**>(vector, 4));
    Mix(static_cast<uint32_t>(
        CallThis<void**>(addr::cGZUIScriptRoot_VectorErase, vector, first, At<void**>(vector, 4)) - first));
    MixPointers(At<void**>(vector, 0), At<void**>(vector, 4));
    CallThis<void>(addr::SC4AttrVector_Destruct, vector);
    for (MpObject* object : objects) {
        Mix(static_cast<uint32_t>(object->refs));
        delete object;
    }
}

// Objects of our own for a LEGACY reader: a window (slot 0 its face
// 0x9A4527CB, 5, 47, 54, 63 and 64 calls, 11 an object, 15 a window), the
// face (slot 3 its value) and values; every call hashed.
struct LgObject {
    void* const* vtable;
    uint32_t id;
    int32_t refs;
    LgObject* face;
    LgObject* value;
    LgObject* other;
};

uint32_t __fastcall LgObjQueryInterface(LgObject* self, void*, uint32_t iid, void** out)
{
    Mix(0xB100);
    Mix(self->id);
    Mix(iid);
    if (iid != 0x9A4527CB || !self->face)
        return 0;
    ++self->face->refs;
    *out = self->face;
    return 1;
}
uint32_t __fastcall LgObjAddRef(LgObject* self, void*)
{
    Mix(0xB101);
    Mix(self->id);
    return static_cast<uint32_t>(++self->refs);
}
uint32_t __fastcall LgObjRelease(LgObject* self, void*)
{
    Mix(0xB102);
    Mix(self->id);
    return static_cast<uint32_t>(--self->refs);
}
void* __fastcall LgObjValue(LgObject* self, void*)
{
    Mix(0xB103);
    Mix(self->id);
    return self->value;
}
void* __fastcall LgObjOther(LgObject* self, void*)
{
    Mix(0xB10B);
    Mix(self->id);
    return self->other;
}
uint32_t __fastcall LgObjTakes(LgObject* self, void*, LgObject* window)
{
    Mix(0xB10F);
    Mix(self->id);
    Mix(window ? window->id : 0xFFu);
    return 0x3C;
}
template <uint32_t Slot>
uint32_t __fastcall LgObjCall0(LgObject* self, void*)
{
    Mix(0xB000 + Slot);
    Mix(self->id);
    return Slot * 0x100 + self->id;
}
template <uint32_t Slot>
uint32_t __fastcall LgObjCall1(LgObject* self, void*, uint32_t argument)
{
    Mix(0xB000 + Slot);
    Mix(self->id);
    Mix(argument);
    return Slot * 0x100 + self->id + (argument << 16);
}

void* const* LgObjTable()
{
    static void* table[65];
    if (!table[0]) {
        for (void*& slot : table)
            slot = reinterpret_cast<void*>(&GsTrap);
        table[0] = reinterpret_cast<void*>(&LgObjQueryInterface);
        table[1] = reinterpret_cast<void*>(&LgObjAddRef);
        table[2] = reinterpret_cast<void*>(&LgObjRelease);
        table[3] = reinterpret_cast<void*>(&LgObjValue);
        table[5] = reinterpret_cast<void*>(&LgObjCall0<5>);
        table[11] = reinterpret_cast<void*>(&LgObjOther);
        table[15] = reinterpret_cast<void*>(&LgObjTakes);
        table[47] = reinterpret_cast<void*>(&LgObjCall1<47>);
        table[54] = reinterpret_cast<void*>(&LgObjCall1<54>);
        table[63] = reinterpret_cast<void*>(&LgObjCall0<63>);
        table[64] = reinterpret_cast<void*>(&LgObjCall1<64>);
    }
    return table;
}

// A reader's fields (not its pointers).
void MixLegacy(char* reader)
{
    Mix(At<uint32_t>(reader, 0) == addr::cGZUIScriptLegacy_Vtable ? 1u : 0u);
    Mix(At<uint32_t>(reader, 4) == addr::cGZUIScriptLegacy_ChildVtable ? 1u : 0u);
    Mix(At<uint32_t>(reader, 8) == addr::cGZUIScriptLegacy_WindowVtable ? 1u : 0u);
    Mix(At<uint32_t>(reader, 0xC));
    for (std::size_t at = 0x10; at <= 0x18; at += 4) {
        LgObject* const held = At<LgObject*>(reader, at);
        Mix(held ? held->id : 0u);
    }
    Mix(At<uint8_t>(reader, 0x1C));
    Mix(At<void*>(reinterpret_cast<void*>(addr::cGZUIScriptLegacy_Table), 0) ? 1u : 0u);
}

void DriveLgLegacy()
{
    std::vector<LgObject*> objects;
    auto make = [&](uint32_t id) {
        LgObject* const object = new LgObject{LgObjTable(), id, 0, nullptr, nullptr, nullptr};
        objects.push_back(object);
        return object;
    };
    void*& table = *reinterpret_cast<void**>(addr::cGZUIScriptLegacy_Table);
    auto run = [&](int round) {
        const bool byAddress = round & 1;
        char* const reader = MpCdecl<char*>(addr::cGZUIScriptDirector_CreateLegacy);
        Mix(reader ? 1u : 0u);
        if (!reader)
            return;
        MixLegacy(reader);
        Mix(byAddress ? CallThis<uint32_t>(addr::cGZUIScriptLegacy_AddRef, reader) : Virtual<uint32_t>(reader, 0x4));
        const uint32_t iids[] = {1, 0x1A356083, 0x5A356159, 0x9A4527CB, 0x5A354595, 0};
        for (const uint32_t iid : iids) {
            void* out = nullptr;
            const uint32_t got = byAddress ? CallThis<uint32_t>(addr::cGZUIScriptLegacy_QueryInterface, reader, iid, &out)
                                           : Virtual<uint32_t>(reader + 8 * (iid & 1), 0x0, iid, &out);
            Mix(got & 0xFF ? got & 0xFF : got);
            Mix(out == reader ? 1u : out == reader + 4 ? 2u : out == reader + 8 ? 3u : out ? 4u : 0u);
            if (out)
                Mix(byAddress ? CallThis<uint32_t>(addr::cGZUIScriptLegacy_Release, reader)
                              : Virtual<uint32_t>(static_cast<char*>(out), 0x8));
        }
        Mix((byAddress ? CallThis<void*>(addr::cGZUIScriptLegacy_TableSlot, reader) : Virtual<void*>(reader, 0x10)) ==
                    table
                ? 1u
                : 0u);
        // No window yet.
        Mix(byAddress ? CallThis<uint32_t>(addr::cGZUIScriptLegacy_Window63, reader) : Virtual<uint32_t>(reader, 0x14));
        if (byAddress) {
            MixByte(CallThis<uint32_t>(addr::cGZUIScriptLegacy_Window54, reader + 4, 7u));
            MixByte(CallThis<uint32_t>(addr::cGZUIScriptLegacy_Window47, reader + 4, 8u));
        } else {
            MixByte(Virtual<uint32_t>(reader + 4, 0x10, 7u));
            MixByte(Virtual<uint32_t>(reader + 4, 0x14, 8u));
        }
        // Parents: none, one without the face, a face without a value, with
        // values (the second replaces the first).
        LgObject* const plain = make(0x10 + round);
        LgObject* const bare = make(0x20 + round);
        bare->face = make(0x30 + round);
        LgObject* const full = make(0x40 + round);
        full->face = make(0x50 + round);
        full->face->value = make(0x60 + round);
        LgObject* const again = make(0x70 + round);
        again->face = make(0x80 + round);
        again->face->value = make(0x90 + round);
        for (LgObject* const parent : {static_cast<LgObject*>(nullptr), plain, bare, full, again}) {
            MixByte(byAddress ? CallThis<uint32_t>(addr::cGZUIScriptLegacy_SetParent, reader, parent)
                              : Virtual<uint32_t>(reader, 0x24, parent));
            MixLegacy(reader);
        }
        // Windows and objects: set, the same again, another, none, set.
        LgObject* const windows[] = {make(0xA0 + round), make(0xB0 + round)};
        LgObject* const things[] = {make(0xC0 + round), make(0xD0 + round)};
        for (LgObject* const window : {windows[0], windows[0], windows[1], static_cast<LgObject*>(nullptr), windows[0]}) {
            // The same again leaves the caller's EAX: not mixed.
            const bool same = At<LgObject*>(reader, 0x10) == window;
            const uint32_t got = byAddress ? CallThis<uint32_t>(addr::cGZUIScriptLegacy_SetWindow, reader + 8, window)
                                           : Virtual<uint32_t>(reader + 8, 0x14, window);
            Mix(same ? 0xFFFFu : got);
            MixLegacy(reader);
        }
        for (LgObject* const thing : {things[0], things[1], things[1], static_cast<LgObject*>(nullptr), things[1]}) {
            const bool same = At<LgObject*>(reader, 0x18) == thing;
            const uint32_t got = byAddress ? CallThis<uint32_t>(addr::cGZUIScriptLegacy_SetObject, reader + 8, thing)
                                           : Virtual<uint32_t>(reader + 8, 0x18, thing);
            Mix(same ? 0xFFFFu : got);
            MixLegacy(reader);
        }
        // The window's calls.
        Mix(byAddress ? CallThis<uint32_t>(addr::cGZUIScriptLegacy_Window63, reader) : Virtual<uint32_t>(reader, 0x14));
        Mix(byAddress ? CallThis<uint32_t>(addr::cGZUIScriptLegacy_Window64, reader, 0x41u)
                      : Virtual<uint32_t>(reader, 0x18, 0x42u));
        MixByte(byAddress ? CallThis<uint32_t>(addr::cGZUIScriptLegacy_Window54, reader + 4, 0x43u)
                          : Virtual<uint32_t>(reader + 4, 0x10, 0x44u));
        MixByte(byAddress ? CallThis<uint32_t>(addr::cGZUIScriptLegacy_Window47, reader + 4, 0x45u)
                          : Virtual<uint32_t>(reader + 4, 0x14, 0x46u));
        // The flag: by the primary slot, the face's and the address.
        const uint32_t flags[] = {0x101u, 0u, 0x2FEu, round < 2 ? 1u : 0u};
        for (int i = 0; i < 4; ++i) {
            uint32_t got;
            if (i == 3 || byAddress)
                got = CallThis<uint32_t>(addr::cGZUIScriptLegacy_SetFlag, reader, flags[i]);
            else if (i == 1)
                got = Virtual<uint32_t>(reader + 8, 0x10, flags[i]);
            else
                got = Virtual<uint32_t>(reader, 0x1C, flags[i]);
            MixByte(got);
            Mix(At<uint8_t>(reader, 0x1C));
        }
        // The window's object at the end (round 0 one, round 1 none).
        windows[0]->other = round == 0 ? make(0xE0) : nullptr;
        // Released to nothing (the deleting destructor).
        while (At<uint32_t>(reader, 0xC) > 1)
            Mix(Virtual<uint32_t>(reader, 0x8));
        Mix(byAddress ? CallThis<uint32_t>(addr::cGZUIScriptLegacy_Release, reader) : Virtual<uint32_t>(reader, 0x8));
        Mix(table ? 1u : 0u);
    };
    // A reader kept for the whole test, so the shared table stays.
    char* const keep = MpCdecl<char*>(addr::cGZUIScriptDirector_CreateLegacy);
    if (keep)
        Virtual<uint32_t>(keep, 0x4);
    for (int round = 0; round < 4; ++round)
        run(round);

    // On memory of our own: the destructor, then the deleting destructor.
    static char memory[2][0x20];
    for (int way = 0; way < 2; ++way) {
        std::memset(memory[way], 0x6B, sizeof memory[way]);
        char* const own = CallThis<char*>(addr::cGZUIScriptLegacy_Construct, memory[way]);
        Mix(own == memory[way] ? 1u : 0u);
        MixLegacy(own);
        LgObject* const window = make(0xF0 + way);
        window->other = make(0xF2 + way);
        CallThis<uint32_t>(addr::cGZUIScriptLegacy_SetWindow, own + 8, window);
        CallThis<uint32_t>(addr::cGZUIScriptLegacy_SetObject, own + 8, make(0xF4 + way));
        CallThis<uint32_t>(addr::cGZUIScriptLegacy_SetFlag, own, 1u);
        if (way == 0)
            CallThis<uint32_t>(addr::cGZUIScriptLegacy_Destruct, own);
        else
            Mix(CallThis<char*>(addr::cGZUIScriptLegacy_DeletingDestructor, own, 0u) == own ? 1u : 0u);
        MixLegacy(own);
    }
    if (keep)
        Mix(Virtual<uint32_t>(keep, 0x8));
    for (LgObject* object : objects) {
        Mix(static_cast<uint32_t>(object->refs));
        delete object;
    }
}

// OPENSC4_TEST_LEGACY=<day>: part u11's functions through their slots and
// addresses. Every result goes into the fingerprint's `lg` part.
void DriveLegacy(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_LEGACY") || !game::IsBuild641())
        return;
    g_lgOn = true;
    uint32_t* const saved = g_into;
    g_into = &g_lgHash;
    DriveLgRoot();
    LgStage("root");
    DriveLgLegacy();
    LgStage("legacy");
    g_into = saved;
    log::Write("autotest: legacy driven (hash %08X)", g_lgHash);
}

void LgFingerprint(cISC4City*, std::string& line)
{
    if (!g_lgOn)
        return;
    char text[32];
    std::snprintf(text, sizeof text, " | lg %08X", g_lgHash);
    line += text;
}

const FingerprintPart kLgFingerprint{"legacy", 4, &LgFingerprint};
const DayHook kLgDrive{"legacy", 4, &DriveLegacy};


// Part u12: the readers of a window's script properties
// (OPENSC4_TEST_PROPREAD). A vector of pairs of our own with properties of
// our own (text ones answer 0x800E and put their text in the string through
// its slot 3, the characters and their count; each hashes every call it gets), strings made by the game's
// constructor; the four called by address with their registers set. Every
// result goes into the hash; addresses never do.

bool g_prOn = false;
uint32_t g_prHash = 2166136261u;

void PrStage(const char* name) { log::Write("autotest: propread: %s %08X", name, g_prHash); }

struct PrProperty {
    void** vtable;
    uint32_t kind;
    const char* text;
};

uint32_t __fastcall PrKind(PrProperty* self, void*)
{
    Mix(0x30000u | self->kind);
    return self->kind;
}

uint32_t __fastcall PrText(PrProperty* self, void*, void* string)
{
    Mix(0x37000u);
    const uint32_t length = static_cast<uint32_t>(std::strlen(self->text));
    Virtual<uint32_t>(string, 0xC, self->text, length);
    return 0x100u | (length & 0xFF);
}

void** PrTable()
{
    static void* table[56] = {};
    table[3] = reinterpret_cast<void*>(&PrKind);
    table[55] = reinterpret_cast<void*>(&PrText);
    return table;
}

// The game's address called with EBX, ESI and EDI set and four stack
// arguments (cdecl; the readers take fewer and leave the rest).
__declspec(naked) uint32_t __cdecl PrCall(std::uintptr_t, void*, void*, void*, uint32_t, uint32_t, uint32_t,
                                          uint32_t)
{
    __asm push ebx
    __asm push esi
    __asm push edi
    __asm push ebp
    __asm mov ebp, esp
    __asm mov ebx, [ebp + 24]
    __asm mov esi, [ebp + 28]
    __asm mov edi, [ebp + 32]
    __asm push dword ptr [ebp + 48]
    __asm push dword ptr [ebp + 44]
    __asm push dword ptr [ebp + 40]
    __asm push dword ptr [ebp + 36]
    __asm call dword ptr [ebp + 20]
    __asm mov esp, ebp
    __asm pop ebp
    __asm pop edi
    __asm pop esi
    __asm pop ebx
    __asm ret
}

uint32_t PrArg(const void* pointer) { return static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(pointer)); }

void PrMixText(char* string)
{
    const char* const begin = At<const char*>(string, 4);
    const char* const end = At<const char*>(string, 8);
    Mix(static_cast<uint32_t>(end - begin));
    for (const char* c = begin; c < end; ++c)
        Mix(static_cast<uint8_t>(*c));
}

void DrivePropRead()
{
    PrProperty properties[] = {
        {PrTable(), 0x800E, "(1,2,3)"},          {PrTable(), 5, "(9,9,9)"},
        {PrTable(), 0x800E, "(7,-8)"},           {PrTable(), 0x800E, "(10,20,30,40)"},
        {PrTable(), 0x800E, "(1,2"},             {PrTable(), 0x800E, "garbage"},
        {PrTable(), 0x800E, "(300,-1,256)"},     {PrTable(), 0x800E, "(5,6,7,8,9)"},
        {PrTable(), 0x800E, ""},                 {PrTable(), 0x800E, "( 4 , 5 , 6 )"},
    };
    struct Pair {
        uint32_t key;
        PrProperty* property;
    };
    Pair pairs[] = {
        {1, &properties[0]}, {2, nullptr},         {3, &properties[1]}, {3, &properties[2]},
        {4, &properties[3]}, {5, &properties[4]},  {6, &properties[5]}, {7, &properties[6]},
        {8, &properties[7]}, {9, &properties[8]},  {10, &properties[9]}, {1, &properties[2]},
    };
    char* vector[3] = {reinterpret_cast<char*>(pairs), reinterpret_cast<char*>(pairs + std::size(pairs)), nullptr};
    char* emptyVector[3] = {reinterpret_cast<char*>(pairs), reinterpret_cast<char*>(pairs), nullptr};

    alignas(4) char string[0x14];
    CallThis<void*>(addr::SC4String_SC4String, string);
    for (uint32_t key = 0; key <= 11; ++key) {
        MixByte(PrCall(addr::cGZUIScriptProps_FindText, string, nullptr, vector, key, 0, 0, 0));
        PrMixText(string);
    }
    MixByte(PrCall(addr::cGZUIScriptProps_FindText, string, nullptr, emptyVector, 1, 0, 0, 0));
    PrMixText(string);
    if (char* const begin = At<char*>(string, 4))
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(begin);
    At<std::uintptr_t>(string, 0) = addr::SC4String_Vtable;
    PrStage("find");

    for (uint32_t key = 0; key <= 11; ++key) {
        uint8_t bytes[3] = {0xAA, 0xBB, 0xCC};
        MixByte(PrCall(addr::cGZUIScriptProps_ReadBytes, nullptr, nullptr, vector, key, PrArg(&bytes[0]),
                       PrArg(&bytes[1]), PrArg(&bytes[2])));
        Mix(bytes[0] | bytes[1] << 8 | bytes[2] << 16);
        int32_t pair[2] = {-77, -78};
        MixByte(PrCall(addr::cGZUIScriptProps_ReadPair, nullptr, nullptr, vector, key, PrArg(&pair[0]),
                       PrArg(&pair[1]), 0));
        Mix(static_cast<uint32_t>(pair[0]));
        Mix(static_cast<uint32_t>(pair[1]));
        int32_t rect[4] = {-1, -2, -3, -4};
        MixByte(PrCall(addr::cGZUIScriptProps_ReadRect, nullptr, rect, vector, key, 0, 0, 0));
        for (int32_t value : rect)
            Mix(static_cast<uint32_t>(value));
    }
    PrStage("scan");
}

// OPENSC4_TEST_PROPREAD=<day>: part u12's readers by address. Every result
// goes into the fingerprint's `pr` part.
void DrivePropReadDay(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_PROPREAD") || !game::IsBuild641())
        return;
    g_prOn = true;
    uint32_t* const saved = g_into;
    g_into = &g_prHash;
    DrivePropRead();
    g_into = saved;
    log::Write("autotest: propread driven (hash %08X)", g_prHash);
}

void PrFingerprint(cISC4City*, std::string& line)
{
    if (!g_prOn)
        return;
    char text[32];
    std::snprintf(text, sizeof text, " | pr %08X", g_prHash);
    line += text;
}

const FingerprintPart kPrFingerprint{"propread", 4, &PrFingerprint};
const DayHook kPrDrive{"propread", 4, &DrivePropReadDay};


// Part u12, slice 2: the makers, loaders and lookups of a window's script
// properties (OPENSC4_TEST_PROPMAKE). The ten called by address with their
// registers set (and checked kept after), on a window of our own (its slot
// 114 hashes the key and the value it gets, then destroys the value), the
// UI script loader's held object set to one of our own and then to none
// (the resource manager's path; the game's own held object put back after),
// and vectors of pairs with number values of our own. Every result goes
// into the hash; addresses never do.

bool g_pmOn = false;
uint32_t g_pmHash = 2166136261u;

void PmStage(const char* name) { log::Write("autotest: propmake: %s %08X", name, g_pmHash); }

struct PmRegs {
    uint32_t eax, ebx, esi, edi;
    uint32_t args[4];
    uint32_t outEax, outEbx, outEsi, outEdi, outEbp;
    std::uintptr_t address;
    uint32_t outEdx;
};

// The address called with EAX, EBX, ESI and EDI set and four stack
// arguments; EAX, EDX, the four registers and the argument slots (as the
// callee left them) back in the block.
__declspec(naked) void __cdecl PmCallRaw(PmRegs*)
{
    __asm push ebx
    __asm push esi
    __asm push edi
    __asm push ebp
    __asm mov ebp, [esp + 20]
    __asm push ebp
    __asm push dword ptr [ebp + 28]
    __asm push dword ptr [ebp + 24]
    __asm push dword ptr [ebp + 20]
    __asm push dword ptr [ebp + 16]
    __asm mov eax, [ebp + 0]
    __asm mov ebx, [ebp + 4]
    __asm mov esi, [ebp + 8]
    __asm mov edi, [ebp + 12]
    __asm call dword ptr [ebp + 52]
    __asm push eax
    __asm mov eax, [esp + 20]
    __asm mov [eax + 36], ebx
    __asm mov [eax + 40], esi
    __asm mov [eax + 44], edi
    __asm mov [eax + 48], ebp
    __asm mov [eax + 56], edx
    __asm pop dword ptr [eax + 32]
    __asm mov ebx, [esp]
    __asm mov [eax + 16], ebx
    __asm mov ebx, [esp + 4]
    __asm mov [eax + 20], ebx
    __asm mov ebx, [esp + 8]
    __asm mov [eax + 24], ebx
    __asm mov ebx, [esp + 12]
    __asm mov [eax + 28], ebx
    __asm add esp, 20
    __asm pop ebp
    __asm pop edi
    __asm pop esi
    __asm pop ebx
    __asm ret
}

constexpr uint32_t kPmEbx = 0xB0B0B0B1u;
constexpr uint32_t kPmEsi = 0x51515152u;
constexpr uint32_t kPmEdi = 0xD1D1D1D2u;

// A call with the registers' values (the ones the callee does not use set
// to marks); whether EBX, ESI, EDI and EBP were kept goes into the hash.
uint32_t PmCall(std::uintptr_t address, PmRegs& regs)
{
    regs.address = address;
    PmCallRaw(&regs);
    const bool kept = regs.outEbx == regs.ebx && regs.outEsi == regs.esi && regs.outEdi == regs.edi &&
                      regs.outEbp == PrArg(&regs);
    Mix(kept ? 0x4B1u : 0xBAD0u);
    return regs.outEax;
}

PmRegs PmRegsOf(uint32_t eax, uint32_t esi, uint32_t edi, uint32_t a0, uint32_t a1)
{
    PmRegs regs = {};
    regs.eax = eax;
    regs.ebx = kPmEbx;
    regs.esi = esi;
    regs.edi = edi;
    regs.args[0] = a0;
    regs.args[1] = a1;
    regs.args[2] = 0xA2A2A2A2u;
    regs.args[3] = 0xA3A3A3A3u;
    return regs;
}

// A script value: the word +0xC, the kind +0xE, +8 and the size +0x10;
// with the kind's 0x8000 the bytes at +4, else the word there.
void PmMixValue(char* value)
{
    Mix(At<uint16_t>(value, 0xC));
    const uint16_t kind = At<uint16_t>(value, 0xE);
    Mix(kind);
    Mix(At<uint32_t>(value, 8));
    const uint32_t size = At<uint32_t>(value, 0x10);
    Mix(size);
    if (kind & 0x8000) {
        const uint8_t* const bytes = At<const uint8_t*>(value, 4);
        Mix(bytes ? 1u : 0u);
        for (uint32_t i = 0; bytes && i < size && i < 64; ++i)
            Mix(bytes[i]);
    } else {
        Mix(At<uint32_t>(value, 4));
    }
}

uint32_t g_pmPuts = 0;

uint32_t __fastcall PmWindowPut(void*, void*, uint32_t key, char* value)
{
    Mix(0x114000u);
    Mix(key);
    if (value) {
        PmMixValue(value);
        Virtual<void>(value, 0x1FC, 1u);
    } else {
        Mix(0xDEADu);
    }
    ++g_pmPuts;
    return 0x00770042u + (g_pmPuts << 8) + (g_pmPuts << 20);
}

void** PmWindowTable()
{
    static void* table[115] = {};
    table[114] = reinterpret_cast<void*>(&PmWindowPut);
    return table;
}

// The held object we give the UI script loader: AddRef, Release, slot 3
// (an image by key: a word of our own), slot 4 (a text by key into a
// string, for odd instances).
struct PmHeld {
    void** vtable;
    uint32_t count;
};

uint32_t __fastcall PmHeldAddRef(PmHeld* self, void*) { return ++self->count; }
uint32_t __fastcall PmHeldRelease(PmHeld* self, void*) { return --self->count; }

void PmMixKey(const uint32_t* key)
{
    Mix(key[0]);
    Mix(key[1]);
    Mix(key[2]);
}

uint32_t __fastcall PmHeldImage(PmHeld*, void*, const uint32_t* key)
{
    Mix(0x3003u);
    PmMixKey(key);
    return key[2] ^ 0x5A5A0000u;
}

uint32_t __fastcall PmHeldText(PmHeld*, void*, const uint32_t* key, void* string)
{
    Mix(0x4004u);
    PmMixKey(key);
    if (!(key[2] & 1))
        return 0x2200u;
    char text[32];
    const int length = std::snprintf(text, sizeof text, "held %08X", key[2]);
    Virtual<uint32_t>(string, 0xC, text, static_cast<uint32_t>(length));
    return 0x2201u;
}

void** PmHeldTable()
{
    static void* table[5] = {};
    table[1] = reinterpret_cast<void*>(&PmHeldAddRef);
    table[2] = reinterpret_cast<void*>(&PmHeldRelease);
    table[3] = reinterpret_cast<void*>(&PmHeldImage);
    table[4] = reinterpret_cast<void*>(&PmHeldText);
    return table;
}

// The number values of the lookups: slot 3 the kind, slots 73 and 75 point
// at two words of their own (+8 and +0xC).
struct PmNumber {
    void** vtable;
    uint32_t kind;
    uint32_t word73;
    uint32_t word75;
};

uint32_t __fastcall PmNumberKind(PmNumber* self, void*)
{
    Mix(0x3000u | self->kind);
    return self->kind;
}

// Slots 73 and 75 leave a mark in EDX, which the finders leave to their
// callers.
__declspec(naked) void PmNumber73()
{
    __asm lea eax, [ecx + 8]
    __asm mov edx, 0x73737373
    __asm ret
}

__declspec(naked) void PmNumber75()
{
    __asm lea eax, [ecx + 12]
    __asm mov edx, 0x75757575
    __asm ret
}

void** PmNumberTable()
{
    static void* table[76] = {};
    table[3] = reinterpret_cast<void*>(&PmNumberKind);
    table[73] = reinterpret_cast<void*>(&PmNumber73);
    table[75] = reinterpret_cast<void*>(&PmNumber75);
    return table;
}

// A game string (SC4String's constructor) with a text, and its end.
void PmStringMake(char* string, const char* text)
{
    CallThis<void*>(addr::SC4String_SC4String, string);
    if (text)
        Virtual<uint32_t>(string, 0xC, text, static_cast<uint32_t>(std::strlen(text)));
}

void PmStringEnd(char* string)
{
    if (char* const begin = At<char*>(string, 4))
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(begin);
    At<std::uintptr_t>(string, 0) = addr::SC4String_Vtable;
}

void DrivePmMake()
{
    void* window[2] = {PmWindowTable(), nullptr};
    const uint32_t window32 = PrArg(window);

    // 0x0094C1A4: none without a window (the caller's EAX back), then three
    // keys of 12 bytes.
    const uint32_t keys[3][3] = {
        {0x856DDBAC, 0x46A006B0, 0x12345678}, {0x2026960B, 0, 0xFFFFFFFF}, {0, 0, 0}};
    PmRegs regs = PmRegsOf(0x13572468u, 0, kPmEdi, 0x8B4BFEFC, PrArg(keys[0]));
    Mix(PmCall(addr::cGZUIScriptProps_MakeKey, regs));
    for (uint32_t i = 0; i < 3; ++i) {
        regs = PmRegsOf(0x24681357u, window32, kPmEdi, 0x8B4BFEFC + i, PrArg(keys[i]));
        Mix(PmCall(addr::cGZUIScriptProps_MakeKey, regs));
    }
    // 0x0094C1DA: two words (the first in EAX) under a key's offset.
    const uint32_t pairs[3][3] = {{0, 0, 0}, {0x856DDBAC, 1, 0x2B343BEC}, {0xFFFFFFFF, 0x80000000, 0x25C5A6B8}};
    for (const auto& pair : pairs) {
        regs = PmRegsOf(pair[0], window32, kPmEdi, pair[1], pair[2]);
        Mix(PmCall(addr::cGZUIScriptProps_MakePair, regs));
    }
    // 0x0094C222: numbers; 0x0094C255: texts.
    const uint32_t numbers[] = {0, 5, 0xFFFFFFFF, 0x3F800000};
    for (uint32_t i = 0; i < std::size(numbers); ++i) {
        regs = PmRegsOf(0x5555AAAAu, window32, kPmEdi, numbers[i], 0x1000 + i);
        Mix(PmCall(addr::cGZUIScriptProps_MakeNumber, regs));
    }
    const char* const texts[] = {nullptr, "hello", "(1,2,3)", "a longer text of a window's script property"};
    for (uint32_t i = 0; i < std::size(texts); ++i) {
        alignas(4) char string[0x14];
        PmStringMake(string, texts[i]);
        regs = PmRegsOf(0x6666BBBBu, window32, kPmEdi, PrArg(string), 0x2000 + i);
        Mix(PmCall(addr::cGZUIScriptProps_MakeText, regs));
        PmStringEnd(string);
    }
    Mix(g_pmPuts);
    PmStage("make");
}

// The two loaders with the group in EAX (the text loader's string in EDI);
// what they leave in their argument slot: unchanged, zero or set.
void PmLoadImage(uint32_t group, uint32_t instance, bool pointer)
{
    PmRegs regs = PmRegsOf(group, kPmEsi, kPmEdi, instance, 0xA1A1A1A1u);
    const uint32_t eax = PmCall(addr::cGZUIScriptProps_LoadImage, regs);
    Mix(pointer ? (eax ? 1u : 0u) : eax);
    Mix(regs.args[0] == instance ? 0x11u : regs.args[0] ? 0x12u : 0x10u);
    Mix(regs.args[1]);
}

void PmLoadText(uint32_t group, uint32_t instance)
{
    alignas(4) char string[0x14];
    PmStringMake(string, "before");
    PmRegs regs = PmRegsOf(group, kPmEsi, PrArg(string), instance, 0xA1A1A1A1u);
    regs.ebx = kPmEbx;
    MixByte(PmCall(addr::cGZUIScriptProps_LoadText, regs));
    Mix(regs.args[0] == instance ? 0x11u : regs.args[0] ? 0x12u : 0x10u);
    PrMixText(string);
    PmStringEnd(string);
}

void DrivePmLoad()
{
    void* holder = nullptr;
    CallThis<void*>(addr::SC4RefPtr_UIScriptLoader, &holder);
    Mix(holder ? 1u : 0u);
    if (!holder) {
        PmStage("load");
        return;
    }
    void* const old = Virtual<void*>(holder, 0x24);
    Mix(old ? 1u : 0u);
    log::Write("autotest: propmake: the loader's held object %s", old ? "set" : "none");
    if (old)
        Virtual<uint32_t>(old, 4);

    PmHeld held = {PmHeldTable(), 0};
    Virtual<uint32_t>(holder, 0x28, static_cast<void*>(&held));
    for (uint32_t instance : {0u, 1u, 2u, 0x7EC82BF1u}) {
        PmLoadImage(0x11223344u, instance, false);
        PmLoadText(0x55667788u, instance);
    }
    Virtual<uint32_t>(holder, 0x28, static_cast<void*>(nullptr));
    Mix(held.count);

    // The resource manager's: images the game ships (the region screen's
    // first edge picture) and none, the text the audio layer's first
    // freshness score reads and none.
    PmLoadImage(0x6A1EED2Cu, At<uint32_t>(reinterpret_cast<void*>(addr::SC4RegionScreen_EdgePictures), 0), true);
    PmLoadImage(0x12345678u, 0x9ABCDEF0u, true);
    PmLoadText(0x0A5BCDA5u, 0x7EC82BF0u);
    PmLoadText(0x12345678u, 0x9ABCDEF0u);

    Virtual<uint32_t>(holder, 0x28, old);
    if (old)
        Virtual<uint32_t>(old, 8);
    CallThis<uint32_t>(addr::SC4RefPtr_Destruct, &holder);
    PmStage("load");
}

// The lookups' EAX: a found word, a count, a kind or the caller's mark in
// full; on a miss over a pair's place, whether it is the last pair's.
void PmMixEax(uint32_t eax, const void* lastPair)
{
    if ((eax & 0xFF) == 1 || eax < 0x10000u || (eax & 0xFFFFFF00u) == 0x77777700u)
        Mix(eax);
    else
        Mix((eax & ~0xFFu) == (PrArg(lastPair) & ~0xFFu) ? 0xADD1u : 0xADD0u);
}

void DrivePmLook()
{
    PmNumber numbers[] = {
        {PmNumberTable(), 6, 1, 0x3F800000},  {PmNumberTable(), 5, 1, 2},  {PmNumberTable(), 6, 0, 0xFFFFFFFF},
        {PmNumberTable(), 6, 2, 7},           {PmNumberTable(), 0x800E, 1, 1},
        {PmNumberTable(), 6, 0x12345601, 0x99}, {PmNumberTable(), 6, 1, 0x42},
    };
    struct Pair {
        uint32_t key;
        PmNumber* value;
    };
    Pair pairs[] = {
        {0x304, &numbers[0]}, {0x40A, nullptr},     {0x40A, &numbers[1]}, {0x40A, &numbers[2]},
        {0x107, &numbers[3]}, {0x1005, &numbers[4]}, {0x1005, &numbers[5]}, {0x20, &numbers[6]},
        {0x21, nullptr},      {0x22, &numbers[4]},
    };
    const uint32_t lookKeys[] = {0, 0x304, 0x40A, 0x107, 0x1005, 0x20, 0x21, 0x22, 0x999};
    char* vector[3] = {reinterpret_cast<char*>(pairs), reinterpret_cast<char*>(pairs + std::size(pairs)), nullptr};
    char* emptyVector[3] = {reinterpret_cast<char*>(pairs), reinterpret_cast<char*>(pairs), nullptr};
    const void* const last = &pairs[std::size(pairs) - 1];

    // 0x0094C64E: the vector in EAX.
    for (uint32_t key : lookKeys) {
        PmRegs regs = PmRegsOf(PrArg(vector), kPmEsi, kPmEdi, key, 0);
        Mix(PmCall(addr::cGZUIScriptProps_HasKey, regs));
    }
    PmRegs regs = PmRegsOf(PrArg(emptyVector), kPmEsi, kPmEdi, 0x304, 0);
    Mix(PmCall(addr::cGZUIScriptProps_HasKey, regs));

    // 0x0094C675 and 0x0094C6C7: the vector in EDI, the out pre-set; EAX a
    // mark (the empty vector's answer).
    for (std::uintptr_t address : {addr::cGZUIScriptProps_FindWord73, addr::cGZUIScriptProps_FindWord75}) {
        for (uint32_t key : lookKeys) {
            uint32_t out = 0xEEEEEEEEu;
            regs = PmRegsOf(0x77777777u, kPmEsi, PrArg(vector), key, PrArg(&out));
            PmMixEax(PmCall(address, regs), last);
            Mix(out);
            if ((regs.outEax & 0xFF) == 1)
                Mix(regs.outEdx);
        }
        uint32_t out = 0xEEEEEEEEu;
        regs = PmRegsOf(0x77777777u, kPmEsi, PrArg(emptyVector), 0x304, PrArg(&out));
        PmMixEax(PmCall(address, regs), last);
        Mix(out);
    }

    // 0x0094C719: tables of (key, mask) in EAX; the flags' slot after.
    const uint32_t table[] = {0x304, 0x1, 0x40A, 0x2, 0x107, 0x4, 0x20, 0x8, 0x21, 0x10, 0x999, 0x20, 0x22, 0x40,
                              0x1005, 0x80, 0};
    const uint32_t emptyTable[] = {0, 0xFF};
    for (uint32_t flags : {0u, 0xFFFFFFFFu, 0x55u, 0xAAu}) {
        regs = PmRegsOf(PrArg(table), kPmEsi, kPmEdi, PrArg(vector), flags);
        Mix(PmCall(addr::cGZUIScriptProps_ReadFlags, regs));
        Mix(regs.args[1]);
        regs = PmRegsOf(PrArg(table), kPmEsi, kPmEdi, PrArg(emptyVector), flags);
        Mix(PmCall(addr::cGZUIScriptProps_ReadFlags, regs));
        regs = PmRegsOf(PrArg(emptyTable), kPmEsi, kPmEdi, PrArg(vector), flags);
        Mix(PmCall(addr::cGZUIScriptProps_ReadFlags, regs));
        Mix(regs.args[1]);
    }
    PmStage("look");
}

// OPENSC4_TEST_PROPMAKE=<day>: part u12's second slice by address. Every
// result goes into the fingerprint's `pm` part.
void DrivePropMakeDay(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_PROPMAKE") || !game::IsBuild641())
        return;
    g_pmOn = true;
    uint32_t* const saved = g_into;
    g_into = &g_pmHash;
    DrivePmMake();
    DrivePmLoad();
    DrivePmLook();
    g_into = saved;
    log::Write("autotest: propmake driven (hash %08X)", g_pmHash);
}

void PmFingerprint(cISC4City*, std::string& line)
{
    if (!g_pmOn)
        return;
    char text[32];
    std::snprintf(text, sizeof text, " | pm %08X", g_pmHash);
    line += text;
}

const FingerprintPart kPmFingerprint{"propmake", 4, &PmFingerprint};
const DayHook kPmDrive{"propmake", 4, &DrivePropMakeDay};
// ====================================================================================
// Part u13: the stock widgets' registrations and GZWinGen's property handler
// (OPENSC4_TEST_WIDGETREAD). The 17 called by address (registers checked
// kept after): the registrations with a variable table of our own put in
// the shared table's place (its slot 3 hashes every name and word; the
// game's put back after), then the two maps' entries for every class;
// GZWinGen's handler on windows, interfaces, number and text values, held
// image objects and images of our own, both passes. Addresses never go
// into the hash.

bool g_wrOn = false;
uint32_t g_wrHash = 2166136261u;

void WrStage(const char* name) { log::Write("autotest: widgetread: %s %08X", name, g_wrHash); }

// The variable table of our own: slot 3 (a string and a word).
uint32_t g_wrAdds = 0;

uint32_t __fastcall WrTableAdd(void*, void*, char* string, uint32_t word)
{
    Mix(0xADD0u);
    PrMixText(string);
    Mix(word);
    ++g_wrAdds;
    return 0x00AD0000u + g_wrAdds;
}

void** WrTableTable()
{
    static void* table[4] = {};
    table[3] = reinterpret_cast<void*>(&WrTableAdd);
    return table;
}

// An entry of one of the two maps (STLport tree: the header's +4 the root,
// a node's +8 left, +0xC right, +0x10 key, +0x14 value); 0xFFFFFFFF none.
uint32_t WrMapFind(std::uintptr_t map, uint32_t key)
{
    char* const header = *reinterpret_cast<char**>(map);
    if (!header)
        return 0xFFFFFFFEu;
    for (char* node = At<char*>(header, 4); node;) {
        const uint32_t nodeKey = At<uint32_t>(node, 0x10);
        if (key < nodeKey)
            node = At<char*>(node, 8);
        else if (nodeKey < key)
            node = At<char*>(node, 0xC);
        else
            return At<uint32_t>(node, 0x14);
    }
    return 0xFFFFFFFFu;
}

void DriveWrRegister()
{
    // The 16 by address in the constructor's order, with the class's
    // interface and the class (the maps' keys).
    struct Registration {
        std::uintptr_t address;
        uint32_t iface;
        uint32_t klass;
    };
    const Registration registrations[] = {
        {addr::cGZUIScriptWidgets_RegisterFlatRect, 0xC2AFA76F, 0xC2AFA76E},
        {addr::cGZUIScriptWidgets_RegisterText, 0x212CDC1F, 0x00000592},
        {addr::cGZUIScriptWidgets_RegisterTextEdit, 0x231A1C57, 0x231A1C58},
        {addr::cGZUIScriptWidgets_RegisterListBox, 0x4132242B, 0x00000598},
        {addr::cGZUIScriptWidgets_RegisterCombo, 0x412CE496, 0x0000059B},
        {addr::cGZUIScriptWidgets_RegisterBtn, 0x00008810, 0x22ECFC47},
        {addr::cGZUIScriptWidgets_RegisterBMP, 0xC12CEA13, 0x82FE68C4},
        {addr::cGZUIScriptWidgets_RegisterSlider, 0x21325207, 0x21325208},
        {addr::cGZUIScriptWidgets_RegisterScrollbar, 0x61325A2D, 0x61325A2E},
        {addr::cGZUIScriptWidgets_RegisterLineInput, 0x21335C59, 0x21335C5A},
        {addr::cGZUIScriptWidgets_RegisterSpinner, 0x612CE0C3, 0x612CE0C4},
        {addr::cGZUIScriptWidgets_RegisterOutline, 0xE303E5AE, 0x4303E5B6},
        {addr::cGZUIScriptWidgets_RegisterTextTicker, 0x032F0B3C, 0xE32F0B31},
        {addr::cGZUIScriptWidgets_RegisterFolders, 0xA13351B8, 0x078E8055},
        {addr::cGZUIScriptWidgets_RegisterCustom, 0x678D1E84, 0x478D1E6F},
        {addr::cGZUIScriptWidgets_RegisterGen, 0x5386D516, 0x4386D516},
    };
    void** const tableAt = reinterpret_cast<void**>(addr::cGZUIScriptLegacy_Table);
    void* const old = *tableAt;
    Mix(old ? 1u : 0u);
    void* table[1] = {WrTableTable()};
    *tableAt = table;
    for (const Registration& registration : registrations) {
        // EAX is the game's free's or a stack place: not hashed.
        PmRegs regs = PmRegsOf(0x13131313u, kPmEsi, kPmEdi, 0xA0A0A0A0u, 0xA1A1A1A1u);
        PmCall(registration.address, regs);
        Mix(g_wrAdds);
        Mix(WrMapFind(addr::cGZUIScriptWidgets_Handlers, registration.iface));
        Mix(WrMapFind(addr::cGZUIScriptWidgets_Interfaces, registration.klass));
    }
    *tableAt = old;
    WrStage("reg");
}

// The window: slot 0 (QueryInterface: 0 none, 1 the interface, 2 the
// interface given but AL 0) and slot 114 (part u12's, which hashes what it
// is given).
struct WrWindow {
    void** vtable;
    uint32_t mode;
    void* face;
};

uint32_t __fastcall WrWindowQuery(WrWindow* self, void*, uint32_t iid, void** out)
{
    Mix(0x0E00u | self->mode);
    Mix(iid);
    if (self->mode == 0)
        return 0x12345600u;
    *out = self->face;
    Virtual<uint32_t>(self->face, 4);
    return self->mode == 1 ? 0x12345601u : 0x12345700u;
}

void** WrWindowTable()
{
    static void* table[115] = {};
    table[0] = reinterpret_cast<void*>(&WrWindowQuery);
    table[114] = reinterpret_cast<void*>(&PmWindowPut);
    return table;
}

// The images our held object gives (slot 2 Release counted).
struct WrImage {
    void** vtable;
    uint32_t tag;
    uint32_t count;
};

uint32_t __fastcall WrImageRelease(WrImage* self, void*)
{
    Mix(0x1A00u | self->tag);
    return --self->count;
}

void** WrImageTable()
{
    static void* table[3] = {};
    table[2] = reinterpret_cast<void*>(&WrImageRelease);
    return table;
}

WrImage g_wrImages[2] = {};

// The held object: AddRef, Release, slot 3 (an image by key: for even
// instances one of ours, held; for odd none).
uint32_t __fastcall WrHeldImage(PmHeld*, void*, const uint32_t* key)
{
    Mix(0x3013u);
    PmMixKey(key);
    if (key[2] & 1)
        return 0;
    WrImage& image = g_wrImages[(key[2] >> 1) & 1];
    ++image.count;
    return PrArg(&image);
}

void** WrHeldTable()
{
    static void* table[5] = {};
    table[1] = reinterpret_cast<void*>(&PmHeldAddRef);
    table[2] = reinterpret_cast<void*>(&PmHeldRelease);
    table[3] = reinterpret_cast<void*>(&WrHeldImage);
    return table;
}

// The interface (IGZWinGen): AddRef, Release, and the slots the handler
// calls, each hashing what it gets.
struct WrFace {
    void** vtable;
    uint32_t count;
};

uint32_t __fastcall WrFaceAddRef(WrFace* self, void*) { return ++self->count; }

uint32_t __fastcall WrFaceRelease(WrFace* self, void*)
{
    Mix(0xF2F2u);
    return 0x00F20000u + --self->count;
}

uint32_t __fastcall WrFaceFlag(WrFace*, void*, uint32_t mask, uint32_t set)
{
    Mix(0xF6u);
    Mix(mask);
    Mix(set);
    return 0x00F60000u | mask;
}

uint32_t __fastcall WrFaceObject(WrFace*, void*, char* object)
{
    Mix(0xF7u);
    Mix(object ? 1u : 0u);
    if (object) {
        Mix(At<std::uintptr_t>(object, 0) == addr::cGZUIScriptWidgets_GenObjectVtable ? 0x7AB1u : 0x7AB0u);
        Mix(At<uint32_t>(object, 4));
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_operator_delete)(object);
    }
    return 0x00F70000u;
}

uint32_t __fastcall WrFaceImage(WrFace*, void*, uint32_t mode, WrImage* image)
{
    Mix(0xF13u);
    Mix(mode);
    Mix(image ? image->tag : 0xDEADu);
    return 0x00F13000u;
}

uint32_t __fastcall WrFaceUser(WrFace*, void*, uint32_t word)
{
    Mix(0xF19u);
    Mix(word);
    return 0x00F19000u;
}

uint32_t __fastcall WrFacePair(WrFace*, void*, uint32_t first, uint32_t second)
{
    Mix(0xF26u);
    Mix(first);
    Mix(second);
    return 0x00F26000u;
}

uint32_t __fastcall WrFaceRect(WrFace*, void*, uint32_t word, uint32_t zero1, uint32_t zero2, const int32_t* rect)
{
    Mix(0xF29u);
    Mix(word);
    Mix(zero1);
    Mix(zero2);
    for (int i = 0; i < 4; ++i)
        Mix(static_cast<uint32_t>(rect[i]));
    return 0x00F29000u;
}

uint32_t __fastcall WrFacePoint(WrFace*, void*, uint32_t word, uint32_t point)
{
    Mix(0xF30u);
    Mix(word);
    Mix(point);
    return 0x00F30000u;
}

void** WrFaceTable()
{
    static void* table[31] = {};
    table[1] = reinterpret_cast<void*>(&WrFaceAddRef);
    table[2] = reinterpret_cast<void*>(&WrFaceRelease);
    table[6] = reinterpret_cast<void*>(&WrFaceFlag);
    table[7] = reinterpret_cast<void*>(&WrFaceObject);
    table[13] = reinterpret_cast<void*>(&WrFaceImage);
    table[19] = reinterpret_cast<void*>(&WrFaceUser);
    table[26] = reinterpret_cast<void*>(&WrFacePair);
    table[29] = reinterpret_cast<void*>(&WrFaceRect);
    table[30] = reinterpret_cast<void*>(&WrFacePoint);
    return table;
}

// The values: slot 3 the kind, slot 55 the text into a string, slot 73
// the word (+8) with a mark in EDX.
struct WrValue {
    void** vtable;
    uint32_t kind;
    uint32_t word;
    const char* text;
};

uint32_t __fastcall WrValueKind(WrValue* self, void*)
{
    Mix(0x3000u | self->kind);
    return self->kind;
}

uint32_t __fastcall WrValueText(WrValue* self, void*, char* string)
{
    Mix(0x55u);
    Virtual<uint32_t>(string, 0xC, self->text, static_cast<uint32_t>(std::strlen(self->text)));
    return 0x5555u;
}

__declspec(naked) void WrValue73()
{
    __asm lea eax, [ecx + 8]
    __asm mov edx, 0x73A5A573
    __asm ret
}

void** WrValueTable()
{
    static void* table[74] = {};
    table[3] = reinterpret_cast<void*>(&WrValueKind);
    table[55] = reinterpret_cast<void*>(&WrValueText);
    table[73] = reinterpret_cast<void*>(&WrValue73);
    return table;
}

WrValue WrNumber(uint32_t word) { return {WrValueTable(), 6, word, ""}; }
WrValue WrText(const char* text) { return {WrValueTable(), 0x800E, 0x7E7E7E7Eu, text}; }

struct WrPair {
    uint32_t key;
    WrValue* value;
};

// One call of the handler: the window, a vector of the pairs and the
// byte's word; its EAX and its first argument slot after.
void WrGen(WrWindow& window, WrPair* pairs, std::size_t count, uint32_t set)
{
    char* vector[3] = {reinterpret_cast<char*>(pairs), reinterpret_cast<char*>(pairs + count), nullptr};
    PmRegs regs = PmRegsOf(0x26262626u, kPmEsi, kPmEdi, PrArg(&window), PrArg(vector));
    regs.args[2] = set;
    Mix(PmCall(addr::cGZUIScriptWidgets_GenRead, regs));
    Mix(regs.args[0] == PrArg(&window) ? 0x77DDu : regs.args[0]);
    Mix(regs.args[1] == PrArg(vector) ? 0x77DEu : 0xBAD1u);
    Mix(regs.args[2]);
}

void DriveWrGen()
{
    void* holder = nullptr;
    CallThis<void*>(addr::SC4RefPtr_UIScriptLoader, &holder);
    Mix(holder ? 1u : 0u);
    if (!holder) {
        WrStage("gen");
        return;
    }
    void* const old = Virtual<void*>(holder, 0x24);
    if (old)
        Virtual<uint32_t>(old, 4);
    PmHeld held = {WrHeldTable(), 0};
    Virtual<uint32_t>(holder, 0x28, static_cast<void*>(&held));
    g_wrImages[0] = {WrImageTable(), 0xA0, 0};
    g_wrImages[1] = {WrImageTable(), 0xA1, 0};

    WrFace face = {WrFaceTable(), 1};
    WrWindow windows[3] = {{WrWindowTable(), 0, &face}, {WrWindowTable(), 1, &face}, {WrWindowTable(), 2, &face}};

    // The set pass's values: numbers, texts, a value that is neither.
    WrValue n0 = WrNumber(0), n1 = WrNumber(1), n2 = WrNumber(2), n3 = WrNumber(3), n5 = WrNumber(5),
            n7 = WrNumber(7), user = WrNumber(0x12345678), other = {WrValueTable(), 9, 1, "(1,2)"};
    WrValue image0 = WrText("{1a2b,3c4c}"), image1 = WrText("{11,13}"), image2 = WrText("{2,6}"),
            imageBad = WrText("{zz}"), imageOne = WrText("{7}"), pairGood = WrText("(5,-6)"),
            pairBad = WrText("(5)"), flagText = WrText("1");
    WrPair full[] = {
        {0x10004, &n1},     {0x10005, &n0},      {0x10006, &n7},      {0x10007, &flagText}, {0x10008, nullptr},
        {0x1000A, &n1},     {0x1000B, &n0},      {0x1000C, &n2},      {0x1000D, &n0},       {0xF00D, &n1},
        {0xF019, &n0},      {0x1000E, &n5},      {0x1000F, &n1},      {0x10000, &user},     {0xF016, &image0},
        {0x10001, &n2},     {0xF004, &pairGood}, {0x10004, &n0},      {0x10009, &other},
    };
    WrPair second[] = {
        {0x10009, &n1}, {0x1000D, &n3}, {0xF016, &image1}, {0x10001, &n3}, {0xF004, &pairBad}, {0x10000, &other},
    };
    WrPair third[] = {{0xF016, &imageBad}, {0xF004, &image0}, {0x1000E, &n0}};
    WrPair fourth[] = {{0xF016, &image2}, {0x1000F, &n0}};
    WrPair fifth[] = {{0xF016, &n1}, {0x10001, &n1}, {0xF016, &imageOne}};
    WrPair sixth[] = {{0x10001, &n1}, {0xF016, &image2}, {0x10001, &n0}};

    // The raw pass's: texts of 0x10002 and 0x10003, good and not.
    WrValue point = WrText("(7,-8)"), pointBad = WrText("(1)"), pointBig = WrText("(4294967295,-1)"),
            rect = WrText("(9,1,2,3,4)"), rectBad = WrText("(1,2,3,4)"), rectNeg = WrText("(0,-1,-2,-3,-4)");
    WrPair raw[] = {
        {0x10002, &point},   {0x10003, &rect},     {0x10002, nullptr}, {0x10003, &n1},     {0x10002, &pointBad},
        {0x10003, &rectBad}, {0x55, &point},       {0x10002, &pointBig}, {0x10003, &rectNeg}, {0x10004, &n1},
    };
    WrPair rawNone[] = {{0x10004, &n1}, {0x55, &rect}};

    for (WrWindow& window : windows) {
        for (uint32_t set : {1u, 0xFFFFFF01u}) {
            WrGen(window, full, std::size(full), set);
            WrGen(window, second, std::size(second), set);
            WrGen(window, third, std::size(third), set);
            WrGen(window, fourth, std::size(fourth), set);
            WrGen(window, fifth, std::size(fifth), set);
            WrGen(window, sixth, std::size(sixth), set);
            WrGen(window, full, 0, set);
        }
        for (uint32_t set : {0u, 0x100u}) {
            WrGen(window, raw, std::size(raw), set);
            WrGen(window, rawNone, std::size(rawNone), set);
            WrGen(window, full, std::size(full), set);
            WrGen(window, raw, 0, set);
        }
    }
    Mix(face.count);
    Mix(g_wrImages[0].count);
    Mix(g_wrImages[1].count);
    Mix(held.count);

    Virtual<uint32_t>(holder, 0x28, old);
    if (old)
        Virtual<uint32_t>(old, 8);
    CallThis<uint32_t>(addr::SC4RefPtr_Destruct, &holder);
    WrStage("gen");
}

// OPENSC4_TEST_WIDGETREAD=<day>: part u13 by address. Every result goes
// into the fingerprint's `wr` part.
void DriveWidgetReadDay(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_WIDGETREAD") || !game::IsBuild641())
        return;
    g_wrOn = true;
    uint32_t* const saved = g_into;
    g_into = &g_wrHash;
    DriveWrRegister();
    DriveWrGen();
    g_into = saved;
    log::Write("autotest: widgetread driven (hash %08X)", g_wrHash);
}

void WrFingerprint(cISC4City*, std::string& line)
{
    if (!g_wrOn)
        return;
    char text[32];
    std::snprintf(text, sizeof text, " | wr %08X", g_wrHash);
    line += text;
}

const FingerprintPart kWrFingerprint{"widgetread", 4, &WrFingerprint};
const DayHook kWrDrive{"widgetread", 4, &DriveWidgetReadDay};
// ====================================================================================
// Part u14: GZWinFileBrowser's registration and property handler
// (OPENSC4_TEST_FILEBROWSER). The two called by address (registers checked
// kept after): the registration with part u13's variable table of our own
// in the shared table's place, then the two maps' entries for the class;
// the handler on part u13's windows and on an interface of our own (every
// slot it calls hashes its number and what it gets; the getters give words
// of their own), with number and text values and the held image object of
// part u13, and the game's font and colour services. Addresses never go
// into the hash.

bool g_fbOn = false;
uint32_t g_fbHash = 2166136261u;

void FbStage(const char* name) { log::Write("autotest: filebrowser: %s %08X", name, g_fbHash); }

void DriveFbRegister()
{
    void** const tableAt = reinterpret_cast<void**>(addr::cGZUIScriptLegacy_Table);
    void* const old = *tableAt;
    Mix(old ? 1u : 0u);
    void* table[1] = {WrTableTable()};
    *tableAt = table;
    const uint32_t before = g_wrAdds;
    // EAX is the game's free's: not hashed.
    PmRegs regs = PmRegsOf(0x14141414u, kPmEsi, kPmEdi, 0xA0A0A0A0u, 0xA1A1A1A1u);
    PmCall(addr::cGZUIScriptWidgets_RegisterFileBrowser, regs);
    Mix(g_wrAdds - before);
    Mix(WrMapFind(addr::cGZUIScriptWidgets_Handlers, 0x3AA52E64));
    Mix(WrMapFind(addr::cGZUIScriptWidgets_Interfaces, 0x1AA52EA4));
    *tableAt = old;
    FbStage("reg");
}

// The fonts the test's numbers ask the window manager for (its slot 5),
// as the game gives them; a font slot hashes which it got.
constexpr uint32_t kFbFontWords[3] = {0xE9C86B5E, 0xE9C86B5F, 0x4A809914};
void* g_fbFonts[3] = {};

// The interface (IGZWinFileBrowser): AddRef, Release and the slots the
// handler calls, each hashing its own number first. The getters write
// words of their own (the column grid's only when `grid` is set: else the
// handler hands on what the line grid's left).
struct FbFace {
    void** vtable;
    uint32_t count;
    uint32_t grid;
};

uint32_t __fastcall FbFaceAddRef(FbFace* self, void*) { return ++self->count; }

uint32_t __fastcall FbFaceRelease(FbFace* self, void*)
{
    Mix(0xFBF2u);
    return 0x00FB0000u + --self->count;
}

template <uint32_t Slot>
uint32_t __fastcall FbSet1(FbFace*, void*, uint32_t a)
{
    Mix(0xFB000000u | Slot);
    Mix(a);
    return 0x00FB0000u | Slot;
}

template <uint32_t Slot>
uint32_t __fastcall FbSet2(FbFace*, void*, uint32_t a, uint32_t b)
{
    Mix(0xFB000000u | Slot);
    Mix(a);
    Mix(b);
    return 0x00FB0000u | Slot;
}

template <uint32_t Slot>
uint32_t __fastcall FbSet3(FbFace*, void*, uint32_t a, uint32_t b, uint32_t c)
{
    Mix(0xFB000000u | Slot);
    Mix(a);
    Mix(b);
    Mix(c);
    return 0x00FB0000u | Slot;
}

template <uint32_t Slot>
uint32_t __fastcall FbString(FbFace*, void*, char* string)
{
    Mix(0xFB000000u | Slot);
    PrMixText(string);
    return 0x00FB0000u | Slot;
}

template <uint32_t Slot>
uint32_t __fastcall FbFont(FbFace*, void*, void* font)
{
    Mix(0xFB000000u | Slot);
    uint32_t which = 0xFF;
    for (uint32_t i = 0; i < 3; ++i) {
        if (font == g_fbFonts[i])
            which = i;
    }
    Mix(which);
    Mix(font ? 1u : 0u);
    return 0x00FB0000u | Slot;
}

template <uint32_t Slot>
uint32_t __fastcall FbGet2(FbFace*, void*, uint32_t* first, uint32_t* second)
{
    Mix(0xFB000000u | Slot);
    *first = 0xC1C1C100u | Slot;
    *second = 0xC2C2C200u | Slot;
    return 0x00FB0000u | Slot;
}

uint32_t __fastcall FbGetGrid(FbFace* self, void*, uint32_t* first, uint32_t* second)
{
    Mix(0xFB000044u | (self->grid << 8));
    if (self->grid) {
        *first = 0xC5C5C501u;
        *second = 0xC6C6C6C6u;
    }
    return 0x00FB0044u;
}

uint32_t __fastcall FbGet3(FbFace*, void*, uint32_t* first, uint32_t* second, uint32_t* third)
{
    Mix(0xFB000026u);
    *first = 0xC3C3C3C3u;
    *second = 0xC4C4C4C4u;
    *third = 7u;
    return 0x00FB0026u;
}

uint32_t __fastcall FbSelection(FbFace*, void*, uint32_t kind, char* name, char* extension)
{
    Mix(0xFB00001Cu);
    Mix(kind);
    PrMixText(name);
    PrMixText(extension);
    return 0x00FB001Cu;
}

uint32_t __fastcall FbFilter(FbFace*, void*, char* name, WrImage* image, uint32_t number, uint32_t zero)
{
    Mix(0xFB000015u);
    PrMixText(name);
    Mix(image ? image->tag : 0xDEADu);
    Mix(number);
    Mix(zero);
    return 0x00FB0015u;
}

uint32_t __fastcall FbImage(FbFace*, void*, WrImage* image)
{
    Mix(0xFB000055u);
    Mix(image ? image->tag : 0xDEADu);
    return 0x00FB0055u;
}

void** FbFaceTable()
{
    static void* table[88] = {};
    table[1] = reinterpret_cast<void*>(&FbFaceAddRef);
    table[2] = reinterpret_cast<void*>(&FbFaceRelease);
    table[15] = reinterpret_cast<void*>(&FbString<15>);
    table[21] = reinterpret_cast<void*>(&FbFilter);
    table[28] = reinterpret_cast<void*>(&FbSelection);
    table[34] = reinterpret_cast<void*>(&FbGet2<34>);
    table[35] = reinterpret_cast<void*>(&FbSet2<35>);
    table[37] = reinterpret_cast<void*>(&FbSet1<37>);
    table[38] = reinterpret_cast<void*>(&FbGet3);
    table[40] = reinterpret_cast<void*>(&FbSet3<40>);
    table[45] = reinterpret_cast<void*>(&FbFont<45>);
    table[46] = reinterpret_cast<void*>(&FbSet2<46>);
    table[47] = reinterpret_cast<void*>(&FbSet1<47>);
    table[51] = reinterpret_cast<void*>(&FbFont<51>);
    table[52] = reinterpret_cast<void*>(&FbSet2<52>);
    table[53] = reinterpret_cast<void*>(&FbSet1<53>);
    table[57] = reinterpret_cast<void*>(&FbFont<57>);
    table[58] = reinterpret_cast<void*>(&FbSet2<58>);
    table[59] = reinterpret_cast<void*>(&FbSet1<59>);
    table[61] = reinterpret_cast<void*>(&FbSet1<61>);
    table[63] = reinterpret_cast<void*>(&FbSet1<63>);
    table[65] = reinterpret_cast<void*>(&FbSet1<65>);
    table[66] = reinterpret_cast<void*>(&FbGet2<66>);
    table[67] = reinterpret_cast<void*>(&FbSet2<67>);
    table[68] = reinterpret_cast<void*>(&FbGetGrid);
    table[69] = reinterpret_cast<void*>(&FbSet2<69>);
    table[70] = reinterpret_cast<void*>(&FbGet2<70>);
    table[71] = reinterpret_cast<void*>(&FbSet2<71>);
    table[77] = reinterpret_cast<void*>(&FbString<77>);
    table[79] = reinterpret_cast<void*>(&FbSet1<79>);
    table[81] = reinterpret_cast<void*>(&FbSet1<81>);
    table[83] = reinterpret_cast<void*>(&FbSet1<83>);
    table[85] = reinterpret_cast<void*>(&FbImage);
    table[87] = reinterpret_cast<void*>(&FbSet1<87>);
    return table;
}

// One call of the handler: the window, a vector of the pairs and the
// byte's word; its EAX and its argument slots after.
void FbRead(WrWindow& window, WrPair* pairs, std::size_t count, uint32_t set)
{
    char* vector[3] = {reinterpret_cast<char*>(pairs), reinterpret_cast<char*>(pairs + count), nullptr};
    PmRegs regs = PmRegsOf(0x27272727u, kPmEsi, kPmEdi, PrArg(&window), PrArg(vector));
    regs.args[2] = set;
    Mix(PmCall(addr::cGZUIScriptWidgets_FileBrowserRead, regs));
    Mix(regs.args[0] == PrArg(&window) ? 0x77DDu : 0xBAD0u);
    Mix(regs.args[1] == PrArg(vector) ? 0x77DEu : 0xBAD1u);
    Mix(regs.args[2]);
}

void DriveFbRead()
{
    void* holder = nullptr;
    CallThis<void*>(addr::SC4RefPtr_UIScriptLoader, &holder);
    Mix(holder ? 1u : 0u);
    void* const fonts = reinterpret_cast<void*(__cdecl*)()>(addr::SC4_Singleton913C72)();
    Mix(fonts ? 1u : 0u);
    if (!holder || !fonts) {
        FbStage("read");
        return;
    }
    for (uint32_t i = 0; i < 3; ++i)
        g_fbFonts[i] = Virtual<void*>(fonts, 0x14, kFbFontWords[i]);
    void* const old = Virtual<void*>(holder, 0x24);
    if (old)
        Virtual<uint32_t>(old, 4);
    PmHeld held = {WrHeldTable(), 0};
    Virtual<uint32_t>(holder, 0x28, static_cast<void*>(&held));
    g_wrImages[0] = {WrImageTable(), 0xB0, 0};
    g_wrImages[1] = {WrImageTable(), 0xB1, 0};

    FbFace face = {FbFaceTable(), 1, 0};
    WrWindow windows[3] = {{WrWindowTable(), 0, &face}, {WrWindowTable(), 1, &face}, {WrWindowTable(), 2, &face}};

    // Numbers: the choices' words, fonts, widths; texts: paths, colours
    // (good, short, out of a byte's range, not numbers), images, filters.
    WrValue nBackdir = WrNumber(0x1302), nDir = WrNumber(0x1303), nFile = WrNumber(0x1304), nBad = WrNumber(0x1399),
            nSort0 = WrNumber(0x1308), nSort1 = WrNumber(0x1309), nSort2 = WrNumber(0x130A), nSort3 = WrNumber(0x130B),
            nDir0 = WrNumber(0x130E), nDir3 = WrNumber(0x1311), nDir2 = WrNumber(0x1310), nIcon = WrNumber(0x1313),
            nName = WrNumber(0x1314), nExt = WrNumber(0x1315), nEmpty = WrNumber(0x1316), nUnused = WrNumber(0x1317),
            nDefault = WrNumber(0x1318), nF006 = WrNumber(0xF006), nF007 = WrNumber(0xF007), nF008 = WrNumber(0xF008),
            nFont0 = WrNumber(kFbFontWords[0]), nFont1 = WrNumber(kFbFontWords[1]), nFont2 = WrNumber(kFbFontWords[2]),
            n0 = WrNumber(0), n1 = WrNumber(1), n5 = WrNumber(5), n16 = WrNumber(16), n40 = WrNumber(40),
            nBig = WrNumber(0xFFFFFFFFu), other = {WrValueTable(), 9, 1, "(1,2,3)"};
    WrValue path = WrText("C:\\Cities\\Saves"), name = WrText("region"), ext = WrText("sc4"),
            fontText = WrText("font text"), back = WrText(".."), c1 = WrText("(1,2,3)"), c2 = WrText("(255,128,0)"),
            c3 = WrText("(300,-1,256)"), c4 = WrText("(10,20,30)"), cShort = WrText("(4,5)"), cWord = WrText("red"),
            c5 = WrText("(7,8,9)"), c6 = WrText("(0,0,0)"), image0 = WrText("{1a2b,3c4c}"), image1 = WrText("{11,13}"),
            imageBad = WrText("{zz}"), f1 = WrText("All files:3:{11,13}"), f2 = WrText("Saves:0:{2,6}"),
            f3 = WrText("Odd:-7:{zz}"), f4 = WrText("Word:x:{1a,4}"), f5 = WrText("a:1:b:{1,2}"),
            f6 = WrText("Big:4294967295:{abc,10}");
    WrPair full[] = {
        {0x1301, &path},     {0x1305, &nDir},     {0x1306, &name},    {0x1307, &ext},      {0x130C, &nSort2},
        {0x130D, &nSort1},   {0x1312, &nDir3},    {0x1319, &nEmpty},  {0x131A, &nIcon},    {0x131B, &nExt},
        {0x131C, &nFont0},   {0x131D, &c1},       {0x131E, &c2},      {0x131F, &c3},       {0x1320, &cShort},
        {0x1321, &cWord},    {0x1322, &c4},       {0x1323, &nF007},   {0x1324, &fontText}, {0x1325, &c2},
        {0x132A, &c5},       {0x132B, &nDefault}, {0x132C, &nFont2},  {0x132D, &c3},       {0x1333, &nF008},
        {0x1334, &n16},      {0x1335, &n40},      {0x1336, &n5},      {0x1337, &n0},       {0x1338, &c4},
        {0x1339, &n5},       {0x133A, &c5},       {0x133B, &c6},      {0x133C, &c1},       {0x133D, &back},
        {0x133E, &nBig},     {0x133F, &c2},       {0x1341, &n1},      {0xF019, &n0},       {0x1340, &image0},
        {0x1342, &f1},       {0x1342, &other},    {0x1342, &f2},      {0x55, &f1},         {0x1342, &f3},
        {0x1342, &f4},       {0x1342, &f5},       {0x1342, &f6},      {0x131C, &nFont1},   {0x1305, &nFile},
    };
    WrPair second[] = {
        {0x1305, &nBackdir}, {0x130C, &nSort0},  {0x130D, &nSort3}, {0x130D, &nBad},    {0x1312, &nDir0},
        {0x1319, &nName},    {0x131A, &nUnused}, {0x131C, &fontText}, {0x1323, &nUnused}, {0x1324, &nFont1},
        {0x1326, &c1},       {0x132B, &nF006},   {0x132C, &back},   {0x1332, &c6},      {0x1333, &nBad},
        {0x1337, &n1},       {0x1339, &n0},      {0x1341, &n0},     {0xF019, &n5},      {0x1340, &imageBad},
        {0x1342, &f2},       {0x1301, &other},   {0x1306, &nDir},
    };
    WrPair third[] = {
        {0x1305, &nBad}, {0x1312, &nDir2}, {0x1319, &nBad},  {0x131C, &other}, {0x1340, &image1},
        {0x1342, &f1},   {0x1342, &f6},    {0x133F, &cWord}, {0x1323, &nF006}, {0x133D, &path},
    };
    WrPair fourth[] = {{0x1342, &f1}, {0x1342, &f2}, {0x1340, &n1}, {0x1301, &back}, {0x132C, &nFont0}};

    for (WrWindow& window : windows) {
        for (uint32_t set : {1u, 0xFFFFFF01u, 0u, 0x100u}) {
            for (uint32_t grid : {1u, 0u}) {
                face.grid = grid;
                FbRead(window, full, std::size(full), set);
                FbRead(window, second, std::size(second), set);
                FbRead(window, third, std::size(third), set);
                FbRead(window, fourth, std::size(fourth), set);
                FbRead(window, full, 0, set);
            }
        }
    }
    Mix(face.count);
    Mix(g_wrImages[0].count);
    Mix(g_wrImages[1].count);
    Mix(held.count);

    Virtual<uint32_t>(holder, 0x28, old);
    if (old)
        Virtual<uint32_t>(old, 8);
    CallThis<uint32_t>(addr::SC4RefPtr_Destruct, &holder);
    FbStage("read");
}

// OPENSC4_TEST_FILEBROWSER=<day>: part u14 by address. Every result goes
// into the fingerprint's `fb` part.
void DriveFileBrowserDay(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_FILEBROWSER") || !game::IsBuild641())
        return;
    g_fbOn = true;
    uint32_t* const saved = g_into;
    g_into = &g_fbHash;
    DriveFbRegister();
    DriveFbRead();
    g_into = saved;
    log::Write("autotest: filebrowser driven (hash %08X)", g_fbHash);
}

void FbFingerprint(cISC4City*, std::string& line)
{
    if (!g_fbOn)
        return;
    char text[32];
    std::snprintf(text, sizeof text, " | fb %08X", g_fbHash);
    line += text;
}

const FingerprintPart kFbFingerprint{"filebrowser", 4, &FbFingerprint};
const DayHook kFbDrive{"filebrowser", 4, &DriveFileBrowserDay};
// ====================================================================================
// Part u15: GZWinOptGrp's registration and property handler, the base
// window's property handler and the LEGACY reader's dispatcher
// (OPENSC4_TEST_OPTGRP). The four called by address (registers checked
// kept after): the registration with part u13's variable table of our own;
// the two handlers on windows and an interface of our own (every slot they
// call hashes its number and what it gets), with part u13's values, a held
// object giving images and texts, and the game's font and colour services;
// the dispatcher on a reader of our own (a parent of our own at +0x14) with
// real windows of the game's classes and a handler of our own put in the
// handler map. Addresses never go into the hash.

bool g_ogOn = false;
uint32_t g_ogHash = 2166136261u;

void OgStage(const char* name) { log::Write("autotest: optgrp: %s %08X", name, g_ogHash); }

void DriveOgRegister()
{
    void** const tableAt = reinterpret_cast<void**>(addr::cGZUIScriptLegacy_Table);
    void* const old = *tableAt;
    Mix(old ? 1u : 0u);
    void* table[1] = {WrTableTable()};
    *tableAt = table;
    const uint32_t before = g_wrAdds;
    // EAX is the game's free's: not hashed.
    PmRegs regs = PmRegsOf(0x15151515u, kPmEsi, kPmEdi, 0xA0A0A0A0u, 0xA1A1A1A1u);
    PmCall(addr::cGZUIScriptWidgets_RegisterOptGrp, regs);
    Mix(g_wrAdds - before);
    Mix(WrMapFind(addr::cGZUIScriptWidgets_Handlers, 0xA1336CC0));
    Mix(WrMapFind(addr::cGZUIScriptWidgets_Interfaces, 0xA1336CC1));
    *tableAt = old;
    OgStage("reg");
}

// The argument slot the base handler's bools are pushed over (its third),
// the list object it keeps there.
uint32_t g_ogSlot = 0;

// The window: slot 0 (part u13's QueryInterface) and the slots the two
// handlers call, each hashing its number and what it gets: the bools' and
// colour bytes' upper bytes only as which they are (clean, the argument
// slot's address), slot 114 every value (the text list's texts).
uint32_t __fastcall OgWinWord(WrWindow*, void*, uint32_t word)
{
    Mix(0x0C000040u);
    Mix(word);
    return 0x00C00040u;
}

template <uint32_t Slot>
uint32_t __fastcall OgWinPair(WrWindow*, void*, uint32_t first, uint32_t second)
{
    Mix(0x0C000000u | Slot);
    Mix(first);
    Mix(second);
    return 0x00C00000u | Slot;
}

uint32_t __fastcall OgWinArea(WrWindow*, void*, const int32_t* rect)
{
    Mix(0x0C000036u);
    for (int i = 0; i < 4; ++i)
        Mix(static_cast<uint32_t>(rect[i]));
    return 0x00C00036u;
}

uint32_t OgUpper(uint32_t value)
{
    const uint32_t upper = value & ~0xFFu;
    if (upper == 0)
        return 0xC0u;
    return upper == (g_ogSlot & ~0xFFu) ? 0xC1u : 0xC2u;
}

uint32_t __fastcall OgWinFlag(WrWindow*, void*, uint32_t mask, uint32_t set)
{
    Mix(0x0C000044u);
    Mix(mask);
    Mix(set & 0xFF);
    Mix(OgUpper(set));
    return 0x00C00044u;
}

uint32_t __fastcall OgWinCaption(WrWindow* self, void*, char* string)
{
    Mix(0x0C00004Au);
    Mix(self->mode);
    PrMixText(string);
    return 0x00C0004Au;
}

uint32_t __fastcall OgWinColour(WrWindow*, void*, uint32_t red, uint32_t green, uint32_t blue)
{
    Mix(0x0C00006Bu);
    Mix(red & 0xFF);
    Mix(green & 0xFF);
    Mix(blue & 0xFF);
    return 0x00C0006Bu;
}

uint32_t __fastcall OgWinPut(WrWindow* self, void* edx, uint32_t key, char* value)
{
    if (key == 0xBACA6879u && value && g_ogSlot) {
        // The text list (in the argument slot): its strings' texts, and
        // which of the value's words holds it.
        char* const list = *reinterpret_cast<char**>(static_cast<std::uintptr_t>(g_ogSlot));
        Mix(0x11461u);
        Mix(At<uint16_t>(value, 0xE));
        uint32_t where = 0xFF;
        for (uint32_t offset = 4; offset < 0x14; offset += 4) {
            if (At<char*>(value, offset) == list)
                where = offset;
        }
        Mix(where);
        if (list) {
            Mix(At<std::uintptr_t>(list, 0) == addr::cGZUIScriptWidgets_BaseListVtable ? 0x7AB1u : 0x7AB0u);
            char* const begin = At<char*>(list, 4);
            char* const end = At<char*>(list, 8);
            Mix(static_cast<uint32_t>(end - begin));
            for (char* string = begin; string < end; string += 0x14)
                PrMixText(string);
            Mix(At<uint32_t>(list, 0x10));
        }
        // The handler releases this one itself (an AddRef before).
        Mix(self->mode);
        Mix(key);
        return 0x00C11461u;
    }
    Mix(self->mode);
    return PmWindowPut(self, edx, key, value);
}

void** OgWindowTable()
{
    static void* table[115] = {};
    table[0] = reinterpret_cast<void*>(&WrWindowQuery);
    table[53] = reinterpret_cast<void*>(&OgWinPair<53>);
    table[54] = reinterpret_cast<void*>(&OgWinArea);
    table[56] = reinterpret_cast<void*>(&OgWinPair<56>);
    table[64] = reinterpret_cast<void*>(&OgWinWord);
    table[68] = reinterpret_cast<void*>(&OgWinFlag);
    table[74] = reinterpret_cast<void*>(&OgWinCaption);
    table[107] = reinterpret_cast<void*>(&OgWinColour);
    table[114] = reinterpret_cast<void*>(&OgWinPut);
    return table;
}

// The held object: part u13's images (slot 3) and part u12's texts (slot
// 4, for odd instances).
void** OgHeldTable()
{
    static void* table[5] = {};
    table[1] = reinterpret_cast<void*>(&PmHeldAddRef);
    table[2] = reinterpret_cast<void*>(&PmHeldRelease);
    table[3] = reinterpret_cast<void*>(&WrHeldImage);
    table[4] = reinterpret_cast<void*>(&PmHeldText);
    return table;
}

// PmCallRaw, with the address of the third argument's slot (as pushed)
// kept in g_ogSlot for the window's slots.
__declspec(naked) void __cdecl OgCallRaw(PmRegs*)
{
    __asm push ebx
    __asm push esi
    __asm push edi
    __asm push ebp
    __asm mov ebp, [esp + 20]
    __asm push ebp
    __asm push dword ptr [ebp + 28]
    __asm push dword ptr [ebp + 24]
    __asm push dword ptr [ebp + 20]
    __asm push dword ptr [ebp + 16]
    __asm lea eax, [esp + 8]
    __asm mov g_ogSlot, eax
    __asm mov eax, [ebp + 0]
    __asm mov ebx, [ebp + 4]
    __asm mov esi, [ebp + 8]
    __asm mov edi, [ebp + 12]
    __asm call dword ptr [ebp + 52]
    __asm push eax
    __asm mov eax, [esp + 20]
    __asm mov [eax + 36], ebx
    __asm mov [eax + 40], esi
    __asm mov [eax + 44], edi
    __asm mov [eax + 48], ebp
    __asm mov [eax + 56], edx
    __asm pop dword ptr [eax + 32]
    __asm mov ebx, [esp]
    __asm mov [eax + 16], ebx
    __asm mov ebx, [esp + 4]
    __asm mov [eax + 20], ebx
    __asm mov ebx, [esp + 8]
    __asm mov [eax + 24], ebx
    __asm mov ebx, [esp + 12]
    __asm mov [eax + 28], ebx
    __asm add esp, 20
    __asm pop ebp
    __asm pop edi
    __asm pop esi
    __asm pop ebx
    __asm ret
}

// The base handler: one call (its EAX not hashed: a free's or a slot's of
// ours; the argument slots after).
void OgBase(WrWindow& window, WrPair* pairs, std::size_t count, uint32_t set)
{
    char* vector[3] = {reinterpret_cast<char*>(pairs), reinterpret_cast<char*>(pairs + count), nullptr};
    PmRegs regs = PmRegsOf(0x28282828u, kPmEsi, kPmEdi, PrArg(&window), PrArg(vector));
    regs.args[2] = set;
    regs.address = addr::cGZUIScriptWidgets_BaseRead;
    OgCallRaw(&regs);
    g_ogSlot = 0;
    const bool kept = regs.outEbx == regs.ebx && regs.outEsi == regs.esi && regs.outEdi == regs.edi &&
                      regs.outEbp == PrArg(&regs);
    Mix(kept ? 0x4B1u : 0xBAD0u);
    Mix(regs.args[0] == PrArg(&window) ? 0x77DDu : 0xBAD0u);
    Mix(regs.args[1] == PrArg(vector) ? 0x77DEu : 0xBAD1u);
    // The third: the byte, a found word, or the (released) list object.
    Mix((set & 0xFF) ? 0x77DFu : regs.args[2]);
}

void DriveOgBase()
{
    void* holder = nullptr;
    CallThis<void*>(addr::SC4RefPtr_UIScriptLoader, &holder);
    Mix(holder ? 1u : 0u);
    if (!holder) {
        OgStage("base");
        return;
    }
    void* const old = Virtual<void*>(holder, 0x24);
    if (old)
        Virtual<uint32_t>(old, 4);
    PmHeld held = {OgHeldTable(), 0};
    Virtual<uint32_t>(holder, 0x28, static_cast<void*>(&held));

    WrWindow window = {OgWindowTable(), 7, nullptr};
    WrValue n0 = WrNumber(0), n1 = WrNumber(1), n5 = WrNumber(5), nBig = WrNumber(0xFFFFFFFFu),
            user = WrNumber(0x12345678), other = {WrValueTable(), 9, 1, "(1,2)"};
    WrValue flagText = WrText("1"), area = WrText("( 1, 2, 3, 4)"), areaTight = WrText("(-5,6,-7,8)"),
            pairGood = WrText("(5,6)"), pairNeg = WrText("(-7,8)"), pairBad = WrText("(1)"), c1 = WrText("(1,2,3)"),
            c2 = WrText("(300,-1,256)"), cShort = WrText("(4,5)"), caption = WrText("Hello"),
            resOdd = WrText("{11,13}"), resEven = WrText("{1a2b,3c4c}"), resBad = WrText("{zz}"), t1 = WrText("one"),
            t2 = WrText("two"), t3 = WrText("three"), t8 = WrText("eight"), empty = WrText("");
    WrPair full[] = {
        {0xF01A, &n1},  {0xF01C, &n0},       {0xF01D, &n5},     {0xF01E, &flagText}, {0xF025, &n1},
        {0xF026, &n0},  {0xF023, &n1},       {0xF024, &nBig},   {0x100, &user},      {0x101, &area},
        {0x102, &pairGood}, {0x104, &c1},    {0x105, &caption}, {0x106, &resOdd},    {0x107, &n0},
        {0x108, &t1},   {0x10A, &t3},        {0x10F, &t8},      {0xF01B, &n1},       {0xF022, &n5},
    };
    WrPair second[] = {
        {0x102, &pairGood}, {0x103, &pairNeg}, {0x104, &c2},  {0x106, &resEven}, {0x107, &n1},
        {0x109, &t2},       {0xF01A, &n0},     {0x100, &other}, {0x105, &empty}, {0xF020, &n1},
    };
    WrPair third[] = {
        {0x106, &resBad}, {0x108, &t1}, {0x109, &t2}, {0x10A, &t3}, {0x10B, &t1}, {0x10C, &t2},
        {0x10D, &t3},     {0x10E, &t8}, {0x10F, &t1}, {0x103, &pairBad}, {0x104, &cShort}, {0x101, &areaTight},
    };
    WrPair clear[] = {
        {0xF01B, &n1}, {0xF01F, &n0}, {0xF020, &n5}, {0xF021, &flagText}, {0xF022, &n1}, {0xF01A, &n1},
    };
    for (uint32_t set : {1u, 0xFFFFFF01u, 0u, 0x100u}) {
        OgBase(window, full, std::size(full), set);
        OgBase(window, second, std::size(second), set);
        OgBase(window, third, std::size(third), set);
        OgBase(window, clear, std::size(clear), set);
        OgBase(window, full, 0, set);
    }
    Mix(held.count);
    Virtual<uint32_t>(holder, 0x28, old);
    if (old)
        Virtual<uint32_t>(old, 8);
    CallThis<uint32_t>(addr::SC4RefPtr_Destruct, &holder);
    OgStage("base");
}

// The interface (IGZWinOptGrp): AddRef, Release and the slots the handler
// calls, each hashing its number and what it gets. Slot 3 gives a window
// of our own; slot 19 an item of our own for even ids (its slot 3 the same
// window, its Release counted).
struct OgFace {
    void** vtable;
    uint32_t count;
    WrWindow* own;
};

struct OgItem {
    void** vtable;
    uint32_t count;
    WrWindow* own;
};

uint32_t __fastcall OgFaceAddRef(OgFace* self, void*) { return ++self->count; }

uint32_t __fastcall OgFaceRelease(OgFace* self, void*)
{
    Mix(0x0F2F2u);
    return 0x00F00000u + --self->count;
}

WrWindow* __fastcall OgFaceWindow(OgFace* self, void*)
{
    Mix(0x0F003u);
    return self->own;
}

template <uint32_t Slot>
uint32_t __fastcall OgSet1(OgFace*, void*, uint32_t a)
{
    Mix(0x0F000000u | Slot);
    Mix(a);
    return 0x00F00000u | Slot;
}

template <uint32_t Slot>
uint32_t __fastcall OgSet2(OgFace*, void*, uint32_t a, uint32_t b)
{
    Mix(0x0F000000u | Slot);
    Mix(a);
    Mix(b);
    return 0x00F00000u | Slot;
}

template <uint32_t Slot>
uint32_t __fastcall OgSet3(OgFace*, void*, uint32_t a, uint32_t b, uint32_t c)
{
    Mix(0x0F000000u | Slot);
    Mix(a);
    Mix(b);
    Mix(c);
    return 0x00F00000u | Slot;
}

template <uint32_t Slot>
uint32_t __fastcall OgFont(OgFace*, void*, void* font)
{
    Mix(0x0F000000u | Slot);
    uint32_t which = 0xFF;
    for (uint32_t i = 0; i < 3; ++i) {
        if (font == g_fbFonts[i])
            which = i;
    }
    Mix(which);
    Mix(font ? 1u : 0u);
    return 0x00F00000u | Slot;
}

uint32_t __fastcall OgFaceAdd(OgFace*, void*, uint32_t id, char* name, uint32_t number, WrImage* image,
                              const int32_t* rect)
{
    Mix(0x0F00000Fu);
    Mix(id);
    PrMixText(name);
    Mix(number);
    Mix(image ? image->tag : 0xDEADu);
    for (int i = 0; i < 4; ++i)
        Mix(static_cast<uint32_t>(rect[i]));
    return 0x00F0000Fu;
}

uint32_t __fastcall OgItemRelease(OgItem* self, void*)
{
    Mix(0x0F1F2u);
    return --self->count;
}

WrWindow* __fastcall OgItemWindow(OgItem* self, void*)
{
    Mix(0x0F103u);
    return self->own;
}

void** OgItemTable()
{
    static void* table[4] = {};
    table[2] = reinterpret_cast<void*>(&OgItemRelease);
    table[3] = reinterpret_cast<void*>(&OgItemWindow);
    return table;
}

OgItem g_ogItem = {};

uint32_t __fastcall OgFaceItem(OgFace*, void*, uint32_t id, OgItem** item)
{
    Mix(0x0F000013u);
    Mix(id);
    if (id & 1)
        return 0x00F01300u;
    *item = &g_ogItem;
    ++g_ogItem.count;
    return 0x00F01301u;
}

uint32_t __fastcall OgFaceImage(OgFace*, void*, WrImage* image)
{
    Mix(0x0F000014u);
    Mix(image ? image->tag : 0xDEADu);
    return 0x00F00014u;
}

uint32_t __fastcall OgFaceColours(OgFace*, void*, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    Mix(0x0F000015u);
    Mix(a);
    Mix(b);
    Mix(c);
    Mix(d);
    return 0x00F00015u;
}

uint32_t __fastcall OgFaceFill(OgFace*, void*)
{
    Mix(0x0F000018u);
    return 0x00F00018u;
}

void** OgFaceTable()
{
    static void* table[29] = {};
    table[1] = reinterpret_cast<void*>(&OgFaceAddRef);
    table[2] = reinterpret_cast<void*>(&OgFaceRelease);
    table[3] = reinterpret_cast<void*>(&OgFaceWindow);
    table[5] = reinterpret_cast<void*>(&OgSet2<5>);
    table[6] = reinterpret_cast<void*>(&OgSet2<6>);
    table[8] = reinterpret_cast<void*>(&OgFont<8>);
    table[9] = reinterpret_cast<void*>(&OgFont<9>);
    table[10] = reinterpret_cast<void*>(&OgSet2<10>);
    table[14] = reinterpret_cast<void*>(&OgSet1<14>);
    table[15] = reinterpret_cast<void*>(&OgFaceAdd);
    table[17] = reinterpret_cast<void*>(&OgSet3<17>);
    table[18] = reinterpret_cast<void*>(&OgSet3<18>);
    table[19] = reinterpret_cast<void*>(&OgFaceItem);
    table[20] = reinterpret_cast<void*>(&OgFaceImage);
    table[21] = reinterpret_cast<void*>(&OgFaceColours);
    table[24] = reinterpret_cast<void*>(&OgFaceFill);
    table[28] = reinterpret_cast<void*>(&OgSet2<28>);
    return table;
}

// One call of the OptGrp handler: its EAX and argument slots after (the
// first may hold the colour service or a word).
void OgRead(WrWindow& window, WrPair* pairs, std::size_t count, uint32_t set, void* colours)
{
    char* vector[3] = {reinterpret_cast<char*>(pairs), reinterpret_cast<char*>(pairs + count), nullptr};
    PmRegs regs = PmRegsOf(0x29292929u, kPmEsi, kPmEdi, PrArg(&window), PrArg(vector));
    regs.args[2] = set;
    Mix(PmCall(addr::cGZUIScriptWidgets_OptGrpRead, regs));
    if (regs.args[0] == PrArg(&window))
        Mix(0x77DDu);
    else if (regs.args[0] == PrArg(colours))
        Mix(0x77DCu);
    else
        Mix(regs.args[0]);
    Mix(regs.args[1] == PrArg(vector) ? 0x77DEu : 0xBAD1u);
    Mix(regs.args[2]);
}

void DriveOgRead()
{
    void* holder = nullptr;
    CallThis<void*>(addr::SC4RefPtr_UIScriptLoader, &holder);
    Mix(holder ? 1u : 0u);
    void* const fonts = reinterpret_cast<void*(__cdecl*)()>(addr::SC4_Singleton913C72)();
    void* const colours = reinterpret_cast<void*(__cdecl*)()>(addr::SC4_Singleton913C1A)();
    Mix(fonts ? 1u : 0u);
    Mix(colours ? 1u : 0u);
    if (!holder || !fonts || !colours) {
        OgStage("read");
        return;
    }
    for (uint32_t i = 0; i < 3; ++i)
        g_fbFonts[i] = Virtual<void*>(fonts, 0x14, kFbFontWords[i]);
    void* const old = Virtual<void*>(holder, 0x24);
    if (old)
        Virtual<uint32_t>(old, 4);
    PmHeld held = {OgHeldTable(), 0};
    Virtual<uint32_t>(holder, 0x28, static_cast<void*>(&held));
    g_wrImages[0] = {WrImageTable(), 0xC0, 0};
    g_wrImages[1] = {WrImageTable(), 0xC1, 0};

    WrWindow own = {OgWindowTable(), 9, nullptr};
    OgFace face = {OgFaceTable(), 1, &own};
    g_ogItem = {OgItemTable(), 0, &own};
    WrWindow windows[3] = {{OgWindowTable(), 0, &face}, {OgWindowTable(), 1, &face}, {OgWindowTable(), 2, &face}};

    WrValue n0 = WrNumber(0), n1 = WrNumber(1), n2 = WrNumber(2), n7 = WrNumber(7), nAuto = WrNumber(0xF0E),
            nManual = WrNumber(0xF0F), n40 = WrNumber(40), nBig = WrNumber(0xFFFFFFFFu),
            nFont0 = WrNumber(kFbFontWords[0]), nFont1 = WrNumber(kFbFontWords[1]),
            other = {WrValueTable(), 9, 1, "(1,2,3)"};
    WrValue pairGood = WrText("(5,-6)"), pairBad = WrText("(5)"), fontText = WrText("font text"),
            c1 = WrText("(1,2,3)"), c2 = WrText("(255,128,0)"), c3 = WrText("(300,-1,256)"), cShort = WrText("(4,5)"),
            cWord = WrText("red"), resOdd = WrText("{11,13}"), resEven = WrText("{1a2b,3c4c}"),
            image0 = WrText("{1a2b,3c4c}"), image1 = WrText("{11,13}"), imageBad = WrText("{zz}"),
            four = WrText("(1,2,3)(4,5,6)(7,8,9)(10,11,12)"), fourBig = WrText("(255,0,0)(0,255,0)(0,0,255)(300,-1,7)"),
            fourShort = WrText("(1,2,3)(4,5,6)(7,8,9)");
    WrPair full[] = {
        {0xF00, &n1},     {0xF01, &n0},      {0xF02, &n2},      {0xF019, &n1},     {0xF004, &pairGood},
        {0xF03, &nFont0}, {0xF04, &fontText}, {0xF05, &c1},     {0xF06, &c2},      {0xF07, &c3},
        {0xF08, &cShort}, {0xF09, &cWord},   {0xF0A, &resOdd},  {0xF0B, &image0},  {0x40B, &four},
        {0xF0C, &nAuto},  {0xF10, &n40},     {0xF00, &other},
    };
    WrPair second[] = {
        {0xF01, &n1},       {0xF004, &pairBad}, {0xF03, &fontText}, {0xF04, &nFont1}, {0xF09, &c2},
        {0xF0A, &resEven},  {0xF0B, &imageBad}, {0x40B, &fourShort}, {0xF0C, &nManual}, {0xF019, &nBig},
    };
    WrPair third[] = {
        {0xF0B, &image1}, {0x40B, &fourBig}, {0xF0C, &nAuto}, {0xF05, &cShort}, {0xF02, &n7},
    };
    WrPair fourth[] = {{0xF0C, &n0}, {0xF03, &other}, {0x40B, &cWord}};

    // The raw pass's: options (the first of each vector with a good id
    // and number: the game's start as its stack held them), moves, sizes.
    WrValue o1 = WrText("2:First:7:{11,13}:(1,2,3,4)"), o2 = WrText("3:Second:-1:{1a2b,3c4c}:(5,6)"),
            o3 = WrText("x:Third:y:{zz}:(-1,-2,-3,-4)"), o4 = WrText("4:Fourth:9"), o5 = WrText("6::0:{2,6}:()"),
            o6 = WrText("8:Last:1:{11,13}:(9,9,9,9):extra"), m1 = WrText("2:(10,20)"), m2 = WrText("3:(-1,-2)"),
            m3 = WrText("z:(5,5)"), m4 = WrText("4:(7)"), s1 = WrText("2:(30,40)"), s2 = WrText("6:(1,1)");
    WrPair raw[] = {
        {0xF0D, &o1}, {0xF11, &m1}, {0xF0D, &o2}, {0xF12, &s1}, {0xF0D, &o3}, {0xF0D, &n1}, {0xF0D, &o4},
        {0xF11, &m2}, {0xF0D, &o5}, {0x55, &o1},  {0xF0D, &o6}, {0xF11, &m3}, {0xF12, &s2}, {0xF11, &m4},
        {0x501, &n7}, {0x304, &n0},
    };
    WrPair rawSecond[] = {{0xF0D, &o6}, {0xF12, &s1}, {0x501, &other}, {0xF11, &n1}};
    WrPair rawThird[] = {{0x501, &n40}, {0x304, &n1}, {0xF0D, &o2}};

    for (WrWindow& window : windows) {
        for (uint32_t set : {1u, 0xFFFFFF01u}) {
            OgRead(window, full, std::size(full), set, colours);
            OgRead(window, second, std::size(second), set, colours);
            OgRead(window, third, std::size(third), set, colours);
            OgRead(window, fourth, std::size(fourth), set, colours);
            OgRead(window, full, 0, set, colours);
        }
        for (uint32_t set : {0u, 0x100u}) {
            OgRead(window, raw, std::size(raw), set, colours);
            OgRead(window, rawSecond, std::size(rawSecond), set, colours);
            OgRead(window, rawThird, std::size(rawThird), set, colours);
            OgRead(window, raw, 0, set, colours);
        }
    }
    Mix(face.count);
    Mix(g_ogItem.count);
    Mix(g_wrImages[0].count);
    Mix(g_wrImages[1].count);
    Mix(held.count);

    Virtual<uint32_t>(holder, 0x28, old);
    if (old)
        Virtual<uint32_t>(old, 8);
    CallThis<uint32_t>(addr::SC4RefPtr_Destruct, &holder);
    OgStage("read");
}

// The dispatcher (thiscall, `ret 4`): ECX the reader, one stack argument;
// EAX and the registers back.
struct OgThis {
    uint32_t ecx, arg, outEax, outEbx, outEsi, outEdi, outEbp;
    std::uintptr_t address;
    uint32_t ebx, esi, edi;
};

__declspec(naked) void __cdecl OgCallThisRaw(OgThis*)
{
    __asm push ebx
    __asm push esi
    __asm push edi
    __asm push ebp
    __asm mov ebp, [esp + 20]
    __asm push ebp
    __asm push dword ptr [ebp + 4]
    __asm mov ecx, [ebp + 0]
    __asm mov ebx, [ebp + 32]
    __asm mov esi, [ebp + 36]
    __asm mov edi, [ebp + 40]
    __asm call dword ptr [ebp + 28]
    __asm mov ecx, [esp]
    __asm mov [ecx + 8], eax
    __asm mov [ecx + 12], ebx
    __asm mov [ecx + 16], esi
    __asm mov [ecx + 20], edi
    __asm mov [ecx + 24], ebp
    __asm add esp, 4
    __asm pop ebp
    __asm pop edi
    __asm pop esi
    __asm pop ebx
    __asm ret
}

// The parent we give the reader (+0x14): AddRef, Release (counted) and
// slot 14 (a child: whether it is the reader's window).
struct OgParent {
    void** vtable;
    uint32_t count;
    char* reader;
    uint32_t children;
};

uint32_t __fastcall OgParentAddRef(OgParent* self, void*) { return ++self->count; }

uint32_t __fastcall OgParentRelease(OgParent* self, void*)
{
    Mix(0x9A2u);
    return --self->count;
}

uint32_t __fastcall OgParentChild(OgParent* self, void*, void* child)
{
    Mix(0x9A14u);
    Mix(child && child == At<void*>(self->reader, 0x10) ? 1u : 0u);
    ++self->children;
    return 0x9A140000u;
}

void** OgParentTable()
{
    static void* table[15] = {};
    table[1] = reinterpret_cast<void*>(&OgParentAddRef);
    table[2] = reinterpret_cast<void*>(&OgParentRelease);
    table[14] = reinterpret_cast<void*>(&OgParentChild);
    return table;
}

// The handler of our own (in the handler map under kOgFace): which pass,
// whether it got the reader's window and vector.
constexpr uint32_t kOgFace = 0x7E570001u;
char* g_ogReader = nullptr;
char* const* g_ogVector = nullptr;

uint32_t __cdecl OgHandler(void* window, char* const* vector, uint32_t set)
{
    Mix(0x4A4D0000u | (set & 0xFFFF));
    Mix(window && window == At<void*>(g_ogReader, 0x10) ? 1u : 0u);
    Mix(vector == g_ogVector ? 1u : 0u);
    Mix(At<uint8_t>(g_ogReader, 0x1C));
    return 0x4A4Du;
}

// One call: a reader of our own (+0x10 a mark, +0x14 our parent, +0x18
// none, +0x1C 0), the vector; AL, the reader's fields, the parent's
// calls; the window made released.
void OgDispatch(WrPair* pairs, std::size_t count)
{
    char* vector[3] = {reinterpret_cast<char*>(pairs), reinterpret_cast<char*>(pairs + count), nullptr};
    alignas(4) char reader[0x20] = {};
    OgParent parent = {OgParentTable(), 1, reader, 0};
    At<uint32_t>(reader, 0x10) = 0x5EEDu;
    At<void*>(reader, 0x14) = &parent;
    g_ogReader = reader;
    g_ogVector = vector;
    OgThis call = {PrArg(reader), PrArg(vector), 0, 0, 0, 0, 0, addr::cGZUIScriptLegacy_Dispatch,
                   kPmEbx, kPmEsi, kPmEdi};
    OgCallThisRaw(&call);
    const bool kept =
        call.outEbx == kPmEbx && call.outEsi == kPmEsi && call.outEdi == kPmEdi && call.outEbp == PrArg(&call);
    Mix(kept ? 0x4B1u : 0xBAD0u);
    Mix(call.outEax & 0xFF);
    void* const window = At<void*>(reader, 0x10);
    const bool made = window && At<uint32_t>(reader, 0x10) != 0x5EEDu;
    Mix(made ? 1u : At<uint32_t>(reader, 0x10));
    Mix(At<void*>(reader, 0x14) == &parent ? 1u : At<void*>(reader, 0x14) ? 2u : 0u);
    Mix(At<uint32_t>(reader, 0x18));
    Mix(At<uint8_t>(reader, 0x1C));
    Mix(parent.count);
    Mix(parent.children);
    if (made)
        Virtual<uint32_t>(window, 8);
    g_ogReader = nullptr;
}

void DriveOgDispatch()
{
    // Our handler under a word of our own (the map's own insert; a second
    // run finds it there).
    const uint32_t pair[2] = {kOgFace, PrArg(reinterpret_cast<void*>(&OgHandler))};
    uint32_t result[2];
    CallThis<uint32_t>(addr::cGZUIScriptWidgets_HandlerInsert, reinterpret_cast<void*>(addr::cGZUIScriptWidgets_Handlers),
                       static_cast<void*>(result), static_cast<const void*>(pair));
    Mix(WrMapFind(addr::cGZUIScriptWidgets_Handlers, kOgFace) == pair[1] ? 1u : 0u);

    // Classes: GZWinFlatRect 0xC2AFA76E (interface 0xC2AFA76F), GZWinCustom
    // 0x478D1E6F, one no factory makes. (A GZWinGen window made and released
    // here, never given a parent, leaves the game hanging when it quits.)
    WrValue klassFlat = WrNumber(0xC2AFA76E), klassCustom = WrNumber(0x478D1E6F), klassNone = WrNumber(0x7E57BAD1),
            klass0 = WrNumber(0), faceOurs = WrNumber(kOgFace), faceFlat = WrNumber(0xC2AFA76F),
            faceNone = WrNumber(0x7E57BAD2), text = WrText("(1,2)"), n1 = WrNumber(1);
    WrPair none[] = {{0xF01A, &n1}};
    WrPair zero[] = {{0xFFFFFFFF, &klass0}, {0xFFFFFFFE, &faceOurs}};
    WrPair notNumber[] = {{0xFFFFFFFF, &text}};
    WrPair flatOurs[] = {{0xFFFFFFFF, &klassFlat}, {0xFFFFFFFE, &faceOurs}, {0xF01A, &n1}};
    WrPair byFace[] = {{0xFFFFFFFE, &faceFlat}, {0xFFFFFFFF, &klassNone}};
    WrPair byDefault[] = {{0xFFFFFFFF, &klassNone}};
    WrPair byDefaultFace[] = {{0xFFFFFFFF, &klassNone}, {0xFFFFFFFE, &faceNone}};
    WrPair flatMap[] = {{0xFFFFFFFF, &klassFlat}, {0xF01A, &n1}};
    WrPair customOurs[] = {{0xFFFFFFFE, &faceOurs}, {0xFFFFFFFF, &klassCustom}};

    OgDispatch(none, 0);
    OgDispatch(none, std::size(none));
    OgDispatch(zero, std::size(zero));
    OgDispatch(notNumber, std::size(notNumber));
    OgDispatch(flatOurs, std::size(flatOurs));
    OgDispatch(byFace, std::size(byFace));
    OgDispatch(byDefault, std::size(byDefault));
    OgDispatch(byDefaultFace, std::size(byDefaultFace));
    OgDispatch(flatMap, std::size(flatMap));
    OgDispatch(customOurs, std::size(customOurs));
    OgStage("disp");
}

// OPENSC4_TEST_OPTGRP=<day>: part u15 by address. Every result goes into
// the fingerprint's `og` part.
void DriveOptGrpDay(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_OPTGRP") || !game::IsBuild641())
        return;
    g_ogOn = true;
    uint32_t* const saved = g_into;
    g_into = &g_ogHash;
    DriveOgRegister();
    DriveOgBase();
    DriveOgRead();
    DriveOgDispatch();
    g_into = saved;
    log::Write("autotest: optgrp driven (hash %08X)", g_ogHash);
}

void OgFingerprint(cISC4City*, std::string& line)
{
    if (!g_ogOn)
        return;
    char text[32];
    std::snprintf(text, sizeof text, " | og %08X", g_ogHash);
    line += text;
}

const FingerprintPart kOgFingerprint{"optgrp", 4, &OgFingerprint};
const DayHook kOgDrive{"optgrp", 4, &DriveOptGrpDay};

// ====================================================================================
// Part u16a: GZWinTreeView's registration and property handler
// (OPENSC4_TEST_TREEVIEW). Both called by address (registers checked kept
// after): the registration with part u13's variable table of our own; the
// handler on windows and an interface of our own (every slot it calls
// hashes its number and what it gets; slot 114 the six maps' entries, the
// game's map code's), with part u13's values, a held object giving images
// (their slot 12 a rectangle of our own) and the game's font and colour
// services, both passes. The inputs keep clear of what the game reads from
// its stack: every vector's first node names its position, the root's
// colours come only after an image pair, and a node's expanded byte is
// hashed alone (the game pushes it with the record's padding).
// Addresses never go into the hash.

bool g_tvOn = false;
uint32_t g_tvHash = 2166136261u;

void TvStage(const char* name) { log::Write("autotest: treeview: %s %08X", name, g_tvHash); }

void DriveTvRegister()
{
    void** const tableAt = reinterpret_cast<void**>(addr::cGZUIScriptLegacy_Table);
    void* const old = *tableAt;
    Mix(old ? 1u : 0u);
    void* table[1] = {WrTableTable()};
    *tableAt = table;
    const uint32_t before = g_wrAdds;
    // EAX is the game's free's: not hashed.
    PmRegs regs = PmRegsOf(0x16161616u, kPmEsi, kPmEdi, 0xA0A0A0A0u, 0xA1A1A1A1u);
    PmCall(addr::cGZUIScriptWidgets_RegisterTreeView, regs);
    Mix(g_wrAdds - before);
    Mix(WrMapFind(addr::cGZUIScriptWidgets_Handlers, 0x3AE8BAD2));
    Mix(WrMapFind(addr::cGZUIScriptWidgets_Interfaces, 0x3AE8BAE1));
    *tableAt = old;
    TvStage("reg");
}

// Which of the test's fonts a font is (0xFF another, then whether there
// is one).
void TvMixFont(void* font)
{
    uint32_t which = 0xFF;
    for (uint32_t i = 0; i < 3; ++i) {
        if (font == g_fbFonts[i])
            which = i;
    }
    Mix(which);
    Mix(font ? 1u : 0u);
}

// A font's word by name: the test's own words as they are, others only as
// being there.
void TvMixFontWord(uint32_t word)
{
    for (uint32_t known : kFbFontWords) {
        if (word == known) {
            Mix(word);
            return;
        }
    }
    Mix(word ? 0xF0F1u : 0xF0F0u);
}

// The six maps the raw pass puts on the window (a number of the map's
// address): name keyed (a string at +0x10, words from +0x24) or word
// keyed (the word at +0x10, a string at +0x14).
struct TvMapKind {
    uint32_t key;
    bool byName;
    uint32_t words;
    bool fontWords;
};

const TvMapKind kTvMaps[] = {{0x46C207A5u, true, 1, false}, {0xC6C207C8u, false, 0, false},
                             {0x66C20802u, true, 2, false}, {0x86C21DA0u, true, 1, true},
                             {0xE6C21D83u, true, 1, false}, {0x26C21E35u, false, 0, false}};

void TvMixMap(char* map, const TvMapKind& kind)
{
    Mix(map ? 1u : 0u);
    if (!map)
        return;
    char* const header = At<char*>(map, 0);
    uint32_t count = 0;
    for (char* node = At<char*>(header, 8); node != header && count < 64; node = TreeNext(node), ++count) {
        if (kind.byName) {
            PrMixText(node + 0x10);
            for (uint32_t i = 0; i < kind.words; ++i) {
                if (kind.fontWords)
                    TvMixFontWord(At<uint32_t>(node, 0x24 + i * 4));
                else
                    Mix(At<uint32_t>(node, 0x24 + i * 4));
            }
        } else {
            Mix(At<uint32_t>(node, 0x10));
            PrMixText(node + 0x14);
        }
    }
    Mix(count);
}

uint32_t __fastcall TvWinPut(WrWindow* self, void* edx, uint32_t key, char* value)
{
    Mix(self->mode);
    for (const TvMapKind& kind : kTvMaps) {
        if (key != kind.key)
            continue;
        Mix(0x114700u);
        Mix(key);
        if (!value) {
            Mix(0xDEADu);
            return 0x00C11470u;
        }
        Mix(At<uint16_t>(value, 0xE));
        TvMixMap(At<char*>(value, 4), kind);
        Virtual<void>(value, 0x1FC, 1u);
        return 0x00C11470u;
    }
    return PmWindowPut(self, edx, key, value);
}

void** TvWindowTable()
{
    static void* table[115] = {};
    table[0] = reinterpret_cast<void*>(&WrWindowQuery);
    table[114] = reinterpret_cast<void*>(&TvWinPut);
    return table;
}

// The images: part u13's, with slot 12 (their own rectangle).
int32_t g_tvImageRect[4] = {3, 4, 50, 60};

int32_t* __fastcall TvImageRect(WrImage* self, void*)
{
    Mix(0x1A0Cu | (self->tag << 16));
    return g_tvImageRect;
}

void** TvImageTable()
{
    static void* table[13] = {};
    table[2] = reinterpret_cast<void*>(&WrImageRelease);
    table[12] = reinterpret_cast<void*>(&TvImageRect);
    return table;
}

// The interface (IGZWinTreeView): AddRef, Release and the slots the
// handler calls, each hashing its number and what it gets. Slot 20 gives
// image indices of its own, slot 24 nodes of its own.
struct TvFace {
    void** vtable;
    uint32_t count;
    uint32_t images;
    uint32_t nodes;
};

uint32_t __fastcall TvFaceAddRef(TvFace* self, void*) { return ++self->count; }

uint32_t __fastcall TvFaceRelease(TvFace* self, void*)
{
    Mix(0x7F2F2u);
    return 0x00F00000u + --self->count;
}

template <uint32_t Slot>
uint32_t __fastcall TvSet1(TvFace*, void*, uint32_t a)
{
    Mix(0x7F000000u | Slot);
    Mix(a);
    return 0x00F00000u | Slot;
}

template <uint32_t Slot>
uint32_t __fastcall TvSet2(TvFace*, void*, uint32_t a, uint32_t b)
{
    Mix(0x7F000000u | Slot);
    Mix(a);
    Mix(b);
    return 0x00F00000u | Slot;
}

template <uint32_t Slot>
uint32_t __fastcall TvSet3(TvFace*, void*, uint32_t a, uint32_t b, uint32_t c)
{
    Mix(0x7F000000u | Slot);
    Mix(a);
    Mix(b);
    Mix(c);
    return 0x00F00000u | Slot;
}

uint32_t __fastcall TvFaceImage(TvFace* self, void*, WrImage* image, const int32_t* rect)
{
    Mix(0x7F000014u);
    Mix(image ? image->tag : 0xDEADu);
    Mix(rect == g_tvImageRect ? 0x12EC7u : 0x12EC0u);
    for (int i = 0; i < 4; ++i)
        Mix(static_cast<uint32_t>(rect[i]));
    return 0x100u + ++self->images;
}

uint32_t __fastcall TvFaceNode(TvFace* self, void*, uint32_t parent, uint32_t position, char* text, uint32_t last)
{
    Mix(0x7F000018u);
    Mix(parent);
    Mix(position);
    PrMixText(text);
    Mix(last);
    return 0x7E000000u + ++self->nodes;
}

uint32_t __fastcall TvFaceText(TvFace*, void*, uint32_t node, char* text)
{
    Mix(0x7F000020u);
    Mix(node);
    PrMixText(text);
    return 0x00F00020u;
}

uint32_t __fastcall TvFaceFont(TvFace*, void*, uint32_t node, void* font)
{
    Mix(0x7F000027u);
    Mix(node);
    TvMixFont(font);
    return 0x00F00027u;
}

// Expanded: the root's bool whole (its upper bytes the EDX 0x0094C675
// leaves), a node's low byte only.
uint32_t __fastcall TvFaceExpand(TvFace*, void*, uint32_t node, uint32_t set)
{
    Mix(0x7F00002Du);
    Mix(node);
    Mix(node ? (set & 0xFF) : set);
    return 0x00F0002Du;
}

void** TvFaceTable()
{
    static void* table[57] = {};
    table[1] = reinterpret_cast<void*>(&TvFaceAddRef);
    table[2] = reinterpret_cast<void*>(&TvFaceRelease);
    table[3] = reinterpret_cast<void*>(&TvSet2<3>);
    table[7] = reinterpret_cast<void*>(&TvSet1<7>);
    table[9] = reinterpret_cast<void*>(&TvSet2<9>);
    table[11] = reinterpret_cast<void*>(&TvSet1<11>);
    table[13] = reinterpret_cast<void*>(&TvSet1<13>);
    table[15] = reinterpret_cast<void*>(&TvSet1<15>);
    table[17] = reinterpret_cast<void*>(&TvSet1<17>);
    table[20] = reinterpret_cast<void*>(&TvFaceImage);
    table[24] = reinterpret_cast<void*>(&TvFaceNode);
    table[32] = reinterpret_cast<void*>(&TvFaceText);
    table[36] = reinterpret_cast<void*>(&TvSet3<36>);
    table[39] = reinterpret_cast<void*>(&TvFaceFont);
    table[43] = reinterpret_cast<void*>(&TvSet3<43>);
    table[45] = reinterpret_cast<void*>(&TvFaceExpand);
    table[56] = reinterpret_cast<void*>(&TvSet2<56>);
    return table;
}

// One call of the handler: its EAX and argument slots after (the first
// may hold a found word).
void TvRead(WrWindow& window, WrPair* pairs, std::size_t count, uint32_t set)
{
    char* vector[3] = {reinterpret_cast<char*>(pairs), reinterpret_cast<char*>(pairs + count), nullptr};
    PmRegs regs = PmRegsOf(0x2A2A2A2Au, kPmEsi, kPmEdi, PrArg(&window), PrArg(vector));
    regs.args[2] = set;
    Mix(PmCall(addr::cGZUIScriptWidgets_TreeViewRead, regs));
    Mix(regs.args[0] == PrArg(&window) ? 0x77DDu : regs.args[0]);
    Mix(regs.args[1] == PrArg(vector) ? 0x77DEu : 0xBAD1u);
    Mix(regs.args[2]);
}

void DriveTvRead()
{
    void* holder = nullptr;
    CallThis<void*>(addr::SC4RefPtr_UIScriptLoader, &holder);
    Mix(holder ? 1u : 0u);
    void* const fonts = reinterpret_cast<void*(__cdecl*)()>(addr::SC4_Singleton913C72)();
    void* const colours = reinterpret_cast<void*(__cdecl*)()>(addr::SC4_Singleton913C1A)();
    Mix(fonts ? 1u : 0u);
    Mix(colours ? 1u : 0u);
    if (!holder || !fonts || !colours) {
        TvStage("read");
        return;
    }
    for (uint32_t i = 0; i < 3; ++i)
        g_fbFonts[i] = Virtual<void*>(fonts, 0x14, kFbFontWords[i]);
    void* const old = Virtual<void*>(holder, 0x24);
    if (old)
        Virtual<uint32_t>(old, 4);
    PmHeld held = {OgHeldTable(), 0};
    Virtual<uint32_t>(holder, 0x28, static_cast<void*>(&held));
    g_wrImages[0] = {TvImageTable(), 0xD0, 0};
    g_wrImages[1] = {TvImageTable(), 0xD1, 0};

    TvFace face = {TvFaceTable(), 1, 0, 0};
    WrWindow windows[3] = {{TvWindowTable(), 0, &face}, {TvWindowTable(), 1, &face}, {TvWindowTable(), 2, &face}};

    WrValue n0 = WrNumber(0), n1 = WrNumber(1), n2 = WrNumber(2), n5 = WrNumber(5), n7 = WrNumber(7),
            n40 = WrNumber(40), nBig = WrNumber(0xFFFFFFFFu), nFont0 = WrNumber(kFbFontWords[0]),
            other = {WrValueTable(), 9, 1, "(1,2,3)"};
    WrValue flagText = WrText("1"), c1 = WrText("(1,2,3)"), c2 = WrText("(255,128,0)"), c3 = WrText("(300,-1,256)"),
            cShort = WrText("(4,5)");
    WrPair full[] = {
        {0x1401, &n1}, {0x1402, &n0}, {0x1403, &n5}, {0x1404, &flagText}, {0x1405, &n7},
        {0x1336, &n40}, {0x1406, &n2}, {0x133F, &c1}, {0x408, &c2},     {0x1401, &other},
    };
    WrPair second[] = {{0x408, &cShort}, {0x1406, &nBig}, {0x133F, &c3}, {0x1404, &n1}, {0x1336, &other}};
    WrPair third[] = {{0x408, &c1}, {0x1402, &nBig}};

    // The raw pass's: images, the root and the nodes.
    WrValue i1 = WrText("folder:{1a2b,3c4c}:(0,0,0,0)"), i2 = WrText("file:{11,16}:(1,2,3,4)"),
            i3 = WrText("none:{11,13}:(5,6,7,8)"), i4 = WrText("bad:{zz}:(1,2)"), i5 = WrText("file:{1a2b,3c4e}:()"),
            rootText = WrText("My Root"), iconFolder = WrText("folder"), iconNone = WrText("unknown"),
            iconFile = WrText("file"), fontText = WrText("Some Font");
    WrValue a = WrText("a:Alpha:Root:first:folder:file:x:y:(1,2,3):(4,5,6):(7,8,9):(10,11,12):Arial:yes:no"),
            b = WrText("b:Beta:a:last:file:folder:folder:file:(1,1,1):(2,2,2):(3,3,3):(4,4,4):Courier:no:yes"),
            c = WrText("c:Gamma:Root:sorted:none:none:none:none:(9,9,9):(8,8,8):(7,7,7):(6,6,6):Times:yes:yes"),
            d = WrText("d:Delta:a:middle:folder:x:x:x:(0,0,0):(1,0,0):(0,1,0):(0,0,1):Arial:YES:yes"),
            e = WrText("e:Orphan:zzz:first:file:file:file:file:(5,5,5):(5,5,5):(5,5,5):(5,5,5):Arial:yes:no"),
            f = WrText("f:Short:b"), g = WrText("a:Again:Root:last:folder:folder:folder:folder:(1,2,3):(1,2,3):(1,2,3):"
                                               "(1,2,3):Arial:no:no"),
            h = WrText("h:Child:c:first:x:x:x:x:(3,2,1):(3,2,1):(3,2,1):(3,2,1)::yes:yes:extra");
    WrPair raw[] = {
        {0xF016, &i1},     {0xF016, &i2},      {0x1102, &a},          {0xF016, &i3},       {0xF016, &i4},
        {0xF016, &n1},     {0x1407, &rootText}, {0x1408, &iconFolder}, {0x1409, &iconNone}, {0x140B, &iconFile},
        {0x140C, &c1},     {0x140E, &c3},      {0x140F, &cShort},     {0x1410, &nFont0},   {0x1411, &n1},
        {0x1412, &n1},     {0x1102, &b},       {0x1102, &c},          {0x1102, &d},        {0x1102, &e},
        {0x1102, &f},      {0x1102, &n2},      {0x1102, &h},
    };
    WrPair rawSecond[] = {
        {0x1410, &fontText}, {0x1411, &n0}, {0x1412, &n2}, {0x1407, &other}, {0x1408, &iconFile},
        {0xF016, &i5},       {0x140D, &c2}, {0x1102, &g},  {0x1102, &a},     {0x1102, &b},
    };
    WrPair rawThird[] = {{0x1411, &nBig}, {0x1412, &n1}, {0x1410, &other}, {0x1102, &c}, {0x1102, &h}};

    for (WrWindow& window : windows) {
        for (uint32_t set : {1u, 0xFFFFFF01u}) {
            TvRead(window, full, std::size(full), set);
            TvRead(window, second, std::size(second), set);
            TvRead(window, third, std::size(third), set);
            TvRead(window, full, 0, set);
        }
        for (uint32_t set : {0u, 0x100u}) {
            TvRead(window, raw, std::size(raw), set);
            TvRead(window, rawSecond, std::size(rawSecond), set);
            TvRead(window, rawThird, std::size(rawThird), set);
            TvRead(window, raw, 0, set);
        }
    }
    Mix(face.count);
    Mix(face.images);
    Mix(face.nodes);
    Mix(g_wrImages[0].count);
    Mix(g_wrImages[1].count);
    Mix(held.count);

    Virtual<uint32_t>(holder, 0x28, old);
    if (old)
        Virtual<uint32_t>(old, 8);
    CallThis<uint32_t>(addr::SC4RefPtr_Destruct, &holder);
    TvStage("read");
}

// OPENSC4_TEST_TREEVIEW=<day>: part u16a by address. Every result goes
// into the fingerprint's `tv` part.
void DriveTreeViewDay(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_TREEVIEW") || !game::IsBuild641())
        return;
    g_tvOn = true;
    uint32_t* const saved = g_into;
    g_into = &g_tvHash;
    DriveTvRegister();
    DriveTvRead();
    g_into = saved;
    log::Write("autotest: treeview driven (hash %08X)", g_tvHash);
}

void TvFingerprint(cISC4City*, std::string& line)
{
    if (!g_tvOn)
        return;
    char text[32];
    std::snprintf(text, sizeof text, " | tv %08X", g_tvHash);
    line += text;
}

const FingerprintPart kTvFingerprint{"treeview", 4, &TvFingerprint};
const DayHook kTvDrive{"treeview", 4, &DriveTreeViewDay};

// ====================================================================================
// Part u16b: GZWinGrid's registration, text helper and property handler,
// GZWinCustom's class code and the LEGACY reader's constructor
// (OPENSC4_TEST_GRID). All called by address (registers checked kept
// after): the registration with part u13's variable table of our own;
// the text helper with strings of the game's; the handler on windows and
// an interface of our own (every slot it calls hashes its number and what
// it gets; the window's slot 114 keeps the three maps' values until the
// call is over, then hashes the maps, read by the game's tree code, and
// destroys them), part u13's values (with a slot 75), the held object of
// part u15 and the game's font and colour services, both passes;
// GZWinCustom on windows of the game's; the constructor with a table of
// our own and with none (the game's COM makes one, which the reader's
// destructor releases). The inputs keep clear of what the game reads from
// its stack: the colours' bytes are hashed by their low bytes, every
// vector with cells has a flag word before them, and a vector whose flag
// word has 0x40000 has only cells that need no key. Addresses never go
// into the hash.

bool g_gdOn = false;
uint32_t g_gdHash = 2166136261u;

void GdStage(const char* name) { log::Write("autotest: grid: %s %08X", name, g_gdHash); }

void DriveGdRegister()
{
    void** const tableAt = reinterpret_cast<void**>(addr::cGZUIScriptLegacy_Table);
    void* const old = *tableAt;
    Mix(old ? 1u : 0u);
    void* table[1] = {WrTableTable()};
    *tableAt = table;
    const uint32_t before = g_wrAdds;
    // EAX is the game's free's: not hashed.
    PmRegs regs = PmRegsOf(0x16161616u, kPmEsi, kPmEdi, 0xA0A0A0A0u, 0xA1A1A1A1u);
    PmCall(addr::cGZUIScriptWidgets_RegisterGrid, regs);
    Mix(g_wrAdds - before);
    Mix(WrMapFind(addr::cGZUIScriptWidgets_Handlers, 0xDAA6B9BE));
    Mix(WrMapFind(addr::cGZUIScriptWidgets_Interfaces, 0xDAA6B9BF));
    *tableAt = old;
    GdStage("reg");
}

// A call with EAX, EBX, ESI and EDI set and ten stack arguments (the text
// helper's); EAX and the four registers back in the block.
struct GdRegs {
    uint32_t eax, ebx, esi, edi;
    uint32_t args[10];
    uint32_t outEax, outEbx, outEsi, outEdi, outEbp;
    std::uintptr_t address;
};

__declspec(naked) void __cdecl GdCallRaw(GdRegs*)
{
    __asm push ebx
    __asm push esi
    __asm push edi
    __asm push ebp
    __asm mov ebp, [esp + 20]
    __asm push ebp
    __asm push dword ptr [ebp + 52]
    __asm push dword ptr [ebp + 48]
    __asm push dword ptr [ebp + 44]
    __asm push dword ptr [ebp + 40]
    __asm push dword ptr [ebp + 36]
    __asm push dword ptr [ebp + 32]
    __asm push dword ptr [ebp + 28]
    __asm push dword ptr [ebp + 24]
    __asm push dword ptr [ebp + 20]
    __asm push dword ptr [ebp + 16]
    __asm mov eax, [ebp + 0]
    __asm mov ebx, [ebp + 4]
    __asm mov esi, [ebp + 8]
    __asm mov edi, [ebp + 12]
    __asm call dword ptr [ebp + 76]
    __asm add esp, 40
    __asm push eax
    __asm mov eax, [esp + 4]
    __asm mov [eax + 60], ebx
    __asm mov [eax + 64], esi
    __asm mov [eax + 68], edi
    __asm mov [eax + 72], ebp
    __asm pop dword ptr [eax + 56]
    __asm add esp, 4
    __asm pop ebp
    __asm pop edi
    __asm pop esi
    __asm pop ebx
    __asm ret
}

uint32_t GdCall(std::uintptr_t address, GdRegs& regs)
{
    regs.address = address;
    GdCallRaw(&regs);
    const bool kept = regs.outEbx == regs.ebx && regs.outEsi == regs.esi && regs.outEdi == regs.edi &&
                      regs.outEbp == PrArg(&regs);
    Mix(kept ? 0x4B1u : 0xBAD0u);
    return regs.outEax;
}

// A game string (SC4String) with the text.
void GdString(char* string, const char* text)
{
    CallThis<void*>(addr::SC4String_SC4String, string);
    Virtual<uint32_t>(string, 0xC, text, static_cast<uint32_t>(std::strlen(text)));
}

void GdEndString(char* string)
{
    if (char* const begin = At<char*>(string, 4))
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(begin);
    At<std::uintptr_t>(string, 0) = addr::SC4String_Vtable;
}

void DriveGdText()
{
    const char* const texts[] = {
        "1,2,0",
        "3,4,0,1a2b,3c4d",
        "5,6,40000,literal text",
        "7,8,1000,k1,k2,Arial,1,2,3,4,5",
        "9,10,41000,lit,Times,9,8,7,6,5",
        "11,12,c0000,a,b",
        " 13 , 14 ,0, a , b ",
        "1,2,0,a,b,c,d,e,f,g,h,i,j,k",
        "15,16,101000,x,y,Courier,4294967295,0,255,256,7",
        "ff,-3,zz,a,b",
    };
    for (const char* text : texts) {
        alignas(4) char cell[0x14];
        GdString(cell, text);
        alignas(4) char out[0x14];
        GdString(out, "old");
        uint32_t words[9];
        for (uint32_t i = 0; i < 9; ++i)
            words[i] = 0xA5A50000u + i;
        GdRegs regs = {};
        regs.eax = PrArg(cell);
        regs.ebx = kPmEbx;
        regs.esi = kPmEsi;
        regs.edi = kPmEdi;
        regs.args[0] = PrArg(&words[0]);
        regs.args[1] = PrArg(&words[1]);
        regs.args[2] = PrArg(&words[2]);
        regs.args[3] = PrArg(out);
        for (uint32_t i = 4; i < 10; ++i)
            regs.args[i] = PrArg(&words[i - 1]);
        MixByte(GdCall(addr::cGZUIScriptWidgets_GridText, regs));
        for (uint32_t i = 0; i < 9; ++i) {
            if (i == 3)
                TvMixFontWord(words[i]);
            else
                Mix(words[i]);
        }
        PrMixText(out);
        PrMixText(cell);
        GdEndString(out);
        GdEndString(cell);
    }
    GdStage("text");
}

// The values: part u13's slots with slot 75 (the word +8 again).
__declspec(naked) void GdValue75()
{
    __asm lea eax, [ecx + 8]
    __asm ret
}

void** GdValueTable()
{
    static void* table[76] = {};
    table[3] = reinterpret_cast<void*>(&WrValueKind);
    table[55] = reinterpret_cast<void*>(&WrValueText);
    table[73] = reinterpret_cast<void*>(&WrValue73);
    table[75] = reinterpret_cast<void*>(&GdValue75);
    return table;
}

WrValue GdNumber(uint32_t word) { return {GdValueTable(), 6, word, ""}; }
WrValue GdText(const char* text) { return {GdValueTable(), 0x800E, 0x7E7E7E7Eu, text}; }

// The interface's window (slot 5): slot 92 and slot 108 (a colour of the
// three bytes' low bytes; the rest of each is the game's stack's or the
// window's argument slot's).
uint32_t __fastcall GdWinSlot92(void*, void*)
{
    Mix(0x7E00005Cu);
    return 0x00E0005Cu;
}

uint32_t __fastcall GdWinColour(void*, void*, uint32_t a, uint32_t b, uint32_t c)
{
    Mix(0x7E00006Cu);
    const uint32_t colour = (a & 0xFF) | (b & 0xFF) << 8 | (c & 0xFF) << 16;
    Mix(colour);
    return 0xC0000000u | colour;
}

void** GdWinTable()
{
    static void* table[109] = {};
    table[92] = reinterpret_cast<void*>(&GdWinSlot92);
    table[108] = reinterpret_cast<void*>(&GdWinColour);
    return table;
}

struct GdWin {
    void** vtable;
} g_gdWin{GdWinTable()};

// The interface (IGZWinGrid): AddRef, Release and the slots the handler
// calls.
void* __fastcall GdFaceWindow(TvFace*, void*)
{
    Mix(0x7F000005u);
    return &g_gdWin;
}

uint32_t __fastcall GdFaceFont(TvFace*, void*, void* font)
{
    Mix(0x7F00000Fu);
    TvMixFont(font);
    return 0x00F0000Fu;
}

uint32_t __fastcall GdFaceGutters(TvFace*, void*, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    Mix(0x7F000018u);
    Mix(a);
    Mix(b);
    Mix(c);
    Mix(d);
    return 0x00F00018u;
}

uint32_t __fastcall GdFaceStyle(TvFace*, void*, uint32_t row, uint32_t column, char* style)
{
    Mix(0x7F000055u);
    Mix(row);
    Mix(column);
    TvMixFont(At<void*>(style, 0));
    for (std::size_t offset = 4; offset < 0x24; offset += 4)
        Mix(At<uint32_t>(style, offset));
    return 0x00F00055u;
}

uint32_t __fastcall GdFaceText(TvFace*, void*, uint32_t row, uint32_t column, const char* text, uint32_t length,
                               uint32_t one, uint32_t flag)
{
    Mix(0x7F000070u);
    Mix(row);
    Mix(column);
    Mix(length);
    for (uint32_t i = 0; text && i < length && i < 64; ++i)
        Mix(static_cast<uint8_t>(text[i]));
    Mix(one);
    Mix(flag);
    return 0x00F00070u;
}

uint32_t __fastcall GdFaceImage(TvFace*, void*, uint32_t row, uint32_t column, WrImage* image, uint32_t one)
{
    Mix(0x7F000072u);
    Mix(row);
    Mix(column);
    Mix(image ? image->tag : 0xDEADu);
    Mix(one);
    return 0x00F00072u;
}

template <uint32_t Slot>
uint32_t __fastcall GdSet0(TvFace*, void*)
{
    Mix(0x7F000000u | Slot);
    return 0x00F00000u | Slot;
}

void** GdFaceTable()
{
    static void* table[115] = {};
    table[1] = reinterpret_cast<void*>(&TvFaceAddRef);
    table[2] = reinterpret_cast<void*>(&TvFaceRelease);
    table[5] = reinterpret_cast<void*>(&GdFaceWindow);
    table[10] = reinterpret_cast<void*>(&TvSet1<10>);
    table[12] = reinterpret_cast<void*>(&TvSet1<12>);
    table[15] = reinterpret_cast<void*>(&GdFaceFont);
    table[17] = reinterpret_cast<void*>(&TvSet1<17>);
    table[21] = reinterpret_cast<void*>(&TvSet1<21>);
    table[24] = reinterpret_cast<void*>(&GdFaceGutters);
    table[27] = reinterpret_cast<void*>(&TvSet2<27>);
    table[39] = reinterpret_cast<void*>(&TvSet2<39>);
    table[58] = reinterpret_cast<void*>(&TvSet2<58>);
    table[60] = reinterpret_cast<void*>(&TvSet1<60>);
    table[64] = reinterpret_cast<void*>(&TvSet1<64>);
    table[65] = reinterpret_cast<void*>(&TvSet3<65>);
    table[66] = reinterpret_cast<void*>(&TvSet1<66>);
    table[67] = reinterpret_cast<void*>(&TvSet3<67>);
    table[74] = reinterpret_cast<void*>(&TvSet2<74>);
    table[76] = reinterpret_cast<void*>(&TvSet2<76>);
    table[85] = reinterpret_cast<void*>(&GdFaceStyle);
    table[112] = reinterpret_cast<void*>(&GdFaceText);
    table[114] = reinterpret_cast<void*>(&GdFaceImage);
    return table;
}

// The window's slot 114: the three maps' values kept (the handler goes on
// using the maps) until the call is over; others part u12's.
struct GdPut {
    uint32_t key;
    char* value;
};

GdPut g_gdPuts[8] = {};
uint32_t g_gdPutCount = 0;

uint32_t __fastcall GdWinPut(WrWindow* self, void* edx, uint32_t key, char* value)
{
    Mix(self->mode);
    if (key == 0x287A7136u || key == 0xC88CCB6Du || key == 0xC88CCB6Eu) {
        Mix(0x114B00u);
        Mix(key);
        if (value && g_gdPutCount < std::size(g_gdPuts))
            g_gdPuts[g_gdPutCount++] = {key, value};
        else
            Mix(0xDEADu);
        return 0x00C114B0u;
    }
    return PmWindowPut(self, edx, key, value);
}

void** GdWindowTable()
{
    static void* table[115] = {};
    table[0] = reinterpret_cast<void*>(&WrWindowQuery);
    table[114] = reinterpret_cast<void*>(&GdWinPut);
    return table;
}

// A grid map (0x24 bytes): its count +0xC, its bounds and byte, then the
// outer tree (a node's key +0x10, a word +0x14, the inner map +0x18) and
// each inner tree (a node's key +0x10, the value +0x18: a cell's record or
// three words).
void GdMixMap(char* map, bool cells)
{
    Mix(map ? 1u : 0u);
    if (!map)
        return;
    for (std::size_t offset = 0xC; offset < 0x20; offset += 4)
        Mix(At<uint32_t>(map, offset));
    Mix(At<uint8_t>(map, 0x20));
    char* const header = At<char*>(map, 0);
    uint32_t outer = 0;
    for (char* node = At<char*>(header, 8); node != header && outer < 64; node = TreeNext(node), ++outer) {
        Mix(At<uint32_t>(node, 0x10));
        Mix(At<uint32_t>(node, 0x14));
        char* const inner = At<char*>(node, 0x18);
        uint32_t count = 0;
        for (char* leaf = At<char*>(inner, 8); leaf != inner && count < 64; leaf = TreeNext(leaf), ++count) {
            Mix(At<uint32_t>(leaf, 0x10));
            char* const value = leaf + 0x18;
            if (cells) {
                for (std::size_t offset = 0; offset < 0xC; offset += 4)
                    Mix(At<uint32_t>(value, offset));
                PrMixText(value + 0xC);
                for (std::size_t offset = 0x20; offset < 0x38; offset += 4)
                    Mix(At<uint32_t>(value, offset));
            } else {
                for (std::size_t offset = 0; offset < 0xC; offset += 4)
                    Mix(At<uint32_t>(value, offset));
            }
        }
        Mix(count);
    }
    Mix(outer);
}

// The kept values: each a number holding the holder (kind 0x10, the
// holder at +4: its vtable, count and byte, its map); destroyed (slot 127),
// which releases the holder and with it the map.
void GdFlushPuts()
{
    const std::uintptr_t vtables[3] = {addr::cGZUIScriptWidgets_GridCellsVtable, addr::cGZUIScriptWidgets_GridColumnsVtable,
                                       addr::cGZUIScriptWidgets_GridRowsVtable};
    for (uint32_t i = 0; i < g_gdPutCount; ++i) {
        char* const value = g_gdPuts[i].value;
        Mix(g_gdPuts[i].key);
        Mix(At<uint16_t>(value, 0xE));
        char* const holder = At<uint16_t>(value, 0xE) == 0x10 ? At<char*>(value, 4) : nullptr;
        Mix(holder ? 1u : 0u);
        if (holder) {
            uint32_t which = 0xFF;
            for (uint32_t v = 0; v < 3; ++v) {
                if (At<std::uintptr_t>(holder, 0) == vtables[v])
                    which = v;
            }
            Mix(which);
            Mix(At<uint32_t>(holder, 8));
            Mix(At<uint8_t>(holder, 0xC));
            GdMixMap(At<char*>(holder, 4), which == 0);
        }
        Virtual<void>(value, 0x1FC, 1u);
    }
    g_gdPutCount = 0;
}

// One call of the handler: its EAX and argument slots after (the raw pass
// clears the first's low byte and writes it), then the kept maps. The
// game's set pass with a text colour 0x1601 found returns with EBX, ESI
// and EDI not kept (its window's slot 108 is pushed a fourth argument it
// does not pop, which its pops then take; ours keeps them): for those
// calls whether they were kept is not hashed.
void GdRead(WrWindow& window, WrPair* pairs, std::size_t count, uint32_t set, bool colour = false)
{
    char* vector[3] = {reinterpret_cast<char*>(pairs), reinterpret_cast<char*>(pairs + count), nullptr};
    PmRegs regs = PmRegsOf(0x2B2B2B2Bu, kPmEsi, kPmEdi, PrArg(&window), PrArg(vector));
    regs.args[2] = set;
    if (colour && (set & 0xFF) && window.mode == 1) {
        regs.address = addr::cGZUIScriptWidgets_GridRead;
        PmCallRaw(&regs);
        Mix(regs.outEbp == PrArg(&regs) ? 0x4B2u : 0xBAD2u);
        Mix(regs.outEax);
    } else {
        Mix(PmCall(addr::cGZUIScriptWidgets_GridRead, regs));
    }
    if (set & 0xFF) {
        Mix(regs.args[0] == PrArg(&window) ? 0x77DDu : 0xBAD0u);
    } else {
        Mix((regs.args[0] & ~0xFFu) == (PrArg(&window) & ~0xFFu) ? 0x77DCu : 0xBAD0u);
        MixByte(regs.args[0]);
    }
    Mix(regs.args[1] == PrArg(vector) ? 0x77DEu : 0xBAD1u);
    Mix(regs.args[2]);
    GdFlushPuts();
}

void DriveGdRead()
{
    void* holder = nullptr;
    CallThis<void*>(addr::SC4RefPtr_UIScriptLoader, &holder);
    Mix(holder ? 1u : 0u);
    void* const fonts = reinterpret_cast<void*(__cdecl*)()>(addr::SC4_Singleton913C72)();
    void* const colours = reinterpret_cast<void*(__cdecl*)()>(addr::SC4_Singleton913C1A)();
    Mix(fonts ? 1u : 0u);
    Mix(colours ? 1u : 0u);
    if (!holder || !fonts || !colours) {
        GdStage("read");
        return;
    }
    for (uint32_t i = 0; i < 3; ++i)
        g_fbFonts[i] = Virtual<void*>(fonts, 0x14, kFbFontWords[i]);
    void* const old = Virtual<void*>(holder, 0x24);
    if (old)
        Virtual<uint32_t>(old, 4);
    PmHeld held = {OgHeldTable(), 0};
    Virtual<uint32_t>(holder, 0x28, static_cast<void*>(&held));
    g_wrImages[0] = {WrImageTable(), 0xE0, 0};
    g_wrImages[1] = {WrImageTable(), 0xE1, 0};

    TvFace face = {GdFaceTable(), 1, 0, 0};
    WrWindow windows[3] = {{GdWindowTable(), 0, &face}, {GdWindowTable(), 1, &face}, {GdWindowTable(), 2, &face}};

    WrValue n0 = GdNumber(0), n1 = GdNumber(1), n2 = GdNumber(2), n5 = GdNumber(5), n7 = GdNumber(7),
            n40 = GdNumber(40), nBig = GdNumber(0xFFFFFFFFu), nKey = GdNumber(0x40000), nFont0 = GdNumber(kFbFontWords[0]),
            other = {GdValueTable(), 9, 1, "(1,2,3)"};
    WrValue c1 = GdText("(1,2,3)"), c2 = GdText("(255,128,0)"), c3 = GdText("(300,-1,256)"), cShort = GdText("(4,5)"),
            rect = GdText("(1,2,3,4)"), rectShort = GdText("(1,2)"), fontText = GdText("Some Font");
    // Cells: a key, an image (even instance) and none (odd), a text (odd
    // instance) and none (even), literals, styles, a short one, a repeat.
    WrValue k1 = GdText("1,2,0,0000a1a2,0000b1b2"), k2 = GdText("2,3,2000,0000d0d0,00000002"),
            k3 = GdText("2,4,2000,0000d0d0,00000003"), k4 = GdText("3,4,10000,00001234,00000005"),
            k5 = GdText("3,5,90000,00001234,00000006"), k6 = GdText("4,5,c0000,Literal"),
            k7 = GdText("5,6,101000,00000001,00000002,Arial,0,7,8,9,10"),
            k8 = GdText("6,7,1000,00000001,00000002,Courier,1,255,256,3,4"), k9 = GdText("bad"),
            k10 = GdText("1,2,0,00000003,00000004"), k11 = GdText("7,0,42000,Lit,Times,2,3,4,5,6");
    WrValue col1 = GdText("1,30,4"), col2 = GdText("2,40"), col3 = GdText("x"), col4 = GdText("-1,-2,-3"),
            row1 = GdText("0,3,20"), row2 = GdText("5,5,7"), row3 = GdText("2,1,5");
    WrPair full[] = {
        {0x1601, &c1},    {0xF000, &nFont0}, {0x1602, &n2},   {0x1603, &n1},    {0xF004, &rect},  {0x1605, &n1},
        {0x1606, &n5},    {0x1616, &n1},     {0x1604, &c2},   {0x1607, &n40},   {0x1608, &n7},    {0x1609, &n1},
        {0x160A, &n5},    {0x160B, &n7},     {0x160C, &n1},   {0x160F, &n0},    {0x1610, &n1},    {0x161F, &n0},
        {0x1622, &n1},    {0x1625, &k1},     {0x1625, &k2},   {0x1625, &k3},    {0x1625, &k4},    {0x1625, &k5},
        {0x1625, &k6},    {0x1625, &k7},     {0x1625, &k8},   {0x1625, &k9},    {0x1625, &n1},    {0x1625, &k10},
        {0x1626, &col1},  {0x1626, &col2},   {0x1626, &col3}, {0x1626, &col4},  {0x1627, &row1},  {0x1627, &row2},
        {0x1601, &other},
    };
    WrPair second[] = {
        {0xF000, &fontText}, {0x1603, &n0},  {0xF004, &rectShort}, {0x1609, &n2},   {0x1616, &n2},  {0x1604, &c3},
        {0x1621, &n1},       {0x1622, &nKey}, {0x1625, &k6},       {0x1625, &k11},  {0x1625, &k9},  {0x1627, &row3},
        {0x1606, &nBig},     {0x1601, &cShort},
    };
    WrPair third[] = {{0x1609, &n7}, {0x160F, &n1}, {0x1602, &other}, {0x1626, &col1}};
    // Every flag key, on and off by turns (0x160F off in the first).
    WrPair flags[23];
    WrPair flagsOff[23];
    for (uint32_t i = 0; i < 23; ++i) {
        flags[i] = {0x160Cu + i, (i & 1) ? &n0 : &n1};
        flagsOff[i] = {0x160Cu + i, (i & 1) ? &n1 : &n0};
    }
    WrPair raw[] = {{0x1623, &c1}, {0x1624, &c3}, {0x1625, &k1}};
    WrPair rawSecond[] = {{0x1624, &c2}, {0x1623, &cShort}};

    for (WrWindow& window : windows) {
        for (uint32_t set : {1u, 0xFFFFFF01u}) {
            GdRead(window, full, std::size(full), set, true);
            GdRead(window, second, std::size(second), set, true);
            GdRead(window, third, std::size(third), set);
            GdRead(window, flags, std::size(flags), set);
            GdRead(window, flagsOff, std::size(flagsOff), set);
            GdRead(window, full, 0, set);
        }
        for (uint32_t set : {0u, 0x100u}) {
            GdRead(window, raw, std::size(raw), set);
            GdRead(window, rawSecond, std::size(rawSecond), set);
            GdRead(window, raw, 0, set);
        }
    }
    Mix(face.count);
    Mix(g_wrImages[0].count);
    Mix(g_wrImages[1].count);
    Mix(held.count);

    Virtual<uint32_t>(holder, 0x28, old);
    if (old)
        Virtual<uint32_t>(old, 8);
    CallThis<uint32_t>(addr::SC4RefPtr_Destruct, &holder);
    GdStage("read");
}

// GZWinCustom: a message target and a drawing surface of our own (the
// window's +0x48 and +0x6C set for the calls and put back after).
uint32_t __fastcall GdTargetMessage(void*, void*, const uint32_t* message)
{
    Mix(0x7A03u);
    Mix(message[0]);
    Mix(message[1]);
    Mix(message[2]);
    return 0x00AB0001u;
}

uint32_t __fastcall GdSurface21(void*, void*, uint32_t value)
{
    Mix(0x5F15u);
    Mix(value);
    return 0x00AB0015u;
}

char* g_gdCustom = nullptr;

uint32_t __fastcall GdSurface35(void*, void*, char* area)
{
    Mix(0x5F23u);
    Mix(area == g_gdCustom + 0x24 ? 0x24u : 0xBADu);
    return 0x00AB0023u;
}

void** GdOtherTable()
{
    static void* table[36] = {};
    table[3] = reinterpret_cast<void*>(&GdTargetMessage);
    table[21] = reinterpret_cast<void*>(&GdSurface21);
    table[35] = reinterpret_cast<void*>(&GdSurface35);
    return table;
}

void GdMixCustom(char* window)
{
    Mix(At<std::uintptr_t>(window, 0) == addr::cGZUIScript478D1E6F_Vtable ? 0xC1u : 0xC0u);
    Mix(At<std::uintptr_t>(window, 0xD8) == addr::cGZUIScript478D1E6F_FaceVtable ? 0xF1u : 0xF0u);
}

void DriveGdCustom()
{
    struct {
        void** vtable;
    } other{GdOtherTable()};
    char* const window = static_cast<char*>(New(0xDC));
    g_gdCustom = window;
    MixIs(CallThis<char*>(addr::cGZUIScript478D1E6F_Construct, window), window);
    GdMixCustom(window);
    Mix(Virtual<uint32_t>(window, 4));
    // QueryInterface: IGZWinCustom without and with an out, IGZWin, another.
    Mix(CallThis<uint32_t>(addr::cGZUIScript478D1E6F_QueryInterface, window, 0x678D1E84u, static_cast<void**>(nullptr)));
    void* out = nullptr;
    Mix(CallThis<uint32_t>(addr::cGZUIScript478D1E6F_QueryInterface, window, 0x678D1E84u, &out));
    MixIs(out, window + 0xD8);
    Mix(Virtual<uint32_t>(window, 8));
    out = nullptr;
    MixByte(CallThis<uint32_t>(addr::cGZUIScript478D1E6F_QueryInterface, window, 0x22BA0121u, &out));
    MixIs(out, window);
    if (out)
        Mix(Virtual<uint32_t>(window, 8));
    out = nullptr;
    MixByte(CallThis<uint32_t>(addr::cGZUIScript478D1E6F_QueryInterface, window, 0x7E57A11Bu, &out));
    MixIs(out, nullptr);
    // Messages, without a target and with ours, the window's slots 43 and
    // 44 (+0xA8 and +0xAC) set for them and put back after.
    const uint32_t place[2] = {At<uint32_t>(window, 0xA8), At<uint32_t>(window, 0xAC)};
    At<uint32_t>(window, 0xA8) = 17;
    At<uint32_t>(window, 0xAC) = 0x2300;
    void* const target = At<void*>(window, 0x48);
    Mix(target ? 1u : 0u);
    for (uint32_t pass = 0; pass < 2; ++pass) {
        if (pass)
            At<void*>(window, 0x48) = &other;
        for (uint32_t kind : {7u, 0xAu, 0xDu, 3u, 8u}) {
            uint32_t message[4] = {kind, 100, 200, 0x55};
            Mix(CallThis<uint32_t>(addr::cGZUIScript478D1E6F_OnMessage, window, static_cast<void*>(message)));
            Mix(message[1]);
            Mix(message[2]);
            Mix(message[3]);
        }
    }
    At<void*>(window, 0x48) = target;
    At<uint32_t>(window, 0xA8) = place[0];
    At<uint32_t>(window, 0xAC) = place[1];
    // Draw, with our surface.
    void* const surface = At<void*>(window, 0x6C);
    At<void*>(window, 0x6C) = &other;
    Mix(CallThis<uint32_t>(addr::cGZUIScript478D1E6F_Draw, window));
    At<void*>(window, 0x6C) = surface;
    // The destructor (then freed), the deleting destructor with 0 and 1.
    CallThis<uint32_t>(addr::cGZUIScript478D1E6F_Destruct, window);
    GdMixCustom(window);
    reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_operator_delete)(window);
    char* const second = static_cast<char*>(New(0xDC));
    CallThis<char*>(addr::cGZUIScript478D1E6F_Construct, second);
    MixIs(CallThis<char*>(addr::cGZUIScript478D1E6F_DeletingDestructor, second, 0u), second);
    GdMixCustom(second);
    reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_operator_delete)(second);
    char* const third = static_cast<char*>(New(0xDC));
    CallThis<char*>(addr::cGZUIScript478D1E6F_Construct, third);
    MixIs(CallThis<char*>(addr::cGZUIScript478D1E6F_DeletingDestructor, third, 1u), third);
    g_gdCustom = nullptr;
    GdStage("custom");
}

// The LEGACY reader's constructor: with a table of our own (held), then
// with none (the game's COM makes one and the registrations fill it; the
// reader's destructor releases it, which forgets it).
uint32_t g_gdTableCount = 0;

uint32_t __fastcall GdTableAddRef(void*, void*)
{
    Mix(0x7AB1u);
    return ++g_gdTableCount;
}

uint32_t __fastcall GdTableRelease(void*, void*)
{
    Mix(0x7AB2u);
    return --g_gdTableCount;
}

void** GdTableTable()
{
    static void* table[3] = {};
    table[1] = reinterpret_cast<void*>(&GdTableAddRef);
    table[2] = reinterpret_cast<void*>(&GdTableRelease);
    return table;
}

void GdMixReader(char* reader)
{
    Mix(At<std::uintptr_t>(reader, 0) == addr::cGZUIScriptLegacy_Vtable ? 1u : 0u);
    Mix(At<std::uintptr_t>(reader, 4) == addr::cGZUIScriptLegacy_ChildVtable ? 1u : 0u);
    Mix(At<std::uintptr_t>(reader, 8) == addr::cGZUIScriptLegacy_WindowVtable ? 1u : 0u);
    for (std::size_t offset = 0xC; offset < 0x1C; offset += 4)
        Mix(At<uint32_t>(reader, offset));
    Mix(At<uint8_t>(reader, 0x1C));
}

void DriveGdLegacy()
{
    void** const tableAt = reinterpret_cast<void**>(addr::cGZUIScriptLegacy_Table);
    void* const old = *tableAt;
    Mix(old ? 1u : 0u);
    struct {
        void** vtable;
    } table{GdTableTable()};
    *tableAt = &table;
    alignas(4) char reader[0x20];
    std::memset(reader, 0xCD, sizeof reader);
    MixIs(CallThis<char*>(addr::cGZUIScriptLegacy_Construct, reader), reader);
    GdMixReader(reader);
    Mix(g_gdTableCount);
    MixIs(*tableAt, &table);
    *tableAt = nullptr;
    std::memset(reader, 0xCD, sizeof reader);
    MixIs(CallThis<char*>(addr::cGZUIScriptLegacy_Construct, reader), reader);
    GdMixReader(reader);
    Mix(*tableAt ? 1u : 0u);
    CallThis<uint32_t>(addr::cGZUIScriptLegacy_Destruct, reader);
    Mix(*tableAt ? 1u : 0u);
    *tableAt = old;
    GdStage("ctor");
}

// OPENSC4_TEST_GRID=<day>: part u16b by address. Every result goes into
// the fingerprint's `gd` part.
void DriveGridDay(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_GRID") || !game::IsBuild641())
        return;
    g_gdOn = true;
    uint32_t* const saved = g_into;
    g_into = &g_gdHash;
    DriveGdRegister();
    DriveGdText();
    DriveGdRead();
    DriveGdCustom();
    DriveGdLegacy();
    g_into = saved;
    log::Write("autotest: grid driven (hash %08X)", g_gdHash);
}

void GdFingerprint(cISC4City*, std::string& line)
{
    if (!g_gdOn)
        return;
    char text[32];
    std::snprintf(text, sizeof text, " | gd %08X", g_gdHash);
    line += text;
}

const FingerprintPart kGdFingerprint{"grid", 4, &GdFingerprint};
const DayHook kGdDrive{"grid", 4, &DriveGridDay};



// Part u12, slice 3: the property handlers and the variable table's words
// (OPENSC4_TEST_PROPHANDLE). The words' filler called by address with a
// variable table of our own in the global's place (its slot 3 hashes each
// word's text and value). The eight handlers called by address with
// windows of our own (slot 0 gives a control of our own for its interface,
// or none; slots 53, 68 and 114 hash what they get), controls of our own
// (every slot a handler calls hashes its arguments: words, a string's
// text, a rectangle's four ints, which image, a pointer's being there, or
// the low byte where the game pushes a dword it wrote only the low byte of
// and leaves its stack's leftovers above), the UI script loader's held
// object one of our own (images of our own for odd instances, texts for
// odd instances), on vectors of pairs of our own (number values whose
// slots 73 and 75 leave a mark in EDX, text values), both passes, EBX,
// ESI, EDI and EBP checked kept and the argument slots read back. Every
// result goes into the hash; addresses never do.

bool g_phOn = false;
uint32_t g_phHash = 2166136261u;

void PhStage(const char* name) { log::Write("autotest: prophandle: %s %08X", name, g_phHash); }

// The values: slot 3 the kind, slot 55 the text into a string, slots 73
// and 75 point at two words of their own (EDX a mark).
struct PhValue {
    void** vtable;
    uint32_t kind;
    uint32_t word73;
    uint32_t word75;
    const char* text;
};

uint32_t __fastcall PhValueKind(PhValue* self, void*)
{
    Mix(0x3000u | self->kind);
    return self->kind;
}

uint32_t __fastcall PhValueText(PhValue* self, void*, void* string)
{
    Mix(0x37000u);
    const uint32_t length = static_cast<uint32_t>(std::strlen(self->text));
    Virtual<uint32_t>(string, 0xC, self->text, length);
    return 0x100u | (length & 0xFF);
}

__declspec(naked) void PhValue73()
{
    __asm lea eax, [ecx + 8]
    __asm mov edx, 0x73737373
    __asm ret
}

__declspec(naked) void PhValue75()
{
    __asm lea eax, [ecx + 12]
    __asm mov edx, 0x75757575
    __asm ret
}

void** PhValueTable()
{
    static void* table[76] = {};
    table[3] = reinterpret_cast<void*>(&PhValueKind);
    table[55] = reinterpret_cast<void*>(&PhValueText);
    table[73] = reinterpret_cast<void*>(&PhValue73);
    table[75] = reinterpret_cast<void*>(&PhValue75);
    return table;
}

// A vector of pairs (key, value) and its values.
struct PhVector {
    std::vector<PhValue> values;
    std::vector<std::pair<uint32_t, PhValue*>> pairs;
    char* vector[3] = {};

    struct Item {
        uint32_t key;
        char type;  // n a number, t a text, x not a number, 0 none
        uint32_t word;
        const char* text;
    };
    PhVector(std::initializer_list<Item> items)
    {
        values.reserve(items.size());
        for (const Item& item : items) {
            if (item.type == '0') {
                pairs.push_back({item.key, nullptr});
                continue;
            }
            const uint32_t kind = item.type == 'n' ? 6u : item.type == 't' ? 0x800Eu : 5u;
            values.push_back({PhValueTable(), kind, item.word, item.word ^ 0x40000000u, item.text ? item.text : ""});
            pairs.push_back({item.key, &values.back()});
        }
        vector[0] = reinterpret_cast<char*>(pairs.data());
        vector[1] = reinterpret_cast<char*>(pairs.data() + pairs.size());
    }
};

// Images of our own: AddRef, Release, width (slot 9), height (slot 10),
// area (slot 12).
struct PhImage {
    void** vtable;
    uint32_t id;
    uint32_t count;
    int32_t area[4];
};

uint32_t __fastcall PhImageAddRef(PhImage* self, void*) { return ++self->count; }
uint32_t __fastcall PhImageRelease(PhImage* self, void*) { return --self->count; }
uint32_t __fastcall PhImageWidth(PhImage* self, void*) { return 0x40 + self->id; }
uint32_t __fastcall PhImageHeight(PhImage* self, void*) { return 0x20 + self->id; }
int32_t* __fastcall PhImageArea(PhImage* self, void*) { return self->area; }

void** PhImageTable()
{
    static void* table[13] = {};
    table[1] = reinterpret_cast<void*>(&PhImageAddRef);
    table[2] = reinterpret_cast<void*>(&PhImageRelease);
    table[9] = reinterpret_cast<void*>(&PhImageWidth);
    table[10] = reinterpret_cast<void*>(&PhImageHeight);
    table[12] = reinterpret_cast<void*>(&PhImageArea);
    return table;
}

PhImage g_phImages[4];

// The held object of the UI script loader: an image for odd instances (one
// of four, held), a text for odd instances.
uint32_t __fastcall PhHeldImage(PmHeld*, void*, const uint32_t* key)
{
    Mix(0x3003u);
    PmMixKey(key);
    if (!(key[2] & 1))
        return 0;
    PhImage& image = g_phImages[(key[2] >> 1) & 3];
    ++image.count;
    return PrArg(&image);
}

void** PhHeldTable()
{
    static void* table[5] = {};
    table[1] = reinterpret_cast<void*>(&PmHeldAddRef);
    table[2] = reinterpret_cast<void*>(&PmHeldRelease);
    table[3] = reinterpret_cast<void*>(&PhHeldImage);
    table[4] = reinterpret_cast<void*>(&PmHeldText);
    return table;
}

// The controls: per interface, the slots the handler calls and what each
// argument is (w a word, b its low byte, s a string, r a rectangle or
// none, i which image or none, p a pointer's being there).
struct PhSlot {
    uint32_t slot;
    const char* args;
};

struct PhInterface {
    uint32_t iid;
    std::vector<PhSlot> slots;
};

const std::vector<PhInterface>& PhInterfaces()
{
    static const std::vector<PhInterface> interfaces = {
        {0xC2AFA76F, {{12, "w"}, {10, "wbbb"}}},
        {0x212CDC1F, {{7, "ww"}, {19, "w"}, {9, "ww"}, {23, "ww"}, {25, "ww"}, {21, "w"}, {5, "ww"}}},
        {0x231A1C57,
         {{43, ""}, {80, ""}, {77, "w"}, {10, "w"}, {12, "ww"}, {18, "w"}, {23, "w"}, {37, "ww"}, {33, "w"},
          {40, "w"}, {42, "w"}, {64, "w"}, {66, "w"}, {68, "ww"}, {72, "w"}, {81, "w"}, {88, "ir"}, {87, "ir"}}},
        {0x4132242B,
         {{7, "sww"}, {14, ""}, {16, ""}, {10, "ww"}, {27, "w"}, {29, "w"}, {9, ""}, {47, ""}, {48, ""}, {35, "bb"},
          {38, "p"}, {39, "ww"}, {42, "w"}, {51, "w"}, {55, "ww"}, {53, "w"}, {57, "ww"}, {59, "ww"}, {17, "w"}}},
        {0x8810,
         {{5, "ww"}, {20, "w"}, {19, "p"}, {23, "ww"}, {30, "ww"}, {29, "wwww"}, {12, "wir"}, {27, "w"}, {35, "s"},
          {40, "ww"}, {38, "w"}, {45, "ww"}}},
        {0xC12CEA13, {{8, "r"}, {11, "ww"}, {6, "ww"}, {4, "i"}}},
        {0x21325207, {{5, "w"}, {8, "w"}, {6, "w"}, {10, "w"}, {12, "ir"}}},
        {0x612CE0C3,
         {{10, "ww"}, {12, "w"}, {9, "w"}, {4, "w"}, {6, "ww"}, {19, "p"}, {23, "ww"}, {14, "iw"}, {27, "w"},
          {29, "ww"}, {31, "wwww"}}},
    };
    return interfaces;
}

struct PhControl {
    void** vtable;
    const PhInterface* face;
    uint32_t count;
    uint32_t calls;
};

void PhMixArg(char kind, uint32_t value)
{
    switch (kind) {
    case 'b': Mix(value & 0xFF); break;
    case 's': PrMixText(static_cast<char*>(reinterpret_cast<void*>(static_cast<std::uintptr_t>(value)))); break;
    case 'r':
        if (!value) {
            Mix(0xDEADu);
        } else {
            const int32_t* const rect = reinterpret_cast<const int32_t*>(static_cast<std::uintptr_t>(value));
            for (int i = 0; i < 4; ++i)
                Mix(static_cast<uint32_t>(rect[i]));
        }
        break;
    case 'i': Mix(value ? reinterpret_cast<const PhImage*>(static_cast<std::uintptr_t>(value))->id : 0xFFu); break;
    case 'p': Mix(value ? 1u : 0u); break;
    default: Mix(value); break;
    }
}

uint32_t PhRecord(PhControl* self, uint32_t slot, const uint32_t* args)
{
    Mix(0xC0000u | slot);
    const char* kinds = "";
    for (const PhSlot& known : self->face->slots) {
        if (known.slot == slot)
            kinds = known.args;
    }
    for (uint32_t i = 0; kinds[i]; ++i)
        PhMixArg(kinds[i], args[i]);
    ++self->calls;
    return (slot * 0x10001u) ^ (self->calls << 8);
}

uint32_t __fastcall PhControlAddRef(PhControl* self, void*) { return ++self->count; }
uint32_t __fastcall PhControlRelease(PhControl* self, void*) { return --self->count; }

template <std::size_t Slot>
uint32_t __fastcall PhSlot0(PhControl* self, void*)
{
    return PhRecord(self, Slot, nullptr);
}

template <std::size_t Slot>
uint32_t __fastcall PhSlot1(PhControl* self, void*, uint32_t a)
{
    const uint32_t args[] = {a};
    return PhRecord(self, Slot, args);
}

template <std::size_t Slot>
uint32_t __fastcall PhSlot2(PhControl* self, void*, uint32_t a, uint32_t b)
{
    const uint32_t args[] = {a, b};
    return PhRecord(self, Slot, args);
}

template <std::size_t Slot>
uint32_t __fastcall PhSlot3(PhControl* self, void*, uint32_t a, uint32_t b, uint32_t c)
{
    const uint32_t args[] = {a, b, c};
    return PhRecord(self, Slot, args);
}

template <std::size_t Slot>
uint32_t __fastcall PhSlot4(PhControl* self, void*, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    const uint32_t args[] = {a, b, c, d};
    return PhRecord(self, Slot, args);
}

constexpr std::size_t kPhSlots = 90;

template <std::size_t... I>
std::array<std::array<void*, kPhSlots>, 5> PhSlotTables(std::index_sequence<I...>)
{
    return {{{reinterpret_cast<void*>(&PhSlot0<I>)...},
             {reinterpret_cast<void*>(&PhSlot1<I>)...},
             {reinterpret_cast<void*>(&PhSlot2<I>)...},
             {reinterpret_cast<void*>(&PhSlot3<I>)...},
             {reinterpret_cast<void*>(&PhSlot4<I>)...}}};
}

void** PhControlTable(const PhInterface& face)
{
    static const auto slots = PhSlotTables(std::make_index_sequence<kPhSlots>{});
    static std::vector<std::array<void*, kPhSlots>> tables(PhInterfaces().size());
    std::array<void*, kPhSlots>& table = tables[&face - PhInterfaces().data()];
    table[1] = reinterpret_cast<void*>(&PhControlAddRef);
    table[2] = reinterpret_cast<void*>(&PhControlRelease);
    for (const PhSlot& slot : face.slots)
        table[slot.slot] = slots[std::strlen(slot.args)][slot.slot];
    return table.data();
}

// The windows: slot 0 gives the control for its interface (held) or
// nothing; slots 53, 68 and 114 hash what they get (114 destroys the
// value by its slot 127 after).
struct alignas(256) PhWindow {
    void** vtable;
    PhControl* control;
    bool give;
};

uint32_t __fastcall PhWindowQuery(PhWindow* self, void*, uint32_t iid, void** out)
{
    Mix(0x10000u);
    Mix(iid);
    if (!self->give || iid != self->control->face->iid)
        return 0x100u;
    ++self->control->count;
    *out = self->control;
    return 0x101u;
}

uint32_t __fastcall PhWindow53(PhWindow*, void*, uint32_t width, uint32_t height)
{
    Mix(0x53000u);
    Mix(width);
    Mix(height);
    return 0x53u;
}

uint32_t __fastcall PhWindow68(PhWindow*, void*, uint32_t flag, uint32_t on)
{
    Mix(0x68000u);
    Mix(flag);
    Mix(on);
    return 0x68u;
}

uint32_t __fastcall PhWindowPut(PhWindow*, void*, uint32_t key, char* value)
{
    Mix(0x114000u);
    Mix(key);
    if (value) {
        PmMixValue(value);
        Virtual<void>(value, 0x1FC, 1u);
    } else {
        Mix(0xDEADu);
    }
    return 0x114u;
}

void** PhWindowTable()
{
    static void* table[115] = {};
    table[0] = reinterpret_cast<void*>(&PhWindowQuery);
    table[53] = reinterpret_cast<void*>(&PhWindow53);
    table[68] = reinterpret_cast<void*>(&PhWindow68);
    table[114] = reinterpret_cast<void*>(&PhWindowPut);
    return table;
}

// The variable table of our own for the words: slot 3 hashes the word's
// text and value.
uint32_t g_phWords = 0;

uint32_t __fastcall PhTableAdd(void*, void*, char* string, uint32_t value)
{
    PrMixText(string);
    Mix(value);
    ++g_phWords;
    return value ^ 0x5A5Au;
}

void DrivePhWords()
{
    static void* table[4] = {};
    table[3] = reinterpret_cast<void*>(&PhTableAdd);
    void* object[2] = {table, nullptr};
    void** const global = reinterpret_cast<void**>(addr::cGZUIScriptLegacy_Table);
    void* const old = *global;
    Mix(old ? 1u : 0u);
    *global = object;
    reinterpret_cast<void(__cdecl*)()>(addr::cGZUIScriptProps_AddWords)();
    reinterpret_cast<void(__cdecl*)()>(addr::cGZUIScriptProps_AddWords)();
    *global = old;
    Mix(g_phWords);
    PhStage("words");
}

const std::uintptr_t kPhHandlers[] = {
    addr::cGZUIScriptProps_HandleC2AFA76F, addr::cGZUIScriptProps_Handle212CDC1F,
    addr::cGZUIScriptProps_Handle231A1C57, addr::cGZUIScriptProps_Handle4132242B,
    addr::cGZUIScriptProps_Handle8810,     addr::cGZUIScriptProps_HandleC12CEA13,
    addr::cGZUIScriptProps_Handle21325207, addr::cGZUIScriptProps_Handle612CE0C3,
};

// One handler on one vector and pass: EBX, ESI, EDI and EBP checked kept;
// the argument slots after (the window's: whether it is still the window,
// else its top and low bytes, the window being 256-aligned, or the word).
void PhRun(std::size_t handler, PhWindow& window, PhVector& properties, uint32_t pass)
{
    const uint32_t window32 = PrArg(&window);
    PmRegs regs = PmRegsOf(0x13572468u, kPmEsi, kPmEdi, window32, PrArg(properties.vector));
    regs.args[2] = 0xABCDEF00u | pass;
    PmCall(kPhHandlers[handler], regs);
    const uint32_t slot = regs.args[0];
    if (slot == window32)
        Mix(0x5A5Au);
    else if ((slot & 0x00FFFF00u) == (window32 & 0x00FFFF00u))
        Mix(0xB0000000u ^ (slot & 0xFF0000FFu));
    else
        Mix(slot);
    Mix(regs.args[1] == PrArg(properties.vector) ? 1u : 0u);
    Mix(regs.args[2]);
    Mix(regs.args[3]);
    Mix(window.control->count);
    Mix(window.control->calls);
}

void DrivePhHandlers()
{
    // The held object of the UI script loader: ours for the stage.
    void* holder = nullptr;
    CallThis<void*>(addr::SC4RefPtr_UIScriptLoader, &holder);
    Mix(holder ? 1u : 0u);
    if (!holder) {
        PhStage("handle");
        return;
    }
    void* const old = Virtual<void*>(holder, 0x24);
    if (old)
        Virtual<uint32_t>(old, 4);
    PmHeld held = {PhHeldTable(), 0};
    Virtual<uint32_t>(holder, 0x28, static_cast<void*>(&held));
    for (uint32_t i = 0; i < 4; ++i)
        g_phImages[i] = {PhImageTable(), i + 1, 0, {int32_t(i), int32_t(i) * 2, 100 + int32_t(i), 50 - int32_t(i)}};

    // Every read succeeds; the second has the other branches; the third
    // malformed texts and values that are not numbers; then none.
    PhVector full = {
        {0xF005, 'n', 0xF006, nullptr}, {0xF005, 't', 0, "text"}, {0xF005, '0', 0, nullptr},
        {0xF005, 'n', 0x205, nullptr},  {0xF005, 'n', 0xF00B, nullptr}, {0xF005, 'n', 0x208, nullptr},
        {0x200, 't', 0, "(1,2,3)"},     {0x201, 't', 0, "(255,0,128)"}, {0x202, 't', 0, "(4,5)"},
        {0x203, 't', 0, "(300,-1,7)"},  {0xF003, 'n', 1, nullptr},      {0x300, 'n', 0, nullptr},
        {0x301, 'n', 0, nullptr},       {0xF000, 'n', 7, nullptr},      {0xF002, 't', 0, "(10,20,30)"},
        {0xF001, 't', 0, "(40,50,60)"}, {0xF004, 't', 0, "(3,4)"},      {0x305, 't', 0, "(7,8)"},
        {0xF015, 'n', 0xF008, nullptr}, {0x304, 'n', 1, nullptr},       {0xF00F, 't', 0, "(1,1,1)"},
        {0xF010, 't', 0, "(2,2,2)"},    {0xF011, 't', 0, "(3,3,3)"},    {0xF012, 't', 0, "(4,4,4)"},
        {0xF013, 't', 0, "(5,5,5)"},    {0xF014, 't', 0, "(6,6,6)"},    {0x407, 't', 0, "(7,7,7)"},
        {0x408, 't', 0, "(8,8,8)"},     {0x40B, 't', 0, "(9,9,9)"},     {0xF019, 'n', 1, nullptr},
        {0x409, 'n', 0x99, nullptr},    {0x40C, 'n', 0, nullptr},       {0x40D, 'n', 0x0D, nullptr},
        {0x40E, 't', 0, "(14,15)"},     {0x40F, 'n', 0x0F, nullptr},    {0x410, 'n', 0x10, nullptr},
        {0x413, 't', 0, "{1234,3}"},    {0x414, 't', 0, "(1,2,3,4)"},   {0x411, 't', 0, "{5678,5}"},
        {0x400, 'n', 1, nullptr},       {0x401, 'n', 0, nullptr},       {0x402, 'n', 1, nullptr},
        {0x403, 'n', 1, nullptr},       {0x415, 'n', 1, nullptr},       {0x502, 't', 0, "item one"},
        {0x502, 't', 0, "item two"},    {0x503, 'n', 1, nullptr},       {0x501, 'n', 3, nullptr},
        {0x505, 'n', 5, nullptr},       {0x506, 'n', 6, nullptr},       {0xF00B, 'n', 1, nullptr},
        {0x500, 't', 0, "(9,10)"},      {0x504, 'n', 1, nullptr},       {0xA00, 'n', 1, nullptr},
        {0xA01, 'n', 0, nullptr},       {0xA02, 'n', 2, nullptr},       {0xF00D, 'n', 0, nullptr},
        {0xA03, 'n', 1, nullptr},       {0xA04, 'n', 0, nullptr},       {0xA05, 'n', 1, nullptr},
        {0xA06, 'n', 1, nullptr},       {0xA07, 'n', 0, nullptr},       {0xA08, 'n', 1, nullptr},
        {0xF016, 't', 0, "{ab,7}"},     {0xA10, 't', 0, "a caption"},   {0xA12, 't', 0, "{2026,9}"},
        {0xA11, 't', 0, "(11,12)"},     {0xA18, 'n', 0x20, nullptr},    {0xA19, 't', 0, "{1,2}"},
        {0xA1A, 't', 0, "{3,4}"},       {0xB00, 'n', 300, nullptr},     {0xB01, 'n', 1, nullptr},
        {0xB02, 'n', 0, nullptr},       {0xF018, 't', 0, "(5,6,7,8)"},  {0xC00, 't', 0, "(9,3)"},
        {0xC01, 'n', 0xC03, nullptr},   {0x40A, 'n', 0x4A, nullptr},    {0x1000, 'n', 0, nullptr},
        {0x1001, 'n', 1, nullptr},      {0x1002, 'n', 0, nullptr},      {0x1003, 'n', 1, nullptr},
        {0x1004, 't', 0, "(13,14)"},    {0x1005, 'n', 0x15, nullptr},   {0x1006, 't', 0, "(16,16,16)"},
        {0x1007, 't', 0, "(17,17,17)"}, {0x1008, 't', 0, "(18,18,18)"}, {0x1009, 't', 0, "(19,19,19)"},
        {0x100A, 't', 0, "(20,20,20)"},
    };
    PhVector other = {
        {0xF005, 'n', 0xA0D, nullptr},  {0xF005, 'n', 0xF00E, nullptr}, {0xF005, 'n', 0x206, nullptr},
        {0xF005, 'n', 0xF007, nullptr}, {0xF005, 'n', 0x204, nullptr},  {0xF005, 'n', 0xF00C, nullptr},
        {0x202, 't', 0, "(9,8,7)"},     {0xF003, 'n', 0, nullptr},      {0x301, 'n', 2, nullptr},
        {0xF000, 't', 0, "a font"},     {0xF004, 't', 0, "(1,2,3,4)"},  {0xF015, 'n', 0xF007, nullptr},
        {0xF011, 't', 0, "(30,31,32)"}, {0x40B, 't', 0, "(1,2,3)(4,5,6)"}, {0xF019, 'n', 0, nullptr},
        {0x40C, 'n', 5, nullptr},       {0x413, 't', 0, "{1234,4}"},    {0x411, 't', 0, "{5678,1}"},
        {0x412, 't', 0, "(9,9,9,9)"},   {0x503, 'n', 2, nullptr},       {0x402, 'n', 0, nullptr},
        {0x401, 'n', 1, nullptr},       {0x502, 'x', 0, nullptr},       {0xA02, 'n', 0, nullptr},
        {0xF00D, 'n', 1, nullptr},      {0xA07, 'n', 1, nullptr},       {0xF016, 't', 0, "{cd,4}"},
        {0xA12, 't', 0, "{2026,8}"},    {0xA19, 't', 0, "{5,6}"},       {0xB00, 'n', 128, nullptr},
        {0xC00, 't', 0, "(3,9)"},       {0xC01, 'n', 5, nullptr},       {0x1000, 'n', 0xF006, nullptr},
        {0x1008, 't', 0, "(1,2,3)"},    {0xF018, 't', 0, "(1,2)"},
    };
    PhVector odd = {
        {0xF005, 'n', 0xA1B, nullptr}, {0xF000, 'x', 0, nullptr},     {0xF015, 'n', 0xF006, nullptr},
        {0x200, 't', 0, "(1,2"},       {0xF004, 't', 0, "(1,2,3)"},   {0x40B, 't', 0, "garbage"},
        {0xF019, 'n', 2, nullptr},     {0x413, 't', 0, "{zz}"},       {0xF016, 't', 0, "{ef,9}"},
        {0xA18, 'x', 0, nullptr},      {0x1000, 'n', 0xF007, nullptr}, {0xC00, 't', 0, "(5)"},
        {0xC01, 'n', 0xC03, nullptr},  {0xF003, 'x', 0, nullptr},     {0x304, '0', 0, nullptr},
        {0xF005, 'n', 0xA09, nullptr}, {0xB00, 'n', 0xFF, nullptr},   {0xF00D, 'x', 0, nullptr},
    };
    PhVector empty = {};

    const auto& faces = PhInterfaces();
    for (std::size_t h = 0; h < std::size(kPhHandlers); ++h) {
        PhControl control = {PhControlTable(faces[h]), &faces[h], 0, 0};
        PhWindow window = {PhWindowTable(), &control, false};
        PhRun(h, window, full, 1);
        window.give = true;
        for (PhVector* properties : {&full, &other, &odd, &empty}) {
            for (uint32_t pass : {0u, 1u})
                PhRun(h, window, *properties, pass);
        }
        log::Write("autotest: prophandle: %08X %08X", faces[h].iid, g_phHash);
    }
    for (const PhImage& image : g_phImages)
        Mix(image.count);

    Virtual<uint32_t>(holder, 0x28, old);
    if (old)
        Virtual<uint32_t>(old, 8);
    Mix(held.count);
    CallThis<uint32_t>(addr::SC4RefPtr_Destruct, &holder);
    PhStage("handle");
}

// OPENSC4_TEST_PROPHANDLE=<day>: part u12's third slice by address. Every
// result goes into the fingerprint's `ph` part.
void DrivePropHandleDay(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_PROPHANDLE") || !game::IsBuild641())
        return;
    g_phOn = true;
    uint32_t* const saved = g_into;
    g_into = &g_phHash;
    DrivePhWords();
    DrivePhHandlers();
    g_into = saved;
    log::Write("autotest: prophandle driven (hash %08X)", g_phHash);
}

void PhFingerprint(cISC4City*, std::string& line)
{
    if (!g_phOn)
        return;
    char text[32];
    std::snprintf(text, sizeof text, " | ph %08X", g_phHash);
    line += text;
}

const FingerprintPart kPhFingerprint{"prophandle", 4, &PhFingerprint};
const DayHook kPhDrive{"prophandle", 4, &DrivePropHandleDay};


// ====================================================================================
// Part u17: the UI script writers (OPENSC4_TEST_WRITERS). All called by
// address (registers checked kept after): the printers on out strings of
// the game's, with names and values of our own; the property helpers on
// windows of our own (slot 113 gives script values of our own, whose
// slots hash what they get); each class's writer on an interface of our
// own (every slot it calls hashes its number and what it gets, and gives
// the case's answers), its window one of ours, the game's font and colour
// services. After each call the out string's text is hashed. Addresses
// never go into the hash.

bool g_wtOn = false;
uint32_t g_wtHash = 2166136261u;

void WtStage(const char* name) { log::Write("autotest: writers: %s %08X", name, g_wtHash); }

// A call with EAX, ECX, EBX, ESI and EDI set and fourteen stack arguments
// (the callee pops none); EAX and the four kept registers back in the
// block.
struct WtRegs {
    uint32_t eax, ecx, ebx, esi, edi;
    uint32_t args[14];
    uint32_t outEax, outEbx, outEsi, outEdi, outEbp;
    std::uintptr_t address;
};

__declspec(naked) void __cdecl WtCallRaw(WtRegs*)
{
    __asm push ebx
    __asm push esi
    __asm push edi
    __asm push ebp
    __asm mov ebp, [esp + 20]
    __asm push ebp
    __asm push dword ptr [ebp + 72]
    __asm push dword ptr [ebp + 68]
    __asm push dword ptr [ebp + 64]
    __asm push dword ptr [ebp + 60]
    __asm push dword ptr [ebp + 56]
    __asm push dword ptr [ebp + 52]
    __asm push dword ptr [ebp + 48]
    __asm push dword ptr [ebp + 44]
    __asm push dword ptr [ebp + 40]
    __asm push dword ptr [ebp + 36]
    __asm push dword ptr [ebp + 32]
    __asm push dword ptr [ebp + 28]
    __asm push dword ptr [ebp + 24]
    __asm push dword ptr [ebp + 20]
    __asm mov eax, [ebp + 0]
    __asm mov ecx, [ebp + 4]
    __asm mov ebx, [ebp + 8]
    __asm mov esi, [ebp + 12]
    __asm mov edi, [ebp + 16]
    __asm call dword ptr [ebp + 96]
    __asm add esp, 56
    __asm push eax
    __asm mov eax, [esp + 4]
    __asm mov [eax + 80], ebx
    __asm mov [eax + 84], esi
    __asm mov [eax + 88], edi
    __asm mov [eax + 92], ebp
    __asm pop dword ptr [eax + 76]
    __asm add esp, 4
    __asm pop ebp
    __asm pop edi
    __asm pop esi
    __asm pop ebx
    __asm ret
}

// Registers the callee does not read are marks.
WtRegs WtRegsOf()
{
    WtRegs regs = {};
    regs.eax = 0xEAEAEAE1u;
    regs.ecx = 0xC1C1C1C2u;
    regs.ebx = kPmEbx;
    regs.esi = kPmEsi;
    regs.edi = kPmEdi;
    return regs;
}

uint32_t WtCall(std::uintptr_t address, WtRegs& regs)
{
    regs.address = address;
    WtCallRaw(&regs);
    const bool kept = regs.outEbx == regs.ebx && regs.outEsi == regs.esi && regs.outEdi == regs.edi &&
                      regs.outEbp == PrArg(&regs);
    Mix(kept ? 0x4B1u : 0xBAD0u);
    return regs.outEax;
}

// A game string (SC4String) on our stack: empty, or with a text; its text
// hashed, then destroyed.
struct WtString {
    alignas(4) char bytes[0x14];
    WtString() { CallThis<void*>(addr::SC4String_SC4String, bytes); }
    explicit WtString(const char* text) { GdString(bytes, text); }
    ~WtString() { GdEndString(bytes); }
    WtString(const WtString&) = delete;
    WtString& operator=(const WtString&) = delete;
    uint32_t Arg() { return PrArg(bytes); }
    void MixText() { PrMixText(bytes); }
};

// A script value of our own (what a window's slot 113 gives): its kind
// (slot 3), type (slot 4), text (slot 55 assigns it to the string given),
// 12 bytes (slot 57), word (slot 73) and pair (slot 103) from `words`;
// each slot hashes its number and what it gets; Release (slot 2) counts.
struct WtValue {
    void** vtable;
    uint32_t kind;
    uint32_t type;
    uint32_t words[3];
    const char* text;
    uint32_t releases;
};

uint32_t __fastcall WtValueRelease(WtValue* self, void*)
{
    Mix(0x7E000002u);
    ++self->releases;
    return 0x52520000u | self->releases;
}

uint32_t __fastcall WtValueKind(WtValue* self, void*)
{
    Mix(0x7E000003u);
    return self->kind;
}

uint32_t __fastcall WtValueType(WtValue* self, void*)
{
    Mix(0x7E000004u);
    return self->type;
}

uint32_t __fastcall WtValueText(WtValue* self, void*, char* string)
{
    Mix(0x7E000037u);
    return Virtual<uint32_t>(string, 0xC, self->text, static_cast<uint32_t>(std::strlen(self->text)));
}

uint32_t __fastcall WtValueBytes(WtValue* self, void*, uint32_t* out, const uint32_t* count)
{
    Mix(0x7E000039u);
    Mix(*count);
    for (uint32_t i = 0; i < 3; ++i)
        out[i] = self->words[i];
    return 0x39390001u;
}

uint32_t* __fastcall WtValueWord(WtValue* self, void*)
{
    Mix(0x7E000049u);
    return self->words;
}

uint32_t* __fastcall WtValuePair(WtValue* self, void*)
{
    Mix(0x7E000067u);
    return self->words;
}

void** WtValueTable()
{
    static void* table[104] = {};
    table[2] = reinterpret_cast<void*>(&WtValueRelease);
    table[3] = reinterpret_cast<void*>(&WtValueKind);
    table[4] = reinterpret_cast<void*>(&WtValueType);
    table[55] = reinterpret_cast<void*>(&WtValueText);
    table[57] = reinterpret_cast<void*>(&WtValueBytes);
    table[73] = reinterpret_cast<void*>(&WtValueWord);
    table[103] = reinterpret_cast<void*>(&WtValuePair);
    return table;
}

WtValue WtNumber(uint32_t word) { return {WtValueTable(), 6, 6, {word, 0, 0}, "", 0}; }
WtValue WtTextValue(const char* text) { return {WtValueTable(), 0x800E, 0x800E, {0, 0, 0}, text, 0}; }
WtValue WtBytes(uint32_t a, uint32_t b, uint32_t c) { return {WtValueTable(), 0xC, 0xC, {a, b, c}, "", 0}; }
WtValue WtPairValue(uint32_t a, uint32_t b) { return {WtValueTable(), 0x8007, 0x8007, {a, b, 0}, "", 0}; }

// A window of our own: slot 113 finds the key among its properties (the
// value given, AL 1) or not (AL 0, the out left as it was); it hashes the
// key. A writer that calls other window slots builds its own table from
// this one (WtWindowSlots) and adds them.
struct WtProp {
    uint32_t key;
    WtValue* value;
};

struct WtWindow {
    void** vtable;
    const WtProp* props;
    std::size_t count;
};

uint32_t __fastcall WtWindowProperty(WtWindow* self, void*, uint32_t key, WtValue** out)
{
    Mix(0x7E000071u);
    Mix(key);
    for (std::size_t i = 0; i < self->count; ++i) {
        if (self->props[i].key == key) {
            *out = self->props[i].value;
            return 0x71710001u;
        }
    }
    return 0x71710000u;
}

void WtWindowSlots(void** table) { table[113] = reinterpret_cast<void*>(&WtWindowProperty); }

void** WtWindowTable()
{
    static void* table[114] = {};
    WtWindowSlots(table);
    return table;
}

// ------------------------------------------------------------------------------------
// The printers

void DriveWtLines()
{
    WtString name("nm");
    WtString value("val ue");
    WtString empty;
    {
        // The colours: the bytes' upper bytes set (only the low ones print).
        WtString out("pre ");
        WtRegs regs = WtRegsOf();
        regs.args[0] = out.Arg();
        regs.args[1] = name.Arg();
        for (uint32_t i = 0; i < 12; ++i)
            regs.args[2 + i] = 0xABCDEF00u + i * 23u;
        WtCall(addr::cGZUIScriptWriters_LineColour, regs);
        WtCall(addr::cGZUIScriptWriters_LineColours2, regs);
        out.MixText();
    }
    {
        WtString out;
        WtRegs regs = WtRegsOf();
        regs.args[0] = out.Arg();
        regs.args[1] = name.Arg();
        for (uint32_t i = 0; i < 12; ++i)
            regs.args[2 + i] = 0x12345600u + i * 37u;
        WtCall(addr::cGZUIScriptWriters_LineColours4, regs);
        out.MixText();
    }
    const uint32_t numbers[][4] = {
        {0, 0, 0, 0}, {1, 0xFFFFFFFFu, 0x7FFFFFFFu, 0x80000000u}, {0xDEADBEEFu, 12345, 0xFFFFFF00u, 7}};
    for (const auto& n : numbers) {
        WtString out("x");
        WtRegs regs = WtRegsOf();
        regs.args[0] = out.Arg();
        regs.args[1] = name.Arg();
        for (uint32_t i = 0; i < 4; ++i)
            regs.args[2 + i] = n[i];
        WtCall(addr::cGZUIScriptWriters_LineInt, regs);
        WtCall(addr::cGZUIScriptWriters_LineHex, regs);
        WtCall(addr::cGZUIScriptWriters_LineKey, regs);
        WtCall(addr::cGZUIScriptWriters_LinePair, regs);
        WtCall(addr::cGZUIScriptWriters_LineRect, regs);
        WtCall(addr::cGZUIScriptWriters_LineMove, regs);
        // The bool's low byte only.
        WtCall(addr::cGZUIScriptWriters_LineBool, regs);
        out.MixText();
    }
    for (WtString* v : {&value, &empty}) {
        WtString out;
        WtRegs regs = WtRegsOf();
        regs.args[0] = out.Arg();
        regs.args[1] = name.Arg();
        regs.args[2] = v->Arg();
        WtCall(addr::cGZUIScriptWriters_LineWord, regs);
        WtCall(addr::cGZUIScriptWriters_LineQuoted, regs);
        regs.args[1] = empty.Arg();
        WtCall(addr::cGZUIScriptWriters_LineWord, regs);
        out.MixText();
        // The append: EAX the string, ECX the out string.
        WtRegs append = WtRegsOf();
        append.eax = v->Arg();
        append.ecx = out.Arg();
        WtCall(addr::cGZUIScriptWriters_LineAppend, append);
        out.MixText();
    }
    {
        // The option: EDI the name, ESI the text; with and without a key.
        const uint32_t keys[][2] = {{0, 0}, {1, 0}, {0, 2}, {0x11112222u, 0x33334444u}};
        for (const auto& key : keys) {
            WtString out;
            WtRegs regs = WtRegsOf();
            regs.edi = name.Arg();
            regs.esi = value.Arg();
            regs.args[0] = out.Arg();
            regs.args[1] = 17;
            regs.args[2] = 0xFFFFFFF0u;
            regs.args[3] = key[0];
            regs.args[4] = key[1];
            regs.args[5] = 0xFFFFFFFFu;
            regs.args[6] = 3;
            regs.args[7] = 640;
            regs.args[8] = 0x80000000u;
            WtCall(addr::cGZUIScriptWriters_LineOption, regs);
            out.MixText();
        }
    }
    {
        // The flag table: rows that differ, rows always written, an
        // empty table.
        struct Row {
            uint32_t mask;
            const char* name;
            uint32_t always;
        };
        static const Row rows[] = {{1, "one", 0},          {2, "two", 0xFFFFFF00u}, {4, "four", 1},
                                   {0x18, "multi", 0},     {0x80000000u, "top", 0}, {0, "zero", 0x101u},
                                   {0, nullptr, 0}};
        const uint32_t masks[][2] = {{0, 0}, {0xFFFFFFFFu, 0}, {5, 0x80000008u}, {0x10, 0x10}};
        for (const auto& m : masks) {
            WtString out;
            WtRegs regs = WtRegsOf();
            regs.eax = PrArg(rows);
            regs.args[0] = out.Arg();
            regs.args[1] = m[0];
            regs.args[2] = m[1];
            WtCall(addr::cGZUIScriptWriters_LineFlags, regs);
            out.MixText();
        }
        WtString out;
        WtRegs regs = WtRegsOf();
        regs.eax = PrArg(&rows[6]);
        regs.args[0] = out.Arg();
        WtCall(addr::cGZUIScriptWriters_LineFlags, regs);
        out.MixText();
    }
    {
        // A font: the known words, the default and an unknown one; AL.
        const uint32_t fonts[] = {kFbFontWords[0], kFbFontWords[1], kFbFontWords[2], 0x68963C4Cu, 0x12345678u, 0};
        for (uint32_t font : fonts) {
            WtString out("f");
            WtRegs regs = WtRegsOf();
            regs.esi = out.Arg();
            regs.args[0] = font;
            MixByte(WtCall(addr::cGZUIScriptWriters_LineFont, regs));
            out.MixText();
        }
    }
    {
        // The key compare (thiscall).
        const uint32_t a[3] = {1, 2, 3};
        const uint32_t b[3] = {9, 2, 3};
        const uint32_t c[3] = {1, 2, 4};
        const uint32_t d[3] = {1, 5, 3};
        Mix(CallThis<uint32_t>(addr::cGZUIScriptWriters_KeyEquals, const_cast<uint32_t*>(a), b));
        Mix(CallThis<uint32_t>(addr::cGZUIScriptWriters_KeyEquals, const_cast<uint32_t*>(a), c));
        Mix(CallThis<uint32_t>(addr::cGZUIScriptWriters_KeyEquals, const_cast<uint32_t*>(a), d));
    }
    WtStage("lines");
}

// ------------------------------------------------------------------------------------
// The property helpers

void DriveWtProps()
{
    WtValue bytes = WtBytes(0x11223344u, 0x55667788u, 0x99AABBCCu);
    WtValue notBytes = WtNumber(5);
    WtValue pair = WtPairValue(0xFFFFFFFEu, 77);
    WtValue noPair = WtNumber(8);
    WtValue font0 = WtNumber(kFbFontWords[0]);
    WtValue font2 = WtNumber(kFbFontWords[2]);
    WtValue number = WtNumber(0xFFFFFF85u);
    WtValue text = WtTextValue("Some Text");
    WtValue other = WtBytes(1, 2, 3);
    const WtProp props[] = {
        {0x1000, &bytes},          {0x1001, &notBytes},       {0xDAA3B860u + 3, &pair},
        {0xDAA3B860u, &noPair},    {0xFAA3BE85u, &font0},     {0xFAA3BE85u + 1, &font2},
        {0xFAA3BE85u + 2, &number}, {0xFAA3BE85u + 3, &text}, {0xFAA3BE85u + 4, &other},
    };
    WtWindow window = {WtWindowTable(), props, sizeof props / sizeof props[0]};
    // 12 bytes: found, the wrong type, missing, and no window.
    for (uint32_t key : {0x1000u, 0x1001u, 0x1002u}) {
        uint32_t out[4] = {0xA0, 0xA1, 0xA2, 0xA3};
        WtRegs regs = WtRegsOf();
        regs.ecx = PrArg(&window);
        regs.args[0] = key;
        regs.args[1] = PrArg(out);
        Mix(WtCall(addr::cGZUIScriptWriters_PropertyBytes, regs));
        for (uint32_t w : out)
            Mix(w);
    }
    {
        WtRegs regs = WtRegsOf();
        regs.ecx = 0;
        regs.eax = 0x0E0E0E77u;
        regs.args[0] = 0x1000;
        Mix(WtCall(addr::cGZUIScriptWriters_PropertyBytes, regs));
    }
    // A pair: found, the wrong kind, missing.
    for (uint32_t index : {3u, 0u, 9u}) {
        uint32_t first = 0xF1, second = 0xF2;
        WtRegs regs = WtRegsOf();
        regs.ecx = PrArg(&window);
        regs.args[0] = PrArg(&first);
        regs.args[1] = PrArg(&second);
        regs.args[2] = index;
        Mix(WtCall(addr::cGZUIScriptWriters_PropertyPair, regs));
        Mix(first);
        Mix(second);
    }
    // A font or a text: two fonts, a number, a text, another kind, missing.
    for (uint32_t index = 0; index < 6; ++index) {
        WtString out("old");
        WtRegs regs = WtRegsOf();
        regs.ecx = PrArg(&window);
        regs.edi = out.Arg();
        regs.args[0] = index;
        Mix(WtCall(addr::cGZUIScriptWriters_PropertyFont, regs));
        out.MixText();
    }
    for (WtValue* v : {&bytes, &notBytes, &pair, &noPair, &font0, &font2, &number, &text, &other})
        Mix(v->releases);
    WtStage("props");
}

// ------------------------------------------------------------------------------------
// GZWinFlatRect's writer (EAX the interface, EBX the out string)

struct WtFlatRectFace {
    void** vtable;
    uint32_t style;
    uint32_t skip; // sides (bits) whose colour the slot leaves unwritten
};

uint32_t __fastcall WtFlatRectColour(WtFlatRectFace* self, void*, uint32_t side, uint8_t* r, uint8_t* g, uint8_t* b)
{
    Mix(0x7E00000Bu);
    Mix(side);
    if (!(self->skip & (1u << side))) {
        *r = static_cast<uint8_t>(0x10 + side * 40 + self->style);
        *g = static_cast<uint8_t>(0xFF - side);
        *b = static_cast<uint8_t>(side * 3);
    }
    return 0x0B0B0000u | side;
}

uint32_t __fastcall WtFlatRectStyle(WtFlatRectFace* self, void*)
{
    Mix(0x7E00000Du);
    return 0xD0D0D000u | self->style;
}

void** WtFlatRectTable()
{
    static void* table[14] = {};
    table[11] = reinterpret_cast<void*>(&WtFlatRectColour);
    table[13] = reinterpret_cast<void*>(&WtFlatRectStyle);
    return table;
}

void DriveWtFlatRect()
{
    // The first side always written (the game's places are never set
    // before); later sides skipped keep the last colour.
    const WtFlatRectFace faces[] = {
        {WtFlatRectTable(), 0x00, 0x0}, {WtFlatRectTable(), 0x7F, 0x0}, {WtFlatRectTable(), 0x95, 0xA},
        {WtFlatRectTable(), 0x2A, 0xE}, {WtFlatRectTable(), 0xC0, 0x4}, {WtFlatRectTable(), 0x41, 0x0},
    };
    for (const WtFlatRectFace& face : faces) {
        WtString out("<");
        WtRegs regs = WtRegsOf();
        regs.eax = PrArg(&face);
        regs.ebx = out.Arg();
        WtCall(addr::cGZUIScriptWriters_FlatRect, regs);
        out.MixText();
    }
    WtStage("flatrect");
}

// ------------------------------------------------------------------------------------
// GZWinText's writer

// The autofit value: part u17's script value with a slot 63 (a pointer to
// its byte, the low byte of its first word).
uint8_t* __fastcall WtTextAutofitByte(WtValue* self, void*)
{
    Mix(0x7E00003Fu);
    return reinterpret_cast<uint8_t*>(self->words);
}

void** WtTextValueTable()
{
    static void* table[104] = {};
    void** const base = WtValueTable();
    for (std::size_t i = 0; i < 104; ++i)
        table[i] = base[i];
    table[63] = reinterpret_cast<void*>(&WtTextAutofitByte);
    return table;
}

// An interface of our own: the case number picks every answer.
struct WtTextFace {
    void** vtable;
    uint32_t c;
    WtWindow* window;
};

const uint32_t kWtTextFonts[6] = {kFbFontWords[0], kFbFontWords[1], kFbFontWords[2], 0x68963C4Cu, 0x12345678u, 0};

WtWindow* __fastcall WtTextWindow(WtTextFace* self, void*)
{
    Mix(0x7E540003u);
    return self->window;
}

uint32_t __fastcall WtTextFlag(WtTextFace* self, void*, uint32_t mask)
{
    Mix(0x7E540006u);
    Mix(mask);
    return 0x06060600u | ((self->c & mask) ? 1u : 0u);
}

uint32_t __fastcall WtTextColour(WtTextFace* self, void*, uint32_t index, uint32_t* colour)
{
    Mix(0x7E54000Cu);
    Mix(index);
    const bool yes = index == 0 ? self->c % 3 != 2 : self->c % 3 != 1;
    if (yes)
        *colour = self->c * 0x0F0E0Du + index * 0x40u + 0x00102030u;
    return 0x0C0C0C00u | (yes ? 1u : 0u);
}

uint32_t __fastcall WtTextFont(WtTextFace* self, void*, uint32_t* font)
{
    Mix(0x7E540014u);
    const uint32_t pick = self->c % 6;
    if (pick == 5)
        return 0x14141400u;
    *font = kWtTextFonts[pick];
    return 0x14141401u;
}

uint32_t __fastcall WtTextAlign(WtTextFace* self, void*, uint32_t* align)
{
    Mix(0x7E540016u);
    if (self->c == 11)
        return 0x16161600u;
    *align = self->c == 10 ? 0xFFFFFFFFu : self->c;
    return 0x16161601u;
}

uint32_t __fastcall WtTextGutters(WtTextFace* self, void*, int8_t* first, int8_t* second)
{
    Mix(0x7E540018u);
    if (self->c % 2)
        return 0x18181800u;
    *first = static_cast<int8_t>(self->c * 23 - 100);
    *second = static_cast<int8_t>(0u - self->c);
    return 0x18181801u;
}

uint32_t __fastcall WtTextOffsets(WtTextFace* self, void*, int8_t* first, int8_t* second)
{
    Mix(0x7E54001Au);
    if (self->c % 4 == 1)
        return 0x1A1A1A00u;
    *first = static_cast<int8_t>(self->c * 50);
    *second = static_cast<int8_t>(127 - self->c);
    return 0x1A1A1A01u;
}

void** WtTextTable()
{
    static void* table[27] = {};
    table[3] = reinterpret_cast<void*>(&WtTextWindow);
    table[6] = reinterpret_cast<void*>(&WtTextFlag);
    table[12] = reinterpret_cast<void*>(&WtTextColour);
    table[20] = reinterpret_cast<void*>(&WtTextFont);
    table[22] = reinterpret_cast<void*>(&WtTextAlign);
    table[24] = reinterpret_cast<void*>(&WtTextGutters);
    table[26] = reinterpret_cast<void*>(&WtTextOffsets);
    return table;
}

void DriveWtText()
{
    for (uint32_t c = 0; c < 12; ++c) {
        // Autofit: no property, a 0 byte, a 1 byte.
        WtValue autofit = WtNumber(c % 3 == 2 ? 0xFFFFFF01u : 0xFFFFFF00u);
        autofit.vtable = WtTextValueTable();
        const WtProp props[] = {{0xDACF8F9Cu, &autofit}};
        WtWindow window = {WtWindowTable(), props, c % 3 == 0 ? 0u : 1u};
        WtTextFace face = {WtTextTable(), c, &window};
        WtString out("<");
        WtRegs regs = WtRegsOf();
        regs.args[0] = PrArg(&face);
        regs.args[1] = out.Arg();
        WtCall(addr::cGZUIScriptWriters_Text, regs);
        out.MixText();
        Mix(autofit.releases);
    }
    WtStage("text");
}

// ------------------------------------------------------------------------------------
// GZWinTextEdit's writer (uses WtTextValueTable and kWtTextFonts of the
// Text stage)

struct WtTextEditFace {
    void** vtable;
    uint32_t c;
    WtWindow* window;
};

WtWindow* __fastcall WtTextEditWindow(WtTextEditFace* self, void*)
{
    Mix(0x7E550005u);
    return self->window;
}

uint32_t __fastcall WtTextEditFont(WtTextEditFace* self, void*, uint32_t* font)
{
    Mix(0x7E55000Bu);
    const uint32_t pick = self->c % 6;
    if (pick == 5)
        return 0x0B0B0B00u;
    *font = kWtTextFonts[pick];
    return 0x0B0B0B01u;
}

uint32_t __fastcall WtTextEditColour(WtTextEditFace* self, void*, uint32_t state, uint32_t* colour)
{
    Mix(0x7E55000Eu);
    Mix(state);
    if ((self->c + state) % 3 == 0)
        return 0x0E0E0E00u;
    *colour = self->c * 0x010203u + state * 0x111u;
    return 0x0E0E0E01u;
}

uint32_t __fastcall WtTextEditCaretColour(WtTextEditFace* self, void*, uint32_t* colour)
{
    Mix(0x7E550013u);
    if (!(self->c & 1))
        return 0x13131300u;
    *colour = 0x00FF00FFu - self->c;
    return 0x13131301u;
}

uint32_t __fastcall WtTextEditHighlight(WtTextEditFace* self, void*)
{
    Mix(0x7E550016u);
    return self->c * 0x30201u;
}

uint32_t __fastcall WtTextEditMaxText(WtTextEditFace* self, void*)
{
    Mix(0x7E550020u);
    return self->c * 1000u - 1u;
}

uint32_t __fastcall WtTextEditOutline(WtTextEditFace* self, void*, uint8_t* on, uint32_t* colour)
{
    Mix(0x7E550026u);
    *on = self->c % 3 == 0 ? 0 : static_cast<uint8_t>(self->c);
    *colour = 0x00ABCDEFu ^ self->c;
    return 0x26262600u;
}

uint32_t __fastcall WtTextEditGutterX(WtTextEditFace* self, void*)
{
    Mix(0x7E550027u);
    return self->c;
}

uint32_t __fastcall WtTextEditGutterY(WtTextEditFace* self, void*)
{
    Mix(0x7E550029u);
    return 0u - self->c;
}

const char* __fastcall WtTextEditText(WtTextEditFace* self, void*)
{
    Mix(0x7E55002Eu);
    static const char* const texts[3] = {"hello world", "", "with \"quote\" inside"};
    return texts[self->c % 3];
}

uint32_t __fastcall WtTextEditInsertMode(WtTextEditFace* self, void*)
{
    Mix(0x7E550041u);
    return 0xFFFFFF00u | (self->c & 1);
}

uint32_t __fastcall WtTextEditInsertIndex(WtTextEditFace* self, void*, uint32_t* index)
{
    Mix(0x7E550043u);
    if (self->c % 3 == 2)
        return 0x43434300u;
    *index = self->c * 7u;
    return 0x43434301u;
}

uint32_t __fastcall WtTextEditInsertPos(WtTextEditFace* self, void*, uint32_t* x, uint32_t* y)
{
    Mix(0x7E550045u);
    if (self->c % 2)
        return 0x45454500u;
    *x = self->c * 11u;
    *y = 0u - self->c;
    return 0x45454501u;
}

uint32_t __fastcall WtTextEditCaretPeriod(WtTextEditFace* self, void*, uint32_t* period)
{
    Mix(0x7E550049u);
    if (self->c % 4 == 3)
        return 0x49494900u;
    *period = 500u + self->c;
    return 0x49494901u;
}

uint32_t __fastcall WtTextEditFlags(WtTextEditFace* self, void*)
{
    Mix(0x7E550050u);
    static const uint32_t flags[5] = {0x11FFu, 0, 0xFFFFFFFFu, 0x2A55u, 0x9C00u};
    return flags[self->c % 5];
}

uint32_t __fastcall WtTextEditMaxUndo(WtTextEditFace* self, void*)
{
    Mix(0x7E550052u);
    return 0x80000000u + self->c;
}

uint32_t WtTextEditRect(WtTextEditFace* self, uint32_t* place, uint32_t* rect, bool yes, uint32_t salt)
{
    if (!yes)
        return 0x55555500u;
    for (uint32_t i = 0; i < 4; ++i)
        place[i] = 0xDDDD0000u + i;
    rect[0] = self->c + salt;
    rect[1] = self->c + 1;
    rect[2] = 0u - self->c;
    rect[3] = 0x7FFFFFFFu - salt;
    return 0x55555501u;
}

uint32_t __fastcall WtTextEditVRect(WtTextEditFace* self, void*, uint32_t* place, uint32_t* rect)
{
    Mix(0x7E550055u);
    return WtTextEditRect(self, place, rect, self->c % 2 == 1, 100);
}

uint32_t __fastcall WtTextEditHRect(WtTextEditFace* self, void*, uint32_t* place, uint32_t* rect)
{
    Mix(0x7E550056u);
    return WtTextEditRect(self, place, rect, self->c % 3 != 0, 200);
}

void** WtTextEditTable()
{
    static void* table[87] = {};
    table[5] = reinterpret_cast<void*>(&WtTextEditWindow);
    table[11] = reinterpret_cast<void*>(&WtTextEditFont);
    table[14] = reinterpret_cast<void*>(&WtTextEditColour);
    table[19] = reinterpret_cast<void*>(&WtTextEditCaretColour);
    table[22] = reinterpret_cast<void*>(&WtTextEditHighlight);
    table[32] = reinterpret_cast<void*>(&WtTextEditMaxText);
    table[38] = reinterpret_cast<void*>(&WtTextEditOutline);
    table[39] = reinterpret_cast<void*>(&WtTextEditGutterX);
    table[41] = reinterpret_cast<void*>(&WtTextEditGutterY);
    table[46] = reinterpret_cast<void*>(&WtTextEditText);
    table[65] = reinterpret_cast<void*>(&WtTextEditInsertMode);
    table[67] = reinterpret_cast<void*>(&WtTextEditInsertIndex);
    table[69] = reinterpret_cast<void*>(&WtTextEditInsertPos);
    table[73] = reinterpret_cast<void*>(&WtTextEditCaretPeriod);
    table[80] = reinterpret_cast<void*>(&WtTextEditFlags);
    table[82] = reinterpret_cast<void*>(&WtTextEditMaxUndo);
    table[85] = reinterpret_cast<void*>(&WtTextEditVRect);
    table[86] = reinterpret_cast<void*>(&WtTextEditHRect);
    return table;
}

void DriveWtTextEdit()
{
    for (uint32_t c = 0; c < 12; ++c) {
        // The scroll images: none, the h one, the v one with autofit set,
        // both with the h one of another type and autofit 0.
        WtValue h = c % 4 == 3 ? WtNumber(4) : WtBytes(0xAAAA0000u + c, 0x11112222u, 0x33334444u + c);
        WtValue v = WtBytes(0xBBBB0000u + c, 0x55556666u ^ c, 0x77778888u);
        WtValue autofit = WtNumber(c % 4 == 2 ? 0x01u : 0x100u);
        autofit.vtable = WtTextValueTable();
        WtProp props[3];
        std::size_t count = 0;
        if (c % 4 == 1 || c % 4 == 3)
            props[count++] = {0x8B4BFEFCu, &h};
        if (c % 4 >= 2) {
            props[count++] = {0x8B4BFEFDu, &v};
            props[count++] = {0xDACF8F9Cu, &autofit};
        }
        WtWindow window = {WtWindowTable(), props, count};
        WtTextEditFace face = {WtTextEditTable(), c, &window};
        WtString out;
        WtRegs regs = WtRegsOf();
        regs.ebx = PrArg(&face);
        regs.args[0] = out.Arg();
        WtCall(addr::cGZUIScriptWriters_TextEdit, regs);
        out.MixText();
        Mix(h.releases);
        Mix(v.releases);
        Mix(autofit.releases);
    }
    WtStage("textedit");
}

// ------------------------------------------------------------------------------------
// GZWinListBox's writer

// A script value with slot 63 too (the bool's place: the low byte of
// `words[0]`), for the window's autofit property.
const char* __fastcall WtListBoxValueBool(WtValue* self, void*)
{
    Mix(0x7E00003Fu);
    return reinterpret_cast<const char*>(self->words);
}

void** WtListBoxValueTable()
{
    static void* table[104] = {};
    if (!table[2]) {
        void** const base = WtValueTable();
        for (std::size_t i = 0; i < 104; ++i)
            table[i] = base[i];
        table[63] = reinterpret_cast<void*>(&WtListBoxValueBool);
    }
    return table;
}

WtValue WtListBoxBool(uint32_t on) { return {WtListBoxValueTable(), 1, 1, {on, 0, 0}, "", 0}; }

// The interface: each case says which getters answer and what they give.
// A getter that answers always writes its places (the game would print
// its stack otherwise).
struct WtListBoxCase {
    uint32_t answers; // bit per getter (below)
    uint8_t vertical, horizontal;
    uint32_t colours[3];
    uint32_t highlight;
    uint32_t outline[2];
    int8_t barGutters[2];
    int8_t gutters[2];
    uint32_t selection;
    int32_t count;
    uint32_t elementFails; // bit per index that does not answer
};

enum : uint32_t {
    kWtLbScroll = 1,
    kWtLbColour0 = 2,
    kWtLbColour1 = 4,
    kWtLbColour2 = 8,
    kWtLbHighlight = 0x10,
    kWtLbOutlineColours = 0x20,
    kWtLbSunken = 0x40,
    kWtLbOutline = 0x80,
    kWtLbBarGutters = 0x100,
    kWtLbGutters = 0x200,
    kWtLbSort = 0x400,
    kWtLbDrop = 0x800,
};

struct WtListBoxFace {
    void** vtable;
    const WtListBoxCase* c;
    WtWindow* window;
};

uint32_t WtListBoxAnswer(const WtListBoxFace* self, uint32_t bit) { return (self->c->answers & bit) ? 0x5A5A0001u : 0x5A5A0000u; }

void* __fastcall WtListBoxWindow(WtListBoxFace* self, void*)
{
    Mix(0x7B000003u);
    return self->window;
}

uint32_t __fastcall WtListBoxSelection(WtListBoxFace* self, void*)
{
    Mix(0x7B00000Cu);
    return self->c->selection;
}

uint32_t __fastcall WtListBoxSort(WtListBoxFace* self, void*)
{
    Mix(0x7B00000Fu);
    return WtListBoxAnswer(self, kWtLbSort);
}

uint32_t __fastcall WtListBoxDrop(WtListBoxFace* self, void*)
{
    Mix(0x7B000012u);
    return WtListBoxAnswer(self, kWtLbDrop);
}

uint32_t __fastcall WtListBoxCount(WtListBoxFace* self, void*)
{
    Mix(0x7B00001Eu);
    return static_cast<uint32_t>(self->c->count);
}

uint32_t __fastcall WtListBoxScrollbars(WtListBoxFace* self, void*, uint8_t* vertical, uint8_t* horizontal)
{
    Mix(0x7B000024u);
    if (!(self->c->answers & kWtLbScroll))
        return WtListBoxAnswer(self, kWtLbScroll);
    *vertical = self->c->vertical;
    *horizontal = self->c->horizontal;
    return WtListBoxAnswer(self, kWtLbScroll);
}

uint32_t __fastcall WtListBoxFontColour(WtListBoxFace* self, void*, uint32_t which, uint32_t* colour)
{
    Mix(0x7B000028u);
    Mix(which);
    const uint32_t bit = which == 0 ? kWtLbColour0 : which == 1 ? kWtLbColour1 : kWtLbColour2;
    if (which < 3 && (self->c->answers & bit))
        *colour = self->c->colours[which];
    return which < 3 ? WtListBoxAnswer(self, bit) : 0x5A5A0000u;
}

uint32_t __fastcall WtListBoxHighlight(WtListBoxFace* self, void*, uint32_t* colour)
{
    Mix(0x7B00002Bu);
    if (self->c->answers & kWtLbHighlight)
        *colour = self->c->highlight;
    return WtListBoxAnswer(self, kWtLbHighlight);
}

uint32_t __fastcall WtListBoxElement(WtListBoxFace* self, void*, uint32_t index, char* string)
{
    Mix(0x7B000031u);
    Mix(index);
    if (index < 32 && (self->c->elementFails & (1u << index)))
        return 0x5A5A0000u;
    static const char* const kTexts[] = {"first item", "", "third: \"q\"", "x"};
    const char* const text = kTexts[index % 4];
    Virtual<uint32_t>(string, 0xC, text, static_cast<uint32_t>(std::strlen(text)));
    return 0x5A5A0001u;
}

uint32_t __fastcall WtListBoxOutline(WtListBoxFace* self, void*)
{
    Mix(0x7B000034u);
    return WtListBoxAnswer(self, kWtLbOutline);
}

uint32_t __fastcall WtListBoxSunken(WtListBoxFace* self, void*)
{
    Mix(0x7B000036u);
    return WtListBoxAnswer(self, kWtLbSunken);
}

uint32_t __fastcall WtListBoxOutlineColours(WtListBoxFace* self, void*, uint32_t* first, uint32_t* second)
{
    Mix(0x7B000038u);
    if (self->c->answers & kWtLbOutlineColours) {
        *first = self->c->outline[0];
        *second = self->c->outline[1];
    }
    return WtListBoxAnswer(self, kWtLbOutlineColours);
}

uint32_t __fastcall WtListBoxBarGutters(WtListBoxFace* self, void*, int8_t* a, int8_t* b)
{
    Mix(0x7B00003Au);
    if (self->c->answers & kWtLbBarGutters) {
        *a = self->c->barGutters[0];
        *b = self->c->barGutters[1];
    }
    return WtListBoxAnswer(self, kWtLbBarGutters);
}

uint32_t __fastcall WtListBoxGutters(WtListBoxFace* self, void*, int8_t* a, int8_t* b)
{
    Mix(0x7B00003Cu);
    if (self->c->answers & kWtLbGutters) {
        *a = self->c->gutters[0];
        *b = self->c->gutters[1];
    }
    return WtListBoxAnswer(self, kWtLbGutters);
}

void** WtListBoxTable()
{
    static void* table[61] = {};
    table[3] = reinterpret_cast<void*>(&WtListBoxWindow);
    table[12] = reinterpret_cast<void*>(&WtListBoxSelection);
    table[15] = reinterpret_cast<void*>(&WtListBoxSort);
    table[18] = reinterpret_cast<void*>(&WtListBoxDrop);
    table[30] = reinterpret_cast<void*>(&WtListBoxCount);
    table[36] = reinterpret_cast<void*>(&WtListBoxScrollbars);
    table[40] = reinterpret_cast<void*>(&WtListBoxFontColour);
    table[43] = reinterpret_cast<void*>(&WtListBoxHighlight);
    table[49] = reinterpret_cast<void*>(&WtListBoxElement);
    table[52] = reinterpret_cast<void*>(&WtListBoxOutline);
    table[54] = reinterpret_cast<void*>(&WtListBoxSunken);
    table[56] = reinterpret_cast<void*>(&WtListBoxOutlineColours);
    table[58] = reinterpret_cast<void*>(&WtListBoxBarGutters);
    table[60] = reinterpret_cast<void*>(&WtListBoxGutters);
    return table;
}

void DriveWtListBox()
{
    const WtListBoxCase cases[] = {
        // Everything answers; three elements, the second failing.
        {0xFFF, 1, 0, {0x00FF8040u, 0x00102030u, 0xFFFFFFFFu}, 0x00A0B0C0u, {0x00010203u, 0x00F0E0D0u}, {-3, 4},
         {127, -128}, 7, 3, 2},
        // Nothing answers; no elements.
        {0, 1, 1, {1, 2, 3}, 4, {5, 6}, {1, 1}, {2, 2}, 0xFFFFFFFFu, 0, 0},
        // Every other getter; a negative count.
        {0x555, 0, 1, {0x00123456u, 0, 0}, 0, {0, 0x00654321u}, {0, -1}, {-1, 0}, 0x80000000u, -2, 0},
        // The others; five elements, all but the first answering.
        {0xAAA, 0xFF, 0x80, {0, 0x00ABCDEFu, 0}, 0x00FEDCBAu, {0x7F7F7F7Fu, 0x01010101u}, {12, 34}, {-56, 78}, 0,
         5, 0x1E},
    };
    WtValue fontKnown = WtNumber(kFbFontWords[0]);
    WtValue fontOther = WtNumber(kFbFontWords[2]);
    WtValue fontUnknown = WtNumber(0x00012345u);
    WtValue fontText = WtTextValue("Arial Narrow");
    WtValue fontBytes = WtBytes(1, 2, 3);
    WtValue autofitOn = WtListBoxBool(1);
    WtValue autofitOff = WtListBoxBool(0x100);
    const WtProp props0[] = {{0xFAA3BE85u, &fontKnown}, {0xDACF8F9Cu, &autofitOn}};
    const WtProp props1[] = {{0xFAA3BE86u, &fontOther}};
    const WtProp props2[] = {{0xFAA3BE85u, &fontText}, {0xDACF8F9Cu, &autofitOff}};
    const WtProp props3[] = {{0xFAA3BE85u, &fontUnknown}, {0xDACF8F9Cu, &autofitOn}};
    const WtProp props4[] = {{0xFAA3BE85u, &fontBytes}, {0xDACF8F9Cu, &autofitOff}};
    WtWindow windows[] = {
        {WtWindowTable(), props0, 2}, {WtWindowTable(), props1, 1}, {WtWindowTable(), props2, 2},
        {WtWindowTable(), props3, 2}, {WtWindowTable(), props4, 2},
    };
    for (std::size_t i = 0; i < 5; ++i) {
        WtListBoxFace face = {WtListBoxTable(), &cases[i % 4], &windows[i]};
        WtString out("<LEGACY ");
        WtRegs regs = WtRegsOf();
        regs.ebx = out.Arg();
        regs.args[0] = PrArg(&face);
        WtCall(addr::cGZUIScriptWriters_ListBox, regs);
        out.MixText();
    }
    for (WtValue* v : {&fontKnown, &fontOther, &fontUnknown, &fontText, &fontBytes, &autofitOn, &autofitOff})
        Mix(v->releases);
    WtStage("listbox");
}

// ------------------------------------------------------------------------------------
// GZWinLineInput's writer

// The interface: each case says which getters answer and what they give.
// The gutters, the style and the alpha are read without an answer, so
// those three always write; a getter that answers always writes too.
struct WtLineInputCase {
    uint32_t answers; // bit per getter (below)
    uint32_t fontColours[3];
    uint32_t colours[4];
    uint32_t fill;
    int8_t gutters[4];
    uint32_t style;
    float alpha;
    uint32_t caret;
    uint32_t limit;
    uint32_t start, length;
};

enum : uint32_t {
    kWtLiFont0 = 1,
    kWtLiFont1 = 2,
    kWtLiFont2 = 4,
    kWtLiColour0 = 8,
    kWtLiColour1 = 0x10,
    kWtLiColour2 = 0x20,
    kWtLiColour3 = 0x40,
    kWtLiFill = 0x80,
    kWtLiFilled = 0x100,
    kWtLiEditable = 0x200,
    kWtLiCaret = 0x400,
    kWtLiLimit = 0x800,
    kWtLiSelection = 0x1000,
};

struct WtLineInputFace {
    void** vtable;
    const WtLineInputCase* c;
    WtWindow* window;
};

uint32_t WtLineInputAnswer(const WtLineInputFace* self, uint32_t bit)
{
    return (self->c->answers & bit) ? 0x6B6B0001u : 0x6B6B0000u;
}

void* __fastcall WtLineInputWindow(WtLineInputFace* self, void*)
{
    Mix(0x7C000003u);
    return self->window;
}

uint32_t __fastcall WtLineInputFontColour(WtLineInputFace* self, void*, uint32_t which, uint32_t* colour)
{
    Mix(0x7C000007u);
    Mix(which);
    const uint32_t bit = kWtLiFont0 << (which % 3);
    if (self->c->answers & bit)
        *colour = self->c->fontColours[which % 3];
    return WtLineInputAnswer(self, bit);
}

uint32_t __fastcall WtLineInputFilled(WtLineInputFace* self, void*)
{
    Mix(0x7C00000Au);
    return WtLineInputAnswer(self, kWtLiFilled);
}

uint32_t __fastcall WtLineInputStyle(WtLineInputFace* self, void*, uint32_t* style)
{
    Mix(0x7C00000Cu);
    *style = self->c->style;
    return 0x6B6B00C0u;
}

uint32_t __fastcall WtLineInputColour(WtLineInputFace* self, void*, uint32_t which, uint32_t* colour)
{
    Mix(0x7C00000Fu);
    Mix(which);
    const uint32_t bit = kWtLiColour0 << (which % 4);
    if (self->c->answers & bit)
        *colour = self->c->colours[which % 4];
    return WtLineInputAnswer(self, bit);
}

uint32_t __fastcall WtLineInputFill(WtLineInputFace* self, void*, uint32_t* colour)
{
    Mix(0x7C000012u);
    if (self->c->answers & kWtLiFill)
        *colour = self->c->fill;
    return WtLineInputAnswer(self, kWtLiFill);
}

uint32_t __fastcall WtLineInputAlpha(WtLineInputFace* self, void*, float* alpha)
{
    Mix(0x7C000014u);
    *alpha = self->c->alpha;
    return 0x6B6B0014u;
}

uint32_t __fastcall WtLineInputCaret(WtLineInputFace* self, void*, uint32_t* word)
{
    Mix(0x7C00001Au);
    if (self->c->answers & kWtLiCaret)
        *word = self->c->caret;
    return WtLineInputAnswer(self, kWtLiCaret);
}

uint32_t __fastcall WtLineInputEditable(WtLineInputFace* self, void*)
{
    Mix(0x7C00001Bu);
    return WtLineInputAnswer(self, kWtLiEditable);
}

uint32_t __fastcall WtLineInputLimit(WtLineInputFace* self, void*, uint32_t* word)
{
    Mix(0x7C00001Eu);
    if (self->c->answers & kWtLiLimit)
        *word = self->c->limit;
    return WtLineInputAnswer(self, kWtLiLimit);
}

uint32_t __fastcall WtLineInputSelection(WtLineInputFace* self, void*, uint32_t* start, uint32_t* length)
{
    Mix(0x7C000020u);
    if (self->c->answers & kWtLiSelection) {
        *start = self->c->start;
        *length = self->c->length;
    }
    return WtLineInputAnswer(self, kWtLiSelection);
}

uint32_t __fastcall WtLineInputGutters(WtLineInputFace* self, void*, int8_t* a, int8_t* b, int8_t* c, int8_t* d)
{
    Mix(0x7C000022u);
    *a = self->c->gutters[0];
    *b = self->c->gutters[1];
    *c = self->c->gutters[2];
    *d = self->c->gutters[3];
    return 0x6B6B0022u;
}

void** WtLineInputTable()
{
    static void* table[35] = {};
    table[3] = reinterpret_cast<void*>(&WtLineInputWindow);
    table[7] = reinterpret_cast<void*>(&WtLineInputFontColour);
    table[10] = reinterpret_cast<void*>(&WtLineInputFilled);
    table[12] = reinterpret_cast<void*>(&WtLineInputStyle);
    table[15] = reinterpret_cast<void*>(&WtLineInputColour);
    table[18] = reinterpret_cast<void*>(&WtLineInputFill);
    table[20] = reinterpret_cast<void*>(&WtLineInputAlpha);
    table[26] = reinterpret_cast<void*>(&WtLineInputCaret);
    table[27] = reinterpret_cast<void*>(&WtLineInputEditable);
    table[30] = reinterpret_cast<void*>(&WtLineInputLimit);
    table[32] = reinterpret_cast<void*>(&WtLineInputSelection);
    table[34] = reinterpret_cast<void*>(&WtLineInputGutters);
    return table;
}

void DriveWtLineInput()
{
    // The alphas: 0, 1, a half, a value that rounds down, negative, large.
    const WtLineInputCase cases[] = {
        {0x1FFF, {0x00FF0000u, 0x0000FF00u, 0x000000FFu}, {1, 2, 3, 0xFFFFFFFFu}, 0x00808080u, {-1, 2, -128, 127}, 1,
         1.0f, 5, 255, 3, 4},
        {0, {0, 0, 0}, {0, 0, 0, 0}, 0, {0, 0, 0, 0}, 0, 0.0f, 0, 0, 0, 0},
        {0x0AAA, {7, 0x00ABCDEFu, 9}, {0x00102030u, 0, 0x00405060u, 0}, 0, {10, -20, 30, -40}, 2, 0.5f, 0, 80, 0, 0},
        {0x1555, {0x00654321u, 0, 0x00FFFFFFu}, {0, 0x00708090u, 0, 0x00A0B0C0u}, 0x00010101u, {0, 0, 0, 1}, 3,
         0.3333333f, 0xFFFFFFFFu, 0, 0xFFFFFFF0u, 0x20},
        {0x1C00, {0, 0, 0}, {0, 0, 0, 0}, 0, {5, 5, 5, 5}, 4, -0.75f, 0x7FFFFFFFu, 12, 0x7FFFFFFFu, 1},
        {0x0300, {0, 0, 0}, {0, 0, 0, 0}, 0, {0, 1, 0, 1}, 0xFFFFFFFFu, 1000.25f, 0, 0, 0, 0},
    };
    WtValue fontKnown = WtNumber(kFbFontWords[1]);
    WtValue fontUnknown = WtNumber(0xFFFFFFF6u);
    WtValue fontText = WtTextValue("Courier");
    WtValue fontOther = WtPairValue(1, 2);
    const WtProp props0[] = {{0xFAA3BE85u, &fontKnown}};
    const WtProp props1[] = {{0x12345678u, &fontKnown}};
    const WtProp props2[] = {{0xFAA3BE85u, &fontText}};
    const WtProp props3[] = {{0xFAA3BE85u, &fontUnknown}};
    const WtProp props4[] = {{0xFAA3BE85u, &fontOther}};
    WtWindow windows[] = {
        {WtWindowTable(), props0, 1}, {WtWindowTable(), props1, 1}, {WtWindowTable(), props2, 1},
        {WtWindowTable(), props3, 1}, {WtWindowTable(), props4, 1}, {WtWindowTable(), props0, 0},
    };
    for (std::size_t i = 0; i < 6; ++i) {
        WtLineInputFace face = {WtLineInputTable(), &cases[i], &windows[i]};
        WtString out;
        WtRegs regs = WtRegsOf();
        regs.ebx = out.Arg();
        regs.args[0] = PrArg(&face);
        WtCall(addr::cGZUIScriptWriters_LineInput, regs);
        out.MixText();
    }
    for (WtValue* v : {&fontKnown, &fontUnknown, &fontText, &fontOther})
        Mix(v->releases);
    WtStage("lineinput");
}

// ------------------------------------------------------------------------------------
// GZWinCombo's writer: an interface of our own (slots 3, 13, 17, 20, 23,
// 30, 31, 34, 36, 38 and 40; each hashes its number and what it gets, and
// answers by the case), its window one of ours with a font property
// (0xFAA3BE85) and a key pair (0xDAA3B860); colours made by the game's
// colour service. A getter that answers yes always writes.

struct WtComboFace {
    void** vtable;
    uint32_t c;
    WtWindow* window;
    uint32_t colours[3];
};

void* __fastcall WtComboWindow(WtComboFace* self, void*)
{
    Mix(0x7C000003u);
    return self->window;
}

uint32_t __fastcall WtComboFontColour(WtComboFace* self, void*, uint32_t index, uint32_t* colour)
{
    Mix(0x7C00000Du);
    Mix(index);
    if ((self->c + index) % 3 == 2)
        return 0x0D0D0000u;
    *colour = self->colours[index % 3];
    return 0x0D0D0001u;
}

uint32_t WtComboColourSlot(WtComboFace* self, uint32_t slot, uint32_t* colour)
{
    Mix(0x7C000000u | slot);
    if ((self->c + slot) % 2)
        return 0x11110000u;
    *colour = self->colours[(self->c + slot) % 3];
    return 0x11110001u;
}

uint32_t __fastcall WtComboHighlight(WtComboFace* self, void*, uint32_t* colour) { return WtComboColourSlot(self, 17, colour); }
uint32_t __fastcall WtComboOutline(WtComboFace* self, void*, uint32_t* colour) { return WtComboColourSlot(self, 23, colour); }
uint32_t __fastcall WtComboArrowColour(WtComboFace* self, void*, uint32_t* colour) { return WtComboColourSlot(self, 36, colour); }

uint32_t __fastcall WtComboEditable(WtComboFace* self, void*)
{
    Mix(0x7C000014u);
    return 0x14140000u | (self->c & 1);
}

uint32_t __fastcall WtComboSelection(WtComboFace* self, void*)
{
    Mix(0x7C00001Eu);
    return self->c * 7u - 3u;
}

uint32_t __fastcall WtComboItem(WtComboFace* self, void*, uint32_t index, char* string)
{
    Mix(0x7C00001Fu);
    Mix(index);
    if (index >= self->c % 4)
        return 0x1F1F0000u;
    // The second item an empty text.
    char text[32] = {};
    if (index != 1)
        std::snprintf(text, sizeof text, "item %u of %u", index, self->c);
    Virtual<uint32_t>(string, 0xC, static_cast<const char*>(text), static_cast<uint32_t>(std::strlen(text)));
    return 0x1F1F0001u;
}

uint32_t __fastcall WtComboArrowRect(WtComboFace* self, void*, uint32_t* word, int32_t* rect)
{
    Mix(0x7C000022u);
    Mix(*word);
    if (!(self->c % 2))
        return 0x22220000u;
    rect[0] = static_cast<int32_t>(self->c);
    rect[1] = -static_cast<int32_t>(self->c);
    rect[2] = 100;
    rect[3] = 0x7FFFFFFF;
    return 0x22220001u;
}

uint32_t __fastcall WtComboGutter(WtComboFace* self, void*, int8_t* gutter)
{
    Mix(0x7C000026u);
    if (self->c % 3 == 1)
        return 0x26260000u;
    *gutter = static_cast<int8_t>(self->c * 50u - 100u);
    return 0x26260001u;
}

uint32_t __fastcall WtComboGutters(WtComboFace* self, void*, int8_t* a, int8_t* b)
{
    Mix(0x7C000028u);
    if (self->c % 2)
        return 0x28280000u;
    *a = static_cast<int8_t>(self->c - 5u);
    *b = 120;
    return 0x28280001u;
}

void** WtComboTable()
{
    static void* table[41] = {};
    table[3] = reinterpret_cast<void*>(&WtComboWindow);
    table[13] = reinterpret_cast<void*>(&WtComboFontColour);
    table[17] = reinterpret_cast<void*>(&WtComboHighlight);
    table[20] = reinterpret_cast<void*>(&WtComboEditable);
    table[23] = reinterpret_cast<void*>(&WtComboOutline);
    table[30] = reinterpret_cast<void*>(&WtComboSelection);
    table[31] = reinterpret_cast<void*>(&WtComboItem);
    table[34] = reinterpret_cast<void*>(&WtComboArrowRect);
    table[36] = reinterpret_cast<void*>(&WtComboArrowColour);
    table[38] = reinterpret_cast<void*>(&WtComboGutter);
    table[40] = reinterpret_cast<void*>(&WtComboGutters);
    return table;
}

void DriveWtCombo()
{
    void* const fonts = reinterpret_cast<void*(__cdecl*)()>(addr::SC4_Singleton913C72)();
    void* const colours = reinterpret_cast<void*(__cdecl*)()>(addr::SC4_Singleton913C1A)();
    Mix(fonts ? 1u : 0u);
    Mix(colours ? 1u : 0u);
    if (!fonts || !colours) {
        WtStage("combo");
        return;
    }
    // The faces of the known font words made (as part u15's test does).
    for (uint32_t word : kFbFontWords)
        Virtual<void*>(fonts, 0x14, word);
    const uint32_t made[3] = {Virtual<uint32_t>(colours, 0x88, 0x12u, 0x34u, 0x56u),
                              Virtual<uint32_t>(colours, 0x88, 0xFFu, 0u, 0x80u),
                              Virtual<uint32_t>(colours, 0x88, 0u, 0u, 0u)};
    for (uint32_t c = 0; c < 6; ++c) {
        // The font: none, a known font, an unknown number, a text, another
        // kind; the key: a pair, a number (no pair kind), none.
        WtValue font0 = WtNumber(kFbFontWords[c % 3]);
        WtValue unknown = WtNumber(12);
        WtValue text = WtTextValue("Combo Face");
        WtValue other = WtBytes(1, 2, 3);
        WtValue pair = WtPairValue(0x11112222u + c, 0x33334444u);
        WtValue number = WtNumber(9);
        WtValue* const fontValues[6] = {nullptr, &font0, &unknown, &text, &other, &font0};
        WtValue* const keyValues[3] = {&pair, &number, nullptr};
        WtProp props[2];
        std::size_t count = 0;
        if (fontValues[c])
            props[count++] = {0xFAA3BE85u, fontValues[c]};
        if (keyValues[c % 3])
            props[count++] = {0xDAA3B860u, keyValues[c % 3]};
        WtWindow window = {WtWindowTable(), props, count};
        WtComboFace face = {WtComboTable(), c, &window, {made[0], made[1], made[2]}};
        WtString out("<");
        WtRegs regs = WtRegsOf();
        regs.ebx = out.Arg();
        regs.args[0] = PrArg(&face);
        WtCall(addr::cGZUIScriptWriters_Combo, regs);
        out.MixText();
        for (WtValue* v : {&font0, &unknown, &text, &other, &pair, &number})
            Mix(v->releases);
    }
    WtStage("combo");
}

// ------------------------------------------------------------------------------------
// GZWinBtn's writer: an interface of our own (slots 3, 4, 11, 21, 25, 28,
// 31, 36, 39 and 41; each hashes its number and what it gets, and answers
// by the case), its window one of ours with the image and tip keys
// (0x2B343BEC, 0x2B340FFE, 12 bytes) and the two sounds (0x0A5BE39D,
// 0x4A5BE2F8, pairs); colours made by the game's colour service. A getter
// that answers yes always writes, and slot 31 (whose answer the writer
// does not read) always writes.

struct WtBtnFace {
    void** vtable;
    uint32_t c;
    WtWindow* window;
    uint32_t colours[3];
};

void* __fastcall WtBtnWindow(WtBtnFace* self, void*)
{
    Mix(0x7B000003u);
    return self->window;
}

uint32_t __fastcall WtBtnFlag(WtBtnFace* self, void*, uint32_t mask)
{
    Mix(0x7B000004u);
    Mix(mask);
    return 0x04040000u | (((self->c * 0x135u + 0xA3u) & mask) ? 1u : 0u);
}

uint32_t __fastcall WtBtnStyle(WtBtnFace* self, void*)
{
    Mix(0x7B00000Bu);
    return self->c % 6;
}

uint32_t __fastcall WtBtnFont(WtBtnFace* self, void*, uint32_t* font)
{
    Mix(0x7B000015u);
    switch (self->c % 4) {
    case 0:
        return 0x15150000u;
    case 1:
        *font = kFbFontWords[self->c % 3];
        break;
    case 2:
        *font = 0x68963C4Cu;
        break;
    default:
        *font = 0x1234ABCDu;
        break;
    }
    return 0x15150001u;
}

uint32_t __fastcall WtBtnColour(WtBtnFace* self, void*, uint32_t index, uint32_t* colour)
{
    Mix(0x7B000019u);
    Mix(index);
    if ((self->c + index) % 4 == 3)
        return 0x19190000u;
    *colour = self->colours[(index + self->c) % 3];
    return 0x19190001u;
}

uint32_t __fastcall WtBtnAlign(WtBtnFace* self, void*, uint32_t* align)
{
    Mix(0x7B00001Cu);
    if (self->c % 5 == 4)
        return 0x1C1C0000u;
    *align = self->c % 5 == 3 ? 7u : self->c % 5;
    return 0x1C1C0001u;
}

uint32_t __fastcall WtBtnGutters(WtBtnFace* self, void*, int32_t* a, int32_t* b, int32_t* c, int32_t* d)
{
    Mix(0x7B00001Fu);
    *a = static_cast<int32_t>(self->c);
    *b = -1;
    *c = 1000;
    *d = -3 * static_cast<int32_t>(self->c);
    // The answer: the writer does not read it.
    return 0x1F1F0000u | (self->c & 1);
}

uint32_t __fastcall WtBtnTipText(WtBtnFace* self, void*, char* string)
{
    Mix(0x7B000024u);
    if (!(self->c % 2))
        return 0x24240000u;
    char text[32];
    std::snprintf(text, sizeof text, "Tip text %u", self->c);
    Virtual<uint32_t>(string, 0xC, static_cast<const char*>(text), static_cast<uint32_t>(std::strlen(text)));
    return 0x24240001u;
}

uint32_t __fastcall WtBtnTipFlag(WtBtnFace* self, void*, uint32_t* flag)
{
    Mix(0x7B000027u);
    if (self->c % 3 == 0)
        return 0x27270000u;
    *flag = 0x80000000u | self->c;
    return 0x27270001u;
}

uint32_t __fastcall WtBtnTipOffsets(WtBtnFace* self, void*, int32_t* a, int32_t* b)
{
    Mix(0x7B000029u);
    if (self->c % 3 == 1)
        return 0x29290000u;
    *a = static_cast<int32_t>(self->c) - 2;
    *b = 7;
    return 0x29290001u;
}

void** WtBtnTable()
{
    static void* table[42] = {};
    table[3] = reinterpret_cast<void*>(&WtBtnWindow);
    table[4] = reinterpret_cast<void*>(&WtBtnFlag);
    table[11] = reinterpret_cast<void*>(&WtBtnStyle);
    table[21] = reinterpret_cast<void*>(&WtBtnFont);
    table[25] = reinterpret_cast<void*>(&WtBtnColour);
    table[28] = reinterpret_cast<void*>(&WtBtnAlign);
    table[31] = reinterpret_cast<void*>(&WtBtnGutters);
    table[36] = reinterpret_cast<void*>(&WtBtnTipText);
    table[39] = reinterpret_cast<void*>(&WtBtnTipFlag);
    table[41] = reinterpret_cast<void*>(&WtBtnTipOffsets);
    return table;
}

void DriveWtBtn()
{
    void* const fonts = reinterpret_cast<void*(__cdecl*)()>(addr::SC4_Singleton913C72)();
    void* const colours = reinterpret_cast<void*(__cdecl*)()>(addr::SC4_Singleton913C1A)();
    Mix(fonts ? 1u : 0u);
    Mix(colours ? 1u : 0u);
    if (!fonts || !colours) {
        WtStage("btn");
        return;
    }
    for (uint32_t word : kFbFontWords)
        Virtual<void*>(fonts, 0x14, word);
    const uint32_t made[3] = {Virtual<uint32_t>(colours, 0x88, 0xC0u, 0x40u, 0x20u),
                              Virtual<uint32_t>(colours, 0x88, 0x01u, 0xFEu, 0x7Fu),
                              Virtual<uint32_t>(colours, 0x88, 0xFFu, 0xFFu, 0xFFu)};
    for (uint32_t c = 0; c < 8; ++c) {
        // The image: a key, the wrong type, none. The tip key: both words
        // 0, the instance only, the group only, none (the image's left in
        // the place). The click sound: a pair, a pair of zeros, a number,
        // none; the up sound: a pair with one zero, none.
        WtValue image = WtBytes(1, 0x22220000u + c, 0x33330000u);
        WtValue imageWrong = WtNumber(4);
        WtValue tips[3] = {WtBytes(9, 0, 0), WtBytes(9, 0, 5), WtBytes(0, 7, 0)};
        WtValue click = WtPairValue(0xAAAA0000u + c, 0xBBBBu);
        WtValue clickZero = WtPairValue(0, 0);
        WtValue clickNumber = WtNumber(3);
        WtValue up = WtPairValue(0, 0x1234u);
        WtProp props[4];
        std::size_t count = 0;
        if (c % 3 == 0)
            props[count++] = {0x2B343BECu, &image};
        else if (c % 3 == 1)
            props[count++] = {0x2B343BECu, &imageWrong};
        if (c % 4 != 3)
            props[count++] = {0x2B340FFEu, &tips[c % 4]};
        WtValue* const clicks[4] = {&click, &clickZero, &clickNumber, nullptr};
        if (clicks[c % 4])
            props[count++] = {0x0A5BE39Du, clicks[c % 4]};
        if (c % 2 == 0)
            props[count++] = {0x4A5BE2F8u, &up};
        WtWindow window = {WtWindowTable(), props, count};
        WtBtnFace face = {WtBtnTable(), c, &window, {made[0], made[1], made[2]}};
        WtString out(">");
        WtRegs regs = WtRegsOf();
        regs.args[0] = PrArg(&face);
        regs.args[1] = out.Arg();
        WtCall(addr::cGZUIScriptWriters_Btn, regs);
        out.MixText();
        for (WtValue* v : {&image, &imageWrong, &tips[0], &tips[1], &tips[2], &click, &clickZero, &clickNumber, &up})
            Mix(v->releases);
    }
    WtStage("btn");
}

// ------------------------------------------------------------------------------------
// GZWinBMP's writer (cdecl: the interface, the out string)

struct WtBmpFace {
    void** vtable;
    WtWindow* window;
    uint32_t flags; // the mask bits slot 10 answers yes for
    uint8_t alphaOn;
    float alpha;
    int32_t rect[4];
};

void* __fastcall WtBmpWindow(WtBmpFace* self, void*)
{
    Mix(0x7F000003u);
    return self->window;
}

uint32_t __fastcall WtBmpAlpha(WtBmpFace* self, void*, uint8_t* on, float* alpha)
{
    Mix(0x7F000007u);
    *on = self->alphaOn;
    *alpha = self->alpha;
    return 0x07070000u;
}

uint32_t __fastcall WtBmpRect(WtBmpFace* self, void*, int32_t* rect)
{
    Mix(0x7F000009u);
    for (uint32_t i = 0; i < 4; ++i)
        rect[i] = self->rect[i];
    return 0x09090000u;
}

uint32_t __fastcall WtBmpFlag(WtBmpFace* self, void*, uint32_t mask)
{
    Mix(0x7F00000Au);
    Mix(mask);
    return 0x0A0A0A00u | ((self->flags & mask) ? 1u : 0u);
}

void** WtBmpTable()
{
    static void* table[11] = {};
    table[3] = reinterpret_cast<void*>(&WtBmpWindow);
    table[7] = reinterpret_cast<void*>(&WtBmpAlpha);
    table[9] = reinterpret_cast<void*>(&WtBmpRect);
    table[10] = reinterpret_cast<void*>(&WtBmpFlag);
    return table;
}

void DriveWtBMP()
{
    WtValue image = WtBytes(0xDEADBEEFu, 0x856DDBACu, 0x12345678u);
    WtValue notImage = WtNumber(0x55);
    const WtProp withImage[] = {{0xAB428C5Du, &image}};
    const WtProp wrongType[] = {{0xAB428C5Du, &notImage}};
    WtWindow windows[] = {
        {WtWindowTable(), withImage, 1},
        {WtWindowTable(), wrongType, 1},
        {WtWindowTable(), nullptr, 0},
    };
    // The alpha's float through the x87 and _ftol: 0, a half, 1, just
    // under 1, above 1 (the low byte), negative.
    const float alphas[] = {0.0f, 0.5f, 1.0f, 0.999f, 1.5f, -0.2f, 0.33333334f};
    const uint32_t flags[] = {0x1B, 0x00, 0x10, 0x03, 0x08, 0xFFFFFFEFu, 0x12};
    for (uint32_t i = 0; i < 7; ++i) {
        WtBmpFace face = {WtBmpTable(), &windows[i % 3], flags[i], static_cast<uint8_t>(i % 2 ? 0 : 1 + i),
                          alphas[i], {static_cast<int32_t>(i), -5, 640 + static_cast<int32_t>(i), -1}};
        WtString out("<");
        WtRegs regs = WtRegsOf();
        regs.args[0] = PrArg(&face);
        regs.args[1] = out.Arg();
        WtCall(addr::cGZUIScriptWriters_BMP, regs);
        out.MixText();
    }
    // Every alpha with the alpha on.
    for (float alpha : alphas) {
        WtBmpFace face = {WtBmpTable(), &windows[2], 0, 1, alpha, {0, 0, 0, 0}};
        WtString out;
        WtRegs regs = WtRegsOf();
        regs.args[0] = PrArg(&face);
        regs.args[1] = out.Arg();
        WtCall(addr::cGZUIScriptWriters_BMP, regs);
        out.MixText();
    }
    Mix(image.releases);
    Mix(notImage.releases);
    WtStage("bmp");
}

// ------------------------------------------------------------------------------------
// GZWinSlider's writer (EDI the interface, EBX the out string)

struct WtSliderFace {
    void** vtable;
    WtWindow* window;
    uint32_t minimum, maximum, direction, value;
};

void* __fastcall WtSliderWindow(WtSliderFace* self, void*)
{
    Mix(0x7F010003u);
    return self->window;
}

uint32_t __fastcall WtSliderValue(WtSliderFace* self, void*)
{
    Mix(0x7F010004u);
    return self->value;
}

uint32_t __fastcall WtSliderMinimum(WtSliderFace* self, void*)
{
    Mix(0x7F010007u);
    return self->minimum;
}

uint32_t __fastcall WtSliderMaximum(WtSliderFace* self, void*)
{
    Mix(0x7F010009u);
    return self->maximum;
}

uint32_t __fastcall WtSliderDirection(WtSliderFace* self, void*, uint32_t* direction)
{
    Mix(0x7F01000Bu);
    *direction = self->direction;
    return 0x0B0B0000u;
}

void** WtSliderTable()
{
    static void* table[12] = {};
    table[3] = reinterpret_cast<void*>(&WtSliderWindow);
    table[4] = reinterpret_cast<void*>(&WtSliderValue);
    table[7] = reinterpret_cast<void*>(&WtSliderMinimum);
    table[9] = reinterpret_cast<void*>(&WtSliderMaximum);
    table[11] = reinterpret_cast<void*>(&WtSliderDirection);
    return table;
}

void DriveWtSlider()
{
    WtValue image = WtBytes(0x0C0C0C0Cu, 0x856DDBACu, 0xABCDEF01u);
    WtValue notImage = WtTextValue("x");
    const WtProp withImage[] = {{0x6B4BCBD4u, &image}};
    const WtProp wrongType[] = {{0x6B4BCBD4u, &notImage}};
    WtWindow windows[] = {
        {WtWindowTable(), withImage, 1},
        {WtWindowTable(), wrongType, 1},
        {WtWindowTable(), nullptr, 0},
    };
    const WtSliderFace faces[] = {
        {WtSliderTable(), &windows[0], 0, 100, 2, 50},
        {WtSliderTable(), &windows[1], 0xFFFFFFF6u, 10, 1, 0xFFFFFFFFu},
        {WtSliderTable(), &windows[2], 5, 5, 0, 5},
        {WtSliderTable(), &windows[0], 0x80000000u, 0x7FFFFFFFu, 3, 0},
        {WtSliderTable(), &windows[2], 1, 2, 0x102, 3},
    };
    for (const WtSliderFace& face : faces) {
        WtString out("<");
        WtRegs regs = WtRegsOf();
        regs.edi = PrArg(&face);
        regs.ebx = out.Arg();
        WtCall(addr::cGZUIScriptWriters_Slider, regs);
        out.MixText();
    }
    Mix(image.releases);
    Mix(notImage.releases);
    WtStage("slider");
}

// ------------------------------------------------------------------------------------
// GZWinScrollbar's writer (EDI the interface, EBX the out string)

struct WtScrollbarFace {
    void** vtable;
    WtWindow* window;
    uint32_t rangeOk, minimum, maximum, direction, page, line, lines, pages, value;
};

void* __fastcall WtScrollbarWindow(WtScrollbarFace* self, void*)
{
    Mix(0x7F020003u);
    return self->window;
}

uint32_t __fastcall WtScrollbarValue(WtScrollbarFace* self, void*)
{
    Mix(0x7F020006u);
    return self->value;
}

// The range: written only when it answers yes (the game reads it only
// then).
uint32_t __fastcall WtScrollbarRange(WtScrollbarFace* self, void*, uint32_t* minimum, uint32_t* maximum)
{
    Mix(0x7F020008u);
    if (!self->rangeOk)
        return 0x08080800u;
    *minimum = self->minimum;
    *maximum = self->maximum;
    return 0x08080801u;
}

uint32_t __fastcall WtScrollbarDirection(WtScrollbarFace* self, void*)
{
    Mix(0x7F02000Au);
    return self->direction;
}

uint32_t __fastcall WtScrollbarPage(WtScrollbarFace* self, void*)
{
    Mix(0x7F02000Cu);
    return self->page;
}

uint32_t __fastcall WtScrollbarLine(WtScrollbarFace* self, void*)
{
    Mix(0x7F02000Eu);
    return self->line;
}

uint32_t __fastcall WtScrollbarCounts(WtScrollbarFace* self, void*, uint32_t* lines, uint32_t* pages)
{
    Mix(0x7F020010u);
    *lines = self->lines;
    *pages = self->pages;
    return 0x10100000u;
}

void** WtScrollbarTable()
{
    static void* table[17] = {};
    table[3] = reinterpret_cast<void*>(&WtScrollbarWindow);
    table[6] = reinterpret_cast<void*>(&WtScrollbarValue);
    table[8] = reinterpret_cast<void*>(&WtScrollbarRange);
    table[10] = reinterpret_cast<void*>(&WtScrollbarDirection);
    table[12] = reinterpret_cast<void*>(&WtScrollbarPage);
    table[14] = reinterpret_cast<void*>(&WtScrollbarLine);
    table[16] = reinterpret_cast<void*>(&WtScrollbarCounts);
    return table;
}

void DriveWtScrollbar()
{
    WtValue image = WtBytes(0x0C0C0C0Cu, 0x11111111u, 0x22222222u);
    WtValue notImage = WtPairValue(3, 4);
    const WtProp withImage[] = {{0x6B4BCBD4u, &image}};
    const WtProp wrongType[] = {{0x6B4BCBD4u, &notImage}};
    WtWindow windows[] = {
        {WtWindowTable(), withImage, 1},
        {WtWindowTable(), wrongType, 1},
        {WtWindowTable(), nullptr, 0},
    };
    const WtScrollbarFace faces[] = {
        {WtScrollbarTable(), &windows[0], 1, 0, 1000, 2, 100, 10, 3, 4, 500},
        {WtScrollbarTable(), &windows[1], 0, 7, 8, 1, 0xFFFFFFFFu, 0, 0, 0, 0xFFFFFF9Cu},
        {WtScrollbarTable(), &windows[2], 0x100, 0xFFFFFFF0u, 16, 0, 1, 1, 0xFFFFFFFFu, 2, 1},
        {WtScrollbarTable(), &windows[0], 0xFF, 5, 5, 0x10002, 0, 0x7FFFFFFFu, 9, 0x80000000u, 5},
        {WtScrollbarTable(), &windows[2], 1, 0x80000000u, 0x7FFFFFFFu, 3, 2, 3, 4, 5, 6},
    };
    for (const WtScrollbarFace& face : faces) {
        WtString out("<");
        WtRegs regs = WtRegsOf();
        regs.edi = PrArg(&face);
        regs.ebx = out.Arg();
        WtCall(addr::cGZUIScriptWriters_Scrollbar, regs);
        out.MixText();
    }
    Mix(image.releases);
    Mix(notImage.releases);
    WtStage("scrollbar");
}

// ------------------------------------------------------------------------------------
// GZWinOptGrp's writer (cdecl: the interface, the out string) and its
// options' callback (cdecl: the window, the id, the IGZWinBtn, the out
// string)

// A script value with slot 63 too (a bool's place: the first word's low
// byte), for the autofit property.
uint8_t* __fastcall WtOptGrpValueBool(WtValue* self, void*)
{
    Mix(0x7E00003Fu);
    return reinterpret_cast<uint8_t*>(&self->words[0]);
}

void** WtOptGrpValueTable()
{
    static void* table[104] = {};
    void** const base = WtValueTable();
    for (std::size_t i = 0; i < 104; ++i)
        table[i] = base[i];
    table[63] = reinterpret_cast<void*>(&WtOptGrpValueBool);
    return table;
}

// A window of our own with slot 113 (WtWindowProperty, so the first three
// fields are WtWindow's), slot 32 (each button handed to the callback by
// its address with the window, its id and the out string; the callback's
// AL hashed) and slot 47 (its area).
struct WtOptGrpButton;

struct WtOptGrpWindow {
    void** vtable;
    const WtProp* props;
    std::size_t count;
    WtOptGrpButton* const* buttons;
    std::size_t buttonCount;
    int32_t area[4];
};

uint32_t __fastcall WtOptGrpChildren(WtOptGrpWindow* self, void*, uint32_t iid, std::uintptr_t callback, void* out);

uint32_t __fastcall WtOptGrpArea(WtOptGrpWindow* self, void*, int32_t* area)
{
    Mix(0x7E00002Fu);
    for (uint32_t i = 0; i < 4; ++i)
        area[i] = self->area[i];
    return 0x2F2F0000u;
}

void** WtOptGrpWindowTable()
{
    static void* table[114] = {};
    WtWindowSlots(table);
    table[32] = reinterpret_cast<void*>(&WtOptGrpChildren);
    table[47] = reinterpret_cast<void*>(&WtOptGrpArea);
    return table;
}

// An option (IGZWinBtn): slot 3 its window, slot 16 the image's
// rectangle, slot 50 its caption (a game string, which is a cIGZString).
struct WtOptGrpButton {
    void** vtable;
    WtOptGrpWindow* window;
    uint32_t id;
    int32_t rect[4];
    WtString* caption;
};

void* __fastcall WtOptGrpButtonWindow(WtOptGrpButton* self, void*)
{
    Mix(0x7F030103u);
    return self->window;
}

uint32_t __fastcall WtOptGrpButtonRect(WtOptGrpButton* self, void*, int32_t* rect)
{
    Mix(0x7F030110u);
    for (uint32_t i = 0; i < 4; ++i)
        rect[i] = self->rect[i];
    return 0x10100000u;
}

void* __fastcall WtOptGrpButtonCaption(WtOptGrpButton* self, void*)
{
    Mix(0x7F030132u);
    return self->caption->bytes;
}

void** WtOptGrpButtonTable()
{
    static void* table[51] = {};
    table[3] = reinterpret_cast<void*>(&WtOptGrpButtonWindow);
    table[16] = reinterpret_cast<void*>(&WtOptGrpButtonRect);
    table[50] = reinterpret_cast<void*>(&WtOptGrpButtonCaption);
    return table;
}

uint32_t __fastcall WtOptGrpChildren(WtOptGrpWindow* self, void*, uint32_t iid, std::uintptr_t callback, void* out)
{
    Mix(0x7E000020u);
    Mix(iid);
    Mix(callback == addr::cGZUIScriptWriters_OptGrpOption ? 1u : 0u);
    using Callback = uint32_t(__cdecl*)(void*, uint32_t, void*, void*);
    for (std::size_t i = 0; i < self->buttonCount; ++i) {
        WtOptGrpButton* const button = self->buttons[i];
        MixByte(reinterpret_cast<Callback>(callback)(self, button ? button->id : 0xFFFFu, button, out));
    }
    return 0x20200001u;
}

// IGZWinOptGrp of our own.
struct WtOptGrpFace {
    void** vtable;
    WtOptGrpWindow* window;
    uint32_t flags;          // slot 4's yes bits
    int32_t gutters[2];      // slot 7
    uint32_t colourOk;       // slot 11's yes indices (bits)
    uint32_t colours[5];     // slot 11
    uint32_t outlineOk;      // slot 22
    uint32_t outline[4];     // slot 22
    uint32_t selection;      // slot 13
    uint32_t autofillOk;     // slot 29
    uint32_t manual, size;   // slot 29
};

void* __fastcall WtOptGrpFaceWindow(WtOptGrpFace* self, void*)
{
    Mix(0x7F030003u);
    return self->window;
}

uint32_t __fastcall WtOptGrpFaceFlag(WtOptGrpFace* self, void*, uint32_t mask)
{
    Mix(0x7F030004u);
    Mix(mask);
    return 0x04040400u | ((self->flags & mask) ? 1u : 0u);
}

uint32_t __fastcall WtOptGrpFaceGutters(WtOptGrpFace* self, void*, int32_t* a, int32_t* b)
{
    Mix(0x7F030007u);
    *a = self->gutters[0];
    *b = self->gutters[1];
    return 0x07070000u;
}

// A colour: written only when it answers yes.
uint32_t __fastcall WtOptGrpFaceColour(WtOptGrpFace* self, void*, uint32_t index, uint32_t* colour)
{
    Mix(0x7F03000Bu);
    Mix(index);
    if (!(self->colourOk & (1u << (index & 31))))
        return 0x0B0B0B00u;
    *colour = self->colours[index % 5];
    return 0x0B0B0B01u;
}

uint32_t __fastcall WtOptGrpFaceSelection(WtOptGrpFace* self, void*)
{
    Mix(0x7F03000Du);
    return self->selection;
}

uint32_t __fastcall WtOptGrpFaceOutline(WtOptGrpFace* self, void*, uint32_t* a, uint32_t* b, uint32_t* c,
                                        uint32_t* d)
{
    Mix(0x7F030016u);
    if (!self->outlineOk)
        return 0x16161600u;
    *a = self->outline[0];
    *b = self->outline[1];
    *c = self->outline[2];
    *d = self->outline[3];
    return 0x16161601u;
}

uint32_t __fastcall WtOptGrpFaceAutofill(WtOptGrpFace* self, void*, uint32_t* manual, uint32_t* size)
{
    Mix(0x7F03001Du);
    if (!self->autofillOk)
        return 0x1D1D1D00u;
    *manual = self->manual;
    *size = self->size;
    return 0x1D1D1D01u;
}

void** WtOptGrpFaceTable()
{
    static void* table[30] = {};
    table[3] = reinterpret_cast<void*>(&WtOptGrpFaceWindow);
    table[4] = reinterpret_cast<void*>(&WtOptGrpFaceFlag);
    table[7] = reinterpret_cast<void*>(&WtOptGrpFaceGutters);
    table[11] = reinterpret_cast<void*>(&WtOptGrpFaceColour);
    table[13] = reinterpret_cast<void*>(&WtOptGrpFaceSelection);
    table[22] = reinterpret_cast<void*>(&WtOptGrpFaceOutline);
    table[29] = reinterpret_cast<void*>(&WtOptGrpFaceAutofill);
    return table;
}

void DriveWtOptGrp()
{
    // The options' windows (their image key under 0xDAA3B860, their area)
    // and captions.
    WtValue optionImage = WtPairValue(0x856DDBACu, 0x00ABCDEFu);
    WtValue optionNoKey = WtPairValue(0, 0);
    const WtProp optionProps[] = {{0xDAA3B860u, &optionImage}};
    const WtProp optionZeroProps[] = {{0xDAA3B860u, &optionNoKey}};
    WtOptGrpWindow optionWindows[] = {
        {WtOptGrpWindowTable(), optionProps, 1, nullptr, 0, {10, 20, 110, 45}},
        {WtOptGrpWindowTable(), nullptr, 0, nullptr, 0, {-5, 0, 5, 0}},
        {WtOptGrpWindowTable(), optionZeroProps, 1, nullptr, 0, {0, 0, 0, 0}},
        {WtOptGrpWindowTable(), optionProps, 1, nullptr, 0, {7, 300, 3, 200}},
    };
    WtString caption0("First");
    WtString caption1("");
    WtString caption2("Third option");
    WtString caption3("x:y");
    WtOptGrpButton buttons[] = {
        {WtOptGrpButtonTable(), &optionWindows[0], 1, {0, 0, 32, 32}, &caption0},
        {WtOptGrpButtonTable(), &optionWindows[1], 0xFFFFFFFFu, {1, 2, 3, 4}, &caption1},
        {WtOptGrpButtonTable(), &optionWindows[2], 77, {-1, -2, -3, -4}, &caption2},
        {WtOptGrpButtonTable(), &optionWindows[3], 0x80000000u, {5, 6, 7, 8}, &caption3},
    };
    WtOptGrpButton* const someButtons[] = {&buttons[0], &buttons[1], nullptr, &buttons[2], &buttons[3]};

    // The group's windows: fonts (two known, a number, a text, missing),
    // the caption's key (a pair, a zero pair, another kind, missing), the
    // back image (a pair, another kind, missing), autofit (on, off,
    // missing).
    WtValue font0 = WtNumber(kFbFontWords[0]);
    WtValue font1 = WtNumber(kFbFontWords[1]);
    WtValue fontNumber = WtNumber(0x1234u);
    WtValue fontText = WtTextValue("Tahoma");
    WtValue captionKey = WtPairValue(0x2026960Bu, 0x00000017u);
    WtValue captionZero = WtPairValue(0, 0);
    WtValue captionHalf = WtPairValue(0, 9);
    WtValue captionNumber = WtNumber(4);
    WtValue backImage = WtPairValue(0x856DDBACu, 0x55555555u);
    WtValue backNumber = WtNumber(3);
    WtValue autofitOn = {WtOptGrpValueTable(), 6, 6, {0xFFFFFF01u, 0, 0}, "", 0};
    WtValue autofitOff = {WtOptGrpValueTable(), 6, 6, {0x100u, 0, 0}, "", 0};
    const WtProp props0[] = {{0xFAA3BE85u, &font0},       {0xFAA3BE86u, &font1},     {0x4A4AF4ACu, &captionKey},
                             {0xDAA3B860u, &backImage},   {0xDACF8F9Cu, &autofitOn}};
    const WtProp props1[] = {{0xFAA3BE85u, &fontNumber},  {0x4A4AF4ACu, &captionZero}, {0xDAA3B860u, &backNumber},
                             {0xDACF8F9Cu, &autofitOff}};
    const WtProp props2[] = {{0xFAA3BE86u, &fontText},    {0x4A4AF4ACu, &captionHalf}};
    const WtProp props3[] = {{0xFAA3BE85u, &fontText},    {0xFAA3BE86u, &fontNumber}, {0x4A4AF4ACu, &captionNumber},
                             {0xDACF8F9Cu, &autofitOn}};
    WtOptGrpWindow windows[] = {
        {WtOptGrpWindowTable(), props0, 5, someButtons, 5, {0, 0, 0, 0}},
        {WtOptGrpWindowTable(), props1, 4, someButtons, 2, {0, 0, 0, 0}},
        {WtOptGrpWindowTable(), props2, 2, nullptr, 0, {0, 0, 0, 0}},
        {WtOptGrpWindowTable(), props3, 4, someButtons + 3, 2, {0, 0, 0, 0}},
    };
    const WtOptGrpFace faces[] = {
        {WtOptGrpFaceTable(), &windows[0], 0xF, {2, 3}, 0x1F, {0xFF000000u, 0xFFFFFFFFu, 0xFF102030u, 0x80FF00FFu,
                                                               0x00123456u},
         1, {0xFFFF0000u, 0xFF00FF00u, 0xFF0000FFu, 0xFFFFFFFFu}, 2, 1, 0, 12},
        {WtOptGrpFaceTable(), &windows[1], 0x0, {-1, 0x7FFFFFFF}, 0x15, {0xFF010203u, 0, 0xFFFEFDFCu, 0, 0x7F7F7F7Fu},
         0, {0, 0, 0, 0}, 0xFFFFFFFFu, 1, 3, 9},
        {WtOptGrpFaceTable(), &windows[2], 0x5, {0, 0}, 0x0, {0, 0, 0, 0, 0},
         1, {0xFF808080u, 0xFF000001u, 0xFF010000u, 0xFF000100u}, 0, 0, 0, 0},
        {WtOptGrpFaceTable(), &windows[3], 0xA, {100, -100}, 0x2, {0, 0xFFABCDEFu, 0, 0, 0},
         0, {0, 0, 0, 0}, 7, 1, 1, 0xFFFFFFFFu},
    };
    for (const WtOptGrpFace& face : faces) {
        WtString out("<");
        WtRegs regs = WtRegsOf();
        regs.args[0] = PrArg(&face);
        regs.args[1] = out.Arg();
        WtCall(addr::cGZUIScriptWriters_OptGrp, regs);
        out.MixText();
    }
    // The callback by address: each option, and none (AL 1, nothing
    // written).
    for (WtOptGrpButton* button : someButtons) {
        WtString out("[");
        WtRegs regs = WtRegsOf();
        regs.args[0] = PrArg(&windows[0]);
        regs.args[1] = button ? button->id : 42u;
        regs.args[2] = PrArg(button);
        regs.args[3] = out.Arg();
        MixByte(WtCall(addr::cGZUIScriptWriters_OptGrpOption, regs));
        out.MixText();
    }
    for (WtValue* v : {&optionImage, &optionNoKey, &font0, &font1, &fontNumber, &fontText, &captionKey,
                       &captionZero, &captionHalf, &captionNumber, &backImage, &backNumber, &autofitOn,
                       &autofitOff})
        Mix(v->releases);
    WtStage("optgrp");
}


// OPENSC4_TEST_WRITERS=<day>: part u17 by address. Every result goes into
// the fingerprint's `wt` part.
void DriveWritersDay(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_WRITERS") || !game::IsBuild641())
        return;
    g_wtOn = true;
    uint32_t* const saved = g_into;
    g_into = &g_wtHash;
    DriveWtLines();
    DriveWtProps();
    DriveWtFlatRect();
    DriveWtText();
    DriveWtTextEdit();
    DriveWtListBox();
    DriveWtLineInput();
    DriveWtCombo();
    DriveWtBtn();
    DriveWtBMP();
    DriveWtSlider();
    DriveWtScrollbar();
    DriveWtOptGrp();
    g_into = saved;
    log::Write("autotest: writers driven (hash %08X)", g_wtHash);
}

void WtFingerprint(cISC4City*, std::string& line)
{
    if (!g_wtOn)
        return;
    char text[32];
    std::snprintf(text, sizeof text, " | wt %08X", g_wtHash);
    line += text;
}

const FingerprintPart kWtFingerprint{"writers", 4, &WtFingerprint};
const DayHook kWtDrive{"writers", 4, &DriveWritersDay};

} // namespace
} // namespace opensc4::autotest
