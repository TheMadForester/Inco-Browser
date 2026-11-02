#include "SettingsPage.hpp"
#include "SearchEngine.hpp"
#include "MainWindow.hpp"
#include "I18n.hpp"
#include "TorControl.hpp"
#include "TorIntegrity.hpp"
#include "Motion.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStyle>
#include <QSysInfo>
#include <QVariantAnimation>
#include <QVBoxLayout>
#include <QWebEngineExtensionInfo>

namespace {

// Same palette as newtab/about pages: #0e0e0e page, #151515 cards,
// #2a2a2a borders, 10–14px radii; amber accent shared with the Onion button.
const char* kSettingsStyle = R"(
#settingsRoot { background: #0e0e0e; }
#settingsRoot QLabel { color: #d6d6d6; font-size: 13px; background: transparent; }
#settingsRoot QLabel#pageTitle { font-size: 26px; font-weight: 600; color: #f2f2f2; }
#settingsRoot QLabel#pageSubtitle { color: #7d7d7d; font-size: 13px; }
#settingsRoot QFrame#card { background: #151515; border: 1px solid #222222; border-radius: 14px; }
#settingsRoot QLabel#sectionTitle { font-size: 15px; font-weight: 600; color: #f0f0f0; }
#settingsRoot QLabel#rowTitle { color: #e4e4e4; font-size: 13px; }
#settingsRoot QLabel#rowDesc, #settingsRoot QLabel#hint { color: #8a8a8a; font-size: 12px; }
#settingsRoot QLabel#fieldLabel { color: #a8a8a8; font-size: 12px; font-weight: 600; }
#settingsRoot QLabel#mono { color: #9a9a9a; font-family: monospace; font-size: 12px; }
#settingsRoot QLabel#footer { color: #6f6f6f; font-size: 12px; }
#settingsRoot QLabel#rowError { color: #f2b8ab; font-size: 12px; }
#settingsRoot QFrame#divider { background: #222222; border: 0; min-height: 1px; max-height: 1px; }
#settingsRoot QLabel#statusOk {
    color: #a6dcb0; background: #12221a; border: 1px solid #1f3a29;
    border-radius: 8px; padding: 6px 10px; font-size: 12px; font-weight: 600;
}
#settingsRoot QLabel#statusBad {
    color: #f2b8ab; background: #261412; border: 1px solid #44231e;
    border-radius: 8px; padding: 6px 10px; font-size: 12px; font-weight: 600;
}
#settingsRoot QCheckBox { spacing: 0; background: transparent; }
#settingsRoot QCheckBox::indicator { width: 38px; height: 22px; image: url(:/icons/toggle-off.svg); }
#settingsRoot QCheckBox::indicator:checked { image: url(:/icons/toggle-on.svg); }
#settingsRoot QCheckBox::indicator:disabled { image: url(:/icons/toggle-off-disabled.svg); }
#settingsRoot QCheckBox::indicator:checked:disabled { image: url(:/icons/toggle-on-disabled.svg); }
#settingsRoot QLineEdit, #settingsRoot QPlainTextEdit, #settingsRoot QComboBox, #settingsRoot QSpinBox {
    background: #101010; color: #eeeeee; border: 1px solid #2a2a2a;
    border-radius: 10px; padding: 8px 12px; font-size: 13px;
    selection-background-color: #3a2a12; selection-color: #f0d9a0;
}
#settingsRoot QLineEdit:focus, #settingsRoot QPlainTextEdit:focus, #settingsRoot QComboBox:focus { border-color: #4a4a4a; }
#settingsRoot QLineEdit:disabled, #settingsRoot QPlainTextEdit:disabled { color: #5c5c5c; border-color: #1e1e1e; }
#settingsRoot QSpinBox { min-width: 90px; }
#settingsRoot QPlainTextEdit { font-family: monospace; font-size: 12px; padding: 6px 8px; }
#settingsRoot QComboBox { padding-right: 30px; min-width: 220px; }
#settingsRoot QComboBox::drop-down { border: 0; width: 28px; subcontrol-origin: padding; subcontrol-position: center right; }
#settingsRoot QComboBox::down-arrow { image: url(:/icons/chevron-down.svg); width: 12px; height: 12px; }
#settingsRoot QComboBox QAbstractItemView {
    background: #1a1a1a; color: #eeeeee; border: 1px solid #2a2a2a;
    selection-background-color: #2a2a2a; outline: 0; padding: 4px;
}
#settingsRoot QPushButton {
    background: #1f1f1f; color: #e8e8e8; border: 1px solid #2c2c2c;
    border-radius: 10px; padding: 8px 14px; font-size: 13px;
}
#settingsRoot QPushButton:hover { background: #272727; border-color: #383838; }
#settingsRoot QPushButton:pressed { background: #1a1a1a; }
#settingsRoot QPushButton:disabled { color: #5a5a5a; background: #171717; border-color: #212121; }
#settingsRoot QPushButton#dangerBtn { color: #f2b8ab; }
#settingsRoot QPushButton#dangerBtn:hover { background: #2a1714; border-color: #44231e; }
)";

// "&" would become a mnemonic underline in QPushButton text.
QString buttonText(QString t) { return t.replace(QLatin1Char('&'), QLatin1String("&&")); }

// One status line under the install buttons, red for failures.
void setStatus(QLabel* label, const QString& text, bool error)
{
    label->setObjectName(error ? "rowError" : "hint");
    label->style()->unpolish(label);
    label->style()->polish(label);
    label->setText(text);
    label->show();
}

