#!/bin/sh
# ralph — autonomous task loop for the Mixxx netmix feature.
#
# Usage:
#   ./scripts/ralph.sh                          # multi-phase: loop through all open phases from the trunk
#   ./scripts/ralph.sh 1.1.1                    # single task (infers release branch)
#   ./scripts/ralph.sh --minutes=30             # loop for up to 30 minutes
#   ./scripts/ralph.sh --hours=2                # loop for up to 2 hours
#   ./scripts/ralph.sh --hours=1 --minutes=30   # loop for up to 1 h 30 m
#   ./scripts/ralph.sh --minutes=45 1.2.4       # single task, still time-bounded
#   ./scripts/ralph.sh --until=1.1.3            # run all open tasks up to and including 1.1.3, then stop
#   ./scripts/ralph.sh --dry-run                # preview the next action and exit
#   ./scripts/ralph.sh --log=/tmp/my-run.log    # log everything to a file
#   ./scripts/ralph.sh --quiet                  # suppress agent stderr (default: verbose)
#
# Ralph can be started from:
#   feat/rollback-network-mixing (the TRUNK) — picks up the next open phase
#                    and loops through all phases.
#   release/X.Y    — resumes that phase, then continues through subsequent
#                    phases automatically after each one merges to the trunk.
#
# Upstream master is NEVER touched by ralph.
#
# Stopping ralph gracefully (it will finish the current task first):
#   touch scripts/STOP.md               # drop a sentinel file in the repo
#   kill -TERM $(cat /tmp/ralph.pid)    # or send SIGTERM to the process
#   Ctrl-C                              # or SIGINT from the terminal
#
# Requires: opencode CLI, cmake, git, a clean working tree.
# The human has given blanket commit+push permission while this runs.

set -e

REPO_ROOT="$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"
SKILL="$REPO_ROOT/skills/ralph.md"
LOG="$REPO_ROOT/docs/ralph-log.md"
BUILD_DIR="$REPO_ROOT/build"

# The feature trunk — ralph's "main". Upstream master stays untouched.
TRUNK="${RALPH_TRUNK:-feat/rollback-network-mixing}"

# ── Agent Configuration ──────────────────────────────────────────────────────
# Each agent is a PROVIDER/MODEL pair (e.g. "opencode-go/deepseek-v4-flash").
# Provider determines which CLI binary is invoked:
#   opencode-go    → opencode
#   kilocode       → kilo
#   github-copilot → copilot
#   claude-code    → claude
# Model is passed directly as --model to the CLI.
# All agents can be overridden at runtime via environment variables.

TASK_PLANNING_AGENT="${TASK_PLANNING_AGENT:-opencode-go/deepseek-v4-flash}"
BASIC_DEV_AGENT="${BASIC_DEV_AGENT:-opencode-go/deepseek-v4-flash}"
MID_DEV_AGENT="${MID_DEV_AGENT:-opencode-go/deepseek-v4-flash}"
PRO_DEV_AGENT="${PRO_DEV_AGENT:-opencode-go/deepseek-v4-flash}"
TASK_REVIEW_AGENT="${TASK_REVIEW_AGENT:-opencode-go/deepseek-v4-flash}"
RELEASE_REVIEW_AGENT="${RELEASE_REVIEW_AGENT:-opencode-go/deepseek-v4-flash}"
MAJOR_RELEASE_REVIEW_AGENT="${MAJOR_RELEASE_REVIEW_AGENT:-opencode-go/deepseek-v4-flash}"
ARCHITECT_AGENT="${ARCHITECT_AGENT:-opencode-go/deepseek-v4-flash}"

BASE_BRANCH="$(git -C "$REPO_ROOT" rev-parse --abbrev-ref HEAD)"

# ── colours ────────────────────────────────────────────────────────────────
BOLD=$(tput bold 2>/dev/null || true)
CYAN=$(tput setaf 6 2>/dev/null || true)
GREEN=$(tput setaf 2 2>/dev/null || true)
YELLOW=$(tput setaf 3 2>/dev/null || true)
RED=$(tput setaf 1 2>/dev/null || true)
RESET=$(tput sgr0 2>/dev/null || true)

log() { printf '%s\n' "${CYAN}[ralph]${RESET} $*"; }
good() { printf '%s\n' "${GREEN}[ralph]${RESET} $*"; }
warn() { printf '%s\n' "${YELLOW}[ralph]${RESET} $*"; }
die() {
  printf '%s\n' "${RED}[ralph]${RESET} $*" >&2
  exit 1
}

# Portable in-place sed (BSD sed on macOS needs -i '').
sed_inplace() {
  if sed --version >/dev/null 2>&1; then
    sed -i "$@"
  else
    _expr="$1"
    shift
    sed -i '' "$_expr" "$@"
  fi
}

# ── argument parsing ────────────────────────────────────────────────────────
SINGLE_TASK=""
UNTIL_TASK=""
DURATION_SECS=0
DRY_RUN=0
QUIET=0
RALPH_LOG_FILE="${RALPH_LOG_FILE:-}"

