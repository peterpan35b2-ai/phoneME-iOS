#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
source "$SCRIPT_DIR/lib/common-test-root.sh"
TEST_ROOT="$(phoneme_make_isolated_root "$CORE_ROOT" "media-service-host-tests" "${PHONEME_MEDIA_SERVICE_TEST_ROOT:-}")"
phoneme_register_cleanup "$TEST_ROOT"
TEST_BINARY="$TEST_ROOT/MediaServiceTests"
CXX="$(xcrun --sdk macosx --find clang++)"
SDK_ROOT="$(xcrun --sdk macosx --show-sdk-path)"

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
  "$CORE_ROOT/Tests/MediaServiceTests.cpp" \
  "$CORE_ROOT/src/media/MediaService.cpp" \
  "$CORE_ROOT/src/media/PlatformMediaAdapter.cpp" \
  -o "$TEST_BINARY"

phoneme_run_with_timeout "${PHONEME_TEST_TIMEOUT:-60}" "$TEST_BINARY"
