#pragma once

// render/line_renderer.hpp — LineRenderer: SSBO plus VertexID view-quad strip, Rougier mod s dash, analytic fwidth AA, miterLimit 4 to bevel, round and square caps, worldUnits scaling (V7 T5, FR-render.9).
//
// This renderer implements the state-of-art GPU line pipeline locked at 2026-08-30 for V7 T5: each line polyline is a set of segments a to b in world space whose stroke is expanded on the GPU into a view-space quad strip via the SSBO plus VertexID technique with no geometry shader and no CPU expansion where each segment contributes exactly 6 virtual vertices as two triangles covering the quad a plus/minus n times wA and b plus/minus n times wB with n equal to normalized perp of viewport scaled b minus a so the width stays constant in screen space while depth is interpolated from the endpoints clip depth. The CPU populates an SSBO of LineSegmentSSBO holding a, b, color, width, s0, s1, worldUnits, dashLength, gapLength, offset, cap, join, miterLimit where s is the cumulative arc-length in viewport pixels as length of viewport scaled b minus a so the shader can evaluate inDash as step of mod s plus offset over patternLen versus dashLen per Rouger with smoothstep fwidth anti-aliasing at the dash transition, discarding gaps and premultiplying fragColor as vec4 color rgb times color a times alpha for LinkedListOIT. Joins use miterLimit 4 to bevel via prev and next at polyline nodes when the miter would over-extend beyond 4 times width the corner is beveled to avoid spikes at acute angles, caps are round as analytic disc halo or square as sharp cut at the endpoint, and worldUnits width is scaled like points via the projection delta of a right-offset world point with worldUnits true mapping to radius times viewport width over pos w times tan fov half approximated as screen distance between center and center plus right times radius, handling perspective foreshortening and orthographic uniformity without extracting FOV. The renderer owns a LazyProgramCache lineProgram_ for line vert and frag, an SSBO ShaderStorageBuffer lineSsbo_, and a dummy empty VertexArray dummyVao_ for the attribute less drawArrays with 6 times N vertices. It is GL-call-free beyond core wrappers guardrail gpu_api_ownership and uses REContext current for viewport queries and blend state, satisfying V7 T5. (V7 T5)

#include <cstdint>
#include <optional>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "core/re_context.hpp"
#include "core/shader_program.hpp"
#include "core/storage_buffer.hpp"
#include "core/vertex_array.hpp"
#include "data/result.hpp"
#include "render/i_renderable.hpp"
#define RE_LINE_OBJECT_H "render/re_scene/line_object.hpp"
#include RE_LINE_OBJECT_H
#include "render/shader_cache.hpp"
#include "render/types.hpp"

