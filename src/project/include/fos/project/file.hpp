// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// The native `.fosp` file: container kind "furnitureos-project" (formats module), payload as
/// documented in src/project/README.md.
///
/// VERSIONING POLICY. kProjectFormatVersion is the newest payload version this build writes and
/// reads. It is increased only when an older reader would misinterpret or reject the payload
/// (purely additive optional keys do not bump it; they are preserved as unknown keys by older
/// readers where the object is extensible). Every bump N -> N+1 adds exactly one migration step
/// in registerProjectMigrations() (src/file.cpp) that rewrites a version-N payload in memory and
/// keeps keys it does not know. Files newer than kProjectFormatVersion are refused with
/// UnsupportedVersion and never partially loaded. There is no downgrade path. Version 1 has no
/// migrations.
#include "fos/formats/atomic_file.hpp"
#include "fos/formats/container.hpp"
#include "fos/formats/recovery.hpp"
#include "fos/project/resolve.hpp"

#include <filesystem>
#include <string>

namespace fos::project {

inline constexpr const char* kProjectFormatKind = "furnitureos-project";
inline constexpr int kProjectFormatVersion = 1;
inline constexpr const char* kProjectFileExtension = ".fosp";

/// Adds the project payload migrations (N -> N+1) to `registry`.
void registerProjectMigrations(formats::MigrationRegistry& registry);
/// Kind, current version and migrations of the `.fosp` format.
formats::FormatSpec projectFormatSpec();

/// The container document of `project` (app = FurnitureOS and this build's version, `savedAt`
/// as given: empty = filled by the save clock).
formats::Document toDocument(const Project& project, std::string savedAt = {});

/// Complete file text (envelope + payload + checksum) for a fixed `savedAt`.
core::Result<std::string> serializeProject(const Project& project, const std::string& savedAt);

struct LoadedProject {
    Project project;
    Resolved resolved;   ///< resolve(project): proof that the project is usable.
    int fileVersion = 0; ///< Version found in the file (before migrations).
    std::vector<formats::AppliedMigration> migrations;
    /// Container warnings (e.g. accepted checksum mismatch) followed by resolved.diagnostics.
    core::Diagnostics diagnostics;
};

/// Container checks (checksum, version, migrations), strict payload decoding, then resolve().
/// Generator findings (warnings, an item that cannot be generated) are diagnostics and never
/// refuse the file. Errors: those of formats::parseDocument, ParseError with a JSON-pointer
/// path into "/payload/...", InvalidArgument for duplicate/empty ids.
core::Result<LoadedProject> parseProject(std::string_view text, const formats::LoadOptions& options = {},
                                         std::string_view sourceName = "project");
core::Result<LoadedProject> loadProject(const std::filesystem::path& path, const formats::LoadOptions& options = {});

/// Atomic save with rotating backups (formats::saveDocument). `options.backupCount` is taken
/// from project.settings.backupCount. The project is first encoded and decoded again; a project
/// that would not load back (e.g. a non-rigid placement, a NaN) is refused and nothing is written.
core::Result<formats::SaveReport> saveProject(const std::filesystem::path& path, const Project& project,
                                              formats::SaveOptions options = {});
/// formats::writeAutosave of the project to `<path>.autosave` (the file at `path` is untouched).
core::Result<formats::SaveReport> writeProjectAutosave(const std::filesystem::path& path, const Project& project,
                                                       const formats::SaveOptions& options = {});
/// Newest valid autosave/backup of `path` (formats::loadNewestValid), decoded and resolved.
core::Result<LoadedProject> recoverProject(const std::filesystem::path& path, const formats::LoadOptions& options = {});

} // namespace fos::project
