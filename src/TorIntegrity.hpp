#pragma once

// Integrity check for the bundled Tor (Option A lock).
//
// At build time CMake hashes every file of the tor/ bundle (except docs/ and
// debug/, which are never loaded) and bakes the SHA-256 list into the binary.
// Before Onionize starts Tor, the bundle on disk must match that list exactly:
// no changed, missing or extra files. This is an integrity check, not a
// guarantee: anyone who can rewrite the IncoBrowser binary itself can also
// rewrite the baked list.

#include <QString>
#include <QStringList>

namespace TorIntegrity {

constexpr quint16 kSocksPort = 9250;

enum class Status {
    Ok,
    NoManifest,   // binary was built without a tor/ bundle
    NotFound,     // no bundle at INCO_TOR_DIR / next to the executable
    Failed        // missing, changed or unexpected files
};

struct Result {
    Status status = Status::NotFound;
    QString torDir;          // resolved bundle directory (canonical)
    QString torBin;          // torDir + "/tor"
    QStringList problems;    // human-readable, one per file
    bool ok() const { return status == Status::Ok; }
};

enum class PortState {
    Free,       // nothing listens on 127.0.0.1:9250
    OurTor,     // listener is the verified bundled tor binary
    Foreign     // someone else (or unknown) owns the port
};

// Directory of the running executable, symlinks resolved (/proc/self/exe).
// Never derived from argv[0] or the current working directory.
QString executableDir();

// INCO_TOR_DIR (must be absolute) or <exe dir>/tor or <exe dir>/../tor.
QString resolveTorDir(QString* why = nullptr);

// Resolve + hash-check the bundle against the baked manifest.
Result verifyBundle();

// Who owns 127.0.0.1:9250? Requires a verified Result.
PortState socksPortState(const Result& verified, QString* detail = nullptr);

// Does /proc/<pid>/exe hash to the baked "tor" entry?
bool runningBinaryMatches(qint64 pid);

// Private per-user Tor data dir, mode 0700, never a fixed name in /tmp.
// $XDG_RUNTIME_DIR/inco-browser/tor-data, else tor-data in a random 0700
// temp folder that lives as long as this process.
QString dataDir();

int manifestFileCount();
QString bakedBundleInfo();   // contents of tor/.inco-bundle at build time, if any

// One-paragraph English summary for logs / dialogs.
QString describe(const Result& r);

} // namespace TorIntegrity
