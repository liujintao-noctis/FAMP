cmake_minimum_required(VERSION 3.22)

set(FAMP_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}")
include("${FAMP_ROOT_DIR}/cmake/FampVcpkg.cmake")

project(FAMP LANGUAGES CXX)
include(CTest)
add_subdirectory(src)
