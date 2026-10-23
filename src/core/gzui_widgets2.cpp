// More widgets' UI script readers (docs/gzui.md, part u14): GZWinFileBrowser,
// the first class of 0x00954000 to 0x0095BBFF. Like the stock classes of part
// u13 (src/core/gzui_widgets.cpp), it registers itself when a LEGACY reader's
// constructor (0x0095B8C4) makes the shared variable table: its names and
// property words into the table, its property handler into the handler map
// (0x00B6358C) and its interface into the interface map (0x00B63598). The
// dispatcher (0x00957A9B) calls the handler with the window and its
// properties, then the base window's handler (0x0095660D), twice. Part
// u15 adds GZWinOptGrp, the base handler and the dispatcher. Replacements,
// each off by default:
//
//   framework.uiscript.filebrowser      the registration 0x009552BE
//   framework.uiscript.filebrowserread  the property handler 0x00954212
//   framework.uiscript.optgrp           GZWinOptGrp's registration 0x00957C88
//   framework.uiscript.optgrpread       its property handler 0x00956C46
//   framework.uiscript.baseread         the base window's handler 0x0095660D
//   framework.uiscript.dispatch         the dispatcher 0x00957A9B (the LEGACY
//                                       reader's slot 3)
//
// The registration is a table, as part u13's are: the game's own literals
// (by address, the text in the comments) and words, the handler (the game's
// address, which the map keeps) and the interface, in the game's order. The
// handler calls the property helpers of part u12, which take arguments in
// registers, by address (ours when their groups are on). Still the game's:
// the maps' insert functions, the STLport tree, list and pair code around
// them in the range (0x0095407A to 0x00956463), the helpers of part u12. The
// groups go in at Attach, with the UI script loader's.
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

template <typename R, typename... Args>
inline R Cdecl(std::uintptr_t address, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(address)(args...);
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
    log::Write("gzui: widgets2: first %s", name);
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
    void Make() { CallThis<void*>(addr::SC4String_SC4String, bytes); }
    uint32_t End(uint32_t eax)
    {
        At<std::uintptr_t>(bytes, 0) = addr::SC4String_Vtable;
        if (char* const begin = Begin())
            return Free(begin);
        return eax;
    }
};

// ====================================================================================
// The registration

// A name (one of the game's literals, by address) and its word.
struct Word {
    std::uintptr_t text;
    uint32_t word;
};

// A class's registration: its names (the class, its interface) into the
// variable table, then its handler under the interface, its interface
// under the class, then its property words.
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

// The game's literals by address.
// GZWinFileBrowser (0x009552BE)
const Word kFileBrowserNames[] = {
    {0x00AD6560, 0x1AA52EA4}, // "GZWinFileBrowser"
    {0x00AD654C, 0x3AA52E64}, // "IGZWinFileBrowser"
};
const Word kFileBrowserWords[] = {
    {0x00AD6540, 0x00001301}, // "defaultpath"
    {0x00AD6538, 0x00001302}, // "backdir"
    {0x00AA03B8, 0x00001303}, // "dir"
    {0x00AC1298, 0x00001304}, // "file"
    {0x00AD6520, 0x00001305}, // "defaultselectiontype"
    {0x00AD6508, 0x00001306}, // "defaultselectionname"
    {0x00AD64EC, 0x00001307}, // "defaultselectionextension"
    {0x00AD64DC, 0x00001308}, // "bynameascending"
    {0x00AD64C8, 0x00001309}, // "bynamedescending"
    {0x00AD64B0, 0x0000130A}, // "byextensionascending"
    {0x00AD6498, 0x0000130B}, // "byextensiondescending"
    {0x00AD6488, 0x0000130C}, // "filesortprimary"
    {0x00AD6474, 0x0000130D}, // "filesortsecondary"
    {0x00AD6464, 0x0000130E}, // "topascending"
    {0x00AD6454, 0x0000130F}, // "topdescending"
    {0x00AD6444, 0x00001310}, // "bottomascending"
    {0x00AD6430, 0x00001311}, // "bottomdescending"
    {0x00AD6420, 0x00001312}, // "directorysort"
    {0x00AD6418, 0x00001313}, // "icon"
    {0x00A81380, 0x00001314}, // "name"
    {0x00AD640C, 0x00001315}, // "extension"
    {0x00A923FC, 0x00001316}, // "empty"
    {0x00AD6404, 0x00001317}, // "unused"
    {0x00AD63FC, 0x00001318}, // "default"
    {0x00AD63F0, 0x00001319}, // "column1data"
    {0x00AD63E4, 0x0000131A}, // "column2data"
    {0x00AD63D8, 0x0000131B}, // "column3data"
    {0x00AD63C8, 0x0000131C}, // "filenamefont"
    {0x00AD63B0, 0x0000131D}, // "filenameforegroundcolor"
    {0x00AD6398, 0x0000131E}, // "filenamebackgroundcolor"
    {0x00AD6378, 0x0000131F}, // "filenameforegroundcolordisabled"
    {0x00AD6358, 0x00001320}, // "filenamebackgroundcolordisabled"
    {0x00AD6338, 0x00001321}, // "filenameforegroundcolorhilited"
    {0x00AD6318, 0x00001322}, // "filenamebackgroundcolorhilited"
    {0x00AD6304, 0x00001323}, // "filenamealignment"
    {0x00AD62F0, 0x00001324}, // "fileextensionfont"
    {0x00AD62D0, 0x00001325}, // "fileextensionforegroundcolor"
    {0x00AD62B0, 0x00001326}, // "fileextensionbackgroundcolor"
    {0x00AD6288, 0x00001327}, // "fileextensionforegroundcolordisabled"
    {0x00AD6260, 0x00001328}, // "fileextensionbackgroundcolordisabled"
    {0x00AD623C, 0x00001329}, // "fileextensionforegroundcolorhilited"
    {0x00AD6218, 0x0000132A}, // "fileextensionbackgroundcolorhilited"
    {0x00AD6200, 0x0000132B}, // "fileextensionalignment"
    {0x00AD61F0, 0x0000132C}, // "drivenamefont"
    {0x00AD61D4, 0x0000132D}, // "drivenameforegroundcolor"
    {0x00AD61B8, 0x0000132E}, // "drivenamebackgroundcolor"
    {0x00AD6194, 0x0000132F}, // "drivenameforegroundcolordisabled"
    {0x00AD6170, 0x00001330}, // "drivenamebackgroundcolordisabled"
    {0x00AD6150, 0x00001331}, // "drivenameforegroundcolorhilited"
    {0x00AD6130, 0x00001332}, // "drivenamebackgroundcolorhilited"
    {0x00AD611C, 0x00001333}, // "drivenamealignment"
    {0x00AD610C, 0x00001334}, // "iconcolumnwidth"
    {0x00AD60F4, 0x00001335}, // "extensioncolumnwidth"
    {0x00AD60E8, 0x00001336}, // "rowheight"
    {0x00AD60D8, 0x00001337}, // "linegridenabled"
    {0x00AD60C8, 0x00001338}, // "linegridcolor"
    {0x00AD60B4, 0x00001339}, // "columngridenabled"
    {0x00AD60A4, 0x0000133A}, // "columngridcolor"
    {0x00AD6088, 0x0000133B}, // "highlightcolorforeground"
    {0x00AD606C, 0x0000133C}, // "highlightcolorbackground"
    {0x00AD6060, 0x0000133D}, // "backdirtext"
    {0x00AD6050, 0x0000133E}, // "drivelistsize"
    {0x00AD6040, 0x0000133F}, // "backgroundcolor"
    {0x00AD6030, 0x00001340}, // "backgroundimage"
    {0x00AD6018, 0x00001341}, // "backgroundtransparent"
    {0x00AD6010, 0x00001342}, // "filter"
};
const Widget kFileBrowser = {kFileBrowserNames, 2, 0x3AA52E64, 0x00954212, 0x1AA52EA4, 0x3AA52E64, kFileBrowserWords, 66};

// The detour (void; EAX as the game's leaves it: the last string's end).
uint32_t __cdecl RegisterFileBrowser()
{
    OPENSC4_FIRST("RegisterFileBrowser");
    return Register(kFileBrowser);
}

// ====================================================================================
// GZWinFileBrowser's property handler

