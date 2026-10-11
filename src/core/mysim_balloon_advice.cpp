// The MySim's balloon advice (cSC4MySimBalloonAdvice, class 0x6B70EFAD, 0x104
// bytes; 0x0042F6D0 to 0x00430250): the advice class behind the speech
// balloons a MySim shows. It is cSC4Advice (0xC0 bytes, ours in advice_1.cpp
// and advice_2.cpp) with a list of balloon icons and two sound keys. Replacement,
// off by default:
//
//   simulators.mysim_agents.balloon_advice: the class's methods, its Init
//     from the script table, and the helpers of the icon vector (a vector of
//     0x30-byte SC4ImageLoader holders) that only this class uses.
//
// The object: main vtable 0x00A85650 at +0 (the advice's slots, Init at slot
// 3, the trigger test at slot 8), the counted base at +4 (0x00A85634), the
// serialisable part at +0xC (0x00A8561C), and from +0xC0 a second interface,
// the balloon part (vtable 0x00A855E0, 15 slots): its `this` is the object plus
// 0xC0. Fields of the balloon part, from the object's start: the icon vector
// at +0xC4 (begin, end, capacity), the message text at +0xD0 (a localised
// string, 0x20 bytes), the part's flags at +0xF4, the sound keys at +0xF8
// (male) and +0xFC (female) and the sound chance at +0x100.
//
// Left the game's, called by address or through vtables: the jump thunks
// 0x0042F6E0 (main slot 28), 0x0042F840, 0x0042F850, 0x0042F860 and 0x0042F870
// (the base's AddRef, Release, Write and Read), the adjustor thunks 0x0042F6F0
// and 0x0042F9D0 to 0x0042FA60, and everything the base class and the string,
// script and image helpers do.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace opensc4::mysim_agents {
namespace {

namespace field { // from the object's start
constexpr std::size_t kTexts = 0x10;    // the base's vector of (field name, localised string)
constexpr std::size_t kIcons = 0xC4;    // vector of SC4ImageLoader holders (0x30 bytes each)
constexpr std::size_t kMessage = 0xD0;  // localised string
constexpr std::size_t kPartFlags = 0xF4;
constexpr std::size_t kSoundMale = 0xF8;
constexpr std::size_t kSoundFemale = 0xFC;
constexpr std::size_t kSoundChance = 0x100;
} // namespace field

constexpr std::size_t kPart = 0xC0;       // the balloon part, in the object
constexpr std::size_t kHolder = 0x30;     // size of one SC4ImageLoader holder
constexpr uint32_t kClassId = 0x6B70EFAD;
constexpr uint32_t kAdviceIid = 0x2B722C71;
constexpr uint32_t kPartIid = 0x6B70E6D9;
constexpr uint32_t kIconType = 0x856DDBAC;
constexpr uint32_t kIconGroup = 0xAB7E5421;
constexpr int32_t kIconCount = 9; // balloon_icon1 to balloon_icon9

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

// An open table of the script context (SC4ScriptTable, 0x24 bytes).
struct ScriptTable {
    int32_t top;
    int32_t index;
    void* script;
    uint8_t open;
    GameString text;
};
static_assert(sizeof(ScriptTable) == 0x24);

// A holder of an image (SC4ImageLoader, 0x30 bytes; the original's frame
// keeps 0x30 bytes for it).
struct ImageLoader {
    std::uintptr_t vtable;
    void* image;
    uint32_t key[3];
    GameString name;
    uint32_t mode;
    uint8_t flag;
};
static_assert(sizeof(ImageLoader) == kHolder);

// An item of the base's text vector.
struct TextField {
    const char* name;
    void* text;
};
static_assert(sizeof(TextField) == 8);

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

void Free(void* memory)
{
    Cdecl<void>(addr::SC4_Free, memory);
}

void* Allocate(std::size_t size)
{
    return Cdecl<void*>(addr::SC4_stl_allocate, size);
}

// The game's slot 1 (AddRef) and 2 (Release) of a counted object.
void AddRef(void* object)
{
    Virtual<uint32_t>(object, 0x4);
}

void Release(void* object)
{
    Virtual<uint32_t>(object, 0x8);
}

void DestroyString(GameString& text)
{
    text.vtable = addr::SC4String_Vtable;
    if (text.body.begin)
        Free(text.body.begin);
}

// The element count of a vector of holders, as the compiler's division
// by 0x30 gives it (signed).
int32_t HolderCount(char* begin, char* end)
{
    return static_cast<int32_t>(end - begin) / static_cast<int32_t>(kHolder);
}

// --- The balloon part (this is the object plus 0xC0) ----------------------------------

// The game's 0x0042F6D0 (slot 5 of the serialisable part).
uint32_t __fastcall GetGZCLSID(char*, void*)
{
    return kClassId;
}

// The game's 0x0042F700 (main slot 8, the advice's "is to be given" test).
// Not while the part's flag 4 is set; then only when the part's flag 2 or
// the advice's own flag 2 is set, and the base class decides.
uint8_t __fastcall IsTriggered(char* self, void*)
{
    char* const part = self + kPart;
    if (!Virtual<uint8_t>(part, 0x38, uint32_t{4})) {
        if (Virtual<uint8_t>(part, 0x38, uint32_t{2}) || Virtual<uint8_t>(self, 0x3C, uint32_t{2}))
            return This<uint8_t>(addr::cSC4Advice_F408970, self);
    }
    return 0;
}

// The game's 0x0042F750 (slots 3 and 4 of the part): the object the part is
// in.
char* __fastcall AsAdvice(char* part, void*)
{
    return part - kPart;
}

// The game's 0x0042F760 (slot 10 of the part): the sound key, male with a
// non-zero argument and female otherwise.
uint32_t __fastcall GetSoundKey(char* part, void*, uint8_t male)
{
    return male ? At<uint32_t>(part, field::kSoundMale - kPart) : At<uint32_t>(part, field::kSoundFemale - kPart);
}

// The game's 0x0042F780 (slot 13 of the part): sets or clears bits in the
// part's flags. Setting bit 2 while the advice has its flag 0x10 set asks
// the advice to reload its texts (main slot 30).
void __fastcall SetPartFlags(char* part, void*, uint32_t mask, uint8_t set)
{
    if (set) {
        const uint32_t flags = At<uint32_t>(part, field::kPartFlags - kPart) | mask;
        At<uint32_t>(part, field::kPartFlags - kPart) = flags;
        if (flags & 2) {
            char* const self = part - kPart;
            if (Virtual<uint8_t>(self, 0x3C, uint32_t{0x10}))
                Virtual<void>(self, 0x78);
        }
    } else {
        At<uint32_t>(part, field::kPartFlags - kPart) &= ~mask;
    }
}

// The game's 0x0042F7D0 (slot 14 of the part): whether any of the bits is
// set.
uint32_t __fastcall HasPartFlags(char* part, void*, uint32_t mask)
{
    return (At<uint32_t>(part, field::kPartFlags - kPart) & mask) != 0 ? 1 : 0;
}

// The game's 0x0042F7E0 (main slot 0): the advice's own ID and the part's
// give this object and the part; the rest is the base class's.
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** out)
{
    if (iid == kAdviceIid) {
        *out = self;
        AddRef(self);
        return 1;
    }
    if (iid == kPartIid) {
        *out = self ? self + kPart : nullptr;
        AddRef(self);
        return 1;
    }
    return This<uint8_t>(addr::SC4Advice_QueryInterface, self, iid, out);
}

