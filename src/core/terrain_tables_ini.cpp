// Our cSTETerrain INI loader (build 641), piece 8 of the city's terrain
// object: the second of the four loaders the terrain's one-time set-up
// 0x0074FE40 runs (the others: terrain_tables.cpp, terrain_brushload.cpp).
//
//   0x0074DC30  LoadIni           opens the resource, reads everything below
//   0x0074D5A0  TextureMapRow     one entry of <prefix>TextureMapTable
//   0x0074C800  LoadMiscTextures  the section <prefix>MiscTextures
//
// The resource: type 0, group 0x8A5971C5, instance 0xAA597172, opened with
// SC4::OpenResourceRecord (0x005F9670); an ini file reader (0x3C bytes) is
// made on the record (0x0091CF46). Nothing is read without the record.
// <prefix> is the string object 0x00B4BC64 (the game's data has the sections
// TropicalTextureMapTable and TropicalMiscTextures).
//
// What is read, in this order, and where it goes (the exe's value in
// brackets: it stays when the key is missing):
//   <prefix>TextureMapTable, every entry (cRZIniFile::ForEachEntry with
//       TextureMapRow): the key is a row number, the value sixteen numbers
//       cut at commas: 0x00B4B420 + 64 x row, uint32[16] [zeros]; each
//       number also into the sorted list 0x00B4BCD8. The row is not
//       checked against the table's 32 rows.
//   <prefix>MiscTextures:
//       HighCliff  uint32 0x00B4B418 [0], and into the list
//       LowCliff   uint32 0x00B4B414 [0], and into the list
//       Beach      uint32 0x00B4B410 [0], and into the list
//     then 0x13, 0x14, 0xE0, 0x11, 0x10 and 0x15 into the list
//   MiscTerrainTunables:
//       RaiseLowerAmount  float 0x00B0D578 [10.5]
//       EnableSimulation  uint8 0x00B0D568 [1]: whether the number is not 0
//   WindErosion:
//       NumIterationsPerUserClick  uint32 0x00B0D594 [5]
//       NumIterationsPerSimMonth   uint32 0x00B0D598 [1]
//       TalusAngle  a whole number (unsigned) x the float 0x00B0D694
//                   (pi / 180): float 0x00B0D59C [0.79]
//     then, with or without the key:
//       0x00B4BCF4 = tan(0x00B0D59C) x 16 (FPTAN; the float 0x00AB3B5C)
//       0x00B4BCF0 = sqrt(2) x 0x00B4BCF4
//       ErosionFactor  float 0x00B4B40C [0]
//   Hydrology:
//       NumWaterErosionIterationsPerPlayerClick  uint32 0x00B0D5A0 [1]
//       NumFlowStepsForGlobalWaterErosion        uint32 0x00B0D5A4 [35]
//       SurfaceWaterThreshold    float 0x00B0D574 [10]
//       RainFallFactor           float 0x00B0D5A8 [0.01]
//       WaterSourceCapacity      float 0x00B0D56C [5.5]
//       WaterSinkCapacity        float 0x00B0D570 [5.5]
//       MaxSedimentPerUnitWater  float 0x00B0D5AC [5]
//       MinErosionRate           float 0x00B0D5B8 [0.02]
//       MaxErosionRate           float 0x00B0D5BC [0.06]
//       HydrErosionRateRampBegin float 0x00B0D5C0 [5.128]:
//                                tan(number x pi / 180) x 16 (FPTAN)
//       HydrErosionRateRampEnd   float 0x00B0D5C4 [38.14]: the same
//       DepositRate              float 0x00B0D5B0 [0.2]
//       SedimentSpreadRateUnderSea  float 0x00B4B408 [0]; then, with or
//                                without the key, 1 when above 1
//       DepositRateUnderSea      float 0x00B0D5B4 [0.02]
//       SedimentInfluenceOnWaterColor  float 0x00B0D5C8 [0.1]
//       MaxSedimentAlpha         float 0x00B0D5CC [0.5]; then 1 when above 1
//       SedimentColorRed, Green, Blue  uint8 0x00B0D5D0, D1, D2
//                                [0x76, 0x71, 0x68]: the number's low byte
// Numbers: whole ones through SC4::TextToUint32 (0x0090FE93), the others
// through the game's text to double (0x00910991).
//
// The x87. Every float the game stores straight from the parser's result
// (FSTP dword on what 0x00910991 left in ST0) is stored the same way here,
// by a naked helper that calls the parser and stores, so that no rounding
// to double comes between. The three tangents are FPTAN, which the C
// library cannot repeat: the helpers execute the game's instructions in the
// game's order on the same operands (FLD float or the parser's result times
// the float 0x00B0D694; FPTAN; FSTP ST(0) to drop the 1.0 it pushes; FMUL
// float 16; FSTP float). FPTAN's result keeps the register's full 64 bits
// whatever the precision setting; the FMUL after it rounds to the 53 bits
// the game runs at, and the store to float rounds again: both happen inside
// the helper exactly as in the game, so nothing depends on how C++ would
// carry the tangent. The square root is FSQRT of the double 2.0 times the
// float just stored, in a helper too.
//
// Allocation order. The game builds each key and section name as a string
// object (one block from the STL allocator per string), asks, and frees
// them; that order is kept: the value string once (8 bytes), then per key
// the key's name, the section's name, the question, the section's name
// freed, the key's name freed. Where the game has the string construction
// inline (the first eight questions) this file allocates through
// SC4::stl_allocate itself; where it calls SC4String::FromText so does this.
//
// Still the game's and called at their addresses: the record's opening, the
// ini file reader (constructor, ForEachEntry, GetString directly and through
// its slot 6, destructor), the string functions (concatenations 0x0041E270
// and 0x00408AE0, 0x00408420, FromRange, FromText, CopyBuffer, SplitOff,
// AssignRange, Data), the two number parsers, the allocator.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/terrain_sim.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::terrain_sim {
namespace {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("terrain: first %s", name);
}

