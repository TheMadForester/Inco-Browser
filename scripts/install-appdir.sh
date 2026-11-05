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
#   bin/inco-browser                launcher script (not a symlink)
#   share/applications/inco-browser.desktop
#   share/icons/hicolor/256x256/apps/inco-browser.png
# update-desktop-database, when it exists, refreshes mimeinfo.cache
# in that applications directory and does not edit other desktop files.
set -euo pipefail

PREFIX="${PREFIX:-$HOME/.local}"
stage=""
work=""
previous=""
live_dest=""
kept_busy=0

die() { printf 'install: %s\n' "$*" >&2; exit 1; }

cleanup() {
  if [[ -n $stage && -d $stage ]]; then
    chmod -R u+w "$stage" 2>/dev/null || true
    rm -rf "$stage"
  fi
  # The live tree was renamed aside and the new one never replaced it.
  if [[ -n $previous && -d $previous && -n $live_dest && ! -e $live_dest ]]; then
    mv "$previous" "$live_dest" || true
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

# One Exec argument, quoted per the Desktop Entry spec.
# The file is unescaped first (\n, \s, \\), then parsed as Exec.
# So a backslash the Exec parser must see is written twice.
# ", `, $ and \ need that. A literal % is %%. Spaces stay spaces.
exec_arg() {
  local s=$1 i c out=""
  local -i n=${#s}
  for ((i = 0; i < n; i++)); do
    c=${s:i:1}
    case $c in
      \\) out+='\\\\' ;;
      \"|\`|\$) out+="\\\\$c" ;;
      %) out+='%%' ;;
      *) out+=$c ;;
    esac
  done
  printf '"%s"' "$out"
}

