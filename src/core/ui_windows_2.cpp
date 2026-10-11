// Interface code at 0x00778E20 to 0x0077A480: the city tree of the import
// dialog, and the base of the dialogs that are built in code.
//
// Two classes meet here:
//
//   - the last methods of the city import dialog (vtable 0x00AB5440, the last
//     of the tables that start at 0x00AB5300; its slot 0 is a thunk that takes
//     4 off `this`, so the part these methods get may lie at +4 of the whole
//     object): its tree of regions and their cities of one size is filled by
//     slot 11 (0x00779060) with the help of 0x00778E20. The rest of the class
//     lies before this range;
//   - the base of the dialogs whose windows are put together in code instead
//     of loaded from a layout (the budget panel's department dialog and the
//     neighbour deals dialog, vtable 0x00AB5494, made by 0x007862B0; the
//     query windows from 0x0077A480 on use the same helpers). It makes its
//     window (0x00779E50), ends it (0x0077A1A0) and closes any window
//     (0x00779850), and has a set of methods that take no notice of `this`
//     and add one control each to a parent window: a control with a number
//     (0x007794E0), a text (0x00779660, 0x00779B80), a combo box
//     (0x007798C0), a text button (0x00779CA0), a picture (0x0077A250,
//     0x0077A390), and two that keep a value on a child window (0x007795A0,
//     0x00779600).
//
// Three copy routines of the 0x18-byte records these dialogs keep in vectors
// (0x00779FE0, 0x0077A030, 0x0077A080) are here too.
//
// Replacement, off by default:
//
//   ui.windows.part2: everything above.
//
// Windows, controls, fonts, colours and pictures are the game's, made and
// called through their services and vtables. Still the game's: 0x0077A120,
// the copy routine of 8-byte pairs, which other classes call as well.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace opensc4::ui {
namespace {

// The city import dialog's fields used here.
namespace importer {
constexpr std::size_t kWindow = 0x0C;
constexpr std::size_t kWidth = 0x1C;       // the city size wanted: 1, 2 or 4
constexpr std::size_t kHeight = 0x20;
constexpr std::size_t kRows = 0x24;        // a list of RowNode: one per city shown
constexpr std::size_t kFolderIcons = 0x48; // four tree pictures of a region's row, by state
constexpr std::size_t kCityIcons = 0x58;   // four of a city's row
} // namespace importer

// The built dialog's fields used here.
namespace built {
constexpr std::size_t kWindow = 0x0C;
constexpr std::size_t kKind = 0x20;   // 2: the neighbour deals
constexpr std::size_t kThird = 0x24;
constexpr std::size_t kSecond = 0x28; // for a department: amounts by month or by year
constexpr std::size_t kDeals = 0x68;  // a vector of 8-byte pairs {number, deal}
} // namespace built

constexpr uint32_t kTreeIid = 0x3AE8BAD2;
constexpr uint32_t kComboIid = 0x412CE496;
constexpr uint32_t kWinGenIid = 0x5386D516;
constexpr uint32_t kValueParam = 0x4BFEE088;
constexpr uint32_t kUiGroup = 0x46A006B0;
constexpr uint32_t kPngType = 0x856DDBAC;

// The game's string body and string.
struct StringBody {
    char* begin;
    char* end;
    char* capacity;
};

struct GameString {
    std::uintptr_t vtable;
    StringBody body;
    uint32_t zero;
};
static_assert(sizeof(GameString) == 0x14);

// The game's image holder (0x00602B00, 0x00602B70, 0x00602BE0).
struct ImageLoader {
    std::uintptr_t vtable;
    void* image;
    uint32_t key[3];
    GameString name;
    uint32_t mode;
    uint8_t flag;
};
static_assert(sizeof(ImageLoader) == 0x30);

// A node of a list of regional cities.
struct ListNode {
    ListNode* next;
    ListNode* previous;
    void* city;
};

// What the import dialog keeps for a city in its tree; the tree's row points
// at it.
struct CityRow {
    uint32_t region; // the region's number in the nation, -1 for the downloads folder
    int32_t x;       // the city's place in its region
    int32_t z;
    GameString path; // the save file, for a downloaded city only
};
static_assert(sizeof(CityRow) == 0x20);

struct RowNode {
    RowNode* next;
    RowNode* previous;
    CityRow row;
};
static_assert(sizeof(RowNode) == 0x28);

// A record of the dialogs' vectors: only these four fields are copied.
struct Entry {
    uint32_t first;
    uint32_t skipped;
    uint32_t second;
    uint32_t third;
    void* object;
    uint32_t tail;
};
static_assert(sizeof(Entry) == 0x18);

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

void AddRef(void* object)
{
    Virtual<uint32_t>(object, 0x4);
}

void Release(void* object)
{
    Virtual<uint32_t>(object, 0x8);
}

// The game's inlined reference holder: assign.
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

// The game's inlined string constructor.
void InitString(GameString& text)
{
    text.body.begin = nullptr;
    text.body.end = nullptr;
    text.body.capacity = nullptr;
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    text.body.capacity = buffer + 8;
    text.body.begin = buffer;
    text.body.end = buffer;
    *buffer = 0;
    text.vtable = addr::SC4String_Vtable;
    text.zero = 0;
}

void DestroyString(GameString& text)
{
    char* const begin = text.body.begin;
    text.vtable = addr::SC4String_Vtable;
    if (begin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(begin));
}

// A control's window.
void* Win(void* control)
{
    return Virtual<void*>(control, 0xC);
}

// The window of a control asked for anew, and one of its flags set.
void SetWinFlag(void* control, uint32_t flag, uint32_t on)
{
    Virtual<void>(Win(control), 0x110, flag, on);
}

void* Toolkit()
{
    return Cdecl<void*>(addr::SC4_Singleton913C72);
}

uint32_t Colour(uint32_t red, uint32_t green, uint32_t blue)
{
    return Virtual<uint32_t>(Cdecl<void*>(addr::SC4_Singleton913C1A), 0x88, red, green, blue);
}

// The game's 0x00778E20 (thiscall, ret 0x14). `this` is the import dialog.
// Adds a row for a region (or the downloads folder) to the tree, and under it
// a row for every city of `cities` that has the size the dialog wants. Each
// city row gets a record in the dialog's list: the region's number, the
// city's place and, when `withPath` is set, its save file.
void __fastcall AddCityRows(char* self, void*, void* tree, ListNode** cities, void* name, uint32_t region,
                            uint8_t withPath)
{
    void* const folder = Virtual<void*>(tree, 0x60, uint32_t{0}, uint32_t{0xFFFFFFFC}, name, uint32_t{0});
    for (uint32_t state = 0; state < 4; ++state)
        Virtual<void>(tree, 0x90, folder, state, At<uint32_t>(self, importer::kFolderIcons + 4 * state));

    ListNode* const head = *cities;
    for (ListNode* node = head->next; node != head; node = node->next) {
        void* const city = node->city;
        GameString title;
        InitString(title);
        uint32_t width;
        uint32_t height;
        Virtual<void>(city, 0x1C, &width, &height); // GetCitySize
        if (width == At<uint32_t>(self, importer::kWidth) && height == At<uint32_t>(self, importer::kHeight)) {
            Virtual<void>(city, 0x84, &title); // GetCityName
            CityRow row;
            InitString(row.path);
            row.region = region;
            Virtual<void>(city, 0x14, &row.x, &row.z); // GetPosition
            if (withPath)
                Virtual<void>(city, 0x7C, &row.path); // GetCitySaveFilePath

            RowNode* const sentinel = At<RowNode*>(self, importer::kRows);
            RowNode* const fresh = Cdecl<RowNode*>(addr::SC4_stl_allocate, uint32_t{0x28});
            char* const place = reinterpret_cast<char*>(fresh) + 8;
            if (place)
                This<void*>(addr::SC4UI_F778B00, place, &row);
            RowNode* const last = sentinel->previous;
            fresh->previous = last;
            fresh->next = sentinel;
            last->next = fresh;
            sentinel->previous = fresh;

            CityRow* const kept = &At<RowNode*>(self, importer::kRows)->previous->row;
            void* const item = Virtual<void*>(tree, 0x60, folder, uint32_t{0xFFFFFFFD}, &title, kept);
            for (uint32_t state = 0; state < 4; ++state)
                Virtual<void>(tree, 0x90, item, state, At<uint32_t>(self, importer::kCityIcons + 4 * state));
            DestroyString(row.path);
        }
        DestroyString(title);
    }
}

// A picture of the interface group loaded and handed to the tree; the number
// the tree gives it is stored in `slot`.
void LoadTreeIcon(void* tree, uint32_t instance, uint32_t& slot)
{
    ImageLoader loader;
    This<void*>(addr::SC4ImageLoader_ConstructParts, &loader, uint32_t{0}, kUiGroup, instance, uint32_t{1},
                uint32_t{0});
    int32_t area[4] = {0, 0, 0, 0};
    slot = Virtual<uint32_t>(tree, 0x50, loader.image, &area[0]);
    This<void>(addr::SC4ImageLoader_Destruct, &loader);
}

// The game's 0x00779060 (thiscall, no arguments; slot 11 of vtable
// 0x00AB5440). `this` is the import dialog. Fills the tree (child 0x1000):
// the size button that matches the wanted city size is set, the tree's
// pictures are loaded if they are not yet, and then every region of the
// nation gets a row with its cities of that size, and last the downloads
// folder with the city files found in it. Regions other than the current one
// are started for this and shut down again.
void __fastcall FillCityTree(char* self, void*)
{
    void* tree = nullptr;
    void* const child = Virtual<void*>(At<void*>(self, importer::kWindow), 0x8C, uint32_t{0x1000});
    Virtual<uint8_t>(child, 0x0, kTreeIid, &tree);
    if (tree) {
        // The original copies the reference to a second holder and drops both.
        AddRef(tree);
        Release(tree);
        Release(tree);
    }

    void* const nation = Virtual<void*>(Global<void*>(addr::SC4_Service43C94), 0x88); // the app's GetNation
    void* const current = Virtual<void*>(nation, 0x20);
    const uint32_t regions = Virtual<uint32_t>(nation, 0x18);

    uint32_t button = 0;
    switch (At<uint32_t>(self, importer::kWidth)) {
    case 1:
        button = 0x2000;
        break;
    case 2:
        button = 0x2001;
        break;
    case 4:
        button = 0x2002;
        break;
    default:
        break;
    }
    if (button != 0) {
        void* const size = Virtual<void*>(At<void*>(self, importer::kWindow), 0x8C, button);
        Virtual<void>(size, 0x110, uint32_t{1}, uint32_t{1});
    }

    if (At<uint32_t>(self, importer::kFolderIcons) == 0xFFFFFFFF)
        LoadTreeIcon(tree, 0x1441624A, At<uint32_t>(self, importer::kFolderIcons));
    if (At<uint32_t>(self, importer::kFolderIcons + 4) == 0xFFFFFFFF)
        LoadTreeIcon(tree, 0x1441624B, At<uint32_t>(self, importer::kFolderIcons + 4));
    if (At<uint32_t>(self, importer::kFolderIcons + 8) == 0xFFFFFFFF)
        At<uint32_t>(self, importer::kFolderIcons + 8) = At<uint32_t>(self, importer::kFolderIcons);
    if (At<uint32_t>(self, importer::kFolderIcons + 0xC) == 0xFFFFFFFF)
        At<uint32_t>(self, importer::kFolderIcons + 0xC) = At<uint32_t>(self, importer::kFolderIcons + 4);
    if (At<uint32_t>(self, importer::kCityIcons) == 0xFFFFFFFF)
        LoadTreeIcon(tree, 0x1441624C, At<uint32_t>(self, importer::kCityIcons));
    for (std::size_t at = 4; at < 0x10; at += 4) {
        if (At<uint32_t>(self, importer::kCityIcons + at) == 0xFFFFFFFF)
            At<uint32_t>(self, importer::kCityIcons + at) = At<uint32_t>(self, importer::kCityIcons);
    }

    ListNode* cities = nullptr;
    cities = Virtual<ListNode*>(NodeAllocatorAt(addr::SC4List_NodeAllocator4), 0xC, uint32_t{0xC});
    cities->next = cities;
    cities->previous = cities;

    for (uint32_t index = 0; index < regions; ++index) {
        void* const region = Virtual<void*>(nation, 0x2C, index);
        if (region != current)
            Virtual<void>(region, 0x20); // Init
        Virtual<void>(region, 0x44, &cities); // GetAllCities
        void* const name = Virtual<void*>(region, 0xC); // GetName
        AddCityRows(self, nullptr, tree, &cities, name, index, 0);
        This<void>(addr::SC4List_ClearReleasing4, &cities);
        if (region != current)
            Virtual<void>(region, 0x24); // Shutdown
    }

    GameString title;
    InitString(title);
    if (!Cdecl<uint8_t>(addr::SC4_LoadLocalizedText, &title, 0x6A231EAAu, 0x2A8F9A7Du, uint32_t{0})) {
        static const char kDownloads[] = "Downloads";
        This<void*>(addr::SC4StringBody_AssignChars, &title.body, &kDownloads[0], &kDownloads[9]);
    }

    // The nation's downloads folder, copied to a string of our own.
    void* const folder = Virtual<void*>(nation, 0x3C);
    const uint32_t length = Virtual<uint32_t>(folder, 0x1C);
    char* const chars = Virtual<char*>(folder, 0x18);
    GameString path;
    path.body.begin = nullptr;
    path.body.end = nullptr;
    path.body.capacity = nullptr;
    const uint32_t size = length + 1;
    char* end;
    if (size != 0) {
        char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, size);
        path.body.begin = buffer;
        path.body.end = buffer;
        path.body.capacity = buffer + size;
        end = buffer;
    } else {
        Cdecl<void>(addr::STL_ThrowLengthError, "basic_string");
        end = path.body.begin;
    }
    if (chars + length != chars)
        end = Cdecl<char*>(addr::SC4_memmove, end, chars, length) + length;
    path.body.end = end;
    *end = 0;
    path.vtable = addr::SC4String_Vtable;
    path.zero = 0;
    Cdecl<void>(addr::cSC4Region_ScanFolderForCities, &cities, &path);
    DestroyString(path);