template <typename R, typename... Args>
inline R This(std::uintptr_t function, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(function)(self, args...);
}

template <typename R, typename... Args>
inline R Cdecl(std::uintptr_t function, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(function)(args...);
}

// The body of a game string (begin, end, capacity end) and the string
// object around it (0x14 bytes; vtable 0x00A80810).
struct GameBody {
    uint32_t begin, end, capacity;
};
struct GameString {
    uint32_t vtable;
    GameBody body;
    uint32_t references;
};
static_assert(sizeof(GameString) == 0x14);

// --- The x87 instructions C++ cannot repeat ----------------------------------

// PUSH text, CALL the parser (cdecl, the number in ST0), ADD ESP 4, FSTP
// float: 0x0074DDFF to 0x0074DE0F and the like.
__declspec(naked) void __cdecl X87ParseToFloat(std::uintptr_t /*parser*/, const char* /*text*/, float* /*out*/)
{
    __asm push dword ptr [esp + 8]
    __asm call dword ptr [esp + 8]
    __asm add esp, 4
    __asm mov eax, [esp + 12]
    __asm fstp dword ptr [eax]
    __asm ret
}

// The same call, then FMUL float (degrees to radians), FPTAN, FSTP ST(0),
// FMUL float (the cell width), FSTP float: 0x0074E83D to 0x0074E85A and
// 0x0074E8C8 to 0x0074E8E5.
__declspec(naked) void __cdecl X87ParseTanScaled(std::uintptr_t /*parser*/, const char* /*text*/, const float* /*degree*/,
                                                 const float* /*scale*/, float* /*out*/)
{
    __asm push dword ptr [esp + 8]
    __asm call dword ptr [esp + 8]
    __asm add esp, 4
    __asm mov eax, [esp + 12]
    __asm fmul dword ptr [eax]
    __asm fptan
    __asm fstp st(0)
    __asm mov eax, [esp + 16]
    __asm fmul dword ptr [eax]
    __asm mov eax, [esp + 20]
    __asm fstp dword ptr [eax]
    __asm ret
}

// FLD float, FPTAN, FSTP ST(0), FMUL float, FSTP float: 0x0074E1D2 to
// 0x0074E1F0.
__declspec(naked) void __cdecl X87TanScaled(const float* /*angle*/, const float* /*scale*/, float* /*out*/)
{
    __asm mov eax, [esp + 4]
    __asm fld dword ptr [eax]
    __asm fptan
    __asm fstp st(0)
    __asm mov eax, [esp + 8]
    __asm fmul dword ptr [eax]
    __asm mov eax, [esp + 12]
    __asm fstp dword ptr [eax]
    __asm ret
}

