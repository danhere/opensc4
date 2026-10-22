// The GZ framework UI layer's part of the autotest (src/core/autotest_hooks.h,
// docs/gzui.md): the property set test (OPENSC4_TEST_PROPSETS, part u1) and
// the readers, writers and factory test (OPENSC4_TEST_PROPIO, parts u2,
// u2b and u3), the UI file parser test (OPENSC4_TEST_XMLPARSE, part u4) and
// the framework sound classes' test (OPENSC4_TEST_GZSOUND, part u5).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <process.h>
#include <string>
#include <vector>

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

} // namespace
} // namespace opensc4::autotest
