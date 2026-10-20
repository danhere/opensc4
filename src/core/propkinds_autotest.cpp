// The derived prop classes' part of the autotest (src/core/autotest_hooks.h;
// propkinds_decal.cpp, propkinds_effect.cpp, propkinds_automata.cpp,
// propkinds_burnt.cpp, the class maps in propkinds.h): the terrain decal, the
// visual effect prop, the automata controller prop, the burnt building, the
// text decal (the label; propkinds_textdecal.cpp) and the sign
// (propkinds_sign.cpp).
// Everything goes through the game's objects: their vtables, the prop
// manager and the classes' allocators and constructors by address. Nothing
// here calls our own functions, so a run with simulators.propkinds.* on and
// one with it off can be compared.
//
//   OPENSC4_TEST_PROPKINDS=<day>   token "propkinds"; sub-results on the log
//     lines "autotest: propkinds: ...". The city is left unchanged:
//     1. the city's props of the six classes, found through the occupant
//        manager's grid (occupant type 0xA823821E and the class ID from
//        GetGZCLSID, confirmed with QueryInterface): the counts and the sum
//        and exclusive or of a hash per object over its plain state;
//     2. props the test makes and never inserts: one of each class from the
//        allocator and the constructor (as the terrain code makes a decal
//        and the lot developer a burnt building, the label tool a label and
//        the sign tool a sign),
//        and up to two of each from the prop manager's CreateProp when its
//        tables have types of the class. QueryInterface, GetGZCLSID and the setters that only write
//        fields while there is no overlay, effect or controller, read back
//        (a controller prop is given a template no script has; a burnt
//        building is initialized, given its own box and shut down); the
//        sign's and the burnt building's geometry (quads, vertices) and
//        their model interface's slots through its thunks (occupant, boxes,
//        transform, rays for the sign's hit test, texture); then released.
//   OPENSC4_TEST_PROPKINDS_EDIT=1  with it, a round trip that changes the
//     city and puts it back: at a spot with no lot whose 64 m occupant cell
//     is empty a decal is initialized, added, shown (base flag 2 through
//     SetAllFlags, as the view does), faded with SetOpacityFade and frame
//     messages the test makes (fixed milliseconds, action 0), hidden, shown
//     and removed; then an effect prop is added, switched through states,
//     shown, stopped, started, turned, given a name and an ID no effect has,
//     and removed; then a controller prop is added (its template from its
//     exemplar when the tables have a type), disabled, enabled, switched
//     through states, given a template no script has and its own back, and
//     removed; then a burnt building is made as the lot developer makes one
//     (but from its own box at the spot), added, shown, hidden, shown,
//     hidden and removed; then a label is added as the label tool adds one,
//     given a text with U+00E9, shown (with signs and labels switched on
//     for it when they were off), given a size, colour, opacity and
//     direction, dragged, sent the view's messages, hidden, shown, refused
//     with signs and labels off, and removed; then a sign is placed and
//     added as the sign tool adds one, shown, given a text with U+00E9,
//     highlighted, edited (the caret driven by frame messages the test
//     makes), dragged, sent the view's messages, hidden, shown, refused with
//     signs and labels off, and removed. The counts of 1 are taken again and
//     must be the same.
//   OPENSC4_TEST_PROPKINDS_EDIT=2  the same, and then a decal, an effect
//     prop, a controller prop, a burnt building, two labels ("Label" and
//     "Label" with U+00E9) and a sign ("Sign" with U+00E9) are added and
//     LEFT in the city, for a save that holds them.
//
// Left out on purpose: Write and Read (a save does them), the deleting
// destructors (Release reaches them), fade actions 1 and 2. Left out of the
// hashes because frames or the view drive them: the overlay's ID and
// manager, base flags, the fresh byte of city effects, every pointer (the
// controller's and the texture bindings only as there or not).
#include "core/automata_ctlmgr.h"
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
#include <iterator>
#include <string>
#include <unordered_set>
#include <vector>

