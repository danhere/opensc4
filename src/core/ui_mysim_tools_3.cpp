// The MySim picker's list of Sims (0x0043B200 to 0x0043D0F0): a small
// reference-counted object (0x2C bytes, vtable 0x00A85B14, nine slots) that
// the MySim dialog creates with operator new (0x00776409). It answers three
// questions: is The Sims installed (slot 8), which Sims can the player pick
// (slot 6: the MySim exemplars of the resources plus the entries of
// MySims.dat in the MySim folder), and import the families The Sims
// exported (slot 7: reads every UserData*\Export\*.pub, converts the face
// pictures to bitmaps in the MySim folder and writes MySims.dat anew).
// None of it draws. Replacement, off by default:
//
//   ui.mysim_tools.part3: the object's functions and its record vector's
//     helpers.
//
// The object: +0 vtable, +4 reference count, +8 list loaded (byte), +0xC
// vector of 0x30-byte records, +0x18 the path of The Sims' data (a string).
// A record: +0 id, +4 picture file name, +0x18 name, +0x2C a flag byte,
// +0x2D the zodiac sign. The file, folder, ini and string classes are the
// game's and are called through their addresses; so are the two vector
// helpers the dialog calls directly (0x0043B360, 0x0043BB90).
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

namespace field {
constexpr std::size_t kLoaded = 0x8;
constexpr std::size_t kSims = 0xC;      // vector of records
constexpr std::size_t kSimsPath = 0x18; // string
} // namespace field

constexpr uint32_t kCatalogMagic = 0x4A32E4AA;
constexpr uint32_t kMySimGroup = 0x6A297266;

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

// A string from the text resources: a string first, 0x20 bytes.
struct LocalizedText {
    GameString text;
    char rest[0xC];
};
static_assert(sizeof(LocalizedText) == 0x20);

struct Record {
    uint32_t id;
    GameString picture;
    GameString name;
    uint8_t flag;
    uint8_t zodiac;
    uint8_t unused[2];
};
static_assert(sizeof(Record) == 0x30);

struct RecordVector {
    Record* begin;
    Record* end;
    Record* capacity;
};

// A path in parts (five strings; the whole path is the last).
struct FileSpec {
    GameString parts[5];
};
static_assert(sizeof(FileSpec) == 0x64);

// The game's folder listing; the folder's path is the string at +8.
struct FolderList {
    alignas(4) char bytes[0x34];
};

struct FileNode {
    FileNode* next;
    FileNode* previous;
    GameString name;
};
static_assert(sizeof(FileNode) == 0x1C);

struct ResourceKey {
    uint32_t type;
    uint32_t group;
    uint32_t instance;
};

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

using SprintfFn = void*(__cdecl*)(void*, const char*, ...);

void Free(void* memory)
{
    Cdecl<void>(addr::SC4_Free, memory);
}

void* Allocate(uint32_t size)
{
    return Cdecl<void*>(addr::SC4_stl_allocate, size);
}

// An empty string as the game's inlined constructor makes it.
void InitString(GameString& text)
{
    text.body.begin = nullptr;
    text.body.end = nullptr;
    text.body.capacity = nullptr;
    char* const buffer = static_cast<char*>(Allocate(8));
    text.body.begin = buffer;
    text.body.end = buffer;
    text.body.capacity = buffer + 8;
    *buffer = 0;
    text.vtable = addr::SC4String_Vtable;
    text.zero = 0;
}

// The inlined destructor.
void DestroyString(GameString& text)
{
    char* const buffer = text.body.begin;
    text.vtable = addr::SC4String_Vtable;
    if (buffer)
        Free(buffer);
}

void FreeBody(StringBody& body)
{
    if (body.begin)
        Free(body.begin);
}

// The body of a string, as the game's casts work it out.
StringBody* Body(void* text)
{
    return text ? reinterpret_cast<StringBody*>(static_cast<char*>(text) + 0x4) : nullptr;
}

void FromText(GameString& text, const char* chars)
{
    This<void*>(addr::SC4String_FromText, &text, chars);
}

void ConvertEncoding(GameString& text)
{
    Cdecl<uint8_t>(addr::RZ_ConvertStringEncoding, &text, int32_t{-1}, &text);
}

// The body of the static string "MySims.dat".
StringBody* CatalogName()
{
    return Body(reinterpret_cast<void*>(addr::SC4MySimImport_CatalogName));
}

StringBody* ConcatText(StringBody& result, StringBody* head, const char* chars)
{
    return Cdecl<StringBody*>(addr::SC4String_ConcatBodyText, &result, head, chars);
}

int32_t CountBetween(const Record* first, const Record* last)
{
    return static_cast<int32_t>(reinterpret_cast<const char*>(last) - reinterpret_cast<const char*>(first)) / 0x30;
}

Record* Offset(Record* record, uint32_t count)
{
    return reinterpret_cast<Record*>(reinterpret_cast<std::uintptr_t>(record) + count * 0x30);
}

