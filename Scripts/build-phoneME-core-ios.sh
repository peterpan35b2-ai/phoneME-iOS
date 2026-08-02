#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
VENDOR_SOURCE="${PHONEME_VENDOR_SOURCE_DIR:-${PHONEME_CORE_DIR:-$REPO_ROOT/Vendor/phoneME}}"
BUILD_ROOT="${PHONEME_CORE_BUILD_DIR:-$REPO_ROOT/.build/phoneME-iphoneos}"
CORE_ROOT="$BUILD_ROOT/vendor"
JOBS="${JOBS:-1}"
IOS_DEPLOYMENT_TARGET="${IOS_DEPLOYMENT_TARGET:-16.0}"
BUILD_LOCK="${TMPDIR:-/tmp}/phoneME-iOS-core-build-${UID}.lock"

if [[ "$JOBS" != "1" ]]; then
  echo "This build intentionally compiles one source at a time; forcing JOBS=1." >&2
  JOBS=1
fi

if [[ "$IOS_DEPLOYMENT_TARGET" != "16.0" ]]; then
  echo "Only iOS 16.0 is supported by this core build." >&2
  exit 2
fi

if ! /usr/bin/shlock -f "$BUILD_LOCK" -p $$; then
  echo "Another phoneME iPhoneOS core build is already running." >&2
  exit 75
fi
trap 'rm -f "$BUILD_LOCK"' EXIT INT TERM

for command in rsync make xcrun shasum find sort awk; do
  command -v "$command" >/dev/null 2>&1 || {
    echo "Required build command not found: $command" >&2
    exit 1
  }
done

if [[ ! -d "$VENDOR_SOURCE/cldc/src" || ! -d "$VENDOR_SOURCE/midp/src" ]]; then
  echo "Vendored phoneME source is missing: $VENDOR_SOURCE" >&2
  exit 1
fi

if [[ "$CORE_ROOT" == "$VENDOR_SOURCE" ]]; then
  echo "The clean build tree must be different from Vendor/phoneME." >&2
  exit 2
fi

JDK_DIR="${JDK_DIR:-$(/usr/libexec/java_home -v 1.8)}"
SDK_ROOT="$(xcrun --sdk iphoneos --show-sdk-path)"
CLANG="$(xcrun --sdk iphoneos --find clang)"

printf '%s\n' '== Creating clean phoneME iPhoneOS source tree =='
rm -rf "$BUILD_ROOT"
mkdir -p "$CORE_ROOT"
rsync -a --delete --prune-empty-dirs \
  --exclude '/.git/' \
  --exclude '/pcsl/output/' \
  --exclude '/pcsl/output-*' \
  --exclude '/cldc/build/classes/' \
  --exclude '/cldc/build/classes.zip' \
  --exclude '/cldc/build/tmpclasses/' \
  --exclude '/cldc/build/ios_arm64/dist/' \
  --exclude '/cldc/build/ios_arm64/loopgen/' \
  --exclude '/cldc/build/ios_arm64/romgen/' \
  --exclude '/cldc/build/ios_arm64/target/' \
  --exclude '/cldc/build/ios_arm64/tools/' \
  --exclude '/cldc/build/ios_simulator_arm64/' \
  --exclude '/midp/build/ios_arm64_gcc/output/' \
  --exclude '/midp/build/ios_simulator_arm64_gcc/' \
  --exclude '/midp/build/darwin_ios_native_gcc/output/' \
  --exclude '/preverifier/build/ios_arm64/' \
  --exclude '/preverifier/build/ios_simulator_arm64/' \
  --exclude '/pcsl/makefiles/platforms/ios_simulator_arm64_gcc.gmk' \
  "$VENDOR_SOURCE/" "$CORE_ROOT/"

MANIFEST="$BUILD_ROOT/vendor-source.sha256"
(
  cd "$CORE_ROOT"
  find . -type f -print0 \
    | sort -z \
    | xargs -0 shasum -a 256
) > "$MANIFEST"

