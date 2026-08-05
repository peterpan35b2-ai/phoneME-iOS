#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
source "$SCRIPT_DIR/lib/common-test-root.sh"
TEST_ROOT="$(phoneme_make_isolated_root "$CORE_ROOT" "scheduler-host-tests" "${PHONEME_SCHEDULER_TEST_ROOT:-}")"
phoneme_register_cleanup "$TEST_ROOT"
TEST_BINARY="$TEST_ROOT/SchedulerTests"
FIXTURE_CLASSES="$TEST_ROOT/fixture-classes"
FIXTURE_JAR="$TEST_ROOT/scheduler-fixture.jar"
CXX="$(xcrun --sdk macosx --find clang++)"
SDK_ROOT="$(xcrun --sdk macosx --show-sdk-path)"
JAVAC="${JAVAC:-$(command -v javac)}"
JAR="${JAR:-$(command -v jar)}"
phoneme_configure_sanitizers
SANITIZER_FLAGS="$PHONEME_SANITIZER_FLAGS"

[[ -n "$JAVAC" && -n "$JAR" ]] || {
  echo "javac and jar are required for scheduler tests." >&2
  exit 1
}

mkdir -p "$FIXTURE_CLASSES"

"$JAVAC" -source 8 -target 8 -Xlint:-options \
  -d "$FIXTURE_CLASSES" \
  "$CORE_ROOT/Tests/fixtures/ThreadOps.java"
"$JAR" cf "$FIXTURE_JAR" -C "$FIXTURE_CLASSES" .

SOURCES=()
while IFS= read -r source; do
  [[ "$source" == */api/CAPI.cpp ]] && continue
  [[ "$source" == */runtime/Runtime.cpp ]] && continue
  if [[ "$source" == "$CORE_ROOT/src/vm/"*Natives.cpp &&
        "$source" != "$CORE_ROOT/src/vm/CoreNatives.cpp" &&
        "$source" != "$CORE_ROOT/src/vm/HeadlessCompatNatives.cpp" &&
        "$source" != "$CORE_ROOT/src/vm/Micro3dNatives.cpp" &&
        "$source" != "$CORE_ROOT/src/vm/Micro3dMathNatives.cpp" &&
        "$source" != "$CORE_ROOT/src/vm/Micro3dRenderNatives.cpp" &&
        "$source" != "$CORE_ROOT/src/vm/Micro3dResourceNatives.cpp" &&
        "$source" != "$CORE_ROOT/src/vm/Micro3dStateNatives.cpp" ]]; then
    continue
  fi
  SOURCES+=("$source")
done < <(find "$CORE_ROOT/src" -type f -name '*.cpp' -print | LC_ALL=C sort)

"$CXX" \
  -std=c++23 \
  -isysroot "$SDK_ROOT" \
  -I"$CORE_ROOT/include" \
  -fno-exceptions \
  -fno-rtti \
  -Wall \
  -Wextra \
  -Wpedantic \
  -Wconversion \
  -Wsign-conversion \
  -Wshadow \
  -Werror=return-type \
  $SANITIZER_FLAGS \
  "$CORE_ROOT/Tests/SchedulerTests.cpp" \
  "${SOURCES[@]}" \
  -lz \
  -framework CoreText \
  -framework CoreGraphics \
  -framework ImageIO \
  -framework CoreFoundation \
  -o "$TEST_BINARY"

phoneme_run_with_timeout "${PHONEME_TEST_TIMEOUT:-600}" \
  "$TEST_BINARY" "$FIXTURE_JAR"
