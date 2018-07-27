cmake_minimum_required(VERSION 3.0)

option(BBB_CROSS_COMPILE "Cross compile for Beaglebone Black (armhf)" OFF)

if(BBB_CROSS_COMPILE)
	MESSAGE(STATUS "Configuring for cross compilation to BBB")
	set(CMAKE_CXX_COMPILER /usr/bin/arm-linux-gnueabihf-g++)
	set(CMAKE_C_COMPILER /usr/bin/arm-linux-gnueabihf-gcc)
	# Tell CMake to only search target paths (not host)
	set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
	set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
	set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
endif()

project(dht_test)

set(CMAKE_CXX_STANDARD 11)

INCLUDE_DIRECTORIES("opendht/include/")
LINK_DIRECTORIES("opendht/build")

ADD_EXECUTABLE(node src/nodeMain.cpp)

TARGET_LINK_LIBRARIES(node opendht)
TARGET_LINK_LIBRARIES(node pthread)

set(CMAKE_CXX_STANDARD 11)

INCLUDE_DIRECTORIES(include)
