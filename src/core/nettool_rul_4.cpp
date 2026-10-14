// Our cSC4NetworkTool, part r4 (build 641; the class map is in
// src/core/nettool_sim.h, the rule layouts in src/core/nettool_rul.h): the
// RUL parsers' second part, 0x0063E060 to 0x00640D3D, and the containers
// they fill. Two groups, each a list of detours at the game's addresses:
//   simulators.nettool.rulparse2: the parsers and line callbacks: the RUL0
//     line callback (its 21 static key strings), the check type parser, the
//     bridge layout parser with the lexer's ParseInt and ParseFloat, the
//     RUL1 section reader with AppendTypeName, the tile verifier load and
//     its line callback, the type rows' exemplar load (LoadExemplars, its
//     enumeration and callback) (12 detours);
//   simulators.nettool.rulmaps2: the containers: the RUL0 rule's Assign, a
//     type row's table +0x3C FindOrInsert, the bridge record int map's
//     hinted insert and copy constructor, the string map's CopyFrom and
//     InsertNoResize, the RUL1 map's CreateNode and InsertAt, the bridge
//     record's constructor, destructor and Assign (11 detours).
// Two groups because the parsers read resources and ini files at every city
// load while the containers are pure memory work: each can be tried and
// bisected alone.
//
// Every function was written from the disassembly. Calls between them go to
// the game's addresses (through the address entries), so each detour is
// independent of the others and OPENSC4_NETTOOL_SKIP bisects them. All 23
// functions of the part are here; none is guarded.
//
// NAM. The guarded RUL2 line callback 0x0063D1E0 and the RUL2 engine
// 0x00634D70 stay the game's; nothing here calls them or passes them on.
// NAM's DLL reads the RUL0 rules (and edits them in place), the store, the
// type rows and the RUL2 multimap by layout: every container, node, rule and
// record here stays in the game's memory with the game's layout, allocated
// through the game's allocators (SC4::stl_allocate, SC4::Free,
// SC4::operator_new) and built through the game's template helpers where the
// game calls them; nothing is kept across calls. No wrapper of
// src/core/game_*.cpp or *fix*.cpp detours or tests return addresses in
// 0x0063E060 to 0x00640D3D; sim_timers.cpp has nothing here.
//
// Floats. Four functions touch the x87, all with + - * on floats and
// stores as float, so doubles in C++ with each store rounded to float (the
// x87 runs at 53 bits):
//   - ParseFloat 0x0063EA00 and the bridge parser's 'height' read an integer
//     token as unsigned: FILD of the word, FADD 2^32 (0x00A80AA8) when it is
//     negative, FSTP float: exact in a double, so the unsigned value's double
//     rounded once to float.
//   - The bridge parser 0x0063EA50 computes each new element's position as
//     (last float - the part's last or first float) + the element's float,
//     each new float as (last float + the part's float) - the part's last or
//     first float, '<' elements as offset + last float, and at the end
//     height + each value; each a chain of FLD float / FADD / FSUB float
//     rounded to 53 bits per operation and FSTP float: the same chain in
//     doubles, in the asm's order and association.
//   - The exemplar callback 0x0063F400: the angle FILD i, FMUL float
//     (0x00AA8D30, pi / 2), FSTP float; the turning itself is the game's
//     0x005D77A0 (FSIN, FCOS), called at its address with the float as a
//     word.
//   - The RUL0 line callback's Piece: the game's 0x00910991 (result on the
//     x87 stack) stored with FSTP float by a naked helper (X87ParseStore), as
//     part r3 does.
//   No __ftol2, nothing held on the x87 stack across a call.
//
// Registers. The scan of every direct caller (ECX, EDX and EAX after the
// call, both branches: 0x00640320, 0x00640D40, 0x00640E10, 0x00640EF0,
// 0x00640FA0, 0x006412B0, 0x00641310, 0x006413B0, 0x00641D30, 0x00642150,
// 0x00642220, 0x00643330, 0x00643580, 0x00623310, the CRT exit code and the
// part's own) found no read of ECX or EDX after any of these functions. EAX
// is the game's where callers read it: the bridge record's constructor
// (this: 0x0064229F pushes it), the inserts (result, the node + 4, the node),
// Table3CFindOrInsert (the value), the parsers (AL; the upper bytes are not
// read: the callers test AL or SETNE it), Assign and the copy constructor
// (this). One function has a register convention: AppendTypeName 0x0063E240
// takes the type in ECX and the string in EAX (its only caller is
// 0x0063E340, ours): its detour is a naked thunk that passes them on, and our
// ReadRul1Pairs calls the game's address through a naked helper that loads
// them (CallWithEcxEax). The line and resource callbacks' return values are
// not read (the ini's slot 0x34 and the resource list's slot 0x1C ignore
// them: every callback of the list slot returns whatever is left in EAX).
//
// Stack leftovers the game stores:
//   - The bridge parser 0x0063EA50 builds each '<' element on its stack and
//     writes only one byte at +0x8 before copying all five words
//     (0x00889C90): bytes +0x9 to +0xB of every such element are the
//     original's stack leftovers. Ours writes zeros there (the element local
//     starts zeroed). Readers: the elements are copied as words (0x00889C90,
//     0x005F51B0 under the vector copies, the int map's and the string map's
//     copies, the bridge record Assign), and read by the network tool's
//     bridge code 0x00632060 (from 0x00632580 and 0x00632830, which find
//     them in a record's int map +0x16C or take the record's own), which
//     reads +0x0, +0x4, the BYTE +0x8 (0x006321E8), +0xC and +0x10 only. No
//     code reads the three bytes, so the difference cannot matter.
//   - The RUL0 line callback's CheckType builds a RUL0 lexer on its stack
//     and copies six words (+0x2C to +0x40) into the check type node; the
//     constructor and the check type parser never write +0x2E, +0x2F and
//     +0x43, nor the edge word and mask of a side without a network type
//     (+0x38 / +0x3C with one type, also +0x30 / +0x34 with none). In the
//     original these are stack leftovers (node +0xA, +0xB, +0x1F, and +0x14
//     / +0x18, +0xC / +0x10); ours are zeros (the lexer local starts
//     zeroed). The readers of the check type nodes (FindOrInsert
//     0x0063C890, CopyCheckTypes 0x00626430, Transpose, Rotate 0x006094B0,
//     MarkRuleCells 0x006097E0, 0x006099A0, 0x00609A00, ParseCellLayout and
//     the rule destructor) never read +0xA, +0xB or +0x1F, and every read
//     of a side's edge word and mask is gated by that side's type byte
//     being non-negative (CopyCheckTypes copies the words as words): the
//     difference cannot matter.
//   - The tile verifier line callback 0x0063F7A0 builds its 8-byte entries
//     in the slot where InsertEqualNoResize left {node, table}: bytes +0x6
//     and +0x7 of every entry are the table's address's upper bytes (0xB4,
//     0x00 for 0x00B466DC). Ours builds them the same way (deterministic,
//     not a leftover).
//   - The exemplar callback 0x0063F400 reads the key 0xE974EAA1 into a stack
//     word it never initialises: an exemplar without that property would get
//     a leftover key in the original (0 in ours). It also reads the first
//     eight floats of 0xE974EAA3 without checking the vector's length (as
//     the game).
//   - The RUL0 line callback's Translate and ReplacementIntersection read
//     their numbers with sscanf into its own argument slots (the key and
//     the context pointers) and do not check the count: a number missing
//     keeps the pointer's value. Ours starts the locals with the same
//     pointer values, so the result is the same.
//   - The Piece record is built on the stack with bytes +0x6 / +0x7 unwritten;
//     PushBack 0x0063D4D0 copies it field by field (word, byte, byte, word,
//     word), so they never reach the rule.
// Other stores into the game's stack: the RUL0 line callback scans into and
// passes as out-slots its own argument slots (key and context), the bridge
// record Assign and the string map CopyFrom pass the source argument's slot
// as an allocator tag, the hinted insert steps iterators stored in its
// argument slots; no caller reads its pushed arguments after the call. Ours
// use locals.
//
// What the RUL0 line callback writes into a rule: exactly the game's fields
// (+0x4 records, +0x10 / +0x14, +0x40 / +0x44 / +0x48, +0x78, +0x7C, +0x80,
// +0x94 / +0x98, +0xB4, +0xC8, +0xCC, +0xE4, +0xE8, +0xE9, the check types
// +0x64, and for CopyFrom the whole rule through Assign then +0x0 and +0xEC
// to +0xF8), nothing else; the fields part r1 found unwritten by
// FindOrInsert's stack temporaries (+0x18 to +0x24, +0x7C, +0x40, +0x44,
// +0xC4) are written only by these keys or the layout parsers.
//
// Calls, each count from the asm's pushes for that call and the callee's own
// `ret N`:
//   - the part's own, at the game's addresses: ParseInt, ParseFloat,
//     Table3CFindOrInsert, CreateNode, the RUL0 rule's Assign one (ret 4);
//     ParseCheckType none; InsertAt (ret 0x14) five; AppendTypeName none
//     (ECX, EAX); LoadExemplarsInto cdecl two; the callbacks are passed as
//     values (0x0063F400, 0x0063F7A0, 0x0063D080);
//   - parts r1 to r3: NextToken none; the lexer's constructor, the records'
//     PushBack and Assign, the check types' FindOrInsert, the string map's
//     Resize and CopyPair, the vector20 copy, the pair table's CopyFrom,
//     Table3CInsertNoResize, the int map's Assign one (ret 4); the string
//     map's FindIterator, the int map's InsertUnique and CopySubtree, the
//     point table's insert two (ret 8); the int map's InsertAt five (ret
//     0x14); EraseSubtree one; Translate two, Rotate one, Transpose none;
//   - template code: SC4::stl_allocate, SC4::Free, SC4::operator_new cdecl
//     one; SC4Tree::RebalanceForInsert cdecl two; PtrVector::Reserve,
//     SC4Uint32HashSet::Resize one (ret 4); SC4PtrVector::FillInsert three
//     (ret 0xC); SC4FloatVector::InsertN5 five (ret 0x14); SC4FloatVector::
//     PushBack 0x00496C50 and SC4Vector20::PushBack 0x00889C90 one (ret 4);
//     the vectors' assigns (0x00611B00, 0x007AD7A0, 0x00461210, 0x00462C10,
//     0x00464430) and copies (0x00462F00, 0x00464760, 0x007CED60) one (ret
//     4); SC4Uint32HashSet::Clear 0x008EBA60, SC4PtrVectorHashMap::Clear
//     0x00584660 none; CopyCheckTypes 0x00626430, CopyConsCells 0x006265B0
//     one (ret 4); SC4Sint32Map::Find 0x006C0030 and SC4Uint32HashTable::
//     Find 0x006F1530 two (ret 8: result*, key*); SC4MapIterator::Next
//     0x005D17C0 and SC4TreeIterator::Decrement 0x006809E0 none (ECX the
//     iterator); SC4StringList::Insert 0x00457250 three (ret 0xC);
//   - the string code: SC4String::FromText 0x00408480 one (ret 4, EAX this),
//     SC4String::FromRange 0x00407FA0 and the body's 0x00408420 three (ret
//     0xC: first, last, tag), the body's append range 0x0040EBC0 three (ret
//     0xC) and append text 0x00410F00 one (ret 4), a string from a body
//     0x00408B90 one (ret 4), SC4String::AssignRange 0x00408A70 three (ret
//     0xC), SC4String::CopyBuffer 0x004089B0 four (ret 0x10), SC4String::
//     SplitAtChar 0x0090F335 two (ret 8; AL), SC4String::Destroy 0x00414250
//     none, cRZBaseString::Assign 0x00424380 one, SC4String::Equals
//     0x00418A40 cdecl two (AL); a cIGZString's slot 0x28 two (another
//     string, 0; AL);
//   - the C runtime: SC4::TextToUint32 0x0090FE93, SC4::TextToInt
//     0x0090FCEF, SC4::atol 0x009EF84A, SC4::ParseDouble910991 cdecl one;
//     SC4::StrChr 0x009F0960, SC4::stricmp 0x00A04487 cdecl two;
//     SC4::sscanf 0x009EF536 cdecl, variadic; SC4::memmove cdecl three;
//     SC4::atexit 0x009EF059 cdecl one; SC4::RotatePoint 0x005D77A0 cdecl
//     four;
//   - resources: the resource manager slot 0xC five (key*, IID 0xA52160F5,
//     out*, 0, 0; AL), slot 0x44 two (out*, a filter's +8; EAX); the
//     resource's slot 0x1C none, Release slot 0x8; SC4ResourceKeyFilter's
//     constructor 0x00682020 two (ret 8; EAX), its slot 0x4 (AddRef) and 0x8;
//     the key list's slot 0x1C two (callback, data) and 0x8;
//     SC4::GetPropertyUint32, GetPropertyUint32Vector, GetPropertyFloatVector
//     cdecl three; the ini's slot 0x34 three (section string*, callback,
//     data); the lexer's slot 0x0 one (an error message; 0x00648F10 does
//     nothing).
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

