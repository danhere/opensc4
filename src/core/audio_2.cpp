// The game's audio code, second part (0x004E4040 to 0x004E6C10): eight of
// the ambience filters and the first half of the ambience layer.
//
// An ambience filter is a small counted object (six vtable slots: three of
// the reference count, two that answer true, and the test at slot 5) that
// says whether something counts for a layer. There are three kinds, told
// apart by interface: a filter of occupants (0xCA400753, test (occupant)),
// a filter of cells (0xCA400757, test (cell x, cell z)) and a filter of the
// city's state (0xEA40074F, test ()). The eight here ask for a building's
// purpose, the first number of its building profile (1 residential,
// 2 commercial services, 5 agriculture, 6 and 7 dirty and manufacturing
// industry, 8 high tech), and remember the last question and its answer:
// an occupant filter keeps the occupant at +4 and the answer at +8, a cell
// filter the cell at +4 and +8 and the answer at +0xC.
//
// The ambience layer (vtable 0x00A934A8, 29 slots, interface 0x4A2835FE;
// its error strings name it cSC4AudioAmbienceLayer) is one layer of the
// city's background sound: it collects the places of the occupants and
// cells its filters let through, works out a measure of them (how many,
// how near, the population, the zoom), turns the measure into a volume and
// a gain through its curves, and keeps its looping sounds and its emitters
// in step with that by sending messages to the framework's sound service.
// Its constructor, its tuning and its sound list's maker are in the next
// part (0x004E6C10 onward).
//
// Replacement, off by default:
//
//   audio.part2: everything named above.
//
// Left the game's: 0x004E4040, 0x004E40A0 and 0x004E41F0 (the building
// profile's assignment and the copy of its vector of 12-byte records:
// another subsystem's container code; 0x004E40A0 is also called from
// 0x0069C098) and 0x004E4BB0 (SC4RefPtr::Copy, called from all over the
// game). Not functions: 0x004E4A90, 0x004E5010 and 0x004E6260 are labels
// inside 0x004E4A70, 0x004E4F60 and 0x004E6240. A function the list
// missed: 0x004E6840 (slot 16), after the jump table of 0x004E6340.
//
// The layer: +0 vtable, +4 and +8 not used here, +0xC a byte "on", +0xD a
// byte "muted", +0x10 the city's rectangle (min x, min z, max x, max z),
// +0x20 and +0x24 two cell sizes for the place sets, +0x28 the set of
// occupants' places, +0x2C the set of cells' places (the same object as
// +0x28 when the kind is 1), +0x30 the set made of both, +0x34 the kind,
// +0x38 what the measure is (1 to 6), +0x3C up to nine places (x, y, z),
// +0xA8 how many, +0xAC the measure, +0xB0 and +0xBC two points given with
// every update (the measure's weights are taken along the line between
// them), +0xC8 the weighted middle of the places, +0xDC the volume, +0xE0
// the gain, +0xE4 the emitters' value, +0xE8 a follower that eases the
// middle and the value over time (0x3C bytes, the code at 0x004F7BD0 to
// 0x004F8790), +0x124 occupants taken, +0x128 cells taken, +0x12C the
// state filters' answer and +0x12D "asked", +0x130 occupants refused,
// +0x134 cells refused, +0x144, +0x158 and +0x16C three bit vectors (one
// bit a filter: the ones every thing must pass, the ones none may pass,
// the ones of which one is enough), +0x180 the list of sounds, +0x184 the
// list of emitters, +0x1A0, +0x1B0 and +0x1C0 three curves (gain, volume,
// emitter value by the measure), +0x1E8 the volume setting (1024 is full),
// +0x1F0 a number of the tuning, +0x200 a weight constant, +0x204 the
// gain's step (0: half, 1: three quarters), +0x214, +0x230 and +0x24C the
// three vectors of filters with their counts at +0x220, +0x23C and +0x258.
//
// A sound's node (0x24 bytes): +0 next, +4 previous, +8 a byte "playing",
// +9 a byte "ready", +0xC the gain last sent, +0x10 the volume last sent,
// +0x1C its ID, +0x20 the sound object (interface 0xDC1CF595). An
// emitter's node (0x20 bytes): +8 its ID, +0xC the instance number while it
// plays (0 when not), +0x10 the place last sent, +0x1C the value last sent.
//
// Nothing here reads a clock. What the layer keeps follows from the city
// (occupants, cells, population), from the two points and the time step it
// is given with each update (the camera's, frame by frame) and from the
// player's sound settings.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace opensc4::audio {
namespace {

namespace filter {
constexpr std::size_t kLast = 0x4;
constexpr std::size_t kOccupantAnswer = 0x8;
constexpr std::size_t kLastZ = 0x8;
constexpr std::size_t kCellAnswer = 0xC;
} // namespace filter

namespace layer {
constexpr std::size_t kOn = 0xC;
constexpr std::size_t kMuted = 0xD;
constexpr std::size_t kBounds = 0x10;
constexpr std::size_t kMinX = 0x10;
constexpr std::size_t kMinZ = 0x14;
constexpr std::size_t kMaxX = 0x18;
constexpr std::size_t kMaxZ = 0x1C;
constexpr std::size_t kOccupantCell = 0x20;
constexpr std::size_t kCellCell = 0x24;
constexpr std::size_t kOccupantSet = 0x28;
constexpr std::size_t kCellSet = 0x2C;
constexpr std::size_t kBothSet = 0x30;
constexpr std::size_t kKind = 0x34;
constexpr std::size_t kMeasureKind = 0x38;
constexpr std::size_t kPlaces = 0x3C;
constexpr std::size_t kPlaceCount = 0xA8;
constexpr std::size_t kMeasure = 0xAC;
constexpr std::size_t kFocus = 0xB0;
constexpr std::size_t kListener = 0xBC;
constexpr std::size_t kMiddle = 0xC8;
constexpr std::size_t kVolume = 0xDC;
constexpr std::size_t kGain = 0xE0;
constexpr std::size_t kEmitterValue = 0xE4;
constexpr std::size_t kFollower = 0xE8;
constexpr std::size_t kOccupantsTaken = 0x124;
constexpr std::size_t kCellsTaken = 0x128;
constexpr std::size_t kStateAnswer = 0x12C;
constexpr std::size_t kStateAsked = 0x12D;
constexpr std::size_t kOccupantsRefused = 0x130;
constexpr std::size_t kCellsRefused = 0x134;
constexpr std::size_t kRequiredBits = 0x144;
constexpr std::size_t kExcludedBits = 0x158;
constexpr std::size_t kAnyBits = 0x16C;
constexpr std::size_t kSounds = 0x180;
constexpr std::size_t kEmitters = 0x184;
constexpr std::size_t kGainCurve = 0x1A0;
constexpr std::size_t kVolumeCurve = 0x1B0;
constexpr std::size_t kEmitterCurve = 0x1C0;
constexpr std::size_t kVolumeSetting = 0x1E8;
constexpr std::size_t kStartNumber = 0x1F0;
constexpr std::size_t kWeightConstant = 0x200;
constexpr std::size_t kGainStep = 0x204;
constexpr std::size_t kRequired = 0x214;
constexpr std::size_t kRequiredEnd = 0x218;
constexpr std::size_t kRequiredCount = 0x220;
constexpr std::size_t kExcluded = 0x230;
constexpr std::size_t kExcludedEnd = 0x234;
constexpr std::size_t kExcludedCount = 0x23C;
constexpr std::size_t kAny = 0x24C;
constexpr std::size_t kAnyEnd = 0x250;
constexpr std::size_t kAnyCount = 0x258;
} // namespace layer

constexpr uint32_t kBuildingIid = 0x87DDA3A9;
constexpr uint32_t kLayerIid = 0x4A2835FE;
constexpr uint32_t kStateFilterIid = 0xEA40074F;
constexpr uint32_t kCellFilterIid = 0xCA400757;
constexpr uint32_t kOccupantFilterIid = 0xCA400753;
constexpr uint32_t kSoundServiceId = 0x23CECBA0;
constexpr uint32_t kSoundServiceIid = 0x106077D2;

// The sound service's messages (names ours) and their interface.
constexpr uint32_t kMessageIid = 0x5B523BA4;
constexpr uint32_t kSoundSetStart = 0xEA2D827C;
constexpr uint32_t kSoundPlay = 0xEA2C0390;
constexpr uint32_t kSoundStop = 0x2A2C0393;
constexpr uint32_t kSoundVolume = 0x4A3DA208;
constexpr uint32_t kSoundGain = 0x0A414C5C;
constexpr uint32_t kSoundDrop = 0x4A2C0389;
constexpr uint32_t kEmitterStart = 0xEA451903;
constexpr uint32_t kEmitterStop = 0x6A451908;
constexpr uint32_t kEmitterSetValue = 0x4A451915;
constexpr uint32_t kEmitterPlace = 0xCA45190F;

constexpr uint32_t kEverything = 0x7FFFFFFF;

struct Vec3 {
    float x;
    float y;
    float z;
};
static_assert(sizeof(Vec3) == 0xC);

// A building profile as the filters copy it (0x40 bytes in the original's
// frame). The first number is the building's purpose.
struct Profile {
    int32_t purpose;
    uint32_t a;
    uint16_t b;
    uint8_t c;
    uint8_t pad0;
    void* begin;
    void* end;
    void* capacity;
    uint8_t d;
    uint8_t pad1[3];
    uint32_t rest[9];
};
static_assert(sizeof(Profile) == 0x40);

struct SoundNode {
    SoundNode* next;
    SoundNode* previous;
    uint8_t playing;
    uint8_t ready;
    uint8_t pad[2];
    uint32_t gainSent;
    uint32_t volumeSent;
    uint32_t spare[2];
    uint32_t id;
    void* sound;
};
static_assert(sizeof(SoundNode) == 0x24);

struct EmitterNode {
    EmitterNode* next;
    EmitterNode* previous;
    uint32_t id;
    uint32_t instance;
    Vec3 placeSent;
    uint32_t valueSent;
};
static_assert(sizeof(EmitterNode) == 0x20);

struct PlaceNode {
    PlaceNode* next;
    PlaceNode* previous;
    Vec3 place;
};
static_assert(sizeof(PlaceNode) == 0x14);

struct BitRef {
    uint32_t* word;
    uint32_t mask;
};

template <typename R, typename... Args>
R This(std::uintptr_t function, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(function)(self, args...);
}

template <typename R, typename... Args>
R Cdecl(std::uintptr_t function, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(function)(args...);
}

uint32_t AddRef(void* object)
{
    return Virtual<uint32_t>(object, 0x4);
}

uint32_t Release(void* object)
{
    return Virtual<uint32_t>(object, 0x8);
}

// The game's inlined reference holder: empty it.
void Clear(void*& holder)
{
    if (void* const object = holder) {
        holder = nullptr;
        Release(object);
    }
}

// The game's inlined reference holder: give it another object.
void Assign(void*& holder, void* object)
{
    void* const old = holder;
    if (object != old) {
        if (object)
            AddRef(object);
        holder = object;
        if (old)
            Release(old);
    }
}

// __ftol2: truncation toward zero, the low 32 bits.
int32_t Ftol(double value)
{
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

void Free(void* memory)
{
    Cdecl<void>(addr::SC4_Free, memory);
}

// Bit `index` of the bit vector (words, first bit's offset) at `vector`, as
// the game's inlined iterator arithmetic finds it.
BitRef Bit(char* self, std::size_t vector, int32_t index)
{
    int32_t at = At<int32_t>(self, vector + 4) + index;
    uint32_t* word = At<uint32_t*>(self, vector) + at / 32;
    at %= 32;
    if (at < 0) {
        at += 32;
        --word;
    }
    return {word, uint32_t{1} << at};
}

void* SoundService()
{
    return Global<void*>(addr::SC4Audio_SoundService);
}

// A new message of the sound service (its slot 13: type, interface, where to
// put it, four numbers).
uint8_t NewMessage(void* service, uint32_t type, void** message, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    return Virtual<uint8_t>(service, 0x34, type, kMessageIid, message, a, b, c, d);
}

// The message behind its interface (slot 6).
void* Body(void* message)
{
    return Virtual<void*>(message, 0x18);
}

void Send(void* message)
{
    Virtual<void>(SoundService(), 0x30, message);
}

// The profile's vector freed as the game's inlined copy of the profile's
// destructor does it; `allocator` is whatever the original's frame had there.
void DestroyInline(Profile& profile, void* allocator)
{
    void* const begin = profile.begin;
    Cdecl<void>(addr::SC4MapVector_DestroyRange, begin, profile.end, allocator);
    if (begin)
        Free(begin);
}

bool IsCommercialServices(int32_t purpose)
{
    return purpose == 2;
}

bool IsDirtyIndustry(int32_t purpose)
{
    return purpose >= 6 && purpose <= 7;
}

bool IsHighTech(int32_t purpose)
{
    return purpose == 8;
}

bool IsAgriculture(int32_t purpose)
{
    return purpose == 5;
}

bool IsResidential(int32_t purpose)
{
    return purpose == 1;
}

// The five occupant filters are one function apart from the purpose asked
// for; 0x004E4370 has the profile's destructor inlined on its "no" path.
uint8_t OccupantPurpose(char* self, void* occupant, bool (*wanted)(int32_t), bool inlineDestroy)
{
    if (occupant == At<void*>(self, filter::kLast))
        return At<uint8_t>(self, filter::kOccupantAnswer);

    void* building = nullptr;
    if (!Virtual<uint8_t>(occupant, 0x0, kBuildingIid, &building)) {
        At<void*>(self, filter::kLast) = occupant;
        At<uint8_t>(self, filter::kOccupantAnswer) = 0;
        if (building)
            Release(building);
        return 0;
    }

    // The original leaves rest[3] (+0x2C) unset; nothing reads it.
    Profile profile{};
    void* const source = Virtual<void*>(building, 0x2C);
    This<void*>(addr::SC4Audio_F4E41F0, &profile, source);
    At<void*>(self, filter::kLast) = occupant;
    if (wanted(profile.purpose)) {
        At<uint8_t>(self, filter::kOccupantAnswer) = 1;
        This<void>(addr::SC4BuildingProfile_Destroy, &profile);
        if (building)
            Release(building);
        return 1;
    }
    At<uint8_t>(self, filter::kOccupantAnswer) = 0;
    if (inlineDestroy)
        DestroyInline(profile, &building);
    else
        This<void>(addr::SC4BuildingProfile_Destroy, &profile);
    if (building)
        Release(building);
    return 0;
}

// The game's 0x004E4260: an occupant that is a commercial services building.
uint8_t __fastcall OccupantIsCommercialServices(char* self, void*, void* occupant)
{
    return OccupantPurpose(self, occupant, &IsCommercialServices, false);
}

// The game's 0x004E4370: an occupant that is a dirty or manufacturing
// industry building.
uint8_t __fastcall OccupantIsDirtyIndustry(char* self, void*, void* occupant)
{
    return OccupantPurpose(self, occupant, &IsDirtyIndustry, true);
}

// The game's 0x004E4490: an occupant that is a high tech building.
uint8_t __fastcall OccupantIsHighTech(char* self, void*, void* occupant)
{
    return OccupantPurpose(self, occupant, &IsHighTech, false);
}

// The game's 0x004E45A0: an occupant that is a farm building.
uint8_t __fastcall OccupantIsAgriculture(char* self, void*, void* occupant)
{
    return OccupantPurpose(self, occupant, &IsAgriculture, false);
}

// The game's 0x004E48A0: an occupant that is a residential building.
uint8_t __fastcall OccupantIsResidential(char* self, void*, void* occupant)
{
    return OccupantPurpose(self, occupant, &IsResidential, false);
}

// The two cell filters that ask the lot's building are one function apart
// from the purpose; 0x004E46B0 has the destructor inlined on its "no" path.
uint8_t CellPurpose(char* self, int32_t x, int32_t z, bool (*wanted)(int32_t), bool inlineDestroy)
{
    if (x == At<int32_t>(self, filter::kLast) && z == At<int32_t>(self, filter::kLastZ))
        return At<uint8_t>(self, filter::kCellAnswer);

    if (void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x18, x, z, uint32_t{0})) {
        if (void* const building = Virtual<void*>(lot, 0xC0)) {
            Profile profile{};
            void* const source = Virtual<void*>(building, 0x2C);
            This<void*>(addr::SC4Audio_F4E41F0, &profile, source);
            if (wanted(profile.purpose)) {
                At<int32_t>(self, filter::kLast) = x;
                At<int32_t>(self, filter::kLastZ) = z;
                At<uint8_t>(self, filter::kCellAnswer) = 1;
                This<void>(addr::SC4BuildingProfile_Destroy, &profile);
                return 1;
            }
            if (inlineDestroy)
                DestroyInline(profile, &x);
            else
                This<void>(addr::SC4BuildingProfile_Destroy, &profile);
        }
    }
    At<uint8_t>(self, filter::kCellAnswer) = 0;
    At<int32_t>(self, filter::kLast) = x;
    At<int32_t>(self, filter::kLastZ) = z;
    return 0;
}

// The game's 0x004E46B0: a cell whose lot has a dirty or manufacturing
// industry building.
uint8_t __fastcall CellIsDirtyIndustry(char* self, void*, int32_t x, int32_t z)
{
    return CellPurpose(self, x, z, &IsDirtyIndustry, true);
}

// The game's 0x004E47C0: a cell whose lot has a high tech building.
uint8_t __fastcall CellIsHighTech(char* self, void*, int32_t x, int32_t z)
{
    return CellPurpose(self, x, z, &IsHighTech, false);
}

// The game's 0x004E49B0: a cell whose lot is of a farm building type (the
// profile comes from the building development simulator by the lot's
// building type, so the lot need not be built on).
uint8_t __fastcall CellIsAgriculture(char* self, void*, int32_t x, int32_t z)
{
    if (x == At<int32_t>(self, filter::kLast) && z == At<int32_t>(self, filter::kLastZ))
        return At<uint8_t>(self, filter::kCellAnswer);

    if (void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x18, x, z, uint32_t{0})) {
        if (const uint32_t type = Virtual<uint32_t>(lot, 0xB8, uint32_t{1})) {
            if (void* const development = Global<void*>(addr::SC4_Service43D68)) {
                void* const source = Virtual<void*>(development, 0x9C, type);
                // 0x40 bytes in the original's frame.
                Profile profile;
                This<void*>(addr::SC4BuildingProfile_CopyConstruct, &profile, source);
                if (profile.purpose == 5) {
                    At<int32_t>(self, filter::kLast) = x;
                    At<int32_t>(self, filter::kLastZ) = z;
                    At<uint8_t>(self, filter::kCellAnswer) = 1;
                    This<void>(addr::SC4BuildingProfile_Destroy, &profile);
                    return 1;
                }
                This<void>(addr::SC4BuildingProfile_Destroy, &profile);
            }
        }
    }
    At<int32_t>(self, filter::kLast) = x;
    At<int32_t>(self, filter::kLastZ) = z;
    At<uint8_t>(self, filter::kCellAnswer) = 0;
    return 0;
}

