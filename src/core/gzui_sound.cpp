// The GZ framework's sound director and the classes it registers that live
// next to it (docs/gzui.md, part u5), on the game's own objects.
// Replacements, each off by default:
//
//   framework.sound.director  the sound director's constructor and the
//                             static one's getter
//   framework.sound.table     the sound table 0x5C1CF5A3 (vtable
//                             0x00AD4818): constructor, Get, Release,
//                             destructor
//   framework.sound.instance  the sound instance 0xBDD9D7A5 (vtables
//                             0x00AD48A0 and 0x00AD487C): constructors,
//                             AddRef, Release, QueryInterface, destructors,
//                             the level, Update, Start, Stop, Place, the
//                             setters, and the hex digit test its slot 5
//                             uses
//   framework.sound.desc      the sound description 0x7DD9C9F2 (vtable
//                             0x00AD4908): constructor, QueryInterface,
//                             Release, destructor, the name and the
//                             setters of its own
//
// Still the game's: the thunk 0x00945785, the base QueryInterface
// 0x009457C6, the adjustors 0x009459A7, 0x009459AF, 0x009459B7 and
// 0x00945C64, the two-instruction getters (0x0094595C, 0x00945E3B,
// 0x00945E3F, 0x00945E43), the setters other classes' vtables share
// (0x00945E47, 0x00945EAC, 0x00945ED0, 0x00945EDC), and the string, list
// and reference holder code these classes call by address. Ours call each
// other directly; a virtual call stays one.
//
// The game's methods answer a byte (AL) over whatever its last call or
// value left in EAX; ours give the same EAX where it comes from a call or a
// value and 0 above AL where the game leaves its caller's EAX.
//
// Every detour is entered through a thunk that keeps ECX and EDX. The
// groups go in at Attach: the audio director adds the sound director at its
// start.
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

template <typename T>
inline T& Global(std::uintptr_t address)
{
    return *reinterpret_cast<T*>(address);
}

inline uint32_t Bits(const void* pointer) { return static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(pointer)); }

// A byte answer over what the game leaves in the rest of EAX.
inline uint32_t Answer(uint32_t eax, bool value) { return (eax & ~0xFFu) | (value ? 1u : 0u); }
inline bool Yes(uint32_t eax) { return (eax & 0xFF) != 0; }

inline uint32_t AddRef(void* object) { return Virtual<uint32_t>(object, 0x4); }
inline uint32_t Release(void* object) { return Virtual<uint32_t>(object, 0x8); }

uint32_t Delete(void* p) { return CallCdecl<uint32_t>(addr::SC4_operator_delete, p); }
uint32_t Free(void* p) { return CallCdecl<uint32_t>(addr::SC4_Free, p); }

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("gzui: sound: first %s", name);
}

#define OPENSC4_FIRST(name)                                                                                    \
    do {                                                                                                       \
        static bool logged_ = false;                                                                           \
        if (!logged_)                                                                                          \
            FirstCall(name, logged_);                                                                          \
    } while (0)

constexpr uint32_t kIID_cIGZUnknown = 1;
constexpr uint32_t kIID_Instance = 0x5DD9D787;
constexpr uint32_t kIID_Desc = 0x1DD9C9DE;
constexpr uint32_t kIID_Positioner = 0x306077D2;  // the buffer's: slot 5 a place (x, y, z)
constexpr uint32_t kIID_ServicePart = 0x5B34F0AF; // the buffer's: handed to the sound service's slot 31

constexpr uint32_t kClsid_Service = 0x3C1CF59E;
constexpr uint32_t kClsid_Table = 0x5C1CF5A3;
constexpr uint32_t kClsid_190 = 0xBDDA72DD;
constexpr uint32_t kClsid_Desc = 0x7DD9C9F2;
constexpr uint32_t kClsid_Instance = 0xBDD9D7A5;

// ====================================================================================
// The sound description 0x7DD9C9F2 (0x68 bytes)

constexpr std::size_t kDescCount = 0x4;
constexpr std::size_t kDescName = 0xC;   // SC4String
constexpr std::size_t kDescLevel = 0x24;
constexpr std::size_t kDescSideX = 0x28;
constexpr std::size_t kDescSideY = 0x2C;
constexpr std::size_t kDescRandom = 0x31; // byte: a centred side becomes random
constexpr std::size_t kDescTime = 0x38;
constexpr std::size_t kDescLoops = 0x48;
constexpr std::size_t kDescLooping = 0x50; // byte
constexpr std::size_t kDescList = 0x64;

