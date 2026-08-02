#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_ROOT="$CORE_ROOT/build/push-host-tests"
TEST_BINARY="$BUILD_ROOT/PushRegistryTests"
CXX="$(xcrun --sdk macosx --find clang++)"
SDK_ROOT="$(xcrun --sdk macosx --show-sdk-path)"

rm -rf "$BUILD_ROOT"
mkdir -p "$BUILD_ROOT"

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
  -fsanitize=address,undefined \
  -fno-omit-frame-pointer \
  "$CORE_ROOT/Tests/PushRegistryTests.cpp" \
  "$CORE_ROOT/src/push/PushRegistry.cpp" \
  -o "$TEST_BINARY"

"$TEST_BINARY"
