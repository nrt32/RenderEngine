// render/plane_renderer.cpp — PlaneRenderer implementation (SPEC §3,
// FR-render.5).

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

namespace re::render {

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

/// Byte-per-pixel sizes of the image channel counts this renderer accepts
/// (4 = RGBA, 3 = RGB, 1 = grayscale). Any other channel count is rejected
/// with a typed error.
constexpr std::int32_t kSupportedChannels[] = {1, 3, 4};

bool supportedChannels(std::int32_t channels) noexcept {
    for (const std::int32_t c : kSupportedChannels) {
        if (c == channels) {
            return true;
        }
    }
    return false;
}

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

std::vector<std::uint8_t> PlaneRenderer::imageToRgba8(
    const data::Image& image) {
    const std::int32_t width = image.width();
    const std::int32_t height = image.height();
    const std::int32_t channels = image.channels();
    std::vector<std::uint8_t> rgba(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u,
        0u);

    // The source image uses a top-left origin (data::Image, stb convention),
    // while core::Texture2D expects row 0 = the BOTTOM scanline (GL
    // convention). Flipping rows here makes the image's top row the quad's
    // top (the v direction of the quad increases upward), so the image's
    // top-left pixel lands at the quad's top-left corner when viewed from the
    // normal's side.
    for (std::int32_t y = 0; y < height; ++y) {
        const std::int32_t glRow = height - 1 - y;
        for (std::int32_t x = 0; x < width; ++x) {
            const std::size_t src = static_cast<std::size_t>(y * width + x) *
                                    static_cast<std::size_t>(channels);
            const std::size_t dst =
                static_cast<std::size_t>(glRow * width + x) * 4u;
            if (channels == 4) {
                rgba[dst + 0u] = image.pixels()[src + 0u];
                rgba[dst + 1u] = image.pixels()[src + 1u];
                rgba[dst + 2u] = image.pixels()[src + 2u];
                rgba[dst + 3u] = image.pixels()[src + 3u];
            } else if (channels == 3) {
                rgba[dst + 0u] = image.pixels()[src + 0u];
                rgba[dst + 1u] = image.pixels()[src + 1u];
                rgba[dst + 2u] = image.pixels()[src + 2u];
                rgba[dst + 3u] = 255u;
            } else { // 1 channel (grayscale)
                const std::uint8_t v = image.pixels()[src];
                rgba[dst + 0u] = v;
                rgba[dst + 1u] = v;
                rgba[dst + 2u] = v;
                rgba[dst + 3u] = 255u;
            }
        }
    }
    return rgba;
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
    const std::vector<std::uint32_t> indices = {0u, 1u, 2u, 0u, 2u, 3u};

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
    quadIndexCount_ = indices.size();
    return data::makeValue<core::VertexArray*>(&*quadVao_);
}

data::Result<void> PlaneRenderer::uploadTexture(const data::Image& image,
                                                core::Texture2D& out) {
    if (!supportedChannels(image.channels())) {
        return data::makeError<void>(
            1, "PlaneRenderer: unsupported image channel count (" +
                   std::to_string(image.channels()) +
                   "); supported: 1 (gray), 3 (RGB), 4 (RGBA)");
    }
    const std::vector<std::uint8_t> rgba =
        imageToRgba8(image); // already flipped to GL bottom-up rows
    out.bind(0u);
    out.upload(static_cast<std::uint32_t>(image.width()),
               static_cast<std::uint32_t>(image.height()), rgba.data());
    out.unbind(0u);
    return data::Result<void>(data::value);
}

data::Result<core::Texture2D*> PlaneRenderer::textureFor(
    const std::shared_ptr<const data::Image>& image) {
    // Weak-observer cache key (T13): the key expires together with its asset,
    // so a destroyed image can never be looked up again and expired entries
    // are pruned here rather than served.
    const WeakAssetKey<data::Image> key = image;
    auto it = textures_.find(key);
    if (it != textures_.end()) {
        return data::makeValue<core::Texture2D*>(&it->second);
    }
    auto texture = core::Texture2D::create();
    if (texture.failed()) {
        return data::makeError<core::Texture2D*>(texture.error().code,
                                                 texture.error().message);
    }
    auto upload = uploadTexture(*image, *texture);
    if (upload.failed()) {
        return data::makeError<core::Texture2D*>(upload.error().code,
                                                 upload.error().message);
    }
    textures_.erase(key); // prune any expired twin that hashed to this key
    auto inserted = textures_.emplace(key, std::move(*texture));
    return data::makeValue<core::Texture2D*>(&inserted.first->second);
}

data::Result<void> PlaneRenderer::render(const PlaneScene& scene,
                                         const Camera& camera,
                                         const RenderTarget& target) {
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

    // Bind the target and prepare draw state. A null framebuffer means the
    // window's on-screen default framebuffer (T12); otherwise bind the
    // offscreen FBO. v1 FBOs are color-only (no depth attachment), so the depth
    // test is left off; blending is off (textures are sampled with alpha and
    // written straight).
    if (target.framebuffer == nullptr) {
        core::bindDefaultFramebuffer();
    } else {
        target.framebuffer->bind();
    }
    core::setViewport(0, 0, static_cast<int>(target.width),
                      static_cast<int>(target.height));
    core::setClearColor(target.clearColor.r, target.clearColor.g,
                        target.clearColor.b, target.clearColor.a);
    core::clearColor();
    core::disableDepthTest();
    core::disableBlend();

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

        auto draw = core::drawElements(*quadVao, quadIndexCount_);
        if (draw.failed()) {
            return draw;
        }
    }
    return data::Result<void>(data::value);
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
    auto programResult = planeProgram();
    if (programResult.failed()) {
        return data::makeError<void>(programResult.error().code, programResult.error().message);
    }
    core::ShaderProgram* program = *programResult;
    auto quadResult = quadGeometry();
    if (quadResult.failed()) {
        return data::makeError<void>(quadResult.error().code, quadResult.error().message);
    }
    core::VertexArray* quadVao = *quadResult;
    program->use();
    program->setUniformMat4("uView", camera.view);
    program->setUniformMat4("uProj", camera.proj);
    program->setUniformInt("uTex", 0);
    for (const PlaneInstance& instance : scene.planes) {
        if (!instance.geometry || !instance.image) {
            continue;
        }
        auto texture = textureFor(instance.image);
        if (texture.failed()) {
            return data::makeError<void>(texture.error().code, texture.error().message);
        }
        core::Texture2D* texPtr = *texture;
        texPtr->bind(0u);
        const glm::vec3 uScale = 0.5f * (instance.geometry->corners[1] - instance.geometry->corners[0]);
        const glm::vec3 vScale = 0.5f * (instance.geometry->corners[3] - instance.geometry->corners[0]);
        const glm::vec3 normal = glm::normalize(glm::cross(uScale, vScale));
        const glm::vec3 translation = instance.geometry->corners[0] + uScale + vScale;
        glm::mat4 basis(1.0f);
        basis[0] = glm::vec4(uScale, 0.0f);
        basis[1] = glm::vec4(vScale, 0.0f);
        basis[2] = glm::vec4(normal, 0.0f);
        basis[3] = glm::vec4(translation, 1.0f);
        const glm::mat4 model = instance.model * basis;
        program->setUniformMat4("uModel", model);
        auto draw = core::drawElements(*quadVao, quadIndexCount_);
        if (draw.failed()) {
            return draw;
        }
    }
    return data::Result<void>(data::value);
}

} // namespace re::render
