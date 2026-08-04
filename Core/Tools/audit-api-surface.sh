#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
source "$SCRIPT_DIR/lib/common-test-root.sh"

if [[ $# -lt 1 || $# -gt 2 ]]; then
  echo "Usage: $0 <phoneME classes.zip> [report.md]" >&2
  exit 64
fi

REFERENCE_ARCHIVE="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
REPORT_PATH="${2:-$CORE_ROOT/build/api-surface-audit.md}"
TEST_ROOT="$(phoneme_make_isolated_root \
  "$CORE_ROOT" \
  "api-surface-audit" \
  "${PHONEME_API_AUDIT_ROOT:-}")"
phoneme_register_cleanup "$TEST_ROOT"
CLASS_LIST="$TEST_ROOT/classes.txt"
AUDIT_BINARY="$TEST_ROOT/ApiSurfaceAudit"
CXX="$(xcrun --sdk macosx --find clang++)"
SDK_ROOT="$(xcrun --sdk macosx --show-sdk-path)"

[[ -f "$REFERENCE_ARCHIVE" ]] || {
  echo "Reference archive not found: $REFERENCE_ARCHIVE" >&2
  exit 1
}
command -v unzip >/dev/null || {
  echo "unzip is required for API surface audit" >&2
  exit 1
}

mkdir -p "$(dirname "$REPORT_PATH")"
unzip -Z1 "$REFERENCE_ARCHIVE" \
  | sed -n 's#\.class$##p' \
  | LC_ALL=C sort -u \
  > "$CLASS_LIST"

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
  "$CORE_ROOT/Tools/ApiSurfaceAudit.cpp" \
  "$CORE_ROOT/src/archive/ZipArchive.cpp" \
  "$CORE_ROOT/src/classfile/ClassFile.cpp" \
  "$CORE_ROOT/src/classfile/BytecodeVerifier.cpp" \
  "$CORE_ROOT/src/platform/MappedFile.cpp" \
  "$CORE_ROOT/src/vm/BuiltinClassRegistry.cpp" \
  "${BUILTIN_SOURCES[@]}" \
  -lz \
  -o "$AUDIT_BINARY"

"$AUDIT_BINARY" "$REFERENCE_ARCHIVE" "$CLASS_LIST" "$REPORT_PATH"
