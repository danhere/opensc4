// The GZ framework's sound service 0x3C1CF59E (docs/gzui.md, parts u6 and
// u7), on the game's own objects: the constructor, the interface, the
// destructors, the table loader and readers, its slots, the beats, and the
// sound instance's play. The audio layer makes one for each freshness score
// it loads (interface 0xDC1CF595). Replacements, each off by default:
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
//   framework.soundsvc.file      slots 38 to 40 (a score by path, by folder
//                                and file, from a table and a name), the
//                                table's reader by path, slot 46 (the name)
//   framework.soundsvc.control   slots 41 to 43, 47 to 51, the unlocked Stop
//                                (53) and slot 56
//   framework.soundsvc.count     slot 52 (in the window) and 59 to 64: the
//                                counts of sounding instances, the wanted
//                                and spare numbers
//   framework.soundsvc.beat      the message target's slot 3 (the beats), the
//                                neighbour pick, slots 54, 55, 57 and 58
//   framework.soundsvc.instance  the sound instance's slot 5 (play) and its
//                                listener's path
//
// Still the game's: the adjustors 0x00948C01 and 0x00948C09, the AddRefs
// 0x0094C47C and 0x009359BD, the getter 0x00948B85 (the description's vtable
// holds it too), the thunks 0x00948321 (the timer's callback, jumping to
// 0x0094761C) and 0x00948568 (slot 36, GZRefPtr::Assign on +0x4140), the
// shared slots 18 to 20, 44 and 45. Ours call each other directly where the
// game does; a virtual call stays one.
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

