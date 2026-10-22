// The GZ framework's sound service 0x3C1CF59E (docs/gzui.md, part u6), on
// the game's own objects: the constructor, the interface, the destructors,
// the table loader and slots 3 to 35. The audio layer makes one for each
// freshness score it loads (interface 0xDC1CF595). Replacements, each off by
// default:
//
//   framework.soundsvc.object    constructor, QueryInterface, Release, the
//                                destructor and the deleting destructor; the
//                                sound table's deleting destructor
//   framework.soundsvc.load      Init (slot 3), and the table's file reader
//                                and text loader it uses
//   framework.soundsvc.play      Prepare, Shutdown, Play, the locked Stop and
//                                SetLevel, Fade and its timer's callback, the
//                                tempo and the beat time, the cell count,
//                                Reseed
//   framework.soundsvc.window    LoadByName, the table, the row and column
//                                and their windows, the plain setters, the
//                                instance getter
//   framework.soundsvc.settings  slots 28 to 35: the neighbour grids and
//                                their size, the key
//
// Still the game's: the adjustors 0x00948C01 and 0x00948C09, the AddRef
// 0x0094C47C, the getter 0x00948B85 (the description's vtable holds it too),
// the thunks 0x00948321 (the timer's callback, jumping to 0x0094761C) and
// 0x00948568 (slot 36, GZRefPtr::Assign on +0x4140), and slots 38 to 64 and
// the message target's slot 3 (part u7). Ours call each other directly where
// the game does; a virtual call stays one.
//
// The game's methods answer a byte (AL) over whatever its last call or
// value left in EAX; ours give the same EAX where it comes from a call or a
// value and 0 above AL where the game leaves its caller's EAX.
//
// Every detour is entered through a thunk that keeps ECX and EDX. The
// groups go in at Attach, with the rest of the framework's sound layer.
#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/thunk.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

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

void* New(uint32_t size) { return CallCdecl<void*>(addr::SC4_operator_new, size); }
uint32_t Delete(void* p) { return CallCdecl<uint32_t>(addr::SC4_operator_delete, p); }
uint32_t Free(void* p) { return CallCdecl<uint32_t>(addr::SC4_Free, p); }

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("gzui: soundsvc: first %s", name);
}

#define OPENSC4_FIRST(name)                                                                                    \
    do {                                                                                                       \
        static bool logged_ = false;                                                                           \
        if (!logged_)                                                                                          \
            FirstCall(name, logged_);                                                                          \
    } while (0)

// The game's string (vtable, buffer begin, end, capacity) on our stack.
struct Text {
    alignas(4) char bytes[0x14];
    void* get() { return bytes; }
    char* begin() { return At<char*>(bytes, 0x4); }
    char* end() { return At<char*>(bytes, 0x8); }
};

void MakeEmpty(Text& text) { CallThis<void*>(addr::SC4String_SC4String, text.get()); }

// The destructor as the game writes it inline: the vtable stored, the buffer
// freed (whose EAX it gives; `eax` stays when there is none).
void Destroy(Text& text, uint32_t& eax)
{
    At<std::uintptr_t>(text.bytes, 0) = addr::SC4String_Vtable;
    if (char* const body = text.begin())
        eax = Free(body);
}

void Destroy(Text& text)
{
    uint32_t eax = 0;
    Destroy(text, eax);
}

// What stands before the first separator, taken off `from` into `into`.
void SplitInto(Text& into, void* from, const char* separator)
{
    Text piece;
    void* const made = CallThis<void*>(addr::SC4String_SplitOff, from, piece.get(), separator);
    CallThis<void*>(addr::cRZBaseString_Assign, into.get(), made);
    Destroy(piece);
}

// The service's lock (0x00B634AC) held for a scope; Unlock gives the lock's
// slot 2's EAX.
struct Locked {
    alignas(4) char guard[8];
    Locked() { CallThis<void*>(addr::SC4LockGuard_Construct, guard, reinterpret_cast<void*>(addr::cGZSoundService_Lock)); }
    uint32_t Unlock() { return Virtual<uint32_t>(At<void*>(guard, 4), 0x8); }
    Locked(const Locked&) = delete;
    Locked& operator=(const Locked&) = delete;
};

constexpr uint32_t kIID_Service = 0xDC1CF595;
constexpr uint32_t kIID_FileView = 0xC56B8F08;

// ====================================================================================
// The sound table's text form: a "version" line, two lines, a line of 14
// settings, a line, then one line per cell of the rows by columns given:
// tab-separated numbers, a name ("NONE" for an empty cell) and pairs
// "(a,b)" for the description's list.

constexpr std::size_t kTableCells = 0x48;
constexpr std::size_t kTableRows = 0x28;
constexpr std::size_t kTableColumns = 0x2C;

using Scan = int(__cdecl*)(const char*, const char*, ...);

