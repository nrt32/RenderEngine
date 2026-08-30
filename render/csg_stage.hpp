#pragma once

// render/csg_stage.hpp — CsgOitStage: Puxel 2-stage SSBO linked-list for GPU CSG (V7 T3).
//
// This stage implements Approach C from the V7 design (user binding 2026-08-30): CSG via a separate CsgOitStage capture (SSBO linked-list) then resolve (sort+classify) writing surviving sorted fragments into a linear csgResolved SSBO plus per-pixel counts, then final LinkedListOIT::endWithCsg merges csgResolved with the Mesh/Point/Line transparent stream via k-way over(). The design follows Kauker 2013 Puxels and Low 2010 Fragment-Sort adapted to the project's existing LinkedListOIT pattern (render/linked_list_oit.hpp:50 maxFpp clamp [1,16], headTexture R32UI plus nodeBuffer and counterBuffer with ensureCapacity sizing). Closed manifold meshes only are allowed because the Puxel classifier assumes watertight operands; a non-manifold hole would misclassify inside/outside and the stage would produce undefined survivors, so callers must ensure manifold inputs. The stage owns optional<Texture2D> headTexture_ (R32UI head pointers), optional<ShaderStorageBuffer> nodeBuffer_, counterBuffer_, resolvedBuffer_, resolvedCount_ plus LazyProgramCache captureProgram_, resolveProgram_ and an optional<ScreenQuad> for the full-screen resolve pass. ensureCapacity(w,h) sizes storage to w*h*maxFpp with maxFpp default 8 clamped to [1,16] exactly like LinkedListOIT, node stride 16B {uint colorU32; float depth; int facing; uint matId;} padded to 16B so nodeCapacity() equals w*h*maxFpp*16 bytes (640×480×8×16=39321600 37.5 MB, max 640×480×16×16=78643200 75 MB well under the 152 MB reference budget 157286400 for 640×480×16×32, see FR-render.7). The capture shader appends front and back fragments for every operand with both facing values +1 and -1 via gl_FrontFacing, and the resolve shader gathers per-pixel lists, insertion-sorts near->far, classifies flat A∩⋂B' plus paint recolor (subW union, baseW visibility, paintW recolor, Bback facing -1 cap emission carrying B's material, paintInterior bool selecting volume interior versus surface strip, blend override). Survivors are written linear per-pixel sorted into resolvedBuffer with per-pixel counts in resolvedCount, ready for the final k-way merge. The stage also probes core::Caps::ssboAtomics and returns a typed BudgetExceeded error code 8 when the capability is missing, mirroring the LinkedListOIT capability guard and the V7 T3 deliverable requirement. (V7 T3)

#include <cstdint>
#include <optional>
#include <vector>

#include "core/framebuffer.hpp"
#include "core/re_context.hpp"
#include "core/shader_program.hpp"
#include "core/storage_buffer.hpp"
#include "core/texture2d.hpp"
#include "data/result.hpp"
#include "render/render_constants.hpp"
#include "render/screen_quad.hpp"
#include "render/shader_cache.hpp"

