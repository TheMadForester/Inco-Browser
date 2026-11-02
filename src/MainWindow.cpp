#include "MainWindow.hpp"
#include "CryptoBox.hpp"
#include "I18n.hpp"
#include "PrivatePaths.hpp"
#include "SearchEngine.hpp"
#include <QColor>
#include "SettingsPage.hpp"
#include "TorIntegrity.hpp"
#include "Motion.hpp"
#include "OnionRoute.hpp"
#include "TorControl.hpp"
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScrollArea>
#include <QCoreApplication>
#include <QWebEngineContextMenuRequest>
#include <QMenu>
#include <QGuiApplication>
#include <QClipboard>
#include <QDateTime>
#include <QTimer>
#include <cstdio>

#ifdef INCO_HAVE_DBUS
#include <QDBusConnection>
#endif

#include <QAuthenticator>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QApplication>
#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QStandardPaths>
#include <QTabWidget>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWebEngineDownloadRequest>
#include <QWebEngineExtensionInfo>
#include <QWebEngineHistory>
#include <QWebEngineNewWindowRequest>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineView>

// Encode any string as a JavaScript string literal (quotes included).
// JSON string syntax is valid JS, so quotes, backslashes, newlines and
// U+2028/U+2029 can never break out of the literal.
static QString jsString(const QString& s)
{
    const QByteArray arr = QJsonDocument(QJsonArray{s}).toJson(QJsonDocument::Compact);
    QString lit = QString::fromUtf8(arr.mid(1, arr.size() - 2));
    lit.replace(QChar(0x2028), QLatin1String("\\u2028"));
    lit.replace(QChar(0x2029), QLatin1String("\\u2029"));
    return lit;
}


// The q value of a form GET to inco://search. Forms encode a space as "+",
// which QUrlQuery leaves alone, so decode it here.
static QString formQuery(const QUrl& url)
{
    const QStringList pairs = url.query(QUrl::FullyEncoded).split(QLatin1Char('&'));
    for (const QString& pair : pairs) {
        if (!pair.startsWith(QLatin1String("q=")))
            continue;
        QString value = pair.mid(2);
        value.replace(QLatin1Char('+'), QLatin1Char(' '));
        return QUrl::fromPercentEncoding(value.toUtf8()).trimmed();
    }
    return {};
}

class IncoPage final : public QWebEnginePage
{
public:
    explicit IncoPage(QWebEngineProfile* profile, MainWindow* window, QObject* parent)
        : QWebEnginePage(profile, parent), m_window(window) {}
protected:
    QWebEnginePage* createWindow(QWebEnginePage::WebWindowType) override
    {
        return m_window->createTabView()->page();
    }
    bool acceptNavigationRequest(const QUrl& url, NavigationType, bool isMainFrame) override
    {
        if (url.scheme() == QLatin1String("inco")) {
            // Only Inco's own pages may use these; a website gets nothing.
            if (!isMainFrame || this->url().scheme() != QLatin1String("qrc"))
                return false;
        }
        if (url.scheme() == QLatin1String("inco") && url.host() == QLatin1String("search")) {
            // New-tab search box. The query never goes back into the page,
            // it only becomes the search engine URL.
            const QString query = formQuery(url);
            if (!query.isEmpty()) {
                const QUrl target = m_window->searchUrl(query);
                QTimer::singleShot(0, this, [this, target] { setUrl(target); });
            }
            return false;
        }
        if (url.scheme() == QLatin1String("inco") && url.host() == QLatin1String("onionize")) {
            m_window->onionizeCurrentTab();
            return false;
        }
        if (url.scheme() == QLatin1String("inco") && url.host() == QLatin1String("unlock")) {
            // Home page link "Unlock to see bookmarks"; let the click finish first.
            QTimer::singleShot(0, m_window, [w = m_window] { w->unlockFromPage(); });
            return false;
        }
        if (url.scheme() == QLatin1String("inco"))
            return false;   // unknown inco:// address: nothing to load
        if (m_window->httpsOnly() && url.scheme() == QLatin1String("http")) {
            QUrl u = url;
            u.setScheme("https");
            setUrl(u);
            return false;
        }
        return true;
    }
private:
    MainWindow* m_window;
};

