# engine/player/CMakeLists.txt
# First-person player movement controller.
#
# NOTE: player_controller.h now includes engine/core/cvar.h and defines
# inline CVar* registrations (cv_pc_*).  These are registered in the
# CVarSystem singleton the first time the header is included.
# nova_core already links nova_vendor_glad transitively, so no new
# link dependencies are needed here.

add_library(nova_player STATIC
    player_controller.h
    player_controller.cpp
)

target_include_directories(nova_player PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${SDL3_INCLUDE_DIR}
)

# nova_core now provides CVar — nova_player inherits it via this link.
target_link_libraries(nova_player PUBLIC
    nova_core
    nova_physics
    nova_platform
)