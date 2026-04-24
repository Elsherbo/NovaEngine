# vendor/CMakeLists.txt
set(CMAKE_TOOLCHAIN_FILE "D:/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "vcpkg toolchain")

# GLAD (manually downloaded)
add_subdirectory(GLAD)

add_custom_target(vendor_placeholder)