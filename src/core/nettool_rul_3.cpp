// Our cSC4NetworkTool, part r3 (build 641; the class map is in
// src/core/nettool_sim.h, the rule layouts in src/core/nettool_rul.h): the
// RUL parsers' first part, 0x0063BF30 to 0x0063DDF0, and the containers
// they fill. Three groups, each a list of detours at the game's addresses:
//   simulators.nettool.rulparse1: the type row's loaders and the ini line
//     callbacks: SetLoaded, AddRuleFile, LoadParams and the placement
//     parameters' Load (the exemplar), LoadRul2, ReadRul1TypeIds, the RUL1
//     type line callback with TypeIndexOfName, the RUL1 pair line callback
//     with the pair table's FindOrInsert and InsertNoResize (11 detours);
//   simulators.nettool.rullex: the RUL0 lexer (constructor, NextToken and
//     its three character tests), the RUL0 rule's record vector (PushBack,
//     InsertOverflow, UninitializedFill, Assign), its check type table
//     (FindOrInsert, InsertNoResize) and the loader's three-list destructor
//     (12 detours);
//   simulators.nettool.rulmaps: the other containers: the RUL2 multimap's
//     InsertEqual and InsertAt (called by the guarded line callback), the
//     RUL1 pair table's Resize and CopyFrom, the bridge loader's
//     string-keyed map (Find, FindIterator, Resize, the pair's constructors,
//     Clear) and 20-byte element vector, the bridge record's int-keyed map
//     (CreateNode, CloneNode, InsertAt, InsertUnique, CopySubtree, Assign,
//     EraseSubtree), the type row's table +0x3C (insert, DestroyTable3C) and
//     a point table's insert (21 detours).
// Three groups because the loaders and callbacks touch resources and the
// type rows at every city load, the lexer and RUL0 record code run under
// the RUL0 parser (part r4/r5, still the game's), and the containers are
// pure memory work shared by the bridge, exemplar and RUL1 loaders: each can
// be tried and bisected alone.
//
// Every function was written from the disassembly. Calls between them go to
// the game's addresses (through the address entries), so each detour is
// independent of the others and OPENSC4_NETTOOL_SKIP bisects them. Of the
// part's 45 functions, the RUL2 line callback 0x0063D1E0 stays the game's
// (below); the other 44 are here.
//
// NAM. The RUL2 line callback 0x0063D1E0 is guarded: NAM's Rul2Engine
// patches 0x0063D316 inside it (a jump to its stub that skips the multimap
// insert), so it is not detoured and not translated. LoadRul2 0x0063D3E0
// passes its address (addr::cSC4NetworkTool_Rul2LineCallback, the game's
// 0x0063D1E0) to the ini's slot 0x34 exactly as the game does. The two
// functions it calls from the part, InsertEqual 0x0063C920 (ret 8) and,
// under it, InsertAt 0x0063C1C0 (ret 0x14), keep the game's convention; the
// callback's code after the call (0x0063D33C: push EBP, call SC4::Free...)
// reads no register the callee leaves, so they are plain detours. NAM's
// DLL reads the RUL2 multimap 0x00B466D0, the type rows (+0x50 to +0x110)
// and the RUL0 rules by layout: every container, node and row here stays in
// the game's memory with the game's layout, allocated through the game's
// allocators (SC4::stl_allocate, SC4::Free, SC4::operator_new) and built
// through the game's template helpers where the game calls them; nothing is
// kept across calls. No wrapper of src/core/game_*.cpp or *fix*.cpp detours
// or tests return addresses in 0x0063BF30 to 0x0063E060; sim_timers.cpp has
// nothing here.
//
// Floats. Three functions touch the x87:
//   - the placement parameters' Load 0x0063CBE0 scales five angles by the
//     float 0x00AA8C60 (degrees to radians): FLD float, FMUL float, FSTP
//     float. The product of two floats is exact in a double (48 bits), so
//     it is the double product rounded once to float; NaNs propagate quiet
//     either way. Nothing is held on the x87 stack across a call.
//   - LoadParams 0x0063D460 takes three tangents with FPTAN (not affected by
//     the precision control: an extended result), multiplies by 16 (FMUL
//     float, rounded to 53 bits), stores with FST (float) and keeps the
//     53-bit product for a second FMUL by 0x00AA8C50: a naked helper with the
//     same instructions (X87ParamTangents), since C++ cannot repeat FPTAN or
//     the double rounding.
//   - NextToken 0x0063DDF0 parses a float with 0x00910991 (the game's
//     strtod-like parser, result on the x87 stack) and stores it with FSTP
//     float: a naked helper calls it at its address and stores ST(0) the
//     same way (X87ParseStore; no double rounding through a C++ double).
//   No __ftol2.
//
// Registers. The scan of every direct caller (ECX, EDX and EAX after the
// call, both branches, and through callers that return right after the
// call: 0x0063C920 to the guarded 0x0063D1E0, 0x0063C890, 0x0063C960,
// 0x0063D4D0 (to 0x00640320's caller), LoadRul0, 0x0063E500, 0x0063FCD0,
// 0x0063EA50, 0x00643330) found no read of ECX or EDX after any of these
// functions (the flagged sites write CL/DL or EDX first). So all are plain
// detours but one: the placement parameters' Load 0x0063CBE0 takes its
// exemplar instance in EAX (an LTCG convention: LoadParams does `mov
// eax,[esp+4]` before the call and Load stores EAX into its key at once),
// so its detour is a naked thunk that passes EAX and ECX on, and our
// LoadParams calls the game's address through a naked helper that loads
// them (CallWithEaxEcx). EAX is the game's where callers read it: the three
// character tests, UninitializedFill (the end), Find (the node), the
// constructors and Assign (this), CreateNode, CloneNode and CopySubtree (the
// node), the inserts (the node + 4, the value, the result), TypeIndexOfName
// (the index), NextToken (the token); also where nobody reads it.
//
// Stack leftovers the game stores:
//   - The RUL1 pair line callback 0x0063D080 stores the turn byte in the
//     low byte of a stack word it never initialised and writes the whole
//     word into the pair table (value +4): the three bytes after the turn
//     byte are the original's stack leftovers (from frames below the ini
//     reader). Ours writes zeros there. The only reader, the solver at
//     0x00634C0F, reads the byte; the table copy 0x0063CAA0 copies the word.
//   - FindOrInsert 0x0063C890 builds the zeroed value with the key byte in
//     a stack word whose three upper bytes it leaves unwritten; only the
//     byte is copied into the node (0x0063C600).
//   - NextToken passes a stack word whose low byte is the character to the
//     character tests, which read only that byte.
// Other stores into the game's stack: several functions overwrite their
// own argument slots (FindOrInsert 0x0063C890 the key pointer's low byte,
// Assign 0x0063C4F0 the source pointer with the source's end, CopyFrom
// 0x0063CAA0 the source pointer with 0 (its fill value), the int map's
// InsertAt and CopySubtree a node pointer, InsertUnique the value pointer
// with the iterator, Table3CInsertNoResize the value pointer + 4) and pass
// such slots as allocator tags; no caller reads its pushed arguments after
// the call, and the tags are not read by the callees. Ours use locals.
//
// Calls, each count from the asm's pushes for that call and the callee's own
// `ret N`:
//   - the part's own, at the game's addresses: the character tests cdecl
//     one; UninitializedFill cdecl four; Find, FindIterator (two), the
//     resizes, InsertNoResizes, FindOrInserts, CopyFrom, PushBack, CopyPair,
//     CreateNode, CloneNode, Assigns, EraseSubtree, Vector20 CopyConstruct
//     one (ret 4); ConstructPair two (ret 8); InsertEqual, InsertUnique,
//     CopySubtree, InsertEqualNoResize two (ret 8); InsertAts and
//     InsertOverflow five (ret 0x14); TypeIndexOfName cdecl one; the
//     placement parameters' Load none (ECX, EAX);
//   - template code: SC4::stl_allocate 0x0090CF54, SC4::Free 0x0090CF63,
//     SC4::operator_new 0x005E55E0 cdecl one; SC4Tree::RebalanceForInsert
//     0x0040D5E0 cdecl two (node, root*); SC4::LowerBoundUint32 0x0079FA90
//     cdecl five (primes, primes end, &hint, hint, 0); SC4PtrVector::
//     Construct 0x0051C950 three (ret 0xC: count, value*, tag*);
//     SC4HashTable::Resize 0x0051CE70, SC4Uint32HashSet::Resize 0x004F0940,
//     PtrVector::Reserve 0x0045D4F0 one (ret 4); SC4PtrVector::FillInsert
//     0x004FBA20 three (ret 0xC: where, count, value*); SC4PtrVector::
//     InsertN 0x0051CA60 and SC4FloatVector::InsertN5 0x00676CC0 five (ret
//     0x14: where, value*, tag*, 1, 1); SC4Rul0Rule::CopyRecords 0x00624E60
//     cdecl four (first, last, destination, tag); SC4Vector16::Copy
//     0x007257C0 cdecl five (first, last, destination, tag, 0); SC4Vector20::
//     UninitializedCopy 0x005F51B0 cdecl four; the vectors' copy
//     constructors (float 0x00464760, uint32 0x00462F00, point 0x007CED60)
//     one (ret 4); SC4Pair HashTable::Find 0x00633970 one (ret 4);
//     SC4TreeIterator::Decrement 0x006809E0 none; the list clear 0x0043ABA0
//     none;
//   - the string code: SC4String::FromRange 0x00407FA0 three (ret 0xC:
//     first, last, tag), SC4String::CopyBuffer 0x004089B0 four (ret 0x10:
//     source body, 0, -1, tag), SC4String::SplitAtChar 0x0090F335 two (ret 8:
//     ',', the rest*), cRZBaseString::Trim 0x0090E8A6 none,
//     SC4ByteString::ConstructCopy 0x0040ADD0 three (ret 0xC: characters,
//     length, tag), SC4StringBody::AssignLength 0x005FA540 two (ret 8),
//     SC4::SplitText 0x00910BD3 cdecl five (text, length, ',', parts,
//     count*), SC4::memmove 0x009EEB70 cdecl three, STL::ThrowLengthError
//     0x009DD1D0 cdecl one; a cIGZString: slot 0x14 (the characters) and
//     0x1C (the length) none, slot 0x24 two (another string, 0);
//   - the C runtime: SC4::isdigit 0x009EF61D, SC4::isalnum 0x009EF70A,
//     SC4::isalpha 0x009EF56A, SC4::isxdigit 0x009EF657, SC4::TextToUint32
//     0x0090FE93, SC4::TextToInt 0x0090FCEF, SC4::ParseDouble910991 cdecl
//     one; SC4::strtoul 0x009F038B cdecl three;
//   - resources: the resource manager 0x00B43CA8 slot 0xC five (key*, IID
//     0xA52160F5, out*, 0, 0; AL), the resource's slot 0x1C none (the
//     property holder) and slot 0x8 Release; SC4::GetPropertyFloat
//     0x005FD4F0, GetPropertyBool 0x005FD390, GetPropertyUint32 0x005FD480,
//     GetPropertyUint32Vector 0x005FF510, GetPropertyFloatVector 0x005FFCC0
//     cdecl three (holder, ID, out*; AL); the ini (cRZIniFile) slot 0x34
//     three (section string*, callback, user data); a type row's rule file:
//     operator new, SC4NetworkRuleFile::Construct 0x00622A90 none, Load
//     0x00623020 two (ret 8).
#include "core/nettool_rul.h"
#include "core/nettool_sim.h"

