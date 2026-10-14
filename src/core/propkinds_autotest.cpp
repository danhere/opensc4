// The terrain decal's and the visual effect prop's part of the autotest
// (src/core/autotest_hooks.h; propkinds_decal.cpp, propkinds_effect.cpp, the
// class maps in propkinds.h). Everything goes through the game's objects:
// their vtables, the prop manager and the two classes' allocators and
// constructors by address. Nothing here calls our own functions, so a run
// with simulators.propkinds.* on and one with it off can be compared.
//
//   OPENSC4_TEST_PROPKINDS=<day>   token "propkinds"; sub-results on the log
//     lines "autotest: propkinds: ...". The city is left unchanged:
//     1. the city's decals and effect props, found through the occupant
//        manager's grid (occupant type 0xA823821E and the class ID from
//        GetGZCLSID, confirmed with QueryInterface for the class ID): the
//        counts and the sum and exclusive or of a hash per object over its
//        plain state;
//     2. props the test makes and never inserts: one of each class from the
//        allocator and the constructor (as the terrain code makes a decal),
//        and up to two of each from the prop manager's CreateProp when its
//        tables have types of the class. QueryInterface, GetGZCLSID and the
//        setters that only write fields while there is no overlay and no
//        effect, read back; then released.
//   OPENSC4_TEST_PROPKINDS_EDIT=1  with it, a round trip that changes the
//     city and puts it back: at a spot with no lot whose 64 m occupant cell
//     is empty a decal is initialized, added, shown (base flag 2 through
//     SetAllFlags, as the view does), faded with SetOpacityFade and frame
//     messages the test makes (fixed milliseconds, action 0), hidden, shown
//     and removed; then an effect prop is added, switched through states,
//     shown, stopped, started, turned, given a name and an ID no effect has,
//     and removed. The counts of 1 are taken again and must be the same.
//   OPENSC4_TEST_PROPKINDS_EDIT=2  the same, and then a decal and an effect
//     prop are added and LEFT in the city, for a save that holds them.
//
// Left out on purpose: Write and Read (a save does them), the deleting
// destructors (Release reaches them), fade actions 1 and 2. Left out of the
// hashes because frames or the view drive them: the overlay's ID and
// manager, base flags, the fresh byte of city effects, every pointer.
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/occmgr_sim.h"
#include "core/propkinds.h"

#include <opensc4/addresses.h>

#include <cISC4City.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <unordered_set>
#include <vector>

