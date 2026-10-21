// The third part of GZSound's music player (docs/audio.md, "Music player"):
// the radio station class. Parts 1 and 2 (music_1.cpp, music_2.cpp) have
// the player service, which holds the stations in a vector and calls them
// through their vtable.
//
// A station is 0x100 bytes, class 0x5CFBDBD5, made by the audio director's
// factory 0x004EB180 (audio part 4) and by the player's station loader
// 0x008D49F8 (part 2). Its vtable 0x00ACC290 has 27 slots:
//    0 QueryInterface 0x008D2994   1 AddRef (shared, 0x00968B60)
//    2 Release (shared, 0x008D29B9) 3, 4 return 1 (shared, 0x00949ADE)
//    5 Update 0x008D2CFD           6 SetPlayer 0x008D3593
//    7 Start 0x008D29D0            8 nothing (shared, 0x00A6D837)
//    9 GetId 0x008D2E5F           10 GetName 0x008D2E88
//   11 the byte +8 (shared, 0x00891D96)
//   12 sets +0xF8 (shared, 0x00994E71)
//   13 SetShuffleOption 0x008D29FC
//   14 SetFilter 0x008D5409       15 DeletingDestruct 0x008D38AD
//   16 ScanFolders 0x008D3960     17 ShuffleLists 0x008D4317
//   18 OrderList 0x008D5EC3       19 Reshuffle 0x008D3485
//   20 HasItems 0x008D2EB0        21 Advance 0x008D338F
//   22 LoadInfo 0x008D4579        23 PlayNext 0x008D4127
//   24 PlayWelcome 0x008D3E7E     25 ExpandPlaylists 0x008D5827
//   26 PlayNextFiltered 0x008D3F8E
//
// Replacement, off by default:
//
//   audio.music.part3: the constructor 0x008D376E, the destructor 0x008D3506,
//   the slots above that are the station's own (0, 5, 6, 7, 9, 10, 13 to
//   26), the shuffle 0x008D3442, and the helpers 0x008D34D1 and 0x008D34EC
//   (the play order's life), 0x008D3EE0 (slot 26's file check) and
//   0x008D3BE2 (slot 18's string copy). 27 functions.
//
// As parts 1 and 2, the group goes in when core.dll is injected (phase
// Attach): the stations are made while the application starts.
//
// Still the game's, called by address: the shared slots above (Release
// 0x008D29B9 is in three other vtables: 0x00A82A28, 0x00AD16D0, 0x00AD4720); part 4's
// playlist reader (0x008D569F, 0x008D56C8, 0x008D6B22), its entries
// (0x008D56EB, 0x008D51FC), the sort by file time 0x008D5E50 and the name shuffle
// 0x008D546F; the string, list, folder, file and stopwatch code.
//
// The kinds: 0 music, 1 DJ, 2 commercials, 3 jingles. Each has a folder
// (SC4String), a list of file names (an STL list: the head node's pointer)
// and the list's place (a node pointer):
//   music       +0x84 +0x98 +0x9C      DJ    +0xA0 +0xB4 +0xB8
//   commercials +0xBC +0xD0 +0xD4      jingle +0xD8 +0xEC +0xF0
// The rest of the object:
//   +0x04 the reference count             +0x08 byte (1), slot 11
//   +0x09 byte: the welcome was played    +0x10 .. +0x18 the play order
//   (a vector of the four kinds, shuffled) +0x1C its place
//   +0x20 a count of music played again in place of the next kind
//   +0x24 byte: waiting for the pause     +0x28 a stopwatch (the pause)
//   +0x40 the player interface (not held) +0x44 its slot 6
//   +0x48 the ID (SC4String, "????")      +0x5C the name (SC4String)
//   +0x70 the station's folder            +0xF4 the music filter (a list)
//   +0xF8 the filter's mode (slot 12)     +0xFC slot 17's argument (13)
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

namespace station {
constexpr std::size_t kRefs = 0x04;
constexpr std::size_t kEnabled = 0x08;   // byte
constexpr std::size_t kWelcomed = 0x09;  // byte
constexpr std::size_t kOrder = 0x10;     // begin, end, capacity
constexpr std::size_t kOrderEnd = 0x14;
constexpr std::size_t kStopwatchInOrder = 0x18; // from kOrder
constexpr std::size_t kPlace = 0x1C;
constexpr std::size_t kRepeats = 0x20;
constexpr std::size_t kWaiting = 0x24;   // byte
constexpr std::size_t kStopwatch = 0x28;
constexpr std::size_t kPlayer = 0x40;
constexpr std::size_t kPlayerSlot6 = 0x44;
constexpr std::size_t kId = 0x48;        // SC4String
constexpr std::size_t kIdBegin = 0x4C;
constexpr std::size_t kIdEnd = 0x50;
constexpr std::size_t kName = 0x5C;      // SC4String
constexpr std::size_t kNameBegin = 0x60;
constexpr std::size_t kNameEnd = 0x64;
constexpr std::size_t kFolder = 0x70;    // SC4String
constexpr std::size_t kFilter = 0xF4;    // a string list
constexpr std::size_t kFilterMode = 0xF8;
constexpr std::size_t kShuffleOption = 0xFC;
} // namespace station