// Toggle switch with the same look as the old toggle-*.svg icons, but the
// knob and colors glide between states instead of jumping.
class SwitchBox final : public QCheckBox
{
public:
    SwitchBox()
    {
        setCursor(Qt::PointingHandCursor);
        setFixedSize(38, 22);
        m_anim = new QVariantAnimation(this);
        m_anim->setDuration(Motion::kFast);
        m_anim->setEasingCurve(QEasingCurve::OutCubic);
        connect(m_anim, &QVariantAnimation::valueChanged, this, [this] { update(); });
        connect(this, &QCheckBox::toggled, this, [this](bool on) {
            const qreal from = m_anim->state() == QAbstractAnimation::Running
                ? m_anim->currentValue().toReal() : (on ? 0.0 : 1.0);
            m_anim->stop();
            m_anim->setStartValue(from);
            m_anim->setEndValue(on ? 1.0 : 0.0);
            m_anim->start();
        });
    }
    QSize sizeHint() const override { return {38, 22}; }
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    bool hitButton(const QPoint& pos) const override { return rect().contains(pos); }
    void paintEvent(QPaintEvent*) override
    {
        const qreal t = m_anim->state() == QAbstractAnimation::Running
            ? m_anim->currentValue().toReal() : (isChecked() ? 1.0 : 0.0);
        const bool en = isEnabled();
        const auto mix = [t](const QColor& a, const QColor& b) {
            return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                                    a.greenF() + (b.greenF() - a.greenF()) * t,
                                    a.blueF() + (b.blueF() - a.blueF()) * t);
        };
        const QColor trackOff = en ? QColor("#262626") : QColor("#1a1a1a");
        const QColor strokeOff = en ? QColor("#363636") : QColor("#242424");
        const QColor knobOff = en ? QColor("#9a9a9a") : QColor("#444444");
        const QColor trackOn = en ? QColor("#e6c07b") : QColor("#4a3f2a");
        const QColor knobOn = en ? QColor("#141414") : QColor("#1a1a1a");
        QColor track = mix(trackOff, trackOn);
        if (en && underMouse())
            track = track.lighter(112);

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(mix(strokeOff, trackOn), 1));
        p.setBrush(track);
        p.drawRoundedRect(QRectF(0.5, 0.5, 37, 21), 10.5, 10.5);
        p.setPen(Qt::NoPen);
        p.setBrush(mix(knobOff, knobOn));
        p.drawEllipse(QPointF(11 + 16 * t, 11), 7.5, 7.5);
        if (hasFocus()) {
            p.setPen(QPen(QColor(230, 192, 123, 110), 1));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(QRectF(0.5, 0.5, 37, 21), 10.5, 10.5);
        }
    }
    void enterEvent(QEnterEvent* e) override { update(); QCheckBox::enterEvent(e); }
    void leaveEvent(QEvent* e) override { update(); QCheckBox::leaveEvent(e); }

private:
    QVariantAnimation* m_anim = nullptr;
};

// Whole row is clickable, toggle sits on the right.
class ToggleRow final : public QWidget
{
public:
    ToggleRow(const QString& title, const QString& desc, bool on, QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setCursor(Qt::PointingHandCursor);
        auto* lay = new QHBoxLayout(this);
        lay->setContentsMargins(0, 10, 0, 10);
        lay->setSpacing(16);
        auto* text = new QVBoxLayout;
        text->setSpacing(2);
        auto* t = new QLabel(title);
        t->setObjectName("rowTitle");
        t->setWordWrap(true);
        text->addWidget(t);
        if (!desc.isEmpty()) {
            auto* d = new QLabel(desc);
            d->setObjectName("rowDesc");
            d->setWordWrap(true);
            text->addWidget(d);
        }
        lay->addLayout(text, 1);
        m_box = new SwitchBox;
        m_box->setChecked(on);
        m_box->setAccessibleName(title);
        lay->addWidget(m_box, 0, Qt::AlignVCenter);
    }
    QCheckBox* box() const { return m_box; }

protected:
    void mouseReleaseEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::LeftButton && m_box->isEnabled() && rect().contains(e->position().toPoint()))
            m_box->toggle();
        QWidget::mouseReleaseEvent(e);
    }

private:
    QCheckBox* m_box = nullptr;
};

} // namespace