// The property helpers of part u12 take arguments in registers: the
// address called with EAX, EBX, ESI and EDI from the block and four stack
// arguments (cdecl, popped here; a callee taking fewer reads only its
// own); EAX answered, EDX back in the block.
struct Regs {
    uint32_t eax, ebx, esi, edi, edx;
};

__declspec(naked) uint32_t __cdecl CallRegs(std::uintptr_t /*address*/, Regs* /*regs*/, uint32_t /*a0*/,
                                            uint32_t /*a1*/, uint32_t /*a2*/, uint32_t /*a3*/)
{
    __asm push ebp
    __asm mov ebp, esp
    __asm push ebx
    __asm push esi
    __asm push edi
    __asm push dword ptr [ebp + 28]
    __asm push dword ptr [ebp + 24]
    __asm push dword ptr [ebp + 20]
    __asm push dword ptr [ebp + 16]
    __asm mov ecx, [ebp + 12]
    __asm mov eax, [ecx]
    __asm mov ebx, [ecx + 4]
    __asm mov esi, [ecx + 8]
    __asm mov edi, [ecx + 12]
    __asm call dword ptr [ebp + 8]
    __asm add esp, 16
    __asm mov ecx, [ebp + 12]
    __asm mov [ecx + 16], edx
    __asm pop edi
    __asm pop esi
    __asm pop ebx
    __asm pop ebp
    __asm ret
}

// The interface the handler asks the window for (IGZWinFileBrowser) and
// the slots it calls on it (byte offsets).
constexpr uint32_t kBrowserInterface = 0x3AA52E64;
constexpr std::size_t kSetPath = 0x3C;        // slot 15 (string)
constexpr std::size_t kSetSelection = 0x70;   // slot 28 (kind, name, extension)
constexpr std::size_t kGetSort = 0x88;        // slot 34 (primary*, secondary*)
constexpr std::size_t kSetSort = 0x8C;        // slot 35 (primary, secondary)
constexpr std::size_t kSetDirSort = 0x94;     // slot 37 (order)
constexpr std::size_t kGetColumns = 0x98;     // slot 38 (three columns*)
constexpr std::size_t kSetColumns = 0xA0;     // slot 40 (three columns)
constexpr std::size_t kSetIconWidth = 0xF4;   // slot 61
constexpr std::size_t kSetExtWidth = 0xFC;    // slot 63
constexpr std::size_t kSetRowHeight = 0x104;  // slot 65
constexpr std::size_t kGetLineGrid = 0x108;   // slot 66 (enabled*, colour*)
constexpr std::size_t kSetLineGrid = 0x10C;   // slot 67 (enabled, colour)
constexpr std::size_t kGetColumnGrid = 0x110; // slot 68
constexpr std::size_t kSetColumnGrid = 0x114; // slot 69
constexpr std::size_t kGetHighlight = 0x118;  // slot 70 (foreground*, background*)
constexpr std::size_t kSetHighlight = 0x11C;  // slot 71
constexpr std::size_t kSetBackText = 0x134;   // slot 77 (string)
constexpr std::size_t kSetFlagF019 = 0x13C;   // slot 79 (bool)
constexpr std::size_t kSetDriveSize = 0x144;  // slot 81
constexpr std::size_t kSetBackColour = 0x14C; // slot 83 (colour)
constexpr std::size_t kSetImage = 0x154;      // slot 85 (image)
constexpr std::size_t kSetTransparent = 0x15C; // slot 87 (bool)
constexpr std::size_t kAddFilter = 0x54;      // slot 21 (name, image, number, 0)
// The font service's slot 5 (a font by word) and the colour service's
// slot 34 (a colour of three bytes).
constexpr std::size_t kFont = 0x14;
constexpr std::size_t kColour = 0x88;
// A value's kind (slot 3) and its text (slot 55).
constexpr uint32_t kTextKind = 0x800E;
constexpr std::size_t kValueKind = 0xC;
constexpr std::size_t kValueText = 0xDC;

// The keys.
constexpr uint32_t kPathKey = 0x1301;
constexpr uint32_t kSelectionKey = 0x1305;
constexpr uint32_t kSelectionNameKey = 0x1306;
constexpr uint32_t kSelectionExtKey = 0x1307;
constexpr uint32_t kSortKeys[2] = {0x130C, 0x130D};
constexpr uint32_t kDirSortKey = 0x1312;
constexpr uint32_t kColumnKeys[3] = {0x1319, 0x131A, 0x131B};
constexpr uint32_t kIconWidthKey = 0x1334;
constexpr uint32_t kExtWidthKey = 0x1335;
constexpr uint32_t kRowHeightKey = 0x1336;
constexpr uint32_t kLineGridKey = 0x1337;
constexpr uint32_t kLineColourKey = 0x1338;
constexpr uint32_t kColumnGridKey = 0x1339;
constexpr uint32_t kColumnColourKey = 0x133A;
constexpr uint32_t kHighlightKeys[2] = {0x133B, 0x133C};
constexpr uint32_t kBackTextKey = 0x133D;
constexpr uint32_t kDriveSizeKey = 0x133E;
constexpr uint32_t kBackColourKey = 0x133F;
constexpr uint32_t kImageKey = 0x1340;
constexpr uint32_t kTransparentKey = 0x1341;
constexpr uint32_t kFlagF019Key = 0xF019;
constexpr uint32_t kFilterKey = 0x1342;

// A word picked from a list: the words and what each stands for.
struct Choice {
    uint32_t word;
    uint32_t value;
};

// backdir, dir, file
const Choice kSelections[] = {{0x1302, 0}, {0x1303, 1}, {0x1304, 2}};
// bynameascending, bynamedescending, byextensionascending, byextensiondescending
const Choice kSorts[] = {{0x1308, 0}, {0x1309, 1}, {0x130A, 2}, {0x130B, 3}};
// topascending, topdescending, bottomascending, bottomdescending
const Choice kDirSorts[] = {{0x130E, 0}, {0x130F, 1}, {0x1310, 2}, {0x1311, 3}};
// icon, name, extension, empty
const Choice kColumns[] = {{0x1313, 0}, {0x1314, 1}, {0x1315, 2}, {0x1316, 5}};
// unused, default and the framework's words 0xF006 to 0xF008
const Choice kAlignments[] = {{0x1317, 0}, {0x1318, 1}, {0xF006, 2}, {0xF007, 4}, {0xF008, 3}};

template <std::size_t N>
bool Pick(const Choice (&choices)[N], uint32_t word, uint32_t& value)
{
    for (const Choice& choice : choices) {
        if (choice.word == word) {
            value = choice.value;
            return true;
        }
    }
    return false;
}

// The three kinds of names (file names, extensions, drive names): the
// font's key and slot, the key its number or text is put under through the
// window (0, 1, 2), the first of six colours' keys and their slot, the
// alignment's key and slot.
struct Names {
    uint32_t fontKey;
    std::size_t fontSlot;
    uint32_t makeKey;
    uint32_t colourKey;
    std::size_t colourSlot;
    uint32_t alignKey;
    std::size_t alignSlot;
};

const Names kNames[] = {
    {0x131C, 0xB4, 0, 0x131D, 0xB8, 0x1323, 0xBC},
    {0x1324, 0xCC, 1, 0x1325, 0xD0, 0x132B, 0xD4},
    {0x132C, 0xE4, 2, 0x132D, 0xE8, 0x1333, 0xEC},
};

// The handler's state: the window, its vector of 8-byte (key, value)
// pairs, the interface; the three bytes the colour reader writes (their
// low bytes; ours zero above, where the game has what its stack held).
struct Browser {
    void* window;
    char* const* vector;
    void* face;
    uint32_t bytes[3];

    uint32_t Vector() const { return Word32(vector); }

    // 0x0094CCD9 (the string in EBX, the vector in EDI).
    bool FindText(Text& text, uint32_t key) const
    {
        Regs regs = {0, Word32(text.bytes), 0, Vector(), 0};
        return Yes(CallRegs(addr::cGZUIScriptProps_FindText, &regs, key, 0, 0, 0));
    }

