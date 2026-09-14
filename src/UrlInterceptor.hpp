#pragma once

#include <QFile>
#include <QHostAddress>
#include <QSet>
#include <QString>
#include <QUrl>
#include <QWebEngineUrlRequestInfo>
#include <QWebEngineUrlRequestInterceptor>

class UrlInterceptor final : public QWebEngineUrlRequestInterceptor
{
public:
    explicit UrlInterceptor(bool* filterEnabled, QObject* parent = nullptr)
        : QWebEngineUrlRequestInterceptor(parent), m_enabled(filterEnabled)
    {
        QFile f(QStringLiteral(":/filter-hosts.txt"));
        if (f.open(QIODevice::ReadOnly)) {
            while (!f.atEnd()) {
                QString line = QString::fromUtf8(f.readLine()).trimmed().toLower();
                if (line.isEmpty() || line.startsWith('#'))
                    continue;
                m_hosts.insert(line);
            }
        }
    }

    void setLists(const QSet<QString>& extra, const QSet<QString>& allow)
    {
        m_extra = extra;
        m_allow = allow;
    }

    void interceptRequest(QWebEngineUrlRequestInfo& info) override
    {
        const QUrl url = info.requestUrl();
        const QString host = url.host().toLower();
        if (host.isEmpty())
            return;

        if (isPrivateHost(host)) {
            info.block(true);
            return;
        }

        if (m_enabled && *m_enabled && !isAllowed(host) && isFiltered(host))
            info.block(true);
    }

private:
    static bool isPrivateHost(const QString& host)
    {
        if (host == QLatin1String("localhost") || host.endsWith(QLatin1String(".localhost")))
            return true;
        const QHostAddress addr(host);
        if (addr.isNull())
            return false;
        return addr.isLoopback() || addr.isLinkLocal() || addr.isMulticast();
    }

    bool isFiltered(const QString& host) const
    {
        QString h = host;
        while (true) {
            if (m_hosts.contains(h) || m_extra.contains(h))
                return true;
            const int dot = h.indexOf('.');
            if (dot <= 0)
                return false;
            h = h.mid(dot + 1);
        }
    }

    bool isAllowed(const QString& host) const
    {
        QString h = host;
        while (true) {
            if (m_allow.contains(h))
                return true;
            const int dot = h.indexOf('.');
            if (dot <= 0)
                return false;
            h = h.mid(dot + 1);
        }
    }

    bool* m_enabled = nullptr;
    QSet<QString> m_hosts;
    QSet<QString> m_extra;
    QSet<QString> m_allow;
};
