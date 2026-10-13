// Our pre-built-model network occupant (build 641; the class map, the
// vtables and the fields are in src/core/prebuiltocc_sim.h): the class's own
// state, query, save and life functions, each a detour at the game's
// address, because the thunks of the other vtable parts jump to them, the
// bridge and tunnel classes hold or call the same code and makers call the
// set-up, the pylon setter, the constructor and the pool allocation. The
// network occupant's functions are called at their addresses, with the
// argument counts below. The drawing stays the game's: GetModelInstances
// 0x0061E8A0, SetTranslucency 0x0061D540, the bounding box 0x0061DB50, the
// draw 0x0061E860, the effects' refresh 0x0061E340 and the four small
// functions that end in it (docs/prebuiltocc.md has the evidence).
//
// Every function was written from the disassembly. The detours are plain
// functions, so they clobber ECX and EDX. The game's functions that leave
// one of them alone: SetSupportPylon 0x0061D340 keeps ECX;
// IsSupportPylonPresent 0x0061D040 touches only EAX when there is a pylon;
// QueryInterface 0x0061D060 keeps ECX up to its AddRef or its tail jump; the
// free 0x0061E300 touches only ECX for a null item and the pool's free
// 0x0061E7E0 keeps ECX (the pool) for its tail jumps. `self` is the
// object's start (the interface part) unless a function says `part` (Write
// and Read get the cIGZSerializable part, the object + 8; the occupant
// part's Init the object + 4) or `pool`.
//
// Calls, each count from the asm's pushes for that call and the callee's own
// `ret N`:
//   - the streams (out vtable 0x00AA2310, in vtable 0x00AA2280): 0x14
//     SetUint8/GetUint8, 0x1C uint16, 0x24 uint32, 0x30 float one argument
//     each, 0x48 GetError none;
//   - the object's own slots: 0x4 AddRef none (0x0065A860), 0x20
//     HasNetworkFlag one (0x00604D10, ret 4), 0x60 GetRotation and 0x64
//     GetFlip none (0x00604DC0, 0x00604DD0), 0xF4 SetLit one (0x0061F4A0,
//     ret 4), 0x128 SetRotationAndFlip one (0x00604DF0, ret 4), 0x12C the
//     position one (0x006193D0, ret 4), 0x140 the bounding box two
//     (0x0061DB50, ret 8);
//   - the occupant part (object + 4): 0x14 IsInitialized none (0x00458A40),
//     0x20 the position one (0x00604E40, which forwards to slot 0x12C);
//   - the exemplar part (object + 0x20): 0xC set the exemplar one
//     (0x00458E60, ret 4), 0x10 the exemplar none (0x0060CE80);
//   - the holder (object + 0xC): 0x3C add a property two (0x005FF6D0, ret
//     8: the property and 0);
//   - the exemplar (cSCResExemplar, vtable 0x00AAF0D0): 0x1C its property
//     holder none, 0x2C its resource key one (0x0067CD20: writes 12 bytes);
//     the exemplar's holder: 0xC HasProperty one;
//   - a property (0x1C bytes, vtable 0x00AA6988): 0x4 and 0x8 none, 0x1C
//     set the value one (a script number);
//   - the network manager (0x00602680): 0x3C one; the resource manager
//     (global 0x00B45254): 0xC five; the path resource: 0x40 none, 0x60
//     none, 0x64 one, 0x8 none; the resource the 0x60 call answered: 0x44
//     one; the path information: 0x74 four, 0x68, 0xA0 and 0x6C one, 0x8
//     none (all as in src/core/netocc_geometry.cpp's GetPathInfo);
//   - the object at 0x00B43DDC: 0x24 none (its answer is handed to SetLit);
//   - a model instance: 0x8 Release none.
// Direct callees: the network occupant's constructor 0x0061BDD0 (one: the
// flag, ret 4), QueryInterface 0x006192C0 (two, ret 8), Write 0x0061A190 and
// Read 0x0061CC00 (one, ret 4), occupant Init 0x0061CB50 (none),
// IsSupportPylonPresent 0x006199C0 (none); the occupant base's Shutdown
// 0x005E8980 (none; the game jumps to the thunk 0x00619100, which adds 4 to
// ECX and jumps there); this class's destructor 0x00604CA0 (none, a tail
// jump to the network occupant's), planes 0x0061D230 (none), constructor
// 0x0061E1A0 (one, ret 4) and free 0x0061E300 (cdecl, one); the property's
// allocation 0x005FD840 (cdecl, one) and constructor 0x005FDE50 (one, ret
// 4); the script number from a bool 0x00915E8E (one, ret 4) and its
// destructor 0x00915EB5 (none); four words from four pointers 0x00466590
// (four, ret 0x10); the network manager's getter 0x00602680 (cdecl, none);
// the page allocator 0x0090D8E0 (cdecl, one), the pool's Grow 0x0090CF9A
// (none), ReleaseBlock 0x0090D031 and ReopenBlock 0x0090CFF9 (one each, ret
// 4).
#include "core/prebuiltocc_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace opensc4::prebuiltocc_sim {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("prebuiltocc: first %s", name);
}

