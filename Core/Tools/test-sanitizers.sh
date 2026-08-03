#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
source "$SCRIPT_DIR/lib/common-test-root.sh"

MODE="${1:-asan-ubsan}"
if [[ "$#" -gt 0 ]]; then shift; fi

case "$MODE" in
  asan|ubsan|asan-ubsan|tsan) ;;
  *)
    echo "Usage: test-sanitizers.sh [asan|ubsan|asan-ubsan|tsan] [MODULE ...]" >&2
    exit 64
    ;;
esac

if [[ "$MODE" == "tsan" ]]; then
  PROBE_ROOT="$(phoneme_make_isolated_root "$CORE_ROOT" "tsan-probe")"
  phoneme_register_cleanup "$PROBE_ROOT"
  CXX="$(xcrun --sdk macosx --find clang++)"
  SDK_ROOT="$(xcrun --sdk macosx --show-sdk-path)"
  if ! printf 'int main() { return 0; }\n' | \
      "$CXX" -std=c++23 -isysroot "$SDK_ROOT" -x c++ - \
      -fsanitize=thread -o "$PROBE_ROOT/tsan-probe" >/dev/null 2>&1; then
    printf 'TSan is unavailable in this host toolchain; skipping optional preset.\n'
    exit 0
  fi
fi

export PHONEME_SANITIZER="$MODE"
export PHONEME_SANITIZE=0
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0:halt_on_error=1:strict_string_checks=1}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"
export TSAN_OPTIONS="${TSAN_OPTIONS:-halt_on_error=1:second_deadlock_stack=1}"

if [[ "$#" -gt 0 ]]; then
  bash "$SCRIPT_DIR/test-all-host.sh" "$@"
else
  bash "$SCRIPT_DIR/test-all-host.sh"
fi
