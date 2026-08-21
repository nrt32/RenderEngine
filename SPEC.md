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

## At a glance

- **Product:** C++20 / OpenGL 4.6 core / CMake rendering engine, developed on
  Ubuntu inside WSL (Windows 11, WSLg). Renders meshes, volumes, planes, mesh
  slices, and OIT; the MPR sample shows T/C/S + 3D views in one window (§1).
- **Stack:** GLFW 3.4, glad2 v2.0.8, GLM 1.0.1, Dear ImGui v1.92.9, GoogleTest
  v1.15.x, spdlog v1.14.1, stb — all pinned via FetchContent `GIT_TAG` (§2, §6).
- **Modules:** `io/ data/ volume/ core/ render/ app/ tests/` — `core/` is the
  sole owner of raw GL calls (§3).
- **FRs:** analytic acceptance constants; tolerances 1/255 (color), 1e-6
  (math), ε=1e-4 (plane geometry) (§4).
- **Guardrails:** dependency lock, GL ownership, forbidden patterns, evidence +
  regression lock, asset licensing, build hygiene (§6).
- **Assets:** committed in-repo under `data/`, licensed, SHA256-pinned (§7).
- **Env:** `source tools/env.sh` is the launch prerequisite; convenience
  scripts in `tools/` reconstruct the §8 build/test contract (§8).
- **V2 roadmap:** ten future-scope items in approved product-first order
  (§9), mirrored by the numbered backlog in `TASKS.md`.