namespace {

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

// The result of the stream's GetError, as the game turns it into the
// function's answer (neg al, sbb eax, eax, inc eax): 1 for no error.
uint32_t NoError(void* stream)
{
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// A float's bits and back (words that the game copies stay words).
float AsFloat(uint32_t bits)
{
    float value = 0;
    std::memcpy(&value, &bits, 4);
    return value;
}

uint32_t AsBits(float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, 4);
    return bits;
}

// What the game's fld, fchs, fstp of a float leaves.
float Negated(float value)
{
    return static_cast<float>(-static_cast<double>(value));
}

// ====================================================================================
// simulators.prebuiltocc.slots: QueryInterface, the pylon, the set-up, Shutdown and Init
// ====================================================================================

// Slot 0 (0x0061D060, ret 8; the other parts' thunks take off their offset
// and jump here, and the bridge's and the tunnel's QueryInterface end here):
// the class's IID (its class ID) gives the object itself, referenced
// through slot 1; every other ID goes to the network occupant's
// QueryInterface by a tail jump. The game's answer in the first case is
// AL = 1 and the rest of EAX from AddRef.
uint32_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    static bool logged = false;
    FirstCall("QueryInterface", logged);
    if (iid == kIidPrebuilt) {
        *object = self;
        const uint32_t count = Virtual<uint32_t>(self, 0x4);
        return (count & 0xFFFFFF00u) | 1u;
    }
    return CallThis<uint32_t, uint32_t, void**>(addr::cSC4NetworkOccupant_QueryInterface, self, iid, object);
}

// Slot 73 (0x0061D040): 1 with a pylon model (+0x19C not zero; only EAX is
// touched then), else whether the network occupant's own answer (flag
// 0x40000000) is not zero; 0 or 1 in all of EAX.
uint32_t __fastcall IsSupportPylonPresent(char* self, void*)
{
    static bool logged = false;
    FirstCall("IsSupportPylonPresent", logged);
    if (At<uint32_t>(self, field::kPylon) != 0)
        return 1;
    if (CallThis<uint8_t>(addr::cSC4NetworkOccupant_IsSupportPylonPresent, self))
        return 1;
    return 0;
}

// 0x0061D340 (ret 0x14; a maker calls it at 0x00628252): the support
// pylon's model, its angle and where it stands (x, y, z), the five words
// stored as they come. ECX is not touched; EAX is the last argument.
uint32_t __fastcall SetSupportPylon(char* self, void*, uint32_t model, uint32_t angle, uint32_t x, uint32_t y,
                                    uint32_t z)
{
    static bool logged = false;
    FirstCall("SetSupportPylon", logged);
    At<uint32_t>(self, field::kPylon) = model;
    At<uint32_t>(self, field::kPylonAngle) = angle;
    At<uint32_t>(self, field::kPylonOffset) = x;
    At<uint32_t>(self, field::kPylonOffset + 4) = y;
    At<uint32_t>(self, field::kPylonOffset + 8) = z;
    return z;
}

// 0x0061D120 (ret 0xC; the makers call it at 0x00612C94, 0x006248A8 and
// 0x006282E7): the exemplar given to the exemplar part (its slot 3), the
// rotation and flip byte to slot 74, the three words of the model's offset
// copied (read after those two calls), and the box worked out by slot 80
// into +0x124 and +0x130. Then, with an exemplar whose property holder has
// no property 0x6A845768: a new property 0x4835C6A6 holding a true (a
// script number made on the stack by 0x00915E8E, given to the property's
// slot 0x1C and destroyed) is added to the object's own holder (its slot
// 0x3C, with 0) and released. A failed allocation or construction adds
// nothing. The game's EAX is whatever the last call left.
void __fastcall SetUp(char* self, void*, void* exemplar, const uint32_t* offset, uint32_t rotationFlip)
{
    static bool logged = false;
    FirstCall("SetUp", logged);
    Virtual<uint32_t>(self + field::kExemplarPart, 0xC, exemplar);
    Virtual<uint32_t>(self, slot::kSetRotationAndFlip, rotationFlip);
    At<uint32_t>(self, field::kModelOffset) = offset[0];
    At<uint32_t>(self, field::kModelOffset + 4) = offset[1];
    At<uint32_t>(self, field::kModelOffset + 8) = offset[2];
    Virtual<uint32_t>(self, slot::kComputeBoundingBox, self + field::kBox, self + field::kBox + 0xC);
    if (!exemplar)
        return;
    void* const exemplarHolder = Virtual<void*>(exemplar, 0x1C);
    if (Virtual<uint8_t>(exemplarHolder, 0xC, kExemplarProperty))
        return;
    void* const memory = CallCdecl<void*, uint32_t>(addr::SC4Property_Allocate, 0x1Cu);
    if (!memory)
        return;
    void* const property = CallThis<void*, uint32_t>(addr::SC4Property_Construct, memory, kPropertySetUp);
    if (!property)
        return;
    Virtual<uint32_t>(property, 0x4);
    alignas(8) char number[0x14];
    void* const made = CallThis<void*, uint32_t>(addr::SC4ScriptNumber_FromBool, number, 1u);
    Virtual<uint32_t>(property, 0x1C, made);
    CallThis<void>(addr::SC4ScriptNumber_Destroy, number);
    Virtual<uint32_t>(self + field::kHolder, 0x3C, property, 0u);
    Virtual<uint32_t>(property, 0x8);
}

// Slot 4 (0x0061D0D0; the occupant part's slot 4 is the thunk 0x00604E90 to
// it, and the bridge's and the tunnel's Shutdown call it): an object that
// is not initialised answers AL = 1 (the rest of EAX as IsInitialized left
// it). Else the model instance and the pylon's are forgotten and released
// (each field is zeroed before its Release), and the occupant base's
// Shutdown is the answer (the game jumps to the thunk 0x00619100 with the
// object, which adds 4 and jumps to 0x005E8980).
uint32_t __fastcall Shutdown(char* self, void*)
{
    static bool logged = false;
    FirstCall("Shutdown", logged);
    const uint32_t initialised = Virtual<uint32_t>(self + field::kOccupant, 0x14);
    if (!static_cast<uint8_t>(initialised))
        return (initialised & 0xFFFFFF00u) | 1u;
    if (void* const model = At<void*>(self, field::kModel)) {
        At<void*>(self, field::kModel) = nullptr;
        Virtual<uint32_t>(model, 0x8);
    }
    if (void* const model = At<void*>(self, field::kPylonModel)) {
        At<void*>(self, field::kPylonModel) = nullptr;
        Virtual<uint32_t>(model, 0x8);
    }
    return CallThis<uint32_t>(addr::cSC4Occupant_Shutdown, self + field::kOccupant);
}

