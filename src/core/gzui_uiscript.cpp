// The GZ framework's UI script loader 0xFA3562FB and its director (docs/gzui.md,
// part u9), on the game's own objects. The loader is the framework's system
// service 0x5A356E15: it makes the UI file parser (part u4) once, defines
// the script's words for it (`_null` to `_val`, `none` to `val`, `LEGACY`,
// `comic9`, `comic10`) and listens to it while a UI script is read: every
// tag's class gives a reader (interface 0x1A356083) that makes a window
// from the tag's attributes, `children` and `/children` move down and up a
// stack of parents, `define` sets a parser variable, and the windows end
// up in a root holder (class 0x5A354595) that the parse answers. The game
// loads every UI file with it. Replacements, each off by default:
//
//   framework.uiscript.object   the director's start, its constructor,
//                               deleting destructor and getter, its four
//                               factories (the loader's constructor is in
//                               its factory's); the loader's QueryInterface,
//                               Init, Shutdown, destructor and deleting
//                               destructor, its slots 3, 4 (a window loaded
//                               by its resource key) and 10, slot 4 of the
//                               interface at +4
//   framework.uiscript.parse    the parser made and its words defined, the
//                               three ways in (a string, a file, a
//                               resource), the parser's getter, the parent
//                               set (slot 3 at +4)
//   framework.uiscript.events   the parser's events: the document's start
//                               and end, a tag's start and the end of its
//                               start tag, the attribute list; the stack of
//                               parents (its top, push, pop)
//
// Still the game's: the 19 adjustors 0x0094B4A2 to 0x0094B532, the base
// service's AddRef and Release adjustors 0x0094B18D and 0x0094B195, the
// director's ID getter 0x0094AF19, the list node 0x0094B33A (shared), the
// shared slots 0x009D41AD, 0x0092B33A and 0x0092973B and the STLport list
// code. Ours call each other directly where the game does; a virtual call
// stays one.
//
// The game's methods answer a byte (AL) over whatever its last call or
// value left in EAX; ours give the same EAX where it comes from a call or a
// value and 0 above AL where the game leaves its caller's EAX.
//
// Every detour is entered through a thunk that keeps ECX and EDX. The
// groups go in at Attach: the game makes the loader while it starts.
#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/thunk.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>

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
inline R CallCdecl(std::uintptr_t address, Args... args)
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

inline uint32_t Bits(const void* pointer) { return static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(pointer)); }

// A byte answer over what the game leaves in the rest of EAX.
inline uint32_t Answer(uint32_t eax, bool value) { return (eax & ~0xFFu) | (value ? 1u : 0u); }
inline bool Yes(uint32_t eax) { return (eax & 0xFF) != 0; }

inline uint32_t AddRef(void* object) { return Virtual<uint32_t>(object, 0x4); }
inline uint32_t Release(void* object) { return Virtual<uint32_t>(object, 0x8); }

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("gzui: uiscript: first %s", name);
}

#define OPENSC4_FIRST(name)                                                                                    \
    do {                                                                                                       \
        static bool logged_ = false;                                                                           \
        if (!logged_)                                                                                          \
            FirstCall(name, logged_);                                                                          \
    } while (0)

void* Com() { return CallCdecl<void*>(addr::RZGetCOM); }

// The COM's GetClassObject (slot 3): AL whether it made one.
uint32_t GetClassObject(uint32_t clsid, uint32_t iid, void* out)
{
    return Virtual<uint32_t>(Com(), 0xC, clsid, iid, out);
}

// A game string on the stack (0x14 bytes), freed as the game's methods do
// it inline: the vtable back and the text's buffer freed.
struct LocalText {
    alignas(4) char bytes[0x14];
    void* get() { return bytes; }
    // The inline destructor's EAX: the free's when there was a buffer.
    void Free(uint32_t& eax)
    {
        At<std::uintptr_t>(bytes, 0) = addr::SC4String_Vtable;
        if (void* const text = At<void*>(bytes, 4))
            eax = CallCdecl<uint32_t>(addr::SC4_Free, text);
    }
};

constexpr uint32_t kCLSID_Loader = 0xFA3562FB;
constexpr uint32_t kCLSID_Root = 0x5A354595;
constexpr uint32_t kCLSID_Legacy = 0xFA450242;
constexpr uint32_t kCLSID_Parser = 0xBA2E7953;
constexpr uint32_t kCLSID_Table = 0xBA2E7954;
constexpr uint32_t kIID_Window = 0x9A4527CB;
constexpr uint32_t kIID_Reader = 0x1A356083;

// ====================================================================================
// The director (0x4C bytes; the static one at 0x00B63538).

// The game's 0x0094AF1F (slot 4, `ret 4`): the loader made through the
// director's COM (slot 12) with interface 0x287FB697 and given to the
// framework (its slot 3), then released. AL 1 over the last call's EAX.
uint32_t __fastcall DirectorStart(void* self, void*, void*)
{
    OPENSC4_FIRST("DirectorStart");
    void* const com = Virtual<void*>(self, 0x30);
    void* loader = nullptr;
    uint32_t eax = Virtual<uint32_t>(com, 0xC, kCLSID_Loader, 0x287FB697u, &loader);
    if (loader) {
        void* const framework = CallCdecl<void*>(addr::RZGetFramework);
        Virtual<uint32_t>(framework, 0xC, loader);
        eax = Release(loader);
    }
    return Answer(eax, true);
}

// The game's 0x0094B400 (the factory's tail jump; not hooked on its own).
char* LoaderConstruct(char* self);

