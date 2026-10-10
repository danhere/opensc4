// The advisor base class, third part (0x00410030 to 0x004117EB): the
// advisor's scalar deleting destructor and the two factories, the record
// vector's push, Init (the advisor's script table and its advice list),
// Read, the debug text helper and the two message handlers. Replacement,
// off by default:
//
//   scripts.advisors.base3: the ten functions of this range.
//
// The object: main vtable at +0, the advisor interface at +8, the
// serialisable part at +0xC, the message target at +0x10. The base is 0xB8
// bytes, the advisor 0xC0. Constructor, destructor, the containers' out of
// line helpers and the functions of the other ranges stay the game's.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace opensc4::advisor_base {
namespace {

namespace field { // from the object's start
constexpr std::size_t kScriptArg = 0x14;  // Init's argument
constexpr std::size_t kCaption = 0x18;    // a localised string, 0x20 bytes
constexpr std::size_t kName = 0x38;       // a localised string
constexpr std::size_t kStamp = 0x58;      // copied to a record's +0xC
constexpr std::size_t kValue5C = 0x5C;    // copied to a new record's +4
constexpr std::size_t kId = 0x60;         // the script table's "id"
constexpr std::size_t kValue64 = 0x64;
constexpr std::size_t kAdviceType = 0x68; // the script table's "advice_type"
constexpr std::size_t kByKey = 0x6C;      // multimap of int to record index
constexpr std::size_t kActive = 0x78;     // map of advice to record index
constexpr std::size_t kRecords = 0x84;    // vector of Record
constexpr std::size_t kList = 0xB0;       // list of (value, object)
constexpr std::size_t kListening = 0xB4;
constexpr std::size_t kDirty = 0xB8;      // the advisor only, from here
constexpr std::size_t kListening2 = 0xB9;
constexpr std::size_t kValueBC = 0xBC;
} // namespace field

// An advice of the advisor.
struct Record {
    void* advice;
    uint32_t a;
    uint32_t b;
    uint32_t c;
};

struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};

// An open table of the script context (SC4ScriptTable, 0x24 bytes).
struct ScriptTable {
    int32_t top;
    int32_t index;
    void* script;
    uint8_t open;
    GameString text;
};
static_assert(sizeof(ScriptTable) == 0x24);

struct PtrVector {
    void** begin;
    void** end;
    void** capacity;
};

// A tree's handle: the header node and the count. The game's copy
// constructor fills one on our stack, so it has the room the original
// frame leaves.
struct Tree {
    char* header;
    uint32_t size;
    uint32_t spare[3];
};

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
    reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(memory);
}

void* Allocate(std::size_t size)
{
    return reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_stl_allocate)(size);
}

void DestroyString(GameString& text)
{
    text.vtable = addr::SC4String_Vtable;
    if (text.begin)
        Free(text.begin);
}

void* MessageServer()
{
    return Global<void*>(addr::SC4_MessageServer);
}

// The message target of the object, as the game's casts work it out.
char* Target(char* obj)
{
    return obj ? obj + 0x10 : nullptr;
}

uint8_t HasFlag(void* advice, uint32_t flag)
{
    return Virtual<uint8_t>(advice, 0x3C, flag);
}

void SetFlag(void* advice, uint32_t flag, uint32_t on)
{
    Virtual<void>(advice, 0x38, flag, on);
}

// The game's 0x0040DA10 takes the object in ecx and three arguments the
// caller removes.
__declspec(naked) void AnnounceThunk()
{
    __asm mov ecx, dword ptr [esp + 4]
    __asm push dword ptr [esp + 16]
    __asm push dword ptr [esp + 16]
    __asm push dword ptr [esp + 16]
    __asm call dword ptr [esp + 32]
    __asm add esp, 12
    __asm ret
}

void Announce(char* obj, uint32_t queued, uint32_t type, uint32_t data)
{
    using ThunkFn = void(__cdecl*)(void*, uint32_t, uint32_t, uint32_t, std::uintptr_t);
    reinterpret_cast<ThunkFn>(&AnnounceThunk)(obj, queued, type, data, addr::cSC4Advisor_F40DA10);
}

// The in-order successor of a tree node (the header after the last).
char* Next(char* node)
{
    if (char* right = At<char*>(node, 0xC)) {
        while (char* left = At<char*>(right, 0x8))
            right = left;
        return right;
    }
    char* parent = At<char*>(node, 0x4);
    if (node == At<char*>(parent, 0xC)) {
        do {
            node = parent;
            parent = At<char*>(parent, 0x4);
        } while (node == At<char*>(parent, 0xC));
    }
    if (At<char*>(node, 0xC) != parent)
        node = parent;
    return node;
}

void ResetTree(char* tree)
{
    char* const header = At<char*>(tree, 0x0);
    At<char*>(header, 0x8) = header;
    At<char*>(At<char*>(tree, 0x0), 0x4) = nullptr;
    At<char*>(At<char*>(tree, 0x0), 0xC) = At<char*>(tree, 0x0);
    At<uint32_t>(tree, 0x4) = 0;
}

// Unlinks a node of a tree; the node to free comes back.
char* Unlink(char* header, char* node)
{
    using Fn = char*(__cdecl*)(char*, char**, char**, char**);
    return reinterpret_cast<Fn>(addr::SC4Tree_RebalanceForErase)(node, &At<char*>(header, 0x4), &At<char*>(header, 0x8),
                                                               &At<char*>(header, 0xC));
}

