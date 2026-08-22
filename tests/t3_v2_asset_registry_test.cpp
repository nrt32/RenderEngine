// tests/t3_v2_asset_registry_test.cpp — V2 T3 gate tests (SPEC §9 V2.5).
//
// The V2.5 deliverable: `render::AssetRegistry::registerAsset()` → copyable
// `AssetHandle{index, generation}`; ONE GPU object per individual CPU object
// GLOBALLY (fixing the MeshRenderer+SliceRenderer double-upload of the same
// `data::Mesh`). Scene instances store AssetHandles instead of raw
// `const data::Mesh*` pointers; handles are the currency views exchange. This
// file verifies
//
//   (1) registering the SAME data::Mesh twice (once via the MeshRenderer path,
//       once via the SliceRenderer path) yields ONE GPU object:
//       `AssetRegistry::slotCount() == 1` and both handles resolve to the same
//       GL object id (the geometry's non-zero VAO id);
//   (2) the fix at the renderer level: a MeshScene and a SliceScene sharing one
//       handle of the same CPU mesh render through MeshRenderer AND
//       SliceRenderer with exactly ONE registered GPU object, reproducing the
//       FR-render.1/4 analytic center pixels {51, 102, 204} (regression lock
//       R3: handle-based scenes render unchanged);
//   (3) dangling-handle detection on generation mismatch: a stale
//       {index,generation} lookup returns a typed error (code 2, message
//       "stale handle"), never a crash (SPEC §5); out-of-range indices return a
//       typed error (code 1); unregister() makes the old handle stale (its
//       slot's generation is bumped), and reusing the freed slot for a new
//       mesh issues a NEW generation — so the old handle stays stale while the
//       new one resolves;
//   (4) the dedup is per individual CPU object: two DISTINCT mesh objects with
//       identical content are two GPU objects (slotCount() == 2, distinct VAO
//       ids — GL object names are unique among live objects), and a stale
//       handle inside a rendered scene propagates a typed error out of the
//       renderer instead of crashing.
//
// Explainable constants (SPEC §9 V2.5 gate + R4 evidence rule):
//   - the golden quad `[-1,1]^2` at z=0 (two triangles) is the FR-render.1
//     mesh; under the orthographic camera mapping NDC [-1,1]^2 onto the full
//     64x64 viewport its center pixel (32,32) is the material's base color
//     {0.2, 0.4, 0.8, 1.0} -> bytes {51, 102, 204} (front-facing, shade 1);
//   - the SAME quad sliced by the plane z=0 (normal +Z, point origin) lies
//     entirely ON the plane: every vertex has signed distance 0, so the clip
//     geometry shader keeps all three vertices of each triangle (d >= 0) and
//     emits the full quad; its geometric normal is +Z, so the center pixel is
//     also {51, 102, 204} (FR-render.4);
//   - the registry's typed-error codes (render/asset_registry.cpp): 1 = index
//     out of range, 2 = generation mismatch (stale/dangling handle),
//     3 = freed slot;
//   - generations start at 1 (the default handle {0,0} is the reserved null
//     handle), and a freed slot's generation is bumped immediately and again
//     on reuse — so every outstanding handle to a freed slot is stale.
//
// Per the GL-ownership + readback guardrails this file uses ONLY core/
// wrappers (including utils::PixelReader for pixel readback) — no raw glXxx
// calls.

#include <gtest/gtest.h>

#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <string>
#include <utility>
#include <vector>

#include "core/framebuffer.hpp"
#include "core/gl_error.hpp"
#include "utils/pixel_reader.hpp"
#include "core/texture2d.hpp"
#include "data/mesh.hpp"
#include "render/asset_registry.hpp"
#include "render/mesh_renderer.hpp"
#include "render/phong_material.hpp"
#include "render/slice_renderer.hpp"
#include "tests/offscreen_fixture.hpp"

