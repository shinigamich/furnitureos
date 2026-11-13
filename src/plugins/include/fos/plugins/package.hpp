// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/result.hpp"
#include "fos/formats/library.hpp"
#include "fos/plugins/manifest.hpp"

#include <filesystem>

/// A plugin package on disk: a directory with `plugin.json` and the data files it lists.
/// Everything here only reads files; no plugin content is ever executed.
namespace fos::plugins {

/// Resolves `relative` (already shape-checked with checkRelativePath) against the plugin
/// directory and verifies, following symlinks, that the result is a regular file that lies
/// INSIDE the canonical plugin directory. Rejects absolute paths, '..' components and
/// symlinks whose target is outside the directory. Returns the canonical path.
/// Errors: InvalidArgument (path shape / escape), NotFound (missing), IoError.
core::Result<std::filesystem::path> resolveInside(const std::filesystem::path& pluginDir, std::string_view relative);

/// Reads at most `maxBytes` bytes of `file` (formats::readFile): fails (ConstraintViolation)
/// when the file is larger — the size is checked before reading and the read itself is
/// bounded — and (IoError) when it is not a regular file (FIFO, device, directory).
core::Result<std::string> readBounded(const std::filesystem::path& file, std::uintmax_t maxBytes);

/// A package that passed manifest validation and the path/size checks.
struct PluginPackage {
    std::filesystem::path directory;                ///< Canonical plugin directory.
    PluginManifest manifest;
    std::vector<std::filesystem::path> libraryFiles; ///< Canonical paths, manifest order.
    std::uintmax_t totalBytes = 0;                   ///< Sum of the library file sizes.
};

/// Outcome of inspecting a package directory without loading its libraries.
struct PackageInspection {
    std::optional<PluginPackage> package; ///< Set when there is no error diagnostic.
    PluginManifest manifest;              ///< Best-effort manifest (for reporting) even on errors.
    core::Diagnostics diagnostics;        ///< Manifest diagnostics + path/size errors.
    bool ok() const noexcept { return package.has_value(); }
};

/// Validates `pluginDir`: it must be a directory, `plugin.json` must be a regular file
/// inside it within limits.maxManifestBytes, the manifest must be valid, and every
/// contributed library must resolve inside the directory, be at most limits.maxLibraryBytes
/// and together at most limits.maxTotalBytes. Sizes are checked before anything is read.
PackageInspection inspectPackage(const std::filesystem::path& pluginDir, const PluginLimits& limits = {});

/// Loads all contributed libraries of an inspected package and combines them into one
/// Library. Each file is read ONCE with readBounded(limits.maxLibraryBytes) and that buffer is
/// parsed with formats::importLibraryText (checksums verified); the file is never re-opened. Errors (all-or-nothing):
/// a file that fails to import, an entity id not of the form "<plugin-id>:<local-id>",
/// an id used twice within the plugin (same category), more than limits.maxEntries
/// entries. Container warnings are appended to `warnings` when given.
core::Result<formats::Library> loadPackageLibraries(const PluginPackage& package, const PluginLimits& limits = {},
                                                    core::Diagnostics* warnings = nullptr);

/// Empty when `entityId` is "<pluginId>:<local>" with a non-empty local part, else the reason.
std::string checkNamespacedId(std::string_view pluginId, std::string_view entityId);

} // namespace fos::plugins
