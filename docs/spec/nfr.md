# SPEC §5 — Non-functional requirements

> Part of the RenderEngine spec (see `SPEC.md` for the index). Section numbers
> are stable; references like "SPEC §5" mean this file.

## 5. Non-functional requirements

### Generic (always kept)
- **Build hygiene** — warnings-as-errors in the gate; no warning-suppression
  pragmas/flags.
- **Incremental/cached builds** — configure runs only when CMake inputs change
  (`tools/configure.sh`); `cmake --build` rebuilds only what changed; ccache
  (when installed) is the compiler launcher so recompiles are served from the
  compiler cache. No script does a full clean rebuild by default.
- **Determinism** — reproducible builds; deterministic test ordering.
- **Documentation** — Doxygen comments on all public API (required by user).
- **Portability** — builds and runs on Ubuntu/WSL target; no Windows-only code
  paths (cross-platform CI out of scope).
- **Memory/sanitizers** — ASan + UBSan on all nine `re_*` libs (not just test/sample TUs) via `INTERFACE re_project_sanitizers` (`-fsanitize=address,undefined -fno-omit-frame-pointer -O1` Debug, `option(RE_ENABLE_SANITIZERS)` ON for Debug, OFF for Release). Every `re_*` target links the interface; ad-hoc per-dir `tests/CMakeLists.txt:84-90` / `app/CMakeLists.txt:97-103` flag blocks deleted (audit `no_per_target_sanitize`). `cmake --build --verbose | grep -c "\-fsanitize.*re_core"` ≥9 proves coverage. `ASAN_OPTIONS` suppressions for llvmpipe/d3d12 documented in `env.md:30` (leak-gate env) and T12 DoD; samples remain clean under `xvfb` (FR-app.1).

### Product-specific (adopted)
- **Deterministic rendering** — same scene + camera → same frame output;
  required by the analytic pixel checks.
- **Single-threaded (V1) — EOL-extensible via `IJobExecutor` (V3) (stretch — deferred to T10 RHI):** one render thread, no concurrency in v1 semantics (no premature `mutex`/`lock`/`thread`/async in render hot path). Documented so no premature threading is added. **V3 retains single-threaded contract but preserves OCP for EOL threading:** `broker::IDirtyTracker` abstracts the dirty bus (DIP) and `broker::IJobExecutor` remains the declared execution seam — **execute()-only since the persistence-honesty task**: the former unused batched entry was removed together with its discarded-results call site (write-only scaffolding), so no code path pretends to parallelize; a future `ThreadPoolExecutor` (V4+) EXTENDS the interface with the batched form when a real consumer exists, without editing `render`/`scene` call sites (OCP). The contract for EOL is explicit: `render` draw calls (`drawLayer`, `ITransparencyPipeline::begin/end`) remain **externally synchronised** (`sync` happens-before `renderAll` happens-before `presentAll`); `AssetStore` ref-counts are `std::atomic<uint32_t>` (data-race-free). See `modules.md` §3/§11.6 EOL-3 and `open_questions.md` §13.8 Q37 — the header review must document which objects are `thread-compatible` vs `thread-safe`.
- **RHI capability contract (EOL, web-verified) (stretch — deferred to T10 RHI):** `core::IRHIContext::capabilities()` enumerates `{bool geometryShader; bool ssboAtomics; uint32_t maxTexture2DSize; uint32_t maxTexture3DSize; bool bindlessTextures; bool computeShader; uint32_t maxColorAttachments; uint32_t maxLayers; }` — renderers query once at init via `IRHIContext::capabilities()` (O3DE RHI Frame Scheduler caps + Qt QRhi `isFeatureSupported()`; Adept RHI command-list caps) and degrade gracefully with typed `Result<void>` error `Error::Unsupported` (SPEC §5 typed error) surfaced by `ViewCompositor` (product decision: typed error, not silent degrade — §13 Q32 binding recommendation — because SPEC §5 mandates typed errors and silent best-effort hides capability loss). Degrade table: `!geometryShader` → `SliceRenderer` CPU clipping + `MeshRenderer` fallback; `!ssboAtomics` → `LinkedListOIT` → weighted-blended OIT; `volumeSize>maxTexture3DSize` → halve res or tile. This preserves OCP for Vulkan/Metal ports (new `IRHIContext` impl, zero `render/` edits) and for future hardware that lacks geometry shaders/SSBO image atomics (see `broker.md` §11.6 EOL-1, §13 Q31/Q32) — **(stretch — T10 deferred — `core/rhi/IRHIContext` not landed this iteration; `IRHIContext::capabilities()` deferred to RHI landing; current path remains direct GL probe via `core::loadCoreGl`, no capability-gated degrade yet; `core/` remains sole `gl*` owner per `gpu_api_ownership` `core|\bgl[A-Z]`)**.
- **Memory budget caps on sample data** — sample scenes capped; the committed
  sample volume is downsampled to ≤ 128³ (§7) to stay within sane GPU/RAM on WSL.
