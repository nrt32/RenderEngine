#pragma once

// scene/geometry_kind.hpp — GeometryKind for MeshObject (T5 collapse).
//
// The 11 byte-identical mesh-backed headers (CubeObject, SphereObject, etc.
// sharing `AssetRef<Mesh> mesh; mat4 transform; MeshMaterialDesc presentation;`
// at scene/objects/*.hpp:36-40) are collapsed into one MeshObject carrying a
// GeometryKind tag. SceneKind stays for technique dispatch only (6 values:
// Mesh, MeshSlice, Volume, VolumeSlice, Plane, Contour) while GeometryKind
// distinguishes data-driven mesh variations (Cube vs Sphere etc.) without a new
// header. SceneFactory + REGISTER_SCENE_OBJECT remain for truly new techniques
// (e.g., StreamlineObject), not for mesh variations. T5.

namespace re::scene {

/// GeometryKind — data-driven variation inside the Mesh technique.
///
/// The 12 values cover the original Mesh plus the 11 collapsed mesh-backed
/// kinds (Cube, Sphere, Cylinder, Torus, Cone, Arrow, Grid, Axes, Capsule,
/// PointCloud, Teapot). All map through the single MeshObjectMapper to the same
/// render::MeshInstance path, so adding a procedural Sphere no longer needs a
/// new header — `MeshObject{ .geometryKind = GeometryKind::Sphere }` renders
/// within 1/255 of the old SphereObject path. T5.
enum class GeometryKind : uint32_t {
    Mesh = 0,
    Cube = 1,
    Sphere = 2,
    Cylinder = 3,
    Torus = 4,
    Cone = 5,
    Arrow = 6,
    Grid = 7,
    Axes = 8,
    Capsule = 9,
    PointCloud = 10,
    Teapot = 11,
    Count = 12
};

} // namespace re::scene
