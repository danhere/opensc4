// Our cSC4BuildingOccupant (build 641; the class map, the vtables slot by
// slot and the fields are in bldgocc_sim.h): the class's own slots in its
// five vtables, the save record's Write and Read, and our own object (the
// constructor, the factory and the pool allocation by detour, the deleting
// destructor and identity from copies of the vtables).
//
// The occupant base (cSC4Occupant: its constructor, destructor,
// QueryInterface, AddRef, Release, Shutdown, Write, Read and message post)
// is every occupant class's and stays the game's: it is called at its
// address.
//
// Every virtual call below has its argument count from the callee's own
// `ret N`:
//   - the streams' vtables 0x00AA2310 (out) and 0x00AA2280 (in): slots
//     0x10, 0x14, 0x1C, 0x24, 0x30 one argument, 0x48 none;
//   - cGZMessage 0x00ACEC00: slots 0x14, 0x30, 0x40, 0x54, 0x68 one
//     argument (0x009132DB, 0x00913322, 0x00913330, 0x0091333E,
//     0x0091334C), 0x84 one (0x0091346B jumps to 0x005447B0, ret 4);
//   - the message server (0x00A8CAFC and 0x00AD1BF4): slot 0x10 two;
//   - this class's holder part 0x00A871B8: slot 0x24 (0x005FEDA0) one,
//     slot 0x30 (0x005FF860) three; its occupant part 0x00A87238: slot 0x1C
//     none, 0x3C two, 0x80 one;
//   - the properties 0x00AA6988 and 0x00AA6BE8: slot 0x14 none; the
//     variants 0x00AA6788, 0x00AA69E8, 0x00ACECD0: slot 0xE0 one
//     (0x009166CF);
//   - the development simulator 0x00AB0AF8: slots 0x94, 0x98, 0x9C one.
#include "core/bldgocc_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::bldgocc_sim {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("bldgocc: first %s", name);
}

namespace {

constexpr uint32_t kPropertyName = 0x899AFBAD;     // the name a player gave the building
constexpr uint32_t kPropertyNameKey = 0x8A416A99;  // the resource key of the building's localized name
constexpr uint32_t kPropertyExemplarName = 0x20;
constexpr uint32_t kMessageChanged = 0x99EF1145;   // occupant message after SetName
constexpr uint32_t kMessageLit = 0x09D8E0B6;       // posted by SetLit

template <typename R, typename... Args>
R CallThis(std::uintptr_t address, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(address)(self, args...);
}

template <typename R, typename... Args>
R CallCdecl(std::uintptr_t address, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(address)(args...);
}

// A new cGZMessage, referenced once. Like the game, no test of the
// allocation before the first call through it.
void* NewMessage()
{
    void* const memory = CallCdecl<void*, std::size_t>(addr::RZ_operator_new, 0x2C);
    return memory ? CallThis<void*>(addr::cGZMessage_cGZMessage, memory) : nullptr;
}

// --- The primary interface (`self` is the object) ---------------------------------

// Slot 0 (0x004587D0): the building interface here, everything else from
// the occupant base (which answers 1 with the object + 4).
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    static bool logged = false;
    FirstCall("QueryInterface", logged);
    if (iid == kIidBuilding) {
        *object = self;
        Virtual<uint32_t>(self, 0x4);
        return 1;
    }
    return CallThis<uint8_t, uint32_t, void**>(addr::cSC4Occupant_QueryInterface, self + field::kOccupant, iid, object);
}

// The other parts' slot 0 (0x004589F0, 0x00458A00, 0x00458A10, 0x00458A30).
template <std::size_t Offset>
uint8_t __fastcall QueryInterfaceAt(char* part, void*, uint32_t iid, void** object)
{
    return QueryInterface(part - Offset, nullptr, iid, object);
}

// Slot 8 (0x00458C60): the box from two corners, copied as they are. EAX
// is left holding the last value moved.
uint32_t __fastcall SetBoundingBox(char* self, void*, const uint32_t* low, const uint32_t* high)
{
    static bool logged = false;
    FirstCall("SetBoundingBox", logged);
    At<uint32_t>(self, field::kMin) = low[0];
    At<uint32_t>(self, field::kMin + 4) = low[1];
    At<uint32_t>(self, field::kMin + 8) = low[2];
    At<uint32_t>(self, field::kMax) = high[0];
    At<uint32_t>(self, field::kMax + 4) = high[1];
    const uint32_t last = high[2];
    At<uint32_t>(self, field::kMax + 8) = last;
    return last;
}

// Slot 9 (0x00458810).
void* __fastcall GetCompletionPercent(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetCompletionPercent", logged);
    return self + field::kCompletion;
}

