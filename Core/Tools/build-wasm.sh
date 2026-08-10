#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${PHONEME_WASM_BUILD_DIR:-${ROOT_DIR}/Core/build/wasm}"
COMPAT_BUILD_DIR="${PHONEME_WASM_COMPAT_BUILD_DIR:-${BUILD_DIR}-compat}"
OUTPUT_DIR="${ROOT_DIR}/web/public/wasm"
BUILD_TYPE="${PHONEME_WASM_BUILD_TYPE:-Release}"
JOBS="${PHONEME_WASM_JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || echo 4)}"

if ! command -v emcmake >/dev/null 2>&1; then
    echo "error: Emscripten is required (emcmake/emcc not found)" >&2
    exit 1
fi

build_variant() {
    local build_dir="$1"
    local simd="$2"
    emcmake cmake \
        -S "${ROOT_DIR}/Core" \
        -B "${build_dir}" \
        -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
        -DPHONEME_ENABLE_DECODED_EXECUTION=ON \
        -DPHONEME_ENABLE_LTO=ON \
        -DPHONEME_WASM_SIMD="${simd}"
    cmake --build "${build_dir}" --target phoneMEWeb --parallel "${JOBS}"
    [[ -f "${build_dir}/phoneme.js" && -f "${build_dir}/phoneme.wasm" ]] || {
        echo "error: WebAssembly build output is incomplete: ${build_dir}" >&2
        exit 1
    }
}

# Modern browsers use SIMD. Safari/iOS 16.0-16.3 cannot even parse a module
# containing v128 types, so publish a scalar build from the same source/exports.
build_variant "${BUILD_DIR}" ON
build_variant "${COMPAT_BUILD_DIR}" OFF

mkdir -p "${OUTPUT_DIR}"

sha256_file() {
    if command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | awk '{print $1}'
    else
        sha256sum "$1" | awk '{print $1}'
    fi
}

publish_variant() {
    local build_dir="$1"
    local js_hash wasm_hash build_id version_name version_dir staging_dir
    js_hash="$(sha256_file "${build_dir}/phoneme.js")"
    wasm_hash="$(sha256_file "${build_dir}/phoneme.wasm")"
    build_id="${js_hash:0:8}${wasm_hash:0:8}"
    version_name="build-${build_id}"
    version_dir="${OUTPUT_DIR}/${version_name}"
    staging_dir="${OUTPUT_DIR}/.${version_name}.$$"

    rm -rf "${staging_dir}"
    mkdir -p "${staging_dir}"
    cp "${build_dir}/phoneme.js" "${staging_dir}/phoneme.js"
    cp "${build_dir}/phoneme.wasm" "${staging_dir}/phoneme.wasm"

    if [[ "${js_hash}" != "$(sha256_file "${staging_dir}/phoneme.js")" || \
          "${wasm_hash}" != "$(sha256_file "${staging_dir}/phoneme.wasm")" ]]; then
        rm -rf "${staging_dir}"
        echo "error: WebAssembly staging verification failed" >&2
        exit 1
    fi

    if [[ -d "${version_dir}" ]]; then
        rm -rf "${staging_dir}"
    else
        mv "${staging_dir}" "${version_dir}"
    fi
    printf '%s\n' "${build_id}"
}

SIMD_BUILD_ID="$(publish_variant "${BUILD_DIR}")"
COMPAT_BUILD_ID="$(publish_variant "${COMPAT_BUILD_DIR}")"
SIMD_VERSION_NAME="build-${SIMD_BUILD_ID}"
COMPAT_VERSION_NAME="build-${COMPAT_BUILD_ID}"
COMBINED_VERSION="${SIMD_BUILD_ID}-${COMPAT_BUILD_ID}"

# Switch the manifest last. A client sees a complete SIMD/scalar set from one
# source revision, never a new frontend paired with a half-published core.
MANIFEST_TMP="${OUTPUT_DIR}/.manifest.$$.json"
printf '{"version":"%s","module":"%s/phoneme.js","wasm":"%s/phoneme.wasm","compatModule":"%s/phoneme.js","compatWasm":"%s/phoneme.wasm"}\n' \
    "${COMBINED_VERSION}" \
    "${SIMD_VERSION_NAME}" "${SIMD_VERSION_NAME}" \
    "${COMPAT_VERSION_NAME}" "${COMPAT_VERSION_NAME}" > "${MANIFEST_TMP}"
mv "${MANIFEST_TMP}" "${OUTPUT_DIR}/manifest.json"

# Fixed names are deliberately the scalar compatibility build. Old frontends
# that predate the dual-build manifest therefore continue to boot on iOS 16.0.
for artifact in phoneme.js phoneme.wasm; do
    legacy_tmp="${OUTPUT_DIR}/.${artifact}.$$"
    cp "${COMPAT_BUILD_DIR}/${artifact}" "${legacy_tmp}"
    mv "${legacy_tmp}" "${OUTPUT_DIR}/${artifact}"
done
rm -f "${OUTPUT_DIR}/phoneme.worker.js"

# Two variants per release; retain current + previous releases so already-open
# pages can still create delayed pthread workers across one deployment switch.
version_mtimes() {
    if stat -f '%m %N' "${OUTPUT_DIR}/${SIMD_VERSION_NAME}" >/dev/null 2>&1; then
        find "${OUTPUT_DIR}" -maxdepth 1 -type d -name 'build-*' -exec stat -f '%m %N' {} \;
    else
        find "${OUTPUT_DIR}" -maxdepth 1 -type d -name 'build-*' -exec stat -c '%Y %n' {} \;
    fi
}
version_mtimes \
    | sort -rn \
    | awk 'NR > 4 { sub(/^[0-9]+ /, ""); print }' \
    | while IFS= read -r old_version; do
        rm -rf "${old_version}"
      done

printf 'Built phoneME WebAssembly SIMD %s and Safari-16 compatibility %s:\n' \
    "${SIMD_BUILD_ID}" "${COMPAT_BUILD_ID}"
ls -lh \
    "${OUTPUT_DIR}/${SIMD_VERSION_NAME}/phoneme.js" \
    "${OUTPUT_DIR}/${SIMD_VERSION_NAME}/phoneme.wasm" \
    "${OUTPUT_DIR}/${COMPAT_VERSION_NAME}/phoneme.js" \
    "${OUTPUT_DIR}/${COMPAT_VERSION_NAME}/phoneme.wasm" \
    "${OUTPUT_DIR}/manifest.json"
