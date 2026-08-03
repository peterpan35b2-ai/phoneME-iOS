#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
PROJECT_ROOT="$(cd "$CORE_ROOT/.." && pwd)"
MANIFEST="$SCRIPT_DIR/expected-results.json"
RUN_ID="$(date -u '+%Y%m%dT%H%M%SZ')-$$"
OUTPUT_ROOT="${PHONEME_COMPAT_ROOT:-$CORE_ROOT/build/compatibility-17/$RUN_ID}"
RUNNER="${PHONEME_CORPUS_RUNNER:-}"
REFERENCE_RUNNER="${PHONEME_REFERENCE_RUNNER:-}"
REFERENCE_NAME="${PHONEME_REFERENCE_RUNTIME:-}"
REFERENCE_VERSION="${PHONEME_REFERENCE_VERSION:-}"
STATIC_ONLY=0
NO_BUILD=0
INCLUDE_DISABLED=0
UPDATE_COVERAGE_DOC=1
LIST_ONLY=0
FILTERS=()

usage() {
  cat <<'USAGE'
Usage: Core/Compatibility/run-corpus.sh [options]

Options:
  --manifest FILE          Corpus manifest (default expected-results.json)
  --output DIR             Isolated output root
  --filter ID_OR_CATEGORY  Run one item/category; may be repeated
  --runner COMMAND         External runner implementing CompatibilityHarness CLI
  --reference-runner CMD   Optional differential reference runner
  --reference-name NAME    Reference runtime name recorded in evidence
  --reference-version VER  Reference runtime version recorded in evidence
  --static-only            Scan JAR class references without executing MIDlets
  --no-build               Do not rebuild project-authored fixture JARs
  --include-disabled       Include disabled local-corpus placeholders
  --no-coverage-doc        Do not refresh docs/J2ME_API_COVERAGE.md
  --list                   List manifest entries and exit
  --sanitize               Build the C++ runner with ASan/UBSan
  -h, --help               Show this help

Environment:
  PHONEME_COMPAT_ROOT      Override isolated report/build root
  PHONEME_CORPUS_RUNNER    External phoneME runner command
  PHONEME_REFERENCE_RUNNER Reference-emulator adapter command
  PHONEME_REFERENCE_RUNTIME Reference runtime name
  PHONEME_REFERENCE_VERSION Reference runtime version/build
  PHONEME_COMPAT_SANITIZE  Set to 1 to build with ASan/UBSan
  CXX, JAVAC, JAR          Override toolchain commands
USAGE
}

while (($#)); do
  case "$1" in
    --manifest)
      MANIFEST="$2"
      shift 2
      ;;
    --output)
      OUTPUT_ROOT="$2"
      shift 2
      ;;
    --filter)
      FILTERS+=("$2")
      shift 2
      ;;
    --runner)
      RUNNER="$2"
      shift 2
      ;;
    --reference-runner)
      REFERENCE_RUNNER="$2"
      shift 2
      ;;
    --reference-name)
      REFERENCE_NAME="$2"
      shift 2
      ;;
    --reference-version)
      REFERENCE_VERSION="$2"
      shift 2
      ;;
    --static-only)
      STATIC_ONLY=1
      shift
      ;;
    --no-build)
      NO_BUILD=1
      shift
      ;;
    --include-disabled)
      INCLUDE_DISABLED=1
      shift
      ;;
    --no-coverage-doc)
      UPDATE_COVERAGE_DOC=0
      shift
      ;;
    --list)
      LIST_ONLY=1
      shift
      ;;
    --sanitize)
      export PHONEME_COMPAT_SANITIZE=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

PYTHON="${PYTHON:-$(command -v python3)}"
[[ -n "$PYTHON" ]] || {
  echo "python3 is required" >&2
  exit 2
}

if ((LIST_ONLY)); then
  exec "$PYTHON" "$SCRIPT_DIR/analyze-failures.py" --manifest "$MANIFEST" list
fi

