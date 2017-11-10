cmake_minimum_required(VERSION 3.8)
project(BFDHT)

set(CMAKE_CXX_STANDARD 11)

set(SOURCE_FILES src/main.cpp src/Node.cpp src/Node.h)
add_executable(BFDHT ${SOURCE_FILES})

target_link_libraries(BFDHT crypto++)