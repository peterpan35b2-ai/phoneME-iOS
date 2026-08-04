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

OPENJDK8_HOME="${PHONEME_OPENJDK8_HOME:-}"
if [[ -z "$OPENJDK8_HOME" ]] && [[ -x /usr/libexec/java_home ]]; then
  OPENJDK8_HOME="$(/usr/libexec/java_home -v 1.8 2>/dev/null || true)"
fi
[[ -n "$OPENJDK8_HOME" && -x "$OPENJDK8_HOME/bin/java" ]] || {
  echo "OpenJDK 8 is required. Set PHONEME_OPENJDK8_HOME." >&2
  exit 1
}
JAVA="$OPENJDK8_HOME/bin/java"
JAVAC="$OPENJDK8_HOME/bin/javac"
JAR="$OPENJDK8_HOME/bin/jar"
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
SIGNATURE_ORACLE_SOURCE="$SOURCE_ROOT/compat/diff/OpenJdk8SignatureOracle.java"
CLASSES="$TEST_ROOT/classes"
FIXTURE_JAR="$TEST_ROOT/vm-differential.jar"
ORACLE_OUTPUT="$TEST_ROOT/openjdk-oracle.tsv"
NATIVE_COVERAGE="$TEST_ROOT/native-handler-coverage.tsv"
OPENJDK8_SIGNATURES="$TEST_ROOT/openjdk8-handler-signatures.tsv"
HARNESS="$TEST_ROOT/VmDifferentialHarness"

mkdir -p "$CLASSES"
"$JAVAC" -source 8 -target 8 -Xlint:-options -Xlint:-deprecation \
  -d "$CLASSES" \
  "$OPS_SOURCE" "$ORACLE_SOURCE" "$SIGNATURE_ORACLE_SOURCE"
"$JAR" cf "$FIXTURE_JAR" -C "$CLASSES" .

"$JAVA" -version > "$TEST_ROOT/openjdk-version.txt" 2>&1
if ! grep -q 'version "1\.8\.' "$TEST_ROOT/openjdk-version.txt"; then
  echo "Differential oracle is not OpenJDK 8:" >&2
  cat "$TEST_ROOT/openjdk-version.txt" >&2
  exit 1
fi
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
  -framework ImageIO \
  -framework CoreFoundation \
  -o "$HARNESS"

phoneme_run_with_timeout "${PHONEME_TEST_TIMEOUT:-300}" \
  "$HARNESS" "$FIXTURE_JAR" "$ORACLE_OUTPUT" "$NATIVE_COVERAGE"

"$JAVA" -cp "$CLASSES" compat.diff.OpenJdk8SignatureOracle \
  "$NATIVE_COVERAGE" "$OPENJDK8_SIGNATURES"

echo "Native handler coverage: $NATIVE_COVERAGE"
echo "OpenJDK 8 signature classification: $OPENJDK8_SIGNATURES"
