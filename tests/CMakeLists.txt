# tests/ — headless unit tests (SPEC §3, §5). Built with ASan+UBSan.

set(RE_TEST_TARGET re_tests)

add_executable(${RE_TEST_TARGET}
    offscreen_fixture.cpp
    test_helpers.cpp
    t1_scaffolding_test.cpp
    t1_scene_test.cpp
    t1_v2_ir_dispatch_test.cpp
    t2_assets_test.cpp
    t2_v2_multiview_test.cpp
    t3_v2_asset_registry_test.cpp
    t3_core_gl_test.cpp
    t4_io_data_test.cpp
    t4_v2_utils_test.cpp
    t5_v2_platform_backend_test.cpp
    t5_volume_test.cpp
    t6_volume_math_test.cpp
    t6_v2_draw_cache_test.cpp
    t7_render_mesh_test.cpp
    t8_render_plane_test.cpp
    t9_render_volume_test.cpp
    t10_oit_test.cpp
    t11_render_slice_test.cpp
    t12_samples_test.cpp
    t14_mpr_test.cpp
    t15_mpr_test.cpp
    t8_v2_glsl_version_test.cpp
    t2_skeletons_test.cpp
    t3_broker_test.cpp
    t4_camera_test.cpp
    t5_view_test.cpp
    t6_persistence_test.cpp
    t7_asset_identity_test.cpp
    t7_owner_driven_handles_test.cpp
    t8_phong_tf_test.cpp
    t9_re_scene_inventory_test.cpp
    t12_plane_path_test.cpp
    t14_unified_asset_store_test.cpp
    t16_volume_slice_test.cpp
    t16_transfer_function_gate_test.cpp
    t17_renderer_consolidation_test.cpp
    t18_depth_test.cpp
    t19_oit_sample_test.cpp
    broker_path_test.cpp
    t20_lsan_suppressions_test.cpp
    t21_persistence_honesty_test.cpp
    t22_error_model_test.cpp
    t23_resize_test.cpp
    t1_hierarchy_gate_test.cpp
    t2_re_context_test.cpp
    t5_header_firewall_test.cpp
    t12_validation_test.cpp
    t13_nrrd_preprobe_test.cpp
    t15_glfw_runtime_test.cpp
    t19_view_lights_test.cpp
    t1_engine_facade_test.cpp
    t3_harness_decouple_test.cpp
    t4_offscreen_test.cpp
    t5_geometry_collapse_test.cpp
    t7_loader_facade_test.cpp
    t8_layer_ordering_test.cpp
    t9_camera_controller_test.cpp
    t11_volume_caps_test.cpp
    t12_fps_standalone_test.cpp
    t13_minimal_example_test.cpp
    t15_engine_lights_test.cpp
    t16_cached_mapper_base_test.cpp
    t1_store_io_depollution_test.cpp
    t5_dumb_layers_test.cpp
    t6_technique_order_test.cpp
    t7_unified_dedup_test.cpp
    t9_samples_bounded_test.cpp
    t10_long_lived_test.cpp
    t11a_nrrd_hardening_test.cpp
    t11b_obj_image_hardening_test.cpp
    t12_canonical_hash_test.cpp
    t13_offscreen_ownership_test.cpp
    t14a_generation_sync_test.cpp
    t14b_stable_key_tombstone_test.cpp
    t15a_deps_ownership.cpp
    t15b_audit_test.cpp
    t16_build_export_hardening_test.cpp
    t19_builder_fix_test.cpp
    t2_scene_value_types_test.cpp
    t3_csg_stage_test.cpp
    t10_csg_gate_test.cpp
    t11_csg_tree_test.cpp
    t4_point_renderer_test.cpp
    t5_line_renderer_test.cpp
    t8_engine_csg_point_line_test.cpp
)

target_link_libraries(${RE_TEST_TARGET}
    PRIVATE
        re_core
        re_utils
        re_test_utils
        re_data
        re_io
        re_volume
        re_scene
        re_broker
        re_render
        re_app
        re_engine
        re_project_warnings
        re_project_sanitizers
        GTest::gtest
        GTest::gtest_main
)
# T13 serialize test includes <nlohmann/json.hpp> directly; re_scene links it
# PRIVATE, so the test needs its own header-only include. Provide it when the
# fetched target exists (root CMakeLists pins v3.11.3).
if(TARGET nlohmann_json::nlohmann_json)
    target_link_libraries(${RE_TEST_TARGET} PRIVATE nlohmann_json::nlohmann_json)
