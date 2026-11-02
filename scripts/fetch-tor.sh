#!/usr/bin/env bash
# Download the Tor Expert Bundle and verify it before it lands in tor/.
#
#   1. Fetch the Tor Browser Developers signing key and require the pinned
#      primary fingerprint (temporary GNUPGHOME; your keyring is untouched).
#   2. Verify the OpenPGP signature on the tarball (.asc).
#   3. Verify the signed sha256sums-signed-build.txt and that the tarball's
#      SHA-256 is listed in it.
#   4. Optionally require TOR_BUNDLE_SHA256=<hex> to match as well.
#
# Anything fails -> nothing in tor/ is replaced.
# After a successful run, rebuild Inco so the new hashes are baked into the
# binary (the integrity check refuses a bundle the binary does not know).
#
# Env:
#   TOR_BUNDLE_VERSION  e.g. 15.0.25 (default: latest stable on dist.torproject.org)
#   TOR_BUNDLE_SHA256   optional extra pin for the tarball
#   TOR_OUT_DIR         where to install (default: <repo>/tor)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${TOR_OUT_DIR:-$ROOT/tor}"
# Tor Browser Developers (signing key)
TOR_SIGNING_FPR="EF6E286DDA85EA2A4BA7DE684E2C6E8793298290"
DIST="https://dist.torproject.org/torbrowser"
ARCHIVE="https://archive.torproject.org/tor-package-archive/torbrowser"

for tool in curl gpg sha256sum tar; do
  command -v "$tool" >/dev/null || { echo "fetch-tor: need $tool" >&2; exit 1; }
done

ARCH="$(uname -m)"
case "$ARCH" in
  x86_64) TARCH=x86_64 ;;
  aarch64|arm64) TARCH=aarch64 ;;
  *) echo "fetch-tor: unsupported arch $ARCH" >&2; exit 1 ;;
esac

VER="${TOR_BUNDLE_VERSION:-}"
if [[ -z $VER ]]; then
  VER="$(curl -fsSL "$DIST/" | grep -oE 'href="[0-9]+\.[0-9]+(\.[0-9]+)?/"' \
         | grep -oE '[0-9]+\.[0-9]+(\.[0-9]+)?' | sort -V | tail -1 || true)"
  [[ -n $VER ]] || { echo "fetch-tor: could not detect latest version; set TOR_BUNDLE_VERSION" >&2; exit 1; }
fi
[[ $VER =~ ^[0-9]+\.[0-9]+(\.[0-9]+)?(a[0-9]+)?$ ]] || { echo "fetch-tor: bad version '$VER'" >&2; exit 1; }

NAME="tor-expert-bundle-linux-${TARCH}-${VER}.tar.gz"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
export GNUPGHOME="$TMP/gnupg"
mkdir -m 700 "$GNUPGHOME"

fetch() { # fetch <file> -> $TMP/<file>, trying dist then archive
  local f="$1"
  curl -fsSL -o "$TMP/$f" "$DIST/$VER/$f" 2>/dev/null \
    || curl -fsSL -o "$TMP/$f" "$ARCHIVE/$VER/$f"
}

echo "fetch-tor: version $VER ($TARCH)"
fetch "$NAME"
fetch "$NAME.asc"
fetch "sha256sums-signed-build.txt"
fetch "sha256sums-signed-build.txt.asc"

echo "fetch-tor: fetching signing key $TOR_SIGNING_FPR"
curl -fsSL -o "$TMP/key.asc" "https://keys.openpgp.org/vks/v1/by-fingerprint/$TOR_SIGNING_FPR" \
  || gpg --batch --quiet --auto-key-locate clear,nodefault,wkd --locate-keys torbrowser@torproject.org
[[ -f $TMP/key.asc ]] && gpg --batch --quiet --import "$TMP/key.asc"

verify_sig() { # verify_sig <sig> <file>: good signature by the pinned primary key
  local status
  status="$(gpg --batch --status-fd 1 --verify "$1" "$2" 2>/dev/null || true)"
  if ! grep -q '^\[GNUPG:\] GOODSIG ' <<<"$status"; then
    echo "fetch-tor: BAD or missing signature on $(basename "$2")" >&2
    return 1
  fi
  # VALIDSIG <subkey fpr> ... <primary fpr> (last field)
  if ! grep -E "^\[GNUPG:\] VALIDSIG .* ${TOR_SIGNING_FPR}\$" <<<"$status" >/dev/null; then
    echo "fetch-tor: $(basename "$2") not signed by $TOR_SIGNING_FPR" >&2
    return 1
  fi
}

verify_sig "$TMP/$NAME.asc" "$TMP/$NAME"
echo "fetch-tor: tarball signature OK"
verify_sig "$TMP/sha256sums-signed-build.txt.asc" "$TMP/sha256sums-signed-build.txt"
echo "fetch-tor: sha256sums signature OK"

SUM="$(sha256sum "$TMP/$NAME" | cut -d' ' -f1)"
if ! grep -qE "^${SUM}  ${NAME}\$" "$TMP/sha256sums-signed-build.txt"; then
  echo "fetch-tor: $NAME sha256 $SUM is not in the signed sums" >&2
  exit 1
fi
if [[ -n ${TOR_BUNDLE_SHA256:-} && ${TOR_BUNDLE_SHA256,,} != "$SUM" ]]; then
  echo "fetch-tor: sha256 $SUM does not match TOR_BUNDLE_SHA256" >&2
  exit 1
fi
echo "fetch-tor: sha256 $SUM OK"

# Refuse odd archive members (absolute paths, .., links, devices).
if tar -tzvf "$TMP/$NAME" | awk '{print substr($1,1,1)}' | grep -qv '[-d]'; then
  echo "fetch-tor: archive contains links or special files; refusing" >&2
  exit 1
fi
if tar -tzf "$TMP/$NAME" | grep -qE '(^/|(^|/)\.\.(/|$))'; then
  echo "fetch-tor: archive contains unsafe paths; refusing" >&2
  exit 1
fi

mkdir -p "$TMP/out"
tar -xzf "$TMP/$NAME" -C "$TMP/out" --no-same-owner
STAGE="$TMP/stage"
mkdir -p "$STAGE"
# Bundle layout is tor/ + data/ + docs/ (+ debug/); flatten tor/ into the root.
if [[ -x $TMP/out/tor/tor ]]; then
  cp -a "$TMP/out/tor/." "$STAGE/"
  for d in "$TMP/out"/*; do
    [[ $(basename "$d") == tor ]] && continue
    cp -a "$d" "$STAGE/"
  done
else
  cp -a "$TMP/out/." "$STAGE/"
fi
[[ -x $STAGE/tor ]] || { echo "fetch-tor: no tor binary in bundle" >&2; exit 1; }

cat > "$STAGE/.inco-bundle" <<INFO
Tor Expert Bundle $VER linux-$TARCH
tarball sha256 $SUM
signature verified: $TOR_SIGNING_FPR
INFO

LD_LIBRARY_PATH="$STAGE" "$STAGE/tor" --version | head -1

# Swap in only after every check passed.
if [[ -d $OUT ]]; then
  chmod -R u+w "$OUT" 2>/dev/null || true
  rm -rf "$OUT.old"
  mv "$OUT" "$OUT.old"
fi
mkdir -p "$(dirname "$OUT")"
cp -a "$STAGE" "$OUT"
rm -rf "$OUT.old"
echo "fetch-tor: installed verified bundle into $OUT"
echo "fetch-tor: now rebuild Inco (cmake --build build) so the new hashes are baked in."
