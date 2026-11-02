#include "PrivatePaths.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>

#include <sys/stat.h>

namespace PrivatePaths {

QString dataDir()
{
    // Same file QSettings("Inco", "IncoBrowser") uses; its folder plus the
    // application name is what AppConfigLocation resolves to.
    const QSettings conf(QStringLiteral("Inco"), QStringLiteral("IncoBrowser"));
    return QFileInfo(conf.fileName()).absolutePath() + QStringLiteral("/IncoBrowser");
}

QString runtimeDir()
{
    const QString runtime = QString::fromLocal8Bit(qgetenv("XDG_RUNTIME_DIR"));
    if (!runtime.isEmpty() && QFileInfo(runtime).isDir() && QFileInfo(runtime).isWritable()) {
        const QString base = runtime + QStringLiteral("/inco-browser");
        if (makePrivateDir(base))
            return base;
    }
    // mkdtemp gives a fresh 0700 folder with a random name, so another local
    // user cannot pre-create or symlink it. Removed when the process exits.
    static QTemporaryDir fallback(QDir::tempPath() + QStringLiteral("/inco-browser-XXXXXX"));
    if (fallback.isValid()) {
        restrictDir(fallback.path());
        return fallback.path();
    }
    return {};
}

bool makePrivateDir(const QString& path)
{
    if (path.isEmpty())
        return false;
    QDir().mkpath(path);
    if (!QFileInfo(path).isDir())
        return false;
    ::chmod(QFile::encodeName(path).constData(), 0700);
    return true;
}

void restrictFile(const QString& path)
{
    if (QFileInfo(path).isFile())
        QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

void restrictDir(const QString& path)
{
    if (QFileInfo(path).isDir())
        ::chmod(QFile::encodeName(path).constData(), 0700);
}

} // namespace PrivatePaths