// The occupant part's slot 3 (0x0061DB00; `part` is the object + 4; the
// bridge's and the tunnel's call it): an initialised object answers 1.
// Else the network occupant's occupant Init, whose answer is this one's;
// when it is not 0: the model part's word at the object + 0x4C loses bit
// 0x10 and gets 0x40, the object at 0x00B43DDC is asked its slot 0x24 and
// the answer (all of EAX) handed to SetLit (slot 61), and the two planes
// are worked out (0x0061D230). Only AL is the answer.
uint8_t __fastcall OccupantInit(char* part, void*)
{
    static bool logged = false;
    FirstCall("OccupantInit", logged);
    if (Virtual<uint8_t>(part, 0x14))
        return 1;
    const uint8_t result = CallThis<uint8_t>(addr::cSC4NetworkOccupant_OccupantInit, part);
    if (!result)
        return result;
    char* const self = part - field::kOccupant;
    At<uint32_t>(self, field::kModelWord) = (At<uint32_t>(self, field::kModelWord) & 0xFFFFFFEFu) | 0x40u;
    void* const lighting = Global<void*>(addr::SC4_Transform43DDC);
    const uint32_t lit = Virtual<uint32_t>(lighting, 0x24);
    Virtual<uint32_t>(self, slot::kSetLit, lit);
    CallThis<void>(addr::cSC4PrebuiltNetworkOccupant_UpdatePlanes, self);
    return result;
}

// ====================================================================================
// simulators.prebuiltocc.geometry: the planes and what they answer, the path information
// ====================================================================================

// 0x0061D230 (no arguments; the occupant part's Init and the makers at
// 0x006243D3, 0x006277AB and 0x00627C24 call it): the two planes of the
// piece's surface from the corner heights h0 to h3 (+0xA0, +0xB8, +0xD0,
// +0xE8) and the position (the occupant part's slot 8; only its y is used),
// 1/16 being the float at 0x00AA81E4 and 1/2 the one at 0x00A84D2C. With
// flag 0x01000000 the height of both planes is (h1 + h3) / 2 - y and the
// slopes are (h3 - h0) / 16 and (h1 - h0) / 16 for the first, (h2 - h1) / 16
// and (h2 - h3) / 16 for the second; without it (h0 + h2) / 2 - y, and
// (h2 - h1) / 16 and (h1 - h0) / 16, then (h3 - h0) / 16 and (h2 - h3) / 16.
// The game works on the FPU at 53 bits and stores floats; doubles do the
// same.
void __fastcall UpdatePlanes(char* self, void*)
{
    static bool logged = false;
    FirstCall("UpdatePlanes", logged);
    float position[3] = {};
    Virtual<uint32_t>(self + field::kOccupant, 0x20, static_cast<float*>(position));
    const bool other = Virtual<uint8_t>(self, slot::kHasNetworkFlag, flag::kOtherDiagonal) != 0;
    const double half = static_cast<double>(Global<float>(addr::SC4_FloatHalf));
    const double scale = static_cast<double>(Global<float>(addr::cSC4PrebuiltNetworkOccupant_PlaneScale));
    const auto height = [self](std::size_t corner) {
        return static_cast<double>(At<float>(self, field::kVertexHeights + corner * field::kVertexStride));
    };
    double base = 0;
    if (other) {
        base = (height(1) + height(3)) * half - static_cast<double>(position[1]);
        At<float>(self, field::kPlaneA) = static_cast<float>((height(3) - height(0)) * scale);
        At<float>(self, field::kPlaneA + 4) = static_cast<float>((height(1) - height(0)) * scale);
        At<float>(self, field::kPlaneA + 8) = static_cast<float>(base);
        At<float>(self, field::kPlaneB) = static_cast<float>((height(2) - height(1)) * scale);
    } else {
        base = (height(0) + height(2)) * half - static_cast<double>(position[1]);
        At<float>(self, field::kPlaneA) = static_cast<float>((height(2) - height(1)) * scale);
        At<float>(self, field::kPlaneA + 4) = static_cast<float>((height(1) - height(0)) * scale);
        At<float>(self, field::kPlaneA + 8) = static_cast<float>(base);
        At<float>(self, field::kPlaneB) = static_cast<float>((height(3) - height(0)) * scale);
    }
    At<float>(self, field::kPlaneB + 4) = static_cast<float>((height(2) - height(3)) * scale);
    At<float>(self, field::kPlaneB + 8) = static_cast<float>(base);
}

// Which plane a point (a, b) from the position lies on, as the three
// functions below ask: with flag 0x01000000 the sum a + b, without it the
// difference a - b, against the float at 0x00A81054 (zero); below it
// (ordered) the first plane, else, and for an unordered compare, the second.
bool OnFirstPlane(char* self, float a, float b)
{
    const bool other = Virtual<uint8_t>(self, slot::kHasNetworkFlag, flag::kOtherDiagonal) != 0;
    const double side =
        other ? static_cast<double>(a) + static_cast<double>(b) : static_cast<double>(a) - static_cast<double>(b);
    return side < static_cast<double>(Global<float>(addr::SC4_FloatZero));
}

