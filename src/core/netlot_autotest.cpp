// The network lot manager's part of the autotest (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/netlot_sim.h"

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
#include <unordered_map>
#include <utility>
#include <vector>

namespace opensc4::autotest {
namespace {

using netlot_sim::AddRefOf;
using netlot_sim::CallCdecl;
using netlot_sim::CallThis;
using netlot_sim::ReleaseOf;
using netlot_sim::TreeSet;
using netlot_sim::Vector;
namespace field = netlot_sim::field;
namespace slot = netlot_sim::slot;
namespace hashtable = netlot_sim::hashtable;
namespace hashnode = netlot_sim::hashnode;
namespace iterator = netlot_sim::iterator;
namespace bitvector = netlot_sim::bitvector;
namespace tree = netlot_sim::tree;
namespace lot = netlot_sim::lot;
namespace lot_slot = netlot_sim::lot_slot;
namespace configuration = netlot_sim::configuration;
namespace configuration_slot = netlot_sim::configuration_slot;
namespace city_slot = netlot_sim::city_slot;
namespace traffic_slot = netlot_sim::traffic_slot;

// An IID nobody answers, the step of the sample of cells without a network
// piece, and where the second walk of IterateAllNetworkLots stops.
constexpr uint32_t kIidNobody = 0x0F10A0CC;
constexpr uint32_t kSampleStep = 7;
constexpr uint32_t kStopAfter = 3;
// A lot configuration object's words before its list of IDs, and the list
// (src/core/lotcfg_sim.h).
constexpr std::size_t kObjectWords = 0x28, kObjectIds = 0x28;

struct Hash {
    uint32_t h = 2166136261u;
    void Mix(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    }
};

// A count, a sum and an exclusive or of checksums: the same in any order.
struct Bag {
    uint32_t count = 0, sum = 0, mixed = 0;
    void Add(uint32_t v)
    {
        ++count;
        sum += v;
        mixed ^= v;
    }
};

// The city's network lot manager as the game reaches it (the city's slot
// 42), once it is initialised; null otherwise.
char* Manager(cISC4City* city)
{
    if (!game::IsBuild641() || !city)
        return nullptr;
    char* const manager = Virtual<char*>(city, city_slot::kNetworkLotManager);
    if (!manager || !At<uint8_t>(manager, field::kInitialized))
        return nullptr;
    return manager;
}

// The identity: the class ID of the serializable part and QueryInterface
// through the three parts (the two others' slot 0 are the adjustor thunks)
// for the IDs the class answers and some it does not (one above 0x89A5BA3F,
// two below): the answer and where in the object it lies. Each answer's
// reference is released through the part it came from, so AddRef and the
// counting branch of Release run through the thunks too.
uint32_t IdentityHash(char* manager)
{
    Hash hash;
    hash.Mix(Virtual<uint32_t>(manager + netlot_sim::kSerializablePart, slot::kGetGZCLSID));
    const uint32_t ids[] = {netlot_sim::kIidUnknown,
                            netlot_sim::kIidManager,
                            netlot_sim::kIidMessageTarget,
                            netlot_sim::kIidSerializable,
                            netlot_sim::kIidLot,
                            kIidNobody,
                            0xE4FDA3D5u};
    for (const std::size_t part : {std::size_t{0}, netlot_sim::kMessagePart, netlot_sim::kSerializablePart})
        for (const uint32_t iid : ids) {
            void* found = nullptr;
            const bool answered = Virtual<uint8_t>(manager + part, slot::kQueryInterface, iid, &found) != 0;
            hash.Mix(answered);
            if (answered && found) {
                hash.Mix(static_cast<uint32_t>(static_cast<char*>(found) - manager));
                ReleaseOf(found);
            }
        }
    return hash.h;
}

// The node after `at` in a tree set, as the game's caller of
// GetNetworkLotConfigurationSet walks one (0x0049A729): the least node of
// the right subtree, or the first ancestor the node is not to the right of;
// the head after the greatest node.
char* TreeNext(char* at)
{
    if (char* right = At<char*>(at, tree::kRight)) {
        while (char* const left = At<char*>(right, tree::kLeft))
            right = left;
        return right;
    }
    char* parent = At<char*>(at, tree::kParent);
    while (at == At<char*>(parent, tree::kRight)) {
        at = parent;
        parent = At<char*>(parent, tree::kParent);
    }
    return At<char*>(at, tree::kRight) != parent ? parent : at;
}

// GetNetworkLotConfigurationSet (slot 6) into a set of the test's own, made
// the way the game's only caller makes one (the lot editor window's
// 0x0049A530: a head node of 0x14 bytes from the node allocator 0x0090CF54,
// its color byte and root cleared, its own left and right, the count 0), the
// configurations appended to `out` in the set's order (by address, so no
// order to rely on), and the set taken down as that caller does: the nodes
// freed from the root (0x00499F50) when there are any, the head freed
// (0x0090CF63). Every entry of `out` holds `calls` references, which the
// caller of this must release: the manager references each configuration at
// every call, and a second call into a set that is not empty frees the
// first call's nodes without releasing what they held. The answer is the
// set's count after each call (the second 0 for one call).
struct SetCounts {
    uint32_t first = 0, second = 0;
};
bool CollectConfigurations(char* manager, std::vector<void*>& out, uint32_t calls, SetCounts& counts)
{
    TreeSet set;
    set.head = CallCdecl<char*, std::size_t>(addr::SC4_stl_allocate, tree::kNodeSize);
    if (!set.head)
        return false;
    set.count = 0;
    At<uint8_t>(set.head, tree::kColor) = 0;
    At<char*>(set.head, tree::kParent) = nullptr;
    At<char*>(set.head, tree::kLeft) = set.head;
    At<char*>(set.head, tree::kRight) = set.head;
    Virtual<void>(manager, slot::kGetNetworkLotConfigurationSet, &set);
    counts.first = set.count;
    if (calls > 1) {
        Virtual<void>(manager, slot::kGetNetworkLotConfigurationSet, &set);
        counts.second = set.count;
    }
    for (char* at = At<char*>(set.head, tree::kLeft); at != set.head; at = TreeNext(at))
        out.push_back(At<void*>(at, tree::kValue));
    if (set.count != 0) {
        CallThis<void, char*>(addr::SC4PtrSet_EraseSubtree, &set, At<char*>(set.head, tree::kParent));
        At<char*>(set.head, tree::kLeft) = set.head;
        At<char*>(set.head, tree::kParent) = nullptr;
        At<char*>(set.head, tree::kRight) = set.head;
        set.count = 0;
    }
    CallCdecl<void, void*>(addr::SC4_Free, set.head);
    return true;
}

uint32_t ConfigurationId(void* config)
{
    return Virtual<uint32_t>(config, configuration_slot::kGetId);
}

// The objects of a vector of lot configuration objects: the count, and of
// each its ten words (kind, flags, orientation and mirror bytes; the place
// and the corners as float bits; the foundation; the ID) and its list of
// IDs. No address.
uint32_t ObjectsHash(const Vector* objects)
{
    Hash hash;
    hash.Mix(static_cast<uint32_t>(static_cast<std::size_t>(objects->end - objects->begin) /
                                   configuration::kObjectSize));
    for (char* object = objects->begin; object != objects->end; object += configuration::kObjectSize) {
        for (std::size_t offset = 0; offset < kObjectWords; offset += 4)
            hash.Mix(At<uint32_t>(object, offset));
        const uint32_t* const first = At<const uint32_t*>(object, kObjectIds);
        const uint32_t* const last = At<const uint32_t*>(object, kObjectIds + 4);
        hash.Mix(static_cast<uint32_t>(last - first));
        for (const uint32_t* id = first; id != last; ++id)
            hash.Mix(*id);
    }
    return hash.h;
}

// One QueryInterface of a lot: the answer, and that the answer is the lot;
// the reference it gave is released through the lot (the counting branch of
// its Release: the map holds a reference).
void AskLot(Hash& hash, char* networkLot, uint32_t iid)
{
    void* found = nullptr;
    const bool answered = Virtual<uint8_t>(networkLot, lot_slot::kQueryInterface, iid, &found) != 0;
    hash.Mix(answered);
    if (answered && found) {
        hash.Mix(found == networkLot);
        ReleaseOf(found);
    }
}

// What the walk over the city's cells gathers.
struct Cells {
    Bag lots;                // per lot: the cell, the configuration's ID, the objects, the interfaces
    uint32_t forced = 0;     // lots whose configuration was forced
    uint32_t bare = 0;       // lots without a configuration
    uint32_t objects = 0;    // the lots' object counts added up
    uint32_t pieces = 0;     // cells with a piece of a surface network
    uint32_t unpaired = 0;   // of the lots: those whose piece is not the cell's
    Bag zone, wealth;        // on the cells with a piece: (cell, answer)
    uint32_t zoneSum = 0, wealthSum = 0;
    Bag offZone, offWealth;  // on every 7th cell without one
    uint32_t offZoneSum = 0, offWealthSum = 0;
};

uint32_t CellAnswer(uint32_t x, uint32_t z, uint32_t answer)
{
    Hash hash;
    hash.Mix(x);
    hash.Mix(z);
    hash.Mix(answer);
    return hash.h;
}

// Every cell of the city, z then x, the bounds from the city (slots 93 and
// 94). GetNetworkLot (slot 5) for each; for a lot its configuration's ID
// (the configuration's slot 4), its object count (the lot's slot 4), its
// objects (the lot's slot 5; the checksum of a vector is kept by its
// address for the walk), whether it has a piece (the lot's slot 3) and its
// interfaces. The cell's piece of a surface network is asked of the traffic
// network the way UpdateCell asks (the city's slot 40, its slot 8 with the
// mask 0x1F4F and 1: a lookup). GetZoneTypeForNetwork (slot 9) and
// GetWealthForNetwork (slot 10) on every cell with such a piece and on
// every 7th of the others. Both only read: the zone manager's grid, the
// city's size, and for the wealth the lot manager's lot of each neighbour
// cell, three getters of that lot and the building development simulator's
// profile table.
void WalkCells(cISC4City* city, char* manager, Cells& cells, std::vector<uint32_t>& ids)
{
    const uint32_t sizeX = Virtual<uint32_t>(city, city_slot::kCellCountX);
    const uint32_t sizeZ = Virtual<uint32_t>(city, city_slot::kCellCountZ);
    void* const traffic = Virtual<void*>(city, city_slot::kTrafficNetwork);
    std::unordered_map<const void*, uint32_t> objectHashes;
    for (uint32_t z = 0; z < sizeZ; ++z)
        for (uint32_t x = 0; x < sizeX; ++x) {
            void* const piece =
                traffic ? Virtual<void*>(traffic, traffic_slot::kGetPiece, x, z, netlot_sim::kSurfaceMask, 1u)
                        : nullptr;
            if (piece)
                ++cells.pieces;
            if (char* const networkLot = Virtual<char*>(manager, slot::kGetNetworkLot, x, z)) {
                Hash hash;
                hash.Mix(x);
                hash.Mix(z);
                void* const config = At<void*>(networkLot, lot::kConfiguration);
                if (config) {
                    const uint32_t id = ConfigurationId(config);
                    ids.push_back(id);
                    hash.Mix(id);
                } else {
                    ++cells.bare;
                }
                const uint32_t count = Virtual<uint32_t>(networkLot, lot_slot::kGetObjectCount);
                cells.objects += count;
                hash.Mix(count);
                const Vector* const objects = Virtual<const Vector*>(networkLot, lot_slot::kGetObjectArray);
                const auto known = objectHashes.find(objects);
                const uint32_t objectsHash = known != objectHashes.end() ? known->second : ObjectsHash(objects);
                if (known == objectHashes.end())
                    objectHashes.emplace(objects, objectsHash);
                hash.Mix(objectsHash);
                void* const lotPiece = Virtual<void*>(networkLot, lot_slot::kGetNetworkOccupant);
                hash.Mix(lotPiece != nullptr);
                if (lotPiece != piece)
                    ++cells.unpaired;
                const uint8_t forced = At<uint8_t>(networkLot, lot::kForced);
                cells.forced += forced != 0;
                hash.Mix(forced);
                for (const uint32_t iid : {netlot_sim::kIidUnknown, netlot_sim::kIidLot, netlot_sim::kIidManager,
                                           kIidNobody})
                    AskLot(hash, networkLot, iid);
                cells.lots.Add(hash.h);
            }
            const bool sampled = (z * sizeX + x) % kSampleStep == 0;
            if (!piece && !sampled)
                continue;
            const uint32_t zone = Virtual<uint32_t>(manager, slot::kGetZoneTypeForNetwork, x, z);
            const uint32_t wealth = Virtual<uint32_t>(manager, slot::kGetWealthForNetwork, x, z);
            if (piece) {
                cells.zone.Add(CellAnswer(x, z, zone));
                cells.wealth.Add(CellAnswer(x, z, wealth));
                cells.zoneSum += zone;
                cells.wealthSum += wealth;
            } else {
                cells.offZone.Add(CellAnswer(x, z, zone));
                cells.offWealth.Add(CellAnswer(x, z, wealth));
                cells.offZoneSum += zone;
                cells.offWealthSum += wealth;
            }
        }
}

// The callback of the test's own for IterateAllNetworkLots: counts, mixes
// the cell and the lot's configuration ID, and stops after `stopAfter` lots
// when that is not 0.
struct Walk {
    Bag lots;
    uint32_t stopAfter = 0;
};
uint8_t __cdecl OnLot(uint32_t x, uint32_t z, void* networkLot, void* context)
{
    Walk& walk = *static_cast<Walk*>(context);
    Hash hash;
    hash.Mix(x);
    hash.Mix(z);
    void* const config = At<void*>(networkLot, lot::kConfiguration);
    hash.Mix(config ? ConfigurationId(config) : 0xFFFFFFFFu);
    walk.lots.Add(hash.h);
    return walk.stopAfter == 0 || walk.lots.count < walk.stopAfter;
}

// A configuration as its vtable shows it: the ID (slot 4), the network
// piece ID (slot 6) and its objects (slot 3).
uint32_t ConfigurationHash(void* config)
{
    Hash hash;
    hash.Mix(ConfigurationId(config));
    hash.Mix(Virtual<uint32_t>(config, configuration_slot::kGetNetworkPieceId));
    hash.Mix(ObjectsHash(Virtual<const Vector*>(config, configuration_slot::kGetObjectArray)));
    return hash.h;
}

// A bit of one of a configuration's two bit vectors, read as the game's
// 0x00477A40 finds it: the first bit's offset plus the index, divided by 32
// toward zero for the word, the remainder for the bit, and for a negative
// remainder the word before and the bit plus 32.
uint32_t BitOf(char* vector, uint32_t index)
{
    const int32_t at = At<int32_t>(vector, bitvector::kOffset) + static_cast<int32_t>(index);
    const uint32_t* word = At<const uint32_t*>(vector, bitvector::kWords) + at / 32;
    int32_t shift = at % 32;
    if (shift < 0) {
        shift += 32;
        --word;
    }
    return (*word >> shift) & 1u;
}

// The size of a bit vector and its bits (the first 32) as a word.
void MixBits(Hash& hash, char* vector)
{
    const uint32_t size = netlot_sim::BitCount(vector);
    uint32_t bits = 0;
    for (uint32_t i = 0; i < size && i < 32; ++i)
        bits |= BitOf(vector, i) << i;
    hash.Mix(size);
    hash.Mix(bits);
}

// What LoadLotConfiguration (and the constructor before it) left in a
// configuration, read from its fields: the ID, the name's length and
// characters, the network piece ID, the four mask bytes, the period, the
// zone types' and the wealths' bit vectors, the two slopes as float bits,
// the piece mask, the rule byte and the number of objects. The bytes the
// constructor never writes (+0x31 to +0x33, +0x66, +0x67) are not read.
uint32_t LoadedHash(char* config)
{
    Hash hash;
    hash.Mix(At<uint32_t>(config, configuration::kId));
    const auto* const name = reinterpret_cast<const netlot_sim::GameString*>(config + configuration::kName);
    hash.Mix(static_cast<uint32_t>(name->end - name->begin));
    for (const char* at = name->begin; at != name->end; ++at)
        hash.Mix(static_cast<uint8_t>(*at));
    hash.Mix(At<uint32_t>(config, configuration::kPieceId));
    for (std::size_t i = 0; i < configuration::kCellMaskCount; ++i)
        hash.Mix(At<uint8_t>(config, configuration::kCellMasks + i));
    hash.Mix(At<uint8_t>(config, configuration::kPeriod));
    MixBits(hash, config + configuration::kZones);
    MixBits(hash, config + configuration::kWealths);
    hash.Mix(At<uint32_t>(config, configuration::kLeastSlope));
    hash.Mix(At<uint32_t>(config, configuration::kGreatestSlope));
    hash.Mix(At<uint8_t>(config, configuration::kPieceMask));
    hash.Mix(At<uint8_t>(config, configuration::kPieceFlagRule));
    const Vector* const objects = reinterpret_cast<const Vector*>(config + configuration::kObjects);
    hash.Mix(static_cast<uint32_t>(static_cast<std::size_t>(objects->end - objects->begin) /
                                   configuration::kObjectSize));
    return hash.h;
}

// GetNetworkLotConfigurationById (slot 7) for every ID of `ids` (sorted,
// each once) and for two that are not among them: how many of each kind it
// knew. Its second argument is never read: a word of the test's own, which
// must come back as it went.
void AskIds(char* manager, std::vector<uint32_t>& ids, uint32_t& known, uint32_t& strangers, uint32_t& untouched)
{
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    uint32_t word = 0x5EEDF00D;
    known = 0;
    for (const uint32_t id : ids)
        known += Virtual<uint8_t>(manager, slot::kGetNetworkLotConfigurationById, id, &word) != 0;
    strangers = 0;
    uint32_t stranger = kIidNobody;
    for (int i = 0; i < 2; ++i) {
        while (std::binary_search(ids.begin(), ids.end(), stranger))
            ++stranger;
        strangers += Virtual<uint8_t>(manager, slot::kGetNetworkLotConfigurationById, stranger, &word) != 0;
        stranger += 0x01000193;
    }
    untouched = word == 0x5EEDF00D;
}

// --- OPENSC4_TEST_NETLOT -------------------------------------------------------------

bool g_ran = false;
bool g_driven = false;
uint32_t g_setSum = 0;

uint32_t References(void* config)
{
    return At<uint32_t>(config, configuration::kRefCount);
}

uint32_t Bit(bool set, uint32_t number)
{
    return static_cast<uint32_t>(set) << number;
}

// --- the second part: life and save ---------------------------------------------------

// An output stream that keeps the bytes written to it, as
// src/core/netmgr_autotest.cpp's. It answers no interface, so the game's
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

// An input stream over a few bytes, as src/core/netmgr_autotest.cpp's: a
// read past the end stores nothing, fails and leaves the stream in error. It
// answers no interface, so SC4Stream::ReadReference (0x005D9730) clears the
// pointer it is to fill, reads nothing and fails.
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

uint32_t TableCount(char* manager, std::size_t table)
{
    return At<uint32_t>(manager, table + hashtable::kCount);
}

// The lots of the lot map that have no configuration (read from the table's
// nodes). Write asks every lot's configuration for its ID, so a manager with
// such a lot cannot be written, by the game's function or by ours.
uint32_t BareLots(char* manager)
{
    char* const table = manager + field::kLots;
    uint32_t bare = 0;
    for (char* node = netlot_sim::FirstNode(table); node; node = netlot_sim::NextNode16(table, node))
        bare += At<void*>(At<char*>(node, hashnode::kValue), lot::kConfiguration) == nullptr;
    return bare;
}

// Write (the serializable part's slot 3) of a manager into a stream of the
// test's own, once into a stream without an error and once into one that
// reports one: the answer (1, then 0), the length and the bytes. With such a
// stream a reference writes nothing (ByteOStream), so the record is the
// version, the lot count, per lot the cell key and the configuration's ID,
// the pothole count and per pothole the cell key: 10 bytes, 6 per lot and 2
// per pothole, in the hash tables' order. Nothing of the manager, the lots
// or the pieces is changed.
uint32_t WriteTest(char* object, uint32_t& length)
{
    Hash hash;
    for (const int32_t error : {0, 1}) {
        ByteOStream out;
        out.error = error;
        cIGZOStream* const stream = &out;
        hash.Mix(Virtual<uint8_t>(object + netlot_sim::kSerializablePart, slot::kWrite, static_cast<void*>(stream)));
        hash.Mix(static_cast<uint32_t>(out.bytes.size()));
        for (const uint8_t byte : out.bytes)
            hash.Mix(byte);
        length = static_cast<uint32_t>(out.bytes.size());
    }
    return hash.h;
}

// Read (the serializable part's slot 4) on a manager of the test's own, with
// streams that build nothing:
//   - an empty stream (the version's slot then keeps the low word of the
//     stream's address, a multiple of four, so never 2), version 1 and
//     version 3: AL 0 after the one uint16;
//   - version 2 and a count of 0, then nothing: the pothole count's read
//     fails, its word still holds the 0 of the lot count, and the stream is
//     in error: AL 0, six bytes taken;
//   - version 2, a count of 0 and a pothole count of 0: AL 1, ten bytes
//     taken, nothing read into the maps;
//   - the same bytes in a stream that is in error before the call: Read does
//     not ask first, takes the ten bytes and answers 0;
//   - an empty stream that is in error: AL 0.
// NOT fed: version 2 and nothing else. The game reads its count into a stack
// word it never set, so on a stream that ends there it would build as many
// lots as that word says.
// The answer, the bytes taken, the stream's error and the counts of the lot
// map and of the pothole map (0 throughout) are mixed.
void ReadRefusals(Hash& hash, char* object)
{
    struct Case {
        std::vector<uint8_t> bytes;
        int32_t error;
    };
    const Case cases[] = {{{}, 0},
                          {{1, 0, 0xEE, 0xEE}, 0},
                          {{3, 0, 0, 0, 0, 0}, 0},
                          {{2, 0, 0, 0, 0, 0}, 0},
                          {{2, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 0},
                          {{2, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 1},
                          {{}, 1}};
    for (const Case& one : cases) {
        ByteIStream in(one.bytes);
        in.error = one.error;
        cIGZIStream* const stream = &in;
        hash.Mix(Virtual<uint8_t>(object + netlot_sim::kSerializablePart, slot::kRead, static_cast<void*>(stream)));
        hash.Mix(static_cast<uint32_t>(in.at));
        hash.Mix(static_cast<uint32_t>(in.error));
        hash.Mix(TableCount(object, field::kLots));
        hash.Mix(TableCount(object, field::kPotholes));
    }
}

// Read of a record with one lot and one pothole, on the manager of the
// test's own: version 2; one lot at cell (0x34, 0x12) with the
// configuration ID 0xAABBCCDD; one pothole at cell (0x34, 0x56). The stream
// answers no interface, so neither reference is read (ReadReference clears
// the lot's piece and the pothole's pointer and takes no byte), and the
// record is 18 bytes. Mixed: the answer (1), the bytes taken, the stream's
// state, both maps' counts (1 and 1); GetNetworkLot (slot 5) of the cell and
// of the cell with x and z swapped (null); of the lot Read built: whether
// its vtable is the game's lot vtable, whether it has a piece and a
// configuration (neither), the saved ID, whether it holds exactly the one
// reference Read gave it, and the forced byte; of the pothole map's one
// node: the key and whether its occupant is null.
//
// Then the lot is released (its count is 1, so its deleting destructor runs
// with nothing to release), which gives back what Read allocated. The lot
// map's node then points at freed memory; the manager's destructor frees the
// nodes without looking at what they hold (0x008EBA60), and Shutdown, which
// would release the lots, does nothing on a manager that was never
// initialised. Write is not called after this (it would ask the null
// configuration for its ID).
void ReadOne(Hash& hash, char* object)
{
    ByteIStream in({2, 0, 1, 0, 0, 0, 0x12, 0x34, 0xDD, 0xCC, 0xBB, 0xAA, 1, 0, 0, 0, 0x56, 0x34});
    cIGZIStream* const stream = &in;
    hash.Mix(Virtual<uint8_t>(object + netlot_sim::kSerializablePart, slot::kRead, static_cast<void*>(stream)));
    hash.Mix(static_cast<uint32_t>(in.at));
    hash.Mix(static_cast<uint32_t>(in.error));
    hash.Mix(TableCount(object, field::kLots));
    hash.Mix(TableCount(object, field::kPotholes));
    char* const networkLot = Virtual<char*>(object, slot::kGetNetworkLot, 0x34u, 0x12u);
    hash.Mix(networkLot != nullptr);
    hash.Mix(Virtual<void*>(object, slot::kGetNetworkLot, 0x12u, 0x34u) != nullptr);
    char* const potholes = object + field::kPotholes;
    if (char* const node = netlot_sim::FirstNode(potholes)) {
        hash.Mix(At<uint16_t>(node, hashnode::kKey));
        hash.Mix(At<void*>(node, hashnode::kValue) == nullptr);
    }
    if (!networkLot)
        return;
    hash.Mix(At<std::uintptr_t>(networkLot, 0x0) == addr::cSC4NetworkLot_vftable);
    hash.Mix(At<void*>(networkLot, lot::kOccupant) != nullptr);
    hash.Mix(At<void*>(networkLot, lot::kConfiguration) != nullptr);
    hash.Mix(At<uint32_t>(networkLot, lot::kSavedConfigurationId));
    hash.Mix(At<uint32_t>(networkLot, lot::kRefCount) == 1);
    hash.Mix(At<uint8_t>(networkLot, lot::kForced));
    ReleaseOf(networkLot);
}

// A second manager, made by the class's factory (0x0060F4F0, at its address)
// and destroyed again. Safe by the asm: the constructor makes its four hash
// tables and nothing else (no subscription, no service asked), and the
// destructor frees the tables' memory and releases the forced configuration
// (none); it does not call Shutdown. Init is NOT called on it (that would
// subscribe it and load every configuration again). Mixed:
//   - as constructed: the initialised byte, whether a configuration is
//     forced, the four tables' counts and bucket counts, the cell iterator
//     (the state byte, the current cell, the step, the rectangle), and
//     whether its three vtable pointers are the city's manager's;
//   - the identity through its three parts (IdentityHash);
//   - Shutdown (slot 4) on it: AL 1 and the initialised byte still 0;
//   - Write of the empty object (WriteTest: ten bytes);
//   - Read (ReadRefusals, then ReadOne);
//   - the last Release's answer (0: the deleting destructor ran).
uint32_t SecondObjectTest(char* manager, bool& made)
{
    Hash hash;
    char* const second = CallCdecl<char*>(addr::cSC4NetworkLotManager_Create);
    made = second != nullptr;
    hash.Mix(made);
    if (!second)
        return hash.h;
    Virtual<uint32_t>(second, slot::kAddRef);
    hash.Mix(At<uint8_t>(second, field::kInitialized));
    hash.Mix(At<void*>(second, field::kForced) != nullptr);
    for (const std::size_t table : {field::kLots, field::kConfigurations, field::kPotholes, field::kPending}) {
        hash.Mix(TableCount(second, table));
        hash.Mix(netlot_sim::BucketCount(second + table));
    }
    char* const cells = second + field::kIterator;
    hash.Mix(At<uint8_t>(cells, iterator::kState));
    for (const std::size_t word : {iterator::kX, iterator::kZ, iterator::kStep, iterator::kLowX, iterator::kLowZ,
                                   iterator::kHighX, iterator::kHighZ})
        hash.Mix(At<uint32_t>(cells, word));
    for (const std::size_t part : {std::size_t{0}, netlot_sim::kMessagePart, netlot_sim::kSerializablePart})
        hash.Mix(At<std::uintptr_t>(second, part) == At<std::uintptr_t>(manager, part));
    hash.Mix(IdentityHash(second));
    hash.Mix(Virtual<uint8_t>(second, slot::kShutdown));
    hash.Mix(At<uint8_t>(second, field::kInitialized));
    uint32_t length = 0;
    hash.Mix(WriteTest(second, length));
    hash.Mix(length);
    ReadRefusals(hash, second);
    ReadOne(hash, second);
    hash.Mix(Virtual<uint32_t>(second, slot::kRelease));
    return hash.h;
}

// The second part's calls of the drive, with a checksum (the fingerprint's
// ` life` field, after ` set`) and a log line of their own: Init (slot 3) on
// the city's manager, which is initialised and so answers 1 and does
// nothing (the four tables' counts are the same after it); Write of the
// city's manager into a stream of the test's own (WriteTest; left out, and
// said so, when a lot has no configuration); and, unless
// OPENSC4_TEST_NETLOT_OBJECT=0, a second manager made, asked, written, read
// into and destroyed (SecondObjectTest). Not called: Shutdown on the city's
// manager, and Init on the second one.
uint32_t g_lifeSum = 0;

void DriveLife(char* manager)
{
    Hash life;
    uint32_t before = 0, after = 0;
    for (const std::size_t table : {field::kLots, field::kConfigurations, field::kPotholes, field::kPending})
        before = before * 31 + TableCount(manager, table);
    const uint8_t again = Virtual<uint8_t>(manager, slot::kInit);
    for (const std::size_t table : {field::kLots, field::kConfigurations, field::kPotholes, field::kPending})
        after = after * 31 + TableCount(manager, table);
    life.Mix(again);
    life.Mix(At<uint8_t>(manager, field::kInitialized));
    life.Mix(before == after);
    const uint32_t bare = BareLots(manager);
    life.Mix(bare);
    uint32_t length = 0, written = 0;
    if (bare == 0) {
        written = WriteTest(manager, length);
        life.Mix(written);
        life.Mix(length);
    }
    const bool object = EnvInt("OPENSC4_TEST_NETLOT_OBJECT", 1) != 0;
    bool second = false;
    uint32_t secondSum = 0;
    if (object) {
        secondSum = SecondObjectTest(manager, second);
        life.Mix(secondSum);
    }
    g_lifeSum = life.h;
    log::Write("autotest: netlot: life: Init again %u (tables %s), write %08X (%u bytes for %u lots and %u "
               "potholes%s), second object %08X (%s), life %08X",
               again, before == after ? "as before" : "CHANGED", written, length,
               TableCount(manager, field::kLots), TableCount(manager, field::kPotholes),
               bare == 0 ? "" : "; NOT written: a lot has no configuration", secondSum,
               !object ? "left out" : second ? "made, written, read into and destroyed" : "not made", g_lifeSum);
}

// OPENSC4_TEST_NETLOT=<day>: once. GetNetworkLotConfigurationSet twice into
// one set (the second call empties a set that is not empty), then
// SetLotConfigurationForNextPiece (slot 13) with the configuration of the
// least ID, with the same again (nothing), with null, with null again, and
// with what was there before, which is kept alive by a reference of the
// test's own meanwhile. After each call: whether the manager's word at
// +0x34 is what was set, and how far the configuration's reference count is
// from where it began (never the count itself). The function references
// the new one and releases the old one, so every count ends where it began.
// Then the second part's calls (DriveLife).
void Drive(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_NETLOT") || !game::IsBuild641() || g_ran)
        return;
    g_ran = true;
    char* const manager = Manager(day.city);
    if (!manager) {
        log::Write("autotest: netlot: nothing driven (no manager)");
        return;
    }
    Hash hash;
    std::vector<void*> configs;
    SetCounts counts;
    const bool collected = CollectConfigurations(manager, configs, 2, counts);
    hash.Mix(collected);
    hash.Mix(counts.first);
    hash.Mix(counts.second);
    hash.Mix(static_cast<uint32_t>(configs.size()));
    // Two references each from the two calls: one is given back here, the
    // other holds the configurations until the end.
    for (void* const config : configs)
        ReleaseOf(config);
    void* chosen = nullptr;
    for (void* const config : configs)
        if (!chosen || ConfigurationId(config) < ConfigurationId(chosen))
            chosen = config;
    void* const before = At<void*>(manager, field::kForced);
    if (before)
        AddRefOf(before);
    uint32_t steps = 0;
    int32_t afterSet = 0, afterAgain = 0, afterNull = 0;
    if (chosen) {
        const uint32_t began = References(chosen);
        Virtual<void>(manager, slot::kSetLotConfigurationForNextPiece, chosen);
        steps |= Bit(At<void*>(manager, field::kForced) == chosen, 0);
        afterSet = static_cast<int32_t>(References(chosen) - began);
        Virtual<void>(manager, slot::kSetLotConfigurationForNextPiece, chosen);
        steps |= Bit(At<void*>(manager, field::kForced) == chosen, 1);
        afterAgain = static_cast<int32_t>(References(chosen) - began);
        Virtual<void>(manager, slot::kSetLotConfigurationForNextPiece, static_cast<void*>(nullptr));
        steps |= Bit(At<void*>(manager, field::kForced) == nullptr, 2);
        afterNull = static_cast<int32_t>(References(chosen) - began);
        hash.Mix(ConfigurationId(chosen));
    } else {
        Virtual<void>(manager, slot::kSetLotConfigurationForNextPiece, static_cast<void*>(nullptr));
        steps |= Bit(At<void*>(manager, field::kForced) == nullptr, 2);
    }
    Virtual<void>(manager, slot::kSetLotConfigurationForNextPiece, static_cast<void*>(nullptr));
    steps |= Bit(At<void*>(manager, field::kForced) == nullptr, 3);
    Virtual<void>(manager, slot::kSetLotConfigurationForNextPiece, before);
    steps |= Bit(At<void*>(manager, field::kForced) == before, 4);
    if (before)
        ReleaseOf(before);
    hash.Mix(steps);
    hash.Mix(static_cast<uint32_t>(afterSet));
    hash.Mix(static_cast<uint32_t>(afterAgain));
    hash.Mix(static_cast<uint32_t>(afterNull));
    hash.Mix(before != nullptr);
    const uint32_t chosenId = chosen ? ConfigurationId(chosen) : 0;
    for (void* const config : configs)
        ReleaseOf(config);
    g_setSum = hash.h;
    g_driven = true;
    log::Write("autotest: netlot: set %08X (the set %s: %u configurations, %u after a second call; forced before: "
               "%s; configuration %08X set: steps %02X, references %+d, again %+d, after null %+d)",
               g_setSum, collected ? "made" : "not made", counts.first, counts.second, before ? "one" : "none",
               chosenId, steps, afterSet, afterAgain, afterNull);
    DriveLife(manager);
}

// The fingerprint part, everything through the manager's vtable (the game's
// functions or ours) or read from its fields, nothing of it an address or a
// reference count:
//   st     the byte at +0x70 (initialised) and whether a configuration is
//          forced for the next piece (+0x34)
//   id     IdentityHash
//   maps   the counts of the lot map, the configuration map (piece IDs),
//          the pothole map and the waiting set
//   lots   GetNetworkLot over every cell: how many cells have a lot, the
//          sum and the exclusive or of the per-lot checksums, the lots that
//          are forced, without a configuration, whose piece is not the
//          cell's, and the object counts added up
//   net    the cells with a piece of a surface network; zone and wealth:
//          GetZoneTypeForNetwork and GetWealthForNetwork on them (the sum
//          of the answers, the sum and the exclusive or of the per-cell
//          checksums); off: the same on every 7th cell without a piece
//          (the number of cells first)
//   iter   IterateAllNetworkLots: the lots it gave, the sum and exclusive
//          or of (x, z, configuration ID), its answer; then the walk that
//          stops after 3: the lots it gave and its answer
//   cfg    GetNetworkLotConfigurationSet: the set's count, the
//          configurations walked, the sum and the exclusive or of their
//          checksums (each released once)
//   load   the same configurations' loaded fields (LoadedHash: what the
//          constructor and LoadLotConfiguration left): how many, the sum
//          and the exclusive or
//   byid   GetNetworkLotConfigurationById: the IDs asked (of the lots and
//          of the set), how many it knew, how many of two strangers it
//          knew, and whether its second argument's word was left alone
// and after OPENSC4_TEST_NETLOT's day ` set <checksum> life <checksum>`
// (` set none` when the drive found no manager).
void Fingerprint(cISC4City* city, std::string& line)
{
    char* const manager = Manager(city);
    if (!manager) {
        line += " | netlot none";
        return;
    }
    Cells cells;
    std::vector<uint32_t> ids;
    WalkCells(city, manager, cells, ids);

    Walk all, some;
    some.stopAfter = kStopAfter;
    const uint32_t allAnswer = Virtual<uint8_t>(manager, slot::kIterateAllNetworkLots, &OnLot, &all);
    const uint32_t someAnswer = Virtual<uint8_t>(manager, slot::kIterateAllNetworkLots, &OnLot, &some);

    std::vector<void*> configs;
    SetCounts counts;
    Bag set, loaded;
    if (CollectConfigurations(manager, configs, 1, counts))
        for (void* const config : configs) {
            ids.push_back(ConfigurationId(config));
            set.Add(ConfigurationHash(config));
            loaded.Add(LoadedHash(static_cast<char*>(config)));
            ReleaseOf(config);
        }
    uint32_t known = 0, strangers = 0, untouched = 0;
    AskIds(manager, ids, known, strangers, untouched);

    char text[768];
    std::snprintf(text, sizeof text,
                  " | netlot st %u%u id %08X maps %u,%u,%u,%u lots %u,%08X,%08X,%u,%u,%u,%u net %u zone %u,%08X,%08X "
                  "wealth %u,%08X,%08X off %u,%u,%08X,%08X,%u,%08X,%08X iter %u,%08X,%08X,%u,%u,%u cfg %u,%u,%08X,%08X "
                  "load %u,%08X,%08X byid %u,%u,%u,%u",
                  At<uint8_t>(manager, field::kInitialized), Bit(At<void*>(manager, field::kForced) != nullptr, 0),
                  IdentityHash(manager), At<uint32_t>(manager, field::kLots + hashtable::kCount),
                  At<uint32_t>(manager, field::kConfigurations + hashtable::kCount),
                  At<uint32_t>(manager, field::kPotholes + hashtable::kCount),
                  At<uint32_t>(manager, field::kPending + hashtable::kCount), cells.lots.count, cells.lots.sum,
                  cells.lots.mixed, cells.forced, cells.bare, cells.unpaired, cells.objects, cells.pieces,
                  cells.zoneSum, cells.zone.sum, cells.zone.mixed, cells.wealthSum, cells.wealth.sum,
                  cells.wealth.mixed, cells.offZone.count, cells.offZoneSum, cells.offZone.sum, cells.offZone.mixed,
                  cells.offWealthSum, cells.offWealth.sum, cells.offWealth.mixed, all.lots.count, all.lots.sum,
                  all.lots.mixed, allAnswer, some.lots.count, someAnswer, counts.first, set.count, set.sum, set.mixed,
                  loaded.count, loaded.sum, loaded.mixed, static_cast<unsigned>(ids.size()), known, strangers,
                  untouched);
    line += text;
    if (g_ran) {
        if (g_driven)
            std::snprintf(text, sizeof text, " set %08X life %08X", g_setSum, g_lifeSum);
        else
            std::snprintf(text, sizeof text, " set none");
        line += text;
    }
}

const FingerprintPart kFingerprint{"netlot", 132, &Fingerprint};
const DayHook kDrive{"netlot.drive", 195, &Drive};

} // namespace
} // namespace opensc4::autotest
