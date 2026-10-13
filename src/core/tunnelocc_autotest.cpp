// The tunnel occupants' part of the autotest (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/occmgr_sim.h"
#include "core/tunnelocc_sim.h"

#include <cIGZIStream.h>
#include <cISC4City.h>

#include <opensc4/addresses.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace opensc4::autotest {
namespace {

namespace field = tunnelocc_sim::field;
namespace slot = tunnelocc_sim::slot;

// The four bytes of the occupant manager's cells the occupant base keeps
// (object offsets; saved): x and z of the first cell, x and z of the last.
constexpr std::size_t kManagerCells = 0x2C;

struct Hash {
    uint32_t h = 2166136261u;
    void Mix(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    }
};

// How many tunnel entrances answered something that says the test reaches
// the paths of the functions (printed after the checksums).
struct Counters {
    uint32_t linked = 0, mutual = 0, usable = 0, translucent = 0;
};

// One QueryInterface through a part: the answer, and where in the object
// the answer lies (never the address); the reference it gave is released.
void AskInterface(Hash& hash, char* self, char* part, uint32_t iid)
{
    void* found = nullptr;
    const bool answered = Virtual<uint8_t>(part, 0x0, iid, &found) != 0;
    hash.Mix(answered);
    if (answered && found) {
        hash.Mix(static_cast<uint32_t>(static_cast<char*>(found) - self));
        Virtual<uint32_t>(found, slot::kRelease);
    }
}

// The part of a tunnel entrance that the network occupant's walk
// (src/core/netocc_autotest.cpp, which takes every occupant of type
// 0xC772BF98, tunnels included) does not reach: its own saved field (the
// other entrance: whether there is one, whether that one is a tunnel whose
// other entrance is this one, and how far away in the occupant manager's
// cells it is), the network flags the constructor and the occupant part's
// Init set, the parent class's saved fields (three floats, the word at
// +0x19C and when it is not zero the four floats after it, the word at
// +0x1B0 and the byte at +0x1B4), the class ID of the serializable part,
// the occupant type and QueryInterface through the interface part and the
// occupant part for the tunnel's IID, the parent's, the network occupant's,
// 0xA9764C8C, 1, the bridge's (not answered) and one nobody answers (each
// reference given is released). Not mixed: pointers, reference counts, the
// parent's objects at +0x1C4 and +0x1C8 and its other fields (the model's
// state, which the view drives).
uint32_t TunnelHash(char* self, Counters& counters)
{
    Hash hash;
    char* const part = self + field::kOccupant;
    char* const other = At<char*>(self, field::kOtherEnd);
    hash.Mix(other != nullptr);
    counters.linked += other != nullptr;
    for (std::size_t i = 0; i < 4; ++i)
        hash.Mix(At<uint8_t>(self, kManagerCells + i));
    if (other) {
        const uint32_t otherClass = Virtual<uint32_t>(other + field::kSerializable, slot::kGetGZCLSID);
        const bool tunnel = otherClass == tunnelocc_sim::kClassId;
        hash.Mix(tunnel);
        const bool mutual = tunnel && At<char*>(other, field::kOtherEnd) == self;
        hash.Mix(mutual);
        counters.mutual += mutual;
        for (std::size_t i = 0; i < 4; ++i)
            hash.Mix(static_cast<uint32_t>(At<uint8_t>(other, kManagerCells + i)) -
                     static_cast<uint32_t>(At<uint8_t>(self, kManagerCells + i)));
    }
    const uint32_t flags = At<uint32_t>(self, field::kFlags);
    hash.Mix(flags & (tunnelocc_sim::kFlagTranslucent | tunnelocc_sim::kFlagsConstructed));
    counters.usable += (flags & 0x10000000u) != 0;
    counters.translucent += (flags & tunnelocc_sim::kFlagTranslucent) != 0;
    for (std::size_t i = 0; i < 3; ++i)
        hash.Mix(At<uint32_t>(self, field::kFloats178 + 4 * i));
    hash.Mix(At<uint32_t>(self, field::kWord19C));
    if (At<uint32_t>(self, field::kWord19C) != 0)
        for (std::size_t i = 0; i < 4; ++i)
            hash.Mix(At<uint32_t>(self, field::kFloats1A0 + 4 * i));
    hash.Mix(At<uint32_t>(self, field::kWord1B0));
    hash.Mix(At<uint8_t>(self, field::kByte1B4));
    hash.Mix(Virtual<uint32_t>(self + field::kSerializable, slot::kGetGZCLSID));
    hash.Mix(Virtual<uint32_t>(part, 0x1C));
    const uint32_t ids[] = {tunnelocc_sim::kIidTunnel,
                            tunnelocc_sim::kIidPrebuilt,
                            tunnelocc_sim::kIidNetworkOccupant,
                            0xA9764C8Cu,
                            1u,
                            0x49CC1BCDu,
                            0x0F10A0CCu};
    for (char* const asked : {self, part})
        for (const uint32_t iid : ids)
            AskInterface(hash, self, asked, iid);
    return hash.h;
}

// Every tunnel entrance of the city (build 641; src/core/tunnelocc_sim.h),
// found through the occupant manager (the city's slot 34 / +0x88): every
// occupant of the network occupant type 0xC772BF98 in its grid, one in
// several cells once, whose serializable part answers the tunnel's class
// ID. The objects are the interface part (the occupant part less 4), in the
// order of their cells (x, then z, of the first cell), so that what is done
// to them one after the other does not follow the order of the walk. False
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
                if (Virtual<uint32_t>(object, 0x1C) != tunnelocc_sim::kOccupantType)
                    continue;
                char* const self = static_cast<char*>(object) - field::kOccupant;
                if (Virtual<uint32_t>(self + field::kSerializable, slot::kGetGZCLSID) == tunnelocc_sim::kClassId)
                    seen.insert(self);
            }
        }
    }
    Virtual<void>(manager + occmgr_sim::field::kLock, 0x8);
    found.assign(seen.begin(), seen.end());
    const auto position = [](char* self) {
        return (static_cast<uint32_t>(At<uint8_t>(self, kManagerCells)) << 8) | At<uint8_t>(self, kManagerCells + 1);
    };
    std::stable_sort(found.begin(), found.end(), [&](char* a, char* b) { return position(a) < position(b); });
    return true;
}

