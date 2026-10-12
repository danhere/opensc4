// Our cSC4Demolition, the small half (build 641, class ID 0x0990C38A, code
// 0x00466480 to 0x00467BB0): the object, Init, Shutdown, the tuning, the
// message handler, DemolishLots, the two effect functions, the class's own
// occupant filter and the helpers the big functions share. The big half
// (0x00467BB0 to 0x004695A0: ModifyTerrainHeight, DemolishRegion,
// DemolishOccupant and the network helper) is demolition_2.cpp; it calls
// the functions here by address, as the original does.
//
// The service: the city makes one (factory 0x00466E40, registered at
// 0x00460090; the city's slot 0x24 hands it out), 0x34 bytes, three
// vtables:
//
//   +0x00  vtable 0x00A88004, 13 slots: cISC4Demolition. 0 QueryInterface
//          0x004664B0, 1 AddRef 0x004664A0 (shared, ours already), 2 Release
//          0x00466950, 3 Init 0x00466EF0, 4 Shutdown 0x00466980, 5
//          SetDefaultOccupantFilter 0x00648F10 (shared no-op), 6
//          DemolishRegion, 7 DemolishLots 0x004670C0, 8 DemolishOccupant, 9
//          ModifyTerrainHeight, 10 DoDemolishOccupantEffect 0x004673B0, 11
//          DoDemolishGroupEffect 0x00467530, 12 the deleting destructor
//          0x00466920.
//   +0x04  vtable 0x00A87FF4, 4 slots: cIGZMessageTarget2. Thunks
//          0x004666A0, 0x00466DF0 around the shared AddRef 0x006C2270; 3
//          DoMessage 0x00466FF0 (called with this + 4).
//   +0x08  vtable 0x00A87FDC, 6 slots: a serializable with the shared no-op
//          Write and Read (0x0042E440, 0x006B8A70); 5 GetGZCLSID
//          0x00466490. Thunks 0x004666B0, 0x00466E00.
//
// QueryInterface: 0x00000001 and 0x26DDE2EA give the object, 0x452294AA the
// message target (+4), 0xE4FDA3D4 the serializable (+8); the answer is
// referenced; anything else is false and leaves the out pointer alone.
//
// Fields of the service (offsets from the object's start):
//   +0x0C uint32  reference count (AddRef is shared code; Release destroys
//                 the object when it is 1 or less)
//   +0x10 uint8   initialised (Init sets it first, Shutdown clears it last)
//   +0x14 uint32  the city's slot 0x174, +0x18 uint32 its slot 0x178: the
//                 city's width and depth in cells
//   +0x1C         the default occupant filter (cSC4DemolitionFilter, made by
//                 Init, referenced)
//   +0x20         the city (0x00B43CEC, referenced)
//   +0x24         the lot developer (the building development simulator
//                 0x00B43D68, slot 0x90; referenced)
//   +0x28         the occupant manager (0x00B43D0C, referenced)
//   +0x2C         the terrain (0x00B43CF4, referenced)
//   +0x30         the region of the DemolishRegion call in progress (the
//                 other part's; never released here)
// Init registers the message target (this + 4) for 0xC53D10AA (tuning
// changed), 0x26D31EC1 (the city has been set up; ignored) and 0xA9DED179
// (a lot has been demolished); Shutdown takes them back.
//
// The filter (cSC4DemolitionFilter, 0x34 bytes, made by Init; constructor
// 0x00466700, vtable 0x00A880B8 with the base's +4 vtable 0x00A880A0; its
// slot 3 Test is 0x00466730, its deleting destructor 0x00466EA0):
//   +0x00 +0x04  the base (SC4::OccupantFilterBase 0x005EA080)
//   +0x0C +0x0D +0x0E uint8  the base's three flags
//   +0x10 uint8   limited to a rectangle (0x00466B90 sets it for its query)
//   +0x14 float x low, +0x18 z low, +0x1C x high, +0x20 z high: the
//                 rectangle in metres (the corner positions of the cells)
//   +0x24 uint8   ask CanRemove (occupant slot 14) with +0x28
//   +0x28 uint32  the removal flags for that (the privilege mode)
//   +0x2C         another filter, the caller's (referenced, released by the
//                 destructor)
//   +0x30         a second filter, the view's (referenced, released by the
//                 destructor)
// The constructor leaves +0x14 to +0x28 as the allocator left them.
//
// The functions of this file, with what they are:
//   0x00466480  GetGZCLSID of the default occupant filter (0xC9C75EAE)
//   0x00466490  GetGZCLSID of the service (0x0990C38A)
//   0x004664B0  QueryInterface (ret 8)
//   0x00466530  Test of the filter ModifyTerrainHeight makes inline
//               (vtable 0x00A88050; ret 4)
//   0x004666F0  GetGZCLSID of the realistic occupant filter (0x8A7A1C2C)
//   0x00466700  the demolition filter's constructor
//   0x00466730  the demolition filter's Test (ret 4)
//   0x00466920  the service's deleting destructor (ret 4)
//   0x00466950  Release
//   0x00466980  Shutdown
//   0x00466A20  the sound of a demolished occupant
//   0x00466B90  the occupants of a rectangle (ret 8)
//   0x00466C50  makes and sends or posts a message (ret 0xC)
//   0x00466CD0  the tuning
//   0x00466E40  the factory
//   0x00466EA0  the filter's deleting destructor (ret 4), 0x00466EC0 its
//               destructor
//   0x00466EF0  Init
//   0x00466FF0  DoMessage (ret 4)
//   0x004670C0  DemolishLots (ret 0x20)
//   0x004673B0  DoDemolishOccupantEffect (ret 0x18)
//   0x00467530  DoDemolishGroupEffect (ret 0x10)
//   0x004677F0  what a demolished lot leaves of its zone (register
//               convention, see below)
//   0x00467A50  the destructor of a set of lots
//   0x00467AA0  a lot's occupants that the filter refuses are dropped (ret 8)
// Written inline here and not hooked, because they take their arguments in
// registers and have no other caller: 0x00466A90 (ESI the occupant, EDI a
// flags word to add bits to; AL true unless a prop lacks its property) and
// 0x004672E0 (ECX the occupant, ESI an optional float out; AL true when
// the occupant is too small for an effect).
//
// Left the game's: 0x00466590 (four dwords copied from four pointers, called
// from another class), 0x004665C0 SC4Rect::Union, 0x00466610
// SC4CellRegion::Test, 0x00466660 S3D::VectorLength, 0x004666C0 (the view
// filter's constructor), 0x00466E10 SC4Model::ConstructCorner, 0x004676A0
// and 0x004676E0 (erase a whole set of referenced objects, in a dozen
// classes' hands), 0x00467720 (the insert helper of 0x00467950), 0x00467950
// SC4LotPtrSet::InsertUnique, 0x00467A10, 0x00467B50 SC4List::PushBack,
// 0x00467B80 (a set insert with a caller in another class), the thunks
// 0x004666A0, 0x004666B0, 0x00466DF0 and 0x00466E00, and the shared
// AddRef 0x004664A0.
//
// Virtual calls: every count is from the callee's `ret N` (va-tools
// vt.py) except where the class is not at hand: the city 0x00A87620 (slots
// 0x174, 0x178: 0, 0x180: 4), the occupant manager 0x00A8F7F0 (0x48: 4,
// 0xA0: 5), the lot developer 0x00AB1B98 (0x34: 1), the terrain 0x00AB3D18
// (0x34: 2), the zone manager 0x00A92E00 (0x2C: 10), the lot manager
// 0x00AB37C0 (0x58: 9, 0x60: 1), the lot 0x00AB19B0 (0x18: 0, 0x38: 1,
// 0x5C: 0, 0x8C: 0, 0x9C: 0, 0xC4: 1), the building development simulator
// 0x00AB0AF8 (0x90: 0), the message server and message (cIGZMessageServer2,
// cIGZMessage2 headers; 0x30 and 0x84 as zonemgr_place.cpp), the resource
// manager (0xC: 5, as aura_init.cpp), the sound service (0x2C: 5, as
// terrain_edit.cpp) and the service at 0x00B43DC4 (0x58: 0, as audio_1.cpp)
// are checked. NOT checked against a callee: the effects manager's slots
// 0x18 (3 arguments; by id) and the effects' slots 0xC, 0x1C, 0x24, 0x2C
// and 0x30 (1, 1, 1, 3, 3; 0xC and 0x24 as disaster_fire.cpp), the
// occupant's slots 0x0, 0x18, 0x1C, 0x20, 0x28, 0x38 and 0x58 (2, 0, 0, 1,
// 2, 1, 1; 0x0, 0x58 and 0x20 as the other files), the network and
// foundation interfaces' slots 0x24 (0 and 1 argument) and 0x58 (1) behind
// 0xC8FCE101 and 0xA821EF94, and 0x007F9080 (4 stack arguments, ret 0x10).
// Those counts are the original's own pushes.
//
// Oddities kept:
//   - Init and the filter's constructor call through a null pointer when
//     the allocator fails; 0x00466C50 and QueryInterface do the same.
//   - Test (0x00466730) for a foundation (0x68FD0C69) asks for the
//     foundation interface without testing the answer.
//   - Test accepts a default-passing occupant the realistic filter refuses
//     only by calling both (the realistic test calls the default one).
//   - DoDemolishGroupEffect's matrix has two bytes at its start that the
//     original never sets; ours sets them to 0.
//   - 0x00466A20 reads its property value from an uninitialised local when
//     the property call says yes without writing; ours starts it at 0.
//   - The corners of the lot-less rectangle in the group effect pass the
//     cell numbers to the terrain as floats, as the original does.
//
// Time and randomness: none. The effect functions ask the camera's zoom
// (frame-dependent, not comparable) and play sounds and effects (what is
// seen and heard only); everything else here changes city state or reads
// it.
#include "core/address_check.h"
#include "core/demolition.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <utility>

