#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
MODE="${1:-debug}"
JOBS="${JOBS:-$(sysctl -n hw.logicalcpu 2>/dev/null || echo 4)}"
BUILD_DIR="$ROOT/cldc/build/darwin_c"
PCSL_OUTPUT_DIR="$ROOT/pcsl/output"
PCSL_DIST_DIR="$PCSL_OUTPUT_DIR/darwin_arm64"

case "$MODE" in
  debug|release|product) ;;
  *)
    echo "Usage: bash tools/macos-arm64/build-cldc.sh [debug|release|product]" >&2
    exit 2
    ;;
esac

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

xcrun --find clang >/dev/null
xcrun --find clang++ >/dev/null
pkg-config --exists sdl3 || {
  echo "SDL3 is required. Install it with: brew install sdl3" >&2
  exit 1
}

make -C "$ROOT/pcsl" -j"$JOBS" all \
  PCSL_PLATFORM=darwin_arm64_gcc \
  PCSL_OUTPUT_DIR="$PCSL_OUTPUT_DIR" \
  USE_SERVER_SOCKET=true

mkdir -p "$BUILD_DIR"
cp "$SCRIPT_DIR/darwin_c.Makefile" "$BUILD_DIR/Makefile"
cp "$SCRIPT_DIR/darwin_c.cfg" "$BUILD_DIR/darwin_c.cfg"

make -C "$BUILD_DIR" -j"$JOBS" "$MODE" \
  JVMWorkSpace="$ROOT/cldc" \
  JDK_DIR="$JDK_DIR" \
  ROMIZING=false \
  ENABLE_PCSL=true \
  PCSL_OUTPUT_DIR="$PCSL_OUTPUT_DIR" \
  PCSL_DIST_DIR="$PCSL_DIST_DIR" \
  ENABLE_COMPILATION_WARNINGS=true \
  NO_DEBUG_SYMBOLS=false

# Build the legacy preverifier as a native arm64 C utility. Its source still
# uses K&R-era C syntax, so GNU89 is intentional; the VM itself uses C++17.
make -C "$ROOT/preverifier/build/darwin" clean all \
  ARCH=ARM64 \
  CC=clang \
  LD=clang \
  EXTRACFLAGS='-std=gnu89 -Wno-deprecated-non-prototype -Wno-implicit-function-declaration -Wno-int-conversion' \
  LDFLAGS='-liconv'

mkdir -p "$BUILD_DIR/dist/bin"
cp "$ROOT/preverifier/build/darwin/preverify" "$BUILD_DIR/dist/bin/preverify"

case "$MODE" in
  debug)
    VM="$BUILD_DIR/dist/bin/cldc_vm_g"
    ROM_SUFFIX="_g"
    ;;
  release)
    VM="$BUILD_DIR/dist/bin/cldc_vm_r"
    ROM_SUFFIX="_r"
    ;;
  product)
    VM="$BUILD_DIR/dist/bin/cldc_vm"
    ROM_SUFFIX=""
    ;;
esac

# The VM archive intentionally excludes its base CLDC ROM object because the
# normal VM executable links it separately. External-classes MIDP launchers
# need the same object to satisfy the VM's __rom_* symbols.
ROM_OBJECT="$BUILD_DIR/target/$MODE/ROMImage.o"
if [[ ! -f "$ROM_OBJECT" ]]; then
  echo "Missing CLDC ROM object: $ROM_OBJECT" >&2
  exit 1
fi
mkdir -p "$BUILD_DIR/dist/lib"
cp "$ROM_OBJECT" "$BUILD_DIR/dist/lib/cldc_rom_image${ROM_SUFFIX}.o"

file "$VM"
file "$BUILD_DIR/dist/bin/preverify"

if ! file "$VM" | grep -q 'Mach-O 64-bit executable arm64'; then
  echo "Unexpected VM architecture: $VM" >&2
  exit 1
fi
if ! file "$BUILD_DIR/dist/bin/preverify" | grep -q 'Mach-O 64-bit executable arm64'; then
  echo "Unexpected preverifier architecture." >&2
  exit 1
fi

printf '\nBuilt native phoneME artifacts:\n  VM: %s\n  preverifier: %s\n' \
  "$VM" "$BUILD_DIR/dist/bin/preverify"
