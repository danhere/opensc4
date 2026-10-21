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
// Still the game's: the 38 small thunks of these classes (a `this`
// adjustment and a jump, or a jump alone, as docs/occbase.md leaves them;
// they all end in the functions here or in the game's), the STLport list
// code, the shared stubs, and the u3 pieces they call (the XML writer and StartElement, the factory's
// Create, the shuffle bag's own slots). Ours call them by address.
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

// The game's 0x0094048B.
char* __fastcall BagConstruct(char* self, void*, const uint32_t* key)
{
    OPENSC4_FIRST("BagConstruct");
    CallThis<void*>(addr::cGZShuffleBag_Construct, self);
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
        return CallThis<uint32_t>(addr::cGZShuffleBag_QueryInterface, self, iid, out);
    *out = self ? self + kBagKey : nullptr;
    return Answer(Virtual<uint32_t>(self, 0x4), true);
}

// The game's 0x00940552.
char* __fastcall BagDeletingDestructor(char* self, void*, uint32_t flags)
{
    OPENSC4_FIRST("BagDeletingDestructor");
    At<std::uintptr_t>(self, 0) = addr::cGZKeyedShuffleBag_Vtable;
    At<std::uintptr_t>(self, kBagKey) = kBagKeyVtable;
    CallThis<uint32_t>(addr::cGZShuffleBag_Destruct, self);
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
};

} // namespace
} // namespace opensc4::gzui
