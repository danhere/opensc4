// The GZ framework's property-set readers and writers and the classes the
// property-set factory makes (docs/gzui.md, part u2), on the game's own
// objects. Replacements, each off by default:
//
//   framework.propio.binary   the binary reader-writer (vtable 0x00AD40F8):
//                             Read, Write, the deleting destructor; and
//                             ReadSet and WriteSet, which pick a binary or
//                             an XML one for a stream
//   framework.propio.xml      the XML reader-writer (vtables 0x00AD442C and,
//                             its handler at +4, 0x00AD43FC): the
//                             constructor, QueryInterface, Release, Read,
//                             the handler's EndElement and Characters, the
//                             destructors; and a UI script handler's
//                             SetText (0x00940868)
//   framework.propio.factory  the factory (vtable 0x00AD40D0): its
//                             constructor, CreateFromResource and
//                             WriteToStream
//   framework.propio.track    the Track objects' slots that read and write
//                             their set (0x00940DFF, 0x00941636, 0x0094164D,
//                             0x00941664) and the key interface's GetKey,
//                             SetType and SetInstance
//   framework.propio.tkdt     the Track objects' text form (part u2b): the
//                             `TKDT` writer 0x00940E5C and reader 0x00941930
//   framework.propio.window   the window of class 0xA3CD96CF: constructor,
//                             QueryInterface, the destructors
//   framework.propio.bag      the keyed shuffle bag 0x7B1ACFCD: constructor,
//                             QueryInterface, the deleting destructor,
//                             Clone, CopyAssign; and the bag's
//                             SetThreshold, Count and Assign
//
// and part u3:
//
//   framework.propio.xmltext    the XML reader-writer's Write (0x0094286B),
//                               its root name and the handler's StartElement
//   framework.propio.script     the UI script property handler's
//                               StartElement and EndElement
//   framework.propio.create     the factory's Create
//   framework.propio.wincopy    the window 0xA3CD96CF's Clone and
//                               CopyAssign, the script window's Assign and
//                               its part's
//   framework.propio.shufflebag the shuffle bag (vtable 0x00AD4648): its
//                               constructor, destructors, QueryInterface,
//                               Release, Add, Remove, Clear, Random, Draw,
//                               Get, Set, Erase, Write and Read
//   framework.propio.kinds      the framework's own 2D and 3D sound kinds'
//                               slots 5 and 6
//
// Still the game's: the 38 small thunks of these classes (a `this`
// adjustment and a jump, or a jump alone, as docs/occbase.md leaves them;
// they all end in the functions here or in the game's), the STLport
// container code (the lists, the window's lists, vector and tree, the
// string map), the shared stubs. Ours call each other directly, so a group
// that is off still runs ours where another group's function calls it.
//
// The game's methods answer a byte (AL) over whatever its last call left
// in EAX; ours give the same EAX where it comes from a call (the last
// call's result) and 0 above AL where the game leaves its caller's EAX.
//
// Game code calls some of these directly (the constructors, ReadSet,
// WriteSet, Assign) and may keep ECX and EDX across a call: every detour is
// entered through a thunk that keeps both. The groups go in at Attach, so
// the game's own use while it starts (the factory, the Track objects, the
// shuffle bags) runs through them too.
#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/thunk.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <iterator>

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
inline uint32_t AnswerByte(uint32_t eax, uint32_t byte) { return (eax & ~0xFFu) | (byte & 0xFFu); }
inline bool Yes(uint32_t eax) { return (eax & 0xFF) != 0; }

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("gzui: propio: first %s", name);
}

#define OPENSC4_FIRST(name)                                                                                    \
    do {                                                                                                       \
        static bool logged_ = false;                                                                           \
        if (!logged_)                                                                                          \
            FirstCall(name, logged_);                                                                          \
    } while (0)

constexpr uint32_t kIID_cIGZUnknown = 1;
constexpr uint32_t kIID_PropSetIO = 0xEBE701F3;
constexpr uint32_t kIID_XmlHandler = 0x4B6B9C9B;
constexpr uint32_t kIID_Key = 0x3B93C5E7;
constexpr uint32_t kIID_Key2 = 0x456B8F1D;
constexpr uint32_t kIID_Seekable = 0xC56B8F08;
constexpr uint32_t kIID_ResourceStream = 0x051B11BD;
constexpr uint32_t kIID_WriteTarget = 0xC51B11CB;
constexpr uint32_t kClsid_XmlParser = 0x6B6B01D1;
constexpr uint32_t kIID_XmlParser = 0x6B6B01CD;

constexpr uint32_t kRecordBinary = 0x2BEA4862;
constexpr uint32_t kRecordBinary2 = 0xCBE750E0;
constexpr uint32_t kTypeTrackText = 0x0B8D821A;
constexpr uint32_t kKeyInstance = 0xFF6F9546;

constexpr uint32_t kTypeUint32 = 0xEB61E4F7;
constexpr uint32_t kTypeSint32 = 0x0C264712;
constexpr uint32_t kTypeFloat32 = 0xABC78708;
constexpr uint32_t kTypeBool = 0xCBA908E1;
constexpr uint32_t kTypeString = 0x0B8BEA18;

// A property value as the game lays it out on the stack.
struct Value {
    std::uintptr_t vtable;
    uint32_t data;
};

// A game string on the stack (0x14 bytes, its text from +4).
struct alignas(4) TempString {
    char bytes[0x14];
};

// An STLport vector<char>.
struct CharVector {
    char* begin;
    char* end;
    char* capacity;
};

// The game string's destructor inline, as the game writes it after a
// temporary: the vtable, and the text freed when there is one. EAX is the
// free's, else the caller's `eax`.
uint32_t DestroyTemp(TempString& text, uint32_t eax)
{
    if (!At<void*>(&text, 0x4)) {
        At<std::uintptr_t>(&text, 0) = addr::SC4String_Vtable;
        return eax;
    }
    return CallThis<uint32_t>(addr::SC4String_Destroy, &text);
}

uint32_t ReleaseIf(void* object, uint32_t eax) { return object ? Virtual<uint32_t>(object, 0x8) : eax; }

// A float pushed as the game pushes it, through the FPU (FLD, FSTP).
__declspec(naked) uint32_t __cdecl ThroughX87(uint32_t)
{
    __asm {
        fld dword ptr [esp + 4]
        fstp dword ptr [esp + 4]
        mov eax, dword ptr [esp + 4]
        ret
    }
}

// The game's text to a double (0x0090FC5B, in ST0) stored as a float.
__declspec(naked) uint32_t __cdecl ParseFloat(std::uintptr_t, const char*)
{
    __asm {
        push dword ptr [esp + 8]
        call dword ptr [esp + 8]
        add esp, 4
        push eax
        fstp dword ptr [esp]
        pop eax
        ret
    }
}

// A binary reader-writer made as ReadSet and WriteSet make it.
void* NewBinary()
{
    char* const object = CallCdecl<char*>(addr::SC4_operator_new, uint32_t{0xC});
    if (object) {
        At<std::uintptr_t>(object, 0) = addr::cGZPropSetBinaryIO_Vtable;
        At<uint32_t>(object, 0x4) = 0;
        At<uint16_t>(object, 0x8) = 0;
    }
    return object;
}

void* NewXml()
{
    void* const memory = CallCdecl<void*>(addr::SC4_operator_new, uint32_t{0x54});
    return memory ? CallThis<void*>(addr::cGZPropSetXmlIO_Construct, memory) : nullptr;
}

// ====================================================================================
// The binary reader-writer

// The game's 0x0094000D.
char* __fastcall BinaryDeletingDestructor(char* self, void*, uint32_t flags)
{
    OPENSC4_FIRST("BinaryDeletingDestructor");
    At<std::uintptr_t>(self, 0) = addr::cGZPropSetBinaryIO_Vtable;
    if (flags & 1)
        CallCdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// The game's 0x009409E0: the record type, the version (kept at +8), the
// count, then each value's type, key and data. Another type is skipped.
uint32_t __fastcall BinaryRead(char* self, void*, void* stream, void* set)
{
    OPENSC4_FIRST("BinaryRead");
    uint32_t record = 0;
    uint32_t last = Virtual<uint32_t>(stream, 0x24, &record);
    if (!Yes(last) || record != kRecordBinary)
        return Answer(last, false);
    last = Virtual<uint32_t>(stream, 0x1C, &At<uint16_t>(self, 0x8));
    if (!Yes(last) || At<uint16_t>(self, 0x8) > 1)
        return Answer(last, false);
    uint32_t count = 0;
    last = Virtual<uint32_t>(stream, 0x24, &count);
    if (!Yes(last))
        return Answer(last, false);
    while (count != 0) {
        --count;
        uint32_t type = 0;
        last = Virtual<uint32_t>(stream, 0x24, &type);
        if (!Yes(last))
            return Answer(last, false);
        uint32_t key = 0;
        last = Virtual<uint32_t>(stream, 0x24, &key);
        if (!Yes(last))
            return Answer(last, false);
        if (type == kTypeString) {
            TempString text;
            CallThis<void*>(addr::SC4String_SC4String, &text);
            last = Virtual<uint32_t>(stream, 0x3C, &text);
            if (!Yes(last))
                return Answer(DestroyTemp(text, last), false);
            Value value;
            CallThis<void*>(addr::cGZPropertyString_ConstructFromIGZString, &value, &text);
            Virtual<uint32_t>(set, 0xC, &key, &value);
            last = CallThis<uint32_t>(addr::cGZPropertyString_Destruct, &value);
            last = DestroyTemp(text, last);
        } else if (type == kTypeFloat32) {
            uint32_t bits = 0;
            last = Virtual<uint32_t>(stream, 0x30, &bits);
            if (!Yes(last))
                return Answer(last, false);
            Value value{addr::cGZPropertyFloat32_Vtable, bits};
            last = Virtual<uint32_t>(set, 0xC, &key, &value);
        } else if (type == kTypeBool) {
            uint8_t byte = 0;
            last = Virtual<uint32_t>(stream, 0x14, &byte);
            if (!Yes(last))
                return Answer(last, false);
            Value value{addr::cGZPropertyBool_Vtable, byte != 0 ? 1u : 0u};
            last = Virtual<uint32_t>(set, 0xC, &key, &value);
        } else if (type == kTypeUint32) {
            uint32_t data = 0;
            last = Virtual<uint32_t>(stream, 0x24, &data);
            if (!Yes(last))
                return Answer(last, false);
            Value value{addr::cGZPropertyUint32_Vtable, data};
            last = Virtual<uint32_t>(set, 0xC, &key, &value);
        } else {
            // Skipped; the game's EAX holds the type.
            last = type;
        }
    }
    return Answer(last, true);
}

// The game's 0x0094017F: nothing for an empty set; the record type, the
// version 1, the count, then per value its type, key and data. A sint32
// stops it.
uint32_t __fastcall BinaryWrite(char*, void*, void* stream, void* set, uint32_t)
{
    OPENSC4_FIRST("BinaryWrite");
    uint32_t key = 0;
    void* value = nullptr;
    uint32_t last = Virtual<uint32_t>(set, 0x18, &key, &value);
    if (!Yes(last))
        return last;
    last = Virtual<uint32_t>(stream, 0x24, kRecordBinary);
    if (!Yes(last))
        return Answer(last, false);
    last = Virtual<uint32_t>(stream, 0x1C, uint32_t{1});
    if (!Yes(last))
        return Answer(last, false);
    last = Virtual<uint32_t>(stream, 0x24, Virtual<uint32_t>(set, 0x28));
    if (!Yes(last))
        return Answer(last, false);
    do {
        last = Virtual<uint32_t>(stream, 0x24, Virtual<uint32_t>(value, 0x10));
        if (!Yes(last))
            return Answer(last, false);
        last = Virtual<uint32_t>(stream, 0x24, key);
        if (!Yes(last))
            return Answer(last, false);
        const uint32_t type = Virtual<uint32_t>(value, 0x10);
        const uint32_t data = At<uint32_t>(value, 0x4);
        if (type == kTypeString)
            last = Virtual<uint32_t>(stream, 0x3C, At<void*>(value, 0x4));
        else if (type == kTypeFloat32)
            last = Virtual<uint32_t>(stream, 0x30, ThroughX87(data));
        else if (type == kTypeBool)
            // The game pushes the dword it stored the byte in: the stream
            // pointer's argument slot.
            last = Virtual<uint32_t>(stream, 0x14, (Bits(stream) & ~0xFFu) | (data & 0xFF));
        else if (type == kTypeUint32)
            last = Virtual<uint32_t>(stream, 0x24, data);
        else
            return Answer(type, false);
        if (!Yes(last))
            return Answer(last, false);
        last = Virtual<uint32_t>(set, 0x1C, &key, &value);
    } while (Yes(last));
    return Answer(last, true);
}

// The game's 0x009408A5 (ECX unused): the record type read and the stream
// rewound, then a binary or an XML reader's Read.
uint32_t __fastcall ReadSet(void*, void*, void* stream, void* set)
{
    OPENSC4_FIRST("ReadSet");
    void* seekable = nullptr;
    uint32_t last = Virtual<uint32_t>(stream, 0x0, kIID_Seekable, &seekable);
    if (!Yes(last))
        return Answer(ReleaseIf(seekable, last), false);
    uint32_t record = 0;
    last = Virtual<uint32_t>(stream, 0x24, &record);
    if (!Yes(last))
        return Answer(ReleaseIf(seekable, last), false);
    last = Virtual<uint32_t>(seekable, 0xDC, uint32_t{0});
    if (!Yes(last))
        return Answer(ReleaseIf(seekable, last), false);
    void* reader = nullptr;
    void* const made = record == kRecordBinary || record == kRecordBinary2 ? NewBinary() : NewXml();
    CallThis<void*>(addr::GZRefPtr_Assign, &reader, made);
    const uint32_t result = Virtual<uint32_t>(reader, 0xC, stream, set);
    last = Virtual<uint32_t>(reader, 0x8);
    return AnswerByte(ReleaseIf(seekable, last), result);
}

// The game's 0x00940973 (ECX unused).
uint32_t __fastcall WriteSet(void*, void*, void* stream, void* set, uint32_t binary)
{
    OPENSC4_FIRST("WriteSet");
    void* writer = nullptr;
    void* const made = (binary & 0xFF) ? NewBinary() : NewXml();
    CallThis<void*>(addr::GZRefPtr_Assign, &writer, made);
    const uint32_t result = Virtual<uint32_t>(writer, 0x10, stream, set, uint32_t{0});
    return AnswerByte(Virtual<uint32_t>(writer, 0x8), result);
}

// ====================================================================================
// The XML reader-writer

// The game's 0x009406CB.
char* __fastcall XmlConstruct(char* self, void*)
{
    OPENSC4_FIRST("XmlConstruct");
    At<std::uintptr_t>(self, 0x4) = addr::cGZPropSetXmlIO_HandlerBaseVtable;
    At<uint32_t>(self, 0x8) = 0;
    At<uint32_t>(self, 0xC) = 0;
    At<std::uintptr_t>(self, 0x0) = addr::cGZPropSetXmlIO_Vtable;
    At<std::uintptr_t>(self, 0x4) = addr::cGZPropSetXmlIO_HandlerVtable;
    At<uint8_t>(self, 0x10) = 0;
    CallThis<void*>(addr::SC4String_SC4String, self + 0x14);
    CallThis<void*>(addr::SC4String_SC4String, self + 0x28);
    CallThis<void*>(addr::SC4String_SC4String, self + 0x3C);
    At<void*>(self, 0x50) = nullptr;
    return self;
}

// The game's 0x00940722.
uint32_t __fastcall XmlQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    OPENSC4_FIRST("XmlQueryInterface");
    if (iid == kIID_cIGZUnknown || iid == kIID_PropSetIO)
        *out = self;
    else if (iid == kIID_XmlHandler)
        *out = self ? self + 0x4 : nullptr;
    else
        return 0;
    return Answer(Virtual<uint32_t>(self, 0x4), true);
}