// Slot 10 (0x00458820): EAX keeps the argument.
const uint32_t* __fastcall SetCompletionPercent(char* self, void*, const uint32_t* percent)
{
    static bool logged = false;
    FirstCall("SetCompletionPercent", logged);
    At<uint32_t>(self, field::kCompletion) = *percent;
    return percent;
}

// The game's function-local static strings (0x00B2147C, 0x00B21494: a
// cRZBaseString of 0x14 bytes, then the guard dword), made on first use as
// the game makes them: the buffer of 8 bytes from the STL allocator, then
// the destructor registered with atexit.
void* EmptyString(std::uintptr_t address, std::uintptr_t atExit)
{
    char* const string = reinterpret_cast<char*>(address);
    uint32_t& guard = At<uint32_t>(string, 0x14);
    if (!(guard & 1)) {
        guard |= 1;
        At<char*>(string, 0x4) = nullptr;
        At<char*>(string, 0x8) = nullptr;
        At<char*>(string, 0xC) = nullptr;
        char* const buffer = CallCdecl<char*, std::size_t>(addr::SC4_stl_allocate, 8);
        At<char*>(string, 0x4) = buffer;
        At<char*>(string, 0x8) = buffer;
        At<char*>(string, 0xC) = buffer + 8;
        buffer[0] = 0;
        At<std::uintptr_t>(string, 0x0) = addr::SC4String_Vtable;
        At<uint32_t>(string, 0x10) = 0;
        CallCdecl<int, std::uintptr_t>(addr::SC4_atexit, atExit);
    }
    return string;
}

// Slot 13 (0x00459050): the development simulator's answer for the
// building type; a static empty string without the simulator.
void* __fastcall GetExemplarName(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetExemplarName", logged);
    if (void* const development = Global<void*>(addr::SC4_Service43D68))
        return Virtual<void*>(development, 0x94, At<uint32_t>(self, field::kBuildingType));
    return EmptyString(addr::cSC4BuildingOccupant_EmptyExemplarName, addr::cSC4BuildingOccupant_EmptyExemplarNameAtExit);
}

// Slot 12 (0x004590E0).
void* __fastcall GetBuildingName(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetBuildingName", logged);
    if (void* const development = Global<void*>(addr::SC4_Service43D68))
        return Virtual<void*>(development, 0x98, At<uint32_t>(self, field::kBuildingType));
    return EmptyString(addr::cSC4BuildingOccupant_EmptyBuildingName, addr::cSC4BuildingOccupant_EmptyBuildingNameAtExit);
}

// Slot 11 (0x00459250): the development simulator's profile of the
// building type; without the simulator the game's static empty profile
// (0x00B214B0, 0x40 bytes, then its guard), cleared on first use as the
// profile's constructor 0x004591E0 does (+0x0B, +0x19 to +0x1B and +0x2C
// are left) and its destructor registered with atexit.
void* __fastcall GetBuildingProfile(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetBuildingProfile", logged);
    if (void* const development = Global<void*>(addr::SC4_Service43D68))
        return Virtual<void*>(development, 0x9C, At<uint32_t>(self, field::kBuildingType));
    char* const profile = reinterpret_cast<char*>(addr::cSC4BuildingOccupant_EmptyProfile);
    uint32_t& guard = At<uint32_t>(profile, 0x40);
    if (!(guard & 1)) {
        guard |= 1;
        At<uint32_t>(profile, 0x0) = 0;
        At<uint32_t>(profile, 0x4) = 0;
        At<uint16_t>(profile, 0x8) = 0;
        At<uint8_t>(profile, 0xA) = 0;
        At<uint32_t>(profile, 0xC) = 0;
        At<uint32_t>(profile, 0x10) = 0;
        At<uint32_t>(profile, 0x14) = 0;
        At<uint8_t>(profile, 0x18) = 0;
        At<uint32_t>(profile, 0x1C) = 0;
        for (std::size_t offset = 0x30; offset <= 0x3C; offset += 4)
            At<uint32_t>(profile, offset) = 0;
        At<uint32_t>(profile, 0x28) = 0;
        At<uint32_t>(profile, 0x24) = 0;
        At<uint32_t>(profile, 0x20) = 0;
        CallCdecl<int, std::uintptr_t>(addr::SC4_atexit, addr::cSC4BuildingOccupant_EmptyProfileAtExit);
    }
    return profile;
}

// Slot 14 (0x00458830): the byte, sign extended.
int32_t __fastcall GetOrientation(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetOrientation", logged);
    return At<int8_t>(self, field::kOrientation);
}

// Slot 15 (0x00458840): clamped to 0-3.
uint32_t __fastcall SetOrientation(char* self, void*, int32_t orientation)
{
    static bool logged = false;
    FirstCall("SetOrientation", logged);
    At<int8_t>(self, field::kOrientation) = static_cast<int8_t>(orientation < 0 ? 0 : orientation > 3 ? 3 : orientation);
    return 1;
}

