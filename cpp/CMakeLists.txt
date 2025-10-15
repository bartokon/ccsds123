cmake_minimum_required(VERSION 3.16)
project(ccsds123_residual LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(residual_mapping
    src/modules/residual_map.cpp
    src/modules/residual_unmap.cpp)

target_include_directories(residual_mapping
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(residual_map_tests
    tests/residual_map_tests.cpp)

target_link_libraries(residual_map_tests
    PRIVATE
        residual_mapping)

target_compile_definitions(residual_map_tests
    PRIVATE
        TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")

enable_testing()
add_test(NAME residual_map COMMAND residual_map_tests)