// FLD double, FSQRT, FMUL float, FSTP float: 0x0074E1F6 to 0x0074E204.
__declspec(naked) void __cdecl X87RootScaled(const double* /*value*/, const float* /*factor*/, float* /*out*/)
{
    __asm mov eax, [esp + 4]
    __asm fld qword ptr [eax]
    __asm fsqrt
    __asm mov eax, [esp + 8]
    __asm fmul dword ptr [eax]
    __asm mov eax, [esp + 12]
    __asm fstp dword ptr [eax]
    __asm ret
}

// --- Strings as the game makes them ------------------------------------------

inline uint32_t Allocate(uint32_t size) { return Cdecl<uint32_t>(addr::SC4_stl_allocate, size); }

inline void Release(uint32_t block) { Cdecl<void>(addr::SC4_Free, block); }

// An empty string: 8 bytes from the allocator (0x0074DC7B, 0x0074C80C,
// 0x0074D5A9).
void MakeEmpty(GameString* text)
{
    const uint32_t block = Allocate(8u);
    text->body.begin = block;
    text->body.end = block;
    text->body.capacity = block + 8u;
    AtAddress<uint8_t>(block) = 0;
    text->vtable = addr::SC4String_Vtable;
    text->references = 0;
}

// A string from `length` characters, as the game's inline constructions:
// length + 1 bytes from the allocator, the characters, a zero.
void MakeCopy(GameString* text, const char* characters, uint32_t length)
{
    const uint32_t block = Allocate(length + 1u);
    text->body.begin = block;
    text->body.capacity = block + length + 1u;
    std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(block)), characters, length);
    text->body.end = block + length;
    AtAddress<uint8_t>(block + length) = 0;
    text->vtable = addr::SC4String_Vtable;
    text->references = 0;
}

inline void MakeInline(GameString* text, const char* literal)
{
    MakeCopy(text, literal, static_cast<uint32_t>(std::strlen(literal)));
}

// SC4String::FromText (0x00408480): the same block, made by the game.
inline void MakeFromText(GameString* text, const char* literal) { This<void*>(addr::SC4String_FromText, text, literal); }

inline void Drop(GameString* text)
{
    if (text->body.begin)
        Release(text->body.begin);
}

inline const char* Data(GameString* text) { return This<const char*>(addr::cRZBaseString_Data, text); }

inline uint32_t Number(GameString* text) { return Cdecl<uint32_t>(addr::SC4_TextToUint32, Data(text)); }

// --- The list of texture IDs -------------------------------------------------

// The game's inline insert into the sorted list 0x00B4BCD8 + 4 x index
// (0x0074CA91, 0x0074D6A9 and others): nothing when the value is there;
// else a node of 0xC bytes before the first greater value (unsigned), or at
// the end.
void AddTextureID(uint32_t index, uint32_t value)
{
    const uint32_t head = Global<uint32_t>(addr::cSTETerrain_TextureIDs + 4u * index);
    uint32_t node = AtAddress<uint32_t>(head);
    while (node != head) {
        const uint32_t have = AtAddress<uint32_t>(node + 8u);
        if (have == value)
            return;
        if (have > value)
            break;
        node = AtAddress<uint32_t>(node);
    }
    const uint32_t fresh = Allocate(0xCu);
    AtAddress<uint32_t>(fresh + 8u) = value;
    const uint32_t previous = AtAddress<uint32_t>(node + 4u);
    AtAddress<uint32_t>(fresh) = node;
    AtAddress<uint32_t>(fresh + 4u) = previous;
    AtAddress<uint32_t>(previous) = fresh;
    AtAddress<uint32_t>(node + 4u) = fresh;
}

// --- 0x0074D5A0 --------------------------------------------------------------