    AddCityRows(self, nullptr, tree, &cities, &title, 0xFFFFFFFF, 1);

    ListNode* const head = cities;
    for (ListNode* node = head->next; node != head; node = node->next)
        Virtual<void>(node->city, 0x10); // the city's Shutdown
    DestroyString(title);
    This<void>(addr::SC4List_ClearReleasing4, &cities);
    ListNode* const sentinel = cities;
    Virtual<void>(NodeAllocatorAt(addr::SC4List_NodeAllocator4), 0x10, sentinel);
}

// The game's 0x007794E0 (thiscall that does not use `this`, ret 0x24). Adds
// a control made by the control service's slot 16 from (id, four values) to
// `parent`, 14 pixels high.
void __fastcall AddNumberControl(char*, void*, void* parent, uint32_t id, uint32_t x, uint32_t y, uint32_t width,
                                 uint32_t first, uint32_t second, uint32_t third, uint32_t fourth)
{
    void* const control = Virtual<void*>(Toolkit(), 0x40, id, first, second, third, fourth);
    SetWinFlag(control, 0x10000, 0);
    SetWinFlag(control, 4, 1);
    Virtual<void>(Win(control), 0xDC, x, y, x + width, y + 0xE);
    SetWinFlag(control, 0x800, 0);
    Virtual<void>(parent, 0x38, Win(control));
    Release(control);
}

