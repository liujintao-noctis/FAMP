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

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(CTest)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(FAMP_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${FAMP_GENERATED_DIR}")
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/app/Version.h.in"
    "${FAMP_GENERATED_DIR}/Version.h"
    @ONLY
)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Concurrent Gui PrintSupport Svg Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Concurrent Gui PrintSupport Svg Widgets)
if(QT_VERSION_MAJOR GREATER_EQUAL 6)
    find_package(Qt6 REQUIRED COMPONENTS OpenGLWidgets)
endif()

qt_wrap_ui(FAMP_UI_HEADERS
    "${FAMP_ROOT_DIR}/ui/MainWindow.ui"
    "${FAMP_ROOT_DIR}/ui/QDlgClip.ui"
    "${FAMP_ROOT_DIR}/ui/QDlgPlotTab.ui"
)

find_package(PCL REQUIRED COMPONENTS common io filters features keypoints registration visualization)
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

# The basename-only includes used by the released code remain supported during
# the physical migration. New code should include from its owning module.
set(FAMP_SOURCE_INCLUDE_DIRS
    "${CMAKE_CURRENT_LIST_DIR}"
    "${CMAKE_CURRENT_LIST_DIR}/app"
    "${CMAKE_CURRENT_LIST_DIR}/core/tasks"
    "${CMAKE_CURRENT_LIST_DIR}/core/text"
    "${CMAKE_CURRENT_LIST_DIR}/domain/cloud"
    "${CMAKE_CURRENT_LIST_DIR}/domain/archaeology"
    "${CMAKE_CURRENT_LIST_DIR}/domain/measurement"
    "${CMAKE_CURRENT_LIST_DIR}/domain/analysis/terrain"
    "${CMAKE_CURRENT_LIST_DIR}/domain/analysis/profile"
    "${CMAKE_CURRENT_LIST_DIR}/domain/analysis/cutfill"
    "${CMAKE_CURRENT_LIST_DIR}/application/processing"
    "${CMAKE_CURRENT_LIST_DIR}/application/workspace"
    "${CMAKE_CURRENT_LIST_DIR}/application/project"
    "${CMAKE_CURRENT_LIST_DIR}/application/analysis"
    "${CMAKE_CURRENT_LIST_DIR}/application/reporting"
    "${CMAKE_CURRENT_LIST_DIR}/infrastructure/cloud_io"
    "${CMAKE_CURRENT_LIST_DIR}/infrastructure/filesystem"
    "${CMAKE_CURRENT_LIST_DIR}/infrastructure/persistence"
    "${CMAKE_CURRENT_LIST_DIR}/infrastructure/geospatial"
    "${CMAKE_CURRENT_LIST_DIR}/infrastructure/reporting"
    "${CMAKE_CURRENT_LIST_DIR}/presentation/shell"
    "${CMAKE_CURRENT_LIST_DIR}/presentation/entity_tree"
    "${CMAKE_CURRENT_LIST_DIR}/presentation/dialogs"
    "${CMAKE_CURRENT_LIST_DIR}/presentation/viewport3d"
    "${CMAKE_CURRENT_LIST_DIR}/presentation/canvas2d"
    "${CMAKE_CURRENT_LIST_DIR}/presentation/common"
    "${FAMP_GENERATED_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}"
)

