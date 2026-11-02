#include "BrowserProfile.hpp"
#include "ExtensionArchive.hpp"
#include "I18n.hpp"
#include "PrivatePaths.hpp"
#include <QSet>

#include <QDir>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QWebEngineCookieStore>
#include <QWebEngineExtensionManager>
#include <QWebEngineProfile>
#include <QWebEngineProfileBuilder>
#include <QWebEngineSettings>
#include <QWebEngineUrlRequestJob>
#include <QWebEngineUrlSchemeHandler>

namespace {

// Qt's own component extensions (PDF viewer, Hangouts) ship in its data dir.
bool isBuiltIn(const QWebEngineExtensionInfo& e)
{
    return !e.isInstalled()
        && e.path().startsWith(QLibraryInfo::path(QLibraryInfo::DataPath) + QLatin1Char('/'));
}

// inco:// addresses are commands for IncoPage (new-tab search, unlock,
// onionize) and never load. Chromium only routes a navigation to a scheme
// with a handler, so this one exists and refuses every request.
class RefuseIncoScheme final : public QWebEngineUrlSchemeHandler
{
public:
    using QWebEngineUrlSchemeHandler::QWebEngineUrlSchemeHandler;
    void requestStarted(QWebEngineUrlRequestJob* job) override
    {
        job->fail(QWebEngineUrlRequestJob::RequestDenied);
    }
};

} // namespace

