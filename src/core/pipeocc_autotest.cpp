// The pipe occupants' part of the autotest (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/occmgr_sim.h"
#include "core/pipeocc_sim.h"

#include <cISC4City.h>

#include <opensc4/addresses.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_set>
#include <vector>

namespace opensc4::autotest {
namespace {

namespace field = pipeocc_sim::field;
namespace slot = pipeocc_sim::slot;

struct Hash {
    uint32_t h = 2166136261u;
    void Mix(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    }
};

// How many pipes answered something that says the test reaches the paths of
// the functions (printed after the checksums).
struct Counters {
    uint32_t watered = 0, placed = 0, stored = 0, keyClass = 0, keyOther = 0, tinted = 0;
};

// The part of a pipe that the network occupant's walk (src/core/netocc_autotest.cpp,
// which takes every occupant of type 0xC772BF98, pipes included) does not
// reach: its own saved word (the watered word), the answers of its own
// overrides (the tint, the descriptor and the default flags, the virtual
// base's piece key, the class ID of the serializable part, QueryInterface)
// and the descriptor the intermediate class stored at +0x26C (whether it is
// slot 101's answer; the address itself is the exe's, so it is the same in
// every run, but only the comparison is mixed). Not mixed: the intermediate
// class's dirty byte at +0x260 (the view's refresh sets it), its other
// fields, pointers and reference counts.
//
// QueryInterface is asked for the pipe's IID, 0xA9764C8C, the network
// occupant's IID, 1 and one nobody answers, through the interface part and
// the occupant part, for one pipe in five (those whose hash so far is a
// multiple of 5, so the choice does not follow the order of the walk); each
// reference given is released.
uint32_t PipeHash(char* self, Counters& counters)
{
    Hash hash;
    char* const part = self + field::kOccupant;
    const uint32_t watered = At<uint32_t>(self, field::kWatered);
    hash.Mix(watered);
    counters.watered += watered == 1;
    uint32_t tint[3] = {};
    Virtual<uint32_t*>(self, slot::kGetTint, static_cast<uint32_t*>(tint));
    for (const uint32_t word : tint)
        hash.Mix(word);
    counters.tinted += tint[1] != tint[0];
    const std::uintptr_t descriptor = Virtual<std::uintptr_t>(self, slot::kGetDescriptor);
    hash.Mix(descriptor == addr::cSC4PipeOccupant_Descriptor);
    const bool stored = At<std::uintptr_t>(self, field::kDescriptor) == descriptor;
    hash.Mix(stored);
    counters.stored += stored;
    hash.Mix(Virtual<uint32_t>(self, slot::kGetDefaultFlags));
    uint32_t key = 0;
    Virtual<uint32_t>(self + field::kVbase, pipeocc_sim::kVbaseGetPieceKey, &key);
    hash.Mix(key);
    if (key == pipeocc_sim::kClassId)
        ++counters.keyClass;
    else
        ++counters.keyOther;
    hash.Mix(Virtual<uint8_t>(self, slot::kIsPlaced));
    counters.placed += Virtual<uint8_t>(self, slot::kIsPlaced) != 0;
    hash.Mix(Virtual<uint32_t>(self + field::kSerializable, 0x14));
    hash.Mix(Virtual<uint32_t>(part, 0x1C));
    if (hash.h % 5 == 0) {
        const uint32_t ids[] = {pipeocc_sim::kIidPipe, pipeocc_sim::kIidObject, pipeocc_sim::kIidNetworkOccupant, 1u,
                                0x0F10A0CCu};
        for (char* const asked : {self, part})
            for (const uint32_t iid : ids) {
                void* found = nullptr;
                const bool answered = Virtual<uint32_t>(asked, 0x0, iid, &found) & 1;
                hash.Mix(answered);
                if (answered && found) {
                    hash.Mix(static_cast<uint32_t>(static_cast<char*>(found) - self));
                    Virtual<uint32_t>(found, 0x8);
                }
            }
    }
    return hash.h;
}

// The statics the first Init sets up (0x00648A90): the eleven words from
// 0x00B46708 (ten floats, the done byte's word last, read as the bytes it
// is). Zero before any pipe was initialised.
uint32_t StaticsHash()
{
    Hash hash;
    const std::uintptr_t statics = addr::cSC4PipeOccupant_Statics;
    for (std::size_t offset = 0; offset < pipeocc_sim::kStaticsDone; offset += 4)
        hash.Mix(Global<uint32_t>(statics + offset));
    hash.Mix(Global<uint8_t>(statics + pipeocc_sim::kStaticsDone));
    return hash.h;
}

// Every pipe of the city (build 641; src/core/pipeocc_sim.h), found through
// the occupant manager (the city's slot 34 / +0x88): every occupant of the
// network occupant type 0xC772BF98 in its grid, one in several cells once,
// whose serializable part answers the pipe's class ID. The objects are the
// interface part (the occupant part less 4). False without a manager.
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
                if (Virtual<uint32_t>(object, 0x1C) != pipeocc_sim::kOccupantType)
                    continue;
                char* const self = static_cast<char*>(object) - field::kOccupant;
                if (Virtual<uint32_t>(self + field::kSerializable, 0x14) == pipeocc_sim::kClassId)
                    seen.insert(self);
            }
        }
    }
    Virtual<void>(manager + occmgr_sim::field::kLock, 0x8);
    found.assign(seen.begin(), seen.end());
    return true;
}

