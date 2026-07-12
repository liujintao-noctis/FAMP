cmake_minimum_required(VERSION 3.22)

set(FAMP_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/..")
include("${FAMP_ROOT_DIR}/cmake/FampVersion.cmake")
include("${FAMP_ROOT_DIR}/cmake/FampVcpkg.cmake")

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

project(FAMP
    VERSION "${FAMP_VERSION}"
    DESCRIPTION "Field Archaeology Mapping Program"
    LANGUAGES CXX
)

set(FAMP_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${FAMP_GENERATED_DIR}")
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/Version.h.in"
    "${FAMP_GENERATED_DIR}/Version.h"
    @ONLY
)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(CTest)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS "${FAMP_ROOT_DIR}/ui")

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Concurrent Gui PrintSupport Svg Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Concurrent Gui PrintSupport Svg Widgets)
if(QT_VERSION_MAJOR GREATER_EQUAL 6)
    find_package(Qt6 REQUIRED COMPONENTS OpenGLWidgets)
endif()

find_package(PCL REQUIRED COMPONENTS common io filters features keypoints visualization)
find_package(PROJ CONFIG REQUIRED)
find_package(VTK REQUIRED COMPONENTS
    CommonColor
    CommonCore
    CommonDataModel
    CommonExecutionModel
    CommonMath
    CommonTransforms
    FiltersCore
    FiltersExtraction
    FiltersGeneral
    FiltersGeometry
    FiltersModeling
    FiltersPoints
    FiltersSources
    GUISupportQt
    ImagingCore
    InteractionStyle
    InteractionWidgets
    RenderingAnnotation
    RenderingCore
    RenderingFreeType
    RenderingOpenGL2
)

set(LASTOOLS_DIR "${FAMP_ROOT_DIR}/third_party/lastools")

add_library(lastools_compat STATIC
    "${LASTOOLS_DIR}/LASzip/src/arithmeticdecoder.cpp"
    "${LASTOOLS_DIR}/LASzip/src/arithmeticencoder.cpp"
    "${LASTOOLS_DIR}/LASzip/src/arithmeticmodel.cpp"
    "${LASTOOLS_DIR}/LASzip/src/integercompressor.cpp"
    "${LASTOOLS_DIR}/LASzip/src/lasindex.cpp"
    "${LASTOOLS_DIR}/LASzip/src/lasinterval.cpp"
    "${LASTOOLS_DIR}/LASzip/src/lasquadtree.cpp"
    "${LASTOOLS_DIR}/LASzip/src/lasreaditemcompressed_v1.cpp"
    "${LASTOOLS_DIR}/LASzip/src/lasreaditemcompressed_v2.cpp"
    "${LASTOOLS_DIR}/LASzip/src/lasreaditemcompressed_v3.cpp"
    "${LASTOOLS_DIR}/LASzip/src/lasreaditemcompressed_v4.cpp"
    "${LASTOOLS_DIR}/LASzip/src/lasreadpoint.cpp"
    "${LASTOOLS_DIR}/LASzip/src/laswriteitemcompressed_v1.cpp"
    "${LASTOOLS_DIR}/LASzip/src/laswriteitemcompressed_v2.cpp"
    "${LASTOOLS_DIR}/LASzip/src/laswriteitemcompressed_v3.cpp"
    "${LASTOOLS_DIR}/LASzip/src/laswriteitemcompressed_v4.cpp"
    "${LASTOOLS_DIR}/LASzip/src/laswritepoint.cpp"
    "${LASTOOLS_DIR}/LASzip/src/laszip.cpp"
    "${LASTOOLS_DIR}/LASlib/src/fopen_compressed.cpp"
    "${LASTOOLS_DIR}/LASlib/src/lasfilter.cpp"
    "${LASTOOLS_DIR}/LASlib/src/lasreader.cpp"
    "${LASTOOLS_DIR}/LASlib/src/lasreader_asc.cpp"
    "${LASTOOLS_DIR}/LASlib/src/lasreader_bil.cpp"
    "${LASTOOLS_DIR}/LASlib/src/lasreader_bin.cpp"
    "${LASTOOLS_DIR}/LASlib/src/lasreader_dtm.cpp"
    "${LASTOOLS_DIR}/LASlib/src/lasreader_las.cpp"
    "${LASTOOLS_DIR}/LASlib/src/lasreader_ply.cpp"
    "${LASTOOLS_DIR}/LASlib/src/lasreader_qfit.cpp"
    "${LASTOOLS_DIR}/LASlib/src/lasreader_shp.cpp"
    "${LASTOOLS_DIR}/LASlib/src/lasreader_txt.cpp"
    "${LASTOOLS_DIR}/LASlib/src/lasreaderbuffered.cpp"
    "${LASTOOLS_DIR}/LASlib/src/lasreadermerged.cpp"
    "${LASTOOLS_DIR}/LASlib/src/lasreaderpipeon.cpp"
    "${LASTOOLS_DIR}/LASlib/src/lasreaderstored.cpp"
    "${LASTOOLS_DIR}/LASlib/src/lastransform.cpp"
    "${LASTOOLS_DIR}/LASlib/src/lasutility.cpp"
    "${LASTOOLS_DIR}/LASlib/src/laswaveform13reader.cpp"
    "${LASTOOLS_DIR}/LASlib/src/laswaveform13writer.cpp"
    "${LASTOOLS_DIR}/LASlib/src/laswriter.cpp"
    "${LASTOOLS_DIR}/LASlib/src/laswriter_bin.cpp"
    "${LASTOOLS_DIR}/LASlib/src/laswriter_las.cpp"
    "${LASTOOLS_DIR}/LASlib/src/laswriter_qfit.cpp"
    "${LASTOOLS_DIR}/LASlib/src/laswriter_txt.cpp"
    "${LASTOOLS_DIR}/LASlib/src/laswriter_wrl.cpp"
    "${LASTOOLS_DIR}/LASlib/src/laswritercompatible.cpp"
)

