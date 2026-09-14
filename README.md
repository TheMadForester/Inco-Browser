# Inco Browser 0.1.0
==================

Private-by-default Linux browser. Optional Tor window.
This is not Tor Browser.

What it does
------------
- No browsing history, no session restore
- Cookies/cache cleared on quit unless you keep logins
- Optional AES-256-GCM password vault
- Optional bookmarks (toolbar + new tab)
- Inco Filter: built-in ad/tracker host list (not uBlock)
- Blocks pages from talking to localhost/LAN
- Onionize: second process + bundled Tor (SOCKS 127.0.0.1:9250)

What it does not
----------------
- Not a VPN
- Not Tor Browser (fingerprint still exists)
- YouTube ads will still appear
- No Chrome/Firefox extensions
- No AppImage that works on other distros yet

Build
-----
Fedora:
  sudo dnf install gcc-c++ cmake ninja-build \
    qt6-qtbase-devel qt6-qtwebengine-devel openssl-devel curl

Arch:
  sudo pacman -S base-devel cmake ninja qt6-base qt6-webengine openssl curl

Then:
  ./scripts/fetch-tor.sh
  cmake -G Ninja -S . -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build
  ./build/IncoBrowser

Install (normal app menu)
-------------------------
  ./scripts/install.sh

  or the GitHub installer once a release exists:
  ./scripts/IncoInstaller.sh

Onionize
--------
Uses bundled tor/ next to the install (INCO_TOR_DIR).
The status bar should read "connected to Tor".
Turning Onionize off closes that window.

Data
----
~/.config/Inco/   settings, optional vault, optional bookmarks

License / Tor
-------------
Bundled Tor is the official expert bundle. Keep their licenses in tor/.
"Tor" is a trademark of The Tor Project. Inco is not affiliated.
