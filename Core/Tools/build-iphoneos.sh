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
  BUILD_ROOT="$(phoneme_prepare_incremental_root "$PHONEME_CORE_BUILD_DIR" "$CORE_ROOT")"
else
  BUILD_ROOT="$(phoneme_make_isolated_root "$CORE_ROOT" "$APPLE_SDK-arm64")"
fi
OBJECT_ROOT="$BUILD_ROOT/objects"
OUTPUT_ARCHIVE="${PHONEME_CORE_OUTPUT:-$BUILD_ROOT/libphoneMECore.a}"
IOS_DEPLOYMENT_TARGET="${IOS_DEPLOYMENT_TARGET:-15.0}"
# Decoded execution has passed the host differential, sanitizer and pinned-JAR
# corpus gates. Keep it enabled for production iOS builds; the legacy
# interpreter remains available at runtime through
# PHONEME_USE_DECODED_EXECUTION=0 for diagnostics.
DECODED_EXECUTION="${PHONEME_ENABLE_DECODED_EXECUTION:-1}"
case "$DECODED_EXECUTION" in
  0|1) ;;
  *)
    echo "PHONEME_ENABLE_DECODED_EXECUTION must be 0 or 1." >&2
    exit 2
    ;;
esac

# Core is a release-optimized static library even when the app target is Debug,
# but keep Debug iteration fast by default. Standalone/device builds default to
# ThinLTO because they are production-style builds unless explicitly disabled.
if [[ -n "${PHONEME_ENABLE_THINLTO:-}" ]]; then
  THINLTO="$PHONEME_ENABLE_THINLTO"
elif [[ "${CONFIGURATION:-Release}" == "Debug" ]]; then
  THINLTO=0
else
  THINLTO=1
fi
case "$THINLTO" in
  0|1) ;;
  *)
    echo "PHONEME_ENABLE_THINLTO must be 0 or 1." >&2
    exit 2
    ;;
esac

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

for command in cmp find sort shasum xcrun awk rg cat mv; do
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
FINAL_SOURCE_LIST="$BUILD_ROOT/source-files-final.txt"
FINAL_SOURCE_HASHES="$BUILD_ROOT/source-sha256-final.txt"
BUILD_LOG="$BUILD_ROOT/build.log"
PROVENANCE="$BUILD_ROOT/build-provenance.txt"
OBJECT_LIST="$BUILD_ROOT/archive-members.txt"
FINAL_BUILD_CONFIG_HASH="$BUILD_ROOT/build-config-sha256-final.txt"

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
SOURCE_TREE_HASH="$(shasum -a 256 "$SOURCE_HASHES" | awk '{print $1}')"
PREVIOUS_SOURCE_TREE_HASH=""
HEADERS_UNCHANGED=false
if [[ -f "$FINAL_SOURCE_HASHES" ]]; then
  PREVIOUS_SOURCE_TREE_HASH="$(shasum -a 256 "$FINAL_SOURCE_HASHES" | awk '{print $1}')"
  CURRENT_HEADER_HASHES="$BUILD_ROOT/header-sha256.txt"
  PREVIOUS_HEADER_HASHES="$BUILD_ROOT/header-sha256-previous.txt"
  awk '$2 ~ /\.(h|hpp)$/' "$SOURCE_HASHES" > "$CURRENT_HEADER_HASHES"
  awk '$2 ~ /\.(h|hpp)$/' "$FINAL_SOURCE_HASHES" > "$PREVIOUS_HEADER_HASHES"
  if cmp -s "$CURRENT_HEADER_HASHES" "$PREVIOUS_HEADER_HASHES"; then
    HEADERS_UNCHANGED=true
  fi
fi

