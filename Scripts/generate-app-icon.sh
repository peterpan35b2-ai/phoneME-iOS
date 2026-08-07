#!/bin/sh
set -eu

SOURCE_SVG="$SRCROOT/phoneME/Resources/Assets.xcassets/AppIcon.appiconset/AppIcon.svg"
GENERATED_ROOT="$DERIVED_FILE_DIR/phoneMEGeneratedAppIcon"
CATALOG="$GENERATED_ROOT/GeneratedAppIcon.xcassets"
ICON_SET="$CATALOG/AppIcon.appiconset"
OUTPUT_DIR="$TARGET_BUILD_DIR/$UNLOCALIZED_RESOURCES_FOLDER_PATH"

rm -rf "$GENERATED_ROOT"
mkdir -p "$ICON_SET" "$OUTPUT_DIR"

# Recover the original 1860962.png payload embedded in the SVG.
sed -n 's/.*base64,\([^\"]*\)\"\/>.*/\1/p' "$SOURCE_SVG" | /usr/bin/base64 -D > "$GENERATED_ROOT/source.png"

# App Store icons must be square and opaque. Keep the original logo aspect ratio,
# flatten transparency onto white, and leave safe padding for iOS corner masking.
/usr/bin/sips -s format jpeg "$GENERATED_ROOT/source.png" --out "$GENERATED_ROOT/source.jpg" >/dev/null
/usr/bin/sips --resampleHeight 820 "$GENERATED_ROOT/source.jpg" --out "$GENERATED_ROOT/scaled.jpg" >/dev/null
/usr/bin/sips --padToHeightWidth 1024 1024 --padColor FFFFFF "$GENERATED_ROOT/scaled.jpg" --out "$GENERATED_ROOT/padded.jpg" >/dev/null
/usr/bin/sips -s format png "$GENERATED_ROOT/padded.jpg" --out "$ICON_SET/AppIcon.png" >/dev/null

cat > "$CATALOG/Contents.json" <<'JSON'
{
  "info" : {
    "author" : "xcode",
    "version" : 1
  }
}
JSON

cat > "$ICON_SET/Contents.json" <<'JSON'
{
  "images" : [
    {
      "filename" : "AppIcon.png",
      "idiom" : "universal",
      "platform" : "ios",
      "size" : "1024x1024"
    }
  ],
  "info" : {
    "author" : "xcode",
    "version" : 1
  }
}
JSON

/usr/bin/xcrun actool "$CATALOG" \
  --compile "$OUTPUT_DIR" \
  --platform "$PLATFORM_NAME" \
  --minimum-deployment-target "$IPHONEOS_DEPLOYMENT_TARGET" \
  --app-icon AppIcon \
  --target-device iphone \
  --output-partial-info-plist "$GENERATED_ROOT/AppIcon-Info.plist" \
  --warnings --errors --notices
