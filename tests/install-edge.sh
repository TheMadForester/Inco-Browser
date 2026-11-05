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

# Desktop-file unescape runs before Exec parsing, so escapes are doubled.
h="$tmp/"$'sp a%b"c$d\\e`f'
mkdir -p "$h"
HOME="$h" PREFIX="$h/.local" bash "$INSTALL" "$app1"
launch="$h/.local/lib/inco-browser/current/AppRun"
desk="$h/.local/share/applications/inco-browser.desktop"
export INCO_TEST_PATH="$launch"
expected=$(python3 - <<'PY'
import os
path = os.environ["INCO_TEST_PATH"]
out = []
for c in path:
    if c == "\\":
        out.append("\\\\\\\\")
    elif c in '"`$':
        out.append("\\\\" + c)
    elif c == "%":
        out.append("%%")
    else:
        out.append(c)
print('Exec="' + "".join(out) + '" %U')
PY
)
[[ $(grep '^Exec=' "$desk") == "$expected" ]]
[[ $(grep '^Icon=' "$desk") == "Icon=$h/.local/share/icons/hicolor/256x256/apps/inco-browser.png" ]]
[[ $("$launch" --version) == one ]]

# Unused side dirs from a previous run go away, including a read-only one.
# A side dir whose executable is still running stays, then goes once it exits.
# A shell whose command line merely mentions the path does not count.
lib="$home/.local/lib/inco-browser"
mkdir -p "$lib/.staging-1.0.0-8" "$lib/.previous-1.0.0-9/usr/bin"
echo x > "$lib/.staging-1.0.0-8/n"
echo y > "$lib/.previous-1.0.0-9/usr/bin/z"
chmod -R a-w "$lib/.previous-1.0.0-9"
busy="$lib/.previous-0.8.0-4"
mkdir -p "$busy/usr/bin"
cp "$(command -v sleep)" "$busy/usr/bin/hold"
chmod 755 "$busy/usr/bin/hold"
"$busy/usr/bin/hold" 40 &
bpid=$!
bash -c "echo $lib >/dev/null; sleep 40" &
decoy=$!
sleep 0.2
[[ $(readlink "/proc/$bpid/exe") == "$busy/usr/bin/hold" ]]
HOME="$home" PREFIX="$home/.local" bash "$INSTALL" "$app2"
[[ ! -e $lib/.staging-1.0.0-8 ]]
[[ ! -e $lib/.previous-1.0.0-9 ]]
[[ -d $busy ]]
kill "$bpid" "$decoy"
wait "$bpid" "$decoy" 2>/dev/null || true
HOME="$home" PREFIX="$home/.local" bash "$INSTALL" "$app2"
[[ ! -d $busy ]]

# Uninstall must not delete a version that still has a running executable.
bhome="$tmp/busyhome"
mkdir -p "$bhome"
HOME="$bhome" PREFIX="$bhome/.local" bash "$INSTALL" "$app1"
root="$bhome/.local/lib/inco-browser/1.0.1"
chmod -R u+w "$root"
cp "$(command -v sleep)" "$root/usr/bin/hold"
chmod 755 "$root/usr/bin/hold"
"$root/usr/bin/hold" 40 &
hpid=$!
sleep 0.2
if HOME="$bhome" PREFIX="$bhome/.local" bash "$INSTALL" --uninstall; then
  echo "uninstall removed a tree that is still running" >&2
  exit 1
fi
[[ -d $root ]]
[[ -f $bhome/.local/share/applications/inco-browser.desktop ]]
[[ -f $bhome/.local/bin/inco-browser ]]
kill "$hpid"
wait "$hpid" 2>/dev/null || true
HOME="$bhome" PREFIX="$bhome/.local" bash "$INSTALL" --uninstall
[[ ! -d $root ]]
[[ ! -e $bhome/.local/bin/inco-browser ]]

echo "install edge checks passed"