void CopyConstruct(Record* target, const Record* source)
{
    This<void*>(addr::SC4UI_F43B100, target, source);
}

// The game's 0x0043AA30 takes the source path in EAX and the destination on
// the stack, which the caller removes: (source, destination, function).
__declspec(naked) void DecodeThunk()
{
    __asm mov eax, dword ptr [esp + 4]
    __asm push dword ptr [esp + 8]
    __asm call dword ptr [esp + 16]
    __asm add esp, 4
    __asm ret
}

uint8_t DecodeFile(GameString* source, GameString* destination)
{
    using ThunkFn = uint8_t(__cdecl*)(void*, void*, std::uintptr_t);
    return reinterpret_cast<ThunkFn>(&DecodeThunk)(source, destination, addr::SC4UI_F43AA30);
}

// The game's 0x0043ABE0 takes the converter in EDI, the source path in EDX
// and the destination on the stack, which the caller removes:
// (converter, source, destination, function).
__declspec(naked) void ConvertThunk()
{
    __asm push edi
    __asm mov edi, dword ptr [esp + 8]
    __asm mov edx, dword ptr [esp + 12]
    __asm push dword ptr [esp + 16]
    __asm call dword ptr [esp + 24]
    __asm add esp, 4
    __asm pop edi
    __asm ret
}

uint8_t ConvertPicture(void* converter, void* source, void* destination)
{
    using ThunkFn = uint8_t(__cdecl*)(void*, void*, void*, std::uintptr_t);
    return reinterpret_cast<ThunkFn>(&ConvertThunk)(converter, source, destination, addr::SC4UI_F43ABE0);
}

// The game's 0x0043B200 (slot 8): The Sims' data path from the registry
// into the string at +0x18; true when that folder exists, else the string is
// emptied.
uint8_t __fastcall FindSims(char* self, void*)
{
    GameString* const path = reinterpret_cast<GameString*>(self + field::kSimsPath);
    if (!Cdecl<uint8_t>(addr::SC4Registry_ReadString, reinterpret_cast<void*>(addr::SC4MySimImport_SimsDataKey),
                        path) &&
        !Cdecl<uint8_t>(addr::SC4Registry_ReadString,
                        reinterpret_cast<void*>(addr::SC4MySimImport_InstallPathKey), path))
        return 0;
    Cdecl<uint8_t>(addr::RZ_ConvertStringEncoding, path, int32_t{-1}, path);
    if (Cdecl<uint8_t>(addr::RZDirectory_Exists, path))
        return 1;
    GameString empty;
    FromText(empty, "");
    char* const begin = empty.body.begin;
    if (&empty.body != &path->body) {
        char tag = 0;
        This<void*>(addr::SC4String_AssignRange, &path->body, begin, empty.body.end, &tag);
    }
    if (begin)
        Free(begin);
    return 0;
}

// The game's 0x0043B290: storage for `count` records with copies of
// [first, last) in it. `this` is not used.
Record* __fastcall AllocateCopy(void*, void*, uint32_t count, Record* first, Record* last)
{
    Record* const fresh = count ? static_cast<Record*>(Allocate(count * 0x30)) : nullptr;
    Record* out = fresh;
    for (Record* in = first; in != last; in = Offset(in, 1), out = Offset(out, 1)) {
        if (out)
            CopyConstruct(out, in);
    }
    return fresh;
}

// The game's 0x0043B2F0: a record assigned from another.
Record* __fastcall AssignRecord(Record* self, void*, Record* source)
{
    char tag = 0;
    self->id = source->id;
    StringBody* body = Body(&source->picture);
    if (body != &self->picture.body)
        This<void*>(addr::SC4String_AssignRange, &self->picture.body, body->begin, body->end, &tag);
    body = Body(&source->name);
    if (body != &self->name.body)
        This<void*>(addr::SC4String_AssignRange, &self->name.body, body->begin, body->end, &tag);
    self->flag = source->flag;
    self->zodiac = source->zodiac;
    return self;
}

// The game's 0x0043B3B0: the constructor.
char* __fastcall Construct(char* self, void*)
{
    This<void*>(addr::cRZBaseUnknown_Construct, self);
    At<std::uintptr_t>(self, 0x0) = addr::SC4MySimImport_Vtable;
    At<uint8_t>(self, field::kLoaded) = 0;
    RecordVector& sims = At<RecordVector>(self, field::kSims);
    sims.begin = nullptr;
    sims.end = nullptr;
    sims.capacity = nullptr;
    InitString(At<GameString>(self, field::kSimsPath));
    return self;
}

// The game's 0x0043B400: the destructor.
void __fastcall Destruct(char* self, void*)
{
    char tag = 0;
    At<std::uintptr_t>(self, 0x0) = addr::SC4MySimImport_Vtable;
    GameString& path = At<GameString>(self, field::kSimsPath);
    path.vtable = addr::SC4String_Vtable;
    if (path.body.begin)
        Free(path.body.begin);
    RecordVector& sims = At<RecordVector>(self, field::kSims);
    Cdecl<void>(addr::SC4UI_F43B180, sims.begin, sims.end, &tag);
    if (sims.begin)
        Free(sims.begin);
    This<void>(addr::cRZBaseUnknown_Destruct, self);
}

