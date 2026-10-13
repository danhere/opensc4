// The network manager's part of the autotest (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/netmgr_sim.h"
#include "core/occmgr_sim.h"

#include <cIGZIStream.h>
#include <cIGZOStream.h>
#include <cIGZString.h>
#include <cIGZUnknown.h>
#include <cISC4City.h>

#include <opensc4/addresses.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace opensc4::autotest {
namespace {

using netmgr_sim::kTypes;
using netmgr_sim::ListNode;
using netmgr_sim::ReleaseOf;
using netmgr_sim::Vector;
namespace field = netmgr_sim::field;
namespace slot = netmgr_sim::slot;
namespace remap = netmgr_sim::remap;
namespace edge = netmgr_sim::edge;
namespace filter = netmgr_sim::filter;
namespace piece_slot = netmgr_sim::piece_slot;

// The occupant type of every network piece (src/core/netocc_sim.h), the
// city's slots for the network manager and the occupant manager, an
// occupant's GetType and an IID nobody answers.
constexpr uint32_t kNetworkOccupantType = 0xC772BF98;
constexpr std::size_t kCityNetworkManager = 0x98, kCityOccupantManager = 0x88, kOccupantGetType = 0x1C;
constexpr uint32_t kIidNobody = 0x0F10A0CC;
constexpr uint32_t kIidFilter = 0x46A80026;

struct Hash {
    uint32_t h = 2166136261u;
    void Mix(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    }
};

// The city's network manager as the game reaches it (the city's slot 38),
// once it is initialised; null otherwise.
char* Manager(cISC4City* city)
{
    if (!game::IsBuild641() || !city)
        return nullptr;
    char* const manager = Virtual<char*>(city, kCityNetworkManager);
    if (!manager || !At<uint8_t>(manager, field::kInitialized))
        return nullptr;
    return manager;
}

uint32_t Count(char* manager, std::size_t vector, std::size_t size)
{
    const Vector* const v = reinterpret_cast<const Vector*>(manager + vector);
    return static_cast<uint32_t>(static_cast<std::size_t>(v->end - v->begin) / size);
}

// One QueryInterface through a part: the answer, and where in the object the
// answer lies (never the address); the reference it gave is released.
void AskInterface(Hash& hash, char* self, char* part, uint32_t iid)
{
    void* found = nullptr;
    const bool answered = Virtual<uint8_t>(part, 0x0, iid, &found) != 0;
    hash.Mix(answered);
    if (answered && found) {
        hash.Mix(static_cast<uint32_t>(static_cast<char*>(found) - self));
        ReleaseOf(found);
    }
}

// The identity: the class ID of the serializable part and QueryInterface
// through the three parts (the two others' slot 0 are the thunks) for the
// IDs the class answers and some it does not (one above 0xA9764C8C, two
// below). Each answer's reference is released through the part it came
// from, so Release runs its counting branch.
uint32_t IdentityHash(char* manager)
{
    Hash hash;
    hash.Mix(Virtual<uint32_t>(manager + netmgr_sim::kSerializablePart, slot::kGetGZCLSID));
    const uint32_t ids[] = {netmgr_sim::kIidUnknown,
                            netmgr_sim::kIidManager,
                            netmgr_sim::kIidMessageTarget,
                            netmgr_sim::kIidSerializable,
                            netmgr_sim::kIidNetworkOccupant,
                            kIidNobody,
                            0xE4FDA3D5u};
    for (const std::size_t part : {std::size_t{0}, netmgr_sim::kMessagePart, netmgr_sim::kSerializablePart})
        for (const uint32_t iid : ids)
            AskInterface(hash, manager, manager + part, iid);
    return hash.h;
}

// The thirteen English names (slot 22), their characters.
uint32_t NamesHash(char* manager)
{
    Hash hash;
    for (uint32_t type = 0; type < kTypes; ++type) {
        const char* const name = Virtual<const char*>(manager, slot::kGetEnglishNetworkName, type);
        hash.Mix(name ? BytesChecksum(name, strnlen(name, 64)) : 0u);
    }
    return hash.h;
}

// GetNetworkTool in its form that makes nothing (the second argument 0): a
// bit per type that has a tool; bit 13 and up for types that must have none
// (13, 14, 0xFFFFFFFF).
uint32_t ToolBits(char* manager)
{
    uint32_t bits = 0;
    for (uint32_t type = 0; type < kTypes; ++type)
        if (Virtual<void*>(manager, slot::kGetNetworkTool, type, 0u))
            bits |= 1u << type;
    uint32_t bit = kTypes;
    for (const uint32_t type : {kTypes, kTypes + 1, 0xFFFFFFFFu})
        if (Virtual<void*>(manager, slot::kGetNetworkTool, type, 0u))
            bits |= 1u << bit++;
    return bits;
}

// The three remap vectors and the shadow edge table as they are: every
// entry's words and its two bytes (not the two bytes after them, which
// nothing sets), every path entry's four words, and the table's nodes in the
// order of the buckets and their chains (key, type, base, mask, rotation).
uint32_t TablesHash(char* manager)
{
    Hash hash;
    for (const std::size_t vector : {field::kPreviewRemap, field::kShadowRemap}) {
        const Vector* const v = reinterpret_cast<const Vector*>(manager + vector);
        for (char* entry = v->begin; entry != v->end; entry += remap::kSize) {
            for (std::size_t offset = 0; offset < remap::kRotation; offset += 4)
                hash.Mix(At<uint32_t>(entry, offset));
            hash.Mix(At<uint8_t>(entry, remap::kRotation));
            hash.Mix(At<uint8_t>(entry, remap::kGoOn));
        }
    }
    const Vector* const paths = reinterpret_cast<const Vector*>(manager + field::kPathRemap);
    for (char* entry = paths->begin; entry != paths->end; entry += remap::kPathSize)
        for (std::size_t offset = 0; offset < remap::kPathSize; offset += 4)
            hash.Mix(At<uint32_t>(entry, offset));
    char* const table = manager + field::kShadowEdges;
    for (char* bucket = At<char*>(table, edge::kBuckets); bucket != At<char*>(table, edge::kBucketsEnd); bucket += 4)
        for (char* node = At<char*>(bucket, 0); node; node = At<char*>(node, edge::kNext)) {
            hash.Mix(At<uint32_t>(node, edge::kKey));
            hash.Mix(At<uint32_t>(node, edge::kType));
            hash.Mix(At<uint32_t>(node, edge::kBase));
            hash.Mix(At<uint32_t>(node, edge::kMask));
            hash.Mix(At<uint8_t>(node, edge::kRotation));
        }
    return hash.h;
}

// Three IDs for a remap entry: its match word (the plainest ID it takes),
// the match word with every bit the mask does not look at set, and the match
// word with the lowest bit of the mask turned (an ID the entry refuses).
void EntryIds(char* entry, uint32_t ids[3])
{
    const uint32_t match = At<uint32_t>(entry, remap::kMatch), mask = At<uint32_t>(entry, remap::kMask);
    ids[0] = match;
    ids[1] = match | ~mask;
    ids[2] = match ^ (mask & (0u - mask));
}

// IDs no table is expected to hold.
constexpr uint32_t kOtherIds[] = {0u, 0xFFFFFFFFu, 0x12345678u, 0x00000300u, 0x0C001000u};
constexpr uint8_t kRotations[] = {0, 1, 0x82, 3};
constexpr std::size_t kMaxEntries = 256;

// The three lookups (slots 13, 14 and 15) over the IDs the tables hold and
// a few they do not; each is a function of the tables and its arguments
// only and writes nothing but the caller's rotation byte.
//   - MapPreviewTexture: for every entry of the vector at +0x44 (the first
//     256) its three IDs, and the other IDs, each with four rotations going
//     in; the ID and the rotation coming out.
//   - MapShadowTexture: the same over the vector at +0x50, with two
//     rotations, the flags of highway, light rail and monorail (the three
//     the function keeps), road (kept as 0) and all three at once (which no
//     table node has), and up to eight edge keys of the table at +0x30 (the
//     first of the walk over its buckets) and two it is not expected to
//     hold. Not asked while the table has no buckets (the game's function
//     divides by their number).
//   - MapPath: the three IDs of every entry of the vector at +0x5C and the
//     other IDs (with the byte at +0x11 clear every answer is the ID).
uint32_t MapsHash(char* manager)
{
    Hash hash;
    const auto preview = [&](uint32_t id) {
        for (const uint8_t start : kRotations) {
            uint8_t rotation = start;
            hash.Mix(Virtual<uint32_t>(manager, slot::kMapPreviewTexture, id, &rotation));
            hash.Mix(rotation);
        }
    };
    const Vector* const previews = reinterpret_cast<const Vector*>(manager + field::kPreviewRemap);
    std::size_t taken = 0;
    for (char* entry = previews->begin; entry != previews->end && taken < kMaxEntries; entry += remap::kSize, ++taken) {
        uint32_t ids[3];
        EntryIds(entry, ids);
        for (const uint32_t id : ids)
            preview(id);
    }
    for (const uint32_t id : kOtherIds)
        preview(id);

    char* const table = manager + field::kShadowEdges;
    std::vector<uint32_t> keys;
    for (char* bucket = At<char*>(table, edge::kBuckets); bucket != At<char*>(table, edge::kBucketsEnd); bucket += 4)
        for (char* node = At<char*>(bucket, 0); node && keys.size() < 8; node = At<char*>(node, edge::kNext)) {
            const uint32_t key = At<uint32_t>(node, edge::kKey);
            if (keys.empty() || keys.back() != key)
                keys.push_back(key);
        }
    keys.push_back(0u);
    keys.push_back(0x0BADF00Du);
    const bool buckets = At<char*>(table, edge::kBuckets) != At<char*>(table, edge::kBucketsEnd);
    const auto shadow = [&](uint32_t id) {
        if (!buckets)
            return;
        for (const uint32_t flag : {1u << 2, 1u << 8, 1u << 9, 1u << 0, netmgr_sim::kShadowFlagMask})
            for (const uint32_t key : keys)
                for (const uint8_t start : {kRotations[0], kRotations[2]}) {
                    uint8_t rotation = start;
                    hash.Mix(Virtual<uint32_t>(manager, slot::kMapShadowTexture, id, &rotation, flag, key));
                    hash.Mix(rotation);
                }
    };
    const Vector* const shadows = reinterpret_cast<const Vector*>(manager + field::kShadowRemap);
    taken = 0;
    for (char* entry = shadows->begin; entry != shadows->end && taken < kMaxEntries; entry += remap::kSize, ++taken) {
        uint32_t ids[3];
        EntryIds(entry, ids);
        for (const uint32_t id : ids)
            shadow(id);
    }
    for (const uint32_t id : kOtherIds)
        shadow(id);

    const Vector* const paths = reinterpret_cast<const Vector*>(manager + field::kPathRemap);
    taken = 0;
    for (char* entry = paths->begin; entry != paths->end && taken < kMaxEntries; entry += remap::kPathSize, ++taken) {
        uint32_t ids[3];
        EntryIds(entry, ids);
        for (const uint32_t id : ids)
            hash.Mix(Virtual<uint32_t>(manager, slot::kMapPath, id));
    }
    for (const uint32_t id : kOtherIds)
        hash.Mix(Virtual<uint32_t>(manager, slot::kMapPath, id));
    return hash.h;
}

// The length of the retrofit list (the nodes between the head node and
// itself; at most 100,000 are counted).
uint32_t RetrofitLength(char* manager)
{
    const ListNode* const head = At<ListNode*>(manager, field::kRetrofit);
    uint32_t length = 0;
    if (head)
        for (const ListNode* node = head->next; node != head && length < 100000; node = node->next)
            ++length;
    return length;
}

// The vector at +0x24, the pieces the save record holds, in the order they
// are stored (at most 100,000): each piece's cell (slot 59) and the class ID
// of its serializable part (asked through QueryInterface; the reference is
// released), as a checksum that depends on the order and as a sum that does
// not. No address and no reference count.
void SavedHash(char* manager, uint32_t& ordered, uint32_t& sum)
{
    Hash hash;
    sum = 0;
    const Vector* const managed = reinterpret_cast<const Vector*>(manager + field::kManaged);
    uint32_t taken = 0;
    for (char* at = managed->begin; at != managed->end && taken < 100000; at += 4, ++taken) {
        char* const piece = At<char*>(at, 0);
        uint32_t x = 0, z = 0;
        Virtual<void>(piece, piece_slot::kGetOccupiedCell, &x, &z);
        uint32_t classId = 0;
        void* part = nullptr;
        if (Virtual<uint8_t>(piece, 0x0, netmgr_sim::kIidSerializable, &part) && part) {
            classId = Virtual<uint32_t>(part, slot::kGetGZCLSID);
            ReleaseOf(part);
        }
        Hash one;
        one.Mix(x);
        one.Mix(z);
        one.Mix(classId);
        hash.Mix(one.h);
        sum += one.h;
    }
    ordered = hash.h;
}

// A network piece of the city for the walks below: the interface part, its
// cell (slot 59), whether it has the flag 0x100000 (a part of an
// intersection of several cells) and whether it has a complex piece origin
// (slot 71 says so).
struct Piece {
    char* self;
    uint32_t x, z;
    bool flagged, origin;
};

// Every occupant of the city once (the occupant manager's grid, as
// src/core/subwayocc_autotest.cpp walks it): the network pieces (occupant
// type 0xC772BF98) into `pieces`, and with `all` every occupant part into
// it. No order is promised: everything made of them below is a count, a sum
// or an exclusive or. False without an occupant manager.
bool Collect(cISC4City* city, std::vector<Piece>& pieces, std::vector<void*>* all)
{
    char* const manager = Virtual<char*>(city, kCityOccupantManager);
    if (!manager || !At<uint8_t>(manager, occmgr_sim::field::kInitialized))
        return false;
    std::unordered_set<void*> seen;
    Virtual<void>(manager + occmgr_sim::field::kLock, 0x4);
    const occmgr_sim::Vector* const grid = occmgr_sim::Grid(manager);
    const int32_t columns = static_cast<int32_t>((grid->end - grid->begin) / 12);
    for (int32_t x = 0; x < columns; ++x) {
        const auto* const column = reinterpret_cast<const occmgr_sim::Vector*>(grid->begin + x * 12);
        const int32_t depth = static_cast<int32_t>((column->end - column->begin) >> 2);
        for (int32_t z = 0; z < depth; ++z) {
            const occmgr_sim::Cell* const cell = occmgr_sim::CellAt(manager, x, z);
            if (!cell)
                continue;
            for (const occmgr_sim::ListNode* node = cell->head->next; node != cell->head; node = node->next) {
                void* const object = occmgr_sim::OccupantOf(node);
                if (!seen.insert(object).second)
                    continue;
                if (all)
                    all->push_back(object);
                if (Virtual<uint32_t>(object, kOccupantGetType) != kNetworkOccupantType)
                    continue;
                Piece found{static_cast<char*>(object) - 4, 0, 0, false, false};
                Virtual<void>(found.self, piece_slot::kGetOccupiedCell, &found.x, &found.z);
                found.flagged =
                    Virtual<uint8_t>(found.self, piece_slot::kHasNetworkFlag, netmgr_sim::kFlagIntersectionPart) != 0;
                int32_t originX = 0, originZ = 0;
                found.origin =
                    Virtual<uint8_t>(found.self, piece_slot::kGetComplexPieceOrigin, &originX, &originZ) != 0;
                pieces.push_back(found);
            }
        }
    }
    Virtual<void>(manager + occmgr_sim::field::kLock, 0x8);
    return true;
}

// GetAllIntersectionPieces (slot 12) for one piece into a list of the
// test's own (a head node from the list allocator, as the game's callers
// make theirs): the answer, the number of entries and the sum of a checksum
// of each entry's cell (the entry is an occupant part: it is asked for the
// network occupant interface and that for its cell). A second call with the
// list as it is must add nothing (the piece is in it). Then every entry is
// released (each holds a reference) and the list freed by the game's
// 0x00413FE0. Pieces on cells with an odd x + z hand the occupant manager
// and themselves as `other` in; the others null for both, which means the
// same.
uint32_t IntersectionHash(char* manager, char* occupants, const Piece& from, uint32_t& entries)
{
    Hash hash;
    ListNode* const head = Virtual<ListNode*>(ListNodeAllocator(), 0xC, 0xCu);
    head->next = head;
    head->previous = head;
    ListNode* list = head;
    const bool spelled = ((from.x + from.z) & 1) != 0;
    char* const nothing = nullptr;
    hash.Mix(Virtual<uint8_t>(manager, slot::kGetAllIntersectionPieces, from.self, &list, spelled ? occupants : nothing,
                              spelled ? from.self : nothing));
    uint32_t count = 0, sum = 0;
    for (ListNode* node = head->next; node != head; node = node->next) {
        ++count;
        void* member = nullptr;
        if (Virtual<uint8_t>(node->value, 0x0, netmgr_sim::kIidNetworkOccupant, &member) && member) {
            uint32_t x = 0, z = 0;
            Virtual<void>(member, piece_slot::kGetOccupiedCell, &x, &z);
            Hash cell;
            cell.Mix(x);
            cell.Mix(z);
            sum += cell.h;
            ReleaseOf(member);
        }
    }
    hash.Mix(count);
    hash.Mix(sum);
    hash.Mix(Virtual<uint8_t>(manager, slot::kGetAllIntersectionPieces, from.self, &list, nothing, nothing));
    uint32_t again = 0;
    for (ListNode* node = head->next; node != head; node = node->next)
        ++again;
    hash.Mix(again);
    for (ListNode* node = head->next; node != head; node = node->next)
        ReleaseOf(node->value);
    reinterpret_cast<void(__thiscall*)(void*)>(addr::SC4List_Destroy2)(&list);
    entries += count;
    return hash.h;
}

// What the fingerprint's walk over the network pieces counted.
struct Walked {
    uint32_t stations = 0, pipes = 0, connections = 0, asked = 0, skipped = 0, entries = 0, flagged = 0;
    uint32_t sum = 0, mixed = 0;
};

// The manager's queries about the city's network pieces, each through the
// manager's vtable, summed and exclusive-ored per piece (no order):
//   - IsOccupantSubwayStation (slot 9) of the piece's occupant part (a
//     network piece is no station: the count says how many said yes);
//   - FindPipeOccupant and FindPipeConnectionOccupant (slots 10 and 11) at
//     the piece's cell: whether one was found; the object each stores is
//     released;
//   - GetAllIntersectionPieces (IntersectionHash), when its answer cannot
//     depend on what the stack held: the walk asks a neighbour with the flag
//     0x100000 whether it is part of the same structure as the piece (slot
//     66, the game's 0x006198A0), and that compares the two pieces' complex
//     piece origins from stack words neither call writes for a piece
//     without one (docs/netocc.md). So it is asked for every piece when no
//     piece of the city has the flag (then slot 66 is never reached), and
//     otherwise only for pieces with an origin, and only when every flagged
//     piece of the city has one; the rest are counted as skipped.
// A piece whose cell is not inside the city (the city's slots 93 and 94) is
// asked nothing and counted as skipped.
void WalkPieces(cISC4City* city, char* manager, char* occupants, const std::vector<Piece>& pieces, Walked& walked)
{
    const uint32_t cellsX = Virtual<uint32_t>(city, netmgr_sim::city_slot::kCellCountX);
    const uint32_t cellsZ = Virtual<uint32_t>(city, netmgr_sim::city_slot::kCellCountZ);
    bool everyOrigin = true;
    for (const Piece& at : pieces)
        if (at.flagged) {
            ++walked.flagged;
            everyOrigin = everyOrigin && at.origin;
        }
    for (const Piece& at : pieces) {
        Hash hash;
        hash.Mix(at.x);
        hash.Mix(at.z);
        if (at.x >= cellsX || at.z >= cellsZ) {
            ++walked.skipped;
            walked.sum += hash.h;
            walked.mixed ^= hash.h;
            continue;
        }
        const bool station = Virtual<uint8_t>(manager, slot::kIsOccupantSubwayStation, at.self + 4) != 0;
        hash.Mix(station);
        walked.stations += station;
        for (const std::size_t lookup : {slot::kFindPipeOccupant, slot::kFindPipeConnectionOccupant}) {
            void* found = nullptr;
            const bool any = Virtual<uint8_t>(manager, lookup, at.x, at.z, &found) != 0;
            hash.Mix(any);
            hash.Mix(found != nullptr);
            (lookup == slot::kFindPipeOccupant ? walked.pipes : walked.connections) += any;
            if (found)
                ReleaseOf(found);
        }
        if (walked.flagged == 0 || (everyOrigin && at.origin)) {
            hash.Mix(IntersectionHash(manager, occupants, at, walked.entries));
            ++walked.asked;
        } else {
            ++walked.skipped;
        }
        walked.sum += hash.h;
        walked.mixed ^= hash.h;
    }
}

// A construction crew of the test's own. The manager calls a registered
// crew's slot 1 (Register) and slot 2 (Unregister) and nothing else; its
// shutdown (not this part's) calls slot 5 of the crews still registered,
// which these never are when it runs. The count is the test's own.
class StandInCrew : public cIGZUnknown {
public:
    bool QueryInterface(uint32_t, void**) override { return false; }
    uint32_t AddRef() override { return ++references; }
    uint32_t Release() override { return --references; }
    virtual uint32_t Slot3() { return 0; }
    virtual uint32_t Slot4() { return 0; }
    virtual uint32_t Slot5() { return 0; }

