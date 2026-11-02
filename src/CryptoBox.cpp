#include "CryptoBox.hpp"
#include "PrivatePaths.hpp"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <openssl/core_names.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/params.h>
#include <openssl/rand.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace CryptoBox {

namespace {

constexpr char kFileMagic[] = "INCS";
constexpr int kFileMagicLen = 4;
constexpr quint8 kFileVersion = 1;
constexpr quint32 kPbkdf2Rounds = 600000;

const unsigned char* bytes(const QByteArray& a)
{
    return reinterpret_cast<const unsigned char*>(a.constData());
}

QByteArray fileHeader(const QByteArray& purpose)
{
    QByteArray h(kFileMagic, kFileMagicLen);
    h.append(char(kFileVersion));
    h.append(char(quint8(purpose.size())));
    h.append(purpose);
    return h;
}

} // namespace

KdfParams defaultKdf(bool lowRam)
{
    KdfParams p;
    if (!argon2Available()) {
        p.kdf = Kdf::Pbkdf2Sha256;
        p.memoryKiB = 0;
        p.iterations = kPbkdf2Rounds;
        p.lanes = 0;
        return p;
    }
    p.kdf = Kdf::Argon2id;
    p.memoryKiB = lowRam ? 32 * 1024 : 64 * 1024;
    p.iterations = lowRam ? 4 : 3;
    p.lanes = 1;
    return p;
}

bool argon2Available()
{
    static const bool available = [] {
        EVP_KDF* kdf = EVP_KDF_fetch(nullptr, "ARGON2ID", nullptr);
        EVP_KDF_free(kdf);
        return kdf != nullptr;
    }();
    return available;
}

bool kdfParamsSane(const KdfParams& p)
{
    switch (p.kdf) {
    case Kdf::Argon2id:
        return p.memoryKiB >= 8 * 1024 && p.memoryKiB <= 1024 * 1024
            && p.iterations >= 1 && p.iterations <= 16
            && p.lanes >= 1 && p.lanes <= 8;
    case Kdf::Pbkdf2Sha256:
        return p.iterations >= 100000 && p.iterations <= 10000000;
    }
    return false;
}

bool randomBytes(unsigned char* out, int n)
{
    return n >= 0 && RAND_bytes(out, n) == 1;
}

QByteArray randomBytes(int n)
{
    QByteArray b(n, 0);
    if (!randomBytes(reinterpret_cast<unsigned char*>(b.data()), n))
        return {};
    return b;
}

bool pbkdf2(const QByteArray& password, const QByteArray& salt, int rounds, SecureKey& out)
{
    unsigned char* dst = out.resize(kKeyLen);
    if (!dst)
        return false;
    if (PKCS5_PBKDF2_HMAC(password.constData(), int(password.size()), bytes(salt), int(salt.size()),
                          rounds, EVP_sha256(), kKeyLen, dst) != 1) {
        out.clear();
        return false;
    }
    return true;
}

bool deriveKey(const QByteArray& password, const QByteArray& salt,
               const KdfParams& params, SecureKey& out)
{
    if (!kdfParamsSane(params))
        return false;
    if (params.kdf == Kdf::Pbkdf2Sha256)
        return pbkdf2(password, salt, int(params.iterations), out);

    EVP_KDF* kdf = EVP_KDF_fetch(nullptr, "ARGON2ID", nullptr);
    if (!kdf)
        return false;
    EVP_KDF_CTX* ctx = EVP_KDF_CTX_new(kdf);
    EVP_KDF_free(kdf);
    if (!ctx)
        return false;

    uint32_t iter = params.iterations;
    uint32_t lanes = params.lanes;
    uint32_t threads = 1;
    uint32_t mem = params.memoryKiB;
    OSSL_PARAM p[] = {
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_PASSWORD,
            const_cast<char*>(password.constData()), size_t(password.size())),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT,
            const_cast<char*>(salt.constData()), size_t(salt.size())),
        OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ITER, &iter),
        OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ARGON2_LANES, &lanes),
        OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_THREADS, &threads),
        OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ARGON2_MEMCOST, &mem),
        OSSL_PARAM_construct_end(),
    };
    unsigned char* dst = out.resize(kKeyLen);
    const bool ok = dst && EVP_KDF_derive(ctx, dst, kKeyLen, p) == 1;
    EVP_KDF_CTX_free(ctx);
    if (!ok)
        out.clear();
    return ok;
}

