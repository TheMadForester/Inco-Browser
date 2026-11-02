#include "BookmarkStore.hpp"
#include "CryptoBox.hpp"
#include "PrivatePaths.hpp"

#include <algorithm>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

QVector<Bookmark> fromJson(const QByteArray& json)
{
    QVector<Bookmark> rows;
    for (const auto& v : QJsonDocument::fromJson(json).array()) {
        const auto o = v.toObject();
        rows.push_back({o.value("title").toString(), o.value("url").toString()});
    }
    return rows;
}

QByteArray toJson(const QVector<Bookmark>& rows, QJsonDocument::JsonFormat format)
{
    QJsonArray arr;
    for (const auto& r : rows) {
        QJsonObject o;
        o["title"] = r.title;
        o["url"] = r.url;
        arr.append(o);
    }
    return QJsonDocument(arr).toJson(format);
}

} // namespace

BookmarkStore::BookmarkStore(const QString& jsonPath)
    : m_path(jsonPath),
      m_binPath(QFileInfo(jsonPath).absolutePath() + QStringLiteral("/bookmarks.bin"))
{
    PrivatePaths::makePrivateDir(QFileInfo(m_path).absolutePath());
    PrivatePaths::restrictFile(m_path);
    PrivatePaths::restrictFile(m_binPath);
}

bool BookmarkStore::isEncrypted() const
{
    return QFile::exists(m_binPath);
}

bool BookmarkStore::isLocked() const
{
    return !m_unlocked && isEncrypted();
}

QVector<Bookmark> BookmarkStore::all() const
{
    if (m_unlocked)
        return m_rows;
    if (isEncrypted())
        return {};
    return readJson();
}

bool BookmarkStore::add(const Bookmark& b)
{
    if (isLocked())
        return false;
    auto rows = all();
    for (const auto& r : rows)
        if (r.url == b.url) return true;
    rows.push_back(b);
    return write(rows);
}

bool BookmarkStore::removeUrl(const QString& url)
{
    if (isLocked())
        return false;
    auto rows = all();
    rows.erase(std::remove_if(rows.begin(), rows.end(),
        [&](const Bookmark& r) { return r.url == url; }), rows.end());
    return write(rows);
}

void BookmarkStore::unlock(const SecureKey& key, bool encrypt)
{
    m_key.assign(key);
    m_rows.clear();
    m_unlocked = false;

    if (isEncrypted()) {
        const QByteArray blob = CryptoBox::readAll(m_binPath);
        QByteArray plain;
        if (CryptoBox::openFile(m_key, kPurpose, blob, &plain)) {
            m_rows = fromJson(plain);
            // Authenticated and well formed: the plain copy kept by the
            // migration can go.
            const bool wellFormed = QJsonDocument::fromJson(plain).isArray();
            CryptoBox::wipe(plain);
            if (wellFormed)
                CryptoBox::removeBackups(m_path + QStringLiteral(".plain.bak"));
        } else {
            // Sealed with another vault (for example before an import):
            // set the file aside instead of overwriting it.
            CryptoBox::backupMove(m_binPath, QStringLiteral(".unreadable.bak"));
        }
    }
    // Plain bookmarks that are still around (first unlock, or added while
    // no vault existed) join the encrypted list.
    const bool havePlain = QFile::exists(m_path);
    if (havePlain) {
        for (const auto& b : readJson()) {
            const bool dup = std::any_of(m_rows.cbegin(), m_rows.cend(),
                                         [&](const Bookmark& r) { return r.url == b.url; });
            if (!dup)
                m_rows.push_back(b);
        }
    }
    m_unlocked = true;

    if (encrypt) {
        if (havePlain || !isEncrypted()) {
            if (writeBin(m_rows) && havePlain)
                CryptoBox::backupMove(m_path, QStringLiteral(".plain.bak"));
        }
    } else if (isEncrypted()) {
        // Opted out: back to plain JSON, the sealed file is kept as a backup.
        if (writeJson(m_rows))
            CryptoBox::backupMove(m_binPath, QStringLiteral(".bak"));
    }
}

void BookmarkStore::lock()
{
    // Plain mode keeps working without the vault (all() reads the JSON);
    // sealed mode lists nothing until the next unlock.
    m_key.clear();
    m_rows.clear();
    m_unlocked = false;
}

QVector<Bookmark> BookmarkStore::readJson() const
{
    QFile f(m_path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return fromJson(f.readAll());
}

bool BookmarkStore::writeJson(const QVector<Bookmark>& rows) const
{
    return CryptoBox::writeAtomic(m_path, toJson(rows, QJsonDocument::Indented));
}

bool BookmarkStore::writeBin(const QVector<Bookmark>& rows) const
{
    if (m_key.isEmpty())
        return false;
    QByteArray json = toJson(rows, QJsonDocument::Compact);
    const QByteArray blob = CryptoBox::sealFile(m_key, kPurpose, json);
    CryptoBox::wipe(json);
    return !blob.isEmpty() && CryptoBox::writeAtomic(m_binPath, blob);
}

bool BookmarkStore::write(const QVector<Bookmark>& rows)
{
    if (m_unlocked && isEncrypted()) {
        if (!writeBin(rows))
            return false;
        m_rows = rows;
        return true;
    }
    if (!writeJson(rows))
        return false;
    if (m_unlocked)
        m_rows = rows;
    return true;
}
