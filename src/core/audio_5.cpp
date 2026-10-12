// The audio code, part 5 (0x004EC3D0 to 0x004EF010): the end of the audio
// director, the three sound kinds of the game and the first half of the
// audio event handler.
//
// What this part holds:
//
//   - The audio director's small classes. The director is the COM director
//     at 0x00B21978 (vtables 0x00A93E18 and 0x00A93DE8 at +4, made on first
//     use by SC4Sound::StaticObject 0x004ECD60). Its constructor registers
//     92 class IDs. Eighteen of the factories are here: sixteen objects of 8
//     bytes (a vtable and a count; vtables 0x00A93E5C to 0x00A93FAC and
//     0x00A93FF4, six slots each, the sixth a function of part 1), one of
//     0x10 bytes (0x00A93FC4) and one of 0x14 bytes (0x00A93FDC). With them
//     the QueryInterface and Release functions those classes and the many
//     like them in part 4 share, and the director's deleting destructor.
//
//   - The sound kinds. A kind is what the framework's sound code asks how a
//     sound of that kind is to be played: slot 5 (source, voice) works out
//     the voice's level, side and distances from the sound's own values,
//     slot 6 (from, to) copies a fixed set of values. Source and voice are
//     value sets with slot 3 Set(number, value, flag) and slot 4
//     Get(number). The game has three kinds, 0x28 bytes each:
//       "2D SC4K"  ID 0x7DCDED2D, vtables 0x00A94068 and 0x00A94054 at +4;
//       "3D SC4K"  ID 0x7DCDED3D, vtables 0x00A940A4 and 0x00A94090 at +4;
//       "3DEffect" ID 0x0A63A543, vtables 0x00A940E4 and 0x00A940D0 at +4.
//     Layout: +0 the kind's vtable, +4 the start and stop interface
//     (0x89E51472: slot 3 start, slot 4 stop), +8 the count, +0xC the name (a
//     string), +0x20 the ID, +0x24 the audio director's object at +0x60,
//     whose value at +0x14 is the level everything is scaled by (of 1024).
//     Their holder (0x20 bytes, vtable 0x00A9403C, class ID 0xAA355871,
//     which the exe's ID table calls cSC4AudioAmbienceLayer; the director
//     keeps it at +0x58) puts them into the framework's registry of kinds
//     (service 0x1B2CE671, interface 0xBB1BD9A8, vtable 0x00AD2968: slot 19
//     adds a kind, slot 20 removes one by ID, slot 22 finds one) in place of
//     the framework's own 2D and 3D kinds, which it keeps to put back.
//     Holder: +4 count, +8 our 2D kind, +0xC the framework's 2D kind, +0x10
//     our 3D kind, +0x14 the framework's, +0x18 the effect kind, +0x1C the
//     registry.
//
//   - cSC4AudioEventHandler, first half. The object (vtables 0x00A94164 at
//     +0, 0x00A94154 at +4, 0x00A94144 the message target at +8, 0x00A94130
//     the start and stop interface at +0xC; constructor 0x004EF6F0 in part
//     6): +0x10 the music player (service 0x1D07B39F, interface 0xE0510260,
//     vtable 0x00ACC310), +0x14 the framework's sound service (0x23CECBA0,
//     vtable 0x00AD7180), +0x18 the ambience manager (service 0x49E50BA1,
//     vtable 0x00A94340), +0x1C a byte set while started, +0x20 the count,
//     +0x24 and +0x28 the view's width and height, +0x30 a stopwatch, +0x48
//     the milliseconds to wait before the next sound of one sort. Here: the
//     object's QueryInterface and Release, the stop half of start and stop,
//     the music starter and four handlers that the message handler of part 6
//     (0x004EF810) calls by message type.
//
// Replacement, off by default:
//
//   audio.part5: everything named above.
//
// Not functions: 0x004EC620 is the end of the factory 0x004EC600, 0x004EE050
// is the middle of the handler's stop (0x004EE000). The last function,
// 0x004EEC10, runs to 0x004EF00B: the next part's first entry, 0x004EEF80,
// is inside it.
//
// Still the game's: SC4Sound::StaticObject 0x004ECD60 (another thread's
// name; it makes the director on first use); 0x004ED100, 0x004ED130,
// 0x004EDF00 and 0x004EDFD0 (reference holders' constructors and
// destructors, code shared with the framework: callers at 0x008D46DD,
// 0x00913CDA, 0x00935AA6 and many more); 0x004ED900 (returns the dword at
// +0x20: folded with the same getter of twenty unrelated classes).
//
// Floating point: the game's x87 code at 53 bits is done in double here, and
// rounded to float where the original stores a float. The sine is taken by
// the x87 as in the original.
//
// Exception frames: 0x004EDF50 has one that swallows what the old object's
// Release throws; ours is plain C++.
#include "core/address_check.h"
#include "core/fixed_seed.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <utility>

