#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
TEST_SOURCE="$CORE_ROOT/tests/CoreTests.cpp"
TEST_ROOT="${PHONEME_TEST_ROOT:-$CORE_ROOT/build/host-tests}"
TEST_BINARY="$TEST_ROOT/CoreTests"
FIXTURE_ROOT="$CORE_ROOT/tests/fixtures"
FIXTURE_RESOURCE="$FIXTURE_ROOT/corefixture/data.bin"
FIXTURE_MANIFEST="$FIXTURE_ROOT/MANIFEST.MF"
STUB_ROOT="$CORE_ROOT/tests/stubs"
STUB_CLASSES="$TEST_ROOT/compile-stubs"
FIXTURE_CLASSES="$TEST_ROOT/fixture-classes"
FIXTURE_JAR="$TEST_ROOT/core-fixture.jar"
CXX="$(xcrun --sdk macosx --find clang++)"
SDK_ROOT="$(xcrun --sdk macosx --show-sdk-path)"
JAVAC="${JAVAC:-$(command -v javac)}"
JAR="${JAR:-$(command -v jar)}"
SANITIZER_FLAGS=""
if [[ "${PHONEME_SANITIZE:-0}" == "1" ]]; then
  SANITIZER_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
fi

[[ -n "$JAVAC" && -n "$JAR" ]] || {
  echo "javac and jar are required for Core VM tests." >&2
  exit 1
}

rm -rf "$TEST_ROOT"
mkdir -p "$TEST_ROOT" "$STUB_CLASSES" "$FIXTURE_CLASSES"

# Stubs are compile-time only and are intentionally excluded from the JAR.
# Runtime resolution must therefore come from the C++ built-in class registry.
STUB_SOURCES=()
while IFS= read -r source; do
  STUB_SOURCES+=("$source")
done < <(find "$STUB_ROOT" -type f -name '*.java' -print | LC_ALL=C sort)

FIXTURE_SOURCES=()
while IFS= read -r source; do
  FIXTURE_SOURCES+=("$source")
done < <(find "$FIXTURE_ROOT" -maxdepth 1 -type f -name '*.java' -print | LC_ALL=C sort)

"$JAVAC" -source 8 -target 8 -Xlint:-options -Xlint:-unchecked \
  -d "$STUB_CLASSES" \
  "${STUB_SOURCES[@]}"
"$JAVAC" -source 8 -target 8 -Xlint:-options \
  -classpath "$STUB_CLASSES" \
  -d "$FIXTURE_CLASSES" \
  "${FIXTURE_SOURCES[@]}"
mkdir -p "$FIXTURE_CLASSES/corefixture"
cp "$FIXTURE_RESOURCE" "$FIXTURE_CLASSES/corefixture/data.bin"

"$JAR" cfm "$FIXTURE_JAR" "$FIXTURE_MANIFEST" \
  -C "$FIXTURE_CLASSES" .

if "$JAR" tf "$FIXTURE_JAR" | grep -q '^javax/microedition/'; then
  echo "Fixture JAR accidentally contains compile-time MIDP stubs." >&2
  exit 1
fi
if "$JAR" tf "$FIXTURE_JAR" | grep -q '^com/sun/midp/'; then
  echo "Fixture JAR accidentally contains compile-time internal stubs." >&2
  exit 1
fi

SOURCES=()
while IFS= read -r source; do
  [[ "$source" == */api/CAPI.cpp ]] && continue
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
  "$TEST_SOURCE" \
  "${SOURCES[@]}" \
  -lz \
  -framework CoreText \
  -framework CoreGraphics \
  -framework CoreFoundation \
  -o "$TEST_BINARY"

"$TEST_BINARY" "$FIXTURE_JAR"
