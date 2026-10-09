# DBPF package reader (P3): portable, no game or Windows dependencies.
add_library(opensc4_dbpf STATIC
  dbpf.cpp
  plugins.cpp
  qfs.cpp)
target_link_libraries(opensc4_dbpf PUBLIC opensc4_options)

add_executable(opensc4_dbpf_tool tool.cpp)
target_link_libraries(opensc4_dbpf_tool PRIVATE opensc4_dbpf)
set_target_properties(opensc4_dbpf_tool PROPERTIES OUTPUT_NAME opensc4-dbpf)
