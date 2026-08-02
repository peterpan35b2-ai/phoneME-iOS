#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

PROJECT_PATH="$REPO_ROOT/phoneME.xcodeproj"
SCHEME="${SCHEME:-phoneME}"
CONFIGURATION="Release"
OUTPUT_ROOT="${OUTPUT_ROOT:-$REPO_ROOT/Artifacts}"
TEAM_ID="${DEVELOPMENT_TEAM:-V73SB7GBMS}"
REBUILD_CORE=false

usage() {
  cat <<'USAGE'
Build and export a signed Release IPA for a physical iPhone.

Usage:
  bash Scripts/build-release-ipa.sh [options]

Options:
  --rebuild-core       Rebuild and package the phoneME core before archiving.
  --team TEAM_ID       Override the Apple Developer team ID.
  --output-root PATH   Override the artifact output directory.
  -h, --help           Show this help.

Environment overrides:
  DEVELOPMENT_TEAM, OUTPUT_ROOT, SCHEME
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --rebuild-core)
      REBUILD_CORE=true
      shift
      ;;
    --team)
      [[ $# -ge 2 ]] || { echo "Missing value for --team" >&2; exit 2; }
      TEAM_ID="$2"
      shift 2
      ;;
    --output-root)
      [[ $# -ge 2 ]] || { echo "Missing value for --output-root" >&2; exit 2; }
      OUTPUT_ROOT="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

for command in xcodebuild unzip codesign lipo plutil shasum; do
  command -v "$command" >/dev/null 2>&1 || {
    echo "Required command not found: $command" >&2
    exit 1
  }
done

[[ -d "$PROJECT_PATH" ]] || {
  echo "Xcode project not found: $PROJECT_PATH" >&2
  exit 1
}

if [[ "$REBUILD_CORE" == true ]]; then
  echo "== Rebuilding independent phoneME Core =="
  bash "$REPO_ROOT/Core/tools/test-host.sh"
  bash "$REPO_ROOT/Core/tools/build-iphoneos.sh"
fi

CORE_LIBRARY="$REPO_ROOT/Core/libphoneMECore.a"
for required_file in "$CORE_LIBRARY"; do
  [[ -f "$required_file" ]] || {
    echo "Required packaged core file is missing: $required_file" >&2
    echo "Run with --rebuild-core." >&2
    exit 1
  }
done

bash "$REPO_ROOT/Core/tools/verify-iphoneos.sh"

BUILD_SETTINGS="$(
  xcodebuild \
    -project "$PROJECT_PATH" \
    -scheme "$SCHEME" \
    -configuration "$CONFIGURATION" \
    -destination 'generic/platform=iOS' \
    -showBuildSettings
)"

build_setting() {
  local key="$1"
  awk -F ' = ' -v key="$key" '$1 ~ "^[[:space:]]*" key "$" { print $2; exit }' <<<"$BUILD_SETTINGS"
}

VERSION="$(build_setting MARKETING_VERSION)"
BUILD_NUMBER="$(build_setting CURRENT_PROJECT_VERSION)"
BUNDLE_ID="$(build_setting PRODUCT_BUNDLE_IDENTIFIER)"
PRODUCT_NAME="$(build_setting PRODUCT_NAME)"

VERSION="${VERSION:-unknown}"
BUILD_NUMBER="${BUILD_NUMBER:-unknown}"
BUNDLE_ID="${BUNDLE_ID:-unknown}"
PRODUCT_NAME="${PRODUCT_NAME:-phoneME}"

TIMESTAMP="$(date '+%Y%m%d-%H%M%S')"
ARTIFACT_DIR="$OUTPUT_ROOT/${PRODUCT_NAME}-Release-${VERSION}-build${BUILD_NUMBER}-iphone-${TIMESTAMP}"
ARCHIVE_PATH="$ARTIFACT_DIR/${PRODUCT_NAME}.xcarchive"
EXPORT_DIR="$ARTIFACT_DIR/export"
EXPORT_OPTIONS="$ARTIFACT_DIR/ExportOptions.plist"
ARCHIVE_LOG="$ARTIFACT_DIR/archive.log"
EXPORT_LOG="$ARTIFACT_DIR/export.log"
FINAL_IPA="$ARTIFACT_DIR/${PRODUCT_NAME}.ipa"

mkdir -p "$ARTIFACT_DIR" "$EXPORT_DIR"

cat >"$EXPORT_OPTIONS" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "https://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>method</key>
  <string>debugging</string>
  <key>signingStyle</key>
  <string>automatic</string>
  <key>teamID</key>
  <string>${TEAM_ID}</string>
  <key>stripSwiftSymbols</key>
  <true/>
  <key>compileBitcode</key>
  <false/>
</dict>
</plist>
PLIST

cleanup_verification=""
cleanup() {
  if [[ -n "$cleanup_verification" && -d "$cleanup_verification" ]]; then
    rm -rf "$cleanup_verification"
  fi
}
trap cleanup EXIT

echo "== Archiving ${PRODUCT_NAME} ${VERSION} (${BUILD_NUMBER}) for iPhone =="
echo "Team: $TEAM_ID"
xcodebuild \
  -project "$PROJECT_PATH" \
  -scheme "$SCHEME" \
  -configuration "$CONFIGURATION" \
  -destination 'generic/platform=iOS' \
  -archivePath "$ARCHIVE_PATH" \
  DEVELOPMENT_TEAM="$TEAM_ID" \
  CODE_SIGN_STYLE=Automatic \
  TARGETED_DEVICE_FAMILY=1 \
  clean archive \
  -allowProvisioningUpdates \
  2>&1 | tee "$ARCHIVE_LOG"

echo "== Exporting signed IPA =="
xcodebuild \
  -exportArchive \
  -archivePath "$ARCHIVE_PATH" \
  -exportPath "$EXPORT_DIR" \
  -exportOptionsPlist "$EXPORT_OPTIONS" \
  -allowProvisioningUpdates \
  2>&1 | tee "$EXPORT_LOG"

EXPORTED_IPA="$(find "$EXPORT_DIR" -maxdepth 1 -type f -name '*.ipa' -print -quit)"
[[ -n "$EXPORTED_IPA" && -f "$EXPORTED_IPA" ]] || {
  echo "Export completed without producing an IPA." >&2
  exit 1
}

cp -f "$EXPORTED_IPA" "$FINAL_IPA"

printf '%s\n' '== Verifying IPA =='
unzip -tq "$FINAL_IPA" >/dev/null
cleanup_verification="$(mktemp -d /tmp/phoneme-release-verify.XXXXXX)"
unzip -q "$FINAL_IPA" -d "$cleanup_verification"

APP_PATH="$(find "$cleanup_verification/Payload" -maxdepth 1 -type d -name '*.app' -print -quit)"
[[ -n "$APP_PATH" && -d "$APP_PATH" ]] || {
  echo "The IPA does not contain an application bundle." >&2
  exit 1
}

INFO_PLIST="$APP_PATH/Info.plist"
EXECUTABLE_NAME="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$INFO_PLIST")"
BINARY_PATH="$APP_PATH/$EXECUTABLE_NAME"
DEVICE_FAMILY="$(plutil -extract UIDeviceFamily json -o - "$INFO_PLIST")"
ARCH_INFO="$(lipo -info "$BINARY_PATH")"

[[ "$DEVICE_FAMILY" == '[1]' ]] || {
  echo "Unexpected UIDeviceFamily: $DEVICE_FAMILY (expected [1] for iPhone only)" >&2
  exit 1
}

[[ "$ARCH_INFO" == *arm64* ]] || {
  echo "The exported binary is not arm64: $ARCH_INFO" >&2
  exit 1
}

codesign --verify --deep --strict "$APP_PATH"

IPA_SIZE="$(du -h "$FINAL_IPA" | awk '{print $1}')"
APP_SIZE="$(du -sh "$APP_PATH" | awk '{print $1}')"
SHA256="$(shasum -a 256 "$FINAL_IPA" | awk '{print $1}')"
CODESIGN_DETAILS="$(codesign -dv --verbose=2 "$APP_PATH" 2>&1)"
SIGNED_BY="$(awk -F= '/^Authority=/{print $2; exit}' <<<"$CODESIGN_DETAILS")"

cat <<RESULT

Release IPA created successfully.
IPA: $FINAL_IPA
Archive: $ARCHIVE_PATH
Bundle: $BUNDLE_ID
Version: $VERSION ($BUILD_NUMBER)
Target: iPhone only, arm64, iOS 16+
IPA size: $IPA_SIZE
App size: $APP_SIZE
Signed by: $SIGNED_BY
SHA-256: $SHA256
RESULT
