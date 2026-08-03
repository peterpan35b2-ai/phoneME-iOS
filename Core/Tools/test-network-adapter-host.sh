#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
source "$SCRIPT_DIR/lib/common-test-root.sh"
BUILD_ROOT="$(phoneme_make_isolated_root "$CORE_ROOT" "network-adapter-host-tests" "${PHONEME_NETWORK_ADAPTER_TEST_ROOT:-}")"
phoneme_register_cleanup "$BUILD_ROOT"
TEST_BINARY="$BUILD_ROOT/PosixNetworkAdapterTests"
HTTPS_BRIDGE_TEST_BINARY="$BUILD_ROOT/HTTPSBridgeTests"
HTTPS_BRIDGE_OBJECT="$BUILD_ROOT/PhoneMEHTTPSBridge.o"
CC="$(xcrun --sdk macosx --find clang)"
CXX="$(xcrun --sdk macosx --find clang++)"
SDK_ROOT="$(xcrun --sdk macosx --show-sdk-path)"
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
  "$CORE_ROOT/Tests/PosixNetworkAdapterTests.cpp" \
  "$CORE_ROOT/src/network/PosixNetworkAdapter.cpp" \
  "$CORE_ROOT/src/network/Url.cpp" \
  -pthread \
  -o "$TEST_BINARY"

phoneme_run_with_timeout "${PHONEME_TEST_TIMEOUT:-300}" "$TEST_BINARY"

"$CC" \
  -isysroot "$SDK_ROOT" \
  -fobjc-arc \
  -fmodules \
  -DPHONEME_HTTPS_TESTING=1 \
  -Wall \
  -Wextra \
  -Wpedantic \
  -Wconversion \
  -Wsign-conversion \
  -Wshadow \
  -Werror=return-type \
  -Werror=unguarded-availability-new \
  $SANITIZER_FLAGS \
  -c "$CORE_ROOT/../phoneME/Core/PhoneMEHTTPSBridge.m" \
  -o "$HTTPS_BRIDGE_OBJECT"

"$CXX" \
  -std=c++23 \
  -isysroot "$SDK_ROOT" \
  -fobjc-arc \
  -fmodules \
  -Wall \
  -Wextra \
  -Wpedantic \
  -Wconversion \
  -Wsign-conversion \
  -Wshadow \
  -Werror=return-type \
  $SANITIZER_FLAGS \
  "$CORE_ROOT/Tests/HTTPSBridgeTests.mm" \
  "$HTTPS_BRIDGE_OBJECT" \
  -framework Foundation \
  -framework Security \
  -pthread \
  -o "$HTTPS_BRIDGE_TEST_BINARY"

phoneme_run_with_timeout "${PHONEME_TEST_TIMEOUT:-300}" \
  "$HTTPS_BRIDGE_TEST_BINARY"