// The game's 0x0094AF64: a loader (0x60 bytes) made.
char* __fastcall DirectorCreateLoader(void*, void*)
{
    OPENSC4_FIRST("DirectorCreateLoader");
    char* const memory = CallCdecl<char*>(addr::SC4_operator_new, 0x60u);
    return memory ? LoaderConstruct(memory) : nullptr;
}

// The game's 0x0094AF7A: a root holder 0x5A354595 (0x24 bytes) made by the
// game's constructor 0x0094C0AF.
void* __fastcall DirectorCreateRoot(void*, void*)
{
    OPENSC4_FIRST("DirectorCreateRoot");
    void* const memory = CallCdecl<void*>(addr::SC4_operator_new, 0x24u);
    return memory ? CallThis<void*>(addr::cGZUIScriptRoot_Construct, memory) : nullptr;
}

// The game's 0x0094AF90: the LEGACY reader 0xFA450242 (0x20 bytes) made by
// the game's constructor 0x0095B8C4.
void* __fastcall DirectorCreateLegacy(void*, void*)
{
    OPENSC4_FIRST("DirectorCreateLegacy");
    void* const memory = CallCdecl<void*>(addr::SC4_operator_new, 0x20u);
    return memory ? CallThis<void*>(addr::cGZUIScriptLegacy_Construct, memory) : nullptr;
}

// The game's 0x0094AFA6: the class 0x478D1E6F (0xDC bytes, the game's
// constructor 0x0095BAB3) made; its interface at +0xD8 answered (0 for
// none).
char* __fastcall DirectorCreate478D1E6F(void*, void*)
{
    OPENSC4_FIRST("DirectorCreate478D1E6F");
    void* const memory = CallCdecl<void*>(addr::SC4_operator_new, 0xDCu);
    char* const object = memory ? CallThis<char*>(addr::cGZUIScript478D1E6F_Construct, memory) : nullptr;
    return object ? object + 0xD8 : nullptr;
}

