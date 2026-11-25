// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Crash-recovery store: a directory (the app uses AppDataLocation/recovery) holding one
/// recovery copy per open document, `<key>.fosp`, plus a small sidecar `<key>.json` with the
/// original path, the time of the autosave and the application version.
///
/// Keys: a saved document uses recoveryKey(path) = the first 16 hex digits of the SHA-256 of
/// its absolute, cleaned path (stable across runs); an untitled document uses
/// "untitled-<id>" (the project id, a UUID). The user's own file is never written here.
#include "fos/core/result.hpp"
#include "fos/project/project.hpp"

#include <QByteArray>
#include <QDateTime>
#include <QString>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace fos::app {

inline constexpr const char* kRecoverySidecarKind = "furnitureos-recovery";
inline constexpr int kRecoverySidecarVersion = 1;

/// Contents of a `<key>.json` sidecar.
struct RecoverySidecar {
    QString originalPath; ///< Absolute path of the user's file; empty = never saved.
    QString projectName;  ///< Project name at the time of the autosave (for display).
    QDateTime savedAt;    ///< UTC time of the autosave.
    QString appVersion;   ///< applicationVersion() of the writer.
};

/// JSON text of the sidecar (kind, version, originalPath, projectName, savedAt ISO-8601 UTC,
/// appVersion).
QByteArray serializeRecoverySidecar(const RecoverySidecar& sidecar);
/// Parses a sidecar; nullopt for anything that is not a JSON object of kind
/// kRecoverySidecarKind with a valid "savedAt" and string fields (never throws, never crashes
/// on truncated or binary input). Unknown keys are ignored; a missing projectName/appVersion
/// is empty; a newer "version" is accepted as long as these fields read.
std::optional<RecoverySidecar> parseRecoverySidecar(const QByteArray& json);

/// Key of a saved document: 16 lowercase hex digits of SHA-256(absolute cleaned path, UTF-8).
std::string recoveryKey(const std::filesystem::path& originalPath);
/// Key of an untitled document: "untitled-<untitledId>" (`untitledId` sanitised to
/// [A-Za-z0-9-]; empty -> "untitled-unnamed").
std::string untitledRecoveryKey(const std::string& untitledId);

/// One recovery copy found in the store.
struct RecoveryEntry {
    std::string key;
    std::filesystem::path file;    ///< `<dir>/<key>.fosp`.
    std::filesystem::path sidecar; ///< `<dir>/<key>.json` (may be missing).
    bool sidecarValid = false;     ///< False: missing or unreadable sidecar (fields below are fallbacks).
    QString originalPath;          ///< Empty when untitled or the sidecar is not valid.
    QString projectName;
    QDateTime savedAt;             ///< Sidecar time, or the file's modification time (UTC).
    QString appVersion;
    bool untitled() const { return originalPath.isEmpty(); }
};

class RecoveryStore {
public:
    explicit RecoveryStore(std::filesystem::path directory);
    const std::filesystem::path& directory() const noexcept { return dir_; }

    /// `<dir>/<key>.fosp` and `<dir>/<key>.json`.
    std::filesystem::path fileFor(const std::string& key) const;
    std::filesystem::path sidecarFor(const std::string& key) const;

    /// Writes `project` atomically to fileFor(key) without backups, then the sidecar
    /// (atomically). Creates the directory. `originalPath` empty = untitled. Returns the file.
    core::Result<std::filesystem::path> write(const std::string& key, const project::Project& project,
                                              const std::filesystem::path& originalPath,
                                              const QDateTime& savedAtUtc = QDateTime::currentDateTimeUtc()) const;

    /// Every `<key>.fosp` in the directory (missing directory = empty list), newest savedAt
    /// first, ties by key. Sidecars without a `.fosp` and temporary files are ignored.
    std::vector<RecoveryEntry> list() const;
    /// Entry of `key`, nullopt when there is no `<key>.fosp`.
    std::optional<RecoveryEntry> find(const std::string& key) const;

    /// Deletes `<key>.fosp` and `<key>.json`; true when neither exists afterwards.
    bool remove(const std::string& key) const;

private:
    std::filesystem::path dir_;
};

} // namespace fos::app
