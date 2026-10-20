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
// A remove and add round trip of one occupant's controllers is not done:
// removing sends 0xEA298D50 to the automata, destroys the controllers and
// can drop the occupant's automaton entry (its automata released), and the
// add makes new controllers through the COM; the city would not be the
// same afterwards.
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

void Drive(const Day& day)
{
    if (EnvInt("OPENSC4_TEST_AUTOMATA", 0) < 2 || !day.counted || g_written)
        return;
    if (day.day != day.first + EnvInt("OPENSC4_TEST_AUTOMATA_DAY", 3))
        return;
    if (EnvInt("OPENSC4_TEST_AUTOMATA_VEHMGR", 0) >= 1 && day.city && game::IsBuild641() && g_managerText.empty()) {
        ManagerDrive(day.city);
        if (EnvInt("OPENSC4_TEST_AUTOMATA_VEHMGR", 0) >= 2)
            ManagerSaveDrive(day.city);
    }
    if (EnvInt("OPENSC4_TEST_AUTOMATA_SCRIPTS", 0) >= 1 && day.city && game::IsBuild641() && g_scriptText.empty())
        ScriptsDrive(day.city);
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
