// Our ordinances (build 641): cSC4OrdinanceSimple, the exemplar-driven
// ordinance class, and the 20 classes derived from it that the ordinance
// simulator creates by class ID (docs/ordinance.md). Reimplemented on the
// original objects: simulators.ordinance.ordinances swaps the base class's
// methods into every ordinance vtable that still uses them,
// simulators.ordinance.special swaps the derived classes' own methods.
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace opensc4::ordinance_class {
namespace {

// Fields of cSC4OrdinanceSimple (0xA0 bytes; derived classes add +0xA0, +0xA4).
namespace field {
constexpr std::size_t kHolder = 0x8;         // cSCPropertyHolder: the exemplar's properties
constexpr std::size_t kHolderParent = 0x1C;  // its second interface: slot 3 sets the exemplar
constexpr std::size_t kInitialised = 0x28;   // uint8
constexpr std::size_t kKey = 0x2C;           // the exemplar's key: type, group, instance
constexpr std::size_t kId = 0x38;
constexpr std::size_t kName = 0x3C;          // cIGZString (std-style buffer at +0x40)
constexpr std::size_t kDescription = 0x50;   // cIGZString (buffer at +0x54)
constexpr std::size_t kYear = 0x64;          // year first available
constexpr std::size_t kChance = 0x68;        // SC4Percentage (float)
constexpr std::size_t kEnactment = 0x70;     // int64
constexpr std::size_t kRetractment = 0x78;   // int64
constexpr std::size_t kConstant = 0x80;      // int64: monthly constant income
constexpr std::size_t kFactor = 0x88;        // float: monthly income per unit
constexpr std::size_t kAdvisor = 0x8C;
constexpr std::size_t kIncomeOrdinance = 0x90; // uint8
constexpr std::size_t kAvailable = 0x91;     // uint8
constexpr std::size_t kOn = 0x92;            // uint8
constexpr std::size_t kEnabled = 0x93;       // uint8
constexpr std::size_t kAdjusted = 0x98;      // int64: this month's adjusted income
constexpr std::size_t kService = 0xA0;       // derived classes: a referenced game object
constexpr std::size_t kService2 = 0xA4;
} // namespace field

using AllocateFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* p);
const auto Allocate = reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("ordinance class: first %s", name);
}

// __ftol2: truncation toward zero, 64 bits.
int64_t Ftol(double value)
{
    return static_cast<int64_t>(value);
}

// The game's cIGZString over an STL-style buffer.
struct GameString {
    const void* vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};

// A localised string (vtable 0x00A85478): the string, then the text's key.
struct LocalizedString {
    GameString string;
    uint32_t key[3];
};

void DestroyString(GameString& string)
{
    string.vtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
    if (string.begin)
        Free(string.begin);
}

// A reference held at `offset` set to `object` (AddRef the new, Release the
// old), as the game's cRZAutoRefCount assignment.
void Assign(char* self, std::size_t offset, void* object)
{
    void* const old = At<void*>(self, offset);
    if (object == old)
        return;
    if (object)
        Virtual<uint32_t>(object, 0x4);
    At<void*>(self, offset) = object;
    if (old)
        Virtual<uint32_t>(old, 0x8);
}

// The reference at `offset` dropped.
void Drop(char* self, std::size_t offset)
{
    if (void* const object = At<void*>(self, offset)) {
        At<void*>(self, offset) = nullptr;
        Virtual<uint32_t>(object, 0x8);
    }
}

// --- cISC4Ordinance: cSC4OrdinanceSimple's methods ---------------------------------

// 3 (0x0080C9B0): once, the exemplar's properties.
uint8_t __fastcall Init(char* self, void*)
{
    static bool logged = false;
    FirstCall("Init", logged);
    if (!At<uint8_t>(self, field::kInitialised)) {
        At<uint8_t>(self, field::kInitialised) = 1;
        At<uint8_t>(self, field::kEnabled) = 1;
        Virtual<uint8_t>(self, 0x7C, self + field::kKey); // LoadFromExemplar
    }
    return 1;
}

// 4 (0x0080C9D0).
uint8_t __fastcall Shutdown(char* self, void*)
{
    static bool logged = false;
    FirstCall("Shutdown", logged);
    if (At<uint8_t>(self, field::kInitialised)) {
        At<uint8_t>(self, field::kInitialised) = 0;
        At<uint8_t>(self, field::kEnabled) = 0;
    }
    return 1;
}

// income = (value x factor + the monthly constant), truncated (x87 at 53 bits).
int64_t Income(char* self, double value)
{
    return Ftol(value * At<float>(self, field::kFactor) + static_cast<double>(At<int64_t>(self, field::kConstant)));
}

// 5 (0x0080CB00): per unit of the service at 0x00B43DA4's slot 16 (population).
int64_t __fastcall GetCurrentMonthlyIncome(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetCurrentMonthlyIncome", logged);
    const int32_t units = Virtual<int32_t>(Global<void*>(addr::SC4_Service43DA4), 0x40);
    return Income(self, units);
}

template <typename T, std::size_t Offset>
T __fastcall Getter(char* self, void*)
{
    return At<T>(self, Offset);
}

template <std::size_t Offset>
void* __fastcall Address(char* self, void*)
{
    return self + Offset;
}

// 16 (0x006C2040, shared): the property holder.
void* __fastcall GetMiscProperties(char* self, void*)
{
    return self ? self + field::kHolder : nullptr;
}

// 18 (0x0080BC70): enacted counts only while available.
uint32_t __fastcall IsOn(char* self, void*)
{
    return At<uint8_t>(self, field::kAvailable) && At<uint8_t>(self, field::kOn) ? 1u : 0u;
}