for _arg in "$@"; do
  case "$_arg" in
  --minutes=*)
    _mins="${_arg#--minutes=}"
    case "$_mins" in '' | *[!0-9]*) die "--minutes requires a positive integer" ;; esac
    DURATION_SECS=$((DURATION_SECS + _mins * 60))
    ;;
  --hours=*)
    _hrs="${_arg#--hours=}"
    case "$_hrs" in '' | *[!0-9]*) die "--hours requires a positive integer" ;; esac
    DURATION_SECS=$((DURATION_SECS + _hrs * 3600))
    ;;
  --until=*)
    UNTIL_TASK="${_arg#--until=}"
    [ -n "$UNTIL_TASK" ] || die "--until requires a task id (e.g. --until=1.1.3)"
    ;;
  --dry-run)
    DRY_RUN=1
    ;;
  --quiet | -q)
    QUIET=1
    ;;
  --log=*)
    _log="${_arg#--log=}"
    [ -n "$_log" ] || die "--log requires a file path"
    RALPH_LOG_FILE="$_log"
    ;;
  -*)
    die "Unknown flag: $_arg  (supported: --minutes=N  --hours=N  --until=TASK_ID  --dry-run  --quiet/-q  --log=FILE)"
    ;;
  *)
    [ -z "$SINGLE_TASK" ] || die "Too many positional arguments — only one task id is allowed"
    SINGLE_TASK="$_arg"
    ;;
  esac
done

START_TIME="$(date +%s)"
if [ "$DURATION_SECS" -gt 0 ]; then
  DEADLINE=$((START_TIME + DURATION_SECS))
else
  DEADLINE=0
fi

# ── session logging ─────────────────────────────────────────────────────────
if [ -n "$RALPH_LOG_FILE" ] && [ -z "${RALPH_LOGGING_ACTIVE:-}" ]; then
  export RALPH_LOGGING_ACTIVE=1
  printf '%s\n' "${CYAN}[ralph]${RESET} Logging session to ${RALPH_LOG_FILE}"
  { "$0" "$@"; } 2>&1 | tee -a "$RALPH_LOG_FILE"
  exit $?
fi

# ── sanity checks ───────────────────────────────────────────────────────────
[ -f "$SKILL" ] || die "skill file missing: $SKILL"
command -v opencode >/dev/null 2>&1 || die "opencode CLI not found in PATH — install from https://github.com/opencode-ai/opencode"
command -v cmake >/dev/null 2>&1 || die "cmake not found in PATH"

git show-ref --verify --quiet "refs/heads/${TRUNK}" 2>/dev/null ||
  die "trunk branch '${TRUNK}' does not exist — create it first or set RALPH_TRUNK"

case "$BASE_BRANCH" in
"$TRUNK")
  MINOR_VERSION="" # resolved per-phase at runtime
  ;;
release/*)
  MINOR_VERSION="${BASE_BRANCH#release/}"
  case "$MINOR_VERSION" in
  *[!0-9.]* | "") die "release branch '${BASE_BRANCH}' has an invalid minor version '${MINOR_VERSION}'" ;;
  esac
  ;;
*)
  die "ralph must be started from '${TRUNK}' or a 'release/X.Y' branch, not '${BASE_BRANCH}'.\n" \
    "  From the trunk (multi-phase — recommended for unattended runs):\n" \
    "    git checkout ${TRUNK} && ./scripts/ralph.sh\n" \
    "  From a specific release branch (single-phase):\n" \
    "    git checkout release/1.1 && ./scripts/ralph.sh"
  ;;
esac

if [ -n "$SINGLE_TASK" ] && [ "$BASE_BRANCH" != "$TRUNK" ]; then
  TASK_MINOR="$(printf '%s' "$SINGLE_TASK" | sed 's/\.[0-9]*$//')"
  [ "$TASK_MINOR" = "$MINOR_VERSION" ] ||
    die "Task ${SINGLE_TASK} (minor: ${TASK_MINOR}) does not belong to ${BASE_BRANCH} (minor: ${MINOR_VERSION})."
fi

if [ "$DRY_RUN" -eq 1 ]; then
  log "Dry run: no changes will be made."
  if [ -n "$SINGLE_TASK" ]; then
    if [ "$BASE_BRANCH" = "$TRUNK" ]; then
      _task_minor="$(printf '%s' "$SINGLE_TASK" | sed 's/\.[0-9]*$//')"
      log "Would switch to release/${_task_minor} and run task ${SINGLE_TASK} there."
    else
      log "Would run task ${SINGLE_TASK} on ${BASE_BRANCH}."
    fi
    exit 0
  fi
  if [ "$BASE_BRANCH" = "$TRUNK" ]; then
    log "Would switch to the next open release branch and start its first task."
    exit 0
  fi
  if [ "${MINOR_VERSION}" != "${MINOR_VERSION%.0}" ]; then
    log "Would create rc/${MINOR_VERSION}.0.0-rc.1 and stop for human sign-off."
  else
    log "Would run the next task or phase review for ${MINOR_VERSION}."
  fi
  exit 0
fi

cd "$REPO_ROOT"

if ! git diff --quiet || ! git diff --cached --quiet || [ -n "$(git ls-files --others --exclude-standard 2>/dev/null)" ]; then
  _dirty_files="$( (git diff --name-only; git diff --cached --name-only; git ls-files --others --exclude-standard) 2>/dev/null | sort -u | grep -v '^$' || true)"
  _non_log="$(printf '%s' "$_dirty_files" | grep -v '^docs/ralph-log.md$' || true)"
  if [ -z "$_non_log" ]; then
    log "Dirty tree from ralph-log.md — checking for unmarked completed tasks..."
    git add docs/ralph-log.md
    _last_done="$(grep -Eo 'DONE: [0-9]+\.[0-9]+\.[0-9]+' docs/ralph-log.md | tail -1 | sed 's/DONE: //')"
    if [ -n "$_last_done" ]; then
      _todo_file=""
      for _f in "$REPO_ROOT/docs"/todo-v*.md; do
        if grep -q "\`${_last_done}\`" "$_f" 2>/dev/null; then
          _todo_file="$_f"
          break
        fi
      done
      if [ -n "$_todo_file" ] && grep -q "\- \[ \] \`${_last_done}\`" "$_todo_file" 2>/dev/null; then
        log "Marking ${_last_done} as done (log says merged, checkbox stale)."
        sed_inplace "s/^- \[ \] \`${_last_done}\`/- [x] \`${_last_done}\`/" "$_todo_file"
        git add "$_todo_file"
      fi
    fi
    git commit -m "docs: ralph-log continuation checkpoint"
    git push origin "$BASE_BRANCH" 2>/dev/null || true
    good "Log checkpoint committed. Continuing."
  else
    die "working tree is dirty — commit or stash changes before running ralph"
  fi
fi

# ── build dir bootstrap ─────────────────────────────────────────────────────
ensure_build_configured() {
  if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    log "Configuring CMake build dir (one-time)..."
    cmake -S "$REPO_ROOT" -B "$BUILD_DIR" \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DQT6=ON \
      -DBUILD_TESTING=ON ||
      die "cmake configure failed — fix the environment before running ralph"
    good "Build dir configured."
  fi
}
ensure_build_configured

# ── PID file + signal / sentinel stop mechanism ─────────────────────────────
RALPH_PID_FILE="/tmp/ralph.pid"
STOP_SENTINEL="$REPO_ROOT/scripts/STOP.md"
STOP_REQUESTED=0

printf '%d\n' $$ >"$RALPH_PID_FILE"

# ── caveman mode for opencode agents ─────────────────────────────────────────
CAVEMAN_FLAG_PATH="${XDG_CONFIG_HOME:-$HOME/.config}/opencode/.caveman-active"
mkdir -p "$(dirname "$CAVEMAN_FLAG_PATH")"
printf '%s\n' 'full' >"$CAVEMAN_FLAG_PATH"

trap 'rm -f "$RALPH_PID_FILE"' EXIT
trap 'STOP_REQUESTED=1; warn "Stop signal received — will exit cleanly after the current task."' INT TERM

# ── announce ────────────────────────────────────────────────────────────────
log "PID $$ written to $RALPH_PID_FILE"
log "Trunk: ${TRUNK}"
if [ -n "$MINOR_VERSION" ]; then
  log "Mode: single-phase  branch: ${BASE_BRANCH}  phase: ${MINOR_VERSION}"
else
  log "Mode: multi-phase  starting from the trunk — will create release branches as needed"
fi
log "To stop gracefully:  kill -TERM \$(cat $RALPH_PID_FILE)  or  touch $STOP_SENTINEL"
log "Caveman mode: full (opencode agents will use terse response style)"

if [ "$DURATION_SECS" -gt 0 ]; then
  _human="$((DURATION_SECS / 3600))h $(((DURATION_SECS % 3600) / 60))m"
  _deadline_str="$(date -r "$DEADLINE" '+%H:%M:%S' 2>/dev/null ||
    date -d "@$DEADLINE" '+%H:%M:%S' 2>/dev/null ||
    printf 'epoch %d' "$DEADLINE")"
  log "Time limit: ${_human} (deadline ${_deadline_str})"
fi

# ── helpers ─────────────────────────────────────────────────────────────────

all_todo_lines() {
  for _f in "$REPO_ROOT/docs"/todo-v*.md; do
    [ -f "$_f" ] && cat "$_f" || true
  done
}

task_for_minor() {
  _minor="$1"
  all_todo_lines | grep -m1 "^\- \[ \] \`${_minor}\.[0-9]\{1,\}\`" |
    sed "s/^- \[ \] \`\([^\`]*\)\`.*/\1/" || true
}