// Adds (key, index) to the multimap of int to int at `map`.
void InsertIndex(char* map, int32_t key, int32_t index)
{
    int32_t pair[2] = {key, index};
    char* parent = At<char*>(map, 0x0);
    char* node = At<char*>(parent, 0x4);
    while (node) {
        parent = node;
        node = key >= At<int32_t>(node, 0x10) ? At<char*>(node, 0xC) : At<char*>(node, 0x8);
    }
    void* result;
    using Fn = void(__thiscall*)(void*, void**, char*, char*, int32_t*, int32_t);
    reinterpret_cast<Fn>(addr::SC4Sint32Multimap_Insert)(map, &result, node, parent, pair, 0);
}

// The slot of an advice in the map of advice to record index (created
// when missing).
int32_t& ActiveSlot(char* map, void* key)
{
    return *reinterpret_cast<int32_t*(__thiscall*)(void*, void*)>(addr::cSC4Advisor_F40EFF0)(map, key);
}

// Appends a record to the vector at `vector`.
void PushRecord(char* vector, const Record* value, void* tag)
{
    Record* const end = At<Record*>(vector, 0x4);
    if (end == At<Record*>(vector, 0x8)) {
        using Fn = void(__thiscall*)(void*, Record*, const Record*, void*, uint32_t, uint32_t);
        reinterpret_cast<Fn>(addr::cSC4Advisor_F40FD90)(vector, end, value, tag, 1, 1);
        return;
    }
    if (end) {
        end->advice = value->advice;
        if (value->advice)
            AddRef(value->advice);
        end->a = value->a;
        end->b = value->b;
        end->c = value->c;
    }
    At<Record*>(vector, 0x4) = At<Record*>(vector, 0x4) + 1;
}

// The game's 0x00410030: the advisor's scalar deleting destructor.
char* __fastcall AdvisorDelete(char* obj, void*, uint32_t flags)
{
    At<std::uintptr_t>(obj, 0x0) = addr::SC4AdvisorBase_Vtable;
    At<std::uintptr_t>(obj, 0x8) = addr::SC4AdvisorBase_Vtable8;
    At<std::uintptr_t>(obj, 0xC) = addr::SC4AdvisorBase_VtableC;
    At<std::uintptr_t>(obj, 0x10) = addr::SC4AdvisorBase_Vtable10;
    reinterpret_cast<void(__thiscall*)(void*)>(addr::SC4AgentBase_Destruct)(obj);
    if (flags & 1)
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_operator_delete)(obj);
    return obj;
}

// The game's 0x00410070: push_back of the record vector.
void __fastcall RecordsPushBack(char* vector, void*, const Record* value)
{
    PushRecord(vector, value, &value);
}

// The game's 0x004100D0: a new base object, as its interface.
char* __cdecl NewAgentBase()
{
    void* const memory = reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0xB8);
    if (memory) {
        if (char* const obj = reinterpret_cast<char*(__thiscall*)(void*)>(addr::SC4AgentBase_Construct)(memory))
            return obj + 0x8;
    }
    return nullptr;
}

void OpenTable(ScriptTable& table, void* script, const char* name, int32_t index)
{
    using Fn = void*(__thiscall*)(ScriptTable*, void*, const char*, int32_t);
    reinterpret_cast<Fn>(addr::SC4ScriptTable_Open)(&table, script, name, index);
}

void CloseTable(ScriptTable& table)
{
    if (table.open)
        Virtual<void>(table.script, 0x1C, table.top);
    DestroyString(table.text);
}

int32_t TableInt(ScriptTable& table, const char* name)
{
    return reinterpret_cast<int32_t(__thiscall*)(ScriptTable*, const char*)>(addr::SC4ScriptTable_GetInt)(&table, name);
}

// A copy of the open table's text field.
void TableString(ScriptTable& table, const char* name, GameString& out, uint8_t* tag)
{
    void* const text =
        reinterpret_cast<void*(__thiscall*)(ScriptTable*, const char*)>(addr::SC4ScriptTable_GetString)(&table, name);
    void* const buffer = text ? static_cast<char*>(text) + 0x4 : nullptr;
    using CopyFn = void*(__thiscall*)(void*, void*, uint32_t, uint32_t, uint8_t*);
    reinterpret_cast<CopyFn>(addr::SC4String_CopyBuffer)(&out.begin, buffer, 0, 0xFFFFFFFF, tag);
    out.vtable = addr::SC4String_Vtable;
    out.zero = 0;
}

// Sets a localised string from a text: the text names a resource, or else
// is taken itself (from code page 1252).
void SetLocalised(char* string, GameString& text)
{
    const char* const data = reinterpret_cast<const char*(__thiscall*)(void*)>(addr::cRZBaseString_Data)(&text);
    reinterpret_cast<void(__thiscall*)(void*, const char*)>(addr::SC4LocalizedString_SetKeyText)(string, data);
    if (reinterpret_cast<uint8_t(__thiscall*)(void*, int32_t, int32_t)>(addr::SC4LocalizedString_Load)(string, 1, 0))
        return;
    reinterpret_cast<void(__cdecl*)(void*, int32_t, void*)>(addr::RZ_ConvertStringEncoding)(&text, 0x4E4, &text);
    Virtual<void>(string, 0x38, &text);
}

