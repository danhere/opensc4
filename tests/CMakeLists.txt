# Smoke tests run the built binaries, so they need a Windows host
# (or Wine via CMAKE_CROSSCOMPILING_EMULATOR on Linux).
if(NOT CMAKE_HOST_WIN32 AND NOT CMAKE_CROSSCOMPILING_EMULATOR)
  return()
endif()

add_executable(opensc4_inject_target inject_target.cpp)
target_link_libraries(opensc4_inject_target PRIVATE opensc4_options)
set_target_properties(opensc4_inject_target PROPERTIES OUTPUT_NAME inject_target)

add_executable(opensc4_hook_test hook_test.cpp)
target_link_libraries(opensc4_hook_test PRIVATE opensc4_core_support)
add_test(NAME hook_layer COMMAND opensc4_hook_test)
set_tests_properties(hook_layer PROPERTIES PASS_REGULAR_EXPRESSION "hook test OK")

add_executable(opensc4_crash_test crash_test.cpp)
target_link_libraries(opensc4_crash_test PRIVATE opensc4_core_support)
add_test(NAME crash_handling COMMAND opensc4_crash_test)
set_tests_properties(crash_handling PROPERTIES PASS_REGULAR_EXPRESSION "crash test OK")

add_executable(opensc4_log_test log_test.cpp)
target_link_libraries(opensc4_log_test PRIVATE opensc4_core_support)
add_test(NAME log_lines COMMAND opensc4_log_test)
set_tests_properties(log_lines PROPERTIES PASS_REGULAR_EXPRESSION "log test OK")

add_executable(opensc4_seh_test seh_test.cpp)
target_link_libraries(opensc4_seh_test PRIVATE opensc4_options)
add_test(NAME seh_pattern COMMAND opensc4_seh_test)
set_tests_properties(seh_pattern PROPERTIES PASS_REGULAR_EXPRESSION "seh test OK")

add_executable(opensc4_interface_counter_test interface_counter_test.cpp)
target_link_libraries(opensc4_interface_counter_test PRIVATE opensc4_core_support)
add_test(NAME interface_counter COMMAND opensc4_interface_counter_test)
set_tests_properties(interface_counter PROPERTIES PASS_REGULAR_EXPRESSION "interface counter test OK")

add_executable(opensc4_registry_test registry_test.cpp)
target_link_libraries(opensc4_registry_test PRIVATE opensc4_core_support)
add_test(NAME replacement_registry COMMAND opensc4_registry_test)
set_tests_properties(replacement_registry PROPERTIES PASS_REGULAR_EXPRESSION "registry test OK")

add_executable(opensc4_coverage_test coverage_test.cpp)
target_link_libraries(opensc4_coverage_test PRIVATE opensc4_core_support)
add_test(NAME coverage_meter COMMAND opensc4_coverage_test)
set_tests_properties(coverage_meter PROPERTIES PASS_REGULAR_EXPRESSION "coverage test OK")

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

add_executable(opensc4_gd_vertex_format_test gd_vertex_format_test.cpp)
target_link_libraries(opensc4_gd_vertex_format_test PRIVATE opensc4_core_support)
add_test(NAME gd_vertex_format COMMAND opensc4_gd_vertex_format_test)
set_tests_properties(gd_vertex_format PROPERTIES PASS_REGULAR_EXPRESSION "gd vertex format test OK")

add_executable(opensc4_settings_test settings_test.cpp)
target_link_libraries(opensc4_settings_test PRIVATE opensc4_core_support)
add_test(NAME settings COMMAND opensc4_settings_test)
set_tests_properties(settings PROPERTIES PASS_REGULAR_EXPRESSION "settings test OK")

add_executable(opensc4_iat_test iat_test.cpp)
target_link_libraries(opensc4_iat_test PRIVATE opensc4_core_support)
add_test(NAME iat_patching COMMAND opensc4_iat_test)
set_tests_properties(iat_patching PROPERTIES PASS_REGULAR_EXPRESSION "iat test OK")

add_executable(opensc4_dbpf_test dbpf_test.cpp)
target_link_libraries(opensc4_dbpf_test PRIVATE opensc4_dbpf)
add_test(NAME dbpf_reader COMMAND opensc4_dbpf_test)
set_tests_properties(dbpf_reader PROPERTIES PASS_REGULAR_EXPRESSION "dbpf test OK")

add_executable(opensc4_seed_code_test seed_code_test.cpp)
target_link_libraries(opensc4_seed_code_test PRIVATE opensc4_core_support)
add_test(NAME seed_code COMMAND opensc4_seed_code_test)
set_tests_properties(seed_code PROPERTIES PASS_REGULAR_EXPRESSION "seed code test OK")

# Not "patch" in the name: Windows installer detection refuses to start a
# manifest-less exe named like an installer or patch without elevation.
add_executable(opensc4_pe_flag_test pe_flag_test.cpp ${PROJECT_SOURCE_DIR}/src/launcher/pe_patch.cpp)
target_link_libraries(opensc4_pe_flag_test PRIVATE opensc4_options)
add_test(NAME pe_flag COMMAND opensc4_pe_flag_test)
set_tests_properties(pe_flag PROPERTIES PASS_REGULAR_EXPRESSION "pe flag test OK")
