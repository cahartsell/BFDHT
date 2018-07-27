cmake_minimum_required(VERSION 3.0)

option(BBB_CROSS_COMPILE "Cross compile for Beaglebone Black (armhf)" OFF)
option(VERBOSE_COMPILE "Set verbose flag for compiler" OFF)

# Cross compile for beaglebone black
if(BBB_CROSS_COMPILE)
	MESSAGE(STATUS "Configuring for cross compilation to BBB")
	set(CMAKE_CXX_COMPILER /usr/bin/arm-linux-gnueabihf-g++)
	set(CMAKE_C_COMPILER /usr/bin/arm-linux-gnueabihf-gcc)
	# Tell CMake to only search target paths (not host)
	set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
	set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
	set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
endif()

project(BFDHT)

set(CMAKE_CXX_STANDARD 11)
INCLUDE_DIRECTORIES(include)

# Verbose compilation
if(VERBOSE_COMPILE)
	set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -v")
	set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -v")
endif()

set(SOURCE_FILES src/main.cpp src/Node.cpp src/Chord.cpp)
add_executable(BFDHT ${SOURCE_FILES})

target_link_libraries(BFDHT crypto++ pthread)
