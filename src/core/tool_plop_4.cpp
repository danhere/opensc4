// The plop tool's preview drawing (T11): the last game code of
// cSC4ViewInputControlPlaceLot. Replacement, off by default:
//
//   ui.tools.plop4: the preview update (0x004C2660), its footprint builder
//     (0x004C2090) and the preview builder (0x004C0680).
//
// With ui.tools.plop1 to plop3 every function of the class is ours but the
// shared helpers listed in docs/tools.md ("Left the game's" under the lot
// tool), among them the three adjustor thunks of the message target, which
// three other classes' vtables share.
//
// BuildPreview runs when the tool gets a lot configuration (Init,
// SetConfiguration, PlaceLot). It makes the preview prop of the
// configuration's building, asks the police, fire, health and education
// services for the radius the cursor shows and the cursor effect names,
// sets the cursor ID and the count of placements, adds a record for each
// prop of the configuration that is drawn with the preview, and paints the
// footprint picture (a white frame on a buffer, made a texture binding at
// +0x100).
//
// UpdatePreview runs on every move. With the preview hidden it hides the
// cursor objects and empties the footprint vectors. Shown, it places the
// overlay holder, the cursor effect, the footprint model (turned by the
// rotation, scaled by the picture's size) and the direction arrow (moved to
// the front of the lot), moves the preview and builds the footprint
// rectangles (UpdateFootprint: a blue rectangle for each record of kind 5
// and a red one for kind 6). A change of the allowed byte changes the
// cursor.
//
// Floats: the game runs the x87 at 53 bit (docs/pollution.md, "Floating
// point") and keeps its chains of operations in registers, so ours computes
// them in double and rounds to float only where the game stores a float.
// The turns go through FCOS and FSIN, whose results are 64 bit; those two
// instructions are run as the game runs them (X87CosSin) and every value
// after them is a single operation on stored floats.
//
// Every function was written from the disassembly. Calls into the game stay
// calls by address or by vtable slot; the three functions and the tool's
// other parts call each other by address, so each reaches ours only through
// its own hook.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::tool_plop_4 {
namespace {

namespace field {
constexpr std::size_t kView = 0x1C;           // the city view
constexpr std::size_t kConfig = 0x30;         // the lot configuration ID
constexpr std::size_t kRotation = 0x3C;       // the rotation in use
constexpr std::size_t kConfigManager = 0x4C;  // the lot configuration manager
constexpr std::size_t kDevelopment = 0x68;    // the building development simulator
constexpr std::size_t kPoint = 0x88;          // the picked point
constexpr std::size_t kAllowed = 0x94;        // byte
constexpr std::size_t kHolder = 0xAC;         // the overlay holder
constexpr std::size_t kInactiveName = 0xB0;   // a game string
constexpr std::size_t kPlopName = 0xC4;       // a game string
constexpr std::size_t kEffect = 0xD8;         // the cursor effect
constexpr std::size_t kFootprint = 0xDC;      // the footprint model
constexpr std::size_t kArrow = 0xE0;          // the direction arrow
constexpr std::size_t kPreview = 0xE4;        // the preview prop's holder
constexpr std::size_t kBlue = 0xE8;           // vector of rectangle models
constexpr std::size_t kRed = 0xF4;            // vector of rectangle models
constexpr std::size_t kTexture = 0x100;       // the footprint picture's binding
constexpr std::size_t kPictureSize = 0x104;   // float
constexpr std::size_t kTurnOffset = 0x108;    // byte
constexpr std::size_t kOffsetA = 0x10C;       // three floats
constexpr std::size_t kRadiusOut = 0x11C;
constexpr std::size_t kRadius = 0x120;        // float
constexpr std::size_t kServices = 0x124;      // flags: police 1, fire 2, health 4, education 8
constexpr std::size_t kCursor = 0x140;
constexpr std::size_t kRecords = 0x144;       // vector of 0x14-byte records
constexpr std::size_t kPlacementsLeft = 0x154;
} // namespace field

constexpr std::size_t kRecordSize = 0x34; // a lot configuration's record
constexpr uint8_t kRecordProp = 1;
constexpr uint8_t kRecordBlue = 5;
constexpr uint8_t kRecordRed = 6;

constexpr uint32_t kCursorAllowed = 0x88E70B76;
constexpr uint32_t kCursorStep = 0xB897D7F5; // added for the refused cursor
constexpr uint32_t kPropertyA = 0x029244DB;
constexpr uint32_t kPropertyB = 0x29F013E6;
constexpr uint32_t kPropertyDrawn = 0x4A89FCF3;
constexpr uint32_t kPropertyHealth = 0x091B42A2;
constexpr uint32_t kPropertyEducation = 0x691B42B3;
constexpr uint32_t kPropertyOnce = 0xEA2E078B;
constexpr uint32_t kPropertyOnceFlag = 0xEA3209F8;
constexpr uint32_t kPropertyCount = 0x0BD86FD3;
constexpr uint32_t kOccupantFlags = 0x10;
constexpr uint32_t kTextureFactoryClass = 0x1AC0E11A;
constexpr uint32_t kTextureFactoryInterface = 0xFAC0E219;
constexpr uint32_t kLockPaint = 0x8001;
constexpr uint32_t kLockData = 0x8040;
constexpr uint32_t kFrameOuter = 0xFFFFFFFF;
constexpr uint32_t kFrameInner = 0xAAFFFFFF;

constexpr float kHalfCell = 8.0f;
constexpr float kHalf = 0.5f;
constexpr float kQuarterTurn = 1.5707950592041016f; // 0x00A90918
constexpr float kScaleLess = 0.99999f;              // 0x00A9091C
constexpr float kNought = 0.0f;                     // 0x00A81054
constexpr float kArrowBack = 6.0f;                  // 0x00A90900
constexpr float kHalfShare = 0.5f;
constexpr float kOtherShare = 0.7f;
constexpr uint32_t kDefaultRadius = 0x42000000; // 32.0f
constexpr uint32_t kTintAlpha = 0x3ECCCCCD;     // 0.4f

// -1.570795 as the double at 0x00A90918's neighbour 0x00A908F8.
double BackQuarterTurn()
{
    const uint64_t bits = 0xBFF921F9F01B866Eull;
    double value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

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

void Release(void* object)
{
    Virtual<uint32_t>(object, 0x8);
}

// The holder at `slot` let go of and cleared, as the game's inline
// reference holder does.
void Clear(void** slot)
{
    if (void* const old = *slot) {
        *slot = nullptr;
        Release(old);
    }
}

uint32_t Bits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

// __ftol2 (0x009EEF04): truncation toward zero to 64 bits; the x87 "integer
// indefinite" for what does not fit and for a NaN. The callers take the low
// dword.
uint32_t Ftol(double value)
{
    if (!(value > -9.2233720368547758e18 && value < 9.2233720368547758e18))
        return 0;
    return static_cast<uint32_t>(static_cast<uint64_t>(static_cast<int64_t>(value)));
}

// A dword loaded with FILD and, when negative, 2^32 added: the unsigned
// value, exact in double.
double Unsigned(uint32_t value)
{
    return static_cast<double>(value);
}

// FLD qword, FLD ST(0), FCOS, FSTP float, FSIN, FSTP float: the game's
// FCOS and FSIN on its angle (the angle is the 53-bit product the game
// keeps in the register, exact in a double).
__declspec(naked) void __cdecl X87CosSin(double /*angle*/, float* /*cosine*/, float* /*sine*/)
{
    __asm fld qword ptr [esp + 4]
    __asm fld st(0)
    __asm fcos
    __asm mov eax, [esp + 12]
    __asm fstp dword ptr [eax]
    __asm fsin
    __asm mov eax, [esp + 16]
    __asm fstp dword ptr [eax]
    __asm ret
}

// What a model's slot 8 takes (as tool_zone_2.cpp has it): a count and
// flags, a 3x3 rotation, the place and the scale. The two bytes after the
// flags are what the game's stack held; ours are zero.
struct Transform {
    uint8_t count;
    uint8_t flags;
    uint8_t unused[2];
    float rotation[9];
    float place[3];
    float scale;
};
static_assert(sizeof(Transform) == 0x38);

constexpr uint8_t kPlaced = 0x1;
constexpr uint8_t kScaled = 0x2;
constexpr uint8_t kTurned = 0x4;

// The rotation block both turns build: the nine floats start as the game's
// unset float (0x00B2183C, copied as bits), the middle row and column are
// set to 0 and 1, and the rest comes from the cosine and sine as the game's
// register juggling leaves them (the factor is 0.0, so each product is a
// signed zero; every value stored is one operation on stored floats or on
// that product). The cosine and sine stored as floats are handed back: the
// game leaves them in stack slots that others read later.
void Turn(Transform& t, double angle, float& c, float& s)
{
    for (float& value : t.rotation)
        std::memcpy(&value, reinterpret_cast<const void*>(addr::SC4_UnsetFloat), sizeof value);
    t.rotation[1] = 0.0f;
    t.rotation[4] = 1.0f;
    t.rotation[7] = 0.0f;
    X87CosSin(angle, &c, &s);
    const double ks = static_cast<double>(kNought) * static_cast<double>(s); // kept in the register
    const float ksf = static_cast<float>(ks);
    const float ckf = static_cast<float>(static_cast<double>(c) * static_cast<double>(kNought));
    t.rotation[6] = static_cast<float>(static_cast<double>(ckf) - static_cast<double>(s));
    t.rotation[0] = static_cast<float>(static_cast<double>(c) - static_cast<double>(ksf));
    t.rotation[3] = static_cast<float>(static_cast<double>(ckf) - static_cast<double>(ksf));
    t.rotation[8] = static_cast<float>(ks + static_cast<double>(c));
    t.rotation[2] = static_cast<float>(static_cast<double>(s) + static_cast<double>(ckf));
    t.rotation[5] = static_cast<float>(static_cast<double>(ksf) + static_cast<double>(ckf));
}

// The identity rotation, set over the unset float as the game sets it.
void Identity(Transform& t)
{
    for (float& value : t.rotation)
        std::memcpy(&value, reinterpret_cast<const void*>(addr::SC4_UnsetFloat), sizeof value);
    t.rotation[0] = 1.0f;
    t.rotation[3] = 0.0f;
    t.rotation[6] = 0.0f;
    t.rotation[1] = 0.0f;
    t.rotation[4] = 1.0f;
    t.rotation[7] = 0.0f;
    t.rotation[2] = 0.0f;
    t.rotation[5] = 0.0f;
    t.rotation[8] = 1.0f;
}

// The block MovePreview hands the preview's parts (0x14 bytes): a mode, a
// tint, two bytes the game's stack held (ours: what the game's last turn
// left there when one ran, else zero), a colour and an alpha.
struct Tint {
    uint8_t mode;
    uint8_t tint;
    uint8_t stack[2];
    float colour[3];
    float alpha;
};
static_assert(sizeof(Tint) == 0x14);

void Colour(Tint& tint)
{
    std::memcpy(tint.colour, reinterpret_cast<const void*>(addr::SC4_ColorWhite), sizeof tint.colour);
}

// A game string made on the stack (0x14 bytes: vtable, begin, end,
// capacity, a count); the game frees its text inline after the copy.
struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t count;
};
static_assert(sizeof(GameString) == 0x14);

void SetName(char* self, std::size_t at, std::uintptr_t text)
{
    GameString name{};
    This<void*>(addr::SC4String_FromText, &name, reinterpret_cast<const char*>(text));
    This<void*>(addr::cRZBaseString_Assign, self + at, &name);
    if (name.begin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(name.begin));
}

// A hash set of uint32 on the stack (0x14 bytes: +4/+8 the buckets, +0x10
// the count).
struct HashSet {
    uint32_t first;
    void** begin;
    void** end;
    void** capacity;
    uint32_t count;
};

// --- UpdateFootprint -------------------------------------------------------------------

// One rectangle model of a footprint vector: the vector grown to hold
// `index` (each new entry asked from the model service by name), then the
// model gets the four corners and is shown.
void PlaceRectangle(char* self, std::size_t vector, uint32_t index, std::uintptr_t name, float* corners)
{
    void*** const at = &At<void**>(self, vector);
    while (index >= static_cast<uint32_t>(at[1] - at[0])) {
        void* zero = nullptr;
        const uint32_t size = static_cast<uint32_t>(at[1] - at[0]);
        const uint32_t wanted = size + 1;
        if (wanted < size)
            This<void*>(addr::SC4RefVector_Erase, at, at[0] + wanted, at[1]);
        else
            This<void>(addr::SC4RefVector_FillInsert, at, at[1], wanted - size, &zero);
        void** const last = at[1] - 1;
        void* const models = Global<void*>(addr::SC4_Service43D1C);
        Clear(last);
        Virtual<void>(models, 0x1C, reinterpret_cast<const char*>(name), last);
    }
    if (void* const model = at[0][index]) {
        Virtual<void>(model, 0x30, uint32_t{0xC}, corners, uint32_t{4});
        Virtual<void>(at[0][index], 0xC, uint32_t{0});
    }
}

// The corners of one record's rectangle: its two points (+0x18 and +0x10,
// each moved half a cell) to cells, the far one less one, turned in the lot
// and moved to the lot's rectangle, the far corner one cell on.
void Corners(char* self, void* city, const char* record, uint32_t width, uint32_t depth, const int32_t* lot,
             float* corners)
{
    const float a = *reinterpret_cast<const float*>(record + 0x10);
    const float b = *reinterpret_cast<const float*>(record + 0x14);
    int32_t cells[4];
    Virtual<void>(city, 0x17C,
                  static_cast<float>(static_cast<double>(*reinterpret_cast<const float*>(record + 0x18)) + kHalfCell),
                  static_cast<float>(static_cast<double>(*reinterpret_cast<const float*>(record + 0x1C)) + kHalfCell),
                  &cells[0], &cells[1]);
    Virtual<void>(city, 0x17C, static_cast<float>(static_cast<double>(a) + kHalfCell),
                  static_cast<float>(static_cast<double>(b) + kHalfCell), &cells[2], &cells[3]);
    --cells[2];
    --cells[3];
    int32_t turned[4];
    Cdecl<void>(addr::SC4_RotateRectangle, At<int32_t>(self, field::kRotation), width, depth,
                static_cast<const int32_t*>(cells), turned);
    corners[0] = static_cast<float>(static_cast<int32_t>(static_cast<uint32_t>(turned[0]) + static_cast<uint32_t>(lot[0])));
    corners[1] = static_cast<float>(static_cast<int32_t>(static_cast<uint32_t>(turned[1]) + static_cast<uint32_t>(lot[1])));
    corners[2] = static_cast<float>(
        static_cast<int32_t>(static_cast<uint32_t>(turned[2]) + static_cast<uint32_t>(lot[0]) + 1u));
    corners[3] = static_cast<float>(
        static_cast<int32_t>(static_cast<uint32_t>(turned[3]) + static_cast<uint32_t>(lot[1]) + 1u));
}

// The game's 0x004C2090 (float point[3]), ret 4.
void __fastcall UpdateFootprint(char* self, void*, const float* point)
{
    void* const configuration =
        Virtual<void*>(At<void*>(self, field::kConfigManager), 0x20, At<uint32_t>(self, field::kConfig));
    if (!configuration)
        return;
    char** const records = Virtual<char**>(configuration, 0x5C);
    float x;
    float z;
    std::memcpy(&x, &point[0], sizeof x);
    std::memcpy(&z, &point[2], sizeof z);
    uint32_t blue = 0;
    uint32_t red = 0;
    float snapped[2];
    int32_t lot[4];
    if (This<uint8_t>(addr::cSC4ViewInputControlPlaceLot_SnapFootprint, self, At<int32_t>(self, field::kRotation), x, z,
                      &snapped[0], &snapped[1], lot)) {
        // The size's first output shares the stack slot the point's x was
        // copied to; the game's configurations always write both.
        uint32_t width = Bits(x);
        uint32_t depth = 0;
        Virtual<void>(configuration, 0x1C, &width, &depth);
        void* const city = Global<void*>(addr::SC4_City);
        if (city)
            Virtual<uint32_t>(city, 0x4);
        for (uint32_t i = 0, offset = 0; i < static_cast<uint32_t>((records[1] - records[0]) / static_cast<int32_t>(kRecordSize));
             ++i, offset += kRecordSize) {
            const char* const record = records[0] + offset;
            float corners[4];
            if (static_cast<uint8_t>(record[0]) == kRecordBlue) {
                Corners(self, city, record, width, depth, lot, corners);
                PlaceRectangle(self, field::kBlue, blue, addr::cSC4ViewInputControlPlaceLot_SelectionBlue, corners);
                ++blue;
            } else if (static_cast<uint8_t>(record[0]) == kRecordRed) {
                Corners(self, city, record, width, depth, lot, corners);
                PlaceRectangle(self, field::kRed, red, addr::cSC4ViewInputControlPlaceLot_SelectionRed, corners);
                ++red;
            }
        }
        if (city)
            Release(city);
    }
    for (uint32_t i = blue; i < static_cast<uint32_t>(At<void**>(self, field::kBlue + 4) - At<void**>(self, field::kBlue));
         ++i)
        if (void* const model = At<void**>(self, field::kBlue)[i])
            Virtual<void>(model, 0x10, uint32_t{0});
    for (uint32_t i = red; i < static_cast<uint32_t>(At<void**>(self, field::kRed + 4) - At<void**>(self, field::kRed));
         ++i)
        if (void* const model = At<void**>(self, field::kRed)[i])
            Virtual<void>(model, 0x10, uint32_t{0});
}

// --- UpdatePreview ---------------------------------------------------------------------

void HideIfShown(void* model)
{
    if (model && Virtual<uint8_t>(model, 0x14))
        Virtual<void>(model, 0x10, uint32_t{0});
}

void ShowIfHidden(void* model)
{
    if (!Virtual<uint8_t>(model, 0x14))
        Virtual<void>(model, 0xC, uint32_t{0});
}

// The model at `slot` hidden, let go of and made again by name.
void Remake(char* self, std::size_t slot, std::uintptr_t name)
{
    void** const holder = &At<void*>(self, slot);
    if (*holder) {
        HideIfShown(*holder);
        Clear(holder);
    }
    void* const models = Global<void*>(addr::SC4_Service43D1C);
    Clear(holder);
    Virtual<void>(models, 0x1C, reinterpret_cast<const char*>(name), holder);
}

// The preview hidden: the holder, the three cursor objects, the preview
// (MovePreview with the flag clear), and the two footprint vectors hidden
// and emptied.
void HidePreview(char* self, uint32_t allowedWord, const float* point)
{
    This<void>(addr::SC4ViewModel_Show, self + field::kHolder, uint32_t{0});
    HideIfShown(At<void*>(self, field::kEffect));
    HideIfShown(At<void*>(self, field::kFootprint));
    HideIfShown(At<void*>(self, field::kArrow));
    if (At<void*>(self, field::kPreview)) {
        Tint tint{};
        Colour(tint);
        tint.mode = 2;
        tint.tint = 0;
        tint.alpha = 0.0f;
        This<void>(addr::cSC4ViewInputControlPlaceLot_MovePreview, self, point, &tint, uint32_t{0});
    }
    void*** const blue = &At<void**>(self, field::kBlue);
    for (uint32_t i = 0; i < static_cast<uint32_t>(blue[1] - blue[0]); ++i)
        if (void* const model = blue[0][i])
            Virtual<void>(model, 0x10, uint32_t{0});
    This<void*>(addr::SC4RefVector_Erase, blue, blue[0], blue[1]);
    void*** const red = &At<void**>(self, field::kRed);
    for (uint32_t i = 0; i < static_cast<uint32_t>(red[1] - red[0]); ++i)
        if (void* const model = red[0][i])
            Virtual<void>(model, 0x10, uint32_t{0});
    void** const kept = Cdecl<void**>(addr::SC4PtrVector_CopyForward, red[1], red[1], red[0], &allowedWord, uint32_t{0});
    void** const end = red[1];
    for (void** p = kept; p != end; ++p)
        if (*p)
            Release(*p);
    red[1] = kept;
}

// The game's 0x004C2660 (allowed byte, shown byte, float point[3]), ret 0xC.
// The two bytes come as the dwords the caller pushed.
void __fastcall UpdatePreview(char* self, void*, uint32_t allowedWord, uint32_t shownWord, const float* point)
{
    const uint8_t allowed = static_cast<uint8_t>(allowedWord);
    if (!static_cast<uint8_t>(shownWord)) {
        HidePreview(self, allowedWord, point);
    } else {
        void* const terrain = Global<void*>(addr::SC4_Terrain);
        void* const surface = Virtual<void*>(terrain, 0x108);
        This<void>(addr::SC4ViewModel_Place, self + field::kHolder, point);
        const uint8_t covered = Virtual<uint8_t>(surface, 0x50);
        This<void>(addr::SC4ViewModel_Show, self + field::kHolder, uint32_t{covered ? 0u : 1u});
        // The stack slot of the shown argument: the size query of the arrow
        // reads it back when the configuration leaves its output; the game's
        // configurations always write it.
        uint32_t shownSlot = shownWord;
        // The dword the tint block's two stack bytes come from.
        uint32_t tintSlot = 0;

        if (void* const effect = At<void*>(self, field::kEffect)) {
            const float radius = At<float>(self, field::kRadius);
            if (static_cast<double>(radius) > 0.0) {
                Transform t{};
                Identity(t);
                const double scale = static_cast<double>(radius) + static_cast<double>(radius);
                t.scale = static_cast<float>(scale);
                t.flags = static_cast<uint8_t>((scale != 1.0 ? kScaled : 0) | kPlaced);
                std::memcpy(t.place, point, sizeof t.place);
                t.count = 2;
                Virtual<void>(effect, 0x20, &t);
                ShowIfHidden(At<void*>(self, field::kEffect));
            }
        }

        if (allowed != At<uint8_t>(self, field::kAllowed) || !At<void*>(self, field::kFootprint) ||
            !Virtual<uint8_t>(At<void*>(self, field::kFootprint), 0x14)) {
            const std::uintptr_t name =
                allowed ? addr::SC4CheatTool_FootprintOk : addr::SC4CheatTool_FootprintNotOk;
            shownSlot = static_cast<uint32_t>(name);
            Remake(self, field::kFootprint, name);
            if (void* const footprint = At<void*>(self, field::kFootprint)) {
                if (At<void*>(self, field::kTexture))
                    Virtual<void>(footprint, 0x34, uint32_t{6}, self + field::kTexture, uint32_t{1});
            }
        }
        if (At<void*>(self, field::kFootprint)) {
            void* const configuration = Virtual<void*>(At<void*>(self, field::kConfigManager), 0x20,
                                                       At<uint32_t>(self, field::kConfig));
            if (configuration) {
                uint32_t size[2] = {0, 0};
                Virtual<void>(configuration, 0x1C, &size[0], &size[1]);
                Transform t{};
                const double scale =
                    static_cast<double>(At<float>(self, field::kPictureSize)) * static_cast<double>(kScaleLess);
                t.scale = static_cast<float>(scale);
                t.flags = static_cast<uint8_t>((scale != 1.0 ? kScaled : 0) | kTurned | kPlaced);
                t.count = 3;
                float c;
                float s;
                Turn(t, static_cast<double>(At<int32_t>(self, field::kRotation)) * static_cast<double>(kQuarterTurn), c,
                     s);
                std::memcpy(t.place, point, sizeof t.place);
                // The cosine went to the shown argument's slot; the sine is
                // what the tint block's first dword holds after this turn.
                shownSlot = Bits(c);
                tintSlot = Bits(s);
                Virtual<void>(At<void*>(self, field::kFootprint), 0x20, &t);
                ShowIfHidden(At<void*>(self, field::kFootprint));
            }
        }

        bool arrow = allowed == At<uint8_t>(self, field::kAllowed) && At<void*>(self, field::kArrow);
        if (!arrow) {
            Remake(self, field::kArrow, addr::cSC4ViewInputControlPlaceLot_DirectionArrow);
            arrow = At<void*>(self, field::kArrow) != nullptr;
        }
        if (arrow) {
            void* const configuration = Virtual<void*>(At<void*>(self, field::kConfigManager), 0x20,
                                                       At<uint32_t>(self, field::kConfig));
            if (configuration) {
                float place[3];
                std::memcpy(place, point, sizeof place);
                uint32_t first = 0;
                Virtual<void>(configuration, 0x1C, &first, &shownSlot);
                const double cell = Virtual<double>(terrain, 0x18);
                const double back = cell * Unsigned(shownSlot) * static_cast<double>(kHalf) -
                                    static_cast<double>(kArrowBack);
                const double step = Unsigned(Ftol(back));
                switch (At<uint32_t>(self, field::kRotation)) {
                case 0:
                    place[2] = static_cast<float>(static_cast<double>(place[2]) - step);
                    break;
                case 1:
                    place[0] = static_cast<float>(step + static_cast<double>(place[0]));
                    break;
                case 2:
                    place[2] = static_cast<float>(step + static_cast<double>(place[2]));
                    break;
                case 3:
                    place[0] = static_cast<float>(static_cast<double>(place[0]) - step);
                    break;
                default:
                    break;
                }
                Transform t{};
                t.scale = 1.0f;
                std::memcpy(&t.place[0], &place[0], sizeof(float));
                std::memcpy(&t.place[1], &place[1], sizeof(float));
                std::memcpy(&t.place[2], &place[2], sizeof(float));
                t.flags = kTurned | kPlaced;
                t.count = 2;
                float c;
                float s;
                Turn(t, static_cast<double>(At<int32_t>(self, field::kRotation)) * BackQuarterTurn(), c, s);
                tintSlot = Bits(static_cast<float>(static_cast<double>(c) * static_cast<double>(kNought)));
                Virtual<void>(At<void*>(self, field::kArrow), 0x20, &t);
                ShowIfHidden(At<void*>(self, field::kArrow));
            }
        }

        if (At<void*>(self, field::kPreview)) {
            Tint tint{};
            Colour(tint);
            tint.mode = 3;
            tint.tint = static_cast<uint8_t>((allowed == 0 ? 1 : 0) + 3);
            tint.stack[0] = static_cast<uint8_t>(tintSlot >> 16);
            tint.stack[1] = static_cast<uint8_t>(tintSlot >> 24);
            std::memcpy(&tint.alpha, &kTintAlpha, sizeof tint.alpha);
            This<void>(addr::cSC4ViewInputControlPlaceLot_MovePreview, self, point, &tint, uint32_t{1});
        }
        This<void>(addr::cSC4ViewInputControlPlaceLot_UpdateFootprint, self, point);
    }
    if (allowed != At<uint8_t>(self, field::kAllowed))
        Virtual<void>(self, 0x20, (allowed ? kCursorStep : 0u) + kCursorAllowed);
}

// --- BuildPreview ----------------------------------------------------------------------

// One service's part of the preview: the flag, the radius from the
// service's slot (32 when it has none) and the two cursor effect names.
void Service(char* self, void* service, std::size_t slot, uint32_t flag, float share, void* occupant,
             std::uintptr_t plop, std::uintptr_t inactive)
{
    At<uint32_t>(self, field::kServices) |= flag;
    float shareArgument = share;
    if (!Virtual<uint8_t>(service, slot, occupant, &shareArgument, self + field::kRadiusOut,
                          &At<float>(self, field::kRadius)))
        At<uint32_t>(self, field::kRadius) = kDefaultRadius;
    SetName(self, field::kPlopName, plop);
    SetName(self, field::kInactiveName, inactive);
}

// The preview prop of the configuration's first building type and what
// goes with it. False when it stops short (nothing more is done then).
void MakePreview(char* self, void* configuration, uint32_t width, uint32_t depth)
{
    HashSet types{};
    This<void>(addr::SC4Uint32HashSet_Construct, &types, uint32_t{100});
    uint32_t key[3] = {0, 0, 0};
    void* const props = Global<void*>(addr::SC4_Service43D10);
    void* const terrain = Global<void*>(addr::SC4_Terrain);
    if (const char* const info = Virtual<const char*>(configuration, 0x60)) {
        At<uint8_t>(self, field::kTurnOffset) = static_cast<uint8_t>(info[2]);
        std::memcpy(&At<float>(self, field::kOffsetA), info + 4, 3 * sizeof(float));
        double cell = Virtual<double>(terrain, 0x18);
        At<float>(self, field::kOffsetA) = static_cast<float>(
            static_cast<double>(At<float>(self, field::kOffsetA)) - cell * Unsigned(width) * static_cast<double>(kHalf));
        cell = Virtual<double>(terrain, 0x18);
        At<float>(self, field::kOffsetA + 8) =
            static_cast<float>(static_cast<double>(At<float>(self, field::kOffsetA + 8)) -
                               cell * Unsigned(depth) * static_cast<double>(kHalf));
        const uint32_t* end = *reinterpret_cast<const uint32_t* const*>(info + 0x2C);
        for (const uint32_t* id = *reinterpret_cast<const uint32_t* const*>(info + 0x28); id != end;) {
            Virtual<void>(At<void*>(self, field::kDevelopment), 0x4C, &types, *id);
            ++id;
            end = *reinterpret_cast<const uint32_t* const*>(info + 0x2C);
        }
        if (types.count) {
            struct {
                char* node;
                void* table;
            } first{};
            This<void>(addr::SC4HashTable_Begin, &types, &first);
            if (Virtual<uint8_t>(At<void*>(self, field::kDevelopment), 0x38, At<uint32_t>(first.node, 4), key)) {
                void** const preview = &At<void*>(self, field::kPreview);
                Clear(preview);
                if (Virtual<uint8_t>(props, 0x40, key, preview)) {
                    void* const others = Global<void*>(addr::SC4_Service43DA4);
                    void* const fire = Global<void*>(addr::SC4_Service43D78);
                    void* const police = Global<void*>(addr::SC4_PoliceSimulator);
                    void* const occupant = Virtual<void*>(*preview, 0xC);
                    Virtual<void>(occupant, 0x74, kOccupantFlags);
                    Cdecl<uint8_t>(addr::SC4_SetPropertyBool, Virtual<void*>(occupant, 0x18), kPropertyA, uint32_t{1});
                    Cdecl<uint8_t>(addr::SC4_SetPropertyBool, Virtual<void*>(occupant, 0x18), kPropertyB, uint32_t{1});
                    Tint tint{};
                    This<void>(addr::SC4Tint_Construct, &tint);
                    tint.mode = 2;
                    tint.alpha = 0.0f;
                    This<void>(addr::cSC4ViewInputControlPlaceLot_MovePreview, self, self + field::kPoint, &tint,
                               uint32_t{0});
                    At<uint32_t>(self, field::kServices) = 0;
                    void* const holder = Virtual<void*>(occupant, 0x18);
                    if (Virtual<uint8_t>(police, 0x58, occupant))
                        Service(self, police, 0x50, 1, kHalfShare, occupant,
                                addr::cSC4ViewInputControlPlaceLot_PolicePlop,
                                addr::cSC4ViewInputControlPlaceLot_PoliceInactive);
                    if (Virtual<uint8_t>(fire, 0x1C, occupant))
                        Service(self, fire, 0x54, 2, kHalfShare, occupant,
                                addr::cSC4ViewInputControlPlaceLot_FirePlop,
                                addr::cSC4ViewInputControlPlaceLot_FireInactive);
                    uint32_t value = 0;
                    if (Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, kPropertyHealth, &value))
                        Service(self, others, 0xB4, 4, kOtherShare, occupant,
                                addr::cSC4ViewInputControlPlaceLot_HealthPlop,
                                addr::cSC4ViewInputControlPlaceLot_HealthInactive);
                    if (Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, kPropertyEducation, &value))
                        Service(self, others, 0xB4, 8, kOtherShare, occupant,
                                addr::cSC4ViewInputControlPlaceLot_EducationPlop,
                                addr::cSC4ViewInputControlPlaceLot_EducationInactive);
                    const uint32_t services = At<uint32_t>(self, field::kServices);
                    This<void>(addr::SC4ViewMarkers_F7D1D10, Virtual<void*>(At<void*>(self, field::kView), 0xC4),
                               services);
                    This<void>(addr::SC4ViewMarkers_F7D20E0, Virtual<void*>(At<void*>(self, field::kView), 0xC4));
                    if (Virtual<uint8_t>(occupant, 0x58, uint32_t{0x1401}))
                        At<uint32_t>(self, field::kCursor) = 0xA;
                    else if (Virtual<uint8_t>(occupant, 0x58, uint32_t{0x1302}))
                        At<uint32_t>(self, field::kCursor) = 0xF2;
                    At<uint32_t>(self, field::kPlacementsLeft) = 1000;
                    uint32_t once = 0;
                    uint8_t onceFlag = 0;
                    if (Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, kPropertyOnce, &once) ||
                        (Cdecl<uint8_t>(addr::SC4_GetPropertyBool, holder, kPropertyOnceFlag, &onceFlag) && onceFlag))
                        At<uint32_t>(self, field::kPlacementsLeft) = 1;
                    uint8_t count = 0;
                    if (Cdecl<uint8_t>(addr::SC4_GetPropertyUint8, holder, kPropertyCount, &count))
                        At<uint32_t>(self, field::kPlacementsLeft) =
                            0x10u - Virtual<uint32_t>(Global<void*>(addr::SC4_Service43DA8), 0x80, uint32_t{count});
                }
            }
        }
    }
    This<void>(addr::SC4Uint32HashSet_Clear, &types);
    if (types.begin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(types.begin));
}

