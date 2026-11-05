#!/usr/bin/env bash
# Install an extracted Inco AppDir into the current user's account.
# No root, no autostart, no background process, no network.
#
#   scripts/install-appdir.sh /path/to/IncoBrowser.AppDir
#   scripts/install-appdir.sh --uninstall
#
# scripts/pack-installer.sh appends the AppDir to a copy of this file.
# Run that copy with bash; it does not need chmod.
#
# Writes only under $PREFIX (default $HOME/.local):
#   lib/inco-browser/<version>/     the AppDir, then made read-only
#   lib/inco-browser/current        symlink to <version>
#   bin/inco-browser                symlink to the launcher
#   share/applications/inco-browser.desktop
#   share/icons/hicolor/256x256/apps/inco-browser.png
# update-desktop-database, when it exists, refreshes mimeinfo.cache
# in that applications directory and does not edit other desktop files.
set -euo pipefail

PREFIX="${PREFIX:-$HOME/.local}"
stage=""
work=""

die() { printf 'install: %s\n' "$*" >&2; exit 1; }

cleanup() {
  if [[ -n $stage && -d $stage ]]; then
    chmod -R u+w "$stage" 2>/dev/null || true
    rm -rf "$stage"
  fi
  if [[ -n $work && -d $work ]]; then
    rm -rf "$work"
  fi
}
trap cleanup EXIT

usage() {
  cat <<'EOF'
Inco Browser user install.

  bash Inco_Browser-<version>-x86_64.install
  bash Inco_Browser-<version>-x86_64.install --uninstall

Puts Inco in the application menu for this account. Nothing is started
in the background, and nothing is set to start at login. Launching the
installed app does not need chmod.

The portable AppImage is unchanged. Installing only unpacks the same
files into your home directory so later launches skip the AppImage
mount. Chromium still starts its own processes; this does not make
pages open instantly.
EOF
}

