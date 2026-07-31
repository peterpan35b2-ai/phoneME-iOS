#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
IMAGE="phoneme-macos-m1-builder"

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

docker run --platform linux/amd64 --rm \
  -v "$ROOT:/src" \
  -w /src/cldc/build/linux_i386 \
  "$IMAGE" \
  bash -lc '
    set -euo pipefail
    rm -rf /tmp/phoneme-smoke
    mkdir -p /tmp/phoneme-smoke/classes /tmp/phoneme-smoke/verified

    cat > /tmp/phoneme-smoke/HelloPhoneME.java <<"JAVA"
public final class HelloPhoneME {
    public static void main(String[] args) {
        System.out.println("phoneME CLDC on Mac M1");
    }
}
JAVA

    javac -source 1.4 -target 1.4 \
      -bootclasspath dist/lib/cldc_classes.zip \
      -d /tmp/phoneme-smoke/classes \
      /tmp/phoneme-smoke/HelloPhoneME.java

    dist/bin/preverify \
      -classpath dist/lib/cldc_classes.zip:/tmp/phoneme-smoke/classes \
      -d /tmp/phoneme-smoke/verified \
      HelloPhoneME

    dist/bin/cldc_vm_g \
      -classpath dist/lib/cldc_classes.zip:/tmp/phoneme-smoke/verified \
      HelloPhoneME
  '
