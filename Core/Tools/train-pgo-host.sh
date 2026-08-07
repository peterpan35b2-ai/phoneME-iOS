#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO_ROOT="$(cd "$CORE_ROOT/.." && pwd)"
MANIFEST="${PHONEME_PGO_CORPUS_MANIFEST:-$SCRIPT_DIR/performance-benchmarks.json}"
OUTPUT_ROOT="${PHONEME_PGO_OUTPUT_ROOT:-$CORE_ROOT/build/performance/pgo-training}"
RAW_ROOT="$OUTPUT_ROOT/raw"
CORPUS_ROOT="$OUTPUT_ROOT/corpus"
PROFILE_DATA="$OUTPUT_ROOT/phoneme-core.profdata"
GENERATE_BUILD_ROOT="${PHONEME_PGO_GENERATE_BUILD_ROOT:-$CORE_ROOT/build/pgo-generate-host}"
USE_BUILD_ROOT="${PHONEME_PGO_USE_BUILD_ROOT:-$CORE_ROOT/build/pgo-use-host}"
BENCHMARK_JSON="$OUTPUT_ROOT/jit-performance-pgo.json"
JOBS="${PHONEME_PGO_CORPUS_JOBS:-1}"
FILTER="${PHONEME_PGO_FILTER:-}"

LLVM_PROFDATA="${LLVM_PROFDATA:-$(xcrun --find llvm-profdata)}"

rm -rf "$OUTPUT_ROOT"
mkdir -p "$RAW_ROOT" "$CORPUS_ROOT"

PHONEME_PGO_FILTER="$FILTER" python3 - "$REPO_ROOT" "$MANIFEST" <<'PY'
import hashlib
import json
import os
import pathlib
import sys

repo_root = pathlib.Path(sys.argv[1])
manifest_path = pathlib.Path(sys.argv[2])
manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
requested = {
    item.strip()
    for item in os.environ.get("PHONEME_PGO_FILTER", "").split(",")
    if item.strip()
}
verified = 0
for benchmark in manifest.get("benchmarks", []):
    relative = pathlib.Path(benchmark["jar"])
    jar_name = pathlib.PurePosixPath(benchmark["jar"]).name
    if requested and benchmark.get("id") not in requested and jar_name not in requested:
        continue
    path = repo_root / relative
    if not path.is_file():
        raise SystemExit(f"missing PGO corpus JAR: {relative}")
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    expected = benchmark["sha256"].lower()
    if digest != expected:
        raise SystemExit(
            f"PGO corpus hash mismatch for {relative}: {digest} != {expected}"
        )
    verified += 1
if verified == 0:
    raise SystemExit("PGO filter matched no corpus entries")
print(f"Verified {verified} PGO corpus JARs")
PY

FILTER_ARGS=()
while IFS= read -r jar_name; do
  [[ -n "$jar_name" ]] || continue
  FILTER_ARGS+=(--filter "$jar_name")
done < <(PHONEME_PGO_FILTER="$FILTER" python3 - "$MANIFEST" <<'PY'
import json
import os
import pathlib
import sys
manifest = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
requested = {
    item.strip()
    for item in os.environ.get("PHONEME_PGO_FILTER", "").split(",")
    if item.strip()
}
for benchmark in manifest.get("benchmarks", []):
    jar_name = pathlib.PurePosixPath(benchmark["jar"]).name
    if requested and benchmark.get("id") not in requested and jar_name not in requested:
        continue
    print(jar_name)
PY
)

TIMEOUT_MS="$(PHONEME_PGO_FILTER="$FILTER" python3 - "$MANIFEST" <<'PY'
import json
import os
import pathlib
import sys
manifest = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
requested = {
    item.strip()
    for item in os.environ.get("PHONEME_PGO_FILTER", "").split(",")
    if item.strip()
}
timeouts = []
for item in manifest.get("benchmarks", []):
    jar_name = pathlib.PurePosixPath(item["jar"]).name
    if requested and item.get("id") not in requested and jar_name not in requested:
        continue
    timeouts.append(int(item.get("timeout_seconds", 1)))
