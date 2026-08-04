#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
PROJECT_ROOT="$(cd "$CORE_ROOT/.." && pwd)"
source "$SCRIPT_DIR/lib/common-test-root.sh"

RUN_HOST=1
RUN_SANITIZERS=1
RUN_IPHONEOS=1
RUN_XCODE=1

usage() {
  cat <<'EOF'
Usage: build-matrix.sh [options]
  --host-only       Run host module matrix only
  --skip-host       Skip host module matrix
  --skip-sanitizers Skip ASan/UBSan matrix
  --skip-iphoneos   Skip standalone arm64 archive build/verification
  --skip-xcode      Skip Xcode Debug/Release no-sign builds
EOF
}

while [[ "$#" -gt 0 ]]; do
  case "$1" in
    --host-only)
      RUN_SANITIZERS=0
      RUN_IPHONEOS=0
      RUN_XCODE=0
      ;;
    --skip-host) RUN_HOST=0 ;;
    --skip-sanitizers) RUN_SANITIZERS=0 ;;
    --skip-iphoneos) RUN_IPHONEOS=0 ;;
    --skip-xcode) RUN_XCODE=0 ;;
    --help|-h) usage; exit 0 ;;
    *) usage >&2; exit 64 ;;
  esac
  shift
done

REPORT_ROOT="$(phoneme_make_isolated_root "$CORE_ROOT" "build-matrix")"
mkdir -p "$REPORT_ROOT/logs"
STATUS_FILE="$REPORT_ROOT/status.txt"
: > "$STATUS_FILE"

run_step() {
  local name="$1"
  local timeout="$2"
  shift 2
  local log="$REPORT_ROOT/logs/$name.log"

  printf '[RUN ] %s\n' "$name"
  if phoneme_run_with_timeout "$timeout" "$@" >"$log" 2>&1; then
    printf '[PASS] %s\n' "$name"
    printf '%s=pass\n' "$name" >> "$STATUS_FILE"
  else
    local status=$?
    printf '[FAIL] %s (exit %s)\n' "$name" "$status" >&2
    printf '%s=fail:%s\n' "$name" "$status" >> "$STATUS_FILE"
    tail -100 "$log" >&2 || true
    printf 'Build matrix reports: %s\n' "$REPORT_ROOT" >&2
    exit "$status"
  fi
}

if [[ "$RUN_HOST" == "1" ]]; then
  run_step host-matrix "${PHONEME_MATRIX_TIMEOUT:-3600}" \
    env PHONEME_TEST_REPORT_ROOT="$REPORT_ROOT/host" \
        PHONEME_TEST_JOBS="${PHONEME_TEST_JOBS:-4}" \
        bash "$SCRIPT_DIR/test-all-host.sh"
fi

if [[ "$RUN_SANITIZERS" == "1" ]]; then
  run_step asan-ubsan "${PHONEME_MATRIX_TIMEOUT:-3600}" \
    env PHONEME_TEST_REPORT_ROOT="$REPORT_ROOT/asan-ubsan" \
        PHONEME_TEST_JOBS="${PHONEME_TEST_JOBS:-4}" \
        bash "$SCRIPT_DIR/test-sanitizers.sh" asan-ubsan
fi

if [[ "$RUN_IPHONEOS" == "1" ]]; then
  IPHONE_ROOT="$REPORT_ROOT/iphoneos"
  IPHONE_ARCHIVE="$IPHONE_ROOT/libphoneMECore.a"
  run_step iphoneos-archive "${PHONEME_MATRIX_TIMEOUT:-3600}" \
    env PHONEME_CORE_BUILD_DIR="$IPHONE_ROOT" \
        PHONEME_CORE_OUTPUT="$IPHONE_ARCHIVE" \
        IOS_DEPLOYMENT_TARGET="${IOS_DEPLOYMENT_TARGET:-15.0}" \
        bash "$SCRIPT_DIR/build-iphoneos.sh"
  run_step iphoneos-verify "${PHONEME_MATRIX_TIMEOUT:-900}" \
    env PHONEME_CORE_BUILD_DIR="$IPHONE_ROOT" \
        PHONEME_CORE_OUTPUT="$IPHONE_ARCHIVE" \
        IOS_DEPLOYMENT_TARGET="${IOS_DEPLOYMENT_TARGET:-15.0}" \
        bash "$SCRIPT_DIR/verify-iphoneos.sh"
fi

if [[ "$RUN_XCODE" == "1" ]]; then
  XCODE_TARGET="${PHONEME_XCODE_TARGET:-phoneME}"
  for configuration in Debug Release; do
    XCODE_ROOT="$REPORT_ROOT/xcode/$configuration"
    mkdir -p "$XCODE_ROOT"
    run_step "xcode-$configuration" "${PHONEME_XCODE_TIMEOUT:-3600}" \
      xcodebuild \
        -project "$PROJECT_ROOT/phoneME.xcodeproj" \
        -target "$XCODE_TARGET" \
        -sdk iphoneos \
        -configuration "$configuration" \
        ARCHS=arm64 \
        ONLY_ACTIVE_ARCH=YES \
        IPHONEOS_DEPLOYMENT_TARGET="${IOS_DEPLOYMENT_TARGET:-15.0}" \
        CODE_SIGNING_ALLOWED=NO \
        CODE_SIGNING_REQUIRED=NO \
        CODE_SIGN_IDENTITY= \
        DEVELOPMENT_TEAM= \
        SYMROOT="$XCODE_ROOT/products" \
        OBJROOT="$XCODE_ROOT/objects" \
        DSTROOT="$XCODE_ROOT/dst" \
        build
  done
fi

if [[ "${PHONEME_RUN_FUZZ:-0}" == "1" ]]; then
  run_step fuzz-smoke "${PHONEME_MATRIX_TIMEOUT:-1800}" \
    env PHONEME_FUZZ_RUNS="${PHONEME_FUZZ_RUNS:-1000}" \
        PHONEME_BUILD_ROOT="$REPORT_ROOT/fuzz" \
        bash "$SCRIPT_DIR/test-fuzz.sh"
fi

printf 'Build matrix passed. Reports: %s\n' "$REPORT_ROOT"