    uint32_t references = 0;
};

// RegisterConstructionCrew and UnregisterConstructionCrew (slots 16 and
// 17) with five stand-ins: registered one after the other (an empty vector
// grows at the first, the second, the third and the fifth: the game's
// InsertN; the fourth goes in place), then taken out: the second (the rest
// moves down), the last (nothing moves), the second again (not there:
// nothing happens), then the others. After each call: the number of crews
// more than there were, and each stand-in's reference count and place among
// the new ones. The vector keeps the room it grew to (the manager's
// destructor frees it).
uint32_t CrewTest(char* manager, uint32_t& there)
{
    Hash hash;
    const Vector* const crews = reinterpret_cast<const Vector*>(manager + field::kCrews);
    there = Count(manager, field::kCrews, 4);
    StandInCrew standIns[5];
    const auto pointer = [&](std::size_t i) { return static_cast<void*>(static_cast<cIGZUnknown*>(&standIns[i])); };
    const auto mixState = [&] {
        const uint32_t count = Count(manager, field::kCrews, 4);
        hash.Mix(count - there);
        for (std::size_t i = 0; i < 5; ++i) {
            hash.Mix(standIns[i].references);
            uint32_t place = 0xFFFFFFFFu;
            for (uint32_t at = there; at < count; ++at)
                if (reinterpret_cast<void**>(crews->begin)[at] == pointer(i))
                    place = at - there;
            hash.Mix(place);
        }
    };
    for (std::size_t i = 0; i < 5; ++i) {
        Virtual<uint32_t>(manager, slot::kRegisterConstructionCrew, pointer(i));
        mixState();
    }
    for (const std::size_t i : {1u, 4u, 1u, 0u, 2u, 3u}) {
        Virtual<uint32_t>(manager, slot::kUnregisterConstructionCrew, pointer(i));
        mixState();
    }
    return hash.h;
}

// One of the two filters (slots 18 and 19): made, referenced, its two
// vtables the class's, QueryInterface for the IDs a filter answers (1,
// 0x46A80026, the serializable part at +4) and one it does not, the class
// ID of the serializable part, the type test (slot 4: always yes), and the
// occupant test (slot 3, what the occupant manager calls for every occupant
// of a query that is given the filter) on every occupant of the city: how
// many it takes. The last Release frees the filter through its deleting
// destructor.
uint32_t FilterTest(char* manager, std::size_t getter, std::uintptr_t vtable, std::uintptr_t vtable4,
                    const std::vector<void*>& all, uint32_t& taken)
{
    Hash hash;
    char* const made = Virtual<char*>(manager, getter);
    hash.Mix(made != nullptr);
    if (!made)
        return hash.h;
    Virtual<uint32_t>(made, filter::kAddRef);
    hash.Mix(At<std::uintptr_t>(made, 0x0) == vtable);
    hash.Mix(At<std::uintptr_t>(made, 0x4) == vtable4);
    for (const uint32_t iid : {netmgr_sim::kIidUnknown, kIidFilter, netmgr_sim::kIidSerializable, kIidNobody})
        AskInterface(hash, made, made, iid);
    hash.Mix(Virtual<uint32_t>(made + 4, slot::kGetGZCLSID));
    for (const uint32_t type : {kNetworkOccupantType, 0xC7CC75F1u, 0u})
        hash.Mix(Virtual<uint8_t>(made, filter::kIsOccupantTypeIncluded, type));
    for (void* const occupant : all)
        taken += Virtual<uint8_t>(made, filter::kIsOccupantIncluded, occupant) != 0;
    hash.Mix(taken);
    Virtual<uint32_t>(made, filter::kRelease);
    return hash.h;
}

// GetNetworkTool in its making form (the second argument 1): a new tool of
// the type, which the manager does not keep. That it is made, that it is not
// the manager's own tool of the type and that its two vtables are that
// tool's; then AddRef and Release, which destroys it. For type 0 this is
// what the game's ProcessRetrofitList does with a tool of its own
// (0x00612847 to 0x00612DF1: operator new, the constructor 0x0062C5F0 with
// 0, slot 1, and at the end slot 2). The other twelve types only with
// `every` (OPENSC4_TEST_NETMGR_TOOLS=1): the game never makes and drops one
// of those without Init, so that it leaves no trace is not shown.
uint32_t ToolTest(char* manager, bool every, uint32_t& made)
{
    Hash hash;
    for (uint32_t type = 0; type < kTypes; ++type) {
        if (type != 0 && !every)
            continue;
        char* const own = Virtual<char*>(manager, slot::kGetNetworkTool, type, 0u);
        char* const fresh = Virtual<char*>(manager, slot::kGetNetworkTool, type, 1u);
        hash.Mix(type);
        hash.Mix(fresh != nullptr);
        if (!fresh)
            continue;
        hash.Mix(fresh != own);
        hash.Mix(own && At<std::uintptr_t>(fresh, 0x0) == At<std::uintptr_t>(own, 0x0));
        hash.Mix(own && At<std::uintptr_t>(fresh, 0x4) == At<std::uintptr_t>(own, 0x4));
        Virtual<uint32_t>(fresh, 0x4);
        Virtual<uint32_t>(fresh, 0x8);
        ++made;
    }
    return hash.h;
}

// An output stream that keeps the bytes written to it, as
// src/core/save_autotest.cpp's. It answers no interface, so the game's
// SC4Stream::WriteReference (0x005D99A0), which first asks the stream for the
// save file's segment interface 0x6999A8FB, writes nothing for a reference
// and does not touch the object. `error` is what GetError answers.
class ByteOStream final : public cIGZOStream {
public:
    bool QueryInterface(uint32_t, void**) override { return false; }
    uint32_t AddRef() override { return 1; }
    uint32_t Release() override { return 1; }
    void Flush() override {}
    bool SetSint8(int8_t v) override { return Put(v); }
    bool SetUint8(uint8_t v) override { return Put(v); }
    bool SetSint16(int16_t v) override { return Put(v); }
    bool SetUint16(uint16_t v) override { return Put(v); }
    bool SetSint32(int32_t v) override { return Put(v); }
    bool SetUint32(uint32_t v) override { return Put(v); }
    bool SetSint64(int64_t v) override { return Put(v); }
    bool SetUint64(uint64_t v) override { return Put(v); }
    bool SetFloat32(float v) override { return Put(v); }
    bool SetFloat64(double v) override { return Put(v); }
    bool SetRZCharStr(char const* text) override { return SetVoid(text, static_cast<uint32_t>(std::strlen(text))); }
    bool SetGZStr(cIGZString const& text) override
    {
        const uint32_t length = text.Strlen();
        Put(length);
        return SetVoid(text.ToChar(), length);
    }
    bool SetGZSerializable(cIGZSerializable const&) override { return false; }
    bool SetVoid(void const* data, uint32_t size) override
    {
        const uint8_t* const from = static_cast<const uint8_t*>(data);
        bytes.insert(bytes.end(), from, from + size);
        return true;
    }
    int32_t GetError() override { return error; }
    int32_t SetUserData(cIGZVariant*) override { return 0; }
    int32_t GetUserData() override { return 0; }