#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>

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

// A hash table's buckets and their number as the game computes it ((end -
// begin) >> 2, arithmetic, used unsigned).
char** Buckets(char* table)
{
    return At<char**>(table, table::kBuckets);
}
uint32_t BucketCount(char* table)
{
    return static_cast<uint32_t>((At<char*>(table, table::kBucketsEnd) - At<char*>(table, table::kBuckets)) >> 2);
}

// A RUL0 record (16 bytes) copied as the game inlines it: word, byte,
// byte, word, word.
void CopyRecord(char* to, const char* from)
{
    char* const f = const_cast<char*>(from);
    At<uint32_t>(to, record::kWord) = At<uint32_t>(f, record::kWord);
    At<uint8_t>(to, record::kTurn) = At<uint8_t>(f, record::kTurn);
    At<uint8_t>(to, record::kFlip) = At<uint8_t>(f, record::kFlip);
    At<uint32_t>(to, record::kX) = At<uint32_t>(f, record::kX);
    At<uint32_t>(to, record::kZ) = At<uint32_t>(f, record::kZ);
}

// A game string object emptied as the game's inlined default constructor
// leaves it: begin, end, capacity 0, then an 8-byte buffer (begin = end,
// capacity + 8, the first byte 0); the vtable and reference count are set
// by the callers where the game sets them.
void EmptyBody(GameString& s)
{
    s.begin = nullptr;
    s.end = nullptr;
    s.capacity = nullptr;
    char* const buffer = Allocate(8);
    s.begin = buffer;
    s.end = buffer;
    s.capacity = buffer + 8;
    *buffer = 0;
}

// A game string object destroyed as the game inlines it: the vtable
// written again, the buffer freed.
void DestroyString(GameString& s)
{
    s.vtable = kStringVtable;
    FreeIfSet(s.begin);
}

// The next prime of the hash tables (the table 0x00B0... between
// SC4HashTable::Primes and PrimesEnd): the first one not below `hint`,
// 0xFFFFFFFB past the end.
uint32_t NextPrime(uint32_t* hint)
{
    const auto* const end = reinterpret_cast<const uint32_t*>(addr::SC4HashTable_PrimesEnd);
    using LowerBoundFn = const uint32_t*(__cdecl*)(const uint32_t*, const uint32_t*, uint32_t*, uint32_t, uint32_t);
    const uint32_t* const pos = reinterpret_cast<LowerBoundFn>(addr::SC4_LowerBoundUint32)(
        reinterpret_cast<const uint32_t*>(addr::SC4HashTable_Primes), end, hint, *hint, 0u);
    return pos == end ? 0xFFFFFFFBu : *pos;
}

// The resize 0x0063C290 and 0x0063C380 inline: when the hint is above the
// bucket count and its prime too, a new bucket vector of that many nulls
// (SC4PtrVector::Construct with a zero and the hint's slot as tag), every
// node moved bucket by bucket to the front of its new bucket, the vectors
// swapped and the old buffer freed.
template <typename HashFn>
void Resize(char* table, uint32_t hint, HashFn bucketOf)
{
    const uint32_t old = BucketCount(table);
    if (hint <= old)
        return;
    uint32_t wanted = hint;
    const uint32_t n = NextPrime(&wanted);
    wanted = n;
    if (n <= old)
        return;
    Vector fresh{};
    uint32_t zero = 0;
    CallThis<void*, uint32_t, uint32_t*, uint32_t*>(addr::SC4PtrVector_Construct, &fresh, n, &zero, &wanted);
    char** const to = reinterpret_cast<char**>(fresh.begin);
    for (uint32_t i = 0; i < old; ++i) {
        char* node = Buckets(table)[i];
        while (node) {
            const uint32_t index = bucketOf(node, n);
            Buckets(table)[i] = At<char*>(node, table::kNext);
            At<char*>(node, table::kNext) = to[index];
            to[index] = node;
            node = Buckets(table)[i];
        }
    }
    char* const previous = At<char*>(table, table::kBuckets);
    At<char*>(table, table::kBuckets) = fresh.begin;
    At<char*>(table, table::kBucketsEnd) = fresh.end;
    At<char*>(table, table::kBucketsCapacity) = fresh.capacity;
    FreeIfSet(previous);
}

// The pair table's bucket of a key: (second + 2 * first) % buckets.
uint32_t PairBucket(uint32_t first, uint32_t second, uint32_t buckets)
{
    return (second + first * 2) % buckets;
}

// ====================================================================================
// simulators.nettool.rulparse1: the type row's loaders and the ini callbacks
// ====================================================================================

// 0x0063BF90 (no arguments): the row's loaded byte +0x4 set.
void __fastcall SetLoaded(char* self, void*)
{
    static bool logged = false;
    FirstCall("SetLoaded", logged);
    At<uint8_t>(self, typerow::kLoaded) = 1;
}

// 0x0063D360 (ret 0xC: a resource key*, a word, a third word it never
// reads): a rule file object (operator new 0x10, the constructor; null when
// the allocation fails) loaded with the key and the word (Load 0x00623020
// on it, also when null), then appended to the row's vector +0x24 (in place,
// or SC4PtrVector::InsertN with the object's stack slot and the word's slot
// as tag).
void __fastcall AddRuleFile(char* self, void*, const void* key, uint32_t word, uint32_t)
{
    static bool logged = false;
    FirstCall("AddRuleFile", logged);
    void* const memory = CallCdecl<void*, uint32_t>(addr::SC4_operator_new, rulefile::kSize);
    char* const file = memory ? CallThis<char*>(addr::SC4NetworkRuleFile_Construct, memory) : nullptr;
    char* local = file;
    uint32_t tag = word;
    CallThis<uint8_t, const void*, uint32_t>(addr::SC4NetworkRuleFile_Load, file, key, word);
    Vector& files = At<Vector>(self, typerow::kFiles);
    if (files.end != files.capacity) {
        if (files.end)
            *reinterpret_cast<char**>(files.end) = file;
        files.end += 4;
        return;
    }
    CallThis<void, char*, char**, uint32_t*, uint32_t, uint32_t>(addr::SC4PtrVector_InsertN, &files, files.end, &local,
                                                                 &tag, 1u, 1u);
}

// The placement parameters' properties (exemplar property IDs).
namespace property {
inline constexpr uint32_t kAngle0 = 0x879D12E7, kAngle1 = 0x279D1707, kAngle2 = 0x07B36C71;
inline constexpr uint32_t kFloat0C = 0xC7B36CA4, kFloat10 = 0xC7B36CA5, kFloat14 = 0xC804DD86;
inline constexpr uint32_t kAngle18 = 0xC804DD87, kAngle1C = 0xC804DD88, kFloat20 = 0xC804DD89;
inline constexpr uint32_t kBool24 = 0xC804DD90, kWord50 = 0xC804DD9B, kVector28 = 0xC804DD92;
inline constexpr uint32_t kFloat34 = 0xC804DD93, kFloat38 = 0xC804DD94, kFloat3C = 0xC804DD95, kFloat40 = 0xC804DD96;
inline constexpr uint32_t kBool44 = 0xC804DD97, kWord48 = 0xC804DD98, kWord4C = 0xC804DD99;
inline constexpr uint32_t kVector54 = 0xE974EAA4, kVector60 = 0xE974EAA5, kFloats6C = 0xE974EAA6;
inline constexpr uint32_t kVectorA0 = 0xE974EAA7, kFloat78 = 0x4A270DB0, kFloat7C = 0x4A270E94;
inline constexpr uint32_t kWord80 = 0x4A271248, kWord84 = 0x4A27124F, kWord88 = 0x4A275748;
inline constexpr uint32_t kWord8C = 0x4A2D49B9, kWord90 = 0x8A2D49EA, kWord94 = 0x8A2D49F0;
inline constexpr uint32_t kFloat98 = 0x6A2D49F4, kWord9C = 0xEA86E3B5;
} // namespace property
inline constexpr uint32_t kParamsType = 0x6534284A, kParamsGroup = 0x084344E0, kResourceIid = 0xA52160F5;

uint8_t GetFloat(void* holder, uint32_t id, char* at)
{
    return CallCdecl<uint8_t, void*, uint32_t, float*>(addr::SC4_GetPropertyFloat, holder, id,
                                                       reinterpret_cast<float*>(at));
}
uint8_t GetBool(void* holder, uint32_t id, char* at)
{
    return CallCdecl<uint8_t, void*, uint32_t, char*>(addr::SC4_GetPropertyBool, holder, id, at);
}
uint8_t GetUint32(void* holder, uint32_t id, char* at)
{
    return CallCdecl<uint8_t, void*, uint32_t, char*>(addr::SC4_GetPropertyUint32, holder, id, at);
}
uint8_t GetUint32Vector(void* holder, uint32_t id, char* at)
{
    return CallCdecl<uint8_t, void*, uint32_t, char*>(addr::SC4_GetPropertyUint32Vector, holder, id, at);
}
uint8_t GetFloatVector(void* holder, uint32_t id, char* at)
{
    return CallCdecl<uint8_t, void*, uint32_t, char*>(addr::SC4_GetPropertyFloatVector, holder, id, at);
}

// FLD float, FMUL the float 0x00AA8C60, FSTP float: the double product of
// two floats is exact, so one rounding to float, as the x87 at 53 bits.
void ToRadians(char* at)
{
    const double degree = static_cast<double>(Global<float>(addr::SC4_FloatDegree));
    const double product = static_cast<double>(At<float>(at, 0)) * degree;
    At<float>(at, 0) = static_cast<float>(product);
}

// A vector emptied the game's way (its memmove of the tail [end, end) never
// runs: the end set to the begin).
void Truncate(char* at)
{
    Vector& v = At<Vector>(at, 0);
    v.end = v.begin;
}