    // 0x0094C675 (the vector in EDI); the EDX it leaves.
    bool FindWord(uint32_t key, uint32_t& word, uint32_t* edx = nullptr) const
    {
        Regs regs = {0, 0, 0, Vector(), 0};
        const bool found = Yes(CallRegs(addr::cGZUIScriptProps_FindWord73, &regs, key, Word32(&word), 0, 0));
        if (edx)
            *edx = regs.edx;
        return found;
    }

    // 0x0094D4CD (the vector in EDI) into the three bytes.
    bool ReadBytes(uint32_t key)
    {
        Regs regs = {0, 0, 0, Vector(), 0};
        return Yes(CallRegs(addr::cGZUIScriptProps_ReadBytes, &regs, key, Word32(&bytes[0]), Word32(&bytes[1]),
                            Word32(&bytes[2])));
    }

    // A colour of the three bytes (the colour service's slot 34).
    uint32_t Colour() const
    {
        void* const service = Cdecl<void*>(addr::SC4_Singleton913C1A);
        return Virtual<uint32_t>(service, kColour, bytes[0], bytes[1], bytes[2]);
    }

    // The face's slot with a word found under the key.
    void SetWord(uint32_t key, std::size_t slot) const
    {
        uint32_t word;
        if (FindWord(key, word))
            Virtual<uint32_t>(face, slot, word);
    }

    // The face's slot with a bool of a word found under the key, pushed
    // over the EDX 0x0094C675 leaves.
    void SetBool(uint32_t key, std::size_t slot) const
    {
        uint32_t word;
        uint32_t edx;
        if (FindWord(key, word, &edx))
            Virtual<uint32_t>(face, slot, (edx & ~0xFFu) | (word != 0 ? 1u : 0u));
    }

    // A word found under the key and picked from the list.
    template <std::size_t N>
    bool Choose(uint32_t key, const Choice (&choices)[N], uint32_t& value) const
    {
        uint32_t word;
        return FindWord(key, word) && Pick(choices, word, value);
    }
};

// A kind of names: the font (a number found: the font service's slot 5
// with it, the face's slot, the number put under the window's key 0, 1 or
// 2 by 0x0094C222; else a text found: put under it by 0x0094C255), six
// colours (slot with 0 to 5 and the colour), the alignment.
void ReadNames(Browser& browser, const Names& names, Text& text)
{
    uint32_t word;
    if (browser.FindWord(names.fontKey, word)) {
        void* const service = Cdecl<void*>(addr::SC4_Singleton913C72);
        const uint32_t font = Virtual<uint32_t>(service, kFont, word);
        Virtual<uint32_t>(browser.face, names.fontSlot, font);
        Regs regs = {0, 0, Word32(browser.window), 0, 0};
        CallRegs(addr::cGZUIScriptProps_MakeNumber, &regs, word, names.makeKey, 0, 0);
    } else if (browser.FindText(text, names.fontKey)) {
        Regs regs = {Word32(text.bytes), 0, Word32(browser.window), 0, 0};
        CallRegs(addr::cGZUIScriptProps_MakeText, &regs, Word32(text.bytes), names.makeKey, 0, 0);
    }
    for (uint32_t i = 0; i < 6; ++i) {
        if (browser.ReadBytes(names.colourKey + i)) {
            const uint32_t colour = browser.Colour();
            Virtual<uint32_t>(browser.face, names.colourSlot, i, colour);
        }
    }
    uint32_t align;
    if (browser.Choose(names.alignKey, kAlignments, align))
        Virtual<uint32_t>(browser.face, names.alignSlot, align);
}

// A pair of the face's (a flag's low byte, a colour): read by its getter,
// the flag (a word not 0) and the colour (three bytes) from their keys,
// given back by its setter. The two grids share the pair's places, as in
// the game.
void ReadFlagColour(Browser& browser, std::size_t get, std::size_t put, uint32_t flagKey, uint32_t colourKey,
                    uint32_t& flag, uint32_t& colour)
{
    Virtual<uint32_t>(browser.face, get, &flag, &colour);
    uint32_t word;
    if (browser.FindWord(flagKey, word))
        flag = (flag & ~0xFFu) | (word != 0 ? 1u : 0u);
    if (browser.ReadBytes(colourKey))
        colour = browser.Colour();
    Virtual<uint32_t>(browser.face, put, flag, colour);
}

// The filters: every pair of 0x1342 with a text value (the vector's start
// read each time; a null value is called as the game's is), its text
// "name:number:{group,instance}" cut at the colons (SplitOff 0x0090E98C,
// each part copied by 0x00408BE0), the number scanned as "%d" (0 when
// not), the image (the rest as "{%x,%x}") loaded by 0x0094C4E0 and its key
// put under the window's index + 1 by 0x0094C1DA, the face's slot 21 with
// the name, the image, the number and 0; the image released.
void ReadFilters(Browser& browser, Text& text)
{
    char* const* const vector = browser.vector;
    const uint32_t count = static_cast<uint32_t>(static_cast<int32_t>(vector[1] - vector[0]) >> 3);
    if (!count)
        return;
    const char* const colon = reinterpret_cast<const char*>(addr::cGZUIScriptWidgets_FilterSeparator);
    uint32_t i = 0;
    do {
        char* const pair = vector[0] + i * 8;
        if (At<uint32_t>(pair, 0) == kFilterKey && Virtual<uint32_t>(At<void*>(pair, 4), kValueKind) == kTextKind) {
            Virtual<uint32_t>(At<void*>(vector[0] + i * 8, 4), kValueText, static_cast<void*>(text.bytes));
            Text name;
            Text part;
            CallThis<void*>(addr::SC4String_Substring, name.bytes,
                            CallThis<void*>(addr::SC4String_SplitOff, text.bytes, part.bytes, colon), 0u,
                            0xFFFFFFFFu);
            part.End(0);
            Text number;
            CallThis<void*>(addr::SC4String_Substring, number.bytes,
                            CallThis<void*>(addr::SC4String_SplitOff, text.bytes, part.bytes, colon), 0u,
                            0xFFFFFFFFu);
            part.End(0);
            int32_t value = 0;
            Scan(number.Begin(), addr::SC4_IntFormat, &value);
            void* image = nullptr;
            uint32_t group;
            uint32_t instance;
            if (Scan(text.Begin(), addr::cGZUIScriptWidgets_GenImageFormat, &group, &instance) == 2) {
                Regs regs = {group, 0, 0, 0, 0};
                image = reinterpret_cast<void*>(static_cast<std::uintptr_t>(
                    CallRegs(addr::cGZUIScriptProps_LoadImage, &regs, instance, 0, 0, 0)));
                regs = {group, 0, Word32(browser.window), 0, 0};
                CallRegs(addr::cGZUIScriptProps_MakePair, &regs, instance, i + 1, 0, 0);
            }
            Virtual<uint32_t>(browser.face, kAddFilter, static_cast<void*>(name.bytes), image, value, 0u);
            if (image)
                Virtual<uint32_t>(image, 8);
            number.End(0);
            name.End(0);
        }
    } while (++i < count);
}

