#!/usr/bin/env bash
set -euo pipefail

echo "The independent phoneME Core supports iphoneos arm64 only (iOS 15.0+)." >&2
echo "Simulator core builds are intentionally disabled." >&2
exit 2