// The setter's exercise (OPENSC4_TEST_PIPEOCC=<day>): the checksum, summed
// over the objects driven, of what the watered word and the tint say after
// SetWatered, and how many objects were driven.
uint32_t g_setSum = 0;
uint32_t g_setCount = 0;
bool g_setDone = false;

// SetWatered (0x00648540) on one pipe whose word is 0 or 1: with the other
// value, the word and the tint read back, then with the old value, both read
// back again. A value the pipe already has does nothing, so each call that
// changes the value runs the refresh the setter ends in (slot 100, the
// intermediate class's) once, as the water simulator's step does for every
// pipe whose value changes. A pipe with another word is left alone.
uint32_t Exercise(char* self)
{
    using SetWateredFn = uint32_t(__thiscall*)(void*, uint32_t);
    const auto setWatered = reinterpret_cast<SetWateredFn>(addr::SC4Pipe_SetWatered);
    const uint32_t old = At<uint32_t>(self, field::kWatered);
    if (old > 1)
        return 0;
    Hash hash;
    for (const uint32_t value : {1u - old, old}) {
        setWatered(self, value);
        hash.Mix(At<uint32_t>(self, field::kWatered));
        uint32_t tint[3] = {};
        Virtual<uint32_t*>(self, slot::kGetTint, static_cast<uint32_t*>(tint));
        for (const uint32_t word : tint)
            hash.Mix(word);
    }
    return hash.h;
}

// One QueryInterface through a part: the answer, and where in the object the
// answer lies (never the address); the reference it gave is released.
void AskInterface(Hash& hash, char* self, char* part, uint32_t iid)
{
    void* found = nullptr;
    const bool answered = (Virtual<uint32_t>(part, 0x0, iid, &found) & 1) != 0;
    hash.Mix(answered);
    if (answered && found) {
        hash.Mix(static_cast<uint32_t>(static_cast<char*>(found) - self));
        Virtual<uint32_t>(found, 0x8);
    }
}