void PushPointer(PtrVector& vector, Record** value, uint8_t* tag)
{
    if (vector.end != vector.capacity) {
        if (vector.end)
            *vector.end = *value;
        ++vector.end;
        return;
    }
    using Fn = void(__thiscall*)(void*, void**, Record**, uint8_t*, uint32_t, uint32_t);
    reinterpret_cast<Fn>(addr::SC4PtrVector_InsertN)(&vector, vector.end, value, tag, 1, 1);
}

// The game's 0x00410100, on the interface at +8: reads the advisor's
// table (argument `which` of the script's "advisors") and brings the
// records in line with the script's "advices" of this advisor's type.
uint8_t __fastcall Init(char* part, void*, int32_t which)
{
    char* const obj = part - 0x8;
    uint8_t tag; // never set by the game either; only its address is passed
    void* const system = Global<void*>(addr::cSC4AdvisorSystem_Instance);
    void* const script = Virtual<void*>(system, 0x28);
    At<int32_t>(obj, field::kScriptArg) = which;
    ScriptTable table;
    OpenTable(table, script, Global<const char*>(addr::SC4ScriptNames_Advisors), which);
    if (!table.open) {
        DestroyString(table.text);
        return 0;
    }
    Virtual<void>(table.script, 0x84, "id");
    Virtual<void>(table.script, 0x98, int32_t{-2});
    const uint32_t id = Virtual<uint32_t>(table.script, 0x58, int32_t{-1});
    Virtual<void>(table.script, 0x2C, int32_t{1});
    if (At<uint32_t>(obj, field::kId) != 0 && At<uint32_t>(obj, field::kId) != id) {
        reinterpret_cast<void(__thiscall*)(ScriptTable*)>(addr::SC4ScriptTable_Close)(&table);
        return 0;
    }
    At<uint32_t>(obj, field::kId) = id;

    GameString caption;
    TableString(table, "caption", caption, &tag);
    char* const captionString = obj + field::kCaption;
    SetLocalised(captionString, caption);
    const int32_t bar = Virtual<int32_t>(captionString, 0x60, "|", uint32_t{0}, uint32_t{1});
    if (bar != -1) {
        GameString left;
        using LeftFn = GameString*(__thiscall*)(void*, GameString*, int32_t);
        GameString* const made = reinterpret_cast<LeftFn>(addr::cRZBaseString_Left)(captionString, &left, bar);
        Virtual<void>(captionString, 0x38, made);
        DestroyString(left);
    }
    GameString name;
    TableString(table, "name", name, &tag);
    if (name.begin != name.end)
        SetLocalised(obj + field::kName, name);
    At<int32_t>(obj, field::kAdviceType) = TableInt(table, "advice_type");
    Announce(obj, 1, 0x8A09FA68, 0);
    DestroyString(name);
    DestroyString(caption);
    CloseTable(table);

    // The script's advices of this type: guid to (index, class).
    Tree wanted;
    wanted.header = static_cast<char*>(Allocate(0x1C));
    wanted.size = 0;
    At<uint8_t>(wanted.header, 0x0) = 0;
    At<char*>(wanted.header, 0x4) = nullptr;
    At<char*>(wanted.header, 0x8) = wanted.header;
    At<char*>(wanted.header, 0xC) = wanted.header;
    OpenTable(table, script, Global<const char*>(addr::SC4ScriptNames_Advices), 0);
    if (!table.open) {
        if (table.text.begin)
            Free(table.text.begin);
        reinterpret_cast<void(__thiscall*)(Tree*)>(addr::cSC4Advisor_F40E960)(&wanted);
        return 0;
    }
    Virtual<void>(table.script, 0x1C, table.top);
    if (table.text.begin)
        Free(table.text.begin);
    Virtual<void>(script, 0x94, Global<const char*>(addr::SC4ScriptNames_Advices));
    const int32_t count = Virtual<int32_t>(script, 0x100, int32_t{-1});
    Virtual<void>(script, 0x2C, int32_t{1});
    for (int32_t index = 1; index <= count; ++index) {
        OpenTable(table, script, Global<const char*>(addr::SC4ScriptNames_Advices), index);
        if (table.open) {
            if (TableInt(table, "type") == At<int32_t>(obj, field::kAdviceType)) {
                const uint32_t guid = static_cast<uint32_t>(TableInt(table, "guid"));
                const uint32_t classId = static_cast<uint32_t>(TableInt(table, "class_id"));
                if (guid && classId) {
                    uint32_t key = guid;
                    using FindFn = uint32_t*(__thiscall*)(Tree*, uint32_t*);
                    uint32_t* const value = reinterpret_cast<FindFn>(addr::cSC4Advisor_F40FD10)(&wanted, &key);
                    value[0] = static_cast<uint32_t>(index);
                    value[1] = classId;
                }
            }
            Virtual<void>(table.script, 0x1C, table.top);
        }
        DestroyString(table.text);
    }

    // Nothing is active any more.
    char* const active = obj + field::kActive;
    while (At<uint32_t>(active, 0x4) != 0) {
        char* header = At<char*>(active, 0x0);
        SetFlag(At<void*>(At<char*>(header, 0x8), 0x10), 2, 0);
        header = At<char*>(active, 0x0);
        char* const node = Unlink(header, At<char*>(header, 0x8));
        if (void* const advice = At<void*>(node, 0x10))
            Release(advice);
        if (node)
            Free(node);
        --At<uint32_t>(active, 0x4);
    }
    Announce(obj, 0, 0x8A09FA67, 0);

    // The records the script still has leave the wanted map; the others
    // are collected and dropped, last first.
    PtrVector stale = {nullptr, nullptr, nullptr};
    Record* it = At<Record*>(obj, field::kRecords);
    Record* const last = At<Record*>(obj, field::kRecords + 0x4);
    for (; it != last; ++it) {
        void* const advice = it->advice;
        const uint32_t guid = Virtual<uint32_t>(advice, 0x28);
        char* const header = wanted.header;
        char* found = header;
        for (char* node = At<char*>(header, 0x4); node;) {
            if (At<uint32_t>(node, 0x10) < guid) {
                node = At<char*>(node, 0xC);
            } else {
                found = node;
                node = At<char*>(node, 0x8);
            }
        }
        if (found == header || guid < At<uint32_t>(found, 0x10)) {
            PushPointer(stale, &it, &tag);
            continue;
        }
        if (!Virtual<uint8_t>(advice, 0xC, At<int32_t>(found, 0x14))) {
            SetFlag(advice, 0x800, 0);
            PushPointer(stale, &it, &tag);
        } else if (HasFlag(advice, 0x800)) {
            PushPointer(stale, &it, &tag);
        }
        if (!HasFlag(advice, 0x800)) {
            if (char* const node = Unlink(wanted.header, found))
                Free(node);
            --wanted.size;
        }
    }
    for (void** at = stale.end; stale.begin != at; --at) {
        Record* const record = static_cast<Record*>(at[-1]);
        void* const advice = record->advice;
        Virtual<void>(part, 0x50, advice);
        Virtual<void>(advice, 0x10);
        Record* const end = At<Record*>(obj, field::kRecords + 0x4);
        if (record + 1 != end) {
            using MoveFn = Record*(__cdecl*)(Record*, Record*, Record*, uint8_t*, int32_t);
            reinterpret_cast<MoveFn>(addr::cSC4Advisor_F40E260)(record + 1, end, record, &tag, 0);
        }
        Record* const newEnd = At<Record*>(obj, field::kRecords + 0x4) - 1;
        At<Record*>(obj, field::kRecords + 0x4) = newEnd;
        if (newEnd->advice)
            Release(newEnd->advice);
    }

    // What is left in the map is new: made by class and appended.
    char* const wantedHeader = wanted.header;
    for (char* node = At<char*>(wantedHeader, 0x8); node != wantedHeader;) {
        const int32_t index = At<int32_t>(node, 0x14);
        const uint32_t classId = At<uint32_t>(node, 0x18);
        void* advice = nullptr;
        void* const com = reinterpret_cast<void*(__cdecl*)()>(addr::RZGetCOM)();
        if (Virtual<uint8_t>(com, 0xC, classId, uint32_t{0x8A09F5F5}, &advice) &&
            Virtual<uint8_t>(advice, 0xC, index)) {
            Record record;
            record.advice = nullptr;
            Virtual<void>(advice, 0x30, Virtual<uint32_t>(part, 0x14));
            if (advice) {
                AddRef(advice);
                record.advice = advice;
            }
            record.a = At<uint32_t>(obj, field::kValue5C);
            record.b = 0;
            record.c = At<uint32_t>(obj, field::kStamp) - 1;
            PushRecord(obj + field::kRecords, &record, &tag);
            if (record.advice)
                Release(record.advice);
        }
        node = Next(node);
        if (advice)
            Release(advice);
    }

    // The index by key, and the advisor system's own.
    char* const byKey = obj + field::kByKey;
    if (At<uint32_t>(byKey, 0x4) != 0) {
        reinterpret_cast<void(__thiscall*)(void*, char*)>(addr::SC4Uint32Map_EraseSubtree2)(
            byKey, At<char*>(At<char*>(byKey, 0x0), 0x4));
        ResetTree(byKey);
    }
    const int32_t records =
        static_cast<int32_t>(At<char*>(obj, field::kRecords + 0x4) - At<char*>(obj, field::kRecords)) >> 4;
    for (int32_t i = 0; i < records; ++i) {
        void* const advice = At<Record*>(obj, field::kRecords)[i].advice;
        if (!HasFlag(advice, 0x40))
            continue;
        const int32_t key = Virtual<int32_t>(advice, 0x68);
        InsertIndex(byKey, key, i);
        Virtual<void>(system, 0x40, key, obj ? part : nullptr);
    }
    reinterpret_cast<void(__thiscall*)(void*)>(addr::cSC4Advisor_F40FA10)(obj);
    if (!At<uint8_t>(obj, field::kListening)) {
        Virtual<uint8_t>(MessageServer(), 0x14, Target(obj), uint32_t{0xEA917750});
        Virtual<uint8_t>(MessageServer(), 0x14, Target(obj), uint32_t{0x8A917759});
        Virtual<uint8_t>(MessageServer(), 0x14, Target(obj), uint32_t{0x4A91DD6E});
        Virtual<uint8_t>(MessageServer(), 0x14, Target(obj), uint32_t{0x4A92ECEF});
        At<uint8_t>(obj, field::kListening) = 1;
    }
    if (stale.begin)
        Free(stale.begin);
    if (wanted.size != 0) {
        char* node = At<char*>(wanted.header, 0x4);
        while (node) {
            reinterpret_cast<void(__thiscall*)(Tree*, char*)>(addr::cSC4Advisor_F40DAB0)(&wanted, At<char*>(node, 0xC));
            char* const left = At<char*>(node, 0x8);
            Free(node);
            node = left;
        }
        At<char*>(wanted.header, 0x8) = wanted.header;
        At<char*>(wanted.header, 0x4) = nullptr;
        At<char*>(wanted.header, 0xC) = wanted.header;
        wanted.size = 0;
    }
    if (wanted.header)
        Free(wanted.header);
    return 1;
}

