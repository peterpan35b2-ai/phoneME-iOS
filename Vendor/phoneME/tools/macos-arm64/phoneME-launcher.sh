#!/bin/sh
set -eu

CONTENTS_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
RESOURCES_DIR="$CONTENTS_DIR/Resources"
RUNTIME_HOME="$HOME/Library/Application Support/phoneME"

mkdir -p "$RUNTIME_HOME"

# Keep writable suite/RMS state outside the signed application bundle.
if [ ! -d "$RUNTIME_HOME/appdb" ]; then
    /usr/bin/ditto "$RESOURCES_DIR/runtime-template/appdb" "$RUNTIME_HOME/appdb"
fi

# Refresh read-only configuration and skin resources on every app update while
# preserving writable suite/RMS state in appdb.
/usr/bin/ditto "$RESOURCES_DIR/runtime-template/lib" "$RUNTIME_HOME/lib"

export MIDP_HOME="$RUNTIME_HOME"

# Ignore the legacy process-serial argument Finder may append.
case "${1:-}" in
    -psn_*) set -- ;;
esac

RUNNER="$CONTENTS_DIR/MacOS/runMidlet"
CLASSPATH="$RESOURCES_DIR/classes.zip"

install_suite() {
    if [ "$#" -lt 1 ]; then
        echo "Usage: phoneME install <game.jar|game.jad|URL>" >&2
        exit 2
    fi

    SOURCE=$1
    case "$SOURCE" in
        http://*|https://*|file:*) ;;
        /*) SOURCE="file://$SOURCE" ;;
        *) SOURCE="file://$(pwd)/$SOURCE" ;;
    esac

    exec "$RUNNER" \
        =HeapCapacity64M \
        -classpathext "$CLASSPATH" \
        -1 com.sun.midp.scriptutil.CommandLineInstaller I "$SOURCE"
}

case "${1:-}" in
    install)
        shift
        install_suite "$@"
        ;;
    list)
        exec "$RUNNER" \
            =HeapCapacity64M \
            -classpathext "$CLASSPATH" \
            -1 com.sun.midp.scriptutil.SuiteLister
        ;;
    help|--help|-h)
        cat <<'EOF'
Usage:
  phoneME                         Open the J2ME application manager
  phoneME install <file-or-URL>  Install a .jar or .jad suite
  phoneME list                   List installed suites
  phoneME <runMidlet arguments>  Pass arguments directly to runMidlet

Controls:
  Arrow keys                     D-pad
  Enter / Space                  Select
  F1 / Page Up / [               Left softkey
  F2 / Page Down / ]             Right softkey
  Esc / End                      Back / End
EOF
        exit 0
        ;;
    *.jar|*.JAR|*.jad|*.JAD)
        install_suite "$1"
        ;;
esac

if [ "$#" -gt 0 ]; then
    exec "$RUNNER" \
        =HeapCapacity512M \
        -classpathext "$CLASSPATH" "$@"
fi

exec "$RUNNER" \
    =HeapCapacity512M \
    -classpathext "$CLASSPATH" \
    internal com.sun.midp.appmanager.Manager