// Texts of the executable the code here compares with or reads from.
inline constexpr std::uintptr_t kTextHeight = 0x00AA8CC4;    // "height"
inline constexpr std::uintptr_t kTextPiece = 0x00AA8CBC;     // "piece"
inline constexpr std::uintptr_t kTextRev = 0x00AA8CB8;       // "rev"
inline constexpr std::uintptr_t kTextLength = 0x00A9F4B8;    // "length"
inline constexpr std::uintptr_t kTextPylon = 0x00AA8CB0;     // "pylon"
inline constexpr std::uintptr_t kTextEnd = 0x00AA0A5C;       // "end"
inline constexpr std::uintptr_t kTextCheck = 0x00AA8D1C;     // "check"
inline constexpr std::uintptr_t kTextMultiOk = 0x00AA8D14;   // "multiok"
inline constexpr std::uintptr_t kTextOptional = 0x00AA8D08;  // "optional"
inline constexpr std::uintptr_t kTextBadMapTag = 0x00AA8D24; // "Bad map tag"
inline constexpr std::uintptr_t kTextTooManyChecks = 0x00AA8CCC; // "Too many network checks in one rule"
inline constexpr std::uintptr_t kTextExpectedNetwork = 0x00AA8CF0; // "Expected network name"
inline constexpr std::uintptr_t kTextSolutions = 0x00AA8C98; // "IntersectionSolutions", 21 characters
inline constexpr uint32_t kSolutionsLength = 21;
inline constexpr std::uintptr_t kFormatPiece = 0x00AA8D48; // ", %i , %i , 0x%x"
inline constexpr std::uintptr_t kFormatPair = 0x00AA8D3C;  // " %d , %d"
inline constexpr std::uintptr_t kFormatOne = 0x00AA8D34;   // " %d "

inline constexpr uint32_t kResourceIid = 0xA52160F5, kExemplarType = 0x6534284A;
inline constexpr uint32_t kExemplarKey = 0xE974EAA1, kExemplarUints = 0xE974EAA2, kExemplarFloats = 0xE974EAA3;
inline constexpr uint32_t kTypeCount = 13;
inline constexpr std::size_t kTableCount = 0x10; // a hash table's count (table::kCount)
inline constexpr std::size_t kElementSize = 0x14;

const char* Text(std::uintptr_t address)
{
    return reinterpret_cast<const char*>(address);
}

char** Buckets(char* table)
{
    return At<char**>(table, table::kBuckets);
}
uint32_t BucketCount(char* table)
{
    return static_cast<uint32_t>((At<char*>(table, table::kBucketsEnd) - At<char*>(table, table::kBuckets)) >> 2);
}

// The number of 20-byte elements as the game computes it (a signed division
// by 20, IMUL by 0x66666667).
int32_t ElementCount(const Vector& v)
{
    return static_cast<int32_t>(v.end - v.begin) / static_cast<int32_t>(kElementSize);
}

// An integer token's word read as unsigned, as FILD, FADD 2^32 when
// negative, FSTP float do: exact in a double, one rounding to float.
float UnsignedToFloat(uint32_t word)
{
    return static_cast<float>(static_cast<double>(word));
}

uint32_t Bits(float value)
{
    uint32_t word = 0;
    std::memcpy(&word, &value, sizeof word);
    return word;
}

// The game's float parser 0x00910991 at its address, its x87 result stored
// with FSTP float (0x006407C3 to 0x006407C8, 0x006407E3 to 0x006407E8).
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

// Calls a game function that takes ECX and EAX (0x0063E240) and returns
// with `ret`.
__declspec(naked) void __cdecl CallWithEcxEax(std::uintptr_t /*function*/, uint32_t /*ecx*/, void* /*eax*/)
{
    __asm {
        mov ecx, [esp + 8]
        mov eax, [esp + 12]
        call dword ptr [esp + 4]
        ret
    }
}

int32_t NextToken(char* lexer)
{
    return CallThis<int32_t>(addr::SC4RulLexer_NextToken, lexer);
}

// The lexer's identifier is `length` bytes equal to the text (the game's
// REPZ CMPS after a length test).
bool IdentIs(char* lexer, std::uintptr_t text, uint32_t length)
{
    const char* const begin = At<char*>(lexer, lexer::kIdentBegin);
    const char* const end = At<char*>(lexer, lexer::kIdentEnd);
    if (static_cast<uint32_t>(end - begin) != length)
        return false;
    return std::memcmp(begin, Text(text), length) == 0;
}

// SC4String::Equals 0x00418A40 (cdecl: a string body*, a C string; AL) on
// the lexer's identifier.
bool IdentEquals(char* lexer, std::uintptr_t text)
{
    return CallCdecl<uint8_t, char*, const char*>(addr::SC4String_Equals, lexer + lexer::kIdent, Text(text)) != 0;
}

// A float appended to a vector the way 0x0063F400 inlines it: in place, or a
// new buffer of size + max(size, 1) words (none for 0), the old words moved
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

int Scan2(const char* text, std::uintptr_t format, int32_t* a, int32_t* b)
{
    using ScanFn = int(__cdecl*)(const char*, const char*, ...);
    return reinterpret_cast<ScanFn>(addr::SC4_sscanf)(text, Text(format), a, b);
}

// ====================================================================================
// simulators.nettool.rulparse2: the RUL1 section reader and the lexer's numbers
// ====================================================================================

// The 13 network names in the order of 0x0063E240's jump table (0x0063E308).
inline constexpr std::uintptr_t kTypeNames[kTypeCount] = {
    0x00A9205C, // Road
    0x00A92054, // Rail
    0x00A9204C, // Highway
    0x00A92044, // Street
    0x00A9203C, // Pipe
    0x00AA1B28, // PowerLine
    0x00A92028, // Avenue
    0x00A92020, // Subway
    0x00AA1B1C, // LightRail
    0x00A92008, // Monorail
    0x00AA85A8, // OneWayRoad
    0x00AA859C, // DirtRoad
    0x00AA858C, // GroundHighway
};

// 0x0063E240 (no stack arguments: ECX the type, EAX a game string): for 0
// to 12 (unsigned) the type's name appended to the string's body
// (0x00410F00); else nothing.
void __cdecl AppendTypeName(uint32_t type, char* string)
{
    static bool logged = false;
    FirstCall("AppendTypeName", logged);
    if (type > 12)
        return;
    CallThis<void, const char*>(addr::cSC4Advisor_F410F00, string + 4, Text(kTypeNames[type]));
}

// The game's callers pass ECX (the type) and EAX (the string).
__declspec(naked) void AppendTypeNameThunk()
{
    __asm {
        push eax
        push ecx
        call AppendTypeName
        add esp, 8
        ret
    }
}

// 0x0063E340 (cdecl: the RUL 0x10000001 ini, types a and b, a RUL1 pair
// table): an empty game string (an 8-byte buffer), the smaller type's name
// and the larger's appended (0x0063E240; signed, b first when equal), then
// 'IntersectionSolutions' (0x0040EBC0 with a's slot as tag); the ini's slot
// 0x34 (the string, the pair callback 0x0063D080, the table); the string
// destroyed.
void __cdecl ReadRul1Pairs(void* ini, int32_t a, int32_t b, char* table)
{
    static bool logged = false;
    FirstCall("ReadRul1Pairs", logged);
    GameString s{};
    char* const buffer = Allocate(8);
    s.begin = buffer;
    s.end = buffer;
    s.capacity = buffer + 8;
    *buffer = 0;
    s.vtable = kStringVtable;
    s.references = 0;
    const int32_t low = a < b ? a : b;
    const int32_t high = a < b ? b : a;
    CallWithEcxEax(addr::cSC4NetworkTool_AppendTypeName, static_cast<uint32_t>(low), &s);
    CallWithEcxEax(addr::cSC4NetworkTool_AppendTypeName, static_cast<uint32_t>(high), &s);
    int32_t tag = a;
    CallThis<void, const char*, const char*, int32_t*>(addr::cSC4Advisor_F40EBC0, &s.begin, Text(kTextSolutions),
                                                       Text(kTextSolutions) + kSolutionsLength, &tag);
    Virtual<uint8_t>(ini, 0x34, &s, static_cast<std::uintptr_t>(addr::cSC4NetworkTool_Rul1PairLineCallback), table);
    s.vtable = kStringVtable;
    FreeIfSet(s.begin);
}

// 0x0063E9D0 (ret 4: out*; AL): the next token; an integer (+0x20) goes to
// out: 1; else 0.
uint8_t __fastcall ParseInt(char* self, void*, uint32_t* out)
{
    static bool logged = false;
    FirstCall("ParseInt", logged);
    if (NextToken(self) != lexer::kTokenInteger)
        return 0;
    *out = At<uint32_t>(self, lexer::kInteger);
    return 1;
}

// 0x0063EA00 (ret 4: a float out*; AL): the next token; a float (+0x24) as
// is, an integer (+0x20) as an unsigned number: 1; else 0.
uint8_t __fastcall ParseFloat(char* self, void*, float* out)
{
    static bool logged = false;
    FirstCall("ParseFloat", logged);
    const int32_t token = NextToken(self);
    if (token == lexer::kTokenFloat) {
        *reinterpret_cast<uint32_t*>(out) = At<uint32_t>(self, lexer::kFloat);
        return 1;
    }
    if (token != lexer::kTokenInteger)
        return 0;
    *out = UnsignedToFloat(At<uint32_t>(self, lexer::kInteger));
    return 1;
}

uint8_t LexInt(char* lexer, void* out)
{
    return CallThis<uint8_t, void*>(addr::SC4RulLexer_ParseInt, lexer, out);
}
uint8_t LexFloat(char* lexer, float* out)
{
    return CallThis<uint8_t, float*>(addr::SC4RulLexer_ParseFloat, lexer, out);
}

// ====================================================================================
// simulators.nettool.rulparse2: the bridge layout parser
// ====================================================================================

