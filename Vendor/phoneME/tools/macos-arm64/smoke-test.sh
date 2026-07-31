#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$ROOT/cldc/build/darwin_c"
MODE="${1:-both}"

case "$MODE" in
  debug|release|both) ;;
  *)
    echo "Usage: bash tools/macos-arm64/smoke-test.sh [debug|release|both]" >&2
    exit 2
    ;;
esac

if [[ "$(uname -s)" != "Darwin" || "$(uname -m)" != "arm64" ]]; then
  echo "This smoke test requires native macOS arm64." >&2
  exit 1
fi

if [[ -z "${JDK_DIR:-}" ]]; then
  JDK_DIR="$(/usr/libexec/java_home -v 1.8 2>/dev/null || true)"
fi
if [[ -z "$JDK_DIR" || ! -x "$JDK_DIR/bin/javac" ]]; then
  echo "JDK 8 is required. Set JDK_DIR to a JDK 8 installation." >&2
  exit 1
fi

PREVERIFY="$BUILD_DIR/dist/bin/preverify"
CLDC_ZIP="$BUILD_DIR/dist/lib/cldc_classes.zip"

[[ -x "$PREVERIFY" ]] || {
  echo "Missing native preverifier. Run build-cldc.sh first." >&2
  exit 1
}
[[ -f "$CLDC_ZIP" ]] || {
  echo "Missing CLDC classes: $CLDC_ZIP" >&2
  exit 1
}

TMP="$(mktemp -d /tmp/phoneme-arm64-smoke.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/classes" "$TMP/verified" "$TMP/cldc"

# The original preverifier has an old ZIP reader. Feeding it exploded CLDC
# classes avoids spurious CRC messages while still running the VM against the
# real cldc_classes.zip below.
ditto -x -k "$CLDC_ZIP" "$TMP/cldc"

"$JDK_DIR/bin/javac" \
  -source 1.4 \
  -target 1.4 \
  -bootclasspath "$CLDC_ZIP" \
  -d "$TMP/classes" \
  "$SCRIPT_DIR/HelloPhoneME.java" \
  "$SCRIPT_DIR/Arm64Stress.java"

"$PREVERIFY" \
  -classpath "$TMP/cldc:$TMP/classes" \
  -d "$TMP/verified" \
  HelloPhoneME Arm64Stress 'Arm64Stress$Node'

run_vm() {
  local label="$1"
  local vm="$2"

  [[ -x "$vm" ]] || {
    echo "Missing $label VM: $vm" >&2
    exit 1
  }

  file "$vm"
  if ! file "$vm" | grep -q 'Mach-O 64-bit executable arm64'; then
    echo "$label VM is not native arm64." >&2
    exit 1
  fi

  echo "=== $label: basic ==="
  "$vm" \
    =HeapCapacity2M \
    -classpath "$CLDC_ZIP:$TMP/verified" \
    HelloPhoneME

  echo "=== $label: GC stress + heap verification ==="
  "$vm" \
    +VerifyGC \
    =HeapCapacity4M \
    -classpath "$CLDC_ZIP:$TMP/verified" \
    Arm64Stress
}

case "$MODE" in
  debug)
    run_vm debug "$BUILD_DIR/dist/bin/cldc_vm_g"
    ;;
  release)
    run_vm release "$BUILD_DIR/dist/bin/cldc_vm_r"
    ;;
  both)
    run_vm debug "$BUILD_DIR/dist/bin/cldc_vm_g"
    run_vm release "$BUILD_DIR/dist/bin/cldc_vm_r"
    ;;
esac

echo "Native arm64 smoke test passed ($MODE)."