namespace re::tests {
namespace {

// ---------------------------------------------------------------------------
// Explainable constants (SPEC §9 V2.5 gate).
// ---------------------------------------------------------------------------

// The material's base color: a clean solid color mapping to exact RGBA8 bytes
// (0.2*255=51, 0.4*255=102, 0.8*255=204). Alpha 1.0 => opaque.
constexpr glm::vec4 kBaseColor(0.2f, 0.4f, 0.8f, 1.0f);
constexpr std::uint8_t kExpectedR = 51u;
constexpr std::uint8_t kExpectedG = 102u;
constexpr std::uint8_t kExpectedB = 204u;

// Target framebuffer size: 64x64 (the golden scenes cover the full viewport).
constexpr std::uint32_t kTargetWidth = 64u;
constexpr std::uint32_t kTargetHeight = 64u;
constexpr std::uint32_t kCenterX = kTargetWidth / 2u;  // 32
constexpr std::uint32_t kCenterY = kTargetHeight / 2u; // 32

// The color tolerance: 1/255 per SPEC §4.
constexpr int kColorTolerance = 1;

// The registry's typed-error codes (render/asset_registry.cpp).
constexpr int kOutOfRangeErrorCode = 1;
constexpr int kStaleHandleErrorCode = 2;

// The clip plane slicing the quad: z = 0 (normal +Z through the origin, kept
// side z >= 0). Every quad vertex lies ON the plane (signed distance 0).
constexpr glm::vec3 kPlaneNormal(0.0f, 0.0f, 1.0f);
constexpr glm::vec3 kPlanePoint(0.0f, 0.0f, 0.0f);

// An index far beyond any slot table the registry could grow to in these
// tests: out-of-range lookups must return a typed error, never a crash.
constexpr std::uint32_t kHugeIndex = 4096u;

// ---------------------------------------------------------------------------
// Test helpers.
// ---------------------------------------------------------------------------

/// Build a golden +Z-facing quad mesh covering [-1,1]^2 at z=0 (two triangles).
data::Mesh makeQuadMesh() {
    std::vector<glm::vec3> positions = {
        glm::vec3(-1.0f, -1.0f, 0.0f), // v0
        glm::vec3(1.0f, -1.0f, 0.0f),  // v1
        glm::vec3(1.0f, 1.0f, 0.0f),   // v2
        glm::vec3(-1.0f, 1.0f, 0.0f),  // v3
    };
    std::vector<std::uint32_t> indices = {0u, 1u, 2u, 0u, 2u, 3u};
    return data::Mesh::fromTriangles(std::move(positions), std::move(indices));
}

/// The default camera: eye at (0,0,5) looking down -Z at the origin, with an
/// orthographic projection mapping NDC [-1,1]^2 onto the full viewport.
render::Camera makeCamera() {
    render::Camera camera;
    camera.position = glm::vec3(0.0f, 0.0f, 5.0f);
    camera.view = glm::lookAt(camera.position, glm::vec3(0.0f, 0.0f, 0.0f),
                              glm::vec3(0.0f, 1.0f, 0.0f));
    camera.proj = glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f, 0.1f, 10.0f);
    return camera;
}

/// Build a color-only render target (64x64) bound for readback.
struct RenderedTarget {
    core::Texture2D color;
    core::Framebuffer framebuffer;

    RenderedTarget(core::Texture2D color, core::Framebuffer framebuffer)
        : color(std::move(color)), framebuffer(std::move(framebuffer)) {}
};

RenderedTarget makeTarget(std::uint32_t w, std::uint32_t h) {
    auto color = core::Texture2D::create();
    auto framebuffer = core::Framebuffer::create();
    EXPECT_TRUE(color.ok()) << color.error().message;
    EXPECT_TRUE(framebuffer.ok()) << framebuffer.error().message;
    std::vector<std::uint8_t> zeros(static_cast<std::size_t>(w) * h * 4u, 0u);
    color->bind(0u);
    color->upload(w, h, zeros.data());
    color->unbind(0u);
    framebuffer->bind();
    framebuffer->attachColor(*color);
    EXPECT_TRUE(framebuffer->isComplete());
    framebuffer->unbind();
    return RenderedTarget(std::move(*color), std::move(*framebuffer));
}

/// Read back the single RGBA8 pixel at (x, y) from the still-bound framebuffer
/// (y = 0 is the bottom scanline).
std::vector<std::uint8_t> readPixel(std::uint32_t x, std::uint32_t y) {
    std::vector<std::uint8_t> pixels;
    re::utils::PixelReader reader;
    auto read = reader.read(x, y, 1u, 1u, pixels);
    EXPECT_TRUE(read.ok()) << read.error().message;
    EXPECT_EQ(pixels.size(), 4u);
    return pixels;
}

/// Assert the pixel bytes are the base-color bytes {51, 102, 204} within 1/255.
void expectBaseColor(const std::vector<std::uint8_t>& pixel,
                     const char* where) {
    EXPECT_NEAR(pixel[0], kExpectedR, kColorTolerance) << "R at " << where;
    EXPECT_NEAR(pixel[1], kExpectedG, kColorTolerance) << "G at " << where;
    EXPECT_NEAR(pixel[2], kExpectedB, kColorTolerance) << "B at " << where;
}

} // namespace

