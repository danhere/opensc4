// The region map window (docs/region.md), part 3: 0x007B4150 to 0x007B51D0.
// Replacement, off by default:
//
//   region.view.part3: the painting of one area of the map picture (slot 3
//     of the cell painter interface at +0xD8 of the map window), the move of
//     the map to a new view position, and the filling of a tile's panel with
//     its city's figures.
//
// The map window (cSC4WinRegionView, 0x128 bytes, window base class, main
// vtable 0x00AB9658) as far as this part shows it: +0xD8 the cell painter
// interface (vtable 0x00AB9644: QueryInterface, AddRef and Release thunks,
// then 0x007B4150), +0xDC the nation's index of the region, +0xE0 a picture
// tiled under the map (none: black), +0xE4 the picked tile record, +0xE8 and
// +0xEC the view's place, +0xF8 and +0xFC two colours (the border tint and
// the picked tile's outline), +0x100 a vector of tile record pointers,
// +0x10C the cell cache, +0x113 borders on, +0x114 names on, +0x115 the
// picked tile's name on, +0x118 the map mode.
//
// A tile record (0x80 bytes, the region screen's): +0x08 and +0x0C its cell,
// +0x10 and +0x14 its place in the map picture (floats), +0x18 the log2 of
// its side, +0x1C its picture, +0x24 its second picture (map mode 1), +0x2C
// its map picture, +0x30 its panel window, +0x34 whether the map picture is
// made, +0x38 the runs of the map picture, +0x50 and +0x5C runs of its mask,
// +0x68 and +0x6C its centre, +0x74 a vector of 12-byte marks (x, y,
// picture).
//
// The painter draws into the picture it is handed: the background, every
// tile's map picture (made on first use by the two passes of part 2), the
// names of the cities (two fonts of the text style service 0xC2C2EB0F,
// shortened with an ellipsis to four thirds of the tile's width), the marks
// of the map modes through a draw context (class 0x0AE6320E), and the
// outline of the picked tile. Fonts, pictures, the draw context and the
// windows are the game's, called through their vtables; the pixel passes
// (0x007B2A30, 0x007B2DD0, 0x007B3300, 0x007B3670) are other parts'.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace opensc4::region {
namespace {

namespace view { // the map window, by offset
constexpr std::size_t kPainter = 0xD8;
constexpr std::size_t kRegionIndex = 0xDC;
constexpr std::size_t kBackground = 0xE0;
constexpr std::size_t kPicked = 0xE4;
constexpr std::size_t kViewX = 0xE8;
constexpr std::size_t kViewY = 0xEC;
constexpr std::size_t kBorderColour = 0xF8;
constexpr std::size_t kOutlineColour = 0xFC;
constexpr std::size_t kRecords = 0x100;
constexpr std::size_t kRecordsEnd = 0x104;
constexpr std::size_t kCache = 0x10C;
constexpr std::size_t kBorders = 0x113;
constexpr std::size_t kNames = 0x114;
constexpr std::size_t kPickedName = 0x115;
constexpr std::size_t kMode = 0x118;
} // namespace view

namespace tile { // a tile record of the region screen
constexpr std::size_t kX = 0x08;
constexpr std::size_t kY = 0x0C;
constexpr std::size_t kMapX = 0x10;
constexpr std::size_t kMapY = 0x14;
constexpr std::size_t kSideLog2 = 0x18;
constexpr std::size_t kPicture = 0x1C;
constexpr std::size_t kSecondPicture = 0x24;
constexpr std::size_t kMapPicture = 0x2C;
constexpr std::size_t kPanel = 0x30;
constexpr std::size_t kMapPictureMade = 0x34;
constexpr std::size_t kMapRuns = 0x38;
constexpr std::size_t kBorderRuns = 0x50;
constexpr std::size_t kOutlineRuns = 0x5C;
constexpr std::size_t kCentreX = 0x68;
constexpr std::size_t kCentreY = 0x6C;
constexpr std::size_t kMarks = 0x74;
constexpr std::size_t kMarksEnd = 0x78;
} // namespace tile

namespace image { // the game's image buffer, vtable offsets
constexpr std::size_t kWidth = 0x24;
constexpr std::size_t kHeight = 0x28;
constexpr std::size_t kArea = 0x30;      // -> int32_t[4]: left, top, right, bottom
constexpr std::size_t kFill = 0x48;      // (colour, area or null)
constexpr std::size_t kMakeColour = 0x78; // (red, green, blue)
} // namespace image

namespace font { // the game's font, vtable offsets
constexpr std::size_t kLineHeight = 0x8C;
constexpr std::size_t kTextWidth = 0xC0;    // (text, length)
constexpr std::size_t kFitEllipsis = 0xC8;  // (string, ellipsis, area, 0)
constexpr std::size_t kDrawLine = 0xCC;     // (target, x, y, text, length, colour, flags, 0, 0)
} // namespace font

namespace win { // the window base class, vtable offsets
constexpr std::size_t kChildByID = 0x8C;
constexpr std::size_t kChildAs = 0x94; // (id, interface, out)
constexpr std::size_t kWidth = 0xA4;
constexpr std::size_t kHeight = 0xA8;
constexpr std::size_t kMoveTo = 0xE0;  // (x, y)
constexpr std::size_t kSetFlag = 0x110; // (flag, on)
constexpr std::size_t kShow = 0x114;
constexpr std::size_t kHide = 0x118;
constexpr std::size_t kSetCaption = 0x128;
} // namespace win

namespace city { // the regional city, vtable offsets
constexpr std::size_t kPopulation = 0x28;
constexpr std::size_t kCommercialJobs = 0x2C;
constexpr std::size_t kIndustrialJobs = 0x30;
constexpr std::size_t kMayorRating = 0x38;
constexpr std::size_t kDifficulty = 0x3C;
constexpr std::size_t kGetName = 0x84;
constexpr std::size_t kGetMayorName = 0x8C;
constexpr std::size_t kEstablished = 0xAC;
constexpr std::size_t kBudget = 0xC8;
constexpr std::size_t kTutorialId = 0x104;
constexpr std::size_t kIsTutorial = 0x10C;
} // namespace city

constexpr uint32_t kTextStyles = 0xC2C2EB0F;
constexpr uint32_t kTextStylesIid = 0x22C2EB1F;
constexpr uint32_t kNameStyleBase = 0xC54664C2;
constexpr uint32_t kMayorStyleBase = 0x8A8CC985;
constexpr uint32_t kEllipsisText = 0x5377BE31;
constexpr uint32_t kDrawContext = 0x0AE6320E;
constexpr uint32_t kDrawContextIid = 0x2AE63219;

// The game's string.
struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};
static_assert(sizeof(GameString) == 0x14);

