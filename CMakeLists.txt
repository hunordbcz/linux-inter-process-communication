cmake_minimum_required(VERSION 3.16)
project(assig3 C)

set(CMAKE_C_STANDARD 99)

add_executable(assig3 a3.c)
find_library(LIBRT rt)
if (LIBRT)
    target_link_libraries(assig3 ${LIBRT})
endif ()