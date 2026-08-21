# SPEC §6 — Guardrails / rules

> Part of the RenderEngine spec (see `SPEC.md` for the index). Section numbers
> are stable; references like "SPEC §6" mean this file.

## 6. Guardrails / rules (hard, enforced by tools/audit.rules + AGENTS.md)

- **Dependency lock** — every third-party dep pinned via FetchContent `GIT_TAG`
  (GLFW 3.4, glad2 v2.0.8, GLM 1.0.1, ImGui v1.92.9, GoogleTest v1.15.x,
  spdlog v1.14.1, stb pinned commit). A `GIT_TAG` must be a **release tag or
  commit SHA, never a branch name** (the reviewer verifies this). No unpinned
  fetches, no vendored binaries. *Audit: `deps_pinned`.*
- **GL ownership** — raw `glXxx(...)` calls only under `core/` (RAII objects +
  thin `core::Draw` API); `render/`, `app/`, `tests/` use `core/` wrappers;
  never in `io/`, `data/`, `volume/`. *Audit: `gpu_api_ownership`.*
- **Forbidden patterns** — legacy fixed-function GL anywhere
  (`no_legacy_api`); hard-coded secrets anywhere (`no_secrets`); readback raw
  calls only under `core/` (test-consumed) (`no_production_readback`); no raw
  `printf`/`std::cout` for diagnostics —
  use spdlog (`no_raw_diagnostics`).
- **Evidence rule + regression lock** (always keep, generic) — every gate
  produces verifiable evidence; a failing gate blocks the task; established
  behaviors must not regress without explicit approval.
- **Asset/data licensing** — only clearly-licensed free data (CC0 / CC-BY /
  CC-BY-SA / public domain) for meshes and volumes; a LICENSE file committed
  beside every dataset; no unlicensed redistribution. *Audit: `assets_licensed`
  (grep is only a floor — the T2 gate enforces one LICENSE per dataset dir).*
- **Build hygiene** — warnings-as-errors, no warning-suppression flags/pragmas
  (generic built-in checks).