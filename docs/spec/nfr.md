# SPEC §5 — Non-functional requirements

> Part of the RenderEngine spec (see `SPEC.md` for the index). Section numbers
> are stable; references like "SPEC §5" mean this file.

## 5. Non-functional requirements

### Generic (always kept)
- **Build hygiene** — warnings-as-errors in the gate; no warning-suppression
  pragmas/flags.
- **Determinism** — reproducible builds; deterministic test ordering.
- **Documentation** — Doxygen comments on all public API (required by user).
- **Portability** — builds and runs on Ubuntu/WSL target; no Windows-only code
  paths (cross-platform CI out of scope).
- **Memory/sanitizers** — ASan + UBSan on test binaries (already in gate).

### Product-specific (adopted)
- **Deterministic rendering** — same scene + camera → same frame output;
  required by the analytic pixel checks.
- **Single-threaded** — one render thread, no concurrency in v1. Documented so
  no premature mutex/threading is added.
- **Memory budget caps on sample data** — sample scenes capped; the committed
  sample volume is downsampled to ≤ 128³ (§7) to stay within sane GPU/RAM on WSL.
- **Typed error reporting** — runtime failures (load, GL, shader) surface as
  typed, actionable diagnostics, never silent.
- **Logging** — **spdlog** (pinned) provides trace/debug/info/warn/error/fatal;
  no custom logging framework. Logging-discipline guardrail still applies: no
  raw `printf`/`std::cout` for diagnostics.
- **Profiling (macro-gated)** — in `core/`: scoped profiler macros compiled out
  unless enabled; can measure FPS, data-load time, data-transfer (upload) time,
  draw-call time.
- **Soft performance floor (stretch): NOT adopted for v1** (no automated FPS
  gate; interactivity is a manual sample check).