namespace opensc4::autotest {
namespace {

namespace layout = propkinds::layout;
namespace decal = propkinds::decal;
namespace effect = propkinds::effect;
namespace automata = propkinds::automata;
namespace burnt = propkinds::burnt;
namespace label = propkinds::textdecal;
namespace sign = propkinds::sign;
using propkinds::Cdecl;
using propkinds::This;

constexpr uint32_t kOccupantType = 0xA823821E;
constexpr uint32_t kIidProp = 0xE9793A65;
constexpr uint32_t kIidMessageTarget = 0x452294AA;
constexpr uint32_t kIidOccupant = 0x26BB767E;
constexpr uint32_t kMessageFrame = 0x86AD10EE;
constexpr uint32_t kBuildingType = 0x278128A0;
// A controller template no script has.
constexpr const char* kNoTemplate = "opensc4_propkinds_no_such_template";

// The six classes, in the order of the counts and the logs.
enum class Kind { Decal, Effect, Controller, Burnt, Text, Sign };
constexpr Kind kKinds[] = {Kind::Decal, Kind::Effect, Kind::Controller, Kind::Burnt, Kind::Text, Kind::Sign};
constexpr int kKindCount = 6;

const char* NameOf(Kind kind)
{
    switch (kind) {
    case Kind::Decal:
        return "decal";
    case Kind::Effect:
        return "effect";
    case Kind::Controller:
        return "controller";
    case Kind::Burnt:
        return "burnt";
    case Kind::Text:
        return "label";
    case Kind::Sign:
        return "sign";
    }
    return "?";
}

uint32_t ClassOf(Kind kind)
{
    switch (kind) {
    case Kind::Decal:
        return decal::kClassId;
    case Kind::Effect:
        return effect::kClassId;
    case Kind::Controller:
        return automata::kClassId;
    case Kind::Burnt:
        return burnt::kClassId;
    case Kind::Text:
        return label::kClassId;
    case Kind::Sign:
        return sign::kClassId;
    }
    return 0;
}

// Where the prop interface and the occupant part are (the burnt building
// holds the prop at +0, the others at +4).
std::size_t PropOf(Kind kind) { return kind == Kind::Burnt ? 0 : layout::kProp; }
std::size_t OccupantPartOf(Kind kind) { return kind == Kind::Burnt ? burnt::kOccupant : layout::kOccupant; }
std::size_t SerializableOf(Kind kind) { return kind == Kind::Burnt ? burnt::kSerializable : layout::kSerializable; }
std::size_t FlagsOf(Kind kind) { return kind == Kind::Burnt ? burnt::kFlags : layout::kFlags; }
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

// A controller prop's plain state: the template's name, the enabled byte,
// whether there is a controller, the state and the box's size.
uint32_t ControllerPlain(char* self)
{
    Fnv f;
    const char* const begin = At<char*>(self, automata::kNameBegin);
    const char* const end = At<char*>(self, automata::kNameEnd);
    const uint32_t length = begin && end >= begin ? static_cast<uint32_t>(end - begin) : 0;
    f.Add(length);
    if (length && length < 4096)
        f.Bytes(begin, length);
    f.Add(At<uint8_t>(self, automata::kEnabled));
    f.Add(At<void*>(self, automata::kController) != nullptr);
    f.Add(At<uint8_t>(self, layout::kState));
    AddBoxSize(f, self);
    return f.h;
}

// A burnt building's plain state: the burnt box, how many texture bindings
// it holds and how many are not null, whether it has a quad, the render
// state, the scaled corner and the prop's box.
uint32_t BurntPlain(char* self)
{
    Fnv f;
    f.Bytes(self + burnt::kBox, 24);
    char* const begin = At<char*>(self, burnt::kTextures);
    char* const end = At<char*>(self, burnt::kTextures + 4);
    const uint32_t count = begin && end >= begin ? static_cast<uint32_t>((end - begin) / 4) : 0;
    f.Add(count);
    uint32_t bound = 0;
    for (uint32_t i = 0; i < count && i < 64; ++i)
        bound += At<void*>(begin, i * 4) != nullptr;
    f.Add(bound);
    f.Add(At<void*>(self, burnt::kQuad) != nullptr);
    f.Bytes(self + burnt::kRenderState, 8);
    f.Bytes(self + burnt::kScaled, 12);
    f.Bytes(self + burnt::kMin, 24);
    return f.h;
}

// The model interface (the virtual base, through the vbptr): its slots are
// thunks to the class's drawing functions and the model part's.
char* ModelOf(char* self, std::size_t vbptr)
{
    return self + vbptr + At<int32_t>(At<char*>(self, vbptr), 0x4);
}

// A burnt building's quad: the positions of its four vertices (the texture
// coordinates are left as the allocation had them).
void AddQuad(Fnv& f, char* self)
{
    char* const quad = At<char*>(self, burnt::kQuad);
    f.Add(quad != nullptr);
    if (quad)
        for (std::size_t vertex = 0; vertex < burnt::kQuadSize; vertex += burnt::kVertexSize)
            f.Bytes(quad + vertex, 12);
}

// A burnt building's model interface: the occupant it names, its box and
// whether it has a texture (slots 3, 8 and 17).
uint32_t BurntModel(char* self)
{
    Fnv f;
    char* const model = ModelOf(self, burnt::kVbptr);
    f.Add(static_cast<uint32_t>(Virtual<char*>(model, 0x0C) - self));
    uint32_t box[6] = {};
    Virtual<void>(model, 0x20, static_cast<uint32_t*>(box));
    f.Bytes(box, sizeof box);
    uint32_t texture = 0xFFFFFFFF;
    Virtual<void>(model, 0x44, &texture);
    f.Add(texture != 0);
    return f.h;
}

// A game string of the test's own (the text decal's GetText and SetText
// take one).
struct OwnString {
    char bytes[0x14];
    OwnString() { This<void*>(addr::SC4String_SC4String, &bytes[0]); }
    explicit OwnString(const char* text) { This<void*>(addr::SC4String_FromText, &bytes[0], text); }
    ~OwnString()
    {
        if (void* const buffer = At<void*>(&bytes[0], 0x4))
            Cdecl<void>(addr::SC4_Free, buffer);
    }
    OwnString(const OwnString&) = delete;
    OwnString& operator=(const OwnString&) = delete;
    void* Pointer() { return &bytes[0]; }
};

// The text of a game string inside an object (its begin and end at +4, +8).
std::string Body(char* object, std::size_t offset)
{
    const char* const begin = At<const char*>(object, offset + 4);
    const char* const end = At<const char*>(object, offset + 8);
    return begin && end >= begin && end - begin < 4096 ? std::string(begin, end) : std::string();
}

// A label's text through its GetText (slot 5).
std::string TextOf(char* self)
{
    OwnString out;
    Virtual<void*>(self, label::slot::kGetText, out.Pointer());
    return Body(static_cast<char*>(out.Pointer()), 0);
}

std::string Hex(const std::string& text)
{
    std::string out;
    char two[4];
    for (const char c : text) {
        std::snprintf(two, sizeof two, "%02X", static_cast<unsigned>(static_cast<uint8_t>(c)));
        out += two;
    }
    return out;
}

void AddText(Fnv& f, const std::string& text)
{
    f.Add(static_cast<uint32_t>(text.size()));
    f.Bytes(text.data(), text.size());
}

// A label's plain state: the text, the size, the moving byte, the place,
// the colour, the opacity and the direction. The width and the box follow
// the texture and the ground, the IDs, managers and texture the view.
uint32_t LabelPlain(char* self)
{
    Fnv f;
    AddText(f, Body(self, label::kText));
    f.Add(At<uint32_t>(self, label::kSize));
    f.Add(At<uint8_t>(self, label::kMoving));
    f.Bytes(self + label::kPosition, 8);
    f.Bytes(self + label::kColour, 12);
    f.Add(At<uint32_t>(self, label::kOpacity));
    f.Add(At<int32_t>(self, label::kDirection));
    return f.h;
}

// How many of a sign's ten texture bindings are there, and how many
// textures its text has (the reference vector of MakeGlyphs).
uint32_t SignBindings(char* self)
{
    uint32_t bound = 0;
    for (uint32_t i = 0; i < sign::kBindingCount; ++i)
        bound += At<void*>(self, sign::kBindings + 4 * i) != nullptr;
    return bound;
}

uint32_t SignGlyphs(char* self)
{
    char* const begin = At<char*>(self, sign::kGlyphs);
    char* const end = At<char*>(self, sign::kGlyphs + 4);
    return begin && end >= begin ? static_cast<uint32_t>((end - begin) / 4) : 0;
}

// A sign's plain state: the text, the position, the editing byte, the
// highlight bit, how many textures the text has and how many bindings are
// there, and the box's size (the geometry builder makes it from the text's
// textures). The caret and its clock follow the frames, the transform's
// count the view's messages.
uint32_t SignPlain(char* self)
{
    Fnv f;
    AddText(f, Body(self, sign::kText));
    f.Bytes(self + sign::kPosition, 12);
    f.Add(At<uint8_t>(self, sign::kEditing));
    f.Add(At<uint32_t>(self, sign::kModelFlags) & 1);
    f.Add(SignGlyphs(self));
    f.Add(SignBindings(self));
    AddBoxSize(f, self);
    return f.h;
}

uint32_t PlainOf(Kind kind, char* self)
{
    switch (kind) {
    case Kind::Sign:
        return SignPlain(self);
    case Kind::Text:
        return LabelPlain(self);
    case Kind::Decal:
        return DecalPlain(self);
    case Kind::Effect:
        return EffectPlain(self);
    case Kind::Controller:
        return ControllerPlain(self);
    case Kind::Burnt:
        return BurntPlain(self);
    }
    return 0;
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
    uint32_t count[kKindCount] = {}, sum[kKindCount] = {}, xorred[kKindCount] = {};
    // Not hashed (frames and the view): decals with an overlay, decals with
    // a fade running, effect props with an effect object, controller props
    // with a controller, labels with a texture and with overlays, shown,
    // started.
    uint32_t withOverlay = 0, fading = 0, withEffect = 0, withController = 0, withTexture = 0, labelOverlays = 0;
    uint32_t shown = 0, started = 0, wrongQuery = 0;
    // Signs being edited, highlighted, with textures for their text, with
    // all ten bindings.
    uint32_t signsEdited = 0, signsHighlighted = 0, signsWithGlyphs = 0, signsBound = 0;
    // The labels' and the signs' texts, sorted (for the log).
    std::vector<std::string> texts, signTexts;
    // The first building occupant of the grid (its box for a burnt building).
    char* building = nullptr;
    bool operator==(const CityCount& o) const
    {
        for (int i = 0; i < kKindCount; ++i)
            if (count[i] != o.count[i] || sum[i] != o.sum[i] || xorred[i] != o.xorred[i])
                return false;
        return true;
    }
};

CityCount WalkCity(void* city)
{
    CityCount out;
    char* const occupants = Virtual<char*>(city, 0x88);
    if (!occupants || !At<uint8_t>(occupants, occmgr_sim::field::kInitialized))
        return out;
    std::unordered_set<char*> found[kKindCount];
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
                const uint32_t type = Virtual<uint32_t>(part, 0x1C);
                if (type == kBuildingType && !out.building)
                    out.building = part;
                if (type != kOccupantType)
                    continue;
                const uint32_t classId = Virtual<uint32_t>(part + 4, 0x14);
                for (const Kind kind : kKinds)
                    if (classId == ClassOf(kind))
                        found[static_cast<int>(kind)].insert(part - OccupantPartOf(kind));
            }
        }
    }
    Virtual<void>(occupants + occmgr_sim::field::kLock, 0x8);
    // QueryInterface through the occupant part's thunk: the class ID (the
    // burnt building answers only the prop's interfaces: the prop's
    // interface, which is the object; the label and the sign not their class
    // IDs but their interfaces, which are the object).
    const auto confirm = [&out](Kind kind, char* self) {
        const uint32_t iid = kind == Kind::Burnt  ? kIidProp
                             : kind == Kind::Text ? label::kIid
                             : kind == Kind::Sign ? sign::kIid
                                                  : ClassOf(kind);
        void* answer = nullptr;
        if (Virtual<uint8_t>(self + OccupantPartOf(kind), 0x0, iid, &answer) && answer) {
            if (answer != self)
                ++out.wrongQuery;
            Virtual<uint32_t>(answer, 0x8);
        } else {
            ++out.wrongQuery;
        }
    };
    for (const Kind kind : kKinds) {
        const int i = static_cast<int>(kind);
        for (char* const self : found[i]) {
            confirm(kind, self);
            const uint32_t hash = PlainOf(kind, self);
            ++out.count[i];
            out.sum[i] += hash;
            out.xorred[i] ^= hash;
            out.shown += (At<uint8_t>(self, FlagsOf(kind)) & 2) != 0;
            switch (kind) {
            case Kind::Decal:
                out.withOverlay += At<void*>(self, decal::kOverlays) != nullptr;
                out.fading += At<float>(self, decal::kFadeRate) != 0.0f;
                break;
            case Kind::Effect:
                out.withEffect += At<void*>(self, effect::kEffect) != nullptr;
                out.started += At<uint8_t>(self, effect::kStarted) != 0;
                break;
            case Kind::Controller:
                out.withController += At<void*>(self, automata::kController) != nullptr;
                break;
            case Kind::Burnt:
                break;
            case Kind::Text:
                out.withTexture += At<void*>(self, label::kTexture) != nullptr;
                out.labelOverlays += (At<uint32_t>(self, label::kOverlayId) != 0xFFFFFFFF) +
                                     (At<uint32_t>(self, label::kOverlayId2) != 0xFFFFFFFF);
                out.texts.push_back(Hex(Body(self, label::kText)));
                break;
            case Kind::Sign:
                out.signsEdited += At<uint8_t>(self, sign::kEditing) != 0;
                out.signsHighlighted += (At<uint32_t>(self, sign::kModelFlags) & 1) != 0;
                out.signsWithGlyphs += SignGlyphs(self) != 0;
                out.signsBound += SignBindings(self) == sign::kBindingCount;
                {
                    // The text, then the position and the box's size (bits).
                    char where[80];
                    std::snprintf(where, sizeof where, " at %08X %08X %08X size %08X %08X %08X", At<uint32_t>(self, sign::kPosition),
                                  At<uint32_t>(self, sign::kPosition + 4), At<uint32_t>(self, sign::kPosition + 8),
                                  Bits(At<float>(self, layout::kMax) - At<float>(self, layout::kMin)),
                                  Bits(At<float>(self, layout::kMax + 4) - At<float>(self, layout::kMin + 4)),
                                  Bits(At<float>(self, layout::kMax + 8) - At<float>(self, layout::kMin + 8)));
                    out.signTexts.push_back(Hex(Body(self, sign::kText)) + where);
                }
                break;
            }
        }
    }
    std::sort(out.texts.begin(), out.texts.end());
    std::sort(out.signTexts.begin(), out.signTexts.end());
    return out;
}

// --- 2. Made props -----------------------------------------------------------------------

