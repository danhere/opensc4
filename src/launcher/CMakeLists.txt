add_executable(opensc4_launcher
  main.cpp
  inject.cpp)
target_link_libraries(opensc4_launcher PRIVATE opensc4_options)
set_target_properties(opensc4_launcher PROPERTIES OUTPUT_NAME opensc4)
