#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
source "$SCRIPT_DIR/lib/common-test-root.sh"

usage() {
  cat <<'EOF'
Usage: test-all-host.sh [--jobs N] [MODULE ...]

Runs the standalone host module matrix in isolated roots. Reports are written
as console.log files plus results.tap and results.json.
EOF
}

JOBS="${PHONEME_TEST_JOBS:-4}"
while [[ "${1:-}" == --* ]]; do
  case "$1" in
    --jobs)
      [[ "$#" -ge 2 ]] || { usage >&2; exit 64; }
      JOBS="$2"
      shift 2
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      usage >&2
      exit 64
      ;;
  esac
done
[[ "$JOBS" =~ ^[0-9]+$ && "$JOBS" -gt 0 ]] || {
  phoneme_tool_error "jobs must be a positive integer: $JOBS"
  exit 64
}

if [[ "$#" -gt 0 ]]; then
  MODULES=("$@")
else
  MODULES=(host builtin-registry graphics graphics-vm push canvas-graphics game-api filesystem network-adapter security c-api rms scheduler suite-installer lcdui-extended vendor-compat coverage-inventory)
fi

if [[ -n "${PHONEME_TEST_REPORT_ROOT:-}" ]]; then
  REPORT_ROOT="$(phoneme_prepare_managed_root "$PHONEME_TEST_REPORT_ROOT" "$CORE_ROOT")"
else
  REPORT_ROOT="$(phoneme_make_isolated_root "$CORE_ROOT" "host-matrix")"
fi
mkdir -p "$REPORT_ROOT/logs" "$REPORT_ROOT/build"

RESULT_NAMES=()
RESULT_CODES=()
RESULT_DURATIONS=()
RESULT_LOGS=()

run_batch() {
  local start_index="$1"
  local end_index="$2"
  local index module log started ended status
  local pids=()
  local starts=()
  local names=()
  local logs=()

  for ((index=start_index; index<end_index; index++)); do
    module="${MODULES[$index]}"
    log="$REPORT_ROOT/logs/$module.log"
    printf '[RUN ] %s\n' "$module"
    started="$(date +%s)"
    PHONEME_BUILD_ROOT="$REPORT_ROOT/build" \
    PHONEME_TASK_ID="$module" \
    PHONEME_TEST_TIMEOUT="${PHONEME_TEST_TIMEOUT:-600}" \
      bash "$SCRIPT_DIR/test-module.sh" "$module" >"$log" 2>&1 &
    pids+=("$!")
    starts+=("$started")
    names+=("$module")
    logs+=("$log")
  done

  for ((index=0; index<${#pids[@]}; index++)); do
    if wait "${pids[$index]}"; then
      status=0
    else
      status=$?
    fi
    ended="$(date +%s)"
    RESULT_NAMES+=("${names[$index]}")
    RESULT_CODES+=("$status")
    RESULT_DURATIONS+=("$((ended - starts[$index]))")
    RESULT_LOGS+=("${logs[$index]}")
    if [[ "$status" -eq 0 ]]; then
      printf '[PASS] %s (%ss)\n' "${names[$index]}" "$((ended - starts[$index]))"
    else
      printf '[FAIL] %s (exit %s, %ss)\n' \
        "${names[$index]}" "$status" "$((ended - starts[$index]))"
    fi
  done
}

for ((batch=0; batch<${#MODULES[@]}; batch+=JOBS)); do
  end=$((batch + JOBS))
  if [[ "$end" -gt "${#MODULES[@]}" ]]; then
    end="${#MODULES[@]}"
  fi
  run_batch "$batch" "$end"
done

{
  printf 'TAP version 13\n'
  printf '1..%s\n' "${#RESULT_NAMES[@]}"
  for ((index=0; index<${#RESULT_NAMES[@]}; index++)); do
    if [[ "${RESULT_CODES[$index]}" -eq 0 ]]; then
      printf 'ok %s - %s # time=%ss\n' \
        "$((index + 1))" "${RESULT_NAMES[$index]}" "${RESULT_DURATIONS[$index]}"
    else
      printf 'not ok %s - %s # exit=%s time=%ss log=%s\n' \
        "$((index + 1))" "${RESULT_NAMES[$index]}" "${RESULT_CODES[$index]}" \
        "${RESULT_DURATIONS[$index]}" "${RESULT_LOGS[$index]}"
    fi
  done
} > "$REPORT_ROOT/results.tap"

{
  printf '{\n  "schema": 1,\n  "results": [\n'
  for ((index=0; index<${#RESULT_NAMES[@]}; index++)); do
    [[ "$index" -eq 0 ]] || printf ',\n'
    printf '    {"module":"%s","status":"%s","exit_code":%s,"duration_seconds":%s,"log":"%s"}' \
      "$(phoneme_json_escape "${RESULT_NAMES[$index]}")" \
      "$([[ "${RESULT_CODES[$index]}" -eq 0 ]] && printf pass || printf fail)" \
      "${RESULT_CODES[$index]}" "${RESULT_DURATIONS[$index]}" \
      "$(phoneme_json_escape "${RESULT_LOGS[$index]}")"
  done
  printf '\n  ]\n}\n'
} > "$REPORT_ROOT/results.json"

FAILED=0
for ((index=0; index<${#RESULT_NAMES[@]}; index++)); do
  if [[ "${RESULT_CODES[$index]}" -ne 0 ]]; then
    FAILED=$((FAILED + 1))
    printf '\n--- %s failure log ---\n' "${RESULT_NAMES[$index]}" >&2
    tail -80 "${RESULT_LOGS[$index]}" >&2 || true
  fi
done

printf '\nReports: %s\n' "$REPORT_ROOT"
printf 'TAP: %s\n' "$REPORT_ROOT/results.tap"
printf 'JSON: %s\n' "$REPORT_ROOT/results.json"

[[ "$FAILED" -eq 0 ]] || exit 1
