// The advice class (cSC4Advice, class 0x8A09F5F4, 0xC0 bytes; 0x00407A90 to
// 0x00409690): one advice an advisor can give. Its script fields and flags,
// the trigger and its evaluation through the script context, the title and
// message texts, the copy, save and load, and the helper that opens an entry
// of a script table. Replacement, off by default:
//
//   scripts.advisors.advice1: the 28 functions of this part.
//
// The object: main vtable at +0, the reference-counted base at +4, the
// serialisable part at +0xC. The string and container helpers between the
// class's functions are shared with the rest of the game and stay the
// game's; they are called through their addresses.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <utility>

namespace opensc4::advice {
namespace {

namespace field { // from the object's start
constexpr std::size_t kTexts = 0x10;       // vector of (field name, localised string)
constexpr std::size_t kText1C = 0x1C;      // a string, never filled here
constexpr std::size_t kTitle = 0x30;       // localised string
constexpr std::size_t kMessage = 0x50;     // localised string
constexpr std::size_t kFlags = 0x70;
constexpr std::size_t kPriority = 0x78;
constexpr std::size_t kFrequency = 0x7C;
constexpr std::size_t kTimeout = 0x80;
constexpr std::size_t kMood = 0x84;
constexpr std::size_t kIndex = 0x88;       // entry in the script's "advices"
constexpr std::size_t kGuid = 0x8C;
constexpr std::size_t kTriggerText = 0x90; // string: the trigger's source
constexpr std::size_t kLoaded = 0xA4;      // byte: set by Read
constexpr std::size_t kEvent = 0xA8;
constexpr std::size_t kSubject = 0xAC;     // a counted object, saved by reference
constexpr std::size_t kSession = 0xB0;
constexpr std::size_t kCommand = 0xB4;
constexpr std::size_t kTriggerRef = 0xB8;  // the script's reference to the trigger function
constexpr std::size_t kEffects = 0xBC;     // list of integers
} // namespace field

constexpr std::size_t kSerializable = 0xC;

constexpr uint32_t kAdviceIid = 0x8A09F5F5;
constexpr uint32_t kSerializableIid = 0xE4FDA3D4;
constexpr int32_t kNoRef = -2;

namespace flag {
constexpr uint32_t kNewsOnly = 0x1;
constexpr uint32_t kActive = 0x2;
constexpr uint32_t kOnce = 0x4;
constexpr uint32_t kFlag8 = 0x8;
constexpr uint32_t kTextsStale = 0x10;
constexpr uint32_t kEvent = 0x40;
constexpr uint32_t kNoText = 0x80;
constexpr uint32_t kPersist = 0x100;
constexpr uint32_t kFlag200 = 0x200;
constexpr uint32_t kFlag400 = 0x400;
constexpr uint32_t kOtherClass = 0x800;
constexpr uint32_t kNoTimeout = 0x8000;
} // namespace flag

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

// An open table of the script context (SC4ScriptTable, 0x24 bytes).
struct ScriptTable {
    int32_t top;
    int32_t index;
    void* script;
    uint8_t open;
    GameString text;
};
static_assert(sizeof(ScriptTable) == 0x24);

struct ListNode {
    ListNode* next;
    ListNode* previous;
    uint32_t value;
};

// An item of the vector at +0x10.
struct TextField {
    const char* name;
    char* text;
};

struct TextKey {
    uint32_t type;
    uint32_t group;
    uint32_t instance;
};

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

uint32_t AddRef(void* object)
{
    return Virtual<uint32_t>(object, 0x4);
}

uint32_t Release(void* object)
{
    return Virtual<uint32_t>(object, 0x8);
}

void Free(void* memory)
{
    Cdecl<void>(addr::SC4_Free, memory);
}

void* Allocate(std::size_t size)
{
    return Cdecl<void*>(addr::SC4_stl_allocate, size);
}

void DestroyString(GameString& text)
{
    text.vtable = addr::SC4String_Vtable;
    if (text.body.begin)
        Free(text.body.begin);
}

// The body of a string, as the game's casts work it out.
StringBody* Body(void* text)
{
    return text ? reinterpret_cast<StringBody*>(static_cast<char*>(text) + 0x4) : nullptr;
}

uint8_t SetFlag(char* self, uint32_t which, uint8_t on)
{
    return Virtual<uint8_t>(self, 0x38, which, on);
}

uint8_t GetFlag(char* self, uint32_t which)
{
    return Virtual<uint8_t>(self, 0x3C, which);
}

int32_t TableInt(ScriptTable& table, const char* name)
{
    return This<int32_t>(addr::SC4ScriptTable_GetInt, &table, name);
}

// The open table's field as text: the table's own string.
void* TableString(ScriptTable& table, const char* name)
{
    return This<void*>(addr::SC4ScriptTable_GetString, &table, name);
}

// The game's 0x00407AA0 (main slot 6).
uint32_t __fastcall GetMood(char* self, void*)
{
    return At<uint32_t>(self, field::kMood);
}

// The game's 0x00407AE0 (main slot 14): sets or clears flags. Turning
// flag 2 on reloads stale texts and, when it was off, updates the effects;
// turning it off marks the texts stale and updates the effects when it was
// on. Bit 0x200 in the argument marks the texts stale either way.
uint8_t __fastcall SetFlagImpl(char* self, void*, uint32_t which, uint8_t on)
{
    const uint32_t flags = At<uint32_t>(self, field::kFlags);
    const bool wasActive = ((flags >> 1) & 1) != 0;
    if (on) {
        At<uint32_t>(self, field::kFlags) = flags | which;
        if (which & flag::kActive) {
            if (GetFlag(self, flag::kTextsStale))
                Virtual<uint32_t>(self, 0x78);
            if (!wasActive)
                Virtual<void>(self, 0x5C);
        }
    } else {
        At<uint32_t>(self, field::kFlags) = ~which & flags;
        if (which & flag::kActive) {
            SetFlag(self, flag::kTextsStale, 1);
            if (wasActive)
                Virtual<void>(self, 0x5C);
        }
    }
    if (which & flag::kFlag200)
        SetFlag(self, flag::kTextsStale, 1);
    return 1;
}

// The game's 0x00407B70 (main slot 15): whether every flag asked for is set.
uint32_t __fastcall GetFlagImpl(char* self, void*, uint32_t which)
{
    return (At<uint32_t>(self, field::kFlags) & which) == which ? 1 : 0;
}

// The game's 0x00407B90 (main slot 29).
uint32_t __fastcall IsActive(char* self, void*)
{
    return Virtual<uint32_t>(self, 0x3C, flag::kActive);
}

// The game's 0x00407BC0.
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** out)
{
    if (iid == kAdviceIid) {
        *out = self;
        AddRef(self);
        return 1;
    }
    if (iid == kSerializableIid) {
        *out = self ? self + kSerializable : nullptr;
        AddRef(self);
        return 1;
    }
    return This<uint8_t>(addr::cRZBaseUnknown_QueryInterface1, self + 0x4, iid, out);
}

// The game's 0x00408720: QueryInterface on the part at +4.
uint8_t __fastcall QueryInterface4(char* part, void*, uint32_t iid, void** out)
{
    return QueryInterface(part - 0x4, nullptr, iid, out);
}

// The game's 0x00408740: QueryInterface on the part at +0xC.
uint8_t __fastcall QueryInterfaceC(char* part, void*, uint32_t iid, void** out)
{
    return QueryInterface(part - kSerializable, nullptr, iid, out);
}

// The game's 0x00408730: the deleting destructor on the part at +4.
void* __fastcall Delete4(char* part, void*, uint32_t flags)
{
    return This<void*>(addr::SC4Advice_Delete, part - 0x4, flags);
}

// The game's 0x00407C20 (main slot 27).
uint32_t __fastcall GetCommand(char* self, void*)
{
    return At<uint32_t>(self, field::kCommand);
}

// The game's 0x00407C30 (main slot 28): a jump to slot 30.
uint32_t __fastcall ReloadTexts(char* self, void*)
{
    return Virtual<uint32_t>(self, 0x78);
}

// The game's 0x00407C50 (main slot 5): a new advice of the same class from
// the same script entry into *out, with this one's +0x74 and a copy of its
// subject.
uint8_t __fastcall Copy(char* self, void*, void** out)
{
    if (At<int32_t>(self, field::kIndex) == 0 || !out)
        return 0;
    *out = nullptr;
    void* copy = nullptr;
    void* const com = Cdecl<void*>(addr::RZGetCOM);
    const uint32_t classId = Virtual<uint32_t>(self + kSerializable, 0x14);
    Virtual<uint8_t>(com, 0xC, classId, kAdviceIid, &copy);
    if (!copy)
        return 0;
    if (Virtual<uint8_t>(copy, 0xC, At<int32_t>(self, field::kIndex))) {
        Virtual<void>(copy, 0x30, Virtual<uint32_t>(self, 0x2C));
        void* const subject = Virtual<void*>(self, 0x64);
        void* subjectCopy = nullptr;
        if (!subject || Virtual<uint8_t>(subject, 0x34, This<void**>(addr::SC4RefPtr_Reset, &subjectCopy))) {
            Virtual<uint8_t>(copy, 0x60, subjectCopy);
            *out = copy;
            AddRef(copy);
            if (subjectCopy)
                Release(subjectCopy);
            if (copy)
                Release(copy);
            return 1;
        }
        if (subjectCopy)
            Release(subjectCopy);
    }
    if (copy)
        Release(copy);
    return 0;
}

// The game's 0x00407D70 (main slot 19): the instance of the title's key.
uint32_t __fastcall GetTitleInstance(char* self, void*)
{
    TextKey key = {0, 0, 0};
    This<void>(addr::SC4LocalizedString_GetKey, self + field::kTitle, &key);
    return key.instance;
}

// The game's 0x00407DA0 (main slot 21): the instance of the message's key.
uint32_t __fastcall GetMessageInstance(char* self, void*)
{
    TextKey key = {0, 0, 0};
    This<void>(addr::SC4LocalizedString_GetKey, self + field::kMessage, &key);
    return key.instance;
}

// The game's 0x00407DD0 (main slot 24): takes the subject and marks the
// texts stale.
uint8_t __fastcall SetSubject(char* self, void*, void* subject)
{
    SetFlag(self, flag::kTextsStale, 1);
    void* const old = At<void*>(self, field::kSubject);
    if (subject != old) {
        if (subject)
            AddRef(subject);
        At<void*>(self, field::kSubject) = subject;
        if (old)
            Release(old);
    }
    return 1;
}

// The game's 0x00407E90 (main slot 4): gives the trigger function's
// reference back to the script context.
uint8_t __fastcall Shutdown(char* self, void*)
{
    if (At<int32_t>(self, field::kTriggerRef) != kNoRef) {
        if (void* const system = Global<void*>(addr::cSC4AdvisorSystem_Instance)) {
            if (void* const script = Virtual<void*>(system, 0x28))
                Virtual<void>(script, 0xF8, At<int32_t>(self, field::kTriggerRef)); // Unref
            At<int32_t>(self, field::kTriggerRef) = kNoRef;
        }
    }
    return 1;
}

// The game's 0x00407ED0 (main slot 22): runs a script text with this advice
// as the advisor system's context advice, when flag 2 is set and flag 8 is
// not.
uint8_t __fastcall RunScript(char* self, void*, void* text)
{
    if (GetFlag(self, flag::kFlag8))
        return 1;
    if (!GetFlag(self, flag::kActive))
        return 1;
    void* const system = Global<void*>(addr::cSC4AdvisorSystem_Instance);
    Virtual<void>(system, 0x48, self);
    void* const script = Virtual<void*>(system, 0x28);
    const char* const data = Virtual<const char*>(text, 0x14);
    const char* end = data;
    while (*end)
        ++end;
    Virtual<void>(script, 0xDC, data, static_cast<int32_t>(end - data), data); // DoBuffer
    Virtual<void>(system, 0x4C);
    return 1;
}

// The game's 0x00407F40 (main slot 23): hands every effect to the service
// at 0x00B43DC4, its slot 47 when flag 2 is set and slot 48 when not.
void __fastcall UpdateEffects(char* self, void*)
{
    ListNode* head = At<ListNode*>(self, field::kEffects);
    if (head->next == head)
        return;
    void* const service = Global<void*>(addr::SC4_Service43DC4);
    if (!service)
        return;
    const uint8_t active = GetFlag(self, flag::kActive);
    head = At<ListNode*>(self, field::kEffects);
    ListNode* node = head->next;
    if (node == head)
        return;
    do {
        if (active)
            Virtual<void>(service, 0xBC, node->value);
        else
            Virtual<void>(service, 0xC0, node->value);
        node = node->next;
    } while (node != At<ListNode*>(self, field::kEffects));
}

// The game's 0x00408090: Write, on the serialisable part at +0xC.
uint32_t __fastcall Write(char* part, void*, void* stream)
{
    char* const self = part - kSerializable;
    Virtual<uint8_t>(stream, 0x1C, uint32_t{4});
    Virtual<uint8_t>(stream, 0x20, At<uint32_t>(self, field::kIndex));
    Virtual<uint8_t>(stream, 0x20, At<uint32_t>(self, field::kGuid));
    Virtual<uint8_t>(stream, 0x20, At<uint32_t>(self, field::kFlags));
    Virtual<uint8_t>(stream, 0x20, At<uint32_t>(self, 0x74));
    Virtual<uint8_t>(stream, 0x20, At<uint32_t>(self, field::kPriority));
    Virtual<uint8_t>(stream, 0x20, At<uint32_t>(self, field::kFrequency));
    Virtual<uint8_t>(stream, 0x20, At<uint32_t>(self, field::kTimeout));
    Virtual<uint8_t>(stream, 0x20, At<uint32_t>(self, field::kMood));
    ListNode* head = At<ListNode*>(self, field::kEffects);
    uint32_t count = 0;
    for (ListNode* node = head->next; node != head; node = node->next)
        ++count;
    Virtual<uint8_t>(stream, 0x20, count);
    head = At<ListNode*>(self, field::kEffects);
    ListNode* node = head->next;
    if (node != head) {
        do {
            const uint32_t value = node->value;
            node = node->next;
            Virtual<uint8_t>(stream, 0x20, value);
        } while (node != At<ListNode*>(self, field::kEffects));
    }
    Cdecl<uint8_t>(addr::SC4Stream_WriteReference, stream, At<void*>(self, field::kSubject));
    return Virtual<uint8_t>(stream, 0x48) == 0 ? 1 : 0;
}

// The game's 0x004082F0: Read, on the serialisable part at +0xC. The
// effects are in versions above 3.
uint8_t __fastcall Read(char* part, void*, void* stream)
{
    char* const self = part - kSerializable;
    // The original reads the version into the low word of its argument.
    uint16_t version = static_cast<uint16_t>(reinterpret_cast<std::uintptr_t>(stream));
    Virtual<uint8_t>(stream, 0x1C, &version);
    Virtual<uint8_t>(stream, 0x20, &At<uint32_t>(self, field::kIndex));
    Virtual<uint8_t>(stream, 0x20, &At<uint32_t>(self, field::kGuid));
    Virtual<uint8_t>(stream, 0x20, &At<uint32_t>(self, field::kFlags));
    Virtual<uint8_t>(stream, 0x20, &At<uint32_t>(self, 0x74));
    Virtual<uint8_t>(stream, 0x20, &At<uint32_t>(self, field::kPriority));
    Virtual<uint8_t>(stream, 0x20, &At<uint32_t>(self, field::kFrequency));
    Virtual<uint8_t>(stream, 0x20, &At<uint32_t>(self, field::kTimeout));
    Virtual<uint8_t>(stream, 0x20, &At<uint32_t>(self, field::kMood));
    if (version > 3) {
        uint32_t count; // set by the stream only, as in the game
        uint32_t value;
        Virtual<uint8_t>(stream, 0x20, &count);
        while (count != 0) {
            --count;
            Virtual<uint8_t>(stream, 0x20, &value);
            ListNode* const head = At<ListNode*>(self, field::kEffects);
            auto* const node = static_cast<ListNode*>(Allocate(sizeof(ListNode)));
            node->value = value;
            ListNode* const last = head->previous;
            node->next = head;
            node->previous = last;
            last->next = node;
            head->previous = node;
        }
    }
    void** const subject = &At<void*>(self, field::kSubject);
    if (void* const old = *subject) {
        *subject = nullptr;
        Release(old);
    }
    Cdecl<uint8_t>(addr::SC4Stream_ReadReference, stream, uint32_t{0x6A26D050}, subject, uint32_t{0});
    SetFlag(self, flag::kTextsStale, 1);
    const uint8_t good = Virtual<uint8_t>(stream, 0x48) == 0 ? 1 : 0;
    At<uint8_t>(self, field::kLoaded) = good;
    return good;
}

// The game's 0x004084E0 (SC4ScriptTable::Open): remembers the script's
// stack top, pushes the global table `name` and, for an index within 1 to
// its length, the entry; `open` says whether a table is on top (the global
// itself for index 0).
ScriptTable* __fastcall TableOpen(ScriptTable* table, void*, void* script, const char* name, int32_t index)
{
    table->top = Virtual<int32_t>(script, 0x18); // GetTop
    table->index = index;
    table->script = script;
    table->open = 0;
    table->text.body.begin = nullptr;
    table->text.body.end = nullptr;
    table->text.body.capacity = nullptr;
    char* const buffer = static_cast<char*>(Allocate(8));
    table->text.body.begin = buffer;
    table->text.body.end = buffer;
    table->text.body.capacity = buffer + 8;
    *buffer = 0;
    table->text.vtable = addr::SC4String_Vtable;
    table->text.zero = 0;
    Virtual<void>(script, 0x94, name); // GetGlobal
    const int32_t length = Virtual<int32_t>(script, 0x100, int32_t{-1}); // GetN
    const uint8_t inside = !(table->index > length) && !(table->index < 0) ? 1 : 0;
    table->open = inside;
    if (inside && index != 0) {
        Virtual<void>(script, 0x7C, static_cast<double>(index)); // PushNumber
        Virtual<void>(script, 0x98, int32_t{-2});               // GetTable
        table->open = Virtual<uint8_t>(script, 0x120, int32_t{-1}); // IsTable
    }
    return table;
}

// The game's 0x004085A0 (SC4ScriptTable::Close): restores the stack top of
// an open table and frees the text.
void __fastcall TableClose(ScriptTable* table, void*)
{
    if (table->open)
        Virtual<void>(table->script, 0x1C, table->top); // SetTop
    table->text.vtable = addr::SC4String_Vtable;
    if (char* const buffer = table->text.body.begin)
        Free(buffer);
}

// An empty string as the game's inlined constructors make it at `at`.
void EmptyString(char* self, std::size_t at)
{
    At<char*>(self, at + 0x4) = nullptr;
    At<char*>(self, at + 0x8) = nullptr;
    At<char*>(self, at + 0xC) = nullptr;
    char* const buffer = static_cast<char*>(Allocate(8));
    At<char*>(self, at + 0x4) = buffer;
    At<char*>(self, at + 0x8) = buffer;
    At<char*>(self, at + 0xC) = buffer + 8;
    *buffer = 0;
    At<std::uintptr_t>(self, at) = addr::SC4String_Vtable;
    At<uint32_t>(self, at + 0x10) = 0;
}

// The game's 0x004085D0: the constructor.
char* __fastcall Construct(char* self, void*)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4Advice_VtableBase;
    This<void*>(addr::cRZBaseUnknown_Construct, self + 0x4);
    At<std::uintptr_t>(self, kSerializable) = addr::SC4SimGrid_VtableBase;
    At<std::uintptr_t>(self, 0x0) = addr::SC4Advice_Vtable;
    At<std::uintptr_t>(self, 0x4) = addr::SC4Advice_UnknownVtable;
    At<std::uintptr_t>(self, kSerializable) = addr::SC4Advice_SerializableVtable;
    At<TextField*>(self, field::kTexts) = nullptr;
    At<TextField*>(self, field::kTexts + 0x4) = nullptr;
    At<TextField*>(self, field::kTexts + 0x8) = nullptr;
    EmptyString(self, field::kText1C);
    This<void*>(addr::cSC4Advisors_F417A30, self + field::kTitle,
                Global<const char*>(addr::SC4AdviceNames_UndefinedTitle));
    This<void*>(addr::cSC4Advisors_F417A30, self + field::kMessage,
                Global<const char*>(addr::SC4AdviceNames_UndefinedMessage));
    At<uint32_t>(self, field::kFlags) = flag::kTextsStale;
    At<uint32_t>(self, 0x74) = 0;
    At<uint32_t>(self, field::kPriority) = 0x32;
    At<uint32_t>(self, field::kFrequency) = 0x1E;
    At<uint32_t>(self, field::kTimeout) = 0x1E;
    At<uint32_t>(self, field::kMood) = 3;
    At<uint32_t>(self, field::kIndex) = 0;
    At<uint32_t>(self, field::kGuid) = 0;
    EmptyString(self, field::kTriggerText);
    At<uint8_t>(self, field::kLoaded) = 0;
    At<void*>(self, field::kSubject) = nullptr;
    At<uint32_t>(self, field::kCommand) = 0;
    At<int32_t>(self, field::kTriggerRef) = kNoRef;
    At<ListNode*>(self, field::kEffects) = nullptr;
    auto* const head = static_cast<ListNode*>(Allocate(sizeof(ListNode)));
    head->next = head;
    head->previous = head;
    At<ListNode*>(self, field::kEffects) = head;
    const uint32_t session = Global<uint32_t>(addr::SC4Advice_SessionCounter) + 1;
    At<uint32_t>(self, field::kSession) = session;
    Global<uint32_t>(addr::SC4Advice_SessionCounter) = session;
    return self;
}

