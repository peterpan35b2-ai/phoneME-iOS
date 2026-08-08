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

SOURCE_JS="${BUILD_DIR}/phoneme.js"
SOURCE_WASM="${BUILD_DIR}/phoneme.wasm"
if [[ ! -f "${SOURCE_JS}" || ! -f "${SOURCE_WASM}" ]]; then
    echo "error: WebAssembly build output is incomplete" >&2
    exit 1
fi

mkdir -p "${OUTPUT_DIR}"

sha256_file() {
    if command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | awk '{print $1}'
    else
        sha256sum "$1" | awk '{print $1}'
    fi
}

# Publish JS + WASM as one immutable versioned pair. The generated pthread glue
# resolves phoneme.js relative to import.meta.url, so keeping both files in the
# same versioned directory also prevents pthreads from mixing two builds.
JS_HASH="$(sha256_file "${SOURCE_JS}")"
WASM_HASH="$(sha256_file "${SOURCE_WASM}")"
BUILD_ID="${JS_HASH:0:8}${WASM_HASH:0:8}"
VERSION_NAME="build-${BUILD_ID}"
VERSION_DIR="${OUTPUT_DIR}/${VERSION_NAME}"
STAGING_DIR="${OUTPUT_DIR}/.${VERSION_NAME}.$$"

rm -rf "${STAGING_DIR}"
mkdir -p "${STAGING_DIR}"
cp "${SOURCE_JS}" "${STAGING_DIR}/phoneme.js"
cp "${SOURCE_WASM}" "${STAGING_DIR}/phoneme.wasm"

if [[ "${JS_HASH}" != "$(sha256_file "${STAGING_DIR}/phoneme.js")" || \
      "${WASM_HASH}" != "$(sha256_file "${STAGING_DIR}/phoneme.wasm")" ]]; then
    rm -rf "${STAGING_DIR}"
    echo "error: WebAssembly staging verification failed" >&2
    exit 1
fi

if [[ -d "${VERSION_DIR}" ]]; then
    rm -rf "${STAGING_DIR}"
else
    mv "${STAGING_DIR}" "${VERSION_DIR}"
fi

# The manifest is switched last, atomically. A page therefore sees either the
# complete previous pair or the complete new pair, never new JS with old WASM.
MANIFEST_TMP="${OUTPUT_DIR}/.manifest.$$.json"
printf '{"version":"%s","module":"%s/phoneme.js","wasm":"%s/phoneme.wasm"}\n' \
    "${BUILD_ID}" "${VERSION_NAME}" "${VERSION_NAME}" > "${MANIFEST_TMP}"
mv "${MANIFEST_TMP}" "${OUTPUT_DIR}/manifest.json"

# Keep fixed names for older deployed frontends. Each file replacement is
# atomic; the current frontend uses manifest.json and never relies on this pair.
for artifact in phoneme.js phoneme.wasm; do
    legacy_tmp="${OUTPUT_DIR}/.${artifact}.$$"
    cp "${BUILD_DIR}/${artifact}" "${legacy_tmp}"
    mv "${legacy_tmp}" "${OUTPUT_DIR}/${artifact}"
done
rm -f "${OUTPUT_DIR}/phoneme.worker.js"

# Retain the current and one previous immutable pair so an already-open page
# can still create a late pthread across one deployment/build transition.
version_mtimes() {
    if stat -f '%m %N' "${VERSION_DIR}" >/dev/null 2>&1; then
        find "${OUTPUT_DIR}" -maxdepth 1 -type d -name 'build-*' -exec stat -f '%m %N' {} \;
    else
        find "${OUTPUT_DIR}" -maxdepth 1 -type d -name 'build-*' -exec stat -c '%Y %n' {} \;
    fi
}
version_mtimes \
    | sort -rn \
    | awk 'NR > 2 { sub(/^[0-9]+ /, ""); print }' \
    | while IFS= read -r old_version; do
        rm -rf "${old_version}"
      done

printf 'Built phoneME WebAssembly %s:\n' "${BUILD_ID}"
ls -lh "${VERSION_DIR}/phoneme.js" "${VERSION_DIR}/phoneme.wasm" "${OUTPUT_DIR}/manifest.json"