// The game's 0x00410AF0, on the serialisable part at +0xC.
uint8_t __fastcall Read(char* part, void*, void* stream)
{
    char* const obj = part - 0xC;
    // The game reads one byte per record into the argument's own slot.
    std::uintptr_t slot = reinterpret_cast<std::uintptr_t>(stream);
    uint16_t version;
    Virtual<uint8_t>(stream, 0x1C, &version);
    if (version != 3)
        return 0;
    Virtual<uint8_t>(stream, 0x20, &At<uint32_t>(obj, field::kScriptArg));
    Virtual<uint8_t>(stream, 0x3C, obj + field::kCaption);
    Virtual<uint8_t>(stream, 0x3C, obj + field::kName);
    Virtual<uint8_t>(stream, 0x20, &At<uint32_t>(obj, field::kStamp));
    Virtual<uint8_t>(stream, 0x20, &At<uint32_t>(obj, field::kValue5C));
    Virtual<uint8_t>(stream, 0x20, &At<uint32_t>(obj, field::kId));
    Virtual<uint8_t>(stream, 0x20, &At<uint32_t>(obj, field::kValue64));
    uint32_t count;
    Record record;
    record.advice = nullptr;
    Virtual<uint8_t>(stream, 0x20, &count);
    using ReferenceFn = uint8_t(__cdecl*)(void*, uint32_t, void**, int32_t);
    const auto ReadReference = reinterpret_cast<ReferenceFn>(addr::SC4Stream_ReadReference);
    char* const records = obj + field::kRecords;
    while (count != 0) {
        --count;
        const int32_t index = static_cast<int32_t>(At<char*>(records, 0x4) - At<char*>(records, 0x0)) >> 4;
        if (record.advice) {
            void* const old = record.advice;
            record.advice = nullptr;
            Release(old);
        }
        ReadReference(stream, 0x8A09F5F5, &record.advice, 0);
        Virtual<uint8_t>(stream, 0x20, &record.b);
        Virtual<uint8_t>(stream, 0x20, &record.c);
        Virtual<uint8_t>(stream, 0x20, &record.a);
        PushRecord(records, &record, &slot);
        Virtual<uint8_t>(stream, 0x10, reinterpret_cast<uint8_t*>(&slot));
        if (static_cast<uint8_t>(slot))
            ActiveSlot(obj + field::kActive, &record) = index;
        int32_t key;
        Virtual<uint8_t>(stream, 0x20, &key);
        if (key != 0)
            InsertIndex(obj + field::kByKey, key, index);
    }
    uint32_t entries;
    Virtual<uint8_t>(stream, 0x20, &entries);
    while (entries != 0) {
        --entries;
        uint32_t value;
        Virtual<uint8_t>(stream, 0x20, &value);
        void* object = nullptr;
        ReadReference(stream, 0x6A26D050, &object, 0);
        void* const held = object;
        if (held)
            AddRef(held);
        char* const sentinel = At<char*>(obj, field::kList);
        char* const node = static_cast<char*>(Allocate(0x10));
        At<uint32_t>(node, 0x8) = value;
        At<void*>(node, 0xC) = held;
        if (held)
            AddRef(held);
        char* const before = At<char*>(sentinel, 0x4);
        At<char*>(node, 0x0) = sentinel;
        At<char*>(node, 0x4) = before;
        At<char*>(before, 0x0) = node;
        At<char*>(sentinel, 0x4) = node;
        if (held)
            Release(held);
        if (object)
            Release(object);
    }
    const uint8_t ok = Virtual<uint8_t>(stream, 0x48) == 0;
    if (record.advice)
        Release(record.advice);
    return ok;
}

