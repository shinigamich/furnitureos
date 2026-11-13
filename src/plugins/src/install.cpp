// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/plugins/manager.hpp"

#include <algorithm>
#include <random>

namespace fos::plugins {
namespace fs = std::filesystem;
using core::ErrorCode;
using core::makeError;

namespace {

std::string randomSuffix() {
    std::random_device device;
    return std::to_string(device()) + "-" + std::to_string(device());
}

std::string upper(std::string s) {
    for (auto& c : s)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return s;
}

/// Top-level documentation/licence files copied on install.
bool isDocumentationFile(const std::string& name) {
    const auto u = upper(name);
    for (const char* prefix : {"LICENSE", "LICENCE", "COPYING", "README", "NOTICE"})
        if (u.rfind(prefix, 0) == 0) return true;
    return false;
}

std::string errorsOf(const core::Diagnostics& diagnostics) {
    std::string s;
    for (const auto& d : diagnostics)
        if (d.severity == core::Severity::Error) s += (s.empty() ? "" : " ") + d.message;
    return s;
}

/// Removes a directory tree on scope exit unless released.
class TreeGuard {
public:
    explicit TreeGuard(fs::path p) : path_(std::move(p)) {}
    ~TreeGuard() {
        if (!path_.empty()) {
            std::error_code ec;
            fs::remove_all(path_, ec);
        }
    }
    void release() { path_.clear(); }
    TreeGuard(const TreeGuard&) = delete;
    TreeGuard& operator=(const TreeGuard&) = delete;

private:
    fs::path path_;
};

/// Full validation shared by the source and the staged copy.
core::Result<PluginPackage> validateForInstall(const fs::path& dir, const PluginLimits& limits, const SemVer& app,
                                               core::Diagnostics* warnings) {
    auto inspection = inspectPackage(dir, limits);
    if (!inspection.ok())
        return makeError(ErrorCode::InvalidArgument,
                         "Plugin package '" + dir.string() + "' is not valid and was not installed: " +
                             errorsOf(inspection.diagnostics),
                         inspection.manifest.id);
    const auto& m = inspection.package->manifest;
    if (!isCompatible(m.compatibility, app))
        return makeError(ErrorCode::Unsupported,
                         "Plugin '" + m.id + "' " + m.version.toString() + " requires FurnitureOS >= " +
                             m.compatibility.minAppVersion.toString() +
                             (m.compatibility.maxAppVersion ? " and <= " + m.compatibility.maxAppVersion->toString() : "") +
                             "; this is " + app.toString() + ". It was not installed.",
                         m.id);
    auto lib = loadPackageLibraries(*inspection.package, limits);
    if (!lib.ok())
        return makeError(lib.error().code, lib.error().message + " The plugin was not installed.", m.id);
    if (warnings)
        for (const auto& d : inspection.diagnostics)
            if (d.severity != core::Severity::Error) warnings->push_back(d);
    return std::move(*inspection.package);
}

} // namespace

core::Result<InstallReport> PluginManager::install(const fs::path& sourceDir, const InstallOptions& options) {
    if (options_.installDir.empty())
        return makeError(ErrorCode::InvalidArgument, "No plugin install directory is configured; nothing was installed.");
    InstallReport report;
    auto validated = validateForInstall(sourceDir, options_.limits, options_.appVersion, &report.warnings);
    if (!validated.ok()) return validated.error();
    const PluginPackage& pkg = validated.value();
    const std::string& id = pkg.manifest.id;
    report.id = id;
    report.version = pkg.manifest.version.toString();

    std::error_code ec;
    fs::create_directories(options_.installDir, ec);
    const fs::path root = fs::canonical(options_.installDir, ec);
    if (ec)
        return makeError(ErrorCode::IoError, "Plugin install directory '" + options_.installDir.string() +
                                                 "' cannot be created or resolved: " + ec.message() + ".");
    const fs::path dest = root / id;
    report.destination = dest;
    if (pkg.directory == dest || fs::equivalent(pkg.directory, dest, ec))
        return makeError(ErrorCode::AlreadyExists, "Plugin '" + id + "' is already installed at '" + dest.string() + "'.", id);

    // Existing copy: never replace a newer (or unreadable) one unless asked.
    const auto destStatus = fs::symlink_status(dest, ec);
    const bool destExists = fs::exists(destStatus);
    if (destExists) {
        if (fs::is_symlink(destStatus) || !fs::is_directory(destStatus))
            return makeError(ErrorCode::AlreadyExists,
                             "'" + dest.string() + "' exists but is not a plain plugin directory; it was left untouched.", id);
        std::optional<SemVer> existing;
        if (auto text = readBounded(dest / kManifestFileName, options_.limits.maxManifestBytes); text.ok()) {
            auto v = parseManifest(text.value(), options_.limits);
            if (v.ok() && v.manifest.id == id) existing = v.manifest.version;
        }
        if (!existing && !options.overwriteNewer)
            return makeError(ErrorCode::AlreadyExists,
                             "An installed copy of '" + id + "' at '" + dest.string() +
                                 "' has no readable manifest; it was not replaced (set overwriteNewer to replace it).",
                             id);
        if (existing && *existing > pkg.manifest.version && !options.overwriteNewer)
            return makeError(ErrorCode::AlreadyExists,
                             "Plugin '" + id + "' " + existing->toString() + " is installed, which is newer than " +
                                 pkg.manifest.version.toString() + "; it was not replaced (set overwriteNewer to downgrade).",
                             id);
        if (existing) report.replacedVersion = existing->toString();
        else report.replacedVersion = std::string("unreadable");
    }

    // Copy the validated files into a hidden staging directory (discovery skips '.' names).
    const fs::path staging = root / (".staging-" + id + "-" + randomSuffix());
    if (!fs::create_directory(staging, ec))
        return makeError(ErrorCode::IoError, "Cannot create staging directory '" + staging.string() + "': " + ec.message() + ".", id);
    TreeGuard stagingGuard(staging);
    auto copyOne = [&](const fs::path& from, const std::string& rel) -> core::Result<void> {
        const fs::path to = staging / fs::path(rel);
        std::error_code e;
        fs::create_directories(to.parent_path(), e);
        if (!e) fs::copy_file(from, to, fs::copy_options::overwrite_existing, e);
        if (e) return makeError(ErrorCode::IoError, "Cannot copy '" + from.string() + "' to '" + to.string() + "': " + e.message() + ".", id);
        report.copiedFiles.push_back(rel);
        return {};
    };
    if (auto r = copyOne(pkg.directory / kManifestFileName, std::string(kManifestFileName)); !r.ok()) return r.error();
    for (std::size_t i = 0; i < pkg.libraryFiles.size(); ++i)
        if (auto r = copyOne(pkg.libraryFiles[i], pkg.manifest.libraries[i]); !r.ok()) return r.error();
    for (fs::directory_iterator it(pkg.directory, ec), end; !ec && it != end; it.increment(ec)) {
        const auto name = it->path().filename().string();
        std::error_code e;
        const auto st = it->symlink_status(e);
        if (e || !fs::is_regular_file(st) || !isDocumentationFile(name)) continue;
        if (fs::file_size(it->path(), e) > options_.limits.maxManifestBytes * 4 || e) continue;
        if (auto r = copyOne(it->path(), name); !r.ok()) return r.error();
    }
    std::sort(report.copiedFiles.begin(), report.copiedFiles.end());

    // Re-validate what was actually written.
    auto staged = validateForInstall(staging, options_.limits, options_.appVersion, nullptr);
    if (!staged.ok())
        return makeError(ErrorCode::CorruptData, "The staged copy of '" + id + "' failed validation: " + staged.error().message, id);
    if (staged.value().manifest.id != id || !samePrecedence(staged.value().manifest.version, pkg.manifest.version))
        return makeError(ErrorCode::CorruptData, "The source of '" + id + "' changed while it was being installed.", id);

    // Swap into place; restore the previous copy on failure.
    fs::path old;
    if (destExists) {
        old = root / (".old-" + id + "-" + randomSuffix());
        fs::rename(dest, old, ec);
        if (ec)
            return makeError(ErrorCode::IoError, "Cannot move the installed copy of '" + id + "' aside: " + ec.message() + ".", id);
    }
    fs::rename(staging, dest, ec);
    if (ec) {
        std::error_code e;
        if (!old.empty()) fs::rename(old, dest, e);
        return makeError(ErrorCode::IoError, "Cannot move the new copy of '" + id + "' into place: " + ec.message() +
                                                 ". The previous installation was kept.", id);
    }
    stagingGuard.release();
    if (!old.empty()) fs::remove_all(old, ec);
    return report;
}

core::Result<void> PluginManager::remove(const std::string& id) {
    if (options_.installDir.empty())
        return makeError(ErrorCode::InvalidArgument, "No plugin install directory is configured; nothing was removed.");
    if (auto why = checkPluginId(id); !why.empty())
        return makeError(ErrorCode::InvalidArgument, "\"" + id + "\" is not a valid plugin id (" + why + "); nothing was removed.", id);
    std::error_code ec;
    const fs::path root = fs::canonical(options_.installDir, ec);
    if (ec) return makeError(ErrorCode::NotFound, "Plugin install directory '" + options_.installDir.string() + "' does not exist.", id);
    const fs::path dir = root / id;
    const auto st = fs::symlink_status(dir, ec);
    if (!fs::exists(st))
        return makeError(ErrorCode::NotFound, "Plugin '" + id + "' is not installed in '" + root.string() + "'.", id);
    if (fs::is_symlink(st) || !fs::is_directory(st))
        return makeError(ErrorCode::InvalidArgument,
                         "'" + dir.string() + "' is not a plain directory (symlink or file); it was not removed.", id);
    const fs::path resolved = fs::canonical(dir, ec);
    if (ec || resolved.parent_path() != root)
        return makeError(ErrorCode::InvalidArgument, "'" + dir.string() + "' is not directly inside the plugin directory '" +
                                                         root.string() + "'; it was not removed.", id);
    const auto manifestStatus = fs::symlink_status(resolved / kManifestFileName, ec);
    if (!fs::is_regular_file(manifestStatus))
        return makeError(ErrorCode::InvalidArgument,
                         "'" + resolved.string() + "' has no plugin.json, so it does not look like a plugin; it was not removed.", id);
    if (auto text = readBounded(resolved / kManifestFileName, options_.limits.maxManifestBytes); text.ok()) {
        const auto v = parseManifest(text.value(), options_.limits);
        if (!v.manifest.id.empty() && v.manifest.id != id)
            return makeError(ErrorCode::InvalidArgument, "'" + resolved.string() + "' contains plugin '" + v.manifest.id +
                                                             "', not '" + id + "'; it was not removed.", id);
    }
    fs::remove_all(resolved, ec); // does not follow symlinks inside the tree
    if (ec) return makeError(ErrorCode::IoError, "Removing '" + resolved.string() + "' failed: " + ec.message() + ".", id);
    return {};
}

} // namespace fos::plugins