elseif(TARGET nlohmann_json)
    target_link_libraries(${RE_TEST_TARGET} PRIVATE nlohmann_json)
endif()

# Repo root, so tests can locate the committed assets under data/ (they run
# from the build dir). Pointed at the SOURCE tree, not the build tree.
target_compile_definitions(${RE_TEST_TARGET} PRIVATE
    TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

# T12/T13 gate (FR-app.1): the sample-smoke test spawns the full 5-sample set
# (mesh/plane/volume/slice/OIT) under Xvfb. The binaries are built by app/
# whenever RE_BUILD_TESTS is on; pass their absolute paths + the build dir (for
# the captured sample logs) as compile definitions.
target_compile_definitions(${RE_TEST_TARGET} PRIVATE
    RE_SAMPLE_MESH_BIN="${CMAKE_BINARY_DIR}/app/re_sample_mesh"
    RE_SAMPLE_PLANE_BIN="${CMAKE_BINARY_DIR}/app/re_sample_plane"
    RE_SAMPLE_VOLUME_BIN="${CMAKE_BINARY_DIR}/app/re_sample_volume"
    RE_SAMPLE_SLICE_BIN="${CMAKE_BINARY_DIR}/app/re_sample_slice"
    RE_SAMPLE_OIT_BIN="${CMAKE_BINARY_DIR}/app/re_sample_oit"
    RE_SAMPLE_MPR_BIN="${CMAKE_BINARY_DIR}/app/re_sample_mpr"
    RE_TEST_BIN_DIR="${CMAKE_BINARY_DIR}"
)

# RenderEngine sources are built with warnings-as-errors via re_project_warnings
# (linked above); keep the test binary clean under -Werror.

# The offscreen fixture registers the shared GL context as a GTest environment.
# Linking GTest::gtest_main provides the main() that instantiates it via
# offscreen_fixture.cpp's registration.

add_test(NAME ${RE_TEST_TARGET} COMMAND ${RE_TEST_TARGET})

# T22 death-test binary: gtest death tests fork(), which is slow (~10 s per
# assertion) and documented-unsafe inside re_tests' offscreen GL fixture
# (llvmpipe worker threads). The debug-abort contract on failed Result
# dereference needs only data/result.hpp, so it runs here — no GL fixture,
# single-threaded, deterministic. Same warnings/sanitizer floors as re_tests.
add_executable(re_tests_death
    t22_debug_assert_death_test.cpp
    t12_validation_death_test.cpp
)
target_link_libraries(re_tests_death
    PRIVATE
        re_core
        re_data
        re_project_warnings
        re_project_sanitizers
        GTest::gtest
        GTest::gtest_main
)
target_compile_definitions(re_tests_death PRIVATE
    TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
add_test(NAME re_tests_death COMMAND re_tests_death)
set_tests_properties(re_tests_death PROPERTIES
    ENVIRONMENT "ASAN_OPTIONS=detect_leaks=1"
)

# Headless GL test environment (SPEC §2, §5):
#  - GALLIUM_DRIVER=llvmpipe: software rasterizer — deterministic and
#    leak-clean under ASan (avoids closed D3D12/NVIDIA driver allocations that
#    LSan cannot attribute to a named module).
#  - MESA_GL_VERSION_OVERRIDE=4.6: force the reported context version to the
#    SPEC §2 target. The WSL D3D12 gallium driver exposes GL 4.6 core natively
#    (verified via glxinfo -l); llvmpipe caps at 4.5, so the override reports
#    the project's target 4.6. The version/profile probe happens inside core/
#    (core::loadCoreGl) and is surfaced through the utils::OffscreenContext
#    wrapper (V2.1 moved the context to utils/).
set_tests_properties(${RE_TEST_TARGET} PROPERTIES
    ENVIRONMENT
        "GALLIUM_DRIVER=llvmpipe;MESA_GL_VERSION_OVERRIDE=4.6;ASAN_OPTIONS=detect_leaks=1"
)
