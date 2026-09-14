#include "BookmarkStore.hpp"
#include <algorithm>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

BookmarkStore::BookmarkStore(const QString& path) : m_path(path)
{
    QDir().mkpath(QFileInfo(m_path).absolutePath());
}

QVector<Bookmark> BookmarkStore::all() const { return read(); }

void BookmarkStore::add(const Bookmark& b)
{
    auto rows = read();
    for (const auto& r : rows)
        if (r.url == b.url) return;
    rows.push_back(b);
    write(rows);
}

void BookmarkStore::removeUrl(const QString& url)
{
    auto rows = read();
    rows.erase(std::remove_if(rows.begin(), rows.end(),
        [&](const Bookmark& r) { return r.url == url; }), rows.end());
    write(rows);
}

QVector<Bookmark> BookmarkStore::read() const
{
    QVector<Bookmark> rows;
    QFile f(m_path);
    if (!f.open(QIODevice::ReadOnly)) return rows;
    for (const auto& v : QJsonDocument::fromJson(f.readAll()).array()) {
        const auto o = v.toObject();
        rows.push_back({o.value("title").toString(), o.value("url").toString()});
    }
    return rows;
}

void BookmarkStore::write(const QVector<Bookmark>& rows) const
{
    QJsonArray arr;
    for (const auto& r : rows) {
        QJsonObject o;
        o["title"] = r.title;
        o["url"] = r.url;
        arr.append(o);
    }
    QFile f(m_path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
}
