#!/usr/bin/env bash
set -euo pipefail

# =============================================================================
# loop-framework: run_task.sh (generic sequential-task supervisor, v2)
# =============================================================================
# Executes task T<N> from TASKS.md in two FRESH sessions (one task per gate):
#   Session 1 (implementer): writes code + tests + docs; build clean and the FULL
#     suite green; leaves ALL changes UNCOMMITTED. Must NOT commit.
#   Runner gate 1: rebuild + full suite green on the uncommitted tree.
#   Session 2 (reviewer): fresh session, same task description + project rules;
#     reviews the uncommitted changes, addresses findings, re-verifies green.
#   Runner gate 2: rebuild + full suite green on the reviewed tree.
#   Runner-owned commit (review-before-commit) + push when origin exists.
#
# v2 learnings implemented here:
#   - LEASE files (tools/logs/task_<N>.lease): phase, pid, start, last_update.
#     Duplicate-launch prevention (live pid -> abort); stale lease reclaim; the
#     lease IS the state so a killed runner can resume at the exact gate.
#   - FREEZE watchdog: a background watcher kills a session whose log has been
#     silent beyond LOOP_FREEZE_MIN while the process is alive.
#   - FAILURE TAXONOMY: infra (provider/timeout/freeze) vs code vs state vs
#     escalation, with attempt caps and escalation records.
#   - PER-ROLE MODELS via tools/run_models.conf (IMPLEMENTER_MODEL/REVIEWER_MODEL);
#     precedence: RUN_MODEL env > conf file > built-in default.
#   - MECHANICAL AUDIT via tools/audit.sh (manifest-driven guardrails) at every gate.
#   - HEADLESS: never blocks on a user prompt (LOOP_HEADLESS=1 default); the
#     implementer/reviewer agents carry allow/deny permission configs.
#
# Env: RUN_TIMEOUT (seconds per session, default 7200)
#      LOOP_FREEZE_MIN (silence threshold before kill, default 30)
#      LOOP_MAX_ATTEMPTS (per task, default 4)   LOOP_HEADLESS (default 1)
#      RUN_MODEL (one-shot override for both phases)
#      RUN_ALL_FORCE=1 (run_all.sh: re-run everything)
#      LOOP_BUILD_TEST_CMD (non-CMake/Make stacks: exact build+test command)
# =============================================================================

cd "$(dirname "$0")/.."
ROOT="$(pwd)"

# Headless policy: sessions must never block on a permission prompt. Exported
# (with a user override) so the loop-lease plugin's permission audit
# (LOOP_HEADLESS=1) is active for the sessions this runner launches.
export LOOP_HEADLESS="${LOOP_HEADLESS:-1}"

# GPU/environment hook (generic): sets platform GPU backends (e.g. WSL d3d12)
# when present; strict no-op otherwise. Absence is fine.
if [ -f "$ROOT/tools/env_gpu.sh" ]; then
  # shellcheck disable=SC1091
  source "$ROOT/tools/env_gpu.sh"
fi

TASKS="$ROOT/TASKS.md"
LOGDIR="$ROOT/tools/logs"
mkdir -p "$LOGDIR"

# Task headings must match `## T<N> <title>` (space, colon, or end-of-line
# after the number). `^## T` alone would also match unrelated `## Testing` /
# `## Tools` sections, so the number is required.
MAX_TASK="$(grep -cE '^## T[0-9]+[a-z]?([ :]|$)' "$TASKS" 2>/dev/null || true)"
MAX_TASK="${MAX_TASK:-0}"
# For suffix tasks, MAX_TASK is count, but valid N are actual IDs like 3a, not just numbers
TASKS_LIST=($(grep -oE '^## T[0-9]+[a-z]?' "$TASKS" 2>/dev/null | sed 's/^## T//' | tr -d ':' || true))

