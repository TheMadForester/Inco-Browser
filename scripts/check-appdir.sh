#!/usr/bin/env bash
# Check an AppDir for the Qt plugins a Fedora KDE Wayland session needs.
# The 1.0.0 AppImage had libqwayland.so, xdg-shell and the xcb GL plugins,
# and did not have wayland-graphics-integration-client. Qt then logged
# "Failed to load client buffer integration: wayland-egl" and the window
# never painted. libEGL and libwayland-client must stay on the host:
# bundling them breaks the Mesa driver.
set -euo pipefail

APPDIR=${1:-}
if [[ -z $APPDIR || ! -d $APPDIR ]]; then
  echo "usage: check-appdir.sh <AppDir>" >&2
  exit 2
fi

errors=0
note() { printf 'check-appdir: %s\n' "$*"; }
bad() { printf 'check-appdir: %s\n' "$*" >&2; errors=$((errors + 1)); }

is_elf() {
  local f=$1
  [[ -f $f ]] || return 1
  local magic
  magic=$(od -An -t x1 -N 4 "$f" | tr -d ' \n')
  [[ $magic == 7f454c46 ]]
}

require_elf() {
  local rel=$1
  if is_elf "$APPDIR/$rel"; then
    note "found $rel"
  else
    bad "missing ELF $rel"
  fi
}

require_elf usr/plugins/platforms/libqwayland.so
require_elf usr/plugins/platforms/libqxcb.so
require_elf usr/plugins/platforms/libqoffscreen.so
require_elf usr/plugins/wayland-shell-integration/libxdg-shell.so
require_elf usr/plugins/xcbglintegrations/libqxcb-egl-integration.so
require_elf usr/plugins/xcbglintegrations/libqxcb-glx-integration.so
require_elf usr/plugins/wayland-graphics-integration-client/libqt-plugin-wayland-egl.so
require_elf usr/lib/libQt6WaylandClient.so.6

decor=$(find "$APPDIR/usr/plugins/wayland-decoration-client" -name '*.so' -type f 2>/dev/null | head -1 || true)
if [[ -n $decor ]]; then
  note "found wayland decoration plugin $(basename "$decor")"
else
  bad "missing wayland-decoration-client plugin"
fi

# Graphics and Wayland client libraries that must come from the host.
# A copy inside the AppDir is loaded first and disagrees with Mesa.
for banned in libEGL.so.1 libGL.so.1 libOpenGL.so.0 libGLdispatch.so.0 libwayland-client.so.0; do
  if [[ -e $APPDIR/usr/lib/$banned ]]; then
    bad "$banned is bundled; leave it on the host so the GPU driver matches"
  fi
done

# Follow Qt/Wayland NEEDED edges of the EGL plugin. Host libraries above
# are allowed to be absent. Anything else Qt-related must be in usr/lib.
host_lib() {
  case $1 in
    libc.so.6|libm.so.6|libdl.so.2|libpthread.so.0|librt.so.1|ld-linux*.so.2)
      return 0 ;;
    libgcc_s.so.1|libstdc++.so.6)
      return 0 ;;
    libEGL.so.1|libGL.so.1|libGLESv2.so.2|libOpenGL.so.0|libGLdispatch.so.0|libGLX.so.0)
      return 0 ;;
    libwayland-client.so.0|libdrm.so.2|libgbm.so.1)
      return 0 ;;
    *) return 1 ;;
  esac
}

declare -A seen=()
queue=()
enqueue() {
  local rel=$1
  [[ -n ${seen[$rel]:-} ]] && return 0
  seen[$rel]=1
  queue+=("$rel")
}

egl_rel=usr/plugins/wayland-graphics-integration-client/libqt-plugin-wayland-egl.so
if is_elf "$APPDIR/$egl_rel"; then
  enqueue "$egl_rel"
fi

while [[ ${#queue[@]} -gt 0 ]]; do
  rel=${queue[0]}
  if [[ ${#queue[@]} -eq 1 ]]; then
    queue=()
  else
    queue=("${queue[@]:1}")
  fi
  so=$APPDIR/$rel
  [[ -f $so ]] || continue
  while IFS= read -r soname; do
    [[ -n $soname ]] || continue
    if host_lib "$soname"; then
      note "NEEDED $soname of $(basename "$rel"): host"
      continue
    fi
    bundled=usr/lib/$soname
    if is_elf "$APPDIR/$bundled"; then
      note "NEEDED $soname of $(basename "$rel"): bundled"
      case $soname in
        libQt6*|libwayland-egl.so.*)
          enqueue "$bundled" ;;
      esac
    else
      bad "NEEDED $soname of $(basename "$rel") is not in usr/lib"
    fi
  done < <(readelf -d "$so" 2>/dev/null | sed -n 's/.*Shared library: \[\([^]]*\)\].*/\1/p')
done

if [[ $errors -gt 0 ]]; then
  bad "$errors problem(s)"
  exit 1
fi
note "ok"
exit 0
