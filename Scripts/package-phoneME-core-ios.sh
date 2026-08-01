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
PREVERIFIER_ARCHIVE="$CORE_ROOT/preverifier/build/ios_arm64/libphoneMEPreverifier.a"
RUNTIME_OUTPUT="$APP_ROOT/phoneME/Resources/PhoneMERuntime"

required_files=(
  "$PREVERIFIER_ARCHIVE"
  "$MIDP_OUTPUT/obj/arm64/libobj.a"
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

# Some cross-compiled iOS builds do not generate the optional appdb template.
# The runtime creates its suite store on first launch, and the iOS HTTPS bridge
# validates certificates through the system trust store instead of _main.ks.

mkdir -p "$(dirname "$ARCHIVE_OUTPUT")"

xcrun libtool -static -o "$ARCHIVE_OUTPUT" \
  "$PREVERIFIER_ARCHIVE" \
  "$MIDP_OUTPUT/obj/arm64/libobj.a" \
  "$CLDC_DIST/lib/libcldc_vm_r.a" \
  "$CLDC_DIST/lib/cldc_rom_image_r.o" \
  "$PCSL_LIB/libpcsl_escfilenames.a" \
  "$PCSL_LIB/libpcsl_file.a" \
  "$PCSL_LIB/libpcsl_memory.a" \
  "$PCSL_LIB/libpcsl_network.a" \
  "$PCSL_LIB/libpcsl_print.a" \
  "$PCSL_LIB/libpcsl_string.a"

mkdir -p "$RUNTIME_OUTPUT/appdb"
# Remove generated suite-store data without deleting the tracked trust-store
# template when the cross-compiled MIDP output does not provide a replacement.
find "$RUNTIME_OUTPUT/appdb" -mindepth 1 -maxdepth 1 \
  ! -name '_main.ks' -exec rm -rf {} +
rm -f "$RUNTIME_OUTPUT/classes.zip"
# The native iOS library is the AMS UI. Keep only the suite store template
# and public-key material required by MIDP security/HTTPS.
if [[ -f "$MIDP_OUTPUT/appdb/_main.ks" ]]; then
  install -m 0644 \
    "$MIDP_OUTPUT/appdb/_main.ks" \
    "$RUNTIME_OUTPUT/appdb/_main.ks"
fi
install -m 0644 "$MIDP_OUTPUT/classes.zip" "$RUNTIME_OUTPUT/classes.zip"

printf 'Packaged phoneME iOS core:\n'
ls -lh "$ARCHIVE_OUTPUT"
du -sh "$RUNTIME_OUTPUT"
