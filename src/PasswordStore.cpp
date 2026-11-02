#include "PasswordStore.hpp"
#include "PrivatePaths.hpp"

#include <algorithm>
#include <cstring>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>

namespace {

using namespace CryptoBox;

// INCO1 (0.2.0 and older): "INCO1" | salt(16) | iv(12) | tag(16) | ct, PBKDF2 200k.
constexpr char kMagicV1[] = "INCO1";
constexpr int kV1Rounds = 200000;

constexpr char kMagicV2[] = "INCO2";
constexpr int kMagicLen = 5;
constexpr quint8 kVersion = 2;
// magic | version | kdf | mem | iter | lanes | salt
constexpr int kParamsLen = kMagicLen + 1 + 1 + 4 + 4 + 4 + kSaltLen;
constexpr int kWrappedLen = kIvLen + kTagLen + kKeyLen;
constexpr int kHeaderLen = kParamsLen + kWrappedLen;

const QByteArray kVaultInfo = QByteArrayLiteral("inco/vault");

QByteArray jsonFromRows(const QVector<SavedLogin>& rows)
{
    QJsonArray arr;
    for (const auto& r : rows) {
        QJsonObject o;
        o["host"] = r.host;
        o["user"] = r.user;
        o["password"] = r.password;
        arr.append(o);
    }
    return QJsonDocument(arr).toJson(QJsonDocument::Compact);
}

bool rowsFromJson(const QByteArray& json, QVector<SavedLogin>* rows)
{
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray())
        return false;
    rows->clear();
    for (const auto& v : doc.array()) {
        const auto o = v.toObject();
        rows->push_back({
            o.value("host").toString(),
            o.value("user").toString(),
            o.value("password").toString()
        });
    }
    return true;
}

void putU32(QByteArray& out, quint32 v)
{
    char b[4];
    qToBigEndian(v, b);
    out.append(b, 4);
}

quint32 getU32(const QByteArray& in, int at)
{
    return qFromBigEndian<quint32>(in.constData() + at);
}

} // namespace

PasswordStore::PasswordStore(const QString& path, bool lowRam)
    : m_path(path), m_lowRam(lowRam)
{
    PrivatePaths::makePrivateDir(QFileInfo(m_path).absolutePath());
    PrivatePaths::restrictFile(m_path);
}

PasswordStore::~PasswordStore()
{
    lock();
}

bool PasswordStore::exists() const
{
    return QFile::exists(m_path);
}

bool PasswordStore::looksLikeVault(const QByteArray& blob)
{
    return blob.startsWith(kMagicV1) || blob.startsWith(kMagicV2);
}

void PasswordStore::wipeState()
{
    m_dataKey.clear();
    m_header.clear();
    for (auto& r : m_rows)
        CryptoBox::wipe(r.password);
    m_rows.clear();
    m_unlocked = false;
}

// New salt and KEK for this password; wraps the current data key.
bool PasswordStore::buildHeader(const QByteArray& password)
{
    const KdfParams params = defaultKdf(m_lowRam);
    const QByteArray salt = randomBytes(kSaltLen);
    if (salt.isEmpty())
        return false;

    QByteArray header(kMagicV2, kMagicLen);
    header.append(char(kVersion));
    header.append(char(params.kdf));
    putU32(header, params.memoryKiB);
    putU32(header, params.iterations);
    putU32(header, params.lanes);
    header.append(salt);

    SecureKey kek;
    if (!deriveKey(password, salt, params, kek))
        return false;
    QByteArray dek(reinterpret_cast<const char*>(m_dataKey.data()), int(m_dataKey.size()));
    const QByteArray wrapped = seal(kek, dek, header);
    CryptoBox::wipe(dek);
    if (wrapped.size() != kWrappedLen)
        return false;
    m_header = header + wrapped;
    return true;
}

bool PasswordStore::create(const QString& masterPassword)
{
    wipeState();
    QByteArray pw = masterPassword.toUtf8();
    unsigned char* dek = m_dataKey.resize(kKeyLen);
    bool ok = dek && randomBytes(dek, kKeyLen) && buildHeader(pw);
    CryptoBox::wipe(pw);
    if (!ok) {
        wipeState();
        return false;
    }
    m_unlocked = true;
    if (!persist()) {
        wipeState();
        return false;
    }
    return true;
}

bool PasswordStore::unlockV2(const QByteArray& blob, const QByteArray& password)
{
    if (blob.size() < kHeaderLen + kIvLen + kTagLen || quint8(blob.at(kMagicLen)) != kVersion)
        return false;
    KdfParams params;
    params.kdf = Kdf(quint8(blob.at(kMagicLen + 1)));
    params.memoryKiB = getU32(blob, kMagicLen + 2);
    params.iterations = getU32(blob, kMagicLen + 6);
    params.lanes = getU32(blob, kMagicLen + 10);
    if (params.kdf != Kdf::Argon2id && params.kdf != Kdf::Pbkdf2Sha256)
        return false;
    const QByteArray paramsBytes = blob.left(kParamsLen);
    const QByteArray salt = blob.mid(kParamsLen - kSaltLen, kSaltLen);
    const QByteArray wrapped = blob.mid(kParamsLen, kWrappedLen);
    const QByteArray header = blob.left(kHeaderLen);

    SecureKey kek;
    if (!deriveKey(password, salt, params, kek))
        return false;
    QByteArray dek;
    if (!open(kek, wrapped, paramsBytes, &dek) || dek.size() != kKeyLen) {
        CryptoBox::wipe(dek);
        return false;
    }
    m_dataKey.assign(reinterpret_cast<const unsigned char*>(dek.constData()), size_t(dek.size()));
    CryptoBox::wipe(dek);

    SecureKey vaultKey;
    QByteArray plain;
    if (!hkdf(m_dataKey, kVaultInfo, vaultKey)
        || !open(vaultKey, blob.mid(kHeaderLen), header, &plain)
        || !rowsFromJson(plain, &m_rows)) {
        CryptoBox::wipe(plain);
        return false;
    }
    CryptoBox::wipe(plain);
    m_header = header;
    return true;
}

