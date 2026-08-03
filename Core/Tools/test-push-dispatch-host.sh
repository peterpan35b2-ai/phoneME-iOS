#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
source "$SCRIPT_DIR/lib/common-test-root.sh"
BUILD_ROOT="$(phoneme_make_isolated_root "$CORE_ROOT" "push-dispatch-host-tests" "${PHONEME_PUSH_DISPATCH_TEST_ROOT:-}")"
phoneme_register_cleanup "$BUILD_ROOT"
TEST_BINARY="$BUILD_ROOT/PushDispatcherTests"
CXX="$(xcrun --sdk macosx --find clang++)"
SDK_ROOT="$(xcrun --sdk macosx --show-sdk-path)"
phoneme_configure_sanitizers

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
  "$CORE_ROOT/Tests/PushDispatcherTests.cpp" \
  "$CORE_ROOT/src/push/PushRegistry.cpp" \
  "$CORE_ROOT/src/push/PushDispatcher.cpp" \
  -o "$TEST_BINARY"

phoneme_run_with_timeout "${PHONEME_TEST_TIMEOUT:-300}" "$TEST_BINARY"