// The game's 0x00946039 (`ret 4`): the table's settings and cells from the
// text, which it consumes. Versions 5 and 6 read 17 numbers a cell, 7 reads
// 18 (the last the description's +0x60, which 5 and 6 clear). Answers AL 1.
uint32_t __fastcall TableLoad(char* self, void*, void* input)
{
    OPENSC4_FIRST("TableLoad");
    const Scan scan = reinterpret_cast<Scan>(addr::SC4_sscanf);
    Text line;
    MakeEmpty(line);
    uint32_t eax = CallThis<uint32_t>(addr::cRZBaseString_BeginsWith, input, "version", 7u);
    if (Yes(eax)) {
        SplitInto(line, input, "version");
        SplitInto(line, input, "\n");
        eax = CallCdecl<uint32_t>(addr::SC4_TextToUint32, CallThis<const char*>(addr::cRZBaseString_Data, line.get()));
        const uint32_t version = eax;
        if (version == 7 || version == 6 || version == 5) {
            SplitInto(line, input, "\n");
            SplitInto(line, input, "\n");
            scan(line.begin(), "\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\n", self + 0x8,
                 self + 0xC, self + 0x10, self + 0x14, self + kTableRows, self + kTableColumns, self + 0x30,
                 self + 0x34, self + 0x24, self + 0x20, self + 0x38, self + 0x3C, self + 0x18, self + 0x1C);
            SplitInto(line, input, "\n");
            eax = Bits(self + kTableRows);
            // The game's flags live across cells too: a short line keeps the
            // last cell's.
            int32_t flag30 = 0, flag31 = 0, flag44 = 0, flag50 = 0, flag20 = 0, flag60 = 0;
            int32_t pair[2] = {0, 0};
            char name[0x104];
            for (int32_t row = 0; row < At<int32_t>(self, kTableRows); ++row) {
                char** cell = reinterpret_cast<char**>(self + kTableCells + row * 0x100);
                for (int32_t column = 0; column < At<int32_t>(self, kTableColumns); ++column, ++cell) {
                    char* desc = static_cast<char*>(New(0x68));
                    if (desc)
                        desc = CallThis<char*>(addr::cGZSoundDesc_Construct, desc);
                    SplitInto(line, input, "\n");
                    std::memset(name, 0, sizeof name);
                    if (version == 7) {
                        scan(line.begin(),
                             "\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%s",
                             desc + 0x24, &flag30, desc + 0x28, desc + 0x2C, &flag31, desc + 0x34, desc + 0x38,
                             desc + 0x3C, &flag44, desc + 0x48, &flag50, desc + 0x4C, desc + 0x40, desc + 0x54,
                             desc + 0x58, &flag20, desc + 0x5C, &flag60, name);
                        At<uint8_t>(desc, 0x31) = flag31 != 0;
                        At<uint8_t>(desc, 0x30) = flag30 != 0;
                        At<uint8_t>(desc, 0x44) = flag44 != 0;
                        At<uint8_t>(desc, 0x50) = flag50 != 0;
                        At<uint8_t>(desc, 0x20) = flag20 != 0;
                        At<uint8_t>(desc, 0x60) = flag60 != 0;
                    } else {
                        scan(line.begin(),
                             "\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%s",
                             desc + 0x24, &flag30, desc + 0x28, desc + 0x2C, &flag31, desc + 0x34, desc + 0x38,
                             desc + 0x3C, &flag44, desc + 0x48, &flag50, desc + 0x4C, desc + 0x40, desc + 0x54,
                             desc + 0x58, &flag20, desc + 0x5C, name);
                        At<uint8_t>(desc, 0x31) = flag31 != 0;
                        At<uint8_t>(desc, 0x30) = flag30 != 0;
                        At<uint8_t>(desc, 0x44) = flag44 != 0;
                        At<uint8_t>(desc, 0x50) = flag50 != 0;
                        At<uint8_t>(desc, 0x60) = 0;
                        At<uint8_t>(desc, 0x20) = flag20 != 0;
                    }

                    // The pairs: the text up to "(" taken off first; while the
                    // piece before each "(" is not empty, one "a,b" up to ")".
                    Text part;
                    MakeEmpty(part);
                    Text pieces;
                    {
                        Text taken;
                        void* const made = CallThis<void*>(addr::SC4String_SplitOff, line.get(), taken.get(), "(");
                        CallThis<void*>(addr::SC4String_Substring, pieces.get(), made, 0u, 0xFFFFFFFFu);
                        Destroy(taken);
                    }
                    while (pieces.end() != pieces.begin()) {
                        SplitInto(part, line.get(), ")");
                        scan(part.begin(), "%ld,%ld", &pair[0], &pair[1]);
                        CallThis<void*>(addr::GZPairList_PushBack, desc + 0x64, pair);
                        SplitInto(pieces, line.get(), "(");
                    }

                    // The name as written; the cell kept unless it is NONE in
                    // capitals.
                    Text upper;
                    CallThis<void*>(addr::SC4String_FromText, upper.get(), static_cast<const char*>(name));
                    CallThis<void*>(addr::cRZBaseString_Assign, desc + 0xC, upper.get());
                    CallThis<void>(addr::cRZBaseString_MakeUpper, upper.get());
                    Text none;
                    char* const made = CallThis<char*>(addr::SC4String_FromText, none.get(), "NONE");
                    const bool empty =
                        Yes(CallCdecl<uint32_t>(addr::SC4UI_F43AB70, upper.bytes + 4, made ? made + 4 : nullptr));
                    Destroy(none);
                    if (empty) {
                        CallThis<uint32_t>(addr::cGZSoundDesc_Destruct, desc);
                        Delete(desc);
                        *cell = nullptr;
                    } else {
                        *cell = desc;
                    }
                    Destroy(upper);
                    Destroy(pieces);
                    Destroy(part);
                }
            }
        }
    }
    Destroy(line, eax);
    return Answer(eax, true);
}

// The game's 0x00946A79 (`ret 4`): the whole file (its slot 7 the size, 14
// a read, 5 a close) as text, loaded. AL 0 for an empty file or a failed
// allocation.
uint32_t __fastcall TableRead(char* self, void*, void* file)
{
    OPENSC4_FIRST("TableRead");
    Text text;
    MakeEmpty(text);
    const uint32_t size = Virtual<uint32_t>(file, 0x1C);
    uint32_t eax = 0;
    if (size == 0) {
        Destroy(text, eax);
        return Answer(eax, false);
    }
    char* const buffer = static_cast<char*>(New(size + 1));
    if (!buffer) {
        Destroy(text, eax);
        return Answer(eax, false);
    }
    Virtual<uint32_t>(file, 0x38, static_cast<void*>(buffer), size);
    Virtual<uint32_t>(file, 0x14);
    buffer[size] = 0;
    CallThis<uint32_t>(addr::cRZBaseString_FromChar, text.get(), static_cast<const char*>(buffer));
    Delete(buffer);
    eax = TableLoad(self, nullptr, text.get());
    const bool loaded = Yes(eax);
    Destroy(text, eax);
    return Answer(eax, loaded);
}

// The game's 0x0094601D (slot 4 of the table, `ret 4`).
char* __fastcall TableDeletingDestructor(char* self, void*, uint32_t flags)
{
    OPENSC4_FIRST("TableDeletingDestructor");
    CallThis<uint32_t>(addr::cGZSoundTable_Destruct, self);
    if (flags & 1)
        Delete(self);
    return self;
}

// ====================================================================================
// The sound service 0x3C1CF59E (0x4144 bytes; the message target part at +4)

constexpr std::size_t kStarted = 0x8;  // byte: Prepare ran
constexpr std::size_t kCount = 0xC;
constexpr std::size_t kRandom = 0x10;  // the generator (0x18 bytes)
constexpr std::size_t kLevel = 0x14;
constexpr std::size_t kWatch = 0x18;   // a stopwatch (0x18 bytes), made by Play
constexpr std::size_t kTable = 0x1C;
constexpr std::size_t kPlaying = 0x20; // byte
constexpr std::size_t kTempo = 0x24;   // beats a minute
constexpr std::size_t kColumn = 0x28;
constexpr std::size_t kRow = 0x2C;
constexpr std::size_t kCells = 0x38;   // descriptions in the window
constexpr std::size_t kBeat = 0x40;
constexpr std::size_t kFirstBeat = 0x44;
constexpr std::size_t kGroups = 0x50;
constexpr std::size_t kColumnSpread = 0x54;
constexpr std::size_t kRowSpread = 0x58;
constexpr std::size_t kInstances = 0x74; // 64 by 64
constexpr std::size_t kName = 0x4074;    // a game string
constexpr std::size_t kFading = 0x4091;  // byte
constexpr std::size_t kTimer = 0x4094;
constexpr std::size_t kFadeStep = 0x4098;
constexpr std::size_t kFadeTarget = 0x409C;
constexpr std::size_t kFadeStop = 0x40A0; // byte: stop at the end
constexpr std::size_t kGridRows = 0x40B4;
constexpr std::size_t kGridColumns = 0x40B8;
constexpr std::size_t kGrid = 0x40BD;    // 3 by 3 bytes
constexpr std::size_t kGridA = 0x40C8;   // 3 by 3 dwords, three of them
constexpr std::size_t kGridB = 0x40EC;
constexpr std::size_t kGridC = 0x4110;
constexpr std::size_t kKey = 0x4134;     // type, group, instance
constexpr std::size_t kHeld = 0x4140;    // a counted reference

constexpr uint32_t kSide = 64;

