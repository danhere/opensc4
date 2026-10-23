// The stock widgets' UI script readers (docs/gzui.md, part u13). Every
// LEGACY reader's constructor (0x0095B8C4) has each stock widget class
// register itself when it makes the shared variable table: the class's
// names and its property words go into the table (its slot 3, a name and a
// word), the class's property handler into the handler map (0x00B6358C,
// interface to handler) and its interface into the interface map
// (0x00B63598, class to interface). The dispatcher (0x00957A9B) finds a
// script's class and calls the handler with the window and its properties.
// Replacements, each off by default:
//
//   framework.uiscript.widgets    the 16 registrations: TextTicker
//                                 0x00951269, Folders 0x009518A8, Custom
//                                 0x0095195E, FlatRect 0x00951A14, Text
//                                 0x00951C9F, TextEdit 0x00952046, ListBox
//                                 0x00952645, Combo 0x00952868, Btn
//                                 0x009529EF, BMP 0x00953056, Slider
//                                 0x009531A9, Scrollbar 0x00953330,
//                                 LineInput 0x00953483, Spinner 0x009537AA,
//                                 Outline 0x00953A9D, Gen 0x00953BF0
//   framework.uiscript.widgetgen  GZWinGen's property handler 0x009513BC
//
// The registrations are one shape, so ours are tables: the game's own
// literals (by address, the text in the comments) and words, the handler
// (the game's address, which the map keeps) and the interface, in the
// game's order. The handler calls the property helpers of part u12, which
// take arguments in registers, by address (ours when their groups are on).
// Still the game's: the maps' insert functions, the other classes' handlers
// (parts u12 and u14 to u16). The groups go in at Attach, with the UI
// script loader's.
#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::gzui {
namespace {

// ====================================================================================
// Helpers

template <typename R, typename... Args>
inline R CallThis(std::uintptr_t address, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(address)(self, args...);
}

template <typename T>
inline T& At(void* base, std::size_t offset)
{
    return *reinterpret_cast<T*>(static_cast<char*>(base) + offset);
}

template <typename R, typename... Args>
inline R Virtual(void* object, std::size_t byteOffset, Args... args)
{
    void** const vtable = *reinterpret_cast<void***>(object);
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(vtable[byteOffset / 4])(object, args...);
}

inline bool Yes(uint32_t eax) { return (eax & 0xFF) != 0; }

inline uint32_t Word32(const void* pointer) { return static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(pointer)); }

template <typename... Args>
inline uint32_t Scan(const char* text, std::uintptr_t format, Args... args)
{
    using Fn = uint32_t(__cdecl*)(const char*, const char*, ...);
    return reinterpret_cast<Fn>(addr::SC4_sscanf)(text, reinterpret_cast<const char*>(format), args...);
}

inline uint32_t Free(void* memory) { return reinterpret_cast<uint32_t(__cdecl*)(void*)>(addr::SC4_Free)(memory); }

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("gzui: widgets: first %s", name);
}

#define OPENSC4_FIRST(name)                                                                                    \
    do {                                                                                                       \
        static bool logged_ = false;                                                                           \
        if (!logged_)                                                                                          \
            FirstCall(name, logged_);                                                                          \
    } while (0)

// A string (SC4String, 0x14 bytes): the vtable 0x00A80810, the characters
// from +4. Destroyed inline as the game does on its stack (the vtable
// back, the characters freed); the free's EAX answered when there were
// characters, else the one given.
constexpr std::size_t kTextBegin = 4;
constexpr std::size_t kTextSize = 0x14;

struct Text {
    alignas(4) char bytes[kTextSize];
    char* Begin() { return At<char*>(bytes, kTextBegin); }
    uint32_t End(uint32_t eax)
    {
        At<std::uintptr_t>(bytes, 0) = addr::SC4String_Vtable;
        if (char* const begin = Begin())
            return Free(begin);
        return eax;
    }
};

// ====================================================================================
// The registrations

// A name (one of the game's literals, by address) and its word.
struct Word {
    std::uintptr_t text;
    uint32_t word;
};

// A class's registration: its names (the class, its interface; GZWinGen's
// also "SC3WinGen") into the variable table, then its handler under the
// interface, its interface under the class, then its property words.
struct Widget {
    const Word* names;
    std::size_t nameCount;
    uint32_t handlerKey;
    std::uintptr_t handler;
    uint32_t classKey;
    uint32_t classInterface;
    const Word* words;
    std::size_t wordCount;
};

// The variable table's slot 3 with a string made from the literal
// (SC4String::FromText, its EAX handed on); the table's vtable is read
// before the string is made and the table again after, as the game does.
uint32_t AddWord(const Word& word)
{
    void* const* const tableAt = reinterpret_cast<void* const*>(addr::cGZUIScriptLegacy_Table);
    void** const vtable = *reinterpret_cast<void***>(*tableAt);
    Text text;
    void* const made = CallThis<void*>(addr::SC4String_FromText, text.bytes, reinterpret_cast<const char*>(word.text));
    using Add = uint32_t(__thiscall*)(void*, void*, uint32_t);
    const uint32_t eax = reinterpret_cast<Add>(vtable[3])(*tableAt, made, word.word);
    return text.End(eax);
}