// ---------------------------------------------------------------------------
// (1) The registry dedup: registering the same data::Mesh twice yields ONE GPU
//     object — slotCount() == 1 and both handles resolve to the same GL object
//     id (the geometry's VAO).
// ---------------------------------------------------------------------------

TEST(T3V2AssetRegistry, SameMeshRegisteredTwiceIsOneGpuObject) {
    render::AssetRegistry registry;
    data::Mesh mesh = makeQuadMesh();

    // Register the same CPU object twice: "once via MeshRenderer, once via
    // SliceRenderer" — the second call must return the EXISTING handle and
    // must not upload a second GPU object (SPEC §9 V2.5).
    const auto first = registry.registerAsset(mesh);
    ASSERT_TRUE(first.ok()) << first.error().message;
    const auto second = registry.registerAsset(mesh);
    ASSERT_TRUE(second.ok()) << second.error().message;

    EXPECT_EQ(registry.slotCount(), 1u)
        << "one GPU object per individual CPU object, globally";

    // Both handles are the same {index, generation} pair (dedup by identity).
    EXPECT_EQ(*first, *second);

    // Both handles resolve to the same GPU object: the same non-zero VAO id
    // (GL reserves 0 — a valid generated name is non-zero).
    const auto g1 = registry.resolve(*first);
    const auto g2 = registry.resolve(*second);
    ASSERT_TRUE(g1.ok()) << g1.error().message;
    ASSERT_TRUE(g2.ok()) << g2.error().message;
    EXPECT_GT((*g1)->vaoId(), 0u) << "a real GL object name";
    EXPECT_EQ((*g1)->vaoId(), (*g2)->vaoId())
        << "both handles resolve to the same GL object id";

    // The quad still draws through the shared geometry (FR-render.1).
    EXPECT_EQ((*g1)->triangleCount(), 2u) << "the golden quad's 2 triangles";
    EXPECT_FALSE(core::hasPendingGlError());
}

// ---------------------------------------------------------------------------
// (2) Renderer-level fix: the same mesh drawn by MeshRenderer AND
//     SliceRenderer (one shared registry) is one GPU object, and both
//     techniques reproduce their analytic center pixels (regression lock R3).
// ---------------------------------------------------------------------------