MainWindow::MainWindow(bool anonymous, bool tor, QWidget* parent)
    : QMainWindow(parent),
      m_settings(m_settingsStore.load()),
      m_profile(anonymous, BrowserProfile::wantsDisk(m_settings)),
      m_anonProfile(true),
      m_passwords(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/vault.bin",
                  m_settings.lowRam),
      m_bookmarks(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/bookmarks.json"),
      m_anonymous(anonymous),
      m_torProcess(tor)
{
    setWindowTitle(anonymous ? I18n::tr("app.anon") : I18n::tr("app.name"));
    setWindowIcon(QIcon(":/inco-browser.png"));
    resize(1280, 800);
    PrivatePaths::restrictFile(m_settingsStore.fileName());
    m_profile.apply(m_settings);
    m_anonProfile.apply(m_settings);
    applyDarkTheme();
    setupUi();
    setupShortcuts();
    if (m_anonymous) {
        if (m_passwordsButton) m_passwordsButton->hide();
        if (m_bookmarkButton) m_bookmarkButton->hide();
        if (m_bookmarkListButton) m_bookmarkListButton->hide();
        auto* banner = new QLabel(
            m_torProcess ? I18n::tr("anon.banner.tor") : I18n::tr("anon.banner"), this);
        banner->setObjectName("anonBanner");
        banner->setWordWrap(true);
        banner->setStyleSheet("background:#1a1510; color:#e8d8b0; padding:10px 14px; border-bottom:1px solid #2a2010; font-size:12px;");
        if (auto* lay = qobject_cast<QVBoxLayout*>(centralWidget()->layout()))
            lay->insertWidget(0, banner);
    }
    connect(m_profile.engine(), &QWebEngineProfile::downloadRequested,
            this, &MainWindow::handleDownload);
    connect(m_anonProfile.engine(), &QWebEngineProfile::downloadRequested,
            this, &MainWindow::handleDownload);
    // Extension UI lives only in normal windows (m_profile is off-the-record
    // in Onionize windows, which BrowserProfile also refuses to extend).
    if (!m_anonymous && !m_torProcess && m_profile.supportsExtensions()) {
        connect(&m_profile, &BrowserProfile::extensionsChanged, this, &MainWindow::updateExtensionButton);
        connect(&m_profile, &BrowserProfile::extensionStateChanged, this, &MainWindow::updateExtensionButton);
    }
    addTab(m_anonymous || m_torProcess ? QUrl(QStringLiteral("qrc:/newtab.html#onion"))
                         : QUrl(QStringLiteral("qrc:/newtab.html")));
    if (m_torProcess)
        checkTorStatus();
    applyLowRam();
    updateExtensionButton();
    if (!m_anonymous)
        setupAutoLock();
}

MainWindow::~MainWindow()
{
    qApp->removeEventFilter(this);
    m_settingsStore.save(m_settings);
    lockAll(false);
    m_profile.wipeOnExit(m_settings);
    // Same reason as the tabs below: the popup page must go first.
    delete m_extPopup.data();

    // Delete every page while its profile still exists. Member profiles are
    // destroyed before QWidget deletes child widgets, which used to trigger
    // "Release of profile requested but WebEnginePage still not deleted".
    if (m_lifecycleTimer)
        m_lifecycleTimer->stop();
    if (m_tabs) {
        disconnect(m_tabs, nullptr, this, nullptr);
        for (auto* view : m_tabs->findChildren<QWebEngineView*>()) {
            disconnect(view, nullptr, this, nullptr);
            if (auto* page = view->page()) {
                disconnect(page, nullptr, this, nullptr);
                view->setPage(nullptr);
                delete page;
            }
        }
        delete m_tabs;
        m_tabs = nullptr;
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    m_settingsStore.save(m_settings);
    lockAll(false);
    QMainWindow::closeEvent(event);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    switch (event->type()) {
    case QEvent::KeyPress:
    case QEvent::MouseButtonPress:
    case QEvent::MouseMove:
    case QEvent::Wheel:
    case QEvent::TouchBegin:
        // Any input counts as activity; restart the timer at most once a second.
        if (m_idleTimer && m_idleTimer->isActive()
            && (!m_idleKick.isValid() || m_idleKick.elapsed() > 1000)) {
            m_idleKick.restart();
            m_idleTimer->start();
        }
        break;
    default:
        break;
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::setupAutoLock()
{
    m_idleTimer = new QTimer(this);
    m_idleTimer->setSingleShot(true);
    connect(m_idleTimer, &QTimer::timeout, this, [this] { lockAll(true); });
    qApp->installEventFilter(this);
#ifdef INCO_HAVE_DBUS
    // Suspend (logind), screen lock (freedesktop / GNOME screensaver) and
    // "loginctl lock-session". Missing services simply never fire.
    auto system = QDBusConnection::systemBus();
    if (system.isConnected()) {
        system.connect(QStringLiteral("org.freedesktop.login1"), QStringLiteral("/org/freedesktop/login1"),
                       QStringLiteral("org.freedesktop.login1.Manager"), QStringLiteral("PrepareForSleep"),
                       this, SLOT(onPrepareForSleep(bool)));
        system.connect(QStringLiteral("org.freedesktop.login1"), QString(),
                       QStringLiteral("org.freedesktop.login1.Session"), QStringLiteral("Lock"),
                       this, SLOT(onSessionLock()));
    }
    auto session = QDBusConnection::sessionBus();
    if (session.isConnected()) {
        session.connect(QString(), QStringLiteral("/org/freedesktop/ScreenSaver"),
                        QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("ActiveChanged"),
                        this, SLOT(onScreenLockChanged(bool)));
        session.connect(QString(), QStringLiteral("/ScreenSaver"),
                        QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("ActiveChanged"),
                        this, SLOT(onScreenLockChanged(bool)));
        session.connect(QString(), QStringLiteral("/org/gnome/ScreenSaver"),
                        QStringLiteral("org.gnome.ScreenSaver"), QStringLiteral("ActiveChanged"),
                        this, SLOT(onScreenLockChanged(bool)));
    }
#endif
}

void MainWindow::resetIdleTimer()
{
    if (!m_idleTimer)
        return;
    if (m_passwords.isUnlocked() && m_settings.autoLockMinutes > 0) {
        m_idleTimer->setInterval(m_settings.autoLockMinutes * 60 * 1000);
        m_idleTimer->start();
        m_idleKick.restart();
    } else {
        m_idleTimer->stop();
    }
}

void MainWindow::onPrepareForSleep(bool start)
{
    if (start && m_settings.lockOnSleep)
        lockAll(true);
}

void MainWindow::onScreenLockChanged(bool active)
{
    if (active && m_settings.lockOnSleep)
        lockAll(true);
}

void MainWindow::onSessionLock()
{
    if (m_settings.lockOnSleep)
        lockAll(true);
}

void MainWindow::onVaultUnlocked()
{
    if (m_anonymous)
        return;   // Onionize windows never read or migrate bookmarks or settings
    bool rebuild = false;
    SecureKey key;
    if (m_passwords.subkey(SettingsStore::kPurpose, key)) {
        const bool wasLocked = m_settings.sensitiveLocked;
        if (m_settingsStore.unlockSensitive(key, m_settings)) {
            m_profile.apply(m_settings);
            m_anonProfile.apply(m_settings);
            rebuild = wasLocked;
        }
    }
    if (m_passwords.subkey(BookmarkStore::kPurpose, key))
        m_bookmarks.unlock(key, m_settings.lockBookmarks);
    key.clear();
    updateBookmarkButtons();
    refreshHomePages();
    resetIdleTimer();
    if (rebuild)
        rebuildSettingsTabIfOpen();
}

void MainWindow::lockAll(bool updateUi)
{
    const bool wasUnlocked = m_passwords.isUnlocked();
    const bool hadSensitive = !m_settings.sensitiveLocked;
    m_passwords.lock();
    m_bookmarks.lock();
    m_settingsStore.lockSensitive(m_settings);
    if (m_idleTimer)
        m_idleTimer->stop();
    if (!updateUi || !wasUnlocked)
        return;
    // The open vault list must not stay on screen after a lock.
    if (auto* dlg = findChild<QDialog*>(QStringLiteral("vaultDialog")))
        dlg->reject();
    updateBookmarkButtons();
    refreshHomePages();
    if (hadSensitive && m_settings.sensitiveLocked)
        rebuildSettingsTabIfOpen();
}

bool MainWindow::bookmarksReady()
{
    if (!m_bookmarks.isLocked())
        return true;
    return ensureVaultUnlocked() && !m_bookmarks.isLocked();
}

bool MainWindow::unlockVault()
{
    return ensureVaultUnlocked();
}

void MainWindow::unlockFromPage()
{
    if (m_anonymous)
        return;
    if (ensureVaultUnlocked())
        refreshHomePages();
}

void MainWindow::applyVaultSettings()
{
    if (m_anonymous)
        return;
    if (m_passwords.isUnlocked()) {
        SecureKey key;
        if (m_passwords.subkey(BookmarkStore::kPurpose, key))
            m_bookmarks.unlock(key, m_settings.lockBookmarks);
    }
    resetIdleTimer();
    updateBookmarkButtons();
    refreshHomePages();
}

void MainWindow::refreshHomePages()
{
    if (!m_tabs)
        return;
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (auto* view = qobject_cast<QWebEngineView*>(m_tabs->widget(i)))
            fillHomeBookmarks(view);
    }
}

void MainWindow::rebuildSettingsTabIfOpen()
{
    if (!m_tabs)
        return;
    for (int i = 0; i < m_tabs->count(); ++i) {
        QWidget* w = m_tabs->widget(i);
        if (w && w->objectName() == QLatin1String("settingsPage")) {
            QWidget* focus = m_tabs->currentWidget();
            m_tabs->removeTab(i);
            w->deleteLater();
            showSettings();   // appends the new page and selects it
            m_tabs->tabBar()->moveTab(m_tabs->currentIndex(), i);
            if (focus != w)
                m_tabs->setCurrentWidget(focus);
            return;
        }
    }
}

void MainWindow::setupUi()
{
    m_addressBar = new QLineEdit(this);
    m_backButton = new QPushButton("←", this);
    m_forwardButton = new QPushButton("→", this);
    m_reloadButton = new QPushButton("⟳", this);
    m_newTabButton = new QPushButton("+", this);
    m_bookmarkButton = new QPushButton("☆", this);
    m_bookmarkListButton = new QPushButton("☰", this);
    m_passwordsButton = new QPushButton("⚿", this);
    m_settingsButton = new QPushButton("⚙", this);
    m_extButton = new QPushButton(this);
    m_muteButton = new QPushButton(this);
    m_onionButton = new QPushButton(I18n::tr("onionize"), this);
    m_zoomLabel = new QLabel("100%", this);
    m_tabs = new QTabWidget(this);

    const auto iconize = [](QPushButton* b, const char* path, const QString& tip) {
        b->setText({});
        b->setIcon(QIcon(QString::fromUtf8(path)));
        b->setIconSize(QSize(18, 18));
        b->setFixedSize(36, 36);
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::NoFocus);
        b->setToolTip(tip);
        b->setFlat(true);
    };
    iconize(m_backButton, ":/icons/back.svg", I18n::tr("back"));
    iconize(m_forwardButton, ":/icons/forward.svg", I18n::tr("forward"));
    iconize(m_reloadButton, ":/icons/reload.svg", I18n::tr("reload"));
    iconize(m_newTabButton, ":/icons/plus.svg", I18n::tr("newtab"));
    iconize(m_bookmarkButton, ":/icons/star.svg", I18n::tr("bookmark.page"));
    iconize(m_bookmarkListButton, ":/icons/bookmarks.svg", I18n::tr("bookmarks"));
    iconize(m_passwordsButton, ":/icons/key.svg", I18n::tr("settings.passwords"));
    iconize(m_settingsButton, ":/icons/settings.svg", I18n::tr("settings"));
    iconize(m_extButton, ":/icons/puzzle.svg", I18n::tr("ext.button"));
    iconize(m_muteButton, ":/icons/plus.svg", I18n::tr("mute"));
    m_zoomLabel->setFixedWidth(44);
    m_zoomLabel->setAlignment(Qt::AlignCenter);
    m_zoomLabel->setStyleSheet("color:#9a9a9a; font-size:12px;");


    m_bookmarkButton->setToolTip(I18n::tr("bookmark.page"));
    m_bookmarkListButton->setToolTip(I18n::tr("bookmarks"));
    m_bookmarkButton->setVisible(m_settings.bookmarksEnabled);
    m_bookmarkListButton->setVisible(m_settings.bookmarksEnabled);

    auto* toolbar = new QToolBar(this);
    toolbar->setMovable(false);
    toolbar->addWidget(m_backButton);
    toolbar->addWidget(m_forwardButton);
    toolbar->addWidget(m_reloadButton);
    toolbar->addWidget(m_newTabButton);
    m_addressBar->setPlaceholderText(I18n::tr("search.placeholder"));
    toolbar->addWidget(m_addressBar);
    toolbar->addWidget(m_onionButton);
    toolbar->addWidget(m_zoomLabel);
    toolbar->addWidget(m_muteButton);
    toolbar->addWidget(m_bookmarkButton);
    toolbar->addWidget(m_bookmarkListButton);
    toolbar->addWidget(m_passwordsButton);
    m_extAction = toolbar->addWidget(m_extButton);
    m_extAction->setVisible(false);
    toolbar->addWidget(m_settingsButton);
    addToolBar(toolbar);

    m_findBar = new QWidget(this);
    m_findBar->setObjectName("findBar");
    auto* findLayout = new QHBoxLayout(m_findBar);
    findLayout->setContentsMargins(12, 8, 12, 8);
    findLayout->setSpacing(8);
    m_findInput = new QLineEdit(m_findBar);
    m_findInput->setPlaceholderText(I18n::tr("find.placeholder"));
    auto* findNextBtn = new QPushButton(I18n::tr("find.next"), m_findBar);
    findNextBtn->setObjectName("findNextBtn");
    findNextBtn->setCursor(Qt::PointingHandCursor);
    auto* findPrevBtn = new QPushButton(I18n::tr("find.prev"), m_findBar);
    findPrevBtn->setObjectName("findPrevBtn");
    findPrevBtn->setCursor(Qt::PointingHandCursor);
    auto* findCloseBtn = new QPushButton("✕", m_findBar);
    findCloseBtn->setCursor(Qt::PointingHandCursor);
    findCloseBtn->setFixedWidth(36);
    auto* findLabel = new QLabel(I18n::tr("find"), m_findBar);
    findLabel->setObjectName("findLabel");
    findLayout->addWidget(findLabel);
    findLayout->addWidget(m_findInput);
    findLayout->addWidget(findPrevBtn);
    findLayout->addWidget(findNextBtn);
    findLayout->addWidget(findCloseBtn);
    m_findBar->hide();

    m_tabs->setTabsClosable(true);
    m_tabs->setDocumentMode(true);
    m_tabs->setMovable(true);
    m_tabs->setIconSize(QSize(16, 16));
    m_tabs->setDocumentMode(true);
    m_tabs->setFocusPolicy(Qt::NoFocus);
    if (auto* bar = m_tabs->tabBar()) {
        bar->setExpanding(false);
        bar->setDocumentMode(true);
        bar->setDrawBase(false);
        bar->setFocusPolicy(Qt::NoFocus);
        bar->setElideMode(Qt::ElideRight);
    }

    m_downloadBar = new QWidget(this);
    m_downloadBar->setObjectName("downloadBar");
    auto* dlLayout = new QVBoxLayout(m_downloadBar);
    dlLayout->setContentsMargins(12, 10, 12, 10);
    dlLayout->setSpacing(8);
    auto* head = new QHBoxLayout;
    auto* dlTitle = new QLabel(I18n::tr("downloads"), m_downloadBar);
    dlTitle->setObjectName("dlTitle");
    head->addWidget(dlTitle);
    auto* dlOpen = new QPushButton(I18n::tr("open.folder"), m_downloadBar);
    dlOpen->setObjectName("dlOpen");
    dlOpen->setCursor(Qt::PointingHandCursor);
    auto* dlHide = new QPushButton(I18n::tr("hide"), m_downloadBar);
    dlHide->setObjectName("dlHide");
    dlHide->setCursor(Qt::PointingHandCursor);
    head->addStretch();
    head->addWidget(dlOpen);
    head->addWidget(dlHide);
    m_downloadList = new QListWidget(m_downloadBar);
    m_downloadList->setMaximumHeight(110);
    dlLayout->addLayout(head);
    dlLayout->addWidget(m_downloadList);
    m_downloadBar->hide();

    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(m_findBar);
    m_torLabel = new QLabel;
    m_torLabel->setWordWrap(true);
    m_torLabel->setStyleSheet("color:#e6c07b;padding:8px 14px;background:#1a1510;border-bottom:1px solid #2a2010;font-size:12px;font-weight:600;");
    m_torLabel->setVisible(m_anonymous || m_torProcess);
    root->addWidget(m_torLabel);
    root->addWidget(m_tabs, 1);
    root->addWidget(m_downloadBar);
    setCentralWidget(central);

    connect(m_backButton, &QPushButton::clicked, this, [this] { if (auto* v = currentView()) v->back(); });
    connect(m_forwardButton, &QPushButton::clicked, this, [this] { if (auto* v = currentView()) v->forward(); });
    connect(m_reloadButton, &QPushButton::clicked, this, [this] { if (auto* v = currentView()) v->reload(); });
    connect(m_newTabButton, &QPushButton::clicked, this, [this] { if (m_torProcess || m_anonymous) checkTorStatus();
    addTab(QUrl((m_anonymous || m_torProcess) ? QStringLiteral("qrc:/newtab.html#onion") : QStringLiteral("qrc:/newtab.html"))); });
    connect(m_bookmarkButton, &QPushButton::clicked, this, &MainWindow::toggleBookmark);
    connect(m_bookmarkListButton, &QPushButton::clicked, this, &MainWindow::showBookmarks);
    connect(m_passwordsButton, &QPushButton::clicked, this, &MainWindow::showPasswordVault);
    connect(m_settingsButton, &QPushButton::clicked, this, &MainWindow::showSettings);
    connect(m_extButton, &QPushButton::clicked, this, &MainWindow::showExtensionMenu);
    m_muteButton->setText({});
    m_muteButton->setIcon(QIcon(":/icons/speaker.svg"));
    m_muteButton->setIconSize(QSize(18, 18));
    m_muteButton->setFixedSize(36, 36);
    m_muteButton->setToolTip(I18n::tr("mute"));
    m_muteButton->setFlat(true);
    if (m_zoomLabel) {
        m_zoomLabel->setFixedWidth(44);
        m_zoomLabel->setAlignment(Qt::AlignCenter);
    }
    m_onionButton->setObjectName("onionBtn");
    m_onionButton->setCheckable(true);
    m_onionButton->setCursor(Qt::PointingHandCursor);
    m_onionButton->setToolTip(I18n::tr("onion.tip"));
    m_onionButton->setChecked(m_anonymous);
    if (m_anonymous)
        m_onionButton->setText(m_torProcess ? I18n::tr("onion.on") : I18n::tr("anon"));
    if (m_anonymous || m_torProcess) {
        m_onionButton->setChecked(true);
        m_onionButton->setText(I18n::tr("onion.on"));
    }
    if (m_anonymous || m_torProcess)
        m_onionButton->setToolTip(I18n::tr("onion.route.tip"));
    connect(m_onionButton, &QPushButton::clicked, this, [this] {
        if (m_anonymous || m_torProcess) {
            // This window is the Onionize window: show the route instead of
            // closing straight away. The panel has the "turn off" button.
            m_onionButton->setChecked(true);
            showOnionRoute();
            return;
        }
        m_onionButton->setChecked(false);
        onionizeCurrentTab();
    });
    connect(m_muteButton, &QPushButton::clicked, this, &MainWindow::toggleMute);
    connect(m_addressBar, &QLineEdit::returnPressed, this, &MainWindow::navigate);
    connect(m_tabs, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTab);
    connect(m_findInput, &QLineEdit::returnPressed, this, &MainWindow::findNext);
    connect(m_findInput, &QLineEdit::textChanged, this, &MainWindow::reloadFind);
    connect(findNextBtn, &QPushButton::clicked, this, &MainWindow::findNext);
    connect(findPrevBtn, &QPushButton::clicked, this, &MainWindow::findPrev);
    connect(findCloseBtn, &QPushButton::clicked, this, &MainWindow::hideFindBar);
    connect(dlOpen, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_settings.downloadDir));
    });
    connect(dlHide, &QPushButton::clicked, this, [this] { Motion::slideClose(m_downloadBar); });
}