// Slot 16 (0x00458870).
uint32_t __fastcall IsLit(char* self, void*)
{
    static bool logged = false;
    FirstCall("IsLit", logged);
    return At<uint8_t>(self, field::kLit) & 1u;
}

// Slot 17 (0x00458C90): when the bit differs it is flipped and message
// 0x09D8E0B6 with the occupant goes to the message server. No return value
// in the game (EAX is whatever the last call left; the byte when nothing
// changed).
uint32_t __fastcall SetLit(char* self, void*, uint32_t lit)
{
    static bool logged = false;
    FirstCall("SetLit", logged);
    const uint8_t state = At<uint8_t>(self, field::kLit);
    if (static_cast<uint8_t>(lit) == (state & 1))
        return state;
    At<uint8_t>(self, field::kLit) = state ^ 1;
    void* const message = NewMessage();
    Virtual<uint32_t>(message, 0x4);
    Virtual<void>(message, 0x14, kMessageLit);
    void* const occupant = Virtual<void*>(self, slot::kAsOccupant);
    Virtual<void*>(message, 0x84, occupant);
    Virtual<uint8_t>(Global<void*>(addr::SC4_MessageServer), 0x10, message, 0u);
    return Virtual<uint32_t>(message, 0x8);
}

// Slot 18 (0x00458880): the name property's value replaced, or the
// property added (holder slot 12: ID, string, 0), then occupant message
// 0x99EF1145 through the occupant part's slot 15, whose EAX is returned.
uint32_t __fastcall SetName(char* self, void*, void* name)
{
    static bool logged = false;
    FirstCall("SetName", logged);
    char* const holder = self + field::kHolder;
    if (void* const property = Virtual<void*>(holder, 0x24, kPropertyName)) {
        void* const value = Virtual<void*>(property, 0x14);
        Virtual<void>(value, 0xE0, name);
    } else {
        Virtual<uint8_t>(holder, 0x30, kPropertyName, name, 0u);
    }
    return Virtual<uint32_t>(self + field::kOccupant, occupant::kPostOccupantMessage, kMessageChanged, 0u);
}

// Slot 19 (0x004588F0): the player's name, else the localized name the
// key property leads to, else the exemplar name property.
uint32_t __fastcall GetName(char* self, void*, void* name)
{
    static bool logged = false;
    FirstCall("GetName", logged);
    char* const holder = self ? self + field::kHolder : nullptr;
    if (CallCdecl<uint8_t, void*, uint32_t, void*>(addr::SC4_GetPropertyString, holder, kPropertyName, name))
        return 1;
    if (CallCdecl<uint8_t, void*, void*, uint32_t, uint32_t>(addr::SC4_GetPropertyText, name, holder, kPropertyNameKey, 0u))
        return 1;
    return CallCdecl<uint8_t, void*, uint32_t, void*>(addr::SC4_GetPropertyString, holder, kPropertyExemplarName, name) != 0;
}

// --- The occupant part (`part` is the object + 4) ---------------------------------

// Slot 4 (0x00458800): a jump to the base's Shutdown.
uint8_t __fastcall Shutdown(char* part, void*)
{
    static bool logged = false;
    FirstCall("Shutdown", logged);
    return CallThis<uint8_t>(addr::cSC4Occupant_Shutdown, part);
}

// Slot 5 (0x00458A40).
uint32_t __fastcall IsInitialized(char* part, void*)
{
    static bool logged = false;
    FirstCall("IsInitialized", logged);
    return At<uint8_t>(part - field::kOccupant, field::kState) & 1u;
}

// Slot 16 (0x00458A50).
uint32_t __fastcall GetHighlight(char* part, void*)
{
    static bool logged = false;
    FirstCall("GetHighlight", logged);
    return static_cast<uint32_t>(At<uint8_t>(part - field::kOccupant, field::kState)) >> 4;
}

// Slot 8 (0x00458BA0): the box's centre in x and z (the sums on the FPU,
// times 0.5, stored as floats) and its low y. EAX keeps the argument.
float* __fastcall GetPosition(char* part, void*, float* position)
{
    static bool logged = false;
    FirstCall("GetPosition", logged);
    char* const self = part - field::kOccupant;
    position[0] = static_cast<float>(
        (static_cast<double>(At<float>(self, field::kMax)) + static_cast<double>(At<float>(self, field::kMin))) * 0.5);
    std::memcpy(&position[1], self + field::kMin + 4, 4);
    position[2] = static_cast<float>(
        (static_cast<double>(At<float>(self, field::kMax + 8)) + static_cast<double>(At<float>(self, field::kMin + 8))) *
        0.5);
    return position;
}