// What a 'piece' line appends from a part (`source`: a value of 20-byte
// elements +0x0 and floats +0xC; the destination's last float `last`):
// `repeat` times the part's elements, in order or reversed (then each
// element's byte +0x8 toggled by 2), each moved so the part starts (or ends)
// at `last`, and after each element whose count is a multiple of the
// lexer's divisor a float. The reads of the part and of the destination's
// ends are where the game reads them (after each append).
void AppendPart(char* lexer, char* value, char* source, uint32_t repeat, bool reversed)
{
    Vector& elements = At<Vector>(value, bridgevalue::kElements);
    Vector& floats = At<Vector>(value, bridgevalue::kFloats);
    const int32_t parts = ElementCount(At<Vector>(source, bridgevalue::kElements));
    if (repeat == 0)
        return;
    do {
        const float last = reinterpret_cast<float*>(floats.end)[-1];
        if (reversed) {
            int32_t i = parts - 1;
            if (i >= 0) {
                int32_t offset = i * static_cast<int32_t>(kElementSize);
                do {
                    CallThis<void, char*>(addr::SC4Vector20_PushBack, &elements, At<char*>(source, 0) + offset);
                    char* const e = elements.end - kElementSize;
                    At<uint8_t>(e, bridgevalue::kByte8) ^= 2;
                    const float partLast = reinterpret_cast<float*>(At<char*>(source, bridgevalue::kFloats + 4))[-1];
                    At<float>(e, bridgevalue::kPosition) = static_cast<float>(
                        (static_cast<double>(last) - static_cast<double>(partLast)) +
                        static_cast<double>(At<float>(elements.end - kElementSize, bridgevalue::kPosition)));
                    const uint32_t divisor = At<uint32_t>(lexer, bridgelexer::kDivisor);
                    if (static_cast<uint32_t>(ElementCount(elements)) % divisor == 0) {
                        const int32_t index = i / static_cast<int32_t>(divisor);
                        const float* const partFloats =
                            reinterpret_cast<float*>(At<char*>(source, bridgevalue::kFloats));
                        const float* const partEnd =
                            reinterpret_cast<float*>(At<char*>(source, bridgevalue::kFloats + 4));
                        float add = static_cast<float>(
                            (static_cast<double>(last) + static_cast<double>(partFloats[index])) -
                            static_cast<double>(partEnd[-1]));
                        CallThis<void, float*>(addr::SC4FloatVector_PushBack, &floats, &add);
                    }
                    --i;
                    offset -= static_cast<int32_t>(kElementSize);
                } while (i >= 0);
            }
        } else {
            int32_t i = 0;
            if (parts > 0) {
                int32_t offset = 0;
                do {
                    CallThis<void, char*>(addr::SC4Vector20_PushBack, &elements, At<char*>(source, 0) + offset);
                    const float partFirst = *reinterpret_cast<float*>(At<char*>(source, bridgevalue::kFloats));
                    char* const e = elements.end - kElementSize;
                    At<float>(e, bridgevalue::kPosition) =
                        static_cast<float>((static_cast<double>(last) - static_cast<double>(partFirst)) +
                                           static_cast<double>(At<float>(e, bridgevalue::kPosition)));
                    const uint32_t divisor = At<uint32_t>(lexer, bridgelexer::kDivisor);
                    if (static_cast<uint32_t>(ElementCount(elements)) % divisor == 0) {
                        const int32_t index = i / static_cast<int32_t>(divisor);
                        const float* const partFloats =
                            reinterpret_cast<float*>(At<char*>(source, bridgevalue::kFloats));
                        float add = static_cast<float>(
                            (static_cast<double>(last) + static_cast<double>(partFloats[index + 1])) -
                            static_cast<double>(partFloats[0]));
                        CallThis<void, float*>(addr::SC4FloatVector_PushBack, &floats, &add);
                    }
                    ++i;
                    offset += static_cast<int32_t>(kElementSize);
                } while (i < parts);
            }
        }
    } while (--repeat != 0);
}

// The part a piece names: 'length(n)': the record's int map value of n
// (SC4Sint32Map::Find; none or no record: null); else the first of the
// lexer's string maps (+0x2C list, in order) that has the identifier
// (FindIterator on a game string copy of it, 0x00408B90, destroyed after):
// that node's value (+0x18); none: null.
char* FindPart(char* lexer, char* record, bool& failed)
{
    failed = true;
    if (IdentEquals(lexer, kTextLength)) {
        if (!record)
            return nullptr;
        if (NextToken(lexer) != '(')
            return nullptr;
        uint32_t n = 0;
        if (!LexInt(lexer, &n))
            return nullptr;
        if (NextToken(lexer) != ')')
            return nullptr;
        int32_t key = static_cast<int32_t>(n);
        char* const map = record + rul::bridge::kIntMap;
        char* found = nullptr;
        CallThis<char**, char**, int32_t*>(addr::SC4Sint32Map_Find, map, &found, &key);
        if (found == At<char*>(map, tree::kHead))
            return nullptr;
        failed = false;
        return found + intmap::kVector20;
    }
    char* const list = At<char*>(lexer, bridgelexer::kMaps);
    char* item = At<char*>(list, 0);
    if (item == list)
        return nullptr;
    for (;;) {
        GameString copy{};
        CallThis<GameString*, char*>(addr::cSC4Advice_F408B90, &copy, lexer + lexer::kIdent);
        char* it[2] = {};
        CallThis<char**, char**, GameString*>(addr::SC4StringHashMap_FindIterator, item + bridgelexer::kListMap, it,
                                              &copy);
        CallThis<void>(addr::SC4String_Destroy, &copy);
        if (it[0]) {
            failed = false;
            return it[0] + stringmap::kPair + stringmap::kVector20;
        }
        item = At<char*>(item, 0);
        if (item == list)
            return nullptr;
    }
}

// 0x0063EA50 (ret 8: a bridge record or null, a value: 20-byte elements and
// floats; this the bridge lexer; AL). A 0.0 float appended (in place, or
// SC4FloatVector::InsertN5). Then lines until the text's end:
//   'height <number>' the height (a float, or an integer read unsigned),
//     then the line's end;
//   'piece' then items until the line's end: an optional count ('<n> *',
//     else 1) and either a part (an optional 'rev(<name>)' that reverses;
//     'length(<n>)' a record's int map value, else <name> a string map
//     value: AppendPart), or an element '<a, b [, f1 [, f2]]>' with an
//     optional ': ' list of numbers (the element's +0xC) or 'pylon' (end
//     kind 1) or 'end' (2), comma-separated; the token after it pushed
//     back; count times an element {f2 + the last float, a, b's byte, the
//     number (-1.0 default), the end kind} appended and, when the element
//     count is a multiple of the divisor, f1 + the last float;
//   'end' then the line's end: done.
// Anything else: 0. Done or at the text's end: the height added to every
// element's float and every float; 1.
uint8_t __fastcall ParseBridgeLayout(char* self, void*, char* record, char* value)
{
    static bool logged = false;
    FirstCall("ParseBridgeLayout", logged);
    char* const lexer = self;
    Vector& elements = At<Vector>(value, bridgevalue::kElements);
    Vector& floats = At<Vector>(value, bridgevalue::kFloats);
    float height = 0.0f;
    uint32_t zero = 0;
    uint8_t tag = 0;
    if (floats.end != floats.capacity) {
        if (floats.end)
            *reinterpret_cast<uint32_t*>(floats.end) = 0;
        floats.end += 4;
    } else {
        CallThis<void, char*, uint32_t*, uint8_t*, uint32_t, uint32_t>(addr::SC4FloatVector_InsertN5, &floats,
                                                                       floats.end, &zero, &tag, 1u, 1u);
    }
    // The '<' element, built once per element line on the stack; the three
    // bytes after +0x8 are never written (the original's leftovers).
    alignas(4) char element[kElementSize] = {};
    const auto more = [&]() { return At<char*>(lexer, lexer::kCursor) < At<char*>(lexer, lexer::kEnd); };

    while (more()) {
        int32_t token = NextToken(lexer);
        if (token == 0)
            continue;
        if (token != lexer::kTokenIdentifier)
            return 0;
        if (IdentIs(lexer, kTextHeight, 6)) {
            token = NextToken(lexer);
            if (token == lexer::kTokenFloat)
                height = At<float>(lexer, lexer::kFloat);
            else if (token == lexer::kTokenInteger)
                height = UnsignedToFloat(At<uint32_t>(lexer, lexer::kInteger));
            else
                return 0;
            if (NextToken(lexer) != 0)
                return 0;
            continue;
        }
        if (IdentIs(lexer, kTextPiece, 5)) {
            while (more()) {
                token = NextToken(lexer);
                if (token == 0)
                    break;
                uint32_t count = 1;
                if (token == lexer::kTokenInteger) {
                    count = At<uint32_t>(lexer, lexer::kInteger);
                    if (NextToken(lexer) != '*')
                        return 0;
                    token = NextToken(lexer);
                }
                if (token == lexer::kTokenIdentifier) {
                    bool reversed = false;
                    if (IdentEquals(lexer, kTextRev)) {
                        if (NextToken(lexer) != '(')
                            return 0;
                        if (NextToken(lexer) != lexer::kTokenIdentifier)
                            return 0;
                        if (NextToken(lexer) != ')')
                            return 0;
                        reversed = true;
                    }
                    bool failed = false;
                    char* const source = FindPart(lexer, record, failed);
                    if (failed || !source)
                        return 0;
                    AppendPart(lexer, value, source, count, reversed);
                    continue;
                }
                if (token != '<')
                    return 0;
                float floatStep = 0.0f, positionStep = 0.0f, number = -1.0f;
                uint32_t endKind = 0;
                uint32_t a = 0, b = 0;
                if (!LexInt(lexer, &a))
                    return 0;
                if (NextToken(lexer) != ',')
                    return 0;
                if (!LexInt(lexer, &b))
                    return 0;
                token = NextToken(lexer);
                if (token == ',') {
                    if (!LexFloat(lexer, &floatStep))
                        return 0;
                    token = NextToken(lexer);
                    if (token == ',') {
                        if (!LexFloat(lexer, &positionStep))
                            return 0;
                        token = NextToken(lexer);
                    }
                }
                if (token != '>')
                    return 0;
                token = NextToken(lexer);
                if (token == ':') {
                    do {
                        token = NextToken(lexer);
                        if (token == lexer::kTokenInteger || token == lexer::kTokenFloat) {
                            At<int32_t>(lexer, lexer::kPushed) = token;
                            if (!LexFloat(lexer, &number))
                                return 0;
                        } else if (token == lexer::kTokenIdentifier) {
                            if (IdentEquals(lexer, kTextPylon))
                                endKind = 1;
                            else if (IdentEquals(lexer, kTextEnd))
                                endKind = 2;
                            else
                                return 0;
                        } else {
                            return 0;
                        }
                        token = NextToken(lexer);
                    } while (token == ',');
                }
                At<int32_t>(lexer, lexer::kPushed) = token;
                if (count == 0)
                    continue;
                At<uint32_t>(element, bridgevalue::kWord4) = a;
                At<uint8_t>(element, bridgevalue::kByte8) = static_cast<uint8_t>(b);
                At<float>(element, bridgevalue::kFloatC) = number;
                At<uint32_t>(element, bridgevalue::kEnd) = endKind;
                uint32_t n = count;
                do {
                    const float lastFloat = reinterpret_cast<float*>(floats.end)[-1];
                    At<float>(element, bridgevalue::kPosition) =
                        static_cast<float>(static_cast<double>(positionStep) + static_cast<double>(lastFloat));
                    CallThis<void, char*>(addr::SC4Vector20_PushBack, &elements, element);
                    const uint32_t divisor = At<uint32_t>(lexer, bridgelexer::kDivisor);
                    if (static_cast<uint32_t>(ElementCount(elements)) % divisor == 0) {
                        float add = static_cast<float>(static_cast<double>(floatStep) +
                                                       static_cast<double>(reinterpret_cast<float*>(floats.end)[-1]));
                        CallThis<void, float*>(addr::SC4FloatVector_PushBack, &floats, &add);
                    }
                } while (--n != 0);
            }
            continue;
        }
        if (IdentIs(lexer, kTextEnd, 3)) {
            if (NextToken(lexer) == 0)
                break;
            return 0;
        }
        return 0;
    }

    // FLD height, FADD the value, FSTP float: every element's float (in
    // blocks of four, then the rest; each once, in order), then every float.
    const int32_t elementCount = ElementCount(elements);
    for (uint32_t i = 0; i < static_cast<uint32_t>(elementCount); ++i) {
        float& at = At<float>(elements.begin, i * kElementSize);
        at = static_cast<float>(static_cast<double>(height) + static_cast<double>(at));
    }
    const int32_t floatCount = static_cast<int32_t>(floats.end - floats.begin) >> 2;
    for (uint32_t i = 0; i < static_cast<uint32_t>(floatCount); ++i) {
        float& at = reinterpret_cast<float*>(floats.begin)[i];
        at = static_cast<float>(static_cast<double>(height) + static_cast<double>(at));
    }
    return 1;
}