    std::vector<uint8_t> bytes;
    int32_t error = 0;

private:
    template <typename T>
    bool Put(const T& value)
    {
        return SetVoid(&value, sizeof value);
    }
};

// An input stream over a few bytes, as src/core/save_autotest.cpp's: a read
// past the end stores nothing, fails and leaves the stream in error. It
// answers no interface, so SC4Stream::ReadReference (0x005D9730) reads
// nothing from it and fails.
class ByteIStream final : public cIGZIStream {
public:
    explicit ByteIStream(std::vector<uint8_t> data) : bytes(std::move(data)) {}
    bool QueryInterface(uint32_t, void**) override { return false; }
    uint32_t AddRef() override { return 1; }
    uint32_t Release() override { return 1; }
    bool Skip(uint32_t count) override
    {
        if (bytes.size() - at < count)
            return Fail();
        at += count;
        return true;
    }
    bool GetSint8(int8_t& v) override { return Get(v); }
    bool GetUint8(uint8_t& v) override { return Get(v); }
    bool GetSint16(int16_t& v) override { return Get(v); }
    bool GetUint16(uint16_t& v) override { return Get(v); }
    bool GetSint32(int32_t& v) override { return Get(v); }
    bool GetUint32(uint32_t& v) override { return Get(v); }
    bool GetSint64(int64_t& v) override { return Get(v); }
    bool GetUint64(uint64_t& v) override { return Get(v); }
    bool GetFloat32(float& v) override { return GetVoid(&v, sizeof v); }
    bool GetFloat64(double& v) override { return GetVoid(&v, sizeof v); }
    bool GetRZCharStr(char*, uint32_t) override { return Fail(); }
    bool GetGZStr(cIGZString&) override { return Fail(); }
    bool GetGZSerializable(cIGZSerializable&) override { return Fail(); }
    bool GetVoid(void* out, uint32_t size) override
    {
        if (bytes.size() - at < size)
            return Fail();
        std::memcpy(out, bytes.data() + at, size);
        at += size;
        return true;
    }
    int32_t GetError() override { return error; }
    int32_t SetUserData(cIGZVariant*) override { return 0; }
    int32_t GetUserData() override { return 0; }

