#pragma once

#include "SecureKey.hpp"

#include <QByteArray>
#include <QString>

// AES-256-GCM sealing, key derivation and private file writes, all on the
// OpenSSL that Inco already links. Sealed layout: iv(12) | tag(16) | ct.
namespace CryptoBox {

constexpr int kKeyLen = 32;
constexpr int kIvLen = 12;
constexpr int kTagLen = 16;
constexpr int kSaltLen = 16;

enum class Kdf : quint8 {
    Argon2id = 1,       // OpenSSL 3.2+ EVP_KDF "ARGON2ID"
    Pbkdf2Sha256 = 2,   // fallback when the linked OpenSSL has no Argon2id
};

struct KdfParams
{
    Kdf kdf = Kdf::Argon2id;
    quint32 memoryKiB = 64 * 1024;   // Argon2id only
    quint32 iterations = 3;          // Argon2id passes, or PBKDF2 rounds
    quint32 lanes = 1;               // Argon2id only
};

// Argon2id m=64 MiB t=3 p=1 (Low RAM: m=32 MiB t=4). PBKDF2-SHA256 with
// 600,000 rounds if Argon2id is not available at runtime.
KdfParams defaultKdf(bool lowRam);
bool argon2Available();
// Refuses parameters that are out of range (a crafted file must not make
// the unlock allocate gigabytes or spin for minutes).
bool kdfParamsSane(const KdfParams& p);

bool randomBytes(unsigned char* out, int n);
QByteArray randomBytes(int n);

// Password -> 32-byte key.
bool deriveKey(const QByteArray& password, const QByteArray& salt,
               const KdfParams& params, SecureKey& out);
// PBKDF2-HMAC-SHA256 with explicit rounds (the INCO1 vault format).
bool pbkdf2(const QByteArray& password, const QByteArray& salt, int rounds, SecureKey& out);
// HKDF-SHA256(ikm, info) -> 32-byte subkey.
bool hkdf(const SecureKey& ikm, const QByteArray& info, SecureKey& out);

// AES-256-GCM with a fresh random 96-bit nonce; aad is authenticated, not stored.
QByteArray seal(const SecureKey& key, const QByteArray& plain, const QByteArray& aad = {});
// Returns false on a wrong key, tampering or a truncated blob.
bool open(const SecureKey& key, const QByteArray& sealed, const QByteArray& aad, QByteArray* plain);
bool open(const unsigned char* key, const QByteArray& sealed, const QByteArray& aad, QByteArray* plain);

// Small self-describing file for data sealed with a vault subkey:
// "INCS" | version(1) | purpose length(1) | purpose | sealed. The header is the AAD,
// so a bookmarks file cannot be swapped in as the settings file.
QByteArray sealFile(const SecureKey& key, const QByteArray& purpose, const QByteArray& plain);
bool openFile(const SecureKey& key, const QByteArray& purpose, const QByteArray& blob, QByteArray* plain);
bool isSealedFile(const QByteArray& blob);

// Write to a temp file, fsync, rename over path. File mode 0600, parent 0700.
bool writeAtomic(const QString& path, const QByteArray& data);
QByteArray readAll(const QString& path, bool* ok = nullptr);
// Copies path to path + suffix (or path + suffix + ".N" if taken). Never
// overwrites or deletes anything. Returns the backup path, empty on failure.
QString backupCopy(const QString& path, const QString& suffix);
// Same, but renames instead of copying (for files that must not stay live).
QString backupMove(const QString& path, const QString& suffix);

// Best effort removal of a plaintext copy: overwrite with zeros, fsync,
// unlink. On SSDs and copy-on-write file systems (Btrfs) the old blocks can
// survive, so this only shrinks the window; full-disk encryption covers it.
bool secureRemove(const QString& path);
// secureRemove() for path and the numbered copies backupCopy() makes
// (path.1, path.2, ...). Returns how many files were removed.
int removeBackups(const QString& path);

// Overwrites the bytes, then empties the array.
void wipe(QByteArray& bytes);
void wipe(QString& text);

} // namespace CryptoBox
