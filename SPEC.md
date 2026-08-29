# SPEC — RenderEngine

> Drafted collaboratively via `/loop-elicitation`. Decisions recorded as they
> land; this file is the source of truth for the loop.

The spec is split into per-section files under `docs/spec/`. Section numbers
are stable — a reference like "SPEC §7" always means section 7 regardless of
which file it lives in. This index is the entry point; edit the individual
section files directly.

## Section index

| § | Section | File |
|---|---|---|
| §1 | Goals & non-goals | `docs/spec/goals.md` |
| §2 | Tech-stack decisions | `docs/spec/techstack.md` |
| §3 | Module blueprint | `docs/spec/modules.md` |
| §4 | Functional requirements | `docs/spec/frs.md` |
| §5 | Non-functional requirements | `docs/spec/nfr.md` |
| §6 | Guardrails / rules | `docs/spec/guardrails.md` |
| §7 | Data & asset plan | `docs/spec/assets.md` |
| §8 | Environment requirements | `docs/spec/env.md` |
| §9 | V2 future scope (roadmap) | `docs/spec/roadmap.md` |
| §10 | Persistence, layouts/pages, and asset lifetime | `docs/spec/persistence.md` |
| §11 | `scene/` disposition and the `broker/` mediation layer | `docs/spec/broker.md` |
| §12 | Materials and lights — `scene/` ↔ `render/` hierarchies | `docs/spec/materials_lights.md` |
| §13 | Open grill — must resolve before V3 implementation | `docs/spec/open_questions.md` |

## At a glance

- **Product:** C++20 / OpenGL 4.6 core / CMake rendering engine, developed on
  Ubuntu inside WSL (Windows 11, WSLg). Renders meshes, volumes, planes, mesh
  slices, and OIT; the MPR sample shows T/C/S + 3D views in one window (§1).
- **Stack:** GLFW 3.4, glad2 v2.0.8, GLM 1.0.1, Dear ImGui v1.92.9, GoogleTest
  v1.15.2, spdlog v1.14.1, stb — all pinned via FetchContent `GIT_TAG` (§2, §6).
- **Modules:** `io/ data/ volume/ scene/ core/ broker/ utils/ render/ app/ tests/` (`test_utils/` peer lib present from start, `AUDIT_SOURCE_DIRS` is `io data volume scene core broker render app utils test_utils tests` — `test_utils` empty until T18, harmless pre-existing, iteration 3 #6) — `core/`
  is the sole owner of raw GL calls (§3); `scene/` `re::scene` `STATIC` is the GL/RE-free app-side
  scene value library (`View{rect,plane,itemIds,gen}`, `Camera{pan/rotate/zoom/orbit → viewMatrix()}`,
  `PlaneDesc{World|VoxelIndex}`, `SceneObject` family `{AssetRef,transform,presentation}`, `SceneStore`/`ViewStore`
  stable handles + per-field `generation` — `data`+`volume`+`glm` only, copyable value semantics, no `App` prefix) (SPEC §3.1, V3.1 landed T1); `broker/` is the heavily abstracted per-type `scene → render` mediation library (ViewBridge) (§3/§11).
- **FRs:** analytic acceptance constants; tolerances 1/255 (color), 1e-6
  (math), ε=1e-4 (plane geometry) (§4).
- **Guardrails:** dependency lock, GL ownership, forbidden patterns, evidence +
  regression lock, asset licensing, build hygiene (§6).
- **Roles:** runner (`tools/run_task.sh`) / implementer / reviewer / orchestrator (user-facing) — see `AGENTS.md` loop-framework + `NAMING_CONVENTIONS.md` §10.
- **Assets:** committed in-repo under `data/`, licensed, SHA256-pinned (§7).
- **Env:** `source tools/env.sh` is the launch prerequisite; convenience
  scripts in `tools/` reconstruct the §8 build/test contract (§8).
- **V2 roadmap:** eight completed V2 items (§9, archived as `V2-T1..V2-T8`) plus the pure-redesign V3 backlog `T1..T16` inc. splits `T3a/b,T8a/b,T11a/b,T14a/b,T15a/b` = 21 active + `T17/T18` stretch = 23 total (`scene/` value lib → `CompositeKey`/`REContext` (formerly `DrawContext`, T2 rename) → `broker/` SRP-split → `View`/`ReView`/`IRenderable` → persistence → `SceneStore`-owned `AssetId` → RE-minimal), mirrored by the numbered backlog in `TASKS.md` (`T1` .. `T16` with splits inc. `T11a/b`, §10-§12). `V3.x` survives only as Spec alias in `roadmap.md` §9.1 — the accepted standard is `Tn: Title / D / T / G` (iteration 2 #1 fixed count `20/22` → `21/23` after `T11→T11a/b` split).
- **Persistence/layouts:** `ReView`/`Re*Object`/assets persist by `CompositeKey{Version,LayoutId,Id,Gen,Hash}` — not by `id` alone or `size` — a camera orbit dirties only `CameraMapper` (per-field `viewGen`), a `2D→3D` toggle on the same `ViewId` rebinds plane+items without map churn (§10).
- **Materials/lights (pure redesign):** `V3` keeps `render::IMaterial→PhongMaterial` single path + fixed headlight (PBR/`Slice`/`Contour`+`ILight` deferred as §1 non-goal); `TransferFunction` stays beside `VolumeMaterial` in `VolumePresentation`; even hierarchies (`Mesh/Volume/Slice/Contour → Phong/PBR`, `Directional/Point/Spot` per `View`) are deferred to §12 inventory only (§12.4). `scene::Camera` (`pan/rotate/zoom/orbit`) sends only `view matrix` to RE (§3.1).