// The game's 0x009489AE.
char* __fastcall ServiceConstruct(char* self, void*)
{
    OPENSC4_FIRST("ServiceConstruct");
    At<std::uintptr_t>(self, 0x4) = addr::cGZSoundService_TargetBaseVtable;
    At<uint32_t>(self, kColumnSpread) = 3;
    At<uint32_t>(self, kRowSpread) = 3;
    At<std::uintptr_t>(self, 0) = addr::cGZSoundService_Vtable;
    At<std::uintptr_t>(self, 0x4) = addr::cGZSoundService_TargetVtable;
    At<uint8_t>(self, kStarted) = 0;
    At<uint32_t>(self, kCount) = 0;
    At<uint32_t>(self, kRandom) = 0;
    At<uint32_t>(self, kLevel) = 0x400;
    At<uint32_t>(self, kWatch) = 0;
    At<uint32_t>(self, kTable) = 0;
    At<uint8_t>(self, kPlaying) = 0;
    At<uint8_t>(self, 0x21) = 0;
    At<uint32_t>(self, kTempo) = 0;
    At<uint32_t>(self, kColumn) = 1;
    At<uint32_t>(self, kRow) = 1;
    At<uint32_t>(self, 0x30) = 1;
    At<uint32_t>(self, 0x34) = 1;
    At<uint32_t>(self, kCells) = 0xF;
    At<uint32_t>(self, 0x3C) = 0;
    At<uint32_t>(self, kBeat) = 1;
    At<uint32_t>(self, kFirstBeat) = 0;
    At<uint32_t>(self, 0x48) = 0;
    At<uint32_t>(self, 0x4C) = 4;
    At<uint32_t>(self, 0x5C) = 0;
    At<uint32_t>(self, 0x60) = 0;
    At<uint32_t>(self, 0x64) = 0;
    At<uint32_t>(self, 0x68) = 0;
    At<uint32_t>(self, 0x6C) = 1;
    At<uint32_t>(self, 0x70) = 1;
    CallThis<void*>(addr::SC4String_SC4String, self + kName);
    At<uint32_t>(self, 0x4088) = 100;
    At<uint32_t>(self, kFadeStep) = 100;
    At<uint32_t>(self, 0x40A4) = 100;
    At<uint32_t>(self, kGridRows) = 2;
    At<uint32_t>(self, kGridColumns) = 2;
    At<uint32_t>(self, 0x408C) = 0;
    At<uint8_t>(self, 0x4090) = 0;
    At<uint8_t>(self, kFading) = 0;
    At<uint32_t>(self, kTimer) = 0;
    At<uint32_t>(self, kFadeTarget) = 0;
    At<uint32_t>(self, 0x40A8) = 1000;
    At<uint32_t>(self, 0x40AC) = 0;
    At<uint32_t>(self, 0x40B0) = 0;
    At<uint8_t>(self, 0x40BC) = 1;
    At<uint32_t>(self, kKey) = 0;
    At<uint32_t>(self, kKey + 4) = 0;
    At<uint32_t>(self, kKey + 8) = 0;
    At<uint32_t>(self, kHeld) = 0;
    void* random = New(0x18);
    if (random)
        random = CallThis<void*>(addr::SC4Random_Construct, random, -1);
    At<void*>(self, kRandom) = random;
    CallThis<void>(addr::cRZRandom_Seed, random, 0xFFFFFFFFu);
    std::memset(self + kInstances, 0, kSide * kSide * 4);
    std::memset(self + kGrid, 0, 9);
    At<uint8_t>(self, kGrid + 3) = 1;
    At<uint8_t>(self, kGrid + 1) = 1;
    At<uint8_t>(self, kGrid + 7) = 1;
    At<uint8_t>(self, kGrid + 5) = 1;
    // The three grids: by row, 0 and by column, in steps of +0x40A4.
    for (uint32_t row = 0; row < At<uint32_t>(self, kGridRows) + 1; ++row) {
        int32_t down = 1;
        for (uint32_t column = 0; column < At<uint32_t>(self, kGridColumns) + 1; ++column, --down) {
            const std::size_t at = row * 0xC + column * 4;
            At<int32_t>(self, kGridA + at) = At<int32_t>(self, 0x40A4) * static_cast<int32_t>(row - 1);
            At<uint32_t>(self, kGridB + at) = 0;
            At<int32_t>(self, kGridC + at) = At<int32_t>(self, 0x40A4) * down;
        }
    }
    return self;
}

// The game's 0x00946FE6 (slot 0, through the adjustor 0x00948C01; `ret 8`):
// the interface 0xDC1CF595 only, not 1.
uint32_t __fastcall ServiceQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    OPENSC4_FIRST("ServiceQueryInterface");
    if (iid != kIID_Service)
        return 0;
    *out = self;
    return Answer(AddRef(self), true);
}

// The game's 0x00947004 (slot 2, through the adjustor 0x00948C09): the
// last one (a count of exactly 1) deletes it through slot 37.
uint32_t __fastcall ServiceRelease(char* self, void*)
{
    OPENSC4_FIRST("ServiceRelease");
    const uint32_t count = At<uint32_t>(self, kCount);
    if (count == 1) {
        Virtual<uint32_t>(self, 0x94, 1u);
        return 0;
    }
    return At<uint32_t>(self, kCount) = count - 1;
}

uint32_t __fastcall ServiceShutdown(char* self, void*);

// The game's 0x00948C54: the generator deleted (not destroyed), Shutdown,
// the reference released, the name destroyed (whose EAX it answers).
uint32_t __fastcall ServiceDestruct(char* self, void*)
{
    OPENSC4_FIRST("ServiceDestruct");
    At<std::uintptr_t>(self, 0) = addr::cGZSoundService_Vtable;
    At<std::uintptr_t>(self, 0x4) = addr::cGZSoundService_TargetVtable;
    if (void* const random = At<void*>(self, kRandom)) {
        Delete(random);
        At<void*>(self, kRandom) = nullptr;
    }
    ServiceShutdown(self, nullptr);
    if (void* const held = At<void*>(self, kHeld))
        Release(held);
    return CallThis<uint32_t>(addr::SC4String_Destroy, self + kName);
}

// The game's 0x00948C98 (slot 37, `ret 4`).
char* __fastcall ServiceDeletingDestructor(char* self, void*, uint32_t flags)
{
    OPENSC4_FIRST("ServiceDeletingDestructor");
    ServiceDestruct(self, nullptr);
    if (flags & 1)
        Delete(self);
    return self;
}

// The game's 0x00947C38 (slot 3, `ret 4`): the key set (slot 34), its
// record found by the resource manager and opened as a file (0xC56B8F08), a
// new table read from it, then Prepare (slot 4). AL whether all of it went.
uint32_t __fastcall ServiceInit(char* self, void*, const uint32_t* key)
{
    OPENSC4_FIRST("ServiceInit");
    bool ok = false;
    Virtual<uint32_t>(self, 0x88, key);
    void* resources = nullptr;
    CallThis<void*>(addr::SC4RefPtr_ResourceManager, &resources);
    if (resources) {
        void* record = nullptr;
        if (Yes(Virtual<uint32_t>(resources, 0x80, key, &record))) {
            void* opened = nullptr;
            if (Yes(Virtual<uint32_t>(record, 0x54, key, &opened, 1u))) {
                void* file = nullptr;
                if (Yes(Virtual<uint32_t>(opened, 0x0, kIID_FileView, &file))) {
                    alignas(4) char view[0x10];
                    CallThis<void*>(addr::cGZDBRawFileView_Construct, view, file);
                    char* table = static_cast<char*>(New(0x4048));
                    if (table)
                        table = CallThis<char*>(addr::cGZSoundTable_Construct, table);
                    At<char*>(self, kTable) = table;
                    ok = Yes(TableRead(table, nullptr, view + 4));
                    CallThis<uint32_t>(addr::cGZDBRawFileView_Close, view + 4);
                    Release(file);
                    CallThis<void>(addr::cGZDBRawFileView_Destruct, view);
                }
                Release(opened);
            }
            Release(record);
            if (ok)
                ok = Yes(Virtual<uint32_t>(self, 0x10));
        }
    }
    return Answer(CallThis<uint32_t>(addr::SC4RefPtr_Destruct, &resources), ok);
}