// Slot 9 (0x00458BD0): the box moved so that the position is its centre in
// x and z and its low y, keeping its size. The game keeps half the width
// and the height on the FPU and stores half the depth as a float before
// using it; the reads of the position and the stores into the box are in
// its order. EAX keeps the argument.
const float* __fastcall SetPosition(char* part, void*, const float* position)
{
    static bool logged = false;
    FirstCall("SetPosition", logged);
    char* const self = part - field::kOccupant;
    const double halfX =
        (static_cast<double>(At<float>(self, field::kMax)) - static_cast<double>(At<float>(self, field::kMin))) * 0.5;
    const double height =
        static_cast<double>(At<float>(self, field::kMax + 4)) - static_cast<double>(At<float>(self, field::kMin + 4));
    const float halfZ = static_cast<float>(
        (static_cast<double>(At<float>(self, field::kMax + 8)) - static_cast<double>(At<float>(self, field::kMin + 8))) *
        0.5);
    At<float>(self, field::kMin) = static_cast<float>(static_cast<double>(position[0]) - halfX);
    At<float>(self, field::kMax) = static_cast<float>(halfX + static_cast<double>(position[0]));
    uint32_t y = 0;
    std::memcpy(&y, &position[1], 4);
    At<uint32_t>(self, field::kMin + 4) = y;
    At<float>(self, field::kMax + 4) = static_cast<float>(height + static_cast<double>(position[1]));
    At<float>(self, field::kMin + 8) = static_cast<float>(static_cast<double>(position[2]) - static_cast<double>(halfZ));
    At<float>(self, field::kMax + 8) = static_cast<float>(static_cast<double>(halfZ) + static_cast<double>(position[2]));
    return position;
}

// Slot 10 (0x00458C30): EAX is left holding the second pointer.
uint32_t* __fastcall GetBoundingBox(char* part, void*, uint32_t* low, uint32_t* high)
{
    static bool logged = false;
    FirstCall("GetBoundingBox", logged);
    char* const self = part - field::kOccupant;
    low[0] = At<uint32_t>(self, field::kMin);
    low[1] = At<uint32_t>(self, field::kMin + 4);
    low[2] = At<uint32_t>(self, field::kMin + 8);
    high[0] = At<uint32_t>(self, field::kMax);
    high[1] = At<uint32_t>(self, field::kMax + 4);
    high[2] = At<uint32_t>(self, field::kMax + 8);
    return high;
}

// Slot 15 (0x00458D10): the base's post, then for the messages 0x99EF1142
// and 0x99EF1143 a second message, 0x8B8B2CED or 0x8B8B2CEE, with the
// occupant (as void 1 and as the object), its flag 4, its type and the
// data, through the occupants' cached message server. No return value in
// the game: EAX is what the last call left.
uint32_t __fastcall PostOccupantMessage(char* part, void*, uint32_t id, uint32_t data)
{
    static bool logged = false;
    FirstCall("PostOccupantMessage", logged);
    const uint32_t result = CallThis<uint32_t, uint32_t, uint32_t>(addr::cSC4Occupant_PostOccupantMessage, part, id, data);
    if (id != 0x99EF1142 && id != 0x99EF1143)
        return result;
    void*& server = Global<void*>(addr::cSC4Occupant_MessageServer);
    if (!server)
        server = Global<void*>(addr::SC4_MessageServer);
    void* const message = NewMessage();
    if (message)
        Virtual<uint32_t>(message, 0x4);
    Virtual<void>(message, 0x14, id != 0x99EF1142 ? 0x8B8B2CEEu : 0x8B8B2CEDu);
    // The object as its occupant interface: null for a null object.
    char* const self = part - field::kOccupant;
    void* const occupant = self ? part : nullptr;
    Virtual<void>(message, 0x30, occupant);
    Virtual<void*>(message, 0x84, occupant);
    const uint8_t flagged = Virtual<uint8_t>(part, occupant::kIsFlagSet, 4u);
    Virtual<void>(message, 0x40, uint32_t{flagged});
    const uint32_t type = Virtual<uint32_t>(part, occupant::kGetType);
    Virtual<void>(message, 0x54, type);
    Virtual<void>(message, 0x68, data);
    Virtual<uint8_t>(Global<void*>(addr::cSC4Occupant_MessageServer), 0x10, message, 0u);
    return Virtual<uint32_t>(message, 0x8);
}

// --- The other parts ----------------------------------------------------------------

// +0x8 slot 5 (0x004587C0).
uint32_t __fastcall GetGZCLSID(void*, void*)
{
    static bool logged = false;
    FirstCall("GetGZCLSID", logged);
    return kClassId;
}