// The game's 0x00945DCC. +0x40 is not set.
char* __fastcall DescConstruct(char* self, void*)
{
    OPENSC4_FIRST("DescConstruct");
    At<std::uintptr_t>(self, 0) = addr::cGZSoundDesc_Vtable;
    At<uint32_t>(self, kDescCount) = 0;
    At<uint32_t>(self, 0x8) = 0;
    CallThis<void*>(addr::SC4String_SC4String, self + kDescName);
    At<uint32_t>(self, kDescSideX) = 0x200;
    At<uint32_t>(self, kDescSideY) = 0x200;
    At<uint8_t>(self, 0x20) = 0;
    At<uint32_t>(self, kDescLevel) = 0x400;
    At<uint8_t>(self, 0x30) = 0;
    At<uint8_t>(self, kDescRandom) = 0;
    At<uint32_t>(self, 0x34) = 0;
    At<uint32_t>(self, kDescTime) = 0;
    At<uint32_t>(self, 0x3C) = 0;
    At<uint8_t>(self, 0x44) = 0;
    At<uint32_t>(self, kDescLoops) = 8;
    At<uint32_t>(self, 0x4C) = 0;
    At<uint8_t>(self, kDescLooping) = 1;
    At<uint32_t>(self, 0x54) = 0;
    At<uint32_t>(self, 0x58) = 0;
    At<uint32_t>(self, 0x5C) = 0;
    At<uint8_t>(self, 0x60) = 0;
    uint8_t allocator = 0;
    CallThis<void*>(addr::GZList_Construct, self + kDescList, &allocator);
    return self;
}

// The game's 0x00945F24: the list, then the name (whose EAX it answers).
uint32_t __fastcall DescDestruct(char* self, void*)
{
    OPENSC4_FIRST("DescDestruct");
    CallThis<void>(addr::cGZDBSegmentPackedFile_ListDestruct, self + kDescList);
    return CallThis<uint32_t>(addr::SC4String_Destroy, self + kDescName);
}

// The game's 0x0094578A (slot 0).
uint32_t __fastcall DescQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    OPENSC4_FIRST("DescQueryInterface");
    if (iid != kIID_cIGZUnknown && iid != kIID_Desc)
        return 0;
    *out = self;
    return Answer(AddRef(self), true);
}

// The game's 0x00945FFC (slot 2).
uint32_t __fastcall DescRelease(char* self, void*)
{
    OPENSC4_FIRST("DescRelease");
    const uint32_t count = At<uint32_t>(self, kDescCount);
    if (count <= 1) {
        DescDestruct(self, nullptr);
        Delete(self);
        return 0;
    }
    return At<uint32_t>(self, kDescCount) = count - 1;
}

// The game's 0x00945E53 (slot 24): the name set by its slot 13.
uint32_t __fastcall DescSetName(char* self, void*, const void* name)
{
    OPENSC4_FIRST("DescSetName");
    return Answer(Virtual<uint32_t>(self + kDescName, 0x34, name), true);
}

// The setters of its own: a byte (AL 1 over the caller's EAX, ours 1) or a
// dword (AL 1 over the argument).
template <std::size_t Offset>
uint32_t __fastcall DescSetByte(char* self, void*, uint32_t value)
{
    static bool logged = false;
    if (!logged) {
        logged = true;
        log::Write("gzui: sound: first DescSet%02X", static_cast<unsigned>(Offset));
    }
    At<uint8_t>(self, Offset) = static_cast<uint8_t>(value);
    return 1;
}

template <std::size_t Offset>
uint32_t __fastcall DescSetDword(char* self, void*, uint32_t value)
{
    static bool logged = false;
    if (!logged) {
        logged = true;
        log::Write("gzui: sound: first DescSet%02X", static_cast<unsigned>(Offset));
    }
    At<uint32_t>(self, Offset) = value;
    return Answer(value, true);
}

// ====================================================================================
// The sound table 0x5C1CF5A3 (0x4048 bytes: a 64 by 64 table of
// descriptions at +0x48)

constexpr std::size_t kTableCount = 0x4;
constexpr std::size_t kTableCells = 0x48;
constexpr uint32_t kTableSide = 64;

// The game's 0x0094585F.
char* __fastcall TableConstruct(char* self, void*)
{
    OPENSC4_FIRST("TableConstruct");
    At<uint32_t>(self, 0x14) = 4;
    At<uint32_t>(self, 0x28) = 4;
    At<uint32_t>(self, 0x2C) = 4;
    At<uint32_t>(self, 0x38) = 1;
    At<uint32_t>(self, 0x3C) = 1;
    At<uint32_t>(self, kTableCount) = 0;
    At<uint32_t>(self, 0xC) = 0;
    At<uint32_t>(self, 0x10) = 0;
    At<uint32_t>(self, 0x18) = 0;
    At<uint32_t>(self, 0x1C) = 0;
    At<uint32_t>(self, 0x40) = 0;
    At<uint32_t>(self, 0x44) = 0;
    At<uint32_t>(self, 0x20) = 2;
    At<uint32_t>(self, 0x24) = 2;
    At<std::uintptr_t>(self, 0) = addr::cGZSoundTable_Vtable;
    At<uint32_t>(self, 0x8) = 0x400;
    At<uint32_t>(self, 0x30) = 0x78;
    At<uint32_t>(self, 0x34) = 8;
    std::memset(self + kTableCells, 0, kTableSide * kTableSide * 4);
    return self;
}

// The game's 0x009458C4 (slot 3; `ret 0xC`): the third argument is not
// read, nor are the indexes checked.
void* __fastcall TableGet(char* self, void*, uint32_t row, uint32_t column, uint32_t)
{
    OPENSC4_FIRST("TableGet");
    const uint32_t index = (row << 6) + column;
    return *reinterpret_cast<void**>(static_cast<std::uintptr_t>(Bits(self) + index * 4 + kTableCells));
}

