#include "MainWindow.hpp"
#include <QColor>
#include "SettingsPage.hpp"
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScrollArea>
#include <QCoreApplication>
#include <QWebEngineContextMenuRequest>
#include <QMenu>
#include <QGuiApplication>
#include <QClipboard>

#include <QAuthenticator>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDesktopServices>
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
#include <QWebEngineHistory>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineView>

static QString jsQuote(const QString& s)
{
    QString o = s;
    o.replace('\\', "\\\\");
    o.replace('\'', "\\'");
    o.replace('\n', "\\n");
    return o;
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
    bool acceptNavigationRequest(const QUrl& url, NavigationType, bool) override
    {
        if (url.scheme() == QLatin1String("inco") && url.host() == QLatin1String("onionize")) {
            m_window->onionizeCurrentTab();
            return false;
        }
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
      m_profile(anonymous),
      m_anonProfile(true),
      m_passwords(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/vault.bin"),
      m_bookmarks(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/bookmarks.json"),
      m_anonymous(anonymous),
      m_torProcess(tor)
{
    setWindowTitle(anonymous ? "Inco Browser — Anonymous" : "Inco Browser");
    setWindowIcon(QIcon(":/inco-browser.png"));
    resize(1280, 800);
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
            m_torProcess
                ? "Anonymous + Tor proxy. Not Tor Browser. Fingerprint still exists."
                : "Anonymous session: nothing saved. Not anonymous on the network unless Tor is used.",
            this);
        banner->setWordWrap(true);
        banner->setStyleSheet("background:#1a1510; color:#e8d8b0; padding:8px 12px;");
        if (auto* lay = qobject_cast<QVBoxLayout*>(centralWidget()->layout()))
            lay->insertWidget(0, banner);
    }
    connect(m_profile.engine(), &QWebEngineProfile::downloadRequested,
            this, &MainWindow::handleDownload);
    connect(m_anonProfile.engine(), &QWebEngineProfile::downloadRequested,
            this, &MainWindow::handleDownload);
    addTab(m_anonymous || m_torProcess ? QUrl(QStringLiteral("qrc:/newtab.html#onion"))
                         : QUrl(QStringLiteral("qrc:/newtab.html")));
}

MainWindow::~MainWindow()
{
    m_passwords.lock();
    m_profile.wipeOnExit(m_settings);
    m_settingsStore.save(m_settings);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    m_passwords.lock();
    m_settingsStore.save(m_settings);
    QMainWindow::closeEvent(event);
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
    m_muteButton = new QPushButton(this);
    m_onionButton = new QPushButton("Onionize", this);
    m_zoomLabel = new QLabel("100%", this);
    m_tabs = new QTabWidget(this);

    const auto iconize = [](QPushButton* b, const char* path, const char* tip) {
        b->setText({});
        b->setIcon(QIcon(QString::fromUtf8(path)));
        b->setIconSize(QSize(18, 18));
        b->setFixedSize(36, 36);
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::NoFocus);
        b->setToolTip(QString::fromUtf8(tip));
        b->setFlat(true);
    };
    iconize(m_backButton, ":/icons/back.svg", "Back");
    iconize(m_forwardButton, ":/icons/forward.svg", "Forward");
    iconize(m_reloadButton, ":/icons/reload.svg", "Reload");
    iconize(m_newTabButton, ":/icons/plus.svg", "New tab");
    iconize(m_bookmarkButton, ":/icons/star.svg", "Bookmark this page");
    iconize(m_bookmarkListButton, ":/icons/bookmarks.svg", "Bookmarks");
    iconize(m_passwordsButton, ":/icons/key.svg", "Passwords");
    iconize(m_settingsButton, ":/icons/settings.svg", "Settings");
    iconize(m_muteButton, ":/icons/plus.svg", "Mute tab");
    m_zoomLabel->setFixedWidth(44);
    m_zoomLabel->setAlignment(Qt::AlignCenter);
    m_zoomLabel->setStyleSheet("color:#9a9a9a; font-size:12px;");


    m_bookmarkButton->setToolTip("Bookmark this page");
    m_bookmarkListButton->setToolTip("Bookmarks");
    m_bookmarkButton->setVisible(m_settings.bookmarksEnabled);
    m_bookmarkListButton->setVisible(m_settings.bookmarksEnabled);

    auto* toolbar = new QToolBar(this);
    toolbar->setMovable(false);
    toolbar->addWidget(m_backButton);
    toolbar->addWidget(m_forwardButton);
    toolbar->addWidget(m_reloadButton);
    toolbar->addWidget(m_newTabButton);
    m_addressBar->setPlaceholderText("Search privately or enter an address");
    toolbar->addWidget(m_addressBar);
    toolbar->addWidget(m_onionButton);
    toolbar->addWidget(m_zoomLabel);
    toolbar->addWidget(m_muteButton);
    toolbar->addWidget(m_bookmarkButton);
    toolbar->addWidget(m_bookmarkListButton);
    toolbar->addWidget(m_passwordsButton);
    toolbar->addWidget(m_settingsButton);
    addToolBar(toolbar);

    m_findBar = new QWidget(this);
    auto* findLayout = new QHBoxLayout(m_findBar);
    findLayout->setContentsMargins(8, 4, 8, 4);
    m_findInput = new QLineEdit(m_findBar);
    m_findInput->setPlaceholderText("Find in page");
    auto* findNextBtn = new QPushButton("Next", m_findBar);
    auto* findPrevBtn = new QPushButton("Prev", m_findBar);
    auto* findCloseBtn = new QPushButton("✕", m_findBar);
    findLayout->addWidget(new QLabel("Find", m_findBar));
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
    auto* dlLayout = new QVBoxLayout(m_downloadBar);
    dlLayout->setContentsMargins(8, 6, 8, 6);
    auto* head = new QHBoxLayout;
    head->addWidget(new QLabel("Downloads", m_downloadBar));
    auto* dlOpen = new QPushButton("Open folder", m_downloadBar);
    auto* dlHide = new QPushButton("Hide", m_downloadBar);
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
    m_torLabel->setStyleSheet("color:#e6c07b;padding:6px 12px;background:#2a2010;");
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
    m_muteButton->setText({});
    m_muteButton->setIcon(QIcon(":/icons/speaker.svg"));
    m_muteButton->setIconSize(QSize(18, 18));
    m_muteButton->setFixedSize(36, 36);
    m_muteButton->setToolTip("Mute tab");
    m_muteButton->setFlat(true);
    if (m_zoomLabel) {
        m_zoomLabel->setFixedWidth(44);
        m_zoomLabel->setAlignment(Qt::AlignCenter);
    }
    m_onionButton->setCheckable(true);
    m_onionButton->setCursor(Qt::PointingHandCursor);
    m_onionButton->setToolTip("Anonymous session. Optional Tor if enabled in Settings.");
    m_onionButton->setStyleSheet(
        "QPushButton { padding: 6px 12px; border-radius: 14px; background:#1c1c1c; }"
        "QPushButton:checked { background:#3a2a12; color:#f0d9a0; }"
    );
    m_onionButton->setChecked(m_anonymous);
    if (m_anonymous)
        m_onionButton->setText(m_torProcess ? "Onion" : "Anon");
    if (m_anonymous || m_torProcess) {
        m_onionButton->setChecked(true);
        m_onionButton->setText("Onion");
    }
    connect(m_onionButton, &QPushButton::clicked, this, [this] {
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
    connect(dlHide, &QPushButton::clicked, this, [this] { m_downloadBar->hide(); });
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
    m_tabs->setCurrentIndex(m_tabs->addTab(view, QIcon(":/inco-browser.png"), "New Tab"));
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
            QString text = title.isEmpty() ? QStringLiteral("New Tab") : title.left(24);
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
                    menu->addAction("Open link in new tab", this, [this, link] { addTab(link); }));
                menu->insertAction(menu->actions().value(1),
                    menu->addAction("Copy link", this, [link] {
                        QGuiApplication::clipboard()->setText(link.toString());
                    }));
            }
        }
        menu->addAction("Back", this, [view] { view->back(); });
        menu->addAction("Reload", this, [view] { view->reload(); });
        menu->exec(view->mapToGlobal(pos));
        menu->deleteLater();
    });

    connect(view->page(), &QWebEnginePage::authenticationRequired,
            this, [this](const QUrl& reqUrl, QAuthenticator* authenticator) {
        if (!m_settings.savePasswords || !ensureVaultUnlocked())
            return;
        QDialog dialog(this);
        dialog.setWindowTitle("Sign in");
        auto* form = new QFormLayout(&dialog);
        auto* user = new QLineEdit(&dialog);
        auto* pass = new QLineEdit(&dialog);
        pass->setEchoMode(QLineEdit::Password);
        auto* ok = new QPushButton("Log in", &dialog);
        form->addRow("Site", new QLabel(reqUrl.host(), &dialog));
        form->addRow("Username", user);
        form->addRow("Password", pass);
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
            background: #161616 !important;
            border: 0 !important;
            color: #cfcfcf !important;
          }
          .result__snippet, .result__extras { color: #9a9a9a !important; }
          input, select, textarea {
            background: #1a1a1a !important;
            color: #eee !important;
            border: 1px solid #333 !important;
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
        "if(u){u.value='%1';u.dispatchEvent(new Event('input',{bubbles:true}));}"
        "if(p){p.value='%2';p.dispatchEvent(new Event('input',{bubbles:true}));}"
        "})();"
    ).arg(jsQuote(row.user), jsQuote(row.password));
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
    updateAddressBar(view->url());
    updateWindowTitle(view->title());
    m_backButton->setEnabled(view->history()->canGoBack());
    m_forwardButton->setEnabled(view->history()->canGoForward());
    updateBookmarkButtons();
    updateZoomLabel();
    if (m_onionButton) {
        const bool on = isOnionTab(view);
        m_onionButton->setChecked(on);
        m_onionButton->setText(on ? QStringLiteral("Onion") : QStringLiteral("Onionize"));
    }
}

QWebEngineView* MainWindow::currentView() const
{
    return qobject_cast<QWebEngineView*>(m_tabs->currentWidget());
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
    if (!ok) {
        QString tmpl = m_settings.searchTemplate;
        if (!tmpl.contains(QLatin1String("%s")))
            tmpl += QLatin1String("%s");
        url = QUrl(tmpl.replace(QLatin1String("%s"),
                    QString::fromUtf8(QUrl::toPercentEncoding(input))));
    }
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
        QMessageBox::information(this, "Bookmarks",
            "Turn on Bookmarks in Settings first.");
        return;
    }
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
        QMessageBox::information(this, "Bookmarks", "Turn on Bookmarks in Settings first.");
        return;
    }

    auto* dlg = new QDialog(this);
    dlg->setWindowTitle("Bookmarks");
    dlg->resize(420, 420);
    auto* root = new QVBoxLayout(dlg);

    auto* list = new QListWidget;
    for (const auto& b : m_bookmarks.all()) {
        auto* it = new QListWidgetItem(b.title.isEmpty() ? b.url : b.title);
        it->setData(Qt::UserRole, b.url);
        list->addItem(it);
    }

    auto* title = new QLineEdit;
    title->setPlaceholderText("Title");
    auto* url = new QLineEdit;
    url->setPlaceholderText("https://");
    auto* add = new QPushButton("Add");
    auto* row = new QHBoxLayout;
    row->addWidget(title);
    row->addWidget(url);
    row->addWidget(add);

    auto* open = new QPushButton("Open");
    auto* del = new QPushButton("Delete");
    auto* close = new QPushButton("Close");
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(open);
    buttons->addWidget(del);
    buttons->addStretch();
    buttons->addWidget(close);

    root->addWidget(list);
    root->addLayout(row);
    root->addLayout(buttons);

    QObject::connect(add, &QPushButton::clicked, dlg, [=] {
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
        title->clear();
        url->clear();
        if (auto* v = currentView())
            fillHomeBookmarks(v);
    });
    QObject::connect(open, &QPushButton::clicked, dlg, [=] {
        auto* it = list->currentItem();
        if (!it) return;
        addTab(QUrl(it->data(Qt::UserRole).toString()));
        dlg->accept();
    });
    QObject::connect(del, &QPushButton::clicked, dlg, [=] {
        auto* it = list->currentItem();
        if (!it) return;
        m_bookmarks.removeUrl(it->data(Qt::UserRole).toString());
        delete it;
        if (auto* v = currentView())
            fillHomeBookmarks(v);
    });
    QObject::connect(close, &QPushButton::clicked, dlg, &QDialog::accept);
    dlg->exec();
    dlg->deleteLater();
}