// 21 (0x0080CAB0): enabled, and the simulator's year (0x00B43D5C slot 6) has
// reached the first available one.
uint32_t CheckConditionsBase(char* self)
{
    if (!At<uint8_t>(self, field::kEnabled))
        return 0;
    void* const simulator = Global<void*>(addr::SC4_SimulatorService);
    if (!simulator)
        return 0;
    uint32_t year = 0;
    Virtual<uint32_t>(simulator, 0x18, &year, 0u, 0u, 0u, 0u);
    return year >= At<uint32_t>(self, field::kYear) ? 1u : 0u;
}

uint32_t __fastcall CheckConditions(char* self, void*)
{
    static bool logged = false;
    FirstCall("CheckConditions", logged);
    return CheckConditionsBase(self);
}

// 23 (0x0080C0F0): the month's adjusted income is the current one.
uint8_t __fastcall Simulate(char* self, void*)
{
    static bool logged = false;
    FirstCall("Simulate", logged);
    At<int64_t>(self, field::kAdjusted) = Virtual<int64_t>(self, 0x14);
    return 1;
}

// 24 (0x0080C9F0).
uint8_t __fastcall SetAvailable(char* self, void*, uint8_t available)
{
    static bool logged = false;
    FirstCall("SetAvailable", logged);
    Virtual<uint8_t>(self, 0x6C, static_cast<uint32_t>(available)); // ForceAvailable
    At<int64_t>(self, field::kAdjusted) = 0;
    return 1;
}

// 25 (0x0080CA50): enacting adds the enactment income's size to this
// month's adjusted income, repealing takes the repeal income's size off.
uint8_t SetOnBase(char* self, uint8_t on)
{
    if (At<uint8_t>(self, field::kOn) != on) {
        const double adjusted = static_cast<double>(At<int64_t>(self, field::kAdjusted));
        if (on)
            At<int64_t>(self, field::kAdjusted) =
                Ftol(std::fabs(static_cast<double>(At<int64_t>(self, field::kEnactment))) + adjusted);
        else
            At<int64_t>(self, field::kAdjusted) =
                Ftol(adjusted - std::fabs(static_cast<double>(At<int64_t>(self, field::kRetractment))));
        At<uint8_t>(self, field::kOn) = on;
    }
    return 1;
}

uint8_t __fastcall SetOn(char* self, void*, uint8_t on)
{
    static bool logged = false;
    FirstCall("SetOn", logged);
    return SetOnBase(self, on);
}

// 26-28 (0x0080BBB0, 0x0080BBC0, 0x0080BBD0).
template <std::size_t Offset>
uint8_t __fastcall SetFlag(char* self, void*, uint8_t value)
{
    At<uint8_t>(self, Offset) = value;
    return 1;
}

// 29 (0x0080BBE0).
uint8_t __fastcall ForceMonthlyAdjustedIncome(char* self, void*, int64_t income)
{
    static bool logged = false;
    FirstCall("ForceMonthlyAdjustedIncome", logged);
    At<int64_t>(self, field::kAdjusted) = income;
    return 1;
}

// 30 (0x0080CA30).
uint8_t __fastcall SetKey(char* self, void*, const uint32_t* key)
{
    static bool logged = false;
    FirstCall("SetKey", logged);
    At<uint32_t>(self, field::kKey) = key[0];
    At<uint32_t>(self, field::kKey + 4) = key[1];
    At<uint32_t>(self, field::kKey + 8) = key[2];
    return 1;
}

// The game's property and string helpers.
using GetUintFn = uint8_t(__cdecl*)(void* holder, uint32_t id, void* value);
using GetTextKeyFn = uint8_t(__cdecl*)(void* holder, uint32_t id, uint32_t* key);
using GetStringFn = uint8_t(__cdecl*)(void* holder, uint32_t id, GameString* value);
using FromTextKeyFn = void*(__thiscall*)(LocalizedString* string, const uint32_t* key, uint32_t flag);
using AssignStringFn = void*(__thiscall*)(void* string, LocalizedString* from);
using AssignRangeFn = void*(__thiscall*)(void* buffer, char* first, char* last, void* allocator);
using FormatFn = int(__cdecl*)(GameString* string, const char* format, ...);
using StringFn = const char*(__thiscall*)(GameString* string);
using LengthFn = uint32_t(__thiscall*)(GameString* string);

// A name or description left empty: the string property, or the ID in hex.
void FillEmpty(char* self, std::size_t offset, uint32_t property, GameString& scratch)
{
    if (At<char*>(self, offset + 4) != At<char*>(self, offset + 8))
        return;
    if (!reinterpret_cast<GetStringFn>(addr::SC4_GetPropertyString)(self + field::kHolder, property, &scratch))
        reinterpret_cast<FormatFn>(addr::cRZBaseString_Sprintf)(&scratch,
                                                               reinterpret_cast<const char*>(addr::Ordinance_IdFormat),
                                                               At<uint32_t>(self, field::kId));
    const char* const text = reinterpret_cast<StringFn>(addr::cRZBaseString_Data)(&scratch);
    const uint32_t length = reinterpret_cast<LengthFn>(addr::cRZBaseString_Strlen)(&scratch);
    Virtual<uint8_t>(self + offset, 0xC, text, length); // FromChar
}