// (key, value, context; cdecl, cRZIniFile::ForEachEntry's callback). An
// empty token string, a copy of the value (SC4String::CopyBuffer of its
// body from 0 to the end; a null value is passed on as a null body, as the
// game does), the row from the key's text (its slot 5). Sixteen times: the
// rest's part before the next comma is split off (SC4String::SplitOff
// returns a string object in the place given) and assigned to the token
// unless it is the token itself, the split-off string's block freed, the
// token's number stored in the row and added to the list.
void __cdecl TextureMapRow(void* key, void* value, uint32_t context)
{
    GameString token;
    MakeEmpty(&token);
    GameString rest{};
    uint32_t tag = 0;
    This<void*>(addr::SC4String_CopyBuffer, static_cast<void*>(&rest.body),
                value ? static_cast<void*>(static_cast<char*>(value) + 4) : nullptr, 0u, 0xFFFFFFFFu, static_cast<void*>(&tag));
    rest.vtable = addr::SC4String_Vtable;
    rest.references = 0;
    const uint32_t row = Cdecl<uint32_t>(addr::SC4_TextToUint32, Virtual<const char*>(key, 0x14));
    const uint32_t cells = static_cast<uint32_t>(addr::cSTETerrain_TextureMap) + ((row + (context << 5)) << 6);
    for (uint32_t column = 0; column < 16; ++column) {
        GameString cut{};
        char* const head = This<char*>(addr::SC4String_SplitOff, static_cast<void*>(&rest), static_cast<void*>(&cut), ",");
        GameBody* const body = head ? reinterpret_cast<GameBody*>(head + 4) : nullptr;
        if (body != &token.body)
            This<void*>(addr::SC4String_AssignRange, static_cast<void*>(&token.body), body->begin, body->end,
                        static_cast<void*>(&tag));
        Drop(&cut);
        const uint32_t number = Number(&token);
        AtAddress<uint32_t>(cells + 4u * column) = number;
        AddTextureID(context, number);
    }
    Drop(&rest);
    Drop(&token);
}

// --- 0x0074C800 --------------------------------------------------------------

// One question of 0x0074C800: the key's name (inline), a copy of the
// section's name (its length taken from its text, as the game does), the
// reader's slot 6 (section, key, out), the copy freed, the key freed.
bool AskMisc(void* ini, GameString* section, const char* key, GameString* value)
{
    GameString name;
    MakeInline(&name, key);
    const char* const text = Data(section);
    GameString copy;
    MakeCopy(&copy, text, static_cast<uint32_t>(std::strlen(text)));
    const bool found = Virtual<uint8_t>(ini, 0x18, static_cast<void*>(&copy), static_cast<void*>(&name), static_cast<void*>(value)) != 0;
    Drop(&copy);
    Drop(&name);
    return found;
}

// (ini; cdecl). An empty value string and the suffix "MiscTextures"
// (SC4String::FromRange). For the one prefix (the game's loop runs while its
// index is below 1): the section's name = prefix + suffix (0x00408AE0 makes
// the body, which is copied into a string of exactly its length and
// freed), the three keys, the six fixed IDs, the section's name freed.
// Then the suffix and the value are freed.
void LoadMiscTextures(void* ini)
{
    static const char kSuffix[] = "MiscTextures";
    GameString value;
    MakeEmpty(&value);
    GameString suffix{};
    uint8_t tag = 0;
    This<void>(addr::SC4String_FromRange, static_cast<void*>(&suffix.body), static_cast<const void*>(&kSuffix[0]),
               static_cast<const void*>(&kSuffix[sizeof kSuffix - 1]), static_cast<void*>(&tag));
    suffix.vtable = addr::SC4String_Vtable;
    suffix.references = 0;
    for (uint32_t index = 0; index < 1; ++index) {
        GameBody joined{0, 0, 0};
        const GameBody* const made = Cdecl<const GameBody*>(
            addr::cSC4Advice_F408AE0, static_cast<void*>(&joined),
            reinterpret_cast<void*>(addr::cSTETerrain_TexturePrefix + 4u + 0x14u * index), static_cast<void*>(&suffix.body));
        GameString section;
        MakeCopy(&section, reinterpret_cast<const char*>(static_cast<std::uintptr_t>(made->begin)), made->end - made->begin);
        if (joined.begin)
            Release(joined.begin);

        if (AskMisc(ini, &section, "HighCliff", &value)) {
            const uint32_t number = Number(&value);
            Global<uint32_t>(addr::cSTETerrain_HighCliffTexture + 4u * index) = number;
            AddTextureID(index, number);
        }
        if (AskMisc(ini, &section, "LowCliff", &value)) {
            const uint32_t number = Number(&value);
            Global<uint32_t>(addr::cSTETerrain_LowCliffTexture + 4u * index) = number;
            AddTextureID(index, number);
        }
        if (AskMisc(ini, &section, "Beach", &value)) {
            const uint32_t number = Number(&value);
            Global<uint32_t>(addr::cSTETerrain_BeachTexture + 4u * index) = number;
            AddTextureID(index, number);
        }
        AddTextureID(index, 0x13u);
        AddTextureID(index, 0x14u);
        AddTextureID(index, 0xE0u);
        AddTextureID(index, 0x11u);
        AddTextureID(index, 0x10u);
        AddTextureID(index, 0x15u);
        Drop(&section);
    }
    Drop(&suffix);
    Drop(&value);
}

