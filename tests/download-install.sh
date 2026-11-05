#!/usr/bin/env bash
# The Install file downloads a release asset and refuses to install it
# until the published checksum matches. These checks use a local server.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
tmp=$(mktemp -d)
srv=""
trap 'chmod -R u+w "$tmp" 2>/dev/null || true; rm -rf "$tmp"; [[ -n $srv ]] && kill "$srv" 2>/dev/null || true' EXIT

asset="Inco_Browser-1.0.1-x86_64.AppImage"
good=$tmp/good
bad=$tmp/bad
mkdir -p "$good" "$bad"
printf 'not an appimage\n' > "$good/$asset"
( cd "$good" && sha256sum "$asset" > SHA256SUMS )
cp "$good/$asset" "$bad/$asset"
printf '0000000000000000000000000000000000000000000000000000000000000000  %s\n' "$asset" > "$bad/SHA256SUMS"

serve() {
  local root=$1
  [[ -n $srv ]] && kill "$srv" 2>/dev/null || true
  [[ -n $srv ]] && wait "$srv" 2>/dev/null || true
  python3 - "$root" "$port" <<'PY' &
import os, sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
root, port = sys.argv[1], int(sys.argv[2])
class H(BaseHTTPRequestHandler):
    def do_GET(self):
        rel = self.path.split("?", 1)[0].lstrip("/")
        path = os.path.normpath(os.path.join(root, rel))
        if not path.startswith(os.path.abspath(root) + os.sep) and path != os.path.abspath(root):
            self.send_response(404)
            self.end_headers()
            return
        if not os.path.isfile(path):
            self.send_response(404)
            self.end_headers()
            self.wfile.write(b"missing\n")
            return
        data = open(path, "rb").read()
        self.send_response(200)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)
    def log_message(self, fmt, *args):
        return
ThreadingHTTPServer.allow_reuse_address = True
ThreadingHTTPServer(("127.0.0.1", port), H).serve_forever()
PY
  srv=$!
  local i
  for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
    curl -sf -o /dev/null "http://127.0.0.1:$port/SHA256SUMS" && return 0
    sleep 0.05
  done
  echo "test server did not start" >&2
  exit 1
}

port=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1]); s.close()')
serve "$good"

installer=$tmp/Inco_Browser-1.0.1-x86_64.install
"$ROOT/scripts/emit-downloader.sh" 1.0.1 "$installer" >/dev/null
[[ $(wc -c < "$installer") -lt 100000 ]]
grep -q 'releases/download/v' "$installer"
! grep -q '__INCO_PAYLOAD__' "$installer"

run_install() {
  local home=$1 base=$2
  mkdir -p "$home"
  HOME="$home" PREFIX="$home/.local" INCO_UI=text INCO_RELEASE_BASE="$base" \
    bash "$installer" >"$home/out" 2>"$home/err" || return $?
}

if run_install "$tmp/net" "http://127.0.0.1:1"; then
  echo "network failure was treated as success" >&2
  exit 1
fi
grep -q 'Could not reach GitHub' "$tmp/net/err"
grep -q 'Nothing was installed' "$tmp/net/err"
[[ ! -d $tmp/net/.local/lib/inco-browser ]]

if run_install "$tmp/missing" "http://127.0.0.1:$port/no-such-release"; then
  echo "missing asset was installed" >&2
  exit 1
fi
grep -q 'HTTP 404' "$tmp/missing/err"
grep -q 'Nothing was installed' "$tmp/missing/err"
[[ ! -d $tmp/missing/.local/lib/inco-browser ]]

serve "$bad"
if run_install "$tmp/badhome" "http://127.0.0.1:$port"; then
  echo "bad checksum was installed" >&2
  exit 1
fi
grep -q 'does not match the published SHA256SUMS' "$tmp/badhome/err"
grep -q 'Nothing was installed' "$tmp/badhome/err"
[[ ! -d $tmp/badhome/.local/lib/inco-browser ]]

serve "$good"
if run_install "$tmp/unpack" "http://127.0.0.1:$port"; then
  echo "non-appimage was installed" >&2
  exit 1
fi
grep -q 'did not unpack' "$tmp/unpack/err"
grep -q 'Nothing was installed' "$tmp/unpack/err"
[[ ! -d $tmp/unpack/.local/lib/inco-browser ]]

# --uninstall does not contact the release. It only removes a menu install.
home=$tmp/menu
mkdir -p "$home"
app=$tmp/IncoBrowser.AppDir
mkdir -p "$app/usr/bin/tor" "$app/usr/share/icons/hicolor/256x256/apps" "$app/apprun-hooks"
cat > "$app/usr/bin/IncoBrowser" <<'EOF'
#!/bin/sh
if [ "$1" = "--verify-tor" ]; then echo ok; exit 0; fi
if [ "$INCO_HOOK_OK" != 1 ]; then echo "hook did not run" >&2; exit 1; fi
echo "Inco Browser 1.0.1"
exit 0
EOF
chmod 755 "$app/usr/bin/IncoBrowser"
printf 'tor\n' > "$app/usr/bin/tor/tor"
printf 'icon\n' > "$app/usr/share/icons/hicolor/256x256/apps/inco-browser.png"
printf 'INCO_HOOK_OK=1\nexport INCO_HOOK_OK\n' > "$app/apprun-hooks/linuxdeploy-plugin-qt-hook.sh"
cat > "$app/AppRun" <<'EOF'
#!/bin/sh
here=$(dirname "$0")
. "$here/apprun-hooks/linuxdeploy-plugin-qt-hook.sh"
exec "$here/usr/bin/IncoBrowser" "$@"
EOF
chmod 755 "$app/AppRun"
printf '%s\n' '[Desktop Entry]' 'X-AppImage-Version=1.0.1' > "$app/inco-browser.desktop"
HOME="$home" PREFIX="$home/.local" bash "$ROOT/scripts/install-appdir.sh" "$app"
HOME="$home" PREFIX="$home/.local" INCO_UI=text INCO_RELEASE_BASE="http://127.0.0.1:1" \
  bash "$installer" --uninstall
[[ ! -d $home/.local/lib/inco-browser/1.0.1 ]]

echo "download install checks passed"
