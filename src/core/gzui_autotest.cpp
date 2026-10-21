// The GZ framework UI layer's part of the autotest (src/core/autotest_hooks.h,
// docs/gzui.md): so far the property set test (OPENSC4_TEST_PROPSETS).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

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

bool g_on = false;
uint32_t g_hash = 2166136261u;

void Mix(uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        g_hash = (g_hash ^ ((value >> (8 * i)) & 0xFF)) * 16777619u;
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

} // namespace
} // namespace opensc4::autotest
