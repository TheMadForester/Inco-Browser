#pragma once
#include <QString>
#include <QVector>

struct Bookmark {
    QString title;
    QString url;
};

class BookmarkStore {
public:
    explicit BookmarkStore(const QString& path);
    QVector<Bookmark> all() const;
    void add(const Bookmark& b);
    void removeUrl(const QString& url);
private:
    QString m_path;
    QVector<Bookmark> read() const;
    void write(const QVector<Bookmark>& rows) const;
};
