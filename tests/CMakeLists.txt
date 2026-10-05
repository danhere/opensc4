# Smoke tests run the built binaries, so they need a Windows host
# (or Wine via CMAKE_CROSSCOMPILING_EMULATOR on Linux).
if(NOT CMAKE_HOST_WIN32 AND NOT CMAKE_CROSSCOMPILING_EMULATOR)
  return()
endif()

add_executable(opensc4_inject_target inject_target.cpp)
target_link_libraries(opensc4_inject_target PRIVATE opensc4_options)
set_target_properties(opensc4_inject_target PROPERTIES OUTPUT_NAME inject_target)

add_test(NAME launcher_version COMMAND opensc4_launcher --version)
set_tests_properties(launcher_version PROPERTIES
  PASS_REGULAR_EXPRESSION "^opensc4 ${PROJECT_VERSION}")

add_test(NAME launcher_loads_core COMMAND opensc4_launcher --check-core)
set_tests_properties(launcher_loads_core PROPERTIES
  WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

# Full launch path against a stand-in exe: suspended start, remote LoadLibraryW,
# resume. The target's exit code (passed through by --wait) says whether
# core.dll was loaded before its entry point ran.
add_test(NAME launcher_injects_core
  COMMAND opensc4_launcher --game $<TARGET_FILE:opensc4_inject_target> --wait)
