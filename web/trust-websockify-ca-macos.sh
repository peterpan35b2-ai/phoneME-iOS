#!/bin/sh
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
CA_CERT="$ROOT_DIR/Artifacts/phoneME-websockify-local-ca.crt"
KEYCHAIN="$HOME/Library/Keychains/login.keychain-db"

if [ "$(uname -s)" != "Darwin" ]; then
  echo "This helper is for macOS only." >&2
  exit 1
fi

if [ ! -f "$CA_CERT" ]; then
  echo "Missing CA certificate: $CA_CERT" >&2
  exit 1
fi

echo "Trusting phoneME Local Development CA for SSL in the user keychain..."
security add-trusted-cert \
  -r trustRoot \
  -p ssl \
  -k "$KEYCHAIN" \
  "$CA_CERT"

echo "CA trusted. Fully quit and reopen Chrome/Safari before retrying WSS."