namespace re::render {

/// CPU mirror of the GPU CsgNode (16B, std430): packed RGBA8 color, depth, facing, material id.
///
/// The GPU side declares `struct CsgNode { uint colorU32; float depth; int facing; uint matId; };`
/// with std430 layout (4-byte alignment, size 16). The color packs 8-bit RGBA as
/// `R | G<<8 | B<<16 | A<<24` (little-endian) so a shader can unpack with shifts and ` /255.0`.
/// `facing` is +1 for front-facing fragments and -1 for back-facing fragments (derived from
/// `gl_FrontFacing` in the capture shader), and `matId` selects the operand's material.
struct CsgNode {
    std::uint32_t colorU32{0u};
    float depth{0.0f};
    std::int32_t facing{1};
    std::uint32_t matId{0u};
};
static_assert(sizeof(CsgNode) == 16u, "CsgNode must be 16 bytes (colorU32+depth+facing+matId)");

// The per-pixel resolved node is identical to CsgNode (survivors keep the same layout).
using CsgResolvedNode = CsgNode;

inline constexpr std::uint32_t kCsgNodeStrideBytes = 16u;
inline constexpr std::uint32_t kCsgDefaultMaxFpp = 8u;
inline constexpr std::uint32_t kCsgHeadImageUnit = 2u;
inline constexpr std::uint32_t kCsgNodeBinding = 0u;
inline constexpr std::uint32_t kCsgCounterBinding = 1u;
inline constexpr std::uint32_t kCsgResolvedBinding = 3u;
inline constexpr std::uint32_t kCsgResolvedCountBinding = 4u;

/// Puxel 2-stage SSBO stage for GPU CSG (V7 T3, Approach C).
///
/// Owns the SSBO linked-list capture storage (head R32UI texture plus counter and node buffer)
/// and the resolved linear storage (resolvedBuffer plus resolvedCount per-pixel SSBO). The
/// capture program appends fragments with `imageAtomicExchange` on the head texture and an
/// atomic counter for the node buffer, storing both front and back faces with facing ±1.
/// The resolve program gathers each pixel's list, insertion-sorts near->far, classifies with
/// the flat `A∩⋂B' + paint recolor` rule (subW union, baseW visibility, paintW recolor,
/// Bback facing -1 cap emission with B's material, paintInterior bool), and writes survivors
/// linear per-pixel sorted into resolvedBuffer plus counts into resolvedCount. The stage
/// probes `core::caps().ssboAtomics` and surfaces `BudgetExceeded` code 8 when the capability
/// is missing, exactly like the LinkedListOIT capability guard (V7 T3).
class CsgOitStage {
   public:
    explicit CsgOitStage(std::uint32_t maxFragmentsPerPixel = kCsgDefaultMaxFpp);

    CsgOitStage(const CsgOitStage&) = delete;
    CsgOitStage& operator=(const CsgOitStage&) = delete;
    CsgOitStage(CsgOitStage&&) noexcept = default;
    CsgOitStage& operator=(CsgOitStage&&) noexcept = default;
    ~CsgOitStage() = default;

    /// Ensure storage for `width`×`height` pixels (head texture R32UI plus SSBOs). Reallocates
    /// only when dimensions change. Returns a typed error if any GL object cannot be created.
    data::Result<void> ensureCapacity(std::uint32_t width, std::uint32_t height);

    /// Begin a CSG capture pass for a `width`×`height` target. Checks `core::caps().ssboAtomics`
    /// and returns `BudgetExceeded` code 8 when the capability is missing, otherwise clears the
    /// head texture to the null sentinel and the atomic counter to zero, binds the head image
    /// and SSBOs to their fixed binding points, and installs the viewport. The caller then
    /// draws CSG operands via `CsgRenderer::drawCsg` before calling `resolve`.
    data::Result<void> begin(std::uint32_t width, std::uint32_t height, core::REContext& ctx);

    /// Compatibility overload taking a RenderTarget (extracts width/height from the target).
    data::Result<void> begin(const struct RenderTarget& target, core::REContext& ctx);

    /// Resolve the captured fragments: per-pixel gather, insertion-sort near->far, flat
    /// `A∩⋂B' + paint recolor` classification, and linear write of survivors plus counts.
    /// Must be called after `begin` and after all `CsgRenderer::drawCsg` draws, with the
    /// same REContext. Returns a typed error if the resolve program or quad cannot be built.
    data::Result<void> resolve(core::REContext& ctx);

    /// The maximum fragments per pixel this stage captures and sorts (clamped [1,16]).
    std::uint32_t maxFragmentsPerPixel() const noexcept { return maxFpp_; }

    std::uint32_t width() const noexcept { return width_; }
    std::uint32_t height() const noexcept { return height_; }

    /// Node capacity in bytes: `width * height * maxFpp * 16` (16B per node). For
    /// 640×480×8×16 this is 39321600 (37.5 MB) and for max 640×480×16×16 it is
    /// 78643200 (75 MB), both well under the 152 MB reference budget 157286400
    /// for 640×480×16×32. Analytic per FR-render.7. (V7 T3)
    std::uint32_t nodeCapacity() const noexcept {
        return width_ * height_ * maxFpp_ * kCsgNodeStrideBytes;
    }

