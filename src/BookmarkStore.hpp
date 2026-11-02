#pragma once
#include "SecureKey.hpp"

#include <QString>
#include <QVector>

struct Bookmark {
    QString title;
    QString url;
};

// Plain bookmarks.json, or bookmarks.bin sealed with the vault subkey
// "inco/bookmarks". While bookmarks.bin exists and no key is set, the store
// is locked: it lists nothing and refuses changes, so nothing is lost.
class BookmarkStore {
public:
    static constexpr const char* kPurpose = "inco/bookmarks";

    explicit BookmarkStore(const QString& jsonPath);
    QVector<Bookmark> all() const;
    // False while locked (nothing changed).
    bool add(const Bookmark& b);
    bool removeUrl(const QString& url);

    bool isEncrypted() const;
    bool isLocked() const;
    // Called after the vault unlocks. encrypt = keep bookmarks under the
    // vault key: plain JSON moves into bookmarks.bin (a copy of the JSON is
    // kept as bookmarks.json.plain.bak until a later unlock opens and
    // verifies bookmarks.bin). encrypt = false moves them back.
    void unlock(const SecureKey& key, bool encrypt);
    void lock();

    QString jsonPath() const { return m_path; }
    QString binPath() const { return m_binPath; }

private:
    QVector<Bookmark> readJson() const;
    bool writeJson(const QVector<Bookmark>& rows) const;
    bool writeBin(const QVector<Bookmark>& rows) const;
    bool write(const QVector<Bookmark>& rows);

    QString m_path;
    QString m_binPath;
    SecureKey m_key;
    QVector<Bookmark> m_rows;   // decrypted rows while unlocked
    bool m_unlocked = false;
};
