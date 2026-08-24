#pragma once

// app/oit_scene.hpp — the OIT sample's analytic scene VALUES (T19; T20 split).
//
// ONE shared definition of the order-independent-transparency demo arrangement,
// consumed by BOTH the sample executable (app/oit_sample.cpp, through the
// broker bridge) and its gate (tests/t19_oit_sample_test.cpp, which assembles
// the same constants into a direct MeshRenderer oracle), so the gate asserts
// exactly the arrangement the sample shows.
//
// Since T20 (broker-only app path) this header is PURE scene data + glm math:
// the render-typed Rig/composeFrame helpers moved into the gate test itself
// (the direct-render oracle twin of the broker composition). app/ must not
// name render/ types, and these constants are all an app needs to rebuild the
// identical scene through the broker mapper inventory:
//
//   * TWO OPAQUE meshes — a golden box built by app::makeBoxMesh (flat-shaded
//     shell: every +Z face shades to EXACTLY its base color under the v1
//     head-on light) and the Stanford bunny loaded from data/meshes/bunny.obj,
//     placed at a DIFFERENT depth than the box;
//   * TWO TRANSPARENT glass boxes (alpha 0.5 ⇒ transparent under the
//     isTransparent ⇔ baseColor.a < 1 rule) at two more depths, nested so each
//     glass footprint covers BOTH opaque meshes somewhere and the glasses cover
//     each other.
//
// Composition contract (the "real OIT contract" of FR-render.2/3): opaque
// meshes render FIRST into a depth-enabled target (true occlusion); the
// transparent set is then captured through the linked-list pipeline (depth
// off), sorted per pixel, and composited back-to-front over the opaque result.
// On the bridge path ViewCompositor::renderAll runs exactly those stages; on
// the direct-oracle path the gate drives the same sequence by hand.
//
// All constants below are analytic: byte expectations derive from the capture
// shader (premultiplied base color, no lighting) and the composite blend;
// opaque bytes from the flat-shading rule (+Z face ⇒ shade factor 1).

#include <algorithm>
#include <cstdint>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "data/mesh.hpp"
#include "scene/camera.hpp"

namespace re::app::oit_scene {

// ---------------------------------------------------------------------------
// Analytic arrangement constants (world units; ortho [-aspect,aspect]x[-1,1]
// camera at (0,0,5) looking down -Z).
// ---------------------------------------------------------------------------

/// Opaque golden box: a wide slab at the DEEPEST slot of the arrangement
/// (identity transform — extents are baked into the mesh).
inline constexpr glm::vec3 kGoldMin{-0.90f, -0.75f, -0.95f};
inline constexpr glm::vec3 kGoldMax{+0.90f, +0.75f, -0.75f};
/// Its material (opaque): the +Z front face shades to exactly this color.
inline constexpr glm::vec4 kGoldColor{0.85f, 0.45f, 0.15f, 1.0f};

/// NEAR glass box (red): nearest surfaces of the whole arrangement, offset
/// LEFT so a column exists that it covers while the far shell does not.
inline constexpr glm::vec3 kNearGlassMin{-0.66f, -0.70f, +0.72f};
inline constexpr glm::vec3 kNearGlassMax{+0.34f, +0.70f, +0.92f};
/// Straight RGBA, alpha 0.5 => transparent (isTransparent ⇔ alpha < 1).
inline constexpr glm::vec4 kNearGlassColor{0.90f, 0.20f, 0.20f, 0.50f};

/// FAR glass box (blue): behind both opaque meshes' front surfaces where the
/// probes look, offset RIGHT so it overlaps the near shell on [x=-0.14,+0.34].
inline constexpr glm::vec3 kFarGlassMin{-0.14f, -0.70f, -0.56f};
inline constexpr glm::vec3 kFarGlassMax{+0.86f, +0.70f, -0.36f};
inline constexpr glm::vec4 kFarGlassColor{0.20f, 0.35f, 0.90f, 0.50f};

/// The bunny is scaled uniformly so its LONGEST AABB side becomes exactly
/// kBunnyMaxSide world units, then centered at kBunnyCenter: its whole body
/// lands strictly BETWEEN the two shells (in front of the far shell's front
/// face at z=-0.36, behind the near shell's front face at z=+0.72) and its
/// footprint stays inside BOTH shells' footprints.
inline constexpr float kBunnyMaxSide = 0.24f;
inline constexpr glm::vec3 kBunnyCenter{+0.06f, -0.50f, -0.08f};
/// Opaque greenish tint for the bunny (never probed numerically — its smooth
/// vertex normals make the shaded color vary; only its ALPHA is asserted).
inline constexpr glm::vec4 kBunnyColor{0.30f, 0.65f, 0.40f, 1.0f};

/// Camera eye on the +Z axis; ortho maps NDC [-1,1]^2 onto the viewport with
/// horizontal extent grown to the aspect ratio (no stretch on any window).
inline constexpr glm::vec3 kEye{0.0f, 0.0f, 5.0f};
/// Near/far clip planes enclosing the whole arrangement (eye distances
/// 4.08..5.95 world units).
inline constexpr float kNearPlane = 0.1f;
inline constexpr float kFarPlane = 10.0f;

/// Frame clear color (probes never sample the background).
inline constexpr glm::vec4 kClearColor{0.0f, 0.0f, 0.0f, 1.0f};

/// Number of transparent instances the pipeline must capture per frame —
/// exactly what the engagement spy must count (FR-render.3 acceptance).
inline constexpr std::size_t kTransparentCount = 2u;

/// Uniform scale so the bunny mesh's longest AABB side is exactly
/// kBunnyMaxSide world units, then centered at kBunnyCenter. Computed from the
/// committed asset's bounds, so the transform is deterministic for the golden
/// file and identical between sample and gate.
inline glm::mat4 bunnyModel(const data::Aabb& b) {
    const glm::vec3 mid = 0.5f * (b.min + b.max);
    const glm::vec3 extent = b.max - b.min;
    const float longest = std::max(extent.x, std::max(extent.y, extent.z));
    const float s = kBunnyMaxSide / longest;
    return glm::translate(glm::mat4(1.0f), kBunnyCenter) *
           glm::scale(glm::mat4(1.0f), glm::vec3(s)) *
           glm::translate(glm::mat4(1.0f), -mid);
}

/// Ortho camera framing the arrangement: eye on +Z looking down -Z, NDC
/// [-1,1] vertically, [-aspect,+aspect] horizontally (aspect = width / height
/// of the target) so no window shape stretches the scene. A pure scene::Camera
/// value — the bridge validates nothing about this 3D-framed ortho pairing
/// because the OIT composition root opts out of the CameraMapper registration
/// (see app_context.hpp Params.registerCameraMapper).
inline scene::Camera cameraFor(float aspect) {
    scene::Camera camera(kEye, glm::vec3(0.0f, 0.0f, 0.0f),
                         glm::vec3(0.0f, 1.0f, 0.0f));
    camera.setOrtho(-aspect, aspect, -1.0f, 1.0f, kNearPlane, kFarPlane);
    return camera;
}

} // namespace re::app::oit_scene