// ====================================================================================
// simulators.nettool.rulparse2: the check type parser
// ====================================================================================

// An error through the lexer's slot 0 (a game string of the text made with
// SC4String::FromText; 0x00648F10 does nothing), the string destroyed inline
// (`viaDestroy`: through SC4String::Destroy, as the game's third message).
void ReportError(char* lexer, std::uintptr_t text, bool viaDestroy)
{
    GameString s{};
    GameString* const made = CallThis<GameString*, const char*>(addr::SC4String_FromText, &s, Text(text));
    Virtual<void>(lexer, 0x0, made);
    if (viaDestroy) {
        CallThis<void>(addr::SC4String_Destroy, &s);
        return;
    }
    s.vtable = kStringVtable;
    FreeIfSet(s.begin);
}

// 0x0063F1F0 (no arguments, this a RUL0 lexer; AL): an identifier and '-'
// (else 0); the identifier must be one character ('Bad map tag': 0): the
// key (+0x28); then until the line's end, identifiers: 'check' clears
// +0x41, 'multiok' sets +0x40, 'optional' sets +0x42 and clears +0x41;
// else a network name (stricmp with the 13 English names; none: 'Expected
// network name', 0; a third: 'Too many network checks in one rule', 0): its
// index in +0x2C + n, then ':' and an edge word (ParseInt into +0x30 + 8n),
// the mask +0x34 + 8n -1, or after ',' a mask (ParseInt), else the token
// pushed back. Anything else: 0. At the line's end 1.
uint8_t __fastcall ParseCheckType(char* self, void*)
{
    static bool logged = false;
    FirstCall("ParseCheckType", logged);
    uint32_t count = 0;
    if (NextToken(self) != lexer::kTokenIdentifier)
        return 0;
    if (NextToken(self) != '-')
        return 0;
    if (At<char*>(self, lexer::kIdentEnd) - At<char*>(self, lexer::kIdentBegin) != 1) {
        ReportError(self, kTextBadMapTag, false);
        return 0;
    }
    At<uint8_t>(self, checklexer::kKey) = static_cast<uint8_t>(*At<char*>(self, lexer::kIdentBegin));
    int32_t token = NextToken(self);
    if (token == 0)
        return 1;
    char* mask = self + checklexer::kEdges + 4;
    for (;;) {
        if (token != lexer::kTokenIdentifier)
            return 0;
        if (IdentIs(self, kTextCheck, 5)) {
            At<uint8_t>(self, checklexer::kChecked) = 0;
        } else if (IdentEquals(self, kTextMultiOk)) {
            At<uint8_t>(self, checklexer::kMultiOk) = 1;
        } else if (IdentEquals(self, kTextOptional)) {
            At<uint8_t>(self, checklexer::kOptional) = 1;
            At<uint8_t>(self, checklexer::kChecked) = 0;
        } else {
            if (static_cast<int32_t>(count) >= 2) {
                ReportError(self, kTextTooManyChecks, true);
                return 0;
            }
            uint32_t type = 0;
            for (; type < kTypeCount; ++type) {
                const char* const name =
                    reinterpret_cast<const char* const*>(addr::cSC4NetworkManager_EnglishNames)[type];
                if (CallCdecl<int32_t, const char*, const char*>(addr::SC4_stricmp, At<char*>(self, lexer::kIdentBegin),
                                                                 name) == 0)
                    break;
            }
            if (type >= kTypeCount) {
                ReportError(self, kTextExpectedNetwork, false);
                return 0;
            }
            At<uint8_t>(self, checklexer::kTypes + count) = static_cast<uint8_t>(type);
            if (NextToken(self) != ':')
                return 0;
            if (!LexInt(self, mask - 4))
                return 0;
            At<uint32_t>(mask, 0) = 0xFFFFFFFFu;
            token = NextToken(self);
            if (token == ',') {
                if (!LexInt(self, mask))
                    return 0;
            } else {
                At<int32_t>(self, lexer::kPushed) = token;
            }
            ++count;
            mask += checklexer::kSide;
        }
        token = NextToken(self);
        if (token == 0)
            return 1;
    }
}

// ====================================================================================
// simulators.nettool.rulparse2: the type rows' exemplars and the tile verifiers
// ====================================================================================

// 0x0063F400 (cdecl: a resource key*, a type row's table +0x3C). The
// resource (the manager's slot 0xC, IID 0xA52160F5); none: the two empty
// vectors' buffers freed. Else from its property holder (slot 0x1C) the key
// 0xE974EAA1, a uint32 vector 0xE974EAA2 and a float vector 0xE974EAA3; its
// first eight floats copied as four points; for quarter turns 1 to 3 (the
// angle float(i * pi / 2)) each point turned about (0, 0) (0x005D77A0) and
// both coordinates appended to the float vector (inline growth); the
// table's value for the key (0x0063E400) assigned both vectors (0x00462C10,
// 0x00464430); the resource released, the buffers freed.
void __cdecl ExemplarCallback(void* key, char* table)
{
    static bool logged = false;
    FirstCall("ExemplarCallback", logged);
    void* resource = nullptr;
    Vector uints{}, floats{};
    void* const manager = Global<void*>(addr::SC4_ResourceManager);
    if (!Virtual<uint8_t>(manager, 0xC, key, kResourceIid, &resource, 0u, 0u)) {
        FreeIfSet(floats.begin);
        FreeIfSet(uints.begin);
        return;
    }
    void* const holder = Virtual<void*>(resource, 0x1C);
    // The original's key word is not initialised (a leftover when the
    // property is missing); ours 0.
    uint32_t value = 0;
    CallCdecl<uint8_t, void*, uint32_t, uint32_t*>(addr::SC4_GetPropertyUint32, holder, kExemplarKey, &value);
    CallCdecl<uint8_t, void*, uint32_t, Vector*>(addr::SC4_GetPropertyUint32Vector, holder, kExemplarUints, &uints);
    CallCdecl<uint8_t, void*, uint32_t, Vector*>(addr::SC4_GetPropertyFloatVector, holder, kExemplarFloats, &floats);
    uint32_t points[8];
    const uint32_t* const first = reinterpret_cast<const uint32_t*>(floats.begin);
    for (int i = 0; i < 4; ++i) {
        points[2 * i] = first[2 * i];
        points[2 * i + 1] = first[2 * i + 1];
    }
    float centre[2] = {0.0f, 0.0f};
    for (int32_t turn = 1; turn < 4; ++turn) {
        const float angle = static_cast<float>(static_cast<double>(turn) *
                                               static_cast<double>(Global<float>(addr::SC4_FloatHalfPi)));
        for (int p = 0; p < 4; ++p) {
            uint32_t out[2] = {};
            CallCdecl<void, float*, uint32_t*, uint32_t, uint32_t*>(addr::SC4_RotatePoint, centre, points + 2 * p,
                                                                     Bits(angle), out);
            PushFloatInline(floats, out[0]);
            PushFloatInline(floats, out[1]);
        }
    }
    char* const entry = CallThis<char*, uint32_t*>(addr::SC4NetworkTypeInfo_Table3CFindOrInsert, table, &value);
    CallThis<void*, Vector*>(addr::SC4Uint32Vector_CopyAssign, entry, &uints);
    CallThis<void*, Vector*>(addr::SC4FloatVector_Assign, entry + 0xC, &floats);
    ReleaseOf(resource);
    FreeIfSet(floats.begin);
    FreeIfSet(uints.begin);
}

// 0x0063F700 (cdecl: an ID, a type row's table +0x3C). Without a resource
// manager nothing. A key filter (operator new 0x14; SC4ResourceKeyFilter's
// constructor with 0x6534284A and the ID when the allocation worked; null
// otherwise, which the game then calls through as it does), referenced
// (slot 4), the manager's slot 0x44 (out*, the filter's +8); when it answers
// a list: the list's slot 0x1C (the callback 0x0063F400, the table), the
// list released; the filter released.
void __cdecl LoadExemplarsInto(uint32_t id, char* table)
{
    static bool logged = false;
    FirstCall("LoadExemplarsInto", logged);
    void* const manager = Global<void*>(addr::SC4_ResourceManager);
    if (!manager)
        return;
    void* list = nullptr;
    void* const memory = CallCdecl<void*, uint32_t>(addr::SC4_operator_new, 0x14);
    char* filter = nullptr;
    if (memory)
        filter = CallThis<char*, uint32_t, uint32_t>(addr::SC4ResourceKeyFilter_ctor, memory, kExemplarType, id);
    AddRefOf(filter);
    if (Virtual<uint32_t>(manager, 0x44, &list, filter + 8) != 0) {
        Virtual<void>(list, 0x1C, static_cast<std::uintptr_t>(addr::SC4NetworkTypeInfo_ExemplarCallback), table);
        ReleaseOf(list);
    }
    ReleaseOf(filter);
}

// 0x0063F780 (ret 4: an ID): 0x0063F700 with the ID and the row's table
// +0x3C.
void __fastcall LoadExemplars(char* self, void*, uint32_t id)
{
    static bool logged = false;
    FirstCall("LoadExemplars", logged);
    CallCdecl<void, uint32_t, char*>(addr::SC4NetworkTypeInfo_LoadExemplarsInto, id, self + typerow::kTable);
}