// 31 (0x0080CDE0) (key): the exemplar's properties: ID, name and description
// (localised text keys, else string properties, else the ID in hex), year,
// chance, incomes, factor, advisor, income flag.
uint8_t __fastcall LoadFromExemplar(char* self, void*, const uint32_t* key)
{
    static bool logged = false;
    FirstCall("LoadFromExemplar", logged);
    uint8_t loaded = 0;
    void* resource = nullptr;
    if (Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, key, 0xA52160F5u, &resource, 0u, 0u)) {
        Virtual<uint8_t>(self, 0x78, key); // SetKey
        Virtual<uint32_t>(self + field::kHolderParent, 0xC, resource);
        uint32_t textKey[3] = {};
        GameString scratch{};
        scratch.begin = static_cast<char*>(Allocate(8));
        scratch.end = scratch.begin;
        scratch.capacity = scratch.begin + 8;
        scratch.begin[0] = 0;
        void* const holder = self + field::kHolder;
        const auto getUint = reinterpret_cast<GetUintFn>(addr::SC4_GetPropertyUint32);
        const auto getTextKey = reinterpret_cast<GetTextKeyFn>(addr::SC4_GetPropertyResourceKey);
        const auto fromTextKey = reinterpret_cast<FromTextKeyFn>(addr::cRZBaseString_FromTextKey);
        scratch.vtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
        scratch.zero = 0;
        uint32_t type = 0;
        getUint(holder, 0x10, &type);
        getUint(holder, 0x28EBBC30, self + field::kId);
        if (getTextKey(holder, 0x6A416B21, textKey) && textKey[0] != 0) {
            LocalizedString text{};
            fromTextKey(&text, textKey, 1);
            reinterpret_cast<AssignStringFn>(addr::cRZBaseString_Assign)(self + field::kName, &text);
            DestroyString(text.string);
        }
        FillEmpty(self, field::kName, 0x28EBBC32, scratch);
        if (getTextKey(holder, 0xCA416B2A, textKey)) {
            LocalizedString text{};
            fromTextKey(&text, textKey, 1);
            char dummy = 0;
            if (reinterpret_cast<char*>(&text.string.begin) != self + field::kDescription + 4)
                reinterpret_cast<AssignRangeFn>(addr::SC4String_AssignRange)(self + field::kDescription + 4,
                                                                               text.string.begin, text.string.end, &dummy);
            DestroyString(text.string);
        }
        FillEmpty(self, field::kDescription, 0x28EBBC34, scratch);
        using GetSint64Fn = uint8_t(__cdecl*)(void*, uint32_t, void*);
        getUint(holder, 0x28EBBC35, self + field::kYear);
        reinterpret_cast<GetUintFn>(addr::SC4_GetPropertyPercentage)(holder, 0x28EBBC36, self + field::kChance);
        reinterpret_cast<GetSint64Fn>(addr::SC4_GetPropertySint64)(holder, 0x28EBBC37, self + field::kEnactment);
        reinterpret_cast<GetSint64Fn>(addr::SC4_GetPropertySint64)(holder, 0x28EBBC38, self + field::kRetractment);
        reinterpret_cast<GetSint64Fn>(addr::SC4_GetPropertySint64)(holder, 0x28EBBC39, self + field::kConstant);
        reinterpret_cast<GetUintFn>(addr::SC4_GetPropertyFloat)(holder, 0x28EBBC3A, self + field::kFactor);
        getUint(holder, 0x28EBBC3B, self + field::kAdvisor);
        reinterpret_cast<GetUintFn>(addr::SC4_GetPropertyBool)(holder, 0x28EBBC3C, self + field::kIncomeOrdinance);
        loaded = 1;
        DestroyString(scratch);
    }
    if (resource)
        Virtual<uint32_t>(resource, 0x8);
    return loaded;
}

// --- cIGZSerializable (+0x4): Write and Read, version 4 ------------------------------

uint8_t __fastcall Write(char* serializable, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    char* const self = serializable - 0x4;
    using WriteBoolFn = uint8_t(__cdecl*)(void* stream, uint32_t value);
    using WriteHolderFn = uint8_t(__thiscall*)(void* holder, void* stream);
    Virtual<uint8_t>(stream, 0x1C, static_cast<uint16_t>(4));
    Virtual<uint8_t>(stream, 0x14, static_cast<uint32_t>(At<uint8_t>(self, field::kInitialised)));
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, field::kId));
    Virtual<uint8_t>(stream, 0x3C, self + field::kName);
    Virtual<uint8_t>(stream, 0x3C, self + field::kDescription);
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, field::kYear));
    Virtual<uint8_t>(stream, 0x30, At<float>(self, field::kChance));
    Virtual<uint8_t>(stream, 0x28, At<int64_t>(self, field::kEnactment));
    Virtual<uint8_t>(stream, 0x28, At<int64_t>(self, field::kRetractment));
    Virtual<uint8_t>(stream, 0x28, At<int64_t>(self, field::kConstant));
    Virtual<uint8_t>(stream, 0x30, At<float>(self, field::kFactor));
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, field::kAdvisor));
    Virtual<uint8_t>(stream, 0x14, static_cast<uint32_t>(At<uint8_t>(self, field::kAvailable)));
    Virtual<uint8_t>(stream, 0x14, static_cast<uint32_t>(At<uint8_t>(self, field::kOn)));
    Virtual<uint8_t>(stream, 0x14, static_cast<uint32_t>(At<uint8_t>(self, field::kEnabled)));
    Virtual<uint8_t>(stream, 0x28, At<int64_t>(self, field::kAdjusted));
    reinterpret_cast<WriteBoolFn>(addr::SC4Stream_WriteBool)(stream,
                                                             static_cast<uint32_t>(At<uint8_t>(self, field::kIncomeOrdinance)));
    reinterpret_cast<WriteHolderFn>(addr::cSCPropertyHolder_Write)(self + field::kHolder, stream);
    return Virtual<uint8_t>(stream, 0x48) == 0 ? 1 : 0;
}