set_target_properties(lastools_compat PROPERTIES
    CXX_STANDARD 14
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

target_include_directories(lastools_compat
    PUBLIC
        "${LASTOOLS_DIR}/LASlib/inc"
        "${LASTOOLS_DIR}/LASzip/src"
)

if(MSVC)
    target_compile_definitions(lastools_compat PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

if(WIN32)
    set(FAMP_EXECUTABLE_TYPE WIN32)
else()
    set(FAMP_EXECUTABLE_TYPE)
endif()

add_executable(FAMP ${FAMP_EXECUTABLE_TYPE}
    Cloud.cpp
    CloudCoordinates.cpp
    CloudDisplaySettings.cpp
    CloudLoader.cpp
    CloudProcessing.cpp
    CompassItem.cpp
    CrsService.cpp
    FAMPController.cpp
    FileIO.cpp
    FormTabulationItem.cpp
    GraphicsItemTransform.cpp
    GraphicsExport.cpp
    GraphicsSceneDocument.cpp
    GraphicsUndoCommands.cpp
    LasLoader.cpp
    MainWindow.cpp
    Measurement.cpp
    MeasurementItem.cpp
    MyGraphicsView.cpp
    MyItem.cpp
    MyVTK.cpp
    PcdLoader.cpp
    ProjectDocument.cpp
    RecentFiles.cpp
    QDlgClip.cpp
    QDlgPlotTab.cpp
    VTKRenderManager.cpp
    VTKPointCloudManager.cpp
    VTKProjectionManager.cpp
    main.cpp
    Cloud.h
    CloudCoordinates.h
    CloudDisplaySettings.h
    CloudLoader.h
    CloudProcessing.h
    CompassItem.h
    CrsService.h
    FAMPController.h
    FileIO.h
    FormTabulationItem.h
    GraphicsItemTransform.h
    GraphicsExport.h
    GraphicsSceneDocument.h
    GraphicsUndoCommands.h
    HelpContent.h
    LasLoader.h
    MainWindow.h
    Measurement.h
    MeasurementItem.h
    MetricScale.h
    MyGraphicsView.h
    MyItem.h
    MyVTK.h
    PcdLoader.h
    ProjectDocument.h
    RecentFiles.h
    QDlgClip.h
    QDlgPlotTab.h
    QstringAndStringConvert.h
    VTKRenderManager.h
    VTKPointCloudManager.h
    VTKProjectionManager.h
    "${FAMP_GENERATED_DIR}/Version.h"
    "${FAMP_ROOT_DIR}/ui/MainWindow.ui"
    "${FAMP_ROOT_DIR}/ui/QDlgClip.ui"
    "${FAMP_ROOT_DIR}/ui/QDlgPlotTab.ui"
    "${FAMP_ROOT_DIR}/resources/MainWindow.qrc"
    "${FAMP_ROOT_DIR}/resources/res.qrc"
)

if(WIN32)
    target_sources(FAMP PRIVATE
        "${FAMP_ROOT_DIR}/resources/images/icon/famp_icon.rc"
    )
endif()

set_target_properties(FAMP PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

target_include_directories(FAMP
    PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}"
        "${FAMP_GENERATED_DIR}"
        "${PCL_INCLUDE_DIRS}"
)

target_compile_definitions(FAMP PRIVATE ${PCL_DEFINITIONS})

if(MSVC)
    target_compile_definitions(FAMP PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
    target_compile_options(FAMP PRIVATE /utf-8)
endif()

target_link_libraries(FAMP
    PRIVATE
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Concurrent
        Qt${QT_VERSION_MAJOR}::Gui
        Qt${QT_VERSION_MAJOR}::PrintSupport
        Qt${QT_VERSION_MAJOR}::Svg
        Qt${QT_VERSION_MAJOR}::Widgets
        PROJ::proj
        ${PCL_LIBRARIES}
        ${VTK_LIBRARIES}
        lastools_compat
)

if(QT_VERSION_MAJOR GREATER_EQUAL 6)
    target_link_libraries(FAMP PRIVATE Qt6::OpenGLWidgets)
endif()

if(WIN32)
    if(TARGET Qt${QT_VERSION_MAJOR}::EntryPoint)
        target_link_libraries(FAMP PRIVATE Qt${QT_VERSION_MAJOR}::EntryPoint)
    elseif(TARGET Qt${QT_VERSION_MAJOR}::WinMain)
        target_link_libraries(FAMP PRIVATE Qt${QT_VERSION_MAJOR}::WinMain)
    endif()
endif()

if(TARGET VTK::CommonCore)
    vtk_module_autoinit(
        TARGETS FAMP
        MODULES ${VTK_LIBRARIES}
    )
endif()

install(TARGETS FAMP RUNTIME DESTINATION bin)

if(UNIX AND NOT APPLE)
    install(FILES "${FAMP_ROOT_DIR}/resources/famp.desktop"
        DESTINATION share/applications
    )
    install(FILES "${FAMP_ROOT_DIR}/resources/images/icon/famp_icon_256.png"
        DESTINATION share/icons/hicolor/256x256/apps
        RENAME famp.png
    )
endif()

if(BUILD_TESTING)
    add_subdirectory("${FAMP_ROOT_DIR}/tests" "${CMAKE_BINARY_DIR}/tests")
endif()