// The game's 0x00946B30 (`ret 0x14`; the path is a game string by value,
// which it destroys): the file at the path opened for reading (cRZFile,
// 0x80 bytes), read whole and closed, the text loaded. AL 0 when it does not
// open, is empty or the buffer is not had.
uint32_t __fastcall TableReadPath(char* self, void*, Text path)
{
    OPENSC4_FIRST("TableReadPath");
    Text text;
    MakeEmpty(text);
    alignas(4) char file[0x80];
    CallThis<void*>(addr::cRZFile_Construct, file, path.get());
    bool loaded = false;
    if (Yes(CallThis<uint32_t>(addr::cRZFile_Open, file, 1u, 2u, 1u))) {
        const uint32_t size = CallThis<uint32_t>(addr::cRZFile_Length, file);
        char* const buffer = size != 0 ? static_cast<char*>(New(size + 1)) : nullptr;
        if (buffer) {
            CallThis<uint32_t>(addr::cRZFile_Read, file, static_cast<void*>(buffer), size);
            CallThis<uint32_t>(addr::cRZFile_Close, file);
            buffer[size] = 0;
            CallThis<uint32_t>(addr::cRZBaseString_FromChar, text.get(), static_cast<const char*>(buffer));
            Delete(buffer);
            loaded = Yes(TableLoad(self, nullptr, text.get()));
        }
    }
    uint32_t eax = CallThis<uint32_t>(addr::cRZFile_Destruct, file);
    Destroy(text, eax);
    Destroy(path, eax);
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
// Part u7: slots 38 to 64, the beats, the sound instance's play

constexpr std::size_t kLast = 0x5C; // the cell slot 56 played last: row, column
constexpr std::size_t kPlayed = 0x64; // the cell the beats played last: row, column
constexpr std::size_t kPaused = 0x4090; // byte: no beats
constexpr std::size_t kPick = 0x40AC; // the neighbour grid's last pick: row, column

// The instance at row * 64 + column, and the table's description there.
char* InstanceAt(char* self, uint32_t index)
{
    return *reinterpret_cast<char**>(static_cast<std::uintptr_t>(Bits(self) + index * 4 + kInstances));
}

char* CellAt(char* table, uint32_t index)
{
    return *reinterpret_cast<char**>(static_cast<std::uintptr_t>(Bits(table) + index * 4 + kTableCells));
}

uint32_t& Word(std::uintptr_t address) { return *reinterpret_cast<uint32_t*>(address); }

uint32_t Advance(void* random) { return CallThis<uint32_t>(addr::cRZRandom_Advance, random); }

// The runtime's abs (0x009F58E2): the lowest int stays as it is.
uint32_t Abs(uint32_t value) { return static_cast<int32_t>(value) < 0 ? 0u - value : value; }

// The game's 0x00948CB4 (slot 38, `ret 4`): the path split by a file spec
// into its folder and its file name, slot 39 with both. AL its answer over
// the spec's destructor's EAX.
uint32_t __fastcall ServiceLoadPath(char* self, void*, const void* path)
{
    OPENSC4_FIRST("ServiceLoadPath");
    alignas(4) char spec[0x64];
    CallThis<void*>(addr::cRZFileSpec_Construct, spec, path);
    Text folder;
    CallThis<void*>(addr::SC4String_Substring, folder.get(), CallThis<void*>(addr::cRZFileSpec_Folder, spec), 0u,
                    0xFFFFFFFFu);
    Text file;
    {
        Text named;
        void* const made = CallThis<void*>(addr::cRZFileSpec_FileName, spec, named.get());
        CallThis<void*>(addr::SC4String_Substring, file.get(), made, 0u, 0xFFFFFFFFu);
        Destroy(named);
    }
    uint32_t eax = Virtual<uint32_t>(self, 0x9C, folder.get(), file.get());
    const bool loaded = Yes(eax);
    Destroy(file, eax);
    Destroy(folder, eax);
    eax = CallThis<uint32_t>(addr::SC4UI_F43AF80, spec);
    return Answer(eax, loaded);
}

// The game's 0x00948D5F (slot 39, `ret 8`): a new table at +0x1C (an old one
// is not freed), read from the folder and the file joined (0x00946B30, its
// answer not used), then slot 40 with it and the folder as the name. AL slot
// 40's answer.
uint32_t __fastcall ServiceLoadFile(char* self, void*, char* folder, char* file)
{
    OPENSC4_FIRST("ServiceLoadFile");
    char* table = static_cast<char*>(New(0x4048));
    if (table)
        table = CallThis<char*>(addr::cGZSoundTable_Construct, table);
    At<char*>(self, kTable) = table;
    alignas(4) char joined[0xC];
    CallCdecl<void*>(addr::cSC4Advice_F408AE0, static_cast<void*>(joined), folder ? folder + 4 : nullptr,
                     file ? file + 4 : nullptr);
    Text path;
    CallThis<void*>(addr::cSC4Advice_F408B90, path.get(), static_cast<void*>(joined));
    if (char* const body = At<char*>(joined, 0))
        Free(body);
    Text copy;
    CallThis<void*>(addr::SC4String_Substring, copy.get(), path.get(), 0u, 0xFFFFFFFFu);
    TableReadPath(At<char*>(self, kTable), nullptr, copy);
    uint32_t eax = Virtual<uint32_t>(self, 0xA0, At<void*>(self, kTable), static_cast<void*>(folder));
    const bool done = Yes(eax);
    if (char* const body = path.begin())
        eax = Free(body);
    return Answer(eax, done);
}

// The game's 0x00948E0B (slot 40, `ret 8`): under the lock, the table put at
// +0x1C (an old one is not freed), the name copied, then Prepare (slot 4).
uint32_t __fastcall ServiceUseTable(char* self, void*, void* table, const void* name)
{
    OPENSC4_FIRST("ServiceUseTable");
    Locked lock;
    At<void*>(self, kTable) = table;
    CallThis<void*>(addr::cRZBaseString_Assign, self + kName, name);
    const bool done = Yes(Virtual<uint32_t>(self, 0x10));
    return Answer(lock.Unlock(), done);
}

// The game's 0x00947150 (slot 41, `ret 4`): SetLevel (slot 11), then Play
// (slot 8), whose EAX it answers.
uint32_t __fastcall ServiceLevelAndPlay(char* self, void*, uint32_t level)
{
    OPENSC4_FIRST("ServiceLevelAndPlay");
    Virtual<uint32_t>(self, 0x2C, level);
    return Virtual<uint32_t>(self, 0x20);
}

// The game's 0x00947197 (slot 42): while fading, the timer stopped (its slot
// 7), the byte cleared under the lock itself, the timer stopped again and
// deleted. Answers the deletion's EAX; the caller's when not fading (ours 0).
uint32_t __fastcall ServiceStopFade(char* self, void*)
{
    OPENSC4_FIRST("ServiceStopFade");
    if (!At<uint8_t>(self, kFading))
        return 0;
    Virtual<uint32_t>(At<void*>(self, kTimer), 0x1C);
    void* const lock = reinterpret_cast<void*>(addr::cGZSoundService_Lock);
    CallThis<void>(addr::RZCriticalSection_Lock, lock);
    At<uint8_t>(self, kFading) = 0;
    CallThis<void>(addr::RZCriticalSection_Unlock, lock);
    uint32_t eax = Virtual<uint32_t>(At<void*>(self, kTimer), 0x1C);
    if (void* const timer = At<void*>(self, kTimer))
        eax = Virtual<uint32_t>(timer, 0x0, 1u);
    At<void*>(self, kTimer) = nullptr;
    return eax;
}

// The game's 0x0094758E (slot 43, `ret 4`): once prepared, the level at
// +0x14 and every instance's (its slot 9). AL 1 over the table; over the
// caller's EAX when not prepared (ours 1).
uint32_t __fastcall ServiceSetLevels(char* self, void*, uint32_t level)
{
    OPENSC4_FIRST("ServiceSetLevels");
    if (!At<uint8_t>(self, kStarted))
        return 1;
    At<uint32_t>(self, kLevel) = level;
    for (int32_t row = 0; row < At<int32_t>(At<char*>(self, kTable), kTableRows); ++row) {
        char** cell = reinterpret_cast<char**>(self + kInstances + row * 0x100);
        for (int32_t column = 0; column < At<int32_t>(At<char*>(self, kTable), kTableColumns); ++column, ++cell) {
            if (*cell)
                Virtual<uint32_t>(*cell, 0x24, At<uint32_t>(self, kLevel));
        }
    }
    return Answer(Bits(At<void*>(self, kTable)), true);
}

// The game's 0x00948E4E (slot 46, `ret 4`): the name copied into `out`.
uint32_t __fastcall ServiceName(char* self, void*, void* out)
{
    OPENSC4_FIRST("ServiceName");
    return Answer(CallThis<uint32_t>(addr::cRZBaseString_Assign, out, static_cast<void*>(self + kName)), true);
}

// The game's 0x009478EA (slot 47, `ret 8`): the row and column spreads; AL 1
// over the second.
uint32_t __fastcall ServiceSetSpreads(char* self, void*, uint32_t rows, uint32_t columns)
{
    OPENSC4_FIRST("ServiceSetSpreads");
    At<uint32_t>(self, kRowSpread) = rows;
    At<uint32_t>(self, kColumnSpread) = columns;
    return Answer(columns, true);
}

// The game's 0x009478FD (slot 48, `ret 8`): the spreads written out; AL 1
// over the column spread.
uint32_t __fastcall ServiceSpreads(char* self, void*, uint32_t* rows, uint32_t* columns)
{
    OPENSC4_FIRST("ServiceSpreads");
    *rows = At<uint32_t>(self, kRowSpread);
    const uint32_t spread = At<uint32_t>(self, kColumnSpread);
    *columns = spread;
    return Answer(spread, true);
}

// The game's 0x00948BC3 (slot 49, `ret 4`): +0x408C; answers the value.
uint32_t __fastcall ServiceSet408C(char* self, void*, uint32_t value)
{
    OPENSC4_FIRST("ServiceSet408C");
    At<uint32_t>(self, 0x408C) = value;
    return value;
}

// The game's 0x00948BD0 (slot 50, `ret 4`): the byte that pauses the beats;
// AL the byte over the caller's EAX (ours 0).
uint32_t __fastcall ServiceSetPaused(char* self, void*, uint32_t value)
{
    OPENSC4_FIRST("ServiceSetPaused");
    const uint8_t paused = static_cast<uint8_t>(value);
    At<uint8_t>(self, kPaused) = paused;
    return paused;
}

// The game's 0x00947914 (slot 51, `ret 8`): +0x6C and +0x70; answers the
// second.
uint32_t __fastcall ServiceSet6C(char* self, void*, uint32_t first, uint32_t second)
{
    OPENSC4_FIRST("ServiceSet6C");
    At<uint32_t>(self, 0x6C) = first;
    At<uint32_t>(self, 0x70) = second;
    return second;
}

// The game's 0x009471EE (slot 52, `ret 8`): whether a cell is within the
// spreads of the row and column. AL over the last distance (abs).
uint32_t __fastcall ServiceInWindow(char* self, void*, uint32_t row, uint32_t column)
{
    OPENSC4_FIRST("ServiceInWindow");
    const uint32_t rows = Abs(row - At<uint32_t>(self, kRow));
    if (static_cast<int32_t>(rows) > At<int32_t>(self, kRowSpread))
        return Answer(rows, false);
    const uint32_t columns = Abs(column - At<uint32_t>(self, kColumn));
    return Answer(columns, static_cast<int32_t>(columns) <= At<int32_t>(self, kColumnSpread));
}

// The game's 0x00947E90 (slot 53): once prepared and playing, the message
// target taken back from the framework's sound service (its slot 31), the
// fade stopped (slot 42), the stopwatch stopped and deleted, every active
// instance (slot 11) stopped (its slot 7), +0x20 cleared. AL 1 over the
// table; over the caller's EAX when not playing (ours 1).
uint32_t __fastcall ServiceStopNow(char* self, void*)
{
    OPENSC4_FIRST("ServiceStopNow");
    if (!At<uint8_t>(self, kStarted) || !At<uint8_t>(self, kPlaying))
        return 1;
    if (CallCdecl<void*>(addr::SC4_Singleton913C9E)) {
        void* const sound = CallCdecl<void*>(addr::SC4_Singleton913C9E);
        Virtual<uint32_t>(sound, 0x7C, static_cast<void*>(self + 4));
    }
    Virtual<uint32_t>(self, 0xA8);
    if (void* const watch = At<void*>(self, kWatch)) {
        CallThis<void>(addr::SC4Stopwatch_Stop, watch);
        if (void* const held = At<void*>(self, kWatch)) {
            CallThis<void>(addr::SC4Stopwatch_Destruct, held);
            Delete(held);
        }
        At<void*>(self, kWatch) = nullptr;
    }
    for (int32_t row = 0; row < At<int32_t>(At<char*>(self, kTable), kTableRows); ++row) {
        char** cell = reinterpret_cast<char**>(self + kInstances + row * 0x100);
        for (int32_t column = 0; column < At<int32_t>(At<char*>(self, kTable), kTableColumns); ++column, ++cell) {
            if (*cell && Yes(Virtual<uint32_t>(*cell, 0x2C)))
                Virtual<uint32_t>(*cell, 0x1C);
        }
    }
    At<uint8_t>(self, kPlaying) = 0;
    return Answer(Bits(At<void*>(self, kTable)), true);
}

// The game's 0x00947519 (slot 56, `ret 8`): the cell kept at +0x5C; its
// instance asked whether active (slot 11), given slot 10 when not, and
// stopped (slot 7) when it is then. Answers the last call's EAX.
uint32_t __fastcall ServiceCellAgain(char* self, void*, uint32_t row, uint32_t column)
{
    OPENSC4_FIRST("ServiceCellAgain");
    At<uint32_t>(self, kLast) = row;
    At<uint32_t>(self, kLast + 4) = column;
    char* const instance = InstanceAt(self, (row << 6) + column);
    uint32_t eax = Virtual<uint32_t>(instance, 0x2C);
    if (!Yes(eax))
        eax = Virtual<uint32_t>(instance, 0x28);
    eax = Virtual<uint32_t>(instance, 0x2C);
    if (Yes(eax))
        eax = Virtual<uint32_t>(instance, 0x1C);
    return eax;
}

// The game's 0x009477A6 (slot 59): the instances active (slot 11).
uint32_t __fastcall ServiceCountActive(char* self, void*)
{
    OPENSC4_FIRST("ServiceCountActive");
    uint32_t count = 0;
    for (int32_t row = 0; row < At<int32_t>(At<char*>(self, kTable), kTableRows); ++row) {
        char** cell = reinterpret_cast<char**>(self + kInstances + row * 0x100);
        for (int32_t column = 0; column < At<int32_t>(At<char*>(self, kTable), kTableColumns); ++column, ++cell) {
            if (*cell && Yes(Virtual<uint32_t>(*cell, 0x2C)))
                ++count;
        }
    }
    return count;
}

// The game's 0x0094780D (slot 60): the instances marked to go (their byte
// +0x2D), the table's size read once.
uint32_t __fastcall ServiceCountMarked(char* self, void*)
{
    OPENSC4_FIRST("ServiceCountMarked");
    uint32_t count = 0;
    char* const table = At<char*>(self, kTable);
    const int32_t rows = At<int32_t>(table, kTableRows);
    if (rows <= 0)
        return 0;
    const int32_t columns = At<int32_t>(table, kTableColumns);
    for (int32_t row = 0; row < rows; ++row) {
        char** cell = reinterpret_cast<char**>(self + kInstances + row * 0x100);
        for (int32_t column = 0; column < columns; ++column, ++cell) {
            if (*cell && At<uint8_t>(*cell, 0x2D))
                ++count;
        }
    }
    return count;
}

// The game's 0x00947858 (slot 61): the instances playing (slot 10).
uint32_t __fastcall ServiceCountPlaying(char* self, void*)
{
    OPENSC4_FIRST("ServiceCountPlaying");
    uint32_t count = 0;
    for (int32_t row = 0; row < At<int32_t>(At<char*>(self, kTable), kTableRows); ++row) {
        char** cell = reinterpret_cast<char**>(self + kInstances + row * 0x100);
        for (int32_t column = 0; column < At<int32_t>(At<char*>(self, kTable), kTableColumns); ++column, ++cell) {
            if (*cell && Yes(Virtual<uint32_t>(*cell, 0x28)))
                ++count;
        }
    }
    return count;
}

// The game's 0x009478BF (slot 62): once prepared and playing, slots 59 and
// 61 (AL 1 over 61's EAX). AL 1 over the caller's EAX when not prepared
// (ours 1), AL 0 when not playing (ours 0).
uint32_t __fastcall ServiceRecount(char* self, void*)
{
    OPENSC4_FIRST("ServiceRecount");
    if (!At<uint8_t>(self, kStarted))
        return 1;
    if (!At<uint8_t>(self, kPlaying))
        return 0;
    Virtual<uint32_t>(self, 0xEC);
    return Answer(Virtual<uint32_t>(self, 0xF4), true);
}

// The game's 0x00947925 (slot 63): how many may sound: +0x4088 percent of
// the table's +0x10, plus the column's share of its +0x18 and the row's of
// its +0x1C (a table of one column or row counts it as two), at most the
// groups in the window (+0x50).
int32_t __fastcall ServiceWanted(char* self, void*)
{
    OPENSC4_FIRST("ServiceWanted");
    char* const table = At<char*>(self, kTable);
    const int32_t base = static_cast<int32_t>(At<uint32_t>(self, 0x4088) * At<uint32_t>(table, 0x10)) / 100;
    int32_t columns = static_cast<int32_t>(At<uint32_t>(table, kTableColumns) - 1);
    if (columns == 0)
        columns = 1;
    const int32_t byColumn =
        static_cast<int32_t>((At<uint32_t>(self, kColumn) + 1) * At<uint32_t>(table, 0x18)) / columns;
    int32_t rows = static_cast<int32_t>(At<uint32_t>(table, kTableRows) - 1);
    if (rows == 0)
        rows = 1;
    const int32_t byRow = static_cast<int32_t>((At<uint32_t>(self, kRow) + 1) * At<uint32_t>(table, 0x1C)) / rows;
    int32_t total = static_cast<int32_t>(static_cast<uint32_t>(base) + static_cast<uint32_t>(byRow) +
                                         static_cast<uint32_t>(byColumn));
    if (total > At<int32_t>(self, kGroups))
        total = At<int32_t>(self, kGroups);
    return total;
}

// The game's 0x00947982 (slot 64): +0x4088 percent of the table's +0x14.
int32_t __fastcall ServiceSpare(char* self, void*)
{
    OPENSC4_FIRST("ServiceSpare");
    return static_cast<int32_t>(At<uint32_t>(At<char*>(self, kTable), 0x14) * At<uint32_t>(self, 0x4088)) / 100;
}

// The game's 0x00947AD9 (`ret 4`): a cell of the 3 by 3 byte grid (rows and
// columns +0x40B4 and +0x40B8, plus one), drawn at random among those set
// other than the last pick (+0x40AC; kept when the draw falls on it), given
// to the instance: slot 15 the byte +0x40BC, 17 +0x40A4 and +0x40A8, 16 the
// three dword grids' cells. AL 0 without a cell set; else AL 1 over slot
// 16's EAX.
uint32_t __fastcall ServicePlaceInstance(char* self, void*, void* instance)
{
    OPENSC4_FIRST("ServicePlaceInstance");
    uint32_t marked = 0;
    if (At<uint32_t>(self, kGridRows) + 1 == 0)
        return 0;
    const uint32_t columns = At<uint32_t>(self, kGridColumns) + 1;
    const uint8_t* cells = reinterpret_cast<const uint8_t*>(self + kGrid);
    for (uint32_t left = At<uint32_t>(self, kGridRows) + 1; left != 0; --left, cells += 3) {
        if (columns == 0)
            continue;
        const uint32_t limit = At<uint32_t>(self, kGridColumns) + 1;
        uint32_t column = 0;
        do {
            if (cells[column])
                ++marked;
            ++column;
        } while (column < limit);
    }
    if (marked == 0)
        return 0;
    uint32_t pickRow = At<uint32_t>(self, kPick);
    uint32_t pickColumn = At<uint32_t>(self, kPick + 4);
    uint32_t draw = CallThis<uint32_t>(addr::cRZRandom_Below, At<void*>(self, kRandom), marked * 1000);
    bool found = false;
    if (At<uint32_t>(self, kGridRows) + 1 != 0) {
        cells = reinterpret_cast<const uint8_t*>(self + kGrid);
        uint32_t row = 0;
        do {
            if (found)
                break;
            if (At<uint32_t>(self, kGridColumns) + 1 != 0) {
                uint32_t column = 0;
                do {
                    if (cells[column] && !(row == At<uint32_t>(self, kPick) && column == At<uint32_t>(self, kPick + 4))) {
                        if (draw < 1000) {
                            pickRow = row;
                            pickColumn = column;
                            found = true;
                            break;
                        }
                        draw -= 1000;
                    }
                    ++column;
                } while (column < At<uint32_t>(self, kGridColumns) + 1);
            }
            cells += 3;
            ++row;
        } while (row < At<uint32_t>(self, kGridRows) + 1);
    }
    At<uint32_t>(self, kPick) = pickRow;
    At<uint32_t>(self, kPick + 4) = pickColumn;
    Virtual<uint32_t>(instance, 0x3C, static_cast<uint32_t>(At<uint8_t>(self, 0x40BC)));
    Virtual<uint32_t>(instance, 0x44, At<uint32_t>(self, 0x40A4), At<uint32_t>(self, 0x40A8));
    const uint32_t at = Bits(self) + (pickColumn + pickRow * 3) * 4;
    const uint32_t a = *reinterpret_cast<uint32_t*>(static_cast<std::uintptr_t>(at + kGridA));
    const uint32_t b = *reinterpret_cast<uint32_t*>(static_cast<std::uintptr_t>(at + kGridB));
    const uint32_t c = *reinterpret_cast<uint32_t*>(static_cast<std::uintptr_t>(at + kGridC));
    return Answer(Virtual<uint32_t>(instance, 0x40, a, b, c), true);
}

// The game's 0x009482C5 (slot 57, `ret 8`): an active instance at the cell
// goes to slot 56; another one is placed (0x00947AD9) and, when it was,
// given the table's description (its slot 4) and played (slot 5). AL 1 over
// the last call's EAX.
uint32_t __fastcall ServicePlayCell(char* self, void*, uint32_t row, uint32_t column)
{
    OPENSC4_FIRST("ServicePlayCell");
    const uint32_t index = (row << 6) + column;
    uint32_t eax = Virtual<uint32_t>(InstanceAt(self, index), 0x2C);
    if (Yes(eax)) {
        eax = Virtual<uint32_t>(self, 0xE0, row, column);
    } else {
        char* const instance = InstanceAt(self, index);
        eax = ServicePlaceInstance(self, nullptr, instance);
        if (Yes(eax)) {
            Virtual<uint32_t>(instance, 0x10, static_cast<void*>(CellAt(At<char*>(self, kTable), index)));
            eax = Virtual<uint32_t>(instance, 0x14);
        }
    }
    return Answer(eax, true);
}

// The game's 0x00948573 (slot 58, `ret 0xC`): a cell's score, 0 when it
// cannot sound: no description, an instance busy (slots 12 and 11), outside
// the window (slot 52), the last cell of slot 56, an active unmarked
// instance of its group (the description's +0x5C) or of a cell its pairs
// name. The weight +0x40 is kept within 0 and 20 in the description: 0
// scores 0, 20 scores 0x64 (0x65 of `group`); else 2 of `group`, or 1 when
// rand() falls below the weight's chance (0x00B16F30) over the cells in the
// window (+0x38, 10 when 0).
uint32_t __fastcall ServiceScore(char* self, void*, uint32_t row, uint32_t column, int32_t group)
{
    OPENSC4_FIRST("ServiceScore");
    const uint32_t index = (row << 6) + column;
    char* const desc = CellAt(At<char*>(self, kTable), index);
    char* const instance = InstanceAt(self, index);
    if (!desc || Yes(Virtual<uint32_t>(instance, 0x30)) || Yes(Virtual<uint32_t>(instance, 0x2C)) ||
        !Yes(Virtual<uint32_t>(self, 0xD0, row, column)))
        return 0;
    if (row == At<uint32_t>(self, kLast) && column == At<uint32_t>(self, kLast + 4))
        return 0;
    if (At<int32_t>(desc, 0x5C) != 0) {
        for (int32_t r = 0; r < At<int32_t>(At<char*>(self, kTable), kTableRows); ++r) {
            char** cell = reinterpret_cast<char**>(self + kInstances + r * 0x100);
            for (int32_t c = 0; c < At<int32_t>(At<char*>(self, kTable), kTableColumns); ++c, ++cell) {
                char* const other = *cell;
                if (!other)
                    continue;
                char* const otherDesc = At<char*>(other, 0x24);
                if (!otherDesc || At<int32_t>(desc, 0x5C) != At<int32_t>(otherDesc, 0x5C))
                    continue;
                if (Yes(Virtual<uint32_t>(*cell, 0x2C)) && At<uint8_t>(*cell, 0x2D) == 0)
                    return 0;
            }
        }
    }
    for (char* node = At<char*>(At<char*>(desc, 0x64), 0); node != At<char*>(desc, 0x64); node = At<char*>(node, 0)) {
        const int32_t r = At<int32_t>(node, 0xC);
        const int32_t c = At<int32_t>(node, 0x8);
        char* const table = At<char*>(self, kTable);
        if (r >= At<int32_t>(table, kTableRows) || c >= At<int32_t>(table, kTableColumns))
            continue;
        char* const other = InstanceAt(self, (static_cast<uint32_t>(r) << 6) + static_cast<uint32_t>(c));
        if (Yes(Virtual<uint32_t>(other, 0x2C)) && At<uint8_t>(other, 0x2D) == 0)
            return 0;
    }
    if (At<int32_t>(desc, 0x40) > 20)
        At<int32_t>(desc, 0x40) = 20;
    if (At<int32_t>(desc, 0x40) < 0)
        At<int32_t>(desc, 0x40) = 0;
    const int32_t weight = At<int32_t>(desc, 0x40);
    if (weight == 0)
        return 0;
    const bool ofGroup = group != 0 && At<int32_t>(desc, 0x5C) == group;
    if (weight == 20)
        return ofGroup ? 0x65 : 0x64;
    if (At<int32_t>(self, kCells) == 0)
        At<int32_t>(self, kCells) = 10;
    if (ofGroup)
        return 2;
    const int32_t chance =
        reinterpret_cast<const int32_t*>(addr::cGZSoundService_Chances)[weight] / At<int32_t>(self, kCells);
    return CallCdecl<int32_t>(addr::CRT_rand) < chance ? 1 : 0;
}

// The game's 0x00948719 (slot 54, `ret 0xC`): the best scoring cell (slot
// 58) of the window (slots 22 to 25), from a random place in it (within the
// spreads of the row and column, kept in the table) to the window's end,
// then from its start; every cell asked slot 52 too, unused. The best is
// written out and its instance asked slot 11, unused. AL whether one
// scored, over the empty list's destruction (a list made for nothing).
uint32_t __fastcall ServiceFindBest(char* self, void*, int32_t* rowOut, int32_t* columnOut, uint32_t group)
{
    OPENSC4_FIRST("ServiceFindBest");
    int32_t best = 0;
    void* list = nullptr;
    uint8_t allocator = 0;
    CallThis<void*>(addr::SC4RefList_Construct, static_cast<void*>(&list), static_cast<void*>(&allocator));
    const uint32_t draw = Advance(At<void*>(self, kRandom));
    const int32_t rowStart = Virtual<int32_t>(self, 0x58);
    const int32_t columnStart = Virtual<int32_t>(self, 0x60);
    const int32_t rowEnd = Virtual<int32_t>(self, 0x5C);
    const int32_t columnEnd = Virtual<int32_t>(self, 0x64);
    const uint32_t rowSpread = At<uint32_t>(self, kRowSpread);
    int32_t startRow = static_cast<int32_t>(static_cast<uint32_t>(static_cast<int32_t>((draw >> 8) & 0xFF) %
                                                                  static_cast<int32_t>(rowSpread * 2 + 1)) +
                                            At<uint32_t>(self, kRow) - rowSpread);
    const uint32_t columnSpread = At<uint32_t>(self, kColumnSpread);
    int32_t startColumn = static_cast<int32_t>(static_cast<uint32_t>(static_cast<int32_t>(draw & 0xFF) %
                                                                     static_cast<int32_t>(columnSpread * 2 + 1)) +
                                               At<uint32_t>(self, kColumn) - columnSpread);
    if (startRow < 0)
        startRow = 0;
    if (startColumn < 0)
        startColumn = 0;
    char* const table = At<char*>(self, kTable);
    if (startRow >= At<int32_t>(table, kTableRows))
        startRow = At<int32_t>(table, kTableRows) - 1;
    if (startColumn >= At<int32_t>(table, kTableColumns))
        startColumn = At<int32_t>(table, kTableColumns) - 1;

    auto visit = [&](int32_t row, int32_t column) {
        Virtual<uint32_t>(self, 0xD0, row, column);
        const int32_t score = Virtual<int32_t>(self, 0xE8, row, column, group);
        if (score > best) {
            *rowOut = row;
            best = score;
            *columnOut = column;
            Virtual<uint32_t>(InstanceAt(self, (static_cast<uint32_t>(row) << 6) + static_cast<uint32_t>(column)),
                              0x2C);
        }
    };
    auto rowOf = [&](int32_t row) {
        for (int32_t column = startColumn; column <= columnEnd; ++column)
            visit(row, column);
        for (int32_t column = columnStart; column < startColumn; ++column)
            visit(row, column);
    };
    for (int32_t row = startRow; row <= rowEnd; ++row)
        rowOf(row);
    for (int32_t row = rowStart; row < startRow; ++row)
        rowOf(row);

    // The game walks the (empty) list to its end here.
    if (group == 3 && best < 2) {
        char* const head = static_cast<char*>(list);
        for (char* node = At<char*>(head, 0); node != head; node = At<char*>(node, 0)) {
        }
    }
    return Answer(CallThis<uint32_t>(addr::SC4PairList_Destroy, static_cast<void*>(&list)), best != 0);
}

// The game's 0x00947220 (slot 55, `ret 8`): an instance to let go. First
// one playing (slot 10) outside the window (slot 52) and not marked, from a
// random cell to the table's end and then from its start (AL 1 over the
// instance); else, among the window's cells (kept in the table), one
// playing, not marked and not of weight 20, at random (AL 1 over its
// column). Two buffers are allocated for the window and never freed. AL 0
// over the window's first row when it is empty, over its last when none
// is found.
uint32_t __fastcall ServicePickPlaying(char* self, void*, int32_t* row, int32_t* column)
{
    OPENSC4_FIRST("ServicePickPlaying");
    const uint32_t draw = Advance(At<void*>(self, kRandom));
    char* const table = At<char*>(self, kTable);
    const int32_t startRow = static_cast<int32_t>((draw >> 8) & 0xFF) % At<int32_t>(table, kTableRows);
    const int32_t startColumn = static_cast<int32_t>(draw & 0xFF) % At<int32_t>(table, kTableColumns);
    auto rows = [&] { return At<int32_t>(At<char*>(self, kTable), kTableRows); };
    auto columns = [&] { return At<int32_t>(At<char*>(self, kTable), kTableColumns); };
    auto at = [&] { return (static_cast<uint32_t>(*row) << 6) + static_cast<uint32_t>(*column); };
    auto stray = [&]() -> char* {
        if (!Yes(Virtual<uint32_t>(InstanceAt(self, at()), 0x28)) || Yes(Virtual<uint32_t>(self, 0xD0, *row, *column)))
            return nullptr;
        char* const instance = InstanceAt(self, at());
        return At<uint8_t>(instance, 0x2D) == 0 ? instance : nullptr;
    };
    auto rowOf = [&]() -> char* {
        for (*column = startColumn; *column < columns(); ++*column) {
            if (char* const found = stray())
                return found;
        }
        *column = 0;
        if (startColumn > 0) {
            for (; *column < startColumn; ++*column) {
                if (char* const found = stray())
                    return found;
            }
        }
        return nullptr;
    };
    *row = startRow;
    if (startRow < rows()) {
        do {
            if (char* const found = rowOf())
                return Answer(Bits(found), true);
            ++*row;
        } while (*row < rows());
    }
    *row = 0;
    if (startRow > 0) {
        do {
            if (char* const found = rowOf())
                return Answer(Bits(found), true);
            ++*row;
        } while (*row < startRow);
    }

    int32_t firstRow = static_cast<int32_t>(At<uint32_t>(self, kRow) - At<uint32_t>(self, kRowSpread));
    int32_t firstColumn = static_cast<int32_t>(At<uint32_t>(self, kColumn) - At<uint32_t>(self, kColumnSpread));
    if (firstRow < 0)
        firstRow = 0;
    if (firstColumn < 0)
        firstColumn = 0;
    int32_t lastRow = static_cast<int32_t>(At<uint32_t>(self, kRow) + At<uint32_t>(self, kRowSpread));
    int32_t lastColumn = static_cast<int32_t>(At<uint32_t>(self, kColumn) + At<uint32_t>(self, kColumnSpread));
    if (lastRow >= rows())
        lastRow = rows() - 1;
    if (lastColumn >= columns())
        lastColumn = columns() - 1;
    const uint32_t size = (static_cast<uint32_t>(lastColumn - firstColumn) + 1) *
                          (static_cast<uint32_t>(lastRow - firstRow) + 1) * 4;
    int32_t* const rowsFound = static_cast<int32_t*>(New(size));
    int32_t* const columnsFound = static_cast<int32_t*>(New(size));
    uint32_t count = 0;
    *row = firstRow;
    if (firstRow > lastRow)
        return Answer(static_cast<uint32_t>(firstRow), false);
    do {
        *column = firstColumn;
        if (firstColumn <= lastColumn) {
            do {
                if (Yes(Virtual<uint32_t>(InstanceAt(self, at()), 0x28))) {
                    char* const instance = InstanceAt(self, at());
                    if (At<int32_t>(At<char*>(instance, 0x24), 0x40) != 20 && At<uint8_t>(instance, 0x2D) == 0) {
                        rowsFound[count] = *row;
                        columnsFound[count] = *column;
                        ++count;
                    }
                }
                ++*column;
            } while (*column <= lastColumn);
        }
        ++*row;
    } while (*row <= lastRow);
    if (count == 0)
        return Answer(static_cast<uint32_t>(lastRow), false);
    const uint32_t pick = Advance(At<void*>(self, kRandom)) % count;
    *row = rowsFound[pick];
    *column = columnsFound[pick];
    return Answer(static_cast<uint32_t>(columnsFound[pick]), true);
}

// The game's 0x00947F58 (the message target's slot 3, `ret 4`; the argument
// is not read; `this` is the part at +4): the beats. Slots 63 and 64 asked
// first (whose EAX it answers when not prepared, not playing or paused).
// Under the lock, with call counts in 0x00B634A8 to 0x00B634CC (cleared
// every 10 s of the stopwatch): every beat the stopwatch has passed (the
// beat time, 0x009475FB) is run, unless a day has: then the beats start
// over at the table's first and the stopwatch is restarted. On a beat one
// or six before the bar's end (the table's +0x34), with enough sounding
// (slot 61 at least 63's), one lets go (slot 55: marked, its group and
// weight kept); with fewer active unmarked (slot 59 less 60) than 64's, a cell is
// found (slot 54, tried again for a group of weight 19, and once more with
// fewer than 63's), placed (0x00947AD9), given its description, the
// reference at +0x4140 (its slot 18) and played. Then every instance's
// Update (slot 6), the bar's beat (+0x3C, back to 1 past the table's +0x34)
// and the beat (+0x40) stepped. Answers the unlock's EAX.
uint32_t __fastcall ServiceBeat(char* part, void*, uint32_t)
{
    OPENSC4_FIRST("ServiceBeat");
    char* const self = part - 4;
    const int32_t wanted = Virtual<int32_t>(self, 0xFC);
    const uint32_t eax = Virtual<uint32_t>(self, 0x100);
    const int32_t spare = static_cast<int32_t>(eax);
    int32_t group = 0;
    if (!At<uint8_t>(self, kStarted) || !At<uint8_t>(self, kPlaying) || At<uint8_t>(self, kPaused))
        return eax;
    Locked lock;
    const std::uintptr_t stats = addr::cGZSoundService_BeatStats;
    uint32_t& now = Word(stats);          // 0x00B634B4
    uint32_t& cleared = Word(stats + 4);  // 0x00B634B8
    uint32_t& tries = Word(stats + 8);    // 0x00B634BC
    uint32_t& beats = Word(stats + 0xC);  // 0x00B634C0
    uint32_t& since = Word(stats + 0x10); // 0x00B634C4
    uint32_t& once = Word(stats + 0x14);  // 0x00B634C8
    uint32_t& calls = Word(stats + 0x18); // 0x00B634CC
    uint32_t& plays = Word(addr::cGZSoundService_BeatPlays);
    auto elapsed = [&] { return CallThis<uint32_t>(addr::GZStopwatch_ElapsedMilliseconds, At<void*>(self, kWatch)); };
    ++calls;
    if (!(once & 1)) {
        once |= 1;
        since = elapsed();
    }
    if (static_cast<int32_t>(elapsed() - since) > 10000) {
        calls = 0;
        beats = 0;
        tries = 0;
        plays = 0;
        cleared = 0;
        since = elapsed();
    }
    const uint32_t time = elapsed();
    now = time;
    int32_t row = 0, column = 0;
    for (uint32_t due = ServiceBeatTime(self, nullptr, At<uint32_t>(self, kBeat)); time >= due;
         due = ServiceBeatTime(self, nullptr, At<uint32_t>(self, kBeat))) {
        const int32_t first = At<int32_t>(At<char*>(self, kTable), 0x34);
        if (time > 86400000u) {
            At<int32_t>(self, kFirstBeat) = first;
            At<int32_t>(self, 0x3C) = first;
            At<int32_t>(self, kBeat) = first;
            CallThis<void>(addr::GZStopwatch_Start, At<void*>(self, kWatch));
            elapsed();
            break;
        }
        ++beats;
        int32_t weight = 0;
        const int32_t bar = At<int32_t>(self, 0x3C);
        if (bar == first - 1 || bar == first - 6) {
            bool retire = Virtual<int32_t>(self, 0xF4) >= wanted;
            if (At<uint8_t>(self, 0x21))
                retire = false;
            if (At<uint32_t>(At<char*>(self, kTable), 0xC) == 0)
                retire = false;
            if (retire && Yes(Virtual<uint32_t>(self, 0xDC, &row, &column))) {
                const uint32_t index = (static_cast<uint32_t>(row) << 6) + static_cast<uint32_t>(column);
                At<uint8_t>(InstanceAt(self, index), 0x2D) = 1;
                char* const desc = At<char*>(InstanceAt(self, index), 0x24);
                group = At<int32_t>(desc, 0x5C);
                if (group != 0)
                    weight = At<int32_t>(desc, 0x40);
            }
        }
        const int32_t marked = Virtual<int32_t>(self, 0xF0);
        const int32_t open = static_cast<int32_t>(Virtual<uint32_t>(self, 0xEC) - static_cast<uint32_t>(marked));
        const int32_t bar2 = At<int32_t>(self, 0x3C);
        const int32_t first2 = At<int32_t>(At<char*>(self, kTable), 0x34);
        if (bar2 == first2 - 1 || bar2 == first2 - 6) {
            bool go = At<uint32_t>(self, 0x4088) != 0;
            if (open >= spare)
                go = false;
            if (At<uint8_t>(self, 0x21))
                go = false;
            if (At<uint8_t>(self, kFading))
                go = false;
            if (go && open < spare) {
                bool found = Yes(Virtual<uint32_t>(self, 0xD8, &row, &column, group));
                ++tries;
                if (!found && group != 0 && weight == 0x13)
                    found = Yes(Virtual<uint32_t>(self, 0xD8, &row, &column, group));
                if (!found && group != 0 && weight == 0x13)
                    found = Yes(Virtual<uint32_t>(self, 0xD8, &row, &column, group));
                if (!found && open < wanted) {
                    found = Yes(Virtual<uint32_t>(self, 0xD8, &row, &column, group));
                    ++tries;
                }
                if (found) {
                    const uint32_t index = (static_cast<uint32_t>(row) << 6) + static_cast<uint32_t>(column);
                    char* const instance = InstanceAt(self, index);
                    if (Yes(ServicePlaceInstance(self, nullptr, instance))) {
                        ++plays;
                        Virtual<uint32_t>(instance, 0x10, static_cast<void*>(CellAt(At<char*>(self, kTable), index)));
                        Virtual<uint32_t>(instance, 0x48, At<uint32_t>(self, kHeld));
                        Virtual<uint32_t>(instance, 0x14);
                        At<int32_t>(self, kPlayed) = row;
                        At<int32_t>(self, kPlayed + 4) = column;
                    }
                }
            }
        }
        for (row = 0; row < At<int32_t>(At<char*>(self, kTable), kTableRows); ++row) {
            for (column = 0; column < At<int32_t>(At<char*>(self, kTable), kTableColumns); ++column) {
                char* const instance =
                    InstanceAt(self, (static_cast<uint32_t>(row) << 6) + static_cast<uint32_t>(column));
                if (instance)
                    Virtual<uint32_t>(instance, 0x18);
            }
        }
        ++At<int32_t>(self, 0x3C);
        if (At<int32_t>(self, 0x3C) > At<int32_t>(At<char*>(self, kTable), 0x34))
            At<int32_t>(self, 0x3C) = 1;
        ++At<int32_t>(self, kBeat);
    }
    return lock.Unlock();
}

// ====================================================================================
// The sound instance 0xBDD9D7A5's slot 5 (part u5 has the rest)

constexpr std::size_t kOwner = 0x18;    // the sound service
constexpr std::size_t kBuffer = 0x1C;   // the sound buffer (interface 0x206077D2)
constexpr std::size_t kState = 0x20;    // 2 playing
constexpr std::size_t kDesc = 0x24;     // the description
constexpr std::size_t kFactor = 0x30;   // the level factor (0x400 full)
constexpr std::size_t kListener = 0x54;

constexpr uint32_t kIID_Buffer = 0x206077D2;
constexpr uint32_t kIID_BufferControl = 0xFD584467;
constexpr uint32_t kIID_BufferPlace = 0x306077D2;
constexpr uint32_t kIID_BufferHandle = 0x5B34F0AF;

// A count as a float, as the x87 makes it from an unsigned dword.
float Unsigned(uint32_t value) { return static_cast<float>(static_cast<double>(value)); }
float Signed(uint32_t value) { return static_cast<float>(static_cast<double>(static_cast<int32_t>(value))); }

// The truncating float-to-int conversion (0x009EEF04): the low dword.
int32_t Ftol(double value) { return static_cast<int32_t>(static_cast<int64_t>(value)); }

// The semitone ratios 2^(n/12) at 0x00B16EB0 (13 doubles), by an index the
// game does not check: at index * 8 for one not negative, at -index * 8
// below.
double Semitone(int32_t index)
{
    const uint32_t base = static_cast<uint32_t>(addr::cGZSoundInstance_Semitones);
    const uint32_t at = index < 0 ? base - static_cast<uint32_t>(index) * 8 : base + static_cast<uint32_t>(index) * 8;
    return *reinterpret_cast<const double*>(static_cast<std::uintptr_t>(at));
}

// The description's random level: the factor less a draw of the service's
// generator (abs) modulo a quarter of it.
void RandomFactor(char* self)
{
    const int32_t factor = At<int32_t>(self, kFactor);
    const uint32_t draw = Abs(Advance(At<void*>(At<char*>(self, kOwner), kRandom)));
    At<int32_t>(self, kFactor) = factor - static_cast<int32_t>(draw) % (factor / 4);
}

// The description's pitch range (+0x54 to +0x58, both 0 for none): a
// semitone index drawn from the service's generator, or nothing.
bool PitchIndex(char* self, int32_t& index)
{
    char* const desc = At<char*>(self, kDesc);
    const int32_t low = At<int32_t>(desc, 0x54);
    if ((low == 0 && At<int32_t>(desc, 0x58) == 0) || low > At<int32_t>(desc, 0x58))
        return false;
    const uint32_t draw = Advance(At<void*>(At<char*>(self, kOwner), kRandom));
    char* const again = At<char*>(self, kDesc);
    const int32_t from = At<int32_t>(again, 0x54);
    const int32_t span = static_cast<int32_t>(At<uint32_t>(again, 0x58) - static_cast<uint32_t>(from) + 1);
    index = static_cast<int32_t>(static_cast<uint32_t>(static_cast<int32_t>(draw) % span) + static_cast<uint32_t>(from));
    return true;
}

// The id in a name holding 8 hex digits (u5's test): the text before the
// first ".", its last 8 characters as a number (SC4::TextToInt). The name
// keeps the text before the ".".
int32_t HexId(Text& name)
{
    SplitInto(name, name.get(), ".");
    Text right;
    void* const made = CallThis<void*>(addr::cRZBaseString_Right, name.get(), right.get(), 8u);
    const int32_t id = CallCdecl<int32_t>(addr::SC4_TextToInt, Virtual<const char*>(made, 0x14));
    Destroy(right);
    return id;
}

// The game's 0x0094680F (`ret 4`; the description is the argument, the
// level's and pitch's the instance's own): the play handed to the listener
// at +0x54. Counted at 0x00B63498; playing (2) at full factor (a random
// level takes off a draw), the level worked out (u5's 0x00945820), the loop
// count from a looping description, a pitch from its range (an index below
// 0 reads the table at its opposite, no division). The sound's id: a hex
// name's number, else the name's id (SC4::NameToId of the file spec's name)
// with 0xFF000000. The listener's slot 3 with the name's text, the id, the
// description, the byte +0x3D, the looping byte and the description's +0x34
// as a float; AL 0 when it refuses. Else slot 5 the place (+0x40 to +0x48),
// slot 8 the distances (+0x4C, +0x50, unsigned), slot 6 the level over
// 1024, slot 7 the pitch; AL 1.
uint32_t __fastcall InstancePlayThrough(char* self, void*, char* desc)
{
    OPENSC4_FIRST("InstancePlayThrough");
    ++Word(addr::cGZSoundInstance_Plays);
    At<uint32_t>(self, kState) = 2;
    At<uint32_t>(self, kFactor) = 0x400;
    if (At<uint8_t>(At<char*>(self, kDesc), 0x30))
        RandomFactor(self);
    CallThis<int32_t>(addr::cGZSoundInstance_ComputeLevel, self);
    char* const own = At<char*>(self, kDesc);
    if (At<uint8_t>(own, 0x50))
        At<uint32_t>(self, 0x28) = At<uint32_t>(own, 0x48);
    double pitch = 1.0;
    int32_t index = 0;
    if (PitchIndex(self, index))
        pitch = Semitone(index);

    Text name;
    CallThis<void*>(addr::SC4String_Substring, name.get(), static_cast<void*>(desc + 0xC), 0u, 0xFFFFFFFFu);
    uint32_t id = 0;
    if (Yes(CallThis<uint32_t>(addr::cGZSoundInstance_HasHexRun, self, name.get())))
        id = static_cast<uint32_t>(HexId(name));
    if (id == 0) {
        alignas(4) char spec[0x64];
        CallThis<void*>(addr::cRZFileSpec_Construct, spec, static_cast<void*>(desc + 0xC));
        char* const part = CallThis<char*>(addr::cRZFileSpec_BuiltName, spec);
        id = CallCdecl<uint32_t>(addr::SC4_NameToId, At<const char*>(part, 4), 0u) | 0xFF000000u;
        CallThis<void>(addr::SC4UI_F43AF80, spec);
    }
    const float time = Signed(At<uint32_t>(desc, 0x34));
    const uint32_t looping = At<uint8_t>(desc, 0x50);
    const uint32_t third = At<uint8_t>(self, 0x3D);
    const char* const text = Virtual<const char*>(desc + 0xC, 0x14);
    uint32_t eax = Virtual<uint32_t>(At<void*>(self, kListener), 0xC, text, id, static_cast<void*>(desc), third,
                                     looping, time);
    if (!Yes(eax)) {
        Destroy(name, eax);
        return Answer(eax, false);
    }
    Virtual<uint32_t>(At<void*>(self, kListener), 0x14, static_cast<void*>(desc), Signed(At<uint32_t>(self, 0x40)),
                      Signed(At<uint32_t>(self, 0x44)), Signed(At<uint32_t>(self, 0x48)));
    Virtual<uint32_t>(At<void*>(self, kListener), 0x20, static_cast<void*>(desc), Unsigned(At<uint32_t>(self, 0x4C)),
                      Unsigned(At<uint32_t>(self, 0x50)));
    const float level =
        static_cast<float>(static_cast<double>(At<int32_t>(self, 0x38)) * static_cast<double>(1.0f / 1024));
    Virtual<uint32_t>(At<void*>(self, kListener), 0x18, static_cast<void*>(desc), level);
    eax = Virtual<uint32_t>(At<void*>(self, kListener), 0x1C, static_cast<void*>(desc), static_cast<float>(pitch));
    Destroy(name, eax);
    return Answer(eax, true);
}

// The game's 0x00946C2C (the sound instance's slot 5, plain ret): play.
// AL 0 without the framework's sound service, while busy (slots 12 and 11).
// The byte +0x2D cleared; with a listener, 0x0094680F. Else a buffer
// (interface 0x206077D2) asked of the audio system (SC4::Singleton913CCA)
// into +0x1C: by id (slot 18; a hex name's number, kept in the
// description's +8) or by the file spec's name (slot 17); AL 0 without one.
// Counted at 0x00B63498. The buffer's interface 0xFD584467 told 0x1C, 1, 0
// (its slot 3); its interface 0x306077D2 (not checked) the byte +0x3D (slot
// 15), the distances (slots 12 and 14) and, with +0x3C, the place (slot
// 5). Played (slot 6) with the level, the looping byte and 1, or with a
// description time, 0 and then its slot 22 (0, the level, the time, 1);
// playing (2); its interface 0x5B34F0AF handed to the framework's sound
// service (slot 30, 250). The factor and the level as in 0x0094680F, the
// buffer's level (slot 21), the loop count; a pitch: the buffer's rate (slot
// 26) times the semitone (divided by it below 0), truncated, to slot 27. The
// three interfaces released; AL 1.
uint32_t __fastcall InstancePlay(char* self, void*)
{
    OPENSC4_FIRST("InstancePlay");
    uint32_t eax = Bits(CallCdecl<void*>(addr::SC4_Singleton913C9E));
    if (!eax)
        return 0;
    eax = Virtual<uint32_t>(self, 0x30);
    if (Yes(eax))
        return Answer(eax, false);
    eax = Virtual<uint32_t>(self, 0x2C);
    if (Yes(eax))
        return Answer(eax, false);
    At<uint8_t>(self, 0x2D) = 0;
    if (At<void*>(self, kListener))
        return InstancePlayThrough(self, nullptr, At<char*>(self, kDesc));

    void** const buffer = reinterpret_cast<void**>(self + kBuffer);
    Text name;
    CallThis<void*>(addr::SC4String_Substring, name.get(), static_cast<void*>(At<char*>(self, kDesc) + 0xC), 0u,
                    0xFFFFFFFFu);
    if (Yes(CallThis<uint32_t>(addr::cGZSoundInstance_HasHexRun, self, name.get()))) {
        const int32_t id = HexId(name);
        At<int32_t>(At<char*>(self, kDesc), 0x8) = id;
        void* const audio = CallCdecl<void*>(addr::SC4_Singleton913CCA);
        eax = Virtual<uint32_t>(audio, 0x48, At<uint32_t>(At<char*>(self, kDesc), 0x8), 0u, 0u, kIID_Buffer,
                                static_cast<void*>(buffer));
        if (!Yes(eax)) {
            Destroy(name, eax);
            return Answer(eax, false);
        }
    } else {
        alignas(4) char spec[0x64];
        CallThis<void*>(addr::cRZFileSpec_Construct, spec, static_cast<void*>(At<char*>(self, kDesc) + 0xC));
        Text file;
        CallThis<void*>(addr::SC4String_Substring, file.get(), CallThis<void*>(addr::cRZFileSpec_NameField, spec), 0u,
                        0xFFFFFFFFu);
        void* const audio = CallCdecl<void*>(addr::SC4_Singleton913CCA);
        eax = Virtual<uint32_t>(audio, 0x44, file.get(), 0u, 0u, kIID_Buffer, static_cast<void*>(buffer));
        const bool made = Yes(eax);
        Destroy(file, eax);
        eax = CallThis<uint32_t>(addr::SC4UI_F43AF80, spec);
        if (!made) {
            Destroy(name, eax);
            return Answer(eax, false);
        }
    }
    if (!*buffer) {
        Destroy(name, eax);
        return Answer(eax, false);
    }

    ++Word(addr::cGZSoundInstance_Plays);
    void* control = nullptr;
    if (Yes(Virtual<uint32_t>(*buffer, 0x0, kIID_BufferControl, static_cast<void*>(&control))))
        Virtual<uint32_t>(control, 0xC, 0x1Cu, 1u, 0u);
    void* place = nullptr;
    Virtual<uint32_t>(*buffer, 0x0, kIID_BufferPlace, static_cast<void*>(&place));
    Virtual<uint32_t>(place, 0x3C, static_cast<uint32_t>(At<uint8_t>(self, 0x3D)));
    Virtual<uint32_t>(place, 0x30, Unsigned(At<uint32_t>(self, 0x4C)));
    Virtual<uint32_t>(place, 0x38, Unsigned(At<uint32_t>(self, 0x50)));
    if (At<uint8_t>(self, 0x3C))
        Virtual<uint32_t>(place, 0x14, Signed(At<uint32_t>(self, 0x40)), Signed(At<uint32_t>(self, 0x44)),
                          Signed(At<uint32_t>(self, 0x48)));
    char* const desc = At<char*>(self, kDesc);
    const uint32_t looping = At<uint8_t>(desc, 0x50);
    if (At<int32_t>(desc, 0x34) != 0) {
        Virtual<uint32_t>(*buffer, 0x18, 0u, looping, 1u);
        Virtual<uint32_t>(*buffer, 0x58, 0u, At<uint32_t>(self, 0x38), At<uint32_t>(At<char*>(self, kDesc), 0x38), 1u);
    } else {
        Virtual<uint32_t>(*buffer, 0x18, At<uint32_t>(self, 0x38), looping, 1u);
    }
    At<uint32_t>(self, kState) = 2;
    void* handle = nullptr;
    if (Yes(Virtual<uint32_t>(*buffer, 0x0, kIID_BufferHandle, static_cast<void*>(&handle)))) {
        void* const sound = CallCdecl<void*>(addr::SC4_Singleton913C9E);
        Virtual<uint32_t>(sound, 0x78, handle, 0xFAu);
    }
    At<uint32_t>(self, kFactor) = 0x400;
    if (At<uint8_t>(At<char*>(self, kDesc), 0x30))
        RandomFactor(self);
    CallThis<int32_t>(addr::cGZSoundInstance_ComputeLevel, self);
    Virtual<uint32_t>(*buffer, 0x54, At<uint32_t>(self, 0x38));
    char* const own = At<char*>(self, kDesc);
    if (At<uint8_t>(own, 0x50))
        At<uint32_t>(self, 0x28) = At<uint32_t>(own, 0x48);
    int32_t index = 0;
    if (PitchIndex(self, index)) {
        const double rate = static_cast<double>(Virtual<int32_t>(*buffer, 0x68));
        const double pitched = index >= 0 ? rate * Semitone(index) : rate / Semitone(index);
        Virtual<uint32_t>(*buffer, 0x6C, Ftol(pitched));
    }
    if (handle)
        eax = Release(handle);
    if (place)
        eax = Release(place);
    if (control)
        eax = Release(control);
    Destroy(name, eax);
    return Answer(eax, true);
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

OPENSC4_THUNK(ServiceLoadPathThunk, ServiceLoadPath, 4)
OPENSC4_THUNK(ServiceLoadFileThunk, ServiceLoadFile, 8)
OPENSC4_THUNK(ServiceUseTableThunk, ServiceUseTable, 8)
OPENSC4_THUNK(TableReadPathThunk, TableReadPath, 20)
OPENSC4_THUNK(ServiceNameThunk, ServiceName, 4)
OPENSC4_THUNK(ServiceLevelAndPlayThunk, ServiceLevelAndPlay, 4)
OPENSC4_THUNK(ServiceStopFadeThunk, ServiceStopFade, 0)
OPENSC4_THUNK(ServiceSetLevelsThunk, ServiceSetLevels, 4)
OPENSC4_THUNK(ServiceSetSpreadsThunk, ServiceSetSpreads, 8)
OPENSC4_THUNK(ServiceSpreadsThunk, ServiceSpreads, 8)
OPENSC4_THUNK(ServiceSet408CThunk, ServiceSet408C, 4)
OPENSC4_THUNK(ServiceSetPausedThunk, ServiceSetPaused, 4)
OPENSC4_THUNK(ServiceSet6CThunk, ServiceSet6C, 8)
OPENSC4_THUNK(ServiceStopNowThunk, ServiceStopNow, 0)
OPENSC4_THUNK(ServiceCellAgainThunk, ServiceCellAgain, 8)
OPENSC4_THUNK(ServiceInWindowThunk, ServiceInWindow, 8)
OPENSC4_THUNK(ServiceCountActiveThunk, ServiceCountActive, 0)
OPENSC4_THUNK(ServiceCountMarkedThunk, ServiceCountMarked, 0)
OPENSC4_THUNK(ServiceCountPlayingThunk, ServiceCountPlaying, 0)
OPENSC4_THUNK(ServiceRecountThunk, ServiceRecount, 0)
OPENSC4_THUNK(ServiceWantedThunk, ServiceWanted, 0)
OPENSC4_THUNK(ServiceSpareThunk, ServiceSpare, 0)
OPENSC4_THUNK(ServiceBeatThunk, ServiceBeat, 4)
OPENSC4_THUNK(ServicePlaceInstanceThunk, ServicePlaceInstance, 4)
OPENSC4_THUNK(ServiceFindBestThunk, ServiceFindBest, 12)
OPENSC4_THUNK(ServicePickPlayingThunk, ServicePickPlaying, 8)
OPENSC4_THUNK(ServicePlayCellThunk, ServicePlayCell, 8)
OPENSC4_THUNK(ServiceScoreThunk, ServiceScore, 12)
OPENSC4_THUNK(InstancePlayThunk, InstancePlay, 0)
OPENSC4_THUNK(InstancePlayThroughThunk, InstancePlayThrough, 4)

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

bool InstallFile(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZSoundService_LoadPath, &ServiceLoadPathThunk},
        {addr::cGZSoundService_LoadFile, &ServiceLoadFileThunk},
        {addr::cGZSoundService_UseTable, &ServiceUseTableThunk},
        {addr::cGZSoundTable_ReadPath, &TableReadPathThunk},
        {addr::cGZSoundService_Name, &ServiceNameThunk},
    };
    return InstallAll(detours);
}

