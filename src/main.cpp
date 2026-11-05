#include "MainWindow.hpp"
#include "I18n.hpp"
#include "SearchEngine.hpp"
#include "SettingsStore.hpp"
#include "TorControl.hpp"
#include "TorIntegrity.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QMessageBox>
#include <QProcess>
#include <QProcessEnvironment>
#include <QThread>
#include <QWebEngineUrlScheme>
#include <cstdio>
#include <utility>

#include <sys/prctl.h>

using TorIntegrity::kSocksPort;

// Start the verified bundled tor. Returns nullptr and fills *err on failure.
static QProcess* startBundledTor(const TorIntegrity::Result& verified, QString* err)
{
    const QString libdir = verified.torDir;
    const QString data = TorIntegrity::dataDir();
    if (data.isEmpty()) {
        *err = QStringLiteral("no private folder for Tor data");
        return nullptr;
    }

    // Empty torrc: never pick up a stray system/user torrc or the Tor Browser
    // torrc-defaults (which points at ./TorBrowser/... paths).
    const QString torrc = data + QStringLiteral("/torrc");
    {
        QFile f(torrc);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            f.close();
        QFile::setPermissions(torrc, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }

    auto* p = new QProcess;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.remove(QStringLiteral("LD_PRELOAD"));
    env.remove(QStringLiteral("LD_AUDIT"));
    env.insert(QStringLiteral("LD_LIBRARY_PATH"), libdir); // bundle libs only
    p->setProcessEnvironment(env);
    p->setWorkingDirectory(data);
    p->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    p->setStandardOutputFile(QProcess::nullDevice());
    QStringList args = {
        QStringLiteral("-f"), torrc,
        QStringLiteral("--defaults-torrc"), torrc,
        QStringLiteral("--SocksPort"), QStringLiteral("127.0.0.1:%1").arg(kSocksPort),
        QStringLiteral("--DataDirectory"), data,
        QStringLiteral("--ClientOnly"), QStringLiteral("1"),
    };
    // Control port only on a Unix socket in the private data dir (pages can
    // only reach TCP), cookie auth. The route panel reads circuits from it.
    const QStringList control = TorControl::torArguments();
    if (control.isEmpty()) {
        args << QStringLiteral("--ControlPort") << QStringLiteral("0");
        std::fprintf(stderr, "Inco: control socket path too long, running Tor without a control port\n");
    } else {
        QFile::remove(TorControl::socketPath());   // stale socket of a crashed run
        args << control;
    }
    // GeoIP files are part of the verified bundle; Tor uses them to name the
    // relay countries in the route panel without asking anyone online.
    for (const auto& [opt, file] : {std::pair{"--GeoIPFile", "/data/geoip"}, std::pair{"--GeoIPv6File", "/data/geoip6"}}) {
        if (QFileInfo(libdir + QLatin1String(file)).isFile())
            args << QLatin1String(opt) << libdir + QLatin1String(file);
    }
    p->start(verified.torBin, args);
    if (!p->waitForStarted(2000)) {
        *err = QStringLiteral("could not execute %1").arg(verified.torBin);
        delete p;
        return nullptr;
    }
    // Close the check-then-exec window for the main binary.
    if (!TorIntegrity::runningBinaryMatches(p->processId())) {
        *err = QStringLiteral("started tor does not match the baked hash");
        p->kill();
        p->waitForFinished(1000);
        delete p;
        return nullptr;
    }
    for (int i = 0; i < 80; ++i) {
        if (p->state() != QProcess::Running)
            break;
        if (TorIntegrity::socksPortState(verified) == TorIntegrity::PortState::OurTor) {
            std::fprintf(stderr, "Inco: verified bundled tor on 127.0.0.1:%u\n", unsigned(kSocksPort));
            return p;
        }
        QThread::msleep(250);
    }
    *err = QStringLiteral("tor started but SOCKS 127.0.0.1:%1 never opened").arg(kSocksPort);
    p->kill();
    p->waitForFinished(1000);
    delete p;
    return nullptr;
}

static int verifyCli()
{
    const auto r = TorIntegrity::verifyBundle();
    std::printf("%s\n", qPrintable(TorIntegrity::describe(r)));
    if (!TorIntegrity::bakedBundleInfo().isEmpty())
        std::printf("Baked bundle info:\n%s\n", qPrintable(TorIntegrity::bakedBundleInfo()));
    QString detail;
    const auto port = TorIntegrity::socksPortState(r, &detail);
    std::printf("SOCKS 127.0.0.1:%u: %s%s\n", unsigned(kSocksPort),
                port == TorIntegrity::PortState::Free ? "free"
                : port == TorIntegrity::PortState::OurTor ? "verified bundled tor" : "FOREIGN",
                detail.isEmpty() ? "" : qPrintable(QStringLiteral(" (") + detail + ')'));
    std::printf("Control socket %s: %s\n", qPrintable(TorControl::socketPath()),
                QFileInfo::exists(TorControl::socketPath()) ? "present" : "absent");
    // Exit code reflects the bundle only; a busy port is reported, not fatal here.
    return r.ok() ? 0 : 1;
}

// Print the circuit the route panel would show. Needs a running Onionize
// window (this never starts Tor itself).
static int circuitCli(int argc, char* argv[], const QString& host)
{
    QCoreApplication app(argc, argv);
    const auto r = TorIntegrity::verifyBundle();
    if (!r.ok()) {
        std::printf("%s\n", qPrintable(TorIntegrity::describe(r)));
        return 1;
    }
    int rc = 1;
    TorControl::queryCircuit(&app, host, {}, [&rc](const TorControl::Circuit& c) {
        if (!c.ok) {
            std::printf("circuit unknown: %s\n", qPrintable(c.error));
        } else {
            const char* pick = c.pick == TorControl::Pick::Stream ? "stream to the host"
                             : c.pick == TorControl::Pick::ExitMatch ? "exit matches check"
                             : "newest built circuit";
            std::printf("verified tor pid %lld, circuit %s (%s%s%s)\n", c.torPid, qPrintable(c.id), pick,
                        c.streamTarget.isEmpty() ? "" : ": ", qPrintable(c.streamTarget));
            const auto print = [](const QVector<TorControl::Hop>& hops, const char* indent) {
                for (int i = 0; i < hops.size(); ++i) {
                    const auto& h = hops.at(i);
                    const char* role = i == 0 ? "guard " : i + 1 == hops.size() ? "exit  " : "middle";
                    std::printf("%s%s  %s  %s  %s  %s\n", indent, role, qPrintable(h.fingerprint),
                                qPrintable(h.nickname.isEmpty() ? QStringLiteral("?") : h.nickname),
                                qPrintable(h.ip.isEmpty() ? QStringLiteral("?") : h.ip),
                                qPrintable(h.country.isEmpty() ? QStringLiteral("?") : h.country));
                }
            };
            print(c.hops, "  ");
            for (const auto& leg : c.otherLegs) {
                std::printf("  conflux leg:\n");
                print(leg, "    ");
            }
            rc = 0;
        }
        QCoreApplication::exit(rc);
    });
    app.exec();
    return rc;
}

int main(int argc, char* argv[])
{
    bool anon = false;
    bool tor = false;
    for (int i = 1; i < argc; ++i) {
        const QByteArray a = argv[i];
        if (a == "--anon")
            anon = true;
        if (a == "--tor")
            tor = true;
        if (a == "--verify-tor")
            return verifyCli();
        if (a == "--tor-circuit")
            return circuitCli(argc, argv, i + 1 < argc ? QString::fromLocal8Bit(argv[i + 1]) : QString());
        if (a == "--version") {
            std::printf("Inco Browser %s\n", INCO_VERSION);
            return 0;
        }
    }

    // Search engine handed over by the window that opened this Onionize
    // process; taken out of the environment before tor is started.
    SearchEngine::takeInherited();

    // No core dumps and no same-user ptrace of the process that holds the
    // vault key. INCO_ALLOW_DEBUG=1 keeps the process debuggable.
    if (qgetenv("INCO_ALLOW_DEBUG") != "1")
        ::prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);

    // inco:// links (new-tab search, unlock, onionize) never load anything:
    // IncoPage handles them and refuses them unless Inco's own qrc pages ask.
    // A local scheme would make Chromium block them from qrc pages as well.
    QWebEngineUrlScheme scheme("inco");
    scheme.setSyntax(QWebEngineUrlScheme::Syntax::Host);
    scheme.setFlags(QWebEngineUrlScheme::SecureScheme);
    QWebEngineUrlScheme::registerScheme(scheme);

    QProcess* torProc = nullptr;
    QString torFailure;

    QByteArray flags = "--autoplay-policy=no-user-gesture-required";
    // Chromium flags are read once at startup, so Low RAM's process and V8
    // limits apply from the next launch. QSettings works before QApplication
    // because SettingsStore names the organization and app explicitly.
    if (SettingsStore().load().lowRam) {
        flags += " --process-per-site";            // same-site tabs share one renderer
        flags += " --renderer-process-limit=3";    // soft cap; site isolation still wins
        flags += " --enable-low-end-device-mode";  // smaller V8 heap, caches, raster tiles
        flags += " --disable-features=BackForwardCache";
    }
    if (tor) {
        const auto verified = TorIntegrity::verifyBundle();
        if (!verified.ok()) {
            torFailure = TorIntegrity::describe(verified);
        } else {
            QString detail;
            switch (TorIntegrity::socksPortState(verified, &detail)) {
            case TorIntegrity::PortState::Foreign:
                torFailure = detail;
                break;
            case TorIntegrity::PortState::OurTor:
                std::fprintf(stderr, "Inco: reusing verified bundled tor on 127.0.0.1:%u\n", unsigned(kSocksPort));
                break;
            case TorIntegrity::PortState::Free:
                torProc = startBundledTor(verified, &torFailure);
                break;
            }
        }
        if (torFailure.isEmpty()) {
            flags += " --proxy-server=socks5://127.0.0.1:" + QByteArray::number(kSocksPort);
            flags += " --force-webrtc-ip-handling-policy=disable_non_proxied_udp";
        }
    }
    if (torFailure.isEmpty())
        qputenv("QTWEBENGINE_CHROMIUM_FLAGS", flags);

    QApplication app(argc, argv);
    QApplication::setApplicationName("IncoBrowser");
    QApplication::setApplicationVersion(INCO_VERSION);
    QApplication::setOrganizationName("Inco");
    QApplication::setWindowIcon(QIcon(":/inco-browser.png"));
    {
        SettingsStore store;
        const auto s = store.load();
        I18n::setLanguage(s.language);
    }

    if (!torFailure.isEmpty()) {
        // Onionize window only: never open an un-proxied "anonymous" window.
        std::fprintf(stderr, "Inco: Onionize refused: %s\n", qPrintable(torFailure));
        QMessageBox box(QMessageBox::Critical, I18n::tr("integrity.title"),
                        I18n::tr("integrity.body"));
        box.setInformativeText(torFailure);
        box.exec();
        delete torProc;
        return 3;
    }

    auto* window = new MainWindow(anon, tor);
    window->show();
    const int rc = app.exec();
    // Pages must go before their QWebEngineProfile, which happens in
    // ~MainWindow; leaking the window made Qt warn about it at shutdown.
    delete window;
    if (torProc) {
        torProc->terminate();
        torProc->waitForFinished(2000);
        delete torProc;
    }
    return rc;
}
