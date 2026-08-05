#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO_ROOT="$(cd "$CORE_ROOT/.." && pwd)"
source "$SCRIPT_DIR/lib/common-test-root.sh"

MANIFEST="${PHONEME_CORPUS_MANIFEST:-$SCRIPT_DIR/performance-benchmarks.json}"
OUTPUT_ROOT="${PHONEME_DECODED_CORPUS_ROOT:-$CORE_ROOT/build/performance/decoded-corpus-final}"
OUTPUT_ROOT="$(phoneme_prepare_managed_root "$OUTPUT_ROOT" "$CORE_ROOT")"
phoneme_register_cleanup "$OUTPUT_ROOT"

LEGACY_ROOT="$OUTPUT_ROOT/legacy"
DECODED_ROOT="$OUTPUT_ROOT/decoded"
COMPARISON_JSON="$OUTPUT_ROOT/comparison.json"
# Screen-identity probes must not compete for CPU/network timing across suites.
# Keep the release-gate corpus serialized; callers may opt into parallel smoke
# runs when exact milestone identity is not being asserted.
JOBS="${PHONEME_CORPUS_JOBS:-1}"

FILTER_ARGS=()
while IFS= read -r jar_name; do
  [[ -n "$jar_name" ]] || continue
  FILTER_ARGS+=(--filter "$jar_name")
done < <(python3 - "$MANIFEST" <<'PY'
import json
import pathlib
import sys
manifest = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
for benchmark in manifest.get("benchmarks", []):
    print(pathlib.PurePosixPath(benchmark["jar"]).name)
PY
)

TIMEOUT_MS="$(python3 - "$MANIFEST" <<'PY'
import json
import pathlib
import sys
manifest = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
timeouts = [int(item.get("timeout_seconds", 1)) for item in manifest.get("benchmarks", [])]
print(max(timeouts, default=1) * 1000)
PY
)"

[[ "${#FILTER_ARGS[@]}" -gt 0 ]] || {
  echo "Decoded corpus manifest contains no benchmarks: $MANIFEST" >&2
  exit 2
}

PHONEME_ENABLE_DECODED_EXECUTION=1 \
PHONEME_USE_DECODED_EXECUTION=0 \
python3 "$SCRIPT_DIR/test-jar-directory.py" \
  --jar-dir "$REPO_ROOT/jar_test" \
  --output "$LEGACY_ROOT" \
  --report "$LEGACY_ROOT/report.md" \
  --mode smoke \
  --jobs "$JOBS" \
  --timeout-ms "$TIMEOUT_MS" \
  --observe-manifest "$MANIFEST" \
  "${FILTER_ARGS[@]}"

RUNNER="$LEGACY_ROOT/harness-build/CompatibilityHarness"
[[ -x "$RUNNER" ]] || {
  echo "Decoded corpus CompatibilityHarness is missing: $RUNNER" >&2
  exit 2
}

PHONEME_ENABLE_DECODED_EXECUTION=1 \
PHONEME_USE_DECODED_EXECUTION=1 \
python3 "$SCRIPT_DIR/test-jar-directory.py" \
  --jar-dir "$REPO_ROOT/jar_test" \
  --output "$DECODED_ROOT" \
  --report "$DECODED_ROOT/report.md" \
  --runner "$RUNNER" \
  --mode smoke \
  --jobs "$JOBS" \
  --timeout-ms "$TIMEOUT_MS" \
  --observe-manifest "$MANIFEST" \
  "${FILTER_ARGS[@]}"

python3 "$SCRIPT_DIR/compare-decoded-corpus.py" \
  --manifest "$MANIFEST" \
  --legacy "$LEGACY_ROOT/report.json" \
  --decoded "$DECODED_ROOT/report.json" \
  --output "$COMPARISON_JSON"

echo "Decoded corpus host tests passed"
echo "Comparison: $COMPARISON_JSON"
echo "Legacy report: $LEGACY_ROOT/report.json"
echo "Decoded report: $DECODED_ROOT/report.json"