// The game's 0x0043B450: `count` copies of a record put in at `position`
// when the vector has no room: new storage of the old size plus the larger
// of the old size and `count`.
void __fastcall InsertOverflow(RecordVector* self, void*, Record* position, const Record* value, void*,
                               uint32_t count, uint8_t atEnd)
{
    char tag = 0;
    const uint32_t oldSize = static_cast<uint32_t>(CountBetween(self->begin, self->end));
    const uint32_t newCapacity = oldSize + (oldSize < count ? count : oldSize);
    Record* const fresh = newCapacity ? static_cast<Record*>(Allocate(newCapacity * 0x30)) : nullptr;
    Record* out = fresh;
    for (Record* in = self->begin; in != position; in = Offset(in, 1), out = Offset(out, 1)) {
        if (out)
            CopyConstruct(out, in);
    }
    if (count == 1) {
        if (out)
            CopyConstruct(out, value);
        out = Offset(out, 1);
    } else if (count > 0) {
        uint32_t left = count;
        do {
            if (out)
                CopyConstruct(out, value);
            out = Offset(out, 1);
        } while (--left);
    }
    if (!atEnd)
        out = Cdecl<Record*>(addr::SC4UI_F43B1D0, position, self->end, out, &tag);
    Cdecl<void>(addr::SC4UI_F43B180, self->begin, self->end, &tag);
    if (self->begin)
        Free(self->begin);
    self->end = out;
    self->begin = fresh;
    self->capacity = Offset(fresh, newCapacity);
}

// The game's 0x0043B580 (its second half is at 0x0043B610): the vector
// assigned from another.
RecordVector* __fastcall AssignVector(RecordVector* self, void*, RecordVector* other)
{
    if (other == self)
        return self;
    char tag = 0;
    Record* const otherEnd = other->end;
    Record* const otherBegin = other->begin;
    const uint32_t count = static_cast<uint32_t>(CountBetween(otherBegin, otherEnd));
    Record* const begin = self->begin;
    if (count > static_cast<uint32_t>(CountBetween(begin, self->capacity))) {
        Record* const fresh = AllocateCopy(self, nullptr, count, otherBegin, otherEnd);
        Cdecl<void>(addr::SC4UI_F43B180, self->begin, self->end, &tag);
        if (self->begin)
            Free(self->begin);
        self->begin = fresh;
        self->capacity = Offset(fresh, count);
    } else {
        const uint32_t size = static_cast<uint32_t>(CountBetween(begin, self->end));
        if (size >= count) {
            Record* const copied =
                Cdecl<Record*>(addr::SC4UI_F43B360, otherBegin, otherEnd, begin, &tag, uint32_t{0});
            Cdecl<void>(addr::SC4UI_F43B180, copied, self->end, &tag);
        } else {
            Cdecl<Record*>(addr::SC4UI_F43B360, otherBegin, Offset(otherBegin, size), begin, &tag, uint32_t{0});
            Record* const end = self->end;
            Record* const last = other->end;
            const uint32_t have = static_cast<uint32_t>(CountBetween(self->begin, end));
            Cdecl<Record*>(addr::SC4UI_F43B1D0, Offset(other->begin, have), last, end, &tag);
        }
    }
    self->end = Offset(self->begin, count);
    return self;
}

// The game's 0x0043B6B0 (slot 3): the deleting destructor.
char* __fastcall DeletingDestruct(char* self, void*, uint32_t flags)
{
    Destruct(self, nullptr);
    if (static_cast<uint8_t>(flags) & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// A record appended to a vector; the game has this inlined in 0x0043B860
// too.
void Append(RecordVector* sims, Record* value)
{
    Record* const end = sims->end;
    if (end != sims->capacity) {
        if (end)
            CopyConstruct(end, value);
        sims->end = Offset(sims->end, 1);
    } else {
        char tag = 0;
        InsertOverflow(sims, nullptr, end, value, &tag, 1, 1);
    }
}

// The game's 0x0043B6D0: a record appended to the vector.
void __fastcall PushBack(RecordVector* self, void*, Record* value)
{
    Append(self, value);
}

// The game's 0x0043B710: the callback of the resource list in GetSims: a
// MySim exemplar (group 0x6A297266, exemplar type 0x26) becomes a record
// of the object given as context. The id and the two bytes keep whatever
// the stack held when the exemplar lacks the property.
void __cdecl AddExemplar(ResourceKey* key, char* self)
{
    if (key->group != kMySimGroup)
        return;
    void* const resources = Global<void*>(addr::SC4_ResourceManager);
    void* resource = nullptr;
    Record record;
    This<void*>(addr::SCLuaRecord_Construct, &record);
    if (record.picture.body.begin != record.picture.body.end) {
        *record.picture.body.begin = 0;
        record.picture.body.end = record.picture.body.begin;
    }
    if (Virtual<uint8_t>(resources, 0xC, key, uint32_t{0xA52160F5}, &resource, uint32_t{0}, uint32_t{0})) {
        void* const holder = Virtual<void*>(resource, 0x1C);
        uint32_t type = 0;
        if (Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, uint32_t{0x10}, &type) && type == 0x26) {
            Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, uint32_t{0xEA296F8D}, &record.id);
            if (!Cdecl<uint8_t>(addr::SC4_GetLocalizedString, &record.name, key->group, key->instance,
                                uint32_t{0xCA416B3F}, uint32_t{0}))
                Cdecl<uint8_t>(addr::SC4_GetPropertyString, holder, uint32_t{0xEA296F8F}, &record.name);
            Cdecl<uint8_t>(addr::SC4_GetPropertyBool, holder, uint32_t{0xEA296F91}, &record.flag);
            Cdecl<uint8_t>(addr::SC4_GetPropertyUint8, holder, uint32_t{0xEA296F90}, &record.zodiac);
            PushBack(&At<RecordVector>(self, field::kSims), nullptr, &record);
        }
        Virtual<uint32_t>(resource, 0x8);
    }
    DestroyString(record.name);
    DestroyString(record.picture);
}

