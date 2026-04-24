# engine/platform/CMakeLists.txt

add_library(nova_platform STATIC)

target_include_directories(nova_platform PUBLIC 
    ${CMAKE_SOURCE_DIR}
    ${SDL3_INCLUDE_DIR}
)

target_link_libraries(nova_platform PUBLIC nova_core)
target_link_libraries(nova_platform PRIVATE ${SDL3_LIBRARY} winmm setupapi imm32 version)

# IPlatform interface
target_sources(nova_platform PUBLIC
    iplatform.h
)

target_sources(nova_platform PRIVATE
    sdl3/sdl3_platform.cpp
)