// Slot 65 (0x0061D660, ret 4: a vector, whose y gets the surface's height
// added). The vector's x and z are turned by the piece's rotation (slot 24)
// into (a, b): 0 gives (x, z), 1 (-z, x), 2 (-x, -z), 3 (z, -x); a rotation
// above 3 leaves the game's two stack slots as they were, the argument's
// (the vector's address) and the pushed ECX (the object), which is done
// here too. A flipped piece (slot 25) negates a. With a path ID override
// the floats at +0x168 and +0x16C are added to a and b (float stores). Then
// with flag 0x01000000 the side is b + a, else a - b, and y becomes
// b * Bz + a * Bx + Bh + y on the first plane and the same with the second
// plane's three floats otherwise, where the first plane's are read as
// (+0x188, +0x184, +0x18C) and the second's as (+0x194, +0x190, +0x198).
// The words the game copies are copied; its fld, fchs, fstp are Negated.
void __fastcall TransformNode(char* self, void*, float* vector)
{
    static bool logged = false;
    FirstCall("TransformNode", logged);
    const uint8_t rotation = Virtual<uint8_t>(self, slot::kGetRotation);
    const uint8_t flip = Virtual<uint8_t>(self, slot::kGetFlip);
    float a = AsFloat(static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(vector)));
    float b = AsFloat(static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(self)));
    switch (rotation) {
    case 0:
        a = AsFloat(At<uint32_t>(vector, 0));
        b = AsFloat(At<uint32_t>(vector, 8));
        break;
    case 1:
        b = AsFloat(At<uint32_t>(vector, 0));
        a = Negated(vector[2]);
        break;
    case 2:
        a = Negated(vector[0]);
        b = Negated(vector[2]);
        break;
    case 3:
        a = AsFloat(At<uint32_t>(vector, 8));
        b = Negated(vector[0]);
        break;
    default:
        break;
    }
    if (flip)
        a = Negated(a);
    if (At<uint32_t>(self, field::kPathIDOverride) != 0) {
        a = static_cast<float>(static_cast<double>(a) + static_cast<double>(At<float>(self, field::kPathOriginX)));
        b = static_cast<float>(static_cast<double>(b) + static_cast<double>(At<float>(self, field::kPathOriginZ)));
    }
    const bool other = Virtual<uint8_t>(self, slot::kHasNetworkFlag, flag::kOtherDiagonal) != 0;
    const double side =
        other ? static_cast<double>(b) + static_cast<double>(a) : static_cast<double>(a) - static_cast<double>(b);
    const std::size_t plane =
        side < static_cast<double>(Global<float>(addr::SC4_FloatZero)) ? field::kPlaneA : field::kPlaneB;
    const double first = static_cast<double>(b) * static_cast<double>(At<float>(self, plane + 4));
    const double second = static_cast<double>(a) * static_cast<double>(At<float>(self, plane));
    const double sum = first + second;
    const double lifted = sum + static_cast<double>(At<float>(self, plane + 8));
    vector[1] = static_cast<float>(lifted + static_cast<double>(vector[1]));
}

// Slot 56 (0x0061D790, ret 8; the height at a world position, in ST0): the
// position of the piece (slot 75) taken off x and z (float stores) gives
// (a, b); on the first plane the answer is a * Ax + b * Az + Ah + y, on the
// second a * Bx + b * Bz + Bh + y, y being the position's. Every
// intermediate stays on the FPU; doubles do the same.
double __fastcall GetAltitude(char* self, void*, float x, float z)
{
    static bool logged = false;
    FirstCall("GetAltitude", logged);
    float position[3] = {};
    Virtual<uint32_t>(self, slot::kGetPosition, static_cast<float*>(position));
    const float a = static_cast<float>(static_cast<double>(x) - static_cast<double>(position[0]));
    const float b = static_cast<float>(static_cast<double>(z) - static_cast<double>(position[2]));
    const std::size_t plane = OnFirstPlane(self, a, b) ? field::kPlaneA : field::kPlaneB;
    const double first = static_cast<double>(a) * static_cast<double>(At<float>(self, plane));
    const double second = static_cast<double>(b) * static_cast<double>(At<float>(self, plane + 4));
    const double sum = first + second;
    const double lifted = sum + static_cast<double>(At<float>(self, plane + 8));
    return lifted + static_cast<double>(position[1]);
}

// Slot 57 (0x0061D840, ret 0xC: the out vector, then x and z). The same
// (a, b) and the same choice of plane as GetAltitude: the out vector is the
// plane's x slope negated, 1.0, its z slope negated. The answer is the out
// pointer.
uint32_t* __fastcall GetNormal(char* self, void*, uint32_t* out, float x, float z)
{
    static bool logged = false;
    FirstCall("GetNormal", logged);
    float position[3] = {};
    Virtual<uint32_t>(self, slot::kGetPosition, static_cast<float*>(position));
    const float a = static_cast<float>(static_cast<double>(x) - static_cast<double>(position[0]));
    const float b = static_cast<float>(static_cast<double>(z) - static_cast<double>(position[2]));
    const std::size_t plane = OnFirstPlane(self, a, b) ? field::kPlaneA : field::kPlaneB;
    const float slopeX = Negated(At<float>(self, plane));
    const float slopeZ = Negated(At<float>(self, plane + 4));
    out[1] = 0x3F800000u;
    out[0] = AsBits(slopeX);
    out[2] = AsBits(slopeZ);
    return out;
}