using SetOtherEndFn = void(__thiscall*)(void*, void*);

SetOtherEndFn SetOtherEnd()
{
    return reinterpret_cast<SetOtherEndFn>(addr::cSC4NetworkTunnelOccupant_SetOtherEnd);
}

// The setter's exercise (OPENSC4_TEST_TUNNELOCC=<day>): the checksum, summed
// over the city's tunnel entrances driven, how many were driven, and the
// made object's checksum.
uint32_t g_setSum = 0;
uint32_t g_setCount = 0;
uint32_t g_lifeSum = 0;
bool g_setDone = false;

// SetOtherEnd (0x00647530) on one tunnel entrance of the city: with null
// (the other entrance let go; the test holds a reference of its own on it
// meanwhile), read back; with the old pointer, read back; with the old
// pointer once more (the same pointer: nothing happens), read back. The
// other entrance's reference count is the same after as before (the
// difference is mixed, not the count). An entrance without another one gets
// null only (nothing happens). Then the occupant part's Init, which on an
// initialised object only answers 1 (an entrance of the city that is not
// initialised is not asked).
uint32_t Exercise(char* self)
{
    Hash hash;
    char* const old = At<char*>(self, field::kOtherEnd);
    const uint32_t before = old ? At<uint32_t>(old, field::kRefCount) : 0;
    if (old)
        Virtual<uint32_t>(old, slot::kAddRef);
    SetOtherEnd()(self, nullptr);
    hash.Mix(At<char*>(self, field::kOtherEnd) == nullptr);
    SetOtherEnd()(self, old);
    hash.Mix(At<char*>(self, field::kOtherEnd) == old);
    SetOtherEnd()(self, old);
    hash.Mix(At<char*>(self, field::kOtherEnd) == old);
    if (old) {
        Virtual<uint32_t>(old, slot::kRelease);
        hash.Mix(At<uint32_t>(old, field::kRefCount) - before);
    }
    if (At<uint8_t>(self, field::kState) & 1)
        hash.Mix(Virtual<uint8_t>(self + field::kOccupant, slot::kOccupantInit));
    return hash.h;
}

