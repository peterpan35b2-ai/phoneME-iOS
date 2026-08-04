#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${PHONEME_WASM_BUILD_DIR:-${ROOT_DIR}/Core/build/wasm}"
OUTPUT_DIR="${ROOT_DIR}/web/public/wasm"
BUILD_TYPE="${PHONEME_WASM_BUILD_TYPE:-Release}"
JOBS="${PHONEME_WASM_JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || echo 4)}"

if ! command -v emcmake >/dev/null 2>&1; then
    echo "error: Emscripten is required (emcmake/emcc not found)" >&2
    exit 1
fi

emcmake cmake \
    -S "${ROOT_DIR}/Core" \
    -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"

cmake --build "${BUILD_DIR}" --target phoneMEWeb --parallel "${JOBS}"

mkdir -p "${OUTPUT_DIR}"
rm -f "${OUTPUT_DIR}/phoneme.js" \
      "${OUTPUT_DIR}/phoneme.wasm" \
      "${OUTPUT_DIR}/phoneme.worker.js"

for artifact in phoneme.js phoneme.wasm phoneme.worker.js; do
    source_path="${BUILD_DIR}/${artifact}"
    if [[ -f "${source_path}" ]]; then
        cp "${source_path}" "${OUTPUT_DIR}/${artifact}"
    fi
done

if [[ ! -f "${OUTPUT_DIR}/phoneme.js" || ! -f "${OUTPUT_DIR}/phoneme.wasm" ]]; then
    echo "error: WebAssembly output is incomplete" >&2
    exit 1
fi

printf 'Built phoneME WebAssembly:\n'
ls -lh "${OUTPUT_DIR}"/phoneme.*