// The game's picture loader (0x00602B70 fills it, 0x00602BE0 empties it).
struct ImageLoader {
    std::uintptr_t vtable;
    void* image;
    uint32_t key[3];
    GameString name;
    uint32_t mode;
    uint8_t flag;
};
static_assert(sizeof(ImageLoader) == 0x30);

// A mark of the map modes on a tile.
struct Mark {
    int32_t x;
    int32_t y;
    void* picture;
};
static_assert(sizeof(Mark) == 0xC);

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

uint32_t Free(void* memory)
{
    return Cdecl<uint32_t>(addr::SC4_Free, memory);
}

void AddRef(void* object)
{
    Virtual<uint32_t>(object, 0x4);
}

void Release(void* object)
{
    Virtual<uint32_t>(object, 0x8);
}

// The game's inlined empty string: an 8-byte buffer with a terminator.
void InitString(GameString& text)
{
    text.begin = nullptr;
    text.end = nullptr;
    text.capacity = nullptr;
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    text.begin = buffer;
    text.end = buffer;
    text.capacity = buffer + 8;
    *buffer = 0;
    text.vtable = addr::SC4String_Vtable;
    text.zero = 0;
}

void DestroyString(GameString& text)
{
    char* const buffer = text.begin;
    text.vtable = addr::SC4String_Vtable;
    if (buffer)
        Free(buffer);
}

uint32_t Length(const GameString& text)
{
    return static_cast<uint32_t>(text.end - text.begin);
}