BrowserProfile::BrowserProfile(bool ephemeral, bool diskBacked, QObject* parent)
    : QObject(parent), m_ephemeral(ephemeral), m_diskBacked(!ephemeral && diskBacked)
{
    const QString dataDir = PrivatePaths::dataDir();
    PrivatePaths::makePrivateDir(dataDir);
    // An existing profile folder from earlier runs keeps its data but gets
    // a private mode even when this run does not use it.
    PrivatePaths::restrictDir(storagePath());
    if (!m_diskBacked) {
        // Anonymous / Onionize, or a normal window without kept logins and
        // extensions: nothing of the web profile touches the disk.
        m_profile = QWebEngineProfileBuilder::createOffTheRecordProfile(this);
        m_profile->setHttpCacheType(QWebEngineProfile::MemoryHttpCache);
        m_profile->setPersistentCookiesPolicy(QWebEngineProfile::NoPersistentCookies);
        m_profile->setPersistentPermissionsPolicy(
            QWebEngineProfile::PersistentPermissionsPolicy::StoreInMemory);
    } else {
        const QString persistent = storagePath();
        // Memory cache only; the path points at per-user tmpfs, never /tmp.
        const QString runtime = PrivatePaths::runtimeDir();
        const QString cache = runtime.isEmpty() ? QString() : runtime + QStringLiteral("/cache");
        PrivatePaths::makePrivateDir(persistent);
        if (!cache.isEmpty())
            PrivatePaths::makePrivateDir(cache);
        // Paths go in before the profile exists: moving the storage of a live
        // profile leaves its extension service on stale prefs (crashes on
        // the first enable/disable).
        QWebEngineProfileBuilder builder;
        builder.setPersistentStoragePath(persistent);
        if (!cache.isEmpty())
            builder.setCachePath(cache);
        builder.setHttpCacheType(QWebEngineProfile::MemoryHttpCache);
        // Site permissions are asked again after a restart instead of
        // landing in profile/permissions.json.
        builder.setPersistentPermissionsPolicy(
            QWebEngineProfile::PersistentPermissionsPolicy::StoreInMemory);
        m_profile = builder.createProfile("inco", this);
    }
    m_profile->settings()->setAttribute(QWebEngineSettings::AutoLoadIconsForPage, true);
    m_profile->settings()->setAttribute(QWebEngineSettings::TouchIconsEnabled, true);
    m_profile->installUrlSchemeHandler("inco", new RefuseIncoScheme(m_profile));
    m_blocker = new UrlInterceptor(&m_filterOn, m_profile);
    m_profile->setUrlRequestInterceptor(m_blocker);
    if (m_ephemeral) {
        m_profile->setHttpUserAgent(
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
            "(KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36");
        m_profile->setHttpAcceptLanguage("en-US,en;q=0.9");
    } else {
        // Some sites (DuckDuckGo among them) hand the old no-JS page to
        // anything that says QtWebEngine, so leave that part out.
        QString ua = m_profile->httpUserAgent();
        ua.remove(QRegularExpression(QStringLiteral(" QtWebEngine/[^ ]+")));
        m_profile->setHttpUserAgent(ua);
    }
    m_profile->settings()->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, false);
    // WebRTC may only use the default public route, so pages never learn LAN
    // addresses. Onionize windows also force non-proxied UDP off in main.cpp.
    m_profile->settings()->setAttribute(QWebEngineSettings::WebRTCPublicInterfacesOnly, true);

    // Qt hands out a manager even for off-the-record profiles. Anything that
    // shows up there that is not a Qt component gets unloaded at once.
    if (auto* m = m_profile->extensionManager()) {
        if (supportsExtensions()) {
            connect(m, &QWebEngineExtensionManager::loadFinished, this, [this] {
                syncExtensions();
                emit extensionsChanged();
            });
            connect(m, &QWebEngineExtensionManager::installFinished, this,
                    [this](const QWebEngineExtensionInfo& e) {
                if (!m_staging.isEmpty())
                    QDir(m_staging.takeFirst()).removeRecursively();
                syncExtensions();
                emit extensionInstallFinished(e.id(), e.name(),
                                              e.isInstalled() ? QString() : e.error());
                emit extensionsChanged();
            });
            for (auto sig : {&QWebEngineExtensionManager::unloadFinished,
                             &QWebEngineExtensionManager::uninstallFinished})
                connect(m, sig, this, [this] { emit extensionsChanged(); });
        } else {
            for (auto sig : {&QWebEngineExtensionManager::loadFinished,
                             &QWebEngineExtensionManager::installFinished})
                connect(m, sig, this, [this] { lockDownExtensions(); });
            lockDownExtensions();
        }
    }
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
        // Parsed on first use, so turning the option off also skips the load.
        m_blocker->setBlockList(settings.incoFilter && settings.ubolLists ? &BlockList::ubol()
                                                                          : nullptr);
    }

    if (!m_diskBacked) {
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

    // Low RAM: cap the in-memory HTTP cache (0 = let Chromium pick) and skip
    // touch icons, which are large images fetched for every site.
    m_profile->setHttpCacheMaximumSize(settings.lowRam ? 8 * 1024 * 1024 : 0);
    m_profile->settings()->setAttribute(QWebEngineSettings::TouchIconsEnabled, !settings.lowRam);

    m_extensionsOn = settings.extensionsOn && supportsExtensions();
    m_enabledExtensions = QSet<QString>(settings.enabledExtensions.cbegin(),
                                        settings.enabledExtensions.cend());
    syncExtensions();
}

void BrowserProfile::wipeOnExit(const IncoSettings& settings)
{
    if (settings.clearCookiesOnExit)
        m_profile->cookieStore()->deleteAllCookies();
    if (settings.clearCacheOnExit)
        m_profile->clearHttpCache();
    // Chromium keeps visited links on disk for named profiles; Inco keeps
    // no history, so they go every time.
    m_profile->clearAllVisitedLinks();
}

bool BrowserProfile::wantsDisk(const IncoSettings& settings)
{
    return settings.keepLogins || settings.extensionsOn;
}

QString BrowserProfile::storagePath()
{
    return PrivatePaths::dataDir() + QStringLiteral("/profile");
}

bool BrowserProfile::supportsExtensions() const
{
    return !m_ephemeral && m_diskBacked && m_profile && !m_profile->isOffTheRecord();
}

