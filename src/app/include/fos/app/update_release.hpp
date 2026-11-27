// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Widget-free logic of the in-app updater: parsing the GitHub Releases API response, the
/// release's `update.json` manifest and `SHA256SUMS.txt`, choosing the release to offer and
/// verifying a downloaded file. No networking here (the GUI fetches, this module decides).
///
/// Release contract (see src/app/README.md, "Updates"): every release tagged vX.Y.Z of
/// kUpdateRepository carries the assets `update.json` and `SHA256SUMS.txt`, plus
/// `FurnitureOS-X.Y.Z-windows-x64.zip` and optionally `FurnitureOS-X.Y.Z-windows-x64-setup.exe`.
#include "fos/app/update_version.hpp"

#include "fos/core/result.hpp"

#include <QByteArray>
#include <QString>
#include <QUrl>

#include <map>
#include <optional>
#include <vector>

namespace fos::app {

inline constexpr const char* kUpdateRepository = "shinigamich/furnitureos";
inline constexpr const char* kUpdateUserAgent = "FurnitureOS-Updater";
inline constexpr const char* kUpdateManifestName = "update.json";
inline constexpr const char* kUpdateChecksumsName = "SHA256SUMS.txt";
/// Keys of update.json "assets".
inline constexpr const char* kAssetWindowsZip = "windows-x64-zip";
inline constexpr const char* kAssetWindowsSetup = "windows-x64-setup";

/// API endpoint: ".../releases/latest" (stable channel; GitHub excludes drafts and
/// pre-releases) or ".../releases?per_page=30" when pre-releases are included.
QUrl releasesApiUrl(bool includePrereleases);

/// One downloadable file of a GitHub release.
struct ReleaseAsset {
    QString name;
    qint64 size = 0;
    QUrl downloadUrl; ///< browser_download_url (https).
};

/// The fields of a GitHub release the updater uses.
struct ReleaseInfo {
    QString tag;          ///< e.g. "v0.2.0-beta.1"
    SemVer version;       ///< parsed from the tag
    bool prerelease = false;
    bool draft = false;
    QString htmlUrl;      ///< release page
    QString publishedAt;  ///< ISO 8601 as given by GitHub
    std::vector<ReleaseAsset> assets;

    const ReleaseAsset* findAsset(const QString& name) const;
};

/// Parses the body of `/releases/latest` (a JSON object) or `/releases` (a JSON array).
/// Releases whose tag is not a semantic version are skipped (they cannot be compared);
/// malformed JSON or a non-release document is an error.
core::Result<std::vector<ReleaseInfo>> parseGitHubReleases(const QByteArray& json);

/// The newest non-draft release strictly newer than `current` that carries update.json and
/// SHA256SUMS.txt; pre-releases only when `includePrereleases`. std::nullopt = up to date.
std::optional<ReleaseInfo> selectUpdate(const std::vector<ReleaseInfo>& releases, const SemVer& current,
                                        bool includePrereleases);

/// One asset entry of update.json, completed with the download URL from the release.
struct UpdateAsset {
    QString name;
    QString sha256; ///< 64 lowercase hex digits
    qint64 size = 0;
    QUrl downloadUrl;
};

/// update.json of one release.
struct UpdateManifest {
    SemVer version;
    QString versionText; ///< as written in update.json
    QString published;
    QString notes;       ///< Markdown
    std::map<QString, UpdateAsset> assets; ///< keyed by kAssetWindowsZip / kAssetWindowsSetup

    const UpdateAsset* asset(const char* key) const;
};

/// Parses update.json and cross-checks it against `release`: the version must equal the
/// tag's version, every asset needs a name, a positive size and a 64-digit SHA-256, must be
/// attached to the release with the same size, and gets its download URL from there.
core::Result<UpdateManifest> parseUpdateManifest(const QByteArray& json, const ReleaseInfo& release);

/// Parses `sha256sum` output ("<64 hex>  name" or "<64 hex> *name" per line; blank lines and
/// '#' comments ignored). Returns file name -> lowercase hash. Malformed lines or duplicate
/// names are errors.
core::Result<std::map<QString, QString>> parseSha256Sums(const QByteArray& text);

/// Checks that update.json and SHA256SUMS.txt agree on `asset` (present in both, same hash).
core::Result<void> checkChecksumsAgree(const UpdateAsset& asset, const std::map<QString, QString>& sums);

/// Verifies a download from its measured size and SHA-256 (hex, any case) against both the
/// manifest and SHA256SUMS.txt. Any mismatch is an error naming the expected and actual values.
core::Result<void> verifyDownload(qint64 actualSize, const QString& actualSha256, const UpdateAsset& asset,
                                  const std::map<QString, QString>& sums);

/// Hashes the file at `path` and calls verifyDownload().
core::Result<void> verifyDownloadedFile(const QString& path, const UpdateAsset& asset,
                                        const std::map<QString, QString>& sums);

/// Lowercase hex SHA-256 of a file; error when it cannot be read.
core::Result<QString> sha256OfFile(const QString& path);

} // namespace fos::app