// Slot 49 (0x0061D380; no arguments; the answer is the path information, the
// object at +0x140). With one already there it is just returned. Without an
// exemplar (the exemplar part's slot 4) the answer is 0. Else the
// exemplar's resource key is fetched (its slot 0x2C) and its slot 0x1C
// called (the answer is not used); the key becomes {0x296678F7, 0xA966883F,
// instance}, the instance the network manager's slot 0x3C answers for the
// path ID override when there is one and for the exemplar's instance
// otherwise; the resource manager is asked for it as class 0xA9667793. With
// a resource: when its slot 0x40 says no its slot 0x60 may give another
// resource, which gets slot 0x44 (1) and replaces the first (released); the
// resource's slot 0x64 (this) gives the path information and the resource
// is released. With path information: with a path ID override it is made
// again by its slot 0x74 from the words at +0x168 and +0x16C and the x and z
// of corners 0 and 2 (four words copied by 0x00466590), the first one
// released; the result is stored (also when it is null) and, when there is
// one and flag 0x00100000 is not set, its slot 0x68 (this) called. Whatever
// the path information is now, slot 0xA0 (this) is called on it, the eight
// words of the path connection table (+0x144) zeroed and slot 0x6C (this)
// called.
void* __fastcall GetPathInfo(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetPathInfo", logged);
    if (At<void*>(self, field::kReference))
        return At<void*>(self, field::kReference);
    void* resource = nullptr;
    void* const exemplar = Virtual<void*>(self + field::kExemplarPart, 0x10);
    if (!exemplar)
        return nullptr;
    struct Key {
        uint32_t type;
        uint32_t group;
        uint32_t instance;
    } key = {0, 0, 0};
    Virtual<uint32_t>(exemplar, 0x2C, &key);
    Virtual<uint32_t>(exemplar, 0x1C);
    const uint32_t pathOverride = At<uint32_t>(self, field::kPathIDOverride);
    key.type = 0x296678F7u;
    key.group = 0xA966883Fu;
    if (pathOverride != 0)
        key.instance = pathOverride;
    void* const network = CallCdecl<void*>(addr::SC4_GetNetworkManager);
    key.instance = Virtual<uint32_t>(network, 0x3C, key.instance);
    void* const manager = Global<void*>(addr::cSC4NetworkOccupant_ResourceManager);
    Virtual<uint8_t>(manager, 0xC, &key, 0xA9667793u, &resource, 0u, 0u);
    if (resource) {
        if (!Virtual<uint8_t>(resource, 0x40)) {
            void* const replacement = Virtual<void*>(resource, 0x60);
            if (replacement) {
                Virtual<uint32_t>(replacement, 0x44, 1u);
                Virtual<uint32_t>(resource, 0x8);
                resource = replacement;
            }
        }
        void* info = Virtual<void*>(resource, 0x64, self);
        Virtual<uint32_t>(resource, 0x8);
        if (info) {
            if (At<uint32_t>(self, field::kPathIDOverride) != 0) {
                uint32_t points[4];
                CallThis<void*, char*, char*, char*, char*>(
                    addr::SC4_FourWordsFromPointers, points, self + field::kCorners, self + field::kCorners + 8,
                    self + field::kCorners + 2 * field::kVertexStride,
                    self + field::kCorners + 2 * field::kVertexStride + 8);
                void* const made = Virtual<void*>(info, 0x74, At<uint32_t>(self, field::kPathOriginX), 0u,
                                                  At<uint32_t>(self, field::kPathOriginZ),
                                                  static_cast<uint32_t*>(points));
                Virtual<uint32_t>(info, 0x8);
                info = made;
            }
            At<void*>(self, field::kReference) = info;
            if (info && !Virtual<uint8_t>(self, slot::kHasNetworkFlag, flag::kNoPathRegister))
                Virtual<uint32_t>(At<void*>(self, field::kReference), 0x68, self);
        }
    }
    if (void* const now = At<void*>(self, field::kReference)) {
        Virtual<uint32_t>(now, 0xA0, self);
        for (std::size_t i = 0; i < 8; ++i)
            At<uint32_t>(self, field::kPathTable + 4 * i) = 0;
        Virtual<uint32_t>(At<void*>(self, field::kReference), 0x6C, self);
    }
    return At<void*>(self, field::kReference);
}

// ====================================================================================
// simulators.prebuiltocc.save: the save record
// ====================================================================================

// +0x8 slot 3 (0x0061D8E0; `part` is the object + 8; the tunnel's Write
// calls it). Version 4, then the network occupant's record (when it fails
// the class's fields are left out and the answer is still the stream's
// state), the three floats of the model's offset, the piece override, the
// rotation override byte, the pylon's model and, with one, its angle and
// its three offset floats. The layout is in
// tools/re/save_schemas/prebuiltocc.py.
uint32_t __fastcall Write(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    char* const self = part - field::kSerializable;
    Virtual<uint8_t>(stream, 0x1C, 4u);
    if (CallThis<uint8_t, void*>(addr::cSC4NetworkOccupant_Write, part, stream)) {
        for (std::size_t i = 0; i < 3; ++i)
            Virtual<uint8_t>(stream, 0x30, At<uint32_t>(self, field::kModelOffset + 4 * i));
        Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, field::kPieceOverride));
        Virtual<uint8_t>(stream, 0x14, uint32_t{At<uint8_t>(self, field::kRotationOverride)});
        Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, field::kPylon));
        if (At<uint32_t>(self, field::kPylon) != 0)
            for (std::size_t i = 0; i < 4; ++i)
                Virtual<uint8_t>(stream, 0x30, At<uint32_t>(self, field::kPylonAngle + 4 * i));
    }
    return NoError(stream);
}