// The game's 0x0094AFCD (slot 15, `ret 4`).
void* __fastcall DirectorDeletingDestructor(void* self, void*, uint32_t flags)
{
    OPENSC4_FIRST("DirectorDeletingDestructor");
    At<std::uintptr_t>(self, 0) = addr::cGZUIScriptDirector_Vtable;
    At<std::uintptr_t>(self, 4) = addr::cGZUIScriptDirector_UnknownVtable;
    CallThis<void>(addr::cRZCOMDllDirector_Destruct, self);
    if (flags & 1)
        CallCdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// The game's 0x0094AFF6: the four classes registered by their factories
// (the game's addresses, so a hooked factory stays ours).
void* __fastcall DirectorConstruct(void* self, void*)
{
    OPENSC4_FIRST("DirectorConstruct");
    CallThis<void*>(addr::cRZCOMDllDirector_Construct, self);
    At<std::uintptr_t>(self, 0) = addr::cGZUIScriptDirector_Vtable;
    At<std::uintptr_t>(self, 4) = addr::cGZUIScriptDirector_UnknownVtable;
    CallThis<void>(addr::cRZCOMDllDirector_AddCls, self, kCLSID_Loader, addr::cGZUIScriptDirector_CreateLoader);
    CallThis<void>(addr::cRZCOMDllDirector_AddCls, self, kCLSID_Root, addr::cGZUIScriptDirector_CreateRoot);
    CallThis<void>(addr::cRZCOMDllDirector_AddCls, self, kCLSID_Legacy, addr::cGZUIScriptDirector_CreateLegacy);
    CallThis<void>(addr::cRZCOMDllDirector_AddCls, self, 0x478D1E6Fu, addr::cGZUIScriptDirector_Create478D1E6F);
    return self;
}

// The game's 0x0094B053: the static director, made on first use (its
// destructor 0x00A7F650 given to atexit).
void* __fastcall DirectorGet(void*, void*)
{
    OPENSC4_FIRST("DirectorGet");
    uint32_t& guard = *reinterpret_cast<uint32_t*>(addr::cGZUIScriptDirector_Guard);
    void* const director = reinterpret_cast<void*>(addr::cGZUIScriptDirector_Static);
    if (!(guard & 1)) {
        guard |= 1;
        DirectorConstruct(director, nullptr);
        CallCdecl<int>(addr::SC4_atexit, addr::cGZUIScriptDirector_StaticDestroy);
    }
    return director;
}

// ====================================================================================
// The loader (0x60 bytes). Seven vtables: the primary one (11 slots), the
// interface 0x7A4A48ED at +4, the parser's listener 0x1A2BDEB8 at +8, its
// tag events 0x1A2BDEB9 at +0xC and attribute events 0x1A2BDEBA at +0x10,
// and the base system service at +0x14 (its own second vtable at +0x18).
// The stack of parents is an STLport list (the header node's pointer at
// +0x40; a node's next at +0, its value at +8).

constexpr std::size_t kPart = 0x4;
constexpr std::size_t kListener = 0x8;
constexpr std::size_t kTags = 0xC;
constexpr std::size_t kAttributes = 0x10;
constexpr std::size_t kService = 0x14;
constexpr std::size_t kInited = 0x30;    // byte: the service's Init ran
constexpr std::size_t kParser = 0x34;    // the UI file parser (counted)
constexpr std::size_t kTable = 0x38;     // the variable table pushed at the start
constexpr std::size_t kDefining = 0x3C;  // byte: set while the words are defined
constexpr std::size_t kStack = 0x40;     // list of parents (counted)
constexpr std::size_t kCurrent = 0x44;   // the tag's reader, not counted
constexpr std::size_t kTagOpen = 0x48;   // byte: the reader's table is pushed
constexpr std::size_t kTag = 0x4C;       // the tag's number
constexpr std::size_t kRoot = 0x50;      // the root holder (counted)
constexpr std::size_t kReader = 0x54;    // the tag's reader (counted)
constexpr std::size_t kContext = 0x58;   // the readers' slot 6 (counted)
constexpr std::size_t kHeld = 0x5C;      // slot 10's object (counted)

char* LoaderConstruct(char* self)
{
    OPENSC4_FIRST("LoaderConstruct");
    At<std::uintptr_t>(self, 0) = addr::cGZUIScriptLoader_BaseVtable;
    At<std::uintptr_t>(self, kPart) = addr::cGZUIScriptLoader_PartBaseVtable;
    At<std::uintptr_t>(self, kListener) = addr::cGZUIScriptLoader_ListenerBaseVtable;
    At<std::uintptr_t>(self, kTags) = addr::cGZUIScriptLoader_TagsBaseVtable;
    At<std::uintptr_t>(self, kAttributes) = addr::cGZUIScriptLoader_AttributesBaseVtable;
    CallThis<void*>(addr::cRZBaseSystemService_cRZBaseSystemService, self + kService, 0x5A356E15u,
                    static_cast<int32_t>(0xFFD23940u));
    At<std::uintptr_t>(self, 0) = addr::cGZUIScriptLoader_Vtable;
    At<std::uintptr_t>(self, kPart) = addr::cGZUIScriptLoader_PartVtable;
    At<std::uintptr_t>(self, kListener) = addr::cGZUIScriptLoader_ListenerVtable;
    At<std::uintptr_t>(self, kTags) = addr::cGZUIScriptLoader_TagsVtable;
    At<std::uintptr_t>(self, kAttributes) = addr::cGZUIScriptLoader_AttributesVtable;
    At<std::uintptr_t>(self, kService) = addr::cGZUIScriptLoader_ServiceVtable;
    At<std::uintptr_t>(self, kService + 4) = addr::cGZUIScriptLoader_ServiceUnknownVtable;
    At<uint8_t>(self, kInited) = 0;
    At<uint32_t>(self, kParser) = 0;
    At<uint32_t>(self, kTable) = 0;
    At<uint8_t>(self, kDefining) = 0;
    uint8_t allocator;
    CallThis<void>(addr::cGZDBSegmentPackedFile_ListConstruct, self + kStack, &allocator);
    At<uint32_t>(self, kCurrent) = 0;
    At<uint8_t>(self, kTagOpen) = 0;
    At<uint32_t>(self, kTag) = 0;
    At<uint32_t>(self, kRoot) = 0;
    At<uint32_t>(self, kReader) = 0;
    At<uint32_t>(self, kContext) = 0;
    At<uint32_t>(self, kHeld) = 0;
    return self;
}

// The game's 0x0094B354: the stack's top (0 when empty).
void* __fastcall LoaderTop(char* self, void*)
{
    OPENSC4_FIRST("LoaderTop");
    char* const header = At<char*>(self, kStack);
    char* const first = At<char*>(header, 0);
    return first != header ? At<void*>(first, 8) : nullptr;
}

// The game's 0x0094B53A: the top released and popped; AL whether there was
// one (over the pop's EAX, or the header's).
uint32_t __fastcall LoaderPop(char* self, void*)
{
    OPENSC4_FIRST("LoaderPop");
    char* const header = At<char*>(self, kStack);
    char* const first = At<char*>(header, 0);
    if (first == header)
        return Answer(Bits(header), false);
    Release(At<void*>(first, 8));
    return Answer(CallThis<uint32_t>(addr::SC4PtrList_PopFront, self + kStack), true);
}

// The game's 0x0094B653: the tag's reader (+0x44) held and pushed, then
// cleared. AL 1 over the push's EAX (the caller's without one; ours 0).
uint32_t __fastcall LoaderPush(char* self, void*)
{
    OPENSC4_FIRST("LoaderPush");
    uint32_t eax = 0;
    if (void* const current = At<void*>(self, kCurrent)) {
        AddRef(current);
        eax = CallThis<uint32_t>(addr::SC4PtrList_PushFront, self + kStack, self + kCurrent);
        At<void*>(self, kCurrent) = nullptr;
    }
    return Answer(eax, true);
}

// The game's 0x0094B73A: the parser made through the COM once, its words
// defined (slot 9; the listener given in between with the flag +0x3C set,
// so the document's start does not push a table). EAX whether there is a
// parser.
uint32_t __fastcall LoaderMakeParser(char* self, void*)
{
    OPENSC4_FIRST("LoaderMakeParser");
    if (!At<void*>(self, kParser) && Yes(GetClassObject(kCLSID_Parser, 0x5A2C1570u, self + kParser))) {
        auto define = [self](const char* name, uint32_t value) {
            LocalText text;
            CallThis<void*>(addr::SC4String_FromText, text.get(), name);
            Virtual<uint32_t>(At<void*>(self, kParser), 0x24, text.get(), value);
            uint32_t eax = 0;
            text.Free(eax);
        };
        define("_null", 0);
        define("_children", 1);
        define("_/children", 2);
        define("_define", 3);
        define("_name", 4);
        define("_val", 5);
        At<uint8_t>(self, kDefining) = 1;
        Virtual<uint32_t>(At<void*>(self, kParser), 0xC, self + kListener);
        define("none", 0);
        define("children", 1);
        define("/children", 2);
        define("define", 3);
        define("name", 4);
        define("val", 5);
        define("LEGACY", kCLSID_Legacy);
        define("comic9", 0x12);
        define("comic10", 0x13);
        At<uint8_t>(self, kDefining) = 0;
    }
    return At<void*>(self, kParser) ? 1u : 0u;
}

// The game's 0x0094BA22, 0x0094BA51 and 0x0094BA80 (slots 5 to 7, `ret
// 4`): with a parser, the listener given and the parser's slot 6, 7 or 8
// (a game string, a file, a resource key); the root holder answered and
// cleared (the caller's reference).
void* LoaderParse(char* self, std::size_t slot, void* source)
{
    if (Yes(LoaderMakeParser(self, nullptr))) {
        Virtual<uint32_t>(At<void*>(self, kParser), 0xC, self + kListener);
        Virtual<uint32_t>(At<void*>(self, kParser), slot, source);
    }
    void* const root = At<void*>(self, kRoot);
    At<void*>(self, kRoot) = nullptr;
    return root;
}

void* __fastcall LoaderParseString(char* self, void*, void* text)
{
    OPENSC4_FIRST("LoaderParseString");
    return LoaderParse(self, 0x18, text);
}

void* __fastcall LoaderParseFile(char* self, void*, void* path)
{
    OPENSC4_FIRST("LoaderParseFile");
    return LoaderParse(self, 0x1C, path);
}

void* __fastcall LoaderParseResource(char* self, void*, void* key)
{
    OPENSC4_FIRST("LoaderParseResource");
    return LoaderParse(self, 0x20, key);
}

// The game's 0x0094BAAF (slot 8): the parser, made first.
void* __fastcall LoaderParser(char* self, void*)
{
    OPENSC4_FIRST("LoaderParser");
    LoaderMakeParser(self, nullptr);
    return At<void*>(self, kParser);
}

// The game's 0x0094B120 (slot 0, also through the adjustors at +4 to
// +0x18; `ret 8`): 1 and 0xFA3562FA give the loader, 0x1A2BDEB8 its
// listener (+8), 0x1A2BDEB9 its tag events (+0xC), 0x1A2BDEBA its
// attribute events (+0x10), 0x7A4A48ED its part at +4 (0 for a null
// loader), AddRef through the primary vtable, AL 1 over its EAX; anything
// else to the base service's QueryInterface (its EAX).
uint32_t __fastcall LoaderQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    OPENSC4_FIRST("LoaderQueryInterface");
    std::size_t offset;
    switch (iid) {
    case 1:
    case 0xFA3562FA:
        *out = self;
        return Answer(AddRef(self), true);
    case 0x1A2BDEB8:
        offset = kListener;
        break;
    case 0x1A2BDEB9:
        offset = kTags;
        break;
    case 0x1A2BDEBA:
        offset = kAttributes;
        break;
    case 0x7A4A48ED:
        offset = kPart;
        break;
    default:
        return CallThis<uint32_t>(addr::cRZBaseSystemService_QueryInterface, self + kService, iid, out);
    }
    *out = self ? self + offset : nullptr;
    return Answer(AddRef(self), true);
}

