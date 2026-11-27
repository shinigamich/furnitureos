// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/update_zip.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <array>
#include <set>

namespace fos::app {
namespace {

using core::ErrorCode;
using core::makeError;

std::string s(const QString& q) { return q.toStdString(); }

std::uint32_t le16(const QByteArray& b, qsizetype at) {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(b[at])) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(b[at + 1])) << 8);
}
std::uint32_t le32(const QByteArray& b, qsizetype at) { return le16(b, at) | (le16(b, at + 2) << 16); }

constexpr std::uint32_t kEocdSignature = 0x06054b50, kCentralSignature = 0x02014b50, kLocalSignature = 0x04034b50;

struct Entry {
    QString name;
    std::uint32_t flags = 0, method = 0, crc = 0, compressedSize = 0, size = 0, localOffset = 0;
};

const std::array<std::uint32_t, 256>& crcTable() {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();
    return table;
}

} // namespace

std::uint32_t crc32(const QByteArray& data, std::uint32_t crc) {
    const auto& t = crcTable();
    crc = ~crc;
    for (const char ch : data) crc = t[(crc ^ static_cast<unsigned char>(ch)) & 0xffu] ^ (crc >> 8);
    return ~crc;
}

QString safeZipEntryPath(const QString& name) {
    QString n = name;
    n.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (n.startsWith(QLatin1Char('/')) || (n.size() >= 2 && n.at(1) == QLatin1Char(':'))) return {};
    QStringList parts;
    for (const QString& part : n.split(QLatin1Char('/'))) {
        if (part.isEmpty() || part == QLatin1String(".")) continue;
        if (part == QLatin1String("..")) return {};
        parts << part;
    }
    return parts.join(QLatin1Char('/'));
}

