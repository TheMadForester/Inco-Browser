#include "SettingsPage.hpp"
#include "MainWindow.hpp"
#include "I18n.hpp"
#include <QFileInfo>

#include <QCheckBox>
#include <QCoreApplication>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QVBoxLayout>

SettingsPage::SettingsPage(IncoSettings* settings, BrowserProfile* profile,
                           SettingsStore* store, MainWindow* main, QWidget* parent)
    : QWidget(parent), m_settings(settings), m_profile(profile),
      m_store(store), m_main(main)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(40, 28, 40, 28);
    root->setSpacing(14);

    auto* title = new QLabel("Settings");
    title->setStyleSheet("font-size: 28px; font-weight: 600; color: #fff;");
    root->addWidget(title);

    auto* privacy = new QGroupBox("Privacy");
    auto* p = new QVBoxLayout(privacy);
    m_cookies = addCheck(p, "Clear cookies when Inco closes", settings->clearCookiesOnExit);
    m_cache = addCheck(p, "Clear cache when Inco closes", settings->clearCacheOnExit);
    m_popups = addCheck(p, "Block popups", settings->blockPopups);
    m_logins = addCheck(p, "Keep website logins (persistent cookies)", settings->keepLogins);
    m_https = addCheck(p, "HTTPS-only (upgrade http to https)", settings->httpsOnly);
    /* filter master is the group if we wrap it */ 
    m_filter = addCheck(p, "Inco Filter (block known ad/tracker hosts)", settings->incoFilter);
    p->addWidget(new QLabel("Also block these hosts (one per line)"));
    m_filterExtra = new QPlainTextEdit(settings->filterExtra);
    m_filterExtra->setPlaceholderText("tracker.example.com");
    m_filterExtra->setFixedHeight(90);
    p->addWidget(m_filterExtra);
    p->addWidget(new QLabel("Never block these hosts"));
    m_filterAllow = new QPlainTextEdit(settings->filterAllow);
    m_filterAllow->setPlaceholderText("cdn.example.com");
    m_filterAllow->setFixedHeight(70);
    p->addWidget(m_filterAllow);
    connect(m_filterExtra, &QPlainTextEdit::textChanged, this, [this] { persist(); });
    connect(m_filterAllow, &QPlainTextEdit::textChanged, this, [this] { persist(); });
    auto* wipeBtn = new QPushButton("Clear cookies & cache now");
    p->addWidget(wipeBtn);
    root->addWidget(privacy);

    m_passGroup = new QGroupBox("Passwords");
    m_passGroup->setCheckable(true);
    m_passGroup->setChecked(settings->savePasswords);
    auto* l = new QVBoxLayout(m_passGroup);
    m_autofill = addCheck(l, "Autofill when vault is unlocked (exact host, one match)", settings->autofillLogins);
    auto* vaultBtn = new QPushButton("Change vault master password");
    l->addWidget(vaultBtn);
    root->addWidget(m_passGroup);

    m_bmGroup = new QGroupBox("Bookmarks");
    m_bmGroup->setCheckable(true);
    m_bmGroup->setChecked(settings->bookmarksEnabled);
    auto* b = new QVBoxLayout(m_bmGroup);
    m_bmHome = addCheck(b, "Show bookmarks on the new tab page", settings->showBookmarksOnHome);
    b->addWidget(new QLabel("Star a page in the toolbar to pin it here."));
    root->addWidget(m_bmGroup);

    auto* search = new QGroupBox("Search");
    auto* s = new QVBoxLayout(search);
    s->addWidget(new QLabel("Search URL (%s = query)"));
    m_search = new QLineEdit(settings->searchTemplate);
    s->addWidget(m_search);
    root->addWidget(search);

    auto* files = new QGroupBox("Files");
    auto* f = new QVBoxLayout(files);
    m_dirBtn = new QPushButton("Download folder: " + settings->downloadDir);
    f->addWidget(m_dirBtn);
    root->addWidget(files);

    auto* anon = new QGroupBox("Anonymous sessions");
    auto* a = new QVBoxLayout(anon);
    m_tor = addCheck(a, "Route anonymous windows through system Tor (127.0.0.1:9050)", settings->useTorForAnon);
    const QString torBin = QFileInfo(QCoreApplication::applicationDirPath() + "/../tor/tor").exists()
        ? QCoreApplication::applicationDirPath() + "/../tor/tor"
        : QStringLiteral("/usr/bin/tor");
    const bool haveTor = QFileInfo(torBin).isExecutable() || QFileInfo(QStringLiteral("/usr/bin/tor")).isExecutable();
    a->addWidget(new QLabel(haveTor ? I18n::tr("tor.status.idle") : I18n::tr("tor.status.missing")));
    a->addWidget(new QLabel(I18n::tr("tor.help")));
    auto* ver = new QLabel("Bundled Tor: checking…");
    auto* chk = new QPushButton("Check for Tor update");
    auto* upd = new QPushButton("Update bundled Tor");
    a->addWidget(ver);
    a->addWidget(chk);
    a->addWidget(upd);

    auto readLocal = [ver] {
        const QString bin = QCoreApplication::applicationDirPath() + "/tor/tor";
        if (!QFileInfo::exists(bin)) {
            ver->setText("Bundled Tor: not found. Run fetch-tor.sh");
            return;
        }
        QProcess p;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("LD_LIBRARY_PATH", QCoreApplication::applicationDirPath() + "/tor");
        p.setProcessEnvironment(env);
        p.start(bin, {"--version"});
        p.waitForFinished(3000);
        ver->setText("Bundled Tor: " + QString::fromUtf8(p.readAllStandardOutput()).trimmed());
    };
    readLocal();

    connect(chk, &QPushButton::clicked, this, [ver] {
        auto* nam = new QNetworkAccessManager(ver);
        QNetworkRequest req(QUrl("https://www.torproject.org/download/tor/"));
        auto* reply = nam->get(req);
        QObject::connect(reply, &QNetworkReply::finished, ver, [ver, reply, nam] {
            const QString html = QString::fromUtf8(reply->readAll());
            const QRegularExpression re("tor-expert-bundle-linux-x86_64-([0-9.]+)\.tar\.gz");
            const auto m = re.match(html);
            if (m.hasMatch())
                ver->setText(ver->text() + "\nLatest expert bundle: " + m.captured(1));
            else
                ver->setText(ver->text() + "\nCould not parse latest version.");
            reply->deleteLater();
            nam->deleteLater();
        });
    });
    connect(upd, &QPushButton::clicked, this, [this, readLocal, ver] {
        const QString script = QCoreApplication::applicationDirPath() + "/fetch-tor.sh";
        if (!QFileInfo::exists(script)) {
            ver->setText("fetch-tor.sh missing next to the binary");
            return;
        }
        ver->setText("Updating… watch the terminal.");
        auto* p = new QProcess(this);
        p->setProcessChannelMode(QProcess::ForwardedChannels);
        p->setWorkingDirectory(QCoreApplication::applicationDirPath() + "/..");
        // project root if running from build/
        const QString root = QFileInfo(QCoreApplication::applicationDirPath() + "/../scripts/fetch-tor.sh").exists()
            ? QCoreApplication::applicationDirPath() + "/.."
            : QCoreApplication::applicationDirPath();
        p->setWorkingDirectory(root);
        QObject::connect(p, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                         this, [readLocal, ver](int code, QProcess::ExitStatus) {
            ver->setText(code == 0 ? "Update finished. Restart Onionize." : "Update failed.");
            readLocal();
        });
        p->start("bash", {root + "/scripts/fetch-tor.sh"});
    });
    root->addWidget(anon);


    auto* note = new QLabel(
        "Data dir: " + QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) +
        "\nNo Tor. No VPN. Privacy here means local residue."
    );
    note->setWordWrap(true);
    note->setStyleSheet("color:#777;");
    root->addWidget(note);
    root->addStretch();

    for (auto* box : {m_autofill, m_cookies, m_cache, m_popups, m_logins, m_https, m_filter})
        connect(box, &QCheckBox::toggled, this, [this](bool) { persist(); });
    connect(m_search, &QLineEdit::editingFinished, this, [this] { persist(); });
    connect(m_tor, &QCheckBox::toggled, this, [this](bool) { persist(); });
    connect(vaultBtn, &QPushButton::clicked, m_main, &MainWindow::changeMasterPassword);
    connect(m_passGroup, &QGroupBox::toggled, this, [this](bool) { persist(); });
    connect(m_bmGroup, &QGroupBox::toggled, this, [this](bool) { persist(); });
    connect(wipeBtn, &QPushButton::clicked, m_main, &MainWindow::wipeNow);
    connect(m_dirBtn, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, "Downloads", m_settings->downloadDir);
        if (dir.isEmpty()) return;
        m_settings->downloadDir = dir;
        m_dirBtn->setText("Download folder: " + dir);
        m_store->save(*m_settings);
    });
}