// The game's 0x0094B08B (slot 3, `ret 0x14`): slot 4's window asked for an
// interface (its slot 0) and released. AL whether it gave one, over the
// release's EAX (0 without a window).
uint32_t __fastcall LoaderCreate(char* self, void*, const uint32_t* key, void* parent, uint32_t id, uint32_t iid,
                                 void** out)
{
    OPENSC4_FIRST("LoaderCreate");
    void* const window = Virtual<void*>(self, 0x10, key, parent, id);
    if (!window)
        return 0;
    const bool given = Yes(Virtual<uint32_t>(window, 0x0, iid, out));
    return Answer(Release(window), given);
}

// The game's 0x0094B210 (slot 4, `ret 0xC`): a window by its UI script's
// resource key. The key's group is the language's (the locale service's
// slots 41 and 42 written `0x%.4u%.4u`) when the resource manager has that
// resource (its slot 14). The parent set (slot 3 at +4), the script parsed
// (slot 7); the root holder's window under the id (its slot 5) asked for
// 0x9A4527CB, whose slot 3 is held and answered; the holder's slot 9 with 0
// and its release. 0 for none.
void* __fastcall LoaderLoad(char* self, void*, const uint32_t* key, void* parent, uint32_t id)
{
    OPENSC4_FIRST("LoaderLoad");
    uint32_t copy[3] = {key[0], key[1], key[2]};
    void* manager = nullptr;
    CallThis<void*>(addr::SC4RefPtr_ResourceManager, &manager);
    if (manager) {
        void* locale = nullptr;
        CallThis<void*>(addr::cSC4Advisors_F4177C0, &locale);
        void* const language = locale ? Virtual<void*>(locale, 0xC) : nullptr;
        if (language) {
            const uint32_t primary = Virtual<uint32_t>(language, 0xA4);
            const uint32_t secondary = Virtual<uint32_t>(language, 0xA8);
            char text[0x30];
            CallCdecl<int>(addr::SC4_sprintf, text, "0x%.4u%.4u", primary, secondary);
            copy[1] = static_cast<uint32_t>(CallCdecl<int32_t>(addr::SC4_TextToInt, text));
            if (!Yes(Virtual<uint32_t>(manager, 0x38, copy)))
                copy[1] = key[1];
        }
        CallThis<void>(addr::SC4RefPtr_Release, &locale);
    }
    Virtual<uint32_t>(self + kPart, 0xC, parent);
    void* answer = nullptr;
    if (void* const root = Virtual<void*>(self, 0x1C, copy)) {
        if (void* const window = Virtual<void*>(root, 0x14, id)) {
            void* found = nullptr;
            if (Yes(Virtual<uint32_t>(window, 0x0, kIID_Window, &found))) {
                answer = Virtual<void*>(found, 0xC);
                if (answer)
                    AddRef(answer);
                Release(found);
            }
        }
        Virtual<uint32_t>(root, 0x24, 0u);
        Release(root);
    }
    CallThis<void>(addr::SC4RefPtr_Destruct, &manager);
    return answer;
}

