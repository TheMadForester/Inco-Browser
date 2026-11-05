#!/usr/bin/env bash
# Reinstall rollback, desktop quoting, and a launcher that is not a symlink.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
INSTALL="$ROOT/scripts/install-appdir.sh"
tmp=$(mktemp -d)
trap 'chmod -R u+w "$tmp" 2>/dev/null || true; rm -rf "$tmp"' EXIT

make_appdir() {
  local dir=$1 id=$2
  mkdir -p "$dir/usr/bin/tor" "$dir/usr/share/icons/hicolor/256x256/apps" "$dir/apprun-hooks"
  printf '%s\n' "$id" > "$dir/usr/bin/build-id"
  cat > "$dir/usr/bin/IncoBrowser" <<'EOF'
#!/bin/sh
if [ "$1" = "--verify-tor" ]; then
  echo ok
  exit 0
fi
if [ "$INCO_HOOK_OK" != 1 ]; then
  echo "hook did not run" >&2
  exit 1
fi
if [ "$1" = "--version" ]; then
  cat "$(dirname "$0")/build-id"
  exit 0
fi
exit 0
EOF
  chmod 644 "$dir/usr/bin/IncoBrowser"
  printf 'tor\n' > "$dir/usr/bin/tor/tor"
  printf 'icon\n' > "$dir/usr/share/icons/hicolor/256x256/apps/inco-browser.png"
  printf 'INCO_HOOK_OK=1\nexport INCO_HOOK_OK\n' > "$dir/apprun-hooks/linuxdeploy-plugin-qt-hook.sh"
  cat > "$dir/AppRun" <<'EOF'
#!/bin/sh
here=$(dirname "$0")
if [ ! -f "$here/apprun-hooks/linuxdeploy-plugin-qt-hook.sh" ]; then
  echo "no hooks next to $0" >&2
  exit 1
fi
. "$here/apprun-hooks/linuxdeploy-plugin-qt-hook.sh"
exec "$here/usr/bin/IncoBrowser" "$@"
EOF
  chmod 755 "$dir/AppRun"
  cat > "$dir/inco-browser.desktop" <<'EOF'
[Desktop Entry]
Name=Inco Browser
X-AppImage-Version=1.0.1
EOF
}

home=$tmp/home
mkdir -p "$home"
export HOME=$home
app1=$tmp/one
app2=$tmp/two
make_appdir "$app1" one
make_appdir "$app2" two
PREFIX="$home/.local" bash "$INSTALL" "$app1"
PREFIX="$home/.local" bash "$INSTALL" "$app2"
id=$("$home/.local/bin/inco-browser" --version)
[[ $id == two ]] || { echo "reinstall left: $id" >&2; exit 1; }
[[ -z $(find "$home/.local/lib/inco-browser" -maxdepth 1 \( -name '.previous-*' -o -name '.staging-*' \)) ]]

app3=$tmp/three
make_appdir "$app3" three
if INCO_INSTALL_ABORT=after-rename PREFIX="$home/.local" bash "$INSTALL" "$app3" \
    >"$tmp/abort.out" 2>"$tmp/abort.err"; then
  echo "reinstall abort published the new tree" >&2
  exit 1
fi
grep -q 'aborted before publishing the new tree' "$tmp/abort.err"
id=$("$home/.local/bin/inco-browser" --version)
[[ $id == two ]] || { echo "rollback left: $id" >&2; exit 1; }
[[ -z $(find "$home/.local/lib/inco-browser" -maxdepth 1 \( -name '.previous-*' -o -name '.staging-*' \)) ]]
grep -q '^two$' "$home/.local/lib/inco-browser/1.0.1/usr/bin/build-id"

# A command the user wrote in bin/ is not a menu launcher, so it stays.
printf '#!/bin/sh\necho custom\n' > "$home/.local/bin/inco-browser"
chmod 755 "$home/.local/bin/inco-browser"
PREFIX="$home/.local" bash "$INSTALL" "$app2"
[[ $(cat "$home/.local/bin/inco-browser") == '#!/bin/sh
echo custom' ]]
out=$("$home/.local/lib/inco-browser/current/AppRun" --version)
[[ $out == two ]]

# Exec is a quoted Desktop Entry argument. Icon keeps spaces and a raw %.
weird="$tmp/home dir%"
mkdir -p "$weird"
HOME="$weird" PREFIX="$weird/.local" bash "$INSTALL" "$app1"
desk="$weird/.local/share/applications/inco-browser.desktop"
launch="$weird/.local/lib/inco-browser/current/AppRun"
icon="$weird/.local/share/icons/hicolor/256x256/apps/inco-browser.png"
[[ $(grep '^Exec=' "$desk") == "Exec=\"${launch//%/%%}\" %U" ]]
[[ $(grep '^Icon=' "$desk") == "Icon=$icon" ]]
[[ $(grep '^Icon=' "$desk") != *\\* ]]
[[ $("$weird/.local/bin/inco-browser" --version) == one ]]
[[ $("$launch" --version) == one ]]
grep -F -q "exec '$launch'" "$weird/.local/bin/inco-browser"

echo "install edge checks passed"
