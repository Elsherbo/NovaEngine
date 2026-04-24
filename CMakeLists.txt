# CMakeLists.txt - MinGW64 with SDL3 from UCRT64 (pre-built DLL)
cmake_minimum_required(VERSION 3.20)
project(NovaEngine VERSION 0.1.0 LANGUAGES C CXX)

# Use local SDL3 from vendor (MinGW64 built)
# Use DLL import library for dynamic linking
set(SDL3_ROOT "${CMAKE_SOURCE_DIR}/vendor/SDL3")
set(SDL3_INCLUDE_DIR "${SDL3_ROOT}/include")
set(SDL3_LIBRARY "${SDL3_ROOT}/lib/libSDL3.dll.a")

if(EXISTS "${SDL3_INCLUDE_DIR}/SDL3/SDL.h")
    message(STATUS "Using local SDL3 from vendor/SDL3 (DLL)")
    include_directories(${SDL3_INCLUDE_DIR})
else()
    message(FATAL_ERROR "SDL3 not found in vendor/SDL3")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Compiler warnings
add_compile_options(-Wall -Wextra -Wpedantic -Werror)

# Build type default
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Vendor / external dependencies
add_subdirectory(vendor)

# Engine static library
add_subdirectory(engine)

# Game shared library
add_subdirectory(game)

# Offline tools (qbsp, light, vis)
add_subdirectory(tools)

# Tests (CTest)
enable_testing()
add_subdirectory(tests)

# ---- Nova Engine Executable ----
add_executable(nova_app
    engine/core/engine.cpp
)

# Add glad.c as C code and disable pedantic warnings for it
target_sources(nova_app PRIVATE
    vendor/GLAD/src/glad.c
)
set_source_files_properties(vendor/GLAD/src/glad.c PROPERTIES 
    LANGUAGE C
    COMPILE_FLAGS "-Wno-pedantic"
)
target_link_libraries(nova_app PRIVATE
    nova_engine
    ${SDL3_LIBRARY}
    nova_vendor_glad
    opengl32
    -lwinmm
    -lsetupapi
    -limm32
    -lversion
)
target_include_directories(nova_app PRIVATE ${CMAKE_SOURCE_DIR})
set_target_properties(nova_app PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)