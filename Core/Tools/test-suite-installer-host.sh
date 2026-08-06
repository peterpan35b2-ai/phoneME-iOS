#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
if [[ -f "$SCRIPT_DIR/lib/common-test-root.sh" ]]; then
  source "$SCRIPT_DIR/lib/common-test-root.sh"
else
  # Item 05 must remain testable before item 18's shared tooling lands.
  phoneme_make_isolated_root() {
    local core_root="$1"
    local label="$2"
    local override="${3:-}"
    local base="${override:-${TMPDIR:-/tmp}}"
    mkdir -p "$base"
    mktemp -d "$base/${label}.$$.XXXXXX"
  }
  phoneme_register_cleanup() {
    local root="$1"
    trap 'rm -rf -- '"'"'$root'"'"'' EXIT
  }
  phoneme_configure_sanitizers() {
    if [[ "${PHONEME_SANITIZE:-0}" == "1" ]]; then
      PHONEME_SANITIZER_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
    else
      PHONEME_SANITIZER_FLAGS=""
    fi
  }
  phoneme_run_with_timeout() {
    shift
    "$@"
  }
fi
TEST_ROOT="$(phoneme_make_isolated_root "$CORE_ROOT" "suite-installer-host-tests" "${PHONEME_SUITE_INSTALLER_TEST_ROOT:-}")"
phoneme_register_cleanup "$TEST_ROOT"
STUB_CLASSES="$TEST_ROOT/stub-classes"
FIXTURE_CLASSES="$TEST_ROOT/fixture-classes"
TEST_BINARY="$TEST_ROOT/SuiteInstallerTests"
CXX="$(xcrun --sdk macosx --find clang++)"
SDK_ROOT="$(xcrun --sdk macosx --show-sdk-path)"
JAVAC="${JAVAC:-$(command -v javac)}"
JAR="${JAR:-$(command -v jar)}"
PYTHON="${PYTHON:-$(command -v python3)}"
phoneme_configure_sanitizers
SANITIZER_FLAGS="$PHONEME_SANITIZER_FLAGS"

if [[ -z "$JAVAC" || -z "$JAR" || -z "$PYTHON" ]]; then
  echo "javac, jar and python3 are required for suite installer tests." >&2
  exit 1
fi

mkdir -p "$STUB_CLASSES" "$FIXTURE_CLASSES" "$TEST_ROOT/manifests"

"$JAVAC" -source 8 -target 8 -Xlint:-options \
  -d "$STUB_CLASSES" \
  "$CORE_ROOT/Tests/stubs/javax/microedition/midlet/MIDlet.java"
"$JAVAC" -source 8 -target 8 -Xlint:-options \
  -classpath "$STUB_CLASSES" -d "$FIXTURE_CLASSES" \
  "$CORE_ROOT/Tests/fixtures/SuiteApp.java"

MANIFEST_V1="$TEST_ROOT/manifests/v1.mf"
MANIFEST_V2="$TEST_ROOT/manifests/v2.mf"
MANIFEST_MISSING="$TEST_ROOT/manifests/missing.mf"
printf '%s\n' \
  'Manifest-Version: 1.0' \
  'MIDlet-Name: Installer Test' \
  'MIDlet-Vendor: phoneME' \
  'MIDlet-Version: 1.0.0' \
  'MicroEdition-Profile: MIDP-2.0' \
  'MicroEdition-Configuration: CLDC-1.1' \
  'MIDlet-1: Installer Test,,SuiteApp' \
  'MIDlet-Permissions: javax.microedition.io.Connector.http, javax.microedition.io.Connector.socket' \
  '' > "$MANIFEST_V1"
printf '%s\n' \
  'Manifest-Version: 1.0' \
  'MIDlet-Name: Installer Test' \
  'MIDlet-Vendor: phoneME' \
  'MIDlet-Version: 1.1.0' \
  'MicroEdition-Profile: MIDP-2.0' \
  'MicroEdition-Configuration: CLDC-1.1' \
  'MIDlet-1: Installer Test,,SuiteApp' \
  'MIDlet-Permissions: javax.microedition.io.Connector.http, javax.microedition.io.Connector.socket' \
  '' > "$MANIFEST_V2"
printf '%s\n' \
  'Manifest-Version: 1.0' \
  'MIDlet-Name: Missing Test' \
  'MIDlet-Vendor: phoneME' \
  'MIDlet-Version: 1.0.0' \
  'MicroEdition-Profile: MIDP-2.0' \
  'MicroEdition-Configuration: CLDC-1.1' \
  'MIDlet-1: Missing Test,,MissingApp' \
  '' > "$MANIFEST_MISSING"

