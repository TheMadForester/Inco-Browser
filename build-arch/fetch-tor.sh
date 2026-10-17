#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VER="${TOR_BUNDLE_VERSION:-15.0.22}"
ARCH="$(uname -m)"
case "$ARCH" in
  x86_64) TARCH=x86_64 ;;
  aarch64|arm64) TARCH=aarch64 ;;
  *) echo "unsupported $ARCH"; exit 1 ;;
esac
URL="https://dist.torproject.org/torbrowser/${VER}/tor-expert-bundle-linux-${TARCH}-${VER}.tar.gz"
TMP="$(mktemp -d)"
curl -L --fail -o "$TMP/bundle.tar.gz" "$URL"
mkdir -p "$TMP/out" "$ROOT/tor"
tar -xzf "$TMP/bundle.tar.gz" -C "$TMP/out"
rm -rf "$ROOT/tor"
mkdir -p "$ROOT/tor"
cp -a "$TMP/out/." "$ROOT/tor/"
# flatten if nested
if [[ -x $ROOT/tor/tor/tor ]]; then
  mv "$ROOT/tor/tor" "$TMP/torbin"
  mv "$TMP/torbin/"* "$ROOT/tor/" 2>/dev/null || true
fi
chmod +x "$ROOT/tor/tor"
export LD_LIBRARY_PATH="$ROOT/tor:$ROOT/tor/lib:${LD_LIBRARY_PATH:-}"
"$ROOT/tor/tor" --version
rm -rf "$TMP"
