Inco Browser 1.0.1
==================

Private-by-default Linux browser. Optional Tor window.
This is not Tor Browser.

What it does
------------
- No browsing history, no session restore
- Cookies/cache cleared on quit unless you keep logins
- Optional AES-256-GCM password vault (Argon2id key)
- Encryption at rest: with a vault, bookmarks and sensitive settings are
  encrypted too
- Auto-lock on idle (Settings), on suspend and on screen lock
- Optional bookmarks (toolbar + new tab)
- Inco Filter: built-in ad/tracker host list plus block lists taken
  from uBlock Origin Lite (GPLv3). Network blocking by host only, no
  cosmetic or script filtering. "Use uBlock Origin block lists" in
  Settings > Privacy turns the uBlock Origin lists off (on by default).
- Pages cannot reach localhost, link-local or multicast addresses
  (other LAN addresses are not blocked)
- Onionize: second process + bundled Tor (SOCKS 127.0.0.1:9250)
- Search: DuckDuckGo (recommended, default) or Startpage. Custom is kept
  only for a custom search address you already had. Address bar, new tab
  and Onionize all follow the same choice.
- Extensions (optional, off by default): install a Chrome extension from a
  folder, .zip or .crx in Settings > Extensions. Never in Onionize windows.

What it does not
----------------
- Not a VPN
- Not Tor Browser (fingerprint still exists)
- YouTube ads will still appear
- Not a full Chrome extension host (only part of the Chrome extension API
  works, so some extensions load but do not fully work)
- AppImage (scripts/make-appimage.sh) only tested on Fedora so far

Network
-------
- No telemetry, no crash reports, no automatic update checks.
- "Check for Tor update" and "Update bundled Tor" in Settings contact
  torproject.org, only when you click them.
- Onionize windows ask check.torproject.org and onionoo.torproject.org,
  through Tor, which exit relay is in use.

Build
-----
Needs Qt 6.10 or newer (extension support).

Fedora:
  sudo dnf install gcc-c++ cmake ninja-build \
    qt6-qtbase-devel qt6-qtwebengine-devel openssl-devel zlib-ng-compat-devel curl

Arch:
  sudo pacman -S base-devel cmake ninja qt6-base qt6-webengine openssl zlib curl

Then:
  ./scripts/fetch-tor.sh
  cmake -G Ninja -S . -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build
  ./build/IncoBrowser

Download
--------
The release has two files. They are the same browser.

Instant Use: Inco_Browser-<version>-x86_64.AppImage
  chmod +x the file once, then run it. Nothing is installed.

Install: Inco_Browser-<version>-x86_64.install
  bash Inco_Browser-<version>-x86_64.install
  Opens from the application menu afterwards. No chmod, no root, no
  autostart, no background program. Files go under ~/.local only.
  Remove with: bash Inco_Browser-<version>-x86_64.install --uninstall

Installing unpacks the app once so later launches skip the AppImage
mount. It does not make Chromium startup instant.

1.0.1 packs the Wayland EGL plugin that 1.0.0 left out. Without it,
Fedora KDE Wayland started the process and showed no usable window.

Install from a source build
---------------------------
  ./scripts/install.sh

Onionize
--------
Uses bundled tor/ next to the real executable, or INCO_TOR_DIR (absolute).
Before Tor starts, every bundle file is checked against SHA-256 hashes baked
into the binary at build time (integrity check). A foreign program on
127.0.0.1:9250 is refused. On failure only Onionize is refused; normal
browsing continues. Check by hand: IncoBrowser --verify-tor
The status bar should read "connected to Tor".
Turning Onionize off closes that window.

Data and encryption
-------------------
~/.config/Inco/IncoBrowser/       vault, bookmarks, web profile (0700, files 0600)
~/.config/Inco/IncoBrowser.conf   settings

- No kept logins and no extensions: normal windows keep cookies and site
  data in memory only.
- Vault: Argon2id + AES-256-GCM (INCO2). INCO1 vaults are converted on the
  first unlock; the old file stays as vault.bin.inco1.bak until the next
  unlock verifies the INCO2 file, then it is zeroed and deleted.
- With a vault, bookmarks and the filter lists, search engine choice and
  download folder are encrypted too. Replaced plain files stay as *.plain.bak until a
  later unlock verifies the encrypted file, then they are zeroed and deleted
  (best effort: SSDs and Btrfs can keep old blocks; LUKS covers that).
- Auto-lock on idle (Settings), on suspend and on screen lock.
- Kept-login cookies and extension data are stored unencrypted by Qt
  WebEngine. Use full-disk encryption (LUKS, "Encrypt my data" in the
  Fedora installer) for full protection.

License / Tor
-------------
Copyright (c) 2026 TheMadForester.
Inco Browser is free software under the GNU GPL, version 3 (GPLv3).
See LICENSE for the full text.

Bundled Tor is the official expert bundle. Keep their licenses in tor/.
"Tor" is a trademark of The Tor Project. Inco is not affiliated.

Third-party lists / credits
---------------------------
resources/filter-ubol.txt holds host rules converted by
scripts/ubol-lists.py from the default rulesets of uBlock Origin Lite
2026.1026.1812 (https://github.com/uBlockOrigin/uBOL-home), by Raymond
Hill and the uBlock Origin contributors, GPLv3. The extension itself is
not shipped. Path, pattern and regex rules, redirects, header rules,
cosmetic filters, scriptlets and regional lists are left out.
The rules keep their original licenses:
- uBlock filters (ads, trackers, badware): GPLv3
  (https://github.com/uBlockOrigin/uAssets)
- EasyList, EasyPrivacy: the EasyList authors, GPLv3 or CC BY-SA 3.0
  (https://easylist.to/)
- Peter Lowe's ad server list: no formal license, the author allows
  combining and republishing it (https://pgl.yoyo.org/adservers/)
- Malicious URL Blocklist (malware-filter): CC0 and MIT, URLhaus data CC0
  (https://gitlab.com/malware-filter/urlhaus-filter)