mkdir -p "$OUTPUT_ROOT"

if ((STATIC_ONLY == 0)) && [[ -z "$RUNNER" ]]; then
  BUILD_ROOT="$OUTPUT_ROOT/harness-build"
  TEST_BINARY="$BUILD_ROOT/CompatibilityHarness"
  mkdir -p "$BUILD_ROOT"

  if [[ -n "${CXX:-}" ]]; then
    CXX_COMMAND="$CXX"
    SDK_FLAGS=()
  elif command -v xcrun >/dev/null 2>&1; then
    CXX_COMMAND="$(xcrun --sdk macosx --find clang++)"
    SDK_FLAGS=(-isysroot "$(xcrun --sdk macosx --show-sdk-path)")
  else
    CXX_COMMAND="$(command -v clang++ || command -v c++)"
    SDK_FLAGS=()
  fi
  [[ -n "$CXX_COMMAND" ]] || {
    echo "a C++23 compiler is required" >&2
    exit 2
  }

  SOURCES=()
  while IFS= read -r source; do
    [[ "$source" == */api/CAPI.cpp ]] && continue
    SOURCES+=("$source")
  done < <(find "$CORE_ROOT/src" -type f -name '*.cpp' -print | LC_ALL=C sort)

  SANITIZER_FLAGS=()
  if [[ "${PHONEME_COMPAT_SANITIZE:-0}" == "1" ]]; then
    SANITIZER_FLAGS=(-fsanitize=address,undefined -fno-omit-frame-pointer)
  fi

  PLATFORM_LIBS=(-lz)
  if [[ "$(uname -s)" == "Darwin" ]]; then
    PLATFORM_LIBS+=(
      -framework CoreText
      -framework CoreGraphics
      -framework ImageIO
      -framework CoreFoundation
    )
  fi

  "$CXX_COMMAND" \
    -std=c++23 \
    ${SDK_FLAGS[@]+"${SDK_FLAGS[@]}"} \
    -I"$CORE_ROOT/include" \
    -fno-exceptions \
    -fno-rtti \
    -Wall \
    -Wextra \
    -Wpedantic \
    -Wconversion \
    -Wsign-conversion \
    -Wshadow \
    -Werror=return-type \
    ${SANITIZER_FLAGS[@]+"${SANITIZER_FLAGS[@]}"} \
    "$CORE_ROOT/Tests/Compatibility/CompatibilityHarness.cpp" \
    "${SOURCES[@]}" \
    "${PLATFORM_LIBS[@]}" \
    -o "$TEST_BINARY"
  RUNNER="$TEST_BINARY"
fi

ARGS=(
  --manifest "$MANIFEST"
  run
  --output "$OUTPUT_ROOT"
)
if ((${#FILTERS[@]})); then
  for filter in "${FILTERS[@]}"; do
    ARGS+=(--filter "$filter")
  done
fi
if ((STATIC_ONLY)); then
  ARGS+=(--static-only)
elif [[ -n "$RUNNER" ]]; then
  ARGS+=(--runner "$RUNNER")
fi
if [[ -n "$REFERENCE_RUNNER" ]]; then
  ARGS+=(--reference-runner "$REFERENCE_RUNNER")
fi
if [[ -n "$REFERENCE_NAME" ]]; then
  ARGS+=(--reference-name "$REFERENCE_NAME")
fi
if [[ -n "$REFERENCE_VERSION" ]]; then
  ARGS+=(--reference-version "$REFERENCE_VERSION")
fi
if ((NO_BUILD)); then
  ARGS+=(--no-build)
fi
if ((INCLUDE_DISABLED)); then
  ARGS+=(--include-disabled)
fi
if ((UPDATE_COVERAGE_DOC)); then
  ARGS+=(--update-coverage-doc "$PROJECT_ROOT/docs/J2ME_API_COVERAGE.md")
fi

"$PYTHON" "$SCRIPT_DIR/analyze-failures.py" "${ARGS[@]}"