// A map's insert (thiscall: the result, a (key, value) pair; `ret 8`);
// its EAX (the result's place) answered.
uint32_t Insert(std::uintptr_t insert, std::uintptr_t map, uint32_t key, uint32_t value)
{
    const uint32_t pair[2] = {key, value};
    uint32_t result[2];
    return CallThis<uint32_t>(insert, reinterpret_cast<void*>(map), static_cast<void*>(result),
                              static_cast<const void*>(pair));
}

// The registration (void; EAX as the last call leaves it).
uint32_t Register(const Widget& widget)
{
    for (std::size_t i = 0; i < widget.nameCount; ++i)
        AddWord(widget.names[i]);
    Insert(addr::cGZUIScriptWidgets_HandlerInsert, addr::cGZUIScriptWidgets_Handlers, widget.handlerKey,
           static_cast<uint32_t>(widget.handler));
    uint32_t eax =
        Insert(addr::SC4FloraMap_Insert, addr::cGZUIScriptWidgets_Interfaces, widget.classKey, widget.classInterface);
    for (std::size_t i = 0; i < widget.wordCount; ++i)
        eax = AddWord(widget.words[i]);
    return eax;
}

// The 16 classes, by address (the constructor registers them in the
// order FlatRect to Outline, then TextTicker, Folders, Custom and Gen);
// the game's literals by address.
// GZWinTextTicker (0x00951269)
const Word kTextTickerNames[] = {
    {0x00AD5774, 0xE32F0B31}, // "GZWinTextTicker"
    {0x00AD5760, 0x032F0B3C}, // "IGZWinTextTicker"
};
const Word kTextTickerWords[] = {
    {0x00AA03BC, 0x00001200}, // "speed"
    {0x00A80A34, 0x00001201}, // "frequency"
    {0x00A809A0, 0x00001202}, // "message"
};
const Widget kTextTicker = {kTextTickerNames, 2, 0x032F0B3C, 0x0094CE28, 0xE32F0B31, 0x032F0B3C, kTextTickerWords, 3};

// GZWinFolders (0x009518A8)
const Word kFoldersNames[] = {
    {0x00AD57B0, 0x078E8055}, // "GZWinFolders"
    {0x00AD57A0, 0xA13351B8}, // "IGZWinFolders"
};
const Widget kFolders = {kFoldersNames, 2, 0xA13351B8, 0x0094D17E, 0x078E8055, 0xA13351B8, nullptr, 0};

// GZWinCustom (0x0095195E)
const Word kCustomNames[] = {
    {0x00AD57D0, 0x478D1E6F}, // "GZWinCustom"
    {0x00AD57C0, 0x678D1E84}, // "IGZWinCustom"
};
const Widget kCustom = {kCustomNames, 2, 0x678D1E84, 0x00A6D837, 0x478D1E6F, 0x678D1E84, nullptr, 0};

// GZWinFlatRect (0x00951A14)
const Word kFlatRectNames[] = {
    {0x00AD5830, 0xC2AFA76E}, // "GZWinFlatRect"
    {0x00AD5820, 0xC2AFA76F}, // "IGZWinFlatRect"
};
const Word kFlatRectWords[] = {
    {0x00AD5814, 0x00000200}, // "colorleft"
    {0x00AD5808, 0x00000201}, // "colortop"
    {0x00AD57FC, 0x00000202}, // "colorright"
    {0x00AD57F0, 0x00000203}, // "colorbottom"
    {0x00AD57EC, 0x00000204}, // "top"
    {0x00AD57E4, 0x00000205}, // "bottom"
    {0x00AA1C64, 0x00000206}, // "all"
    {0x00AD57E0, 0x00000207}, // "lrb"
    {0x00AD57DC, 0x00000208}, // "trb"
};
const Widget kFlatRect = {kFlatRectNames, 2, 0xC2AFA76F, 0x0094E33B, 0xC2AFA76E, 0xC2AFA76F, kFlatRectWords, 9};

// GZWinText (0x00951C9F)
const Word kTextNames[] = {
    {0x00AD58E8, 0x00000592}, // "GZWinText"
    {0x00AD58DC, 0x212CDC1F}, // "IGZWinText"
};
const Word kTextWords[] = {
    {0x00AD58D4, 0x00000300}, // "wrapped"
    {0x00AD58CC, 0x00000301}, // "opaque"
    {0x00AD58C0, 0x00000302}, // "defcolor"
    {0x00AD58B4, 0x00000303}, // "dbgdrawarea"
    {0x00AD58AC, 0x00000304}, // "autofit"
    {0x00AD58A4, 0x00000000}, // "lefttop"
    {0x00AD5898, 0x00000001}, // "leftcenter"
    {0x00AD588C, 0x00000002}, // "leftbottom"
    {0x00AD5880, 0x00000003}, // "centertop"
    {0x00AD5644, 0x00000004}, // "center"
    {0x00AD5870, 0x00000005}, // "centerbottom"
    {0x00AD5864, 0x00000006}, // "righttop"
    {0x00AD5858, 0x00000007}, // "rightcenter"
    {0x00AD584C, 0x00000008}, // "rightbottom"
    {0x00AD5840, 0x00000305}, // "textoffsets"
};
const Widget kText = {kTextNames, 2, 0x212CDC1F, 0x0094E516, 0x00000592, 0x212CDC1F, kTextWords, 15};