SettingsPage::SettingsPage(IncoSettings* settings, BrowserProfile* profile,
                           SettingsStore* store, MainWindow* main, QWidget* parent)
    : QWidget(parent), m_settings(settings), m_profile(profile),
      m_store(store), m_main(main)
{
    setObjectName("settingsRoot");
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QString::fromUtf8(kSettingsStyle));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // Centered, readable column (like the newtab/about pages).
    auto* outer = new QHBoxLayout(this);
    outer->setContentsMargins(24, 0, 24, 0);
    auto* columnHost = new QWidget;
    columnHost->setMaximumWidth(760);
    columnHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    outer->addStretch(1);
    outer->addWidget(columnHost, 100);
    outer->addStretch(1);

    auto* root = new QVBoxLayout(columnHost);
    root->setContentsMargins(0, 36, 0, 40);
    root->setSpacing(16);

    // Header
    auto* title = new QLabel(I18n::tr("settings"));
    title->setObjectName("pageTitle");
    auto* subtitle = new QLabel(I18n::tr("settings.subtitle"));
    subtitle->setObjectName("pageSubtitle");
    root->addWidget(title);
    root->addWidget(subtitle);
    root->addSpacing(6);

    // Language
    {
        auto* card = addCard(root, I18n::tr("settings.language"));
        auto* lay = qobject_cast<QVBoxLayout*>(card->layout());
        auto* langCombo = new QComboBox;
        langCombo->setCursor(Qt::PointingHandCursor);
        langCombo->addItem(I18n::tr("lang.system"), "system");
        langCombo->addItem(I18n::tr("lang.en"), "en");
        langCombo->addItem(I18n::tr("lang.de"), "de");
        langCombo->addItem(I18n::tr("lang.fr"), "fr");
        langCombo->addItem(I18n::tr("lang.it"), "it");
        langCombo->addItem(I18n::tr("lang.zh"), "zh");
        const int idx = langCombo->findData(settings->language);
        langCombo->setCurrentIndex(idx >= 0 ? idx : 0);
        auto* row = new QHBoxLayout;
        row->addWidget(langCombo);
        row->addStretch();
        lay->addLayout(row);
        connect(langCombo, &QComboBox::currentIndexChanged, this, [this, langCombo] {
            m_settings->language = langCombo->currentData().toString();
            I18n::setLanguage(m_settings->language);
            persist();
            m_main->refreshSettings();
        });
    }

    // Privacy
    QPushButton* wipeBtn = nullptr;
    {
        auto* card = addCard(root, I18n::tr("settings.privacy"));
        auto* p = qobject_cast<QVBoxLayout*>(card->layout());
        m_cookies = addToggle(p, I18n::tr("clear.cookies"), settings->clearCookiesOnExit);
        p->addWidget(divider());
        m_cache = addToggle(p, I18n::tr("clear.cache"), settings->clearCacheOnExit);
        p->addWidget(divider());
        m_popups = addToggle(p, I18n::tr("block.popups"), settings->blockPopups);
        p->addWidget(divider());
        m_logins = addToggle(p, I18n::tr("keep.logins"), settings->keepLogins);
        m_restartHint = new QLabel(I18n::tr("keep.logins.restart"));
        m_restartHint->setObjectName("hint");
        m_restartHint->setWordWrap(true);
        m_restartHint->setContentsMargins(0, 0, 0, 8);
        p->addWidget(m_restartHint);
        p->addWidget(divider());
        m_https = addToggle(p, I18n::tr("https.only"), settings->httpsOnly);
        p->addWidget(divider());
        m_filter = addToggle(p, I18n::tr("inco.filter"), settings->incoFilter);

        auto* lists = new QWidget;
        auto* ll = new QVBoxLayout(lists);
        ll->setContentsMargins(0, 4, 0, 0);
        ll->setSpacing(6);
        m_ubolLists = addToggle(ll, I18n::tr("filter.ubol"), settings->ubolLists);
        auto* ubolNote = new QLabel(I18n::tr("filter.ubol.note"));
        ubolNote->setObjectName("hint");
        ubolNote->setWordWrap(true);
        ll->addWidget(ubolNote);
        ll->addSpacing(6);
        ll->addWidget(fieldLabel(I18n::tr("filter.extra")));
        m_filterExtra = new QPlainTextEdit(settings->filterExtra);
        m_filterExtra->setPlaceholderText("tracker.example.com");
        m_filterExtra->setFixedHeight(88);
        ll->addWidget(m_filterExtra);
        ll->addSpacing(6);
        ll->addWidget(fieldLabel(I18n::tr("filter.allow")));
        m_filterAllow = new QPlainTextEdit(settings->filterAllow);
        m_filterAllow->setPlaceholderText("cdn.example.com");
        m_filterAllow->setFixedHeight(70);
        ll->addWidget(m_filterAllow);
        p->addWidget(lists);
        lists->setEnabled(m_filter->isChecked());
        connect(m_filter, &QCheckBox::toggled, lists, &QWidget::setEnabled);
        // Sealed in settings.bin and the vault is locked: show, do not edit.
        m_filterExtra->setEnabled(!settings->sensitiveLocked);
        m_filterAllow->setEnabled(!settings->sensitiveLocked);
        connect(m_filterExtra, &QPlainTextEdit::textChanged, this, [this] { persist(); });
        connect(m_filterAllow, &QPlainTextEdit::textChanged, this, [this] { persist(); });

        p->addSpacing(8);
        wipeBtn = new QPushButton(buttonText(I18n::tr("wipe.now")));
        wipeBtn->setObjectName("dangerBtn");
        wipeBtn->setCursor(Qt::PointingHandCursor);
        auto* row = new QHBoxLayout;
        row->addWidget(wipeBtn);
        row->addStretch();
        p->addLayout(row);
    }

    // Passwords (header toggle enables the card body)
    QPushButton* vaultBtn = nullptr;
    {
        auto* card = addCard(root, I18n::tr("settings.passwords"), &m_passwords, settings->savePasswords);
        auto* lay = qobject_cast<QVBoxLayout*>(card->layout());
        auto* body = new QWidget;
        auto* l = new QVBoxLayout(body);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(0);
        m_autofill = addToggle(l, I18n::tr("autofill"), settings->autofillLogins);
        l->addSpacing(8);
        vaultBtn = new QPushButton(buttonText(I18n::tr("change.vault")));
        vaultBtn->setCursor(Qt::PointingHandCursor);
        auto* row = new QHBoxLayout;
        row->addWidget(vaultBtn);
        row->addStretch();
        l->addLayout(row);
        lay->addWidget(body);
        body->setEnabled(m_passwords->isChecked());
        connect(m_passwords, &QCheckBox::toggled, body, &QWidget::setEnabled);
    }

    // Vault lock: auto-lock and sealed settings
    buildVaultLockSection(root);

    // Bookmarks
    {
        auto* card = addCard(root, I18n::tr("settings.bookmarks"), &m_bookmarks, settings->bookmarksEnabled);
        auto* lay = qobject_cast<QVBoxLayout*>(card->layout());
        auto* body = new QWidget;
        auto* b = new QVBoxLayout(body);
        b->setContentsMargins(0, 0, 0, 0);
        b->setSpacing(0);
        m_bmHome = addToggle(b, I18n::tr("show.bookmarks"), settings->showBookmarksOnHome,
                             I18n::tr("bookmarks.hint"));
        b->addWidget(divider());
        m_lockBm = addToggle(b, I18n::tr("lock.bookmarks"), settings->lockBookmarks,
                             I18n::tr("lock.bookmarks.d"));
        lay->addWidget(body);
        body->setEnabled(m_bookmarks->isChecked());
        connect(m_bookmarks, &QCheckBox::toggled, body, &QWidget::setEnabled);
    }

    // Search
    {
        auto* card = addCard(root, I18n::tr("settings.search"));
        auto* s = qobject_cast<QVBoxLayout*>(card->layout());
        const bool editable = !settings->sensitiveLocked;
        s->addWidget(fieldLabel(I18n::tr("search.engine")));
        m_engine = new QComboBox;
        m_engine->setCursor(Qt::PointingHandCursor);
        m_engine->setAccessibleName(I18n::tr("search.engine"));
        m_engine->addItem(I18n::tr("search.ddg"), QStringLiteral("ddg"));
        m_engine->addItem(QStringLiteral("Startpage"), QStringLiteral("startpage"));
        // Custom only for an address the user already had, so it is not lost.
        if (settings->searchEngine == QLatin1String("custom")
            || SearchEngine::detect(settings->searchTemplate) == QLatin1String("custom"))
            m_engine->addItem(I18n::tr("search.custom"), QStringLiteral("custom"));
        const int idx = m_engine->findData(settings->searchEngine);
        m_engine->setCurrentIndex(idx >= 0 ? idx : 0);
        m_engine->setEnabled(editable);
        auto* row = new QHBoxLayout;
        row->addWidget(m_engine);
        row->addStretch();
        s->addLayout(row);

        if (m_engine->findData(QStringLiteral("custom")) >= 0) {
            m_customBox = new QWidget;
            auto* cx = new QVBoxLayout(m_customBox);
            cx->setContentsMargins(0, 8, 0, 0);
            cx->setSpacing(0);
            cx->addWidget(fieldLabel(I18n::tr("search.url")));
            m_search = new QLineEdit(settings->searchTemplate);
            m_search->setPlaceholderText("https://duckduckgo.com/?q=%s");
            m_search->setAccessibleName(I18n::tr("search.url"));
            m_search->setEnabled(editable);
            cx->addWidget(m_search);
            s->addWidget(m_customBox);
        }

        m_searchFallback = new QLabel(I18n::tr("search.fallback"));
        m_searchFallback->setObjectName("hint");
        m_searchFallback->setWordWrap(true);
        s->addSpacing(6);
        s->addWidget(m_searchFallback);
        updateSearchFields();
    }

    // Files
    {
        auto* card = addCard(root, I18n::tr("settings.files"));
        auto* f = qobject_cast<QVBoxLayout*>(card->layout());
        QString label = I18n::tr("download.folder").trimmed();
        if (label.endsWith(QLatin1Char(':')) || label.endsWith(QChar(0xFF1A)))
            label.chop(1);
        label = label.trimmed();
        auto* row = new QHBoxLayout;
        row->setSpacing(16);
        auto* text = new QVBoxLayout;
        text->setSpacing(2);
        auto* t = new QLabel(label);
        t->setObjectName("rowTitle");
        m_dirLabel = new QLabel(settings->downloadDir);
        m_dirLabel->setObjectName("mono");
        m_dirLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_dirLabel->setWordWrap(true);
        text->addWidget(t);
        text->addWidget(m_dirLabel);
        row->addLayout(text, 1);
        m_dirBtn = new QPushButton(buttonText(I18n::tr("choose.folder")));
        m_dirBtn->setCursor(Qt::PointingHandCursor);
        m_dirBtn->setEnabled(!settings->sensitiveLocked);
        row->addWidget(m_dirBtn, 0, Qt::AlignVCenter);
        f->addLayout(row);
    }

    // Performance
    {
        auto* card = addCard(root, I18n::tr("settings.performance"));
        auto* lay = qobject_cast<QVBoxLayout*>(card->layout());
        m_lowRam = addToggle(lay, I18n::tr("low.ram"), settings->lowRam, I18n::tr("low.ram.d"));
        // main() put the Chromium flags of this run into the environment.
        const bool activeNow = qgetenv("QTWEBENGINE_CHROMIUM_FLAGS").contains("--enable-low-end-device-mode");
        auto* hint = new QLabel(I18n::tr("low.ram.restart"));
        hint->setObjectName("hint");
        hint->setWordWrap(true);
        hint->setVisible(settings->lowRam != activeNow);
        lay->addWidget(hint);
        connect(m_lowRam, &QCheckBox::toggled, hint, [hint, activeNow](bool on) {
            if (on != activeNow)
                Motion::fadeIn(hint, Motion::kFast);
            else
                hint->hide();
        });
    }

    // Extensions (normal profile only)
    buildExtensionsSection(root);

    // Anonymous sessions / Onionize + Tor integrity
    buildTorSection(root);

    // Footer
    auto* note = new QLabel(
        I18n::tr("data.dir").arg(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)) +
        "\n" + I18n::tr("data.note"));
    note->setObjectName("footer");
    note->setWordWrap(true);
    note->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addSpacing(4);
    root->addWidget(note);
    root->addStretch();

    for (auto* box : {m_autofill, m_cookies, m_cache, m_popups, m_logins, m_https, m_filter, m_ubolLists,
                      m_passwords, m_bookmarks, m_bmHome, m_lowRam, m_extOn,
                      m_lockBm, m_lockSleep})
        if (box) connect(box, &QCheckBox::toggled, this, [this](bool) { persist(); });
    for (auto* box : {m_logins, m_extOn})
        if (box) connect(box, &QCheckBox::toggled, this, [this](bool) { updateRestartHint(); });
    updateRestartHint();
    connect(m_engine, &QComboBox::currentIndexChanged, this, [this](int) {
        SearchEngine::clearInherited();
        updateSearchFields();
        persist();
    });
    if (m_search) {
        connect(m_search, &QLineEdit::textChanged, this, [this] { updateSearchFields(); });
        connect(m_search, &QLineEdit::textEdited, this, [] { SearchEngine::clearInherited(); });
        connect(m_search, &QLineEdit::editingFinished, this, [this] { persist(); });
    }
    connect(vaultBtn, &QPushButton::clicked, m_main, &MainWindow::changeMasterPassword);
    connect(wipeBtn, &QPushButton::clicked, m_main, &MainWindow::wipeNow);
    connect(m_dirBtn, &QPushButton::clicked, this, [this] {
        if (m_settings->sensitiveLocked)
            return;
        const QString dir = QFileDialog::getExistingDirectory(this, I18n::tr("downloads"), m_settings->downloadDir);
        if (dir.isEmpty()) return;
        m_settings->downloadDir = dir;
        m_dirLabel->setText(dir);
        m_store->save(*m_settings);
    });

    // Cards rise in one after another, quickly.
    int delay = 0;
    for (auto* card : columnHost->findChildren<QFrame*>("card", Qt::FindDirectChildrenOnly)) {
        Motion::fadeIn(card, Motion::kNormal, delay);
        delay += 25;
    }
}