// The game's 0x00410DD0: a new advisor, as its interface.
char* __cdecl NewAdvisor()
{
    char* const obj = static_cast<char*>(reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0xC0));
    if (!obj)
        return nullptr;
    reinterpret_cast<char*(__thiscall*)(void*)>(addr::SC4AgentBase_Construct)(obj);
    At<std::uintptr_t>(obj, 0x0) = addr::SC4AdvisorBase_Vtable;
    At<std::uintptr_t>(obj, 0xC) = addr::SC4AdvisorBase_VtableC;
    At<std::uintptr_t>(obj, 0x10) = addr::SC4AdvisorBase_Vtable10;
    At<uint8_t>(obj, field::kDirty) = 1;
    At<uint8_t>(obj, field::kListening2) = 0;
    At<uint32_t>(obj, field::kValueBC) = 0;
    At<std::uintptr_t>(obj, 0x8) = addr::SC4AdvisorBase_Vtable8;
    return obj + 0x8;
}

// The game's 0x00410E30, the advisor's Init on the interface at +8.
uint8_t __fastcall AdvisorInit(char* part, void*, int32_t which)
{
    if (!Init(part, nullptr, which))
        return 0;
    char* const obj = part - 0x8;
    const uint8_t listening = At<uint8_t>(obj, field::kListening2);
    At<uint8_t>(obj, field::kDirty) = 1;
    if (listening)
        return 1;
    At<uint8_t>(obj, field::kListening2) = 1;
    Virtual<uint8_t>(MessageServer(), 0x14, Target(obj), uint32_t{0x99EF1142});
    Virtual<uint8_t>(MessageServer(), 0x14, Target(obj), uint32_t{0x99EF1143});
    Virtual<uint8_t>(MessageServer(), 0x14, Target(obj), uint32_t{0x26D31EC1});
    Virtual<uint8_t>(MessageServer(), 0x14, Target(obj), uint32_t{0xEA8AE29A});
    Virtual<uint8_t>(MessageServer(), 0x14, Target(obj), uint32_t{0xC2C09554});
    return 1;
}

