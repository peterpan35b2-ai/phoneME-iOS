#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
source "$SCRIPT_DIR/lib/common-test-root.sh"

RMS_SOURCE="$CORE_ROOT/Tests/RmsAdvancedTests.cpp"
RMS_RUNNER="$SCRIPT_DIR/test-rms-host.sh"
MATRIX_RUNNER="$SCRIPT_DIR/test-all-host.sh"
REPORT_ROOT="$(phoneme_make_isolated_root "$CORE_ROOT" "coverage-inventory" "${PHONEME_COVERAGE_REPORT_ROOT:-}")"
phoneme_register_cleanup "$REPORT_ROOT"

for file in "$RMS_SOURCE" "$RMS_RUNNER" "$MATRIX_RUNNER"; do
  [[ -f "$file" ]] || {
    phoneme_tool_error "coverage inventory input is missing: $file"
    exit 66
  }
done

mapfile_compat() {
  local output_name="$1"
  shift
  local value
  eval "$output_name=()"
  while IFS= read -r value; do
    [[ -n "$value" ]] || continue
    eval "$output_name+=(\"\$value\")"
  done < <("$@")
}

extract_rms_tests() {
  rg -o '^void (test_[A-Za-z0-9_]+)\(' "$RMS_SOURCE" \
    | sed -E 's/^void (test_[A-Za-z0-9_]+)\(.*/\1/' \
    | LC_ALL=C sort -u
}

RMS_TESTS=()
mapfile_compat RMS_TESTS extract_rms_tests
[[ "${#RMS_TESTS[@]}" -gt 0 ]] || {
  phoneme_tool_error 'RmsAdvancedTests.cpp declares no test_* functions'
  exit 1
}

REQUIRED_NEGATIVE_TESTS=(
  test_suite_quota
  test_fault_rollback
  test_recovery_selection
  test_process_crash_recovery
  test_migration_and_future_version
  test_suite_isolation_and_names
)

FAILURES=0
TAP_INDEX=0
TAP_TOTAL=$((${#RMS_TESTS[@]} + ${#REQUIRED_NEGATIVE_TESTS[@]} + 4))
TAP_FILE="$REPORT_ROOT/coverage-inventory.tap"
JSON_FILE="$REPORT_ROOT/coverage-inventory.json"
: > "$TAP_FILE"
printf 'TAP version 13\n1..%s\n' "$TAP_TOTAL" >> "$TAP_FILE"

record_check() {
  local label="$1"
  local status="$2"
  local detail="${3:-}"
  TAP_INDEX=$((TAP_INDEX + 1))
  if [[ "$status" == "pass" ]]; then
    printf 'ok %s - %s%s\n' "$TAP_INDEX" "$label" \
      "$([[ -n "$detail" ]] && printf ' # %s' "$detail")" >> "$TAP_FILE"
  else
    printf 'not ok %s - %s%s\n' "$TAP_INDEX" "$label" \
      "$([[ -n "$detail" ]] && printf ' # %s' "$detail")" >> "$TAP_FILE"
    FAILURES=$((FAILURES + 1))
  fi
}

JSON_TESTS=()
for test_name in "${RMS_TESTS[@]}"; do
  occurrence_count="$(rg -c "\\b${test_name}\\(" "$RMS_SOURCE")"
  if [[ "$occurrence_count" -ge 2 ]]; then
    record_check "RMS method $test_name is invoked" pass "occurrences=$occurrence_count"
    JSON_TESTS+=("{\"name\":\"$test_name\",\"invoked\":true,\"occurrences\":$occurrence_count}")
  else
    record_check "RMS method $test_name is invoked" fail "occurrences=$occurrence_count"
    JSON_TESTS+=("{\"name\":\"$test_name\",\"invoked\":false,\"occurrences\":$occurrence_count}")
  fi
done

for test_name in "${REQUIRED_NEGATIVE_TESTS[@]}"; do
  if printf '%s\n' "${RMS_TESTS[@]}" | grep -qx "$test_name"; then
    record_check "required RMS negative path $test_name exists" pass
  else
    record_check "required RMS negative path $test_name exists" fail
  fi
done

if rg -q 'RmsAdvancedTests\.cpp' "$RMS_RUNNER"; then
  record_check 'RMS runner compiles advanced native tests' pass
else
  record_check 'RMS runner compiles advanced native tests' fail
fi
if rg -q 'fixtures/RmsOps\.java' "$RMS_RUNNER"; then
  record_check 'RMS runner compiles VM fixture' pass
else
  record_check 'RMS runner compiles VM fixture' fail
fi
if rg -q 'RmsCrashHarness\.cpp' "$RMS_RUNNER"; then
  record_check 'RMS runner compiles crash-recovery harness' pass
else
  record_check 'RMS runner compiles crash-recovery harness' fail
fi
MATRIX_INCLUDES_RMS=false
if rg -q 'MODULES=.*\brms\b' "$MATRIX_RUNNER"; then
  MATRIX_INCLUDES_RMS=true
  record_check 'default host matrix includes RMS advanced module' pass
else
  record_check 'default host matrix includes RMS advanced module' fail
fi

{
  printf '{\n'
  printf '  "schema": 1,\n'
  printf '  "module": "rms",\n'
  printf '  "advanced_test_source": "%s",\n' \
    "$(phoneme_json_escape "$RMS_SOURCE")"
  printf '  "runner": "%s",\n' "$(phoneme_json_escape "$RMS_RUNNER")"
  printf '  "default_matrix_includes_module": %s,\n' \
    "$MATRIX_INCLUDES_RMS"
  printf '  "methods": [\n'
  for ((index=0; index<${#JSON_TESTS[@]}; index++)); do
    [[ "$index" -eq 0 ]] || printf ',\n'
    printf '    %s' "${JSON_TESTS[$index]}"
  done
  printf '\n  ],\n'
  printf '  "failures": %s\n' "$FAILURES"
  printf '}\n'
} > "$JSON_FILE"

cat "$TAP_FILE"
printf 'Coverage inventory JSON: %s\n' "$JSON_FILE"
[[ "$FAILURES" -eq 0 ]]
