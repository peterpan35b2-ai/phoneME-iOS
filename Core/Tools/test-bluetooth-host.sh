#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
source "$SCRIPT_DIR/lib/common-test-root.sh"
TEST_ROOT="$(phoneme_make_isolated_root "$CORE_ROOT" "bluetooth-host-tests" "${PHONEME_BLUETOOTH_TEST_ROOT:-}")"
phoneme_register_cleanup "$TEST_ROOT"
STUB_CLASSES="$TEST_ROOT/compile-stubs"
FIXTURE_CLASSES="$TEST_ROOT/fixture-classes"
FIXTURE_JAR="$TEST_ROOT/bluetooth-fixture.jar"
TEST_BINARY="$TEST_ROOT/BluetoothVmTests"
CXX="$(xcrun --sdk macosx --find clang++)"
SDK_ROOT="$(xcrun --sdk macosx --show-sdk-path)"
JAVAC="${JAVAC:-$(command -v javac)}"
JAR="${JAR:-$(command -v jar)}"
phoneme_configure_sanitizers
SANITIZER_FLAGS="$PHONEME_SANITIZER_FLAGS"

mkdir -p "$STUB_CLASSES" "$FIXTURE_CLASSES"

STUB_SOURCES=()
while IFS= read -r source; do
  STUB_SOURCES+=("$source")
done < <(find "$CORE_ROOT/Tests/stubs" -type f -name '*.java' -print | LC_ALL=C sort)

"$JAVAC" -source 8 -target 8 -Xlint:-options -Xlint:-unchecked \
  -d "$STUB_CLASSES" "${STUB_SOURCES[@]}"
"$JAVAC" -source 8 -target 8 -Xlint:-options -Xlint:-unchecked \
  -classpath "$STUB_CLASSES" -d "$FIXTURE_CLASSES" \
  "$CORE_ROOT/Tests/fixtures/BluetoothOps.java"
"$JAR" cf "$FIXTURE_JAR" -C "$FIXTURE_CLASSES" .

SOURCES=()
while IFS= read -r source; do
  case "$source" in
    */api/CAPI.cpp|*/network/PosixNetworkAdapter.cpp) continue ;;
  esac
  SOURCES+=("$source")
done < <(find "$CORE_ROOT/src" -type f -name '*.cpp' -print | LC_ALL=C sort)

"$CXX" -std=c++23 -isysroot "$SDK_ROOT" \
  -I"$CORE_ROOT/include" -I"$CORE_ROOT/src/vm" \
  -fno-exceptions -fno-rtti \
  -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow \
  -Werror=return-type $SANITIZER_FLAGS \
  "$CORE_ROOT/Tests/BluetoothVmTests.cpp" "${SOURCES[@]}" \
  -lz -framework CoreText -framework CoreGraphics -framework ImageIO -framework CoreFoundation \
  -o "$TEST_BINARY"

phoneme_run_with_timeout "${PHONEME_TEST_TIMEOUT:-300}" \
  "$TEST_BINARY" "$FIXTURE_JAR"
