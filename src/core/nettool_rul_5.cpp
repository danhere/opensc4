// Our cSC4NetworkTool, part r5 (build 641; the class map is in
// src/core/nettool_sim.h, the rule layouts in src/core/nettool_rul.h): the
// RUL parsers' third part, 0x00640D40 to 0x0064382F (but the type row's
// static constructor 0x00641F70 and destructor 0x00643510, part r1's), and
// the containers they fill. Two groups, each a list of detours at the game's
// addresses:
//   simulators.nettool.rulparse3: the loaders and parsers: LoadRul0 with its
//     section callback and the 'Ordering' line callback, LoadRul1, a type
//     row's Clear, LoadBridges, the bridge ini line callback (the exemplar
//     reader), the bridge layout load and the bridge lexer (constructor,
//     Parse, ParseBridge, ParseLength, PushMap, ClearMaps) (14 detours);
//   simulators.nettool.rulmaps3: the containers: the int-keyed map's and
//     the string map's FindOrInsert, the RUL1 map's InsertUnique, hinted
//     insert and FindOrInsert, the bridge record's copy constructor and the
//     record vector's range helpers (CopyRange, UninitializedCopy,
//     UninitializedFillN), Erase and InsertOverflow (11 detours).
// Two groups because the parsers read resources and ini files at every city
// load while the containers are pure memory work: each can be tried and
// bisected alone.
//
// Every function was written from the disassembly. Calls between them go to
// the game's addresses (through the address entries), so each detour is
// independent of the others and OPENSC4_NETTOOL_SKIP bisects them. All 25
// functions of the part are here; none is guarded.
//
// NAM. The guarded RUL2 line callback 0x0063D1E0 and the RUL2 engine
// 0x00634D70 stay the game's; nothing here calls them or passes them on.
// NAM's DLL reads the RUL0 rules (and edits them in place), the store, the
// type rows and the RUL2 multimap by layout: every container, node, rule and
// record here stays in the game's memory with the game's layout, allocated
// through the game's allocators (SC4::stl_allocate, SC4::Free) and built
// through the game's template helpers where the game calls them; nothing is
// kept across calls. The callbacks this code hands to the ini (0x006416B0,
// 0x00640320, 0x00641A50, 0x00642220) are the game's addresses, so a game
// caller or NAM patching one of them sees the same values. No wrapper of
// src/core/game_*.cpp or *fix*.cpp detours or tests return addresses in
// 0x00640D40 to 0x0064382F; sim_timers.cpp has nothing here.
//
// Floats. One function touches the x87: the bridge line callback 0x00642220.
//   - The test of +0x20's floats against 0.0 (FCOMP, TEST AH,1: skipped when
//     below or unordered) is `value >= 0.0f` (false for NaN, as the game).
//   - The quarter-turn angles: FILD i, FMUL float pi/2 (0x00AA8D30), FSTP
//     float: the product of a small integer and a float is exact in a
//     double, so one rounding to float in both (as part r4's exemplar
//     callback); the turning itself is the game's 0x005D77A0 (FSIN, FCOS),
//     called at its address with the float as a word.
//   - The chain +0x60[i] = (a[2i+1] - a[2i]) + +0x60[i+1] and +0x6C[i] =
//     +0x60[i+1] + a[2i+1], and the angles +0x12C = +0x114 * -1.0, +0x130 =
//     +0x118, +0x138 = +0x120 * -1.0, +0x13C = +0x124: naked x87 helpers
//     with the game's instructions (FLD / FSUB / FADD / FMUL float, FSTP
//     float), so NaN operands, the sign of a negated NaN and the copy of a
//     signalling NaN come out as the game's (a C++ compiler may turn
//     x * -1.0 into a sign flip and a float copy into a word move).
//   No __ftol2, nothing held on the x87 stack across a call.
//
// Registers. The scan of every direct caller (ECX, EDX and EAX after the
// call, both branches: 0x006253D0, 0x00623310, 0x00643510 and the part's
// own) found no read of ECX or EDX after any of these functions, and no
// callee that the callers reach next reads them before writing. EAX is the
// game's where callers read it: the FindOrInserts (the value), the inserts
// and Erase (the result, first), the copy constructor and the lexer's
// constructor (this), the range helpers (the end), the parsers (AL; the
// callers test AL only). A type row's Clear ends with a jump to 0x0063D760
// (EAX is whatever that leaves; no caller reads it); ours calls it. No
// naked thunk was needed. The callbacks' return values are not read (the
// ini's slot 0x34 and slot 0x38 call them and pop their arguments).
//
// Stack leftovers the game stores:
//   - The bridge line callback 0x00642220 builds each record on its stack
//     (the constructor 0x00640030 writes only the vectors, the 24 floats
//     +0xE4 to +0x140, the name and the int map) and copies it whole into
//     the vector (0x00640FA0), then fills the copy from the exemplar. What
//     it does not write keeps the original's stack leftovers; ours zeros
//     (the temporary starts zeroed). Never written: bytes +0x31 to +0x33
//     (+0x30 gets one byte, GetValUint8). Written only with a property:
//     +0x2C (0x482CB445), the byte +0x30 (0x482CB446), +0x3C (only when
//     0x482CB448 gives +0x40 non-zero), +0x5C (0x482CB441), +0x7C (when
//     +0x10 is not zero: 0x482CB44D), the bytes +0x98 / +0x99 (0x482CB44F /
//     50), +0xDC, +0xE0, +0x144 (the tail chain). Readers (every function
//     that touches a record, found through every 0x178 stride, the
//     0xAE4C415D magic, the tool's row pointer +0x358 and the rows'
//     literal addresses; records are copied only by 0x00640FA0 and
//     0x0063FCD0, inside this vector's code):
//       +0x31..+0x33: never read (0x0063176D reads +0x30 as a byte, and
//         uses its low two bits): cannot matter;
//       +0x3C: read only at 0x0063143A / 0x00631445 in 0x00631300 behind
//         +0x40 != 0 (0x0063137A): never a leftover when read;
//       +0xDC, +0xE0, +0x144 (and +0xE4 to +0x140): read only in 0x00631300
//         behind the byte +0xD8 (0x00631A59), which is 1 only when the
//         whole chain was written: cannot matter;
//       +0x2C (0x00631759 / 0x00631767, a middle cell's piece ID) and the
//         byte +0x30 (0x0063176D) in 0x00631300, +0x5C in 0x0062EA90
//         (0x0062EB1E, the deck height of automatic bridges), +0x7C in
//         0x00631300 (0x0063135B; effective when +0x40 or +0xD8 is set), the
//         bytes +0x98 / +0x99 in 0x0062F9D0 (0x0062FA66 / 0x0062FAA1, which
//         bridge lists a record is in): read UNGATED. These differ between
//         the original and ours only for an exemplar that lacks the
//         property; then the original reads whatever its stack held (the
//         network tool's bridge choice and pieces could differ). The
//         parity checksum hashes these fields apart from the always-written
//         ones (src/core/nettool_rul_autotest.cpp, ` y`), so an exemplar
//         without one would show as A-vs-A noise there.
//     Bytes +0x9A, +0x9B and +0xD9 to +0xDB are never written nor copied
//     (byte copies) nor read.
//   - The RUL1 loader 0x00641D30 and FindOrInsert 0x00641C40 build a value
//     {two types, a pair table} on the stack whose table word +0x0 they never
//     write; CopyFrom 0x0063CAA0 and the node fill (0x0063E980 and InsertAt's
//     inline copy) never read or copy that word: cannot matter.
//   - LoadRul0 0x00641B60 leaves the context's rule word +0x8 unwritten; the
//     section callback 0x00641A50 writes it before the only reader (the
//     RUL0 line callback 0x00640320) runs; the Ordering callback does not
//     read it: cannot matter.
//   - The bridge lexer on 0x006436D0's stack: the constructor and Parse
//     write every field read later (+0x20 / +0x24 only after NextToken
//     wrote them); nothing reads past +0x30.
// Other stores into the game's stack: several functions overwrite their own
// argument slots (the int map's and the RUL1 map's FindOrInserts store the
// insert's result in the key's slot, the Ordering callback keeps its first
// / previous rules and the split flag in its argument slots, the section
// callback the context + 0x10, InsertUnique the iterator in the value's
// slot, InsertHint the iterator in the hint's or value's slot, ParseLength
// the integer in the record's slot) and pass such slots as allocator tags;
// no caller reads its pushed arguments after the call (the ini's slots 0x34
// and 0x38 pop them), and the tags are not read. Ours use locals.
//
// Calls, each count from the asm's pushes for that call and the callee's own
// `ret N`:
//   - the part's own, at the game's addresses: the int map's FindOrInsert,
//     the string map's FindOrInsert, the RUL1 map's FindOrInsert, the record
//     copy constructor, ParseLength one (ret 4); InsertUnique, Erase, the
//     lexer's constructor, Parse two (ret 8); InsertHint three (ret 0xC);
//     InsertOverflow five (ret 0x14); ParseBridge, PushMap, ClearMaps none; CopyRange cdecl five (the game pushes a tag
//     and 0 after its three), UninitializedCopy and UninitializedFillN cdecl
//     four, LoadBridgeLayouts cdecl three;
//   - parts r1 to r4: NextToken, Transpose none; the bridge layout parser
//     two (ret 8); the store's FindOrInsert, ParseCellLayout,
//     ParseConsLayout, the vector20 and int map copy constructors, the
//     string map's CopyFrom and InsertNoResize, Find, the pair table's
//     CopyFrom, the bridge record Assign one (ret 4); ConstructPair two (ret
//     8); the int map's InsertHint three (ret 0xC); the RUL1 map's InsertAt
//     five (ret 0x14); the store's Clear, the loader lists' Destruct, the
//     record's constructor and destructor, the string map's Clear,
//     DestroyTable3C none; EraseSubtree one; ReadRul1Pairs cdecl four;
//   - template code: SC4::stl_allocate, SC4::Free cdecl one; PtrVector::
//     Reserve, SC4Uint32HashSet::Construct, SC4PointVector::Resize,
//     SC4PointVector::CopyConstruct, SC4FloatVector::CopyConstruct and
//     Assign, SC4ByteVector::ConstructZeroed one (ret 4); SC4FloatVector::
//     Resize2 two (ret 8); SC4PtrVector::FillInsert, SC4FloatVector::
//     InsertN, SC4Vector2Vector::InsertN three (ret 0xC); SC4Uint32HashSet::
//     Clear none; SC4PairMap::LowerBound one; SC4Sint32Map::Find two (ret 8:
//     result*, key*); SC4PairLess::Compare two (ret 8; AL); SC4MapIterator::
//     Next and SC4TreeIterator::Decrement none (ECX the iterator);
//     SC4UI::F43ABA0 (a string list's clear) none;
//   - strings: SC4String::FromText one (ret 4), FromRange and the body's
//     0x00408420 three (ret 0xC: first, last, tag), CopyBuffer four (ret
//     0x10), AssignRange three (ret 0xC), SplitAtChar two (ret 8; AL),
//     cRZBaseString::BeginsWith two (ret 8; AL), cRZBaseString::Mid three
//     (ret 0xC; EAX out); a cIGZString's slot 0x28 two (another string, 0;
//     AL); SC4LocalizedString's constructor three (ret 0xC; EAX this);
//   - the C runtime: SC4::TextToUint32, SC4::TextToInt cdecl one; SC4::
//     SplitText cdecl five; SC4::memmove cdecl three; SC4::atexit cdecl one;
//     SC4::RotatePoint 0x005D77A0 cdecl four;
//   - resources: the resource manager (0x00B43CA8) slot 0xC five (key*, IID
//     0xA52160F5, out*, 0, 0; AL); SC4::OpenResourceRecord cdecl three; a
//     resource's slot 0x1C none (the size or the property holder), slot 0x38
//     two (buffer, size), Release slot 0x8; the holder's slot 0x24 one (a
//     property ID; the property or null), the property's slot 0x14 none (the
//     variant), the variant's slots 0x10 (count), 0x17C / 0x19C / 0x1BC (the
//     uint8 / uint32 / float data) none and 0x34 (GetValUint8), 0x64
//     (GetValUint32), 0x94 (GetValFloat32) one (a reference); SC4::
//     GetPropertyUint32, GetPropertyFloat, GetPropertyBool,
//     GetPropertyFloatVector cdecl three (AL); the ini's slot 0x34 three
//     (section string*, callback, data) and slot 0x38 two (callback, data);
//     a rule file's slot 0 one (1: the deleting destructor).
#include "core/nettool_rul.h"
#include "core/nettool_sim.h"