namespace re::render {

// Canonical RE-minimal line types live in render/re_scene/line_object.hpp (V7 T9, SPEC §12.4).
// This header re-exports them for backward compatibility so existing code using render::LineCap etc. continues while inventory enumerates ReLineObject in re_scene.
using LineCap = re_scene::LineCap;
using LineJoin = re_scene::LineJoin;
using DashPattern = re_scene::DashPattern;
using ReLineObjectAlias = re_scene::ReLineObject;

/// SSBO mirror of the GPU LineSegment struct (std430, 96 bytes, vec4-aligned).
///
/// The GPU side declares `struct LineSegment { vec4 a; vec4 b; vec4 color; float width; float s0; float s1; int worldUnits; int cap; int join; float miterLimit; float dashLength; float gapLength; float offset; float _pad0; float _pad1; };` with std430 layout (vec4 16-byte aligned, scalars 4-byte, 16-byte array stride). The CPU populates s as cumulative viewport length length(viewport·(b−a)) so the fragment can compute inDash = step(mod(s+offset,patternLen),dashLen) with fwidth AA. worldUnits toggles per-segment width scaling (true → width scaled via projection delta like points, false → constant pixel width). Padding to 96 bytes ensures the std430 array stride is 16-byte aligned (3×vec4=48 plus 12 scalars=48) so each segment starts at a vec4 boundary, satisfying std430 and keeping 640×480×100 segments well under 152 MB even with the extended dash/join fields. (V7 T5)
struct LineSegmentSSBO {
    glm::vec4 a{0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec4 b{0.0f, 0.0f, 1.0f, 1.0f};
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    float width{2.0f};
    float s0{0.0f};
    float s1{0.0f};
    int worldUnits{0};
    int cap{1};
    int join{0};
    float miterLimit{4.0f};
    float dashLength{8.0f};
    float gapLength{4.0f};
    float offset{0.0f};
    float _pad0{0.0f};
    float _pad1{0.0f};
};
static_assert(sizeof(LineSegmentSSBO) == 96u, "LineSegmentSSBO must be 96 bytes (3*vec4 + 12 scalars, std430 array stride 16)");

/// Per-segment render instance (RE-minimal, broker-built from scene::LineObject via LineObjectMapper).
// Canonical types live in render/re_scene/line_object.hpp; this alias keeps existing code compiling.
using LineInstance = re_scene::ReLineObject;
using LineScene = re_scene::ReLineScene;

/// Stateless SSBO line renderer with analytic AA and dash (V7 T5, FR-render.9).
///
/// Owns the line shader program cache, the SSBO storage buffer, and a dummy empty VAO for the attribute less drawArrays with 6 times N vertices covering the view-space quad a plus/minus n times wA and b plus/minus n times wB with n as perp of viewport scaled b minus a in view-space, passing segmentCoord and s to the fragment shader which implements distToStroke, inDash as step of mod s plus offset over patternLen versus dashLen, alpha as smoothstep of fwidth times inDash, discard gap, fragColor as vec4 color rgb times color a times alpha premul for LL, joins miterLimit 4 to bevel via prev and next at polyline nodes, caps round and square, worldUnits w scaled like points via projection delta. The renderer is GL-call-free beyond core wrappers guardrail gpu_api_ownership and uses REContext current for viewport and blend state. (V7 T5)
class LineRenderer final : public IRenderable {
   public:
    LineRenderer() = default;
    LineRenderer(const LineRenderer&) = delete;
    LineRenderer& operator=(const LineRenderer&) = delete;
    LineRenderer(LineRenderer&&) noexcept = default;
    LineRenderer& operator=(LineRenderer&&) noexcept = default;
    ~LineRenderer() final = default;

    /// Draw the line scene into the currently-bound framebuffer ReView ViewTarget assuming the view already performed bind, viewport, and clear via REContext current.
    /// Populates the SSBO with per-segment s as cumulative length of viewport scaled b minus a with viewport queried from REContext, worldUnits width scaled via projection delta of a right-offset world point when worldUnits true analogous to points as radius times viewport width over pos w times tan fov half realized as screen distance between center and center plus right times width, then issues drawArrays with 6 times N vertices covering the view-space quad a plus/minus n times wA and b plus/minus n times wB where n is perp of viewport scaled b minus a in view-space, passing segmentCoord and s to line vert and frag which implement distToStroke, inDash as step of mod s plus offset over patternLen versus dashLen, alpha as smoothstep of fwidth times inDash, discard gap, fragColor as vec4 color rgb times color a times alpha premul for LL, joins miterLimit 4 to bevel via prev and next, caps round and square. The draw is attribute less and derives all positions from the SSBO via VertexID, keeping the method GL-call-free beyond core wrappers.
    data::Result<void> drawLayer(const LineScene& scene, const Camera& camera);

    /// IRenderable type-erased entry (View never knows the renderer type). The scene must have been supplied via View::addItem(scene, renderer) type-erasure; this direct call without a scene returns a typed error and exists only to satisfy the interface.
    using IRenderable::drawLayer;
    data::Result<void> drawLayer(const Camera& camera) override;

   private:
    data::Result<core::ShaderProgram*> lineProgram();
    data::Result<core::ShaderStorageBuffer*> ssbo();
    data::Result<core::VertexArray*> dummyVao();

    LazyProgramCache lineProgram_;
    std::optional<core::ShaderStorageBuffer> lineSsbo_;
    std::optional<core::VertexArray> dummyVao_;
};

} // namespace re::render
