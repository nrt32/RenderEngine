#!/usr/bin/env bash
# RenderEngine loop environment (SPEC S8). Source before launching the loop:
#   source tools/env.sh && tools/run_task.sh <N>
# Kept project-local; nothing is written to the user's shell profile.

export AUDIT_SOURCE_DIRS="io data volume core render app tests"
export LOOP_BUILD_TEST_CMD="cmake -S . -B build && cmake --build build -j\$(nproc) && ctest --test-dir build --output-on-failure"