    /// Node count (fragments) capacity: `width * height * maxFpp`.
    std::uint32_t nodeCountCapacity() const noexcept {
        return width_ * height_ * maxFpp_;
    }

    /// Read back the captured fragment count (global atomic counter). Test-consumed
    /// readback (guardrail no_production_readback): the render path never reads back;
    /// tests call this after `resolve` for FR-render.7 evidence. Returns a typed
    /// error if no frame has been begun yet or no GL context is current.
    data::Result<std::uint32_t> readCapturedCount();

    /// Read back per-pixel resolved counts (one uint per pixel, row-major bottom-up).
    /// Returns a vector of `width*height` counts; each entry is the number of
    /// surviving fragments for that pixel after classification (0 for background,
    /// 1 for hole or base surface in the opaque case). Test façade via REContext.
    data::Result<std::vector<std::uint32_t>> readResolvedCounts() const;

    /// Read back the resolved count for a single pixel (x,y). Convenience for tests
    /// that only need the hole versus outside check.
    data::Result<std::uint32_t> readResolvedCount(std::uint32_t x, std::uint32_t y) const;

    /// Read back the entire resolved buffer (linear per-pixel sorted survivors, each
    /// 16B). The buffer is `width*height*maxFpp` nodes, with per-pixel slots at
    /// `pixelIdx*maxFpp + slot`.
    data::Result<std::vector<CsgResolvedNode>> readResolvedNodes() const;
    data::Result<std::vector<CsgNode>> readCapturedNodes() const;

    /// Read back the depth of the first surviving fragment at pixel (x,y). Returns
    /// a typed error if the pixel has no survivors. Used for the analytic
    /// `resolved depth 1e-6` gate. (V7 T3)
    data::Result<float> readResolvedDepth(std::uint32_t x, std::uint32_t y) const;

    /// Debug readback of the capture FBO color at pixel (x,y) (1/255). The capture
    /// shader writes the operand's packed color to the color attachment for
    /// headless verification; this reads one RGBA8 pixel via REContext.
    data::Result<std::vector<std::uint8_t>> readCapturePixel(std::uint32_t x, std::uint32_t y) const;

    /// Debug readback of the head texture at pixel (x,y) (raw R32UI head index).
    data::Result<std::uint32_t> readHead(std::uint32_t x, std::uint32_t y) const;
    data::Result<std::uint32_t> readHeadCount(std::uint32_t x, std::uint32_t y) const;

    /// Whether a capture pass is currently begun (between begin and resolve).
    bool isBegun() const noexcept { return begun_; }

   private:
    data::Result<core::ShaderProgram*> captureProgram();
    data::Result<core::ShaderProgram*> resolveProgram();
    data::Result<core::VertexArray*> screenQuad();

    std::uint32_t maxFpp_{kCsgDefaultMaxFpp};
    std::uint32_t width_{0u};
    std::uint32_t height_{0u};
    bool begun_{false};

    std::optional<core::Texture2D> headTexture_;
    std::optional<core::Texture2D> headCountTexture_;
    std::optional<core::ShaderStorageBuffer> headBuffer_;
    std::optional<core::ShaderStorageBuffer> headCountBuffer_;
    std::optional<core::ShaderStorageBuffer> nodeBuffer_;
    std::optional<core::ShaderStorageBuffer> counterBuffer_;
    std::optional<core::ShaderStorageBuffer> resolvedBuffer_;
    std::optional<core::ShaderStorageBuffer> resolvedCountBuffer_;

    LazyProgramCache captureProgram_;
    LazyProgramCache resolveProgram_;
    std::optional<ScreenQuad> screenQuad_;
    std::optional<core::Texture2D> resolveColor_;
    std::optional<core::Framebuffer> resolveFbo_;
    std::uint32_t resolveWidth_{0u};
    std::uint32_t resolveHeight_{0u};
    std::optional<core::Texture2D> captureColor_;
    std::optional<core::Framebuffer> captureFbo_;
    std::uint32_t captureWidth_{0u};
    std::uint32_t captureHeight_{0u};
};

} // namespace re::render