// The game's 0x0094B0C7 (slot 10, `ret 4`): +0x5C held (the new one's
// AddRef first). The EAX of the last call (the caller's; ours 0).
uint32_t __fastcall LoaderSetHeld(char* self, void*, void* object)
{
    OPENSC4_FIRST("LoaderSetHeld");
    uint32_t eax = 0;
    if (object == At<void*>(self, kHeld))
        return eax;
    if (object)
        eax = AddRef(object);
    if (void* const old = At<void*>(self, kHeld))
        eax = Release(old);
    At<void*>(self, kHeld) = object;
    return eax;
}

// The game's 0x0094B0F3 (slot 4 of the part at +4, `ret 4`): the readers'
// context +0x58 held (the old one's Release first); AL 1.
uint32_t __fastcall LoaderSetContext(char* part, void*, void* object)
{
    OPENSC4_FIRST("LoaderSetContext");
    char* const self = part - kPart;
    uint32_t eax = 0;
    void* const old = At<void*>(self, kContext);
    if (object != old) {
        if (old)
            eax = Release(old);
        At<void*>(self, kContext) = object;
        if (object)
            eax = AddRef(object);
    }
    return Answer(eax, true);
}

// The game's 0x0094B676 (slot 3 of the part at +4, `ret 4`): a LEGACY
// reader made through the COM, its 0x9A4527CB interface given the parent
// (slot 5), and pushed on the stack as the first parent. AL 1 over the
// COM's EAX, or the reader's release.
uint32_t __fastcall LoaderSetParent(char* part, void*, void* parent)
{
    OPENSC4_FIRST("LoaderSetParent");
    char* const self = part - kPart;
    void* reader = nullptr;
    uint32_t eax = GetClassObject(kCLSID_Legacy, kIID_Reader, &reader);
    if (Yes(eax)) {
        void* window = nullptr;
        if (Yes(Virtual<uint32_t>(reader, 0x0, kIID_Window, &window))) {
            Virtual<uint32_t>(window, 0x14, parent);
            Release(window);
        }
        At<void*>(self, kCurrent) = reader;
        LoaderPush(self, nullptr);
        eax = Release(reader);
    }
    return Answer(eax, true);
}

// The game's 0x0094B07E (the service's slot 8): Init, +0x30 set; AL 1 (over
// the caller's EAX; ours 0).
uint32_t __fastcall LoaderInit(char* service, void*)
{
    OPENSC4_FIRST("LoaderInit");
    char* const self = service - kService;
    if (!At<uint8_t>(self, kInited))
        At<uint8_t>(self, kInited) = 1;
    return 1;
}

// The game's 0x0094B5F4 (the service's slot 9): Shutdown after an Init: the
// parser, the root holder, the reader and +0x5C released, the stack
// emptied, +0x30 cleared. AL 1 over the last call's EAX.
uint32_t __fastcall LoaderShutdown(char* service, void*)
{
    OPENSC4_FIRST("LoaderShutdown");
    char* const self = service - kService;
    uint32_t eax = 0;
    if (!At<uint8_t>(self, kInited))
        return Answer(eax, true);
    for (const std::size_t offset : {kParser, kRoot, kReader, kHeld}) {
        if (void* const object = At<void*>(self, offset)) {
            eax = Release(object);
            At<void*>(self, offset) = nullptr;
        }
    }
    do
        eax = LoaderPop(self, nullptr);
    while (Yes(eax));
    At<uint8_t>(self, kInited) = 0;
    return Answer(eax, true);
}

// The game's 0x0094B6EC: the vtables back, Shutdown, the stack destroyed,
// then the base service's destructor (a tail jump: its EAX).
uint32_t __fastcall LoaderDestruct(char* self, void*)
{
    OPENSC4_FIRST("LoaderDestruct");
    At<std::uintptr_t>(self, 0) = addr::cGZUIScriptLoader_Vtable;
    At<std::uintptr_t>(self, kPart) = addr::cGZUIScriptLoader_PartVtable;
    At<std::uintptr_t>(self, kListener) = addr::cGZUIScriptLoader_ListenerVtable;
    At<std::uintptr_t>(self, kTags) = addr::cGZUIScriptLoader_TagsVtable;
    At<std::uintptr_t>(self, kAttributes) = addr::cGZUIScriptLoader_AttributesVtable;
    At<std::uintptr_t>(self, kService) = addr::cGZUIScriptLoader_ServiceVtable;
    At<std::uintptr_t>(self, kService + 4) = addr::cGZUIScriptLoader_ServiceUnknownVtable;
    LoaderShutdown(self + kService, nullptr);
    CallThis<void>(addr::cGZDBSegmentPackedFile_ListDestruct, self + kStack);
    return CallThis<uint32_t>(addr::cRZBaseSystemService_Destroy, self + kService);
}