// 0x0063CBE0 (no stack arguments; ECX the row's placement parameters +0x50,
// EAX the exemplar instance; AL). The exemplar (type 0x6534284A, group
// 0x084344E0, the instance) from the resource manager (slot 0xC, IID
// 0xA52160F5); none: AL 0. Else its property holder (slot 0x1C) read in the
// game's order: floats, the five angles turned to radians right after each
// read, a bool +0x24, the byte +0x25 cleared, +0x50 (1000 when missing),
// vectors (each emptied before its read), and when +0x60 is missing that
// many zeros as +0x54 has words, when the float vector +0x6C is missing that
// many 10.0, defaults written before their reads (+0x78 1.5, +0x7C 32,
// +0x80 and +0x84 0, +0x88 through a stack word set to 1) or after a
// missing read (+0x8C 1, +0x90 10, +0x94 20, +0x98 0.1; and +0x8C 1 again
// when 0xEA86E3B5 for +0x9C is missing: the game writes +0x8C there, not
// +0x9C). The resource released; AL 1.
uint8_t __cdecl LoadPlacementParams(char* self, uint32_t instance)
{
    static bool logged = false;
    FirstCall("LoadPlacementParams", logged);
    void* resource = nullptr;
    uint32_t key[3] = {kParamsType, kParamsGroup, instance};
    void* const manager = Global<void*>(addr::SC4_ResourceManager);
    if (!Virtual<uint8_t>(manager, 0xC, key, kResourceIid, &resource, 0u, 0u))
        return 0;
    void* const holder = Virtual<void*>(resource, 0x1C);
    GetFloat(holder, property::kAngle0, self + placement::kAngle0);
    ToRadians(self + placement::kAngle0);
    GetFloat(holder, property::kAngle1, self + placement::kAngle1);
    ToRadians(self + placement::kAngle1);
    GetFloat(holder, property::kAngle2, self + placement::kAngle2);
    ToRadians(self + placement::kAngle2);
    GetFloat(holder, property::kFloat0C, self + 0x0C);
    GetFloat(holder, property::kFloat10, self + 0x10);
    GetFloat(holder, property::kFloat14, self + 0x14);
    GetFloat(holder, property::kAngle18, self + 0x18);
    ToRadians(self + 0x18);
    GetFloat(holder, property::kAngle1C, self + 0x1C);
    ToRadians(self + 0x1C);
    GetFloat(holder, property::kFloat20, self + 0x20);
    GetBool(holder, property::kBool24, self + placement::kBool24);
    At<uint8_t>(self, placement::kDiagonal) = 0;
    if (!GetUint32(holder, property::kWord50, self + placement::kWord50))
        At<uint32_t>(self, placement::kWord50) = 1000;
    GetUint32Vector(holder, property::kVector28, self + placement::kVector28);
    GetFloat(holder, property::kFloat34, self + 0x34);
    GetFloat(holder, property::kFloat38, self + 0x38);
    GetFloat(holder, property::kFloat3C, self + 0x3C);
    GetFloat(holder, property::kFloat40, self + 0x40);
    GetBool(holder, property::kBool44, self + placement::kBool44);
    GetUint32(holder, property::kWord48, self + 0x48);
    GetUint32(holder, property::kWord4C, self + 0x4C);
    Truncate(self + placement::kVector54);
    GetUint32Vector(holder, property::kVector54, self + placement::kVector54);
    Truncate(self + placement::kVector60);
    if (!GetUint32Vector(holder, property::kVector60, self + placement::kVector60)) {
        const Vector& source = At<Vector>(self, placement::kVector54);
        Vector& v = At<Vector>(self, placement::kVector60);
        uint8_t tag = 0;
        for (uint32_t i = 0; i < static_cast<uint32_t>((source.end - source.begin) >> 2); ++i) {
            uint32_t value = 0;
            if (v.end != v.capacity) {
                if (v.end)
                    *reinterpret_cast<uint32_t*>(v.end) = 0;
                v.end += 4;
            } else {
                CallThis<void, char*, uint32_t*, uint8_t*, uint32_t, uint32_t>(addr::SC4PtrVector_InsertN, &v, v.end,
                                                                               &value, &tag, 1u, 1u);
            }
        }
    }
    Truncate(self + placement::kFloats6C);
    if (!GetFloatVector(holder, property::kFloats6C, self + placement::kFloats6C)) {
        const Vector& source = At<Vector>(self, placement::kVector54);
        Vector& v = At<Vector>(self, placement::kFloats6C);
        uint8_t tag = 0;
        for (uint32_t i = 0; i < static_cast<uint32_t>((source.end - source.begin) >> 2); ++i) {
            uint32_t value = 0x41200000u; // 10.0f, as a word
            if (v.end != v.capacity) {
                if (v.end)
                    *reinterpret_cast<uint32_t*>(v.end) = 0x41200000u;
                v.end += 4;
            } else {
                CallThis<void, char*, uint32_t*, uint8_t*, uint32_t, uint32_t>(addr::SC4FloatVector_InsertN5, &v, v.end,
                                                                               &value, &tag, 1u, 1u);
            }
        }
    }
    Truncate(self + placement::kVectorA0);
    GetUint32Vector(holder, property::kVectorA0, self + placement::kVectorA0);
    At<uint32_t>(self, placement::kFloat78) = 0x3FC00000u; // 1.5f
    GetFloat(holder, property::kFloat78, self + placement::kFloat78);
    At<uint32_t>(self, placement::kFloat7C) = 0x42000000u; // 32.0f
    GetFloat(holder, property::kFloat7C, self + placement::kFloat7C);
    At<uint32_t>(self, placement::kWord80) = 0;
    GetUint32(holder, property::kWord80, self + placement::kWord80);
    At<uint32_t>(self, placement::kWord84) = 0;
    GetUint32(holder, property::kWord84, self + placement::kWord84);
    uint32_t word88 = 1;
    GetUint32(holder, property::kWord88, reinterpret_cast<char*>(&word88));
    At<uint32_t>(self, placement::kWord88) = word88;
    if (!GetUint32(holder, property::kWord8C, self + placement::kWord8C))
        At<uint32_t>(self, placement::kWord8C) = 1;
    if (!GetUint32(holder, property::kWord90, self + placement::kWord90))
        At<uint32_t>(self, placement::kWord90) = 10;
    if (!GetUint32(holder, property::kWord94, self + placement::kWord94))
        At<uint32_t>(self, placement::kWord94) = 20;
    if (!GetFloat(holder, property::kFloat98, self + placement::kFloat98))
        At<uint32_t>(self, placement::kFloat98) = 0x3DCCCCCDu; // 0.1f
    if (!GetUint32(holder, property::kWord9C, self + placement::kWord9C))
        At<uint32_t>(self, placement::kWord8C) = 1; // sic: the game's write goes to +0x8C
    ReleaseOf(resource);
    return 1;
}

// The game's caller passes ECX (the parameters) and EAX (the instance).
__declspec(naked) void LoadPlacementParamsThunk()
{
    __asm {
        push eax
        push ecx
        call LoadPlacementParams
        add esp, 8
        ret
    }
}

// Calls a game function that takes ECX and EAX (0x0063CBE0) and returns
// with `ret`.
__declspec(naked) void __cdecl CallWithEaxEcx(std::uintptr_t /*function*/, void* /*ecx*/, uint32_t /*eax*/)
{
    __asm {
        mov ecx, [esp + 8]
        mov eax, [esp + 12]
        call dword ptr [esp + 4]
        ret
    }
}

// 0x0063D472 to 0x0063D4BD: FLD float, FPTAN, FSTP ST(0), FMUL float (16),
// FST float, FMUL float (0x00AA8C50), FSTP float, for the angles +0x50 (to
// +0x100, +0x104) and +0x58 (to +0x108, +0x10C), and without the second
// product for +0x54 (to +0x110).
__declspec(naked) void __cdecl X87ParamTangents(char* /*row*/, const float* /*sixteen*/, const float* /*rootHalf*/)
{
    __asm {
        mov eax, [esp + 4]
        mov ecx, [esp + 8]
        mov edx, [esp + 12]
        fld dword ptr [eax + 0x50]
        fptan
        fstp st(0)
        fmul dword ptr [ecx]
        fst dword ptr [eax + 0x100]
        fmul dword ptr [edx]
        fstp dword ptr [eax + 0x104]
        fld dword ptr [eax + 0x58]
        fptan
        fstp st(0)
        fmul dword ptr [ecx]
        fst dword ptr [eax + 0x108]
        fmul dword ptr [edx]
        fstp dword ptr [eax + 0x10c]
        fld dword ptr [eax + 0x54]
        fptan
        fstp st(0)
        fmul dword ptr [ecx]
        fstp dword ptr [eax + 0x110]
        ret
    }
}

// 0x0063D460 (ret 4: the exemplar instance): the placement parameters
// loaded (0x0063CBE0 with ECX the row's +0x50 and EAX the instance), then
// the five derived floats: +0x100 = tan(+0x50) * 16, +0x104 = that *
// 0.70710677, +0x108 / +0x10C the same from +0x58, +0x110 = tan(+0x54) * 16.
void __fastcall LoadParams(char* self, void*, uint32_t instance)
{
    static bool logged = false;
    FirstCall("LoadParams", logged);
    CallWithEaxEcx(addr::SC4NetworkPlacementParams_Load, self + typerow::kParams, instance);
    X87ParamTangents(self, &Global<float>(addr::SC4_FloatSixteen),
                     &Global<float>(addr::SC4NetworkTypeInfo_FloatRootHalf));
}

// 0x0063D3E0 (cdecl: the RUL 0x10000002 ini, a section name): the name in
// a game string (SC4String::FromRange with the name's slot as tag), the
// ini's slot 0x34 (the string, the guarded callback 0x0063D1E0, the RUL2
// multimap 0x00B466D0), the string destroyed.
void __cdecl LoadRul2(void* ini, const char* name)
{
    static bool logged = false;
    FirstCall("LoadRul2", logged);
    GameString section{};
    const char* tag = name;
    CallThis<void, const char*, const char*, const char**>(addr::SC4String_FromRange, &section.begin, name,
                                                           name + std::strlen(name), &tag);
    section.vtable = kStringVtable;
    section.references = 0;
    Virtual<uint8_t>(ini, 0x34, &section, static_cast<std::uintptr_t>(addr::cSC4NetworkTool_Rul2LineCallback),
                     static_cast<std::uintptr_t>(addr::cSC4NetworkTool_Rul2Overrides));
    DestroyString(section);
}

// 0x0063C7A0 (cdecl: a cIGZString*; EAX): for each of the 13 English
// network names (0x00AA845C), a game string of it (an exact buffer of the
// length + 1; the length error path the game has for a 4 GB name kept), the
// argument's slot 0x24 (that string, 0); the string destroyed; the first
// index it says equal, else -1.
int32_t __cdecl TypeIndexOfName(void* name)
{
    static bool logged = false;
    FirstCall("TypeIndexOfName", logged);
    for (int32_t i = 0; i < static_cast<int32_t>(kTypes); ++i) {
        const char* const text = reinterpret_cast<const char* const*>(addr::cSC4NetworkManager_EnglishNames)[i];
        const uint32_t length = static_cast<uint32_t>(std::strlen(text));
        GameString s{};
        const uint32_t wanted = length + 1;
        char* buffer = nullptr;
        if (wanted == 0) {
            CallCdecl<void, std::uintptr_t>(addr::STL_ThrowLengthError, addr::STL_BasicStringText);
            buffer = s.end;
        } else {
            buffer = Allocate(wanted);
            s.begin = buffer;
            s.end = buffer;
            s.capacity = buffer + wanted;
        }
        char* end = buffer;
        if (length)
            end = CallCdecl<char*, char*, const char*, uint32_t>(addr::SC4_memmove, buffer, text, length) + length;
        s.end = end;
        *end = 0;
        s.vtable = kStringVtable;
        s.references = 0;
        const uint8_t same = Virtual<uint8_t>(name, 0x24, &s, 0u);
        DestroyString(s);
        if (same)
            return i;
    }
    return -1;
}

