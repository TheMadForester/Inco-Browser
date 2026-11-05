#!/usr/bin/env bash
# Write the small Install download. It is a script, not a copy of the browser.
#   scripts/emit-downloader.sh <version> <output>
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VER=${1:-}
OUT=${2:-}
[[ -n $VER && -n $OUT ]] || { echo "usage: emit-downloader.sh <version> <output>" >&2; exit 2; }
{
  printf '#!/usr/bin/env bash\nINCO_INSTALL_LIBRARY=1\nINCO_VERSION=%s\n' "$VER"
  tail -n +2 "$ROOT/scripts/install-appdir.sh"
  printf '\n'
  tail -n +2 "$ROOT/scripts/download-install.sh"
} > "$OUT"
chmod 644 "$OUT"
echo "emit-downloader: $OUT"
