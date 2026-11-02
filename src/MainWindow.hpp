#pragma once

#include "BookmarkStore.hpp"
#include "BrowserProfile.hpp"
#include "OnionRoute.hpp"
#include "TorControl.hpp"
#include "PasswordStore.hpp"
#include "SettingsStore.hpp"

#include <QElapsedTimer>
#include <QLabel>
#include <QMainWindow>
#include <QPointer>
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
    void refreshSettings();
    bool httpsOnly() const;
    // Search address for the address bar and the new-tab box: the engine of
    // this session (the vault may be locked by now), DuckDuckGo if none.
    QString searchTemplate() const;
    QUrl searchUrl(const QString& query) const;
    void setMuted(bool on);
    // Re-applies Low RAM mode after a settings change (tab freezing/discarding).
    void applyLowRam();
    // Shows the toolbar extensions button while any extension is enabled.
    void updateExtensionButton();
    // Asks for the master password if needed (home page link, Settings).
    bool unlockVault();
    void unlockFromPage();
    // Re-applies bookmark lock and idle auto-lock after a settings change.
    void applyVaultSettings();
    bool vaultExists() const { return m_passwords.exists(); }
private slots:
    void onPrepareForSleep(bool start);
    void onScreenLockChanged(bool active);
    void onSessionLock();
private:
    bool isOnionTab(QWebEngineView* view) const;

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void navigate();
    void addTab(const QUrl& url = QUrl("qrc:/newtab.html"));
    void closeCurrentTab();
    void closeTab(int index);
    void onTabChanged(int index);
    void showSettings();
    void showPasswordVault();
    bool ensureVaultUnlocked();
    // Vault open: hand subkeys to bookmarks and sensitive settings.
    void onVaultUnlocked();
    // Forget every key (vault, bookmarks, settings). updateUi = false at exit.
    void lockAll(bool updateUi);
    // Bookmarks sealed and locked: ask for the master password first.
    bool bookmarksReady();
    void refreshHomePages();
    void rebuildSettingsTabIfOpen();
    void setupAutoLock();
    void resetIdleTimer();
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
    void applyLanguageUi();
    void setupShortcuts();
    void wireView(QWebEngineView* view);
    void setTabIcon(QWebEngineView* view, const QIcon& icon);
    void reloadFind();
    void zoomBy(double delta);
    void resetZoom();
    void updateZoomLabel();
    void toggleMute();
    QWebEngineView* currentView() const;
    void showOnionRoute();
    void runTorProbe();
    void readCircuit();
    void updateLifecycles();
    void showExtensionMenu();
    void openExtensionPopup(const QUrl& url);

    SettingsStore m_settingsStore;
    IncoSettings m_settings;
    BrowserProfile m_profile;
    BrowserProfile m_anonProfile;
    PasswordStore m_passwords;
    BookmarkStore m_bookmarks;
    bool m_anonymous = false;
    bool m_torProcess = false;
    bool m_onionBusy = false;
    bool m_probeInFlight = false;
    int m_probeAttempt = 0;
    TorExitInfo m_lastExit;
    bool m_circuitInFlight = false;
    TorControl::Circuit m_lastCircuit;
    class QNetworkAccessManager* m_torNet = nullptr;
    QLabel* m_torLabel = nullptr;
    OnionRoutePanel* m_routePanel = nullptr;
    class QTimer* m_lifecycleTimer = nullptr;
    class QTimer* m_idleTimer = nullptr;   // vault auto-lock
    QElapsedTimer m_idleKick;              // throttles timer restarts on input

    QLineEdit* m_addressBar = nullptr;
    QPushButton* m_backButton = nullptr;
    QPushButton* m_forwardButton = nullptr;
    QPushButton* m_reloadButton = nullptr;
    QPushButton* m_newTabButton = nullptr;
    QPushButton* m_bookmarkButton = nullptr;
    QPushButton* m_bookmarkListButton = nullptr;
    QPushButton* m_passwordsButton = nullptr;
    QPushButton* m_settingsButton = nullptr;
    QPushButton* m_extButton = nullptr;
    class QAction* m_extAction = nullptr;   // toolbar slot of m_extButton
    QPointer<QWidget> m_extPopup;
    QPushButton* m_muteButton = nullptr;
    QPushButton* m_onionButton = nullptr;
    QLabel* m_zoomLabel = nullptr;
    QTabWidget* m_tabs = nullptr;
    QWidget* m_findBar = nullptr;
    QLineEdit* m_findInput = nullptr;
    QWidget* m_downloadBar = nullptr;
    QListWidget* m_downloadList = nullptr;
};
