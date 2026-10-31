// Our rasterizer factory of the GZ framework's software rasterizer
// (docs/renderer.md, "The software rasterizer"): the system service of
// class 0xEB2EA3CB a drawing context asks for interface 0x4B2EA3B3 when it
// is given a target (vtable 0x00AC1668, 0x34 bytes, constructor
// 0x0082BBF0). It keeps a list of rasterizer makers (+0x20; Init puts the
// one maker there, whose slot 3 is GZRasterizer::Make), a list of tracked
// objects (+0x24) that are let go of five ticks after they were last used,
// the tick count (+0x28) and a lock (+0x2C, an RZCriticalSection). Every
// method of it that ran, all detours here, off by default:
//
//   renderer.raster.factory  the constructor 0x0082BBF0; QueryInterface
//                            (slot 0, and the service part's thunk
//                            0x0082BC60), MakeRasterizer (4),
//                            MakeContextBuffer (5), Track (6), Untrack (7)
//                            and ReleaseIdle (8); the service part's (+0x04,
//                            vtable 0x00AC1694) Init, Shutdown and OnTick
//                            (slots 8 to 10); 11 detours
//
// Left the game's: AddRef and Release and slot 10 (the tick count), shared
// with other classes; AddMaker (3) and ReleaseAll (9), which never ran; the
// destructor 0x0082BE00 and the third part's (+0x08, vtable 0x00AC1650)
// thunks; the base service's own slots.
//
// Return values: every one of ours answers what the game's leaves in EAX.
// QueryInterface hands an interface it does not know to the base service's
// with a jump, and MakeRasterizer with no makers answers AL 0 over the
// caller's EAX; those few instructions are assembly, the game's own.
// Everything else ends on a call whose answer is set (the lock's Unlock
// answers its count).
//
// No compare mode. The autotest (raster_autotest.cpp, "The factory") drives
// every method by address with a known EAX on factories of the game's
// constructor over recording makers and tracked objects, makes buffers with
// a context through the graphics system and draws through them, and runs
// Init, OnTick and Shutdown against the framework.
#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::raster {
namespace {

// A node of the game's std::list: the lists are rings through a sentinel.
struct Node {
    Node* next;
    Node* prev;
    void* value;
};

struct Factory {
    void** vtable;         // 0x00: 0x00AC1668, interface 0x4B2EA3B3
    void** serviceVtable;  // 0x04: 0x00AC1694, the system service base's part
    void** thirdVtable;    // 0x08: 0x00AC1650 (the base's second table replaced)
    uint8_t base[0x14];    // 0x0C: the system service base's
    Node* makers;          // 0x20: the rasterizer makers, held
    Node* tracked;         // 0x24: the tracked objects, not held
    uint32_t ticks;        // 0x28: OnTick's count
    void* lock[2];         // 0x2C: an RZCriticalSection
};
static_assert(sizeof(Factory) == 0x34 && offsetof(Factory, makers) == 0x20 && offsetof(Factory, ticks) == 0x28 &&
                  offsetof(Factory, lock) == 0x2C,
              "the game's rasterizer factory is 0x34 bytes");

Factory* FromService(void* service) { return reinterpret_cast<Factory*>(static_cast<uint8_t*>(service) - 4); }

// A vtable method of the game's (thiscall) called as __fastcall with EDX
// unused.
template <typename R, typename... A>
R Call(void* self, int slot, A... args)
{
    using Fn = R(__fastcall*)(void*, void*, A...);
    return reinterpret_cast<Fn>((*static_cast<void***>(self))[slot])(self, nullptr, args...);
}

// A thiscall function of the game's by address.
template <typename R, typename... A>
R Method(std::uintptr_t address, void* self, A... args)
{
    return reinterpret_cast<R(__fastcall*)(void*, void*, A...)>(address)(self, nullptr, args...);
}

template <typename R, typename... A>
R Cdecl(std::uintptr_t address, A... args)
{
    return reinterpret_cast<R(__cdecl*)(A...)>(address)(args...);
}

// EAX with AL replaced.
uint32_t WithAl(uint32_t eax, uint8_t al) { return (eax & ~0xFFu) | al; }

constexpr uint32_t kServiceId = 0xEB2EA3CB;
constexpr int32_t kServicePriority = 1500000;

enum Slot {
    kAddRef = 1,
    kRelease = 2,
    kLockSlot = 1,   // the lock's
    kUnlockSlot = 2, // the lock's
    kMake = 3,       // a maker's
    kInitBuffer = 3, // a plain buffer's (the graphics system's slot 3 makes one)
    kSetTarget = 17, // a drawing context's
    kSetContext = 7, // a buffer with a context's side interface's
    kReleaseIdle = 8,
    kAddToTick = 9,       // the framework's
    kRemoveFromTick = 10, // the framework's
    kTrackedRelease = 6,  // a tracked object's: let go of what it holds
    kTrackedHolding = 9,  // whether it holds anything
    kTrackedLastUsed = 11 // the tick it was last used
};

// The side interface of a buffer with a context.
constexpr std::size_t kBufferSide = 0x0C;

uint32_t Lock(Factory* f) { return Call<uint32_t>(f->lock, kLockSlot); }
uint32_t Unlock(Factory* f) { return Call<uint32_t>(f->lock, kUnlockSlot); }

Node* NewSentinel()
{
    Node* const s = Cdecl<Node*>(addr::SC4_stl_allocate, std::size_t{sizeof(Node)});
    s->next = s;
    s->prev = s;
    return s;
}

// The service part as the framework is given it: null for a null factory.
void* AsService(void* service) { return reinterpret_cast<uintptr_t>(service) == 4 ? nullptr : service; }

// ====================================================================================
// Making

// 0x0082BBF0: the system service base (ID 0xEB2EA3CB, priority 1500000)
// under the abstract table 0x00AC15F0, then the three tables, the two lists
// (empty rings), the tick count 0 and the lock. Answers this.
Factory* __fastcall Construct(Factory* f, void*)
{
    f->vtable = reinterpret_cast<void**>(addr::GZRasterizerFactory_vftable_base);
    Method<void*>(addr::cRZBaseSystemService_cRZBaseSystemService, &f->serviceVtable, kServiceId, kServicePriority);
    f->serviceVtable = reinterpret_cast<void**>(addr::GZRasterizerFactory_vftable_4);
    f->vtable = reinterpret_cast<void**>(addr::GZRasterizerFactory_vftable);
    f->thirdVtable = reinterpret_cast<void**>(addr::GZRasterizerFactory_vftable_8);
    f->makers = nullptr;
    f->makers = NewSentinel();
    f->tracked = nullptr;
    f->tracked = NewSentinel();
    f->ticks = 0;
    Method<void*>(addr::RZCriticalSection_Construct, &f->lock);
    return f;
}

// Slot 0 past the thunk below: itself, held.
uint32_t __fastcall QueryBody(Factory* f, void*, uint32_t, void** out)
{
    *out = f;
    return WithAl(Call<uint32_t>(f, kAddRef), 1);
}

// Slot 0, 0x0082B890 (ret 8): the factory's interface, else the base
// service's QueryInterface (on the service part) with EAX the interface.
std::uintptr_t g_baseQuery = addr::cRZBaseSystemService_QueryInterface;

__declspec(naked) void QueryInterface()
{
    __asm {
        mov eax, dword ptr [esp + 4]
        cmp eax, 0x4B2EA3B3
        je mine
        add ecx, 4
        jmp dword ptr [g_baseQuery]
    mine:
        jmp QueryBody
    }
}

// The service part's slot 0, 0x0082BC60.
__declspec(naked) void ServiceQueryInterface()
{
    __asm sub ecx, 4
    __asm jmp QueryInterface
}

// Slot 4 past the thunk below: the makers in turn until one makes a
// rasterizer for the target. AL 1 over the maker's answer, or 0 over the
// last one's.
uint32_t __fastcall MakeRasterizerBody(Factory* f, void*, void* target, void** out)
{
    Node* const sentinel = f->makers;
    uint32_t answer = 0;
    for (Node* n = sentinel->next; n != sentinel; n = n->next) {
        answer = Call<uint32_t>(n->value, kMake, target, out);
        if (static_cast<uint8_t>(answer) != 0)
            return WithAl(answer, 1);
    }
    return WithAl(answer, 0);
}

// Slot 4, 0x0082BAB0 (ret 8). No makers: AL 0 over the caller's EAX.
__declspec(naked) void MakeRasterizer()
{
    __asm {
        mov edx, dword ptr [ecx + 0x20]
        cmp dword ptr [edx], edx
        jne MakeRasterizerBody
        xor al, al
        ret 8
    }
}

// Slot 5, 0x0082B980: a plain buffer from the graphics system (its slot 3)
// set up at the size and colour type (the type and its depth, by value),
// then a buffer with a context (0x0082B780) and a drawing context
// (0x0082C9A0), each held; the context's target is the plain buffer and
// the buffer is given both (its side's SetContext). *out gets the buffer,
// held; the rest let go. AL 1 over the plain buffer's Release; AL 0 when
// the plain buffer is not made or not set up.
uint32_t __fastcall MakeContextBuffer(Factory*, void*, void** out, uint32_t width, uint32_t height, uint32_t type)
{
    void* plain = nullptr;
    void* const system = Cdecl<void*>(addr::SC4_Singleton913C1A);
    uint32_t answer = Call<uint32_t>(system, kInitBuffer, &plain);
    if (static_cast<uint8_t>(answer) == 0) {
        if (plain)
            answer = Call<uint32_t>(plain, kRelease);
        return WithAl(answer, 0);
    }
    // The colour type: its kind and the kind's depth (DepthOf leaves its
    // `this` unused).
    uint32_t colourType[2] = {type, 0};
    colourType[1] = Method<uint32_t>(addr::SC4BufferColorType_DepthOf, colourType, type);
    answer = Call<uint32_t>(plain, kInitBuffer, width, height, colourType[0], colourType[1]);
    if (static_cast<uint8_t>(answer) == 0) {
        if (plain)
            answer = Call<uint32_t>(plain, kRelease);
        return WithAl(answer, 0);
    }
    void* buffer = Cdecl<void*>(addr::SC4_operator_new, std::size_t{0x4C});
    if (buffer) {
        buffer = Method<void*>(addr::GZContextBuffer_GZContextBuffer, buffer);
        if (buffer)
            Call<uint32_t>(buffer, kAddRef);
    }
    void* context = Cdecl<void*>(addr::SC4_operator_new, std::size_t{200});
    if (context) {
        context = Method<void*>(addr::GZDrawContext_GZDrawContext, context);
        if (context)
            Call<uint32_t>(context, kAddRef);
    }
    Call<uint32_t>(context, kSetTarget, plain);
    Call<uint32_t>(static_cast<uint8_t*>(buffer) + kBufferSide, kSetContext, context, plain);
    Call<uint32_t>(buffer, kAddRef);
    *out = buffer;
    Call<uint32_t>(context, kRelease);
    answer = Call<uint32_t>(buffer, kRelease);
    if (plain)
        answer = Call<uint32_t>(plain, kRelease);
    return WithAl(answer, 1);
}

// ====================================================================================
// The tracked objects (under the lock; each answers the lock's Unlock)

// Slot 6, 0x0082BD90 (ret 4): to the list's front, not held.
uint32_t __fastcall Track(Factory* f, void*, void* object)
{
    Lock(f);
    Node* const first = f->tracked->next;
    Node* const node = Cdecl<Node*>(addr::SC4_stl_allocate, std::size_t{sizeof(Node)});
    node->value = object;
    Node* const before = first->prev;
    node->prev = before;
    node->next = first;
    before->next = node;
    first->prev = node;
    return Unlock(f);
}

// Slot 7, 0x0082BAF0 (ret 4): the first node with it taken out and freed.
uint32_t __fastcall Untrack(Factory* f, void*, void* object)
{
    Lock(f);
    Node* const sentinel = f->tracked;
    Node* n = sentinel->next;
    while (n != sentinel && n->value != object)
        n = n->next;
    if (n != f->tracked) {
        Node* const next = n->next;
        Node* const prev = n->prev;
        prev->next = next;
        next->prev = prev;
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(n));
    }
    return Unlock(f);
}

