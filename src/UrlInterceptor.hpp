#pragma once

#include "BlockList.hpp"

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

    // uBlock Origin Lite host rules, nullptr while that option is off.
    void setBlockList(const BlockList* list) { m_ubol = list; }

    // Filter decision for a request; the never-block list wins over both lists.
    bool shouldFilter(const QString& host, const QString& page,
                      QWebEngineUrlRequestInfo::ResourceType type) const
    {
        if (!m_enabled || !*m_enabled || isAllowed(host))
            return false;
        return isFiltered(host) || (m_ubol && m_ubol->blocks(host, page, type));
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

        // The page or frame behind the request, for site-specific rules.
        QString page = info.initiator().host().toLower();
        if (page.isEmpty())
            page = info.firstPartyUrl().host().toLower();
        if (shouldFilter(host, page, info.resourceType()))
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
    const BlockList* m_ubol = nullptr;
    QSet<QString> m_hosts;
    QSet<QString> m_extra;
    QSet<QString> m_allow;
};