// The game's 0x0094701E (slot 4): once, under the lock: the settings from
// the table, an instance for every description (made with the service, its
// slot 4 given the description), the cells in the window counted by slot
// 52.
uint32_t __fastcall ServicePrepare(char* self, void*)
{
    OPENSC4_FIRST("ServicePrepare");
    Locked lock;
    if (At<uint8_t>(self, kStarted))
        return Answer(lock.Unlock(), true);
    At<int32_t>(self, 0x5C) = -1;
    At<int32_t>(self, 0x60) = -1;
    At<int32_t>(self, 0x64) = -1;
    At<int32_t>(self, 0x68) = -1;
    char* const table = At<char*>(self, kTable);
    At<uint32_t>(self, kLevel) = At<uint32_t>(table, 0x8);
    At<uint32_t>(self, kColumn) = 1;
    At<uint32_t>(self, kRow) = 1;
    At<uint32_t>(self, 0x30) = 1;
    At<uint32_t>(self, 0x48) = At<uint32_t>(table, 0x10);
    At<uint32_t>(self, 0x4C) = At<uint32_t>(table, 0x14);
    At<uint32_t>(self, kColumnSpread) = At<uint32_t>(table, 0x24);
    At<uint32_t>(self, kRowSpread) = At<uint32_t>(table, 0x20);
    for (int32_t row = 0; row < At<int32_t>(At<char*>(self, kTable), kTableRows); ++row) {
        for (int32_t column = 0; column < At<int32_t>(At<char*>(self, kTable), kTableColumns); ++column) {
            void* const desc = Virtual<void*>(At<void*>(self, kTable), 0xC, row, column, 0u);
            if (!desc)
                continue;
            char* instance = static_cast<char*>(New(0x5C));
            if (instance)
                instance = CallThis<char*>(addr::cGZSoundInstance_ConstructOwned, instance, static_cast<void*>(self));
            Virtual<uint32_t>(instance, 0x10, desc);
            At<char*>(self, kInstances + (static_cast<uint32_t>(row) * kSide + column) * 4) = instance;
        }
    }
    At<uint32_t>(self, kCells) = 0;
    for (int32_t row = 0; row < At<int32_t>(At<char*>(self, kTable), kTableRows); ++row) {
        for (int32_t column = 0; column < At<int32_t>(At<char*>(self, kTable), kTableColumns); ++column) {
            if (Yes(Virtual<uint32_t>(self, 0xD0, row, column)))
                ++At<uint32_t>(self, kCells);
        }
    }
    At<uint8_t>(self, kStarted) = 1;
    return Answer(lock.Unlock(), true);
}

// The game's 0x00948C11 (slot 5, `ret 8`; the second argument is not
// read): slot 38 with the text as a game string.
uint32_t __fastcall ServiceLoadByName(char* self, void*, const void* name, uint32_t)
{
    OPENSC4_FIRST("ServiceLoadByName");
    Text text;
    CallThis<void*>(addr::SC4String_FromIGZString, text.get(), name);
    uint32_t eax = Virtual<uint32_t>(self, 0x98, text.get());
    const bool done = Yes(eax);
    Destroy(text, eax);
    return Answer(eax, done);
}

// The game's 0x00948B7F (slot 6, `ret 4`; the argument is not read).
void* __fastcall ServiceTable(char* self, void*, uint32_t)
{
    OPENSC4_FIRST("ServiceTable");
    return At<void*>(self, kTable);
}

// The game's 0x00947D28 (slot 7): the fade stopped (slot 42), then under the
// lock, once prepared: slot 10, the table deleted (its slot 4) and every
// instance (its part at +4's slot 7); always the bytes +8 and +0x20 cleared
// and the reference let go.
uint32_t __fastcall ServiceShutdown(char* self, void*)
{
    OPENSC4_FIRST("ServiceShutdown");
    Virtual<uint32_t>(self, 0xA8);
    Locked lock;
    if (At<uint8_t>(self, kStarted)) {
        Virtual<uint32_t>(self, 0x28);
        if (void* const table = At<void*>(self, kTable)) {
            Virtual<uint32_t>(table, 0x10, 1u);
            At<void*>(self, kTable) = nullptr;
        }
        char** cell = reinterpret_cast<char**>(self + kInstances);
        for (uint32_t i = 0; i < kSide * kSide; ++i, ++cell) {
            if (*cell) {
                Virtual<uint32_t>(*cell + 4, 0x1C, 1u);
                *cell = nullptr;
            }
        }
    }
    At<uint8_t>(self, kPlaying) = 0;
    At<uint8_t>(self, kStarted) = 0;
    CallThis<void*>(addr::GZRefPtr_Assign, self + kHeld, static_cast<void*>(nullptr));
    return Answer(lock.Unlock(), true);
}

// The game's 0x009476C3: the descriptions in the window (slots 22 to 25,
// asked again on every step) counted into +0x38, and into +0x50 those of
// group 0 and the first of each other group (the description's +0x5C; the
// game's marks are 32 bytes on its stack, so groups 0 to 31: a larger one
// writes over the game's loop counters, ours counts it every time). Answers
// slot 23's last EAX.
uint32_t __fastcall ServiceCountCells(char* self, void*)
{
    OPENSC4_FIRST("ServiceCountCells");
    At<uint32_t>(self, kCells) = 0;
    At<uint32_t>(self, kGroups) = 0;
    uint8_t seen[0x20] = {};
    int32_t row = Virtual<int32_t>(self, 0x58);
    uint32_t eax = Virtual<uint32_t>(self, 0x5C);
    if (row > static_cast<int32_t>(eax))
        return eax;
    uint32_t base = static_cast<uint32_t>(row) << 6;
    do {
        int32_t column = Virtual<int32_t>(self, 0x60);
        if (column <= Virtual<int32_t>(self, 0x64)) {
            uint32_t offset = (static_cast<uint32_t>(column) + base) * 4 + kTableCells;
            do {
                char* const desc = *reinterpret_cast<char**>(At<std::uintptr_t>(self, kTable) + offset);
                if (desc) {
                    const uint32_t group = At<uint32_t>(desc, 0x5C);
                    const bool marked = group < sizeof seen;
                    if (group == 0 || !marked || !seen[group]) {
                        if (group != 0 && marked)
                            seen[group] = 1;
                        ++At<uint32_t>(self, kGroups);
                    }
                    ++At<uint32_t>(self, kCells);
                }
                ++column;
                offset += 4;
            } while (column <= Virtual<int32_t>(self, 0x64));
        }
        ++row;
        base += 0x40;
        eax = Virtual<uint32_t>(self, 0x5C);
    } while (row <= static_cast<int32_t>(eax));
    return eax;
}