// The game's 0x00410F00: appends a text to a string's buffer.
void* __fastcall AppendText(char* buffer, void*, const char* text)
{
    const char* end = text;
    while (*end)
        ++end;
    using Fn = void*(__thiscall*)(void*, const char*, const char*, void*);
    return reinterpret_cast<Fn>(addr::cSC4Advisor_F40EBC0)(buffer, text, end, &text);
}

void* NewString(GameString* text)
{
    return reinterpret_cast<void*(__thiscall*)(GameString*)>(addr::SC4String_SC4String)(text);
}

void DestroyGameString(GameString* text)
{
    reinterpret_cast<void(__thiscall*)(GameString*)>(addr::SC4String_Destroy)(text);
}

// Formats into the string (its slot 27, cdecl) and appends it to `out`.
void AppendFormat(GameString* out, void* text, const char* format, std::uintptr_t value)
{
    using SprintfFn = void*(__cdecl*)(void*, const char*, ...);
    void* const made = (*static_cast<SprintfFn**>(text))[0x6C / 4](text, format, value);
    reinterpret_cast<void(__thiscall*)(GameString*, void*)>(addr::cRZBaseString_AppendString)(out, made);
}

// The record whose advice answers `id` (slots 10, 19 or 21), from `it`.
Record* FindRecord(char* obj, Record* it, uint32_t id)
{
    do {
        if (Virtual<uint32_t>(it->advice, 0x28) == id || Virtual<uint32_t>(it->advice, 0x4C) == id ||
            Virtual<uint32_t>(it->advice, 0x54) == id)
            return it;
        ++it;
    } while (it != At<Record*>(obj, field::kRecords + 0x4));
    return nullptr;
}

// Message 0x4A92ECEF: builds a description of the advice (and drops it).
void Describe(void* advice)
{
    GameString out;
    NewString(&out);
    Virtual<void>(advice, 0x70);
    reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(addr::cSC4Advisor_F40F1B0)(&out.begin, 0x800);
    AppendText(reinterpret_cast<char*>(&out.begin), nullptr, "------------------------------------------\n");
    {
        GameString line;
        void* const text = NewString(&line);
        AppendFormat(&out, text, "-- Advice %08x -----------------------\n", Virtual<uint32_t>(advice, 0x28));
        DestroyGameString(&line);
    }
    AppendText(reinterpret_cast<char*>(&out.begin), nullptr, "\t\t\t*Trigger*==\n");
    {
        void* const trigger = Virtual<void*>(advice, 0x24);
        GameString line;
        void* const text = NewString(&line);
        AppendFormat(&out, text, "\t%s\n", Virtual<std::uintptr_t>(trigger, 0x14));
        DestroyGameString(&line);
    }
    AppendText(reinterpret_cast<char*>(&out.begin), nullptr, "\t\t\t*Title*==\n");
    {
        void* const title = Virtual<void*>(advice, 0x48);
        GameString line;
        void* const text = NewString(&line);
        AppendFormat(&out, text, "\t%s\n", Virtual<std::uintptr_t>(title, 0x14));
        DestroyGameString(&line);
    }
    AppendText(reinterpret_cast<char*>(&out.begin), nullptr, "\t\t\t*Message*==\n");
    {
        void* const message = Virtual<void*>(advice, 0x50);
        GameString line;
        void* const text = NewString(&line);
        AppendFormat(&out, text, "\t%s\n\n", Virtual<std::uintptr_t>(message, 0x14));
        DestroyGameString(&line);
    }
    DestroyGameString(&out);
}

// Whether the advice (without flag 0x80) has a trigger with no "=(0)".
bool TriggerWithoutZero(void* advice, bool inlineDestroy)
{
    if (HasFlag(advice, 0x80))
        return false;
    void* const trigger = Virtual<void*>(advice, 0x24);
    GameString pattern;
    using FromTextFn = void*(__thiscall*)(GameString*, const char*);
    void* const text = reinterpret_cast<FromTextFn>(addr::SC4String_FromText)(&pattern, "=(0)");
    const bool none = Virtual<int32_t>(trigger, 0x5C, text, uint32_t{0}, uint32_t{1}) == -1;
    if (inlineDestroy)
        DestroyString(pattern);
    else
        DestroyGameString(&pattern);
    return none;
}

