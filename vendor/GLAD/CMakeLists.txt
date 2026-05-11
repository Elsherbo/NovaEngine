# vendor/GLAD/CMakeLists.txt
# GLAD - GL Loader-Generator (manually downloaded)

add_library(nova_vendor_glad STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/glad.c
)
target_include_directories(nova_vendor_glad PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/src/glad.c PROPERTIES
    LANGUAGE C
    COMPILE_FLAGS "-Wno-pedantic"
)