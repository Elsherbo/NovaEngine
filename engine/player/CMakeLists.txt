# engine/player/CMakeLists.txt
# First-person player movement controller.
# Lives in nova_engine (not nova_game) because the engine main loop
# owns the PlayerController instance and calls it each tick.

add_library(nova_player STATIC
    player_controller.h
    player_controller.cpp
)

target_include_directories(nova_player PUBLIC
    ${CMAKE_SOURCE_DIR}          # engine/player/player_controller.h via "engine/player/..."
    ${SDL3_INCLUDE_DIR}          # SDL_scancode.h
)

# nova_player only needs core types + physics interface + platform (InputState).
# It must NOT link nova_game — that is the entire point of this split.
target_link_libraries(nova_player PUBLIC
    nova_core
    nova_physics
    nova_platform
)