// The game's 0x00408750: the destructor. The two localised strings are
// taken down as plain strings.
void __fastcall Destruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4Advice_Vtable;
    At<std::uintptr_t>(self, 0x4) = addr::SC4Advice_UnknownVtable;
    At<std::uintptr_t>(self, kSerializable) = addr::SC4Advice_SerializableVtable;
    This<void>(addr::SC4PairList_Destroy, self + field::kEffects);
    if (void* const subject = At<void*>(self, field::kSubject))
        Release(subject);
    for (const std::size_t at : {field::kTriggerText, field::kMessage, field::kTitle, field::kText1C}) {
        At<std::uintptr_t>(self, at) = addr::SC4String_Vtable;
        if (char* const buffer = At<char*>(self, at + 0x4))
            Free(buffer);
    }
    if (TextField* const texts = At<TextField*>(self, field::kTexts))
        Free(texts);
    This<void>(addr::cRZBaseUnknown_Destruct, self + 0x4);
}

// The game's 0x00408800: runs the trigger function with this advice as the
// advisor system's context advice; true for a true boolean or a number
// other than zero. A trigger that is no function or fails only formats an
// error text (and leaves the script's stack as it is).
uint8_t __fastcall EvaluateTrigger(char* self, void*)
{
    void* const system = Global<void*>(addr::cSC4AdvisorSystem_Instance);
    Virtual<void>(system, 0x48, self);
    uint8_t result = 0;
    void* const script = Virtual<void*>(system, 0x28);
    const int32_t reference = At<int32_t>(self, field::kTriggerRef);
    if (reference != kNoRef) {
        Virtual<void>(script, 0xAC, reference); // GetRef
        if (Virtual<uint8_t>(script, 0x11C, int32_t{-1}) && // IsFunction
            Virtual<int32_t>(script, 0xCC, int32_t{0}, int32_t{1}) == 0) { // CallProtected
            if (Virtual<uint8_t>(script, 0x48, int32_t{-1})) { // IsBoolean
                result = Virtual<uint8_t>(script, 0x74, int32_t{-1}); // ToBoolean
            } else if (Virtual<uint8_t>(script, 0x38, int32_t{-1})) { // IsNumber
                const double number = Virtual<double>(script, 0x5C, int32_t{-1}); // ToNumber
                result = !(Global<double>(addr::SC4_DoubleZero) == number) ? 1 : 0;
            }
            Virtual<void>(script, 0x2C, int32_t{1}); // Pop
            Virtual<void>(system, 0x4C);
            return result;
        }
        GameString text;
        text.body.begin = nullptr;
        text.body.end = nullptr;
        text.body.capacity = nullptr;
        char* const buffer = static_cast<char*>(Allocate(8));
        text.body.begin = buffer;
        text.body.end = buffer;
        text.body.capacity = buffer + 8;
        *buffer = 0;
        text.vtable = addr::SC4String_Vtable;
        text.zero = 0;
        using SprintfFn = void*(__cdecl*)(void*, const char*, ...);
        reinterpret_cast<SprintfFn>(addr::cRZBaseString_Sprintf)(
            &text, "cSC4Advice::GetTrigger() -- error while executing a trigger script.\n Script GUID %0X.",
            At<uint32_t>(self, field::kGuid));
        DestroyString(text);
    }
    Virtual<void>(system, 0x4C);
    return result;
}

