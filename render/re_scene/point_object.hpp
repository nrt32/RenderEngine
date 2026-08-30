#pragma once

// render/re_scene/point_object.hpp — RePointObject RE-minimal handle for GPU point impostors (V7 T9, SPEC §12.4).
//
// Points are visualised via two broker-routed paths per the V7 design locked at 2026-08-30: a single 3D Point with world-space radius and Solid fill reuses MeshRenderer with GeometryKind::Sphere to guarantee an exact lit-sphere oracle within 1/255 (the delegate builds a sphere mesh via the shared AssetRegistry and forwards to MeshRenderer::drawLayer), while every other case — PointCloudObject with hundreds of points sharing one worldUnits toggle but per-point radii/colors/fill bits, 2D circles where is2D()==true via ClipPlane presence (flat alpha halo, no gl_FragDepth write), pixel-constant 10 px markers that must stay 10 px at two camera distances within 1/255, and Hollow vs GridDashed fill variants — is drawn as an impostor billboard by PointRenderer. The billboard owns a LazyProgramCache impostorProgram_ for point_impostor.vert/.frag and expands each center via center→clip→ndc→viewport using Camera right/up derived from the inverse view matrix, with radiusScreen = worldUnits ? radius*viewport.w/pos.w/tan(fov/2) approximated via projection delta of a right-offset world point : radiusPx, then the fragment shader computes r2=dot(mapping,mapping), discards where r2>1, branches on fill (Solid fills disk, Hollow discards inner ring r<0.5, GridDashed discards checker 0.7 threshold giving a distinct golden within 1/255), shades with max(dot(n,(0,0,1)),0) headlight, and writes gl_FragDepth from ray-sphere intersection for 3D but flat halo for 2D. This RE type therefore keeps only RE-direct fields: pos (derived world-space after object transform), radius (uniform-ready float), worldUnits (uniform-ready bool toggle), color (uniform-ready vec4, alpha<1 routes to LinkedListOIT), PointFill (uniform-ready enum), fillParam (uniform-ready grid density). No verbatim scene::PointObject copy beyond these uniform-ready handles, and no data::Mesh positions, satisfying asset_indirection. (V7 T9)

#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace re::render::re_scene {

/// RE-minimal point fill (mirrors scene::PointFill without including scene — disposition_render).
enum class PointFill : uint8_t { Solid = 0, Hollow = 1, GridDashed = 2 };

/// RE-minimal single point object (V7 T9).
///
/// Mirrors scene::PointObject {vec3 position, float radius, bool worldUnits, vec4 color, PointFill fill, float fillParam}
/// with only RE-direct fields: pos is world-space after object.transform (derived from local position), radius/worldUnits/color/fill are uniform-ready handles for the impostor shader. No raw GL, no scene include.
struct RePointObject {
    glm::vec3 pos{0.0f, 0.0f, 0.0f};       ///< derived — world-space position = transform * vec4(localPos,1)
    float radius{5.0f};                   ///< uniform-ready — radius (world units when worldUnits true, else px)
    bool worldUnits{true};                ///< uniform-ready — true→world-scaled via projection delta, false→10px constant
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f}; ///< uniform-ready — baseColor (alpha<1 → OIT premul)
    PointFill fill{PointFill::Solid};    ///< uniform-ready — fill mode for impostor interior
    float fillParam{0.0f};                ///< uniform-ready — grid density / hollow thickness param
};

/// RE-minimal point cloud collection for batched impostor draws (V7 T9, SPEC §12.4, point impostor sharing).
///
/// The V7 design batches hundreds of PointCloud points sharing one worldUnits flag but with per-point radii/colors/fill encodings into a single SSBO of PointInstance; this collection mirrors that by holding a vector of RePointObject where each element's pos is already world-space after the object's transform (derived), radius/worldUnits/color/fill are uniform-ready for the impostor shader, and the shared worldUnits policy lets the renderer decide world-scaled vs pixel-constant in one draw without per-point branching. (V7 T9)
struct RePointScene {
    std::vector<RePointObject> points; ///< handle — vector of point instances (handle-free collection, uniform-ready per element)
};
using RePointCloud = RePointScene;

} // namespace re::render::re_scene

namespace re::render {
using RePointObject = re_scene::RePointObject;
using RePointCloud = re_scene::RePointCloud;
/// Backward alias for broker point-cloud mapper (expects RePointCloudObject).
using RePointCloudObject = re_scene::RePointCloud;
}