// The set pass (the third argument's byte set).
void ReadAll(Browser& browser)
{
    void* const face = browser.face;
    Text path;
    path.Make();
    if (browser.FindText(path, kPathKey))
        Virtual<uint32_t>(face, kSetPath, static_cast<void*>(path.bytes));

    // The default selection: its kind and its name's and extension's texts.
    Text selectionName;
    selectionName.Make();
    Text selectionExt;
    selectionExt.Make();
    browser.FindText(selectionName, kSelectionNameKey);
    browser.FindText(selectionExt, kSelectionExtKey);
    uint32_t value;
    if (browser.Choose(kSelectionKey, kSelections, value))
        Virtual<uint32_t>(face, kSetSelection, value, static_cast<void*>(selectionName.bytes),
                          static_cast<void*>(selectionExt.bytes));

    // The file sorts (primary, secondary), the directory sort.
    uint32_t sorts[2] = {};
    Virtual<uint32_t>(face, kGetSort, &sorts[0], &sorts[1]);
    for (int i = 0; i < 2; ++i)
        browser.Choose(kSortKeys[i], kSorts, sorts[i]);
    Virtual<uint32_t>(face, kSetSort, sorts[0], sorts[1]);
    if (browser.Choose(kDirSortKey, kDirSorts, value))
        Virtual<uint32_t>(face, kSetDirSort, value);

    // The three columns' contents.
    uint32_t columns[3] = {};
    Virtual<uint32_t>(face, kGetColumns, &columns[0], &columns[1], &columns[2]);
    for (int i = 0; i < 3; ++i)
        browser.Choose(kColumnKeys[i], kColumns, columns[i]);
    Virtual<uint32_t>(face, kSetColumns, columns[0], columns[1], columns[2]);

    Text text;
    text.Make();
    for (const Names& names : kNames)
        ReadNames(browser, names, text);

    browser.SetWord(kIconWidthKey, kSetIconWidth);
    browser.SetWord(kExtWidthKey, kSetExtWidth);
    browser.SetWord(kRowHeightKey, kSetRowHeight);
    uint32_t flag = 0;
    uint32_t colour = 0;
    ReadFlagColour(browser, kGetLineGrid, kSetLineGrid, kLineGridKey, kLineColourKey, flag, colour);
    ReadFlagColour(browser, kGetColumnGrid, kSetColumnGrid, kColumnGridKey, kColumnColourKey, flag, colour);

    // The highlight's two colours.
    uint32_t highlight[2] = {};
    Virtual<uint32_t>(face, kGetHighlight, &highlight[0], &highlight[1]);
    for (int i = 0; i < 2; ++i) {
        if (browser.ReadBytes(kHighlightKeys[i]))
            highlight[i] = browser.Colour();
    }
    Virtual<uint32_t>(face, kSetHighlight, highlight[0], highlight[1]);

    if (browser.FindText(path, kBackTextKey))
        Virtual<uint32_t>(face, kSetBackText, static_cast<void*>(path.bytes));
    browser.SetWord(kDriveSizeKey, kSetDriveSize);
    if (browser.ReadBytes(kBackColourKey)) {
        const uint32_t colour = browser.Colour();
        Virtual<uint32_t>(face, kSetBackColour, colour);
    }
    browser.SetBool(kTransparentKey, kSetTransparent);
    browser.SetBool(kFlagF019Key, kSetFlagF019);

    // The background image (the text "{%x,%x}": the group and instance
    // loaded by 0x0094C4E0, given to the face, the key's two words put
    // under the window's 0 by 0x0094C1DA, the image released), then the
    // filters, both only when the text is there.
    if (browser.FindText(path, kImageKey)) {
        uint32_t group;
        uint32_t instance;
        if (Scan(path.Begin(), addr::cGZUIScriptWidgets_GenImageFormat, &group, &instance) == 2) {
            Regs regs = {group, 0, 0, 0, 0};
            void* const image = reinterpret_cast<void*>(
                static_cast<std::uintptr_t>(CallRegs(addr::cGZUIScriptProps_LoadImage, &regs, instance, 0, 0, 0)));
            if (image)
                Virtual<uint32_t>(face, kSetImage, image);
            regs = {group, 0, Word32(browser.window), 0, 0};
            CallRegs(addr::cGZUIScriptProps_MakePair, &regs, instance, 0, 0, 0);
            if (image)
                Virtual<uint32_t>(image, 8);
        }
        ReadFilters(browser, path);
    }
    text.End(0);
    selectionExt.End(0);
    selectionName.End(0);
    path.End(0);
}

// The game's 0x00954212 (cdecl: the window, its properties' vector of
// pairs and a byte): the window's IGZWinFileBrowser (QueryInterface
// 0x3AA52E64); with it and the byte set the set pass; the interface
// released (its EAX answered; the QueryInterface's EAX when it gave none).
uint32_t __cdecl FileBrowserRead(void* window, char* const* vector, uint32_t set)
{
    OPENSC4_FIRST("FileBrowserRead");
    Browser browser = {window, vector, nullptr, {0, 0, 0}};
    uint32_t eax = Virtual<uint32_t>(window, 0, kBrowserInterface, &browser.face);
    if (Yes(eax) && (set & 0xFF))
        ReadAll(browser);
    if (browser.face)
        eax = Virtual<uint32_t>(browser.face, 8);
    return eax;
}

// ====================================================================================
// GZWinOptGrp's registration (part u15)

// GZWinOptGrp (0x00957C88)
const Word kOptGrpNames[] = {
    {0x00AD66F4, 0xA1336CC1}, // "GZWinOptGrp"
    {0x00AD66E4, 0xA1336CC0}, // "IGZWinOptGrp"
};
const Word kOptGrpWords[] = {
    {0x00AD66D8, 0x00000F00}, // "maincaption"
    {0x00AD66CC, 0x00000F01}, // "itemcaption"
    {0x00AD66C0, 0x00000F02}, // "drawfill"
    {0x00AD66B4, 0x00000F03}, // "labelfont"
    {0x00AD66A8, 0x00000F04}, // "buttonfont"
    {0x00AD6690, 0x00000F05}, // "labelfontcolornormal"
    {0x00AD6678, 0x00000F06}, // "labelfontcolordisabled"
    {0x00AD6660, 0x00000F07}, // "buttonfontcolornormal"
    {0x00AD6648, 0x00000F08}, // "buttonfontcolorhilited"
    {0x00AD6630, 0x00000F09}, // "buttonfontcolordisabled"
    {0x00AD661C, 0x00000F0A}, // "captiontextresid"
    {0x00AD6610, 0x00000F0B}, // "backimage"
    {0x00AD6600, 0x00000F0C}, // "autofilltype"
    {0x00ABBE78, 0x00000F0D}, // "option"
    {0x00AD65F4, 0x00000F0E}, // "automatic"
    {0x00AD65EC, 0x00000F0F}, // "manual"
    {0x00AD65DC, 0x00000F10}, // "autofillsize"
    {0x00AD65CC, 0x00000F11}, // "optionmoveto"
    {0x00AD65BC, 0x00000F12}, // "optionsetsize"
};
const Widget kOptGrp = {kOptGrpNames, 2, 0xA1336CC0, 0x00956C46, 0xA1336CC1, 0xA1336CC0, kOptGrpWords, 19};

// The detour (void; EAX as the game's leaves it: the last string's end).
uint32_t __cdecl RegisterOptGrp()
{
    OPENSC4_FIRST("RegisterOptGrp");
    return Register(kOptGrp);
}

// ====================================================================================
// The property readers' common calls (parts u15 on)

// The window's slots the readers call (byte offsets).
constexpr std::size_t kWinInit = 0x10;      // slot 4
constexpr std::size_t kWinChild = 0x38;     // slot 14 (a parent's: the child)
constexpr std::size_t kWinShown = 0x5C;     // slot 23
constexpr std::size_t kWinSize = 0xD4;      // slot 53 (two words)
constexpr std::size_t kWinArea = 0xD8;      // slot 54 (a rectangle)
constexpr std::size_t kWinPlace = 0xE0;     // slot 56 (two words)
constexpr std::size_t kWinUser = 0x100;     // slot 64 (a word)
constexpr std::size_t kWinFlag = 0x110;     // slot 68 (mask, bool)
constexpr std::size_t kWinCaption = 0x128;  // slot 74 (a string)
constexpr std::size_t kWinColour = 0x1AC;   // slot 107 (three bytes)
constexpr std::size_t kWinPut = 0x1C8;      // slot 114 (key, script value)

// The helpers of part u12 by address (ours when their groups are on), the
// registers each reads set as the game's callers have them.
uint32_t FindWordIn(char* const* vector, uint32_t key, uint32_t* word, uint32_t* edx = nullptr)
{
    Regs regs = {0, 0, 0, Word32(vector), 0};
    const uint32_t eax = CallRegs(addr::cGZUIScriptProps_FindWord73, &regs, key, Word32(word), 0, 0);
    if (edx)
        *edx = regs.edx;
    return eax;
}

bool FindTextIn(char* const* vector, Text& text, uint32_t key)
{
    Regs regs = {0, Word32(text.bytes), 0, Word32(vector), 0};
    return Yes(CallRegs(addr::cGZUIScriptProps_FindText, &regs, key, 0, 0, 0));
}

bool ReadPairIn(char* const* vector, uint32_t key, uint32_t* first, uint32_t* second)
{
    Regs regs = {0, 0, 0, Word32(vector), 0};
    return Yes(CallRegs(addr::cGZUIScriptProps_ReadPair, &regs, key, Word32(first), Word32(second), 0));
}