void SettingsPage::buildExtensionsSection(QVBoxLayout* column)
{
    // Onionize / anonymous windows run on an off-the-record profile that
    // never loads extensions; say so instead of offering controls.
    if (m_profile->isEphemeral()) {
        auto* card = addCard(column, I18n::tr("settings.extensions"));
        auto* lay = qobject_cast<QVBoxLayout*>(card->layout());
        auto* note = new QLabel(I18n::tr("ext.offhere"));
        note->setObjectName("hint");
        note->setWordWrap(true);
        lay->addWidget(note);
        return;
    }
    // Normal window on the memory-only profile (no kept logins, extensions
    // off at start): the switch works, installing needs the disk profile.
    if (!m_profile->supportsExtensions()) {
        auto* card = addCard(column, I18n::tr("settings.extensions"), &m_extOn, m_settings->extensionsOn);
        auto* lay = qobject_cast<QVBoxLayout*>(card->layout());
        auto* note = new QLabel(I18n::tr("ext.note"));
        note->setObjectName("rowDesc");
        note->setWordWrap(true);
        lay->addWidget(note);
        lay->addSpacing(8);
        auto* memory = new QLabel(I18n::tr("ext.memory"));
        memory->setObjectName("hint");
        memory->setWordWrap(true);
        lay->addWidget(memory);
        return;
    }

    auto* card = addCard(column, I18n::tr("settings.extensions"), &m_extOn, m_settings->extensionsOn);
    auto* lay = qobject_cast<QVBoxLayout*>(card->layout());
    auto* note = new QLabel(I18n::tr("ext.note"));
    note->setObjectName("rowDesc");
    note->setWordWrap(true);
    lay->addWidget(note);
    lay->addSpacing(10);

    auto* body = new QWidget;
    auto* b = new QVBoxLayout(body);
    b->setContentsMargins(0, 0, 0, 0);
    b->setSpacing(0);

    auto* fromDir = new QPushButton(buttonText(I18n::tr("ext.install.folder")));
    auto* fromFile = new QPushButton(buttonText(I18n::tr("ext.install.file")));
    for (auto* btn : {fromDir, fromFile})
        btn->setCursor(Qt::PointingHandCursor);
    auto* btnRow = new QHBoxLayout;
    btnRow->setSpacing(8);
    btnRow->addWidget(fromDir);
    btnRow->addWidget(fromFile);
    btnRow->addStretch();
    b->addLayout(btnRow);

    m_extStatus = new QLabel;
    m_extStatus->setObjectName("hint");
    m_extStatus->setWordWrap(true);
    m_extStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_extStatus->hide();
    b->addSpacing(6);
    b->addWidget(m_extStatus);
    b->addSpacing(4);

    auto* listHost = new QWidget;
    m_extList = new QVBoxLayout(listHost);
    m_extList->setContentsMargins(0, 0, 0, 0);
    m_extList->setSpacing(0);
    b->addWidget(listHost);

    auto* path = new QLabel(I18n::tr("ext.path").arg(m_profile->extensionInstallPath()));
    path->setObjectName("hint");
    path->setWordWrap(true);
    path->setTextInteractionFlags(Qt::TextSelectableByMouse);
    b->addSpacing(8);
    b->addWidget(path);

    lay->addWidget(body);
    body->setEnabled(m_extOn->isChecked());
    connect(m_extOn, &QCheckBox::toggled, body, &QWidget::setEnabled);

    const auto install = [this](const QString& source) {
        if (source.isEmpty())
            return;
        setStatus(m_extStatus, I18n::tr("ext.installing"), false);
        m_profile->installExtension(source);
    };
    connect(fromDir, &QPushButton::clicked, this, [this, install] {
        install(QFileDialog::getExistingDirectory(this, I18n::tr("ext.pick.folder"), QDir::homePath()));
    });
    connect(fromFile, &QPushButton::clicked, this, [this, install] {
        install(QFileDialog::getOpenFileName(this, I18n::tr("ext.pick.file"), QDir::homePath(),
                                             I18n::tr("ext.filter") + QStringLiteral(" (*.zip *.crx)")));
    });

    connect(m_profile, &BrowserProfile::extensionInstallFinished, this,
            [this](const QString& id, const QString& name, const QString& error) {
        if (error.isEmpty()) {
            setStatus(m_extStatus, I18n::tr("ext.installed").arg(name), false);
            // A fresh install is switched on right away; the row toggle
            // can turn it off again.
            setExtensionEnabled(id, true);
        } else {
            setStatus(m_extStatus, I18n::tr("ext.install.fail").arg(error), true);
        }
    });
    // Queued: the list is rebuilt outside of the toggle/button that caused it.
    connect(m_profile, &BrowserProfile::extensionsChanged, this,
            &SettingsPage::refreshExtensionList, Qt::QueuedConnection);
    refreshExtensionList();
}