// The game's 0x004E4A60 (slot 11): switches the layer on or off.
void __fastcall LayerSetOn(char* self, void*, uint8_t on)
{
    At<uint8_t>(self, layer::kOn) = on;
}

// The game's 0x004E4A70 (slot 0).
uint8_t __fastcall LayerQueryInterface(char* self, void*, uint32_t iid, void** object)
{
    if (iid == 1 || iid == kLayerIid) {
        *object = self;
        AddRef(self);
        return 1;
    }
    return 0;
}

// The game's 0x004E4AA0: the volume and the gain from the measure.
void __fastcall LayerWorkOutVolume(char* self, void*)
{
    if (!At<uint8_t>(self, layer::kOn)) {
        At<uint32_t>(self, layer::kVolume) = 0;
        At<int32_t>(self, layer::kGain) = 0;
        return;
    }
    const int32_t volume =
        Ftol(This<double>(addr::SC4Curve_Evaluate, self + layer::kVolumeCurve, At<float>(self, layer::kMeasure)));
    At<uint32_t>(self, layer::kVolume) =
        (static_cast<uint32_t>(volume) * At<uint32_t>(self, layer::kVolumeSetting)) >> 10;
    const int32_t gain =
        Ftol(This<double>(addr::SC4Curve_Evaluate, self + layer::kGainCurve, At<float>(self, layer::kMeasure)));
    At<int32_t>(self, layer::kGain) = gain;
    switch (At<int32_t>(self, layer::kGainStep)) {
    case 0:
        At<int32_t>(self, layer::kGain) = static_cast<int32_t>(static_cast<uint32_t>(gain) * 50u) / 100;
        break;
    case 1:
        At<int32_t>(self, layer::kGain) = static_cast<int32_t>(static_cast<uint32_t>(gain) * 75u) / 100;
        break;
    default:
        break;
    }
}