namespace opensc4::audio {
namespace {

// The camera's code (another thread's; called by address, no entry of ours):
// (float[3] in and out), ret 4: a place in the city to a place in the view.
constexpr std::uintptr_t kCameraToView = 0x007F78E0;

constexpr std::uintptr_t kDirectorVtable = 0x00A93E18;

constexpr uint32_t kUnknownIid = 1;
constexpr uint32_t kStartStopIid = 0x89E51472;
constexpr uint32_t kTargetIid = 0x452294AA;
constexpr uint32_t kStandardMessageIid = 0x65297976;
constexpr uint32_t kOccupantIid = 0x26BB767E;

constexpr uint32_t kKind2D = 0x7DCDED2D;
constexpr uint32_t kKind3D = 0x7DCDED3D;
constexpr uint32_t kKindEffect = 0x0A63A543;

// 50.0f as the game pushes it.
constexpr uint32_t kFifty = 0x42480000;

namespace kind { // offsets from a sound kind
constexpr std::size_t kStartStop = 0x04;
constexpr std::size_t kCount = 0x08;
constexpr std::size_t kName = 0x0C;
constexpr std::size_t kNameBody = 0x10;
constexpr std::size_t kNameCount = 0x1C;
constexpr std::size_t kId = 0x20;
constexpr std::size_t kLevels = 0x24;
} // namespace kind

namespace holder { // offsets from the kinds' holder
constexpr std::size_t kCount = 0x04;
constexpr std::size_t kOur2D = 0x08;
constexpr std::size_t kTheir2D = 0x0C;
constexpr std::size_t kOur3D = 0x10;
constexpr std::size_t kTheir3D = 0x14;
constexpr std::size_t kEffect = 0x18;
constexpr std::size_t kRegistry = 0x1C;
} // namespace holder

namespace handler { // offsets from the event handler
constexpr std::size_t kSecondTarget = 0x04;
constexpr std::size_t kTarget = 0x08;
constexpr std::size_t kStartStop = 0x0C;
constexpr std::size_t kMusic = 0x10;
constexpr std::size_t kSoundService = 0x14;
constexpr std::size_t kAmbience = 0x18;
constexpr std::size_t kStarted = 0x1C;
constexpr std::size_t kCount = 0x20;
constexpr std::size_t kViewWidth = 0x24;
constexpr std::size_t kViewHeight = 0x28;
constexpr std::size_t kStopwatch = 0x30;
constexpr std::size_t kWait = 0x48;
} // namespace handler

struct Vec3 {
    float x;
    float y;
    float z;
};
static_assert(sizeof(Vec3) == 0xC);

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

uint32_t AddRef(void* object)
{
    return Virtual<uint32_t>(object, 0x4);
}

uint32_t Release(void* object)
{
    return Virtual<uint32_t>(object, 0x8);
}

// The truncating float-to-int conversion (_ftol, 0x009EEF04): the low dword.
int32_t Truncate(double value)
{
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

// FSIN of the angle times a float, left unrounded as in the game.
double SinTimes(double angle, float factor)
{
    double result;
    __asm {
        fld angle
        fsin
        fmul factor
        fstp result
    }
    return result;
}

// FSQRT.
double Root(double value)
{
    double result;
    __asm {
        fld value
        fsqrt
        fstp result
    }
    return result;
}

// A reference kept in a field, as the game writes it inline: the new object
// referenced, then the old one released.
void Keep(void*& field, void* object)
{
    void* const old = field;
    if (object == old)
        return;
    if (object)
        AddRef(object);
    field = object;
    if (old)
        Release(old);
}

// A system service of the framework, as the game's inlined holders fetch it.
void FetchService(uint32_t service, uint32_t iid, void** out)
{
    if (void* const framework = Cdecl<void*>(addr::RZGetFramework))
        Virtual<uint8_t>(framework, 0x14, service, iid, out);
}

// --- The director's small classes ------------------------------------------------

// The sixteen factories of 8-byte objects: a vtable and a count of 0.
void* MakeSmall(std::uintptr_t vtable)
{
    char* const object = Cdecl<char*>(addr::SC4_operator_new, uint32_t{8});
    if (!object)
        return nullptr;
    At<std::uintptr_t>(object, 0x0) = vtable;
    At<uint32_t>(object, 0x4) = 0;
    return object;
}

// The game's 0x004EC3D0 (class 0x8A401E0F).
void* __cdecl MakeA93E5C()
{
    return MakeSmall(0x00A93E5C);
}

// The game's 0x004EC3F0 (class 0x2A426986).
void* __cdecl MakeA93E74()
{
    return MakeSmall(0x00A93E74);
}

// The game's 0x004EC410 (class 0x8A42698A).
void* __cdecl MakeA93E8C()
{
    return MakeSmall(0x00A93E8C);
}

// The game's 0x004EC430 (class 0x6A42698E).
void* __cdecl MakeA93EA4()
{
    return MakeSmall(0x00A93EA4);
}

// The game's 0x004EC450 (class 0x0A426991).
void* __cdecl MakeA93EBC()
{
    return MakeSmall(0x00A93EBC);
}

// The game's 0x004EC470 (class 0x6A426995).
void* __cdecl MakeA93ED4()
{
    return MakeSmall(0x00A93ED4);
}

// The game's 0x004EC490 (class 0x0A426998).
void* __cdecl MakeA93EEC()
{
    return MakeSmall(0x00A93EEC);
}

// The game's 0x004EC4B0 (class 0xAA54E710).
void* __cdecl MakeA93F04()
{
    return MakeSmall(0x00A93F04);
}

// The game's 0x004EC4D0 (class 0xAA54E715).
void* __cdecl MakeA93F1C()
{
    return MakeSmall(0x00A93F1C);
}

// The game's 0x004EC4F0 (class 0x0A54E719).
void* __cdecl MakeA93F34()
{
    return MakeSmall(0x00A93F34);
}

// The game's 0x004EC510 (class 0x0B817C41).
void* __cdecl MakeA93F4C()
{
    return MakeSmall(0x00A93F4C);
}

// The game's 0x004EC530 (class 0xAB817C0B).
void* __cdecl MakeA93F64()
{
    return MakeSmall(0x00A93F64);
}

// The game's 0x004EC550 (class 0xCB817C1B).
void* __cdecl MakeA93F7C()
{
    return MakeSmall(0x00A93F7C);
}

// The game's 0x004EC570 (class 0x2B817C27).
void* __cdecl MakeA93F94()
{
    return MakeSmall(0x00A93F94);
}

// The game's 0x004EC590 (class 0x8B817C32).
void* __cdecl MakeA93FAC()
{
    return MakeSmall(0x00A93FAC);
}

// The game's 0x004EC6F0 (class 0xCA426982).
void* __cdecl MakeA93FF4()
{
    return MakeSmall(0x00A93FF4);
}

// The game's 0x004EC5B0: Release of the classes whose count is at +0xC.
uint32_t __fastcall ReleaseAtC(char* self, void*)
{
    const int32_t count = At<int32_t>(self, 0xC);
    if (count > 1) {
        At<int32_t>(self, 0xC) = count - 1;
        return static_cast<uint32_t>(count - 1);
    }
    Cdecl<void>(addr::SC4_operator_delete, self);
    return 0;
}

// The game's 0x004EC5D0: QueryInterface of the same classes.
uint8_t __fastcall QueryAtC(char* self, void*, uint32_t iid, void** out)
{
    if (iid == 0x4A40074C || iid == 0xCA400753) {
        *out = self;
        AddRef(self);
        return 1;
    }
    return 0;
}

// The game's 0x004EC600 (class 0x0A401BA0): 0x10 bytes.
void* __cdecl MakeA93FC4()
{
    char* const object = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0x10});
    if (!object)
        return nullptr;
    At<std::uintptr_t>(object, 0x0) = 0x00A93FC4;
    At<uint32_t>(object, 0x4) = 0;
    At<uint8_t>(object, 0x8) = 0;
    At<uint32_t>(object, 0xC) = 0;
    return object;
}

// The game's 0x004EC630: Release of the classes whose count is at +0x10.
uint32_t __fastcall ReleaseAt10(char* self, void*)
{
    const int32_t count = At<int32_t>(self, 0x10);
    if (count > 1) {
        At<int32_t>(self, 0x10) = count - 1;
        return static_cast<uint32_t>(count - 1);
    }
    Cdecl<void>(addr::SC4_operator_delete, self);
    return 0;
}

// The game's 0x004EC650 (class 0x8A427F7C): 0x14 bytes.
void* __cdecl MakeA93FDC()
{
    char* const object = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0x14});
    if (!object)
        return nullptr;
    At<std::uintptr_t>(object, 0x0) = 0x00A93FDC;
    At<uint32_t>(object, 0x4) = 0xFFFFFFFF;
    At<uint32_t>(object, 0x8) = 0xFFFFFFFF;
    At<uint8_t>(object, 0xC) = 0;
    At<uint32_t>(object, 0x10) = 0;
    return object;
}

