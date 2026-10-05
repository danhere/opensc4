add_library(opensc4_core SHARED
  dllmain.cpp)
target_link_libraries(opensc4_core PRIVATE opensc4_options)
set_target_properties(opensc4_core PROPERTIES OUTPUT_NAME core)
