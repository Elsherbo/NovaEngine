# ============================================================
# FILE:    game/CMakeLists.txt
# GAME:   Game DLL - implements IGameModule for gameplay
# PURPOSE: Separates game logic from engine. Different games
#          = different DLLs, same engine core.
# ============================================================

add_library(nova_game SHARED
    src/game_main.cpp
    src/game_module.cpp
    src/entities/game_player.cpp
    src/entities/game_items.cpp
)

target_include_directories(nova_game PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/engine/core
    ${CMAKE_SOURCE_DIR}/engine/entities
    ${CMAKE_SOURCE_DIR}/engine/physics
    ${CMAKE_SOURCE_DIR}/engine/renderer
    ${CMAKE_SOURCE_DIR}/engine/platform
    ${CMAKE_SOURCE_DIR}/vendor/SDL3/include
)

target_link_libraries(nova_game PRIVATE
    nova_engine
    nova_entities
    nova_renderer
    nova_physics
    nova_core
    nova_platform
)

set_target_properties(nova_game PROPERTIES
    OUTPUT_NAME "nova_game"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