// 0x0063D530 (cdecl: key, value, 13 words; the RUL1 'AllowedOverrides'
// callback): the value copied (SC4String::CopyBuffer of its body, null when
// the value is null, with the value's slot as tag) into a string A, an
// empty string B, A split at ',' (the rest to B), both trimmed, each looked
// up (0x0063C7A0); when both are known, word[a] |= 1 << b and word[b] |= 1
// << a (shift counts masked by 31 as SHL does). Both strings destroyed (B
// first).
void __cdecl Rul1TypeLine(void*, char* value, uint32_t* words)
{
    static bool logged = false;
    FirstCall("Rul1TypeLine", logged);
    GameString a;
    char* tag = value;
    CallThis<void, char*, uint32_t, uint32_t, char**>(addr::SC4String_CopyBuffer, &a.begin,
                                                      value ? value + 4 : nullptr, 0u, 0xFFFFFFFFu, &tag);
    a.vtable = kStringVtable;
    a.references = 0;
    GameString b;
    EmptyBody(b);
    b.vtable = kStringVtable;
    b.references = 0;
    CallThis<uint8_t, uint32_t, GameString*>(addr::SC4String_SplitAtChar, &a, uint32_t{','}, &b);
    CallThis<void>(addr::cRZBaseString_Trim, &a);
    CallThis<void>(addr::cRZBaseString_Trim, &b);
    const int32_t first = CallCdecl<int32_t, GameString*>(addr::cSC4NetworkTool_TypeIndexOfName, &a);
    const int32_t second = CallCdecl<int32_t, GameString*>(addr::cSC4NetworkTool_TypeIndexOfName, &b);
    if (first >= 0 && second >= 0) {
        words[first] |= 1u << (static_cast<uint32_t>(second) & 31);
        words[second] |= 1u << (static_cast<uint32_t>(first) & 31);
    }
    DestroyString(b);
    DestroyString(a);
}

// 0x0063D630 (cdecl: the RUL 0x10000001 ini, 13 words): the section name
// 'AllowedOverrides' (16 characters at 0x00AA8C84, FromRange with the ini's
// slot as tag) and the ini's slot 0x34 (it, the callback 0x0063D530, the
// words); the string destroyed.
void __cdecl ReadRul1TypeIds(void* ini, uint32_t* words)
{
    static bool logged = false;
    FirstCall("ReadRul1TypeIds", logged);
    GameString section{};
    void* tag = ini;
    const char* const name = reinterpret_cast<const char*>(addr::cSC4NetworkTool_Rul1SectionName);
    CallThis<void, const char*, const char*, void**>(addr::SC4String_FromRange, &section.begin, name, name + 16, &tag);
    section.vtable = kStringVtable;
    section.references = 0;
    Virtual<uint8_t>(ini, 0x34, &section, static_cast<std::uintptr_t>(addr::cSC4NetworkTool_Rul1TypeLineCallback),
                     words);
    DestroyString(section);
}

// 0x0063D080 (cdecl: key, value, a RUL1 pair table; pushed by 0x0063E340):
// five empty game strings, the value split at ',' into them (SC4::SplitText
// with a count of 5); unless exactly five: the strings destroyed (the last
// first) and nothing else. Else a, b, c, d, e = SC4::TextToUint32 of parts
// 0, 1, 2, 4, 3 (that order); the turn byte e, | 0x80 when d is not 0; the
// pair (a, b)'s value (FindOrInsert 0x0063C960) gets c and the byte (the
// three bytes after it: zeros here, the original's stack leftovers). The
// strings destroyed, the last first.
void __cdecl Rul1PairLine(void*, char* value, char* table)
{
    static bool logged = false;
    FirstCall("Rul1PairLine", logged);
    GameString parts[5];
    for (GameString& part : parts) {
        EmptyBody(part);
        part.vtable = kStringVtable;
        part.references = 0;
    }
    int32_t count = 5;
    const char* const begin = At<char*>(value, 4);
    const uint32_t length = static_cast<uint32_t>(At<char*>(value, 8) - begin);
    CallCdecl<uint8_t, const char*, uint32_t, uint32_t, GameString*, int32_t*>(addr::SC4_SplitText, begin, length,
                                                                              uint32_t{','}, parts, &count);
    if (count == 5) {
        const auto number = [](const GameString& s) {
            return CallCdecl<uint32_t, const char*>(addr::SC4_TextToUint32, s.begin);
        };
        uint32_t key[2];
        key[0] = number(parts[0]);
        key[1] = number(parts[1]);
        const uint32_t word = number(parts[2]);
        const bool flipped = number(parts[4]) != 0;
        uint8_t turn = static_cast<uint8_t>(number(parts[3]));
        if (flipped)
            turn |= 0x80;
        uint32_t* const out = CallThis<uint32_t*, uint32_t*>(addr::SC4PairHashTable_FindOrInsert, table, key);
        out[0] = word;
        out[1] = turn;
    }
    for (int i = 4; i >= 0; --i)
        DestroyString(parts[i]);
}

// ====================================================================================
// simulators.nettool.rullex: the RUL0 lexer and the RUL0 rule's containers
// ====================================================================================

// 0x0063BF30 (cdecl: a byte; EAX 0 or 1): SC4::isdigit of the byte
// (zero-extended) not 0.
int32_t __cdecl IsDigit(uint32_t c)
{
    static bool logged = false;
    FirstCall("IsDigit", logged);
    return CallCdecl<int32_t, int32_t>(addr::SC4_isdigit, static_cast<int32_t>(c & 0xFF)) != 0 ? 1 : 0;
}

// 0x0063BF50 (cdecl: a byte; EAX 0 or 1): '_', or SC4::isalnum not 0.
int32_t __cdecl IsIdentChar(uint32_t c)
{
    static bool logged = false;
    FirstCall("IsIdentChar", logged);
    const uint8_t b = static_cast<uint8_t>(c);
    if (b == '_')
        return 1;
    return CallCdecl<int32_t, int32_t>(addr::SC4_isalnum, static_cast<int32_t>(b)) != 0 ? 1 : 0;
}

// 0x0063BF70 (cdecl: a byte; EAX 0 or 1): SC4::isalpha not 0.
int32_t __cdecl IsAlpha(uint32_t c)
{
    static bool logged = false;
    FirstCall("IsAlpha", logged);
    return CallCdecl<int32_t, int32_t>(addr::SC4_isalpha, static_cast<int32_t>(c & 0xFF)) != 0 ? 1 : 0;
}

// 0x0063C720 (ret 4: a cIGZString*; EAX this): the base vtable, no pushed
// token, an 8-byte identifier buffer (0), the vtable; the text's length
// (slot 0x1C) and characters (slot 0x14): begin and cursor the characters,
// end + length; the word +0x2C -1, bytes +0x40 0, +0x42 0, +0x41 1.
char* __fastcall LexerConstruct(char* self, void*, void* text)
{
    static bool logged = false;
    FirstCall("LexerConstruct", logged);
    At<std::uintptr_t>(self, lexer::kVtable) = addr::SC4RulLexer_BaseVtable;
    At<int32_t>(self, lexer::kPushed) = lexer::kNoToken;
    At<char*>(self, lexer::kIdentBegin) = nullptr;
    At<char*>(self, lexer::kIdentEnd) = nullptr;
    At<char*>(self, lexer::kIdentCapacity) = nullptr;
    char* const buffer = Allocate(lexer::kFirstBuffer);
    At<char*>(self, lexer::kIdentCapacity) = buffer + lexer::kFirstBuffer;
    At<char*>(self, lexer::kIdentBegin) = buffer;
    At<char*>(self, lexer::kIdentEnd) = buffer;
    *buffer = 0;
    At<std::uintptr_t>(self, lexer::kVtable) = addr::SC4RulLexer_Vtable;
    const uint32_t length = Virtual<uint32_t>(text, 0x1C);
    char* const characters = Virtual<char*>(text, 0x14);
    At<char*>(self, lexer::kCursor) = characters;
    At<char*>(self, lexer::kBegin) = characters;
    At<char*>(self, lexer::kEnd) = characters + length;
    At<uint16_t>(self, lexer::kWord2C) = 0xFFFF;
    At<uint8_t>(self, lexer::kByte40) = 0;
    At<uint8_t>(self, lexer::kByte42) = 0;
    At<uint8_t>(self, lexer::kByte41) = 1;
    return self;
}

// The game's float parser 0x00910991 at its address, its x87 result stored
// with FSTP float (0x0063E007 to 0x0063E00D).
__declspec(naked) void __cdecl X87ParseStore(std::uintptr_t /*parser*/, const char* /*text*/, float* /*out*/)
{
    __asm {
        push dword ptr [esp + 8]
        call dword ptr [esp + 8]
        add esp, 4
        mov eax, [esp + 12]
        fstp dword ptr [eax]
        ret
    }
}

int32_t CharTest(std::uintptr_t test, uint8_t c)
{
    return CallCdecl<int32_t, uint32_t>(test, c);
}

// The text of [first, first + length) in a local STL string body
// (SC4ByteString::ConstructCopy with the flag byte's address as tag): its
// begin; the caller frees it.
char* LocalCopy(const char* first, uint32_t length, uint8_t* tag)
{
    Vector body{};
    CallThis<void*, const char*, uint32_t, uint8_t*>(addr::SC4ByteString_ConstructCopy, &body, first, length, tag);
    return body.begin;
}