PCSL_OUTPUT="$CORE_ROOT/pcsl/output-ios"
CLDC_WORKSPACE="$CORE_ROOT/cldc"
CLDC_BUILD="$CLDC_WORKSPACE/build/ios_arm64"
CLDC_DIST="$CLDC_BUILD/dist"
PCSL_DIST="$PCSL_OUTPUT/darwin_arm64"
MIDP_BUILD="$CORE_ROOT/midp/build/ios_arm64_gcc"
MIDP_OUTPUT="$MIDP_BUILD/output"
TOOLS_DIR="$CORE_ROOT/tools"
RUNTIME_CLASSES="$REPO_ROOT/phoneME/Resources/PhoneMERuntime/classes.zip"
PREVERIFIER_ARCHIVE="$CORE_ROOT/preverifier/build/ios_arm64/libphoneMEPreverifier.a"

COMMON_TARGET_FLAGS=(
  "IOS_DEPLOYMENT_TARGET=$IOS_DEPLOYMENT_TARGET"
)

printf '%s\n' '== Building PCSL for iphoneos arm64, one source at a time =='
make -C "$CORE_ROOT/pcsl" clean \
  PCSL_PLATFORM=ios_arm64_gcc \
  PCSL_OUTPUT_DIR="$PCSL_OUTPUT" \
  "${COMMON_TARGET_FLAGS[@]}" \
  >/dev/null 2>&1 || true
make -C "$CORE_ROOT/pcsl" -j1 \
  PCSL_PLATFORM=ios_arm64_gcc \
  PCSL_OUTPUT_DIR="$PCSL_OUTPUT" \
  USE_DEBUG=false \
  USE_DATAGRAM=true \
  USE_SERVER_SOCKET=true \
  "${COMMON_TARGET_FLAGS[@]}" \
  all

printf '%s\n' '== Building CLDC MVM for iphoneos arm64 without merged sources =='
CLDC_ARGUMENTS=(
  "JVMWorkSpace=$CLDC_WORKSPACE"
  "JDK_DIR=$JDK_DIR"
  "PCSL_OUTPUT_DIR=$PCSL_OUTPUT"
  "PCSL_DIST_DIR=$PCSL_DIST"
  "ROMIZING=false"
  "ENABLE_ISOLATES=true"
  "ENABLE_COMPILATION_WARNINGS=true"
  "MERGE_SOURCE_FILES=false"
  "SOURCE_MERGER_SIZE=1"
  "IOS_DEPLOYMENT_TARGET=$IOS_DEPLOYMENT_TARGET"
)
make -C "$CLDC_BUILD" clean "${CLDC_ARGUMENTS[@]}" >/dev/null 2>&1 || true
make -C "$CLDC_BUILD" -j1 tools "${CLDC_ARGUMENTS[@]}"
if ! make -C "$CLDC_BUILD" -j1 release "${CLDC_ARGUMENTS[@]}"; then
  echo "CLDC standalone executable link failed; validating static iPhoneOS outputs." >&2
fi

if find "$CLDC_BUILD" -type f -name '_MergedSrc*.cpp' -print -quit | grep -q .; then
  echo "CLDC generated merged sources even though MERGE_SOURCE_FILES=false." >&2
  exit 1
fi

CLDC_ARCHIVE="$CLDC_BUILD/target/bin/libcldc_vm_r.a"
CLDC_ROM_OBJECT="$CLDC_BUILD/target/release/ROMImage.o"
if [[ ! -f "$CLDC_ARCHIVE" || ! -f "$CLDC_ROM_OBJECT" ]]; then
  echo "CLDC build did not produce the required iPhoneOS artifacts." >&2
  exit 1
fi
mkdir -p "$CLDC_DIST/bin" "$CLDC_DIST/lib" "$CLDC_DIST/include"
install -m 0755 "$CLDC_BUILD/romgen/app/romgen" "$CLDC_DIST/bin/romgen"
install -m 0644 "$CLDC_ARCHIVE" "$CLDC_DIST/lib/libcldc_vm_r.a"
install -m 0644 "$CLDC_ROM_OBJECT" "$CLDC_DIST/lib/cldc_rom_image_r.o"
install -m 0644 "$CLDC_BUILD/target/generated/jvmconfig.h" "$CLDC_DIST/include/jvmconfig.h"
install -m 0644 "$CLDC_WORKSPACE/src/vm/cpu/c/kni_md.h" "$CLDC_DIST/include/kni_md.h"
make -C "$CLDC_BUILD/target/release" -j1 \
  ENABLE_ISOLATES=true \
  MERGE_SOURCE_FILES=false \
  SOURCE_MERGER_SIZE=1 \
  ../../dist/lib/cldc_vm_r.make