# ── agent helpers ───────────────────────────────────────────────────────────

agent_provider() {
  printf '%s' "$1" | sed 's|/.*||'
}

agent_model() {
  printf '%s' "$1" | sed 's|^[^/]*/||'
}

agent_cli() {
  case "$(agent_provider "$1")" in
  opencode-go) printf 'opencode' ;;
  kilocode) printf 'kilo' ;;
  github-copilot) printf 'copilot' ;;
  claude-code) printf 'claude' ;;
  *) printf 'opencode' ;;
  esac
}

# Default agent timeout (seconds). C++ builds are slow — give agents room.
AGENT_TIMEOUT="${RALPH_AGENT_TIMEOUT:-1800}"

invoke_agent() {
  _agent="$1"
  shift
  _prompt="$1"
  shift
  _cli="$(agent_cli "$_agent")"
  _model="$(agent_model "$_agent")"

  _pf="$(mktemp)"
  printf '%s' "$_prompt" >"$_pf"

  _timeout_cmd=""
  if command -v timeout >/dev/null 2>&1; then
    _timeout_cmd="timeout --foreground --kill-after=30s ${AGENT_TIMEOUT}s"
  elif command -v gtimeout >/dev/null 2>&1; then
    _timeout_cmd="gtimeout --foreground --kill-after=30s ${AGENT_TIMEOUT}s"
  fi

  # claude and copilot: exec directly so shell-metacharacters in prompt text
  # don't cause eval errors.
  if [ "$_cli" = "claude" ] || [ "$_cli" = "copilot" ]; then
    if [ "$_cli" = "claude" ]; then
      _extra="--dangerously-skip-permissions"
    else
      _extra="--allow-all-tools"
    fi
    _prompt_text="$(cat "$_pf")"
    rm -f "$_pf"
    # shellcheck disable=SC2086
    if [ "$QUIET" -eq 1 ]; then
      $_timeout_cmd "$_cli" -p "$_prompt_text" --model "$_model" $_extra "$@" 2>/dev/null
    else
      $_timeout_cmd "$_cli" -p "$_prompt_text" --model "$_model" $_extra "$@"
    fi
    return $?
  fi

  # opencode and others: prompt stays in file, piped via stdin.
  # shellcheck disable=SC2124
  _cmd="cat \"$_pf\" | \"$_cli\" run --model \"$_agent\" \"$@\""
  if [ "$QUIET" -eq 1 ]; then
    _cmd="${_cmd} 2>/dev/null"
  fi
  if [ -n "$_timeout_cmd" ]; then
    _cmd="$_timeout_cmd ${_cmd}"
  fi
  eval "$_cmd"
  _rc=$?
  rm -f "$_pf"
  return $_rc
}