// +0x8 slot 4 (0x0061D9B0; `part` is the object + 8; the tunnel's Read
// calls it): versions 3 and 4. The game reads the version into its
// argument's stack slot (the stream pointer until the stream writes two
// bytes of it), so it starts with that value here. Another version: AL
// cleared, the low word of EAX the version and the rest as the stream's
// read left it. The network occupant's record, and when that is good the
// class's fields as Write puts them; version 3 has six more uint32 after
// the model's offset and one more at the end, all read into one local slot
// that holds the part's address until the stream writes it (the `push ecx`
// of the game's frame) and dropped. The answer is the stream's state.
uint32_t __fastcall Read(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Read", logged);
    char* const self = part - field::kSerializable;
    uint32_t version = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(stream));
    const uint32_t read = Virtual<uint32_t>(stream, 0x1C, &version);
    if (static_cast<uint16_t>(version) < 3 || static_cast<uint16_t>(version) > 4)
        return ((read & 0xFFFF0000u) | static_cast<uint16_t>(version)) & 0xFFFFFF00u;
    if (CallThis<uint8_t, void*>(addr::cSC4NetworkOccupant_Read, part, stream)) {
        uint32_t dropped = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(part));
        for (std::size_t i = 0; i < 3; ++i)
            Virtual<uint8_t>(stream, 0x30, &At<uint32_t>(self, field::kModelOffset + 4 * i));
        if (static_cast<uint16_t>(version) < 4)
            for (int i = 0; i < 6; ++i)
                Virtual<uint8_t>(stream, 0x24, &dropped);
        Virtual<uint8_t>(stream, 0x24, &At<uint32_t>(self, field::kPieceOverride));
        Virtual<uint8_t>(stream, 0x14, &At<uint8_t>(self, field::kRotationOverride));
        Virtual<uint8_t>(stream, 0x24, &At<uint32_t>(self, field::kPylon));
        if (At<uint32_t>(self, field::kPylon) != 0)
            for (std::size_t i = 0; i < 4; ++i)
                Virtual<uint8_t>(stream, 0x30, &At<uint32_t>(self, field::kPylonAngle + 4 * i));
        if (static_cast<uint16_t>(version) < 4)
            Virtual<uint8_t>(stream, 0x24, &dropped);
    }
    return NoError(stream);
}

// ====================================================================================
// simulators.prebuiltocc.object: construction, destruction and the pool
// ====================================================================================

// The constructor (0x0061E1A0; one argument: non-zero for the most derived
// object, which sets the virtual base table at +0x3C and the abstract
// vtable at +0x1D4 itself; the bridge's and the tunnel's constructors call
// with 0 after their own table is in place). The network occupant's
// constructor with 0, then the six vtables and the virtual base's, found
// through the table at +0x3C as the game does (the table is read twice),
// with the displacement word before it. The six plane floats, then the
// pylon's three offset floats, each from the float at 0x00B45298 (zero;
// read every time, as the game does); the piece override and its byte
// zero; the two effect IDs -1; the two seen bytes 0xFF; the two model
// instances null; +0x16C and +0x168 zero; the immovable flag set; the six
// plane floats and the pylon's model zero. Left as the pool's memory holds
// them (as in the game): the model's offset (+0x178 to +0x183, the set-up
// and Read set it), the pylon's angle (+0x1A0), +0x1B5 to +0x1B7, +0x1C2,
// +0x1C3 and +0x1CC. The game's EAX is the object.
void* __fastcall Construct(char* self, void*, uint32_t mostDerived)
{
    static bool logged = false;
    FirstCall("Construct", logged);
    if (mostDerived) {
        At<std::uintptr_t>(self, field::kVbtable) = addr::cSC4PrebuiltNetworkOccupant_vbtable;
        At<std::uintptr_t>(self, field::kVbase) = addr::cSC4NetworkOccupant_vbase_abstract_vftable;
    }
    CallThis<void*, uint32_t>(addr::cSC4NetworkOccupant_cSC4NetworkOccupant, self, 0u);
    At<std::uintptr_t>(self, 0x0) = addr::cSC4PrebuiltNetworkOccupant_vftable;
    At<std::uintptr_t>(self, field::kOccupant) = addr::cSC4PrebuiltNetworkOccupant_vftable_4;
    At<std::uintptr_t>(self, field::kSerializable) = addr::cSC4PrebuiltNetworkOccupant_vftable_8;
    At<std::uintptr_t>(self, field::kHolder) = addr::cSC4PrebuiltNetworkOccupant_vftable_C;
    At<std::uintptr_t>(self, field::kExemplarPart) = addr::cSC4PrebuiltNetworkOccupant_vftable_20;
    At<std::uintptr_t>(self, field::kModelPart) = addr::cSC4PrebuiltNetworkOccupant_vftable_38;
    const uint32_t displacement = At<uint32_t>(At<char*>(self, field::kVbtable), 4);
    At<std::uintptr_t>(self, displacement + field::kVbtable) = addr::cSC4PrebuiltNetworkOccupant_vbase_vftable;
    const uint32_t again = At<uint32_t>(At<char*>(self, field::kVbtable), 4);
    At<uint32_t>(self, again + field::kModelPart) = again - 0x198;
    const std::uintptr_t zero = addr::cSC4PrebuiltNetworkOccupant_ZeroFloat;
    for (std::size_t offset = field::kPlaneA; offset < field::kPylon; offset += 4)
        At<uint32_t>(self, offset) = Global<uint32_t>(zero);
    for (std::size_t offset = field::kPylonOffset; offset < field::kPieceOverride; offset += 4)
        At<uint32_t>(self, offset) = Global<uint32_t>(zero);
    At<uint32_t>(self, field::kPieceOverride) = 0;
    At<uint8_t>(self, field::kRotationOverride) = 0;
    At<uint32_t>(self, field::kEffects) = 0xFFFFFFFFu;
    At<uint32_t>(self, field::kEffects + 4) = 0xFFFFFFFFu;
    At<uint8_t>(self, field::kSeenZoom) = 0xFF;
    At<uint8_t>(self, field::kSeenDetail) = 0xFF;
    At<uint32_t>(self, field::kModel) = 0;
    At<uint32_t>(self, field::kPylonModel) = 0;
    const uint32_t flags = At<uint32_t>(self, field::kFlags);
    At<uint32_t>(self, field::kPathOriginZ) = 0;
    At<uint32_t>(self, field::kPathOriginX) = 0;
    At<uint32_t>(self, field::kFlags) = flags | flag::kImmovable;
    for (std::size_t offset = field::kPlaneA; offset < field::kPylon; offset += 4)
        At<uint32_t>(self, offset) = 0;
    At<uint32_t>(self, field::kPylon) = 0;
    return self;
}