// The game's 0x0094070E: the count lowered first.
uint32_t __fastcall XmlRelease(char* self, void*)
{
    OPENSC4_FIRST("XmlRelease");
    const int32_t count = --At<int32_t>(self, 0x8);
    if (count > 0)
        return static_cast<uint32_t>(count);
    Virtual<void*>(self, 0x14, uint32_t{1});
    return 0;
}

// The game's 0x00941679: the parser through the COM, the stream's bytes
// read whole and parsed with this object's handler into the set.
uint32_t __fastcall XmlRead(char* self, void*, void* stream, void* set)
{
    OPENSC4_FIRST("XmlRead");
    void* parser = nullptr;
    void* const com = CallCdecl<void*>(addr::RZGetCOM);
    uint32_t last = Virtual<uint32_t>(com, 0xC, kClsid_XmlParser, kIID_XmlParser, &parser);
    if (!parser)
        return Answer(last, false);
    last = Virtual<uint32_t>(parser, 0x20, self ? self + 0x4 : nullptr);
    if (!Yes(last))
        return Answer(Virtual<uint32_t>(parser, 0x8), false);
    void* seekable = nullptr;
    last = Virtual<uint32_t>(stream, 0x0, kIID_Seekable, &seekable);
    if (!Yes(last)) {
        last = ReleaseIf(seekable, last);
        return Answer(Virtual<uint32_t>(parser, 0x8), false);
    }
    CharVector bytes;
    uint8_t fill = 0;
    CallThis<void*>(addr::SC4CharVector_Construct, &bytes, &fill);
    const uint32_t size = Virtual<uint32_t>(seekable, 0xD0);
    fill = 0;
    CallThis<void>(addr::SC4CharVector_Resize, &bytes, size, &fill);
    last = Virtual<uint32_t>(stream, 0x44, bytes.begin, static_cast<uint32_t>(bytes.end - bytes.begin));
    if (!Yes(last)) {
        CallThis<void>(addr::SC4CharVector_Destroy, &bytes);
        last = ReleaseIf(seekable, last);
        return Answer(Virtual<uint32_t>(parser, 0x8), false);
    }
    At<void*>(self, 0x50) = set;
    last = Virtual<uint32_t>(parser, 0xC, bytes.begin, static_cast<uint32_t>(bytes.end - bytes.begin), uint32_t{1});
    const uint32_t result = last;
    CallThis<void>(addr::SC4CharVector_Destroy, &bytes);
    last = ReleaseIf(seekable, last);
    last = ReleaseIf(parser, last);
    return AnswerByte(last, result);
}

// The game's 0x00940C5B (this is the handler, the object + 4): the element
// closed; under the root, the value its key, type and text describe put in
// the set.
uint32_t __fastcall XmlEndElement(char* handler, void*, const void*)
{
    OPENSC4_FIRST("XmlEndElement");
    if (--At<int32_t>(handler, 0x8) == 0)
        return 1;
    uint32_t key = static_cast<uint32_t>(
        CallCdecl<int32_t>(addr::SC4_TextToInt, Virtual<const char*>(handler + 0x10, 0x14)));
    const uint32_t type = static_cast<uint32_t>(CallCdecl<int32_t>(addr::SC4_TextToInt, At<const char*>(handler, 0x28)));
    const char* const text = At<const char*>(handler, 0x3C);
    void* const set = At<void*>(handler, 0x4C);
    uint32_t last;
    if (type == kTypeString) {
        Value value;
        CallThis<void*>(addr::cGZPropertyString_ConstructFromIGZString, &value, handler + 0x38);
        Virtual<uint32_t>(set, 0xC, &key, &value);
        last = CallThis<uint32_t>(addr::cGZPropertyString_Destruct, &value);
    } else if (type == kTypeSint32) {
        Value value{addr::cGZPropertySint32_Vtable, CallCdecl<uint32_t>(addr::SC4_TextToUint32, text)};
        last = Virtual<uint32_t>(set, 0xC, &key, &value);
    } else if (type == kTypeFloat32) {
        Value value{addr::cGZPropertyFloat32_Vtable, ParseFloat(addr::SC4_ParseDouble90FC5B, text)};
        last = Virtual<uint32_t>(set, 0xC, &key, &value);
    } else if (type == kTypeBool) {
        TempString truth;
        CallThis<void*>(addr::SC4String_FromText, &truth, reinterpret_cast<const char*>(addr::cGZPropSetXmlIO_TrueText));
        const int32_t order = Virtual<int32_t>(handler + 0x38, 0x30, &truth, uint32_t{1});
        Value value{addr::cGZPropertyBool_Vtable, order == 0 ? 1u : 0u};
        last = Virtual<uint32_t>(set, 0xC, &key, &value);
        last = DestroyTemp(truth, last);
    } else if (type == kTypeUint32) {
        Value value{addr::cGZPropertyUint32_Vtable, CallCdecl<uint32_t>(addr::SC4_TextToUint32, text)};
        last = Virtual<uint32_t>(set, 0xC, &key, &value);
    } else {
        return Answer(type, false);
    }
    return Answer(last, true);
}

// The game's 0x00940773: text with anything but tabs, line feeds,
// carriage returns and spaces replaces the string at +0x3C.
uint32_t __fastcall XmlCharacters(char* handler, void*, const char* text, uint32_t length)
{
    OPENSC4_FIRST("XmlCharacters");
    for (uint32_t i = 0; i < length; ++i) {
        const signed char c = static_cast<signed char>(text[i]);
        if (c < 9 || (c > 10 && c != 13 && c != 32))
            return Answer(Virtual<uint32_t>(handler + 0x38, 0xC, text, length), true);
    }
    return 1;
}

// The game's 0x009407CB.
uint32_t __fastcall XmlDestruct(char* self, void*)
{
    OPENSC4_FIRST("XmlDestruct");
    At<std::uintptr_t>(self, 0x0) = addr::cGZPropSetXmlIO_Vtable;
    At<std::uintptr_t>(self, 0x4) = addr::cGZPropSetXmlIO_HandlerVtable;
    CallThis<uint32_t>(addr::SC4String_Destroy, self + 0x3C);
    CallThis<uint32_t>(addr::SC4String_Destroy, self + 0x28);
    return CallThis<uint32_t>(addr::SC4String_Destroy, self + 0x14);
}

