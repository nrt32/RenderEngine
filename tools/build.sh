#!/usr/bin/env bash
set -euo pipefail

# =============================================================================
# RenderEngine build.sh — incremental configure + build everything (no tests).
#   tools/build.sh              build all targets
#   tools/build.sh <target...>  pass through to cmake --build (e.g. re_sample_mesh)
# Configure only runs when needed (see tools/configure.sh); cmake --build is
# incremental and ccache (if installed) is wired in as the compiler launcher,
# so recompiles hit the compiler cache.
# =============================================================================

cd "$(dirname "$0")/.."

# shellcheck disable=SC1091
source tools/env.sh

tools/configure.sh

cmake --build build -j"$(nproc)" "$@"