// 0x0063F7A0 (cdecl: key, value, the tile verifier table). The key's number
// (SC4::TextToInt); the value copied into a game string A (CopyBuffer, null
// for a null value), four empty strings B to E (8-byte buffers, in that
// order); the table grown for one more (SC4Uint32HashSet::Resize) and a node
// {the number, a copy of an empty point vector} inserted (0x0063D7E0; the
// value's vector freed after). Then while the last split found a comma: A
// split at ',' four times into B, C, D, E; b = atol(B), c = TextToInt(C), d
// = atol(D), e = atol(E) (bytes but c); the entry {c, (d + (e's byte ? 0x80
// : 0)) | b << 8 | the upper half of the insert's second result word}
// appended to the node's vector (in place, or a new buffer of size + max(size,
// 1) entries, the old ones copied one by one, the old buffer freed). The
// strings destroyed, E first.
void __cdecl TileVerifierLine(char* key, char* value, char* table)
{
    static bool logged = false;
    FirstCall("TileVerifierLine", logged);
    const int32_t id = CallCdecl<int32_t, const char*>(addr::SC4_TextToInt, At<char*>(key, 4));
    GameString a{};
    uint8_t tag = 0;
    CallThis<void, char*, uint32_t, uint32_t, uint8_t*>(addr::SC4String_CopyBuffer, &a.begin,
                                                        value ? value + 4 : nullptr, 0u, 0xFFFFFFFFu, &tag);
    a.vtable = kStringVtable;
    a.references = 0;
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
    Vector empty{};
    alignas(4) char inserted[0x10] = {};
    At<int32_t>(inserted, 0) = id;
    CallThis<void*, Vector*>(addr::SC4PointVector_CopyConstruct, inserted + 4, &empty);
    CallThis<void, uint32_t>(addr::SC4Uint32HashSet_Resize, table, At<uint32_t>(table, kTableCount) + 1);
    char* result[2] = {};
    CallThis<char**, char**, char*>(addr::SC4PointsHashTable_InsertEqualNoResize, table, result, inserted);
    char* const node = result[0];
    FreeIfSet(At<char*>(inserted, 4));
    uint32_t second = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(result[1]));
    uint8_t more = 0;
    do {
        CallThis<uint8_t, uint32_t, GameString*>(addr::SC4String_SplitAtChar, &a, uint32_t{','}, &parts[0]);
        CallThis<uint8_t, uint32_t, GameString*>(addr::SC4String_SplitAtChar, &a, uint32_t{','}, &parts[1]);
        CallThis<uint8_t, uint32_t, GameString*>(addr::SC4String_SplitAtChar, &a, uint32_t{','}, &parts[2]);
        more = CallThis<uint8_t, uint32_t, GameString*>(addr::SC4String_SplitAtChar, &a, uint32_t{','}, &parts[3]);
        const uint8_t b = static_cast<uint8_t>(CallCdecl<int32_t, const char*>(addr::SC4_atol, parts[0].begin));
        const uint32_t c = CallCdecl<uint32_t, const char*>(addr::SC4_TextToInt, parts[1].begin);
        const uint8_t d = static_cast<uint8_t>(CallCdecl<int32_t, const char*>(addr::SC4_atol, parts[2].begin));
        const uint8_t e = static_cast<uint8_t>(CallCdecl<int32_t, const char*>(addr::SC4_atol, parts[3].begin));
        const uint8_t flags = static_cast<uint8_t>((e != 0 ? 0x80u : 0u) + d);
        second = (second & 0xFFFF0000u) | (static_cast<uint32_t>(b) << 8) | flags;
        Vector& v = At<Vector>(node, pointtable::kPoints);
        if (v.end != v.capacity) {
            if (v.end) {
                At<uint32_t>(v.end, tileverifier::kWord) = c;
                At<uint32_t>(v.end, tileverifier::kFlags) = second;
            }
            v.end += tileverifier::kEntrySize;
            continue;
        }
        char* const end = v.end;
        const uint32_t size = static_cast<uint32_t>(static_cast<int32_t>(end - v.begin) >> 3);
        const uint32_t grow = size < 1 ? 1u : size;
        const uint32_t length = size + grow;
        char* const buffer = length ? Allocate(length * tileverifier::kEntrySize) : nullptr;
        char* to = buffer;
        for (char* from = v.begin; from != end; from += tileverifier::kEntrySize, to += tileverifier::kEntrySize) {
            if (to) {
                At<uint32_t>(to, 0) = At<uint32_t>(from, 0);
                At<uint32_t>(to, 4) = At<uint32_t>(from, 4);
            }
        }
        if (to) {
            At<uint32_t>(to, tileverifier::kWord) = c;
            At<uint32_t>(to, tileverifier::kFlags) = second;
        }
        to += tileverifier::kEntrySize;
        FreeIfSet(v.begin);
        v.begin = buffer;
        v.end = to;
        v.capacity = buffer + length * tileverifier::kEntrySize;
    } while (more);
    for (int i = 3; i >= 0; --i) {
        parts[i].vtable = kStringVtable;
        FreeIfSet(parts[i].begin);
    }
    a.vtable = kStringVtable;
    FreeIfSet(a.begin);
}

// 0x0063FAF0 (cdecl: the network ini, a section name): the tile verifier
// table 0x00B466DC cleared (0x00584660); the name in a game string
// (FromRange with the name's slot as tag), the ini's slot 0x34 (the string,
// the callback 0x0063F7A0, the table); the string destroyed.
void __cdecl LoadTileVerifiers(void* ini, const char* name)
{
    static bool logged = false;
    FirstCall("LoadTileVerifiers", logged);
    CallThis<void>(addr::SC4PtrVectorHashMap_Clear, reinterpret_cast<void*>(addr::cSC4NetworkTool_TileVerifiers));
    GameString section{};
    const char* tag = name;
    CallThis<void, const char*, const char*, const char**>(addr::SC4String_FromRange, &section.begin, name,
                                                           name + std::strlen(name), &tag);
    section.vtable = kStringVtable;
    section.references = 0;
    Virtual<uint8_t>(ini, 0x34, &section, static_cast<std::uintptr_t>(addr::cSC4NetworkTool_TileVerifierLineCallback),
                     static_cast<std::uintptr_t>(addr::cSC4NetworkTool_TileVerifiers));
    section.vtable = kStringVtable;
    FreeIfSet(section.begin);
}

// ====================================================================================
// simulators.nettool.rulparse2: the RUL0 line callback
// ====================================================================================

// The 21 static key strings: guard bit i, the text, the atexit thunk; the
// objects are 0x14 bytes apart, bit 0 (PieceType) at 0x00B46688 down to bit
// 20 (OneWayDir) at 0x00B464F8 (addr::cSC4NetworkTool_Rul0Keys).
struct KeyString {
    std::uintptr_t text;
    std::uintptr_t thunk;
};
inline constexpr KeyString kKeys[21] = {
    {0x00AA8E64, 0x00A7C250}, // PieceType
    {0x00AA8E5C, 0x00A7C510}, // Piece
    {0x00AA8E50, 0x00A7C4F0}, // ResetPieces
    {0x00AA8E44, 0x00A7C4D0}, // StepOffsets
    {0x00AA8E38, 0x00A7C4B0}, // CellLayout
    {0x00AA8E2C, 0x00A7C490}, // ConsLayout
    {0x00AA8E20, 0x00A7C470}, // CheckType
    {0x00AA8E14, 0x00A7C450}, // CopyFrom
    {0x00AA8E08, 0x00A7C430}, // Transpose
    {0x00AA8E00, 0x00A7C410}, // Rotate
    {0x00AA8DE8, 0x00A7C3F0}, // ReplacementIntersection
    {0x00AA8DD8, 0x00A7C3D0}, // ConvertQueryID
    {0x00AA8DC8, 0x00A7C3B0}, // PlaceQueryID
    {0x00AA8DB8, 0x00A7C390}, // HandleOffset
    {0x00AA8DA8, 0x00A7C370}, // PreviewEffect
    {0x00AA8DA0, 0x00A7C350}, // Costs
    {0x00AA8D90, 0x00A7C330}, // AutoTileBase
    {0x00AA8D80, 0x00A7C310}, // AutoPathBase
    {0x00AA8D74, 0x00A7C2F0}, // Translate
    {0x00AA8D68, 0x00A7C2D0}, // AutoPlace
    {0x00AA8D5C, 0x00A7C2B0}, // OneWayDir
};
enum Key : uint32_t {
    kPiece = 1,
    kStepOffsets = 3,
    kCellLayout = 4,
    kConsLayout = 5,
    kCheckType = 6,
    kCopyFrom = 7,
    kTranspose = 8,
    kRotate = 9,
    kReplacementIntersection = 10,
    kConvertQueryID = 11,
    kPlaceQueryID = 12,
    kHandleOffset = 13,
    kPreviewEffect = 14,
    kCosts = 15,
    kAutoTileBase = 16,
    kAutoPathBase = 17,
    kTranslate = 18,
    kAutoPlace = 19,
    kOneWayDir = 20,
};
inline constexpr std::size_t kKeyStringSize = 0x14;

void* KeyObject(uint32_t bit)
{
    return reinterpret_cast<void*>(addr::cSC4NetworkTool_Rul0Keys + (20 - bit) * kKeyStringSize);
}

// Each key string not made yet (its guard bit clear): the bit set, the
// string made (SC4String::FromText), its destructor thunk given to atexit.
void MakeKeys()
{
    uint32_t& guard = Global<uint32_t>(addr::cSC4NetworkTool_Rul0KeyGuard);
    for (uint32_t bit = 0; bit < 21; ++bit) {
        if (guard & (1u << bit))
            continue;
        guard |= 1u << bit;
        CallThis<void*, const char*>(addr::SC4String_FromText, KeyObject(bit), Text(kKeys[bit].text));
        CallCdecl<int32_t, std::uintptr_t>(addr::SC4_atexit, kKeys[bit].thunk);
    }
}

// The rule's check type (a lexer on the stack, its constructor at the
// game's address; the original's lexer is its stack leftovers but for what
// the constructor and the parser write, ours starts zeroed): when the check
// type parser accepts the value, the key's value (FindOrInsert in +0x64)
// gets the lexer's six words +0x2C to +0x40 and each non-negative type's bit
// (0x00AA8C10) is ORed into +0xE4; the identifier's buffer freed.
void ReadCheckType(void* value, char* context)
{
    alignas(4) char lexer[lexer::kSize] = {};
    CallThis<char*, void*>(addr::SC4RulLexer_Construct, lexer, value);
    if (CallThis<uint8_t>(addr::SC4RulLexer_ParseCheckType, lexer)) {
        uint8_t key = At<uint8_t>(lexer, checklexer::kKey);
        char* const entry = CallThis<char*, uint8_t*>(addr::SC4Rul0CheckTypes_FindOrInsert,
                                                      At<char*>(context, rul0ctx::kRule) + rul0::kCheckTypes, &key);
        for (std::size_t i = 0; i < checklexer::kValueWords; ++i)
            At<uint32_t>(entry, 4 * i) = At<uint32_t>(lexer, checklexer::kValue + 4 * i);
        const auto* const bits = reinterpret_cast<const uint32_t*>(addr::cSC4NetworkTool_TypeBits);
        const int8_t first = static_cast<int8_t>(At<uint8_t>(lexer, checklexer::kTypes));
        if (first >= 0)
            At<uint32_t>(At<char*>(context, rul0ctx::kRule), rul0::kWordE4) |= bits[first];
        const int8_t second = static_cast<int8_t>(At<uint8_t>(lexer, checklexer::kTypes + 1));
        if (second >= 0)
            At<uint32_t>(At<char*>(context, rul0ctx::kRule), rul0::kWordE4) |= bits[second];
    }
    FreeIfSet(At<char*>(lexer, lexer::kIdentBegin));
}