#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace opensc4::nettool_sim {
namespace {

using namespace rul;

// The game's allocator and free (cdecl, one argument).
char* Allocate(uint32_t size)
{
    return CallCdecl<char*, uint32_t>(addr::SC4_stl_allocate, size);
}
void Free(void* block)
{
    CallCdecl<void, void*>(addr::SC4_Free, block);
}
void FreeIfSet(void* block)
{
    if (block)
        Free(block);
}

inline constexpr std::uintptr_t kStringVtable = 0x00A80810; // addr::SC4String_Vtable

// Texts of the executable the code here compares with or reads from.
inline constexpr std::uintptr_t kTextOrdering = 0x00AA8EA8;     // "Ordering", 8 characters
inline constexpr std::uintptr_t kTextIntersection = 0x00AA8E8C; // "HighwayIntersectionInfo_", 24
inline constexpr std::uintptr_t kTextRotationRing = 0x00AA8E7C; // "RotationRing"
inline constexpr std::uintptr_t kTextAddTypes = 0x00AA8E70;     // "AddTypes"
inline constexpr std::uintptr_t kTextSection = 0x00AA8EB8;      // "section", 7
inline constexpr std::uintptr_t kTextBridge = 0x00AA8EC0;       // "bridge", 6
inline constexpr std::uintptr_t kTextStop = 0x00AA8EC8;         // "stop", 4
inline constexpr std::uintptr_t kTextLength = 0x00A9F4B8;       // "length", 6
inline constexpr std::uintptr_t kTextEnd = 0x00AA0A5C;          // "end", 3
inline constexpr uint32_t kOrderingLength = 8, kIntersectionLength = 0x18;
// The atexit thunks that destroy the two static key strings.
inline constexpr std::uintptr_t kRotationRingThunk = 0x00A7C290, kAddTypesThunk = 0x00A7C270;

inline constexpr uint32_t kResourceIid = 0xA52160F5;
inline constexpr uint32_t kLayoutType = 0x0A5BCF4B, kLayoutGroup = 0xAA5BCF57; // the bridge layout resources
inline constexpr std::size_t kRecordSize = bridgerecord::kSize;
inline constexpr uint32_t kRul1Types = 13;

const char* Text(std::uintptr_t address)
{
    return reinterpret_cast<const char*>(address);
}

uint32_t Bits(float value)
{
    uint32_t word = 0;
    std::memcpy(&word, &value, sizeof word);
    return word;
}

// The number of records of a range as the game computes it (a signed
// division by 0x178, IMUL by 0xAE4C415D).
int32_t RecordCount(const char* first, const char* last)
{
    return static_cast<int32_t>(last - first) / static_cast<int32_t>(kRecordSize);
}

int32_t NextToken(char* lexer)
{
    return CallThis<int32_t>(addr::SC4RulLexer_NextToken, lexer);
}

// The lexer's identifier is `length` bytes equal to the text (the game's
// length test and REPZ CMPS, or one dword compare for 'stop').
bool IdentIs(char* lexer, std::uintptr_t text, uint32_t length)
{
    const char* const begin = At<char*>(lexer, lexer::kIdentBegin);
    const char* const end = At<char*>(lexer, lexer::kIdentEnd);
    if (static_cast<uint32_t>(end - begin) != length)
        return false;
    return std::memcmp(begin, Text(text), length) == 0;
}

// A float appended to a vector the way 0x00642220 inlines it: in place, or
// a new buffer of size + max(size, 1) words, the old words moved
// (SC4::memmove), the new one stored (no null test on that path), the old
// buffer freed.
void PushFloatInline(Vector& v, uint32_t word)
{
    if (v.end != v.capacity) {
        if (v.end)
            *reinterpret_cast<uint32_t*>(v.end) = word;
        v.end += 4;
        return;
    }
    char* const begin = v.begin;
    char* const end = v.end;
    const uint32_t size = static_cast<uint32_t>(static_cast<int32_t>(end - begin) >> 2);
    const uint32_t grow = size < 1 ? 1u : size;
    const uint32_t length = grow + size;
    char* const buffer = length ? Allocate(length * 4) : nullptr;
    char* at = buffer;
    if (end != begin) {
        const uint32_t bytes = static_cast<uint32_t>(end - begin);
        at = CallCdecl<char*, char*, char*, uint32_t>(addr::SC4_memmove, buffer, begin, bytes) + bytes;
    }
    *reinterpret_cast<uint32_t*>(at) = word;
    at += 4;
    FreeIfSet(v.begin);
    v.begin = buffer;
    v.end = at;
    v.capacity = buffer + length * 4;
}

// *out = (*a - *b) + *c as FLD / FSUB / FADD float and FSTP float
// (0x00642922 to 0x00642936).
__declspec(naked) void __cdecl X87SubAddStore(const float* /*a*/, const float* /*b*/, const float* /*c*/,
                                              float* /*out*/)
{
    __asm {
        mov eax, [esp + 4]
        fld dword ptr [eax]
        mov eax, [esp + 8]
        fsub dword ptr [eax]
        mov eax, [esp + 12]
        fadd dword ptr [eax]
        mov eax, [esp + 16]
        fstp dword ptr [eax]
        ret
    }
}

// *out = *a + *b as FLD / FADD float and FSTP float (0x0064293B to
// 0x00642945).
__declspec(naked) void __cdecl X87AddStore(const float* /*a*/, const float* /*b*/, float* /*out*/)
{
    __asm {
        mov eax, [esp + 4]
        fld dword ptr [eax]
        mov eax, [esp + 8]
        fadd dword ptr [eax]
        mov eax, [esp + 12]
        fstp dword ptr [eax]
        ret
    }
}

// The record's derived angles as 0x00643250 to 0x006432BB compute them:
// +0x12C = +0x114 * -1.0, +0x130 = +0x118, +0x138 = +0x120 * -1.0, +0x13C =
// +0x124 (FLD, FMUL float, FSTP float; the -1.0 is the game's 0x00A8FE78).
__declspec(naked) void __cdecl X87BridgeAngles(char* /*record*/, const float* /*minusOne*/)
{
    __asm {
        mov eax, [esp + 4]
        mov ecx, [esp + 8]
        fld dword ptr [eax + 0x114]
        fmul dword ptr [ecx]
        fld dword ptr [eax + 0x118]
        fxch st(1)
        fstp dword ptr [eax + 0x12C]
        fstp dword ptr [eax + 0x130]
        fld dword ptr [eax + 0x120]
        fmul dword ptr [ecx]
        fld dword ptr [eax + 0x124]
        fxch st(1)
        fstp dword ptr [eax + 0x138]
        fstp dword ptr [eax + 0x13C]
        ret
    }
}

// The key (two signed words) is below a node's (at +0x10 / +0x14): first
// word, then second.
bool PairBelow(const int32_t* key, char* node)
{
    const int32_t first = At<int32_t>(node, rul1map::kFirst);
    if (key[0] != first)
        return key[0] < first;
    return key[1] < At<int32_t>(node, rul1map::kSecond);
}

// A node's pair is below the key.
bool NodeBelow(char* node, const int32_t* key)
{
    const int32_t first = At<int32_t>(node, rul1map::kFirst);
    if (first != key[0])
        return first < key[0];
    return At<int32_t>(node, rul1map::kSecond) < key[1];
}

// ====================================================================================
// simulators.nettool.rulmaps3: the int-keyed map, the string map, the RUL1 map
// ====================================================================================

// 0x00640D40 (ret 4: the key*; EAX the value's element vector, node +
// 0x14): the lower bound of the key (signed); when it is not the head and
// the key is not below its key, that node. Else a value {the key, copies
// (0x0063C150, 0x00464760) of two empty vectors} inserted by InsertHint
// 0x0063E500 with the lower bound as the hint, the value's buffers freed.
char* __fastcall IntMapFindOrInsert(char* self, void*, const int32_t* key)
{
    static bool logged = false;
    FirstCall("IntMapFindOrInsert", logged);
    char* const head = At<char*>(self, tree::kHead);
    char* y = head;
    for (char* x = At<char*>(head, tree::kParent); x;) {
        if (At<int32_t>(x, intmap::kKey) < *key) {
            x = At<char*>(x, tree::kRight);
        } else {
            y = x;
            x = At<char*>(x, tree::kLeft);
        }
    }
    if (y != head && *key >= At<int32_t>(y, intmap::kKey))
        return y + intmap::kVector20;
    alignas(4) char empty[intmap::kValueSize - intmap::kValueVector20] = {};
    alignas(4) char value[intmap::kValueSize] = {};
    At<int32_t>(value, 0) = *key;
    CallThis<char*, char*>(addr::SC4Vector20_CopyConstruct, value + intmap::kValueVector20, empty);
    CallThis<char*, char*>(addr::SC4FloatVector_CopyConstruct, value + intmap::kValueFloats,
                           empty + (intmap::kValueFloats - intmap::kValueVector20));
    char* node = nullptr;
    char** const result =
        CallThis<char**, char**, char*, char*>(addr::SC4BridgeIntMap_InsertHint, self, &node, y, value);
    (void)result;
    FreeIfSet(At<char*>(value, intmap::kValueFloats));
    FreeIfSet(At<char*>(value, intmap::kValueVector20));
    return node + intmap::kVector20;
}

// 0x00640E10 (ret 4: a game string*; EAX the value, node + 0x18): the node
// of the key (0x0063C0F0); else a pair of the key and an empty value
// (0x0063D6A0) inserted (0x0063FB80, whose answer + 0x14 is the value), the
// pair's float vector, element vector and string freed, then the empty
// value's two vectors.
char* __fastcall StringMapFindOrInsert(char* self, void*, void* key)
{
    static bool logged = false;
    FirstCall("StringMapFindOrInsert", logged);
    char* const node = CallThis<char*, void*>(addr::SC4StringHashMap_Find, self, key);
    if (node)
        return node + stringmap::kPair + stringmap::kVector20;
    alignas(4) char empty[stringmap::kPairSize - stringmap::kVector20] = {};
    alignas(4) char pair[stringmap::kPairSize] = {};
    char* const made = CallThis<char*, void*, char*>(addr::SC4StringHashMap_ConstructPair, pair, key, empty);
    char* const value =
        CallThis<char*, char*>(addr::SC4StringHashMap_InsertNoResize, self, made) + stringmap::kVector20;
    FreeIfSet(At<char*>(pair, stringmap::kFloats));
    FreeIfSet(At<char*>(pair, stringmap::kVector20));
    At<std::uintptr_t>(pair, stringmap::kKey) = kStringVtable;
    FreeIfSet(At<char*>(pair, stringmap::kKey + 4));
    FreeIfSet(At<char*>(empty, stringmap::kValueFloats));
    FreeIfSet(At<char*>(empty, 0));
    return value;
}

// 0x00640EF0 (ret 8: result*, value* (two signed types, a pair table); EAX
// result): STLport's insert_unique: down from the root (left when the value
// is below the node's pair), the last node y; when the last step went left
// and y is the leftmost, inserted at once; else j = y (or its predecessor,
// 0x006809E0, after a left step): when j's pair is below the value,
// inserted (InsertAt 0x0063FBF0 with x (null), y, the value, 0; its result
// slot is j), {the node, 1}; else {j, 0}. Only the result's byte +4 is
// written after the node.
char* __fastcall Rul1InsertUnique(char* self, void*, char* result, const int32_t* value)
{
    static bool logged = false;
    FirstCall("Rul1InsertUnique", logged);
    char* const head = At<char*>(self, tree::kHead);
    char* x = At<char*>(head, tree::kParent);
    char* y = head;
    bool left = true;
    while (x) {
        y = x;
        left = PairBelow(value, x);
        x = At<char*>(x, left ? tree::kLeft : tree::kRight);
    }
    char* j = y;
    const auto insert = [&]() {
        char** const at = CallThis<char**, char**, char*, char*, const int32_t*, char*>(
            addr::SC4Rul1Map_InsertAt, self, &j, x, y, value, nullptr);
        At<char*>(result, 0) = *at;
        At<uint8_t>(result, 4) = 1;
        return result;
    };
    if (left) {
        if (y == At<char*>(head, tree::kLeft))
            return insert();
        CallThis<void>(addr::SC4TreeIterator_Decrement, &j);
    }
    if (NodeBelow(j, value))
        return insert();
    At<char*>(result, 0) = j;
    At<uint8_t>(result, 4) = 0;
    return result;
}

// 0x006413B0 (ret 0xC: result*, a hint node, value*; EAX result): STLport's
// insert_unique with a hint, in the game's order of tests:
//   - the hint is the leftmost: an empty map: InsertUnique; the value below
//     the hint: InsertAt(hint, hint); equal: the hint; else with `after` the
//     hint's successor (0x005D17C0): after is the head: InsertAt(0, hint,
//     w = hint); the value not below after's pair (0x006337B0): InsertUnique;
//     else InsertAt(0, hint, w = hint) when the hint has no right child,
//     InsertAt(after, after) otherwise;
//   - the hint is the head: above the rightmost: InsertAt(0, rightmost, w =
//     head); else InsertUnique;
//   - else `before` the hint's predecessor (0x006809E0): the value below the
//     hint and above before: InsertAt(0, before, w = before) when before has
//     no right child, InsertAt(hint, hint) otherwise; the value not below
//     the hint: after the successor, the hint's pair not below the value:
//     the hint; else (after not the head and the value not below after's:
//     InsertUnique) InsertAt(0, hint, w = hint) or InsertAt(after, after);
//     the value below the hint but not above before: InsertUnique.
// InsertUnique is 0x00640EF0 (its node into *result).
char** __fastcall Rul1InsertHint(char* self, void*, char** result, char* hint, const int32_t* value)
{
    static bool logged = false;
    FirstCall("Rul1InsertHint", logged);
    char* const head = At<char*>(self, tree::kHead);
    const auto insertAt = [&](char* x, char* y, char* w) {
        return CallThis<char**, char**, char*, char*, const int32_t*, char*>(addr::SC4Rul1Map_InsertAt, self,
                                                                               result, x, y, value, w);
    };
    const auto insertUnique = [&]() {
        alignas(4) char pair[8] = {};
        char* const answer = CallThis<char*, char*, const int32_t*>(addr::SC4Rul1Map_InsertUnique, self, pair, value);
        *result = At<char*>(answer, 0);
        return result;
    };
    const auto less = [&](const void* a, const void* b) {
        return CallThis<uint8_t, const void*, const void*>(addr::SC4PairLess_Compare, self + 8, a, b) != 0;
    };
    if (hint == At<char*>(head, tree::kLeft)) {
        if (At<uint32_t>(self, tree::kCount) == 0)
            return insertUnique();
        if (PairBelow(value, hint))
            return insertAt(hint, hint, nullptr);
        if (!NodeBelow(hint, value)) {
            *result = hint;
            return result;
        }
        char* after = hint;
        CallThis<void>(addr::SC4MapIterator_Next, &after);
        if (after == head)
            return insertAt(nullptr, hint, hint);
        if (!less(value, after + tree::kValue))
            return insertUnique();
        if (!At<char*>(hint, tree::kRight))
            return insertAt(nullptr, hint, hint);
        return insertAt(after, after, nullptr);
    }
    if (hint == head) {
        char* const rightmost = At<char*>(head, tree::kRight);
        if (NodeBelow(rightmost, value))
            return insertAt(nullptr, rightmost, hint);
        return insertUnique();
    }
    char* before = hint;
    CallThis<void>(addr::SC4TreeIterator_Decrement, &before);
    uint8_t flag = 0;
    if (PairBelow(value, hint)) {
        flag = 1;
        if (NodeBelow(before, value)) {
            if (!At<char*>(before, tree::kRight))
                return insertAt(nullptr, before, before);
            return insertAt(hint, hint, nullptr);
        }
    }
    char* after = hint;
    CallThis<void>(addr::SC4MapIterator_Next, &after);
    uint8_t below = flag == 0 ? 1 : 0;
    if (flag == 0) {
        below = less(hint + tree::kValue, value) ? 1 : 0;
        if (!below) {
            *result = hint;
            return result;
        }
        if (after == head || less(value, after + tree::kValue)) {
            if (!At<char*>(hint, tree::kRight))
                return insertAt(nullptr, hint, hint);
            return insertAt(after, after, nullptr);
        }
    }
    if (flag == below) {
        *result = hint;
        return result;
    }
    return insertUnique();
}

// 0x00641C40 (ret 4: a type pair*; EAX the node's pair table, node +
// 0x18): the lower bound (0x0063C070); when it is not the head and the pair
// is not below its pair, that node. Else a pair table of 100 buckets
// (0x00829BC0), a value {the pair, a copy of that table (0x0063CAA0)}
// inserted by 0x006413B0 with the lower bound as the hint, both tables
// cleared (0x008EBA60) and their buckets freed.
char* __fastcall Rul1FindOrInsert(char* self, void*, const int32_t* key)
{
    static bool logged = false;
    FirstCall("Rul1FindOrInsert", logged);
    char* const bound = CallThis<char*, const int32_t*>(addr::SC4PairMap_LowerBound, self, key);
    if (bound != At<char*>(self, tree::kHead) && !PairBelow(key, bound))
        return bound + rul1map::kTable;
    alignas(4) char empty[table::kSize] = {};
    CallThis<void, uint32_t>(addr::SC4Uint32HashSet_Construct, empty, table::kFirstBuckets);
    alignas(4) char value[rul1map::kTable - rul1map::kFirst + table::kSize] = {};
    At<int32_t>(value, 0) = key[0];
    At<int32_t>(value, 4) = key[1];
    char* const copy = value + rul1node::kValueTable;
    CallThis<void, char*>(addr::SC4PairHashTable_CopyFrom, copy, empty);
    char* node = nullptr;
    CallThis<char**, char**, char*, char*>(addr::SC4Rul1Map_InsertHint, self, &node, bound, value);
    CallThis<void>(addr::SC4Uint32HashSet_Clear, copy);
    FreeIfSet(At<char*>(copy, table::kBuckets));
    CallThis<void>(addr::SC4Uint32HashSet_Clear, empty);
    FreeIfSet(At<char*>(empty, table::kBuckets));
    return node + rul1map::kTable;
}

// ====================================================================================
// simulators.nettool.rulmaps3: the bridge records and their vector
// ====================================================================================

void CopyWords(char* to, const char* from, std::size_t first, std::size_t last)
{
    for (std::size_t at = first; at <= last; at += 4)
        At<uint32_t>(to, at) = At<uint32_t>(const_cast<char*>(from), at);
}

// 0x00640FA0 (ret 4: the source record*; EAX this): in the game's order:
// the words +0x0 to +0x10, the points +0x14 (0x007CED60), the floats +0x20
// (0x00464760), the words +0x2C to +0x40, the points +0x44, the floats
// +0x50, +0x5C, the floats +0x60 and +0x6C, +0x78, +0x7C, the floats +0x80
// and +0x8C, the bytes +0x98 and +0x99, the words +0x9C to +0xD4, the byte
// +0xD8, the words +0xDC to +0x154, the name (CopyBuffer of the source's
// body, null for a null name, with the source argument's slot as tag; then
// the vtable and the count 0), the int map (0x0063E710).
char* __fastcall BridgeRecordCopyConstruct(char* self, void*, char* source)
{
    static bool logged = false;
    FirstCall("BridgeRecordCopyConstruct", logged);
    CopyWords(self, source, 0x0, 0x10);
    CallThis<void*, char*>(addr::SC4PointVector_CopyConstruct, self + 0x14, source + 0x14);
    CallThis<void*, char*>(addr::SC4FloatVector_CopyConstruct, self + 0x20, source + 0x20);
    CopyWords(self, source, 0x2C, 0x40);
    CallThis<void*, char*>(addr::SC4PointVector_CopyConstruct, self + 0x44, source + 0x44);
    CallThis<void*, char*>(addr::SC4FloatVector_CopyConstruct, self + 0x50, source + 0x50);
    CopyWords(self, source, 0x5C, 0x5C);
    CallThis<void*, char*>(addr::SC4FloatVector_CopyConstruct, self + 0x60, source + 0x60);
    CallThis<void*, char*>(addr::SC4FloatVector_CopyConstruct, self + 0x6C, source + 0x6C);
    CopyWords(self, source, 0x78, 0x7C);
    CallThis<void*, char*>(addr::SC4FloatVector_CopyConstruct, self + 0x80, source + 0x80);
    CallThis<void*, char*>(addr::SC4FloatVector_CopyConstruct, self + 0x8C, source + 0x8C);
    At<uint8_t>(self, 0x98) = At<uint8_t>(source, 0x98);
    At<uint8_t>(self, 0x99) = At<uint8_t>(source, 0x99);
    CopyWords(self, source, 0x9C, 0xD4);
    At<uint8_t>(self, 0xD8) = At<uint8_t>(source, 0xD8);
    CopyWords(self, source, 0xDC, 0x154);
    char* const name = source + bridgerecord::kNameVtable;
    char* const body = name ? source + bridgerecord::kNameBegin : nullptr;
    char* tag = source;
    CallThis<void, char*, uint32_t, uint32_t, char**>(addr::SC4String_CopyBuffer, self + bridgerecord::kNameBegin,
                                                      body, 0u, 0xFFFFFFFFu, &tag);
    At<std::uintptr_t>(self, bridgerecord::kNameVtable) = kStringVtable;
    At<uint32_t>(self, bridgerecord::kNameReferences) = 0;
    CallThis<char*, char*>(addr::SC4BridgeIntMap_CopyConstruct, self + bridgerecord::kMapHead,
                           source + bridgerecord::kMapHead);
    return self;
}

// 0x006412B0 (cdecl: first, last, dest; EAX the dest end): (last - first) /
// 0x178 records assigned (0x0063FCD0) in order; none for a count <= 0.
char* __cdecl BridgeCopyRange(char* first, char* last, char* dest)
{
    static bool logged = false;
    FirstCall("BridgeCopyRange", logged);
    const int32_t count = RecordCount(first, last);
    if (count <= 0)
        return dest;
    for (int32_t i = count; i != 0; --i) {
        CallThis<char*, char*>(addr::SC4NetworkBridgeRecord_Assign, dest, first);
        first += kRecordSize;
        dest += kRecordSize;
    }
    return dest;
}

// 0x00641640 (cdecl: first, last, dest, a tag; EAX the dest end): each
// record of [first, last) copy-constructed (0x00640FA0) where dest is not
// null.
char* __cdecl BridgeUninitializedCopy(char* first, char* last, char* dest)
{
    static bool logged = false;
    FirstCall("BridgeUninitializedCopy", logged);
    for (; first != last; first += kRecordSize, dest += kRecordSize) {
        if (dest)
            CallThis<char*, char*>(addr::SC4NetworkBridgeRecord_CopyConstruct, dest, first);
    }
    return dest;
}

// 0x00641680 (cdecl: dest, n, the record*, a tag; EAX the end): n copies
// (0x00640FA0) where dest is not null.
char* __cdecl BridgeUninitializedFillN(char* dest, uint32_t count, char* value)
{
    static bool logged = false;
    FirstCall("BridgeUninitializedFillN", logged);
    for (uint32_t i = count; i != 0; --i) {
        if (dest)
            CallThis<char*, char*>(addr::SC4NetworkBridgeRecord_CopyConstruct, dest, value);
        dest += kRecordSize;
    }
    return dest;
}

// 0x00641EB0 (ret 8: first, last; EAX first): the records [last, end)
// assigned down to first (0x006412B0 with first's slot as tag and 0), the
// records from the new end to the old one destroyed (0x00640230), the end
// set.
char* __fastcall BridgeVectorErase(char* self, void*, char* first, char* last)
{
    static bool logged = false;
    FirstCall("BridgeVectorErase", logged);
    Vector& v = At<Vector>(self, 0);
    char* tag = first;
    char* const end = CallCdecl<char*, char*, char*, char*, char**, uint32_t>(
        addr::SC4NetworkBridgeRecord_CopyRange, last, v.end, first, &tag, 0u);
    char* const old = v.end;
    for (char* r = end; r != old; r += kRecordSize)
        CallThis<void>(addr::SC4NetworkBridgeRecord_Destruct, r);
    v.end = end;
    return first;
}

// 0x00642030 (ret 0x14: position, the record*, a tag, n, at end; EAX the
// new buffer): a buffer of size + max(size, n) records (none for 0); the
// records before the position copied (0x00641640, the at-end argument's
// slot as tag), n copies of the record (one by 0x00640FA0 when not null,
// else 0x00641680), the records after it unless the at-end byte is set; the
// old records destroyed (0x00640230) and their buffer freed.
char* __fastcall BridgeVectorInsertOverflow(char* self, void*, char* position, char* value, void* tag,
                                            uint32_t count, uint32_t atEnd)
{
    static bool logged = false;
    FirstCall("BridgeVectorInsertOverflow", logged);
    (void)tag;
    Vector& v = At<Vector>(self, 0);
    const uint32_t size = static_cast<uint32_t>(RecordCount(v.begin, v.end));
    const uint32_t grow = size < count ? count : size;
    const uint32_t length = grow + size;
    char* const buffer = length ? Allocate(length * static_cast<uint32_t>(kRecordSize)) : nullptr;
    uint32_t atEndSlot = atEnd;
    using CopyFn = char*(__cdecl*)(char*, char*, char*, uint32_t*);
    char* at = reinterpret_cast<CopyFn>(addr::SC4NetworkBridgeRecord_UninitializedCopy)(v.begin, position, buffer,
                                                                                       &atEndSlot);
    if (count == 1) {
        if (at)
            CallThis<char*, char*>(addr::SC4NetworkBridgeRecord_CopyConstruct, at, value);
        at += kRecordSize;
    } else {
        using FillFn = char*(__cdecl*)(char*, uint32_t, char*, uint32_t*);
        at = reinterpret_cast<FillFn>(addr::SC4NetworkBridgeRecord_UninitializedFillN)(at, count, value, &atEndSlot);
    }
    if (static_cast<uint8_t>(atEnd) == 0)
        at = reinterpret_cast<CopyFn>(addr::SC4NetworkBridgeRecord_UninitializedCopy)(position, v.end, at,
                                                                                     &atEndSlot);
    char* const end = v.end;
    for (char* r = v.begin; r != end; r += kRecordSize)
        CallThis<void>(addr::SC4NetworkBridgeRecord_Destruct, r);
    FreeIfSet(v.begin);
    v.end = at;
    v.capacity = buffer + length * kRecordSize;
    v.begin = buffer;
    return buffer;
}

// ====================================================================================
// simulators.nettool.rulparse3: the RUL0 loader
// ====================================================================================

// The 'Ordering' line callback's two static key strings: made once each
// (guard bit set, SC4String::FromText, the destructor thunk given to
// atexit), in the game's order.
void MakeOrderingKeys()
{
    uint32_t& guard = Global<uint32_t>(addr::cSC4NetworkTool_Rul0OrderingKeyGuard);
    if (!(guard & 1u)) {
        guard |= 1u;
        CallThis<void*, const char*>(addr::SC4String_FromText,
                                     reinterpret_cast<void*>(addr::cSC4NetworkTool_Rul0RotationRingKey),
                                     Text(kTextRotationRing));
        CallCdecl<int32_t, std::uintptr_t>(addr::SC4_atexit, kRotationRingThunk);
    }
    if (!(guard & 2u)) {
        guard |= 2u;
        CallThis<void*, const char*>(addr::SC4String_FromText,
                                     reinterpret_cast<void*>(addr::cSC4NetworkTool_Rul0AddTypesKey),
                                     Text(kTextAddTypes));
        CallCdecl<int32_t, std::uintptr_t>(addr::SC4_atexit, kAddTypesThunk);
    }
}

// The store's node of an ID as the Ordering callback inlines the lookup:
// the bucket ID % buckets (unsigned), its chain to the node whose key is the
// ID; null when none.
char* StoreNode(char* store, uint32_t id)
{
    char** const buckets = At<char**>(store, table::kBuckets);
    const uint32_t count =
        static_cast<uint32_t>((At<char*>(store, table::kBucketsEnd) - At<char*>(store, table::kBuckets)) >> 2);
    for (char* node = buckets[id % count]; node; node = At<char*>(node, table::kNext)) {
        if (At<uint32_t>(node, table::kKey) == id)
            return node;
    }
    return nullptr;
}

// One comma-separated part of `rest` split off into a new string (an
// 8-byte buffer, SplitAtChar), its number (SC4::TextToInt); `more` is what
// the split answered.
struct Part {
    GameString token{};
    int32_t number = 0;
    uint8_t more = 0;
};

void SplitPart(GameString& rest, Part& part)
{
    GameString& t = part.token;
    t.begin = nullptr;
    t.end = nullptr;
    t.capacity = nullptr;
    char* const buffer = Allocate(8);
    t.capacity = buffer + 8;
    t.begin = buffer;
    t.end = buffer;
    *buffer = 0;
    t.vtable = kStringVtable;
    t.references = 0;
    part.more = CallThis<uint8_t, uint32_t, GameString*>(addr::SC4String_SplitAtChar, &rest, uint32_t{','}, &t);
    part.number = CallCdecl<int32_t, const char*>(addr::SC4_TextToInt, t.begin);
}

void DropPart(Part& part)
{
    part.token.vtable = kStringVtable;
    FreeIfSet(part.token.begin);
}

// 0x006416B0 (cdecl: key, value (cIGZStrings), the RUL0 loader's context;
// passed to the ini's slot 0x34 by 0x00641A50). The two key strings made
// (MakeOrderingKeys); the value copied (CopyBuffer, null for a null value);
// the key compared (its slot 0x28, case flag 0) with:
//   RotationRing: the context's +0x18 set to -1; for each part of the value
//     (split at ','; TextToInt): the context's +0x18 set to it while it is
//     negative; the store's rule of it (the context's +0x4) linked into a
//     ring after the previous rule: rule +0xEC the previous one's ID, +0xF0
//     the first one's, the previous one's +0xF0 and the first one's +0xEC
//     this rule's ID;
//   AddTypes: the ring's start X read from the context's +0x18; for each
//     part: the context's +0x18 set to it until a rule is found; the rule
//     linked as above into a ring of its own, and when X has a rule R: R's
//     +0xF8 rule S (FindOrInsert 0x0062C2F0: inserted when missing), this
//     rule's +0xF4 R's ID and +0xF8 S's, R's +0xF8 and S's +0xF4 this rule's
//     ID, X = R's +0xF0.
// The parts' strings and the copy freed.
void __cdecl Rul0OrderingLine(void* key, void* value, char* context)
{
    static bool logged = false;
    FirstCall("Rul0OrderingLine", logged);
    MakeOrderingKeys();
    GameString rest{};
    char* tag = context;
    CallThis<void, char*, uint32_t, uint32_t, char**>(addr::SC4String_CopyBuffer, reinterpret_cast<char*>(&rest.begin),
                                                      value ? static_cast<char*>(value) + 4 : nullptr, 0u,
                                                      0xFFFFFFFFu, &tag);
    rest.vtable = kStringVtable;
    rest.references = 0;
    if (Virtual<uint8_t>(key, 0x28, reinterpret_cast<void*>(addr::cSC4NetworkTool_Rul0RotationRingKey), 0u)) {
        char* first = nullptr;
        char* previous = nullptr;
        At<int32_t>(context, rul0ctx::kWord18) = -1;
        for (;;) {
            Part part;
            SplitPart(rest, part);
            if (At<int32_t>(context, rul0ctx::kWord18) < 0)
                At<int32_t>(context, rul0ctx::kWord18) = part.number;
            char* const node = StoreNode(At<char*>(context, rul0ctx::kStore), static_cast<uint32_t>(part.number));
            if (node) {
                char* const rule = node + store::kRule;
                if (previous) {
                    At<uint32_t>(rule, 0xEC) = At<uint32_t>(previous, rul0::kId);
                    At<uint32_t>(rule, 0xF0) = At<uint32_t>(first, rul0::kId);
                    At<uint32_t>(previous, 0xF0) = At<uint32_t>(rule, rul0::kId);
                    At<uint32_t>(first, 0xEC) = At<uint32_t>(rule, rul0::kId);
                }
                previous = rule;
                if (!first)
                    first = rule;
            }
            DropPart(part);
            if (!part.more)
                break;
        }
    } else if (Virtual<uint8_t>(key, 0x28, reinterpret_cast<void*>(addr::cSC4NetworkTool_Rul0AddTypesKey), 0u)) {
        char* first = nullptr;
        char* previous = nullptr;
        uint32_t ring = At<uint32_t>(context, rul0ctx::kWord18);
        for (;;) {
            Part part;
            SplitPart(rest, part);
            if (!first)
                At<int32_t>(context, rul0ctx::kWord18) = part.number;
            char* const node = StoreNode(At<char*>(context, rul0ctx::kStore), static_cast<uint32_t>(part.number));
            if (node) {
                char* const rule = node + store::kRule;
                if (previous) {
                    At<uint32_t>(rule, 0xEC) = At<uint32_t>(previous, rul0::kId);
                    At<uint32_t>(rule, 0xF0) = At<uint32_t>(first, rul0::kId);
                    At<uint32_t>(previous, 0xF0) = At<uint32_t>(rule, rul0::kId);
                    At<uint32_t>(first, 0xEC) = At<uint32_t>(rule, rul0::kId);
                }
                if (!first)
                    first = rule;
                previous = rule;
                char* const storeNow = At<char*>(context, rul0ctx::kStore);
                char* const ringNode = StoreNode(storeNow, ring);
                if (ringNode) {
                    char* const r = ringNode + store::kRule;
                    uint32_t next = At<uint32_t>(r, 0xF8);
                    char* const s = CallThis<char*, uint32_t*>(addr::SC4Rul0Store_FindOrInsert, storeNow, &next);
                    At<uint32_t>(rule, 0xF4) = At<uint32_t>(r, rul0::kId);
                    At<uint32_t>(rule, 0xF8) = At<uint32_t>(s, rul0::kId);
                    At<uint32_t>(r, 0xF8) = At<uint32_t>(rule, rul0::kId);
                    At<uint32_t>(s, 0xF4) = At<uint32_t>(rule, rul0::kId);
                    ring = At<uint32_t>(r, 0xF0);
                }
            }
            DropPart(part);
            if (!part.more)
                break;
        }
    }
    rest.vtable = kStringVtable;
    FreeIfSet(rest.begin);
}

// 0x00641A50 (cdecl: a section name (a game string), an empty string, the
// RUL0 loader's context; passed to the ini's slot 0x38 by LoadRul0). A
// section beginning with 'Ordering': its lines through 0x006416B0 (the
// ini's slot 0x34 with the context). One beginning with
// 'HighwayIntersectionInfo_': the rest of the name (cRZBaseString::Mid from
// 24) read as a number (SC4::TextToUint32), the store's rule of it
// (FindOrInsert 0x0062C2F0) made the context's rule (+0x8) with the ID in
// +0x0 and +0xEC to +0xF8; the context's CellLayout and ConsLayout lists
// cleared (0x0043ABA0); the section's lines through the RUL0 line callback
// 0x00640320; then the rule's ParseCellLayout / ParseConsLayout of each
// list that is not empty. Anything else: nothing.
void __cdecl Rul0Section(void* section, void* /*empty*/, char* context)
{
    static bool logged = false;
    FirstCall("Rul0Section", logged);
    const auto begins = [&](std::uintptr_t text, uint32_t length) {
        return CallThis<uint8_t, const char*, uint32_t>(addr::cRZBaseString_BeginsWith, section, Text(text), length) !=
               0;
    };
    if (begins(kTextOrdering, kOrderingLength)) {
        Virtual<uint8_t>(At<void*>(context, rul0ctx::kIni), 0x34, section,
                         static_cast<std::uintptr_t>(addr::cSC4NetworkTool_Rul0OrderingLineCallback), context);
        return;
    }
    if (!begins(kTextIntersection, kIntersectionLength))
        return;
    GameString rest{};
    char* const made = CallThis<char*, GameString*, uint32_t, uint32_t>(addr::cRZBaseString_Mid, section, &rest,
                                                                         kIntersectionLength, 0xFFFFFFFFu);
    uint32_t id = CallCdecl<uint32_t, const char*>(addr::SC4_TextToUint32, At<char*>(made, 4));
    rest.vtable = kStringVtable;
    FreeIfSet(rest.begin);
    char* const rule =
        CallThis<char*, uint32_t*>(addr::SC4Rul0Store_FindOrInsert, At<char*>(context, rul0ctx::kStore), &id);
    At<char*>(context, rul0ctx::kRule) = rule;
    At<uint32_t>(rule, rul0::kId) = id;
    for (std::size_t i = 0; i < 4; ++i)
        At<uint32_t>(rule, rul0::kWordsEC + 4 * i) = id;
    char* const cells = context + rul0ctx::kCellLines;
    char* const cons = context + rul0ctx::kConsLines;
    CallThis<void>(addr::SC4UI_F43ABA0, cells);
    CallThis<void>(addr::SC4UI_F43ABA0, cons);
    Virtual<uint8_t>(At<void*>(context, rul0ctx::kIni), 0x34, section,
                     static_cast<std::uintptr_t>(addr::cSC4NetworkTool_Rul0LineCallback), context);
    char* const cellHead = At<char*>(cells, 0);
    if (At<char*>(cellHead, 0) != cellHead)
        CallThis<void, char*>(addr::SC4Rul0Rule_ParseCellLayout, rule, cells);
    char* const consHead = At<char*>(cons, 0);
    if (At<char*>(consHead, 0) != consHead)
        CallThis<void, char*>(addr::SC4Rul0Rule_ParseConsLayout, rule, cons);
}

// 0x00641B60 (cdecl: the RUL 0x10000000 ini, the RUL0 store): a context on
// the stack (the ini, the store, three empty string lists (0x1C-byte heads
// linked to themselves), +0x18 -1; the rule word +0x8 is not written: the
// section callback sets it before anything reads it), the store cleared
// (0x0062A3C0), the ini's sections through 0x00641A50 (slot 0x38), the
// lists destroyed (0x0063C470).
void __cdecl LoadRul0(void* ini, char* store)
{
    static bool logged = false;
    FirstCall("LoadRul0", logged);
    alignas(4) char context[rul0ctx::kSize] = {};
    for (const std::size_t list : {rul0ctx::kCellLines, rul0ctx::kConsLines, rul0ctx::kList14}) {
        char* const head = Allocate(rul0ctx::kListNodeSize);
        At<char*>(head, 0) = head;
        At<char*>(head, 4) = head;
        At<char*>(context, list) = head;
    }
    At<void*>(context, rul0ctx::kIni) = ini;
    At<char*>(context, rul0ctx::kStore) = store;
    At<int32_t>(context, rul0ctx::kWord18) = -1;
    CallThis<void>(addr::SC4Rul0Store_Clear, store);
    Virtual<uint8_t>(ini, 0x38, static_cast<std::uintptr_t>(addr::cSC4NetworkTool_Rul0SectionCallback), context);
    CallThis<void>(addr::SC4Rul0LoaderLists_Destruct, context);
}

// ====================================================================================
// simulators.nettool.rulparse3: the RUL1 loader
// ====================================================================================

// 0x00641D30 (cdecl: the RUL 0x10000001 ini): the RUL1 map 0x00B460DC
// emptied when it has nodes (EraseSubtree 0x00635C60 from the root; the
// head relinked, the count 0); then for every pair of network types a < b
// (0 <= a < b <= 12): an empty pair table with the bucket count the prime
// table (0x00A829B0) gives for 100 (Reserve, FillInsert of nulls), a value
// {a, b, a copy of it (0x0063CAA0)} inserted (InsertUnique 0x00640EF0),
// both tables cleared and freed, the pair's table (FindOrInsert 0x00641C40)
// filled from the ini (ReadRul1Pairs 0x0063E340 with a, b).
void __cdecl LoadRul1(void* ini)
{
    static bool logged = false;
    FirstCall("LoadRul1", logged);
    char* const map = reinterpret_cast<char*>(addr::cSC4NetworkTool_Rul1Map);
    if (At<uint32_t>(map, tree::kCount)) {
        CallThis<void, char*>(addr::SC4Rul1Map_EraseSubtree, map,
                              At<char*>(At<char*>(map, tree::kHead), tree::kParent));
        At<char*>(At<char*>(map, tree::kHead), tree::kLeft) = At<char*>(map, tree::kHead);
        At<char*>(At<char*>(map, tree::kHead), tree::kParent) = nullptr;
        At<char*>(At<char*>(map, tree::kHead), tree::kRight) = At<char*>(map, tree::kHead);
        At<uint32_t>(map, tree::kCount) = 0;
    }
    const auto* const primes = reinterpret_cast<const uint32_t*>(addr::SC4HashTable_Primes);
    const auto* const primesEnd = reinterpret_cast<const uint32_t*>(addr::SC4HashTable_PrimesEnd);
    for (int32_t a = 0; a + 1 != static_cast<int32_t>(kRul1Types); ++a) {
        uint32_t zero = 0;
        for (int32_t b = a + 1; b != static_cast<int32_t>(kRul1Types); ++b) {
            alignas(4) char empty[table::kSize] = {};
            const uint32_t* p = primes;
            int32_t n = static_cast<int32_t>(primesEnd - primes);
            do {
                const int32_t half = n >> 1;
                if (p[half] < table::kFirstBuckets) {
                    p += half + 1;
                    n += -1 - half;
                } else {
                    n = half;
                }
            } while (n > 0);
            const uint32_t buckets = p == primesEnd ? 0xFFFFFFFBu : *p;
            Vector& v = At<Vector>(empty, table::kBuckets);
            CallThis<void, uint32_t>(addr::PtrVector_Reserve, &v, buckets);
            CallThis<void, char*, uint32_t, uint32_t*>(addr::SC4PtrVector_FillInsert, &v, v.end, buckets, &zero);
            alignas(4) char value[rul1map::kTable - rul1map::kFirst + table::kSize] = {};
            At<int32_t>(value, 0) = a;
            At<int32_t>(value, 4) = b;
            char* const copy = value + rul1node::kValueTable;
            CallThis<void, char*>(addr::SC4PairHashTable_CopyFrom, copy, empty);
            alignas(4) char result[8] = {};
            CallThis<char*, char*, char*>(addr::SC4Rul1Map_InsertUnique, map, result, value);
            CallThis<void>(addr::SC4Uint32HashSet_Clear, copy);
            FreeIfSet(At<char*>(copy, table::kBuckets));
            CallThis<void>(addr::SC4Uint32HashSet_Clear, empty);
            FreeIfSet(At<char*>(empty, table::kBuckets));
            int32_t key[2] = {a, b};
            char* const pairs = CallThis<char*, int32_t*>(addr::SC4Rul1Map_FindOrInsert, map, key);
            CallCdecl<void, void*, int32_t, int32_t, char*>(addr::cSC4NetworkTool_ReadRul1Pairs, ini, a, b, pairs);
        }
    }
}

// ====================================================================================
// simulators.nettool.rulparse3: the type row's Clear and the bridge loaders
// ====================================================================================

// 0x006421D0 (no arguments): the rule files (+0x24) deleted from the last
// (slot 0 with 1 where not null; the end moved back one each), the bridge
// records erased (0x00641EB0 over the whole vector), then the table +0x3C
// destroyed (a jump to 0x0063D760).
void __fastcall TypeRowClear(char* self, void*)
{
    static bool logged = false;
    FirstCall("TypeRowClear", logged);
    Vector& files = At<Vector>(self, typerow::kFiles);
    while (files.begin != files.end) {
        void* const file = *reinterpret_cast<void**>(files.end - 4);
        if (file)
            Virtual<void, uint32_t>(file, 0x0, 1u);
        files.end -= 4;
    }
    Vector& bridges = At<Vector>(self, typerow::kBridges);
    CallThis<char*, char*, char*>(addr::SC4BridgeRecordVector_Erase, &bridges, bridges.begin, bridges.end);
    CallThis<void>(addr::SC4NetworkTypeInfo_DestroyTable3C, self + typerow::kTable);
}

// A variant of a property of the holder: its slot 0x24 (the property or
// null), the property's slot 0x14.
void* VariantOf(void* holder, uint32_t property)
{
    void* const p = Virtual<void*>(holder, 0x24, property);
    return p ? Virtual<void*>(p, 0x14) : nullptr;
}

uint8_t GetUint32(void* holder, uint32_t property, char* out)
{
    return CallCdecl<uint8_t, void*, uint32_t, char*>(addr::SC4_GetPropertyUint32, holder, property, out);
}
uint8_t GetFloat(void* holder, uint32_t property, char* out)
{
    return CallCdecl<uint8_t, void*, uint32_t, char*>(addr::SC4_GetPropertyFloat, holder, property, out);
}
uint8_t GetFloats(void* holder, uint32_t property, void* out)
{
    return CallCdecl<uint8_t, void*, uint32_t, void*>(addr::SC4_GetPropertyFloatVector, holder, property, out);
}

// A float vector's first eight words turned by 1 to 3 quarter turns about
// (0, 0) (SC4::RotatePoint 0x005D77A0; the angle float(i * pi / 2)), each
// turned point's two words appended (in place or grown as the game
// inlines it).
void TurnPoints(Vector& v)
{
    float centre[2] = {0.0f, 0.0f};
    uint32_t points[8];
    for (int i = 0; i < 4; ++i) {
        points[2 * i] = reinterpret_cast<const uint32_t*>(v.begin)[2 * i];
        points[2 * i + 1] = reinterpret_cast<const uint32_t*>(v.begin)[2 * i + 1];
    }
    for (int32_t turn = 1; turn < 4; ++turn) {
        const float angle =
            static_cast<float>(static_cast<double>(turn) * static_cast<double>(Global<float>(addr::SC4_FloatHalfPi)));
        for (int p = 0; p < 4; ++p) {
            uint32_t out[2] = {};
            CallCdecl<void, float*, uint32_t*, uint32_t, uint32_t*>(addr::SC4_RotatePoint, centre, points + 2 * p,
                                                                     Bits(angle), out);
            PushFloatInline(v, out[0]);
            PushFloatInline(v, out[1]);
        }
    }
}

// Copies the first words of a variant's array into a vector of `stride`
// bytes (at `offset` in each element): min(count, limit) of them.
void CopyVariantWords(const uint32_t* words, uint32_t count, uint32_t limit, char* begin, std::size_t stride)
{
    const uint32_t n = count > limit ? limit : count;
    for (uint32_t i = 0; i < n; ++i)
        At<uint32_t>(begin, stride * i) = words[i];
}

// The bytes of a variant into the points' byte +4: the count's low byte, or
// the low byte of the two counts' bytes' product when it is larger than
// their product.
void CopyVariantBytes(const uint8_t* bytes, uint32_t count, char* record, std::size_t countAt, char* points)
{
    const uint32_t limit = At<uint32_t>(record, countAt) * At<uint32_t>(record, bridgefields::kWordC);
    uint8_t n = static_cast<uint8_t>(count);
    if (static_cast<uint32_t>(n) > limit)
        n = static_cast<uint8_t>(At<uint8_t>(record, countAt) * At<uint8_t>(record, bridgefields::kWordC));
    for (int32_t i = 0; i < static_cast<int32_t>(n); ++i)
        At<uint8_t>(points, 8 * static_cast<std::size_t>(i) + 4) = bytes[i];
}

// The fields of a bridge record from its exemplar's property holder, in the
// game's order (0x006423E3 to 0x006432D4); see BridgeLine.
void ReadBridgeExemplar(char* record, void* holder)
{
    using namespace bridgefields;
    if (void* const v = VariantOf(holder, 0x482CB441))
        Virtual<uint8_t>(v, 0x94, record + kFloat5C);
    if (!GetUint32(holder, 0x482CB45F, record + kWordC))
        At<uint32_t>(record, kWordC) = 1;
    if (!GetUint32(holder, 0x482CB442, record + kCount10))
        At<uint32_t>(record, kCount10) = 0;
    CallThis<void, uint32_t>(addr::SC4PointVector_Resize, record + 0x14,
                             At<uint32_t>(record, kCount10) * At<uint32_t>(record, kWordC));
    uint32_t zero = 0;
    using ResizeFn = void(__thiscall*)(void*, uint32_t, uint32_t*);
    const auto resize = reinterpret_cast<ResizeFn>(addr::SC4FloatVector_Resize2);
    resize(record + 0x60, At<uint32_t>(record, kCount10) + 1, &zero);
    zero = 0;
    resize(record + 0x6C, At<uint32_t>(record, kCount10), &zero);
    zero = 0;
    resize(record + 0x20, At<uint32_t>(record, kCount10), &zero);
    if (At<uint32_t>(record, kCount10) == 0) {
        At<uint32_t>(record, kWord7C) = 0;
    } else {
        if (void* const v = VariantOf(holder, 0x482CB443)) {
            const uint32_t* const words = Virtual<uint32_t*>(v, 0x19C);
            const uint32_t count = Virtual<uint32_t>(v, 0x10);
            CopyVariantWords(words, count, At<uint32_t>(record, kCount10) * At<uint32_t>(record, kWordC),
                             At<char*>(record, 0x14), 8);
        }
        if (void* const v = VariantOf(holder, 0x482CB444)) {
            const uint8_t* const bytes = Virtual<uint8_t*>(v, 0x17C);
            const uint32_t count = Virtual<uint32_t>(v, 0x10);
            CopyVariantBytes(bytes, count, record, kCount10, At<char*>(record, 0x14));
        }
        if (void* const v = VariantOf(holder, 0x482CB44D)) {
            const uint32_t* const words = Virtual<uint32_t*>(v, 0x1BC);
            uint32_t count = Virtual<uint32_t>(v, 0x10);
            if (count > At<uint32_t>(record, kCount10))
                count = At<uint32_t>(record, kCount10);
            for (uint32_t i = 0; i < count; ++i)
                At<uint32_t>(At<char*>(record, 0x20), 4 * i) = words[i];
            At<uint32_t>(record, kWord7C) = 0;
            // FCOMP with 0.0, TEST AH,1: below or unordered skips.
            for (uint32_t i = 1; i < count; ++i) {
                if (At<float>(At<char*>(record, 0x20), 4 * i) >= 0.0f)
                    At<uint32_t>(record, kWord7C) = i;
            }
        }
    }
    if (void* const v = VariantOf(holder, 0x482CB445))
        Virtual<uint8_t>(v, 0x64, record + kWord2C);
    if (void* const v = VariantOf(holder, 0x482CB446))
        Virtual<uint8_t>(v, 0x34, record + kByte30);
    At<uint32_t>(record, kCount40) = 0;
    At<uint32_t>(record, kWord34) = 0;
    At<uint32_t>(record, kWord38) = 0;
    void* const counts = VariantOf(holder, 0x482CB448);
    if (counts) {
        Virtual<uint8_t>(counts, 0x64, record + kCount40);
    }
    if (counts && At<uint32_t>(record, kCount40) != 0) {
        // The points +0x44 resized to +0xC * +0x40 ((0, 0) for new ones;
        // a shrink only moves the end), the floats +0x50 to +0x40 (0).
        const uint32_t want = At<uint32_t>(record, kWordC) * At<uint32_t>(record, kCount40);
        Vector& points = At<Vector>(record, 0x44);
        uint32_t pair[2] = {0, 0};
        const uint32_t size = static_cast<uint32_t>(static_cast<int32_t>(points.end - points.begin) >> 3);
        if (want < size) {
            points.end = points.begin + static_cast<std::size_t>(want) * 8;
        } else {
            CallThis<void, char*, uint32_t, uint32_t*>(addr::SC4Vector2Vector_InsertN, &points, points.end,
                                                       want - size, pair);
        }
        Vector& floats = At<Vector>(record, 0x50);
        const uint32_t count = At<uint32_t>(record, kCount40);
        uint32_t fill = 0;
        const uint32_t floatsSize = static_cast<uint32_t>(static_cast<int32_t>(floats.end - floats.begin) >> 2);
        if (count < floatsSize) {
            floats.end = floats.begin + static_cast<std::size_t>(count) * 4;
        } else {
            CallThis<void, char*, uint32_t, uint32_t*>(addr::SC4FloatVector_InsertN, &floats, floats.end,
                                                       count - floatsSize, &fill);
        }
        if (void* const v = VariantOf(holder, 0x482CB447))
            Virtual<uint8_t>(v, 0x64, record + kWord34);
        if (!GetUint32(holder, 0x482CB459, record + kWord38))
            At<uint32_t>(record, kWord38) = At<uint32_t>(record, kWord34);
        At<uint32_t>(record, kFloat3C) = 0;
        GetFloat(holder, 0x482CD465, record + kFloat3C);
        if (void* const v = VariantOf(holder, 0x482CB449)) {
            const uint32_t* const words = Virtual<uint32_t*>(v, 0x19C);
            const uint32_t n = Virtual<uint32_t>(v, 0x10);
            CopyVariantWords(words, n, At<uint32_t>(record, kCount40) * At<uint32_t>(record, kWordC),
                             At<char*>(record, 0x44), 8);
        }
        if (void* const v = VariantOf(holder, 0x482CB44A)) {
            const uint8_t* const bytes = Virtual<uint8_t*>(v, 0x17C);
            const uint32_t n = Virtual<uint32_t>(v, 0x10);
            CopyVariantBytes(bytes, n, record, kCount40, At<char*>(record, 0x44));
        }
        if (void* const v = VariantOf(holder, 0x482CB44C)) {
            const uint32_t* const words = Virtual<uint32_t*>(v, 0x1BC);
            const uint32_t n = Virtual<uint32_t>(v, 0x10);
            CopyVariantWords(words, n, At<uint32_t>(record, kCount40), At<char*>(record, 0x50), 4);
        }
    }
    GetFloats(holder, 0x482CB44E, record + 0x80);
    At<uint32_t>(record, kWord78) = 0;
    if (At<uint32_t>(record, kCount10) != 0) {
        if (void* const v = VariantOf(holder, 0x482CB44B)) {
            const float* const pairs = Virtual<float*>(v, 0x1BC);
            Virtual<uint32_t>(v, 0x10);
            At<uint32_t>(At<char*>(record, 0x60), 4 * At<uint32_t>(record, kCount10)) = 0;
            for (int32_t i = static_cast<int32_t>(At<uint32_t>(record, kCount10) - 1); i >= 0; --i) {
                float* const f60 = At<float*>(record, 0x60);
                float* const f6C = At<float*>(record, 0x6C);
                X87SubAddStore(&pairs[2 * i + 1], &pairs[2 * i], &f60[i + 1], &f60[i]);
                X87AddStore(&f60[i + 1], &pairs[2 * i + 1], &f6C[i]);
            }
            At<uint32_t>(record, kWord78) = At<uint32_t>(At<char*>(record, 0x60), 0);
        }
    }
    if (!GetFloats(holder, 0x482CB45E, record + 0x8C))
        CallThis<void*, char*>(addr::SC4FloatVector_Assign, record + 0x8C, record + 0x80);
    TurnPoints(At<Vector>(record, 0x80));
    TurnPoints(At<Vector>(record, 0x8C));
    if (!GetUint32(holder, 0x482CB458, record + kMinLength))
        At<uint32_t>(record, kMinLength) = At<uint32_t>(record, kCount10) * 2 + 2;
    if (!GetUint32(holder, 0x482CB467, record + kMaxLength))
        At<uint32_t>(record, kMaxLength) = 100000;
    CallCdecl<uint8_t, void*, uint32_t, char*>(addr::SC4_GetPropertyBool, holder, 0x482CB44F, record + kByte98);
    CallCdecl<uint8_t, void*, uint32_t, char*>(addr::SC4_GetPropertyBool, holder, 0x482CB450, record + kByte99);
    struct Default {
        bool isFloat;
        uint32_t property;
        std::size_t at;
        uint32_t value;
    };
    static constexpr Default kDefaults[] = {
        {true, 0x482CB455, 0x9C, 0},  {true, 0x482CB456, 0xA0, 0},  {true, 0x482CB457, 0xA4, 0},
        {false, 0x482CB45A, 0xA8, 0}, {false, 0x482CB45B, 0xAC, 100}, {false, 0x482CB45C, 0xB0, 0},
        {false, 0x482CB45D, 0xB4, 0}, {true, 0x482CB471, 0xB8, 0},  {true, 0x482CD460, 0xBC, 0},
        {true, 0x482CD461, 0xC0, 0},  {true, 0x482CD462, 0xC4, 0},  {true, 0x482CD463, 0xC8, 0},
        {true, 0x482CD464, 0xCC, 0x461C4000}, // 10000.0f
        {true, 0x482CB468, 0xD0, 0},  {true, 0x482CB472, 0xD4, 0},
    };
    for (const Default& d : kDefaults) {
        const uint8_t got = d.isFloat ? GetFloat(holder, d.property, record + d.at)
                                      : GetUint32(holder, d.property, record + d.at);
        if (!got)
            At<uint32_t>(record, d.at) = d.value;
    }
}

// 0x00642220 (cdecl: key, value (cIGZStrings), a type row's bridge record
// vector +0x30; passed to the ini's slot 0x34 by LoadBridges). Four empty
// strings (8-byte buffers); a record built on the stack (0x00640030; the
// original's temporary is its stack leftovers but for what the constructor
// writes, ours starts zeroed) appended (copied in place by 0x00640FA0, or
// through InsertOverflow 0x00642030 when full) and destroyed; the new
// record's ID from the key (TextToUint32). The value split at ',' into the
// four strings (SC4::SplitText): unless four parts, done. The resource of
// the key {TextToUint32 of the first three} (the manager's slot 0xC, IID
// 0xA52160F5): none: the record taken off again (the end moved back, the
// record destroyed). Else the name the fourth part (AssignRange), the
// exemplar's holder (slot 0x1C) read into the record (ReadBridgeExemplar):
//   0x482CB441 +0x5C (GetValFloat32); 0x482CB45F +0xC (1), 0x482CB442 +0x10
//   (0); the points +0x14 resized to +0x10 * +0xC, the floats +0x60 to
//   +0x10 + 1, +0x6C and +0x20 to +0x10; with +0x10 zero, +0x7C 0; else
//   0x482CB443 words into the points (min of the counts), 0x482CB444 bytes
//   into their byte +4 (a byte count, as the game), 0x482CB44D floats into
//   +0x20 with +0x7C the last index >= 1 of a float >= 0 (0 when none);
//   0x482CB445 +0x2C (GetValUint32), 0x482CB446 the byte +0x30
//   (GetValUint8); +0x40, +0x34, +0x38 0; 0x482CB448 +0x40, and when that is
//   not zero: the points +0x44 resized to +0xC * +0x40, the floats +0x50 to
//   +0x40, 0x482CB447 +0x34, 0x482CB459 +0x38 (+0x34), +0x3C 0 then
//   0x482CD465, 0x482CB449 / 0x482CB44A / 0x482CB44C into the points and
//   the floats; 0x482CB44E the floats +0x80, +0x78 0; with +0x10 not zero
//   and 0x482CB44B: +0x60[+0x10] 0 and from the last down +0x60[i] = (b - a)
//   + +0x60[i + 1], +0x6C[i] = +0x60[i + 1] + b for the pair (a, b) i of the
//   floats, +0x78 = +0x60[0]; 0x482CB45E the floats +0x8C (else a copy of
//   +0x80); both turned (TurnPoints); 0x482CB458 +0x4 (2 * +0x10 + 2),
//   0x482CB467 +0x8 (100000), 0x482CB44F / 0x482CB450 the bytes +0x98 /
//   +0x99, the words +0x9C to +0xD4 (defaults 0, 100 for +0xAC, 10000.0 for
//   +0xCC);
// then 0x482CD466: a localized name (SC4LocalizedString of its two words)
// assigned to the name; 0x482CB46F / 0x482CB470 two words each into +0x148
// / +0x150 (zeros without); +0xD8 0, then the chain 0x482CB46B +0xDC,
// 0x482CB46C +0xE0, the float vectors 0x482CB469, 0x482CB46A, 0x482CB46D
// (their first six words into +0xE4, +0xFC, +0x114), +0x12C to +0x140
// derived (X87BridgeAngles, +0x134 / +0x140 the words +0x11C / +0x128),
// 0x482CB46E +0x144, and +0xD8 1 when every step answered. The resource
// released, the vector and the strings freed.
void __cdecl BridgeLine(void* key, void* value, Vector* records)
{
    static bool logged = false;
    FirstCall("BridgeLine", logged);
    GameString parts[4];
    for (GameString& s : parts) {
        s.begin = nullptr;
        s.end = nullptr;
        s.capacity = nullptr;
        char* const buffer = Allocate(8);
        s.begin = buffer;
        s.end = buffer;
        s.capacity = buffer + 8;
        *buffer = 0;
        s.vtable = kStringVtable;
        s.references = 0;
    }
    const auto dropParts = [&]() {
        for (int i = 3; i >= 0; --i) {
            parts[i].vtable = kStringVtable;
            FreeIfSet(parts[i].begin);
        }
    };
    uint32_t count = 4;
    uint32_t resourceKey[3] = {0, 0, 0};
    alignas(8) char temporary[kRecordSize] = {};
    char* const made = CallThis<char*>(addr::SC4NetworkBridgeRecord_Construct, temporary);
    if (records->end != records->capacity) {
        if (records->end)
            CallThis<char*, char*>(addr::SC4NetworkBridgeRecord_CopyConstruct, records->end, made);
        records->end += kRecordSize;
    } else {
        uint8_t tag = 0;
        CallThis<char*, char*, char*, uint8_t*, uint32_t, uint32_t>(addr::SC4BridgeRecordVector_InsertOverflow,
                                                                     records, records->end, made, &tag, 1u, 1u);
    }
    CallThis<void>(addr::SC4NetworkBridgeRecord_Destruct, temporary);
    char* const record = records->end - kRecordSize;
    At<uint32_t>(record, bridgefields::kId) =
        CallCdecl<uint32_t, const char*>(addr::SC4_TextToUint32, At<char*>(key, 4));
    const char* const text = At<char*>(value, 4);
    CallCdecl<uint8_t, const char*, uint32_t, uint32_t, GameString*, uint32_t*>(
        addr::SC4_SplitText, text, static_cast<uint32_t>(At<char*>(value, 8) - text), uint32_t{','}, parts, &count);
    if (count != 4) {
        dropParts();
        return;
    }
    resourceKey[0] = CallCdecl<uint32_t, const char*>(addr::SC4_TextToUint32, parts[0].begin);
    resourceKey[1] = CallCdecl<uint32_t, const char*>(addr::SC4_TextToUint32, parts[1].begin);
    resourceKey[2] = CallCdecl<uint32_t, const char*>(addr::SC4_TextToUint32, parts[2].begin);
    void* resource = nullptr;
    void* const manager = Global<void*>(addr::SC4_ResourceManager);
    if (!Virtual<uint8_t>(manager, 0xC, resourceKey, kResourceIid, &resource, 0u, 0u)) {
        records->end -= kRecordSize;
        CallThis<void>(addr::SC4NetworkBridgeRecord_Destruct, records->end);
        dropParts();
        return;
    }
    uint8_t tag = 0;
    if (record + bridgerecord::kNameBegin != reinterpret_cast<char*>(&parts[3].begin))
        CallThis<void, char*, char*, uint8_t*>(addr::SC4String_AssignRange, record + bridgerecord::kNameBegin,
                                               parts[3].begin, parts[3].end, &tag);
    void* const holder = Virtual<void*>(resource, 0x1C);
    ReadBridgeExemplar(record, holder);
    if (void* const v = VariantOf(holder, 0x482CD466)) {
        const uint32_t* const words = Virtual<uint32_t*>(v, 0x19C);
        alignas(4) char localized[0x20] = {};
        char* const got = CallThis<char*, uint32_t, uint32_t, uint32_t>(
            addr::SC4LocalizedString_SC4LocalizedString, localized, words[0], words[1], 1u);
        char* const body = got ? got + 4 : nullptr;
        if (body != record + bridgerecord::kNameBegin)
            CallThis<void, char*, char*, uint8_t*>(addr::SC4String_AssignRange, record + bridgerecord::kNameBegin,
                                                   At<char*>(body, 0), At<char*>(body, 4), &tag);
        At<std::uintptr_t>(localized, 0) = kStringVtable;
        FreeIfSet(At<char*>(localized, 4));
    }
    using namespace bridgefields;
    for (const auto& keys : {std::pair<uint32_t, std::size_t>{0x482CB46F, kKeys148},
                             std::pair<uint32_t, std::size_t>{0x482CB470, kKeys150}}) {
        if (void* const v = VariantOf(holder, keys.first)) {
            At<uint32_t>(record, keys.second) = Virtual<uint32_t*>(v, 0x19C)[0];
            At<uint32_t>(record, keys.second + 4) = Virtual<uint32_t*>(v, 0x19C)[1];
        } else {
            At<uint32_t>(record, keys.second + 4) = 0;
            At<uint32_t>(record, keys.second) = 0;
        }
    }
    Vector floats{};
    At<uint8_t>(record, kChained) = 0;
    const auto sextet = [&](uint32_t property, std::size_t at) {
        if (!GetFloats(holder, property, &floats))
            return false;
        for (std::size_t i = 0; i < 6; ++i)
            At<uint32_t>(record, at + 4 * i) = reinterpret_cast<const uint32_t*>(floats.begin)[i];
        return true;
    };
    if (GetUint32(holder, 0x482CB46B, record + kWordDC) && GetFloat(holder, 0x482CB46C, record + kFloatE0) &&
        sextet(0x482CB469, kSextets) && sextet(0x482CB46A, kSextets + 0x18) && sextet(0x482CB46D, kSextets + 0x30)) {
        At<uint32_t>(record, 0x134) = At<uint32_t>(record, 0x11C);
        X87BridgeAngles(record, &Global<float>(addr::SC4_FloatMinusOne));
        At<uint32_t>(record, 0x140) = At<uint32_t>(record, 0x128);
        if (GetUint32(holder, 0x482CB46E, record + kWord144))
            At<uint8_t>(record, kChained) = 1;
    }
    ReleaseOf(resource);
    FreeIfSet(floats.begin);
    dropParts();
}

// 0x00641F00 (ret 8: the bridge record vector*, the divisor; EAX this): the
// RUL0 lexer's base (vtable 0x00AA8C58, pushed token -1, an empty
// identifier with an 8-byte buffer), the bridge lexer's vtable 0x00AA8EB4,
// +0x28 the records, +0x2C an empty list (a 0x1C-byte head linked to
// itself), +0x30 the divisor. The text's fields +0x4 to +0xC are left to
// Parse.
char* __fastcall BridgeLexerConstruct(char* self, void*, Vector* records, uint32_t divisor)
{
    static bool logged = false;
    FirstCall("BridgeLexerConstruct", logged);
    At<std::uintptr_t>(self, lexer::kVtable) = addr::SC4RulLexer_BaseVtable;
    At<int32_t>(self, lexer::kPushed) = lexer::kNoToken;
    At<char*>(self, lexer::kIdentBegin) = nullptr;
    At<char*>(self, lexer::kIdentEnd) = nullptr;
    At<char*>(self, lexer::kIdentCapacity) = nullptr;
    char* const buffer = Allocate(lexer::kFirstBuffer);
    At<char*>(self, lexer::kIdentBegin) = buffer;
    At<char*>(self, lexer::kIdentEnd) = buffer;
    At<char*>(self, lexer::kIdentCapacity) = buffer + lexer::kFirstBuffer;
    *buffer = 0;
    At<std::uintptr_t>(self, lexer::kVtable) = addr::SC4BridgeLexer_Vtable;
    At<Vector*>(self, bridgelexer5::kRecords) = records;
    At<char*>(self, bridgelexer5::kMaps) = nullptr;
    char* const head = Allocate(bridgelexer5::kListNodeSize);
    At<char*>(head, bridgelexer5::kNext) = head;
    At<char*>(head, bridgelexer5::kPrevious) = head;
    At<char*>(self, bridgelexer5::kMaps) = head;
    At<uint32_t>(self, bridgelexer5::kDivisor) = divisor;
    return self;
}

// 0x00642150 (no arguments): a string map of 100 buckets on the stack
// (0x00829BC0); a list node (0x1C bytes) whose map (+0x8, the words +0x4
// to +0x10 zeroed when the node + 8 is not null) is a copy of it
// (0x0063E7C0), linked last in the list +0x2C; the stack map cleared
// (0x0063DA30) and its buckets freed.
void __fastcall BridgeLexerPushMap(char* self, void*)
{
    static bool logged = false;
    FirstCall("BridgeLexerPushMap", logged);
    alignas(4) char empty[table::kSize] = {};
    CallThis<void, uint32_t>(addr::SC4Uint32HashSet_Construct, empty, table::kFirstBuckets);
    char* const head = At<char*>(self, bridgelexer5::kMaps);
    char* const node = Allocate(bridgelexer5::kListNodeSize);
    char* const map = node + bridgelexer5::kMap;
    if (map) {
        At<uint32_t>(map, table::kBuckets) = 0;
        At<uint32_t>(map, table::kBucketsEnd) = 0;
        At<uint32_t>(map, table::kBucketsCapacity) = 0;
        At<uint32_t>(map, table::kCount) = 0;
        CallThis<void, char*>(addr::SC4StringHashMap_CopyFrom, map, empty);
    }
    char* const last = At<char*>(head, bridgelexer5::kPrevious);
    At<char*>(node, bridgelexer5::kNext) = head;
    At<char*>(node, bridgelexer5::kPrevious) = last;
    At<char*>(last, bridgelexer5::kNext) = node;
    At<char*>(head, bridgelexer5::kPrevious) = node;
    CallThis<void>(addr::SC4StringHashMap_Clear, empty);
    FreeIfSet(At<char*>(empty, table::kBuckets));
}

// 0x00641BF0 (no arguments; this the lexer's list +0x2C): every node's map
// cleared (0x0063DA30), its buckets and the node freed, the head linked to
// itself.
void __fastcall BridgeLexerClearMaps(char* self, void*)
{
    static bool logged = false;
    FirstCall("BridgeLexerClearMaps", logged);
    char* node = At<char*>(At<char*>(self, 0), bridgelexer5::kNext);
    while (node != At<char*>(self, 0)) {
        char* const current = node;
        node = At<char*>(node, bridgelexer5::kNext);
        char* const map = current + bridgelexer5::kMap;
        CallThis<void>(addr::SC4StringHashMap_Clear, map);
        FreeIfSet(At<char*>(map, table::kBuckets));
        Free(current);
    }
    At<char*>(At<char*>(self, 0), bridgelexer5::kNext) = At<char*>(self, 0);
    At<char*>(At<char*>(self, 0), bridgelexer5::kPrevious) = At<char*>(self, 0);
}

// 'section <name>': the name in a game string (the body's 0x00408420 from
// the identifier), the value of the last scope's map (FindOrInsert
// 0x00640E10), the string freed, the layout parsed into it (0x0063EA50 with
// no record); AL what the parser answered. False when the name is not an
// identifier.
bool ParseSection(char* self, uint8_t& ok)
{
    if (NextToken(self) != lexer::kTokenIdentifier)
        return false;
    GameString name{};
    uint8_t tag = 0;
    CallThis<void, char*, char*, uint8_t*>(addr::cSC4Advice_F408420, reinterpret_cast<char*>(&name.begin),
                                           At<char*>(self, lexer::kIdentBegin), At<char*>(self, lexer::kIdentEnd),
                                           &tag);
    name.vtable = kStringVtable;
    name.references = 0;
    char* const last = At<char*>(At<char*>(self, bridgelexer5::kMaps), bridgelexer5::kPrevious);
    char* const value =
        CallThis<char*, GameString*>(addr::SC4StringHashMap_FindOrInsert, last + bridgelexer5::kMap, &name);
    name.vtable = kStringVtable;
    FreeIfSet(name.begin);
    ok = CallThis<uint8_t, char*, char*>(addr::SC4RulLexer_ParseBridgeLayout, self, nullptr, value);
    return true;
}

// 0x00641310 (ret 4: a bridge record*; AL): an integer then the line's end;
// the integer n within [+0x4, +0x8] (unsigned) and not yet a key of the
// record's int map (SC4Sint32Map::Find answers the head): the layout
// parsed (0x0063EA50) into the map's new value for n (0x00640D40); AL 1
// when the parser accepted it.
uint8_t __fastcall BridgeParseLength(char* self, void*, char* record)
{
    static bool logged = false;
    FirstCall("BridgeParseLength", logged);
    if (NextToken(self) != lexer::kTokenInteger)
        return 0;
    if (NextToken(self) != lexer::kTokenEnd)
        return 0;
    uint32_t length = At<uint32_t>(self, lexer::kInteger);
    if (length < At<uint32_t>(record, bridgefields::kMinLength) ||
        length > At<uint32_t>(record, bridgefields::kMaxLength))
        return 0;
    char* const map = record + bridgerecord::kMapHead;
    char* found = nullptr;
    CallThis<char**, char**, uint32_t*>(addr::SC4Sint32Map_Find, map, &found, &length);
    if (found != At<char*>(map, tree::kHead))
        return 0;
    char* const value = CallThis<char*, uint32_t*>(addr::SC4BridgeIntMap_FindOrInsert, map, &length);
    return CallThis<uint8_t, char*, char*>(addr::SC4RulLexer_ParseBridgeLayout, self, record, value) != 0 ? 1 : 0;
}

// 0x00643330 (no arguments; AL): an integer then the line's end; the
// record of +0x28 with that ID (else 0); a scope pushed (0x00642150); then
// until the text's end, line by line (empty lines skipped): 'section'
// (ParseSection), 'length' (0x00641310 with the record), 'end' followed by
// the line's end (done); anything else, or a part refused: 0. Done or at
// the end: the last scope unlinked, its map cleared (0x0063DA30), its
// buckets and node freed; AL 1.
uint8_t __fastcall BridgeParseBridge(char* self, void*)
{
    static bool logged = false;
    FirstCall("BridgeParseBridge", logged);
    if (NextToken(self) != lexer::kTokenInteger)
        return 0;
    if (NextToken(self) != lexer::kTokenEnd)
        return 0;
    Vector* const records = At<Vector*>(self, bridgelexer5::kRecords);
    char* record = records->begin;
    char* const end = records->end;
    const int32_t id = At<int32_t>(self, lexer::kInteger);
    if (record == end)
        return 0;
    while (At<int32_t>(record, bridgefields::kId) != id) {
        record += kRecordSize;
        if (record == end)
            return 0;
    }
    CallThis<void>(addr::SC4BridgeLexer_PushMap, self);
    while (At<char*>(self, lexer::kCursor) < At<char*>(self, lexer::kEnd)) {
        const int32_t token = NextToken(self);
        if (token == lexer::kTokenEnd)
            continue;
        if (token != lexer::kTokenIdentifier)
            return 0;
        uint8_t ok = 0;
        if (IdentIs(self, kTextSection, 7)) {
            if (!ParseSection(self, ok))
                return 0;
        } else if (IdentIs(self, kTextLength, 6)) {
            ok = CallThis<uint8_t, char*>(addr::SC4BridgeLexer_ParseLength, self, record);
        } else if (IdentIs(self, kTextEnd, 3)) {
            if (NextToken(self) != lexer::kTokenEnd)
                return 0;
            break;
        } else {
            return 0;
        }
        if (!ok)
            return 0;
    }
    char* const node = At<char*>(At<char*>(self, bridgelexer5::kMaps), bridgelexer5::kPrevious);
    char* const previous = At<char*>(node, bridgelexer5::kPrevious);
    char* const next = At<char*>(node, bridgelexer5::kNext);
    At<char*>(previous, bridgelexer5::kNext) = next;
    At<char*>(next, bridgelexer5::kPrevious) = previous;
    char* const map = node + bridgelexer5::kMap;
    CallThis<void>(addr::SC4StringHashMap_Clear, map);
    FreeIfSet(At<char*>(map, table::kBuckets));
    Free(node);
    return 1;
}

// 0x00643580 (ret 8: the text, its size; AL): the lexer's text set (+0x4
// and the cursor +0x8 the text, +0xC its end); a scope pushed (0x00642150,
// not popped here); then until the text's end, line by line (empty lines
// skipped): 'stop' (AL 1), 'section' (ParseSection), 'bridge' (0x00643330);
// anything else, or a part refused: AL 0. The end: AL 1.
uint8_t __fastcall BridgeParse(char* self, void*, char* text, uint32_t size)
{
    static bool logged = false;
    FirstCall("BridgeParse", logged);
    At<char*>(self, lexer::kCursor) = text;
    At<char*>(self, lexer::kBegin) = text;
    At<char*>(self, lexer::kEnd) = text + size;
    CallThis<void>(addr::SC4BridgeLexer_PushMap, self);
    while (At<char*>(self, lexer::kCursor) < At<char*>(self, lexer::kEnd)) {
        const int32_t token = NextToken(self);
        if (token == lexer::kTokenEnd)
            continue;
        if (token != lexer::kTokenIdentifier)
            return 0;
        if (IdentIs(self, kTextStop, 4))
            return 1;
        uint8_t ok = 0;
        if (IdentIs(self, kTextSection, 7)) {
            if (!ParseSection(self, ok))
                return 0;
        } else if (IdentIs(self, kTextBridge, 6)) {
            ok = CallThis<uint8_t>(addr::SC4BridgeLexer_ParseBridge, self);
        } else {
            return 0;
        }
        if (!ok)
            return 0;
    }
    return 1;
}

// 0x006436D0 (cdecl: an ID, the row's bridge record vector, the divisor):
// the resource {0x0A5BCF4B, 0xAA5BCF57, the ID} (SC4::OpenResourceRecord);
// when found: a zeroed buffer of its size (slot 0x1C; 0x00608EC0), the
// record read into it (slot 0x38), a bridge lexer on the stack (0x00641F00
// with the vector and the divisor; the original's slot is its stack
// leftovers but for what the constructor and Parse write, ours starts
// zeroed) parsing it (0x00643580), the lexer's scopes freed (0x00641BF0,
// the head), its identifier buffer and the text buffer freed. The resource
// released when there is one.
void __cdecl LoadBridgeLayouts(uint32_t id, Vector* records, uint32_t divisor)
{
    static bool logged = false;
    FirstCall("LoadBridgeLayouts", logged);
    void* resource = nullptr;
    uint32_t key[3] = {kLayoutType, kLayoutGroup, id};
    if (CallCdecl<uint8_t, uint32_t*, void**, uint32_t>(addr::SC4_OpenResourceRecord, key, &resource, 0u)) {
        Vector bytes{};
        CallThis<void*, uint32_t>(addr::SC4ByteVector_ConstructZeroed, &bytes, Virtual<uint32_t>(resource, 0x1C));
        char* const begin = bytes.begin;
        const uint32_t size = static_cast<uint32_t>(bytes.end - bytes.begin);
        Virtual<uint8_t>(resource, 0x38, begin, size);
        alignas(4) char lexer[lexer::kSize] = {};
        char* const made =
            CallThis<char*, Vector*, uint32_t>(addr::SC4BridgeLexer_Construct, lexer, records, divisor);
        CallThis<uint8_t, char*, uint32_t>(addr::SC4BridgeLexer_Parse, made, begin, size);
        CallThis<void>(addr::SC4BridgeLexer_ClearMaps, lexer + bridgelexer5::kMaps);
        FreeIfSet(At<char*>(lexer, bridgelexer5::kMaps));
        FreeIfSet(At<char*>(lexer, lexer::kIdentBegin));
        FreeIfSet(begin);
    }
    if (resource)
        ReleaseOf(resource);
}

// 0x006437A0 (ret 0x10: the network ini, a section name, an ID, the
// divisor): the name in a game string (SC4String::FromRange with the name's
// slot as tag), the ini's slot 0x34 (it, the line callback 0x00642220, the
// row's bridge records +0x30), the string freed; then the layouts
// (0x006436D0 with the ID, the records, the divisor).
void __fastcall LoadBridges(char* self, void*, void* ini, const char* name, uint32_t id, uint32_t divisor)
{
    static bool logged = false;
    FirstCall("LoadBridges", logged);
    GameString section{};
    const char* tag = name;
    CallThis<void, const char*, const char*, const char**>(addr::SC4String_FromRange, &section.begin, name,
                                                           name + std::strlen(name), &tag);
    section.vtable = kStringVtable;
    section.references = 0;
    char* const records = self + typerow::kBridges;
    Virtual<uint8_t>(ini, 0x34, &section, static_cast<std::uintptr_t>(addr::SC4NetworkTypeInfo_BridgeLineCallback),
                     records);
    section.vtable = kStringVtable;
    FreeIfSet(section.begin);
    CallCdecl<void, uint32_t, char*, uint32_t>(addr::SC4NetworkTypeInfo_LoadBridgeLayouts, id, records, divisor);
}

// ====================================================================================
// Installation
// ====================================================================================

#define OPENSC4_DETOUR(address, fn) OPENSC4_NETTOOL_DETOUR(address, fn)

const Detour kRulParse3[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkTool_LoadRul0, LoadRul0),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_Rul0SectionCallback, Rul0Section),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_Rul0OrderingLineCallback, Rul0OrderingLine),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_LoadRul1, LoadRul1),
    OPENSC4_DETOUR(addr::SC4NetworkTypeInfo_Clear, TypeRowClear),
    OPENSC4_DETOUR(addr::SC4NetworkTypeInfo_LoadBridges, LoadBridges),
    OPENSC4_DETOUR(addr::SC4NetworkTypeInfo_LoadBridgeLayouts, LoadBridgeLayouts),
    OPENSC4_DETOUR(addr::SC4NetworkTypeInfo_BridgeLineCallback, BridgeLine),
    OPENSC4_DETOUR(addr::SC4BridgeLexer_Construct, BridgeLexerConstruct),
    OPENSC4_DETOUR(addr::SC4BridgeLexer_Parse, BridgeParse),
    OPENSC4_DETOUR(addr::SC4BridgeLexer_ParseBridge, BridgeParseBridge),
    OPENSC4_DETOUR(addr::SC4BridgeLexer_ParseLength, BridgeParseLength),
    OPENSC4_DETOUR(addr::SC4BridgeLexer_PushMap, BridgeLexerPushMap),
    OPENSC4_DETOUR(addr::SC4BridgeLexer_ClearMaps, BridgeLexerClearMaps),
};

