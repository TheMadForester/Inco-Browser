#pragma once

#include <QString>

// Unpacks .zip and .crx (v2/v3) extension packages. Qt WebEngine installs
// unpacked folders reliably, so archives are extracted here first.
namespace ExtensionArchive {

enum class Result { Ok, NotArchive, Corrupt, Unsafe, WriteFailed };

// Extracts file into destDir (which must exist). Never writes outside it.
Result extract(const QString& file, const QString& destDir);

// The folder holding manifest.json: destDir itself, or its only subfolder
// when the archive wraps everything in one top-level directory. Empty if none.
QString manifestRoot(const QString& destDir);

} // namespace ExtensionArchive
