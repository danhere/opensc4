// Our cSC4NetworkTool, part t12, first group (build 641; the class map is in
// src/core/nettool_sim.h, the RUL0 rule layouts in src/core/nettool_rul.h):
// the RUL0 intersection question. One group, a list of detours at the game's
// addresses:
//   simulators.nettool.puzzle: slot 49 QueryUserForPotentialIntersections
//     0x0062D450, the rule against a cell 0x00609A00 (SC4Rul0Rule::MatchAt)
//     and the RUL0 store's lookup 0x00625380 (3 detours).
//
// What they are (from the code). 0x00625380 (cdecl, one argument): the RUL0
// rule of an ID in the store 0x00B452B0, or null; EAX only is read by its
// callers (the guarded 0x0062CD50, the network view control's puzzle piece
// code 0x00660E60, 0x00660E80 and 0x00661010). 0x00609A00 (this a RUL0 rule,
// ret 0xC: x, z, the tool): says whether the rule's pattern fits at the cell
// (x, z). For every cell of the rule (+0x58, 12 bytes: x, z, a check type
// character) whose check type (the rule's table +0x64, a hash table keyed by
// the character) is not optional (node +0x1E): the tool's cell info of the
// cell (0x00633220; none: the answer 2). A checked type needs, when the cell
// has an occupant with the flag 0x100000 (or the cell info says so) and the
// rule's byte +0x48 is set, the occupant's complex piece origin (slot 0x11C)
// to be the rule's (+0x40, +0x44) moved to (x, z) (else the answer 1), and an
// immovable cell without that flag is the answer 1. Then for each of the check
// type's two (type, edge word, mask) sides with a type of 0 or more: the
// cell's network flags must contain the type's flag (table 0x00AA74F8; else
// 1) and the cell's edge word for the type, xor the pattern's, must show
// nothing under the mask (else 2); unless the check type says multi-ok
// (+0x1C) the cell may have no other network type (else 1); a cell with a
// one-way type (flag 0x400 among the sides') whose check type is not checked
// compares the cell's one-way bits (xor the rule's rotation byte, table
// 0x00AA752C) with the pattern's (else 2). With no cells of the rule left to
// test the answer is 0 when the rule has no points (+0x28), else the points
// (moved to (x, z)) must be dry and free of network occupants: the terrain's
// slot 0x88 (under water; answer 1), the occupant manager's slot 0x80 through
// a filter of the network mask 0x1F4F (an occupant there; answer 1); the
// answer is then 1 when the lots' collector 0x0061F7C0 (cdecl: the points
// vector, a region out, a cost out, a set, the tool's filter +0x364) says
// false, else 0. Slot 49 (no arguments): for each of the tool's override
// records (+0x08, 12 bytes: x, z, a word; the placing code's, t6): a new tool
// of type 2 from the network manager (GetNetworkTool(2, unique 1), referenced,
// Init), the first rule of the store with a non-zero text ID (+0x78) that fits
// there (MatchAt gives 0) is tried: the guarded puzzle placement 0x0062CD50
// (x, z, rule ID, 0) must accept it (AL), the tool's cost of the solution
// (slot 20) must not be above the city's cash (the city's slot 0xD0, the
// budget's slot 6, 64 bits); if the rule has an effect name (+0x84 .. +0x88)
// the effects manager (0x00B43D1C slot 7) makes the effect, which is turned
// into place with a transform (a 3 x 3 matrix, the position of the cell in
// metres plus the rule's first record (+0x04: a turn byte at +4, a flip byte
// at +5, x and z floats at +8 and +0xC), the terrain's altitude, scale 1)
// and started (slot 3); the localized text 0x2A592FD1 / the rule's +0x78
// (else "## Intersection query string missing ##") is asked in the question
// dialog 0x0078DFF0 (an empty title); on yes the puzzle placement is made
// again with its last argument 1, the budget charged (slot 8: cost, 0), and
// the sound 0xEA5EC59F played (0x00B43CB8 slot 11); then the effect is
// stopped (slot 4) and released, and the tool shut down (slot 4) and released.
// Only the first matching rule of the store's walk is used for an override
// record: every path out of the candidate leaves the walk. The world cache of
// the tool (+0x1C) is reset first (0x00647B20).
//
// MatchAt is also the t6 placing code's (nettool_place_1.cpp), which calls it
// at its address; the guarded 0x0062CD50 calls it at 0x0062D2A5 and
// the rule-cell code 0x0063B493 does too.
//
// Every function was written from the disassembly. Calls between part
// functions go to the game's addresses, so each detour stands alone and
// OPENSC4_NETTOOL_SKIP bisects them.
//
// NAM. The guarded 0x0062CD50 (NAM's FlexPieces jumps out at 0x0062CEDC and
// back at 0x0062CF42 with the game's frame) and 0x006099A0 (FlexPieces changes
// the immediate at 0x006099C4) stay the game's and are called at their
// addresses: 0x0062CD50 from slot 49 with the game's arguments. The guarded
// function calls 0x00625380 (at 0x0062CE1E) and 0x00609A00 (at 0x0062D2A5)
// and reads only EAX after either (scanned: ECX and EDX are written before
// any read, both branches; the network view control's calls read EAX only).
// NAM's DLL calls none of the three (its Rul0 hooks sit in the guarded
// functions, which keep using the rule layouts: this part writes nothing of
// the rules, the store, the multimap or the type table).
//
// Floats: slot 49 builds the effect's transform on the x87 at 53 bits. The
// position is FILD int, FADD 0.5, FMUL 16.0 and FADD the record's float,
// stored as a float (done in doubles: an int and a float are exact in double
// and the single rounding to float is the same); the rotation is the turn
// byte times the float -pi/2 (0x00AA88B4) and its FCOS and FSIN, which C++
// cannot repeat (naked X87Cos and X87Sin, each FLD qword + the instruction;
// the argument is exact in a double: a byte times a 24-bit float). Everything
// else is float stores of sums of floats and of a float times 0.0 (the signs
// of the zeros are kept: 0.0 * sin, cos * 0.0). The altitude is the
// terrain's slot 0x34 (two floats: the cell as floats), a value in ST0 stored
// as a float. Nothing stays on the x87 stack across a call.
//
// Registers. The scan of every direct caller of 0x00625380 and 0x00609A00
// (scratch regscan.py / eaxscan.py) found only EAX read; slot 49 and 50 are
// called through vtables (the network view control, the manager's
// DestroyNetworkOccupants, the bulldoze and the pipe burst), whose callers
// cannot rely on a register a virtual callee leaves.
//
// Wrappers: no file of src/core/game_*.cpp or *fix*.cpp detours a function of
// this part or tests a return address in 0x00609A00 to 0x0062D450 (the
// fixes.puzzle_piece detour is the satisfier's NextEntry 0x0065ED80, which
// nothing here calls); sim_timers.cpp's crew range 0x00605490 to 0x00608C50
// is past 0x00608C50 and nothing here reads a stopwatch. The question dialog
// 0x0078DFF0 may be ours (group ui.panels.part1); it is called at its
// address.
//
// Stack leftovers: the effect's transform is written whole but for its two
// padding bytes at +2 and +3 (the original passes the stack's; ours 0).
// MatchAt's complex origin outputs are written by the occupant's slot before
// they are read. The transform's other nine words are filled from the global
// 0x00B452AC before the matrices are built, and every word that is read is
// then written; ours does not repeat the fill.
//
// Calls, each count from the asm's pushes for that call and the callee's own
// `ret N` (slots as byte offsets):
//   - the part's own, at the game's addresses: MatchAt three (ret 0xC), the
//     guarded puzzle placement 0x0062CD50 four (ret 0x10);
//   - the game's: the cell entry three... one (ret 4), GetNetworkManager none,
//     the world cache's Reset none, NetworkFilterConstruct one (ret 4), the
//     collector five (cdecl), the point vector's InsertN five (ret 0x14), the
//     tree's EraseSubtree one (ret 4), the list's Clear2 none, operator new /
//     delete and SC4::stl_allocate / SC4::Free one (cdecl), LoadLocalizedText
//     four (cdecl), FromRange three (ret 0xC), AssignRange three (ret 0xC),
//     the question dialog four (cdecl), the lists' node allocator (game_access.h);
//   - a network manager's slot 0x14 two (type, unique); a tool's slots 4
//     (AddRef), 3 (Init), 0x50 (cost), 0x10 (Shutdown), 2 (Release); the
//     terrain's 0x88 two, 0x34 two; the occupant manager's 0x80 four; an
//     occupant's 0x20 one, 0x11C two; the city's 0xD0 none; the budget's 0x18
//     none (EDX:EAX), 0x20 two; the effects manager's 0x1C two; an effect's
//     0x1C one, 0xC one, 0x10 one, 8 none; the sound service's 0x2C five.
#include "core/nettool_occupants.h"
#include "core/nettool_place.h"
#include "core/nettool_sim.h"

