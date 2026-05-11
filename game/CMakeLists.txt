# ============================================================
# FILE:    game/CMakeLists.txt
# GAME:   Game DLL - implements IGameModule for gameplay
# PURPOSE: Separates game logic from engine. Different games
#          = different DLLs, same engine core.
# ============================================================

add_library(nova_game SHARED
    src/game_main.cpp
    src/game_module.cpp
    src/game_entity_classes.cpp
    src/entities/game_player.cpp
    src/entities/game_items.cpp
)

target_include_directories(nova_game PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(nova_game PRIVATE
    nova_core
    nova_entities
)

set_target_properties(nova_game PROPERTIES
    OUTPUT_NAME "nova_game"
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
