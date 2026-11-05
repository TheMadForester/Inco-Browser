#!/usr/bin/env bash
# Packaging and installer checks. They do not build Qt or download Tor.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
bash "$ROOT/tests/appdir-plugins.sh"
bash "$ROOT/tests/user-install.sh"
bash "$ROOT/tests/install-edge.sh"
bash "$ROOT/tests/download-install.sh"
echo "all checks passed"
