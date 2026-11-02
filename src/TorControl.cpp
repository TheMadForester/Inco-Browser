#include "TorControl.hpp"
#include "TorIntegrity.hpp"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLocalSocket>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <memory>

namespace TorControl {

QString socketPath() { return TorIntegrity::dataDir() + QStringLiteral("/control.sock"); }
QString cookiePath() { return TorIntegrity::dataDir() + QStringLiteral("/control_auth_cookie"); }

QStringList torArguments()
{
    const QString sock = socketPath();
    // sun_path is 108 bytes on Linux, including the terminating NUL; Tor's
    // own option parser also splits on spaces.
    if (QFile::encodeName(sock).size() >= int(sizeof(sockaddr_un::sun_path)) || sock.contains(QLatin1Char(' ')))
        return {};
    return {
        QStringLiteral("--ControlPort"), QStringLiteral("unix:") + sock,
        QStringLiteral("--CookieAuthentication"), QStringLiteral("1"),
        QStringLiteral("--CookieAuthFile"), cookiePath(),
    };
}

namespace {

struct Entry {
    QString head;        // "key=value" or plain text after the status code
    QStringList data;    // lines of a "+" data block
};

struct Reply {
    int code = 0;
    QVector<Entry> entries;
    bool ok() const { return code == 250; }
    // Values of a GETINFO key, one per line.
    QStringList values(const QString& key) const
    {
        QStringList out;
        const QString prefix = key + QLatin1Char('=');
        for (const auto& e : entries) {
            if (!e.head.startsWith(prefix))
                continue;
            const QString inl = e.head.mid(prefix.size());
            if (!inl.isEmpty())
                out << inl;
            out << e.data;
        }
        return out;
    }
};

struct RawCircuit {
    QString id;
    QString purpose;
    QString created;
    QString confluxId;
    QStringList fps;
    QStringList nicks;
};

bool isHex40(const QString& s)
{
    static const QRegularExpression re(QStringLiteral("^[0-9A-F]{40}$"));
    return re.match(s).hasMatch();
}

RawCircuit parseCircuit(const QString& line, bool* usable)
{
    RawCircuit c;
    *usable = false;
    const QStringList cols = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (cols.size() < 3)
        return c;
    c.id = cols.at(0);
    const bool built = cols.at(1) == QLatin1String("BUILT");
    QString flags;
    for (int i = 2; i < cols.size(); ++i) {
        const QString& col = cols.at(i);
        if (i == 2 && col.startsWith(QLatin1Char('$'))) {
            for (const QString& hop : col.split(QLatin1Char(','))) {
                QString h = hop.mid(1);
                QString nick;
                const int sep = h.indexOf(QRegularExpression(QStringLiteral("[~=]")));
                if (sep > 0) {
                    nick = h.mid(sep + 1);
                    h = h.left(sep);
                }
                c.fps << h.toUpper();
                c.nicks << nick;
            }
        } else if (col.startsWith(QLatin1String("BUILD_FLAGS="))) {
            flags = col.mid(12);
        } else if (col.startsWith(QLatin1String("PURPOSE="))) {
            c.purpose = col.mid(8);
        } else if (col.startsWith(QLatin1String("TIME_CREATED="))) {
            c.created = col.mid(13);
        } else if (col.startsWith(QLatin1String("CONFLUX_ID="))) {
            c.confluxId = col.mid(11);
        }
    }
    const QStringList f = flags.split(QLatin1Char(','));
    *usable = built
        && (c.purpose == QLatin1String("GENERAL") || c.purpose == QLatin1String("CONFLUX_LINKED"))
        && !f.contains(QLatin1String("IS_INTERNAL")) && !f.contains(QLatin1String("ONEHOP_TUNNEL"))
        && c.fps.size() >= 3
        && std::all_of(c.fps.cbegin(), c.fps.cend(), isHex40);
    return c;
}

// One connection, one query. Deletes itself when done.
class Session final : public QObject
{
public:
    Session(QObject* context, QString host, QString exitIp, std::function<void(const Circuit&)> done)
        : QObject(nullptr), m_context(context), m_host(std::move(host)),
          m_exitIp(std::move(exitIp)), m_done(std::move(done))
    {
        m_sock = new QLocalSocket(this);
        connect(m_sock, &QLocalSocket::connected, this, [this] { onConnected(); });
        connect(m_sock, &QLocalSocket::readyRead, this, [this] { onReadyRead(); });
        connect(m_sock, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError) {
            fail(m_authed ? Circuit::Fail::Protocol : Circuit::Fail::NoSocket, m_sock->errorString());
        });
        m_timer.setSingleShot(true);
        connect(&m_timer, &QTimer::timeout, this, [this] {
            fail(Circuit::Fail::Protocol, QStringLiteral("control port did not answer in time"));
        });
        if (context)
            connect(context, &QObject::destroyed, this, [this] { m_done = nullptr; finish(); });
    }