void SettingsPage::refreshExtensionList()
{
    if (!m_extList)
        return;
    while (auto* item = m_extList->takeAt(0)) {
        if (auto* w = item->widget())
            w->deleteLater();
        delete item;
    }

    const auto exts = m_profile->userExtensions();
    if (exts.isEmpty()) {
        auto* none = new QLabel(I18n::tr("ext.none"));
        none->setObjectName("rowDesc");
        none->setContentsMargins(0, 10, 0, 4);
        m_extList->addWidget(none);
        return;
    }

    bool first = true;
    for (const auto& e : exts) {
        if (!first)
            m_extList->addWidget(divider());
        first = false;

        QString desc = e.description().simplified();
        if (desc.size() > 140)
            desc = desc.left(137).trimmed() + QStringLiteral("...");
        const QString name = e.name().isEmpty() ? e.id() : e.name();
        const bool on = m_settings->enabledExtensions.contains(e.id());
        auto* row = new ToggleRow(name, desc, on);
        auto* rowLay = qobject_cast<QHBoxLayout*>(row->layout());
        if (!e.error().isEmpty() || !e.isLoaded()) {
            if (auto* text = rowLay ? qobject_cast<QVBoxLayout*>(rowLay->itemAt(0)->layout()) : nullptr) {
                auto* err = new QLabel(I18n::tr("ext.error").arg(
                    e.error().isEmpty() ? I18n::tr("ext.notloaded") : e.error()));
                err->setObjectName("rowError");
                err->setWordWrap(true);
                err->setTextInteractionFlags(Qt::TextSelectableByMouse);
                text->addWidget(err);
            }
            row->box()->setEnabled(false);
        }
        auto* remove = new QPushButton(buttonText(I18n::tr("ext.remove")));
        remove->setObjectName("dangerBtn");
        remove->setCursor(Qt::PointingHandCursor);
        if (rowLay)
            rowLay->insertWidget(1, remove, 0, Qt::AlignVCenter);
        m_extList->addWidget(row);

        const QString id = e.id();
        connect(row->box(), &QCheckBox::toggled, this, [this, id](bool checked) {
            setExtensionEnabled(id, checked);
        });
        connect(remove, &QPushButton::clicked, this, [this, id, name] {
            QMessageBox box(QMessageBox::Question, I18n::tr("ext.remove"),
                            I18n::tr("ext.remove.body").arg(name),
                            QMessageBox::Yes | QMessageBox::No, this);
            box.setDefaultButton(QMessageBox::No);
            if (box.exec() != QMessageBox::Yes)
                return;
            setExtensionEnabled(id, false);
            m_profile->removeExtension(id);
        });
    }
}