bool PasswordStore::unlockV1(const QByteArray& blob, const QByteArray& password)
{
    const int magic = int(std::strlen(kMagicV1));
    if (blob.size() < magic + kSaltLen + kIvLen + kTagLen)
        return false;
    const QByteArray salt = blob.mid(magic, kSaltLen);
    SecureKey key;
    QByteArray plain;
    if (!pbkdf2(password, salt, kV1Rounds, key)
        || !open(key, blob.mid(magic + kSaltLen), {}, &plain)
        || !rowsFromJson(plain, &m_rows)) {
        CryptoBox::wipe(plain);
        return false;
    }
    CryptoBox::wipe(plain);

    // Rewrite as INCO2 under a fresh data key; the old file is kept as a copy.
    unsigned char* dek = m_dataKey.resize(kKeyLen);
    if (!dek || !randomBytes(dek, kKeyLen) || !buildHeader(password))
        return false;
    const QString backup = CryptoBox::backupCopy(m_path, QStringLiteral(".inco1.bak"));
    if (backup.isEmpty())
        return false;   // no copy, no rewrite: stay on the readable INCO1 file
    m_migrationBackup = backup;
    // If the rewrite fails the INCO1 file is untouched (atomic write) and the
    // next save tries again.
    m_unlocked = true;
    persist();
    return true;
}

bool PasswordStore::unlock(const QString& masterPassword)
{
    wipeState();
    bool readOk = false;
    const QByteArray blob = CryptoBox::readAll(m_path, &readOk);
    if (!readOk)
        return false;
    QByteArray pw = masterPassword.toUtf8();
    bool ok = false;
    const bool v2 = blob.startsWith(kMagicV2);
    if (v2)
        ok = unlockV2(blob, pw);
    else if (blob.startsWith(kMagicV1))
        ok = unlockV1(blob, pw);
    CryptoBox::wipe(pw);
    if (!ok) {
        // Never leave a derived or half-unwrapped key behind after a miss.
        wipeState();
        return false;
    }
    m_unlocked = true;
    // The INCO2 file just opened and verified from disk, so the INCO1 copy
    // kept by the migration is no longer needed.
    if (v2)
        CryptoBox::removeBackups(m_path + QStringLiteral(".inco1.bak"));
    return true;
}

bool PasswordStore::changeMasterPassword(const QString& oldPw, const QString& newPw)
{
    if (m_unlocked) {
        // Check the old password on a separate instance so a typo does not
        // lock the open vault.
        PasswordStore probe(m_path, m_lowRam);
        if (!probe.unlock(oldPw))
            return false;
    } else if (!unlock(oldPw)) {
        return false;
    }
    // Same data key, new salt and KEK: files sealed with subkeys stay valid.
    QByteArray pw = newPw.toUtf8();
    const QByteArray previous = m_header;
    const bool ok = buildHeader(pw);
    CryptoBox::wipe(pw);
    if (!ok || !persist()) {
        m_header = previous;
        return false;
    }
    return true;
}

void PasswordStore::lock()
{
    wipeState();
}

QVector<SavedLogin> PasswordStore::all() const
{
    return m_unlocked ? m_rows : QVector<SavedLogin>{};
}

bool PasswordStore::subkey(const QByteArray& purpose, SecureKey& out) const
{
    if (!m_unlocked || m_dataKey.isEmpty())
        return false;
    return hkdf(m_dataKey, purpose, out);
}

bool PasswordStore::persist() const
{
    if (!m_unlocked || m_header.size() != kHeaderLen)
        return false;
    SecureKey vaultKey;
    if (!hkdf(m_dataKey, kVaultInfo, vaultKey))
        return false;
    QByteArray json = jsonFromRows(m_rows);
    const QByteArray body = seal(vaultKey, json, m_header);
    CryptoBox::wipe(json);
    if (body.isEmpty())
        return false;
    return CryptoBox::writeAtomic(m_path, m_header + body);
}

bool PasswordStore::upsert(const SavedLogin& login)
{
    if (!m_unlocked)
        return false;
    bool found = false;
    for (auto& row : m_rows) {
        if (row.host == login.host && row.user == login.user) {
            row.password = login.password;
            found = true;
            break;
        }
    }
    if (!found)
        m_rows.push_back(login);
    return persist();
}

bool PasswordStore::removeHost(const QString& host)
{
    if (!m_unlocked)
        return false;
    m_rows.erase(std::remove_if(m_rows.begin(), m_rows.end(),
                                [&](const SavedLogin& r) { return r.host == host; }),
                 m_rows.end());
    return persist();
}