// GZWinTextEdit (0x00952046)
const Word kTextEditNames[] = {
    {0x00AD5A70, 0x231A1C58}, // "GZWinTextEdit"
    {0x00AD5A60, 0x231A1C57}, // "IGZWinTextEdit"
};
const Word kTextEditWords[] = {
    {0x00AD5A54, 0x00000400}, // "editable"
    {0x00AD5A48, 0x00000401}, // "hscrollbar"
    {0x00AD5A3C, 0x00000402}, // "vscrollbar"
    {0x00AD5A2C, 0x00000403}, // "caretvisible"
    {0x00AD5A20, 0x00000404}, // "allowinsert"
    {0x00AD5A14, 0x00000405}, // "allowundo"
    {0x00AD5A08, 0x00000406}, // "singleline"
    {0x00AD59FC, 0x00000407}, // "caretcolor"
    {0x00AD59EC, 0x00000408}, // "highlightcolor"
    {0x00AD59E4, 0x00000409}, // "maxtext"
    {0x00AD59D8, 0x0000040A}, // "initvalue"
    {0x00AD59C8, 0x0000040B}, // "outlinecolor"
    {0x00AD59BC, 0x0000040C}, // "overwrite"
    {0x00AD59B0, 0x0000040D}, // "insertindex"
    {0x00AD59A4, 0x0000040E}, // "insertpos"
    {0x00AD5998, 0x0000040F}, // "caretperiod"
    {0x00AD5990, 0x00000410}, // "maxundo"
    {0x00AD5980, 0x00000413}, // "hscrollimage"
    {0x00AD596C, 0x00000414}, // "hscrollimageRect"
    {0x00AD595C, 0x00000411}, // "vscrollimage"
    {0x00AD5948, 0x00000412}, // "vscrollimageRect"
    {0x00AD5938, 0x00000415}, // "notifyonreturn"
    {0x00AD5928, 0x00000416}, // "notifyonchange"
    {0x00AD5914, 0x00000417}, // "notifyonlostfocus"
    {0x00AD5904, 0x00000418}, // "enableclipboard"
    {0x00AD58F4, 0x00000419}, // "passwordmode"
};
const Widget kTextEdit = {kTextEditNames, 2, 0x231A1C57, 0x0094E7C8, 0x231A1C58, 0x231A1C57, kTextEditWords, 26};

// GZWinListBox (0x00952645)
const Word kListBoxNames[] = {
    {0x00AD5AE8, 0x00000598}, // "GZWinListBox"
    {0x00AD5AD8, 0x4132242B}, // "IGZWinListBox"
};
const Word kListBoxWords[] = {
    {0x00AD5AC4, 0x00000500}, // "scrollbargutters"
    {0x00AD5AB4, 0x00000501}, // "initselection"
    {0x00ACA8D8, 0x00000503}, // "sort"
    {0x00AD5AAC, 0x00000504}, // "drop"
    {0x00AD5A9C, 0x00000505}, // "setlinevisible"
    {0x00AD5A8C, 0x00000506}, // "setlineontop"
    {0x00AD5A80, 0x00000502}, // "listelement"
};
const Widget kListBox = {kListBoxNames, 2, 0x4132242B, 0x0094EE39, 0x00000598, 0x4132242B, kListBoxWords, 7};

// GZWinCombo (0x00952868)
const Word kComboNames[] = {
    {0x00AD5B4C, 0x0000059B}, // "GZWinCombo"
    {0x00AD5B40, 0x412CE496}, // "IGZWinCombo"
};
const Word kComboWords[] = {
    {0x00AD5B2C, 0x00000800}, // "combodownarrowimage"
    {0x00AD5B18, 0x00000801}, // "combodownarrowrect"
    {0x00AD5B08, 0x00000802}, // "combodowncolor"
    {0x00AD5AF8, 0x00000803}, // "buttongutter"
};
const Widget kCombo = {kComboNames, 2, 0x412CE496, 0x0094F36D, 0x0000059B, 0x412CE496, kComboWords, 4};

