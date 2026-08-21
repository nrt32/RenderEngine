#!/usr/bin/env bash
set -euo pipefail

# =============================================================================
# RenderEngine clean.sh — remove only the build/ directory. Never touches the
# source tree or any other directory. (tools/force_purge.sh is the destructive
# full-teardown; this stays scoped to build/.)
#   tools/clean.sh            remove build/ (the ccache survives, so the next
#                             build recompiles from the compiler cache)
#   tools/clean.sh --ccache   also clear the ccache compiler cache + stats
# =============================================================================

cd "$(dirname "$0")/.."

rm -rf -- build

if [ "${1:-}" = "--ccache" ]; then
    if command -v ccache >/dev/null 2>&1; then
        ccache --clear >/dev/null
        ccache --zero-stats >/dev/null
        echo "build/ removed and ccache cleared."
    else
        echo "build/ removed (ccache not installed — nothing to clear)."
    fi
fi