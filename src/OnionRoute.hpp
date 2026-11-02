#pragma once

// Onionize route panel: shows the path traffic takes in an Onionize window.
//
// Where the data comes from:
//  - guard, middle and exit relays: Tor's own control port (local socket,
//    peer checked against the baked tor hash, read-only GETINFO). The panel
//    says which circuit it shows and how sure that is.
//  - exit address: check.torproject.org, asked through the same SOCKS port
//    the pages use, plus the public relay directory (Onionoo) over Tor.
// Anything Inco could not read is shown as unknown, never guessed.

#include <QDateTime>
#include <QFrame>
#include <QString>
#include <functional>

#include "TorControl.hpp"

class QLabel;
class QPushButton;
class QVariantAnimation;
class RouteView;

struct TorExitInfo {
    bool ok = false;          // request through Tor succeeded
    bool isTor = false;       // check.torproject.org says this IP is a Tor exit
    QString ip;
    QString nickname;         // from Onionoo, may be empty
    QString country;          // from Onionoo, may be empty
    QString asName;           // from Onionoo, may be empty
    QString error;
    QDateTime when;
};

class QNetworkAccessManager;

// Network access that goes through the Tor SOCKS port on 127.0.0.1. Keep one
// per window and reuse it; tearing one down mid-connection makes Qt print
// "QIODevice::write (QTcpSocket): device not open".
QNetworkAccessManager* makeTorNetwork(QObject* parent, quint16 socksPort);

// Ask check.torproject.org through `nam`, then look the exit up in Onionoo.
// `done` runs on the GUI thread; nothing runs once `nam` is gone.
void probeTorExit(QNetworkAccessManager* nam, std::function<void(const TorExitInfo&)> done);

class OnionRoutePanel final : public QFrame
{
public:
    // tor = this window really sends traffic through the bundled Tor.
    OnionRoutePanel(bool tor, QWidget* parent);

    void popupBelow(QWidget* anchor);
    // True right after the popup closed; the click that closed it should not
    // reopen it when it lands on the toolbar button.
    bool justClosed() const { return QDateTime::currentMSecsSinceEpoch() - m_hiddenAt < 250; }
    // Result of a probe started through onRefresh (or by the window itself).
    void setExitInfo(const TorExitInfo& info);
    // Circuit read from the control port (or why it could not be read).
    void setCircuit(const TorControl::Circuit& c);
    void setCircuitBusy();
    std::function<void()> onRefresh;
    std::function<void()> onTurnOff;
    std::function<void()> onOpenCheck;

protected:
    void paintEvent(QPaintEvent*) override;
    void hideEvent(QHideEvent*) override;

private:
    void refresh();
    void updateTexts();
    void fitToContent();

    bool m_tor = false;
    TorExitInfo m_info;
    TorControl::Circuit m_circuit;
    bool m_busy = false;
    bool m_circuitBusy = false;
    qint64 m_hiddenAt = 0;
    QWidget* m_content = nullptr;
    RouteView* m_route = nullptr;
    QLabel* m_note = nullptr;
    QPushButton* m_refresh = nullptr;
    QPushButton* m_check = nullptr;
    QPushButton* m_off = nullptr;
};
