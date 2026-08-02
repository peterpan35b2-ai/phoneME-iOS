#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
TEST_ROOT="${PHONEME_GRAPHICS_TEST_ROOT:-$CORE_ROOT/build/graphics-host-tests}"
TEST_BINARY="$TEST_ROOT/GraphicsModuleTests"
CXX="$(xcrun --sdk macosx --find clang++)"
SDK_ROOT="$(xcrun --sdk macosx --show-sdk-path)"
SANITIZER_FLAGS=""
if [[ "${PHONEME_SANITIZE:-0}" == "1" ]]; then
  SANITIZER_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
fi

rm -rf "$TEST_ROOT"
mkdir -p "$TEST_ROOT"

SOURCES=()
while IFS= read -r source; do
  SOURCES+=("$source")
done < <(find "$CORE_ROOT/src/graphics" -type f -name '*.cpp' -print | LC_ALL=C sort)

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
  "$CORE_ROOT/tests/GraphicsModuleTests.cpp" \
  "${SOURCES[@]}" \
  -lz \
  -framework CoreText \
  -framework CoreGraphics \
  -framework CoreFoundation \
  -o "$TEST_BINARY"

"$TEST_BINARY"