// +0x20 slot 3 (0x00458E60; `part` = object + 0x20): the exemplar pointer
// at part + 4 exchanged: the new one referenced first, the old released
// after the store.
void __fastcall SetPropertyExemplar(char* part, void*, void* exemplar)
{
    static bool logged = false;
    FirstCall("SetPropertyExemplar", logged);
    void* const old = At<void*>(part, 0x4);
    if (exemplar == old)
        return;
    if (exemplar)
        Virtual<uint32_t>(exemplar, 0x4);
    At<void*>(part, 0x4) = exemplar;
    if (old)
        Virtual<uint32_t>(old, 0x8);
}

// --- Save ---------------------------------------------------------------------------

// +0x8 slot 3 (0x00458A90; `part` = object + 8): version 2, the occupant
// base's record, then the building type, the lit byte, the box, the
// orientation and the completion. The layout is in
// tools/re/save_schemas/bldgocc.py. False when the base fails, else when
// the stream is in error after.
uint32_t __fastcall Write(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    char* const self = part - field::kSerializable;
    Virtual<uint8_t>(stream, 0x1C, 2u);
    if (!CallThis<uint8_t, void*>(addr::cSC4Occupant_Write, part, stream))
        return 0;
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, field::kBuildingType));
    Virtual<uint8_t>(stream, 0x14, uint32_t{At<uint8_t>(self, field::kLit)});
    CallCdecl<void*, void*, void*>(addr::SC4Stream_WriteVector3, stream, self + field::kMin);
    CallCdecl<void*, void*, void*>(addr::SC4Stream_WriteVector3, stream, self + field::kMax);
    Virtual<uint8_t>(stream, 0x10, uint32_t{At<uint8_t>(self, field::kOrientation)});
    Virtual<uint8_t>(stream, 0x30, At<uint32_t>(self, field::kCompletion));
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// +0x8 slot 4 (0x00458B10; `part` = object + 8): version 2 only. The game
// reads the version into its argument's stack slot (the stream pointer
// until the stream writes two bytes of it) and the completion into the
// slot of a pushed ECX (this part's address until the stream writes it);
// both start with those values here.
uint32_t __fastcall Read(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Read", logged);
    char* const self = part - field::kSerializable;
    uint32_t version = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(stream));
    Virtual<uint8_t>(stream, 0x1C, &version);
    if (static_cast<uint16_t>(version) != 2)
        return 0;
    if (!CallThis<uint8_t, void*>(addr::cSC4Occupant_Read, part, stream))
        return 0;
    Virtual<uint8_t>(stream, 0x24, self + field::kBuildingType);
    Virtual<uint8_t>(stream, 0x14, self + field::kLit);
    CallCdecl<void*, void*, void*>(addr::SC4Stream_ReadVector3, stream, self + field::kMin);
    CallCdecl<void*, void*, void*>(addr::SC4Stream_ReadVector3, stream, self + field::kMax);
    Virtual<uint8_t>(stream, 0x10, self + field::kOrientation);
    uint32_t completion = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(part));
    Virtual<uint8_t>(stream, 0x30, &completion);
    At<uint32_t>(self, field::kCompletion) = completion;
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// The holder part's slots 23 and 24 (0x00458E40, 0x00458E50): this - 4.
uint32_t __fastcall WriteThunk(char* holder, void*, void* stream) { return Write(holder - 0x4, nullptr, stream); }
uint32_t __fastcall ReadThunk(char* holder, void*, void* stream) { return Read(holder - 0x4, nullptr, stream); }

// --- Installation -------------------------------------------------------------------