resolve_dev_agent() {
  _task_block="$1"

  _agent=$(printf '%s' "$_task_block" | grep -im1 'Agent:' | sed 's/.*Agent:[[:space:]]*//' | xargs)
  if [ -n "$_agent" ]; then
    case "$_agent" in
    task_planning_agent | TASK_PLANNING_AGENT) printf '%s' "$TASK_PLANNING_AGENT" ;;
    basic_dev_agent | BASIC_DEV_AGENT) printf '%s' "$BASIC_DEV_AGENT" ;;
    mid_dev_agent | MID_DEV_AGENT) printf '%s' "$MID_DEV_AGENT" ;;
    pro_dev_agent | PRO_DEV_AGENT) printf '%s' "$PRO_DEV_AGENT" ;;
    task_review_agent | TASK_REVIEW_AGENT) printf '%s' "$TASK_REVIEW_AGENT" ;;
    release_review_agent | RELEASE_REVIEW_AGENT) printf '%s' "$RELEASE_REVIEW_AGENT" ;;
    major_release_review_agent | MAJOR_RELEASE_REVIEW_AGENT) printf '%s' "$MAJOR_RELEASE_REVIEW_AGENT" ;;
    architect_agent | ARCHITECT_AGENT) printf '%s' "$ARCHITECT_AGENT" ;;
    *) printf '%s' "$MID_DEV_AGENT" ;;
    esac
    return
  fi

  _model=$(printf '%s' "$_task_block" | grep -im1 'Model:' | sed 's/.*Model:[[:space:]]*//' | xargs)
  _difficulty=$(printf '%s' "$_task_block" | grep -im1 'Difficulty:' | sed 's/.*Difficulty:[[:space:]]*//' | xargs)

  case "$_model" in
  *[Ff]lagship*)
    printf '%s' "$PRO_DEV_AGENT"
    return
    ;;
  *[Hh]uman*)
    printf '%s' "$PRO_DEV_AGENT"
    return
    ;;
  Standard)
    case "$_difficulty" in
    High)
      printf '%s' "$PRO_DEV_AGENT"
      return
      ;;
    Low)
      printf '%s' "$BASIC_DEV_AGENT"
      return
      ;;
    *)
      printf '%s' "$MID_DEV_AGENT"
      return
      ;;
    esac
    ;;
  esac

  case "$_difficulty" in
  Low)
    printf '%s' "$BASIC_DEV_AGENT"
    return
    ;;
  Medium)
    printf '%s' "$MID_DEV_AGENT"
    return
    ;;
  High)
    printf '%s' "$PRO_DEV_AGENT"
    return
    ;;
  esac

  printf '%s' "$MID_DEV_AGENT"
}

next_task() {
  task_for_minor "$MINOR_VERSION"
}

next_minor() {
  all_todo_lines | grep -m1 "^\- \[ \] \`[0-9]\{1,\}\.[0-9]\{1,\}\.[0-9]\{1,\}\`" |
    sed "s/^- \[ \] \`\([^\`]*\)\`.*/\1/" |
    sed 's/\.[0-9]*$//' || true
}

switch_to_phase() {
  _minor="$1"
  _branch="release/${_minor}"

  if git show-ref --verify --quiet "refs/heads/${_branch}" 2>/dev/null ||
    git ls-remote --exit-code --heads origin "${_branch}" >/dev/null 2>&1; then
    log "Switching to existing ${_branch}"
    git checkout "$_branch" >/dev/null 2>&1
    git pull --ff-only origin "$_branch" >/dev/null 2>&1 ||
      warn "could not fast-forward ${_branch} from origin — continuing on local"
  else
    log "Creating ${_branch} from ${TRUNK}"
    git checkout "$TRUNK" >/dev/null 2>&1
    git pull --ff-only origin "$TRUNK" >/dev/null 2>&1 ||
      warn "could not fast-forward ${TRUNK} from origin — continuing on local"
    git checkout -b "$_branch"
    git push -u origin "$_branch" 2>/dev/null ||
      warn "could not push ${_branch} to origin — continuing on local"
    good "${_branch} created."
  fi

  BASE_BRANCH="$_branch"
  MINOR_VERSION="$_minor"
  log "Now on ${BASE_BRANCH}  (phase ${MINOR_VERSION})"
}

