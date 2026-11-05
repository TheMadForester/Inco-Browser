#!/usr/bin/env bash
# Wrap an AppDir as a self-contained installer.
#   scripts/pack-installer.sh <AppDir> <output.install>
# The result is scripts/install-appdir.sh with a gzipped tar appended.
# Users run it with bash, so the download does not need chmod.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APPDIR=${1:-}
OUT=${2:-}
[[ -d $APPDIR && -n $OUT ]] || { echo "usage: pack-installer.sh <AppDir> <output>" >&2; exit 2; }
[[ -f $APPDIR/usr/bin/IncoBrowser ]] || { echo "pack-installer: no usr/bin/IncoBrowser in $APPDIR" >&2; exit 1; }

parent=$(cd "$(dirname "$APPDIR")" && pwd)
base=$(basename "$APPDIR")

tmp=$(mktemp)
cat "$ROOT/scripts/install-appdir.sh" > "$tmp"
# The marker line must be the first thing after the script, and the tar
# must follow it with no extra bytes, or extraction reads garbage.
printf '\n__INCO_PAYLOAD__\n' >> "$tmp"
tar -C "$parent" -czf - "$base" >> "$tmp"
chmod 644 "$tmp"
mv "$tmp" "$OUT"
echo "pack-installer: $OUT"
