#include "MainWindow.hpp"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTcpSocket>
#include <QThread>
#include <QWebEngineUrlScheme>
#include <cstdio>

static bool portOpen(quint16 port)
{
    QTcpSocket s;
    s.connectToHost(QStringLiteral("127.0.0.1"), port);
    return s.waitForConnected(300);
}

static QString bundledTor(const QString& appDir)
{
    const QString a = appDir + "/tor/tor";
    const QString b = appDir + "/../tor/tor";
    if (QFileInfo::exists(a) && QFileInfo(a).isExecutable())
        return QFileInfo(a).absoluteFilePath();
    if (QFileInfo::exists(b) && QFileInfo(b).isExecutable())
        return QFileInfo(b).absoluteFilePath();
    return {};
}

static QProcess* startBundledTor(const QString& bin)
{
    if (portOpen(9250))
        return nullptr;

    const QString libdir = QFileInfo(bin).absolutePath();
    const QString data = QDir::tempPath() + "/inco-tor";
    QDir().mkpath(data);

    auto* p = new QProcess;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("LD_LIBRARY_PATH",
               libdir + ":" + libdir + "/lib:" + env.value("LD_LIBRARY_PATH"));
    p->setProcessEnvironment(env);
    p->setWorkingDirectory(libdir);
    p->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    p->start(bin, {
        "--SocksPort", "127.0.0.1:9250",
        "--ControlPort", "0",
        "--DataDirectory", data,
        "--ClientOnly", "1",
        "--ignore-missing-torrc"
    });
    if (!p->waitForStarted(2000)) {
        std::fprintf(stderr, "Inco: failed to exec %s\n", qPrintable(bin));
        delete p;
        return nullptr;
    }
    for (int i = 0; i < 60; ++i) {
        if (portOpen(9250)) {
            std::fprintf(stderr, "Inco: bundled tor on 127.0.0.1:9250\n");
            return p;
        }
        QThread::msleep(250);
    }
    std::fprintf(stderr, "Inco: tor started but SOCKS never opened\n");
    p->kill();
    p->waitForFinished(1000);
    delete p;
    return nullptr;
}

int main(int argc, char* argv[])
{
    QWebEngineUrlScheme scheme("inco");
    scheme.setSyntax(QWebEngineUrlScheme::Syntax::Host);
    scheme.setFlags(QWebEngineUrlScheme::SecureScheme |
                    QWebEngineUrlScheme::LocalScheme |
                    QWebEngineUrlScheme::LocalAccessAllowed);
    QWebEngineUrlScheme::registerScheme(scheme);

    bool anon = false;
    bool tor = false;
    for (int i = 1; i < argc; ++i) {
        const QByteArray a = argv[i];
        if (a == "--anon")
            anon = true;
        if (a == "--tor")
            tor = true;
    }

    const QString appDir = QFileInfo(QString::fromLocal8Bit(argv[0])).absolutePath();
    QProcess* torProc = nullptr;

    QByteArray flags = "--autoplay-policy=no-user-gesture-required";
    if (tor) {
        const QString bin = bundledTor(appDir);
        if (bin.isEmpty() && !portOpen(9250)) {
            std::fprintf(stderr, "Inco: no bundled tor at %s/tor/tor\n", qPrintable(appDir));
            return 2;
        }
        if (!portOpen(9250) && !bin.isEmpty())
            torProc = startBundledTor(bin);
        if (!portOpen(9250)) {
            std::fprintf(stderr, "Inco: SOCKS 9250 not available\n");
            delete torProc;
            return 2;
        }
        flags += " --proxy-server=socks5://127.0.0.1:9250";
        flags += " --force-webrtc-ip-handling-policy=disable_non_proxied_udp";
        std::fprintf(stderr, "Inco: using SOCKS 127.0.0.1:9250\n");
    }
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS", flags);

    QApplication app(argc, argv);
    QApplication::setApplicationName("IncoBrowser");
    QApplication::setApplicationVersion("0.1.0");
    QApplication::setOrganizationName("Inco");
    QApplication::setWindowIcon(QIcon(":/inco-browser.png"));

    auto* window = new MainWindow(anon, tor);
    window->show();
    const int rc = app.exec();
    if (torProc) {
        torProc->terminate();
        torProc->waitForFinished(2000);
        delete torProc;
    }
    return rc;
}