// The game's 0x009407AF.
char* __fastcall XmlDeletingDestructor(char* self, void*, uint32_t flags)
{
    OPENSC4_FIRST("XmlDeletingDestructor");
    CallThis<uint32_t>(addr::cGZPropSetXmlIO_Destruct, self);
    if (flags & 1)
        CallCdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// The game's 0x00940868: the text, as a game string, to slot 16 of the
// object at +0x1C.
uint32_t __fastcall ScriptHandlerSetText(char* self, void*, const char* text, uint32_t)
{
    OPENSC4_FIRST("ScriptHandlerSetText");
    TempString string;
    CallThis<void*>(addr::SC4String_FromText, &string, text);
    const uint32_t last = Virtual<uint32_t>(self + 0x1C, 0x40, &string);
    return Answer(DestroyTemp(string, last), true);
}

// ====================================================================================
// The factory

// The game's 0x00940294.
char* __fastcall FactoryConstruct(char* self, void*)
{
    OPENSC4_FIRST("FactoryConstruct");
    At<uint32_t>(self, 0x4) = 0;
    At<std::uintptr_t>(self, 0) = addr::cGZPropSetFactory_Vtable;
    return self;
}

// The game's 0x009400B2: an object of the resource's type, given its key,
// asked for iid and read from the resource's stream.
uint32_t __fastcall FactoryCreateFromResource(char* self, void*, void* resource, uint32_t iid, void** out, uint32_t a4,
                                              uint32_t a5)
{
    OPENSC4_FIRST("FactoryCreateFromResource");
    void* object = nullptr;
    alignas(4) char key[0x10];
    const uint32_t* const type = Virtual<const uint32_t*>(resource, 0xC, key);
    uint32_t last = Virtual<uint32_t>(self, 0x10, *type, kIID_Key, &object, a4, a5);
    if (!Yes(last))
        return Answer(ReleaseIf(object, last), false);
    Virtual<uint32_t>(object, 0x10, Virtual<const uint32_t*>(resource, 0xC, key));
    void* stream = nullptr;
    last = Virtual<uint32_t>(resource, 0x0, kIID_ResourceStream, &stream);
    bool done = false;
    if (Yes(last)) {
        last = Virtual<uint32_t>(object, 0x0, iid, out);
        if (Yes(last)) {
            last = Virtual<uint32_t>(object, 0x30, stream, a4, a5);
            done = Yes(last);
        }
    }
    last = ReleaseIf(stream, last);
    return Answer(ReleaseIf(object, last), done);
}

// The game's 0x00940041: the object's key interface's slot 13 (write) to
// the target's interface 0xC51B11CB.
uint32_t __fastcall FactoryWriteToStream(char*, void*, void* object, void* target)
{
    OPENSC4_FIRST("FactoryWriteToStream");
    void* stream = nullptr;
    uint32_t last = Virtual<uint32_t>(target, 0x0, kIID_WriteTarget, &stream);
    if (!Yes(last))
        return Answer(ReleaseIf(stream, last), false);
    void* key = nullptr;
    Virtual<uint32_t>(object, 0x0, kIID_Key, &key);
    const uint32_t result = Virtual<uint32_t>(key, 0x34, stream, uint32_t{0}, uint32_t{0});
    last = ReleaseIf(key, result);
    return AnswerByte(ReleaseIf(stream, last), result);
}

// ====================================================================================
// The Track objects and the key interface

// The game's 0x00940DFF (this is the key interface at +0x18): the key's
// instance kept under 0xFF6F9546, then the text reader for type 0x0B8D821A,
// else ReadSet.
uint32_t __fastcall TrackReadResource(char* key, void*, void* stream, uint32_t a2, uint32_t a3)
{
    OPENSC4_FIRST("TrackReadResource");
    char* const set = key - 0x18;
    uint32_t id = kKeyInstance;
    Value value{addr::cGZPropertyUint32_Vtable, At<uint32_t>(key, 0xC)};
    Virtual<uint32_t>(set, 0xC, &id, &value);
    if (At<uint32_t>(key, 0x4) == kTypeTrackText)
        return Virtual<uint32_t>(set, 0x30, stream, a2, a3);
    return CallThis<uint32_t>(addr::cGZPropSetIO_ReadSet, &value, stream, set);
}

// The game's 0x00941636, 0x0094164D and 0x00941664.
uint32_t __fastcall TrackWriteXml(char* set, void*, void* stream, uint32_t, uint32_t)
{
    OPENSC4_FIRST("TrackWriteXml");
    Value local;
    return CallThis<uint32_t>(addr::cGZPropSetIO_WriteSet, &local, stream, set, uint32_t{0});
}

uint32_t __fastcall TrackWriteBinary(char* set, void*, void* stream, uint32_t, uint32_t)
{
    OPENSC4_FIRST("TrackWriteBinary");
    Value local;
    return CallThis<uint32_t>(addr::cGZPropSetIO_WriteSet, &local, stream, set, uint32_t{1});
}

uint32_t __fastcall TrackReadSet(char* set, void*, void* stream, uint32_t, uint32_t)
{
    OPENSC4_FIRST("TrackReadSet");
    Value local;
    return CallThis<uint32_t>(addr::cGZPropSetIO_ReadSet, &local, stream, set);
}

// The game's 0x00940512, 0x0094052A and 0x00940536.
uint32_t* __fastcall KeyGetKey(char* self, void*, uint32_t* out)
{
    OPENSC4_FIRST("KeyGetKey");
    out[0] = At<uint32_t>(self, 0x4);
    out[1] = At<uint32_t>(self, 0x8);
    out[2] = At<uint32_t>(self, 0xC);
    return out;
}

uint32_t __fastcall KeySetType(char* self, void*, uint32_t type)
{
    OPENSC4_FIRST("KeySetType");
    At<uint32_t>(self, 0x4) = type;
    return Answer(type, true);
}

uint32_t __fastcall KeySetInstance(char* self, void*, uint32_t instance)
{
    OPENSC4_FIRST("KeySetInstance");
    At<uint32_t>(self, 0xC) = instance;
    return Answer(instance, true);
}

// ====================================================================================
// The Track objects' text form (part u2b)
//
// `TKDT,2,` and an empty name, then 27 values in a fixed order, each a
// number followed by a comma (or a comma alone where the set has no uint32
// under the key), then `ETKD`.

enum class Print : uint8_t { Hex, Dec };

struct TextField {
    uint32_t key;
    Print print;
};

// The writer's order; the reader's is the same, with the sixth field only
// from version 1 and the last six only from version 2.
constexpr TextField kTextFields[] = {
    {0xFFAEC905, Print::Hex}, {0xFF5F15E1, Print::Hex}, {0xFF4DCD0D, Print::Dec}, {0xFF050A94, Print::Dec},
    {0xFFB0333D, Print::Dec}, {0xFF706616, Print::Dec}, {0xFFCC156F, Print::Dec}, {0xFFCC3B29, Print::Dec},
    {0xFF891EAA, Print::Hex}, {0xFFAE9DDB, Print::Dec}, {0xFFF7B8DF, Print::Dec}, {0xFF90AAC8, Print::Dec},
    {0xFF3C2160, Print::Hex}, {0xFFD7EE4D, Print::Hex}, {0xFF59FDD8, Print::Hex}, {0xFF2D7CD1, Print::Hex},
    {0xFF0246FE, Print::Dec}, {0xFFFF4E79, Print::Dec}, {0xFF595FFD, Print::Dec}, {0xFF8DCFD8, Print::Dec},
    {0xFF69510B, Print::Dec}, {0xFF099DB2, Print::Dec}, {0xFF53DBF3, Print::Dec}, {0xFF3EC880, Print::Dec},
    {0xFF47F685, Print::Dec}, {0xFFCAA344, Print::Dec}, {0xFF303B1B, Print::Dec},
};
constexpr std::size_t kTextFieldVersion1 = 5;  // 0xFF706616
constexpr std::size_t kTextFieldFirstSet = 6;  // 0xFFCC156F
constexpr std::size_t kTextFieldDefault = 8;   // 0xFF891EAA
constexpr std::size_t kTextFieldVersion2 = 21; // 0xFF099DB2 on

constexpr uint32_t kTagTkd2 = 0x544B4432;
constexpr uint32_t kIID_cIGZString = 0x089B7DC8;
constexpr uint32_t kTextDefault = 0x7DCDED2D;
constexpr std::size_t kSetInstance = 0x24; // the key interface's instance

uint32_t SetUint32(char* set, uint32_t key, uint32_t data)
{
    Value value{addr::cGZPropertyUint32_Vtable, data};
    return Virtual<uint32_t>(set, 0xC, &key, &value);
}

// The game's 0x00940E5C: the writer's two static strings made on first use
// (`ETKD` and `,`, destroyed at exit), the text built and written whole
// through the stream's slot 0x44. Its result is not looked at: true.
uint32_t __fastcall TrackWriteText(char* set, void*, void* stream, uint32_t, uint32_t)
{
    OPENSC4_FIRST("TrackWriteText");
    TempString number;
    CallThis<void*>(addr::SC4String_SC4String, &number);
    TempString text;
    CallThis<void*>(addr::SC4String_FromText, &text, reinterpret_cast<const char*>(addr::cGZPropSetTrack_TextHeader));
    uint32_t& guard = *reinterpret_cast<uint32_t*>(addr::cGZPropSetTrack_TextStaticsGuard);
    void* const end = reinterpret_cast<void*>(addr::cGZPropSetTrack_TextEnd);
    void* const comma = reinterpret_cast<void*>(addr::cGZPropSetTrack_TextComma);
    if (!(guard & 1)) {
        guard |= 1;
        CallThis<void*>(addr::SC4String_FromText, end, reinterpret_cast<const char*>(addr::cGZPropSetTrack_EndText));
        CallCdecl<int>(addr::SC4_atexit, addr::cGZPropSetTrack_DestroyTextEnd);
    }
    if (!(guard & 2)) {
        guard |= 2;
        CallThis<void*>(addr::SC4String_FromText, comma, reinterpret_cast<const char*>(addr::SC4_CommaText));
        CallCdecl<int>(addr::SC4_atexit, addr::cGZPropSetTrack_DestroyTextComma);
    }
    TempString unused;
    CallThis<void*>(addr::SC4String_FromText, &unused, reinterpret_cast<const char*>(addr::MySim_ScriptEmpty));
    // The name: empty.
    CallThis<void*>(addr::cRZBaseString_AppendText, &text, CallThis<const char*>(addr::cRZBaseString_Data, comma));
    Value value{addr::cGZPropertyUint32_Vtable, 0};
    for (const TextField& field : kTextFields) {
        uint32_t key = field.key;
        void* from = comma;
        if (Yes(Virtual<uint32_t>(set, 0x20, &key, &value))) {
            const std::uintptr_t format = field.print == Print::Hex ? addr::cGZPropSetTrack_HexFormat
                                                                     : addr::cGZPropSetTrack_DecFormat;
            CallCdecl<uint32_t>(addr::cRZBaseString_Sprintf, &number, reinterpret_cast<const char*>(format),
                                value.data);
            from = &number;
        }
        CallThis<void*>(addr::cRZBaseString_AppendText, &text, CallThis<const char*>(addr::cRZBaseString_Data, from));
    }
    CallThis<void*>(addr::cRZBaseString_AppendText, &text, CallThis<const char*>(addr::cRZBaseString_Data, end));
    const uint32_t length = CallThis<uint32_t>(addr::cRZBaseString_Strlen, &text);
    uint32_t last = Virtual<uint32_t>(stream, 0x44, CallThis<const char*>(addr::cRZBaseString_Data, &text), length);
    value.vtable = addr::cGZPropertyValue_Vtable;
    if (void* const body = At<void*>(&unused, 0x4))
        last = CallCdecl<uint32_t>(addr::SC4_Free, body);
    last = DestroyTemp(text, last);
    last = DestroyTemp(number, last);
    return Answer(last, true);
}

// The reader's strings and the stream's seekable interface, freed in the
// game's order on either way out.
struct TextReadState {
    TempString body;   // the stream's bytes
    TempString rest;   // what is left to split
    TempString field;  // the field just split off
    TempString name;   // the third field, kept and unused
    void* seekable = nullptr;

    uint32_t Finish(uint32_t last)
    {
        last = DestroyTemp(name, last);
        last = DestroyTemp(field, last);
        last = DestroyTemp(rest, last);
        last = ReleaseIf(seekable, last);
        return DestroyTemp(body, last);
    }
};

// The next field off the text into `field`, as the game splits it (the
// piece a temporary, assigned and destroyed).
uint32_t NextField(TextReadState& state)
{
    TempString piece;
    void* const split = CallThis<void*>(addr::SC4String_SplitOff, &state.rest, &piece,
                                        reinterpret_cast<const char*>(addr::SC4_CommaText));
    const uint32_t last = CallThis<uint32_t>(addr::cRZBaseString_Assign, &state.field, split);
    return DestroyTemp(piece, last);
}

const char* FieldText(TextReadState& state) { return CallThis<const char*>(addr::cRZBaseString_Data, &state.field); }
uint32_t FieldNumber(TextReadState& state) { return CallCdecl<uint32_t>(addr::SC4_TextToUint32, FieldText(state)); }

// A field put in the set as a uint32 when it is not empty.
uint32_t ReadField(TextReadState& state, char* set, uint32_t key)
{
    NextField(state);
    const uint32_t length = CallThis<uint32_t>(addr::cRZBaseString_Strlen, &state.field);
    if (length == 0)
        return length;
    return SetUint32(set, key, FieldNumber(state));
}

// The game's 0x00941930: the stream's bytes whole (a leading dword equal to
// the size less 4 is skipped; the first dword read is not part of the text),
// `TKD2` text starting 8 bytes on; split at commas. The first field is
// skipped, the second is the version, the third (a name) kept and unused;
// then the values in the writer's order, each one only when its field is
// not empty. The first equal to the set's own instance stops it with false.
// 0xFF891EAA is always set: its field when not 0, else 0x7DCDED2D (or
// 0x7DCDED3D when 0xFFCC156F was given as a number other than 0).
uint32_t __fastcall TrackReadText(char* set, void*, void* stream, uint32_t, uint32_t)
{
    OPENSC4_FIRST("TrackReadText");
    TextReadState state;
    CallThis<void*>(addr::SC4String_SC4String, &state.body);
    uint32_t tag = 0;
    if (Yes(Virtual<uint32_t>(stream, 0x0, kIID_Seekable, &state.seekable))) {
        uint32_t size = Virtual<uint32_t>(state.seekable, 0xD0);
        CallThis<void>(addr::cRZBaseString_Truncate, &state.body, size);
        Virtual<uint32_t>(state.seekable, 0x34, &tag);
        if (tag == size - 4) {
            Virtual<uint32_t>(state.seekable, 0x34, &tag);
            size -= 4;
        }
        Virtual<uint32_t>(state.seekable, 0x6C, At<char*>(&state.body, 0x4), size - 4);
    }
    const char* const bytes = At<char*>(&state.body, 0x4);
    CallThis<void*>(addr::SC4String_FromText, &state.rest, bytes + (tag == kTagTkd2 ? 8 : 0));
    CallThis<void*>(addr::SC4String_SC4String, &state.field);
    const char* const comma = reinterpret_cast<const char*>(addr::SC4_CommaText);
    {
        TempString piece;
        CallThis<void*>(addr::SC4String_SplitOff, &state.rest, &piece, comma);
        DestroyTemp(piece, 0);
    }
    NextField(state);
    const uint32_t version = FieldNumber(state);
    {
        TempString piece;
        void* const split = CallThis<void*>(addr::SC4String_SplitOff, &state.rest, &piece, comma);
        CallThis<void*>(addr::SC4String_Substring, &state.name, split, uint32_t{0}, uint32_t{0xFFFFFFFF});
        DestroyTemp(piece, 0);
    }
    void* nameInterface = nullptr;
    CallThis<uint32_t>(addr::cRZBaseString_QueryInterface, &state.name, kIID_cIGZString, &nameInterface);

    uint32_t last = 0;
    NextField(state);
    if (CallThis<uint32_t>(addr::cRZBaseString_Strlen, &state.field) != 0) {
        const uint32_t first = FieldNumber(state);
        if (first == At<uint32_t>(set, kSetInstance))
            return Answer(state.Finish(first), false);
        SetUint32(set, kTextFields[0].key, first);
    }
    for (std::size_t i = 1; i < kTextFieldFirstSet; ++i)
        if (i != kTextFieldVersion1 || version > 0)
            ReadField(state, set, kTextFields[i].key);
    uint32_t given = 0;
    NextField(state);
    if (CallThis<uint32_t>(addr::cRZBaseString_Strlen, &state.field) != 0) {
        given = FieldNumber(state);
        SetUint32(set, kTextFields[kTextFieldFirstSet].key, given);
    }
    ReadField(state, set, kTextFields[kTextFieldFirstSet + 1].key);
    uint32_t fallback = kTextDefault + (given != 0 ? 0x10 : 0);
    NextField(state);
    if (CallThis<uint32_t>(addr::cRZBaseString_Strlen, &state.field) != 0) {
        const uint32_t number = FieldNumber(state);
        if (number != 0)
            fallback = number;
    }
    last = SetUint32(set, kTextFields[kTextFieldDefault].key, fallback);
    for (std::size_t i = kTextFieldDefault + 1; i < std::size(kTextFields); ++i)
        if (i < kTextFieldVersion2 || version > 1)
            last = ReadField(state, set, kTextFields[i].key);
    return Answer(state.Finish(last), true);
}

// ====================================================================================
// The window of class 0xA3CD96CF

constexpr std::uintptr_t kWindowVtables[8] = {
    0x00AD4268, 0x00AD4244, 0x00AD4200, 0x00AD4168, 0x00AD4158, 0x00AD4148, 0x00AD4128, 0x00AD4110,
};
constexpr std::size_t kWindowKey = 0x104;

void SetWindowVtables(char* self)
{
    for (std::size_t i = 0; i < 8; ++i)
        At<std::uintptr_t>(self, 4 * i) = kWindowVtables[i];
}

// The game's 0x009402A1.
char* __fastcall WindowConstruct(char* self, void*, const uint32_t* key)
{
    OPENSC4_FIRST("WindowConstruct");
    CallThis<void*>(addr::cGZScriptWin_Construct, self);
    At<std::uintptr_t>(self, kWindowKey) = addr::cGZResourceKey_Vtable;
    At<std::uintptr_t>(self, kWindowKey) = 0x00AD42FC;
    SetWindowVtables(self);
    At<uint32_t>(self, kWindowKey + 0x4) = key[0];
    At<uint32_t>(self, kWindowKey + 0x8) = key[1];
    At<uint32_t>(self, kWindowKey + 0xC) = key[2];
    return self;
}

// The game's 0x00940312.
uint32_t __fastcall WindowQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    OPENSC4_FIRST("WindowQueryInterface");
    if (iid != kIID_Key && iid != kIID_Key2)
        return CallThis<uint32_t>(addr::cGZScriptWin_QueryInterface, self, iid, out);
    *out = self ? self + kWindowKey : nullptr;
    return Answer(Virtual<uint32_t>(self, 0x4), true);
}

