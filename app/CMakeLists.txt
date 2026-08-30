# app/ — compositions + samples (SPEC §3). T12 adds the shared sample harness
# (window + ImGui overlay wiring + run loop, FR-app.1) and the mesh/plane/volume
# sample applications driven through it.

# ---------------------------------------------------------------------------
# Dear ImGui (core + GLFW + OpenGL3 backends). This is THIRD-PARTY code fetched
# via FetchContent (SPEC §2, pinned GIT_TAG), so it is NOT built with the
# project's warnings-as-errors (SPEC §5 applies -Werror only to RenderEngine's
# own targets, per CMakeLists.txt). The OpenGL3 backend uses its own
# self-contained imgl3w loader, so no glad wiring is required.
# ---------------------------------------------------------------------------
add_library(re_imgui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/imgui_demo.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp"
)
target_include_directories(re_imgui PUBLIC
    "${imgui_SOURCE_DIR}"
    "${imgui_SOURCE_DIR}/backends"
)
# The GLFW backend needs the GLFW headers and the OpenGL3 backend needs the GL
# loader; glfw is linked through re_core, but expose it here so re_app and the
# samples resolve GLFW symbols regardless of link order. T16 hardening: glfw is
# PRIVATE to re_imgui (not propagated to consumers) — re_app and samples link
# glfw via re_core's PRIVATE BUILD_INTERFACE, so no consumer inherits via
# re_imgui's INTERFACE.
target_link_libraries(re_imgui PRIVATE
    $<BUILD_INTERFACE:glfw>
)

# ---------------------------------------------------------------------------
# re_app — the shared sample harness (T12).
# ---------------------------------------------------------------------------
add_library(re_app STATIC
    sample_harness.cpp
    frame_loop.cpp
    imgui_overlay.cpp
    mpr_slice.cpp
    glfw_camera_interactor.cpp
)

target_include_directories(re_app PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../>
)

# re_app is the shared sample harness + pure CPU scaffolding (data/+volume/
# only — no render/ include anywhere in app/, enforced by the acl_app_render
# audit rule). Samples resolve GL-adjacent needs through re_broker (the only
# scene+render library): the bridge façade, the AppContext composition root,
# and the slice-display camera factories (broker/slice_display.hpp).
target_link_libraries(re_app PUBLIC
    re_core
    re_scene
    re_broker
    $<BUILD_INTERFACE:re_imgui>
    $<BUILD_INTERFACE:spdlog::spdlog>
)
# T16 hardening: warnings/sanitizers are PRIVATE to re_app — consumers of
# re_app (samples, tests) already link re_project_warnings via their own
# PRIVATE linkage, so no consumer inherits -Werror via INTERFACE propagation
# from re_app (prevents -Werror leak to install consumers). re_app itself
# needs glfw headers for glfw_camera_interactor.cpp (GLFW/glfw3.h) — link it
# PRIVATE here since re_imgui no longer propagates glfw via PUBLIC (T16).
target_link_libraries(re_app PRIVATE
    $<BUILD_INTERFACE:glfw>
    re_project_warnings
    re_project_sanitizers
)

# ---------------------------------------------------------------------------
# Sample applications. Built when samples are requested OR when the test suite
# is enabled (RE_BUILD_TESTS): the T12/T13 gates (FR-app.1) run the full
# 5-sample set (mesh/plane/volume/slice/OIT) headlessly under Xvfb, so their
# executables must exist whenever the test suite is configured (a missing
# binary would fail the gate loudly).
# ---------------------------------------------------------------------------
if(RE_BUILD_SAMPLES OR RE_BUILD_TESTS)
    add_executable(re_sample_mesh mesh_sample.cpp)
    add_executable(re_sample_plane plane_sample.cpp)
    add_executable(re_sample_volume volume_sample.cpp)
    add_executable(re_sample_slice slice_sample.cpp)
    add_executable(re_sample_oit oit_sample.cpp)
    add_executable(re_sample_mpr mpr_sample.cpp)
    add_executable(re_sample_csg csg_sample.cpp)
    add_executable(re_sample_point point_sample.cpp)

    foreach(sample_target re_sample_mesh re_sample_plane re_sample_volume
            re_sample_slice re_sample_oit re_sample_mpr re_sample_csg re_sample_point)
        target_link_libraries(${sample_target} PRIVATE
            re_app
            re_broker
            re_io
            re_engine
            glm::glm
            spdlog::spdlog
            re_project_warnings
            re_project_sanitizers
        )
        # All samples drive rendering through the broker façade (AppContext +
        # IViewBridge, SPEC §11 ACL: app never includes render/). MPR also
        # reads pixels for its RE_SAMPLE_DUMP_FRAME diagnostic through
        # utils::PixelReader (core/ readback anchor), so MPR links utils.
        target_link_libraries(re_sample_mpr PRIVATE re_utils)
        # Repo root, so samples locate the committed assets under data/ (SPEC
        # §7). Pointed at the SOURCE tree, not the build tree.
        target_compile_definitions(${sample_target} PRIVATE
            RE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        )
    endforeach()
endif()

# ---------------------------------------------------------------------------
# Long-lived interactive samples (T10) — not for testing, bypass sampleMaxFrames
# via SampleHarness::runInteractive() until shouldClose() with pan+rotate+zoom
# via scene::CameraController + app::GlfwCameraInteractor (left drag rotate
# dx*0.5deg, right drag pan dx*0.01, scroll/middle drag zoom exp(-dy*0.02)).
# Each mirrors its bounded peer but renderFrame calls interactor.update(view)
# before syncRenderPresent, respects WantCaptureMouse, skips orthographic slice
# views, mutates via view.setCamera. Guard: EXCLUDE_FROM_ALL so tools/test.sh
# and ctest never run them (audit: long samples excluded from default build/test).
# Docs note long-lived not for testing.
# ---------------------------------------------------------------------------
foreach(long_target re_sample_mesh_long re_sample_volume_long re_sample_plane_long
        re_sample_slice_long re_sample_oit_long re_sample_mpr_long re_sample_csg_long re_sample_point_long)
    # Map target to source: re_sample_mesh_long -> mesh_long.cpp etc.
    string(REPLACE "re_sample_" "" _stem "${long_target}")
    string(REPLACE "_long" "" _base "${_stem}")
    add_executable(${long_target} EXCLUDE_FROM_ALL ${_base}_long.cpp)
    target_link_libraries(${long_target} PRIVATE
        re_app
        re_broker
        re_io
        re_engine
        glm::glm
        spdlog::spdlog
        re_project_warnings
        re_project_sanitizers
    )
    target_compile_definitions(${long_target} PRIVATE
        RE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )
endforeach()
target_link_libraries(re_sample_mpr_long PRIVATE re_utils)
target_link_libraries(re_sample_csg_long PRIVATE re_utils)
add_custom_target(re_samples_long DEPENDS
    re_sample_mesh_long re_sample_volume_long re_sample_plane_long
    re_sample_slice_long re_sample_oit_long re_sample_mpr_long re_sample_csg_long re_sample_point_long)
