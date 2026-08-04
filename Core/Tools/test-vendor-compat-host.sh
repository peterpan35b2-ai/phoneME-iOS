#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
source "$SCRIPT_DIR/lib/common-test-root.sh"
TEST_ROOT="$(phoneme_make_isolated_root \
  "$CORE_ROOT" \
  "vendor-compat-tests" \
  "${PHONEME_VENDOR_COMPAT_TEST_ROOT:-}")"
phoneme_register_cleanup "$TEST_ROOT"
TEST_BINARY="$TEST_ROOT/VendorCompatibilityTests"
NATIVE_OBJECT="$TEST_ROOT/VendorNatives.o"
CXX="$(xcrun --sdk macosx --find clang++)"
SDK_ROOT="$(xcrun --sdk macosx --show-sdk-path)"
phoneme_configure_sanitizers
SANITIZER_FLAGS="$PHONEME_SANITIZER_FLAGS"

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

# Compile the native bridge independently so unresolved host callbacks cannot
# hide syntax, type or warning regressions in a registry-only test binary.
"$CXX" \
  "${COMMON_FLAGS[@]}" \
  $SANITIZER_FLAGS \
  -c "$CORE_ROOT/src/vm/VendorNatives.cpp" \
  -o "$NATIVE_OBJECT"

"$CXX" \
  "${COMMON_FLAGS[@]}" \
  $SANITIZER_FLAGS \
  "$CORE_ROOT/Tests/VendorCompatibilityTests.cpp" \
  "$CORE_ROOT/src/classfile/ClassFile.cpp" \
  "$CORE_ROOT/src/classfile/BytecodeVerifier.cpp" \
  "$CORE_ROOT/src/vm/BuiltinClassRegistry.cpp" \
  "$CORE_ROOT/src/vm/VendorBuiltinClasses.cpp" \
  -o "$TEST_BINARY"

phoneme_run_with_timeout "${PHONEME_TEST_TIMEOUT:-120}" "$TEST_BINARY"