// The game's 0x00947DB8 (slot 8): under the lock, once prepared and not
// playing: the tempo and the first beat from the table (+0x30, +0x34; one
// less above 2), the cells counted, a new stopwatch started (the old one is
// not freed), the column and row set (slots 14 and 13 with the table's +0x40
// and +0x44), the byte +0x20 set, and the message target part given to the
// framework's sound service (its slot 30, 250). A fade going on is stopped
// first.
uint32_t __fastcall ServicePlay(char* self, void*)
{
    OPENSC4_FIRST("ServicePlay");
    Locked lock;
    if (At<uint8_t>(self, kStarted)) {
        if (At<uint8_t>(self, kFading))
            Virtual<uint32_t>(self, 0xA8);
        if (!At<uint8_t>(self, kPlaying)) {
            char* const table = At<char*>(self, kTable);
            At<uint8_t>(self, 0x21) = 0;
            At<uint32_t>(self, kTempo) = At<uint32_t>(table, 0x30);
            At<uint32_t>(self, kFirstBeat) = At<uint32_t>(table, 0x34);
            if (At<int32_t>(table, 0x34) > 2)
                At<uint32_t>(self, kFirstBeat) = At<uint32_t>(table, 0x34) - 1;
            At<uint32_t>(self, 0x3C) = At<uint32_t>(self, kFirstBeat);
            At<uint32_t>(self, kBeat) = At<uint32_t>(self, kFirstBeat);
            ServiceCountCells(self, nullptr);
            void* watch = New(0x18);
            if (watch)
                watch = CallThis<void*>(addr::SC4Stopwatch_Construct, watch, 4u);
            At<void*>(self, kWatch) = watch;
            CallThis<void>(addr::SC4Stopwatch_Start, watch);
            Virtual<uint32_t>(self, 0x38, At<uint32_t>(At<char*>(self, kTable), 0x40));
            Virtual<uint32_t>(self, 0x34, At<uint32_t>(At<char*>(self, kTable), 0x44));
            At<uint8_t>(self, kPlaying) = 1;
            if (CallCdecl<void*>(addr::SC4_Singleton913C9E)) {
                void* const sound = CallCdecl<void*>(addr::SC4_Singleton913C9E);
                Virtual<uint32_t>(sound, 0x78, static_cast<void*>(self + 4), 0xFAu);
            }
        }
    }
    return Answer(lock.Unlock(), true);
}

// The game's 0x00947167 (slot 10): Stop (slot 53) under the lock.
uint32_t __fastcall ServiceStop(char* self, void*)
{
    OPENSC4_FIRST("ServiceStop");
    Locked lock;
    const bool done = Yes(Virtual<uint32_t>(self, 0xD4));
    return Answer(lock.Unlock(), done);
}

// The game's 0x00947559 (slot 11, `ret 4`): every instance's level (slot
// 43) under the lock.
uint32_t __fastcall ServiceSetLevel(char* self, void*, uint32_t level)
{
    OPENSC4_FIRST("ServiceSetLevel");
    Locked lock;
    const bool done = Yes(Virtual<uint32_t>(self, 0xAC, level));
    return Answer(lock.Unlock(), done);
}

// The game's 0x0094832A (slot 12, `ret 0xC`): a fade over `time` to `level`
// in steps of a twentieth, while playing; a step of 0 sets the level at
// once (and stops when asked). Otherwise a timer (0x28 bytes) calls
// 0x00948321 every time / 20 unless one runs already. A stopwatch is made
// and destroyed on the stack, never started.
uint32_t __fastcall ServiceFade(char* self, void*, int32_t time, uint32_t level, uint32_t stop)
{
    OPENSC4_FIRST("ServiceFade");
    Locked lock;
    alignas(4) char watch[0x18];
    CallThis<void*>(addr::SC4Stopwatch_Construct, watch, 4u);
    if (!At<uint8_t>(self, kStarted) || !At<uint8_t>(self, kPlaying)) {
        CallThis<void>(addr::SC4Stopwatch_Destruct, watch);
        return Answer(lock.Unlock(), true);
    }
    const int32_t step = static_cast<int32_t>(level - At<uint32_t>(self, kLevel)) / 20;
    const uint8_t stopAtEnd = static_cast<uint8_t>(stop);
    At<uint8_t>(self, kFadeStop) = stopAtEnd;
    At<uint32_t>(self, kFadeTarget) = level;
    At<int32_t>(self, kFadeStep) = step;
    if (step == 0) {
        Virtual<uint32_t>(self, 0x2C, level);
        if (stopAtEnd)
            Virtual<uint32_t>(self, 0x28);
    } else if (!At<uint8_t>(self, kFading)) {
        void* timer = New(0x28);
        if (timer)
            timer = CallThis<void*>(addr::cRZTimer_Construct, timer);
        At<void*>(self, kTimer) = timer;
        Virtual<uint32_t>(timer, 0x28, addr::cGZSoundService_TimerThunk, static_cast<void*>(self));
        Virtual<uint32_t>(At<void*>(self, kTimer), 0x4, 100u);
        Virtual<uint32_t>(At<void*>(self, kTimer), 0x14, time / 20, 0u);
        At<uint8_t>(self, kFading) = 1;
    }
    CallThis<void>(addr::SC4Stopwatch_Destruct, watch);
    return Answer(lock.Unlock(), true);
}

// The game's 0x0094761C (the timer's callback, through 0x00948321): nothing
// while the lock is held; else under it, while fading: the level one step
// on (0 to 0x400) to slot 43, and at or past the target the fade stopped
// (slot 42), then for a fade down asked to, Stop (slot 53).
uint32_t __fastcall ServiceFadeStep(char* self, void*)
{
    OPENSC4_FIRST("ServiceFadeStep");
    const uint32_t held =
        CallThis<uint32_t>(addr::RZCriticalSection_GetLockCount, reinterpret_cast<void*>(addr::cGZSoundService_Lock));
    if (held != 0)
        return held;
    Locked lock;
    if (At<uint8_t>(self, kFading)) {
        int32_t level = static_cast<int32_t>(At<uint32_t>(self, kLevel) + At<uint32_t>(self, kFadeStep));
        if (level < 0)
            level = 0;
        if (level > 0x400)
            level = 0x400;
        Virtual<uint32_t>(self, 0xAC, level);
        const int32_t step = At<int32_t>(self, kFadeStep);
        const int32_t target = At<int32_t>(self, kFadeTarget);
        if (step < 0) {
            if (level <= target) {
                Virtual<uint32_t>(self, 0xA8);
                if (At<uint8_t>(self, kFadeStop))
                    Virtual<uint32_t>(self, 0xD4);
            }
        } else if (step > 0 && level >= target) {
            Virtual<uint32_t>(self, 0xA8);
        }
    }
    return lock.Unlock();
}

// The game's 0x009475FB (`ret 4`): the milliseconds from the first beat to
// `beat` at the tempo; -1 without one.
uint32_t __fastcall ServiceBeatTime(char* self, void*, uint32_t beat)
{
    OPENSC4_FIRST("ServiceBeatTime");
    const int32_t tempo = At<int32_t>(self, kTempo);
    if (tempo == 0)
        return 0xFFFFFFFFu;
    return static_cast<uint32_t>(static_cast<int32_t>((beat - At<uint32_t>(self, kFirstBeat)) * 60000u) / tempo);
}