// A kind's folder, list and place.
struct Kind {
    std::size_t folder;
    std::size_t list;
    std::size_t place;
};
constexpr Kind kKinds[4] = {{0x84, 0x98, 0x9C}, {0xA0, 0xB4, 0xB8}, {0xBC, 0xD0, 0xD4}, {0xD8, 0xEC, 0xF0}};

constexpr uint32_t kUnknownIid = 1;
constexpr uint32_t kStationIid = 0x4056574B;
constexpr uint32_t kPauseMs = 4000;

// The game's string body and string (SC4String, 0x14 bytes).
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

struct StringNode {
    StringNode* next;
    StringNode* previous;
    GameString text;
};

// A vector of the game's (begin, end, capacity).
struct Vector {
    char* begin;
    char* end;
    char* capacity;
};

// The folder listing object (0x34 bytes), the file (0x80) and part 4's
// playlist reader (0x48), as the game has them on its stack.
struct Directory {
    alignas(4) char bytes[0x34];
};
struct File {
    alignas(4) char bytes[0x80];
};
struct Reader {
    alignas(4) char bytes[0x48];
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
    log::Write("music station: first %s", name);
}

void FreeText(char* begin)
{
    if (begin)
        Cdecl<void>(addr::SC4_Free, begin);
}

void Construct(GameString& text)
{
    This<void*>(addr::SC4String_SC4String, &text);
}

// What the game does at the end of a string's life on its own stack: the
// vtable put back and the text freed.
void Drop(GameString& text)
{
    text.vtable = addr::SC4String_Vtable;
    FreeText(text.body.begin);
}

void Assign(void* target, void* source)
{
    This<void*>(addr::cRZBaseString_Assign, target, source);
}

// The body of a string, or null for no string.
Body* BodyOf(void* text)
{
    return text ? reinterpret_cast<Body*>(static_cast<char*>(text) + 4) : nullptr;
}

// `target` = `first` followed by `second` (0x00408AE0, 0x00408B90, the
// assignment; both temporaries freed).
void AssignJoin(void* target, Body* first, Body* second)
{
    Body joined{};
    Body* made = Cdecl<Body*>(addr::cSC4Advice_F408AE0, &joined, first, second);
    GameString copy;
    This<void*>(addr::cSC4Advice_F408B90, &copy, made);
    Assign(target, &copy);
    Drop(copy);
    FreeText(joined.begin);
}

bool Exists(void* path)
{
    return Cdecl<uint8_t>(addr::RZFile_Exists, path) != 0;
}

StringNode*& ListOf(char* self, std::size_t offset)
{
    return At<StringNode*>(self, offset);
}

void* Player(char* self)
{
    return At<void*>(self, station::kPlayer);
}

// The player's slot 7: a file played if it exists.
void PlayFile(char* self, GameString& path)
{
    Virtual<uint8_t>(Player(self), 0x1C, &path);
}

// ---- Life ------------------------------------------------------------------------

// The game's 0x008D34D1 (thiscall on the station's +0x10).
char* __fastcall ConstructOrder(char* order, void*)
{
    static bool logged = false;
    FirstCall("ConstructOrder", logged);
    At<uint32_t>(order, 0x0) = 0;
    At<uint32_t>(order, 0x4) = 0;
    At<uint32_t>(order, 0x8) = 0;
    This<void*>(addr::SC4Stopwatch_Construct, order + station::kStopwatchInOrder, uint32_t{4});
    return order;
}

// The game's 0x008D34EC (thiscall on the station's +0x10).
void __fastcall DestructOrder(char* order, void*)
{
    static bool logged = false;
    FirstCall("DestructOrder", logged);
    This<void>(addr::SC4Stopwatch_Destruct, order + station::kStopwatchInOrder);
    FreeText(At<char*>(order, 0x0));
}

// The game's 0x008D3442 (cdecl): the play order shuffled. The draws come
// from the sound code's own sequence, as the game's do under
// determinism.fixed_seed (src/core/fixed_seed.cpp).
void __cdecl Shuffle(uint32_t* first, uint32_t* last)
{
    static bool logged = false;
    FirstCall("Shuffle", logged);
    if (first == last)
        return;
    uint32_t* at = first + 1;
    if (at == last)
        return;
    int32_t bytes = static_cast<int32_t>(reinterpret_cast<char*>(at) - reinterpret_cast<char*>(first));
    do {
        const int32_t drawn = fixed_seed::SoundRand();
        const int32_t pick = drawn % ((bytes >> 2) + 1);
        const uint32_t kept = *at;
        bytes += 4;
        *at = first[pick];
        ++at;
        first[pick] = kept;
    } while (at != last);
}