bool ReadBytesIn(char* const* vector, uint32_t key, uint32_t* first, uint32_t* second, uint32_t* third)
{
    Regs regs = {0, 0, 0, Word32(vector), 0};
    return Yes(CallRegs(addr::cGZUIScriptProps_ReadBytes, &regs, key, Word32(first), Word32(second), Word32(third)));
}

bool HasKeyIn(char* const* vector, uint32_t key)
{
    Regs regs = {Word32(vector), 0, 0, Word32(vector), 0};
    return Yes(CallRegs(addr::cGZUIScriptProps_HasKey, &regs, key, 0, 0, 0));
}

// 0x0094C58D: the text of (0x2026960B, group, instance) into the string.
bool LoadTextIn(Text& text, uint32_t group, uint32_t instance)
{
    Regs regs = {group, 0, 0, Word32(text.bytes), 0};
    return Yes(CallRegs(addr::cGZUIScriptProps_LoadText, &regs, instance, 0, 0, 0));
}

// 0x0094C4E0: the image of (group, instance), or none.
void* LoadImageOf(uint32_t group, uint32_t instance)
{
    Regs regs = {group, 0, 0, 0, 0};
    return reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(CallRegs(addr::cGZUIScriptProps_LoadImage, &regs, instance, 0, 0, 0)));
}

// 0x0094C1DA: the two words put under the key through the window.
void MakePairOn(void* window, uint32_t group, uint32_t instance, uint32_t key)
{
    Regs regs = {group, 0, Word32(window), 0, 0};
    CallRegs(addr::cGZUIScriptProps_MakePair, &regs, instance, key, 0, 0);
}

// A new script value (0x14 bytes; none without memory) made by its
// constructor (thiscall, returning it) with the arguments.
template <typename... Args>
void* NewValue(std::uintptr_t constructor, Args... args)
{
    void* const memory = Cdecl<void*>(addr::SC4_operator_new, std::size_t{0x14});
    return memory ? CallThis<void*>(constructor, memory, args...) : nullptr;
}

// The colour of three words (the colour service's slot 34).
uint32_t ColourOf(uint32_t first, uint32_t second, uint32_t third)
{
    void* const service = Cdecl<void*>(addr::SC4_Singleton913C1A);
    return Virtual<uint32_t>(service, kColour, first, second, third);
}

// ====================================================================================
// The base window's property handler (part u15)

// Flags the set pass gives first (mask, bool), then the keys whose word
// gives a mask (on when not 0, off when 0), in the game's order.
struct Flag {
    uint32_t key;
    uint32_t mask;
};

const uint32_t kBaseFixed[][2] = {{1, 1}, {0x10000, 1}, {0x800, 0}, {0x20000, 1}, {0x40000, 1}, {4, 0}, {0x8000, 1}};
const Flag kBaseSetFlags[] = {{0xF01A, 1},      {0xF01C, 0x100},  {0xF01D, 0x200},     {0xF01E, 0x800},
                              {0xF025, 0x8000}, {0xF026, 4},      {0xF023, 0x80000},   {0xF024, 0x200000}};
const Flag kBaseClearFlags[] = {{0xF01B, 2}, {0xF01F, 0x10000}, {0xF020, 0x20000}, {0xF021, 0x40000}, {0xF022, 0x100000}};

// A key's word (0x0094C675, into the byte's own argument slot) given as
// a bool to the window's slot 68 with the mask; the bool pushed over the
// slot's address, as the game's ECX holds it. EAX as the game's last left it.
uint32_t BaseFlag(void* window, char* const* vector, uint32_t* slot, const Flag& flag)
{
    uint32_t eax = FindWordIn(vector, flag.key, slot);
    if (Yes(eax))
        eax = Virtual<uint32_t>(window, kWinFlag, flag.mask, (Word32(slot) & ~0xFFu) | (*slot != 0 ? 1u : 0u));
    return eax;
}

// The set pass. `slot` is the byte's argument slot, which the game uses
// for the found words, the pairs' second words, a colour's third byte and
// the list object.
uint32_t BaseSet(void* window, char* const* vector, uint32_t* slot)
{
    for (const auto& fixed : kBaseFixed)
        Virtual<uint32_t>(window, kWinFlag, fixed[0], fixed[1]);
    for (const Flag& flag : kBaseSetFlags)
        BaseFlag(window, vector, slot, flag);
    if (Yes(FindWordIn(vector, 0x100, slot)))
        Virtual<uint32_t>(window, kWinUser, *slot);

    // The area: the text of 0x101 as "( %d, %d, %d, %d)"; without it the
    // pairs of 0x102 (slot 56) and 0x103 (slot 53). The game's rectangle
    // keeps what its stack held where the text has no number (ours 0).
    Text text;
    text.Make();
    uint32_t middle = 0; // the pairs' first word and the colour's second byte share a place
    if (FindTextIn(vector, text, 0x101)) {
        int32_t rect[4] = {};
        Scan(text.Begin(), addr::cGZUIScriptWidgets_BaseRectFormat, &rect[0], &rect[1], &rect[2], &rect[3]);
        Virtual<uint32_t>(window, kWinArea, static_cast<void*>(rect));
    } else {
        if (ReadPairIn(vector, 0x102, &middle, slot))
            Virtual<uint32_t>(window, kWinPlace, middle, *slot);
        if (ReadPairIn(vector, 0x103, &middle, slot))
            Virtual<uint32_t>(window, kWinSize, middle, *slot);
    }
    uint32_t low = 0;
    if (ReadBytesIn(vector, 0x104, &low, &middle, slot))
        Virtual<uint32_t>(window, kWinColour, low, middle, *slot);
    if (FindTextIn(vector, text, 0x105))
        Virtual<uint32_t>(window, kWinCaption, static_cast<void*>(text.bytes));

    // The caption's resource: the text of 0x106 as "{%x,%x}" (not
    // checked), the text of the key (0x0094C58D) as the caption, the key
    // put under 0x0B31771A (0x0094C1A4); the two words under 0x3ACA6906
    // in any case.
    uint32_t group = 0;
    uint32_t instance = 0;
    if (FindTextIn(vector, text, 0x106)) {
        Scan(text.Begin(), addr::cGZUIScriptWidgets_GenImageFormat, &group, &instance);
        Text caption;
        caption.Make();
        if (LoadTextIn(caption, group, instance))
            Virtual<uint32_t>(window, kWinCaption, static_cast<void*>(caption.bytes));
        const uint32_t key[3] = {0x2026960B, group, instance};
        Regs regs = {Word32(key), Word32(text.bytes), Word32(window), Word32(caption.bytes), 0};
        CallRegs(addr::cGZUIScriptProps_MakeKey, &regs, 0x0B31771A, Word32(key), 0, 0);
        caption.End(0);
    }
    const uint32_t words[2] = {group, instance};
    Virtual<uint32_t>(window, kWinPut, 0x3ACA6906u,
                      NewValue(addr::SC4ScriptNumber_FromBytes, static_cast<const void*>(words), 8u, 0x8000u));
    if (HasKeyIn(vector, 0x107))
        Virtual<uint32_t>(window, kWinFlag, 0x20000u, 1u);

    // The eight texts of 0x108 to 0x10F (each pushed, found or not) in a
    // list object (vtable 0x00AD5420, its vector at +4) given to a new
    // script value's slot 61, the value put under 0xBACA6879.
    Text item;
    item.Make();
    char* list = static_cast<char*>(Cdecl<void*>(addr::SC4_operator_new, std::size_t{0x14}));
    if (list) {
        At<std::uintptr_t>(list, 0) = addr::cGZUIScriptWidgets_BaseListVtable;
        for (std::size_t offset = 4; offset < 0x14; offset += 4)
            At<uint32_t>(list, offset) = 0;
    }
    *slot = Word32(list);
    Virtual<uint32_t>(reinterpret_cast<void*>(static_cast<std::uintptr_t>(*slot)), 4);
    for (uint32_t key = 0x108; key <= 0x10F; ++key) {
        FindTextIn(vector, item, key);
        CallThis<uint32_t>(addr::SC4StringVector_PushBack, reinterpret_cast<char*>(static_cast<std::uintptr_t>(*slot)) + 4,
                           static_cast<void*>(item.bytes));
    }
    void* const value = NewValue(addr::SC4ScriptNumber_Construct);
    Virtual<uint32_t>(value, 4);
    list = reinterpret_cast<char*>(static_cast<std::uintptr_t>(*slot));
    Virtual<uint32_t>(value, 0xF4, static_cast<void*>(list));
    Virtual<uint32_t>(window, kWinPut, 0xBACA6879u, value);
    Virtual<uint32_t>(value, 8);
    uint32_t eax = Virtual<uint32_t>(list, 8);
    eax = item.End(eax);
    return text.End(eax);
}

