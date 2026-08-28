#pragma once

// scene/object.hpp — SceneObject family aggregate header (T1 pure-redesign, T5 collapse).
//
// The closed `variant< MeshObject,…>` alias (scene/object.hpp:144 in the value-
// type iteration) could not remain the canonical family type once the engine
// grew to at least fifteen object kinds: every new kind required editing the
// variant list and every std::visit call site, violating the open-closed
// principle. This header now aggregates the polymorphic hierarchy header
// scene/iscene_object.hpp plus the six technique kinds that each derive from
// ObjectBase<Derived> and register via REGISTER_SCENE_OBJECT into SceneFactory
// and the Broker. The 11 byte-identical mesh-backed headers (CubeObject,
// SphereObject, Cylinder, Torus, Cone, Arrow, Grid, Axes, Capsule, PointCloud,
// Teapot sharing `AssetRef<Mesh> mesh; mat4 transform; MeshMaterialDesc
// presentation;` at scene/objects/*.hpp:36-40) are collapsed into one
// MeshObject carrying GeometryKind {Mesh, Cube, Sphere, Cylinder, Torus, Cone,
// Arrow, Grid, Axes, Capsule, PointCloud, Teapot} (T5). SceneKind stays for
// technique dispatch only (6 values: Mesh, MeshSlice, Volume, VolumeSlice,
// Plane, Contour); adding a Sphere no longer needs a new header — `MeshObject{
// .geometryKind = Sphere }` via the single MeshObjectMapper renders within
// 1/255 of the old per-kind path. SceneFactory + REGISTER_SCENE_OBJECT remain
// for truly new techniques (e.g., StreamlineObject), not for data-driven mesh
// variations. T5 keeps the file as the stable include point so existing
// `#include "scene/object.hpp"` call sites continue to see MeshObject etc.
// without chasing six includes. T17 AssetRef<T> shared-ptr co-ownership stays
// (object is heap-allocated via unique_ptr<ISceneObject>, asset stays shared),
// and the variant's trivial copy plus exhaustive std::visit compile error are
// replaced by virtual clone() and the loud startup registry completeness check
// SceneFactory::create(kind) (nullptr → typed error). The variant alias is
// removed entirely; `grep -c "variant< MeshObject" scene/` must be zero after
// T1 (Phase A/C gate). Materials stay variant-based — the hierarchy does not
// cascade to LightDesc per the task scope. T5.

#include "scene/geometry_kind.hpp"
#include "scene/iscene_object.hpp"
#include "scene/objects/mesh_object.hpp"
#include "scene/objects/mesh_slice_object.hpp"
#include "scene/objects/volume_object.hpp"
#include "scene/objects/volume_slice_object.hpp"
#include "scene/objects/plane_object.hpp"
#include "scene/objects/contour_object.hpp"

// scene/object.hpp no longer defines a closed variant alias. The canonical
// family type is now the polymorphic re::scene::ISceneObject hierarchy
// (ObjectBase<Derived> plus SceneFactory registry) with 6 technique kinds
// (Mesh, MeshSlice, Volume, VolumeSlice, Plane, Contour) plus GeometryKind
// inside MeshObject. Adding a Sphere variation needs no new header — `MeshObject{
// .geometryKind = GeometryKind::Sphere }` suffices — while a new technique
// (e.g., StreamlineObject) still needs one new header plus one registration
// line. Zero edits to the store or ViewSynchronizer dispatch for variations.
// Every technique object now also carries a visual Layer tag (eight values from Background to OverlayTop) whose default matches its technique (Volume→Volume, Plane→Plane, Mesh→Mesh, etc.) and whose setLayer bumps generation so the broker re-groups by (layer, techniquePriority) — the deterministic ordering replacement for insertion order.