struct Slot {
    std::uintptr_t vtable;
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(vtable, index, original, fn) {vtable, index, original, reinterpret_cast<void*>(&fn)}

const Slot kSlots[] = {
    OPENSC4_SLOT(addr::cSC4BuildingOccupant_vftable, 0, addr::cSC4BuildingOccupant_QueryInterface, QueryInterface),
    OPENSC4_SLOT(addr::cSC4BuildingOccupant_vftable, 8, addr::cSC4BuildingOccupant_SetBoundingBox, SetBoundingBox),
    OPENSC4_SLOT(addr::cSC4BuildingOccupant_vftable, 9, addr::cSC4BuildingOccupant_GetCompletionPercent,
                 GetCompletionPercent),
    OPENSC4_SLOT(addr::cSC4BuildingOccupant_vftable, 10, addr::cSC4BuildingOccupant_SetCompletionPercent,
                 SetCompletionPercent),
    OPENSC4_SLOT(addr::cSC4BuildingOccupant_vftable, 11, addr::cSC4BuildingOccupant_GetBuildingProfile,
                 GetBuildingProfile),
    OPENSC4_SLOT(addr::cSC4BuildingOccupant_vftable, 12, addr::cSC4BuildingOccupant_GetBuildingName, GetBuildingName),
    OPENSC4_SLOT(addr::cSC4BuildingOccupant_vftable, 13, addr::cSC4BuildingOccupant_GetExemplarName, GetExemplarName),
    OPENSC4_SLOT(addr::cSC4BuildingOccupant_vftable, 14, addr::cSC4BuildingOccupant_GetOrientation, GetOrientation),
    OPENSC4_SLOT(addr::cSC4BuildingOccupant_vftable, 15, addr::cSC4BuildingOccupant_SetOrientation, SetOrientation),
    OPENSC4_SLOT(addr::cSC4BuildingOccupant_vftable, 16, addr::cSC4BuildingOccupant_IsLit, IsLit),
    OPENSC4_SLOT(addr::cSC4BuildingOccupant_vftable, 17, addr::cSC4BuildingOccupant_SetLit, SetLit),
    OPENSC4_SLOT(addr::cSC4BuildingOccupant_vftable, 18, addr::cSC4BuildingOccupant_SetName, SetName),
    OPENSC4_SLOT(addr::cSC4BuildingOccupant_vftable, 19, addr::cSC4BuildingOccupant_GetName, GetName),
    OPENSC4_SLOT(addr::cSC4BuildingOccupant_vftable_4, 0, addr::cSC4BuildingOccupant_QueryInterfaceThunk4,
                 QueryInterfaceAt<0x4>),
    OPENSC4_SLOT(addr::cSC4BuildingOccupant_vftable_4, 4, addr::cSC4BuildingOccupant_ShutdownThunk, Shutdown),
    OPENSC4_SLOT(addr::cSC4BuildingOccupant_vftable_4, 5, addr::cSC4BuildingOccupant_IsInitialized, IsInitialized),
    OPENSC4_SLOT(addr::cSC4BuildingOccupant_vftable_4, 8, addr::cSC4BuildingOccupant_GetPosition, GetPosition),
    OPENSC4_SLOT(addr::cSC4BuildingOccupant_vftable_4, 9, addr::cSC4BuildingOccupant_SetPosition, SetPosition),
    OPENSC4_SLOT(addr::cSC4BuildingOccupant_vftable_4, 10, addr::cSC4BuildingOccupant_GetBoundingBox, GetBoundingBox),
    OPENSC4_SLOT(addr::cSC4BuildingOccupant_vftable_4, 15, addr::cSC4BuildingOccupant_PostOccupantMessage,
                 PostOccupantMessage),
    OPENSC4_SLOT(addr::cSC4BuildingOccupant_vftable_4, 16, addr::cSC4BuildingOccupant_GetHighlight, GetHighlight),
    OPENSC4_SLOT(addr::cSC4BuildingOccupant_vftable_8, 0, addr::cSC4BuildingOccupant_QueryInterfaceThunk8,
                 QueryInterfaceAt<0x8>),
    OPENSC4_SLOT(addr::cSC4BuildingOccupant_vftable_8, 5, addr::cSC4BuildingOccupant_GetGZCLSID, GetGZCLSID),
    OPENSC4_SLOT(addr::cSC4BuildingOccupant_vftable_C, 0, addr::cSC4BuildingOccupant_QueryInterfaceThunkC,
                 QueryInterfaceAt<0xC>),
    OPENSC4_SLOT(addr::cSC4BuildingOccupant_vftable_20, 0, addr::cSC4BuildingOccupant_QueryInterfaceThunk20,
                 QueryInterfaceAt<0x20>),
    OPENSC4_SLOT(addr::cSC4BuildingOccupant_vftable_20, 3, addr::cSC4BuildingOccupant_SetPropertyExemplar,
                 SetPropertyExemplar),
};
const Slot kSaveSlots[] = {
    OPENSC4_SLOT(addr::cSC4BuildingOccupant_vftable_8, 3, addr::cSC4BuildingOccupant_Write, Write),
    OPENSC4_SLOT(addr::cSC4BuildingOccupant_vftable_8, 4, addr::cSC4BuildingOccupant_Read, Read),
    OPENSC4_SLOT(addr::cSC4BuildingOccupant_vftable_C, 23, addr::cSC4BuildingOccupant_WriteThunk, WriteThunk),
    OPENSC4_SLOT(addr::cSC4BuildingOccupant_vftable_C, 24, addr::cSC4BuildingOccupant_ReadThunk, ReadThunk),
};

#undef OPENSC4_SLOT

template <std::size_t N>
bool SwapSlots(const Slot (&slots)[N], const char* what)
{
    if (!game::IsBuild641())
        return false;
    for (const Slot& slot : slots) {
        const auto* const vtable = reinterpret_cast<const std::uintptr_t*>(slot.vtable);
        if (vtable[slot.index] != slot.original) {
            log::Write("bldgocc %s: slot %d of %08X holds %08X, expected %08X; not installing", what, slot.index,
                       static_cast<unsigned>(slot.vtable), static_cast<unsigned>(vtable[slot.index]),
                       static_cast<unsigned>(slot.original));
            return false;
        }
    }
    for (const Slot& slot : slots) {
        auto* const entry = reinterpret_cast<std::uintptr_t*>(slot.vtable) + slot.index;
        DWORD old = 0;
        if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &old))
            return false;
        *entry = reinterpret_cast<std::uintptr_t>(slot.ours);
        VirtualProtect(entry, sizeof(*entry), old, &old);
    }
    log::Write("bldgocc %s: %u slots are ours", what, static_cast<unsigned>(N));
    return true;
}

