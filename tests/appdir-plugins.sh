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

# Host libraries Qt Gui/Core link and that AppImages leave on the system.
mkdir -p "$tmp/host"
echo 'int fc(void){return 1;}' | cc_so "$tmp/host/libfontconfig.so.1" -Wl,-soname,libfontconfig.so.1
echo 'int x11(void){return 1;}' | cc_so "$tmp/host/libX11.so.6" -Wl,-soname,libX11.so.6
echo 'int hb(void){return 1;}' | cc_so "$tmp/host/libharfbuzz.so.0" -Wl,-soname,libharfbuzz.so.0
echo 'int ft(void){return 1;}' | cc_so "$tmp/host/libfreetype.so.6" -Wl,-soname,libfreetype.so.6
echo 'int zed(void){return 1;}' | cc_so "$tmp/host/libz.so.1" -Wl,-soname,libz.so.1
echo 'extern int zed(void); int core(void){return zed();}' \
  | cc_so "$tmp/link/libQt6Core.so.6" -Wl,-soname,libQt6Core.so.6 \
      -L"$tmp/host" -Wl,--no-as-needed -l:libz.so.1
echo 'extern int fc(void); extern int x11(void); extern int hb(void); extern int ft(void); extern int zed(void);
      int gui(void){return fc()+x11()+hb()+ft()+zed();}' \
  | cc_so "$tmp/link/libQt6Gui.so.6" -Wl,-soname,libQt6Gui.so.6 \
      -L"$tmp/host" -Wl,--no-as-needed \
      -l:libfontconfig.so.1 -l:libX11.so.6 -l:libharfbuzz.so.0 -l:libfreetype.so.6 -l:libz.so.1
echo 'int wlegl(void){return 2;}' | cc_so "$tmp/link/libwayland-egl.so.1" -Wl,-soname,libwayland-egl.so.1
echo 'extern int gui(void); extern int core(void); extern int wlegl(void);
      int eglhw(void){return gui()+core()+wlegl();}' \
  | cc_so "$tmp/link/libQt6WaylandEglClientHwIntegration.so.6" \
      -Wl,-soname,libQt6WaylandEglClientHwIntegration.so.6 \
      -L"$tmp/link" -Wl,--no-as-needed \
      -l:libQt6Gui.so.6 -l:libQt6Core.so.6 -l:libwayland-egl.so.1
echo 'extern int eglhw(void); int plug(void){return eglhw();}' \
  | cc_so "$pass_dir/usr/plugins/wayland-graphics-integration-client/libqt-plugin-wayland-egl.so" \
      -L"$tmp/link" -Wl,--no-as-needed \
      -l:libQt6WaylandEglClientHwIntegration.so.6

# Dependency is not bundled yet: the checker must fail.
if "$CHECK" "$pass_dir"; then
  echo "expected failure while Qt Wayland EGL libs are missing" >&2
  exit 1
fi

cp -a "$tmp/link/libQt6WaylandEglClientHwIntegration.so.6" "$pass_dir/usr/lib/"
cp -a "$tmp/link/libwayland-egl.so.1" "$pass_dir/usr/lib/"
cp -a "$tmp/link/libQt6Gui.so.6" "$tmp/link/libQt6Core.so.6" "$pass_dir/usr/lib/"
# fontconfig, X11, harfbuzz, freetype and zlib stay out of the AppDir.
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
