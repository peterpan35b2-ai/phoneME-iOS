#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OUTPUT_JSON="${1:-$CORE_ROOT/build/performance/jit-performance.json}"
BUILD_ROOT="${PHONEME_JIT_PERF_BUILD_ROOT:-$CORE_ROOT/build/jit-performance-host}"
LTO_ENABLED="${PHONEME_ENABLE_LTO:-ON}"
VM_PROFILING="${PHONEME_ENABLE_VM_PROFILING:-ON}"
PGO_MODE="${PHONEME_PGO_MODE:-OFF}"
PGO_PROFILE="${PHONEME_PGO_PROFILE:-}"
FIXTURE_ROOT="$BUILD_ROOT/fixture"
FIXTURE_CLASSES="$FIXTURE_ROOT/classes"
FIXTURE_JAR="$FIXTURE_ROOT/jit-performance.jar"
TEST_BINARY="$BUILD_ROOT/jit-performance-tests"

CMAKE="${CMAKE:-cmake}"
CXX="${CXX:-$(xcrun --sdk macosx --find clang++)}"
SDK_ROOT="${SDKROOT:-$(xcrun --sdk macosx --show-sdk-path)}"
JAVAC="${JAVAC:-$(command -v javac)}"
JAR="${JAR:-$(command -v jar)}"

[[ -n "$JAVAC" && -n "$JAR" ]] || {
  echo "javac and jar are required for the JIT benchmark." >&2
  exit 1
}

mkdir -p "$FIXTURE_CLASSES" "$(dirname "$OUTPUT_JSON")"
rm -rf "$FIXTURE_CLASSES"
mkdir -p "$FIXTURE_CLASSES"
"$JAVAC" -source 8 -target 8 -Xlint:-options \
  -d "$FIXTURE_CLASSES" \
  "$CORE_ROOT/Tests/fixtures/JitOps.java"
"$JAR" cf "$FIXTURE_JAR" -C "$FIXTURE_CLASSES" .

"$CMAKE" \
  -S "$CORE_ROOT" \
  -B "$BUILD_ROOT" \
  -DCMAKE_BUILD_TYPE=Release \
  -DPHONEME_ENABLE_VM_PROFILING="$VM_PROFILING" \
  -DPHONEME_ENABLE_DECODED_EXECUTION=ON \
  -DPHONEME_ENABLE_LTO="$LTO_ENABLED" \
  -DPHONEME_PGO_MODE="$PGO_MODE" \
  -DPHONEME_PGO_PROFILE="$PGO_PROFILE"
"$CMAKE" --build "$BUILD_ROOT" --parallel "${PHONEME_TEST_JOBS:-4}"

if [[ "$VM_PROFILING" == "ON" || "$VM_PROFILING" == "1" ||
      "$VM_PROFILING" == "TRUE" || "$VM_PROFILING" == "YES" ]]; then
  VM_PROFILING_DEFINE=1
else
  VM_PROFILING_DEFINE=0
fi

"$CXX" \
  -std=c++23 \
  -O3 \
  -DNDEBUG \
  -DPHONEME_ENABLE_VM_PROFILING="$VM_PROFILING_DEFINE" \
  -DPHONEME_ENABLE_DECODED_EXECUTION=1 \
  -isysroot "$SDK_ROOT" \
  -I"$CORE_ROOT/include" \
  -fno-exceptions \
  -fno-rtti \
  "$CORE_ROOT/Tests/JitPerformanceTests.cpp" \
  "$BUILD_ROOT/libphoneMECore.a" \
  -lz \
  -framework CoreText \
  -framework CoreGraphics \
  -framework ImageIO \
  -framework CoreFoundation \
  -o "$TEST_BINARY"

PHONEME_JIT_HOT_THRESHOLD=1 \
PHONEME_JIT_BACKGROUND_COMPILE=0 \
  "$TEST_BINARY" "$FIXTURE_JAR" "$OUTPUT_JSON"

echo "JIT benchmark: $OUTPUT_JSON"
