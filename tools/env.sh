#!/usr/bin/env bash
# RenderEngine loop environment (SPEC S8). Source before launching the loop:
#   source tools/env.sh && tools/run_task.sh <N>
# Kept project-local; nothing is written to the user's shell profile.

export AUDIT_SOURCE_DIRS="io data volume scene core broker render app utils tests"
# tools/configure.sh skips cmake when nothing changed and wires ccache in as the
# compiler launcher, so loop gates rebuild incrementally from cache.
export LOOP_BUILD_TEST_CMD="tools/configure.sh && cmake --build build -j\$(nproc) && ctest --test-dir build --output-on-failure"