// The game's 0x007795A0 (thiscall that does not use `this`, ret 0xC). Keeps
// `value` on the child window `id` of `window`, as its parameter 0x4BFEE088.
uint32_t __fastcall SetChildValue(char*, void*, void* window, uint32_t id, uint32_t value)
{
    void* const child = Virtual<void*>(window, 0x8C, id);
    if (!child)
        return 0;
    void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x14});
    void* const variant = memory ? This<void*>(addr::SC4ScriptNumber_FromVoidPtr, memory, value) : nullptr;
    return Virtual<uint32_t>(child, 0x1C8, kValueParam, variant);
}

// The game's 0x00779600 (thiscall that does not use `this`, ret 8). The value
// kept on the child window `id` by 0x007795A0, or 0.
uint32_t __fastcall GetChildValue(char*, void*, void* window, uint32_t id)
{
    uint32_t value = 0;
    void* const child = Virtual<void*>(window, 0x8C, id);
    if (child) {
        void* variant = window; // the original reuses the argument's slot
        if (Virtual<uint8_t>(child, 0x1C4, kValueParam, &variant)) {
            if (Virtual<uint32_t>(variant, 0xC) == 0xF)
                value = Virtual<uint32_t>(variant, 0xCC);
            Release(variant);
        }
    }
    return value;
}

