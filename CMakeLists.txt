cmake_minimum_required(VERSION 3.8)
project(BFDHT)

set(CMAKE_CXX_STANDARD 11)

include_directories(include)

set(SOURCE_FILES src/main.cpp src/Node.cpp src/Chord.cpp)
add_executable(BFDHT ${SOURCE_FILES})

target_link_libraries(BFDHT crypto++ zmq)