// The game's 0x004EC680: the director's deleting destructor (slot 16).
void* __fastcall DirectorDelete(char* self, void*, uint32_t flags)
{
    This<void>(addr::SC4Audio_F4EBEC0, self);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// The game's 0x004EC6A0: Release of the classes whose count is at +4.
uint32_t __fastcall ReleaseAt4(char* self, void*)
{
    const int32_t count = At<int32_t>(self, 0x4);
    if (count > 1) {
        At<int32_t>(self, 0x4) = count - 1;
        return static_cast<uint32_t>(count - 1);
    }
    Cdecl<void>(addr::SC4_operator_delete, self);
    return 0;
}

// The game's 0x004EC6C0: QueryInterface of the same classes.
uint8_t __fastcall QueryAt4(char* self, void*, uint32_t iid, void** out)
{
    if (iid == 0x4A40074C || iid == 0xEA40074F) {
        *out = self;
        AddRef(self);
        return 1;
    }
    return 0;
}

// What the director's constructor registers, in its order: class ID and
// factory (the game's addresses; ours take over where they are hooked).
struct Registration {
    uint32_t classId;
    std::uintptr_t factory;
};

constexpr Registration kRegistrations[] = {
    {0xAA355871, 0x004ED700}, {0x29E2797F, 0x004EB120}, {0xBDB6171E, 0x004EB0A0}, {0x69E500BC, 0x004EB100},
    {0x6A12F51C, 0x004EB0C0}, {0x4A47DC0D, 0x004EB080}, {0xCA4A66B9, 0x004EB0E0}, {0x5CFBDBBF, 0x004EB150},
    {0x5CFBDBD5, 0x004EB180}, {0xCA426982, 0x004EC6F0}, {0x8A401E0F, 0x004EC3D0}, {0x2A426986, 0x004EC3F0},
    {0x8A42698A, 0x004EC410}, {0x6A42698E, 0x004EC430}, {0x0A426991, 0x004EC450}, {0x6A426995, 0x004EC470},
    {0x0A426998, 0x004EC490}, {0xAA54E710, 0x004EC4B0}, {0xAA54E715, 0x004EC4D0}, {0x0A54E719, 0x004EC4F0},
    {0x0B817C41, 0x004EC510}, {0xAB817C0B, 0x004EC530}, {0xCB817C1B, 0x004EC550}, {0x2B817C27, 0x004EC570},
    {0x8B817C32, 0x004EC590}, {0x0A401BA0, 0x004EC600}, {0x4A401BA7, 0x004EB260}, {0xCA401BAE, 0x004EB290},
    {0x6A401BB3, 0x004EB2C0}, {0x6A401BBA, 0x004EB2F0}, {0x0A42B520, 0x004EB320}, {0x6A43C75D, 0x004EB350},
    {0xCA5A6A5F, 0x004EB380}, {0x0A5A6A64, 0x004EB3B0}, {0x2A5A6A68, 0x004EB3E0}, {0x6A5A6A6B, 0x004EB410},
    {0x4A5A6A6F, 0x004EB440}, {0x0A5A6A72, 0x004EB470}, {0x0A5A6A76, 0x004EB4A0}, {0xAA5A6A7A, 0x004EB4D0},
    {0xAA5A6A7D, 0x004EB500}, {0xCA5A6A81, 0x004EB530}, {0x2A5A6A84, 0x004EB560}, {0x4A5A6A88, 0x004EB590},
    {0xEA5A6A8B, 0x004EB5C0}, {0x6A5A6A8F, 0x004EB5F0}, {0x4A5D0EB0, 0x004EB620}, {0xAA5D0EB5, 0x004EB650},
    {0xEA5F4954, 0x004EB680}, {0x8A5F603E, 0x004EB6B0}, {0x8A60F033, 0x004EB6E0}, {0x2A872395, 0x004EB710},
    {0xCA9D68A1, 0x004EB740}, {0xCA9D68A5, 0x004EB770}, {0xAA9D68A9, 0x004EB7A0}, {0x2AA17957, 0x004EB7D0},
    {0x8A427F7C, 0x004EC650}, {0xEA4298FC, 0x004EB800}, {0x2A429900, 0x004EB830}, {0x4A429903, 0x004EB860},
    {0x2A429907, 0x004EB890}, {0xCA42990A, 0x004EB8C0}, {0x8A53E23E, 0x004EB8F0}, {0xCA53E243, 0x004EB920},
    {0x0A53E247, 0x004EB950}, {0x4A53E24B, 0x004EB980}, {0x2A53E24F, 0x004EB9B0}, {0xCA53E252, 0x004EB9E0},
    {0x0A53E290, 0x004EBA10}, {0x2A53F0C3, 0x004EBA40}, {0x8A53F0E4, 0x004EBAA0}, {0x4A53F0E8, 0x004EBAD0},
    {0x8A53F584, 0x004EBB00}, {0xEA5A6FCE, 0x004EBB30}, {0x8A5A6FD1, 0x004EBB60}, {0xEA5A6FD4, 0x004EBB90},
    {0x6A5D0682, 0x004EBBC0}, {0x0A5D068D, 0x004EBBF0}, {0x2A5D0691, 0x004EBC20}, {0xEA5F495C, 0x004EBC50},
    {0x4A60D870, 0x004EBC80}, {0x8A60D873, 0x004EBCB0}, {0x6A60F166, 0x004EBCE0}, {0xEA80835E, 0x004EBD10},
    {0x0A808367, 0x004EBD40}, {0xCA80836C, 0x004EBD70}, {0x2A808371, 0x004EBDA0}, {0x4A808375, 0x004EBDD0},
    {0x4A8084F4, 0x004EBE00}, {0x2A9D6690, 0x004EBE30}, {0xAA9D6694, 0x004EBE60}, {0x2A9D6697, 0x004EBE90},
};
static_assert(sizeof(kRegistrations) / sizeof(kRegistrations[0]) == 92);

// The game's 0x004EC710: the director's constructor. It runs once, when
// SC4Sound::StaticObject is first called, which is long before our hooks
// are in place.
void* __fastcall DirectorConstruct(char* self, void*)
{
    This<void*>(addr::cRZCOMDllDirector_Construct, self);
    At<std::uintptr_t>(self, 0x0) = kDirectorVtable;
    At<std::uintptr_t>(self, 0x4) = 0x00A93DE8;
    for (std::size_t offset = 0x4C; offset <= 0x68; offset += 4)
        At<uint32_t>(self, offset) = 0;
    for (const Registration& entry : kRegistrations)
        This<void>(addr::cRZCOMDllDirector_AddCls, self, entry.classId, entry.factory);
    return self;
}

// --- The sound kinds -------------------------------------------------------------

uint32_t Get(void* values, uint32_t number)
{
    return Virtual<uint32_t>(values, 0x10, number);
}

uint32_t Set(void* values, uint32_t number, uint32_t value, uint32_t flag)
{
    return Virtual<uint32_t>(values, 0xC, number, value, flag);
}

void Copy(void* from, void* to, uint32_t number)
{
    const uint32_t value = Get(from, number);
    Set(to, number, value, 1);
}

// The signed division by 1024 the game does with CDQ, AND, ADD and SAR.
int32_t Div1024(int32_t value)
{
    return value / 1024;
}

// The product of two of a set's values (32 bits, as IMUL leaves it).
int32_t Product(uint32_t a, uint32_t b)
{
    return static_cast<int32_t>(a * b);
}

// A level times the director's level, of 1024. The getter is the game's
// 0x0040CE70 (the dword at +0x14, code shared with other classes).
uint32_t TimesMaster(char* self, int32_t level)
{
    void* const levels = At<void*>(self, kind::kLevels);
    const uint32_t master = levels ? This<uint32_t>(addr::cSC4Advisor_F40CE70, levels) : 0;
    return (master * static_cast<uint32_t>(level)) >> 10;
}

// The game's 0x004ECDA0: slot 6 of the 2D kind.
uint8_t __fastcall Kind2DCopy(char*, void*, void* from, void* to)
{
    for (const uint32_t number : {0x13u, 0x19u, 0x1Au, 0x1Bu, 0x23u, 0x24u, 0x25u, 0x26u, 0x33u})
        Copy(from, to, number);
    return 1;
}

// The game's 0x004ECE70: slot 6 of the 3D kind.
uint8_t __fastcall Kind3DCopy(char*, void*, void* from, void* to)
{
    Set(to, 0x1C, 1, 1);
    for (const uint32_t number : {0x1Du, 0x13u, 0x19u, 0x1Au, 0x1Bu, 0x23u, 0x24u, 0x25u, 0x26u, 0x33u, 0x1Eu, 0x1Fu})
        Copy(from, to, number);
    return 1;
}

// The game's 0x004ECF90: slot 6 of the effect kind.
uint8_t __fastcall KindEffectCopy(char*, void*, void* from, void* to)
{
    Set(to, 0x1C, 0, 1);
    for (const uint32_t number : {0x1Du, 0x13u, 0x19u, 0x1Au, 0x1Bu, 0x23u, 0x24u, 0x25u, 0x26u, 0x33u, 0x1Eu, 0x1Fu})
        Copy(from, to, number);
    return 1;
}

// The game's 0x004ED0B0: Release of the kinds' holder.
uint32_t __fastcall HolderRelease(char* self, void*)
{
    const uint32_t count = At<uint32_t>(self, holder::kCount);
    if (count == 1) {
        Virtual<void*>(self, 0xC, uint32_t{1});
        return 0;
    }
    At<uint32_t>(self, holder::kCount) = count - 1;
    return count - 1;
}

// The game's 0x004ED0D0: QueryInterface of the kinds' holder.
uint8_t __fastcall HolderQuery(char* self, void*, uint32_t iid, void** out)
{
    if (iid == kUnknownIid || iid == 0xAA355871) {
        *out = self;
        AddRef(self);
        return 1;
    }
    return 0;
}

// The game's 0x004ED180: a kind's start (slot 3 of the part at +4, which
// `this` is): takes the director's object at +0x60.
uint8_t __fastcall KindStart(char* part, void*)
{
    void* none = nullptr;
    void** from = &none;
    if (char* const director = Cdecl<char*>(addr::SC4Sound_StaticObject))
        from = &At<void*>(director, 0x60);
    Keep(At<void*>(part, 0x20), *from);
    return At<void*>(part, 0x20) != nullptr;
}

// The game's 0x004ED1F0: a kind's stop (slot 4 of the part at +4).
uint8_t __fastcall KindStop(char* part, void*)
{
    if (void* const levels = At<void*>(part, 0x20)) {
        At<void*>(part, 0x20) = nullptr;
        Release(levels);
    }
    return 1;
}

// The game's 0x004ED210: slot 5 of the 2D kind. The level is the sound's
// two levels multiplied (values 0x14 and 0x15, of 1024) times the
// director's; the side (0x16) goes over as it is.
uint8_t __fastcall Kind2DApply(char* self, void*, void* source, void* voice)
{
    const uint32_t side = Get(source, 0x16);
    const uint32_t b = Get(source, 0x15);
    const uint32_t a = Get(source, 0x14);
    const uint32_t level = TimesMaster(self, Div1024(Product(a, b)));
    Set(source, 0xC, level, 1);
    Set(voice, 0x16, side, 1);
    Set(voice, 0x15, level, 0);
    return 1;
}

// The game's 0x004ED2A0: slot 5 of the 3D kind. The place (0x20 to 0x22)
// and three more values go over; the sound's reach (0x38, hundredths) times
// the tuning value at 0x00B21A30 gives the voice's near distance (0x1E) and
// sixteen times that the far one (0x1F). The original leaves the last
// call's result in EAX; so does this.
uint32_t __fastcall Kind3DApply(char* self, void*, void* source, void* voice)
{
    for (const uint32_t number : {0x20u, 0x21u, 0x22u, 0x40u, 0x41u, 0x42u})
        Copy(source, voice, number);
    const double reach = static_cast<double>(Get(source, 0x38)) * static_cast<double>(0.01f);
    const float nearest =
        static_cast<float>(static_cast<double>(Global<float>(addr::SC4Audio_StartScale)) * reach);
    Set(voice, 0x1E, static_cast<uint32_t>(Truncate(nearest)), 1);
    Set(voice, 0x1F, static_cast<uint32_t>(Truncate(static_cast<double>(nearest) * 16.0)), 1);
    const uint32_t b = Get(source, 0x15);
    const uint32_t a = Get(source, 0x14);
    const uint32_t level = TimesMaster(self, Div1024(Product(a, b)));
    Set(source, 0xC, level, 1);
    return Set(voice, 0x15, level, 0);
}

// The game's 0x004ED3F0: slot 5 of the effect kind. The sound's place is
// taken to the view; its distance from the view's middle against the reach
// gives a fall-off, and how far across the view it is gives the side. With
// no camera view it returns 0; otherwise the last call's result is left in
// EAX as in the 3D kind.
uint32_t __fastcall KindEffectApply(char* self, void*, void* source, void* voice)
{
    for (const uint32_t number : {0x20u, 0x21u, 0x22u, 0x40u, 0x41u, 0x42u})
        Copy(source, voice, number);
    const float reach =
        static_cast<float>(static_cast<double>(Get(source, 0x38)) * static_cast<double>(0.01f));
    Vec3 place;
    place.x = static_cast<float>(static_cast<double>(static_cast<int32_t>(Get(source, 0x20))));
    place.y = static_cast<float>(static_cast<double>(static_cast<int32_t>(Get(source, 0x21))));
    const uint32_t z = Get(source, 0x22);
    void* const camera = Global<void*>(addr::SC4_CameraView);
    place.z = static_cast<float>(static_cast<double>(static_cast<int32_t>(z)));
    if (!camera)
        return z & 0xFFFFFF00u;
    This<void>(kCameraToView, camera, &place);
    const int32_t across = Truncate(place.x);
    char* const view = Global<char*>(addr::SC4_CameraView);
    const int32_t height = At<int32_t>(view, 0x2C);
    const int32_t width = At<int32_t>(view, 0x28);
    const int32_t dx = static_cast<int32_t>(static_cast<uint32_t>(across) - static_cast<uint32_t>(width / 2));
    const int32_t dy =
        static_cast<int32_t>(static_cast<uint32_t>(Truncate(place.y)) - static_cast<uint32_t>(height / 2));
    const int32_t squared = static_cast<int32_t>(static_cast<uint32_t>(dy) * static_cast<uint32_t>(dy) +
                                                 static_cast<uint32_t>(dx) * static_cast<uint32_t>(dx));
    const float distance = static_cast<float>(Root(static_cast<double>(squared)));
    const int32_t span = static_cast<int32_t>(static_cast<uint32_t>(height) + static_cast<uint32_t>(width));
    const float range = static_cast<float>(static_cast<double>(span) * static_cast<double>(0.5f) *
                                           static_cast<double>(reach));
    double fall = 1024.0;
    if (distance > range)
        fall = 1024.0 / (static_cast<double>(distance) / static_cast<double>(range));
    const int32_t falloff = Truncate(fall);

    const double angle =
        static_cast<double>(across) * static_cast<double>(3.14159274f) * static_cast<double>(0.0009765625f);
    const float rounded = static_cast<float>(angle);
    double swing = SinTimes(angle, 512.0f);
    if (rounded > 1.57079637f)
        swing = 1024.0 - swing;
    int32_t side = Truncate(swing);
    if (side > 0x400)
        side = 0x400;
    if (side < 0)
        side = 0;
    Set(voice, 0x16, static_cast<uint32_t>(side), 1);

    const uint32_t b = Get(source, 0x15);
    const uint32_t a = Get(source, 0x14);
    const int32_t own = Div1024(Product(a, b));
    const int32_t faded = Div1024(Product(static_cast<uint32_t>(own), static_cast<uint32_t>(falloff)));
    const uint32_t level = TimesMaster(self, faded);
    Set(source, 0xC, level, 1);
    return Set(voice, 0x15, level, 0);
}

// The game's 0x004ED6A0: the holder's destructor.
void __fastcall HolderDestruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0x0) = 0x00A9403C;
    for (const std::size_t offset : {holder::kRegistry, holder::kEffect, holder::kTheir3D, holder::kOur3D,
                                     holder::kTheir2D, holder::kOur2D}) {
        if (void* const object = At<void*>(self, offset))
            Release(object);
    }
}