// The game's 0x00408970 (main slot 8): whether the advice is to be given:
// flag 0x400, or a true trigger and texts to show.
uint8_t __fastcall IsTriggered(char* self, void*)
{
    if (GetFlag(self, flag::kFlag400))
        return 1;
    if (!EvaluateTrigger(self, nullptr))
        return 0;
    return GetFlag(self, flag::kNoText) == 0 ? 1 : 0;
}

// The game's 0x00408A50: the class's factory.
char* __cdecl Create()
{
    void* const memory = Cdecl<void*>(addr::SC4_operator_new, std::size_t{0xC0});
    if (!memory)
        return nullptr;
    return Construct(static_cast<char*>(memory), nullptr);
}

// The game's 0x00408C70 (main slot 3): reads entry `index` of the script's
// "advices": the numbers, the trigger (compiled into a script function the
// advice keeps a reference to), the flags, the effects and the keys of the
// title and message.
uint8_t __fastcall Init(char* self, void*, int32_t index)
{
    uint8_t tag; // never set by the game either; only its address is passed
    At<int32_t>(self, field::kIndex) = index;
    if (index == 0)
        return 0;
    void* const script = Virtual<void*>(Global<void*>(addr::cSC4AdvisorSystem_Instance), 0x28);
    ScriptTable table;
    TableOpen(&table, nullptr, script, Global<const char*>(addr::SC4AdviceNames_Advices), index);
    if (!table.open) {
        DestroyString(table.text);
        return 0;
    }
    const uint32_t guid = static_cast<uint32_t>(TableInt(table, "guid"));
    if (At<uint32_t>(self, field::kGuid) != 0 && At<uint32_t>(self, field::kGuid) != guid) {
        TableClose(&table, nullptr);
        return 0;
    }
    At<uint32_t>(self, field::kGuid) = guid;
    At<int32_t>(self, field::kPriority) = TableInt(table, "priority");
    At<int32_t>(self, field::kMood) = TableInt(table, "mood");
    At<int32_t>(self, field::kFrequency) = TableInt(table, "frequency");
    At<int32_t>(self, field::kTimeout) = TableInt(table, "timeout");

    StringBody joined; // filled by the game's concatenation
    if (Virtual<uint8_t>(Global<void*>(addr::SC4_Service43C94), 0x54)) {
        GameString opening;
        StringBody* const openBody = Body(This<void*>(addr::SC4String_FromText, &opening, "=("));
        GameString prefix;
        StringBody* const prefixBody = Body(
            This<void*>(addr::SC4String_FromText, &prefix, Global<const char*>(addr::SC4AdviceNames_TriggerPrefix)));
        StringBody* const both = Cdecl<StringBody*>(addr::cSC4Advice_F408AE0, &joined, prefixBody, openBody);
        GameString whole;
        This<void*>(addr::cSC4Advice_F408B90, &whole, both);
        This<void*>(addr::cRZBaseString_Assign, self + field::kTriggerText, &whole);
        if (whole.body.begin)
            Free(whole.body.begin);
        if (joined.begin)
            Free(joined.begin);
        DestroyString(prefix);
        DestroyString(opening);
    }

    GameString trigger;
    This<void*>(addr::SC4String_CopyBuffer, &trigger.body, Body(TableString(table, "trigger")), uint32_t{0},
                uint32_t{0xFFFFFFFF}, &tag);
    const bool empty = trigger.body.begin == trigger.body.end;
    trigger.vtable = addr::SC4String_Vtable;
    trigger.zero = 0;
    if (empty)
        This<void>(addr::cRZBaseString_FromChar, &trigger, "0");
    GameString source;
    This<void*>(addr::SC4String_SC4String, &source);
    using SprintfFn = void*(__cdecl*)(void*, const char*, ...);
    reinterpret_cast<SprintfFn>(addr::cRZBaseString_Sprintf)(
        &source, "%s = function () return (%s); end", Global<const char*>(addr::SC4AdviceNames_TriggerPrefix),
        This<const char*>(addr::cRZBaseString_Data, &trigger));
    if (Virtual<int32_t>(script, 0xD8, This<const char*>(addr::cRZBaseString_Data, &source)) == 0) { // DoString
        Virtual<int32_t>(script, 0x18); // GetTop
        Virtual<void>(script, 0x94, Global<const char*>(addr::SC4AdviceNames_TriggerPrefix)); // GetGlobal
        if (Virtual<uint8_t>(script, 0x11C, int32_t{-1})) // IsFunction
            At<int32_t>(self, field::kTriggerRef) = Virtual<int32_t>(script, 0xC8, int32_t{1}); // Ref
        else
            Virtual<void>(script, 0x2C, int32_t{1}); // Pop
    }
    if (Virtual<uint8_t>(Global<void*>(addr::SC4_Service43C94), 0x54)) {
        GameString closing;
        StringBody* const closeBody = Body(This<void*>(addr::SC4String_FromText, &closing, ")"));
        StringBody* const both = Cdecl<StringBody*>(addr::cSC4Advice_F408AE0, &joined, &trigger.body, closeBody);
        char* const end = both->end;
        char* const begin = both->begin;
        This<void*>(addr::SC4String_AppendRange, self + field::kTriggerText + 0x4, begin, end, &tag);
        if (joined.begin)
            Free(joined.begin);
        DestroyString(closing);
    }

    SetFlag(self, flag::kOnce, TableInt(table, "once") != 0);
    SetFlag(self, flag::kNewsOnly, TableInt(table, "news_only") != 0);
    const int32_t event = TableInt(table, "event");
    At<int32_t>(self, field::kEvent) = event;
    SetFlag(self, flag::kEvent, event != 0);
    At<int32_t>(self, field::kCommand) = TableInt(table, "command");
    SetFlag(self, flag::kPersist, TableInt(table, "persist") != 0);
    SetFlag(self, flag::kNoTimeout, TableInt(table, "no_timeout") != 0);
    const uint32_t classId = static_cast<uint32_t>(TableInt(table, "class_id"));
    if (classId != Virtual<uint32_t>(self + kSerializable, 0x14))
        SetFlag(self, flag::kOtherClass, 1);

    const int32_t top = Virtual<int32_t>(script, 0x18); // GetTop
    if (table.open && Cdecl<uint8_t>(addr::SC4Script_GetField, script, "effects", top)) {
        if (Virtual<uint8_t>(script, 0x120, int32_t{-1})) { // IsTable
            Cdecl<uint32_t>(addr::SC4Script_ReadIntList, script, self + field::kEffects, int32_t{-1});
        } else if (Virtual<uint8_t>(script, 0x38, int32_t{-1})) { // IsNumber
            const double number = Virtual<double>(script, 0x5C, int32_t{-1}); // ToNumber
            int32_t value = static_cast<int32_t>(static_cast<int64_t>(number));
            This<void>(addr::SC4StlList_PushBack, self + field::kEffects, &value);
        }
        Virtual<void>(script, 0x2C, int32_t{1}); // Pop
    }

    GameString title;
    This<void*>(addr::SC4String_SC4String, &title);
    GameString message;
    This<void*>(addr::SC4String_SC4String, &message);
    StringBody* textBody = Body(TableString(table, "message"));
    if (textBody != &message.body)
        This<void*>(addr::SC4String_AssignRange, &message.body, textBody->begin, textBody->end, &tag);
    char* const messageText = self + field::kMessage;
    This<void>(addr::SC4LocalizedString_SetKeyText, messageText, This<const char*>(addr::cRZBaseString_Data, &message));
    textBody = Body(TableString(table, "title"));
    if (textBody != &title.body)
        This<void*>(addr::SC4String_AssignRange, &title.body, textBody->begin, textBody->end, &tag);
    char* const titleText = self + field::kTitle;
    This<void>(addr::SC4LocalizedString_SetKeyText, titleText, This<const char*>(addr::cRZBaseString_Data, &title));
    const uint8_t noText =
        message.body.begin == message.body.end && title.body.begin == title.body.end ? 1 : 0;
    SetFlag(self, flag::kNoText, noText);
    SetFlag(self, flag::kTextsStale, 1);
    if (GetFlag(self, flag::kActive) && !At<uint8_t>(self, field::kLoaded))
        Virtual<uint32_t>(self, 0x78);
    At<uint8_t>(self, field::kLoaded) = 0;
    This<void>(addr::SC4PairVector_Clear, self + field::kTexts);
    if (!GetFlag(self, flag::kNoText)) {
        TextField item;
        item.name = reinterpret_cast<const char*>(addr::SC4AdviceNames_Message);
        item.text = messageText;
        This<void>(addr::SC4PairVector_PushBack, self + field::kTexts, &item);
        item.name = reinterpret_cast<const char*>(addr::SC4AdviceNames_Title);
        item.text = titleText;
        This<void>(addr::SC4PairVector_PushBack, self + field::kTexts, &item);
    }
    DestroyString(message);
    DestroyString(title);
    DestroyString(source);
    DestroyString(trigger);
    if (table.open)
        Virtual<void>(table.script, 0x1C, table.top); // SetTop
    DestroyString(table.text);
    return 1;
}