// The game's 0x0043B860: the records of MySims.dat in the MySim folder
// appended to the vector. The file: the magic, then per Sim a length byte
// and the name, the flag, the zodiac sign, a length byte and the picture's
// name without ".bmp".
void __fastcall LoadCatalog(char* self, void*)
{
    char tag = 0;
    GameString folder;
    InitString(folder);
    GameString path;
    InitString(path);
    Record record;
    This<void*>(addr::SCLuaRecord_Construct, &record);
    Virtual<uint8_t>(Global<void*>(addr::SC4_Service43C94), 0xF4, &folder); // GetMySimDirectory
    {
        StringBody joined;
        StringBody* const both = Cdecl<StringBody*>(addr::cSC4Advice_F408AE0, &joined, &folder.body, CatalogName());
        StringBody whole = {nullptr, nullptr, nullptr};
        This<void>(addr::cSC4Advice_F408420, &whole, both->begin, both->end, &tag);
        char* const begin = whole.begin;
        This<void*>(addr::SC4String_AssignRange, &path.body, begin, whole.end, &tag);
        if (begin)
            Free(begin);
        FreeBody(joined);
    }
    void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x80});
    void* const file = memory ? This<void*>(addr::cRZFile_Construct, memory, &path) : nullptr;
    Virtual<uint32_t>(file, 0x4);
    if (!Virtual<uint8_t>(file, 0xC, int32_t{1}, int32_t{2}, int32_t{1})) { // Open
        Virtual<uint32_t>(file, 0x8);
        DestroyString(record.name);
        DestroyString(record.picture);
    } else {
        uint32_t magic;
        if (!Virtual<uint8_t>(file, 0x38, &magic, uint32_t{4}) || magic != kCatalogMagic) {
            Virtual<uint8_t>(file, 0x14);
            Virtual<uint32_t>(file, 0x8);
            DestroyString(record.name);
            DestroyString(record.picture);
        } else {
            uint8_t length;
            uint8_t value;
            char buffer[0x100];
            record.id = 0;
            if (Virtual<uint8_t>(file, 0x38, &length, uint32_t{1})) {
                do {
                    if (!Virtual<uint8_t>(file, 0x38, buffer, uint32_t{length}))
                        break;
                    This<void>(addr::cRZBaseString_FromCharLength, &record.name, buffer, uint32_t{length});
                    if (!Virtual<uint8_t>(file, 0x38, &value, uint32_t{1}))
                        break;
                    record.flag = value != 0;
                    if (!Virtual<uint8_t>(file, 0x38, &value, uint32_t{1}))
                        break;
                    record.zodiac = value;
                    if (!Virtual<uint8_t>(file, 0x38, &length, uint32_t{1}))
                        break;
                    if (!Virtual<uint8_t>(file, 0x38, buffer, uint32_t{length}))
                        break;
                    This<void>(addr::cRZBaseString_FromCharLength, &record.picture, buffer, uint32_t{length});
                    This<void*>(addr::cRZBaseString_AppendChars, &record.picture, ".bmp", uint32_t{4});
                    Append(&At<RecordVector>(self, field::kSims), &record);
                } while (Virtual<uint8_t>(file, 0x38, &length, uint32_t{1}));
            }
            Virtual<uint8_t>(file, 0x14);
            Virtual<uint32_t>(file, 0x8);
            This<void>(addr::SC4UI_F43AF40, &record);
        }
    }
    DestroyString(path);
    DestroyString(folder);
}

