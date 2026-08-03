#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
PROJECT_ROOT="$(cd "$CORE_ROOT/.." && pwd)"
source "$SCRIPT_DIR/lib/common-test-root.sh"
REGRESSION_ROOT="$(phoneme_make_isolated_root "$CORE_ROOT" "full-regression" "${PHONEME_FULL_REGRESSION_ROOT:-}")"
phoneme_register_cleanup "$REGRESSION_ROOT"
export PHONEME_BUILD_ROOT="$REGRESSION_ROOT/host-runs"
export PHONEME_TEST_TIMEOUT="${PHONEME_TEST_TIMEOUT:-300}"

run_test() {
  local label="$1"
  shift
  printf '\n== %s ==\n' "$label"
  PHONEME_TASK_ID="$(phoneme_sanitize_task_id "$label")" "$@"
}

run_test "core-host" bash "$SCRIPT_DIR/test-host.sh"
run_test "core-host-asan-ubsan" env PHONEME_SANITIZER=asan-ubsan \
  bash "$SCRIPT_DIR/test-host.sh"

while IFS= read -r script; do
  label="$(basename "$script" .sh)"
  label="${label#test-}"
  run_test "$label" bash "$script"
done < <(find "$SCRIPT_DIR" -maxdepth 1 -type f \
  -name 'test-*-host.sh' ! -name 'test-all-host.sh' -print | LC_ALL=C sort)

if [[ "${PHONEME_SKIP_IPHONEOS:-0}" != "1" ]]; then
  IOS_BUILD_ROOT="$REGRESSION_ROOT/iphoneos"
  run_test "iphoneos-build" env \
    PHONEME_CORE_BUILD_DIR="$IOS_BUILD_ROOT" \
    PHONEME_CORE_OUTPUT="$IOS_BUILD_ROOT/libphoneMECore.a" \
    bash "$SCRIPT_DIR/build-iphoneos.sh"
  run_test "iphoneos-verify" env \
    PHONEME_CORE_BUILD_DIR="$IOS_BUILD_ROOT" \
    PHONEME_CORE_OUTPUT="$IOS_BUILD_ROOT/libphoneMECore.a" \
    bash "$SCRIPT_DIR/verify-iphoneos.sh"
fi

if [[ "${PHONEME_SKIP_XCODE:-0}" != "1" ]]; then
  XCODE_SKIP_CORE_REBUILD=1
  if [[ "${PHONEME_SKIP_IPHONEOS:-0}" == "1" ]]; then
    XCODE_SKIP_CORE_REBUILD=0
  fi
  for configuration in Debug Release; do
    run_test "xcode-${configuration}" \
      xcodebuild \
        -project "$PROJECT_ROOT/phoneME.xcodeproj" \
        -scheme phoneME \
        -configuration "$configuration" \
        -sdk iphoneos \
        -destination 'generic/platform=iOS' \
        -derivedDataPath "$REGRESSION_ROOT/xcode-$configuration" \
        PHONEME_SKIP_CORE_REBUILD="$XCODE_SKIP_CORE_REBUILD" \
        CODE_SIGNING_ALLOWED=NO \
        build
  done
fi

printf '\nphoneME full regression passed\n'
