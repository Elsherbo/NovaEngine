# engine/core/CMakeLists.txt

add_library(nova_core STATIC)

target_include_directories(nova_core PUBLIC ${CMAKE_SOURCE_DIR})
target_include_directories(nova_core PRIVATE 
    ${CMAKE_SOURCE_DIR}/vendor/GLAD/include
    ${SDL3_INCLUDE_DIR}
)

# Link GLAD for the console's raw GL calls
target_link_libraries(nova_core PRIVATE
    nova_vendor_glad
)


# Math
target_sources(nova_core PUBLIC
    math/math.h
    math/common.h
    math/vec.h
    math/mat4.h
    math/quat.h
    math/shapes.h

    # CVar system — public so game DLL and all engine modules can register cvars
    cvar.h
 
    # In-game console overlay
    console.h

    # 2D text rendering (Quake2-style overlay)
    text_2d.h
)

# Console and CVars
target_sources(nova_core PRIVATE
    cvar.cpp

    # Console — needs GLAD for GL calls, SDL3 for scancode constants
    console.cpp

    # 2D text rendering (Quake2-style overlay)
    text_2d.h
    text_2d.cpp
)


# Memory
target_sources(nova_core PRIVATE
    memory/arena.h
    memory/arena.cpp
    memory/zone.h
    memory/zone.cpp
    memory/pool.h
    memory/pool.cpp
)

# Logging
target_sources(nova_core PRIVATE
    log.h
    log.cpp
)

# Camera
target_sources(nova_core PRIVATE
    camera.h
    camera.cpp
)

# Image (minimal loaders)
target_sources(nova_core PRIVATE
    image_tga.h
    image_tga.cpp
    image_load.h
    image_load.cpp
    asset_fs.h
    asset_fs.cpp
)

# Engine main loop (executable target, not library — handled separately)