// The game's 0x00940371.
uint32_t __fastcall WindowDestruct(char* self, void*)
{
    OPENSC4_FIRST("WindowDestruct");
    SetWindowVtables(self);
    At<std::uintptr_t>(self, kWindowKey) = 0x00AD42FC;
    return CallThis<uint32_t>(addr::cGZScriptWin_Destruct, self);
}

// The game's 0x00940355.
char* __fastcall WindowDeletingDestructor(char* self, void*, uint32_t flags)
{
    OPENSC4_FIRST("WindowDeletingDestructor");
    CallThis<uint32_t>(addr::cGZKeyedWin_Destruct, self);
    if (flags & 1)
        CallCdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// ====================================================================================
// The keyed shuffle bag

constexpr std::uintptr_t kBagKeyVtable = 0x00AD4338;
constexpr std::size_t kBagKey = 0x18;

// The shuffle bag's own slots (part u3, below), which the keyed bag calls.
char* __fastcall ShuffleBagConstruct(char* self, void*);
uint32_t __fastcall ShuffleBagQueryInterface(char* self, void*, uint32_t iid, void** out);
uint32_t __fastcall ShuffleBagDestruct(char* self, void*);

// The game's 0x0094048B.
char* __fastcall BagConstruct(char* self, void*, const uint32_t* key)
{
    OPENSC4_FIRST("BagConstruct");
    ShuffleBagConstruct(self, nullptr);
    At<std::uintptr_t>(self, kBagKey) = addr::cGZResourceKey_Vtable;
    At<std::uintptr_t>(self, 0) = addr::cGZKeyedShuffleBag_Vtable;
    At<std::uintptr_t>(self, kBagKey) = kBagKeyVtable;
    At<uint32_t>(self, kBagKey + 0x4) = key[0];
    At<uint32_t>(self, kBagKey + 0x8) = key[1];
    At<uint32_t>(self, kBagKey + 0xC) = key[2];
    return self;
}

// The game's 0x009404C2: a fixed threshold.
uint32_t __fastcall BagSetThreshold(char* self, void*, uint32_t threshold)
{
    OPENSC4_FIRST("BagSetThreshold");
    At<uint32_t>(self, 0x10) = threshold;
    At<uint8_t>(self, 0x14) = 1;
    return Answer(threshold, true);
}

// The game's 0x009404D2.
uint32_t __fastcall BagQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    OPENSC4_FIRST("BagQueryInterface");
    if (iid != kIID_Key && iid != kIID_Key2)
        return ShuffleBagQueryInterface(self, nullptr, iid, out);
    *out = self ? self + kBagKey : nullptr;
    return Answer(Virtual<uint32_t>(self, 0x4), true);
}