// The game's 0x0095660D (cdecl: the window, its properties' vector and a
// byte): with the byte set (the dispatcher's first call) the window's
// defaults, flags, user word, area, colour, caption, resource key and
// text list; with it clear (the second call, after the window's slot 23)
// five more flags. EAX as the game's last call left it.
uint32_t __cdecl BaseReadBody(uint32_t* arguments)
{
    OPENSC4_FIRST("BaseRead");
    void* const window = reinterpret_cast<void*>(static_cast<std::uintptr_t>(arguments[0]));
    char* const* const vector = reinterpret_cast<char* const*>(static_cast<std::uintptr_t>(arguments[1]));
    uint32_t* const slot = &arguments[2];
    if (*slot & 0xFF)
        return BaseSet(window, vector, slot);
    uint32_t eax = 0;
    for (const Flag& flag : kBaseClearFlags)
        eax = BaseFlag(window, vector, slot, flag);
    return eax;
}

// The thunk hands the body the argument slots (the handler writes the
// third and passes its address on).
__declspec(naked) void BaseRead()
{
    __asm lea eax, [esp + 4]
    __asm push eax
    __asm call BaseReadBody
    __asm add esp, 4
    __asm ret
}

// ====================================================================================
// GZWinOptGrp's property handler (part u15)

// The interface the handler asks the window for (IGZWinOptGrp) and the
// slots it calls on it (byte offsets).
constexpr uint32_t kOptGrpInterface = 0xA1336CC0;
constexpr std::size_t kOptWindow = 0xC;      // slot 3 (its window)
constexpr std::size_t kOptFlag = 0x14;       // slot 5 (mask, bool)
constexpr std::size_t kOptPair = 0x18;       // slot 6 (two words)
constexpr std::size_t kOptLabelFont = 0x20;  // slot 8 (font)
constexpr std::size_t kOptButtonFont = 0x24; // slot 9 (font)
constexpr std::size_t kOptColour = 0x28;     // slot 10 (index, colour)
constexpr std::size_t kOptSize = 0x38;       // slot 14 (word)
constexpr std::size_t kOptAdd = 0x3C;        // slot 15 (id, name, number, image, rectangle)
constexpr std::size_t kOptMove = 0x44;       // slot 17 (id, x, y)
constexpr std::size_t kOptResize = 0x48;     // slot 18 (id, x, y)
constexpr std::size_t kOptItem = 0x4C;       // slot 19 (id, item*)
constexpr std::size_t kOptBackImage = 0x50;  // slot 20 (image)
constexpr std::size_t kOptColours = 0x54;    // slot 21 (four colours)
constexpr std::size_t kOptFill = 0x60;       // slot 24
constexpr std::size_t kOptAutoFill = 0x70;   // slot 28 (automatic, size)

// The flags (on when the word is 1, else off), the two fonts.
const Flag kOptFlags[] = {{0xF00, 1}, {0xF01, 2}, {0xF02, 4}, {0xF019, 8}};

struct Font {
    uint32_t key;
    std::size_t slot;
    uint32_t makeKey;
};

const Font kOptFonts[] = {{0xF03, kOptLabelFont, 0}, {0xF04, kOptButtonFont, 1}};

// The set pass (the byte set). `arguments` are the handler's: the game
// reuses the window's slot for the four-colour services and the autofill
// words.
void OptGrpSet(uint32_t* arguments, void* window, char* const* vector, void* face)
{
    // The word's place is also the colours' first byte and, after the
    // caption, the second of the words put under 0x4A4AF4AC; ours starts
    // at 0 where the game's holds what its stack held.
    uint32_t word = 0;
    for (const Flag& flag : kOptFlags) {
        uint32_t edx;
        if (Yes(FindWordIn(vector, flag.key, &word, &edx)))
            Virtual<uint32_t>(face, kOptFlag, flag.mask, (edx & ~0xFFu) | (word == 1 ? 1u : 0u));
    }
    uint32_t first;
    uint32_t second;
    if (ReadPairIn(vector, 0xF004, &first, &second))
        Virtual<uint32_t>(face, kOptPair, first, second);

    // The fonts: a number (the font service's slot 5, put under the
    // window's 0 or 1 by 0x0094C222) or else a text (by 0x0094C255).
    Text label;
    label.Make();
    for (const Font& font : kOptFonts) {
        if (Yes(FindWordIn(vector, font.key, &word))) {
            void* const service = Cdecl<void*>(addr::SC4_Singleton913C72);
            const uint32_t made = Virtual<uint32_t>(service, kFont, word);
            Virtual<uint32_t>(face, font.slot, made);
            Regs regs = {0, 0, Word32(window), 0, 0};
            CallRegs(addr::cGZUIScriptProps_MakeNumber, &regs, word, font.makeKey, 0, 0);
        } else if (FindTextIn(vector, label, font.key)) {
            Regs regs = {Word32(label.bytes), Word32(label.bytes), Word32(window), Word32(vector), 0};
            CallRegs(addr::cGZUIScriptProps_MakeText, &regs, Word32(label.bytes), font.makeKey, 0, 0);
        }
    }

    // Five colours (0 to 4). The second and third bytes' places are not
    // written before (ours 0 above the low bytes; the game's what its
    // stack held).
    uint32_t green = 0;
    uint32_t blue = 0;
    for (uint32_t i = 0; i < 5; ++i) {
        if (ReadBytesIn(vector, 0xF05 + i, &word, &green, &blue)) {
            const uint32_t colour = ColourOf(word, green, blue);
            Virtual<uint32_t>(face, kOptColour, i, colour);
        }
    }

    // The caption's resource: the text of 0xF0A as "{%x,%x}" (not
    // checked; ours 0 where the game keeps its stack's words), the text of
    // the key given to the face's window's caption slot, the two words
    // put under 0x4A4AF4AC.
    Text caption;
    caption.Make();
    if (FindTextIn(vector, caption, 0xF0A)) {
        uint32_t group = 0;
        uint32_t instance = 0;
        Scan(caption.Begin(), addr::cGZUIScriptWidgets_GenImageFormat, &group, &instance);
        Text text;
        text.Make();
        if (LoadTextIn(text, group, instance)) {
            void* const own = Virtual<void*>(face, kOptWindow);
            Virtual<uint32_t>(own, kWinCaption, static_cast<void*>(text.bytes));
        }
        word = instance;
        const uint32_t words[2] = {group, word};
        Virtual<uint32_t>(window, kWinPut, 0x4A4AF4ACu,
                          NewValue(addr::SC4ScriptNumber_FromBytes, static_cast<const void*>(words), 8u, 0x8000u));
        text.End(0);
    }

    // The back image: the text of 0xF0B as "{%x,%x}", loaded (0x0094C4E0)
    // for slot 20, its two words put under the window's 0; released.
    Text back;
    back.Make();
    uint32_t group;
    uint32_t instance;
    if (FindTextIn(vector, back, 0xF0B) &&
        Scan(back.Begin(), addr::cGZUIScriptWidgets_GenImageFormat, &group, &instance) == 2) {
        void* const image = LoadImageOf(group, instance);
        if (image)
            Virtual<uint32_t>(face, kOptBackImage, image);
        MakePairOn(window, group, instance, 0);
        if (image)
            Virtual<uint32_t>(image, 8);
    }

    // Four colours of 0x40B, "(%d,%d,%d)" four times; the colour service
    // asked four times first (the last kept in the window's slot), the
    // colours made last to first, given to slot 21.
    int32_t c[12];
    if (FindTextIn(vector, back, 0x40B) &&
        Scan(back.Begin(), addr::cGZUIScriptWidgets_OptGrpColourFormat, &c[0], &c[1], &c[2], &c[3], &c[4], &c[5],
             &c[6], &c[7], &c[8], &c[9], &c[10], &c[11]) == 12) {
        void* services[4];
        for (void*& service : services)
            service = Cdecl<void*>(addr::SC4_Singleton913C1A);
        arguments[0] = Word32(services[3]);
        uint32_t colours[4];
        for (int i = 3; i >= 0; --i) {
            colours[i] = Virtual<uint32_t>(services[3 - i], kColour, static_cast<uint32_t>(c[3 * i]),
                                           static_cast<uint32_t>(c[3 * i + 1]), static_cast<uint32_t>(c[3 * i + 2]));
        }
        Virtual<uint32_t>(face, kOptColours, colours[0], colours[1], colours[2], colours[3]);
    }

    // Autofill (0xF0C, into the window's slot): `automatic` (0xF0E) with
    // the size of 0xF10 (none without it), else manual.
    if (Yes(FindWordIn(vector, 0xF0C, &arguments[0]))) {
        if (arguments[0] != 0xF0E)
            Virtual<uint32_t>(face, kOptAutoFill, 1u, 0u);
        else if (Yes(FindWordIn(vector, 0xF10, &arguments[0])))
            Virtual<uint32_t>(face, kOptAutoFill, 0u, arguments[0]);
    }
    back.End(0);
    caption.End(0);
    label.End(0);
}

