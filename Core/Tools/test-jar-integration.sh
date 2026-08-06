#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

OBSERVE_MS="${PHONEME_INTEGRATION_OBSERVE_MS:-12000}"
TIMEOUT_MS="${PHONEME_INTEGRATION_TIMEOUT_MS:-30000}"
STALL_MS="${PHONEME_INTEGRATION_STALL_MS:-5000}"
JOBS="${PHONEME_INTEGRATION_JOBS:-4}"
INPUT_START_DELAY_MS="${PHONEME_INTEGRATION_INPUT_START_DELAY_MS:-400}"
INPUT_INTERVAL_MS="${PHONEME_INTEGRATION_INPUT_INTERVAL_MS:-220}"
HEARTBEAT_MS="${PHONEME_INTEGRATION_HEARTBEAT_MS:-1000}"
WIDTH="${PHONEME_INTEGRATION_WIDTH:-240}"
HEIGHT="${PHONEME_INTEGRATION_HEIGHT:-320}"
SKIP_TEARDOWN="${PHONEME_INTEGRATION_SKIP_TEARDOWN:-1}"

ARGS=(
  --mode both
  --all-midlets
  --autoplay
  --require-visual
  --observe-ms "$OBSERVE_MS"
  --timeout-ms "$TIMEOUT_MS"
  --stall-ms "$STALL_MS"
  --input-start-delay-ms "$INPUT_START_DELAY_MS"
  --input-interval-ms "$INPUT_INTERVAL_MS"
  --heartbeat-ms "$HEARTBEAT_MS"
  --width "$WIDTH"
  --height "$HEIGHT"
  --jobs "$JOBS"
)

if [[ "$SKIP_TEARDOWN" == "1" ]]; then
  ARGS+=(--skip-teardown)
fi

if [[ "${PHONEME_INTEGRATION_SANITIZE:-0}" == "1" ]]; then
  ARGS+=(--sanitize)
fi

exec bash "$SCRIPT_DIR/test-jar-directory.sh" "${ARGS[@]}" "$@"