// The game's 0x004ED700: the holder's factory (class 0xAA355871).
void* __cdecl MakeHolder()
{
    char* const object = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0x20});
    if (!object)
        return nullptr;
    At<std::uintptr_t>(object, 0x0) = 0x00A9403C;
    for (std::size_t offset = 0x4; offset <= 0x1C; offset += 4)
        At<uint32_t>(object, offset) = 0;
    return object;
}

// The game's 0x004ED730: the holder's deleting destructor (slot 3).
void* __fastcall HolderDelete(char* self, void*, uint32_t flags)
{
    HolderDestruct(self, nullptr);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// One of the holder's own kinds stopped (through its part at +4) and let go.
// `part` is the caller's holder of that interface, kept between kinds.
void StopKind(char* self, std::size_t offset, void*& part, bool resetFirst)
{
    void* const kindObject = At<void*>(self, offset);
    if (!kindObject)
        return;
    if (resetFirst && part) {
        void* const old = part;
        part = nullptr;
        Release(old);
    }
    if (Virtual<uint8_t>(kindObject, 0x0, kStartStopIid, &part))
        Virtual<uint8_t>(part, 0x10);
    if (void* const held = At<void*>(self, offset)) {
        At<void*>(self, offset) = nullptr;
        Release(held);
    }
}

// The game's 0x004ED750: the holder's stop. The framework's own 2D and 3D
// kinds go back into the registry, ours come out and are stopped.
uint8_t __fastcall HolderStop(char* self, void*)
{
    void* const their2D = At<void*>(self, holder::kTheir2D);
    Virtual<uint32_t>(At<void*>(self, holder::kRegistry), 0x50, kKind2D);
    Virtual<uint32_t>(At<void*>(self, holder::kRegistry), 0x4C, their2D);
    void* const their3D = At<void*>(self, holder::kTheir3D);
    Virtual<uint32_t>(At<void*>(self, holder::kRegistry), 0x50, kKind3D);
    Virtual<uint32_t>(At<void*>(self, holder::kRegistry), 0x4C, their3D);
    Virtual<uint32_t>(At<void*>(self, holder::kRegistry), 0x50, kKindEffect);

    void* part = nullptr;
    StopKind(self, holder::kOur2D, part, false);
    StopKind(self, holder::kOur3D, part, true);
    StopKind(self, holder::kEffect, part, true);
    for (const std::size_t offset : {holder::kTheir2D, holder::kTheir3D, holder::kRegistry}) {
        if (void* const held = At<void*>(self, offset)) {
            At<void*>(self, offset) = nullptr;
            Release(held);
        }
    }
    if (part)
        Release(part);
    return 1;
}

// A kind's constructor: the three differ in vtables, name and ID.
void* ConstructKind(char* self, std::uintptr_t vtable, std::uintptr_t partVtable, const char* name,
                    std::size_t length, uint32_t id)
{
    uint8_t allocator = 0;
    At<std::uintptr_t>(self, kind::kStartStop) = 0x00A94018;
    At<std::uintptr_t>(self, 0x0) = vtable;
    At<std::uintptr_t>(self, kind::kStartStop) = partVtable;
    At<uint32_t>(self, kind::kCount) = 0;
    At<uint32_t>(self, kind::kNameBody + 0x0) = 0;
    At<uint32_t>(self, kind::kNameBody + 0x4) = 0;
    At<uint32_t>(self, kind::kNameBody + 0x8) = 0;
    This<void>(addr::SC4String_FromRange, self + kind::kNameBody, name, name + length, &allocator);
    At<uint32_t>(self, kind::kNameCount) = 0;
    At<std::uintptr_t>(self, kind::kName) = addr::SC4String_Vtable;
    At<uint32_t>(self, kind::kId) = id;
    At<void*>(self, kind::kLevels) = nullptr;
    return self;
}

// The game's 0x004ED8A0: the 2D kind's constructor.
void* __fastcall Kind2DConstruct(char* self, void*)
{
    return ConstructKind(self, 0x00A94068, 0x00A94054, "2D SC4K", 7, kKind2D);
}

// The game's 0x004ED9A0: the 3D kind's constructor.
void* __fastcall Kind3DConstruct(char* self, void*)
{
    return ConstructKind(self, 0x00A940A4, 0x00A94090, "3D SC4K", 7, kKind3D);
}

// The game's 0x004EDA00: the effect kind's constructor.
void* __fastcall KindEffectConstruct(char* self, void*)
{
    return ConstructKind(self, 0x00A940E4, 0x00A940D0, "3DEffect", 8, kKindEffect);
}

// The game's 0x004ED910: a kind's Release.
uint32_t __fastcall KindRelease(char* self, void*)
{
    const uint32_t count = At<uint32_t>(self, kind::kCount);
    if (count == 1) {
        Virtual<void*>(self, 0x1C, uint32_t{1});
        return 0;
    }
    At<uint32_t>(self, kind::kCount) = count - 1;
    return count - 1;
}

// The game's 0x004ED930: a kind's QueryInterface.
uint8_t __fastcall KindQuery(char* self, void*, uint32_t iid, void** out)
{
    if (iid == kUnknownIid || iid == 0xDDCF3BD7) {
        *out = self;
        AddRef(self);
        return 1;
    }
    if (iid == kStartStopIid) {
        *out = self ? self + kind::kStartStop : nullptr;
        AddRef(self);
        return 1;
    }
    return 0;
}

// The game's 0x004EDA60: a kind's name copied into the caller's string
// (slot 4).
uint8_t __fastcall KindGetName(char* self, void*, void* string)
{
    Virtual<uint32_t>(string, 0x38, self + kind::kName);
    return 1;
}

// The game's 0x004EDA80: QueryInterface of a kind's part at +4.
uint8_t __fastcall KindPartQuery(char* part, void*, uint32_t iid, void** out)
{
    return KindQuery(part - kind::kStartStop, nullptr, iid, out);
}

// The game's 0x004EDA90: Release of a kind's part at +4.
uint32_t __fastcall KindPartRelease(char* part, void*)
{
    return KindRelease(part - kind::kStartStop, nullptr);
}

// One of the holder's kinds made, kept and started. False when the kind has
// no start interface or its start fails. The first of the three clears the
// caller's holder of the interface inline, the others through the game's
// function.
bool StartKind(char* self, std::size_t offset, void* (__fastcall* construct)(char*, void*), void*& part,
               bool first)
{
    char* const memory = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0x28});
    void* const made = memory ? construct(memory, nullptr) : nullptr;
    This<void*>(addr::SC4RefPtr_Assign, &At<void*>(self, offset), made);
    void* const kindObject = At<void*>(self, offset);
    void** out = &part;
    if (first) {
        if (void* const old = part) {
            part = nullptr;
            Release(old);
        }
    } else {
        out = This<void**>(addr::SC4RefPtr_Reset, &part);
    }
    if (!Virtual<uint8_t>(kindObject, 0x0, kStartStopIid, out))
        return false;
    return Virtual<uint8_t>(part, 0xC) != 0;
}

