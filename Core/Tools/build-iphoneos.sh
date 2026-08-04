#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
source "$SCRIPT_DIR/lib/common-test-root.sh"
APPLE_SDK="${PHONEME_APPLE_SDK:-iphoneos}"
case "$APPLE_SDK" in
  iphoneos)
    TARGET_TRIPLE="arm64-apple-ios${IOS_DEPLOYMENT_TARGET:-15.0}"
    MIN_VERSION_FLAG="-miphoneos-version-min=${IOS_DEPLOYMENT_TARGET:-15.0}"
    ;;
  iphonesimulator)
    TARGET_TRIPLE="arm64-apple-ios${IOS_DEPLOYMENT_TARGET:-15.0}-simulator"
    MIN_VERSION_FLAG="-mios-simulator-version-min=${IOS_DEPLOYMENT_TARGET:-15.0}"
    ;;
  *)
    echo "Unsupported Apple SDK: $APPLE_SDK" >&2
    exit 2
    ;;
esac

if [[ -n "${PHONEME_CORE_BUILD_DIR:-}" ]]; then
  BUILD_ROOT="$(phoneme_prepare_managed_root "$PHONEME_CORE_BUILD_DIR" "$CORE_ROOT")"
else
  BUILD_ROOT="$(phoneme_make_isolated_root "$CORE_ROOT" "$APPLE_SDK-arm64")"
fi
OBJECT_ROOT="$BUILD_ROOT/objects"
OUTPUT_ARCHIVE="${PHONEME_CORE_OUTPUT:-$BUILD_ROOT/libphoneMECore.a}"
IOS_DEPLOYMENT_TARGET="${IOS_DEPLOYMENT_TARGET:-15.0}"

version_is_supported() {
  /usr/bin/awk -v version="$1" 'BEGIN {
    count = split(version, part, ".");
    major = part[1] + 0;
    minor = count > 1 ? part[2] + 0 : 0;
    exit !((major > 15) || (major == 15 && minor >= 0));
  }'
}

if ! version_is_supported "$IOS_DEPLOYMENT_TARGET"; then
  echo "phoneME Core requires iOS 15.0 or newer." >&2
  exit 2
fi

for command in find sort shasum xcrun awk rg; do
  command -v "$command" >/dev/null 2>&1 || {
    echo "Required build command not found: $command" >&2
    exit 1
  }
done

[[ -d "$CORE_ROOT/include" && -d "$CORE_ROOT/src" ]] || {
  echo "Core/include or Core/src is missing." >&2
  exit 1
}

if find "$CORE_ROOT/src" -type f \
  ! \( -name '*.cpp' -o -name '*.hpp' \) \
  -print -quit | grep -q .; then
  echo "Core/src may contain C++ source and private header files only." >&2
  exit 1
fi

if rg -n 'Vendor/phoneME|phoneME/Resources/PhoneMERuntime|_MergedSrc' \
  "$CORE_ROOT/include" "$CORE_ROOT/src" "$CORE_ROOT/Tests" \
  "$CORE_ROOT/CMakeLists.txt" >/dev/null; then
  echo "Core contains a forbidden legacy/import dependency." >&2
  exit 1
fi

SDK_ROOT="$(xcrun --sdk "$APPLE_SDK" --show-sdk-path)"
CXX="$(xcrun --sdk "$APPLE_SDK" --find clang++)"

mkdir -p "$OBJECT_ROOT" "$(dirname "$OUTPUT_ARCHIVE")"

SOURCE_LIST="$BUILD_ROOT/source-files.txt"
SOURCE_HASHES="$BUILD_ROOT/source-sha256.txt"
BUILD_LOG="$BUILD_ROOT/build.log"
PROVENANCE="$BUILD_ROOT/build-provenance.txt"
OBJECT_LIST="$BUILD_ROOT/archive-members.txt"

(
  cd "$CORE_ROOT"
  find include src -type f \( -name '*.h' -o -name '*.hpp' -o -name '*.cpp' \) \
    -print | LC_ALL=C sort
) > "$SOURCE_LIST"

(
  cd "$CORE_ROOT"
  while IFS= read -r path; do
    shasum -a 256 "$path"
  done < "$SOURCE_LIST"
) > "$SOURCE_HASHES"