bool InstallControl(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZSoundService_LevelAndPlay, &ServiceLevelAndPlayThunk},
        {addr::cGZSoundService_StopFade, &ServiceStopFadeThunk},
        {addr::cGZSoundService_SetLevels, &ServiceSetLevelsThunk},
        {addr::cGZSoundService_SetSpreads, &ServiceSetSpreadsThunk},
        {addr::cGZSoundService_Spreads, &ServiceSpreadsThunk},
        {addr::cGZSoundService_Set408C, &ServiceSet408CThunk},
        {addr::cGZSoundService_SetPaused, &ServiceSetPausedThunk},
        {addr::cGZSoundService_Set6C, &ServiceSet6CThunk},
        {addr::cGZSoundService_StopNow, &ServiceStopNowThunk},
        {addr::cGZSoundService_CellAgain, &ServiceCellAgainThunk},
    };
    return InstallAll(detours);
}

bool InstallCount(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZSoundService_InWindow, &ServiceInWindowThunk},
        {addr::cGZSoundService_CountActive, &ServiceCountActiveThunk},
        {addr::cGZSoundService_CountMarked, &ServiceCountMarkedThunk},
        {addr::cGZSoundService_CountPlaying, &ServiceCountPlayingThunk},
        {addr::cGZSoundService_Recount, &ServiceRecountThunk},
        {addr::cGZSoundService_Wanted, &ServiceWantedThunk},
        {addr::cGZSoundService_Spare, &ServiceSpareThunk},
    };
    return InstallAll(detours);
}

