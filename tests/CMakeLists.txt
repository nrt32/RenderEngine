# tests/ — headless unit tests (SPEC S3, S5). Built with ASan+UBSan.

set(RE_TEST_TARGET re_tests)

add_executable(${RE_TEST_TARGET}
    offscreen_fixture.cpp
    t1_scaffolding_test.cpp
    t1_v2_ir_dispatch_test.cpp
    t2_assets_test.cpp
    t3_core_gl_test.cpp
    t4_io_data_test.cpp
    t5_volume_test.cpp
    t6_volume_math_test.cpp
    t7_render_mesh_test.cpp
    t8_render_plane_test.cpp
    t9_render_volume_test.cpp
    t10_oit_test.cpp
    t11_render_slice_test.cpp
    t12_samples_test.cpp
    t14_mpr_test.cpp
    t15_mpr_test.cpp
)

target_link_libraries(${RE_TEST_TARGET}
    PRIVATE
        re_core
        re_data
        re_io
        re_volume
        re_render
        re_app
        re_project_warnings
        GTest::gtest
        GTest::gtest_main
)

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

# Sanitizers on the test binary: ASan + UBSan (SPEC S5, "Memory/sanitizers").
target_compile_options(${RE_TEST_TARGET} PRIVATE
    -fsanitize=address,undefined
    -fno-omit-frame-pointer
)
target_link_options(${RE_TEST_TARGET} PRIVATE
    -fsanitize=address,undefined
)

# RenderEngine sources are built with warnings-as-errors via re_project_warnings
# (linked above); keep the test binary clean under -Werror.

# The offscreen fixture registers the shared GL context as a GTest environment.
# Linking GTest::gtest_main provides the main() that instantiates it via
# offscreen_fixture.cpp's registration.

add_test(NAME ${RE_TEST_TARGET} COMMAND ${RE_TEST_TARGET})

# Headless GL test environment (SPEC S2, S5):
#  - GALLIUM_DRIVER=llvmpipe: software rasterizer — deterministic and
#    leak-clean under ASan (avoids closed D3D12/NVIDIA driver allocations that
#    LSan cannot attribute to a named module).
#  - MESA_GL_VERSION_OVERRIDE=4.6: force the reported context version to the
#    SPEC S2 target. The WSL D3D12 gallium driver exposes GL 4.6 core natively
#    (verified via glxinfo -l); llvmpipe caps at 4.5, so the override reports
#    the project's target 4.6. The version/profile probe happens inside core/
#    and is surfaced through the core::OffscreenContext wrapper.
set_tests_properties(${RE_TEST_TARGET} PROPERTIES
    ENVIRONMENT
        "GALLIUM_DRIVER=llvmpipe;MESA_GL_VERSION_OVERRIDE=4.6;ASAN_OPTIONS=detect_leaks=1"
)