COMMON_FLAGS=(
  -std=c++23
  -target "$TARGET_TRIPLE"
  -isysroot "$SDK_ROOT"
  "$MIN_VERSION_FLAG"
  -I"$CORE_ROOT/include"
  -DPHONEME_IPHONEOS_ONLY=1
  -fno-exceptions
  -fno-rtti
  -fvisibility=hidden
  -ffunction-sections
  -fdata-sections
  -fstrict-aliasing
  -O3
  -DNDEBUG
  -Wall
  -Wextra
  -Wpedantic
  -Wconversion
  -Wsign-conversion
  -Wshadow
  -Werror=return-type
  -Werror=pointer-to-int-cast
  -Werror=int-to-pointer-cast
  -Werror=implicit-function-declaration
)

: > "$BUILD_LOG"
OBJECTS=()
while IFS= read -r source; do
  relative="${source#src/}"
  object="$OBJECT_ROOT/${relative//\//_}"
  object="${object%.cpp}.o"
  printf 'CXX %s\n' "$source" | tee -a "$BUILD_LOG"
  "$CXX" "${COMMON_FLAGS[@]}" \
    -c "$CORE_ROOT/$source" \
    -o "$object" \
    2>&1 | tee -a "$BUILD_LOG"
  OBJECTS+=("$object")
done < <(cd "$CORE_ROOT" && find src -type f -name '*.cpp' -print | LC_ALL=C sort)

rm -f "$OUTPUT_ARCHIVE"
ZERO_AR_DATE=1 xcrun libtool -static -D -o "$OUTPUT_ARCHIVE" "${OBJECTS[@]}"

ARCHITECTURES="$(xcrun lipo -archs "$OUTPUT_ARCHIVE")"
if [[ "$ARCHITECTURES" != "arm64" ]]; then
  echo "Unexpected Core architectures: $ARCHITECTURES" >&2
  exit 1
fi

xcrun ar -t "$OUTPUT_ARCHIVE" > "$OBJECT_LIST"
if rg -n 'Merged|linux|win32|wince|simulator|legacy|vendor' \
  "$OBJECT_LIST" >/dev/null; then
  echo "Core archive contains a forbidden object name." >&2
  exit 1
fi

SOURCE_COUNT="$(wc -l < "$SOURCE_LIST" | awk '{print $1}')"
OBJECT_COUNT="$(awk 'NF && $0 != "__.SYMDEF" {count++} END {print count + 0}' "$OBJECT_LIST")"
ARCHIVE_HASH="$(shasum -a 256 "$OUTPUT_ARCHIVE" | awk '{print $1}')"
ARCHIVE_SIZE="$(du -h "$OUTPUT_ARCHIVE" | awk '{print $1}')"

cat > "$PROVENANCE" <<EOF
source_root=$CORE_ROOT
platform=$APPLE_SDK
architecture=arm64
minimum_ios=$IOS_DEPLOYMENT_TARGET
compiler=$CXX
sdk=$SDK_ROOT
cxx_standard=c++23
legacy_source_compiled=false
vendor_source_compiled=false
external_runtime_archive_required=false
builtin_boot_classes=true
merged_sources=false
source_files=$SOURCE_COUNT
archive_members=$OBJECT_COUNT
archive=$OUTPUT_ARCHIVE
archive_sha256=$ARCHIVE_HASH
archive_size=$ARCHIVE_SIZE
EOF

cat > "$BUILD_ROOT/build-result.env" <<EOF
PHONEME_CORE_BUILD_DIR=$BUILD_ROOT
PHONEME_CORE_OUTPUT=$OUTPUT_ARCHIVE
IOS_DEPLOYMENT_TARGET=$IOS_DEPLOYMENT_TARGET
PHONEME_APPLE_SDK=$APPLE_SDK
EOF

cat <<EOF
phoneME Core built successfully.
Source: $CORE_ROOT/include + $CORE_ROOT/src
Archive: $OUTPUT_ARCHIVE
Architecture: arm64
Platform: $APPLE_SDK
Deployment target: iOS $IOS_DEPLOYMENT_TARGET
C++ standard: C++23
Source files: $SOURCE_COUNT
Archive members: $OBJECT_COUNT
Archive size: $ARCHIVE_SIZE
External phoneME runtime archive required: no
Built-in C++ boot classes: yes
Build result: $BUILD_ROOT/build-result.env
EOF