namespace opensc4::demolition {
namespace {

namespace field {
constexpr std::size_t kRefCount = 0xC;
constexpr std::size_t kInitialized = 0x10;
constexpr std::size_t kCityWidth = 0x14;
constexpr std::size_t kCityDepth = 0x18;
constexpr std::size_t kFilter = 0x1C;
constexpr std::size_t kCity = 0x20;
constexpr std::size_t kLotDeveloper = 0x24;
constexpr std::size_t kOccupants = 0x28;
constexpr std::size_t kTerrain = 0x2C;
} // namespace field

namespace filter {
constexpr std::size_t kLimited = 0x10;
constexpr std::size_t kLeft = 0x14;
constexpr std::size_t kTop = 0x18;
constexpr std::size_t kRight = 0x1C;
constexpr std::size_t kBottom = 0x20;
constexpr std::size_t kActive = 0x24;
constexpr std::size_t kMode = 0x28;
constexpr std::size_t kPassed = 0x2C;
constexpr std::size_t kView = 0x30;
} // namespace filter

constexpr std::size_t kMessagePart = 0x4;

constexpr uint32_t kMessageTuning = 0xC53D10AA;
constexpr uint32_t kMessagePostCityInit = 0x26D31EC1;
constexpr uint32_t kMessageLotDemolished = 0xA9DED179;

constexpr uint32_t kTypeFoundation = 0x68FD0C69;
constexpr uint32_t kTypeConstruction = 0x096E763C;
constexpr uint32_t kTypeBuilding = 0x278128A0;
constexpr uint32_t kTypePowerPole = 0x2890D4DE;
constexpr uint32_t kTypeFlora = 0x74758926;
constexpr uint32_t kTypeProp = 0xA823821E;
constexpr uint32_t kTypeNetwork = 0xC772BF98;

constexpr uint32_t kIidFoundation = 0xC8FCE101;
constexpr uint32_t kIidNetworkPiece = 0xA821EF94;
constexpr uint32_t kIidExemplar = 0xA52160F5;

constexpr uint32_t kEffectById = 0x6A3589A0;

using NewFn = void*(__cdecl*)(uint32_t size);
using DeleteFn = void(__cdecl*)(void* block);
using GetUint32Fn = uint8_t(__cdecl*)(void* holder, uint32_t property, uint32_t* value);
using GetBoolFn = uint8_t(__cdecl*)(void* holder, uint32_t property, uint8_t* value);
using VectorFn = void(__stdcall*)(void* first, uint32_t size, uint32_t count, std::uintptr_t construct);

template <typename R, typename... Args>
R This(std::uintptr_t function, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(function)(self, args...);
}

void* OperatorNew(uint32_t size) { return reinterpret_cast<NewFn>(addr::SC4_operator_new)(size); }

void OperatorDelete(void* block) { reinterpret_cast<DeleteFn>(addr::SC4_operator_delete)(block); }

void Free(void* block) { reinterpret_cast<DeleteFn>(addr::SC4_Free)(block); }

uint32_t AddRefOf(void* object) { return Virtual<uint32_t>(object, 0x4); }

uint32_t ReleaseOf(void* object) { return Virtual<uint32_t>(object, 0x8); }

// Three floats set to the float at 0x00B215E8 (zero in every run; copied as
// bits, as the game's own initialisations do).
void FillFromZero(float* values)
{
    for (int i = 0; i < 3; ++i)
        std::memcpy(&values[i], reinterpret_cast<const void*>(addr::SC4_ZeroFloat), sizeof(float));
}

// A cell region on the stack: the rectangle, then the cells part (vtable,
// width, depth, words per column, the columns) at +0x10, 0x24 bytes.
struct CellRegion {
    int32_t rect[4];
    uint32_t cells[5];
};
static_assert(sizeof(CellRegion) == 0x24);

// An effect's placement as the other disasters'.
struct Placement {
    uint8_t first;
    uint8_t second;
    uint8_t unused[2];
    float matrix[9];
    float position[3];
    float scale;
};
static_assert(sizeof(Placement) == 0x38);

// --- Identity -----------------------------------------------------------------------

// The game's 0x00466480.
uint32_t __fastcall DefaultFilterId(char*, void*) { return 0xC9C75EAE; }

// The game's 0x00466490.
uint32_t __fastcall DemolitionId(char*, void*) { return 0x0990C38A; }

// The game's 0x004666F0.
uint32_t __fastcall RealisticFilterId(char*, void*) { return 0x8A7A1C2C; }

// The game's 0x004664B0 (ret 8).
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** out)
{
    char* found;
    if (iid == 0x00000001u || iid == 0x26DDE2EAu)
        found = self;
    else if (iid == 0x452294AAu)
        found = self ? self + 4 : nullptr;
    else if (iid == 0xE4FDA3D4u)
        found = self ? self + 8 : nullptr;
    else
        return 0;
    *out = found;
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// --- The object ---------------------------------------------------------------------

// The game's 0x00466920 (ret 4): the vtables are put back, and the object
// freed when the flag says so. Returns the (maybe freed) object.
void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    At<std::uintptr_t>(self, 0x0) = addr::cSC4Demolition_Vtable;
    At<std::uintptr_t>(self, 0x4) = addr::cSC4Demolition_Vtable4;
    At<std::uintptr_t>(self, 0x8) = addr::cSC4Demolition_Vtable8;
    if (flags & 1)
        OperatorDelete(self);
    return self;
}

// The game's 0x00466950: the count when it stays above 1; else the object
// is shut down and deleted and the answer is 0.
uint32_t __fastcall Release(char* self, void*)
{
    uint32_t count = At<uint32_t>(self, field::kRefCount);
    if (count > 1) {
        --count;
        At<uint32_t>(self, field::kRefCount) = count;
        return count;
    }
    Virtual<uint8_t>(self, 0x10);
    Virtual<void*>(self, 0x30, uint32_t{1});
    return 0;
}

// The game's 0x00466E40, the factory (cdecl, no arguments): the object, or
// null when the allocator fails.
void* __cdecl Factory()
{
    char* const self = static_cast<char*>(OperatorNew(0x34));
    if (!self)
        return nullptr;
    At<std::uintptr_t>(self, 0x0) = addr::cSC4Demolition_Vtable;
    At<std::uintptr_t>(self, 0x4) = addr::cSC4Demolition_Vtable4;
    At<std::uintptr_t>(self, 0x8) = addr::cSC4Demolition_Vtable8;
    At<uint32_t>(self, field::kRefCount) = 0;
    At<uint8_t>(self, field::kInitialized) = 0;
    At<uint32_t>(self, field::kCityWidth) = 0;
    At<uint32_t>(self, field::kCityDepth) = 0;
    At<uint32_t>(self, field::kFilter) = 0;
    At<uint32_t>(self, field::kCity) = 0;
    At<uint32_t>(self, field::kLotDeveloper) = 0;
    At<uint32_t>(self, field::kOccupants) = 0;
    At<uint32_t>(self, field::kTerrain) = 0;
    At<uint32_t>(self, 0x30) = 0;
    return self;
}

// --- The filter ---------------------------------------------------------------------

// The game's 0x00466700: the base, the flags, the two filters empty.
void* __fastcall ConstructFilter(char* self, void*)
{
    This<void*>(addr::SC4_OccupantFilterBase, self);
    At<uint8_t>(self, 0xC) = 0;
    At<uint8_t>(self, 0xD) = 0;
    At<uint8_t>(self, 0xE) = 0;
    At<uint8_t>(self, filter::kLimited) = 0;
    At<std::uintptr_t>(self, 0x0) = addr::cSC4DemolitionFilter_Vtable;
    At<std::uintptr_t>(self, 0x4) = addr::cSC4DemolitionFilter_Vtable4;
    At<uint32_t>(self, filter::kPassed) = 0;
    At<uint32_t>(self, filter::kView) = 0;
    return self;
}

// The game's 0x00466EC0: the two filters given back, then the base.
void __fastcall DestructFilter(char* self, void*)
{
    if (void* const view = At<void*>(self, filter::kView))
        ReleaseOf(view);
    if (void* const passed = At<void*>(self, filter::kPassed))
        ReleaseOf(passed);
    This<void>(addr::SC4OccupantFilterBase_Destroy, self);
}

// The game's 0x00466EA0 (ret 4).
void* __fastcall DeleteFilter(char* self, void*, uint32_t flags)
{
    DestructFilter(self, nullptr);
    if (flags & 1)
        OperatorDelete(self);
    return self;
}

// The game's 0x00466530, slot 3 of the filter ModifyTerrainHeight makes (ret
// 4): a prop, building, power pole or network piece whose exemplar does not
// have the bool 0x29F013E6 set. The bool starts at 0 in the argument's
// slot; the answer is 0 or 1 in EAX.
uint32_t __fastcall TerrainFilterTest(char*, void*, void* occupant)
{
    const uint32_t type = Virtual<uint32_t>(occupant, 0x1C);
    if (type != kTypeProp && type != kTypeBuilding && type != kTypePowerPole && type != kTypeNetwork)
        return 0;
    uint8_t flag = 0;
    reinterpret_cast<GetBoolFn>(addr::SC4_GetPropertyBool)(Virtual<void*>(occupant, 0x18), 0x29F013E6, &flag);
    return flag == 0 ? 1 : 0;
}

// The game's 0x00466730, slot 3 of the demolition filter (ret 4): what a
// bulldozer may take. First the default and realistic filters' tests (the
// realistic one runs the default one); an occupant the default test takes
// and the realistic one refuses is out. Then the caller's filter (+0x2C);
// the rectangle (+0x10) against the occupant's position; the removal test
// (+0x24); then by type. A foundation passes if its interface's slot 9 says
// 1. Building, construction, power pole, flora, prop and network pieces go
// on to the view's filter (+0x30) and, for a network piece, to the view's
// level: unless the view is at level 1 a piece for which the network
// interface's slot 9 (0x1F4F) says yes is out.
uint8_t __fastcall FilterTest(char* self, void*, void* occupant)
{
    if (!This<uint8_t>(addr::cSC4RealisticOccupantFilter_Test, self, occupant)) {
        if (This<uint8_t>(addr::cSC4DefaultOccupantFilter_Test, self, occupant))
            return 0;
    }
    if (void* const passed = At<void*>(self, filter::kPassed)) {
        if (!Virtual<uint8_t>(passed, 0xC, occupant))
            return 0;
    }
    if (At<uint8_t>(self, filter::kLimited)) {
        float position[3];
        FillFromZero(position);
        Virtual<void>(occupant, 0x20, position);
        // x87 compares: "less" is also true for NaN in the first two, and the
        // last two refuse anything but x <= limit.
        if (!(position[0] >= At<float>(self, filter::kLeft)))
            return 0;
        if (!(position[2] >= At<float>(self, filter::kTop)))
            return 0;
        if (!(position[0] <= At<float>(self, filter::kRight)))
            return 0;
        if (!(position[2] <= At<float>(self, filter::kBottom)))
            return 0;
    }
    if (At<uint8_t>(self, filter::kActive)) {
        if (!Virtual<uint8_t>(occupant, 0x38, At<uint32_t>(self, filter::kMode)))
            return 0;
    }
    const uint32_t type = Virtual<uint32_t>(occupant, 0x1C);
    if (type == kTypeFoundation) {
        void* foundation = nullptr;
        Virtual<uint8_t>(occupant, 0x0, kIidFoundation, &foundation);
        const uint32_t answer = Virtual<uint32_t>(foundation, 0x24);
        if (foundation)
            ReleaseOf(foundation);
        return answer == 1 ? 1 : 0;
    }
    if (type != kTypeConstruction && type != kTypeBuilding && type != kTypePowerPole && type != kTypeFlora &&
        type != kTypeProp && type != kTypeNetwork)
        return 0;
    uint8_t accepted = 1;
    if (void* const view = At<void*>(self, filter::kView)) {
        if (!Virtual<uint8_t>(view, 0xC, occupant))
            return 0;
    }
    if (type == kTypeNetwork && Virtual<uint32_t>(Global<void*>(addr::SC4_Service43DC4), 0x58) != 1) {
        void* piece = nullptr;
        void** const out = This<void**>(addr::SC4RefPtr_Reset, &piece);
        if (Virtual<uint8_t>(occupant, 0x0, kIidNetworkPiece, out)) {
            if (Virtual<uint8_t>(piece, 0x24, uint32_t{0x1F4F}))
                accepted = 0;
        }
        if (piece)
            ReleaseOf(piece);
    }
    return accepted;
}

// --- Init, Shutdown, tuning, messages -----------------------------------------------

// The game's 0x00466CD0 (this is never read): the least and greatest sizes
// of an occupant that gets the effects, and the sizes of a group, from the
// exemplar 0x6534284A / 0x690F693F / 0x2A8A09CA, then the cost scale from
// the one at 0x6534284A / 0xE7E2C2DB / 0xE8DA7677. A property that is not
// there leaves its global as it was.
void __fastcall LoadTuning(char*, void*)
{
    const GetUint32Fn getUint32 = reinterpret_cast<GetUint32Fn>(addr::SC4_GetPropertyUint32);
    void* const resources = Global<void*>(addr::SC4_ResourceManager);
    uint32_t firstKey[3] = {0x6534284A, 0x690F693F, 0x2A8A09CA};
    void* first = nullptr;
    if (Virtual<uint8_t>(resources, 0xC, static_cast<uint32_t*>(firstKey), kIidExemplar, &first, uint32_t{0},
                         uint32_t{0})) {
        if (void* const holder = Virtual<void*>(first, 0x1C)) {
            getUint32(holder, 0x6A8A09D2, &Global<uint32_t>(addr::cSC4Demolition_OccupantEffectLow));
            getUint32(holder, 0x6A8A09D3, &Global<uint32_t>(addr::cSC4Demolition_OccupantEffectHigh));
            getUint32(holder, 0x6A8A09D4, &Global<uint32_t>(addr::cSC4Demolition_GroupEffectLow));
            getUint32(holder, 0x6A8A09D5, &Global<uint32_t>(addr::cSC4Demolition_GroupEffectHigh));
        }
    }
    uint32_t secondKey[3] = {0x6534284A, 0xE7E2C2DB, 0xE8DA7677};
    void* second = nullptr;
    if (Virtual<uint8_t>(resources, 0xC, static_cast<uint32_t*>(secondKey), kIidExemplar, &second, uint32_t{0},
                         uint32_t{0})) {
        if (void* const holder = Virtual<void*>(second, 0x1C))
            getUint32(holder, 0x2AB845E7, &Global<uint32_t>(addr::cSC4Demolition_LotCostScale));
    }
    if (second)
        ReleaseOf(second);
    if (first)
        ReleaseOf(first);
}

// The game's 0x00466EF0: takes the services, makes the filter, reads the
// tuning. True also when it was already initialised.
uint8_t __fastcall Init(char* self, void*)
{
    if (At<uint8_t>(self, field::kInitialized))
        return 1;
    At<uint8_t>(self, field::kInitialized) = 1;
    void* const server = Global<void*>(addr::SC4_MessageServer);
    void* const target = self + kMessagePart;
    Virtual<uint8_t>(server, 0x14, target, kMessageTuning);
    Virtual<uint8_t>(server, 0x14, target, kMessagePostCityInit);
    Virtual<uint8_t>(server, 0x14, target, kMessageLotDemolished);
    if (!At<void*>(self, field::kCity)) {
        void* const city = Global<void*>(addr::SC4_City);
        At<void*>(self, field::kCity) = city;
        AddRefOf(city);
        At<uint32_t>(self, field::kCityWidth) = Virtual<uint32_t>(At<void*>(self, field::kCity), 0x174);
        At<uint32_t>(self, field::kCityDepth) = Virtual<uint32_t>(At<void*>(self, field::kCity), 0x178);
    }
    if (!At<void*>(self, field::kOccupants)) {
        void* const occupants = Global<void*>(addr::SC4_NetworkService);
        At<void*>(self, field::kOccupants) = occupants;
        AddRefOf(occupants);
    }
    if (!At<void*>(self, field::kLotDeveloper)) {
        void* const developer = Virtual<void*>(Global<void*>(addr::SC4_Service43D68), 0x90);
        At<void*>(self, field::kLotDeveloper) = developer;
        AddRefOf(developer);
    }
    if (!At<void*>(self, field::kTerrain)) {
        void* const terrain = Global<void*>(addr::SC4_Terrain);
        At<void*>(self, field::kTerrain) = terrain;
        AddRefOf(terrain);
    }
    if (!At<void*>(self, field::kFilter)) {
        void* const memory = OperatorNew(0x34);
        void* const made = memory ? ConstructFilter(static_cast<char*>(memory), nullptr) : nullptr;
        At<void*>(self, field::kFilter) = made;
        AddRefOf(made);
    }
    LoadTuning(self, nullptr);
    return 1;
}

// The game's 0x00466980: gives the references back in the order filter,
// terrain, lot developer, occupant manager, city, and takes the message
// target out of the three messages.
uint8_t __fastcall Shutdown(char* self, void*)
{
    if (!At<uint8_t>(self, field::kInitialized))
        return 1;
    for (const std::size_t offset :
         {field::kFilter, field::kTerrain, field::kLotDeveloper, field::kOccupants, field::kCity}) {
        if (void* const held = At<void*>(self, offset)) {
            ReleaseOf(held);
            At<void*>(self, offset) = nullptr;
        }
    }
    void* const server = Global<void*>(addr::SC4_MessageServer);
    void* const target = self + kMessagePart;
    Virtual<uint8_t>(server, 0x18, target, kMessageTuning);
    Virtual<uint8_t>(server, 0x18, target, kMessagePostCityInit);
    Virtual<uint8_t>(server, 0x18, target, kMessageLotDemolished);
    At<uint8_t>(self, field::kInitialized) = 0;
    return 1;
}

// The game's 0x00466FF0 (ret 4), slot 3 of the message target (this + 4).
// 0xC53D10AA: the tuning is read again. 0xA9DED179: the message's data is
// a block of a lot (referenced) and a byte; if the lot is initialised the
// lot developer finishes its demolition, with the byte set the lot's whole
// rectangle is dezoned (zone manager PlaceZone, type 0, placing, no funds
// asked, whole lots) and the lot is given back; the block is freed in every
// case. Always true.
uint8_t __fastcall DoMessage(char* part, void*, void* message)
{
    char* const self = part - kMessagePart;
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    if (type == kMessageTuning) {
        LoadTuning(self, nullptr);
        return 1;
    }
    if (type != kMessageLotDemolished)
        return 1;
    char* const data = Virtual<char*>(message, 0x24);
    void* const lot = At<void*>(data, 0);
    if (lot && Virtual<uint8_t>(lot, 0x18)) {
        Virtual<uint8_t>(At<void*>(self, field::kLotDeveloper), 0x34, lot);
        if (At<uint8_t>(data, 4)) {
            int32_t rect[4];
            Virtual<uint8_t>(lot, 0x38, static_cast<int32_t*>(rect));
            CellRegion region;
            This<void*>(addr::SC4CellRegion_Construct, &region, static_cast<const int32_t*>(rect), uint32_t{1});
            Virtual<uint8_t>(Global<void*>(addr::SC4_ZoneManager), 0x2C, static_cast<void*>(&region), uint32_t{0},
                             uint32_t{1}, uint32_t{1}, uint32_t{0}, uint32_t{1}, uint32_t{1},
                             static_cast<int64_t*>(nullptr), static_cast<int32_t*>(nullptr),
                             static_cast<void*>(nullptr));
            region.cells[0] = static_cast<uint32_t>(addr::SC4CellRegion_CellsVtable);
            This<void>(addr::SC4CellRegion_DestroyCells, region.cells);
        }
        ReleaseOf(lot);
    }
    OperatorDelete(data);
    return 1;
}

// --- Helpers of the big functions ---------------------------------------------------

// The game's 0x00466B90 (ret 8): the occupants in the rectangle (x1, z1, x2,
// z2 in cells) go into the list. The filter is told the rectangle in metres
// (the positions of the corner x1, z1 and the corner x2 + 1, z2 + 1) and
// limits itself to it for the query, which asks the occupant manager by
// standard city cells with the filter and -1.
void __fastcall CollectOccupants(char* self, void*, const int32_t* rect, void* list)
{
    void* const city = At<void*>(self, field::kCity);
    uint32_t low[2];
    Virtual<void>(city, 0x180, rect[0], rect[1], &low[0], &low[1]);
    uint32_t high[2];
    Virtual<void>(city, 0x180, rect[2] + 1, rect[3] + 1, &high[0], &high[1]);
    int32_t corners[4] = {rect[2], rect[3], rect[0], rect[1]};
    char* const limit = At<char*>(self, field::kFilter);
    At<uint8_t>(limit, filter::kLimited) = 1;
    At<uint32_t>(limit, filter::kLeft) = low[0];
    At<uint32_t>(limit, filter::kTop) = low[1];
    At<uint32_t>(limit, filter::kRight) = high[0];
    At<uint32_t>(limit, filter::kBottom) = high[1];
    Virtual<uint8_t>(At<void*>(self, field::kOccupants), 0xA0, list, &corners[2], &corners[0],
                     At<void*>(self, field::kFilter), int32_t{-1});
    At<uint8_t>(At<char*>(self, field::kFilter), filter::kLimited) = 0;
}

// The game's 0x00466C50 (ret 0xC; this is never read): a message of the
// given type with the data set twice (slots 0x30 and 0x84), posted (normal
// priority) when the byte is set and sent otherwise; the reference is given
// back.
void __fastcall SendNewMessage(char*, void*, uint32_t type, void* data, uint32_t post)
{
    void* const server = Global<void*>(addr::SC4_MessageServer);
    void* const memory = reinterpret_cast<NewFn>(addr::RZ_operator_new)(0x2C);
    void* const message = memory ? This<void*>(addr::cGZMessage_cGZMessage, memory) : nullptr;
    AddRefOf(message);
    Virtual<void>(message, 0x14, type);
    Virtual<void>(message, 0x30, data);
    Virtual<void>(message, 0x84, data);
    if (static_cast<uint8_t>(post))
        Virtual<uint8_t>(server, 0x10, message, uint32_t{0});
    else
        Virtual<uint8_t>(server, 0xC, message);
    ReleaseOf(message);
}

// The game's 0x00467A50: empties and frees a std::set of referenced lots
// (a head node at +0 with the root at +4, the leftmost at +8 and the
// rightmost at +0xC; the count at +4 of the set).
void __fastcall DestroyLotSet(char* self, void*)
{
    if (At<uint32_t>(self, 4)) {
        This<void>(addr::SC4RefSet_EraseSubtree, self, At<void*>(At<char*>(self, 0), 4));
        At<char*>(At<char*>(self, 0), 8) = At<char*>(self, 0);
        At<uint32_t>(At<char*>(self, 0), 4) = 0;
        At<char*>(At<char*>(self, 0), 0xC) = At<char*>(self, 0);
        At<uint32_t>(self, 4) = 0;
    }
    if (char* const head = At<char*>(self, 0))
        Free(head);
}

// The game's 0x00467AA0 (ret 8): the lot's occupants go into the list, and
// every one the default filter (+0x1C) refuses is taken out of it again (its
// node returned to the node allocator, the occupant released). Nodes are
// linked (next, previous, value at +8) around the head node the list
// points to; the list keeps no count.
void __fastcall DropRefusedOccupants(char* self, void*, void* lot, char* list)
{
    Virtual<uint8_t>(lot, 0xC4, list);
    char* const head = At<char*>(list, 0);
    char* node = At<char*>(head, 0);
    while (node != At<char*>(list, 0)) {
        void* const occupant = At<void*>(node, 8);
        if (Virtual<uint8_t>(At<void*>(self, field::kFilter), 0xC, occupant)) {
            node = At<char*>(node, 0);
            continue;
        }
        char* const next = At<char*>(node, 0);
        char* const previous = At<char*>(node, 4);
        At<char*>(previous, 0) = next;
        At<char*>(next, 4) = previous;
        Virtual<void>(ListNodeAllocator(), 0x10, static_cast<void*>(node));
        ReleaseOf(occupant);
        node = next;
    }
}

// --- DemolishLots -------------------------------------------------------------------

// The game's 0x004670C0, slot 7 (ret 0x20): demolish a list of lots. The
// region is the rectangle round all of them (the lots' bounding rectangles
// merged, the first lot's twice), with a bit set for every cell of every
// lot that is in a demolishable state (lot slot 39). Then DemolishRegion
// (slot 6) with the same arguments and 0x2000 added to the flags, and the
// region destroyed. An empty list is true at once.
uint8_t __fastcall DemolishLots(char* self, void*, uint32_t demolish, char* lots, int32_t privilege, uint32_t flags,
                                uint32_t clearZoned, void* filterArgument, void* cost, void* taken)
{
    char* const head = At<char*>(lots, 0);
    if (At<char*>(head, 0) == head)
        return 1;
    flags |= 0x2000;
    int32_t bounds[4];
    Virtual<uint8_t>(At<void*>(At<char*>(head, 0), 8), 0x38, static_cast<int32_t*>(bounds));
    for (char* node = At<char*>(At<char*>(lots, 0), 0); node != At<char*>(lots, 0); node = At<char*>(node, 0)) {
        int32_t each[4];
        Virtual<uint8_t>(At<void*>(node, 8), 0x38, static_cast<int32_t*>(each));
        if (!(bounds[0] < each[0]))
            bounds[0] = each[0];
        if (!(bounds[1] < each[1]))
            bounds[1] = each[1];
        if (!(bounds[2] > each[2]))
            bounds[2] = each[2];
        if (!(bounds[3] > each[3]))
            bounds[3] = each[3];
    }
    CellRegion region;
    region.rect[0] = bounds[0];
    region.rect[1] = bounds[1];
    region.rect[2] = bounds[2];
    region.rect[3] = bounds[3];
    const uint8_t fill = 0;
    This<void*>(addr::SC4CellRegion_ConstructCells, region.cells, static_cast<uint32_t>(bounds[2] - bounds[0] + 1),
                static_cast<uint32_t>(bounds[3] - bounds[1] + 1), &fill);
    for (char* node = At<char*>(At<char*>(lots, 0), 0); node != At<char*>(lots, 0); node = At<char*>(node, 0)) {
        void* const lot = At<void*>(node, 8);
        if (!Virtual<uint8_t>(lot, 0x9C))
            continue;
        int32_t each[4];
        Virtual<uint8_t>(lot, 0x38, static_cast<int32_t*>(each));
        uint32_t** const columns = reinterpret_cast<uint32_t**>(region.cells[4]);
        for (int32_t x = each[0]; x <= each[2]; ++x) {
            for (int32_t z = each[1]; z <= each[3]; ++z) {
                const uint32_t bit = static_cast<uint32_t>(z - region.rect[1]);
                columns[x - region.rect[0]][bit >> 5] |= 1u << (bit & 31);
            }
        }
    }
    const uint8_t result = Virtual<uint8_t>(self, 0x18, demolish, static_cast<void*>(&region), privilege, flags,
                                            clearZoned, filterArgument, cost, taken, uint32_t{0}, uint32_t{0},
                                            uint32_t{0});
    region.cells[0] = static_cast<uint32_t>(addr::SC4CellRegion_CellsVtable);
    This<void>(addr::SC4CellRegion_DestroyCells, region.cells);
    return result;
}

// The game's 0x004677F0, a function of the register convention (ECX the
// lot, AL the flag, two stack arguments the caller pops): called by
// DemolishRegion for each lot it finishes, with the call's flags and the
// lot's zone type. With the flag set, or for the zone types 11, 12 and 15,
// the lot's whole rectangle is dezoned (PlaceZone, type 0, placing, no
// funds asked, whole lots, over a region with every bit set). Otherwise,
// unless the flags have 0x800 and only for the zone types 7 to 9: the lot
// is given up to the lot manager (slot 24) and each of its cells that is
// still of that zone type gets a new one-cell lot (the lot manager's slot
// 22, with the lot's facing), which is given back at once.
void __cdecl DezoneLot(char* lot, uint32_t clearZoned, uint32_t flags, int32_t zoneType)
{
    void* const zones = Global<void*>(addr::SC4_ZoneManager);
    if (static_cast<uint8_t>(clearZoned) || ((1u << (static_cast<uint32_t>(zoneType) & 31)) & 0x9800u) != 0) {
        int32_t rect[4];
        Virtual<uint8_t>(lot, 0x38, static_cast<int32_t*>(rect));
        CellRegion region;
        region.rect[0] = rect[0];
        region.rect[1] = rect[1];
        region.rect[2] = rect[2];
        region.rect[3] = rect[3];
        const uint8_t fill = 1;
        This<void*>(addr::SC4CellRegion_ConstructCells, region.cells, static_cast<uint32_t>(rect[2] - rect[0] + 1),
                    static_cast<uint32_t>(rect[3] - rect[1] + 1), &fill);
        Virtual<uint8_t>(zones, 0x2C, static_cast<void*>(&region), uint32_t{0}, uint32_t{1}, uint32_t{1}, uint32_t{0},
                         uint32_t{1}, uint32_t{1}, static_cast<int64_t*>(nullptr), static_cast<int32_t*>(nullptr),
                         static_cast<void*>(nullptr));
        region.cells[0] = static_cast<uint32_t>(addr::SC4CellRegion_CellsVtable);
        This<void>(addr::SC4CellRegion_DestroyCells, region.cells);
        return;
    }
    if (flags & 0x800)
        return;
    if (zoneType < 7 || zoneType > 9)
        return;
    int32_t rect[4];
    Virtual<uint8_t>(lot, 0x38, static_cast<int32_t*>(rect));
    const uint32_t facing = Virtual<uint32_t>(lot, 0x5C);
    void* const lotManager = Global<void*>(addr::SC4_LotManager);
    Virtual<uint32_t>(lotManager, 0x60, static_cast<void*>(lot));
    for (int32_t x = rect[0]; x <= rect[2]; ++x) {
        for (int32_t z = rect[1]; z <= rect[3]; ++z) {
            if (Virtual<uint32_t>(zones, 0x30, x, z) != static_cast<uint32_t>(zoneType))
                continue;
            void* created = nullptr;
            Virtual<uint8_t>(lotManager, 0x58, x, z, uint32_t{1}, uint32_t{1}, facing, &created, uint32_t{0},
                             uint32_t{0}, uint32_t{0});
            if (created)
                ReleaseOf(created);
        }
    }
}

__declspec(naked) void DezoneLotThunk()
{
    __asm push dword ptr [esp + 8]
    __asm push dword ptr [esp + 8]
    __asm movzx eax, al
    __asm push eax
    __asm push ecx
    __asm call DezoneLot
    __asm add esp, 16
    __asm ret
}

// --- The effects --------------------------------------------------------------------

// The game's 0x004672E0 (ECX the occupant, ESI an optional float out): the
// largest side of the occupant's bounding box (each side the float
// difference of the corners, the first greatest kept), stored in `size`;
// true when it times the camera's zoom factor (+0x154 of the camera view) is
// below the limit in the application's +0x24 object (+0x15C): too small
// for an effect. NaN is not smaller.
bool TooSmallForEffect(void* occupant, float* size)
{
    float low[3] = {1.0f, 1.0f, 1.0f};
    float high[3] = {0.0f, 0.0f, 0.0f};
    Virtual<void*>(occupant, 0x28, static_cast<float*>(low), static_cast<float*>(high));
    float sides[3];
    for (int i = 0; i < 3; ++i)
        sides[i] = static_cast<float>(static_cast<double>(high[i]) - static_cast<double>(low[i]));
    const float* greatest = &sides[0];
    for (const float* each = &sides[1]; each != sides + 3; ++each) {
        if (*greatest < *each)
            greatest = each;
    }
    if (size)
        *size = *greatest;
    const float zoom = At<float>(Global<void*>(addr::SC4_CameraView), 0x154);
    const float limit = At<float>(At<void*>(Global<void*>(addr::SC4_App43CC4), 0x24), 0x15C);
    return static_cast<double>(*greatest) * static_cast<double>(zoom) < static_cast<double>(limit);
}

// The game's 0x00466A90 (ESI the occupant, EDI the flags word): adds the
// kind bits for the effect: 0x10 for a building or a power pole, 0x20 for
// flora; for a network type (0xC772BF98) 0x40, or 0x50 when its interface's
// slot 22 says no for 2 and yes for 1; for a prop 0x20 when it is in group
// 0x5004 and 0x30 when not, false when its bool 0x8A5E5DB8 is missing or
// clear. Any other type leaves the flags and is true.
bool EffectKind(void* occupant, uint32_t* flags)
{
    const uint32_t type = Virtual<uint32_t>(occupant, 0x1C);
    if (type == kTypeBuilding || type == kTypePowerPole) {
        *flags |= 0x10;
        return true;
    }
    if (type == kTypeFlora) {
        *flags |= 0x20;
        return true;
    }
    if (type == kTypeNetwork) {
        void* piece = nullptr;
        uint32_t value = *flags;
        if (Virtual<uint8_t>(occupant, 0x0, kIidNetworkPiece, &piece)) {
            if (!Virtual<uint8_t>(piece, 0x58, uint32_t{2}))
                value |= Virtual<uint8_t>(piece, 0x58, uint32_t{1}) ? 0x50u : 0x40u;
            else
                value |= 0x40;
        } else {
            value |= 0x40;
        }
        *flags = value;
        if (piece)
            ReleaseOf(piece);
        return true;
    }
    if (type == kTypeProp) {
        uint8_t flag = 0;
        if (!reinterpret_cast<GetBoolFn>(addr::SC4_GetPropertyBool)(Virtual<void*>(occupant, 0x18), 0x8A5E5DB8, &flag))
            return false;
        if (!flag)
            return false;
        const bool grouped = Virtual<uint8_t>(occupant, 0x58, uint32_t{0x5004}) != 0;
        *flags |= grouped ? 0x20u : 0x30u;
        return true;
    }
    return true;
}

// The game's 0x00466A20 (ECX the occupant): if the occupant's exemplar has
// the property 0x0A93036F, the sound service plays it (slot 0x2C with 1 and
// the value).
void __fastcall PlayDemolishSound(char* occupant, void*)
{
    void* const holder = Virtual<void*>(occupant, 0x18);
    if (holder)
        AddRefOf(holder);
    uint32_t sound = 0;
    if (reinterpret_cast<GetUint32Fn>(addr::SC4_GetPropertyUint32)(holder, 0x0A93036F, &sound)) {
        if (void* const sounds = Global<void*>(addr::SC4_Sound43CB8)) {
            AddRefOf(sounds);
            Virtual<void>(sounds, 0x2C, uint32_t{1}, sound, uint32_t{0}, uint32_t{0}, uint32_t{0});
            ReleaseOf(sounds);
        }
    }
    if (holder)
        ReleaseOf(holder);
}

// The game's 0x004673B0, slot 10 (ret 0x18; this is never read): the effect
// for a demolished occupant. Nothing when it is too small for the camera.
// The flags are the kind byte shifted up eight plus the kind bits (above),
// and the sound plays; then 1 or 2 when the occupant's size is above the
// two limits of the tuning. The effects manager (slot 0x18, by 0x6A3589A0
// and this key) makes the effect; when none exists for a key, the key is
// lowered by one until its low nibble is 0. The effect (slot 9) takes the
// occupant; if it does, with `aim` set it gets the screen ray's two points
// (the camera view's 0x007F9080 with x and z) as its parameter 9, for a lot
// whose condition is neither 0 nor 1 it gets parameter 4 (the zero at
// 0x00B215EC, 3), and it starts (slot 3, 0). The effect is given back.
void __fastcall DoDemolishOccupantEffect(char*, void*, void* occupant, uint32_t kind, void* lot, uint32_t aim,
                                         int32_t x, int32_t z)
{
    float size;
    if (TooSmallForEffect(occupant, &size))
        return;
    uint32_t flags = (kind & 0xFF) << 8;
    if (!EffectKind(occupant, &flags))
        return;
    PlayDemolishSound(static_cast<char*>(occupant), nullptr);
    uint32_t key = flags;
    if (static_cast<double>(Global<uint32_t>(addr::cSC4Demolition_OccupantEffectHigh)) < static_cast<double>(size))
        key |= 2;
    else if (static_cast<double>(Global<uint32_t>(addr::cSC4Demolition_OccupantEffectLow)) < static_cast<double>(size))
        key |= 1;
    void* effect = nullptr;
    for (;;) {
        Virtual<uint8_t>(Global<void*>(addr::SC4_Service43D1C), 0x18, kEffectById, key, &effect);
        if (effect)
            break;
        const uint32_t nibble = key & 0xF;
        --key;
        if (nibble == 0)
            return;
    }
    if (Virtual<uint8_t>(effect, 0x24, occupant)) {
        if (static_cast<uint8_t>(aim)) {
            float ray[6];
            reinterpret_cast<VectorFn>(addr::SC4UI_F41D490)(ray, 0xC, 2, addr::cSC4Advice_F409670);
            This<uint8_t>(addr::SC4CameraView_ScreenRay, Global<void*>(addr::SC4_CameraView), x, z, &ray[0], &ray[3]);
            Virtual<void>(effect, 0x2C, uint32_t{9}, static_cast<float*>(ray), uint32_t{2});
        }
        if (lot) {
            if (Virtual<uint32_t>(lot, 0x8C) != 0 && Virtual<uint32_t>(lot, 0x8C) != 1)
                Virtual<void>(effect, 0x30, uint32_t{4}, reinterpret_cast<void*>(addr::SC4_ZeroDword), uint32_t{3});
        }
        Virtual<void>(effect, 0xC, uint32_t{0});
    }
    ReleaseOf(effect);
}

// The game's 0x00467530, slot 11 (ret 0x10; this and the third argument are
// never read): the effect for a group of demolished occupants in the cell
// rectangle (x1, z1, x2, z2). The place is the position of the middle cell
// (the occupant manager's slot 0x48, from the sums of the corners halved
// toward zero) at the height the terrain gives for the cell numbers as
// floats. The key is the kind byte shifted up eight plus 2 when the count is
// above the tuning's greater group limit or 1 when above the lesser. The
// effect (slot 0x18 of the effects manager) is placed at that spot, scale
// 1 and no rotation (slot 7), and started (slot 3, 0), and given back.
void __fastcall DoDemolishGroupEffect(char*, void*, uint32_t kind, uint32_t count, int32_t, const int32_t* rect)
{
    float spot[3];
    FillFromZero(spot);
    const int32_t sumX = rect[0] + rect[2];
    const int32_t sumZ = rect[1] + rect[3];
    const int32_t cellX = (sumX - (sumX >> 31)) >> 1;
    const int32_t cellZ = (sumZ - (sumZ >> 31)) >> 1;
    Virtual<uint8_t>(Global<void*>(addr::SC4_NetworkService), 0x48, cellX, cellZ, &spot[0], &spot[2]);
    const double height = Virtual<double>(Global<void*>(addr::SC4_Terrain), 0x34, static_cast<float>(cellX),
                                          static_cast<float>(cellZ));
    spot[1] = static_cast<float>(height);
    uint32_t key = (kind & 0xFF) << 8;
    if (count > Global<uint32_t>(addr::cSC4Demolition_GroupEffectHigh))
        key |= 2;
    else if (count > Global<uint32_t>(addr::cSC4Demolition_GroupEffectLow))
        key |= 1;
    void* effect = nullptr;
    if (Virtual<uint8_t>(Global<void*>(addr::SC4_Service43D1C), 0x18, kEffectById, key, &effect)) {
        Placement placement;
        std::memset(&placement, 0, sizeof(placement));
        placement.first = 1;
        placement.second = 1;
        placement.matrix[0] = 1.0f;
        placement.matrix[4] = 1.0f;
        placement.matrix[8] = 1.0f;
        placement.position[0] = spot[0];
        placement.position[1] = spot[1];
        placement.position[2] = spot[2];
        placement.scale = 1.0f;
        Virtual<void>(effect, 0x1C, &placement);
        Virtual<void>(effect, 0xC, uint32_t{0});
    }
    if (effect)
        ReleaseOf(effect);
}

// --- Registration -------------------------------------------------------------------

bool g_effectOurs = false;

bool InstallDemolition1(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[40] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cSC4DefaultOccupantFilter_GetGZCLSID, reinterpret_cast<void*>(&DefaultFilterId)},
        {addr::cSC4Demolition_GetGZCLSID, reinterpret_cast<void*>(&DemolitionId)},
        {addr::cSC4Demolition_QueryInterface, reinterpret_cast<void*>(&QueryInterface)},
        {addr::cSC4DemolitionTerrainFilter_Test, reinterpret_cast<void*>(&TerrainFilterTest)},
        {addr::cSC4RealisticOccupantFilter_GetGZCLSID, reinterpret_cast<void*>(&RealisticFilterId)},
        {addr::cSC4DemolitionFilter_cSC4DemolitionFilter, reinterpret_cast<void*>(&ConstructFilter)},
        {addr::cSC4DemolitionFilter_Test, reinterpret_cast<void*>(&FilterTest)},
        {addr::cSC4Demolition_DeletingDestructor, reinterpret_cast<void*>(&DeletingDestructor)},
        {addr::cSC4Demolition_Release, reinterpret_cast<void*>(&Release)},
        {addr::cSC4Demolition_Shutdown, reinterpret_cast<void*>(&Shutdown)},
        {addr::cSC4Demolition_PlayDemolishSound, reinterpret_cast<void*>(&PlayDemolishSound)},
        {addr::cSC4Demolition_CollectOccupants, reinterpret_cast<void*>(&CollectOccupants)},
        {addr::cSC4Demolition_SendNewMessage, reinterpret_cast<void*>(&SendNewMessage)},
        {addr::cSC4Demolition_LoadTuning, reinterpret_cast<void*>(&LoadTuning)},
        {addr::cSC4Demolition_Factory, reinterpret_cast<void*>(&Factory)},
        {addr::cSC4DemolitionFilter_DeletingDestructor, reinterpret_cast<void*>(&DeleteFilter)},
        {addr::cSC4DemolitionFilter_Destruct, reinterpret_cast<void*>(&DestructFilter)},
        {addr::cSC4Demolition_Init, reinterpret_cast<void*>(&Init)},
        {addr::cSC4Demolition_DoMessage, reinterpret_cast<void*>(&DoMessage)},
        {addr::cSC4Demolition_DemolishLots, reinterpret_cast<void*>(&DemolishLots)},
        {addr::cSC4Demolition_DoDemolishOccupantEffect, reinterpret_cast<void*>(&DoDemolishOccupantEffect)},
        {addr::cSC4Demolition_DoDemolishGroupEffect, reinterpret_cast<void*>(&DoDemolishGroupEffect)},
        {addr::cSC4Demolition_DezoneLot, reinterpret_cast<void*>(&DezoneLotThunk)},
        {addr::SC4LotPtrSet_Destroy, reinterpret_cast<void*>(&DestroyLotSet)},
        {addr::cSC4Demolition_DropRefusedOccupants, reinterpret_cast<void*>(&DropRefusedOccupants)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    static_assert(count <= sizeof(originals) / sizeof(originals[0]));
    // All entries are found before the first hook goes in, so a missing one
    // cannot leave a part of the table installed (a hook that fails to
    // install after that is not rolled back: the hook layer cannot remove one).
    const addr::Entry* entries[count];
    for (std::size_t i = 0; i < count; ++i) {
        entries[i] = addr_check::Find(hooks[i].first);
        if (!entries[i])
            return false;
    }
    for (std::size_t i = 0; i < count; ++i) {
        const bool effect = hooks[i].first == addr::cSC4Demolition_DoDemolishOccupantEffect;
        // Ours installs before game.quiet.demolish (PreAppInit), so a detour
        // already here is a foreign patch: skip this hook, keep the others.
        if (effect && hook::IsInstalledAt(entries[i]->address)) {
            log::Write("demolition: DoDemolishOccupantEffect is already hooked; ours not installed there");
            continue;
        }
        if (!hook::Install(*entries[i], hooks[i].second, &originals[i]))
            return false;
        g_effectOurs = g_effectOurs || effect;
    }
    return true;
}

} // namespace

std::uintptr_t EffectEntry() { return reinterpret_cast<std::uintptr_t>(&DoDemolishOccupantEffect); }
bool EffectIsOurs() { return g_effectOurs; }

namespace {

const bool g_demolition1Registered = registry::Register({
    "simulators.demolition.part1",
    "simulators/demolition",
    registry::Kind::Detour,
    registry::Phase::PreAppInit, // before game.quiet.demolish wraps the effect (demolition.h)
    addr::cSC4Demolition_Init,
    "Our demolition service, small half: object, Init, Shutdown, tuning, messages, DemolishLots, effects, filter "
    "(src/core/demolition_1.cpp)",
    false,
    InstallDemolition1,
});

} // namespace
} // namespace opensc4::demolition
