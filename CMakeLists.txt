cmake_minimum_required (VERSION 2.8.8)

project(AMTL)

set(CMAKE_CXX_FLAGS_DEBUG "-std=c++0x -fPIC -DPIC -O0 -g3 -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-std=c++0x -fPIC -DPIC -O3 -DNDEBUG -mfpmath=sse,387 -msse2 -msse3")

if(CMAKE_COMPILER_IS_GNUCXX)
    message(STATUS "GCC detected, adding compile flags")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS} -pedantic -Wall -Wextra")
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS} -pedantic -Wall -Wextra")
endif(CMAKE_COMPILER_IS_GNUCXX)

add_subdirectory(AMTL)

option(BUILD_EXAMPLES "Build examples" ON)

if (BUILD_EXAMPLES)
	add_subdirectory(Examples)
endif (BUILD_EXAMPLES)