// The game's 0x008D376E (thiscall). The four places in the lists are not
// set here: slot 17 sets them, and only for lists that are not empty.
char* __fastcall ConstructStation(char* self, void*)
{
    static bool logged = false;
    FirstCall("ConstructStation", logged);
    At<std::uintptr_t>(self, 0x0) = addr::GZRadioStation_Vtable;
    At<uint32_t>(self, station::kRefs) = 0;
    At<uint8_t>(self, station::kEnabled) = 1;
    At<uint8_t>(self, station::kWelcomed) = 0;
    This<char*>(addr::GZRadioStation_ConstructOrder, self + station::kOrder);
    At<uint32_t>(self, station::kPlayer) = 0;
    At<uint32_t>(self, station::kPlayerSlot6) = 0;
    This<void*>(addr::SC4String_FromText, self + station::kId, reinterpret_cast<const char*>(addr::GZMusicPlayer_BlankIdText));
    This<void*>(addr::SC4String_SC4String, self + station::kName);
    This<void*>(addr::SC4String_SC4String, self + station::kFolder);
    uint8_t tag = 0;
    for (const Kind& kind : kKinds) {
        This<void*>(addr::SC4String_SC4String, self + kind.folder);
        This<void*>(addr::SC4UI_F43AEF0, self + kind.list, &tag);
    }
    This<void*>(addr::SC4UI_F43AEF0, self + station::kFilter, &tag);
    At<uint32_t>(self, station::kFilterMode) = 0;
    At<uint32_t>(self, station::kShuffleOption) = 0;
    for (uint32_t kind = 0; kind < 4; ++kind) {
        uint32_t value = kind;
        This<void>(addr::SC4PtrVector_PushBack, self + station::kOrder, &value);
    }
    Cdecl<void>(addr::GZRadioStation_Shuffle, At<uint32_t*>(self, station::kOrder), At<uint32_t*>(self, station::kOrderEnd));
    At<uint32_t>(self, station::kPlace) = 0;
    At<uint32_t>(self, station::kRepeats) = 0;
    At<uint8_t>(self, station::kWaiting) = 0;
    This<void>(addr::SC4Stopwatch_ConstructMode, self + station::kStopwatch, uint32_t{4});
    This<void>(addr::SC4Stopwatch_Start, self + station::kStopwatch);
    return self;
}

// The game's 0x008D3506 (thiscall).
void __fastcall Destruct(char* self, void*)
{
    static bool logged = false;
    FirstCall("Destruct", logged);
    At<std::uintptr_t>(self, 0x0) = addr::GZRadioStation_Vtable;
    This<void>(addr::SC4UI_F43AF20, self + station::kFilter);
    for (int i = 3; i >= 0; --i) {
        This<void>(addr::SC4UI_F43AF20, self + kKinds[i].list);
        This<void>(addr::SC4String_Destroy, self + kKinds[i].folder);
    }
    This<void>(addr::SC4String_Destroy, self + station::kFolder);
    This<void>(addr::SC4String_Destroy, self + station::kName);
    This<void>(addr::SC4String_Destroy, self + station::kId);
    This<void>(addr::GZRadioStation_DestructOrder, self + station::kOrder);
}