core::Result<ZipExtractStats> extractZip(const QString& zipPath, const QString& destDir) {
    QFile zip(zipPath);
    if (!zip.open(QIODevice::ReadOnly))
        return makeError(ErrorCode::IoError, "Cannot open " + s(zipPath) + ": " + s(zip.errorString()) + ".");
    const qint64 fileSize = zip.size();
    const std::string label = s(QFileInfo(zipPath).fileName());
    if (fileSize < 22) return makeError(ErrorCode::CorruptData, label + " is too small to be a ZIP archive.");

    // End of central directory: the last 22 + up to 65535 (comment) bytes.
    const qint64 tailSize = std::min<qint64>(fileSize, 22 + 65535);
    zip.seek(fileSize - tailSize);
    const QByteArray tail = zip.read(tailSize);
    qsizetype eocd = -1;
    for (qsizetype i = tail.size() - 22; i >= 0; --i)
        if (le32(tail, i) == kEocdSignature) {
            eocd = i;
            break;
        }
    if (eocd < 0) return makeError(ErrorCode::CorruptData, label + " is not a ZIP archive (no end-of-directory record).");
    const std::uint32_t entryCount = le16(tail, eocd + 10);
    const std::uint32_t cdSize = le32(tail, eocd + 12), cdOffset = le32(tail, eocd + 16);
    if (le16(tail, eocd + 4) != 0 || le16(tail, eocd + 6) != 0)
        return makeError(ErrorCode::Unsupported, label + " is a multi-part ZIP archive, which is not supported.");
    if (entryCount == 0xffff || cdOffset == 0xffffffffu || cdSize == 0xffffffffu)
        return makeError(ErrorCode::Unsupported, label + " is a ZIP64 archive, which is not supported.");
    if (static_cast<qint64>(cdOffset) + cdSize > fileSize)
        return makeError(ErrorCode::CorruptData, label + ": the central directory lies outside the file.");

    zip.seek(cdOffset);
    const QByteArray cd = zip.read(cdSize);
    if (cd.size() != static_cast<qsizetype>(cdSize))
        return makeError(ErrorCode::IoError, "Reading the directory of " + label + " failed.");
    std::vector<Entry> entries;
    qsizetype p = 0;
    for (std::uint32_t i = 0; i < entryCount; ++i) {
        if (p + 46 > cd.size() || le32(cd, p) != kCentralSignature)
            return makeError(ErrorCode::CorruptData, label + ": directory entry " + std::to_string(i + 1) + " is damaged.");
        Entry e;
        e.flags = le16(cd, p + 8);
        e.method = le16(cd, p + 10);
        e.crc = le32(cd, p + 16);
        e.compressedSize = le32(cd, p + 20);
        e.size = le32(cd, p + 24);
        const std::uint32_t nameLen = le16(cd, p + 28), extraLen = le16(cd, p + 30), commentLen = le16(cd, p + 32);
        e.localOffset = le32(cd, p + 42);
        if (p + 46 + nameLen > cd.size())
            return makeError(ErrorCode::CorruptData, label + ": directory entry " + std::to_string(i + 1) + " is damaged.");
        const QByteArray rawName = cd.mid(p + 46, nameLen);
        e.name = (e.flags & 0x800u) ? QString::fromUtf8(rawName) : QString::fromLatin1(rawName);
        p += 46 + nameLen + extraLen + commentLen;
        entries.push_back(std::move(e));
    }

    const QDir dest(destDir);
    if (!dest.exists()) return makeError(ErrorCode::NotFound, "The extraction folder " + s(destDir) + " does not exist.");
    ZipExtractStats stats;
    std::set<QString> seen;
    for (const Entry& e : entries) {
        const std::string what = label + " entry '" + s(e.name) + "'";
        const QString rel = safeZipEntryPath(e.name);
        if (rel.isEmpty() && !e.name.isEmpty() && e.name != QLatin1String("/") && e.name != QLatin1String("./"))
            return makeError(ErrorCode::CorruptData, what + " points outside the destination folder; the archive is refused.");
        if (rel.isEmpty()) continue;
        if (e.flags & 0x1u) return makeError(ErrorCode::Unsupported, what + " is encrypted, which is not supported.");
        if (!seen.insert(rel.toLower()).second)
            return makeError(ErrorCode::CorruptData, what + " appears twice in the archive.");
        const QString target = dest.filePath(rel);
        const bool isDir = e.name.endsWith(QLatin1Char('/')) || e.name.endsWith(QLatin1Char('\\'));
        if (isDir) {
            if (!dest.mkpath(rel)) return makeError(ErrorCode::IoError, "Cannot create folder " + s(target) + ".");
            ++stats.directories;
            continue;
        }
        if (e.method != 0 && e.method != 8)
            return makeError(ErrorCode::Unsupported, what + " uses compression method " + std::to_string(e.method) +
                                                         "; only stored and deflate are supported.");
        // Local header: its name/extra lengths may differ from the central directory's.
        if (!zip.seek(e.localOffset)) return makeError(ErrorCode::CorruptData, what + " has an invalid offset.");
        const QByteArray local = zip.read(30);
        if (local.size() != 30 || le32(local, 0) != kLocalSignature)
            return makeError(ErrorCode::CorruptData, what + " has a damaged local header.");
        const qint64 dataStart = static_cast<qint64>(e.localOffset) + 30 + le16(local, 26) + le16(local, 28);
        if (dataStart + e.compressedSize > fileSize)
            return makeError(ErrorCode::CorruptData, what + " extends beyond the end of the archive.");
        zip.seek(dataStart);
        const QByteArray packed = zip.read(e.compressedSize);
        if (packed.size() != static_cast<qsizetype>(e.compressedSize))
            return makeError(ErrorCode::IoError, "Reading " + what + " failed.");
        QByteArray data;
        if (e.method == 0) {
            if (e.compressedSize != e.size)
                return makeError(ErrorCode::CorruptData, what + " is stored but its two sizes differ.");
            data = packed;
        } else {
            auto inflated = inflateRaw(packed, e.size);
            if (!inflated) return makeError(ErrorCode::CorruptData, what + ": " + inflated.error().message);
            data = std::move(inflated).value();
        }
        if (crc32(data) != e.crc) return makeError(ErrorCode::CorruptData, what + " fails its CRC-32 check.");
        if (!dest.mkpath(QFileInfo(rel).path()))
            return makeError(ErrorCode::IoError, "Cannot create the folder for " + s(target) + ".");
        QFile out(target);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(data) != data.size() || !out.flush())
            return makeError(ErrorCode::IoError, "Cannot write " + s(target) + ": " + s(out.errorString()) + ".");
        ++stats.files;
        stats.bytes += data.size();
    }
    return stats;
}

} // namespace fos::app
