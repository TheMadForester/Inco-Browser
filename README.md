# Inco Browser

Private-by-default Linux browser with an optional Tor window.

**This is not Tor Browser.**

## What it is

Inco is a small Qt WebEngine browser for Linux.

- No history, no session restore
- Cookies and cache cleared on quit unless you keep logins
- Optional encrypted password vault
- Optional bookmarks
- Inco Filter (short host list, not uBlock)
- Blocks localhost / LAN
- Onionize: second process + bundled Tor

## What it is not

- Not a VPN
- Not Tor Browser
- Not a YouTube ad blocker
- No extensions
- No working AppImage

## Build

Debian/Ubuntu:

```bash
sudo apt install build-essential cmake ninja-build curl \
  qt6-base-dev qt6-webengine-dev libssl-dev pkg-config
```

Fedora:

```bash
sudo dnf install gcc-c++ cmake ninja-build \
  qt6-qtbase-devel qt6-qtwebengine-devel openssl-devel curl
```

Arch:

```bash
sudo pacman -S base-devel cmake ninja qt6-base qt6-webengine openssl curl
```
```bash
./scripts/fetch-tor.sh
cmake -G Ninja -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/IncoBrowser
```