// The game's 0x0094776D (slot 15, `ret 4`): +0x34 once prepared.
uint32_t __fastcall ServiceSet34(char* self, void*, uint32_t value)
{
    OPENSC4_FIRST("ServiceSet34");
    if (!At<uint8_t>(self, kStarted))
        return 1;
    At<uint32_t>(self, 0x34) = value;
    return Answer(value, true);
}

// The game's 0x0094777F (slot 16, `ret 4`): +0x4088 once prepared.
uint32_t __fastcall ServiceSet4088(char* self, void*, uint32_t value)
{
    OPENSC4_FIRST("ServiceSet4088");
    if (!At<uint8_t>(self, kStarted))
        return 1;
    At<uint32_t>(self, 0x4088) = value;
    return Answer(value, true);
}

// The game's 0x00947794 (slot 21, `ret 4`): the byte +0x21 once prepared;
// AL 1 over the caller's EAX (ours 1).
uint32_t __fastcall ServiceSet21(char* self, void*, uint32_t value)
{
    OPENSC4_FIRST("ServiceSet21");
    if (At<uint8_t>(self, kStarted))
        At<uint8_t>(self, 0x21) = static_cast<uint8_t>(value);
    return 1;
}

// The game's 0x00948420 (slot 17, `ret 4`): a new tempo while playing (slot
// 9): the stopwatch stopped and restarted, the beat set to the first plus
// +0x3C; the elapsed time and the beat time are asked and not used.
uint32_t __fastcall ServiceSetTempo(char* self, void*, uint32_t tempo)
{
    OPENSC4_FIRST("ServiceSetTempo");
    Locked lock;
    if (!Yes(Virtual<uint32_t>(self, 0x24)) || At<uint32_t>(self, kTempo) == tempo)
        return Answer(lock.Unlock(), true);
    At<uint32_t>(self, kTempo) = tempo;
    CallThis<void>(addr::SC4Stopwatch_Stop, At<void*>(self, kWatch));
    CallThis<void>(addr::GZStopwatch_Start, At<void*>(self, kWatch));
    At<uint32_t>(self, kBeat) = At<uint32_t>(self, kFirstBeat) + At<uint32_t>(self, 0x3C);
    CallThis<uint32_t>(addr::GZStopwatch_ElapsedMilliseconds, At<void*>(self, kWatch));
    const uint32_t beat = At<uint32_t>(self, kBeat);
    ServiceBeatTime(self, nullptr, beat);
    ServiceBeatTime(self, nullptr, beat);
    CallThis<uint32_t>(addr::GZStopwatch_ElapsedMilliseconds, At<void*>(self, kWatch));
    return Answer(lock.Unlock(), true);
}

// A place from 0 to 1000 on `size` cells; on the table's grid (its +0xC)
// snapped to steps of 2 * spread + 1, offset by the spread.
int32_t Snap(char* table, int32_t size, uint32_t place, int32_t spread)
{
    int32_t at = static_cast<int32_t>(static_cast<uint32_t>(size) * place) / 1000;
    if (At<uint32_t>(table, 0xC) != 0) {
        const int32_t step = static_cast<int32_t>(static_cast<uint32_t>(spread) * 2 + 1);
        at = static_cast<int32_t>(static_cast<uint32_t>(at / step) * static_cast<uint32_t>(step) +
                                  static_cast<uint32_t>(spread));
    }
    return at;
}

// The game's 0x00948504 (slot 13, `ret 4`): the row from a place, kept off
// the edges by the row spread, then within 0 and the rows less one; the
// cells counted. Only once prepared.
uint32_t __fastcall ServiceSetRow(char* self, void*, uint32_t place)
{
    OPENSC4_FIRST("ServiceSetRow");
    if (!At<uint8_t>(self, kStarted))
        return 1;
    char* const table = At<char*>(self, kTable);
    const int32_t rows = At<int32_t>(table, kTableRows);
    int32_t at = Snap(table, rows, place, At<int32_t>(self, kRowSpread));
    const int32_t spread = At<int32_t>(self, kRowSpread);
    const int32_t limit = static_cast<int32_t>(static_cast<uint32_t>(rows) - static_cast<uint32_t>(spread));
    if (at >= limit)
        at = static_cast<int32_t>(static_cast<uint32_t>(limit) - 1);
    if (at < spread)
        at = spread;
    const int32_t last = static_cast<int32_t>(static_cast<uint32_t>(rows) - 1);
    if (at > last)
        at = last;
    if (at < 0)
        at = 0;
    At<int32_t>(self, kRow) = at;
    return Answer(ServiceCountCells(self, nullptr), true);
}

// The game's 0x009484A0 (slot 14, `ret 4`): the column likewise, with the
// last two limits the other way round.
uint32_t __fastcall ServiceSetColumn(char* self, void*, uint32_t place)
{
    OPENSC4_FIRST("ServiceSetColumn");
    if (!At<uint8_t>(self, kStarted))
        return 1;
    char* const table = At<char*>(self, kTable);
    const int32_t columns = At<int32_t>(table, kTableColumns);
    int32_t at = Snap(table, columns, place, At<int32_t>(self, kColumnSpread));
    const int32_t spread = At<int32_t>(self, kColumnSpread);
    const int32_t limit = static_cast<int32_t>(static_cast<uint32_t>(columns) - static_cast<uint32_t>(spread));
    if (at >= limit)
        at = static_cast<int32_t>(static_cast<uint32_t>(limit) - 1);
    if (at < spread)
        at = spread;
    if (at < 0)
        at = 0;
    if (at >= columns)
        at = static_cast<int32_t>(static_cast<uint32_t>(columns) - 1);
    At<int32_t>(self, kColumn) = at;
    return Answer(ServiceCountCells(self, nullptr), true);
}

// The window's edges: slots 22 and 24 the first row and column (the place
// less its spread, 0 when that is negative), 23 and 25 the last (the place
// plus its spread, the table's last below its size).
uint32_t __fastcall ServiceRowStart(char* self, void*)
{
    OPENSC4_FIRST("ServiceRowStart");
    const uint32_t at = At<uint32_t>(self, kRow) - At<uint32_t>(self, kRowSpread);
    return static_cast<int32_t>(at) < 0 ? 0 : at;
}

uint32_t __fastcall ServiceRowEnd(char* self, void*)
{
    OPENSC4_FIRST("ServiceRowEnd");
    const uint32_t at = At<uint32_t>(self, kRowSpread) + At<uint32_t>(self, kRow);
    const int32_t rows = At<int32_t>(At<char*>(self, kTable), kTableRows);
    return static_cast<int32_t>(at) < rows ? at : static_cast<uint32_t>(rows) - 1;
}

uint32_t __fastcall ServiceColumnStart(char* self, void*)
{
    OPENSC4_FIRST("ServiceColumnStart");
    const uint32_t at = At<uint32_t>(self, kColumn) - At<uint32_t>(self, kColumnSpread);
    return static_cast<int32_t>(at) < 0 ? 0 : at;
}

uint32_t __fastcall ServiceColumnEnd(char* self, void*)
{
    OPENSC4_FIRST("ServiceColumnEnd");
    const uint32_t at = At<uint32_t>(self, kColumnSpread) + At<uint32_t>(self, kColumn);
    const int32_t columns = At<int32_t>(At<char*>(self, kTable), kTableColumns);
    return static_cast<int32_t>(at) < columns ? at : static_cast<uint32_t>(columns) - 1;
}