// The game's 0x0043BBD0 (slot 6): the Sims to pick from, copied into the
// caller's vector. The first call collects them: the MySim exemplars of the
// resources, then the catalog file.
RecordVector* __fastcall GetSims(char* self, void*, RecordVector* out)
{
    if (!At<uint8_t>(self, field::kLoaded)) {
        void* const resources = Global<void*>(addr::SC4_ResourceManager);
        void* list = nullptr;
        if (resources && Virtual<uint32_t>(resources, 0x48, &list, uint32_t{0x6534284A})) {
            Virtual<void>(list, 0x1C, reinterpret_cast<void*>(addr::SC4UI_F43B710), self); // EnumKeys
            Virtual<uint32_t>(list, 0x8);
        }
        LoadCatalog(self, nullptr);
        At<uint8_t>(self, field::kLoaded) = 1;
    }
    return AssignVector(out, nullptr, &At<RecordVector>(self, field::kSims));
}

// The six strings of one family member, destroyed as the game's inlined
// code does.
void DestroyMember(GameString& picture, GameString& face, GameString& zodiac, GameString& gender,
                   GameString& name, GameString& key)
{
    DestroyString(picture);
    DestroyString(face);
    DestroyString(zodiac);
    DestroyString(gender);
    DestroyString(name);
    DestroyString(key);
}

// The same through the game's destructor.
void DestroyMemberCalled(GameString& picture, GameString& face, GameString& zodiac, GameString& gender,
                         GameString& name, GameString& key)
{
    This<void>(addr::SC4String_Destroy, &picture);
    This<void>(addr::SC4String_Destroy, &face);
    This<void>(addr::SC4String_Destroy, &zodiac);
    This<void>(addr::SC4String_Destroy, &gender);
    This<void>(addr::SC4String_Destroy, &name);
    This<void>(addr::SC4String_Destroy, &key);
}

// One member of a family: its picture converted and its entry written,
// unless the ini lacks one of its keys or the picture fails.
void ImportMember(void* ini, void* file, void* converter, uint32_t member, GameString& familyName,
                  LocalizedText& genderText, FileSpec& source, FileSpec& target, uint32_t* count)
{
    const auto format = reinterpret_cast<SprintfFn>(addr::cRZBaseString_Sprintf);
    GameString key;
    InitString(key);
    GameString name;
    InitString(name);
    GameString gender;
    InitString(gender);
    GameString zodiac;
    InitString(zodiac);
    GameString face;
    InitString(face);
    GameString picture;
    InitString(picture);
    format(&key, "sims_familymember%d_name", member);
    uint8_t missing;
    {
        GameString section;
        FromText(section, "FamilyInfo");
        missing = !Virtual<uint8_t>(ini, 0x18, &section, &key, &name);
        DestroyString(section);
    }
    if (missing) {
        DestroyMember(picture, face, zodiac, gender, name, key);
        return;
    }
    ConvertEncoding(name);
    format(&key, "sims_familymember%d_gender", member);
    {
        GameString section;
        FromText(section, "FamilyInfo");
        missing = !Virtual<uint8_t>(ini, 0x18, &section, &key, &gender);
        DestroyString(section);
    }
    if (missing) {
        DestroyMember(picture, face, zodiac, gender, name, key);
        return;
    }
    ConvertEncoding(gender);
    format(&key, "sims_familymember%d_zodiac", member);
    {
        GameString section;
        FromText(section, "FamilyInfo");
        missing = !Virtual<uint8_t>(ini, 0x18, &section, &key, &zodiac);
        DestroyString(section);
    }
    if (missing) {
        DestroyMemberCalled(picture, face, zodiac, gender, name, key);
        return;
    }
    ConvertEncoding(zodiac);
    This<void>(addr::cRZBaseString_MakeLower, &zodiac);
    format(&key, "sims_familymember%d_guid", member);
    uint32_t guid;
    {
        GameString section;
        FromText(section, "FamilyInfo");
        missing = !Virtual<uint8_t>(ini, 0x1C, &section, &key, &guid);
        This<void>(addr::SC4String_Destroy, &section);
    }
    if (missing) {
        DestroyMemberCalled(picture, face, zodiac, gender, name, key);
        return;
    }
    format(&face, "family%d_face", member);
    This<uint8_t>(addr::cRZFileSpec_SetName, &source, &face, uint32_t{0});
    format(&picture, "%s_%x", This<const char*>(addr::cRZBaseString_Data, &name), guid);
    This<uint8_t>(addr::cRZFileSpec_SetName, &target, &picture, uint32_t{0});
    GameString targetPath;
    GameString sourcePath;
    void* const to = This<void*>(addr::cRZFileSpec_GetPath, &target, &targetPath);
    void* const from = This<void*>(addr::cRZFileSpec_GetPath, &source, &sourcePath);
    const uint8_t failed = !ConvertPicture(converter, from, to);
    This<void>(addr::SC4String_Destroy, &sourcePath);
    This<void>(addr::SC4String_Destroy, &targetPath);
    if (!failed) {
        This<void>(addr::cSC4Advisor_F410F00, &name.body, " ");
        This<void>(addr::SC4String_AppendStd, &name.body, &familyName.body);
        uint8_t length = static_cast<uint8_t>(reinterpret_cast<std::uintptr_t>(name.body.end) -
                                              reinterpret_cast<std::uintptr_t>(name.body.begin));
        Virtual<uint8_t>(file, 0x40, &length, uint32_t{1});
        Virtual<uint8_t>(file, 0x40, This<const char*>(addr::cRZBaseString_Data, &name), uint32_t{length});
        uint8_t value = This<uint8_t>(addr::cRZBaseString_IsEqual, &gender, &genderText, uint32_t{0});
        Virtual<uint8_t>(file, 0x40, &value, uint32_t{1});
        value = Cdecl<uint8_t>(addr::SC4UI_F43AFF0, &zodiac);
        Virtual<uint8_t>(file, 0x40, &value, uint32_t{1});
        length = static_cast<uint8_t>(reinterpret_cast<std::uintptr_t>(picture.body.end) -
                                      reinterpret_cast<std::uintptr_t>(picture.body.begin));
        Virtual<uint8_t>(file, 0x40, &length, uint32_t{1});
        Virtual<uint8_t>(file, 0x40, This<const char*>(addr::cRZBaseString_Data, &picture), uint32_t{length});
        ++*count;
    }
    DestroyMemberCalled(picture, face, zodiac, gender, name, key);
}