// No function of the slots or of the save record is called directly from
// outside the class (the QueryInterface thunks jump to 0x004587D0 and the
// save thunks to Write and Read inside it; the class registration's call
// of GetGZCLSID at 0x00460694 runs before any replacement is installed),
// so these two groups are slot swaps only.
bool InstallSlots(registry::Mode) { return SwapSlots(kSlots, "slots"); }
bool InstallSave(registry::Mode) { return SwapSlots(kSaveSlots, "save"); }

const bool g_registered[] = {
    registry::Register({"simulators.bldgocc.slots", "simulators/bldgocc", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4BuildingOccupant_vftable,
                        "Our cSC4BuildingOccupant slots: 13 of the primary vtable, 8 of the occupant part, the parts' "
                        "QueryInterface thunks, GetGZCLSID and SetPropertyExemplar (src/core/bldgocc_sim.h)",
                        false, InstallSlots}),
    registry::Register({"simulators.bldgocc.save", "simulators/bldgocc", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4BuildingOccupant_vftable_8,
                        "Our cSC4BuildingOccupant Write and Read (the save record, version 2; src/core/bldgocc_sim.h)",
                        false, InstallSave}),
};

// --- Our object ---------------------------------------------------------------------

// Copies of the game's five vtables, taken at the first construction, after
// every PostAppInit slot swap of the other groups.
constexpr int kSlots0 = 20, kSlots4 = 34, kSlots8 = 6, kSlotsC = 25, kSlots20 = 5;
void* g_vtable[kSlots0];
void* g_vtable4[kSlots4];
void* g_vtable8[kSlots8];
void* g_vtableC[kSlotsC];
void* g_vtable20[kSlots20];

void SetVtables(char* self)
{
    At<void**>(self, 0x0) = g_vtable;
    At<void**>(self, field::kOccupant) = g_vtable4;
    At<void**>(self, field::kSerializable) = g_vtable8;
    At<void**>(self, field::kHolder) = g_vtableC;
    At<void**>(self, field::kExemplarPart) = g_vtable20;
}

// An item of the building pool (0x00458A60, and inline in the factory):
// the current page's free head, the pool refilled when the page has none
// (0x0090CF9A returns the new head); the page is read again after.
void* PoolAllocate()
{
    char* const pool = reinterpret_cast<char*>(addr::cSC4BuildingOccupant_Pool);
    void* item = At<void*>(At<char*>(pool, 0x4), 0x8);
    if (!item)
        item = CallThis<void*>(addr::SC4EdgePool_Grow, pool);
    At<void*>(At<char*>(pool, 0x4), 0x8) = *static_cast<void**>(item);
    ++At<uint32_t>(At<char*>(pool, 0x4), 0xC);
    return item;
}

// The deleting destructor (0x00458F80): the class's vtables, the occupant
// base destroyed, and with flag 1 the memory back to the pool (the page
// header is at the address with its low 13 bits clear).
void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("DeletingDestructor", logged);
    SetVtables(self);
    CallThis<void>(addr::cSC4Occupant_Destroy, self + field::kOccupant);
    if (flags & 1) {
        char* const page = reinterpret_cast<char*>(reinterpret_cast<std::uintptr_t>(self) & 0xFFFFE000u);
        At<void*>(self, 0x0) = At<void*>(page, 0x8);
        const uint32_t used = At<uint32_t>(page, 0xC) - 1;
        At<void*>(page, 0x8) = self;
        At<uint32_t>(page, 0xC) = used;
        void* const pool = reinterpret_cast<void*>(addr::cSC4BuildingOccupant_Pool);
        if (used == 0)
            CallThis<void, void*>(addr::SC4Pool_ReleaseBlock, pool, page);
        else if (used == 0x40000042)
            CallThis<void, void*>(addr::SC4Pool_ReopenBlock, pool, page);
    }
    return self;
}

// The holder part's slot 22 (0x00458A20).
void* __fastcall DeletingDestructorAtHolder(char* part, void*, uint32_t flags)
{
    return DeletingDestructor(part - field::kHolder, nullptr, flags);
}

