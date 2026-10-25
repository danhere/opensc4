// Our font system of the framework's fonts, f2a of docs/fonts.md ("f2a: the
// font system is ours"): class 0x20673960 (0x70 bytes, vtables 0x00AC5488
// and 0x00AC5454 at +4), the system service that finds the faces in the
// game's Fonts folder and Windows's, keeps a table of aliases and makes the
// fonts (0x1AA404AC) everything that draws text asks it for. Each is a
// detour on the game's function, in four groups, off by default and
// installed at Attach (the faces are found while the game starts):
//
//   framework.font.system.object   the constructor (0x00895268), the
//                                  destructor (0x00894F2C) and its deleting
//                                  slot 24 (0x0089538D), QueryInterface (slot
//                                  0, 0x00893D2C), Release (slot 2,
//                                  0x00893DD7) and what it calls at the last
//                                  reference (0x00893D68), Shutdown (slot 4,
//                                  0x00894F8D), SetEncoding (slot 5,
//                                  0x00893D7B), SetBlitter (slot 6,
//                                  0x00893D91), the version (0x00894F77) and
//                                  the +4 vtable's thunks to QueryInterface,
//                                  Release and Shutdown (0x00894F7D,
//                                  0x00894F85, 0x00895260)
//   framework.font.system.faces    a font file's type (0x0089433C),
//                                  AddFontFile (slot 14, 0x00895564),
//                                  AddFontFolder (slot 15, 0x00894864),
//                                  AddFont (slot 13, 0x008957D6), RemoveFace
//                                  (slot 17, 0x00894A3A), ReleaseFont (slot
//                                  12, 0x00893F22), ListFaces (slot 18,
//                                  0x00893E5B), ListFonts (slot 19,
//                                  0x00893ECB), HasFace (slot 22, 0x008944B2)
//   framework.font.system.aliases  Init (slot 3, 0x00895E68) and its thunk
//                                  (0x00895F2E), the default face of a family
//                                  (0x00895DCF), AddAlias (slot 20,
//                                  0x0089595D), GetAliasFaces (slot 21,
//                                  0x00894198), ResolveAlias (slot 23,
//                                  0x0089423C)
//   framework.font.system.getfont  GetFont (slot 11, 0x00894ADF), MakeFont
//                                  (slot 10, 0x00895A1C), GetFontFor (slot 9,
//                                  0x008942E3), SetMonoRange (slot 8,
//                                  0x008954CB)
//
// Slot 7 (0x00893D87, the default draw mode into +0x14) is shared with two
// other classes' vtables and is f2b's (font_system_tables.cpp); slot 1
// (AddRef, 0x0094C47C) and the +4 vtable's framework functions stay the
// game's. The STLport maps, lists and vectors of f2b the system keeps its
// tables in are called at the game's addresses (ours when their groups are
// on), as are the game's strings, sets of names, files and folders (cRZFile,
// cRZDirectory), the T2K holder (f5), the font class and the COM.
//
// The system (0x70 bytes): +0x08 a byte set by Init and cleared by
// Shutdown, +0x0C the references, +0x10 the class, +0x14 the draw mode
// fonts get by default, +0x18 and +0x1C the sizes drawn in mode 1, +0x20 the
// text encoding fonts get, +0x24 the blitter fonts get, +0x28 a set of the
// faces' names in lower case, +0x34 a map of a family's code (a word) to its
// default face, +0x40 the faces (a map of a file's path to: +0x24 a list of
// the fonts made of it, each with its style, +0x28 the face's name, +0x3C
// its style bits, +0x40 its kind (1, or 2 when T2K's scaler has +0x5C),
// +0x44 the file's type), +0x4C the aliases (a map of a name to a vector of
// face names), +0x58 a vector of the file patterns the folder scan looks
// for, +0x64 a map of a face's name to its own sizes drawn in mode 1.
//
// Compare mode runs ours and then the game's where both can run. What only
// reads compares its result (the version, a file's type, HasFace) or the
// bytes it wrote (QueryInterface, SetEncoding, ListFaces, ListFonts);
// GetAliasFaces and ResolveAlias assign their strings twice, the same text,
// and compare it. GetFont runs ours without its effects to its decision (a
// font it would reuse, or the face, size, style and reuse it would ask its
// slot 10 for) and then the game's, whose call to slot 10 is noted and
// compared. Init and AddFontFile run ours and the game's on two scratch
// systems made by the game's constructor and compare their tables, then
// the game's on the system it was called on. What makes fonts, releases
// them or adds to the tables otherwise runs the game's alone. While the
// game's side runs, our detours inside it pass through to the game's.
#include "core/address_check.h"
#include "core/compare.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/thunk.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