// A record for each prop of the configuration drawn with the preview: a
// record of kind 1 (its flag bit 1 clear) with exactly one prop type that
// the prop manager has and makes, its property 0x4A89FCF3 true.
void AddProps(char* self, void* configuration, uint32_t width, uint32_t depth)
{
    void* const props = Global<void*>(addr::SC4_Service43D10);
    void* const terrain = Global<void*>(addr::SC4_Terrain);
    char** const records = Virtual<char**>(configuration, 0x5C);
    for (char* record = records[0]; record != records[1]; record += kRecordSize) {
        if (record[0] != kRecordProp || (record[1] & 1) != 0)
            continue;
        const uint32_t* const types = *reinterpret_cast<uint32_t* const*>(record + 0x28);
        const uint32_t* const typesEnd = *reinterpret_cast<uint32_t* const*>(record + 0x2C);
        if (((reinterpret_cast<std::uintptr_t>(typesEnd) - reinterpret_cast<std::uintptr_t>(types)) & ~3u) != 4)
            continue;
        const uint32_t type = *types;
        if (!Virtual<uint8_t>(props, 0x20, type))
            continue;
        void* holder = nullptr;
        if (!Virtual<uint8_t>(props, 0x3C, type, &holder))
            continue;
        uint8_t drawn = 0;
        void* const occupant = Virtual<void*>(holder, 0xC);
        Virtual<void>(occupant, 0x74, kOccupantFlags);
        Cdecl<uint8_t>(addr::SC4_GetPropertyBool, Virtual<void*>(occupant, 0x18), kPropertyDrawn, &drawn);
        if (drawn) {
            Cdecl<uint8_t>(addr::SC4_SetPropertyBool, Virtual<void*>(occupant, 0x18), kPropertyA, uint32_t{1});
            Cdecl<uint8_t>(addr::SC4_SetPropertyBool, Virtual<void*>(occupant, 0x18), kPropertyB, uint32_t{1});
            struct {
                uint8_t turn;
                uint8_t stack[3];
                float a;
                float b;
                float c;
                void* holder;
            } entry{};
            static_assert(sizeof(entry) == 0x14);
            entry.turn = static_cast<uint8_t>(record[2]);
            std::memcpy(&entry.a, record + 4, sizeof(float));
            std::memcpy(&entry.b, record + 8, sizeof(float));
            std::memcpy(&entry.c, record + 0xC, sizeof(float));
            double cell = Virtual<double>(terrain, 0x18);
            entry.a = static_cast<float>(static_cast<double>(entry.a) -
                                         cell * Unsigned(width) * static_cast<double>(kHalf));
            cell = Virtual<double>(terrain, 0x18);
            entry.holder = holder;
            entry.c = static_cast<float>(static_cast<double>(entry.c) -
                                         cell * Unsigned(depth) * static_cast<double>(kHalf));
            Virtual<uint32_t>(holder, 0x4);
            This<void>(addr::cSC4ViewInputControlPlaceLot_PushBack, self + field::kRecords, &entry);
        }
        Release(holder);
    }
}