# ---------------------------------------------------------------------------
# Model governance: run_models.conf is read at every invocation so edits take
# effect from the next session launch. Precedence: RUN_MODEL > conf > default.
# ---------------------------------------------------------------------------
DEFAULT_MODEL="opencode-go/muse-spark-1.2-contributor"
IMPLEMENTER_MODEL="${RUN_MODEL:-$DEFAULT_MODEL}"
REVIEWER_MODEL="${RUN_MODEL:-$DEFAULT_MODEL}"
if [ -f "$ROOT/tools/run_models.conf" ]; then
  # shellcheck disable=SC1091
  source "$ROOT/tools/run_models.conf"
  IMPLEMENTER_MODEL="${RUN_MODEL:-${IMPLEMENTER_MODEL:-$DEFAULT_MODEL}}"
  REVIEWER_MODEL="${RUN_MODEL:-${REVIEWER_MODEL:-$DEFAULT_MODEL}}"
fi

# Optional model availability check (warn-and-continue; a stale list must not
# stall the loop).
check_model() {
  local m="$1"
  if [ -n "${LOOP_SKIP_MODEL_CHECK:-}" ]; then return 0; fi
  if command -v opencode >/dev/null 2>&1 && opencode models 2>/dev/null | grep -qx "$m"; then
    return 0
  fi
  echo "[warn] model '$m' not found in 'opencode models' — continuing (set LOOP_SKIP_MODEL_CHECK=1 to silence)."
}

usage() { echo "usage: $0 <N>   (task 1..$MAX_TASK)"; }

# ---------------------------------------------------------------------------
# State helpers
# ---------------------------------------------------------------------------
lease_write() { # <phase>
  {
    echo "task=$N"
    echo "phase=$1"
    echo "pid=$$"
    echo "start=$START_EPOCH"
    echo "last_update=$(date +%s)"
  } > "$LEASE"
}

lease_clear() { rm -f "$LEASE"; }

lease_pid() { awk -F= '/^pid=/{print $2}' "$LEASE" 2>/dev/null || true; }
lease_phase() { awk -F= '/^phase=/{print $2}' "$LEASE" 2>/dev/null || true; }

is_live() {
  if kill -0 "$1" 2>/dev/null; then return 0; fi
  # EPERM ("Operation not permitted") also means the process EXISTS (just owned
  # by another user); only ESRCH means it is really gone. Capture the message
  # explicitly — a `kill -0 ... | grep` pipeline returns non-zero whenever kill
  # fails, even on EPERM (pipefail), so it cannot be used as the test itself.
  local err
  err="$(kill -0 "$1" 2>&1 || true)"
  case "$err" in
    *"not permitted"*|*"permission denied"*) return 0 ;;
  esac
  return 1
}

# Validate a state file against a known schema: every line must be KEY=value
# with KEY in an allowed set. Tampered state fails loudly (anti-hallucination).
validate_state_file() {
  local f="$1"; local allowed="$2"
  [ ! -f "$f" ] && return 0
  while IFS= read -r line; do
    [ -z "$line" ] && continue
    case "$line" in
      \#*|'') continue ;;
    esac
    key="${line%%=*}"
    case " $allowed " in
      *" $key "*) : ;;
      *) echo "[state] TAMPERED state file: $f has unknown key '$key'" >&2; return 1 ;;
    esac
  done < "$f"
}

# Failure classes (loop-protocol R14): infra = provider/timeout/freeze/crash/
# push; code = build or tests not green at a gate; state = repo-hygiene
# violations (dirty tree, premature commits, tampering, no-op sessions);
# gate = mechanical audit/guardrail failures.
record_failure() { # <class> <detail>
  {
    echo "time: $(date -u +%FT%TZ)"
    echo "stage: $1"
    echo "detail: $2"
  } > "$FAILFILE"
  rm -f "$PASSFILE"
}

bump_attempt() {
  local n=0
  [ -f "$ATTEMPTFILE" ] && n="$(cat "$ATTEMPTFILE")"
  n=$((n + 1))
  echo "$n" > "$ATTEMPTFILE"
}

escalate() { # <task> <reason>
  local ef="$LOGDIR/escalation_$1.txt"
  {
    echo "time: $(date -u +%FT%TZ)"
    echo "task: $1"
    echo "reason: $2"
    echo "attempts: $(cat "$ATTEMPTFILE" 2>/dev/null || echo '?')"
    echo "---"
    echo "The loop is STOPPED. Review the escalation, fix root cause, then"
    echo "resume with: tools/run_task.sh $1"
  } > "$ef"
  echo "ESCALATED: $1 ($2). See $ef"
}