// The game's 0x00940552.
char* __fastcall BagDeletingDestructor(char* self, void*, uint32_t flags)
{
    OPENSC4_FIRST("BagDeletingDestructor");
    At<std::uintptr_t>(self, 0) = addr::cGZKeyedShuffleBag_Vtable;
    At<std::uintptr_t>(self, kBagKey) = kBagKeyVtable;
    ShuffleBagDestruct(self, nullptr);
    if (flags & 1)
        CallCdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// The game's 0x0094067A: both lists' sizes.
uint32_t __fastcall BagCount(char* self, void*)
{
    OPENSC4_FIRST("BagCount");
    const uint32_t drawn = CallThis<uint32_t>(addr::GZUint32List_Size, self + 0xC);
    return drawn + CallThis<uint32_t>(addr::GZUint32List_Size, self + 0x8);
}

// The game's 0x009417FF: the count at +4 copied too.
char* __fastcall BagAssign(char* self, void*, char* other)
{
    OPENSC4_FIRST("BagAssign");
    At<uint32_t>(self, 0x4) = At<uint32_t>(other, 0x4);
    CallThis<void*>(addr::GZUint32List_Assign, self + 0x8, other + 0x8);
    CallThis<void*>(addr::GZUint32List_Assign, self + 0xC, other + 0xC);
    At<uint32_t>(self, 0x10) = At<uint32_t>(other, 0x10);
    At<uint8_t>(self, 0x14) = At<uint8_t>(other, 0x14);
    return self;
}

// The game's 0x009417D8.
char* __fastcall BagCopyAssign(char* self, void*, char* other)
{
    OPENSC4_FIRST("BagCopyAssign");
    CallThis<void*>(addr::cGZShuffleBag_Assign, self, other);
    At<uint32_t>(self, 0x1C) = At<uint32_t>(other, 0x1C);
    At<uint32_t>(self, 0x20) = At<uint32_t>(other, 0x20);
    At<uint32_t>(self, 0x24) = At<uint32_t>(other, 0x24);
    return self;
}

// The game's 0x00941799 (this is the key interface): a copy with this key,
// asked for iid.
uint32_t __fastcall BagClone(char* key, void*, uint32_t iid, void** out)
{
    OPENSC4_FIRST("BagClone");
    void* const memory = CallCdecl<void*>(addr::SC4_operator_new, uint32_t{0x28});
    char* const copy = memory ? CallThis<char*>(addr::cGZKeyedShuffleBag_Construct, memory, key + 0x4) : nullptr;
    CallThis<void*>(addr::cGZKeyedShuffleBag_CopyAssign, copy, key - kBagKey);
    return Virtual<uint32_t>(copy, 0x0, iid, out);
}

// ====================================================================================
// Part u3: helpers

inline const char* Text(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }
inline const char* TextAt(std::uintptr_t pointer) { return *reinterpret_cast<const char* const*>(pointer); }
inline const char* Body(const TempString& string) { return At<const char*>(const_cast<TempString*>(&string), 0x4); }

// A virtual __cdecl slot (the game string's Sprintf, slot 0x6C): `this` on
// the stack, the caller pops.
template <typename... Args>
inline uint32_t VirtualCdecl(void* object, std::size_t byteOffset, Args... args)
{
    void** const vtable = *reinterpret_cast<void***>(object);
    return reinterpret_cast<uint32_t(__cdecl*)(void*, Args...)>(vtable[byteOffset / 4])(object, args...);
}

// A float widened as the game widens it for a `%f` (FLD, FSTP double).
__declspec(naked) double __cdecl WidenX87(uint32_t)
{
    __asm {
        fld dword ptr [esp + 4]
        ret
    }
}

// A __thiscall function without arguments that answers in ST0, stored as a
// float (FSTP dword).
__declspec(naked) uint32_t __cdecl ThisToFloat(std::uintptr_t, void*)
{
    __asm {
        mov ecx, dword ptr [esp + 8]
        call dword ptr [esp + 4]
        push eax
        fstp dword ptr [esp]
        pop eax
        ret
    }
}

uint32_t Strlen(void* string) { return CallThis<uint32_t>(addr::cRZBaseString_Strlen, string); }
const char* Data(void* string) { return CallThis<const char*>(addr::cRZBaseString_Data, string); }

// A game string's text through the stream's slot 0x44, the body read first
// as the game does.
uint32_t WriteText(void* stream, TempString& text)
{
    const char* const body = Body(text);
    const uint32_t length = Strlen(&text);
    return Virtual<uint32_t>(stream, 0x44, body, length);
}

// An STLport list<uint32_t> by its head node (each node: next, previous,
// the value at +8).
inline char* Head(char* list) { return At<char*>(list, 0); }
inline char* Next(char* node) { return At<char*>(node, 0); }

uint32_t Size(char* list)
{
    uint32_t n = 0;
    for (char* node = Next(Head(list)); node != Head(list); node = Next(node))
        ++n;
    return n;
}

// ====================================================================================
// Part u3: the XML writer and the handler's StartElement

// The game's 0x00942D50 (this and the third argument unused): the root
// element's name into out.
uint32_t __fastcall XmlRootName(char*, void*, void* out, const void*)
{
    OPENSC4_FIRST("XmlRootName");
    TempString name;
    void* const made = CallThis<void*>(addr::SC4String_FromText, &name, TextAt(addr::cGZPropSetXmlIO_RootTag));
    const uint32_t last = CallThis<uint32_t>(addr::cRZBaseString_Assign, out, made);
    return DestroyTemp(name, last);
}

// The strings of one value's element, destroyed in the game's order.
struct XmlLine {
    TempString indent;
    TempString start;
    TempString data;
    TempString end;
    TempString id;

    uint32_t Finish(uint32_t last)
    {
        last = DestroyTemp(id, last);
        last = DestroyTemp(end, last);
        last = DestroyTemp(data, last);
        last = DestroyTemp(start, last);
        return DestroyTemp(indent, last);
    }
};

// The game's 0x0094286B: the header, the root element, then one element per
// value (three spaces, the opening tag with the key and type, the value's
// text, the closing tag), the root's end; every piece one write. A value of
// another type or a failed write stops it with false.
uint32_t __fastcall XmlWrite(char* self, void*, void* stream, void* set, uint32_t)
{
    OPENSC4_FIRST("XmlWrite");
    uint32_t key = 0;
    void* value = nullptr;
    uint32_t last = Virtual<uint32_t>(set, 0x18, &key, &value);
    if (!Yes(last))
        return last;
    TempString header;
    CallThis<void*>(addr::SC4String_SC4String, &header);
    CallCdecl<uint32_t>(addr::cRZBaseString_Sprintf, &header, Text(addr::cGZPropSetXmlIO_HeaderFormat), uint32_t{1},
                        uint32_t{0});
    last = WriteText(stream, header);
    if (!Yes(last))
        return Answer(DestroyTemp(header, last), false);
    TempString root;
    CallThis<void*>(addr::SC4String_SC4String, &root);
    XmlRootName(self, nullptr, &root, &key);
    TempString open;
    CallThis<void*>(addr::SC4String_SC4String, &open);
    CallCdecl<uint32_t>(addr::cRZBaseString_Sprintf, &open, Text(addr::cGZPropSetXmlIO_OpenRootFormat), Body(root));
    last = WriteText(stream, open);
    bool done = false;
    if (Yes(last)) {
        for (;;) {
            XmlLine line;
            CallThis<void*>(addr::SC4String_FromText, &line.indent, Text(addr::cGZPropSetXmlIO_Indent));
            CallThis<void*>(addr::SC4String_SC4String, &line.start);
            CallThis<void*>(addr::SC4String_SC4String, &line.data);
            CallThis<void*>(addr::SC4String_SC4String, &line.end);
            CallThis<void*>(addr::SC4String_SC4String, &line.id);
            VirtualCdecl(&line.id, 0x6C, Text(addr::Ordinance_IdFormat), key);
            const uint32_t type = Virtual<uint32_t>(value, 0x10);
            std::uintptr_t opening = 0;
            std::uintptr_t closing = 0;
            if (type == kTypeString) {
                opening = addr::cGZPropSetXmlIO_StringOpen;
                closing = addr::cGZPropSetXmlIO_StringClose;
            } else if (type == kTypeSint32) {
                opening = addr::cGZPropSetXmlIO_Sint32Open;
                closing = addr::cGZPropSetXmlIO_Sint32Close;
            } else if (type == kTypeFloat32) {
                opening = addr::cGZPropSetXmlIO_FloatOpen;
                closing = addr::cGZPropSetXmlIO_FloatClose;
            } else if (type == kTypeBool) {
                opening = addr::cGZPropSetXmlIO_BoolOpen;
                closing = addr::cGZPropSetXmlIO_BoolClose;
            } else if (type == kTypeUint32) {
                opening = addr::cGZPropSetXmlIO_Uint32Open;
                closing = addr::cGZPropSetXmlIO_Uint32Close;
            } else {
                // The game's EAX holds the type.
                last = line.Finish(type);
                break;
            }
            const char* const idText = Body(line.id);
            const uint32_t typeAgain = Virtual<uint32_t>(value, 0x10);
            CallCdecl<uint32_t>(addr::cRZBaseString_Sprintf, &line.start, Text(opening),
                                TextAt(addr::cGZPropSetXmlIO_KeyName), idText, TextAt(addr::cGZPropSetXmlIO_TypeName),
                                typeAgain);
            CallCdecl<uint32_t>(addr::cRZBaseString_Sprintf, &line.end, Text(closing));
            const uint32_t data = At<uint32_t>(value, 0x4);
            if (type == kTypeString)
                Virtual<uint32_t>(&line.data, 0x34, At<void*>(value, 0x4));
            else if (type == kTypeFloat32)
                CallCdecl<uint32_t>(addr::cRZBaseString_Sprintf, &line.data, Text(addr::SC4_FloatFormat), WidenX87(data));
            else if (type == kTypeBool)
                CallCdecl<uint32_t>(addr::cRZBaseString_Sprintf, &line.data, Text(addr::SC4_StringFormat),
                                    Text((data & 0xFF) ? addr::cGZPropSetXmlIO_TrueText : addr::SC4_FalseText));
            else
                CallCdecl<uint32_t>(addr::cRZBaseString_Sprintf, &line.data, Text(addr::SC4_IntFormat), data);
            last = WriteText(stream, line.indent);
            if (Yes(last))
                last = WriteText(stream, line.start);
            if (Yes(last))
                last = WriteText(stream, line.data);
            if (Yes(last))
                last = WriteText(stream, line.end);
            if (!Yes(last)) {
                last = line.Finish(last);
                break;
            }
            line.Finish(last);
            if (!Yes(Virtual<uint32_t>(set, 0x1C, &key, &value))) {
                TempString close;
                CallThis<void*>(addr::SC4String_SC4String, &close);
                CallCdecl<uint32_t>(addr::cRZBaseString_Sprintf, &close, Text(addr::cGZPropSetXmlIO_CloseRootFormat),
                                    Body(root));
                last = WriteText(stream, close);
                done = Yes(last);
                last = DestroyTemp(close, last);
                break;
            }
        }
    }
    last = DestroyTemp(open, last);
    last = DestroyTemp(root, last);
    return Answer(DestroyTemp(header, last), done);
}

// The game's 0x00942D87 (this is the handler, the object + 4). Before the
// root: the name must be the root's. After it: the `key` and `type`
// attributes kept (names compared with no case), the text emptied in place.
uint32_t __fastcall XmlStartElement(char* handler, void*, const char* name, const char* const* attributes, uint32_t,
                                    uint32_t)
{
    OPENSC4_FIRST("XmlStartElement");
    if (!At<uint8_t>(handler, 0xC)) {
        TempString root;
        CallThis<void*>(addr::SC4String_SC4String, &root);
        XmlRootName(handler - 0x4, nullptr, &root, &name);
        TempString given;
        char* const made = CallThis<char*>(addr::SC4String_FromText, &given, name);
        const uint32_t equal = CallCdecl<uint32_t>(addr::SC4UI_F43AB70, reinterpret_cast<char*>(&root) + 0x4,
                                                   made ? made + 0x4 : nullptr);
        uint32_t last = DestroyTemp(given, equal);
        if (!Yes(equal))
            return Answer(DestroyTemp(root, last), false);
        At<uint8_t>(handler, 0xC) = 1;
        return Answer(DestroyTemp(root, last), true);
    }
    for (const char* const* pair = attributes; pair && pair[0]; pair += 2) {
        TempString attribute;
        CallThis<void*>(addr::SC4String_FromText, &attribute, pair[0]);
        TempString text;
        CallThis<void*>(addr::SC4String_FromText, &text, pair[1]);
        TempString keyName;
        void* const keyMade = CallThis<void*>(addr::SC4String_FromText, &keyName, TextAt(addr::cGZPropSetXmlIO_KeyName));
        const uint32_t isKey = Virtual<uint32_t>(keyMade, 0x24, &attribute, uint32_t{1});
        DestroyTemp(keyName, isKey);
        if (Yes(isKey)) {
            Virtual<uint32_t>(handler + 0x10, 0x38, &text);
        } else {
            TempString typeName;
            void* const typeMade =
                CallThis<void*>(addr::SC4String_FromText, &typeName, TextAt(addr::cGZPropSetXmlIO_TypeName));
            const uint32_t isType = Virtual<uint32_t>(typeMade, 0x24, &attribute, uint32_t{1});
            DestroyTemp(typeName, isType);
            if (Yes(isType))
                Virtual<uint32_t>(handler + 0x24, 0x38, &text);
        }
        DestroyTemp(text, 0);
        DestroyTemp(attribute, 0);
    }
    // The text string at +0x38 emptied in place (its body at +0x3C, end +0x40).
    if (At<char*>(handler, 0x3C) != At<char*>(handler, 0x40)) {
        *At<char*>(handler, 0x3C) = 0;
        At<char*>(handler, 0x40) = At<char*>(handler, 0x3C);
    }
    return Answer(Bits(At<char*>(handler, 0x3C)), true);
}

// ====================================================================================
// Part u3: the UI script property handler's StartElement and EndElement

constexpr std::size_t kScriptSet = 0xC;
constexpr std::size_t kScriptMap = 0x10;
constexpr std::size_t kScriptText = 0x1C;

// The game's 0x00943809: the text emptied in place; each attribute pair put
// in the map (the `Name` and `Value` ones also copied into strings that are
// not used). `count` is the number of texts.
uint32_t __fastcall ScriptHandlerStartElement(char* self, void*, const char*, const char* const* attributes,
                                              uint32_t count, uint32_t)
{
    OPENSC4_FIRST("ScriptHandlerStartElement");
    if (At<char*>(self, kScriptText + 0x4) != At<char*>(self, kScriptText + 0x8)) {
        *At<char*>(self, kScriptText + 0x4) = 0;
        At<char*>(self, kScriptText + 0x8) = At<char*>(self, kScriptText + 0x4);
    }
    TempString name;
    CallThis<void*>(addr::SC4String_SC4String, &name);
    TempString value;
    uint32_t last = CallThis<uint32_t>(addr::SC4String_SC4String, &value);
    if (count != 0) {
        const char* const* pair = attributes;
        do {
            TempString attribute;
            CallThis<void*>(addr::SC4String_FromText, &attribute, pair[0]);
            TempString text;
            CallThis<void*>(addr::SC4String_FromText, &text, pair[1]);
            pair += 2;
            if (CallCdecl<int>(addr::SC4_strcmp, Data(&attribute), Text(addr::cGZUIScriptHandler_NameText)) == 0)
                CallThis<uint32_t>(addr::cRZBaseString_Assign, &name, &text);
            else if (CallCdecl<int>(addr::SC4_strcmp, Data(&attribute), Text(addr::cGZUIScriptHandler_ValueText)) == 0)
                CallThis<uint32_t>(addr::cRZBaseString_Assign, &value, &text);
            void* const slot = CallThis<void*>(addr::SC4StringMap_Index, self + kScriptMap, &attribute);
            last = CallThis<uint32_t>(addr::cRZBaseString_Assign, slot, &text);
            count -= 2;
            last = DestroyTemp(text, last);
            last = DestroyTemp(attribute, last);
        } while (count != 0);
    }
    last = DestroyTemp(value, last);
    return Answer(DestroyTemp(name, last), true);
}

// The key a property element names: the name's ID with the top byte set.
uint32_t ScriptKey(TempString& name)
{
    return CallCdecl<uint32_t>(addr::SC4_NameToId, Data(&name), uint32_t{0}) | 0xFF000000u;
}

// A number's text must not be empty and must start with a digit.
bool ScriptNumberText(TempString& value)
{
    const char* const text = Data(&value);
    return Strlen(&value) != 0 && CallCdecl<int>(addr::SC4_isdigit, static_cast<int>(static_cast<signed char>(*text)));
}

// The game's 0x009425A2: the element's `Name` and `Value` (the text when
// there is no `Value`) as a property of the type the element's name says,
// put in the set; then the map emptied.
uint32_t __fastcall ScriptHandlerEndElement(char* self, void*, const char* element)
{
    OPENSC4_FIRST("ScriptHandlerEndElement");
    TempString value;
    CallThis<void*>(addr::SC4String_Substring, &value, self + kScriptText, uint32_t{0}, uint32_t{0xFFFFFFFF});
    TempString name;
    uint32_t last = CallThis<uint32_t>(addr::SC4String_SC4String, &name);
    char* const map = self + kScriptMap;
    for (char* node = At<char*>(Head(map), 0x8); node != Head(map);
         node = CallCdecl<char*>(addr::cSC4Advisor_F40D4C0, node)) {
        if (CallCdecl<int>(addr::SC4_strcmp, Virtual<const char*>(node + 0x10, 0x14),
                           Text(addr::cGZUIScriptHandler_NameText)) == 0)
            CallThis<uint32_t>(addr::cRZBaseString_Assign, &name, node + 0x24);
        else if (CallCdecl<int>(addr::SC4_strcmp, Virtual<const char*>(node + 0x10, 0x14),
                                Text(addr::cGZUIScriptHandler_ValueText)) == 0)
            CallThis<uint32_t>(addr::cRZBaseString_Assign, &value, node + 0x24);
    }
    void* const set = At<void*>(self, kScriptSet);
    if (CallCdecl<int>(addr::SC4_strcmp, element, Text(addr::cGZUIScriptHandler_StringStringText)) == 0) {
        // Nothing.
    } else if (CallCdecl<int>(addr::SC4_strcmp, element, Text(addr::cGZUIScriptHandler_Uint32StringText)) == 0) {
        if (Strlen(&name) != 0 || Strlen(&value) != 0) {
            Value string;
            CallThis<void*>(addr::cGZPropertyString_ConstructFromIGZString, &string, &value);
            uint32_t key = ScriptKey(name);
            Virtual<uint32_t>(set, 0xC, &key, &string);
            last = CallThis<uint32_t>(addr::cGZPropertyString_Destruct, &string);
        }
    } else if (CallCdecl<int>(addr::SC4_strcmp, element, Text(addr::cGZUIScriptHandler_Uint32Uint32Text)) == 0) {
        if (!ScriptNumberText(value)) {
            // The game's EAX is the failed check's 0.
            last = DestroyTemp(name, 0);
            return Answer(DestroyTemp(value, last), false);
        }
        if (Strlen(&name) != 0 || Strlen(&value) != 0) {
            const uint32_t number = CallCdecl<uint32_t>(addr::SC4_TextToUint32, Data(&value));
            Value data{addr::cGZPropertyUint32_Vtable, number};
            uint32_t key = ScriptKey(name);
            last = Virtual<uint32_t>(set, 0xC, &key, &data);
        }
    } else if (CallCdecl<int>(addr::SC4_strcmp, element, Text(addr::cGZUIScriptHandler_Uint32Float32Text)) == 0) {
        if (!ScriptNumberText(value)) {
            // The game's EAX is the failed check's 0.
            last = DestroyTemp(name, 0);
            return Answer(DestroyTemp(value, last), false);
        }
        if (Strlen(&name) != 0 || Strlen(&value) != 0) {
            alignas(4) char number[0x14];
            CallThis<void*>(addr::SC4ScriptNumber_FromString, number, &value);
            const uint32_t bits = ThisToFloat(addr::SC4ScriptNumber_ToFloat, number);
            uint32_t key = ScriptKey(name);
            Value data{addr::cGZPropertyFloat32_Vtable, bits};
            Virtual<uint32_t>(set, 0xC, &key, &data);
            last = CallThis<uint32_t>(addr::SC4ScriptNumber_Destroy, number);
        }
    }
    last = CallThis<uint32_t>(addr::SC4StringMap_Clear, map);
    last = DestroyTemp(name, last);
    return Answer(DestroyTemp(value, last), true);
}

// ====================================================================================
// Part u3: the factory's Create

constexpr uint32_t kTypeTrackText2 = 0x0B9EB87E;
constexpr uint32_t kTypeTrackA = 0xEBFEE345;
constexpr uint32_t kTypeTrackB = 0xEBFEE349;
constexpr uint32_t kTypeWindow = 0xA3CD96CF;
constexpr uint32_t kTypeBag = 0x7B1ACFCD;

// The game's 0x00942F24 (this and the last two arguments unused): an object
// of the type with the key (type, 0, 0), held by a counted reference while
// it is asked for iid. A new Track object of the text form gets 0xFF4C97BE
// and its name `Track`.
uint32_t __fastcall FactoryCreate(char*, void*, uint32_t type, uint32_t iid, void** out, uint32_t, uint32_t)
{
    OPENSC4_FIRST("FactoryCreate");
    const uint32_t key[3] = {type, 0, 0};
    void* made = nullptr;
    if (type == kTypeTrackText || type == kTypeTrackText2) {
        void* const memory = CallCdecl<void*>(addr::SC4_operator_new, uint32_t{0x28});
        made = memory ? CallThis<void*>(addr::cGZPropSetTrack_ConstructText, memory, key) : nullptr;
    } else if (type == kTypeBag) {
        char* const memory = CallCdecl<char*>(addr::SC4_operator_new, uint32_t{0x28});
        made = memory ? BagConstruct(memory, nullptr, key) : nullptr;
    } else if (type == kTypeWindow) {
        char* const memory = CallCdecl<char*>(addr::SC4_operator_new, uint32_t{0x114});
        made = memory ? WindowConstruct(memory, nullptr, key) : nullptr;
    } else if (type == kTypeTrackA || type == kTypeTrackB) {
        void* const memory = CallCdecl<void*>(addr::SC4_operator_new, uint32_t{0x28});
        made = memory ? CallThis<void*>(addr::cGZPropSetTrack_Construct, memory, key) : nullptr;
    } else {
        return Answer(type, false);
    }
    void* held = nullptr;
    CallThis<void*>(addr::cGZDBSegmentPackedFile_CountedPointerConstruct, &held, made);
    if (type == kTypeTrackText || type == kTypeTrackText2) {
        uint32_t id = 0xFF4C97BE;
        Value number{addr::cGZPropertyUint32_Vtable, 0xAB903A1B};
        Virtual<uint32_t>(held, 0xC, &id, &number);
        number.vtable = addr::cGZPropertyValue_Vtable;
        uint32_t nameId = 0xFF1CDF6A;
        Value name;
        void* const string =
            CallThis<void*>(addr::cGZPropertyString_ConstructFromText, &name, Text(addr::cGZPropSetFactory_TrackText));
        Virtual<uint32_t>(held, 0xC, &nameId, string);
        CallThis<uint32_t>(addr::cGZPropertyString_Destruct, &name);
    }
    const uint32_t result = Virtual<uint32_t>(held, 0x0, iid, out);
    return AnswerByte(Virtual<uint32_t>(held, 0x8), result);
}

// ====================================================================================
// Part u3: the window's Clone and copy

// The game's 0x009436BF: the script window's part at +0xC8.
char* __fastcall ScriptWinPartAssign(char* self, void*, char* other)
{
    OPENSC4_FIRST("ScriptWinPartAssign");
    At<uint32_t>(self, 0x18) = At<uint32_t>(other, 0x18);
    At<uint32_t>(self, 0x1C) = At<uint32_t>(other, 0x1C);
    At<uint8_t>(self, 0x20) = At<uint8_t>(other, 0x20);
    if (other != self) {
        CallThis<void*>(addr::SC4RefPtr_Assign, self + 0x24, At<void*>(other, 0x24));
        CallThis<void*>(addr::GZRefPtr_Assign, self + 0x28, At<void*>(other, 0x28));
        CallThis<void*>(addr::GZRefPtr_Assign, self + 0x2C, At<void*>(other, 0x2C));
        CallThis<void*>(addr::SC4RefPtr_Assign, self + 0x30, At<void*>(other, 0x30));
    }
    CallThis<void*>(addr::GZRefPtrList_Assign, self + 0x34, other + 0x34);
    return self;
}

// The game's 0x00943112: the script window's members (only Clone copies a
// window). The counted references are assigned only when they are not
// the same member.
char* __fastcall ScriptWinAssign(char* self, void*, char* other)
{
    OPENSC4_FIRST("ScriptWinAssign");
    CallThis<uint32_t>(addr::cRZBaseString_Assign, self + 0x20, other + 0x20);
    if (other != self)
        CallThis<void*>(addr::SC4RefPtr_Assign, self + 0x34, At<void*>(other, 0x34));
    for (std::size_t offset = 0x38; offset <= 0x54; offset += 4)
        At<uint32_t>(self, offset) = At<uint32_t>(other, offset);
    At<uint8_t>(self, 0x58) = At<uint8_t>(other, 0x58);
    At<uint32_t>(self, 0x5C) = At<uint32_t>(other, 0x5C);
    At<uint8_t>(self, 0x60) = At<uint8_t>(other, 0x60);
    for (std::size_t offset = 0x64; offset <= 0x74; offset += 4)
        At<uint32_t>(self, offset) = At<uint32_t>(other, offset);
    CallThis<void*>(addr::GZValueList_Assign, self + 0x78, other + 0x78);
    if (other != self)
        for (std::size_t offset = 0x7C; offset <= 0x94; offset += 4)
            CallThis<void*>(addr::SC4RefPtr_Assign, self + offset, At<void*>(other, offset));
    CallThis<void*>(addr::SC4RefPtrList_Assign, self + 0x98, other + 0x98);
    At<uint8_t>(self, 0x9C) = At<uint8_t>(other, 0x9C);
    At<uint8_t>(self, 0x9D) = At<uint8_t>(other, 0x9D);
    At<uint8_t>(self, 0x9E) = At<uint8_t>(other, 0x9E);
    CallThis<void*>(addr::GZRefPtrVector_Assign, self + 0xA0, other + 0xA0);
    if (other != self) {
        CallThis<void*>(addr::GZRefPtr_Assign, self + 0xAC, At<void*>(other, 0xAC));
        CallThis<void*>(addr::GZRefPtr_Assign, self + 0xB0, At<void*>(other, 0xB0));
    }
    CallThis<void*>(addr::GZWinTree_Assign, self + 0xB4, other + 0xB4);
    At<uint32_t>(self, 0xC0) = At<uint32_t>(other, 0xC0);
    At<uint8_t>(self, 0xC4) = At<uint8_t>(other, 0xC4);
    ScriptWinPartAssign(self + 0xC8, nullptr, other + 0xC8);
    At<uint8_t>(self, 0x100) = At<uint8_t>(other, 0x100);
    return self;
}

// The game's 0x009430E1: the script window's members and the key.
char* __fastcall WindowCopyAssign(char* self, void*, char* other)
{
    OPENSC4_FIRST("WindowCopyAssign");
    ScriptWinAssign(self, nullptr, other);
    At<uint32_t>(self, kWindowKey + 0x4) = At<uint32_t>(other, kWindowKey + 0x4);
    At<uint32_t>(self, kWindowKey + 0x8) = At<uint32_t>(other, kWindowKey + 0x8);
    At<uint32_t>(self, kWindowKey + 0xC) = At<uint32_t>(other, kWindowKey + 0xC);
    return self;
}

// The game's 0x0094309C (this is the key interface): a new window with this
// key, a copy of this one, asked for iid.
uint32_t __fastcall WindowClone(char* key, void*, uint32_t iid, void** out)
{
    OPENSC4_FIRST("WindowClone");
    char* const memory = CallCdecl<char*>(addr::SC4_operator_new, uint32_t{0x114});
    char* const copy = memory ? WindowConstruct(memory, nullptr, reinterpret_cast<const uint32_t*>(key + 0x4)) : nullptr;
    WindowCopyAssign(copy, nullptr, key - kWindowKey);
    return Virtual<uint32_t>(copy, 0x0, iid, out);
}

// ====================================================================================
// Part u3: the shuffle bag (vtable 0x00AD4648)
//
// 0x18 bytes: the count at +4, two lists of uint32 (+8, the entries not yet
// drawn; +0xC, the ones drawn), the threshold at +0x10 and at +0x14 whether
// it is fixed. A draw takes a random entry of the first list and puts it at
// the back of the second; when the second holds more than the threshold,
// its oldest go back to the first.

constexpr uint32_t kIID_ShuffleBag = 0xDB1ACFB2;
constexpr std::size_t kBagFirst = 0x8;
constexpr std::size_t kBagSecond = 0xC;
constexpr std::size_t kBagThreshold = 0x10;
constexpr std::size_t kBagFixed = 0x14;

uint32_t& LiveBags() { return *reinterpret_cast<uint32_t*>(addr::cGZShuffleBag_LiveCount); }

// The game's 0x009439CF.
char* __fastcall ShuffleBagConstruct(char* self, void*)
{
    OPENSC4_FIRST("ShuffleBagConstruct");
    At<uint32_t>(self, 0x4) = 0;
    uint8_t allocator = 0;
    At<std::uintptr_t>(self, 0) = addr::cGZShuffleBag_Vtable;
    CallThis<void*>(addr::SC4RefList_Construct, self + kBagFirst, &allocator);
    CallThis<void*>(addr::SC4RefList_Construct, self + kBagSecond, &allocator);
    At<uint32_t>(self, kBagThreshold) = 10;
    At<uint8_t>(self, kBagFixed) = 0;
    ++LiveBags();
    return self;
}

// The game's 0x00943A0E.
uint32_t __fastcall ShuffleBagDestruct(char* self, void*)
{
    OPENSC4_FIRST("ShuffleBagDestruct");
    At<std::uintptr_t>(self, 0) = addr::cGZShuffleBag_Vtable;
    --LiveBags();
    CallThis<uint32_t>(addr::SC4PairList_Destroy, self + kBagSecond);
    return CallThis<uint32_t>(addr::SC4PairList_Destroy, self + kBagFirst);
}

// The game's 0x00943BE5.
char* __fastcall ShuffleBagDeletingDestructor(char* self, void*, uint32_t flags)
{
    OPENSC4_FIRST("ShuffleBagDeletingDestructor");
    ShuffleBagDestruct(self, nullptr);
    if (flags & 1)
        CallCdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// The game's 0x0094392E.
uint32_t __fastcall ShuffleBagQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    OPENSC4_FIRST("ShuffleBagQueryInterface");
    if (iid != kIID_cIGZUnknown && iid != kIID_ShuffleBag)
        return 0;
    *out = self;
    return Answer(Virtual<uint32_t>(self, 0x4), true);
}

// The game's 0x00943917: at a count of 1 or less the object is deleted
// without lowering it.
uint32_t __fastcall ShuffleBagRelease(char* self, void*)
{
    OPENSC4_FIRST("ShuffleBagRelease");
    const int32_t count = At<int32_t>(self, 0x4);
    if (count <= 1) {
        Virtual<void*>(self, 0x44, uint32_t{1});
        return 0;
    }
    At<int32_t>(self, 0x4) = count - 1;
    return static_cast<uint32_t>(count - 1);
}

// Without a fixed threshold, half of both lists' sizes; EAX the second
// list's head, as the game's count of it leaves it.
uint32_t Rethreshold(char* self, uint32_t eax)
{
    if (At<uint8_t>(self, kBagFixed))
        return eax;
    const uint32_t first = Size(self + kBagFirst);
    const uint32_t second = Size(self + kBagSecond);
    At<uint32_t>(self, kBagThreshold) = (second + first) >> 1;
    return Bits(Head(self + kBagSecond));
}

// The game's 0x00943C01.
uint32_t __fastcall ShuffleBagAdd(char* self, void*, uint32_t value)
{
    OPENSC4_FIRST("ShuffleBagAdd");
    const uint32_t pushed = CallThis<uint32_t>(addr::SC4StlList_PushBack, self + kBagFirst, &value);
    return Answer(Rethreshold(self, pushed), true);
}

// The game's 0x00943A2E: every entry equal to value, from both lists.
uint32_t __fastcall ShuffleBagRemove(char* self, void*, uint32_t value)
{
    OPENSC4_FIRST("ShuffleBagRemove");
    CallThis<uint32_t>(addr::SC4Uint32List_Remove, self + kBagFirst, &value);
    const uint32_t head = CallThis<uint32_t>(addr::SC4Uint32List_Remove, self + kBagSecond, &value);
    return Answer(Rethreshold(self, head), true);
}

// The game's 0x0094397E.
uint32_t __fastcall ShuffleBagClear(char* self, void*)
{
    OPENSC4_FIRST("ShuffleBagClear");
    CallThis<uint32_t>(addr::SC4StlList_Clear, self + kBagFirst);
    return Answer(CallThis<uint32_t>(addr::SC4StlList_Clear, self + kBagSecond), true);
}

// The game's 0x00943953 (this unused): a number from 0 below count, from the
// registry of sound kinds.
uint32_t __fastcall ShuffleBagRandom(char*, void*, uint32_t count)
{
    OPENSC4_FIRST("ShuffleBagRandom");
    void* registry = nullptr;
    CallThis<void*>(addr::SC4Audio_F4ED100, &registry);
    const uint32_t number = Virtual<uint32_t>(registry, 0x68, count, uint32_t{0});
    CallThis<void>(addr::SC4Audio_F4ED130, &registry);
    return number;
}

// The front of a list moved to the back of another (or the same) list.
uint32_t MoveFront(char* from, char* to)
{
    uint32_t value = At<uint32_t>(Next(Head(from)), 0x8);
    CallThis<void>(addr::SC4StlList_PopFront, from);
    CallThis<void>(addr::SC4StlList_PushBack, to, &value);
    return value;
}

// The game's 0x00943C46.
uint32_t __fastcall ShuffleBagDraw(char* self, void*)
{
    OPENSC4_FIRST("ShuffleBagDraw");
    char* const first = self + kBagFirst;
    char* const second = self + kBagSecond;
    if (Next(Head(first)) == Head(first))
        return MoveFront(second, second);
    uint32_t steps = ShuffleBagRandom(self, nullptr, Size(first));
    char* node = Next(Head(first));
    for (; steps != 0; --steps)
        node = Next(node);
    uint32_t drawn = At<uint32_t>(node, 0x8);
    void* after = nullptr;
    CallThis<void>(addr::SC4StdList_Erase, first, &after, node);
    CallThis<void>(addr::SC4StlList_PushBack, second, &drawn);
    while (static_cast<int32_t>(Size(second)) > At<int32_t>(self, kBagThreshold))
        MoveFront(second, first);
    return drawn;
}

// The node at index, counting the first list then the second; null past
// the end, with the total in `total`.
struct Place {
    char* list;
    char* node;
    uint32_t total;
};

Place Locate(char* self, uint32_t index)
{
    char* const first = self + kBagFirst;
    char* const second = self + kBagSecond;
    const uint32_t inFirst = Size(first);
    if (inFirst != 0 && index < inFirst) {
        char* node = Next(Head(first));
        for (uint32_t i = index; i != 0; --i)
            node = Next(node);
        return {first, node, 0};
    }
    const uint32_t total = Virtual<uint32_t>(self, 0x24);
    if (index >= total)
        return {nullptr, nullptr, total};
    char* node = Next(Head(second));
    for (uint32_t i = index - inFirst; i != 0; --i)
        node = Next(node);
    return {second, node, total};
}

// The game's 0x00943A9B.
uint32_t __fastcall ShuffleBagGet(char* self, void*, uint32_t index, uint32_t* out)
{
    OPENSC4_FIRST("ShuffleBagGet");
    const Place place = Locate(self, index);
    if (!place.node)
        return Answer(place.total, false);
    *out = At<uint32_t>(place.node, 0x8);
    return Answer(*out, true);
}

// The game's 0x00943B07.
uint32_t __fastcall ShuffleBagSet(char* self, void*, uint32_t index, uint32_t value)
{
    OPENSC4_FIRST("ShuffleBagSet");
    const Place place = Locate(self, index);
    if (!place.node)
        return Answer(place.total, false);
    At<uint32_t>(place.node, 0x8) = value;
    return Answer(Bits(place.node), true);
}

// The game's 0x00943B71.
uint32_t __fastcall ShuffleBagErase(char* self, void*, uint32_t index)
{
    OPENSC4_FIRST("ShuffleBagErase");
    const Place place = Locate(self, index);
    if (!place.node)
        return Answer(place.total, false);
    void* after = nullptr;
    return Answer(CallThis<uint32_t>(addr::SC4StdList_Erase, place.list, &after, place.node), true);
}

// The game's 0x00943CF2: 1, the count, then each list's entries (each list
// turned round once as it goes). The writes' results are not looked at.
uint32_t __fastcall ShuffleBagWrite(char* self, void*, void* stream, uint32_t, uint32_t)
{
    OPENSC4_FIRST("ShuffleBagWrite");
    Virtual<uint32_t>(stream, 0x24, uint32_t{1});
    char* const first = self + kBagFirst;
    char* const second = self + kBagSecond;
    const uint32_t inFirst = Size(first);
    const uint32_t inSecond = Size(second);
    Virtual<uint32_t>(stream, 0x24, inSecond + inFirst);
    for (uint32_t i = 0; i < inFirst; ++i)
        Virtual<uint32_t>(stream, 0x24, MoveFront(first, first));
    uint32_t last = inSecond;
    for (uint32_t i = 0; i < inSecond; ++i)
        last = Virtual<uint32_t>(stream, 0x24, MoveFront(second, second));
    return Answer(last, true);
}

// Entries of the text form: `n` or `from-to` (each number of it).
void AddTextEntry(char* list, TempString& field)
{
    TempString piece;
    void* const split = CallThis<void*>(addr::SC4String_SplitOff, &field, &piece, Text(addr::SC4_DashText));
    TempString from;
    CallThis<void*>(addr::SC4String_Substring, &from, split, uint32_t{0}, uint32_t{0xFFFFFFFF});
    DestroyTemp(piece, 0);
    if (Strlen(&from) != 0) {
        uint32_t number = CallCdecl<uint32_t>(addr::SC4_TextToUint32, Data(&from));
        const uint32_t to = CallCdecl<uint32_t>(addr::SC4_TextToUint32, Data(&field));
        if (number <= to) {
            do {
                uint32_t entry = number;
                CallThis<void>(addr::SC4StlList_PushBack, list, &entry);
                ++number;
            } while (number <= to);
        }
    } else {
        uint32_t entry = CallCdecl<uint32_t>(addr::SC4_TextToUint32, Data(&field));
        CallThis<void>(addr::SC4StlList_PushBack, list, &entry);
    }
    DestroyTemp(from, 0);
}

// The game's 0x00943DBC: the stream's bytes whole; by their size a text of
// entries (the first dword the text's length), version 1 with a count, or a
// count; the entries go to the first list and the threshold becomes a third
// of its size plus one.
uint32_t __fastcall ShuffleBagRead(char* self, void*, void* stream, uint32_t, uint32_t)
{
    OPENSC4_FIRST("ShuffleBagRead");
    void* seekable = nullptr;
    // Without the seekable interface the game takes the stream's address as
    // the size (and then calls through the null interface).
    const uint32_t size = Yes(Virtual<uint32_t>(stream, 0x0, kIID_Seekable, &seekable))
                              ? Virtual<uint32_t>(seekable, 0xD0)
                              : Bits(stream);
    uint32_t* const words = CallCdecl<uint32_t*>(addr::SC4_operator_new, size + 1);
    Virtual<uint32_t>(seekable, 0x6C, words, size);
    const uint32_t head = words[0];
    int form = -1;
    uint32_t count = 0;
    if (size == head + 4) {
        form = 0;
    } else if (head == 1 && size == words[1] * 4 + 8) {
        form = 1;
        count = words[1];
    } else if (size == head * 4 + 4) {
        form = 2;
        count = head;
    }
    char* const first = self + kBagFirst;
    if (form < 0) {
        const uint32_t freed = CallCdecl<uint32_t>(addr::SC4_operator_delete, words);
        return Answer(ReleaseIf(seekable, freed), false);
    }
    if (form == 0) {
        reinterpret_cast<char*>(words)[size] = 0;
        TempString rest;
        CallThis<void*>(addr::SC4String_FromText, &rest, reinterpret_cast<const char*>(words + 1));
        while (Strlen(&rest) != 0) {
            TempString piece;
            void* const split = CallThis<void*>(addr::SC4String_SplitOff, &rest, &piece, Text(addr::SC4_CommaText));
            TempString field;
            CallThis<void*>(addr::SC4String_Substring, &field, split, uint32_t{0}, uint32_t{0xFFFFFFFF});
            DestroyTemp(piece, 0);
            if (Strlen(&field) == 0) {
                // No comma left: the field is the rest.
                CallThis<uint32_t>(addr::cRZBaseString_Assign, &field, &rest);
                TempString empty;
                void* const none = CallThis<void*>(addr::SC4String_FromText, &empty, Text(addr::MySim_ScriptEmpty));
                CallThis<uint32_t>(addr::cRZBaseString_Assign, &rest, none);
                DestroyTemp(empty, 0);
            }
            AddTextEntry(first, field);
            DestroyTemp(field, 0);
        }
        DestroyTemp(rest, 0);
    } else {
        const uint32_t* entry = words + (form == 1 ? 2 : 1);
        for (uint32_t i = count; i != 0; --i) {
            uint32_t value = *entry++;
            CallThis<void>(addr::SC4StlList_PushBack, first, &value);
        }
    }
    Virtual<uint32_t>(self, 0x1C, Size(first) / 3 + 1);
    const uint32_t freed = CallCdecl<uint32_t>(addr::SC4_operator_delete, words);
    return Answer(ReleaseIf(seekable, freed), true);
}

// ====================================================================================
// Part u3: the framework's own 2D and 3D sound kinds (vtables 0x00AD2804
// and 0x00AD2830), which the game's SC4K kinds replace (docs/audio.md).
// Value sets: slot 3 Set(number, value, flag), slot 4 Get(number).

constexpr uint32_t kKindPriority = 0xFFAE9DDB;

uint32_t GetValue(void* values, uint32_t number) { return Virtual<uint32_t>(values, 0x10, number); }
uint32_t SetValue(void* values, uint32_t number, uint32_t value, uint32_t flag)
{
    return Virtual<uint32_t>(values, 0xC, number, value, flag);
}
uint32_t CopyValue(void* from, void* to, uint32_t number) { return SetValue(to, number, GetValue(from, number), 1); }

// 0x15 * 0x14 / 1024 (signed, as IMUL and IDIV do it), halved when the
// registry has a value 0xFFAE9DDB and the source's is below it.
int32_t VoiceLevel(int32_t a, int32_t b, void* source)
{
    int32_t level = static_cast<int32_t>(static_cast<uint32_t>(a) * static_cast<uint32_t>(b)) / 0x400;
    void* const registry = CallCdecl<void*>(addr::SC4_SoundKindRegistry);
    const int32_t limit = Virtual<int32_t>(registry, 0x7C, kKindPriority);
    if (limit != 0 && static_cast<int32_t>(GetValue(source, kKindPriority)) < limit)
        level /= 2;
    return level;
}

// The game's 0x00944088 (this unused).
uint32_t __fastcall Kind2DSetUpVoice(char*, void*, void* source, void* voice)
{
    OPENSC4_FIRST("Kind2DSetUpVoice");
    const uint32_t v16 = GetValue(source, 0x16);
    const int32_t v15 = static_cast<int32_t>(GetValue(source, 0x15));
    const int32_t v14 = static_cast<int32_t>(GetValue(source, 0x14));
    const int32_t level = VoiceLevel(v14, v15, source);
    SetValue(voice, 0x16, v16, 1);
    return Answer(SetValue(voice, 0x15, static_cast<uint32_t>(level), 0), true);
}

// The game's 0x00944117 (this unused).
uint32_t __fastcall Kind2DCopyValues(char*, void*, void* from, void* to)
{
    OPENSC4_FIRST("Kind2DCopyValues");
    uint32_t last = 0;
    for (const uint32_t number : {0x13u, 0x19u, 0x1Au, 0x1Bu, 0x23u, 0x24u, 0x25u, 0x26u, 0x33u})
        last = CopyValue(from, to, number);
    return Answer(last, true);
}

// The game's 0x009441E3 (this unused): nothing for the same object.
uint32_t __fastcall Kind3DSetUpVoice(char*, void*, void* source, void* voice)
{
    OPENSC4_FIRST("Kind3DSetUpVoice");
    if (voice == source)
        return 0;
    for (const uint32_t number : {0x20u, 0x21u, 0x22u, 0x40u, 0x41u, 0x42u})
        CopyValue(source, voice, number);
    if (GetValue(source, 0x1F) != 0 && GetValue(source, 0x1F) != 0) {
        CopyValue(source, voice, 0x1E);
        CopyValue(source, voice, 0x1F);
    } else {
        uint32_t base = GetValue(source, 0x38);
        if (base == 0)
            base = 4;
        SetValue(voice, 0x1E, base, 1);
        SetValue(voice, 0x1F, base * 500, 1);
    }
    const int32_t v15 = static_cast<int32_t>(GetValue(source, 0x15));
    const int32_t v14 = static_cast<int32_t>(GetValue(source, 0x14));
    const int32_t level = VoiceLevel(v14, v15, source);
    return SetValue(voice, 0x15, static_cast<uint32_t>(level), 0);
}

// The game's 0x00944351 (this unused).
uint32_t __fastcall Kind3DCopyValues(char*, void*, void* from, void* to)
{
    OPENSC4_FIRST("Kind3DCopyValues");
    uint32_t last = SetValue(to, 0x1C, 1, 1);
    for (const uint32_t number :
         {0x1Du, 0x13u, 0x19u, 0x1Au, 0x1Bu, 0x23u, 0x24u, 0x25u, 0x26u, 0x33u, 0x1Eu, 0x1Fu})
        last = CopyValue(from, to, number);
    return Answer(last, true);
}

// ====================================================================================
// Thunks and installation

OPENSC4_THUNK(BinaryDeletingDestructorThunk, BinaryDeletingDestructor, 4)
OPENSC4_THUNK(BinaryReadThunk, BinaryRead, 8)
OPENSC4_THUNK(BinaryWriteThunk, BinaryWrite, 12)
OPENSC4_THUNK(ReadSetThunk, ReadSet, 8)
OPENSC4_THUNK(WriteSetThunk, WriteSet, 12)

OPENSC4_THUNK(XmlConstructThunk, XmlConstruct, 0)
OPENSC4_THUNK(XmlQueryInterfaceThunk, XmlQueryInterface, 8)
OPENSC4_THUNK(XmlReleaseThunk, XmlRelease, 0)
OPENSC4_THUNK(XmlReadThunk, XmlRead, 8)
OPENSC4_THUNK(XmlEndElementThunk, XmlEndElement, 4)
OPENSC4_THUNK(XmlCharactersThunk, XmlCharacters, 8)
OPENSC4_THUNK(XmlDestructThunk, XmlDestruct, 0)
OPENSC4_THUNK(XmlDeletingDestructorThunk, XmlDeletingDestructor, 4)
OPENSC4_THUNK(ScriptHandlerSetTextThunk, ScriptHandlerSetText, 8)

OPENSC4_THUNK(FactoryConstructThunk, FactoryConstruct, 0)
OPENSC4_THUNK(FactoryCreateFromResourceThunk, FactoryCreateFromResource, 20)
OPENSC4_THUNK(FactoryWriteToStreamThunk, FactoryWriteToStream, 8)

OPENSC4_THUNK(TrackReadResourceThunk, TrackReadResource, 12)
OPENSC4_THUNK(TrackWriteXmlThunk, TrackWriteXml, 12)
OPENSC4_THUNK(TrackWriteBinaryThunk, TrackWriteBinary, 12)
OPENSC4_THUNK(TrackReadSetThunk, TrackReadSet, 12)
OPENSC4_THUNK(TrackWriteTextThunk, TrackWriteText, 12)
OPENSC4_THUNK(TrackReadTextThunk, TrackReadText, 12)
OPENSC4_THUNK(KeyGetKeyThunk, KeyGetKey, 4)
OPENSC4_THUNK(KeySetTypeThunk, KeySetType, 4)
OPENSC4_THUNK(KeySetInstanceThunk, KeySetInstance, 4)

OPENSC4_THUNK(WindowConstructThunk, WindowConstruct, 4)
OPENSC4_THUNK(WindowQueryInterfaceThunk, WindowQueryInterface, 8)
OPENSC4_THUNK(WindowDestructThunk, WindowDestruct, 0)
OPENSC4_THUNK(WindowDeletingDestructorThunk, WindowDeletingDestructor, 4)

OPENSC4_THUNK(BagConstructThunk, BagConstruct, 4)
OPENSC4_THUNK(BagSetThresholdThunk, BagSetThreshold, 4)
OPENSC4_THUNK(BagQueryInterfaceThunk, BagQueryInterface, 8)
OPENSC4_THUNK(BagDeletingDestructorThunk, BagDeletingDestructor, 4)
OPENSC4_THUNK(BagCountThunk, BagCount, 0)
OPENSC4_THUNK(BagAssignThunk, BagAssign, 4)
OPENSC4_THUNK(BagCopyAssignThunk, BagCopyAssign, 4)
OPENSC4_THUNK(BagCloneThunk, BagClone, 8)

OPENSC4_THUNK(XmlWriteThunk, XmlWrite, 12)
OPENSC4_THUNK(XmlRootNameThunk, XmlRootName, 8)
OPENSC4_THUNK(XmlStartElementThunk, XmlStartElement, 16)

OPENSC4_THUNK(ScriptHandlerStartElementThunk, ScriptHandlerStartElement, 16)
OPENSC4_THUNK(ScriptHandlerEndElementThunk, ScriptHandlerEndElement, 4)

OPENSC4_THUNK(FactoryCreateThunk, FactoryCreate, 20)

OPENSC4_THUNK(WindowCloneThunk, WindowClone, 8)
OPENSC4_THUNK(WindowCopyAssignThunk, WindowCopyAssign, 4)
OPENSC4_THUNK(ScriptWinAssignThunk, ScriptWinAssign, 4)
OPENSC4_THUNK(ScriptWinPartAssignThunk, ScriptWinPartAssign, 4)

OPENSC4_THUNK(ShuffleBagConstructThunk, ShuffleBagConstruct, 0)
OPENSC4_THUNK(ShuffleBagDestructThunk, ShuffleBagDestruct, 0)
OPENSC4_THUNK(ShuffleBagDeletingDestructorThunk, ShuffleBagDeletingDestructor, 4)
OPENSC4_THUNK(ShuffleBagQueryInterfaceThunk, ShuffleBagQueryInterface, 8)
OPENSC4_THUNK(ShuffleBagReleaseThunk, ShuffleBagRelease, 0)
OPENSC4_THUNK(ShuffleBagAddThunk, ShuffleBagAdd, 4)
OPENSC4_THUNK(ShuffleBagRemoveThunk, ShuffleBagRemove, 4)
OPENSC4_THUNK(ShuffleBagClearThunk, ShuffleBagClear, 0)
OPENSC4_THUNK(ShuffleBagRandomThunk, ShuffleBagRandom, 4)
OPENSC4_THUNK(ShuffleBagDrawThunk, ShuffleBagDraw, 0)
OPENSC4_THUNK(ShuffleBagGetThunk, ShuffleBagGet, 8)
OPENSC4_THUNK(ShuffleBagSetThunk, ShuffleBagSet, 8)
OPENSC4_THUNK(ShuffleBagEraseThunk, ShuffleBagErase, 4)
OPENSC4_THUNK(ShuffleBagWriteThunk, ShuffleBagWrite, 12)
OPENSC4_THUNK(ShuffleBagReadThunk, ShuffleBagRead, 12)

OPENSC4_THUNK(Kind2DSetUpVoiceThunk, Kind2DSetUpVoice, 8)
OPENSC4_THUNK(Kind2DCopyValuesThunk, Kind2DCopyValues, 8)
OPENSC4_THUNK(Kind3DSetUpVoiceThunk, Kind3DSetUpVoice, 8)
OPENSC4_THUNK(Kind3DCopyValuesThunk, Kind3DCopyValues, 8)

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

bool InstallBinary(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZPropSetBinaryIO_DeletingDestructor, &BinaryDeletingDestructorThunk},
        {addr::cGZPropSetBinaryIO_Read, &BinaryReadThunk},
        {addr::cGZPropSetBinaryIO_Write, &BinaryWriteThunk},
        {addr::cGZPropSetIO_ReadSet, &ReadSetThunk},
        {addr::cGZPropSetIO_WriteSet, &WriteSetThunk},
    };
    return InstallAll(detours);
}

