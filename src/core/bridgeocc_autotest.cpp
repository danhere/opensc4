// The bridge occupants' part of the autotest (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/bridgeocc_sim.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/occmgr_sim.h"

#include <cISC4City.h>

#include <opensc4/addresses.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_set>
#include <vector>

namespace opensc4::autotest {
namespace {

namespace field = bridgeocc_sim::field;
namespace slot = bridgeocc_sim::slot;
namespace occupant = bridgeocc_sim::occupant;
namespace attachments = bridgeocc_sim::attachments;

struct Hash {
    uint32_t h = 2166136261u;
    void Mix(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    }
};

// How many bridge pieces answered something that says the test reaches the
// paths of the functions (printed after the checksums).
struct Counters {
    uint32_t initialised = 0, flagged = 0, canLight = 0, counted = 0, own = 0, parent = 0;
};

// One QueryInterface through a part: the answer, and where in the object the
// answer lies (never the address); the reference it gave is released. True
// when the part answered.
bool AskInterface(Hash& hash, char* self, char* part, uint32_t iid)
{
    void* found = nullptr;
    const bool answered = (Virtual<uint32_t>(part, 0x0, iid, &found) & 1) != 0;
    hash.Mix(answered);
    if (answered && found) {
        hash.Mix(static_cast<uint32_t>(static_cast<char*>(found) - self));
        Virtual<uint32_t>(found, 0x8);
    }
    return answered;
}

// The cell a piece is in (the interface's slot 59, the network occupant's).
void CellOf(char* self, uint32_t& x, uint32_t& z)
{
    x = 0;
    z = 0;
    Virtual<void, uint32_t*, uint32_t*>(self, slot::kGetOccupiedCell, &x, &z);
}

// The part of a bridge piece that the network occupant's walk
// (src/core/netocc_autotest.cpp, which takes every occupant of type
// 0xC772BF98, bridge pieces included) does not reach: the answers of the
// bridge's own overrides (the class ID of the serializable part,
// QueryInterface, CanBeLit), the two flags its constructor sets, and what
// the parent class saves after the network occupant's record (three floats
// at +0x178, the word at +0x1B0, the byte at +0x1B4, the count at +0x19C and
// the four floats after it when it is not zero), with the piece's cell and
// whether it is initialised. Not mixed: the member at +0x1D0 (its owner is
// the piece's model instance, which the view makes and drops), the parent
// class's model references and unsaved fields, pointers and reference
// counts.
//
// QueryInterface is asked through the interface part and the occupant part
// for the bridge's IID, the parent class's, the network occupant's, 1 and
// one nobody answers, for every piece; each reference given is released.
uint32_t BridgeHash(char* self, Counters& counters)
{
    Hash hash;
    char* const part = self + field::kOccupant;
    hash.Mix(Virtual<uint32_t>(self + field::kSerializable, 0x14));
    hash.Mix(Virtual<uint32_t>(part, occupant::kGetType));
    const bool initialised = Virtual<uint8_t>(part, occupant::kIsInitialized) != 0;
    hash.Mix(initialised);
    counters.initialised += initialised;
    const uint32_t flags = At<uint32_t>(self, field::kFlags) & bridgeocc_sim::kBridgeFlags;
    hash.Mix(flags);
    counters.flagged += flags == bridgeocc_sim::kBridgeFlags;
    const bool canLight = Virtual<uint8_t>(self, slot::kCanBeLit) != 0;
    hash.Mix(canLight);
    counters.canLight += canLight;
    for (std::size_t offset = 0; offset < 12; offset += 4)
        hash.Mix(At<uint32_t>(self, field::kSavedFloats + offset));
    hash.Mix(At<uint32_t>(self, field::kSavedWord));
    hash.Mix(At<uint8_t>(self, field::kSavedByte));
    const uint32_t count = At<uint32_t>(self, field::kSavedCount);
    hash.Mix(count);
    if (count) {
        ++counters.counted;
        for (std::size_t offset = 0; offset < 16; offset += 4)
            hash.Mix(At<uint32_t>(self, field::kSavedFour + offset));
    }
    uint32_t x = 0, z = 0;
    CellOf(self, x, z);
    hash.Mix(x);
    hash.Mix(z);
    const uint32_t ids[] = {bridgeocc_sim::kIidBridge, bridgeocc_sim::kIidPrebuilt,
                            bridgeocc_sim::kIidNetworkOccupant, 1u, 0x0F10A0CCu};
    for (char* const asked : {self, part}) {
        counters.own += AskInterface(hash, self, asked, ids[0]);
        counters.parent += AskInterface(hash, self, asked, ids[1]);
        for (std::size_t i = 2; i < 5; ++i)
            AskInterface(hash, self, asked, ids[i]);
    }
    return hash.h;
}

// Every bridge piece of the city (build 641; src/core/bridgeocc_sim.h),
// found through the occupant manager (the city's slot 34 / +0x88): every
// occupant of the network occupant type 0xC772BF98 in its grid, one in
// several cells once, whose serializable part answers the bridge's class
// ID. The objects are the interface part (the occupant part less 4). False
// without a manager.
bool Collect(cISC4City* city, std::vector<char*>& found)
{
    if (!game::IsBuild641())
        return false;
    char* const manager = Virtual<char*>(city, 0x88);
    if (!manager || !At<uint8_t>(manager, occmgr_sim::field::kInitialized))
        return false;
    std::unordered_set<char*> seen;
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
                if (Virtual<uint32_t>(object, 0x1C) != bridgeocc_sim::kOccupantType)
                    continue;
                char* const self = static_cast<char*>(object) - field::kOccupant;
                if (Virtual<uint32_t>(self + field::kSerializable, 0x14) == bridgeocc_sim::kClassId)
                    seen.insert(self);
            }
        }
    }
    Virtual<void>(manager + occmgr_sim::field::kLock, 0x8);
    found.assign(seen.begin(), seen.end());
    return true;
}

