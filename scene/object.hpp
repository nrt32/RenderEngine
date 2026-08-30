#pragma once

// scene/object.hpp — SceneObject family aggregate header (T1 pure-redesign, T5 collapse, V7 T2 extension for Csg/Point/Line).
//
// V7 T2 adds three new technique kinds — Csg (flat multi-subtract/multi-paint Puxel pipeline, base+subtractors+paints, B's material drives hole, paintInterior controls recolor scope), Point (single marker, 3D→MeshRenderer Sphere reuse vs 2D→PointRenderer impostor with Solid/Hollow/GridDashed fill, worldUnits toggle) and PointCloud (batched hundreds, per-point fillBits, shared worldUnits), and Line (SSBO+gl_VertexID 6-vert view-quad strip, Rougier dash, miterLimit 4→bevel caps) — raising SceneKind::Count from 6 to 9 while Layer::Count stays 8 because layers are stacking (anonymous LAYER_0..7) and kinds are technique dispatch order inside each layer (global techniqueOrder [Volume,VolumeSlice,Plane,Csg,Mesh,MeshSlice,Point,Line,Contour] size 9, Csg before Mesh via CsgOitStage). This header aggregates the polymorphic hierarchy header plus the now nine technique kinds that each derive from ObjectBase<Derived> and register via REGISTER_SCENE_OBJECT into SceneFactory and the Broker; the new headers are header-only value types with no GL/RE dependency so disposition_scene and gpu_api_ownership remain satisfied, and the variant MaterialDesc additively grows 4→7 (Mesh,Volume,Slice,Contour,Point,Line,Csg) without editing existing descs.
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
#include "scene/objects/csg_object.hpp"
#include "scene/objects/line_object.hpp"
#include "scene/objects/mesh_object.hpp"
#include "scene/objects/mesh_slice_object.hpp"
#include "scene/objects/plane_object.hpp"
#include "scene/objects/point_cloud_object.hpp"
#include "scene/objects/point_object.hpp"
#include "scene/objects/volume_object.hpp"
#include "scene/objects/volume_slice_object.hpp"
#include "scene/objects/contour_object.hpp"

// scene/object.hpp no longer defines a closed variant alias. The canonical
// family type is now the polymorphic re::scene::ISceneObject hierarchy
// (ObjectBase<Derived> plus SceneFactory registry) with 9 technique kinds
// (Mesh, MeshSlice, Volume, VolumeSlice, Plane, Contour, Csg, Point, Line) plus GeometryKind
// inside MeshObject. Adding a Sphere variation needs no new header — `MeshObject{
// .geometryKind = GeometryKind::Sphere }` suffices — while a new technique
// (e.g., CsgObject/PointObject/LineObject) still needs one new header plus one registration
// line. Zero edits to the store or ViewSynchronizer dispatch for variations (closed for modification, open for extension via SceneFactory + Broker pair-key).
// Every technique object now also carries a visual Layer tag LAYER_0..7 (eight anonymous values, lower numeric draws first) whose default is LAYER_0 for all nine kinds and whose setLayer and setPriority bump generation so the broker re-groups by (layer, techniqueOrder, priority) — the deterministic ordering replacement for insertion order; no semantic names, no per-view mask or override. (V7 T2)
