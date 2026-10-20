// The automata controller manager's part of the autotest
// (src/core/autotest_hooks.h; the manager in src/core/automata_ctlmgr.cpp).
//
// OPENSC4_TEST_AUTOMATA=1: a fingerprint part on every day,
//   ` | auto n<occupants>/<controllers> c<checksum> g<side>,<filled>,<entries>,<checksum>`:
//   the controller table (+0x58): the occupants and the controllers, each
//   occupant by its type, (not for automata) its cells and (not for props
//   and automata) its position in x and z, each controller by its kind (attractor, generator,
//   other) and its template (class ID, name, flags, radius), mixed per
//   occupant and summed (the table's order follows addresses); the grid
//   (+0x1C): its side, the cells with a controller, the entries and a sum
//   of (cell index, template). The controller time (+0x34..+0x43) follows
//   frames (A vs A differs from day 4 on): the log only. Nothing
//   of a controller's own state is read (the attractor and generator code
//   is frame-driven). The automaton table (+0x6C) is written by that code
//   on frames: it goes to the log only (`autotest: automata: ...`).
// OPENSC4_TEST_AUTOMATA=2: also, on the run day OPENSC4_TEST_AUTOMATA_DAY
//   (default 3), Write (the game's address: ours when
//   simulators.automata.ctlsave is on) into a stream of the test's own; the
//   fingerprint then carries ` w<answer>,<bytes>,<checksum>` from that day
//   on: the version (the controller time's 12 bytes left out: frames), the occupant count and
//   the sorted per-occupant controller counts (references write nothing to
//   this stream: SC4Stream::WriteReference asks the stream for the save
//   file's segment interface first). The automaton table's records go to
//   the log. Read-only: nothing of the manager or the controllers changes.
//   On the same day it also calls part a2's queries at their game addresses
//   (ours when simulators.automata.ctlquery is on): for up to 16 (occupant,
//   controller) pairs of the controller table, occupants neither automata
//   nor props (their positions follow frames), sorted by occupant and
//   controller hash (address-free): GetController by template (slot 18) and
//   by the controller's name (slot 19), slot 18 with no template, and
//   GetController near the occupant's position by name (slot 17, radius
//   64); the answers by kind and template go into the fingerprint as
//   ` q<pairs>,<checksum>` from that day on. At the same positions
//   GetRelevantControllersByWorldPos for attractors and generators (each
//   list released through ReleaseControllerList, its head freed after), the
//   composite attraction effect and, for network occupants, the commotion:
//   they read the controllers' flags, the controller time and the attractor
//   state, which follow frames: the log only (`autotest: automata: query
//   ...`). Nothing adds, removes or moves a controller or sends a message
//   (the relevant lists' references are given back).
// OPENSC4_TEST_AUTOMATA=3 (part a3c): also, on the same day, for up to 8
//   attractors and 8 generators of the controller table (occupants neither
//   automata nor props, sorted as the query pairs), at the game's addresses
//   (ours when the controller groups are on): the +4 serializable part's
//   QueryInterface 0x00517F30 (three interfaces; the AddRef it makes is
//   taken back from +0x20) and GetGZCLSID 0x00517F00; Write (the part's slot
//   3) into a stream of the test's own; a round trip: a new controller of
//   the same class from its factory reads those bytes (slot 4) and writes
//   them again, which must give the same bytes with an empty name (Read puts
//   the name into the controller's own name, Write saves the template's),
//   and its own name must be the template's; on that new controller SetLimit
//   (slot 19), SetTemplate (15) and ResetAccumulators (26), and for a
//   generator a Read of a stream with version 0 (answers 0); then it is
//   released. And the position from the occupant's lot (slot 22) of the
//   picked controller. The bytes' fixed part (versions, name, position,
//   +0x3C, the attractor's curve, the generator's +0x90) goes into the
//   fingerprint as ` s<controllers>,<checksum>,<state>`, the state checksum
//   (+0x50, +0x54, the flags, the generator's clock pairs and +0x94, the
//   automaton count: tick- and frame-driven) after it; the field values go
//   to the log (`autotest: automata: save ...`). Then the spawn (slot 41,
//   curve 1.0) of up to 4 of those generators, called directly (the tick
//   only calls it with flag 4, the camera view, which the harness never
//   sets): the automata it makes (count, +0x88, +0x90, +0x94, +0x98, each
//   new automaton's type and position) as ` p<generators>,<made>,<checksum>`;
//   with the first new automaton the generator's events 29 (kinds 4 and 0)
//   and 30 (kind 4, which takes back what 29 added), and the first
//   attractor's (one whose template keeps automata, if any) events 29 and
//   30 with kind 0 (in and out of its own list,
//   then 30 again when it is not there), the answers and list counts mixed.
//   The spawn adds automata to the city: from that day on the run is not
//   the one without the test (on both sides alike).
// OPENSC4_TEST_AUTOMATA_VEHMGR=1 (part v1, with OPENSC4_TEST_AUTOMATA=2 or
//   more): on the same day the vehicle and pedestrian managers (city slots
//   43 and 44) are queried at the game's addresses (ours when
//   simulators.automata.vehmgr is on), each manager into the fingerprint as
//   ` v<identity>,<kinds>,<scale>,<cells>,<records>,<hits>,<groups>` (the
//   vehicle manager) and ` <...>` (the pedestrian manager) from that day
//   on: QueryInterface for four interfaces on the object and through the
//   serializable part (each AddRef given back) and GetGZCLSID; for kinds 0
//   to 12 slots 73, 74, 85 (the curve's address), 86, 87 and 88 (a 16-word
//   block of the test's own, scales 1.25 and -0.75); slot 82 for the four
//   directions and the record list's hit test (0x00551630) at 12 x 12 cells
//   spread over the city (sizes from the city); the group tests 0x0054D1A0,
//   0x0054D210 and 0x0054D230 over groups 0x4000..0x4240. Slot 75 (it reads
//   the 24-hour clock), the automata of the manager's list with slots 68
//   and 41 and 0x0054D150 (which automata there are follows frames), and
//   those two tests with null go to the log only (`autotest: automata:
//   vehicle manager: ...`). Read-only.
// OPENSC4_TEST_AUTOMATA_VEHMGR=2 (part v2): also, on the same day, for each
//   manager at the game's addresses (ours when the simulators.automata.vehmgr
//   groups are on): Write (the serializable part's slot 3) into a stream of
//   the test's own; into the fingerprint as ` x<fixed>,<flags>` (the vehicle
//   manager) and ` <fixed>,<flags>` (the pedestrian manager) from that day
//   on, where fixed is the versions, the most automata, the two bools,
//   +0xA8C, the kind and the record list (the automata count and the common
//   base's table, frame-driven, go to the log only). Then a round trip: a new
//   manager of the class from its factory (0x005145A0, 0x005145D0), held by
//   one reference, reads those bytes (slot 4; the automata count zeroed in
//   the copy, the references read back as nothing) and writes them again,
//   which must give the copy with its own +0xA8C (Read drops the saved one);
//   reads of version 0, of version 2 (no record list) and of a record list
//   of version 9; AddAutomaton with none while full (its count set to the
//   most for the call, so nothing is made) and RemoveAutomaton with none
//   (flags 0 and 1); a message of a type no one handles and, for the vehicle
//   manager, 0xC9DA96E8 (its list is empty) to its message target, and the
//   vehicle manager's +4 slot 3 with a zeroed message; then the release
//   (Shutdown, not initialized, and the deleting destructor). Then the
//   pedestrian manager's gathering 0x00544F30 on the city's manager around
//   its first pedestrian with distance 0 (none is sent; the sound message
//   0xA9E3B6E3 goes out), the pedestrian's send 0x005448D0 on that
//   pedestrian to its own position (its mode changes: frame-driven state,
//   alike on both sides), and the vehicle manager's overlay stop 0x00562D60
//   on a zeroed object (nothing). The answers' AL and whether the bytes came
//   back the same are the flags. Nothing else of the city's managers
//   changes.
// OPENSC4_TEST_AUTOMATA_VEHMGR=3 (part v3): also, on the same day, for each
//   manager at the game's addresses (ours when the simulators.automata.vehmgr
//   groups are on), into the fingerprint as ` y<fixed>,<flags>` (the vehicle
//   manager) and ` <fixed>,<flags>` (the pedestrian manager) from that day
//   on: on a new manager of the class from its factory, three records by
//   slot 70 (and none without an object) with two building occupants of the
//   controller table (the least by OccupantHash), slot 55 taking the first
//   occupant's records out (0x0054DB50 moves the rest down) and then the
//   second's, the records hashed after each step with the occupants as
//   indices; slot 69 and slot 52 (with the first of two network occupants,
//   the cells' occupants at 12 x 12 places spread over the cells object's
//   rectangle) on its empty automata list (their AL and
//   whether EAX's upper bytes are the list head's); slot 72 for kinds 0, 7,
//   8 and 11 (zero, or slot 88 with a 16-word block of the test's own); then
//   the release. The record copy 0x0054DB50 on records of the test's own.
//   For the vehicle manager, the tuning map's insert and lookup (0x00555350,
//   0x00555400) on a map of the test's own (two records; no key that is not
//   there, which would add a record of stack leftovers in the game), then
//   the lookup of each key of the game's map 0x00B21D60 (read-only), its
//   keys and records hashed. The log only (`autotest: automata: ... manager
//   frames`), on the city's manager, since which automata there are, the
//   cells' contents and the manager's generator follow frames:
//   GetRandomCreationPoint (slot 16) at four places spread over the city
//   (sizes from the city) with the generator (+0x50) seeded for the calls
//   and put back after; the occupant's position (0x0054DD50) and slot 72 for
//   the kinds of +0xA88's tables at the two network occupants; then AddAutomaton
//   with none while there is room (the branch that makes one from the
//   class's pool) and the one made, the list's last, removed again (slot 8
//   with 1). Then the frame work the harness's saves leave off (+0x4C, the
//   manager's own generation, is clear in them and they have no records):
//   the kind sweep (slot 76) through each kind of the list, with the
//   generator seeded for it and put back after (the harness's stopwatch
//   counts its reads, so a call walks a few cells: it is called again until
//   the kind's walk is through, at most 40000 calls), and records as slot 53
//   makes them (the IDs of property 0xE9AEE3DA / 0xE9AEE3DC of the first
//   building occupant of the controller table that has it, by
//   OccupantHash; for the vehicle manager also the drawn record 0x4206)
//   spent by slot 84 with a time of 1, then what is left of them released
//   and cleared. These add automata to the city and take some away: from
//   that day on the run is not the one without the test (on both sides
//   alike).
// OPENSC4_TEST_AUTOMATA_SCRIPTS=1 (part s1, with OPENSC4_TEST_AUTOMATA=2 or
//   more): on the same day the city's automata script system (city slot
//   0xC0) is queried at the game's addresses (ours when the
//   simulators.automata.scripts groups are on), into the fingerprint as
//   ` S<identity>,<maps>,<groups>,<matches>,<fresh>` from that day on:
//   QueryInterface for six interfaces on the object and through the +4, +8
//   and +0xC adjustors (each AddRef given back through the answer's slot
//   2, our Release), GetGZCLSID and the +8 part's ID test; the six map
//   getters (as offsets); every name of the name map (+0x4C) through
//   GetOccupantGroup and GetAutomataGroup by name (slots 12, 7), every ID
//   of the ID map (+0x74) through slots 11 and 6 and three IDs not there,
//   the special filter and track building lists, every drive command (slot
//   14) and two keys not there, every controller template by name (slot
//   17), all summed so that the maps' order does not count; per group its
//   ID, class, percentage, words, parent's ID and the group queries
//   (Depth 0x0051C4A0, GetClass 0x0051C3B0, IsInGroup 0x0051C380 for its
//   own, its parent's and 0x4000, FindName 0x0051D970 on its own name map's
//   names and one not there); Matches (the group's slot 3) of up to 16
//   groups (the ID map's, sorted by ID) for up to 8 occupants of the
//   controller table (neither automata nor props, sorted by their hash);
//   it seeds a cRZRandom from the clock for groups below 100 percent (the
//   same on both sides). Then a fresh script system from the factory
//   (0x005148A0) with one reference: QueryInterface, three groups made by
//   CreateOccupantGroup (an automata group with an ID, one without an ID,
//   one with an ID whose parent is set to the first) and the first name
//   once more (refused), the lookups and group queries on them (the first
//   group's class and the third's percentage, 0.5, set for the test), then
//   ReleaseTemplates 0x00520120 and ReleaseGroups 0x005201D0 on it (the
//   groups' last references, so their destructors run) and its Release
//   (Shutdown, not initialized, the destructor body). The city's script
//   system is not changed.
// OPENSC4_TEST_AUTOMATA_SCRIPTS=2 (part s2): also, after the ` S` token,
//   ` T<templates>,<helpers>,<records>`: the city's controller templates
//   (kind, class, +0x20, +0x34, name), summed, and a fresh script system's
//   CreateControllerTemplate (slot 16) for an attractor and a generator, the
//   first name again and a class with no template (both refused), its
//   template lookup and ReleaseTemplates; for the city's groups of the ID
//   map the cached ID list 0x0051CD90 (it fills an empty cache, alike on
//   both sides) and the record lookup 0x005204D0 for every key byte into a
//   three-string record (0x0051D040, 0x0051D0D0); std::sort 0x00522800 of
//   40 behaviour records of the test's own (keyed so that the comparator is
//   a strict weak order; above 16 it partitions), partial_sort 0x00522590 of
//   40 with a middle of 20 (the heap functions), and the full record
//   vector's insert 0x00521350 of 3 copies in the middle and of one at the
//   end. The template names go to the log. The Lua functions are reached
//   with OPENSC4_TEST_LUA_SCRIPT=tests/scripts/automata.lua (an absolute
//   path), which calls sc4game.automata.* (the ` | lua` part's script hash).
// OPENSC4_TEST_AUTOMATA_SCRIPTS=3 (part s3a): also ` L<city>,<fresh>,<load>`:
//   the city's script system's loaded state (every template: its fields,
//   curves, ID lists and vectors, behaviour records; every group's loaded
//   fields; the drive commands; the track buildings; summed by name) and
//   the same of a fresh script system from the factory into which
//   LoadScripts (0x00523B10, at the game's address) loads the scripts, and
//   that load's answer; the fresh system's templates and groups are then
//   released and the object released. The two hashes are equal when the
//   load at the test day reads what the city's load read.
// OPENSC4_TEST_AUTOMATA_AWMGR=1 (part aw1, with OPENSC4_TEST_AUTOMATA=2 or
//   more): on the same day, before the parts v1-v3 drives, the city's four
//   automata managers (aircraft, watercraft, vehicle, pedestrian: city
//   slots 45, 46, 43, 44) are queried at the game's addresses (ours when
//   simulators.automata.awmgr is on), into the fingerprint as
//   ` A<aircraft>,<watercraft>,<vehicle>,<pedestrian>,<fresh>,<flags>` from
//   that day on: QueryInterface for the four class IDs and six interfaces on
//   the object and through the +4, +8 and +0xC parts (AL and the answer's
//   offset; each AddRef given back through the answer's Release), AddRef
//   and Release (which must give the count back), GetGZCLSID through the
//   serializable part and the common base's 0x00512740, IsActive, IsPaused,
//   IsDrivingMode, GetMinZoom, IsCameraFollowUpdating, GetExemplarData and
//   its map (as offsets), the five-argument Dispatch stub 0x005053F0; every
//   exemplar record found by its own key (slot 44) and a key not there;
//   every key of the exemplar map through CanCreateGroup (slot 30) and keys
//   not there; every key of the source building table (+0xA64, sorted)
//   through slot 36 and a key not there, and slot 37 over groups
//   0x4000..0x4240. Slot 36 for 0x1402 and 0x1406 and slot 37 for groups
//   whose class is one of those (they ask the object at 0x00B43D28), the
//   reference counts and the automata counts go to the log only. Then a
//   new aircraft and a new watercraft manager from their factories (held
//   by one reference): the same queries, Pause and Resume, three map nodes
//   of the test's own in the aircraft manager's +0xA88 and nodes in the
//   watercraft manager's +0xAA0 and +0xA8C tables (nothing referenced), so
//   that the release (Shutdown, not initialized, and the deleting
//   destructor) walks them; and a bare common base (0xA88 bytes) built by
//   0x005125E0 and deleted by its slot 38 0x00512CD0. The city's managers
//   are not changed.
// OPENSC4_TEST_AUTOMATA_AWMGR=2 (part aw2): also, on the same day, at the
//   game's addresses (ours when the simulators.automata.awmgr groups are on),
//   into the fingerprint as ` B<aircraft>,<watercraft>,<vehicle>,<pedestrian>,
//   <fresh>,<cheats>,<flags>` from that day on: per city manager the common
//   base's Write 0x0050CB80 into a stream of the test's own (the version,
//   the most, the two bools and the source building table; the automata
//   count, which follows frames, to the log), and TrackAutomataGroupInstances
//   (slot 31) for the first two groups of the exemplar map that are not
//   tracked, tracked and untracked again (the AL answers and whether the
//   entry is gone; the set's size, frame-driven, to the log), and for a
//   group the map does not hold. Then a new aircraft manager from its
//   factory, held by one reference: Read 0x005108E0 of the first manager's
//   bytes (the count zeroed) and Write again, which must give the same bytes;
//   reads of versions 7 (refused), 4 and 5; SetMaxCount with -5, 30, 30 and
//   9000; Activate off and on; ReleaseAutomatonSet on an empty set of the
//   test's own; DoMessage with thirteen messages of the test's own (the
//   scale switch, the camera hold and its release, the camera moves, slots
//   48, 50, 57, 58, the null-object branches of 0x89D3E9F3 and 0xEBFF18A3, a
//   type no one handles); then the release. The group instance table's
//   insert 0x00512030 and erase 0x00510F10 on a table of the test's own (two
//   keys in one bucket, the second in the chain erased first, through
//   0x00510450), and the exemplar record copy 0x00512C80 / 0x005120E0 on two
//   records of the test's own. Then the cheat target 0x0050DD30
//   on the city's aircraft manager: another message, another cheat,
//   0xCC478B79 without a word, with the word at 0x00A82B94 and with another
//   (the flag 0x00B09968 put back after), and, when there is no route line
//   yet, 0xE97FCBC8 with the count 7 (the route line made, prepared and
//   shown) and 0xE97FCBC9 (taken away). The route line's place and the new
//   manager's camera point go to the log only.
// A remove and add round trip of one occupant's controllers is not done:
// removing sends 0xEA298D50 to the automata, destroys the controllers and
// can drop the occupant's automaton entry (its automata released), and the
// add makes new controllers through the COM; the city would not be the
// same afterwards.
#include "core/automata_awmgr.h"
#include "core/automata_ctlmgr.h"
#include "core/automata_scripts.h"
#include "core/automata_vehmgr.h"
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"

#include <cIGZIStream.h>
#include <cIGZOStream.h>
#include <cIGZString.h>
#include <cISC4City.h>