// The exercise (OPENSC4_TEST_BRIDGEOCC=<day>): the checksum, summed over
// the objects driven, and how many objects were driven.
uint32_t g_setSum = 0;
uint32_t g_setCount = 0;
bool g_setDone = false;

const uint32_t kAllIds[] = {bridgeocc_sim::kIidBridge,
                            bridgeocc_sim::kIidPrebuilt,
                            bridgeocc_sim::kIidNetworkOccupant,
                            0xA9764C8Cu,
                            1u,
                            0x26BB767Eu,
                            0xE4FDA3D4u,
                            0x26A68449u,
                            0xC9C05C6Eu,
                            0x0F10A0CCu};

// QueryInterface through every part that answers to it (the interface, the
// occupant part, the serializable part, the holder, the exemplar part and
// the virtual base, whose slot 0 is a thunk to it) for the IDs of kAllIds.
void AskEveryPart(Hash& hash, char* self)
{
    for (const std::size_t offset : {std::size_t{0}, field::kOccupant, field::kSerializable, field::kHolder,
                                     field::kExemplarPart, field::kVbase})
        for (const uint32_t iid : kAllIds)
            AskInterface(hash, self, self + offset, iid);
}

// One bridge piece of the city. The class has no setter of its own: what
// can be driven on a piece that is in use, without changing it, is the
// occupant part's Init on an initialised object (the bridge's branch that
// answers true and does nothing; a piece that is not initialised is left
// alone, the other branch would initialise it) and QueryInterface through
// every part. Not called: Shutdown (it ends the piece) and slot 81 (the
// game's; it changes what the piece's model instance carries, which the view
// owns).
uint32_t Exercise(char* self)
{
    Hash hash;
    char* const part = self + field::kOccupant;
    if (Virtual<uint8_t>(part, occupant::kIsInitialized))
        hash.Mix(Virtual<uint8_t>(part, occupant::kInit));
    AskEveryPart(hash, self);
    return hash.h;
}

