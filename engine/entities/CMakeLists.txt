# ============================================================
# Entity Module CMake
# ============================================================

add_library(nova_entities STATIC
    entity_id.h
    entity.h
    entity_list.h
    entity_list.cpp
    igame_module.h
    game_dll_loader.h
    game_dll_loader.cpp
)

target_include_directories(nova_entities PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../core
    ${CMAKE_CURRENT_SOURCE_DIR}/../platform
)

target_link_libraries(nova_entities nova_core nova_renderer)