// The game's 0x0042F880 (slot 7 of the part): the number of icons.
int32_t __fastcall GetIconCount(char* part, void*)
{
    return HolderCount(At<char*>(part, 0x4), At<char*>(part, 0x8));
}

// The game's 0x0042F8A0 (slot 8 of the part): the loaded image of icon
// `index`, or null.
uint32_t __fastcall GetIcon(char* part, void*, uint32_t index)
{
    const int32_t count = HolderCount(At<char*>(part, 0x4), At<char*>(part, 0x8));
    if (index < static_cast<uint32_t>(count) && static_cast<int32_t>(index) >= 0)
        return At<uint32_t>(At<char*>(part, 0x4) + index * kHolder, 0x4);
    return 0;
}

// The game's 0x0042F8E0 (slot 9 of the part): the instance ID of icon
// `index`'s image key, or 0.
uint32_t __fastcall GetIconInstance(char* part, void*, uint32_t index)
{
    const int32_t count = HolderCount(At<char*>(part, 0x4), At<char*>(part, 0x8));
    if (index < static_cast<uint32_t>(count) && static_cast<int32_t>(index) >= 0) {
        uint32_t key[3] = {0, 0, 0};
        This<void>(addr::SC4ImageLoader_GetKey, At<char*>(part, 0x4) + index * kHolder, key);
        return key[2];
    }
    return 0;
}