#include <opensc4/addresses.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace opensc4::autotest {
namespace {

using automata_ctlmgr::AutomatonNode;
using automata_ctlmgr::ControllerNode;
using automata_ctlmgr::Grid;
using automata_ctlmgr::HashTable;
using automata_ctlmgr::ListNode;
namespace field = automata_ctlmgr::field;
namespace tmpl = automata_ctlmgr::tmpl;
namespace controller = automata_ctlmgr::controller;

struct Hash {
    uint32_t h = 2166136261u;
    void Mix(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    }
    void MixBytes(const void* data, std::size_t size)
    {
        const auto* const b = static_cast<const uint8_t*>(data);
        for (std::size_t i = 0; i < size; ++i)
            h = (h ^ b[i]) * 16777619u;
    }
};

// The occupant's slots (cISC4Occupant): type, position (out), cells (out).
constexpr std::size_t kGetType = 0x1C, kGetPosition = 0x20, kGetCells = 0x64;
// The automata managers' list of their automata (the common base's +0x44:
// head of 0xC-byte nodes, the automaton at +8).
constexpr std::size_t kManagerAutomata = 0x44;
constexpr uint32_t kTypeAutomaton = 0xC7CC75F1, kTypeAutomaton2 = 0x496E5783, kTypeProp = 0xA823821E;

char* Manager(cISC4City* city)
{
    if (!game::IsBuild641() || !city)
        return nullptr;
    return Virtual<char*>(city, automata_ctlmgr::kCitySlotManager);
}

HashTable* Table(char* manager, std::size_t offset) { return reinterpret_cast<HashTable*>(manager + offset); }

template <typename Node>
void ForEach(HashTable* table, auto&& fn)
{
    const uint32_t buckets = static_cast<uint32_t>(table->end - table->begin);
    for (uint32_t i = 0; i < buckets; ++i)
        for (auto* node = static_cast<Node*>(table->begin[i]); node; node = node->next)
            fn(node);
}

uint32_t OccupantHash(void* occupant)
{
    Hash hash;
    if (!occupant) {
        hash.Mix(0);
        return hash.h;
    }
    const uint32_t type = Virtual<uint32_t>(occupant, kGetType);
    hash.Mix(type);
    // An automaton's cells move with it on frames: left out with its
    // position.
    if (type != kTypeAutomaton && type != kTypeAutomaton2) {
        uint8_t rect[4] = {0xFF, 0xFF, 0xFF, 0xFF};
        Virtual<uint8_t>(occupant, kGetCells, static_cast<uint8_t*>(rect));
        hash.MixBytes(rect, sizeof rect);
    }
    if (type != kTypeProp && type != kTypeAutomaton && type != kTypeAutomaton2) {
        float position[3] = {};
        Virtual<uint8_t>(occupant, kGetPosition, static_cast<float*>(position));
        hash.MixBytes(&position[0], 4);
        hash.MixBytes(&position[2], 4);
    }
    return hash.h;
}

// A controller by its kind and its template (nothing of its own state).
uint32_t ControllerHash(void* object)
{
    Hash hash;
    if (!object) {
        hash.Mix(0);
        return hash.h;
    }
    const std::uintptr_t vtable = *static_cast<std::uintptr_t*>(object);
    hash.Mix(vtable == controller::kAttractorVtable ? 1u : vtable == controller::kGeneratorVtable ? 2u : 3u);
    char* const t = Virtual<char*>(object, controller::kTemplate);
    if (!t) {
        hash.Mix(0);
        return hash.h;
    }
    hash.Mix(At<uint32_t>(t, tmpl::kClassId));
    hash.Mix(At<uint8_t>(t, tmpl::kFlags));
    hash.Mix(At<uint32_t>(t, tmpl::kRadius));
    const char* const begin = At<const char*>(t, tmpl::kNameBegin);
    const char* const end = At<const char*>(t, tmpl::kNameEnd);
    if (begin && end > begin && end - begin < 256)
        hash.MixBytes(begin, static_cast<std::size_t>(end - begin));
    return hash.h;
}

// An output stream that keeps the bytes written to it (as
// src/core/netmgr_autotest.cpp's). It answers no interface, so the game's
// SC4Stream::WriteReference writes nothing for a reference.
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
    int32_t GetError() override { return 0; }
    int32_t SetUserData(cIGZVariant*) override { return 0; }
    int32_t GetUserData() override { return 0; }

    std::vector<uint8_t> bytes;

private:
    template <typename T>
    bool Put(const T& value)
    {
        return SetVoid(&value, sizeof value);
    }
};

// An input stream over the bytes a ByteOStream kept: a read past the end
// stores nothing and leaves the stream in error. It answers no interface, so
// SC4Stream::ReadReference reads nothing from it and fails (the references
// ByteOStream wrote nothing for). A string is its length and its bytes, as
// ByteOStream::SetGZStr writes it.
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
    bool GetGZStr(cIGZString& text) override
    {
        uint32_t length = 0;
        if (!Get(length) || bytes.size() - at < length)
            return Fail();
        text.FromChar(reinterpret_cast<const char*>(bytes.data() + at), length);
        at += length;
        return true;
    }
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

// What the Write drive left for the fingerprint.
bool g_written = false;
std::string g_writeText;

uint32_t U32(const std::vector<uint8_t>& bytes, std::size_t at)
{
    uint32_t v = 0;
    if (at + 4 <= bytes.size())
        std::memcpy(&v, bytes.data() + at, 4);
    return v;
}

void WriteDrive(char* manager)
{
    ByteOStream out;
    using WriteFn = uint32_t(__thiscall*)(void* part, void* stream);
    const uint32_t answer = reinterpret_cast<WriteFn>(addr::cSC4AutomataControllerManager_Write)(
        manager + field::kSerializable, static_cast<cIGZOStream*>(&out));
    const std::vector<uint8_t>& b = out.bytes;
    // version (2), the time (4 + 2 + 4 + 2), the occupant count, its counts.
    constexpr std::size_t kHeader = 14;
    Hash hash;
    std::size_t at = kHeader;
    bool fits = b.size() >= kHeader + 4;
    // Bytes 0-1 the version; 2-13 the controller time (frame-driven, left out).
    if (fits)
        hash.MixBytes(b.data(), 2);
    const uint32_t occupants = U32(b, at);
    at += 4;
    std::vector<uint32_t> counts;
    for (uint32_t i = 0; fits && i < occupants; ++i, at += 4) {
        if (at + 4 > b.size()) {
            fits = false;
            break;
        }
        counts.push_back(U32(b, at));
    }
    std::sort(counts.begin(), counts.end());
    hash.Mix(occupants);
    for (const uint32_t c : counts)
        hash.Mix(c);
    const std::size_t first = at;
    // The automaton table: a count, then per entry a count and 25 bytes.
    const uint32_t entries = U32(b, at);
    at += 4;
    std::vector<uint32_t> records;
    for (uint32_t i = 0; fits && i < entries; ++i) {
        if (at + 4 + 25 > b.size()) {
            fits = false;
            break;
        }
        Hash record;
        record.MixBytes(b.data() + at, 4 + 25);
        records.push_back(record.h);
        at += 4 + 25;
    }
    std::sort(records.begin(), records.end());
    Hash automata;
    for (const uint32_t r : records)
        automata.Mix(r);
    char text[96];
    std::snprintf(text, sizeof text, " w%u,%u,%08X%s", answer, static_cast<unsigned>(first), hash.h,
                  fits && at == b.size() ? "" : "?");
    g_writeText = text;
    g_written = true;
    float timeA = 0, timeC = 0;
    int16_t timeB = 0, timeD = 0;
    if (b.size() >= kHeader) {
        std::memcpy(&timeA, b.data() + 2, 4);
        std::memcpy(&timeB, b.data() + 6, 2);
        std::memcpy(&timeC, b.data() + 8, 4);
        std::memcpy(&timeD, b.data() + 12, 2);
    }
    log::Write("autotest: automata: write answer %u, %u bytes, time %.9g %d %.9g %d, %u occupants, %u automaton "
               "entries (%08X)%s",
               answer, static_cast<unsigned>(b.size()), static_cast<double>(timeA), timeB,
               static_cast<double>(timeC), timeD, occupants, entries, automata.h,
               fits && at == b.size() ? "" : ", not the expected layout");
}

// What the query drive left for the fingerprint.
std::string g_queryText;

uint32_t Bits(float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, 4);
    return bits;
}

// A list head as the game makes one (12 bytes, linked to itself).
ListNode* NewHead()
{
    auto* const head =
        reinterpret_cast<ListNode*(__cdecl*)(uint32_t)>(addr::SC4_stl_allocate)(static_cast<uint32_t>(sizeof(ListNode)));
    head->next = head;
    head->prev = head;
    return head;
}

// The controllers of an answered list by kind and template, order-free.
uint32_t ListSum(ListNode* head, uint32_t& count)
{
    uint32_t sum = 0;
    for (ListNode* e = head->next; e != head; e = e->next) {
        ++count;
        sum += ControllerHash(e->value);
    }
    return sum;
}

void QueryDrive(char* manager)
{
    using ByTemplateFn = void*(__thiscall*)(void*, void*, void*);
    using ByNameFn = void*(__thiscall*)(void*, void*, void*);
    using NearFn = void*(__thiscall*)(void*, uint32_t, uint32_t, uint32_t, void*, uint32_t);
    using RelevantFn = uint8_t(__thiscall*)(void*, uint32_t, uint32_t, uint32_t, ListNode**);
    using ReleaseFn = void(__thiscall*)(void*, ListNode**);
    using EffectFn = void*(__thiscall*)(void*, void*, float*, uint32_t*, uint32_t*);
    using CommotionFn = float(__thiscall*)(void*, void*);
    struct Pick {
        uint32_t key;
        void* occupant;
        void* controller;
    };
    std::vector<Pick> picks;
    ForEach<ControllerNode>(Table(manager, field::kControllers), [&](ControllerNode* node) {
        void* const occupant = node->occupant;
        if (!occupant)
            return;
        const uint32_t type = Virtual<uint32_t>(occupant, kGetType);
        if (type == kTypeAutomaton || type == kTypeAutomaton2 || type == kTypeProp)
            return;
        const uint32_t occupantHash = OccupantHash(occupant);
        for (ListNode* e = node->head->next; e != node->head; e = e->next) {
            if (!e->value || !Virtual<void*>(e->value, controller::kTemplate))
                continue;
            Hash key;
            key.Mix(occupantHash);
            key.Mix(ControllerHash(e->value));
            picks.push_back({key.h, occupant, e->value});
        }
    });
    std::sort(picks.begin(), picks.end(), [](const Pick& a, const Pick& b) { return a.key < b.key; });
    if (picks.size() > 16)
        picks.resize(16);
    const auto byTemplate = reinterpret_cast<ByTemplateFn>(addr::cSC4AutomataControllerManager_GetControllerByTemplate);
    const auto byName = reinterpret_cast<ByNameFn>(addr::cSC4AutomataControllerManager_GetControllerByName);
    const auto nearFn = reinterpret_cast<NearFn>(addr::cSC4AutomataControllerManager_GetControllerNear);
    const auto relevant =
        reinterpret_cast<RelevantFn>(addr::cSC4AutomataControllerManager_GetRelevantControllersByWorldPos);
    const auto release = reinterpret_cast<ReleaseFn>(addr::cSC4AutomataControllerManager_ReleaseControllerList);
    const auto effect = reinterpret_cast<EffectFn>(addr::cSC4AutomataControllerManager_GetCompositeAttractionEffect);
    const auto commotion =
        reinterpret_cast<CommotionFn>(addr::cSC4AutomataControllerManager_GetCompositeAttractionCommotion);
    const auto freeFn = reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free);
    Hash answers, frames;
    uint32_t found = 0, relevantCount = 0, effects = 0, commotions = 0;
    for (const Pick& pick : picks) {
        void* const t = Virtual<void*>(pick.controller, controller::kTemplate);
        void* const name = Virtual<void*>(pick.controller, controller::kName);
        void* const first = byTemplate(manager, pick.occupant, t);
        void* const named = name ? byName(manager, pick.occupant, name) : nullptr;
        void* const none = byTemplate(manager, pick.occupant, nullptr);
        answers.Mix(pick.key);
        answers.Mix(ControllerHash(first));
        answers.Mix(ControllerHash(named));
        answers.Mix(none ? 1u : 0u);
        found += first ? 1u : 0u;
        float position[3] = {};
        Virtual<uint8_t>(pick.occupant, kGetPosition, static_cast<float*>(position));
        void* const close = name ? nearFn(manager, Bits(position[0]), Bits(position[1]), Bits(position[2]), name,
                                          Bits(64.0f))
                                 : nullptr;
        answers.Mix(ControllerHash(close));
        // Frame-driven from here: the log only.
        for (const uint32_t type : {controller::kTypeAttractor, controller::kTypeGenerator}) {
            ListNode* list = NewHead();
            const uint8_t any = relevant(manager, Bits(position[0]), Bits(position[2]), type, &list);
            uint32_t count = 0;
            frames.Mix(any);
            frames.Mix(ListSum(list, count));
            frames.Mix(count);
            relevantCount += count;
            release(manager, &list);
            freeFn(list);
        }
        float vector[3] = {};
        uint32_t value = 0, strength = 0;
        void* const strongest = effect(manager, pick.occupant, static_cast<float*>(vector), &value, &strength);
        effects += strongest ? 1u : 0u;
        frames.Mix(ControllerHash(strongest));
        frames.MixBytes(vector, sizeof vector);
        frames.Mix(value);
        frames.Mix(strength);
        void* network = nullptr;
        if (Virtual<uint8_t>(pick.occupant, 0x0, 0xA821EF94u, &network) && network) {
            frames.Mix(Bits(commotion(manager, network)));
            ++commotions;
            Virtual<uint32_t>(network, 0x8);
        }
    }
    char text[64];
    std::snprintf(text, sizeof text, " q%u,%08X", static_cast<unsigned>(picks.size()), answers.h);
    g_queryText = text;
    log::Write("autotest: automata: query %u pairs, %u found, answers %08X; relevant %u, effects %u, commotions %u "
               "(%08X)",
               static_cast<unsigned>(picks.size()), found, answers.h, relevantCount, effects, commotions, frames.h);
}

// Part a3c's drive (OPENSC4_TEST_AUTOMATA=3), see the top of the file.
std::string g_controllerText;

// The +4 serializable part's slots, and the controller's slots and fields.
namespace serial {
constexpr std::size_t kWrite = 0x0C, kRead = 0x10;
} // namespace serial
namespace ctl {
constexpr std::size_t kRelease = 0x08, kSetTemplate = 0x3C, kSetLimit = 0x4C, kPositionFromBox = 0x58;
constexpr std::size_t kResetAccumulators = 0x68, kEventB = 0x74, kEventC = 0x78, kSpawn = 0xA4;
constexpr std::size_t kInitialized = 0x08, kTemplate = 0x10, kReferences = 0x20, kNameBegin = 0x2C, kNameEnd = 0x30;
constexpr std::size_t kFlags = 0x40, kAutomata = 0x44, kCount = 0x48, k54 = 0x54, k58 = 0x58;
constexpr std::size_t kGen88 = 0x88, kGen90 = 0x90, kGen94 = 0x94, kGen98 = 0x98;
} // namespace ctl

uint32_t U16(const std::vector<uint8_t>& bytes, std::size_t at)
{
    uint16_t v = 0;
    if (at + 2 <= bytes.size())
        std::memcpy(&v, bytes.data() + at, 2);
    return v;
}

float F32(const std::vector<uint8_t>& bytes, std::size_t at)
{
    float v = 0;
    if (at + 4 <= bytes.size())
        std::memcpy(&v, bytes.data() + at, 4);
    return v;
}

uint32_t ListCount(char* controller)
{
    ListNode* const head = At<ListNode*>(controller, ctl::kAutomata);
    uint32_t n = 0;
    for (ListNode* e = head->next; e != head; e = e->next)
        ++n;
    return n;
}

// One controller's save, its round trip and the queries on the new one,
// mixed into `fixed` and `state`. False when the bytes are not laid out as
// expected.
bool SaveOne(char* c, bool generator, Hash& fixed, Hash& state, unsigned index)
{
    using QueryFn = uint8_t(__thiscall*)(void*, uint32_t, void**);
    using ClsidFn = uint32_t(__thiscall*)(void*);
    using CreateFn = char*(__cdecl*)();
    char* const part = c + 4;
    const auto query = reinterpret_cast<QueryFn>(addr::cSC4AutomataController_SerializableQueryInterface);
    for (const uint32_t iid : {0xE4FDA3D4u, 0x6A1857F8u, 0x12345678u}) {
        void* out = nullptr;
        const uint8_t answer = query(part, iid, &out);
        fixed.Mix(answer);
        fixed.Mix(out == part ? 1u : out == c ? 2u : out ? 3u : 0u);
        if (answer)
            --At<uint32_t>(c, ctl::kReferences); // the AddRef QueryInterface made
    }
    fixed.Mix(reinterpret_cast<ClsidFn>(addr::cSC4AutomataController_SerializableGetGZCLSID)(part));

    ByteOStream out;
    const uint8_t written = Virtual<uint8_t>(part, serial::kWrite, static_cast<cIGZOStream*>(&out));
    const std::vector<uint8_t>& b = out.bytes;
    // [generator: version 4] version 5, the name (length, bytes), the
    // position (12 bytes), +0x50, +0x54, +0x3C, the flags, the count; then
    // the attractor's version 1 and curve, or the generator's 20 bytes.
    const std::size_t nameAt = generator ? 4 : 2;
    const uint32_t nameLength = U32(b, nameAt);
    const std::size_t at = nameAt + 4 + nameLength;
    const std::size_t baseEnd = at + 12 + 4 * 5;
    fixed.Mix(written);
    fixed.Mix(static_cast<uint32_t>(b.size()));
    if (nameLength >= 256 || b.size() < baseEnd + (generator ? 20 : 2)) {
        fixed.MixBytes(b.data(), b.size());
        log::Write("autotest: automata: save %u: %u bytes, not the expected layout", index,
                   static_cast<unsigned>(b.size()));
        return false;
    }
    fixed.MixBytes(b.data(), nameAt + 4 + nameLength); // the versions and the name
    fixed.MixBytes(b.data() + at, 12);                 // the position
    fixed.Mix(U32(b, at + 20));                        // +0x3C
    state.Mix(U32(b, at + 12));                        // +0x50
    state.Mix(U32(b, at + 16));                        // +0x54
    state.Mix(U32(b, at + 24));                        // the flags
    state.Mix(U32(b, at + 28));                        // the count
    if (generator) {
        state.MixBytes(b.data() + baseEnd, 12); // the clock pairs
        fixed.Mix(U32(b, baseEnd + 12));        // +0x90
        state.Mix(U32(b, baseEnd + 16));        // +0x94
    } else {
        fixed.MixBytes(b.data() + baseEnd, b.size() - baseEnd); // version 1 and the curve
    }

    // The round trip through a new controller of the same class.
    const auto create = reinterpret_cast<CreateFn>(generator ? addr::cSC4AutomataGenerator_Create
                                                             : addr::cSC4AutomataAttractor_Create);
    char* const copy = create();
    uint32_t readAnswer = 0, consumed = 0, readError = 0, same = 0, nameSame = 0, limit = 0;
    uint32_t flagsAfter = 0, value54 = 0, value58 = 0, refused = 0;
    if (copy) {
        ByteIStream in(b);
        readAnswer = Virtual<uint8_t>(copy + 4, serial::kRead, static_cast<cIGZIStream*>(&in));
        consumed = static_cast<uint32_t>(in.at);
        readError = static_cast<uint32_t>(in.error);
        ByteOStream again;
        Virtual<uint8_t>(copy + 4, serial::kWrite, static_cast<cIGZOStream*>(&again));
        std::vector<uint8_t> expected(b.begin(), b.begin() + static_cast<std::ptrdiff_t>(nameAt));
        expected.insert(expected.end(), 4, 0);
        expected.insert(expected.end(), b.begin() + static_cast<std::ptrdiff_t>(at), b.end());
        same = again.bytes == expected ? 1u : 0u;
        const char* const nameBegin = At<const char*>(copy, ctl::kNameBegin);
        const char* const nameEnd = At<const char*>(copy, ctl::kNameEnd);
        nameSame = nameEnd - nameBegin == static_cast<std::ptrdiff_t>(nameLength) &&
                           std::memcmp(nameBegin, b.data() + nameAt + 4, nameLength) == 0
                       ? 1u
                       : 0u;
        Virtual<void>(copy, ctl::kSetLimit, Bits(7.5f));
        limit = At<uint32_t>(copy, ctl::k54);
        Virtual<void>(copy, ctl::kSetTemplate, At<void*>(c, ctl::kTemplate));
        Virtual<void>(copy, ctl::kResetAccumulators);
        flagsAfter = At<uint32_t>(copy, ctl::kFlags);
        value54 = At<uint32_t>(copy, ctl::k54);
        value58 = At<uint32_t>(copy, ctl::k58);
        Virtual<void>(copy, ctl::kSetTemplate, static_cast<void*>(nullptr));
        if (generator) {
            ByteIStream bad(std::vector<uint8_t>{0, 0});
            refused = Virtual<uint32_t>(copy + 4, serial::kRead, static_cast<cIGZIStream*>(&bad)) & 0xFF;
            refused |= static_cast<uint32_t>(bad.at) << 8;
        }
        Virtual<uint32_t>(copy, ctl::kRelease); // the deleting destructor
    }
    for (const uint32_t v : {readAnswer, consumed == b.size() ? 1u : 0u, readError, same, nameSame, limit, flagsAfter,
                             value54, value58, refused})
        fixed.Mix(v);

    float place[3] = {};
    Virtual<void>(c, ctl::kPositionFromBox, static_cast<float*>(place));
    fixed.MixBytes(place, sizeof place);

    log::Write("autotest: automata: save %u %s '%.*s' answer %u, %u bytes, at %.9g %.9g %.9g, 50 %.9g, 54 %.9g, "
               "3C %u, flags %08X, count %u",
               index, generator ? "generator" : "attractor", static_cast<int>(nameLength),
               reinterpret_cast<const char*>(b.data() + nameAt + 4), written, static_cast<unsigned>(b.size()),
               static_cast<double>(F32(b, at)), static_cast<double>(F32(b, at + 4)),
               static_cast<double>(F32(b, at + 8)), static_cast<double>(F32(b, at + 12)),
               static_cast<double>(F32(b, at + 16)), U32(b, at + 20), U32(b, at + 24), U32(b, at + 28));
    if (generator)
        log::Write("autotest: automata: save %u clock %.9g %d %.9g %d, 90 %08X, 94 %u", index,
                   static_cast<double>(F32(b, baseEnd)), static_cast<int>(static_cast<int16_t>(U16(b, baseEnd + 4))),
                   static_cast<double>(F32(b, baseEnd + 6)),
                   static_cast<int>(static_cast<int16_t>(U16(b, baseEnd + 10))), U32(b, baseEnd + 12),
                   U32(b, baseEnd + 16));
    log::Write("autotest: automata: save %u round trip read %u, %u of %u bytes, error %u, same %u, name %u; limit "
               "%08X, reset flags %08X 54 %08X 58 %u, refused %04X; place %.9g %.9g %.9g",
               index, readAnswer, consumed, static_cast<unsigned>(b.size()), readError, same, nameSame, limit,
               flagsAfter, value54, value58, refused, static_cast<double>(place[0]), static_cast<double>(place[1]),
               static_cast<double>(place[2]));
    return true;
}

