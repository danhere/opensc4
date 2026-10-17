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

void Drive(const Day& day)
{
    if (EnvInt("OPENSC4_TEST_AUTOMATA", 0) < 2 || !day.counted || g_written)
        return;
    if (day.day != day.first + EnvInt("OPENSC4_TEST_AUTOMATA_DAY", 3))
        return;
    if (char* const manager = Manager(day.city)) {
        WriteDrive(manager);
        QueryDrive(manager);
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
        line += g_writeText + g_queryText;
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