// --- Construction and destruction -------------------------------------------------------

// The game's 0x0042F950 (no arguments). Writes the vtables again, frees the
// message text and the icon holders (their virtual destructor, without
// freeing them) and the vector, then the base class's destructor.
void __fastcall Destruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0x0) = addr::cSC4MySimBalloonAdvice_Vtable;
    At<std::uintptr_t>(self, 0x4) = addr::cSC4MySimBalloonAdvice_Vtable4;
    At<std::uintptr_t>(self, 0xC) = addr::cSC4MySimBalloonAdvice_VtableC;
    At<std::uintptr_t>(self, kPart) = addr::cSC4MySimBalloonAdvice_VtableImages;
    At<std::uintptr_t>(self, field::kMessage) = addr::SC4String_Vtable;
    if (char* const text = At<char*>(self, field::kMessage + 0x4))
        Free(text);
    char* const end = At<char*>(self, field::kIcons + 0x4);
    for (char* item = At<char*>(self, field::kIcons); item != end; item += kHolder)
        Virtual<void>(item, 0x0, uint32_t{0});
    if (char* const begin = At<char*>(self, field::kIcons))
        Free(begin);
    This<void>(addr::SC4Advice_Destruct, self);
}

// The game's 0x0042FA70. The constructor: the base class's, then the
// vtables (the part's first with a placeholder one, as the original does),
// an empty icon vector, the message text and zeroed sound fields. Returns
// this. It draws no random numbers.
char* __fastcall Construct(char* self, void*)
{
    This<void*>(addr::SC4Advice_Construct, self);
    At<std::uintptr_t>(self, kPart) = addr::cSC4MySimBalloonAdvice_VtableConstruct;
    At<std::uintptr_t>(self, 0x0) = addr::cSC4MySimBalloonAdvice_Vtable;
    At<std::uintptr_t>(self, 0x4) = addr::cSC4MySimBalloonAdvice_Vtable4;
    At<std::uintptr_t>(self, 0xC) = addr::cSC4MySimBalloonAdvice_VtableC;
    At<std::uintptr_t>(self, kPart) = addr::cSC4MySimBalloonAdvice_VtableImages;
    At<char*>(self, field::kIcons) = nullptr;
    At<char*>(self, field::kIcons + 0x4) = nullptr;
    At<char*>(self, field::kIcons + 0x8) = nullptr;
    This<void*>(addr::SC4LocalizedString_ConstructBase, self + field::kMessage);
    At<std::uintptr_t>(self, field::kMessage) = addr::SC4LocalizedString_Vtable;
    At<uint32_t>(self, 0xF0) = 0;
    At<uint32_t>(self, field::kPartFlags) = 0;
    At<uint32_t>(self, field::kSoundMale) = 0;
    At<uint32_t>(self, field::kSoundFemale) = 0;
    At<uint32_t>(self, field::kSoundChance) = 0;
    return self;
}

