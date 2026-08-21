#!/usr/bin/env bash
set -euo pipefail

# =============================================================================
# RenderEngine configure.sh — run the CMake configure step ONLY when needed.
# Skips cmake entirely when build/CMakeCache.txt exists and no CMakeLists.txt
# or *.cmake file is newer than it, so every build stays incremental/cached.
# Wires ccache in as the C/C++ compiler launcher when ccache is on PATH; the
# launcher is persisted in CMakeCache.txt, so later configures keep it even
# without the flag.
# =============================================================================

cd "$(dirname "$0")/.."

LAUNCHER_FLAGS=()
if command -v ccache >/dev/null 2>&1; then
    LAUNCHER_FLAGS=(
        -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
        -DCMAKE_C_COMPILER_LAUNCHER=ccache
    )
fi

# Compare against a stamp we own: CMake only rewrites CMakeCache.txt when its
# CONTENT changes, so its mtime can lag behind (e.g. `git checkout` touches a
# CMakeLists.txt), and touching CMakeCache.txt ourselves would make
# `cmake --build` re-run its internal regenerate check (16s+). The stamp keeps
# the skip accurate without perturbing cmake's own bookkeeping.
STAMP=build/.re_configured_stamp

NEEDS_CONFIG=0
if [ ! -f build/CMakeCache.txt ] || [ ! -f "$STAMP" ]; then
    NEEDS_CONFIG=1
elif [ -n "$(find . -path ./build -prune -o -path ./.git -prune -o \
    -type f \( -name CMakeLists.txt -o -name '*.cmake' \) \
    -newer "$STAMP" -print -quit)" ]; then
    NEEDS_CONFIG=1
fi

if [ "$NEEDS_CONFIG" -eq 1 ]; then
    cmake -S . -B build "${LAUNCHER_FLAGS[@]}"
    touch "$STAMP"
fi