# True when /proc says some process was exec'd from a file inside $1.
# Only the kernel's executable path counts, not the command line.
dir_in_use() {
  local root=$1 piddir exe
  [[ -d $root ]] || return 1
  root=$(cd "$root" && pwd -P) || return 1
  for piddir in /proc/[0-9]*; do
    [[ -e $piddir/exe ]] || continue
    exe=$(readlink "$piddir/exe" 2>/dev/null) || continue
    exe=${exe%' (deleted)'}
    [[ $exe == "$root" || $exe == "$root"/* ]] && return 0
  done
  return 1
}

# $2 is "fail" when skipping should make uninstall report failure.
remove_tree() {
  local d=$1 mark=${2:-}
  if dir_in_use "$d"; then
    echo "install: leaving $d (a program from there is still running)"
    [[ $mark == fail ]] && kept_busy=1
    return 1
  fi
  chmod -R u+w "$d" 2>/dev/null || true
  rm -rf -- "$d"
}

# Half-finished installs from this script only. Never a version directory.
reclaim_side_dirs() {
  local libroot=$1 d base
  [[ -d $libroot ]] || return 0
  while IFS= read -r d; do
    base=$(basename "$d")
    case $base in
      .previous-*|.staging-*) ;;
      *) continue ;;
    esac
    remove_tree "$d" || true
  done < <(find "$libroot" -mindepth 1 -maxdepth 1 -type d)
}

quote_sh() {
  local s=$1
  s=${s//\'/\'\\\'\'}
  printf "'%s'" "$s"
}

# Menu installs carry inco-install.txt. Source builds do not.
drop_stamped() {
  local keep=${1:-} libroot=$PREFIX/lib/inco-browser d name
  [[ -d $libroot ]] || return 0
  while IFS= read -r d; do
    name=$(basename "$d")
    [[ -n $keep && $name == "$keep" ]] && continue
    valid_version "$name" || continue
    [[ -f $d/inco-install.txt ]] || continue
    grep -q '^kind=user$' "$d/inco-install.txt" || continue
    remove_tree "$d" fail || true
  done < <(find "$libroot" -mindepth 1 -maxdepth 1 -type d)
}

write_launcher() {
  local binlink=$1 target=$2 q
  if [[ -L $binlink ]]; then
    case $(readlink "$binlink") in
      *lib/inco-browser/*) rm -f "$binlink" ;;
      *) echo "install: left $binlink alone"; return 0 ;;
    esac
  elif [[ -e $binlink ]] && ! grep -q '^# inco-install: user$' "$binlink"; then
    echo "install: left $binlink alone"
    return 0
  fi
  rm -f "$binlink"
  q=$(quote_sh "$target")
  cat > "$binlink" <<EOF
#!/bin/sh
# inco-install: user
exec $q "\$@"
EOF
  chmod 755 "$binlink"
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
  if [[ -e $libroot/current && ! -L $libroot/current ]]; then
    die "refusing to replace $libroot/current because it is not a symlink"
  fi
  live_dest=$dest
  previous=""
  stage=$libroot/.staging-$ver-$$
  mkdir -p "$libroot" "$PREFIX/bin" \
           "$PREFIX/share/applications" \
           "$PREFIX/share/icons/hicolor/256x256/apps"
  reclaim_side_dirs "$libroot"

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

  # Rename the live tree aside, then rename the staged tree into its place.
  # Both are on the same directory, so each mv is a rename. The old tree
  # is removed only after the new one is in place. cleanup puts the old
  # tree back if we exit before that.
  if [[ -e $dest ]]; then
    previous=$libroot/.previous-$ver-$$
    mv "$dest" "$previous"
  fi
  if [[ -n $previous && ${INCO_INSTALL_ABORT:-} == after-rename ]]; then
    die "aborted before publishing the new tree"
  fi
  if ! mv "$stage" "$dest"; then
    die "could not move the new tree into place"
  fi
  stage=""
  if [[ -n $previous ]]; then
    local old=$previous
    previous=""
    # The running program's executable now lives in this renamed tree.
    remove_tree "$old" || true
  fi
  chmod -R a-w "$dest"
  ln -sfn "$ver" "$libroot/current"
  drop_stamped "$ver"

  local launch=$libroot/current/$launch_rel
  [[ -x $launch ]] || die "installed launcher is not executable"

  # AppRun finds apprun-hooks with dirname "$0". A symlink in bin/ would
  # make that directory ~/.local/bin. A script execs the real path instead.
  write_launcher "$PREFIX/bin/inco-browser" "$launch"

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
  exec_field=$(exec_arg "$launch")
  if [[ -f $icon_dest ]]; then
    icon_field=$icon_dest
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
  echo "  $PREFIX/bin/inco-browser"
  echo "Open it from the application menu. It is already executable."
  local others
  others=$(find "$libroot" -mindepth 1 -maxdepth 1 -type d \
    ! -name "$ver" ! -name '.staging-*' ! -name '.previous-*' -printf '%f ' 2>/dev/null || true)
  if [[ -n $others ]]; then
    echo "  other versions kept: $others"
  fi
}

uninstall_tree() {
  local libroot=$PREFIX/lib/inco-browser
  kept_busy=0
  reclaim_side_dirs "$libroot"
  local names=() d name
  if [[ -d $libroot ]]; then
    while IFS= read -r d; do
      name=$(basename "$d")
      valid_version "$name" || continue
      [[ -f $d/inco-install.txt ]] || continue
      grep -q '^kind=user$' "$d/inco-install.txt" || continue
      names+=("$name")
    done < <(find "$libroot" -mindepth 1 -maxdepth 1 -type d | sort)
  fi
  [[ ${#names[@]} -gt 0 ]] || die "no menu install to remove"

  local busy_any=0
  for name in "${names[@]}"; do
    if dir_in_use "$libroot/$name"; then
      busy_any=1
    fi
  done

  # Leave the menu entry while a copy we installed is still running.
  if [[ $busy_any -eq 0 ]]; then
    local desktop=$PREFIX/share/applications/inco-browser.desktop
    if [[ -f $desktop ]] && grep -q '^X-Inco-Install=user$' "$desktop" \
        && grep -q 'lib/inco-browser/' "$desktop"; then
      rm -f "$desktop"
    fi

    local icon_dest=$PREFIX/share/icons/hicolor/256x256/apps/inco-browser.png
    local icon_src
    for name in "${names[@]}"; do
      icon_src=$libroot/$name/usr/share/icons/hicolor/256x256/apps/inco-browser.png
      [[ -f $icon_src ]] || icon_src=$libroot/$name/inco-browser.png
      if [[ -f $icon_dest && -f $icon_src ]] && cmp -s "$icon_dest" "$icon_src"; then
        rm -f "$icon_dest"
        break
      fi
    done

    local binlink=$PREFIX/bin/inco-browser
    if [[ -L $binlink ]]; then
      case $(readlink "$binlink") in
        *lib/inco-browser/*) rm -f "$binlink" ;;
      esac
    elif [[ -f $binlink ]] && grep -q '^# inco-install: user$' "$binlink"; then
      rm -f "$binlink"
    fi
  fi

  drop_stamped ""
  if [[ -L $libroot/current ]]; then
    local tgt
    tgt=$(readlink "$libroot/current")
    if [[ ! -d $libroot/$tgt ]]; then
      rm -f "$libroot/current"
    fi
  fi

  if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$PREFIX/share/applications" >/dev/null 2>&1 || true
  fi
  if [[ $kept_busy -eq 1 ]]; then
    echo "install: quit Inco and run uninstall again to remove what is still running"
    return 1
  fi
  echo "Removed Inco Browser ${names[*]}"
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