// --- 0x0074DC30 --------------------------------------------------------------

// How the game makes the two names of a question.
enum class Names {
    Inline,        // both inline (0x0074DD30 to 0x0074E356)
    InlineKeyOnly, // the key inline, the section through FromText (0x0074E3C4)
    FromText,      // both through SC4String::FromText (0x0074E478 on)
};

// One question: the key's name, the section's name, cRZIniFile::GetString
// (section, key, out), the section's name freed, the key's name freed.
bool Ask(void* ini, const char* section, const char* key, GameString* value, Names names)
{
    GameString keyName, sectionName;
    if (names == Names::FromText)
        MakeFromText(&keyName, key);
    else
        MakeInline(&keyName, key);
    if (names == Names::Inline)
        MakeInline(&sectionName, section);
    else
        MakeFromText(&sectionName, section);
    const bool found = This<uint8_t>(addr::cRZIniFile_GetString, ini, static_cast<void*>(&sectionName),
                                     static_cast<void*>(&keyName), static_cast<void*>(value)) != 0;
    Drop(&sectionName);
    Drop(&keyName);
    return found;
}

// The value's text parsed and stored as a float, the game's way.
inline void StoreFloat(GameString* value, std::uintptr_t destination)
{
    X87ParseToFloat(addr::SC4_ParseDouble910991, reinterpret_cast<const char*>(static_cast<std::uintptr_t>(value->body.begin)),
                    &Global<float>(destination));
}

// A Hydrology float.
inline void HydrologyFloat(void* ini, GameString* value, const char* key, std::uintptr_t destination)
{
    if (Ask(ini, "Hydrology", key, value, Names::FromText))
        StoreFloat(value, destination);
}

// A Hydrology slope: degrees to the tangent times the cell width.
inline void HydrologySlope(void* ini, GameString* value, const char* key, std::uintptr_t destination)
{
    if (Ask(ini, "Hydrology", key, value, Names::FromText))
        X87ParseTanScaled(addr::SC4_ParseDouble910991,
                          reinterpret_cast<const char*>(static_cast<std::uintptr_t>(value->body.begin)),
                          &Global<float>(addr::cSTETerrain_DegreeFloat), &Global<float>(addr::cSTETerrain_CellWidthFloat),
                          &Global<float>(destination));
}

// FLD float, FCOMP double 1.0, 1.0 when above (0x0074E9E1, 0x0074EB6F): a
// NaN stays.
inline void AtMostOne(std::uintptr_t destination)
{
    if (static_cast<double>(Global<float>(destination)) > Global<double>(addr::SC4_DoubleOne))
        Global<uint32_t>(destination) = 0x3F800000u;
}

