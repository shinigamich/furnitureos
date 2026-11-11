// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/diagnostic.hpp"
#include "fos/core/result.hpp"
#include "fos/formats/json.hpp"

#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

/// Versioned document container: the envelope shared by the native `.fosp` project
/// files and `.foslib` library files (ARCHITECTURE.md section 6, src/formats/README.md).
///
///   { "format": "<kind>", "formatVersion": N,
///     "app": {"name": "FurnitureOS", "version": "..."},
///     "savedAt": "2026-01-31T12:00:00Z",
///     "checksum": {"algorithm": "sha256", "value": "<hex>"},
///     "payload": { ... } }
///
/// The container never interprets the payload: every key in it survives load -> save.
namespace fos::formats {

/// Application that wrote a document.
struct AppInfo {
    std::string name = "FurnitureOS";
    std::string version;
};

/// In-memory form of a container file.
struct Document {
    std::string format;           ///< Kind tag, e.g. "furnitureos-project".
    int formatVersion = 1;        ///< Version of the payload schema (>= 1).
    AppInfo app;
    std::string savedAt;          ///< ISO-8601 UTC timestamp, e.g. "2026-01-31T12:00:00Z".
    Json payload = Json::object();
    /// Envelope keys this version does not know (kept and written back unchanged).
    Json extra = Json::object();
};

/// One migration step: rewrites a payload of version N in place into version N + 1.
using MigrationStep = std::function<core::Result<void>(Json& payload)>;

struct AppliedMigration {
    int fromVersion = 0;
    int toVersion = 0;
    std::string description;
};

/// Ordered set of N -> N+1 payload migrations of one document kind.
class MigrationRegistry {
public:
    /// Registers the step upgrading `fromVersion` to `fromVersion + 1`.
    /// Fails for fromVersion < 1, an empty step, or a version registered twice.
    core::Result<void> add(int fromVersion, MigrationStep step, std::string description = {});
    bool has(int fromVersion) const { return steps_.count(fromVersion) != 0; }

    /// Applies the steps fromVersion -> ... -> toVersion in order. All-or-nothing:
    /// `payload` is modified only when every step succeeded. A missing step, a failing
    /// step or a step that throws yields an error naming the step.
    core::Result<std::vector<AppliedMigration>> run(Json& payload, int fromVersion, int toVersion) const;

private:
    struct Entry {
        MigrationStep step;
        std::string description;
    };
    std::map<int, Entry> steps_;
};

/// What a reader accepts: the kind tag, the newest version it understands and how to
/// upgrade older payloads.
struct FormatSpec {
    std::string kind;
    int currentVersion = 1;
    MigrationRegistry migrations;
};

struct LoadOptions {
    /// Recovery switch. When true, a missing or wrong checksum does not fail the load;
    /// the payload is returned together with a Warning diagnostic
    /// ("formats.container.checksum_mismatch" / "formats.container.checksum_missing").
    bool allowChecksumMismatch = false;
};

struct LoadedDocument {
    Document document;                        ///< formatVersion == spec.currentVersion after migration.
    int fileVersion = 0;                      ///< formatVersion found in the file.
    std::vector<AppliedMigration> migrations; ///< Steps that ran, in order (empty for a current file).
    core::Diagnostics diagnostics;            ///< Warnings (never errors).
};

/// Canonical text of a JSON value, the input of the checksum: object keys sorted by
/// byte-wise comparison of their UTF-8 names at every level, no whitespace, strings
/// not ASCII-escaped. Fails (InvalidArgument) when a string is not valid UTF-8.
core::Result<std::string> canonicalDump(const Json& value);

/// Lower-case hex SHA-256 of canonicalDump(payload).
core::Result<std::string> payloadChecksum(const Json& payload);

/// "YYYY-MM-DDTHH:MM:SSZ" (UTC, whole seconds).
std::string formatIso8601Utc(std::chrono::system_clock::time_point time);

/// Envelope text (UTF-8, 2-space indented, trailing newline) with a fresh checksum.
/// Fails when format is empty, formatVersion < 1, the payload is not an object or
/// contains invalid UTF-8.
core::Result<std::string> serializeDocument(const Document& document);

/// Parses and validates envelope text, verifies the checksum and migrates the payload
/// to spec.currentVersion. `sourceName` is used in messages (normally the file name).
/// Errors: ParseError (not JSON; message has line and column), CorruptData (not an
/// object, missing/ill-typed envelope keys, missing payload, checksum mismatch),
/// Unsupported (other `format` kind), UnsupportedVersion (newer than supported — the
/// payload is never touched; or a migration step is missing).
core::Result<LoadedDocument> parseDocument(std::string_view text, const FormatSpec& spec,
                                           const LoadOptions& options = {}, std::string_view sourceName = "document");

/// Whole file as bytes. IoError when it is missing, a directory or unreadable.
core::Result<std::string> readFile(const std::filesystem::path& path);

/// readFile + parseDocument.
core::Result<LoadedDocument> loadDocument(const std::filesystem::path& path, const FormatSpec& spec,
                                          const LoadOptions& options = {});

} // namespace fos::formats
