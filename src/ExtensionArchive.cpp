#include "ExtensionArchive.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <zlib.h>

namespace ExtensionArchive {
namespace {

constexpr qint64 kMaxArchive = 256LL * 1024 * 1024;   // packed size
constexpr qint64 kMaxUnpacked = 512LL * 1024 * 1024;  // sum of all entries

quint16 u16(const QByteArray& d, qint64 at)
{
    const auto* p = reinterpret_cast<const uchar*>(d.constData() + at);
    return quint16(p[0] | (p[1] << 8));
}

quint32 u32(const QByteArray& d, qint64 at)
{
    const auto* p = reinterpret_cast<const uchar*>(d.constData() + at);
    return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
}

// Relative, normalized path inside the archive, or empty when the entry
// would escape the destination (absolute paths, "..", drive letters).
QString safeName(QString name)
{
    name.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (name.isEmpty() || name.startsWith(QLatin1Char('/')) || name.contains(QLatin1Char(':')))
        return {};
    const QString clean = QDir::cleanPath(name);
    if (clean == QLatin1String(".") || clean == QLatin1String("..")
        || clean.startsWith(QLatin1String("../")) || clean.startsWith(QLatin1Char('/')))
        return {};
    return clean;
}

bool inflateRaw(const char* src, qint64 srcLen, QByteArray* out, qint64 expected)
{
    out->resize(expected);
    z_stream z{};
    if (inflateInit2(&z, -MAX_WBITS) != Z_OK)
        return false;
    z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(src));
    z.avail_in = uInt(srcLen);
    z.next_out = reinterpret_cast<Bytef*>(out->data());
    z.avail_out = uInt(expected);
    const int rc = inflate(&z, Z_FINISH);
    const bool ok = rc == Z_STREAM_END && qint64(z.total_out) == expected;
    inflateEnd(&z);
    return ok;
}

} // namespace

Result extract(const QString& file, const QString& destDir)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly) || f.size() > kMaxArchive)
        return Result::NotArchive;
    const QByteArray all = f.readAll();
    if (all.size() < 22)
        return Result::NotArchive;

    // A .crx is a signed header followed by a plain zip.
    qint64 base = 0;
    if (all.startsWith("Cr24")) {
        if (all.size() < 16)
            return Result::Corrupt;
        const quint32 version = u32(all, 4);
        if (version == 3)
            base = 12 + qint64(u32(all, 8));
        else if (version == 2)
            base = 16 + qint64(u32(all, 8)) + qint64(u32(all, 12));
        else
            return Result::NotArchive;
        if (base + 22 > all.size())
            return Result::Corrupt;
    }
    const QByteArray zip = all.mid(base);
    const qint64 n = zip.size();

    // End of central directory record, searched from the back (it may be
    // followed by a comment of up to 64 KiB).
    qint64 eocd = -1;
    for (qint64 i = n - 22; i >= 0 && i >= n - 22 - 65535; --i) {
        if (u32(zip, i) == 0x06054b50u) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0)
        return Result::NotArchive;
    const quint16 count = u16(zip, eocd + 10);
    const qint64 cdSize = u32(zip, eocd + 12);
    const qint64 cdOffset = u32(zip, eocd + 16);
    if (cdOffset + cdSize > eocd)
        return Result::Corrupt;   // also rejects Zip64 (0xFFFFFFFF markers)

    const QDir dest(destDir);
    qint64 unpacked = 0;
    qint64 at = cdOffset;
    for (quint16 e = 0; e < count; ++e) {
        if (at + 46 > eocd || u32(zip, at) != 0x02014b50u)
            return Result::Corrupt;
        const quint16 flags = u16(zip, at + 8);
        const quint16 method = u16(zip, at + 10);
        const qint64 packed = u32(zip, at + 20);
        const qint64 size = u32(zip, at + 24);
        const quint16 nameLen = u16(zip, at + 28);
        const quint16 extraLen = u16(zip, at + 30);
        const quint16 commentLen = u16(zip, at + 32);
        const quint32 attrs = u32(zip, at + 38);
        const qint64 local = u32(zip, at + 42);
        if (at + 46 + nameLen > eocd)
            return Result::Corrupt;
        const QString raw = QString::fromUtf8(zip.constData() + at + 46, nameLen);
        at += 46 + nameLen + extraLen + commentLen;

        if (flags & 0x1)
            return Result::Corrupt;   // encrypted entries are not supported
        const quint32 unixType = (attrs >> 16) & 0170000u;
        if (unixType == 0120000u)
            continue;                 // never recreate symlinks
        const bool isDir = raw.endsWith(QLatin1Char('/')) || raw.endsWith(QLatin1Char('\\'));
        const QString name = safeName(raw);
        if (name.isEmpty())
            return Result::Unsafe;
        if (isDir) {
            if (!dest.mkpath(name))
                return Result::WriteFailed;
            continue;
        }

        if (local + 30 > n || u32(zip, local) != 0x04034b50u)
            return Result::Corrupt;
        const qint64 dataAt = local + 30 + u16(zip, local + 26) + u16(zip, local + 28);
        if (dataAt + packed > n)
            return Result::Corrupt;
        unpacked += size;
        if (unpacked > kMaxUnpacked)
            return Result::Corrupt;

        QByteArray data;
        if (method == 0) {
            if (packed != size)
                return Result::Corrupt;
            data = zip.mid(dataAt, size);
        } else if (method == 8) {
            if (!inflateRaw(zip.constData() + dataAt, packed, &data, size))
                return Result::Corrupt;
        } else {
            return Result::Corrupt;
        }

        const QString target = dest.filePath(name);
        if (!dest.mkpath(QFileInfo(name).path()))
            return Result::WriteFailed;
        QFile out(target);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(data) != data.size())
            return Result::WriteFailed;
    }
    return Result::Ok;
}

QString manifestRoot(const QString& destDir)
{
    const QDir dir(destDir);
    if (dir.exists(QStringLiteral("manifest.json")))
        return dir.absolutePath();
    const QStringList subs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    if (subs.size() == 1 && QFileInfo::exists(dir.filePath(subs.first() + QStringLiteral("/manifest.json"))))
        return dir.absoluteFilePath(subs.first());
    return {};
}

} // namespace ExtensionArchive
