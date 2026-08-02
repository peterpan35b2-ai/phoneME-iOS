#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_ROOT="${PHONEME_CORE_BUILD_DIR:-$CORE_ROOT/build/iphoneos-arm64}"
ARCHIVE="${PHONEME_CORE_OUTPUT:-$CORE_ROOT/libphoneMECore.a}"
IOS_DEPLOYMENT_TARGET="${IOS_DEPLOYMENT_TARGET:-16.0}"
PROVENANCE="$BUILD_ROOT/build-provenance.txt"

[[ -f "$ARCHIVE" ]] || {
  echo "Core archive is missing: $ARCHIVE" >&2
  exit 1
}

ARCHITECTURES="$(xcrun lipo -archs "$ARCHIVE")"
[[ "$ARCHITECTURES" == "arm64" ]] || {
  echo "Core is not arm64-only: $ARCHITECTURES" >&2
  exit 1
}

if find "$CORE_ROOT/src" -type f \
  ! -name '*.cpp' ! -name '*.hpp' ! -name '*.h' \
  -print -quit | grep -q .; then
  echo "Core/src contains a non-C++ source file." >&2
  exit 1
fi

if rg -n 'Vendor/phoneME|phoneME/Resources/PhoneMERuntime|_MergedSrc' \
  "$CORE_ROOT/include" "$CORE_ROOT/src" "$CORE_ROOT/tests" \
  "$CORE_ROOT/CMakeLists.txt" >/dev/null; then
  echo "Core references an imported or legacy source tree." >&2
  exit 1
fi

if rg -n --glob '*.{cpp,hpp,h}' \
  'reinterpret_cast<\s*(u?int(8|16|32)_t|u?int(8|16|32)|int|long)|\(\s*(u?int(8|16|32)_t|int|long)\s*\)\s*[A-Za-z_][A-Za-z0-9_]*\s*\*' \
  "$CORE_ROOT/include" "$CORE_ROOT/src" >/dev/null; then
  echo "Core contains a pointer-to-narrow-integer cast." >&2
  exit 1
fi

MEMBERS="$(xcrun ar -t "$ARCHIVE")"
if printf '%s\n' "$MEMBERS" | rg -n \
  'Merged|linux|win32|wince|simulator|legacy|vendor' >/dev/null; then
  echo "Core archive contains a forbidden object member." >&2
  exit 1
fi

OBJECT_COUNT="$(printf '%s\n' "$MEMBERS" | awk 'NF && $0 != "__.SYMDEF" {count++} END {print count + 0}')"
[[ "$OBJECT_COUNT" -gt 0 ]] || {
  echo "Core archive has no object members." >&2
  exit 1
}

while IFS= read -r object; do
  [[ -f "$object" ]] || continue
  INFO="$(otool -l "$object")"
  printf '%s\n' "$INFO" | rg -q 'platform 2' || {
    echo "Object is not built for iphoneos: $object" >&2
    exit 1
  }
  printf '%s\n' "$INFO" | rg -q "minos ${IOS_DEPLOYMENT_TARGET//./\\.}" || {
    echo "Object has the wrong deployment target: $object" >&2
    exit 1
  }
done < <(find "$BUILD_ROOT/objects" -type f -name '*.o' -print | LC_ALL=C sort)

[[ -f "$PROVENANCE" ]] || {
  echo "Core build provenance is missing." >&2
  exit 1
}
rg -q '^external_runtime_archive_required=false$' "$PROVENANCE" || {
  echo "Core provenance does not guarantee standalone boot classes." >&2
  exit 1
}
rg -q '^builtin_boot_classes=true$' "$PROVENANCE" || {
  echo "Core provenance does not record built-in boot classes." >&2
  exit 1
}

printf 'phoneME Core verification passed.\n'
printf 'Archive: %s\n' "$ARCHIVE"
printf 'Architecture: arm64\n'
printf 'Platform: iphoneos\n'
printf 'Minimum deployment target: iOS %s\n' "$IOS_DEPLOYMENT_TARGET"
printf 'C++ implementation files: %s\n' "$(find "$CORE_ROOT/src" -type f -name '*.cpp' | wc -l | awk '{print $1}')"
printf 'Archive members: %s\n' "$OBJECT_COUNT"
printf 'Imported source references: none\n'
printf 'External phoneME runtime archive: not required\n'
printf 'Built-in C++ boot classes: enabled\n'
printf 'Pointer-to-32-bit casts: none detected\n'
