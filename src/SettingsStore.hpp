#pragma once

#include "SecureKey.hpp"

#include <QSettings>
#include <QString>
#include <QStringList>

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
    bool ubolLists = true;   // Inco Filter also uses the uBlock Origin Lite host rules
    // Sensitive: stored in settings.bin under the vault key once a vault exists.
    QString filterExtra;
    QString filterAllow;
    QString searchTemplate = QStringLiteral("https://duckduckgo.com/?q=%s");   // Custom address
    QString searchEngine = QStringLiteral("ddg");   // ddg, startpage, custom
    QString downloadDir;   // sensitive, see above
    QString language = QStringLiteral("system");
    bool lowRam = false;
    // Normal profile only; Onionize windows never load extensions.
    bool extensionsOn = false;
    QStringList enabledExtensions;   // extension ids the user switched on
    // Vault lock
    bool lockBookmarks = true;       // bookmarks sealed while a vault exists
    int autoLockMinutes = 15;        // 0 = never lock on idle
    bool lockOnSleep = true;         // lock on suspend and screen lock

    // Runtime only, never stored: the sensitive values above are sealed in
    // settings.bin and the vault is locked, so they hold defaults (or the
    // values of this session) and must not be edited.
    bool sensitiveLocked = false;
};

class SettingsStore
{
public:
    static constexpr const char* kPurpose = "inco/settings";

    SettingsStore();
    IncoSettings load() const;
    void save(const IncoSettings& s);

    // settings.bin exists: sensitive values live there, not in the conf.
    bool hasSealed() const;
    // With the vault open: read settings.bin into s, or on first use move the
    // sensitive conf values into it (IncoBrowser.conf.plain.bak keeps a copy
    // until a later unlock opens and verifies settings.bin).
    bool unlockSensitive(const SecureKey& key, IncoSettings& s);
    // Vault locked: forget the key; s keeps its values for this session.
    void lockSensitive(IncoSettings& s);

    QString fileName() const { return m_settings.fileName(); }
    QString sealedPath() const { return m_sealedPath; }

private:
    bool writeSealed(const IncoSettings& s) const;
    void removeSensitiveKeys();

    QSettings m_settings;
    QString m_sealedPath;
    SecureKey m_key;
};
