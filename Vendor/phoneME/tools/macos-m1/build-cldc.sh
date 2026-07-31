#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
IMAGE="phoneme-macos-m1-builder"

docker build --platform linux/amd64 \
  -t "$IMAGE" \
  -f "$ROOT/tools/macos-m1/Dockerfile" \
  "$ROOT"

docker run --platform linux/amd64 --rm \
  -v "$ROOT:/src" \
  -w /src/cldc/build/linux_i386 \
  "$IMAGE" \
  bash -lc 'export JVMWorkSpace=/src/cldc JDK_DIR="$JAVA_HOME"; make ENABLE_COMPILATION_WARNINGS=true ROMIZING=false ENABLE_JNI=false debug'

printf '\nBuilt: %s\n' "$ROOT/cldc/build/linux_i386/dist/bin/cldc_vm_g"
