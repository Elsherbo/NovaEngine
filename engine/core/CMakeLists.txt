# engine/core/CMakeLists.txt

add_library(nova_core STATIC)

target_include_directories(nova_core PUBLIC ${CMAKE_SOURCE_DIR})
target_include_directories(nova_core PRIVATE ${SDL3_INCLUDE_DIR})

# Math
target_sources(nova_core PUBLIC
    math/math.h
    math/common.h
    math/vec.h
    math/mat4.h
    math/quat.h
    math/shapes.h
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