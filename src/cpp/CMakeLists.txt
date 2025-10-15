cmake_minimum_required(VERSION 3.20)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_library(ccsds123 STATIC
    src/Ccsds123Codec.cpp
    src/modules.cpp)

target_include_directories(ccsds123
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/cpp/include>
        $<INSTALL_INTERFACE:include>)

target_compile_features(ccsds123 PUBLIC cxx_std_20)

add_executable(ccsds123_encode
    src/ccsds123_encode_main.cpp)
target_link_libraries(ccsds123_encode PRIVATE ccsds123)

add_executable(ccsds123_decode
    src/ccsds123_decode_main.cpp)
target_link_libraries(ccsds123_decode PRIVATE ccsds123)

if(BUILD_TESTING)
    add_executable(ccsds123_tests
        tests/codec_tests.cpp
        tests/local_diff_vectors.cpp)
    target_include_directories(ccsds123_tests PRIVATE tests)
    target_link_libraries(ccsds123_tests PRIVATE ccsds123)
    target_compile_definitions(ccsds123_tests PRIVATE PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    add_test(NAME codec_unit_tests COMMAND ccsds123_tests)
endif()

add_custom_target(hdl_ref
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/generate_gradient.py
            --nx 8 --ny 8 --nz 3
            --output ${CMAKE_CURRENT_BINARY_DIR}/rgb8x8_u16le.bsq
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/hdl_compare.py
            --nx 8 --ny 8 --nz 3 --depth 16
            --reference ${CMAKE_CURRENT_BINARY_DIR}/rgb8x8_u16le.bsq
            --hdl-recon ${CMAKE_CURRENT_BINARY_DIR}/rgb8x8_u16le.bsq
    COMMENT "Running HDL comparison stub against generated sample data")