// A localised text read into the string's buffer at `buffer` (Read assigns
// only a non-empty text).
void ReadText(char* self, uint32_t property, uint32_t* textKey, std::size_t buffer)
{
    char* const holder = self ? self + field::kHolder : nullptr;
    if (!reinterpret_cast<GetTextKeyFn>(addr::SC4_GetPropertyResourceKey)(holder, property, textKey))
        return;
    LocalizedString text{};
    reinterpret_cast<FromTextKeyFn>(addr::cRZBaseString_FromTextKey)(&text, textKey, 1);
    GameString& string = text.string;
    if (string.begin != string.end && reinterpret_cast<char*>(&string.begin) != self + buffer) {
        char dummy = 0;
        reinterpret_cast<AssignRangeFn>(addr::SC4String_AssignRange)(self + buffer, string.begin, string.end, &dummy);
    }
    DestroyString(string);
}

// Versions below 4 keep the chance in percent, below 2 lack the income flag
// (set for 0xA0D07129, legalised gambling). The exemplar is loaded again
// afterwards, so its values win over the saved ones.
uint8_t __fastcall Read(char* serializable, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Read", logged);
    char* const self = serializable - 0x4;
    using ReadBoolFn = uint8_t(__cdecl*)(void* stream, void* value);
    using ReadHolderFn = uint8_t(__thiscall*)(void* holder, void* stream);
    uint16_t version = 0;
    uint8_t byte = 0;
    Virtual<uint8_t>(stream, 0x1C, &version);
    Virtual<uint8_t>(stream, 0x14, &byte);
    At<uint8_t>(self, field::kInitialised) = byte != 0;
    Virtual<uint8_t>(stream, 0x24, self + field::kId);
    Virtual<uint8_t>(stream, 0x3C, self + field::kName);
    Virtual<uint8_t>(stream, 0x3C, self + field::kDescription);
    Virtual<uint8_t>(stream, 0x24, self + field::kYear);
    float chance = 0.0f;
    Virtual<uint8_t>(stream, 0x30, &chance);
    if (version >= 4)
        At<float>(self, field::kChance) = chance;
    else
        At<float>(self, field::kChance) =
            static_cast<float>(static_cast<double>(chance) * 0.01f);
    Virtual<uint8_t>(stream, 0x28, self + field::kEnactment);
    Virtual<uint8_t>(stream, 0x28, self + field::kRetractment);
    Virtual<uint8_t>(stream, 0x28, self + field::kConstant);
    Virtual<uint8_t>(stream, 0x30, self + field::kFactor);
    Virtual<uint8_t>(stream, 0x24, self + field::kAdvisor);
    for (const std::size_t flag : {field::kAvailable, field::kOn, field::kEnabled}) {
        Virtual<uint8_t>(stream, 0x14, &byte);
        At<uint8_t>(self, flag) = byte != 0;
    }
    Virtual<uint8_t>(stream, 0x28, self + field::kAdjusted);
    if (version >= 2)
        reinterpret_cast<ReadBoolFn>(addr::SC4Stream_ReadBool)(stream, self + field::kIncomeOrdinance);
    else
        At<uint8_t>(self, field::kIncomeOrdinance) = At<uint32_t>(self, field::kId) == 0xA0D07129;
    reinterpret_cast<ReadHolderFn>(addr::cSCPropertyHolder_Read)(self + field::kHolder, stream);
    uint32_t textKey[3] = {};
    ReadText(self, 0x6A416B21, textKey, field::kName + 4);
    ReadText(self, 0xCA416B2A, textKey, field::kDescription + 4);
    if (void* const exemplar = Virtual<void*>(self + field::kHolderParent, 0x10))
        Virtual<uint32_t>(exemplar, 0x2C, self + field::kKey); // its key
    Virtual<uint8_t>(self, 0x7C, self + field::kKey);          // LoadFromExemplar
    return Virtual<uint8_t>(stream, 0x48) == 0 ? 1 : 0;
}

// --- The vtables --------------------------------------------------------------

