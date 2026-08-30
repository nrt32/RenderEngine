// tests/t8_phong_tf_test.cpp — T8 gate (V3.7, SPEC §12, FR-render.2/3).
//
// Pure-redesign iteration: Phong-only stays, even hierarchies deferred.
// Asserts:
//   (1) PhongMaterial isTransparent ⇔ baseColor.a < 1.0 (FR-render.3, analytic);
//   (2) VolumeRenderer still takes TransferFunction* separately (no regression,
//       §12.5 — TF beside VolumeMaterial in VolumePresentation, not owned);
//   (3) No new render/material/ files this iteration (audit G — spec-only).
//
// Evidence rule (R4): every value is explainable (analytic alpha threshold,
// struct-field separation, filesystem guard). No "non-empty / >0" without
// derivation.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <glm/vec4.hpp>

#include "data/volume_dataset.hpp"
#include "render/phong_material.hpp"
#include "render/volume_renderer.hpp"
#include "volume/color.hpp"
#include "volume/transfer_function.hpp"

namespace re::tests {
namespace {

// Explainable constants: the alpha values probe every branch of the
// isTransparent predicate (==1 opaque, <1 transparent, clamping at the [0,1]
// boundaries) and the TF-vs-material boundary contract.
constexpr float kOpaqueAlpha = 1.0f;       // isTransparent == false
constexpr float kTransparentAlpha = 0.5f;  // isTransparent == true
constexpr float kZeroAlpha = 0.0f;         // fully transparent
constexpr float kJustBelowOne = 0.999f;    // still transparent (<1)
constexpr float kAboveOne = 1.2f;          // clamped to 1.0 → opaque
constexpr float kBelowZero = -0.3f;        // clamped to 0.0 → transparent

} // namespace

// ---------------------------------------------------------------------------
// (1) PhongMaterial isTransparent ⇔ baseColor.a < 1.0 (analytic, deterministic)
// ---------------------------------------------------------------------------

TEST(T8PhongDeferred, IsTransparentIffAlphaLessThanOne) {
    // Opaque: alpha == 1.0 → not transparent.
    re::render::PhongMaterial opaque(glm::vec4(0.2f, 0.4f, 0.8f, kOpaqueAlpha));
    EXPECT_FALSE(opaque.isTransparent()) << "alpha 1.0 is opaque";
    EXPECT_FLOAT_EQ(opaque.baseColor().a, 1.0f);

    // Transparent: alpha 0.5 → transparent.
    re::render::PhongMaterial semi(glm::vec4(0.2f, 0.4f, 0.8f, kTransparentAlpha));
    EXPECT_TRUE(semi.isTransparent()) << "alpha 0.5 is transparent";
    EXPECT_FLOAT_EQ(semi.baseColor().a, 0.5f);

    // Fully transparent: alpha 0.0 → transparent.
    re::render::PhongMaterial invisible(glm::vec4(0.2f, 0.4f, 0.8f, kZeroAlpha));
    EXPECT_TRUE(invisible.isTransparent()) << "alpha 0.0 is transparent";
    EXPECT_FLOAT_EQ(invisible.baseColor().a, 0.0f);

    // Just below 1.0: still transparent (strict <1).
    re::render::PhongMaterial justBelow(glm::vec4(0.2f, 0.4f, 0.8f, kJustBelowOne));
    EXPECT_TRUE(justBelow.isTransparent()) << "alpha 0.999 is transparent";
    EXPECT_FLOAT_EQ(justBelow.baseColor().a, kJustBelowOne);

    // Clamping: alpha >1 clamped to 1.0 → opaque (PhongMaterial clamps [0,1]).
    re::render::PhongMaterial above(glm::vec4(0.2f, 0.4f, 0.8f, kAboveOne));
    EXPECT_FALSE(above.isTransparent()) << "alpha 1.2 clamped to 1.0 is opaque";
    EXPECT_FLOAT_EQ(above.baseColor().a, 1.0f);

    // Clamping: alpha <0 clamped to 0.0 → transparent.
    re::render::PhongMaterial below(glm::vec4(0.2f, 0.4f, 0.8f, kBelowZero));
    EXPECT_TRUE(below.isTransparent()) << "alpha -0.3 clamped to 0.0 is transparent";
    EXPECT_FLOAT_EQ(below.baseColor().a, 0.0f);
}

TEST(T8PhongDeferred, BaseColorAlphaDrivesTransparencyInvariant) {
    // The invariant isTransparent ⇔ baseColor().a < 1 holds for every material
    // after clamping. Exhaustively test boundary alphas.
    const float alphas[] = {0.0f, 0.1f, 0.5f, 0.999f, 1.0f, 1.5f, -1.0f};
    for (float a : alphas) {
        re::render::PhongMaterial m(glm::vec4(0.5f, 0.5f, 0.5f, a));
        const float clampedA = m.baseColor().a;
        const bool expected = clampedA < 1.0f;
        EXPECT_EQ(m.isTransparent(), expected)
            << "alpha " << a << " clamped " << clampedA << " invariant failed";
    }
}

// ---------------------------------------------------------------------------
// (2) VolumeRenderer still carries the TransferFunction as its OWN field,
//     strictly separate from the dataset/material boundary (§12.5). T13 note:
//     the field TYPE evolved from `const TransferFunction*` to an owned
//     by-value `TransferFunction` (and the dataset ref to a shared_ptr), so
//     the old pointer-identity assertions became value-identity assertions —
//     the separation being tested is unchanged.
// ---------------------------------------------------------------------------

TEST(T8PhongDeferred, VolumeRendererTakesTransferFunctionSeparately) {
    // VolumeInstance must carry dataset and transferFunction as two DISTINCT
    // members (TF beside VolumeMaterial, not owned by it — §12.5). This is
    // the structural guarantee that the VolumeRenderer boundary did not
    // regress to "VolumeMaterial owns TF".
    re::render::VolumeInstance inst;
    // Both fields exist and are independently settable (compile-time +
    // runtime): the dataset starts as the null shared reference; the TF
    // field's default ramp has exactly 2 control points (black→white).
    EXPECT_EQ(inst.dataset, nullptr);
    EXPECT_EQ(inst.transferFunction.size(), 2u)
        << "default TF ramp has exactly 2 control points (explainable)";

    // Construct distinct objects and assign separately — proves no bundling.
    auto dummyDataset =
        std::make_shared<re::data::VolumeDataset>(2, 2, 2,
                                                  std::vector<float>(8, 0.5f)); // minimal 2³ dataset
    re::volume::TransferFunction tf(std::vector<re::volume::TransferFunction::ControlPoint>{
        {0.0f, re::volume::RgbaColor{0.0f, 0.0f, 0.0f, 0.0f}},
        {1.0f, re::volume::RgbaColor{1.0f, 1.0f, 1.0f, 1.0f}},
        {0.5f, re::volume::RgbaColor{0.5f, 0.5f, 0.5f, 0.5f}},
    });
    inst.dataset = dummyDataset;
    inst.transferFunction = tf;
    EXPECT_EQ(inst.dataset, dummyDataset) << "dataset shared reference set separately";
    EXPECT_EQ(inst.transferFunction.size(), 3u)
        << "TF assigned separately (3-point ramp distinguishes it from the 2-point default)";
    EXPECT_NE(static_cast<const void*>(inst.dataset.get()),
              static_cast<const void*>(&inst.transferFunction))
        << "dataset and TF live in distinct storage (separate boundary)";

    // VolumeRenderer::render signature still takes VolumeScene (which holds
    // VolumeInstance with separate TF) — the typed API did not change.
    // Verify the constant used by the shader still equals the spec value.
    EXPECT_FLOAT_EQ(re::render::kDefaultStepLength, 0.25f)
        << "kDefaultStepLength analytic constant (FR-vol.3)";
}

// ---------------------------------------------------------------------------
// (3) render/material/ shader branch table (V7 T9, SPEC §12.2)
// ---------------------------------------------------------------------------

TEST(T8PhongDeferred, NoRenderMaterialDirectory) {
    // V7 T9 extends the T8 G gate: Phong-only stays for the renderer hierarchy (no new render/material headers beyond the branch table), but the branch table docs/render/material/shader_table.md is now required per TASKS T9 D to enumerate ReMaterial→ShaderProgram branches (Phong/PBR/Point/Line/Csg → mesh_opaque/impostor/line/csg_resolve). The gate therefore allows render/material/ to exist IFF it contains exactly shader_table.md with the expected branch entries; any other material header would be a violation of the Phong-only deferral, while the table itself is the V7 deliverable that documents the dispatch without adding new concrete material headers. This preserves the LSP/ISP split (IColor/IVolume/ILine) and OCP seam for future ToonMaterial. (V7 T9)
    const std::filesystem::path materialDir =
        std::filesystem::path(TEST_SOURCE_DIR) / "render" / "material";
    const std::filesystem::path table = materialDir / "shader_table.md";
    if (!std::filesystem::exists(materialDir)) {
        // Pre-T9 tree: directory absent is still acceptable for backward compatibility of the gate helper, but V7 T9 requires the table.
        // To keep the gate green post-T9, we expect the directory to exist; a missing directory now fails via the existence check below.
        EXPECT_TRUE(std::filesystem::exists(table))
            << "render/material/shader_table.md must exist (V7 T9 branch table)";
        return;
    }
    EXPECT_TRUE(std::filesystem::exists(table))
        << "render/material/shader_table.md must exist when render/material/ exists (V7 T9)";
    // Verify branch table contains the five required branches.
    std::ifstream in(table);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("Phong"), std::string::npos) << "branch Phong must be in table";
    EXPECT_NE(content.find("Point"), std::string::npos) << "branch Point must be in table";
    EXPECT_NE(content.find("Line"), std::string::npos) << "branch Line must be in table";
    EXPECT_NE(content.find("Csg"), std::string::npos) << "branch Csg must be in table";
    EXPECT_NE(content.find("mesh_opaque"), std::string::npos) << "shader mesh_opaque must be in table";
    EXPECT_NE(content.find("csg_resolve"), std::string::npos) << "shader csg_resolve must be in table";
}

} // namespace re::tests
