#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
PROJECT_ROOT="$(cd "$CORE_ROOT/.." && pwd)"
source "$CORE_ROOT/Tools/lib/common-test-root.sh"

REFERENCE_ROOT="${PHONEME_REFERENCE_ROOT:-$(cd "$PROJECT_ROOT/.." && pwd)/phoneME}"
REFERENCE_OUTPUT="${PHONEME_REFERENCE_OUTPUT:-$REFERENCE_ROOT/midp/build/darwin_sdl3_gcc/output}"
REFERENCE_CLASSES="${PHONEME_REFERENCE_CLASSES:-$REFERENCE_OUTPUT/classes.zip}"
REFERENCE_PREVERIFY="${PHONEME_REFERENCE_PREVERIFY:-$REFERENCE_OUTPUT/bin/arm64/preverify}"
REFERENCE_RUNNER="${PHONEME_REFERENCE_RUNNER:-$REFERENCE_OUTPUT/bin/arm64/runMidlet}"
REFERENCE_TEMPLATE="${PHONEME_REFERENCE_TEMPLATE:-$REFERENCE_ROOT/dist/macos/phoneME.app/Contents/Resources/runtime-template}"

TEST_ROOT="$(phoneme_make_isolated_root \
  "$CORE_ROOT" \
  "javame-differential" \
  "${PHONEME_JAVAME_DIFFERENTIAL_TEST_ROOT:-}")"
phoneme_register_cleanup "$TEST_ROOT"
phoneme_configure_sanitizers

if [[ -n "${JAVA8_HOME:-}" ]]; then
  JDK8="$JAVA8_HOME"
elif [[ "$(uname -s)" == "Darwin" ]]; then
  JDK8="$(/usr/libexec/java_home -v 1.8 2>/dev/null || true)"
else
  JDK8=""
fi

[[ -x "$JDK8/bin/javac" && -x "$JDK8/bin/jar" ]] || {
  echo "A JDK 8 installation is required to emit CLDC-compatible classfile 48 bytecode." >&2
  echo "Set JAVA8_HOME to a JDK 8 home." >&2
  exit 2
}
for path in "$REFERENCE_CLASSES" "$REFERENCE_PREVERIFY" "$REFERENCE_RUNNER"; do
  [[ -e "$path" ]] || {
    echo "phoneME reference artifact is missing: $path" >&2
    echo "Set PHONEME_REFERENCE_ROOT or the individual PHONEME_REFERENCE_* variables." >&2
    exit 2
  }
done
[[ -d "$REFERENCE_TEMPLATE/appdb" && -d "$REFERENCE_TEMPLATE/lib" ]] || {
  echo "phoneME reference runtime template is missing: $REFERENCE_TEMPLATE" >&2
  exit 2
}

SOURCE="$SCRIPT_DIR/fixtures/src/compat/javame/JavaMeDifferentialMIDlet.java"
CLASSES="$TEST_ROOT/classes"
PREVERIFIED="$TEST_ROOT/preverified"
FIXTURE_JAR="$TEST_ROOT/javame-differential.jar"
MANIFEST="$TEST_ROOT/MANIFEST.MF"
REFERENCE_HOME="$TEST_ROOT/reference-home"
CPP_HOME="$TEST_ROOT/cpp-home"
REFERENCE_RAW="$TEST_ROOT/reference.raw.log"
CPP_RAW="$TEST_ROOT/cpp.raw.log"
REFERENCE_RESULTS="$TEST_ROOT/reference.tsv"
CPP_RESULTS="$TEST_ROOT/cpp.tsv"
HARNESS="$TEST_ROOT/CompatibilityHarness"

mkdir -p "$CLASSES" "$PREVERIFIED" "$REFERENCE_HOME" "$CPP_HOME"
cp -R "$REFERENCE_TEMPLATE/appdb" "$REFERENCE_HOME/appdb"
cp -R "$REFERENCE_TEMPLATE/lib" "$REFERENCE_HOME/lib"

