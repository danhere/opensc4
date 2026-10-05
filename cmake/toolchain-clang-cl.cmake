# Toolchain: clang-cl + lld-link targeting 32-bit Windows with the MSVC ABI.
#
# Used unchanged on Windows and Linux hosts. The Windows SDK and MSVC CRT come
# from an xwin splat in winsysroot layout (see scripts/fetch-sdk.sh), so every
# host builds against the same headers and libraries.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_VERSION 10.0)
set(CMAKE_SYSTEM_PROCESSOR X86)

set(OPENSC4_TARGET_TRIPLE i386-pc-windows-msvc)

if(NOT DEFINED OPENSC4_WINSYSROOT)
  if(DEFINED ENV{OPENSC4_WINSYSROOT})
    set(OPENSC4_WINSYSROOT "$ENV{OPENSC4_WINSYSROOT}")
  else()
    get_filename_component(OPENSC4_WINSYSROOT "${CMAKE_CURRENT_LIST_DIR}/../.xwin/sysroot" ABSOLUTE)
  endif()
endif()
# try_compile projects re-read this file; make sure they see the same value.
set(ENV{OPENSC4_WINSYSROOT} "${OPENSC4_WINSYSROOT}")

if(NOT EXISTS "${OPENSC4_WINSYSROOT}/VC" OR NOT EXISTS "${OPENSC4_WINSYSROOT}/Windows Kits")
  message(FATAL_ERROR
    "Windows SDK/CRT not found at ${OPENSC4_WINSYSROOT}.\n"
    "Run scripts/fetch-sdk.sh, or set OPENSC4_WINSYSROOT to an xwin winsysroot-style splat.")
endif()

set(CMAKE_C_COMPILER clang-cl)
set(CMAKE_CXX_COMPILER clang-cl)
set(CMAKE_LINKER lld-link)
set(CMAKE_AR llvm-lib)
set(CMAKE_RC_COMPILER llvm-rc)

# Static release CRT in every configuration: nothing to install next to the
# game, and xwin does not fetch the debug CRT. Set here, not in CMakeLists.txt,
# so CMake's compiler checks link against it too.
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)

set(CMAKE_C_COMPILER_TARGET ${OPENSC4_TARGET_TRIPLE})
set(CMAKE_CXX_COMPILER_TARGET ${OPENSC4_TARGET_TRIPLE})

set(_opensc4_compile_flags "/winsysroot \"${OPENSC4_WINSYSROOT}\"")
set(CMAKE_C_FLAGS_INIT "${_opensc4_compile_flags}")
set(CMAKE_CXX_FLAGS_INIT "${_opensc4_compile_flags}")

# llvm-mt is not part of every LLVM distribution, so skip embedded manifests.
set(_opensc4_link_flags "/winsysroot:\"${OPENSC4_WINSYSROOT}\" /machine:x86 /manifest:no")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_opensc4_link_flags}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_opensc4_link_flags}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_opensc4_link_flags}")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