# PREFIX must stay inside the home directory. The version we delete is
# only the directory named by a dotted number, never a path we were handed.
check_prefix() {
  [[ $(id -u) -eq 0 ]] && die "this installer is for your own account and does not run as root"
  [[ -n ${HOME:-} ]] || die "HOME is not set"
  case $PREFIX in
    "$HOME"/*) ;;
    *) die "PREFIX must be inside your home directory (got $PREFIX)" ;;
  esac
  case $PREFIX in
    *$'\n'*|*'..'*) die "PREFIX is not a usable path" ;;
  esac
}

payload_line() {
  awk '
    /^__INCO_PAYLOAD__$/ { print NR + 1; found = 1; exit }
    END { if (!found) exit 1 }
  ' "$1"
}

read_version() {
  local appdir=$1 f ver
  for f in "$appdir/inco-browser.desktop" "$appdir/usr/share/applications/inco-browser.desktop"; do
    [[ -f $f ]] || continue
    ver=$(sed -n 's/^X-AppImage-Version=//p' "$f" | head -1)
    ver=${ver//$'\r'/}
    printf '%s' "$ver"
    return 0
  done
  return 1
}

valid_version() {
  [[ $1 =~ ^[0-9]+(\.[0-9]+){1,3}$ ]]
}

escape_desktop() {
  # Desktop Exec and Icon values treat space as a separator.
  local s=$1
  s=${s//\\/\\\\}
  s=${s// /\\ }
  printf '%s' "$s"
}

install_tree() {
  local appdir=$1
  [[ -d $appdir ]] || die "no AppDir at $appdir"
  local ver
  ver=$(read_version "$appdir") || die "AppDir has no X-AppImage-Version"
  valid_version "$ver" || die "refusing version '$ver'"

  local bin=$appdir/usr/bin/IncoBrowser
  [[ -e $bin ]] || die "AppDir has no usr/bin/IncoBrowser"
  [[ -d $appdir/usr/bin/tor ]] || die "AppDir has no usr/bin/tor (integrity check would fail)"

  local libroot=$PREFIX/lib/inco-browser
  local dest=$libroot/$ver
  stage=$libroot/.staging-$ver-$$
  mkdir -p "$libroot" "$PREFIX/bin" \
           "$PREFIX/share/applications" \
           "$PREFIX/share/icons/hicolor/256x256/apps"

  rm -rf "$stage"
  mkdir -p "$stage"

  cp -a "$appdir"/. "$stage"/
  chmod a+x "$stage/usr/bin/IncoBrowser"
  if [[ -e $stage/AppRun ]]; then
    chmod a+x "$stage/AppRun" 2>/dev/null || true
  fi
  if [[ -f $stage/usr/bin/tor/tor ]]; then
    chmod a+x "$stage/usr/bin/tor/tor"
  fi
  printf 'version=%s\nkind=user\n' "$ver" > "$stage/inco-install.txt"

  local launch_rel=AppRun
  if [[ ! -x $stage/AppRun ]]; then
    launch_rel=usr/bin/IncoBrowser
  fi
  [[ -x $stage/$launch_rel ]] || die "launcher is not executable"

  echo "install: checking staged Tor bundle"
  local vlog
  vlog=$(mktemp)
  if ! env -u INCO_TOR_DIR "$stage/usr/bin/IncoBrowser" --verify-tor >"$vlog" 2>&1; then
    cat "$vlog" >&2 || true
    rm -f "$vlog"
    die "integrity check of staged tree failed; nothing installed"
  fi
  rm -f "$vlog"

  if [[ -e $dest ]]; then
    chmod -R u+w "$dest"
    rm -rf "$dest"
  fi
  mv "$stage" "$dest"
  stage=""
  chmod -R a-w "$dest"
  if [[ -e $libroot/current && ! -L $libroot/current ]]; then
    die "refusing to replace $libroot/current because it is not a symlink"
  fi
  ln -sfn "$ver" "$libroot/current"

  local launch=$dest/$launch_rel
  [[ -x $launch ]] || die "installed launcher is not executable"

  local binlink=$PREFIX/bin/inco-browser
  if [[ -e $binlink && ! -L $binlink ]]; then
    echo "install: left $binlink alone (it is not a symlink)"
  else
    ln -sfn "../lib/inco-browser/current/AppRun" "$binlink"
    if [[ ! -x $dest/AppRun ]]; then
      ln -sfn "../lib/inco-browser/current/usr/bin/IncoBrowser" "$binlink"
    fi
  fi

  local icon_src=$dest/usr/share/icons/hicolor/256x256/apps/inco-browser.png
  [[ -f $icon_src ]] || icon_src=$dest/inco-browser.png
  local icon_dest=$PREFIX/share/icons/hicolor/256x256/apps/inco-browser.png
  if [[ -f $icon_src ]]; then
    # cp keeps the source mode, and the tree was just made read-only.
    # The menu icon is the user's file, so it stays writable.
    cp -f "$icon_src" "$icon_dest"
    chmod 644 "$icon_dest"
  fi

  local desktop=$PREFIX/share/applications/inco-browser.desktop
  local exec_field icon_field
  exec_field=$(escape_desktop "$launch")
  icon_field=$icon_dest
  if [[ -f $icon_dest ]]; then
    icon_field=$(escape_desktop "$icon_dest")
  else
    icon_field=inco-browser
  fi
  cat > "$desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Inco Browser
Comment=Private-by-default web browser
Exec=$exec_field %U
Icon=$icon_field
Terminal=false
Categories=Network;WebBrowser;
MimeType=text/html;x-scheme-handler/http;x-scheme-handler/https;
StartupWMClass=IncoBrowser
X-Inco-Install=user
EOF

  if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$PREFIX/share/applications" >/dev/null 2>&1 || true
  fi

  echo "Installed Inco Browser $ver"
  echo "  $dest (read-only)"
  echo "  menu name: Inco Browser"
  echo "  $binlink"
  echo "Open it from the application menu. It is already executable."
  local others
  others=$(find "$libroot" -mindepth 1 -maxdepth 1 -type d ! -name "$ver" ! -name '.staging-*' -printf '%f ' 2>/dev/null || true)
  [[ -n $others ]] && echo "  other versions kept: $others"
}

uninstall_tree() {
  local libroot=$PREFIX/lib/inco-browser
  [[ -L $libroot/current ]] || die "no user install at $libroot/current"
  local ver
  ver=$(readlink "$libroot/current")
  valid_version "$ver" || die "current does not point at a version ($ver)"
  local dest=$libroot/$ver
  [[ -d $dest ]] || die "missing $dest"
  [[ -f $dest/inco-install.txt ]] || die "$dest is not a menu install; leaving it in place"
  grep -q '^kind=user$' "$dest/inco-install.txt" || die "$dest is not a menu install; leaving it in place"

  local desktop=$PREFIX/share/applications/inco-browser.desktop
  if [[ -f $desktop ]] && grep -q '^X-Inco-Install=user$' "$desktop" \
      && grep -q 'lib/inco-browser/' "$desktop"; then
    rm -f "$desktop"
  fi

  local icon_dest=$PREFIX/share/icons/hicolor/256x256/apps/inco-browser.png
  local icon_src=$dest/usr/share/icons/hicolor/256x256/apps/inco-browser.png
  [[ -f $icon_src ]] || icon_src=$dest/inco-browser.png
  if [[ -f $icon_dest && -f $icon_src ]] && cmp -s "$icon_dest" "$icon_src"; then
    rm -f "$icon_dest"
  fi

  local binlink=$PREFIX/bin/inco-browser
  if [[ -L $binlink ]]; then
    local target
    target=$(readlink "$binlink")
    case $target in
      *lib/inco-browser/*) rm -f "$binlink" ;;
    esac
  fi

  chmod -R u+w "$dest"
  rm -rf "$dest"
  if [[ -L $libroot/current ]] && [[ $(readlink "$libroot/current") == "$ver" ]]; then
    rm -f "$libroot/current"
  fi

  if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$PREFIX/share/applications" >/dev/null 2>&1 || true
  fi
  echo "Removed Inco Browser $ver"
}

main() {
  local cmd=${1:-}
  case $cmd in
    -h|--help) usage; exit 0 ;;
  esac
  check_prefix
  if [[ $cmd == --uninstall ]]; then
    uninstall_tree
    exit 0
  fi

  local appdir=$cmd
  if pl=$(payload_line "$0"); then
    work=$(mktemp -d)
    tail -n +"$pl" "$0" | tar -xz -C "$work"
    appdir=$(find "$work" -mindepth 1 -maxdepth 1 -type d | head -1)
    [[ -n $appdir ]] || die "installer payload did not contain an AppDir"
  fi
  [[ -n $appdir ]] || die "usage: install-appdir.sh <AppDir> | --uninstall"
  install_tree "$appdir"
}

main "$@"
exit $?
