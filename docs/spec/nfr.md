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
- **Memory/sanitizers** — ASan + UBSan on test binaries (already in gate).

### Product-specific (adopted)
- **Deterministic rendering** — same scene + camera → same frame output;
  required by the analytic pixel checks.
- **Single-threaded (V1) — EOL-extensible via `IJobExecutor` (V3):** one render thread, no concurrency in v1 semantics (no premature `mutex`/`lock`/`thread`/`async` in render hot path). Documented so no premature threading is added. **V3 retains single-threaded contract but preserves OCP for EOL threading:** `ViewSynchronizer::sync` / `Broker::parallelFor` / `AssetStore::registerAsset` are abstracted behind `core::IJobExecutor` + `broker::IDirtyTracker` (DIP) — the V3 code ships with an **inline `IJobExecutor` fallback** (synchronous, zero threads, `execute(f){f();}`) that keeps ASan/UBSan/1-thread determinism, while a future `ThreadPoolExecutor` (V4+) can be injected without editing `render`/`broker`/`scene` (OCP). The contract for EOL is explicit: `render` draw calls (`drawLayer`, `ITransparencyPipeline::begin/end`) remain **externally synchronised** (`sync` happens-before `renderAll` happens-before `presentAll`); `AssetStore` ref-counts are `std::atomic<uint32_t>` (data-race-free) even under inline executor. See `modules.md` §3/§11.6 EOL-3 and `open_questions.md` §13.8 Q37 — the header review must document which objects are `thread-compatible` vs `thread-safe`.
- **RHI capability contract (EOL, web-verified):** `core::IRHIContext::capabilities()` enumerates `{bool geometryShader; bool ssboAtomics; uint32_t maxTexture2DSize; uint32_t maxTexture3DSize; bool bindlessTextures; bool computeShader; uint32_t maxColorAttachments; uint32_t maxLayers; }` — renderers query once at init via `IRHIContext::capabilities()` (O3DE RHI Frame Scheduler caps + Qt QRhi `isFeatureSupported()`; Adept RHI command-list caps) and degrade gracefully with typed `Result<void>` error `Error::Unsupported` (SPEC §5 typed error) surfaced by `ViewCompositor` (product decision: typed error, not silent degrade — §13 Q32 binding recommendation — because SPEC §5 mandates typed errors and silent best-effort hides capability loss). Degrade table: `!geometryShader` → `SliceRenderer` CPU clipping + `MeshRenderer` fallback; `!ssboAtomics` → `LinkedListOIT` → weighted-blended OIT; `volumeSize>maxTexture3DSize` → halve res or tile. This preserves OCP for Vulkan/Metal ports (new `IRHIContext` impl, zero `render/` edits) and for future hardware that lacks geometry shaders/SSBO image atomics (see `broker.md` §11.6 EOL-1, §13 Q31/Q32).
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
- **EOL sustainment (V3 design-to-EOL — web-verified):** The V3 redesign is the **last architectural break** — it must sustain to EOL without another rewrite (see §11.6 table; Adept multi-GPU RHI threading + Qt QRhi cross-API + O3DE Frame Scheduler). Contracts that guarantee this: (a) `core/rhi/` abstraction (`IRHIContext{ITexture (stretch — T10),IBuffer,IFramebuffer,IShader,capabilities(),blit,memoryBarrier}` — new graphics API = new impl, zero `render` edits; shader via `IRHIShaderDesc{stages,SPIRV|GLSL,defines}` so Vulkan ingests SPIR-V — Qt QRhi `QShader`); (b) `IMapper`/`IViewBridge` + `TranslateContext{ViewContext,optional<VolumeContext>}` abstractions owned by `broker/` (policy owns abstraction — DIP per Oleksii Tym, Baeldung; composition root `AppContext`); (c) `CompositeKey{Version,LayoutId,ViewId,Type,Generation,ContentHash}` hierarchical `Version:LayoutId:Type:Hash` SHA-256 at load time (cache-key versioning per Software Patterns Lexicon + Dev Genius + System Overflow); (d) per-field generations + `IDirtyTracker` hybrid poll/push + `IJobExecutor` inline fallback (SRP per field, ISP per mapper, OCP via job executor — ICS SRP); (e) role-segregated `IMaterial` (`IColorMaterial`/`IVolumeMaterial`/`ILineMaterial`) + `ILight` (`Directional/Point/Spot`) variant visitor + `VolumePresentation{mat+TF}` ISP/OCP (lsp Rectangle-Square fix; variant vs virtuals Here Be Braces); (f) type-erased `IRenderable` (`ITypeErasedDraw` virtual, not `std::function` — avoids allocation, OCP for `PointCloudRenderer`); (g) `DrawContext{Viewport,ClearColor,Depth,Blend,spy}` per `FrameContext` instance (SRP via instance, not global static `invalidateDrawCache`); (h) `Layout::resolve(framebufferSize,contentScale)` physical pixels + `contentScale` (HiDPI DPR via `glfwGetFramebufferSize`/`glfwGetWindowContentScale` — web.dev high-dpi + GLFW #1857); (i) serialization `SceneStore::serialize()/deserialize()` JSON+binary + `SceneMigrator{Version→Version}` chain + `Arc<AssetData>` copy-on-write for undo/page branching (DCS Data Contracts 2026). Each contract is audited in `tools/audit.rules` (`broker_per_type`, `forbid_outside core/rhi/gl|`, `asset_indirection`, `no_dump_sync`, `disposition_scene/render`) + N>=3 readback green runs. See `open_questions.md` §13.8 for the 8 EOL questions (now with binding recommendations for Q31-36/38; Q37 threading contract binding, see above).