// 0x00640320 (cdecl: key, value (cIGZStrings), the RUL0 loader's context;
// pushed by 0x00641A50). The key strings made (MakeKeys); the context's
// rule (+0x8); the first key the key equals (its slot 0x28, case flag 0):
//   Piece: x (0x00910991 of the text), z (of the text after the first ','),
//     then after the next ',' sscanf ', %i , %i , 0x%x' (turn, flip, word):
//     exactly three: the record {word, turn, flip, x, z} appended (PushBack);
//   StepOffsets: sscanf ' %d , %d', exactly two: +0x10, +0x14;
//   CellLayout / ConsLayout: the value inserted at the end of the context's
//     list +0xC / +0x10 (SC4StringList::Insert before the head);
//   AutoTileBase / AutoPathBase: TextToUint32 into +0xC8 / +0xCC;
//   CheckType: ReadCheckType;
//   CopyFrom: the store's rule of the ID (TextToUint32; SC4Uint32HashTable::
//     Find in the context's +0x4) assigned to this one (0x0063E060), then
//     +0x0 and +0xEC to +0xF8 this rule's ID; none: nothing;
//   Transpose: 0x006091F0; Rotate: 0x006094B0 with TextToUint32;
//   Translate: sscanf ' %d , %d' (a missing number keeps the key's pointer
//     or the context's), 0x00608D20 with both;
//   ReplacementIntersection: the same scan (context, key defaults), +0x48 1,
//     +0x40, +0x44;
//   ConvertQueryID / PlaceQueryID: TextToUint32 into +0x78 / +0x7C;
//   HandleOffset: sscanf ' %d , %d', exactly two: +0x94, +0x98;
//   PreviewEffect: the string +0x80 assigned (cRZBaseString::Assign);
//   Costs: sscanf ' %d ' into +0xB4; AutoPlace: +0xE9 1; OneWayDir: +0xE8
//     the byte of atol.
// Anything else: nothing.
void __cdecl Rul0Line(void* key, void* value, char* context)
{
    static bool logged = false;
    FirstCall("Rul0Line", logged);
    char* const rule = At<char*>(context, rul0ctx::kRule);
    MakeKeys();
    const auto is = [&](uint32_t bit) { return Virtual<uint8_t>(key, 0x28, KeyObject(bit), 0u) != 0; };
    const auto text = [&]() { return At<char*>(value, 4); };
    const auto number = [&]() { return CallCdecl<uint32_t, const char*>(addr::SC4_TextToUint32, text()); };
    const int32_t keyWord = static_cast<int32_t>(reinterpret_cast<std::uintptr_t>(key));
    const int32_t contextWord = static_cast<int32_t>(reinterpret_cast<std::uintptr_t>(context));

    if (is(kPiece)) {
        const char* const first = text();
        float x = 0.0f, z = 0.0f;
        X87ParseStore(addr::SC4_ParseDouble910991, first, &x);
        const char* comma = CallCdecl<const char*, const char*, int32_t>(addr::SC4_StrChr, first, int32_t{','});
        if (!comma)
            return;
        const char* const second = comma + 1;
        X87ParseStore(addr::SC4_ParseDouble910991, second, &z);
        if (!second)
            return;
        comma = CallCdecl<const char*, const char*, int32_t>(addr::SC4_StrChr, second, int32_t{','});
        if (!comma)
            return;
        int32_t turn = keyWord, flip = 0, word = contextWord;
        using ScanFn = int(__cdecl*)(const char*, const char*, ...);
        if (reinterpret_cast<ScanFn>(addr::SC4_sscanf)(comma, Text(kFormatPiece), &turn, &flip, &word) != 3)
            return;
        // Built as the game's stack record: bytes +0x6 / +0x7 are not
        // written there (PushBack copies field by field).
        alignas(4) char record[record::kSize] = {};
        At<int32_t>(record, record::kWord) = word;
        At<uint8_t>(record, record::kTurn) = static_cast<uint8_t>(turn);
        At<uint8_t>(record, record::kFlip) = static_cast<uint8_t>(flip);
        At<float>(record, record::kX) = x;
        At<float>(record, record::kZ) = z;
        CallThis<void, char*>(addr::SC4Rul0RecordVector_PushBack, rule + rul0::kRecords, record);
        return;
    }
    if (is(kStepOffsets)) {
        int32_t a = contextWord, b = keyWord;
        if (Scan2(text(), kFormatPair, &a, &b) != 2)
            return;
        At<int32_t>(rule, rul0::kWord10) = a;
        At<int32_t>(rule, rul0::kWord14) = b;
        return;
    }
    const auto insertLine = [&](std::size_t list) {
        char* result = nullptr;
        CallThis<char**, char**, char*, void*>(addr::SC4StringList_Insert, context + list, &result,
                                               At<char*>(context, list), value);
    };
    if (is(kCellLayout)) {
        insertLine(rul0ctx::kCellLines);
        return;
    }
    if (is(kConsLayout)) {
        insertLine(rul0ctx::kConsLines);
        return;
    }
    if (is(kAutoTileBase)) {
        At<uint32_t>(rule, rul0::kWordC8) = number();
        return;
    }
    if (is(kAutoPathBase)) {
        At<uint32_t>(rule, rul0::kWordCC) = number();
        return;
    }
    if (is(kCheckType)) {
        ReadCheckType(value, context);
        return;
    }
    if (is(kCopyFrom)) {
        uint32_t id = number();
        char* found[2] = {};
        CallThis<char**, char**, uint32_t*>(addr::SC4Uint32HashTable_Find, At<char*>(context, rul0ctx::kStore), found,
                                            &id);
        if (!found[0])
            return;
        const uint32_t own = At<uint32_t>(rule, rul0::kId);
        CallThis<char*, char*>(addr::SC4Rul0Rule_Assign, rule, found[0] + store::kRule);
        At<uint32_t>(rule, rul0::kId) = own;
        for (std::size_t i = 0; i < 4; ++i)
            At<uint32_t>(rule, rul0::kWordsEC + 4 * i) = own;
        return;
    }
    if (is(kTranspose)) {
        CallThis<void>(addr::SC4Rul0Rule_Transpose, rule);
        return;
    }
    if (is(kRotate)) {
        CallThis<void, uint32_t>(addr::SC4Rul0Rule_Rotate, rule, number());
        return;
    }
    if (is(kTranslate)) {
        int32_t a = keyWord, b = contextWord;
        Scan2(text(), kFormatPair, &a, &b);
        CallThis<void, int32_t, int32_t>(addr::SC4Rul0Rule_Translate, rule, a, b);
        return;
    }
    if (is(kReplacementIntersection)) {
        int32_t a = contextWord, b = keyWord;
        Scan2(text(), kFormatPair, &a, &b);
        At<uint8_t>(rule, rul0::kByte48) = 1;
        At<int32_t>(rule, rul0::kWord40) = a;
        At<int32_t>(rule, rul0::kWord44) = b;
        return;
    }
    if (is(kConvertQueryID)) {
        At<uint32_t>(rule, rul0::kWord78) = number();
        return;
    }
    if (is(kPlaceQueryID)) {
        At<uint32_t>(rule, rul0::kWord7C) = number();
        return;
    }
    if (is(kHandleOffset)) {
        int32_t a = contextWord, b = keyWord;
        if (Scan2(text(), kFormatPair, &a, &b) != 2)
            return;
        At<int32_t>(rule, rul0::kOffsetX) = a;
        At<int32_t>(rule, rul0::kOffsetZ) = b;
        return;
    }
    if (is(kPreviewEffect)) {
        CallThis<void*, void*>(addr::cRZBaseString_Assign, rule + rul0::kText, value);
        return;
    }
    if (is(kCosts)) {
        using ScanFn = int(__cdecl*)(const char*, const char*, ...);
        reinterpret_cast<ScanFn>(addr::SC4_sscanf)(text(), Text(kFormatOne), rule + rul0::kWordB4);
        return;
    }
    if (is(kAutoPlace)) {
        At<uint8_t>(rule, rul0::kByteE9) = 1;
        return;
    }
    if (is(kOneWayDir)) {
        At<uint8_t>(rule, rul0::kRotation) =
            static_cast<uint8_t>(CallCdecl<int32_t, const char*>(addr::SC4_atol, text()));
        return;
    }
}

// ====================================================================================
// simulators.nettool.rulmaps2: the RUL0 rule and the type row's table +0x3C
// ====================================================================================

void CopyWords(char* to, char* from, std::size_t first, std::size_t last)
{
    for (std::size_t at = first; at <= last; at += 4)
        At<uint32_t>(to, at) = At<uint32_t>(from, at);
}

// A string's body assigned from another's as the game inlines it: the
// source's body (null for a null string) unless it is this one's,
// SC4String::AssignRange (begin, end, the source argument's slot as tag).
void AssignBody(char* self, char* source, std::size_t string)
{
    const bool set = reinterpret_cast<std::uintptr_t>(source) + string != 0;
    char* const from = set ? source + string + 4 : nullptr;
    if (from == self + string + 4)
        return;
    char* tag = source;
    CallThis<void, char*, char*, char**>(addr::SC4String_AssignRange, self + string + 4, At<char*>(from, 0),
                                         At<char*>(from, 4), &tag);
}

// 0x0063E060 (ret 4: the source rule*; EAX this): operator=, in the game's
// order: the ID, the records (0x0063C4F0), the words +0x10 to +0x24, the
// point vectors +0x28 and +0x34 (0x00611B00), +0x40, +0x44, the byte +0x48,
// the points +0x4C, the cells +0x58 (0x007AD7A0), unless the same rule the
// check types cleared (0x008EBA60) and copied (0x00626430), +0x78, +0x7C,
// the string +0x80, +0x94, +0x98, the cons grid (0x00461210), +0xA8 to
// +0xB4, the cell grid, +0xC4 to +0xCC, the cons cells (cleared, 0x006265B0),
// +0xE4, the bytes +0xE8, +0xE9, the words +0xEC to +0xF8.
char* __fastcall RuleAssign(char* self, void*, char* source)
{
    static bool logged = false;
    FirstCall("RuleAssign", logged);
    At<uint32_t>(self, rul0::kId) = At<uint32_t>(source, rul0::kId);
    CallThis<char*, char*>(addr::SC4Rul0RecordVector_Assign, self + rul0::kRecords, source + rul0::kRecords);
    CopyWords(self, source, rul0::kWord10, rul0::kMaxZ);
    CallThis<char*, char*>(addr::cSC4NetworkManager_PointVectorAssign, self + rul0::kPoints, source + rul0::kPoints);
    CallThis<char*, char*>(addr::cSC4NetworkManager_PointVectorAssign, self + rul0::kPoints34,
                           source + rul0::kPoints34);
    CopyWords(self, source, rul0::kWord40, rul0::kWord44);
    At<uint8_t>(self, rul0::kByte48) = At<uint8_t>(source, rul0::kByte48);
    CallThis<char*, char*>(addr::cSC4NetworkManager_PointVectorAssign, self + rul0::kChecked, source + rul0::kChecked);
    CallThis<char*, char*>(addr::SC4TrafficRecords_Update, self + rul0::kCells, source + rul0::kCells);
    if (source + rul0::kCheckTypes != self + rul0::kCheckTypes) {
        CallThis<void>(addr::SC4Uint32HashSet_Clear, self + rul0::kCheckTypes);
        CallThis<void, char*>(addr::SC4Rul0Rule_CopyCheckTypes, self + rul0::kCheckTypes, source + rul0::kCheckTypes);
    }
    CopyWords(self, source, rul0::kWord78, rul0::kWord7C);
    AssignBody(self, source, rul0::kText);
    CopyWords(self, source, rul0::kOffsetX, rul0::kOffsetZ);
    CallThis<char*, char*>(addr::SC4ByteVector_Assign, self + rul0::kConsGrid, source + rul0::kConsGrid);
    CopyWords(self, source, rul0::kConsWidth, rul0::kWordB4);
    CallThis<char*, char*>(addr::SC4ByteVector_Assign, self + rul0::kCellGrid, source + rul0::kCellGrid);
    CopyWords(self, source, rul0::kCellWidth, rul0::kWordCC);
    if (source + rul0::kConsCells != self + rul0::kConsCells) {
        CallThis<void>(addr::SC4Uint32HashSet_Clear, self + rul0::kConsCells);
        CallThis<void, char*>(addr::SC4Rul0Rule_CopyConsCells, self + rul0::kConsCells, source + rul0::kConsCells);
    }
    At<uint32_t>(self, rul0::kWordE4) = At<uint32_t>(source, rul0::kWordE4);
    At<uint8_t>(self, rul0::kRotation) = At<uint8_t>(source, rul0::kRotation);
    At<uint8_t>(self, rul0::kByteE9) = At<uint8_t>(source, rul0::kByteE9);
    CopyWords(self, source, rul0::kWordsEC, rul0::kWordsEC + 0xC);
    return self;
}

// 0x0063E400 (ret 4: a key*; EAX the value): the node of the key in its
// bucket (key % buckets): its value (node + 8); else a value {the key, a
// copy of an empty uint32 vector (0x00462F00), a copy of an empty float
// vector (0x00464760)} inserted (0x0063D9A0): the node + 8; the value's
// buffers freed (its float vector first), then the empty ones'.
char* __fastcall Table3CFindOrInsert(char* self, void*, const uint32_t* key)
{
    static bool logged = false;
    FirstCall("Table3CFindOrInsert", logged);
    const uint32_t k = *key;
    for (char* node = Buckets(self)[k % BucketCount(self)]; node; node = At<char*>(node, table::kNext)) {
        if (At<uint32_t>(node, table3c::kKey) == k)
            return node + table3c::kUints;
    }
    Vector uints{}, floats{};
    alignas(4) char value[table3c::kValueFloats + 0xC] = {};
    At<uint32_t>(value, 0) = k;
    CallThis<void*, Vector*>(addr::SC4Uint32Vector_Copy, value + table3c::kValueUints, &uints);
    CallThis<void*, Vector*>(addr::SC4FloatVector_CopyConstruct, value + table3c::kValueFloats, &floats);
    char* const result =
        CallThis<char*, char*>(addr::SC4NetworkTypeInfo_Table3CInsertNoResize, self, value) + 4;
    FreeIfSet(At<char*>(value, table3c::kValueFloats));
    FreeIfSet(At<char*>(value, table3c::kValueUints));
    FreeIfSet(floats.begin);
    FreeIfSet(uints.begin);
    return result;
}