- **OIT per-view SSBO budget and no-fallback contract (T8, corrected)** — per-view capture storage cost is `w*h*16*32` bytes (w×h pixels × 16 fragments/pixel × 32 B/node: vec4 16 + float 4 + uint 4 padded to vec4). Example: 640×480≈152 MB (307200×16×32 = 157286400 B) and 1920×1080≈1.03 GB (2073600×16×32 = 1061683200 B). If `ITransparencyPipeline::begin()` cannot satisfy the budget (unsupported atomics or over-budget viewport) it returns a typed error that aborts the transparent-capable mesh pass — no silent blend fallback — and the bridge surfaces the error so the pass renders opaque-only (SPEC §5).
- **Typed error reporting** — runtime failures (load, GL, shader) surface as
  typed, actionable diagnostics, never silent. Error identity is the pair
  `(data::Error::domain, data::Error::code)`: numeric code ranges repeat
  across producers (the three io/ loaders all start at `FileOpen == 1`), so
  each producer stamps its `data::ErrorDomain` (`ImageIo`/`MeshIo`/
  `VolumeIo`/`Shader`/`Core`/`Utils`/`Render`/`Broker`/`Scene`) and
  consumers disambiguate structurally — never by parsing message strings
  (SPEC §6 "Error codes carry their domain"; landed T22). Dereferencing a
  failed `Result` asserts in debug builds (abort, never an exception);
  release builds keep the documented UB, so callers branch on `ok()` /
  `failed()` first. The dead `hasValue()` accessor (could never differ from
  `ok()`) was removed in T22; monadic `map()`/`andThen()` helpers collapse
  sequential fallible-call chains (e.g. the MPR bridge's
  sync → renderAll → presentAll).
- **Logging** — **spdlog** (pinned) provides trace/debug/info/warn/error/fatal;
  no custom logging framework. Logging-discipline guardrail still applies: no
  raw `printf`/`std::cout` for diagnostics.
- **Profiling (macro-gated)** — in `core/`: scoped profiler macros compiled out
  unless enabled; can measure FPS, data-load time, data-transfer (upload) time,
  draw-call time.
- **Infra/tests batch (T6 IT1-IT5, landed T6):** `IT2` extracted
  `tests/test_helpers.{hpp,cpp}` (`makeQuad`/`makeCamera`/`WindowTarget`/`readPixel`/`expectPixel`
  — single source, ~150 lines removed, drift eliminated; `grep -c "makeQuadMesh"
  tests/test_helpers.cpp==1` proves single definition, `ctest -V` still shows one
  binary `re_tests` (monolithic, single shared GL context via
  `OffscreenEnvironment`)); `IT1`/`IT3`/`IT4`/`IT5` **deferred/documented — intentional
  gate choices:** monolithic `re_tests` (single GL context, deterministic headless
  fixture) + `tN_` naming (task traceability, `T1..T19` maps to `TASKS.md`) + xvfb
  hard-fail (config-fail loudness, FR-app.1 sample smoke needs display) + weak
  asserts compensated by strong neighbors (R4 evidence rule, 1/255 + 1e-6 gates).
  Restructure deferred; this note is the decision record. Ownership split vs T18:
  pixel-read (`readPixel`/`expectPixel` via `utils::PixelReader` → `core::readRgba8`)
  stays here until T18 migrates to `test_utils::PixelReader` via
  `REContext::readRgba8` (raw `glReadPixels` stays `core/re_context.cpp` count 1).
- **Soft performance floor (stretch) — NOT adopted for v1** (no automated FPS
  gate; interactivity is a manual sample check) — **(stretch)** explicitly tagged for checklist.