// An input stream over a few bytes, as src/core/save_autotest.cpp's: a read
// past the end stores nothing, fails and leaves the stream in error. It
// answers no interface.
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

// Read's refusal of a version that is not 2, on a made object: through the
// serializable part's slot 4 and through the holder's thunk (slot 24, this
// - 4), with a stream that holds version 1, one that holds 3 and an empty
// one (the version's slot then keeps the low word of the part's address,
// which ends in 8, so it is never 2). Read asks the stream for one uint16
// and answers AL 0 without touching the object; the bytes taken and the
// stream's error are mixed with the answer.
void ReadRefusals(Hash& hash, char* self)
{
    const std::vector<uint8_t> streams[] = {{1, 0, 0xEE, 0xEE}, {3, 0, 0xEE, 0xEE}, {}};
    for (const std::vector<uint8_t>& bytes : streams)
        for (const bool throughHolder : {false, true}) {
            ByteIStream in(bytes);
            cIGZIStream* const stream = &in;
            if (throughHolder)
                hash.Mix(Virtual<uint8_t>(self + field::kHolder, 0x60, static_cast<void*>(stream)));
            else
                hash.Mix(Virtual<uint8_t>(self + field::kSerializable, slot::kRead, static_cast<void*>(stream)));
            hash.Mix(static_cast<uint32_t>(in.at));
            hash.Mix(static_cast<uint32_t>(in.error));
        }
    hash.Mix(At<void*>(self, field::kOtherEnd) == nullptr);
}

void* Create()
{
    return reinterpret_cast<void*(__cdecl*)()>(addr::cSC4NetworkTunnelOccupant_Create)();
}

