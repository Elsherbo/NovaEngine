# engine/renderer/CMakeLists.txt

add_library(nova_renderer STATIC)

target_include_directories(nova_renderer PUBLIC ${CMAKE_SOURCE_DIR})
target_include_directories(nova_renderer PRIVATE ${SDL3_INCLUDE_DIR})

target_link_libraries(nova_renderer PUBLIC nova_core nova_platform)
target_link_libraries(nova_renderer PUBLIC nova_vendor_glad)

# IRenderBackend interface
target_sources(nova_renderer PUBLIC
    irender_backend.h
)

# OpenGL implementation
target_sources(nova_renderer PRIVATE
    gl/gl_backend.h
    gl/gl_backend.cpp
)

# BSP loader
target_sources(nova_renderer PRIVATE
    bsp/bsp.h
    bsp/bsp_loader.cpp
)