struct Slot {
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(index, original, fn) {index, original, reinterpret_cast<void*>(&fn)}

// cSC4OrdinanceSimple's methods by slot (vtable 0x00ABEA10).
const Slot kBaseSlots[] = {
    OPENSC4_SLOT(3, 0x0080C9B0, Init),
    OPENSC4_SLOT(4, 0x0080C9D0, Shutdown),
    OPENSC4_SLOT(5, 0x0080CB00, GetCurrentMonthlyIncome),
    OPENSC4_SLOT(6, 0x0080BC00, (Getter<uint32_t, field::kId>)),
    OPENSC4_SLOT(7, 0x0080BC10, Address<field::kName>),
    OPENSC4_SLOT(8, 0x00407BB0, Address<field::kDescription>),
    OPENSC4_SLOT(9, 0x00809250, (Getter<uint32_t, field::kYear>)),
    OPENSC4_SLOT(10, 0x0080BC20, Address<field::kChance>),
    OPENSC4_SLOT(11, 0x0080BC30, (Getter<int64_t, field::kEnactment>)),
    OPENSC4_SLOT(12, 0x0080BC40, (Getter<int64_t, field::kRetractment>)),
    OPENSC4_SLOT(13, 0x0080C350, (Getter<int64_t, field::kConstant>)),
    OPENSC4_SLOT(14, 0x0080BC50, (Getter<float, field::kFactor>)),
    OPENSC4_SLOT(15, 0x007C9FD0, (Getter<uint32_t, field::kAdvisor>)),
    OPENSC4_SLOT(16, 0x006C2040, GetMiscProperties),
    OPENSC4_SLOT(17, 0x0080BC60, (Getter<uint8_t, field::kAvailable>)),
    OPENSC4_SLOT(18, 0x0080BC70, IsOn),
    OPENSC4_SLOT(19, 0x0080BC90, (Getter<uint8_t, field::kEnabled>)),
    OPENSC4_SLOT(20, 0x0080BCA0, (Getter<int64_t, field::kAdjusted>)),
    OPENSC4_SLOT(21, 0x0080CAB0, CheckConditions),
    OPENSC4_SLOT(22, 0x007D2A10, (Getter<uint8_t, field::kIncomeOrdinance>)),
    OPENSC4_SLOT(23, 0x0080C0F0, Simulate),
    OPENSC4_SLOT(24, 0x0080C9F0, SetAvailable),
    OPENSC4_SLOT(25, 0x0080CA50, SetOn),
    OPENSC4_SLOT(26, 0x0080BBB0, SetFlag<field::kEnabled>),
    OPENSC4_SLOT(27, 0x0080BBC0, SetFlag<field::kAvailable>),
    OPENSC4_SLOT(28, 0x0080BBD0, SetFlag<field::kOn>),
    OPENSC4_SLOT(29, 0x0080BBE0, ForceMonthlyAdjustedIncome),
    OPENSC4_SLOT(30, 0x0080CA30, SetKey),
    OPENSC4_SLOT(31, 0x0080CDE0, LoadFromExemplar),
};

// Its cIGZSerializable methods (vtable 0x00ABE9F4).
const Slot kSerializableSlots[] = {
    OPENSC4_SLOT(3, 0x0080CB30, Write),
    OPENSC4_SLOT(4, 0x0080D0B0, Read),
};

// The ordinance classes: class ID, primary vtable, cIGZSerializable vtable.
struct Class {
    uint32_t id;
    std::uintptr_t vtable;
    std::uintptr_t serializable;
};

const Class kClasses[] = {
    {0x8BD4FC74, 0x00ABEA10, 0x00ABE9F4}, // cSC4OrdinanceSimple
    {0xA2BF1DDC, 0x00ABD9D8, 0x00ABD9BC}, {0xA2BF1E43, 0x00ABDAF8, 0x00ABDADC},
    {0x00D0723D, 0x00ABDC18, 0x00ABDBFC}, {0x40D07236, 0x00ABDD38, 0x00ABDD1C},
    {0x815B4CEF, 0x00ABDE58, 0x00ABDE3C}, {0x22F6E80C, 0x00ABDF78, 0x00ABDF5C},
    {0xA0D07129, 0x00ABE098, 0x00ABE07C}, {0x62BF1DAA, 0x00ABE1B8, 0x00ABE19C},
    {0x62BF1DB9, 0x00ABE2D8, 0x00ABE2BC}, {0xE0D0722E, 0x00ABE3F8, 0x00ABE3DC},
    {0x22F6E81B, 0x00ABE518, 0x00ABE4FC}, {0x82B9999B, 0x00ABE638, 0x00ABE61C},
    {0xE0D07233, 0x00ABE758, 0x00ABE73C}, {0xA2BF1DE5, 0x00ABE878, 0x00ABE85C},
    {0xC2F6E81F, 0x00ABEBC8, 0x00ABEBAC}, {0x62F6E7CF, 0x00ABECE8, 0x00ABECCC},
    {0x42BF1E18, 0x00ABEE08, 0x00ABEDEC}, {0xC2BF1E04, 0x00ABEF28, 0x00ABEF0C},
    {0x02BF1DFA, 0x00ABF048, 0x00ABF02C}, {0xC2BF1DC5, 0x00ABF168, 0x00ABF14C},
};

// Writes the slots that still hold the given originals; a slot holding
// something else (a derived class's own method) is left alone. Returns how
// many were written.
int PatchMatching(std::uintptr_t address, const Slot* slots, std::size_t count)
{
    auto* const vtable = reinterpret_cast<std::uintptr_t*>(address);
    constexpr std::size_t kSize = 32 * sizeof(std::uintptr_t);
    DWORD old = 0;
    if (!VirtualProtect(vtable, kSize, PAGE_READWRITE, &old))
        return -1;
    int written = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (vtable[slots[i].index] == slots[i].original) {
            vtable[slots[i].index] = reinterpret_cast<std::uintptr_t>(slots[i].ours);
            ++written;
        }
    }
    VirtualProtect(vtable, kSize, old, &old);
    return written;
}

bool InstallBase(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    // The base class's own vtable must hold every original.
    const auto* const base = reinterpret_cast<const std::uintptr_t*>(kClasses[0].vtable);
    for (const Slot& slot : kBaseSlots)
        if (base[slot.index] != slot.original) {
            log::Write("ordinance class: slot %d holds %08X, expected %08X; not installing", slot.index,
                       static_cast<unsigned>(base[slot.index]), static_cast<unsigned>(slot.original));
            return false;
        }
    int total = 0;
    for (const Class& c : kClasses) {
        const int primary = PatchMatching(c.vtable, kBaseSlots, sizeof(kBaseSlots) / sizeof(kBaseSlots[0]));
        const int serializable =
            PatchMatching(c.serializable, kSerializableSlots, sizeof(kSerializableSlots) / sizeof(kSerializableSlots[0]));
        if (primary < 0 || serializable < 0)
            return false;
        total += primary + serializable;
    }
    log::Write("ordinance class: %d slots in %u ordinance vtables are ours", total,
               static_cast<unsigned>(sizeof(kClasses) / sizeof(kClasses[0])));
    return true;
}

