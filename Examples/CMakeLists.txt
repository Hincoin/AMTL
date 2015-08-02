project(AMTL_Examples CXX)
cmake_minimum_required (VERSION 3.0.0)

include_directories(${AMTL_Core_SOURCE_DIR})

add_executable(AMTL_Examples main.cpp)
target_link_libraries(AMTL_Examples AMTL_Core)