// GZWinBtn (0x009529EF)
const Word kBtnNames[] = {
    {0x00AD5CAC, 0x22ECFC47}, // "GZWinBtn"
    {0x00AD5CA0, 0x00008810}, // "IGZWinBtn"
};
const Word kBtnWords[] = {
    {0x00AD5C98, 0x00000A00}, // "toggle"
    {0x00AD5C88, 0x00000A01}, // "triggerondown"
    {0x00AD5C7C, 0x00000A02}, // "showcaption"
    {0x00AD5C70, 0x00000A03}, // "autosize"
    {0x00AD5C64, 0x00000A04}, // "wrapcaption"
    {0x00AD5C54, 0x00000A05}, // "shiftcaption"
    {0x00AD5C4C, 0x00000A06}, // "tips"
    {0x00AD5C40, 0x00000A07}, // "tipsdelay"
    {0x00AD5C34, 0x00000A08}, // "tipstimeout"
    {0x00AD5C2C, 0x00000A09}, // "label"
    {0x00AD5C20, 0x00000A0A}, // "standard"
    {0x00AD5C10, 0x00000A0B}, // "standardfixed"
    {0x00AD5C04, 0x00000A0C}, // "togglebtn"
    {0x00AD5BF4, 0x00000A0D}, // "togglebtnfixed"
    {0x00AD5BE8, 0x00000A0E}, // "checkbox"
    {0x00AD5BDC, 0x00000A0F}, // "radiobtn"
    {0x00AD5BD4, 0x00000A10}, // "tiptext"
    {0x00AD5BC8, 0x00000A11}, // "tipoffsets"
    {0x00AD5BC0, 0x00000A12}, // "tipres"
    {0x00AD5BB8, 0x00000A13}, // "noimage"
    {0x00AD5BA8, 0x00000A14}, // "standardhilite"
    {0x00AD5B94, 0x00000A15}, // "standardhilitefixed"
    {0x00AD5B84, 0x00000A16}, // "radiocheckall"
    {0x00A86E38, 0x00000A17}, // "radio"
    {0x00AD5B7C, 0x00000A18}, // "tipflag"
    {0x00AD5B70, 0x00000A19}, // "btnclicksnd"
    {0x00AD5B64, 0x00000A1A}, // "btnupsnd"
    {0x00AD5B58, 0x00000A1B}, // "radiocheck"
};
const Widget kBtn = {kBtnNames, 2, 0x00008810, 0x0094F757, 0x22ECFC47, 0x00008810, kBtnWords, 28};

// GZWinBMP (0x00953056)
const Word kBMPNames[] = {
    {0x00AD5CE0, 0x82FE68C4}, // "GZWinBMP"
    {0x00AD5CD4, 0xC12CEA13}, // "IGZWinBMP"
};
const Word kBMPWords[] = {
    {0x00A9F5B0, 0x00000B00}, // "alpha"
    {0x00AD5CC4, 0x00000B01}, // "transparentbkg"
    {0x00AD5CB8, 0x00000B02}, // "edgeimage"
};
const Widget kBMP = {kBMPNames, 2, 0xC12CEA13, 0x0095002C, 0x82FE68C4, 0xC12CEA13, kBMPWords, 3};

// GZWinSlider (0x009531A9)
const Word kSliderNames[] = {
    {0x00AD5D20, 0x21325208}, // "GZWinSlider"
    {0x00AD5D10, 0x21325207}, // "IGZWinSlider"
};
const Word kSliderWords[] = {
    {0x00AD5D04, 0x00000C00}, // "minmaxvalue"
    {0x00AD5CF8, 0x00000C01}, // "direction"
    {0x00AD5CEC, 0x00000C02}, // "horizontal"
    {0x00AA0108, 0x00000C03}, // "vertical"
};
const Widget kSlider = {kSliderNames, 2, 0x21325207, 0x00950218, 0x21325208, 0x21325207, kSliderWords, 4};

// GZWinScrollbar (0x00953330)
const Word kScrollbarNames[] = {
    {0x00AD5D64, 0x61325A2E}, // "GZWinScrollbar"
    {0x00AD5D54, 0x61325A2D}, // "IGZWinScrollbar"
};
const Word kScrollbarWords[] = {
    {0x00AD5D48, 0x00000D00}, // "pagesize"
    {0x00AD5D3C, 0x00000D01}, // "linesize"
    {0x00AD5D2C, 0x00000D02}, // "linepagecount"
};
const Widget kScrollbar = {kScrollbarNames, 2, 0x61325A2D, 0x009503AB, 0x61325A2E, 0x61325A2D, kScrollbarWords, 3};

// GZWinLineInput (0x00953483)
const Word kLineInputNames[] = {
    {0x00AD5E18, 0x21335C5A}, // "GZWinLineInput"
    {0x00AD5E08, 0x21335C59}, // "IGZWinLineInput"
};
const Word kLineInputWords[] = {
    {0x00AD5E00, 0x00000E00}, // "filled"
    {0x00AD5DF4, 0x00000E01}, // "outline3d"
    {0x00AD5DE8, 0x00000E02}, // "outline3dex"
    {0x00AD5DE0, 0x00000E03}, // "colorlt"
    {0x00AD5DD8, 0x00000E04}, // "colorrb"
    {0x00AD5DC8, 0x00000E05}, // "colorltinside"
    {0x00AD5DB8, 0x00000E06}, // "colorrbinside"
    {0x00AD5DA8, 0x00000E07}, // "fillcolorhilite"
    {0x00AD5D98, 0x00000E08}, // "fillalphahilite"
    {0x00AD5D8C, 0x00000E09}, // "caretpos"
    {0x00AD5D80, 0x00000E0A}, // "charlimit"
    {0x00AD5D74, 0x00000E0B}, // "selection"
};
const Widget kLineInput = {kLineInputNames, 2, 0x21335C59, 0x009505B6, 0x21335C5A, 0x21335C59, kLineInputWords, 12};

