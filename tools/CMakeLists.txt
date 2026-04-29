# tools/CMakeLists.txt
# Offline compile tools: qbsp, light, vis.
# Each becomes its own executable in Phase 1 (light/vis) or Phase 2 (qbsp).

# add_subdirectory(qbsp)
# add_subdirectory(light)
# add_subdirectory(vis)

# 1. Setup Paths
set(Q2TOOL_PATH "${CMAKE_SOURCE_DIR}/tools/external/q2tools/bin/q2tool.exe")
set(ASSETS_DIR "${CMAKE_SOURCE_DIR}/assets")
set(MAP_SRC_DIR "${ASSETS_DIR}/maps")
set(MAP_OUT_DIR "${CMAKE_BINARY_DIR}/assets/maps")

# Ensure output directory exists
file(MAKE_DIRECTORY ${MAP_OUT_DIR})

# 2. Map Compilation Function
function(add_nova_map MAP_NAME)
    add_custom_command(
        OUTPUT "${MAP_OUT_DIR}/${MAP_NAME}.bsp"
        # q2tool flags:
        # -b: BSP pass (with -v220 for TrenchBroom)
        # -v: VIS pass (calculates what is visible)
        # -l: RAD/Light pass (bakes lighting)
        # -f: Force overwrite
        # -basedir: Points to where your textures/WADs are
        COMMAND ${Q2TOOL_PATH} -b -v220 -v -l -f 
                -basedir "${ASSETS_DIR}" 
                "${MAP_SRC_DIR}/${MAP_NAME}.map"
        
        # Copy the resulting BSP to the output folder after compilation
        COMMAND ${CMAKE_COMMAND} -E copy "${MAP_SRC_DIR}/${MAP_NAME}.bsp" "${MAP_OUT_DIR}/${MAP_NAME}.bsp"
        
        DEPENDS "${MAP_SRC_DIR}/${MAP_NAME}.map"
        COMMENT "NovaEngine: Compiling and Baking ${MAP_NAME}..."
        WORKING_DIRECTORY ${MAP_SRC_DIR}
    )

    # Create a target so you can run 'make map_test' or build it in your IDE
    add_custom_target(map_${MAP_NAME} ALL DEPENDS "${MAP_OUT_DIR}/${MAP_NAME}.bsp")
endfunction()

# 3. Register your maps here
# Example: If you have assets/maps/test_room.map
# add_nova_map(test_room)

add_custom_target(nova_tools_placeholder)
