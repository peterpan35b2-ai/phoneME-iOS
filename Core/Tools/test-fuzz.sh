#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
source "$SCRIPT_DIR/lib/common-test-root.sh"

RUNS="${PHONEME_FUZZ_RUNS:-1000}"
[[ "$RUNS" =~ ^[0-9]+$ && "$RUNS" -gt 0 ]] || {
  phoneme_tool_error "PHONEME_FUZZ_RUNS must be a positive integer"
  exit 64
}

BUILD_ROOT="$(phoneme_make_isolated_root "$CORE_ROOT" "fuzz-tests")"
phoneme_register_cleanup "$BUILD_ROOT"
CXX="$(xcrun --sdk macosx --find clang++)"
SDK_ROOT="$(xcrun --sdk macosx --show-sdk-path)"

if ! printf '%s\n' \
    '#include <cstddef>' \
    'extern "C" int LLVMFuzzerTestOneInput(const unsigned char*, std::size_t) { return 0; }' | \
    "$CXX" -std=c++23 -isysroot "$SDK_ROOT" -x c++ - \
      -fsanitize=fuzzer -o "$BUILD_ROOT/fuzzer-probe" >/dev/null 2>&1; then
  printf 'libFuzzer is unavailable in this host toolchain; skipping optional fuzz targets.\n'
  exit 0
fi

COMMON_FLAGS=(
  -std=c++23
  -isysroot "$SDK_ROOT"
  -I"$CORE_ROOT/include"
  -fno-exceptions
  -fno-rtti
  -fno-omit-frame-pointer
  -fsanitize=fuzzer,address,undefined
  -Wall
  -Wextra
  -Wpedantic
  -Werror=return-type
)

build_and_run() {
  local name="$1"
  shift
  local binary="$BUILD_ROOT/$name"
  printf '[FUZZ BUILD] %s\n' "$name"
  "$CXX" "${COMMON_FLAGS[@]}" "$@" -o "$binary"
  printf '[FUZZ RUN  ] %s (%s runs)\n' "$name" "$RUNS"
  phoneme_run_with_timeout "${PHONEME_FUZZ_TIMEOUT:-300}" \
    "$binary" -runs="$RUNS" -max_len=65536
}

build_and_run classfile-verifier \
  "$SCRIPT_DIR/fuzz/FuzzClassFile.cpp" \
  "$CORE_ROOT/src/classfile/ClassFile.cpp" \
  "$CORE_ROOT/src/classfile/BytecodeVerifier.cpp"

build_and_run png \
  "$SCRIPT_DIR/fuzz/FuzzPng.cpp" \
  "$CORE_ROOT/src/graphics/PngDecoder.cpp" \
  "$CORE_ROOT/src/graphics/Image.cpp" \
  -lz

build_and_run url \
  "$SCRIPT_DIR/fuzz/FuzzUrl.cpp" \
  "$CORE_ROOT/src/network/Url.cpp"

build_and_run jad-manifest \
  "$SCRIPT_DIR/fuzz/FuzzManifest.cpp" \
  "$CORE_ROOT/src/runtime/JadParser.cpp" \
  "$CORE_ROOT/src/platform/MappedFile.cpp"

printf 'Fuzz smoke targets passed.\n'