void MainWindow::lockVault()
{
    m_passwords.lock();
    QMessageBox::information(this, "Vault", "Vault locked.");
}

void MainWindow::changeMasterPassword()
{
    if (!ensureVaultUnlocked())
        return;
    bool ok = false;
    const QString oldPw = QInputDialog::getText(this, "Change master password",
        "Current master password:", QLineEdit::Password, {}, &ok);
    if (!ok) return;
    const QString a = QInputDialog::getText(this, "Change master password",
        "New master password:", QLineEdit::Password, {}, &ok);
    if (!ok || a.size() < 8) {
        QMessageBox::warning(this, "Vault", "Need at least 8 characters.");
        return;
    }
    const QString b = QInputDialog::getText(this, "Change master password",
        "Repeat new password:", QLineEdit::Password, {}, &ok);
    if (!ok || a != b) return;
    if (!m_passwords.changeMasterPassword(oldPw, a))
        QMessageBox::warning(this, "Vault", "Change failed. Check the current password.");
    else
        QMessageBox::information(this, "Vault", "Master password updated.");
}

void MainWindow::exportVault()
{
    const QString dest = QFileDialog::getSaveFileName(this, "Export vault", "vault.bin");
    if (dest.isEmpty()) return;
    if (!QFile::copy(m_passwords.path(), dest) && !(QFile::remove(dest) && QFile::copy(m_passwords.path(), dest)))
        QMessageBox::warning(this, "Vault", "Export failed.");
}

