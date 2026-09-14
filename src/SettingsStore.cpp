#include "SettingsStore.hpp"
#include <QDir>
#include <QStandardPaths>

SettingsStore::SettingsStore() : m_settings("Inco", "IncoBrowser") {}

IncoSettings SettingsStore::load() const
{
    IncoSettings s;
    s.savePasswords = m_settings.value("savePasswords", true).toBool();
    s.bookmarksEnabled = m_settings.value("bookmarksEnabled", false).toBool();
    s.showBookmarksOnHome = m_settings.value("showBookmarksOnHome", true).toBool();
    s.clearCookiesOnExit = m_settings.value("clearCookiesOnExit", true).toBool();
    s.clearCacheOnExit = m_settings.value("clearCacheOnExit", true).toBool();
    s.blockPopups = m_settings.value("blockPopups", true).toBool();
    s.keepLogins = m_settings.value("keepLogins", false).toBool();
    s.autofillLogins = m_settings.value("autofillLogins", true).toBool();
    s.httpsOnly = m_settings.value("httpsOnly", false).toBool();
    s.incoFilter = m_settings.value("incoFilter", true).toBool();
    s.filterExtra = m_settings.value("filterExtra").toString();
    s.filterAllow = m_settings.value("filterAllow").toString();
    s.searchTemplate = m_settings.value("searchTemplate", s.searchTemplate).toString();
    s.useTorForAnon = m_settings.value("useTorForAnon", false).toBool();
    s.torHost = m_settings.value("torHost", s.torHost).toString();
    s.torPort = m_settings.value("torPort", 9050).toInt();
    s.downloadDir = m_settings.value(
        "downloadDir",
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)).toString();
    QDir().mkpath(s.downloadDir);
    if (s.keepLogins)
        s.clearCookiesOnExit = false;
    return s;
}

void SettingsStore::save(const IncoSettings& s)
{
    m_settings.setValue("savePasswords", s.savePasswords);
    m_settings.setValue("bookmarksEnabled", s.bookmarksEnabled);
    m_settings.setValue("showBookmarksOnHome", s.showBookmarksOnHome);
    m_settings.setValue("clearCookiesOnExit", s.clearCookiesOnExit);
    m_settings.setValue("clearCacheOnExit", s.clearCacheOnExit);
    m_settings.setValue("blockPopups", s.blockPopups);
    m_settings.setValue("keepLogins", s.keepLogins);
    m_settings.setValue("autofillLogins", s.autofillLogins);
    m_settings.setValue("httpsOnly", s.httpsOnly);
    m_settings.setValue("incoFilter", s.incoFilter);
    m_settings.setValue("filterExtra", s.filterExtra);
    m_settings.setValue("filterAllow", s.filterAllow);
    m_settings.setValue("searchTemplate", s.searchTemplate);
    m_settings.setValue("useTorForAnon", s.useTorForAnon);
    m_settings.setValue("torHost", s.torHost);
    m_settings.setValue("torPort", s.torPort);
    m_settings.setValue("downloadDir", s.downloadDir);
}
