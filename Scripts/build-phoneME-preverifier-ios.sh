#!/bin/bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
  echo "Usage: $0 <iphoneos|iphonesimulator> <target-triple> <output-archive>" >&2
  exit 2
fi

SDK_NAME="$1"
TARGET_TRIPLE="$2"
OUTPUT_ARCHIVE="$3"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CORE_ROOT="${PHONEME_CORE_DIR:-$REPO_ROOT/Vendor/phoneME}"
SOURCE_ROOT="$CORE_ROOT/preverifier/src"
OBJECT_ROOT="$(dirname "$OUTPUT_ARCHIVE")/objects"
SDK_ROOT="$(xcrun --sdk "$SDK_NAME" --show-sdk-path)"
CLANG="$(xcrun --sdk "$SDK_NAME" --find clang)"

if [[ ! -d "$SOURCE_ROOT" ]]; then
  echo "phoneME preverifier source is missing: $SOURCE_ROOT" >&2
  exit 1
fi

mkdir -p "$OBJECT_ROOT" "$(dirname "$OUTPUT_ARCHIVE")"
find "$OBJECT_ROOT" -type f -delete

COMMON_FLAGS=(
  -target "$TARGET_TRIPLE"
  -isysroot "$SDK_ROOT"
  -I"$SOURCE_ROOT"
  -DUNIX
  -DDARWIN
  -DJAVAVERIFY
  -DTRIMMED
  -Darm64
  -DPHONEME_PREVERIFIER_LIBRARY
  -O2
  -DNDEBUG
  -Wno-everything
)

LEGACY_SOURCES=(
  check_class
  utf
  check_code
  convert_md
  util
  jar
  jar_support
  classloader
  file
  classresolver
  stubs
  inlinejsr
  sys_support
)

for source_name in "${LEGACY_SOURCES[@]}"; do
  "$CLANG" "${COMMON_FLAGS[@]}" \
    -Dexit=phoneme_preverify_abort \
    -c "$SOURCE_ROOT/$source_name.c" \
    -o "$OBJECT_ROOT/$source_name.o"
done

"$CLANG" "${COMMON_FLAGS[@]}" \
  -c "$SOURCE_ROOT/phoneme_preverify.c" \
  -o "$OBJECT_ROOT/phoneme_preverify.o"

xcrun libtool -static -o "$OUTPUT_ARCHIVE" "$OBJECT_ROOT"/*.o
printf 'Built embedded phoneME preverifier: %s\n' "$OUTPUT_ARCHIVE"