// The game's 0x00779660 (thiscall that does not use `this`, ret 0x28). Adds a
// text to `parent`, sized to its caption, and returns its width. `align` 6
// puts its right edge at x, 0 its left edge; 0x63 makes it a block from x to
// the parent's width less x and down to the parent's bottom. Other values
// leave it where the service made it.
uint32_t __fastcall AddText(char*, void*, void* parent, uint32_t id, uint32_t x, uint32_t y, void* caption,
                            uint32_t align, uint32_t fontId, uint32_t red, uint32_t green, uint32_t blue)
{
    void* const text = Virtual<void*>(Toolkit(), 0x34, id, caption);
    Virtual<void>(Win(text), 0x100, id);
    void* const font = Virtual<void*>(Toolkit(), 0x14, fontId);
    Virtual<void>(text, 0x48, font);
    const uint32_t colour = Colour(red, green, blue);
    Virtual<void>(text, 0x28, colour);
    Virtual<void>(text, 0x14, uint32_t{0}, uint32_t{0}); // fit to the text

    const uint32_t width = Virtual<uint32_t>(Win(text), 0xA4);
    const uint32_t* const area = Virtual<uint32_t*>(Win(text), 0xC0);
    const uint32_t top = area[1];
    const uint32_t right = area[2];
    const uint32_t bottom = area[3];
    Virtual<void>(Win(text), 0xDC, right, top, uint32_t{0x3E8}, bottom);

    if (align == 6) {
        void* const measured = Win(text);
        void* const moved = Win(text);
        const uint32_t now = Virtual<uint32_t>(measured, 0xA4);
        Virtual<void>(moved, 0xE0, x - now, y);
    } else if (align == 0) {
        Virtual<void>(Win(text), 0xE0, x, y);
    } else if (align == 0x63) {
        void* const window = Win(text);
        const uint32_t down = Virtual<uint32_t>(parent, 0xA8) - y;
        const uint32_t across = Virtual<uint32_t>(parent, 0xA4) - 2 * x;
        Virtual<void>(window, 0xDC, x, y, across, down);
        align = 0;
        Virtual<void>(Win(text), 0xE0, x, y);
    }
    Virtual<void>(text, 0x54, align);
    SetWinFlag(text, 0x800, 0);
    SetWinFlag(text, 0x8000, 0);
    Virtual<void>(parent, 0x38, Win(text));
    Release(text);
    return width;
}