// 0x0063DDF0 (no arguments; EAX the token). A pushed-back token (+0x10 not
// negative) is answered and cleared (-1). Else, from the cursor: spaces and
// tabs skipped, '#' skips to the line's end (not past '\r' / '\n'); at the
// text's end 0. The character c taken (the cursor past it):
//   '\r', '\n', ';': 0;
//   '0' followed by 'x': the cursor past the 'x' and the hex digits after it
//     (SC4::isxdigit), their text's value (SC4::TextToInt) in +0x20: 0x100;
//   a digit (0x0063BF30), '.', or '-' before a digit (SC4::isdigit of the
//     next byte sign-extended) or '.': a number: from c on, digits, '.' and
//     'x' (a '.' anywhere, c included, makes it a float); a float through
//     0x00910991 into +0x24 (0x101), else SC4::strtoul(text, 0, 0) into
//     +0x20 (0x100);
//   a letter (0x0063BF70): an identifier: c and the following '_' or
//     alphanumerics (0x0063BF50) assigned to +0x14 (0x005FA540): 0x102;
//   anything else: c sign-extended.
int32_t __fastcall NextToken(char* self, void*)
{
    static bool logged = false;
    FirstCall("LexerNextToken", logged);
    const int32_t pushed = At<int32_t>(self, lexer::kPushed);
    if (pushed >= 0) {
        At<int32_t>(self, lexer::kPushed) = lexer::kNoToken;
        return pushed;
    }
    char*& cursor = At<char*>(self, lexer::kCursor);
    char* const end = At<char*>(self, lexer::kEnd);
    if (cursor >= end)
        return lexer::kTokenEnd;
    uint8_t c = 0;
    for (;;) {
        c = static_cast<uint8_t>(*cursor);
        ++cursor;
        if (c == '#') {
            while (cursor < end) {
                const char k = *cursor;
                if (k == '\r' || k == '\n')
                    break;
                ++cursor;
            }
        } else if (c != ' ' && c != '\t') {
            break;
        }
        if (cursor >= At<char*>(self, lexer::kEnd))
            return lexer::kTokenEnd;
    }
    if (c == '\r' || c == '\n' || c == ';')
        return lexer::kTokenEnd;
    uint8_t flag = 0;
    if (c == '0' && cursor < end && *cursor == 'x') {
        char* const first = cursor + 1;
        cursor = first;
        while (cursor < At<char*>(self, lexer::kEnd) &&
               CallCdecl<int32_t, int32_t>(addr::SC4_isxdigit, static_cast<int32_t>(static_cast<uint8_t>(*cursor))))
            ++cursor;
        char* const text = LocalCopy(first, static_cast<uint32_t>(cursor - first), &flag);
        At<uint32_t>(self, lexer::kInteger) = CallCdecl<uint32_t, const char*>(addr::SC4_TextToInt, text);
        FreeIfSet(text);
        return lexer::kTokenInteger;
    }
    bool number = CharTest(addr::SC4RulLexer_IsDigit, c) != 0 || c == '.';
    if (!number && c == '-' && cursor < At<char*>(self, lexer::kEnd)) {
        number = CallCdecl<int32_t, int32_t>(addr::SC4_isdigit, static_cast<int32_t>(static_cast<int8_t>(*cursor))) !=
                     0 ||
                 *cursor == '.';
    }
    if (!number) {
        if (!CharTest(addr::SC4RulLexer_IsAlpha, c))
            return static_cast<int32_t>(static_cast<int8_t>(c));
        char* const first = cursor - 1;
        while (cursor < At<char*>(self, lexer::kEnd) &&
               CharTest(addr::SC4RulLexer_IsIdentChar, static_cast<uint8_t>(*cursor)))
            ++cursor;
        CallThis<void, const char*, uint32_t>(addr::SC4StringBody_AssignLength, self + lexer::kIdent, first,
                                              static_cast<uint32_t>(cursor - first));
        return lexer::kTokenIdentifier;
    }
    flag = c == '.' ? 1 : 0;
    char* const first = cursor - 1;
    while (cursor < At<char*>(self, lexer::kEnd)) {
        const uint8_t k = static_cast<uint8_t>(*cursor);
        if (k == '.')
            flag = 1;
        else if (!CallCdecl<int32_t, int32_t>(addr::SC4_isdigit, static_cast<int32_t>(k)) && k != 'x')
            break;
        ++cursor;
    }
    char* const text = LocalCopy(first, static_cast<uint32_t>(cursor - first), &flag);
    if (flag) {
        X87ParseStore(addr::SC4_ParseDouble910991, text, &At<float>(self, lexer::kFloat));
        FreeIfSet(text);
        return lexer::kTokenFloat;
    }
    At<uint32_t>(self, lexer::kInteger) =
        CallCdecl<uint32_t, const char*, char**, int32_t>(addr::SC4_strtoul, text, nullptr, 0);
    FreeIfSet(text);
    return lexer::kTokenInteger;
}

// 0x0063C0B0 (cdecl: destination, count, record*, tag; EAX the end): count
// copies of the record (a null destination skips the writes).
char* __cdecl RecordUninitializedFill(char* destination, uint32_t count, const char* value, void*)
{
    static bool logged = false;
    FirstCall("RecordUninitializedFill", logged);
    for (; count != 0; --count, destination += record::kSize) {
        if (destination)
            CopyRecord(destination, value);
    }
    return destination;
}

// 0x0063C9B0 (ret 0x14: where, record*, tag, count, at end): the new
// length size + max(size, count) (unsigned), a buffer of it (none for 0);
// [begin, where) copied (0x00624E60 with the at-end slot as tag), then one
// record copied in place for count 1 or count copies (0x0063C0B0), then
// unless at end (its byte) [where, end) copied; the old buffer freed.
void __fastcall RecordInsertOverflow(char* self, void*, char* where, const char* value, void*, uint32_t count,
                                     uint32_t atEnd)
{
    static bool logged = false;
    FirstCall("RecordInsertOverflow", logged);
    Vector& v = *reinterpret_cast<Vector*>(self);
    const uint32_t size = static_cast<uint32_t>(static_cast<int32_t>(v.end - v.begin) >> 4);
    const uint32_t length = size + (size < count ? count : size);
    char* const buffer = length ? Allocate(length << 4) : nullptr;
    uint32_t tag = atEnd;
    char* end = CallCdecl<char*, char*, char*, char*, uint32_t*>(addr::SC4Rul0Rule_CopyRecords, v.begin, where, buffer,
                                                                 &tag);
    if (count == 1) {
        if (end)
            CopyRecord(end, value);
        end += record::kSize;
    } else {
        end = CallCdecl<char*, char*, uint32_t, const char*, uint32_t*>(
            addr::SC4Rul0RecordVector_UninitializedFill, end, count, value, &tag);
    }
    if (!static_cast<uint8_t>(atEnd))
        end = CallCdecl<char*, char*, char*, char*, uint32_t*>(addr::SC4Rul0Rule_CopyRecords, where, v.end, end, &tag);
    FreeIfSet(v.begin);
    v.begin = buffer;
    v.end = end;
    v.capacity = buffer + (length << 4);
}

// 0x0063D4D0 (ret 4: a record*): appended in place, or InsertOverflow
// 0x0063C9B0 (end, record, the argument's slot as tag, 1, 1).
void __fastcall RecordPushBack(char* self, void*, const char* value)
{
    static bool logged = false;
    FirstCall("RecordPushBack", logged);
    Vector& v = *reinterpret_cast<Vector*>(self);
    if (v.end != v.capacity) {
        if (v.end)
            CopyRecord(v.end, value);
        v.end += record::kSize;
        return;
    }
    const char* tag = value;
    CallThis<void, char*, const char*, const char**, uint32_t, uint32_t>(addr::SC4Rul0RecordVector_InsertOverflow,
                                                                         self, v.end, value, &tag, 1u, 1u);
}

// 0x0063C4F0 (ret 4: the source vector*; EAX this): operator=. Nothing for
// itself. More records than this's capacity (unsigned): a new buffer (none
// for 0) filled by 0x00624E60, the old one freed. Else when this has at
// least as many: SC4Vector16::Copy over the start, the end moved; else the
// first ones copied over (SC4Vector16::Copy) and the rest copied after
// (0x00624E60). The tag is the argument's slot, which the game overwrites
// with the source's end.
char* __fastcall RecordAssign(char* self, void*, Vector* source)
{
    static bool logged = false;
    FirstCall("RecordAssign", logged);
    Vector& v = *reinterpret_cast<Vector*>(self);
    if (source == &v)
        return self;
    char* const first = source->begin;
    char* tag = source->end;
    const uint32_t count = static_cast<uint32_t>(static_cast<int32_t>(source->end - first) >> 4);
    const uint32_t capacity = static_cast<uint32_t>(static_cast<int32_t>(v.capacity - v.begin) >> 4);
    if (count > capacity) {
        char* const buffer = count ? Allocate(count << 4) : nullptr;
        CallCdecl<char*, char*, char*, char*, char**>(addr::SC4Rul0Rule_CopyRecords, first, tag, buffer, &tag);
        FreeIfSet(v.begin);
        v.end = buffer + (count << 4);
        v.begin = buffer;
        v.capacity = buffer + (count << 4);
        return self;
    }
    const uint32_t size = static_cast<uint32_t>(static_cast<int32_t>(v.end - v.begin) >> 4);
    if (size >= count) {
        CallCdecl<char*, char*, char*, char*, char**, uint32_t>(addr::SC4Vector16_Copy, first, source->end, v.begin,
                                                                &tag, 0u);
        v.end = v.begin + (count << 4);
        return self;
    }
    CallCdecl<char*, char*, char*, char*, char**, uint32_t>(addr::SC4Vector16_Copy, first, first + (size << 4), v.begin,
                                                            &tag, 0u);
    const uint32_t now = static_cast<uint32_t>(static_cast<int32_t>(v.end - v.begin) >> 4);
    CallCdecl<char*, char*, char*, char*, char**>(addr::SC4Rul0Rule_CopyRecords, source->begin + (now << 4),
                                                  source->end, v.end, &tag);
    v.end = v.begin + (count << 4);
    return self;
}

// 0x0063C600 (ret 4: a check type value*: the key byte, six words; EAX the
// node + 4): the table grown for one more (SC4HashTable::Resize), the
// key's bucket (after the growth), a 32-byte node (next 0; the key byte and
// the six words when the node + 4 is not null) put first in it; the count
// + 1.
char* __fastcall CheckTypeInsertNoResize(char* self, void*, const char* value)
{
    static bool logged = false;
    FirstCall("CheckTypeInsertNoResize", logged);
    CallThis<void, uint32_t>(addr::SC4HashTable_Resize, self, At<uint32_t>(self, table::kCount) + 1);
    const uint32_t bucket = static_cast<uint32_t>(*reinterpret_cast<const uint8_t*>(value)) % BucketCount(self);
    char* const first = Buckets(self)[bucket];
    char* const node = Allocate(checktype::kNodeSize);
    char* const entry = node + table::kKey;
    At<char*>(node, table::kNext) = nullptr;
    if (entry) {
        At<uint8_t>(entry, 0) = *reinterpret_cast<const uint8_t*>(value);
        for (std::size_t i = 0; i < checkvalue::kWordCount; ++i)
            At<uint32_t>(entry, 4 + 4 * i) = At<uint32_t>(const_cast<char*>(value), checkvalue::kWords + 4 * i);
    }
    At<char*>(node, table::kNext) = first;
    Buckets(self)[bucket] = node;
    At<uint32_t>(self, table::kCount) += 1;
    return entry;
}

// 0x0063C890 (ret 4: a key byte*; EAX the value, node + 8): the node with
// that key in its bucket, else a zeroed value with the key inserted
// (0x0063C600).
char* __fastcall CheckTypeFindOrInsert(char* self, void*, const uint8_t* key)
{
    static bool logged = false;
    FirstCall("CheckTypeFindOrInsert", logged);
    const uint8_t k = *key;
    for (char* node = Buckets(self)[static_cast<uint32_t>(k) % BucketCount(self)]; node;
         node = At<char*>(node, table::kNext)) {
        if (At<uint8_t>(node, table::kKey) == k)
            return node + 8;
    }
    alignas(4) char value[checkvalue::kSize] = {};
    At<uint8_t>(value, checkvalue::kKey) = k;
    return CallThis<char*, char*>(addr::SC4Rul0CheckTypes_InsertNoResize, self, value) + 4;
}

// 0x0063C470 (no arguments): the three lists at +0x14, +0x10 and +0xC (that
// order) each cleared (0x0043ABA0) and its node freed.
void __fastcall LoaderListsDestruct(char* self, void*)
{
    static bool logged = false;
    FirstCall("LoaderListsDestruct", logged);
    for (const std::size_t list : {std::size_t{0x14}, std::size_t{0x10}, std::size_t{0xC}}) {
        CallThis<void>(addr::SC4UI_F43ABA0, self + list);
        FreeIfSet(At<char*>(self, list));
    }
}

// ====================================================================================
// simulators.nettool.rulmaps: the RUL2 multimap's insert (under the guarded callback)
// ====================================================================================