- **EOL sustainment (V3 design-to-EOL — web-verified) (stretch) — stretch (T10) deferred for (a)(d-i):** The V3 redesign is the **last architectural break** — it must sustain to EOL without another rewrite (see §11.6 table; Adept multi-GPU RHI threading + Qt QRhi cross-API + O3DE Frame Scheduler). Contracts that guarantee this: (a) `core/rhi/` abstraction (`IRHIContext{ITexture (stretch — T10 deferred — not landed this iteration; only `REContext` instance (formerly `DrawContext`, T2) + `IDirtyTracker` interface + `CompositeKey::Version` field remain as extension points; `core/` remains sole `gl*` owner per `gpu_api_ownership` `core|\bgl[A-Z]`),IBuffer,IFramebuffer,IShader,capabilities(),blit,memoryBarrier}` — new graphics API = new impl, zero `render` edits; shader via `IRHIShaderDesc{stages,SPIRV|GLSL,defines}` so Vulkan ingests SPIR-V — Qt QRhi `QShader`) — **(stretch — T10 deferred — `core/rhi/` still absent; `core/` remains sole `gl*` owner, `rhi_ownership` / `require_only` not yet enforced)**; (b) `IMapper`/`IViewBridge` + `TranslateContext{ViewContext,optional<VolumeContext>}` abstractions owned by `broker/` (policy owns abstraction — DIP per Oleksii Tym, Baeldung; composition root `AppContext`); (c) `CompositeKey{Version,LayoutId,ViewId,Type,Generation,ContentHash}` hierarchical `Version:LayoutId:Type:Hash` SHA-256 at load time (cache-key versioning per Software Patterns Lexicon + Dev Genius + System Overflow) — **`Version` field is the only T10 extension point landed; full `Version` migration (file format + `SceneMigrator` chain) is stretch deferred**; (d) per-field generations + `IDirtyTracker` hybrid poll/push + `IJobExecutor` inline fallback (SRP per field, ISP per mapper, OCP via job executor — ICS SRP) — **`IDirtyTracker` interface + `REContext` instance (formerly `DrawContext`, T2) are the landed extension points; `IJobExecutor` `ThreadPoolExecutor` and parallel execution are stretch deferred (execute()-only inline fallback since the persistence-honesty task removed the unused batched entry)**; (e) role-segregated `IMaterial` (`IColorMaterial`/`IVolumeMaterial`/`ILineMaterial`) + `ILight` (`Directional/Point/Spot`) variant visitor + `VolumePresentation{mat+TF}` ISP/OCP (lsp Rectangle-Square fix; variant vs virtuals Here Be Braces); (f) type-erased `IRenderable` (`ITypeErasedDraw` virtual, not `std::function` — avoids allocation, OCP for `PointCloudRenderer`); (g) `REContext{Viewport,ClearColor,Depth,Blend,spy}` (formerly `DrawContext`, T2) per `FrameContext` instance (SRP via instance, not global static `invalidateDrawCache`) — **landed extension point (T2); no `core/rhi/` migration yet**; (h) `Layout::resolve(framebufferSize,contentScale)` physical pixels + `contentScale` (HiDPI DPR via `glfwGetFramebufferSize`/`glfwGetWindowContentScale` — web.dev high-dpi + GLFW #1857); (i) serialization `SceneStore::serialize()/deserialize()` JSON+binary + `SceneMigrator{Version→Version}` chain + `Arc<AssetData>` copy-on-write for undo/page branching (DCS Data Contracts 2026) — **(stretch — T10 deferred — not landed this iteration; no file format, no `Version` migration chain; JSON dependency `nlohmann/json` pinned for future use only)**. Each contract is audited in `tools/audit.rules` (`broker_per_type`, `forbid_outside core/rhi/gl|`, `asset_indirection`, `no_dump_sync`, `disposition_scene/render`) — **`rhi_ownership` / `require_only` remain stretch deferred (`core|` anchor stays, `core/rhi/gl|` not yet enforced; `IJobExecutor` inline fallback remains synchronous)** + N>=3 readback green runs. See `open_questions.md` §13.8 for the 8 EOL questions (now with binding recommendations for Q31-36/38; Q37 threading contract binding, see above).