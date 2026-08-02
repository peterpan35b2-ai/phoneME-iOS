#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

bash "$REPO_ROOT/Core/tools/build-iphoneos.sh"
bash "$REPO_ROOT/Core/tools/verify-iphoneos.sh"