// 0x0063C1C0 (ret 0x14: result*, x, y, value*, w; EAX result): a 0x34-byte
// node with a copy of the value (nine words, when the node + 0x10 is not
// null) linked left of y when y is the head, else right when w is set,
// else left when x is set or the key is below y's (unsigned), else right;
// the head's leftmost, root and rightmost kept; the node's parent y, no
// children; rebalanced (0x0040D5E0 with the root's slot); the count + 1;
// *result = node.
char** __fastcall Rul2InsertAt(char* self, void*, char** result, char* x, char* y, const char* value, char* w)
{
    static bool logged = false;
    FirstCall("Rul2InsertAt", logged);
    char* const head = At<char*>(self, tree::kHead);
    bool left;
    if (y == head)
        left = true;
    else if (w)
        left = false;
    else if (x)
        left = true;
    else
        left = At<uint32_t>(const_cast<char*>(value), 0) < At<uint32_t>(y, tree::kValue);
    char* const node = Allocate(rul2node::kNodeSize);
    if (node + tree::kValue) {
        for (std::size_t i = 0; i < rul2node::kValueWords; ++i)
            At<uint32_t>(node, tree::kValue + 4 * i) = At<uint32_t>(const_cast<char*>(value), 4 * i);
    }
    if (left) {
        At<char*>(y, tree::kLeft) = node;
        char* const h = At<char*>(self, tree::kHead);
        if (y == h) {
            At<char*>(h, tree::kParent) = node;
            At<char*>(At<char*>(self, tree::kHead), tree::kRight) = node;
        } else if (y == At<char*>(h, tree::kLeft)) {
            At<char*>(h, tree::kLeft) = node;
        }
    } else {
        At<char*>(y, tree::kRight) = node;
        char* const h = At<char*>(self, tree::kHead);
        if (y == At<char*>(h, tree::kRight))
            At<char*>(h, tree::kRight) = node;
    }
    At<char*>(node, tree::kParent) = y;
    At<char*>(node, tree::kLeft) = nullptr;
    At<char*>(node, tree::kRight) = nullptr;
    CallCdecl<void, char*, char*>(addr::SC4Tree_RebalanceForInsert, node,
                                  At<char*>(self, tree::kHead) + tree::kParent);
    At<uint32_t>(self, rul2::kCount) += 1;
    *result = node;
    return result;
}

// 0x0063C920 (ret 8: result*, value*; EAX result): insert_equal: from the
// root down, right when the key is not below the node's (unsigned), else
// left; then 0x0063C1C0 (result, the null child, the last node or the head,
// the value, 0).
char** __fastcall Rul2InsertEqual(char* self, void*, char** result, const char* value)
{
    static bool logged = false;
    FirstCall("Rul2InsertEqual", logged);
    char* y = At<char*>(self, tree::kHead);
    char* x = At<char*>(y, tree::kParent);
    while (x) {
        y = x;
        x = At<uint32_t>(const_cast<char*>(value), 0) >= At<uint32_t>(x, tree::kValue) ? At<char*>(x, tree::kRight)
                                                                                         : At<char*>(x, tree::kLeft);
    }
    CallThis<char**, char**, char*, char*, const char*, uint32_t>(addr::SC4Rul2Multimap_InsertAt, self, result, x, y,
                                                                  value, 0u);
    return result;
}

// ====================================================================================
// simulators.nettool.rulmaps: the RUL1 pair table
// ====================================================================================

// 0x0063C380 (ret 4: a count hint): Resize with each node's bucket (second
// + 2 * first) % the new count.
void __fastcall PairResize(char* self, void*, uint32_t hint)
{
    static bool logged = false;
    FirstCall("PairResize", logged);
    Resize(self, hint, [](char* node, uint32_t n) {
        return PairBucket(At<uint32_t>(node, pairtable::kFirst), At<uint32_t>(node, pairtable::kSecond), n);
    });
}

// 0x0063C6A0 (ret 4: a value*: first, second, two words; EAX the node + 4):
// the table grown for one more (0x0063C380), a 20-byte node (next 0; the
// four words when the node + 4 is not null) put first in its bucket; the
// count + 1.
char* __fastcall PairInsertNoResize(char* self, void*, const uint32_t* value)
{
    static bool logged = false;
    FirstCall("PairInsertNoResize", logged);
    CallThis<void, uint32_t>(addr::SC4PairHashTable_Resize, self, At<uint32_t>(self, table::kCount) + 1);
    const uint32_t bucket = PairBucket(value[0], value[1], BucketCount(self));
    char* const first = Buckets(self)[bucket];
    char* const node = Allocate(pairtable::kNodeSize);
    char* const entry = node + table::kKey;
    At<char*>(node, table::kNext) = nullptr;
    if (entry) {
        for (std::size_t i = 0; i < 4; ++i)
            At<uint32_t>(entry, 4 * i) = value[i];
    }
    At<char*>(node, table::kNext) = first;
    Buckets(self)[bucket] = node;
    At<uint32_t>(self, table::kCount) += 1;
    return entry;
}

// 0x0063C960 (ret 4: a key pair*; EAX the value, node + 0xC): Find
// 0x00633970's node, else the key with two zero words inserted (0x0063C6A0).
char* __fastcall PairFindOrInsert(char* self, void*, const uint32_t* key)
{
    static bool logged = false;
    FirstCall("PairFindOrInsert", logged);
    char* const node = CallThis<char*, const uint32_t*>(addr::SC4PairHashTable_Find, self, key);
    if (node)
        return node + pairtable::kValue;
    uint32_t value[4] = {key[0], key[1], 0, 0};
    return CallThis<char*, uint32_t*>(addr::SC4PairHashTable_InsertNoResize, self, value) + 8;
}

// 0x0063CAA0 (ret 4: the source table*): this's buckets emptied (the end
// set to the begin; the old nodes are not freed), reserved and filled
// (PtrVector::Reserve, SC4PtrVector::FillInsert of the source's bucket
// count of nulls), then each source bucket's chain copied in order (20-byte
// nodes: next 0, the four words when the node + 4 is not null); the count
// copied.
void __fastcall PairCopyFrom(char* self, void*, char* source)
{
    static bool logged = false;
    FirstCall("PairCopyFrom", logged);
    Vector& buckets = At<Vector>(self, table::kBuckets);
    buckets.end = buckets.begin;
    CallThis<void, uint32_t>(addr::PtrVector_Reserve, &buckets, BucketCount(source));
    uint32_t zero = 0;
    CallThis<void, char*, uint32_t, uint32_t*>(addr::SC4PtrVector_FillInsert, &buckets, buckets.end,
                                               BucketCount(source), &zero);
    const auto copy = [](char* from) {
        char* const node = Allocate(pairtable::kNodeSize);
        char* const entry = node + table::kKey;
        At<char*>(node, table::kNext) = nullptr;
        if (entry) {
            for (std::size_t i = 0; i < 4; ++i)
                At<uint32_t>(entry, 4 * i) = At<uint32_t>(from, table::kKey + 4 * i);
        }
        return node;
    };
    for (uint32_t i = 0; i < BucketCount(source); ++i) {
        char* from = Buckets(source)[i];
        if (!from)
            continue;
        char* last = copy(from);
        Buckets(self)[i] = last;
        for (from = At<char*>(from, table::kNext); from; from = At<char*>(from, table::kNext)) {
            char* const node = copy(from);
            At<char*>(last, table::kNext) = node;
            last = node;
        }
    }
    At<uint32_t>(self, table::kCount) = At<uint32_t>(source, table::kCount);
}

// ====================================================================================
// simulators.nettool.rulmaps: the vector of 20-byte elements and the string-keyed map
// ====================================================================================

// 0x0063C150 (ret 4: the source vector*; EAX this): emptied, a buffer of
// the source's count (a signed division by 20; none for 0), the elements
// copied (0x005F51B0 with the argument's slot as tag), the end its answer.
char* __fastcall Vector20CopyConstruct(char* self, void*, Vector* source)
{
    static bool logged = false;
    FirstCall("Vector20CopyConstruct", logged);
    const int32_t count = static_cast<int32_t>(source->end - source->begin) / static_cast<int32_t>(stringmap::kElement);
    Vector& v = *reinterpret_cast<Vector*>(self);
    v.begin = nullptr;
    v.end = nullptr;
    v.capacity = nullptr;
    char* const buffer = count ? Allocate(static_cast<uint32_t>(count) * stringmap::kElement) : nullptr;
    v.begin = buffer;
    v.end = buffer;
    v.capacity = buffer + static_cast<uint32_t>(count) * stringmap::kElement;
    Vector* tag = source;
    v.end = CallCdecl<char*, char*, char*, char*, Vector**>(addr::SC4Vector20_UninitializedCopy, source->begin,
                                                            source->end, buffer, &tag);
    return self;
}

// 0x0063C0F0 (ret 4: a game string*; EAX the node or null): the chain of
// bucket 0 (the game computes 0 % the bucket count: every key's bucket; a
// table without buckets would divide by zero, none has) walked for a node
// whose string (node + 4) has the key's length and bytes.
char* __fastcall StringMapFind(char* self, void*, const GameString* key)
{
    static bool logged = false;
    FirstCall("StringMapFind", logged);
    for (char* node = Buckets(self)[0]; node; node = At<char*>(node, table::kNext)) {
        const char* const begin = At<char*>(node, stringmap::kPair + 4);
        const char* const end = At<char*>(node, stringmap::kPair + 8);
        const int32_t length = static_cast<int32_t>(end - begin);
        if (length != static_cast<int32_t>(key->end - key->begin))
            continue;
        if (std::memcmp(begin, key->begin, static_cast<std::size_t>(static_cast<uint32_t>(length))) == 0)
            return node;
    }
    return nullptr;
}

// 0x0063C4D0 (ret 8: result*, a game string*; EAX result): {Find, this}.
char** __fastcall StringMapFindIterator(char* self, void*, char** result, const GameString* key)
{
    static bool logged = false;
    FirstCall("StringMapFindIterator", logged);
    result[0] = CallThis<char*, const GameString*>(addr::SC4StringHashMap_Find, self, key);
    result[1] = self;
    return result;
}

// 0x0063C290 (ret 4: a count hint): Resize with every node to bucket 0.
void __fastcall StringMapResize(char* self, void*, uint32_t hint)
{
    static bool logged = false;
    FirstCall("StringMapResize", logged);
    Resize(self, hint, [](char*, uint32_t n) { return 0u % n; });
}

// The pair's string, as 0x0063D6A0 and 0x0063D940 build it: the source's
// body copied (SC4String::CopyBuffer, null for a null source, with the
// argument's slot as tag), the vtable, no references.
void CopyPairString(char* self, char* source)
{
    char* tag = source;
    CallThis<void, char*, uint32_t, uint32_t, char**>(addr::SC4String_CopyBuffer, self + 4,
                                                      source ? source + 4 : nullptr, 0u, 0xFFFFFFFFu, &tag);
    At<std::uintptr_t>(self, 0) = kStringVtable;
    At<uint32_t>(self, 0x10) = 0;
}

// 0x0063D6A0 (ret 8: a game string*, a value*: a 20-byte element vector
// and a float vector; EAX this): the pair: the string, the vectors copied
// (0x0063C150, SC4FloatVector::CopyConstruct).
char* __fastcall StringMapConstructPair(char* self, void*, char* key, char* value)
{
    static bool logged = false;
    FirstCall("StringMapConstructPair", logged);
    CopyPairString(self, key);
    CallThis<char*, char*>(addr::SC4Vector20_CopyConstruct, self + stringmap::kVector20, value);
    CallThis<void*, char*>(addr::SC4FloatVector_CopyConstruct, self + stringmap::kFloats,
                           value + stringmap::kValueFloats);
    return self;
}