// The game's 0x004E4B60: the emitters' value from the measure.
void __fastcall LayerWorkOutEmitterValue(char* self, void*)
{
    At<int32_t>(self, layer::kEmitterValue) = 0;
    if (!At<uint8_t>(self, layer::kOn))
        return;
    // fucompp, test ah,0x44, jnp: zero only when the two compare equal.
    if (At<float>(self, layer::kMeasure) == 0.0f) {
        At<int32_t>(self, layer::kEmitterValue) = 0;
        return;
    }
    At<int32_t>(self, layer::kEmitterValue) =
        Ftol(This<double>(addr::SC4Curve_Evaluate, self + layer::kEmitterCurve, At<float>(self, layer::kMeasure)));
}

// The game's 0x004E4BD0 (slot 22): puts an occupant's place into the set and
// keeps the occupant.
uint8_t __fastcall LayerAddOccupant(char* self, void*, void* occupant)
{
    if (occupant) {
        Vec3 place;
        Virtual<void>(occupant, 0x20, &place);
        Vec3 copy = place;
        if (This<uint8_t>(addr::SC4Audio_F4F1210, At<void*>(self, layer::kOccupantSet), occupant, &copy,
                          uint32_t{0})) {
            AddRef(occupant);
            ++At<int32_t>(self, layer::kOccupantsTaken);
            return 1;
        }
    }
    return 0;
}

// The game's 0x004E4C40 (slot 23): an occupant's place put again.
uint8_t __fastcall LayerMoveOccupant(char* self, void*, void* occupant)
{
    if (!occupant)
        return 0;
    Vec3 place;
    Virtual<void>(occupant, 0x20, &place);
    Vec3 copy = place;
    This<void>(addr::SC4Audio_F4F12B0, At<void*>(self, layer::kOccupantSet), occupant, &copy,
               At<uint32_t>(self, layer::kOccupantCell));
    return 1;
}

// The game's 0x004E4CA0 (slot 24): takes an occupant out of the set.
uint8_t __fastcall LayerRemoveOccupant(char* self, void*, void* occupant)
{
    if (occupant && This<uint8_t>(addr::SC4Audio_F4F0AC0, At<void*>(self, layer::kOccupantSet), occupant)) {
        Release(occupant);
        --At<int32_t>(self, layer::kOccupantsTaken);
        return 1;
    }
    return 0;
}

// The game's 0x004E4CE0 (slot 25): a new city rectangle; the sets, the
// counts and the filters' bits start again.
void __fastcall LayerReset(char* self, void*, const uint32_t* bounds)
{
    uint32_t* const kept = &At<uint32_t>(self, layer::kBounds);
    kept[0] = bounds[0];
    kept[1] = bounds[1];
    kept[2] = bounds[2];
    kept[3] = bounds[3];
    if (void* const set = At<void*>(self, layer::kOccupantSet))
        This<void>(addr::SC4Audio_F4F0DE0, set, uint32_t{1});
    if (void* const set = At<void*>(self, layer::kCellSet)) {
        if (set != At<void*>(self, layer::kOccupantSet))
            This<void>(addr::SC4Audio_F4F0DE0, set, uint32_t{1});
    }
    At<int32_t>(self, layer::kOccupantsTaken) = 0;
    At<int32_t>(self, layer::kCellsTaken) = 0;
    At<uint8_t>(self, layer::kStateAnswer) = 0;
    At<uint8_t>(self, layer::kStateAsked) = 0;
    At<int32_t>(self, layer::kOccupantsRefused) = 0;
    At<int32_t>(self, layer::kCellsRefused) = 0;
    for (int32_t i = 0; i < At<int32_t>(self, layer::kRequiredCount); ++i) {
        const BitRef bit = Bit(self, layer::kRequiredBits, i);
        *bit.word &= ~bit.mask;
    }
    for (int32_t i = 0; i < At<int32_t>(self, layer::kExcludedCount); ++i) {
        const BitRef bit = Bit(self, layer::kExcludedBits, i);
        *bit.word &= ~bit.mask;
    }
    for (int32_t i = 0; i < At<int32_t>(self, layer::kAnyCount); ++i) {
        const BitRef bit = Bit(self, layer::kAnyBits, i);
        *bit.word &= ~bit.mask;
    }
}