// The game's float to integer conversion (0x009EEF04): truncation.
int32_t Truncate(double value)
{
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

double Floor(double value)
{
    return Cdecl<double>(addr::CRT_floor, value);
}

const int32_t* Area(void* picture)
{
    return Virtual<const int32_t*>(picture, image::kArea);
}

// The place of a tile's picture in the view, worked out as 0x007B3030 does
// (inlined twice in 0x007B4150): the left edge from the floor of the tile's
// place, the top from the floor of its place (through a float) less the
// picture's height.
void TilePlace(char* window, char* record, int32_t& x, int32_t& y)
{
    x = Truncate(Floor(static_cast<double>(At<float>(record, tile::kMapX))) -
                 static_cast<double>(At<int32_t>(window, view::kViewX)));
    void* const picture = At<void*>(record, tile::kPicture);
    const float top = static_cast<float>(Floor(static_cast<double>(At<float>(record, tile::kMapY))) -
                                         static_cast<double>(At<int32_t>(window, view::kViewY)));
    const uint32_t height = Virtual<uint32_t>(picture, image::kHeight);
    y = Truncate(static_cast<double>(top) - static_cast<double>(height));
}

// ---------------------------------------------------------------------------
// The pixel passes of the other parts, which take arguments in registers.
// ---------------------------------------------------------------------------

// The game's 0x007B3300 takes the source picture in EAX and the map picture,
// the border runs (or null) and the border tint on the stack (caller cleans).
void* g_copyAndTint = reinterpret_cast<void*>(addr::cSC4WinRegionView_CopyAndTint);

__declspec(naked) void __cdecl CopyAndTint(void* /*source*/, void* /*mapPicture*/, void* /*runs*/, uint32_t /*tint*/)
{
    __asm mov eax, [esp + 4]
    __asm push dword ptr [esp + 16]
    __asm push dword ptr [esp + 16]
    __asm push dword ptr [esp + 16]
    __asm call dword ptr [g_copyAndTint]
    __asm add esp, 12
    __asm ret
}

// The game's 0x007B2A30 takes the source picture's area in ECX, the place in
// the target in EAX and the target in EDI, and the source picture and its
// runs on the stack (caller cleans).
void* g_putRuns = reinterpret_cast<void*>(addr::cSC4WinRegionView_BlendRuns);

__declspec(naked) void __cdecl PutRuns(const int32_t* /*sourceArea*/, const int32_t* /*place*/, void* /*target*/,
                                       void* /*source*/, void* /*runs*/)
{
    __asm push edi
    __asm mov ecx, [esp + 8]
    __asm mov eax, [esp + 12]
    __asm mov edi, [esp + 16]
    __asm push dword ptr [esp + 24]
    __asm push dword ptr [esp + 24]
    __asm call dword ptr [g_putRuns]
    __asm add esp, 8
    __asm pop edi
    __asm ret
}

// The game's 0x007B2DD0 takes the place in the target in EAX and the target
// in ESI, and the runs and a colour on the stack (caller cleans).
void* g_colourRuns = reinterpret_cast<void*>(addr::cSC4WinRegionView_TintRuns);

__declspec(naked) void __cdecl ColourRuns(const int32_t* /*place*/, void* /*target*/, void* /*runs*/,
                                          uint32_t /*colour*/)
{
    __asm push esi
    __asm mov eax, [esp + 8]
    __asm mov esi, [esp + 12]
    __asm push dword ptr [esp + 20]
    __asm push dword ptr [esp + 20]
    __asm call dword ptr [g_colourRuns]
    __asm add esp, 8
    __asm pop esi
    __asm ret
}

// ---------------------------------------------------------------------------
// The painter.
// ---------------------------------------------------------------------------

uint32_t RecordCount(char* window)
{
    const std::uintptr_t bytes =
        At<std::uintptr_t>(window, view::kRecordsEnd) - At<std::uintptr_t>(window, view::kRecords);
    return static_cast<uint32_t>(static_cast<int32_t>(bytes) >> 2);
}

// A city's name and its mayor's name, centred over the tile's centre, the
// mayor's under the city's. The city's holder is the region's.
void DrawNames(char* record, void** holder, void* target, int32_t x, int32_t y)
{
    void* styles = nullptr;
    if (void* const framework = Cdecl<void*>(addr::RZGetFramework))
        Virtual<uint8_t>(framework, 0x14, kTextStyles, kTextStylesIid, &styles);

    const int32_t centreX = At<int32_t>(record, tile::kCentreX) + x;
    const int32_t baseY = At<int32_t>(record, tile::kCentreY) + y;

    // Like the original this does not survive a missing style service.
    void* const nameFont = Virtual<void*>(
        styles, 0x14, uint32_t{(kNameStyleBase - uint32_t{At<uint8_t>(record, tile::kSideLog2)}) << 1});
    void* const mayorFont = Virtual<void*>(
        styles, 0x14, uint32_t{kMayorStyleBase - (uint32_t{At<uint8_t>(record, tile::kSideLog2)} << 1)});

    Virtual<uint32_t>(target, image::kMakeColour, uint32_t{0x3C}, uint32_t{0x53}, uint32_t{0x8C}); // not used
    const uint32_t colour =
        Virtual<uint32_t>(target, image::kMakeColour, uint32_t{0xDE}, uint32_t{0xE8}, uint32_t{0xE3});

    GameString name;
    InitString(name);
    GameString mayor;
    InitString(mayor);
    Virtual<void>(*holder, city::kGetName, &name);
    Virtual<void>(*holder, city::kGetMayorName, &mayor);

    // A tutorial city whose tutorial is flagged shows no mayor.
    void* const regionalCity = *holder;
    void* const tutorials = Virtual<void*>(Global<void*>(addr::SC4_Service43C94), 0xC4);
    if (Virtual<uint8_t>(regionalCity, city::kIsTutorial)) {
        const uint32_t id = Virtual<uint32_t>(regionalCity, city::kTutorialId);
        char* const tutorial = This<char*>(addr::cSC4TutorialRegistry_Find, tutorials, id);
        if (tutorial && At<uint8_t>(tutorial, 0x10))
            Virtual<void>(&mayor, 0x3C, uint32_t{0}); // Resize
    }

    const int32_t* const area = Area(At<void*>(record, tile::kPicture));
    int32_t fit[4];
    const int32_t fitWidth = static_cast<int32_t>(static_cast<uint32_t>(area[2] - area[0]) << 2) / 3;
    const int32_t fitHeight = Virtual<int32_t>(nameFont, font::kLineHeight);
    fit[0] = 0;
    fit[1] = 0;
    fit[2] = fitWidth;
    fit[3] = fitHeight;
    void* const ellipsis = Virtual<void*>(styles, 0x8C, kEllipsisText);
    Virtual<uint32_t>(nameFont, font::kFitEllipsis, &name, ellipsis, static_cast<int32_t*>(fit), uint32_t{0});
    Virtual<uint32_t>(mayorFont, font::kFitEllipsis, &mayor, ellipsis, static_cast<int32_t*>(fit), uint32_t{0});

    const int32_t nameX = centreX - Virtual<int32_t>(nameFont, font::kTextWidth, name.begin, Length(name)) / 2;
    const int32_t mayorX = centreX - Virtual<int32_t>(mayorFont, font::kTextWidth, mayor.begin, Length(mayor)) / 2;
    const int32_t nameY = baseY - Virtual<int32_t>(nameFont, font::kLineHeight);
    const int32_t mayorY = Virtual<int32_t>(mayorFont, font::kLineHeight) + nameY;

    Virtual<uint8_t>(nameFont, font::kDrawLine, target, nameX, nameY, name.begin, Length(name), colour,
                     uint32_t{0x8000000}, uint32_t{0}, uint32_t{0});
    Virtual<uint8_t>(mayorFont, font::kDrawLine, target, mayorX, mayorY, mayor.begin, Length(mayor), colour,
                     uint32_t{0x8000000}, uint32_t{0}, uint32_t{0});

    DestroyString(mayor);
    DestroyString(name);
    This<void>(addr::SC4ServiceHolder_Release, &styles); // releases the service
}

// The marks of a tile, each centred on its point, through a draw context.
void DrawMarks(char* record, void* target, int32_t x, int32_t y)
{
    void* context = nullptr;
    void* const com = Cdecl<void*>(addr::RZGetCOM);
    Virtual<uint8_t>(com, 0xC, kDrawContext, kDrawContextIid, &context);
    // Like the original this does not survive a missing draw context.
    Virtual<void>(context, 0x44, target);
    Virtual<void>(context, 0x54, uint32_t{0xFFFFFFFF});
    Virtual<void>(context, 0x5C, uint32_t{1});
    Virtual<void>(context, 0x58, uint32_t{1});

    Mark* const end = At<Mark*>(record, tile::kMarksEnd);
    for (Mark* mark = At<Mark*>(record, tile::kMarks); mark != end; ++mark) {
        const int32_t* const area = Area(mark->picture);
        int32_t source[4];
        source[0] = area[0];
        source[1] = area[1];
        source[2] = area[2];
        source[3] = area[3];
        const int32_t shiftY = mark->y - (source[3] - source[1]) / 2 - y;
        const int32_t shiftX = mark->x - (source[2] - source[0]) / 2 - x;
        int32_t place[4];
        place[0] = source[0] + shiftX;
        place[1] = source[1] + shiftY;
        place[2] = source[2] + shiftX;
        place[3] = source[3] + shiftY;
        Virtual<void>(context, 0x98, mark->picture, static_cast<int32_t*>(source), static_cast<int32_t*>(place));
    }

    if (context)
        Release(context);
}

// The game's 0x007B4150, slot 3 of the cell painter interface (this is the
// map window + 0xD8; the target picture and the place of its corner in the
// map, ret 0xC): paints that area of the map into the picture.
void __fastcall PaintArea(char* self, void*, void* target, int32_t x, int32_t y)
{
    char* const window = self - view::kPainter;

    if (void* const background = At<void*>(window, view::kBackground)) {
        const int32_t shiftY = 0 - (At<int32_t>(window, view::kViewY) + y);
        const int32_t shiftX = 0 - (At<int32_t>(window, view::kViewX) + x);
        const int32_t* const targetArea = Area(target);
        const int32_t* const backgroundArea = Area(background);
        Cdecl<uint8_t>(addr::GZBuffer_TileBlit, target, background, backgroundArea, targetArea, shiftX, shiftY,
                       uint32_t{0});
    } else {
        const uint32_t black = Virtual<uint32_t>(target, image::kMakeColour, uint32_t{0}, uint32_t{0}, uint32_t{0});
        Virtual<uint8_t>(target, image::kFill, black, uint32_t{0});
    }

    // The tiles' map pictures.
    for (uint32_t index = 0; index < RecordCount(window); ++index) {
        char* const record = At<char**>(window, view::kRecords)[index];
        int32_t left;
        int32_t top;
        This<void>(addr::cSC4WinRegionView_TilePlace, window, record, &left, &top);
        left += At<int32_t>(window, view::kViewX) - x;
        top += At<int32_t>(window, view::kViewY) - y;

        void* const picture = At<void*>(record, tile::kPicture);
        int32_t place[4];
        const int32_t bottom = Virtual<int32_t>(picture, image::kHeight) + top;
        place[1] = top;
        place[0] = left;
        place[2] = Virtual<int32_t>(picture, image::kWidth) + left;
        place[3] = bottom;

        // The original tests the picture's own area, not its place, against
        // the target's.
        const int32_t* const area = Area(At<void*>(record, tile::kPicture));
        int32_t common[4];
        common[0] = area[0];
        common[1] = area[1];
        common[2] = area[2];
        common[3] = area[3];
        const int32_t* const targetArea = Area(target);
        if (!This<uint8_t>(addr::SC4UI_F79DD60, static_cast<int32_t*>(common),
                           static_cast<int32_t*>(common), targetArea))
            continue;
        void* const mapPicture = At<void*>(record, tile::kMapPicture);
        if (!mapPicture)
            continue;

        if (!At<uint8_t>(record, tile::kMapPictureMade)) {
            void* const runs =
                At<uint8_t>(window, view::kBorders) ? static_cast<void*>(record + tile::kBorderRuns) : nullptr;
            void** source = &At<void*>(record, tile::kPicture);
            if (At<int32_t>(window, view::kMode) == 1 && At<void*>(record, tile::kSecondPicture))
                source = &At<void*>(record, tile::kSecondPicture);
            CopyAndTint(*source, mapPicture, runs, At<uint32_t>(window, view::kBorderColour));
            Cdecl<void>(addr::cSC4WinRegionView_OutlineAndCollect, At<void*>(record, tile::kMapPicture),
                        static_cast<void*>(record + tile::kMapRuns));
            At<uint8_t>(record, tile::kMapPictureMade) = 1;
        }

        void* const made = At<void*>(record, tile::kMapPicture);
        const int32_t* const madeArea = Area(made);
        PutRuns(madeArea, place, target, made, record + tile::kMapRuns);
    }

    // The names and the marks.
    for (uint32_t index = 0; index < RecordCount(window); ++index) {
        char* const record = At<char**>(window, view::kRecords)[index];
        int32_t left;
        int32_t top;
        TilePlace(window, record, left, top);
        left += At<int32_t>(window, view::kViewX) - x;
        top += At<int32_t>(window, view::kViewY) - y;

        const int32_t* const area = Area(At<void*>(record, tile::kPicture));
        int32_t place[4];
        const int32_t shiftX = left - area[0];
        const int32_t shiftY = top - area[1];
        place[0] = area[0] + shiftX;
        place[1] = area[1] + shiftY;
        place[2] = area[2] + shiftX;
        place[3] = area[3] + shiftY;
        const int32_t* const targetArea = Area(target);
        if (!This<uint8_t>(addr::SC4UI_F79DD60, static_cast<int32_t*>(place),
                           static_cast<int32_t*>(place), targetArea))
            continue;

        void* const nation = Virtual<void*>(Global<void*>(addr::SC4_Service43C94), 0x88); // GetNation
        void* const region = Virtual<void*>(nation, 0x2C, At<uint32_t>(window, view::kRegionIndex));
        void** const holder =
            Virtual<void**>(region, 0x2C, At<int32_t>(record, tile::kX), At<int32_t>(record, tile::kY)); // GetCity
        if (!holder)
            continue;

        // A tutorial city always has its name shown; another one when it is
        // established and names are on. The picked tile has its own switch.
        const uint8_t tutorial = Virtual<uint8_t>(*holder, city::kIsTutorial);
        bool named = tutorial || Virtual<uint8_t>(*holder, city::kEstablished);
        if (named && record == At<char*>(window, view::kPicked) && !At<uint8_t>(window, view::kPickedName))
            named = false;
        if (named && !tutorial && !At<uint8_t>(window, view::kNames))
            named = false;
        if (named)
            DrawNames(record, holder, target, left, top);

        if (At<int32_t>(window, view::kMode) != 0)
            DrawMarks(record, target, x, y);
    }

    // The outline of the picked tile.
    if (char* const picked = At<char*>(window, view::kPicked)) {
        int32_t left;
        int32_t top;
        TilePlace(window, picked, left, top);
        left += At<int32_t>(window, view::kViewX) - x;
        top += At<int32_t>(window, view::kViewY) - y;

        void* const picture = At<void*>(At<char*>(window, view::kPicked), tile::kPicture);
        int32_t place[4];
        const int32_t bottom = Virtual<int32_t>(picture, image::kHeight) + top;
        place[1] = top;
        place[0] = left;
        const int32_t width = Virtual<int32_t>(picture, image::kWidth);
        const uint32_t colour = At<uint32_t>(window, view::kOutlineColour);
        place[2] = width + left;
        place[3] = bottom;
        ColourRuns(place, target, At<char*>(window, view::kPicked) + tile::kOutlineRuns, colour);
    }
}

// ---------------------------------------------------------------------------
// The tiles' panels.
// ---------------------------------------------------------------------------

// A tile's panel put over its tile: centred on the middle of the tile's
// picture, its bottom on the picture's middle row. Shared by the two
// functions below, which have it inlined.
void PlacePanel(char* window, char* record, void* panel)
{
    int32_t left;
    int32_t top;
    This<void>(addr::cSC4WinRegionView_TilePlace, window, record, &left, &top);
    const int32_t* const first = Area(At<void*>(record, tile::kPicture));
    const int32_t right = first[2] - first[0] + left;
    const int32_t* const second = Area(At<void*>(record, tile::kPicture));
    const int32_t bottom = second[3] - second[1] + top;
    const int32_t middleX = (right + left) / 2;
    const int32_t middleY = (top + bottom) / 2;

    const int32_t y = middleY - Virtual<int32_t>(panel, win::kHeight);
    const int32_t x = middleX - Virtual<int32_t>(panel, win::kWidth) / 2;
    Virtual<uint8_t>(panel, win::kMoveTo, x, y);
}

// The game's 0x007B4A60 (this is the map window; the view's new place, ret
// 8): stores the place, moves every tile's panel with it and has the cell
// cache follow. Nothing happens when the place is the old one.
void __fastcall SetViewPlace(char* self, void*, int32_t x, int32_t y)
{
    if (At<int32_t>(self, view::kViewX) == x && At<int32_t>(self, view::kViewY) == y)
        return;
    At<int32_t>(self, view::kViewX) = x;
    char** const begin = At<char**>(self, view::kRecords);
    At<int32_t>(self, view::kViewY) = y;
    char** const end = At<char**>(self, view::kRecordsEnd);

    for (char** slot = begin; slot != end; ++slot) {
        char* const record = *slot;
        if (!At<void*>(record, tile::kPanel))
            continue;
        // The original reads the panel again after working out the place.
        int32_t left;
        int32_t top;
        This<void>(addr::cSC4WinRegionView_TilePlace, self, record, &left, &top);
        const int32_t* const first = Area(At<void*>(record, tile::kPicture));
        const int32_t right = first[2] - first[0] + left;
        const int32_t* const second = Area(At<void*>(record, tile::kPicture));
        const int32_t bottom = second[3] - second[1] + top;
        void* const panel = At<void*>(record, tile::kPanel);
        const int32_t middleX = (right + left) / 2;
        const int32_t middleY = (top + bottom) / 2;
        const int32_t panelY = middleY - Virtual<int32_t>(panel, win::kHeight);
        const int32_t panelX = middleX - Virtual<int32_t>(panel, win::kWidth) / 2;
        Virtual<uint8_t>(panel, win::kMoveTo, panelX, panelY);
    }

    // Like the original this does not survive a missing cell cache.
    This<void>(addr::SC4RegionViewGrid_MoveTo, At<void*>(self, view::kCache), At<int32_t>(self, view::kViewX),
               At<int32_t>(self, view::kViewY));
}

// A window flag set from a comparison. The original pushes the whole
// register the comparison's byte is in; the upper bytes are passed on by the
// window, so ours has the same ones.
void SetFlagWord(void* window, uint32_t flag, uint32_t on)
{
    Virtual<uint8_t>(window, win::kSetFlag, flag, on);
}

// What `neg eax; sbb al, al; inc al` leaves in EAX: 1 for zero, otherwise
// the upper three bytes of the negated value over a zero byte.
uint32_t IsZeroWord(uint32_t value)
{
    return value == 0 ? uint32_t{1} : ((0 - value) & 0xFFFFFF00);
}

// The game's 0x007B4B80 (this is the map window; a tile record and the
// region's holder of its city or null, ret 8): puts the tile's panel in
// place and fills it. With an established city: the names, the difficulty
// marks, the budget (in the debt control when negative and there is one),
// population and jobs, and the mayor rating's meter. Otherwise the price
// from the tuning exemplar (10,000 when it is missing).
void __fastcall FillPanel(char* self, void*, char* record, void** holder)
{
    double meterValue = 0.5;
    void* const panel = At<void*>(record, tile::kPanel);
    // Like the original this does not survive a record without a panel.
    PlacePanel(self, record, panel);

    if (holder && Virtual<uint8_t>(*holder, city::kEstablished)) {
        void* const regionalCity = *holder;
        if (regionalCity)
            AddRef(regionalCity);

        GameString name;
        InitString(name);
        GameString mayor;
        InitString(mayor);
        // A third string the original never uses: only its buffer is left
        // of it.
        char* const unused = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
        *unused = 0;

        Virtual<void>(regionalCity, city::kGetName, &name);
        Virtual<void>(regionalCity, city::kGetMayorName, &mayor);
        This<void>(addr::cSC4WinRegionView_BlankFlaggedTutorialText, self, regionalCity, &mayor);

        if (void* const control = Virtual<void*>(panel, win::kChildByID, uint32_t{0x4A552000}))
            Virtual<uint8_t>(control, win::kSetCaption, &name);
        if (void* const control = Virtual<void*>(panel, win::kChildByID, uint32_t{0x4A552001}))
            Virtual<uint8_t>(control, win::kSetCaption, &mayor);

        // One mark per difficulty.
        if (void* const control = Virtual<void*>(panel, win::kChildByID, uint32_t{0x6C06F4A0})) {
            const uint32_t difficulty = Virtual<uint32_t>(regionalCity, city::kDifficulty);
            SetFlagWord(control, 1, IsZeroWord(difficulty));
        }
        if (void* const control = Virtual<void*>(panel, win::kChildByID, uint32_t{0xAC06F4C4})) {
            const uint32_t difficulty = Virtual<uint32_t>(regionalCity, city::kDifficulty);
            SetFlagWord(control, 1, IsZeroWord(difficulty - 1));
        }
        if (void* const control = Virtual<void*>(panel, win::kChildByID, uint32_t{0xCC06F4CF})) {
            const uint32_t difficulty = Virtual<uint32_t>(regionalCity, city::kDifficulty);
            // `sete cl` on the ECX the getter (0x0080BC00) leaves: the city.
            const uint32_t upper = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(regionalCity)) & 0xFFFFFF00;
            const uint32_t word = upper | (difficulty == 2 ? 1u : 0u);
            SetFlagWord(control, 1, word);
        }

        if (void* const budgetText = Virtual<void*>(panel, win::kChildByID, uint32_t{0x4A552002})) {
            void* const debtText = Virtual<void*>(panel, win::kChildByID, uint32_t{0x4A552006});
            GameString text;
            This<void*>(addr::SC4String_SC4String, &text);
            const double budget = Virtual<double>(regionalCity, city::kBudget);
            Cdecl<void>(addr::SC4_FormatMoney, static_cast<int64_t>(budget), &text);
            void* shown = budgetText;
            if (budget < 0.0) {
                if (debtText) {
                    Virtual<void>(budgetText, win::kHide);
                    Virtual<uint8_t>(debtText, win::kSetCaption, &text);
                    shown = debtText;
                } else {
                    Virtual<uint8_t>(budgetText, win::kSetCaption, &text);
                }
            } else {
                if (debtText)
                    Virtual<void>(debtText, win::kHide);
                Virtual<uint8_t>(budgetText, win::kSetCaption, &text);
            }
            Virtual<void>(shown, win::kShow);
            DestroyString(text);
        }

        void* const formatter = Virtual<void*>(Cdecl<void*>(addr::SC4_CurrentApp), 0x98, uint32_t{0});
        GameString number;
        InitString(number);
        if (void* const control = Virtual<void*>(panel, win::kChildByID, uint32_t{0x4A552003})) {
            const int32_t value = Virtual<int32_t>(regionalCity, city::kPopulation);
            Virtual<void>(formatter, 0x48, static_cast<int64_t>(value), &number);
            Virtual<uint8_t>(control, win::kSetCaption, &number);
        }
        if (void* const control = Virtual<void*>(panel, win::kChildByID, uint32_t{0x4A552004})) {
            const int32_t value = Virtual<int32_t>(regionalCity, city::kCommercialJobs);
            Virtual<void>(formatter, 0x48, static_cast<int64_t>(value), &number);
            Virtual<uint8_t>(control, win::kSetCaption, &number);
        }
        if (void* const control = Virtual<void*>(panel, win::kChildByID, uint32_t{0x4A552005})) {
            const int32_t value = Virtual<int32_t>(regionalCity, city::kIndustrialJobs);
            Virtual<void>(formatter, 0x48, static_cast<int64_t>(value), &number);
            Virtual<uint8_t>(control, win::kSetCaption, &number);
        }

        const int32_t rating = static_cast<int8_t>(Virtual<uint8_t>(regionalCity, city::kMayorRating));
        meterValue = static_cast<double>(rating) * 0.005 + 0.5;

        DestroyString(number);
        Free(unused);
        DestroyString(mayor);
        DestroyString(name);
        Release(regionalCity);
    } else {
        int32_t price = 10000;
        void* exemplar = nullptr;
        uint32_t key[3] = {0x6534284A, 0xE7E2C2DB, 0xC8E8F6C5};
        if (void* const resources = Global<void*>(addr::SC4_ResourceManager)) {
            if (Virtual<uint8_t>(resources, 0xC, static_cast<uint32_t*>(key), uint32_t{0xA52160F5}, &exemplar,
                                 uint32_t{0}, uint32_t{0})) {
                void* const properties = Virtual<void*>(exemplar, 0x1C);
                Cdecl<uint8_t>(addr::SC4_GetPropertySint32, properties, uint32_t{0x6A9C7718}, &price);
            }
        }

        GameString text;
        InitString(text);
        if (void* const control = Virtual<void*>(panel, win::kChildByID, uint32_t{0x4A552002})) {
            Cdecl<void>(addr::SC4_FormatMoney, static_cast<int64_t>(price), &text);
            Virtual<uint8_t>(control, win::kSetCaption, &text);
        }
        if (!holder) {
            if (void* const control = Virtual<void*>(panel, win::kChildByID, uint32_t{0x4A560003}))
                Virtual<uint8_t>(control, win::kSetFlag, uint32_t{2}, uint32_t{0});
        }
        DestroyString(text);
        if (exemplar)
            Release(exemplar);
    }

    void* meter = nullptr;
    if (Virtual<uint8_t>(panel, win::kChildAs, uint32_t{0x4A553000}, uint32_t{0x4A5D1208}, &meter)) {
        Virtual<void>(meter, 0x10, meterValue);
        ImageLoader loader;
        This<void*>(addr::SC4ImageLoader_ConstructParts, &loader, uint32_t{0}, uint32_t{0x46A006B0},
                    uint32_t{0x14416327}, uint32_t{9}, uint32_t{0});
        if (loader.image)
            Virtual<void>(meter, 0xC, loader.image);
        This<void>(addr::SC4ImageLoader_Destruct, &loader);
    }
    if (meter)
        Release(meter);
}

bool InstallRegionView3(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[4] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSC4WinRegionView_PaintArea, reinterpret_cast<void*>(&PaintArea)},
        {addr::cSC4WinRegionView_SetViewPlace, reinterpret_cast<void*>(&SetViewPlace)},
        {addr::cSC4WinRegionView_FillPanel, reinterpret_cast<void*>(&FillPanel)},
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

const bool g_regionView3Registered = registry::Register({
    "region.view.part3",
    "region",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4WinRegionView_PaintArea,
    "Our region map window, part 3: the painting of an area of the map picture (background, tiles, names, marks, "
    "the picked tile's outline), the move to a new view position and the filling of a tile's panel",
    false,
    InstallRegionView3,
});

} // namespace
} // namespace opensc4::region