COMMON_FLAGS=(
  -std=c++23
  -target "$TARGET_TRIPLE"
  -isysroot "$SDK_ROOT"
  "$MIN_VERSION_FLAG"
  -I"$CORE_ROOT/include"
  -DPHONEME_IPHONEOS_ONLY=1
  -DPHONEME_ENABLE_DECODED_EXECUTION="$DECODED_EXECUTION"
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
if [[ "$THINLTO" == "1" ]]; then
  COMMON_FLAGS+=( -flto=thin )
fi

# Object-cache validity must include the actual compilation configuration, not
# only source contents. Otherwise toggling decoded execution, optimization
# flags, SDK/toolchain, or ThinLTO can silently reuse incompatible stale .o
# files and make both benchmarks and shipped archives non-reproducible.
BUILD_CONFIG_HASH="$({
  printf 'compiler=%s\n' "$CXX"
  "$CXX" --version
  printf 'sdk=%s\n' "$SDK_ROOT"
  printf 'target=%s\n' "$TARGET_TRIPLE"
  printf 'flag=%s\n' "${COMMON_FLAGS[@]}"
  shasum -a 256 "$BASH_SOURCE"
} | shasum -a 256 | awk '{print $1}')"
PREVIOUS_BUILD_CONFIG_HASH=""
if [[ -f "$FINAL_BUILD_CONFIG_HASH" ]]; then
  PREVIOUS_BUILD_CONFIG_HASH="$(cat "$FINAL_BUILD_CONFIG_HASH")"
fi
BUILD_CONFIG_UNCHANGED=false
if [[ -n "$PREVIOUS_BUILD_CONFIG_HASH" &&
      "$PREVIOUS_BUILD_CONFIG_HASH" == "$BUILD_CONFIG_HASH" ]]; then
  BUILD_CONFIG_UNCHANGED=true
fi
CURRENT_OBJECT_FINGERPRINT="$BUILD_CONFIG_HASH:$SOURCE_TREE_HASH"
PREVIOUS_OBJECT_FINGERPRINT="$BUILD_CONFIG_HASH:$PREVIOUS_SOURCE_TREE_HASH"

: > "$BUILD_LOG"
BUILD_JOBS="${PHONEME_BUILD_JOBS:-$(sysctl -n hw.logicalcpu 2>/dev/null || printf '4')}"
case "$BUILD_JOBS" in
  ''|*[!0-9]*|0)
    echo "PHONEME_BUILD_JOBS must be a positive integer." >&2
    exit 2
    ;;
esac
# Avoid excessive peak memory on machines with many logical cores while still
# reducing a clean device build from several minutes to well below the Xcode
# command timeout.
if (( BUILD_JOBS > 8 )); then
  BUILD_JOBS=8
fi

compile_source() {
  local source="$1"
  local relative="${source#src/}"
  local object="$OBJECT_ROOT/${relative//\//_}"
  object="${object%.cpp}.o"
  local marker="$object.source-sha256"

  if [[ -f "$object" && -f "$marker" ]]; then
    local marker_hash
    marker_hash="$(cat "$marker")"
    if [[ "$marker_hash" == "$CURRENT_OBJECT_FINGERPRINT" ]]; then
      printf 'CACHED %s\n' "$source" | tee -a "$BUILD_LOG"
      return 0
    fi

    # A previous complete build can be reused across .cpp-only changes only
    # when the compiler configuration is identical. A header or build-flag
    # change invalidates every object.
    if [[ "$HEADERS_UNCHANGED" == true &&
          "$BUILD_CONFIG_UNCHANGED" == true &&
          -n "$PREVIOUS_SOURCE_TREE_HASH" &&
          "$marker_hash" == "$PREVIOUS_OBJECT_FINGERPRINT" ]]; then
      local current_source_hash
      local previous_source_hash
      current_source_hash="$(awk -v path="$source" '$2 == path { print $1; exit }' "$SOURCE_HASHES")"
      previous_source_hash="$(awk -v path="$source" '$2 == path { print $1; exit }' "$FINAL_SOURCE_HASHES")"
      if [[ -n "$current_source_hash" &&
            "$current_source_hash" == "$previous_source_hash" ]]; then
        printf '%s\n' "$CURRENT_OBJECT_FINGERPRINT" > "$marker.tmp"
        mv "$marker.tmp" "$marker"
        printf 'CACHED %s\n' "$source" | tee -a "$BUILD_LOG"
        return 0
      fi
    fi
  fi

  printf 'CXX %s\n' "$source" | tee -a "$BUILD_LOG"
  "$CXX" "${COMMON_FLAGS[@]}" \
    -c "$CORE_ROOT/$source" \
    -o "$object" \
    2>&1 | tee -a "$BUILD_LOG"
  printf '%s\n' "$CURRENT_OBJECT_FINGERPRINT" > "$marker.tmp"
  mv "$marker.tmp" "$marker"
}

