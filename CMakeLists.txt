cmake_minimum_required(VERSION 3.16)
project(MiniDB VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -g -O0")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUG")

# Find dependencies
find_package(Threads REQUIRED)

# Collect sources (exclude main.cpp from library)
file(GLOB_RECURSE MINIDB_SOURCES "src/*.cpp")
file(GLOB_RECURSE MINIDB_TEST_SOURCES "test/*.cpp")
list(REMOVE_ITEM MINIDB_SOURCES "${CMAKE_SOURCE_DIR}/src/main.cpp")

# Main library
add_library(minidb_lib STATIC ${MINIDB_SOURCES})
target_include_directories(minidb_lib PUBLIC src/include)
target_link_libraries(minidb_lib PUBLIC Threads::Threads)

# Main executable
add_executable(minidb src/main.cpp src/linenoise.c)
target_link_libraries(minidb PRIVATE minidb_lib Threads::Threads)

# Tests
enable_testing()
add_subdirectory(test)