// The footprint picture: the configuration's size in world units, scaled
// to fit 256 pixels when larger, on a square buffer of the next power of two
// from 16 (the larger side), a white frame two pixels wide inside a
// translucent white one; the buffer becomes the texture binding at +0x100.
void PaintPicture(char* self, void* configuration)
{
    void* buffer = nullptr;
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_Service43C9C), 0xC, &buffer)) {
        if (buffer)
            Release(buffer);
        return;
    }
    void* const terrain = Global<void*>(addr::SC4_Terrain);
    uint32_t width = 0;
    uint32_t depth = 0;
    Virtual<void>(configuration, 0x1C, &width, &depth);
    uint32_t pixelsX = Ftol(Virtual<double>(terrain, 0x18) * Unsigned(width));
    uint32_t pixelsZ = Ftol(Virtual<double>(terrain, 0x18) * Unsigned(depth));
    const uint32_t larger = pixelsX < pixelsZ ? pixelsZ : pixelsX;
    uint32_t side;
    float size;
    if (larger > 0x100) {
        size = static_cast<float>(Unsigned(larger));
        pixelsX = (pixelsX << 8) / larger;
        side = 0x100;
        pixelsZ = (pixelsZ << 8) / larger;
    } else {
        if (larger <= 0x10)
            side = 0x10;
        else if (larger <= 0x20)
            side = 0x20;
        else if (larger <= 0x40)
            side = 0x40;
        else
            side = larger > 0x80 ? 0x100 : 0x80;
        size = static_cast<float>(Unsigned(side));
    }
    At<float>(self, field::kPictureSize) = size;
    uint32_t left = (side - pixelsX) >> 1;
    uint32_t top = (side - pixelsZ) >> 1;
    if (Virtual<uint8_t>(buffer, 0xC, side, side, uint32_t{9}, uint32_t{0x20})) {
        if (Virtual<uint8_t>(buffer, 0x18, kLockPaint)) {
            Virtual<void>(buffer, 0x48, uint32_t{0}, uint32_t{0});
            Virtual<void>(buffer, 0x4C, kFrameOuter, left, top, side - left, side - top, uint32_t{0});
            top += 2;
            left += 2;
            Virtual<void>(buffer, 0x4C, kFrameInner, left, top, side - left, side - top, uint32_t{0});
            Virtual<void>(buffer, 0x1C, kLockPaint);
        }
        if (Virtual<uint8_t>(buffer, 0x18, kLockData)) {
            void* image = Virtual<void*>(buffer, 0x88);
            void* factory = nullptr;
            if (void* const framework = Cdecl<void*>(addr::RZGetFramework))
                Virtual<uint8_t>(framework, 0x14, kTextureFactoryClass, kTextureFactoryInterface, &factory);
            void** const binding = &At<void*>(self, field::kTexture);
            // The description: two ones, the side twice, a word the game's
            // stack held (ours zero), 3, 1 and the image.
            struct {
                uint32_t one[2];
                uint32_t width;
                uint32_t height;
                uint32_t stack;
                uint32_t three;
                uint32_t mips;
                void** images;
            } description{{1, 1}, side, side, 0, 3, 1, &image};
            if (void* const old = *binding) {
                *binding = nullptr;
                This<void>(addr::SC4TextureBinding_Release, old);
            }
            Virtual<void>(factory, 0x10, &description, binding);
            Virtual<void>(buffer, 0x1C, kLockData);
            This<void>(addr::SC4RefPtr_Destruct, &factory);
        }
        Virtual<void>(buffer, 0x10);
        Clear(&buffer);
    }
    if (buffer)
        Release(buffer);
}

