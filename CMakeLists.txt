cmake_minimum_required(VERSION 3.15)
project(H264_SPS_Parser VERSION 1.0.0 LANGUAGES CXX)

# 设置 C++17 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 强制开启 MSVC UTF-8 编码支持
if(MSVC)
    add_compile_options(/utf-8)
endif()

# 1. 声明 Header-only 接口库目标（零依赖，便于其他工程直接依赖引入）
add_library(h264_sps_parser INTERFACE)
target_include_directories(h264_sps_parser INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# 2. 单元测试套件构建
option(BUILD_TESTS "Build unit tests" ON)
if(BUILD_TESTS)
    enable_testing()
    add_executable(test_h264_parser tests/test_main.cpp)
    target_link_libraries(test_h264_parser PRIVATE h264_sps_parser)
    add_test(NAME H264ParserTest COMMAND test_h264_parser)
endif()