// Slot 8, 0x0082BB40 (ret 4, the argument unused): each object holding
// something (its slot 9) and last used five or more ticks ago (its slot
// 11, unsigned) lets go of it (its slot 6). The next node is read after.
uint32_t __fastcall ReleaseIdle(Factory* f, void*, uint32_t)
{
    Lock(f);
    Node* const sentinel = f->tracked;
    for (Node* n = sentinel->next; n != sentinel; n = n->next) {
        void* const object = n->value;
        if (static_cast<uint8_t>(Call<uint32_t>(object, kTrackedHolding)) != 0 &&
            f->ticks - Call<uint32_t>(object, kTrackedLastUsed) >= 5)
            Call<uint32_t>(object, kTrackedRelease);
    }
    return Unlock(f);
}

// ====================================================================================
// The service part (+0x04; `this` is the service part)

// Slot 8, 0x0082BC90: the one maker (0xC bytes, a counted object: tables
// 0x00AC162C and 0x00AC163C, its slot 3 GZRasterizer::Make) to the makers'
// end, held there; the service on the framework's tick list. AL 1 over
// AddToTick's answer.
uint32_t __fastcall Init(void* service, void*)
{
    Factory* const f = FromService(service);
    void** maker = Cdecl<void**>(addr::SC4_operator_new, std::size_t{0xC});
    if (maker) {
        maker[0] = reinterpret_cast<void*>(addr::GZRasterizerMaker_vftable_base);
        Method<void*>(addr::cRZBaseRefCount_Construct, &maker[1]);
        maker[0] = reinterpret_cast<void*>(addr::GZRasterizerMaker_vftable);
        maker[1] = reinterpret_cast<void*>(addr::GZRasterizerMaker_vftable_4);
        Call<uint32_t>(maker, kAddRef);
    }
    Node* const sentinel = f->makers;
    Node* const node = Cdecl<Node*>(addr::SC4_stl_allocate, std::size_t{sizeof(Node)});
    node->value = maker;
    if (maker)
        Call<uint32_t>(maker, kAddRef);
    Node* const last = sentinel->prev;
    node->next = sentinel;
    node->prev = last;
    last->next = node;
    sentinel->prev = node;
    if (maker)
        Call<uint32_t>(maker, kRelease);
    void* const framework = Cdecl<void*>(addr::RZGetFramework);
    return WithAl(Call<uint32_t>(framework, kAddToTick, AsService(service)), 1);
}

