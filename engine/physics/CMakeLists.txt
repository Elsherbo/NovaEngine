# ============================================================
# Physics Module CMake
# ============================================================

add_library(nova_physics STATIC
    iphysics_world.h
    aabb_physics.h
    aabb_physics.cpp
)

target_include_directories(nova_physics PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../core
    ${CMAKE_CURRENT_SOURCE_DIR}/../renderer
    ${CMAKE_CURRENT_SOURCE_DIR}/../entities
)

target_link_libraries(nova_physics nova_core nova_renderer nova_entities)