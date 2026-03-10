cmake_minimum_required(VERSION 3.10)
project(iot_ide C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -g")

# ── SDK 路径（zip 解压后放到项目根目录的 LinkSDK/ 子目录）──
set(SDK_DIR ${CMAKE_SOURCE_DIR}/LinkSDK)

# ── SDK 头文件 ──
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${SDK_DIR}/core
    ${SDK_DIR}/components/dm
    ${SDK_DIR}/external/mbedtls/include
    ${SDK_DIR}/portfiles/aiot_port
)

# ── 收集 SDK 源文件 ──
file(GLOB_RECURSE SDK_CORE_SRC    "${SDK_DIR}/core/*.c")
file(GLOB_RECURSE SDK_DM_SRC      "${SDK_DIR}/components/dm/*.c")
file(GLOB_RECURSE SDK_MBEDTLS_SRC "${SDK_DIR}/external/mbedtls/library/*.c")
file(GLOB_RECURSE SDK_PORT_SRC    "${SDK_DIR}/portfiles/aiot_port/*.c")

# ── 业务源文件 ──
file(GLOB APP_SRC "src/*.c")

# ── 可执行文件 ──
add_executable(iot_ide
    ${APP_SRC}
    ${SDK_CORE_SRC}
    ${SDK_DM_SRC}
    ${SDK_MBEDTLS_SRC}
    ${SDK_PORT_SRC}
)

# ── 链接系统库 ──
target_link_libraries(iot_ide
    pthread
    curl
    zip
    m
)

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "SDK dir:    ${SDK_DIR}")