// (no arguments). The record of the key (0, 0x8A5971C5, 0xAA597172); when
// it does not open, a record the opener may have left is released and that
// is all. Else, as the list at the top of the file says. The game leaves
// the record's release to the end, after the reader's destructor.
void __cdecl LoadIni()
{
    static bool logged = false;
    FirstCall("LoadIni", logged);
    void* record = nullptr;
    uint32_t key[3] = {0u, 0x8A5971C5u, 0xAA597172u};
    if (!Cdecl<uint8_t>(addr::SC4_OpenResourceRecord, static_cast<void*>(key), &record, 0u)) {
        if (record)
            Virtual<uint32_t>(record, 0x8);
        return;
    }
    alignas(4) uint8_t reader[0x3C];
    void* const ini = static_cast<void*>(reader);
    This<void*>(addr::cRZIniFile_ConstructFromRecord, ini, record);
    GameString value;
    MakeEmpty(&value);

    // <prefix>TextureMapTable: the name's body by 0x0041E270, copied into a
    // string by 0x00408420 and freed.
    {
        GameBody joined{0, 0, 0};
        const GameBody* const made =
            Cdecl<const GameBody*>(addr::SC4String_ConcatBodyText, static_cast<void*>(&joined),
                                   reinterpret_cast<void*>(addr::cSTETerrain_TexturePrefix + 4u), "TextureMapTable");
        GameString section{};
        uint8_t tag = 0;
        This<void>(addr::cSC4Advice_F408420, static_cast<void*>(&section.body), made->begin, made->end, static_cast<void*>(&tag));
        section.vtable = addr::SC4String_Vtable;
        section.references = 0;
        if (joined.begin)
            Release(joined.begin);
        This<void>(addr::cRZIniFile_ForEachEntry, ini, static_cast<void*>(&section), reinterpret_cast<void*>(&TextureMapRow), 0u);
        Drop(&section);
    }
    LoadMiscTextures(ini);

    if (Ask(ini, "MiscTerrainTunables", "RaiseLowerAmount", &value, Names::Inline))
        StoreFloat(&value, addr::cSTETerrain_IniRaiseLowerAmount);
    if (Ask(ini, "MiscTerrainTunables", "EnableSimulation", &value, Names::Inline))
        Global<uint8_t>(addr::cSTETerrain_IniEnableSimulation) = Number(&value) != 0 ? 1 : 0;
    if (Ask(ini, "WindErosion", "NumIterationsPerUserClick", &value, Names::Inline))
        Global<uint32_t>(addr::cSTETerrain_SmoothOnlyPasses) = Number(&value);
    if (Ask(ini, "WindErosion", "NumIterationsPerSimMonth", &value, Names::Inline))
        Global<uint32_t>(addr::cSTETerrain_IniWindIterationsPerMonth) = Number(&value);
    if (Ask(ini, "WindErosion", "TalusAngle", &value, Names::Inline)) {
        // FILD of the number (2 to the 32nd added when its top bit is set:
        // the unsigned value), FLD float, FMUL, FSTP float (0x0074E1AA to
        // 0x0074E1CA): exact as a double product.
        const uint32_t degrees = Number(&value);
        Global<float>(addr::cSTETerrain_IniTalusAngle) =
            static_cast<float>(static_cast<double>(Global<float>(addr::cSTETerrain_DegreeFloat)) * static_cast<double>(degrees));
    }
    X87TanScaled(&Global<float>(addr::cSTETerrain_IniTalusAngle), &Global<float>(addr::cSTETerrain_CellWidthFloat),
                 &Global<float>(addr::cSTETerrain_SmoothStraightLimit));
    X87RootScaled(&Global<double>(addr::SC4_DoubleTwo), &Global<float>(addr::cSTETerrain_SmoothStraightLimit),
                  &Global<float>(addr::cSTETerrain_SmoothDiagonalLimit));
    if (Ask(ini, "WindErosion", "ErosionFactor", &value, Names::Inline))
        StoreFloat(&value, addr::cSTETerrain_SmoothFactor);

    if (Ask(ini, "Hydrology", "NumWaterErosionIterationsPerPlayerClick", &value, Names::Inline))
        Global<uint32_t>(addr::cSTETerrain_ErodeRuns) = Number(&value);
    if (Ask(ini, "Hydrology", "NumFlowStepsForGlobalWaterErosion", &value, Names::InlineKeyOnly))
        Global<uint32_t>(addr::cSTETerrain_ErodeIterations) = Number(&value);
    HydrologyFloat(ini, &value, "SurfaceWaterThreshold", addr::cSTETerrain_IniSurfaceWaterThreshold);
    HydrologyFloat(ini, &value, "RainFallFactor", addr::cSTETerrain_ErodeRain);
    HydrologyFloat(ini, &value, "WaterSourceCapacity", addr::cSTETerrain_IniWaterSourceCapacity);
    HydrologyFloat(ini, &value, "WaterSinkCapacity", addr::cSTETerrain_IniWaterSinkCapacity);
    HydrologyFloat(ini, &value, "MaxSedimentPerUnitWater", addr::cSTETerrain_ErodeCapacity);
    HydrologyFloat(ini, &value, "MinErosionRate", addr::cSTETerrain_ErodeRateLow);
    HydrologyFloat(ini, &value, "MaxErosionRate", addr::cSTETerrain_ErodeRateHigh);
    HydrologySlope(ini, &value, "HydrErosionRateRampBegin", addr::cSTETerrain_ErodeSlopeLow);
    HydrologySlope(ini, &value, "HydrErosionRateRampEnd", addr::cSTETerrain_ErodeSlopeHigh);
    HydrologyFloat(ini, &value, "DepositRate", addr::cSTETerrain_ErodeDeposit);
    HydrologyFloat(ini, &value, "SedimentSpreadRateUnderSea", addr::cSTETerrain_IniSedimentSpreadUnderSea);
    AtMostOne(addr::cSTETerrain_IniSedimentSpreadUnderSea);
    HydrologyFloat(ini, &value, "DepositRateUnderSea", addr::cSTETerrain_IniDepositRateUnderSea);
    HydrologyFloat(ini, &value, "SedimentInfluenceOnWaterColor", addr::cSTETerrain_IniSedimentInfluence);
    HydrologyFloat(ini, &value, "MaxSedimentAlpha", addr::cSTETerrain_IniMaxSedimentAlpha);
    AtMostOne(addr::cSTETerrain_IniMaxSedimentAlpha);
    if (Ask(ini, "Hydrology", "SedimentColorRed", &value, Names::FromText))
        Global<uint8_t>(addr::cSTETerrain_IniSedimentColor) = static_cast<uint8_t>(Number(&value));
    if (Ask(ini, "Hydrology", "SedimentColorGreen", &value, Names::FromText))
        Global<uint8_t>(addr::cSTETerrain_IniSedimentColor + 1u) = static_cast<uint8_t>(Number(&value));
    if (Ask(ini, "Hydrology", "SedimentColorBlue", &value, Names::FromText))
        Global<uint8_t>(addr::cSTETerrain_IniSedimentColor + 2u) = static_cast<uint8_t>(Number(&value));

    Drop(&value);
    This<void>(addr::cRZIniFile_Destruct, ini);
    if (record)
        Virtual<uint32_t>(record, 0x8);
}