task_block() {
  TASK_ID="$1"
  all_todo_lines | awk -v tid="$TASK_ID" '
        BEGIN { pat = "^- \\[.\\] `" tid "`" }
        $0 ~ pat      { found=1; print; next }
        found && /^- \[.\] `[0-9]/ { exit }
        found         { print }
    '
}

ralph_log() {
  ENTRY="$1"
  mkdir -p "$(dirname "$LOG")"
  printf '\n## %s\n\n%s\n' "$(date '+%Y-%m-%d %H:%M')" "$ENTRY" >>"$LOG"
}

# ── deadline / stop helpers ──────────────────────────────────────────────────

time_remaining() {
  _now="$(date +%s)"
  _left=$((DEADLINE - _now))
  if [ "$_left" -le 0 ]; then
    printf '0s'
  else
    printf '%dh %dm %ds' $((_left / 3600)) $(((_left % 3600) / 60)) $((_left % 60))
  fi
}

deadline_reached() {
  [ "$DEADLINE" -gt 0 ] && [ "$(date +%s)" -ge "$DEADLINE" ]
}

stop_requested() {
  [ "$STOP_REQUESTED" -eq 1 ] && return 0
  if [ -f "$STOP_SENTINEL" ]; then
    warn "Stop sentinel found: $STOP_SENTINEL — consuming it."
    rm -f "$STOP_SENTINEL"
    STOP_REQUESTED=1
    return 0
  fi
  return 1
}

# ── major-release gate ──────────────────────────────────────────────────────

is_major_release() {
  case "$MINOR_VERSION" in
  *.0) return 0 ;;
  *) return 1 ;;
  esac
}

prepare_major_rc() {
  MAJOR_VER="${MINOR_VERSION%.0}"
  RC_VER="${MAJOR_VER}.0.0"
  RC_BRANCH="rc/${RC_VER}-rc.1"
  RC_TAG="netmix-${RC_VER}-rc.1"

  log "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
  log "MAJOR RELEASE — phase ${MINOR_VERSION} requires human sign-off."
  log "Auto-merge to the trunk is BLOCKED for major releases."
  log "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

  if git show-ref --verify --quiet "refs/heads/${RC_BRANCH}" 2>/dev/null; then
    warn "RC branch ${RC_BRANCH} already exists — skipping creation."
    git checkout "$RC_BRANCH" >/dev/null 2>&1
  else
    log "Creating RC branch ${RC_BRANCH} from ${BASE_BRANCH}"
    git checkout -b "$RC_BRANCH" >/dev/null 2>&1
    git push -u origin "$RC_BRANCH" 2>/dev/null || true
    good "RC branch ${RC_BRANCH} created."
  fi

  if git show-ref --verify --quiet "refs/tags/${RC_TAG}" 2>/dev/null; then
    warn "RC tag ${RC_TAG} already exists — skipping."
  else
    git tag -a "$RC_TAG" -m "Release candidate: ${RC_TAG}"
    git push origin "$RC_TAG" 2>/dev/null || true
    good "RC tag ${RC_TAG} created."
  fi

  ralph_log "MAJOR_RC_READY: ${RC_BRANCH} + tag ${RC_TAG} created from ${BASE_BRANCH}. Awaiting human sign-off before merge to ${TRUNK}."

  warn ""
  warn "  RC is ready: ${RC_TAG}  (branch: ${RC_BRANCH})"
  warn ""
  warn "  Required before merging to ${TRUNK}:"
  warn "    1. Human review and sign-off"
  warn "    2. Documented manual two-instance end-to-end session test"
  warn "    3. Human runs: git checkout ${TRUNK} && git merge --no-ff ${RC_BRANCH}"
  warn ""
  warn "  Ralph has stopped — do NOT re-run ralph to merge this release."
  exit 0
}

# ── phase completion review + auto-merge to the trunk ───────────────────────

phase_review_and_merge() {
  if is_major_release; then
    prepare_major_rc
  fi

  log "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
  log "Phase ${MINOR_VERSION} complete — running review before merging to ${TRUNK}"
  log "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

  MAX_REVIEW_ATTEMPTS=3
  REVIEW_ATTEMPT=0

  while [ $REVIEW_ATTEMPT -lt $MAX_REVIEW_ATTEMPTS ]; do
    REVIEW_ATTEMPT=$((REVIEW_ATTEMPT + 1))
    log "Phase review attempt ${REVIEW_ATTEMPT}/${MAX_REVIEW_ATTEMPTS}"

    PHASE_TASK_STATUS="$(all_todo_lines | grep "\`${MINOR_VERSION}\." | head -30 || true)"
    CHANGED_FILES="$(git diff --name-only "${TRUNK}"..."$BASE_BRANCH" 2>&1 || true)"
    COMMIT_LOG="$(git log --oneline "${TRUNK}"..."$BASE_BRANCH" 2>&1 || true)"
    REVIEW_LOG="/tmp/ralph-phase-review-${MINOR_VERSION}-${REVIEW_ATTEMPT}.log"

    invoke_agent "$RELEASE_REVIEW_AGENT" "You are performing a phase completion review for the Mixxx netmix feature.
All tasks in phase ${MINOR_VERSION} are reported complete. You have full read
access to the repository working directory. Inspect changed files and docs
directly. The feature plan is docs/plan.md; the skill file with the
architectural invariants is skills/ralph.md.

Branches: ${TRUNK} vs ${BASE_BRANCH}

Phase task status:
${PHASE_TASK_STATUS}

Commits in this phase (one per line):
${COMMIT_LOG}

Files changed in this phase:
${CHANGED_FILES}

Review checklist — for each item write PASS or FAIL and a one-line reason:
1. Every ${MINOR_VERSION}.* task in docs/todo-v*.md is marked [x] (except the phase-merge task itself).
2. No regressions: 'cmake --build build --target mixxx-test' succeeds and 'ctest --test-dir build -R Netmix --output-on-failure' passes.
3. Every new source file is registered in the root CMakeLists.txt.
4. No unrelated scope creep — only paths owned by ${MINOR_VERSION}.* tasks changed (plus CMakeLists.txt and docs).
5. The seven architectural invariants in skills/ralph.md hold (audio-callback purity, no audio on the wire, echo suppression, versioned protocol, deterministic clock math, cache safety, upstream isolation).
6. No security issues: no path traversal from network input, bounded reads, sha256 verification intact.
7. docs/memory.md has entries for any conventions this phase established.

If every item is PASS, print exactly: PHASE_APPROVED
If any item is FAIL, print exactly: PHASE_BLOCKED
Then list what must be fixed before the merge can proceed." \
      2>&1 | tee "$REVIEW_LOG"

    ln -sf "$REVIEW_LOG" "/tmp/ralph-phase-review-${MINOR_VERSION}.log" 2>/dev/null || true

    if grep -q "PHASE_APPROVED" "$REVIEW_LOG" 2>/dev/null; then
      good "Review approved the merge — merging ${BASE_BRANCH} → ${TRUNK}."
      if ! git diff --quiet docs/ralph-log.md 2>/dev/null; then
        git add docs/ralph-log.md
        git commit -m "docs: ralph-log — phase ${MINOR_VERSION} wrapped up"
        git push origin "$BASE_BRANCH" 2>/dev/null || true
      fi
      git checkout "$TRUNK"
      git pull --ff-only origin "$TRUNK" >/dev/null 2>&1 ||
        warn "could not fast-forward ${TRUNK} from origin — continuing on local"
      git merge --no-ff "$BASE_BRANCH" \
        -m "release: merge ${BASE_BRANCH} to ${TRUNK} — netmix phase ${MINOR_VERSION} complete"
      git push origin "$TRUNK" 2>/dev/null || warn "could not push ${TRUNK} — push manually later"
      good "Phase ${MINOR_VERSION} merged to ${TRUNK} successfully."
      ralph_log "PHASE_COMPLETE: ${MINOR_VERSION} merged to ${TRUNK} after review approval."
      return 0
    fi

    warn "Phase review blocked (attempt ${REVIEW_ATTEMPT}/${MAX_REVIEW_ATTEMPTS}) — see ${REVIEW_LOG}"

    if [ $REVIEW_ATTEMPT -eq $MAX_REVIEW_ATTEMPTS ]; then
      warn "Phase review still blocked after ${MAX_REVIEW_ATTEMPTS} auto-fix attempts — manual intervention required."
      ralph_log "PHASE_BLOCKED: ${MINOR_VERSION} review failed after ${MAX_REVIEW_ATTEMPTS} auto-fix attempts. See ${REVIEW_LOG} for the last reviewer output. Fix manually then re-run ralph."
      exit 1
    fi

    REVIEW_OUT="$(cat "$REVIEW_LOG")"
    log "Asking architect agent to fix phase review blockers (attempt ${REVIEW_ATTEMPT})…"

    invoke_agent "$ARCHITECT_AGENT" "The phase review for netmix phase ${MINOR_VERSION} in the Mixxx repository returned PHASE_BLOCKED.
Read the full review output below. Fix every issue listed under 'Must fix before merge'.
Make the minimum changes necessary — do not touch code or docs unrelated to the listed blockers.
Verify with: cmake --build build --target mixxx-test && ctest --test-dir build -R Netmix --output-on-failure
Do NOT commit. Just edit the files. Print exactly: REVIEW_FIXES_DONE when all edits are complete.

Review output:
${REVIEW_OUT}" \
      2>&1 | tee "/tmp/ralph-review-fix-${MINOR_VERSION}-${REVIEW_ATTEMPT}.log"

    if git diff --quiet && git diff --cached --quiet; then
      warn "Fix agent made no file changes — review may still block on next attempt."
    else
      FIX_MSG="fix: address netmix phase ${MINOR_VERSION} review blockers (attempt ${REVIEW_ATTEMPT})"
      FIX_ATTEMPT=0
      FIX_MAX=3
      FIX_COMMIT_LOG="/tmp/ralph-review-fix-commit-${MINOR_VERSION}-${REVIEW_ATTEMPT}.log"

      while [ $FIX_ATTEMPT -lt $FIX_MAX ]; do
        FIX_ATTEMPT=$((FIX_ATTEMPT + 1))
        log "Fix commit attempt ${FIX_ATTEMPT}/${FIX_MAX}"
        git add -A
        if git commit -m "$FIX_MSG" >"$FIX_COMMIT_LOG" 2>&1; then
          cat "$FIX_COMMIT_LOG"
          good "Pre-commit checks passed — commit succeeded."
          git push origin "$BASE_BRANCH" 2>/dev/null || true
          break
        fi
        cat "$FIX_COMMIT_LOG"
        warn "Pre-commit hook failed on fix commit attempt ${FIX_ATTEMPT}."
        if [ $FIX_ATTEMPT -eq $FIX_MAX ]; then
          warn "Fix commit still failing after ${FIX_MAX} attempts — discarding unstaged changes and retrying the review anyway."
          git checkout -- . 2>/dev/null || true
          git clean -fd 2>/dev/null || true
          break
        fi
        HOOK_OUT="$(cat "$FIX_COMMIT_LOG")"
        invoke_agent "$ARCHITECT_AGENT" "The pre-commit hook rejected the review-fix commit for netmix phase ${MINOR_VERSION}.
Fix every failure shown below. Change only what is required to pass.
When done print exactly: FIXES_DONE.

Pre-commit hook output:
${HOOK_OUT}" \
          2>&1 | tee "/tmp/ralph-review-fix-hook-${MINOR_VERSION}-${REVIEW_ATTEMPT}-${FIX_ATTEMPT}.log"
      done
    fi

    log "Re-running phase review after fixes…"
  done
}

# ── per-task runner ──────────────────────────────────────────────────────────
run_task() {
  TASK_ID="$1"
  BRANCH="task-${TASK_ID}"

  log "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
  log "Starting task ${BOLD}${TASK_ID}${RESET}"
  log "Branch: ${BRANCH}  (from ${BASE_BRANCH})"
  log "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

  git checkout "$BASE_BRANCH" >/dev/null 2>&1
  git pull --ff-only origin "$BASE_BRANCH" >/dev/null 2>&1 ||
    warn "could not fast-forward from origin/${BASE_BRANCH} — continuing on local"
  if ! git checkout -b "$BRANCH" >/dev/null 2>&1; then
    ralph_log "BLOCKED on task ${TASK_ID}: branch ${BRANCH} already exists. Resolve it manually before re-running ralph."
    die "Branch '${BRANCH}' already exists — ralph cannot safely continue.\n" \
      "  To retry this task cleanly:\n" \
      "    git branch -D ${BRANCH}\n" \
      "    git push origin --delete ${BRANCH}   # if it was pushed\n" \
      "  Or mark ${TASK_ID} as done in the todo file on ${BASE_BRANCH} if the work is already complete."
  fi

  TASK_BLOCK="$(task_block "$TASK_ID")"
  SKILL_TEXT="$(cat "$SKILL")"

  # ── Step 1: planning agent produces a written plan ─────────────────────
  log "Step 1/3 — planning with $(agent_model "$TASK_PLANNING_AGENT")"

  PLAN="$(
    invoke_agent "$TASK_PLANNING_AGENT" "You are an expert C++/Qt engineer planning a task for the Mixxx netmix feature.
Read the skill file and the task block carefully, then write a numbered
implementation plan. Do NOT write any code. Output plain text only.
The feature plan with architecture context is docs/plan.md — read it.

TASK ID: ${TASK_ID}

TASK BLOCK:
${TASK_BLOCK}

SKILL FILE:
${SKILL_TEXT}

Produce:
1. A numbered list of files to create or edit (path + one-sentence purpose), including the CMakeLists.txt registration edits.
2. A numbered list of tests to write (fixture/test name + what it proves).
3. Any blockers, thread-safety, or security concerns to flag before coding starts."
  )"

  log "Plan ready."

  # ── Step 2: dev agent implements (resolved from task block) ─────────────
  DEV_AGENT="$(resolve_dev_agent "$TASK_BLOCK")"
  log "Step 2/3 — implementing with $(agent_model "$DEV_AGENT")"

  invoke_agent "$DEV_AGENT" "You are Ralph, the autonomous task agent for the Mixxx netmix feature.
Implement task ${TASK_ID} in full, following every rule in the skill file below.
The pre-commit hook runs automatically on git commit — never use --no-verify.
Do not commit yet. Write all files, then verify your work by running ONLY:
  cmake --build build --target mixxx-test -j8
  ctest --test-dir build -R Netmix --output-on-failure
Fix any build or test failures until both pass clean.
When finished print exactly: IMPLEMENTATION_DONE.

TASK ID: ${TASK_ID}

TASK BLOCK:
${TASK_BLOCK}

PLAN:
${PLAN}

SKILL FILE:
${SKILL_TEXT}" \
    2>&1 | tee /tmp/ralph-impl-"$TASK_ID".log

  # ── Step 3: task review agent self-reviews the diff ─────────────────────
  log "Step 3/3 — self-review with $(agent_model "$TASK_REVIEW_AGENT")"

  DIFF="$(git diff HEAD 2>&1 | head -600)"

  invoke_agent "$TASK_REVIEW_AGENT" "You are reviewing a C++/Qt implementation for the Mixxx netmix feature.
Work through every item in the self-review checklist from the skill file.
For each item write PASS or FAIL and a one-line reason.
For any FAIL item: open the file and fix it now before printing REVIEW_DONE.
Do not skip any checklist item.

TASK ID: ${TASK_ID}

GIT DIFF (up to 600 lines):
${DIFF}

SKILL FILE (contains the checklist):
${SKILL_TEXT}

After fixing all failures print exactly: REVIEW_DONE." \
    2>&1 | tee /tmp/ralph-review-"$TASK_ID".log

  # ── Commit with retry — pre-commit hook is the single check gate ────────
  log "Committing task ${TASK_ID}"

  COMMIT_MSG="$(
    invoke_agent "$TASK_PLANNING_AGENT" "Write a git commit message for netmix task ${TASK_ID} in the Mixxx repository.
Format: first line is '${TASK_ID}: <description, 10 words max>'.
Then a blank line. Then one paragraph body: what was done and why.
Output only the commit message text, no markdown fences.
Task: ${TASK_BLOCK}"
  )"

  COMMIT_ATTEMPTS=0
  MAX_ATTEMPTS=3
  COMMIT_LOG="/tmp/ralph-commit-${TASK_ID}.log"

  while [ $COMMIT_ATTEMPTS -lt $MAX_ATTEMPTS ]; do
    COMMIT_ATTEMPTS=$((COMMIT_ATTEMPTS + 1))
    log "Commit attempt ${COMMIT_ATTEMPTS}/${MAX_ATTEMPTS}"

    git add -A
    if git commit -m "$COMMIT_MSG" >"$COMMIT_LOG" 2>&1; then
      cat "$COMMIT_LOG"
      good "Pre-commit checks passed — commit succeeded."
      break
    fi

    if grep -q "nothing to commit" "$COMMIT_LOG" 2>/dev/null; then
      cat "$COMMIT_LOG"
      good "Working tree is clean — commit already exists."
      break
    fi

    cat "$COMMIT_LOG"
    warn "Pre-commit hook failed on attempt ${COMMIT_ATTEMPTS}."

    if [ $COMMIT_ATTEMPTS -eq $MAX_ATTEMPTS ]; then
      ralph_log "BLOCKED on task ${TASK_ID}: pre-commit hook still failing after ${MAX_ATTEMPTS} attempts."
      git checkout "$BASE_BRANCH" >/dev/null 2>&1
      git branch -D "$BRANCH" >/dev/null 2>&1 || true
      die "Giving up on ${TASK_ID} after ${MAX_ATTEMPTS} commit attempts."
    fi

    HOOK_OUT="$(cat "$COMMIT_LOG")"
    log "Asking architect agent to fix failures…"
    invoke_agent "$ARCHITECT_AGENT" "The pre-commit hook rejected the commit for netmix task ${TASK_ID} in the Mixxx repository.
Fix every failure shown below. Change only what is required to pass.
When done print exactly: FIXES_DONE.

Pre-commit hook output:
${HOOK_OUT}" \
      2>&1 | tee /tmp/ralph-fix-"$TASK_ID"-"$COMMIT_ATTEMPTS".log
  done

  git push origin HEAD 2>/dev/null || warn "could not push ${BRANCH} — continuing on local"

  good "Task ${TASK_ID} committed on branch ${BRANCH}."

  # ── Mark task done in todo file before merging ──────────────────────────
  log "Marking ${TASK_ID} done in todo file..."
  _todo_match=""
  for _f in "$REPO_ROOT/docs"/todo-v*.md; do
    if grep -q "\`${TASK_ID}\`" "$_f" 2>/dev/null; then
      _todo_match="$_f"
      break
    fi
  done
  if [ -n "$_todo_match" ]; then
    if grep -q "\- \[ \] \`${TASK_ID}\`" "$_todo_match" 2>/dev/null; then
      sed_inplace "s/^- \[ \] \`${TASK_ID}\`/- [x] \`${TASK_ID}\`/" "$_todo_match"
      git add "$_todo_match"
      git commit -m "docs: mark ${TASK_ID} done"
      git push origin HEAD 2>/dev/null || true
      good "Task ${TASK_ID} checked off in todo."
    else
      good "Task ${TASK_ID} already checked off — skipping."
    fi
  else
    warn "Could not find todo file for task ${TASK_ID} — checkbox not updated."
  fi

  # Merge back into the release branch so next_task() reads a consistent baseline.
  log "Merging ${BRANCH} back into ${BASE_BRANCH}"
  git checkout "$BASE_BRANCH"
  git pull --ff-only origin "$BASE_BRANCH" >/dev/null 2>&1 ||
    warn "could not fast-forward ${BASE_BRANCH} from origin — continuing on local"
  git merge --no-ff "$BRANCH" -m "merge: ${BRANCH} into ${BASE_BRANCH}"
  git push origin "$BASE_BRANCH" 2>/dev/null || true

  git branch -d "$BRANCH"
  git push origin --delete "$BRANCH" 2>/dev/null || true

  good "Task ${TASK_ID} merged into ${BASE_BRANCH} and task branch cleaned up."
  ralph_log "DONE: ${TASK_ID} merged into ${BASE_BRANCH}."
}

# ── main ────────────────────────────────────────────────────────────────────

if [ -n "$SINGLE_TASK" ]; then
  if deadline_reached || stop_requested; then
    warn "Stop/deadline condition met before task could start."
    exit 0
  fi
  if [ "$BASE_BRANCH" = "$TRUNK" ]; then
    _task_minor="$(printf '%s' "$SINGLE_TASK" | sed 's/\.[0-9]*$//')"
    switch_to_phase "$_task_minor"
  fi
  run_task "$SINGLE_TASK"
  exit 0
fi

TASKS_DONE=0

while true; do
  if deadline_reached; then
    good "Time limit reached. Tasks completed this session: ${TASKS_DONE}."
    ralph_log "Time limit reached. Tasks completed: ${TASKS_DONE}."
    exit 0
  fi

  if stop_requested; then
    good "Graceful stop. Tasks completed this session: ${TASKS_DONE}."
    ralph_log "Graceful stop. Tasks completed: ${TASKS_DONE}."
    exit 0
  fi

  if [ "$BASE_BRANCH" = "$TRUNK" ]; then
    git pull --ff-only origin "$TRUNK" >/dev/null 2>&1 ||
      warn "could not fast-forward ${TRUNK} from origin — continuing on local"
    _next_minor="$(next_minor)"
    if [ -z "$_next_minor" ]; then
      good "All phases complete. Tasks completed this session: ${TASKS_DONE}."
      ralph_log "All phases complete. Tasks completed: ${TASKS_DONE}."
      exit 0
    fi
    switch_to_phase "$_next_minor"
  fi

  TASK_ID="$(next_task)"

  if [ -z "$TASK_ID" ]; then
    good "All ${MINOR_VERSION} tasks complete (${TASKS_DONE} done this session)."
    ralph_log "All ${MINOR_VERSION} tasks complete. Starting phase review."
    phase_review_and_merge
    BASE_BRANCH="$TRUNK"
    MINOR_VERSION=""
    continue
  fi

  run_task "$TASK_ID" || {
    warn "Task ${TASK_ID} failed — logging and moving on."
    ralph_log "FAILED: ${TASK_ID} — see /tmp/ralph-*.log for details."
    git checkout "$BASE_BRANCH" >/dev/null 2>&1 || true
    git branch -D "task-${TASK_ID}" >/dev/null 2>&1 || true
  }

  TASKS_DONE=$((TASKS_DONE + 1))

  if [ -n "$UNTIL_TASK" ] && [ "$TASK_ID" = "$UNTIL_TASK" ]; then
    good "Reached --until=${UNTIL_TASK}. Tasks completed this session: ${TASKS_DONE}."
    ralph_log "UNTIL_STOP: reached ${UNTIL_TASK} after ${TASKS_DONE} tasks."
    exit 0
  fi

  if [ "$DEADLINE" -gt 0 ]; then
    log "Time remaining: $(time_remaining)"
  fi

  sleep 2
done
