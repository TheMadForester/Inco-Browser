#pragma once

#include <QSettings>
#include <QString>

struct IncoSettings
{
    bool savePasswords = true;
    bool bookmarksEnabled = false;
    bool showBookmarksOnHome = true;
    bool clearCookiesOnExit = true;
    bool clearCacheOnExit = true;
    bool blockPopups = true;
    bool keepLogins = false;
    bool autofillLogins = true;
    bool httpsOnly = false;
    bool incoFilter = true;
    QString filterExtra;
    QString filterAllow;
    QString searchTemplate = QStringLiteral("https://duckduckgo.com/?q=%s");
    bool useTorForAnon = false;
    QString torHost = QStringLiteral("127.0.0.1");
    int torPort = 9050;
    QString downloadDir;
    QString language = QStringLiteral("system");
};

class SettingsStore
{
public:
    SettingsStore();
    IncoSettings load() const;
    void save(const IncoSettings& s);
private:
    QSettings m_settings;
};