# ---------------------------------------------------------------------------
# Gates (generic — CMake, bare Makefiles, LOOP_BUILD_TEST_CMD stacks, or not-yet-existing build)
# ---------------------------------------------------------------------------
gate_build_test() { # <where>
  local where="$1"
  # Non-CMake/Make stacks (cargo, npm, gradle, ...) set LOOP_BUILD_TEST_CMD to
  # the exact build+test command; it replaces the built-in gate entirely.
  if [ -n "${LOOP_BUILD_TEST_CMD:-}" ]; then
    echo "[$where] running LOOP_BUILD_TEST_CMD"
    if ! (cd "$ROOT" && eval "$LOOP_BUILD_TEST_CMD") >>"$CTESTLOG" 2>&1; then
      echo "[$where] BUILD/TEST FAILED (see $CTESTLOG)"; return 1
    fi
    return 0
  fi
  if [ ! -f "$ROOT/CMakeLists.txt" ] && [ ! -f "$ROOT/Makefile" ]; then
    echo "[$where] no build config yet — suite gate skipped (expected before T1)."
    return 0
  fi
  if [ -f "$ROOT/CMakeLists.txt" ]; then
    if [ ! -d "$ROOT/build" ]; then
      echo "[$where] configuring and building"
      if ! cmake -S "$ROOT" -B "$ROOT/build" >"$CTESTLOG" 2>&1; then
        echo "[$where] CONFIGURE FAILED (see $CTESTLOG)"; return 1
      fi
    fi
    echo "[$where] rebuild"
    if ! cmake --build "$ROOT/build" -j"$(nproc)" >>"$CTESTLOG" 2>&1; then
      echo "[$where] BUILD FAILED (see $CTESTLOG)"; return 1
    fi
    echo "[$where] ctest"
    if ! (cd "$ROOT/build" && ctest --output-on-failure) >>"$CTESTLOG" 2>&1; then
      echo "[$where] SUITE FAILED — failing tests:"
      grep -E '\*\*\*Failed' "$CTESTLOG" || tail -20 "$CTESTLOG"
      return 1
    fi
  elif [ -f "$ROOT/Makefile" ]; then
    echo "[$where] make"
    if ! make -C "$ROOT" -j"$(nproc)" >>"$CTESTLOG" 2>&1; then
      echo "[$where] BUILD FAILED (see $CTESTLOG)"; return 1
    fi
    echo "[$where] make test"
    if ! make -C "$ROOT" test >>"$CTESTLOG" 2>&1; then
      echo "[$where] SUITE FAILED (see $CTESTLOG)"; return 1
    fi
  fi
  return 0
}

gate_audit() { # <where> — mechanical guardrails, manifest-driven
  local where="$1"
  if [ ! -f "$ROOT/tools/audit.sh" ]; then
    echo "[$where] audit skipped (tools/audit.sh not installed)."
    return 0
  fi
  if ! "$ROOT/tools/audit.sh"; then
    echo "[$where] AUDIT FAILED (see above)"; return 1
  fi
  return 0
}

# Resolve the real process group of a session launched via `setsid ... &`.
# `setsid` forks a grandchild into a new session when the caller is already a
# process group leader, so `$!` may be a wrapper whose own group is empty. The
# actual session group is the wrapper's child (timeout -> opencode). Kill THAT.
session_pgid() { # <wrapper-pid> — echoes the pgid that holds the session tree
  local p="$1" child pg
  pg="$(ps -o pgid= -p "$p" 2>/dev/null | tr -d ' ')"
  if [ -n "$pg" ] && [ "$pg" = "$p" ]; then echo "$p"; return 0; fi
  child="$(pgrep -P "$p" 2>/dev/null | head -1)"
  if [ -n "$child" ]; then
    pg="$(ps -o pgid= -p "$child" 2>/dev/null | tr -d ' ')"
    [ -n "$pg" ] && { echo "$pg"; return 0; }
  fi
  echo "$p"
}

