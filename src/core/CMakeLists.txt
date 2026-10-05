add_library(opensc4_core SHARED
  autotest.cpp
  dllmain.cpp
  director.cpp
  game.cpp
  log.cpp
  pollution_proxy.cpp)
target_link_libraries(opensc4_core PRIVATE opensc4_options opensc4_gzcom)
set_target_properties(opensc4_core PROPERTIES OUTPUT_NAME core)
