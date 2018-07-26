cmake_minimum_required(VERSION 3.0)
project(dht_test)

set(CMAKE_CXX_STANDARD 11)

INCLUDE_DIRECTORIES("opendht/include/")
LINK_DIRECTORIES("opendht/build")

add_executable(node src/nodeMain.cpp)

TARGET_LINK_LIBRARIES(node opendht)
TARGET_LINK_LIBRARIES(node pthread)