// 0x0063D940 (ret 4: a pair*; EAX this): the pair copied (its string, its
// two vectors).
char* __fastcall StringMapCopyPair(char* self, void*, char* source)
{
    static bool logged = false;
    FirstCall("StringMapCopyPair", logged);
    CopyPairString(self, source);
    CallThis<char*, char*>(addr::SC4Vector20_CopyConstruct, self + stringmap::kVector20, source + stringmap::kVector20);
    CallThis<void*, char*>(addr::SC4FloatVector_CopyConstruct, self + stringmap::kFloats, source + stringmap::kFloats);
    return self;
}

// 0x0063DA30 (no arguments): every bucket's nodes freed (the float vector,
// the 20-byte vector, the string's vtable written and its buffer freed,
// the node), the bucket nulled; the count 0.
void __fastcall StringMapClear(char* self, void*)
{
    static bool logged = false;
    FirstCall("StringMapClear", logged);
    for (uint32_t i = 0; i < BucketCount(self); ++i) {
        char* node = Buckets(self)[i];
        while (node) {
            char* const next = At<char*>(node, table::kNext);
            FreeIfSet(At<char*>(node, stringmap::kPair + stringmap::kFloats));
            FreeIfSet(At<char*>(node, stringmap::kPair + stringmap::kVector20));
            At<std::uintptr_t>(node, stringmap::kPair) = kStringVtable;
            FreeIfSet(At<char*>(node, stringmap::kPair + 4));
            Free(node);
            node = next;
        }
        Buckets(self)[i] = nullptr;
    }
    At<uint32_t>(self, table::kCount) = 0;
}

// ====================================================================================
// simulators.nettool.rulmaps: the type row's table +0x3C and the point table
// ====================================================================================

// 0x0063D9A0 (ret 4: a value*: a key, a uint32 vector, a float vector; EAX
// the node + 4): the table grown for one more (SC4Uint32HashSet::Resize), a
// 32-byte node (next 0; when the node + 4 is not null the key and the
// vectors copied: SC4Uint32Vector::Copy, SC4FloatVector::CopyConstruct) put
// first in the key's bucket; the count + 1.
char* __fastcall Table3CInsertNoResize(char* self, void*, char* value)
{
    static bool logged = false;
    FirstCall("Table3CInsertNoResize", logged);
    CallThis<void, uint32_t>(addr::SC4Uint32HashSet_Resize, self, At<uint32_t>(self, table::kCount) + 1);
    const uint32_t bucket = At<uint32_t>(value, 0) % BucketCount(self);
    char* const first = Buckets(self)[bucket];
    char* const node = Allocate(table3c::kNodeSize);
    char* const entry = node + table::kKey;
    At<char*>(node, table::kNext) = nullptr;
    if (entry) {
        At<uint32_t>(entry, 0) = At<uint32_t>(value, 0);
        CallThis<void*, char*>(addr::SC4Uint32Vector_Copy, node + table3c::kUints, value + table3c::kValueUints);
        CallThis<void*, char*>(addr::SC4FloatVector_CopyConstruct, node + table3c::kFloats,
                               value + table3c::kValueFloats);
    }
    At<char*>(node, table::kNext) = first;
    Buckets(self)[bucket] = node;
    At<uint32_t>(self, table::kCount) += 1;
    return entry;
}

// 0x0063D760 (no arguments): every bucket's nodes freed (the vectors +0x14
// and +0x8, the node), the bucket nulled; the count 0.
void __fastcall DestroyTable3C(char* self, void*)
{
    static bool logged = false;
    FirstCall("DestroyTable3C", logged);
    for (uint32_t i = 0; i < BucketCount(self); ++i) {
        char* node = Buckets(self)[i];
        while (node) {
            char* const next = At<char*>(node, table::kNext);
            FreeIfSet(At<char*>(node, table3c::kFloats));
            FreeIfSet(At<char*>(node, table3c::kUints));
            Free(node);
            node = next;
        }
        Buckets(self)[i] = nullptr;
    }
    At<uint32_t>(self, table::kCount) = 0;
}

// 0x0063D7E0 (ret 8: result*, a value*: a key, a point vector; EAX result):
// insert_equal without growth: a 20-byte node (next 0; when the node + 4
// is not null the key and the vector copied: SC4PointVector::CopyConstruct)
// put after the first node of the key's bucket with that key, else first in
// the bucket; the count + 1; result {node, this}.
char** __fastcall PointInsertEqualNoResize(char* self, void*, char** result, char* value)
{
    static bool logged = false;
    FirstCall("PointInsertEqualNoResize", logged);
    const uint32_t key = At<uint32_t>(value, 0);
    const uint32_t bucket = key % BucketCount(self);
    char* const first = Buckets(self)[bucket];
    char* same = first;
    while (same && At<uint32_t>(same, pointtable::kKey) != key)
        same = At<char*>(same, table::kNext);
    char* const node = Allocate(pointtable::kNodeSize);
    char* const entry = node + table::kKey;
    At<char*>(node, table::kNext) = nullptr;
    if (entry) {
        At<uint32_t>(entry, 0) = key;
        CallThis<void*, char*>(addr::SC4PointVector_CopyConstruct, node + pointtable::kPoints, value + 4);
    }
    if (same) {
        At<char*>(node, table::kNext) = At<char*>(same, table::kNext);
        At<char*>(same, table::kNext) = node;
    } else {
        At<char*>(node, table::kNext) = first;
        Buckets(self)[bucket] = node;
    }
    At<uint32_t>(self, table::kCount) += 1;
    result[0] = node;
    result[1] = self;
    return result;
}

// ====================================================================================
// simulators.nettool.rulmaps: the bridge record's int-keyed map
// ====================================================================================

// The value copied into a node (+0x10) when the node + 0x10 is not null:
// the key, the 20-byte vector (0x0063C150), the float vector.
void CopyIntValue(char* node, char* key, char* vector20, char* floats)
{
    if (!(node + intmap::kKey))
        return;
    At<uint32_t>(node, intmap::kKey) = At<uint32_t>(key, 0);
    CallThis<char*, char*>(addr::SC4Vector20_CopyConstruct, node + intmap::kVector20, vector20);
    CallThis<void*, char*>(addr::SC4FloatVector_CopyConstruct, node + intmap::kFloats, floats);
}

// 0x0063D8B0 (ret 4: a value*; EAX the node): a 0x2C-byte node with a copy
// of the value (links and color not written).
char* __fastcall IntMapCreateNode(char* self, void*, char* value)
{
    static bool logged = false;
    FirstCall("IntMapCreateNode", logged);
    (void)self;
    char* const node = Allocate(intmap::kNodeSize);
    CopyIntValue(node, value, value + intmap::kValueVector20, value + intmap::kValueFloats);
    return node;
}

// 0x0063D8F0 (ret 4: a node; EAX the copy): a node with a copy of the
// node's value, then no children and the node's color byte.
char* __fastcall IntMapCloneNode(char* self, void*, char* source)
{
    static bool logged = false;
    FirstCall("IntMapCloneNode", logged);
    (void)self;
    char* const node = Allocate(intmap::kNodeSize);
    CopyIntValue(node, source + intmap::kKey, source + intmap::kVector20, source + intmap::kFloats);
    At<char*>(node, tree::kLeft) = nullptr;
    At<char*>(node, tree::kRight) = nullptr;
    At<uint8_t>(node, intmap::kColor) = At<uint8_t>(source, intmap::kColor);
    return node;
}

// 0x0063D700 (ret 4: a node): the subtree freed: right by recursion (at the
// game's address), then the vectors +0x20 and +0x14 and the node, then left.
void __fastcall IntMapEraseSubtree(char* self, void*, char* node)
{
    static bool logged = false;
    FirstCall("IntMapEraseSubtree", logged);
    while (node) {
        CallThis<void, char*>(addr::SC4BridgeIntMap_EraseSubtree, self, At<char*>(node, tree::kRight));
        char* const left = At<char*>(node, tree::kLeft);
        FreeIfSet(At<char*>(node, intmap::kFloats));
        FreeIfSet(At<char*>(node, intmap::kVector20));
        Free(node);
        node = left;
    }
}

// 0x0063DAE0 (ret 0x14: result*, x, y, value*, w; EAX result): as the RUL2
// multimap's (0x0063C1C0) but signed keys, the right node from CreateNode
// 0x0063D8B0 and the left one copied inline; the count at +0x4.
char** __fastcall IntMapInsertAt(char* self, void*, char** result, char* x, char* y, char* value, char* w)
{
    static bool logged = false;
    FirstCall("IntMapInsertAt", logged);
    char* node = nullptr;
    const bool left = y == At<char*>(self, tree::kHead) ||
                      (!w && (x || At<int32_t>(value, 0) < At<int32_t>(y, intmap::kKey)));
    if (!left) {
        node = CallThis<char*, char*>(addr::SC4BridgeIntMap_CreateNode, self, value);
        At<char*>(y, tree::kRight) = node;
        char* const h = At<char*>(self, tree::kHead);
        if (y == At<char*>(h, tree::kRight))
            At<char*>(h, tree::kRight) = node;
    } else {
        node = Allocate(intmap::kNodeSize);
        CopyIntValue(node, value, value + intmap::kValueVector20, value + intmap::kValueFloats);
        At<char*>(y, tree::kLeft) = node;
        char* const h = At<char*>(self, tree::kHead);
        if (y == h) {
            At<char*>(h, tree::kParent) = node;
            At<char*>(At<char*>(self, tree::kHead), tree::kRight) = node;
        } else if (y == At<char*>(h, tree::kLeft)) {
            At<char*>(h, tree::kLeft) = node;
        }
    }
    At<char*>(node, tree::kLeft) = nullptr;
    At<char*>(node, tree::kRight) = nullptr;
    At<char*>(node, tree::kParent) = y;
    CallCdecl<void, char*, char*>(addr::SC4Tree_RebalanceForInsert, node,
                                  At<char*>(self, tree::kHead) + tree::kParent);
    At<uint32_t>(self, tree::kCount) += 1;
    *result = node;
    return result;
}

// 0x0063DC70 (ret 8: result*, value*; EAX result: {node, a byte inserted}):
// insert_unique: down from the root (left while the key is below the
// node's, signed); when the last step went left and that node is the
// leftmost, inserted (0x0063DAE0); else the node before it (SC4TreeIterator::
// Decrement) when the last step went left; when that node's key is below
// the value's, inserted; else {that node, 0}.
char* __fastcall IntMapInsertUnique(char* self, void*, char* result, char* value)
{
    static bool logged = false;
    FirstCall("IntMapInsertUnique", logged);
    char* const head = At<char*>(self, tree::kHead);
    char* x = At<char*>(head, tree::kParent);
    char* y = head;
    bool below = true;
    const int32_t key = At<int32_t>(value, 0);
    while (x) {
        y = x;
        below = key < At<int32_t>(x, intmap::kKey);
        x = below ? At<char*>(x, tree::kLeft) : At<char*>(x, tree::kRight);
    }
    char* j = y;
    const auto insert = [&]() {
        char* node = nullptr;
        CallThis<char**, char**, char*, char*, char*, uint32_t>(addr::SC4BridgeIntMap_InsertAt, self, &node, x, y,
                                                                value, 0u);
        At<char*>(result, 0) = node;
        At<uint8_t>(result, 4) = 1;
        return result;
    };
    if (below) {
        if (y == At<char*>(head, tree::kLeft))
            return insert();
        CallThis<void>(addr::SC4TreeIterator_Decrement, &j);
    }
    if (At<int32_t>(j, intmap::kKey) < key)
        return insert();
    At<uint8_t>(result, 4) = 0;
    At<char*>(result, 0) = j;
    return result;
}