void ControllerDrive(char* manager)
{
    struct Pick {
        uint32_t key;
        char* controller;
    };
    std::vector<Pick> attractors, generators;
    ForEach<ControllerNode>(Table(manager, field::kControllers), [&](ControllerNode* node) {
        void* const occupant = node->occupant;
        if (!occupant)
            return;
        const uint32_t type = Virtual<uint32_t>(occupant, kGetType);
        if (type == kTypeAutomaton || type == kTypeAutomaton2 || type == kTypeProp)
            return;
        const uint32_t occupantHash = OccupantHash(occupant);
        for (ListNode* e = node->head->next; e != node->head; e = e->next) {
            char* const c = static_cast<char*>(e->value);
            if (!c || !At<void*>(c, ctl::kTemplate))
                continue;
            const std::uintptr_t vtable = *reinterpret_cast<std::uintptr_t*>(c);
            if (vtable != controller::kAttractorVtable && vtable != controller::kGeneratorVtable)
                continue;
            Hash key;
            key.Mix(occupantHash);
            key.Mix(ControllerHash(c));
            (vtable == controller::kGeneratorVtable ? generators : attractors).push_back({key.h, c});
        }
    });
    for (auto* list : {&attractors, &generators})
        std::sort(list->begin(), list->end(), [](const Pick& a, const Pick& b) { return a.key < b.key; });
    Hash fixed, state;
    unsigned index = 0, odd = 0;
    for (auto* list : {&attractors, &generators})
        for (std::size_t i = 0; i < list->size() && i < 8; ++i) {
            fixed.Mix((*list)[i].key);
            if (!SaveOne((*list)[i].controller, list == &generators, fixed, state, index++))
                ++odd;
        }

    // The spawn of up to 4 initialized generators of the whole table, the
    // first of each (count kind, flags 0x80 and 0x800) first, so the spawn's
    // other branches run where a city has them; then the events with its
    // first new automaton.
    std::vector<Pick> spawners;
    std::vector<uint32_t> kinds;
    for (const int pass : {0, 1})
        for (const Pick& pick : generators) {
            if (spawners.size() == 4)
                break;
            char* const g = pick.controller;
            if (!At<uint8_t>(g, ctl::kInitialized))
                continue;
            const uint32_t kind = (At<uint32_t>(At<char*>(g, ctl::kTemplate), 0x8C) << 16) |
                                  (At<uint32_t>(g, ctl::kFlags) & 0x880u);
            const bool known = std::find(kinds.begin(), kinds.end(), kind) != kinds.end();
            const bool taken = std::find_if(spawners.begin(), spawners.end(), [&](const Pick& p) {
                                   return p.controller == g;
                               }) != spawners.end();
            if (taken || (pass == 0 && known))
                continue;
            kinds.push_back(kind);
            spawners.push_back(pick);
        }
    Hash spawn;
    unsigned spawned = 0, made = 0;
    void* first = nullptr;
    char* firstGenerator = nullptr;
    for (const Pick& pick : spawners) {
        char* const g = pick.controller;
        ++spawned;
        std::vector<void*> before;
        ListNode* const head = At<ListNode*>(g, ctl::kAutomata);
        for (ListNode* e = head->next; e != head; e = e->next)
            before.push_back(e->value);
        const uint32_t countBefore = At<uint32_t>(g, ctl::kCount);
        uint32_t day = 0;
        Virtual<void>(g, ctl::kSpawn, static_cast<void*>(&day), Bits(1.0f));
        uint32_t fresh = 0;
        for (ListNode* e = head->next; e != head; e = e->next) {
            if (std::find(before.begin(), before.end(), e->value) != before.end())
                continue;
            ++fresh;
            void* const automaton = e->value;
            if (!first) {
                first = automaton;
                firstGenerator = g;
            }
            // The list holds automaton interfaces; the type and the position
            // are the occupant's.
            uint32_t type = 0;
            float position[3] = {};
            void* occupant = nullptr;
            if (Virtual<uint8_t>(automaton, 0x0, automata_ctlmgr::kIidOccupant, &occupant) && occupant) {
                type = Virtual<uint32_t>(occupant, kGetType);
                Virtual<uint8_t>(occupant, kGetPosition, static_cast<float*>(position));
                Virtual<uint32_t>(occupant, ctl::kRelease);
            }
            spawn.Mix(type);
            spawn.MixBytes(position, sizeof position);
            log::Write("autotest: automata: spawn %u made type %08X at %.9g %.9g %.9g", spawned, type,
                       static_cast<double>(position[0]), static_cast<double>(position[1]),
                       static_cast<double>(position[2]));
        }
        made += fresh;
        spawn.Mix(pick.key);
        spawn.Mix(At<uint32_t>(g, ctl::kCount) - countBefore);
        spawn.Mix(fresh);
        for (const std::size_t offset : {ctl::kGen88, ctl::kGen90, ctl::kGen94, ctl::kGen98})
            spawn.Mix(At<uint32_t>(g, offset));
        char* const t = At<char*>(g, ctl::kTemplate);
        log::Write("autotest: automata: spawn %u: %u made (count %u to %u), 88 %u, 90 %08X, 94 %u, 98 %u; flags "
                   "%08X, kind %u",
                   spawned, fresh, countBefore, At<uint32_t>(g, ctl::kCount), At<uint32_t>(g, ctl::kGen88),
                   At<uint32_t>(g, ctl::kGen90), At<uint32_t>(g, ctl::kGen94), At<uint32_t>(g, ctl::kGen98),
                   At<uint32_t>(g, ctl::kFlags), At<uint32_t>(t, 0x8C));
    }
    if (first) {
        spawn.Mix(Virtual<uint8_t>(firstGenerator, ctl::kEventB, first, 4u));
        spawn.Mix(Virtual<uint8_t>(firstGenerator, ctl::kEventB, first, 0u));
        spawn.Mix(Virtual<uint8_t>(firstGenerator, ctl::kEventC, first, 4u));
        if (!attractors.empty()) {
            // An attractor whose template keeps its automata (+0x54), so 29
            // adds and 30 takes out; else the first.
            char* a = attractors.front().controller;
            for (const Pick& pick : attractors)
                if (At<uint32_t>(At<char*>(pick.controller, ctl::kTemplate), 0x54)) {
                    a = pick.controller;
                    break;
                }
            const uint32_t c0 = ListCount(a), n0 = At<uint32_t>(a, ctl::kCount);
            spawn.Mix(Virtual<uint8_t>(a, ctl::kEventB, first, 0u));
            const uint32_t c1 = ListCount(a), n1 = At<uint32_t>(a, ctl::kCount);
            spawn.Mix(Virtual<uint8_t>(a, ctl::kEventC, first, 0u));
            const uint32_t c2 = ListCount(a), n2 = At<uint32_t>(a, ctl::kCount);
            spawn.Mix(Virtual<uint8_t>(a, ctl::kEventC, first, 0u));
            const uint32_t c3 = ListCount(a), n3 = At<uint32_t>(a, ctl::kCount);
            for (const uint32_t v : {c1 - c0, n1 - n0, c2 - c0, n2 - n0, c3 - c0, n3 - n0})
                spawn.Mix(v);
            log::Write("autotest: automata: events: attractor list %u %u %u %u, count %u %u %u %u", c0, c1, c2, c3, n0,
                       n1, n2, n3);
        }
    }
    char text[128];
    std::snprintf(text, sizeof text, " s%u,%08X,%08X%s p%u,%u,%08X", index, fixed.h, state.h, odd ? "?" : "",
                  spawned, made, spawn.h);
    g_controllerText = text;
    log::Write("autotest: automata: controllers%s", text);
}

// ====================================================================================
// Part v1's drive (OPENSC4_TEST_AUTOMATA_VEHMGR=1), see the top of the file.

std::string g_managerText;

uint32_t Bits64(double value)
{
    uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    return static_cast<uint32_t>(bits) ^ (static_cast<uint32_t>(bits >> 32) * 31u);
}

void ManagerDrive(cISC4City* city)
{
    namespace vm = automata_vehmgr;
    std::string text;
    const uint32_t cellsX = city->CellCountX(), cellsZ = city->CellCountZ();
    for (int m = 0; m < 2; ++m) {
        char* const manager = Virtual<char*>(city, m ? vm::kCitySlotPedestrians : vm::kCitySlotVehicles);
        if (!manager) {
            text += m ? " -" : " v-";
            continue;
        }
        Hash identity, kinds, scale, cells, records, groups, rate, automata;
        // QueryInterface on the object and through the serializable part's
        // adjustor; each AddRef given back through the answer's slot 2.
        const uint32_t own = m ? vm::kPedestrianClassId : vm::kVehicleClassId;
        const uint32_t other = m ? vm::kVehicleClassId : vm::kPedestrianClassId;
        const uint32_t iids[] = {own, other, 1u, 0x12345678u};
        for (char* part : {manager, manager + vm::field::kSerializable})
            for (const uint32_t iid : iids) {
                void* out = nullptr;
                const uint8_t answer = Virtual<uint8_t>(part, 0x00, iid, &out);
                identity.Mix(answer);
                identity.Mix(out ? static_cast<uint32_t>(static_cast<char*>(out) - manager) : 0xFFFFFFFFu);
                if (answer && out)
                    Virtual<uint32_t>(out, 0x08);
            }
        identity.Mix(Virtual<uint32_t>(manager + vm::field::kSerializable, 0x14)); // GetGZCLSID
        // The kind tables (73, 74, 85, 86), the scale (87), the matrix (88)
        // with a 16-word block of the test's own, and the rate (75).
        char data[0x54] = {};
        for (int i = 0; i < 16; ++i)
            At<uint16_t>(data, 0x34 + 2 * i) = static_cast<uint16_t>(i * 37 + 5);
        for (uint32_t kind = 0; kind <= 12; ++kind) {
            kinds.Mix(Virtual<uint32_t>(manager, vm::slot::kKindClass, kind));
            kinds.Mix(Virtual<uint32_t>(manager, vm::slot::kKindGroup, kind));
            kinds.Mix(static_cast<uint32_t>(
                reinterpret_cast<std::uintptr_t>(Virtual<void*>(manager, vm::slot::kGetKindCurve, kind))));
            kinds.Mix(Bits(Virtual<float>(manager, vm::slot::kGetKindValue, kind)));
            scale.Mix(Bits64(Virtual<double>(manager, vm::slot::kGetScale, kind, 7u)));
            for (const float s : {1.25f, -0.75f}) {
                float out[16] = {};
                Virtual<void>(manager, vm::slot::kScaledMatrix, kind, static_cast<const char*>(data), s,
                              static_cast<float*>(out));
                scale.MixBytes(out, sizeof out);
            }
            rate.Mix(Bits64(Virtual<double>(manager, vm::slot::kKindRate, kind)));
        }
        // The neighbour cell test (82) and the record list's hits at 12 x 12
        // cells spread over the city, its edges included.
        const bool cellsThere = At<void*>(manager, vm::field::kCells) != nullptr;
        uint32_t recordCount = 0;
        char* const recordHead = At<char*>(manager, vm::field::kRecordList);
        for (char* n = At<char*>(recordHead, 0); n != recordHead; n = At<char*>(n, 0))
            ++recordCount;
        using Hits = uint32_t(__thiscall*)(void*, int32_t, int32_t);
        for (uint32_t i = 0; i < 12 && cellsX && cellsZ; ++i)
            for (uint32_t j = 0; j < 12; ++j) {
                const int32_t x = static_cast<int32_t>(i * (cellsX - 1) / 11);
                const int32_t z = static_cast<int32_t>(j * (cellsZ - 1) / 11);
                if (cellsThere)
                    for (uint32_t direction = 0; direction < 4; ++direction)
                        cells.Mix(Virtual<uint8_t>(manager, vm::slot::kTestNeighbourCell, 0u, x, z, direction));
                records.Mix(reinterpret_cast<Hits>(addr::cSC4VehPedManagerBase_RecordListHits)(
                    manager + vm::field::kRecordList, x, z));
            }
        // The group tests, at the game's addresses.
        using GroupTest = uint32_t(__thiscall*)(void*, uint32_t);
        for (uint32_t group = 0x4000; group <= 0x4240; ++group) {
            const uint32_t a = reinterpret_cast<GroupTest>(addr::cSC4VehPedManagerBase_IsServiceGroup)(manager, group);
            const uint32_t b = reinterpret_cast<GroupTest>(addr::cSC4VehPedManagerBase_IsGroup422C)(manager, group);
            const uint32_t c = reinterpret_cast<GroupTest>(addr::cSC4VehPedManagerBase_IsGroup422E)(manager, group);
            if (a | b | c)
                groups.Mix(group * 8 + a + 2 * b + 4 * c);
        }
        // The automata of the manager's list (+0x44; which there are follows
        // frames): their class (68), the occupant test and the bounds test
        // (41); the log only. Then both tests with null.
        using OccupantTest = uint32_t(__thiscall*)(void*, void*);
        const auto occupantTest = reinterpret_cast<OccupantTest>(addr::cSC4VehPedManagerBase_IsAutomatonOccupant);
        uint32_t count = 0;
        char* const head = At<char*>(manager, kManagerAutomata);
        for (char* n = At<char*>(head, 0); n != head && count < 64; n = At<char*>(n, 0)) {
            void* const automaton = At<void*>(n, 8);
            if (!automaton)
                continue;
            ++count;
            automata.Mix(Virtual<uint32_t>(manager, vm::slot::kAutomatonClass, automaton));
            automata.Mix(occupantTest(manager, automaton));
            automata.Mix(Virtual<uint8_t>(manager, vm::slot::kInExtendedBounds, automaton));
        }
        const uint32_t nulls =
            occupantTest(manager, nullptr) +
            2u * Virtual<uint8_t>(manager, vm::slot::kInExtendedBounds, static_cast<void*>(nullptr));
        char part[160];
        std::snprintf(part, sizeof part, " %s%08X,%08X,%08X,%08X,%u,%08X,%08X", m ? "" : "v", identity.h, kinds.h,
                      scale.h, cells.h, recordCount, records.h, groups.h);
        text += part;
        log::Write("autotest: automata: %s manager: identity %08X kinds %08X scale %08X cells %08X (%s) records %u "
                   "%08X groups %08X rate %08X nulls %u automata %u %08X",
                   m ? "pedestrian" : "vehicle", identity.h, kinds.h, scale.h, cells.h, cellsThere ? "grid" : "none",
                   recordCount, records.h, groups.h, rate.h, nulls, count, automata.h);
    }
    g_managerText = text;
}

// ====================================================================================
// Part v2's drive (OPENSC4_TEST_AUTOMATA_VEHMGR=2), see the top of the file.

void Put32(std::vector<uint8_t>& bytes, std::size_t at, uint32_t v)
{
    if (at + 4 <= bytes.size())
        std::memcpy(bytes.data() + at, &v, 4);
}

void Put16(std::vector<uint8_t>& bytes, std::size_t at, uint16_t v)
{
    if (at + 2 <= bytes.size())
        std::memcpy(bytes.data() + at, &v, 2);
}

// A cGZMessage of the type, made as the game makes one (0x00544F30), sent to
// a message target part's slot 3; AL of the answer.
uint32_t SendMessageTo(void* target, uint32_t type)
{
    using NewFn = void*(__cdecl*)(std::size_t);
    using ConstructFn = void*(__thiscall*)(void*);
    void* const raw = reinterpret_cast<NewFn>(addr::RZ_operator_new)(0x2C);
    if (!raw)
        return 0xFF;
    void* const msg = reinterpret_cast<ConstructFn>(addr::cGZMessage_cGZMessage)(raw);
    Virtual<uint32_t>(msg, 0x04);
    Virtual<uint32_t>(msg, 0x14, type);
    const uint32_t answer = Virtual<uint32_t>(target, automata_vehmgr::slot::kDoMessage, msg) & 0xFF;
    Virtual<uint32_t>(msg, 0x08);
    return answer;
}

