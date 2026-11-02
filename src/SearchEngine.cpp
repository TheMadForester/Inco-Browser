#include "SearchEngine.hpp"
#include "SettingsStore.hpp"


#include <cstdlib>
#include <cstring>

namespace SearchEngine {

namespace {

// %s is not valid percent-encoding, so swap it for a token before parsing.
constexpr const char* kToken = "INCOQUERYTOKEN";

QUrl parseTemplate(const QString& searchTemplate)
{
    QString t = searchTemplate.trimmed();
    if (!t.contains(QLatin1String("%s")))
        t += QLatin1String("%s");
    t.replace(QLatin1String("%s"), QLatin1String(kToken));
    return QUrl(t, QUrl::StrictMode);
}

QString& inheritedStore()
{
    static QString value;
    return value;
}

} // namespace

QString detect(const QString& searchTemplate)
{
    const QString t = searchTemplate.trimmed();
    if (t.isEmpty())
        return QStringLiteral("ddg");
    const QUrl u = parseTemplate(t);
    if (!u.isValid() || u.scheme() != QLatin1String("https"))
        return QStringLiteral("custom");
    const QString host = u.host().toLower();
    const QString path = u.path();
    const bool plainQuery = u.query() == QStringLiteral("q=") + QLatin1String(kToken)
        && u.fragment().isEmpty() && u.userInfo().isEmpty() && u.port() == -1;
    if (!plainQuery)
        return QStringLiteral("custom");
    if ((host == QLatin1String("duckduckgo.com") || host == QLatin1String("www.duckduckgo.com"))
        && (path.isEmpty() || path == QLatin1String("/")))
        return QStringLiteral("ddg");
    if ((host == QLatin1String("startpage.com") || host == QLatin1String("www.startpage.com"))
        && (path == QLatin1String("/do/search") || path == QLatin1String("/sp/search")
            || path == QLatin1String("/search")))
        return QStringLiteral("startpage");
    return QStringLiteral("custom");
}

bool isUsableTemplate(const QString& searchTemplate)
{
    if (searchTemplate.trimmed().isEmpty())
        return false;
    const QUrl u = parseTemplate(searchTemplate);
    return u.isValid() && !u.host().isEmpty()
        && (u.scheme() == QLatin1String("https") || u.scheme() == QLatin1String("http"));
}

QString templateFor(const IncoSettings& s, bool* fellBack)
{
    if (fellBack)
        *fellBack = false;
    const QString duck = QString::fromLatin1(kDuckDuckGo);
    if (s.searchEngine == QLatin1String("startpage"))
        return QString::fromLatin1(kStartpage);
    if (s.searchEngine == QLatin1String("custom")) {
        if (isUsableTemplate(s.searchTemplate))
            return s.searchTemplate.trimmed();
        if (fellBack)
            *fellBack = true;
        return duck;
    }
    return duck;
}

QUrl urlFor(const QString& searchTemplate, const QString& query)
{
    QString t = searchTemplate.trimmed();
    if (!t.contains(QLatin1String("%s")))
        t += QLatin1String("%s");
    t.replace(QLatin1String("%s"), QString::fromUtf8(QUrl::toPercentEncoding(query)));
    return QUrl(t);
}

void takeInherited()
{
    char* raw = std::getenv(kEnvName);
    if (!raw)
        return;
    const QString value = QString::fromUtf8(raw);
    // Clear the bytes too: /proc/<pid>/environ shows the original block.
    std::memset(raw, 0, std::strlen(raw));
    ::unsetenv(kEnvName);
    if (isUsableTemplate(value))
        inheritedStore() = value.trimmed();
}

QString inherited()
{
    return inheritedStore();
}

void clearInherited()
{
    inheritedStore().clear();
}

} // namespace SearchEngine