void MainWindow::setupShortcuts()
{
    auto add = [this](const QKeySequence& keys, auto slot) {
        auto* s = new QShortcut(keys, this);
        connect(s, &QShortcut::activated, this, slot);
    };
    add(QKeySequence("Ctrl+T"), [this] { addTab(); });
    add(QKeySequence("Ctrl+W"), [this] { closeCurrentTab(); });
    add(QKeySequence("Ctrl+L"), [this] { m_addressBar->setFocus(); m_addressBar->selectAll(); });
    add(QKeySequence("Ctrl+R"), [this] { if (auto* v = currentView()) v->reload(); });
    add(QKeySequence("F5"), [this] { if (auto* v = currentView()) v->reload(); });
    add(QKeySequence("Ctrl+F"), [this] { showFindBar(); });
    add(QKeySequence(Qt::Key_Escape), [this] { hideFindBar(); });
    add(QKeySequence("Alt+Left"), [this] { if (auto* v = currentView()) v->back(); });
    add(QKeySequence("Alt+Right"), [this] { if (auto* v = currentView()) v->forward(); });
    add(QKeySequence("Ctrl+Tab"), [this] {
        if (m_tabs->count())
            m_tabs->setCurrentIndex((m_tabs->currentIndex() + 1) % m_tabs->count());
    });
    add(QKeySequence("Ctrl+Shift+Tab"), [this] {
        if (!m_tabs->count()) return;
        const int i = m_tabs->currentIndex() - 1;
        m_tabs->setCurrentIndex(i < 0 ? m_tabs->count() - 1 : i);
    });
    add(QKeySequence("Ctrl+Q"), [this] { close(); });
    add(QKeySequence("Ctrl+D"), [this] { if (m_settings.bookmarksEnabled) toggleBookmark(); });
    add(QKeySequence("Ctrl+Shift+B"), [this] { if (m_settings.bookmarksEnabled) showBookmarks(); });
    add(QKeySequence("Ctrl++"), [this] { zoomBy(0.1); });
    add(QKeySequence("Ctrl+="), [this] { zoomBy(0.1); });
    add(QKeySequence("Ctrl+-"), [this] { zoomBy(-0.1); });
    add(QKeySequence("Ctrl+0"), [this] { resetZoom(); });
    add(QKeySequence("Ctrl+Shift+N"), [this] { openAnonymousSession(); });
    add(QKeySequence("Ctrl+P"), [this] {
        if (auto* v = currentView())
            v->page()->printToPdf(m_settings.downloadDir + "/inco-print.pdf");
    });
}

QWebEngineView* MainWindow::createTabView()
{
    auto* view = new QWebEngineView(m_tabs);
    view->setPage(new IncoPage(m_profile.engine(), this, view));
    view->page()->setBackgroundColor(QColor("#0e0e0e"));
    view->setStyleSheet("background:#0e0e0e;");
    view->page()->setAudioMuted(false);
    wireView(view);
    m_tabs->setCurrentIndex(m_tabs->addTab(view, QIcon(":/inco-browser.png"), I18n::tr("newtab")));
    return view;
}

void MainWindow::addTab(const QUrl& url) { createTabView()->setUrl(url); }

void MainWindow::setTabIcon(QWebEngineView* view, const QIcon& icon)
{
    const int i = m_tabs->indexOf(view);
    if (i >= 0)
        m_tabs->setTabIcon(i, icon.isNull() ? QIcon(":/inco-browser.png") : icon);
}

void MainWindow::wireView(QWebEngineView* view)
{
    connect(view, &QWebEngineView::titleChanged, this, [this, view](const QString& title) {
        const int i = m_tabs->indexOf(view);
        if (title.startsWith(QLatin1String("inco-onion-toggle"))) {
            if (!m_onionBusy)
                onionizeCurrentTab();
            return;
        }
        if (i >= 0) {
            QString text = title.isEmpty() ? I18n::tr("newtab") : title.left(24);
            if (isOnionTab(view) && !text.startsWith(QLatin1String("◐ ")))
                text = QStringLiteral("◐ ") + text;
            m_tabs->setTabText(i, text);
        }
        if (view == currentView())
            updateWindowTitle(title);
    });
    connect(view, &QWebEngineView::urlChanged, this, [this, view](const QUrl& url) {
        if (url.toString().contains(QLatin1String("onionize.html"))) {
            onionizeCurrentTab();
            return;
        }
        if (view == currentView()) {
            updateAddressBar(url);
            m_backButton->setEnabled(view->history()->canGoBack());
            m_forwardButton->setEnabled(view->history()->canGoForward());
            updateBookmarkButtons();
        }
    });
    connect(view, &QWebEngineView::iconChanged, this, [this, view](const QIcon& icon) {
        if (view->url().scheme() == QLatin1String("qrc"))
            setTabIcon(view, QIcon(":/inco-browser.png"));
        else
            setTabIcon(view, icon);
    });
    connect(view, &QWebEngineView::loadFinished, this, [this, view](bool ok) {
        if (ok && view->url().scheme() == QLatin1String("qrc")) {
            const QString lang = I18n::currentLang();
            view->page()->runJavaScript(
                QStringLiteral("if (window.incoApplyLang) window.incoApplyLang('%1');").arg(lang)
            );
        }

        if (view->url().scheme() == QLatin1String("qrc"))
            setTabIcon(view, QIcon(":/inco-browser.png"));
        else
            setTabIcon(view, view->icon());
        if (ok) {
            paintSearchPage(view);
            fillHomeBookmarks(view);
            tryAutofill(view);
        }
    });
    view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(view, &QWidget::customContextMenuRequested, this, [this, view](const QPoint& pos) {
        auto* menu = view->createStandardContextMenu();
        if (auto* req = view->lastContextMenuRequest()) {
            const QUrl link = req->linkUrl();
            if (link.isValid() && !link.isEmpty()) {
                menu->insertAction(menu->actions().value(0),
                    menu->addAction(I18n::tr("open.link"), this, [this, link] { addTab(link); }));
                menu->insertAction(menu->actions().value(1),
                    menu->addAction(I18n::tr("copy.link"), this, [link] {
                        QGuiApplication::clipboard()->setText(link.toString());
                    }));
            }
        }
        menu->addAction(I18n::tr("back"), this, [view] { view->back(); });
        menu->addAction(I18n::tr("reload"), this, [view] { view->reload(); });
        menu->exec(view->mapToGlobal(pos));
        menu->deleteLater();
    });

    connect(view->page(), &QWebEnginePage::authenticationRequired,
            this, [this](const QUrl& reqUrl, QAuthenticator* authenticator) {
        if (!m_settings.savePasswords || !ensureVaultUnlocked())
            return;
        QDialog dialog(this);
        dialog.setWindowTitle(I18n::tr("sign.in"));
        auto* form = new QFormLayout(&dialog);
        auto* user = new QLineEdit(&dialog);
        auto* pass = new QLineEdit(&dialog);
        pass->setEchoMode(QLineEdit::Password);
        auto* ok = new QPushButton(I18n::tr("log.in"), &dialog);
        form->addRow(I18n::tr("site"), new QLabel(reqUrl.host(), &dialog));
        form->addRow(I18n::tr("username"), user);
        form->addRow(I18n::tr("password"), pass);
        form->addRow(ok);
        connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
        if (dialog.exec() != QDialog::Accepted)
            return;
        authenticator->setUser(user->text());
        authenticator->setPassword(pass->text());
        if (!user->text().isEmpty())
            m_passwords.upsert({reqUrl.host(), user->text(), pass->text()});
    });
}