// The class's life on one made object (OPENSC4_TEST_PIPEOCC=<day>): the
// game's factory (0x006489B0: the pool, the constructor; it returns the
// occupant part), one reference held by the test, QueryInterface through
// the parts that answer to it (the interface, the occupant part, the
// serializable part, the holder, the exemplar part and the virtual base,
// whose slot 0 is a thunk to it) for the IDs the class answers (its own,
// 0xA9764C8C, the network occupant's, 1, the occupant, the serializable
// and 0x26A68449) and some it does not (the network occupant's class ID,
// one nobody has); GetGZCLSID of the serializable part; the fields the
// constructor sets (the watered word, the virtual base's vtable and shift,
// the tint, the descriptor and default flags answers, the piece key); and
// both Inits on an object marked initialised (the branch that leaves the
// object alone: they only look at the statics). The last Release destroys
// the object through the deleting destructor and returns the item to its
// pool page. Not called: the Inits of an object not initialised, which end
// in the occupant message 0x99EF1140 (queued, with the made object as its
// object): listeners of that message would meet an occupant that is in no
// cell and no network. Not mixed: the descriptor stored at +0x26C, which
// the constructors leave as the pool's memory holds it, and the dirty byte.
uint32_t Life()
{
    Hash hash;
    char* const part = reinterpret_cast<char*(__cdecl*)()>(addr::cSC4PipeOccupant_Create)();
    if (!part)
        return 0;
    char* const self = part - field::kOccupant;
    Virtual<uint32_t>(self, 0x4);
    hash.Mix(Virtual<uint32_t>(self + field::kSerializable, 0x14));
    const uint32_t ids[] = {pipeocc_sim::kIidPipe,
                            pipeocc_sim::kIidObject,
                            pipeocc_sim::kIidNetworkOccupant,
                            1u,
                            0x26BB767Eu,
                            0xE4FDA3D4u,
                            0x26A68449u,
                            0xC9C05C6Eu,
                            0x0F10A0CCu};
    for (const std::size_t offset : {std::size_t{0}, field::kOccupant, field::kSerializable, field::kHolder,
                                     field::kExemplarPart, field::kVbase})
        for (const uint32_t iid : ids)
            AskInterface(hash, self, self + offset, iid);
    hash.Mix(At<uint32_t>(self, field::kWatered));
    hash.Mix(At<uint32_t>(self, field::kVbaseShift));
    hash.Mix(At<std::uintptr_t>(self, field::kVbase) == addr::cSC4PipeOccupant_vbase_vftable);
    hash.Mix(At<std::uintptr_t>(self, field::kVbtable) == addr::cSC4PipeOccupant_vbtable);
    uint32_t tint[3] = {};
    Virtual<uint32_t*>(self, slot::kGetTint, static_cast<uint32_t*>(tint));
    for (const uint32_t word : tint)
        hash.Mix(word);
    hash.Mix(Virtual<std::uintptr_t>(self, slot::kGetDescriptor) == addr::cSC4PipeOccupant_Descriptor);
    hash.Mix(Virtual<uint32_t>(self, slot::kGetDefaultFlags));
    uint32_t key = 0;
    Virtual<uint32_t>(self + field::kVbase, pipeocc_sim::kVbaseGetPieceKey, &key);
    hash.Mix(key);
    At<uint8_t>(self, field::kOccupant + 0x2E) |= 1;
    hash.Mix(Virtual<uint8_t>(part, 0xC));
    uint32_t description[16] = {};
    hash.Mix(Virtual<uint8_t>(self, slot::kInit, static_cast<uint32_t*>(description)));
    At<uint8_t>(self, field::kOccupant + 0x2E) &= 0xFE;
    Virtual<uint32_t>(self, 0x8);
    return hash.h;
}

// OPENSC4_TEST_PIPEOCC=<day>: on that day every seventh pipe (chosen by the
// grid position the intermediate class keeps at +0x23C and +0x240, so the
// choice does not follow the order of the walk; the pipes' own state is the
// same for nearly all) goes through Exercise, and one made object through Life. The
// read-back checksums are summed into the fingerprint's ` set` field from
// then on.
void Drive(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_PIPEOCC") || !game::IsBuild641() || g_setDone)
        return;
    g_setDone = true;
    std::vector<char*> objects;
    if (!Collect(day.city, objects))
        return;
    g_setSum += Life();
    for (char* const self : objects) {
        if ((At<uint32_t>(self, 0x23C) * 31u + At<uint32_t>(self, 0x240)) % 7 != 0)
            continue;
        g_setSum += Exercise(self);
        ++g_setCount;
    }
}

// The fingerprint part: the number of pipes and the sum and the exclusive
// or of PipeHash over them (order independent; no addresses), then the
// counters and the statics' checksum, and after OPENSC4_TEST_PIPEOCC's day
// ` set <checksum>,<count>`.
void Fingerprint(cISC4City* city, std::string& line)
{
    std::vector<char*> objects;
    if (!Collect(city, objects)) {
        line += " | pipeocc none";
        return;
    }
    uint32_t sum = 0, mixed = 0;
    Counters counters;
    for (char* const self : objects) {
        const uint32_t hash = PipeHash(self, counters);
        sum += hash;
        mixed ^= hash;
    }
    char text[256];
    std::snprintf(text, sizeof text,
                  " | pipeocc %u,%08X,%08X wet %u placed %u stored %u keyc %u keyo %u tint %u st %08X",
                  static_cast<unsigned>(objects.size()), sum, mixed, counters.watered, counters.placed,
                  counters.stored, counters.keyClass, counters.keyOther, counters.tinted, StaticsHash());
    line += text;
    if (g_setDone) {
        std::snprintf(text, sizeof text, " set %08X,%u", g_setSum, static_cast<unsigned>(g_setCount));
        line += text;
    }
}

const FingerprintPart kFingerprint{"pipeocc", 121, &Fingerprint};
const DayHook kDrive{"pipeocc.drive", 185, &Drive};

} // namespace
} // namespace opensc4::autotest