namespace opensc4::fontsystem {
namespace {

// ====================================================================================
// Helpers

using Address = uint32_t;

inline Address At(const void* p) { return static_cast<Address>(reinterpret_cast<uintptr_t>(p)); }
inline void* Ptr(Address at) { return reinterpret_cast<void*>(static_cast<uintptr_t>(at)); }

template <typename T>
inline T Load(Address at)
{
    T value;
    std::memcpy(&value, Ptr(at), sizeof value);
    return value;
}

template <typename T>
inline void Store(Address at, T value)
{
    std::memcpy(Ptr(at), &value, sizeof value);
}

template <typename R, typename... Args>
inline R Virtual(void* object, std::size_t byteOffset, Args... args)
{
    void** const vtable = *reinterpret_cast<void***>(object);
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(vtable[byteOffset / 4])(object, args...);
}

template <typename R, typename... Args>
inline R VirtualAt(Address object, std::size_t byteOffset, Args... args)
{
    return Virtual<R>(Ptr(object), byteOffset, args...);
}

// A thiscall game function: `self` in ECX, the rest on the stack.
template <typename R, typename... Args>
inline R This(std::uintptr_t fn, void* self, Args... args)
{
    return reinterpret_cast<R(__fastcall*)(void*, void*, Args...)>(fn)(self, nullptr, args...);
}

template <typename R, typename... Args>
inline R Cdecl(std::uintptr_t fn, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(fn)(args...);
}

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("fontsystem: first %s", name);
}

#define OPENSC4_FIRST(name)                                                                                    \
    do {                                                                                                       \
        static bool logged_ = false;                                                                           \
        if (!logged_)                                                                                          \
            FirstCall(name, logged_);                                                                          \
    } while (0)

// ====================================================================================
// The game's functions we call

// The game's string (cIGZString over an STLport body at +4: begin, end,
// capacity, and a word), 0x14 bytes.
struct String {
    Address vtable, begin, end, capacity, zero;
};
static_assert(sizeof(String) == 0x14);

// An STLport vector's three pointers.
struct Vector {
    Address begin, end, capacity;
};

constexpr std::uintptr_t kStringConstruct = 0x00578390;  // SC4String::SC4String, thiscall ()
constexpr std::uintptr_t kStringFromIGZ = 0x0040AE00;    // thiscall (cIGZString*)
constexpr std::uintptr_t kStringFromText = 0x00408480;   // thiscall (const char*)
constexpr std::uintptr_t kStringCopy = 0x00408BE0;       // thiscall (const String*, pos, count): a copy
constexpr std::uintptr_t kStringLower = 0x0090EAC5;      // thiscall ()
constexpr std::uintptr_t kStringAssign = 0x00424380;     // thiscall (const String*)
constexpr std::uintptr_t kStringDestroy = 0x00414250;    // thiscall ()
constexpr std::uintptr_t kStringFromBody = 0x00408B90;   // thiscall (const body*)
constexpr std::uintptr_t kBodyPushBack = 0x00417A70;     // thiscall on the body (char)
constexpr std::uintptr_t kBodyAppendText = 0x00410F00;   // thiscall on the body (const char*)
constexpr std::uintptr_t kBodyAppendChars = 0x004D8650;  // thiscall on the body (const char*, count)
constexpr std::uintptr_t kBodyAppendBody = 0x00408C20;   // thiscall on the body (const body*)
constexpr std::uintptr_t kBodyAssignChars = 0x0044D3A0;  // thiscall on the body (begin, end)
constexpr std::uintptr_t kBodyFind = 0x00442850;         // thiscall on the body (const char*, pos) -> pos, -1
constexpr std::uintptr_t kBytesFind = 0x004427F0;        // thiscall on the body (const char*, pos, count)
constexpr std::uintptr_t kBodyConcat = 0x00408AE0;       // cdecl (body* out, const body*, const body*)
constexpr std::uintptr_t kBodyEquals = 0x0043AB70;       // cdecl (const body*, const body*) -> bool
constexpr std::uintptr_t kBodyEqualsText = 0x00418A40;   // cdecl (const body*, const char*) -> bool
constexpr std::uintptr_t kStringFromChars = 0x00911358;  // cdecl (const char*, count, encoding, String*)
constexpr std::uintptr_t kStringConvert = 0x0091152F;    // cdecl (const String*, String*, encoding)
constexpr std::uintptr_t kSplitText = 0x00910BD3;        // cdecl (text, count, separator, String[], uint32_t* n)
constexpr std::uintptr_t kIsSeparator = 0x005F8FE0;      // cdecl (int) -> bool: '\' or '/'
constexpr std::uintptr_t kFileExists = 0x00919E96;       // cdecl (const String*) -> bool
constexpr std::uintptr_t kFileExtension = 0x00918417;    // cdecl (const cIGZString*, String*)
constexpr std::uintptr_t kDestroyArray = 0x0041D3C0;     // stdcall (array, size, count, destructor)
constexpr std::uintptr_t kTreeNext = 0x0040D4C0;         // cdecl (node) -> next node in order
constexpr std::uintptr_t kBytesMake = 0x008940A7;        // thiscall (count, byte): a vector of count bytes
constexpr std::uintptr_t kDefaultEncoding = 0x00940DA8;  // cdecl () -> RZText's default encoding

// cRZFile and cRZDirectory.
constexpr std::uintptr_t kFileConstruct = 0x00918C00;  // thiscall (const cIGZString* path)
constexpr std::uintptr_t kFileOpen = 0x00919B00;       // thiscall (access, creation, share) -> bool
constexpr std::uintptr_t kFileRead = 0x00918382;       // thiscall (buffer, count) -> bool
constexpr std::uintptr_t kFileClose = 0x009188A5;      // thiscall ()
constexpr std::uintptr_t kFileDestruct = 0x00918C70;   // thiscall ()
constexpr std::uintptr_t kDirConstruct = 0x0091BD31;   // thiscall (const String* folder, 0, 0, 0)
constexpr std::uintptr_t kDirPattern = 0x0091BCD7;     // thiscall (const String* pattern, 0)
constexpr std::uintptr_t kDirFiles = 0x0091B9C7;       // thiscall (list*) -> bool
constexpr std::uintptr_t kDirDestruct = 0x0091B2F7;    // thiscall ()
constexpr std::uintptr_t kNameListMake = 0x0043AEF0;   // thiscall (allocator*): the list of names
constexpr std::uintptr_t kNameListFree = 0x0043AF20;   // thiscall ()
constexpr uint32_t kFileBytes = 0x80;
constexpr uint32_t kDirBytes = 0x34;

// The T2K holder (f5).
constexpr std::uintptr_t kHolderConstruct = 0x00897FED;
constexpr std::uintptr_t kHolderOpen = 0x00898CD9;     // thiscall (const String*) -> bool
constexpr std::uintptr_t kHolderName = 0x0089801E;     // thiscall (String*, id) -> bool
constexpr std::uintptr_t kHolderStyles = 0x0089883F;   // thiscall () -> bits
constexpr std::uintptr_t kHolderDestruct = 0x00897E36;
constexpr uint32_t kHolderBytes = 0x34;
constexpr Address kHolderScaler = 0x08;
constexpr Address kScalerKind2 = 0x5C;

// The system's tables (STLport, the game's code). The faces: a face's
// value (0x24 bytes: the list, the name, styles, kind, type) made empty,
// the pair of a path and a value (0x38 bytes) made of both, destroyed, the
// map's find (the header if not there) and insert; the list's insert and
// push_back of a (font, style) pair; the set of names' find, insert and
// erase.
constexpr std::uintptr_t kFaceMake = 0x00894144;       // thiscall () -> this
constexpr std::uintptr_t kFaceFree = 0x0089415F;       // thiscall ()
constexpr std::uintptr_t kFacePairMake = 0x00894740;   // thiscall (const String* key, const value*)
constexpr std::uintptr_t kFacesFind = 0x008FEB1E;      // thiscall (const String*) -> node; every string map's
constexpr std::uintptr_t kFacesInsert = 0x008953A9;    // thiscall (result*, const pair*)
constexpr std::uintptr_t kFacesErase = 0x008945E1;     // thiscall (node by value)
constexpr std::uintptr_t kListFree = 0x0096EDBC;       // thiscall ()
constexpr std::uintptr_t kListInsert = 0x0092119E;     // thiscall (result*, position by value, const pair*)
constexpr std::uintptr_t kListPushBack = 0x0089408D;   // thiscall (const pair*)
constexpr std::uintptr_t kListErase = 0x00949259;      // thiscall (result*, node by value)
constexpr std::uintptr_t kListClear = 0x009A841F;      // thiscall ()
constexpr std::uintptr_t kNamesFind = 0x00662840;      // thiscall (const String*) -> node
constexpr std::uintptr_t kNamesInsert = 0x004DFC60;    // thiscall (result*, const String*)
constexpr std::uintptr_t kNamesErase = 0x005C65B0;     // thiscall (const String*)
// The aliases: the pair of a name and a vector of names (0x20 bytes), the
// map's insert, the vector's push_back and destructor.
constexpr std::uintptr_t kAliasPairMake = 0x00894762;  // thiscall (const String*, const Vector*)
constexpr std::uintptr_t kAliasesInsert = 0x0089543A;  // thiscall (result*, const pair*)
constexpr std::uintptr_t kVectorPushBack = 0x004522F0; // thiscall (const String*)
constexpr std::uintptr_t kVectorFree = 0x00419140;     // thiscall ()
// The families: find, the pair of a code (a signed word) and a name (0x18
// bytes), insert, the name of a code (made if not there).
constexpr std::uintptr_t kFamiliesFind = 0x004B5C40;   // thiscall (const int32_t*) -> node
constexpr std::uintptr_t kFamilyPairMake = 0x004CCF20; // thiscall (const int32_t*, const String*)
constexpr std::uintptr_t kFamiliesInsert = 0x004B6530; // thiscall (result*, const pair*)
constexpr std::uintptr_t kFamiliesAt = 0x004B6D90;     // thiscall (const int32_t*) -> String*
// The sizes in mode 1 by face: the pair of a name and two words (0x1C
// bytes), insert, erase by name.
constexpr std::uintptr_t kRangePairMake = 0x00894585;  // thiscall (const String*, const int32_t[2])
constexpr std::uintptr_t kRangesInsert = 0x008950A5;   // thiscall (result*, const pair*)
constexpr std::uintptr_t kRangesErase = 0x00894E0C;    // thiscall (const String*) -> count
// The tables' constructors, destructors and clears.
constexpr std::uintptr_t kNamesMake = 0x004DE7B0, kNamesFree = 0x004DE760, kNamesClear = 0x004DE720;
constexpr std::uintptr_t kFamiliesMake = 0x00671890, kFamiliesFree = 0x004B6400, kFamiliesClear = 0x004B6310;
constexpr std::uintptr_t kFacesMake = 0x0089505A, kFacesFree = 0x00894DC7, kFacesClear = 0x00894784;
constexpr std::uintptr_t kAliasesMake = 0x00895073, kAliasesFree = 0x00894DDE, kAliasesClear = 0x008947AD;
constexpr std::uintptr_t kRangesMake = 0x0089508C, kRangesFree = 0x00894DF5;

constexpr uint32_t kFaceValueBytes = 0x24;
constexpr uint32_t kFacePairBytes = 0x38;
constexpr uint32_t kAliasPairBytes = 0x20;
constexpr uint32_t kFamilyPairBytes = 0x18;
constexpr uint32_t kRangePairBytes = 0x1C;

// The font (class 0x1AA404AC, interface 0x10673960) and its slots.
constexpr uint32_t kFontClass = 0x1AA404AC;
constexpr uint32_t kFontInterface = 0x10673960;
constexpr uint32_t kFontSize = 0x15C;
namespace font {
constexpr std::size_t kAddRef = 0x04, kRelease = 0x08, kLoad = 0x0C, kFamily = 0x14, kStyle = 0x1C,
                      kSetStyle = 0x20, kKind = 0x24, kSize = 0x28, kSetSize = 0x2C, kSetColours = 0x38,
                      kSetMode = 0x40, kPath = 0x54, kSetPath = 0x58, kSetEncoding = 0x60, kSetBlitter = 0xD8;
} // namespace font

// cIGZString's slots: assign from text and count, the text, its count, and
// equality (with a flag: 1 ignores case).
constexpr std::size_t kIgzAssign = 0x0C, kIgzData = 0x18, kIgzLength = 0x1C, kIgzEquals = 0x28;
// cIGZCOM's slot 3 makes an object of a class with an interface.
constexpr std::size_t kComGetClassObject = 0x0C;

// ====================================================================================
// Layout

namespace sys {
constexpr uint32_t kSize = 0x70;
constexpr Address kVtable = 0x00AC5488;
constexpr Address kVtable2 = 0x00AC5454;
constexpr Address kBaseVtable2 = 0x00A93658; // written to +4 first, as the base's constructor would
constexpr uint32_t kClass = 0x20673960;
constexpr Address kReady = 0x08;
constexpr Address kRefs = 0x0C;
constexpr Address kClassId = 0x10;
constexpr Address kDrawMode = 0x14;
constexpr Address kMonoMin = 0x18;
constexpr Address kMonoMax = 0x1C;
constexpr Address kEncoding = 0x20;
constexpr Address kBlitter = 0x24;
constexpr Address kNames = 0x28;
constexpr Address kFamilies = 0x34;
constexpr Address kFaces = 0x40;
constexpr Address kFaceCount = 0x44;
constexpr Address kAliases = 0x4C;
constexpr Address kPatterns = 0x58;
constexpr Address kRanges = 0x64;
// The system's own slots it calls.
constexpr std::size_t kShutdown = 0x10, kMakeFont = 0x28, kGetFont = 0x2C, kAddFontFile = 0x38, kHasFace = 0x58,
                      kDelete = 0x60;
} // namespace sys

// A map's node: the key at +0x10, the value at +0x24 (all the system's
// maps have 0x14-byte string keys but the families', a word); the header's
// +8 is the first node.
constexpr Address kNodeKey = 0x10;
constexpr Address kNodeValue = 0x24;
constexpr Address kHeaderFirst = 0x08;
// A face's value from the node: the list, the name, styles, kind, type.
constexpr Address kFaceList = 0x24;
constexpr Address kFaceName = 0x28;
constexpr Address kFaceStyles = 0x3C;
constexpr Address kFaceKind = 0x40;
constexpr Address kFaceType = 0x44;
// The same from the start of a pair (key first).
constexpr Address kPairList = 0x14;
constexpr Address kPairName = 0x18;
constexpr Address kPairStyles = 0x2C;
constexpr Address kPairKind = 0x30;
constexpr Address kPairType = 0x34;
// A list node: next, previous, then the font and its style.
constexpr Address kEntryFont = 0x08;
constexpr Address kEntryStyle = 0x0C;
// A family's node: the code at +0x10, its name (a String) at +0x14.
constexpr Address kFamilyName = 0x14;
// A face's sizes in mode 1: +0x24 and +0x28.
constexpr Address kRangeMin = 0x24;
constexpr Address kRangeMax = 0x28;

// The game's texts.
constexpr Address kTextFonts = 0x00A8704C;    // "Fonts"
constexpr Address kTextTtf = 0x00A92EA0;      // ".ttf"
constexpr Address kTextTtc = 0x00AC5444;      // ".ttc"
constexpr Address kTextOtf = 0x00AC543C;      // ".otf"
constexpr Address kTextPfb = 0x00AC5434;      // ".pfb"
constexpr Address kTextPostScript = 0x00AC542C; // "%!PS"
constexpr Address kTextTrueType = 0x00AC5424;   // 00 01 00 00, four bytes
constexpr Address kTextTtcf = 0x00AC541C;     // "ttcf"
constexpr Address kTextPfbUpper = 0x00AC544C; // ".PFB"
// The patterns' texts, through the four pointers the game reads them by:
// "*.tt?", "*.pfb", "*.mxf", "*.otf".
constexpr Address kPatternPointers = 0x00B13434;
// Init's default faces of four families.
struct Family {
    uint32_t code;
    Address name;
};
constexpr Family kFamiliesByDefault[] = {
    {5, 0x00AC5508},  // "Times New Roman"
    {9, 0x00A8EC00},  // "Arial"
    {6, 0x00AC54FC},  // "Courier New"
    {10, 0x00AC54EC}, // "Lucida Console"
};

constexpr uint32_t kIidGzUnknown = 1;
constexpr uint32_t kIidFontSystem = 0x00673960;
constexpr uint32_t kIidFontSystem2 = 0x287FB697;
constexpr uint32_t kVersion = 900000;
constexpr uint32_t kStyleKept = 0xFFFFFFC3; // GetFont compares a face's styles with these bits of the asked
constexpr uint32_t kNoScore = 1000;
constexpr uint32_t kNamesAtMost = 32;
constexpr uint32_t kColourOwn = 0x08000000;
constexpr uint32_t kWindowsPath = 0x200;

// ====================================================================================
// Strings

String* Str(Address at) { return static_cast<String*>(Ptr(at)); }
Address Body(const String* s) { return At(s) + 4; }

void Construct(String* s) { This<void*>(kStringConstruct, s); }
void FromIgz(String* s, void* igz) { This<void*>(kStringFromIGZ, s, igz); }
void FromText(String* s, Address text) { This<void*>(kStringFromText, s, text); }
void CopyOf(String* s, const void* from) { This<void*>(kStringCopy, s, from, uint32_t{0}, uint32_t{0xFFFFFFFF}); }
void Lower(void* s) { This<void>(kStringLower, s); }
void Assign(void* s, const void* from) { This<void*>(kStringAssign, s, from); }
void DestroyString(void* s) { This<void>(kStringDestroy, s); }

void FreeText(Address begin)
{
    if (begin)
        Cdecl<void>(addr::SC4_Free, Ptr(begin));
}

// The string's destructor as the game inlines it: its text freed (the
// vtable put back first where the game does, which changes nothing here).
void Drop(String* s)
{
    s->vtable = addr::SC4String_Vtable;
    FreeText(s->begin);
}

void PushBack(String* s, char c) { This<void>(kBodyPushBack, Ptr(Body(s)), static_cast<uint32_t>(static_cast<uint8_t>(c))); }

bool IsSeparator(char c) { return Cdecl<uint8_t>(kIsSeparator, static_cast<int32_t>(c)) != 0; }

// The last character of a string (its end less one, as the game reads it,
// even from an empty string).
char Last(const String* s) { return Load<char>(s->end - 1); }

// "<Windows>\Fonts\" into `out` if the Windows folder is found, as the game
// builds it before using `out`: false if not.
bool WindowsDirectory(char (&buffer)[kWindowsPath])
{
    const UINT n = GetWindowsDirectoryA(buffer, kWindowsPath);
    return n != 0 && n <= kWindowsPath;
}

void AppendFonts(String* s)
{
    if (!IsSeparator(Last(s)))
        PushBack(s, '\\');
    This<void>(kBodyAppendText, Ptr(Body(s)), kTextFonts);
    PushBack(s, '\\');
}

bool Exists(const String* s) { return Cdecl<uint8_t>(kFileExists, s) != 0; }

Address TreeNext(Address node) { return Cdecl<Address>(kTreeNext, node); }
Address Header(Address map) { return Load<Address>(map); }
Address First(Address map) { return Load<Address>(Header(map) + kHeaderFirst); }
Address FindIn(std::uintptr_t find, Address map, const void* key) { return This<Address>(find, Ptr(map), key); }

int32_t Count14(Address begin, Address end) { return static_cast<int32_t>(end - begin) / 0x14; }

struct Entry {
    Address font;
    uint32_t style;
};

// ====================================================================================
// The object

// The game's 0x00893D2C (slot 0, ret 8): 1 and 0x00673960 give the system,
// 0x287FB697 its +4 (null for a null system), each AddRef'd through slot 1.
bool QueryInterface(void* self, uint32_t iid, Address* out)
{
    if (iid == kIidGzUnknown || iid == kIidFontSystem)
        *out = At(self);
    else if (iid == kIidFontSystem2)
        *out = self ? At(self) + 4 : 0;
    else
        return false;
    Virtual<uint32_t>(self, 0x4);
    return true;
}

// The game's 0x00893D68 (thiscall): Shutdown, then the deleting destructor.
void Destroy(void* self)
{
    Virtual<uint32_t>(self, sys::kShutdown);
    Virtual<void*>(self, sys::kDelete, uint32_t{1});
}

// The game's 0x00893DD7 (slot 2): the count less one while above 1; at 1 or
// below the system is destroyed (the count left) and 0 returned.
uint32_t Release(void* self)
{
    const uint32_t refs = Load<uint32_t>(At(self) + sys::kRefs);
    if (refs > 1) {
        Store<uint32_t>(At(self) + sys::kRefs, refs - 1);
        return refs - 1;
    }
    Destroy(self);
    return 0;
}

// The game's 0x00893D7B (slot 5, ret 4): the encoding fonts get.
bool SetEncoding(void* self, uint32_t encoding)
{
    Store<uint32_t>(At(self) + sys::kEncoding, encoding);
    return true;
}

// The game's 0x00893D91 (slot 6, ret 4): the blitter fonts get, AddRef'd,
// the one before released.
void SetBlitter(void* self, void* blitter)
{
    if (blitter)
        Virtual<uint32_t>(blitter, 0x4);
    if (void* const old = Load<void*>(At(self) + sys::kBlitter))
        Virtual<uint32_t>(old, 0x8);
    Store<Address>(At(self) + sys::kBlitter, At(blitter));
}

// The game's 0x00894F77 (the +4 vtable's slots 5 and 12).
uint32_t Version() { return kVersion; }

// The game's 0x00895268 (thiscall): the fields, the tables, the four file
// patterns, the encoding RZText uses by default. Returns this.
void* Construct(void* self)
{
    const Address at = At(self);
    Store<Address>(at + 4, sys::kBaseVtable2);
    Store<Address>(at, sys::kVtable);
    Store<Address>(at + 4, sys::kVtable2);
    Store<uint8_t>(at + sys::kReady, 0);
    Store<uint32_t>(at + sys::kRefs, 0);
    Store<uint32_t>(at + sys::kClassId, sys::kClass);
    for (Address field = sys::kDrawMode; field <= sys::kBlitter; field += 4)
        Store<uint32_t>(at + field, 0);
    This<void*>(kNamesMake, Ptr(at + sys::kNames));
    This<void*>(kFamiliesMake, Ptr(at + sys::kFamilies));
    This<void*>(kFacesMake, Ptr(at + sys::kFaces));
    This<void*>(kAliasesMake, Ptr(at + sys::kAliases));
    Store<Vector>(at + sys::kPatterns, Vector{});
    This<void*>(kRangesMake, Ptr(at + sys::kRanges));
    for (Address pointer = kPatternPointers; pointer < kPatternPointers + 16; pointer += 4) {
        String pattern;
        FromText(&pattern, Load<Address>(pointer));
        This<void>(kVectorPushBack, Ptr(at + sys::kPatterns), &pattern);
        Drop(&pattern);
    }
    Store<uint32_t>(at + sys::kEncoding, Cdecl<uint32_t>(kDefaultEncoding));
    return self;
}

// The game's 0x00894F2C (thiscall): the vtables, then the tables taken down
// last first. Nothing is released.
void Destruct(void* self)
{
    const Address at = At(self);
    Store<Address>(at, sys::kVtable);
    Store<Address>(at + 4, sys::kVtable2);
    This<void>(kRangesFree, Ptr(at + sys::kRanges));
    This<void>(kVectorFree, Ptr(at + sys::kPatterns));
    This<void>(kAliasesFree, Ptr(at + sys::kAliases));
    This<void>(kFacesFree, Ptr(at + sys::kFaces));
    This<void>(kFamiliesFree, Ptr(at + sys::kFamilies));
    This<void>(kNamesFree, Ptr(at + sys::kNames));
}

// The game's 0x0089538D (slot 24, ret 4): destroyed, and freed if bit 0.
void* DeletingDestruct(void* self, uint32_t flags)
{
    Destruct(self);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// The game's 0x00894F8D (slot 4): if Init ran, every font made is released
// once (one still referenced is asked its family, which is dropped), each
// face's list emptied, the faces, families, names and aliases emptied, the
// blitter released. The sizes in mode 1 by face and the patterns stay.
// Returns true.
bool Shutdown(void* self)
{
    const Address at = At(self);
    if (Load<uint8_t>(at + sys::kReady) == 0)
        return true;
    const Address header = Header(at + sys::kFaces);
    for (Address node = Load<Address>(header + kHeaderFirst); node != header; node = TreeNext(node)) {
        const Address list = Load<Address>(node + kFaceList);
        for (Address entry = Load<Address>(list); entry != list; entry = Load<Address>(entry)) {
            const Address made = Load<Address>(entry + kEntryFont);
            if (made && VirtualAt<uint32_t>(made, font::kRelease) != 0) {
                String family;
                Construct(&family);
                VirtualAt<void>(made, font::kFamily, &family);
                Drop(&family);
            }
        }
        This<void>(kListClear, Ptr(node + kFaceList));
    }
    This<void>(kFacesClear, Ptr(at + sys::kFaces));
    This<void>(kFamiliesClear, Ptr(at + sys::kFamilies));
    This<void>(kNamesClear, Ptr(at + sys::kNames));
    This<void>(kAliasesClear, Ptr(at + sys::kAliases));
    if (void* const blitter = Load<void*>(at + sys::kBlitter)) {
        Virtual<uint32_t>(blitter, 0x8);
        Store<uint32_t>(at + sys::kBlitter, 0);
    }
    Store<uint8_t>(at + sys::kReady, 0);
    return true;
}

// ====================================================================================
// Aliases

// The game's 0x00895DCF (thiscall, ret 8): the default face of a family's
// code: a new entry, or the name it had replaced. Returns true.
bool SetFamily(void* self, uint32_t code, void* name)
{
    const Address families = At(self) + sys::kFamilies;
    // The game passes the address of its argument.
    const uint32_t key = code;
    if (FindIn(kFamiliesFind, families, &key) == Header(families)) {
        String text;
        FromIgz(&text, name);
        alignas(8) uint8_t pair[kFamilyPairBytes];
        This<void*>(kFamilyPairMake, pair, &key, &text);
        Drop(&text);
        uint8_t result[8];
        This<void*>(kFamiliesInsert, Ptr(families), result, pair);
        DestroyString(pair + 4);
        return true;
    }
    const uint32_t length = Virtual<uint32_t>(name, kIgzLength);
    const Address data = Virtual<Address>(name, kIgzData);
    const Address value = This<Address>(kFamiliesAt, Ptr(families), &key);
    This<void>(kBodyAssignChars, Ptr(value + 4), data, data + length);
    return true;
}

// The game's 0x00895E68 (slot 3): the first time, the default faces of four
// families; then the byte at +8 is set. Returns true.
bool Init(void* self)
{
    if (Load<uint8_t>(At(self) + sys::kReady) != 0)
        return true;
    for (const Family& family : kFamiliesByDefault) {
        String name;
        FromText(&name, family.name);
        SetFamily(self, family.code, &name);
        Drop(&name);
    }
    Store<uint8_t>(At(self) + sys::kReady, 1);
    return true;
}

// The game's 0x0089595D (slot 20, ret 8): `face` added to the alias's names,
// the alias made if new. Returns true.
bool AddAlias(void* self, void* alias, void* face)
{
    const Address aliases = At(self) + sys::kAliases;
    String key;
    FromIgz(&key, alias);
    Address node = FindIn(kFacesFind, aliases, &key);
    bool add = true;
    if (node == Header(aliases)) {
        const Vector empty{};
        alignas(8) uint8_t pair[kAliasPairBytes];
        This<void*>(kAliasPairMake, pair, &key, &empty);
        This<void>(kVectorFree, const_cast<Vector*>(&empty));
        uint8_t result[8];
        This<void*>(kAliasesInsert, Ptr(aliases), result, pair);
        node = FindIn(kFacesFind, aliases, &key);
        This<void>(kVectorFree, pair + 0x14);
        FreeText(Load<Address>(At(pair) + 4));
        add = node != Header(aliases);
    }
    if (add) {
        String name;
        FromIgz(&name, face);
        This<void>(kVectorPushBack, Ptr(node + kNodeValue), &name);
        FreeText(name.begin);
    }
    FreeText(key.begin);
    return true;
}

// The game's 0x00894198 (slot 21, ret 0xC): the alias's names into `count`
// strings (each skipped if null), answered how many it has, at most count.
// It goes on to `count` whatever the alias has: a string past its names is
// given what lies past its vector.
uint32_t GetAliasFaces(void* self, void* alias, void** outs, uint32_t count)
{
    const Address aliases = At(self) + sys::kAliases;
    String key;
    FromIgz(&key, alias);
    const Address node = FindIn(kFacesFind, aliases, &key);
    if (node == Header(aliases)) {
        FreeText(key.begin);
        return 0;
    }
    uint32_t result = static_cast<uint32_t>(Count14(Load<Address>(node + kNodeValue), Load<Address>(node + kNodeValue + 4)));
    if (result > count)
        result = count;
    for (uint32_t i = 0; i < count; ++i) {
        const Address name = Load<Address>(node + kNodeValue) + i * 0x14;
        if (outs[i]) {
            const Address begin = Load<Address>(name + 4);
            Virtual<void>(outs[i], kIgzAssign, begin, Load<Address>(name + 8) - begin);
        }
    }
    FreeText(key.begin);
    return result;
}

// The game's 0x0089423C (slot 23, ret 8): the first of the alias's names
// the system has a face of (HasFace, style 0) into `out`. False if none.
bool ResolveAlias(void* self, void* alias, void* out)
{
    const Address aliases = At(self) + sys::kAliases;
    String key;
    FromIgz(&key, alias);
    const Address node = FindIn(kFacesFind, aliases, &key);
    if (node != Header(aliases)) {
        const uint32_t n = static_cast<uint32_t>(Count14(Load<Address>(node + kNodeValue), Load<Address>(node + kNodeValue + 4)));
        for (uint32_t i = 0; i < n; ++i) {
            const Address name = Load<Address>(node + kNodeValue) + i * 0x14;
            if (Virtual<uint8_t>(self, sys::kHasFace, name, uint32_t{0}, uint32_t{0})) {
                const Address begin = Load<Address>(name + 4);
                Virtual<void>(out, kIgzAssign, begin, Load<Address>(name + 8) - begin);
                FreeText(key.begin);
                return true;
            }
        }
    }
    FreeText(key.begin);
    return false;
}

// ====================================================================================
// Faces

// The game's 0x0089433C (thiscall, ret 4; `this` unused): a font file's
// type by its extension (.ttf 1, .ttc and .otf 4, .pfb 2), else by its
// first 32 bytes ("%!PS" anywhere 2, 00 01 00 00 at the start 1, "ttcf" at
// the start 4); 0 if none or the file cannot be read.
uint32_t FileType(void* path)
{
    uint32_t type = 0;
    String extension;
    Construct(&extension);
    Cdecl<void>(kFileExtension, path, &extension);
    Lower(&extension);
    const Address body = Body(&extension);
    const auto is = [&](Address text) { return Cdecl<uint8_t>(kBodyEqualsText, body, text) != 0; };
    if (is(kTextTtf)) {
        type = 1;
    } else if (is(kTextTtc) || is(kTextOtf)) {
        type = 4;
    } else if (is(kTextPfb)) {
        type = 2;
    } else {
        alignas(8) uint8_t file[kFileBytes];
        This<void*>(kFileConstruct, file, path);
        if (This<uint8_t>(kFileOpen, file, uint32_t{1}, uint32_t{2}, uint32_t{1})) {
            Vector head;
            This<void*>(kBytesMake, &head, uint32_t{0x20}, uint32_t{0});
            if (This<uint8_t>(kFileRead, file, head.begin, head.end - head.begin)) {
                if (This<int32_t>(kBodyFind, &head, kTextPostScript, uint32_t{0}) != -1)
                    type = 2;
                else if (This<int32_t>(kBytesFind, &head, kTextTrueType, uint32_t{0}, uint32_t{4}) == 0)
                    type = 1;
                else if (This<int32_t>(kBodyFind, &head, kTextTtcf, uint32_t{0}) == 0)
                    type = 4;
            }
            This<void>(kFileClose, file);
            FreeText(head.begin);
        }
        This<void>(kFileDestruct, file);
    }
    Drop(&extension);
    return type;
}

// A face's value made empty (the list and the name; the three words are
// left as the space had them, which in the game is its stack: ours zeroes
// the space first).
struct FaceValue {
    alignas(8) uint8_t bytes[kFaceValueBytes] = {};
    FaceValue() { This<void*>(kFaceMake, bytes); }
    ~FaceValue()
    {
        DestroyString(bytes + 4);
        This<void>(kListFree, bytes);
    }
    FaceValue(const FaceValue&) = delete;
    FaceValue& operator=(const FaceValue&) = delete;
};

// The game's 0x00895564 (slot 14, ret 4): a font file added as a face, the
// path tried as it is and then in Windows's Fonts folder; the face's name is
// the family T2K gives, its styles, kind and type from the file. False if
// the path is a face already, the file is not there or T2K gives no name.
bool AddFontFile(void* self, void* pathIn)
{
    const Address at = At(self);
    const Address faces = at + sys::kFaces;
    bool result = false;
    String path;
    FromIgz(&path, pathIn);
    {
        const Address header = Header(faces);
        String copy;
        CopyOf(&copy, &path);
        const bool absent = FindIn(kFacesFind, faces, &copy) == header;
        Drop(&copy);
        if (!absent) {
            Drop(&path);
            return false;
        }
    }
    if (!Exists(&path)) {
        char windows[kWindowsPath];
        if (WindowsDirectory(windows)) {
            Cdecl<void>(kStringFromChars, windows, static_cast<uint32_t>(std::strlen(windows)), uint32_t{0xFFFFFFFF},
                        &path);
            AppendFonts(&path);
            const uint32_t length = Virtual<uint32_t>(pathIn, kIgzLength);
            const Address data = Virtual<Address>(pathIn, kIgzData);
            This<void>(kBodyAppendChars, Ptr(Body(&path)), data, length);
        }
    }
    if (!Exists(&path)) {
        Drop(&path);
        return false;
    }
    String converted;
    Construct(&converted);
    Cdecl<void>(kStringConvert, &path, &converted, uint32_t{0xFFFFFFFF});
    String name;
    Construct(&name);
    uint32_t kind = 1;
    alignas(8) uint8_t holder[kHolderBytes];
    This<void*>(kHolderConstruct, holder);
    if (This<uint8_t>(kHolderOpen, holder, &path)) {
        const uint32_t type = FileType(&path);
        This<uint8_t>(kHolderName, holder, &name, uint32_t{1});
        const uint32_t styles = This<uint32_t>(kHolderStyles, holder);
        if (Load<uint32_t>(Load<Address>(At(holder) + kHolderScaler) + kScalerKind2) != 0)
            kind = 2;
        result = name.begin != name.end;
        if (result) {
            FaceValue value;
            alignas(8) uint8_t pair[kFacePairBytes];
            This<void*>(kFacePairMake, pair, &path, value.bytes);
            Assign(pair + kPairName, &name);
            Store<uint32_t>(At(pair) + kPairStyles, styles);
            Store<uint32_t>(At(pair) + kPairKind, kind);
            Store<uint32_t>(At(pair) + kPairType, type);
            uint8_t inserted[8];
            This<void*>(kFacesInsert, Ptr(faces), inserted, pair);
            Lower(&name);
            This<void*>(kNamesInsert, Ptr(at + sys::kNames), inserted, &name);
            This<void>(kFaceFree, pair + kPairList);
            FreeText(Load<Address>(At(pair) + 4));
        }
    }
    This<void>(kHolderDestruct, holder);
    Drop(&name);
    Drop(&converted);
    Drop(&path);
    return result;
}

// The game's 0x00894864 (slot 15, ret 4): every file of the folder (Windows's
// Fonts folder if empty) that matches a pattern given to slot 14. Counts
// each file added, and each whose name has ".PFB" in it, in capitals.
uint32_t AddFontFolder(void* self, void* folderIn)
{
    const Address at = At(self);
    String folder;
    FromIgz(&folder, folderIn);
    uint32_t count = 0;
    if (folder.begin == folder.end) {
        char windows[kWindowsPath];
        if (WindowsDirectory(windows)) {
            String text;
            FromText(&text, At(windows));
            Assign(&folder, &text);
            FreeText(text.begin);
            AppendFonts(&folder);
        }
    }
    if (!IsSeparator(Last(&folder)))
        PushBack(&folder, '\\');
    const int32_t patterns = Count14(Load<Address>(at + sys::kPatterns), Load<Address>(at + sys::kPatterns + 4));
    for (int32_t i = 0; i < patterns; ++i) {
        alignas(8) uint8_t directory[kDirBytes];
        This<void*>(kDirConstruct, directory, &folder, uint32_t{0}, uint32_t{0}, uint32_t{0});
        Address names = 0;
        uint8_t allocator = 0;
        This<void*>(kNameListMake, &names, &allocator);
        This<void>(kDirPattern, directory, Load<Address>(at + sys::kPatterns) + static_cast<uint32_t>(i) * 0x14,
                   uint32_t{0});
        if (This<uint8_t>(kDirFiles, directory, &names)) {
            const Address header = names;
            for (Address entry = Load<Address>(header); entry != header;) {
                const Address nameBody = entry + 0xC;
                Vector joined;
                Cdecl<void*>(kBodyConcat, &joined, Body(&folder), entry + 8 ? nameBody : 0);
                String file;
                This<void*>(kStringFromBody, &file, &joined);
                FreeText(joined.begin);
                if (This<int32_t>(kBodyFind, Ptr(nameBody), kTextPfbUpper, uint32_t{0}) != -1)
                    ++count;
                if (Virtual<uint8_t>(self, sys::kAddFontFile, &file))
                    ++count;
                entry = Load<Address>(entry);
                Drop(&file);
            }
        }
        This<void>(kNameListFree, &names);
        This<void>(kDirDestruct, directory);
    }
    Drop(&folder);
    return count;
}

// The game's 0x008957D6 (slot 13, ret 4): a font made elsewhere kept in the
// faces, AddRef'd: a new face of its file, or the face's list if no font of
// its size is there. False for none, or one of that size there.
bool AddFont(void* self, void* made)
{
    if (!made)
        return false;
    const Address at = At(self);
    const Address faces = at + sys::kFaces;
    String path;
    Construct(&path);
    Virtual<void>(made, font::kPath, &path);
    const Address node = FindIn(kFacesFind, faces, &path);
    if (node == Header(faces)) {
        alignas(8) uint8_t pair[kFacePairBytes];
        {
            FaceValue value;
            This<void*>(kFacePairMake, pair, &path, value.bytes);
        }
        Virtual<void>(made, font::kFamily, pair + kPairName);
        Store<uint32_t>(At(pair) + kPairStyles, Virtual<uint32_t>(made, font::kStyle));
        Store<uint32_t>(At(pair) + kPairKind, Virtual<uint32_t>(made, font::kKind));
        Virtual<uint32_t>(made, font::kAddRef);
        Entry entry{At(made), 0};
        entry.style = Virtual<uint32_t>(made, font::kStyle);
        uint8_t result[8];
        This<void*>(kListInsert, pair + kPairList, result, Load<Address>(At(pair) + kPairList), &entry);
        This<void*>(kFacesInsert, Ptr(faces), result, pair);
        Lower(pair + kPairName);
        This<void*>(kNamesInsert, Ptr(at + sys::kNames), result, pair + kPairName);
        This<void>(kFaceFree, pair + kPairList);
        Drop(Str(At(pair)));
        Drop(&path);
        return true;
    }
    const Address list = Load<Address>(node + kFaceList);
    Address entry = Load<Address>(list);
    for (; entry != list; entry = Load<Address>(entry)) {
        const uint32_t size = VirtualAt<uint32_t>(Load<Address>(entry + kEntryFont), font::kSize);
        if (size == Virtual<uint32_t>(made, font::kSize))
            break;
    }
    if (entry != list) {
        Drop(&path);
        return false;
    }
    Virtual<uint32_t>(made, font::kAddRef);
    Entry pushed{At(made), 0};
    pushed.style = Virtual<uint32_t>(made, font::kStyle);
    This<void>(kListPushBack, Ptr(node + kFaceList), &pushed);
    Drop(&path);
    return true;
}

// The game's 0x00894A3A (slot 17, ret 4): the faces whose name equals `name`
// (ignoring case) removed, each of their fonts released once; the name
// dropped from the names in lower case. Returns false.
bool RemoveFace(void* self, void* name)
{
    const Address at = At(self);
    const Address faces = at + sys::kFaces;
    String lower;
    FromIgz(&lower, name);
    Lower(&lower);
    This<uint32_t>(kNamesErase, Ptr(at + sys::kNames), &lower);
    Address node = First(faces);
    while (node != Header(faces)) {
        if (VirtualAt<uint8_t>(node + kFaceName, kIgzEquals, name, uint32_t{0})) {
            const Address list = Load<Address>(node + kFaceList);
            for (Address entry = Load<Address>(list); entry != list; entry = Load<Address>(entry))
                VirtualAt<uint32_t>(Load<Address>(entry + kEntryFont), font::kRelease);
            const Address gone = node;
            node = TreeNext(node);
            This<void>(kFacesErase, Ptr(faces), gone);
        } else {
            node = TreeNext(node);
        }
    }
    Drop(&lower);
    return false;
}

// The game's 0x00893F22 (slot 12, ret 4): the font released wherever a
// face's list has it (the first time in each list); where that leaves it
// one reference, released again and taken out of the list. Answers the
// last release's count (0 if not found).
uint32_t ReleaseFont(void* self, void* made)
{
    uint32_t result = 0;
    const Address header = Header(At(self) + sys::kFaces);
    for (Address node = Load<Address>(header + kHeaderFirst); node != header; node = TreeNext(node)) {
        const Address listAt = node + kFaceList;
        const Address list = Load<Address>(listAt);
        for (Address entry = Load<Address>(list); entry != list; entry = Load<Address>(entry)) {
            if (Load<Address>(entry + kEntryFont) != At(made))
                continue;
            result = Virtual<uint32_t>(made, font::kRelease);
            if (result == 1) {
                result = Virtual<uint32_t>(made, font::kRelease);
                uint8_t erased[8];
                This<void*>(kListErase, Ptr(listAt), erased, entry);
            }
            break;
        }
    }
    return result;
}

// The game's 0x00893E5B (slot 18, ret 8): the number of faces, or with a
// count, at most that many faces (0x88 bytes each: the name, cut to 127
// characters, then its styles and kind) and how many.
uint32_t ListFaces(void* self, uint8_t* out, uint32_t count)
{
    const Address at = At(self);
    uint32_t n = Load<uint32_t>(at + sys::kFaceCount);
    if (count == 0)
        return n;
    if (n > count)
        n = count;
    Address node = First(at + sys::kFaces);
    for (uint32_t i = 0; i < n; ++i) {
        std::memset(out, 0, 0x80);
        std::strncpy(reinterpret_cast<char*>(out), static_cast<const char*>(Ptr(Load<Address>(node + kFaceName + 4))),
                     0x7F);
        Store<uint32_t>(At(out) + 0x80, Load<uint32_t>(node + kFaceStyles));
        Store<uint32_t>(At(out) + 0x84, Load<uint32_t>(node + kFaceKind));
        out += 0x88;
        node = TreeNext(node);
    }
    return n;
}

// The game's 0x00893ECB (slot 19, ret 8): every font the faces hold, as
// many as fit in `count` (with a count of 0, none written); answers how many
// there are, or with a count how many were written.
uint32_t ListFonts(void* self, Address* out, uint32_t count)
{
    const Address header = Header(At(self) + sys::kFaces);
    uint32_t written = 0, total = 0;
    for (Address node = Load<Address>(header + kHeaderFirst); node != header; node = TreeNext(node)) {
        const Address list = Load<Address>(node + kFaceList);
        for (Address entry = Load<Address>(list); entry != list; entry = Load<Address>(entry)) {
            if (count == 0) {
                ++total;
            } else if (written < count) {
                out[written++] = Load<Address>(entry + kEntryFont);
                ++total;
            }
        }
    }
    return total;
}

// The game's 0x008944B2 (slot 22, ret 0xC): whether a face's name is known
// and a face of it has these style bits (the low two).
bool HasFace(void* self, void* name, uint32_t, uint32_t style)
{
    const Address at = At(self);
    String lower;
    FromIgz(&lower, name);
    Lower(&lower);
    const Address names = at + sys::kNames;
    const Address namesHeader = Header(names);
    if (FindIn(kNamesFind, names, &lower) != namesHeader) {
        const Address header = Header(at + sys::kFaces);
        style &= 3;
        for (Address node = Load<Address>(header + kHeaderFirst); node != header; node = TreeNext(node)) {
            if (Load<uint32_t>(node + kFaceStyles) != style)
                continue;
            String face;
            CopyOf(&face, Ptr(node + kFaceName));
            Lower(&face);
            const bool same = Cdecl<uint8_t>(kBodyEquals, Body(&face), Body(&lower)) != 0;
            FreeText(face.begin);
            if (same) {
                Drop(&lower);
                return true;
            }
        }
    }
    Drop(&lower);
    return false;
}

// ====================================================================================
// Fonts

// What GetFont decided, for compare mode: a font it reused, or the face
// (its key), size, style and reuse it asked its slot 10 for.
struct Decision {
    bool made = false;
    Address reused = 0;
    Address key = 0;
    uint32_t size = 0, style = 0, reuse = 0;
};

// The game's 0x00894ADF (slot 11, ret 0x14): a font of the first face of a
// comma-separated list, in lower case, that is best by name and style. A
// name the system has no face of becomes the alias whose names have it.
// Each face scores 0 for the list's first name, 2 for another, 0x20 for
// none (without a list, 0, or 4 if its kind is not `kind`), plus 8 if its
// styles are fewer than asked (the asked's bits 2 to 5 aside), 0x10 if
// they have one more; the first lowest wins. With `reuse`, a font of that
// size and style already made of it is AddRef'd and given; else slot 10
// makes one. With `decision`, nothing is AddRef'd or made: what would be
// is noted there.
void* GetFont(void* self, void* faces, uint32_t kind, uint32_t size, uint32_t style, uint32_t reuse,
              Decision* decision)
{
    const Address at = At(self);
    void* result = nullptr;
    Address best = 0, bestKey = 0;
    int32_t bestScore = static_cast<int32_t>(kNoScore);
    String list;
    FromIgz(&list, faces);
    String current;
    Construct(&current);
    const uint32_t kept = style & kStyleKept;
    Lower(&list);
    String names[kNamesAtMost];
    for (String& name : names)
        Construct(&name);
    uint32_t count = kNamesAtMost;
    Cdecl<void>(kSplitText, list.begin, list.end - list.begin, uint32_t{','}, names, &count);

    // Names without a face of their own become the alias that lists them.
    for (int32_t i = static_cast<int32_t>(count) - 1; i >= 0; --i) {
        String* const name = &names[i];
        for (Address node = First(at + sys::kAliases); node != Header(at + sys::kAliases); node = TreeNext(node)) {
            uint32_t n = static_cast<uint32_t>(Count14(Load<Address>(node + kNodeValue), Load<Address>(node + kNodeValue + 4)));
            if (n == 0)
                continue;
            for (uint32_t j = 0; j < n;) {
                const Address face = Load<Address>(node + kNodeValue) + j * 0x14;
                if (Virtual<uint8_t>(name, kIgzEquals, face, uint32_t{0})) {
                    String lower;
                    CopyOf(&lower, name);
                    Lower(&lower);
                    const Address set = at + sys::kNames;
                    const Address setHeader = Header(set);
                    if (FindIn(kNamesFind, set, &lower) == setHeader) {
                        Assign(name, Ptr(node + kNodeKey));
                        Lower(name);
                        j = static_cast<uint32_t>(
                                Count14(Load<Address>(node + kNodeValue), Load<Address>(node + kNodeValue + 4))) -
                            1;
                    }
                    Drop(&lower);
                }
                n = static_cast<uint32_t>(Count14(Load<Address>(node + kNodeValue), Load<Address>(node + kNodeValue + 4)));
                ++j;
            }
        }
    }

    const Address header = Header(at + sys::kFaces);
    for (Address node = Load<Address>(header + kHeaderFirst); node != header; node = TreeNext(node)) {
        const Address value = node + kNodeValue;
        int32_t score = 0;
        if (count == 0) {
            if (Load<uint32_t>(value + 0x1C) != kind)
                score = 4;
        } else {
            Assign(&current, Ptr(value + 4));
            Lower(&current);
            uint32_t j = 0;
            while (j < count && !Cdecl<uint8_t>(kBodyEquals, Body(&names[j]), Body(&current)))
                ++j;
            if (j >= count)
                score = 0x20;
            else if (static_cast<int32_t>(j) > 0)
                score = 2;
        }
        const uint32_t styles = Load<uint32_t>(value + 0x18);
        if (styles != kept)
            score += (styles & ~kept) == 0 ? 8 : 0x10;
        if (score < bestScore) {
            best = value;
            bestKey = node + kNodeKey;
            bestScore = score;
        }
    }

    if (best) {
        bool found = false;
        if (reuse & 0xFF) {
            const Address fonts = Load<Address>(best);
            for (Address entry = Load<Address>(fonts); entry != fonts; entry = Load<Address>(entry)) {
                const Address made = Load<Address>(entry + kEntryFont);
                if (VirtualAt<uint32_t>(made, font::kSize) == size && Load<uint32_t>(entry + kEntryStyle) == style) {
                    result = Ptr(made);
                    if (decision)
                        decision->reused = made;
                    else
                        VirtualAt<uint32_t>(made, font::kAddRef);
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            if (decision) {
                *decision = Decision{true, 0, bestKey, size, style, reuse};
            } else {
                result = Virtual<void*>(self, sys::kMakeFont, Ptr(bestKey), size, style, reuse);
            }
        }
    }
    reinterpret_cast<void(__stdcall*)(void*, uint32_t, uint32_t, std::uintptr_t)>(kDestroyArray)(
        names, sizeof(String), kNamesAtMost, kStringDestroy);
    Drop(&current);
    Drop(&list);
    return result;
}

// The game's 0x00895A1C (slot 10, ret 0x10): a font of the file at `path`
// (tried as it is, else in Windows's Fonts folder): with `reuse`, one of
// that size and style already in a face of that path (the last such face's)
// if any; else made through the COM (or by hand if the COM has none),
// given the path, size, style, the system's encoding and blitter, and draw
// mode 1 if the size is in the system's range or the face's own (else the
// system's default mode), and loaded. With `reuse` a font made is kept in
// the faces (a new face named by its family if the path is not one). Null
// for size 0 or a font that does not load.
void* MakeFont(void* self, void* pathIn, uint32_t size, uint32_t style, uint32_t reuse)
{
    const Address at = At(self);
    const Address faces = at + sys::kFaces;
    void* made = nullptr;
    String path;
    FromIgz(&path, pathIn);
    if (!Exists(&path)) {
        String name;
        CopyOf(&name, &path);
        char windows[kWindowsPath];
        if (WindowsDirectory(windows)) {
            String text;
            FromText(&text, At(windows));
            Assign(&path, &text);
            FreeText(text.begin);
            AppendFonts(&path);
            This<void>(kBodyAppendBody, Ptr(Body(&path)), Body(&name));
        }
        FreeText(name.begin);
    }
    if (size == 0) {
        Drop(&path);
        return nullptr;
    }
    const bool keep = (reuse & 0xFF) != 0;
    if (keep) {
        const Address header = Header(faces);
        for (Address node = Load<Address>(header + kHeaderFirst); node != header; node = TreeNext(node)) {
            if (!VirtualAt<uint8_t>(node + kNodeKey, kIgzEquals, &path, uint32_t{1}))
                continue;
            const Address list = Load<Address>(node + kFaceList);
            for (Address entry = Load<Address>(list); entry != list; entry = Load<Address>(entry)) {
                const Address one = Load<Address>(entry + kEntryFont);
                if (VirtualAt<uint32_t>(one, font::kSize) == size && Load<uint32_t>(entry + kEntryStyle) == style) {
                    made = Ptr(one);
                    break;
                }
            }
        }
    }
    if (!made) {
        void* const com = Cdecl<void*>(addr::RZGetCOM);
        if (!Virtual<uint8_t>(com, kComGetClassObject, kFontClass, kFontInterface, &made)) {
            void* const space = Cdecl<void*>(addr::SC4_operator_new, kFontSize);
            made = space ? This<void*>(addr::GZFont_Construct, space) : nullptr;
            Virtual<uint32_t>(made, font::kAddRef);
        }
        Virtual<void>(made, font::kSetPath, &path);
        Virtual<uint32_t>(made, font::kSetSize, size);
        Virtual<uint32_t>(made, font::kSetStyle, style, uint32_t{1});
        Virtual<uint32_t>(made, font::kSetEncoding, Load<uint32_t>(at + sys::kEncoding));
        Virtual<void>(made, font::kSetBlitter, Load<Address>(at + sys::kBlitter));
        uint32_t mode = Load<uint32_t>(at + sys::kDrawMode);
        if (Load<uint32_t>(at + sys::kMonoMin) <= size && size <= Load<uint32_t>(at + sys::kMonoMax)) {
            mode = 1;
        } else {
            String family;
            Construct(&family);
            Virtual<void>(made, font::kFamily, &family);
            const Address ranges = at + sys::kRanges;
            const Address node = FindIn(kFacesFind, ranges, &family);
            if (node != Header(ranges) && Load<uint32_t>(node + kRangeMin) <= size && size <= Load<uint32_t>(node + kRangeMax))
                mode = 1;
            Drop(&family);
        }
        Virtual<void>(made, font::kSetMode, mode);
        if (!Virtual<uint8_t>(made, font::kLoad)) {
            Virtual<uint32_t>(made, font::kRelease);
            Drop(&path);
            return nullptr;
        }
        if (keep) {
            String copy;
            CopyOf(&copy, &path);
            const Address node = FindIn(kFacesFind, faces, &copy);
            Drop(&copy);
            Entry entry{At(made), style};
            if (node == Header(faces)) {
                alignas(8) uint8_t pair[kFacePairBytes];
                {
                    FaceValue value;
                    This<void*>(kFacePairMake, pair, &path, value.bytes);
                }
                Virtual<void>(made, font::kFamily, pair + kPairName);
                Store<uint32_t>(At(pair) + kPairStyles, Virtual<uint32_t>(made, font::kStyle));
                Store<uint32_t>(At(pair) + kPairKind, Virtual<uint32_t>(made, font::kKind));
                uint8_t result[8];
                This<void*>(kListInsert, pair + kPairList, result, Load<Address>(At(pair) + kPairList), &entry);
                This<void*>(kFacesInsert, Ptr(faces), result, pair);
                Lower(pair + kPairName);
                This<void*>(kNamesInsert, Ptr(at + sys::kNames), result, pair + kPairName);
                This<void>(kFaceFree, pair + kPairList);
                Drop(Str(At(pair)));
            } else {
                This<void>(kListPushBack, Ptr(node + kFaceList), &entry);
            }
        }
    }
    Virtual<uint32_t>(made, font::kAddRef);
    if (!keep)
        Virtual<uint32_t>(made, font::kRelease);
    Drop(&path);
    return made;
}

// The game's 0x008942E3 (slot 9, ret 0x14): GetFont without a list of faces
// (by kind and style), its colours set to `colour` and the font's own.
void* GetFontFor(void* self, uint32_t kind, uint32_t size, uint32_t style, uint32_t colour, uint32_t reuse)
{
    String none;
    Construct(&none);
    void* const made = Virtual<void*>(self, sys::kGetFont, &none, kind, size, style, reuse);
    Drop(&none);
    if (made)
        Virtual<void>(made, font::kSetColours, colour, kColourOwn);
    return made;
}

// The game's 0x008954CB (slot 8, ret 0xC): the sizes drawn in mode 1, for
// every face (without a name) or for one face's (a new entry; an entry of
// the face already there is kept as it was); a face's entry is removed if
// `low` is not below `high` (signed).
void SetMonoRange(void* self, int32_t low, int32_t high, void* face)
{
    const Address at = At(self);
    if (!face) {
        Store<int32_t>(at + sys::kMonoMin, low);
        Store<int32_t>(at + sys::kMonoMax, high);
        return;
    }
    String key;
    FromIgz(&key, face);
    if (low >= high) {
        This<uint32_t>(kRangesErase, Ptr(at + sys::kRanges), &key);
        Drop(&key);
        return;
    }
    const int32_t range[2] = {low, high};
    alignas(8) uint8_t pair[kRangePairBytes];
    This<void*>(kRangePairMake, pair, &key, range);
    uint8_t result[8];
    This<void*>(kRangesInsert, Ptr(at + sys::kRanges), result, pair);
    FreeText(Load<Address>(At(pair) + 4));
    FreeText(key.begin);
}

// ====================================================================================
// Detours

enum Group { kObject, kFaces, kAliases, kGetFont, kGroups };
bool g_compare[kGroups] = {};

// Above 0 while the game's side of a comparison runs: our detours inside it
// pass through to the game's.
int g_inGame = 0;

struct GameSide {
    GameSide() { ++g_inGame; }
    ~GameSide() { --g_inGame; }
    GameSide(const GameSide&) = delete;
    GameSide& operator=(const GameSide&) = delete;
};

std::string Words(std::initializer_list<uint32_t> words)
{
    std::string text;
    char one[12];
    for (const uint32_t w : words) {
        std::snprintf(one, sizeof one, "%X ", w);
        text += one;
    }
    return text;
}

std::string Hex(const uint8_t* bytes, std::size_t count)
{
    std::string text;
    char two[4];
    for (std::size_t i = 0; i < count; ++i) {
        std::snprintf(two, sizeof two, "%02X", bytes[i]);
        text += two;
    }
    return text;
}

std::string Text(Address string)
{
    const Address begin = Load<Address>(string + 4);
    const Address end = Load<Address>(string + 8);
    return begin ? std::string(static_cast<const char*>(Ptr(begin)), end - begin) : std::string();
}

void Report(const char* id, const std::string& game, const std::string& ours)
{
    compare::Record(id, game == ours, game == ours ? std::string() : game, game == ours ? std::string() : ours);
}

// The system's tables as text: the faces (path, name, styles, kind, type,
// the fonts' sizes and styles), the names, the families, the aliases, the
// ranges, the patterns and the fields.
std::string Tables(void* self, bool withType)
{
    const Address at = At(self);
    std::string text = Words({Load<uint8_t>(at + sys::kReady), Load<uint32_t>(at + sys::kRefs),
                              Load<uint32_t>(at + sys::kDrawMode), Load<uint32_t>(at + sys::kMonoMin),
                              Load<uint32_t>(at + sys::kMonoMax), Load<uint32_t>(at + sys::kEncoding),
                              Load<uint32_t>(at + sys::kBlitter)});
    const auto each = [&](Address map, auto&& fn) {
        text += "| " + Words({Load<uint32_t>(map + 4)});
        for (Address node = First(map); node != Header(map); node = TreeNext(node))
            fn(node);
    };
    each(at + sys::kFaces, [&](Address node) {
        text += Text(node + kNodeKey) + "=" + Text(node + kFaceName) + " " +
                Words({Load<uint32_t>(node + kFaceStyles), Load<uint32_t>(node + kFaceKind),
                       withType ? Load<uint32_t>(node + kFaceType) : 0u});
        const Address list = Load<Address>(node + kFaceList);
        for (Address entry = Load<Address>(list); entry != list; entry = Load<Address>(entry))
            text += Words({Load<uint32_t>(entry + kEntryStyle)});
        text += "; ";
    });
    each(at + sys::kNames, [&](Address node) { text += Text(node + kNodeKey) + "; "; });
    each(at + sys::kFamilies, [&](Address node) {
        text += Words({Load<uint32_t>(node + kNodeKey)}) + Text(node + kFamilyName) + "; ";
    });
    each(at + sys::kAliases, [&](Address node) {
        text += Text(node + kNodeKey) + "=";
        const Address begin = Load<Address>(node + kNodeValue), end = Load<Address>(node + kNodeValue + 4);
        for (Address name = begin; name < end; name += 0x14)
            text += Text(name) + ",";
        text += "; ";
    });
    each(at + sys::kRanges, [&](Address node) {
        text += Text(node + kNodeKey) + Words({Load<uint32_t>(node + kRangeMin), Load<uint32_t>(node + kRangeMax)}) + "; ";
    });
    text += "| ";
    for (Address p = Load<Address>(at + sys::kPatterns); p < Load<Address>(at + sys::kPatterns + 4); p += 0x14)
        text += Text(p) + " ";
    return text;
}

// --- the object

using ThisOnlyFn = uint32_t(__fastcall*)(void*, void*);
using ThisOneFn = uint32_t(__fastcall*)(void*, void*, uint32_t);
using ThisTwoFn = uint32_t(__fastcall*)(void*, void*, uint32_t, uint32_t);
using ThisThreeFn = uint32_t(__fastcall*)(void*, void*, uint32_t, uint32_t, uint32_t);
using ThisFourFn = uint32_t(__fastcall*)(void*, void*, uint32_t, uint32_t, uint32_t, uint32_t);
using ThisFiveFn = uint32_t(__fastcall*)(void*, void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

ThisOnlyFn g_construct = nullptr;
ThisOnlyFn g_destruct = nullptr;
ThisOneFn g_deletingDestruct = nullptr;
ThisTwoFn g_queryInterface = nullptr;
ThisOnlyFn g_release = nullptr;
ThisOnlyFn g_destroy = nullptr;
ThisOnlyFn g_shutdown = nullptr;
ThisOneFn g_setEncoding = nullptr;
ThisOneFn g_setBlitter = nullptr;
ThisOnlyFn g_version = nullptr;
ThisTwoFn g_queryInterface2 = nullptr;
ThisOnlyFn g_release2 = nullptr;
ThisOnlyFn g_shutdown2 = nullptr;

void* Self2(void* plus4) { return Ptr(At(plus4) - 4); }

uint32_t __fastcall DConstruct(void* self, void*)
{
    OPENSC4_FIRST("Construct 0x00895268");
    if (g_inGame || g_compare[kObject])
        return g_construct(self, nullptr);
    return At(Construct(self));
}

uint32_t __fastcall DDestruct(void* self, void*)
{
    OPENSC4_FIRST("Destruct 0x00894F2C");
    if (g_inGame || g_compare[kObject])
        return g_destruct(self, nullptr);
    Destruct(self);
    return 0;
}

uint32_t __fastcall DDeletingDestruct(void* self, void*, uint32_t flags)
{
    OPENSC4_FIRST("DeletingDestruct 0x0089538D");
    if (g_inGame || g_compare[kObject])
        return g_deletingDestruct(self, nullptr, flags);
    return At(DeletingDestruct(self, flags));
}

uint32_t __fastcall DQueryInterface(void* self, void*, uint32_t iid, Address* out)
{
    OPENSC4_FIRST("QueryInterface 0x00893D2C");
    if (g_inGame)
        return g_queryInterface(self, nullptr, iid, At(out));
    if (!g_compare[kObject])
        return QueryInterface(self, iid, out) ? 1 : 0;
    // Ours first, what it wrote and the count kept aside and put back (the
    // AddRef only counts up), then the game's.
    // Neither reads `out` for an ID they do not answer.
    const bool answers = iid == kIidGzUnknown || iid == kIidFontSystem || iid == kIidFontSystem2;
    if (!answers)
        return g_queryInterface(self, nullptr, iid, At(out));
    const Address refsAt = At(self) + sys::kRefs;
    const uint32_t refs = Load<uint32_t>(refsAt);
    const Address before = *out;
    const uint32_t mine = QueryInterface(self, iid, out) ? 1 : 0;
    const std::string ours = Words({mine, *out - At(self), Load<uint32_t>(refsAt) - refs});
    *out = before;
    Store<uint32_t>(refsAt, refs);
    uint32_t result;
    {
        GameSide side;
        result = g_queryInterface(self, nullptr, iid, At(out));
    }
    Report("fontsystem.queryinterface", Words({result & 0xFF, *out - At(self), Load<uint32_t>(refsAt) - refs}), ours);
    return result;
}

uint32_t __fastcall DRelease(void* self, void*)
{
    OPENSC4_FIRST("Release 0x00893DD7");
    if (g_inGame || g_compare[kObject])
        return g_release(self, nullptr);
    return Release(self);
}

uint32_t __fastcall DDestroy(void* self, void*)
{
    OPENSC4_FIRST("Destroy 0x00893D68");
    if (g_inGame || g_compare[kObject])
        return g_destroy(self, nullptr);
    Destroy(self);
    return 0;
}

uint32_t __fastcall DShutdown(void* self, void*)
{
    OPENSC4_FIRST("Shutdown 0x00894F8D");
    if (g_inGame || g_compare[kObject])
        return g_shutdown(self, nullptr);
    return Shutdown(self) ? 1 : 0;
}

uint32_t __fastcall DSetEncoding(void* self, void*, uint32_t encoding)
{
    OPENSC4_FIRST("SetEncoding 0x00893D7B");
    if (g_inGame)
        return g_setEncoding(self, nullptr, encoding);
    if (!g_compare[kObject])
        return SetEncoding(self, encoding) ? 1 : 0;
    const Address field = At(self) + sys::kEncoding;
    const uint32_t before = Load<uint32_t>(field);
    const uint32_t mine = SetEncoding(self, encoding) ? 1 : 0;
    const std::string ours = Words({mine, Load<uint32_t>(field)});
    Store<uint32_t>(field, before);
    uint32_t result;
    {
        GameSide side;
        result = g_setEncoding(self, nullptr, encoding);
    }
    Report("fontsystem.setencoding", Words({result & 0xFF, Load<uint32_t>(field)}), ours);
    return result;
}

uint32_t __fastcall DSetBlitter(void* self, void*, uint32_t blitter)
{
    OPENSC4_FIRST("SetBlitter 0x00893D91");
    if (g_inGame || g_compare[kObject])
        return g_setBlitter(self, nullptr, blitter);
    SetBlitter(self, Ptr(blitter));
    return 0;
}

uint32_t __fastcall DVersion(void* self, void*)
{
    OPENSC4_FIRST("Version 0x00894F77");
    if (g_inGame)
        return g_version(self, nullptr);
    if (!g_compare[kObject])
        return Version();
    return compare::Call("fontsystem.version", [&] {
        GameSide side;
        return g_version(self, nullptr);
    }, [&] { return Version(); });
}

// The +4 vtable's thunks: ours call ours with the system; in compare mode
// the game's thunk, which reaches our detour of what it jumps to.
uint32_t __fastcall DQueryInterface2(void* plus4, void*, uint32_t iid, Address* out)
{
    OPENSC4_FIRST("QueryInterface2 0x00894F7D");
    if (g_inGame || g_compare[kObject])
        return g_queryInterface2(plus4, nullptr, iid, At(out));
    return QueryInterface(Self2(plus4), iid, out) ? 1 : 0;
}

uint32_t __fastcall DRelease2(void* plus4, void*)
{
    OPENSC4_FIRST("Release2 0x00894F85");
    if (g_inGame || g_compare[kObject])
        return g_release2(plus4, nullptr);
    return Release(Self2(plus4));
}

uint32_t __fastcall DShutdown2(void* plus4, void*)
{
    OPENSC4_FIRST("Shutdown2 0x00895260");
    if (g_inGame || g_compare[kObject])
        return g_shutdown2(plus4, nullptr);
    return Shutdown(Self2(plus4)) ? 1 : 0;
}

OPENSC4_THUNK(TConstruct, DConstruct, 0)
OPENSC4_THUNK(TDestruct, DDestruct, 0)
OPENSC4_THUNK(TDeletingDestruct, DDeletingDestruct, 4)
OPENSC4_THUNK(TQueryInterface, DQueryInterface, 8)
OPENSC4_THUNK(TRelease, DRelease, 0)
OPENSC4_THUNK(TDestroy, DDestroy, 0)
OPENSC4_THUNK(TShutdown, DShutdown, 0)
OPENSC4_THUNK(TSetEncoding, DSetEncoding, 4)
OPENSC4_THUNK(TSetBlitter, DSetBlitter, 4)
OPENSC4_THUNK(TVersion, DVersion, 0)
OPENSC4_THUNK(TQueryInterface2, DQueryInterface2, 8)
OPENSC4_THUNK(TRelease2, DRelease2, 0)
OPENSC4_THUNK(TShutdown2, DShutdown2, 0)

// A system of the game's own making, for a comparison: allocated by the
// game's allocator, made by the game's constructor, taken down by the game's
// destructor (it holds no fonts).
struct Scratch {
    void* self;
    Scratch()
    {
        GameSide side;
        self = Cdecl<void*>(addr::SC4_operator_new, sys::kSize);
        (g_construct ? g_construct : reinterpret_cast<ThisOnlyFn>(addr::GZFontSystem_Construct))(self, nullptr);
    }
    ~Scratch()
    {
        GameSide side;
        (g_destruct ? g_destruct : reinterpret_cast<ThisOnlyFn>(addr::GZFontSystem_Destruct))(self, nullptr);
        Cdecl<void>(addr::SC4_operator_delete, self);
    }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
};

// --- faces

ThisOneFn g_fileType = nullptr;
ThisOneFn g_addFontFile = nullptr;
ThisOneFn g_addFontFolder = nullptr;
ThisOneFn g_addFont = nullptr;
ThisOneFn g_removeFace = nullptr;
ThisOneFn g_releaseFont = nullptr;
ThisTwoFn g_listFaces = nullptr;
ThisTwoFn g_listFonts = nullptr;
ThisThreeFn g_hasFace = nullptr;

uint32_t __fastcall DFileType(void* self, void*, uint32_t path)
{
    OPENSC4_FIRST("FileType 0x0089433C");
    if (g_inGame)
        return g_fileType(self, nullptr, path);
    if (!g_compare[kFaces])
        return FileType(Ptr(path));
    return compare::Call("fontsystem.filetype", [&] {
        GameSide side;
        return g_fileType(self, nullptr, path);
    }, [&] { return FileType(Ptr(path)); });
}

uint32_t __fastcall DAddFontFile(void* self, void*, uint32_t path)
{
    OPENSC4_FIRST("AddFontFile 0x00895564");
    if (g_inGame)
        return g_addFontFile(self, nullptr, path);
    if (!g_compare[kFaces])
        return AddFontFile(self, Ptr(path)) ? 1 : 0;
    // Ours and the game's on two scratch systems, their tables compared;
    // then the game's on this one.
    {
        Scratch ours, game;
        const uint32_t mine = AddFontFile(ours.self, Ptr(path)) ? 1 : 0;
        uint32_t theirs;
        {
            GameSide side;
            theirs = g_addFontFile(game.self, nullptr, path);
        }
        Report("fontsystem.addfontfile", Words({theirs & 0xFF}) + Tables(game.self, true),
               Words({mine}) + Tables(ours.self, true));
    }
    return g_addFontFile(self, nullptr, path);
}

uint32_t __fastcall DAddFontFolder(void* self, void*, uint32_t folder)
{
    OPENSC4_FIRST("AddFontFolder 0x00894864");
    if (g_inGame || g_compare[kFaces])
        return g_addFontFolder(self, nullptr, folder);
    return AddFontFolder(self, Ptr(folder));
}

uint32_t __fastcall DAddFont(void* self, void*, uint32_t made)
{
    OPENSC4_FIRST("AddFont 0x008957D6");
    if (g_inGame || g_compare[kFaces])
        return g_addFont(self, nullptr, made);
    return AddFont(self, Ptr(made)) ? 1 : 0;
}

uint32_t __fastcall DRemoveFace(void* self, void*, uint32_t name)
{
    OPENSC4_FIRST("RemoveFace 0x00894A3A");
    if (g_inGame || g_compare[kFaces])
        return g_removeFace(self, nullptr, name);
    return RemoveFace(self, Ptr(name)) ? 1 : 0;
}

uint32_t __fastcall DReleaseFont(void* self, void*, uint32_t made)
{
    OPENSC4_FIRST("ReleaseFont 0x00893F22");
    if (g_inGame || g_compare[kFaces])
        return g_releaseFont(self, nullptr, made);
    return ReleaseFont(self, Ptr(made));
}

// Ours first, what it wrote kept aside and the bytes put back, then the
// game's; the results and the bytes compared.
template <typename Original, typename Ours>
uint32_t ComparedBytes(const char* id, uint8_t* p, std::size_t size, Original&& original, Ours&& ours)
{
    const std::vector<uint8_t> before(p, p + size);
    const uint32_t mine = ours();
    const std::vector<uint8_t> written(p, p + size);
    std::memcpy(p, before.data(), size);
    uint32_t result;
    {
        GameSide side;
        result = original();
    }
    const bool same = result == mine && std::memcmp(p, written.data(), size) == 0;
    compare::Record(id, same, same ? std::string() : Words({result}) + Hex(p, size < 64 ? size : 64),
                    same ? std::string() : Words({mine}) + Hex(written.data(), size < 64 ? size : 64));
    return result;
}

uint32_t __fastcall DListFaces(void* self, void*, uint32_t out, uint32_t count)
{
    OPENSC4_FIRST("ListFaces 0x00893E5B");
    if (g_inGame)
        return g_listFaces(self, nullptr, out, count);
    if (!g_compare[kFaces])
        return ListFaces(self, static_cast<uint8_t*>(Ptr(out)), count);
    // What it writes: as many as it answers, 0x88 bytes each.
    uint32_t n = Load<uint32_t>(At(self) + sys::kFaceCount);
    if (count == 0 || n > count)
        n = count;
    return ComparedBytes("fontsystem.listfaces", static_cast<uint8_t*>(Ptr(out)), std::size_t{n} * 0x88,
                         [&] { return g_listFaces(self, nullptr, out, count); },
                         [&] { return ListFaces(self, static_cast<uint8_t*>(Ptr(out)), count); });
}

uint32_t __fastcall DListFonts(void* self, void*, uint32_t out, uint32_t count)
{
    OPENSC4_FIRST("ListFonts 0x00893ECB");
    if (g_inGame)
        return g_listFonts(self, nullptr, out, count);
    if (!g_compare[kFaces])
        return ListFonts(self, static_cast<Address*>(Ptr(out)), count);
    const uint32_t total = ListFonts(self, nullptr, 0);
    const uint32_t n = count < total ? count : total;
    return ComparedBytes("fontsystem.listfonts", static_cast<uint8_t*>(Ptr(out)), std::size_t{n} * 4,
                         [&] { return g_listFonts(self, nullptr, out, count); },
                         [&] { return ListFonts(self, static_cast<Address*>(Ptr(out)), count); });
}

uint32_t __fastcall DHasFace(void* self, void*, uint32_t name, uint32_t unused, uint32_t style)
{
    OPENSC4_FIRST("HasFace 0x008944B2");
    if (g_inGame)
        return g_hasFace(self, nullptr, name, unused, style);
    if (!g_compare[kFaces])
        return HasFace(self, Ptr(name), unused, style) ? 1 : 0;
    return compare::Call("fontsystem.hasface", [&] {
        GameSide side;
        return g_hasFace(self, nullptr, name, unused, style) & 0xFF;
    }, [&] { return HasFace(self, Ptr(name), unused, style) ? 1u : 0u; });
}

OPENSC4_THUNK(TFileType, DFileType, 4)
OPENSC4_THUNK(TAddFontFile, DAddFontFile, 4)
OPENSC4_THUNK(TAddFontFolder, DAddFontFolder, 4)
OPENSC4_THUNK(TAddFont, DAddFont, 4)
OPENSC4_THUNK(TRemoveFace, DRemoveFace, 4)
OPENSC4_THUNK(TReleaseFont, DReleaseFont, 4)
OPENSC4_THUNK(TListFaces, DListFaces, 8)
OPENSC4_THUNK(TListFonts, DListFonts, 8)
OPENSC4_THUNK(THasFace, DHasFace, 12)

// --- aliases

ThisOnlyFn g_init = nullptr;
ThisOnlyFn g_init2 = nullptr;
ThisTwoFn g_setFamily = nullptr;
ThisTwoFn g_addAlias = nullptr;
ThisThreeFn g_getAliasFaces = nullptr;
ThisTwoFn g_resolveAlias = nullptr;

uint32_t __fastcall DInit(void* self, void*)
{
    OPENSC4_FIRST("Init 0x00895E68");
    if (g_inGame)
        return g_init(self, nullptr);
    if (!g_compare[kAliases])
        return Init(self) ? 1 : 0;
    {
        Scratch ours, game;
        Store<uint8_t>(At(ours.self) + sys::kReady, Load<uint8_t>(At(self) + sys::kReady));
        Store<uint8_t>(At(game.self) + sys::kReady, Load<uint8_t>(At(self) + sys::kReady));
        const uint32_t mine = Init(ours.self) ? 1 : 0;
        uint32_t theirs;
        {
            GameSide side;
            theirs = g_init(game.self, nullptr);
        }
        Report("fontsystem.init", Words({theirs & 0xFF}) + Tables(game.self, true),
               Words({mine}) + Tables(ours.self, true));
    }
    return g_init(self, nullptr);
}

uint32_t __fastcall DInit2(void* plus4, void*)
{
    OPENSC4_FIRST("Init2 0x00895F2E");
    if (g_inGame || g_compare[kAliases])
        return g_init2(plus4, nullptr);
    return Init(Self2(plus4)) ? 1 : 0;
}

uint32_t __fastcall DSetFamily(void* self, void*, uint32_t code, uint32_t name)
{
    OPENSC4_FIRST("SetFamily 0x00895DCF");
    if (g_inGame || g_compare[kAliases])
        return g_setFamily(self, nullptr, code, name);
    return SetFamily(self, code, Ptr(name)) ? 1 : 0;
}

uint32_t __fastcall DAddAlias(void* self, void*, uint32_t alias, uint32_t face)
{
    OPENSC4_FIRST("AddAlias 0x0089595D");
    if (g_inGame || g_compare[kAliases])
        return g_addAlias(self, nullptr, alias, face);
    return AddAlias(self, Ptr(alias), Ptr(face)) ? 1 : 0;
}

// The text of a cIGZString through its own slots.
std::string IgzText(void* s)
{
    const uint32_t length = Virtual<uint32_t>(s, kIgzLength);
    const char* const data = Virtual<const char*>(s, kIgzData);
    return data ? std::string(data, length) : std::string();
}

uint32_t __fastcall DGetAliasFaces(void* self, void*, uint32_t alias, uint32_t outs, uint32_t count)
{
    OPENSC4_FIRST("GetAliasFaces 0x00894198");
    void** const strings = static_cast<void**>(Ptr(outs));
    if (g_inGame)
        return g_getAliasFaces(self, nullptr, alias, outs, count);
    if (!g_compare[kAliases])
        return GetAliasFaces(self, Ptr(alias), strings, count);
    // Both assign the same strings the same texts: ours, then the game's.
    const auto state = [&](uint32_t result) {
        std::string text = Words({result});
        for (uint32_t i = 0; i < count; ++i)
            text += strings[i] ? IgzText(strings[i]) + "," : "-,";
        return text;
    };
    const std::string mine = state(GetAliasFaces(self, Ptr(alias), strings, count));
    uint32_t result;
    {
        GameSide side;
        result = g_getAliasFaces(self, nullptr, alias, outs, count);
    }
    Report("fontsystem.getaliasfaces", state(result), mine);
    return result;
}

uint32_t __fastcall DResolveAlias(void* self, void*, uint32_t alias, uint32_t out)
{
    OPENSC4_FIRST("ResolveAlias 0x0089423C");
    if (g_inGame)
        return g_resolveAlias(self, nullptr, alias, out);
    if (!g_compare[kAliases])
        return ResolveAlias(self, Ptr(alias), Ptr(out)) ? 1 : 0;
    // The string is assigned only when a name is found, the same text by
    // both: compared after each.
    const uint32_t mine = ResolveAlias(self, Ptr(alias), Ptr(out)) ? 1 : 0;
    const std::string ours = Words({mine}) + IgzText(Ptr(out));
    uint32_t result;
    {
        GameSide side;
        result = g_resolveAlias(self, nullptr, alias, out);
    }
    Report("fontsystem.resolvealias", Words({result & 0xFF}) + IgzText(Ptr(out)), ours);
    return result;
}

OPENSC4_THUNK(TInit, DInit, 0)
OPENSC4_THUNK(TInit2, DInit2, 0)
OPENSC4_THUNK(TSetFamily, DSetFamily, 8)
OPENSC4_THUNK(TAddAlias, DAddAlias, 8)
OPENSC4_THUNK(TGetAliasFaces, DGetAliasFaces, 12)
OPENSC4_THUNK(TResolveAlias, DResolveAlias, 8)

// --- fonts

ThisFiveFn g_getFont = nullptr;
ThisFourFn g_makeFont = nullptr;
ThisFiveFn g_getFontFor = nullptr;
ThisThreeFn g_setMonoRange = nullptr;

// While the game's GetFont runs in compare mode: its call to slot 10, noted.
Decision* g_noted = nullptr;

uint32_t __fastcall DMakeFont(void* self, void*, uint32_t path, uint32_t size, uint32_t style, uint32_t reuse)
{
    OPENSC4_FIRST("MakeFont 0x00895A1C");
    if (g_noted) {
        *g_noted = Decision{true, 0, path, size, style, reuse};
        g_noted = nullptr;
    }
    if (g_inGame || g_compare[kGetFont])
        return g_makeFont(self, nullptr, path, size, style, reuse);
    return At(MakeFont(self, Ptr(path), size, style, reuse));
}

std::string DecisionText(const Decision& d)
{
    if (d.made)
        return "make " + Words({d.key, d.size, d.style, d.reuse});
    return "reuse " + Words({d.reused});
}

uint32_t __fastcall DGetFont(void* self, void*, uint32_t faces, uint32_t kind, uint32_t size, uint32_t style,
                             uint32_t reuse)
{
    OPENSC4_FIRST("GetFont 0x00894ADF");
    if (g_inGame)
        return g_getFont(self, nullptr, faces, kind, size, style, reuse);
    if (!g_compare[kGetFont])
        return At(GetFont(self, Ptr(faces), kind, size, style, reuse, nullptr));
    // Ours to its decision without its effects, then the game's with its
    // call to slot 10 noted. A font reused is the game's result; a font
    // made, the face, size, style and reuse slot 10 is asked for.
    Decision mine;
    GetFont(self, Ptr(faces), kind, size, style, reuse, &mine);
    Decision theirs;
    uint32_t result;
    {
        GameSide side;
        g_noted = &theirs;
        result = g_getFont(self, nullptr, faces, kind, size, style, reuse);
        g_noted = nullptr;
    }
    if (!theirs.made)
        theirs.reused = result;
    Report("fontsystem.getfont", DecisionText(theirs), DecisionText(mine));
    return result;
}

uint32_t __fastcall DGetFontFor(void* self, void*, uint32_t kind, uint32_t size, uint32_t style, uint32_t colour,
                                uint32_t reuse)
{
    OPENSC4_FIRST("GetFontFor 0x008942E3");
    if (g_inGame || g_compare[kGetFont])
        return g_getFontFor(self, nullptr, kind, size, style, colour, reuse);
    return At(GetFontFor(self, kind, size, style, colour, reuse));
}

uint32_t __fastcall DSetMonoRange(void* self, void*, uint32_t low, uint32_t high, uint32_t face)
{
    OPENSC4_FIRST("SetMonoRange 0x008954CB");
    if (g_inGame || g_compare[kGetFont])
        return g_setMonoRange(self, nullptr, low, high, face);
    SetMonoRange(self, static_cast<int32_t>(low), static_cast<int32_t>(high), Ptr(face));
    return 0;
}

OPENSC4_THUNK(TGetFont, DGetFont, 20)
OPENSC4_THUNK(TMakeFont, DMakeFont, 16)
OPENSC4_THUNK(TGetFontFor, DGetFontFor, 20)
OPENSC4_THUNK(TSetMonoRange, DSetMonoRange, 12)

// ====================================================================================
// Installation

struct Detour {
    std::uintptr_t address;
    void (*thunk)();
    void** original;
};

template <typename Fn>
void** Slot(Fn* fn)
{
    return reinterpret_cast<void**>(fn);
}

template <std::size_t N>
bool InstallAll(registry::Mode mode, Group group, const char* name, const Detour (&detours)[N])
{
    if (!game::IsBuild641())
        return false;
    g_compare[group] = mode == registry::Mode::Compare;
    for (const Detour& detour : detours) {
        const addr::Entry* entry = addr_check::Find(detour.address);
        if (!entry || !hook::Install(*entry, reinterpret_cast<void*>(detour.thunk), detour.original))
            return false;
    }
    log::Write("fontsystem: %s: %zu functions %s", name, N,
               mode == registry::Mode::Compare ? "compared with the game's" : "ours");
    return true;
}

bool InstallObject(registry::Mode mode)
{
    const Detour detours[] = {
        {addr::GZFontSystem_Construct, &TConstruct, Slot(&g_construct)},
        {addr::GZFontSystem_Destruct, &TDestruct, Slot(&g_destruct)},
        {addr::GZFontSystem_DeletingDestruct, &TDeletingDestruct, Slot(&g_deletingDestruct)},
        {addr::GZFontSystem_QueryInterface, &TQueryInterface, Slot(&g_queryInterface)},
        {addr::GZFontSystem_Release, &TRelease, Slot(&g_release)},
        {addr::GZFontSystem_Destroy, &TDestroy, Slot(&g_destroy)},
        {addr::GZFontSystem_Shutdown, &TShutdown, Slot(&g_shutdown)},
        {addr::GZFontSystem_SetEncoding, &TSetEncoding, Slot(&g_setEncoding)},
        {addr::GZFontSystem_SetBlitter, &TSetBlitter, Slot(&g_setBlitter)},
        {addr::GZFontSystem_Version, &TVersion, Slot(&g_version)},
        {addr::GZFontSystem_QueryInterface2, &TQueryInterface2, Slot(&g_queryInterface2)},
        {addr::GZFontSystem_Release2, &TRelease2, Slot(&g_release2)},
        {addr::GZFontSystem_Shutdown2, &TShutdown2, Slot(&g_shutdown2)},
    };
    return InstallAll(mode, kObject, "object", detours);
}

bool InstallFaces(registry::Mode mode)
{
    const Detour detours[] = {
        {addr::GZFontSystem_FileType, &TFileType, Slot(&g_fileType)},
        {addr::GZFontSystem_AddFontFile, &TAddFontFile, Slot(&g_addFontFile)},
        {addr::GZFontSystem_AddFontFolder, &TAddFontFolder, Slot(&g_addFontFolder)},
        {addr::GZFontSystem_AddFont, &TAddFont, Slot(&g_addFont)},
        {addr::GZFontSystem_RemoveFace, &TRemoveFace, Slot(&g_removeFace)},
        {addr::GZFontSystem_ReleaseFont, &TReleaseFont, Slot(&g_releaseFont)},
        {addr::GZFontSystem_ListFaces, &TListFaces, Slot(&g_listFaces)},
        {addr::GZFontSystem_ListFonts, &TListFonts, Slot(&g_listFonts)},
        {addr::GZFontSystem_HasFace, &THasFace, Slot(&g_hasFace)},
    };
    return InstallAll(mode, kFaces, "faces", detours);
}

bool InstallAliases(registry::Mode mode)
{
    const Detour detours[] = {
        {addr::GZFontSystem_Init, &TInit, Slot(&g_init)},
        {addr::GZFontSystem_Init2, &TInit2, Slot(&g_init2)},
        {addr::GZFontSystem_SetFamily, &TSetFamily, Slot(&g_setFamily)},
        {addr::GZFontSystem_AddAlias, &TAddAlias, Slot(&g_addAlias)},
        {addr::GZFontSystem_GetAliasFaces, &TGetAliasFaces, Slot(&g_getAliasFaces)},
        {addr::GZFontSystem_ResolveAlias, &TResolveAlias, Slot(&g_resolveAlias)},
    };
    return InstallAll(mode, kAliases, "aliases", detours);
}

bool InstallGetFont(registry::Mode mode)
{
    const Detour detours[] = {
        {addr::GZFontSystem_GetFont, &TGetFont, Slot(&g_getFont)},
        {addr::GZFontSystem_MakeFont, &TMakeFont, Slot(&g_makeFont)},
        {addr::GZFontSystem_GetFontFor, &TGetFontFor, Slot(&g_getFontFor)},
        {addr::GZFontSystem_SetMonoRange, &TSetMonoRange, Slot(&g_setMonoRange)},
    };
    return InstallAll(mode, kGetFont, "getfont", detours);
}

const bool g_registered[] = {
    registry::Register({"framework.font.system.object", "framework/font", registry::Kind::Detour,
                        registry::Phase::Attach, addr::GZFontSystem_Construct,
                        "Our font system's object: constructor, destructors, QueryInterface, Release, Shutdown, "
                        "SetEncoding, SetBlitter, the version and the +4 thunks (13 detours; docs/fonts.md)",
                        true, InstallObject}),
    registry::Register({"framework.font.system.faces", "framework/font", registry::Kind::Detour,
                        registry::Phase::Attach, addr::GZFontSystem_AddFontFile,
                        "Our font system's faces: a file's type, AddFontFile, AddFontFolder, AddFont, RemoveFace, "
                        "ReleaseFont, ListFaces, ListFonts, HasFace (9 detours; docs/fonts.md)",
                        true, InstallFaces}),
    registry::Register({"framework.font.system.aliases", "framework/font", registry::Kind::Detour,
                        registry::Phase::Attach, addr::GZFontSystem_Init,
                        "Our font system's aliases: Init and its thunk, the families' default faces, AddAlias, "
                        "GetAliasFaces, ResolveAlias (6 detours; docs/fonts.md)",
                        true, InstallAliases}),
    registry::Register({"framework.font.system.getfont", "framework/font", registry::Kind::Detour,
                        registry::Phase::Attach, addr::GZFontSystem_GetFont,
                        "Our font system's fonts: GetFont, MakeFont (slot 10), GetFontFor (slot 9), SetMonoRange "
                        "(4 detours; docs/fonts.md)",
                        true, InstallGetFont}),
};

} // namespace
} // namespace opensc4::fontsystem