bool hkdf(const SecureKey& ikm, const QByteArray& info, SecureKey& out)
{
    if (ikm.isEmpty())
        return false;
    EVP_KDF* kdf = EVP_KDF_fetch(nullptr, "HKDF", nullptr);
    if (!kdf)
        return false;
    EVP_KDF_CTX* ctx = EVP_KDF_CTX_new(kdf);
    EVP_KDF_free(kdf);
    if (!ctx)
        return false;
    char digest[] = "SHA256";
    OSSL_PARAM p[] = {
        OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST, digest, 0),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY,
            const_cast<unsigned char*>(ikm.data()), ikm.size()),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_INFO,
            const_cast<char*>(info.constData()), size_t(info.size())),
        OSSL_PARAM_construct_end(),
    };
    unsigned char* dst = out.resize(kKeyLen);
    const bool ok = dst && EVP_KDF_derive(ctx, dst, kKeyLen, p) == 1;
    EVP_KDF_CTX_free(ctx);
    if (!ok)
        out.clear();
    return ok;
}

QByteArray seal(const SecureKey& key, const QByteArray& plain, const QByteArray& aad)
{
    if (key.size() != kKeyLen)
        return {};
    QByteArray iv = randomBytes(kIvLen);
    if (iv.isEmpty())
        return {};
    QByteArray out(plain.size(), 0);
    QByteArray tag(kTagLen, 0);
    int len = 0;
    int total = 0;
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    bool ok = ctx
        && EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, key.data(), bytes(iv)) == 1;
    if (ok && !aad.isEmpty())
        ok = EVP_EncryptUpdate(ctx, nullptr, &len, bytes(aad), int(aad.size())) == 1;
    if (ok && !plain.isEmpty()) {
        ok = EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char*>(out.data()), &len,
                               bytes(plain), int(plain.size())) == 1;
        total = len;
    }
    ok = ok && EVP_EncryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(out.data()) + total, &len) == 1;
    total += len;
    ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, kTagLen, tag.data()) == 1;
    EVP_CIPHER_CTX_free(ctx);
    if (!ok)
        return {};
    out.resize(total);
    return iv + tag + out;
}

bool open(const unsigned char* key, const QByteArray& sealed, const QByteArray& aad, QByteArray* plain)
{
    if (!key || sealed.size() < kIvLen + kTagLen)
        return false;
    const QByteArray iv = sealed.left(kIvLen);
    QByteArray tag = sealed.mid(kIvLen, kTagLen);
    const QByteArray ct = sealed.mid(kIvLen + kTagLen);
    QByteArray out(ct.size(), 0);
    int len = 0;
    int total = 0;
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    bool ok = ctx && EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, key, bytes(iv)) == 1;
    if (ok && !aad.isEmpty())
        ok = EVP_DecryptUpdate(ctx, nullptr, &len, bytes(aad), int(aad.size())) == 1;
    if (ok && !ct.isEmpty()) {
        ok = EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char*>(out.data()), &len,
                               bytes(ct), int(ct.size())) == 1;
        total = len;
    }
    ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, kTagLen, tag.data()) == 1;
    ok = ok && EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(out.data()) + total, &len) == 1;
    total += len;
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) {
        wipe(out);
        return false;
    }
    out.resize(total);
    if (plain)
        *plain = out;
    return true;
}

bool open(const SecureKey& key, const QByteArray& sealed, const QByteArray& aad, QByteArray* plain)
{
    if (key.size() != kKeyLen)
        return false;
    return open(key.data(), sealed, aad, plain);
}