void MainWindow::importVault()
{
    const QString src = QFileDialog::getOpenFileName(this, "Import vault");
    if (src.isEmpty()) return;
    m_passwords.lock();
    QFile::remove(m_passwords.path());
    if (!QFile::copy(src, m_passwords.path())) {
        QMessageBox::warning(this, "Vault", "Import failed.");
        return;
    }
    QMessageBox::information(this, "Vault", "Imported. Unlock with that vault’s master password.");
}

void MainWindow::showPasswordVault()
{
    if (!m_settings.savePasswords) {
        QMessageBox::information(this, "Passwords", "Disabled in settings.");
        return;
    }
    if (!ensureVaultUnlocked())
        return;

    QDialog dialog(this);
    dialog.setWindowTitle("Saved passwords");
    dialog.resize(520, 400);
    auto* layout = new QVBoxLayout(&dialog);
    auto* list = new QListWidget(&dialog);
    auto reload = [&] {
        list->clear();
        for (const auto& row : m_passwords.all())
            list->addItem(row.host + "  —  " + row.user);
    };
    reload();
    layout->addWidget(new QLabel("AES-256-GCM vault. Unlocks with your master password.", &dialog));
    layout->addWidget(list);
    auto* row = new QHBoxLayout;
    auto* addBtn = new QPushButton("Add", &dialog);
    auto* delBtn = new QPushButton("Delete", &dialog);
    auto* chgBtn = new QPushButton("Change master", &dialog);
    auto* lockBtn = new QPushButton("Lock", &dialog);
    auto* expBtn = new QPushButton("Export", &dialog);
    auto* impBtn = new QPushButton("Import", &dialog);
    auto* closeBtn = new QPushButton("Close", &dialog);
    for (auto* b : {addBtn, delBtn, chgBtn, lockBtn, expBtn, impBtn})
        row->addWidget(b);
    row->addStretch();
    row->addWidget(closeBtn);
    layout->addLayout(row);

    connect(addBtn, &QPushButton::clicked, &dialog, [&] {
        bool ok = false;
        const QString host = QInputDialog::getText(&dialog, "Add", "Site host:", QLineEdit::Normal, {}, &ok);
        if (!ok || host.isEmpty()) return;
        const QString user = QInputDialog::getText(&dialog, "Add", "Username:", QLineEdit::Normal, {}, &ok);
        if (!ok) return;
        const QString pass = QInputDialog::getText(&dialog, "Add", "Password:", QLineEdit::Password, {}, &ok);
        if (!ok) return;
        m_passwords.upsert({host, user, pass});
        reload();
    });
    connect(delBtn, &QPushButton::clicked, &dialog, [&] {
        auto* item = list->currentItem();
        if (!item) return;
        m_passwords.removeHost(item->text().section("  —  ", 0, 0));
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
    QMessageBox::information(this, "Privacy", "Cookies and cache cleared for this profile.");
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
    page = nullptr;

    const int i = m_tabs->addTab(scroll, QIcon(":/icons/settings.svg"), "Settings");
    m_tabs->setCurrentIndex(i);
}


void MainWindow::showFindBar()
{
    m_findBar->show();
    m_findInput->setFocus();
    m_findInput->selectAll();
    reloadFind();
}
void MainWindow::hideFindBar()
{
    m_findBar->hide();
    if (auto* v = currentView()) v->findText(QString());
}
void MainWindow::reloadFind()
{
    if (m_findBar->isVisible())
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
    m_downloadBar->show();
    auto* item = new QListWidgetItem(download->downloadFileName() + " — starting", m_downloadList);

    connect(download, &QWebEngineDownloadRequest::receivedBytesChanged, this, [download, item] {
        const qint64 total = download->totalBytes();
        const qint64 got = download->receivedBytes();
        QString extra = total > 0
            ? QString("%1%").arg(int((got * 100) / total))
            : QString("%1 KB").arg(got / 1024);
        item->setText(download->downloadFileName() + " — " + extra);
    });
    connect(download, &QWebEngineDownloadRequest::isFinishedChanged, this, [download, item] {
        if (!download->isFinished()) return;
        QString state = "done";
        if (download->state() == QWebEngineDownloadRequest::DownloadCancelled)
            state = "cancelled";
        else if (download->state() == QWebEngineDownloadRequest::DownloadInterrupted)
            state = "failed";
        item->setText(download->downloadFileName() + " — " + state);
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
    setWindowTitle(title.isEmpty() ? "Inco Browser" : title + " — Inco Browser");
}
void MainWindow::applyDarkTheme()
{
    setStyleSheet(R"(
        QMainWindow, QDialog { background: #0e0e0e; color: #ececec; }
        QWidget { outline: 0; }
        QToolBar {
            background: #121212;
            border: 0;
            padding: 6px 8px 2px 8px;
            spacing: 4px;
            margin: 0;
        }
        QTabWidget { margin: 0; padding: 0; background: #0e0e0e; }
        QTabWidget::pane { border: 0; margin: 0; padding: 0; top: 0; }
        QTabBar { background: #0e0e0e; }
        QTabBar::tab {
            background: #161616;
            color: #bdbdbd;
            padding: 7px 8px 7px 10px;
            margin: 0 3px 0 0;
            border: 0;
            border-radius: 8px;
            min-width: 80px;
        }
        QTabBar::tab:selected { background: #2a2a2a; color: #fff; }
        QTabBar::tab:hover:!selected { background: #1d1d1d; }
        QTabBar::tab:focus { outline: 0; }
        QTabBar::close-button {
            image: url(:/icons/close.svg);
            width: 12px;
            height: 12px;
            margin: 0 1px 0 4px;
            background: transparent;
            border-radius: 6px;
        }
        QTabBar::close-button:hover { background: #444; }
        QPushButton {
            background: #1c1c1c; color: #eee; border: 0;
            border-radius: 10px; padding: 7px 10px;
        }
        QPushButton:hover { background: #2a2a2a; }
        QPushButton:disabled { color: #555; }
        QLineEdit {
            background: #1a1a1a; color: #f0f0f0;
            border: 1px solid #2a2a2a; border-radius: 18px; padding: 8px 14px;
        }
        QListWidget { background: #161616; border: 0; border-radius: 12px; }
        QCheckBox, QLabel { color: #ddd; }
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
        m_muteButton->setToolTip(muted ? "Unmute tab" : "Mute tab");
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
        close();
        return;
    }

    const QString exe = QCoreApplication::applicationFilePath();
    QProcess proc;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QByteArray torDir = qgetenv("INCO_TOR_DIR");
    if (!torDir.isEmpty())
        env.insert("INCO_TOR_DIR", QString::fromLocal8Bit(torDir));
    else
        env.insert("INCO_TOR_DIR", QFileInfo(exe).absolutePath() + "/tor");
    proc.setProgram(exe);
    proc.setArguments({"--anon", "--tor"});
    proc.setProcessEnvironment(env);
    if (!proc.startDetached()) {
        QMessageBox::warning(this, "Onionize",
            "Could not start the Tor window.\nInstall tor:\n  sudo dnf install tor");
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
    if (!m_settings.bookmarksEnabled || !m_settings.showBookmarksOnHome)
        return;

    QString items;
    int n = 0;
    for (const auto& bm : m_bookmarks.all()) {
        if (n++ >= 16)
            break;
        QString title = bm.title;
        if (title.isEmpty())
            title = bm.url;
        title = title.left(32);
        title.replace('\\', "\\\\").replace('\'', "\\'");
        QString href = bm.url;
        href.replace('\\', "\\\\").replace('\'', "\\'");
        items += "<a href='" + href + "'>" + title.toHtmlEscaped() + "</a>";
    }
    const QString js = QStringLiteral(
        "(function(){const e=document.getElementById('pins'); if(!e) return; e.innerHTML='%1';})();"
    ).arg(items.replace(QStringLiteral("\""), QStringLiteral("\\\"")));
    view->page()->runJavaScript(js);
}


bool MainWindow::ensureVaultUnlocked()
{
    if (m_passwords.isUnlocked())
        return true;
    bool ok = false;
    const QString pw = QInputDialog::getText(
        this, "Vault",
        m_passwords.exists() ? "Master password" : "Create master password",
        QLineEdit::Password, {}, &ok);
    if (!ok || pw.isEmpty())
        return false;
    if (m_passwords.exists()) {
        if (!m_passwords.unlock(pw)) {
            QMessageBox::warning(this, "Vault", "Wrong password.");
            return false;
        }
    } else if (!m_passwords.create(pw)) {
        QMessageBox::warning(this, "Vault", "Could not create vault.");
        return false;
    }
    return m_passwords.isUnlocked();
}


void MainWindow::checkTorStatus()
{
    if (!m_torLabel)
        return;
    m_torLabel->setText("Onionize: checking Tor…");
    auto* nam = new QNetworkAccessManager(this);
    QNetworkProxy proxy(QNetworkProxy::Socks5Proxy, QStringLiteral("127.0.0.1"), 9250);
    nam->setProxy(proxy);
    QNetworkRequest req(QUrl(QStringLiteral("https://check.torproject.org/api/ip")));
    req.setTransferTimeout(12000);
    auto* reply = nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam] {
        const auto body = reply->readAll();
        const auto obj = QJsonDocument::fromJson(body).object();
        const bool ok = obj.value(QLatin1String("IsTor")).toBool();
        if (m_torLabel) {
            if (reply->error() != QNetworkReply::NoError)
                m_torLabel->setText("Onionize: SOCKS issue — " + reply->errorString());
            else if (ok)
                m_torLabel->setText("Onionize: connected to Tor");
            else
                m_torLabel->setText("Onionize: proxy up, but this is not a Tor exit");
        }
        reply->deleteLater();
        nam->deleteLater();
    });
}
