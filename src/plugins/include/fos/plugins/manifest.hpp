// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/diagnostic.hpp"
#include "fos/plugins/semver.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// Plugin manifest (`plugin.json`) of a declarative data plugin (ADR-0007 phase 1).
/// Specified in docs/api/plugins.md. No plugin code is ever executed.
namespace fos::plugins {

inline constexpr std::string_view kManifestFileName = "plugin.json";
inline constexpr int kManifestVersion = 1;
/// Prefix separator of contributed entity ids: "<plugin-id>:<local-id>".
inline constexpr char kNamespaceSeparator = ':';

/// Resource limits applied before anything is read (all sizes in bytes).
struct PluginLimits {
    std::uintmax_t maxManifestBytes = 64 * 1024;             ///< plugin.json
    std::uintmax_t maxLibraryBytes = 16 * 1024 * 1024;       ///< each contributed .foslib
    std::uintmax_t maxTotalBytes = 64 * 1024 * 1024;         ///< all contributed files of one plugin
    std::size_t maxLibraries = 32;                           ///< entries of contributes.libraries
    std::size_t maxEntries = 20000;                          ///< materials + edge bands + hardware of one plugin
    int maxJsonDepth = 32;                                   ///< nesting depth of plugin.json
};

/// Kinds of contribution a manifest may list under "contributes". Only Libraries is
/// supported by this version; the others are accepted with a warning and ignored.
enum class ExtensionKind { Libraries, Templates, Reports, Postprocessors, Importers };
const char* toString(ExtensionKind kind) noexcept; ///< The manifest key, e.g. "libraries".
bool isSupported(ExtensionKind kind) noexcept;     ///< True only for Libraries.
std::optional<ExtensionKind> extensionKindFromKey(std::string_view key) noexcept;

/// Permissions a plugin may request. Phase 1 defines only "data" (read-only data files).
inline constexpr std::string_view kDataPermission = "data";

struct Compatibility {
    SemVer minAppVersion;
    std::optional<SemVer> maxAppVersion; ///< Inclusive upper bound; none = no upper bound.
};

/// A validated manifest. Fields are filled as far as they could be read; they are only
/// meaningful when ManifestValidation::ok().
struct PluginManifest {
    int manifestVersion = 0;
    std::string id;
    std::string name;
    SemVer version;
    std::string versionText; ///< As written in the file.
    std::string description;
    std::string author;
    std::string license; ///< SPDX identifier or simple AND/OR/WITH expression.
    std::optional<std::string> homepage;
    Compatibility compatibility;
    std::vector<std::string> permissions;
    /// contributes.libraries: relative paths (forward slashes) of `.foslib` files, file order.
    std::vector<std::string> libraries;
    /// Unsupported contribution kinds that were present (and ignored), in file order.
    std::vector<ExtensionKind> ignoredContributions;
    std::map<std::string, std::string> localizedNames; ///< Language tag -> name.
};

/// Result of manifest validation. Error diagnostics make the plugin not loadable;
/// warnings (unknown keys, unsupported contribution kinds) do not. Every message starts
/// with the JSON pointer of the offending value, e.g. "/compatibility/minAppVersion: ...".
struct ManifestValidation {
    PluginManifest manifest;
    core::Diagnostics diagnostics;
    bool ok() const noexcept { return !core::hasErrors(diagnostics); }
};

/// Plugin id rule: 3..128 characters, at least two dot-separated segments, each segment
/// matching [a-z][a-z0-9]*(-[a-z0-9]+)* (lower-case letters, digits, single inner hyphens).
/// Equivalent regex: ^[a-z][a-z0-9]*(-[a-z0-9]+)*(\.[a-z][a-z0-9]*(-[a-z0-9]+)*)+$
/// Returns an empty string when valid, otherwise the reason.
std::string checkPluginId(std::string_view id);

/// Checks a contributed relative path (shape only, nothing touches the file system):
/// non-empty, forward slashes only, not absolute, no drive letter or ':', no backslash,
/// no empty, "." or ".." component, no control characters. Empty string = valid.
std::string checkRelativePath(std::string_view path);

/// Parses and validates manifest text. `sourceName` prefixes nothing; it is used as the
/// Diagnostic::entityId when the id cannot be read. Never throws.
ManifestValidation parseManifest(std::string_view text, const PluginLimits& limits = {},
                                 std::string_view sourceName = "plugin.json");

/// True when `appVersion` lies within [minAppVersion, maxAppVersion] (inclusive, SemVer
/// precedence; so the pre-release 0.2.0-dev is *below* a minimum of 0.2.0).
bool isCompatible(const Compatibility& compatibility, const SemVer& appVersion) noexcept;

} // namespace fos::plugins
