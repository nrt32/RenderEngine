#pragma once

// test_utils/capture_helpers.hpp — test-consumed GPU capture helpers (T18).
//
// Moves test-consumed readback surface out of critical RE into the peer lib
// test_utils/ (AUDIT_SOURCE_DIRS += test_utils). The render core stays lean:
// production passes (MeshRenderer, SliceRenderer, LinkedListOIT) contain only
// the draw/capture submission; the readback that observes GPU results for
// FR-render gates lives here and is reached only by tests.
//
// Candidates identified by the architecture review (T18 D):
//   - render/linked_list_oit::readCapturedFragmentCount()
//   - render/slice_renderer::captureCrossSection() + TransformFeedback harness
//   - core/read_pixels raw anchor and utils/pixel_reader façade (now
//     test_utils::PixelReader via REContext::readRgba8)
//
// Raw GL stays exclusive to REContext (core/re_context.cpp is the only site
// for readback raw calls; audit gpu_api_ownership / no_production_readback
// now allow core|test_utils — raw stays core, façade in test_utils via
// REContext façade, not raw). Every context-setting GL call still flows
// through T2 REContext — no test helper touches raw GL.
//
// This header provides thin test_utils façades that delegate to the existing
// render/core readback paths via REContext, so the critical RE does not need
// a second raw anchor and tests consume verification only through test_utils.

#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>

#include "data/result.hpp"

namespace re::render {
class LinkedListOIT;
class SliceRenderer;
struct SliceScene;
struct ClipPlane;
} // namespace re::render

namespace re::test_utils {

/// Read the OIT node-allocator counter back from the GPU (test-consumed
/// readback, guardrail no_production_readback). Delegates to the render
/// pipeline's existing readback path which itself delegates to the REContext
/// buffer-read anchor (raw stays in core/re_context.cpp). The render path
/// never reads back; tests call this after end() for FR-render.2 evidence.
///
/// Returns a typed error if no frame has been begun yet or no GL context is
/// current. Thin façade — no raw GL in test_utils.
data::Result<std::uint32_t> readOitCapturedFragmentCount(
    render::LinkedListOIT& pipeline);

/// Capture the on-plane cross-section vertices emitted by the slice geometry
/// shader (test-consumed readback via TransformFeedback). Delegates to
/// SliceRenderer::captureCrossSection which captures via core::TransformFeedback
/// and reads back through REContext's buffer-read anchor (raw stays in
/// core/re_context.cpp). The render path never reads back; the FR-render.4
/// gate calls this to assert every emitted vertex lies on the clip plane.
///
/// Returns a typed error if the capture program fails to build or the capture
/// cannot be issued. Thin façade — no raw GL in test_utils.
data::Result<void> captureSliceCrossSection(
    render::SliceRenderer& renderer, const render::SliceScene& scene,
    const render::ClipPlane& plane, std::vector<glm::vec3>& out);

} // namespace re::test_utils
