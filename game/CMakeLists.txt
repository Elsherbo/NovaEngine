# game/CMakeLists.txt
# Game DLL — implements IGameModule, isolated from engine internals.
# Not built until Phase 2 (IGameModule interface exists).

# add_library(nova_game SHARED
#     src/game_main.cpp
# )
# target_link_libraries(nova_game PRIVATE nova_engine)

add_custom_target(nova_game_placeholder)