namespace opensc4::autotest {
namespace {

namespace layout = propkinds::layout;
namespace decal = propkinds::decal;
namespace effect = propkinds::effect;
using propkinds::Cdecl;
using propkinds::This;

constexpr uint32_t kOccupantType = 0xA823821E;
constexpr uint32_t kIidProp = 0xE9793A65;
constexpr uint32_t kIidMessageTarget = 0x452294AA;
constexpr uint32_t kIidOccupant = 0x26BB767E;
constexpr uint32_t kMessageFrame = 0x86AD10EE;
// The texture the terrain code gives its own decal (0x0075EE2C).
constexpr uint32_t kTexture[3] = {0x7AB50E44, 0x891B0E1A, 0x0999D1B7};

// The prop manager's slots used (vtable 0x00A8F928; src/core/propmgr_autotest.cpp).
namespace manager {
constexpr std::size_t kGetAllPropTypes = 0x14, kGetPropKeyFromType = 0x1C, kGetPropProfilePtr = 0x38;
constexpr std::size_t kCreateProp = 0x3C, kCreateGenericProp = 0x40, kAddCityProp = 0x4C, kRemoveCityPropsInArea = 0x58;
constexpr std::size_t kOccupants = 0x14, kLots = 0x20;
} // namespace manager

struct Fnv {
    uint32_t h = 2166136261u;
    void Bytes(const void* data, std::size_t size)
    {
        for (std::size_t i = 0; i < size; ++i)
            h = (h ^ static_cast<const uint8_t*>(data)[i]) * 16777619u;
    }
    template <typename T>
    void Add(const T& value)
    {
        Bytes(&value, sizeof value);
    }
};

char g_token[40];

uint32_t Bits(float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, 4);
    return bits;
}

// The box's size as the bits of three floats.
void AddBoxSize(Fnv& f, char* self)
{
    for (std::size_t i = 0; i < 12; i += 4)
        f.Add(Bits(At<float>(self, layout::kMax + i) - At<float>(self, layout::kMin + i)));
}

// A decal's plain state: the texture key, the size, the centre, the fade's
// end, rate and action, the two bytes and the box's size; and the opacity
// and the fading opacity while no fade runs (rate 0).
uint32_t DecalPlain(char* self)
{
    Fnv f;
    f.Bytes(self + decal::kTexture, 12);
    f.Add(At<uint32_t>(self, decal::kSize));
    f.Bytes(self + decal::kCentre, 8);
    f.Add(At<uint32_t>(self, decal::kFadeEnd));
    f.Add(At<uint32_t>(self, decal::kFadeRate));
    f.Add(At<uint8_t>(self, decal::kFadeAction));
    f.Add(At<uint8_t>(self, decal::kSelfReference));
    f.Add(At<uint8_t>(self, decal::kFrames));
    AddBoxSize(f, self);
    if (At<float>(self, decal::kFadeRate) == 0.0f) {
        f.Add(At<uint32_t>(self, decal::kOpacity));
        f.Add(At<uint32_t>(self, decal::kFadeNow));
    }
    return f.h;
}

// An effect prop's plain state: the name's bytes, the ID, the two property
// counts, the started byte and whether there is an effect object.
uint32_t EffectPlain(char* self)
{
    Fnv f;
    const char* const begin = At<char*>(self, effect::kNameBegin);
    const char* const end = At<char*>(self, effect::kNameEnd);
    const uint32_t length = begin && end >= begin ? static_cast<uint32_t>(end - begin) : 0;
    f.Add(length);
    if (length && length < 4096)
        f.Bytes(begin, length);
    f.Add(At<uint32_t>(self, effect::kEffectId));
    f.Add(At<uint32_t>(self, effect::kNameCount));
    f.Add(At<uint32_t>(self, effect::kIdCount));
    f.Add(At<uint8_t>(self, effect::kStarted));
    f.Add(At<void*>(self, effect::kEffect) != nullptr);
    return f.h;
}

// QueryInterface through the object's own slot 0 for a list of IDs: whether
// it answers and which part came back (as an offset from the object); every
// reference taken is given back.
void AddQueries(Fnv& f, char* self, std::initializer_list<uint32_t> iids)
{
    for (const uint32_t iid : iids) {
        void* found = nullptr;
        const bool answered = Virtual<uint8_t>(self, 0x0, iid, &found) != 0;
        f.Add(answered);
        if (answered && found) {
            f.Add(static_cast<uint32_t>(static_cast<char*>(found) - self));
            Virtual<uint32_t>(found, 0x8);
        }
    }
    f.Add(Virtual<uint32_t>(self + layout::kSerializable, 0x14));
}

// --- 1. The city's decals and effect props ---------------------------------------------

struct CityCount {
    uint32_t decals = 0, effects = 0;
    uint32_t decalSum = 0, decalXor = 0, effectSum = 0, effectXor = 0;
    // Not hashed (frames and the view): decals with an overlay, decals with
    // a fade running, effect props with an effect object, shown, started.
    uint32_t withOverlay = 0, fading = 0, withEffect = 0, shown = 0, started = 0, wrongQuery = 0;
    bool operator==(const CityCount& o) const
    {
        return decals == o.decals && effects == o.effects && decalSum == o.decalSum && decalXor == o.decalXor &&
               effectSum == o.effectSum && effectXor == o.effectXor;
    }
};

CityCount WalkCity(void* city)
{
    CityCount out;
    char* const occupants = Virtual<char*>(city, 0x88);
    if (!occupants || !At<uint8_t>(occupants, occmgr_sim::field::kInitialized))
        return out;
    std::unordered_set<char*> decals, effects;
    Virtual<void>(occupants + occmgr_sim::field::kLock, 0x4);
    const occmgr_sim::Vector* const grid = occmgr_sim::Grid(occupants);
    const int32_t columns = static_cast<int32_t>((grid->end - grid->begin) / 12);
    for (int32_t x = 0; x < columns; ++x) {
        const auto* const column = reinterpret_cast<const occmgr_sim::Vector*>(grid->begin + x * 12);
        const int32_t depth = static_cast<int32_t>((column->end - column->begin) >> 2);
        for (int32_t z = 0; z < depth; ++z) {
            const occmgr_sim::Cell* const cell = occmgr_sim::CellAt(occupants, x, z);
            if (!cell)
                continue;
            for (const occmgr_sim::ListNode* node = cell->head->next; node != cell->head; node = node->next) {
                char* const part = static_cast<char*>(occmgr_sim::OccupantOf(node));
                if (Virtual<uint32_t>(part, 0x1C) != kOccupantType)
                    continue;
                const uint32_t classId = Virtual<uint32_t>(part + 4, 0x14);
                if (classId == decal::kClassId)
                    decals.insert(part - layout::kOccupant);
                else if (classId == effect::kClassId)
                    effects.insert(part - layout::kOccupant);
            }
        }
    }
    Virtual<void>(occupants + occmgr_sim::field::kLock, 0x8);
    const auto confirm = [&out](char* self, uint32_t classId) {
        void* found = nullptr;
        if (Virtual<uint8_t>(self + layout::kOccupant, 0x0, classId, &found) && found) {
            if (found != self)
                ++out.wrongQuery;
            Virtual<uint32_t>(found, 0x8);
        } else {
            ++out.wrongQuery;
        }
    };
    for (char* const self : decals) {
        confirm(self, decal::kClassId);
        const uint32_t hash = DecalPlain(self);
        ++out.decals;
        out.decalSum += hash;
        out.decalXor ^= hash;
        out.withOverlay += At<void*>(self, decal::kOverlays) != nullptr;
        out.fading += At<float>(self, decal::kFadeRate) != 0.0f;
        out.shown += (At<uint8_t>(self, layout::kFlags) & 2) != 0;
    }
    for (char* const self : effects) {
        confirm(self, effect::kClassId);
        const uint32_t hash = EffectPlain(self);
        ++out.effects;
        out.effectSum += hash;
        out.effectXor ^= hash;
        out.withEffect += At<void*>(self, effect::kEffect) != nullptr;
        out.shown += (At<uint8_t>(self, layout::kFlags) & 2) != 0;
        out.started += At<uint8_t>(self, effect::kStarted) != 0;
    }
    return out;
}

// --- 2. Made props -----------------------------------------------------------------------

// A decal or an effect prop from the class's allocator and constructor, as
// the terrain code makes its decal; one reference, the caller's.
char* MakeDirect(bool isDecal)
{
    void* const memory = isDecal ? Cdecl<void*>(addr::cSC4PropOccupantTerrainDecal_Allocate, std::size_t{decal::kObjectSize})
                                 : Cdecl<void*>(addr::cSC4PropOccupantVisualEffect_Allocate, std::size_t{effect::kObjectSize});
    if (!memory)
        return nullptr;
    char* const self = static_cast<char*>(isDecal ? This<void*>(addr::cSC4PropOccupantTerrainDecal_Construct, memory)
                                                  : This<void*>(addr::cSC4PropOccupantVisualEffect_Construct, memory));
    if (self)
        Virtual<uint32_t>(self, 0x4);
    return self;
}

// The setters of a decal that only write fields while it has no overlay.
uint32_t DriveDecalFields(char* self)
{
    Fnv f;
    AddQueries(f, self, {kIidMessageTarget, decal::kIid, decal::kClassId, kIidProp, kIidOccupant, 1u, 0x12345678u});
    f.Add(DecalPlain(self));
    f.Add(At<uint32_t>(self, decal::kOverlayId));
    f.Add(At<void*>(self, decal::kOverlays) != nullptr);
    Virtual<void>(self, decal::slot::kSetDecalTexture, static_cast<const uint32_t*>(kTexture), 48.0f);
    f.Add(DecalPlain(self));
    f.Bytes(self + layout::kMin, 24);
    Virtual<void>(self, decal::slot::kSetOpacity, 0.5f);
    f.Add(At<uint32_t>(self, decal::kOpacity));
    Virtual<void>(self, decal::slot::kSetDecalSize, 24.0f);
    f.Add(DecalPlain(self));
    const float position[3] = {1000.0f, 271.5f, 2000.25f};
    Virtual<void>(self + layout::kOccupant, 0x24, static_cast<const float*>(position));
    f.Add(DecalPlain(self));
    f.Bytes(self + layout::kMin, 24);
    float back[3] = {0, 0, 0};
    Virtual<void>(self + layout::kOccupant, 0x20, static_cast<float*>(back));
    f.Bytes(back, sizeof back);
    // A change of a flag other than 2 goes to the base alone.
    const uint32_t flags = At<uint8_t>(self, layout::kFlags);
    Virtual<uint32_t>(self + layout::kOccupant, 0x78, flags ^ 0x40u);
    f.Add(static_cast<uint8_t>(At<uint8_t>(self, layout::kFlags) ^ flags));
    Virtual<uint32_t>(self + layout::kOccupant, 0x78, flags);
    f.Add(At<void*>(self, decal::kOverlays) != nullptr);
    f.Add(At<uint8_t>(self, decal::kFrames));
    return f.h;
}

// The same for an effect prop: while it has no effect and no name, ID or
// properties counted, nothing asks the effects manager.
uint32_t DriveEffectFields(char* self)
{
    Fnv f;
    AddQueries(f, self, {effect::kIid, effect::kClassId, kIidMessageTarget, kIidProp, kIidOccupant, 1u, 0x12345678u});
    f.Add(static_cast<uint32_t>(Virtual<char*>(self, 0x0C) - self));
    f.Add(static_cast<uint32_t>(Virtual<char*>(self, 0x10) - self));
    f.Add(EffectPlain(self));
    f.Add(At<uint8_t>(self, effect::kFresh));
    const bool quiet = At<char*>(self, effect::kNameBegin) == At<char*>(self, effect::kNameEnd) &&
                       !At<uint32_t>(self, effect::kEffectId) && !At<uint32_t>(self, effect::kNameCount) &&
                       !At<uint32_t>(self, effect::kIdCount);
    f.Add(quiet);
    f.Add(Virtual<uint8_t>(self, effect::slot::kStopEffect, 1u));
    f.Add(At<uint8_t>(self, effect::kStarted));
    f.Add(Virtual<uint8_t>(self, effect::slot::kStartEffect, 1u));
    f.Add(At<uint8_t>(self, effect::kStarted));
    f.Add(static_cast<uint8_t>(Virtual<uint32_t>(self + layout::kProp, 0x28, int32_t{7})));
    f.Add(At<uint8_t>(self, layout::kOrientation));
    f.Add(static_cast<uint8_t>(Virtual<uint32_t>(self + layout::kProp, 0x28, int32_t{-2})));
    f.Add(At<uint8_t>(self, layout::kOrientation));
    const float position[3] = {1000.0f, 271.5f, 2000.25f};
    Virtual<void>(self + layout::kOccupant, 0x24, static_cast<const float*>(position));
    f.Bytes(self + layout::kMin, 24);
    if (quiet) {
        // A new state with nothing to make an effect from: the byte, and
        // the started byte set.
        At<uint8_t>(self, effect::kStarted) = 0;
        Virtual<void>(self + layout::kProp, 0x2C, uint32_t{At<uint8_t>(self, layout::kState)} ^ 1u);
        f.Add(At<uint8_t>(self, layout::kState));
        f.Add(At<uint8_t>(self, effect::kStarted));
        // The ID the object has: nothing.
        Virtual<void>(self, effect::slot::kSetEffectTypeById, uint32_t{0});
    }
    const uint32_t flags = At<uint8_t>(self, layout::kFlags);
    Virtual<uint32_t>(self + layout::kOccupant, 0x78, flags ^ 0x40u);
    f.Add(static_cast<uint8_t>(At<uint8_t>(self, layout::kFlags) ^ flags));
    Virtual<uint32_t>(self + layout::kOccupant, 0x78, flags);
    f.Add(EffectPlain(self));
    return f.h;
}

// A vector of uint32 as the game's.
struct Vec {
    uint32_t* begin = nullptr;
    uint32_t* end = nullptr;
    uint32_t* capacity = nullptr;
};

// The prop manager's types whose profile names the class, sorted.
struct Types {
    std::vector<uint32_t> decals, effects;
};

Types TypesOf(void* m)
{
    Types out;
    Vec all;
    Virtual<uint8_t>(m, manager::kGetAllPropTypes, &all);
    std::vector<uint32_t> sorted;
    for (const uint32_t* p = all.begin; p && p != all.end && sorted.size() < 1000000; ++p)
        sorted.push_back(*p);
    This<void>(addr::SC4OccupantVector_Destroy, &all);
    std::sort(sorted.begin(), sorted.end());
    for (const uint32_t type : sorted) {
        const uint32_t* const profile = Virtual<const uint32_t*>(m, manager::kGetPropProfilePtr, type);
        if (!profile)
            continue;
        if (profile[0] == decal::kClassId)
            out.decals.push_back(type);
        else if (profile[0] == effect::kClassId)
            out.effects.push_back(type);
    }
    return out;
}

// The object of a prop the manager made (it hands out the prop interface,
// object + 4), when its class is the one wanted.
char* ObjectOf(void* prop, uint32_t classId)
{
    if (!prop)
        return nullptr;
    char* const self = static_cast<char*>(prop) - layout::kProp;
    return Virtual<uint32_t>(self + layout::kSerializable, 0x14) == classId ? self : nullptr;
}

void DriveMade(Fnv& f, void* m, const Types& types)
{
    for (const bool isDecal : {true, false}) {
        char* const self = MakeDirect(isDecal);
        const uint32_t h = !self ? 0xEEEEEEEEu : isDecal ? DriveDecalFields(self) : DriveEffectFields(self);
        if (self)
            Virtual<uint32_t>(self, 0x8);
        f.Add(h);
        log::Write("autotest: propkinds: made direct %s: %08X", isDecal ? "decal" : "effect", h);
    }
    if (!m)
        return;
    for (const bool isDecal : {true, false}) {
        const std::vector<uint32_t>& list = isDecal ? types.decals : types.effects;
        for (std::size_t i = 0; i < list.size() && i < 2; ++i) {
            void* prop = nullptr;
            const uint8_t made = Virtual<uint8_t>(m, manager::kCreateProp, list[i], &prop);
            char* const self = ObjectOf(prop, isDecal ? decal::kClassId : effect::kClassId);
            Fnv mine;
            mine.Add(made);
            mine.Add(self != nullptr);
            if (self)
                mine.Add(isDecal ? DriveDecalFields(self) : DriveEffectFields(self));
            if (prop)
                Virtual<uint32_t>(prop, 0x8);
            f.Add(mine.h);
            log::Write("autotest: propkinds: made %s type %08X: %08X", isDecal ? "decal" : "effect", list[i], mine.h);
        }
    }
}

// --- The round trip ----------------------------------------------------------------------

// A cell with no lot whose 64 m occupant cell is empty (the flammability
// simulator recalculates where occupants come and go), from a quarter of
// the city on.
bool FindSpot(void* m, int32_t sizeX, int32_t sizeZ, int32_t* spotX, int32_t* spotZ)
{
    void* const lots = At<void*>(m, manager::kLots);
    void* const occupants = At<void*>(m, manager::kOccupants);
    if (!lots || !occupants)
        return false;
    for (int32_t z = sizeZ / 4; z < sizeZ * 3 / 4; ++z)
        for (int32_t x = sizeX / 4; x < sizeX * 3 / 4; ++x) {
            if (Virtual<void*>(lots, 0x18, x, z, uint32_t{0}))
                continue;
            if (!(Virtual<uint8_t>(occupants, 0x64, x / 4 + 64, z / 4 + 64) & 1))
                continue;
            *spotX = x;
            *spotZ = z;
            return true;
        }
    return false;
}

// A prop of the class for the round trip: from the first type's key through
// CreateGenericProp when the tables have a type, else from the allocator
// and the constructor. One reference, the caller's.
char* MakeForEdit(void* m, const std::vector<uint32_t>& types, bool isDecal, const char** how)
{
    if (!types.empty()) {
        uint32_t key[3] = {};
        if (Virtual<uint8_t>(m, manager::kGetPropKeyFromType, types[0], key)) {
            void* prop = nullptr;
            if (Virtual<uint8_t>(m, manager::kCreateGenericProp, static_cast<const uint32_t*>(key), &prop) && prop) {
                if (char* const self = ObjectOf(prop, isDecal ? decal::kClassId : effect::kClassId)) {
                    *how = "CreateGenericProp";
                    return self;
                }
                Virtual<uint32_t>(prop, 0x8);
            }
        }
    }
    *how = "constructor";
    return MakeDirect(isDecal);
}

// What the removal left of a prop the test still holds.
void AddRemoved(Fnv& f, char* self)
{
    f.Add(Virtual<uint8_t>(self + layout::kOccupant, 0x14));
    f.Add(static_cast<uint8_t>(At<uint8_t>(self, layout::kFlags) & 1));
}

void EditDecal(Fnv& f, void* m, const Types& types, const float* position, const int32_t* cells)
{
    const char* how = "";
    char* const self = MakeForEdit(m, types.decals, true, &how);
    if (!self) {
        f.Add(uint8_t{0xEE});
        log::Write("autotest: propkinds: edit decal: none made");
        return;
    }
    char* const occupant = self + layout::kOccupant;
    Fnv mine;
    // Init first: a new decal's Init puts the box back around the origin.
    mine.Add(Virtual<uint8_t>(occupant, 0x0C));
    mine.Add(DecalPlain(self));
    Virtual<void>(self, decal::slot::kSetDecalTexture, static_cast<const uint32_t*>(kTexture), 12.0f);
    Virtual<void>(self, decal::slot::kSetOpacity, 0.75f);
    Virtual<void>(occupant, 0x24, position);
    Virtual<void>(self + layout::kProp, 0x28, int32_t{0});
    const uint8_t added = Virtual<uint8_t>(m, manager::kAddCityProp, static_cast<void*>(occupant));
    mine.Add(added);
    mine.Add(DecalPlain(self));
    mine.Add(Virtual<uint8_t>(occupant, 0x14));
    mine.Add(static_cast<uint8_t>(At<uint8_t>(self, layout::kFlags) & 1));
    mine.Add(Virtual<uint8_t>(self + layout::kProp, 0x58));
    const uint32_t afterAdd = mine.h;
    // Shown, as the view does it.
    const bool wasShown = (At<uint8_t>(self, layout::kFlags) & 2) != 0;
    if (!wasShown)
        Virtual<uint32_t>(occupant, 0x78, uint32_t{At<uint8_t>(self, layout::kFlags)} | 2u);
    const bool overlay = At<void*>(self, decal::kOverlays) != nullptr && At<uint32_t>(self, decal::kOverlayId) != 0xFFFFFFFF;
    mine.Add(overlay);
    mine.Add(static_cast<uint8_t>(At<uint8_t>(self, layout::kFlags) & 2));
    // With an overlay the setters also tell the overlay manager.
    Virtual<void>(self, decal::slot::kSetDecalSize, 14.0f);
    Virtual<void>(occupant, 0x24, position);
    Virtual<void>(self, decal::slot::kSetOpacity, 1.0f);
    mine.Add(DecalPlain(self));
    // A fade from 1 to 0.25 in 2 seconds, action 0, stepped by 500 ms frames
    // the test makes: four steps down, then two past the end.
    Virtual<void>(self, decal::slot::kSetOpacityFade, 2.0f, uint32_t{0}, 1.0f, 0.25f);
    mine.Add(DecalPlain(self));
    mine.Add(At<uint32_t>(self, decal::kFadeNow));
    uint32_t steps[6][2] = {};
    if (overlay) {
        for (auto& step : steps) {
            Deliver(self + layout::kProp, kMessageFrame, nullptr, 0, Bits(500.0f));
            step[0] = At<uint32_t>(self, decal::kFadeNow);
            step[1] = At<uint32_t>(self, decal::kOpacity);
            mine.Add(step[0]);
            mine.Add(step[1]);
        }
        // Another message is not the decal's.
        Deliver(self + layout::kProp, 0x12345678, nullptr, 0, Bits(500.0f));
        mine.Add(At<uint32_t>(self, decal::kFadeNow));
        // Hidden and shown again: the overlay stays and is switched.
        Virtual<uint32_t>(occupant, 0x78, uint32_t{At<uint8_t>(self, layout::kFlags)} & ~2u);
        mine.Add(At<void*>(self, decal::kOverlays) != nullptr);
        Virtual<uint32_t>(occupant, 0x78, uint32_t{At<uint8_t>(self, layout::kFlags)} | 2u);
    }
    const uint32_t afterFade = mine.h;
    const uint8_t removed = Virtual<uint8_t>(m, manager::kRemoveCityPropsInArea, cells);
    mine.Add(removed);
    AddRemoved(mine, self);
    mine.Add(At<void*>(self, decal::kOverlays) != nullptr);
    mine.Add(At<uint32_t>(self, decal::kOverlayId));
    mine.Add(At<uint8_t>(self, decal::kFrames));
    mine.Add(At<uint32_t>(self, decal::kTexture + 8));
    const bool stillListening = At<uint8_t>(self, decal::kFrames) != 0;
    log::Write("autotest: propkinds: edit decal (%s): added %u %08X, overlay %d (shown before %d), fade %08X %08X %08X %08X "
               "%08X %08X / opacity %08X %08X %08X %08X %08X %08X, %08X; removed %u, initialized %u, in manager %u, "
               "listening %d",
               how, added, afterAdd, overlay ? 1 : 0, wasShown ? 1 : 0, steps[0][0], steps[1][0], steps[2][0], steps[3][0],
               steps[4][0], steps[5][0], steps[0][1], steps[1][1], steps[2][1], steps[3][1], steps[4][1], steps[5][1],
               afterFade, removed, Virtual<uint8_t>(occupant, 0x14), At<uint8_t>(self, layout::kFlags) & 1,
               stillListening ? 1 : 0);
    if (stillListening) {
        // The removal did not shut the decal down: its Shutdown takes it
        // off the frame service before the reference goes.
        log::Write("autotest: propkinds: edit decal: STILL REGISTERED after the removal; shut down by the test");
        Virtual<uint8_t>(occupant, 0x10);
    }
    Virtual<uint32_t>(self, 0x8);
    f.Add(mine.h);
}

void EditEffect(Fnv& f, void* m, const Types& types, const float* position, const int32_t* cells)
{
    const char* how = "";
    char* const self = MakeForEdit(m, types.effects, false, &how);
    if (!self) {
        f.Add(uint8_t{0xEE});
        log::Write("autotest: propkinds: edit effect: none made");
        return;
    }
    char* const occupant = self + layout::kOccupant;
    char* const prop = self + layout::kProp;
    Fnv mine;
    Virtual<void>(occupant, 0x24, position);
    Virtual<uint32_t>(prop, 0x28, int32_t{1});
    const uint8_t added = Virtual<uint8_t>(m, manager::kAddCityProp, static_cast<void*>(occupant));
    mine.Add(added);
    mine.Add(EffectPlain(self));
    mine.Add(At<uint8_t>(self, effect::kFresh));
    mine.Add(Virtual<uint8_t>(occupant, 0x14));
    mine.Add(static_cast<uint8_t>(At<uint8_t>(self, layout::kFlags) & 1));
    AddBoxSize(mine, self);
    const uint32_t afterAdd = mine.h;
    const uint32_t names = At<uint32_t>(self, effect::kNameCount), ids = At<uint32_t>(self, effect::kIdCount);
    const bool hadEffect = At<void*>(self, effect::kEffect) != nullptr;
    // The states: the next one, one past every property, back to the first.
    const uint32_t state = At<uint8_t>(self, layout::kState);
    for (const uint32_t next : {state + 1, 200u, state}) {
        Virtual<void>(prop, 0x2C, next);
        mine.Add(At<uint8_t>(self, layout::kState));
        mine.Add(EffectPlain(self));
    }
    const uint32_t afterStates = mine.h;
    // Shown, as the view does it; then hidden and shown again.
    if (!(At<uint8_t>(self, layout::kFlags) & 2))
        Virtual<uint32_t>(occupant, 0x78, uint32_t{At<uint8_t>(self, layout::kFlags)} | 2u);
    mine.Add(At<uint8_t>(self, effect::kFresh));
    Virtual<uint32_t>(occupant, 0x78, uint32_t{At<uint8_t>(self, layout::kFlags)} & ~2u);
    Virtual<uint32_t>(occupant, 0x78, uint32_t{At<uint8_t>(self, layout::kFlags)} | 2u);
    mine.Add(At<uint8_t>(self, effect::kFresh));
    mine.Add(Virtual<uint8_t>(self, effect::slot::kStopEffect, 1u));
    mine.Add(At<uint8_t>(self, effect::kStarted));
    mine.Add(Virtual<uint8_t>(self, effect::slot::kStopEffect, 0u));
    mine.Add(Virtual<uint8_t>(self, effect::slot::kStartEffect, 0u));
    mine.Add(At<uint8_t>(self, effect::kStarted));
    // Turned and moved with an effect there: the effect is placed again.
    for (const int32_t turn : {3, 2, 0})
        Virtual<uint32_t>(prop, 0x28, turn);
    Virtual<void>(occupant, 0x24, position);
    mine.Add(At<uint8_t>(self, layout::kOrientation));
    mine.Add(EffectPlain(self));
    const uint32_t afterShown = mine.h;
    // A name and an ID no effect has.
    Virtual<void>(self, effect::slot::kSetEffectTypeByName, "opensc4_propkinds_no_such_effect");
    mine.Add(EffectPlain(self));
    Virtual<void>(self, effect::slot::kSetEffectTypeByName, "opensc4_propkinds_no_such_effect");
    mine.Add(EffectPlain(self));
    Virtual<void>(self, effect::slot::kSetEffectTypeById, uint32_t{0x0BADEFFE});
    mine.Add(EffectPlain(self));
    Virtual<void>(self, effect::slot::kSetEffectTypeByName, "off");
    mine.Add(EffectPlain(self));
    const uint32_t afterNames = mine.h;
    const uint8_t removed = Virtual<uint8_t>(m, manager::kRemoveCityPropsInArea, cells);
    mine.Add(removed);
    AddRemoved(mine, self);
    mine.Add(EffectPlain(self));
    log::Write("autotest: propkinds: edit effect (%s): added %u %08X (name properties %u, ID properties %u, effect %d), "
               "states %08X, shown %08X, names %08X; removed %u, initialized %u, in manager %u, effect %d",
               how, added, afterAdd, names, ids, hadEffect ? 1 : 0, afterStates, afterShown, afterNames, removed,
               Virtual<uint8_t>(occupant, 0x14), At<uint8_t>(self, layout::kFlags) & 1,
               At<void*>(self, effect::kEffect) != nullptr ? 1 : 0);
    if (Virtual<uint8_t>(occupant, 0x14)) {
        // Not removed: stopped and released by its own Shutdown before the
        // reference goes.
        log::Write("autotest: propkinds: edit effect: STILL INITIALIZED after the removal; shut down by the test");
        Virtual<uint8_t>(occupant, 0x10);
    }
    Virtual<uint32_t>(self, 0x8);
    f.Add(mine.h);
}

// OPENSC4_TEST_PROPKINDS_EDIT=2: after the round trip a decal (no fade) and
// an effect prop are added at the spot and left there, so that the run's
// save holds them and a run started from that save reads them back.
void LeaveInCity(Fnv& f, void* m, const Types& types, const float* position)
{
    for (const bool isDecal : {true, false}) {
        const char* how = "";
        char* const self = MakeForEdit(m, isDecal ? types.decals : types.effects, isDecal, &how);
        if (!self) {
            f.Add(uint8_t{0xEE});
            continue;
        }
        char* const occupant = self + layout::kOccupant;
        Fnv mine;
        if (isDecal) {
            mine.Add(Virtual<uint8_t>(occupant, 0x0C));
            Virtual<void>(self, decal::slot::kSetDecalTexture, static_cast<const uint32_t*>(kTexture), 12.0f);
            Virtual<void>(self, decal::slot::kSetOpacity, 0.75f);
        }
        Virtual<void>(occupant, 0x24, position);
        Virtual<uint32_t>(self + layout::kProp, 0x28, int32_t{0});
        const uint8_t added = Virtual<uint8_t>(m, manager::kAddCityProp, static_cast<void*>(occupant));
        mine.Add(added);
        const uint32_t plain = isDecal ? DecalPlain(self) : EffectPlain(self);
        mine.Add(plain);
        log::Write("autotest: propkinds: left in the city: %s (%s): added %u, plain %08X, initialized %u, in manager %u",
                   isDecal ? "decal" : "effect", how, added, plain, Virtual<uint8_t>(occupant, 0x14),
                   At<uint8_t>(self, layout::kFlags) & 1);
        Virtual<uint32_t>(self, 0x8);
        f.Add(mine.h);
    }
}

// --- The switch --------------------------------------------------------------------------

void LogCity(const char* when, const CityCount& c)
{
    log::Write("autotest: propkinds: city %s: decals %u (%08X %08X; overlay %u, fading %u) effects %u (%08X %08X; "
               "effect %u, started %u) shown %u, wrong QueryInterface %u",
               when, c.decals, c.decalSum, c.decalXor, c.withOverlay, c.fading, c.effects, c.effectSum, c.effectXor,
               c.withEffect, c.started, c.shown, c.wrongQuery);
}

void Drive(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_PROPKINDS") || !game::IsBuild641())
        return;
    void* const m = Global<void*>(addr::cSC4PropManager_Global);
    void* const city = Global<void*>(addr::SC4_City);
    if (!city) {
        std::snprintf(g_token, sizeof g_token, " propkinds none");
        return;
    }
    const CityCount before = WalkCity(city);
    LogCity("before", before);
    Fnv all;
    for (const uint32_t v : {before.decals, before.decalSum, before.decalXor, before.effects, before.effectSum, before.effectXor,
                             before.wrongQuery})
        all.Add(v);

    Types types;
    if (m)
        types = TypesOf(m);
    log::Write("autotest: propkinds: types: %u decal, %u effect", static_cast<unsigned>(types.decals.size()),
               static_cast<unsigned>(types.effects.size()));
    Fnv made;
    DriveMade(made, m, types);
    all.Add(made.h);

    Fnv edit;
    const int editLevel = EnvInt("OPENSC4_TEST_PROPKINDS_EDIT", 0);
    const bool editing = editLevel != 0;
    if (editing && m) {
        int32_t sizeX = Virtual<int32_t>(city, 0x174), sizeZ = Virtual<int32_t>(city, 0x178);
        if (sizeX <= 0 || sizeZ <= 0 || sizeX > 4096 || sizeZ > 4096)
            sizeX = sizeZ = 0;
        int32_t x = -1, z = -1;
        if (sizeX && FindSpot(m, sizeX, sizeZ, &x, &z)) {
            float corner[2] = {0, 0};
            Virtual<void>(city, 0x180, x, z, &corner[0], &corner[1]);
            void* const terrain = Global<void*>(addr::SC4_Terrain);
            const float position[3] = {corner[0] + 8.0f, terrain ? Virtual<float>(terrain, 0x38, x, z) : 0.0f, corner[1] + 8.0f};
            const int32_t cells[4] = {x, z, x, z};
            log::Write("autotest: propkinds: edit at cell %d,%d", x, z);
            EditDecal(edit, m, types, position, cells);
            EditEffect(edit, m, types, position, cells);
            if (editLevel >= 2) {
                const CityCount between = WalkCity(city);
                LogCity("after the round trip", between);
                edit.Add(between == before);
                LeaveInCity(edit, m, types, position);
            }
        } else {
            edit.Add(uint8_t{0xEE});
            log::Write("autotest: propkinds: edit skipped: no empty spot");
        }
        const CityCount after = WalkCity(city);
        LogCity("after", after);
        if (editLevel >= 2) {
            for (const uint32_t v : {after.decals, after.decalSum, after.decalXor, after.effects, after.effectSum, after.effectXor})
                edit.Add(v);
        } else {
            edit.Add(after == before);
            log::Write("autotest: propkinds: restored %s", after == before ? "yes" : "NO");
        }
        all.Add(edit.h);
    }
    log::Write("autotest: propkinds: made %08X, edit %s %08X", made.h, editing ? "on" : "off", edit.h);
    std::snprintf(g_token, sizeof g_token, " propkinds %08X", all.h);
}

const DayHook kPropKinds{"propkinds", 59, &Drive};

void Fingerprint(cISC4City*, std::string& line)
{
    line += g_token;
}

const FingerprintPart kFingerprint{"propkinds", 7, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
