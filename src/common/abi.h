// Compile-time checks that we are building for the same ABI as SimCity 4.exe
// (32-bit x86, MSVC object model). See docs/architecture.md, "ABI rules".
#pragma once

#if !defined(_MSC_VER) || !defined(__clang__)
#error "OpenSC4 must be built with clang-cl (MSVC ABI)."
#endif

#if !defined(_M_IX86)
#error "OpenSC4 must target 32-bit x86."
#endif

namespace opensc4::abi {

static_assert(sizeof(void*) == 4);
static_assert(sizeof(long) == 4);
static_assert(sizeof(wchar_t) == 2);
static_assert(alignof(double) == 8);

// GZCOM interfaces rely on MSVC's vtable layout: one vptr per polymorphic
// base, laid out in declaration order.
struct IA { virtual void a() = 0; };
struct IB { virtual void b() = 0; };
struct AB : IA, IB {};
static_assert(sizeof(AB) == 2 * sizeof(void*));

} // namespace opensc4::abi