// The game's 0x0094BBE2 (through the adjustor 0x0094B532, `ret 4`).
char* __fastcall LoaderDeletingDestructor(char* self, void*, uint32_t flags)
{
    OPENSC4_FIRST("LoaderDeletingDestructor");
    LoaderDestruct(self, nullptr);
    if (flags & 1)
        CallCdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// ====================================================================================
// The parser's events

// The game's 0x0094B19D (the listener's slot 3, `ret 4`): the document's
// start. Unless the words are being defined, a variable table 0xBA2E7954
// made into +0x38 (not released first) and pushed on the parser (slot 13);
// then a root holder 0x5A354595 (interface 0x1A35608A) into +0x50 (not
// released first). AL whether the COM made it, over its EAX.
uint32_t __fastcall LoaderDocumentStart(char* listener, void*, void* parser)
{
    OPENSC4_FIRST("LoaderDocumentStart");
    char* const self = listener - kListener;
    if (!At<uint8_t>(self, kDefining)) {
        if (Yes(GetClassObject(kCLSID_Table, 0x1A2BDEBBu, self + kTable)))
            Virtual<uint32_t>(parser, 0x34, At<void*>(self, kTable));
    }
    const uint32_t eax = GetClassObject(kCLSID_Root, 0x1A35608Au, self + kRoot);
    return Answer(eax, Yes(eax));
}

// The table and the stack let go at the document's end (the game's code
// after the check of +0x3C): the parser's slot 14, the table released
// (unchecked), the stack emptied, the tag's reader cleared.
uint32_t DocumentEndTail(char* self, void* parser)
{
    Virtual<uint32_t>(parser, 0x38);
    Release(At<void*>(self, kTable));
    At<void*>(self, kTable) = nullptr;
    uint32_t eax;
    do
        eax = LoaderPop(self, nullptr);
    while (Yes(eax));
    At<void*>(self, kCurrent) = nullptr;
    return eax;
}

// The game's 0x0094B55D (the listener's slot 4, `ret 8`): the document's
// end. Without the flag (it failed) the root holder and the reader are
// released and a pushed reader's table popped (slot 14). Unless the words
// are being defined, the tail above. AL 1 over the last call's EAX.
uint32_t __fastcall LoaderDocumentEnd(char* listener, void*, void* parser, uint32_t done)
{
    OPENSC4_FIRST("LoaderDocumentEnd");
    char* const self = listener - kListener;
    uint32_t eax = 0;
    if (!(done & 0xFF)) {
        for (const std::size_t offset : {kRoot, kReader}) {
            if (void* const object = At<void*>(self, offset)) {
                eax = Release(object);
                At<void*>(self, offset) = nullptr;
            }
        }
        if (At<uint8_t>(self, kTagOpen)) {
            eax = Virtual<uint32_t>(parser, 0x38);
            At<uint8_t>(self, kTagOpen) = 0;
        }
    }
    if (!At<uint8_t>(self, kDefining))
        eax = DocumentEndTail(self, parser);
    return Answer(eax, true);
}

// The game's 0x0094B364 (the tag events' slot 3, `ret 8`): a tag's start.
// The number kept (+0x4C), the last reader released, and the tag's class
// made through the COM with interface 0x1A356083 into +0x54 (AL 0 over the
// COM's EAX when it is not one). Its 0x9A4527CB interface given the context
// (slot 6), the reader given the stack's top (slot 9), kept at +0x44 (not
// counted), its table (slot 4) pushed on the parser and +0x48 set. AL 1
// over the push's EAX.
uint32_t __fastcall LoaderTagStart(char* tags, void*, void* parser, uint32_t tag)
{
    OPENSC4_FIRST("LoaderTagStart");
    char* const self = tags - kTags;
    At<uint32_t>(self, kTag) = tag;
    if (void* const old = At<void*>(self, kReader)) {
        Release(old);
        At<void*>(self, kReader) = nullptr;
    }
    const uint32_t eax = GetClassObject(tag, kIID_Reader, self + kReader);
    if (!Yes(eax))
        return Answer(eax, false);
    void* window = nullptr;
    if (Yes(Virtual<uint32_t>(At<void*>(self, kReader), 0x0, kIID_Window, &window))) {
        Virtual<uint32_t>(window, 0x18, At<void*>(self, kContext));
        Release(window);
    }
    void* const reader = At<void*>(self, kReader);
    Virtual<uint32_t>(reader, 0x24, LoaderTop(self, nullptr));
    At<void*>(self, kCurrent) = At<void*>(self, kReader);
    void* const table = Virtual<void*>(At<void*>(self, kReader), 0x10);
    const uint32_t pushed = Virtual<uint32_t>(parser, 0x34, table);
    At<uint8_t>(self, kTagOpen) = 1;
    return Answer(pushed, true);
}

// The game's 0x0094B1F4 (the tag events' slot 4, `ret 8`): the end of a
// start tag: a pushed reader's table popped (slot 14). AL 1 over its EAX
// (the caller's without; ours 0).
uint32_t __fastcall LoaderTagEnd(char* tags, void*, void* parser, uint32_t)
{
    OPENSC4_FIRST("LoaderTagEnd");
    char* const self = tags - kTags;
    uint32_t eax = 0;
    if (At<uint8_t>(self, kTagOpen)) {
        eax = Virtual<uint32_t>(parser, 0x38);
        At<uint8_t>(self, kTagOpen) = 0;
    }
    return Answer(eax, true);
}

// The game's 0x0094BABC (the attribute events' slot 3, `ret 0xC`): a tag's
// attribute list (a vector of number and value pairs). With a reader and a
// root holder: the reader reads the list (slot 3), the holder takes the
// reader (slot 4), the reader released. Else by the tag: `children` (1)
// pushes the last reader, `/children` (2) pops and makes the top the last
// reader, `define` (3) sets the parser variable named by `name` (4, a text
// value: type 0x800E, its text by slot 55) to `val` (5) (slots 11 and 10;
// without `val` only removed); AL 0 (over the tag less 3) for any other.
uint32_t __fastcall LoaderAttributes(char* attributes, void*, void* parser, uint32_t tag, char* const* list)
{
    OPENSC4_FIRST("LoaderAttributes");
    char* const self = attributes - kAttributes;
    if (At<void*>(self, kReader) && At<void*>(self, kRoot)) {
        Virtual<uint32_t>(At<void*>(self, kReader), 0xC, list);
        Virtual<uint32_t>(At<void*>(self, kRoot), 0x10, At<void*>(self, kReader));
        const uint32_t eax = Release(At<void*>(self, kReader));
        At<void*>(self, kReader) = nullptr;
        return Answer(eax, true);
    }
    switch (tag) {
    case 1: {
        uint32_t eax = 0;
        if (At<void*>(self, kCurrent)) {
            eax = LoaderPush(self, nullptr);
            At<void*>(self, kCurrent) = nullptr;
        }
        return Answer(eax, true);
    }
    case 2: {
        LoaderPop(self, nullptr);
        void* const top = LoaderTop(self, nullptr);
        At<void*>(self, kCurrent) = top;
        return Answer(Bits(top), true);
    }
    case 3:
        break;
    default:
        return Answer(tag - 3, false);
    }
    const uint32_t count = static_cast<uint32_t>(list[1] - list[0]) >> 3;
    if (count == 0)
        return 1;
    void* name = nullptr;
    void* value = nullptr;
    for (uint32_t i = 0; i < count; ++i) {
        const char* const pair = list[0] + 8 * i;
        const uint32_t number = At<uint32_t>(const_cast<char*>(pair), 0);
        if (number == 4)
            name = At<void*>(const_cast<char*>(pair), 4);
        else if (number == 5)
            value = At<void*>(const_cast<char*>(pair), 4);
    }
    if (!name)
        return 1;
    const uint32_t type = Virtual<uint32_t>(name, 0xC);
    if (type != 0x800E)
        return Answer(type, true);
    LocalText text;
    CallThis<void*>(addr::SC4String_SC4String, text.get());
    uint32_t eax = Virtual<uint32_t>(name, 0xDC, text.get());
    if (value) {
        Virtual<uint32_t>(parser, 0x2C, text.get());
        eax = Virtual<uint32_t>(parser, 0x28, text.get(), value);
    } else {
        eax = Virtual<uint32_t>(parser, 0x2C, text.get());
    }
    text.Free(eax);
    return Answer(eax, true);
}

// ====================================================================================
// Thunks and installation

OPENSC4_THUNK(DirectorStartThunk, DirectorStart, 4)
OPENSC4_THUNK(DirectorCreateLoaderThunk, DirectorCreateLoader, 0)
OPENSC4_THUNK(DirectorCreateRootThunk, DirectorCreateRoot, 0)
OPENSC4_THUNK(DirectorCreateLegacyThunk, DirectorCreateLegacy, 0)
OPENSC4_THUNK(DirectorCreate478D1E6FThunk, DirectorCreate478D1E6F, 0)
OPENSC4_THUNK(DirectorDeletingDestructorThunk, DirectorDeletingDestructor, 4)
OPENSC4_THUNK(DirectorConstructThunk, DirectorConstruct, 0)
OPENSC4_THUNK(DirectorGetThunk, DirectorGet, 0)
OPENSC4_THUNK(LoaderInitThunk, LoaderInit, 0)
OPENSC4_THUNK(LoaderCreateThunk, LoaderCreate, 20)
OPENSC4_THUNK(LoaderSetHeldThunk, LoaderSetHeld, 4)
OPENSC4_THUNK(LoaderSetContextThunk, LoaderSetContext, 4)
OPENSC4_THUNK(LoaderQueryInterfaceThunk, LoaderQueryInterface, 8)
OPENSC4_THUNK(LoaderDocumentStartThunk, LoaderDocumentStart, 4)
OPENSC4_THUNK(LoaderTagEndThunk, LoaderTagEnd, 8)
OPENSC4_THUNK(LoaderLoadThunk, LoaderLoad, 12)
OPENSC4_THUNK(LoaderTopThunk, LoaderTop, 0)
OPENSC4_THUNK(LoaderTagStartThunk, LoaderTagStart, 8)
OPENSC4_THUNK(LoaderPopThunk, LoaderPop, 0)
OPENSC4_THUNK(LoaderDocumentEndThunk, LoaderDocumentEnd, 8)
OPENSC4_THUNK(LoaderShutdownThunk, LoaderShutdown, 0)
OPENSC4_THUNK(LoaderPushThunk, LoaderPush, 0)
OPENSC4_THUNK(LoaderSetParentThunk, LoaderSetParent, 4)
OPENSC4_THUNK(LoaderDestructThunk, LoaderDestruct, 0)
OPENSC4_THUNK(LoaderMakeParserThunk, LoaderMakeParser, 0)
OPENSC4_THUNK(LoaderParseStringThunk, LoaderParseString, 4)
OPENSC4_THUNK(LoaderParseFileThunk, LoaderParseFile, 4)
OPENSC4_THUNK(LoaderParseResourceThunk, LoaderParseResource, 4)
OPENSC4_THUNK(LoaderParserThunk, LoaderParser, 0)
OPENSC4_THUNK(LoaderAttributesThunk, LoaderAttributes, 12)
OPENSC4_THUNK(LoaderDeletingDestructorThunk, LoaderDeletingDestructor, 4)

struct Detour {
    std::uintptr_t address;
    void (*ours)();
};

template <std::size_t N>
bool InstallAll(const Detour (&detours)[N])
{
    if (!game::IsBuild641())
        return false;
    for (const Detour& detour : detours) {
        const addr::Entry* entry = addr_check::Find(detour.address);
        void* original = nullptr;
        if (!entry || !hook::Install(*entry, reinterpret_cast<void*>(detour.ours), &original))
            return false;
    }
    return true;
}

bool InstallObject(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZUIScriptDirector_Start, &DirectorStartThunk},
        {addr::cGZUIScriptDirector_CreateLoader, &DirectorCreateLoaderThunk},
        {addr::cGZUIScriptDirector_CreateRoot, &DirectorCreateRootThunk},
        {addr::cGZUIScriptDirector_CreateLegacy, &DirectorCreateLegacyThunk},
        {addr::cGZUIScriptDirector_Create478D1E6F, &DirectorCreate478D1E6FThunk},
        {addr::cGZUIScriptDirector_DeletingDestructor, &DirectorDeletingDestructorThunk},
        {addr::cGZUIScriptDirector_Construct, &DirectorConstructThunk},
        {addr::cGZUIScriptDirector_Get, &DirectorGetThunk},
        {addr::cGZUIScriptLoader_QueryInterface, &LoaderQueryInterfaceThunk},
        {addr::cGZUIScriptLoader_Init, &LoaderInitThunk},
        {addr::cGZUIScriptLoader_Shutdown, &LoaderShutdownThunk},
        {addr::cGZUIScriptLoader_Destruct, &LoaderDestructThunk},
        {addr::cGZUIScriptLoader_DeletingDestructor, &LoaderDeletingDestructorThunk},
        {addr::cGZUIScriptLoader_Create, &LoaderCreateThunk},
        {addr::cGZUIScriptLoader_Load, &LoaderLoadThunk},
        {addr::cGZUIScriptLoader_SetHeld, &LoaderSetHeldThunk},
        {addr::cGZUIScriptLoader_SetContext, &LoaderSetContextThunk},
    };
    return InstallAll(detours);
}