// The class's life on made objects (OPENSC4_TEST_TUNNELOCC=<day>), which are
// in no cell and no network.
//   - `self` from the game's factory (0x006477F0: the pool, the constructor;
//     it returns the occupant part), one reference held by the test;
//     GetGZCLSID of the serializable part; QueryInterface through the parts
//     that answer to it (the interface, the occupant part, the serializable
//     part, the holder, the exemplar part and the virtual base, whose slot 0
//     is a thunk to it) for the IDs the class answers (its own, the
//     parent's, the network occupant's, 0xA9764C8C, 1, the occupant, the
//     serializable and 0x26A68449) and some it does not (the network
//     occupant's class ID, the bridge's, one nobody has).
//   - the fields the constructors set: no other entrance, the network flags,
//     the virtual base's shift, the eight vtable and table pointers, the
//     parent's words at +0x19C and +0x1B0 and byte at +0x1B4 (its floats at
//     +0x178 and +0x1A0 are left as the pool's memory holds them: not
//     mixed).
//   - `other` made as the network tool makes one (0x006242B5): the pool
//     allocation 0x00647230 with the size, then the constructor with 1. The
//     setter both ways and with null and the same pointer again, the
//     pointers and both reference counts read back after each call (made
//     objects: the counts are the test's own). Then the test's reference on
//     `other` is released and `self` given null: `other` dies in the
//     setter's Release, through the deleting destructor, whose destructor
//     releases its other entrance (`self`, whose count is read back).
//   - the occupant part's Init on `self` marked initialised (the branch
//     that answers 1 and does nothing) and slot 4 on `self` not initialised
//     (the same), through the interface and through the occupant part's
//     thunk; Read's refusals (ReadRefusals).
//   - with `pool`: 68 more objects from the factory (a page holds 68, so
//     the pool takes another page, through its Grow and the page set-up
//     0x006471F0), how many of them are not on `self`'s page, then all
//     released, last made first (pages emptied and given back, the full one
//     opened again at 51 used).
// The last Release destroys `self` and returns the item to its page.
// Not called: the occupant part's Init of an object not initialised (it
// ends in the occupant message 0x99EF1140, queued with the made object as
// its object) and slot 4 of an initialised one (message 0x99EF1141):
// listeners would meet an occupant that is in no cell. Write and Read of a
// whole record: no autotest makes the game's memory stream, and the record
// holds the property holder's, the network occupant's and the parent's
// parts, whose Reads end in the model's state; they run when a city with a
// tunnel is saved and loaded.
uint32_t Life(bool pool, uint32_t& elsewhere)
{
    Hash hash;
    char* const part = static_cast<char*>(Create());
    if (!part)
        return 0;
    char* const self = part - field::kOccupant;
    Virtual<uint32_t>(self, slot::kAddRef);
    hash.Mix(Virtual<uint32_t>(self + field::kSerializable, slot::kGetGZCLSID));
    const uint32_t ids[] = {tunnelocc_sim::kIidTunnel,
                            tunnelocc_sim::kIidPrebuilt,
                            tunnelocc_sim::kIidNetworkOccupant,
                            0xA9764C8Cu,
                            1u,
                            0x26BB767Eu,
                            tunnelocc_sim::kIidSerializable,
                            0x26A68449u,
                            0xC9C05C6Eu,
                            0x49CC1BCDu,
                            0x0F10A0CCu};
    for (const std::size_t offset : {std::size_t{0}, field::kOccupant, field::kSerializable, field::kHolder,
                                     field::kExemplarPart, field::kVbase})
        for (const uint32_t iid : ids)
            AskInterface(hash, self, self + offset, iid);
    hash.Mix(At<void*>(self, field::kOtherEnd) == nullptr);
    hash.Mix(At<uint32_t>(self, field::kFlags));
    hash.Mix(At<uint32_t>(self, field::kVbaseShift));
    hash.Mix(At<std::uintptr_t>(self, 0x0) == addr::cSC4NetworkTunnelOccupant_vftable);
    hash.Mix(At<std::uintptr_t>(self, field::kOccupant) == addr::cSC4NetworkTunnelOccupant_vftable_4);
    hash.Mix(At<std::uintptr_t>(self, field::kSerializable) == addr::cSC4NetworkTunnelOccupant_vftable_8);
    hash.Mix(At<std::uintptr_t>(self, field::kHolder) == addr::cSC4NetworkTunnelOccupant_vftable_C);
    hash.Mix(At<std::uintptr_t>(self, field::kExemplarPart) == addr::cSC4NetworkTunnelOccupant_vftable_20);
    hash.Mix(At<std::uintptr_t>(self, field::kModelPart) == addr::cSC4NetworkTunnelOccupant_vftable_38);
    hash.Mix(At<std::uintptr_t>(self, field::kVbase) == addr::cSC4NetworkTunnelOccupant_vbase_vftable);
    hash.Mix(At<std::uintptr_t>(self, field::kVbtable) == addr::cSC4NetworkTunnelOccupant_vbtable);
    hash.Mix(At<uint32_t>(self, field::kWord19C));
    hash.Mix(At<uint32_t>(self, field::kWord1B0));
    hash.Mix(At<uint8_t>(self, field::kByte1B4));

    void* const raw = reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::cSC4NetworkTunnelOccupant_Allocate)(
        tunnelocc_sim::kObjectSize);
    char* const other = raw ? reinterpret_cast<char*(__thiscall*)(void*, uint32_t)>(
                                  addr::cSC4NetworkTunnelOccupant_cSC4NetworkTunnelOccupant)(raw, 1u)
                            : nullptr;
    if (other) {
        Virtual<uint32_t>(other, slot::kAddRef);
        const auto state = [&] {
            hash.Mix(At<char*>(self, field::kOtherEnd) == other);
            hash.Mix(At<char*>(self, field::kOtherEnd) == nullptr);
            hash.Mix(At<uint32_t>(self, field::kRefCount));
        };
        const auto both = [&] {
            state();
            hash.Mix(At<char*>(other, field::kOtherEnd) == self);
            hash.Mix(At<uint32_t>(other, field::kRefCount));
        };
        both();
        SetOtherEnd()(self, other);
        both();
        SetOtherEnd()(self, other);
        both();
        SetOtherEnd()(other, self);
        both();
        SetOtherEnd()(self, nullptr);
        both();
        SetOtherEnd()(self, other);
        both();
        Virtual<uint32_t>(other, slot::kRelease);
        both();
        SetOtherEnd()(self, nullptr);
        state();
    }

    At<uint8_t>(self, field::kState) |= 1;
    hash.Mix(Virtual<uint8_t>(part, slot::kOccupantInit));
    At<uint8_t>(self, field::kState) &= 0xFE;
    hash.Mix(Virtual<uint8_t>(self, slot::kShutdown));
    hash.Mix(Virtual<uint8_t>(part, slot::kShutdown));
    hash.Mix(At<uint32_t>(self, field::kFlags));
    ReadRefusals(hash, self);

    if (pool) {
        const std::uintptr_t page = reinterpret_cast<std::uintptr_t>(self) & tunnelocc_sim::kPageMask;
        std::vector<char*> made;
        for (uint32_t i = 0; i < tunnelocc_sim::kPageItems; ++i) {
            char* const more = static_cast<char*>(Create());
            if (!more)
                break;
            char* const object = more - field::kOccupant;
            Virtual<uint32_t>(object, slot::kAddRef);
            elsewhere += (reinterpret_cast<std::uintptr_t>(object) & tunnelocc_sim::kPageMask) != page;
            made.push_back(object);
        }
        hash.Mix(static_cast<uint32_t>(made.size()));
        hash.Mix(elsewhere);
        while (!made.empty()) {
            Virtual<uint32_t>(made.back(), slot::kRelease);
            made.pop_back();
        }
    }
    hash.Mix(At<uint32_t>(self, field::kRefCount));
    Virtual<uint32_t>(self, slot::kRelease);
    return hash.h;
}