TEST(T3V2AssetRegistry, MeshAndSliceRenderersShareOneGpuObject) {
    render::AssetRegistry registry;
    data::Mesh mesh = makeQuadMesh();
    const auto handle = registry.registerAsset(mesh);
    ASSERT_TRUE(handle.ok()) << handle.error().message;
    render::PhongMaterial material(kBaseColor);
    ASSERT_FALSE(material.isTransparent());

    // The same AssetHandle appears in both scenes (the currency views
    // exchange): the mesh path and the slice path reference the same CPU
    // object through the shared registry.
    render::MeshScene meshScene;
    meshScene.meshes.push_back(
        render::MeshInstance{*handle, &material, glm::mat4(1.0f)});
    render::SliceScene sliceScene;
    sliceScene.meshes.push_back(
        render::MeshInstance{*handle, &material, glm::mat4(1.0f)});
    sliceScene.plane.normal = kPlaneNormal;
    sliceScene.plane.point = kPlanePoint;

    render::MeshRenderer meshRenderer(&registry);
    render::SliceRenderer sliceRenderer(&registry);

    // MeshRenderer: FR-render.1 center pixel {51, 102, 204}.
    {
        RenderedTarget target = makeTarget(kTargetWidth, kTargetHeight);
        render::RenderTarget rt;
        rt.framebuffer = &target.framebuffer;
        rt.width = kTargetWidth;
        rt.height = kTargetHeight;
        rt.clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);

        const auto result = meshRenderer.render(meshScene, makeCamera(), rt);
        ASSERT_TRUE(result.ok()) << result.error().message;
        expectBaseColor(readPixel(kCenterX, kCenterY),
                        "mesh path center (32, 32)");
        EXPECT_FALSE(core::hasPendingGlError());
    }

    // SliceRenderer: the same quad sliced at z=0 lies entirely on the plane
    // (all signed distances 0), so the clip keeps all triangles and the full
    // quad renders at {51, 102, 204} (FR-render.4 analytic setup above).
    {
        RenderedTarget target = makeTarget(kTargetWidth, kTargetHeight);
        render::RenderTarget rt;
        rt.framebuffer = &target.framebuffer;
        rt.width = kTargetWidth;
        rt.height = kTargetHeight;
        rt.clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);

        const auto result = sliceRenderer.render(sliceScene, makeCamera(),
                                                 sliceScene.plane, rt);
        ASSERT_TRUE(result.ok()) << result.error().message;
        expectBaseColor(readPixel(kCenterX, kCenterY),
                        "slice path center (32, 32)");
        EXPECT_FALSE(core::hasPendingGlError());
    }

    // After BOTH renderers drew the same mesh: still exactly ONE GPU object —
    // neither renderer uploaded a second copy (the pre-V2 double-upload fix).
    EXPECT_EQ(registry.slotCount(), 1u);
    const auto resolved = registry.resolve(*handle);
    ASSERT_TRUE(resolved.ok()) << resolved.error().message;
    EXPECT_GT((*resolved)->vaoId(), 0u);
}

// ---------------------------------------------------------------------------
// (3) Dangling-handle detection: a stale {index,generation} lookup returns a
//     typed error — no crash. unregister() bumps the slot generation, so the
//     old handle goes stale immediately and stays stale when the freed slot is
//     reused with a fresh generation.
// ---------------------------------------------------------------------------

TEST(T3V2AssetRegistry, StaleHandleResolveReturnsTypedError) {
    render::AssetRegistry registry;
    data::Mesh mesh = makeQuadMesh();
    const auto handle = registry.registerAsset(mesh);
    ASSERT_TRUE(handle.ok()) << handle.error().message;
    const std::uint32_t index = handle->index;
    const std::uint32_t generation = handle->generation;
    EXPECT_GT(generation, 0u) << "generation 0 is the never-allocated marker";

    // Generation mismatch: same index, fabricated wrong generation.
    const render::AssetHandle stale{index, generation + 1u};
    const auto staleResult = registry.resolve(stale);
    EXPECT_TRUE(staleResult.failed());
    EXPECT_EQ(staleResult.error().code, kStaleHandleErrorCode);
    EXPECT_NE(staleResult.error().message.find("stale"), std::string::npos);

    // Generation 0: never allocated (the reserved null-handle marker).
    const render::AssetHandle neverAllocated{index, 0u};
    const auto neverResult = registry.resolve(neverAllocated);
    EXPECT_TRUE(neverResult.failed());
    EXPECT_EQ(neverResult.error().code, kStaleHandleErrorCode);

    // Out-of-range index: beyond the slot table -> typed error, no crash.
    const render::AssetHandle outOfRange{kHugeIndex, generation};
    const auto rangeResult = registry.resolve(outOfRange);
    EXPECT_TRUE(rangeResult.failed());
    EXPECT_EQ(rangeResult.error().code, kOutOfRangeErrorCode);

    // The default handle is the reserved null handle.
    const render::AssetHandle nullHandle;
    EXPECT_TRUE(nullHandle.isNull());
    const auto nullResult = registry.resolve(nullHandle);
    EXPECT_TRUE(nullResult.failed()) << "null handle never resolves";

    // Real dangling: unregister frees the slot and bumps its generation.
    const auto freed = registry.unregister(*handle);
    ASSERT_TRUE(freed.ok()) << freed.error().message;
    EXPECT_EQ(registry.slotCount(), 0u);
    const auto dangling = registry.resolve(*handle);
    EXPECT_TRUE(dangling.failed());
    EXPECT_EQ(dangling.error().code, kStaleHandleErrorCode)
        << "the old handle is stale after unregister";
    // Unregistering the stale handle again is itself a typed error.
    const auto doubleFree = registry.unregister(*handle);
    EXPECT_TRUE(doubleFree.failed());
    EXPECT_EQ(doubleFree.error().code, kStaleHandleErrorCode);

    // Reuse: a NEW CPU object reuses the freed slot with a NEW generation —
    // the old handle stays stale while the new one resolves.
    data::Mesh other = makeQuadMesh(); // distinct object, identical content
    const auto reused = registry.registerAsset(other);
    ASSERT_TRUE(reused.ok()) << reused.error().message;
    EXPECT_EQ(reused->index, index) << "the freed slot index is reused";
    EXPECT_NE(reused->generation, generation)
        << "reuse issues a fresh generation";
    const auto oldStillStale = registry.resolve(*handle);
    EXPECT_TRUE(oldStillStale.failed());
    EXPECT_EQ(oldStillStale.error().code, kStaleHandleErrorCode);
    const auto newResolves = registry.resolve(*reused);
    ASSERT_TRUE(newResolves.ok()) << newResolves.error().message;
    EXPECT_GT((*newResolves)->vaoId(), 0u);
    EXPECT_EQ(registry.slotCount(), 1u);
    EXPECT_FALSE(core::hasPendingGlError());
}