void SettingsPage::setExtensionEnabled(const QString& id, bool on)
{
    if (id.isEmpty())
        return;
    m_settings->enabledExtensions.removeAll(id);
    if (on)
        m_settings->enabledExtensions << id;
    persist();
}

void SettingsPage::buildTorSection(QVBoxLayout* column)
{
    auto* card = addCard(column, I18n::tr("settings.anon"));
    auto* a = qobject_cast<QVBoxLayout*>(card->layout());
    auto* help = new QLabel(I18n::tr("tor.help"));
    help->setObjectName("hint");
    help->setWordWrap(true);
    a->addWidget(help);
    a->addSpacing(10);

    // Integrity status: never execute an unverified tor binary, not even --version.
    const auto verified = TorIntegrity::verifyBundle();
    auto* status = new QLabel;
    if (verified.ok()) {
        status->setObjectName("statusOk");
        status->setText(QStringLiteral("✓  ") + I18n::tr("tor.integrity.ok"));
    } else {
        status->setObjectName("statusBad");
        status->setText(QStringLiteral("✕  ") + (verified.status == TorIntegrity::Status::Failed
                                                   ? I18n::tr("tor.integrity.fail")
                                                   : I18n::tr("tor.integrity.none")));
    }
    status->setToolTip(TorIntegrity::describe(verified));
    auto* statusRow = new QHBoxLayout;
    statusRow->addWidget(status);
    statusRow->addStretch();
    a->addLayout(statusRow);
    a->addSpacing(6);

    auto* ctl = new QLabel(I18n::tr("tor.control"));
    ctl->setObjectName("hint");
    ctl->setWordWrap(true);
    ctl->setToolTip(QStringLiteral("unix:") + TorControl::socketPath());
    a->addWidget(ctl);
    a->addSpacing(6);

    auto* ver = new QLabel;
    ver->setObjectName("mono");
    ver->setWordWrap(true);
    ver->setTextInteractionFlags(Qt::TextSelectableByMouse);
    a->addWidget(ver);

    const auto readLocal = [ver] {
        const auto r = TorIntegrity::verifyBundle();
        if (!r.ok()) {
            ver->setText(TorIntegrity::describe(r));
            return;
        }
        QProcess p;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.remove("LD_PRELOAD");
        env.insert("LD_LIBRARY_PATH", r.torDir);
        p.setProcessEnvironment(env);
        p.start(r.torBin, {"--version"});
        p.waitForFinished(3000);
        const QString first = QString::fromUtf8(p.readAllStandardOutput()).section('\n', 0, 0).trimmed();
        QString text = r.torDir + "\n" + (first.isEmpty() ? QStringLiteral("tor: ?") : first);
        const QString info = TorIntegrity::bakedBundleInfo().section('\n', 0, 0);
        if (!info.isEmpty())
            text += "\n" + info;
        ver->setText(text);
    };
    readLocal();

    auto* chk = new QPushButton(buttonText(I18n::tr("check.tor")));
    auto* upd = new QPushButton(buttonText(I18n::tr("update.tor")));
    for (auto* b : {chk, upd})
        b->setCursor(Qt::PointingHandCursor);
    auto* btnRow = new QHBoxLayout;
    btnRow->setSpacing(8);
    btnRow->addWidget(chk);
    btnRow->addWidget(upd);
    btnRow->addStretch();
    a->addSpacing(8);
    a->addLayout(btnRow);

    auto* updNote = new QLabel;
    updNote->setObjectName("hint");
    updNote->setWordWrap(true);
    updNote->hide();
    a->addWidget(updNote);

    connect(chk, &QPushButton::clicked, this, [ver, updNote] {
        auto* nam = new QNetworkAccessManager(ver);
        QNetworkRequest req(QUrl("https://www.torproject.org/download/tor/"));
        auto* reply = nam->get(req);
        QObject::connect(reply, &QNetworkReply::finished, ver, [updNote, reply, nam] {
            const QString arch = QSysInfo::currentCpuArchitecture() == QLatin1String("arm64")
                ? QStringLiteral("aarch64") : QStringLiteral("x86_64");
            const QString html = QString::fromUtf8(reply->readAll());
            const QRegularExpression re(QStringLiteral(R"(tor-expert-bundle-linux-%1-([0-9.]+)\.tar\.gz)").arg(arch));
            const auto m = re.match(html);
            updNote->setText(m.hasMatch() ? I18n::tr("tor.latest").arg(m.captured(1))
                                          : I18n::tr("tor.latest.fail"));
            updNote->show();
            reply->deleteLater();
            nam->deleteLater();
        });
    });

    // Only a source checkout can update (installed trees are read-only and
    // the hashes are baked at build time).
    const QString script = QFileInfo(TorIntegrity::executableDir() + "/../scripts/fetch-tor.sh").canonicalFilePath();
    if (script.isEmpty()) {
        upd->setEnabled(false);
        upd->setToolTip(I18n::tr("tor.update.installed"));
    }
    connect(upd, &QPushButton::clicked, this, [this, script, updNote, upd] {
        if (script.isEmpty())
            return;
        updNote->setText(I18n::tr("tor.updating"));
        updNote->show();
        upd->setEnabled(false);
        auto* p = new QProcess(this);
        p->setProcessChannelMode(QProcess::ForwardedChannels);
        p->setWorkingDirectory(QFileInfo(script).absolutePath() + "/..");
        QObject::connect(p, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                         this, [p, updNote, upd](int code, QProcess::ExitStatus) {
            updNote->setText(code == 0 ? I18n::tr("tor.update.rebuild")
                                       : I18n::tr("tor.update.fail"));
            upd->setEnabled(true);
            p->deleteLater();
        });
        p->start("bash", {script});
    });
}

