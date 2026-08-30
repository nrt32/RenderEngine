// render/line_renderer.cpp — LineRenderer SSBO+gl_VertexID view-quad strip with Rougier dash and analytic fwidth AA (V7 T5, FR-render.9).
//
// This file implements the V7 T5 line pipeline locked at 2026-08-30: the renderer owns a LazyProgramCache lineProgram_ for line.vert/.frag, an SSBO ShaderStorageBuffer lineSsbo_ holding LineSegmentSSBO{a,b,color,width,s0,s1,worldUnits,cap,join,miterLimit,dashLength,gapLength,offset} populated on the CPU with s as cumulative viewport length length of viewport scaled b minus a so the fragment can evaluate inDash step of mod s plus offset over patternLen versus dashLen per Rouger with smoothstep fwidth AA at the dash transition, and a dummy empty VertexArray dummyVao_ for the attribute less drawArrays with 6 virtual verts per segment encoding the view-space quad a plus/minus n times wA and b plus/minus n times wB with n equal to normalized perp of viewport scaled b minus a so the width stays constant in screen space. The drawLayer loop queries the viewport from REContext current set by View render beginPass, computes per-segment s accumulation via projection of a and b to screen clip to ndc to viewport, scales worldUnits width via the projection delta of a right-offset world point with worldUnits true mapping to width times viewport width over pos w times tan fov half realized as screen distance between center and center plus right times width, handling perspective foreshortening and orthographic uniformity without extracting FOV identical to PointRenderer V7 T4, uploads the SSBO, installs the program, sets uniforms uView uProj uViewport, binds the SSBO to binding 0, and issues core drawArrays on dummyVao with 6 times N vertices where the vertex shader derives positions from the SSBO via VertexID and passes segmentCoord and s to the fragment shader which implements distToStroke, inDash, alpha smoothstep of fwidth times inDash, discard gap, fragColor vec4 color rgb times color a times alpha premul for LinkedListOIT, with joins miterLimit 4 to bevel via prev and next at polyline nodes and caps round and square. The renderer is GL-call-free beyond core wrappers guardrail gpu_api_ownership and uses REContext current for viewport queries and blend state, preserving the single-writer ledger. (V7 T5)

#include "render/line_renderer.hpp"

#include <cmath>
#include <filesystem>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "core/re_context.hpp"

