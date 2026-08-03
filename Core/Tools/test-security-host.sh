#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
source "$SCRIPT_DIR/lib/common-test-root.sh"
BUILD_ROOT="$(phoneme_make_isolated_root "$CORE_ROOT" "security-host-tests" "${PHONEME_SECURITY_TEST_ROOT:-}")"
phoneme_register_cleanup "$BUILD_ROOT"
TEST_BINARY="$BUILD_ROOT/SecurityPolicyTests"
CXX="$(xcrun --sdk macosx --find clang++)"
SDK_ROOT="$(xcrun --sdk macosx --show-sdk-path)"
if [[ -z "${PHONEME_SANITIZER:-}" ]]; then
  case "${PHONEME_SECURITY_SANITIZER:-none}" in
    none) PHONEME_SANITIZER=none ;;
    address) PHONEME_SANITIZER=asan-ubsan ;;
    thread) PHONEME_SANITIZER=tsan ;;
    *)
      echo "Unknown PHONEME_SECURITY_SANITIZER value." >&2
      exit 2
      ;;
  esac
fi
phoneme_configure_sanitizers
SANITIZER_FLAGS="$PHONEME_SANITIZER_FLAGS"

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
  "$CORE_ROOT/Tests/SecurityPolicyTests.cpp" \
  "$CORE_ROOT/src/security/PermissionCatalog.cpp" \
  "$CORE_ROOT/src/security/PermissionPolicy.cpp" \
  -pthread \
  -o "$TEST_BINARY"

phoneme_run_with_timeout "${PHONEME_TEST_TIMEOUT:-300}" "$TEST_BINARY"