// The game's 0x004E4E80 (slot 27): the two place sets' own pass (0x004F0A30).
void __fastcall LayerSweepSets(char* self, void*)
{
    if (void* const set = At<void*>(self, layer::kOccupantSet))
        This<void>(addr::SC4Audio_F4F0A30, set);
    if (void* const set = At<void*>(self, layer::kCellSet)) {
        if (set != At<void*>(self, layer::kOccupantSet))
            This<void>(addr::SC4Audio_F4F0A30, set);
    }
}

// The game's 0x004E4EB0: keeps a reference to the resource manager at
// 0x00B21950 and fetches the framework's sound service into 0x00B21954.
// Called like a method (ecx the layer), which it does not use.
uint8_t __fastcall FetchSoundService(char*, void*)
{
    void* const resources = Global<void*>(addr::SC4_ResourceManager);
    void* kept = Global<void*>(addr::SC4Audio_ResourceManagerRef);
    if (resources != kept) {
        if (resources)
            AddRef(resources);
        Global<void*>(addr::SC4Audio_ResourceManagerRef) = resources;
        if (kept)
            Release(kept);
        kept = Global<void*>(addr::SC4Audio_ResourceManagerRef);
    }
    if (!kept)
        return 0;

    void* service = nullptr;
    if (void* const framework = Cdecl<void*>(addr::RZGetFramework))
        Virtual<uint8_t>(framework, 0x14, kSoundServiceId, kSoundServiceIid, &service);
    if (!service) {
        This<void>(addr::SC4RefPtr_DestructGuarded, &service);
        return 0;
    }
    void* const old = Global<void*>(addr::SC4Audio_SoundService);
    if (service != old) {
        AddRef(service);
        Global<void*>(addr::SC4Audio_SoundService) = service;
        if (old)
            Release(old);
    }
    This<void>(addr::SC4RefPtr_DestructGuarded, &service);
    return 1;
}

// A new place set (0x28 bytes, constructor 0x004F12E0) for cells of `size`.
void* NewPlaceSet(uint32_t size)
{
    void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x28});
    return memory ? This<void*>(addr::SC4Audio_F4F12E0, memory, size) : nullptr;
}

// The game's 0x004E4F60: makes the layer's place sets.
void __fastcall LayerMakeSets(char* self, void*)
{
    Assign(At<void*>(self, layer::kOccupantSet), NewPlaceSet(At<uint32_t>(self, layer::kOccupantCell)));
    This<uint8_t>(addr::SC4_ReturnTrue648F00, At<void*>(self, layer::kOccupantSet));
    if (At<int32_t>(self, layer::kKind) == 1) {
        // One set serves for both.
        Assign(At<void*>(self, layer::kCellSet), At<void*>(self, layer::kOccupantSet));
        return;
    }
    Assign(At<void*>(self, layer::kCellSet), NewPlaceSet(At<uint32_t>(self, layer::kCellCell)));
    This<uint8_t>(addr::SC4_ReturnTrue648F00, At<void*>(self, layer::kCellSet));
    const uint32_t size = At<int32_t>(self, layer::kKind) == 2 ? At<uint32_t>(self, layer::kCellCell)
                                                               : At<uint32_t>(self, layer::kOccupantCell);
    Assign(At<void*>(self, layer::kBothSet), NewPlaceSet(size));
    This<uint8_t>(addr::SC4_ReturnTrue648F00, At<void*>(self, layer::kBothSet));
}

// The game's 0x004E50A0: empties and lets go of the place sets. The cells'
// set is left alone when it is the occupants' set.
void __fastcall LayerDropSets(char* self, void*)
{
    if (void* const set = At<void*>(self, layer::kBothSet)) {
        This<uint8_t>(addr::SC4Audio_F4F1200, set);
        Clear(At<void*>(self, layer::kBothSet));
    }
    if (void* const set = At<void*>(self, layer::kCellSet)) {
        if (set != At<void*>(self, layer::kOccupantSet)) {
            This<uint8_t>(addr::SC4Audio_F4F1200, set);
            Clear(At<void*>(self, layer::kCellSet));
        }
    }
    if (void* const set = At<void*>(self, layer::kOccupantSet)) {
        This<uint8_t>(addr::SC4Audio_F4F1200, set);
        Clear(At<void*>(self, layer::kOccupantSet));
    }
}

// The three loops the layer's three questions share: every filter of the
// first vector that has the interface must say yes, none of the second may,
// and the third stops at its first yes. A filter's bit is set as it gives
// its good answer. Returns false when a filter of the first two refuses;
// `asked` counts the filters that had the interface, `stoppedAt` is where the
// third loop ended. `bitCall`: the first loop finds its bit through the
// game's out-of-line 0x00477A40 (0x004E5110 and 0x004E5670 do, 0x004E5370
// has it inlined).
template <typename Test>
bool AskFilters(char* self, uint32_t iid, bool bitCall, void*& holder, int32_t& asked, int32_t& stoppedAt, Test test)
{
    for (int32_t i = 0; i < At<int32_t>(self, layer::kRequiredCount); ++i) {
        void* const candidate = At<void**>(self, layer::kRequired)[i];
        Clear(holder);
        if (Virtual<uint8_t>(candidate, 0x0, iid, &holder)) {
            ++asked;
            if (!test(holder))
                return false;
            if (bitCall) {
                BitRef bit;
                const BitRef* const found =
                    This<const BitRef*>(addr::SC4BitVector_At, self + layer::kRequiredBits, &bit, i);
                *found->word |= found->mask;
            } else {
                const BitRef bit = Bit(self, layer::kRequiredBits, i);
                *bit.word |= bit.mask;
            }
        }
    }
    for (int32_t i = 0; i < At<int32_t>(self, layer::kExcludedCount); ++i) {
        void* const candidate = At<void**>(self, layer::kExcluded)[i];
        Clear(holder);
        if (Virtual<uint8_t>(candidate, 0x0, iid, &holder)) {
            ++asked;
            if (test(holder))
                return false;
            const BitRef bit = Bit(self, layer::kExcludedBits, i);
            *bit.word |= bit.mask;
        }
    }
    for (stoppedAt = 0; stoppedAt < At<int32_t>(self, layer::kAnyCount); ++stoppedAt) {
        void* const candidate = At<void**>(self, layer::kAny)[stoppedAt];
        Clear(holder);
        if (Virtual<uint8_t>(candidate, 0x0, iid, &holder)) {
            ++asked;
            if (test(holder)) {
                const BitRef bit = Bit(self, layer::kAnyBits, stoppedAt);
                *bit.word |= bit.mask;
                break;
            }
        }
    }
    return true;
}

// After the loops: something was asked, and the third vector is empty or one
// of its filters said yes.
bool Passed(char* self, int32_t asked, int32_t stoppedAt)
{
    if (asked == 0)
        return false;
    const int32_t any = At<int32_t>(self, layer::kAnyCount);
    return any == 0 || stoppedAt != any;
}

// The game's 0x004E5110 (slot 19): whether the city's state lets the layer
// sound at all; asked once and kept until the next reset. Nothing when the
// player's sound preference (the byte at +4 of the preferences) is off.
uint8_t __fastcall LayerStateAllows(char* self, void*)
{
    void* const preferences = Virtual<void*>(Global<void*>(addr::SC4_Service43C94), 0x98);
    if (!At<uint8_t>(preferences, 0x4))
        return 0;
    if (At<uint8_t>(self, layer::kStateAsked))
        return At<uint8_t>(self, layer::kStateAnswer);
    At<uint8_t>(self, layer::kStateAsked) = 1;

    void* holder = nullptr;
    int32_t asked = 0;
    int32_t stoppedAt = 0;
    const bool unrefused = AskFilters(self, kStateFilterIid, true, holder, asked, stoppedAt,
                                      [](void* state) { return Virtual<uint8_t>(state, 0x14) != 0; });
    if (!unrefused || asked == 0) {
        if (holder)
            Release(holder);
        return 0;
    }
    if (Passed(self, asked, stoppedAt))
        At<uint8_t>(self, layer::kStateAnswer) = 1;
    const uint8_t answer = At<uint8_t>(self, layer::kStateAnswer);
    if (holder)
        Release(holder);
    return answer;
}

