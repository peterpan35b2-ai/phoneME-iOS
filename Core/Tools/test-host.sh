#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
source "$SCRIPT_DIR/lib/common-test-root.sh"
TEST_ROOT="$(phoneme_make_isolated_root "$CORE_ROOT" "host-tests" "${PHONEME_TEST_ROOT:-}")"
phoneme_register_cleanup "$TEST_ROOT"
TEST_SOURCE="$CORE_ROOT/Tests/CoreTests.cpp"
TEST_BINARY="$TEST_ROOT/CoreTests"
FIXTURE_ROOT="$CORE_ROOT/Tests/fixtures"
FIXTURE_RESOURCE="$FIXTURE_ROOT/corefixture/data.bin"
FIXTURE_MANIFEST="$FIXTURE_ROOT/MANIFEST.MF"
STUB_ROOT="$CORE_ROOT/Tests/stubs"
STUB_CLASSES="$TEST_ROOT/compile-stubs"
LINKAGE_STUB_ROOT="$CORE_ROOT/Tests/linkage-stubs"
LINKAGE_STUB_CLASSES="$TEST_ROOT/linkage-stubs"
FIXTURE_CLASSES="$TEST_ROOT/fixture-classes"
FIXTURE_JAR="$TEST_ROOT/core-fixture.jar"
CXX="$(xcrun --sdk macosx --find clang++)"
SDK_ROOT="$(xcrun --sdk macosx --show-sdk-path)"
JAVAC="${JAVAC:-$(command -v javac)}"
JAR="${JAR:-$(command -v jar)}"
phoneme_configure_sanitizers
SANITIZER_FLAGS="$PHONEME_SANITIZER_FLAGS"

[[ -n "$JAVAC" && -n "$JAR" ]] || {
  echo "javac and jar are required for Core VM tests." >&2
  exit 1
}

mkdir -p "$STUB_CLASSES" "$LINKAGE_STUB_CLASSES" "$FIXTURE_CLASSES"

# Stubs are compile-time only and are intentionally excluded from the JAR.
# Runtime resolution must therefore come from the C++ built-in class registry.
STUB_SOURCES=()
while IFS= read -r source; do
  STUB_SOURCES+=("$source")
done < <(find "$STUB_ROOT" -type f -name '*.java' -print | LC_ALL=C sort)

FIXTURE_SOURCES=()
case "${PHONEME_TEST_FILTER:-}" in
  xml)
    FIXTURE_SOURCES=(
      "$FIXTURE_ROOT/XmlOps.java"
    )
    ;;
  security)
    FIXTURE_SOURCES=(
      "$FIXTURE_ROOT/Arithmetic.java"
      "$FIXTURE_ROOT/Exceptions.java"
      "$FIXTURE_ROOT/LifecycleApp.java"
      "$FIXTURE_ROOT/SecurityOps.java"
      "$FIXTURE_ROOT/FileOps.java"
      "$FIXTURE_ROOT/NetworkOps.java"
      "$FIXTURE_ROOT/MediaOps.java"
    )
    ;;
  *)
    while IFS= read -r source; do
      FIXTURE_SOURCES+=("$source")
    done < <(find "$FIXTURE_ROOT" -maxdepth 1 -type f -name '*.java' -print | LC_ALL=C sort)
    ;;
esac

LINKAGE_FIXTURES_ENABLED=0
REGULAR_FIXTURE_SOURCES=()
for source in "${FIXTURE_SOURCES[@]}"; do
  case "$source" in
    "$FIXTURE_ROOT/LinkageOps.java"|"$FIXTURE_ROOT/LinkageTarget.java")
      LINKAGE_FIXTURES_ENABLED=1
      ;;
    *)
      REGULAR_FIXTURE_SOURCES+=("$source")
      ;;
  esac
done
FIXTURE_SOURCES=("${REGULAR_FIXTURE_SOURCES[@]}")

"$JAVAC" -source 8 -target 8 -Xlint:-options -Xlint:-unchecked \
  -d "$STUB_CLASSES" \
  "${STUB_SOURCES[@]}"
if [[ "${#FIXTURE_SOURCES[@]}" -gt 0 ]]; then
  "$JAVAC" -source 8 -target 8 -Xlint:-options \
    -classpath "$STUB_CLASSES" \
    -d "$FIXTURE_CLASSES" \
    "${FIXTURE_SOURCES[@]}"
fi

if [[ "$LINKAGE_FIXTURES_ENABLED" -eq 1 ]]; then
  LINKAGE_STUB_SOURCES=()
  while IFS= read -r source; do
    LINKAGE_STUB_SOURCES+=("$source")
  done < <(find "$LINKAGE_STUB_ROOT" -type f -name '*.java' -print | LC_ALL=C sort)
  "$JAVAC" -source 8 -target 8 -Xlint:-options \
    -d "$LINKAGE_STUB_CLASSES" \
    "${LINKAGE_STUB_SOURCES[@]}"
  "$JAVAC" -source 8 -target 8 -Xlint:-options \
    -classpath "$STUB_CLASSES:$LINKAGE_STUB_CLASSES" \
    -d "$FIXTURE_CLASSES" \
    "$FIXTURE_ROOT/LinkageOps.java"
  "$JAVAC" -source 8 -target 8 -Xlint:-options \
    -classpath "$STUB_CLASSES" \
    -d "$FIXTURE_CLASSES" \
    "$FIXTURE_ROOT/LinkageTarget.java"
fi
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
if "$JAR" tf "$FIXTURE_JAR" | grep -q '^corefixture/LinkageMissing.class$'; then
  echo "Fixture JAR accidentally contains the missing-linkage compile stub." >&2
  exit 1
fi

SOURCES=()
while IFS= read -r source; do
  [[ "$source" == */api/CAPI.cpp ]] && continue
  SOURCES+=("$source")
done < <(find "$CORE_ROOT/src" -type f -name '*.cpp' -print | LC_ALL=C sort)

"$CXX" \
  -std=c++23 \
  -DPHONEME_ENABLE_VM_PROFILING="${PHONEME_ENABLE_VM_PROFILING:-0}" \
  -DPHONEME_ENABLE_DECODED_EXECUTION="${PHONEME_ENABLE_DECODED_EXECUTION:-0}" \
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
  -framework ImageIO \
  -framework CoreFoundation \
  -o "$TEST_BINARY"

phoneme_run_with_timeout "${PHONEME_TEST_TIMEOUT:-300}" \
  "$TEST_BINARY" "$FIXTURE_JAR"