void MainWindow::paintSearchPage(QWebEngineView* view)
{
    const QString host = view->url().host();
    if (!host.contains(QLatin1String("duckduckgo.com")))
        return;

    view->page()->runJavaScript(QStringLiteral(R"JS(
      (function(){
        if (document.getElementById('inco-dark')) return;
        const s = document.createElement('style');
        s.id = 'inco-dark';
        s.textContent = `
          html, body, #content-wrap, .site-wrapper, .header, .header-wrap,
          .results, .serp__results, #links, .footer {
            background: #0e0e0e !important;
            color: #d6d6d6 !important;
          }
          a, .result__a, .result__url { color: #c9c9c9 !important; }
          a:hover { color: #fff !important; }
          .result, .results_links, .web-result, .result__body,
          .zci, .zci__main, .module {
            background: #151515 !important;
            border: 1px solid #222222 !important;
            border-radius: 12px !important;
            color: #cfcfcf !important;
          }
          .result__snippet, .result__extras { color: #9a9a9a !important; }
          input, select, textarea {
            background: #101010 !important;
            color: #eee !important;
            border: 1px solid #2a2a2a !important;
            border-radius: 10px !important;
          }
          header, .header--aside, nav { background: #0e0e0e !important; }
          img[src*="logo"], .logo { opacity: .85; filter: grayscale(1) brightness(1.4); }
        `;
        document.documentElement.appendChild(s);
      })();
    )JS"));
}

void MainWindow::tryAutofill(QWebEngineView* view)
{
    if (!m_settings.savePasswords || !m_settings.autofillLogins)
        return;
    if (!m_passwords.isUnlocked())
        return;
    const QString host = view->url().host();
    if (host.isEmpty())
        return;
    QVector<SavedLogin> matches;
    for (const auto& row : m_passwords.all()) {
        if (row.host.compare(host, Qt::CaseInsensitive) == 0)
            matches.push_back(row);
    }
    if (matches.size() != 1)
        return;
    const auto row = matches.first();
    const QString js = QStringLiteral(
        "(function(){"
        "const u=document.querySelector('input[type=email],input[autocomplete=username],input[name*=user i],input[type=text]');"
        "const p=document.querySelector('input[type=password]');"
        "if(u){u.value=%1;u.dispatchEvent(new Event('input',{bubbles:true}));}"
        "if(p){p.value=%2;p.dispatchEvent(new Event('input',{bubbles:true}));}"
        "})();"
    ).arg(jsString(row.user), jsString(row.password));
    view->page()->runJavaScript(js);
}


void MainWindow::closeTab(int index)
{
    if (index < 0 || index >= m_tabs->count())
        return;
    QWidget* w = m_tabs->widget(index);
    m_tabs->removeTab(index);
    if (auto* view = qobject_cast<QWebEngineView*>(w)) {
        if (auto* page = view->page()) {
            view->setPage(nullptr);
            page->deleteLater();
        }
        view->deleteLater();
    } else if (w) {
        w->deleteLater();
    }
    if (m_tabs->count() == 0)
        addTab(QUrl((m_anonymous || m_torProcess) ? QStringLiteral("qrc:/newtab.html#onion") : QStringLiteral("qrc:/newtab.html")));
}


void MainWindow::closeCurrentTab() { closeTab(m_tabs->currentIndex()); }

void MainWindow::onTabChanged(int)
{
    auto* view = currentView();
    if (!view) return;
    view->setProperty("incoHiddenSince", QVariant());
    if (view->page()->lifecycleState() != QWebEnginePage::LifecycleState::Active)
        view->page()->setLifecycleState(QWebEnginePage::LifecycleState::Active);
    updateAddressBar(view->url());
    updateWindowTitle(view->title());
    m_backButton->setEnabled(view->history()->canGoBack());
    m_forwardButton->setEnabled(view->history()->canGoForward());
    updateBookmarkButtons();
    updateZoomLabel();
    if (m_onionButton) {
        // The whole window is onionized, not single tabs.
        const bool on = m_anonymous || m_torProcess || isOnionTab(view);
        m_onionButton->setChecked(on);
        m_onionButton->setText(!on ? I18n::tr("onionize")
                               : (m_anonymous && !m_torProcess) ? I18n::tr("anon")
                               : I18n::tr("onion.on"));
    }
}

QWebEngineView* MainWindow::currentView() const
{
    return qobject_cast<QWebEngineView*>(m_tabs->currentWidget());
}

QString MainWindow::searchTemplate() const
{
    // An Onionize process starts without the vault; it follows the window
    // that opened it.
    if (m_anonymous && !SearchEngine::inherited().isEmpty())
        return SearchEngine::inherited();
    return SearchEngine::templateFor(m_settings);
}

QUrl MainWindow::searchUrl(const QString& query) const
{
    QUrl url = SearchEngine::urlFor(searchTemplate(), query);
    if (!url.isValid() || url.host().isEmpty())
        url = SearchEngine::urlFor(QString::fromLatin1(SearchEngine::kDuckDuckGo), query);
    return url;
}

void MainWindow::navigate()
{
    auto* view = currentView();
    if (!view) return;
    const QString input = m_addressBar->text().trimmed();
    if (input.isEmpty()) return;
    QUrl url = QUrl::fromUserInput(input);
    const bool ok = url.isValid() &&
        (url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("https"));
    if (!ok)
        url = searchUrl(input);
    if (m_settings.httpsOnly && url.scheme() == QLatin1String("http"))
        url.setScheme(QLatin1String("https"));
    if (!url.isValid() || url.isEmpty() || url.scheme() == QLatin1String("about"))
        view->setUrl(QUrl((m_anonymous || m_torProcess)
            ? QStringLiteral("qrc:/newtab.html#onion")
            : QStringLiteral("qrc:/newtab.html")));
    else
        view->setUrl(url);
}

void MainWindow::updateBookmarkButtons()
{
    m_bookmarkButton->setVisible(m_settings.bookmarksEnabled);
    m_bookmarkListButton->setVisible(m_settings.bookmarksEnabled);
    if (!m_settings.bookmarksEnabled)
        return;
    auto* v = currentView();
    const QString url = v ? v->url().toString() : QString();
    bool starred = false;
    for (const auto& b : m_bookmarks.all())
        if (b.url == url) { starred = true; break; }
    m_bookmarkButton->setIcon(QIcon(starred ? QStringLiteral(":/icons/star-fill.svg") : QStringLiteral(":/icons/star.svg")));
}

void MainWindow::toggleBookmark()
{
    auto* view = currentView();
    if (!view)
        return;
    if (!m_settings.bookmarksEnabled) {
        QMessageBox::information(this, I18n::tr("bookmarks"), I18n::tr("bookmarks.off"));
        return;
    }
    if (!bookmarksReady())
        return;
    const QUrl url = view->url();
    if (!url.isValid() || url.scheme() == QLatin1String("qrc"))
        return;
    bool have = false;
    for (const auto& b : m_bookmarks.all()) {
        if (b.url == url.toString() || b.url == url)
            have = true;
    }
    if (have)
        m_bookmarks.removeUrl(url.toString());
    else {
        Bookmark bm;
        bm.url = url.toString();
        bm.title = view->title();
        m_bookmarks.add(bm);
    }
    updateBookmarkButtons();
    fillHomeBookmarks(view);
}


void MainWindow::showBookmarks()
{
    if (!m_settings.bookmarksEnabled) {
        QMessageBox::information(this, I18n::tr("bookmarks"), I18n::tr("bookmarks.off"));
        return;
    }
    if (!bookmarksReady())
        return;

    auto* dlg = new QDialog(this);
    dlg->setWindowTitle(I18n::tr("bookmarks"));
    dlg->resize(480, 440);
    auto* root = new QVBoxLayout(dlg);
    root->setContentsMargins(18, 18, 18, 18);
    root->setSpacing(10);
    auto* heading = new QLabel(I18n::tr("bookmarks"), dlg);
    heading->setObjectName("dialogTitle");
    root->addWidget(heading);

    auto* list = new QListWidget;
    list->setObjectName("dialogList");
    for (const auto& b : m_bookmarks.all()) {
        auto* it = new QListWidgetItem(b.title.isEmpty() ? b.url : b.title);
        it->setData(Qt::UserRole, b.url);
        list->addItem(it);
    }

    auto* empty = new QLabel(I18n::tr("bm.empty"), dlg);
    empty->setObjectName("dialogHint");
    empty->setVisible(list->count() == 0);

    auto* title = new QLineEdit;
    title->setPlaceholderText(I18n::tr("bm.title"));
    auto* url = new QLineEdit;
    url->setPlaceholderText("https://");
    auto* add = new QPushButton(I18n::tr("add"));
    auto* row = new QHBoxLayout;
    row->setSpacing(8);
    row->addWidget(title);
    row->addWidget(url, 1);
    row->addWidget(add);

    auto* open = new QPushButton(I18n::tr("open"));
    auto* del = new QPushButton(I18n::tr("delete"));
    del->setObjectName("dangerBtn");
    auto* close = new QPushButton(I18n::tr("close"));
    for (auto* b : {add, open, del, close})
        b->setCursor(Qt::PointingHandCursor);
    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    buttons->addWidget(open);
    buttons->addWidget(del);
    buttons->addStretch();
    buttons->addWidget(close);

    root->addWidget(list, 1);
    root->addWidget(empty);
    root->addLayout(row);
    root->addLayout(buttons);

    QObject::connect(add, &QPushButton::clicked, dlg, [=, this] {
        const QUrl u(url->text().trimmed());
        if (!u.isValid() || u.scheme().isEmpty())
            return;
        Bookmark bm;
        bm.url = u.toString();
        bm.title = title->text().trimmed();
        if (bm.title.isEmpty())
            bm.title = u.host();
        m_bookmarks.add(bm);
        auto* it = new QListWidgetItem(bm.title);
        it->setData(Qt::UserRole, bm.url);
        list->addItem(it);
        empty->hide();
        title->clear();
        url->clear();
        if (auto* v = currentView())
            fillHomeBookmarks(v);
    });
    QObject::connect(open, &QPushButton::clicked, dlg, [=, this] {
        auto* it = list->currentItem();
        if (!it) return;
        addTab(QUrl(it->data(Qt::UserRole).toString()));
        dlg->accept();
    });
    QObject::connect(del, &QPushButton::clicked, dlg, [=, this] {
        auto* it = list->currentItem();
        if (!it) return;
        m_bookmarks.removeUrl(it->data(Qt::UserRole).toString());
        delete it;
        empty->setVisible(list->count() == 0);
        if (auto* v = currentView())
            fillHomeBookmarks(v);
    });
    QObject::connect(close, &QPushButton::clicked, dlg, &QDialog::accept);
    dlg->exec();
    dlg->deleteLater();
}


void MainWindow::lockVault()
{
    lockAll(true);
    QMessageBox::information(this, I18n::tr("vault"), I18n::tr("vault.locked"));
}

void MainWindow::changeMasterPassword()
{
    if (!ensureVaultUnlocked())
        return;
    const QString title = I18n::tr("vault.change.btn");
    bool ok = false;
    const QString oldPw = QInputDialog::getText(this, title, I18n::tr("vault.current"),
                                                QLineEdit::Password, {}, &ok);
    if (!ok) return;
    const QString a = QInputDialog::getText(this, title, I18n::tr("vault.new"),
                                            QLineEdit::Password, {}, &ok);
    if (!ok) return;
    if (a.size() < 8) {
        QMessageBox::warning(this, I18n::tr("vault"), I18n::tr("vault.short"));
        return;
    }
    const QString b = QInputDialog::getText(this, title, I18n::tr("vault.repeat"),
                                            QLineEdit::Password, {}, &ok);
    if (!ok) return;
    if (a != b) {
        QMessageBox::warning(this, I18n::tr("vault"), I18n::tr("vault.mismatch"));
        return;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool changed = m_passwords.changeMasterPassword(oldPw, a);
    QApplication::restoreOverrideCursor();
    if (!changed)
        QMessageBox::warning(this, I18n::tr("vault"), I18n::tr("vault.change.fail"));
    else
        QMessageBox::information(this, I18n::tr("vault"), I18n::tr("vault.change.ok"));
}

void MainWindow::exportVault()
{
    const QString dest = QFileDialog::getSaveFileName(this, I18n::tr("vault.export.title"), "vault.bin");
    if (dest.isEmpty()) return;
    if (!QFile::copy(m_passwords.path(), dest) && !(QFile::remove(dest) && QFile::copy(m_passwords.path(), dest)))
        QMessageBox::warning(this, I18n::tr("vault"), I18n::tr("vault.export.fail"));
    else
        QMessageBox::information(this, I18n::tr("vault"), I18n::tr("vault.export.ok"));
}

void MainWindow::importVault()
{
    const QString src = QFileDialog::getOpenFileName(this, I18n::tr("vault.import.title"));
    if (src.isEmpty()) return;
    if (!PasswordStore::looksLikeVault(CryptoBox::readAll(src).left(8))) {
        QMessageBox::warning(this, I18n::tr("vault"), I18n::tr("vault.import.badfile"));
        return;
    }
    const bool had = m_passwords.exists();
    if (had &&
        QMessageBox::question(this, I18n::tr("vault.import.title"), I18n::tr("vault.import.confirm"))
            != QMessageBox::Yes)
        return;
    // Sealed bookmarks and settings belong to the current vault's key: open
    // them now and seal them again under the imported vault below.
    const bool sealed = !m_anonymous && (m_bookmarks.isEncrypted() || m_settingsStore.hasSealed());
    if (had && sealed && !ensureVaultUnlocked())
        return;
    const QVector<Bookmark> keepBookmarks = m_bookmarks.all();
    const QString path = m_passwords.path();
    QString previous;
    if (had) {
        previous = CryptoBox::backupCopy(path, QStringLiteral(".before-import.bak"));
        if (previous.isEmpty()) {
            QMessageBox::warning(this, I18n::tr("vault"), I18n::tr("vault.import.fail"));
            return;
        }
    }
    lockAll(true);
    const auto restore = [&] {
        QFile::remove(path);
        if (!previous.isEmpty())
            QFile::copy(previous, path);
        PrivatePaths::restrictFile(path);
    };
    QFile::remove(path);
    if (!QFile::copy(src, path)) {
        restore();
        QMessageBox::warning(this, I18n::tr("vault"), I18n::tr("vault.import.fail"));
        return;
    }
    PrivatePaths::restrictFile(path);
    if (had && sealed) {
        QMessageBox::information(this, I18n::tr("vault"), I18n::tr("vault.import.unlock"));
        if (!ensureVaultUnlocked()) {
            restore();
            QMessageBox::warning(this, I18n::tr("vault"), I18n::tr("vault.import.restored"));
            return;
        }
        // onVaultUnlocked() set the old sealed files aside; put the
        // bookmarks back under the new key (settings kept their values).
        for (const auto& b : keepBookmarks)
            m_bookmarks.add(b);
        refreshHomePages();
    }
    QMessageBox::information(this, I18n::tr("vault"), I18n::tr("vault.import.ok"));
}

void MainWindow::showPasswordVault()
{
    if (!m_settings.savePasswords) {
        QMessageBox::information(this, I18n::tr("settings.passwords"), I18n::tr("vault.disabled"));
        return;
    }
    if (!ensureVaultUnlocked())
        return;

    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("vaultDialog"));
    dialog.setWindowTitle(I18n::tr("vault.title"));
    dialog.resize(600, 440);
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(10);
    auto* heading = new QLabel(I18n::tr("vault.title"), &dialog);
    heading->setObjectName("dialogTitle");
    auto* desc = new QLabel(I18n::tr("vault.desc"), &dialog);
    desc->setObjectName("dialogHint");
    desc->setWordWrap(true);
    auto* list = new QListWidget(&dialog);
    list->setObjectName("dialogList");
    auto* empty = new QLabel(I18n::tr("vault.empty"), &dialog);
    empty->setObjectName("dialogHint");
    auto reload = [&] {
        list->clear();
        for (const auto& row : m_passwords.all()) {
            auto* it = new QListWidgetItem(row.host + "  ·  " + row.user, list);
            it->setData(Qt::UserRole, row.host);
        }
        empty->setVisible(list->count() == 0);
    };
    reload();
    layout->addWidget(heading);
    layout->addWidget(desc);
    layout->addWidget(list, 1);
    layout->addWidget(empty);
    auto* row = new QHBoxLayout;
    row->setSpacing(8);
    auto* addBtn = new QPushButton(I18n::tr("add"), &dialog);
    auto* delBtn = new QPushButton(I18n::tr("delete"), &dialog);
    delBtn->setObjectName("dangerBtn");
    auto* chgBtn = new QPushButton(I18n::tr("vault.change.btn"), &dialog);
    auto* lockBtn = new QPushButton(I18n::tr("vault.lock"), &dialog);
    auto* expBtn = new QPushButton(I18n::tr("export"), &dialog);
    auto* impBtn = new QPushButton(I18n::tr("import"), &dialog);
    auto* closeBtn = new QPushButton(I18n::tr("close"), &dialog);
    for (auto* b : {addBtn, delBtn, chgBtn, lockBtn, expBtn, impBtn, closeBtn})
        b->setCursor(Qt::PointingHandCursor);
    for (auto* b : {addBtn, delBtn, chgBtn, lockBtn})
        row->addWidget(b);
    row->addStretch();
    auto* row2 = new QHBoxLayout;
    row2->setSpacing(8);
    row2->addWidget(expBtn);
    row2->addWidget(impBtn);
    row2->addStretch();
    row2->addWidget(closeBtn);
    layout->addLayout(row);
    layout->addLayout(row2);

    connect(addBtn, &QPushButton::clicked, &dialog, [&] {
        bool ok = false;
        const QString title = I18n::tr("add");
        const QString host = QInputDialog::getText(&dialog, title, I18n::tr("vault.host"), QLineEdit::Normal, {}, &ok).trimmed();
        if (!ok || host.isEmpty()) return;
        const QString user = QInputDialog::getText(&dialog, title, I18n::tr("username"), QLineEdit::Normal, {}, &ok);
        if (!ok) return;
        const QString pass = QInputDialog::getText(&dialog, title, I18n::tr("password"), QLineEdit::Password, {}, &ok);
        if (!ok) return;
        m_passwords.upsert({host, user, pass});
        reload();
    });
    connect(delBtn, &QPushButton::clicked, &dialog, [&] {
        auto* item = list->currentItem();
        if (!item) return;
        m_passwords.removeHost(item->data(Qt::UserRole).toString());
        reload();
    });
    connect(chgBtn, &QPushButton::clicked, this, &MainWindow::changeMasterPassword);
    connect(lockBtn, &QPushButton::clicked, &dialog, [this, &dialog] {
        lockVault();
        dialog.reject();
    });
    connect(expBtn, &QPushButton::clicked, this, &MainWindow::exportVault);
    connect(impBtn, &QPushButton::clicked, &dialog, [this, &dialog] {
        importVault();
        dialog.reject();
    });
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.exec();
}

void MainWindow::wipeNow()
{
    IncoSettings wipe = m_settings;
    wipe.clearCookiesOnExit = true;
    wipe.clearCacheOnExit = true;
    m_profile.wipeOnExit(wipe);
    QMessageBox::information(this, I18n::tr("settings.privacy"), I18n::tr("wipe.done"));
}

void MainWindow::refreshSettings()
{
    applyLanguageUi();
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (m_tabs->widget(i) &&
            m_tabs->widget(i)->objectName() == QLatin1String("settingsPage")) {
            m_tabs->widget(i)->deleteLater();
            m_tabs->removeTab(i);
            break;
        }
    }
    showSettings();
}