// ====================================================================================
// simulators.nettool.rulmaps2: the bridge record's int-keyed map
// ====================================================================================

// 0x0063E500 (ret 0xC: result*, a hint node, value*; EAX result): STLport's
// insert_unique(hint, value) with signed keys:
//   the hint is the leftmost: an empty map: insert_unique (0x0063DC70);
//     the key below the hint's: InsertAt (hint, hint); equal: the hint;
//     else the node after it (0x005D17C0): the head, or the key below its
//     key: after the hint (InsertAt (0, hint, w hint) when the hint has no
//     right child, else (after, after)); else insert_unique;
//   the hint is the head: the rightmost's key below the key: InsertAt (0,
//     rightmost, w head); else insert_unique;
//   else the node before it (0x006809E0): the key below the hint's and the
//     before's below the key: before the hint (InsertAt (0, before, w
//     before) when before has no right child, else (hint, hint)); else the
//     node after the hint: the key above the hint's and (after is the head
//     or the key below its): after the hint as above; equal: the hint; else
//     insert_unique.
// insert_unique's answer: its node (through the {node, byte} it fills).
char** __fastcall IntMapInsertHint(char* self, void*, char** result, char* hint, char* value)
{
    static bool logged = false;
    FirstCall("IntMapInsertHint", logged);
    char* const head = At<char*>(self, tree::kHead);
    const auto insertAt = [&](char* x, char* y, char* w) {
        CallThis<char**, char**, char*, char*, char*, char*>(addr::SC4BridgeIntMap_InsertAt, self, result, x, y, value,
                                                             w);
        return result;
    };
    const auto unique = [&]() {
        alignas(4) char pair[8] = {};
        char* const answer = CallThis<char*, char*, char*>(addr::SC4BridgeIntMap_InsertUnique, self, pair, value);
        *result = At<char*>(answer, 0);
        return result;
    };
    const auto at = [&]() {
        *result = hint;
        return result;
    };
    const auto afterHint = [&](char* after) {
        if (!At<char*>(hint, tree::kRight))
            return insertAt(nullptr, hint, hint);
        return insertAt(after, after, nullptr);
    };
    if (hint == At<char*>(head, tree::kLeft)) {
        if (At<uint32_t>(self, tree::kCount) == 0)
            return unique();
        const int32_t key = At<int32_t>(value, 0);
        const int32_t hintKey = At<int32_t>(hint, intmap::kKey);
        if (hintKey > key)
            return insertAt(hint, hint, nullptr);
        if (!(hintKey < key))
            return at();
        char* after = hint;
        CallThis<void>(addr::SC4MapIterator_Next, &after);
        if (after == At<char*>(self, tree::kHead))
            return insertAt(nullptr, hint, hint);
        if (key < At<int32_t>(after, intmap::kKey))
            return afterHint(after);
        return unique();
    }
    if (hint == head) {
        char* const rightmost = At<char*>(head, tree::kRight);
        if (At<int32_t>(rightmost, intmap::kKey) < At<int32_t>(value, 0))
            return insertAt(nullptr, rightmost, hint);
        return unique();
    }
    char* before = hint;
    CallThis<void>(addr::SC4TreeIterator_Decrement, &before);
    const int32_t key = At<int32_t>(value, 0);
    const bool below = key < At<int32_t>(hint, intmap::kKey);
    if (below && At<int32_t>(before, intmap::kKey) < key) {
        if (!At<char*>(before, tree::kRight))
            return insertAt(nullptr, before, before);
        return insertAt(hint, hint, nullptr);
    }
    char* after = hint;
    CallThis<void>(addr::SC4MapIterator_Next, &after);
    bool above = !below;
    if (!below) {
        above = At<int32_t>(hint, intmap::kKey) < key;
        if (!above)
            return at();
        if (after == At<char*>(self, tree::kHead) || key < At<int32_t>(after, intmap::kKey))
            return afterHint(after);
    }
    if (below == above)
        return at();
    return unique();
}