// Makes the record's advice active, with the given flags set.
void Activate(char* obj, Record* record, int32_t index, uint32_t flags)
{
    SetFlag(record->advice, flags, 1);
    record->c = At<uint32_t>(obj, field::kStamp);
    Virtual<void>(record->advice, 0x60, int32_t{0});
    ActiveSlot(obj + field::kActive, record) = index;
    reinterpret_cast<void(__thiscall*)(void*, Record*)>(addr::cSC4Advisor_F40DF20)(obj, record);
}

// Messages 0xEA917750 and 0x8A917759: the active advices are announced
// again, each with its own message 0x8A09FA64.
void Reannounce(char* obj)
{
    using EraseFn = void(__thiscall*)(void*, char*);
    const auto EraseSubtree = reinterpret_cast<EraseFn>(addr::cSC4Advisor_F40E050);
    char* const active = obj + field::kActive;
    Tree copy;
    reinterpret_cast<void*(__thiscall*)(Tree*, char*)>(addr::cSC4Advisor_F40F0C0)(&copy, active);
    if (At<uint32_t>(active, 0x4) != 0) {
        EraseSubtree(active, At<char*>(At<char*>(active, 0x0), 0x4));
        ResetTree(active);
    }
    Announce(obj, 0, 0x8A09FA67, 0);
    reinterpret_cast<void*(__thiscall*)(void*, Tree*)>(addr::cSC4Advisor_F40EB00)(active, &copy);
    char* node = At<char*>(At<char*>(active, 0x0), 0x8);
    if (node != At<char*>(active, 0x0)) {
        do {
            void* const advice = At<void*>(node, 0x10);
            SetFlag(advice, 2, 0);
            SetFlag(advice, 2, 1);
            void* subject = nullptr;
            if (Virtual<uint8_t>(advice, 0x0, uint32_t{1}, &subject)) {
                void* const memory = reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::RZ_operator_new)(0x2C);
                void* const message =
                    memory ? reinterpret_cast<void*(__thiscall*)(void*)>(addr::cGZMessage_cGZMessage)(memory) : nullptr;
                if (message)
                    AddRef(message);
                Virtual<void>(message, 0x14, uint32_t{0x8A09FA64});
                Virtual<void>(message, 0x84, subject);
                Virtual<void>(message, 0x2C, int32_t{0});
                Virtual<void>(MessageServer(), 0xC, message);
                Release(message);
            }
            if (subject)
                Release(subject);
            node = Next(node);
        } while (node != At<char*>(active, 0x0));
    }
    if (copy.size != 0) {
        EraseSubtree(&copy, At<char*>(copy.header, 0x4));
        At<char*>(copy.header, 0x8) = copy.header;
        At<char*>(copy.header, 0x4) = nullptr;
        At<char*>(copy.header, 0xC) = copy.header;
        copy.size = 0;
    }
    if (copy.header)
        Free(copy.header);
}

