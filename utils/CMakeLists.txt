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
    glfw
    spdlog::spdlog
    re_project_warnings
    re_project_sanitizers
)

# EGL is used by the surfaceless fallback in offscreen_context.cpp.
find_package(Threads REQUIRED)
find_library(RE_EGL_LIBRARY NAMES EGL REQUIRED)
target_link_libraries(re_utils PUBLIC Threads::Threads)
target_link_libraries(re_utils PRIVATE ${RE_EGL_LIBRARY})

# Raw GL / EGL loader symbols live only under core/.