// The game's 0x004EDAA0: the holder's start. Finds the registry of kinds,
// keeps the framework's 2D and 3D kinds, makes and starts the game's three
// and puts them in.
uint8_t __fastcall HolderStart(char* self, void*)
{
    void* registry = nullptr;
    FetchService(0x1B2CE671, 0xBB1BD9A8, &registry);
    if (!registry) {
        This<void>(addr::SC4Audio_F4ED130, &registry);
        return 0;
    }
    Keep(At<void*>(self, holder::kRegistry), registry);

    void* part = nullptr;
    bool done = false;
    void* found = Virtual<void*>(At<void*>(self, holder::kRegistry), 0x58, kKind2D);
    Keep(At<void*>(self, holder::kTheir2D), found);
    if (At<void*>(self, holder::kTheir2D)) {
        found = Virtual<void*>(At<void*>(self, holder::kRegistry), 0x58, kKind3D);
        Keep(At<void*>(self, holder::kTheir3D), found);
        if (At<void*>(self, holder::kTheir3D) && StartKind(self, holder::kOur2D, &Kind2DConstruct, part, true) &&
            StartKind(self, holder::kOur3D, &Kind3DConstruct, part, false) &&
            StartKind(self, holder::kEffect, &KindEffectConstruct, part, false)) {
            Virtual<uint32_t>(At<void*>(self, holder::kRegistry), 0x4C, At<void*>(self, holder::kEffect));
            void* const our2D = At<void*>(self, holder::kOur2D);
            Virtual<uint32_t>(At<void*>(self, holder::kRegistry), 0x50, kKind2D);
            Virtual<uint32_t>(At<void*>(self, holder::kRegistry), 0x4C, our2D);
            void* const our3D = At<void*>(self, holder::kOur3D);
            Virtual<uint32_t>(At<void*>(self, holder::kRegistry), 0x50, kKind3D);
            Virtual<uint32_t>(At<void*>(self, holder::kRegistry), 0x4C, our3D);
            done = true;
        }
    }
    if (part)
        Release(part);
    This<void>(addr::SC4Audio_F4ED130, &registry);
    return done ? 1 : 0;
}