const bool g_registeredBase = registry::Register({
    "simulators.ordinance.ordinances",
    "simulators/ordinance",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    0x00ABEA10,
    "Our cSC4OrdinanceSimple methods in every ordinance class that uses them: flags, incomes, conditions, the "
    "exemplar load, Write and Read (docs/ordinance.md)",
    false,
    InstallBase,
});

// --- The derived classes' own methods ---------------------------------------------

using SimpleFn = uint8_t (*)(char*);

// Init variants: 0x0080BE70 (the base Init) and 0x0080C120 (0x22F6E80C: the
// objects at 0x00B43DA0 and 0x00B43D28 referenced first).
uint8_t __fastcall InitPlain(char* self, void*)
{
    static bool logged = false;
    FirstCall("InitPlain", logged);
    return Init(self, nullptr);
}

uint8_t __fastcall InitWithServices(char* self, void*)
{
    static bool logged = false;
    FirstCall("InitWithServices", logged);
    Assign(self, field::kService2, Global<void*>(addr::SC4_Service43DA0));
    Assign(self, field::kService, Global<void*>(addr::SC4_Value43D28));
    return Init(self, nullptr);
}

// Shutdown variants: 0x0080C750 (+0xA0 dropped), 0x0080C190 (+0xA4, +0xA0),
// 0x0080EBA0 (+0xA0, +0xA4).
uint8_t __fastcall ShutdownService(char* self, void*)
{
    static bool logged = false;
    FirstCall("ShutdownService", logged);
    Drop(self, field::kService);
    return Shutdown(self, nullptr);
}

uint8_t __fastcall ShutdownServices42(char* self, void*)
{
    static bool logged = false;
    FirstCall("ShutdownServices42", logged);
    Drop(self, field::kService2);
    Drop(self, field::kService);
    return Shutdown(self, nullptr);
}

uint8_t __fastcall ShutdownServices24(char* self, void*)
{
    static bool logged = false;
    FirstCall("ShutdownServices24", logged);
    Drop(self, field::kService);
    Drop(self, field::kService2);
    return Shutdown(self, nullptr);
}

// The budget simulator referenced at `offset` when nothing is (0x00B43D64,
// assumed present, as the game does).
void ReferenceBudget(char* self, std::size_t offset)
{
    if (!At<void*>(self, offset))
        Assign(self, offset, Global<void*>(addr::SC4_BudgetSimulator));
}

// The city's object from its slot 74, referenced at +0xA0 when nothing is.
void ReferenceCityObject(char* self)
{
    void* const city = Global<void*>(addr::SC4_City);
    if (!At<void*>(self, field::kService) && city)
        Assign(self, field::kService, Virtual<void*>(city, 0x128));
}

// A budget department's line (budget slot 28, department slot 20) value
// (slot 10), 0 when either is missing.
int64_t LineValue(void* department, uint32_t line)
{
    if (!department)
        return 0;
    void* const item = Virtual<void*>(department, 0x50, line);
    return item ? Virtual<int64_t>(item, 0x28) : 0;
}

void* Department(uint32_t id)
{
    return Virtual<void*>(Global<void*>(addr::SC4_BudgetSimulator), 0x70, id);
}

// 0x0080BE90 (0x40D07236): per unit of budget department 0x09188F42's line 0x03110000.
int64_t __fastcall IncomeBe90(char* self, void*)
{
    static bool logged = false;
    FirstCall("IncomeBe90", logged);
    ReferenceBudget(self, field::kService);
    return Income(self, static_cast<double>(LineValue(Department(0x09188F42), 0x03110000)));
}

// 0x0080BF90 (0x815B4CEF): per unit of department 0x8A4260A6's total (slot 15).
int64_t __fastcall IncomeBf90(char* self, void*)
{
    static bool logged = false;
    FirstCall("IncomeBf90", logged);
    ReferenceCityObject(self);
    return Income(self, static_cast<double>(Virtual<int64_t>(Department(0x8A4260A6), 0x3C)));
}

// 0x0080C020 (0x815B4CEF): the base conditions and that department's total is not 0.
uint32_t __fastcall CheckC020(char* self, void*)
{
    static bool logged = false;
    FirstCall("CheckC020", logged);
    ReferenceCityObject(self);
    return CheckConditionsBase(self) && Virtual<int64_t>(Department(0x8A4260A6), 0x3C) != 0 ? 1u : 0u;
}

// The second of the two counts +0xA0's slot 10 gives (initialised first if
// +0xA0 is missing, or with `both`, +0xA4).
uint32_t ServiceCount(char* self, bool both)
{
    if (!At<void*>(self, field::kService) || (both && !At<void*>(self, field::kService2)))
        Virtual<uint8_t>(self, 0xC); // Init
    uint32_t first = 0, second = 0;
    Virtual<uint32_t>(At<void*>(self, field::kService), 0x28, &first, &second);
    return second;
}

// 0x0080C1D0 (0x22F6E80C): per unit of that count.
int64_t __fastcall IncomeC1d0(char* self, void*)
{
    static bool logged = false;
    FirstCall("IncomeC1d0", logged);
    return Income(self, static_cast<double>(ServiceCount(self, false)));
}

// 0x0080C240 (0x22F6E80C): the count is not 0, +0xA4's slot 23 (3) is
// positive, and the base conditions.
uint32_t __fastcall CheckC240(char* self, void*)
{
    static bool logged = false;
    FirstCall("CheckC240", logged);
    if (ServiceCount(self, true) == 0)
        return 0;
    if (Virtual<int32_t>(At<void*>(self, field::kService2), 0x5C, 3u) <= 0)
        return 0;
    return CheckConditionsBase(self) ? 1u : 0u;
}