QFrame* SettingsPage::addCard(QVBoxLayout* column, const QString& title,
                              QCheckBox** headerToggle, bool headerOn)
{
    auto* card = new QFrame;
    card->setObjectName("card");
    auto* lay = new QVBoxLayout(card);
    lay->setContentsMargins(22, 18, 22, 18);
    lay->setSpacing(0);

    auto* head = new QLabel(title);
    head->setObjectName("sectionTitle");
    if (headerToggle) {
        auto* row = new ToggleRow(QString(), QString(), headerOn);
        // Replace the empty title label with the section title.
        if (auto* t = row->findChild<QLabel*>("rowTitle")) {
            t->setObjectName("sectionTitle");
            t->setText(title);
        }
        row->layout()->setContentsMargins(0, 0, 0, 6);
        row->box()->setAccessibleName(title);
        *headerToggle = row->box();
        lay->addWidget(row);
        delete head;
    } else {
        lay->addWidget(head);
        lay->addSpacing(10);
    }
    column->addWidget(card);
    return card;
}

QCheckBox* SettingsPage::addToggle(QVBoxLayout* layout, const QString& title, bool on,
                                   const QString& desc)
{
    auto* row = new ToggleRow(title, desc, on);
    layout->addWidget(row);
    return row->box();
}

QLabel* SettingsPage::fieldLabel(const QString& text)
{
    auto* l = new QLabel(text);
    l->setObjectName("fieldLabel");
    l->setWordWrap(true);
    return l;
}

QFrame* SettingsPage::divider()
{
    auto* d = new QFrame;
    d->setObjectName("divider");
    d->setFixedHeight(1);
    return d;
}

