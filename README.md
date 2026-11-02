# Inco Browser

Private-by-default Linux browser with an optional Tor window.

**This is not Tor Browser.**

## What it is

Inco is a small Qt WebEngine browser for Linux. Version 1.0.0.

- No history, no session restore
- Cookies and cache cleared on quit unless you keep logins
- Optional password vault (Argon2id, AES-256-GCM)
- Encryption at rest: while a vault exists, bookmarks and sensitive settings
  (filter lists, search choice, download folder) are encrypted with it too
- Auto-lock: the vault locks after the idle time set in Settings, on suspend
  and on screen lock
- Optional bookmarks
- Inco Filter: Inco's own request filter, with a short built-in host list
  plus block lists taken from uBlock Origin Lite (GPLv3). Network-level
  blocking by host only: no cosmetic filtering, no scriptlets. The uBlock
  Origin lists can be turned off in Settings > Privacy ("Use uBlock Origin
  block lists", on by default)
- Pages cannot reach localhost, link-local or multicast addresses (other LAN
  addresses are not blocked)
- Onionize: second process + bundled Tor, hash-checked before it starts
- Search: DuckDuckGo (recommended, the default) or Startpage. Custom is kept
  only for a custom search address you already had. The address bar, the
  new-tab box and Onionize windows all use the same choice.
- Extensions (optional, off by default): install a Chrome extension from a
  folder, a .zip or a .crx in Settings > Extensions. They never run in
  Onionize windows.

## What it is not

- Not a VPN
- Not Tor Browser
- Not a YouTube ad blocker
- Not a full Chrome extension host: only part of the Chrome extension API
  works, so some extensions load but do not fully work
- The AppImage (`scripts/make-appimage.sh`) is only tested on Fedora so far

## Network

- No telemetry, no crash reports, no automatic update checks.
- "Check for Tor update" and "Update bundled Tor" in Settings contact
  torproject.org, and only when you click them.
- Onionize windows ask check.torproject.org and onionoo.torproject.org,
  through Tor, which exit relay is in use (route panel and status bar).

## Build

Needs Qt 6.10 or newer (extension support).

### Debian / Ubuntu
```bash
sudo apt install build-essential cmake ninja-build curl qt6-base-dev qt6-webengine-dev libssl-dev zlib1g-dev pkg-config
```

### Fedora
```bash
sudo dnf install gcc-c++ cmake ninja-build qt6-qtbase-devel qt6-qtwebengine-devel openssl-devel zlib-ng-compat-devel curl
```

### Arch
```bash
sudo pacman -S base-devel cmake ninja qt6-base qt6-webengine openssl zlib curl
```

Then on all three:
```bash
./scripts/fetch-tor.sh
cmake -G Ninja -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/IncoBrowser
```
## Install

./scripts/install.sh

Open Inco Browser from the application menu.

Installs to `~/.local/lib/inco-browser/<version>/` (made read-only), with
`~/.local/lib/inco-browser/current` and `~/.local/bin/IncoBrowser` as symlinks. No sudo.

## Tor integrity check (Onionize)

- `scripts/fetch-tor.sh` checks the Tor Browser Developers OpenPGP signature
  (pinned fingerprint `EF6E 286D DA85 EA2A 4BA7 DE68 4E2C 6E87 9329 8290`) and the
  signed SHA-256 list before it replaces `tor/`.
- At build time the SHA-256 of every file in `tor/` (except `docs/`, `debug/`) is
  baked into the binary. After updating Tor, rebuild.
- Tor is found via `INCO_TOR_DIR` (absolute path) or `tor/` next to the real
  executable. Never the current directory.
- Before Onionize starts Tor, the bundle must match the baked hashes exactly
  (no changed, missing or extra files). If something else already listens on
  127.0.0.1:9250, Onionize refuses. Either way a dialog explains it and normal
  browsing keeps working.
- Tor data lives in `$XDG_RUNTIME_DIR/inco-browser/tor-data` (0700). Without
  XDG_RUNTIME_DIR it goes to a random 0700 temp folder, never the home folder.
- `IncoBrowser --verify-tor` prints the result without starting anything.

This is an integrity check, not tamper-proofing: anyone who can rewrite the
IncoBrowser binary can also rewrite the baked hashes.

## Route panel (control port)

- The bundled Tor opens its control port only as a Unix socket,
  `tor-data/control.sock`, with cookie auth (`tor-data/control_auth_cookie`).
  No TCP control port, so web pages cannot reach it.
- Before the cookie is sent, Inco checks the peer of the socket (SO_PEERCRED):
  same user, and `/proc/<pid>/exe` must hash to the baked `tor` entry.
- Only `GETINFO` is used (circuit-status, stream-status, ns/id, ip-to-country).
  Countries come from the GeoIP files inside the verified bundle.
- The panel says which circuit it shows: the one carrying the tab's stream,
  the one whose exit matches check.torproject.org, or just the newest one.
  Conflux legs are listed. Anything Inco cannot read is shown as unknown.
- `IncoBrowser --tor-circuit [host]` prints the same data while an Onionize
  window is open.

## Data and encryption

- Data: `~/.config/Inco/IncoBrowser/` (vault, bookmarks, web profile), settings in
  `~/.config/Inco/IncoBrowser.conf`. Folders 0700, files 0600.
- Without kept logins and extensions, normal windows run on a memory-only
  profile: cookies, site storage, visited links and permissions never touch the disk.
- Vault (`vault.bin`, format INCO2): Argon2id (64 MiB, 3 passes) unwraps a random
  data key; AES-256-GCM with the header as associated data; atomic writes.
  Older INCO1 vaults are converted on the first unlock; the copy
  `vault.bin.inco1.bak` is removed at the next unlock, once the INCO2 file has
  opened and verified.
- While a vault exists, bookmarks (`bookmarks.bin`) and the filter lists, search
  engine choice and download folder (`settings.bin`) are encrypted with subkeys of the
  vault data key. The plain files they replace are kept as `*.plain.bak` until
  the encrypted file has been read back and verified on a later unlock, then
  overwritten with zeros and deleted. If verification fails, the copy stays.
  On SSDs and Btrfs, old blocks can survive this; LUKS covers them.
- The vault locks after the idle time set in Settings, on suspend and on screen lock.
- Kept-login cookies, site data of the disk profile and extensions are stored
  unencrypted by Qt WebEngine on Linux. Use full-disk encryption (LUKS, "Encrypt
  my data" in the Fedora installer) to protect them and everything else on disk.
- Onionize keeps Tor state in `/run/user/<uid>/inco-browser` (tmpfs), gone at logout.

## License

Copyright (c) 2026 TheMadForester.

Inco Browser is free software under the GNU General Public License, version 3
(GPLv3). See `LICENSE` for the full text.

Bundled Tor is the official expert bundle under its own licenses (see `tor/`).
"Tor" is a trademark of The Tor Project. Inco is not affiliated.

## Third-party lists and credits

`resources/filter-ubol.txt` holds host rules converted from the default
rulesets of uBlock Origin Lite 2026.1026.1812
(https://github.com/uBlockOrigin/uBOL-home) by `scripts/ubol-lists.py`.
uBlock Origin Lite is by Raymond Hill and the uBlock Origin contributors,
GPLv3. Inco does not ship the extension itself, only these host rules, and
only the parts that work at the network level: path, pattern and regex
rules, redirect and header rules, cosmetic filters and scriptlets are left
out. Regional lists are not included.

The rules stay under their original authors' licenses:

- uBlock filters (ads, trackers, badware): uBlock Origin contributors,
  GPLv3 (https://github.com/uBlockOrigin/uAssets)
- EasyList and EasyPrivacy: the EasyList authors (https://easylist.to/),
  dual licensed GPLv3 or CC BY-SA 3.0
- Peter Lowe's ad and tracking server list (https://pgl.yoyo.org/adservers/):
  no formal license; the author allows combining and republishing it
- Malicious URL Blocklist by malware-filter
  (https://gitlab.com/malware-filter/urlhaus-filter): CC0 and MIT, built
  from URLhaus data (CC0)

To refresh the list, unpack a newer uBlock Origin Lite release (Chromium
zip) and run `scripts/ubol-lists.py <unpacked folder>`. The short built-in
list `resources/filter-hosts.txt` stays and applies even with the uBlock
Origin lists off.
