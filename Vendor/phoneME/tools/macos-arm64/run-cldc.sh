#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$ROOT/cldc/build/darwin_c"
MODE="${PHONE_ME_MODE:-release}"

case "$MODE" in
  debug)   VM="$BUILD_DIR/dist/bin/cldc_vm_g" ;;
  release) VM="$BUILD_DIR/dist/bin/cldc_vm_r" ;;
  product) VM="$BUILD_DIR/dist/bin/cldc_vm" ;;
  *)
    echo "PHONE_ME_MODE must be debug, release or product." >&2
    exit 2
    ;;
esac

[[ -x "$VM" ]] || {
  echo "Missing VM: $VM" >&2
  echo "Build it with: bash tools/macos-arm64/build-cldc.sh $MODE" >&2
  exit 1
}

CLDC_ZIP="$BUILD_DIR/dist/lib/cldc_classes.zip"
[[ -f "$CLDC_ZIP" ]] || {
  echo "Missing CLDC classes: $CLDC_ZIP" >&2
  exit 1
}

CLASSPATH="$CLDC_ZIP"
if [[ -n "${PHONE_ME_CLASSPATH:-}" ]]; then
  CLASSPATH="$CLASSPATH:$PHONE_ME_CLASSPATH"
fi

exec "$VM" -classpath "$CLASSPATH" "$@"