bool InstallParse(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZUIScriptLoader_MakeParser, &LoaderMakeParserThunk},
        {addr::cGZUIScriptLoader_ParseString, &LoaderParseStringThunk},
        {addr::cGZUIScriptLoader_ParseFile, &LoaderParseFileThunk},
        {addr::cGZUIScriptLoader_ParseResource, &LoaderParseResourceThunk},
        {addr::cGZUIScriptLoader_Parser, &LoaderParserThunk},
        {addr::cGZUIScriptLoader_SetParent, &LoaderSetParentThunk},
    };
    return InstallAll(detours);
}

bool InstallEvents(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZUIScriptLoader_DocumentStart, &LoaderDocumentStartThunk},
        {addr::cGZUIScriptLoader_DocumentEnd, &LoaderDocumentEndThunk},
        {addr::cGZUIScriptLoader_TagStart, &LoaderTagStartThunk},
        {addr::cGZUIScriptLoader_TagEnd, &LoaderTagEndThunk},
        {addr::cGZUIScriptLoader_Attributes, &LoaderAttributesThunk},
        {addr::cGZUIScriptLoader_Top, &LoaderTopThunk},
        {addr::cGZUIScriptLoader_Push, &LoaderPushThunk},
        {addr::cGZUIScriptLoader_Pop, &LoaderPopThunk},
    };
    return InstallAll(detours);
}