"$JDK8/bin/javac" \
  -source 1.4 \
  -target 1.4 \
  -Xlint:-options \
  -classpath "$REFERENCE_CLASSES" \
  -d "$CLASSES" \
  "$SOURCE"

"$REFERENCE_PREVERIFY" \
  -classpath "$REFERENCE_CLASSES" \
  -d "$PREVERIFIED" \
  "$CLASSES" \
  >"$TEST_ROOT/preverify.log" 2>&1

cat > "$MANIFEST" <<'EOF'
Manifest-Version: 1.0
MIDlet-Name: JavaME Differential
MIDlet-Version: 1.0.0
MIDlet-Vendor: phoneME C++ Tests
MicroEdition-Configuration: CLDC-1.1
MicroEdition-Profile: MIDP-2.0
MIDlet-1: JavaME Differential,,compat.javame.JavaMeDifferentialMIDlet
EOF
"$JDK8/bin/jar" cfm "$FIXTURE_JAR" "$MANIFEST" -C "$PREVERIFIED" .

REFERENCE_CLASSPATH="$REFERENCE_CLASSES:$FIXTURE_JAR"
MIDP_HOME="$REFERENCE_HOME" \
PHONEME_SCALE=1 \
phoneme_run_with_timeout "${PHONEME_REFERENCE_TIMEOUT:-60}" \
  "$REFERENCE_RUNNER" \
  =HeapCapacity64M \
  -classpathext "$REFERENCE_CLASSPATH" \
  -1 compat.javame.JavaMeDifferentialMIDlet \
  >"$REFERENCE_RAW" 2>&1

grep $'^JME_DIFF\t' "$REFERENCE_RAW" > "$REFERENCE_RESULTS" || {
  echo "The phoneME C reference produced no Java ME differential records." >&2
  cat "$REFERENCE_RAW" >&2
  exit 1
}

CXX="${CXX:-$(xcrun --sdk macosx --find clang++)}"
SDK_ROOT="$(xcrun --sdk macosx --show-sdk-path)"
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
  $PHONEME_SANITIZER_FLAGS \
  "$CORE_ROOT/Tests/Compatibility/CompatibilityHarness.cpp" \
  "${SOURCES[@]}" \
  -lz \
  -framework CoreText \
  -framework CoreGraphics \
  -framework ImageIO \
  -framework CoreFoundation \
  -o "$HARNESS"

phoneme_run_with_timeout "${PHONEME_TEST_TIMEOUT:-180}" \
  "$HARNESS" \
  --jar "$FIXTURE_JAR" \
  --main compat.javame.JavaMeDifferentialMIDlet \
  --runtime-home "$CPP_HOME" \
  --result "$TEST_ROOT/cpp-result.json" \
  --frame "$TEST_ROOT/cpp-frame.ppm" \
  --width 320 \
  --height 240 \
  >"$CPP_RAW" 2>&1

grep $'^JME_DIFF\t' "$CPP_RAW" > "$CPP_RESULTS" || {
  echo "The C++ Runtime produced no Java ME differential records." >&2
  cat "$CPP_RAW" >&2
  [[ -f "$TEST_ROOT/cpp-result.json" ]] && cat "$TEST_ROOT/cpp-result.json" >&2
  exit 1
}

if ! diff -u "$REFERENCE_RESULTS" "$CPP_RESULTS"; then
  echo "Java ME differential mismatch. Raw logs:" >&2
  echo "  phoneME C: $REFERENCE_RAW" >&2
  echo "  C++ Runtime: $CPP_RAW" >&2
  exit 1
fi

COUNT="$(wc -l < "$CPP_RESULTS" | tr -d ' ')"
echo "Java ME differential summary: $COUNT/$COUNT matched"
echo "Reference runtime: $REFERENCE_RUNNER"
echo "CLDC classfile: major version 48, preverified by phoneME"
