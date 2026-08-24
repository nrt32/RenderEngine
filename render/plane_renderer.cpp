// render/plane_renderer.cpp — PlaneRenderer implementation: draw an image-
// backed quad (textured plane) with a dedicated shader program. The renderer
// owns the GPU path end-to-end — texture upload from the CPU image, the unit
// quad geometry, and the draw call all live here — so app code never parses
// plane corners or builds vertex buffers by hand; it only supplies the
// PlaneInstance values.

#include "render/plane_renderer.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <glm/glm.hpp>
#include <string>
#include <utility>
#include <vector>

#include "core/draw.hpp"
#include "core/element_buffer.hpp"
#include "core/shader_program.hpp"
#include "core/vertex_array.hpp"
#include "core/vertex_buffer.hpp"
#include "render/screen_quad.hpp"

namespace re::render {

PlaneRenderer::PlaneRenderer(std::shared_ptr<AssetRegistry> assets)
    : assets_(std::move(assets)) {}

// Textured-plane shaders live as .glsl files under render/shaders/
// (SPEC §9 V2.6) and are loaded via core::ShaderProgram's file helpers.
// The plane is drawn as a plain textured quad; fragment samples the texture
// directly so a plane whose UV maps the image exactly onto the viewport
// reproduces the source texels (FR-render.5).

// Interleaved vertex layout: position (3 floats) + UV (2 floats) + normal
// (3 floats) = 8 floats per vertex.
constexpr std::size_t kStrideBytes = 8u * sizeof(float);
constexpr std::size_t kUvOffsetBytes = 3u * sizeof(float);
constexpr std::size_t kNormalOffsetBytes = 5u * sizeof(float);

PlaneGeometry PlaneGeometry::unitQuadXY() {
    PlaneGeometry g;
    g.corners = {glm::vec3(-1.0f, -1.0f, 0.0f), glm::vec3(1.0f, -1.0f, 0.0f),
                 glm::vec3(1.0f, 1.0f, 0.0f), glm::vec3(-1.0f, 1.0f, 0.0f)};
    g.uv = {glm::vec2(0.0f, 0.0f), glm::vec2(1.0f, 0.0f), glm::vec2(1.0f, 1.0f),
            glm::vec2(0.0f, 1.0f)};
    // Analytic normal: normalized cross(c1 - c0, c3 - c0) = (0,0,1).
    g.normal = glm::normalize(
        glm::cross(g.corners[1] - g.corners[0], g.corners[3] - g.corners[0]));
    return g;
}

data::Result<core::ShaderProgram*> PlaneRenderer::planeProgram() {
    if (planeProgram_.has_value()) {
        return data::makeValue<core::ShaderProgram*>(&*planeProgram_);
    }
    const std::filesystem::path dir = RE_SHADER_DIR;
    auto program = core::ShaderProgram::createFromFiles(
        dir / "plane.vert.glsl", dir / "plane.frag.glsl");
    if (program.failed()) {
        return data::makeError<core::ShaderProgram*>(program.error().code,
                                                     program.error().message);
    }
    planeProgram_ = std::move(*program);
    return data::makeValue<core::ShaderProgram*>(&*planeProgram_);
}

data::Result<core::VertexArray*> PlaneRenderer::quadGeometry() {
    if (quadVao_.has_value()) {
        return data::makeValue<core::VertexArray*>(&*quadVao_);
    }
    auto vao = core::VertexArray::create();
    if (vao.failed()) {
        return data::makeError<core::VertexArray*>(vao.error().code,
                                                   vao.error().message);
    }
    auto vbo = core::VertexBuffer::create();
    if (vbo.failed()) {
        return data::makeError<core::VertexArray*>(vbo.error().code,
                                                   vbo.error().message);
    }
    auto ebo = core::ElementBuffer::create();
    if (ebo.failed()) {
        return data::makeError<core::VertexArray*>(ebo.error().code,
                                                   ebo.error().message);
    }

    // Interleaved unit-quad vertices: position + UV + normal, indexed by
    // corner order (corner0..corner3) matching PlaneGeometry::unitQuadXY.
    // This is NOT the shared NDC ScreenQuad: the plane shader samples per-
    // vertex UVs and needs the normal attribute, so the interleaved 8-float
    // layout stays here — only the two-triangle index pattern comes from the
    // one shared definition kQuadTriangleIndices (render/screen_quad.hpp).
    const PlaneGeometry unit = PlaneGeometry::unitQuadXY();
    const std::vector<float> verts = {
        unit.corners[0].x, unit.corners[0].y, unit.corners[0].z, unit.uv[0].x,
        unit.uv[0].y,      unit.normal.x,     unit.normal.y,     unit.normal.z,
        unit.corners[1].x, unit.corners[1].y, unit.corners[1].z, unit.uv[1].x,
        unit.uv[1].y,      unit.normal.x,     unit.normal.y,     unit.normal.z,
        unit.corners[2].x, unit.corners[2].y, unit.corners[2].z, unit.uv[2].x,
        unit.uv[2].y,      unit.normal.x,     unit.normal.y,     unit.normal.z,
        unit.corners[3].x, unit.corners[3].y, unit.corners[3].z, unit.uv[3].x,
        unit.uv[3].y,      unit.normal.x,     unit.normal.y,     unit.normal.z,
    };
    const std::vector<std::uint32_t> indices(kQuadTriangleIndices.begin(),
                                             kQuadTriangleIndices.end());

    vao->bind();
    vbo->bind();
    vbo->upload(verts.data(), verts.size() * sizeof(float),
                core::BufferUsage::StaticDraw);
    ebo->bind();
    ebo->upload(indices.data(), indices.size(), core::BufferUsage::StaticDraw);
    vao->setAttribute(0u, 3, /*normalized=*/false, kStrideBytes, 0u);
    vao->setAttribute(1u, 2, /*normalized=*/false, kStrideBytes,
                      kUvOffsetBytes);
    vao->setAttribute(2u, 3, /*normalized=*/false, kStrideBytes,
                      kNormalOffsetBytes);
    vao->unbind();

    // Keep the EBO alive for the lifetime of the renderer: the VAO captures
    // the GL_ELEMENT_ARRAY_BUFFER binding by name, so deleting the EBO (as the
    // local would on scope exit) would free the index buffer the shared quad
    // draws from. Mirrors MeshGeometry's ebo_ ownership.
    quadVbo_ = std::move(*vbo);
    quadEbo_ = std::move(*ebo);
    quadVao_ = std::move(*vao);
    return data::makeValue<core::VertexArray*>(&*quadVao_);
}

data::Result<core::Texture2D*> PlaneRenderer::textureFor(
    const std::shared_ptr<const data::Image>& image) {
    // The shared asset store dedups by content hash (T14): identical pixel
    // content — even through a second renderer instance or a distinct
    // allocation — resolves to ONE store-owned GL texture. The lazy lookup
    // never changes reference counts; owners manage explicit lifetimes via
    // registerImage/unregisterImage. Unsupported channel counts surface as a
    // typed error from the store's upload.
    return assets_->lookupImage(image);
}

data::Result<void> PlaneRenderer::drawInstances(const PlaneScene& scene,
                                                const Camera& camera,
                                                core::ShaderProgram* program,
                                                core::VertexArray* quadVao) {
    program->use();
    program->setUniformMat4("uView", camera.view);
    program->setUniformMat4("uProj", camera.proj);
    program->setUniformInt("uTex", 0); // sampler reads texture unit 0

    for (const PlaneInstance& instance : scene.planes) {
        if (!instance.geometry || !instance.image) {
            continue;
        }
        auto texture = textureFor(instance.image);
        if (texture.failed()) {
            return data::makeError<void>(texture.error().code,
                                         texture.error().message);
        }
        core::Texture2D* texPtr = *texture;
        texPtr->bind(0u);

        // Map the shared unit quad (XY square [-1,1]^2 at z=0) onto the
        // instance's corner box, then apply the instance's model transform.
        //
        // The shared quad's local corners are L0=(-1,-1,0), L1=(1,-1,0),
        // L2=(1,1,0), L3=(-1,1,0). The affine map world = M*local + t maps
        // them onto the geometry's corners, where M's columns are
        //   uScale = (corner1 - corner0)/2   (M * e_x)
        //   vScale = (corner3 - corner0)/2   (M * e_y)
        //   normal = normalize(cross(uScale, vScale))   (M * e_z, so the
        //            plane's z=0 stays in the plane)
        // and t = corner0 + uScale + vScale (so M*L0 + t = corner0 exactly).
        // The full model matrix is instance.model * [uScale|vScale|normal|t]
        // (a single mat4, glm column-major).
        const glm::vec3 uScale = 0.5f * (instance.geometry->corners[1] -
                                         instance.geometry->corners[0]);
        const glm::vec3 vScale = 0.5f * (instance.geometry->corners[3] -
                                         instance.geometry->corners[0]);
        const glm::vec3 normal = glm::normalize(glm::cross(uScale, vScale));
        const glm::vec3 translation =
            instance.geometry->corners[0] + uScale + vScale;

        glm::mat4 basis(1.0f);
        basis[0] = glm::vec4(uScale, 0.0f);
        basis[1] = glm::vec4(vScale, 0.0f);
        basis[2] = glm::vec4(normal, 0.0f);
        basis[3] = glm::vec4(translation, 1.0f);
        const glm::mat4 model = instance.model * basis;

        program->setUniformMat4("uModel", model);

        auto draw =
            core::drawElements(*quadVao, kQuadTriangleIndices.size());
        if (draw.failed()) {
            return draw;
        }
    }
    return data::Result<void>(data::value);
}

data::Result<void> PlaneRenderer::render(const PlaneScene& scene,
                                         const Camera& camera,
                                         const RenderTarget& target) {
    if (assets_ == nullptr) {
        // Constructed with a null store (member-init-order safety): fail with
        // a typed error instead of dereferencing (mirrors MeshRenderer).
        return data::makeError<void>(4, "PlaneRenderer: no shared asset store");
    }
    if (target.width == 0u || target.height == 0u) {
        return data::makeError<void>(1, "PlaneRenderer: invalid target size");
    }

    auto programResult = planeProgram();
    if (programResult.failed()) {
        return data::makeError<void>(programResult.error().code,
                                     programResult.error().message);
    }
    core::ShaderProgram* program = *programResult;

    auto quadResult = quadGeometry();
    if (quadResult.failed()) {
        return data::makeError<void>(quadResult.error().code,
                                     quadResult.error().message);
    }
    core::VertexArray* quadVao = *quadResult;

    // Begin the pass through the ONE shared prologue (bind target → viewport
    // → clear → depth state → blend off). A null framebuffer selects the
    // window's on-screen default framebuffer; otherwise the offscreen FBO is
    // bound. Direct single-scene renders keep the deterministic depth-off
    // painter's-order pass (a target's optional depth attachment is consumed
    // only via the per-view opt-in), so the depth test is left off; blending
    // is off (textures are sampled with alpha and written straight).
    core::DrawContext ctx;
    ctx.beginPass(target.framebuffer, target.width, target.height,
                  target.clearColor.r, target.clearColor.g,
                  target.clearColor.b, target.clearColor.a);

    return drawInstances(scene, camera, program, quadVao);
}

data::Result<void> PlaneRenderer::render(const Scene& scene,
                                          const Camera& camera,
                                          const RenderTarget& target) {
    const PlaneScene* const* planeScene = std::get_if<const PlaneScene*>(&scene);
    if (planeScene == nullptr || *planeScene == nullptr) {
        // The dispatch contract (SPEC §9 V2.3) rejects a scene of a different
        // technique — or the null "no scene" payload (render/types.hpp) — with
        // a typed error instead of throwing or crashing (SPEC §5).
        return data::makeError<void>(
            2, "PlaneRenderer: scene does not hold a PlaneScene");
    }
    return render(**planeScene, camera, target);
}

data::Result<void> PlaneRenderer::drawLayer(const PlaneScene& scene, const Camera& camera,
                                            core::DrawContext& ctx) {
    // ReView already bind+viewport+clear via ctx; does not clear between layers.
    (void)ctx;
    if (assets_ == nullptr) {
        return data::makeError<void>(4, "PlaneRenderer: no shared asset store");
    }
    auto programResult = planeProgram();
    if (programResult.failed()) {
        return data::makeError<void>(programResult.error().code, programResult.error().message);
    }
    auto quadResult = quadGeometry();
    if (quadResult.failed()) {
        return data::makeError<void>(quadResult.error().code, quadResult.error().message);
    }
    return drawInstances(scene, camera, *programResult, *quadResult);
}

} // namespace re::render
