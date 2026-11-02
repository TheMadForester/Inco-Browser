#pragma once

#include "SettingsStore.hpp"
#include "UrlInterceptor.hpp"

#include <QList>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QWebEngineExtensionInfo>

class QWebEngineExtensionManager;
class QWebEngineProfile;

class BrowserProfile : public QObject
{
    Q_OBJECT

public:
    // ephemeral: anonymous / Onionize profile. Otherwise the normal profile,
    // kept on disk only when diskBacked (see wantsDisk); else it is
    // off-the-record too and cookies, site storage, visited links,
    // permissions and HSTS stay in memory.
    explicit BrowserProfile(bool ephemeral = false, bool diskBacked = false,
                            QObject* parent = nullptr);
    bool isEphemeral() const { return m_ephemeral; }
    bool isDiskBacked() const { return m_diskBacked; }
    // The normal profile needs disk storage for kept logins, and for
    // extensions (Qt installs and loads them from the profile folder).
    // Fixed at construction, so a change applies after a restart.
    static bool wantsDisk(const IncoSettings& settings);
    static QString storagePath();

    QWebEngineProfile* engine() const { return m_profile; }
    void apply(const IncoSettings& settings);
    void wipeOnExit(const IncoSettings& settings);

    // Extensions exist only on the normal, disk-backed profile. Ephemeral
    // (anonymous / Onionize) profiles never install, load or enable any.
    bool supportsExtensions() const;
    // Extensions the user installed (Qt's built-in PDF/Hangouts components
    // are left out).
    QList<QWebEngineExtensionInfo> userExtensions() const;
    // Enabled user extensions, empty while the master switch is off.
    QList<QWebEngineExtensionInfo> activeExtensions() const;
    QString extensionInstallPath() const;
    // Folder with manifest.json, or a .zip / .crx package.
    void installExtension(const QString& path);
    void removeExtension(const QString& id);

signals:
    // An extension was installed, loaded, unloaded or removed.
    void extensionsChanged();
    // Enabled state of some extension changed (master switch or row toggle).
    void extensionStateChanged();
    // Result of installExtension(); error is empty on success.
    void extensionInstallFinished(const QString& id, const QString& name, const QString& error);

private:
    QWebEngineExtensionManager* manager() const;
    void syncExtensions();
    void lockDownExtensions();

    QWebEngineProfile* m_profile = nullptr;
    UrlInterceptor* m_blocker = nullptr;
    bool m_filterOn = true;
    bool m_ephemeral = false;
    bool m_diskBacked = false;
    bool m_extensionsOn = false;
    QSet<QString> m_enabledExtensions;
    QStringList m_staging;   // extracted archives waiting for installFinished
};
