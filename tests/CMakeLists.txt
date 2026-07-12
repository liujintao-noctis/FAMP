cmake_minimum_required(VERSION 3.22)

# ── Resolve project root (works regardless of which CMakeLists.txt is the
#    entry point — root /CMakeLists.txt or /src/CMakeLists.txt) ──────────
if(DEFINED FAMP_ROOT_DIR)
    set(FAMP_PROJECT_ROOT "${FAMP_ROOT_DIR}")
else()
    set(FAMP_PROJECT_ROOT "${CMAKE_SOURCE_DIR}")
endif()
set(FAMP_SRC_DIR "${FAMP_PROJECT_ROOT}/src")

# ── Google Test via FetchContent ──────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# ── Cloud library (standalone, no GUI deps) ──────────────────────────────
# Cloud.h has a residual #include <qdebug.h>; we pull Qt::Core for its
# headers only, so no Widgets / GUI linkage is needed.
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core PrintSupport Svg Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core PrintSupport Svg Widgets)
find_package(PCL REQUIRED COMPONENTS common features filters io)
find_package(PROJ CONFIG REQUIRED)

add_library(famp_cloud STATIC
    "${FAMP_SRC_DIR}/Cloud.cpp"
    "${FAMP_SRC_DIR}/Cloud.h"
    "${FAMP_SRC_DIR}/CloudCoordinates.h"
    "${FAMP_SRC_DIR}/CloudLoader.cpp"
    "${FAMP_SRC_DIR}/CloudLoader.h"
    "${FAMP_SRC_DIR}/LasLoader.cpp"
    "${FAMP_SRC_DIR}/LasLoader.h"
    "${FAMP_SRC_DIR}/PcdLoader.cpp"
    "${FAMP_SRC_DIR}/PcdLoader.h"
    "${FAMP_SRC_DIR}/RecentFiles.cpp"
    "${FAMP_SRC_DIR}/RecentFiles.h"
)

target_include_directories(famp_cloud
    PUBLIC
        "${FAMP_SRC_DIR}"
        ${PCL_INCLUDE_DIRS}
)

target_link_libraries(famp_cloud
    PUBLIC
        ${PCL_LIBRARIES}
        Qt${QT_VERSION_MAJOR}::Core
        lastools_compat
)

target_compile_definitions(famp_cloud PUBLIC ${PCL_DEFINITIONS})

# ── Test executable ───────────────────────────────────────────────────────
add_executable(famp_tests
    "${FAMP_SRC_DIR}/CrsService.cpp"
    "${FAMP_SRC_DIR}/FileIO.cpp"
    "${FAMP_SRC_DIR}/GraphicsItemTransform.cpp"
    "${FAMP_SRC_DIR}/GraphicsExport.cpp"
    "${FAMP_SRC_DIR}/GraphicsSceneDocument.cpp"
    "${FAMP_SRC_DIR}/GraphicsUndoCommands.cpp"
    "${FAMP_SRC_DIR}/CompassItem.cpp"
    "${FAMP_SRC_DIR}/FormTabulationItem.cpp"
    "${FAMP_SRC_DIR}/MyItem.cpp"
    "${FAMP_SRC_DIR}/Measurement.cpp"
    "${FAMP_SRC_DIR}/MeasurementItem.cpp"
    "${FAMP_SRC_DIR}/CloudProcessing.cpp"
    "${FAMP_SRC_DIR}/CloudDisplaySettings.cpp"
    "${FAMP_SRC_DIR}/ProjectDocument.cpp"
    test_main.cpp
    test_cloud.cpp
    test_cloud_loader.cpp
    test_cloud_display_settings.cpp
    test_cloud_processing.cpp
    test_crs_service.cpp
    test_file_io.cpp
    test_graphics_item_transform.cpp
    test_graphics_export.cpp
    test_graphics_scene_document.cpp
    test_graphics_undo_commands.cpp
    test_help_content.cpp
    test_las_loader.cpp
    test_metric_scale.cpp
    test_measurement.cpp
    test_pcd_loader.cpp
    test_project_document.cpp
    test_recent_files.cpp
    test_string_convert.cpp
)

target_link_libraries(famp_tests
    PRIVATE
        famp_cloud
        GTest::gtest
        Qt${QT_VERSION_MAJOR}::PrintSupport
        Qt${QT_VERSION_MAJOR}::Svg
        Qt${QT_VERSION_MAJOR}::Widgets
        PROJ::proj
        VTK::RenderingCore
)

target_compile_definitions(famp_tests
    PRIVATE
        FAMP_SAMPLE_DIR="${FAMP_PROJECT_ROOT}/samples"
)

target_include_directories(famp_tests
    PRIVATE
        "${FAMP_SRC_DIR}"
)

# gtest-discover-tests scans the binary at build time for registered tests.
include(GoogleTest)
gtest_discover_tests(famp_tests)