// The game's 0x009457AF (slot 2).
uint32_t __fastcall TableRelease(char* self, void*)
{
    OPENSC4_FIRST("TableRelease");
    const uint32_t count = At<uint32_t>(self, kTableCount);
    if (count <= 1) {
        Virtual<uint32_t>(self, 0x10, 1u);
        return 0;
    }
    return At<uint32_t>(self, kTableCount) = count - 1;
}

// The game's 0x00945FC4: every cell's description destroyed and deleted.
uint32_t __fastcall TableDestruct(char* self, void*)
{
    OPENSC4_FIRST("TableDestruct");
    At<std::uintptr_t>(self, 0) = addr::cGZSoundTable_Vtable;
    void** cell = reinterpret_cast<void**>(self + kTableCells);
    uint32_t eax = 0;
    for (uint32_t row = 0; row < kTableSide; ++row) {
        for (uint32_t column = 0; column < kTableSide; ++column, ++cell) {
            if (char* const desc = static_cast<char*>(*cell)) {
                DescDestruct(desc, nullptr);
                eax = Delete(desc);
            }
            *cell = nullptr;
        }
    }
    return eax;
}

// ====================================================================================
// The sound instance 0xBDD9D7A5 (0x5C bytes; a base part at +4)

constexpr std::size_t kCount = 0x14;
constexpr std::size_t kService = 0x18;  // its +0x10 a generator, +0x14 and +0x1C's +8 levels
constexpr std::size_t kBuffer = 0x1C;   // slots 7, 20, 21 (the level), 22
constexpr std::size_t kState = 0x20;    // 0 idle, 2 playing, 3 waiting
constexpr std::size_t kDesc = 0x24;
constexpr std::size_t kLoops = 0x28;
constexpr std::size_t kStopAtEnd = 0x2D; // byte
constexpr std::size_t kVolume = 0x30;
constexpr std::size_t kLevel = 0x34;
constexpr std::size_t kWorked = 0x38;   // the level worked out
constexpr std::size_t kListener = 0x54; // slots 4 (finished) and 9 (go on?)

constexpr uint32_t kPlaying = 2;
constexpr uint32_t kWaiting = 3;

// The constructors' common part. +0x28, +0x30 and +0x38 are not set.
char* InstanceInit(char* self, void* service)
{
    At<std::uintptr_t>(self, 0) = addr::cGZSoundInstance_BaseVtable;
    CallThis<void*>(addr::GZBaseAD8FA4_Construct, self + 4);
    At<void*>(self, kService) = service;
    At<uint32_t>(self, kCount) = 0;
    At<std::uintptr_t>(self, 0) = addr::cGZSoundInstance_Vtable;
    At<std::uintptr_t>(self, 4) = addr::cGZSoundInstance_Vtable2;
    At<void*>(self, kBuffer) = nullptr;
    At<uint32_t>(self, kState) = 0;
    At<void*>(self, kDesc) = nullptr;
    At<uint8_t>(self, 0x2C) = 0;
    At<uint8_t>(self, kStopAtEnd) = 0;
    At<uint8_t>(self, 0x3C) = 0;
    At<uint32_t>(self, 0x40) = 0;
    At<uint32_t>(self, 0x44) = 0;
    At<uint32_t>(self, kLevel) = 0x400;
    At<uint8_t>(self, 0x3D) = 1;
    At<uint32_t>(self, 0x48) = 100;
    At<uint32_t>(self, 0x4C) = 1000;
    At<uint32_t>(self, 0x50) = 100;
    At<void*>(self, kListener) = nullptr;
    At<uint8_t>(self, 0x58) = 0;
    return self;
}

// The game's 0x009458D6 (`ret 4`): the service given.
char* __fastcall InstanceConstructOwned(char* self, void*, void* service)
{
    OPENSC4_FIRST("InstanceConstructOwned");
    return InstanceInit(self, service);
}

// The game's 0x00945C6C.
char* __fastcall InstanceConstruct(char* self, void*)
{
    OPENSC4_FIRST("InstanceConstruct");
    return InstanceInit(self, nullptr);
}

// The game's 0x00945942 (slot 1).
uint32_t __fastcall InstanceAddRef(char* self, void*)
{
    OPENSC4_FIRST("InstanceAddRef");
    return ++At<uint32_t>(self, kCount);
}

// The game's 0x009457E1 (through the adjustor 0x009459AF): the last one
// deletes it through the part at +4's slot 7.
uint32_t __fastcall InstanceRelease(char* self, void*)
{
    OPENSC4_FIRST("InstanceRelease");
    const uint32_t count = At<uint32_t>(self, kCount);
    if (count <= 1) {
        Virtual<uint32_t>(self + 4, 0x1C, 1u);
        return 0;
    }
    return At<uint32_t>(self, kCount) = count - 1;
}

// The game's 0x009457FB (through the adjustor 0x009459A7).
uint32_t __fastcall InstanceQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    OPENSC4_FIRST("InstanceQueryInterface");
    if (iid != kIID_cIGZUnknown && iid != kIID_Instance)
        return 0;
    *out = self;
    return Answer(AddRef(self), true);
}