const bool g_registered[] = {
    registry::Register({"framework.uiscript.object", "framework/uiscript", registry::Kind::Detour,
                        registry::Phase::Attach, addr::cGZUIScriptDirector_CreateLoader,
                        "Our UI script loader 0xFA3562FB and its director: the director's start, constructor, "
                        "destructor, getter and factories; the loader's constructor, QueryInterface, Init, Shutdown, "
                        "destructors, window loading and setters (17 detours; part u9, docs/gzui.md)",
                        false, InstallObject}),
    registry::Register({"framework.uiscript.parse", "framework/uiscript", registry::Kind::Detour, registry::Phase::Attach,
                        addr::cGZUIScriptLoader_MakeParser,
                        "Our UI script loader's parser made and its words defined, the three ways in, the parser's "
                        "getter, the parent set (6 detours; part u9, docs/gzui.md)",
                        false, InstallParse}),
    registry::Register({"framework.uiscript.events", "framework/uiscript", registry::Kind::Detour, registry::Phase::Attach,
                        addr::cGZUIScriptLoader_TagStart,
                        "Our UI script loader's parser events (the document's start and end, a tag's start and end, "
                        "the attribute list) and its stack of parents (8 detours; part u9, docs/gzui.md)",
                        false, InstallEvents}),
};

} // namespace
} // namespace opensc4::gzui
