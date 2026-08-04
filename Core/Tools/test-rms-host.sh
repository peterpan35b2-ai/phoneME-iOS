#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
if [[ -f "$SCRIPT_DIR/lib/common-test-root.sh" ]]; then
  source "$SCRIPT_DIR/lib/common-test-root.sh"
else
  # Keep the RMS handoff runnable from its own historical checkpoint. The
  # shared tooling helper belongs to item 18 and may not be integrated yet.
  phoneme_make_isolated_root() {
    local core_root="$1"
    local label="$2"
    local override="${3:-}"
    local base="${override:-${TMPDIR:-/tmp}}"
    mkdir -p "$base"
    mktemp -d "$base/${label}.$$.XXXXXX"
  }
  phoneme_register_cleanup() {
    local root="$1"
    trap 'rm -rf -- '"'"'$root'"'"'' EXIT
  }
  phoneme_configure_sanitizers() {
    if [[ "${PHONEME_SANITIZE:-0}" == "1" ]]; then
      PHONEME_SANITIZER_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
    else
      PHONEME_SANITIZER_FLAGS=""
    fi
  }
  phoneme_run_with_timeout() {
    shift
    "$@"
  }
fi
TEST_ROOT="$(phoneme_make_isolated_root "$CORE_ROOT" "rms-host-tests" "${PHONEME_RMS_TEST_ROOT:-}")"
phoneme_register_cleanup "$TEST_ROOT"
STUB_CLASSES="$TEST_ROOT/compile-stubs"
FIXTURE_CLASSES="$TEST_ROOT/fixture-classes"
FIXTURE_JAR="$TEST_ROOT/rms-fixture.jar"
CRASH_HARNESS="$TEST_ROOT/RmsCrashHarness"
TEST_BINARY="$TEST_ROOT/RmsAdvancedTests"
TEST_DATA="$TEST_ROOT/data"
CXX="$(xcrun --sdk macosx --find clang++)"
SDK_ROOT="$(xcrun --sdk macosx --show-sdk-path)"
JAVAC="${JAVAC:-$(command -v javac)}"
JAR="${JAR:-$(command -v jar)}"
phoneme_configure_sanitizers
SANITIZER_FLAGS="$PHONEME_SANITIZER_FLAGS"

mkdir -p "$STUB_CLASSES" "$FIXTURE_CLASSES" "$TEST_DATA"

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
  "$CORE_ROOT/Tests/fixtures/RmsOps.java"
"$JAR" cf "$FIXTURE_JAR" -C "$FIXTURE_CLASSES" .

COMMON_FLAGS=(
  -std=c++23
  -isysroot "$SDK_ROOT"
  -I"$CORE_ROOT/include"
  -I"$CORE_ROOT/src/vm"
  -fno-exceptions
  -fno-rtti
  -Wall
  -Wextra
  -Wpedantic
  -Wconversion
  -Wsign-conversion
  -Wshadow
  -Werror=return-type
)

"$CXX" \
  "${COMMON_FLAGS[@]}" \
  $SANITIZER_FLAGS \
  "$CORE_ROOT/Tests/RmsCrashHarness.cpp" \
  "$CORE_ROOT/src/runtime/RecordStoreRegistry.cpp" \
  -lz \
  -o "$CRASH_HARNESS"

SOURCES=(
  "$CORE_ROOT/src/archive/ZipArchive.cpp"
  "$CORE_ROOT/src/classfile/BytecodeVerifier.cpp"
  "$CORE_ROOT/src/classfile/ClassFile.cpp"
  "$CORE_ROOT/src/filesystem/FileSystem.cpp"
  "$CORE_ROOT/src/filesystem/ResourceLoader.cpp"
  "$CORE_ROOT/src/media/MediaService.cpp"
  "$CORE_ROOT/src/media/PlatformMediaAdapter.cpp"
  "$CORE_ROOT/src/network/ConnectionRegistry.cpp"
  "$CORE_ROOT/src/network/Url.cpp"
  "$CORE_ROOT/src/platform/MappedFile.cpp"
  "$CORE_ROOT/src/push/PushRegistry.cpp"
  "$CORE_ROOT/src/runtime/CanvasRuntime.cpp"
  "$CORE_ROOT/src/runtime/Framebuffer.cpp"
  "$CORE_ROOT/src/runtime/RecordStoreRegistry.cpp"
  "$CORE_ROOT/src/translation/TranslationService.cpp"
  "$CORE_ROOT/src/security/PermissionCatalog.cpp"
  "$CORE_ROOT/src/security/PermissionPolicy.cpp"
)
if [[ -f "$CORE_ROOT/src/filesystem/SandboxResolver.cpp" ]]; then
  SOURCES+=("$CORE_ROOT/src/filesystem/SandboxResolver.cpp")
fi
while IFS= read -r source; do
  SOURCES+=("$source")
done < <(find "$CORE_ROOT/src/graphics" -type f -name '*.cpp' -print | LC_ALL=C sort)
while IFS= read -r source; do
  SOURCES+=("$source")
done < <(find "$CORE_ROOT/src/vm" -type f -name '*.cpp' -print | LC_ALL=C sort)

"$CXX" \
  "${COMMON_FLAGS[@]}" \
  $SANITIZER_FLAGS \
  "$CORE_ROOT/Tests/RmsAdvancedTests.cpp" \
  "${SOURCES[@]}" \
  -lz \
  -framework CoreText \
  -framework CoreGraphics \
  -framework ImageIO \
  -framework CoreFoundation \
  -o "$TEST_BINARY"

phoneme_run_with_timeout "${PHONEME_TEST_TIMEOUT:-600}" \
  "$TEST_BINARY" "$FIXTURE_JAR" "$CRASH_HARNESS" "$TEST_DATA"
