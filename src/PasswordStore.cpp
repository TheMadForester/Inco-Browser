#include "PasswordStore.hpp"

#include <algorithm>
#include <cstring>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>

#include <openssl/evp.h>
#include <openssl/rand.h>

namespace {
constexpr int kSaltLen = 16;
constexpr int kIvLen = 12;
constexpr int kTagLen = 16;
constexpr int kKeyLen = 32;
constexpr int kIters = 200000;
constexpr char kMagic[] = "INCO1";

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

QVector<SavedLogin> rowsFromJson(const QByteArray& json)
{
    QVector<SavedLogin> rows;
    const auto doc = QJsonDocument::fromJson(json);
    for (const auto& v : doc.array()) {
        const auto o = v.toObject();
        rows.push_back({
            o.value("host").toString(),
            o.value("user").toString(),
            o.value("password").toString()
        });
    }
    return rows;
}
} // namespace

PasswordStore::PasswordStore(const QString& path)
    : m_path(path)
{
    QDir().mkpath(QFileInfo(m_path).absolutePath());
}

bool PasswordStore::exists() const
{
    return QFile::exists(m_path);
}

QByteArray PasswordStore::deriveKey(const QString& master, const QByteArray& salt) const
{
    QByteArray key(kKeyLen, 0);
    const QByteArray pass = master.toUtf8();
    if (PKCS5_PBKDF2_HMAC(pass.constData(), pass.size(),
                          reinterpret_cast<const unsigned char*>(salt.constData()), salt.size(),
                          kIters, EVP_sha256(), kKeyLen,
                          reinterpret_cast<unsigned char*>(key.data())) != 1)
        return {};
    return key;
}

QByteArray PasswordStore::encrypt(const QByteArray& plaintext) const
{
    QByteArray iv(kIvLen, 0);
    if (RAND_bytes(reinterpret_cast<unsigned char*>(iv.data()), kIvLen) != 1)
        return {};

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    QByteArray out(plaintext.size(), 0);
    QByteArray tag(kTagLen, 0);
    int len = 0;
    int outLen = 0;

    bool ok =
        ctx &&
        EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr,
                           reinterpret_cast<const unsigned char*>(m_key.constData()),
                           reinterpret_cast<const unsigned char*>(iv.constData())) == 1 &&
        EVP_EncryptUpdate(ctx,
                          reinterpret_cast<unsigned char*>(out.data()), &len,
                          reinterpret_cast<const unsigned char*>(plaintext.constData()),
                          plaintext.size()) == 1;
    outLen = len;
    ok = ok && EVP_EncryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(out.data()) + len, &len) == 1;
    outLen += len;
    out.resize(outLen);
    ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, kTagLen, tag.data()) == 1;
    EVP_CIPHER_CTX_free(ctx);
    if (!ok)
        return {};

    return QByteArray(kMagic) + m_salt + iv + tag + out;
}

QByteArray PasswordStore::decrypt(const QByteArray& blob) const
{
    const int prefix = int(strlen(kMagic)) + kSaltLen + kIvLen + kTagLen;
    if (blob.size() < prefix || !blob.startsWith(kMagic))
        return {};

    const QByteArray iv = blob.mid(int(strlen(kMagic)) + kSaltLen, kIvLen);
    const QByteArray tag = blob.mid(int(strlen(kMagic)) + kSaltLen + kIvLen, kTagLen);
    const QByteArray ct = blob.mid(prefix);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    QByteArray out(ct.size(), 0);
    int len = 0;
    int outLen = 0;
    bool ok =
        ctx &&
        EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr,
                           reinterpret_cast<const unsigned char*>(m_key.constData()),
                           reinterpret_cast<const unsigned char*>(iv.constData())) == 1 &&
        EVP_DecryptUpdate(ctx,
                          reinterpret_cast<unsigned char*>(out.data()), &len,
                          reinterpret_cast<const unsigned char*>(ct.constData()),
                          ct.size()) == 1;
    outLen = len;
    ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, kTagLen,
                                   const_cast<char*>(tag.constData())) == 1;
    ok = ok && EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(out.data()) + len, &len) == 1;
    outLen += len;
    EVP_CIPHER_CTX_free(ctx);
    if (!ok)
        return {};
    out.resize(outLen);
    return out;
}

bool PasswordStore::create(const QString& masterPassword)
{
    m_salt = QByteArray(kSaltLen, 0);
    if (RAND_bytes(reinterpret_cast<unsigned char*>(m_salt.data()), kSaltLen) != 1)
        return false;
    m_key = deriveKey(masterPassword, m_salt);
    m_rows.clear();
    m_unlocked = true;
    return persist();
}

bool PasswordStore::unlock(const QString& masterPassword)
{
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray blob = file.readAll();
    if (!blob.startsWith(kMagic) || blob.size() < int(strlen(kMagic)) + kSaltLen)
        return false;

    m_salt = blob.mid(int(strlen(kMagic)), kSaltLen);
    m_key = deriveKey(masterPassword, m_salt);
    const QByteArray plain = decrypt(blob);
    if (plain.isNull() && !plain.isEmpty())
        return false;
    if (plain.isEmpty() && blob.size() > int(strlen(kMagic)) + kSaltLen + kIvLen + kTagLen + 2)
        return false;

    // empty vault is valid ("[]")
    if (plain.isEmpty())
        return false;

    m_rows = rowsFromJson(plain);
    m_unlocked = true;
    return true;
}


bool PasswordStore::changeMasterPassword(const QString& oldPw, const QString& newPw)
{
    if (!unlock(oldPw))
        return false;
    const auto rows = m_rows;
    lock();
    if (!create(newPw))
        return false;
    m_rows = rows;
    return persist();
}

void PasswordStore::lock()
{
    m_key.fill('\0');
    m_key.clear();
    m_rows.clear();
    m_unlocked = false;
}

QVector<SavedLogin> PasswordStore::all() const
{
    return m_unlocked ? m_rows : QVector<SavedLogin>{};
}

bool PasswordStore::persist() const
{
    if (!m_unlocked)
        return false;
    const QByteArray blob = encrypt(jsonFromRows(m_rows));
    if (blob.isEmpty())
        return false;
    QFile file(m_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(blob) == blob.size();
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
