#pragma once

// scene/point_fill.hpp — point fill modes for PointObject / PointCloudObject (V7 T2).
//
// Points are visualised in two distinct render paths per the V7 design: a single 3D Point reuses MeshRenderer with GeometryKind::Sphere (lit sphere impostor, world-space radius default with optional worldUnits false → 10 px constant screen size for markers that stay legible under zoom), while PointCloud and 2D points (ClipPlane present → flat alpha halo, no gl_FragDepth write) go through PointRenderer's impostor billboard that expands a quad [−1,−1]..[1,1] in viewport space and shades inside the unit circle r2=dot(mapping,mapping)≤1. The fill mode controls the interior of that circle: Solid fills the entire disk, Hollow leaves a ring (outer radius 1, inner radius fillParam), GridDashed draws a grid or dashed pattern inside the disk for dense clouds where solid markers would occlude. These are pure value tags — no GL, no render dependency — so scene/ stays GL-free and RE-free as required by the disposition guardrails, and the point mapper can turn them into a uniform-ready fill uniform for the impostor shader without touching render headers. (V7 T2)

namespace re::scene {

/// Point fill — controls how the impostor circle interior is shaded in PointRenderer.
///
/// Solid: filled disk (opaque where r2≤1, discard otherwise). Hollow: ring
/// with thickness derived from fillParam (inner radius = fillParam, outer =1;
/// e.g., hollow sphere outline). GridDashed: grid or dashed hatch inside the
/// disk (fillParam drives grid spacing/dash density) for dense PointClouds
/// where solid points would occlude. Shared between PointObject (single) and
/// PointCloudObject (per-point fillBits encodes the same variant for 100s of
/// points with a shared worldUnits flag).
enum class PointFill : uint8_t {
    Solid = 0,
    Hollow = 1,
    GridDashed = 2
};

} // namespace re::scene
