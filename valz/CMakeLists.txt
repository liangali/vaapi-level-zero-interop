cmake_minimum_required(VERSION 3.0)

project(valz)

set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g")
set(CMAKE_CXX_FLAGS_RELEASE "-O0 -g")

include_directories("/usr/local/include/level_zero")

add_executable(valz main.cpp)

target_link_libraries(valz ze_loader va va-drm)