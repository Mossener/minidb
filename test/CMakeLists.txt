# Test sources (manual registration for clarity)
set(TEST_SOURCES
    buffer_pool_test.cpp
    b_plus_tree_test.cpp
    table_heap_test.cpp
)

foreach(TEST_FILE ${TEST_SOURCES})
    get_filename_component(TEST_NAME ${TEST_FILE} NAME_WE)
    add_executable(${TEST_NAME} ${TEST_FILE})
    target_link_libraries(${TEST_NAME} PRIVATE minidb_lib Threads::Threads)
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endforeach()

# Benchmark (not a test, just compile and run manually)
add_executable(benchmark benchmark_test.cpp)
target_link_libraries(benchmark PRIVATE minidb_lib Threads::Threads)