// One exported family (a .pub file in an Export folder): decoded to an ini
// next to it, its members imported, the ini removed again.
void ImportFamily(void* file, void* converter, FolderList& finder, FileNode* node, GameString& userData,
                  LocalizedText& genderText, FileSpec& target, uint32_t* count)
{
    char tag = 0;
    GameString pubPath;
    InitString(pubPath);
    GameString iniPath;
    InitString(iniPath);
    GameString webPages;
    InitString(webPages);
    uint32_t members = 0;
    GameString familyName;
    InitString(familyName);
    StringBody* const fileName = Body(&node->name);
    {
        GameString folder;
        This<void>(addr::SC4String_CopyBuffer, &folder.body, reinterpret_cast<StringBody*>(finder.bytes + 0xC),
                   uint32_t{0}, uint32_t{0xFFFFFFFF}, &tag);
        folder.vtable = addr::SC4String_Vtable;
        folder.zero = 0;
        StringBody joined;
        StringBody* const both = Cdecl<StringBody*>(addr::cSC4Advice_F408AE0, &joined, &folder.body, fileName);
        GameString whole;
        whole.body = {nullptr, nullptr, nullptr};
        This<void>(addr::cSC4Advice_F408420, &whole.body, both->begin, both->end, &tag);
        char* const begin = whole.body.begin;
        whole.zero = 0;
        This<void*>(addr::SC4String_AssignRange, &pubPath.body, begin, whole.body.end, &tag);
        whole.vtable = addr::SC4String_Vtable;
        if (begin)
            Free(begin);
        FreeBody(joined);
        DestroyString(folder);
    }
    {
        GameString stem;
        void* const left =
            This<void*>(addr::cRZBaseString_Left, &pubPath, &stem,
                        static_cast<uint32_t>(pubPath.body.end - pubPath.body.begin) - uint32_t{4});
        StringBody joined;
        StringBody* const both = ConcatText(joined, Body(left), ".ini");
        GameString whole;
        whole.body = {nullptr, nullptr, nullptr};
        This<void>(addr::cSC4Advice_F408420, &whole.body, both->begin, both->end, &tag);
        char* const begin = whole.body.begin;
        whole.zero = 0;
        This<void*>(addr::SC4String_AssignRange, &iniPath.body, begin, whole.body.end, &tag);
        whole.vtable = addr::SC4String_Vtable;
        if (begin)
            Free(begin);
        FreeBody(joined);
        DestroyString(stem);
    }
    if (DecodeFile(&pubPath, &iniPath)) {
        void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x3C});
        void* const ini = memory ? This<void*>(addr::cRZIniFile_Construct, memory, &iniPath) : nullptr;
        if (!ini) {
            Cdecl<uint8_t>(addr::RZFile_Remove, &iniPath);
        } else {
            GameString key;
            FromText(key, "WebPages");
            GameString section;
            FromText(section, "FamilyDescription");
            const uint8_t missing = !Virtual<uint8_t>(ini, 0x18, &section, &key, &webPages);
            DestroyString(section);
            DestroyString(key);
            if (missing) {
                Virtual<void>(ini, 0x14);
                Virtual<void>(ini, 0x0, uint32_t{1});
            } else {
                ConvertEncoding(webPages);
                if (This<uint8_t>(addr::cRZBaseString_EndsWith, &webPages, "/", uint32_t{1}))
                    This<void>(addr::cRZBaseString_Truncate, &webPages,
                               static_cast<uint32_t>(webPages.body.end - webPages.body.begin) - uint32_t{1});
                StringBody a;
                StringBody b;
                StringBody c;
                StringBody d;
                StringBody e;
                StringBody f;
                StringBody g;
                StringBody* part = ConcatText(a, &userData.body, "\\");
                part = ConcatText(b, part, "Web Pages");
                part = ConcatText(c, part, "\\");
                part = Cdecl<StringBody*>(addr::cSC4Advice_F408AE0, &d, part, &webPages.body);
                part = ConcatText(e, part, "\\");
                part = ConcatText(f, part, "FamilyGFX");
                part = ConcatText(g, part, "\\");
                GameString pictures;
                This<void*>(addr::cSC4Advice_F408B90, &pictures, part);
                FreeBody(g);
                FreeBody(f);
                FreeBody(e);
                FreeBody(d);
                FreeBody(c);
                FreeBody(b);
                FreeBody(a);
                FileSpec source;
                This<void*>(addr::cRZFileSpec_Construct, &source, &pictures);
                {
                    GameString extension;
                    FromText(extension, ".jpg");
                    This<uint8_t>(addr::cRZFileSpec_SetExtension, &source, &extension, uint32_t{0});
                    DestroyString(extension);
                }
                {
                    GameString numberKey;
                    FromText(numberKey, "sims_family_nummembers");
                    GameString info;
                    FromText(info, "FamilyInfo");
                    Virtual<uint8_t>(ini, 0x1C, &info, &numberKey, &members);
                    DestroyString(info);
                    DestroyString(numberKey);
                }
                {
                    GameString nameKey;
                    FromText(nameKey, "sims_family_name");
                    GameString info;
                    FromText(info, "FamilyInfo");
                    Virtual<uint8_t>(ini, 0x18, &info, &nameKey, &familyName);
                    DestroyString(info);
                    DestroyString(nameKey);
                }
                ConvertEncoding(familyName);
                for (uint32_t member = 1; member <= members; ++member)
                    ImportMember(ini, file, converter, member, familyName, genderText, source, target, count);
                Virtual<void>(ini, 0x14);
                Virtual<void>(ini, 0x0, uint32_t{1});
                Cdecl<uint8_t>(addr::RZFile_Remove, &iniPath);
                This<void>(addr::SC4UI_F43AF80, &source);
                DestroyString(pictures);
            }
        }
    }
    DestroyString(familyName);
    DestroyString(webPages);
    DestroyString(iniPath);
    DestroyString(pubPath);
}

