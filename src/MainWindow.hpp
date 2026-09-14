#pragma once

#include "BookmarkStore.hpp"
#include "BrowserProfile.hpp"
#include "PasswordStore.hpp"
#include "SettingsStore.hpp"

#include <QLabel>
#include <QMainWindow>
#include <QUrl>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTabWidget;
class QWebEngineDownloadRequest;
class QWebEngineView;
class QWidget;

class MainWindow final : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(bool anonymous = false, bool tor = false, QWidget* parent = nullptr);
    ~MainWindow() override;
    QWebEngineView* createTabView();
    void updateBookmarkButtons();
    void wipeNow();
    void onionizeCurrentTab();
    void fillHomeBookmarks(QWebEngineView* view);
    void checkTorStatus();
    void openAnonymousSession();
    void changeMasterPassword();
    bool httpsOnly() const;
    void setMuted(bool on);
public slots:
private:
    bool isOnionTab(QWebEngineView* view) const;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void navigate();
    void addTab(const QUrl& url = QUrl("qrc:/newtab.html"));
    void closeCurrentTab();
    void closeTab(int index);
    void onTabChanged(int index);
    void showSettings();
    void showPasswordVault();
    bool ensureVaultUnlocked();
    void lockVault();
    void exportVault();
    void importVault();
    void showBookmarks();
    void toggleBookmark();
    void tryAutofill(QWebEngineView* view);
    void paintSearchPage(QWebEngineView* view);
    void showFindBar();
    void hideFindBar();
    void findNext();
    void findPrev();
    void handleDownload(QWebEngineDownloadRequest* download);
    void updateAddressBar(const QUrl& url);
    void updateWindowTitle(const QString& title);
    void applyDarkTheme();
    void setupUi();
    void setupShortcuts();
    void wireView(QWebEngineView* view);
    void setTabIcon(QWebEngineView* view, const QIcon& icon);
    void reloadFind();
    void zoomBy(double delta);
    void resetZoom();
    void updateZoomLabel();
    void toggleMute();
    QWebEngineView* currentView() const;

    SettingsStore m_settingsStore;
    IncoSettings m_settings;
    BrowserProfile m_profile;
    BrowserProfile m_anonProfile;
    PasswordStore m_passwords;
    BookmarkStore m_bookmarks;
    bool m_anonymous = false;
    bool m_torProcess = false;
    bool m_onionBusy = false;
    QLabel* m_torLabel = nullptr;

    QLineEdit* m_addressBar = nullptr;
    QPushButton* m_backButton = nullptr;
    QPushButton* m_forwardButton = nullptr;
    QPushButton* m_reloadButton = nullptr;
    QPushButton* m_newTabButton = nullptr;
    QPushButton* m_bookmarkButton = nullptr;
    QPushButton* m_bookmarkListButton = nullptr;
    QPushButton* m_passwordsButton = nullptr;
    QPushButton* m_settingsButton = nullptr;
    QPushButton* m_muteButton = nullptr;
    QPushButton* m_onionButton = nullptr;
    QLabel* m_zoomLabel = nullptr;
    QTabWidget* m_tabs = nullptr;
    QWidget* m_findBar = nullptr;
    QLineEdit* m_findInput = nullptr;
    QWidget* m_downloadBar = nullptr;
    QListWidget* m_downloadList = nullptr;
};
