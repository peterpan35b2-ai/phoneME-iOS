#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
source "$SCRIPT_DIR/lib/common-test-root.sh"
TEST_ROOT="$(phoneme_make_isolated_root "$CORE_ROOT" "builtin-registry-tests" "${PHONEME_BUILTIN_REGISTRY_TEST_ROOT:-}")"
phoneme_register_cleanup "$TEST_ROOT"
TEST_BINARY="$TEST_ROOT/BuiltinRegistryTests"
CXX="$(xcrun --sdk macosx --find clang++)"
SDK_ROOT="$(xcrun --sdk macosx --show-sdk-path)"
phoneme_configure_sanitizers
SANITIZER_FLAGS="$PHONEME_SANITIZER_FLAGS"

BUILTIN_SOURCES=("$CORE_ROOT"/src/vm/*BuiltinClasses.cpp)

"$CXX" \
  -std=c++23 \
  -isysroot "$SDK_ROOT" \
  -I"$CORE_ROOT/include" \
  -I"$CORE_ROOT/src/vm" \
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
  "$CORE_ROOT/Tests/BuiltinRegistryTests.cpp" \
  "$CORE_ROOT/src/classfile/ClassFile.cpp" \
  "$CORE_ROOT/src/classfile/BytecodeVerifier.cpp" \
  "$CORE_ROOT/src/vm/BuiltinClassRegistry.cpp" \
  "${BUILTIN_SOURCES[@]}" \
  -o "$TEST_BINARY"

phoneme_run_with_timeout "${PHONEME_TEST_TIMEOUT:-300}" "$TEST_BINARY"