const Detour kRulMaps3[] = {
    OPENSC4_DETOUR(addr::SC4BridgeIntMap_FindOrInsert, IntMapFindOrInsert),
    OPENSC4_DETOUR(addr::SC4StringHashMap_FindOrInsert, StringMapFindOrInsert),
    OPENSC4_DETOUR(addr::SC4Rul1Map_InsertUnique, Rul1InsertUnique),
    OPENSC4_DETOUR(addr::SC4Rul1Map_InsertHint, Rul1InsertHint),
    OPENSC4_DETOUR(addr::SC4Rul1Map_FindOrInsert, Rul1FindOrInsert),
    OPENSC4_DETOUR(addr::SC4NetworkBridgeRecord_CopyConstruct, BridgeRecordCopyConstruct),
    OPENSC4_DETOUR(addr::SC4NetworkBridgeRecord_CopyRange, BridgeCopyRange),
    OPENSC4_DETOUR(addr::SC4NetworkBridgeRecord_UninitializedCopy, BridgeUninitializedCopy),
    OPENSC4_DETOUR(addr::SC4NetworkBridgeRecord_UninitializedFillN, BridgeUninitializedFillN),
    OPENSC4_DETOUR(addr::SC4BridgeRecordVector_Erase, BridgeVectorErase),
    OPENSC4_DETOUR(addr::SC4BridgeRecordVector_InsertOverflow, BridgeVectorInsertOverflow),
};

