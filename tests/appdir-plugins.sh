#!/usr/bin/env bash
# Fixture checks for scripts/check-appdir.sh. No Qt install required.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CHECK="$ROOT/scripts/check-appdir.sh"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

cc_so() {
  # cc_so <output> [extra gcc args...]  and the source is stdin.
  # The source file has to come before -l flags or the linker drops them.
  local out=$1
  shift
  gcc -shared -fPIC -x c -o "$out" - "$@"
}

pass_dir="$tmp/pass"
mkdir -p \
  "$pass_dir/usr/plugins/platforms" \
  "$pass_dir/usr/plugins/wayland-shell-integration" \
  "$pass_dir/usr/plugins/wayland-decoration-client" \
  "$pass_dir/usr/plugins/wayland-graphics-integration-client" \
  "$pass_dir/usr/plugins/xcbglintegrations" \
  "$pass_dir/usr/lib" \
  "$tmp/link"

echo 'int dec(void){return 0;}' | cc_so "$pass_dir/usr/plugins/wayland-decoration-client/libbradient.so"
echo 'int shell(void){return 0;}' | cc_so "$pass_dir/usr/plugins/wayland-shell-integration/libxdg-shell.so"
echo 'int wayland(void){return 0;}' | cc_so "$pass_dir/usr/plugins/platforms/libqwayland.so"
echo 'int xcb(void){return 0;}' | cc_so "$pass_dir/usr/plugins/platforms/libqxcb.so"
echo 'int off(void){return 0;}' | cc_so "$pass_dir/usr/plugins/platforms/libqoffscreen.so"
echo 'int xegl(void){return 0;}' | cc_so "$pass_dir/usr/plugins/xcbglintegrations/libqxcb-egl-integration.so"
echo 'int xglx(void){return 0;}' | cc_so "$pass_dir/usr/plugins/xcbglintegrations/libqxcb-glx-integration.so"
echo 'int client(void){return 0;}' | cc_so "$pass_dir/usr/lib/libQt6WaylandClient.so.6" -Wl,-soname,libQt6WaylandClient.so.6
echo 'int eglhw(void){return 1;}' | cc_so "$tmp/link/libQt6WaylandEglClientHwIntegration.so.6" \
  -Wl,-soname,libQt6WaylandEglClientHwIntegration.so.6
echo 'int wlegl(void){return 2;}' | cc_so "$tmp/link/libwayland-egl.so.1" -Wl,-soname,libwayland-egl.so.1
echo 'extern int eglhw(void); extern int wlegl(void); int plug(void){return eglhw()+wlegl();}' \
  | cc_so "$pass_dir/usr/plugins/wayland-graphics-integration-client/libqt-plugin-wayland-egl.so" \
      -L"$tmp/link" -Wl,--no-as-needed \
      -l:libQt6WaylandEglClientHwIntegration.so.6 -l:libwayland-egl.so.1

# Dependency is not bundled yet: the checker must fail.
if "$CHECK" "$pass_dir"; then
  echo "expected failure while Qt Wayland EGL libs are missing" >&2
  exit 1
fi

cp -a "$tmp/link/libQt6WaylandEglClientHwIntegration.so.6" "$pass_dir/usr/lib/"
cp -a "$tmp/link/libwayland-egl.so.1" "$pass_dir/usr/lib/"
"$CHECK" "$pass_dir"

# The 1.0.0 failure mode: everything except the graphics integration plugin.
rm -rf "$pass_dir/usr/plugins/wayland-graphics-integration-client"
if "$CHECK" "$pass_dir"; then
  echo "expected failure without wayland-egl plugin" >&2
  exit 1
fi

# Restored plugin, but a bundled libEGL must be rejected.
mkdir -p "$pass_dir/usr/plugins/wayland-graphics-integration-client"
cp -a "$tmp/link/libQt6WaylandEglClientHwIntegration.so.6" \
  "$pass_dir/usr/plugins/wayland-graphics-integration-client/libqt-plugin-wayland-egl.so"
# That copy does not NEEDED libwayland-egl. Put the real plugin back.
echo 'extern int eglhw(void); extern int wlegl(void); int plug(void){return eglhw()+wlegl();}' \
  | cc_so "$pass_dir/usr/plugins/wayland-graphics-integration-client/libqt-plugin-wayland-egl.so" \
      -L"$tmp/link" -Wl,--no-as-needed \
      -l:libQt6WaylandEglClientHwIntegration.so.6 -l:libwayland-egl.so.1
echo 'not a driver' > "$pass_dir/usr/lib/libEGL.so.1"
if "$CHECK" "$pass_dir"; then
  echo "expected failure when libEGL.so.1 is bundled" >&2
  exit 1
fi

if [[ -n ${INCO_RELEASE_APPDIR:-} ]]; then
  if "$CHECK" "$INCO_RELEASE_APPDIR"; then
    echo "released AppDir was expected to fail the Wayland check" >&2
    exit 1
  fi
  echo "released AppDir fails the check, as expected"
fi

echo "appdir plugin checks passed"