void ManagerSaveDrive(cISC4City* city)
{
    namespace vm = automata_vehmgr;
    std::string text;
    for (int m = 0; m < 2; ++m) {
        char* const manager = Virtual<char*>(city, m ? vm::kCitySlotPedestrians : vm::kCitySlotVehicles);
        if (!manager) {
            text += m ? " -" : " x-";
            continue;
        }
        char* const part = manager + vm::field::kSerializable;
        ByteOStream out;
        const uint32_t written = Virtual<uint32_t>(part, vm::slot::kWrite, static_cast<cIGZOStream*>(&out)) & 0xFF;
        const std::vector<uint8_t>& b = out.bytes;
        // u16 3, the common base's (u16 6, s32 the most, two bools, u32 the
        // count, references (none in this stream), u32 +0xA74 and as many
        // pairs of words), u32 +0xA8C, u32 the kind, the record list (u16 1,
        // u32 the count, 49 bytes a record).
        const std::size_t pairsAt = 18;
        const uint32_t pairs = U32(b, 14);
        const std::size_t ownAt = pairsAt + 8 * static_cast<std::size_t>(pairs);
        const std::size_t listAt = ownAt + 8;
        const uint32_t records = U32(b, listAt + 2);
        const bool layout = b.size() >= listAt + 6 && b.size() == listAt + 6 + 49 * static_cast<std::size_t>(records);
        Hash fixed, table;
        fixed.Mix(written);
        fixed.Mix(layout ? 1u : 0u);
        fixed.MixBytes(b.data(), std::min<std::size_t>(b.size(), 10)); // versions, the most, the bools
        if (layout)
            fixed.MixBytes(b.data() + ownAt, b.size() - ownAt);
        if (b.size() >= ownAt)
            table.MixBytes(b.data() + pairsAt, ownAt - pairsAt);
        // The round trip into a new manager of the class, from its factory
        // (the object + 8), held by one reference. The automata count is
        // zeroed in its copy (the references read back as nothing).
        using Factory = void*(__cdecl*)();
        char* const freshPart = static_cast<char*>(
            reinterpret_cast<Factory>(m ? addr::cSC4PedestrianManager_Create : addr::cSC4VehicleManager_Create)());
        uint32_t flags = 0;
        bool same = false;
        if (freshPart && layout) {
            char* const fresh = freshPart - vm::field::kSerializable;
            Virtual<uint32_t>(fresh, 0x04);
            std::vector<uint8_t> in = b;
            Put32(in, 10, 0);
            ByteIStream reader(in);
            const uint32_t read = Virtual<uint32_t>(freshPart, vm::slot::kRead, static_cast<cIGZIStream*>(&reader)) & 0xFF;
            ByteOStream again;
            const uint32_t rewritten =
                Virtual<uint32_t>(freshPart, vm::slot::kWrite, static_cast<cIGZOStream*>(&again)) & 0xFF;
            // Read drops the saved +0xA8C: the new manager writes its own.
            std::vector<uint8_t> expect = in;
            Put32(expect, ownAt, At<uint32_t>(fresh, vm::field::kDefaultClass));
            same = again.bytes == expect;
            std::size_t firstDiff = 0;
            while (firstDiff < again.bytes.size() && firstDiff < expect.size() &&
                   again.bytes[firstDiff] == expect[firstDiff])
                ++firstDiff;
            log::Write("autotest: automata: %s manager round trip: %u bytes back of %u, first difference at %u "
                       "(the own part from %u)",
                       m ? "pedestrian" : "vehicle", static_cast<unsigned>(again.bytes.size()),
                       static_cast<unsigned>(expect.size()), static_cast<unsigned>(firstDiff),
                       static_cast<unsigned>(ownAt));
            // Reads refused or cut short: version 0; version 2 (no record
            // list); the record list's version 9.
            ByteIStream zero(std::vector<uint8_t>{0, 0});
            const uint32_t v0 = Virtual<uint32_t>(freshPart, vm::slot::kRead, static_cast<cIGZIStream*>(&zero)) & 0xFF;
            std::vector<uint8_t> two(in.begin(), in.begin() + static_cast<std::ptrdiff_t>(listAt));
            Put16(two, 0, 2);
            ByteIStream twoReader(two);
            const uint32_t v2 = Virtual<uint32_t>(freshPart, vm::slot::kRead, static_cast<cIGZIStream*>(&twoReader)) & 0xFF;
            std::vector<uint8_t> nine = in;
            Put16(nine, listAt, 9);
            ByteIStream nineReader(nine);
            const uint32_t v9 = Virtual<uint32_t>(freshPart, vm::slot::kRead, static_cast<cIGZIStream*>(&nineReader)) & 0xFF;
            // AddAutomaton with none when full (the count set to the most for
            // the call, so nothing is made), RemoveAutomaton with none.
            const uint32_t count = At<uint32_t>(fresh, vm::field::kCount);
            At<uint32_t>(fresh, vm::field::kCount) = At<uint32_t>(fresh, vm::field::kMaxCount);
            const uint32_t add =
                Virtual<uint32_t>(fresh, vm::slot::kAddAutomaton, static_cast<void*>(nullptr)) & 0xFF;
            At<uint32_t>(fresh, vm::field::kCount) = count;
            const uint32_t remove0 =
                Virtual<uint32_t>(fresh, vm::slot::kRemoveAutomaton, static_cast<void*>(nullptr), 0u) & 0xFF;
            const uint32_t remove1 =
                Virtual<uint32_t>(fresh, vm::slot::kRemoveAutomaton, static_cast<void*>(nullptr), 1u) & 0xFF;
            // Messages to its target (+0xC): a type no one handles and, for
            // the vehicle manager, 0xC9DA96E8 (its automata list is empty);
            // the vehicle manager's +4 slot 3 with a zeroed message.
            const uint32_t msgA = SendMessageTo(fresh + vm::field::kMessage, 0x12345678u);
            const uint32_t msgB = m ? 1u : SendMessageTo(fresh + vm::field::kMessage, 0xC9DA96E8u);
            uint32_t zeroMessage[3] = {};
            const uint32_t four =
                m ? 0u
                  : Virtual<uint32_t>(fresh + vm::field::kVtable4, vm::slot::kDoMessage4, static_cast<void*>(zeroMessage)) &
                        0xFF;
            flags = read | rewritten << 1 | (same ? 1u : 0u) << 2 | v0 << 3 | v2 << 4 | v9 << 5 | add << 6 |
                    remove0 << 7 | remove1 << 8 | msgA << 9 | msgB << 10 | four << 11;
            // Release: Shutdown (slot 4, not initialized) and the deleting
            // destructor.
            Virtual<uint32_t>(fresh, 0x08);
        } else if (freshPart) {
            char* const fresh = freshPart - vm::field::kSerializable;
            Virtual<uint32_t>(fresh, 0x04);
            Virtual<uint32_t>(fresh, 0x08);
        }
        // The pedestrian manager's gathering (0x00544F30) on the city's
        // manager, around its first pedestrian with distance 0 (so none is
        // sent; the sound message 0xA9E3B6E3 still goes out), and the
        // pedestrian's send (0x005448D0) called on that pedestrian with its
        // own position (it changes the pedestrian's mode: frame-driven state,
        // alike on both sides). The vehicle manager's overlay stop
        // (0x00562D60) on a zeroed object (+0xC clear: nothing). The log
        // only, but the answers' AL go into the flags.
        uint32_t live = 0;
        if (m) {
            char* const head = At<char*>(manager, kManagerAutomata);
            char* const first = At<char*>(head, 0) != head ? At<char*>(At<char*>(head, 0), 8) : nullptr;
            void* occupant = nullptr;
            if (first)
                Virtual<uint8_t>(first, 0x00, 0x26BB767Eu, &occupant);
            if (occupant) {
                using GatherFn = uint32_t(__thiscall*)(void*, void*, uint32_t, uint32_t);
                const uint32_t gathered =
                    reinterpret_cast<GatherFn>(addr::cSC4PedestrianManager_Gather)(manager, occupant, 1u, 0u) & 0xFF;
                float position[3] = {};
                Virtual<uint32_t>(first, 0x1C, static_cast<float*>(position));
                using SendFn = uint32_t(__thiscall*)(void*, const float*, uint32_t);
                const uint32_t sent =
                    reinterpret_cast<SendFn>(addr::cSC4PedestrianManager_SendTo)(first, position, 1u) & 0xFF;
                live = 1u | gathered << 1 | sent << 2;
                Virtual<uint32_t>(occupant, 0x08);
            }
        } else {
            alignas(4) char overlay[0x60] = {};
            using StopFn = void(__thiscall*)(void*);
            reinterpret_cast<StopFn>(addr::cSC4VehicleManager_OverlayStop)(overlay);
            live = 1;
        }
        flags |= live << 12;
        char partText[96];
        std::snprintf(partText, sizeof partText, " %s%08X,%X", m ? "" : "x", fixed.h, flags);
        text += partText;
        log::Write("autotest: automata: %s manager save: %u bytes written %u layout %d count %u table %u %08X kind %u "
                   "records %u fixed %08X flags %X",
                   m ? "pedestrian" : "vehicle", static_cast<unsigned>(b.size()), written, layout ? 1 : 0, U32(b, 10),
                   pairs, table.h, U32(b, ownAt + 4), records, fixed.h, flags);
    }
    g_managerText += text;
}

// ====================================================================================
// Part v3's drive (OPENSC4_TEST_AUTOMATA_VEHMGR=3), see the top of the file.

// Two building occupants of the controller table (neither automata nor
// props), the least by OccupantHash: an order that does not follow
// addresses.
void PickOccupants(char* controllers, void*& first, void*& second)
{
    first = second = nullptr;
    uint32_t h1 = 0xFFFFFFFFu, h2 = 0xFFFFFFFFu;
    if (!controllers)
        return;
    ForEach<ControllerNode>(Table(controllers, field::kControllers), [&](ControllerNode* node) {
        void* const occupant = node->occupant;
        if (!occupant)
            return;
        const uint32_t type = Virtual<uint32_t>(occupant, kGetType);
        if (type == kTypeAutomaton || type == kTypeAutomaton2 || type == kTypeProp)
            return;
        const uint32_t h = OccupantHash(occupant);
        if (h < h1 || (h == h1 && !first)) {
            second = first;
            h2 = h1;
            first = occupant;
            h1 = h;
        } else if (h < h2 && occupant != first) {
            second = occupant;
            h2 = h;
        }
    });
}

// Two network occupants (the cells' occupants, the interface 0xA821EF94 that
// slots 52, 72 and 0x0054DD50 take): the first two found at 12 x 12 places
// spread over the cells object's rectangle (its slot 0x44), in that order.
void PickNetworks(char* manager, void*& first, void*& second)
{
    first = second = nullptr;
    void* const cells = At<void*>(manager, automata_vehmgr::field::kCells);
    if (!cells)
        return;
    const int32_t* const area = Virtual<const int32_t*>(cells, 0x44);
    for (int32_t i = 0; i < 12 && !second; ++i)
        for (int32_t j = 0; j < 12 && !second; ++j) {
            const int32_t x = area[0] + i * (area[2] - area[0]) / 11;
            const int32_t z = area[1] + j * (area[3] - area[1]) / 11;
            char* const cell = Virtual<char*>(cells, 0x34, x, z);
            void* const network = cell ? At<void*>(cell, 0) : nullptr;
            if (!network || network == first)
                continue;
            (first ? second : first) = network;
        }
}

// The +0xAA8 vector's records, each object as its index in objects (0 for
// another), into the hash.
void MixRecords(Hash& hash, char* manager, void* const* objects, int count)
{
    namespace vm = automata_vehmgr;
    char* const begin = At<char*>(manager, vm::field::kRecordsBegin);
    char* const end = At<char*>(manager, vm::field::kRecordsEnd);
    hash.Mix(static_cast<uint32_t>((end - begin) / 0x1C));
    for (char* r = begin; r < end; r += 0x1C) {
        uint32_t words[7];
        std::memcpy(words, r, sizeof words);
        uint32_t index = 0;
        for (int i = 0; i < count; ++i)
            if (reinterpret_cast<void*>(static_cast<std::uintptr_t>(words[1])) == objects[i])
                index = static_cast<uint32_t>(i + 1);
        words[1] = index;
        hash.MixBytes(words, sizeof words);
    }
}

void ManagerFrameDrive(cISC4City* city)
{
    namespace vm = automata_vehmgr;
    using Factory = void*(__cdecl*)();
    std::string text;
    void* a = nullptr;
    void* b = nullptr;
    PickOccupants(Manager(city), a, b);
    for (int m = 0; m < 2; ++m) {
        char* const manager = Virtual<char*>(city, m ? vm::kCitySlotPedestrians : vm::kCitySlotVehicles);
        if (!manager) {
            text += m ? " -" : " y-";
            continue;
        }
        void* n1 = nullptr;
        void* n2 = nullptr;
        PickNetworks(manager, n1, n2);
        Hash fixed;
        uint32_t flags = 0;
        // On a new manager of the class (held by one reference): records by
        // slot 70 (none without an object), slot 55 taking them out again
        // (0x0054DB50 moves the rest down), slots 69 and 52 on its empty
        // automata list, slot 72's zero kinds and slot 88 kinds.
        char* const freshPart = static_cast<char*>(
            reinterpret_cast<Factory>(m ? addr::cSC4PedestrianManager_Create : addr::cSC4VehicleManager_Create)());
        if (freshPart && a && b && n1) {
            char* const fresh = freshPart - vm::field::kSerializable;
            Virtual<uint32_t>(fresh, 0x04);
            void* const objects[2] = {a, b};
            const uint32_t none = Virtual<uint32_t>(fresh, 0x118, 0x1111u, static_cast<void*>(nullptr), 3u, 1u, 2u,
                                                    0x10u) & 0xFF;
            uint32_t added = 0;
            added |= Virtual<uint32_t>(fresh, 0x118, 0xAu, a, 3u, 1u, 2u, 0x10u) & 0xFF;
            added |= (Virtual<uint32_t>(fresh, 0x118, 0xBu, b, 4u, 0u, 3u, 0x20u) & 0xFF) << 1;
            added |= (Virtual<uint32_t>(fresh, 0x118, 0xCu, a, 5u, 1u, 0u, 0x40u) & 0xFF) << 2;
            MixRecords(fixed, fresh, objects, 2);
            const uint32_t removeA = Virtual<uint32_t>(fresh, 0xDC, a) & 0xFF;
            MixRecords(fixed, fresh, objects, 2);
            const uint32_t removeB = Virtual<uint32_t>(fresh, 0xDC, b) & 0xFF;
            MixRecords(fixed, fresh, objects, 2);
            char* const head = At<char*>(fresh, kManagerAutomata);
            const uint32_t forget = Virtual<uint32_t>(fresh, 0x114, static_cast<void*>(nullptr));
            const uint32_t gone = Virtual<uint32_t>(fresh, 0xD0, n1);
            const uint32_t headBits = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(head));
            const uint32_t upper = ((forget ^ headBits) & 0xFFFFFF00u) == 0 ? 1u : 0u;
            const uint32_t upper2 = ((gone ^ headBits) & 0xFFFFFF00u) == 0 ? 1u : 0u;
            char data[0x54] = {};
            for (int i = 0; i < 16; ++i)
                At<uint16_t>(data, 0x34 + 2 * i) = static_cast<uint16_t>(i * 29 + 3);
            for (const uint32_t kind : {0u, 7u, 8u, 11u}) {
                float out[16];
                std::memset(out, 0xCD, sizeof out);
                Virtual<void>(fresh, 0x120, static_cast<void*>(reinterpret_cast<char*>(static_cast<std::uintptr_t>(kind))),
                              static_cast<void*>(data), kind, Bits(kind == 8 ? -0.5f : 1.5f), static_cast<float*>(out));
                fixed.MixBytes(out, sizeof out);
            }
            flags = none | added << 1 | removeA << 4 | removeB << 5 | upper << 7 |
                    (gone & 0xFF) << 8 | upper2 << 9;
            Virtual<uint32_t>(fresh, 0x08);
        } else if (freshPart) {
            char* const fresh = freshPart - vm::field::kSerializable;
            Virtual<uint32_t>(fresh, 0x04);
            Virtual<uint32_t>(fresh, 0x08);
            flags = 0x800;
        }
        // The record copy 0x0054DB50 on records of the test's own: three
        // moved down by one, and an empty range.
        {
            using CopyFn = char*(__cdecl*)(const char*, const char*, char*, void*, uint32_t);
            uint32_t buffer[7 * 4];
            for (int i = 0; i < 28; ++i)
                buffer[i] = static_cast<uint32_t>(i * 0x01010101u);
            uint32_t tag = 0;
            char* const base = reinterpret_cast<char*>(buffer);
            char* const end = reinterpret_cast<CopyFn>(addr::cSC4VehPedManagerBase_CopyRecords)(
                base + 0x1C, base + 0x70, base, &tag, 0u);
            char* const same = reinterpret_cast<CopyFn>(addr::cSC4VehPedManagerBase_CopyRecords)(
                base + 0x1C, base + 0x1C, base + 0x38, &tag, 0u);
            fixed.MixBytes(buffer, sizeof buffer);
            flags |= (end == base + 0x54 ? 1u : 0u) << 10 | (same == base + 0x38 ? 1u : 0u) << 11;
        }
        // The tuning map's insert and lookup (0x00555350, 0x00555400) on a
        // map of the test's own: two records, both found, a third key not
        // asked (it would add a record of stack leftovers in the game).
        if (m == 0) {
            using InsertFn = char*(__thiscall*)(void*, const uint32_t*);
            using AtFn = char*(__thiscall*)(void*, const uint32_t*);
            alignas(4) char map[0x14] = {};
            uint32_t pairs[2][9] = {};
            for (int i = 0; i < 2; ++i) {
                pairs[i][0] = 0x1234u + 97u * static_cast<uint32_t>(i);
                for (int j = 1; j < 5; ++j)
                    pairs[i][j] = Bits(static_cast<float>(i * 4 + j) * 0.25f);
            }
            char* const in0 = reinterpret_cast<InsertFn>(addr::cSC4VehPedManagerBase_MapInsert)(map, pairs[0]);
            char* const in1 = reinterpret_cast<InsertFn>(addr::cSC4VehPedManagerBase_MapInsert)(map, pairs[1]);
            const uint32_t key0 = pairs[0][0], key1 = pairs[1][0];
            char* const at0 = reinterpret_cast<AtFn>(addr::cSC4VehPedManagerBase_MapAt)(map, &key0);
            char* const at1 = reinterpret_cast<AtFn>(addr::cSC4VehPedManagerBase_MapAt)(map, &key1);
            const uint32_t count = At<uint32_t>(map, 0x10);
            const uint32_t buckets = static_cast<uint32_t>((At<char*>(map, 8) - At<char*>(map, 4)) / 4);
            fixed.Mix(count);
            fixed.Mix(buckets);
            if (in0 && in1) {
                fixed.MixBytes(in0, 0x24);
                fixed.MixBytes(in1, 0x24);
            }
            flags |= (at0 == in0 + 4 ? 1u : 0u) << 12 | (at1 == in1 + 4 ? 1u : 0u) << 13;
            // Freed as the game frees its nodes (the curves are empty).
            for (uint32_t i = 0; i < buckets; ++i)
                for (char* node = At<char**>(map, 4)[i]; node;) {
                    char* const next = At<char*>(node, 0);
                    if (At<void*>(node, 0x18))
                        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(At<void*>(node, 0x18));
                    reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(node);
                    node = next;
                }
            if (At<void*>(map, 4))
                reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(At<void*>(map, 4));
            // The game's tuning map (0x00B21D60, loaded with the tuning):
            // each key's record by the lookup, read-only.
            char* const tuning = reinterpret_cast<char*>(0x00B21D60);
            const uint32_t tuningBuckets = static_cast<uint32_t>((At<char*>(tuning, 8) - At<char*>(tuning, 4)) / 4);
            uint32_t keys = 0, sum = 0, found = 1;
            for (uint32_t i = 0; i < tuningBuckets && i < 4096; ++i)
                for (char* node = At<char**>(tuning, 4)[i]; node; node = At<char*>(node, 0)) {
                    const uint32_t key = At<uint32_t>(node, 4);
                    char* const record = reinterpret_cast<AtFn>(addr::cSC4VehPedManagerBase_MapAt)(tuning, &key);
                    found &= record == node + 8 ? 1u : 0u;
                    Hash h;
                    h.Mix(key);
                    h.MixBytes(node + 8, 0x10);
                    h.Mix(At<uint32_t>(node, 0x24)); // the curve's count
                    sum += h.h;
                    ++keys;
                }
            fixed.Mix(keys);
            fixed.Mix(sum);
            flags |= found << 14;
        }
        // On the city's manager, the log only (which automata there are,
        // the cells' contents and the manager's generator follow frames):
        // GetRandomCreationPoint (slot 16) at four places spread over the
        // city, with the generator (+0x50) seeded for the call and put back
        // after; the occupant's position (0x0054DD50) and slot 72 for kinds
        // 1 to 10 at the two occupants; then AddAutomaton with none (the
        // branch that makes one from the class's pool) while there is room,
        // and the one made (the list's last) removed again (slot 8, 1).
        Hash live;
        uint32_t made = 0, removed = 0;
        {
            constexpr std::size_t kGenerator = 0x50, kGeneratorSize = 0x9C8;
            std::vector<uint8_t> saved(manager + kGenerator, manager + kGenerator + kGeneratorSize);
            reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(addr::cRZRandomSuper_Seed)(manager + kGenerator, 12345u);
            const uint32_t cellsX = city->CellCountX(), cellsZ = city->CellCountZ();
            for (uint32_t i = 0; i < 4 && cellsX && cellsZ; ++i) {
                const float position[3] = {static_cast<float>((i % 2 ? 3 : 1) * cellsX * 4), 0.0f,
                                           static_cast<float>((i / 2 ? 3 : 1) * cellsZ * 4)};
                float out[3] = {};
                const uint32_t answer =
                    Virtual<uint32_t>(manager, 0x40, m ? 0x4001u : 0x4002u, static_cast<const float*>(position),
                                      Bits(0.0f), Bits(200.0f), static_cast<float*>(out), 0u, 0u) & 0xFF;
                live.Mix(answer);
                live.MixBytes(out, sizeof out);
            }
            std::memcpy(manager + kGenerator, saved.data(), kGeneratorSize);
            using PositionFn = void*(__thiscall*)(void*, uint32_t, void*, uint32_t, float*);
            for (void* occupant : {n1, n2}) {
                if (!occupant)
                    continue;
                float out[3] = {};
                void* const point = reinterpret_cast<PositionFn>(addr::cSC4VehPedManagerBase_OccupantPosition)(
                    manager, m ? 0x4001u : 0x4002u, occupant, 0u, out);
                live.Mix(point ? 1u : 0u);
                live.MixBytes(out, sizeof out);
                for (uint32_t kind = 1; kind <= 10; ++kind) {
                    if (kind == 7 || kind == 8)
                        continue;
                    float table[16] = {};
                    Virtual<void>(manager, 0x120, occupant, 0u, kind, Bits(1.0f), static_cast<float*>(table));
                    live.MixBytes(table, sizeof table);
                }
            }
            if (At<uint32_t>(manager, vm::field::kCount) < At<uint32_t>(manager, vm::field::kMaxCount)) {
                char* const list = At<char*>(manager, kManagerAutomata);
                char* const lastBefore = At<char*>(list, 4);
                made = Virtual<uint32_t>(manager, vm::slot::kAddAutomaton, static_cast<void*>(nullptr)) & 0xFF;
                char* const last = At<char*>(list, 4);
                if (made && last != lastBefore && last != list)
                    removed = Virtual<uint32_t>(manager, vm::slot::kRemoveAutomaton, At<void*>(last, 8), 1u) & 0xFF;
            }
            // The frame work the harness's saves leave off (+0x4C, the
            // manager's own generation, is clear in them, and they have no
            // records): the kind sweep (slot 76) once per kind of the list,
            // with the generator seeded for it and put back after (the
            // stopwatch is on simulation time, so a sweep goes through the
            // whole rectangle); then records as slot 53 makes them for the
            // first building occupant of the controller table with the
            // property (0xE9AEE3DA / 0xE9AEE3DC; for the vehicle manager also
            // the drawn record 0x4206), spent by slot 84 with a time of 1,
            // and what is left of them released and cleared.
            const uint32_t before = At<uint32_t>(manager, vm::field::kCount);
            std::memcpy(saved.data(), manager + kGenerator, kGeneratorSize);
            reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(addr::cRZRandomSuper_Seed)(manager + kGenerator, 54321u);
            uint32_t kinds = 0;
            char* const kindHead = At<char*>(manager, vm::field::kKinds);
            for (char* n = At<char*>(kindHead, 0); n != kindHead && kinds < 16; n = At<char*>(n, 0))
                ++kinds;
            // The sweep's gates, to the log: the day or night (0x00B43DC4's
            // slot 0x58), the kinds' mask (0x00B21D5C) and each kind's slot 42.
            char gates[160];
            int at = std::snprintf(gates, sizeof gates, "light %u mask %X",
                                   Virtual<uint32_t>(*reinterpret_cast<void**>(0x00B43DC4), 0x58),
                                   *reinterpret_cast<const uint32_t*>(0x00B21D5C));
            for (char* n = At<char*>(kindHead, 0); n != kindHead && at > 0 && at < 140; n = At<char*>(n, 0))
                at += std::snprintf(gates + at, sizeof gates - static_cast<std::size_t>(at), " %u:%.3g", At<uint32_t>(n, 8),
                                    Virtual<double>(manager, 0xA8, At<uint32_t>(n, 8)));
            // Each call stops after five reads of its stopwatch (the
            // harness's runs on simulation time and counts each read): it is
            // called again until the kind's walk is through (the cursor back
            // to -1), at most 40000 calls in all.
            const uint8_t generation = At<uint8_t>(manager, 0x4C);
            At<uint8_t>(manager, 0x4C) = 1;
            uint32_t calls = 0;
            for (uint32_t i = 0; i < kinds; ++i)
                do {
                    Virtual<uint32_t>(manager, 0x130, Bits(1.0f));
                } while (++calls < 40000 && At<int32_t>(manager, vm::field::kA9C) >= 0);
            const uint32_t swept = At<uint32_t>(manager, vm::field::kCount);
            void* holder = nullptr;
            const uint32_t property = m ? 0xE9AEE3DCu : 0xE9AEE3DAu;
            uint32_t bestHash = 0xFFFFFFFFu;
            ForEach<ControllerNode>(Table(Manager(city), field::kControllers), [&](ControllerNode* node) {
                void* const occupant = node->occupant;
                if (!occupant)
                    return;
                const uint32_t type = Virtual<uint32_t>(occupant, kGetType);
                if (type == kTypeAutomaton || type == kTypeAutomaton2 || type == kTypeProp)
                    return;
                void* const properties = Virtual<void*>(occupant, 0x18);
                if (!properties || !Virtual<void*>(properties, 0x24, property))
                    return;
                const uint32_t h = OccupantHash(occupant);
                if (h < bestHash) {
                    bestHash = h;
                    holder = occupant;
                }
            });
            uint32_t records = 0;
            if (holder) {
                void* const data = Virtual<void*>(Virtual<void*>(Virtual<void*>(holder, 0x18), 0x24, property), 0x14);
                const uint32_t* const ids = Virtual<const uint32_t*>(data, 0x19C);
                const uint32_t count = Virtual<uint32_t>(data, 0x10);
                for (uint32_t i = 0; i < count && i < 8; ++i)
                    records += Virtual<uint32_t>(manager, 0x118, ids[i], holder, 1u, m ? 0u : 1u, m ? 2u : 5u, 2u) & 0xFF;
                if (!m)
                    records += Virtual<uint32_t>(manager, 0x118, 0x4206u, holder, 1u, 0u, 8u, 2u) & 0xFF;
                Virtual<uint32_t>(manager, 0x150, Bits(1.0f));
            }
            char* const begin = At<char*>(manager, vm::field::kRecordsBegin);
            const uint32_t left = static_cast<uint32_t>((At<char*>(manager, vm::field::kRecordsEnd) - begin) / 0x1C);
            for (char* r = begin; r < At<char*>(manager, vm::field::kRecordsEnd); r += 0x1C)
                if (void* const object = At<void*>(r, 4))
                    Virtual<uint32_t>(object, 0x08);
            At<char*>(manager, vm::field::kRecordsEnd) = begin;
            At<uint8_t>(manager, 0x4C) = generation;
            std::memcpy(manager + kGenerator, saved.data(), kGeneratorSize);
            // Slot 53 on a new manager with the holder's building interface
            // (what the building's completion message 0x2977AE2F carries; its
            // slot 0xC gives the occupant back): its records, hashed with the
            // occupant as index 1, then released and cleared; room for eight
            // made for the call (+0x3C), put back after.
            // Without an occupant of the property, the first building
            // occupant (slot 53 then makes no record).
            void* const building = holder ? holder : a;
            char* const freshPart2 = building ? static_cast<char*>(reinterpret_cast<Factory>(
                                                   m ? addr::cSC4PedestrianManager_Create : addr::cSC4VehicleManager_Create)())
                                            : nullptr;
            if (freshPart2) {
                char* const fresh = freshPart2 - vm::field::kSerializable;
                Virtual<uint32_t>(fresh, 0x04);
                // The holder's building interface (cISC4BuildingOccupant,
                // 0x87DDA3A9), the reference given back after.
                void* lot = nullptr;
                Virtual<uint8_t>(building, 0x00, 0x87DDA3A9u, &lot);
                if (lot && Virtual<void*>(lot, 0x0C) != building)
                    flags |= 1u << 20;
                if (lot && Virtual<void*>(lot, 0x0C) == building) {
                    const uint32_t most = At<uint32_t>(fresh, vm::field::kMaxCount);
                    At<uint32_t>(fresh, vm::field::kMaxCount) = 8;
                    const uint32_t added = Virtual<uint32_t>(fresh, 0xD4, lot) & 0xFF;
                    At<uint32_t>(fresh, vm::field::kMaxCount) = most;
                    void* const objects[1] = {building};
                    MixRecords(fixed, fresh, objects, 1);
                    char* const fb = At<char*>(fresh, vm::field::kRecordsBegin);
                    for (char* r = fb; r < At<char*>(fresh, vm::field::kRecordsEnd); r += 0x1C)
                        if (void* const object = At<void*>(r, 4))
                            Virtual<uint32_t>(object, 0x08);
                    At<char*>(fresh, vm::field::kRecordsEnd) = fb;
                    flags |= (1u | added << 1) << 16;
                }
                if (lot)
                    Virtual<uint32_t>(lot, 0x08);
                Virtual<uint32_t>(fresh, 0x08);
            }
            log::Write("autotest: automata: %s manager frames: %u kinds swept (%u calls), automata %u -> %u -> %u, %u records "
                       "(%u left), holder %d; %s",
                       m ? "pedestrian" : "vehicle", kinds, calls, before, swept, At<uint32_t>(manager, vm::field::kCount),
                       records, left, holder ? 1 : 0, gates);
        }
        char partText[96];
        std::snprintf(partText, sizeof partText, " %s%08X,%X", m ? "" : "y", fixed.h, flags);
        text += partText;
        log::Write("autotest: automata: %s manager frames: fixed %08X flags %X live %08X made %u removed %u "
                   "occupants %d networks %d",
                   m ? "pedestrian" : "vehicle", fixed.h, flags, live.h, made, removed, (a ? 1 : 0) + (b ? 1 : 0),
                   (n1 ? 1 : 0) + (n2 ? 1 : 0));
    }
    g_managerText += text;
}