// The game's 0x0043BC40 (slot 7; one function up to 0x0043D0E1): imports
// the families The Sims exported. For UserData and UserData2 to UserData99
// under The Sims' data path, every Export\*.pub is decoded and each family
// member's face (Web Pages\<pages>\FamilyGFX\family<n>_face.jpg) converted
// to <name>_<guid>.bmp in the MySim folder; MySims.dat is written anew with
// one entry per member. `count` gets the number of members imported; true
// when there is at least one, and then the cached list is dropped.
uint8_t __fastcall Import(char* self, void*, uint32_t* count)
{
    const auto format = reinterpret_cast<SprintfFn>(addr::cRZBaseString_Sprintf);
    char tag = 0;
    GameString catalog;
    InitString(catalog);
    void* converter = nullptr;
    LocalizedText genderText;
    This<void*>(addr::SC4LocalizedString_SC4LocalizedString, &genderText, uint32_t{0xEA231E96},
                uint32_t{0x6A7630C6}, uint32_t{1});
    *count = 0;
    if (!Virtual<uint8_t>(self, 0x20)) {
        DestroyString(genderText.text);
        DestroyString(catalog);
        return 0;
    }
    GameString folder;
    InitString(folder);
    Virtual<uint8_t>(Global<void*>(addr::SC4_Service43C94), 0xF4, &folder); // GetMySimDirectory
    if (!Cdecl<uint8_t>(addr::RZDirectory_Exists, &folder))
        Cdecl<uint8_t>(addr::RZDirectory_Create, &folder);
    FileSpec target;
    This<void*>(addr::cRZFileSpec_Construct, &target, &folder);
    {
        GameString extension;
        FromText(extension, ".bmp");
        This<uint8_t>(addr::cRZFileSpec_SetExtension, &target, &extension, uint32_t{0});
        DestroyString(extension);
    }
    Virtual<uint8_t>(Cdecl<void*>(addr::RZGetCOM), 0xC, uint32_t{0xA2FFB5D3}, uint32_t{0x02FFBB60}, &converter);
    if (!converter) {
        This<void>(addr::SC4UI_F43AF80, &target);
        DestroyString(folder);
        DestroyString(genderText.text);
        DestroyString(catalog);
        return 0;
    }
    {
        StringBody joined;
        StringBody* const both = Cdecl<StringBody*>(addr::cSC4Advice_F408AE0, &joined, &folder.body, CatalogName());
        GameString whole;
        This<void*>(addr::cSC4Advice_F408B90, &whole, both);
        char* const begin = whole.body.begin;
        This<void*>(addr::SC4String_AssignRange, &catalog.body, begin, whole.body.end, &tag);
        if (begin)
            Free(begin);
        FreeBody(joined);
    }
    void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x80});
    void* const file = memory ? This<void*>(addr::cRZFile_Construct, memory, &catalog) : nullptr;
    Virtual<uint32_t>(file, 0x4);
    uint8_t done = 0;
    if (Virtual<uint8_t>(file, 0xC, int32_t{2}, int32_t{1}, int32_t{1}) && // Open
        Virtual<uint8_t>(file, 0x40, &kCatalogMagic, uint32_t{4})) {
        for (int32_t family = 1; family <= 0x63; ++family) {
            GameString userData;
            InitString(userData);
            GameString exportFolder;
            InitString(exportFolder);
            FileNode* files = static_cast<FileNode*>(Allocate(0x1C));
            files->next = files;
            files->previous = files;
            GameString empty;
            InitString(empty);
            FolderList finder;
            This<void*>(addr::cRZDirectory_Construct, &finder, &empty, uint32_t{0}, uint32_t{0}, uint32_t{0});
            DestroyString(empty);
            void* const simsPath = self + field::kSimsPath;
            if (family == 1)
                format(&userData, "%s%sUserData", Virtual<const char*>(simsPath, 0x14), "\\");
            else
                format(&userData, "%s%sUserData%d", Virtual<const char*>(simsPath, 0x14), "\\", family);
            if (Cdecl<uint8_t>(addr::RZDirectory_Exists, &userData)) {
                {
                    StringBody a;
                    StringBody b;
                    StringBody c;
                    StringBody* part = ConcatText(a, &userData.body, "\\");
                    part = ConcatText(b, part, "Export");
                    part = ConcatText(c, part, "\\");
                    GameString whole;
                    This<void*>(addr::cSC4Advice_F408B90, &whole, part);
                    char* const begin = whole.body.begin;
                    This<void*>(addr::SC4String_AssignRange, &exportFolder.body, begin, whole.body.end, &tag);
                    whole.vtable = addr::SC4String_Vtable;
                    if (begin)
                        Free(begin);
                    FreeBody(c);
                    FreeBody(b);
                    FreeBody(a);
                }
                This<uint8_t>(addr::cRZDirectory_SetPath, &finder, &exportFolder, uint32_t{1});
                {
                    GameString pattern;
                    FromText(pattern, "*.pub");
                    This<uint8_t>(addr::cRZDirectory_SetPattern, &finder, &pattern, uint32_t{1});
                    DestroyString(pattern);
                }
                This<void>(addr::cRZDirectory_GetFiles, &finder, &files);
                for (FileNode* node = files->next; node != files; node = node->next)
                    ImportFamily(file, converter, finder, node, userData, genderText, target, count);
            }
            This<void>(addr::cRZDirectory_Destruct, &finder);
            This<void>(addr::SC4UI_F43ABA0, &files);
            if (files)
                Free(files);
            DestroyString(exportFolder);
            DestroyString(userData);
        }
        Virtual<uint8_t>(file, 0x14);
        Virtual<uint32_t>(file, 0x8);
        Virtual<uint32_t>(converter, 0x8);
        if (*count != 0) {
            if (At<uint8_t>(self, field::kLoaded)) {
                This<void>(addr::SC4UI_F43BB90, self + field::kSims);
                At<uint8_t>(self, field::kLoaded) = 0;
            }
            done = 1;
        }
    }
    This<void>(addr::SC4UI_F43AF80, &target);
    DestroyString(folder);
    DestroyString(genderText.text);
    DestroyString(catalog);
    return done;
}