OBJECTS=()
PENDING_PIDS=()
COMPILE_STATUS=0
while IFS= read -r source; do
  relative="${source#src/}"
  object="$OBJECT_ROOT/${relative//\//_}"
  object="${object%.cpp}.o"
  OBJECTS+=("$object")

  compile_source "$source" &
  PENDING_PIDS+=("$!")
  if (( ${#PENDING_PIDS[@]} >= BUILD_JOBS )); then
    if ! wait "${PENDING_PIDS[0]}"; then
      COMPILE_STATUS=1
    fi
    PENDING_PIDS=("${PENDING_PIDS[@]:1}")
  fi
done < <(cd "$CORE_ROOT" && find src -type f -name '*.cpp' -print | LC_ALL=C sort)

for pid in "${PENDING_PIDS[@]}"; do
  if ! wait "$pid"; then
    COMPILE_STATUS=1
  fi
done
if (( COMPILE_STATUS != 0 )); then
  echo "phoneME Core compilation failed; see $BUILD_LOG" >&2
  exit "$COMPILE_STATUS"
fi

(
  cd "$CORE_ROOT"
  find include src -type f \( -name '*.h' -o -name '*.hpp' -o -name '*.cpp' \) \
    -print | LC_ALL=C sort
) > "$FINAL_SOURCE_LIST"
(
  cd "$CORE_ROOT"
  while IFS= read -r path; do
    shasum -a 256 "$path"
  done < "$FINAL_SOURCE_LIST"
) > "$FINAL_SOURCE_HASHES"
if ! cmp -s "$SOURCE_LIST" "$FINAL_SOURCE_LIST" ||
   ! cmp -s "$SOURCE_HASHES" "$FINAL_SOURCE_HASHES"; then
  echo "phoneME Core sources changed during compilation; retry the build." >&2
  exit 1
fi

rm -f "$OUTPUT_ARCHIVE"
ZERO_AR_DATE=1 xcrun libtool -static -D -o "$OUTPUT_ARCHIVE" "${OBJECTS[@]}"
printf '%s\n' "$BUILD_CONFIG_HASH" > "$FINAL_BUILD_CONFIG_HASH.tmp"
mv "$FINAL_BUILD_CONFIG_HASH.tmp" "$FINAL_BUILD_CONFIG_HASH"

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
decoded_execution_compiled=$DECODED_EXECUTION
thinlto_compiled=$THINLTO
build_config_sha256=$BUILD_CONFIG_HASH
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
PHONEME_ENABLE_DECODED_EXECUTION=$DECODED_EXECUTION
PHONEME_ENABLE_THINLTO=$THINLTO
EOF

cat <<EOF
phoneME Core built successfully.
Source: $CORE_ROOT/include + $CORE_ROOT/src
Archive: $OUTPUT_ARCHIVE
Architecture: arm64
Platform: $APPLE_SDK
Deployment target: iOS $IOS_DEPLOYMENT_TARGET
C++ standard: C++23
Decoded execution compiled: $DECODED_EXECUTION
ThinLTO compiled: $THINLTO
Source files: $SOURCE_COUNT
Archive members: $OBJECT_COUNT
Archive size: $ARCHIVE_SIZE
External phoneME runtime archive required: no
Built-in C++ boot classes: yes
Build result: $BUILD_ROOT/build-result.env
EOF
