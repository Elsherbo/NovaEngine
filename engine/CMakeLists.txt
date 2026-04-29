# engine/CMakeLists.txt

add_library(nova_engine STATIC)

# Include root so #include "engine/..." works from outside
target_include_directories(nova_engine PUBLIC ${CMAKE_SOURCE_DIR})

# ---- Phase 1 modules (uncomment as implemented) ----

# Core (math, memory, containers, logging)
add_subdirectory(core)
target_link_libraries(nova_engine PUBLIC nova_core)

# Platform
add_subdirectory(platform)
target_link_libraries(nova_engine PUBLIC nova_platform)

# Renderer
add_subdirectory(renderer)
target_link_libraries(nova_engine PUBLIC nova_renderer)

# Physics
add_subdirectory(physics)
target_link_libraries(nova_engine PUBLIC nova_physics)

# Entities
add_subdirectory(entities)
target_link_libraries(nova_engine PUBLIC nova_entities)

# Player controller (engine-side — drives the main loop, not game logic)
add_subdirectory(player)
target_link_libraries(nova_engine PUBLIC nova_player)

# Audio
# add_subdirectory(audio)
# target_link_libraries(nova_engine PUBLIC nova_audio)

# Network
# add_subdirectory(network)
# target_link_libraries(nova_engine PUBLIC nova_network)

# Scripting
# add_subdirectory(scripting)
# target_link_libraries(nova_engine PUBLIC nova_scripting)
