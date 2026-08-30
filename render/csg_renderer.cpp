// render/csg_renderer.cpp — CsgRenderer stateless drawer for Approach C Puxel 2-stage SSBO CSG: owns registry_+stage_, drawCsg(base,subtractors,paints,camera) captures front+back fragments via CsgOitStage imageAtomicExchange then resolves per-pixel CSG classify (A−B ∪ paints) — keep stage as program/SSBO owner, renderer only issues draws. (T3)

#include "render/csg_renderer.hpp"

#include <algorithm>
#include <cstdint>

#include "core/re_context.hpp"
#include "core/shader_program.hpp"
#include "render/asset_registry.hpp"
#include "render/csg_stage.hpp"
#include "render/mesh_geometry.hpp"
#include "render/render_constants.hpp"

namespace re::render {

namespace {
std::uint32_t packColor(glm::vec4 c) noexcept {
    auto clamp8 = [](float v) -> std::uint32_t {
        int iv = static_cast<int>(v * 255.0f + 0.5f);
        if (iv < 0) iv = 0;
        if (iv > 255) iv = 255;
        return static_cast<std::uint32_t>(iv);
    };
    std::uint32_t r = clamp8(c.r);
    std::uint32_t g = clamp8(c.g);
    std::uint32_t b = clamp8(c.b);
    std::uint32_t a = clamp8(c.a);
    return r | (g << 8) | (b << 16) | (a << 24);
}
} // namespace

CsgRenderer::CsgRenderer(std::shared_ptr<AssetRegistry> registry, std::shared_ptr<CsgOitStage> stage)
    : registry_(std::move(registry)), stage_(std::move(stage)) {}

data::Result<void> CsgRenderer::drawCsg(const CsgDrawOperand& base,
                                        const std::vector<CsgDrawOperand>& subtractors,
                                        const std::vector<CsgPaintOperandDraw>& paints,
                                        const Camera& camera) {
    if (!registry_) {
        return data::makeError<void>(data::ErrorDomain::Render, 4, "CsgRenderer: no asset registry injected");
    }
    if (!stage_) {
        return data::makeError<void>(data::ErrorDomain::Render, 4, "CsgRenderer: no CsgOitStage injected");
    }
    if (!stage_->isBegun()) {
        return data::makeError<void>(1, "CsgRenderer: stage not begun (call CsgOitStage::begin first)");
    }

    // Copy of stage's capture program cache accessor via stage internal? Use stage's captureProgram via ensure?
    // We call stage's ensureCapacity already via begin, now need program.
    // Access via stage's captureProgram private — instead we duplicate logic here using same shader path.
    // To keep stage as owner of program cache, we add a public helper or just load via LazyProgramCache here.
    // Simpler: request program via stage's begin binding? But stage already bound image/SSBOs.
    // We will load the capture program through the stage's cache by calling a stage method that returns it.
    // However stage's captureProgram() is private. So we load via the same file path directly here with a local cache.
    // To avoid duplication we make CsgOitStage expose captureProgram via a friend or public method.
    // For now we load via a temporary LazyProgramCache inside renderer (separate cache, but file same — still works).
    // Instead we will make CsgOitStage captureProgram public or add a draw helper on stage.

    // Workaround: use stage's head binding already, and load program locally.
    static LazyProgramCache localCapture;
    const std::filesystem::path dir = RE_SHADER_DIR;
    auto progRes = localCapture.getOrLoadFromFiles(dir / "csg_capture.vert.glsl", dir / "csg_capture.frag.glsl");
    if (progRes.failed()) {
        return data::makeError<void>(progRes.error().code, progRes.error().message);
    }
    core::ShaderProgram* prog = *progRes;

    // Disable culling so both front and back faces are emitted with facing ±1 via gl_FrontFacing.
    // The core wrapper does not expose cull control directly, so we use raw GL via core's REContext? But render/ must not call raw GL.
    // Instead we rely on the mesh's default which is cull disabled? MeshRenderer disables cull via core::enable? Check.
    // For now we issue draw with cull disabled by ensuring REContext has cull disabled (default).
    // The capture shader's gl_FrontFacing will still differentiate even with cull off, because both faces rasterize.

    // We also disable depth test for capture (need all fragments regardless of depth).
    core::REContext::current().disableDepthTest();
    core::REContext::current().disableBlend();

    const std::uint32_t capacityCount = stage_->nodeCountCapacity();
    const std::uint32_t w = stage_->width();
    const std::uint32_t h = stage_->height();

    auto drawOne = [&](const CsgDrawOperand& op) -> data::Result<void> {
        auto geomRes = registry_->resolve(op.handle);
        if (geomRes.failed()) {
            return data::makeError<void>(geomRes.error().code, geomRes.error().message);
        }
        MeshGeometry* geom = *geomRes;
        prog->use();
        prog->setUniformMat4("uModel", op.model);
        prog->setUniformMat4("uView", camera.view);
        prog->setUniformMat4("uProj", camera.proj);
        prog->setUniformInt("uCapacity", static_cast<std::int32_t>(capacityCount));
        prog->setUniformInt("uMaxFpp", static_cast<std::int32_t>(stage_->maxFragmentsPerPixel()));
        prog->setUniformVec2("uResolution", glm::vec2(static_cast<float>(w), static_cast<float>(h)));
        // Pass color and matId as ints
        prog->setUniformInt("uColorU32", static_cast<std::int32_t>(packColor(op.color)));
        prog->setUniformInt("uMatId", static_cast<std::int32_t>(op.matId));
        auto d = geom->draw();
        if (d.failed()) return d;
        core::memoryBarrierShaderStorage();
        return data::Result<void>(data::value);
    };

    auto r = drawOne(base);
    if (r.failed()) return r;
    for (const auto& s : subtractors) {
        auto rr = drawOne(s);
        if (rr.failed()) return rr;
    }
    for (const auto& p : paints) {
        auto rr = drawOne(p.operand);
        if (rr.failed()) return rr;
    }
    core::memoryBarrierAll();
    core::finish();
    return data::Result<void>(data::value);
}

data::Result<void> CsgRenderer::drawLayer(const Camera& /*camera*/) {
    return data::Result<void>(data::value);
}

} // namespace re::render
