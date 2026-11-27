// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Minimal, self-contained ZIP extraction for the portable-update path (Qt has no public ZIP
/// API and no third-party dependency is wanted). Supports the subset release archives use:
/// methods "stored" (0) and "deflate" (8), no encryption, no ZIP64, archives below 4 GiB.
/// Every entry's CRC-32 and size are checked; entry names that are absolute or contain ".."
/// are refused (no writing outside the destination).
#include "fos/core/result.hpp"

#include <QByteArray>
#include <QString>

#include <cstdint>

namespace fos::app {

/// CRC-32 (IEEE 802.3, as used by ZIP and gzip) of `data`, continuing from `crc`.
std::uint32_t crc32(const QByteArray& data, std::uint32_t crc = 0);

/// Inflates a raw DEFLATE stream (RFC 1951, no zlib/gzip header). The output must be exactly
/// `expectedSize` bytes (larger output is cut off with an error: no unbounded growth).
core::Result<QByteArray> inflateRaw(const QByteArray& compressed, qint64 expectedSize);

struct ZipExtractStats {
    int files = 0;
    int directories = 0;
    qint64 bytes = 0; ///< total uncompressed bytes written
};

/// Extracts `zipPath` into the existing, empty directory `destDir`. On error, files already
/// written stay in `destDir`; the caller owns that (staging) directory and removes it.
core::Result<ZipExtractStats> extractZip(const QString& zipPath, const QString& destDir);

/// The safe relative form of a ZIP entry name ('\\' read as '/'), or an empty string when
/// the name is absolute, has a drive letter, or contains a ".." component.
QString safeZipEntryPath(const QString& name);

} // namespace fos::app
