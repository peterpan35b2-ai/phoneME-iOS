#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SOURCE_ROOT="${PHONEME_SOURCE_DIR:-/Users/duypham/Developer/phoneME}"
DESTINATION="$REPO_ROOT/Vendor/phoneME"

if [[ ! -d "$SOURCE_ROOT/cldc/src" || ! -d "$SOURCE_ROOT/midp/src" ]]; then
  echo "phoneME source tree not found: $SOURCE_ROOT" >&2
  exit 1
fi

rm -rf "$DESTINATION"
mkdir -p \
  "$DESTINATION/cldc/build" \
  "$DESTINATION/midp/build" \
  "$DESTINATION"

copy_tree() {
  local source="$1"
  local destination="$2"
  shift 2
  mkdir -p "$destination"
  rsync -a --delete "$@" "$source/" "$destination/"
}

# CLDC VM and the shared/arm64 build descriptions used by the iOS target.
copy_tree "$SOURCE_ROOT/cldc/src" "$DESTINATION/cldc/src" \
  --exclude '.DS_Store'
copy_tree "$SOURCE_ROOT/cldc/build/share" "$DESTINATION/cldc/build/share" \
  --exclude '.DS_Store'
mkdir -p "$DESTINATION/cldc/build/ios_arm64"
install -m 0644 \
  "$SOURCE_ROOT/cldc/build/ios_arm64/Makefile" \
  "$DESTINATION/cldc/build/ios_arm64/Makefile"
install -m 0644 \
  "$SOURCE_ROOT/cldc/build/ios_arm64/ios_arm64.cfg" \
  "$DESTINATION/cldc/build/ios_arm64/ios_arm64.cfg"

# MIDP/JSR implementation. Build outputs and desktop-only generated products
# are deliberately excluded; the selected build descriptions regenerate them.
copy_tree "$SOURCE_ROOT/midp/src" "$DESTINATION/midp/src" \
  --exclude '.DS_Store' \
  --exclude '*/test/' \
  --exclude '*/tests/' \
  --exclude '*/i3test/' \
  --exclude '*/benchmark/'
copy_tree "$SOURCE_ROOT/midp/build/common" "$DESTINATION/midp/build/common" \
  --exclude '.DS_Store'
copy_tree "$SOURCE_ROOT/midp/build/ios_arm64_gcc" "$DESTINATION/midp/build/ios_arm64_gcc" \
  --exclude output \
  --exclude '*.log' \
  --exclude '.DS_Store'
copy_tree "$SOURCE_ROOT/midp/build/darwin_ios_native_gcc" "$DESTINATION/midp/build/darwin_ios_native_gcc" \
  --exclude output \
  --exclude '*.log' \
  --exclude '.DS_Store'
copy_tree "$SOURCE_ROOT/midp/build/linux_fb_gcc" "$DESTINATION/midp/build/linux_fb_gcc" \
  --exclude output \
  --exclude '*.log' \
  --exclude '.DS_Store'

# PCSL services used by CLDC/MIDP: memory, file, network, strings and printing.
copy_tree "$SOURCE_ROOT/pcsl" "$DESTINATION/pcsl" \
  --exclude output \
  --exclude output-ios \
  --exclude docs \
  --exclude donuts \
  --exclude '.DS_Store'

# Host-side preverifier and source generators needed for a clean rebuild.
copy_tree "$SOURCE_ROOT/preverifier" "$DESTINATION/preverifier" \
  --exclude '*.o' \
  --exclude preverify \
  --exclude '.DS_Store'
copy_tree "$SOURCE_ROOT/tools" "$DESTINATION/tools" \
  --exclude '.DS_Store'

if [[ -d "$SOURCE_ROOT/legal" ]]; then
  copy_tree "$SOURCE_ROOT/legal" "$DESTINATION/legal" \
    --exclude '.DS_Store'
fi

cat > "$DESTINATION/VENDORED_CORE.txt" <<'MANIFEST'
This directory is the source-of-truth phoneME core used by phoneME-iOS.

Included:
- CLDC VM source and iOS arm64 build files
- MIDP source and platform-widget LCDUI bridge
- PCSL file/memory/network/string/print layers
- preverifier and host generators

Excluded:
- CDC
- desktop emulator frontends
- generated object files and build output
- unrelated platform build products
- test and benchmark trees
MANIFEST

printf 'Vendored phoneME core source:\n'
du -sh "$DESTINATION"
find "$DESTINATION" -type f | wc -l | awk '{ print $1 " files" }'