bool InstallBeat(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZSoundService_Beat, &ServiceBeatThunk},
        {addr::cGZSoundService_PlaceInstance, &ServicePlaceInstanceThunk},
        {addr::cGZSoundService_FindBest, &ServiceFindBestThunk},
        {addr::cGZSoundService_PickPlaying, &ServicePickPlayingThunk},
        {addr::cGZSoundService_PlayCell, &ServicePlayCellThunk},
        {addr::cGZSoundService_Score, &ServiceScoreThunk},
    };
    return InstallAll(detours);
}

bool InstallInstance(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZSoundInstance_Play, &InstancePlayThunk},
        {addr::cGZSoundInstance_PlayThrough, &InstancePlayThroughThunk},
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
    registry::Register({"framework.soundsvc.file", "framework/sound", registry::Kind::Detour, registry::Phase::Attach,
                        addr::cGZSoundService_LoadFile,
                        "Our framework sound service's slots 38 to 40 and 46 (a score by path, folder and file, "
                        "table and name) and the sound table's reader by path (5 detours; part u7, docs/gzui.md)",
                        false, InstallFile}),
    registry::Register({"framework.soundsvc.control", "framework/sound", registry::Kind::Detour,
                        registry::Phase::Attach, addr::cGZSoundService_StopNow,
                        "Our framework sound service's slots 41 to 43, 47 to 51, 53 and 56: level and play, the "
                        "fade's stop, every level, the spreads, setters, the unlocked Stop, a cell again (10 "
                        "detours; part u7, docs/gzui.md)",
                        false, InstallControl}),
    registry::Register({"framework.soundsvc.count", "framework/sound", registry::Kind::Detour,
                        registry::Phase::Attach, addr::cGZSoundService_Wanted,
                        "Our framework sound service's slots 52 and 59 to 64: in the window, the counts of its "
                        "instances, how many may sound (7 detours; part u7, docs/gzui.md)",
                        false, InstallCount}),
    registry::Register({"framework.soundsvc.beat", "framework/sound", registry::Kind::Detour, registry::Phase::Attach,
                        addr::cGZSoundService_Beat,
                        "Our framework sound service's beats (its message target's slot 3), the neighbour pick and "
                        "slots 54, 55, 57 and 58: which cell plays, which lets go (6 detours; part u7, "
                        "docs/gzui.md)",
                        false, InstallBeat}),
    registry::Register({"framework.soundsvc.instance", "framework/sound", registry::Kind::Detour,
                        registry::Phase::Attach, addr::cGZSoundInstance_Play,
                        "Our framework sound instance's play (its slot 5) and the listener's path (2 detours; part "
                        "u7, docs/gzui.md)",
                        false, InstallInstance}),
};

} // namespace
} // namespace opensc4::gzui