QCheckBox* SettingsPage::addCheck(QVBoxLayout* layout, const QString& label, bool on)
{
    auto* box = new QCheckBox(label);
    box->setChecked(on);
    layout->addWidget(box);
    return box;
}

void SettingsPage::persist()
{
    m_settings->savePasswords = m_passGroup && m_passGroup->isChecked();
    m_settings->autofillLogins = m_autofill->isChecked();
    m_settings->bookmarksEnabled = m_bmGroup && m_bmGroup->isChecked();
        m_settings->showBookmarksOnHome = m_bmHome && m_bmHome->isChecked();
    m_settings->clearCookiesOnExit = m_cookies->isChecked();
    m_settings->clearCacheOnExit = m_cache->isChecked();
    m_settings->blockPopups = m_popups->isChecked();
    m_settings->keepLogins = m_logins->isChecked();
    m_settings->httpsOnly = m_https->isChecked();
    m_settings->incoFilter = m_filter->isChecked();
        m_settings->filterExtra = m_filterExtra->toPlainText();
        m_settings->filterAllow = m_filterAllow->toPlainText();
    m_settings->searchTemplate = m_search->text().trimmed();
    m_settings->useTorForAnon = m_tor->isChecked();
    if (m_settings->keepLogins)
        m_settings->clearCookiesOnExit = false;
    m_profile->apply(*m_settings);
    m_store->save(*m_settings);
    m_main->updateBookmarkButtons();
}