void BuildVtables()
{
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4BuildingOccupant_vftable), sizeof(g_vtable));
    std::memcpy(g_vtable4, reinterpret_cast<const void*>(addr::cSC4BuildingOccupant_vftable_4), sizeof(g_vtable4));
    std::memcpy(g_vtable8, reinterpret_cast<const void*>(addr::cSC4BuildingOccupant_vftable_8), sizeof(g_vtable8));
    std::memcpy(g_vtableC, reinterpret_cast<const void*>(addr::cSC4BuildingOccupant_vftable_C), sizeof(g_vtableC));
    std::memcpy(g_vtable20, reinterpret_cast<const void*>(addr::cSC4BuildingOccupant_vftable_20), sizeof(g_vtable20));
    g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
    g_vtable4[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x4>);
    g_vtable8[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x8>);
    g_vtable8[5] = reinterpret_cast<void*>(&GetGZCLSID);
    g_vtableC[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0xC>);
    g_vtableC[22] = reinterpret_cast<void*>(&DeletingDestructorAtHolder);
    g_vtable20[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x20>);
    log::Write("bldgocc object: vtables copied (slots 9/17 %p %p, +4 slots 9/15 %p %p, +8 slots 3/4 %p %p)", g_vtable[9],
               g_vtable[17], g_vtable4[9], g_vtable4[15], g_vtable8[3], g_vtable8[4]);
}

// The constructor (detour of 0x00458E90): the occupant base constructed at
// its address on the object + 4, our vtables, the fields zeroed, the
// completion from the game's default (0x00B07F18, 1.0) and the occupant
// type. +0x56 and +0x57 are left as they are.
void* __fastcall Construct(char* self, void*)
{
    static bool logged = false;
    FirstCall("Construct", logged);
    static bool built = false;
    if (!built) {
        built = true;
        BuildVtables();
    }
    CallThis<void*>(addr::cSC4Occupant_cSC4Occupant, self + field::kOccupant);
    At<uint32_t>(self, field::kBuildingType) = 0;
    SetVtables(self);
    for (std::size_t offset = field::kMin; offset < field::kOrientation; offset += 4)
        At<uint32_t>(self, offset) = 0;
    At<uint8_t>(self, field::kOrientation) = 0;
    At<uint8_t>(self, field::kLit) = 0;
    At<uint32_t>(self, field::kCompletion) = Global<uint32_t>(addr::cSC4BuildingOccupant_DefaultCompletion);
    At<uint32_t>(self, field::kType) = kOccupantType;
    static int created = 0;
    if (created++ < 3)
        log::Write("bldgocc object: constructed %p", static_cast<void*>(self));
    return self;
}

// The pool allocator (detour of 0x00458A60; the size is not used).
void* __cdecl Allocate(std::size_t)
{
    static bool logged = false;
    FirstCall("Allocate", logged);
    return PoolAllocate();
}

// The factory (detour of 0x00458F40): an item of the pool, constructed.
void* __cdecl Create()
{
    static bool logged = false;
    FirstCall("Create", logged);
    return Construct(static_cast<char*>(PoolAllocate()), nullptr);
}

using ConstructFn = void*(__fastcall*)(char*, void*);
using AllocateFn = void*(__cdecl*)(std::size_t);
using CreateFn = void*(__cdecl*)();
ConstructFn g_originalConstruct = nullptr;
AllocateFn g_originalAllocate = nullptr;
CreateFn g_originalCreate = nullptr;

// Every construction path: the factory (called through the class
// registration's pointer when a save is read) and the development
// simulator's 0x00698E1F / 0x00698E2D, which reads only EAX after the
// allocator (add esp, test eax) and after the constructor (mov esi, eax)
// and keeps nothing in ECX or EDX across them; our own development code
// calls both at their addresses.
bool ApplyObject(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    const addr::Entry* const construct = addr_check::Find(addr::cSC4BuildingOccupant_cSC4BuildingOccupant);
    const addr::Entry* const allocate = addr_check::Find(addr::cSC4BuildingOccupant_Allocate);
    const addr::Entry* const create = addr_check::Find(addr::cSC4BuildingOccupant_Create);
    return construct && allocate && create && hook::Install(*construct, &Construct, &g_originalConstruct) &&
           hook::Install(*allocate, &Allocate, &g_originalAllocate) &&
           hook::Install(*create, &Create, &g_originalCreate);
}

const bool g_registeredObject = registry::Register({
    "simulators.bldgocc.object",
    "simulators/bldgocc",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4BuildingOccupant_cSC4BuildingOccupant,
    "Our cSC4BuildingOccupant object: the constructor, the factory and the pool allocation (detours), the deleting "
    "destructor, identity; methods from copies of the (patched) game vtables taken once (src/core/bldgocc_sim.h)",
    false,
    ApplyObject,
});

} // namespace
} // namespace opensc4::bldgocc_sim