add_library(famp_build_settings INTERFACE)
target_include_directories(famp_build_settings
    INTERFACE
        ${FAMP_SOURCE_INCLUDE_DIRS}
        "${PCL_INCLUDE_DIRS}"
)
target_compile_definitions(famp_build_settings INTERFACE ${PCL_DEFINITIONS})
target_link_libraries(famp_build_settings
    INTERFACE
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
    target_link_libraries(famp_build_settings INTERFACE Qt6::OpenGLWidgets)
endif()

function(famp_configure_module target)
    set_target_properties(${target} PROPERTIES AUTOMOC ON)
    target_link_libraries(${target} PRIVATE famp_build_settings)
    if(MSVC)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
        target_compile_options(${target} PRIVATE /utf-8)
    endif()
endfunction()

function(famp_enable_ui target)
    target_sources(${target} PRIVATE ${FAMP_UI_HEADERS})
endfunction()

add_library(famp_core OBJECT
    core/tasks/TaskCancellation.h
    core/tasks/TaskManager.cpp
    core/tasks/TaskManager.h
    core/text/QstringAndStringConvert.h
)
famp_configure_module(famp_core)
add_library(famp::core ALIAS famp_core)

add_library(famp_domain OBJECT
    domain/cloud/Cloud.cpp
    domain/cloud/Cloud.h
    domain/cloud/CloudAttributes.cpp
    domain/cloud/CloudAttributes.h
    domain/cloud/CloudCoordinates.cpp
    domain/cloud/CloudCoordinates.h
    domain/cloud/CloudDisplaySettings.cpp
    domain/cloud/CloudDisplaySettings.h
    domain/cloud/CloudLayer.cpp
    domain/cloud/CloudLayer.h
    domain/archaeology/ArchaeologyMetadata.cpp
    domain/archaeology/ArchaeologyMetadata.h
    domain/archaeology/ControlPoints.cpp
    domain/archaeology/ControlPoints.h
    domain/measurement/Measurement.cpp
    domain/measurement/Measurement.h
    domain/analysis/terrain/TerrainAnalysis.cpp
    domain/analysis/terrain/TerrainAnalysis.h
    domain/analysis/profile/ProfileAnalysis.cpp
    domain/analysis/profile/ProfileAnalysis.h
    domain/analysis/cutfill/CutFillAnalysis.cpp
    domain/analysis/cutfill/CutFillAnalysis.h
)
famp_configure_module(famp_domain)
add_library(famp::domain ALIAS famp_domain)

add_library(famp_processing OBJECT
    application/processing/CloudCrop.cpp
    application/processing/CloudCrop.h
    application/processing/CloudProjection.cpp
    application/processing/CloudProjection.h
    application/processing/CloudProcessing.cpp
    application/processing/CloudProcessing.h
    application/processing/CloudRegistration.cpp
    application/processing/CloudRegistration.h
    application/processing/CloudReprojection.cpp
    application/processing/CloudReprojection.h
    application/processing/ProcessingRecipe.cpp
    application/processing/ProcessingRecipe.h
)
famp_configure_module(famp_processing)
add_library(famp::processing ALIAS famp_processing)

add_library(famp_cloud_io OBJECT
    infrastructure/cloud_io/AdditionalCloudLoader.cpp
    infrastructure/cloud_io/AdditionalCloudLoader.h
    infrastructure/cloud_io/CloudLoader.cpp
    infrastructure/cloud_io/CloudLoader.h
    infrastructure/cloud_io/LasLoader.cpp
    infrastructure/cloud_io/LasLoader.h
    infrastructure/cloud_io/PcdLoader.cpp
    infrastructure/cloud_io/PcdLoader.h
)
famp_configure_module(famp_cloud_io)
add_library(famp::cloud_io ALIAS famp_cloud_io)

add_library(famp_filesystem OBJECT
    infrastructure/filesystem/FileIO.cpp
    infrastructure/filesystem/FileIO.h
    infrastructure/filesystem/RecentFiles.cpp
    infrastructure/filesystem/RecentFiles.h
)
famp_configure_module(famp_filesystem)
add_library(famp::filesystem ALIAS famp_filesystem)

add_library(famp_persistence OBJECT
    infrastructure/persistence/CutFillIO.cpp
    infrastructure/persistence/CutFillIO.h
    infrastructure/persistence/ProfileIO.cpp
    infrastructure/persistence/ProfileIO.h
    infrastructure/persistence/ProjectDocument.cpp
    infrastructure/persistence/ProjectDocument.h
    infrastructure/persistence/TerrainIO.cpp
    infrastructure/persistence/TerrainIO.h
)
famp_configure_module(famp_persistence)
add_library(famp::persistence ALIAS famp_persistence)

add_library(famp_geospatial OBJECT
    infrastructure/geospatial/CrsService.cpp
    infrastructure/geospatial/CrsService.h
)
famp_configure_module(famp_geospatial)
add_library(famp::geospatial ALIAS famp_geospatial)

add_library(famp_reporting OBJECT
    infrastructure/reporting/ArchaeologyReport.cpp
    infrastructure/reporting/ArchaeologyReport.h
)
famp_configure_module(famp_reporting)
add_library(famp::reporting ALIAS famp_reporting)

add_library(famp_canvas2d OBJECT
    presentation/canvas2d/CompassItem.cpp
    presentation/canvas2d/CompassItem.h
    presentation/canvas2d/ContourItem.cpp
    presentation/canvas2d/ContourItem.h
    presentation/canvas2d/FormTabulationItem.cpp
    presentation/canvas2d/FormTabulationItem.h
    presentation/canvas2d/GraphicsExport.cpp
    presentation/canvas2d/GraphicsExport.h
    presentation/canvas2d/GraphicsItemTransform.cpp
    presentation/canvas2d/GraphicsItemTransform.h
    presentation/canvas2d/GraphicsSceneDocument.cpp
    presentation/canvas2d/GraphicsSceneDocument.h
    presentation/canvas2d/GraphicsUndoCommands.cpp
    presentation/canvas2d/GraphicsUndoCommands.h
    presentation/canvas2d/MeasurementItem.cpp
    presentation/canvas2d/MeasurementItem.h
    presentation/canvas2d/MyGraphicsView.cpp
    presentation/canvas2d/MyGraphicsView.h
    presentation/canvas2d/MyItem.cpp
    presentation/canvas2d/MyItem.h
    presentation/common/HelpContent.h
    presentation/common/MetricGrid.cpp
    presentation/common/MetricGrid.h
    presentation/common/MetricScale.h
)
famp_configure_module(famp_canvas2d)
famp_enable_ui(famp_canvas2d)
add_library(famp::canvas2d ALIAS famp_canvas2d)

add_library(famp_viewport3d OBJECT
    presentation/viewport3d/MyVTK.cpp
    presentation/viewport3d/MyVTK.h
    presentation/viewport3d/PlaneWidgetPlacement.cpp
    presentation/viewport3d/PlaneWidgetPlacement.h
    presentation/viewport3d/VTKPointCloudManager.cpp
    presentation/viewport3d/VTKPointCloudManager.h
    presentation/viewport3d/VTKProjectionManager.cpp
    presentation/viewport3d/VTKProjectionManager.h
    presentation/viewport3d/VTKRenderManager.cpp
    presentation/viewport3d/VTKRenderManager.h
)
famp_configure_module(famp_viewport3d)
famp_enable_ui(famp_viewport3d)
add_library(famp::viewport3d ALIAS famp_viewport3d)

add_library(famp_dialogs OBJECT
    presentation/dialogs/ArchaeologyMetadataDialog.cpp
    presentation/dialogs/ArchaeologyMetadataDialog.h
    presentation/dialogs/ControlPointDialog.cpp
    presentation/dialogs/ControlPointDialog.h
    presentation/dialogs/CutFillDialog.cpp
    presentation/dialogs/CutFillDialog.h
    presentation/dialogs/ProfileDialog.cpp
    presentation/dialogs/ProfileDialog.h
    presentation/dialogs/QDlgClip.cpp
    presentation/dialogs/QDlgClip.h
    presentation/dialogs/QDlgPlotTab.cpp
    presentation/dialogs/QDlgPlotTab.h
    presentation/dialogs/TerrainDialog.cpp
    presentation/dialogs/TerrainDialog.h
)
famp_configure_module(famp_dialogs)
famp_enable_ui(famp_dialogs)
add_library(famp::dialogs ALIAS famp_dialogs)

add_library(famp_shell OBJECT
    presentation/shell/FAMPController.cpp
    presentation/shell/FAMPController.h
    presentation/shell/MainWindow.cpp
    presentation/shell/MainWindow.h
)
famp_configure_module(famp_shell)
famp_enable_ui(famp_shell)
add_library(famp::shell ALIAS famp_shell)

# Stable v1 workspace model. It deliberately has no presentation dependency.
add_library(famp_workspace OBJECT
    application/workspace/EntityWriterRegistry.cpp
    application/workspace/EntityWriterRegistry.h
    application/workspace/RendererRegistry.cpp
    application/workspace/RendererRegistry.h
    application/workspace/WorkspaceEntity.cpp
    application/workspace/WorkspaceEntity.h
    application/workspace/WorkspaceStore.cpp
    application/workspace/WorkspaceStore.h
    application/workspace/WorkspaceSnapshot.cpp
    application/workspace/WorkspaceSnapshot.h
)
famp_configure_module(famp_workspace)
add_library(famp::workspace ALIAS famp_workspace)

add_library(famp_entity_tree OBJECT
    presentation/entity_tree/EntityTreeModel.cpp
    presentation/entity_tree/EntityTreeModel.h
)
famp_configure_module(famp_entity_tree)
add_library(famp::entity_tree ALIAS famp_entity_tree)

# Reserved stable module name for analysis orchestration services.
add_library(famp_analysis INTERFACE)
target_link_libraries(famp_analysis INTERFACE famp_build_settings)
add_library(famp::analysis ALIAS famp_analysis)

add_library(famp_runtime STATIC
    $<TARGET_OBJECTS:famp_core>
    $<TARGET_OBJECTS:famp_domain>
    $<TARGET_OBJECTS:famp_processing>
    $<TARGET_OBJECTS:famp_cloud_io>
    $<TARGET_OBJECTS:famp_filesystem>
    $<TARGET_OBJECTS:famp_persistence>
    $<TARGET_OBJECTS:famp_geospatial>
    $<TARGET_OBJECTS:famp_reporting>
    $<TARGET_OBJECTS:famp_canvas2d>
    $<TARGET_OBJECTS:famp_viewport3d>
    $<TARGET_OBJECTS:famp_dialogs>
    $<TARGET_OBJECTS:famp_shell>
    $<TARGET_OBJECTS:famp_workspace>
    $<TARGET_OBJECTS:famp_entity_tree>
)
target_link_libraries(famp_runtime PUBLIC famp_build_settings)
target_include_directories(famp_runtime PUBLIC ${FAMP_SOURCE_INCLUDE_DIRS})
add_library(famp::runtime ALIAS famp_runtime)

if(WIN32)
    set(FAMP_EXECUTABLE_TYPE WIN32)
else()
    set(FAMP_EXECUTABLE_TYPE)
endif()

add_executable(FAMP ${FAMP_EXECUTABLE_TYPE}
    app/main.cpp
    "${FAMP_GENERATED_DIR}/Version.h"
    "${FAMP_ROOT_DIR}/resources/MainWindow.qrc"
    "${FAMP_ROOT_DIR}/resources/res.qrc"
)
set_target_properties(FAMP PROPERTIES
    AUTOMOC ON
    AUTORCC ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
target_link_libraries(FAMP PRIVATE famp::runtime)

if(MSVC)
    target_compile_definitions(FAMP PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
    target_compile_options(FAMP PRIVATE /utf-8)
endif()

if(WIN32)
    target_sources(FAMP PRIVATE "${FAMP_ROOT_DIR}/resources/images/icon/famp_icon.rc")
    if(TARGET Qt${QT_VERSION_MAJOR}::EntryPoint)
        target_link_libraries(FAMP PRIVATE Qt${QT_VERSION_MAJOR}::EntryPoint)
    elseif(TARGET Qt${QT_VERSION_MAJOR}::WinMain)
        target_link_libraries(FAMP PRIVATE Qt${QT_VERSION_MAJOR}::WinMain)
    endif()
endif()

if(TARGET VTK::CommonCore)
    vtk_module_autoinit(TARGETS FAMP MODULES ${VTK_LIBRARIES})
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
