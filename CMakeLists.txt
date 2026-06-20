cmake_minimum_required(VERSION 3.22)

project(FAMP
    VERSION 0.1.0
    DESCRIPTION "Field Archaeology Mapping Program"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/ui")

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Gui Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Widgets)
if(QT_VERSION_MAJOR GREATER_EQUAL 6)
    find_package(Qt6 REQUIRED COMPONENTS OpenGLWidgets)
endif()

find_package(PCL REQUIRED COMPONENTS common io filters features keypoints visualization)
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

set(LASTOOLS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/lastools")

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

target_include_directories(lastools_compat
    PUBLIC
        "${LASTOOLS_DIR}/LASlib/inc"
        "${LASTOOLS_DIR}/LASzip/src"
)

if(MSVC)
    target_compile_definitions(lastools_compat PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

add_executable(FAMP
    src/Cloud.cpp
    src/MainWindow.cpp
    src/MyGraphicsView.cpp
    src/MyVTK.cpp
    src/QDlgClip.cpp
    src/QDlgPlotTab.cpp
    src/main.cpp
    src/Cloud.h
    src/MainWindow.h
    src/MyGraphicsView.h
    src/MyVTK.h
    src/QDlgClip.h
    src/QDlgPlotTab.h
    src/QstringAndStringConvert.h
    ui/MainWindow.ui
    ui/QDlgClip.ui
    ui/QDlgPlotTab.ui
    resources/MainWindow.qrc
    resources/res.qrc
)

target_include_directories(FAMP
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
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
        Qt${QT_VERSION_MAJOR}::Gui
        Qt${QT_VERSION_MAJOR}::Widgets
        ${PCL_LIBRARIES}
        ${VTK_LIBRARIES}
        lastools_compat
)

if(QT_VERSION_MAJOR GREATER_EQUAL 6)
    target_link_libraries(FAMP PRIVATE Qt6::OpenGLWidgets)
endif()

if(TARGET VTK::CommonCore)
    vtk_module_autoinit(
        TARGETS FAMP
        MODULES ${VTK_LIBRARIES}
    )
endif()

install(TARGETS FAMP RUNTIME DESTINATION bin)
