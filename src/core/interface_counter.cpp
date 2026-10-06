#include "core/interface_counter.h"

#include "core/log.h"

#include <cstddef>

// Naked functions are MSVC-style; clang-cl is the only supported compiler
// (docs/decisions.md), so one macro keeps them in one place.
#define OPENSC4_NAKED __declspec(naked)

namespace opensc4::interface_counter {
namespace {

// cIGZUnknown's layout (gzcom-dll), so this file needs no GZCOM headers.
struct Unknown {
    virtual bool QueryInterface(uint32_t iid, void** object) = 0;
    virtual uint32_t AddRef() = 0;
    virtual uint32_t Release() = 0;
};
constexpr uint32_t kIidUnknown = 1; // GZIID_cIGZUnknown

struct Wrapper {
    const void* const* vtable; // offset 0
    void* inner;               // offset 4: the wrapped interface pointer
    Counters* counters;        // offset 8
    std::atomic<uint32_t> refs;
};
static_assert(offsetof(Wrapper, inner) == 4 && offsetof(Wrapper, counters) == 8, "the thunks use these offsets");
static_assert(offsetof(Counters, calls) == 12, "the thunks use this offset");

Unknown* Inner(Wrapper* w)
{
    return static_cast<Unknown*>(w->inner);
}

// cIGZUnknown slots, written as __fastcall with an unused EDX (= __thiscall).
bool __fastcall QueryInterface(Wrapper* self, void*, uint32_t iid, void** object)
{
    self->counters->calls[0].fetch_add(1, std::memory_order_relaxed);
    if (iid == self->counters->iid || iid == kIidUnknown) {
        self->refs.fetch_add(1);
        *object = self;
        return true;
    }
    return Inner(self)->QueryInterface(iid, object);
}

uint32_t __fastcall AddRef(Wrapper* self, void*)
{
    self->counters->calls[1].fetch_add(1, std::memory_order_relaxed);
    return self->refs.fetch_add(1) + 1;
}

uint32_t __fastcall Release(Wrapper* self, void*)
{
    self->counters->calls[2].fetch_add(1, std::memory_order_relaxed);
    const uint32_t refs = self->refs.fetch_sub(1) - 1;
    if (refs == 0) {
        Inner(self)->Release();
        delete self;
    }
    return refs;
}

// Slot N: count, then jump to slot N of the wrapped object with ECX = inner.
// The original returns straight to the caller; EAX and EDX are scratch at a
// __thiscall call.
#define OPENSC4_THUNK(n)                                     \
    OPENSC4_NAKED void Thunk##n()                            \
    {                                                        \
        __asm mov edx, dword ptr [ecx + 8]                   \
        __asm lock inc dword ptr [edx + 12 + (n) * 4]        \
        __asm mov ecx, dword ptr [ecx + 4]                   \
        __asm mov eax, dword ptr [ecx]                       \
        __asm jmp dword ptr [eax + (n) * 4]                  \
    }
OPENSC4_THUNK(3) OPENSC4_THUNK(4) OPENSC4_THUNK(5) OPENSC4_THUNK(6) OPENSC4_THUNK(7)
OPENSC4_THUNK(8) OPENSC4_THUNK(9) OPENSC4_THUNK(10) OPENSC4_THUNK(11) OPENSC4_THUNK(12)
OPENSC4_THUNK(13) OPENSC4_THUNK(14) OPENSC4_THUNK(15) OPENSC4_THUNK(16) OPENSC4_THUNK(17)
OPENSC4_THUNK(18) OPENSC4_THUNK(19) OPENSC4_THUNK(20) OPENSC4_THUNK(21) OPENSC4_THUNK(22)
OPENSC4_THUNK(23)
#undef OPENSC4_THUNK

const void* const kVtable[kMaxSlots] = {
    reinterpret_cast<const void*>(&QueryInterface),
    reinterpret_cast<const void*>(&AddRef),
    reinterpret_cast<const void*>(&Release),
    reinterpret_cast<const void*>(&Thunk3),  reinterpret_cast<const void*>(&Thunk4),
    reinterpret_cast<const void*>(&Thunk5),  reinterpret_cast<const void*>(&Thunk6),
    reinterpret_cast<const void*>(&Thunk7),  reinterpret_cast<const void*>(&Thunk8),
    reinterpret_cast<const void*>(&Thunk9),  reinterpret_cast<const void*>(&Thunk10),
    reinterpret_cast<const void*>(&Thunk11), reinterpret_cast<const void*>(&Thunk12),
    reinterpret_cast<const void*>(&Thunk13), reinterpret_cast<const void*>(&Thunk14),
    reinterpret_cast<const void*>(&Thunk15), reinterpret_cast<const void*>(&Thunk16),
    reinterpret_cast<const void*>(&Thunk17), reinterpret_cast<const void*>(&Thunk18),
    reinterpret_cast<const void*>(&Thunk19), reinterpret_cast<const void*>(&Thunk20),
    reinterpret_cast<const void*>(&Thunk21), reinterpret_cast<const void*>(&Thunk22),
    reinterpret_cast<const void*>(&Thunk23),
};

} // namespace

void* Wrap(void* inner, Counters& counters)
{
    if (!inner || counters.slots < 3 || counters.slots > kMaxSlots)
        return inner;
    auto* w = new Wrapper{kVtable, inner, &counters, {1}};
    return w;
}

void Log(const Counters& counters)
{
    for (int i = 0; i < counters.slots; ++i) {
        const uint32_t n = counters.calls[i].load(std::memory_order_relaxed);
        if (n)
            log::Write("  %s (0x%08X) slot %-2d %u", counters.name, counters.iid, i, n);
    }
}

} // namespace opensc4::interface_counter