// ---------------------------------------------------------------------------
// (4) Dedup is per individual CPU object: two distinct meshes (identical
//     content) are two GPU objects; and a stale handle inside a rendered scene
//     propagates a typed error out of the renderer instead of crashing.
// ---------------------------------------------------------------------------

TEST(T3V2AssetRegistry, TwoDistinctMeshesAreTwoGpuObjects) {
    render::AssetRegistry registry;
    data::Mesh meshA = makeQuadMesh();
    data::Mesh meshB = makeQuadMesh(); // same content, DIFFERENT object

    const auto a = registry.registerAsset(meshA);
    const auto b = registry.registerAsset(meshB);
    ASSERT_TRUE(a.ok()) << a.error().message;
    ASSERT_TRUE(b.ok()) << b.error().message;

    // One GPU object per individual CPU object: two distinct objects (even
    // with identical content) are two slots with distinct GL names (GL object
    // names are unique among live objects of a type).
    EXPECT_EQ(registry.slotCount(), 2u);
    EXPECT_NE(*a, *b);
    const auto ga = registry.resolve(*a);
    const auto gb = registry.resolve(*b);
    ASSERT_TRUE(ga.ok()) << ga.error().message;
    ASSERT_TRUE(gb.ok()) << gb.error().message;
    EXPECT_NE((*ga)->vaoId(), (*gb)->vaoId());

    // Registering meshA AGAIN still dedups to its existing slot.
    const auto aAgain = registry.registerAsset(meshA);
    ASSERT_TRUE(aAgain.ok()) << aAgain.error().message;
    EXPECT_EQ(*aAgain, *a);
    EXPECT_EQ(registry.slotCount(), 2u);
}

TEST(T3V2AssetRegistry, RendererPropagatesStaleHandleError) {
    render::AssetRegistry registry;
    data::Mesh mesh = makeQuadMesh();
    const auto handle = registry.registerAsset(mesh);
    ASSERT_TRUE(handle.ok()) << handle.error().message;
    render::PhongMaterial material(kBaseColor);

    // Free the slot behind the handle, then render a scene still carrying the
    // now-stale handle: the renderer must surface the typed error (SPEC §5,
    // no exceptions, no crash).
    const auto freed = registry.unregister(*handle);
    ASSERT_TRUE(freed.ok()) << freed.error().message;

    render::MeshScene scene;
    scene.meshes.push_back(
        render::MeshInstance{*handle, &material, glm::mat4(1.0f)});

    RenderedTarget target = makeTarget(kTargetWidth, kTargetHeight);
    render::RenderTarget rt;
    rt.framebuffer = &target.framebuffer;
    rt.width = kTargetWidth;
    rt.height = kTargetHeight;
    rt.clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);

    render::MeshRenderer renderer(&registry);
    const auto result = renderer.render(scene, makeCamera(), rt);
    EXPECT_TRUE(result.failed());
    EXPECT_NE(result.error().message.find("stale"), std::string::npos);
    EXPECT_FALSE(core::hasPendingGlError());
}

} // namespace re::tests