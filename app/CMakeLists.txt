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
# samples resolve GLFW symbols regardless of link order.
target_link_libraries(re_imgui PUBLIC
    glfw
)

# ---------------------------------------------------------------------------
# re_app — the shared sample harness (T12).
# ---------------------------------------------------------------------------
add_library(re_app STATIC
    sample_harness.cpp
)

target_include_directories(re_app PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../>
)

target_link_libraries(re_app PUBLIC
    re_core
    re_imgui
    spdlog::spdlog
    re_project_warnings
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

    foreach(sample_target re_sample_mesh re_sample_plane re_sample_volume
            re_sample_slice re_sample_oit)
        target_link_libraries(${sample_target} PRIVATE
            re_app
            re_render
            re_io
            glm::glm
            spdlog::spdlog
            re_project_warnings
        )
        # Repo root, so samples locate the committed assets under data/ (SPEC
        # §7). Pointed at the SOURCE tree, not the build tree.
        target_compile_definitions(${sample_target} PRIVATE
            RE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        )
        # Sanitizers on the sample binaries, matching the test binaries: the
        # gate (FR-app.1) verifies the samples exit cleanly with no sanitizer
        # reports (SPEC §5).
        target_compile_options(${sample_target} PRIVATE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
        )
        target_link_options(${sample_target} PRIVATE
            -fsanitize=address,undefined
        )
    endforeach()
endif()
