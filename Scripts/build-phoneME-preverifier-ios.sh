#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 <output-archive>" >&2
  exit 2
fi

OUTPUT_ARCHIVE="$1"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CORE_ROOT="${PHONEME_CORE_DIR:-$REPO_ROOT/Vendor/phoneME}"
SOURCE_ROOT="$CORE_ROOT/preverifier/src"
OBJECT_ROOT="$(dirname "$OUTPUT_ARCHIVE")/objects"
IOS_DEPLOYMENT_TARGET="${IOS_DEPLOYMENT_TARGET:-16.0}"
SDK_ROOT="$(xcrun --sdk iphoneos --show-sdk-path)"
CLANG="$(xcrun --sdk iphoneos --find clang)"
TARGET_TRIPLE="arm64-apple-ios${IOS_DEPLOYMENT_TARGET}"

if [[ "$IOS_DEPLOYMENT_TARGET" != "16.0" ]]; then
  echo "Only iOS 16.0 is supported by the embedded preverifier build." >&2
  exit 2
fi

if [[ ! -d "$SOURCE_ROOT" ]]; then
  echo "phoneME preverifier source is missing: $SOURCE_ROOT" >&2
  exit 1
fi

rm -rf "$OBJECT_ROOT"
mkdir -p "$OBJECT_ROOT" "$(dirname "$OUTPUT_ARCHIVE")"

COMMON_FLAGS=(
  -target "$TARGET_TRIPLE"
  -isysroot "$SDK_ROOT"
  -miphoneos-version-min="$IOS_DEPLOYMENT_TARGET"
  -I"$SOURCE_ROOT"
  -DUNIX
  -DDARWIN
  -DJAVAVERIFY
  -DTRIMMED
  -Darm64
  -DPHONEME_PREVERIFIER_LIBRARY
  -O2
  -DNDEBUG
  -fno-common
  -Wno-everything
)

SOURCES=(
  check_class.c
  utf.c
  check_code.c
  convert_md.c
  util.c
  jar.c
  jar_support.c
  classloader.c
  file.c
  classresolver.c
  stubs.c
  inlinejsr.c
  sys_support.c
  phoneme_preverify.c
)

OBJECTS=()
for source_file in "${SOURCES[@]}"; do
  object_file="$OBJECT_ROOT/${source_file%.c}.o"
  extra_flags=()
  if [[ "$source_file" != "phoneme_preverify.c" ]]; then
    extra_flags+=("-Dexit=phoneme_preverify_abort")
  fi
  "$CLANG" "${COMMON_FLAGS[@]}" "${extra_flags[@]}" \
    -c "$SOURCE_ROOT/$source_file" \
    -o "$object_file"
  OBJECTS+=("$object_file")
done

rm -f "$OUTPUT_ARCHIVE"
ZERO_AR_DATE=1 xcrun libtool -static -D -o "$OUTPUT_ARCHIVE" "${OBJECTS[@]}"

ARCHS="$(xcrun lipo -archs "$OUTPUT_ARCHIVE")"
if [[ "$ARCHS" != "arm64" ]]; then
  echo "Unexpected preverifier architectures: $ARCHS" >&2
  exit 1
fi

printf 'Built embedded phoneME preverifier: %s (%s, iOS %s+)\n' \
  "$OUTPUT_ARCHIVE" "$ARCHS" "$IOS_DEPLOYMENT_TARGET"