JAR_V1="$TEST_ROOT/app-v1.jar"
JAR_V2="$TEST_ROOT/app-v2.jar"
JAR_V1_CHANGED="$TEST_ROOT/app-v1-changed.jar"
JAR_MISSING="$TEST_ROOT/missing.jar"
JAR_TRAVERSAL="$TEST_ROOT/traversal.jar"
JAR_ZIP_BOMB="$TEST_ROOT/zip-bomb.jar"
"$JAR" cfm "$JAR_V1" "$MANIFEST_V1" -C "$FIXTURE_CLASSES" SuiteApp.class
"$JAR" cfm "$JAR_V2" "$MANIFEST_V2" -C "$FIXTURE_CLASSES" SuiteApp.class
"$JAR" cfm "$JAR_MISSING" "$MANIFEST_MISSING" -C "$FIXTURE_CLASSES" SuiteApp.class

SIGNATURE_ROOT="$TEST_ROOT/signature-fixture"
mkdir -p "$SIGNATURE_ROOT/META-INF"
printf '%s\n' 'Signature-Version: 1.0' > "$SIGNATURE_ROOT/META-INF/PHONE.SF"
printf '%s\n' 'unverified-test-signature-block' > "$SIGNATURE_ROOT/META-INF/PHONE.RSA"
"$JAR" uf "$JAR_V1" -C "$SIGNATURE_ROOT" META-INF/PHONE.SF \
  -C "$SIGNATURE_ROOT" META-INF/PHONE.RSA
"$JAR" uf "$JAR_V2" -C "$SIGNATURE_ROOT" META-INF/PHONE.SF \
  -C "$SIGNATURE_ROOT" META-INF/PHONE.RSA
cp "$JAR_V1" "$JAR_V1_CHANGED"
printf '%s\n' 'same-version changed resource' > "$TEST_ROOT/scoped-resource.txt"
"$JAR" uf "$JAR_V1_CHANGED" -C "$TEST_ROOT" scoped-resource.txt

"$PYTHON" -c '
import pathlib, sys, zipfile
manifest = pathlib.Path(sys.argv[1]).read_bytes()
klass = pathlib.Path(sys.argv[2]).read_bytes()
with zipfile.ZipFile(sys.argv[3], "w", zipfile.ZIP_DEFLATED) as archive:
    archive.writestr("META-INF/MANIFEST.MF", manifest)
    archive.writestr("SuiteApp.class", klass)
    archive.writestr("../escape.txt", b"escape")
' "$MANIFEST_V1" "$FIXTURE_CLASSES/SuiteApp.class" "$JAR_TRAVERSAL"

"$PYTHON" -c '
import pathlib, sys, zipfile
manifest = pathlib.Path(sys.argv[1]).read_bytes()
klass = pathlib.Path(sys.argv[2]).read_bytes()
with zipfile.ZipFile(sys.argv[3], "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
    archive.writestr("META-INF/MANIFEST.MF", manifest)
    archive.writestr("SuiteApp.class", klass)
    archive.writestr("assets/zeroes.bin", bytes(4 * 1024 * 1024))
' "$MANIFEST_V1" "$FIXTURE_CLASSES/SuiteApp.class" "$JAR_ZIP_BOMB"

"$CXX" -std=c++23 -isysroot "$SDK_ROOT" \
  -I"$CORE_ROOT/include" \
  -fno-exceptions -fno-rtti \
  -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow \
  -Werror=return-type $SANITIZER_FLAGS \
  "$CORE_ROOT/Tests/SuiteInstallerTests.cpp" \
  "$CORE_ROOT/src/runtime/JadParser.cpp" \
  "$CORE_ROOT/src/runtime/SuiteInstaller.cpp" \
  "$CORE_ROOT/src/runtime/SuiteDatabase.cpp" \
  "$CORE_ROOT/src/runtime/SuiteStore.cpp" \
  "$CORE_ROOT/src/archive/ZipArchive.cpp" \
  "$CORE_ROOT/src/platform/MappedFile.cpp" \
  "$CORE_ROOT/src/classfile/ClassFile.cpp" \
  "$CORE_ROOT/src/classfile/BytecodeVerifier.cpp" \
  -lz -o "$TEST_BINARY"

phoneme_run_with_timeout "${PHONEME_TEST_TIMEOUT:-300}" \
  "$TEST_BINARY" "$TEST_ROOT/runtime" "$JAR_V1" "$JAR_V2" \
  "$JAR_V1_CHANGED" "$JAR_MISSING" "$JAR_TRAVERSAL" "$JAR_ZIP_BOMB"