# ---------------------------------------------------------------------------
# Session runner with freeze watchdog
# ---------------------------------------------------------------------------
watch_freeze() { # <wrapper-pid> <logfile> <start-epoch> — kills the session if silent
  local wrapper="$1" log="$2" start="$3" pgid age now a lu found f
  local quiet="$(( ${LOOP_FREEZE_MIN:-30} * 60 ))"
  while kill -0 "$wrapper" 2>/dev/null; do
    now="$(date +%s)"
    found=0; age="$quiet"
    # PRIMARY liveness signal: the event-driven plugin lease (session_*.lease)
    # last_update, which advances on every session/tool event — NOT the raw log
    # mtime. opencode block-buffers stdout when redirected to a file, and a long
    # silent "thinking" stretch emits no tokens, so log mtime is an unreliable
    # freeze indicator and caused false kills. Only consider leases written at or
    # after this session started (the runner runs ONE session at a time, so the
    # current session's lease is the newest eligible one).
    for f in "$LOGDIR"/session_*.lease; do
      [ -e "$f" ] || continue
      lu="$(awk -F= '/^last_update=/{print $2}' "$f" | head -1)"
      [ -n "$lu" ] && [ "$lu" -ge "$start" ] 2>/dev/null || continue
      found=1; a=$(( now - lu )); [ "$a" -lt "$age" ] && age="$a"
    done
    # FALLBACK: no lease available (plugin inactive/not installed) — use log mtime.
    if [ "$found" -eq 0 ] && [ -f "$log" ]; then
      age=$(( now - $(stat -c %Y "$log") ))
    fi
    if [ "$age" -gt "$quiet" ]; then
      pgid="$(session_pgid "$wrapper")"
      echo "[freeze] session silent ${age}s (>${quiet}s) — killing process group $pgid"
      kill -TERM -- "-$pgid" 2>/dev/null
      sleep 5
      kill -9 -- "-$pgid" 2>/dev/null
      kill -9 "$wrapper" 2>/dev/null
      return 0
    fi
    sleep 60
  done
}

run_session() { # <phase-label> <logfile> <promptfile> <model> [agent]
  local label="$1" log="$2" prompt="$3" model="$4" agent="${5:-}" rc
  local agentargs=()
  check_model "$model"
  # When a loop agent is installed (mode: all), pass it so its mechanical
  # allow/deny permission config actually gates this session. LOOP_DISABLE_AGENTS=1
  # falls back to prompt-only sessions (rules embedded, not mechanically enforced).
  if [ -n "$agent" ] && [ "${LOOP_DISABLE_AGENTS:-0}" != "1" ]; then
    agentargs=(--agent "$agent")
  fi
  # Optional model variant (reasoning effort, e.g. low|high|max) from
  # tools/run_models.conf MODEL_VARIANT (or RUN_MODEL_VARIANT env override).
  local variant="${RUN_MODEL_VARIANT:-${MODEL_VARIANT:-}}"
  [ -n "$variant" ] && agentargs+=(--variant "$variant")
  echo "Starting fresh session: $TITLE — $label ($model${variant:+/$variant}${agent:+ / $agent})"
  set +e
  local sess_start sess_pid tail_pid
  sess_start="$(date +%s)"
  # Run the session in its own process group (setsid) so the freeze watchdog can
  # kill the whole tree (timeout + opencode) on a hang. `$!` must stay the setsid
  # wrapper PID (no output pipe), because wait + session_pgid depend on it.
  setsid timeout "${RUN_TIMEOUT:-7200}" opencode run "${agentargs[@]}" --model "$model" \
    --title "$TITLE — $label" "$(cat "$prompt")" > "$log" 2>&1 &
  sess_pid=$!
  # When stdout is a real terminal (foreground run) mirror the session log LIVE
  # so the operator sees progress; `--pid` makes tail exit as soon as the session
  # dies, so it never lingers and never holds a pipe (it only runs on a TTY).
  # Backgrounded or piped runs keep file-only logging (run_all.sh already tees to
  # its own log).
  if [ -t 1 ]; then
    tail -f --pid="$sess_pid" "$log" 2>/dev/null &
    tail_pid=$!
  else
    tail_pid=""
  fi
  # The watchdog must not inherit the runner's stdout/stderr: its `sleep 60`
  # subprocess can outlive the killed watchdog and keep the pipe write end
  # open, hanging any `run_all.sh | tee | grep` for up to 60s. Point it at the
  # session log instead (freeze messages land there as diagnostics).
  watch_freeze "$sess_pid" "$log" "$sess_start" >> "$log" 2>&1 &
  local watch_pid=$!
  wait "$sess_pid"; rc=$?
  kill "$watch_pid" 2>/dev/null; wait "$watch_pid" 2>/dev/null
  [ -n "$tail_pid" ] && { kill "$tail_pid" 2>/dev/null; wait "$tail_pid" 2>/dev/null; }
  set -e
  rm -f "$prompt"
  return "$rc"
}