// A prop of the class from its allocator and constructor, as the terrain
// code makes its decal and the lot developer its burnt building and the
// sign tool its sign (the most derived object: 1); one reference, the
// caller's.
char* MakeDirect(Kind kind)
{
    void* memory = nullptr;
    char* self = nullptr;
    switch (kind) {
    case Kind::Decal:
        if ((memory = Cdecl<void*>(addr::cSC4PropOccupantTerrainDecal_Allocate, std::size_t{decal::kObjectSize})))
            self = static_cast<char*>(This<void*>(addr::cSC4PropOccupantTerrainDecal_Construct, memory));
        break;
    case Kind::Effect:
        if ((memory = Cdecl<void*>(addr::cSC4PropOccupantVisualEffect_Allocate, std::size_t{effect::kObjectSize})))
            self = static_cast<char*>(This<void*>(addr::cSC4PropOccupantVisualEffect_Construct, memory));
        break;
    case Kind::Controller:
        if ((memory = Cdecl<void*>(addr::cSC4PropOccupantAutomataController_Allocate, std::size_t{automata::kObjectSize})))
            self = static_cast<char*>(This<void*>(addr::cSC4PropOccupantAutomataController_Construct, memory));
        break;
    case Kind::Burnt:
        if ((memory = Cdecl<void*>(addr::cSC4PropOccupantBurntBuilding_Allocate, std::size_t{burnt::kObjectSize})))
            self = static_cast<char*>(This<void*, uint32_t>(addr::cSC4PropOccupantBurntBuilding_Construct, memory, 1u));
        break;
    case Kind::Text:
        if ((memory = Cdecl<void*>(addr::cSC4PropOccupantTextDecal_Allocate, std::size_t{label::kObjectSize})))
            self = static_cast<char*>(This<void*>(addr::cSC4PropOccupantTextDecal_Construct, memory));
        break;
    case Kind::Sign:
        if ((memory = Cdecl<void*>(addr::cSC4PropOccupantSign_Allocate, std::size_t{sign::kObjectSize})))
            self = static_cast<char*>(This<void*, uint32_t>(addr::cSC4PropOccupantSign_Construct, memory, 1u));
        break;
    }
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

// The same for a controller prop: while it has no controller, a template
// no script has makes none (the manager is asked and answers null), so
// nothing is added to the automata.
uint32_t DriveControllerFields(char* self)
{
    Fnv f;
    AddQueries(f, self, {automata::kIid, automata::kClassId, kIidMessageTarget, kIidProp, kIidOccupant, 1u, 0x12345678u});
    f.Add(static_cast<uint32_t>(Virtual<char*>(self, 0x0C) - self));
    f.Add(static_cast<uint32_t>(Virtual<char*>(self, 0x10) - self));
    f.Add(ControllerPlain(self));
    const bool quiet = At<void*>(self, automata::kController) == nullptr;
    f.Add(quiet);
    if (quiet) {
        const uint8_t enabled = At<uint8_t>(self, automata::kEnabled);
        f.Add(static_cast<uint8_t>(Virtual<uint32_t>(self, automata::slot::kSetEnabled, 1u)));
        f.Add(At<uint8_t>(self, automata::kEnabled));
        // The template: the controller made again (none), then the same
        // name, which is not made again.
        Virtual<void>(self, automata::slot::kSetTemplateName, kNoTemplate);
        f.Add(ControllerPlain(self));
        Virtual<void>(self, automata::slot::kSetTemplateName, kNoTemplate);
        f.Add(ControllerPlain(self));
        // A state other than 0 asks for "name_state"; then back.
        const uint32_t state = At<uint8_t>(self, layout::kState);
        Virtual<void>(self + layout::kProp, 0x2C, state ^ 1u);
        f.Add(ControllerPlain(self));
        Virtual<void>(self + layout::kProp, 0x2C, state ^ 1u);
        Virtual<void>(self + layout::kProp, 0x2C, state);
        f.Add(ControllerPlain(self));
        f.Add(static_cast<uint8_t>(Virtual<uint32_t>(self, automata::slot::kSetEnabled, uint32_t{enabled})));
        f.Add(At<uint8_t>(self, automata::kEnabled));
    }
    const float position[3] = {1000.0f, 271.5f, 2000.25f};
    Virtual<void>(self + layout::kOccupant, 0x24, static_cast<const float*>(position));
    f.Bytes(self + layout::kMin, 24);
    const uint32_t flags = At<uint8_t>(self, layout::kFlags);
    Virtual<uint32_t>(self + layout::kOccupant, 0x78, flags ^ 0x40u);
    f.Add(static_cast<uint8_t>(At<uint8_t>(self, layout::kFlags) ^ flags));
    Virtual<uint32_t>(self + layout::kOccupant, 0x78, flags);
    f.Add(ControllerPlain(self));
    return f.h;
}

// A burnt building as the lot developer sets one up, never inserted: the
// box of `source` (a building of the city, or the object's own occupant
// part) and flag 0x40; a change of a flag other than 2. Init and Shutdown
// are left to the round trip: the base's send the occupant messages
// 0x99EF1140 and 0x99EF1141, which a prop outside the city should not. So
// the quad SetOccupant makes is not freed (Shutdown frees it; the
// destructor does not), as in the game. QueryInterface answers the prop's
// interfaces only.
uint32_t DriveBurntFields(char* self, void* source)
{
    Fnv f;
    char* const occupant = self + burnt::kOccupant;
    for (const uint32_t iid : {kIidProp, kIidMessageTarget, kIidOccupant, burnt::kClassId, 1u, 0x12345678u}) {
        void* found = nullptr;
        const bool answered = Virtual<uint8_t>(self, 0x0, iid, &found) != 0;
        f.Add(answered);
        if (answered && found) {
            f.Add(static_cast<uint32_t>(static_cast<char*>(found) - self));
            Virtual<uint32_t>(found, 0x8);
        }
    }
    f.Add(Virtual<uint32_t>(self + burnt::kSerializable, 0x14));
    f.Add(BurntPlain(self));
    f.Add(At<int32_t>(self, burnt::kVtordisp));
    This<void, void*>(addr::cSC4PropOccupantBurntBuilding_SetOccupant, self, source ? source : static_cast<void*>(occupant));
    f.Add(BurntPlain(self));
    AddQuad(f, self);
    f.Add(BurntModel(self));
    Virtual<uint32_t>(occupant, 0x74, 0x40u);
    f.Add(At<uint8_t>(self, burnt::kFlags));
    f.Add(Virtual<uint8_t>(occupant, 0x14));
    // Shutdown of a prop that is not initialized: nothing.
    f.Add(Virtual<uint8_t>(occupant, 0x10));
    const uint32_t flags = At<uint8_t>(self, burnt::kFlags);
    Virtual<uint32_t>(occupant, 0x78, flags ^ 0x80u);
    f.Add(static_cast<uint8_t>(At<uint8_t>(self, burnt::kFlags) ^ flags));
    Virtual<uint32_t>(occupant, 0x78, flags);
    f.Add(BurntPlain(self));
    return f.h;
}

// The texts the tests give labels: ASCII, and one with U+00E9 (UTF-8).
constexpr const char* kLabelText = "Label";
constexpr const char* kLabelAccent = "Label \xC3\xA9";

void SetLabelText(char* self, const char* text)
{
    OwnString source(text);
    Virtual<void>(self, label::slot::kSetText, source.Pointer());
}

// What a label leaves for its overlays: the width, the box, whether each
// overlay manager and each ID is there, whether it has a texture.
void AddOverlayState(Fnv& f, char* self)
{
    f.Add(At<uint32_t>(self, label::kWidth));
    f.Bytes(self + layout::kMin, 24);
    f.Add(At<void*>(self, label::kOverlays) != nullptr);
    f.Add(At<void*>(self, label::kOverlays2) != nullptr);
    f.Add(At<uint32_t>(self, label::kOverlayId) != 0xFFFFFFFF);
    f.Add(At<uint32_t>(self, label::kOverlayId2) != 0xFFFFFFFF);
    f.Add(At<void*>(self, label::kTexture) != nullptr);
}

// A label as the label tool and the TextDecal command make one, never
// inserted and never shown (base flag 2 clear): the setters only write
// fields, MakeOverlays stops before the overlay managers. The moving byte
// picks the managers (references to the terrain view's), and a text makes
// a texture; the destructor lets both go.
uint32_t DriveLabelFields(char* self)
{
    Fnv f;
    AddQueries(f, self, {label::kIid, label::kClassId, kIidMessageTarget, kIidProp, kIidOccupant, 1u, 0x12345678u});
    f.Add(static_cast<uint32_t>(Virtual<char*>(self, 0x0C) - self));
    f.Add(LabelPlain(self));
    AddOverlayState(f, self);
    // Sizes: 0, 8 and below 0 are refused, 0.8 kept.
    for (const float size : {0.0f, 8.0f, -1.0f, 0.8f}) {
        Virtual<void>(self, label::slot::kSetSize, size);
        f.Add(At<uint32_t>(self, label::kSize));
    }
    // A colour within 0.0001 of the one kept is no change.
    Virtual<void>(self, label::slot::kSetColour, 255.00002f, 255.0f, 254.99998f);
    f.Bytes(self + label::kColour, 12);
    Virtual<void>(self, label::slot::kSetColour, 128.0f, 64.0f, 32.0f);
    f.Bytes(self + label::kColour, 12);
    Virtual<void>(self, label::slot::kSetOpacity, 1.0f);
    Virtual<void>(self, label::slot::kSetOpacity, 0.5f);
    f.Add(At<uint32_t>(self, label::kOpacity));
    Virtual<void>(self, label::slot::kSetDirection, int32_t{1});
    Virtual<void>(self, label::slot::kSetDirection, int32_t{0});
    f.Add(At<int32_t>(self, label::kDirection));
    const float position[3] = {1000.0f, 271.5f, 2000.25f};
    Virtual<void>(self + layout::kOccupant, 0x24, static_cast<const float*>(position));
    f.Add(LabelPlain(self));
    f.Bytes(self + layout::kMin, 24);
    // Moving: the managers 2 and 3 picked; the setters go to the overlays
    // there are (none).
    Virtual<void>(self, label::slot::kSetMoving, 1u);
    AddOverlayState(f, self);
    const float moved[3] = {1004.0f, 271.5f, 2003.0f};
    Virtual<void>(self + layout::kOccupant, 0x24, static_cast<const float*>(moved));
    Virtual<void>(self, label::slot::kSetColour, 10.0f, 20.0f, 30.0f);
    Virtual<void>(self, label::slot::kSetOpacity, 0.25f);
    f.Add(LabelPlain(self));
    Virtual<void>(self, label::slot::kSetMoving, 0u);
    AddOverlayState(f, self);
    // The text, ASCII and not, and back through GetText.
    SetLabelText(self, kLabelText);
    const bool texture = At<void*>(self, label::kTexture) != nullptr;
    const std::string ascii = TextOf(self);
    AddText(f, ascii);
    SetLabelText(self, kLabelAccent);
    const std::string accent = TextOf(self);
    AddText(f, accent);
    f.Add(texture);
    AddOverlayState(f, self);
    // A change of a flag other than 2 goes to the base alone.
    const uint32_t flags = At<uint8_t>(self, layout::kFlags);
    Virtual<uint32_t>(self + layout::kOccupant, 0x78, flags ^ 0x40u);
    f.Add(static_cast<uint8_t>(At<uint8_t>(self, layout::kFlags) ^ flags));
    Virtual<uint32_t>(self + layout::kOccupant, 0x78, flags);
    f.Add(LabelPlain(self));
    log::Write("autotest: propkinds: label fields: texts %s %s, texture %d, width %08X", Hex(ascii).c_str(), Hex(accent).c_str(),
               texture ? 1 : 0, At<uint32_t>(self, label::kWidth));
    return f.h;
}

// The texts the tests give signs: ASCII, and one with U+00E9 (UTF-8).
constexpr const char* kSignText = "Sign";
constexpr const char* kSignAccent = "Sign \xC3\xA9";

void SetSignText(char* self, const char* text)
{
    OwnString source(text);
    Virtual<void>(self, sign::slot::kSetText, source.Pointer());
}

// A sign's text through its GetText (slot 5).
std::string SignTextOf(char* self)
{
    OwnString out;
    Virtual<void*>(self, sign::slot::kGetText, out.Pointer());
    return Body(static_cast<char*>(out.Pointer()), 0);
}

// What a sign's transform and boxes hold: the transform's count and flags,
// the rotation, the position, the scale, the four boxes, the displacement,
// the model part's flags, the editing byte, how many textures and bindings.
void AddSignState(Fnv& f, char* self)
{
    f.Add(At<uint8_t>(self, sign::kTransformCount));
    f.Add(At<uint8_t>(self, sign::kTransformFlags));
    f.Bytes(self + sign::kRotation, 0x34);
    f.Bytes(self + sign::kBoxes, 0x60);
    f.Add(At<int32_t>(self, sign::kVtordisp));
    f.Add(At<uint32_t>(self, sign::kModelFlags));
    f.Add(At<uint8_t>(self, sign::kEditing));
    f.Add(SignGlyphs(self));
    f.Add(SignBindings(self));
}

// The four corner colours SetHighlight writes.
void AddHighlight(Fnv& f, char* self)
{
    for (std::size_t corner = 0; corner < 0x40; corner += 0x10)
        f.Add(At<uint32_t>(self, sign::kHighlight + corner));
}

// A frame message as the frame service sends it: the time in data 2 (the
// sign gives its caret's clock the time over 1000).
void Frame(char* self, uint32_t time)
{
    Deliver(self + layout::kProp, kMessageFrame, nullptr, time, 0);
}

// What a sign's geometry builder made: the eleven quads, the board, the
// caret (only after an edit: nothing sets it before) and the text's
// vertices.
void AddSignGeometry(Fnv& f, char* self, bool caret)
{
    f.Bytes(self + sign::kQuads, sign::kQuadCount * sign::kQuadSize);
    f.Bytes(self + sign::kBoard, 0x40);
    if (caret)
        f.Bytes(self + sign::kCaretQuad, 0x40);
    char* const begin = At<char*>(self, sign::kGlyphVertices);
    char* const end = At<char*>(self, sign::kGlyphVertices + 4);
    f.Add(static_cast<uint32_t>(end - begin));
    if (begin && end > begin && end - begin <= 0x10000)
        f.Bytes(begin, static_cast<std::size_t>(end - begin));
}

// A ray as the sign's hit test takes it: the origin, the direction, then
// the distance so far at +0x28.
struct Ray {
    float origin[3];
    float direction[3];
    uint32_t unused[4];
    float distance;
};

// A sign's model interface (slots 3, 7 to 11 and 17): the occupant it
// names, the two boxes, the transform read and given back (the count
// raised), three rays straight down (through the board's middle with room,
// the same with too little, one beside the sign) and the texture.
uint32_t SignModel(char* self)
{
    Fnv f;
    char* const model = ModelOf(self, sign::kVbptr);
    f.Add(static_cast<uint32_t>(Virtual<char*>(model, 0x0C) - self));
    uint32_t box[6] = {};
    Virtual<void>(model, 0x1C, static_cast<uint32_t*>(box));
    f.Bytes(box, sizeof box);
    Virtual<void>(model, 0x20, static_cast<uint32_t*>(box));
    f.Bytes(box, sizeof box);
    uint8_t transform[0x38] = {};
    Virtual<void>(model, 0x24, static_cast<uint8_t*>(transform));
    f.Bytes(transform, sizeof transform);
    const uint8_t count = At<uint8_t>(self, sign::kTransformCount);
    Virtual<void>(model, 0x28, static_cast<uint8_t*>(transform));
    f.Add(static_cast<uint8_t>(At<uint8_t>(self, sign::kTransformCount) - count));
    f.Bytes(self + sign::kRotation, 0x34);
    const float* const board = &At<float>(self, sign::kBoardBox);
    const float middle[2] = {(board[0] + board[3]) * 0.5f, (board[2] + board[5]) * 0.5f};
    uint32_t hits[3];
    const float starts[3][2] = {{middle[0], 1.0e9f}, {middle[0], 1.0f}, {middle[0] + 1000.0f, 1.0e9f}};
    for (int i = 0; i < 3; ++i) {
        Ray ray{{starts[i][0], board[4] + 100.0f, middle[1]}, {0.0f, -1.0f, 0.0f}, {}, starts[i][1]};
        hits[i] = Virtual<uint8_t>(model, 0x2C, &ray);
        f.Add(hits[i]);
        f.Add(ray.distance);
    }
    uint32_t texture = 0xFFFFFFFF;
    Virtual<void>(model, 0x44, &texture);
    f.Add(texture);
    log::Write("autotest: propkinds: sign model: hits %u %u %u, texture %08X", hits[0], hits[1], hits[2], texture);
    return f.h;
}

// A sign as the sign tool makes one, never inserted and never shown (base
// flag 2 clear): the renderer is not told. The text makes textures and the
// geometry is built (the game's builder); the highlight writes the corner
// colours; editing listens to the frame service until it ends, and the
// caret's clock is driven by frame messages the test makes. Shutdown of a
// sign that is not initialized does nothing. The destructor lets the
// textures go.
uint32_t DriveSignFields(char* self)
{
    Fnv f;
    AddQueries(f, self, {sign::kIid, sign::kClassId, kIidMessageTarget, kIidProp, kIidOccupant, 1u, 0x12345678u});
    f.Add(static_cast<uint32_t>(Virtual<char*>(self, 0x0C) - self));
    f.Add(SignPlain(self));
    AddSignState(f, self);
    const float position[3] = {1000.0f, 271.5f, 2000.25f};
    Virtual<void>(self + layout::kOccupant, 0x24, static_cast<const float*>(position));
    AddSignState(f, self);
    f.Bytes(self + layout::kMin, 24);
    const float moved[3] = {1003.75f, 270.125f, 1999.5f};
    Virtual<void>(self + layout::kOccupant, 0x24, static_cast<const float*>(moved));
    AddSignState(f, self);
    // The text, ASCII, the same again (nothing), then not ASCII; and back
    // through GetText.
    SetSignText(self, kSignText);
    const std::string ascii = SignTextOf(self);
    AddText(f, ascii);
    AddSignState(f, self);
    const uint32_t glyphs = SignGlyphs(self);
    const uint8_t count = At<uint8_t>(self, sign::kTransformCount);
    SetSignText(self, kSignText);
    f.Add(static_cast<uint8_t>(At<uint8_t>(self, sign::kTransformCount) - count));
    SetSignText(self, kSignAccent);
    const std::string accent = SignTextOf(self);
    AddText(f, accent);
    AddSignState(f, self);
    AddSignGeometry(f, self, false);
    f.Bytes(self + layout::kMin, 24);
    // Highlighted, again (nothing), and not.
    Virtual<void>(self, sign::slot::kSetHighlight, 1u);
    f.Add(At<uint32_t>(self, sign::kModelFlags));
    AddHighlight(f, self);
    Virtual<void>(self, sign::slot::kSetHighlight, 1u);
    f.Add(At<uint32_t>(self, sign::kModelFlags));
    Virtual<void>(self, sign::slot::kSetHighlight, 0u);
    f.Add(At<uint32_t>(self, sign::kModelFlags));
    AddHighlight(f, self);
    // Edited: the caret's clock by frames of 300, 300 (the caret flips),
    // 499 and 1 (it flips back), then a message that is not a frame.
    Virtual<void>(self, sign::slot::kSetEditing, 1u);
    f.Add(At<uint8_t>(self, sign::kEditing));
    f.Add(At<uint32_t>(self, sign::kModelFlags));
    uint32_t caret[5][2] = {};
    std::size_t step = 0;
    for (const uint32_t time : {300000u, 300999u, 499000u, 1000u}) {
        Frame(self, time);
        caret[step][0] = At<uint8_t>(self, sign::kCaret);
        caret[step][1] = At<uint32_t>(self, sign::kCaretTime);
        ++step;
    }
    Deliver(self + layout::kProp, 0x12345678, nullptr, 600000u, 0);
    caret[step][0] = At<uint8_t>(self, sign::kCaret);
    caret[step][1] = At<uint32_t>(self, sign::kCaretTime);
    f.Bytes(caret, sizeof caret);
    AddSignGeometry(f, self, true);
    // Highlight taken off while editing: the edit ends.
    Virtual<void>(self, sign::slot::kSetHighlight, 0u);
    f.Add(At<uint8_t>(self, sign::kEditing));
    f.Add(At<uint32_t>(self, sign::kModelFlags));
    Virtual<void>(self, sign::slot::kSetEditing, 1u);
    Virtual<void>(self, sign::slot::kSetEditing, 0u);
    f.Add(At<uint8_t>(self, sign::kEditing));
    f.Add(At<uint32_t>(self, sign::kModelFlags));
    // The camera moved: the geometry built again.
    Deliver(self + layout::kProp, 0xA6B79602, nullptr, 0, 0);
    AddSignState(f, self);
    // A change of a flag other than 2 goes to the base alone; Shutdown of a
    // sign that is not initialized does nothing.
    const uint32_t flags = At<uint8_t>(self, layout::kFlags);
    Virtual<uint32_t>(self + layout::kOccupant, 0x78, flags ^ 0x40u);
    f.Add(static_cast<uint8_t>(At<uint8_t>(self, layout::kFlags) ^ flags));
    Virtual<uint32_t>(self + layout::kOccupant, 0x78, flags);
    f.Add(Virtual<uint8_t>(self + layout::kOccupant, 0x10));
    f.Add(SignPlain(self));
    // A shorter text (the vertices cut), then the model interface.
    SetSignText(self, kSignText);
    AddSignState(f, self);
    AddSignGeometry(f, self, false);
    f.Add(SignModel(self));
    log::Write("autotest: propkinds: sign fields: texts %s %s, glyphs %u then %u, caret %u/%u %u/%u %u/%u %u/%u %u/%u",
               Hex(ascii).c_str(), Hex(accent).c_str(), glyphs, SignGlyphs(self), caret[0][0], caret[0][1], caret[1][0],
               caret[1][1], caret[2][0], caret[2][1], caret[3][0], caret[3][1], caret[4][0], caret[4][1]);
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
    std::vector<uint32_t> of[kKindCount];
    const std::vector<uint32_t>& operator[](Kind kind) const { return of[static_cast<int>(kind)]; }
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
        for (const Kind kind : kKinds)
            if (profile[0] == ClassOf(kind))
                out.of[static_cast<int>(kind)].push_back(type);
    }
    return out;
}