// Slot 9, 0x0082B8C0: off the framework's tick list. AL 1 over its answer.
uint32_t __fastcall Shutdown(void* service, void*)
{
    void* const framework = Cdecl<void*>(addr::RZGetFramework);
    return WithAl(Call<uint32_t>(framework, kRemoveFromTick, AsService(service)), 1);
}

// Slot 10, 0x0082B8E0 (ret 4, the argument unused): every fifth tick from
// the first, ReleaseIdle(5); the count goes up. AL 1 over ReleaseIdle's
// answer, or over the count divided by 5 (the game's division).
uint32_t __fastcall OnTick(void* service, void*, uint32_t)
{
    Factory* const f = FromService(service);
    const uint32_t ticks = f->ticks;
    uint32_t answer = ticks / 5;
    if (ticks % 5 == 0)
        answer = Call<uint32_t>(f, kReleaseIdle, uint32_t{5});
    ++f->ticks;
    return WithAl(answer, 1);
}

// ====================================================================================
// Installation

struct Detour {
    std::uintptr_t address;
    void* detour;
};

bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static const Detour detours[] = {
        {addr::GZRasterizerFactory_GZRasterizerFactory, reinterpret_cast<void*>(&Construct)},
        {addr::GZRasterizerFactory_QueryInterface, reinterpret_cast<void*>(&QueryInterface)},
        {addr::GZRasterizerFactory_ServiceQueryInterface, reinterpret_cast<void*>(&ServiceQueryInterface)},
        {addr::GZRasterizerFactory_MakeRasterizer, reinterpret_cast<void*>(&MakeRasterizer)},
        {addr::GZRasterizerFactory_MakeContextBuffer, reinterpret_cast<void*>(&MakeContextBuffer)},
        {addr::GZRasterizerFactory_Track, reinterpret_cast<void*>(&Track)},
        {addr::GZRasterizerFactory_Untrack, reinterpret_cast<void*>(&Untrack)},
        {addr::GZRasterizerFactory_ReleaseIdle, reinterpret_cast<void*>(&ReleaseIdle)},
        {addr::GZRasterizerFactory_Init, reinterpret_cast<void*>(&Init)},
        {addr::GZRasterizerFactory_Shutdown, reinterpret_cast<void*>(&Shutdown)},
        {addr::GZRasterizerFactory_OnTick, reinterpret_cast<void*>(&OnTick)},
    };
    for (const Detour& d : detours) {
        const addr::Entry* entry = addr_check::Find(d.address);
        void* original = nullptr;
        if (!entry || !hook::Install(*entry, d.detour, &original))
            return false;
    }
    log::Write("raster: factory: %u ours", static_cast<unsigned>(sizeof detours / sizeof detours[0]));
    return true;
}

const bool g_registered = registry::Register(
    {"renderer.raster.factory", "renderer/raster", registry::Kind::Detour, registry::Phase::Attach,
     addr::GZRasterizerFactory_GZRasterizerFactory,
     "Our rasterizer factory of the software rasterizer (vtable 0x00AC1668, the service a drawing context asks for "
     "its rasterizer): its constructor, QueryInterface, making rasterizers and buffers with a context, the tracked "
     "objects, and the service's Init, Shutdown and OnTick (11 detours; docs/renderer.md)",
     false, Install});

} // namespace
} // namespace opensc4::raster