session_failed() { # <rc> <phase-label>
  local rc="$1" phase="$2"
  if [ "$rc" -eq 124 ]; then
    record_failure infra "$phase session timed out after ${RUN_TIMEOUT:-7200}s"
    echo "FAILED: $phase session timed out (set RUN_TIMEOUT). Logs: $SESSIONLOG / $REVIEWLOG"
  else
    record_failure infra "$phase session exited with code $rc"
    echo "FAILED: $phase session exited with code $rc. Logs: $SESSIONLOG / $REVIEWLOG"
  fi
  exit 1
}

# ---------------------------------------------------------------------------
# Argument handling
# ---------------------------------------------------------------------------
if ! command -v opencode >/dev/null 2>&1; then
  echo "error: 'opencode' CLI not found on PATH"; exit 1
fi
if [ "$#" -ne 1 ]; then usage; exit 2; fi
N="$1"
case "$N" in
  *[!0-9a-z]*) echo "error: task id must be numeric or numeric+suffix like 3a"; usage; exit 2 ;;
esac
# Handle both numeric and suffix IDs like 3a — check against TASKS_LIST, fallback to numeric range for backwards compat
if [[ ! " ${TASKS_LIST[*]} " =~ " $N " ]]; then
  if [ "$N" -lt 1 ] || [ "$N" -gt "$MAX_TASK" ] 2>/dev/null; then
    echo "error: task number out of range (1..$MAX_TASK) or unknown id '$N' (valid: ${TASKS_LIST[*]})"; usage; exit 2
  fi
fi

FAILFILE="$LOGDIR/task_$N.last_failure"
PASSFILE="$LOGDIR/task_$N.pass"
ATTEMPTFILE="$LOGDIR/task_$N.attempt"
LEASE="$LOGDIR/task_$N.lease"
SESSIONLOG="$LOGDIR/task_$N.log"
REVIEWLOG="$LOGDIR/task_$N.review"
CTESTLOG="$LOGDIR/task_$N.gate.log"
START_EPOCH="$(date +%s)"

# Already passed?
if [ -f "$PASSFILE" ]; then
  echo "T$N already passed — skipping (RUN_ALL_FORCE=1 to re-run)."
  exit 0
fi

# --- Duplicate-launch prevention / stale-lease reclaim / resume entry point ---
ENTRY="start"
if [ -f "$LEASE" ]; then
  if ! validate_state_file "$LEASE" "task phase pid start last_update"; then
    record_failure state "tampered lease file"
    echo "FAILED: lease file tampered. Inspect and remove $LEASE, then re-run."; exit 1
  fi
  LPID="$(lease_pid)"
  if [ -n "$LPID" ] && is_live "$LPID" && [ "$LPID" != "$$" ]; then
    echo "DUPLICATE: task T$N already running under pid $LPID (phase=$(lease_phase)). Aborting."
    exit 3
  fi
  echo "Stale lease for T$N (pid ${LPID:-unknown} dead) — reclaiming and resuming at phase '$(lease_phase)'."
  ENTRY="$(lease_phase)"
fi