    void start()
    {
        const QString path = socketPath();
        struct stat st {};
        if (::lstat(QFile::encodeName(path).constData(), &st) != 0) {
            fail(Circuit::Fail::NoSocket, QStringLiteral("no control socket at %1").arg(path));
            return;
        }
        if (!S_ISSOCK(st.st_mode) || st.st_uid != ::getuid()) {
            fail(Circuit::Fail::NotVerified, QStringLiteral("%1 is not a socket owned by this user").arg(path));
            return;
        }
        m_timer.start(8000);
        m_sock->connectToServer(path);
    }

private:
    using Handler = std::function<void(const Reply&)>;

    void send(const QString& cmd, Handler h)
    {
        m_pending.push_back(std::move(h));
        m_sock->write(cmd.toLatin1() + "\r\n");
    }

    void onConnected()
    {
        // Who is on the other end? Only the verified bundled tor gets the cookie.
        ucred cred {};
        socklen_t len = sizeof(cred);
        const int fd = int(m_sock->socketDescriptor());
        if (fd < 0 || ::getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) != 0) {
            fail(Circuit::Fail::NotVerified, QStringLiteral("cannot read the peer of the control socket"));
            return;
        }
        if (cred.uid != ::getuid() || !TorIntegrity::runningBinaryMatches(cred.pid)) {
            fail(Circuit::Fail::NotVerified,
                 QStringLiteral("control socket is held by pid %1, not the verified bundled Tor").arg(cred.pid));
            return;
        }
        m_result.torPid = cred.pid;

        QFile f(cookiePath());
        QByteArray cookie;
        if (f.open(QIODevice::ReadOnly))
            cookie = f.read(64);
        if (cookie.size() != 32) {
            fail(Circuit::Fail::Auth, QStringLiteral("control cookie missing or malformed"));
            return;
        }
        send(QStringLiteral("AUTHENTICATE ") + QString::fromLatin1(cookie.toHex()), [this](const Reply& r) {
            if (!r.ok()) {
                fail(Circuit::Fail::Auth, QStringLiteral("Tor refused the control cookie"));
                return;
            }
            m_authed = true;
            readCircuits();
        });
    }

    void readCircuits()
    {
        send(QStringLiteral("GETINFO circuit-status"), [this](const Reply& r) {
            if (!r.ok()) {
                fail(Circuit::Fail::Protocol, QStringLiteral("GETINFO circuit-status failed (%1)").arg(r.code));
                return;
            }
            for (const QString& line : r.values(QStringLiteral("circuit-status"))) {
                bool usable = false;
                RawCircuit c = parseCircuit(line, &usable);
                if (usable)
                    m_circuits.push_back(c);
            }
            send(QStringLiteral("GETINFO stream-status"), [this](const Reply& r2) {
                if (r2.ok())
                    m_streams = r2.values(QStringLiteral("stream-status"));
                lookupRelays();
            });
        });
    }

