#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
source "$SCRIPT_DIR/lib/common-test-root.sh"

TEST_ROOT="$(phoneme_make_isolated_root \
  "$CORE_ROOT" \
  "security-integration-host" \
  "${PHONEME_SECURITY_TEST_ROOT:-}")"
phoneme_register_cleanup "$TEST_ROOT"
TEST_BINARY="$TEST_ROOT/SecurityIntegrationTests"
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
  -pthread \
  $PHONEME_SANITIZER_FLAGS \
  "$CORE_ROOT/Tests/SecurityIntegrationTests.cpp" \
  "$CORE_ROOT/src/security/PermissionPolicy.cpp" \
  "$CORE_ROOT/src/security/PermissionCatalog.cpp" \
  -o "$TEST_BINARY"

phoneme_run_with_timeout "${PHONEME_TEST_TIMEOUT:-120}" "$TEST_BINARY"
