#!/usr/bin/env bash
# Install download for Inco Browser.
# Downloads the portable AppImage and SHA256SUMS from the GitHub release,
# checks the file, then installs it into the account menu.
# It does not stay running, start at login, or check for updates later.
#
# A separate graphical program would need GTK or Qt. Those are large, and
# the AppImage exists so the browser does not depend on them. This script
# uses kdialog (KDE) or zenity (GNOME) for a progress window when one of
# those is already installed, and the terminal otherwise.
set -euo pipefail

INCO_VERSION="${INCO_VERSION:-1.0.1}"
INCO_REPO="${INCO_REPO:-TheMadForester/Inco-Browser}"
INCO_ASSET="Inco_Browser-${INCO_VERSION}-x86_64.AppImage"
INCO_RELEASE_BASE="${INCO_RELEASE_BASE:-https://github.com/${INCO_REPO}/releases/download/v${INCO_VERSION}}"

if ! declare -f install_tree >/dev/null 2>&1; then
  lib="$(cd "$(dirname "$0")" && pwd)/install-appdir.sh"
  if [[ ! -f $lib ]]; then
    printf 'install: this copy is incomplete (missing install-appdir.sh)\n' >&2
    exit 1
  fi
  INCO_INSTALL_LIBRARY=1
  # shellcheck disable=SC1090
  source "$lib"
fi

ui_kind=text
ui_ref=""
ui_pid=""
ui_fifo=""

have_display() { [[ -n ${DISPLAY:-}${WAYLAND_DISPLAY:-} ]]; }

qdbus_bin() { command -v qdbus6 || command -v qdbus || true; }

ui_open() {
  ui_kind=text
  [[ ${INCO_UI:-} == text ]] && return 0
  if have_display && command -v kdialog >/dev/null 2>&1; then
    ui_ref=$(kdialog --title "Inco Browser" --progressbar "Starting" 100 2>/dev/null || true)
    [[ -n $ui_ref ]] && ui_kind=kdialog && return 0
  fi
  if have_display && command -v zenity >/dev/null 2>&1; then
    ui_fifo=$(mktemp -u)
    mkfifo "$ui_fifo"
    zenity --progress --title="Inco Browser" --text="Starting" --percentage=0 --auto-close <"$ui_fifo" &
    ui_pid=$!
    exec 9>"$ui_fifo"
    ui_kind=zenity
  fi
}

ui_step() {
  local pct=$1 msg=$2 q
  printf '%s\n' "$msg"
  case $ui_kind in
    kdialog)
      q=$(qdbus_bin)
      if [[ -n $q ]]; then
        "$q" $ui_ref Set "" value "$pct" >/dev/null 2>&1 || true
        "$q" $ui_ref setLabelText "$msg" >/dev/null 2>&1 || true
      fi
      ;;
    zenity)
      printf '%s\n# %s\n' "$pct" "$msg" >&9 || true
      ;;
  esac
}

ui_close() {
  local q
  case $ui_kind in
    kdialog)
      q=$(qdbus_bin)
      [[ -n $q && -n $ui_ref ]] && "$q" $ui_ref close >/dev/null 2>&1 || true
      ui_ref=""
      ui_kind=text
      ;;
    zenity)
      exec 9>&- 2>/dev/null || true
      [[ -n $ui_pid ]] && wait "$ui_pid" 2>/dev/null || true
      rm -f "$ui_fifo"
      ui_pid=""
      ui_kind=text
      ;;
  esac
}

ui_fail() {
  local msg=$1
  ui_close
  printf 'install: %s\n' "$msg" >&2
  if [[ ${INCO_UI:-} != text ]] && have_display; then
    if command -v kdialog >/dev/null 2>&1; then
      kdialog --title "Inco Browser" --error "$msg" >/dev/null 2>&1 || true
    elif command -v zenity >/dev/null 2>&1; then
      zenity --error --title="Inco Browser" --text="$msg" >/dev/null 2>&1 || true
    fi
  fi
  exit 1
}