// --- Installation ------------------------------------------------------------

void* g_original = nullptr;

// The one direct call site of 0x0074DC30 is 0x0074FE7F in 0x0074FE40: no
// arguments, nothing kept in a register across it, no use of a result; its
// address occurs nowhere else in the image. 0x0074C800 is called only from
// 0x0074DC30 (0x0074DD2B; one stack argument, cdecl) and 0x0074D5A0 only
// through the pointer 0x0074DC30 hands cRZIniFile::ForEachEntry (its address
// occurs only at 0x0074DCFE): plain functions here.
bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    if (!addr_check::Find(addr::cSTETerrain_LoadIni)) {
        log::Write("terrain tables_ini: no address entry for %08X; not installing",
                   static_cast<unsigned>(addr::cSTETerrain_LoadIni));
        return false;
    }
    if (!hook::Install(*addr_check::Find(addr::cSTETerrain_LoadIni), reinterpret_cast<void*>(&LoadIni), &g_original)) {
        log::Write("terrain tables_ini: the detour failed (%08X)", static_cast<unsigned>(addr::cSTETerrain_LoadIni));
        return false;
    }
    log::Write("terrain tables_ini: the INI loader %08X (detour) is ours%s", static_cast<unsigned>(addr::cSTETerrain_LoadIni),
               Global<uint8_t>(addr::cSTETerrain_StaticsDone) ? "; the game has run its loaders already" : "");
    return true;
}

const bool g_registered = registry::Register(
    {"simulators.terrain.tables_ini", "simulators/terrain", registry::Kind::Detour, registry::Phase::PostAppInit,
     addr::cSTETerrain_LoadIni,
     "Our cSTETerrain INI loader: the resource of group 0x8A5971C5, instance 0xAA597172: the texture map table and the "
     "cliff and beach textures into the terrain's tables and the list of texture IDs, the MiscTerrainTunables, "
     "WindErosion and Hydrology keys into globals (the tangents by FPTAN in naked helpers), by a detour",
     false, Install});

} // namespace
} // namespace opensc4::terrain_sim
