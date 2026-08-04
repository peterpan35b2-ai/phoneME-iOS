#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_ROOT="${PHONEME_CORE_BUILD_DIR:-${1:-}}"
[[ -n "$BUILD_ROOT" ]] || {
  echo "Usage: PHONEME_CORE_BUILD_DIR=/path [PHONEME_APPLE_SDK=iphoneos|iphonesimulator] bash Core/Tools/verify-iphoneos.sh" >&2
  exit 64
}
ARCHIVE="${PHONEME_CORE_OUTPUT:-${2:-$BUILD_ROOT/libphoneMECore.a}}"
IOS_DEPLOYMENT_TARGET="${IOS_DEPLOYMENT_TARGET:-15.0}"
APPLE_SDK="${PHONEME_APPLE_SDK:-iphoneos}"
case "$APPLE_SDK" in
  iphoneos) EXPECTED_PLATFORM_ID=2 ;;
  iphonesimulator) EXPECTED_PLATFORM_ID=7 ;;
  *)
    echo "Unsupported Apple SDK: $APPLE_SDK" >&2
    exit 2
    ;;
esac
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
  "$CORE_ROOT/include" "$CORE_ROOT/src" "$CORE_ROOT/Tests" \
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

OBJECT_MEMBERS="$(printf '%s\n' "$MEMBERS" | awk 'NF && $0 != "__.SYMDEF"')"
OBJECT_COUNT="$(printf '%s\n' "$OBJECT_MEMBERS" | awk 'NF {count++} END {print count + 0}')"
[[ "$OBJECT_COUNT" -gt 0 ]] || {
  echo "Core archive has no object members." >&2
  exit 1
}

DUPLICATE_OBJECTS="$(printf '%s\n' "$OBJECT_MEMBERS" | LC_ALL=C sort | uniq -d)"
[[ -z "$DUPLICATE_OBJECTS" ]] || {
  echo "Core archive contains duplicate object members:" >&2
  printf '%s\n' "$DUPLICATE_OBJECTS" >&2
  exit 1
}

SOURCE_COUNT=0
while IFS= read -r source; do
  relative="${source#"$CORE_ROOT/src/"}"
  expected="${relative//\//_}"
  expected="${expected%.cpp}.o"
  printf '%s\n' "$OBJECT_MEMBERS" | grep -qx "$expected" || {
    echo "Core archive is missing source object: $relative -> $expected" >&2
    exit 1
  }
  SOURCE_COUNT=$((SOURCE_COUNT + 1))
done < <(find "$CORE_ROOT/src" -type f -name '*.cpp' -print | LC_ALL=C sort)

[[ "$OBJECT_COUNT" -eq "$SOURCE_COUNT" ]] || {
  echo "Core source/object inventory mismatch: $SOURCE_COUNT sources, $OBJECT_COUNT objects" >&2
  exit 1
}

for required_object in api_CAPI.o classfile_ClassFile.o runtime_Runtime.o vm_Machine.o; do
  grep -qx "$required_object" <<< "$MEMBERS" || {
    echo "Core archive is missing required object: $required_object" >&2
    exit 1
  }
done

GLOBAL_SYMBOLS="$(xcrun nm -g "$ARCHIVE" 2>/dev/null)"
if printf '%s\n' "$GLOBAL_SYMBOLS" | rg -n \
  '(^|[[:space:]_])(JVM_|KNI_|SNI_|pcsl_|javacall_|midp_|CVM_)' >/dev/null; then
  echo "Core archive exposes or imports a forbidden vendor runtime symbol." >&2
  exit 1
fi
rg -q '[_[:space:]]phoneme_c_api_version$' <<< "$GLOBAL_SYMBOLS" || {
  echo "Core archive is missing the versioned public C ABI symbol." >&2
  exit 1
}

while IFS= read -r object; do
  [[ -f "$object" ]] || continue
  INFO="$(otool -l "$object")"
  rg -q "platform ${EXPECTED_PLATFORM_ID}" <<< "$INFO" || {
    echo "Object is not built for $APPLE_SDK: $object" >&2
    exit 1
  }
  rg -q "minos ${IOS_DEPLOYMENT_TARGET//./\\.}" <<< "$INFO" || {
    echo "Object has the wrong deployment target: $object" >&2
    exit 1
  }
done < <(find "$BUILD_ROOT/objects" -type f -name '*.o' -print | LC_ALL=C sort)

[[ -f "$PROVENANCE" ]] || {
  echo "Core build provenance is missing." >&2
  exit 1
}
rg -q "^platform=$APPLE_SDK$" "$PROVENANCE" || {
  echo "Core provenance has the wrong Apple platform." >&2
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
printf 'Platform: %s\n' "$APPLE_SDK"
printf 'Minimum deployment target: iOS %s\n' "$IOS_DEPLOYMENT_TARGET"
printf 'C++ implementation files: %s\n' "$SOURCE_COUNT"
printf 'Archive members: %s\n' "$OBJECT_COUNT"
printf 'Source/object inventory: complete and unique\n'
printf 'Required archive objects: present\n'
printf 'C API version symbol: present\n'
printf 'Forbidden vendor symbols: none\n'
printf 'Imported source references: none\n'
printf 'External phoneME runtime archive: not required\n'
printf 'Built-in C++ boot classes: enabled\n'
printf 'Pointer-to-32-bit casts: none detected\n'