QWebEngineExtensionManager* BrowserProfile::manager() const
{
    return supportsExtensions() ? m_profile->extensionManager() : nullptr;
}

QList<QWebEngineExtensionInfo> BrowserProfile::userExtensions() const
{
    QList<QWebEngineExtensionInfo> out;
    if (auto* m = manager()) {
        for (const auto& e : m->extensions()) {
            if (e.isInstalled() && !isBuiltIn(e))
                out << e;
        }
    }
    return out;
}

QList<QWebEngineExtensionInfo> BrowserProfile::activeExtensions() const
{
    QList<QWebEngineExtensionInfo> out;
    if (!m_extensionsOn)
        return out;
    for (const auto& e : userExtensions()) {
        if (e.isLoaded() && e.isEnabled())
            out << e;
    }
    return out;
}

QString BrowserProfile::extensionInstallPath() const
{
    auto* m = manager();
    return m ? m->installPath() : QString();
}

void BrowserProfile::installExtension(const QString& path)
{
    auto* m = manager();
    if (!m)
        return;
    const QFileInfo info(path);
    if (info.isDir()) {
        if (!QFileInfo::exists(info.absoluteFilePath() + QStringLiteral("/manifest.json"))) {
            emit extensionInstallFinished({}, info.fileName(), I18n::tr("ext.err.manifest"));
            return;
        }
        m->installExtension(info.absoluteFilePath());
        return;
    }

    // Qt names the installed copy after the source folder, so keep the
    // package name in the staging folder.
    QString base = info.completeBaseName();
    base.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")), QStringLiteral("_"));
    if (base.isEmpty())
        base = QStringLiteral("extension");
    QTemporaryDir staging(QDir::tempPath() + QStringLiteral("/") + base + QStringLiteral("-XXXXXX"));
    if (!staging.isValid()) {
        emit extensionInstallFinished({}, info.fileName(), I18n::tr("ext.err.write"));
        return;
    }
    const auto result = ExtensionArchive::extract(info.absoluteFilePath(), staging.path());
    if (result != ExtensionArchive::Result::Ok) {
        emit extensionInstallFinished({}, info.fileName(),
            result == ExtensionArchive::Result::WriteFailed ? I18n::tr("ext.err.write")
                                                            : I18n::tr("ext.err.archive"));
        return;
    }
    const QString root = ExtensionArchive::manifestRoot(staging.path());
    if (root.isEmpty()) {
        emit extensionInstallFinished({}, info.fileName(), I18n::tr("ext.err.manifest"));
        return;
    }
    staging.setAutoRemove(false);   // removed once Qt reports installFinished
    m_staging << staging.path();
    m->installExtension(root);
}

void BrowserProfile::removeExtension(const QString& id)
{
    auto* m = manager();
    if (!m)
        return;
    for (const auto& e : userExtensions()) {
        if (e.id() == id) {
            m->uninstallExtension(e);
            return;
        }
    }
}

// Installed extensions come back disabled on every start; this turns on
// exactly the ones the user enabled, and only while the master switch is on.
void BrowserProfile::syncExtensions()
{
    auto* m = manager();
    if (!m) {
        lockDownExtensions();
        return;
    }
    bool changed = false;
    for (const auto& e : userExtensions()) {
        if (!e.isLoaded())
            continue;
        const bool want = m_extensionsOn && m_enabledExtensions.contains(e.id());
        if (e.isEnabled() != want) {
            m->setExtensionEnabled(e, want);
            changed = true;
        }
    }
    if (changed)
        emit extensionStateChanged();
}

void BrowserProfile::lockDownExtensions()
{
    if (supportsExtensions() || !m_profile)
        return;
    auto* m = m_profile->extensionManager();
    if (!m)
        return;
    for (const auto& e : m->extensions()) {
        if (!isBuiltIn(e) && e.isLoaded()) {
            if (e.isEnabled())
                m->setExtensionEnabled(e, false);
            m->unloadExtension(e);
        }
    }
}
