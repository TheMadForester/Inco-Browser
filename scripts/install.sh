#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PREFIX="${PREFIX:-$HOME/.local}"

if [[ ! -x $ROOT/build/IncoBrowser ]]; then
  cmake -G Ninja -S "$ROOT" -B "$ROOT/build" -DCMAKE_BUILD_TYPE=Release
  cmake --build "$ROOT/build"
fi
BIN="$ROOT/build/IncoBrowser"
[[ -x $BIN ]] || { echo "build failed"; exit 1; }

if [[ ! -x $ROOT/tor/tor ]]; then
  "$ROOT/scripts/fetch-tor.sh"
fi

mkdir -p "$PREFIX/bin" "$PREFIX/lib/inco-browser" \
         "$PREFIX/share/applications" \
         "$PREFIX/share/icons/hicolor/256x256/apps"

cp "$BIN" "$PREFIX/bin/IncoBrowser"
chmod +x "$PREFIX/bin/IncoBrowser"
rm -rf "$PREFIX/lib/inco-browser/tor"
cp -a "$ROOT/tor" "$PREFIX/lib/inco-browser/tor"

ICON="$ROOT/resources/inco-browser.png"
[[ -f $ICON ]] || ICON="$ROOT/resources/inco.png"
cp "$ICON" "$PREFIX/share/icons/hicolor/256x256/apps/inco-browser.png"

cat > "$PREFIX/share/applications/inco-browser.desktop" <<D
[Desktop Entry]
Name=Inco Browser
Comment=Private-by-default browser
Exec=env INCO_TOR_DIR=$PREFIX/lib/inco-browser/tor $PREFIX/bin/IncoBrowser
Icon=inco-browser
Type=Application
Categories=Network;WebBrowser;
StartupWMClass=IncoBrowser
Terminal=false
D
update-desktop-database "$PREFIX/share/applications" 2>/dev/null || true
echo "Installed $PREFIX/bin/IncoBrowser"
