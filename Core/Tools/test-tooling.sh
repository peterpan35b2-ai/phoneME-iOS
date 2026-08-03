#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
source "$SCRIPT_DIR/lib/common-test-root.sh"

ROOT="$(phoneme_make_isolated_root "$CORE_ROOT" "tooling-self-test")"
phoneme_register_cleanup "$ROOT"

fail() {
  printf 'Tooling self-test failed: %s\n' "$*" >&2
  exit 1
}

UNMARKED="$ROOT/unmarked"
mkdir -p "$UNMARKED"
printf 'keep\n' > "$UNMARKED/sentinel"
if phoneme_safe_remove_root "$UNMARKED" "$CORE_ROOT" 2>/dev/null; then
  fail 'unmarked directory was removed'
fi
[[ -f "$UNMARKED/sentinel" ]] || fail 'path safety sentinel disappeared'
if phoneme_safe_remove_root / "$CORE_ROOT" 2>/dev/null; then
  fail 'filesystem root passed the safety check'
fi
ln -s / "$ROOT/root-link"
if phoneme_make_isolated_root "$CORE_ROOT" "symlink-check" "$ROOT/root-link" \
    >/dev/null 2>&1; then
  fail 'symlink build base passed the safety check'
fi

FIRST="$(phoneme_make_isolated_root "$CORE_ROOT" "uniqueness" "$ROOT/generated")"
SECOND="$(phoneme_make_isolated_root "$CORE_ROOT" "uniqueness" "$ROOT/generated")"
[[ "$FIRST" != "$SECOND" ]] || fail 'isolated roots collided'
phoneme_safe_remove_root "$FIRST" "$CORE_ROOT"
phoneme_safe_remove_root "$SECOND" "$CORE_ROOT"
[[ ! -e "$FIRST" && ! -e "$SECOND" ]] || fail 'marked roots were not removed'

FAIL_STATUS=0
PHONEME_TOOLING_SELF_TEST=1 \
  bash "$SCRIPT_DIR/test-module.sh" --timeout 5 tooling-fail >/dev/null 2>&1 \
  || FAIL_STATUS=$?
[[ "$FAIL_STATUS" -eq 42 ]] || {
  fail "intentional failure returned $FAIL_STATUS instead of 42"
}

LEAK_FILE="$ROOT/timeout-child-survived"
TIMEOUT_STATUS=0
phoneme_run_with_timeout 1 bash -c \
  '(sleep 3; printf leaked > "$1") & wait' _ "$LEAK_FILE" >/dev/null 2>&1 \
  || TIMEOUT_STATUS=$?
[[ "$TIMEOUT_STATUS" -eq 124 ]] || {
  fail "timeout returned $TIMEOUT_STATUS instead of 124"
}
sleep 3
[[ ! -e "$LEAK_FILE" ]] || fail 'timeout left a descendant process running'

printf 'Tooling self-tests passed: path/symlink safety, unique roots, failure status, timeout cleanup.\n'