// A text value's next part: what stands before the first ':' cut off the
// text (SplitOff 0x0090E98C) and assigned to the part (0x00424380).
void CutInto(Text& text, Text& part)
{
    Text cut;
    CallThis<void*>(addr::SC4String_SplitOff, text.bytes, cut.bytes,
                    reinterpret_cast<const char*>(addr::cGZUIScriptWidgets_FilterSeparator));
    CallThis<uint32_t>(addr::cRZBaseString_Assign, part.bytes, static_cast<void*>(cut.bytes));
    cut.End(0);
}

// The pair's value, when the pair has the key and a text value (slot 3
// answers 0x800E; a null value is called as the game's is), its text read
// into the string (the vector's start read again).
bool PairText(char* const* vector, uint32_t i, uint32_t key, Text& text)
{
    char* const pair = vector[0] + i * 8;
    if (At<uint32_t>(pair, 0) != key || Virtual<uint32_t>(At<void*>(pair, 4), kValueKind) != kTextKind)
        return false;
    Virtual<uint32_t>(At<void*>(vector[0] + i * 8, 4), kValueText, static_cast<void*>(text.bytes));
    return true;
}

// The raw pass (the byte clear): the options (0xF0D,
// "id:name:number:{group,instance}:(x,y,w,h)"), their moves (0xF11) and
// sizes (0xF12) ("id:(x,y)"), the size word 0x501 and the fill flag 0x304.
void OptGrpRaw(void* window, char* const* vector, void* face)
{
    Text part;
    part.Make();
    Text name;
    name.Make();
    Text text;
    text.Make();
    // The game keeps these across the pairs; the id and number start as its
    // stack held them (ours 0), the key's words at 0.
    int32_t id = 0;
    int32_t number = 0;
    uint32_t group = 0;
    uint32_t instance = 0;
    uint32_t count = static_cast<uint32_t>(static_cast<int32_t>(vector[1] - vector[0]) >> 3);
    for (uint32_t i = 0; i < count; ++i) {
        if (!PairText(vector, i, 0xF0D, text))
            continue;
        CutInto(text, part);
        Scan(part.Begin(), addr::SC4_IntFormat, &id);
        CutInto(text, name);
        CutInto(text, part);
        Scan(part.Begin(), addr::SC4_IntFormat, &number);
        void* image = nullptr;
        CutInto(text, part);
        if (Scan(part.Begin(), addr::cGZUIScriptWidgets_GenImageFormat, &group, &instance) == 2)
            image = LoadImageOf(group, instance);
        int32_t rect[4] = {0, 0, 1, 1};
        CallThis<uint32_t>(addr::cRZBaseString_Assign, part.bytes, static_cast<void*>(text.bytes));
        int32_t read[4];
        if (Scan(part.Begin(), addr::cGZUIScriptProps_RectFormat, &read[0], &read[1], &read[2], &read[3]) == 4) {
            for (int k = 0; k < 4; ++k)
                rect[k] = read[k];
        }
        Virtual<uint32_t>(face, kOptAdd, id, static_cast<void*>(name.bytes), number, image, static_cast<void*>(rect));
        void* item = nullptr;
        if (Yes(Virtual<uint32_t>(face, kOptItem, id, &item))) {
            void* const own = Virtual<void*>(item, kOptWindow);
            MakePairOn(own, group, instance, 0);
            Virtual<uint32_t>(item, 8);
        }
        if (image)
            Virtual<uint32_t>(image, 8);
    }

    count = static_cast<uint32_t>(static_cast<int32_t>(vector[1] - vector[0]) >> 3);
    for (uint32_t i = 0; i < count; ++i) {
        const struct {
            uint32_t key;
            std::size_t slot;
        } moves[] = {{0xF11, kOptMove}, {0xF12, kOptResize}};
        for (const auto& move : moves) {
            if (!PairText(vector, i, move.key, text))
                continue;
            CutInto(text, part);
            Scan(part.Begin(), addr::SC4_IntFormat, &id);
            int32_t x;
            int32_t y;
            if (Scan(text.Begin(), addr::cGZUIScriptProps_PairFormat, &x, &y) == 2)
                Virtual<uint32_t>(face, move.slot, id, x, y);
        }
    }

    uint32_t size;
    if (Yes(FindWordIn(vector, 0x501, &size)))
        Virtual<uint32_t>(face, kOptSize, size);
    if (HasKeyIn(vector, 0x304)) {
        Virtual<uint32_t>(window, kWinPut, 0xDACF8F9Cu, NewValue(addr::SC4ScriptNumber_FromBool, 1u));
        Virtual<uint32_t>(face, kOptFill);
    } else {
        Virtual<uint32_t>(window, kWinPut, 0xDACF8F9Cu, NewValue(addr::SC4ScriptNumber_FromBool, 0u));
    }
    text.End(0);
    name.End(0);
    part.End(0);
}

// The game's 0x00956C46 (cdecl: the window, its properties' vector and a
// byte): the window's IGZWinOptGrp (QueryInterface 0xA1336CC0); with it the
// set pass (byte set) or the raw pass (clear); the interface released (its
// EAX answered; the QueryInterface's when it gave none).
uint32_t __cdecl OptGrpReadBody(uint32_t* arguments)
{
    OPENSC4_FIRST("OptGrpRead");
    void* const window = reinterpret_cast<void*>(static_cast<std::uintptr_t>(arguments[0]));
    char* const* const vector = reinterpret_cast<char* const*>(static_cast<std::uintptr_t>(arguments[1]));
    void* face = nullptr;
    uint32_t eax = Virtual<uint32_t>(window, 0, kOptGrpInterface, &face);
    if (Yes(eax)) {
        if (arguments[2] & 0xFF)
            OptGrpSet(arguments, window, vector, face);
        else
            OptGrpRaw(window, vector, face);
    }
    if (face)
        eax = Virtual<uint32_t>(face, 8);
    return eax;
}

// The thunk hands the body the argument slots (the set pass writes the
// first).
__declspec(naked) void OptGrpRead()
{
    __asm lea eax, [esp + 4]
    __asm push eax
    __asm call OptGrpReadBody
    __asm add esp, 4
    __asm ret
}

// ====================================================================================
// The LEGACY reader's dispatcher (part u15)

// The interface windows are made for (IGZWin) and the class made when the
// script's is not there.
constexpr uint32_t kWinInterface = 0x22BA0121;
constexpr uint32_t kDefaultClass = 0x478D1E6F;
constexpr std::size_t kComCreate = 0xC; // the COM's slot 3 (class, interface, out)

using Handler = uint32_t(__cdecl*)(void*, char* const*, uint32_t);

// A map's header (the map object's first word).
char* HeaderOf(std::uintptr_t map) { return *reinterpret_cast<char**>(map); }

