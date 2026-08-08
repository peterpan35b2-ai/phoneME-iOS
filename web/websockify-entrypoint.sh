#!/bin/sh
set -eu

CERT_DIR="${PHONEME_TLS_DIR:-/certs}"
CA_CERT_PATH="${PHONEME_TLS_CA_CERT:-$CERT_DIR/ca.crt}"
CA_KEY_PATH="${PHONEME_TLS_CA_KEY:-$CERT_DIR/ca.key}"
CERT_PATH="${PHONEME_TLS_CERT:-$CERT_DIR/tls.crt}"
KEY_PATH="${PHONEME_TLS_KEY:-$CERT_DIR/tls.key}"
CSR_PATH="$CERT_DIR/tls.csr"
TLS_CN="${PHONEME_TLS_CN:-localhost}"
TLS_SAN="${PHONEME_TLS_SAN:-DNS:localhost,IP:127.0.0.1,DNS:host.docker.internal}"
TLS_MODE="${PHONEME_TLS_MODE:-on}"

if [ "$TLS_MODE" = "off" ]; then
  exec python -m websockify_compat \
    --verbose \
    --token-plugin host_port_token.HostPortToken \
    0.0.0.0:38473 \
    "$@"
fi

mkdir -p "$CERT_DIR"

if [ ! -f "$CA_CERT_PATH" ] || [ ! -f "$CA_KEY_PATH" ]; then
  echo "phoneME websockify TLS: generating persistent local development CA"
  rm -f "$CA_CERT_PATH" "$CA_KEY_PATH" "$CERT_PATH" "$KEY_PATH" "$CSR_PATH"
  openssl req \
    -x509 \
    -nodes \
    -newkey rsa:3072 \
    -sha256 \
    -days 3650 \
    -subj "/CN=phoneME Local Development CA" \
    -addext "basicConstraints=critical,CA:TRUE" \
    -addext "keyUsage=critical,keyCertSign,cRLSign" \
    -addext "subjectKeyIdentifier=hash" \
    -keyout "$CA_KEY_PATH" \
    -out "$CA_CERT_PATH"
fi

renew_server_cert=0
if [ ! -f "$CERT_PATH" ] || [ ! -f "$KEY_PATH" ]; then
  renew_server_cert=1
elif ! openssl verify -CAfile "$CA_CERT_PATH" "$CERT_PATH" >/dev/null 2>&1; then
  renew_server_cert=1
elif ! openssl x509 -checkend 2592000 -noout -in "$CERT_PATH" >/dev/null 2>&1; then
  renew_server_cert=1
fi

if [ "$renew_server_cert" -eq 1 ]; then
  echo "phoneME websockify TLS: issuing localhost server certificate from local CA"
  rm -f "$CERT_PATH" "$KEY_PATH" "$CSR_PATH"
  openssl req \
    -nodes \
    -newkey rsa:2048 \
    -sha256 \
    -subj "/CN=$TLS_CN" \
    -addext "subjectAltName=$TLS_SAN" \
    -addext "extendedKeyUsage=serverAuth" \
    -addext "keyUsage=critical,digitalSignature,keyEncipherment" \
    -keyout "$KEY_PATH" \
    -out "$CSR_PATH"
  openssl x509 \
    -req \
    -in "$CSR_PATH" \
    -CA "$CA_CERT_PATH" \
    -CAkey "$CA_KEY_PATH" \
    -CAcreateserial \
    -sha256 \
    -days 365 \
    -copy_extensions copy \
    -out "$CERT_PATH"
  rm -f "$CSR_PATH"
fi

chmod 600 "$CA_KEY_PATH" "$KEY_PATH"
chmod 644 "$CA_CERT_PATH" "$CERT_PATH"

if ! openssl verify -CAfile "$CA_CERT_PATH" "$CERT_PATH" >/dev/null; then
  echo "phoneME websockify TLS: generated certificate chain is invalid" >&2
  exit 1
fi

exec python -m websockify_compat \
  --verbose \
  --ssl-only \
  --ssl-version tlsv1_2 \
  --cert "$CERT_PATH" \
  --key "$KEY_PATH" \
  --token-plugin host_port_token.HostPortToken \
  0.0.0.0:38473 \
  "$@"