// GZWinSpinner (0x009537AA)
const Word kSpinnerNames[] = {
    {0x00AD5ED4, 0x612CE0C4}, // "GZWinSpinner"
    {0x00AD5EC4, 0x612CE0C3}, // "IGZWinSpinner"
};
const Word kSpinnerWords[] = {
    {0x00AD5EBC, 0x00001000}, // "digits"
    {0x00AD5EB0, 0x00001001}, // "autonumber"
    {0x00AD5EA0, 0x00001002}, // "autonumbercomma"
    {0x00AD5E8C, 0x00001003}, // "autonumbercurrency"
    {0x00AD5E84, 0x00001004}, // "minmax"
    {0x00AD5E78, 0x00001005}, // "stepsize"
    {0x00AD5E68, 0x00001006}, // "coloroutline"
    {0x00AD5E58, 0x00001007}, // "coloroutlinel"
    {0x00AD5E48, 0x00001008}, // "coloroutlinet"
    {0x00AD5E38, 0x00001009}, // "coloroutliner"
    {0x00AD5E28, 0x0000100A}, // "coloroutlineb"
};
const Widget kSpinner = {kSpinnerNames, 2, 0x612CE0C3, 0x00950AF6, 0x612CE0C4, 0x612CE0C3, kSpinnerWords, 11};

// GZWinOutline (0x00953A9D)
const Word kOutlineNames[] = {
    {0x00AD5F14, 0x4303E5B6}, // "GZWinOutline"
    {0x00AD5F04, 0xE303E5AE}, // "IGZWinOutline"
};
const Word kOutlineWords[] = {
    {0x00AD5EF8, 0x00001100}, // "colordark"
    {0x00AD5EEC, 0x00001101}, // "colorlight"
    {0x00AD5EE4, 0x00001102}, // "node"
};
const Widget kOutline = {kOutlineNames, 2, 0xE303E5AE, 0x00951084, 0x4303E5B6, 0xE303E5AE, kOutlineWords, 3};

// GZWinGen (0x00953BF0)
const Word kGenNames[] = {
    {0x00AD6004, 0x4386D516}, // "SC3WinGen"
    {0x00AD5FF8, 0x4386D516}, // "GZWinGen"
    {0x00AD5FEC, 0x5386D516}, // "IGZWinGen"
};
const Word kGenWords[] = {
    {0x00ACB938, 0x00010000}, // "userdata"
    {0x00AD5FE4, 0x00010001}, // "blttype"
    {0x00AD5FDC, 0x00000000}, // "tiled"
    {0x00AD5FD4, 0x00000002}, // "edge"
    {0x00AD5FCC, 0x00010002}, // "divider"
    {0x00AD5FC4, 0x00010003}, // "bluebar"
    {0x00AD5FB8, 0x00010004}, // "moveable"
    {0x00AD5FAC, 0x00010005}, // "sizeable"
    {0x00AD5FA0, 0x00010006}, // "defaultkeys"
    {0x00AD5F90, 0x00010007}, // "closevisible"
    {0x00AD5F80, 0x00010008}, // "gobackvisible"
    {0x00AD5F70, 0x00010009}, // "minmaxvisible"
    {0x00AD5F60, 0x0001000A}, // "closedisabled"
    {0x00AD5F50, 0x0001000B}, // "gobackdisabled"
    {0x00AD5F40, 0x0001000C}, // "minmaxdisabled"
    {0x00AD5F34, 0x0001000D}, // "titlebar"
    {0x00AD5F2C, 0x0001000E}, // "paint"
    {0x00AD5F24, 0x0001000F}, // "sidebar"
};
const Widget kGen = {kGenNames, 3, 0x5386D516, 0x009513BC, 0x4386D516, 0x5386D516, kGenWords, 18};