// The game's 0x00945820: 32-bit products, signed divisions.
int32_t __fastcall InstanceComputeLevel(char* self, void*)
{
    OPENSC4_FIRST("InstanceComputeLevel");
    char* const desc = At<char*>(self, kDesc);
    int32_t level = static_cast<int32_t>(At<uint32_t>(desc, kDescLevel) * At<uint32_t>(self, kVolume));
    At<int32_t>(self, kWorked) = level;
    char* const service = At<char*>(self, kService);
    level = static_cast<int32_t>(At<uint32_t>(service, 0x14) * static_cast<uint32_t>(level)) / 0x100000;
    At<int32_t>(self, kWorked) = level;
    const uint32_t master = At<uint32_t>(At<char*>(service, 0x1C), 0x8);
    level = static_cast<int32_t>(master * At<uint32_t>(self, kLevel) * static_cast<uint32_t>(level)) / 0x100000;
    At<int32_t>(self, kWorked) = level;
    return level;
}

// The game's 0x00945949 (slot 10).
uint32_t __fastcall InstanceIsPlaying(char* self, void*)
{
    OPENSC4_FIRST("InstanceIsPlaying");
    return At<uint32_t>(self, kState) == kPlaying ? 1u : 0u;
}

// The game's 0x00945953 (slot 11).
uint32_t __fastcall InstanceIsActive(char* self, void*)
{
    OPENSC4_FIRST("InstanceIsActive");
    return At<uint32_t>(self, kState) != 0 ? 1u : 0u;
}

// The game's 0x00945960 (slot 13): AL the byte.
uint32_t __fastcall InstanceSetByte58(char* self, void*, uint32_t value)
{
    OPENSC4_FIRST("InstanceSetByte58");
    At<uint8_t>(self, 0x58) = static_cast<uint8_t>(value);
    return value & 0xFF;
}

// The game's 0x0094596A (slot 15).
uint32_t __fastcall InstanceSetByte3D(char* self, void*, uint32_t value)
{
    OPENSC4_FIRST("InstanceSetByte3D");
    At<uint8_t>(self, 0x3D) = static_cast<uint8_t>(value);
    return 1;
}

// The game's 0x00945976 (slot 16).
uint32_t __fastcall InstanceSetPlace(char* self, void*, uint32_t x, uint32_t y, uint32_t z)
{
    OPENSC4_FIRST("InstanceSetPlace");
    At<uint32_t>(self, 0x40) = x;
    At<uint32_t>(self, 0x44) = y;
    At<uint32_t>(self, 0x48) = z;
    At<uint8_t>(self, 0x3C) = 1;
    return Answer(z, true);
}

// The game's 0x00945994 (slot 17).
uint32_t __fastcall InstanceSetDistances(char* self, void*, uint32_t near, uint32_t far)
{
    OPENSC4_FIRST("InstanceSetDistances");
    At<uint32_t>(self, 0x4C) = near;
    At<uint32_t>(self, 0x50) = far;
    return Answer(far, true);
}

// The game's 0x00945E70: slot 4 of the instance (its description) and slot
// 26 of the description (its level); both at +0x24.
uint32_t __fastcall InstanceSetDesc(char* self, void*, uint32_t value)
{
    OPENSC4_FIRST("InstanceSetDesc");
    At<uint32_t>(self, kDesc) = value;
    return Answer(value, true);
}

// The game's 0x00945A70 (slot 8).
uint32_t __fastcall InstanceStop(char* self, void*)
{
    OPENSC4_FIRST("InstanceStop");
    const uint32_t eax = CallThis<uint32_t>(addr::SC4RefPtr_Assign, self + kBuffer, static_cast<void*>(nullptr));
    At<uint32_t>(self, kState) = 0;
    At<uint8_t>(self, kStopAtEnd) = 0;
    return Answer(eax, true);
}

// The game's 0x00945A89 (slot 9).
uint32_t __fastcall InstanceSetLevel(char* self, void*, uint32_t level)
{
    OPENSC4_FIRST("InstanceSetLevel");
    const bool buffered = At<void*>(self, kBuffer) != nullptr;
    At<uint32_t>(self, kLevel) = level;
    if (!buffered)
        return Answer(level, true);
    InstanceComputeLevel(self, nullptr);
    return Virtual<uint32_t>(At<void*>(self, kBuffer), 0x54, At<uint32_t>(self, kWorked));
}

// The listener told the sound finished: its slot 4 with the description and
// the description's +0x38 as a float.
uint32_t TellFinished(char* self, char* desc)
{
    const float time = static_cast<float>(At<int32_t>(desc, kDescTime));
    return Virtual<uint32_t>(At<void*>(self, kListener), 0x10, desc, time);
}

