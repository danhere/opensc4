cmake_minimum_required(VERSION 3.25)

project(OpenSC4
  VERSION 0.0.1
  DESCRIPTION "Incremental open-source reimplementation of SimCity 4 Deluxe"
  LANGUAGES C CXX)

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  message(FATAL_ERROR "OpenSC4 builds only with clang-cl. Configure with a preset (cmake --preset debug).")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "OpenSC4 must target 32-bit x86 to load into SimCity 4.exe.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

add_library(opensc4_options INTERFACE)
target_compile_options(opensc4_options INTERFACE
  /W4 /permissive- /Zc:__cplusplus /utf-8
  /EHsc
  -Wno-microsoft-cast)
target_compile_definitions(opensc4_options INTERFACE
  WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
  _WIN32_WINNT=0x0601
  OPENSC4_VERSION="${PROJECT_VERSION}")
target_include_directories(opensc4_options INTERFACE ${PROJECT_SOURCE_DIR}/src)

add_subdirectory(third_party)
add_subdirectory(src/core)
add_subdirectory(src/launcher)

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