bool InstallMySimTools3(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[16] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F43B200, reinterpret_cast<void*>(&FindSims)},
        {addr::SC4UI_F43B290, reinterpret_cast<void*>(&AllocateCopy)},
        {addr::SC4UI_F43B2F0, reinterpret_cast<void*>(&AssignRecord)},
        {addr::SC4UI_F43B3B0, reinterpret_cast<void*>(&Construct)},
        {addr::SC4UI_F43B400, reinterpret_cast<void*>(&Destruct)},
        {addr::SC4UI_F43B450, reinterpret_cast<void*>(&InsertOverflow)},
        {addr::SC4UI_F43B580, reinterpret_cast<void*>(&AssignVector)},
        {addr::SC4UI_F43B6B0, reinterpret_cast<void*>(&DeletingDestruct)},
        {addr::SC4UI_F43B6D0, reinterpret_cast<void*>(&PushBack)},
        {addr::SC4UI_F43B710, reinterpret_cast<void*>(&AddExemplar)},
        {addr::SC4UI_F43B860, reinterpret_cast<void*>(&LoadCatalog)},
        {addr::SC4UI_F43BBD0, reinterpret_cast<void*>(&GetSims)},
        {addr::SC4UI_F43BC40, reinterpret_cast<void*>(&Import)},
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

const bool g_mySimTools3Registered = registry::Register({
    "ui.mysim_tools.part3",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F43BC40,
    "Our list of Sims for the MySim picker and the import from The Sims (docs/ui.md)",
    false,
    InstallMySimTools3,
});

} // namespace
} // namespace opensc4::ui
