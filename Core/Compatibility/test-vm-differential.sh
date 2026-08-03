#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
source "$CORE_ROOT/Tools/lib/common-test-root.sh"
TEST_ROOT="$(phoneme_make_isolated_root \
  "$CORE_ROOT" \
  "vm-differential" \
  "${PHONEME_VM_DIFFERENTIAL_TEST_ROOT:-}")"
phoneme_register_cleanup "$TEST_ROOT"
phoneme_configure_sanitizers

JAVA="${JAVA:-$(command -v java)}"
JAVAC="${JAVAC:-$(command -v javac)}"
JAR="${JAR:-$(command -v jar)}"
CXX="$(xcrun --sdk macosx --find clang++)"
SDK_ROOT="$(xcrun --sdk macosx --show-sdk-path)"
SANITIZER_FLAGS="$PHONEME_SANITIZER_FLAGS"

[[ -n "$JAVA" && -n "$JAVAC" && -n "$JAR" ]] || {
  echo "java, javac and jar are required for VM differential tests." >&2
  exit 1
}

SOURCE_ROOT="$SCRIPT_DIR/fixtures/src"
OPS_SOURCE="$SOURCE_ROOT/compat/diff/VmDifferentialOps.java"
ORACLE_SOURCE="$SOURCE_ROOT/compat/diff/VmDifferentialOracle.java"
CLASSES="$TEST_ROOT/classes"
FIXTURE_JAR="$TEST_ROOT/vm-differential.jar"
ORACLE_OUTPUT="$TEST_ROOT/openjdk-oracle.tsv"
HARNESS="$TEST_ROOT/VmDifferentialHarness"

mkdir -p "$CLASSES"
"$JAVAC" -source 8 -target 8 -Xlint:-options -Xlint:-deprecation \
  -d "$CLASSES" \
  "$OPS_SOURCE" "$ORACLE_SOURCE"
"$JAR" cf "$FIXTURE_JAR" -C "$CLASSES" .

"$JAVA" -version > "$TEST_ROOT/openjdk-version.txt" 2>&1
"$JAVA" -cp "$CLASSES" compat.diff.VmDifferentialOracle > "$ORACLE_OUTPUT"

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
  "$CORE_ROOT/Tests/Compatibility/VmDifferentialHarness.cpp" \
  "${SOURCES[@]}" \
  -lz \
  -framework CoreText \
  -framework CoreGraphics \
  -framework CoreFoundation \
  -o "$HARNESS"

phoneme_run_with_timeout "${PHONEME_TEST_TIMEOUT:-300}" \
  "$HARNESS" "$FIXTURE_JAR" "$ORACLE_OUTPUT"