// ====================================================================================
// Part s1's drive (OPENSC4_TEST_AUTOMATA_SCRIPTS=1), see the top of the file.

std::string g_scriptText;

// An SC4String of the test's own (vtable 0x00A80810, the text in a buffer
// from the game's allocator, as the game makes them).
struct TestString {
    std::uintptr_t vtable = addr::SC4String_Vtable;
    char* begin = nullptr;
    char* end = nullptr;
    char* capacity = nullptr;
    uint32_t extra = 0;
    explicit TestString(const char* text)
    {
        const std::size_t length = std::strlen(text);
        using Allocate = void*(__cdecl*)(uint32_t);
        begin = static_cast<char*>(
            reinterpret_cast<Allocate>(addr::SC4_stl_allocate)(static_cast<uint32_t>(length + 1)));
        std::memcpy(begin, text, length + 1);
        end = begin + length;
        capacity = begin + length + 1;
    }
    ~TestString() { reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(begin); }
    TestString(const TestString&) = delete;
    TestString& operator=(const TestString&) = delete;
};

uint32_t NameHash(const char* begin, const char* end)
{
    Hash h;
    if (begin && end >= begin && end - begin < 512)
        h.MixBytes(begin, static_cast<std::size_t>(end - begin));
    return h.h;
}

// The nodes of a script system hash map (the field: +4 the buckets' begin,
// +8 their end), in bucket order.
template <typename Fn>
void ForEachScriptNode(char* table, Fn&& fn)
{
    char** const begin = At<char**>(table, 4);
    char** const end = At<char**>(table, 8);
    for (char** b = begin; b < end; ++b)
        for (char* node = *b; node; node = At<char*>(node, 0))
            fn(node);
}

// A group's own values and its queries at the game's addresses.
uint32_t GroupHash(char* g)
{
    namespace sg = automata_scripts::group;
    Hash h;
    if (!g) {
        h.Mix(0);
        return h.h;
    }
    using Get = uint32_t(__thiscall*)(void*);
    using Test = uint8_t(__thiscall*)(void*, uint32_t);
    using Find = uint8_t(__thiscall*)(void*, const void*, uint32_t*);
    const auto isIn = reinterpret_cast<Test>(addr::cSC4OccupantGroupTemplate_IsInGroup);
    char* const parent = At<char*>(g, sg::kParent);
    h.Mix(At<uint32_t>(g, sg::kId));
    h.Mix(At<uint32_t>(g, sg::kClass));
    h.Mix(At<uint32_t>(g, sg::kPercentage));
    h.Mix(At<uint16_t>(g, sg::kWord4C) | static_cast<uint32_t>(At<uint16_t>(g, sg::kWord4E)) << 16);
    h.Mix(parent ? At<uint32_t>(parent, sg::kId) : 0xFFFFFFFFu);
    h.Mix(reinterpret_cast<Get>(addr::cSC4OccupantGroupTemplate_Depth)(g));
    h.Mix(reinterpret_cast<Get>(addr::cSC4OccupantGroupTemplate_GetClass)(g));
    h.Mix(isIn(g, At<uint32_t>(g, sg::kId)) | isIn(g, parent ? At<uint32_t>(parent, sg::kId) : 0u) << 1 |
          isIn(g, 0x4000u) << 2);
    // FindName on the group's own name map, each name and one not there;
    // the answers summed (the map's order does not count). A node stands
    // for an SC4String: its key is at +4, where the lookups read a name's.
    const auto find = reinterpret_cast<Find>(addr::cSC4OccupantGroupTemplate_FindName);
    uint32_t names = 0, sum = 0;
    ForEachScriptNode(g + sg::kNames, [&](char* node) {
        uint32_t out = 0;
        const uint8_t found = find(g, node, &out);
        Hash e;
        e.Mix(NameHash(At<const char*>(node, 4), At<const char*>(node, 8)));
        e.Mix(found);
        e.Mix(out == At<uint32_t>(node, 0x10) ? 1u : 0u);
        sum += e.h;
        ++names;
    });
    TestString missing("opensc4 no such name");
    uint32_t out = 0;
    h.Mix(names);
    h.Mix(sum);
    h.Mix(find(g, &missing, &out));
    return h.h;
}

