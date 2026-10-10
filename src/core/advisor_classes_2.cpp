// The advisor classes derived from the advisor base, second part (functions
// between 0x00417600 and 0x00422FF0). Replacement, off by default:
//
//   scripts.advisors.classes2: the daylight saving bounds, the localised
//       string's key from a text, the "%xx" decoder of a link, the advisor
//       class 0x4A3AD3E1 (a set at +0xC0), the advisor class 0xCA2C2A2F (its
//       advice has the script field "nd_type") and that advice (class
//       0x6A3848DB).
//
// An advisor: main vtable at +0, the advisor interface at +8, the
// serialisable part at +0xC, the message target at +0x10. The advice: main
// vtable at +0, a second one at +4, the serialisable part at +0xC. The base
// classes' functions stay where they are and are called by address.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <utility>

namespace opensc4::advisor_classes {
namespace {

namespace field { // from an advisor's start
constexpr std::size_t kTicks = 0x58;     // copied to a record's +0xC
constexpr std::size_t kTriggers = 0x6C;  // multimap of event type to record index
constexpr std::size_t kActive = 0x78;    // map of record to record index
constexpr std::size_t kRecords = 0x84;   // vector of Record
constexpr std::size_t kEvents = 0xB0;    // list of (type, object)
constexpr std::size_t kDirty = 0xB8;
constexpr std::size_t kSet = 0xC0;       // class 0x4A3AD3E1: a set, header and count
constexpr std::size_t kSetFlag = 0xCC;   // class 0x4A3AD3E1: listening
constexpr std::size_t kDealFlag = 0xC0;  // class 0xCA2C2A2F: listening
} // namespace field

namespace advice { // from the advice's start
constexpr std::size_t kScriptIndex = 0x88;
constexpr std::size_t kDealType = 0xC0; // the script field "nd_type"
} // namespace advice

struct Record {
    void* advice;
    uint32_t a;
    uint32_t b;
    uint32_t c;
};

struct TreeNode {
    uint8_t color;
    TreeNode* parent;
    TreeNode* left;
    TreeNode* right;
    int32_t key;
    int32_t value;
};

struct Tree {
    TreeNode* header;
    uint32_t count;
};

struct ListNode {
    ListNode* next;
    ListNode* previous;
    int32_t type;
    void* object;
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

struct Date {
    std::uintptr_t vtable;
    uint32_t day;
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

void* MessageServer()
{
    return Global<void*>(addr::SC4_MessageServer);
}

// The message target of the object, as the game's casts work it out.
char* Target(char* obj)
{
    return obj ? obj + 0x10 : nullptr;
}

// The in-order successor of a tree node (the header after the last).
TreeNode* Next(TreeNode* node)
{
    if (TreeNode* right = node->right) {
        while (TreeNode* left = right->left)
            right = left;
        return right;
    }
    TreeNode* parent = node->parent;
    if (node == parent->right) {
        do {
            node = parent;
            parent = parent->parent;
        } while (node == parent->right);
    }
    if (node->right != parent)
        node = parent;
    return node;
}

// The game's 0x00417600: the second at which daylight saving starts in a
// year, into *out.
uint32_t* __cdecl DaylightStart(uint32_t* out, uint32_t year)
{
    using ConstructFn = Date*(__thiscall*)(Date*, uint32_t, uint32_t, uint32_t);
    using BackFn = Date*(__thiscall*)(Date*, Date*, uint32_t);
    const auto construct = reinterpret_cast<ConstructFn>(addr::SC4Date_Construct);
    const auto back = reinterpret_cast<BackFn>(addr::SC4Date_BackToWeekday);
    Date first;
    Date found;
    construct(&first, 4, 8, year);
    const Date* day;
    if (year > 0x7C2)
        day = back(&first, &found, 7);
    else if (year == 0x7B6)
        day = construct(&found, 1, 6, year);
    else if (year == 0x7B7)
        day = construct(&found, 2, 0x17, year);
    else
        day = back(&first, &found, 7);
    *out = (day->day - Global<uint32_t>(addr::SC4Date_EpochDay)) * 0x15180 + Global<uint32_t>(addr::SC4_Timezone) +
           0x1C20;
    return out;
}

// The game's 0x004176E0: the second at which daylight saving ends in a year,
// into *out.
uint32_t* __cdecl DaylightEnd(uint32_t* out, uint32_t year)
{
    using ConstructFn = Date*(__thiscall*)(Date*, uint32_t, uint32_t, uint32_t);
    using BackFn = Date*(__thiscall*)(Date*, Date*, uint32_t);
    Date last;
    Date found;
    reinterpret_cast<ConstructFn>(addr::SC4Date_Construct)(&last, 0xA, 0x1F, year);
    const Date* const day = reinterpret_cast<BackFn>(addr::SC4Date_BackToWeekday)(&last, &found, 7);
    *out = (day->day - Global<uint32_t>(addr::SC4Date_EpochDay)) * 0x15180 + Global<uint32_t>(addr::SC4_Timezone) +
           0xE10;
    return out;
}

// The game's 0x004177B0: the two arguments combined with exclusive or. Its
// callers are direct calls, so it touches no register but EAX.
__declspec(naked) void XorPair()
{
    __asm mov eax, dword ptr [esp + 4]
    __asm xor eax, dword ptr [esp + 8]
    __asm ret
}

// The game's 0x004184B0: copies a string, turning each "%xx" with two
// hexadecimal digits into the character the game's text to number function
// makes of the two.
uint8_t __cdecl DecodePercent(GameString* source, GameString* out)
{
    // The original passes the address of its first argument's slot where the
    // string helpers take an allocator.
    GameString* tag = source;
    GameString text;
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, std::size_t{8});
    text.capacity = buffer + 8;
    *buffer = 0;
    const int32_t length = static_cast<int32_t>(source->end - source->begin);
    text.begin = buffer;
    text.end = buffer;
    text.vtable = addr::SC4String_Vtable;
    text.zero = 0;
    if (length > 0) {
        int32_t ahead = 2;
        int32_t i = 0;
        do {
            const char c = source->begin[i];
            bool decoded = false;
            if (c == '%' && ahead < length) {
                char digits[3];
                digits[0] = source->begin[i + 1];
                digits[1] = source->begin[i + 2];
                digits[2] = 0;
                if (Cdecl<int>(addr::SC4_isxdigit, static_cast<int32_t>(static_cast<int8_t>(digits[0]))) &&
                    Cdecl<int>(addr::SC4_isxdigit, static_cast<int32_t>(static_cast<int8_t>(digits[1])))) {
                    const uint8_t value = static_cast<uint8_t>(Cdecl<int32_t>(addr::SC4_TextToInt, &digits[0]));
                    This<void>(addr::SC4String_PushBack, &text.begin, static_cast<uint32_t>(value));
                    i += 2;
                    ahead += 2;
                    decoded = true;
                }
            }
            if (!decoded) {
                if (text.end + 1 == text.capacity) {
                    const uint32_t size = static_cast<uint32_t>(text.end - text.begin);
                    const uint32_t grow = size < 1 ? 1 : size;
                    This<void>(addr::cSC4Advisor_F40F1B0, &text.begin, grow + size);
                }
                text.end[1] = 0;
                text.end[0] = c;
                ++text.end;
            }
            ++i;
            ++ahead;
        } while (i < length);
    }
    if (&text.begin != &out->begin)
        This<void>(addr::SC4String_AssignRange, &out->begin, text.begin, text.end, &tag);
    if (text.begin)
        Free(text.begin);
    return 1;
}

// The game's 0x0041B7F0: the class ID of the advisor with the set.
__declspec(naked) void SetAdvisorClassId()
{
    __asm mov eax, 0x4A3AD3E1
    __asm ret
}

// The game's 0x0041B800: the set advisor's Init (on the advisor interface):
// listens for message 0x4C04DE69, then the advisor's Init.
uint8_t __fastcall SetAdvisorInit(char* self, void*, uint32_t argument)
{
    char* const obj = self - 0x8;
    if (!At<uint8_t>(obj, field::kSetFlag)) {
        Virtual<uint8_t>(MessageServer(), 0x14, Target(obj), uint32_t{0x4C04DE69});
        At<uint8_t>(obj, field::kSetFlag) = 1;
    }
    At<uint8_t>(obj, field::kDirty) = 1;
    return This<uint8_t>(addr::cSC4Advisor_F410E30, self, argument);
}

// The game's 0x0041B850: the set advisor's Shutdown (on the advisor
// interface).
uint32_t __fastcall SetAdvisorShutdown(char* self, void*)
{
    char* const obj = self - 0x8;
    if (At<uint8_t>(obj, field::kSetFlag)) {
        Virtual<uint8_t>(MessageServer(), 0x18, Target(obj), uint32_t{0x4C04DE69});
        At<uint8_t>(obj, field::kSetFlag) = 0;
    }
    return This<uint32_t>(addr::cSC4Advisor_F40EF30, self);
}

// The game's 0x0041B890: frees a subtree of the set.
void __fastcall SetEraseSubtree(Tree* self, void*, TreeNode* node)
{
    while (node) {
        SetEraseSubtree(self, nullptr, node->right);
        TreeNode* const left = node->left;
        Free(node);
        node = left;
    }
}

void SetClear(Tree* self)
{
    SetEraseSubtree(self, nullptr, self->header->parent);
    self->header->left = self->header;
    self->header->parent = nullptr;
    self->header->right = self->header;
    self->count = 0;
}

// The game's 0x0041BE50: the set's destructor.
void __fastcall SetDestroy(Tree* self, void*)
{
    if (self->count)
        SetClear(self);
    if (TreeNode* const header = self->header)
        Free(header);
}

// The game's 0x0041BEA0: erases the set's nodes from first up to last.
void __fastcall SetEraseRange(Tree* self, void*, TreeNode* first, TreeNode* last)
{
    TreeNode* const header = self->header;
    if (first == header->left && last == header) {
        if (self->count)
            SetClear(self);
        return;
    }
    while (first != last) {
        TreeNode* const node = first;
        first = Next(first);
        TreeNode* const now = self->header;
        using Fn = TreeNode*(__cdecl*)(TreeNode*, TreeNode**, TreeNode**, TreeNode**);
        TreeNode* const freed =
            reinterpret_cast<Fn>(addr::SC4Tree_RebalanceForErase)(node, &now->parent, &now->left, &now->right);
        if (freed)
            Free(freed);
        --self->count;
    }
}

// The game's 0x0041BF60: the set advisor's destructor.
void __fastcall SetAdvisorDestruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4SetAdvisor_Vtable;
    At<std::uintptr_t>(self, 0x8) = addr::SC4SetAdvisor_Vtable8;
    At<std::uintptr_t>(self, 0xC) = addr::SC4SetAdvisor_VtableC;
    At<std::uintptr_t>(self, 0x10) = addr::SC4SetAdvisor_Vtable10;
    SetDestroy(reinterpret_cast<Tree*>(self + field::kSet), nullptr);
    This<void>(addr::cSC4Advisor_F40FCC0, self);
}

// The game's 0x0041C080: the set advisor's constructor.
char* __fastcall SetAdvisorConstruct(char* self, void*)
{
    This<void*>(addr::cSC4Advisor_F40FFF0, self);
    At<std::uintptr_t>(self, 0x0) = addr::SC4SetAdvisor_Vtable;
    At<std::uintptr_t>(self, 0x8) = addr::SC4SetAdvisor_Vtable8;
    At<std::uintptr_t>(self, 0xC) = addr::SC4SetAdvisor_VtableC;
    At<std::uintptr_t>(self, 0x10) = addr::SC4SetAdvisor_Vtable10;
    Tree& set = At<Tree>(self, field::kSet);
    set.header = nullptr;
    set.header = Cdecl<TreeNode*>(addr::SC4_stl_allocate, std::size_t{0x14});
    set.count = 0;
    set.header->color = 0;
    set.header->parent = nullptr;
    set.header->left = set.header;
    set.header->right = set.header;
    At<uint8_t>(self, field::kSetFlag) = 0;
    return self;
}

// The game's 0x0041C0F0: the set advisor's scalar deleting destructor.
char* __fastcall SetAdvisorDelete(char* self, void*, uint32_t flags)
{
    SetAdvisorDestruct(self, nullptr);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// The game's 0x0041C170: makes a set advisor; its advisor interface comes
// back.
char* __cdecl NewSetAdvisor()
{
    char* const memory = Cdecl<char*>(addr::SC4_operator_new, std::size_t{0xD0});
    if (memory) {
        if (char* const obj = SetAdvisorConstruct(memory, nullptr))
            return obj + 0x8;
    }
    return nullptr;
}

// The game's 0x00420D30: the class ID of the deal advisor.
__declspec(naked) void DealAdvisorClassId()
{
    __asm mov eax, 0xCA2C2A2F
    __asm ret
}

// The game's 0x00420D40: the class ID of the deal advice.
__declspec(naked) void DealAdviceClassId()
{
    __asm mov eax, 0x6A3848DB
    __asm ret
}

// The game's 0x00420D60: the deal advice's QueryInterface.
uint8_t __fastcall DealAdviceQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    if (iid != 0xEA3848BA)
        return This<uint8_t>(addr::SC4Advice_QueryInterface, self, iid, out);
    *out = self;
    AddRef(self);
    return 1;
}

// The game's 0x00420D90: the deal advice's type.
uint32_t __fastcall DealAdviceGetType(char* self, void*)
{
    return At<uint32_t>(self, advice::kDealType);
}

// The game's 0x00420DA0: QueryInterface from the advice's part at +4.
uint8_t __fastcall DealAdviceQueryInterface4(char* self, void*, uint32_t iid, void** out)
{
    return DealAdviceQueryInterface(self - 0x4, nullptr, iid, out);
}

// The game's 0x00420DC0: QueryInterface from the advice's part at +0xC.
uint8_t __fastcall DealAdviceQueryInterfaceC(char* self, void*, uint32_t iid, void** out)
{
    return DealAdviceQueryInterface(self - 0xC, nullptr, iid, out);
}

// The game's 0x00420DD0: makes a deal advisor; its advisor interface comes
// back.
char* __cdecl NewDealAdvisor()
{
    char* const obj = Cdecl<char*>(addr::SC4_operator_new, std::size_t{0xC4});
    if (!obj)
        return nullptr;
    This<void*>(addr::cSC4Advisor_F40FFF0, obj);
    At<std::uintptr_t>(obj, 0x0) = addr::SC4DealAdvisor_Vtable;
    At<std::uintptr_t>(obj, 0xC) = addr::SC4DealAdvisor_VtableC;
    At<std::uintptr_t>(obj, 0x10) = addr::SC4DealAdvisor_Vtable10;
    At<uint8_t>(obj, field::kDealFlag) = 0;
    At<std::uintptr_t>(obj, 0x8) = addr::SC4DealAdvisor_Vtable8;
    return obj + 0x8;
}

// The game's 0x00420E20: the deal advisor's scalar deleting destructor.
char* __fastcall DealAdvisorDelete(char* self, void*, uint32_t flags)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4DealAdvisor_Vtable;
    At<std::uintptr_t>(self, 0x8) = addr::SC4DealAdvisor_Vtable8;
    At<std::uintptr_t>(self, 0xC) = addr::SC4DealAdvisor_VtableC;
    At<std::uintptr_t>(self, 0x10) = addr::SC4DealAdvisor_Vtable10;
    This<void>(addr::cSC4Advisor_F40FCC0, self);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// The game's 0x00420E60: makes a deal advice.
char* __cdecl NewDealAdvice()
{
    char* const obj = Cdecl<char*>(addr::SC4_operator_new, std::size_t{0xC4});
    if (!obj)
        return nullptr;
    This<void*>(addr::SC4Advice_Construct, obj);
    At<std::uintptr_t>(obj, 0x0) = addr::SC4DealAdvice_Vtable;
    At<std::uintptr_t>(obj, 0x4) = addr::SC4DealAdvice_Vtable4;
    At<std::uintptr_t>(obj, 0xC) = addr::SC4DealAdvice_VtableC;
    return obj;
}

// The game's 0x00420EA0: the deal advice's scalar deleting destructor.
char* __fastcall DealAdviceDelete(char* self, void*, uint32_t flags)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4DealAdvice_Vtable;
    At<std::uintptr_t>(self, 0x4) = addr::SC4DealAdvice_Vtable4;
    At<std::uintptr_t>(self, 0xC) = addr::SC4DealAdvice_VtableC;
    This<void>(addr::SC4Advice_Destruct, self);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// The game's 0x00420DB0: the deleting destructor from the advice's part at
// +4.
char* __fastcall DealAdviceDelete4(char* self, void*, uint32_t flags)
{
    return DealAdviceDelete(self - 0x4, nullptr, flags);
}

// The game's 0x00420EE0: a copy of the deal advice into *out, with its
// type. The original uses the copy's interface without checking that the
// query gave one.
uint8_t __fastcall DealAdviceCopy(char* self, void*, void** out)
{
    if (!This<uint8_t>(addr::SC4Advice_Copy, self, out))
        return 0;
    void* const copy = *out;
    void* deal = nullptr;
    Virtual<uint8_t>(copy, 0x0, uint32_t{0xEA3848BA}, &deal);
    void* const target = deal;
    Virtual<void>(target, 0x80, Virtual<uint32_t>(self, 0x7C));
    if (deal)
        Release(deal);
    return 1;
}

// The game's 0x00420F40: the deal advice's Write (on the serialisable part
// at +0xC).
uint32_t __fastcall DealAdviceWrite(char* self, void*, void* stream)
{
    Virtual<void>(stream, 0x1C, uint32_t{2});
    This<void>(addr::SC4Advice_Write, self, stream);
    Virtual<void>(stream, 0x20, At<uint32_t>(self, advice::kDealType - 0xC));
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// The game's 0x00420F80: the deal advice's Read (on the serialisable part at
// +0xC). The version is read into the stream argument's own slot.
uint32_t __fastcall DealAdviceRead(char* self, void*, void* stream)
{
    uint32_t version = reinterpret_cast<uint32_t>(stream);
    Virtual<void>(stream, 0x1C, &version);
    if (static_cast<uint16_t>(version) != 2)
        return 0;
    This<void>(addr::SC4Advice_Read, self, stream);
    Virtual<void>(stream, 0x20, &At<uint32_t>(self, advice::kDealType - 0xC));
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// The game's 0x004217C0: the deal advice's Init from its script table: the
// base's, then the field "nd_type" of its entry in the advices table.
uint8_t __fastcall DealAdviceInit(char* self, void*, uint32_t index)
{
    const uint8_t result = This<uint8_t>(addr::SC4Advice_Init, self, index);
    void* const context = Virtual<void*>(Global<void*>(addr::cSC4AdvisorSystem_Instance), 0x28);
    ScriptTable table;
    This<void>(addr::SC4ScriptTable_Open, &table, context, Global<const char*>(addr::SC4ScriptNames_Advices2),
               At<uint32_t>(self, advice::kScriptIndex));
    if (!table.open) {
        if (table.text.begin)
            Free(table.text.begin);
        return 0;
    }
    void* const script = table.script;
    Virtual<void>(script, 0x84, "nd_type");
    Virtual<void>(script, 0x98, int32_t{-2});
    const uint32_t type = Virtual<uint32_t>(script, 0x58, int32_t{-1});
    Virtual<void>(script, 0x2C, int32_t{1});
    At<uint32_t>(self, advice::kDealType) = type;
    Virtual<void>(script, 0x1C, table.top);
    if (table.text.begin)
        Free(table.text.begin);
    return result;
}

// The game's 0x00421880: the callback the deal advisor registers for the
// names in an advice's text: (name, out, subject). The subject's slot 20
// object must be a deal advice whose slot 25 object has a slot 8 object with
// interface 0x0773FEF1; the lower-cased name then picks one of three texts.
uint8_t __cdecl DealToken(void* name, void* out, void* subject)
{
    uint8_t result = 0;
    // The original passes the address of the name's slot as the allocator
    // and keeps the second reference in that slot afterwards.
    void* tag = name;
    const uint32_t length = Virtual<uint32_t>(name, 0x1C);
    char* const data = Virtual<char*>(name, 0x18);
    GameString text;
    text.begin = nullptr;
    text.end = nullptr;
    text.capacity = nullptr;
    This<void>(addr::SC4String_FromRange, &text.begin, data, data + length, &tag);
    text.vtable = addr::SC4String_Vtable;
    text.zero = 0;
    This<void>(addr::cRZBaseString_MakeLower, &text);
    void* const money = Virtual<void*>(Cdecl<void*>(addr::SC4_CurrentApp), 0x98, int32_t{0});

    void* deal = nullptr;
    void* party = nullptr;
    bool found = false;
    bool queried = false;
    if (void* const object = Virtual<void*>(subject, 0x50)) {
        if (Virtual<uint8_t>(object, 0x0, uint32_t{0xEA3848BA}, &deal)) {
            queried = true;
            void* const holder = Virtual<void*>(object, 0x64);
            if (holder && Virtual<uint32_t>(holder, 0x20)) {
                void* const who = Virtual<void*>(holder, 0x20);
                if (Virtual<uint8_t>(who, 0x0, uint32_t{0x0773FEF1}, &party))
                    found = true;
            }
        }
    }
    if (!found) {
        if (queried && party)
            Release(party);
        if (deal)
            Release(deal);
        text.vtable = addr::SC4String_Vtable;
        if (text.begin)
            Free(text.begin);
        return 0;
    }

    using EqualFn = uint8_t(__thiscall*)(void*, std::uintptr_t, uint32_t);
    const auto equal = reinterpret_cast<EqualFn>(addr::cRZBaseString_IsEqual);
    if (equal(&text, addr::SC4DealAdvisor_TokenName1, 0)) {
        if (void* const source = Virtual<void*>(party, 0x1C)) {
            Virtual<void>(source, 0x84, out);
            result = 1;
        }
    } else if (equal(&text, addr::SC4DealAdvisor_TokenName2, 0)) {
        const int32_t amount = Virtual<int32_t>(party, 0x28);
        Virtual<void>(money, 0x48, static_cast<int64_t>(amount), out);
        result = 1;
    } else if (equal(&text, addr::SC4DealAdvisor_TokenName3, 0)) {
        const int64_t amount = Virtual<int64_t>(party, 0x58);
        Cdecl<void>(addr::SC4_FormatMoney, amount, out);
        result = 1;
    }
    if (party)
        Release(party);
    if (deal)
        Release(deal);
    This<void>(addr::SC4String_Destroy, &text);
    return result;
}

// Adds or removes the deal advisor's token callback (the service's slot 7:
// callback, context, on): the context is the city's slot 81 value. The service and its vtable are read before the city is
// asked, as in the original.
void RegisterDealToken(uint32_t on)
{
    void* const city = Global<void*>(addr::SC4_City);
    void* const service = Global<void*>(addr::SC4_Service43CDC);
    void** const vtable = *static_cast<void***>(service);
    const uint32_t value = Virtual<uint32_t>(city, 0x144);
    using Fn = void(__thiscall*)(void*, std::uintptr_t, uint32_t, uint32_t);
    reinterpret_cast<Fn>(vtable[0x1C / sizeof(void*)])(service, addr::cSC4Advisors_F421880, value, on);
}

// The game's 0x004229C0: the deal advisor's Init (on the advisor interface).
uint8_t __fastcall DealAdvisorInit(char* self, void*, uint32_t argument)
{
    if (!This<uint8_t>(addr::cSC4Advisor_F410E30, self, argument))
        return 0;
    char* const obj = self - 0x8;
    const uint8_t listening = At<uint8_t>(obj, field::kDealFlag);
    At<uint8_t>(obj, field::kDirty) = 1;
    if (listening)
        return 1;
    Virtual<uint8_t>(MessageServer(), 0x14, Target(obj), uint32_t{0xEA418D8F});
    Virtual<uint8_t>(MessageServer(), 0x14, Target(obj), uint32_t{0xCA358F85});
    Virtual<uint8_t>(MessageServer(), 0x14, Target(obj), uint32_t{0xCA358F9C});
    void* const list = Virtual<void*>(Global<void*>(addr::SC4_Service43C94), 0x90);
    Virtual<void>(list, 0x98, Virtual<uint32_t>(self, 0x20));
    RegisterDealToken(1);
    At<uint8_t>(obj, field::kDealFlag) = 1;
    return 1;
}

// The game's 0x00422AA0: the deal advisor's Shutdown (on the advisor
// interface).
uint32_t __fastcall DealAdvisorShutdown(char* self, void*)
{
    char* const obj = self - 0x8;
    if (At<uint8_t>(obj, field::kDealFlag)) {
        Virtual<uint8_t>(MessageServer(), 0x18, Target(obj), uint32_t{0xEA418D8F});
        Virtual<uint8_t>(MessageServer(), 0x18, Target(obj), uint32_t{0xCA358F85});
        Virtual<uint8_t>(MessageServer(), 0x18, Target(obj), uint32_t{0xCA358F9C});
        RegisterDealToken(0);
        At<uint8_t>(obj, field::kDealFlag) = 0;
    }
    return This<uint32_t>(addr::cSC4Advisor_F40EF30, self);
}

// Starts a record's advice for an event's object, when the advice agrees
// (slot 8).
void StartFor(char* self, Record* record, int32_t index, void* subject)
{
    int32_t* const slot = This<int32_t*>(addr::cSC4Advisor_F40EFF0, self + field::kActive, record);
    *slot = index;
    Virtual<void>(record->advice, 0x60, subject);
    This<void>(addr::cSC4Advisor_F40DF20, self, record);
}

// The game's 0x00422B40: the deal advisor's turn at its queued events (as
// many as are queued on entry). For each, the records triggered by the
// event's type: an ordinary event starts every idle one and renews every
// running one; one of the four deal events goes to the one record whose deal
// type is the event's party's, and when none of that type takes it, a copy
// of the last running advice of that type is added as a new record and
// started.
void __fastcall DealAdvisorEvents(char* self, void*)
{
    int32_t left = 0;
    {
        ListNode* const head = At<ListNode*>(self, field::kEvents);
        for (ListNode* node = head->next; node != head; node = node->next)
            ++left;
    }
    if (!left)
        return;
    do {
        ListNode* const event = At<ListNode*>(self, field::kEvents)->next;
        const int32_t type = event->type;
        uint8_t handled = 0;
        const uint8_t deals = type == 0x4A383049 || type == static_cast<int32_t>(0xAA383052) ||
                              type == 0x3A383059 || type == 0x0A383053;
        TreeNode* lower = At<TreeNode*>(self, field::kTriggers);
        for (TreeNode* node = lower->parent; node;) {
            if (node->key < type) {
                node = node->right;
            } else {
                lower = node;
                node = node->left;
            }
        }
        TreeNode* upper = At<TreeNode*>(self, field::kTriggers);
        for (TreeNode* node = upper->parent; node;) {
            if (type < node->key) {
                upper = node;
                node = node->left;
            } else {
                node = node->right;
            }
        }
        void* const subject = event->object;
        void* kept = nullptr;
        for (TreeNode* it = lower; it != upper; it = Next(it)) {
            const int32_t index = it->value;
            Record* const record = At<Record*>(self, field::kRecords) + index;
            if (!deals) {
                if (!Virtual<uint8_t>(record->advice, 0x3C, uint32_t{2})) {
                    record->c = At<uint32_t>(self, field::kTicks);
                    if (Virtual<uint8_t>(record->advice, 0x20))
                        StartFor(self, record, index, subject);
                } else {
                    void* const before = Virtual<void*>(record->advice, 0x64);
                    if (before)
                        AddRef(before);
                    Virtual<void>(record->advice, 0x38, uint32_t{0x10}, uint32_t{1});
                    Virtual<void>(record->advice, 0x60, subject);
                    if (!Virtual<uint8_t>(record->advice, 0x20))
                        Virtual<void>(record->advice, 0x60, before);
                    if (before)
                        Release(before);
                }
                continue;
            }
            if (handled)
                continue;
            void* party = nullptr;
            void* deal = nullptr;
            // Not checked in the original either.
            Virtual<uint8_t>(record->advice, 0x0, uint32_t{0xEA3848BA}, &deal);
            Virtual<uint32_t>(deal, 0x7C);
            if (subject && Virtual<uint32_t>(subject, 0x20)) {
                void* const who = Virtual<void*>(subject, 0x20);
                if (Virtual<uint8_t>(who, 0x0, uint32_t{0x0773FEF1}, &party)) {
                    const uint32_t partyType = Virtual<uint32_t>(party, 0x20);
                    if (partyType == Virtual<uint32_t>(deal, 0x7C)) {
                        if (!Virtual<uint8_t>(record->advice, 0x3C, uint32_t{2})) {
                            record->c = At<uint32_t>(self, field::kTicks);
                            if (Virtual<uint8_t>(record->advice, 0x20)) {
                                record->c = At<uint32_t>(self, field::kTicks);
                                StartFor(self, record, index, subject);
                            }
                            handled = 1;
                        } else {
                            void* const before = Virtual<void*>(record->advice, 0x64);
                            bool same = false;
                            if (before) {
                                const uint32_t mine = Virtual<uint32_t>(subject, 0x20);
                                same = mine == Virtual<uint32_t>(before, 0x20);
                            }
                            if (same) {
                                Virtual<void>(record->advice, 0x38, uint32_t{0x10}, uint32_t{1});
                                Virtual<void>(record->advice, 0x60, subject);
                                handled = 1;
                            } else if (&record->advice != &kept) {
                                void* const advice = record->advice;
                                void* const old = kept;
                                if (advice != old) {
                                    if (advice)
                                        AddRef(advice);
                                    kept = advice;
                                    if (old)
                                        Release(old);
                                }
                            }
                        }
                    }
                }
            }
            if (deal)
                Release(deal);
            if (party)
                Release(party);
        }

        if (deals && !handled && kept) {
            void* fresh = nullptr;
            if (Virtual<uint8_t>(kept, 0x14, &fresh)) {
                void* deal = nullptr;
                if (Virtual<uint8_t>(fresh, 0x20) && Virtual<uint8_t>(fresh, 0x0, uint32_t{0xEA3848BA}, &deal)) {
                    // The original sets only the advice of the record it
                    // pushes; the three numbers are written before use.
                    Record blank = {nullptr, 0, 0, 0};
                    char* const records = self + field::kRecords;
                    This<void>(addr::cSC4Advisor_F410070, records, &blank);
                    // The inserter's result sits right below the pair, as in
                    // the original frame.
                    struct {
                        TreeNode* result;
                        uint32_t key;
                        int32_t index;
                    } insert;
                    const int32_t index =
                        (static_cast<int32_t>(At<char*>(records, 0x4) - At<char*>(records, 0x0)) >> 4) - 1;
                    insert.key = Virtual<uint32_t>(fresh, 0x68);
                    insert.index = index;
                    This<void>(addr::cSC4Advisor_F40F080, self + field::kTriggers, &insert.result, &insert.key);
                    Record* const record = At<Record*>(self, field::kRecords + 0x4) - 1;
                    This<void>(addr::SC4RefPtr_AssignFrom, &record->advice, &fresh);
                    record->c = At<uint32_t>(self, field::kTicks);
                    int32_t* const slot = This<int32_t*>(addr::cSC4Advisor_F40EFF0, self + field::kActive, record);
                    *slot = (static_cast<int32_t>(At<char*>(records, 0x4) - At<char*>(records, 0x0)) >> 4) - 1;
                    Virtual<void>(record->advice, 0x60, subject);
                    This<void>(addr::cSC4Advisor_F40DF20, self, record);
                }
                if (deal)
                    Release(deal);
            }
            if (fresh)
                Release(fresh);
        }

        ListNode* const done = At<ListNode*>(self, field::kEvents)->next;
        ListNode* const previous = done->previous;
        ListNode* const next = done->next;
        previous->next = next;
        next->previous = previous;
        if (done->object)
            Release(done->object);
        Free(done);
        if (kept)
            Release(kept);
    } while (--left);
}

bool InstallClasses2(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[36] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSC4Advisors_F417600, reinterpret_cast<void*>(&DaylightStart)},
        {addr::cSC4Advisors_F4176E0, reinterpret_cast<void*>(&DaylightEnd)},
        {addr::cSC4Advisors_F4177B0, reinterpret_cast<void*>(&XorPair)},
        {addr::cSC4Advisors_F4184B0, reinterpret_cast<void*>(&DecodePercent)},
        {addr::cSC4Advisors_F41B7F0, reinterpret_cast<void*>(&SetAdvisorClassId)},
        {addr::cSC4Advisors_F41B800, reinterpret_cast<void*>(&SetAdvisorInit)},
        {addr::cSC4Advisors_F41B850, reinterpret_cast<void*>(&SetAdvisorShutdown)},
        {addr::cSC4Advisors_F41B890, reinterpret_cast<void*>(&SetEraseSubtree)},
        {addr::cSC4Advisors_F41BE50, reinterpret_cast<void*>(&SetDestroy)},
        {addr::cSC4Advisors_F41BEA0, reinterpret_cast<void*>(&SetEraseRange)},
        {addr::cSC4Advisors_F41BF60, reinterpret_cast<void*>(&SetAdvisorDestruct)},
        {addr::cSC4Advisors_F41C080, reinterpret_cast<void*>(&SetAdvisorConstruct)},
        {addr::cSC4Advisors_F41C0F0, reinterpret_cast<void*>(&SetAdvisorDelete)},
        {addr::cSC4Advisors_F41C170, reinterpret_cast<void*>(&NewSetAdvisor)},
        {addr::cSC4Advisors_F420D30, reinterpret_cast<void*>(&DealAdvisorClassId)},
        {addr::cSC4Advisors_F420D40, reinterpret_cast<void*>(&DealAdviceClassId)},
        {addr::cSC4Advisors_F420D60, reinterpret_cast<void*>(&DealAdviceQueryInterface)},
        {addr::cSC4Advisors_F420D90, reinterpret_cast<void*>(&DealAdviceGetType)},
        {addr::cSC4Advisors_F420DA0, reinterpret_cast<void*>(&DealAdviceQueryInterface4)},
        {addr::cSC4Advisors_F420DB0, reinterpret_cast<void*>(&DealAdviceDelete4)},
        {addr::cSC4Advisors_F420DC0, reinterpret_cast<void*>(&DealAdviceQueryInterfaceC)},
        {addr::cSC4Advisors_F420DD0, reinterpret_cast<void*>(&NewDealAdvisor)},
        {addr::cSC4Advisors_F420E20, reinterpret_cast<void*>(&DealAdvisorDelete)},
        {addr::cSC4Advisors_F420E60, reinterpret_cast<void*>(&NewDealAdvice)},
        {addr::cSC4Advisors_F420EA0, reinterpret_cast<void*>(&DealAdviceDelete)},
        {addr::cSC4Advisors_F420EE0, reinterpret_cast<void*>(&DealAdviceCopy)},
        {addr::cSC4Advisors_F420F40, reinterpret_cast<void*>(&DealAdviceWrite)},
        {addr::cSC4Advisors_F420F80, reinterpret_cast<void*>(&DealAdviceRead)},
        {addr::cSC4Advisors_F4217C0, reinterpret_cast<void*>(&DealAdviceInit)},
        {addr::cSC4Advisors_F421880, reinterpret_cast<void*>(&DealToken)},
        {addr::cSC4Advisors_F4229C0, reinterpret_cast<void*>(&DealAdvisorInit)},
        {addr::cSC4Advisors_F422AA0, reinterpret_cast<void*>(&DealAdvisorShutdown)},
        {addr::cSC4Advisors_F422B40, reinterpret_cast<void*>(&DealAdvisorEvents)},
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

const bool g_classes2Registered = registry::Register({
    "scripts.advisors.classes2",
    "scripts/advisors",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4Advisors_F422B40,
    "Our advisor classes, second part: the set advisor, the deal advisor and its advice (docs/lua.md)",
    false,
    InstallClasses2,
});

} // namespace
} // namespace opensc4::advisor_classes
