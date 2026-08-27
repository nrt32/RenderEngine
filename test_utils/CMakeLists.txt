# test_utils/ — test-support peer lib (SPEC §6, T18).
#
# Hosts the test-consumed readback façades that keep the critical RE lean:
#   - PixelReader (framebuffer RGBA8 readback via REContext::readRgba8)
#   - capture helpers (OIT fragment-count + slice cross-section via render/core
#     readback paths that themselves delegate to REContext)
#
# Raw GL stays exclusive to core/re_context.cpp (guardrail no_production_readback
# / gpu_api_ownership: raw readback calls only under core/; test_utils façade
# via REContext::readRgba8, not raw — present from start). test_utils never
# touches raw gl* — every context-setting GL call still flows through T2
# REContext. utils/offscreen_context.* stays in utils/ (T15 GlfwRuntime owns
# OffscreenContext lifetime; T18 owns only PixelReader/read_pixels/capture
# helpers).

add_library(re_test_utils STATIC
    pixel_reader.cpp
    capture_helpers.cpp
)

target_include_directories(re_test_utils PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../>
)

target_link_libraries(re_test_utils PUBLIC
    re_core
    re_render
    re_project_warnings
    re_project_sanitizers
)

# test_utils is part of AUDIT_SOURCE_DIRS (via tools/env.sh) — disposition
# and gpu-ownership audits cover it alongside core/utils.
