#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
source "$SCRIPT_DIR/lib/common-test-root.sh"
TEST_ROOT="$(phoneme_make_isolated_root "$CORE_ROOT" "canvas-graphics-host-tests" "${PHONEME_CANVAS_GRAPHICS_TEST_ROOT:-}")"
phoneme_register_cleanup "$TEST_ROOT"
STUB_CLASSES="$TEST_ROOT/compile-stubs"
FIXTURE_CLASSES="$TEST_ROOT/fixture-classes"
FIXTURE_JAR="$TEST_ROOT/canvas-graphics-fixture.jar"
TEST_BINARY="$TEST_ROOT/CanvasGraphicsRuntimeTests"
CXX="$(xcrun --sdk macosx --find clang++)"
SDK_ROOT="$(xcrun --sdk macosx --show-sdk-path)"
JAVAC="${JAVAC:-$(command -v javac)}"
JAR="${JAR:-$(command -v jar)}"
phoneme_configure_sanitizers
SANITIZER_FLAGS="$PHONEME_SANITIZER_FLAGS"

[[ -n "$JAVAC" && -n "$JAR" ]] || {
  echo "javac and jar are required for Canvas graphics tests." >&2
  exit 1
}

mkdir -p "$STUB_CLASSES" "$FIXTURE_CLASSES"

STUB_SOURCES=()
while IFS= read -r source; do
  STUB_SOURCES+=("$source")
done < <(find "$CORE_ROOT/Tests/stubs" -type f -name '*.java' -print | LC_ALL=C sort)

"$JAVAC" -source 8 -target 8 -Xlint:-options -Xlint:-unchecked \
  -d "$STUB_CLASSES" \
  "${STUB_SOURCES[@]}"
"$JAVAC" -source 8 -target 8 -Xlint:-options \
  -classpath "$STUB_CLASSES" \
  -d "$FIXTURE_CLASSES" \
  "$CORE_ROOT/Tests/fixtures/CanvasOps.java" \
  "$CORE_ROOT/Tests/fixtures/CanvasCopyAreaOps.java" \
  "$CORE_ROOT/Tests/fixtures/CanvasRaceOps.java" \
  "$CORE_ROOT/Tests/fixtures/CanvasEventOps.java" \
  "$CORE_ROOT/Tests/fixtures/CanvasSuppressOps.java" \
  "$CORE_ROOT/Tests/fixtures/CanvasThrowOps.java"
"$JAR" cfm "$FIXTURE_JAR" \
  "$CORE_ROOT/Tests/fixtures/CanvasGraphics.MF" \
  -C "$FIXTURE_CLASSES" .

SOURCES=()
while IFS= read -r source; do
  case "$source" in
    */api/CAPI.cpp|*/vm/ConnectionNatives.cpp|*/vm/MediaNatives.cpp|*/vm/GameApiNatives.cpp|*/network/PosixNetworkAdapter.cpp)
      continue
      ;;
  esac
  SOURCES+=("$source")
done < <(find "$CORE_ROOT/src" -type f -name '*.cpp' -print | LC_ALL=C sort)

"$CXX" \
  -std=c++23 \
  -isysroot "$SDK_ROOT" \
  -I"$CORE_ROOT/include" \
  -I"$CORE_ROOT/src/vm" \
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
  "$CORE_ROOT/Tests/CanvasGraphicsRuntimeTests.cpp" \
  "${SOURCES[@]}" \
  -lz \
  -framework CoreText \
  -framework CoreGraphics \
  -framework CoreFoundation \
  -framework ImageIO \
  -o "$TEST_BINARY"

phoneme_run_with_timeout "${PHONEME_TEST_TIMEOUT:-300}" \
  "$TEST_BINARY" "$FIXTURE_JAR"