void ScriptsDrive(cISC4City* city)
{
    namespace ss = automata_scripts;
    char* const scripts = Virtual<char*>(city, 0xC0); // GetAutomataScriptSystem
    if (!scripts) {
        g_scriptText = " S-";
        return;
    }
    Hash identity, maps, groups, matches, fresh;
    // QueryInterface on the object and through the adjustors of its +4, +8
    // and +0xC parts; each AddRef given back through the answer's slot 2.
    const uint32_t iids[] = {ss::kScriptSystemIid, 1u, 0x452294AAu, 0xE98B2F57u, 0xE4FDA3D4u, 0x12345678u};
    for (const std::size_t at : {std::size_t{0}, ss::field::kVtable4, ss::field::kVtable8, ss::field::kSerializable})
        for (const uint32_t iid : iids) {
            void* out = nullptr;
            const uint8_t answer = Virtual<uint8_t>(scripts + at, 0x00, iid, &out);
            identity.Mix(answer);
            identity.Mix(out ? static_cast<uint32_t>(static_cast<char*>(out) - scripts) : 0xFFFFFFFFu);
            if (answer && out)
                Virtual<uint32_t>(out, 0x08);
        }
    identity.Mix(Virtual<uint32_t>(scripts + ss::field::kSerializable, 0x14)); // GetGZCLSID
    const uint32_t ids[] = {0x87163754u, 0u};
    for (const uint32_t& id : ids)
        identity.Mix(Virtual<uint8_t>(scripts + ss::field::kVtable8, 0x0C, &id));
    identity.Mix(At<uint8_t>(scripts, ss::field::kInitialized));
    // The map getters (slots 5, 8, 9, 10, 15, 18), as offsets.
    for (const std::size_t s : {0x14u, 0x20u, 0x24u, 0x28u, 0x3Cu, 0x48u})
        maps.Mix(static_cast<uint32_t>(Virtual<char*>(scripts, s) - scripts));
    // The name map: each name by slot 12 and slot 7, each group's hash.
    uint32_t named = 0, namedSum = 0;
    ForEachScriptNode(scripts + ss::field::kGroupsByName, [&](char* node) {
        char* const g = At<char*>(node, 0x10);
        Hash e;
        e.Mix(NameHash(At<const char*>(node, 4), At<const char*>(node, 8)));
        e.Mix(Virtual<char*>(scripts, 0x30, static_cast<void*>(node)) == g ? 1u : 0u);
        char* const automata = Virtual<char*>(scripts, 0x1C, static_cast<void*>(node));
        e.Mix(automata == g ? 1u : automata ? 2u : 0u);
        e.Mix(GroupHash(g));
        namedSum += e.h;
        ++named;
    });
    maps.Mix(named);
    maps.Mix(namedSum);
    uint32_t automataCount = 0;
    ForEachScriptNode(scripts + ss::field::kAutomataGroups, [&](char*) { ++automataCount; });
    maps.Mix(automataCount);
    // The ID map: each ID by slots 11 and 6, three IDs not there; the groups
    // kept for Matches, sorted by ID.
    std::vector<std::pair<uint32_t, char*>> byId;
    uint32_t idSum = 0;
    ForEachScriptNode(scripts + ss::field::kGroupsById, [&](char* node) {
        const uint32_t id = At<uint32_t>(node, 4);
        char* const g = At<char*>(node, 8);
        Hash e;
        e.Mix(id);
        e.Mix(Virtual<char*>(scripts, 0x2C, id) == g ? 1u : 0u);
        e.Mix(Virtual<char*>(scripts, 0x18, id) == g ? 1u : 0u);
        e.Mix(GroupHash(g));
        idSum += e.h;
        byId.emplace_back(id, g);
    });
    maps.Mix(static_cast<uint32_t>(byId.size()));
    maps.Mix(idSum);
    for (const uint32_t id : {0u, 0xFFFFFFFFu, 0x7A5A0000u})
        maps.Mix(Virtual<uint32_t>(scripts, 0x2C, id) | Virtual<uint32_t>(scripts, 0x18, id));
    // The special filter and track building lists (0xC-byte nodes, the
    // value at +8), in order.
    for (const std::size_t at : {ss::field::kSpecialGroups, ss::field::kTrackGroups}) {
        char* const head = At<char*>(scripts, at);
        uint32_t count = 0;
        for (char* n = At<char*>(head, 0); n != head && count < 4096; n = At<char*>(n, 0)) {
            ++count;
            maps.Mix(at == ss::field::kSpecialGroups ? GroupHash(At<char*>(n, 8)) : At<uint32_t>(n, 8));
        }
        maps.Mix(count);
    }
    // The drive commands (slot 14): each key and two not there.
    uint32_t drives = 0, driveSum = 0;
    ForEachScriptNode(scripts + ss::field::kDriveCommands, [&](char* node) {
        Hash e;
        e.Mix(At<uint32_t>(node, 4));
        e.Mix(At<uint32_t>(node, 8));
        e.Mix(Virtual<uint32_t>(scripts, 0x38, At<uint32_t>(node, 4)));
        driveSum += e.h;
        ++drives;
    });
    maps.Mix(drives);
    maps.Mix(driveSum);
    maps.Mix(Virtual<uint32_t>(scripts, 0x38, 0x7A5A0000u));
    maps.Mix(Virtual<uint32_t>(scripts, 0x38, 0xFFFFFFFFu));
    // The controller templates by name (slot 17).
    uint32_t templates = 0, templateSum = 0;
    ForEachScriptNode(scripts + ss::field::kTemplates, [&](char* node) {
        char* const t = At<char*>(node, 0x10);
        Hash e;
        e.Mix(NameHash(At<const char*>(node, 4), At<const char*>(node, 8)));
        e.Mix(Virtual<char*>(scripts, 0x44, static_cast<void*>(node)) == t ? 1u : 0u);
        e.Mix(t ? At<uint32_t>(t, tmpl::kClassId) : 0u);
        templateSum += e.h;
        ++templates;
    });
    maps.Mix(templates);
    maps.Mix(templateSum);
    TestString missing("opensc4 no such name");
    maps.Mix(Virtual<uint32_t>(scripts, 0x44, static_cast<void*>(&missing)) |
             Virtual<uint32_t>(scripts, 0x30, static_cast<void*>(&missing)) |
             Virtual<uint32_t>(scripts, 0x1C, static_cast<void*>(&missing)));
    groups.Mix(named);
    groups.Mix(namedSum ^ idSum);
    // Matches (slot 3) of up to 16 groups for up to 8 occupants.
    std::sort(byId.begin(), byId.end());
    if (byId.size() > 16)
        byId.resize(16);
    std::vector<std::pair<uint32_t, void*>> occupants;
    if (char* const manager = Manager(city))
        ForEach<ControllerNode>(Table(manager, field::kControllers), [&](ControllerNode* node) {
            if (!node->occupant)
                return;
            const uint32_t type = Virtual<uint32_t>(node->occupant, kGetType);
            if (type == kTypeAutomaton || type == kTypeAutomaton2 || type == kTypeProp)
                return;
            occupants.emplace_back(OccupantHash(node->occupant), node->occupant);
        });
    std::sort(occupants.begin(), occupants.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    if (occupants.size() > 8)
        occupants.resize(8);
    uint32_t matched = 0, matchedId = 0;
    for (const auto& [hash, occupant] : occupants)
        for (const auto& [id, g] : byId) {
            const uint8_t m = Virtual<uint8_t>(g, ss::slot::kGroupMatches, occupant);
            matches.Mix(m);
            matched += m;
            if (m && !matchedId)
                matchedId = id;
        }
    matches.Mix(static_cast<uint32_t>(occupants.size()));
    // A fresh script system from the factory, held by one reference.
    using Factory = char*(__cdecl*)();
    char* const made = reinterpret_cast<Factory>(addr::cSC4AutomataScriptSystem_Create)();
    uint32_t flags = 0;
    if (made) {
        Virtual<uint32_t>(made, 0x04);
        void* out = nullptr;
        const uint8_t qi = Virtual<uint8_t>(made, 0x00, ss::kScriptSystemIid, &out);
        if (qi && out)
            Virtual<uint32_t>(out, 0x08);
        TestString nameA("opensc4 test a"), nameB("opensc4 test b"), nameC("opensc4 test c");
        char* const a =
            Virtual<char*>(made, ss::slot::kCreateOccupantGroup, static_cast<void*>(&nameA), 0x7A5A0001u, 1u);
        char* const b = Virtual<char*>(made, ss::slot::kCreateOccupantGroup, static_cast<void*>(&nameB), 0u, 0u);
        char* const c =
            Virtual<char*>(made, ss::slot::kCreateOccupantGroup, static_cast<void*>(&nameC), 0x7A5A0003u, 0u);
        char* const again =
            Virtual<char*>(made, ss::slot::kCreateOccupantGroup, static_cast<void*>(&nameA), 0x7A5A0009u, 1u);
        flags = qi | (a ? 1u : 0u) << 1 | (b ? 1u : 0u) << 2 | (c ? 1u : 0u) << 3 | (again ? 1u : 0u) << 4;
        if (a && b && c) {
            At<char*>(c, ss::group::kParent) = a;
            At<uint32_t>(a, ss::group::kClass) = 0x12345u;
            At<float>(c, ss::group::kPercentage) = 0.5f;
            fresh.Mix(Virtual<char*>(made, 0x30, static_cast<void*>(&nameA)) == a ? 1u : 0u);
            fresh.Mix(Virtual<char*>(made, 0x30, static_cast<void*>(&nameB)) == b ? 1u : 0u);
            fresh.Mix(Virtual<char*>(made, 0x1C, static_cast<void*>(&nameA)) == a ? 1u : 0u);
            fresh.Mix(Virtual<char*>(made, 0x1C, static_cast<void*>(&nameC)) ? 1u : 0u);
            fresh.Mix(Virtual<char*>(made, 0x2C, 0x7A5A0003u) == c ? 1u : 0u);
            fresh.Mix(Virtual<char*>(made, 0x18, 0x7A5A0001u) == a ? 1u : 0u);
            fresh.Mix(Virtual<uint32_t>(made, 0x2C, 0x7A5A0009u));
            fresh.Mix(Virtual<uint32_t>(made, 0x38, 5u));
            char* const head = At<char*>(made, ss::field::kSpecialGroups);
            fresh.Mix(At<char*>(head, 0) != head && At<char*>(At<char*>(head, 0), 8) == b ? 1u : 0u);
            fresh.Mix(GroupHash(a));
            fresh.Mix(GroupHash(b));
            fresh.Mix(GroupHash(c));
            // Matches of the third group (50 percent) with the ID of a city
            // group that matched above, so that it draws; its own ID back
            // after.
            if (matchedId) {
                At<uint32_t>(c, ss::group::kId) = matchedId;
                for (const auto& [hash, occupant] : occupants)
                    fresh.Mix(Virtual<uint8_t>(c, ss::slot::kGroupMatches, occupant));
                At<uint32_t>(c, ss::group::kId) = 0x7A5A0003u;
            }
        }
        using Life = void(__thiscall*)(void*);
        reinterpret_cast<Life>(addr::cSC4AutomataScriptSystem_ReleaseTemplates)(made);
        reinterpret_cast<Life>(addr::cSC4AutomataScriptSystem_ReleaseGroups)(made);
        char* const head = At<char*>(made, ss::field::kSpecialGroups);
        fresh.Mix(At<char*>(head, 0) == head ? 1u : 0u);
        fresh.Mix(At<uint32_t>(made + ss::field::kGroupsByName, 0x10) |
                  At<uint32_t>(made + ss::field::kGroupsById, 0x10));
        fresh.Mix(Virtual<uint32_t>(made, 0x08)); // Release: Shutdown (not initialized), the destructor body
    }
    fresh.Mix(flags);
    char text[128];
    std::snprintf(text, sizeof text, " S%08X,%08X,%08X,%08X,%08X", identity.h, maps.h, groups.h, matches.h, fresh.h);
    g_scriptText = text;
    log::Write("autotest: automata: scripts: identity %08X maps %08X (names %u automata %u ids %u drives %u "
               "templates %u) groups %08X matches %08X (%u of %u x %u) fresh %08X flags %X",
               identity.h, maps.h, named, automataCount, static_cast<unsigned>(byId.size()), drives, templates,
               groups.h, matches.h, matched, static_cast<unsigned>(occupants.size()),
               static_cast<unsigned>(byId.size()), fresh.h, flags);
}

// Part s2's drive (OPENSC4_TEST_AUTOMATA_SCRIPTS=2), see the top of the file.

// A template's own values: its kind by vtable, class (+0x18, +0x1C), +0x20,
// +0x34 and name.
uint32_t TemplateHash(char* t)
{
    Hash h;
    if (!t) {
        h.Mix(0);
        return h.h;
    }
    const std::uintptr_t vtable = At<std::uintptr_t>(t, 0);
    h.Mix(vtable == addr::cSC4AttractorTemplate_Vtable ? 1u : vtable == addr::cSC4GeneratorTemplate_Vtable ? 2u : 3u);
    h.Mix(At<uint32_t>(t, 0x18));
    h.Mix(At<uint32_t>(t, 0x1C));
    h.Mix(At<uint32_t>(t, 0x20));
    h.Mix(At<uint32_t>(t, 0x34));
    h.Mix(NameHash(At<const char*>(t, 0x08), At<const char*>(t, 0x0C)));
    return h.h;
}

constexpr std::size_t kRecord = 0x7C;

// A test vector of behaviour records (0x0050BD40), keyed so that the
// comparator is a strict weak order (the float +4 and the uint32 +0xC from
// one rank, ties included), the index at +0x28.
char* NewRecords(uint32_t count, uint32_t seed)
{
    using Allocate = void*(__cdecl*)(uint32_t);
    using Construct = void*(__thiscall*)(void*);
    char* const block =
        static_cast<char*>(reinterpret_cast<Allocate>(addr::SC4_stl_allocate)(count * static_cast<uint32_t>(kRecord)));
    for (uint32_t i = 0; i < count; ++i) {
        char* const r = block + i * kRecord;
        reinterpret_cast<Construct>(addr::SC4BehaviourRecord_Construct)(r);
        const uint32_t rank = (i * 7919u + seed) % (count / 2 + 3);
        At<float>(r, 4) = static_cast<float>(rank) * 0.5f;
        At<uint32_t>(r, 0xC) = 1000u - rank;
        At<uint32_t>(r, 0x28) = i;
    }
    return block;
}

uint32_t RecordsHash(const char* first, uint32_t count)
{
    Hash h;
    for (uint32_t i = 0; i < count; ++i) {
        char* const r = const_cast<char*>(first) + i * kRecord;
        h.Mix(At<uint32_t>(r, 4));
        h.Mix(At<uint32_t>(r, 0xC));
        h.Mix(At<uint32_t>(r, 0x28));
        h.Mix(At<uint32_t>(r, 0x40));
    }
    return h.h;
}

void FreeRecords(char* first, char* last)
{
    uint8_t tag = 0;
    reinterpret_cast<void(__cdecl*)(char*, char*, void*)>(addr::cSC4AutomataControllerTemplate_DestroyRecords)(
        first, last, &tag);
    reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(first);
}

void ScriptsDrive2(cISC4City* city)
{
    namespace ss = automata_scripts;
    char* const scripts = Virtual<char*>(city, 0xC0);
    if (!scripts) {
        g_scriptText += " T-";
        return;
    }
    Hash templates, helpers, records;
    // The city's templates (static after the load), summed.
    uint32_t count = 0, sum = 0;
    std::vector<std::string> names;
    ForEachScriptNode(scripts + ss::field::kTemplates, [&](char* node) {
        sum += TemplateHash(At<char*>(node, 0x10));
        ++count;
        const char* const b = At<const char*>(node, 4);
        const char* const e = At<const char*>(node, 8);
        if (b && e > b && e - b < 128)
            names.emplace_back(b, e);
    });
    templates.Mix(count);
    templates.Mix(sum);
    std::sort(names.begin(), names.end());
    std::string listed;
    for (std::size_t i = 0; i < names.size() && i < 6; ++i)
        listed += (i ? ", " : "") + names[i];
    log::Write("autotest: automata: scripts: templates %u, first names %s", count, listed.c_str());
    // On a fresh script system: CreateControllerTemplate (slot 16) for an
    // attractor and a generator, the first name again (refused), a class
    // with no template (the factory's refusal); then the templates
    // destroyed by ReleaseTemplates and the object released.
    using Factory = char*(__cdecl*)();
    char* const made = reinterpret_cast<Factory>(addr::cSC4AutomataScriptSystem_Create)();
    if (made) {
        Virtual<uint32_t>(made, 0x04);
        TestString first("opensc4 test template 1"), second("opensc4 test template 2");
        char* const a = Virtual<char*>(made, 0x40, static_cast<void*>(&first), 0x6A1628BAu);
        char* const g = Virtual<char*>(made, 0x40, static_cast<void*>(&second), 0x4A1628C5u);
        char* const again = Virtual<char*>(made, 0x40, static_cast<void*>(&first), 0x4A1628C5u);
        TestString third("opensc4 test template 3");
        char* const none = Virtual<char*>(made, 0x40, static_cast<void*>(&third), 0x12345678u);
        templates.Mix(TemplateHash(a));
        templates.Mix(TemplateHash(g));
        templates.Mix((again ? 1u : 0u) | (none ? 2u : 0u));
        templates.Mix(Virtual<char*>(made, 0x44, static_cast<void*>(&second)) == g ? 1u : 0u);
        using Life = void(__thiscall*)(void*);
        reinterpret_cast<Life>(addr::cSC4AutomataScriptSystem_ReleaseTemplates)(made);
        templates.Mix(At<uint32_t>(made + ss::field::kTemplates, 0x10));
        Virtual<uint32_t>(made, 0x08);
    }
    // The helpers on the city's groups (the ID map's, sorted by ID): the
    // cached ID list (0x0051CD90; it fills an empty cache, alike on both
    // sides) and the record lookup (0x005204D0) for every key byte into a
    // three-string record (0x0051D040, 0x0051D0D0).
    std::vector<std::pair<uint32_t, char*>> byId;
    ForEachScriptNode(scripts + ss::field::kGroupsById,
                      [&](char* node) { byId.emplace_back(At<uint32_t>(node, 4), At<char*>(node, 8)); });
    std::sort(byId.begin(), byId.end());
    using Cache = void*(__thiscall*)(void*);
    using Find = uint8_t(__thiscall*)(void*, uint32_t, void*);
    using Record3 = void*(__thiscall*)(void*);
    uint32_t found = 0;
    for (const auto& [id, g] : byId) {
        void* const part = reinterpret_cast<Cache>(addr::cSC4OccupantGroupTemplate_CacheIds)(g);
        helpers.Mix(static_cast<uint32_t>(static_cast<char*>(part) - g));
        helpers.Mix(Virtual<uint32_t>(part, 0x10));
        alignas(4) char record[0x40] = {};
        reinterpret_cast<Record3>(addr::cSC4OccupantGroupTemplate_Record3Construct)(record);
        for (uint32_t key = 0; key < 256; ++key) {
            const uint8_t hit = reinterpret_cast<Find>(addr::cSC4OccupantGroupTemplate_FindRecord)(g, key, record);
            if (!hit)
                continue;
            ++found;
            helpers.Mix(key);
            for (const std::size_t at : {std::size_t{0}, std::size_t{0x14}, std::size_t{0x28}})
                helpers.Mix(NameHash(At<const char*>(record, at + 4), At<const char*>(record, at + 8)));
            helpers.Mix(At<uint32_t>(record, 0x3C));
        }
        reinterpret_cast<Record3>(addr::cSC4OccupantGroupTemplate_Record3Destroy)(record);
    }
    helpers.Mix(found);
    // The behaviour records: std::sort (0x00522800) of 40 records (the
    // introsort's partition, median and unguarded insertion run above 16),
    // partial_sort (0x00522590: the heap functions) of 40 with a middle of
    // 20, and the full vector's insert (0x00521350) of 3 copies in the
    // middle and of one at the end.
    using Sort = void(__cdecl*)(char*, char*, uint32_t);
    using PartialSort = void(__cdecl*)(char*, char*, char*, uint32_t, uint32_t);
    char* const sorted = NewRecords(40, 11);
    reinterpret_cast<Sort>(addr::SC4BehaviourRecord_Sort)(sorted, sorted + 40 * kRecord, 0u);
    records.Mix(RecordsHash(sorted, 40));
    FreeRecords(sorted, sorted + 40 * kRecord);
    char* const partial = NewRecords(40, 5);
    reinterpret_cast<PartialSort>(addr::SC4BehaviourRecord_PartialSort)(partial, partial + 20 * kRecord,
                                                                       partial + 40 * kRecord, 0u, 0u);
    records.Mix(RecordsHash(partial, 40));
    FreeRecords(partial, partial + 40 * kRecord);
    using Insert = void(__thiscall*)(void*, char*, const char*, uint32_t, uint32_t, uint32_t);
    for (const uint32_t atEnd : {0u, 1u}) {
        char* vector[3];
        vector[0] = NewRecords(5, 3 + atEnd);
        vector[1] = vector[0] + 5 * kRecord;
        vector[2] = vector[1];
        char* const value = NewRecords(1, 9);
        At<uint32_t>(value, 0x40) = 0x1234u + atEnd;
        char* const position = atEnd ? vector[1] : vector[0] + 2 * kRecord;
        reinterpret_cast<Insert>(addr::SC4BehaviourRecord_VectorInsertOverflow)(vector, position, value, 0u,
                                                                               atEnd ? 1u : 3u, atEnd);
        const uint32_t size = static_cast<uint32_t>((vector[1] - vector[0]) / static_cast<std::ptrdiff_t>(kRecord));
        records.Mix(size);
        records.Mix(static_cast<uint32_t>((vector[2] - vector[0]) / static_cast<std::ptrdiff_t>(kRecord)));
        records.Mix(RecordsHash(vector[0], size));
        FreeRecords(vector[0], vector[1]);
        FreeRecords(value, value + kRecord);
    }
    char text[96];
    std::snprintf(text, sizeof text, " T%08X,%08X,%08X", templates.h, helpers.h, records.h);
    g_scriptText += text;
    log::Write("autotest: automata: scripts 2: templates %08X helpers %08X (%u groups, %u records found) records %08X",
               templates.h, helpers.h, static_cast<unsigned>(byId.size()), found, records.h);
}

// Part s3a's drive (OPENSC4_TEST_AUTOMATA_SCRIPTS=3), see the top of the file.

void MixIdList(Hash& h, char* head)
{
    uint32_t count = 0;
    for (char* n = At<char*>(head, 0); n != head && count < 4096; n = At<char*>(n, 0), ++count)
        h.Mix(At<uint32_t>(n, 8));
    h.Mix(count);
}

void MixIdVector(Hash& h, char* vector)
{
    const uint32_t* const begin = At<const uint32_t*>(vector, 0);
    const uint32_t* const end = At<const uint32_t*>(vector, 4);
    uint32_t count = 0;
    for (const uint32_t* p = begin; p && p < end && count < 4096; ++p, ++count)
        h.Mix(*p);
    h.Mix(count);
}

// A curve's points (SC4Curve: the point vector's begin and end at +0, +4).
void MixCurve(Hash& h, char* curve)
{
    const uint32_t* const begin = At<const uint32_t*>(curve, 0);
    const uint32_t* const end = At<const uint32_t*>(curve, 4);
    uint32_t count = 0;
    for (const uint32_t* p = begin; p && p < end && count < 4096; ++p, ++count)
        h.Mix(*p);
    h.Mix(count);
}

// A behaviour record's fields (not its pointers).
uint32_t BehaviourHash(char* r)
{
    Hash h;
    for (const std::size_t at : {0x0u, 0x4u, 0x8u, 0xCu, 0x28u, 0x30u, 0x34u, 0x38u, 0x3Cu, 0x40u})
        h.Mix(At<uint32_t>(r, at));
    h.Mix(At<uint8_t>(r, 0x2C));
    for (std::size_t at = 0x44; at <= 0x49; ++at)
        h.Mix(At<uint8_t>(r, at));
    for (std::size_t at = 0x4C; at <= 0x78; at += 4)
        h.Mix(At<uint32_t>(r, at));
    // The anims (SC4Strings of 0x14 bytes) and the sound IDs.
    char* const sb = At<char*>(r, 0x10);
    char* const se = At<char*>(r, 0x14);
    uint32_t strings = 0;
    for (char* s = sb; s && s < se && strings < 256; s += 0x14, ++strings)
        h.Mix(NameHash(At<const char*>(s, 4), At<const char*>(s, 8)));
    h.Mix(strings);
    MixIdVector(h, r + 0x1C);
    return h.h;
}

// Everything the loading writes into a template.
uint32_t LoadedTemplateHash(char* t)
{
    Hash h;
    h.Mix(TemplateHash(t));
    for (const std::size_t at : {0x4Cu, 0x50u, 0x54u, 0x58u, 0x5Cu})
        h.Mix(At<uint32_t>(t, at));
    MixCurve(h, t + 0x24);
    MixIdList(h, At<char*>(t, 0x44));
    MixIdList(h, At<char*>(t, 0x48));
    MixIdVector(h, t + 0x60);
    MixIdVector(h, t + 0x6C);
    const uint32_t classId = At<uint32_t>(t, 0x18);
    if (classId == 0x6A1628BAu || classId == 0x4A1628C5u) {
        MixIdVector(h, t + 0x78);
        uint32_t names = 0;
        char* const head = At<char*>(t, 0x84);
        for (char* n = At<char*>(head, 0); n != head && names < 4096; n = At<char*>(n, 0))
            ++names;
        h.Mix(names);
    }
    if (classId == 0x6A1628BAu)
        MixCurve(h, t + 0x88);
    if (classId == 0x4A1628C5u) {
        for (std::size_t at = 0x88; at <= 0xAC; at += 4)
            h.Mix(At<uint32_t>(t, at));
        h.Mix(At<uint8_t>(t, 0xB0));
    }
    char* const rb = At<char*>(t, 0x38);
    char* const re = At<char*>(t, 0x3C);
    uint32_t records = 0;
    for (char* r = rb; r && r < re && records < 1024; r += kRecord, ++records)
        h.Mix(BehaviourHash(r));
    h.Mix(records);
    return h.h;
}

// What the loading writes into a group (not the parts filled at run time).
uint32_t LoadedGroupHash(char* g)
{
    namespace sg = automata_scripts::group;
    Hash h;
    char* const parent = At<char*>(g, sg::kParent);
    h.Mix(At<uint32_t>(g, sg::kId));
    h.Mix(At<uint32_t>(g, sg::kClass));
    h.Mix(At<uint32_t>(g, sg::kPercentage));
    h.Mix(parent ? At<uint32_t>(parent, sg::kId) : 0xFFFFFFFFu);
    h.Mix(At<uint16_t>(g, sg::kWord4C) | static_cast<uint32_t>(At<uint16_t>(g, sg::kWord4E)) << 16);
    MixIdList(h, At<char*>(g, sg::kList50));
    h.Mix(At<uint32_t>(g + sg::kNames, 0x10));
    h.Mix(At<uint32_t>(g + sg::kRecords, 0x10));
    // Part s3b's fields: the controller templates (+0x30, in order), the
    // models (+0x48, three IDs a node), the trailers (+0x54, SC4Strings at
    // +8 of 0x1C-byte nodes), the anims (+0x34, name -> value) and the key
    // effects (+0x70, byte key -> three strings and flags) summed, the
    // speeds, the drive parameter and driving blocks (raw; the allocations
    // are zeroed under determinism.zero_alloc), the image and the source
    // building.
    uint32_t count = 0;
    char* head = At<char*>(g, sg::kList30);
    for (char* n = At<char*>(head, 0); n != head && count < 4096; n = At<char*>(n, 0), ++count)
        h.Mix(At<char*>(n, 8) ? TemplateHash(At<char*>(n, 8)) : 0u);
    h.Mix(count);
    count = 0;
    head = At<char*>(g, sg::kList48);
    for (char* n = At<char*>(head, 0); n != head && count < 4096; n = At<char*>(n, 0), ++count)
        for (const std::size_t at : {0x8u, 0xCu, 0x10u})
            h.Mix(At<uint32_t>(n, at));
    h.Mix(count);
    count = 0;
    head = At<char*>(g, sg::kList54);
    for (char* n = At<char*>(head, 0); n != head && count < 4096; n = At<char*>(n, 0), ++count)
        h.Mix(NameHash(At<const char*>(n, 0xC), At<const char*>(n, 0x10)));
    h.Mix(count);
    uint32_t sum = 0;
    ForEachScriptNode(g + sg::kNames, [&](char* node) {
        Hash e;
        e.Mix(NameHash(At<const char*>(node, 4), At<const char*>(node, 8)));
        e.Mix(At<uint32_t>(node, 0x10));
        sum += e.h;
    });
    h.Mix(sum);
    sum = 0;
    ForEachScriptNode(g + sg::kRecords, [&](char* node) {
        Hash e;
        e.Mix(At<uint8_t>(node, 4));
        for (const std::size_t at : {0x8u, 0x1Cu, 0x30u})
            e.Mix(NameHash(At<const char*>(node, at + 4), At<const char*>(node, at + 8)));
        e.Mix(At<uint32_t>(node, 0x44));
        sum += e.h;
    });
    h.Mix(sum);
    for (const std::size_t at : {0x58u, 0x5Cu, 0x68u, 0x6Cu})
        h.Mix(At<uint32_t>(g, at));
    struct Block {
        std::size_t at, size;
    };
    for (const Block b : {Block{0x60, 0xA0}, Block{0x64, 0x94}}) {
        const uint32_t* const block = At<const uint32_t*>(g, b.at);
        h.Mix(block ? 1u : 0u);
        for (std::size_t i = 0; block && i < b.size / 4; ++i)
            h.Mix(block[i]);
    }
    return h.h;
}

// A script system's loaded state: the templates and groups by name (summed),
// the drive commands (summed), the track building list (in order).
uint32_t LoadedSystemHash(char* system, uint32_t& templates, uint32_t& groups, uint32_t& drives)
{
    namespace ss = automata_scripts;
    Hash h;
    uint32_t sum = 0;
    templates = 0;
    ForEachScriptNode(system + ss::field::kTemplates, [&](char* node) {
        Hash e;
        e.Mix(NameHash(At<const char*>(node, 4), At<const char*>(node, 8)));
        e.Mix(LoadedTemplateHash(At<char*>(node, 0x10)));
        sum += e.h;
        ++templates;
    });
    h.Mix(templates);
    h.Mix(sum);
    sum = 0;
    groups = 0;
    ForEachScriptNode(system + ss::field::kGroupsByName, [&](char* node) {
        Hash e;
        e.Mix(NameHash(At<const char*>(node, 4), At<const char*>(node, 8)));
        e.Mix(LoadedGroupHash(At<char*>(node, 0x10)));
        sum += e.h;
        ++groups;
    });
    h.Mix(groups);
    h.Mix(sum);
    sum = 0;
    drives = 0;
    ForEachScriptNode(system + ss::field::kDriveCommands, [&](char* node) {
        Hash e;
        e.Mix(At<uint32_t>(node, 4));
        e.Mix(At<uint32_t>(node, 8));
        sum += e.h;
        ++drives;
    });
    h.Mix(drives);
    h.Mix(sum);
    MixIdList(h, At<char*>(system, ss::field::kTrackGroups));
    return h.h;
}

void ScriptsDrive3(cISC4City* city)
{
    char* const scripts = Virtual<char*>(city, 0xC0);
    if (!scripts) {
        g_scriptText += " L-";
        return;
    }
    uint32_t cityTemplates = 0, cityGroups = 0, cityDrives = 0;
    const uint32_t cityHash = LoadedSystemHash(scripts, cityTemplates, cityGroups, cityDrives);
    // A fresh script system from the factory with the scripts loaded into it
    // (LoadScripts at the game's address), then its templates and groups
    // released and the object released.
    using Factory = char*(__cdecl*)();
    using Life = void(__thiscall*)(void*);
    using Load = uint8_t(__thiscall*)(void*);
    char* const made = reinterpret_cast<Factory>(addr::cSC4AutomataScriptSystem_Create)();
    uint32_t freshHash = 0, loaded = 0xFF, freshTemplates = 0, freshGroups = 0, freshDrives = 0;
    if (made) {
        Virtual<uint32_t>(made, 0x04);
        loaded = reinterpret_cast<Load>(addr::cSC4AutomataScriptSystem_LoadScripts)(made);
        freshHash = LoadedSystemHash(made, freshTemplates, freshGroups, freshDrives);
        reinterpret_cast<Life>(addr::cSC4AutomataScriptSystem_ReleaseTemplates)(made);
        reinterpret_cast<Life>(addr::cSC4AutomataScriptSystem_ReleaseGroups)(made);
        Virtual<uint32_t>(made, 0x08);
    }
    char text[96];
    std::snprintf(text, sizeof text, " L%08X,%08X,%X", cityHash, freshHash, loaded);
    g_scriptText += text;
    log::Write("autotest: automata: scripts 3: city %08X (%u templates, %u groups, %u drives) fresh %08X (%u, %u, %u) "
               "load %u",
               cityHash, cityTemplates, cityGroups, cityDrives, freshHash, freshTemplates, freshGroups, freshDrives,
               loaded);
}

// ====================================================================================
// Part aw1's drive (OPENSC4_TEST_AUTOMATA_AWMGR=1), see the top of the file.

std::string g_awText;

// The keys of a std::map's subtree (null children), in order.
void TreeKeys(char* node, std::vector<uint32_t>& keys)
{
    namespace aw = automata_awmgr;
    while (node && keys.size() < 4096) {
        TreeKeys(At<char*>(node, aw::tree::kLeft), keys);
        keys.push_back(At<uint32_t>(node, aw::tree::kKey));
        node = At<char*>(node, aw::tree::kRight);
    }
}

// The class slot 37 (GetSourceBuildingCountForAutomataGroup) passes on for
// a group ID, as the game finds it; 0xFFFFFFFF for none.
uint32_t GroupClassOf(char* manager, uint32_t id)
{
    namespace aw = automata_awmgr;
    void* const scripts = At<void*>(manager, aw::field::kScripts);
    if (!scripts)
        return 0xFFFFFFFFu;
    char* const group = Virtual<char*>(scripts, 0x18, id);
    if (!group)
        return 0xFFFFFFFFu;
    if (const uint32_t c = At<uint32_t>(group, 0x6C))
        return c;
    if (char* const parent = At<char*>(group, 0xC))
        return reinterpret_cast<uint32_t(__thiscall*)(void*)>(addr::cSC4AutomataManager_GroupClass)(parent);
    return 0;
}

// One of the city's four managers; the identity and the queries into
// `hash`, the frame- and source-dependent values into `logged`.
void AwQuery(char* manager, Hash& hash, Hash& logged, uint32_t& flags)
{
    namespace aw = automata_awmgr;
    const uint32_t classes[] = {aw::kAircraftClassId, aw::kWatercraftClassId, automata_vehmgr::kVehicleClassId,
                                automata_vehmgr::kPedestrianClassId};
    const uint32_t iids[] = {1u, aw::kManagerIid, aw::kSerializableIid, aw::kMessageIid, 0x12345678u, 0xF0000001u};
    for (const std::size_t at : {aw::field::kVtable, aw::field::kVtable4, aw::field::kSerializable, aw::field::kMessage}) {
        char* const part = manager + at;
        auto ask = [&](uint32_t iid) {
            void* out = nullptr;
            const uint32_t answer = Virtual<uint32_t>(part, aw::slot::kQueryInterface, iid, &out) & 0xFF;
            hash.Mix(answer);
            hash.Mix(out ? static_cast<uint32_t>(static_cast<char*>(out) - manager) : 0xFFFFFFFFu);
            if (answer && out)
                Virtual<uint32_t>(out, aw::slot::kRelease);
        };
        for (const uint32_t iid : classes)
            ask(iid);
        for (const uint32_t iid : iids)
            ask(iid);
    }
    // The count: AddRef and Release give it back.
    const uint32_t count = At<uint32_t>(manager, aw::field::kRefCount);
    const uint32_t up = Virtual<uint32_t>(manager, aw::slot::kAddRef);
    const uint32_t down = Virtual<uint32_t>(manager, aw::slot::kRelease);
    hash.Mix(up - count);
    if (down != count)
        flags |= 1;
    logged.Mix(count);
    hash.Mix(Virtual<uint32_t>(manager + aw::field::kSerializable, aw::slot::kGetGZCLSID));
    using Get = uint32_t(__thiscall*)(void*);
    hash.Mix(reinterpret_cast<Get>(addr::cSC4AutomataManager_GetGZCLSID)(manager + aw::field::kSerializable));
    hash.Mix(Virtual<uint8_t>(manager, aw::slot::kIsActive));
    hash.Mix(Virtual<uint8_t>(manager, aw::slot::kIsPaused));
    hash.Mix(Virtual<uint8_t>(manager, aw::slot::kIsDrivingMode));
    hash.Mix(Virtual<uint32_t>(manager, aw::slot::kGetMinZoom));
    hash.Mix(Virtual<uint8_t>(manager, aw::slot::kIsCameraFollowUpdating));
    hash.Mix(static_cast<uint32_t>(Virtual<char*>(manager, aw::slot::kGetExemplarData) - manager));
    hash.Mix(static_cast<uint32_t>(Virtual<char*>(manager, aw::slot::kGetExemplarDataMap) - manager));
    using Dispatch = uint32_t(__thiscall*)(void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    hash.Mix(reinterpret_cast<Dispatch>(addr::cSC4AutomataManager_Dispatch5)(manager, 0, 0, 0, 0, 0));
    // The exemplar vector: each record found by its own key (slot 44) and a
    // key not there.
    char* const begin = At<char*>(manager, aw::field::kExemplarsBegin);
    char* const end = At<char*>(manager, aw::field::kExemplarsEnd);
    const uint32_t records = begin ? static_cast<uint32_t>((end - begin) / aw::exemplar::kSize) : 0;
    hash.Mix(records);
    for (uint32_t i = 0; i < records && i < 512; ++i) {
        const uint32_t* const key = &At<uint32_t>(begin + i * aw::exemplar::kSize, aw::exemplar::kKey);
        hash.MixBytes(key, 12);
        char* const found = Virtual<char*>(manager, aw::slot::kFindExemplar, key);
        hash.Mix(found ? static_cast<uint32_t>((found - begin) / aw::exemplar::kSize) : 0xFFFFFFFFu);
    }
    const uint32_t none[3] = {0x7A5A0001u, 0x7A5A0002u, 0x7A5A0003u};
    hash.Mix(Virtual<char*>(manager, aw::slot::kFindExemplar, static_cast<const uint32_t*>(none)) ? 1u : 0u);
    // The exemplar map: every key (slot 30) and keys not there.
    std::vector<uint32_t> keys;
    char* const header = At<char*>(manager, aw::field::kExemplarMap);
    TreeKeys(At<char*>(header, aw::tree::kParent), keys);
    hash.Mix(At<uint32_t>(manager, aw::field::kExemplarMapSize));
    hash.Mix(static_cast<uint32_t>(keys.size()));
    for (const uint32_t key : keys) {
        hash.Mix(key);
        hash.Mix(Virtual<uint32_t>(manager, aw::slot::kCanCreateGroup, key));
        hash.Mix(Virtual<uint32_t>(manager, aw::slot::kCanCreateGroup, key ^ 0x7A5A0000u));
    }
    hash.Mix(Virtual<uint32_t>(manager, aw::slot::kCanCreateGroup, 0u));
    hash.Mix(Virtual<uint32_t>(manager, aw::slot::kCanCreateGroup, 0xFFFFFFFFu));
    // The source building counts: every key of the table (slot 36, sorted)
    // and keys not there; slot 37 over groups 0x4000..0x4240. Counts that
    // come from the object at 0x00B43D28 (0x1402 and 0x1406) go to the log.
    std::vector<std::pair<uint32_t, uint32_t>> counts;
    char* const t = manager + aw::field::kSourceCounts;
    for (char** b = At<char**>(t, aw::table::kBegin); b && b != At<char**>(t, aw::table::kEnd); ++b)
        for (char* node = *b; node && counts.size() < 4096; node = At<char*>(node, 0))
            counts.emplace_back(At<uint32_t>(node, 4), At<uint32_t>(node, 8));
    std::sort(counts.begin(), counts.end());
    hash.Mix(static_cast<uint32_t>(counts.size()));
    for (const auto& [key, value] : counts) {
        hash.Mix(key);
        hash.Mix(value);
        if (key != 0x1402 && key != 0x1406)
            hash.Mix(Virtual<uint32_t>(manager, aw::slot::kGetSourceBuildingCount, key));
    }
    hash.Mix(Virtual<uint32_t>(manager, aw::slot::kGetSourceBuildingCount, 0x7A5A0001u));
    for (const uint32_t special : {0x1402u, 0x1406u})
        logged.Mix(Virtual<uint32_t>(manager, aw::slot::kGetSourceBuildingCount, special));
    for (uint32_t id = 0x4000; id <= 0x4240; ++id) {
        const uint32_t c = GroupClassOf(manager, id);
        const uint32_t answer = Virtual<uint32_t>(manager, aw::slot::kGetSourceBuildingCountForGroup, id);
        if (c == 0x1402 || c == 0x1406) {
            logged.Mix(id);
            logged.Mix(answer);
        } else if (answer || c != 0xFFFFFFFFu) {
            hash.Mix(id);
            hash.Mix(c);
            hash.Mix(answer);
        }
    }
    logged.Mix(At<uint32_t>(manager, aw::field::kCount));
}

void AwManagerDrive(cISC4City* city)
{
    namespace aw = automata_awmgr;
    std::string text = " A";
    Hash logged;
    uint32_t flags = 0;
    const std::size_t slots[] = {aw::kCitySlotAircraft, aw::kCitySlotWatercraft, automata_vehmgr::kCitySlotVehicles,
                                 automata_vehmgr::kCitySlotPedestrians};
    const char* const names[] = {"aircraft", "watercraft", "vehicle", "pedestrian"};
    for (int m = 0; m < 4; ++m) {
        char* const manager = Virtual<char*>(city, slots[m]);
        char part[16];
        if (!manager) {
            text += m ? ",-" : "-";
            continue;
        }
        Hash hash;
        AwQuery(manager, hash, logged, flags);
        std::snprintf(part, sizeof part, "%s%08X", m ? "," : "", hash.h);
        text += part;
        log::Write("autotest: automata: %s manager (aw1): %08X refs %u automata %u exemplars %u map %u counts %u",
                   names[m], hash.h, At<uint32_t>(manager, aw::field::kRefCount), At<uint32_t>(manager, aw::field::kCount),
                   static_cast<unsigned>((At<char*>(manager, aw::field::kExemplarsEnd) -
                                          At<char*>(manager, aw::field::kExemplarsBegin)) /
                                         aw::exemplar::kSize),
                   At<uint32_t>(manager, aw::field::kExemplarMapSize),
                   At<uint32_t>(manager + aw::field::kSourceCounts, aw::table::kCount));
    }
    // New managers from the two factories, held by one reference, with
    // nodes of the test's own in their containers so that the destructors'
    // clears walk them; then released (Shutdown, not initialized, and the
    // deleting destructor). And a bare common base built and destroyed by
    // its slot 38.
    Hash fresh;
    using Factory = void*(__cdecl*)();
    using Alloc = void*(__cdecl*)(uint32_t);
    const auto allocate = reinterpret_cast<Alloc>(addr::SC4_stl_allocate);
    for (int m = 0; m < 2; ++m) {
        void* const serial =
            reinterpret_cast<Factory>(m ? addr::cSC4WatercraftManager_Create : addr::cSC4AircraftManager_Create)();
        if (!serial) {
            flags |= 2;
            continue;
        }
        char* const made = static_cast<char*>(serial) - aw::field::kSerializable;
        fresh.Mix(Virtual<uint32_t>(made, aw::slot::kAddRef));
        Hash ignored;
        AwQuery(made, fresh, ignored, flags);
        fresh.Mix(At<uint32_t>(made, aw::field::kGroup));
        fresh.Mix(At<uint32_t>(made, aw::field::kAutomatonClass));
        fresh.Mix(At<uint32_t>(made, aw::field::kA40));
        Virtual<void>(made, aw::slot::kPause);
        fresh.Mix(Virtual<uint8_t>(made, aw::slot::kIsPaused));
        Virtual<void>(made, aw::slot::kResume);
        fresh.Mix(Virtual<uint8_t>(made, aw::slot::kIsPaused));
        if (m == 0) {
            // Three map nodes (a root with two children; nothing in them
            // is referenced) for the map clear and its erase.
            char* const header = At<char*>(made, aw::field::kAirMap);
            char* nodes[3];
            for (char*& n : nodes) {
                n = static_cast<char*>(allocate(0x18));
                std::memset(n, 0, 0x18);
            }
            At<char*>(nodes[0], aw::tree::kParent) = header;
            At<char*>(nodes[0], aw::tree::kLeft) = nodes[1];
            At<char*>(nodes[0], aw::tree::kRight) = nodes[2];
            At<char*>(nodes[1], aw::tree::kParent) = nodes[0];
            At<char*>(nodes[2], aw::tree::kParent) = nodes[0];
            At<char*>(header, aw::tree::kParent) = nodes[0];
            At<char*>(header, aw::tree::kLeft) = nodes[1];
            At<char*>(header, aw::tree::kRight) = nodes[2];
            At<uint32_t>(made, aw::field::kAirMapSize) = 3;
        } else {
            // Two nodes in the first bucket of +0xAA0 and one in +0xA8C,
            // nothing referenced, for the two table clears.
            for (const std::size_t at : {aw::field::kWaterTable2, aw::field::kWaterTable}) {
                char* const tb = made + at;
                char** const buckets = At<char**>(tb, aw::table::kBegin);
                if (!buckets || buckets == At<char**>(tb, aw::table::kEnd)) {
                    flags |= 4;
                    continue;
                }
                const int n = at == aw::field::kWaterTable2 ? 2 : 1;
                for (int i = 0; i < n; ++i) {
                    const uint32_t size = at == aw::field::kWaterTable2 ? 0xC : 0x20;
                    char* const node = static_cast<char*>(allocate(size));
                    std::memset(node, 0, size);
                    At<char*>(node, 0) = buckets[0];
                    buckets[0] = node;
                    ++At<uint32_t>(tb, aw::table::kCount);
                }
            }
        }
        fresh.Mix(Virtual<uint32_t>(made, aw::slot::kRelease));
    }
    {
        using Op = void*(__cdecl*)(std::size_t);
        char* const bare = static_cast<char*>(reinterpret_cast<Op>(addr::SC4_operator_new)(0xA88));
        if (bare) {
            using Ctor = char*(__thiscall*)(void*);
            using Dtor = char*(__thiscall*)(void*, uint32_t);
            char* const self = reinterpret_cast<Ctor>(addr::cSC4AutomataManager_Construct)(bare);
            fresh.Mix(self == bare ? 1u : 0u);
            fresh.Mix(static_cast<uint32_t>(*reinterpret_cast<std::uintptr_t*>(self) == addr::cSC4AutomataManager_Vtable));
            fresh.Mix(Virtual<uint8_t>(self, aw::slot::kIsActive));
            fresh.Mix(Virtual<uint32_t>(self + aw::field::kSerializable, aw::slot::kGetGZCLSID));
            fresh.Mix(reinterpret_cast<Dtor>(addr::cSC4AutomataManager_DeletingDestructor)(self, 1) == bare ? 1u : 0u);
        }
    }
    char part[48];
    std::snprintf(part, sizeof part, ",%08X,%X", fresh.h, flags);
    text += part;
    g_awText = text;
    log::Write("autotest: automata: aw1%s logged %08X", text.c_str(), logged.h);
}

// ====================================================================================
// Part aw2's drive (OPENSC4_TEST_AUTOMATA_AWMGR=2), see the top of the file.

std::string g_aw2Text;

// A message object of the test's own: slot 0x10 the type, slot 0x24 a
// value, every other slot (up to 0x84) 0.
struct FakeMessage {
    void** vtable;
    uint32_t type;
    uint32_t value;
};
uint32_t __fastcall FakeZero(void*, void*) { return 0; }
uint32_t __fastcall FakeType(FakeMessage* m, void*) { return m->type; }
uint32_t __fastcall FakeValue(FakeMessage* m, void*) { return m->value; }
void** FakeMessageVtable()
{
    static void* table[34];
    if (!table[0]) {
        for (void*& slot : table)
            slot = reinterpret_cast<void*>(&FakeZero);
        table[0x10 / 4] = reinterpret_cast<void*>(&FakeType);
        table[0x24 / 4] = reinterpret_cast<void*>(&FakeValue);
    }
    return table;
}

// A cIGZString of the test's own: slot 5 (ToChar) the text.
struct FakeString {
    void** vtable;
    const char* text;
};
const char* __fastcall FakeToChar(FakeString* s, void*) { return s->text; }
void** FakeStringVtable()
{
    static void* table[8];
    if (!table[0]) {
        for (void*& slot : table)
            slot = reinterpret_cast<void*>(&FakeZero);
        table[5] = reinterpret_cast<void*>(&FakeToChar);
    }
    return table;
}

void AwLifeDrive(cISC4City* city)
{
    namespace aw = automata_awmgr;
    std::string text = " B";
    Hash logged, fresh, cheats;
    uint32_t flags = 0;
    using Io = uint32_t(__thiscall*)(void*, void*);
    const auto write = reinterpret_cast<Io>(addr::cSC4AutomataManager_Write);
    const auto read = reinterpret_cast<Io>(addr::cSC4AutomataManager_Read);
    const std::size_t slots[] = {aw::kCitySlotAircraft, aw::kCitySlotWatercraft, automata_vehmgr::kCitySlotVehicles,
                                 automata_vehmgr::kCitySlotPedestrians};
    std::vector<uint8_t> sample;
    for (int m = 0; m < 4; ++m) {
        char* const manager = Virtual<char*>(city, slots[m]);
        char part[16];
        if (!manager) {
            text += m ? ",-" : "-";
            continue;
        }
        // The common base's Write: the version, the most, the two bools and
        // the table go into the token; the count (bytes 8..11) follows frames.
        ByteOStream out;
        Hash hash;
        hash.Mix(write(manager + aw::field::kSerializable, &out) & 0xFF);
        hash.Mix(static_cast<uint32_t>(out.bytes.size()));
        if (out.bytes.size() >= 12) {
            hash.MixBytes(out.bytes.data(), 8);
            hash.MixBytes(out.bytes.data() + 12, out.bytes.size() - 12);
            logged.Mix(U32(out.bytes, 8));
        }
        if (m == 0)
            sample = out.bytes;
        // TrackAutomataGroupInstances for the first two groups of the
        // exemplar map that are not tracked: AL into the token, the set's
        // size to the log, then untracked again (the references given back).
        std::vector<uint32_t> keys;
        TreeKeys(At<char*>(At<char*>(manager, aw::field::kExemplarMap), aw::tree::kParent), keys);
        int tried = 0;
        for (const uint32_t key : keys) {
            if (tried == 2)
                break;
            struct {
                char* node;
                void* table;
            } it = {};
            uint32_t k = key;
            using Find = void(__thiscall*)(void*, void*, const uint32_t*);
            reinterpret_cast<Find>(addr::SC4Uint32HashTable_Find)(manager + aw::field::kGroupInstances, &it, &k);
            if (it.node)
                continue;
            ++tried;
            hash.Mix(key);
            hash.Mix(Virtual<uint8_t>(manager, 0x7C, key, 1u));
            reinterpret_cast<Find>(addr::SC4Uint32HashTable_Find)(manager + aw::field::kGroupInstances, &it, &k);
            logged.Mix(it.node ? At<uint32_t>(it.node + 8, aw::table::kCount) : 0xFFFFFFFFu);
            hash.Mix(Virtual<uint8_t>(manager, 0x7C, key, 0u));
            reinterpret_cast<Find>(addr::SC4Uint32HashTable_Find)(manager + aw::field::kGroupInstances, &it, &k);
            hash.Mix(it.node ? 1u : 0u);
        }
        hash.Mix(Virtual<uint8_t>(manager, 0x7C, 0x7A5A0001u, 1u)); // a group the map does not hold
        std::snprintf(part, sizeof part, "%s%08X", m ? "," : "", hash.h);
        text += part;
    }
    // A new aircraft manager, held by one reference: the round trip of the
    // first city manager's bytes (its count zeroed: the references read
    // back as nothing), reads of versions 7, 4 and 5, SetMaxCount, Activate,
    // ReleaseAutomatonSet on a set of the test's own, messages, then the
    // release.
    using Factory = void*(__cdecl*)();
    if (void* const serial = reinterpret_cast<Factory>(addr::cSC4AircraftManager_Create)()) {
        char* const made = static_cast<char*>(serial) - aw::field::kSerializable;
        Virtual<uint32_t>(made, aw::slot::kAddRef);
        if (sample.size() >= 12) {
            std::vector<uint8_t> copy = sample;
            Put32(copy, 8, 0);
            ByteIStream in(copy);
            fresh.Mix(read(serial, &in) & 0xFF);
            ByteOStream again;
            write(serial, &again);
            if (again.bytes != copy)
                flags |= 1;
            fresh.Mix(At<uint8_t>(made, 0xA78));
            fresh.Mix(At<uint8_t>(made, 0x10));
            for (const uint16_t version : {uint16_t{7}, uint16_t{4}, uint16_t{5}}) {
                std::vector<uint8_t> v = copy;
                Put16(v, 0, version);
                if (version == 5)
                    v.resize(12 + 4), Put32(v, 12, 0); // no table records
                ByteIStream vin(v);
                fresh.Mix(read(serial, &vin) & 0xFF);
                fresh.Mix(At<uint8_t>(made, 0xA78));
                fresh.Mix(At<uint32_t>(made + aw::field::kSourceCounts, aw::table::kCount));
            }
        } else {
            flags |= 2;
        }
        for (const int32_t most : {-5, 30, 30, 9000}) {
            Virtual<void>(made, 0x5C, most);
            fresh.Mix(At<uint32_t>(made, aw::field::kMaxCount));
            fresh.Mix(At<uint32_t>(made, aw::field::kShare));
        }
        for (const uint32_t on : {0u, 1u})
            fresh.Mix(Virtual<uint8_t>(made, 0x14, on));
        {
            alignas(4) char set[aw::table::kSize] = {};
            using Ctor = void*(__thiscall*)(void*, uint32_t);
            reinterpret_cast<Ctor>(addr::SC4Uint32HashSet_Construct)(set, 0x64);
            Virtual<void>(made, 0x84, static_cast<void*>(set));
            fresh.Mix(At<uint32_t>(set, aw::table::kCount));
            using Free = void(__cdecl*)(void*);
            if (void* const buckets = At<void*>(set, aw::table::kBegin))
                reinterpret_cast<Free>(addr::SC4_Free)(buckets);
        }
        // Messages to the message target (+0xC).
        FakeMessage message{FakeMessageVtable(), 0, 0};
        auto send = [&](uint32_t type, uint32_t value) {
            message.type = type;
            message.value = value;
            fresh.Mix(Virtual<uint8_t>(made + aw::field::kMessage, 0x0C, static_cast<void*>(&message)));
            fresh.Mix(At<uint8_t>(made, aw::field::kA3D));
            fresh.Mix(At<uint8_t>(made, 0xA4C));
        };
        send(0x4A7FB807, 1);
        send(0x4A7FB807, 0);
        send(0xCAD61E00, 0);
        send(0xA6B79612, 0); // held: nothing
        send(0xEAD61E16, 0);
        send(0xA6B79602, 0);
        send(0xA6B79612, 0);
        send(0xC9F775BB, 0);
        send(0x06CDB65A, 0);
        send(0x89D3E9F3, 1); // no object: nothing
        send(0xEA298D50, 0);
        send(0xEBFF18A3, 0); // no object: nothing
        send(0x12345678, 0);
        logged.MixBytes(made + 0xA30, 12);
        fresh.Mix(Virtual<uint32_t>(made, aw::slot::kRelease));
    } else {
        flags |= 4;
    }
    // The group instance table's insert and erase on a table of the test's
    // own: two keys in one bucket, the older (second in the chain) erased
    // first, which goes through 0x00510450. And the exemplar record copy
    // 0x00512C80 / 0x005120E0 on two records of the test's own (no string).
    {
        using Ctor = void*(__thiscall*)(void*, uint32_t);
        using Insert = void(__thiscall*)(void*, void*, const void*);
        using Erase = void(__thiscall*)(void*, void*);
        using Clear = void(__thiscall*)(void*);
        using Free = void(__cdecl*)(void*);
        alignas(4) char table[aw::table::kSize] = {};
        reinterpret_cast<Ctor>(addr::SC4Uint32HashSet_Construct)(table, 0x64);
        const uint32_t buckets = static_cast<uint32_t>(At<char**>(table, aw::table::kEnd) -
                                                       At<char**>(table, aw::table::kBegin));
        void* nodes[2] = {};
        for (int i = 0; i < 2 && buckets; ++i) {
            alignas(4) char pair[4 + aw::table::kSize] = {};
            At<uint32_t>(pair, 0) = 7 + static_cast<uint32_t>(i) * buckets;
            reinterpret_cast<Ctor>(addr::SC4Uint32HashSet_Construct)(pair + 4, 0x64);
            alignas(4) char result[12] = {};
            reinterpret_cast<Insert>(addr::cSC4AutomataManager_InsertInstances)(table, result, pair);
            nodes[i] = At<void*>(result, 0);
            fresh.Mix(At<uint8_t>(result, 8));
            reinterpret_cast<Clear>(addr::SC4Uint32HashSet_Clear)(pair + 4);
            if (void* const b = At<void*>(pair + 4, aw::table::kBegin))
                reinterpret_cast<Free>(addr::SC4_Free)(b);
        }
        fresh.Mix(At<uint32_t>(table, aw::table::kCount));
        for (int i = 0; i < 2; ++i) {
            void* iterator[2] = {nodes[i], table};
            reinterpret_cast<Erase>(addr::cSC4AutomataManager_EraseInstances)(table, iterator);
            fresh.Mix(At<uint32_t>(table, aw::table::kCount));
        }
        if (void* const b = At<void*>(table, aw::table::kBegin))
            reinterpret_cast<Free>(addr::SC4_Free)(b);
        alignas(4) char records[2][aw::exemplar::kSize] = {};
        for (std::size_t at = 0; at < aw::exemplar::kSize; at += 4)
            if (at < 0x1C || at >= 0x30)
                At<uint32_t>(records[0], at) = 0x01010101u * static_cast<uint32_t>(at);
        // Their strings (+0x1C: a cIGZString, the buffer at +0x20) made by
        // SC4String::FromText, as the game's records have them.
        using FromText = void*(__thiscall*)(void*, const char*);
        reinterpret_cast<FromText>(addr::SC4String_FromText)(records[0] + 0x1C, "copied");
        reinterpret_cast<FromText>(addr::SC4String_FromText)(records[1] + 0x1C, "");
        using Copy = char*(__cdecl*)(char*, char*, char*, void*, uint32_t);
        char tag = 0;
        char* const end = reinterpret_cast<Copy>(addr::cSC4AutomataManager_CopyExemplars)(
            records[0], records[0] + aw::exemplar::kSize, records[1], &tag, 0);
        fresh.Mix(end == records[1] + aw::exemplar::kSize ? 1u : 0u);
        fresh.MixBytes(records[1], 0x1C);
        fresh.MixBytes(records[1] + 0x30, aw::exemplar::kSize - 0x30);
        const char* const copied = At<const char*>(records[1], 0x20);
        const char* const copiedEnd = At<const char*>(records[1], 0x24);
        if (copied && copiedEnd > copied && copiedEnd - copied < 64)
            fresh.MixBytes(copied, static_cast<std::size_t>(copiedEnd - copied));
        for (auto& record : records)
            if (void* const b = At<void*>(record, 0x20))
                reinterpret_cast<Free>(addr::SC4_Free)(b);
    }
    // The cheat target (the city's aircraft manager's +4 part; its this is
    // not read): another message, another cheat, 0xCC478B79 with and without
    // the word it compares (0x00B09968 put back after), and the route line's
    // 0xE97FCBC8 (shown, count 7) and 0xE97FCBC9 (taken away).
    if (char* const manager = Virtual<char*>(city, aw::kCitySlotAircraft)) {
        using Cheat = uint32_t(__thiscall*)(void*, uint32_t*);
        const auto cheat = reinterpret_cast<Cheat>(addr::cSC4AutomataManager_DoMessage4);
        void* const target = manager + aw::field::kVtable4;
        static std::string line;
        FakeString string{FakeStringVtable(), ""};
        uint32_t message[3] = {0x12345678u, 0xCC478B79u, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&string))};
        cheats.Mix(cheat(target, message) & 0xFF);
        message[0] = 0x230E27ACu;
        message[1] = 0x7A5A0001u;
        cheats.Mix(cheat(target, message) & 0xFF);
        const uint8_t parked = *reinterpret_cast<uint8_t*>(0x00B09968);
        message[1] = 0xCC478B79u;
        for (int i = 0; i < 3; ++i) {
            line = i == 0 ? "cheat" : i == 1 ? std::string("cheat ") + reinterpret_cast<const char*>(0x00A82B94)
                                              : "cheat other";
            string.text = line.c_str();
            cheats.Mix(cheat(target, message) & 0xFF);
            cheats.Mix(*reinterpret_cast<uint8_t*>(0x00B09968));
        }
        *reinterpret_cast<uint8_t*>(0x00B09968) = parked;
        void*& route = *reinterpret_cast<void**>(0x00B21BA0);
        if (!route) {
            line = "route 7";
            string.text = line.c_str();
            message[1] = 0xE97FCBC8u;
            cheats.Mix(cheat(target, message) & 0xFF);
            cheats.Mix(route ? At<uint32_t>(route, 0x18) : 0xFFFFFFFEu);
            cheats.Mix(route ? At<uint8_t>(route, 0xC) : 0xFFu);
            message[1] = 0xE97FCBC9u;
            cheats.Mix(cheat(target, message) & 0xFF);
            cheats.Mix(route ? 1u : 0u);
        } else {
            flags |= 8;
        }
    }
    char part[64];
    std::snprintf(part, sizeof part, ",%08X,%08X,%X", fresh.h, cheats.h, flags);
    text += part;
    g_aw2Text = text;
    log::Write("autotest: automata: aw2%s logged %08X", text.c_str(), logged.h);
}