// The game's 0x009459BF (slot 6).
uint32_t __fastcall InstanceUpdate(char* self, void*)
{
    OPENSC4_FIRST("InstanceUpdate");
    const uint32_t state = At<uint32_t>(self, kState);
    if (state == kPlaying) {
        char* const desc = At<char*>(self, kDesc);
        if (At<uint8_t>(desc, kDescLooping) != 0) {
            const uint32_t left = --At<uint32_t>(self, kLoops);
            if (At<uint8_t>(self, kStopAtEnd) != 0) {
                if (left == 0)
                    return Bits(desc);
                return Virtual<uint32_t>(self, 0x20);
            }
            if (left != 0)
                return Bits(desc);
            return At<uint32_t>(self, kLoops) = At<uint32_t>(desc, kDescLoops);
        }
        uint32_t eax = Bits(desc);
        if (void* const listener = At<void*>(self, kListener)) {
            eax = Virtual<uint32_t>(listener, 0x24, desc);
            if (!Yes(eax)) {
                eax = TellFinished(self, At<char*>(self, kDesc));
                At<uint32_t>(self, kState) = 0;
                return eax;
            }
        }
        void* const buffer = At<void*>(self, kBuffer);
        if (!buffer)
            return eax;
        eax = Virtual<uint32_t>(buffer, 0x1C);
        if (Yes(eax))
            return eax;
        eax = Virtual<uint32_t>(self, 0x1C);
        At<uint32_t>(self, kState) = 0;
        return eax;
    }
    if (state != kWaiting)
        return state - kWaiting;
    uint32_t eax = 0;
    if (void* const listener = At<void*>(self, kListener)) {
        eax = Virtual<uint32_t>(listener, 0x24, At<void*>(self, kDesc));
        if (!Yes(eax)) {
            eax = TellFinished(self, At<char*>(self, kDesc));
            At<uint32_t>(self, kState) = 0;
            return eax;
        }
    }
    void* const buffer = At<void*>(self, kBuffer);
    if (!buffer)
        return eax;
    eax = Virtual<uint32_t>(buffer, 0x50);
    if (eax != 0)
        return eax;
    eax = Virtual<uint32_t>(self, 0x20);
    At<uint32_t>(self, kState) = 0;
    return eax;
}

// The game's 0x00945C20 (`ret 4`).
uint32_t __fastcall InstanceRestart(char* self, void*, char* desc)
{
    OPENSC4_FIRST("InstanceRestart");
    if (At<uint32_t>(self, kState) == kPlaying)
        TellFinished(self, desc);
    const uint32_t state = At<uint32_t>(self, kState);
    if (state == kPlaying) {
        if (At<uint32_t>(desc, kDescTime) != 0)
            At<uint32_t>(self, kState) = kWaiting;
        return 1;
    }
    const uint32_t eax = ((state - kWaiting) & ~0xFFu) | 1u;
    if (state != kWaiting)
        At<uint32_t>(self, kState) = 0;
    return eax;
}

// The buffer's slot 20, then its slot 22 with what that answered, 0, the
// count and 1 (the game pushes those three before the first call); the
// vtable read once.
uint32_t BufferPlay(void* buffer, uint32_t count)
{
    void** const vtable = *static_cast<void***>(buffer);
    const uint32_t status = reinterpret_cast<uint32_t(__thiscall*)(void*)>(vtable[0x50 / 4])(buffer);
    return reinterpret_cast<uint32_t(__thiscall*)(void*, uint32_t, uint32_t, uint32_t, uint32_t)>(vtable[0x58 / 4])(
        buffer, status, 0u, count, 1u);
}

// The game's 0x00945D1A (slot 7).
uint32_t __fastcall InstanceStart(char* self, void*)
{
    OPENSC4_FIRST("InstanceStart");
    if (At<void*>(self, kListener))
        return InstanceRestart(self, nullptr, At<char*>(self, kDesc));
    if (At<void*>(self, kBuffer)) {
        void* service = nullptr;
        CallThis<void*>(addr::SC4RefPtr_ConstructSoundService, &service);
        if (service) {
            void* part = nullptr;
            if (Yes(Virtual<uint32_t>(At<void*>(self, kBuffer), 0x0, kIID_ServicePart, &part)))
                Virtual<uint32_t>(service, 0x7C, part);
            if (part)
                Release(part);
        }
        CallThis<void>(addr::SC4RefPtr_DestructGuarded, &service);
    }
    const uint32_t state = At<uint32_t>(self, kState);
    if (state == kPlaying) {
        char* const desc = At<char*>(self, kDesc);
        if (At<uint32_t>(desc, kDescTime) == 0)
            return Answer(Virtual<uint32_t>(self, 0x20), true);
        At<uint32_t>(self, kState) = kWaiting;
        void* const buffer = At<void*>(self, kBuffer);
        if (!buffer)
            return Answer(Bits(desc), true);
        return Answer(BufferPlay(buffer, At<uint32_t>(desc, kDescTime)), true);
    }
    const uint32_t eax = ((state - kWaiting) & ~0xFFu) | 1u;
    if (state != kWaiting)
        At<uint32_t>(self, kState) = 0;
    return eax;
}

// The service's generator: two words of the game's LCG step.
uint32_t Draw(char* self)
{
    void* const generator = At<void*>(At<char*>(self, kService), 0x10);
    return static_cast<uint32_t>(CallThis<uint64_t>(addr::cRZRandom_Advance, generator));
}

inline double RandomSign(char* self) { return static_cast<double>(static_cast<int32_t>((Draw(self) & 1) * 2) - 1); }

