#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
TEST_ROOT="$CORE_ROOT/build/builtin-registry-tests"
TEST_BINARY="$TEST_ROOT/BuiltinRegistryTests"
CXX="$(xcrun --sdk macosx --find clang++)"
SDK_ROOT="$(xcrun --sdk macosx --show-sdk-path)"

rm -rf "$TEST_ROOT"
mkdir -p "$TEST_ROOT"

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
  "$CORE_ROOT/Tests/BuiltinRegistryTests.cpp" \
  "$CORE_ROOT/src/classfile/ClassFile.cpp" \
  "$CORE_ROOT/src/classfile/BytecodeVerifier.cpp" \
  "$CORE_ROOT/src/vm/BuiltinClassRegistry.cpp" \
  "${BUILTIN_SOURCES[@]}" \
  -o "$TEST_BINARY"

"$TEST_BINARY"