void Drive(const Day& day)
{
    if (EnvInt("OPENSC4_TEST_AUTOMATA", 0) < 2 || !day.counted || g_written)
        return;
    if (day.day != day.first + EnvInt("OPENSC4_TEST_AUTOMATA_DAY", 3))
        return;
    if (EnvInt("OPENSC4_TEST_AUTOMATA_AWMGR", 0) >= 1 && day.city && game::IsBuild641() && g_awText.empty())
        AwManagerDrive(day.city);
    if (EnvInt("OPENSC4_TEST_AUTOMATA_AWMGR", 0) >= 2 && day.city && game::IsBuild641() && g_aw2Text.empty())
        AwLifeDrive(day.city);
    if (EnvInt("OPENSC4_TEST_AUTOMATA_VEHMGR", 0) >= 1 && day.city && game::IsBuild641() && g_managerText.empty()) {
        ManagerDrive(day.city);
        if (EnvInt("OPENSC4_TEST_AUTOMATA_VEHMGR", 0) >= 2)
            ManagerSaveDrive(day.city);
        if (EnvInt("OPENSC4_TEST_AUTOMATA_VEHMGR", 0) >= 3)
            ManagerFrameDrive(day.city);
    }
    if (EnvInt("OPENSC4_TEST_AUTOMATA_SCRIPTS", 0) >= 1 && day.city && game::IsBuild641() && g_scriptText.empty()) {
        ScriptsDrive(day.city);
        if (EnvInt("OPENSC4_TEST_AUTOMATA_SCRIPTS", 0) >= 2)
            ScriptsDrive2(day.city);
        if (EnvInt("OPENSC4_TEST_AUTOMATA_SCRIPTS", 0) >= 3)
            ScriptsDrive3(day.city);
    }
    if (char* const manager = Manager(day.city)) {
        WriteDrive(manager);
        QueryDrive(manager);
        if (EnvInt("OPENSC4_TEST_AUTOMATA", 0) >= 3)
            ControllerDrive(manager);
    }
}

