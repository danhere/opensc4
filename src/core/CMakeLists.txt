# Support code shared by core.dll and the tests: logging, build detection,
# the address database check and the hook layer.
add_library(opensc4_core_support STATIC
  address_check.cpp
  compare.cpp
  coverage.cpp
  crash.cpp
  game.cpp
  hook.cpp
  interface_counter.cpp
  log.cpp
  registry.cpp)
target_link_libraries(opensc4_core_support
  PUBLIC opensc4_options opensc4_address_db
  PRIVATE opensc4_minhook)

add_library(opensc4_core SHARED
  autotest.cpp
  diagnostics.cpp
  dllmain.cpp
  director.cpp
  gdriver_proxy.cpp
  pollution_proxy.cpp
  trace.cpp)
target_link_libraries(opensc4_core PRIVATE opensc4_core_support opensc4_gzcom)
set_target_properties(opensc4_core PROPERTIES OUTPUT_NAME core)
