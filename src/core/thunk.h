// Register-saving entry thunks for detours on small game functions.
//
// Some game callers keep values in ECX (and EDX) across calls into
// functions that happen to leave them alone (the S3D camera's constructor
// 0x007FF930 uses ECX after calling ResetOrientation, Project after
// BuildView). A C++ detour may change both, so each such detour is entered
// through a thunk that saves them, passes the stack arguments on and
// restores them. `Target` is a __fastcall function (this in ECX, EDX unused)
// with `ArgBytes` bytes of stack arguments; EAX (the result) is kept.
#pragma once

#define OPENSC4_THUNK(Name, Target, ArgBytes)                                                                  \
    __declspec(naked) void Name()                                                                              \
    {                                                                                                          \
        __asm push ecx                                                                                         \
        __asm push edx                                                                                         \
        OPENSC4_REPUSH_##ArgBytes                                                                              \
        __asm call Target                                                                                      \
        __asm pop edx                                                                                          \
        __asm pop ecx                                                                                          \
        __asm ret ArgBytes                                                                                     \
    }

// The same for a cdecl game function (the caller pops the arguments):
// `Target` is a cdecl function with `ArgBytes` bytes of arguments.
#define OPENSC4_CDECL_THUNK(Name, Target, ArgBytes)                                                            \
    __declspec(naked) void Name()                                                                              \
    {                                                                                                          \
        __asm push ecx                                                                                         \
        __asm push edx                                                                                         \
        OPENSC4_REPUSH_##ArgBytes                                                                              \
        __asm call Target                                                                                      \
        __asm add esp, ArgBytes                                                                                \
        __asm pop edx                                                                                          \
        __asm pop ecx                                                                                          \
        __asm ret                                                                                              \
    }

#define OPENSC4_PUSH(Bytes) __asm push dword ptr [esp + 8 + Bytes]
#define OPENSC4_REPUSH_0
#define OPENSC4_REPUSH_4 OPENSC4_PUSH(4)
#define OPENSC4_REPUSH_8 OPENSC4_PUSH(8) OPENSC4_PUSH(8)
#define OPENSC4_REPUSH_12 OPENSC4_PUSH(12) OPENSC4_PUSH(12) OPENSC4_PUSH(12)
#define OPENSC4_REPUSH_16 OPENSC4_PUSH(16) OPENSC4_PUSH(16) OPENSC4_PUSH(16) OPENSC4_PUSH(16)
#define OPENSC4_REPUSH_20 OPENSC4_PUSH(20) OPENSC4_PUSH(20) OPENSC4_PUSH(20) OPENSC4_PUSH(20) OPENSC4_PUSH(20)
#define OPENSC4_REPUSH_24 OPENSC4_PUSH(24) OPENSC4_PUSH(24) OPENSC4_PUSH(24) OPENSC4_PUSH(24) OPENSC4_PUSH(24) OPENSC4_PUSH(24)
#define OPENSC4_REPUSH_28 OPENSC4_PUSH(28) OPENSC4_PUSH(28) OPENSC4_PUSH(28) OPENSC4_PUSH(28) OPENSC4_PUSH(28) OPENSC4_PUSH(28) OPENSC4_PUSH(28)
#define OPENSC4_REPUSH_32 OPENSC4_PUSH(32) OPENSC4_PUSH(32) OPENSC4_PUSH(32) OPENSC4_PUSH(32) OPENSC4_PUSH(32) OPENSC4_PUSH(32) OPENSC4_PUSH(32) OPENSC4_PUSH(32)
#define OPENSC4_REPUSH_36 OPENSC4_PUSH(36) OPENSC4_PUSH(36) OPENSC4_PUSH(36) OPENSC4_PUSH(36) OPENSC4_PUSH(36) OPENSC4_PUSH(36) OPENSC4_PUSH(36) OPENSC4_PUSH(36) OPENSC4_PUSH(36)
#define OPENSC4_REPUSH_40 OPENSC4_PUSH(40) OPENSC4_PUSH(40) OPENSC4_PUSH(40) OPENSC4_PUSH(40) OPENSC4_PUSH(40) OPENSC4_PUSH(40) OPENSC4_PUSH(40) OPENSC4_PUSH(40) OPENSC4_PUSH(40) OPENSC4_PUSH(40)
