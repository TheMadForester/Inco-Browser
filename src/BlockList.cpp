#include "BlockList.hpp"

#include <QFile>

#include <algorithm>
#include <iterator>

namespace {

// Bit order follows the declarativeNetRequest type names.
const char* const kTypeNames[] = {
    "main_frame", "sub_frame", "stylesheet", "script", "image", "font", "object",
    "xmlhttprequest", "ping", "csp_report", "media", "websocket", "webtransport",
    "webbundle", "other",
};
constexpr quint32 kMainFrame = 1u << 0;
constexpr quint32 kAllTypes = (1u << std::size(kTypeNames)) - 1;
constexpr quint32 kDefaultTypes = kAllTypes & ~kMainFrame;

quint32 typeMask(const QByteArray& list)
{
    quint32 mask = 0;
    for (const QByteArray& name : list.split(',')) {
        for (size_t i = 0; i < std::size(kTypeNames); ++i) {
            if (name == kTypeNames[i]) {
                mask |= 1u << i;
                break;
            }
        }
    }
    return mask;
}

quint32 typeBit(BlockList::Type type)
{
    using Info = QWebEngineUrlRequestInfo;
    switch (type) {
    case Info::ResourceTypeMainFrame:
    case Info::ResourceTypeNavigationPreloadMainFrame:
        return kMainFrame;
    case Info::ResourceTypeSubFrame:
    case Info::ResourceTypeNavigationPreloadSubFrame:
        return 1u << 1;
    case Info::ResourceTypeStylesheet:
        return 1u << 2;
    case Info::ResourceTypeScript:
    case Info::ResourceTypeWorker:
    case Info::ResourceTypeSharedWorker:
    case Info::ResourceTypeServiceWorker:
        return 1u << 3;
    case Info::ResourceTypeImage:
    case Info::ResourceTypeFavicon:
        return 1u << 4;
    case Info::ResourceTypeFontResource:
        return 1u << 5;
    case Info::ResourceTypeObject:
    case Info::ResourceTypePluginResource:
        return 1u << 6;
    case Info::ResourceTypeXhr:
        return 1u << 7;
    case Info::ResourceTypePing:
        return 1u << 8;
    case Info::ResourceTypeCspReport:
        return 1u << 9;
    case Info::ResourceTypeMedia:
        return 1u << 10;
    case Info::ResourceTypeWebSocket:
        return 1u << 11;
    default:
        return 1u << 14;   // other
    }
}

bool isUnder(const QByteArray& host, const QByteArray& domain)
{
    if (!host.endsWith(domain))
        return false;
    return host.size() == domain.size() || host.at(host.size() - domain.size() - 1) == '.';
}

bool underAny(const QByteArray& host, const QList<QByteArray>& domains)
{
    for (const QByteArray& d : domains)
        if (isUnder(host, d))
            return true;
    return false;
}

// Rough registrable domain without a public suffix list: the last two
// labels, or three for names like example.co.uk.
QByteArray siteOf(const QByteArray& host)
{
    const int last = host.lastIndexOf('.');
    if (last <= 0)
        return host;
    const int second = host.lastIndexOf('.', last - 1);
    if (second < 0)
        return host;
    const QByteArray middle = host.mid(second + 1, last - second - 1);
    const bool shortSuffix = host.size() - last - 1 == 2 && middle.size() <= 3;
    if (!shortSuffix)
        return host.mid(second + 1);
    const int third = host.lastIndexOf('.', second - 1);
    return third < 0 ? host : host.mid(third + 1);
}

} // namespace

const BlockList& BlockList::ubol()
{
    static const BlockList list = [] {
        BlockList l;
        l.load(QStringLiteral(":/filter-ubol.txt"));
        return l;
    }();
    return list;
}

bool BlockList::load(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray data = f.readAll();
    m_plain.reserve(data.count('\n'));
    qsizetype start = 0;
    while (start < data.size()) {
        qsizetype end = data.indexOf('\n', start);
        if (end < 0)
            end = data.size();
        const QByteArray line = data.mid(start, end - start).trimmed();
        if (!line.isEmpty() && !line.startsWith('#'))
            parseLine(line);
        start = end + 1;
    }
    return true;
}

bool BlockList::parseLine(const QByteArray& line)
{
    const QList<QByteArray> parts = line.split(' ');
    Rule rule;
    rule.types = kDefaultTypes;
    QByteArray host = parts.first();
    if (host.startsWith("@@")) {
        rule.allow = true;
        host = host.mid(2);
    }
    if (host.isEmpty())
        return false;
    bool plain = !rule.allow;
    for (qsizetype i = 1; i < parts.size(); ++i) {
        const QByteArray& opt = parts.at(i);
        const int eq = opt.indexOf('=');
        const QByteArray key = eq < 0 ? opt : opt.left(eq);
        const QByteArray value = eq < 0 ? QByteArray() : opt.mid(eq + 1);
        if (key == "types") {
            rule.types = typeMask(value);
        } else if (key == "not-types") {
            rule.types = kAllTypes & ~typeMask(value);
        } else if (key == "sites") {
            rule.sites = value.split('|');
            plain = false;
        } else if (key == "not-sites") {
            rule.notSites = value.split('|');
            plain = false;
        } else if (key == "third-party") {
            rule.thirdParty = true;
            plain = false;
        } else if (key == "first-party") {
            rule.firstParty = true;
            plain = false;
        } else if (key == "priority") {
            rule.priority = value.toInt();
            plain = plain && rule.priority == 10;
        } else {
            return false;   // unknown option: skip rather than guess
        }
    }
    if (plain)
        m_plain[host] |= rule.types;
    else
        m_rules.insert(host, rule);
    return true;
}

bool BlockList::matches(const Rule& rule, const QByteArray& host, const QByteArray& page,
                        quint32 type)
{
    if (!(rule.types & type))
        return false;
    if (!rule.sites.isEmpty() && (page.isEmpty() || !underAny(page, rule.sites)))
        return false;
    if (!rule.notSites.isEmpty() && !page.isEmpty() && underAny(page, rule.notSites))
        return false;
    if (rule.thirdParty || rule.firstParty) {
        const bool third = !page.isEmpty() && siteOf(host) != siteOf(page);
        if (third != rule.thirdParty)
            return false;
    }
    return true;
}

bool BlockList::blocks(const QString& host, const QString& page, Type type) const
{
    const QByteArray h = host.toLatin1();
    const QByteArray p = page.toLatin1();
    const quint32 bit = typeBit(type);
    int block = -1;
    int allow = -1;

    // Walk example.com's parents: a.b.example.com, b.example.com, example.com.
    qsizetype from = 0;
    while (from < h.size()) {
        const QByteArray key = QByteArray::fromRawData(h.constData() + from, h.size() - from);
        if (const auto it = m_plain.constFind(key); it != m_plain.cend() && (*it & bit))
            block = std::max(block, 10);
        for (auto it = m_rules.constFind(key); it != m_rules.cend() && it.key() == key; ++it) {
            if (!matches(*it, h, p, bit))
                continue;
            int& best = it->allow ? allow : block;
            best = std::max(best, it->priority);
        }
        const qsizetype dot = h.indexOf('.', from);
        if (dot < 0)
            break;
        from = dot + 1;
    }
    return block > allow;
}
