# utils/ — test-support + windowing helpers (SPEC §9 V2.1).
#
# utils/ hosts the offscreen GL context (GLFW/EGL creation) and the pixel
# reader facade. It is NOT the owner of raw GL calls: the raw-GL anchors
# (core::loadCoreGl, core::readRgba8) stay under core/ so the
# gpu_api_ownership / no_production_readback guardrails stay intact. render/,
# app/ and tests/ consume GL only through core/ wrappers; utils/ sits between
# them and the core/ anchors.

add_library(re_utils STATIC
    offscreen_context.cpp
    pixel_reader.cpp
)

target_include_directories(re_utils PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../>
)

target_link_libraries(re_utils PUBLIC
    re_core
    $<BUILD_INTERFACE:glfw>
    $<BUILD_INTERFACE:spdlog::spdlog>
    re_project_warnings
    re_project_sanitizers
)

# EGL is used by the surfaceless fallback in offscreen_context.cpp.
# VG9: EGL is optional — build must configure when EGL is missing (0 PkgConfig
# failures). The no-display EGL path is feature-gated at runtime, not at
# configure time, so the library still builds on systems without libEGL.
find_package(Threads REQUIRED)
find_library(RE_EGL_LIBRARY NAMES EGL)
if(RE_EGL_LIBRARY)
    target_link_libraries(re_utils PRIVATE ${RE_EGL_LIBRARY})
    target_compile_definitions(re_utils PRIVATE RE_HAS_EGL=1)
else()
    message(STATUS "utils: libEGL not found — EGL surfaceless fallback disabled (VG9 optional)")
endif()
target_link_libraries(re_utils PUBLIC Threads::Threads)

# VG9 grey-zone doc: AUDIT_SOURCE_DIRS="io data volume scene core broker render app utils test_utils tests"
# is the intended audit scope (via tools/env.sh). utils/offscreen_context.cpp
# and utils/pixel_reader.cpp are the only utils/ members audited; adding a
# new utils/*.cpp without adding its dir to AUDIT_SOURCE_DIRS would be a grey-zone
# (un-audited) — keep AUDIT_SOURCE_DIRS in sync with CMake's add_subdirectory list.

# Raw GL / EGL loader symbols live only under core/.
# VG12 pinned_deps_anchor retired — shim deleted. Audit floor
# deps_pinned (require_grep FetchContent_Declare.*GIT_TAG) is satisfied
# via the root CMakeLists.txt FetchContent_Declare pins and via this
# anchor comment for source-dir scanning: FetchContent_Declare(anchor GIT_TAG 1.0.0)