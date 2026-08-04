#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib/common-test-root.sh"

usage() {
  cat <<'EOF'
Usage: test-module.sh [--timeout SECONDS] MODULE
       test-module.sh --list

Modules:
  host
  builtin-registry
  graphics
  graphics-vm
  push
  canvas-graphics
  game-api
  filesystem
  network-adapter
  security
  c-api
  rms
  scheduler
  suite-installer
  coverage-inventory
  lcdui-extended
  vendor-compat
EOF
}

TIMEOUT_SECONDS="${PHONEME_TEST_TIMEOUT:-600}"
if [[ "${1:-}" == "--list" ]]; then
  usage
  exit 0
fi
if [[ "${1:-}" == "--timeout" ]]; then
  [[ "$#" -ge 3 ]] || { usage >&2; exit 64; }
  TIMEOUT_SECONDS="$2"
  shift 2
fi

MODULE="${1:-}"
[[ -n "$MODULE" && "$#" -eq 1 ]] || { usage >&2; exit 64; }

case "$MODULE" in
  host) SCRIPT="$SCRIPT_DIR/test-host.sh" ;;
  builtin-registry) SCRIPT="$SCRIPT_DIR/test-builtin-registry.sh" ;;
  graphics) SCRIPT="$SCRIPT_DIR/test-graphics-host.sh" ;;
  graphics-vm) SCRIPT="$SCRIPT_DIR/test-graphics-vm-host.sh" ;;
  push) SCRIPT="$SCRIPT_DIR/test-push-host.sh" ;;
  canvas-graphics) SCRIPT="$SCRIPT_DIR/test-canvas-graphics-host.sh" ;;
  game-api) SCRIPT="$SCRIPT_DIR/test-game-api-host.sh" ;;
  filesystem) SCRIPT="$SCRIPT_DIR/test-filesystem-host.sh" ;;
  network-adapter) SCRIPT="$SCRIPT_DIR/test-network-adapter-host.sh" ;;
  security) SCRIPT="$SCRIPT_DIR/test-security-host.sh" ;;
  c-api) SCRIPT="$SCRIPT_DIR/test-c-api-host.sh" ;;
  rms) SCRIPT="$SCRIPT_DIR/test-rms-host.sh" ;;
  scheduler) SCRIPT="$SCRIPT_DIR/test-scheduler-host.sh" ;;
  suite-installer) SCRIPT="$SCRIPT_DIR/test-suite-installer-host.sh" ;;
  coverage-inventory) SCRIPT="$SCRIPT_DIR/test-coverage-inventory.sh" ;;
  lcdui-extended) SCRIPT="$SCRIPT_DIR/test-lcdui-extended-host.sh" ;;
  vendor-compat) SCRIPT="$SCRIPT_DIR/test-vendor-compat-host.sh" ;;
  tooling-fail)
    [[ "${PHONEME_TOOLING_SELF_TEST:-0}" == "1" ]] || {
      phoneme_tool_error "unknown module: $MODULE"
      exit 64
    }
    phoneme_run_with_timeout "$TIMEOUT_SECONDS" bash -c 'exit 42'
    exit $?
    ;;
  *)
    phoneme_tool_error "unknown module: $MODULE"
    usage >&2
    exit 64
    ;;
esac

[[ -f "$SCRIPT" ]] || {
  phoneme_tool_error "module script is missing: $SCRIPT"
  exit 66
}

export PHONEME_TASK_ID="${PHONEME_TASK_ID:-$MODULE}"
phoneme_run_with_timeout "$TIMEOUT_SECONDS" bash "$SCRIPT"