// 0x0063DBB0 (ret 8: a node, its copy's parent; EAX the copy): STLport's
// _M_copy: the node cloned (0x0063D8F0) under the parent, its right subtree
// copied by recursion (the game's address); then down the left spine, each
// node cloned inline (value, color, no children), hung left of the previous
// copy, its right subtree copied by recursion.
char* __fastcall IntMapCopySubtree(char* self, void*, char* x, char* parent)
{
    static bool logged = false;
    FirstCall("IntMapCopySubtree", logged);
    char* const top = CallThis<char*, char*>(addr::SC4BridgeIntMap_CloneNode, self, x);
    At<char*>(top, tree::kParent) = parent;
    if (char* const right = At<char*>(x, tree::kRight))
        At<char*>(top, tree::kRight) =
            CallThis<char*, char*, char*>(addr::SC4BridgeIntMap_CopySubtree, self, right, top);
    char* p = top;
    for (x = At<char*>(x, tree::kLeft); x; x = At<char*>(x, tree::kLeft)) {
        char* const y = Allocate(intmap::kNodeSize);
        CopyIntValue(y, x + intmap::kKey, x + intmap::kVector20, x + intmap::kFloats);
        At<uint8_t>(y, intmap::kColor) = At<uint8_t>(x, intmap::kColor);
        At<char*>(y, tree::kLeft) = nullptr;
        At<char*>(y, tree::kRight) = nullptr;
        At<char*>(p, tree::kLeft) = y;
        At<char*>(y, tree::kParent) = p;
        if (char* const right = At<char*>(x, tree::kRight))
            At<char*>(y, tree::kRight) =
                CallThis<char*, char*, char*>(addr::SC4BridgeIntMap_CopySubtree, self, right, y);
        p = y;
    }
    return top;
}

// 0x0063DD30 (ret 4: the source map*; EAX this): operator=. Nothing for
// itself. When this has nodes: the root's subtree erased (0x0063D700), the
// head emptied, the count 0; the count 0 again. A source without a root:
// the head emptied. Else the root copied (0x0063DBB0 under the head), the
// leftmost and rightmost found, the count copied.
char* __fastcall IntMapAssign(char* self, void*, char* source)
{
    static bool logged = false;
    FirstCall("IntMapAssign", logged);
    if (self == source)
        return self;
    if (At<uint32_t>(self, tree::kCount)) {
        CallThis<void, char*>(addr::SC4BridgeIntMap_EraseSubtree, self,
                              At<char*>(At<char*>(self, tree::kHead), tree::kParent));
        char* const h = At<char*>(self, tree::kHead);
        At<char*>(h, tree::kLeft) = h;
        At<char*>(h, tree::kParent) = nullptr;
        At<char*>(h, tree::kRight) = h;
        At<uint32_t>(self, tree::kCount) = 0;
    }
    At<uint32_t>(self, tree::kCount) = 0;
    char* const root = At<char*>(At<char*>(source, tree::kHead), tree::kParent);
    char* const h = At<char*>(self, tree::kHead);
    if (!root) {
        At<char*>(h, tree::kParent) = nullptr;
        At<char*>(h, tree::kLeft) = h;
        At<char*>(h, tree::kRight) = h;
        return self;
    }
    At<char*>(h, tree::kParent) = CallThis<char*, char*, char*>(addr::SC4BridgeIntMap_CopySubtree, self, root, h);
    char* leftmost = At<char*>(h, tree::kParent);
    while (char* const next = At<char*>(leftmost, tree::kLeft))
        leftmost = next;
    At<char*>(h, tree::kLeft) = leftmost;
    char* rightmost = At<char*>(h, tree::kParent);
    while (char* const next = At<char*>(rightmost, tree::kRight))
        rightmost = next;
    At<char*>(h, tree::kRight) = rightmost;
    At<uint32_t>(self, tree::kCount) = At<uint32_t>(source, tree::kCount);
    return self;
}

// ====================================================================================
// Installation
// ====================================================================================

#define OPENSC4_DETOUR(address, fn) OPENSC4_NETTOOL_DETOUR(address, fn)

const Detour kRulParse1[] = {
    OPENSC4_DETOUR(addr::SC4NetworkTypeInfo_SetLoaded, SetLoaded),
    OPENSC4_DETOUR(addr::SC4NetworkTypeInfo_AddRuleFile, AddRuleFile),
    OPENSC4_DETOUR(addr::SC4NetworkPlacementParams_Load, LoadPlacementParamsThunk),
    OPENSC4_DETOUR(addr::SC4NetworkTypeInfo_LoadParams, LoadParams),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_LoadRul2, LoadRul2),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_TypeIndexOfName, TypeIndexOfName),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_Rul1TypeLineCallback, Rul1TypeLine),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_ReadRul1TypeIds, ReadRul1TypeIds),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_Rul1PairLineCallback, Rul1PairLine),
    OPENSC4_DETOUR(addr::SC4PairHashTable_FindOrInsert, PairFindOrInsert),
    OPENSC4_DETOUR(addr::SC4PairHashTable_InsertNoResize, PairInsertNoResize),
};

const Detour kRulLex[] = {
    OPENSC4_DETOUR(addr::SC4RulLexer_IsDigit, IsDigit),
    OPENSC4_DETOUR(addr::SC4RulLexer_IsIdentChar, IsIdentChar),
    OPENSC4_DETOUR(addr::SC4RulLexer_IsAlpha, IsAlpha),
    OPENSC4_DETOUR(addr::SC4RulLexer_Construct, LexerConstruct),
    OPENSC4_DETOUR(addr::SC4RulLexer_NextToken, NextToken),
    OPENSC4_DETOUR(addr::SC4Rul0RecordVector_UninitializedFill, RecordUninitializedFill),
    OPENSC4_DETOUR(addr::SC4Rul0RecordVector_InsertOverflow, RecordInsertOverflow),
    OPENSC4_DETOUR(addr::SC4Rul0RecordVector_PushBack, RecordPushBack),
    OPENSC4_DETOUR(addr::SC4Rul0RecordVector_Assign, RecordAssign),
    OPENSC4_DETOUR(addr::SC4Rul0CheckTypes_InsertNoResize, CheckTypeInsertNoResize),
    OPENSC4_DETOUR(addr::SC4Rul0CheckTypes_FindOrInsert, CheckTypeFindOrInsert),
    OPENSC4_DETOUR(addr::SC4Rul0LoaderLists_Destruct, LoaderListsDestruct),
};

const Detour kRulMaps[] = {
    OPENSC4_DETOUR(addr::SC4Rul2Multimap_InsertAt, Rul2InsertAt),
    OPENSC4_DETOUR(addr::SC4Rul2Multimap_InsertEqual, Rul2InsertEqual),
    OPENSC4_DETOUR(addr::SC4PairHashTable_Resize, PairResize),
    OPENSC4_DETOUR(addr::SC4PairHashTable_CopyFrom, PairCopyFrom),
    OPENSC4_DETOUR(addr::SC4Vector20_CopyConstruct, Vector20CopyConstruct),
    OPENSC4_DETOUR(addr::SC4StringHashMap_Find, StringMapFind),
    OPENSC4_DETOUR(addr::SC4StringHashMap_FindIterator, StringMapFindIterator),
    OPENSC4_DETOUR(addr::SC4StringHashMap_Resize, StringMapResize),
    OPENSC4_DETOUR(addr::SC4StringHashMap_ConstructPair, StringMapConstructPair),
    OPENSC4_DETOUR(addr::SC4StringHashMap_CopyPair, StringMapCopyPair),
    OPENSC4_DETOUR(addr::SC4StringHashMap_Clear, StringMapClear),
    OPENSC4_DETOUR(addr::SC4NetworkTypeInfo_Table3CInsertNoResize, Table3CInsertNoResize),
    OPENSC4_DETOUR(addr::SC4NetworkTypeInfo_DestroyTable3C, DestroyTable3C),
    OPENSC4_DETOUR(addr::SC4PointsHashTable_InsertEqualNoResize, PointInsertEqualNoResize),
    OPENSC4_DETOUR(addr::SC4BridgeIntMap_CreateNode, IntMapCreateNode),
    OPENSC4_DETOUR(addr::SC4BridgeIntMap_CloneNode, IntMapCloneNode),
    OPENSC4_DETOUR(addr::SC4BridgeIntMap_EraseSubtree, IntMapEraseSubtree),
    OPENSC4_DETOUR(addr::SC4BridgeIntMap_InsertAt, IntMapInsertAt),
    OPENSC4_DETOUR(addr::SC4BridgeIntMap_InsertUnique, IntMapInsertUnique),
    OPENSC4_DETOUR(addr::SC4BridgeIntMap_CopySubtree, IntMapCopySubtree),
    OPENSC4_DETOUR(addr::SC4BridgeIntMap_Assign, IntMapAssign),
};

#undef OPENSC4_DETOUR

bool InstallRulParse1(registry::Mode) { return InstallDetours(kRulParse1, "rulparse1"); }
bool InstallRulLex(registry::Mode) { return InstallDetours(kRulLex, "rullex"); }
bool InstallRulMaps(registry::Mode) { return InstallDetours(kRulMaps, "rulmaps"); }

const bool g_registered[] = {
    registry::Register({"simulators.nettool.rulparse1", "simulators/nettool", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::SC4NetworkTypeInfo_LoadParams,
                        "Our network tool's rule loaders: the type row's SetLoaded, AddRuleFile, LoadParams and the "
                        "placement parameters' load, LoadRul2, ReadRul1TypeIds and the RUL1 ini line callbacks "
                        "with the pair table's FindOrInsert and insert (11 detours; src/core/nettool_rul_3.cpp)",
                        false, InstallRulParse1}),
    registry::Register({"simulators.nettool.rullex", "simulators/nettool", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::SC4RulLexer_NextToken,
                        "Our network tool's RUL0 lexer (constructor, NextToken, character tests) and the RUL0 rule's "
                        "record vector, check type table and the loader's list destructor (12 detours; "
                        "src/core/nettool_rul_3.cpp)",
                        false, InstallRulLex}),
    registry::Register({"simulators.nettool.rulmaps", "simulators/nettool", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::SC4Rul2Multimap_InsertEqual,
                        "Our network tool's rule containers: the RUL2 multimap's insert, the RUL1 pair table's resize "
                        "and copy, the bridge loader's string map and int map, the type row's table +0x3C and a point "
                        "table (21 detours; src/core/nettool_rul_3.cpp)",
                        false, InstallRulMaps}),
};

} // namespace
} // namespace opensc4::nettool_sim
