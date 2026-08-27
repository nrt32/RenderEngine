#pragma once

// scene/object.hpp — SceneObject family aggregate header (T1 pure-redesign).
//
// The closed `variant< MeshObject,…>` alias (scene/object.hpp:144 in the value-
// type iteration) could not remain the canonical family type once the engine
// grew to at least fifteen object kinds: every new kind required editing the
// variant list and every std::visit call site, violating the open-closed
// principle. This header now aggregates the polymorphic hierarchy header
// scene/iscene_object.hpp plus the fifteen concrete objects/*.hpp kinds that
// each derive from ObjectBase<Derived> and register via REGISTER_SCENE_OBJECT
// into SceneFactory and the Broker. The shared ObjectHeader{ObjectId, transform,
// generation, setTransform} that every former value type hand-copied is now
// centralised in ObjectBase, so generation bumping stays consistent and a
// future slab or arena in SceneStore can move the header without touching each
// concrete header. T1 D keeps the file as the stable include point so existing
// `#include "scene/object.hpp"` call sites continue to see MeshObject etc.
// without chasing fifteen includes. T17 AssetRef<T> shared-ptr co-ownership
// stays (object is heap-allocated via unique_ptr<ISceneObject>, asset stays
// shared), and the variant's trivial copy plus exhaustive std::visit compile
// error are replaced by virtual clone() and the loud startup registry
// completeness check SceneFactory::create(kind) (nullptr → typed error). The
// variant alias is removed entirely; `grep -c "variant< MeshObject" scene/`
// must be zero after T1 (Phase A/C gate). Materials stay variant-based — the
// hierarchy does not cascade to LightDesc per the task scope. T1 D.

#include "scene/iscene_object.hpp"
#include "scene/objects/mesh_object.hpp"
#include "scene/objects/mesh_slice_object.hpp"
#include "scene/objects/volume_object.hpp"
#include "scene/objects/volume_slice_object.hpp"
#include "scene/objects/plane_object.hpp"
#include "scene/objects/contour_object.hpp"
#include "scene/objects/teapot_object.hpp"
#include "scene/objects/sphere_object.hpp"
#include "scene/objects/cube_object.hpp"
#include "scene/objects/cylinder_object.hpp"
#include "scene/objects/torus_object.hpp"
#include "scene/objects/cone_object.hpp"
#include "scene/objects/arrow_object.hpp"
#include "scene/objects/grid_object.hpp"
#include "scene/objects/axes_object.hpp"
#include "scene/objects/point_cloud_object.hpp"
#include "scene/objects/capsule_object.hpp"

// scene/object.hpp no longer defines a closed variant alias. The canonical
// family type is now the polymorphic re::scene::ISceneObject hierarchy
// (ObjectBase<Derived> plus SceneFactory registry), so adding TeapotObject
// or any future kind needs only one new header plus one registration line and
// zero edits to the store or ViewSynchronizer dispatch (open for extension).
