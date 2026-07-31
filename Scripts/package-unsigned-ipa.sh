#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
APP_PATH="${1:-/tmp/phoneME-release-iphone/Build/Products/Release-iphoneos/phoneME.app}"
OUTPUT_PATH="${2:-$REPO_ROOT/Artifacts/phoneME-Release-unsigned.ipa}"

if [[ ! -d "$APP_PATH" ]]; then
  echo "Release app not found: $APP_PATH" >&2
  exit 1
fi

STAGING_DIR="$(mktemp -d /tmp/phoneme-ipa.XXXXXX)"
trap 'rm -rf "$STAGING_DIR"' EXIT

mkdir -p "$STAGING_DIR/Payload" "$(dirname "$OUTPUT_PATH")"
ditto "$APP_PATH" "$STAGING_DIR/Payload/phoneME.app"
rm -f "$OUTPUT_PATH"
(
  cd "$STAGING_DIR"
  /usr/bin/zip -qry "$OUTPUT_PATH" Payload
)

/usr/bin/codesign --verify --deep --strict "$STAGING_DIR/Payload/phoneME.app" >/dev/null 2>&1 && SIGNING="signed" || SIGNING="unsigned"

printf 'Created IPA (%s):\n%s\n' "$SIGNING" "$OUTPUT_PATH"
/usr/bin/file "$OUTPUT_PATH"
/usr/bin/du -h "$OUTPUT_PATH"
