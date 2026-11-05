#!/usr/bin/env bash
# The menu installer must only touch its own files, must leave the launcher
# executable, and must keep a second version alone.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
INSTALL="$ROOT/scripts/install-appdir.sh"
PACK="$ROOT/scripts/pack-installer.sh"

tmp=$(mktemp -d)
trap 'chmod -R u+w "$tmp" 2>/dev/null || true; rm -rf "$tmp"' EXIT

home=$tmp/home
mkdir -p "$home"
export HOME=$home
unset PREFIX

appdir=$tmp/IncoBrowser.AppDir
mkdir -p "$appdir/usr/bin/tor" \
         "$appdir/usr/share/icons/hicolor/256x256/apps" \
         "$appdir/usr/share/applications"
cat > "$appdir/usr/bin/IncoBrowser" <<'EOF'
#!/bin/sh
if [ "$1" = "--verify-tor" ]; then
  echo "tor bundle ok"
  exit 0
fi
if [ "$INCO_HOOK_OK" != 1 ]; then
  echo "hook did not run" >&2
  exit 1
fi
if [ "$1" = "--version" ]; then
  echo "Inco Browser 1.0.1"
  exit 0
fi
echo "ran $*"
exit 0
EOF
chmod 644 "$appdir/usr/bin/IncoBrowser"
printf 'tor-bytes\n' > "$appdir/usr/bin/tor/tor"
chmod 644 "$appdir/usr/bin/tor/tor"
printf 'icon-bytes\n' > "$appdir/usr/share/icons/hicolor/256x256/apps/inco-browser.png"
mkdir -p "$appdir/apprun-hooks"
cat > "$appdir/apprun-hooks/linuxdeploy-plugin-qt-hook.sh" <<'EOF'
INCO_HOOK_OK=1
export INCO_HOOK_OK
EOF
# linuxdeploy's AppRun locates hooks with dirname "$0" and does not
# resolve a symlink. A bin/ symlink would look in the wrong directory.
cat > "$appdir/AppRun" <<'EOF'
#!/bin/sh
here=$(dirname "$0")
if [ ! -f "$here/apprun-hooks/linuxdeploy-plugin-qt-hook.sh" ]; then
  echo "no hooks next to $0" >&2
  exit 1
fi
. "$here/apprun-hooks/linuxdeploy-plugin-qt-hook.sh"
exec "$here/usr/bin/IncoBrowser" "$@"
EOF
chmod 755 "$appdir/AppRun"
cat > "$appdir/inco-browser.desktop" <<'EOF'
[Desktop Entry]
Name=Inco Browser
Exec=IncoBrowser %U
X-AppImage-Version=1.0.1
EOF

# Unrelated files the installer must not change or remove.
printf 'keep\n' > "$home/notes.txt"
mkdir -p "$home/.local/share/applications" "$home/.local/bin" "$home/.local/lib/inco-browser/0.9.0" \
         "$home/.config"
printf 'other\n' > "$home/.local/share/applications/other.desktop"
printf 'tool\n' > "$home/.local/bin/keep-me"
chmod 755 "$home/.local/bin/keep-me"
printf 'old\n' > "$home/.local/lib/inco-browser/0.9.0/marker"
mkdir -p "$home/.local/lib/inco-browser/0.7.0"
printf 'version=0.7.0\nkind=user\n' > "$home/.local/lib/inco-browser/0.7.0/inco-install.txt"
printf 'stamped-old\n' > "$home/.local/lib/inco-browser/0.7.0/payload"
printf 'stray\n' > "$home/.local/lib/inco-browser/notes"
printf 'cfg\n' > "$home/.config/keep.conf"
mkdir -p "$home/.config/autostart"
printf 'no\n' > "$home/.config/autostart/unrelated.desktop"

PREFIX="$home/.local" bash "$INSTALL" "$appdir"

# Terminal launch goes through the wrapper, which execs the real AppRun.
launcher=$home/.local/bin/inco-browser
[[ -f $launcher && ! -L $launcher ]]
[[ -x $launcher ]]
grep -q '^# inco-install: user$' "$launcher"
grep -q "exec '$home/.local/lib/inco-browser/current/AppRun'" "$launcher"
out=$("$launcher" --version)
[[ $out == "Inco Browser 1.0.1" ]] || { echo "launcher printed: $out" >&2; exit 1; }
# The same AppRun, reached through a symlink, looks for hooks next to the link.
ln -s "$home/.local/lib/inco-browser/current/AppRun" "$home/.local/bin/via-symlink"
if "$home/.local/bin/via-symlink" --version >/tmp/inco-symlink.out 2>&1; then
  echo "symlink launch found hooks; AppRun would be fine as a symlink" >&2
  exit 1
