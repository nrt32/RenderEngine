#!/usr/bin/env bash
# tools/generate_font_atlas.sh — deterministic font atlas golden generation for T3a (iteration 1 #14)
# Generated in-repo at T3a, not fetched at setup — via SampleHarness headless FBO capture.
# This script is the fetch method for data/fixtures/font_atlas_golden.rgba (project-owned, deterministic,
# byte-identical on re-run). No external network, no pip deps — uses the built re_tests binary.
# Usage: ./tools/generate_font_atlas.sh
# Output: data/fixtures/font_atlas_golden.rgba + sha256sum pinned in T3a gate (TBD_T3a placeholder filled at T3a).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build"
FIXTURE="$ROOT/data/fixtures/font_atlas_golden.rgba"
mkdir -p "$(dirname "$FIXTURE")"
# Headless FBO capture via SampleHarness (requires built tests with OffscreenContext)
RE_SAMPLE_MAX_FRAMES=1 "$BUILD/tests/re_tests" --gtest_filter=*FontAtlas* --gtest_break_on_failure
# The test itself writes the fixture via capture; verify deterministic sha
echo "[generate_font_atlas] fixture at $FIXTURE"
sha256sum "$FIXTURE" || echo "fixture not yet generated — T3a will create it"
# Reproducibility gate: re-running yields byte-identical SHA (asserted in T3a)