ui_done() {
  local msg=$1
  ui_step 100 "$msg"
  ui_close
  if [[ ${INCO_UI:-} != text ]] && have_display; then
    if command -v kdialog >/dev/null 2>&1; then
      kdialog --title "Inco Browser" --msgbox "$msg" >/dev/null 2>&1 || true
    elif command -v zenity >/dev/null 2>&1; then
      zenity --info --title="Inco Browser" --text="$msg" >/dev/null 2>&1 || true
    fi
  fi
}

usage() {
  cat <<EOF
Install Inco Browser ${INCO_VERSION} for this account.

  bash Inco_Browser-${INCO_VERSION}-x86_64.install
  bash Inco_Browser-${INCO_VERSION}-x86_64.install --uninstall

The script downloads the portable AppImage and SHA256SUMS from the
GitHub release, checks the file, and only then puts Inco in the menu.
Nothing is installed if the download or the checksum fails.
No root, no autostart, no background program.
EOF
}

# Download $url to $dest. Prints the HTTP status, or 000 if the network failed.
http_get() {
  local url=$1 dest=$2 code rc
  set +e
  code=$(curl -sS -L --retry 2 --retry-delay 1 -o "$dest" -w "%{http_code}" "$url" 2>"$dest.err")
  rc=$?
  set -e
  if [[ $rc -ne 0 && ( -z ${code:-} || $code == 000 ) ]]; then
    printf '000'
    return 0
  fi
  printf '%s' "${code:-000}"
}

explain_http() {
  local code=$1 name=$2
  if [[ $code == 000 ]]; then
    ui_fail "Could not reach GitHub to download ${name}. Check the network and run this again. Nothing was installed."
  fi
  if [[ $code == 404 ]]; then
    ui_fail "GitHub has no public ${name} for v${INCO_VERSION} (HTTP 404). A draft release is not downloadable without a login. Publish the release, or mark it as a pre-release, then run this again. Nothing was installed."
  fi
  if [[ $code != 200 ]]; then
    ui_fail "GitHub returned HTTP ${code} for ${name}. Nothing was installed."
  fi
}

download_and_install() {
  local work sums app hash got appdir
  command -v curl >/dev/null 2>&1 || ui_fail "This installer needs curl. Fedora: sudo dnf install curl. Debian: sudo apt install curl."
  command -v sha256sum >/dev/null 2>&1 || ui_fail "This installer needs sha256sum from coreutils."

  download_work=$(mktemp -d)
  ui_open

  ui_step 10 "Downloading the checksum list"
  sums=$download_work/SHA256SUMS
  explain_http "$(http_get "$INCO_RELEASE_BASE/SHA256SUMS" "$sums")" "SHA256SUMS"

  ui_step 30 "Downloading Inco Browser ${INCO_VERSION}"
  app=$download_work/$INCO_ASSET
  explain_http "$(http_get "$INCO_RELEASE_BASE/$INCO_ASSET" "$app")" "$INCO_ASSET"

  ui_step 75 "Checking the download"
  hash=$(awk -v name="$INCO_ASSET" '$2 == name { print $1; exit }' "$sums")
  if [[ -z $hash ]]; then
    ui_fail "SHA256SUMS from the v${INCO_VERSION} release does not list ${INCO_ASSET}. Nothing was installed."
  fi
  got=$(sha256sum "$app" | awk '{ print $1 }')
  if [[ ${got,,} != ${hash,,} ]]; then
    ui_fail "The download does not match the published SHA256SUMS. Nothing was installed."
  fi

  ui_step 85 "Unpacking"
  chmod +x "$app"
  if ! (cd "$download_work" && "$app" --appimage-extract >/dev/null 2>&1); then
    ui_fail "The AppImage did not unpack. Nothing was installed."
  fi
  appdir=$download_work/squashfs-root
  [[ -d $appdir ]] || ui_fail "The AppImage did not unpack. Nothing was installed."

  ui_step 92 "Installing into the application menu"
  install_tree "$appdir"
  ui_done "Inco Browser ${INCO_VERSION} is in the application menu."
}

case ${1:-} in
  -h|--help) usage; exit 0 ;;
  --uninstall)
    check_prefix
    uninstall_tree
    exit $?
    ;;
  "")
    check_prefix
    download_and_install
    ;;
  *)
    printf 'install: unknown argument %s\n' "$1" >&2
    usage >&2
    exit 2
    ;;
esac
