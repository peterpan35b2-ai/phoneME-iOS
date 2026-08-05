#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
source "$SCRIPT_DIR/lib/common-test-root.sh"

WORK_ROOT="${PHONEME_DECODED_DIFF_ROOT:-$CORE_ROOT/build/decoded-differential}"
WORK_ROOT="$(phoneme_prepare_managed_root "$WORK_ROOT" "$CORE_ROOT")"
phoneme_register_cleanup "$WORK_ROOT"
PROFILE_ROOT="$WORK_ROOT/profiles"
BUILD_ROOT="$WORK_ROOT/build"
REPORT_PATH="${PHONEME_DECODED_DIFF_REPORT:-$CORE_ROOT/build/performance/decoded-differential.json}"
mkdir -p "$PROFILE_ROOT" "$(dirname "$REPORT_PATH")"

PHONEME_BUILD_ROOT="$BUILD_ROOT" \
PHONEME_KEEP_TEST_ROOT=1 \
PHONEME_TEST_FILTER=vm-invocation \
PHONEME_ENABLE_VM_PROFILING=1 \
PHONEME_ENABLE_DECODED_EXECUTION=1 \
PHONEME_TEST_TIMEOUT="${PHONEME_TEST_TIMEOUT:-300}" \
  bash "$SCRIPT_DIR/test-host.sh"

TEST_BINARY="$(find "$BUILD_ROOT" -type f -name CoreTests -print -quit)"
FIXTURE_JAR="$(find "$BUILD_ROOT" -type f -name core-fixture.jar -print -quit)"
[[ -x "$TEST_BINARY" && -f "$FIXTURE_JAR" ]] || {
  echo "decoded differential test binary or fixture JAR is missing" >&2
  exit 1
}

FILTERS=(vm-invocation vm-dispatch micro3d)
if [[ -n "${PHONEME_DECODED_DIFF_FILTERS:-}" ]]; then
  read -r -a FILTERS <<<"$PHONEME_DECODED_DIFF_FILTERS"
fi

for filter in "${FILTERS[@]}"; do
  legacy_json="$PROFILE_ROOT/$filter.legacy.json"
  decoded_json="$PROFILE_ROOT/$filter.decoded.json"

  PHONEME_TEST_FILTER="$filter" \
  PHONEME_USE_DECODED_EXECUTION=0 \
  PHONEME_VM_PROFILE_JSON="$legacy_json" \
    "$TEST_BINARY" "$FIXTURE_JAR"

  PHONEME_TEST_FILTER="$filter" \
  PHONEME_USE_DECODED_EXECUTION=1 \
  PHONEME_VM_PROFILE_JSON="$decoded_json" \
    "$TEST_BINARY" "$FIXTURE_JAR"
done

python3 - "$PROFILE_ROOT" "$REPORT_PATH" "${FILTERS[@]}" <<'PY'
import json
import pathlib
import sys

profile_root = pathlib.Path(sys.argv[1])
report_path = pathlib.Path(sys.argv[2])
filters = sys.argv[3:]

interpreter_keys = (
    "executed_bytecodes",
    "method_invocations",
    "native_invocations",
    "maximum_java_call_depth",
    "exception_dispatches",
    "class_initializations",
    "instruction_budget_exits",
    "scheduler_quanta",
)
# Resolution/cache counters are intentionally allowed to differ: decoded
# execution exists to remove repeated lookups. Keep immutable decode totals in
# the comparison while treating hot-path cache activity as performance data.
metadata_ignored = {
    "class_cache_hits",
    "class_cache_misses",
    "method_resolution_hits",
    "method_resolution_misses",
    "declared_method_resolution_hits",
    "declared_method_resolution_misses",
    "field_resolution_hits",
    "field_resolution_misses",
    "assignability_cache_hits",
    "assignability_cache_misses",
    "native_registry_lookups",
    "metadata_key_constructions",
    "virtual_inline_cache_hits",
    "virtual_inline_cache_misses",
    "direct_call_cache_hits",
    "direct_call_cache_misses",
    "operand_resolution_hits",
    "operand_resolution_misses",
    "operand_resolution_failures",
    "descriptor_cache_hits",
    "descriptor_cache_misses",
    "decoded_opcode_dispatches",
    "decoded_operand_dispatches",
}
heap_ignored = {
    "gc_total_nanoseconds",
    "gc_max_pause_nanoseconds",
}

results = []
for filter_name in filters:
    legacy_path = profile_root / f"{filter_name}.legacy.json"
    decoded_path = profile_root / f"{filter_name}.decoded.json"
    legacy = json.loads(legacy_path.read_text(encoding="utf-8"))
    decoded = json.loads(decoded_path.read_text(encoding="utf-8"))

    differences = {}
    for key in interpreter_keys:
        if legacy["interpreter"][key] != decoded["interpreter"][key]:
            differences[f"interpreter.{key}"] = [
                legacy["interpreter"][key],
                decoded["interpreter"][key],
            ]
    if legacy["opcode_counts"] != decoded["opcode_counts"]:
        differences["opcode_counts"] = [
            [
                index,
                legacy["opcode_counts"][index],
                decoded["opcode_counts"][index],
            ]
            for index in range(256)
            if legacy["opcode_counts"][index] != decoded["opcode_counts"][index]
        ]
    for section, ignored in (
        ("metadata", metadata_ignored),
        ("heap", heap_ignored),
        ("scheduler", set()),
    ):
        for key, legacy_value in legacy[section].items():
            if key in ignored:
                continue
            decoded_value = decoded[section][key]
            if legacy_value != decoded_value:
                differences[f"{section}.{key}"] = [legacy_value, decoded_value]

    results.append({
        "filter": filter_name,
        "passed": not differences,
        "differences": differences,
        "legacy_profile": str(legacy_path),
        "decoded_profile": str(decoded_path),
    })

report = {
    "schema_version": 1,
    "decoded_execution_build_enabled": True,
    "filters": results,
    "passed": all(result["passed"] for result in results),
}
report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
if not report["passed"]:
    print(json.dumps(report, indent=2), file=sys.stderr)
    raise SystemExit(1)
print(f"Decoded differential tests passed: {', '.join(filters)}")
print(f"Report: {report_path}")
PY
