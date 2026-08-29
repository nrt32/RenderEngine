#!/usr/bin/env bash
set -euo pipefail

# =============================================================================
# loop-framework: run_all.sh (generic sequential runner, v2)
# =============================================================================
# Runs every task in TASKS.md strictly in order, each in its own fresh session.
# - Skips tasks with a tools/logs/task_<N>.pass marker (RUN_ALL_FORCE=1 re-runs).
# - Stops at the first failed gate or an escalation (attempt cap exceeded).
# - Tolerates being killed and resumed: run_task.sh reads its own lease and
#   resumes at the exact gate, so restarting this script never redoes gates.
# - Writes a combined progress log to tools/logs/run_all.out.
# =============================================================================

cd "$(dirname "$0")/.."

if [ -f "tools/env_gpu.sh" ]; then
  # shellcheck disable=SC1091
  source "tools/env_gpu.sh"
fi

OUTLOG="tools/logs/run_all.out"
mkdir -p "tools/logs"

# Task headings must match `## T<N> <title>` (see run_task.sh) — the number is
# required so unrelated `## Testing`-style sections are not miscounted.
# Supports suffix like T3a/T11b for split tasks (V5 T8→T8a/T8b, T11→T11a/T11b, T14→T14a/b etc.)
TASKS_LIST=($(grep -oE '^## T[0-9]+[a-z]?' TASKS.md 2>/dev/null | sed 's/^## T//' | tr -d ':' || true))
MAX_TASK=${#TASKS_LIST[@]}
echo "===== run_all: $MAX_TASK tasks (${TASKS_LIST[*]}) =====" >> "$OUTLOG"

for n in "${TASKS_LIST[@]}"; do
  if [ "${RUN_ALL_FORCE:-0}" != "1" ] && [ -f "tools/logs/task_$n.pass" ]; then
    echo "===== T$n already passed — skipping (RUN_ALL_FORCE=1 to re-run) =====" | tee -a "$OUTLOG"
    continue
  fi
  echo "===== Task T$n of $MAX_TASK =====" | tee -a "$OUTLOG"
  tools/run_task.sh "$n" 2>&1 | tee -a "$OUTLOG" || {
    echo "FAILED at task T$n — sequence stopped." | tee -a "$OUTLOG"
    if [ -f "tools/logs/escalation_$n.txt" ]; then
      echo "Task T$n ESCALATED — inspect tools/logs/escalation_$n.txt, fix root cause," | tee -a "$OUTLOG"
      echo "then resume with: tools/run_task.sh $n" | tee -a "$OUTLOG"
    else
      echo "Fix it, then resume with: tools/run_task.sh $n" | tee -a "$OUTLOG"
    fi
    exit 1
  }
done

echo "All $MAX_TASK tasks completed, each in its own session." | tee -a "$OUTLOG"
