cmake_minimum_required(VERSION 3.22)

set(FAMP_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}")

# Configure vcpkg before the first project() call. CMake only loads the
# toolchain during initial language enablement, so root builds must set it here.
if(NOT CMAKE_TOOLCHAIN_FILE)
    if(NOT DEFINED VCPKG_ROOT AND DEFINED ENV{VCPKG_ROOT})
        set(VCPKG_ROOT "$ENV{VCPKG_ROOT}")
    endif()
    if(DEFINED VCPKG_ROOT)
        set(CMAKE_TOOLCHAIN_FILE "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "vcpkg toolchain")
        set(VCPKG_INSTALLED_DIR "${VCPKG_ROOT}/installed" CACHE PATH "vcpkg installed tree")
        message(STATUS "Using vcpkg toolchain: ${CMAKE_TOOLCHAIN_FILE}")
    else()
        message(STATUS "VCPKG_ROOT not set; using system packages")
    endif()
else()
    message(STATUS "Using cached toolchain: ${CMAKE_TOOLCHAIN_FILE}")
endif()

set(VCPKG_MANIFEST_DIR "${FAMP_ROOT_DIR}" CACHE PATH "vcpkg manifest directory")
set(VCPKG_OVERLAY_TRIPLETS "${FAMP_ROOT_DIR}/triplets" CACHE PATH "vcpkg overlay triplets")

if(DEFINED CMAKE_TOOLCHAIN_FILE)
    if(WIN32)
        set(DEFAULT_TRIPLET "x64-windows")
    else()
        set(DEFAULT_TRIPLET "x64-linux-release")
    endif()
    set(VCPKG_TARGET_TRIPLET "${DEFAULT_TRIPLET}" CACHE STRING "vcpkg target triplet")
    set(VCPKG_HOST_TRIPLET "${DEFAULT_TRIPLET}" CACHE STRING "vcpkg host triplet")
endif()

project(FAMP LANGUAGES CXX)
add_subdirectory(src)