// 0x0080C350 (0xA0D07129, slot 5): the monthly constant only.
int64_t __fastcall IncomeConstant(char* self, void*)
{
    static bool logged = false;
    FirstCall("IncomeConstant", logged);
    return At<int64_t>(self, field::kConstant);
}

// 0x0080C3A0 (0xA0D07129, legalised gambling): repealing it demolishes the
// casinos (lots whose slot 46 (0) is 0x033A0000) through the service at
// 0x00B43D00, makes that building group unavailable again (the service at
// 0x00B43DB4) and drops the tool 0x88F154FB if it is the current one.
uint8_t __fastcall GamblingSetOn(char* self, void*, uint8_t on)
{
    static bool logged = false;
    FirstCall("GamblingSetOn", logged);
    const uint8_t result = SetOnBase(self, on);
    if (on)
        return result;
    void* const lots = Global<void*>(addr::SC4_LotManager);
    if (!lots)
        return result;
    if (auto* const iterator = Virtual<char**>(lots, 0x3C)) {
        // A list<cISC4Lot*> of the casinos: nodes next, previous, lot.
        auto** const head = static_cast<void**>(Allocate(0xC));
        head[0] = head;
        head[1] = head;
        for (;;) {
            auto** const end = reinterpret_cast<char**>(iterator[1]);
            if (!end)
                break;
            char* const at = iterator[0];
            if (at == *end)
                break;
            iterator[0] = *reinterpret_cast<char**>(at);
            void* const lot = At<void*>(at, 0x8);
            if (Virtual<uint32_t>(lot, 0xB8, 0u) != 0x033A0000)
                continue;
            Virtual<uint32_t>(lot, 0x4);
            auto** const node = static_cast<void**>(Allocate(0xC));
            node[2] = lot;
            Virtual<uint32_t>(lot, 0x4);
            auto** const last = static_cast<void**>(head[1]);
            node[0] = head;
            node[1] = last;
            last[0] = node;
            head[1] = node;
            Virtual<uint32_t>(lot, 0x8);
        }
        using ThisFn = void(__thiscall*)(void*);
        reinterpret_cast<ThisFn>(addr::SC4ListIterator_Release)(iterator);
        void* list = head;
        if (head[0] != head)
            Virtual<uint32_t>(Global<void*>(addr::SC4_Service43D00), 0x1C, 1u, &list, 0u, 1u, 0u, 0u, 0u, 0u);
        reinterpret_cast<ThisFn>(addr::SC4RefList_Clear)(&list);
        if (list)
            Free(list);
    }
    if (void* const groups = Global<void*>(addr::SC4_Service43DB4)) {
        if (void* const group = Virtual<void*>(groups, 0x18, 0x033A0000u)) {
            struct {
                uint8_t flag;
                GameString name;
            } copy{};
            using CopyFn = void*(__thiscall*)(void* to, void* from);
            reinterpret_cast<CopyFn>(addr::SC4BuildingGroup_Copy)(&copy, group);
            copy.flag = 1;
            Virtual<uint32_t>(groups, 0x1C, 0x033A0000u, &copy);
            DestroyString(copy.name);
        }
    }
    void* const tools = Global<void*>(addr::SC4_Service43DC4);
    if (void* const tool = Virtual<void*>(tools, 0x60))
        if (Virtual<uint32_t>(tool, 0x14) == 0x88F154FB)
            Virtual<uint32_t>(tools, 0x68, 0u);
    return result;
}

// 0x0080C590 (0x62BF1DB9): the base conditions and the object at 0x00B43D98
// (referenced at +0xA0) has a count (slot 24).
uint32_t __fastcall CheckC590(char* self, void*)
{
    static bool logged = false;
    FirstCall("CheckC590", logged);
    if (!At<void*>(self, field::kService))
        Assign(self, field::kService, Global<void*>(addr::SC4_PoliceSimulator));
    if (!CheckConditionsBase(self))
        return 0;
    void* const service = At<void*>(self, field::kService);
    return service && Virtual<uint32_t>(service, 0x60) != 0 ? 1u : 0u;
}

// 0x0080C780 (0xE0D07233): per unit of department 0x09188F4C's lines
// 0x03190000 and 0x0034A000 together.
int64_t __fastcall IncomeC780(char* self, void*)
{
    static bool logged = false;
    FirstCall("IncomeC780", logged);
    ReferenceBudget(self, field::kService);
    int64_t value = 0;
    if (void* const department = Department(0x09188F4C)) {
        if (void* const item = Virtual<void*>(department, 0x50, 0x03190000u))
            value = Virtual<int64_t>(item, 0x28);
        if (void* const item = Virtual<void*>(department, 0x50, 0x0034A000u))
            value += Virtual<int64_t>(item, 0x28);
    }
    return Income(self, static_cast<double>(value));
}

// 0x0080EA90 (0x62F6E7CF): department 0x8A416368's total times the factor,
// truncated, plus the constant.
int64_t __fastcall IncomeEa90(char* self, void*)
{
    static bool logged = false;
    FirstCall("IncomeEa90", logged);
    ReferenceBudget(self, field::kService);
    double scaled = 0.0;
    if (At<void*>(self, field::kService))
        if (void* const department = Department(0x8A416368))
            scaled = static_cast<double>(Virtual<int64_t>(department, 0x3C)) * At<float>(self, field::kFactor);
    return Ftol(scaled) + At<int64_t>(self, field::kConstant);
}