void MainWindow::applyLanguageUi()
{
    setWindowTitle(m_anonymous ? I18n::tr("app.anon") : I18n::tr("app.name"));
    if (m_addressBar)
        m_addressBar->setPlaceholderText(I18n::tr("search.placeholder"));
    if (m_backButton) m_backButton->setToolTip(I18n::tr("back"));
    if (m_forwardButton) m_forwardButton->setToolTip(I18n::tr("forward"));
    if (m_reloadButton) m_reloadButton->setToolTip(I18n::tr("reload"));
    if (m_newTabButton) m_newTabButton->setToolTip(I18n::tr("newtab"));
    if (m_bookmarkButton) m_bookmarkButton->setToolTip(I18n::tr("bookmark.page"));
    if (m_bookmarkListButton) m_bookmarkListButton->setToolTip(I18n::tr("bookmarks"));
    if (m_passwordsButton) m_passwordsButton->setToolTip(I18n::tr("settings.passwords"));
    if (m_settingsButton) m_settingsButton->setToolTip(I18n::tr("settings"));
    if (m_extButton) m_extButton->setToolTip(I18n::tr("ext.button"));
    if (m_muteButton) m_muteButton->setToolTip(I18n::tr("mute"));
    if (m_onionButton) {
        m_onionButton->setToolTip(I18n::tr("onion.tip"));
        if (m_anonymous || m_torProcess)
            m_onionButton->setText(m_torProcess ? I18n::tr("onion.on") : I18n::tr("anon"));
        else if (!m_onionButton->isChecked())
            m_onionButton->setText(I18n::tr("onionize"));
        else
            m_onionButton->setText(I18n::tr("onion.on"));
    }
    if (m_findInput)
        m_findInput->setPlaceholderText(I18n::tr("find.placeholder"));
    if (auto* l = m_findBar ? m_findBar->findChild<QLabel*>("findLabel") : nullptr)
        l->setText(I18n::tr("find"));
    if (auto* b = m_findBar ? m_findBar->findChild<QPushButton*>("findNextBtn") : nullptr)
        b->setText(I18n::tr("find.next"));
    if (auto* b = m_findBar ? m_findBar->findChild<QPushButton*>("findPrevBtn") : nullptr)
        b->setText(I18n::tr("find.prev"));
    if (auto* l = m_downloadBar ? m_downloadBar->findChild<QLabel*>("dlTitle") : nullptr)
        l->setText(I18n::tr("downloads"));
    if (auto* b = m_downloadBar ? m_downloadBar->findChild<QPushButton*>("dlOpen") : nullptr)
        b->setText(I18n::tr("open.folder"));
    if (auto* b = m_downloadBar ? m_downloadBar->findChild<QPushButton*>("dlHide") : nullptr)
        b->setText(I18n::tr("hide"));

    const QString lang = I18n::currentLang();
    const QString js = QStringLiteral(
        "if (window.incoApplyLang) window.incoApplyLang('%1');").arg(lang);
    for (int i = 0; i < m_tabs->count(); ++i) {
        QWidget* w = m_tabs->widget(i);
        if (w && w->objectName() == QLatin1String("settingsPage"))
            m_tabs->setTabText(i, I18n::tr("settings"));
        auto* view = qobject_cast<QWebEngineView*>(w);
        if (!view)
            continue;
        if (view->url().scheme() == QLatin1String("qrc")) {
            view->page()->runJavaScript(js);
            const QString title = view->title();
            if (title.isEmpty() || title == QLatin1String("New Tab") ||
                title == QLatin1String("Neuer Tab") || title == QLatin1String("Nouvel onglet") ||
                title == QLatin1String("Nuova scheda") || title == QLatin1String("新标签页"))
                m_tabs->setTabText(i, I18n::tr("newtab"));
        }
    }
}