// The game's 0x004E5370 (slot 20): offers a cell; when the cell filters let
// it through, its middle goes into the cells' place set. Always answers 0.
uint8_t __fastcall LayerOfferCell(char* self, void*, int32_t x, int32_t z)
{
    if (!Virtual<uint8_t>(self, 0x4C))
        return 0;

    void* holder = nullptr;
    int32_t asked = 0;
    int32_t stoppedAt = 0;
    const bool unrefused =
        AskFilters(self, kCellFilterIid, false, holder, asked, stoppedAt,
                   [x, z](void* cells) { return Virtual<uint8_t>(cells, 0x14, x, z) != 0; });
    if (!unrefused) {
        ++At<int32_t>(self, layer::kCellsRefused);
    } else if (Passed(self, asked, stoppedAt)) {
        ++At<int32_t>(self, layer::kCellsTaken);
        // The last cell's place is kept in globals.
        if (x != Global<int32_t>(addr::SC4Audio_LastCellX) || z != Global<int32_t>(addr::SC4Audio_LastCellZ)) {
            Virtual<void>(Global<void*>(addr::SC4_City), 0x184, x, z, &Global<float>(addr::SC4Audio_LastCellPlaceX),
                          &Global<float>(addr::SC4Audio_LastCellPlaceZ));
            Global<float>(addr::SC4Audio_LastCellHeight) = static_cast<float>(
                Virtual<double>(Global<void*>(addr::SC4_Terrain), 0x34, Global<float>(addr::SC4Audio_LastCellPlaceX),
                                Global<float>(addr::SC4Audio_LastCellPlaceZ)));
            Global<int32_t>(addr::SC4Audio_LastCellX) = x;
            Global<int32_t>(addr::SC4Audio_LastCellZ) = z;
        }
        Vec3 place;
        place.x = Global<float>(addr::SC4Audio_LastCellPlaceX);
        place.y = Global<float>(addr::SC4Audio_LastCellHeight);
        place.z = Global<float>(addr::SC4Audio_LastCellPlaceZ);
        This<uint8_t>(addr::SC4Audio_F4F0E80, At<void*>(self, layer::kCellSet), &place, uint32_t{0});
    }
    if (holder)
        Release(holder);
    return 0;
}

// The game's 0x004E5670 (slot 21): offers an occupant; when the occupant
// filters let it through, its place goes into the occupants' place set.
// Always answers 0.
uint8_t __fastcall LayerOfferOccupant(char* self, void*, void* occupant)
{
    if (!Virtual<uint8_t>(self, 0x4C))
        return 0;
    if (!occupant)
        return 0;

    void* holder = nullptr;
    int32_t asked = 0;
    int32_t stoppedAt = 0;
    const bool unrefused =
        AskFilters(self, kOccupantFilterIid, true, holder, asked, stoppedAt,
                   [occupant](void* occupants) { return Virtual<uint8_t>(occupants, 0x14, occupant) != 0; });
    if (!unrefused) {
        ++At<int32_t>(self, layer::kOccupantsRefused);
    } else if (Passed(self, asked, stoppedAt)) {
        ++At<int32_t>(self, layer::kOccupantsTaken);
        Vec3 place;
        Virtual<void>(occupant, 0x20, &place);
        Vec3 copy = place;
        This<uint8_t>(addr::SC4Audio_F4F0E80, At<void*>(self, layer::kOccupantSet), &copy, uint32_t{0});
    }
    if (holder)
        Release(holder);
    return 0;
}

// The game's 0x004E5920: brings the layer's sounds in step with its volume
// and gain: starts and stops them, places them, sends what changed. The
// argument is not read.
void __fastcall LayerUpdateSounds(char* self, void*, uint32_t)
{
    LayerWorkOutVolume(self, nullptr);
    SoundNode* const head = At<SoundNode*>(self, layer::kSounds);
    for (SoundNode* node = head->next; node != head; node = node->next) {
        void* message = nullptr;
        bool stopping = true;
        if (At<uint32_t>(self, layer::kVolume) != 0) {
            if (At<int32_t>(self, layer::kGain) != 0 && !node->playing && node->ready &&
                !At<uint8_t>(self, layer::kMuted)) {
                node->gainSent = 0;
                node->volumeSent = 0;
                if (NewMessage(SoundService(), kSoundSetStart, &message, 0, 0, 0, 0)) {
                    // 0x00A823A4: 0.01.
                    const double number = static_cast<double>(At<uint32_t>(self, layer::kStartNumber));
                    const double scale = static_cast<double>(Global<float>(addr::SC4Audio_StartScale));
                    const float start = static_cast<float>(number * static_cast<double>(0.01f) * scale);
                    Virtual<void>(Body(message), 0x84, node->sound);
                    Virtual<void>(Body(message), 0x2C, Ftol(static_cast<double>(start)));
                    Virtual<void>(Body(message), 0x40, Ftol(static_cast<double>(start) * static_cast<double>(16.0f)));
                    Send(message);
                }
                void* const service = SoundService();
                Clear(message);
                if (NewMessage(service, kSoundPlay, &message, 0, 0, 0, 0)) {
                    Virtual<void>(Body(message), 0x84, node->sound);
                    Virtual<void>(Body(message), 0x2C, uint32_t{At<int32_t>(self, layer::kKind) == 4});
                    Send(message);
                    node->playing = 1;
                }
            }
            if (At<uint32_t>(self, layer::kVolume) != 0 && At<int32_t>(self, layer::kGain) != 0 &&
                !At<uint8_t>(self, layer::kMuted))
                stopping = false;
        }
        bool done = false;
        if (stopping) {
            if (!node->playing) {
                done = true;
            } else {
                void* const service = SoundService();
                Clear(message);
                if (NewMessage(service, kSoundStop, &message, 0, 0, 0, 0)) {
                    Virtual<void>(Body(message), 0x84, node->sound);
                    Send(message);
                    node->playing = 0;
                    done = true;
                }
                // A stop message that could not be made leaves the sound to
                // be treated as playing below.
            }
        }
        if (!done && node->playing) {
            if (At<int32_t>(self, layer::kKind) != 4) {
                uint32_t i = 0;
                if (At<int32_t>(self, layer::kPlaceCount) > 0) {
                    const Vec3* place = &At<Vec3>(self, layer::kPlaces);
                    do {
                        const uint32_t row = i / 3;
                        const uint32_t column = i % 3;
                        const int32_t z = Ftol(static_cast<double>(place->z));
                        const int32_t y = Ftol(static_cast<double>(place->y));
                        const int32_t x = Ftol(static_cast<double>(place->x));
                        Virtual<void>(node->sound, 0x84, column, row, x, y, z);
                        Virtual<void>(node->sound, 0x80, column, row, uint32_t{1});
                        ++i;
                        ++place;
                    } while (static_cast<int32_t>(i) < At<int32_t>(self, layer::kPlaceCount));
                }
                for (; i < 9; ++i)
                    Virtual<void>(node->sound, 0x80, i % 3, i / 3, uint32_t{0});
            }
            const uint32_t volume = At<uint32_t>(self, layer::kVolume);
            if (node->volumeSent != volume) {
                node->volumeSent = volume;
                if (node->playing) {
                    void* const service = SoundService();
                    Clear(message);
                    if (NewMessage(service, kSoundVolume, &message, 0, 0, 0, 0)) {
                        Virtual<void>(Body(message), 0x84, node->sound);
                        Virtual<void>(Body(message), 0x2C, node->volumeSent);
                        Send(message);
                    }
                }
            }
            if (node->gainSent != At<uint32_t>(self, layer::kGain)) {
                if (node->playing) {
                    void* const service = SoundService();
                    Clear(message);
                    if (NewMessage(service, kSoundGain, &message, 0, 0, 0, 0)) {
                        Virtual<void>(Body(message), 0x84, node->sound);
                        Virtual<void>(Body(message), 0x2C, At<uint32_t>(self, layer::kGain));
                        Send(message);
                    }
                }
                node->gainSent = At<uint32_t>(self, layer::kGain);
            }
        }
        if (message)
            Release(message);
    }
}