// The game's 0x00409300 (main slot 30): loads the title and the message.
// Each field of the script entry names a text resource (after "%xx" is
// decoded); a text not in the database becomes a notice with the name.
// With flag 0x200 a text that has a key is wrapped in markers with the
// key's instance and the advice's GUID.
uint8_t __fastcall LoadTexts(char* self, void*)
{
    if (At<TextField*>(self, field::kTexts) == At<TextField*>(self, field::kTexts + 0x4))
        return 1;
    uint8_t tag; // never set by the game either; only its address is passed
    void* const system = Global<void*>(addr::cSC4AdvisorSystem_Instance);
    Virtual<void>(system, 0x48, self);
    void* const script = Virtual<void*>(system, 0x28);
    ScriptTable table;
    TableOpen(&table, nullptr, script, Global<const char*>(addr::SC4AdviceNames_Advices),
              At<int32_t>(self, field::kIndex));
    if (table.open) {
        TextField* const end = At<TextField*>(self, field::kTexts + 0x4);
        TextField* item = At<TextField*>(self, field::kTexts);
        if (item != end) {
            do {
                const char* const name = item->name;
                char* const text = item->text;
                ++item;
                if (table.open) {
                    Virtual<void>(&table.text, 0x58, uint32_t{0}, uint32_t{0xFFFFFFFF});
                    Virtual<void>(table.script, 0x84, name);        // PushString
                    Virtual<void>(table.script, 0x98, int32_t{-2}); // GetTable
                    const char* const value = Virtual<const char*>(table.script, 0x60, int32_t{-1}); // ToString
                    Virtual<void>(&table.text, 0x10, value);
                    Virtual<void>(table.script, 0x2C, int32_t{1}); // Pop
                }
                GameString key;
                key.body.begin = nullptr;
                key.body.end = nullptr;
                key.body.capacity = nullptr;
                This<void>(addr::cSC4Advice_F408420, &key.body, table.text.body.begin, table.text.body.end, &tag);
                key.vtable = addr::SC4String_Vtable;
                key.zero = 0;
                Cdecl<uint8_t>(addr::cSC4Advisors_F4184B0, &key, &key);
                This<void>(addr::SC4LocalizedString_SetKeyText, text,
                           This<const char*>(addr::cRZBaseString_Data, &key));
                if (!This<uint8_t>(addr::SC4LocalizedString_Load, text, int32_t{0}, int32_t{0})) {
                    static const char kMissing[] = "***_NOT_IN_TEXT_DATABASE_*** ";
                    GameString notice;
                    notice.body.begin = nullptr;
                    notice.body.end = nullptr;
                    notice.body.capacity = nullptr;
                    This<void>(addr::SC4String_FromRange, &notice.body, kMissing, kMissing + sizeof(kMissing) - 1,
                               &tag);
                    notice.vtable = addr::SC4String_Vtable;
                    notice.zero = 0;
                    StringBody joined; // filled by the game's concatenation
                    StringBody* const both =
                        Cdecl<StringBody*>(addr::cSC4Advice_F408AE0, &joined, &notice.body, &key.body);
                    char* const bothEnd = both->end;
                    char* const bothBegin = both->begin;
                    GameString whole;
                    whole.body.begin = nullptr;
                    whole.body.end = nullptr;
                    whole.body.capacity = nullptr;
                    This<void>(addr::cSC4Advice_F408420, &whole.body, bothBegin, bothEnd, &tag);
                    whole.zero = 0;
                    This<void*>(addr::SC4String_AssignRange, &key.body, whole.body.begin, whole.body.end, &tag);
                    DestroyString(whole);
                    if (joined.begin)
                        Free(joined.begin);
                    DestroyString(notice);
                    Cdecl<uint8_t>(addr::RZ_ConvertStringEncoding, &key, int32_t{0x4E4}, &key);
                    Virtual<void>(text, 0x38, &key);
                }
                This<uint8_t>(addr::SC4LocalizedString_Substitute, text, int32_t{0}, int32_t{1});
                if (GetFlag(self, flag::kFlag200)) {
                    TextKey textKey = {0, 0, 0};
                    This<void>(addr::SC4LocalizedString_GetKey, text, &textKey);
                    if (textKey.instance != 0) {
                        GameString shown;
                        This<void*>(addr::SC4String_FromText, &shown, Virtual<const char*>(text, 0x14));
                        using SprintfFn = void*(__cdecl*)(void*, const char*, ...);
                        const SprintfFn format = (*reinterpret_cast<SprintfFn**>(text))[0x6C / 4];
                        const uint32_t guid = Virtual<uint32_t>(self, 0x28);
                        format(text, "=text@%08X= %s =adv@%08X=", textKey.instance,
                               This<const char*>(addr::cRZBaseString_Data, &shown), guid);
                        DestroyString(shown);
                    }
                }
                DestroyString(key);
            } while (item != end);
        }
    }
    Virtual<void>(system, 0x4C);
    SetFlag(self, flag::kTextsStale, 0);
    if (table.open)
        Virtual<void>(table.script, 0x1C, table.top); // SetTop
    DestroyString(table.text);
    return 1;
}