#include "core/registry.h"

#include <opensc4/addresses.h>

#include <bit>
#include <cstddef>
#include <cstdint>

namespace opensc4::nettool_sim {
namespace {

using occupants::Entry;
using occupants::ListNode;
using occupants::StoreRule;

// ====================================================================================
// Layouts
// ====================================================================================

// The rule's fields this part reads beyond nettool_rul.h's.
namespace prule2 {
inline constexpr std::size_t kPoints = rul::rul0::kPoints, kPointsEnd = rul::rul0::kPoints + 4;
inline constexpr std::size_t kCellsEnd = rul::rul0::kCells + 4;
inline constexpr std::size_t kBuckets = 0x68, kBucketsEnd = 0x6C; // the check types' table (+0x64)
inline constexpr std::size_t kOriginX = 0x40, kOriginZ = 0x44;    // the complex piece origin the pattern wants
inline constexpr std::size_t kNeedsOrigin = 0x48;                 // uint8
inline constexpr std::size_t kTextId = 0x78;                      // the localized text's instance (0: no query)
inline constexpr std::size_t kEffectName = 0x84, kEffectNameEnd = 0x88; // the string at +0x80: its characters
} // namespace prule2

// A check type's node (the hash table at the rule's +0x64; nettool_rul.h's
// checktype has the checked byte): +0x4 the character (a byte), +0x8 and
// +0x9 the two signed network types, +0xC / +0x10 the first side's edge word
// and mask, +0x14 / +0x18 the second's, +0x1C multi-ok, +0x1D checked, +0x1E
// optional.
namespace pnode {
inline constexpr std::size_t kKey = 0x4, kTypes = 0x8, kPairs = 0xC, kPairSize = 8;
inline constexpr std::size_t kMultiOk = 0x1C, kChecked = 0x1D, kOptional = 0x1E;
} // namespace pnode

// The words of the network flags per type MatchAt tests (a table of the
// RUL0 code's own) and its rule rotation byte table.
inline uint32_t RuleTypeFlag(int32_t type)
{
    return Global<uint32_t>(static_cast<uint32_t>(addr::cSC4NetworkTool_RuleTypeFlags + type * 4));
}
inline uint8_t RuleRotationMask(uint32_t index)
{
    return Global<uint8_t>(static_cast<uint32_t>(addr::cSC4NetworkTool_RuleRotationMasks + index));
}
inline constexpr int8_t kOneWayType = rule::kOneWayType; // a local named `rule` hides the namespace

// The network occupant's slots (netocc_sim.h has the names).
namespace nslot {
inline constexpr std::size_t kHasFlag = 0x20, kGetComplexPieceOrigin = 0x11C;
} // namespace nslot

// The other objects' slots slot 49 calls.
namespace qslot {
inline constexpr std::size_t kGetNetworkTool = 0x14;                         // the network manager's
inline constexpr std::size_t kUnderWater = 0x88, kAltitude = 0x34;           // the terrain's
inline constexpr std::size_t kFirstByCell = 0x80;                            // the occupant manager's
inline constexpr std::size_t kCostOfSolution = 0x50;                         // the tool's (slot 20)
inline constexpr std::size_t kToolInit = 0x0C, kToolShutdown = 0x10;         // the tool's slots 3 and 4
inline constexpr std::size_t kBudget = 0xD0;                                 // the city's
inline constexpr std::size_t kCash = 0x18, kSpend = 0x20;                    // the budget's
inline constexpr std::size_t kMakeEffect = 0x1C;                             // the effects manager's
inline constexpr std::size_t kSetTransform = 0x1C, kStart = 0x0C, kStop = 0x10; // an effect's
inline constexpr std::size_t kPlaySound = 0x2C;                              // the sound service's
} // namespace qslot

// The question's texts and the numbers around it.
inline constexpr uint32_t kQuestionGroup = 0x2A592FD1;
inline constexpr uint32_t kSoundId = 0xEA5EC59F;
inline constexpr uint32_t kNetworkMask = 0x1F4F;
inline constexpr uint32_t kRulesToolType = 2; // the type of the tool slot 49 makes (the highway)
inline constexpr std::size_t kMissingTextLength = 0x27; // "## Intersection query string missing ##"
inline constexpr std::size_t kRegionCells = 0x10;       // the cell region's cells part (its vtable at +0x10)

// The effect's transform (0x38 bytes): +0 the number of parts set, +1 which
// (1 a translation, 4 a rotation), +4 the 3 x 3 matrix, +0x28 the position,
// +0x34 the scale. The bytes +2 and +3 are the stack's in the original.
struct Transform {
    uint8_t count;
    uint8_t parts;
    uint8_t padding[2];
    float matrix[9];
    float position[3];
    float scale;
};
static_assert(sizeof(Transform) == 0x38, "the transform is 0x38 bytes");

// A tree set as the inlined code keeps it on the stack: the head node and the count.
struct TreeSet {
    char* head;
    uint32_t count;
};

// ====================================================================================
// Helpers
// ====================================================================================

inline void Free(void* block) { CallCdecl<void, void*>(addr::SC4_Free, block); }

// A point appended to a local vector of 8-byte points as MatchAt inlines
// push_back: in place when there is room (nothing written at a null end),
// else the game's InsertN 0x007C61A0 (this the vector, where, the value's
// address, an unread tag's, 1, 1).
void PushPoint(Vector& v, uint32_t x, uint32_t z)
{
    uint32_t value[2] = {x, z};
    uint32_t tag = 0;
    char* const end = v.end;
    if (end != v.capacity) {
        if (end) {
            At<uint32_t>(end, 0) = x;
            At<uint32_t>(end, 4) = z;
        }
        v.end += kPointSize;
        return;
    }
    CallThis<void, char*, uint32_t*, uint32_t*, uint32_t, uint32_t>(addr::SC4PointVector_InsertN, reinterpret_cast<char*>(&v),
                                                                  end, value, &tag, 1u, 1u);
}

// A localized text: the game's string, the group, the instance and a flag; AL.
inline uint8_t LoadLocalizedText(GameString* out, uint32_t group, uint32_t instance)
{
    return CallCdecl<uint8_t, GameString*, uint32_t, uint32_t, uint32_t>(addr::SC4_LoadLocalizedText, out, group,
                                                                        instance, 0u);
}

// A new empty game string as slot 49 builds them: 8 characters from the STL
// allocator, a NUL, the vtable and a reference count of 0.
void InitGameString(GameString& s)
{
    s.begin = s.end = s.capacity = nullptr;
    char* const buffer = CallCdecl<char*, uint32_t>(addr::SC4_stl_allocate, static_cast<uint32_t>(kFirstTextBuffer));
    s.begin = buffer;
    s.end = buffer;
    s.capacity = buffer + kFirstTextBuffer;
    buffer[0] = 0;
    s.vtable = addr::SC4String_Vtable;
    s.references = 0;
}

// A game string's end of life as the game inlines it: the vtable written
// again, the characters freed.
void DestroyGameString(GameString& s)
{
    s.vtable = addr::SC4String_Vtable;
    if (s.begin)
        Free(s.begin);
}

// FLD qword, FLD ST0, FCOS, FSTP dword, FSIN, FSTP dword: the x87
// instructions C++ cannot repeat, each result rounded once, straight to a
// float. The argument is the turn byte times -pi/2 (exact in a double).
__declspec(naked) void __cdecl X87CosSin(double /*angle*/, float* /*cosine*/, float* /*sine*/)
{
    __asm fld qword ptr [esp + 4]
    __asm fld st(0)
    __asm fcos
    __asm mov eax, [esp + 12]
    __asm fstp dword ptr [eax]
    __asm fsin
    __asm mov eax, [esp + 16]
    __asm fstp dword ptr [eax]
    __asm ret
}

// ====================================================================================
// 0x00625380 (cdecl, one argument): the RUL0 store's rule of an ID
// ====================================================================================

// The bucket key % count (the count the bucket vector's length shifted
// arithmetically; the division unsigned, so no buckets is the game's #DE), the
// chain by the key at +4; the rule at the node + 8, else null. EAX only.
char* __cdecl FindRule(uint32_t id)
{
    static bool logged = false;
    FirstCall("FindRule", logged);
    return occupants::StoreRule(id);
}

// ====================================================================================
// 0x00609A00 (SC4Rul0Rule::MatchAt, thiscall, ret 0xC): the rule at a cell
// ====================================================================================

// Answers 0 (fits), 1 (does not) or 2 (the cell's network disagrees or there
// is no cell info). See the top of this file.
uint32_t __fastcall MatchAt(char* rule, void*, uint32_t x, uint32_t z, char* tool)
{
    static bool logged = false;
    FirstCall("RuleMatchAt", logged);
    const uint8_t rotationMask = RuleRotationMask(At<uint8_t>(rule, rul::rul0::kRotation));
    char* const cellsEnd = At<char*>(rule, prule2::kCellsEnd);
    for (char* patternCell = At<char*>(rule, rul::rul0::kCells); patternCell != cellsEnd; patternCell += rul::cellrec::kSize) {
        // The check type of the cell's character: key % buckets (unsigned,
        // the count a signed shift), the chain by the key byte (a byte compare).
        const uint8_t key = At<uint8_t>(patternCell, rul::cellrec::kKey);
        char* const bucketsBegin = At<char*>(rule, prule2::kBuckets);
        const uint32_t bucketCount = static_cast<uint32_t>(
            static_cast<int32_t>(At<char*>(rule, prule2::kBucketsEnd) - bucketsBegin) >> 2);
        char* node = reinterpret_cast<char**>(bucketsBegin)[static_cast<uint32_t>(key) % bucketCount];
        while (node && At<uint8_t>(node, pnode::kKey) != key)
            node = At<char*>(node, rul::table::kNext);
        // A null node faults here, as in the original (+0x1E).
        if (At<uint8_t>(node, pnode::kOptional))
            continue;
        const uint32_t cellKey =
            ((At<uint32_t>(patternCell, rul::cellrec::kZ) + z) << 16) + (At<uint32_t>(patternCell, rul::cellrec::kX) + x);
        char* const info = occupants::Entry(tool, cellKey);
        if (!info)
            return 2;
        if (At<uint8_t>(node, pnode::kChecked)) {
            // 0x00609AA1: `flagged` is the occupant's flag 0x100000; `onward`
            // whether the origin test is made at all.
            uint8_t flagged = 0;
            bool onward;
            void* const occupant = At<void*>(info, cell::kOccupant);
            if (occupant && Virtual<uint8_t>(occupant, nslot::kHasFlag, cell::kOccupantFlag100000)) {
                flagged = 1;
                onward = true;
            } else {
                onward = At<uint8_t>(info, cell::kHasFlag100000) != 0;
            }
            if (onward) {
                if (!At<uint8_t>(rule, prule2::kNeedsOrigin))
                    return 1;
                if (flagged) {
                    uint32_t originX = 0, originZ = 0;
                    Virtual<void>(At<void*>(info, cell::kOccupant), nslot::kGetComplexPieceOrigin, &originX,
                                  &originZ);
                    if (originX != At<uint32_t>(rule, prule2::kOriginX) + x)
                        return 1;
                    if (originZ != At<uint32_t>(rule, prule2::kOriginZ) + z)
                        return 1;
                }
            }
            if (At<uint8_t>(info, cell::kImmovable) && !At<uint8_t>(info, cell::kHasFlag100000))
                return 1;
        }
        // The check type's two sides.
        uint32_t unionFlags = 0;
        uint32_t oneWayWord = 0;
        for (uint32_t side = 0; side < 2; ++side) {
            const int8_t type = static_cast<int8_t>(At<uint8_t>(node, pnode::kTypes + side));
            if (type < 0)
                continue;
            char* const pair = node + pnode::kPairs + side * pnode::kPairSize;
            if (type == kOneWayType)
                oneWayWord = At<uint32_t>(pair, 0);
            const uint32_t typeFlag = RuleTypeFlag(type);
            if (!(typeFlag & At<uint32_t>(info, cell::kFlags)))
                return 1;
            unionFlags |= typeFlag;
            const uint32_t difference = At<uint32_t>(info, cell::kByType + static_cast<std::size_t>(type) * 4) ^
                                        At<uint32_t>(pair, 0);
            if (At<uint32_t>(pair, 4) & difference)
                return 2;
        }
        if (!At<uint8_t>(node, pnode::kMultiOk)) {
            if (At<uint32_t>(info, cell::kFlags) & ~unionFlags & cell::kOccupantFlagMask)
                return 1;
        }
        if (unionFlags & 0x400) {
            if (!At<uint8_t>(node, pnode::kChecked)) {
                // Each byte of the pattern's one-way word with a connection (low
                // three bits non-zero) becomes 4 in that byte, summed into the top
                // byte by the multiply: the one-way sides the pattern asks about.
                uint32_t wanted = ((oneWayWord & 0x03030303u) + 0x03030303u) & 0x04040404u;
                wanted *= 0xC30C30u;
                wanted >>= 24;
                const uint8_t differing = static_cast<uint8_t>(At<uint8_t>(info, cell::kOneWay) ^ rotationMask);
                if (differing & static_cast<uint8_t>(wanted))
                    return 2;
            }
        }
    }

    // No point to test: it fits.
    if (At<char*>(rule, prule2::kPoints) == At<char*>(rule, prule2::kPointsEnd))
        return 0;

    // The points (moved to (x, z)) must be dry and free of network occupants.
    ListNode* list = Virtual<ListNode*>(ListNodeAllocator(), 0xC, static_cast<uint32_t>(sizeof(ListNode)));
    list->next = list;
    list->previous = list;
    ListNode* const head = list;
    void* const manager = Global<void*>(addr::SC4_NetworkService);
    void* filter = nullptr;
    if (char* const memory = CallCdecl<char*, uint32_t>(addr::SC4_operator_new, 0x10u)) {
        filter = CallThis<void*, uint32_t>(addr::cSC4NetworkManager_NetworkFilterConstruct, memory, kNetworkMask);
        if (filter)
            AddRefOf(filter);
    }
    Vector points = {nullptr, nullptr, nullptr};
    void* const terrain = Global<void*>(addr::SC4_Terrain);
    // 0x00609F56: the list cleared and its head given back.
    const auto cleanUp = [&]() {
        CallThis<void>(addr::SC4List_Clear2, &list);
        Virtual<void>(ListNodeAllocator(), 0x10, static_cast<void*>(head));
    };
    char* const pointsEnd = At<char*>(rule, prule2::kPointsEnd);
    for (char* point = At<char*>(rule, prule2::kPoints); point != pointsEnd; point += kPointSize) {
        const uint32_t px = At<uint32_t>(point, 0) + x;
        const uint32_t pz = At<uint32_t>(point, 4) + z;
        if (Virtual<uint8_t>(terrain, qslot::kUnderWater, px, pz)) {
            if (points.begin)
                Free(points.begin);
            if (filter)
                ReleaseOf(filter);
            cleanUp();
            return 1;
        }
        void* found = nullptr;
        if (Virtual<uint8_t>(manager, qslot::kFirstByCell, &found, px, pz, filter)) {
            ReleaseOf(found);
            if (points.begin)
                Free(points.begin);
            if (filter)
                ReleaseOf(filter);
            cleanUp();
            return 1;
        }
        PushPoint(points, px, pz);
    }

    // The set the collector fills (a head node of its own), the region it
    // makes and the cost it gives.
    TreeSet set;
    set.count = 0;
    set.head = CallCdecl<char*, uint32_t>(addr::SC4_stl_allocate, 0x14u);
    At<uint8_t>(set.head, 0) = 0;
    At<uint32_t>(set.head, tree::kParent) = 0;
    At<char*>(set.head, tree::kLeft) = set.head;
    At<char*>(set.head, tree::kRight) = set.head;
    char* region = nullptr;
    uint32_t cost[2] = {0, 0};
    const uint8_t collected = CallCdecl<uint8_t, Vector*, char**, uint32_t*, TreeSet*, void*>(
        addr::cSC4NetworkTool_CollectHighlights, &points, &region, cost, &set, At<void*>(tool, field::kFilter));
    if (region) {
        // The region's cells part (+0x10): the vtable, the rows freed one by
        // one, the row pointers and the region itself (the destructor, inlined).
        char* const cells = region + kRegionCells;
        At<std::uintptr_t>(cells, 0) = life::kPreviewListVtable;
        if (At<char*>(cells, 0x10)) {
            for (uint32_t i = 0; i < At<uint32_t>(cells, 4); ++i)
                CallCdecl<void, void*>(addr::SC4_operator_delete, At<char**>(cells, 0x10)[i]);
            CallCdecl<void, void*>(addr::SC4_operator_delete, At<char*>(cells, 0x10));
        }
        CallCdecl<void, void*>(addr::SC4_operator_delete, region);
    }
    const uint32_t answer = collected == 0 ? 1u : 0u;
    if (set.count != 0) {
        CallThis<void, char*>(addr::SC4RefSet_EraseSubtree, &set, At<char*>(set.head, tree::kParent));
        At<char*>(set.head, tree::kLeft) = set.head;
        At<uint32_t>(set.head, tree::kParent) = 0;
        At<char*>(set.head, tree::kRight) = set.head;
        set.count = 0;
    }
    if (set.head)
        Free(set.head);
    if (points.begin)
        Free(points.begin);
    if (filter)
        ReleaseOf(filter);
    cleanUp();
    return answer;
}

// ====================================================================================
// Slot 49 (0x0062D450): QueryUserForPotentialIntersections
// ====================================================================================

// The cell's position in metres: (cell + 0.5) * 16 + the record's float, as a
// float (the integer exact in a double; the sum rounded once, to float).
inline float CellMetres(int32_t cell, float record)
{
    const double half = Global<float>(addr::SC4_FloatHalf);
    const double sixteen = Global<float>(addr::SC4_FloatSixteen);
    return static_cast<float>((static_cast<double>(cell) + half) * sixteen + static_cast<double>(record));
}

// The effect's transform for a RUL0 rule's first record at a cell: the matrix
// of the record's turn and flip (about the vertical), the position, scale 1.
// 0x0062D650 to 0x0062D90A.
void BuildTransform(Transform& transform, void* terrain, int32_t x, int32_t z, char* record)
{
    const float zero = Global<float>(addr::SC4_FloatZero);
    const float xFloat = static_cast<float>(x);
    const float zFloat = static_cast<float>(z);
    const float px = CellMetres(x, At<float>(record, rul::record::kX));
    const float pz = CellMetres(z, At<float>(record, rul::record::kZ));
    // The terrain's altitude (slot 0x34: the cell as two floats; ST0).
    const float py = static_cast<float>(Virtual<double>(terrain, qslot::kAltitude, xFloat, zFloat));
    transform.count = 0;
    transform.parts = 0;
    transform.padding[0] = transform.padding[1] = 0;
    for (float& m : transform.matrix)
        m = 0.0f;
    transform.matrix[0] = transform.matrix[4] = transform.matrix[8] = 1.0f;
    transform.scale = 1.0f;
    transform.position[0] = px;
    transform.position[1] = py;
    transform.position[2] = pz;
    transform.parts = static_cast<uint8_t>(transform.parts | 1);
    transform.count = static_cast<uint8_t>(transform.count + 1);

    // The rotation: a = turn * -pi/2; C = float(cos a); S = sin a (a double);
    // the zeros keep their signs (0.0 * S and C * 0.0).
    const uint8_t turn = At<uint8_t>(record, rul::record::kTurn);
    const uint8_t flip = At<uint8_t>(record, rul::record::kFlip);
    const double angle = static_cast<double>(turn) * static_cast<double>(Global<float>(addr::cSC4NetworkTool_QuarterTurn));
    float cosine = 0.0f, sineF = 0.0f;
    X87CosSin(angle, &cosine, &sineF);
    // 0.0 * sin: only the sign of sin matters (a float keeps it).
    const double sineZero = static_cast<double>(zero) * static_cast<double>(sineF);
    const float sineZeroF = static_cast<float>(sineZero);
    const double cosineZero = static_cast<double>(cosine) * static_cast<double>(zero);
    const float cosineZeroF = static_cast<float>(cosineZero);
    const float d1 = static_cast<float>(static_cast<double>(cosine) - sineZero);
    const float p = static_cast<float>(static_cast<double>(cosineZeroF) - static_cast<double>(sineZeroF));
    const float q = static_cast<float>(static_cast<double>(cosineZeroF) - static_cast<double>(sineF));
    const float r = static_cast<float>(static_cast<double>(sineF) + static_cast<double>(cosineZeroF));
    const float u = static_cast<float>(static_cast<double>(sineZeroF) + static_cast<double>(cosineZeroF));
    const float v = static_cast<float>(static_cast<double>(sineZeroF) + static_cast<double>(cosine));
    transform.matrix[0] = flip ? -d1 : d1;
    transform.matrix[1] = 0.0f;
    transform.matrix[2] = flip ? -r : r;
    transform.matrix[3] = p;
    transform.matrix[4] = 1.0f;
    transform.matrix[5] = u;
    transform.matrix[6] = q;
    transform.matrix[7] = 0.0f;
    transform.matrix[8] = v;
    transform.parts = static_cast<uint8_t>(transform.parts | 4);
    transform.count = static_cast<uint8_t>(transform.count + 1);
}

// 0x0062D58F to 0x0062DAF2: the first rule of the walk that fits.
void Propose(char* self, char* tool, char* rule, int32_t x, int32_t z)
{
    const uint32_t ruleId = At<uint32_t>(rule, rul::rul0::kId);
    // The guarded puzzle placement, tried (last argument 0).
    if (!CallThis<uint8_t, uint32_t, uint32_t, uint32_t, uint32_t>(addr::cSC4NetworkTool_PlacePuzzlePiece, tool,
                                                                    static_cast<uint32_t>(x), static_cast<uint32_t>(z),
                                                                    ruleId, 0u))
        return;
    const uint32_t cost = Virtual<uint32_t>(tool, qslot::kCostOfSolution);
    void* const city = Global<void*>(addr::SC4_City);
    void* const budget = Virtual<void*>(city, qslot::kBudget);
    // The cash is 64 bits (EDX:EAX); the cost is the low dword only.
    if (Virtual<int64_t>(budget, qslot::kCash) < static_cast<int64_t>(cost))
        return;

    void* effect = nullptr;
    if (At<char*>(rule, prule2::kEffectName) != At<char*>(rule, prule2::kEffectNameEnd)) {
        Virtual<uint8_t>(Global<void*>(addr::SC4_Service43D1C), qslot::kMakeEffect,
                         At<const char*>(rule, prule2::kEffectName), &effect);
        if (effect) {
            Transform transform;
            BuildTransform(transform, At<void*>(self, field::kTerrain), x, z, At<char*>(rule, rul::rul0::kRecords));
            Virtual<void>(effect, qslot::kSetTransform, &transform);
            Virtual<void>(effect, qslot::kStart, 0u);
        }
    }

    // The question: an empty title and the localized text of the rule (else
    // the text that says it is missing).
    GameString title, text;
    InitGameString(title);
    InitGameString(text);
    if (!LoadLocalizedText(&text, kQuestionGroup, At<uint32_t>(rule, prule2::kTextId))) {
        GameString missing;
        uint8_t tag = 0;
        missing.begin = missing.end = missing.capacity = nullptr;
        CallThis<void, const char*, const char*, void*>(
            addr::SC4String_FromRange, &missing.begin, reinterpret_cast<const char*>(addr::cSC4NetworkTool_MissingIntersectionText),
            reinterpret_cast<const char*>(addr::cSC4NetworkTool_MissingIntersectionText + kMissingTextLength), &tag);
        missing.references = 0;
        CallThis<void, char*, char*, void*>(addr::SC4String_AssignRange, &text.begin, missing.begin, missing.end, &tag);
        missing.vtable = addr::SC4String_Vtable;
        if (missing.begin)
            Free(missing.begin);
    }
    if (CallCdecl<uint8_t, GameString*, GameString*, uint32_t, uint32_t>(addr::SC4UI_QuestionDialog, &title, &text, 0u,
                                                                         0u)) {
        // Yes: the placement for real (its answer is not read), the budget
        // charged and the sound played.
        CallThis<uint8_t, uint32_t, uint32_t, uint32_t, uint32_t>(addr::cSC4NetworkTool_PlacePuzzlePiece, tool,
                                                                   static_cast<uint32_t>(x), static_cast<uint32_t>(z),
                                                                   ruleId, 1u);
        Virtual<void>(budget, qslot::kSpend, cost, 0u);
        if (void* const sound = Global<void*>(addr::SC4_Sound43CB8)) {
            AddRefOf(sound);
            Virtual<void>(sound, qslot::kPlaySound, 1u, kSoundId, 0u, 0u, 0u);
            ReleaseOf(sound);
        }
    }
    if (effect)
        Virtual<void>(effect, qslot::kStop, 0u);
    DestroyGameString(text);
    DestroyGameString(title);
    if (effect)
        ReleaseOf(effect);
}

// Slot 49. See the top of this file.
void __fastcall QueryUserForPotentialIntersections(char* self)
{
    static bool logged = false;
    FirstCall("QueryUserForPotentialIntersections", logged);
    char* element = At<char*>(self, place::pfield::kOverrides);
    char* const overridesEnd = At<char*>(self, place::pfield::kOverrides + 4);
    CallThis<void>(addr::cSC4NetworkWorldCache_Reset, self + field::kWorldCache);
    if (element == overridesEnd)
        return;
    char* const store = reinterpret_cast<char*>(addr::cSC4NetworkTool_Rul0Store);
    do {
        // The store's first node, then the walk.
        char* node = nullptr;
        {
            char** const begin = At<char**>(store, rul::table::kBuckets);
            const uint32_t buckets = static_cast<uint32_t>(
                static_cast<int32_t>(reinterpret_cast<char*>(At<char**>(store, rul::table::kBucketsEnd)) -
                                     reinterpret_cast<char*>(begin)) >>
                2);
            for (uint32_t b = 0; b < buckets; ++b)
                if (begin[b]) {
                    node = begin[b];
                    break;
                }
        }
        // The tool of this element: a new highway tool, referenced and initialised.
        char* const tool =
            Virtual<char*>(CallCdecl<void*>(addr::SC4_GetNetworkManager), qslot::kGetNetworkTool, kRulesToolType, 1u);
        if (tool)
            AddRefOf(tool);
        Virtual<void>(tool, qslot::kToolInit);
        const int32_t x = At<int32_t>(element, 0);
        const int32_t z = At<int32_t>(element, 4);
        for (; node; node = place::HashNext(store, node)) {
            char* const rule = node + rul::store::kRule;
            if (At<uint32_t>(rule, prule2::kTextId) == 0)
                continue;
            if (CallThis<uint32_t, uint32_t, uint32_t, char*>(addr::SC4Rul0Rule_MatchAt, rule, static_cast<uint32_t>(x),
                                                              static_cast<uint32_t>(z), tool) != 0)
                continue;
            // The first rule that fits is the one; every way out leaves the walk.
            Propose(self, tool, rule, x, z);
            break;
        }
        Virtual<void>(tool, qslot::kToolShutdown);
        ReleaseOf(tool);
        element += 0xC;
    } while (element != overridesEnd);
}

// ====================================================================================
// Installation
// ====================================================================================

#define OPENSC4_DETOUR(address, fn) OPENSC4_NETTOOL_DETOUR(address, fn)

const Detour kPuzzle[] = {
    OPENSC4_DETOUR(addr::SC4Rul0Store_Find, FindRule),
    OPENSC4_DETOUR(addr::SC4Rul0Rule_MatchAt, MatchAt),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_QueryUserForPotentialIntersections, QueryUserForPotentialIntersections),
};

#undef OPENSC4_DETOUR

bool InstallPuzzle(registry::Mode) { return InstallDetours(kPuzzle, "puzzle"); }

const bool g_registered[] = {
    registry::Register({"simulators.nettool.puzzle", "simulators/nettool", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkTool_QueryUserForPotentialIntersections,
                        "Our network tool's RUL0 intersection question: slot 49 QueryUserForPotentialIntersections, "
                        "the rule against a cell (MatchAt 0x00609A00) and the RUL0 store's lookup 0x00625380 (3 "
                        "detours; src/core/nettool_puzzle.cpp)",
                        false, InstallPuzzle}),
};

} // namespace
} // namespace opensc4::nettool_sim