// The game's 0x00779850 (thiscall that does not use `this`, ret 8). Closes a
// window: one with flag 0x1000 gets message 0xF posted with `result`, any
// other has message 1 posted to its parent. False without a window.
uint8_t __fastcall CloseWindow(char*, void*, void* window, uint32_t result)
{
    if (!window)
        return 0;
    void* const manager = Virtual<void*>(window, 0x18);
    if (Virtual<uint8_t>(window, 0x10C, uint32_t{0x1000})) {
        Virtual<void>(manager, 0x24, uint32_t{0xF}, window, result);
        return 1;
    }
    void* const parent = Virtual<void*>(window, 0x2C);
    Virtual<void>(manager, 0x38, parent, uint32_t{1}, window, uint32_t{0}, uint32_t{0});
    return 1;
}

// The game's 0x007798C0 (thiscall that does not use `this`, ret 0x14). Adds a
// combo box (class 0x59B) of 120 by 15 pixels to `parent` in the dialogs'
// colours. The fifth argument is not used. If the class cannot be made the
// original goes on with a null pointer; so does this.
void __fastcall AddCombo(char*, void*, void* parent, uint32_t id, uint32_t x, uint32_t y, uint32_t)
{
    void* combo = nullptr;
    void* arrow = nullptr;
    void* const com = Cdecl<void*>(addr::RZGetCOM);
    if (Virtual<uint8_t>(com, 0xC, uint32_t{0x59B}, kComboIid, &combo)) {
        Virtual<void>(Win(combo), 0x100, id);
        Virtual<void>(Win(combo), 0xDC, x, y, x + 0x78, y + 0xF);
        void* const font = Virtual<void*>(Toolkit(), 0x14, 0x4A809917u);
        Virtual<void>(combo, 0x2C, font);
        uint32_t colour = Virtual<uint32_t>(Win(combo), 0x1B0, uint32_t{0x3F}, uint32_t{0x49}, uint32_t{0x67});
        Virtual<void>(combo, 0x30, uint32_t{0}, colour);
        colour = Virtual<uint32_t>(Win(combo), 0x1B0, uint32_t{0xFF}, uint32_t{0xFF}, uint32_t{0xFF});
        Virtual<void>(combo, 0x30, uint32_t{2}, colour);
        colour = Virtual<uint32_t>(Win(combo), 0x1B0, uint32_t{0x11}, uint32_t{0x22}, uint32_t{0x66});
        Virtual<void>(combo, 0x40, colour);
        void* const source = Win(combo);
        void* const filled = Win(combo);
        colour = Virtual<uint32_t>(source, 0x1B0, uint32_t{0xDE}, uint32_t{0xE8}, uint32_t{0xE3});
        Virtual<void>(filled, 0x1A8, colour);
        colour = Virtual<uint32_t>(Win(combo), 0x1B0, uint32_t{0}, uint32_t{0}, uint32_t{0});
        Virtual<void>(combo, 0x58, colour);
        SetWinFlag(combo, 0x10000, 1);
        SetWinFlag(combo, 0x800, 0);
        SetWinFlag(combo, 4, 1);
        Virtual<void>(combo, 0x54, uint32_t{0});

        void* const child = Virtual<void*>(Win(combo), 0x8C, 0x53430D98u);
        if (child) {
            AddRef(child);
            arrow = child;
        }
        void* const edit = Virtual<void*>(combo, 0x14);
        if (edit) {
            AddRef(edit);
            colour = Virtual<uint32_t>(Virtual<void*>(edit, 0x14), 0x1B0, uint32_t{0xDE}, uint32_t{0xE8},
                                       uint32_t{0xE3});
            Virtual<void>(edit, 0x54, colour);
            colour = Virtual<uint32_t>(Virtual<void*>(edit, 0x14), 0x1B0, uint32_t{0x3F}, uint32_t{0x49},
                                       uint32_t{0x67});
            Virtual<void>(edit, 0x30, uint32_t{0}, colour);
            Release(edit);
        }
    }
    Virtual<void>(parent, 0x38, Win(combo));
    if (combo) {
        void* const held = combo;
        combo = nullptr;
        Release(held);
    }
    if (arrow)
        Release(arrow);
}

