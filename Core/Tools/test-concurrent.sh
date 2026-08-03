#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
source "$SCRIPT_DIR/lib/common-test-root.sh"

HOSTS_ONLY=0
case "${1:-}" in
  --hosts-only) HOSTS_ONLY=1 ;;
  '') ;;
  *)
    echo "Usage: test-concurrent.sh [--hosts-only]" >&2
    exit 64
    ;;
esac

REPORT_ROOT="$(phoneme_make_isolated_root "$CORE_ROOT" "concurrent-tests")"
mkdir -p "$REPORT_ROOT/host-a" "$REPORT_ROOT/host-b"

run_host() {
  local label="$1"
  local base="$2"
  local log="$3"
  PHONEME_BUILD_ROOT="$base" \
  PHONEME_TASK_ID="$label" \
  PHONEME_TEST_TIMEOUT="${PHONEME_TEST_TIMEOUT:-900}" \
    bash "$SCRIPT_DIR/test-module.sh" host >"$log" 2>&1
}

run_host host-a "$REPORT_ROOT/host-a" "$REPORT_ROOT/host-a.log" &
PID_A=$!
run_host host-b "$REPORT_ROOT/host-b" "$REPORT_ROOT/host-b.log" &
PID_B=$!

STATUS_A=0
STATUS_B=0
wait "$PID_A" || STATUS_A=$?
wait "$PID_B" || STATUS_B=$?

if [[ "$STATUS_A" -ne 0 || "$STATUS_B" -ne 0 ]]; then
  printf 'Concurrent host tests failed: host-a=%s host-b=%s\n' \
    "$STATUS_A" "$STATUS_B" >&2
  tail -80 "$REPORT_ROOT/host-a.log" >&2 || true
  tail -80 "$REPORT_ROOT/host-b.log" >&2 || true
  printf 'Reports: %s\n' "$REPORT_ROOT" >&2
  exit 1
fi

printf 'Concurrent host tests passed with independent roots.\n'
if [[ "$HOSTS_ONLY" == "1" ]]; then
  printf 'Concurrent verification reports: %s\n' "$REPORT_ROOT"
  exit 0
fi

PHONEME_TEST_REPORT_ROOT="$REPORT_ROOT/module-matrix" \
PHONEME_TEST_JOBS="${PHONEME_TEST_JOBS:-4}" \
PHONEME_TEST_TIMEOUT="${PHONEME_TEST_TIMEOUT:-900}" \
  bash "$SCRIPT_DIR/test-all-host.sh"

printf 'Concurrent verification reports: %s\n' "$REPORT_ROOT"