// The game's 0x00948B89 (slot 26, `ret 0xC`): the instance at row * 64 +
// column, unchecked; the third argument is not read.
void* __fastcall ServiceInstance(char* self, void*, uint32_t row, uint32_t column, uint32_t)
{
    OPENSC4_FIRST("ServiceInstance");
    const uint32_t index = (row << 6) + column;
    return *reinterpret_cast<void**>(static_cast<std::uintptr_t>(Bits(self) + index * 4 + kInstances));
}

// The game's 0x009479D4 (slot 27, `ret 4`): the generator seeded, the beats
// and +0x48 to +0x68 back to their start, the cells counted; while playing,
// Stop (slot 10) and Play (slot 8).
uint32_t __fastcall ServiceReseed(char* self, void*, uint32_t seed)
{
    OPENSC4_FIRST("ServiceReseed");
    CallThis<void>(addr::cRZRandom_Seed, At<void*>(self, kRandom), seed);
    At<uint32_t>(self, 0x3C) = 0;
    At<uint32_t>(self, kBeat) = 1;
    At<uint32_t>(self, kFirstBeat) = 0;
    At<uint32_t>(self, 0x48) = 0;
    At<uint32_t>(self, 0x4C) = 4;
    At<uint32_t>(self, 0x5C) = 0;
    At<uint32_t>(self, 0x60) = 0;
    At<uint32_t>(self, 0x64) = 0;
    At<uint32_t>(self, 0x68) = 0;
    uint32_t eax = ServiceCountCells(self, nullptr);
    if (At<uint8_t>(self, kPlaying)) {
        Virtual<uint32_t>(self, 0x28);
        eax = Virtual<uint32_t>(self, 0x20);
    }
    return Answer(eax, true);
}

// ====================================================================================
// Settings: slots 28 to 35

// The game's 0x00948B9B (slot 28, `ret 4`): the byte +0x40BC; AL 1 over the
// caller's EAX (ours 1).
uint32_t __fastcall ServiceSet40BC(char* self, void*, uint32_t value)
{
    OPENSC4_FIRST("ServiceSet40BC");
    At<uint8_t>(self, 0x40BC) = static_cast<uint8_t>(value);
    return 1;
}

// The game's 0x00948BAA (slot 29, `ret 8`): +0x40A4 and +0x40A8; AL 1 over
// the second.
uint32_t __fastcall ServiceSet40A4(char* self, void*, uint32_t first, uint32_t second)
{
    OPENSC4_FIRST("ServiceSet40A4");
    At<uint32_t>(self, 0x40A4) = first;
    At<uint32_t>(self, 0x40A8) = second;
    return Answer(second, true);
}

// The game's 0x00947AA9 (slot 30, `ret 4`): the grids' rows less one, below
// 3; AL over the argument.
uint32_t __fastcall ServiceSetGridRows(char* self, void*, uint32_t value)
{
    OPENSC4_FIRST("ServiceSetGridRows");
    if (value >= 3)
        return Answer(value, false);
    At<uint32_t>(self, kGridRows) = value;
    return Answer(value, true);
}

// The game's 0x00947AC1 (slot 31, `ret 4`): the grids' columns less one.
uint32_t __fastcall ServiceSetGridColumns(char* self, void*, uint32_t value)
{
    OPENSC4_FIRST("ServiceSetGridColumns");
    if (value >= 3)
        return Answer(value, false);
    At<uint32_t>(self, kGridColumns) = value;
    return Answer(value, true);
}

// The game's 0x00947A2A (slot 32, `ret 0xC`): one byte of the 3 by 3 grid;
// AL over the row.
uint32_t __fastcall ServiceSetGrid(char* self, void*, uint32_t row, uint32_t column, uint32_t value)
{
    OPENSC4_FIRST("ServiceSetGrid");
    if (row >= 3 || column >= 3)
        return Answer(row, false);
    At<uint8_t>(self, kGrid + row * 3 + column) = static_cast<uint8_t>(value);
    return Answer(row, true);
}

// The game's 0x00947A55 (slot 33, `ret 0x14`): one cell of each of the three
// dword grids. AL 0 over the caller's EAX for a bad row (ours 0), over the
// column for a bad column; AL 1 over the third grid's index.
uint32_t __fastcall ServiceSetGridCell(char* self, void*, uint32_t row, uint32_t column, uint32_t a, uint32_t b,
                                       uint32_t c)
{
    OPENSC4_FIRST("ServiceSetGridCell");
    if (row >= 3)
        return 0;
    if (column >= 3)
        return Answer(column, false);
    const uint32_t at = (row * 3 + column) * 4;
    At<uint32_t>(self, kGridA + at) = a;
    At<uint32_t>(self, kGridB + at) = b;
    At<uint32_t>(self, kGridC + at) = c;
    return Answer(column + (row + 0x56C) * 3, true);
}

// The game's 0x00948BDD (slot 34, `ret 4`): the key copied in; answers
// where it is.
void* __fastcall ServiceSetKey(char* self, void*, const uint32_t* key)
{
    OPENSC4_FIRST("ServiceSetKey");
    At<uint32_t>(self, kKey) = key[0];
    At<uint32_t>(self, kKey + 4) = key[1];
    At<uint32_t>(self, kKey + 8) = key[2];
    return self + kKey;
}

// The game's 0x00948BFA (slot 35).
void* __fastcall ServiceKey(char* self, void*)
{
    OPENSC4_FIRST("ServiceKey");
    return self + kKey;
}

// ====================================================================================
// Thunks and installation

OPENSC4_THUNK(TableLoadThunk, TableLoad, 4)
OPENSC4_THUNK(TableReadThunk, TableRead, 4)
OPENSC4_THUNK(TableDeletingDestructorThunk, TableDeletingDestructor, 4)