    void lookupRelays()
    {
        if (m_circuits.isEmpty()) {
            fail(Circuit::Fail::NoCircuit, QStringLiteral("Tor has no built circuit for web traffic yet"));
            return;
        }
        for (const auto& c : m_circuits)
            for (const QString& fp : c.fps)
                if (!m_ns.contains(fp) && !m_todo.contains(fp))
                    m_todo << fp;
        nextRelay();
    }

    void nextRelay()
    {
        if (m_todo.isEmpty()) {
            choose();
            return;
        }
        const QString fp = m_todo.takeFirst();
        send(QStringLiteral("GETINFO ns/id/") + fp, [this, fp](const Reply& r) {
            Hop h;
            h.fingerprint = fp;
            if (r.ok()) {
                for (const QString& line : r.values(QStringLiteral("ns/id/") + fp)) {
                    if (!line.startsWith(QLatin1String("r ")))
                        continue;
                    // r <nick> <id> <digest> <date> <time> <IP> <ORPort> <DirPort>
                    const QStringList p = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
                    if (p.size() > 1)
                        h.nickname = p.at(1);
                    static const QRegularExpression ipv4(QStringLiteral("^\\d{1,3}(\\.\\d{1,3}){3}$"));
                    for (int i = 4; i < p.size(); ++i)
                        if (ipv4.match(p.at(i)).hasMatch()) { h.ip = p.at(i); break; }
                }
            }
            m_ns.insert(fp, h);
            nextRelay();
        });
    }

    QVector<Hop> hopsOf(const RawCircuit& c) const
    {
        QVector<Hop> out;
        for (int i = 0; i < c.fps.size(); ++i) {
            Hop h = m_ns.value(c.fps.at(i));
            h.fingerprint = c.fps.at(i);
            if (h.nickname.isEmpty())
                h.nickname = c.nicks.value(i);
            out.push_back(h);
        }
        return out;
    }

    void choose()
    {
        const RawCircuit* pick = nullptr;
        // 1. A stream to the tab's host that Tor attached to a usable circuit.
        if (!m_host.isEmpty()) {
            int best = -1;
            for (const QString& s : m_streams) {
                const QStringList p = s.split(QLatin1Char(' '), Qt::SkipEmptyParts);
                if (p.size() < 4 || p.at(2) == QLatin1String("0"))
                    continue;
                const QString target = p.at(3);
                const QString host = target.left(target.lastIndexOf(QLatin1Char(':')));
                if (host.compare(m_host, Qt::CaseInsensitive) != 0)
                    continue;
                const int sid = p.at(0).toInt();
                for (const auto& c : m_circuits) {
                    if (c.id == p.at(2) && sid > best) {
                        best = sid;
                        pick = &c;
                        m_result.streamTarget = target;
                    }
                }
            }
            if (pick)
                m_result.pick = Pick::Stream;
        }
        // 2. The circuit whose exit address check.torproject.org reported.
        if (!pick && !m_exitIp.isEmpty()) {
            for (const auto& c : m_circuits) {
                if (m_ns.value(c.fps.last()).ip == m_exitIp
                    && (!pick || c.created > pick->created))
                    pick = &c;
            }
            if (pick)
                m_result.pick = Pick::ExitMatch;
        }
        // 3. Otherwise the newest one, and the panel says it is only a guess.
        if (!pick) {
            for (const auto& c : m_circuits)
                if (!pick || c.created > pick->created)
                    pick = &c;
            m_result.pick = Pick::Newest;
        }

        m_result.id = pick->id;
        m_result.hops = hopsOf(*pick);
        if (!pick->confluxId.isEmpty()) {
            for (const auto& c : m_circuits)
                if (&c != pick && c.confluxId == pick->confluxId)
                    m_result.otherLegs.push_back(hopsOf(c));
        }
        // Countries from Tor's own GeoIP file: no extra network request.
        for (const auto& h : m_result.hops)
            if (!h.ip.isEmpty()) m_ips.insert(h.ip);
        for (const auto& leg : m_result.otherLegs)
            for (const auto& h : leg)
                if (!h.ip.isEmpty()) m_ips.insert(h.ip);
        m_ipTodo = QStringList(m_ips.cbegin(), m_ips.cend());
        nextCountry();
    }