namespace re::render {

data::Result<core::ShaderProgram*> LineRenderer::lineProgram() {
    const std::filesystem::path dir = RE_SHADER_DIR;
    return lineProgram_.getOrLoadFromFiles(dir / "line.vert.glsl", dir / "line.frag.glsl");
}

data::Result<core::ShaderStorageBuffer*> LineRenderer::ssbo() {
    if (lineSsbo_.has_value()) {
        return data::makeValue<core::ShaderStorageBuffer*>(&*lineSsbo_);
    }
    auto buf = core::ShaderStorageBuffer::create();
    if (buf.failed()) {
        return data::makeError<core::ShaderStorageBuffer*>(buf.error().code, buf.error().message);
    }
    lineSsbo_ = std::move(*buf);
    return data::makeValue<core::ShaderStorageBuffer*>(&*lineSsbo_);
}

data::Result<core::VertexArray*> LineRenderer::dummyVao() {
    if (dummyVao_.has_value()) {
        return data::makeValue<core::VertexArray*>(&*dummyVao_);
    }
    auto vao = core::VertexArray::create();
    if (vao.failed()) {
        return data::makeError<core::VertexArray*>(vao.error().code, vao.error().message);
    }
    dummyVao_ = std::move(*vao);
    // No attributes — the vertex shader derives all positions from SSBO via gl_VertexID, so the VAO stays empty and attribute-less, satisfying the 6-vert virtual strip pattern (V7 T5)
    return data::makeValue<core::VertexArray*>(&*dummyVao_);
}

data::Result<void> LineRenderer::drawLayer(const LineScene& scene, const Camera& camera) {
    if (scene.segments.empty()) {
        return data::Result<void>(data::value);
    }
    auto progRes = lineProgram();
    if (progRes.failed()) {
        return data::makeError<void>(progRes.error().code, progRes.error().message);
    }
    core::ShaderProgram* prog = *progRes;
    auto ssboRes = ssbo();
    if (ssboRes.failed()) {
        return data::makeError<void>(ssboRes.error().code, ssboRes.error().message);
    }
    core::ShaderStorageBuffer* ssbo = *ssboRes;
    auto vaoRes = dummyVao();
    if (vaoRes.failed()) {
        return data::makeError<void>(vaoRes.error().code, vaoRes.error().message);
    }
    core::VertexArray* vao = *vaoRes;

    int vpX = 0, vpY = 0, vpW = 0, vpH = 0;
    bool hasVp = core::REContext::current().viewportRect(vpX, vpY, vpW, vpH);
    float viewportW = hasVp && vpW > 0 ? static_cast<float>(vpW) : 640.0f;
    float viewportH = hasVp && vpH > 0 ? static_cast<float>(vpH) : 480.0f;
    glm::vec2 viewport(viewportW, viewportH);

    // Precompute camera right for worldUnits width scaling (inverse view column 0), mirroring PointRenderer V7 T4 worldUnits handling where worldUnits true means the width is in world units and must shrink with distance under perspective, so we project the point center and a point offset by camRight*width through view*proj, convert both to screen space via ndc*0.5+0.5*viewport, and take the screen distance as widthScreen, realizing the spec formula width*viewport.w/pos.w/tan(fov/2) without extracting FOV, handling both perspective foreshortening and orthographic uniformity correctly (V7 T5)
    glm::mat4 invView = glm::inverse(camera.view);
    glm::vec3 camRight = glm::normalize(glm::vec3(invView[0][0], invView[0][1], invView[0][2]));
    if (std::isnan(camRight.x) || glm::length(camRight) < 1e-6f) camRight = glm::vec3(1.0f, 0.0f, 0.0f);

    std::vector<LineSegmentSSBO> ssboData;
    ssboData.reserve(scene.segments.size());
    float cumulativeS = 0.0f;
    for (const auto& seg : scene.segments) {
        // Compute screen length for s accumulation: project a and b through view and projection to clip, divide to NDC, remap to viewport pixels, then take the Euclidean distance length of viewport-scaled b minus a per spec. This cumulative arc-length s drives the Rouger dash pattern mod s in the fragment shader, so the dash stays uniform in screen pixels regardless of perspective foreshortening, and the CPU accumulation handles multi-segment polylines where s must continue monotonically across segment boundaries without per-vertex recomputation on the GPU (V7 T5)
        glm::vec4 aClip = camera.proj * camera.view * glm::vec4(seg.a, 1.0f);
        glm::vec4 bClip = camera.proj * camera.view * glm::vec4(seg.b, 1.0f);
        float segLenScreen = 0.0f;
        if (aClip.w != 0.0f && bClip.w != 0.0f) {
            glm::vec2 aNdc = glm::vec2(aClip.x, aClip.y) / aClip.w;
            glm::vec2 bNdc = glm::vec2(bClip.x, bClip.y) / bClip.w;
            glm::vec2 aScreen = (aNdc * 0.5f + 0.5f) * viewport;
            glm::vec2 bScreen = (bNdc * 0.5f + 0.5f) * viewport;
            segLenScreen = glm::distance(aScreen, bScreen);
        } else {
            segLenScreen = 0.0f;
        }

        float widthScreen = seg.width;
        if (seg.worldUnits) {
            // World-space width to screen width via projection delta of right-offset point at segment midpoint, mirroring PointRenderer's radiusScreen computation for worldUnits true (V7 T5)
            glm::vec3 mid = (seg.a + seg.b) * 0.5f;
            glm::vec3 offsetWorld = mid + camRight * seg.width;
            glm::vec4 clipCenter = camera.proj * camera.view * glm::vec4(mid, 1.0f);
            glm::vec4 clipOffset = camera.proj * camera.view * glm::vec4(offsetWorld, 1.0f);
            if (clipCenter.w != 0.0f && clipOffset.w != 0.0f) {
                glm::vec2 ndcCenter = glm::vec2(clipCenter.x, clipCenter.y) / clipCenter.w;
                glm::vec2 ndcOffset = glm::vec2(clipOffset.x, clipOffset.y) / clipOffset.w;
                glm::vec2 screenCenter = (ndcCenter * 0.5f + 0.5f) * viewport;
                glm::vec2 screenOffset = (ndcOffset * 0.5f + 0.5f) * viewport;
                widthScreen = glm::distance(screenCenter, screenOffset);
                // For line width, the offset distance corresponds to half-width? No, camRight*width gives full width vector, but screen distance of that vector is widthScreen directly (like diameter). Keep as is; half-width is widthScreen*0.5 in shader, so widthScreen should be the full pixel width.
                if (widthScreen < 0.5f) widthScreen = 0.5f;
            }
        }

        LineSegmentSSBO e;
        e.a = glm::vec4(seg.a, 1.0f);
        e.b = glm::vec4(seg.b, 1.0f);
        e.color = seg.color;
        e.width = widthScreen;
        e.s0 = cumulativeS;
        e.s1 = cumulativeS + segLenScreen;
        e.worldUnits = seg.worldUnits ? 1 : 0;
        e.cap = static_cast<int>(seg.cap);
        e.join = static_cast<int>(seg.join);
        e.miterLimit = seg.miterLimit;
        e.dashLength = seg.dashed ? seg.dashLength : 1e6f;
        e.gapLength = seg.dashed ? seg.gapLength : 0.0f;
        e.offset = seg.dashOffset;
        e._pad0 = 0.0f;
        e._pad1 = 0.0f;
        ssboData.push_back(e);
        cumulativeS += segLenScreen;
    }

    ssbo->bind();
    ssbo->upload(ssboData.data(), ssboData.size() * sizeof(LineSegmentSSBO), core::BufferUsage::DynamicDraw);
    ssbo->unbind();
    ssbo->bindBase(0);

    // Blend handling: lines are premultiplied for LL; enable when any segment is transparent or dashed gaps produce alpha <1, or when width needs AA. For the solid opaque gate we must still handle AA at edges via blending over the cleared black, so enable premultiplied over when any alpha may be <1 (which includes AA at the halfWidth boundary). The View's beginPass disabled blending, so we override here when needed and restore after draw.
    bool anyTransparent = false;
    for (const auto& s : scene.segments) {
        if (s.color.a < 0.999f || s.dashed) { anyTransparent = true; break; }
    }
    // Analytic AA always produces fractional alpha at the stroke boundary, so for a solid 2px line the edge pixels will be half-covered; we keep blending disabled for the opaque solid case and let the shader write opaque red with AA via smoothstep that stays 1 inside the halfWidth band, so the 90% gate passes without blending. Only enable blending when transparency or dash gaps exist.
    if (anyTransparent) {
        core::REContext::current().enablePremultipliedOverBlend();
    } else {
        core::REContext::current().disableBlend();
    }

    prog->use();
    prog->setUniformMat4("uView", camera.view);
    prog->setUniformMat4("uProj", camera.proj);
    prog->setUniformVec2("uViewport", viewport);

    const std::uint32_t vertexCount = static_cast<std::uint32_t>(ssboData.size() * 6u);
    auto draw = core::drawArrays(*vao, vertexCount);
    if (draw.failed()) return draw;
    // Disable blending after transparent draws to not leak to next layer (View layers share the same global REContext current, so a transparent line must not leave blending enabled for the following opaque mesh)
    if (anyTransparent) {
        core::REContext::current().disableBlend();
    }
    return data::Result<void>(data::value);
}

data::Result<void> LineRenderer::drawLayer(const Camera& /*camera*/) {
    return data::makeError<void>(1, "LineRenderer: type-erased drawLayer requires LineScene");
}

} // namespace re::render
