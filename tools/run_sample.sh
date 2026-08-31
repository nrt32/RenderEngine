#!/usr/bin/env bash
set -euo pipefail

# =============================================================================
# RenderEngine run_sample.sh <name> [--long] — build + run one sample.
# Requires a display (WSLg on WSL).
#   bounded (default): SampleHarness::run(RE_SAMPLE_MAX_FRAMES) — exits after N
#                      frames (RE_SAMPLE_MAX_FRAMES env, default 300, e.g. mesh/plane/volume/slice/oit/mpr/csg/point/line)
#   long (--long):     SampleHarness::runInteractive() — runs indefinitely until window close
#                      with pan/rotate/zoom via CameraController/GlfwCameraInteractor (EXCLUDE_FROM_ALL, not in ctest)
# Valid names: mesh plane volume slice oit mpr csg point line (+ _long suffix or --long flag)
# =============================================================================

cd "$(dirname "$0")/.."

NAME="${1:-}"
LONG_FLAG="${2:-}"
# Normalize: allow "csg_long" or "csg --long" both
if [[ "$NAME" == *_long ]]; then
    LONG_FLAG="--long"
    NAME="${NAME%_long}"
fi
case "$NAME" in
    mesh|plane|volume|slice|oit|mpr|csg|point|line) ;;
    *)
        echo "usage: tools/run_sample.sh <name> [--long]" >&2
        echo "valid names: mesh plane volume slice oit mpr csg point line" >&2
        echo "  bounded: tools/run_sample.sh csg        # RE_SAMPLE_MAX_FRAMES=300 (default, exits after N frames)" >&2
        echo "  long:    tools/run_sample.sh csg --long # runInteractive until window close (indefinite)" >&2
        exit 2
        ;;
esac

# shellcheck disable=SC1091
source tools/env.sh

tools/configure.sh
if [[ "$LONG_FLAG" == "--long" ]]; then
    cmake --build build --target "re_sample_${NAME}_long" -j"$(nproc)"
    exec "build/app/re_sample_${NAME}_long"
else
    cmake --build build --target "re_sample_${NAME}" -j"$(nproc)"
    exec "build/app/re_sample_${NAME}"
fi