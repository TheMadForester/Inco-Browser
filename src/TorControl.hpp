#pragma once

// Read-only client for the control port of the bundled Tor.
//
// Inco starts its Tor with the control port on a Unix socket inside the
// private tor-data directory (mode 0700) and cookie authentication, so web
// pages (which only reach TCP) and other users cannot talk to it. Before the
// cookie is sent, the process on the other end of the socket is checked with
// SO_PEERCRED: same user, and /proc/<pid>/exe must hash to the baked "tor"
// entry. Only GETINFO is used; nothing is ever changed through the port.

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

class QLocalSocket;

namespace TorControl {

// <tor-data>/control.sock and <tor-data>/control_auth_cookie.
QString socketPath();
QString cookiePath();

// Arguments that make tor open the control socket. Empty when the path does
// not fit into sockaddr_un (Tor then runs without a control port and the
// route panel says so).
QStringList torArguments();

struct Hop {
    QString fingerprint;   // 40 hex chars
    QString nickname;
    QString ip;            // OR address from the consensus, may be empty
    QString country;       // two-letter code from Tor's GeoIP file, may be empty
};

enum class Pick {
    None,
    Stream,     // the circuit carrying a stream to the tab's host
    ExitMatch,  // exit address equals the one check.torproject.org saw
    Newest      // newest usable circuit; the tab may be on another one
};

struct Circuit {
    bool ok = false;            // talked to the verified Tor and read circuits
    QString error;              // why not, in English (technical detail)
    enum class Fail { None, NoSocket, NotVerified, Auth, Protocol, NoCircuit } fail = Fail::None;
    Pick pick = Pick::None;
    QString id;
    QString streamTarget;       // host:port for Pick::Stream
    QVector<Hop> hops;          // guard first, exit last
    QVector<QVector<Hop>> otherLegs; // conflux: the other legs of the same set
    qint64 torPid = 0;
    QDateTime when;
};

// Connect, verify the peer, authenticate, read circuit-status and
// stream-status and look up the chosen relays. `preferHost` is the current
// tab's host, `exitIp` the address check.torproject.org reported (both may be
// empty). `done` runs once on the GUI thread.
void queryCircuit(QObject* context, const QString& preferHost, const QString& exitIp,
                  std::function<void(const Circuit&)> done);

} // namespace TorControl