// The game's 0x0042FAF0 (main slot 3, through the thunk 0x0042FA00).
// The scalar deleting destructor; returns this.
char* __fastcall DeletingDestruct(char* self, void*, uint32_t flags)
{
    This<void>(addr::cSC4MySimBalloonAdvice_Destruct, self);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// The game's 0x0042FB10 (cdecl, no arguments; the class factory, address
// stored for the class registration). Returns the balloon part of a new
// object, or null.
void* __cdecl Create()
{
    char* const memory = Cdecl<char*>(addr::SC4_operator_new, std::size_t{0x104});
    if (!memory)
        return nullptr;
    char* const advice = This<char*>(addr::cSC4MySimBalloonAdvice_Construct, memory);
    if (!advice)
        return nullptr;
    return advice + kPart;
}

// --- The icon holder and the vector of holders -------------------------------------------

// The game's 0x0042FB40 (the holder's copy constructor, 0x30 bytes); `this`
// is the new holder. Copies the image (counted), the key, the file name
// (built from the source's buffer) and the mode and flag. Returns this.
char* __fastcall HolderCopy(char* self, void*, char* source)
{
    uint32_t tag; // the original passes the address of its argument slot
    At<std::uintptr_t>(self, 0x0) = addr::SC4ImageLoader_Vtable;
    void* const image = At<void*>(source, 0x4);
    At<void*>(self, 0x4) = image;
    if (image)
        AddRef(image);
    At<uint32_t>(self, 0x8) = At<uint32_t>(source, 0x8);
    At<uint32_t>(self, 0xC) = At<uint32_t>(source, 0xC);
    At<uint32_t>(self, 0x10) = At<uint32_t>(source, 0x10);
    This<void>(addr::SC4String_CopyBuffer, self + 0x18, source + 0x18, uint32_t{0}, uint32_t{0xFFFFFFFF}, &tag);
    At<std::uintptr_t>(self, 0x14) = addr::SC4String_Vtable;
    At<uint32_t>(self, 0x24) = 0;
    At<uint32_t>(self, 0x28) = At<uint32_t>(source, 0x28);
    At<uint8_t>(self, 0x2C) = At<uint8_t>(source, 0x2C);
    return self;
}

// The game's 0x0042FBB0 (slot 0 of the holder's vtable 0x00A856CC): the
// deleting destructor; returns this.
char* __fastcall HolderDeletingDestruct(char* self, void*, uint32_t flags)
{
    This<void>(addr::SC4ImageLoader_Destruct, self);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// The game's 0x0042FBD0 (cdecl; first, last, destination, tag): copy
// constructs the holders [first, last) at the destination; returns the end
// of the destination.
char* __cdecl HolderCopyRange(char* first, char* last, char* destination, void*)
{
    for (; first != last; first += kHolder, destination += kHolder) {
        if (destination)
            This<char*>(addr::SC4ImageLoader_CopyConstruct, destination, first);
    }
    return destination;
}

// The game's 0x0042FC00 (the vector's insert with reallocation; `this` is
// the vector; position, value, a tag, the count, whether the position is the
// end). The new capacity is size + max(size, count); the holders before the
// position, `count` copies of the value, and (unless at the end) the rest are
// copied into the new block; then the old holders are destroyed and the
// old block is freed.
void __fastcall HolderInsertOverflow(char* self, void*, char* position, char* value, void*, uint32_t count,
                                     uint8_t atEnd)
{
    const uint32_t size = static_cast<uint32_t>(HolderCount(At<char*>(self, 0x0), At<char*>(self, 0x4)));
    const uint32_t grow = size < count ? count : size;
    const uint32_t capacity = grow + size;
    char* const block = capacity ? static_cast<char*>(Allocate(capacity * kHolder)) : nullptr;
    char* cursor = block;
    for (char* item = At<char*>(self, 0x0); item != position; item += kHolder, cursor += kHolder) {
        if (cursor)
            This<char*>(addr::SC4ImageLoader_CopyConstruct, cursor, item);
    }
    for (uint32_t i = 0; i < count; ++i, cursor += kHolder) {
        if (cursor)
            This<char*>(addr::SC4ImageLoader_CopyConstruct, cursor, value);
    }
    char* newEnd = cursor;
    if (!atEnd) {
        uint32_t copyTag = 0; // the original passes the address of its argument slot
        newEnd = Cdecl<char*>(addr::SC4ImageLoaderVector_UninitializedCopy, position, At<char*>(self, 0x4), cursor,
                              &copyTag);
    }
    char* const oldEnd = At<char*>(self, 0x4);
    for (char* item = At<char*>(self, 0x0); item != oldEnd; item += kHolder)
        Virtual<void>(item, 0x0, uint32_t{0});
    if (char* const old = At<char*>(self, 0x0))
        Free(old);
    At<char*>(self, 0x4) = newEnd;
    At<char*>(self, 0x0) = block;
    At<char*>(self, 0x8) = block + capacity * kHolder;
}

// The game's 0x0042FD30 (the holder's assignment; `this` is the target).
// Takes the source's image (counted, the old one released), key, file name,
// mode and flag. Returns this.
char* __fastcall HolderAssign(char* self, void*, char* source)
{
    uint32_t tag; // the original passes a stack address
    void** const target = &At<void*>(self, 0x4);
    void** const from = &At<void*>(source, 0x4);
    if (from != target) {
        void* const old = *target;
        void* const image = *from;
        if (image != old) {
            if (image)
                AddRef(image);
            *target = image;
            if (old)
                Release(old);
        }
    }
    At<uint32_t>(self, 0x8) = At<uint32_t>(source, 0x8);
    At<uint32_t>(self, 0xC) = At<uint32_t>(source, 0xC);
    At<uint32_t>(self, 0x10) = At<uint32_t>(source, 0x10);
    StringBody* const name = reinterpret_cast<StringBody*>(source + 0x18);
    if (name != reinterpret_cast<StringBody*>(self + 0x18))
        This<void>(addr::SC4String_AssignRange, self + 0x18, name->begin, name->end, &tag);
    At<uint32_t>(self, 0x28) = At<uint32_t>(source, 0x28);
    At<uint8_t>(self, 0x2C) = At<uint8_t>(source, 0x2C);
    return self;
}

// The game's 0x0042FDC0 (cdecl; first, last, destination and two arguments
// that are not read): assigns the holders [first, last) to the destination;
// returns the end of the destination.
char* __cdecl HolderAssignRange(char* first, char* last, char* destination, void*, uint32_t)
{
    int32_t count = HolderCount(first, last);
    if (count > 0) {
        do {
            This<char*>(addr::SC4ImageLoader_Assign, destination, first);
            first += kHolder;
            destination += kHolder;
        } while (--count != 0);
        return destination;
    }
    return destination;
}

// The game's 0x0042FE10 (the vector's erase of [first, last); `this` is the
// vector). The holders after the range are assigned down, the leftover
// holders at the end are destroyed and the end moves. Returns first.
char* __fastcall HolderErase(char* self, void*, char* first, char* last)
{
    uint32_t tag; // the original passes a stack address
    char* const newEnd = Cdecl<char*>(addr::SC4ImageLoaderVector_AssignRange, last, At<char*>(self, 0x4), first, &tag,
                                      uint32_t{0});
    char* const oldEnd = At<char*>(self, 0x4);
    for (char* item = newEnd; item != oldEnd; item += kHolder)
        Virtual<void>(item, 0x0, uint32_t{0});
    At<char*>(self, 0x4) = newEnd;
    return first;
}

// --- Init from the script table ------------------------------------------------------------

// The game's 0x0042FE60 (main slot 3, ret 4). Reads entry `index` of the
// script's "advices": the base class's Init first, then "balloon_message"
// (the message text key), up to nine icons "balloon_icon1" to
// "balloon_icon9" (each a number or string naming an image of type
// 0x856DDBAC, group 0xAB7E5421, with magenta as its transparent colour),
// and the sound keys "balloon_sound", "_m" and "_f" and the chance. True
// when the base class succeeded and there is an icon or a message text.
// The game's own failure paths (the base Init failing) do not stop the rest
// from running; the table not opening does.
uint8_t __fastcall Init(char* self, void*, int32_t index)
{
    uint8_t baseOk = This<uint8_t>(addr::SC4Advice_Init, self, index); // also its tag address below
    void* const script = Virtual<void*>(Global<void*>(addr::cSC4AdvisorSystem_Instance), 0x28);
    ScriptTable table;
    This<void*>(addr::SC4ScriptTable_Open, &table, script, Global<const char*>(addr::SC4BalloonNames_Advices), index);
    if (!table.open) {
        DestroyString(table.text);
        return 0;
    }

    // The message text: a game string made by hand, then the table's text
    // assigned to it.
    GameString message;
    message.body.begin = nullptr;
    message.body.end = nullptr;
    message.body.capacity = nullptr;
    char* const buffer = static_cast<char*>(Allocate(8));
    message.body.begin = buffer;
    message.body.end = buffer;
    message.body.capacity = buffer + 8;
    *buffer = 0;
    message.vtable = addr::SC4String_Vtable;
    message.zero = 0;
    void* const found = This<void*>(addr::SC4ScriptTable_GetString, &table,
                                    reinterpret_cast<const char*>(addr::SC4BalloonNames_Message));
    StringBody* const source = found ? reinterpret_cast<StringBody*>(static_cast<char*>(found) + 0x4) : nullptr;
    if (source != &message.body)
        This<void>(addr::SC4String_AssignRange, &message.body, source->begin, source->end, &baseOk);
    This<void>(addr::SC4LocalizedString_SetKeyText, self + field::kMessage,
               This<const char*>(addr::cRZBaseString_Data, &message));

    // The icons.
    char* const icons = self + field::kIcons;
    This<char*>(addr::SC4ImageLoaderVector_Erase, icons, At<char*>(icons, 0x0), At<char*>(icons, 0x4));
    char key[14] = "balloon_icon0";
    for (int32_t number = 1; number <= kIconCount; ++number) {
        key[12] = static_cast<char>('0' + number);
        if (!table.open)
            continue;
        Virtual<void>(table.script, 0x84, static_cast<const char*>(key)); // push the field
        Virtual<void>(table.script, 0x98, int32_t{-2});                   // GetTable
        const bool usable = Virtual<int32_t>(table.script, 0x30, int32_t{-1}) == 3 ||
                            Virtual<int32_t>(table.script, 0x30, int32_t{-1}) == 4; // number or string
        Virtual<void>(table.script, 0x2C, int32_t{1});                              // Pop
        if (!usable || !table.open)
            continue;
        Virtual<void>(table.script, 0x84, static_cast<const char*>(key));
        Virtual<void>(table.script, 0x98, int32_t{-2});
        const int32_t instance = Virtual<int32_t>(table.script, 0x58, int32_t{-1}); // ToInteger
        Virtual<void>(table.script, 0x2C, int32_t{1});
        if (!instance)
            continue;
        ImageLoader loader;
        This<void*>(addr::SC4ImageLoader_Construct, &loader, uint32_t{0});
        This<void>(addr::SC4ImageLoader_SetKey, &loader, kIconType, kIconGroup, static_cast<uint32_t>(instance));
        if (This<uint8_t>(addr::SC4ImageLoader_Load, &loader) && loader.image) {
            void* const image = loader.image;
            const uint32_t magenta = Virtual<uint32_t>(image, 0x78, uint32_t{0xFF}, uint32_t{0}, uint32_t{0xFF});
            Virtual<void>(image, 0x5C, magenta);
            if (At<char*>(icons, 0x4) != At<char*>(icons, 0x8)) {
                if (At<char*>(icons, 0x4))
                    This<char*>(addr::SC4ImageLoader_CopyConstruct, At<char*>(icons, 0x4),
                                reinterpret_cast<char*>(&loader));
                At<char*>(icons, 0x4) += kHolder;
            } else {
                This<void>(addr::SC4ImageLoaderVector_InsertOverflow, icons, At<char*>(icons, 0x4),
                           reinterpret_cast<char*>(&loader), &baseOk, uint32_t{1}, uint8_t{1});
            }
        }
        This<void>(addr::SC4ImageLoader_Destruct, &loader);
    }

    uint8_t result = 0;
    if (baseOk) {
        if (At<char*>(icons, 0x0) != At<char*>(icons, 0x4))
            result = 1;
        else if (message.body.begin != message.body.end)
            result = 1;
    }

    // The sounds: one key for both, then the male and the female key, and
    // the chance.
    int32_t both = 0;
    if (This<uint8_t>(addr::SC4ScriptTable_Has, &table, "balloon_sound"))
        both = This<int32_t>(addr::SC4ScriptTable_GetInt, &table, "balloon_sound");
    At<int32_t>(self, field::kSoundFemale) = both;
    At<int32_t>(self, field::kSoundMale) = both;
    if (This<uint8_t>(addr::SC4ScriptTable_Has, &table, "balloon_sound_m"))
        At<int32_t>(self, field::kSoundMale) = This<int32_t>(addr::SC4ScriptTable_GetInt, &table, "balloon_sound_m");
    if (This<uint8_t>(addr::SC4ScriptTable_Has, &table, "balloon_sound_f"))
        At<int32_t>(self, field::kSoundFemale) = This<int32_t>(addr::SC4ScriptTable_GetInt, &table, "balloon_sound_f");
    if (This<uint8_t>(addr::SC4ScriptTable_Has, &table, "balloon_sound_chance"))
        At<int32_t>(self, field::kSoundChance) =
            This<int32_t>(addr::SC4ScriptTable_GetInt, &table, "balloon_sound_chance");

    // The message text joins the base's text vector, to be loaded with the
    // advice's own texts.
    char* const texts = self + field::kTexts;
    TextField item;
    item.name = reinterpret_cast<const char*>(addr::SC4BalloonNames_Message);
    item.text = self + field::kMessage;
    if (At<char*>(texts, 0x4) != At<char*>(texts, 0x8)) {
        if (TextField* const end = At<TextField*>(texts, 0x4)) {
            end->name = item.name;
            end->text = item.text;
        }
        At<char*>(texts, 0x4) += sizeof(TextField);
    } else {
        This<void>(addr::SC4PointVector_InsertN, texts, At<char*>(texts, 0x4), &item, &baseOk, uint32_t{1},
                   uint8_t{1});
    }

    message.vtable = addr::SC4String_Vtable;
    if (message.body.begin)
        Free(message.body.begin);
    if (table.open)
        Virtual<void>(table.script, 0x1C, table.top); // SetTop
    DestroyString(table.text);
    return result;
}

bool InstallBalloonAdvice(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[24] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSC4MySimBalloonAdvice_GetGZCLSID, reinterpret_cast<void*>(&GetGZCLSID)},
        {addr::cSC4MySimBalloonAdvice_IsTriggered, reinterpret_cast<void*>(&IsTriggered)},
        {addr::cSC4MySimBalloonAdvice_AsAdvice, reinterpret_cast<void*>(&AsAdvice)},
        {addr::cSC4MySimBalloonAdvice_GetSoundKey, reinterpret_cast<void*>(&GetSoundKey)},
        {addr::cSC4MySimBalloonAdvice_SetPartFlags, reinterpret_cast<void*>(&SetPartFlags)},
        {addr::cSC4MySimBalloonAdvice_HasPartFlags, reinterpret_cast<void*>(&HasPartFlags)},
        {addr::cSC4MySimBalloonAdvice_QueryInterface, reinterpret_cast<void*>(&QueryInterface)},
        {addr::cSC4MySimBalloonAdvice_GetIconCount, reinterpret_cast<void*>(&GetIconCount)},
        {addr::cSC4MySimBalloonAdvice_GetIcon, reinterpret_cast<void*>(&GetIcon)},
        {addr::cSC4MySimBalloonAdvice_GetIconInstance, reinterpret_cast<void*>(&GetIconInstance)},
        {addr::cSC4MySimBalloonAdvice_Destruct, reinterpret_cast<void*>(&Destruct)},
        {addr::cSC4MySimBalloonAdvice_Construct, reinterpret_cast<void*>(&Construct)},
        {addr::cSC4MySimBalloonAdvice_DeletingDestruct, reinterpret_cast<void*>(&DeletingDestruct)},
        {addr::cSC4MySimBalloonAdvice_Create, reinterpret_cast<void*>(&Create)},
        {addr::SC4ImageLoader_CopyConstruct, reinterpret_cast<void*>(&HolderCopy)},
        {addr::SC4ImageLoader_DeletingDestruct, reinterpret_cast<void*>(&HolderDeletingDestruct)},
        {addr::SC4ImageLoaderVector_UninitializedCopy, reinterpret_cast<void*>(&HolderCopyRange)},
        {addr::SC4ImageLoaderVector_InsertOverflow, reinterpret_cast<void*>(&HolderInsertOverflow)},
        {addr::SC4ImageLoader_Assign, reinterpret_cast<void*>(&HolderAssign)},
        {addr::SC4ImageLoaderVector_AssignRange, reinterpret_cast<void*>(&HolderAssignRange)},
        {addr::SC4ImageLoaderVector_Erase, reinterpret_cast<void*>(&HolderErase)},
        {addr::cSC4MySimBalloonAdvice_Init, reinterpret_cast<void*>(&Init)},
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

const bool g_balloonAdviceRegistered = registry::Register({
    "simulators.mysim_agents.balloon_advice",
    "simulators/mysim_agents",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4MySimBalloonAdvice_Init,
    "Our cSC4MySimBalloonAdvice: the balloon icons, sound keys and Init from the script table (docs/mysim-agents.md)",
    false,
    InstallBalloonAdvice,
});

} // namespace
} // namespace opensc4::mysim_agents
