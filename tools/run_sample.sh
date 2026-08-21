#!/usr/bin/env bash
set -euo pipefail

# =============================================================================
# RenderEngine run_sample.sh <name> — build + run one sample interactively.
# Requires a display (WSLg on WSL). Each sample exits after RE_SAMPLE_MAX_FRAMES
# frames (default 300).
# =============================================================================

cd "$(dirname "$0")/.."

NAME="${1:-}"
case "$NAME" in
    mesh|plane|volume|slice|oit|mpr) ;;
    *)
        echo "usage: tools/run_sample.sh <name>" >&2
        echo "valid names: mesh plane volume slice oit mpr" >&2
        exit 2
        ;;
esac

# shellcheck disable=SC1091
source tools/env.sh

cmake -S . -B build
cmake --build build --target "re_sample_${NAME}" -j"$(nproc)"

exec "build/app/re_sample_${NAME}"