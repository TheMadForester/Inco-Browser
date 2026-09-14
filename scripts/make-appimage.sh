#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build-release"
DIST="$ROOT/dist"
APPDIR="$DIST/IncoBrowser.AppDir"

cmake -G Ninja -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD"
[[ -x $BUILD/tor/tor || -x $ROOT/tor/tor ]] || "$ROOT/scripts/fetch-tor.sh"
cmake --build "$BUILD"

rm -rf "$DIST"
mkdir -p "$APPDIR/usr/bin" \
         "$APPDIR/usr/share/applications" \
         "$APPDIR/usr/share/icons/hicolor/256x256/apps"

cp "$BUILD/IncoBrowser" "$APPDIR/usr/bin/"
if [[ -d $BUILD/tor ]]; then
  cp -a "$BUILD/tor" "$APPDIR/usr/bin/tor"
else
  cp -a "$ROOT/tor" "$APPDIR/usr/bin/tor"
fi
cp "$ROOT/scripts/fetch-tor.sh" "$APPDIR/usr/bin/" || true

ICON="$ROOT/resources/inco-browser.png"
[[ -f $ICON ]] || ICON="$ROOT/resources/inco.png"
cp "$ICON" "$APPDIR/usr/share/icons/hicolor/256x256/apps/inco-browser.png"
cp "$ICON" "$APPDIR/inco-browser.png"

cat > "$APPDIR/inco-browser.desktop" <<D
[Desktop Entry]
Name=Inco Browser
Exec=IncoBrowser
Icon=inco-browser
Type=Application
Categories=Network;WebBrowser;
StartupWMClass=IncoBrowser
D
cp "$APPDIR/inco-browser.desktop" "$APPDIR/usr/share/applications/"

cat > "$APPDIR/AppRun" <<'R'
#!/bin/bash
HERE="$(dirname "$(readlink -f "$0")")"
export PATH="$HERE/usr/bin:$PATH"
export LD_LIBRARY_PATH="$HERE/usr/bin/tor:$HERE/usr/bin/tor/lib:$HERE/usr/lib:$HERE/usr/lib64:${LD_LIBRARY_PATH:-}"
exec "$HERE/usr/bin/IncoBrowser" "$@"
R
chmod +x "$APPDIR/AppRun"

echo "AppDir ready: $APPDIR"

if ! command -v linuxdeploy >/dev/null; then
  mkdir -p "$HOME/Tools"
  cd "$HOME/Tools"
  curl -L --fail -o linuxdeploy-x86_64.AppImage \
    https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
  curl -L --fail -o linuxdeploy-plugin-qt-x86_64.AppImage \
    https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
  chmod +x linuxdeploy-x86_64.AppImage linuxdeploy-plugin-qt-x86_64.AppImage
  export PATH="$HOME/Tools:$PATH"
fi

cd "$DIST"
linuxdeploy --appdir "$APPDIR" --plugin qt --output appimage
ls -lh "$DIST"/*.AppImage