// The area of a control in a given font: `width` across from (x, y) and as
// high as the font.
void SetAreaForFont(void* control, void* font, uint32_t x, uint32_t y, uint32_t width)
{
    void* const window = Win(control);
    uint32_t area[4];
    const uint32_t height = Virtual<uint32_t>(font, 0x8C);
    area[0] = x;
    area[1] = y;
    area[2] = x + width;
    area[3] = height + y;
    Virtual<void>(window, 0xD8, &area[0]);
}

// The game's 0x00779B80 (thiscall that does not use `this`, ret 0x2C). Adds a
// text of a given width and font to `parent`; returns the width.
uint32_t __fastcall AddSizedText(char*, void*, void* parent, uint32_t id, uint32_t x, uint32_t y, uint32_t width,
                                 void* caption, uint32_t align, uint32_t fontId, uint32_t red, uint32_t green,
                                 uint32_t blue)
{
    void* const font = Virtual<void*>(Toolkit(), 0x14, fontId);
    void* const text = Virtual<void*>(Toolkit(), 0x34, id, caption);
    Virtual<void>(Win(text), 0x100, id);
    const uint32_t colour = Colour(red, green, blue);
    Virtual<void>(text, 0x28, colour);
    if (font) {
        Virtual<void>(text, 0x48, font);
        SetAreaForFont(text, font, x, y, width);
    }
    Virtual<void>(text, 0x54, align);
    SetWinFlag(text, 0x800, 0);
    SetWinFlag(text, 0x8000, 0);
    Virtual<void>(parent, 0x38, Win(text));
    Release(text);
    return width;
}

// The game's 0x00779CA0 (thiscall that does not use `this`, ret 0x2C). Adds a
// button that is only its caption, with the hand cursor (cursor 0x11), to
// `parent`; returns the width.
uint32_t __fastcall AddTextButton(char*, void*, void* parent, uint32_t id, uint32_t x, uint32_t y, uint32_t width,
                                  void* caption, uint32_t style, uint32_t fontId, uint32_t red, uint32_t green,
                                  uint32_t blue)
{
    void* const font = Virtual<void*>(Toolkit(), 0x14, fontId);
    void* const button = Virtual<void*>(Toolkit(), 0x24, id, caption, uint32_t{0}, uint32_t{0}, uint32_t{0});
    Virtual<void>(Win(button), 0x100, id);
    Virtual<void>(button, 0x14, uint32_t{0x80}, uint32_t{1});
    Virtual<void>(button, 0x14, uint32_t{0x10}, uint32_t{1});
    Virtual<void>(button, 0x14, uint32_t{0x20}, uint32_t{0});
    Virtual<void>(button, 0x30, uint32_t{2}, uint32_t{0}, uint32_t{0});
    uint32_t colour = Colour(red, green, blue);
    Virtual<void>(button, 0x5C, uint32_t{0}, colour);
    colour = Colour(0, 0, 0);
    Virtual<void>(button, 0x5C, uint32_t{2}, colour);
    if (font) {
        Virtual<void>(button, 0x4C, font);
        SetAreaForFont(button, font, x, y, width);
    }
    Virtual<void>(button, 0x6C, style);

    void* const cursors = Virtual<void*>(Cdecl<void*>(addr::SC4_GetWinManager), 0x80);
    void* const window = Win(button);
    void* const cursor = Virtual<void*>(cursors, 0x24, uint32_t{0x11});
    Virtual<void>(window, 0x154, cursor, uint32_t{1});
    SetWinFlag(button, 0x800, 0);
    SetWinFlag(button, 0x8000, 0);
    Virtual<void>(parent, 0x38, Win(button));
    Release(button);
    return width;
}