printf '%s\n' '== Building MIDP MVM platform-widget core for iphoneos arm64 =='
MIDP_ARGUMENTS=(
  "JDK_DIR=$JDK_DIR"
  "TOOLS_DIR=$TOOLS_DIR"
  "PCSL_OUTPUT_DIR=$PCSL_OUTPUT"
  "CLDC_DIST_DIR=$CLDC_DIST"
  "IOS_DEPLOYMENT_TARGET=$IOS_DEPLOYMENT_TARGET"
  "USE_MULTIPLE_ISOLATES=true"
  "USE_NATIVE_APP_MANAGER=true"
  "USE_DEBUG=false"
  "USE_CLDC_RELEASE=true"
)
make -C "$MIDP_BUILD" clean "${MIDP_ARGUMENTS[@]}"
make -C "$MIDP_BUILD" -j1 rom "${MIDP_ARGUMENTS[@]}"

required_midp_outputs=(
  "$MIDP_OUTPUT/obj/arm64/libobj.a"
  "$MIDP_OUTPUT/classes.zip"
)
make -C "$MIDP_BUILD" -j1 \
  "${required_midp_outputs[@]}" \
  "${MIDP_ARGUMENTS[@]}"

for output in "${required_midp_outputs[@]}"; do
  if [[ ! -f "$output" ]]; then
    echo "MIDP build failed before producing $output" >&2
    exit 1
  fi
done

printf '%s\n' '== Syncing MIDP Java runtime =='
mkdir -p "$(dirname "$RUNTIME_CLASSES")"
install -m 0644 "$MIDP_OUTPUT/classes.zip" "$RUNTIME_CLASSES"

printf '%s\n' '== Building embedded preverifier for iphoneos arm64 =='
PHONEME_CORE_DIR="$CORE_ROOT" \
IOS_DEPLOYMENT_TARGET="$IOS_DEPLOYMENT_TARGET" \
  bash "$SCRIPT_DIR/build-phoneME-preverifier-ios.sh" "$PREVERIFIER_ARCHIVE"

printf '%s\n' '== Packaging clean iphoneos core into phoneME-iOS =='
PHONEME_CORE_DIR="$CORE_ROOT" \
IOS_DEPLOYMENT_TARGET="$IOS_DEPLOYMENT_TARGET" \
  "$SCRIPT_DIR/package-phoneME-core-ios.sh"

ARCHIVE_OUTPUT="$REPO_ROOT/phoneME/Core/libphoneMECore.a"
ARCHS="$(xcrun lipo -archs "$ARCHIVE_OUTPUT")"
if [[ "$ARCHS" != "arm64" ]]; then
  echo "Unexpected packaged core architectures: $ARCHS" >&2
  exit 1
fi

if xcrun ar -t "$ARCHIVE_OUTPUT" | grep -q '_MergedSrc'; then
  echo "Packaged core still contains merged CLDC translation units." >&2
  exit 1
fi

TARGET_OBJECT_COUNT="$(xcrun ar -t "$ARCHIVE_OUTPUT" | awk 'NF { count++ } END { print count + 0 }')"
SOURCE_FILE_COUNT="$(wc -l < "$MANIFEST" | awk '{print $1}')"
CORE_SIZE="$(du -h "$ARCHIVE_OUTPUT" | awk '{print $1}')"

cat <<RESULT

phoneME iPhoneOS core build complete.
Source tree: $CORE_ROOT
Source manifest: $MANIFEST
Copied source files: $SOURCE_FILE_COUNT
Archive: $ARCHIVE_OUTPUT
Archive members: $TARGET_OBJECT_COUNT
Architecture: arm64
SDK: $SDK_ROOT
Compiler: $CLANG
Deployment target: iOS $IOS_DEPLOYMENT_TARGET
Core size: $CORE_SIZE
Build mode: serial, one translation unit per source, no simulator target
RESULT