// The game's 0x00945AAF (slot 14): the side from the description's +0x28
// and +0x2C, a centred one random when its +0x31 is set, to the buffer's
// interface 0x306077D2. Neither the buffer nor the interface is checked.
uint32_t __fastcall InstancePlace(char* self, void*)
{
    OPENSC4_FIRST("InstancePlace");
    char* const desc = At<char*>(self, kDesc);
    if (!desc)
        return 0;
    if (Global<uint8_t>(addr::cGZSoundInstance_ForceRandom) != 0)
        At<uint8_t>(desc, kDescRandom) = 1;
    const double scale = Global<double>(addr::cGZSoundInstance_SideScale);
    const int32_t sideX = At<int32_t>(desc, kDescSideX);
    const int32_t sideY = At<int32_t>(desc, kDescSideY);
    double x = static_cast<double>(static_cast<int32_t>(static_cast<uint32_t>(sideX) - 0x200u)) * scale;
    double y = static_cast<double>(static_cast<int32_t>(static_cast<uint32_t>(sideY) - 0x200u)) * scale;
    const bool random = At<uint8_t>(desc, kDescRandom) != 0;
    if (random && sideX == 0x200 && sideY == 0x200) {
        uint32_t corner = Draw(self) & 3;
        if (Global<uint8_t>(addr::cGZSoundInstance_ForceCorner) != 0)
            corner = Global<uint32_t>(addr::cGZSoundInstance_Corner);
        const double minusOne = Global<double>(addr::SC4_MinusOneDouble);
        switch (corner) {
        case 0:
            x = 0.0;
            y = 1.0;
            break;
        case 1:
            x = 0.0;
            y = minusOne;
            break;
        case 2:
            x = minusOne;
            y = 0.0;
            break;
        case 3:
            x = 1.0;
            y = 0.0;
            break;
        default:
            break;
        }
    } else {
        if (random && sideX == 0x200)
            x = RandomSign(self);
        if (random && sideY == 0x200)
            y = RandomSign(self);
    }
    const double hundred = Global<double>(addr::cGZSoundInstance_Hundred);
    x *= hundred;
    y *= hundred;
    void* positioner = nullptr;
    Virtual<uint32_t>(At<void*>(self, kBuffer), 0x0, kIID_Positioner, &positioner);
    uint32_t eax = Virtual<uint32_t>(positioner, 0x14, static_cast<float>(x), 0.0f, static_cast<float>(y));
    if (positioner)
        eax = Release(positioner);
    return eax;
}

// The game's 0x00945CEE: the listener and the buffer released, the part
// at +4 destroyed; the vtables are not stored again.
uint32_t __fastcall InstanceDestruct(char* self, void*)
{
    OPENSC4_FIRST("InstanceDestruct");
    uint32_t eax = 0;
    if (void* const listener = At<void*>(self, kListener))
        eax = Release(listener);
    if (void* const buffer = At<void*>(self, kBuffer))
        eax = Release(buffer);
    CallThis<void>(addr::GZBaseAD8FA4_Destruct, self ? self + 4 : nullptr);
    return eax;
}

// The game's 0x00945CD2 (through the adjustor 0x009459B7).
char* __fastcall InstanceDeletingDestructor(char* self, void*, uint32_t flags)
{
    OPENSC4_FIRST("InstanceDeletingDestructor");
    InstanceDestruct(self, nullptr);
    if (flags & 1)
        Delete(self);
    return self;
}

// The game's 0x00945F38 (`ret 4`; `this` is not read): whether the text
// holds 8 hex digits in a row, by strchr (a zero byte counts).
uint32_t __fastcall InstanceHasHexRun(void*, void*, const void* text)
{
    OPENSC4_FIRST("InstanceHasHexRun");
    alignas(4) char string[0x14];
    CallThis<void*>(addr::SC4String_FromIGZString, string, text);
    const uint32_t length = CallThis<uint32_t>(addr::cRZBaseString_Strlen, string);
    const char* const body = At<const char*>(string, 0x4);
    uint32_t eax = Bits(body);
    bool found = false;
    uint32_t run = 0;
    for (uint32_t i = 0; i < length; ++i) {
        eax = Bits(CallCdecl<const char*>(addr::SC4_StrChr, reinterpret_cast<const char*>(addr::SC4_HexDigits),
                                          static_cast<int>(static_cast<signed char>(body[i]))));
        run = eax ? run + 1 : 0;
        if (run == 8) {
            found = true;
            break;
        }
    }
    At<std::uintptr_t>(string, 0) = addr::SC4String_Vtable;
    if (body)
        eax = Free(const_cast<char*>(body));
    return Answer(eax, found);
}

// ====================================================================================
// The director

// The game's 0x009456EC: the sound director (vtables 0x00AD47C0 and
// 0x00AD4790), five classes registered by their factories' addresses.
char* __fastcall DirectorConstruct(char* self, void*)
{
    OPENSC4_FIRST("DirectorConstruct");
    CallThis<void*>(addr::cRZCOMDllDirector_Construct, self);
    At<std::uintptr_t>(self, 0) = addr::cGZSoundDirector_Vtable;
    At<std::uintptr_t>(self, 0x4) = addr::cGZSoundDirector_Vtable2;
    CallThis<void>(addr::cRZCOMDllDirector_AddCls, self, kClsid_Service, addr::cGZSoundDirector_New4144);
    CallThis<void>(addr::cRZCOMDllDirector_AddCls, self, kClsid_Table, addr::cGZSoundDirector_New4048);
    CallThis<void>(addr::cRZCOMDllDirector_AddCls, self, kClsid_190, addr::cGZSoundDirector_New190);
    CallThis<void>(addr::cRZCOMDllDirector_AddCls, self, kClsid_Desc, addr::cGZSoundDirector_New68);
    CallThis<void>(addr::cRZCOMDllDirector_AddCls, self, kClsid_Instance, addr::cGZSoundDirector_New5C);
    return self;
}