void Fingerprint(cISC4City* city, std::string& line)
{
    if (EnvInt("OPENSC4_TEST_AUTOMATA", 0) < 1)
        return;
    char* const manager = Manager(city);
    if (!manager) {
        line += " | auto none";
        return;
    }
    uint32_t occupants = 0, controllers = 0, sum = 0;
    ForEach<ControllerNode>(Table(manager, field::kControllers), [&](ControllerNode* node) {
        ++occupants;
        Hash entry;
        entry.Mix(OccupantHash(node->occupant));
        uint32_t count = 0, inner = 0;
        for (ListNode* e = node->head->next; e != node->head; e = e->next) {
            ++count;
            inner += ControllerHash(e->value);
        }
        controllers += count;
        entry.Mix(count);
        entry.Mix(inner);
        sum += entry.h;
    });
    const Grid* const grid = reinterpret_cast<const Grid*>(manager + field::kGrid);
    uint32_t filled = 0, entries = 0, gridSum = 0;
    const uint32_t cells = static_cast<uint32_t>(grid->end - grid->begin);
    for (uint32_t i = 0; i < cells; ++i) {
        ListNode* const head = grid->begin[i];
        if (!head)
            continue;
        uint32_t here = 0;
        for (ListNode* e = head->next; e != head; e = e->next) {
            ++here;
            Hash h;
            h.Mix(i);
            h.Mix(ControllerHash(e->value));
            gridSum += h.h;
        }
        entries += here;
        filled += here ? 1u : 0u;
    }
    Hash time;
    time.MixBytes(manager + field::kTimeA, 4);
    time.MixBytes(manager + field::kTimeB, 2);
    time.MixBytes(manager + field::kTimeC, 4);
    time.MixBytes(manager + field::kTimeD, 2);
    char text[192];
    std::snprintf(text, sizeof text, " | auto n%u/%u c%08X g%u,%u,%u,%08X", occupants, controllers, sum,
                  static_cast<unsigned>(grid->cellsX), filled, entries, gridSum);
    line += text;
    if (g_written)
        line += g_writeText + g_queryText + g_controllerText;
    line += g_managerText;
    line += g_scriptText;
    line += g_awText;
    line += g_aw2Text;
    // The automaton table: frame-driven, the log only.
    uint32_t parked = 0, automata = 0, parkedSum = 0;
    ForEach<AutomatonNode>(Table(manager, field::kAutomata), [&](AutomatonNode* node) {
        ++parked;
        Hash entry;
        entry.Mix(OccupantHash(node->occupant));
        uint32_t count = 0;
        for (ListNode* e = node->value.head->next; e != node->value.head; e = e->next)
            ++count;
        automata += count;
        entry.Mix(count);
        entry.Mix(node->value.count);
        entry.MixBytes(node->value.first, sizeof node->value.first);
        entry.MixBytes(node->value.second, sizeof node->value.second);
        entry.Mix(node->value.flag);
        parkedSum += entry.h;
    });
    static uint32_t lines = 0;
    if (lines++ < 64)
        log::Write("autotest: automata: %u automaton entries, %u automata (%08X), time %.9g %d %.9g %d (%08X)", parked,
                   automata, parkedSum, static_cast<double>(At<float>(manager, field::kTimeA)),
                   At<int16_t>(manager, field::kTimeB), static_cast<double>(At<float>(manager, field::kTimeC)),
                   At<int16_t>(manager, field::kTimeD), time.h);
}

const FingerprintPart kFingerprint{"automata", 241, &Fingerprint};
const DayHook kDrive{"automata.write", 241, &Drive};

} // namespace
} // namespace opensc4::autotest
