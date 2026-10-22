// The fourth part of GZSound's music player (docs/audio.md, "Music player"):
// the playlist reader and the container code it and the stations use. Part 3
// (music_3.cpp) has the station class: its slot 25 reads every .m3u and .pls
// name of a list with the reader here, and its slot 18 sorts a list's names
// with the sort here or shuffles them.
//
// Replacement, off by default:
//
//   audio.music.part4: 49 functions.
//   - The reader (0x48 bytes: a vector of entries, the playlist's full path
//     +0x0C, its folder +0x20 and its text +0x34): the constructor
//     0x008D569F, the destructor 0x008D56C8, Read 0x008D6B22, the three
//     formats (#EXTM3U 0x008D64E3, [playlist] 0x008D6824, a plain list
//     0x008D66C9), the line reader 0x008D6258 with its two searches
//     0x008D5F5B and 0x008D5FDC, and the path code 0x008D6352 (a name made
//     a full path from the folder), 0x008D6123, 0x008D6196, 0x008D6056 and
//     0x008D60C4.
//   - The entries (0x2C bytes: a title, a path, a length in seconds) and
//     their vector (STLport): 0x008D6171, 0x008D3C38, 0x008D50DB,
//     0x008D50B2, 0x008D6230, 0x008D51E3, 0x008D51FC, 0x008D521A,
//     0x008D5245, 0x008D54C2, 0x008D56EB, 0x008D63F3, 0x008D64AB.
//   - The sort of a vector of names by their files' times, newest first
//     (STLport's introsort with a comparator that holds the folder by
//     value): 0x008D5E50 and the 20 functions under it, and the shuffle of
//     names 0x008D546F.
//
// As parts 1 to 3, the group goes in when core.dll is injected (phase
// Attach). Every one of these is called only from the music player
// (0x008D2994 to 0x008D7FFF), and none is in a vtable.
//
// Still the game's, called by address: the allocation 0x008D3376 (also
// called from 0x00875337), the string, file and path code, the strings'
// copy_backward 0x005C82C0 and an entry's two strings destroyed 0x0091C947
// (both shared).
//
// Fix, on by default:
//
//   fixes.playlist_last_line: the line reader 0x008D6258 alone (ours), taking
//   a last line with no line break off the text, where the game hung.
#include "core/address_check.h"
#include "core/fixed_seed.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace opensc4::audio {
namespace {

constexpr uint32_t kAll = 0xFFFFFFFFu;

// The game's string body and string (SC4String, 0x14 bytes). Trivially
// copyable, so that a string passed by value has the game's layout on the
// stack; the function that takes one frees its text, as the game's do.
struct Body {
    char* begin;
    char* end;
    char* capacity;
};
static_assert(sizeof(Body) == 0xC);

struct GameString {
    std::uintptr_t vtable;
    Body body;
    uint32_t zero;
};
static_assert(sizeof(GameString) == 0x14);

// A playlist entry.
struct Entry {
    GameString title;
    GameString path;
    int32_t length;
};
static_assert(sizeof(Entry) == 0x2C);

struct Entries {
    Entry* begin;
    Entry* end;
    Entry* capacity;
};

struct Reader {
    Entries entries;
    GameString path;   // the playlist's full path
    GameString folder; // its folder
    GameString text;   // what is left to read
};
static_assert(sizeof(Reader) == 0x48);

// The file object (0x80 bytes), as the game has it on its stack.
struct File {
    alignas(4) char bytes[0x80];
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

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("music playlist: first %s", name);
}

#define OPENSC4_FIRST(name)          \
    static bool logged = false;      \
    FirstCall(name, logged)

void FreeText(char* begin)
{
    if (begin)
        Cdecl<void>(addr::SC4_Free, begin);
}

void Construct(GameString& text)
{
    This<void*>(addr::SC4String_SC4String, &text);
}

void FromText(GameString& text, const char* source)
{
    This<void*>(addr::SC4String_FromText, &text, source);
}

// A copy (0x00408BE0 with 0 and -1).
GameString Copy(const void* source)
{
    GameString text;
    This<void*>(addr::SC4String_Substring, &text, source, uint32_t{0}, kAll);
    return text;
}

// The end of a string's life on the game's stack: the vtable put back and
// the text freed.
void Drop(GameString& text)
{
    text.vtable = addr::SC4String_Vtable;
    FreeText(text.body.begin);
}

void Assign(void* target, const void* source)
{
    This<void*>(addr::cRZBaseString_Assign, target, source);
}

// The body of a string, or null for no string.
Body* BodyOf(void* text)
{
    return text ? reinterpret_cast<Body*>(static_cast<char*>(text) + 4) : nullptr;
}

// `first` followed by `second` (0x00408AE0 then 0x00408B90; the joined body
// freed).
GameString Joined(Body* first, Body* second)
{
    Body joined{};
    Body* const made = Cdecl<Body*>(addr::cSC4Advice_F408AE0, &joined, first, second);
    GameString text;
    This<void*>(addr::cSC4Advice_F408B90, &text, made);
    FreeText(joined.begin);
    return text;
}

uint8_t BeginsWith(void* text, const char* start, uint32_t length)
{
    return This<uint8_t>(addr::cRZBaseString_BeginsWith, text, start, length);
}

uint32_t Strlen(void* text)
{
    return This<uint32_t>(addr::cRZBaseString_Strlen, text);
}

const char* Data(void* text)
{
    return This<const char*>(addr::cRZBaseString_Data, text);
}

int32_t Find(void* text, const char* what)
{
    return This<int32_t>(addr::SC4String_Find, text, what, uint32_t{0}, uint32_t{1});
}

void Erase(void* text, uint32_t position, uint32_t count)
{
    This<void>(addr::cRZBaseString_Erase, text, position, count);
}

// The string's virtual functions (vtable 0x00A80810) the reader uses.
namespace slot {
constexpr std::size_t kText = 0x14;
constexpr std::size_t kChars = 0x18;
constexpr std::size_t kLength = 0x1C;
constexpr std::size_t kAssign = 0x34;
constexpr std::size_t kResize = 0x3C;
constexpr std::size_t kAppend = 0x40;
constexpr std::size_t kErase = 0x58;
constexpr std::size_t kFindLast = 0x68;
} // namespace slot

// ---- Entries ---------------------------------------------------------------------

// The game's 0x008D6171 (thiscall, ret 0xC): an entry from a length and two
// cIGZStrings.
Entry* __fastcall ConstructEntry(Entry* self, void*, int32_t length, void* title, void* path)
{
    OPENSC4_FIRST("ConstructEntry");
    This<void*>(addr::SC4String_FromIGZString, &self->title, title);
    This<void*>(addr::SC4String_FromIGZString, &self->path, path);
    self->length = length;
    return self;
}

// The game's 0x008D3C38 (thiscall, ret 4): an entry copied.
Entry* __fastcall CopyEntry(Entry* self, void*, const Entry* source)
{
    OPENSC4_FIRST("CopyEntry");
    This<void*>(addr::SC4String_Substring, &self->title, &source->title, uint32_t{0}, kAll);
    This<void*>(addr::SC4String_Substring, &self->path, &source->path, uint32_t{0}, kAll);
    self->length = source->length;
    return self;
}

// The game's 0x008D50DB (thiscall, ret 4): an entry assigned.
Entry* __fastcall AssignEntry(Entry* self, void*, const Entry* source)
{
    OPENSC4_FIRST("AssignEntry");
    Assign(&self->title, &source->title);
    Assign(&self->path, &source->path);
    self->length = source->length;
    return self;
}

// The game's 0x008D50B2 (cdecl): entries copied into raw memory; a null
// place is passed over.
Entry* __cdecl CopyInto(const Entry* first, const Entry* last, Entry* result, void*)
{
    OPENSC4_FIRST("CopyInto");
    for (; first != last; ++first, ++result) {
        if (result)
            CopyEntry(result, nullptr, first);
    }
    return result;
}

// The game's 0x008D6230 (cdecl): `count` copies of an entry into raw memory.
Entry* __cdecl FillInto(Entry* first, uint32_t count, const Entry* value, void*)
{
    OPENSC4_FIRST("FillInto");
    for (; count != 0; --count, ++first) {
        if (first)
            CopyEntry(first, nullptr, value);
    }
    return first;
}

// The game's 0x008D51E3 (cdecl): entries destroyed.
void __cdecl DestroyEntries(Entry* first, Entry* last)
{
    OPENSC4_FIRST("DestroyEntries");
    for (; first != last; ++first)
        This<void>(addr::SC4StringPair_Destruct, first);
}

// The game's 0x008D51FC (thiscall): the vector's entries destroyed and its
// memory freed.
void __fastcall DestructEntries(Entries* self, void*)
{
    OPENSC4_FIRST("DestructEntries");
    DestroyEntries(self->begin, self->end);
    if (self->begin)
        FreeText(reinterpret_cast<char*>(self->begin));
}

Entry* Allocate(Entries* self, uint32_t count)
{
    return This<Entry*>(addr::GZPlaylistEntries_Allocate, reinterpret_cast<char*>(self) + 8, count);
}

// The game's 0x008D521A (thiscall, ret 0xC): memory for `count` entries
// with a range copied into it.
Entry* __fastcall AllocateCopy(Entries* self, void*, uint32_t count, const Entry* first, const Entry* last)
{
    OPENSC4_FIRST("AllocateCopy");
    Entry* const made = Allocate(self, count);
    uint8_t tag = 0;
    CopyInto(first, last, made, &tag);
    return made;
}

// The game's 0x008D5245 (cdecl): entries assigned over others.
Entry* __cdecl AssignRange(const Entry* first, const Entry* last, Entry* result, void*, uint32_t)
{
    OPENSC4_FIRST("AssignRange");
    for (int32_t count = static_cast<int32_t>(last - first); count > 0; --count, ++first, ++result)
        AssignEntry(result, nullptr, first);
    return result;
}

// The game's 0x008D54C2 (cdecl): the same.
Entry* __cdecl AssignOver(const Entry* first, const Entry* last, Entry* result, void*)
{
    OPENSC4_FIRST("AssignOver");
    uint8_t tag = 0;
    return AssignRange(first, last, result, &tag, 0);
}

// The game's 0x008D56EB (thiscall, ret 4): a vector of entries assigned
// from another (part 3 takes a reader's entries with it).
Entries* __fastcall AssignEntries(Entries* self, void*, const Entries* other)
{
    OPENSC4_FIRST("AssignEntries");
    if (other == self)
        return self;
    const Entry* const otherBegin = other->begin;
    const Entry* const otherEnd = other->end;
    const uint32_t count = static_cast<uint32_t>(otherEnd - otherBegin);
    const uint32_t capacity = static_cast<uint32_t>(self->capacity - self->begin);
    if (count > capacity) {
        Entry* const made = AllocateCopy(self, nullptr, count, otherBegin, otherEnd);
        DestructEntries(self, nullptr);
        self->begin = made;
        self->capacity = made + count;
    } else {
        uint8_t tag = 0;
        const uint32_t size = static_cast<uint32_t>(self->end - self->begin);
        if (size >= count) {
            Entry* const at = AssignOver(otherBegin, otherEnd, self->begin, &tag);
            DestroyEntries(at, self->end);
        } else {
            AssignOver(otherBegin, otherBegin + size, self->begin, &tag);
            CopyInto(other->begin + (self->end - self->begin), other->end, self->end, &tag);
        }
    }
    self->end = self->begin + count;
    return self;
}

// The game's 0x008D63F3 (thiscall, ret 0x14): an insert into a full vector:
// a new block of the size plus the greater of the size and `count`.
void __fastcall InsertGrown(Entries* self, void*, Entry* position, const Entry* value, void*, uint32_t count,
                            uint8_t atEnd)
{
    OPENSC4_FIRST("InsertGrown");
    const uint32_t size = static_cast<uint32_t>(self->end - self->begin);
    const uint32_t capacity = size + (size < count ? count : size);
    Entry* const made = Allocate(self, capacity);
    uint8_t tag = 0;
    Entry* at = CopyInto(self->begin, position, made, &tag);
    if (count == 1) {
        if (at)
            CopyEntry(at, nullptr, value);
        ++at;
    } else {
        at = FillInto(at, count, value, &tag);
    }
    if (!atEnd)
        at = CopyInto(position, self->end, at, &tag);
    DestructEntries(self, nullptr);
    self->begin = made;
    self->end = at;
    self->capacity = made + capacity;
}

// The game's 0x008D64AB (thiscall, ret 4): push_back.
void __fastcall PushBack(Entries* self, void*, const Entry* value)
{
    OPENSC4_FIRST("PushBack");
    Entry* const end = self->end;
    if (end == self->capacity) {
        uint8_t tag = 0;
        InsertGrown(self, nullptr, end, value, &tag, 1, 1);
        return;
    }
    if (end)
        CopyEntry(end, nullptr, value);
    ++self->end;
}

// ---- The sort of names by their files' times ----------------------------------------------
//
// Part 3's slot 18 sorts a list's names with 0x008D5E50 when the player's
// option bit is set. The comparator holds the folder (a string, passed by
// value down every call and freed by each callee) and puts a name before
// another when the folder plus that name has the later modification time.

// The game's 0x008D3C67 (thiscall on the comparator, ret 8): whether
// `first`'s file is newer than `second`'s (RZFile 0x00918CB1 with 2: the
// modification time; 0 for no file).
uint8_t __fastcall Newer(GameString* folder, void*, GameString* first, GameString* second)
{
    OPENSC4_FIRST("Newer");
    Body* const base = BodyOf(folder);
    GameString firstPath = Joined(base, BodyOf(first));
    GameString secondPath = Joined(base, BodyOf(second));
    const int32_t secondTime = Cdecl<int32_t>(addr::RZFile_GetFileTime, &secondPath, uint32_t{2});
    const int32_t firstTime = Cdecl<int32_t>(addr::RZFile_GetFileTime, &firstPath, uint32_t{2});
    Drop(secondPath);
    Drop(firstPath);
    return firstTime > secondTime;
}

bool Before(GameString& order, GameString* first, GameString* second)
{
    return Newer(&order, nullptr, first, second) != 0;
}

// The game's 0x008D3DEF (cdecl): the median of three.
GameString* __cdecl Median(GameString* a, GameString* b, GameString* c, GameString order)
{
    OPENSC4_FIRST("Median");
    GameString* result;
    if (Before(order, a, b)) {
        if (Before(order, b, c))
            result = b;
        else if (Before(order, a, c))
            result = c;
        else
            result = a;
    } else {
        if (Before(order, a, c))
            result = a;
        else if (Before(order, b, c))
            result = c;
        else
            result = b;
    }
    FreeText(order.body.begin);
    return result;
}

// The game's 0x008D5277 (cdecl): the unguarded partition around a pivot.
GameString* __cdecl Partition(GameString* first, GameString* last, GameString pivot, GameString order)
{
    OPENSC4_FIRST("Partition");
    for (;;) {
        while (Before(order, first, &pivot))
            ++first;
        do
            --last;
        while (Before(order, &pivot, last));
        if (!(first < last))
            break;
        Cdecl<void>(addr::SC4String_Swap, first, last);
        ++first;
    }
    FreeText(pivot.body.begin);
    FreeText(order.body.begin);
    return first;
}

// The game's 0x008D5102 (cdecl): the unguarded linear insert of a value
// that was at `last`.
void __cdecl LinearInsertUnguarded(GameString* last, GameString value, GameString order)
{
    OPENSC4_FIRST("LinearInsertUnguarded");
    GameString* next = last - 1;
    while (Before(order, &value, next)) {
        Assign(last, next);
        last = next;
        --next;
    }
    Assign(last, &value);
    FreeText(value.body.begin);
    FreeText(order.body.begin);
}

// The game's 0x008D5158 (cdecl): push_heap's hole moved up.
void __cdecl PushHeap(GameString* first, int32_t hole, int32_t top, GameString value, GameString order)
{
    OPENSC4_FIRST("PushHeap");
    int32_t parent = (hole - 1) / 2;
    while (hole > top && Before(order, first + parent, &value)) {
        Assign(first + hole, first + parent);
        hole = parent;
        parent = (hole - 1) / 2;
    }
    Assign(first + hole, &value);
    FreeText(value.body.begin);
    FreeText(order.body.begin);
}

// The game's 0x008D5355 (cdecl): adjust_heap.
void __cdecl AdjustHeap(GameString* first, int32_t hole, int32_t length, GameString value, GameString order)
{
    OPENSC4_FIRST("AdjustHeap");
    const int32_t top = hole;
    int32_t child = 2 * hole + 2;
    while (child < length) {
        if (Before(order, first + child, first + (child - 1)))
            --child;
        Assign(first + hole, first + child);
        hole = child;
        child = 2 * child + 2;
    }
    if (child == length) {
        Assign(first + hole, first + (child - 1));
        hole = child - 1;
    }
    GameString orderCopy = Copy(&order);
    GameString valueCopy = Copy(&value);
    PushHeap(first, hole, top, valueCopy, orderCopy);
    FreeText(value.body.begin);
    FreeText(order.body.begin);
}

// The game's 0x008D5515 (cdecl): pop_heap's step: the first value to
// `result` and the held value put into the heap.
void __cdecl PopHeapStep(GameString* first, GameString* last, GameString* result, GameString value,
                         GameString order, void*)
{
    OPENSC4_FIRST("PopHeapStep");
    Assign(result, first);
    GameString orderCopy = Copy(&order);
    GameString valueCopy = Copy(&value);
    AdjustHeap(first, 0, static_cast<int32_t>(last - first), valueCopy, orderCopy);
    FreeText(value.body.begin);
    FreeText(order.body.begin);
}

// The game's 0x008D562B (cdecl): pop_heap.
void __cdecl PopHeapAux(GameString* first, GameString* last, void*, GameString order)
{
    OPENSC4_FIRST("PopHeapAux");
    GameString* const back = last - 1;
    GameString orderCopy = Copy(&order);
    GameString held = Copy(back);
    GameString value = Copy(&held);
    PopHeapStep(first, back, back, value, orderCopy, nullptr);
    Drop(held);
    FreeText(order.body.begin);
}

// The game's 0x008D57F1 (cdecl): pop_heap.
void __cdecl PopHeap(GameString* first, GameString* last, GameString order)
{
    OPENSC4_FIRST("PopHeap");
    PopHeapAux(first, last, nullptr, Copy(&order));
    FreeText(order.body.begin);
}

// The game's 0x008D5580 (cdecl): make_heap.
void __cdecl MakeHeapAux(GameString* first, GameString* last, GameString order, void*, void*)
{
    OPENSC4_FIRST("MakeHeapAux");
    const int32_t length = static_cast<int32_t>(last - first);
    if (length >= 2) {
        int32_t parent = (length - 2) / 2;
        GameString* at = first + parent;
        for (;;) {
            GameString orderCopy = Copy(&order);
            GameString held = Copy(at);
            GameString value = Copy(&held);
            AdjustHeap(first, parent, length, value, orderCopy);
            Drop(held);
            if (parent == 0)
                break;
            --parent;
            --at;
        }
    }
    FreeText(order.body.begin);
}

// The game's 0x008D57B9 (cdecl): make_heap.
void __cdecl MakeHeap(GameString* first, GameString* last, GameString order)
{
    OPENSC4_FIRST("MakeHeap");
    MakeHeapAux(first, last, Copy(&order), nullptr, nullptr);
    FreeText(order.body.begin);
}

// The game's 0x008D5B03 (cdecl): sort_heap.
void __cdecl SortHeap(GameString* first, GameString* last, GameString order)
{
    OPENSC4_FIRST("SortHeap");
    while (last - first > 1) {
        PopHeap(first, last, Copy(&order));
        --last;
    }
    FreeText(order.body.begin);
}

// The game's 0x008D5BBA (cdecl): partial_sort.
void __cdecl PartialSortAux(GameString* first, GameString* middle, GameString* last, void*, GameString order)
{
    OPENSC4_FIRST("PartialSortAux");
    MakeHeap(first, middle, Copy(&order));
    for (GameString* at = middle; at < last; ++at) {
        if (!Before(order, at, first))
            continue;
        GameString orderCopy = Copy(&order);
        GameString held = Copy(at);
        GameString value = Copy(&held);
        PopHeapStep(first, middle, at, value, orderCopy, nullptr);
        Drop(held);
    }
    SortHeap(first, middle, Copy(&order));
    FreeText(order.body.begin);
}

// The game's 0x008D5D06 (cdecl): partial_sort.
void __cdecl PartialSort(GameString* first, GameString* middle, GameString* last, GameString order)
{
    OPENSC4_FIRST("PartialSort");
    PartialSortAux(first, middle, last, nullptr, Copy(&order));
    FreeText(order.body.begin);
}

// The game's 0x008D5A7D (cdecl): the linear insert of the value that was at
// `last`: to the front (copy_backward 0x005C82C0) when it comes before the
// first, else the unguarded insert.
void __cdecl LinearInsert(GameString* first, GameString* last, GameString value, GameString order)
{
    OPENSC4_FIRST("LinearInsert");
    if (Before(order, &value, first)) {
        Cdecl<void*>(addr::SC4String_CopyBackward, first, last, last + 1);
        Assign(first, &value);
    } else {
        GameString orderCopy = Copy(&order);
        GameString valueCopy = Copy(&value);
        LinearInsertUnguarded(last, valueCopy, orderCopy);
    }
    FreeText(value.body.begin);
    FreeText(order.body.begin);
}

// The game's 0x008D5B62 (cdecl): insertion sort.
void __cdecl InsertionSort(GameString* first, GameString* last, GameString order)
{
    OPENSC4_FIRST("InsertionSort");
    if (first != last) {
        for (GameString* at = first + 1; at != last; ++at) {
            GameString orderCopy = Copy(&order);
            GameString value = Copy(at);
            LinearInsert(first, at, value, orderCopy);
        }
    }
    FreeText(order.body.begin);
}

// The game's 0x008D52DF (cdecl): the unguarded insertion sort.
void __cdecl InsertionSortUnguardedAux(GameString* first, GameString* last, void*, GameString order)
{
    OPENSC4_FIRST("InsertionSortUnguardedAux");
    for (GameString* at = first; at != last; ++at) {
        GameString orderCopy = Copy(&order);
        GameString held = Copy(at);
        GameString value = Copy(&held);
        LinearInsertUnguarded(at, value, orderCopy);
        Drop(held);
    }
    FreeText(order.body.begin);
}

// The game's 0x008D54DF (cdecl): the same.
void __cdecl InsertionSortUnguarded(GameString* first, GameString* last, GameString order)
{
    OPENSC4_FIRST("InsertionSortUnguarded");
    InsertionSortUnguardedAux(first, last, nullptr, Copy(&order));
    FreeText(order.body.begin);
}

// The game's 0x008D5C8F (cdecl): the final insertion sort (the first 16
// guarded, the rest unguarded).
void __cdecl FinalInsertionSort(GameString* first, GameString* last, GameString order)
{
    OPENSC4_FIRST("FinalInsertionSort");
    if (last - first > 16) {
        InsertionSort(first, first + 16, Copy(&order));
        InsertionSortUnguarded(first + 16, last, Copy(&order));
    } else {
        InsertionSort(first, last, Copy(&order));
    }
    FreeText(order.body.begin);
}

// The game's 0x008D5D3F (cdecl): introsort's loop; a heap sort once the
// depth runs out.
void __cdecl IntrosortLoop(GameString* first, GameString* last, void*, int32_t depth, GameString order)
{
    OPENSC4_FIRST("IntrosortLoop");
    while (last - first > 16) {
        if (depth == 0) {
            PartialSort(first, last, last, Copy(&order));
            break;
        }
        --depth;
        GameString partitionOrder = Copy(&order);
        GameString medianOrder = Copy(&order);
        GameString* const pick = Median(first, first + (last - first) / 2, last - 1, medianOrder);
        GameString held = Copy(pick);
        GameString pivot = Copy(&held);
        GameString* const cut = Partition(first, last, pivot, partitionOrder);
        Drop(held);
        IntrosortLoop(cut, last, nullptr, depth, Copy(&order));
        last = cut;
    }
    FreeText(order.body.begin);
}

// The game's 0x008D5E50 (cdecl, what part 3's slot 18 calls): sort, with a
// depth of twice the floor of log2 of the count.
void __cdecl SortByFileTime(GameString* first, GameString* last, GameString order)
{
    OPENSC4_FIRST("SortByFileTime");
    if (first != last) {
        GameString loopOrder = Copy(&order);
        int32_t count = static_cast<int32_t>(last - first);
        int32_t depth = 0;
        while (count != 1) {
            ++depth;
            count >>= 1;
        }
        IntrosortLoop(first, last, nullptr, depth * 2, loopOrder);
        FinalInsertionSort(first, last, Copy(&order));
    }
    FreeText(order.body.begin);
}

// The game's 0x008D546F (cdecl): the names shuffled (random_shuffle). The
// draws come from the sound code's own sequence, as the game's do under
// determinism.fixed_seed (src/core/fixed_seed.cpp).
void __cdecl ShuffleNames(GameString* first, GameString* last)
{
    OPENSC4_FIRST("ShuffleNames");
    if (first == last)
        return;
    GameString* at = first + 1;
    if (at == last)
        return;
    int32_t bytes = static_cast<int32_t>(reinterpret_cast<char*>(at) - reinterpret_cast<char*>(first));
    do {
        const int32_t drawn = fixed_seed::SoundRand();
        const int32_t pick = drawn % (bytes / 0x14 + 1);
        Cdecl<void>(addr::SC4String_Swap, at, first + pick);
        ++at;
        bytes += 0x14;
    } while (at != last);
}

// ---- The reader's text and paths -------------------------------------------------------------
//
// These are thiscall on the reader, which they do not use.

// The game's 0x008D5F5B (ret 0xC): the first character of `text` that is
// not in `set`; 0 when there is none.
uint8_t __fastcall FindNotOf(void*, void*, void* text, void* set, uint32_t* place)
{
    OPENSC4_FIRST("FindNotOf");
    const uint32_t length = Virtual<uint32_t>(text, slot::kLength);
    const uint32_t setLength = Virtual<uint32_t>(set, slot::kLength);
    *place = 0;
    if (length == 0)
        return 0;
    do {
        const char c = Virtual<const char*>(text, slot::kChars)[*place];
        bool found = false;
        if (setLength == 0)
            return 1;
        uint32_t i = 0;
        do {
            if (c == Virtual<const char*>(set, slot::kChars)[i])
                found = true;
            ++i;
        } while (i < setLength);
        if (!found)
            return 1;
        ++*place;
    } while (*place < length);
    return 0;
}

// The game's 0x008D5FDC (ret 0xC): the first character of `text` that is in
// `set`; 0 when there is none.
uint8_t __fastcall FindOf(void*, void*, void* text, void* set, uint32_t* place)
{
    OPENSC4_FIRST("FindOf");
    const uint32_t length = Virtual<uint32_t>(text, slot::kLength);
    const uint32_t setLength = Virtual<uint32_t>(set, slot::kLength);
    *place = 0;
    if (length == 0)
        return 0;
    do {
        const char c = Virtual<const char*>(text, slot::kChars)[*place];
        for (uint32_t i = 0; i < setLength; ++i) {
            if (c == Virtual<const char*>(set, slot::kChars)[i])
                return 1;
        }
        ++*place;
    } while (*place < length);
    return 0;
}

// The game's 0x008D6056 (ret 0xC): the first part of a path, up to and with
// the first backslash at or after `start` (all of it, and one more
// character, when there is none), taken from `text` into `out`.
uint8_t __fastcall SplitPart(void*, void*, void* text, void* out, uint32_t start)
{
    OPENSC4_FIRST("SplitPart");
    const std::uintptr_t data = reinterpret_cast<std::uintptr_t>(Virtual<const char*>(text, slot::kText));
    const std::uintptr_t end = data + Virtual<uint32_t>(text, slot::kLength);
    if (!data || !end || start > Virtual<uint32_t>(text, slot::kLength))
        return 0;
    std::uintptr_t at = data + start;
    while (at < end && *reinterpret_cast<const char*>(at) != '\\')
        ++at;
    const uint32_t taken = static_cast<uint32_t>(at - data) + 1;
    This<void*>(addr::cRZBaseString_AssignLimited, out, Virtual<const char*>(text, slot::kText), taken);
    Virtual<uint32_t>(text, slot::kErase, uint32_t{0}, taken);
    return 1;
}

// The game's 0x008D60C4 (ret 4): a path one folder up: a closing backslash
// taken off, then everything after the last backslash (all of it when there
// is none).
uint8_t __fastcall FolderUp(void*, void*, void* path)
{
    OPENSC4_FIRST("FolderUp");
    if (This<uint8_t>(addr::cRZBaseString_EndsWith, path, "\\", uint32_t{1}))
        Virtual<uint32_t>(path, slot::kResize, Virtual<uint32_t>(path, slot::kLength) - 1);
    const char* const data = Virtual<const char*>(path, slot::kText);
    if (!data)
        return 0;
    const int32_t last = Virtual<int32_t>(path, slot::kFindLast, "\\", kAll, uint32_t{1});
    if (last != 0 || *data == '\\')
        Virtual<uint32_t>(path, slot::kResize, static_cast<uint32_t>(last + 1));
    return 1;
}

// The game's 0x008D6123 (ret 8): a path's root taken into `out`: a drive
// ("C:\") or, after a backslash, the part up to the next backslash after
// the second character.
uint8_t __fastcall SplitRoot(void* self, void*, void* text, void* out)
{
    OPENSC4_FIRST("SplitRoot");
    uint32_t start = 2;
    if (!BeginsWith(text, "\\", 1)) {
        if (Virtual<uint32_t>(text, slot::kLength) <= 1)
            return 0;
        if (Virtual<const char*>(text, slot::kText)[1] != ':')
            return 0;
        start = 0;
    }
    return SplitPart(self, nullptr, text, out, start);
}

// The game's 0x008D6196 (ret 8): the first part of `text` added to `path`;
// a part of n dots goes n - 1 folders up.
void __fastcall AddPart(void* self, void*, void* text, void* path)
{
    OPENSC4_FIRST("AddPart");
    GameString part;
    Construct(part);
    if (SplitPart(self, nullptr, text, &part, 0)) {
        if (BeginsWith(&part, ".", 1)) {
            const char* at = Data(&part);
            const char* const end = at + Strlen(&part);
            int32_t dots = 0;
            while (at < end) {
                const char c = *at++;
                if (c != '.')
                    break;
                ++dots;
            }
            for (int32_t up = dots - 1; up != 0; --up)
                FolderUp(self, nullptr, path);
        } else {
            Virtual<uint32_t>(path, slot::kAppend, &part);
        }
    }
    Drop(part);
}

// The game's 0x008D6352 (ret 0xC, returns 1): `name` as a full path in
// `out` (which may be `name`): the folder, then each part of the name.
uint8_t __fastcall ResolvePath(void* self, void*, void* folder, void* name, void* out)
{
    OPENSC4_FIRST("ResolvePath");
    GameString rest = Copy(name);
    Assign(out, folder);
    while (Strlen(&rest) != 0) {
        GameString root;
        Construct(root);
        if (SplitRoot(self, nullptr, &rest, &root))
            Assign(out, &root);
        else
            AddPart(self, nullptr, &rest, out);
        Drop(root);
    }
    Drop(rest);
    return 1;
}

// The game's 0x008D6258 (ret 8): the next line of `text` into `out`: the
// white space before it (" \t\n\r") and the line taken off the text, the
// line ending at the first "\n" or "\r", trimmed and converted (code page
// -1). The game's bug: a last line with no line break is not taken off the
// text, so every later call reads it again and the reader never ends (the
// game hangs). Fixed when fixes.playlist_last_line is on: that line is taken
// off too.
bool g_fixLastLine = false;

uint8_t __fastcall ReadLine(void* self, void*, void* text, void* out)
{
    OPENSC4_FIRST("ReadLine");
    GameString spaces;
    FromText(spaces, " \t\n\r");
    GameString breaks;
    FromText(breaks, "\n\r");
    uint32_t place = 0;
    if (!FindNotOf(self, nullptr, text, &spaces, &place)) {
        Drop(breaks);
        Drop(spaces);
        return 0;
    }
    if (place != 0)
        Virtual<uint32_t>(text, slot::kErase, uint32_t{0}, place);
    if (FindOf(self, nullptr, text, &breaks, &place)) {
        Assign(out, text);
        Virtual<uint32_t>(out, slot::kResize, place);
        Virtual<uint32_t>(text, slot::kErase, uint32_t{0}, place);
    } else {
        Assign(out, text);
        if (g_fixLastLine)
            Virtual<uint32_t>(text, slot::kErase, uint32_t{0}, Virtual<uint32_t>(text, slot::kLength));
    }
    This<void>(addr::cRZBaseString_Trim, out);
    Cdecl<void>(addr::RZ_ConvertStringEncoding, out, kAll, out);
    Drop(breaks);
    Drop(spaces);
    return 1;
}

// ---- The reader ----------------------------------------------------------------------------

// The game's 0x008D569F (thiscall).
Reader* __fastcall ConstructReader(Reader* self, void*)
{
    OPENSC4_FIRST("ConstructReader");
    self->entries.begin = nullptr;
    self->entries.end = nullptr;
    self->entries.capacity = nullptr;
    Construct(self->path);
    Construct(self->folder);
    Construct(self->text);
    return self;
}

// The game's 0x008D56C8 (thiscall).
void __fastcall DestructReader(Reader* self, void*)
{
    OPENSC4_FIRST("DestructReader");
    This<void>(addr::SC4String_Destroy, &self->text);
    This<void>(addr::SC4String_Destroy, &self->folder);
    This<void>(addr::SC4String_Destroy, &self->path);
    DestructEntries(&self->entries, nullptr);
}

// An entry added and the temporary destroyed as the game does (its path by
// SC4String::Destroy, its title inline).
void AddEntry(Reader* self, int32_t length, void* title, void* path)
{
    Entry entry;
    ConstructEntry(&entry, nullptr, length, title, path);
    PushBack(&self->entries, nullptr, &entry);
    This<void>(addr::SC4String_Destroy, &entry.path);
    Drop(entry.title);
}

// The game's 0x008D64E3 (thiscall): an extended .m3u after its "#EXTM3U"
// line: pairs of "#EXTINF:<seconds>,<title>" and a path. Returns 1 at the
// end of the text; 0 when a first line of a pair is not "#EXTINF:" or a
// pair has no second line (the entries already read stay).
uint8_t __fastcall ReadExtended(Reader* self, void*)
{
    OPENSC4_FIRST("ReadExtended");
    GameString line;
    Construct(line);
    uint8_t more = ReadLine(self, nullptr, &self->text, &line);
    while (more) {
        if (!BeginsWith(&line, "#EXTINF:", 8)) {
            Drop(line);
            return 0;
        }
        GameString split;
        This<void*>(addr::SC4String_SplitOff, &line, &split, ":");
        GameString head = Copy(&split);
        Drop(split);
        This<void*>(addr::SC4String_SplitOff, &line, &split, ",");
        GameString seconds = Copy(&split);
        Drop(split);
        const int32_t length = Cdecl<int32_t>(addr::SC4_atol, Data(&seconds));
        GameString path;
        Construct(path);
        if (!ReadLine(self, nullptr, &self->text, &path)) {
            Drop(path);
            Drop(seconds);
            FreeText(head.body.begin);
            Drop(line);
            return 0;
        }
        ResolvePath(self, nullptr, &self->folder, &path, &path);
        AddEntry(self, length, &line, &path);
        Drop(path);
        Drop(seconds);
        Drop(head);
        Drop(line);
        Construct(line);
        more = ReadLine(self, nullptr, &self->text, &line);
    }
    Drop(line);
    return 1;
}

// The game's 0x008D66C9 (thiscall): a plain list, a path a line; the title
// is the file's name without its extension, the length 0. Returns 1. Kept:
// the path is to be joined to the playlist's when the line has ":" and "\\"
// at its first place (Find's -1 for "not found" was taken for 0), which no
// line has; every name is made a full path from the folder anyway.
uint8_t __fastcall ReadPlain(Reader* self, void*)
{
    OPENSC4_FIRST("ReadPlain");
    GameString line;
    Construct(line);
    uint8_t more = ReadLine(self, nullptr, &self->text, &line);
    while (more) {
        if (Find(&line, ":") == 0 && Find(&line, "\\\\") == 0) {
            GameString full = Joined(BodyOf(&self->path), &line.body);
            Assign(&line, &full);
            Drop(full);
        }
        GameString name;
        Construct(name);
        Cdecl<uint8_t>(addr::RZFile_GetFileName, &line, &name, uint32_t{0});
        ResolvePath(self, nullptr, &self->folder, &line, &line);
        AddEntry(self, 0, &name, &line);
        Drop(name);
        Drop(line);
        Construct(line);
        more = ReadLine(self, nullptr, &self->text, &line);
    }
    Drop(line);
    return 1;
}

// After "<key>=" is found at the start of a line: what follows it.
void TakeValue(void* line)
{
    Erase(line, 0, static_cast<uint32_t>(Find(line, "=") + 1));
}

// The game's 0x008D6824 (thiscall): a .pls after its "[playlist]" line.
// Lines up to "NumberOfEntries" are read; a "File" line's path is taken
// with the "Title" and "Length" lines right after it, when they are there
// (a line that is not one of them is kept for the next turn). Returns
// whether the number of entries read is the number on the
// "NumberOfEntries" line (1 for an empty text; 0 when the text ends
// first). Kept: while it looks for the line after an entry, it tests
// whether the entry's path, not that line, begins with "File".
uint8_t __fastcall ReadPls(Reader* self, void*)
{
    OPENSC4_FIRST("ReadPls");
    int32_t count = 0;
    GameString line;
    Construct(line);
    if (!ReadLine(self, nullptr, &self->text, &line)) {
        Drop(line);
        return 1;
    }
    while (!BeginsWith(&line, "NumberOfEntries", 15)) {
        if (!BeginsWith(&line, "File", 4)) {
            if (!ReadLine(self, nullptr, &self->text, &line)) {
                Drop(line);
                return 0;
            }
            continue;
        }
        TakeValue(&line);
        GameString pending;
        Construct(pending);
        GameString title;
        Construct(title);
        GameString next;
        bool ended = false;
        if (!ReadLine(self, nullptr, &self->text, &title)) {
            Drop(title);
            Drop(pending);
            Drop(line);
            return 0;
        }
        if (BeginsWith(&title, "Title", 5))
            TakeValue(&title);
        else
            Assign(&pending, &title);
        Construct(next);
        int32_t length = 0;
        if (Strlen(&pending) == 0) {
            if (!ReadLine(self, nullptr, &self->text, &next)) {
                ended = true;
            } else {
                if (BeginsWith(&next, "Length", 6))
                    TakeValue(&next);
                else
                    Assign(&pending, &next);
                length = Cdecl<int32_t>(addr::SC4_atol, Data(&next));
            }
        }
        while (!ended && Strlen(&pending) == 0) {
            if (!ReadLine(self, nullptr, &self->text, &pending))
                ended = true;
            else if (BeginsWith(&line, "File", 4))
                break;
        }
        if (ended) {
            Drop(next);
            Drop(title);
            Drop(pending);
            Drop(line);
            return 0;
        }
        ResolvePath(self, nullptr, &self->folder, &line, &line);
        AddEntry(self, length, &title, &line);
        ++count;
        Assign(&line, &pending);
        Drop(next);
        Drop(title);
        Drop(pending);
    }
    Erase(&line, 0, 16);
    const int32_t total = Cdecl<int32_t>(addr::SC4_atol, Data(&line));
    const uint8_t same = count == total ? 1 : 0;
    Drop(line);
    return same;
}

// The game's 0x008D6B22 (thiscall, ret 4): a playlist read. The full path
// (0x0091AA3B, else the path as given) and its folder are kept; the file is
// read into the text with its zero bytes but the last made spaces; the
// first line says the format ("#EXTM3U", "[playlist]", else a plain list,
// read from the start). Returns what the format's reader returns, 0 when
// the file cannot be opened or read. Kept: the buffer is not freed when the
// read fails, and an empty file runs the zero-byte loop past its buffer
// (the count is the size minus 1, unsigned).
uint8_t __fastcall Read(Reader* self, void*, void* path)
{
    OPENSC4_FIRST("Read");
    if (!Cdecl<uint8_t>(addr::RZFile_F91AA3B, path, &self->path))
        Virtual<uint32_t>(&self->path, slot::kAssign, path);
    Cdecl<void>(addr::RZFile_GetDirectoryOf, &self->path, &self->folder);
    File file;
    This<void*>(addr::cRZFile_Construct, &file, path);
    uint8_t result = 0;
    if (This<uint8_t>(addr::cRZFile_Open, &file, uint32_t{1}, uint32_t{2}, uint32_t{1})) {
        const uint32_t size = This<uint32_t>(addr::cRZFile_Length, &file);
        Virtual<uint32_t>(&self->text, slot::kResize, size);
        char* const buffer = Cdecl<char*>(addr::SC4_operator_new, size);
        const uint8_t read = This<uint8_t>(addr::cRZFile_Read, &file, buffer, size);
        This<void>(addr::cRZFile_Close, &file);
        if (read) {
            for (uint32_t i = 0; i < size - 1; ++i) {
                if (buffer[i] == 0)
                    buffer[i] = ' ';
            }
            This<void*>(addr::cRZBaseString_AssignLimited, &self->text, buffer, size);
            Cdecl<void>(addr::SC4_operator_delete, buffer);
            GameString whole = Copy(&self->text);
            GameString first;
            Construct(first);
            ReadLine(self, nullptr, &self->text, &first);
            GameString header;
            FromText(header, "#EXTM3U");
            const uint8_t extended = Cdecl<uint8_t>(addr::SC4UI_F43AB70, &first.body, BodyOf(&header));
            Drop(header);
            if (extended) {
                result = ReadExtended(self, nullptr);
            } else {
                FromText(header, "[playlist]");
                const uint8_t pls = Cdecl<uint8_t>(addr::SC4UI_F43AB70, &first.body, BodyOf(&header));
                Drop(header);
                if (pls) {
                    result = ReadPls(self, nullptr);
                } else {
                    Assign(&self->text, &whole);
                    result = ReadPlain(self, nullptr);
                }
            }
            Drop(first);
            FreeText(whole.body.begin);
        }
    }
    This<void>(addr::cRZFile_Destruct, &file);
    return result;
}

#undef OPENSC4_FIRST

// Our 0x008D6258, installed once for whichever of audio.music.part4 and
// fixes.playlist_last_line comes first.
bool InstallReadLine()
{
    static bool installed = false;
    static void* original = nullptr;
    if (installed)
        return true;
    const addr::Entry* const found = addr_check::Find(addr::GZPlaylistReader_ReadLine);
    installed = found && hook::Install(*found, reinterpret_cast<void*>(&ReadLine), &original);
    return installed;
}

bool InstallMusic4(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[48] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::GZPlaylistEntry_Copy, reinterpret_cast<void*>(&CopyEntry)},
        {addr::GZFileTimeSort_Newer, reinterpret_cast<void*>(&Newer)},
        {addr::GZFileTimeSort_Median, reinterpret_cast<void*>(&Median)},
        {addr::GZPlaylistEntries_CopyInto, reinterpret_cast<void*>(&CopyInto)},
        {addr::GZPlaylistEntry_Assign, reinterpret_cast<void*>(&AssignEntry)},
        {addr::GZFileTimeSort_LinearInsertUnguarded, reinterpret_cast<void*>(&LinearInsertUnguarded)},
        {addr::GZFileTimeSort_PushHeap, reinterpret_cast<void*>(&PushHeap)},
        {addr::GZPlaylistEntries_DestroyRange, reinterpret_cast<void*>(&DestroyEntries)},
        {addr::GZPlaylistEntries_Destruct, reinterpret_cast<void*>(&DestructEntries)},
        {addr::GZPlaylistEntries_AllocateCopy, reinterpret_cast<void*>(&AllocateCopy)},
        {addr::GZPlaylistEntries_AssignRange, reinterpret_cast<void*>(&AssignRange)},
        {addr::GZFileTimeSort_Partition, reinterpret_cast<void*>(&Partition)},
        {addr::GZFileTimeSort_InsertionSortUnguardedAux, reinterpret_cast<void*>(&InsertionSortUnguardedAux)},
        {addr::GZFileTimeSort_AdjustHeap, reinterpret_cast<void*>(&AdjustHeap)},
        {addr::GZRadioStation_ShuffleNames, reinterpret_cast<void*>(&ShuffleNames)},
        {addr::GZPlaylistEntries_AssignOver, reinterpret_cast<void*>(&AssignOver)},
        {addr::GZFileTimeSort_InsertionSortUnguarded, reinterpret_cast<void*>(&InsertionSortUnguarded)},
        {addr::GZFileTimeSort_PopHeapStep, reinterpret_cast<void*>(&PopHeapStep)},
        {addr::GZFileTimeSort_MakeHeapAux, reinterpret_cast<void*>(&MakeHeapAux)},
        {addr::GZFileTimeSort_PopHeapAux, reinterpret_cast<void*>(&PopHeapAux)},
        {addr::GZPlaylistReader_Construct, reinterpret_cast<void*>(&ConstructReader)},
        {addr::GZPlaylistReader_Destruct, reinterpret_cast<void*>(&DestructReader)},
        {addr::GZPlaylistEntries_AssignFrom, reinterpret_cast<void*>(&AssignEntries)},
        {addr::GZFileTimeSort_MakeHeap, reinterpret_cast<void*>(&MakeHeap)},
        {addr::GZFileTimeSort_PopHeap, reinterpret_cast<void*>(&PopHeap)},
        {addr::GZFileTimeSort_LinearInsert, reinterpret_cast<void*>(&LinearInsert)},
        {addr::GZFileTimeSort_SortHeap, reinterpret_cast<void*>(&SortHeap)},
        {addr::GZFileTimeSort_InsertionSort, reinterpret_cast<void*>(&InsertionSort)},
        {addr::GZFileTimeSort_PartialSortAux, reinterpret_cast<void*>(&PartialSortAux)},
        {addr::GZFileTimeSort_FinalInsertionSort, reinterpret_cast<void*>(&FinalInsertionSort)},
        {addr::GZFileTimeSort_PartialSort, reinterpret_cast<void*>(&PartialSort)},
        {addr::GZFileTimeSort_IntrosortLoop, reinterpret_cast<void*>(&IntrosortLoop)},
        {addr::GZRadioStation_SortByFileTime, reinterpret_cast<void*>(&SortByFileTime)},
        {addr::GZPlaylistReader_FindNotOf, reinterpret_cast<void*>(&FindNotOf)},
        {addr::GZPlaylistReader_FindOf, reinterpret_cast<void*>(&FindOf)},
        {addr::GZPlaylistReader_SplitPart, reinterpret_cast<void*>(&SplitPart)},
        {addr::GZPlaylistReader_FolderUp, reinterpret_cast<void*>(&FolderUp)},
        {addr::GZPlaylistReader_SplitRoot, reinterpret_cast<void*>(&SplitRoot)},
        {addr::GZPlaylistEntry_Construct, reinterpret_cast<void*>(&ConstructEntry)},
        {addr::GZPlaylistReader_AddPart, reinterpret_cast<void*>(&AddPart)},
        {addr::GZPlaylistEntries_FillInto, reinterpret_cast<void*>(&FillInto)},
        {addr::GZPlaylistReader_ResolvePath, reinterpret_cast<void*>(&ResolvePath)},
        {addr::GZPlaylistEntries_InsertGrown, reinterpret_cast<void*>(&InsertGrown)},
        {addr::GZPlaylistEntries_PushBack, reinterpret_cast<void*>(&PushBack)},
        {addr::GZPlaylistReader_ReadExtended, reinterpret_cast<void*>(&ReadExtended)},
        {addr::GZPlaylistReader_ReadPlain, reinterpret_cast<void*>(&ReadPlain)},
        {addr::GZPlaylistReader_ReadPls, reinterpret_cast<void*>(&ReadPls)},
        {addr::GZPlaylistReader_Read, reinterpret_cast<void*>(&Read)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    static_assert(count == sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < count; ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].first);
        if (!found || !hook::Install(*found, hooks[i].second, &originals[i]))
            return false;
    }
    return InstallReadLine();
}

// fixes.playlist_last_line: our 0x008D6258 alone, taking a last line with no
// line break off the text (docs/audio.md, "The playlist hang").
bool InstallLastLineFix(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    g_fixLastLine = true;
    const bool ok = InstallReadLine();
    log::Write("playlist last line fix: %s", ok ? "on" : "NOT installed");
    if (!ok)
        g_fixLastLine = false;
    return ok;
}

const bool g_music4Registered = registry::Register({
    "audio.music.part4",
    "audio",
    registry::Kind::Detour,
    registry::Phase::Attach,
    addr::GZPlaylistReader_Read,
    "Our music player's playlist reader (.m3u, .pls), its entries and the sort of names by file time (docs/audio.md)",
    false,
    InstallMusic4,
});

const bool g_lastLineFixRegistered = registry::Register({
    "fixes.playlist_last_line",
    "fixes",
    registry::Kind::Detour,
    registry::Phase::Attach,
    addr::GZPlaylistReader_ReadLine,
    "A playlist whose last line has no line break is read to its end (the game read that line forever and hung)",
    false,
    InstallLastLineFix,
    true, // a fix, on unless turned off
});

} // namespace
} // namespace opensc4::audio
