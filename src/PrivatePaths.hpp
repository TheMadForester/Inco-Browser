#pragma once

#include <QString>

// Where Inco keeps things, and the modes it keeps them with.
namespace PrivatePaths {

// ~/.config/Inco/IncoBrowser (vault, bookmarks, web profile). Works before
// QApplication exists because it does not rely on the application name.
QString dataDir();

// Per-user tmpfs folder for runtime state, mode 0700:
// $XDG_RUNTIME_DIR/inco-browser, else a random 0700 folder under the temp
// dir that lives as long as this process. Never inside the home folder.
QString runtimeDir();

// mkpath + chmod 0700. Returns false if the folder is not there afterwards.
bool makePrivateDir(const QString& path);
// chmod 0600 if the file exists.
void restrictFile(const QString& path);
// chmod 0700 if the folder exists (never creates it).
void restrictDir(const QString& path);

} // namespace PrivatePaths