# --- Attempt cap / escalation ---
# A fresh start OR a genuine retry after a recorded failure consumes an attempt.
# A pure resume after a kill/crash (stale lease, no failure recorded) must NOT
# burn the retry budget, so the same task isn't escalated just because it was
# relaunched. With infra failures (timeout/freeze) the failure file IS written,
# so each fresh retry still counts toward the cap.
if [ "$ENTRY" = "start" ] || [ -f "$FAILFILE" ]; then
  bump_attempt
  if [ -f "$ATTEMPTFILE" ] && [ "$(cat "$ATTEMPTFILE")" -gt "${LOOP_MAX_ATTEMPTS:-4}" ]; then
    escalate "$N" "exceeded ${LOOP_MAX_ATTEMPTS:-4} attempts"
    exit 4
  fi
fi

# --- R2 pre-gate: prior suite rebuilt + green; clean tree on fresh start ---
if [ "$ENTRY" = "start" ]; then
  if ! gate_build_test pre; then
    record_failure code "prior suite not green (build or tests failed)"
    echo "R2 gate FAILED: fix the tree before starting task T$N."; exit 1
  fi
  if ! gate_audit pre; then
    record_failure gate "mechanical audit failed"
    echo "Audit gate FAILED before T$N."; exit 1
  fi
  if ! git rev-parse --verify --quiet HEAD >/dev/null 2>&1; then
    echo "R2 pre-gate: repository has no commits yet (expected before T1) — clean-tree check skipped."
  elif [ ! -f "$FAILFILE" ] && [ -n "$(git status --porcelain -- . ':(exclude)tools/logs')" ]; then
    record_failure state "dirty working tree on a fresh start (no previous failure recorded)"
    echo "R2 gate FAILED: working tree is dirty:"; git status --porcelain -- . ':(exclude)tools/logs' | head -20
    echo "Commit or revert leftovers before starting task T$N."; exit 1
  fi
fi

# --- Assemble per-task prompts: project preamble + task block + generic rules ---
HEADER="$(grep -m1 -E "^## T$N([ :]|$)" "$TASKS")"
if [ -z "$HEADER" ]; then
  echo "error: task heading '## T$N ...' not found in TASKS.md (headings must match '## T<N> <title>' with optional a/b suffix)." >&2
  exit 2
fi
TITLE="$(sed -E 's/^## T[0-9]+[a-z]?[ :]+//' <<< "$HEADER")"
case "$TITLE" in '## T'*) TITLE="T$N" ;; esac
ORIGIN="$(git remote get-url origin 2>/dev/null || true)"
CORE="$(mktemp)"; IMPLPROMPT="$(mktemp)"; REVIEWPROMPT="$(mktemp)"
trap 'rm -f "$CORE" "$IMPLPROMPT" "$REVIEWPROMPT"' EXIT