void MainWindow::showSettings()
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (m_tabs->widget(i) &&
            m_tabs->widget(i)->objectName() == QLatin1String("settingsPage")) {
            m_tabs->setCurrentIndex(i);
            return;
        }
    }

    auto* page = new SettingsPage(&m_settings, &m_profile, &m_settingsStore, this, m_tabs);
    auto* scroll = new QScrollArea(m_tabs);
    scroll->setObjectName("settingsPage");
    scroll->setWidget(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet(
        "QScrollArea { background: #0e0e0e; border: 0; }"
        "QScrollArea > QWidget > QWidget { background: #0e0e0e; }"
        "QScrollBar:vertical { background: transparent; width: 10px; margin: 4px 2px; }"
        "QScrollBar::handle:vertical { background: #2c2c2c; border-radius: 3px; min-height: 40px; }"
        "QScrollBar::handle:vertical:hover { background: #3a3a3a; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }");
    page = nullptr;

    const int i = m_tabs->addTab(scroll, QIcon(":/icons/settings.svg"), I18n::tr("settings"));
    m_tabs->setCurrentIndex(i);
}


void MainWindow::showFindBar()
{
    Motion::slideOpen(m_findBar);
    m_findInput->setFocus();
    m_findInput->selectAll();
    reloadFind();
}
void MainWindow::hideFindBar()
{
    if (!m_findBar->isVisible())
        return;
    Motion::slideClose(m_findBar);
    if (auto* v = currentView()) v->findText(QString());
}
void MainWindow::reloadFind()
{
    if (m_findBar->isVisible() && !m_findBar->property("motionHiding").toBool())
        if (auto* v = currentView()) v->findText(m_findInput->text());
}
void MainWindow::findNext() { if (auto* v = currentView()) v->findText(m_findInput->text()); }
void MainWindow::findPrev()
{
    if (auto* v = currentView())
        v->findText(m_findInput->text(), QWebEnginePage::FindBackward);
}
void MainWindow::zoomBy(double d)
{
    if (auto* v = currentView())
        v->setZoomFactor(qBound(0.5, v->zoomFactor() + d, 3.0));
    updateZoomLabel();
}
void MainWindow::resetZoom() { if (auto* v = currentView()) { v->setZoomFactor(1.0); updateZoomLabel(); } }

void MainWindow::handleDownload(QWebEngineDownloadRequest* download)
{
    if (!download) return;
    download->setDownloadDirectory(m_settings.downloadDir);
    download->accept();
    Motion::slideOpen(m_downloadBar);
    auto* item = new QListWidgetItem(download->downloadFileName() + " · " + I18n::tr("dl.starting"), m_downloadList);

    connect(download, &QWebEngineDownloadRequest::receivedBytesChanged, this, [download, item] {
        const qint64 total = download->totalBytes();
        const qint64 got = download->receivedBytes();
        QString extra = total > 0
            ? QString("%1%").arg(int((got * 100) / total))
            : QString("%1 KB").arg(got / 1024);
        item->setText(download->downloadFileName() + " · " + extra);
    });
    connect(download, &QWebEngineDownloadRequest::isFinishedChanged, this, [download, item] {
        if (!download->isFinished()) return;
        QString state = I18n::tr("dl.done");
        if (download->state() == QWebEngineDownloadRequest::DownloadCancelled)
            state = I18n::tr("dl.cancelled");
        else if (download->state() == QWebEngineDownloadRequest::DownloadInterrupted)
            state = I18n::tr("dl.failed");
        item->setText(download->downloadFileName() + " · " + state);
    });
}

