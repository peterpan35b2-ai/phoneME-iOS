#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
source "$SCRIPT_DIR/lib/common-test-root.sh"
TEST_ROOT="$(phoneme_make_isolated_root "$CORE_ROOT" "filesystem-host-tests" "${PHONEME_FILESYSTEM_TEST_ROOT:-}")"
phoneme_register_cleanup "$TEST_ROOT"
BUILD_ROOT="$TEST_ROOT/build"
RUNTIME_ROOT="$TEST_ROOT/runtime"
RESOURCE_ROOT="$TEST_ROOT/resources"
RESOURCE_JAR="$TEST_ROOT/resources.jar"
CXX="${CXX:-$(xcrun --sdk macosx --find clang++)}"
SDK_ROOT="$(xcrun --sdk macosx --show-sdk-path)"
phoneme_configure_sanitizers
SANITIZER_FLAGS="$PHONEME_SANITIZER_FLAGS"

mkdir -p "$BUILD_ROOT" "$RUNTIME_ROOT" "$RESOURCE_ROOT/pkg"
printf '%s' 'relative-data' > "$RESOURCE_ROOT/pkg/data.bin"
printf '%s' 'unicode-data' > "$RESOURCE_ROOT/pkg/tên-☃.bin"
python3 - "$RESOURCE_ROOT" "$RESOURCE_JAR" <<'PY'
import pathlib
import sys
import zipfile

root = pathlib.Path(sys.argv[1])
archive = pathlib.Path(sys.argv[2])
with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as output:
    for path in sorted(root.rglob("*")):
        if path.is_file():
            output.write(path, path.relative_to(root).as_posix())
PY

COMMON_FLAGS=(
  -std=c++23
  -isysroot "$SDK_ROOT"
  -Wall
  -Wextra
  -Wpedantic
  -Werror
  -I "$CORE_ROOT/include"
)
SOURCES=(
  "$CORE_ROOT/Tests/FileSystemSecurityTests.cpp"
  "$CORE_ROOT/src/filesystem/FileSystem.cpp"
  "$CORE_ROOT/src/filesystem/SandboxResolver.cpp"
  "$CORE_ROOT/src/filesystem/ResourceLoader.cpp"
  "$CORE_ROOT/src/archive/ZipArchive.cpp"
  "$CORE_ROOT/src/platform/MappedFile.cpp"
)

"$CXX" "${COMMON_FLAGS[@]}" $SANITIZER_FLAGS "${SOURCES[@]}" \
  -pthread -lz -o "$BUILD_ROOT/filesystem-security-tests"

ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0:halt_on_error=1}" \
UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}" \
  phoneme_run_with_timeout "${PHONEME_TEST_TIMEOUT:-300}" \
    "$BUILD_ROOT/filesystem-security-tests" "$RUNTIME_ROOT" "$RESOURCE_JAR"

JAVAC="${JAVAC:-$(command -v javac)}"
JAR="${JAR:-$(command -v jar)}"
[[ -n "$JAVAC" && -n "$JAR" ]] || {
  echo "javac and jar are required for filesystem VM tests." >&2
  exit 1
}

STUB_CLASSES="$TEST_ROOT/stub-classes"
FIXTURE_CLASSES="$TEST_ROOT/fixture-classes"
FIXTURE_JAR="$TEST_ROOT/file-fixture.jar"
mkdir -p "$STUB_CLASSES" "$FIXTURE_CLASSES"
STUB_SOURCES=(
  "$CORE_ROOT/Tests/stubs/javax/microedition/io/Connection.java"
  "$CORE_ROOT/Tests/stubs/javax/microedition/io/InputConnection.java"
  "$CORE_ROOT/Tests/stubs/javax/microedition/io/OutputConnection.java"
  "$CORE_ROOT/Tests/stubs/javax/microedition/io/StreamConnection.java"
  "$CORE_ROOT/Tests/stubs/javax/microedition/io/Connector.java"
  "$CORE_ROOT/Tests/stubs/javax/microedition/io/file/FileConnection.java"
  "$CORE_ROOT/Tests/stubs/javax/microedition/io/file/FileSystemListener.java"
  "$CORE_ROOT/Tests/stubs/javax/microedition/io/file/FileSystemRegistry.java"
  "$CORE_ROOT/Tests/stubs/javax/microedition/io/file/ConnectionClosedException.java"
  "$CORE_ROOT/Tests/stubs/javax/microedition/io/file/IllegalModeException.java"
)
"$JAVAC" -source 8 -target 8 -Xlint:-options -Xlint:-unchecked \
  -d "$STUB_CLASSES" "${STUB_SOURCES[@]}"
"$JAVAC" -source 8 -target 8 -Xlint:-options \
  -classpath "$STUB_CLASSES" -d "$FIXTURE_CLASSES" \
  "$CORE_ROOT/Tests/fixtures/FileOps.java"
mkdir -p "$FIXTURE_CLASSES/corefixture"
cp "$CORE_ROOT/Tests/fixtures/corefixture/data.bin" \
  "$FIXTURE_CLASSES/corefixture/data.bin"
"$JAR" cf "$FIXTURE_JAR" -C "$FIXTURE_CLASSES" .

VM_SOURCES=()
while IFS= read -r source; do
  [[ "$source" == */api/CAPI.cpp ]] && continue
  VM_SOURCES+=("$source")
done < <(find "$CORE_ROOT/src" -type f -name '*.cpp' -print | LC_ALL=C sort)

"$CXX" \
  -std=c++23 \
  -isysroot "$SDK_ROOT" \
  -I "$CORE_ROOT/include" \
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
  "$CORE_ROOT/Tests/FileSystemVmTests.cpp" \
  "${VM_SOURCES[@]}" \
  -pthread -lz \
  -framework CoreText \
  -framework CoreGraphics \
  -framework ImageIO \
  -framework CoreFoundation \
  -o "$BUILD_ROOT/filesystem-vm-tests"

ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0:halt_on_error=1}" \
UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}" \
  phoneme_run_with_timeout "${PHONEME_TEST_TIMEOUT:-300}" \
    "$BUILD_ROOT/filesystem-vm-tests" "$FIXTURE_JAR" \
    "$RUNTIME_ROOT/vm"