// The game's 0x00410F30, on the message target at +0x10.
uint8_t __fastcall AgentDoMessage(char* target, void*, void* message)
{
    char* const obj = target - 0x10;
    switch (Virtual<uint32_t>(message, 0x10)) {
    case 0x4A92ECEF: {
        const uint32_t id = Virtual<uint32_t>(message, 0x24);
        Record* const begin = At<Record*>(obj, field::kRecords);
        if (begin == At<Record*>(obj, field::kRecords + 0x4))
            break;
        Record* const record = FindRecord(obj, begin, id);
        if (!record || !record->advice)
            break;
        Describe(record->advice);
        break;
    }
    case 0x4A91DD6E: {
        const uint32_t id = Virtual<uint32_t>(message, 0x24);
        Record* it = At<Record*>(obj, field::kRecords);
        Record* const end = At<Record*>(obj, field::kRecords + 0x4);
        if (id != At<uint32_t>(obj, field::kId)) {
            if (it == end)
                break;
            it = FindRecord(obj, it, id);
            if (!it)
                break;
            void* const advice = it->advice;
            const int32_t index =
                static_cast<int32_t>(reinterpret_cast<char*>(it) - At<char*>(obj, field::kRecords)) >> 4;
            if (!advice)
                break;
            if (HasFlag(advice, 2))
                break;
            Record* const record = At<Record*>(obj, field::kRecords) + index;
            if (!TriggerWithoutZero(advice, false))
                break;
            Activate(obj, record, index, 0x600);
            break;
        }
        if (it != end) {
            do {
                void* const advice = it->advice;
                const int32_t index =
                    static_cast<int32_t>(reinterpret_cast<char*>(it) - At<char*>(obj, field::kRecords)) >> 4;
                if (advice && !HasFlag(advice, 2)) {
                    Record* const record = At<Record*>(obj, field::kRecords) + index;
                    if (TriggerWithoutZero(advice, true))
                        Activate(obj, record, index, 0x400);
                }
                ++it;
            } while (it != At<Record*>(obj, field::kRecords + 0x4));
        }
        using PostFn = void(__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
        reinterpret_cast<PostFn>(addr::SC4_PostMessage)(0xEA917750, 1, 0, 0, 0, 0);
        break;
    }
    case 0xEA917750: {
        // The game pushes the byte's whole stack slot; its upper three
        // bytes are whatever the frame held.
        const uint32_t on = Virtual<uint32_t>(message, 0x24) != 0;
        Record* it = At<Record*>(obj, field::kRecords);
        if (it != At<Record*>(obj, field::kRecords + 0x4)) {
            do {
                SetFlag(it->advice, 0x200, on);
                ++it;
            } while (it != At<Record*>(obj, field::kRecords + 0x4));
        }
        Reannounce(obj);
        break;
    }
    case 0x8A917759:
        Reannounce(obj);
        break;
    default:
        break;
    }
    return 1;
}

void RequestUpdate(char* obj, uint32_t a, uint32_t b)
{
    reinterpret_cast<void(__thiscall*)(void*, uint32_t, uint32_t)>(addr::cSC4Advisor_RequestUpdate)(obj, a, b);
}

// The game's 0x00411570, the advisor's own on the message target at
// +0x10; every message goes on to the base's handler.
uint8_t __fastcall AdvisorDoMessage(char* target, void*, void* message)
{
    char* const obj = target - 0x10;
    switch (Virtual<uint32_t>(message, 0x10)) {
    case 0xC2C09554:
        RequestUpdate(obj, 2, 0);
        break;
    case 0x99EF1142:
    case 0x99EF1143: {
        void* const subject = Virtual<void*>(message, 0x28);
        if (Virtual<uint32_t>(subject, 0x1C) != 0x278128A0)
            break;
        void* const holder = Virtual<void*>(subject, 0x18);
        uint8_t flag = 0;
        using BoolFn = uint8_t(__cdecl*)(void*, uint32_t, uint8_t*);
        reinterpret_cast<BoolFn>(addr::SC4_GetPropertyBool)(holder, 0xEA3209F8, &flag);
        if (!flag)
            break;
        void* reference = nullptr;
        uint32_t where[3] = {0, 0, 0};
        void** const out = reinterpret_cast<void**(__thiscall*)(void**)>(addr::SC4RefPtr_Reset)(&reference);
        if (Virtual<uint8_t>(subject, 0x0, uint32_t{0x87DDA3A9}, out)) {
            if (void* const service = Global<void*>(addr::SC4_Service43D68)) {
                void* const got = Virtual<void*>(reference, 0x10);
                if (Virtual<uint8_t>(service, 0x38, got, &where[0]))
                    RequestUpdate(obj, 1, where[2]);
            }
        }
        if (reference)
            Release(reference);
        break;
    }
    case 0xCA8F3DB6:
        if (Virtual<uint32_t>(message, 0x24) == 1) {
            const uint32_t id = Virtual<uint32_t>(message, 0x38);
            void* const advice = Virtual<void*>(obj + 0x8, 0x30, id);
            if (!advice)
                break;
            Virtual<void>(advice, 0x20);
            const uint8_t* const entry = Virtual<const uint8_t*>(Global<void*>(addr::SC4_Service43DB4), 0x18, id);
            if (entry && (*entry & 2))
                break;
            SetFlag(advice, 8, 1);
            At<uint8_t>(obj, field::kDirty) = 1;
        } else if (Virtual<uint32_t>(message, 0x24) == 2) {
            Record* it = At<Record*>(obj, field::kRecords);
            Record* const end = At<Record*>(obj, field::kRecords + 0x4);
            while (it != end) {
                void* const advice = it->advice;
                ++it;
                if (advice && HasFlag(advice, 0x1000))
                    Virtual<void>(advice, 0x20);
            }
        }
        break;
    case 0xEA8AE29A: {
        int32_t left =
            static_cast<int32_t>(At<char*>(obj, field::kRecords + 0x4) - At<char*>(obj, field::kRecords)) >> 4;
        if (left <= 0)
            break;
        for (int32_t i = 0; left != 0; ++i, --left) {
            Record* const record = At<Record*>(obj, field::kRecords) + i;
            void* kind = nullptr;
            if (Virtual<uint8_t>(record->advice, 0x0, uint32_t{0xE4FDA3D4}, &kind)) {
                if (Virtual<uint32_t>(kind, 0x14) == 0xAA371C32)
                    Virtual<void>(record->advice, 0x20);
            }
            if (kind)
                Release(kind);
        }
        break;
    }
    case 0x26D31EC1:
        Virtual<void>(obj, 0x24);
        break;
    default:
        break;
    }
    return AgentDoMessage(target, nullptr, message);
}

bool InstallBase3(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[10] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSC4Advisor_F410030, reinterpret_cast<void*>(&AdvisorDelete)},
        {addr::cSC4Advisor_F410070, reinterpret_cast<void*>(&RecordsPushBack)},
        {addr::cSC4Advisor_F4100D0, reinterpret_cast<void*>(&NewAgentBase)},
        {addr::SC4AdvisorBase_Init, reinterpret_cast<void*>(&Init)},
        {addr::SC4AgentBase_Read, reinterpret_cast<void*>(&Read)},
        {addr::cSC4Advisor_F410DD0, reinterpret_cast<void*>(&NewAdvisor)},
        {addr::cSC4Advisor_F410E30, reinterpret_cast<void*>(&AdvisorInit)},
        {addr::SC4AgentBase_DoMessage, reinterpret_cast<void*>(&AgentDoMessage)},
        {addr::cSC4Advisor_DoMessage, reinterpret_cast<void*>(&AdvisorDoMessage)},
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

const bool g_base3Registered = registry::Register({
    "scripts.advisors.base3",
    "scripts/advisors",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4AdvisorBase_Init,
    "Our advisor base class, third part: Init, Read and the message handlers (docs/lua.md)",
    false,
    InstallBase3,
});

} // namespace
} // namespace opensc4::advisor_base
