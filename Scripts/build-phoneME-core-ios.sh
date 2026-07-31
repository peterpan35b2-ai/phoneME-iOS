#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CORE_ROOT="${PHONEME_CORE_DIR:-$REPO_ROOT/Vendor/phoneME}"
JOBS="${JOBS:-8}"

if [[ ! -d "$CORE_ROOT/cldc/src" || ! -d "$CORE_ROOT/midp/src" ]]; then
  echo "Vendored phoneME source is missing: $CORE_ROOT" >&2
  echo "Run Scripts/vendor-phoneME-core-source.sh first." >&2
  exit 1
fi

JDK_DIR="${JDK_DIR:-$(/usr/libexec/java_home -v 1.8)}"
PCSL_OUTPUT="$CORE_ROOT/pcsl/output-ios"
CLDC_WORKSPACE="$CORE_ROOT/cldc"
CLDC_BUILD="$CLDC_WORKSPACE/build/ios_arm64"
CLDC_DIST="$CLDC_BUILD/dist"
PCSL_DIST="$PCSL_OUTPUT/darwin_arm64"
MIDP_BUILD="$CORE_ROOT/midp/build/ios_arm64_gcc"
MIDP_OUTPUT="$MIDP_BUILD/output"
TOOLS_DIR="$CORE_ROOT/tools"
RUNTIME_CLASSES="$REPO_ROOT/phoneME/Resources/PhoneMERuntime/classes.zip"

printf '%s\n' '== Building PCSL for iOS arm64 =='
make -C "$CORE_ROOT/pcsl" clean \
  PCSL_PLATFORM=ios_arm64_gcc \
  PCSL_OUTPUT_DIR="$PCSL_OUTPUT" \
  >/dev/null 2>&1 || true
make -C "$CORE_ROOT/pcsl" -j"$JOBS" \
  PCSL_PLATFORM=ios_arm64_gcc \
  PCSL_OUTPUT_DIR="$PCSL_OUTPUT" \
  USE_DEBUG=false \
  USE_DATAGRAM=true \
  USE_SERVER_SOCKET=true \
  all

printf '%s\n' '== Building CLDC VM for iOS arm64 =='
make -C "$CLDC_BUILD" clean \
  JVMWorkSpace="$CLDC_WORKSPACE" \
  JDK_DIR="$JDK_DIR" \
  PCSL_OUTPUT_DIR="$PCSL_OUTPUT" \
  PCSL_DIST_DIR="$PCSL_DIST" \
  ROMIZING=false \
  >/dev/null 2>&1 || true
make -C "$CLDC_BUILD" tools \
  JVMWorkSpace="$CLDC_WORKSPACE" \
  JDK_DIR="$JDK_DIR" \
  PCSL_OUTPUT_DIR="$PCSL_OUTPUT" \
  PCSL_DIST_DIR="$PCSL_DIST" \
  ROMIZING=false \
  ENABLE_COMPILATION_WARNINGS=true
if ! make -C "$CLDC_BUILD" \
  JVMWorkSpace="$CLDC_WORKSPACE" \
  JDK_DIR="$JDK_DIR" \
  PCSL_OUTPUT_DIR="$PCSL_OUTPUT" \
  PCSL_DIST_DIR="$PCSL_DIST" \
  ROMIZING=false \
  ENABLE_COMPILATION_WARNINGS=true \
  release; then
  echo "CLDC standalone executable link failed; validating optimized static outputs." >&2
fi

CLDC_ARCHIVE="$CLDC_BUILD/target/bin/libcldc_vm_r.a"
CLDC_ROM_OBJECT="$CLDC_BUILD/target/release/ROMImage.o"
if [[ ! -f "$CLDC_ARCHIVE" || ! -f "$CLDC_ROM_OBJECT" ]]; then
  echo "CLDC build did not produce the required iOS artifacts." >&2
  exit 1
fi
mkdir -p "$CLDC_DIST/lib" "$CLDC_DIST/include"
install -m 0644 "$CLDC_ARCHIVE" "$CLDC_DIST/lib/libcldc_vm_r.a"
install -m 0644 "$CLDC_ROM_OBJECT" "$CLDC_DIST/lib/cldc_rom_image_r.o"
install -m 0644 \
  "$CLDC_BUILD/target/generated/jvmconfig.h" \
  "$CLDC_DIST/include/jvmconfig.h"
install -m 0644 \
  "$CLDC_WORKSPACE/src/vm/cpu/c/kni_md.h" \
  "$CLDC_DIST/include/kni_md.h"
make -C "$CLDC_BUILD/target/release" \
  ../../dist/lib/cldc_vm_r.make

printf '%s\n' '== Building MIDP platform-widget core =='
MIDP_ARGUMENTS=(
  "JDK_DIR=$JDK_DIR"
  "TOOLS_DIR=$TOOLS_DIR"
  "PCSL_OUTPUT_DIR=$PCSL_OUTPUT"
  "CLDC_DIST_DIR=$CLDC_DIST"
)
make -C "$MIDP_BUILD" clean "${MIDP_ARGUMENTS[@]}"
make -C "$MIDP_BUILD" rom "${MIDP_ARGUMENTS[@]}"

# The historical makefile also tries to link a standalone runMidlet executable.
# iOS embeds runMidlet() into the app and intentionally has no native main().
# Accept that final link failure only after all required static outputs exist.
if ! make -C "$MIDP_BUILD" -j"$JOBS" "${MIDP_ARGUMENTS[@]}"; then
  required_midp_outputs=(
    "$MIDP_OUTPUT/obj/arm64/libobj.a"
    "$MIDP_OUTPUT/obj/arm64/runMidlet.o"
    "$MIDP_OUTPUT/classes.zip"
  )
  for output in "${required_midp_outputs[@]}"; do
    if [[ ! -f "$output" ]]; then
      echo "MIDP build failed before producing $output" >&2
      exit 1
    fi
  done
fi

printf '%s\n' '== Syncing MIDP Java runtime =='
mkdir -p "$(dirname "$RUNTIME_CLASSES")"
install -m 0644 "$MIDP_OUTPUT/classes.zip" "$RUNTIME_CLASSES"

printf '%s\n' '== Packaging core into phoneME-iOS =='
PHONEME_CORE_DIR="$CORE_ROOT" "$SCRIPT_DIR/package-phoneME-core-ios.sh"

printf '%s\n' 'phoneME iOS core build complete.'
