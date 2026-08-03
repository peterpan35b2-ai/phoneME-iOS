#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
source "$SCRIPT_DIR/lib/common-test-root.sh"
TEST_ROOT="$(phoneme_make_isolated_root "$CORE_ROOT" "c-api-host-tests" "${PHONEME_C_API_TEST_ROOT:-}")"
phoneme_register_cleanup "$TEST_ROOT"
TEST_BINARY="$TEST_ROOT/CApiHeaderTests"
CC="$(xcrun --sdk macosx --find clang)"
SDK_ROOT="$(xcrun --sdk macosx --show-sdk-path)"

"$CC" \
  -std=c11 \
  -isysroot "$SDK_ROOT" \
  -I"$CORE_ROOT/include" \
  -Wall \
  -Wextra \
  -Wpedantic \
  -Wconversion \
  -Wsign-conversion \
  -Werror \
  "$CORE_ROOT/Tests/CApiHeaderTests.c" \
  -o "$TEST_BINARY"

phoneme_run_with_timeout "${PHONEME_TEST_TIMEOUT:-300}" "$TEST_BINARY"
printf 'C API header compatibility tests passed\n'
