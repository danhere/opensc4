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
// A remove and add round trip of one occupant's controllers is not done:
// removing sends 0xEA298D50 to the automata, destroys the controllers and
// can drop the occupant's automaton entry (its automata released), and the
// add makes new controllers through the COM; the city would not be the
// same afterwards.
#include "core/automata_ctlmgr.h"
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

void Drive(const Day& day)
{
    if (EnvInt("OPENSC4_TEST_AUTOMATA", 0) < 2 || !day.counted || g_written)
        return;
    if (day.day != day.first + EnvInt("OPENSC4_TEST_AUTOMATA_DAY", 3))
        return;
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
