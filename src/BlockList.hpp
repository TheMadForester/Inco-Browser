#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QWebEngineUrlRequestInfo>

// Host rules taken from uBlock Origin Lite (resources/filter-ubol.txt, made
// by scripts/ubol-lists.py). Matching follows declarativeNetRequest as far as
// a per-host check can: the highest priority wins, allow wins a tie, and a
// rule without types matches everything except top-level pages.
class BlockList
{
public:
    using Type = QWebEngineUrlRequestInfo::ResourceType;

    // The bundled list, read once per process on first use.
    static const BlockList& ubol();

    bool load(const QString& path);
    // page: host of the page or frame that made the request, may be empty.
    bool blocks(const QString& host, const QString& page, Type type) const;
    qsizetype size() const { return m_plain.size() + m_rules.size(); }

private:
    struct Rule
    {
        quint32 types = 0;
        int priority = 10;
        bool allow = false;
        bool thirdParty = false;
        bool firstParty = false;
        QList<QByteArray> sites;
        QList<QByteArray> notSites;
    };

    bool parseLine(const QByteArray& line);
    static bool matches(const Rule& rule, const QByteArray& host, const QByteArray& page,
                        quint32 type);

    // Most rules: block at priority 10 on every page, value is the type mask.
    QHash<QByteArray, quint32> m_plain;
    QMultiHash<QByteArray, Rule> m_rules;
};
