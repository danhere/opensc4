# Support code shared by core.dll and the tests: logging, build detection,
# the address database check and the hook layer.
add_library(opensc4_core_support STATIC
  address_check.cpp
  compare.cpp
  coverage.cpp
  crash.cpp
  game.cpp
  gd/pixels.cpp
  gd/vertex_format.cpp
  hook.cpp
  iat.cpp
  interface_counter.cpp
  log.cpp
  registry.cpp
  settings.cpp)
target_link_libraries(opensc4_core_support
  PUBLIC opensc4_options opensc4_address_db
  PRIVATE opensc4_minhook)

add_library(opensc4_core SHARED
  autotest.cpp
  crime_sim.cpp
  crime_object.cpp
  crime_step.cpp
  crime_tuning.cpp
  flora_growth.cpp
  flora_messages.cpp
  flora_object.cpp
  flora_place.cpp
  flora_sim.cpp
  flora_types.cpp
  neighbors_classes.cpp
  neighbors_connections.cpp
  neighbors_deals.cpp
  neighbors_messages.cpp
  neighbors_object.cpp
  neighbors_sim.cpp
  diagnostics.cpp
  dllmain.cpp
  director.cpp
  fixed_seed.cpp
  gd/d3d11_backend.cpp
  gd/driver.cpp
  d3d7_probe.cpp
  dbpf_survey.cpp
  gdriver_proxy.cpp
  landvalue_object.cpp
  landvalue_sim.cpp
  pollution_buildings.cpp
  pollution_effects.cpp
  pollution_init.cpp
  pollution_object.cpp
  pollution_proxy.cpp
  pollution_save.cpp
  pollution_tuning.cpp
  power_object.cpp
  power_sim.cpp
  traffic_object.cpp
  traffic_pathfinder.cpp
  traffic_sim.cpp
  traffic_tuning.cpp
  traffic_water.cpp
  traffic_walks.cpp
  water_object.cpp
  water_sim.cpp
  aura_init.cpp
  aura_object.cpp
  aura_sim.cpp
  pollution_sim.cpp
  pollution_tracts.cpp
  virtual_clock.cpp
  thread_trace.cpp
  rng_trace.cpp
  fixed_ticks.cpp
  trip_trace.cpp
  agent_order.cpp
  trace.cpp
  multi_instance.cpp)
target_link_libraries(opensc4_core PRIVATE opensc4_core_support opensc4_dbpf opensc4_gzcom user32)
set_target_properties(opensc4_core PROPERTIES OUTPUT_NAME core)