void SettingsPage::persist()
{
    m_settings->savePasswords = m_passwords && m_passwords->isChecked();
    m_settings->autofillLogins = m_autofill->isChecked();
    m_settings->bookmarksEnabled = m_bookmarks && m_bookmarks->isChecked();
    m_settings->showBookmarksOnHome = m_bmHome && m_bmHome->isChecked();
    m_settings->clearCookiesOnExit = m_cookies->isChecked();
    m_settings->clearCacheOnExit = m_cache->isChecked();
    m_settings->blockPopups = m_popups->isChecked();
    m_settings->keepLogins = m_logins->isChecked();
    m_settings->httpsOnly = m_https->isChecked();
    m_settings->incoFilter = m_filter->isChecked();
    m_settings->ubolLists = m_ubolLists->isChecked();
    if (!m_settings->sensitiveLocked) {
        m_settings->filterExtra = m_filterExtra->toPlainText();
        m_settings->filterAllow = m_filterAllow->toPlainText();
        m_settings->searchEngine = m_engine->currentData().toString();
        if (m_search)
            m_settings->searchTemplate = m_search->text().trimmed();
    }
    if (m_lockBm)
        m_settings->lockBookmarks = m_lockBm->isChecked();
    if (m_idleMinutes)
        m_settings->autoLockMinutes = m_idleMinutes->value();
    if (m_lockSleep)
        m_settings->lockOnSleep = m_lockSleep->isChecked();
    m_settings->lowRam = m_lowRam && m_lowRam->isChecked();
    if (m_extOn)   // absent in Onionize windows: keep the stored choice
        m_settings->extensionsOn = m_extOn->isChecked();
    if (m_settings->keepLogins)
        m_settings->clearCookiesOnExit = false;
    m_profile->apply(*m_settings);
    m_store->save(*m_settings);
    m_main->updateBookmarkButtons();
    m_main->applyLowRam();
    m_main->updateExtensionButton();
    m_main->applyVaultSettings();
}

void SettingsPage::updateSearchFields()
{
    if (!m_engine)
        return;
    const QString id = m_engine->currentData().toString();
    if (m_customBox)
        m_customBox->setVisible(id == QLatin1String("custom"));
    // Same rule as the search itself: no usable address means DuckDuckGo.
    IncoSettings probe;
    probe.searchEngine = id;
    probe.searchTemplate = m_search ? m_search->text() : m_settings->searchTemplate;
    bool fellBack = false;
    SearchEngine::templateFor(probe, &fellBack);
    m_searchFallback->setVisible(fellBack);
}

void SettingsPage::updateRestartHint()
{
    if (!m_restartHint)
        return;
    IncoSettings wanted = *m_settings;
    wanted.keepLogins = m_logins && m_logins->isChecked();
    if (m_extOn)
        wanted.extensionsOn = m_extOn->isChecked();
    const bool differs = !m_profile->isEphemeral()
        && BrowserProfile::wantsDisk(wanted) != m_profile->isDiskBacked();
    if (differs && !m_restartHint->isVisible())
        Motion::fadeIn(m_restartHint, Motion::kFast);
    else if (!differs)
        m_restartHint->hide();
}

void SettingsPage::buildVaultLockSection(QVBoxLayout* column)
{
    auto* card = addCard(column, I18n::tr("settings.security"));
    auto* lay = qobject_cast<QVBoxLayout*>(card->layout());

    // Idle minutes: title and hint on the left, number on the right.
    auto* row = new QHBoxLayout;
    row->setSpacing(16);
    auto* text = new QVBoxLayout;
    text->setSpacing(2);
    auto* t = new QLabel(I18n::tr("autolock.idle"));
    t->setObjectName("rowTitle");
    t->setWordWrap(true);
    auto* d = new QLabel(I18n::tr("autolock.idle.d"));
    d->setObjectName("rowDesc");
    d->setWordWrap(true);
    text->addWidget(t);
    text->addWidget(d);
    row->addLayout(text, 1);
    m_idleMinutes = new QSpinBox;
    m_idleMinutes->setRange(0, 720);
    m_idleMinutes->setSpecialValueText(I18n::tr("autolock.off"));
    m_idleMinutes->setSuffix(I18n::tr("autolock.min"));
    m_idleMinutes->setValue(m_settings->autoLockMinutes);
    m_idleMinutes->setAccessibleName(I18n::tr("autolock.idle"));
    row->addWidget(m_idleMinutes, 0, Qt::AlignVCenter);
    lay->addLayout(row);
    lay->addSpacing(8);
    lay->addWidget(divider());
    m_lockSleep = addToggle(lay, I18n::tr("autolock.sleep"), m_settings->lockOnSleep);
    connect(m_idleMinutes, &QSpinBox::valueChanged, this, [this](int) { persist(); });

    // Sealed filter lists, search address and download folder.
    if (m_settings->sensitiveLocked && !m_profile->isEphemeral()) {
        lay->addWidget(divider());
        lay->addSpacing(10);
        auto* note = new QLabel(I18n::tr("sensitive.locked"));
        note->setObjectName("hint");
        note->setWordWrap(true);
        lay->addWidget(note);
        lay->addSpacing(8);
        auto* unlock = new QPushButton(buttonText(I18n::tr("sensitive.unlock")));
        unlock->setCursor(Qt::PointingHandCursor);
        auto* btnRow = new QHBoxLayout;
        btnRow->addWidget(unlock);
        btnRow->addStretch();
        lay->addLayout(btnRow);
        // The page is rebuilt by MainWindow once the values are open.
        connect(unlock, &QPushButton::clicked, m_main, [main = m_main] { main->unlockVault(); },
                Qt::QueuedConnection);
    }
}