// 0x0080EBE0 (0x42BF1E18): as 0x0080EA90 with department 0x6A42A8F5 of the
// budget simulator referenced at +0xA4; the constant alone without it.
int64_t __fastcall IncomeEbe0(char* self, void*)
{
    static bool logged = false;
    FirstCall("IncomeEbe0", logged);
    ReferenceBudget(self, field::kService2);
    void* const budget = At<void*>(self, field::kService2);
    if (!budget)
        return At<int64_t>(self, field::kConstant);
    double scaled = 0.0;
    if (void* const department = Virtual<void*>(budget, 0x70, 0x6A42A8F5u))
        scaled = static_cast<double>(Virtual<int64_t>(department, 0x3C)) * At<float>(self, field::kFactor);
    return Ftol(scaled) + At<int64_t>(self, field::kConstant);
}

// 0x0080EC90 (0x42BF1E18): the base conditions and the pollution simulator's
// value (slot 33, a float) above 0.
uint32_t __fastcall CheckEc90(char* self, void*)
{
    static bool logged = false;
    FirstCall("CheckEc90", logged);
    if (!At<void*>(self, field::kService))
        Assign(self, field::kService, Global<void*>(addr::SC4_PollutionSimulator));
    if (!CheckConditionsBase(self))
        return 0;
    void* const pollution = At<void*>(self, field::kService);
    if (!pollution)
        return 0;
    const float* const value = Virtual<const float*>(pollution, 0x84);
    return static_cast<double>(*value) * 100.0f > 0.0 ? 1u : 0u;
}

struct Override {
    std::uintptr_t vtable;
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_OVERRIDE(vtable, index, original, fn) {vtable, index, original, reinterpret_cast<void*>(&fn)}

const Override kOverrides[] = {
    OPENSC4_OVERRIDE(0x00ABDD38, 3, 0x0080BE70, InitPlain),           // 0x40D07236
    OPENSC4_OVERRIDE(0x00ABDD38, 4, 0x0080C750, ShutdownService),
    OPENSC4_OVERRIDE(0x00ABDD38, 5, 0x0080BE90, IncomeBe90),
    OPENSC4_OVERRIDE(0x00ABDE58, 4, 0x0080C750, ShutdownService),     // 0x815B4CEF
    OPENSC4_OVERRIDE(0x00ABDE58, 5, 0x0080BF90, IncomeBf90),
    OPENSC4_OVERRIDE(0x00ABDE58, 21, 0x0080C020, CheckC020),
    OPENSC4_OVERRIDE(0x00ABDF78, 3, 0x0080C120, InitWithServices),    // 0x22F6E80C
    OPENSC4_OVERRIDE(0x00ABDF78, 4, 0x0080C190, ShutdownServices42),
    OPENSC4_OVERRIDE(0x00ABDF78, 5, 0x0080C1D0, IncomeC1d0),
    OPENSC4_OVERRIDE(0x00ABDF78, 21, 0x0080C240, CheckC240),
    OPENSC4_OVERRIDE(0x00ABE098, 5, 0x0080C350, IncomeConstant),      // 0xA0D07129
    OPENSC4_OVERRIDE(0x00ABE098, 25, 0x0080C3A0, GamblingSetOn),
    OPENSC4_OVERRIDE(0x00ABE2D8, 21, 0x0080C590, CheckC590),          // 0x62BF1DB9
    OPENSC4_OVERRIDE(0x00ABE758, 4, 0x0080C750, ShutdownService),     // 0xE0D07233
    OPENSC4_OVERRIDE(0x00ABE758, 5, 0x0080C780, IncomeC780),
    OPENSC4_OVERRIDE(0x00ABECE8, 3, 0x0080BE70, InitPlain),           // 0x62F6E7CF
    OPENSC4_OVERRIDE(0x00ABECE8, 4, 0x0080C750, ShutdownService),
    OPENSC4_OVERRIDE(0x00ABECE8, 5, 0x0080EA90, IncomeEa90),
    OPENSC4_OVERRIDE(0x00ABEE08, 3, 0x0080BE70, InitPlain),           // 0x42BF1E18
    OPENSC4_OVERRIDE(0x00ABEE08, 4, 0x0080EBA0, ShutdownServices24),
    OPENSC4_OVERRIDE(0x00ABEE08, 5, 0x0080EBE0, IncomeEbe0),
    OPENSC4_OVERRIDE(0x00ABEE08, 21, 0x0080EC90, CheckEc90),
};

#undef OPENSC4_OVERRIDE
#undef OPENSC4_SLOT

bool InstallSpecial(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    for (const Override& o : kOverrides) {
        const auto* const vtable = reinterpret_cast<const std::uintptr_t*>(o.vtable);
        if (vtable[o.index] != o.original) {
            log::Write("ordinance special: slot %d of %08X holds %08X, expected %08X; not installing", o.index,
                       static_cast<unsigned>(o.vtable), static_cast<unsigned>(vtable[o.index]),
                       static_cast<unsigned>(o.original));
            return false;
        }
    }
    for (const Override& o : kOverrides) {
        auto* const slot = reinterpret_cast<std::uintptr_t*>(o.vtable) + o.index;
        DWORD old = 0;
        if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old))
            return false;
        *slot = reinterpret_cast<std::uintptr_t>(o.ours);
        VirtualProtect(slot, sizeof(*slot), old, &old);
    }
    log::Write("ordinance special: %u derived-class slots are ours",
               static_cast<unsigned>(sizeof(kOverrides) / sizeof(kOverrides[0])));
    return true;
}

const bool g_registeredSpecial = registry::Register({
    "simulators.ordinance.special",
    "simulators/ordinance",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    0x00ABE098,
    "Our derived ordinance classes' own methods: their incomes from the budget and city services, their "
    "conditions, and legalised gambling's repeal (docs/ordinance.md)",
    false,
    InstallSpecial,
});

} // namespace
} // namespace opensc4::ordinance_class
