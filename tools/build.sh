#!/usr/bin/env bash
set -euo pipefail

# =============================================================================
# RenderEngine build.sh — configure + build everything (no tests).
#   tools/build.sh              build all targets
#   tools/build.sh <target...>  pass through to cmake --build (e.g. re_sample_mesh)
# =============================================================================

cd "$(dirname "$0")/.."

# shellcheck disable=SC1091
source tools/env.sh

cmake -S . -B build
cmake --build build -j"$(nproc)" "$@"