// The object of a prop the manager made (it hands out the prop interface:
// object + 4, the burnt building's object itself), when its class is the
// one wanted.
char* ObjectOf(void* prop, Kind kind)
{
    if (!prop)
        return nullptr;
    char* const self = static_cast<char*>(prop) - PropOf(kind);
    return Virtual<uint32_t>(self + SerializableOf(kind), 0x14) == ClassOf(kind) ? self : nullptr;
}

uint32_t DriveFields(Kind kind, char* self, void* building)
{
    switch (kind) {
    case Kind::Decal:
        return DriveDecalFields(self);
    case Kind::Effect:
        return DriveEffectFields(self);
    case Kind::Controller:
        return DriveControllerFields(self);
    case Kind::Burnt:
        return DriveBurntFields(self, building);
    case Kind::Text:
        return DriveLabelFields(self);
    case Kind::Sign:
        return DriveSignFields(self);
    }
    return 0;
}

void DriveMade(Fnv& f, void* m, const Types& types, void* building)
{
    for (const Kind kind : kKinds) {
        char* const self = MakeDirect(kind);
        const uint32_t h = !self ? 0xEEEEEEEEu : DriveFields(kind, self, building);
        if (self)
            Virtual<uint32_t>(self, 0x8);
        f.Add(h);
        log::Write("autotest: propkinds: made direct %s: %08X", NameOf(kind), h);
    }
    if (!m)
        return;
    for (const Kind kind : kKinds) {
        const std::vector<uint32_t>& list = types[kind];
        for (std::size_t i = 0; i < list.size() && i < 2; ++i) {
            void* prop = nullptr;
            const uint8_t made = Virtual<uint8_t>(m, manager::kCreateProp, list[i], &prop);
            char* const self = ObjectOf(prop, kind);
            Fnv mine;
            mine.Add(made);
            mine.Add(self != nullptr);
            if (self)
                mine.Add(DriveFields(kind, self, building));
            if (prop)
                Virtual<uint32_t>(prop, 0x8);
            f.Add(mine.h);
            log::Write("autotest: propkinds: made %s type %08X: %08X", NameOf(kind), list[i], mine.h);
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

// The same for a sign, whose box is far wider than a cell (about 92 m by
// 38 m with the test's text, the long side along x): anywhere but the
// city's outer 64 m, a cell with no lot in the middle of its 64 m occupant
// cell (the third of its four columns and rows) whose occupant cell and the
// two beside it along x are empty. The box reaches into those two and stays
// inside its own along z.
bool FindSignSpot(void* m, int32_t sizeX, int32_t sizeZ, int32_t* spotX, int32_t* spotZ)
{
    void* const lots = At<void*>(m, manager::kLots);
    void* const occupants = At<void*>(m, manager::kOccupants);
    if (!lots || !occupants)
        return false;
    for (int32_t z = 6; z < sizeZ - 4; z += 4)
        for (int32_t x = 6; x < sizeX - 4; x += 4) {
            if (Virtual<void*>(lots, 0x18, x, z, uint32_t{0}))
                continue;
            bool empty = true;
            for (int32_t dx = -1; dx <= 1 && empty; ++dx)
                empty = (Virtual<uint8_t>(occupants, 0x64, x / 4 + 64 + dx, z / 4 + 64) & 1) != 0;
            if (!empty)
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
char* MakeForEdit(void* m, const std::vector<uint32_t>& types, Kind kind, const char** how)
{
    if (!types.empty()) {
        uint32_t key[3] = {};
        if (Virtual<uint8_t>(m, manager::kGetPropKeyFromType, types[0], key)) {
            void* prop = nullptr;
            if (Virtual<uint8_t>(m, manager::kCreateGenericProp, static_cast<const uint32_t*>(key), &prop) && prop) {
                if (char* const self = ObjectOf(prop, kind)) {
                    *how = "CreateGenericProp";
                    return self;
                }
                Virtual<uint32_t>(prop, 0x8);
            }
        }
    }
    *how = "constructor";
    return MakeDirect(kind);
}

// What the removal left of a prop the test still holds.
void AddRemoved(Fnv& f, char* self, Kind kind = Kind::Decal)
{
    f.Add(Virtual<uint8_t>(self + OccupantPartOf(kind), 0x14));
    f.Add(static_cast<uint8_t>(At<uint8_t>(self, FlagsOf(kind)) & 1));
}

void EditDecal(Fnv& f, void* m, const Types& types, const float* position, const int32_t* cells)
{
    const char* how = "";
    char* const self = MakeForEdit(m, types[Kind::Decal], Kind::Decal, &how);
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
    char* const self = MakeForEdit(m, types[Kind::Effect], Kind::Effect, &how);
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

// A template the script system has, for the round trip: the smallest name
// of the templates of the automata controller manager's controllers (empty
// when it has none).
std::string SomeTemplate(void* city)
{
    namespace ctl = automata_ctlmgr;
    char* const manager = Virtual<char*>(city, ctl::kCitySlotManager);
    if (!manager)
        return {};
    std::string best;
    const auto* const table = reinterpret_cast<const ctl::HashTable*>(manager + ctl::field::kControllers);
    const uint32_t buckets = static_cast<uint32_t>(table->end - table->begin);
    for (uint32_t i = 0; i < buckets && i < 1000000; ++i)
        for (auto* node = static_cast<ctl::ControllerNode*>(table->begin[i]); node; node = node->next)
            for (ctl::ListNode* e = node->head->next; e != node->head; e = e->next) {
                char* const t = e->value ? Virtual<char*>(e->value, ctl::controller::kTemplate) : nullptr;
                if (!t)
                    continue;
                const char* const begin = At<const char*>(t, ctl::tmpl::kNameBegin);
                const char* const end = At<const char*>(t, ctl::tmpl::kNameEnd);
                if (!begin || end <= begin || end - begin > 255)
                    continue;
                std::string name(begin, end);
                if (best.empty() || name < best)
                    best = name;
            }
    return best;
}

// The controller prop's template name as a std::string (for logs and to
// give it back).
std::string NameOfController(char* self)
{
    const char* const begin = At<char*>(self, automata::kNameBegin);
    const char* const end = At<char*>(self, automata::kNameEnd);
    if (!begin || end < begin || end - begin > 4096)
        return {};
    return std::string(begin, end);
}

void EditController(Fnv& f, void* m, const Types& types, const float* position, const int32_t* cells,
                    const std::string& known)
{
    const char* how = "";
    char* const self = MakeForEdit(m, types[Kind::Controller], Kind::Controller, &how);
    if (!self) {
        f.Add(uint8_t{0xEE});
        log::Write("autotest: propkinds: edit controller: none made");
        return;
    }
    char* const occupant = self + layout::kOccupant;
    char* const prop = self + layout::kProp;
    Fnv mine;
    Virtual<void>(occupant, 0x24, position);
    Virtual<uint32_t>(prop, 0x28, int32_t{0});
    // Added: Init takes the template from the exemplar and the manager makes
    // the controller when a script has the template.
    const uint8_t added = Virtual<uint8_t>(m, manager::kAddCityProp, static_cast<void*>(occupant));
    mine.Add(added);
    mine.Add(ControllerPlain(self));
    mine.Add(Virtual<uint8_t>(occupant, 0x14));
    mine.Add(static_cast<uint8_t>(At<uint8_t>(self, layout::kFlags) & 1));
    // Without a template from the exemplar, one the scripts have.
    if (NameOfController(self).empty() && !known.empty())
        Virtual<void>(self, automata::slot::kSetTemplateName, known.c_str());
    mine.Add(ControllerPlain(self));
    const std::string name = NameOfController(self);
    const bool made = At<void*>(self, automata::kController) != nullptr;
    const uint32_t afterAdd = mine.h;
    // Shown, as the view does it (the base's alone), then disabled and
    // enabled: the controller is made inactive and active.
    if (!(At<uint8_t>(self, layout::kFlags) & 2))
        Virtual<uint32_t>(occupant, 0x78, uint32_t{At<uint8_t>(self, layout::kFlags)} | 2u);
    mine.Add(static_cast<uint8_t>(Virtual<uint32_t>(self, automata::slot::kSetEnabled, 0u)));
    mine.Add(ControllerPlain(self));
    mine.Add(static_cast<uint8_t>(Virtual<uint32_t>(self, automata::slot::kSetEnabled, 1u)));
    mine.Add(ControllerPlain(self));
    // The states: the next one ("name_1" and so on), then back.
    const uint32_t state = At<uint8_t>(self, layout::kState);
    Virtual<void>(prop, 0x2C, state + 1);
    mine.Add(ControllerPlain(self));
    const bool madeForState = At<void*>(self, automata::kController) != nullptr;
    Virtual<void>(prop, 0x2C, state);
    mine.Add(ControllerPlain(self));
    const uint32_t afterStates = mine.h;
    // A template no script has, then its own back.
    Virtual<void>(self, automata::slot::kSetTemplateName, kNoTemplate);
    mine.Add(ControllerPlain(self));
    if (!name.empty())
        Virtual<void>(self, automata::slot::kSetTemplateName, name.c_str());
    mine.Add(ControllerPlain(self));
    const bool madeAgain = At<void*>(self, automata::kController) != nullptr;
    const uint32_t afterNames = mine.h;
    const uint8_t removed = Virtual<uint8_t>(m, manager::kRemoveCityPropsInArea, cells);
    mine.Add(removed);
    AddRemoved(mine, self);
    mine.Add(ControllerPlain(self));
    log::Write("autotest: propkinds: edit controller (%s): added %u %08X, template \"%s\", controller %d, for state %u %d, "
               "states %08X, names %08X (again %d); removed %u, initialized %u, in manager %u, controller %d",
               how, added, afterAdd, name.c_str(), made ? 1 : 0, state + 1, madeForState ? 1 : 0, afterStates, afterNames,
               madeAgain ? 1 : 0, removed, Virtual<uint8_t>(occupant, 0x14), At<uint8_t>(self, layout::kFlags) & 1,
               At<void*>(self, automata::kController) != nullptr ? 1 : 0);
    if (Virtual<uint8_t>(occupant, 0x14)) {
        // Not removed: its Shutdown takes the controller off the manager
        // before the reference goes.
        log::Write("autotest: propkinds: edit controller: STILL INITIALIZED after the removal; shut down by the test");
        Virtual<uint8_t>(occupant, 0x10);
    }
    Virtual<uint32_t>(self, 0x8);
    f.Add(mine.h);
}

// A burnt building set up at the spot as the lot developer sets one up from
// a building, but from its own box: the prop sized 16 m by 8 m by 16 m and
// moved to the position, SetOccupant with its own occupant part (the box
// copied into the burnt box, the quad built), flag 0x40.
void PlaceBurnt(char* self, const float* position)
{
    char* const occupant = self + burnt::kOccupant;
    const float size[3] = {16.0f, 8.0f, 16.0f};
    This<void, const float*>(addr::cSC4PropOccupant_SetSize, self, static_cast<const float*>(size));
    Virtual<void>(occupant, 0x24, position);
    Virtual<uint32_t>(self, 0x28, int32_t{0});
    This<void, void*>(addr::cSC4PropOccupantBurntBuilding_SetOccupant, self, static_cast<void*>(occupant));
    Virtual<uint32_t>(occupant, 0x74, 0x40u);
}

void EditBurnt(Fnv& f, void* m, const Types& types, const float* position, const int32_t* cells)
{
    const char* how = "";
    char* const self = MakeForEdit(m, types[Kind::Burnt], Kind::Burnt, &how);
    if (!self) {
        f.Add(uint8_t{0xEE});
        log::Write("autotest: propkinds: edit burnt: none made");
        return;
    }
    char* const occupant = self + burnt::kOccupant;
    Fnv mine;
    PlaceBurnt(self, position);
    mine.Add(BurntPlain(self));
    // Added: Init loads the texture bindings and sets up the render state.
    const uint8_t added = Virtual<uint8_t>(m, manager::kAddCityProp, static_cast<void*>(occupant));
    mine.Add(added);
    mine.Add(BurntPlain(self));
    mine.Add(Virtual<uint8_t>(occupant, 0x14));
    mine.Add(static_cast<uint8_t>(At<uint8_t>(self, burnt::kFlags) & 1));
    AddQuad(mine, self);
    mine.Add(BurntModel(self));
    const uint32_t afterAdd = mine.h;
    // Shown, hidden, shown and hidden, as the view does it: the model goes to
    // the renderer and is taken off again. It is hidden before the removal,
    // so the renderer holds nothing of it after.
    void* const renderer = Global<void*>(addr::SC4_View43DD0);
    void* const model = self + burnt::kVbptr + At<int32_t>(At<char*>(self, burnt::kVbptr), 0x4);
    uint32_t held[4] = {};
    const bool wasShown = (At<uint8_t>(self, burnt::kFlags) & 2) != 0;
    if (renderer) {
        for (uint32_t& h : held) {
            const uint32_t flags = At<uint8_t>(self, burnt::kFlags);
            Virtual<uint32_t>(occupant, 0x78, (flags & 2) ? flags & ~2u : flags | 2u);
            h = Virtual<uint8_t>(renderer, 0x6C, model);
            mine.Add(static_cast<uint8_t>(At<uint8_t>(self, burnt::kFlags) & 2));
            mine.Add(h);
        }
        if (At<uint8_t>(self, burnt::kFlags) & 2)
            Virtual<uint32_t>(occupant, 0x78, uint32_t{At<uint8_t>(self, burnt::kFlags)} & ~2u);
    }
    const uint32_t afterShown = mine.h;
    const uint8_t removed = Virtual<uint8_t>(m, manager::kRemoveCityPropsInArea, cells);
    mine.Add(removed);
    AddRemoved(mine, self, Kind::Burnt);
    mine.Add(BurntPlain(self));
    log::Write("autotest: propkinds: edit burnt (%s): added %u %08X, plain %08X, shown before %d, in the renderer %u %u %u %u, "
               "%08X; removed %u, initialized %u, in manager %u, quad %d",
               how, added, afterAdd, BurntPlain(self), wasShown ? 1 : 0, held[0], held[1], held[2], held[3], afterShown, removed,
               Virtual<uint8_t>(occupant, 0x14), At<uint8_t>(self, burnt::kFlags) & 1,
               At<void*>(self, burnt::kQuad) != nullptr ? 1 : 0);
    if (Virtual<uint8_t>(occupant, 0x14)) {
        log::Write("autotest: propkinds: edit burnt: STILL INITIALIZED after the removal; shut down by the test");
        Virtual<uint8_t>(occupant, 0x10);
    }
    Virtual<uint32_t>(self, 0x8);
    f.Add(mine.h);
}

bool SignsShown()
{
    return Virtual<uint8_t>(Global<void*>(addr::SC4_Service43DB8), 0x28) != 0;
}

void ShowSigns(bool shown)
{
    Virtual<void>(Global<void*>(addr::SC4_Service43DB8), 0x20, uint32_t{shown ? 1u : 0u});
}

// A label as the label tool puts one down: placed, added to the city (Init:
// the prop type, the overlay managers, shown when the city shows signs and
// labels), its text set. With signs and labels shown (switched on for the
// test when they were off, and back at the end) and the label shown as the
// view shows it: the setters with overlays, a drag (the moving byte, a
// move, colour and opacity while moving), the view's messages (the camera
// moved, the city's init, a new visibility filter: the one the city has),
// hidden and shown (the filter message's data 2 points at the filter). Then
// with signs and labels off, showing it is refused.
// Then it is removed.
void EditLabel(Fnv& f, void* m, const Types& types, const float* position, const int32_t* cells)
{
    const char* how = "";
    char* const self = MakeForEdit(m, types[Kind::Text], Kind::Text, &how);
    if (!self) {
        f.Add(uint8_t{0xEE});
        log::Write("autotest: propkinds: edit label: none made");
        return;
    }
    char* const occupant = self + layout::kOccupant;
    const bool signsBefore = SignsShown();
    Fnv mine;
    // Small: the box (the text's width, its texture's width over height
    // times the size times 16) stays inside the spot's empty 64 m occupant
    // cell, where the flammability simulator does not see it come and go.
    Virtual<void>(self, label::slot::kSetSize, 0.1f);
    Virtual<void>(occupant, 0x24, position);
    const uint8_t added = Virtual<uint8_t>(m, manager::kAddCityProp, static_cast<void*>(occupant));
    mine.Add(added);
    mine.Add(LabelPlain(self));
    mine.Add(Virtual<uint8_t>(occupant, 0x14));
    mine.Add(static_cast<uint8_t>(At<uint8_t>(self, layout::kFlags) & 1));
    mine.Add(At<uint16_t>(self, layout::kProp + 0x5C));
    const bool shownByInit = (At<uint8_t>(self, layout::kFlags) & 2) != 0;
    mine.Add(shownByInit);
    AddOverlayState(mine, self);
    const uint32_t afterAdd = mine.h;
    if (!signsBefore)
        ShowSigns(true);
    if (!(At<uint8_t>(self, layout::kFlags) & 2))
        Virtual<uint32_t>(occupant, 0x78, uint32_t{At<uint8_t>(self, layout::kFlags)} | 2u);
    const bool shown = (At<uint8_t>(self, layout::kFlags) & 2) != 0;
    mine.Add(shown);
    SetLabelText(self, kLabelAccent);
    const std::string text = TextOf(self);
    AddText(mine, text);
    mine.Add(LabelPlain(self));
    AddOverlayState(mine, self);
    const uint32_t width = At<uint32_t>(self, label::kWidth);
    const bool overlays =
        At<uint32_t>(self, label::kOverlayId) != 0xFFFFFFFF && At<uint32_t>(self, label::kOverlayId2) != 0xFFFFFFFF;
    // The setters with overlays there: each lays the text down again.
    Virtual<void>(self, label::slot::kSetSize, 0.15f);
    AddOverlayState(mine, self);
    Virtual<void>(self, label::slot::kSetColour, 200.0f, 100.0f, 50.0f);
    Virtual<void>(self, label::slot::kSetOpacity, 0.7f);
    Virtual<void>(self, label::slot::kSetDirection, int32_t{0});
    AddOverlayState(mine, self);
    Virtual<void>(self, label::slot::kSetDirection, int32_t{1});
    mine.Add(LabelPlain(self));
    AddOverlayState(mine, self);
    const uint32_t afterSetters = mine.h;
    // A drag: managers 2 and 3, the overlays moved and told.
    Virtual<void>(self, label::slot::kSetMoving, 1u);
    AddOverlayState(mine, self);
    const float moved[3] = {position[0] + 2.0f, position[1], position[2] + 1.0f};
    Virtual<void>(occupant, 0x24, static_cast<const float*>(moved));
    Virtual<void>(self, label::slot::kSetColour, 255.0f, 255.0f, 0.0f);
    Virtual<void>(self, label::slot::kSetOpacity, 0.5f);
    mine.Add(LabelPlain(self));
    AddOverlayState(mine, self);
    Virtual<void>(self, label::slot::kSetMoving, 0u);
    Virtual<void>(occupant, 0x24, position);
    mine.Add(LabelPlain(self));
    AddOverlayState(mine, self);
    const uint32_t afterDrag = mine.h;
    // The view's messages.
    Deliver(self + layout::kProp, 0xA6B79602, nullptr, 0, 0);
    AddOverlayState(mine, self);
    Deliver(self + layout::kProp, 0x26D31EC1, nullptr, 0, 0);
    AddOverlayState(mine, self);
    mine.Add(static_cast<uint8_t>(At<uint8_t>(self, layout::kFlags) & 2));
    void* filter = nullptr;
    Virtual<void>(Global<void*>(addr::SC4_Service43DC4), 0x48, &filter);
    if (filter) {
        Deliver(self + layout::kProp, 0x06CDB65B, nullptr, static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(&filter)), 0);
        mine.Add(static_cast<uint8_t>(At<uint8_t>(self, layout::kFlags) & 2));
        Virtual<uint32_t>(filter, 0x8);
    }
    // Hidden and shown again.
    Virtual<uint32_t>(occupant, 0x78, uint32_t{At<uint8_t>(self, layout::kFlags)} & ~2u);
    AddOverlayState(mine, self);
    Virtual<uint32_t>(occupant, 0x78, uint32_t{At<uint8_t>(self, layout::kFlags)} | 2u);
    AddOverlayState(mine, self);
    const uint32_t afterMessages = mine.h;
    // Signs and labels off: hidden, and showing it is refused.
    ShowSigns(false);
    if (At<uint8_t>(self, layout::kFlags) & 2)
        Virtual<uint32_t>(occupant, 0x78, uint32_t{At<uint8_t>(self, layout::kFlags)} & ~2u);
    Virtual<uint32_t>(occupant, 0x78, uint32_t{At<uint8_t>(self, layout::kFlags)} | 2u);
    const bool refused = !(At<uint8_t>(self, layout::kFlags) & 2);
    mine.Add(refused);
    AddOverlayState(mine, self);
    ShowSigns(signsBefore);
    const uint8_t removed = Virtual<uint8_t>(m, manager::kRemoveCityPropsInArea, cells);
    mine.Add(removed);
    AddRemoved(mine, self, Kind::Text);
    AddOverlayState(mine, self);
    mine.Add(LabelPlain(self));
    log::Write("autotest: propkinds: edit label (%s): added %u %08X (shown by Init %d, signs shown before %d), shown %d, text %s, "
               "width %08X, overlays %d, setters %08X, drag %08X, messages %08X, refused %d; removed %u, initialized %u, in "
               "manager %u, managers %d %d, texture %d",
               how, added, afterAdd, shownByInit ? 1 : 0, signsBefore ? 1 : 0, shown ? 1 : 0, Hex(text).c_str(), width,
               overlays ? 1 : 0, afterSetters, afterDrag, afterMessages, refused ? 1 : 0, removed, Virtual<uint8_t>(occupant, 0x14),
               At<uint8_t>(self, layout::kFlags) & 1, At<void*>(self, label::kOverlays) != nullptr ? 1 : 0,
               At<void*>(self, label::kOverlays2) != nullptr ? 1 : 0, At<void*>(self, label::kTexture) != nullptr ? 1 : 0);
    if (Virtual<uint8_t>(occupant, 0x14)) {
        // Not removed: its Shutdown gives up the messages and the overlays
        // before the reference goes.
        log::Write("autotest: propkinds: edit label: STILL INITIALIZED after the removal; shut down by the test");
        Virtual<uint8_t>(occupant, 0x10);
    }
    Virtual<uint32_t>(self, 0x8);
    f.Add(mine.h);
}

// A sign as the sign tool puts one down, but placed before it is added (so
// that it never stands at the origin): added to the city (Init: the boxes,
// the prop type, flag 0x10, the ten bindings, the messages, the geometry).
// With signs and labels shown (switched on for the test when they were off,
// and back at the end) and the sign shown as the view shows it (its model
// goes to the renderer): the text, the highlight, an edit (frames listened
// to, the caret driven by frame messages the test makes), a drag, the
// view's messages (the camera moved, a new visibility filter: the one the
// city has), hidden and shown. Then with signs and labels off, showing it is
// refused. It is hidden before it is removed, so the renderer holds nothing
// of it after.
void EditSign(Fnv& f, void* m, const Types& types, const float* position, const int32_t* cells)
{
    const char* how = "";
    char* const self = MakeForEdit(m, types[Kind::Sign], Kind::Sign, &how);
    if (!self) {
        f.Add(uint8_t{0xEE});
        log::Write("autotest: propkinds: edit sign: none made");
        return;
    }
    char* const occupant = self + layout::kOccupant;
    const bool signsBefore = SignsShown();
    void* const renderer = Global<void*>(addr::SC4_View43DD0);
    void* const model = self + sign::kVbptr + At<int32_t>(At<char*>(self, sign::kVbptr), 0x4);
    const auto held = [renderer, model]() -> uint32_t { return renderer ? Virtual<uint8_t>(renderer, 0x6C, model) : 0xEE; };
    Fnv mine;
    Virtual<void>(occupant, 0x24, position);
    const uint8_t added = Virtual<uint8_t>(m, manager::kAddCityProp, static_cast<void*>(occupant));
    mine.Add(added);
    mine.Add(SignPlain(self));
    mine.Add(Virtual<uint8_t>(occupant, 0x14));
    mine.Add(static_cast<uint8_t>(At<uint8_t>(self, layout::kFlags) & 0x11));
    mine.Add(Virtual<uint32_t>(self + layout::kProp, 0x1C));
    const bool shownByInit = (At<uint8_t>(self, layout::kFlags) & 2) != 0;
    mine.Add(shownByInit);
    AddSignState(mine, self);
    const uint32_t afterAdd = mine.h;
    if (!signsBefore)
        ShowSigns(true);
    if (!(At<uint8_t>(self, layout::kFlags) & 2))
        Virtual<uint32_t>(occupant, 0x78, uint32_t{At<uint8_t>(self, layout::kFlags)} | 2u);
    const bool shown = (At<uint8_t>(self, layout::kFlags) & 2) != 0;
    uint32_t inRenderer[4] = {held(), 0, 0, 0};
    mine.Add(shown);
    mine.Add(inRenderer[0]);
    // The text, as the sign tool gives it.
    SetSignText(self, kSignAccent);
    const std::string text = SignTextOf(self);
    AddText(mine, text);
    mine.Add(SignPlain(self));
    AddSignState(mine, self);
    AddSignGeometry(mine, self, false);
    mine.Add(SignModel(self));
    log::Write("autotest: propkinds: edit sign: at %.2f %.2f %.2f, box %.2f %.2f %.2f to %.2f %.2f %.2f", position[0], position[1],
               position[2], At<float>(self, layout::kMin), At<float>(self, layout::kMin + 4), At<float>(self, layout::kMin + 8),
               At<float>(self, layout::kMax), At<float>(self, layout::kMax + 4), At<float>(self, layout::kMax + 8));
    // Highlighted and not; edited, with two frames (the caret flips), then
    // the edit ended by the highlight going.
    Virtual<void>(self, sign::slot::kSetHighlight, 1u);
    AddHighlight(mine, self);
    Virtual<void>(self, sign::slot::kSetHighlight, 0u);
    AddHighlight(mine, self);
    Virtual<void>(self, sign::slot::kSetEditing, 1u);
    Frame(self, 250000u);
    Frame(self, 260000u);
    const uint32_t caret = At<uint8_t>(self, sign::kCaret), caretTime = At<uint32_t>(self, sign::kCaretTime);
    mine.Add(caret);
    mine.Add(caretTime);
    mine.Add(At<uint32_t>(self, sign::kModelFlags));
    Virtual<void>(self, sign::slot::kSetHighlight, 0u);
    const uint8_t editedAfter = At<uint8_t>(self, sign::kEditing);
    mine.Add(editedAfter);
    mine.Add(SignPlain(self));
    const uint32_t afterEdit = mine.h;
    // A drag: moved and back, shown all the while.
    const float moved[3] = {position[0] + 2.0f, position[1], position[2] + 1.0f};
    Virtual<void>(occupant, 0x24, static_cast<const float*>(moved));
    mine.Add(SignPlain(self));
    AddSignState(mine, self);
    Virtual<void>(occupant, 0x24, position);
    mine.Add(SignPlain(self));
    const uint32_t afterDrag = mine.h;
    // The view's messages.
    Deliver(self + layout::kProp, 0xA6B79602, nullptr, 0, 0);
    mine.Add(SignPlain(self));
    void* filter = nullptr;
    Virtual<void>(Global<void*>(addr::SC4_Service43DC4), 0x48, &filter);
    if (filter) {
        Deliver(self + layout::kProp, 0x06CDB65B, nullptr, static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(&filter)), 0);
        mine.Add(static_cast<uint8_t>(At<uint8_t>(self, layout::kFlags) & 2));
        Virtual<uint32_t>(filter, 0x8);
    }
    // Hidden and shown again: the model taken off the renderer and added.
    Virtual<uint32_t>(occupant, 0x78, uint32_t{At<uint8_t>(self, layout::kFlags)} & ~2u);
    inRenderer[1] = held();
    Virtual<uint32_t>(occupant, 0x78, uint32_t{At<uint8_t>(self, layout::kFlags)} | 2u);
    inRenderer[2] = held();
    mine.Add(inRenderer[1]);
    mine.Add(inRenderer[2]);
    const uint32_t afterMessages = mine.h;
    // Signs and labels off: hidden, and showing it is refused.
    ShowSigns(false);
    if (At<uint8_t>(self, layout::kFlags) & 2)
        Virtual<uint32_t>(occupant, 0x78, uint32_t{At<uint8_t>(self, layout::kFlags)} & ~2u);
    Virtual<uint32_t>(occupant, 0x78, uint32_t{At<uint8_t>(self, layout::kFlags)} | 2u);
    const bool refused = !(At<uint8_t>(self, layout::kFlags) & 2);
    mine.Add(refused);
    ShowSigns(signsBefore);
    if (At<uint8_t>(self, layout::kFlags) & 2)
        Virtual<uint32_t>(occupant, 0x78, uint32_t{At<uint8_t>(self, layout::kFlags)} & ~2u);
    inRenderer[3] = held();
    mine.Add(inRenderer[3]);
    const uint8_t removed = Virtual<uint8_t>(m, manager::kRemoveCityPropsInArea, cells);
    mine.Add(removed);
    AddRemoved(mine, self, Kind::Sign);
    mine.Add(SignPlain(self));
    mine.Add(SignBindings(self));
    mine.Add(SignGlyphs(self));
    log::Write("autotest: propkinds: edit sign (%s): added %u %08X (shown by Init %d, signs shown before %d), shown %d, text %s, "
               "caret %u/%u, edited after %u, edit %08X, drag %08X, messages %08X, in the renderer %u %u %u %u, refused %d; "
               "removed %u, initialized %u, in manager %u, bindings %u, glyphs %u",
               how, added, afterAdd, shownByInit ? 1 : 0, signsBefore ? 1 : 0, shown ? 1 : 0, Hex(text).c_str(), caret, caretTime,
               editedAfter, afterEdit, afterDrag, afterMessages, inRenderer[0], inRenderer[1], inRenderer[2], inRenderer[3],
               refused ? 1 : 0, removed, Virtual<uint8_t>(occupant, 0x14), At<uint8_t>(self, layout::kFlags) & 1,
               SignBindings(self), SignGlyphs(self));
    if (Virtual<uint8_t>(occupant, 0x14)) {
        // Not removed: its Shutdown gives up the messages and the bindings
        // before the reference goes.
        log::Write("autotest: propkinds: edit sign: STILL INITIALIZED after the removal; shut down by the test");
        Virtual<uint8_t>(occupant, 0x10);
    }
    Virtual<uint32_t>(self, 0x8);
    f.Add(mine.h);
}

// OPENSC4_TEST_PROPKINDS_EDIT=2: after the round trip a decal (no fade), an
// effect prop, a controller prop, a burnt building, two labels and a sign
// are added at the spot and left there, so that the run's save holds them
// and a run started from that save reads them back.
void LeaveInCity(Fnv& f, void* m, const Types& types, const float* position, const std::string& known)
{
    // Two labels: one with U+00E9 at the spot, one in ASCII 4 m off.
    Kind kinds[kKindCount + 1];
    std::copy(std::begin(kKinds), std::end(kKinds), kinds);
    kinds[kKindCount] = Kind::Text;
    for (int i = 0; i <= kKindCount; ++i) {
        const Kind kind = kinds[i];
        const bool second = i == kKindCount;
        const char* how = "";
        char* const self = MakeForEdit(m, types[kind], kind, &how);
        if (!self) {
            f.Add(uint8_t{0xEE});
            continue;
        }
        char* const occupant = self + OccupantPartOf(kind);
        Fnv mine;
        const float off[3] = {position[0] + 4.0f, position[1], position[2] + 4.0f};
        if (kind == Kind::Decal) {
            mine.Add(Virtual<uint8_t>(occupant, 0x0C));
            Virtual<void>(self, decal::slot::kSetDecalTexture, static_cast<const uint32_t*>(kTexture), 12.0f);
            Virtual<void>(self, decal::slot::kSetOpacity, 0.75f);
        }
        if (kind == Kind::Burnt) {
            PlaceBurnt(self, position);
        } else {
            Virtual<void>(occupant, 0x24, second ? off : position);
            Virtual<uint32_t>(self + layout::kProp, 0x28, int32_t{0});
        }
        const uint8_t added = Virtual<uint8_t>(m, manager::kAddCityProp, static_cast<void*>(occupant));
        mine.Add(added);
        if (kind == Kind::Controller && NameOfController(self).empty() && !known.empty())
            Virtual<void>(self, automata::slot::kSetTemplateName, known.c_str());
        if (kind == Kind::Text) {
            // As the label tool does it: added, then the text; then a colour
            // and a size of its own.
            SetLabelText(self, second ? kLabelText : kLabelAccent);
            Virtual<void>(self, label::slot::kSetColour, 255.0f, 230.0f, 128.0f);
            Virtual<void>(self, label::slot::kSetSize, second ? 0.64f : 0.8f);
            log::Write("autotest: propkinds: left in the city: label text %s", Hex(TextOf(self)).c_str());
        }
        if (kind == Kind::Sign) {
            // As the sign tool does it: added, then the text.
            SetSignText(self, kSignAccent);
            log::Write("autotest: propkinds: left in the city: sign text %s", Hex(SignTextOf(self)).c_str());
        }
        const uint32_t plain = PlainOf(kind, self);
        mine.Add(plain);
        log::Write("autotest: propkinds: left in the city: %s (%s): added %u, plain %08X, initialized %u, in manager %u",
                   NameOf(kind), how, added, plain, Virtual<uint8_t>(occupant, 0x14), At<uint8_t>(self, FlagsOf(kind)) & 1);
        Virtual<uint32_t>(self, 0x8);
        f.Add(mine.h);
    }
}

// --- The switch --------------------------------------------------------------------------

void LogCity(const char* when, const CityCount& c)
{
    log::Write("autotest: propkinds: city %s: decals %u (%08X %08X; overlay %u, fading %u) effects %u (%08X %08X; "
               "effect %u, started %u) controllers %u (%08X %08X; controller %u) burnt %u (%08X %08X) labels %u (%08X %08X; "
               "texture %u, overlays %u) signs %u (%08X %08X; edited %u, highlighted %u, with glyphs %u, bound %u) shown %u, "
               "wrong QueryInterface %u",
               when, c.count[0], c.sum[0], c.xorred[0], c.withOverlay, c.fading, c.count[1], c.sum[1], c.xorred[1], c.withEffect,
               c.started, c.count[2], c.sum[2], c.xorred[2], c.withController, c.count[3], c.sum[3], c.xorred[3], c.count[4],
               c.sum[4], c.xorred[4], c.withTexture, c.labelOverlays, c.count[5], c.sum[5], c.xorred[5], c.signsEdited,
               c.signsHighlighted, c.signsWithGlyphs, c.signsBound, c.shown, c.wrongQuery);
    for (std::size_t i = 0; i < c.texts.size() && i < 8; ++i)
        log::Write("autotest: propkinds: city %s: label %u text %s", when, static_cast<unsigned>(i), c.texts[i].c_str());
    for (std::size_t i = 0; i < c.signTexts.size() && i < 8; ++i)
        log::Write("autotest: propkinds: city %s: sign %u text %s", when, static_cast<unsigned>(i), c.signTexts[i].c_str());
}

void AddCounts(Fnv& f, const CityCount& c)
{
    for (int i = 0; i < kKindCount; ++i) {
        f.Add(c.count[i]);
        f.Add(c.sum[i]);
        f.Add(c.xorred[i]);
    }
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
    AddCounts(all, before);
    all.Add(before.wrongQuery);

    Types types;
    if (m)
        types = TypesOf(m);
    log::Write("autotest: propkinds: types: %u decal, %u effect, %u controller, %u burnt, %u label, %u sign",
               static_cast<unsigned>(types[Kind::Decal].size()), static_cast<unsigned>(types[Kind::Effect].size()),
               static_cast<unsigned>(types[Kind::Controller].size()), static_cast<unsigned>(types[Kind::Burnt].size()),
               static_cast<unsigned>(types[Kind::Text].size()), static_cast<unsigned>(types[Kind::Sign].size()));
    Fnv made;
    DriveMade(made, m, types, before.building);
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
            const std::string known = SomeTemplate(city);
            log::Write("autotest: propkinds: a template the scripts have: \"%s\"", known.c_str());
            EditController(edit, m, types, position, cells, known);
            EditBurnt(edit, m, types, position, cells);
            EditLabel(edit, m, types, position, cells);
            int32_t signX = -1, signZ = -1;
            if (FindSignSpot(m, sizeX, sizeZ, &signX, &signZ)) {
                float signCorner[2] = {0, 0};
                Virtual<void>(city, 0x180, signX, signZ, &signCorner[0], &signCorner[1]);
                const float signAt[3] = {signCorner[0] + 8.0f, terrain ? Virtual<float>(terrain, 0x38, signX, signZ) : 0.0f,
                                         signCorner[1] + 8.0f};
                const int32_t signCells[4] = {signX, signZ, signX, signZ};
                log::Write("autotest: propkinds: edit sign at cell %d,%d", signX, signZ);
                EditSign(edit, m, types, signAt, signCells);
            } else {
                edit.Add(uint8_t{0xED});
                log::Write("autotest: propkinds: edit sign skipped: no empty spot");
            }
            if (editLevel >= 2) {
                const CityCount between = WalkCity(city);
                LogCity("after the round trip", between);
                edit.Add(between == before);
                LeaveInCity(edit, m, types, position, known);
            }
        } else {
            edit.Add(uint8_t{0xEE});
            log::Write("autotest: propkinds: edit skipped: no empty spot");
        }
        const CityCount after = WalkCity(city);
        LogCity("after", after);
        if (editLevel >= 2) {
            AddCounts(edit, after);
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
