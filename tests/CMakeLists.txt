# Smoke tests run the built binaries, so they need a Windows host
# (or Wine via CMAKE_CROSSCOMPILING_EMULATOR on Linux).
if(NOT CMAKE_HOST_WIN32 AND NOT CMAKE_CROSSCOMPILING_EMULATOR)
  return()
endif()

add_test(NAME launcher_version COMMAND opensc4_launcher --version)
set_tests_properties(launcher_version PROPERTIES
  PASS_REGULAR_EXPRESSION "^opensc4 ${PROJECT_VERSION}")

add_test(NAME launcher_loads_core COMMAND opensc4_launcher --check-core)
set_tests_properties(launcher_loads_core PROPERTIES
  WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
