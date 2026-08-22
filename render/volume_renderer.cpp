// render/volume_renderer.cpp — VolumeRenderer implementation (SPEC §3,
// FR-render.6).

#include "render/volume_renderer.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "core/draw.hpp"
#include "volume/color.hpp"

namespace re::render {

namespace {

// Ray-cast shaders, GLSL 450 (SPEC §8: gate/test shaders compile on llvmpipe
// which caps at 4.50).
//
// The vertex shader draws a full-screen quad in NDC and forwards the pixel's
// NDC position to the fragment shader. The fragment shader reconstructs the
// world ray for the pixel by unprojecting the NDC near/far points through
// uViewProj (works for ortho and perspective), intersects it against the
// volume's world AABB (closed-form slab method, FR-vol.3), then steps along
// the segment at center positions (FR-vol.3) sampling the 3D density texture
// with GL_LINEAR (trilinear) and evaluating the piecewise-linear transfer
// function (FR-vol.1), accumulating front-to-back with premultiplied alpha
// (FR-vol.2). The whole pass mirrors the pure volume/ math so the T9 gate can
// check the GPU output against the analytic CPU ray-cast within 1/255.

constexpr char kRayCastVertexShader[] =
    "#version 450 core\n"
    "layout(location = 0) in vec2 aPos;\n"
    "out vec2 vNdc;\n"
    "void main() {\n"
    "    vNdc = aPos;\n"
    "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
    "}\n";

constexpr char kRayCastFragmentShader[] =
    "#version 450 core\n"
    "in vec2 vNdc;\n"
    "uniform mat4 uViewProj;\n"
    "uniform vec3 uBoxMin;\n"
    "uniform vec3 uBoxMax;\n"
    "uniform mat4 uInvModel;\n"
    "uniform vec3 uSize;\n"
    "uniform float uStepLength;\n"
    "uniform int uTfCount;\n"
    "uniform float uTfValues[8];\n"
    "uniform vec4 uTfColors[8];\n"
    "uniform sampler3D uVolume;\n"
    "layout(location = 0) out vec4 oColor;\n"
    "\n"
    "bool intersectRayAabb(vec3 ro, vec3 rd, vec3 bmin, vec3 bmax,\n"
    "                      out float tEntry, out float tExit) {\n"
    "    float tNear = -1e30;\n"
    "    float tFar = 1e30;\n"
    "    for (int axis = 0; axis < 3; ++axis) {\n"
    "        float dir = rd[axis];\n"
    "        float orig = ro[axis];\n"
    "        if (abs(dir) < 1e-7) {\n"
    "            if (orig < bmin[axis] || orig > bmax[axis]) {\n"
    "                return false;\n"
    "            }\n"
    "            continue;\n"
    "        }\n"
    "        float t1 = (bmin[axis] - orig) / dir;\n"
    "        float t2 = (bmax[axis] - orig) / dir;\n"
    "        if (t1 > t2) {\n"
    "            float tmp = t1; t1 = t2; t2 = tmp;\n"
    "        }\n"
    "        tNear = max(tNear, t1);\n"
    "        tFar = min(tFar, t2);\n"
    "        if (tNear > tFar) {\n"
    "            return false;\n"
    "        }\n"
    "    }\n"
    "    if (tFar < 0.0) {\n"
    "        return false;\n"
    "    }\n"
    "    tEntry = max(tNear, 0.0);\n"
    "    tExit = tFar;\n"
    "    return true;\n"
    "}\n"
    "\n"
    "vec4 tfSample(float value) {\n"
    "    if (value <= uTfValues[0]) {\n"
    "        return uTfColors[0];\n"
    "    }\n"
    "    if (value >= uTfValues[uTfCount - 1]) {\n"
    "        return uTfColors[uTfCount - 1];\n"
    "    }\n"
    "    int lo = 0;\n"
    "    int hi = uTfCount - 1;\n"
    "    while (hi - lo > 1) {\n"
    "        int mid = (lo + hi) / 2;\n"
    "        if (uTfValues[mid] <= value) {\n"
    "            lo = mid;\n"
    "        } else {\n"
    "            hi = mid;\n"
    "        }\n"
    "    }\n"
    "    float t = (value - uTfValues[lo]) / (uTfValues[hi] - uTfValues[lo]);\n"
    "    return mix(uTfColors[lo], uTfColors[hi], t);\n"
    "}\n"
    "\n"
    "void main() {\n"
    "    vec4 nearNdc = vec4(vNdc, -1.0, 1.0);\n"
    "    vec4 farNdc = vec4(vNdc, 1.0, 1.0);\n"
    "    vec4 worldNear = inverse(uViewProj) * nearNdc;\n"
    "    worldNear /= worldNear.w;\n"
    "    vec4 worldFar = inverse(uViewProj) * farNdc;\n"
    "    worldFar /= worldFar.w;\n"
    "    vec3 ro = worldNear.xyz;\n"
    "    vec3 rd = normalize(worldFar.xyz - worldNear.xyz);\n"
    "\n"
    "    float tEntry = 0.0;\n"
    "    float tExit = 0.0;\n"
    "    if (!intersectRayAabb(ro, rd, uBoxMin, uBoxMax, tEntry, tExit)) {\n"
    "        oColor = vec4(0.0);\n"
    "        return;\n"
    "    }\n"
    "    float span = tExit - tEntry;\n"
    "    int count = int(floor(span / uStepLength));\n"
    "    if (count < 1) {\n"
    "        oColor = vec4(0.0);\n"
    "        return;\n"
    "    }\n"
    "\n"
    "    vec3 rgb = vec3(0.0);\n"
    "    float alpha = 0.0;\n"
    "    for (int k = 0; k < count; ++k) {\n"
    "        float t = tEntry + (float(k) + 0.5) * uStepLength;\n"
    "        vec3 worldPos = ro + rd * t;\n"
    "        vec3 modelPos = (uInvModel * vec4(worldPos, 1.0)).xyz;\n"
    "        vec3 texCoord = (modelPos * (uSize - vec3(1.0)) + vec3(0.5)) / "
    "uSize;\n"
    "        float density = texture(uVolume, texCoord).r;\n"
    "        vec4 tf = tfSample(density);\n"
    "        float w = (1.0 - alpha) * tf.a;\n"
    "        rgb += w * tf.rgb;\n"
    "        alpha += w;\n"
    "    }\n"
    "    oColor = vec4(rgb, alpha);\n"
    "}\n";

// Maximum transfer-function control points the shader accepts (uniform array
// size in the GLSL above). A volume::TransferFunction with more points is
// rejected with a typed error.
constexpr std::size_t kMaxTfPoints = 8u;

// Full-screen quad vertices in NDC (x,y): two triangles sharing a diagonal.
constexpr std::array<float, 8> kScreenQuadVerts = {
    -1.0f, -1.0f, // corner 0
    1.0f,  -1.0f, // corner 1
    1.0f,  1.0f,  // corner 2
    -1.0f, 1.0f,  // corner 3
};

} // namespace

std::pair<glm::vec3, glm::vec3> VolumeRenderer::worldAabb(
    const VolumeInstance& instance) {
    // The dataset occupies [0,1]^3 in model space; transform the 8 corners by
    // the model matrix and take the axis-aligned bounding box. Exact for
    // axis-aligned scaling/translation (the v1 case); conservative for rotated
    // models.
    glm::vec3 minv(std::numeric_limits<float>::max());
    glm::vec3 maxv(std::numeric_limits<float>::lowest());
    for (std::uint32_t i = 0u; i < 8u; ++i) {
        const glm::vec3 corner{static_cast<float>((i & 1u) ? 1u : 0u),
                               static_cast<float>((i & 2u) ? 1u : 0u),
                               static_cast<float>((i & 4u) ? 1u : 0u)};
        const glm::vec4 world = instance.model * glm::vec4(corner, 1.0f);
        minv = glm::min(minv, glm::vec3(world));
        maxv = glm::max(maxv, glm::vec3(world));
    }
    return {minv, maxv};
}

data::Result<core::ShaderProgram*> VolumeRenderer::rayCastProgram() {
    if (rayCastProgram_.has_value()) {
        return data::makeValue<core::ShaderProgram*>(&*rayCastProgram_);
    }
    auto program = core::ShaderProgram::create(kRayCastVertexShader,
                                               kRayCastFragmentShader);
    if (program.failed()) {
        return data::makeError<core::ShaderProgram*>(program.error().code,
                                                     program.error().message);
    }
    rayCastProgram_ = std::move(*program);
    return data::makeValue<core::ShaderProgram*>(&*rayCastProgram_);
}

data::Result<core::VertexArray*> VolumeRenderer::screenQuad() {
    if (screenQuadVao_.has_value()) {
        return data::makeValue<core::VertexArray*>(&*screenQuadVao_);
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

    constexpr std::array<std::uint32_t, 6> kIndices = {0u, 1u, 2u, 0u, 2u, 3u};
    const std::size_t strideBytes = 2u * sizeof(float);

    // The VAO captures both the GL_ARRAY_BUFFER (via setAttribute) and the
    // GL_ELEMENT_ARRAY_BUFFER (via EBO bind) bindings, so both must be bound
    // while the VAO is bound (mirrors PlaneRenderer::quadGeometry). The EBO
    // must outlive the local scope because the VAO references it by name.
    vao->bind();
    vbo->bind();
    vbo->upload(kScreenQuadVerts.data(),
                kScreenQuadVerts.size() * sizeof(float),
                core::BufferUsage::StaticDraw);
    ebo->bind();
    ebo->upload(kIndices.data(), kIndices.size(),
                core::BufferUsage::StaticDraw);
    // Interleaved position-only layout: attribute 0 = 2 floats.
    vao->setAttribute(0u, 2, /*normalized=*/false, strideBytes, 0u);
    vao->unbind();

    screenQuadVbo_ = std::move(*vbo);
    screenQuadEbo_ = std::move(*ebo);
    screenQuadVao_ = std::move(*vao);
    screenQuadIndexCount_ = kIndices.size();
    return data::makeValue<core::VertexArray*>(&*screenQuadVao_);
}

data::Result<void> VolumeRenderer::uploadTexture(
    const data::VolumeDataset& dataset, core::Texture3D& out) {
    out.bind(0u);
    out.upload(dataset.sizeX(), dataset.sizeY(), dataset.sizeZ(),
               dataset.voxels().data());
    out.unbind(0u);
    return data::Result<void>(data::value);
}

data::Result<core::Texture3D*> VolumeRenderer::textureFor(
    const data::VolumeDataset& dataset) {
    const auto it = textures_.find(&dataset);
    if (it != textures_.end()) {
        return data::makeValue<core::Texture3D*>(&it->second);
    }
    auto texture = core::Texture3D::create();
    if (texture.failed()) {
        return data::makeError<core::Texture3D*>(texture.error().code,
                                                 texture.error().message);
    }
    auto upload = uploadTexture(dataset, *texture);
    if (upload.failed()) {
        return data::makeError<core::Texture3D*>(upload.error().code,
                                                 upload.error().message);
    }
    auto inserted = textures_.emplace(&dataset, std::move(*texture));
    return data::makeValue<core::Texture3D*>(&inserted.first->second);
}

void VolumeRenderer::uploadTransferFunction(
    const volume::TransferFunction& tf) const {
    const std::size_t count = tf.size();
    std::vector<float> values(count);
    std::vector<glm::vec4> colors(count);
    for (std::size_t i = 0u; i < count; ++i) {
        const auto& cp = tf.controlPoints()[i];
        values[i] = cp.value;
        colors[i] = glm::vec4(cp.color.r, cp.color.g, cp.color.b, cp.color.a);
    }
    rayCastProgram_->setUniformInt("uTfCount",
                                   static_cast<std::int32_t>(count));
    rayCastProgram_->setUniformFloatArray("uTfValues", values.data(), count);
    rayCastProgram_->setUniformVec4Array("uTfColors", colors.data(), count);
}

data::Result<void> VolumeRenderer::render(const VolumeScene& scene,
                                          const Camera& camera,
                                          const RenderTarget& target) {
    if (target.width == 0u || target.height == 0u) {
        return data::makeError<void>(1, "VolumeRenderer: invalid target size");
    }

    auto programResult = rayCastProgram();
    if (programResult.failed()) {
        return data::makeError<void>(programResult.error().code,
                                     programResult.error().message);
    }
    core::ShaderProgram* program = *programResult;

    auto quadResult = screenQuad();
    if (quadResult.failed()) {
        return data::makeError<void>(quadResult.error().code,
                                     quadResult.error().message);
    }
    core::VertexArray* quadVao = *quadResult;

    // Bind the target and prepare draw state. A null framebuffer means the
    // window's on-screen default framebuffer (T12); otherwise bind the
    // offscreen FBO. v1 FBOs are color-only (no depth attachment), so the depth
    // test is left off; blending is off because the shader already writes the
    // final premultiplied composited color.
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
    program->setUniformMat4("uViewProj", camera.proj * camera.view);
    program->setUniformInt("uVolume", 0); // sampler reads texture unit 0

    for (const VolumeInstance& instance : scene.volumes) {
        if (instance.dataset == nullptr ||
            instance.transferFunction == nullptr) {
            continue;
        }
        if (instance.transferFunction->size() > kMaxTfPoints) {
            return data::makeError<void>(
                1, "VolumeRenderer: transfer function has more than " +
                       std::to_string(kMaxTfPoints) + " control points");
        }

        auto texture = textureFor(*instance.dataset);
        if (texture.failed()) {
            return data::makeError<void>(texture.error().code,
                                         texture.error().message);
        }
        core::Texture3D* texPtr = *texture;
        texPtr->bind(0u);

        const auto [boxMin, boxMax] = worldAabb(instance);
        const glm::mat4 invModel = glm::inverse(instance.model);
        const glm::vec3 size(static_cast<float>(instance.dataset->sizeX()),
                             static_cast<float>(instance.dataset->sizeY()),
                             static_cast<float>(instance.dataset->sizeZ()));

        program->setUniformVec3("uBoxMin", boxMin);
        program->setUniformVec3("uBoxMax", boxMax);
        program->setUniformMat4("uInvModel", invModel);
        program->setUniformVec3("uSize", size);
        program->setUniformFloat("uStepLength", kDefaultStepLength);
        uploadTransferFunction(*instance.transferFunction);

        auto draw = core::drawElements(*quadVao, screenQuadIndexCount_);
        if (draw.failed()) {
            return draw;
        }
    }
    return data::Result<void>(data::value);
}

data::Result<void> VolumeRenderer::render(const Scene& scene,
                                          const Camera& camera,
                                          const RenderTarget& target) {
    const VolumeScene* const* volumeScene =
        std::get_if<const VolumeScene*>(&scene);
    if (volumeScene == nullptr || *volumeScene == nullptr) {
        // The dispatch contract (SPEC §9 V2.3) rejects a scene of a different
        // technique — or the null "no scene" payload (render/types.hpp) — with
        // a typed error instead of throwing or crashing (SPEC §5).
        return data::makeError<void>(
            2, "VolumeRenderer: scene does not hold a VolumeScene");
    }
    return render(**volumeScene, camera, target);
}

} // namespace re::render