// An item of the pool (0x0061D200, and inline in the factory): the current
// page's free head, the pool refilled when the page has none (0x0090CF9A
// returns the new head); the page is read again after. The item's memory
// is not cleared.
void* PoolAllocate()
{
    char* const pool = reinterpret_cast<char*>(addr::cSC4PrebuiltNetworkOccupant_Pool);
    void* item = At<void*>(At<char*>(pool, 0x4), 0x8);
    if (!item)
        item = CallThis<void*>(addr::SC4EdgePool_Grow, pool);
    At<void*>(At<char*>(pool, 0x4), 0x8) = *static_cast<void**>(item);
    ++At<uint32_t>(At<char*>(pool, 0x4), 0xC);
    return item;
}

// An item of the pool back in its page (the page header is at the address
// with its low 15 bits clear): onto the page's free list, the used count
// down by one; at 0 the page is released, at 0x40000033 opened again.
void PoolReturn(void* pool, char* item)
{
    char* const page = reinterpret_cast<char*>(reinterpret_cast<std::uintptr_t>(item) & 0xFFFF8000u);
    At<void*>(item, 0x0) = At<void*>(page, 0x8);
    const uint32_t used = At<uint32_t>(page, 0xC) - 1;
    At<void*>(page, 0x8) = item;
    At<uint32_t>(page, 0xC) = used;
    if (used == 0)
        CallThis<void, void*>(addr::SC4Pool_ReleaseBlock, pool, page);
    else if (used == 0x40000033)
        CallThis<void, void*>(addr::SC4Pool_ReopenBlock, pool, page);
}

// The free (0x0061E300; cdecl, one argument: the item; the deleting
// destructor calls it): null does nothing, else the item goes back to the
// pool at 0x00B0B3A8.
void __cdecl Free(char* item)
{
    static bool logged = false;
    FirstCall("Free", logged);
    if (!item)
        return;
    PoolReturn(reinterpret_cast<void*>(addr::cSC4PrebuiltNetworkOccupant_Pool), item);
}

// The deleting destructor (0x00605020, one argument; the holder part's slot
// 22 and the +0x38 part's slot 0 reach it through thunks): the class's
// destructor (0x00604CA0, the game's: the bridge and the tunnel call it
// too), and with flag 1 the free. The result is the object.
void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("DeletingDestructor", logged);
    CallThis<void>(addr::cSC4PrebuiltNetworkOccupant_Destroy, self);
    if (flags & 1)
        CallCdecl<void, char*>(addr::cSC4PrebuiltNetworkOccupant_Free, self);
    return self;
}

// The pool's free (0x0061E7E0; slot 1 of the pool's table at 0x00AA81EC, ECX
// the pool, one argument: the item; null does nothing).
void __fastcall PoolFree(void* pool, void*, char* item)
{
    static bool logged = false;
    FirstCall("PoolFree", logged);
    if (!item)
        return;
    PoolReturn(pool, item);
}

// The pool's page set-up (0x0061D090; slot 2 of the same table; ECX is not
// read; the static initialiser at 0x00A73CB5 calls it too, before any
// replacement is installed): a page of 32 KB (the allocator's order 0xF), 69
// items of 0x1D8 bytes from +0x10 chained upwards (each item's first word
// is the address of the next one, the last has 0), the page's free list at
// +8 the lowest item and its used count at +0xC zero. The result is the
// page.
char* __cdecl InitPoolPage()
{
    static bool logged = false;
    FirstCall("InitPoolPage", logged);
    char* const page = CallCdecl<char*, uint32_t>(addr::SC4_AllocatePoolPage, 0xFu);
    char* item = page + 0x7D70;
    std::uintptr_t previous = 0;
    char* lowest = nullptr;
    for (int i = 0; i < 0x45; ++i) {
        At<std::uintptr_t>(item, 0) = previous;
        lowest = item;
        previous = reinterpret_cast<std::uintptr_t>(item);
        item -= kObjectSize;
    }
    At<void*>(page, 0x8) = lowest;
    At<uint32_t>(page, 0xC) = 0;
    return page;
}

// The pool allocation (0x0061D200; cdecl, the size 0x1D8 pushed by the maker
// at 0x006242F6 is not read).
void* __cdecl Allocate(std::size_t)
{
    static bool logged = false;
    FirstCall("Allocate", logged);
    return PoolAllocate();
}

// The factory (0x0061E820): an item of the pool, constructed as the most
// derived object (0x0061E1A0 with 1). The result is the occupant part (the
// object + 4), or null if the constructor gave none (it never does).
void* __cdecl Create()
{
    static bool logged = false;
    FirstCall("Create", logged);
    char* const object = CallThis<char*, uint32_t>(addr::cSC4PrebuiltNetworkOccupant_cSC4PrebuiltNetworkOccupant,
                                                   PoolAllocate(), 1u);
    return object ? object + field::kOccupant : nullptr;
}

// ====================================================================================
// Installation
// ====================================================================================

struct Detour {
    std::uintptr_t address;
    void* ours;
};

#define OPENSC4_DETOUR(address, fn) {address, reinterpret_cast<void*>(&fn)}

