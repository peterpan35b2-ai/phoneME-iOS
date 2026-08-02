#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
if [[ -n "${PHONEME_SCHEDULER_TEST_ROOT:-}" ]]; then
  TEST_ROOT="$PHONEME_SCHEDULER_TEST_ROOT"
  rm -rf "$TEST_ROOT"
  mkdir -p "$TEST_ROOT"
else
  TEST_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/phoneme-scheduler-01.XXXXXX")"
fi
if [[ "${PHONEME_KEEP_TEST_ROOT:-0}" != "1" ]]; then
  trap 'rm -rf "$TEST_ROOT"' EXIT
fi
TEST_BINARY="$TEST_ROOT/SchedulerTests"
FIXTURE_CLASSES="$TEST_ROOT/fixture-classes"
FIXTURE_JAR="$TEST_ROOT/scheduler-fixture.jar"
CXX="$(xcrun --sdk macosx --find clang++)"
SDK_ROOT="$(xcrun --sdk macosx --show-sdk-path)"
JAVAC="${JAVAC:-$(command -v javac)}"
JAR="${JAR:-$(command -v jar)}"
SANITIZER_FLAGS=""
case "${PHONEME_SANITIZER:-}" in
  ''|none)
    if [[ "${PHONEME_SANITIZE:-0}" == "1" ]]; then
      SANITIZER_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
    fi
    ;;
  asan|address)
    SANITIZER_FLAGS="-fsanitize=address -fno-omit-frame-pointer"
    ;;
  ubsan|undefined)
    SANITIZER_FLAGS="-fsanitize=undefined -fno-omit-frame-pointer"
    ;;
  asan-ubsan|address-undefined)
    SANITIZER_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
    ;;
  tsan|thread)
    SANITIZER_FLAGS="-fsanitize=thread -fno-omit-frame-pointer"
    ;;
  *)
    echo "unknown sanitizer mode: ${PHONEME_SANITIZER}" >&2
    exit 64
    ;;
esac

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
        "$source" != "$CORE_ROOT/src/vm/CoreNatives.cpp" ]]; then
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
  -framework CoreFoundation \
  -o "$TEST_BINARY"

"$TEST_BINARY" "$FIXTURE_JAR"