// The game's 0x008D38AD (slot 15, ret 4).
char* __fastcall DeletingDestruct(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("DeletingDestruct", logged);
    This<void>(addr::GZRadioStation_Destruct, self);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// ---- COM ---------------------------------------------------------------------------

// The game's 0x008D2994 (slot 0, ret 8).
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** out)
{
    static bool logged = false;
    FirstCall("QueryInterface", logged);
    if (iid != kUnknownIid && iid != kStationIid)
        return 0;
    *out = self;
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// ---- Playing -----------------------------------------------------------------------

// The game's 0x008D2CFD (slot 5, what the player's slot 5 jumps to). Once
// the player is done with a file, the station waits four seconds, then
// takes the next kind of the play order: music with a draw of 75 in 100 to
// play more music in place of the next kind, DJ with 50 in 100, commercials
// and jingles with 10 in 100; a kind with nothing to play is skipped
// (without a turn of the order). Returns 1.
uint8_t __fastcall Update(char* self, void*)
{
    static bool logged = false;
    FirstCall("Update", logged);
    if (!Virtual<uint8_t>(self, 0x2C))
        return 1;
    if (!Virtual<uint32_t>(Player(self), 0x3C))
        return 1;
    if (Virtual<uint8_t>(Player(self), 0x88))
        return 1;
    if (!At<uint8_t>(self, station::kWaiting)) {
        if (Virtual<uint8_t>(Player(self), 0x88))
            return 1;
        At<uint8_t>(self, station::kWaiting) = 1;
        This<void>(addr::SC4Stopwatch_Reset, self + station::kStopwatch);
        This<void>(addr::SC4Stopwatch_Start, self + station::kStopwatch);
        return 1;
    }
    if (This<uint32_t>(addr::GZStopwatch_ElapsedMilliseconds, self + station::kStopwatch) < kPauseMs)
        return 1;
    uint32_t* const order = At<uint32_t*>(self, station::kOrder);
    uint32_t kind = 0;
    if (order[At<uint32_t>(self, station::kPlace)] == 0 && Virtual<uint8_t>(self, 0x50, uint32_t{0}) &&
        static_cast<uint32_t>(fixed_seed::SoundRand() % 100) < 75) {
        ++At<uint32_t>(self, station::kRepeats);
    } else {
        ++At<uint32_t>(self, station::kPlace);
        const int32_t count = static_cast<int32_t>(At<char*>(self, station::kOrderEnd) - At<char*>(self, station::kOrder)) >> 2;
        if (At<int32_t>(self, station::kPlace) == count) {
            Virtual<void>(self, 0x4C);
            At<uint32_t>(self, station::kPlace) = 0;
        }
        kind = At<uint32_t*>(self, station::kOrder)[At<uint32_t>(self, station::kPlace)];
        if (kind == 0) {
            if (!Virtual<uint8_t>(self, 0x50, uint32_t{0}))
                return 1;
        } else if (kind == 1 || kind == 2 || kind == 3) {
            const uint32_t odds = kind == 1 ? 50 : 10;
            if (static_cast<uint32_t>(fixed_seed::SoundRand() % 100) >= odds)
                return 1;
            if (!Virtual<uint8_t>(self, 0x50, kind))
                return 1;
        } else {
            return 1;
        }
    }
    At<uint8_t>(self, station::kWaiting) = 0;
    Virtual<uint8_t>(self, 0x5C, kind);
    return 1;
}

// The game's 0x008D3593 (slot 6, ret 4).
uint8_t __fastcall SetPlayer(char* self, void*, void* player)
{
    static bool logged = false;
    FirstCall("SetPlayer", logged);
    At<void*>(self, station::kPlayer) = player;
    At<uint32_t>(self, station::kPlayerSlot6) = Virtual<uint32_t>(player, 0x18);
    return 1;
}

// The game's 0x008D29D0 (slot 7): the ID and name read (slot 22), the
// folders listed (slot 16), the lists ordered (slot 17), and the welcome
// played (slot 24, by a jump) unless it was.
void __fastcall Start(char* self, void*)
{
    static bool logged = false;
    FirstCall("Start", logged);
    Virtual<uint8_t>(self, 0x58);
    Virtual<void>(self, 0x40);
    Virtual<void>(self, 0x44, At<uint32_t>(self, station::kShuffleOption));
    if (!At<uint8_t>(self, station::kWelcomed))
        Virtual<void>(self, 0x60);
}

// The game's 0x008D2E5F (slot 9, ret 4).
void __fastcall GetId(char* self, void*, char* out)
{
    static bool logged = false;
    FirstCall("GetId", logged);
    for (int i = 0; i < 4; ++i)
        out[i] = At<char*>(self, station::kIdBegin)[i];
}

// The game's 0x008D2E88 (slot 10, ret 4): the name, or the ID when there is
// none, given to a cIGZString (its slot 4 with the text, slot 5 of ours).
void __fastcall GetName(char* self, void*, void* out)
{
    static bool logged = false;
    FirstCall("GetName", logged);
    char* const source = At<char*>(self, station::kNameBegin) == At<char*>(self, station::kNameEnd) ? self + station::kId
                                                                                                    : self + station::kName;
    const char* const text = Virtual<const char*>(source, 0x14);
    Virtual<uint32_t>(out, 0x10, text);
}

// The game's 0x008D29FC (slot 13, ret 4).
void __fastcall SetShuffleOption(char* self, void*, uint32_t value)
{
    static bool logged = false;
    FirstCall("SetShuffleOption", logged);
    At<uint32_t>(self, station::kShuffleOption) = value;
}

// The game's 0x008D5409 (slot 14, ret 4): the music filter made a copy of a
// list of names.
void __fastcall SetFilter(char* self, void*, StringNode** source)
{
    static bool logged = false;
    FirstCall("SetFilter", logged);
    This<void>(addr::SC4UI_F43ABA0, self + station::kFilter);
    if (!source || (*source)->next == *source)
        return;
    GameString name;
    Construct(name);
    for (StringNode* node = (*source)->next; node != *source; node = node->next) {
        Assign(&name, &node->text);
        This<void>(addr::SC4StringList_PushBack, self + station::kFilter, &name);
    }
    FreeText(name.body.begin);
}

// The game's 0x008D3960 (slot 16): the four lists emptied and filled with
// the files of each folder that match "*.mp3,*.wav,*.xa,*.m3u,*.pls", one
// pattern at a time, then each list's playlists expanded (slot 25).
void __fastcall ScanFolders(char* self, void*)
{
    static bool logged = false;
    FirstCall("ScanFolders", logged);
    GameString empty;
    Construct(empty);
    Directory finder;
    This<void*>(addr::cRZDirectory_Construct, &finder, &empty, uint32_t{0}, uint32_t{0}, uint32_t{0});
    Drop(empty);
    GameString patterns;
    This<void*>(addr::SC4String_FromText, &patterns, Global<const char*>(addr::GZMusicPlayer_MusicPatterns));
    for (const Kind& kind : kKinds) {
        StringNode* const head = ListOf(self, kind.list);
        StringNode* out = nullptr;
        This<void*>(addr::SC4StringList_EraseRange, self + kind.list, &out, head->next, head);
    }
    GameString pattern;
    Construct(pattern);
    uint8_t more = 0;
    do {
        more = This<uint8_t>(addr::SC4String_SplitAtChar, &patterns, uint32_t{','}, &pattern);
        This<void>(addr::cRZDirectory_SetPath, &finder, self + kKinds[0].folder, uint32_t{0});
        This<void>(addr::cRZDirectory_SetPattern, &finder, &pattern, uint32_t{0});
        This<void>(addr::cRZDirectory_GetFiles, &finder, self + kKinds[0].list);
        for (int i = 1; i < 4; ++i) {
            This<void>(addr::cRZDirectory_SetPath, &finder, self + kKinds[i].folder, uint32_t{0});
            GameString copy;
            This<void*>(addr::SC4String_Substring, &copy, &pattern, uint32_t{0}, uint32_t{0xFFFFFFFF});
            This<void>(addr::cRZDirectory_SetPattern, &finder, &copy, uint32_t{0});
            Drop(copy);
            This<void>(addr::cRZDirectory_GetFiles, &finder, self + kKinds[i].list);
        }
    } while (more);
    for (const Kind& kind : kKinds)
        Virtual<void>(self, 0x64, self + kind.folder, self + kind.list);
    Drop(pattern);
    Drop(patterns);
    This<void>(addr::cRZDirectory_Destruct, &finder);
}

// The game's 0x008D4317 (slot 17, ret 4): each list that is not empty
// ordered (slot 18 with the argument); when the name that was last comes
// out first, the first and the last are swapped. The list's place is set
// to its first.
void __fastcall ShuffleLists(char* self, void*, uint32_t how)
{
    static bool logged = false;
    FirstCall("ShuffleLists", logged);
    GameString last;
    GameString kept;
    Construct(last);
    Construct(kept);
    for (const Kind& kind : kKinds) {
        StringNode*& head = ListOf(self, kind.list);
        if (head->next == head)
            continue;
        Assign(&last, &head->previous->text);
        Virtual<void>(self, 0x48, self + kind.folder, &head, how);
        if (Cdecl<uint8_t>(addr::SC4UI_F43AB70, &last.body, BodyOf(&head->next->text))) {
            Assign(&kept, &head->previous->text);
            Assign(&head->previous->text, &head->next->text);
            Assign(&head->next->text, &kept);
        }
        ListOf(self, kind.place) = head->next;
    }
    FreeText(kept.body.begin);
    FreeText(last.body.begin);
}

// The game's 0x008D5EC3 (slot 18, ret 0xC): a list copied into a vector,
// sorted by the files' modification times in the folder, newest first
// (part 4's 0x008D5E50, which takes the folder by value and destroys it)
// when `how` is 1, else shuffled (0x008D546F), and copied back. The object
// is not used.
void __fastcall OrderList(char*, void*, void* folder, StringNode** list, uint32_t how)
{
    static bool logged = false;
    FirstCall("OrderList", logged);
    StringNode* node = (*list)->next;
    if (node == *list)
        return;
    Vector names{};
    do {
        This<void>(addr::SC4StringVector_PushBack, &names, &node->text);
        node = node->next;
    } while (node != *list);
    char* at = names.begin;
    if (how == 1) {
        GameString copy;
        This<void*>(addr::GZRadioStation_CopyString, &copy, folder);
        reinterpret_cast<void(__cdecl*)(char*, char*, GameString)>(addr::GZRadioStation_SortByFileTime)(at, names.end, copy);
    } else {
        Cdecl<void>(addr::GZRadioStation_ShuffleNames, at, names.end);
    }
    This<void>(addr::SC4UI_F43ABA0, list);
    for (int32_t left = static_cast<int32_t>(names.end - at) / 0x14; left > 0; --left, at += 0x14)
        This<void>(addr::SC4StringList_PushBack, list, at);
    This<void>(addr::cSC4Advisors_F419140, &names);
}

// The game's 0x008D3485 (slot 19): the play order shuffled again. When the
// new first kind is the old last one and that is not music, the first and
// the last are swapped, so that no kind but music comes twice in a row.
void __fastcall Reshuffle(char* self, void*)
{
    static bool logged = false;
    FirstCall("Reshuffle", logged);
    uint32_t* order = At<uint32_t*>(self, station::kOrder);
    int32_t count = static_cast<int32_t>(At<char*>(self, station::kOrderEnd) - reinterpret_cast<char*>(order)) >> 2;
    const uint32_t last = order[count - 1];
    Cdecl<void>(addr::GZRadioStation_Shuffle, order, At<uint32_t*>(self, station::kOrderEnd));
    if (last == 0)
        return;
    order = At<uint32_t*>(self, station::kOrder);
    if (order[0] != last)
        return;
    const uint32_t first = order[0];
    count = static_cast<int32_t>(At<char*>(self, station::kOrderEnd) - reinterpret_cast<char*>(order)) >> 2;
    order[0] = order[count - 1];
    count = static_cast<int32_t>(At<char*>(self, station::kOrderEnd) - At<char*>(self, station::kOrder)) >> 2;
    At<uint32_t*>(self, station::kOrder)[count - 1] = first;
}

// The game's 0x008D2EB0 (slot 20, ret 4): whether there is something of a
// kind to play. DJ, commercials and jingles need the player's options 0, 1
// and 2 (its slot 26).
uint8_t __fastcall HasItems(char* self, void*, uint32_t kind)
{
    static bool logged = false;
    FirstCall("HasItems", logged);
    if (kind > 3)
        return 0;
    if (kind != 0 && !Virtual<uint8_t>(Player(self), 0x68, kind - 1))
        return 0;
    StringNode* const head = ListOf(self, kKinds[kind].list);
    return head->next != head;
}

// The game's 0x008D338F (slot 21, ret 4): a kind's place moved on, past the
// end to the first.
void __fastcall Advance(char* self, void*, uint32_t kind)
{
    static bool logged = false;
    FirstCall("Advance", logged);
    if (kind > 3)
        return;
    StringNode*& place = ListOf(self, kKinds[kind].place);
    place = place->next;
    StringNode* const head = ListOf(self, kKinds[kind].list);
    if (place == head)
        place = head->next;
}

// The game's 0x008D4579 (slot 22): the ID is the name of the station
// folder's "*.id" file, the name that of its "*.txt" file (both without
// the extension, RZFile's 0x0091A93D). Returns whether there is an ID.
uint8_t __fastcall LoadInfo(char* self, void*)
{
    static bool logged = false;
    FirstCall("LoadInfo", logged);
    GameString empty;
    Construct(empty);
    Directory finder;
    This<void*>(addr::cRZDirectory_Construct, &finder, &empty, uint32_t{0}, uint32_t{0}, uint32_t{0});
    Drop(empty);
    GameString pattern;
    This<void*>(addr::SC4String_FromText, &pattern, Global<const char*>(addr::GZMusicPlayer_IdPatternText));
    This<void>(addr::cRZDirectory_SetPath, &finder, self + station::kFolder, uint32_t{1});
    This<void>(addr::cRZDirectory_SetPattern, &finder, &pattern, uint32_t{1});
    if (This<void*>(addr::cRZBaseString_Data, &finder)) {
        char* const entry = This<char*>(addr::cRZDirectory_EntryAt, &finder, uint32_t{0});
        Cdecl<void*>(addr::RZFile_GetFileName, entry + 4, self + station::kId, uint32_t{0});
    }
    GameString text;
    This<void*>(addr::SC4String_FromText, &text, Global<const char*>(addr::GZRadioStation_TextPatternText));
    Assign(&pattern, &text);
    Drop(text);
    This<void>(addr::cRZDirectory_SetPattern, &finder, &pattern, uint32_t{1});
    if (This<void*>(addr::cRZBaseString_Data, &finder)) {
        char* const entry = This<char*>(addr::cRZDirectory_EntryAt, &finder, uint32_t{0});
        Cdecl<void*>(addr::RZFile_GetFileName, entry + 4, self + station::kName, uint32_t{0});
    }
    const uint8_t any = At<char*>(self, station::kIdBegin) != At<char*>(self, station::kIdEnd);
    Drop(pattern);
    This<void>(addr::cRZDirectory_Destruct, &finder);
    return any;
}

// The game's 0x008D4127 (slot 23, ret 4): the next file of a kind played
// through the player (its slot 7). Music goes to slot 26 when there is a
// filter (mode 1 with names, or another mode); a music name that is not a
// file is taken in the Music folder. The other kinds are always taken in
// their folders. Returns 0 for a kind above 3.
uint8_t __fastcall PlayNext(char* self, void*, uint32_t kind)
{
    static bool logged = false;
    FirstCall("PlayNext", logged);
    GameString path;
    Construct(path);
    if (kind > 3) {
        Drop(path);
        return 0;
    }
    const Kind& k = kKinds[kind];
    if (kind == 0) {
        const uint32_t mode = At<uint32_t>(self, station::kFilterMode);
        StringNode* const filter = ListOf(self, station::kFilter);
        if (mode != 0 && (mode != 1 || filter->next != filter)) {
            const uint8_t played = Virtual<uint8_t>(self, 0x68);
            Drop(path);
            return played;
        }
    }
    StringNode*& place = ListOf(self, k.place);
    place = place->next;
    StringNode* const head = ListOf(self, k.list);
    if (place == head)
        place = head->next;
    if (kind == 0) {
        Assign(&path, &place->text);
        if (!Exists(&path))
            AssignJoin(&path, BodyOf(self + k.folder), &path.body);
    } else {
        AssignJoin(&path, BodyOf(self + k.folder), BodyOf(&place->text));
    }
    PlayFile(self, path);
    Drop(path);
    return 1;
}

// The game's 0x008D3E7E (slot 24): the station folder's Welcome.wav played
// when it exists; the station then waits again and is marked welcomed.
void __fastcall PlayWelcome(char* self, void*)
{
    static bool logged = false;
    FirstCall("PlayWelcome", logged);
    GameString path;
    This<void*>(addr::SC4String_Substring, &path, self + station::kFolder, uint32_t{0}, uint32_t{0xFFFFFFFF});
    This<void>(addr::cSC4Advisor_F410F00, &path.body, Global<const char*>(addr::GZRadioStation_WelcomeText));
    if (Exists(&path)) {
        PlayFile(self, path);
        At<uint8_t>(self, station::kWaiting) = 0;
        At<uint8_t>(self, station::kWelcomed) = 1;
    }
    Drop(path);
}

// The game's 0x008D5827 (slot 25, ret 8): every name in the list that ends
// in ".m3u" or ".pls" is read as a playlist (part 4's reader; the name taken
// in `folder` when it is not a file), the files it names are added at the
// end of the list, and the playlist's name is taken out. The object is not
// used. Kept: after the name is taken out, the next pattern is still
// compared with the freed node's name.
void __fastcall ExpandPlaylists(char*, void*, void* folder, StringNode** list)
{
    static bool logged = false;
    FirstCall("ExpandPlaylists", logged);
    StringNode* node = (*list)->next;
    if (node == *list)
        return;
    do {
        GameString* const name = &node->text;
        GameString rest;
        GameString ending;
        Construct(rest);
        Construct(ending);
        GameString patterns;
        This<void*>(addr::SC4String_FromText, &patterns, Global<const char*>(addr::GZRadioStation_PlaylistPatterns));
        uint8_t more = 0;
        do {
            more = This<uint8_t>(addr::SC4String_SplitAtChar, &patterns, uint32_t{','}, &rest);
            This<void>(addr::cRZBaseString_Erase, &rest, uint32_t{0}, uint32_t{1});
            const uint32_t length = This<uint32_t>(addr::cRZBaseString_Strlen, &rest);
            GameString right;
            void* const got = This<void*>(addr::cRZBaseString_Right, name, &right, length);
            Assign(&ending, got);
            Drop(right);
            if (!This<uint8_t>(addr::cRZBaseString_IsEqual, &ending, &rest, uint32_t{0}))
                continue;
            Reader reader;
            This<void*>(addr::GZPlaylistReader_Construct, &reader);
            GameString path;
            Construct(path);
            File file;
            This<void*>(addr::cRZFile_Construct, &file, name);
            if (Exists(name))
                Assign(&path, name);
            else
                AssignJoin(&path, BodyOf(folder), BodyOf(name));
            if (This<uint8_t>(addr::GZPlaylistReader_Read, &reader, &path)) {
                Vector entries{};
                This<void>(addr::GZPlaylistEntries_AssignFrom, &entries, &reader);
                const int32_t count = static_cast<int32_t>(entries.end - entries.begin) / 0x2C;
                char* at = entries.begin + 0x14;
                for (uint32_t left = static_cast<uint32_t>(count); left != 0; --left, at += 0x2C) {
                    GameString added;
                    Construct(added);
                    Virtual<uint32_t>(&added, 0x34, at);
                    This<void>(addr::SC4StringList_PushBack, list, &added);
                    Drop(added);
                }
                This<void>(addr::GZPlaylistEntries_Destruct, &entries);
            }
            StringNode* after = nullptr;
            This<void*>(addr::SC4StringList_Erase, list, &after, node);
            node = after->previous;
            This<void>(addr::cRZFile_Destruct, &file);
            Drop(path);
            This<void>(addr::GZPlaylistReader_Destruct, &reader);
        } while (more);
        node = node->next;
        Drop(patterns);
        Drop(ending);
        Drop(rest);
    } while (node != *list);
}

// The game's 0x008D3EE0 (thiscall, ret 4): whether a name is a file, as it
// is or in the Music folder.
uint8_t __fastcall MusicFileExists(char* self, void*, void* name)
{
    static bool logged = false;
    FirstCall("MusicFileExists", logged);
    uint8_t found = 1;
    GameString path;
    Construct(path);
    Virtual<uint32_t>(&path, 0x34, name);
    if (!Exists(&path)) {
        AssignJoin(&path, BodyOf(self + kKinds[0].folder), &path.body);
        found = Exists(&path) ? 1 : 0;
    }
    Drop(path);
    return found;
}

// The game's 0x008D3F8E (slot 26): from the music list's place on, the next
// name that is (any mode but 1) or is not (mode 1) in the filter and is a
// file is played, once round the list. When the round comes back to where
// it started, the lists are ordered again (slot 17). Returns 1.
uint8_t __fastcall PlayNextFiltered(char* self, void*)
{
    static bool logged = false;
    FirstCall("PlayNextFiltered", logged);
    if (ListOf(self, kKinds[0].list)->next == ListOf(self, kKinds[0].list))
        return 1;
    StringNode*& place = ListOf(self, kKinds[0].place);
    StringNode* const start = place;
    bool wrapped = false;
    place = start->next;
    while (place != start) {
        if (place == ListOf(self, kKinds[0].list)) {
            if (wrapped)
                break;
            wrapped = true;
            place = ListOf(self, kKinds[0].list)->next;
        }
        GameString* const name = &place->text;
        StringNode* const filter = ListOf(self, station::kFilter);
        StringNode* found = nullptr;
        Cdecl<void*>(addr::SC4StringList_FindFrom, &found, filter->next, filter, name);
        const bool listed = found != filter;
        const bool wanted = At<uint32_t>(self, station::kFilterMode) == 1 ? !listed : listed;
        if (wanted && This<uint8_t>(addr::GZRadioStation_MusicFileExists, self, name)) {
            GameString path;
            Construct(path);
            Assign(&path, &place->text);
            if (!Exists(&path))
                AssignJoin(&path, BodyOf(self + kKinds[0].folder), &path.body);
            PlayFile(self, path);
            Drop(path);
            return 1;
        }
        place = place->next;
    }
    if (place == start)
        Virtual<void>(self, 0x44, At<uint32_t>(self, station::kShuffleOption));
    return 1;
}

// The game's 0x008D3BE2 (thiscall, ret 4): a copy of a string.
void* __fastcall CopyString(void* self, void*, void* source)
{
    static bool logged = false;
    FirstCall("CopyString", logged);
    This<void*>(addr::SC4String_Substring, self, source, uint32_t{0}, uint32_t{0xFFFFFFFF});
    return self;
}

bool InstallMusic3(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[27] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::GZRadioStation_QueryInterface, reinterpret_cast<void*>(&QueryInterface)},
        {addr::GZRadioStation_Start, reinterpret_cast<void*>(&Start)},
        {addr::GZRadioStation_SetShuffleOption, reinterpret_cast<void*>(&SetShuffleOption)},
        {addr::GZRadioStation_Update, reinterpret_cast<void*>(&Update)},
        {addr::GZRadioStation_GetId, reinterpret_cast<void*>(&GetId)},
        {addr::GZRadioStation_GetName, reinterpret_cast<void*>(&GetName)},
        {addr::GZRadioStation_HasItems, reinterpret_cast<void*>(&HasItems)},
        {addr::GZRadioStation_Advance, reinterpret_cast<void*>(&Advance)},
        {addr::GZRadioStation_Shuffle, reinterpret_cast<void*>(&Shuffle)},
        {addr::GZRadioStation_Reshuffle, reinterpret_cast<void*>(&Reshuffle)},
        {addr::GZRadioStation_ConstructOrder, reinterpret_cast<void*>(&ConstructOrder)},
        {addr::GZRadioStation_DestructOrder, reinterpret_cast<void*>(&DestructOrder)},
        {addr::GZRadioStation_Destruct, reinterpret_cast<void*>(&Destruct)},
        {addr::GZRadioStation_SetPlayer, reinterpret_cast<void*>(&SetPlayer)},
        {addr::GZSound_Construct8D376E, reinterpret_cast<void*>(&ConstructStation)},
        {addr::GZRadioStation_DeletingDestruct, reinterpret_cast<void*>(&DeletingDestruct)},
        {addr::GZRadioStation_ScanFolders, reinterpret_cast<void*>(&ScanFolders)},
        {addr::GZRadioStation_CopyString, reinterpret_cast<void*>(&CopyString)},
        {addr::GZRadioStation_PlayWelcome, reinterpret_cast<void*>(&PlayWelcome)},
        {addr::GZRadioStation_MusicFileExists, reinterpret_cast<void*>(&MusicFileExists)},
        {addr::GZRadioStation_PlayNextFiltered, reinterpret_cast<void*>(&PlayNextFiltered)},
        {addr::GZRadioStation_PlayNext, reinterpret_cast<void*>(&PlayNext)},
        {addr::GZRadioStation_ShuffleLists, reinterpret_cast<void*>(&ShuffleLists)},
        {addr::GZRadioStation_LoadInfo, reinterpret_cast<void*>(&LoadInfo)},
        {addr::GZRadioStation_SetFilter, reinterpret_cast<void*>(&SetFilter)},
        {addr::GZRadioStation_ExpandPlaylists, reinterpret_cast<void*>(&ExpandPlaylists)},
        {addr::GZRadioStation_OrderList, reinterpret_cast<void*>(&OrderList)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    static_assert(count == sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < count; ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].first);
        if (!found || !hook::Install(*found, hooks[i].second, &originals[i]))
            return false;
    }
    return true;
}

const bool g_music3Registered = registry::Register({
    "audio.music.part3",
    "audio",
    registry::Kind::Detour,
    registry::Phase::Attach,
    addr::GZSound_Construct8D376E,
    "Our music player's radio stations: their life, playing order, folders and playlists (docs/audio.md)",
    false,
    InstallMusic3,
});

} // namespace
} // namespace opensc4::audio