OPENSC4_THUNK(ServiceConstructThunk, ServiceConstruct, 0)
OPENSC4_THUNK(ServiceQueryInterfaceThunk, ServiceQueryInterface, 8)
OPENSC4_THUNK(ServiceReleaseThunk, ServiceRelease, 0)
OPENSC4_THUNK(ServiceDestructThunk, ServiceDestruct, 0)
OPENSC4_THUNK(ServiceDeletingDestructorThunk, ServiceDeletingDestructor, 4)
OPENSC4_THUNK(ServiceInitThunk, ServiceInit, 4)
OPENSC4_THUNK(ServicePrepareThunk, ServicePrepare, 0)
OPENSC4_THUNK(ServiceLoadByNameThunk, ServiceLoadByName, 8)
OPENSC4_THUNK(ServiceTableThunk, ServiceTable, 4)
OPENSC4_THUNK(ServiceShutdownThunk, ServiceShutdown, 0)
OPENSC4_THUNK(ServiceCountCellsThunk, ServiceCountCells, 0)
OPENSC4_THUNK(ServicePlayThunk, ServicePlay, 0)
OPENSC4_THUNK(ServiceStopThunk, ServiceStop, 0)
OPENSC4_THUNK(ServiceSetLevelThunk, ServiceSetLevel, 4)
OPENSC4_THUNK(ServiceFadeThunk, ServiceFade, 12)
OPENSC4_THUNK(ServiceFadeStepThunk, ServiceFadeStep, 0)
OPENSC4_THUNK(ServiceBeatTimeThunk, ServiceBeatTime, 4)
OPENSC4_THUNK(ServiceSet34Thunk, ServiceSet34, 4)
OPENSC4_THUNK(ServiceSet4088Thunk, ServiceSet4088, 4)
OPENSC4_THUNK(ServiceSet21Thunk, ServiceSet21, 4)
OPENSC4_THUNK(ServiceSetTempoThunk, ServiceSetTempo, 4)
OPENSC4_THUNK(ServiceSetRowThunk, ServiceSetRow, 4)
OPENSC4_THUNK(ServiceSetColumnThunk, ServiceSetColumn, 4)
OPENSC4_THUNK(ServiceRowStartThunk, ServiceRowStart, 0)
OPENSC4_THUNK(ServiceRowEndThunk, ServiceRowEnd, 0)
OPENSC4_THUNK(ServiceColumnStartThunk, ServiceColumnStart, 0)
OPENSC4_THUNK(ServiceColumnEndThunk, ServiceColumnEnd, 0)
OPENSC4_THUNK(ServiceInstanceThunk, ServiceInstance, 12)
OPENSC4_THUNK(ServiceReseedThunk, ServiceReseed, 4)
OPENSC4_THUNK(ServiceSet40BCThunk, ServiceSet40BC, 4)
OPENSC4_THUNK(ServiceSet40A4Thunk, ServiceSet40A4, 8)
OPENSC4_THUNK(ServiceSetGridRowsThunk, ServiceSetGridRows, 4)
OPENSC4_THUNK(ServiceSetGridColumnsThunk, ServiceSetGridColumns, 4)
OPENSC4_THUNK(ServiceSetGridThunk, ServiceSetGrid, 12)
OPENSC4_THUNK(ServiceSetGridCellThunk, ServiceSetGridCell, 20)
OPENSC4_THUNK(ServiceSetKeyThunk, ServiceSetKey, 4)
OPENSC4_THUNK(ServiceKeyThunk, ServiceKey, 0)

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
        {addr::cGZSoundService_Construct, &ServiceConstructThunk},
        {addr::cGZSoundService_QueryInterface, &ServiceQueryInterfaceThunk},
        {addr::cGZSoundService_Release, &ServiceReleaseThunk},
        {addr::cGZSoundService_Destruct, &ServiceDestructThunk},
        {addr::cGZSoundService_DeletingDestructor, &ServiceDeletingDestructorThunk},
        {addr::cGZSoundTable_DeletingDestructor, &TableDeletingDestructorThunk},
    };
    return InstallAll(detours);
}

bool InstallLoad(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZSoundService_Init, &ServiceInitThunk},
        {addr::cGZSoundTable_Read, &TableReadThunk},
        {addr::cGZSoundTable_Load, &TableLoadThunk},
    };
    return InstallAll(detours);
}

bool InstallPlay(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZSoundService_Prepare, &ServicePrepareThunk},
        {addr::cGZSoundService_Shutdown, &ServiceShutdownThunk},
        {addr::cGZSoundService_Play, &ServicePlayThunk},
        {addr::cGZSoundService_Stop, &ServiceStopThunk},
        {addr::cGZSoundService_SetLevel, &ServiceSetLevelThunk},
        {addr::cGZSoundService_Fade, &ServiceFadeThunk},
        {addr::cGZSoundService_FadeStep, &ServiceFadeStepThunk},
        {addr::cGZSoundService_SetTempo, &ServiceSetTempoThunk},
        {addr::cGZSoundService_BeatTime, &ServiceBeatTimeThunk},
        {addr::cGZSoundService_CountCells, &ServiceCountCellsThunk},
        {addr::cGZSoundService_Reseed, &ServiceReseedThunk},
    };
    return InstallAll(detours);
}

bool InstallWindow(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZSoundService_LoadByName, &ServiceLoadByNameThunk},
        {addr::cGZSoundService_Table, &ServiceTableThunk},
        {addr::cGZSoundService_SetRow, &ServiceSetRowThunk},
        {addr::cGZSoundService_SetColumn, &ServiceSetColumnThunk},
        {addr::cGZSoundService_Set34, &ServiceSet34Thunk},
        {addr::cGZSoundService_Set4088, &ServiceSet4088Thunk},
        {addr::cGZSoundService_Set21, &ServiceSet21Thunk},
        {addr::cGZSoundService_RowStart, &ServiceRowStartThunk},
        {addr::cGZSoundService_RowEnd, &ServiceRowEndThunk},
        {addr::cGZSoundService_ColumnStart, &ServiceColumnStartThunk},
        {addr::cGZSoundService_ColumnEnd, &ServiceColumnEndThunk},
        {addr::cGZSoundService_Instance, &ServiceInstanceThunk},
    };
    return InstallAll(detours);
}

bool InstallSettings(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZSoundService_Set40BC, &ServiceSet40BCThunk},
        {addr::cGZSoundService_Set40A4, &ServiceSet40A4Thunk},
        {addr::cGZSoundService_SetGridRows, &ServiceSetGridRowsThunk},
        {addr::cGZSoundService_SetGridColumns, &ServiceSetGridColumnsThunk},
        {addr::cGZSoundService_SetGrid, &ServiceSetGridThunk},
        {addr::cGZSoundService_SetGridCell, &ServiceSetGridCellThunk},
        {addr::cGZSoundService_SetKey, &ServiceSetKeyThunk},
        {addr::cGZSoundService_Key, &ServiceKeyThunk},
    };
    return InstallAll(detours);
}

const bool g_registered[] = {
    registry::Register({"framework.soundsvc.object", "framework/sound", registry::Kind::Detour,
                        registry::Phase::Attach, addr::cGZSoundService_Construct,
                        "Our framework sound service 0x3C1CF59E: constructor, QueryInterface, Release, destructors; "
                        "the sound table's deleting destructor (6 detours; part u6, docs/gzui.md)",
                        false, InstallObject}),
    registry::Register({"framework.soundsvc.load", "framework/sound", registry::Kind::Detour, registry::Phase::Attach,
                        addr::cGZSoundService_Init,
                        "Our framework sound service's Init and the sound table's file reader and text loader (3 "
                        "detours; part u6, docs/gzui.md)",
                        false, InstallLoad}),
    registry::Register({"framework.soundsvc.play", "framework/sound", registry::Kind::Detour, registry::Phase::Attach,
                        addr::cGZSoundService_Play,
                        "Our framework sound service's Prepare, Shutdown, Play, Stop, SetLevel, Fade and its step, "
                        "the tempo, the beat time, the cell count and Reseed (11 detours; part u6, docs/gzui.md)",
                        false, InstallPlay}),
    registry::Register({"framework.soundsvc.window", "framework/sound", registry::Kind::Detour,
                        registry::Phase::Attach, addr::cGZSoundService_SetRow,
                        "Our framework sound service's LoadByName, table getter, row and column, their windows, "
                        "setters and the instance getter (12 detours; part u6, docs/gzui.md)",
                        false, InstallWindow}),
    registry::Register({"framework.soundsvc.settings", "framework/sound", registry::Kind::Detour,
                        registry::Phase::Attach, addr::cGZSoundService_SetKey,
                        "Our framework sound service's slots 28 to 35: the neighbour grids and their size, the key "
                        "(8 detours; part u6, docs/gzui.md)",
                        false, InstallSettings}),
};

} // namespace
} // namespace opensc4::gzui
