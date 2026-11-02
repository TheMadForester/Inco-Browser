#pragma once

#include "BrowserProfile.hpp"
#include "SettingsStore.hpp"

#include <QPlainTextEdit>
#include <QWidget>

class MainWindow;
class QCheckBox;
class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QVBoxLayout;

class SettingsPage final : public QWidget
{
public:
    SettingsPage(IncoSettings* settings, BrowserProfile* profile,
                 SettingsStore* store, MainWindow* main, QWidget* parent = nullptr);

private:
    void persist();
    void buildTorSection(QVBoxLayout* column);
    void buildVaultLockSection(QVBoxLayout* column);
    void updateRestartHint();
    void updateSearchFields();
    void buildExtensionsSection(QVBoxLayout* column);
    void refreshExtensionList();
    void setExtensionEnabled(const QString& id, bool on);

    // UI helpers: a card with a section title, toggle rows, field labels.
    QFrame* addCard(QVBoxLayout* column, const QString& title,
                    QCheckBox** headerToggle = nullptr, bool headerOn = false);
    QCheckBox* addToggle(QVBoxLayout* layout, const QString& title, bool on,
                         const QString& desc = {});
    static QLabel* fieldLabel(const QString& text);
    static QFrame* divider();

    IncoSettings* m_settings;
    BrowserProfile* m_profile;
    SettingsStore* m_store;
    MainWindow* m_main;

    QCheckBox* m_bmHome = nullptr;
    QCheckBox* m_passwords = nullptr;
    QCheckBox* m_autofill = nullptr;
    QCheckBox* m_bookmarks = nullptr;
    QCheckBox* m_cookies = nullptr;
    QCheckBox* m_cache = nullptr;
    QCheckBox* m_popups = nullptr;
    QCheckBox* m_logins = nullptr;
    QCheckBox* m_https = nullptr;
    QCheckBox* m_filter = nullptr;
    QCheckBox* m_ubolLists = nullptr;
    QPlainTextEdit* m_filterExtra = nullptr;
    QPlainTextEdit* m_filterAllow = nullptr;
    QCheckBox* m_lowRam = nullptr;
    QCheckBox* m_extOn = nullptr;
    QVBoxLayout* m_extList = nullptr;
    QLabel* m_extStatus = nullptr;
    QComboBox* m_engine = nullptr;
    QWidget* m_customBox = nullptr;      // only when a custom address exists
    QLineEdit* m_search = nullptr;       // custom address
    QLabel* m_searchFallback = nullptr;  // "no usable address, DuckDuckGo"
    QPushButton* m_dirBtn = nullptr;
    QLabel* m_dirLabel = nullptr;
    QCheckBox* m_lockBm = nullptr;
    QSpinBox* m_idleMinutes = nullptr;
    QCheckBox* m_lockSleep = nullptr;
    QLabel* m_restartHint = nullptr;   // profile kind changes after a restart
};