// A kind's deleting destructor (slot 7): the three differ in vtables.
void* DeleteKind(char* self, std::uintptr_t vtable, std::uintptr_t partVtable, uint32_t flags)
{
    At<std::uintptr_t>(self, 0x0) = vtable;
    At<std::uintptr_t>(self, kind::kStartStop) = partVtable;
    if (void* const levels = At<void*>(self, kind::kLevels))
        Release(levels);
    At<std::uintptr_t>(self, kind::kName) = addr::SC4String_Vtable;
    if (void* const text = At<void*>(self, kind::kNameBody))
        Cdecl<void>(addr::SC4_Free, text);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// The game's 0x004EDD30.
void* __fastcall Kind2DDelete(char* self, void*, uint32_t flags)
{
    return DeleteKind(self, 0x00A94068, 0x00A94054, flags);
}

// The game's 0x004EDD80.
void* __fastcall Kind3DDelete(char* self, void*, uint32_t flags)
{
    return DeleteKind(self, 0x00A940A4, 0x00A94090, flags);
}

// The game's 0x004EDDD0.
void* __fastcall KindEffectDelete(char* self, void*, uint32_t flags)
{
    return DeleteKind(self, 0x00A940E4, 0x00A940D0, flags);
}

// --- The event handler -----------------------------------------------------------

// The game's 0x004EDE20: which sound a message of type 0xEA3ADA33 asks for:
// the sort in ECX (1, 2 or 3), a flag in AL. Changes only EAX and ECX.
uint32_t __cdecl SoundOfSort(uint32_t sort, uint32_t flag)
{
    switch (sort) {
    case 1:
        return flag ? 0x0A5E802C : 0x4A5E7FFC;
    case 2:
        return flag ? 0x0A5E7F51 : 0x2A5E7F2C;
    case 3:
        return flag ? 0x6A5E8088 : 0x2A5E8067;
    default:
        return 0x69F01280;
    }
}

__declspec(naked) void SoundOfSortThunk()
{
    __asm push edx
    __asm movzx eax, al
    __asm push eax
    __asm push ecx
    __asm call SoundOfSort
    __asm add esp, 8
    __asm pop edx
    __asm ret
}

// The game's 0x004EDE60: the handler's Release.
uint32_t __fastcall HandlerRelease(char* self, void*)
{
    const int32_t count = At<int32_t>(self, handler::kCount);
    if (count > 1) {
        At<int32_t>(self, handler::kCount) = count - 1;
        return static_cast<uint32_t>(count - 1);
    }
    Virtual<void*>(self, 0x10, uint32_t{1});
    return 0;
}

// The game's 0x004EDE80: the handler's QueryInterface.
uint8_t __fastcall HandlerQuery(char* self, void*, uint32_t iid, void** out)
{
    if (iid == kUnknownIid || iid == 0xBB523BB2) {
        *out = self;
    } else if (iid == kStartStopIid) {
        *out = self ? self + handler::kStartStop : nullptr;
    } else if (iid == kTargetIid) {
        *out = self ? self + handler::kTarget : nullptr;
    } else {
        return 0;
    }
    AddRef(self);
    return 1;
}

// The game's 0x004EDF50: a reference holder takes another object (or none).
// What the old object's Release throws is swallowed in the original.
void** __fastcall HolderAssign(void** self, void*, void* object)
{
    if (object)
        AddRef(object);
    if (*self)
        Release(*self);
    *self = object;
    return self;
}

// The messages the handler listens for from start to stop, in the order
// stop takes them back.
constexpr uint32_t kAlwaysMessages[] = {
    0x4348B111, 0x2A3AD653, 0x0A611B34, 0x2A611B51, 0xAA611B6E, 0xAA650860, 0xAA650861,
    0x4A7FB7E9, 0x26D31EC1, 0x26D31EC2, 0xC53D10AA, 0x26D31EC4, 0x26D31EC5,
};

// The messages it listens for while a city is open.
constexpr uint32_t kCityMessages[] = {
    0xE94671EB, 0xE94671ED, 0x894671F6, 0xA9E3B6E3, 0x8A385DD8, 0xAA396960,
    0x49F10111, 0xC9F1011A, 0xEA3ADA33, 0x89F10866, 0xA9F10E45,
};

// The game's 0x004EE000: the handler's stop (slot 4 of the part at +0xC,
// which `this` is).
uint8_t __fastcall HandlerStop(char* part, void*)
{
    if (!At<uint8_t>(part, handler::kStarted - handler::kStartStop))
        return 1;
    void* service = nullptr;
    FetchService(0x23CECBA0, 0x106077D2, &service);
    if (!service) {
        This<void>(addr::SC4RefPtr_DestructGuarded, &service);
        return 0;
    }
    char* const self = part - handler::kStartStop;
    Virtual<uint32_t>(service, 0x40, self);
    if (void* const ambience = At<void*>(self, handler::kAmbience)) {
        At<void*>(self, handler::kAmbience) = nullptr;
        Release(ambience);
    }
    if (void* const music = At<void*>(self, handler::kMusic))
        Virtual<uint32_t>(music, 0x5C, uint32_t{0});
    HolderAssign(&At<void*>(self, handler::kMusic), nullptr, nullptr);

    void* server = nullptr;
    FetchService(0x04FA845B, 0x652294C7, &server);
    for (const uint32_t type : kAlwaysMessages)
        Virtual<uint8_t>(server, 0x18, self ? self + handler::kTarget : nullptr, type);

    void* const second = Global<void*>(addr::SC4_ServiceB43CC8);
    if (second)
        AddRef(second);
    Virtual<uint8_t>(second, 0x18, self ? self + handler::kSecondTarget : nullptr, uint32_t{0x62BA7B42});
    Virtual<uint8_t>(second, 0x18, self ? self + handler::kSecondTarget : nullptr, uint32_t{0x62BA7B43});
    At<uint8_t>(self, handler::kStarted) = 0;
    Release(second);
    This<void>(addr::SC4RefPtr_Destruct, &server);
    This<void>(addr::SC4RefPtr_DestructGuarded, &service);
    return 1;
}

// The game's 0x004EE270: starts a list of music unless the piece playing
// now is of that list already. With `from` the player gets a start entry
// (0, entry); without, none.
void __fastcall StartMusic(char* self, void*, uint32_t list, uint32_t entry, uint32_t from)
{
    void* const music = At<void*>(self, handler::kMusic);
    if (!music)
        return;
    uint32_t current;
    Virtual<uint32_t>(music, 0x28, &current);
    Virtual<uint32_t>(At<void*>(self, handler::kMusic), 0x78, uint32_t{0});
    const uint32_t playing = Virtual<uint32_t>(At<void*>(self, handler::kMusic), 0x30, current);
    if (playing != 0 && playing == Virtual<uint32_t>(At<void*>(self, handler::kMusic), 0x2C, list))
        return;
    void* const player = At<void*>(self, handler::kMusic);
    if (static_cast<uint8_t>(from)) {
        uint32_t start[2];
        start[1] = entry;
        start[0] = 0;
        Virtual<uint32_t>(player, 0x34, list, &start[0]);
    } else {
        Virtual<uint32_t>(player, 0x34, list, static_cast<uint32_t*>(nullptr));
    }
}

// The subject of a message held and asked for interface 0xC9B6D7D6, as the
// two message types that only check it do. False when there is no subject
// or it has no such interface.
bool SubjectHasC9B6D7D6(void* standard)
{
    void* held;
    This<void*>(addr::SC4RefHolder_SC4RefHolder, &held, Virtual<void*>(standard, 0x80));
    if (!held)
        return false;
    void* part = nullptr;
    void** const out = This<void**>(addr::SC4RefPtr_Reset, &part);
    const uint8_t found = Virtual<uint8_t>(held, 0x0, uint32_t{0xC9B6D7D6}, out);
    if (part)
        Release(part);
    Release(held);
    return found != 0;
}

// The game's 0x004EE300: the handler for messages 0x894671F6, 0xE94671EB,
// 0x49F10111 and 0xC9F1011A. The first two, with a first number of 0, 1, 5
// or 6 and a subject, make and play a sound of the sound service (0x09C66951
// and 0x29D99E9E).
uint8_t __fastcall OnToolMessages(char* self, void*, void* message)
{
    if (!message)
        return 0;
    void* standard = nullptr;
    Virtual<uint8_t>(message, 0x0, kStandardMessageIid, &standard);
    if (!standard)
        return 0;
    if (!At<void*>(self, handler::kSoundService)) {
        Release(standard);
        return 0;
    }
    if (!Virtual<uint8_t>(standard, 0x20)) {
        if (standard)
            Release(standard);
        return 0;
    }
    const uint32_t first = Virtual<uint32_t>(standard, 0x24);
    void* const subject = Virtual<void*>(standard, 0x80);
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    uint32_t sound = 0;
    if (type == 0x49F10111 || type == 0xC9F1011A) {
        if (!SubjectHasC9B6D7D6(standard)) {
            if (standard)
                Release(standard);
            return 0;
        }
    } else if (type == 0x894671F6) {
        sound = 0x09C66951;
    } else if (type == 0xE94671EB) {
        sound = 0x29D99E9E;
    }
    if (sound != 0 && first <= 6 && !(first >= 2 && first <= 4) && subject) {
        void* const service = At<void*>(self, handler::kSoundService);
        void* made = nullptr;
        void** const out = This<void**>(addr::SC4RefPtr_Reset, &made);
        Virtual<uint32_t>(service, 0x34, uint32_t{0xEA451903}, uint32_t{0x5B523BA4}, out, sound, subject,
                          uint32_t{0}, uint32_t{0});
        Virtual<uint32_t>(At<void*>(self, handler::kSoundService), 0x30, made);
        if (made)
            Release(made);
    }
    if (standard)
        Release(standard);
    return 1;
}

// The game's 0x004EE560: the handler for messages 0xA9E3B6E3, 0x8A385DD8,
// 0xAA396960 and 0xEA3ADA33.
uint8_t __fastcall OnOccupantMessages(char* self, void*, void* message)
{
    if (!At<void*>(self, handler::kAmbience))
        return 0;
    if (!message)
        return 0;
    void* standard = nullptr;
    Virtual<uint8_t>(message, 0x0, kStandardMessageIid, &standard);
    if (!standard)
        return 0;
    if (!At<void*>(self, handler::kSoundService)) {
        Release(standard);
        return 0;
    }
    void* part = nullptr;
    // The original leaves this unset when the subject is not an occupant.
    Vec3 place;
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    if (type == 0xA9E3B6E3) {
        // An occupant comes or goes: two sounds stopped, or one started at
        // its place.
        if (!Virtual<void*>(standard, 0x80) || !Virtual<uint8_t>(standard, 0x20) ||
            !Virtual<uint8_t>(standard, 0x34)) {
            if (standard)
                Release(standard);
            return 0;
        }
        const int32_t first = Virtual<int32_t>(standard, 0x24);
        if (first == 0) {
            void* ambience = At<void*>(self, handler::kAmbience);
            Virtual<uint32_t>(ambience, 0x38, uint32_t{0x49DE8A94}, Virtual<void*>(standard, 0x80));
            ambience = At<void*>(self, handler::kAmbience);
            Virtual<uint32_t>(ambience, 0x38, uint32_t{0xA9DE8B59}, Virtual<void*>(standard, 0x80));
        } else {
            const uint32_t sound = first > 0 ? 0x49DE8A94 : 0xA9DE8B59;
            void* const subject = Virtual<void*>(standard, 0x80);
            void** const out = This<void**>(addr::SC4RefPtr_Reset, &part);
            if (!Virtual<uint8_t>(subject, 0x0, kOccupantIid, out)) {
                if (part)
                    Release(part);
                if (standard)
                    Release(standard);
                return 0;
            }
            Virtual<uint32_t>(part, 0x20, &place);
            void* const ambience = At<void*>(self, handler::kAmbience);
            Vec3 at = place;
            Virtual<uint32_t>(ambience, 0x34, sound, Virtual<void*>(standard, 0x80), &at, kFifty);
            if (part)
                Release(part);
        }
    } else if (type == 0x8A385DD8) {
        const uint32_t first = Virtual<uint32_t>(standard, 0x24);
        if (first == 0xA9F14C47) {
            // Not more often than every one to four seconds, by the clock.
            char* const watch = self + handler::kStopwatch;
            if (This<uint8_t>(addr::GZStopwatch_IsRunning, watch) &&
                This<uint32_t>(addr::GZStopwatch_ElapsedMilliseconds, watch) < At<uint32_t>(self, handler::kWait)) {
                if (standard)
                    Release(standard);
                return 1;
            }
            This<void>(addr::GZStopwatch_Start, watch);
            const uint32_t scaled = static_cast<uint32_t>(fixed_seed::SoundRand()) * 3000u;
            // The division by 32767 as the compiler wrote it.
            const uint32_t high = static_cast<uint32_t>((static_cast<uint64_t>(scaled) * 0x20005u) >> 32);
            At<uint32_t>(self, handler::kWait) = ((((scaled - high) >> 1) + high) >> 14) + 1000;
        }
        void* const subject = Virtual<void*>(standard, 0x80);
        void** const out = This<void**>(addr::SC4RefPtr_Reset, &part);
        if (!Virtual<uint8_t>(subject, 0x0, uint32_t{0xA9B40F05}, out)) {
            if (part)
                Release(part);
            if (standard)
                Release(standard);
            return 0;
        }
        const float start = Global<float>(addr::SC4Audio_PlaceDefault);
        place.x = start;
        place.z = start;
        place.y = start;
        Virtual<uint32_t>(part, 0x1C, &place);
        void* service = At<void*>(self, handler::kSoundService);
        {
            const uint32_t second = Virtual<uint32_t>(standard, 0x38);
            const uint32_t again = Virtual<uint32_t>(standard, 0x24);
            Virtual<uint32_t>(service, 0x2C, uint32_t{0xEA451903}, again, second, uint32_t{1}, uint32_t{0});
        }
        service = At<void*>(self, handler::kSoundService);
        const int32_t z = Truncate(place.z);
        const int32_t y = Truncate(place.y);
        const int32_t x = Truncate(place.x);
        const uint32_t second = Virtual<uint32_t>(standard, 0x38);
        Virtual<uint32_t>(service, 0x2C, uint32_t{0xCA45190F}, second, x, y, z);
        if (part)
            Release(part);
    } else if (type == 0xAA396960) {
        void* const service = At<void*>(self, handler::kSoundService);
        const uint32_t second = Virtual<uint32_t>(standard, 0x38);
        Virtual<uint32_t>(service, 0x2C, uint32_t{0x6A451908}, second, uint32_t{0}, uint32_t{0}, uint32_t{0});
    } else if (type == 0xEA3ADA33) {
        // A sound of one of three sorts at the subject's place. The message
        // itself is asked here, not its standard interface.
        void* const subject = Virtual<void*>(message, 0x80);
        void** const out = This<void**>(addr::SC4RefPtr_Reset, &part);
        if (Virtual<uint8_t>(subject, 0x0, kOccupantIid, out)) {
            Virtual<uint32_t>(part, 0x20, &place);
            if (part) {
                void* const old = part;
                part = nullptr;
                Release(old);
            }
        }
        const uint32_t first = Virtual<uint8_t>(message, 0x20) ? Virtual<uint32_t>(message, 0x24) : 0xFFFFFFFF;
        const uint32_t sort = Virtual<uint8_t>(message, 0x34) ? Virtual<uint32_t>(message, 0x38) : 0xFFFFFFFF;
        Cdecl<void*>(addr::SC4Sound_DirectorObject60);
        void* const ambience = At<void*>(self, handler::kAmbience);
        Vec3 at = place;
        const uint32_t number = Cdecl<uint32_t>(addr::SC4Sound_NextInstance);
        const uint32_t sound = SoundOfSort(sort, first != 0);
        Virtual<uint32_t>(ambience, 0x34, sound, number, &at, kFifty);
        if (part)
            Release(part);
    }
    if (standard)
        Release(standard);
    return 1;
}

// The game's 0x004EE970: the handler for the city's opening (0x26D31EC1)
// and closing (0x26D31EC2): the city's messages are asked for or given
// back; on opening the view's size is kept and, in a city that has a name,
// message 0x2A3AD653 is posted to start the music.
uint8_t __fastcall OnCityMessages(char* self, void*, void* message)
{
    if (!message)
        return 0;
    void* standard = nullptr;
    Virtual<uint8_t>(message, 0x0, kStandardMessageIid, &standard);
    if (!standard)
        return 0;
    if (!At<void*>(self, handler::kSoundService)) {
        Release(standard);
        return 0;
    }
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    if (type == 0x26D31EC2) {
        if (void* const ambience = At<void*>(self, handler::kAmbience))
            Virtual<uint32_t>(ambience, 0x20, uint32_t{0}, uint32_t{0});
        void* server;
        This<void*>(addr::SC4MessageServerRef_Construct, &server);
        for (const uint32_t wanted : kCityMessages)
            Virtual<uint8_t>(server, 0x18, self + handler::kTarget, wanted);
        This<void>(addr::SC4RefPtr_Destruct, &server);
    } else if (type == 0x26D31EC1) {
        void* server;
        This<void*>(addr::SC4MessageServerRef_Construct, &server);
        for (const uint32_t wanted : kCityMessages)
            Virtual<uint8_t>(server, 0x14, self + handler::kTarget, wanted);
        if (!At<void*>(self, handler::kAmbience)) {
            This<void>(addr::SC4RefPtr_Destruct, &server);
            if (standard)
                Release(standard);
            return 0;
        }
        At<uint32_t>(self, handler::kViewWidth) = At<uint32_t>(Global<void*>(addr::SC4_CameraView), 0x28);
        At<uint32_t>(self, handler::kViewHeight) = At<uint32_t>(Global<void*>(addr::SC4_CameraView), 0x2C);
        void* const city = Global<void*>(addr::SC4_City);
        if (city && Virtual<uint8_t>(city, 0x58)) {
            void* const memory = Cdecl<void*>(addr::RZ_operator_new, uint32_t{0x2C});
            void* const posted = memory ? This<void*>(addr::cGZMessage_cGZMessage, memory) : nullptr;
            AddRef(posted);
            Virtual<uint32_t>(posted, 0x14, uint32_t{0x2A3AD653});
            Virtual<uint32_t>(posted, 0x2C, uint32_t{0});
            Virtual<uint8_t>(Global<void*>(addr::SC4_MessageServer), 0x10, posted, uint32_t{0});
            Release(posted);
        }
        This<void>(addr::SC4RefPtr_Destruct, &server);
    }
    if (standard)
        Release(standard);
    return 1;
}

// One of two sounds by bit 1 of a message's third number.
uint32_t SoundByBit(uint32_t third)
{
    return (third & 2) ? 0xEA62C800 : 0xAA62C82A;
}

// The game's 0x004EEC10: the handler for messages 0x89F10866, 0x49E7B1E2,
// 0xA9F10E45 and 0xC9E7B1E8: sounds of the ambience manager at a subject's
// place.
uint8_t __fastcall OnPlacedSoundMessages(char* self, void*, void* message)
{
    if (!At<void*>(self, handler::kAmbience))
        return 0;
    if (!message)
        return 0;
    void* standard = nullptr;
    Virtual<uint8_t>(message, 0x0, kStandardMessageIid, &standard);
    if (!standard)
        return 0;
    void* part = nullptr;
    Vec3 place;
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    if (type == 0x89F10866) {
        const uint32_t first = Virtual<uint32_t>(standard, 0x24);
        bool placed = false;
        if (Virtual<uint8_t>(standard, 0x34))
            placed = Virtual<uint32_t>(standard, 0x38) != 0;
        const uint32_t sound = SoundByBit(Virtual<uint32_t>(standard, 0x4C));
        if (placed) {
            void* const subject = Virtual<void*>(standard, 0x80);
            void** const out = This<void**>(addr::SC4RefPtr_Reset, &part);
            Virtual<uint8_t>(subject, 0x0, uint32_t{0xA821EF94}, out);
            if (part) {
                void* const occupant = Virtual<void*>(part, 0xC0);
                Virtual<uint32_t>(occupant, 0x20, &place);
                void* const ambience = At<void*>(self, handler::kAmbience);
                Vec3 at = place;
                Virtual<uint32_t>(ambience, 0x34, sound, first, &at, uint32_t{0});
                if (part)
                    Release(part);
            }
        } else {
            Virtual<uint32_t>(At<void*>(self, handler::kAmbience), 0x38, sound, first);
        }
    } else if (type == 0x49E7B1E2) {
        if (!Virtual<void*>(standard, 0x80)) {
            if (standard)
                Release(standard);
            return 0;
        }
        void* const subject = Virtual<void*>(standard, 0x80);
        void** const out = This<void**>(addr::SC4RefPtr_Reset, &part);
        if (!Virtual<uint8_t>(subject, 0x0, kOccupantIid, out) || !At<void*>(self, handler::kAmbience)) {
            if (part)
                Release(part);
            if (standard)
                Release(standard);
            return 0;
        }
        Virtual<uint32_t>(part, 0x20, &place);
        void* const ambience = At<void*>(self, handler::kAmbience);
        Vec3 at = place;
        Virtual<uint32_t>(ambience, 0x34, uint32_t{0xC9DC0A61}, part, &at, uint32_t{0});
        if (part)
            Release(part);
    } else if (type == 0xA9F10E45) {
        if (!Virtual<void*>(standard, 0x80)) {
            if (standard)
                Release(standard);
            return 0;
        }
        const uint32_t first = Virtual<uint32_t>(standard, 0x24);
        const uint32_t third = Virtual<uint32_t>(standard, 0x4C);
        void* const subject = Virtual<void*>(standard, 0x80);
        void** const out = This<void**>(addr::SC4RefPtr_Reset, &part);
        Virtual<uint8_t>(subject, 0x0, uint32_t{0xA821EF94}, out);
        if (part) {
            if (!At<void*>(self, handler::kAmbience)) {
                Release(part);
                if (standard)
                    Release(standard);
                return 0;
            }
            void* const occupant = Virtual<void*>(part, 0xC0);
            Virtual<uint32_t>(occupant, 0x20, &place);
            void* const ambience = At<void*>(self, handler::kAmbience);
            Vec3 at = place;
            Virtual<uint32_t>(ambience, 0x3C, SoundByBit(third), first, &at, uint32_t{0});
            if (part)
                Release(part);
        }
    } else if (type == 0xC9E7B1E8) {
        if (!Virtual<void*>(standard, 0x80)) {
            if (standard)
                Release(standard);
            return 0;
        }
        void* const subject = Virtual<void*>(standard, 0x80);
        void** const out = This<void**>(addr::SC4RefPtr_Reset, &part);
        if (!Virtual<uint8_t>(subject, 0x0, kOccupantIid, out) || !At<void*>(self, handler::kAmbience) ||
            !Virtual<uint8_t>(standard, 0x20)) {
            if (part)
                Release(part);
            if (standard)
                Release(standard);
            return 0;
        }
        const bool placed = Virtual<uint32_t>(standard, 0x24) != 0;
        Virtual<uint32_t>(part, 0x20, &place);
        void* const ambience = At<void*>(self, handler::kAmbience);
        if (placed) {
            Vec3 at = place;
            Virtual<uint32_t>(ambience, 0x34, uint32_t{0x49E7B4B0}, part, &at, uint32_t{0});
        } else {
            Virtual<uint32_t>(ambience, 0x38, uint32_t{0x49E7B4B0}, part);
        }
        if (part)
            Release(part);
    }
    if (standard)
        Release(standard);
    return 1;
}

bool InstallAudio5(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[72] = {};
    const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4Audio_F4EC3D0, reinterpret_cast<void*>(&MakeA93E5C)},
        {addr::SC4Audio_F4EC3F0, reinterpret_cast<void*>(&MakeA93E74)},
        {addr::SC4Audio_F4EC410, reinterpret_cast<void*>(&MakeA93E8C)},
        {addr::SC4Audio_F4EC430, reinterpret_cast<void*>(&MakeA93EA4)},
        {addr::SC4Audio_F4EC450, reinterpret_cast<void*>(&MakeA93EBC)},
        {addr::SC4Audio_F4EC470, reinterpret_cast<void*>(&MakeA93ED4)},
        {addr::SC4Audio_F4EC490, reinterpret_cast<void*>(&MakeA93EEC)},
        {addr::SC4Audio_F4EC4B0, reinterpret_cast<void*>(&MakeA93F04)},
        {addr::SC4Audio_F4EC4D0, reinterpret_cast<void*>(&MakeA93F1C)},
        {addr::SC4Audio_F4EC4F0, reinterpret_cast<void*>(&MakeA93F34)},
        {addr::SC4Audio_F4EC510, reinterpret_cast<void*>(&MakeA93F4C)},
        {addr::SC4Audio_F4EC530, reinterpret_cast<void*>(&MakeA93F64)},
        {addr::SC4Audio_F4EC550, reinterpret_cast<void*>(&MakeA93F7C)},
        {addr::SC4Audio_F4EC570, reinterpret_cast<void*>(&MakeA93F94)},
        {addr::SC4Audio_F4EC590, reinterpret_cast<void*>(&MakeA93FAC)},
        {addr::SC4Audio_F4EC5B0, reinterpret_cast<void*>(&ReleaseAtC)},
        {addr::SC4Audio_F4EC5D0, reinterpret_cast<void*>(&QueryAtC)},
        {addr::SC4Audio_F4EC600, reinterpret_cast<void*>(&MakeA93FC4)},
        {addr::SC4Audio_F4EC630, reinterpret_cast<void*>(&ReleaseAt10)},
        {addr::SC4Audio_F4EC650, reinterpret_cast<void*>(&MakeA93FDC)},
        {addr::SC4Audio_F4EC680, reinterpret_cast<void*>(&DirectorDelete)},
        {addr::SC4Audio_F4EC6A0, reinterpret_cast<void*>(&ReleaseAt4)},
        {addr::SC4Audio_F4EC6C0, reinterpret_cast<void*>(&QueryAt4)},
        {addr::SC4Audio_F4EC6F0, reinterpret_cast<void*>(&MakeA93FF4)},
        {addr::SC4Audio_F4EC710, reinterpret_cast<void*>(&DirectorConstruct)},
        {addr::SC4Audio_F4ECDA0, reinterpret_cast<void*>(&Kind2DCopy)},
        {addr::SC4Audio_F4ECE70, reinterpret_cast<void*>(&Kind3DCopy)},
        {addr::SC4Audio_F4ECF90, reinterpret_cast<void*>(&KindEffectCopy)},
        {addr::SC4Audio_F4ED0B0, reinterpret_cast<void*>(&HolderRelease)},
        {addr::SC4Audio_F4ED0D0, reinterpret_cast<void*>(&HolderQuery)},
        {addr::SC4Audio_F4ED180, reinterpret_cast<void*>(&KindStart)},
        {addr::SC4Audio_F4ED1F0, reinterpret_cast<void*>(&KindStop)},
        {addr::SC4Audio_F4ED210, reinterpret_cast<void*>(&Kind2DApply)},
        {addr::SC4Audio_F4ED2A0, reinterpret_cast<void*>(&Kind3DApply)},
        {addr::SC4Audio_F4ED3F0, reinterpret_cast<void*>(&KindEffectApply)},
        {addr::SC4Audio_F4ED6A0, reinterpret_cast<void*>(&HolderDestruct)},
        {addr::SC4Audio_F4ED700, reinterpret_cast<void*>(&MakeHolder)},
        {addr::SC4Audio_F4ED730, reinterpret_cast<void*>(&HolderDelete)},
        {addr::SC4Audio_F4ED750, reinterpret_cast<void*>(&HolderStop)},
        {addr::SC4Audio_F4ED8A0, reinterpret_cast<void*>(&Kind2DConstruct)},
        {addr::SC4Audio_F4ED910, reinterpret_cast<void*>(&KindRelease)},
        {addr::SC4Audio_F4ED930, reinterpret_cast<void*>(&KindQuery)},
        {addr::SC4Audio_F4ED9A0, reinterpret_cast<void*>(&Kind3DConstruct)},
        {addr::SC4Audio_F4EDA00, reinterpret_cast<void*>(&KindEffectConstruct)},
        {addr::SC4Audio_F4EDA60, reinterpret_cast<void*>(&KindGetName)},
        {addr::SC4Audio_F4EDA80, reinterpret_cast<void*>(&KindPartQuery)},
        {addr::SC4Audio_F4EDA90, reinterpret_cast<void*>(&KindPartRelease)},
        {addr::SC4Audio_F4EDAA0, reinterpret_cast<void*>(&HolderStart)},
        {addr::SC4Audio_F4EDD30, reinterpret_cast<void*>(&Kind2DDelete)},
        {addr::SC4Audio_F4EDD80, reinterpret_cast<void*>(&Kind3DDelete)},
        {addr::SC4Audio_F4EDDD0, reinterpret_cast<void*>(&KindEffectDelete)},
        {addr::SC4Audio_F4EDE20, reinterpret_cast<void*>(&SoundOfSortThunk)},
        {addr::SC4Audio_F4EDE60, reinterpret_cast<void*>(&HandlerRelease)},
        {addr::SC4Audio_F4EDE80, reinterpret_cast<void*>(&HandlerQuery)},
        {addr::SC4Audio_F4EDF50, reinterpret_cast<void*>(&HolderAssign)},
        {addr::SC4Audio_F4EE000, reinterpret_cast<void*>(&HandlerStop)},
        {addr::SC4Audio_F4EE270, reinterpret_cast<void*>(&StartMusic)},
        {addr::SC4Audio_F4EE300, reinterpret_cast<void*>(&OnToolMessages)},
        {addr::SC4Audio_F4EE560, reinterpret_cast<void*>(&OnOccupantMessages)},
        {addr::SC4Audio_F4EE970, reinterpret_cast<void*>(&OnCityMessages)},
        {addr::SC4Audio_F4EEC10, reinterpret_cast<void*>(&OnPlacedSoundMessages)},
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

const bool g_audio5Registered = registry::Register({
    "audio.part5",
    "audio",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4Audio_F4ED700,
    "Our audio code, part 5: the director's small classes, the three sound kinds and the event handler's "
    "first half (docs/audio.md)",
    false,
    InstallAudio5,
});

} // namespace
} // namespace opensc4::audio
