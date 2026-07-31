#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
TARGET_DIR="$ROOT/midp/build/darwin_sdl3_gcc"
OUTPUT_DIR="$TARGET_DIR/output"
CLDC_DIST_DIR="$ROOT/cldc/build/darwin_c/dist"
PCSL_OUTPUT_DIR="$ROOT/pcsl/output"
APP_DIR="$ROOT/dist/macos/phoneME.app"
CONTENTS_DIR="$APP_DIR/Contents"
JOBS="${JOBS:-$(sysctl -n hw.logicalcpu 2>/dev/null || echo 4)}"

if [[ "$(uname -s)" != "Darwin" || "$(uname -m)" != "arm64" ]]; then
    echo "This target requires native macOS arm64." >&2
    exit 1
fi

if [[ -z "${JDK_DIR:-}" ]]; then
    JDK_DIR="$(/usr/libexec/java_home -v 1.8 2>/dev/null || true)"
fi
if [[ -z "$JDK_DIR" || ! -x "$JDK_DIR/bin/javac" ]]; then
    echo "JDK 8 is required. Set JDK_DIR to a JDK 8 installation." >&2
    exit 1
fi

command -v pkg-config >/dev/null
pkg-config --exists sdl3 || {
    echo "SDL3 is required. Install it with: brew install sdl3" >&2
    exit 1
}

bash "$SCRIPT_DIR/build-cldc.sh" product

MAKE_VARS=(
    "USE_DEBUG=false"
    "JDK_DIR=$JDK_DIR"
    "TOOLS_DIR=$ROOT/tools"
    "PCSL_OUTPUT_DIR=$PCSL_OUTPUT_DIR"
    "CLDC_DIST_DIR=$CLDC_DIST_DIR"
)

if [[ "${CLEAN:-0}" == "1" ]]; then
    make -C "$TARGET_DIR" clean "${MAKE_VARS[@]}"
fi

# ROMStructs.h generation is stateful and must run serially. This target uses
# the structs-only arm64 path and does not serialize a MIDP ROM image.
make -C "$TARGET_DIR" rom "${MAKE_VARS[@]}"
make -C "$TARGET_DIR" -j"$JOBS" "${MAKE_VARS[@]}"

RUNNER="$OUTPUT_DIR/bin/arm64/runMidlet"
if [[ ! -x "$RUNNER" ]]; then
    echo "Missing MIDP launcher: $RUNNER" >&2
    exit 1
fi
if ! file "$RUNNER" | grep -q 'Mach-O 64-bit executable arm64'; then
    echo "Unexpected launcher architecture: $RUNNER" >&2
    exit 1
fi

SDL_LIBRARY="$(otool -L "$RUNNER" | awk '/libSDL3[^ ]*\.dylib/ { print $1; exit }')"
if [[ -z "$SDL_LIBRARY" || ! -f "$SDL_LIBRARY" ]]; then
    echo "Unable to locate the SDL3 dylib used by $RUNNER" >&2
    exit 1
fi
SDL_BASENAME="$(basename "$SDL_LIBRARY")"

rm -rf "$APP_DIR"
mkdir -p \
    "$CONTENTS_DIR/MacOS" \
    "$CONTENTS_DIR/Frameworks" \
    "$CONTENTS_DIR/Resources/runtime-template"

cp "$SCRIPT_DIR/Info.plist" "$CONTENTS_DIR/Info.plist"
cp "$SCRIPT_DIR/phoneME-launcher.sh" "$CONTENTS_DIR/MacOS/phoneME"
cp "$RUNNER" "$CONTENTS_DIR/MacOS/runMidlet"
cp -L "$SDL_LIBRARY" "$CONTENTS_DIR/Frameworks/$SDL_BASENAME"
cp "$OUTPUT_DIR/classes.zip" "$CONTENTS_DIR/Resources/classes.zip"
/usr/bin/ditto "$OUTPUT_DIR/appdb" \
    "$CONTENTS_DIR/Resources/runtime-template/appdb"
/usr/bin/ditto "$OUTPUT_DIR/lib" \
    "$CONTENTS_DIR/Resources/runtime-template/lib"

chmod 755 "$CONTENTS_DIR/MacOS/phoneME" "$CONTENTS_DIR/MacOS/runMidlet"
install_name_tool -change "$SDL_LIBRARY" \
    "@executable_path/../Frameworks/$SDL_BASENAME" \
    "$CONTENTS_DIR/MacOS/runMidlet"
install_name_tool -id "@rpath/$SDL_BASENAME" \
    "$CONTENTS_DIR/Frameworks/$SDL_BASENAME"

plutil -lint "$CONTENTS_DIR/Info.plist" >/dev/null
codesign --force --deep --sign - "$APP_DIR" >/dev/null
codesign --verify --deep --strict "$APP_DIR"

printf '\nBuilt native macOS app:\n  %s\n\n' "$APP_DIR"
file "$CONTENTS_DIR/MacOS/runMidlet"
otool -L "$CONTENTS_DIR/MacOS/runMidlet"
printf '\nLaunch with:\n  open %q\n' "$APP_DIR"
