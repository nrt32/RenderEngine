#!/usr/bin/env bash
set -euo pipefail

# =============================================================================
# RenderEngine test.sh — configure, build, run the full suite headless.
# Equivalent to eval "$LOOP_BUILD_TEST_CMD" (SPEC S8): conditional configure
# (tools/configure.sh) + incremental cmake --build + ctest. Samples also build
# (RE_BUILD_TESTS pulls them in for the T12/T13 gates).
# =============================================================================

cd "$(dirname "$0")/.."

# shellcheck disable=SC1091
source tools/env.sh

tools/configure.sh
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure