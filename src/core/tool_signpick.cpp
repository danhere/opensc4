// The sign removal tool of the city view (0x004D7680 to 0x004D7AE2).
// Replacement, off by default:
//
//   ui.tools.signpick: the view input control of 0x30 bytes (vtable
//     0x00A928A0, ID 0xCBCC987E) the view makes on its command 0xABCDEE11,
//     and the occupant filter of its picks (0xC bytes, vtables 0x00A92880
//     and 0x00A92868).
//
// The tool removes signs. While the pointer moves, the sign under it (a prop
// occupant whose kind is a sign on the terrain, 0xAB72FBB3, or a sign on a
// network, 0xCB79919B) is kept at +0x2C and marked: a sign on the terrain has
// its flag set (interface 0x4B44FBE2, slot 6), a sign on a network gets 0.7
// (interface 0x2B3B7D86, slot 10; 0.5 again when the pointer leaves it).
// Letting the left button go removes the kept sign through the prop manager
// (slot 29) and plays a sound. Init also turns the city's signs and labels on
// (the prop developer's slot 8).
//
// Fields: +0x28 the filter (referenced), +0x2C the sign under the pointer
// (its interface 0xE9793A65, referenced). The layout is the cheat tool's
// (0x00A92AC0), whose destructor body 0x004DA180 and deleting destructor
// 0x004D7AA0 this class shares; both are ours already (tool_zone.cpp,
// tool_base.cpp).
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::tool_signpick {
namespace {

// The base class's fields.
namespace base {
constexpr std::size_t kInited = 0x04; // byte
constexpr std::size_t kId = 0x0C;
constexpr std::size_t kCursor = 0x10; // cursor ID
} // namespace base

// The tool's fields.
namespace field {
constexpr std::size_t kFilter = 0x28; // the occupant filter of the picks (referenced)
constexpr std::size_t kSign = 0x2C;   // the sign under the pointer (interface 0xE9793A65, referenced)
} // namespace field

constexpr uint32_t kToolId = 0xCBCC987E;
constexpr uint32_t kCursorId = 0x8BD4B428;
constexpr uint32_t kTypeProp = 0xA823821E;
constexpr uint32_t kIidProp = 0xE9793A65;
constexpr uint32_t kSignOnTerrain = 0xAB72FBB3;
constexpr uint32_t kSignOnNetwork = 0xCB79919B;
constexpr uint32_t kIidTerrainSign = 0x4B44FBE2;
constexpr uint32_t kIidNetworkSign = 0x2B3B7D86;
constexpr uint32_t kSoundPlay = 0xEA451903;
constexpr uint32_t kSoundRemoved = 0x8A5EC047;

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

void AddRef(void* object)
{
    Virtual<uint32_t>(object, 0x4);
}

void Release(void* object)
{
    Virtual<uint32_t>(object, 0x8);
}

// Puts an object into a counted field: the new one referenced first, the old
// one released after. Nothing when they are the same.
void Hold(char* self, std::size_t field, void* object)
{
    void* const old = At<void*>(self, field);
    if (object == old)
        return;
    if (object)
        AddRef(object);
    At<void*>(self, field) = object;
    if (old)
        Release(old);
}

// The game's 0x004D7680 (slot 3 of the filter, ret 4): whether the picks
// take an occupant. A prop occupant (type 0xA823821E) whose prop interface
// says it is a sign on the terrain or on a network. `this` is not read.
uint8_t __fastcall FilterIncludes(char*, void*, void* occupant)
{
    if (Virtual<uint32_t>(occupant, 0x1C) != kTypeProp)
        return 0;
    void* prop = nullptr;
    if (Virtual<uint8_t>(occupant, 0x0, kIidProp, &prop)) {
        const uint32_t kind = Virtual<uint32_t>(prop, 0x1C);
        if (kind == kSignOnTerrain || kind == kSignOnNetwork) {
            if (prop)
                Release(prop);
            return 1;
        }
    }
    if (prop)
        Release(prop);
    return 0;
}

// The game's 0x004D76F0: the constructor; the caller allocated 0x30 bytes.
char* __fastcall Construct(char* self, void*)
{
    This<void*>(addr::SC4ViewInputControl_Construct, self);
    At<std::uintptr_t>(self, 0) = addr::SC4SignRemoveTool_Vtable;
    At<void*>(self, field::kFilter) = nullptr;
    At<void*>(self, field::kSign) = nullptr;
    return self;
}

// The game's 0x004D7710 (slot 3): Init. The ID and the cursor, a new filter
// kept at +0x28, the city's signs and labels shown (the prop developer's
// slot 8 with true), then the base Init.
uint8_t __fastcall Init(char* self, void*)
{
    if (At<uint8_t>(self, base::kInited))
        return 1;
    At<uint32_t>(self, base::kId) = kToolId;
    At<uint32_t>(self, base::kCursor) = kCursorId;
    char* filter = Cdecl<char*>(addr::SC4_operator_new, std::size_t{0xC});
    if (filter) {
        This<void*>(addr::SC4_OccupantFilterBase, filter);
        At<std::uintptr_t>(filter, 0x0) = addr::SC4SignRemoveTool_FilterVtable;
        At<std::uintptr_t>(filter, 0x4) = addr::SC4SignRemoveTool_FilterVtable4;
    }
    Hold(self, field::kFilter, filter);
    Virtual<void>(Global<void*>(addr::SC4_Service43DB8), 0x20, uint32_t{1}); // ShowSignsAndLabels
    This<uint8_t>(addr::SC4ViewInputControl_Init, self);
    return 1;
}

// The game's 0x004D7790 (slot 4): Shutdown. Lets go of the filter (the field
// cleared first), then the base. The sign at +0x2C is left to the
// destructor.
uint8_t __fastcall Shutdown(char* self, void*)
{
    if (!At<uint8_t>(self, base::kInited))
        return 1;
    if (void* const filter = At<void*>(self, field::kFilter)) {
        At<void*>(self, field::kFilter) = nullptr;
        Release(filter);
    }
    This<uint8_t>(addr::SC4ViewInputControl_Shutdown, self);
    return 1;
}

// The game's 0x004D77C0 (slot 18, OnMouseUpL, ret 0xC). With a sign under
// the pointer: the prop manager's slot 29 (RemoveProp) gets its occupant
// (the prop interface's slot 3), the tool lets go of it (the field cleared
// first), and the sound service, when there is one, plays 0x8A5EC047. True
// always.
uint8_t __fastcall OnMouseUpL(char* self, void*, int32_t, int32_t, uint32_t)
{
    if (void* const sign = At<void*>(self, field::kSign)) {
        void* const props = Global<void*>(addr::cSC4PropManager_Global);
        Virtual<void>(props, 0x74, Virtual<void*>(sign, 0xC));
        if (void* const held = At<void*>(self, field::kSign)) {
            At<void*>(self, field::kSign) = nullptr;
            Release(held);
        }
        if (void* const sound = Global<void*>(addr::SC4_Sound43CB8))
            Virtual<void>(sound, 0x2C, kSoundPlay, kSoundRemoved, uint32_t{0}, uint32_t{0}, uint32_t{0});
    }
    return 1;
}

// The game's 0x004D7820 (ret 8): the sign at a screen point, or null. First
// the renderer's pick of a model with the tool's filter (slot 0x104): the
// model's owner (slot 3) is asked for the prop interface, not checked for
// null. With nothing from that, the terrain point under the pointer (the
// renderer's PickTerrain, slot 0xF0, with 0 and 1) and the occupant manager's
// first occupant at it that the filter takes (slot 28). The pointer comes
// back already released: whoever keeps it references it again.
void* __fastcall SignAt(char* self, void*, int32_t x, int32_t z)
{
    void* sign = nullptr;
    void* model = nullptr;
    if (Virtual<uint8_t>(Global<void*>(addr::SC4_View43DD0), 0x104, x, z, At<void*>(self, field::kFilter), &model)) {
        void* const owner = Virtual<void*>(model, 0xC);
        Virtual<uint8_t>(owner, 0x0, kIidProp, &sign);
        Release(model);
    }
    if (!sign) {
        // Three copies of the zero at 0x00B2191C.
        uint32_t point[3];
        point[0] = point[1] = point[2] = Global<uint32_t>(addr::SC4SignRemoveTool_Zero);
        if (Virtual<uint8_t>(Global<void*>(addr::SC4_View43DD0), 0xF0, x, z, &point[0], uint32_t{0}, uint32_t{1})) {
            void* occupant = nullptr;
            Virtual<void>(Global<void*>(addr::SC4_NetworkService), 0x70, &occupant, point[0], point[2],
                          At<void*>(self, field::kFilter));
            if (occupant) {
                Virtual<uint8_t>(occupant, 0x0, kIidProp, &sign);
                Release(occupant);
            }
        }
    }
    if (sign)
        Release(sign);
    return sign;
}

// Marks or unmarks the kept sign: a sign on a network gets `level` through
// its interface 0x2B3B7D86 (slot 10), a sign on the terrain `flag` through
// 0x4B44FBE2 (slot 6).
void Mark(char* self, float level, uint32_t flag)
{
    void* const sign = At<void*>(self, field::kSign);
    if (!sign)
        return;
    if (Virtual<uint32_t>(sign, 0x1C) == kSignOnNetwork) {
        void* part = nullptr;
        if (Virtual<uint8_t>(At<void*>(self, field::kSign), 0x0, kIidNetworkSign, &part))
            Virtual<void>(part, 0x28, level);
        if (part)
            Release(part);
    } else if (Virtual<uint32_t>(At<void*>(self, field::kSign), 0x1C) == kSignOnTerrain) {
        void* part = nullptr;
        if (Virtual<uint8_t>(At<void*>(self, field::kSign), 0x0, kIidTerrainSign, &part))
            Virtual<void>(part, 0x18, flag);
        if (part)
            Release(part);
    }
}

// The game's 0x004D7950 (ret 4): the sign under the pointer changes. The
// kept one is unmarked (0.5, or the flag cleared), the new one kept in its
// place (referenced; the old one released) and marked (0.7, or the flag
// set). The same sign again is unmarked and marked again.
void __fastcall SetSign(char* self, void*, void* sign)
{
    Mark(self, 0.5f, 0);
    Hold(self, field::kSign, sign);
    Mark(self, 0.7f, 1);
}

// The game's 0x004D7AC0 (slot 20, OnMouseMove, ret 0xC): the sign at the
// point becomes the kept one. True always.
uint8_t __fastcall OnMouseMove(char* self, void*, int32_t x, int32_t z, uint32_t)
{
    SetSign(self, nullptr, SignAt(self, nullptr, x, z));
    return 1;
}

// --- Registration ----------------------------------------------------------------------

bool InstallSignPick(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[8] = {};
    static const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::SC4SignRemoveTool_FilterIncludes, reinterpret_cast<void*>(&FilterIncludes)},
        {addr::SC4SignRemoveTool_Construct, reinterpret_cast<void*>(&Construct)},
        {addr::SC4SignRemoveTool_Init, reinterpret_cast<void*>(&Init)},
        {addr::SC4SignRemoveTool_Shutdown, reinterpret_cast<void*>(&Shutdown)},
        {addr::SC4SignRemoveTool_OnMouseUpL, reinterpret_cast<void*>(&OnMouseUpL)},
        {addr::SC4SignRemoveTool_SignAt, reinterpret_cast<void*>(&SignAt)},
        {addr::SC4SignRemoveTool_SetSign, reinterpret_cast<void*>(&SetSign)},
        {addr::SC4SignRemoveTool_OnMouseMove, reinterpret_cast<void*>(&OnMouseMove)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    static_assert(count <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < count; ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].address);
        if (!found || !hook::Install(*found, hooks[i].ours, &originals[i]))
            return false;
    }
    return true;
}

const bool g_registered = registry::Register({
    "ui.tools.signpick",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4SignRemoveTool_SetSign,
    "Our sign removal tool of the city view (docs/tools.md)",
    false,
    InstallSignPick,
});

} // namespace
} // namespace opensc4::tool_signpick
