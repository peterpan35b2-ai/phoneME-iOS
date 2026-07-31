#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
IMAGE="phoneme-macos-m1-builder"
VM="/src/cldc/build/linux_i386/dist/bin/cldc_vm_g"

if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
  docker build --platform linux/amd64 \
    -t "$IMAGE" \
    -f "$ROOT/tools/macos-m1/Dockerfile" \
    "$ROOT"
fi

if [[ ! -f "$ROOT/cldc/build/linux_i386/dist/bin/cldc_vm_g" ]]; then
  echo "Missing cldc_vm_g. Run: bash tools/macos-m1/build-cldc.sh" >&2
  exit 1
fi

DOCKER_ARGS=(--platform linux/amd64 --rm)
if [[ -t 0 && -t 1 ]]; then
  DOCKER_ARGS+=(-it)
fi

docker run "${DOCKER_ARGS[@]}" \
  -v "$ROOT:/src" \
  -w /src/cldc/build/linux_i386 \
  "$IMAGE" \
  "$VM" "$@"