#undef OPENSC4_DETOUR

bool InstallRulParse3(registry::Mode) { return InstallDetours(kRulParse3, "rulparse3"); }
bool InstallRulMaps3(registry::Mode) { return InstallDetours(kRulMaps3, "rulmaps3"); }

const bool g_registered[] = {
    registry::Register({"simulators.nettool.rulparse3", "simulators/nettool", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::SC4NetworkTypeInfo_BridgeLineCallback,
                        "Our network tool's RUL parsers, third part: LoadRul0 with its section and Ordering "
                        "callbacks, LoadRul1, a type row's Clear, LoadBridges, the bridge ini line callback, the "
                        "bridge layout load and the bridge lexer (constructor, Parse, ParseBridge, ParseLength, "
                        "PushMap, ClearMaps) (14 detours; src/core/nettool_rul_5.cpp)",
                        false, InstallRulParse3}),
    registry::Register({"simulators.nettool.rulmaps3", "simulators/nettool", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::SC4Rul1Map_InsertHint,
                        "Our network tool's rule containers, third part: the int-keyed and string maps' "
                        "FindOrInsert, the RUL1 map's InsertUnique, hinted insert and FindOrInsert, the bridge "
                        "record's copy constructor and the record vector's copies, Erase and InsertOverflow (11 "
                        "detours; src/core/nettool_rul_5.cpp)",
                        false, InstallRulMaps3}),
};

} // namespace
} // namespace opensc4::nettool_sim