bool InstallXml(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZPropSetXmlIO_Construct, &XmlConstructThunk},
        {addr::cGZPropSetXmlIO_QueryInterface, &XmlQueryInterfaceThunk},
        {addr::cGZPropSetXmlIO_Release, &XmlReleaseThunk},
        {addr::cGZPropSetXmlIO_Read, &XmlReadThunk},
        {addr::cGZPropSetXmlIO_EndElement, &XmlEndElementThunk},
        {addr::cGZPropSetXmlIO_Characters, &XmlCharactersThunk},
        {addr::cGZPropSetXmlIO_Destruct, &XmlDestructThunk},
        {addr::cGZPropSetXmlIO_DeletingDestructor, &XmlDeletingDestructorThunk},
        {addr::cGZUIScriptHandler_SetText, &ScriptHandlerSetTextThunk},
    };
    return InstallAll(detours);
}

bool InstallFactory(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZPropSetFactory_Construct, &FactoryConstructThunk},
        {addr::cGZPropSetFactory_CreateFromResource, &FactoryCreateFromResourceThunk},
        {addr::cGZPropSetFactory_WriteToStream, &FactoryWriteToStreamThunk},
    };
    return InstallAll(detours);
}

bool InstallTrack(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZPropSetTrack_ReadResource, &TrackReadResourceThunk},
        {addr::cGZPropSetTrack_WriteXml, &TrackWriteXmlThunk},
        {addr::cGZPropSetTrack_WriteBinary, &TrackWriteBinaryThunk},
        {addr::cGZPropSetTrack_ReadSet, &TrackReadSetThunk},
        {addr::cGZResourceKey_GetKey, &KeyGetKeyThunk},
        {addr::cGZResourceKey_SetType, &KeySetTypeThunk},
        {addr::cGZResourceKey_SetInstance, &KeySetInstanceThunk},
    };
    return InstallAll(detours);
}