// OPENSC4_TEST_TUNNELOCC=<day>: on that day made objects go through Life
// (its pool part unless OPENSC4_TEST_TUNNELOCC_POOL=0, for bisecting) and
// every tunnel entrance of the city, in the order of its cells, through
// Exercise (tunnels are few: none is left out, so nothing is picked). The
// checksums are in the fingerprint's ` set` field from then on, and one log
// line says what ran, also when the city has no tunnel.
void Drive(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_TUNNELOCC") || !game::IsBuild641() || g_setDone)
        return;
    g_setDone = true;
    std::vector<char*> objects;
    const bool collected = Collect(day.city, objects);
    const bool pool = EnvInt("OPENSC4_TEST_TUNNELOCC_POOL", 1) != 0;
    uint32_t elsewhere = 0;
    g_lifeSum = Life(pool, elsewhere);
    for (char* const self : objects) {
        g_setSum += Exercise(self);
        ++g_setCount;
    }
    log::Write("autotest: tunnelocc: made objects %08X (pool part %s, %u on another page), %u tunnel entrances of the "
               "city driven %08X%s",
               g_lifeSum, pool ? "on" : "off", elsewhere, g_setCount, g_setSum, collected ? "" : " (no manager)");
}

// The fingerprint part: the number of tunnel entrances and the sum and the
// exclusive or of TunnelHash over them (order independent; no addresses),
// then the counters (entrances with another entrance, pairs that point at
// each other counted from both sides, usable, translucent), and after
// OPENSC4_TEST_TUNNELOCC's day ` set <made objects' checksum>,<the city
// entrances' checksum>,<count>`.
void Fingerprint(cISC4City* city, std::string& line)
{
    std::vector<char*> objects;
    if (!Collect(city, objects)) {
        line += " | tunnelocc none";
        return;
    }
    uint32_t sum = 0, mixed = 0;
    Counters counters;
    for (char* const self : objects) {
        const uint32_t hash = TunnelHash(self, counters);
        sum += hash;
        mixed ^= hash;
    }
    char text[256];
    std::snprintf(text, sizeof text, " | tunnelocc %u,%08X,%08X link %u mut %u use %u trans %u",
                  static_cast<unsigned>(objects.size()), sum, mixed, counters.linked, counters.mutual,
                  counters.usable, counters.translucent);
    line += text;
    if (g_setDone) {
        std::snprintf(text, sizeof text, " set %08X,%08X,%u", g_lifeSum, g_setSum, static_cast<unsigned>(g_setCount));
        line += text;
    }
}

const FingerprintPart kFingerprint{"tunnelocc", 128, &Fingerprint};
const DayHook kDrive{"tunnelocc.drive", 191, &Drive};

} // namespace
} // namespace opensc4::autotest
