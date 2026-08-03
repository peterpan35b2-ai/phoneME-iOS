#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
PYTHON="${PYTHON:-$(command -v python3)}"

if [[ -z "$PYTHON" ]]; then
  echo "python3 is required" >&2
  exit 2
fi

exec "$PYTHON" "$SCRIPT_DIR/test-jar-directory.py" \
  --jar-dir "${PHONEME_JAR_TEST_DIR:-$PROJECT_ROOT/jar_test}" \
  "$@"
