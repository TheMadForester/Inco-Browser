#!/usr/bin/env bash
# Build an AppImage of the current tree. Everything stays inside the repo:
#   build/appimage/build     Release build
#   build/appimage/*.AppDir  staging folder
#   build/appimage/Inco_Browser-<version>-x86_64.AppImage
#   build/tools              linuxdeploy, linuxdeploy-plugin-qt, appimagetool,
#                            AppImage runtime (official GitHub releases)
# The tools are unpacked with --appimage-extract, so no FUSE mount is needed.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/appimage"
BUILD="$OUT/build"
APPDIR="$OUT/IncoBrowser.AppDir"
TOOLS="$ROOT/build/tools"
export TMPDIR="$ROOT/build/tmp"
mkdir -p "$OUT" "$TOOLS" "$TMPDIR"

VER="$(sed -n 's/^project(IncoBrowser VERSION \([0-9][0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
[[ -n $VER ]] || { echo "make-appimage: cannot read version from CMakeLists.txt" >&2; exit 1; }
[[ -x $ROOT/tor/tor ]] || { echo "make-appimage: no tor/ bundle, run scripts/fetch-tor.sh first" >&2; exit 1; }

cmake -G Ninja -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD"

fetch() {   # name url
  [[ -f $TOOLS/$1 ]] && return
  echo "make-appimage: downloading $1"
  curl -L --fail -o "$TOOLS/$1.part" "$2"
  mv "$TOOLS/$1.part" "$TOOLS/$1"
}
GH=https://github.com
fetch linuxdeploy-x86_64.AppImage $GH/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
fetch linuxdeploy-plugin-qt-x86_64.AppImage $GH/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
fetch appimagetool-x86_64.AppImage $GH/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
fetch runtime-x86_64 $GH/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64
mkdir -p "$TOOLS/bin"
for t in linuxdeploy linuxdeploy-plugin-qt appimagetool; do
  if [[ ! -x $TOOLS/$t.d/squashfs-root/AppRun ]]; then
    chmod +x "$TOOLS/$t-x86_64.AppImage"
    rm -rf "$TOOLS/$t.d" && mkdir "$TOOLS/$t.d"
    (cd "$TOOLS/$t.d" && "../$t-x86_64.AppImage" --appimage-extract >/dev/null)
  fi
  ln -sf "../$t.d/squashfs-root/AppRun" "$TOOLS/bin/$t"
done
# The patchelf inside linuxdeploy-plugin-qt (0.15) breaks libraries that use
# DT_RELR (Fedora and other current distros): plugins then crash on load.
# Use the newer patchelf that ships inside linuxdeploy itself.
ln -sf "$TOOLS/linuxdeploy.d/squashfs-root/usr/bin/patchelf" \
       "$TOOLS/linuxdeploy-plugin-qt.d/squashfs-root/usr/bin/patchelf"
export PATH="$TOOLS/bin:$PATH"

rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin"
cp "$BUILD/IncoBrowser" "$APPDIR/usr/bin/"

cat > "$OUT/inco-browser.desktop" <<D
[Desktop Entry]
Name=Inco Browser
Comment=Private-by-default web browser
Exec=IncoBrowser %U
Icon=inco-browser
Type=Application
Categories=Network;WebBrowser;
MimeType=text/html;x-scheme-handler/http;x-scheme-handler/https;
StartupWMClass=IncoBrowser
X-AppImage-Version=$VER
D

# The strip inside linuxdeploy cannot read Fedora's .relr.dyn sections.
export NO_STRIP=1
export QMAKE="${QMAKE:-$(command -v qmake6 || command -v qmake)}"
# Wayland for Plasma/GNOME sessions, offscreen for headless checks.
export EXTRA_PLATFORM_PLUGINS="libqwayland.so;libqoffscreen.so"
linuxdeploy --appdir "$APPDIR" \
  --desktop-file "$OUT/inco-browser.desktop" \
  --icon-file "$ROOT/share/icons/hicolor/256x256/apps/inco-browser.png" \
  --plugin qt

# Tor goes in after linuxdeploy so it is never patched or stripped: the
# integrity check compares it byte for byte with the hashes in the binary.
cp -a "$BUILD/tor" "$APPDIR/usr/bin/tor"
cp "$ROOT/scripts/fetch-tor.sh" "$APPDIR/usr/bin/"
mkdir -p "$APPDIR/usr/share/licenses/inco-browser"
cp "$ROOT/LICENSE" "$APPDIR/usr/share/licenses/inco-browser/"
"$APPDIR/usr/bin/IncoBrowser" --verify-tor | head -1

IMG="$OUT/Inco_Browser-$VER-x86_64.AppImage"
rm -f "$IMG"
ARCH=x86_64 VERSION="$VER" appimagetool --runtime-file "$TOOLS/runtime-x86_64" "$APPDIR" "$IMG"
ls -lh "$IMG"
sha256sum "$IMG"