QByteArray sealFile(const SecureKey& key, const QByteArray& purpose, const QByteArray& plain)
{
    if (purpose.isEmpty() || purpose.size() > 255)
        return {};
    const QByteArray header = fileHeader(purpose);
    const QByteArray body = seal(key, plain, header);
    if (body.isEmpty())
        return {};
    return header + body;
}

bool isSealedFile(const QByteArray& blob)
{
    return blob.size() > kFileMagicLen + 2 && blob.startsWith(QByteArray(kFileMagic, kFileMagicLen))
        && quint8(blob.at(kFileMagicLen)) == kFileVersion;
}

bool openFile(const SecureKey& key, const QByteArray& purpose, const QByteArray& blob, QByteArray* plain)
{
    if (!isSealedFile(blob))
        return false;
    const QByteArray header = fileHeader(purpose);
    if (!blob.startsWith(header))
        return false;
    return open(key, blob.mid(header.size()), header, plain);
}

bool writeAtomic(const QString& path, const QByteArray& data)
{
    PrivatePaths::makePrivateDir(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    // The temp file gets 0600 before any byte lands in it.
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (f.write(data) != data.size()) {
        f.cancelWriting();
        return false;
    }
    if (!f.commit())
        return false;
    PrivatePaths::restrictFile(path);
    return true;
}

QByteArray readAll(const QString& path, bool* ok)
{
    QFile f(path);
    const bool opened = f.open(QIODevice::ReadOnly);
    if (ok)
        *ok = opened;
    return opened ? f.readAll() : QByteArray();
}

namespace {
QString freeName(const QString& base)
{
    if (!QFileInfo::exists(base))
        return base;
    for (int i = 1; i < 1000; ++i) {
        const QString candidate = base + QLatin1Char('.') + QString::number(i);
        if (!QFileInfo::exists(candidate))
            return candidate;
    }
    return {};
}
} // namespace

QString backupCopy(const QString& path, const QString& suffix)
{
    if (!QFileInfo(path).isFile())
        return {};
    const QString dest = freeName(path + suffix);
    if (dest.isEmpty() || !QFile::copy(path, dest))
        return {};
    PrivatePaths::restrictFile(dest);
    return dest;
}

QString backupMove(const QString& path, const QString& suffix)
{
    if (!QFileInfo(path).isFile())
        return {};
    const QString dest = freeName(path + suffix);
    if (dest.isEmpty() || !QFile::rename(path, dest))
        return {};
    PrivatePaths::restrictFile(dest);
    return dest;
}

bool secureRemove(const QString& path)
{
    const QByteArray native = QFile::encodeName(path);
    struct stat st{};
    if (::lstat(native.constData(), &st) != 0)
        return false;
    if (S_ISREG(st.st_mode)) {
        // O_NOFOLLOW: never write through a symlink planted in its place.
        const int fd = ::open(native.constData(), O_WRONLY | O_NOFOLLOW | O_CLOEXEC);
        if (fd >= 0) {
            static const char zeros[4096] = {};
            off_t left = st.st_size;
            bool ok = true;
            while (ok && left > 0) {
                const size_t n = size_t(qMin<off_t>(left, off_t(sizeof zeros)));
                const ssize_t w = ::write(fd, zeros, n);
                ok = w > 0;
                if (ok)
                    left -= w;
            }
            ::fsync(fd);
            ::close(fd);
        }
    }
    return ::unlink(native.constData()) == 0;
}

int removeBackups(const QString& path)
{
    int removed = 0;
    if (QFileInfo::exists(path) && secureRemove(path))
        ++removed;
    for (int i = 1; i < 1000; ++i) {
        const QString numbered = path + QLatin1Char('.') + QString::number(i);
        if (!QFileInfo::exists(numbered))
            continue;
        if (secureRemove(numbered))
            ++removed;
    }
    return removed;
}

void wipe(QByteArray& b)
{
    if (!b.isEmpty())
        OPENSSL_cleanse(b.data(), size_t(b.size()));
    b.clear();
}

void wipe(QString& s)
{
    if (!s.isEmpty())
        OPENSSL_cleanse(s.data(), size_t(s.size()) * sizeof(QChar));
    s.clear();
}

} // namespace CryptoBox