print(max(timeouts, default=1) * 1000)
PY
)"

[[ "${#FILTER_ARGS[@]}" -gt 0 ]] || {
  echo "PGO corpus manifest contains no benchmarks: $MANIFEST" >&2
  exit 2
}

# Build the instrumented core once with the production CMake flags, then link
# the compatibility harness against that archive. This keeps the profile tied
# to the exact core sources while avoiding a second monolithic compile of every
# translation unit in test-jar-directory.py.
cmake \
  -S "$CORE_ROOT" \
  -B "$GENERATE_BUILD_ROOT" \
  -DCMAKE_BUILD_TYPE=Release \
  -DPHONEME_ENABLE_VM_PROFILING=OFF \
  -DPHONEME_ENABLE_DECODED_EXECUTION=ON \
  -DPHONEME_ENABLE_LTO=OFF \
  -DPHONEME_PGO_MODE=GENERATE
cmake --build "$GENERATE_BUILD_ROOT" --parallel "${PHONEME_TEST_JOBS:-4}"

LLVM_PROFILE_FILE="$RAW_ROOT/phoneme-%m-%p.profraw" \
PHONEME_ENABLE_VM_PROFILING=0 \
PHONEME_ENABLE_DECODED_EXECUTION=1 \
PHONEME_PREBUILT_CORE_LIB="$GENERATE_BUILD_ROOT/libphoneMECore.a" \
PHONEME_EXTRA_CXXFLAGS="-O3 -DNDEBUG" \
PHONEME_EXTRA_LDFLAGS="-fprofile-instr-generate" \
python3 "$SCRIPT_DIR/test-jar-directory.py" \
  --jar-dir "$REPO_ROOT/jar_test" \
  --output "$CORPUS_ROOT" \
  --report "$CORPUS_ROOT/report.md" \
  --mode smoke \
  --jobs "$JOBS" \
  --timeout-ms "$TIMEOUT_MS" \
  --observe-manifest "$MANIFEST" \
  "${FILTER_ARGS[@]}"

RAW_PROFILES=()
while IFS= read -r profile; do
  [[ -n "$profile" ]] || continue
  RAW_PROFILES+=("$profile")
done < <(find "$RAW_ROOT" -type f -name '*.profraw' -size +0c -print | sort)
[[ "${#RAW_PROFILES[@]}" -gt 0 ]] || {
  echo "PGO corpus produced no profile data in $RAW_ROOT" >&2
  exit 3
}

"$LLVM_PROFDATA" merge -sparse "${RAW_PROFILES[@]}" -o "$PROFILE_DATA"
[[ -s "$PROFILE_DATA" ]] || {
  echo "Merged PGO profile is empty: $PROFILE_DATA" >&2
  exit 4
}

cmake \
  -S "$CORE_ROOT" \
  -B "$USE_BUILD_ROOT" \
  -DCMAKE_BUILD_TYPE=Release \
  -DPHONEME_ENABLE_VM_PROFILING=OFF \
  -DPHONEME_ENABLE_DECODED_EXECUTION=ON \
  -DPHONEME_ENABLE_LTO=ON \
  -DPHONEME_PGO_MODE=USE \
  -DPHONEME_PGO_PROFILE="$PROFILE_DATA"
cmake --build "$USE_BUILD_ROOT" --parallel "${PHONEME_TEST_JOBS:-4}"

PHONEME_JIT_PERF_BUILD_ROOT="$USE_BUILD_ROOT" \
PHONEME_ENABLE_LTO=ON \
PHONEME_ENABLE_VM_PROFILING=OFF \
PHONEME_PGO_MODE=USE \
PHONEME_PGO_PROFILE="$PROFILE_DATA" \
  bash "$SCRIPT_DIR/benchmark-jit-host.sh" "$BENCHMARK_JSON"

echo "PGO training completed"
echo "Profile: $PROFILE_DATA"
echo "Corpus report: $CORPUS_ROOT/report.json"
echo "PGO benchmark: $BENCHMARK_JSON"