// The game's 0x00957A9B (the LEGACY reader's slot 3, thiscall: the
// properties' vector; `ret 4`). The class (key -1; none or 0: nothing) and the
// interface (key -2, else the interface map's for the class); the window
// made (+0x10) by the COM for the class, else for the class whose
// interface it is, else for GZWinCustom; the class and interface put
// under 0xE85EEBDE and 0x08D6E462; +0x1C set; the class's handler (the
// handler map's for the interface) and the base handler with 1; the
// reader's object handed on (0x0094C77D); the window's Init; the window
// given to the parent (+0x14, released and cleared) or the window
// manager's top window; the window's slot 23; both handlers with 0. AL 0
// over the last call's EAX.
uint32_t __fastcall Dispatch(char* reader, void*, char* const* vector)
{
    OPENSC4_FIRST("Dispatch");
    uint32_t klass = 0;
    uint32_t eax = FindWordIn(vector, 0xFFFFFFFFu, &klass);
    if (!Yes(eax) || !klass)
        return eax & ~0xFFu;
    uint32_t face = 0;
    if (!Yes(FindWordIn(vector, 0xFFFFFFFEu, &face))) {
        char* const node = CallThis<char*>(addr::SC4FlagMap_Find, reinterpret_cast<void*>(addr::cGZUIScriptWidgets_Interfaces),
                                           static_cast<const void*>(&klass));
        if (node != HeaderOf(addr::cGZUIScriptWidgets_Interfaces))
            face = At<uint32_t>(node, 0x14);
    }
    void** const made = &At<void*>(reader, 0x10);
    *made = nullptr;
    eax = Virtual<uint32_t>(Cdecl<void*>(addr::RZGetCOM), kComCreate, klass, kWinInterface, static_cast<void*>(made));
    if (!Yes(eax)) {
        // The class whose interface it is (the first in the map's order).
        bool done = false;
        for (char* node = At<char*>(HeaderOf(addr::cGZUIScriptWidgets_Interfaces), 8);
             node != HeaderOf(addr::cGZUIScriptWidgets_Interfaces); node = Cdecl<char*>(addr::SC4Tree_Next, node)) {
            if (At<uint32_t>(node, 0x14) == face) {
                done = Yes(Virtual<uint32_t>(Cdecl<void*>(addr::RZGetCOM), kComCreate, At<uint32_t>(node, 0x10),
                                             kWinInterface, static_cast<void*>(made)));
                break;
            }
        }
        if (!done) {
            eax = Virtual<uint32_t>(Cdecl<void*>(addr::RZGetCOM), kComCreate, kDefaultClass, kWinInterface,
                                    static_cast<void*>(made));
            if (!Yes(eax))
                return eax & ~0xFFu;
        }
    }
    Virtual<uint32_t>(*made, kWinPut, 0xE85EEBDEu, NewValue(addr::SC4ScriptNumber_FromUint, klass));
    if (face)
        Virtual<uint32_t>(*made, kWinPut, 0x08D6E462u, NewValue(addr::SC4ScriptNumber_FromUint, face));
    At<uint8_t>(reader, 0x1C) = 1;
    Handler handler = nullptr;
    char* const node = CallThis<char*>(addr::cGZCOM_ClassMapFind, reinterpret_cast<void*>(addr::cGZUIScriptWidgets_Handlers),
                                       static_cast<const void*>(&face));
    if (node != HeaderOf(addr::cGZUIScriptWidgets_Handlers))
        handler = At<Handler>(node, 0x14);
    const Handler base = reinterpret_cast<Handler>(addr::cGZUIScriptWidgets_BaseRead);
    if (handler)
        handler(*made, vector, 1);
    base(*made, vector, 1);
    CallThis<uint32_t>(addr::cGZUIScriptLegacy_GiveObject, reader, *made, 1u);
    Virtual<uint32_t>(*made, kWinInit);
    if (At<void*>(reader, 0x14)) {
        Virtual<uint32_t>(At<void*>(reader, 0x14), kWinChild, *made);
        Virtual<uint32_t>(At<void*>(reader, 0x14), 8);
        At<void*>(reader, 0x14) = nullptr;
    } else {
        void* const manager = Cdecl<void*>(addr::SC4_GetWinManager);
        void* const top = Virtual<void*>(manager, 0xC);
        Virtual<uint32_t>(top, kWinChild, *made);
    }
    Virtual<uint32_t>(*made, kWinShown);
    if (handler)
        handler(*made, vector, 0);
    eax = base(*made, vector, 0);
    return eax & ~0xFFu;
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

bool InstallFileBrowser(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZUIScriptWidgets_RegisterFileBrowser, reinterpret_cast<void*>(&RegisterFileBrowser)},
    };
    return InstallAll(detours);
}

bool InstallFileBrowserRead(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZUIScriptWidgets_FileBrowserRead, reinterpret_cast<void*>(&FileBrowserRead)},
    };
    return InstallAll(detours);
}

bool InstallOptGrp(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZUIScriptWidgets_RegisterOptGrp, reinterpret_cast<void*>(&RegisterOptGrp)},
    };
    return InstallAll(detours);
}

bool InstallOptGrpRead(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZUIScriptWidgets_OptGrpRead, reinterpret_cast<void*>(&OptGrpRead)},
    };
    return InstallAll(detours);
}

bool InstallBaseRead(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZUIScriptWidgets_BaseRead, reinterpret_cast<void*>(&BaseRead)},
    };
    return InstallAll(detours);
}

bool InstallDispatch(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZUIScriptLegacy_Dispatch, reinterpret_cast<void*>(&Dispatch)},
    };
    return InstallAll(detours);
}

const bool g_registered[] = {
    registry::Register({"framework.uiscript.filebrowser", "framework/uiscript", registry::Kind::Detour,
                        registry::Phase::Attach, addr::cGZUIScriptWidgets_RegisterFileBrowser,
                        "Our registration of GZWinFileBrowser: its names and 66 property words into the LEGACY "
                        "readers' variable table, its handler and interface into the two maps (1 detour; part "
                        "u14, docs/gzui.md)",
                        false, InstallFileBrowser}),
    registry::Register({"framework.uiscript.filebrowserread", "framework/uiscript", registry::Kind::Detour,
                        registry::Phase::Attach, addr::cGZUIScriptWidgets_FileBrowserRead,
                        "Our property handler of GZWinFileBrowser (the file browser window): its path, default "
                        "selection, sorts, columns, fonts, colours, alignments, grids, background and filters (1 "
                        "detour; part u14, docs/gzui.md)",
                        false, InstallFileBrowserRead}),
    registry::Register({"framework.uiscript.optgrp", "framework/uiscript", registry::Kind::Detour,
                        registry::Phase::Attach, addr::cGZUIScriptWidgets_RegisterOptGrp,
                        "Our registration of GZWinOptGrp: its names and 19 property words into the LEGACY readers' "
                        "variable table, its handler and interface into the two maps (1 detour; part u15, "
                        "docs/gzui.md)",
                        false, InstallOptGrp}),
    registry::Register({"framework.uiscript.optgrpread", "framework/uiscript", registry::Kind::Detour,
                        registry::Phase::Attach, addr::cGZUIScriptWidgets_OptGrpRead,
                        "Our property handler of GZWinOptGrp (the option group): its flags, fonts, colours, caption, "
                        "back image and autofill, then its options, their moves and sizes (1 detour; part u15, "
                        "docs/gzui.md)",
                        false, InstallOptGrpRead}),
    registry::Register({"framework.uiscript.baseread", "framework/uiscript", registry::Kind::Detour,
                        registry::Phase::Attach, addr::cGZUIScriptWidgets_BaseRead,
                        "Our property handler of the base window, which every scripted window gets: its default "
                        "and scripted flags, area, colour, caption, resource key and text list (1 detour; part u15, "
                        "docs/gzui.md)",
                        false, InstallBaseRead}),
    registry::Register({"framework.uiscript.dispatch", "framework/uiscript", registry::Kind::Detour,
                        registry::Phase::Attach, addr::cGZUIScriptLegacy_Dispatch,
                        "Our LEGACY reader's window maker: a script's window made by its class, given its class "
                        "and interface, both property handlers twice, its Init and its parent (1 detour; part u15, "
                        "docs/gzui.md)",
                        false, InstallDispatch}),
};

} // namespace
} // namespace opensc4::gzui
