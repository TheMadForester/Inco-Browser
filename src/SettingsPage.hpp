#pragma once

#include "BrowserProfile.hpp"
#include "SettingsStore.hpp"

#include <QPlainTextEdit>
#include <QGroupBox>
#include <QWidget>

class MainWindow;
class QCheckBox;
class QLineEdit;
class QPushButton;

class SettingsPage final : public QWidget
{
public:
    SettingsPage(IncoSettings* settings, BrowserProfile* profile,
                 SettingsStore* store, MainWindow* main, QWidget* parent = nullptr);

private:
    void persist();
    QCheckBox* addCheck(class QVBoxLayout* layout, const QString& label, bool on);

    IncoSettings* m_settings;
    BrowserProfile* m_profile;
    SettingsStore* m_store;
    MainWindow* m_main;

    QGroupBox* m_passGroup = nullptr;
    QGroupBox* m_bmGroup = nullptr;
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
    QPlainTextEdit* m_filterExtra = nullptr;
    QPlainTextEdit* m_filterAllow = nullptr;
    QCheckBox* m_tor = nullptr;
    QLineEdit* m_search = nullptr;
    QPushButton* m_dirBtn = nullptr;
};