// The 16 detours (void; EAX as the game's leaves it: the last string's
// end, or the interface map's insert for the classes without words).
#define OPENSC4_REGISTER(name)                                                                                 \
    uint32_t __cdecl Register##name()                                                                          \
    {                                                                                                          \
        OPENSC4_FIRST("Register" #name);                                                                       \
        return Register(k##name);                                                                              \
    }

OPENSC4_REGISTER(TextTicker)
OPENSC4_REGISTER(Folders)
OPENSC4_REGISTER(Custom)
OPENSC4_REGISTER(FlatRect)
OPENSC4_REGISTER(Text)
OPENSC4_REGISTER(TextEdit)
OPENSC4_REGISTER(ListBox)
OPENSC4_REGISTER(Combo)
OPENSC4_REGISTER(Btn)
OPENSC4_REGISTER(BMP)
OPENSC4_REGISTER(Slider)
OPENSC4_REGISTER(Scrollbar)
OPENSC4_REGISTER(LineInput)
OPENSC4_REGISTER(Spinner)
OPENSC4_REGISTER(Outline)
OPENSC4_REGISTER(Gen)

#undef OPENSC4_REGISTER

// ====================================================================================
// GZWinGen's property handler

// The property helpers of part u12 take arguments in registers: the
// address called with EAX, EBX, ESI and EDI from the block and three stack
// arguments (cdecl, popped here; a callee taking fewer reads only its
// own); EAX answered, EDX back in the block.
struct Regs {
    uint32_t eax, ebx, esi, edi, edx;
};

__declspec(naked) uint32_t __cdecl CallRegs(std::uintptr_t /*address*/, Regs* /*regs*/, uint32_t /*a0*/,
                                            uint32_t /*a1*/, uint32_t /*a2*/)
{
    __asm push ebp
    __asm mov ebp, esp
    __asm push ebx
    __asm push esi
    __asm push edi
    __asm push dword ptr [ebp + 24]
    __asm push dword ptr [ebp + 20]
    __asm push dword ptr [ebp + 16]
    __asm mov ecx, [ebp + 12]
    __asm mov eax, [ecx]
    __asm mov ebx, [ecx + 4]
    __asm mov esi, [ecx + 8]
    __asm mov edi, [ecx + 12]
    __asm call dword ptr [ebp + 8]
    __asm add esp, 12
    __asm mov ecx, [ebp + 12]
    __asm mov [ecx + 16], edx
    __asm pop edi
    __asm pop esi
    __asm pop ebx
    __asm pop ebp
    __asm ret
}

// The interface GZWinGen's handler asks the window for (IGZWinGen) and the
// slots it calls on it.
constexpr uint32_t kGenInterface = 0x5386D516;
constexpr std::size_t kSetFlag = 0x18;    // slot 6 (mask, bool)
constexpr std::size_t kSetObject = 0x1C;  // slot 7 (an 8-byte object)
constexpr std::size_t kSetImage = 0x34;   // slot 13 (mode, image)
constexpr std::size_t kSetUser = 0x4C;    // slot 19 (word)
constexpr std::size_t kSetPair = 0x68;    // slot 26 (two ints)
constexpr std::size_t kSetRect = 0x74;    // slot 29 (word, 0, 0, rectangle)
constexpr std::size_t kSetPoint = 0x78;   // slot 30 (word, int)
// A text value's kind and slot 55 (its text into a string).
constexpr uint32_t kTextKind = 0x800E;
constexpr std::size_t kValueText = 0xDC;

// The flags: a number's key, the mask, and whether the flag is set when
// the word is not 0 (else when it is 0).
struct GenFlag {
    uint32_t key;
    uint32_t mask;
    bool whenSet;
};

const GenFlag kGenFlags[] = {
    {0x10004, 0x1, true},       {0x10005, 0x2, true},      {0x10006, 0x4, true},     {0x10007, 0x10, true},
    {0x10008, 0x20, true},      {0x10009, 0x40, true},     {0x1000A, 0x100, true},   {0x1000B, 0x200, true},
    {0x1000C, 0x400, true},     {0x1000D, 0x1000, false},  {0xF00D, 0x2000, false},  {0xF019, 0x4000, false},
    {0x1000E, 0x8000, false},   {0x1000F, 0x10000, true},
};

// The keys of the rest.
constexpr uint32_t kUserKey = 0x10000;
constexpr uint32_t kImageTextKey = 0xF016;
constexpr uint32_t kImageModeKey = 0x10001;
constexpr uint32_t kPairKey = 0xF004;
constexpr uint32_t kRawPointKey = 0x10002;
constexpr uint32_t kRawRectKey = 0x10003;
// The image's key: its type, and the keys the makers put it under.
constexpr uint32_t kImageType = 0x856DDBAC;
constexpr uint32_t kImageKey = 0xAB428C5D;
constexpr uint32_t kImagePairKey = 0;
// The 8-byte object given to slot 7 (its vtable, +4 zeroed).
constexpr std::size_t kObjectSize = 8;

// The set pass (the third argument's byte set): the 14 flags (0x0094C675;
// the bool pushed over the EDX the finder leaves), the user word, the
// background image (the text "{%x,%x}" of 0xF016: the group and instance
// loaded with 0x0094C4E0, the mode a number of 0x10001 (0 and 2 as they
// are, else 1), the image given with it, the key put under 0xAB428C5D and
// the two words under 0 through the window's slot 114, the image
// released), the pair of 0xF004 (0x0094D55A), and a new 8-byte object.
uint32_t GenSet(void* window, char* const* vector, void* face)
{
    const uint32_t vector32 = Word32(vector);
    uint32_t word = 0;
    uint32_t eax = 0;
    for (const GenFlag& flag : kGenFlags) {
        Regs regs = {Word32(&word), 0, 0, vector32, 0};
        eax = CallRegs(addr::cGZUIScriptProps_FindWord73, &regs, flag.key, Word32(&word), 0);
        if (Yes(eax)) {
            const uint32_t set = flag.whenSet ? (word != 0 ? 1u : 0u) : (word == 0 ? 1u : 0u);
            eax = Virtual<uint32_t>(face, kSetFlag, flag.mask, (regs.edx & ~0xFFu) | set);
        }
    }
    uint32_t user = 0;
    Regs regs = {Word32(&user), 0, 0x10000, vector32, 0};
    if (Yes(CallRegs(addr::cGZUIScriptProps_FindWord73, &regs, kUserKey, Word32(&user), 0)))
        Virtual<uint32_t>(face, kSetUser, user);

    Text text;
    CallThis<void*>(addr::SC4String_SC4String, text.bytes);
    regs = {Word32(text.bytes), Word32(text.bytes), 0x10000, vector32, 0};
    uint32_t group = 0;
    uint32_t instance = 0;
    if (Yes(CallRegs(addr::cGZUIScriptProps_FindText, &regs, kImageTextKey, 0, 0)) &&
        Scan(text.Begin(), addr::cGZUIScriptWidgets_GenImageFormat, &group, &instance) == 2) {
        regs = {group, 0, 0x10000, vector32, 0};
        void* const image = reinterpret_cast<void*>(
            static_cast<std::uintptr_t>(CallRegs(addr::cGZUIScriptProps_LoadImage, &regs, instance, 0, 0)));
        uint32_t mode = 0;
        regs = {Word32(&mode), Word32(image), 0x10000, vector32, 0};
        CallRegs(addr::cGZUIScriptProps_FindWord73, &regs, kImageModeKey, Word32(&mode), 0);
        if (image)
            Virtual<uint32_t>(face, kSetImage, mode == 0 || mode == 2 ? mode : 1u, image);
        const uint32_t key[3] = {kImageType, group, instance};
        regs = {Word32(key), Word32(image), Word32(window), vector32, 0};
        CallRegs(addr::cGZUIScriptProps_MakeKey, &regs, kImageKey, Word32(key), 0);
        regs = {group, Word32(image), Word32(window), vector32, 0};
        CallRegs(addr::cGZUIScriptProps_MakePair, &regs, instance, kImagePairKey, 0);
        if (image)
            Virtual<uint32_t>(image, 8);
    }

    int32_t pair[2];
    regs = {0, 0, 0, vector32, 0};
    if (Yes(CallRegs(addr::cGZUIScriptProps_ReadPair, &regs, kPairKey, Word32(&pair[0]), Word32(&pair[1]))))
        Virtual<uint32_t>(face, kSetPair, pair[0], pair[1]);

    void* const object = reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(kObjectSize);
    if (object) {
        At<uint32_t>(object, 4) = 0;
        At<std::uintptr_t>(object, 0) = addr::cGZUIScriptWidgets_GenObjectVtable;
    }
    eax = Virtual<uint32_t>(face, kSetObject, object);
    return text.End(eax);
}

// A text value's text (slot 55 of the pair's value, read again) into a
// string of the caller's.
void ValueText(char* const* vector, uint32_t index, Text& text)
{
    CallThis<void*>(addr::SC4String_SC4String, text.bytes);
    Virtual<uint32_t>(At<void*>(vector[0] + index * 8, 4), kValueText, static_cast<void*>(text.bytes));
}

// The raw pass (the byte 0): every pair, its index kept in the window's
// own argument slot; a text value of 0x10002 scanned as "(%u,%d)" for slot
// 30, of 0x10003 as "(%u,%d,%d,%d,%d)" for slot 29 (the word, 0, 0 and the
// rectangle). EAX as the game's last left it.
uint32_t GenRaw(uint32_t* arguments, char* const* vector, void* face, uint32_t eax)
{
    const uint32_t count = static_cast<uint32_t>(static_cast<int32_t>(vector[1] - vector[0]) >> 3);
    arguments[0] = 0;
    if (!count)
        return eax;
    do {
        const uint32_t i = arguments[0];
        char* const pair = vector[0] + i * 8;
        eax = Word32(pair);
        const uint32_t key = At<uint32_t>(pair, 0);
        if (key == kRawPointKey || key == kRawRectKey) {
            void* const value = At<void*>(pair, 4);
            eax = Word32(value);
            if (value) {
                eax = Virtual<uint32_t>(value, 0xC);
                if (eax == kTextKind) {
                    Text text;
                    ValueText(vector, i, text);
                    if (key == kRawRectKey) {
                        uint32_t word;
                        int32_t rect[4];
                        eax = Scan(text.Begin(), addr::cGZUIScriptWidgets_GenRectFormat, &word, &rect[0], &rect[1],
                                   &rect[2], &rect[3]);
                        if (eax == 5)
                            eax = Virtual<uint32_t>(face, kSetRect, word, 0u, 0u, static_cast<void*>(rect));
                    } else {
                        uint32_t word;
                        int32_t point;
                        eax = Scan(text.Begin(), addr::cGZUIScriptWidgets_GenPointFormat, &word, &point);
                        if (eax == 2)
                            eax = Virtual<uint32_t>(face, kSetPoint, word, point);
                    }
                    eax = text.End(eax);
                }
            }
        }
    } while (++arguments[0] < count);
    return eax;
}

// The game's 0x009513BC (cdecl: the window, its properties' vector of
// pairs and a byte): the window's IGZWinGen (QueryInterface 0x5386D516);
// with it the set pass or the raw pass; the interface released (its EAX
// answered; the QueryInterface's EAX when it gave none, even when it
// failed).
uint32_t __cdecl GenReadBody(uint32_t* arguments)
{
    OPENSC4_FIRST("GenRead");
    void* const window = reinterpret_cast<void*>(static_cast<std::uintptr_t>(arguments[0]));
    char* const* const vector = reinterpret_cast<char* const*>(static_cast<std::uintptr_t>(arguments[1]));
    void* face = nullptr;
    uint32_t eax = Virtual<uint32_t>(window, 0, kGenInterface, &face);
    if (Yes(eax))
        eax = (arguments[2] & 0xFF) ? GenSet(window, vector, face) : GenRaw(arguments, vector, face, eax);
    if (face)
        eax = Virtual<uint32_t>(face, 8);
    return eax;
}

// The thunk hands the body the argument slots (the raw pass writes the
// first).
__declspec(naked) void GenRead()
{
    __asm lea eax, [esp + 4]
    __asm push eax
    __asm call GenReadBody
    __asm add esp, 4
    __asm ret
}

// ====================================================================================
// Install

struct Detour {
    std::uintptr_t address;
    void* ours;
};

template <std::size_t N>
bool InstallAll(const Detour (&detours)[N])
{
    if (!game::IsBuild641())
        return false;
    for (const Detour& detour : detours) {
        const addr::Entry* entry = addr_check::Find(detour.address);
        void* original = nullptr;
        if (!entry || !hook::Install(*entry, detour.ours, &original))
            return false;
    }
    return true;
}

bool InstallWidgets(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZUIScriptWidgets_RegisterTextTicker, reinterpret_cast<void*>(&RegisterTextTicker)},
        {addr::cGZUIScriptWidgets_RegisterFolders, reinterpret_cast<void*>(&RegisterFolders)},
        {addr::cGZUIScriptWidgets_RegisterCustom, reinterpret_cast<void*>(&RegisterCustom)},
        {addr::cGZUIScriptWidgets_RegisterFlatRect, reinterpret_cast<void*>(&RegisterFlatRect)},
        {addr::cGZUIScriptWidgets_RegisterText, reinterpret_cast<void*>(&RegisterText)},
        {addr::cGZUIScriptWidgets_RegisterTextEdit, reinterpret_cast<void*>(&RegisterTextEdit)},
        {addr::cGZUIScriptWidgets_RegisterListBox, reinterpret_cast<void*>(&RegisterListBox)},
        {addr::cGZUIScriptWidgets_RegisterCombo, reinterpret_cast<void*>(&RegisterCombo)},
        {addr::cGZUIScriptWidgets_RegisterBtn, reinterpret_cast<void*>(&RegisterBtn)},
        {addr::cGZUIScriptWidgets_RegisterBMP, reinterpret_cast<void*>(&RegisterBMP)},
        {addr::cGZUIScriptWidgets_RegisterSlider, reinterpret_cast<void*>(&RegisterSlider)},
        {addr::cGZUIScriptWidgets_RegisterScrollbar, reinterpret_cast<void*>(&RegisterScrollbar)},
        {addr::cGZUIScriptWidgets_RegisterLineInput, reinterpret_cast<void*>(&RegisterLineInput)},
        {addr::cGZUIScriptWidgets_RegisterSpinner, reinterpret_cast<void*>(&RegisterSpinner)},
        {addr::cGZUIScriptWidgets_RegisterOutline, reinterpret_cast<void*>(&RegisterOutline)},
        {addr::cGZUIScriptWidgets_RegisterGen, reinterpret_cast<void*>(&RegisterGen)},
    };
    return InstallAll(detours);
}

bool InstallWidgetGen(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZUIScriptWidgets_GenRead, reinterpret_cast<void*>(&GenRead)},
    };
    return InstallAll(detours);
}

const bool g_registered[] = {
    registry::Register({"framework.uiscript.widgets", "framework/uiscript", registry::Kind::Detour,
                        registry::Phase::Attach, addr::cGZUIScriptWidgets_RegisterTextTicker,
                        "Our registrations of the 16 stock widget classes: their names and property words into "
                        "the LEGACY readers' variable table, their handlers and interfaces into the two maps "
                        "(16 detours; part u13, docs/gzui.md)",
                        false, InstallWidgets}),
    registry::Register({"framework.uiscript.widgetgen", "framework/uiscript", registry::Kind::Detour,
                        registry::Phase::Attach, addr::cGZUIScriptWidgets_GenRead,
                        "Our property handler of GZWinGen (the generic window): its flags, user word, background "
                        "image, pair and the raw rectangle and point keys (1 detour; part u13, docs/gzui.md)",
                        false, InstallWidgetGen}),
};

} // namespace
} // namespace opensc4::gzui