// The game's 0x00779E50 (thiscall, ret 0x10). `this` is a built dialog. Makes
// the dialog's window: a plain window (class 0x4386D516) with this object as
// its window procedure and id 0x0423278F. The window goes to `*window` with a
// reference and into the dialog; the three numbers are kept for the derived
// class to build the contents from. False if the window class cannot be made.
uint8_t __fastcall MakeWindow(char* self, void*, void** window, uint32_t kind, uint32_t second, uint32_t third)
{
    void* gen = nullptr;
    *window = nullptr;
    void* const com = Cdecl<void*>(addr::RZGetCOM);
    if (!Virtual<uint8_t>(com, 0xC, uint32_t{0x4386D516}, kWinGenIid, &gen)) {
        if (gen)
            Release(gen);
        return 0;
    }

    *window = Virtual<void*>(gen, 0xC);
    AddRef(*window);
    Assign(At<void*>(self, built::kWindow), *window);

    Virtual<void>(gen, 0x18, uint32_t{1}, uint32_t{1});
    Virtual<void>(gen, 0x18, uint32_t{2}, uint32_t{0});
    Virtual<void>(gen, 0x18, uint32_t{0x10000}, uint32_t{0});
    Virtual<void>(gen, 0x18, uint32_t{0x20}, uint32_t{0});
    Virtual<void>(gen, 0x18, uint32_t{0x40}, uint32_t{0});
    Virtual<void>(gen, 0x18, uint32_t{0x1000}, uint32_t{1});
    Virtual<void>(gen, 0x18, uint32_t{0x2000}, uint32_t{0});
    Virtual<void>(gen, 0x18, uint32_t{0x4000}, uint32_t{0});
    Virtual<void>(gen, 0x1C, static_cast<void*>(self)); // SetWinProc

    Virtual<void>(*window, 0x110, uint32_t{0x100}, uint32_t{1});
    Virtual<void>(*window, 0x110, uint32_t{0x10000}, uint32_t{0});
    Virtual<void>(*window, 0x110, uint32_t{4}, uint32_t{1});
    Virtual<void>(*window, 0x110, uint32_t{0x800}, uint32_t{0});
    Virtual<void>(*window, 0x100, 0x0423278Fu);

    At<uint32_t>(self, built::kThird) = third;
    At<uint32_t>(self, built::kKind) = kind;
    At<uint32_t>(self, built::kSecond) = second;
    if (gen)
        Release(gen);
    return 1;
}

// The game's 0x00779FE0 (cdecl). Copy-constructs the records of
// [first, last) at `out`: four fields each, with a reference taken on the
// object. Returns the end of the copies.
Entry* __cdecl CopyConstructEntries(Entry* first, Entry* last, Entry* out)
{
    for (; first != last; ++first, ++out) {
        if (out) {
            out->first = first->first;
            out->second = first->second;
            out->third = first->third;
            void* const object = first->object;
            out->object = object;
            if (object)
                AddRef(object);
        }
    }
    return out;
}

// The game's 0x0077A030 (cdecl). Copy-constructs `count` records at `out`
// from one value. Returns the end of the copies.
Entry* __cdecl FillConstructEntries(Entry* out, uint32_t count, Entry* value)
{
    for (; count != 0; --count, ++out) {
        if (out) {
            out->first = value->first;
            out->second = value->second;
            out->third = value->third;
            void* const object = value->object;
            out->object = object;
            if (object)
                AddRef(object);
        }
    }
    return out;
}

// The game's 0x0077A080 (cdecl). Assigns the records of [first, last) to the
// records at `out`. Returns the end of the assigned ones.
Entry* __cdecl CopyAssignEntries(Entry* first, Entry* last, Entry* out)
{
    int32_t count = static_cast<int32_t>((reinterpret_cast<char*>(last) - reinterpret_cast<char*>(first)) / 0x18);
    if (count <= 0)
        return out;
    do {
        out->first = first->first;
        out->second = first->second;
        out->third = first->third;
        if (&first->object != &out->object)
            Assign(out->object, first->object);
        ++first;
        ++out;
    } while (--count != 0);
    return out;
}

// The game's 0x0077A1A0 (thiscall, no arguments). `this` is a built dialog.
// Ends the dialog. The neighbour deals dialog (kind 2) first cancels every
// deal of its list that has flag 1, and lets the neighbours simulator's
// interface go on and count its deals again. Then the simulator service's
// slot 13 is called and the window closed with result 0x42B7C353.
uint8_t __fastcall Finish(char* self, void*)
{
    if (At<uint32_t>(self, built::kKind) == 2) {
        void* const neighbours = Virtual<void*>(Global<void*>(addr::SC4_City), 0xF8);
        for (uint32_t index = 0;
             index < static_cast<uint32_t>((At<char*>(self, built::kDeals + 4) - At<char*>(self, built::kDeals)) >> 3);
             ++index) {
            void* const deal = At<void*>(At<char*>(self, built::kDeals), index * 8 + 4);
            if (deal)
                AddRef(deal);
            if (Virtual<uint32_t>(deal, 0x90) & 1) // GetFlags
                Virtual<void>(deal, 0x88);         // Cancel
            Release(deal);
        }
        Virtual<void>(neighbours, 0x6C, uint32_t{0}); // SetUIDisableFlag
        Virtual<void>(neighbours, 0x7C);              // MarkDealsDirty
    }
    Virtual<void>(Global<void*>(addr::SC4_SimulatorService), 0x34);
    void* const window = At<void*>(self, built::kWindow);
    if (window)
        return CloseWindow(self, nullptr, window, 0x42B7C353);
    return 1;
}