bool InstallTkdt(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZPropSetTrack_WriteText, &TrackWriteTextThunk},
        {addr::cGZPropSetTrack_ReadText, &TrackReadTextThunk},
    };
    return InstallAll(detours);
}

bool InstallWindow(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZKeyedWin_Construct, &WindowConstructThunk},
        {addr::cGZKeyedWin_QueryInterface, &WindowQueryInterfaceThunk},
        {addr::cGZKeyedWin_Destruct, &WindowDestructThunk},
        {addr::cGZKeyedWin_DeletingDestructor, &WindowDeletingDestructorThunk},
    };
    return InstallAll(detours);
}

bool InstallBag(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZKeyedShuffleBag_Construct, &BagConstructThunk},
        {addr::cGZShuffleBag_SetThreshold, &BagSetThresholdThunk},
        {addr::cGZKeyedShuffleBag_QueryInterface, &BagQueryInterfaceThunk},
        {addr::cGZKeyedShuffleBag_DeletingDestructor, &BagDeletingDestructorThunk},
        {addr::cGZShuffleBag_Count, &BagCountThunk},
        {addr::cGZShuffleBag_Assign, &BagAssignThunk},
        {addr::cGZKeyedShuffleBag_CopyAssign, &BagCopyAssignThunk},
        {addr::cGZKeyedShuffleBag_Clone, &BagCloneThunk},
    };
    return InstallAll(detours);
}

