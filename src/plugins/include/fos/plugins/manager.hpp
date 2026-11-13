// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/plugins/package.hpp"

#include <set>

/// Discovery, validation, enabling/disabling, loading, installing and removing of
/// declarative data plugins (ADR-0007 phase 1). Behaviour: docs/api/plugins.md.
namespace fos::plugins {

enum class PluginState {
    Invalid,      ///< Manifest, path or size errors (see diagnostics); never loaded.
    Incompatible, ///< Valid, but the app version is outside the plugin's compatibility range.
    Shadowed,     ///< Another valid, compatible copy with the same id wins (higher version / earlier dir).
    Disabled,     ///< Valid and compatible, but its id is in the disabled list.
    Enabled,      ///< Will be loaded by loadInto().
    Loaded,       ///< loadInto() merged its libraries.
    LoadFailed    ///< loadInto() failed for this plugin (others are unaffected).
};
const char* toString(PluginState state) noexcept;

/// One discovered plugin directory.
struct PluginRecord {
    std::filesystem::path directory; ///< As found in the search dir (not canonicalised).
    std::size_t searchDirIndex = 0;  ///< Index into PluginManagerOptions::searchDirs.
    PluginManifest manifest;         ///< Best-effort (id may be empty when unreadable).
    std::optional<PluginPackage> package; ///< Set for every state except Invalid.
    PluginState state = PluginState::Invalid;
    core::Diagnostics diagnostics;
};

struct PluginManagerOptions {
    SemVer appVersion;                           ///< Running application version.
    std::vector<std::filesystem::path> searchDirs; ///< Scanned in this order (earlier wins ties).
    /// Directory install()/remove() operate on (normally the per-user plugin dir; it should
    /// also be listed in searchDirs). Empty = install/remove refuse.
    std::filesystem::path installDir;
    PluginLimits limits;
};

/// Per-plugin outcome of loadInto().
struct PluginLoadResult {
    std::string id;
    std::string version;
    formats::MergeReport merge; ///< Entries merged into the target (empty on failure).
};

struct LoadReport {
    std::vector<PluginLoadResult> loaded; ///< Successfully loaded plugins, sorted by id.
    core::Diagnostics diagnostics;        ///< Failures (errors) and warnings, sorted by plugin id.
};

struct InstallOptions {
    /// Replace an installed copy whose version is NEWER than the incoming one, or whose
    /// manifest cannot be read. Equal or older installed versions are always replaced.
    bool overwriteNewer = false;
};

struct InstallReport {
    std::string id;
    std::string version;
    std::filesystem::path destination;           ///< <installDir>/<id>
    std::optional<std::string> replacedVersion;  ///< Version that was replaced, if any.
    std::vector<std::string> copiedFiles;        ///< Relative paths, sorted.
    core::Diagnostics warnings;                  ///< Manifest warnings of the installed plugin.
};

class PluginManager {
public:
    explicit PluginManager(PluginManagerOptions options);

    const PluginManagerOptions& options() const noexcept { return options_; }

    /// Disabled plugin ids (persisted by the application). Stored sorted and de-duplicated.
    void setDisabledIds(const std::vector<std::string>& ids);
    std::vector<std::string> disabledIds() const;
    void setEnabled(const std::string& id, bool enabled);

    /// Scans every search dir in order; within a dir, subdirectories in byte-wise name order
    /// (names starting with '.' and non-directories are skipped). Each subdirectory is
    /// inspected (inspectPackage); its name must equal the manifest id. Then compatibility,
    /// duplicate ids (highest version among valid compatible copies wins; equal version ->
    /// earlier search dir) and the disabled list are applied. Records are kept in scan
    /// order. Returns discovery-level diagnostics (missing search dirs: Info).
    core::Diagnostics discover();

    const std::vector<PluginRecord>& plugins() const noexcept { return records_; }
    /// The winning record (Enabled/Disabled/Loaded/LoadFailed) for an id, or null.
    const PluginRecord* find(std::string_view id) const;

    /// Loads every Enabled plugin, in id order, into `target` via formats::mergeLibrary with
    /// ConflictPolicy::Skip: an entry whose id already exists in `target` is kept unchanged
    /// (warning "plugins.load.skipped_existing"). Each plugin is all-or-nothing and a failing
    /// plugin (LoadFailed) never affects the others. Packages are re-inspected before reading.
    LoadReport loadInto(formats::Library& target);

    /// Installs the package at `sourceDir` into <installDir>/<id> after full validation
    /// (manifest, paths, sizes, app compatibility, loading every library and the namespace
    /// rule). Copies only plugin.json, the contributed libraries and top-level LICENSE*/
    /// LICENCE*/COPYING*/README*/NOTICE* regular files, through a hidden staging directory
    /// that is re-validated before it replaces the destination. Refuses to replace a newer
    /// installed version unless options.overwriteNewer. Call discover() afterwards.
    core::Result<InstallReport> install(const std::filesystem::path& sourceDir, const InstallOptions& options = {});

    /// Removes <installDir>/<id>. Verifies first: the id is syntactically valid, the path is
    /// a real directory (not a symlink) directly inside the canonical installDir, and it
    /// contains a plugin.json whose id (if readable) equals `id`. Never deletes anything
    /// outside installDir. Call discover() afterwards.
    core::Result<void> remove(const std::string& id);

private:
    PluginManagerOptions options_;
    std::set<std::string> disabled_;
    std::vector<PluginRecord> records_;
};

} // namespace fos::plugins