    void nextCountry()
    {
        if (m_ipTodo.isEmpty()) {
            auto fill = [this](QVector<Hop>& hops) {
                for (auto& h : hops)
                    h.country = m_cc.value(h.ip);
            };
            fill(m_result.hops);
            for (auto& leg : m_result.otherLegs)
                fill(leg);
            m_result.ok = true;
            finish();
            return;
        }
        const QString ip = m_ipTodo.takeFirst();
        send(QStringLiteral("GETINFO ip-to-country/") + ip, [this, ip](const Reply& r) {
            if (r.ok()) {
                const QString cc = r.values(QStringLiteral("ip-to-country/") + ip).value(0).trimmed();
                if (cc.size() == 2 && cc != QLatin1String("??"))
                    m_cc.insert(ip, cc.toUpper());
            }
            nextCountry();
        });
    }

    void onReadyRead()
    {
        m_buf += m_sock->readAll();
        for (;;) {
            const int nl = m_buf.indexOf("\r\n");
            if (nl < 0)
                return;
            const QString line = QString::fromUtf8(m_buf.left(nl));
            m_buf.remove(0, nl + 2);
            if (m_inData) {
                if (line == QLatin1String(".")) {
                    m_inData = false;
                } else {
                    m_reply.entries.last().data << (line.startsWith(QLatin1String("..")) ? line.mid(1) : line);
                }
                continue;
            }
            if (line.size() < 4) {
                fail(Circuit::Fail::Protocol, QStringLiteral("unexpected control reply"));
                return;
            }
            const QChar kind = line.at(3);
            m_reply.code = line.left(3).toInt();
            m_reply.entries.push_back({line.mid(4), {}});
            if (kind == QLatin1Char('+')) {
                m_inData = true;
            } else if (kind == QLatin1Char(' ')) {
                Reply r = m_reply;
                m_reply = {};
                if (r.code / 100 == 6)    // async event: never subscribed, ignore
                    continue;
                if (m_pending.isEmpty())
                    continue;
                Handler h = m_pending.takeFirst();
                h(r);
                if (m_finished)
                    return;
            }
        }
    }

    void fail(Circuit::Fail why, const QString& msg)
    {
        if (m_finished)
            return;
        m_result.ok = false;
        m_result.fail = why;
        m_result.error = msg;
        finish();
    }

    void finish()
    {
        if (m_finished)
            return;
        m_finished = true;
        m_timer.stop();
        m_result.when = QDateTime::currentDateTime();
        auto done = std::move(m_done);
        m_done = nullptr;
        const Circuit result = m_result;
        m_sock->abort();
        deleteLater();
        if (done && m_context)
            done(result);
    }

    QPointer<QObject> m_context;
    QString m_host;
    QString m_exitIp;
    std::function<void(const Circuit&)> m_done;
    QLocalSocket* m_sock = nullptr;
    QTimer m_timer;
    QByteArray m_buf;
    Reply m_reply;
    bool m_inData = false;
    bool m_authed = false;
    bool m_finished = false;
    QVector<Handler> m_pending;
    QVector<RawCircuit> m_circuits;
    QStringList m_streams;
    QStringList m_todo;
    QHash<QString, Hop> m_ns;
    QSet<QString> m_ips;
    QStringList m_ipTodo;
    QHash<QString, QString> m_cc;
    Circuit m_result;
};

} // namespace

void queryCircuit(QObject* context, const QString& preferHost, const QString& exitIp,
                  std::function<void(const Circuit&)> done)
{
    auto* s = new Session(context, preferHost, exitIp, std::move(done));
    // Start from the event loop so `done` never runs inside the caller.
    QTimer::singleShot(0, s, [s] { s->start(); });
}

} // namespace TorControl
