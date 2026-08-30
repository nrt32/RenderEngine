#pragma once

// broker/csg_stage.hpp — CsgStage thin façade over render::CsgOitStage for ViewCompositor (V7 T6, Approach C Puxel).
//
// Thin façade that brokers owns as part of RenderStack: it co-owns the render-side CsgOitStage (shared_ptr) and exposes the stage's Puxel 2-stage SSBO capture→sort→filter→resolved API (head R32UI + counter + node 16B {colorU32,depth,facing,matId} padded 16B, maxFpp [1,16] default 8, nodeCapacity=w*h*maxFpp*16 ≤152 MB analytic 157286400 for 640×480×16×32 reference) plus the ViewCompositor-facing begin/resolve/read* helpers. The façade keeps broker/ free of raw GL (render/ owns GL via core/) and preserves the `gpu_api_ownership forbid_outside core|\bgl` guardrail: broker never calls `gl*` directly, only forwards through the render stage's core wrappers. The compositor's final `LinkedListOIT::endWithCsg` k-way merge `over()` consumes the resolved `csgResolved` sorted SSBO linear per-pixel plus counts produced by this stage's `resolve`. This file is the `broker_per_type` coordinator exemption (not a mapper — `CsgOitStage` coordinator, `ViewBridge` façade pattern) so `broker_per_type`'s `class.*Mapper` count stays 1 per mapper file. (V7 T6)

#include <memory>

#include "core/re_context.hpp"
#include "data/result.hpp"
#include "render/csg_stage.hpp"
#include "render/types.hpp"

namespace re::broker {

/// Thin façade over render::CsgOitStage for ViewCompositor (V7 T6).
///
/// Holds a shared handle to the render-side stage (co-owned with RenderStack and tests) so the stage's lifetime is tied to the composition root and can never dangle mid-frame. All GL stays in render/ via core/ wrappers; broker only forwards calls (preserves `gpu_api_ownership`). The façade is intentionally thin — it does not duplicate ensureCapacity/begin/resolve logic, it delegates to the owned render stage, keeping the single-owner SSBO lifecycle (headTexture R32UI + node/counter/resolved SSBOs, LazyProgramCache, ScreenQuad) in one place (render/csg_stage.hpp). ViewCompositor will call begin→draw via CsgRenderer→resolve per view when the stack carries csgStage (FR-render.7).
class CsgStage {
   public:
    /// Construct over an existing render stage (co-owned). A null stage is accepted at construction but every delegated call validates and returns typed error code 4 instead of dereferencing (so composition order can never silently break initialization).
    explicit CsgStage(std::shared_ptr<render::CsgOitStage> stage) : stage_(std::move(stage)) {}

    CsgStage(const CsgStage&) = delete;
    CsgStage& operator=(const CsgStage&) = delete;
    CsgStage(CsgStage&&) noexcept = default;
    CsgStage& operator=(CsgStage&&) noexcept = default;
    ~CsgStage() = default;

    /// Access the underlying render stage (shared handle, for CsgRenderer wiring).
    const std::shared_ptr<render::CsgOitStage>& stage() const noexcept { return stage_; }
    std::shared_ptr<render::CsgOitStage>& stage() noexcept { return stage_; }

    /// Delegate ensureCapacity to the render stage (typed error on failure, BudgetExceeded code 8 when ssboAtomics missing on begin).
    data::Result<void> ensureCapacity(std::uint32_t w, std::uint32_t h) const {
        if (!stage_) return data::makeError<void>(data::ErrorDomain::Render, 4, "CsgStage: null render stage");
        return stage_->ensureCapacity(w, h);
    }

    data::Result<void> begin(std::uint32_t w, std::uint32_t h, core::REContext& ctx) const {
        if (!stage_) return data::makeError<void>(data::ErrorDomain::Render, 4, "CsgStage: null render stage");
        return stage_->begin(w, h, ctx);
    }

    data::Result<void> begin(const render::RenderTarget& target, core::REContext& ctx) const {
        if (!stage_) return data::makeError<void>(data::ErrorDomain::Render, 4, "CsgStage: null render stage");
        return stage_->begin(target, ctx);
    }

    data::Result<void> resolve(core::REContext& ctx) const {
        if (!stage_) return data::makeError<void>(data::ErrorDomain::Render, 4, "CsgStage: null render stage");
        return stage_->resolve(ctx);
    }

    std::uint32_t maxFragmentsPerPixel() const noexcept { return stage_ ? stage_->maxFragmentsPerPixel() : 0u; }
    std::uint32_t nodeCapacity() const noexcept { return stage_ ? stage_->nodeCapacity() : 0u; }
    std::uint32_t width() const noexcept { return stage_ ? stage_->width() : 0u; }
    std::uint32_t height() const noexcept { return stage_ ? stage_->height() : 0u; }

   private:
    std::shared_ptr<render::CsgOitStage> stage_;
};

} // namespace re::broker