void MainWindow::updateAddressBar(const QUrl& url)
{
    if (url.scheme() == QLatin1String("qrc"))
        m_addressBar->clear();
    else
        m_addressBar->setText(url.toString());
}
void MainWindow::updateWindowTitle(const QString& title)
{
    const QString app = m_anonymous ? I18n::tr("app.anon") : I18n::tr("app.name");
    setWindowTitle(title.isEmpty() ? app : title + " · " + app);
}
void MainWindow::applyDarkTheme()
{
    setStyleSheet(R"(
        QMainWindow, QDialog, QMessageBox, QInputDialog {
            background: #0e0e0e; color: #ececec;
        }
        QWidget { outline: 0; }
        QToolBar {
            background: #121212;
            border: 0;
            border-bottom: 1px solid #1c1c1c;
            padding: 8px 10px;
            spacing: 6px;
            margin: 0;
        }
        QToolBar QToolButton, QToolBar QPushButton {
            background: transparent;
            border: 0;
            border-radius: 10px;
            padding: 6px;
            color: #e4e4e4;
        }
        QToolBar QToolButton:hover, QToolBar QPushButton:hover {
            background: #1f1f1f;
        }
        QToolBar QToolButton:pressed, QToolBar QPushButton:pressed {
            background: #1a1a1a;
        }
        QToolBar QLineEdit {
            background: #101010;
            color: #eeeeee;
            border: 1px solid #2a2a2a;
            border-radius: 12px;
            padding: 8px 14px;
            font-size: 13px;
            selection-background-color: #3a2a12;
            selection-color: #f0d9a0;
        }
        QToolBar QLineEdit:focus { border-color: #4a4a4a; }
        QPushButton#onionBtn {
            background: #1f1f1f;
            color: #e8e8e8;
            border: 1px solid #2c2c2c;
            border-radius: 10px;
            padding: 7px 14px;
            font-size: 13px;
        }
        QPushButton#onionBtn:hover { background: #272727; border-color: #383838; }
        QPushButton#onionBtn:checked {
            background: #3a2a12;
            color: #f0d9a0;
            border-color: #5a4320;
        }
        QPushButton#onionBtn:checked:hover {
            background: #453318;
            border-color: #6a5230;
        }
        QTabWidget { margin: 0; padding: 0; background: #0e0e0e; }
        QTabWidget::pane { border: 0; margin: 0; padding: 0; top: 0; }
        QTabBar {
            background: #0e0e0e;
        }
        QTabBar::tab {
            background: #151515;
            color: #a8a8a8;
            padding: 8px 10px 8px 12px;
            margin: 6px 3px 4px 0;
            border: 1px solid #222222;
            border-radius: 10px;
            min-width: 80px;
            font-size: 12px;
        }
        QTabBar::tab:selected {
            background: #1f1f1f;
            color: #f2f2f2;
            border-color: #2c2c2c;
        }
        QTabBar::tab:hover:!selected {
            background: #1a1a1a;
            color: #e0e0e0;
        }
        QTabBar::tab:focus { outline: 0; }
        QTabBar::close-button {
            image: url(:/icons/close.svg);
            width: 12px;
            height: 12px;
            margin: 0 1px 0 4px;
            background: transparent;
            border-radius: 6px;
        }
        QTabBar::close-button:hover { background: #2c2c2c; }
        QPushButton {
            background: #1f1f1f;
            color: #e8e8e8;
            border: 1px solid #2c2c2c;
            border-radius: 10px;
            padding: 8px 14px;
            font-size: 13px;
        }
        QPushButton:hover { background: #272727; border-color: #383838; }
        QPushButton:pressed { background: #1a1a1a; }
        QPushButton:disabled {
            color: #5a5a5a;
            background: #171717;
            border-color: #212121;
        }
        QPushButton#dangerBtn { color: #f2b8ab; }
        QPushButton#dangerBtn:hover {
            background: #2a1714;
            border-color: #44231e;
        }
        QLineEdit, QPlainTextEdit, QTextEdit, QSpinBox, QComboBox {
            background: #101010;
            color: #eeeeee;
            border: 1px solid #2a2a2a;
            border-radius: 10px;
            padding: 8px 12px;
            font-size: 13px;
            selection-background-color: #3a2a12;
            selection-color: #f0d9a0;
        }
        QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus, QComboBox:focus {
            border-color: #4a4a4a;
        }
        QListWidget, QTreeWidget, QTableWidget {
            background: #151515;
            color: #e4e4e4;
            border: 1px solid #222222;
            border-radius: 12px;
            padding: 4px;
            outline: 0;
        }
        QListWidget::item, QTreeWidget::item {
            padding: 8px 10px;
            border-radius: 8px;
        }
        QListWidget::item:selected, QTreeWidget::item:selected {
            background: #3a2a12;
            color: #f0d9a0;
        }
        QListWidget::item:hover:!selected, QTreeWidget::item:hover:!selected {
            background: #1f1f1f;
        }
        QCheckBox, QLabel { color: #d6d6d6; font-size: 13px; }
        QMenu {
            background: #151515;
            color: #e8e8e8;
            border: 1px solid #2a2a2a;
            border-radius: 10px;
            padding: 6px;
        }
        QMenu::item {
            padding: 8px 18px;
            border-radius: 8px;
        }
        QMenu::item:selected {
            background: #3a2a12;
            color: #f0d9a0;
        }
        QMenu::separator {
            height: 1px;
            background: #222222;
            margin: 4px 8px;
        }
        QScrollBar:vertical {
            background: transparent;
            width: 10px;
            margin: 4px 2px;
        }
        QScrollBar::handle:vertical {
            background: #2c2c2c;
            border-radius: 3px;
            min-height: 40px;
        }
        QScrollBar::handle:vertical:hover { background: #3a3a3a; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
        QWidget#findBar, QWidget#downloadBar {
            background: #121212;
            border-top: 1px solid #1c1c1c;
            border-bottom: 1px solid #1c1c1c;
        }
        QWidget#findBar QLabel, QWidget#downloadBar QLabel {
            color: #a8a8a8;
            font-size: 12px;
            font-weight: 600;
        }
        QWidget#findBar QLineEdit {
            background: #101010;
            border: 1px solid #2a2a2a;
            border-radius: 10px;
            padding: 6px 12px;
        }
        QMessageBox QLabel { color: #d6d6d6; }
        QDialog QLabel#dialogTitle { color: #f0f0f0; font-size: 15px; font-weight: 600; }
        QDialog QLabel#dialogHint { color: #8a8a8a; font-size: 12px; }
        QToolTip {
            background: #1a1a1a;
            color: #e8e8e8;
            border: 1px solid #2a2a2a;
            border-radius: 8px;
            padding: 6px 10px;
        }
    )");
}

bool MainWindow::httpsOnly() const
{
    return m_settings.httpsOnly;
}

void MainWindow::toggleMute()
{
    auto* v = currentView();
    if (!v) return;
    v->page()->setAudioMuted(!v->page()->isAudioMuted());
    updateZoomLabel();
}

void MainWindow::updateZoomLabel()
{
    auto* v = currentView();
    if (!m_zoomLabel)
        return;
    if (!v) {
        m_zoomLabel->setText("100%");
        return;
    }
    m_zoomLabel->setText(QString::number(int(v->zoomFactor() * 100)) + "%");
    if (m_muteButton) {
        const bool muted = v->page()->isAudioMuted();
        m_muteButton->setToolTip(muted ? I18n::tr("unmute") : I18n::tr("mute"));
        m_muteButton->setIcon(QIcon(muted ? QStringLiteral(":/icons/speaker-off.svg")
                                          : QStringLiteral(":/icons/speaker.svg")));
    }
}



#include <QProcess>
#include <QTcpSocket>

bool MainWindow::isOnionTab(QWebEngineView* view) const
{
    return view && view->property("incoOnion").toBool();
}

void MainWindow::onionizeCurrentTab()
{
    if (m_torProcess || m_anonymous) {
        if (m_tabs && m_tabs->count() > 1) {
            QMessageBox box(QMessageBox::Question, I18n::tr("onion.off.title"), I18n::tr("onion.off.body"),
                            QMessageBox::Yes | QMessageBox::Cancel, this);
            box.setDefaultButton(QMessageBox::Yes);
            if (box.exec() != QMessageBox::Yes)
                return;
        }
        close();
        return;
    }

    // Integrity check in this window first: if it fails, only Onionize is
    // refused; this window keeps browsing normally.
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    const auto verified = TorIntegrity::verifyBundle();
    QString portDetail;
    const auto port = verified.ok() ? TorIntegrity::socksPortState(verified, &portDetail)
                                    : TorIntegrity::PortState::Free;
    QGuiApplication::restoreOverrideCursor();

    if (!verified.ok() || port == TorIntegrity::PortState::Foreign) {
        QMessageBox box(QMessageBox::Warning, I18n::tr("integrity.title"),
                        verified.ok() ? I18n::tr("integrity.port") : I18n::tr("integrity.body"),
                        QMessageBox::Ok, this);
        box.setInformativeText(verified.ok() ? portDetail : TorIntegrity::describe(verified));
        box.exec();
        if (m_onionButton) {
            m_onionButton->setChecked(false);
            m_onionButton->setText(I18n::tr("onionize"));
        }
        return;
    }

    const QString exe = QCoreApplication::applicationFilePath();
    QProcess proc;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("INCO_TOR_DIR", verified.torDir);  // the dir we just verified
    env.insert(QString::fromLatin1(SearchEngine::kEnvName), searchTemplate());
    proc.setProgram(exe);
    proc.setArguments({"--anon", "--tor"});
    proc.setProcessEnvironment(env);
    if (!proc.startDetached()) {
        QMessageBox::warning(this, I18n::tr("onionize"), I18n::tr("onion.start.fail").arg(exe));
        return;
    }
}

void MainWindow::openAnonymousSession()
{
    onionizeCurrentTab();
}


void MainWindow::fillHomeBookmarks(QWebEngineView* view)
{
    if (!view)
        return;
    if (view->url().scheme() != QLatin1String("qrc"))
        return;
    // Onionize / anonymous windows never show the normal profile's bookmarks.
    if (m_anonymous || !m_settings.bookmarksEnabled || !m_settings.showBookmarksOnHome)
        return;

    // Hand the data over as JSON and build the links with DOM calls, so a
    // quote in a URL or title can never end up as JavaScript or HTML.
    QJsonArray rows;
    if (m_bookmarks.isLocked()) {
        // Sealed and locked: one link that asks for the master password.
        rows.append(QJsonObject{{QStringLiteral("u"), QStringLiteral("inco://unlock")},
                                {QStringLiteral("t"), I18n::tr("bookmarks.locked")}});
    }
    for (const auto& bm : m_bookmarks.all()) {
        if (rows.size() >= 16)
            break;
        const QUrl u(bm.url);
        if (u.scheme() != QLatin1String("http") && u.scheme() != QLatin1String("https"))
            continue;
        const QString title = (bm.title.isEmpty() ? bm.url : bm.title).left(32);
        rows.append(QJsonObject{{QStringLiteral("u"), bm.url}, {QStringLiteral("t"), title}});
    }
    const QString json = QString::fromUtf8(QJsonDocument(rows).toJson(QJsonDocument::Compact));
    const QString js = QStringLiteral(
        "(function(rows){"
        "const e=document.getElementById('pins'); if(!e) return;"
        "e.textContent='';"
        "rows.forEach(function(r,i){"
        "const a=document.createElement('a');"
        "a.href=r.u; a.textContent=r.t; a.style.animationDelay=(i*25)+'ms';"
        "e.appendChild(a);"
        "});"
        "})(%1);").arg(json);
    view->page()->runJavaScript(js);
}


bool MainWindow::ensureVaultUnlocked()
{
    if (m_passwords.isUnlocked())
        return true;
    bool ok = false;
    const bool exists = m_passwords.exists();
    QString pw = QInputDialog::getText(
        this, I18n::tr("vault"),
        exists ? I18n::tr("vault.master") : I18n::tr("vault.create") + QLatin1String("\n") + I18n::tr("vault.new"),
        QLineEdit::Password, {}, &ok);
    if (!ok || pw.isEmpty())
        return false;
    if (exists) {
        // Argon2id takes a moment by design.
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const bool unlocked = m_passwords.unlock(pw);
        QApplication::restoreOverrideCursor();
        CryptoBox::wipe(pw);
        if (!unlocked) {
            QMessageBox::warning(this, I18n::tr("vault"), I18n::tr("vault.wrong"));
            return false;
        }
    } else {
        if (pw.size() < 8) {
            CryptoBox::wipe(pw);
            QMessageBox::warning(this, I18n::tr("vault"), I18n::tr("vault.short"));
            return false;
        }
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const bool created = m_passwords.create(pw);
        QApplication::restoreOverrideCursor();
        CryptoBox::wipe(pw);
        if (!created) {
            QMessageBox::warning(this, I18n::tr("vault"), I18n::tr("vault.create.fail"));
            return false;
        }
    }
    onVaultUnlocked();
    return m_passwords.isUnlocked();
}


void MainWindow::checkTorStatus()
{
    if (!m_torProcess || m_probeInFlight)
        return;
    m_probeInFlight = true;
    m_probeAttempt = 0;
    if (m_torLabel)
        m_torLabel->setText(I18n::tr("torbar.checking"));
    runTorProbe();
}

// Tor opens its SOCKS port before it has finished bootstrapping, so the
// first requests of a fresh window can be refused. Retry for ~30 s before
// reporting a problem.
void MainWindow::runTorProbe()
{
    if (!m_torNet)
        m_torNet = makeTorNetwork(this, TorIntegrity::kSocksPort);
    probeTorExit(m_torNet, [this](const TorExitInfo& info) {
        if (!info.ok && ++m_probeAttempt < 10) {
            QTimer::singleShot(3000, this, [this] { runTorProbe(); });
            return;
        }
        m_probeInFlight = false;
        m_lastExit = info;
        if (m_routePanel)
            m_routePanel->setExitInfo(info);
        // The exit address helps pick the matching circuit.
        readCircuit();
        if (!m_torLabel)
            return;
        QString text;
        if (!info.ok)
            text = I18n::tr("torbar.fail").arg(info.error);
        else if (!info.isTor)
            text = I18n::tr("torbar.nottor");
        else {
            QString exit = info.ip;
            if (!info.country.isEmpty())
                exit += QStringLiteral(" (") + info.country + QLatin1Char(')');
            text = I18n::tr("torbar.ok").arg(exit);
        }
        if (text != m_torLabel->text()) {
            m_torLabel->setText(text);
            Motion::fadeIn(m_torLabel, Motion::kNormal);
        }
    });
}

// Ask the bundled Tor (control socket, read-only) which relays the
// current circuit uses. Failures end up in the panel as "unknown".
void MainWindow::readCircuit()
{
    if (!m_torProcess || m_circuitInFlight)
        return;
    m_circuitInFlight = true;
    if (m_routePanel)
        m_routePanel->setCircuitBusy();
    QString host;
    if (auto* v = currentView())
        if (v->url().scheme().startsWith(QLatin1String("http")))
            host = v->url().host();
    const QString exitIp = m_lastExit.ok && m_lastExit.isTor ? m_lastExit.ip : QString();
    TorControl::queryCircuit(this, host, exitIp, [this](const TorControl::Circuit& c) {
        m_circuitInFlight = false;
        m_lastCircuit = c;
        if (!c.ok)
            std::fprintf(stderr, "Inco: circuit unknown: %s\n", qPrintable(c.error));
        if (m_routePanel)
            m_routePanel->setCircuit(c);
    });
}

void MainWindow::showOnionRoute()
{
    if (!m_routePanel) {
        m_routePanel = new OnionRoutePanel(m_torProcess, this);
        // One probe at a time: the panel and the status line share it.
        m_routePanel->onRefresh = [this] {
            // A running exit probe reads the circuit when it finishes.
            if (m_probeInFlight)
                return;
            checkTorStatus();
        };
        m_routePanel->onTurnOff = [this] { onionizeCurrentTab(); };
        m_routePanel->onOpenCheck = [this] {
            addTab(QUrl(QStringLiteral("https://check.torproject.org/")));
        };
        if (m_lastExit.when.isValid())
            m_routePanel->setExitInfo(m_lastExit);
        if (m_lastCircuit.when.isValid())
            m_routePanel->setCircuit(m_lastCircuit);
    } else if (m_routePanel->isVisible() || m_routePanel->justClosed()) {
        m_routePanel->hide();
        return;
    }
    m_routePanel->popupBelow(m_onionButton);
}

void MainWindow::applyLowRam()
{
    if (m_settings.lowRam) {
        if (!m_lifecycleTimer) {
            m_lifecycleTimer = new QTimer(this);
            m_lifecycleTimer->setInterval(5000);
            connect(m_lifecycleTimer, &QTimer::timeout, this, &MainWindow::updateLifecycles);
        }
        m_lifecycleTimer->start();
        updateLifecycles();
        return;
    }
    if (m_lifecycleTimer)
        m_lifecycleTimer->stop();
    // Wake frozen background tabs again; discarded ones reload when opened.
    for (int i = 0; m_tabs && i < m_tabs->count(); ++i) {
        auto* view = qobject_cast<QWebEngineView*>(m_tabs->widget(i));
        if (!view)
            continue;
        view->setProperty("incoHiddenSince", QVariant());
        if (view->page()->lifecycleState() == QWebEnginePage::LifecycleState::Frozen)
            view->page()->setLifecycleState(QWebEnginePage::LifecycleState::Active);
    }
}

// Low RAM: background tabs are frozen after 20 s (no JS/timers) and
// discarded after 60 s (renderer memory released, page reloads on return).
// Chromium's recommendedState() keeps tabs that play audio, have devtools
// open or are otherwise busy from being frozen or discarded.
void MainWindow::updateLifecycles()
{
    if (!m_tabs || !m_settings.lowRam)
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 freezeAfter = qEnvironmentVariableIntValue("INCO_FREEZE_MS") > 0
        ? qEnvironmentVariableIntValue("INCO_FREEZE_MS") : 20000;
    const qint64 discardAfter = qEnvironmentVariableIntValue("INCO_DISCARD_MS") > 0
        ? qEnvironmentVariableIntValue("INCO_DISCARD_MS") : 60000;
    QWebEngineView* current = currentView();
    for (int i = 0; i < m_tabs->count(); ++i) {
        auto* view = qobject_cast<QWebEngineView*>(m_tabs->widget(i));
        if (!view || view == current)
            continue;
        QWebEnginePage* page = view->page();
        if (!view->property("incoHiddenSince").isValid()) {
            view->setProperty("incoHiddenSince", now);
            continue;
        }
        const qint64 hidden = now - view->property("incoHiddenSince").toLongLong();
        using LS = QWebEnginePage::LifecycleState;
        const LS state = page->lifecycleState();
        const LS allowed = page->recommendedState();
        if (hidden >= discardAfter && state != LS::Discarded && allowed >= LS::Discarded)
            page->setLifecycleState(LS::Discarded);
        else if (hidden >= freezeAfter && state == LS::Active && allowed >= LS::Frozen)
            page->setLifecycleState(LS::Frozen);
    }
}

void MainWindow::updateExtensionButton()
{
    if (!m_extAction)
        return;
    const bool show = !m_anonymous && !m_torProcess && m_profile.supportsExtensions()
                      && m_settings.extensionsOn && !m_profile.activeExtensions().isEmpty();
    m_extAction->setVisible(show);
    if (!show && m_extPopup)
        m_extPopup->close();
}

void MainWindow::showExtensionMenu()
{
    if (m_anonymous || m_torProcess || !m_profile.supportsExtensions())
        return;
    QMenu menu(this);
    const auto active = m_profile.activeExtensions();
    for (const auto& e : active) {
        const QString name = e.name().isEmpty() ? e.id() : e.name();
        const QUrl popup = e.actionPopupUrl();
        auto* act = menu.addAction(name, this, [this, popup] { openExtensionPopup(popup); });
        if (popup.isEmpty()) {
            act->setEnabled(false);
            act->setText(I18n::tr("ext.nopopup").arg(name));
        }
    }
    if (!active.isEmpty())
        menu.addSeparator();
    menu.addAction(I18n::tr("ext.manage"), this, &MainWindow::showSettings);
    menu.exec(m_extButton->mapToGlobal(QPoint(0, m_extButton->height())));
}

// Extension popups (the toolbar action page) open in a small popup window on
// the normal profile, below the extensions button.
void MainWindow::openExtensionPopup(const QUrl& url)
{
    if (m_anonymous || m_torProcess || !m_profile.supportsExtensions() || !url.isValid())
        return;
    if (m_extPopup)
        delete m_extPopup.data();

    auto* popup = new QWidget(this, Qt::Popup);
    popup->setAttribute(Qt::WA_DeleteOnClose);
    popup->setStyleSheet("background:#151515;");
    auto* lay = new QVBoxLayout(popup);
    lay->setContentsMargins(1, 1, 1, 1);
    auto* view = new QWebEngineView(popup);
    auto* page = new QWebEnginePage(m_profile.engine(), view);
    view->setPage(page);
    lay->addWidget(view);
    popup->resize(360, 480);

    const auto place = [this, popup] {
        const QPoint corner = m_extButton->mapToGlobal(QPoint(m_extButton->width(), m_extButton->height() + 4));
        popup->move(corner.x() - popup->width(), corner.y());
    };
    // Popups size themselves through their content; follow within bounds.
    connect(page, &QWebEnginePage::contentsSizeChanged, popup, [popup, place](const QSizeF& size) {
        const int w = qBound(240, int(size.width()) + 2, 800);
        const int h = qBound(120, int(size.height()) + 2, 600);
        if (popup->size() != QSize(w, h)) {
            popup->resize(w, h);
            place();
        }
    });
    connect(page, &QWebEnginePage::windowCloseRequested, popup, &QWidget::close);
    connect(page, &QWebEnginePage::newWindowRequested, this, [this, popup](QWebEngineNewWindowRequest& request) {
        addTab(request.requestedUrl());
        popup->close();
    });
    page->setUrl(url);
    place();
    m_extPopup = popup;
    popup->show();
}