// The game's 0x004C0680 (no arguments).
void __fastcall BuildPreview(char* self, void*)
{
    void* const configuration =
        Virtual<void*>(At<void*>(self, field::kConfigManager), 0x20, At<uint32_t>(self, field::kConfig));
    if (!configuration)
        return;
    uint32_t width = 0;
    uint32_t depth = 0;
    Virtual<void>(configuration, 0x1C, &width, &depth);
    if (!At<void*>(self, field::kPreview))
        MakePreview(self, configuration, width, depth);
    AddProps(self, configuration, width, depth);
    if (!At<void*>(self, field::kTexture))
        PaintPicture(self, configuration);
}

bool InstallPlop4(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[3] = {};
    static const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::cSC4ViewInputControlPlaceLot_UpdatePreview, reinterpret_cast<void*>(&UpdatePreview)},
        {addr::cSC4ViewInputControlPlaceLot_UpdateFootprint, reinterpret_cast<void*>(&UpdateFootprint)},
        {addr::cSC4ViewInputControlPlaceLot_BuildPreview, reinterpret_cast<void*>(&BuildPreview)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    static_assert(count <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < count; ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].address);
        if (!found || !hook::Install(*found, hooks[i].ours, &originals[i]))
            return false;
    }
    return true;
}

const bool g_registered = registry::Register({
    "ui.tools.plop4",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4ViewInputControlPlaceLot_UpdatePreview,
    "Our plop tool's preview drawing: the preview update, the footprint rectangles and the preview builder "
    "(docs/tools.md)",
    false,
    InstallPlop4,
});

} // namespace
} // namespace opensc4::tool_plop_4