// The game's 0x004E5D50: brings the layer's emitters in step: starts and
// stops them, sends their value and their place (the layer's middle, eased by
// the follower) when these change, then moves the follower on by `elapsed`.
void __fastcall LayerUpdateEmitters(char* self, void*, uint32_t elapsed)
{
    LayerWorkOutEmitterValue(self, nullptr);
    char* const follower = self + layer::kFollower;
    Vec3* const middle = &At<Vec3>(self, layer::kMiddle);
    EmitterNode* const head = At<EmitterNode*>(self, layer::kEmitters);
    for (EmitterNode* node = head->next; node != head; node = node->next) {
        void* message = nullptr;
        bool sounding = false;
        if (At<int32_t>(self, layer::kEmitterValue) != 0) {
            if (node->instance == 0) {
                This<void>(addr::SC4Audio_F4F8240, follower, middle, uint32_t{1});
                This<void>(addr::SC4Audio_F4F7BD0, follower, At<uint32_t>(self, layer::kEmitterValue), uint32_t{1});
                node->valueSent = 0;
                node->placeSent.x = 0.0f;
                node->placeSent.y = 0.0f;
                node->placeSent.z = 0.0f;
                // The original fetches this object and does nothing with it
                // (what is left of a temporary reference never taken).
                Cdecl<void*>(addr::SC4Sound_StaticObject);
                node->instance = Cdecl<uint32_t>(addr::SC4Sound_NextInstance);
                void* const service = SoundService();
                Clear(message);
                NewMessage(service, kEmitterStart, &message, node->id, node->instance, 1,
                           At<uint32_t>(self, layer::kEmitterValue));
                Send(message);
            }
            if (At<int32_t>(self, layer::kEmitterValue) != 0)
                sounding = true;
        }
        if (!sounding && node->instance != 0) {
            void* const service = SoundService();
            Clear(message);
            NewMessage(service, kEmitterStop, &message, node->instance, 0, 0, 0);
            Send(message);
            node->instance = 0;
        }

        This<void>(addr::SC4Audio_F4F8240, follower, middle, uint32_t{0});
        This<void>(addr::SC4Audio_F4F8280, follower, middle);
        This<void>(addr::SC4Audio_F4F7BD0, follower, At<uint32_t>(self, layer::kEmitterValue), uint32_t{0});
        At<uint32_t>(self, layer::kEmitterValue) = This<uint32_t>(addr::SC4_ReturnField1C808620, follower);
        if (node->valueSent != At<uint32_t>(self, layer::kEmitterValue)) {
            node->valueSent = At<uint32_t>(self, layer::kEmitterValue);
            if (node->instance != 0) {
                void* const service = SoundService();
                Clear(message);
                NewMessage(service, kEmitterSetValue, &message, node->instance, node->valueSent, 0, 0);
                Send(message);
            }
        }

        // 0x00A9332C: 0.0001.
        const double tolerance = static_cast<double>(0.0001f);
        const bool still =
            std::fabs(static_cast<double>(node->placeSent.x) - static_cast<double>(middle->x)) < tolerance &&
            std::fabs(static_cast<double>(node->placeSent.y) - static_cast<double>(middle->y)) < tolerance &&
            std::fabs(static_cast<double>(node->placeSent.z) - static_cast<double>(middle->z)) < tolerance;
        if (!still && node->instance != 0) {
            void* const service = SoundService();
            Clear(message);
            const int32_t z = Ftol(static_cast<double>(middle->z));
            const int32_t y = Ftol(static_cast<double>(middle->y));
            const int32_t x = Ftol(static_cast<double>(middle->x));
            NewMessage(service, kEmitterPlace, &message, node->instance, static_cast<uint32_t>(x),
                       static_cast<uint32_t>(y), static_cast<uint32_t>(z));
            Send(message);
            node->placeSent = *middle;
        }
        if (message)
            Release(message);
    }
    This<void>(addr::SC4Audio_F4F8650, follower, static_cast<float>(elapsed));
}

// The game's 0x004E6070: copies the places of a list into `places`: all of
// them when there are no more than nine, else the first place met in each
// ninth of the city's rectangle (three by three).
void __fastcall LayerPickPlaces(char* self, void*, PlaceNode** list, uint32_t count, Vec3* places, int32_t* picked)
{
    PlaceNode* const head = *list;
    PlaceNode* node = head->next;
    if (count <= 9) {
        *picked = 0;
        for (; node != head; node = node->next)
            places[(*picked)++] = node->place;
        return;
    }

    uint32_t squares[9];
    Cdecl<void>(addr::SC4_FillUint32, squares, uint32_t{0}, uint32_t{9});
    // 0x00A92480: a third.
    const double third = static_cast<double>(0.333333343f);
    const double minX = static_cast<double>(At<float>(self, layer::kMinX));
    const double minZ = static_cast<double>(At<float>(self, layer::kMinZ));
    const int32_t width = Ftol((static_cast<double>(At<float>(self, layer::kMaxX)) - minX) * third);
    const int32_t depth = Ftol((static_cast<double>(At<float>(self, layer::kMaxZ)) - minZ) * third);
    *picked = 0;
    for (; node != head; node = node->next) {
        int32_t column = Ftol((static_cast<double>(node->place.x) - minX) / static_cast<double>(width));
        int32_t row = Ftol((static_cast<double>(node->place.z) - minZ) / static_cast<double>(depth));
        if (column < 0)
            column = 0;
        if (row < 0)
            row = 0;
        if (column > 2)
            column = 2;
        if (row > 2)
            row = 2;
        const int32_t square = column + row * 3;
        if (squares[square]++ == 0)
            places[(*picked)++] = node->place;
    }
}

// The game's 0x004E61F0 (slot 17): the volume setting (1024 is full).
void __fastcall LayerSetVolume(char* self, void*, uint32_t setting)
{
    At<uint32_t>(self, layer::kVolumeSetting) = setting;
    LayerUpdateSounds(self, nullptr, kEverything);
}

// The game's 0x004E6210 (slot 28): mutes the layer or lets it sound again.
void __fastcall LayerSetMuted(char* self, void*, uint8_t muted)
{
    At<uint8_t>(self, layer::kMuted) = muted;
    LayerUpdateSounds(self, nullptr, kEverything);
    LayerUpdateEmitters(self, nullptr, kEverything);
}

// The game's 0x004E6240 (slot 13): takes an emitter off the list (it is not
// stopped).
uint8_t __fastcall LayerRemoveEmitter(char* self, void*, uint32_t id)
{
    EmitterNode* const head = At<EmitterNode*>(self, layer::kEmitters);
    for (EmitterNode* node = head->next; node != head; node = node->next) {
        if (node->id == id) {
            EmitterNode* const next = node->next;
            EmitterNode* const previous = node->previous;
            previous->next = next;
            next->previous = previous;
            Free(node);
            return 1;
        }
    }
    return 0;
}

// The game's 0x004E6280: stops every emitter and empties the list.
uint8_t __fastcall LayerDropEmitters(char* self, void*)
{
    EmitterNode* const head = At<EmitterNode*>(self, layer::kEmitters);
    EmitterNode* node = head->next;
    while (node != head) {
        if (const uint32_t instance = node->instance) {
            void* message = nullptr;
            NewMessage(SoundService(), kEmitterStop, &message, instance, 0, 0, 0);
            Send(message);
            if (message)
                Release(message);
        }
        EmitterNode* const next = node->next;
        EmitterNode* const previous = node->previous;
        previous->next = next;
        next->previous = previous;
        Free(node);
        node = next;
    }
    return 1;
}

// The game's 0x004E6300: empties a list of sounds (`list` is the layer's
// +0x180), letting go of each sound object. No messages.
void __fastcall SoundListClear(SoundNode** list, void*)
{
    SoundNode* node = (*list)->next;
    while (node != *list) {
        SoundNode* const doomed = node;
        node = node->next;
        if (void* const sound = doomed->sound)
            Release(sound);
        Free(doomed);
    }
    (*list)->next = *list;
    (*list)->previous = *list;
}

