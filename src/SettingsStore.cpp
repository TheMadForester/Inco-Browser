#include "SettingsStore.hpp"
#include "CryptoBox.hpp"
#include "PrivatePaths.hpp"
#include "SearchEngine.hpp"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace {

const char* const kSensitiveKeys[] = {"filterExtra", "filterAllow", "searchTemplate", "searchEngine",
                                     "downloadDir"};
// No longer read. Old conf files may still carry them; loading ignores them.
const char* const kRetiredKeys[] = {"useTorForAnon", "torHost", "torPort"};

QString defaultDownloadDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
}

// Older settings only have a search address: pick the matching entry.
QString engineOrDetect(const QString& stored, const QString& searchTemplate)
{
    static const QStringList known = {QStringLiteral("ddg"), QStringLiteral("startpage"),
                                      QStringLiteral("custom")};
    if (known.contains(stored))
        return stored;
    return SearchEngine::detect(searchTemplate);
}

} // namespace

SettingsStore::SettingsStore()
    : m_settings("Inco", "IncoBrowser"),
      m_sealedPath(PrivatePaths::dataDir() + QStringLiteral("/settings.bin"))
{
}

bool SettingsStore::hasSealed() const
{
    return QFile::exists(m_sealedPath);
}

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
    s.ubolLists = m_settings.value("ubolLists", true).toBool();
    s.sensitiveLocked = hasSealed();
    if (s.sensitiveLocked) {
        // Built-in filter list, default search and folder until the vault opens.
        s.downloadDir = defaultDownloadDir();
    } else {
        s.filterExtra = m_settings.value("filterExtra").toString();
        s.filterAllow = m_settings.value("filterAllow").toString();
        s.searchTemplate = m_settings.value("searchTemplate", s.searchTemplate).toString();
        s.searchEngine = engineOrDetect(m_settings.value("searchEngine").toString(), s.searchTemplate);
        s.downloadDir = m_settings.value("downloadDir", defaultDownloadDir()).toString();
    }
    s.language = m_settings.value("language", "system").toString();
    s.lowRam = m_settings.value("lowRamMode", false).toBool();
    s.extensionsOn = m_settings.value("extensionsOn", false).toBool();
    s.enabledExtensions = m_settings.value("enabledExtensions").toStringList();
    s.lockBookmarks = m_settings.value("lockBookmarks", true).toBool();
    s.autoLockMinutes = qBound(0, m_settings.value("autoLockMinutes", 15).toInt(), 720);
    s.lockOnSleep = m_settings.value("lockOnSleep", true).toBool();
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
    m_settings.setValue("ubolLists", s.ubolLists);
    if (hasSealed() || !m_key.isEmpty()) {
        // Sealed mode: never write these into the conf, and only reseal
        // values that really came out of settings.bin.
        removeSensitiveKeys();
        if (!m_key.isEmpty() && !s.sensitiveLocked)
            writeSealed(s);
    } else {
        m_settings.setValue("filterExtra", s.filterExtra);
        m_settings.setValue("filterAllow", s.filterAllow);
        m_settings.setValue("searchTemplate", s.searchTemplate);
        m_settings.setValue("searchEngine", s.searchEngine);
        m_settings.setValue("downloadDir", s.downloadDir);
    }
    // Left over from the unused system Tor option of 0.2.x: drop on save.
    for (const char* key : kRetiredKeys)
        m_settings.remove(QLatin1String(key));
    m_settings.setValue("language", s.language);
    m_settings.setValue("lowRamMode", s.lowRam);
    m_settings.setValue("extensionsOn", s.extensionsOn);
    m_settings.setValue("enabledExtensions", s.enabledExtensions);
    m_settings.setValue("lockBookmarks", s.lockBookmarks);
    m_settings.setValue("autoLockMinutes", s.autoLockMinutes);
    m_settings.setValue("lockOnSleep", s.lockOnSleep);
    m_settings.sync();
    PrivatePaths::restrictFile(m_settings.fileName());
}

void SettingsStore::removeSensitiveKeys()
{
    for (const char* key : kSensitiveKeys)
        m_settings.remove(QLatin1String(key));
}

bool SettingsStore::writeSealed(const IncoSettings& s) const
{
    QJsonObject o;
    o["filterExtra"] = s.filterExtra;
    o["filterAllow"] = s.filterAllow;
    o["searchTemplate"] = s.searchTemplate;
    o["searchEngine"] = s.searchEngine;
    o["downloadDir"] = s.downloadDir;
    QByteArray json = QJsonDocument(o).toJson(QJsonDocument::Compact);
    const QByteArray blob = CryptoBox::sealFile(m_key, kPurpose, json);
    CryptoBox::wipe(json);
    return !blob.isEmpty() && CryptoBox::writeAtomic(m_sealedPath, blob);
}

bool SettingsStore::unlockSensitive(const SecureKey& key, IncoSettings& s)
{
    if (!m_key.assign(key))
        return false;
    if (hasSealed()) {
        QByteArray plain;
        if (CryptoBox::openFile(m_key, kPurpose, CryptoBox::readAll(m_sealedPath), &plain)) {
            const QJsonDocument doc = QJsonDocument::fromJson(plain);
            const QJsonObject o = doc.object();
            CryptoBox::wipe(plain);
            // Authenticated and well formed: the plain conf copy kept by the
            // migration can go.
            if (doc.isObject())
                CryptoBox::removeBackups(m_settings.fileName() + QStringLiteral(".plain.bak"));
            s.filterExtra = o.value("filterExtra").toString();
            s.filterAllow = o.value("filterAllow").toString();
            s.searchTemplate = o.value("searchTemplate").toString(IncoSettings().searchTemplate);
            s.searchEngine = engineOrDetect(o.value("searchEngine").toString(), s.searchTemplate);
            s.downloadDir = o.value("downloadDir").toString(defaultDownloadDir());
            if (s.downloadDir.isEmpty())
                s.downloadDir = defaultDownloadDir();
            QDir().mkpath(s.downloadDir);
            s.sensitiveLocked = false;
            return true;
        }
        // Sealed under another vault: keep the file aside, start from the
        // values of this session.
        CryptoBox::backupMove(m_sealedPath, QStringLiteral(".unreadable.bak"));
    } else {
        // First unlock with a vault: the conf still has the plain values.
        bool plainInConf = false;
        for (const char* k : kSensitiveKeys)
            plainInConf = plainInConf || m_settings.contains(QLatin1String(k));
        if (plainInConf) {
            m_settings.sync();
            CryptoBox::backupCopy(m_settings.fileName(), QStringLiteral(".plain.bak"));
        }
    }
    if (!writeSealed(s)) {
        m_key.clear();   // stay in the old mode, nothing removed from the conf
        return false;
    }
    s.sensitiveLocked = false;
    removeSensitiveKeys();
    m_settings.sync();
    PrivatePaths::restrictFile(m_settings.fileName());
    return true;
}

void SettingsStore::lockSensitive(IncoSettings& s)
{
    m_key.clear();
    if (hasSealed())
        s.sensitiveLocked = true;
}
