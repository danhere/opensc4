# gzcom-dll (LGPL-2.1-or-later): the SC4 plugin SDK. Only the sources core.dll
# needs are compiled; upstream's own CMake build globs every source, including
# example directors that each define RZGetCOMDllDirector().
set(GZCOM_DIR ${CMAKE_CURRENT_LIST_DIR}/gzcom-dll/gzcom-dll)

if(NOT EXISTS ${GZCOM_DIR}/include/cRZCOMDllDirector.h)
  message(FATAL_ERROR "third_party/gzcom-dll is missing. Run: git submodule update --init")
endif()

add_library(opensc4_gzcom STATIC
  ${GZCOM_DIR}/src/cRZBaseString.cpp
  ${GZCOM_DIR}/src/cRZBaseSystemService.cpp
  ${GZCOM_DIR}/src/cRZCOMDllDirector.cpp
  ${GZCOM_DIR}/src/SC4String.cpp)
target_include_directories(opensc4_gzcom SYSTEM PUBLIC ${GZCOM_DIR}/include)
target_compile_definitions(opensc4_gzcom PUBLIC _CRT_SECURE_NO_WARNINGS)
target_compile_features(opensc4_gzcom PUBLIC cxx_std_20)
# Upstream code: keep its warnings out of our build output.
target_compile_options(opensc4_gzcom PRIVATE /W0)

# MinHook (BSD-2-Clause, includes Hacker Disassembler Engine 32): inline
# detours for core.dll's hook layer (src/core/hook.h). 32-bit sources only.
set(MINHOOK_DIR ${CMAKE_CURRENT_LIST_DIR}/minhook)
if(NOT EXISTS ${MINHOOK_DIR}/include/MinHook.h)
  message(FATAL_ERROR "third_party/minhook is missing. Run: git submodule update --init")
endif()

add_library(opensc4_minhook STATIC
  ${MINHOOK_DIR}/src/buffer.c
  ${MINHOOK_DIR}/src/hook.c
  ${MINHOOK_DIR}/src/trampoline.c
  ${MINHOOK_DIR}/src/hde/hde32.c)
target_include_directories(opensc4_minhook SYSTEM PUBLIC ${MINHOOK_DIR}/include)
target_compile_options(opensc4_minhook PRIVATE /W0)