// The measure for what the layer's tuning asks (+0x38); false when the kind
// stores nothing through the x87 path.
bool Measure(char* self, int32_t found, double& measure)
{
    switch (At<int32_t>(self, layer::kMeasureKind)) {
    case 1:
        measure = static_cast<double>(found);
        return true;
    case 2: {
        // The residential simulator's slot 16, a count.
        void* const residential = Global<void*>(addr::SC4_Service43DA4);
        measure = static_cast<double>(residential ? Virtual<int32_t>(residential, 0x40) : int32_t{0});
        return true;
    }
    case 3:
        At<uint32_t>(self, layer::kMeasure) = 0x7F7FFFFF;
        return false;
    case 4:
        measure = static_cast<double>(At<int32_t>(Global<void*>(addr::SC4_CameraView), 0xC));
        return true;
    case 5: {
        const int32_t places = At<int32_t>(self, layer::kPlaceCount);
        measure = places != 0 ? static_cast<double>(found) / static_cast<double>(places) : 0.0;
        return true;
    }
    case 6: {
        // The original's static object cannot be null; were it, the original
        // would read a null reference the same way.
        void* const sounds = Cdecl<void*>(addr::SC4Sound_StaticObject);
        void* const object = sounds ? At<void*>(sounds, 0x54) : nullptr;
        measure = static_cast<double>(This<uint32_t>(addr::SC4Audio_F4E82A0, object));
        return true;
    }
    default:
        return false;
    }
}

// The weighted middle of the layer's places: a place weighs more the nearer
// the line from the listener to it runs along the line from the listener to
// the focus. x87 as the original: sums of x and y stay on the stack (double),
// the sum of z and every stored value is a float.
void WorkOutMiddle(char* self)
{
    const double one = static_cast<double>(Global<float>(addr::SC4Audio_OneFloat));
    double sumX = static_cast<double>(Global<float>(addr::SC4Audio_ZeroFloat));
    double sumY = static_cast<double>(Global<float>(addr::SC4Audio_ZeroFloat));
    float sumZ = Global<float>(addr::SC4Audio_ZeroFloat);
    double weights = static_cast<double>(0.0f);
    const int32_t count = At<int32_t>(self, layer::kPlaceCount);
    const Vec3& focus = At<Vec3>(self, layer::kFocus);
    const Vec3& listener = At<Vec3>(self, layer::kListener);

    const double dx = static_cast<double>(focus.x) - static_cast<double>(listener.x);
    const double dy = static_cast<double>(focus.y) - static_cast<double>(listener.y);
    const double dz = static_cast<double>(focus.z) - static_cast<double>(listener.z);
    float nearest = 3.40282347e+38f;
    float aimZ = static_cast<float>(dz);
    double square = dz * static_cast<double>(aimZ);
    square = square + dy * dy;
    square = square + dx * dx;
    const float inverse = static_cast<float>(one / std::sqrt(square));
    const float aimX = static_cast<float>(dx * static_cast<double>(inverse));
    const float aimY = static_cast<float>(dy * static_cast<double>(inverse));
    aimZ = static_cast<float>(static_cast<double>(aimZ) * static_cast<double>(inverse));

    const Vec3* place = &At<Vec3>(self, layer::kPlaces);
    for (int32_t left = count; left > 0; --left, ++place) {
        const float toX = static_cast<float>(static_cast<double>(place->x) - static_cast<double>(listener.x));
        const float toY = static_cast<float>(static_cast<double>(place->y) - static_cast<double>(listener.y));
        const double toZWide = static_cast<double>(place->z) - static_cast<double>(listener.z);
        const float toZ = static_cast<float>(toZWide);
        double distance = toZWide * static_cast<double>(toZ);
        distance = distance + static_cast<double>(toY) * static_cast<double>(toY);
        distance = distance + static_cast<double>(toX) * static_cast<double>(toX);
        distance = std::sqrt(distance);
        // The nearest place's distance is kept in a local nothing reads.
        if (distance < static_cast<double>(nearest))
            nearest = static_cast<float>(distance);
        const double scale = one / distance;
        const double unitX = static_cast<double>(toX) * scale;
        const double unitY = static_cast<double>(toY) * scale;
        const double unitZ = static_cast<double>(toZ) * scale;
        double along = unitZ * static_cast<double>(aimZ) + unitY * static_cast<double>(aimY);
        along = along + unitX * static_cast<double>(aimX);
        const float alongKept = static_cast<float>(std::fabs(along));
        // 0x00A81228: 1.0.
        const double constant = static_cast<double>(At<float>(self, layer::kWeightConstant));
        const double weight =
            static_cast<double>(alongKept) * (static_cast<double>(1.0f) / (constant - static_cast<double>(alongKept)));
        weights = weights + weight;
        const float partX = static_cast<float>(weight * static_cast<double>(place->x));
        const float partY = static_cast<float>(weight * static_cast<double>(place->y));
        const double partZ = weight * static_cast<double>(place->z);
        sumX = sumX + static_cast<double>(partX);
        sumY = sumY + static_cast<double>(partY);
        sumZ = static_cast<float>(partZ + static_cast<double>(sumZ));
    }
    (void)nearest;

    const float share = static_cast<float>(one / weights);
    Vec3& middle = At<Vec3>(self, layer::kMiddle);
    middle.x = static_cast<float>(static_cast<double>(share) * sumX);
    const double middleY = sumY * static_cast<double>(share);
    const double middleZ = static_cast<double>(sumZ) * static_cast<double>(share);
    middle.y = static_cast<float>(middleY);
    middle.z = static_cast<float>(middleZ);
}

// The game's 0x004E6340 (slot 26): the layer's turn. Takes the two points,
// switches itself on or off by its filters' bits and the city's state, picks
// its places from the sets, works out the measure and the middle, then
// updates its sounds and emitters.
void __fastcall LayerUpdate(char* self, void*, uint32_t elapsed, const Vec3* focus, const Vec3* listener)
{
    At<Vec3>(self, layer::kFocus) = *focus;
    At<Vec3>(self, layer::kListener) = *listener;

    for (int32_t i = 0; i < At<int32_t>(self, layer::kRequiredCount); ++i) {
        const BitRef bit = Bit(self, layer::kRequiredBits, i);
        if (!(*bit.word & bit.mask))
            At<uint8_t>(self, layer::kOn) = 0;
    }
    for (int32_t i = 0; i < At<int32_t>(self, layer::kExcludedCount); ++i) {
        const BitRef bit = Bit(self, layer::kExcludedBits, i);
        if (!(*bit.word & bit.mask))
            At<uint8_t>(self, layer::kOn) = 0;
    }
    for (int32_t i = 0; i < At<int32_t>(self, layer::kAnyCount); ++i) {
        const BitRef bit = Bit(self, layer::kAnyBits, i);
        if (*bit.word & bit.mask)
            At<uint8_t>(self, layer::kOn) = 1;
    }

    const uint8_t on = Virtual<uint8_t>(self, 0x4C);
    At<uint8_t>(self, layer::kOn) = on;
    if (on) {
        PlaceNode* found = Cdecl<PlaceNode*>(addr::SC4_stl_allocate, uint32_t{0x14});
        found->next = found;
        found->previous = found;
        int32_t count = 0;
        const uint32_t kind = At<uint32_t>(self, layer::kKind);
        if (kind == 1) {
            int32_t* const most = &At<int32_t>(self, layer::kPlaceCount);
            *most = 9;
            This<void>(addr::SC4Audio_F4F0B40, At<void*>(self, layer::kOccupantSet), self + layer::kBounds, &found,
                       most, &count, uint32_t{0});
        } else if (kind > 1 && kind <= 3) {
            This<void>(addr::SC4Audio_F4F0EA0, At<void*>(self, layer::kBothSet), At<void*>(self, layer::kOccupantSet),
                       At<void*>(self, layer::kCellSet), uint32_t{kind == 2});
            int32_t* const most = &At<int32_t>(self, layer::kPlaceCount);
            *most = 9;
            This<void>(addr::SC4Audio_F4F0B40, At<void*>(self, layer::kBothSet), self + layer::kBounds, &found, most,
                       &count, uint32_t{0});
        }

        double measure = 0.0;
        if (Measure(self, count, measure))
            At<float>(self, layer::kMeasure) = static_cast<float>(measure);

        if (const uint32_t places = At<uint32_t>(self, layer::kPlaceCount)) {
            LayerPickPlaces(self, nullptr, &found, places, &At<Vec3>(self, layer::kPlaces),
                            &At<int32_t>(self, layer::kPlaceCount));
            WorkOutMiddle(self);
        }

        PlaceNode* node = found->next;
        while (node != found) {
            PlaceNode* const doomed = node;
            node = node->next;
            Free(doomed);
        }
        found->next = found;
        found->previous = found;
        Free(found);
    }
    LayerUpdateSounds(self, nullptr, elapsed);
    LayerUpdateEmitters(self, nullptr, elapsed);
}