bool InstallAdvice1(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[28] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSC4Advice_F407AA0, reinterpret_cast<void*>(&GetMood)},
        {addr::cSC4Advice_F407AE0, reinterpret_cast<void*>(&SetFlagImpl)},
        {addr::cSC4Advice_F407B70, reinterpret_cast<void*>(&GetFlagImpl)},
        {addr::cSC4Advice_F407B90, reinterpret_cast<void*>(&IsActive)},
        {addr::SC4Advice_QueryInterface, reinterpret_cast<void*>(&QueryInterface)},
        {addr::cSC4Advice_F407C20, reinterpret_cast<void*>(&GetCommand)},
        {addr::cSC4Advice_F407C30, reinterpret_cast<void*>(&ReloadTexts)},
        {addr::SC4Advice_Copy, reinterpret_cast<void*>(&Copy)},
        {addr::cSC4Advice_F407D70, reinterpret_cast<void*>(&GetTitleInstance)},
        {addr::cSC4Advice_F407DA0, reinterpret_cast<void*>(&GetMessageInstance)},
        {addr::cSC4Advice_F407DD0, reinterpret_cast<void*>(&SetSubject)},
        {addr::cSC4Advice_F407E90, reinterpret_cast<void*>(&Shutdown)},
        {addr::cSC4Advice_F407ED0, reinterpret_cast<void*>(&RunScript)},
        {addr::cSC4Advice_F407F40, reinterpret_cast<void*>(&UpdateEffects)},
        {addr::SC4Advice_Write, reinterpret_cast<void*>(&Write)},
        {addr::SC4Advice_Read, reinterpret_cast<void*>(&Read)},
        {addr::SC4ScriptTable_Open, reinterpret_cast<void*>(&TableOpen)},
        {addr::SC4ScriptTable_Close, reinterpret_cast<void*>(&TableClose)},
        {addr::SC4Advice_Construct, reinterpret_cast<void*>(&Construct)},
        {addr::cSC4Advice_F408720, reinterpret_cast<void*>(&QueryInterface4)},
        {addr::cSC4Advice_F408730, reinterpret_cast<void*>(&Delete4)},
        {addr::cSC4Advice_F408740, reinterpret_cast<void*>(&QueryInterfaceC)},
        {addr::SC4Advice_Destruct, reinterpret_cast<void*>(&Destruct)},
        {addr::cSC4Advice_F408800, reinterpret_cast<void*>(&EvaluateTrigger)},
        {addr::cSC4Advice_F408970, reinterpret_cast<void*>(&IsTriggered)},
        {addr::cSC4Advice_F408A50, reinterpret_cast<void*>(&Create)},
        {addr::SC4Advice_Init, reinterpret_cast<void*>(&Init)},
        {addr::cSC4Advice_F409300, reinterpret_cast<void*>(&LoadTexts)},
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

const bool g_advice1Registered = registry::Register({
    "scripts.advisors.advice1",
    "scripts/advisors",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Advice_Init,
    "Our advice class (cSC4Advice): flags, trigger, texts, copy, save and load, and the script table helper "
    "(docs/lua.md)",
    false,
    InstallAdvice1,
});

} // namespace
} // namespace opensc4::advice