bool InstallXmlText(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZPropSetXmlIO_Write, &XmlWriteThunk},
        {addr::cGZPropSetXmlIO_RootName, &XmlRootNameThunk},
        {addr::cGZPropSetXmlIO_StartElement, &XmlStartElementThunk},
    };
    return InstallAll(detours);
}

bool InstallScript(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZUIScriptHandler_StartElement, &ScriptHandlerStartElementThunk},
        {addr::cGZUIScriptHandler_EndElement, &ScriptHandlerEndElementThunk},
    };
    return InstallAll(detours);
}

bool InstallCreate(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZPropSetFactory_Create, &FactoryCreateThunk},
    };
    return InstallAll(detours);
}

bool InstallWinCopy(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZKeyedWin_Clone, &WindowCloneThunk},
        {addr::cGZKeyedWin_CopyAssign, &WindowCopyAssignThunk},
        {addr::cGZScriptWin_Assign, &ScriptWinAssignThunk},
        {addr::cGZScriptWinPart_Assign, &ScriptWinPartAssignThunk},
    };
    return InstallAll(detours);
}

bool InstallShuffleBag(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZShuffleBag_Construct, &ShuffleBagConstructThunk},
        {addr::cGZShuffleBag_Destruct, &ShuffleBagDestructThunk},
        {addr::cGZShuffleBag_DeletingDestructor, &ShuffleBagDeletingDestructorThunk},
        {addr::cGZShuffleBag_QueryInterface, &ShuffleBagQueryInterfaceThunk},
        {addr::cGZShuffleBag_Release, &ShuffleBagReleaseThunk},
        {addr::cGZShuffleBag_Add, &ShuffleBagAddThunk},
        {addr::cGZShuffleBag_Remove, &ShuffleBagRemoveThunk},
        {addr::cGZShuffleBag_Clear, &ShuffleBagClearThunk},
        {addr::cGZShuffleBag_Random, &ShuffleBagRandomThunk},
        {addr::cGZShuffleBag_Draw, &ShuffleBagDrawThunk},
        {addr::cGZShuffleBag_Get, &ShuffleBagGetThunk},
        {addr::cGZShuffleBag_Set, &ShuffleBagSetThunk},
        {addr::cGZShuffleBag_Erase, &ShuffleBagEraseThunk},
        {addr::cGZShuffleBag_Write, &ShuffleBagWriteThunk},
        {addr::cGZShuffleBag_Read, &ShuffleBagReadThunk},
    };
    return InstallAll(detours);
}

bool InstallKinds(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZSoundKind2D_SetUpVoice, &Kind2DSetUpVoiceThunk},
        {addr::cGZSoundKind2D_CopyValues, &Kind2DCopyValuesThunk},
        {addr::cGZSoundKind3D_SetUpVoice, &Kind3DSetUpVoiceThunk},
        {addr::cGZSoundKind3D_CopyValues, &Kind3DCopyValuesThunk},
    };
    return InstallAll(detours);
}

const bool g_registered[] = {
    registry::Register({"framework.propio.binary", "framework/propio", registry::Kind::Detour, registry::Phase::Attach,
                        addr::cGZPropSetBinaryIO_Read,
                        "Our binary property-set reader-writer (Read, Write, the deleting destructor) and ReadSet and "
                        "WriteSet, which pick a binary or an XML one (5 detours; part u2, docs/gzui.md)",
                        false, InstallBinary}),
    registry::Register({"framework.propio.xml", "framework/propio", registry::Kind::Detour, registry::Phase::Attach,
                        addr::cGZPropSetXmlIO_Read,
                        "Our XML property-set reader-writer: the constructor, QueryInterface, Release, Read, the "
                        "handler's EndElement and Characters, the destructors; and a UI script handler's SetText (9 "
                        "detours; part u2, docs/gzui.md)",
                        false, InstallXml}),
    registry::Register({"framework.propio.factory", "framework/propio", registry::Kind::Detour,
                        registry::Phase::Attach, addr::cGZPropSetFactory_CreateFromResource,
                        "Our property-set factory's constructor, CreateFromResource and WriteToStream (3 detours; part "
                        "u2, docs/gzui.md)",
                        false, InstallFactory}),
    registry::Register({"framework.propio.track", "framework/propio", registry::Kind::Detour, registry::Phase::Attach,
                        addr::cGZPropSetTrack_ReadResource,
                        "Our Track objects' reads and writes of their set and the key interface's GetKey, SetType and "
                        "SetInstance (7 detours; part u2, docs/gzui.md)",
                        false, InstallTrack}),
    registry::Register({"framework.propio.tkdt", "framework/propio", registry::Kind::Detour, registry::Phase::Attach,
                        addr::cGZPropSetTrack_ReadText,
                        "Our Track objects' TKDT text form: the writer and the reader (2 detours; part u2b, "
                        "docs/gzui.md)",
                        false, InstallTkdt}),
    registry::Register({"framework.propio.window", "framework/propio", registry::Kind::Detour, registry::Phase::Attach,
                        addr::cGZKeyedWin_Construct,
                        "Our window of class 0xA3CD96CF: the constructor, QueryInterface and the destructors (4 "
                        "detours; part u2, docs/gzui.md)",
                        false, InstallWindow}),
    registry::Register({"framework.propio.bag", "framework/propio", registry::Kind::Detour, registry::Phase::Attach,
                        addr::cGZKeyedShuffleBag_Construct,
                        "Our keyed shuffle bag 0x7B1ACFCD (constructor, QueryInterface, deleting destructor, Clone, "
                        "CopyAssign) and the bag's SetThreshold, Count and Assign (8 detours; part u2, docs/gzui.md)",
                        false, InstallBag}),
    registry::Register({"framework.propio.xmltext", "framework/propio", registry::Kind::Detour,
                        registry::Phase::Attach, addr::cGZPropSetXmlIO_Write,
                        "Our XML property-set writer, its root name and the XML handler's StartElement (3 detours; "
                        "part u3, docs/gzui.md)",
                        false, InstallXmlText}),
    registry::Register({"framework.propio.script", "framework/propio", registry::Kind::Detour, registry::Phase::Attach,
                        addr::cGZUIScriptHandler_EndElement,
                        "Our UI script property handler's StartElement and EndElement (2 detours; part u3, "
                        "docs/gzui.md)",
                        false, InstallScript}),
    registry::Register({"framework.propio.create", "framework/propio", registry::Kind::Detour, registry::Phase::Attach,
                        addr::cGZPropSetFactory_Create,
                        "Our property-set factory's Create (1 detour; part u3, docs/gzui.md)", false, InstallCreate}),
    registry::Register({"framework.propio.wincopy", "framework/propio", registry::Kind::Detour,
                        registry::Phase::Attach, addr::cGZKeyedWin_Clone,
                        "Our window 0xA3CD96CF's Clone and CopyAssign and the script window's Assign (4 detours; part "
                        "u3, docs/gzui.md)",
                        false, InstallWinCopy}),
    registry::Register({"framework.propio.shufflebag", "framework/propio", registry::Kind::Detour,
                        registry::Phase::Attach, addr::cGZShuffleBag_Draw,
                        "Our shuffle bag: constructor, destructors, QueryInterface, Release, Add, Remove, Clear, "
                        "Random, Draw, Get, Set, Erase, Write, Read (15 detours; part u3, docs/gzui.md)",
                        false, InstallShuffleBag}),
    registry::Register({"framework.propio.kinds", "framework/propio", registry::Kind::Detour, registry::Phase::Attach,
                        addr::cGZSoundKind2D_SetUpVoice,
                        "Our framework's own 2D and 3D sound kinds' slots 5 and 6 (4 detours; part u3, docs/gzui.md)",
                        false, InstallKinds}),
};

} // namespace
} // namespace opensc4::gzui