// A picture window (0xFC bytes) made for an image, placed at (x, y) in the
// image's own size. Returns the window, with one reference.
char* MakePicture(void* image, uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0xFC});
    char* const picture = memory ? This<char*>(addr::GZWinBMP_Construct, memory) : nullptr;
    AddRef(picture);
    Virtual<void>(picture, 0xDC, x, y, width + x, height + y);
    Virtual<void>(picture + 0xD8, 0x10, image);
    return picture;
}

// The game's 0x0077A250 (thiscall that does not use `this`, ret 0x14). Adds a
// picture of the interface group (type 0x856DDBAC) to `parent` at (x, y)
// with window id `id`.
void __fastcall AddPicture(char*, void*, uint32_t instance, uint32_t x, uint32_t y, void* parent, uint32_t id)
{
    ImageLoader loader;
    This<void*>(addr::SC4ImageLoader_ConstructParts, &loader, kPngType, kUiGroup, instance, uint32_t{1},
                uint32_t{0});
    const uint32_t* const area = Virtual<uint32_t*>(loader.image, 0x30);
    const uint32_t width = area[2];
    const uint32_t height = area[3];

    char* const picture = MakePicture(loader.image, x, y, width, height);
    char* const part = picture + 0xD8;
    Virtual<void>(part, 0x2C, uint32_t{4}, uint32_t{1});
    SetWinFlag(part, 0x20000, 1);
    SetWinFlag(part, 0x10000, 1);
    SetWinFlag(part, 0x800, 0);
    SetWinFlag(part, 4, 1);
    Virtual<void>(Win(part), 0x100, id);
    Virtual<void>(parent, 0x38, static_cast<void*>(picture));
    Release(picture);
    This<void>(addr::SC4ImageLoader_Destruct, &loader);
}

// The game's 0x0077A390 (thiscall that does not use `this`, ret 0x10). Adds a
// picture of the interface group to `parent` at (x, y), with no id, and
// returns its height.
uint32_t __fastcall AddPlainPicture(char*, void*, uint32_t instance, uint32_t x, uint32_t y, void* parent)
{
    uint32_t key[3] = {kPngType, kUiGroup, instance};
    ImageLoader loader;
    This<void*>(addr::SC4ImageLoader_ConstructKey, &loader, &key[0], uint32_t{1}, uint32_t{0});
    const uint32_t* const area = Virtual<uint32_t*>(loader.image, 0x30);
    const uint32_t width = area[2];
    const uint32_t height = area[3];

    char* const picture = MakePicture(loader.image, x, y, width, height);
    char* const part = picture + 0xD8;
    Virtual<void>(part, 0x2C, uint32_t{6}, uint32_t{1});
    SetWinFlag(part, 0x800, 0);
    Virtual<void>(parent, 0x38, static_cast<void*>(picture));
    Release(picture);
    This<void>(addr::SC4ImageLoader_Destruct, &loader);
    return height;
}

bool InstallWindows2(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[20] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F778E20, reinterpret_cast<void*>(&AddCityRows)},
        {addr::SC4UI_F779060, reinterpret_cast<void*>(&FillCityTree)},
        {addr::SC4UI_F7794E0, reinterpret_cast<void*>(&AddNumberControl)},
        {addr::SC4UI_F7795A0, reinterpret_cast<void*>(&SetChildValue)},
        {addr::SC4UI_F779600, reinterpret_cast<void*>(&GetChildValue)},
        {addr::SC4UI_F779660, reinterpret_cast<void*>(&AddText)},
        {addr::SC4UI_F779850, reinterpret_cast<void*>(&CloseWindow)},
        {addr::SC4UI_F7798C0, reinterpret_cast<void*>(&AddCombo)},
        {addr::SC4UI_F779B80, reinterpret_cast<void*>(&AddSizedText)},
        {addr::SC4UI_F779CA0, reinterpret_cast<void*>(&AddTextButton)},
        {addr::SC4UI_F779E50, reinterpret_cast<void*>(&MakeWindow)},
        {addr::SC4UI_F779FE0, reinterpret_cast<void*>(&CopyConstructEntries)},
        {addr::SC4UI_F77A030, reinterpret_cast<void*>(&FillConstructEntries)},
        {addr::SC4UI_F77A080, reinterpret_cast<void*>(&CopyAssignEntries)},
        {addr::SC4UI_F77A1A0, reinterpret_cast<void*>(&Finish)},
        {addr::SC4UI_F77A250, reinterpret_cast<void*>(&AddPicture)},
        {addr::SC4UI_F77A390, reinterpret_cast<void*>(&AddPlainPicture)},
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

const bool g_windows2Registered = registry::Register({
    "ui.windows.part2",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F779E50,
    "Our city tree of the import dialog and the base of the dialogs built in code: the window made and "
    "ended, texts, buttons, combo boxes and pictures added (docs/ui.md)",
    false,
    InstallWindows2,
});

} // namespace
} // namespace opensc4::ui
