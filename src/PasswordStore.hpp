#pragma once

#include "CryptoBox.hpp"
#include "SecureKey.hpp"

#include <QByteArray>
#include <QString>
#include <QVector>

struct SavedLogin
{
    QString host;
    QString user;
    QString password;
};

// Master-password vault, file format INCO2:
//   "INCO2" | version(1) | kdf(1) | memKiB(4) | iterations(4) | lanes(4) | salt(16)
//   | wrapped data key: iv(12) tag(16) ct(32)                      <- header, used as AAD
//   | iv(12) | tag(16) | ciphertext (JSON rows)
// The password only unwraps a random data key; every file Inco encrypts
// uses an HKDF subkey of that data key. Older INCO1 files (PBKDF2, no data
// key) are read once and rewritten as INCO2, with a .inco1.bak copy kept
// until the next unlock opens and verifies the INCO2 file.
class PasswordStore
{
public:
    explicit PasswordStore(const QString& path, bool lowRam = false);
    ~PasswordStore();

    bool exists() const;
    bool isUnlocked() const { return m_unlocked; }

    bool create(const QString& masterPassword);
    bool unlock(const QString& masterPassword);
    bool changeMasterPassword(const QString& oldPw, const QString& newPw);
    void lock();
    QString path() const { return m_path; }
    // Path of the INCO1 copy kept by the last migration (empty if none).
    QString migrationBackup() const { return m_migrationBackup; }

    QVector<SavedLogin> all() const;
    bool upsert(const SavedLogin& login);
    bool removeHost(const QString& host);

    // HKDF-SHA256 subkey of the data key for one purpose, for example
    // "inco/bookmarks". Fails while locked.
    bool subkey(const QByteArray& purpose, SecureKey& out) const;

    // INCO1 or INCO2 magic; used to check a file before importing it.
    static bool looksLikeVault(const QByteArray& blob);

private:
    bool persist() const;
    bool buildHeader(const QByteArray& password);
    bool unlockV1(const QByteArray& blob, const QByteArray& password);
    bool unlockV2(const QByteArray& blob, const QByteArray& password);
    void wipeState();

    QString m_path;
    bool m_lowRam = false;
    SecureKey m_dataKey;     // random DEK, only while unlocked
    QByteArray m_header;     // INCO2 header incl. wrapped DEK (no secrets)
    QVector<SavedLogin> m_rows;
    QString m_migrationBackup;
    bool m_unlocked = false;
};