{
  awk '/^## T[0-9]+[a-z]?([ :]|$)/ { exit } { print }' "$TASKS"
  echo
  echo "## Task to execute (fresh session, one task only)"
  awk -v n="$N" '
    $0 ~ ("^## T" n "([ :]|$)") { on = 1; next }
    on && ($0 ~ /^## T[0-9]+[a-z]?([ :]|$)/ || $0 ~ /^# /) { exit }
    on { print }
  ' "$TASKS"
} > "$CORE"

append_prompt() { # <dest> <implement|review>
  local dest="$1" phase="$2"
  {
    cat "$CORE"
    echo
    echo "## Loop runner constraints (binding, generic)"
    echo "- Execute ONLY task T$N. Do not start other tasks; do not skip ahead."
    echo "- AGENTS.md and the 'loop-protocol' skill (SKILL.md) are binding: load the skill"
    echo "  and follow every rule (evidence rule, regression lock, stop-and-report,"
    echo "  verification protocol, permission policy, state-file ownership, URL discipline)."
    echo "- Gate order: build clean -> unit tests green -> independent review session ->"
    echo "  review gate -> runner commit -> push. You do NOT commit; the runner owns commits."
    if [ "$phase" = implement ]; then
      echo "- IMPLEMENTATION PHASE: write the code, its unit tests, and the docs per the"
      echo "  project's documentation map. When done: build clean and FULL suite green."
      echo "- VERIFICATION PROTOCOL: after your FINAL full-suite run you may make NO further"
      echo "  edits. If you must edit, re-run the full suite again. No exceptions."
      echo "- NEVER create or modify tools/logs/ state files (.pass/.lease/.last_failure/.attempt)."
      echo "- DO NOT git commit or push. Leave everything UNCOMMITTED."
    else
      echo "- REVIEW PHASE: review the UNCOMMITTED changes with FRESH eyes against the task"
      echo "  block, AGENTS.md and the loop-protocol skill: conformance, test evidence"
      echo "  (explainable constants, never non-empty/non-black), audit compliance,"
      echo "  -Werror cleanliness, docs per the map, no weakened prior tests."
      echo "- ADDRESS EVERY FINDING directly (edit code/tests/docs), then build + full suite"
      echo "  green before finishing."
      echo "- NEVER create or modify tools/logs/ state files. DO NOT git commit or push."
    fi
    if [ -n "$ORIGIN" ]; then
      echo "- origin: $ORIGIN — the runner will git push at gate close."
    else
      echo "- Local mode: no 'origin' remote — the runner skips push with a warning."
    fi
    if [ -s "$FAILFILE" ]; then
      echo
      echo "## Previous attempt of T$N FAILED — context for this attempt"
      cat "$FAILFILE"
      [ -s "$CTESTLOG" ] && { echo "--- previous gate log tail ---"; tail -20 "$CTESTLOG"; }
      [ -s "$SESSIONLOG" ] && { echo "--- previous implementer output tail ---"; tail -30 "$SESSIONLOG" | sed -r 's/\x1B\[[0-9;]*[mK]//g'; }
      [ -s "$REVIEWLOG" ] && { echo "--- previous review output tail ---"; tail -30 "$REVIEWLOG" | sed -r 's/\x1B\[[0-9;]*[mK]//g'; }
    fi
    return 0
  } > "$dest"
}

append_prompt "$IMPLPROMPT" implement
append_prompt "$REVIEWPROMPT" review
rm -f "$CORE"

START_SHA="$(git rev-parse --verify --quiet HEAD 2>/dev/null || true)"
count_new_commits() {
  if [ -n "$START_SHA" ]; then
    git rev-list "$START_SHA..HEAD" 2>/dev/null | wc -l | tr -d ' '
  else
    git rev-list HEAD 2>/dev/null | wc -l | tr -d ' '
  fi
}

# ---------------------------------------------------------------------------
# Phase 1: implementer session + implementer gate
# ---------------------------------------------------------------------------
if [ "$ENTRY" = "start" ] || [ "$ENTRY" = "impl" ]; then
  echo "===== T$N PHASE 1/2: implementer session ====="
  lease_write impl
  RC=0
  run_session "implementation" "$SESSIONLOG" "$IMPLPROMPT" "$IMPLEMENTER_MODEL" "loop-implementer" || RC=$?
  if [ "$RC" -ne 0 ]; then session_failed "$RC" implementation; fi
  lease_write impl-gate
fi
# The gate is its own resume point: a runner killed during the gate resumes
# HERE and re-runs the checks without re-running the implementer session.
if [ "$ENTRY" = "start" ] || [ "$ENTRY" = "impl" ] || [ "$ENTRY" = "impl-gate" ]; then
  if [ "$(count_new_commits)" -ne 0 ]; then
    record_failure state "implementer committed — commits are runner-owned"
    echo "FAILED: implementer created commit(s) before the review gate."; exit 1
  fi
  if [ -z "$(git status --porcelain)" ]; then
    record_failure state "implementer left the working tree unchanged — nothing to review"
    echo "FAILED: no changes in the working tree after the implementer session."; exit 1
  fi
  if ! gate_build_test impl; then
    record_failure code "not green after implementation"
    echo "FAILED: post-implementation suite not green."; exit 1
  fi
  if ! gate_audit impl; then
    record_failure gate "audit failed after implementation"
    echo "FAILED: audit violations after the implementer session."; exit 1
  fi
  echo "Implementer gate passed: build + full suite green on the uncommitted tree."
fi

# ---------------------------------------------------------------------------
# Phase 2: reviewer session + review gate
# ---------------------------------------------------------------------------
if [ "$ENTRY" = "start" ] || [ "$ENTRY" = "impl" ] || [ "$ENTRY" = "impl-gate" ] || [ "$ENTRY" = "review" ]; then
  echo "===== T$N PHASE 2/2: reviewer session ====="
  lease_write review
  RC=0
  run_session "review" "$REVIEWLOG" "$REVIEWPROMPT" "$REVIEWER_MODEL" "loop-reviewer" || RC=$?
  if [ "$RC" -ne 0 ]; then session_failed "$RC" review; fi
  lease_write review-gate
fi
# Same split as phase 1: resuming from review-gate re-runs only the checks.
if [ "$ENTRY" = "start" ] || [ "$ENTRY" = "impl" ] || [ "$ENTRY" = "impl-gate" ] || [ "$ENTRY" = "review" ] || [ "$ENTRY" = "review-gate" ]; then
  if [ "$(count_new_commits)" -ne 0 ]; then
    record_failure state "reviewer committed — commits are runner-owned"
    echo "FAILED: reviewer created commit(s)."; exit 1
  fi
  if [ -z "$(git status --porcelain)" ]; then
    record_failure state "reviewer reverted the task changes — nothing to commit"
    echo "FAILED: working tree is clean after the review session."; exit 1
  fi
  if ! gate_build_test review; then
    record_failure code "suite not green after review"
    echo "FAILED: post-review suite not green."; exit 1
  fi
  if ! gate_audit review; then
    record_failure gate "audit failed after review"
    echo "FAILED: audit violations after the review session."; exit 1
  fi
  echo "Review gate passed: findings addressed, build + full suite green again."
fi

# ---------------------------------------------------------------------------
# Phase 3: runner-owned commit (review-before-commit)
# ---------------------------------------------------------------------------
if [ "$ENTRY" = "start" ] || [ "$ENTRY" = "impl" ] || [ "$ENTRY" = "impl-gate" ] || [ "$ENTRY" = "review" ] || [ "$ENTRY" = "review-gate" ] || [ "$ENTRY" = "commit" ]; then
  echo "Committing T$N (post-review)..."
  lease_write commit
  # tools/logs is runner-owned state — never commit it. It is gitignored by
  # install.sh; the reset below is a safety net for projects that removed the
  # ignore (git add -A would otherwise stage the untracked state files).
  if ! git add -A -- .; then
    record_failure state "git add failed before the task commit"; exit 1
  fi
  git reset -q -- tools/logs 2>/dev/null || true
  # Idempotent across resumes: if the task commit already exists (we were killed
  # after committing but before push/pass), nothing is staged — skip the commit
  # instead of failing on "nothing to commit".
  if [ -z "$(git diff --cached --name-only)" ]; then
    echo "T$N already committed (nothing staged) — resuming post-commit."
  else
    if ! git commit -q -m "T$N: $TITLE"; then
      record_failure state "task commit failed (T$N: $TITLE)"
      echo "FAILED: task commit failed. Re-run tools/run_task.sh $N."; exit 1
    fi
    if [ "$(count_new_commits)" -eq 0 ]; then
      record_failure state "no commit was created after the review gate"; exit 1
    fi
    echo "Runner created the T$N commit after the review gate."
  fi

  if ! gate_build_test post; then
    record_failure code "suite not green on the committed tree"; exit 1
  fi
  if ! gate_audit post; then
    record_failure gate "audit failed on the committed tree"; exit 1
  fi

  if [ -n "$ORIGIN" ]; then
    echo "Pushing to origin ($ORIGIN)..."
    if ! git push -u origin HEAD; then
      record_failure infra "git push failed"
      echo "FAILED: push failed. Commit exists locally; check the remote."; exit 1
    fi
  else
    echo "Local mode: no origin remote — push skipped."
  fi
fi

rm -f "$FAILFILE" "$ATTEMPTFILE"
touch "$PASSFILE"
lease_clear
echo "===== GATE PASSED for T$N: $TITLE ====="
