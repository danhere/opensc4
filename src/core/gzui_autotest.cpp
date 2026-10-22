// The GZ framework UI layer's part of the autotest (src/core/autotest_hooks.h,
// docs/gzui.md): the property set test (OPENSC4_TEST_PROPSETS, part u1) and
// the readers, writers and factory test (OPENSC4_TEST_PROPIO, parts u2,
// u2b and u3).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
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

} // namespace
} // namespace opensc4::autotest