// The game's 0x0094575A: the static director, made on first use.
void* __fastcall DirectorGet(void*, void*)
{
    OPENSC4_FIRST("DirectorGet");
    uint32_t& guard = Global<uint32_t>(addr::cGZSoundDirector_Guard);
    char* const director = reinterpret_cast<char*>(addr::cGZSoundDirector_Instance);
    if (!(guard & 1)) {
        guard |= 1;
        DirectorConstruct(director, nullptr);
        CallCdecl<int32_t>(addr::SC4_atexit, addr::cGZSoundDirector_AtExit);
    }
    return director;
}

// ====================================================================================
// Thunks and installation

OPENSC4_THUNK(DescConstructThunk, DescConstruct, 0)
OPENSC4_THUNK(DescDestructThunk, DescDestruct, 0)
OPENSC4_THUNK(DescQueryInterfaceThunk, DescQueryInterface, 8)
OPENSC4_THUNK(DescReleaseThunk, DescRelease, 0)
OPENSC4_THUNK(DescSetNameThunk, DescSetName, 4)
OPENSC4_THUNK(DescSet20Thunk, DescSetByte<0x20>, 4)
OPENSC4_THUNK(DescSet2CThunk, DescSetDword<0x2C>, 4)
OPENSC4_THUNK(DescSet30Thunk, DescSetByte<0x30>, 4)
OPENSC4_THUNK(DescSet31Thunk, DescSetByte<0x31>, 4)
OPENSC4_THUNK(DescSet34Thunk, DescSetDword<0x34>, 4)
OPENSC4_THUNK(DescSet3CThunk, DescSetDword<0x3C>, 4)
OPENSC4_THUNK(DescSet44Thunk, DescSetByte<0x44>, 4)
OPENSC4_THUNK(DescSet50Thunk, DescSetByte<0x50>, 4)
OPENSC4_THUNK(DescSet54Thunk, DescSetDword<0x54>, 4)
OPENSC4_THUNK(DescSet58Thunk, DescSetDword<0x58>, 4)
OPENSC4_THUNK(DescSet5CThunk, DescSetDword<0x5C>, 4)
OPENSC4_THUNK(DescSet60Thunk, DescSetByte<0x60>, 4)

OPENSC4_THUNK(TableConstructThunk, TableConstruct, 0)
OPENSC4_THUNK(TableGetThunk, TableGet, 12)
OPENSC4_THUNK(TableReleaseThunk, TableRelease, 0)
OPENSC4_THUNK(TableDestructThunk, TableDestruct, 0)

OPENSC4_THUNK(InstanceConstructOwnedThunk, InstanceConstructOwned, 4)
OPENSC4_THUNK(InstanceConstructThunk, InstanceConstruct, 0)
OPENSC4_THUNK(InstanceAddRefThunk, InstanceAddRef, 0)
OPENSC4_THUNK(InstanceReleaseThunk, InstanceRelease, 0)
OPENSC4_THUNK(InstanceQueryInterfaceThunk, InstanceQueryInterface, 8)
OPENSC4_THUNK(InstanceComputeLevelThunk, InstanceComputeLevel, 0)
OPENSC4_THUNK(InstanceIsPlayingThunk, InstanceIsPlaying, 0)
OPENSC4_THUNK(InstanceIsActiveThunk, InstanceIsActive, 0)
OPENSC4_THUNK(InstanceSetByte58Thunk, InstanceSetByte58, 4)
OPENSC4_THUNK(InstanceSetByte3DThunk, InstanceSetByte3D, 4)
OPENSC4_THUNK(InstanceSetPlaceThunk, InstanceSetPlace, 12)
OPENSC4_THUNK(InstanceSetDistancesThunk, InstanceSetDistances, 8)
OPENSC4_THUNK(InstanceSetDescThunk, InstanceSetDesc, 4)
OPENSC4_THUNK(InstanceStopThunk, InstanceStop, 0)
OPENSC4_THUNK(InstanceSetLevelThunk, InstanceSetLevel, 4)
OPENSC4_THUNK(InstanceUpdateThunk, InstanceUpdate, 0)
OPENSC4_THUNK(InstanceRestartThunk, InstanceRestart, 4)
OPENSC4_THUNK(InstanceStartThunk, InstanceStart, 0)
OPENSC4_THUNK(InstancePlaceThunk, InstancePlace, 0)
OPENSC4_THUNK(InstanceDestructThunk, InstanceDestruct, 0)
OPENSC4_THUNK(InstanceDeletingDestructorThunk, InstanceDeletingDestructor, 4)
OPENSC4_THUNK(InstanceHasHexRunThunk, InstanceHasHexRun, 4)

OPENSC4_THUNK(DirectorConstructThunk, DirectorConstruct, 0)
OPENSC4_THUNK(DirectorGetThunk, DirectorGet, 0)

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

bool InstallDirector(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZSoundDirector_Construct, &DirectorConstructThunk},
        {addr::cGZSoundDirector_Get, &DirectorGetThunk},
    };
    return InstallAll(detours);
}