    std::vector<uint8_t> bytes;
    std::size_t at = 0;
    int32_t error = 0;

private:
    bool Fail()
    {
        error = 1;
        return false;
    }
    template <typename T>
    bool Get(T& value)
    {
        return GetVoid(&value, sizeof value);
    }
};

// Write (the serializable part's slot 3) of a manager into a stream of the
// test's own, once into a stream without an error and once into one that
// reports one: the answer (1, then 0) and the bytes. With such a stream the
// record is the version and the count only (six bytes): the game's
// WriteReference is called for every piece of the vector at +0x24 and writes
// nothing (ByteOStream). Nothing of the manager or the pieces is changed.
uint32_t WriteTest(char* object, uint32_t& length)
{
    Hash hash;
    for (const int32_t error : {0, 1}) {
        ByteOStream out;
        out.error = error;
        cIGZOStream* const stream = &out;
        hash.Mix(Virtual<uint8_t>(object + netmgr_sim::kSerializablePart, slot::kWrite, static_cast<void*>(stream)));
        hash.Mix(static_cast<uint32_t>(out.bytes.size()));
        hash.Mix(BytesChecksum(out.bytes.data(), out.bytes.size()));
        length = static_cast<uint32_t>(out.bytes.size());
    }
    return hash.h;
}

// Read (the serializable part's slot 4) on a manager of the test's own, with
// streams that end before a reference is stored:
//   - an empty stream (the version's slot then keeps the low word of the
//     stream's address, a multiple of four, so never 1), version 2 and
//     version 0: AL 0 after the one uint16;
//   - version 1 and nothing else: the count's read fails and the stream is
//     in error: AL 0 (in the game by the way of its unset count, see
//     src/core/netmgr_life.cpp);
//   - version 1 and a count of 0: AL 1, six bytes taken;
//   - version 1 and a count of 2: the first reference cannot be read from a
//     stream that answers no interface: AL 0, six bytes taken;
//   - a stream that is in error before the call: AL 0, nothing taken.
// The answer, the bytes taken, the stream's error and the number of entries
// of the vector at +0x24 (0 throughout) are mixed.
void ReadRefusals(Hash& hash, char* object)
{
    const std::vector<uint8_t> streams[] = {
        {}, {2, 0, 0xEE, 0xEE}, {0, 0}, {1, 0}, {1, 0, 0, 0, 0, 0}, {1, 0, 2, 0, 0, 0}, {1, 0, 0, 0, 0, 0}};
    for (std::size_t i = 0; i < sizeof streams / sizeof streams[0]; ++i) {
        ByteIStream in(streams[i]);
        if (i == 6)
            in.error = 1;
        cIGZIStream* const stream = &in;
        hash.Mix(Virtual<uint8_t>(object + netmgr_sim::kSerializablePart, slot::kRead, static_cast<void*>(stream)));
        hash.Mix(static_cast<uint32_t>(in.at));
        hash.Mix(static_cast<uint32_t>(in.error));
        hash.Mix(Count(object, field::kManaged, 4));
    }
}

// A second manager, made by the class's factory (0x00613750, at its address)
// and destroyed again. Safe by the asm: the constructor subscribes to
// nothing and asks no service but the resource manager (LoadRemapping opens,
// reads and closes the ini record, as it does for every city), and the
// destructor of a manager that was never initialised frees its own memory
// only (its Shutdown returns at once). Init is NOT called on it (that would
// subscribe it and make thirteen tools). Mixed:
//   - as constructed: the four state bytes, the dirty rectangle, the sizes
//     of the vectors at +0x24, +0x68 and +0x7C, the retrofit list's length,
//     how many of the thirteen tool slots are null, the number of buckets
//     of the shadow edge table, whether its three vtable pointers are the
//     city's manager's, and whether the remap tables it loaded are the
//     city's manager's (TablesHash and the four sizes);
//   - the identity through its three parts (IdentityHash);
//   - Shutdown (slot 4) on it: AL 1 and the initialised byte still 0;
//   - Write (WriteTest) and Read (ReadRefusals);
//   - the last Release's answer (0: the deleting destructor ran).
uint32_t SecondObjectTest(char* manager, bool& made)
{
    Hash hash;
    char* const second = reinterpret_cast<char*(__cdecl*)()>(addr::cSC4NetworkManager_Create)();
    made = second != nullptr;
    hash.Mix(made);
    if (!second)
        return hash.h;
    Virtual<uint32_t>(second, slot::kAddRef);
    for (const std::size_t byte : {field::kInitialized, field::kLeftHand, field::kBatching, field::kQueueing})
        hash.Mix(At<uint8_t>(second, byte));
    for (std::size_t i = 0; i < 4; ++i)
        hash.Mix(At<uint32_t>(second, field::kDirtyRect + 4 * i));
    hash.Mix(Count(second, field::kManaged, 4));
    hash.Mix(Count(second, field::kQueue, 4));
    hash.Mix(Count(second, field::kCrews, 4));
    hash.Mix(RetrofitLength(second));
    uint32_t noTools = 0;
    for (uint32_t type = 0; type < kTypes; ++type)
        noTools += At<void*>(second, field::kTools + 4 * type) == nullptr;
    hash.Mix(noTools);
    hash.Mix(netmgr_sim::BucketCount(second + field::kShadowEdges));
    for (const std::size_t part : {std::size_t{0}, netmgr_sim::kMessagePart, netmgr_sim::kSerializablePart})
        hash.Mix(At<std::uintptr_t>(second, part) == At<std::uintptr_t>(manager, part));
    hash.Mix(TablesHash(second) == TablesHash(manager));
    hash.Mix(Count(second, field::kPreviewRemap, remap::kSize) == Count(manager, field::kPreviewRemap, remap::kSize));
    hash.Mix(Count(second, field::kShadowRemap, remap::kSize) == Count(manager, field::kShadowRemap, remap::kSize));
    hash.Mix(Count(second, field::kPathRemap, remap::kPathSize) == Count(manager, field::kPathRemap, remap::kPathSize));
    hash.Mix(At<uint32_t>(second, field::kShadowEdges + edge::kCount) ==
             At<uint32_t>(manager, field::kShadowEdges + edge::kCount));
    hash.Mix(IdentityHash(second));
    hash.Mix(Virtual<uint8_t>(second, slot::kShutdown));
    hash.Mix(At<uint8_t>(second, field::kInitialized));
    uint32_t length = 0;
    hash.Mix(WriteTest(second, length));
    ReadRefusals(hash, second);
    hash.Mix(Virtual<uint32_t>(second, slot::kRelease));
    return hash.h;
}

// The drive's results (OPENSC4_TEST_NETMGR=<day>): the first part's and the
// second part's (life and save).
uint32_t g_setSum = 0;
uint32_t g_lifeSum = 0;
bool g_setDone = false;

// OPENSC4_TEST_NETMGR=<day>: on that day the calls that make or change
// something and put it back: the construction crews (CrewTest), the two
// filters (FilterTest) and the subway station test (slot 9) over every
// occupant of the city, and GetNetworkTool's making form (ToolTest). The
// checksum of all of it is in the fingerprint's ` set` field from then on,
// and one log line says what ran, also without a manager. Not called:
// AddToRetrofitList (nothing takes an entry off the list before the next
// retrofit pass).
//
// Then the second part's calls, with a checksum and a log line of their own
// (the fingerprint's ` life` field, after ` set`): Init (slot 3) on the
// city's manager, which is initialised and so answers 1 and does nothing;
// Write of the city's manager into a stream of the test's own (WriteTest);
// and, unless OPENSC4_TEST_NETMGR_OBJECT=0, a second manager made, asked,
// written, read into and destroyed (SecondObjectTest). Not called: Shutdown
// on the city's manager, and Init on the second one.
void Drive(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_NETMGR") || !game::IsBuild641() || g_setDone)
        return;
    g_setDone = true;
    char* const manager = Manager(day.city);
    if (!manager) {
        log::Write("autotest: netmgr: nothing driven (no manager)");
        return;
    }
    std::vector<Piece> pieces;
    std::vector<void*> all;
    const bool collected = Collect(day.city, pieces, &all);
    Hash hash;
    uint32_t there = 0;
    const uint32_t crews = CrewTest(manager, there);
    hash.Mix(crews);
    uint32_t water = 0, subway = 0;
    const uint32_t waterSum =
        FilterTest(manager, slot::kGetWaterOccupantFilter, addr::cSC4NetworkManager_WaterFilter_vftable,
                   addr::cSC4NetworkManager_WaterFilter_vftable_4, all, water);
    const uint32_t subwaySum =
        FilterTest(manager, slot::kGetSubwayOccupantFilter, addr::cSC4NetworkManager_SubwayFilter_vftable,
                   addr::cSC4NetworkManager_SubwayFilter_vftable_4, all, subway);
    hash.Mix(waterSum);
    hash.Mix(subwaySum);
    uint32_t stations = 0;
    for (void* const occupant : all)
        stations += Virtual<uint8_t>(manager, slot::kIsOccupantSubwayStation, occupant) != 0;
    hash.Mix(stations);
    // The number of occupants is in the log line only: it is not the same
    // in two runs of the original (82,730 and 82,729 on one save; the
    // filters' and the station counts were).
    const bool every = EnvInt("OPENSC4_TEST_NETMGR_TOOLS", 0) != 0;
    uint32_t made = 0;
    const uint32_t tools = ToolTest(manager, every, made);
    hash.Mix(tools);
    g_setSum = hash.h;
    log::Write("autotest: netmgr: crews %08X (5 stand-ins registered and unregistered beside %u), filters %08X and "
               "%08X (of %u occupants the water filter took %u and the subway filter %u; %u are subway stations), "
               "tools %08X (%u made and released%s), set %08X%s",
               crews, there, waterSum, subwaySum, static_cast<unsigned>(all.size()), water, subway, stations, tools,
               made, every ? "" : ", type 0 only", g_setSum, collected ? "" : " (no occupant manager)");

    Hash life;
    const uint8_t again = Virtual<uint8_t>(manager, slot::kInit);
    life.Mix(again);
    uint32_t length = 0;
    const uint32_t written = WriteTest(manager, length);
    life.Mix(written);
    const bool object = EnvInt("OPENSC4_TEST_NETMGR_OBJECT", 1) != 0;
    bool second = false;
    uint32_t secondSum = 0;
    if (object) {
        secondSum = SecondObjectTest(manager, second);
        life.Mix(secondSum);
    }
    g_lifeSum = life.h;
    log::Write("autotest: netmgr: life: Init again %u, write %08X (%u bytes for %u pieces), second object %08X (%s), "
               "life %08X",
               again, written, length, Count(manager, field::kManaged, 4), secondSum,
               !object ? "left out" : second ? "made, written, read into and destroyed" : "not made", g_lifeSum);
}

// The fingerprint part, everything through the manager's vtable (the game's
// functions or ours) or read from its fields, nothing of it an address or a
// reference count:
//   st     the bytes at +0x10, +0x11, +0x12 and +0x74 (initialised, the
//          path remap's switch, batching, queueing)
//   rect   a checksum of the dirty cell rectangle
//   mg q crews retro   the sizes of the vectors at +0x24, +0x68 and +0x7C
//          and the length of the retrofit list
//   saved  (at the end of the part) the pieces of the vector at +0x24, the
//          save record's: SavedHash's checksum in the stored order and its
//          sum without an order
//   tools  ToolBits; names NamesHash; id IdentityHash
//   tab    TablesHash and the sizes of the three remap vectors and the edge
//          table; maps MapsHash
//   occ    the number of network pieces; station, pipe, conn: how many the
//          three lookups said yes for; isect: GetAllIntersectionPieces
//          asked and skipped, the entries it gave in all and the pieces
//          with the flag 0x100000; then the sum and the exclusive or of the
//          per-piece checksums (WalkPieces)
// and after OPENSC4_TEST_NETMGR's day ` set <checksum> life <checksum>` (the
// first part's calls and the second part's).
void Fingerprint(cISC4City* city, std::string& line)
{
    char* const manager = Manager(city);
    if (!manager) {
        line += " | netmgr none";
        return;
    }
    Hash rect;
    for (std::size_t i = 0; i < 4; ++i)
        rect.Mix(At<uint32_t>(manager, field::kDirtyRect + 4 * i));
    std::vector<Piece> pieces;
    Walked walked;
    if (Collect(city, pieces, nullptr))
        WalkPieces(city, manager, Virtual<char*>(city, kCityOccupantManager), pieces, walked);
    uint32_t savedOrdered = 0, savedSum = 0;
    SavedHash(manager, savedOrdered, savedSum);
    char text[512];
    std::snprintf(text, sizeof text,
                  " | netmgr st %u%u%u%u rect %08X mg %u q %u crews %u retro %u tools %04X names %08X id %08X tab "
                  "%08X,%u,%u,%u,%u maps %08X occ %u station %u pipe %u conn %u isect %u,%u,%u,%u %08X,%08X saved "
                  "%08X,%08X",
                  At<uint8_t>(manager, field::kInitialized), At<uint8_t>(manager, field::kLeftHand),
                  At<uint8_t>(manager, field::kBatching), At<uint8_t>(manager, field::kQueueing), rect.h,
                  Count(manager, field::kManaged, 4), Count(manager, field::kQueue, 4),
                  Count(manager, field::kCrews, 4), RetrofitLength(manager), ToolBits(manager), NamesHash(manager),
                  IdentityHash(manager), TablesHash(manager), Count(manager, field::kPreviewRemap, remap::kSize),
                  Count(manager, field::kShadowRemap, remap::kSize),
                  Count(manager, field::kPathRemap, remap::kPathSize),
                  At<uint32_t>(manager, field::kShadowEdges + edge::kCount), MapsHash(manager),
                  static_cast<unsigned>(pieces.size()), walked.stations, walked.pipes, walked.connections,
                  walked.asked, walked.skipped, walked.entries, walked.flagged, walked.sum, walked.mixed, savedOrdered,
                  savedSum);
    line += text;
    if (g_setDone) {
        std::snprintf(text, sizeof text, " set %08X life %08X", g_setSum, g_lifeSum);
        line += text;
    }
}

const FingerprintPart kFingerprint{"netmgr", 131, &Fingerprint};
const DayHook kDrive{"netmgr.drive", 194, &Drive};

} // namespace
} // namespace opensc4::autotest