// One sound told to go, taken off the list and let go of. `extraRelease`:
// 0x004E6840 releases the sound object once more than 0x004E6910 does.
void DropSound(SoundNode* node, bool extraRelease)
{
    void* message = nullptr;
    if (NewMessage(SoundService(), kSoundDrop, &message, 0, 0, 0, 0)) {
        Virtual<void>(Body(message), 0x84, node->sound);
        Send(message);
    }
    if (extraRelease)
        Release(node->sound);
    Clear(node->sound);
    SoundNode* const next = node->next;
    SoundNode* const previous = node->previous;
    previous->next = next;
    next->previous = previous;
    if (node->sound)
        Release(node->sound);
    Free(node);
    if (message)
        Release(message);
}

// The game's 0x004E6840 (slot 16): drops the sound with this ID.
uint8_t __fastcall LayerRemoveSound(char* self, void*, uint32_t id)
{
    SoundNode* const head = At<SoundNode*>(self, layer::kSounds);
    for (SoundNode* node = head->next; node != head; node = node->next) {
        if (node->id == id) {
            DropSound(node, true);
            return 1;
        }
    }
    return 0;
}

// The game's 0x004E6910: drops every sound.
uint8_t __fastcall LayerDropSounds(char* self, void*)
{
    SoundNode* const head = At<SoundNode*>(self, layer::kSounds);
    SoundNode* node = head->next;
    while (node != head) {
        SoundNode* const next = node->next;
        DropSound(node, false);
        node = next;
    }
    return 1;
}

// The game's 0x004E69D0 (slot 12): a new emitter at the list's end, unless
// the ID is 0 or there already.
uint8_t __fastcall LayerAddEmitter(char* self, void*, uint32_t id)
{
    if (!id)
        return 0;
    EmitterNode* const head = At<EmitterNode*>(self, layer::kEmitters);
    for (EmitterNode* node = head->next; node != head; node = node->next) {
        if (node->id == id)
            return 0;
    }
    const uint32_t zero = Global<uint32_t>(addr::SC4Audio_ZeroFloat);
    EmitterNode* const node = Cdecl<EmitterNode*>(addr::SC4_stl_allocate, uint32_t{0x20});
    node->id = id;
    node->instance = 0;
    At<uint32_t>(node, 0x10) = zero;
    At<uint32_t>(node, 0x14) = zero;
    At<uint32_t>(node, 0x18) = zero;
    node->valueSent = 0;
    EmitterNode* const last = head->previous;
    node->next = head;
    node->previous = last;
    last->next = node;
    head->previous = node;
    return 1;
}

// One vector of filters emptied from the back, as the original's inlined
// loop does it: a reference taken, the vector's own given back, the place
// cleared, the taken one given back.
void DropFilters(char* self, std::size_t begin, std::size_t end, std::size_t count)
{
    int32_t left = static_cast<int32_t>(At<char*>(self, end) - At<char*>(self, begin)) >> 2;
    while (left != 0) {
        void* const one = At<void**>(self, begin)[left - 1];
        --left;
        if (one)
            AddRef(one);
        Release(one);
        At<void**>(self, begin)[left] = nullptr;
        Release(one);
    }
    At<int32_t>(self, count) = 0;
    At<void*>(self, end) = At<void*>(self, begin);
}

// The game's 0x004E6A70: lets go of every filter.
void __fastcall LayerDropFilters(char* self, void*)
{
    DropFilters(self, layer::kRequired, layer::kRequiredEnd, layer::kRequiredCount);
    DropFilters(self, layer::kExcluded, layer::kExcludedEnd, layer::kExcludedCount);
    DropFilters(self, layer::kAny, layer::kAnyEnd, layer::kAnyCount);
}

bool InstallAudio2(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[48] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4Audio_F4E4260, reinterpret_cast<void*>(&OccupantIsCommercialServices)},
        {addr::SC4Audio_F4E4370, reinterpret_cast<void*>(&OccupantIsDirtyIndustry)},
        {addr::SC4Audio_F4E4490, reinterpret_cast<void*>(&OccupantIsHighTech)},
        {addr::SC4Audio_F4E45A0, reinterpret_cast<void*>(&OccupantIsAgriculture)},
        {addr::SC4Audio_F4E46B0, reinterpret_cast<void*>(&CellIsDirtyIndustry)},
        {addr::SC4Audio_F4E47C0, reinterpret_cast<void*>(&CellIsHighTech)},
        {addr::SC4Audio_F4E48A0, reinterpret_cast<void*>(&OccupantIsResidential)},
        {addr::SC4Audio_F4E49B0, reinterpret_cast<void*>(&CellIsAgriculture)},
        {addr::SC4Audio_F4E4A60, reinterpret_cast<void*>(&LayerSetOn)},
        {addr::SC4Audio_F4E4A70, reinterpret_cast<void*>(&LayerQueryInterface)},
        {addr::SC4Audio_F4E4AA0, reinterpret_cast<void*>(&LayerWorkOutVolume)},
        {addr::SC4Audio_F4E4B60, reinterpret_cast<void*>(&LayerWorkOutEmitterValue)},
        {addr::SC4Audio_F4E4BD0, reinterpret_cast<void*>(&LayerAddOccupant)},
        {addr::SC4Audio_F4E4C40, reinterpret_cast<void*>(&LayerMoveOccupant)},
        {addr::SC4Audio_F4E4CA0, reinterpret_cast<void*>(&LayerRemoveOccupant)},
        {addr::SC4Audio_F4E4CE0, reinterpret_cast<void*>(&LayerReset)},
        {addr::SC4Audio_F4E4E80, reinterpret_cast<void*>(&LayerSweepSets)},
        {addr::SC4Audio_F4E4EB0, reinterpret_cast<void*>(&FetchSoundService)},
        {addr::SC4Audio_F4E4F60, reinterpret_cast<void*>(&LayerMakeSets)},
        {addr::SC4Audio_F4E50A0, reinterpret_cast<void*>(&LayerDropSets)},
        {addr::SC4Audio_F4E5110, reinterpret_cast<void*>(&LayerStateAllows)},
        {addr::SC4Audio_F4E5370, reinterpret_cast<void*>(&LayerOfferCell)},
        {addr::SC4Audio_F4E5670, reinterpret_cast<void*>(&LayerOfferOccupant)},
        {addr::SC4Audio_F4E5920, reinterpret_cast<void*>(&LayerUpdateSounds)},
        {addr::SC4Audio_F4E5D50, reinterpret_cast<void*>(&LayerUpdateEmitters)},
        {addr::SC4Audio_F4E6070, reinterpret_cast<void*>(&LayerPickPlaces)},
        {addr::SC4Audio_F4E61F0, reinterpret_cast<void*>(&LayerSetVolume)},
        {addr::SC4Audio_F4E6210, reinterpret_cast<void*>(&LayerSetMuted)},
        {addr::SC4Audio_F4E6240, reinterpret_cast<void*>(&LayerRemoveEmitter)},
        {addr::SC4Audio_F4E6280, reinterpret_cast<void*>(&LayerDropEmitters)},
        {addr::SC4Audio_F4E6300, reinterpret_cast<void*>(&SoundListClear)},
        {addr::SC4Audio_F4E6340, reinterpret_cast<void*>(&LayerUpdate)},
        {addr::SC4Audio_F4E6840, reinterpret_cast<void*>(&LayerRemoveSound)},
        {addr::SC4Audio_F4E6910, reinterpret_cast<void*>(&LayerDropSounds)},
        {addr::SC4Audio_F4E69D0, reinterpret_cast<void*>(&LayerAddEmitter)},
        {addr::SC4Audio_F4E6A70, reinterpret_cast<void*>(&LayerDropFilters)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    static_assert(count <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < count; ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].first);
        if (!found || !hook::Install(*found, hooks[i].second, &originals[i]))
            return false;
    }
    return true;
}

const bool g_audio2Registered = registry::Register({
    "audio.part2",
    "audio",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Audio_F4E4260,
    "Our ambience filters by building purpose and the first half of the ambience layer (docs/audio.md)",
    false,
    InstallAudio2,
});

} // namespace
} // namespace opensc4::audio