// The class's life on one made object (OPENSC4_TEST_BRIDGEOCC=<day>): the
// game's factory (0x00605450: the pool, the constructor; it returns the
// occupant part), one reference held by the test, GetGZCLSID of the
// serializable part, QueryInterface through six parts for ten IDs; the
// fields the constructors set (the two flags, the parent's saved word, byte
// and count, the virtual base's vtable and shift, the member's vtable, owner
// and count: comparisons and numbers, no addresses); CanBeLit; Shutdown on
// the object as made (not initialised: the branch that answers true and does
// nothing); and the occupant part's Init on the object marked initialised
// (the branch that does nothing). The last
// Release destroys the object through the deleting destructor (the member's
// destructor, the parent's, the network occupant's) and returns the item to
// its pool page. Not called: the occupant part's Init of an object not
// initialised, which goes to the parent's and the network occupant's and
// ends in the occupant message 0x99EF1140 (queued, with the made object as
// its object): listeners of that message would meet an occupant that is in
// no cell and no network; Shutdown of an initialised object (the parent's
// and the network occupant's shutdown of an object no manager knows); slot
// 81 (the game's: the model code calls it with a model instance, and drawing
// is not this work's); Write and Read (the parent's; no test here writes
// into a stream of the game's).
uint32_t Life()
{
    Hash hash;
    char* const part = reinterpret_cast<char*(__cdecl*)()>(addr::cSC4NetworkBridgeOccupant_Create)();
    if (!part)
        return 0;
    char* const self = part - field::kOccupant;
    char* const member = self + field::kAttachments;
    Virtual<uint32_t>(self, 0x4);
    hash.Mix(Virtual<uint32_t>(self + field::kSerializable, 0x14));
    AskEveryPart(hash, self);
    hash.Mix(At<uint32_t>(self, field::kFlags) & bridgeocc_sim::kBridgeFlags);
    hash.Mix(At<uint32_t>(self, field::kSavedWord));
    hash.Mix(At<uint8_t>(self, field::kSavedByte));
    hash.Mix(At<uint32_t>(self, field::kSavedCount));
    hash.Mix(At<uint32_t>(self, field::kVbaseShift));
    hash.Mix(At<std::uintptr_t>(self, field::kVbase) == addr::cSC4NetworkBridgeOccupant_vbase_vftable);
    hash.Mix(At<std::uintptr_t>(self, field::kVbtable) == addr::cSC4NetworkBridgeOccupant_vbtable);
    hash.Mix(At<std::uintptr_t>(member, 0) == addr::cSC4NetworkBridgeOccupant_Attachments_vftable);
    hash.Mix(At<void*>(member, attachments::kOwner) == nullptr);
    hash.Mix(At<uint32_t>(member, attachments::kCount));
    hash.Mix(Virtual<uint8_t>(self, slot::kCanBeLit));
    hash.Mix(Virtual<uint8_t>(part, occupant::kIsInitialized));
    hash.Mix(Virtual<uint8_t>(self, slot::kShutdown));
    At<uint8_t>(self, field::kOccupant + occupant::kState) |= 1;
    hash.Mix(Virtual<uint8_t>(part, occupant::kInit));
    At<uint8_t>(self, field::kOccupant + occupant::kState) &= 0xFE;
    Virtual<uint32_t>(self, 0x8);
    return hash.h;
}

// OPENSC4_TEST_BRIDGEOCC=<day>: on that day one made object goes through
// Life and every third bridge piece (chosen by the cell it is in, so the
// choice does not follow the order of the walk) through Exercise. The
// checksums are summed into the fingerprint's ` set` field from then on;
// the field is printed once the test ran, with a count of 0 when the city
// has no bridge.
void Drive(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_BRIDGEOCC") || !game::IsBuild641() || g_setDone)
        return;
    g_setDone = true;
    std::vector<char*> objects;
    if (!Collect(day.city, objects))
        return;
    g_setSum += Life();
    for (char* const self : objects) {
        uint32_t x = 0, z = 0;
        CellOf(self, x, z);
        if ((x * 31u + z) % 3 != 0)
            continue;
        g_setSum += Exercise(self);
        ++g_setCount;
    }
}

// The fingerprint part: the number of bridge pieces and the sum and the
// exclusive or of BridgeHash over them (order independent; no addresses),
// then the counters, and after OPENSC4_TEST_BRIDGEOCC's day
// ` set <checksum>,<count>`.
void Fingerprint(cISC4City* city, std::string& line)
{
    std::vector<char*> objects;
    if (!Collect(city, objects)) {
        line += " | bridgeocc none";
        return;
    }
    uint32_t sum = 0, mixed = 0;
    Counters counters;
    for (char* const self : objects) {
        const uint32_t hash = BridgeHash(self, counters);
        sum += hash;
        mixed ^= hash;
    }
    char text[256];
    std::snprintf(text, sizeof text, " | bridgeocc %u,%08X,%08X init %u fl %u lit %u cnt %u own %u par %u",
                  static_cast<unsigned>(objects.size()), sum, mixed, counters.initialised, counters.flagged,
                  counters.canLight, counters.counted, counters.own, counters.parent);
    line += text;
    if (g_setDone) {
        std::snprintf(text, sizeof text, " set %08X,%u", g_setSum, static_cast<unsigned>(g_setCount));
        line += text;
    }
}

const FingerprintPart kFingerprint{"bridgeocc", 127, &Fingerprint};
const DayHook kDrive{"bridgeocc.drive", 190, &Drive};

} // namespace
} // namespace opensc4::autotest
