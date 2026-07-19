cmake_minimum_required(VERSION 3.22)

include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(famp_tests
    test_main.cpp
    test_archaeology_metadata.cpp
    test_archaeology_report.cpp
    test_cloud.cpp
    test_cloud_attributes.cpp
    test_cloud_layer.cpp
    test_cloud_coordinates.cpp
    test_cloud_loader.cpp
    test_additional_cloud_loader.cpp
    test_cloud_display_settings.cpp
    test_cloud_processing.cpp
    test_cloud_projection.cpp
    test_cloud_registration.cpp
    test_cloud_reprojection.cpp
    test_clip_dialog.cpp
    test_control_points.cpp
    test_control_point_dialog.cpp
    test_contour_item.cpp
    test_cut_fill_analysis.cpp
    test_cut_fill_dialog.cpp
    test_cut_fill_io.cpp
    test_cut_fill_workflow.cpp
    test_profile_analysis.cpp
    test_profile_dialog.cpp
    test_profile_io.cpp
    test_terrain_analysis.cpp
    test_terrain_dialog.cpp
    test_terrain_io.cpp
    test_cloud_crop.cpp
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
    test_plane_widget_placement.cpp
    test_plane_clip_commit.cpp
    test_projection_drafting_input.cpp
    test_projection_workflow_ui.cpp
    test_project_document.cpp
    test_processing_recipe.cpp
    test_recent_files.cpp
    test_string_convert.cpp
    test_task_manager.cpp
    test_workspace_store.cpp
    test_workspace_snapshot.cpp
    test_entity_tree_model.cpp
    test_main_window_layout.cpp
    test_vtk_render_manager.cpp
    test_release_acceptance_real_cloud.cpp
)

target_link_libraries(famp_tests
    PRIVATE
        famp::runtime
        GTest::gtest
)

target_compile_definitions(famp_tests
    PRIVATE
        FAMP_SAMPLE_DIR="${FAMP_ROOT_DIR}/samples"
        FAMP_ACCEPTANCE_CLOUD_PATH="${CMAKE_BINARY_DIR}/bin/大墓坑_对齐_sampling_25000.pcd"
)

if(TARGET VTK::CommonCore)
    vtk_module_autoinit(TARGETS famp_tests MODULES ${VTK_LIBRARIES})
endif()

include(GoogleTest)
gtest_discover_tests(
    famp_tests
    # Windows needs to load the deployed Qt/VTK/PCL DLL graph before it can
    # answer --gtest_list_tests. The CMake default of five seconds is too low
    # for a cold hosted runner and turns successful compilation into a build
    # failure during post-build test discovery.
    DISCOVERY_TIMEOUT 60
)