// 0x0063E710 (ret 4: the source map*; EAX this): the head pointer 0, a
// 0x2C-byte head, the count 0, the head's color byte 0; an empty source:
// the head linked to itself (no parent); else the source's root copied
// (0x0063DBB0 under the head), the leftmost and rightmost found; the count
// copied.
char* __fastcall IntMapCopyConstruct(char* self, void*, char* source)
{
    static bool logged = false;
    FirstCall("IntMapCopyConstruct", logged);
    At<char*>(self, tree::kHead) = nullptr;
    char* const head = Allocate(intmap::kNodeSize);
    At<char*>(self, tree::kHead) = head;
    At<uint32_t>(self, tree::kCount) = 0;
    char* const root = At<char*>(At<char*>(source, tree::kHead), tree::kParent);
    At<uint8_t>(head, intmap::kColor) = 0;
    if (!root) {
        At<char*>(At<char*>(self, tree::kHead), tree::kParent) = nullptr;
        At<char*>(At<char*>(self, tree::kHead), tree::kLeft) = At<char*>(self, tree::kHead);
        At<char*>(At<char*>(self, tree::kHead), tree::kRight) = At<char*>(self, tree::kHead);
        At<uint32_t>(self, tree::kCount) = At<uint32_t>(source, tree::kCount);
        return self;
    }
    At<char*>(head, tree::kParent) = CallThis<char*, char*, char*>(addr::SC4BridgeIntMap_CopySubtree, self, root, head);
    char* const h = At<char*>(self, tree::kHead);
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
// simulators.nettool.rulmaps2: the string map and the RUL1 map
// ====================================================================================

// 0x0063E7C0 (ret 4: the source map*): this map's bucket vector emptied
// (end = begin; its old nodes are not freed: the callers copy into empty
// maps), reserved (PtrVector::Reserve) and filled with the source's bucket
// count of nulls (SC4PtrVector::FillInsert); each source bucket's chain
// copied in order into 0x30-byte nodes (next 0): the first through CopyPair
// 0x0063D940, the others inline (the string's body from the source's
// characters (0x00408420, the length capped at 0xFFFFFFFF, the source
// argument's slot as tag), the vtable, no references, the 20-byte vector
// 0x0063C150, the float vector 0x00464760); the count copied.
void __fastcall StringMapCopyFrom(char* self, void*, char* source)
{
    static bool logged = false;
    FirstCall("StringMapCopyFrom", logged);
    Vector& buckets = At<Vector>(self, table::kBuckets);
    buckets.end = buckets.begin;
    CallThis<void, uint32_t>(addr::PtrVector_Reserve, &buckets, BucketCount(source));
    uint32_t zero = 0;
    CallThis<void, char*, uint32_t, uint32_t*>(addr::SC4PtrVector_FillInsert, &buckets, buckets.end,
                                               BucketCount(source), &zero);
    for (uint32_t i = 0; i < BucketCount(source); ++i) {
        char* from = Buckets(source)[i];
        if (!from)
            continue;
        char* const node = Allocate(stringmap::kNodeSize);
        At<char*>(node, table::kNext) = nullptr;
        if (node + stringmap::kPair)
            CallThis<char*, char*>(addr::SC4StringHashMap_CopyPair, node + stringmap::kPair, from + stringmap::kPair);
        Buckets(self)[i] = node;
        char* last = node;
        for (from = At<char*>(from, table::kNext); from; from = At<char*>(from, table::kNext)) {
            char* const copy = Allocate(stringmap::kNodeSize);
            char* const pair = copy + stringmap::kPair;
            At<char*>(copy, table::kNext) = nullptr;
            if (pair) {
                char* const sourcePair = from + stringmap::kPair;
                char* const body = sourcePair ? sourcePair + 4 : nullptr;
                At<char*>(pair, 4) = nullptr;
                At<char*>(pair, 8) = nullptr;
                At<char*>(pair, 0xC) = nullptr;
                char* const first = At<char*>(body, 0);
                const uint32_t length = static_cast<uint32_t>(At<char*>(body, 4) - first);
                const uint32_t capped = length < 0xFFFFFFFFu ? length : 0xFFFFFFFFu;
                char* tag = source;
                CallThis<void, char*, char*, char**>(addr::cSC4Advice_F408420, pair + 4, first, first + capped, &tag);
                At<std::uintptr_t>(pair, 0) = kStringVtable;
                At<uint32_t>(pair, 0x10) = 0;
                CallThis<char*, char*>(addr::SC4Vector20_CopyConstruct, pair + stringmap::kVector20,
                                       sourcePair + stringmap::kVector20);
                CallThis<void*, char*>(addr::SC4FloatVector_CopyConstruct, pair + stringmap::kFloats,
                                       sourcePair + stringmap::kFloats);
            }
            At<char*>(last, table::kNext) = copy;
            last = copy;
        }
    }
    At<uint32_t>(self, kTableCount) = At<uint32_t>(source, kTableCount);
}

// 0x0063FB80 (ret 4: a pair*; EAX the node + 4): the map grown for one
// more (0x0063C290), the bucket 0 % buckets (every key's), a 0x30-byte node
// (next 0; the pair copied by 0x0063D940 when the node + 4 is not null) put
// first in it; the count + 1.
char* __fastcall StringMapInsertNoResize(char* self, void*, char* pair)
{
    static bool logged = false;
    FirstCall("StringMapInsertNoResize", logged);
    CallThis<void, uint32_t>(addr::SC4StringHashMap_Resize, self, At<uint32_t>(self, kTableCount) + 1);
    const uint32_t bucket = 0u % BucketCount(self);
    char* const first = Buckets(self)[bucket];
    char* const node = Allocate(stringmap::kNodeSize);
    char* const entry = node + stringmap::kPair;
    At<char*>(node, table::kNext) = nullptr;
    if (entry)
        CallThis<char*, char*>(addr::SC4StringHashMap_CopyPair, entry, pair);
    At<char*>(node, table::kNext) = first;
    Buckets(self)[bucket] = node;
    At<uint32_t>(self, kTableCount) += 1;
    return entry;
}

// The RUL1 map's node value written when the node + 0x10 is not null: the
// two types, the pair table's +0x4 to +0x10 zeroed, the table copied
// (0x0063CAA0).
void FillRul1Node(char* node, const char* value)
{
    char* const v = node + tree::kValue;
    if (!v)
        return;
    At<uint32_t>(v, 0) = At<uint32_t>(const_cast<char*>(value), 0);
    At<uint32_t>(v, 4) = At<uint32_t>(const_cast<char*>(value), 4);
    char* const t = v + rul1node::kValueTable;
    At<uint32_t>(t, table::kBuckets) = 0;
    At<uint32_t>(t, table::kBucketsEnd) = 0;
    At<uint32_t>(t, table::kBucketsCapacity) = 0;
    At<uint32_t>(t, table::kCount) = 0;
    CallThis<void, const char*>(addr::SC4PairHashTable_CopyFrom, t, value + rul1node::kValueTable);
}

// 0x0063E980 (ret 4: a value*: two types, a pair table; EAX the node): a
// 0x2C-byte node with the value (links and color not written).
char* __fastcall Rul1CreateNode(char* self, void*, const char* value)
{
    static bool logged = false;
    FirstCall("Rul1CreateNode", logged);
    (void)self;
    char* const node = Allocate(rul1node::kNodeSize);
    FillRul1Node(node, value);
    return node;
}

// 0x0063FBF0 (ret 0x14: result*, x, y, value*, w; EAX result): linked left
// of y when y is the head, else right when w is set, else left when x is
// set or the key pair is below y's (first word, then second; signed), else
// right. Right: the node from CreateNode 0x0063E980, y's right child, the
// head's rightmost updated; left: a node with the value made inline, y's
// left child, the head's root and rightmost (y the head) or leftmost
// updated. The node's children null, its parent y; rebalanced (0x0040D5E0);
// the count + 1; *result = node.
char** __fastcall Rul1InsertAt(char* self, void*, char** result, char* x, char* y, const int32_t* value, char* w)
{
    static bool logged = false;
    FirstCall("Rul1InsertAt", logged);
    bool left = false;
    if (y == At<char*>(self, tree::kHead)) {
        left = true;
    } else if (w) {
        left = false;
    } else if (x) {
        left = true;
    } else {
        const int32_t first = At<int32_t>(y, rul1map::kFirst);
        if (first > value[0])
            left = true;
        else if (first < value[0])
            left = false;
        else
            left = value[1] < At<int32_t>(y, rul1map::kSecond);
    }
    char* node = nullptr;
    if (!left) {
        node = CallThis<char*, const int32_t*>(addr::SC4Rul1Map_CreateNode, self, value);
        At<char*>(y, tree::kRight) = node;
        char* const h = At<char*>(self, tree::kHead);
        if (y == At<char*>(h, tree::kRight))
            At<char*>(h, tree::kRight) = node;
    } else {
        node = Allocate(rul1node::kNodeSize);
        FillRul1Node(node, reinterpret_cast<const char*>(value));
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

// ====================================================================================
// simulators.nettool.rulmaps2: the bridge record
// ====================================================================================

// 0x0063FCD0 (ret 4: the source record*; EAX this): field by field in the
// game's order: +0x0 to +0x10, the points +0x14 (0x00611B00), the floats
// +0x20 (0x00464430), +0x2C to +0x40, the points +0x44, the floats +0x50,
// +0x5C, the floats +0x60, +0x6C, +0x78, +0x7C, the floats +0x80, +0x8C, the
// bytes +0x98, +0x99, the words +0x9C to +0xD4, the byte +0xD8, the words
// +0xDC to +0x154, the name's body (AssignRange unless the same), the int
// map (0x0063DD30).
char* __fastcall BridgeRecordAssign(char* self, void*, char* source)
{
    static bool logged = false;
    FirstCall("BridgeRecordAssign", logged);
    CopyWords(self, source, 0x0, 0x10);
    CallThis<char*, char*>(addr::cSC4NetworkManager_PointVectorAssign, self + 0x14, source + 0x14);
    CallThis<void*, char*>(addr::SC4FloatVector_Assign, self + 0x20, source + 0x20);
    CopyWords(self, source, 0x2C, 0x40);
    CallThis<char*, char*>(addr::cSC4NetworkManager_PointVectorAssign, self + 0x44, source + 0x44);
    CallThis<void*, char*>(addr::SC4FloatVector_Assign, self + 0x50, source + 0x50);
    CopyWords(self, source, 0x5C, 0x5C);
    CallThis<void*, char*>(addr::SC4FloatVector_Assign, self + 0x60, source + 0x60);
    CallThis<void*, char*>(addr::SC4FloatVector_Assign, self + 0x6C, source + 0x6C);
    CopyWords(self, source, 0x78, 0x7C);
    CallThis<void*, char*>(addr::SC4FloatVector_Assign, self + 0x80, source + 0x80);
    CallThis<void*, char*>(addr::SC4FloatVector_Assign, self + 0x8C, source + 0x8C);
    At<uint8_t>(self, 0x98) = At<uint8_t>(source, 0x98);
    At<uint8_t>(self, 0x99) = At<uint8_t>(source, 0x99);
    CopyWords(self, source, 0x9C, 0xD4);
    At<uint8_t>(self, 0xD8) = At<uint8_t>(source, 0xD8);
    CopyWords(self, source, 0xDC, 0x154);
    AssignBody(self, source, bridgerecord::kNameVtable);
    CallThis<char*, char*>(addr::SC4BridgeIntMap_Assign, self + bridgerecord::kMapHead,
                           source + bridgerecord::kMapHead);
    return self;
}

// 0x00640030 (no arguments; EAX this): the eight vectors emptied, the 24
// floats +0xE4 to +0x140 from 0x00B466CC (read for each), the name an empty
// game string (an 8-byte buffer), the int map a 0x2C-byte head (color 0, no
// root, linked to itself) with count 0.
char* __fastcall BridgeRecordConstruct(char* self, void*)
{
    static bool logged = false;
    FirstCall("BridgeRecordConstruct", logged);
    for (const std::size_t at : bridgerecord::kVectors) {
        At<char*>(self, at) = nullptr;
        At<char*>(self, at + 4) = nullptr;
        At<char*>(self, at + 8) = nullptr;
    }
    for (std::size_t i = 0; i < bridgerecord::kFloatCount; ++i)
        At<uint32_t>(self, bridgerecord::kFloats + 4 * i) = Global<uint32_t>(addr::SC4NetworkBridgeRecord_DefaultFloat);
    At<char*>(self, bridgerecord::kNameBegin) = nullptr;
    At<char*>(self, bridgerecord::kNameEnd) = nullptr;
    At<char*>(self, bridgerecord::kNameCapacity) = nullptr;
    char* const buffer = Allocate(8);
    At<char*>(self, bridgerecord::kNameBegin) = buffer;
    At<char*>(self, bridgerecord::kNameEnd) = buffer;
    At<char*>(self, bridgerecord::kNameCapacity) = buffer + 8;
    *buffer = 0;
    At<std::uintptr_t>(self, bridgerecord::kNameVtable) = kStringVtable;
    At<uint32_t>(self, bridgerecord::kNameReferences) = 0;
    At<char*>(self, bridgerecord::kMapHead) = nullptr;
    char* const head = Allocate(intmap::kNodeSize);
    At<char*>(self, bridgerecord::kMapHead) = head;
    At<uint32_t>(self, bridgerecord::kMapCount) = 0;
    At<uint8_t>(head, intmap::kColor) = 0;
    At<char*>(At<char*>(self, bridgerecord::kMapHead), tree::kParent) = nullptr;
    At<char*>(At<char*>(self, bridgerecord::kMapHead), tree::kLeft) = At<char*>(self, bridgerecord::kMapHead);
    At<char*>(At<char*>(self, bridgerecord::kMapHead), tree::kRight) = At<char*>(self, bridgerecord::kMapHead);
    return self;
}

// 0x00640230 (no arguments): the int map's nodes erased when it has any
// (0x0063D700 from the root; the head relinked, the count 0), its head
// freed; the name's vtable written and its buffer freed; the vectors +0x8C,
// +0x80, +0x6C, +0x60, +0x50, +0x44, +0x20, +0x14 freed.
void __fastcall BridgeRecordDestruct(char* self, void*)
{
    static bool logged = false;
    FirstCall("BridgeRecordDestruct", logged);
    char* const map = self + bridgerecord::kMapHead;
    if (At<uint32_t>(self, bridgerecord::kMapCount)) {
        CallThis<void, char*>(addr::SC4BridgeIntMap_EraseSubtree, map,
                              At<char*>(At<char*>(map, tree::kHead), tree::kParent));
        At<char*>(At<char*>(map, tree::kHead), tree::kLeft) = At<char*>(map, tree::kHead);
        At<char*>(At<char*>(map, tree::kHead), tree::kParent) = nullptr;
        At<char*>(At<char*>(map, tree::kHead), tree::kRight) = At<char*>(map, tree::kHead);
        At<uint32_t>(map, tree::kCount) = 0;
    }
    FreeIfSet(At<char*>(map, tree::kHead));
    At<std::uintptr_t>(self, bridgerecord::kNameVtable) = kStringVtable;
    FreeIfSet(At<char*>(self, bridgerecord::kNameBegin));
    for (const std::size_t at : {std::size_t{0x8C}, std::size_t{0x80}, std::size_t{0x6C}, std::size_t{0x60},
                                 std::size_t{0x50}, std::size_t{0x44}, std::size_t{0x20}, std::size_t{0x14}})
        FreeIfSet(At<char*>(self, at));
}

// ====================================================================================
// Installation
// ====================================================================================

#define OPENSC4_DETOUR(address, fn) OPENSC4_NETTOOL_DETOUR(address, fn)

const Detour kRulParse2[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkTool_AppendTypeName, AppendTypeNameThunk),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_ReadRul1Pairs, ReadRul1Pairs),
    OPENSC4_DETOUR(addr::SC4RulLexer_ParseInt, ParseInt),
    OPENSC4_DETOUR(addr::SC4RulLexer_ParseFloat, ParseFloat),
    OPENSC4_DETOUR(addr::SC4RulLexer_ParseBridgeLayout, ParseBridgeLayout),
    OPENSC4_DETOUR(addr::SC4RulLexer_ParseCheckType, ParseCheckType),
    OPENSC4_DETOUR(addr::SC4NetworkTypeInfo_ExemplarCallback, ExemplarCallback),
    OPENSC4_DETOUR(addr::SC4NetworkTypeInfo_LoadExemplarsInto, LoadExemplarsInto),
    OPENSC4_DETOUR(addr::SC4NetworkTypeInfo_LoadExemplars, LoadExemplars),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_TileVerifierLineCallback, TileVerifierLine),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_LoadTileVerifiers, LoadTileVerifiers),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_Rul0LineCallback, Rul0Line),
};

const Detour kRulMaps2[] = {
    OPENSC4_DETOUR(addr::SC4Rul0Rule_Assign, RuleAssign),
    OPENSC4_DETOUR(addr::SC4NetworkTypeInfo_Table3CFindOrInsert, Table3CFindOrInsert),
    OPENSC4_DETOUR(addr::SC4BridgeIntMap_InsertHint, IntMapInsertHint),
    OPENSC4_DETOUR(addr::SC4BridgeIntMap_CopyConstruct, IntMapCopyConstruct),
    OPENSC4_DETOUR(addr::SC4StringHashMap_CopyFrom, StringMapCopyFrom),
    OPENSC4_DETOUR(addr::SC4StringHashMap_InsertNoResize, StringMapInsertNoResize),
    OPENSC4_DETOUR(addr::SC4Rul1Map_CreateNode, Rul1CreateNode),
    OPENSC4_DETOUR(addr::SC4Rul1Map_InsertAt, Rul1InsertAt),
    OPENSC4_DETOUR(addr::SC4NetworkBridgeRecord_Assign, BridgeRecordAssign),
    OPENSC4_DETOUR(addr::SC4NetworkBridgeRecord_Construct, BridgeRecordConstruct),
    OPENSC4_DETOUR(addr::SC4NetworkBridgeRecord_Destruct, BridgeRecordDestruct),
};

#undef OPENSC4_DETOUR

bool InstallRulParse2(registry::Mode) { return InstallDetours(kRulParse2, "rulparse2"); }
bool InstallRulMaps2(registry::Mode) { return InstallDetours(kRulMaps2, "rulmaps2"); }

const bool g_registered[] = {
    registry::Register({"simulators.nettool.rulparse2", "simulators/nettool", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkTool_Rul0LineCallback,
                        "Our network tool's RUL parsers, second part: the RUL0 line callback, the check type and "
                        "bridge layout parsers with the lexer's ParseInt and ParseFloat, the RUL1 section reader "
                        "with AppendTypeName, the tile verifier load and line callback, the type rows' exemplar "
                        "load (12 detours; src/core/nettool_rul_4.cpp)",
                        false, InstallRulParse2}),
    registry::Register({"simulators.nettool.rulmaps2", "simulators/nettool", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::SC4NetworkBridgeRecord_Construct,
                        "Our network tool's rule containers, second part: the RUL0 rule's Assign, a type row's "
                        "table +0x3C FindOrInsert, the bridge int map's hinted insert and copy, the string map's "
                        "CopyFrom and insert, the RUL1 map's CreateNode and InsertAt, the bridge record's "
                        "constructor, destructor and Assign (11 detours; src/core/nettool_rul_4.cpp)",
                        false, InstallRulMaps2}),
};

} // namespace
} // namespace opensc4::nettool_sim
