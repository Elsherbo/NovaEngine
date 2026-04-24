# vendor/GLAD/CMakeLists.txt
# GLAD - GL Loader-Generator (manually downloaded)

# Just include path - source will be compiled in the module that uses it
add_library(nova_vendor_glad INTERFACE)

target_include_directories(nova_vendor_glad INTERFACE 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)