bool InstallTable(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZSoundTable_Construct, &TableConstructThunk},
        {addr::cGZSoundTable_Get, &TableGetThunk},
        {addr::cGZSoundTable_Release, &TableReleaseThunk},
        {addr::cGZSoundTable_Destruct, &TableDestructThunk},
    };
    return InstallAll(detours);
}

bool InstallInstance(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZSoundInstance_ConstructOwned, &InstanceConstructOwnedThunk},
        {addr::cGZSoundInstance_Construct, &InstanceConstructThunk},
        {addr::cGZSoundInstance_AddRef, &InstanceAddRefThunk},
        {addr::cGZSoundInstance_Release, &InstanceReleaseThunk},
        {addr::cGZSoundInstance_QueryInterface, &InstanceQueryInterfaceThunk},
        {addr::cGZSoundInstance_ComputeLevel, &InstanceComputeLevelThunk},
        {addr::cGZSoundInstance_IsPlaying, &InstanceIsPlayingThunk},
        {addr::cGZSoundInstance_IsActive, &InstanceIsActiveThunk},
        {addr::cGZSoundInstance_SetByte58, &InstanceSetByte58Thunk},
        {addr::cGZSoundInstance_SetByte3D, &InstanceSetByte3DThunk},
        {addr::cGZSoundInstance_SetPlace, &InstanceSetPlaceThunk},
        {addr::cGZSoundInstance_SetDistances, &InstanceSetDistancesThunk},
        {addr::cGZSoundInstance_SetDesc, &InstanceSetDescThunk},
        {addr::cGZSoundInstance_Stop, &InstanceStopThunk},
        {addr::cGZSoundInstance_SetLevel, &InstanceSetLevelThunk},
        {addr::cGZSoundInstance_Update, &InstanceUpdateThunk},
        {addr::cGZSoundInstance_Restart, &InstanceRestartThunk},
        {addr::cGZSoundInstance_Start, &InstanceStartThunk},
        {addr::cGZSoundInstance_Place, &InstancePlaceThunk},
        {addr::cGZSoundInstance_Destruct, &InstanceDestructThunk},
        {addr::cGZSoundInstance_DeletingDestructor, &InstanceDeletingDestructorThunk},
        {addr::cGZSoundInstance_HasHexRun, &InstanceHasHexRunThunk},
    };
    return InstallAll(detours);
}

bool InstallDesc(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZSoundDesc_Construct, &DescConstructThunk},
        {addr::cGZSoundDesc_Destruct, &DescDestructThunk},
        {addr::cGZSoundDesc_QueryInterface, &DescQueryInterfaceThunk},
        {addr::cGZSoundDesc_Release, &DescReleaseThunk},
        {addr::cGZSoundDesc_SetName, &DescSetNameThunk},
        {addr::cGZSoundDesc_Set20, &DescSet20Thunk},
        {addr::cGZSoundDesc_Set2C, &DescSet2CThunk},
        {addr::cGZSoundDesc_Set30, &DescSet30Thunk},
        {addr::cGZSoundDesc_Set31, &DescSet31Thunk},
        {addr::cGZSoundDesc_Set34, &DescSet34Thunk},
        {addr::cGZSoundDesc_Set3C, &DescSet3CThunk},
        {addr::cGZSoundDesc_Set44, &DescSet44Thunk},
        {addr::cGZSoundDesc_Set50, &DescSet50Thunk},
        {addr::cGZSoundDesc_Set54, &DescSet54Thunk},
        {addr::cGZSoundDesc_Set58, &DescSet58Thunk},
        {addr::cGZSoundDesc_Set5C, &DescSet5CThunk},
        {addr::cGZSoundDesc_Set60, &DescSet60Thunk},
    };
    return InstallAll(detours);
}

const bool g_registered[] = {
    registry::Register({"framework.sound.director", "framework/sound", registry::Kind::Detour,
                        registry::Phase::Attach, addr::cGZSoundDirector_Construct,
                        "Our framework sound director: its constructor and the static one's getter (2 detours; part "
                        "u5, docs/gzui.md)",
                        false, InstallDirector}),
    registry::Register({"framework.sound.table", "framework/sound", registry::Kind::Detour, registry::Phase::Attach,
                        addr::cGZSoundTable_Get,
                        "Our framework sound table 0x5C1CF5A3: constructor, Get, Release, destructor (4 detours; part "
                        "u5, docs/gzui.md)",
                        false, InstallTable}),
    registry::Register({"framework.sound.instance", "framework/sound", registry::Kind::Detour,
                        registry::Phase::Attach, addr::cGZSoundInstance_Update,
                        "Our framework sound instance 0xBDD9D7A5: constructors, AddRef, Release, QueryInterface, "
                        "destructors, the level, Update, Start, Stop, Place, the setters and the hex digit test (22 "
                        "detours; part u5, docs/gzui.md)",
                        false, InstallInstance}),
    registry::Register({"framework.sound.desc", "framework/sound", registry::Kind::Detour, registry::Phase::Attach,
                        addr::cGZSoundDesc_Construct,
                        "Our framework sound description 0x7DD9C9F2: constructor, QueryInterface, Release, destructor, "
                        "the name and its own setters (17 detours; part u5, docs/gzui.md)",
                        false, InstallDesc}),
};

} // namespace
} // namespace opensc4::gzui
