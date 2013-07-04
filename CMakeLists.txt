cmake_minimum_required(VERSION 2.8)

# Must set UV_ROOT and LCB_ROOT
INCLUDE_DIRECTORIES(
    ${UV_ROOT}/include
    ${LCB_ROOT}/include
    )

LINK_DIRECTORIES(${UV_ROOT}/Release)
SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /W3 /EHsc /FD")


ADD_LIBRARY(plugin-libuv SHARED
    src/iops.c
    src/plugin.c
    src/socket.c
    src/timer.c
    src/util.c)

TARGET_LINK_LIBRARIES(plugin-libuv libuv.lib)