// Hooks to leave out, for bisecting: OPENSC4_PREBUILTOCC_SKIP lists function
// addresses (hex, eight digits), as OPENSC4_NETOCC_SKIP does.
bool Skipped(std::uintptr_t address)
{
    char list[1024] = {};
    if (!GetEnvironmentVariableA("OPENSC4_PREBUILTOCC_SKIP", list, sizeof list))
        return false;
    char wanted[16];
    std::snprintf(wanted, sizeof wanted, "%08X", static_cast<unsigned>(address));
    return std::strstr(list, wanted) != nullptr;
}

template <std::size_t N>
bool InstallDetours(const Detour (&detours)[N], const char* what)
{
    if (!game::IsBuild641())
        return false;
    for (const Detour& detour : detours)
        if (!addr_check::Find(detour.address)) {
            log::Write("prebuiltocc %s: no address entry for 0x%08X", what, static_cast<unsigned>(detour.address));
            return false;
        }
    unsigned installed = 0;
    for (const Detour& detour : detours) {
        if (Skipped(detour.address))
            continue;
        void* original = nullptr;
        if (!hook::Install(*addr_check::Find(detour.address), detour.ours, &original))
            return false;
        ++installed;
    }
    log::Write("prebuiltocc %s: %u detours", what, installed);
    return true;
}

const Detour kSlots[] = {
    OPENSC4_DETOUR(addr::cSC4PrebuiltNetworkOccupant_QueryInterface, QueryInterface),
    OPENSC4_DETOUR(addr::cSC4PrebuiltNetworkOccupant_IsSupportPylonPresent, IsSupportPylonPresent),
    OPENSC4_DETOUR(addr::cSC4PrebuiltNetworkOccupant_SetSupportPylon, SetSupportPylon),
    OPENSC4_DETOUR(addr::cSC4PrebuiltNetworkOccupant_SetUp, SetUp),
    OPENSC4_DETOUR(addr::cSC4PrebuiltNetworkOccupant_Shutdown, Shutdown),
    OPENSC4_DETOUR(addr::cSC4PrebuiltNetworkOccupant_OccupantInit, OccupantInit),
};

const Detour kGeometry[] = {
    OPENSC4_DETOUR(addr::cSC4PrebuiltNetworkOccupant_UpdatePlanes, UpdatePlanes),
    OPENSC4_DETOUR(addr::cSC4PrebuiltNetworkOccupant_TransformNode, TransformNode),
    OPENSC4_DETOUR(addr::cSC4PrebuiltNetworkOccupant_GetAltitude, GetAltitude),
    OPENSC4_DETOUR(addr::cSC4PrebuiltNetworkOccupant_GetNormal, GetNormal),
    OPENSC4_DETOUR(addr::cSC4PrebuiltNetworkOccupant_GetPathInfo, GetPathInfo),
};

const Detour kSave[] = {
    OPENSC4_DETOUR(addr::cSC4PrebuiltNetworkOccupant_Write, Write),
    OPENSC4_DETOUR(addr::cSC4PrebuiltNetworkOccupant_Read, Read),
};

const Detour kObject[] = {
    OPENSC4_DETOUR(addr::cSC4PrebuiltNetworkOccupant_cSC4PrebuiltNetworkOccupant, Construct),
    OPENSC4_DETOUR(addr::cSC4PrebuiltNetworkOccupant_DeletingDestructor, DeletingDestructor),
    OPENSC4_DETOUR(addr::cSC4PrebuiltNetworkOccupant_Free, Free),
    OPENSC4_DETOUR(addr::cSC4PrebuiltNetworkOccupant_PoolFree, PoolFree),
    OPENSC4_DETOUR(addr::cSC4PrebuiltNetworkOccupant_InitPoolPage, InitPoolPage),
    OPENSC4_DETOUR(addr::cSC4PrebuiltNetworkOccupant_Allocate, Allocate),
    OPENSC4_DETOUR(addr::cSC4PrebuiltNetworkOccupant_Create, Create),
};

bool InstallSlots(registry::Mode) { return InstallDetours(kSlots, "slots"); }
bool InstallGeometry(registry::Mode) { return InstallDetours(kGeometry, "geometry"); }
bool InstallSave(registry::Mode) { return InstallDetours(kSave, "save"); }
bool InstallObject(registry::Mode) { return InstallDetours(kObject, "object"); }

const bool g_registered[] = {
    registry::Register({"simulators.prebuiltocc.slots", "simulators/prebuiltocc", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4PrebuiltNetworkOccupant_QueryInterface,
                        "Our pre-built-model network occupant small functions: QueryInterface, the pylon's query and "
                        "setter, the set-up, Shutdown and the occupant part's Init (6 detours; "
                        "src/core/prebuiltocc_sim.cpp)",
                        false, InstallSlots}),
    registry::Register({"simulators.prebuiltocc.geometry", "simulators/prebuiltocc", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4PrebuiltNetworkOccupant_UpdatePlanes,
                        "Our pre-built-model network occupant surface planes, TransformNode, GetAltitude, GetNormal "
                        "and GetPathInfo (5 detours; src/core/prebuiltocc_sim.cpp)",
                        false, InstallGeometry}),
    registry::Register({"simulators.prebuiltocc.save", "simulators/prebuiltocc", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4PrebuiltNetworkOccupant_Write,
                        "Our pre-built-model network occupant save record: Write (version 4) and Read (3 and 4; 2 "
                        "detours; src/core/prebuiltocc_sim.cpp)",
                        false, InstallSave}),
    registry::Register({"simulators.prebuiltocc.object", "simulators/prebuiltocc", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4PrebuiltNetworkOccupant_cSC4PrebuiltNetworkOccupant,
                        "Our pre-built-model network occupant constructor, deleting destructor, the free, the pool's "
                        "free, page set-up and allocation and the factory (7 detours; src/core/prebuiltocc_sim.cpp)",
                        false, InstallObject}),
};

} // namespace
} // namespace opensc4::prebuiltocc_sim
