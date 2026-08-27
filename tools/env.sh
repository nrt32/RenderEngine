#!/usr/bin/env bash
# RenderEngine loop environment (SPEC §8). Source before launching the loop:
#   source tools/env.sh && tools/run_task.sh <N>
# Kept project-local; nothing is written to the user's shell profile.

export AUDIT_SOURCE_DIRS="io data volume scene core broker render app utils test_utils tests"
# test_utils peer lib present from start (empty until T18) — disposition + gpu ownership allow test_utils façade (audits harmless pre-existing)
# Leak-gate driver is llvmpipe, not d3d12 — attribution is stable on llvmpipe (SPEC §8 leak-gate note, env.md:88).
# These exports are gate env; do not override — running on d3d12 yields nondeterministic LSAN.
export MESA_GL_VERSION_OVERRIDE=4.6
export GALLIUM_DRIVER=llvmpipe
# Sanitizer suppressions for llvmpipe/d3d12 false positives (SPEC §5, docs/spec/nfr.md:16-19, docs/spec/env.md:30).
# `ASAN_OPTIONS`/`LSAN_OPTIONS` mirror `docs/spec/env.md` + `tools/lsan.supp` when present; kept here so manual `source tools/env.sh` matches gate env.
export ASAN_OPTIONS="detect_leaks=1:abort_on_error=1:detect_invalid_pointer_pairs=1"
if [ -f "tools/lsan.supp" ]; then
  export LSAN_OPTIONS="suppressions=$(pwd)/tools/lsan.supp:print_suppressions=0"
else
  export LSAN_OPTIONS="print_suppressions=0"
fi
# `tools/configure.sh` consumes `RE_ENABLE_SANITIZERS` (ON for Debug via `option(RE_ENABLE_SANITIZERS)`, OFF for Release) — no per-target `-fsanitize` (audit `no_per_target_sanitize`).
# tools/configure.sh skips cmake when nothing changed and wires ccache in as the
# compiler launcher, so loop gates rebuild incrementally from cache.
export LOOP_BUILD_TEST_CMD="tools/configure.sh && cmake --build build -j\$(nproc) && ctest --test-dir build --output-on-failure"