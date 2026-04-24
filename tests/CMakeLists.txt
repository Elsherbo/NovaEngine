# tests/CMakeLists.txt

add_executable(test_math core/test_math.cpp)
target_link_libraries(test_math PRIVATE nova_core)
add_test(NAME MathTests COMMAND test_math)

add_executable(test_memory core/test_memory.cpp)
target_link_libraries(test_memory PRIVATE nova_core)
add_test(NAME MemoryTests COMMAND test_memory)

add_executable(test_log core/test_log.cpp)
target_link_libraries(test_log PRIVATE nova_core)
add_test(NAME LogTests COMMAND test_log)

# add_executable(test_bsp renderer/test_bsp.cpp)
# target_link_libraries(test_bsp PRIVATE nova_engine)
# add_test(NAME BSPTests COMMAND test_bsp)

add_executable(test_entity entities/test_entity.cpp)
target_link_libraries(test_entity PRIVATE nova_entities)
add_test(NAME EntityTests COMMAND test_entity)