fi
grep -q 'no hooks next to' /tmp/inco-symlink.out
rm -f "$home/.local/bin/via-symlink"
[[ -x $home/.local/lib/inco-browser/1.0.1/usr/bin/IncoBrowser ]]
[[ -x $home/.local/lib/inco-browser/1.0.1/usr/bin/tor/tor ]]

desk=$home/.local/share/applications/inco-browser.desktop
grep -q '^X-Inco-Install=user$' "$desk"
grep -q '^Name=Inco Browser$' "$desk"
[[ $(grep '^Exec=' "$desk") == "Exec=\"$home/.local/lib/inco-browser/current/AppRun\" %U" ]]
[[ $(grep '^Icon=' "$desk") == "Icon=$home/.local/share/icons/hicolor/256x256/apps/inco-browser.png" ]]
grep -q '^Terminal=false$' "$desk"
# No autostart entry of ours.
find "$home/.config/autostart" -type f -printf '%f\n' | grep -qx 'unrelated.desktop'

# Read-only tree, stamp present, older version untouched.
[[ ! -w $home/.local/lib/inco-browser/1.0.1/inco-install.txt ]]
grep -q '^kind=user$' "$home/.local/lib/inco-browser/1.0.1/inco-install.txt"
[[ ! -e $home/.local/lib/inco-browser/0.7.0 ]]
grep -q '^old$' "$home/.local/lib/inco-browser/0.9.0/marker"
grep -q '^stray$' "$home/.local/lib/inco-browser/notes"
grep -q '^keep$' "$home/notes.txt"
grep -q '^other$' "$home/.local/share/applications/other.desktop"
grep -q '^tool$' "$home/.local/bin/keep-me"
grep -q '^cfg$' "$home/.config/keep.conf"
cmp -s "$home/.local/share/icons/hicolor/256x256/apps/inco-browser.png" \
      "$home/.local/lib/inco-browser/1.0.1/usr/share/icons/hicolor/256x256/apps/inco-browser.png"

# Replacing the icon by hand must survive uninstall.
printf 'user-icon\n' > "$home/.local/share/icons/hicolor/256x256/apps/inco-browser.png"
# A desktop file the user rewrote, without our marker, must survive too.
printf 'custom\n' > "$desk"

# Packed installer reinstalls over the same version. Mode 644, run with bash.
packed=$tmp/Inco_Browser-1.0.1-x86_64.install
"$PACK" "$appdir" "$packed"
chmod 644 "$packed"
bash "$packed"

[[ -x $home/.local/lib/inco-browser/1.0.1/usr/bin/IncoBrowser ]]
grep -q '^X-Inco-Install=user$' "$desk"
# The user's icon was replaced by the reinstall, which is the install step.
# Uninstall must not delete an icon that no longer matches the bundle.
printf 'user-icon\n' > "$home/.local/share/icons/hicolor/256x256/apps/inco-browser.png"
mkdir -p "$home/.local/lib/inco-browser/2.0.0"
printf 'version=2.0.0\nkind=user\n' > "$home/.local/lib/inco-browser/2.0.0/inco-install.txt"

bash "$packed" --uninstall
[[ ! -e $home/.local/lib/inco-browser/1.0.1 ]]
[[ ! -e $home/.local/lib/inco-browser/2.0.0 ]]
[[ ! -e $home/.local/lib/inco-browser/current ]]
[[ ! -e $home/.local/bin/inco-browser ]]
[[ ! -e $desk ]]
[[ -f $home/.local/share/icons/hicolor/256x256/apps/inco-browser.png ]]
grep -q '^user-icon$' "$home/.local/share/icons/hicolor/256x256/apps/inco-browser.png"
grep -q '^old$' "$home/.local/lib/inco-browser/0.9.0/marker"
grep -q '^stray$' "$home/.local/lib/inco-browser/notes"
grep -q '^other$' "$home/.local/share/applications/other.desktop"
grep -q '^keep$' "$home/notes.txt"
grep -q '^cfg$' "$home/.config/keep.conf"
grep -q '^no$' "$home/.config/autostart/unrelated.desktop"

# A tree without our stamp is not deleted.
mkdir -p "$home/.local/lib/inco-browser/1.0.1"
printf 'source\n' > "$home/.local/lib/inco-browser/1.0.1/marker"
ln -s 1.0.1 "$home/.local/lib/inco-browser/current"
if bash "$INSTALL" --uninstall; then
  echo "uninstall deleted a tree that was not a menu install" >&2
  exit 1
fi
grep -q '^source$' "$home/.local/lib/inco-browser/1.0.1/marker"

# PREFIX outside the home directory is refused.
if PREFIX=/tmp bash "$INSTALL" "$appdir"; then
  echo "installer accepted a prefix outside HOME" >&2
  exit 1
fi

# Nothing new appeared under autostart.
[[ $(find "$home/.config/autostart" -type f | wc -l) -eq 1 ]]

echo "user install checks passed"
