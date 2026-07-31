#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CORE_ROOT="${PHONEME_CORE_DIR:-$APP_ROOT/Vendor/phoneME}"

if [[ -z "$CORE_ROOT" || ! -d "$CORE_ROOT" ]]; then
  echo "phoneME core directory not found." >&2
  echo "Run Scripts/vendor-phoneME-core-source.sh or set PHONEME_CORE_DIR." >&2
  exit 1
fi

MIDP_OUTPUT="$CORE_ROOT/midp/build/ios_arm64_gcc/output"
CLDC_DIST="$CORE_ROOT/cldc/build/ios_arm64/dist"
PCSL_LIB="$CORE_ROOT/pcsl/output-ios/darwin_arm64/lib"
ARCHIVE_OUTPUT="$APP_ROOT/phoneME/Core/libphoneMECore.a"
RUNTIME_OUTPUT="$APP_ROOT/phoneME/Resources/PhoneMERuntime"

required_files=(
  "$MIDP_OUTPUT/obj/arm64/libobj.a"
  "$MIDP_OUTPUT/obj/arm64/runMidlet.o"
  "$MIDP_OUTPUT/classes.zip"
  "$CLDC_DIST/lib/libcldc_vm_r.a"
  "$CLDC_DIST/lib/cldc_rom_image_r.o"
  "$PCSL_LIB/libpcsl_escfilenames.a"
  "$PCSL_LIB/libpcsl_file.a"
  "$PCSL_LIB/libpcsl_memory.a"
  "$PCSL_LIB/libpcsl_network.a"
  "$PCSL_LIB/libpcsl_print.a"
  "$PCSL_LIB/libpcsl_string.a"
)

for path in "${required_files[@]}"; do
  if [[ ! -f "$path" ]]; then
    echo "Missing iOS phoneME build output: $path" >&2
    exit 1
  fi
done

if [[ ! -d "$MIDP_OUTPUT/appdb" ]]; then
  echo "MIDP app database template is missing under $MIDP_OUTPUT." >&2
  exit 1
fi

mkdir -p "$(dirname "$ARCHIVE_OUTPUT")"

xcrun libtool -static -o "$ARCHIVE_OUTPUT" \
  "$MIDP_OUTPUT/obj/arm64/libobj.a" \
  "$MIDP_OUTPUT/obj/arm64/runMidlet.o" \
  "$CLDC_DIST/lib/libcldc_vm_r.a" \
  "$CLDC_DIST/lib/cldc_rom_image_r.o" \
  "$PCSL_LIB/libpcsl_escfilenames.a" \
  "$PCSL_LIB/libpcsl_file.a" \
  "$PCSL_LIB/libpcsl_memory.a" \
  "$PCSL_LIB/libpcsl_network.a" \
  "$PCSL_LIB/libpcsl_print.a" \
  "$PCSL_LIB/libpcsl_string.a"

rm -rf "$RUNTIME_OUTPUT"
mkdir -p "$RUNTIME_OUTPUT/appdb"
# Direct-JAR launch does not use the legacy AMS selector splash/icons. Keep
# only the public-key store required by MIDP security/HTTPS.
if [[ -f "$MIDP_OUTPUT/appdb/_main.ks" ]]; then
  install -m 0644 \
    "$MIDP_OUTPUT/appdb/_main.ks" \
    "$RUNTIME_OUTPUT/appdb/_main.ks"
fi
install -m 0644 "$MIDP_OUTPUT/classes.zip" "$RUNTIME_OUTPUT/classes.zip"

printf 'Packaged phoneME iOS core:\n'
ls -lh "$ARCHIVE_OUTPUT"
du -sh "$RUNTIME_OUTPUT"
