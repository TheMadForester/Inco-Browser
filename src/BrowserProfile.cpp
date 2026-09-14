#include "BrowserProfile.hpp"
#include <QSet>

#include <QDir>
#include <QStandardPaths>
#include <QWebEngineCookieStore>
#include <QWebEngineProfile>
#include <QWebEngineSettings>

BrowserProfile::BrowserProfile(bool ephemeral, QObject* parent)
    : QObject(parent), m_ephemeral(ephemeral)
{
    if (ephemeral) {
        m_profile = new QWebEngineProfile(this); // off-the-record
        m_profile->setHttpCacheType(QWebEngineProfile::MemoryHttpCache);
        m_profile->setPersistentCookiesPolicy(QWebEngineProfile::NoPersistentCookies);
    } else {
        const QString persistent =
            QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
            + "/profile";
        const QString cache =
            QStandardPaths::writableLocation(QStandardPaths::TempLocation)
            + "/inco-browser-cache";
        QDir().mkpath(persistent);
        m_profile = new QWebEngineProfile("inco", this);
        m_profile->setPersistentStoragePath(persistent);
        m_profile->setCachePath(cache);
        m_profile->setHttpCacheType(QWebEngineProfile::MemoryHttpCache);
    }
    m_profile->settings()->setAttribute(QWebEngineSettings::AutoLoadIconsForPage, true);
    m_profile->settings()->setAttribute(QWebEngineSettings::TouchIconsEnabled, true);
    m_blocker = new UrlInterceptor(&m_filterOn, m_profile);
    m_profile->setUrlRequestInterceptor(m_blocker);
    if (m_ephemeral) {
        m_profile->setHttpUserAgent(
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
            "(KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36");
        m_profile->setHttpAcceptLanguage("en-US,en;q=0.9");
    }
    m_profile->settings()->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, false);
    if (m_ephemeral)
        m_profile->settings()->setAttribute(QWebEngineSettings::WebRTCPublicInterfacesOnly, true);
    m_profile->settings()->setAttribute(QWebEngineSettings::WebRTCPublicInterfacesOnly, false);
}

void BrowserProfile::apply(const IncoSettings& settings)
{
    m_filterOn = settings.incoFilter;

    if (m_blocker) {
        QSet<QString> extra;
        QSet<QString> allow;
        for (QString line : settings.filterExtra.split('\n')) {
            line = line.trimmed().toLower();
            if (!line.isEmpty() && !line.startsWith('#'))
                extra.insert(line);
        }
        for (QString line : settings.filterAllow.split('\n')) {
            line = line.trimmed().toLower();
            if (!line.isEmpty() && !line.startsWith('#'))
                allow.insert(line);
        }
        m_blocker->setLists(extra, allow);
    }

    if (m_ephemeral) {
        m_profile->setPersistentCookiesPolicy(QWebEngineProfile::NoPersistentCookies);
    } else {
    m_profile->setPersistentCookiesPolicy(
        (settings.keepLogins && !settings.clearCookiesOnExit)
            ? QWebEngineProfile::AllowPersistentCookies
            : QWebEngineProfile::NoPersistentCookies
    );
    }
    m_profile->settings()->setAttribute(
        QWebEngineSettings::JavascriptCanOpenWindows, !settings.blockPopups
    );
}

void BrowserProfile::wipeOnExit(const IncoSettings& settings)
{
    if (settings.clearCookiesOnExit)
        m_profile->cookieStore()->deleteAllCookies();
    if (settings.clearCacheOnExit)
        m_profile->clearHttpCache();
}
