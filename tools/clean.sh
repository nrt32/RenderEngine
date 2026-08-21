#!/usr/bin/env bash
set -euo pipefail

# =============================================================================
# RenderEngine clean.sh — remove only the build/ directory. Never touches the
# source tree or any other directory. (tools/force_purge.sh is the destructive
# full-teardown; this stays scoped to build/.)
# =============================================================================

cd "$(dirname "$0")/.."

rm -rf -- build