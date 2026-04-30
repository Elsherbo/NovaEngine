# tools/CMakeLists.txt
# Map compilation using ericw-tools

# 1. Setup Paths
set(QBSP_EXE "${CMAKE_SOURCE_DIR}/tools/external/ericw-tools/qbsp.exe")
set(VIS_EXE "${CMAKE_SOURCE_DIR}/tools/external/ericw-tools/vis.exe")
set(LIGHT_EXE "${CMAKE_SOURCE_DIR}/tools/external/ericw-tools/light.exe")
set(ASSETS_DIR "${CMAKE_SOURCE_DIR}/assets")
set(MAP_SRC_DIR "${ASSETS_DIR}/maps")
set(MAP_OUT_DIR "${CMAKE_BINARY_DIR}/assets/maps")

# Ensure output directory exists
file(MAKE_DIRECTORY ${MAP_OUT_DIR})

# Copy textures to build for BSP loader
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/assets/textures")
file(COPY "${ASSETS_DIR}/textures" DESTINATION "${CMAKE_BINARY_DIR}/assets")

# Copy pics (for palette)
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/assets/pics")
file(COPY "${ASSETS_DIR}/pics" DESTINATION "${CMAKE_BINARY_DIR}/assets")

# 2. Map Compilation Function
function(add_nova_map MAP_NAME)
    # BSP pass - writes to source dir
    add_custom_command(
        OUTPUT "${MAP_SRC_DIR}/${MAP_NAME}.bsp"
        COMMAND ${QBSP_EXE} -q2bsp "${MAP_SRC_DIR}/${MAP_NAME}.map"
        DEPENDS "${MAP_SRC_DIR}/${MAP_NAME}.map"
        COMMENT "NovaEngine: Compiling BSP ${MAP_NAME}..."
        WORKING_DIRECTORY ${MAP_SRC_DIR}
    )
    
    # Copy BSP to build dir
    add_custom_command(
        OUTPUT "${MAP_OUT_DIR}/${MAP_NAME}.bsp"
        COMMAND ${CMAKE_COMMAND} -E copy "${MAP_SRC_DIR}/${MAP_NAME}.bsp" "${MAP_OUT_DIR}/${MAP_NAME}.bsp"
        DEPENDS "${MAP_SRC_DIR}/${MAP_NAME}.bsp"
        COMMENT "NovaEngine: Copying ${MAP_NAME}.bsp to build..."
    )
    
    # VIS pass (skip for now - needs correct BSP path)
    # Light pass (skip for now - needs correct BSP path)
    
    # Final BSP target 
    add_custom_target(map_${MAP_NAME} ALL 
        DEPENDS "${MAP_OUT_DIR}/${MAP_NAME}.bsp"
    )
    
    add_custom_target(build_${MAP_NAME} DEPENDS map_${MAP_NAME})
endfunction()

# 3. Register your maps here
add_nova_map(test)

add_custom_target(nova_tools_placeholder)