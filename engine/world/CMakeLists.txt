# ============================================================
# World Module CMake
# ============================================================

add_library(nova_world STATIC
    iworld.h
    bsp_world.h
    bsp_world.cpp
)

target_include_directories(nova_world PUBLIC
    ${CMAKE_SOURCE_DIR}
)

# BSPWorld needs BSPMap (renderer), IPhysicsWorld (physics), and
# AABBPhysics for the trace delegation.
target_link_libraries(nova_world PUBLIC nova_core nova_renderer nova_physics)