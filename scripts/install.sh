#!/usr/bin/env bash
# Per-user install of a source build, no sudo.
# The release "Install" file is scripts/download-install.sh. It downloads
# the AppImage. This script is only for a local build.
# Layout:
#   ~/.local/lib/inco-browser/<version>/IncoBrowser
#   ~/.local/lib/inco-browser/<version>/tor/        (hash-checked at runtime)
#   ~/.local/lib/inco-browser/current -> <version>
#   ~/.local/bin/IncoBrowser -> ../lib/inco-browser/current/IncoBrowser
# The version tree is made read-only (chmod a-w). That only stops accidents;
# the real protection is the integrity check against hashes baked into the
# binary at build time.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PREFIX="${PREFIX:-$HOME/.local}"
BUILD="${BUILD_DIR:-$ROOT/build}"

VER="$(sed -n 's/^project(IncoBrowser VERSION \([0-9][0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
[[ -n $VER ]] || { echo "install: cannot read version from CMakeLists.txt" >&2; exit 1; }

if [[ ! -x $ROOT/tor/tor ]]; then
  "$ROOT/scripts/fetch-tor.sh"
fi

# Always (re)build so the baked hashes match the current tor/.
if [[ ! -f $BUILD/CMakeCache.txt ]]; then
  cmake -G Ninja -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
fi
cmake --build "$BUILD"
BIN="$BUILD/IncoBrowser"
[[ -x $BIN ]] || { echo "install: build failed" >&2; exit 1; }

LIBROOT="$PREFIX/lib/inco-browser"
DEST="$LIBROOT/$VER"
STAGE="$LIBROOT/.staging-$VER-$$"
mkdir -p "$LIBROOT" "$PREFIX/bin" \
         "$PREFIX/share/applications" \
         "$PREFIX/share/icons/hicolor/256x256/apps"

trap '[[ -d $STAGE ]] && { chmod -R u+w "$STAGE"; rm -rf "$STAGE"; }' EXIT
mkdir -p "$STAGE"
cp "$BIN" "$STAGE/IncoBrowser"
chmod 755 "$STAGE/IncoBrowser"
cp -a "$ROOT/tor" "$STAGE/tor"
cp "$ROOT/LICENSE" "$STAGE/LICENSE"

echo "install: checking staged Tor bundle"
env -u INCO_TOR_DIR "$STAGE/IncoBrowser" --verify-tor \
  || { echo "install: integrity check of staged tree failed; nothing installed" >&2; exit 1; }

if [[ -e $DEST ]]; then
  chmod -R u+w "$DEST"
  rm -rf "$DEST"
fi
mv "$STAGE" "$DEST"
chmod -R a-w "$DEST"
ln -sfn "$VER" "$LIBROOT/current"

# Legacy (pre-0.2) layout: plain binary in bin/, unversioned tor/.
if [[ -d $LIBROOT/tor && ! -L $LIBROOT/tor ]]; then
  chmod -R u+w "$LIBROOT/tor" 2>/dev/null || true
  rm -rf "$LIBROOT/tor"
fi
if [[ -e $PREFIX/bin/IncoBrowser || -L $PREFIX/bin/IncoBrowser ]]; then
  rm -f "$PREFIX/bin/IncoBrowser"
fi
ln -s "../lib/inco-browser/current/IncoBrowser" "$PREFIX/bin/IncoBrowser"

ICON="$ROOT/resources/inco-browser.png"
[[ -f $ICON ]] || ICON="$ROOT/resources/inco.png"
cp "$ICON" "$PREFIX/share/icons/hicolor/256x256/apps/inco-browser.png"

# No INCO_TOR_DIR: the binary finds <version>/tor via /proc/self/exe.
cat > "$PREFIX/share/applications/inco-browser.desktop" <<D
[Desktop Entry]
Name=Inco Browser
Comment=Private-by-default browser
Exec=$PREFIX/bin/IncoBrowser
Icon=inco-browser
Type=Application
Categories=Network;WebBrowser;
StartupWMClass=IncoBrowser
Terminal=false
D
update-desktop-database "$PREFIX/share/applications" 2>/dev/null || true

echo "Installed Inco Browser $VER"
echo "  $DEST (read-only)"
echo "  $PREFIX/bin/IncoBrowser -> ../lib/inco-browser/current/IncoBrowser"
others="$(find "$LIBROOT" -mindepth 1 -maxdepth 1 -type d ! -name "$VER" ! -name '.staging-*' -printf '%f ' 2>/dev/null || true)"
[[ -n $others ]] && echo "  other versions kept: $others(remove with chmod -R u+w + rm -rf)"
exit 0
