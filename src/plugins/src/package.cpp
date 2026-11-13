// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/plugins/package.hpp"

#include <fstream>
#include <set>

namespace fos::plugins {
namespace fs = std::filesystem;
using core::ErrorCode;
using core::makeError;

namespace {

/// True when `inner` is strictly below `outer` (both canonical), compared component-wise.
bool isStrictlyInside(const fs::path& outer, const fs::path& inner) {
    auto o = outer.begin();
    auto i = inner.begin();
    for (; o != outer.end(); ++o, ++i) {
        if (i == inner.end() || *o != *i) return false;
    }
    return i != inner.end();
}

core::Diagnostic error(const std::string& entity, const std::string& code, const std::string& message) {
    return core::Diagnostic{core::Severity::Error, code, message, entity};
}

} // namespace

std::string checkNamespacedId(std::string_view pluginId, std::string_view entityId) {
    const std::string prefix = std::string(pluginId) + kNamespaceSeparator;
    if (entityId.size() <= prefix.size() || entityId.compare(0, prefix.size(), prefix) != 0)
        return "id \"" + std::string(entityId) + "\" must start with \"" + prefix +
               "\" followed by a local id, so that plugins cannot overwrite user or other plugins' entries";
    return {};
}

core::Result<fs::path> resolveInside(const fs::path& pluginDir, std::string_view relative) {
    if (auto why = checkRelativePath(relative); !why.empty())
        return makeError(ErrorCode::InvalidArgument, "Path \"" + std::string(relative) + "\" is not allowed: " + why + ".",
                         std::string(relative));
    std::error_code ec;
    const fs::path root = fs::canonical(pluginDir, ec);
    if (ec)
        return makeError(ErrorCode::IoError, "Cannot resolve plugin directory '" + pluginDir.string() + "': " + ec.message() + ".");
    const fs::path candidate = root / fs::path(std::string(relative));
    const auto lst = fs::symlink_status(candidate, ec);
    if (ec || !fs::exists(lst))
        return makeError(ErrorCode::NotFound,
                         "File \"" + std::string(relative) + "\" does not exist in plugin directory '" + root.string() + "'.",
                         std::string(relative));
    const fs::path resolved = fs::canonical(candidate, ec);
    if (ec)
        return makeError(ErrorCode::NotFound,
                         "File \"" + std::string(relative) + "\" in '" + root.string() + "' cannot be resolved (" +
                             ec.message() + "; a dangling symlink?).",
                         std::string(relative));
    if (!isStrictlyInside(root, resolved))
        return makeError(ErrorCode::InvalidArgument,
                         "File \"" + std::string(relative) + "\" resolves to '" + resolved.string() +
                             "', which is outside the plugin directory '" + root.string() +
                             "' (symlink escape); it was not read.",
                         std::string(relative));
    if (!fs::is_regular_file(resolved, ec))
        return makeError(ErrorCode::InvalidArgument,
                         "\"" + std::string(relative) + "\" in '" + root.string() + "' is not a regular file.",
                         std::string(relative));
    return resolved;
}

core::Result<std::string> readBounded(const fs::path& file, std::uintmax_t maxBytes) {
    std::error_code ec;
    const auto size = fs::file_size(file, ec);
    if (ec) return makeError(ErrorCode::IoError, "Cannot determine the size of '" + file.string() + "': " + ec.message() + ".");
    if (size > maxBytes)
        return makeError(ErrorCode::ConstraintViolation,
                         "'" + file.string() + "' is " + std::to_string(size) + " bytes; the limit is " +
                             std::to_string(maxBytes) + " bytes. It was not read.");
    std::ifstream in(file, std::ios::binary);
    if (!in) return makeError(ErrorCode::IoError, "Cannot open '" + file.string() + "' for reading.");
    std::string bytes;
    bytes.resize(static_cast<std::size_t>(maxBytes) + 1);
    in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    const auto got = static_cast<std::uintmax_t>(in.gcount());
    if (got > maxBytes) // grew after the size check
        return makeError(ErrorCode::ConstraintViolation,
                         "'" + file.string() + "' is larger than the limit of " + std::to_string(maxBytes) + " bytes.");
    if (in.bad()) return makeError(ErrorCode::IoError, "Error while reading '" + file.string() + "'.");
    bytes.resize(static_cast<std::size_t>(got));
    return bytes;
}

PackageInspection inspectPackage(const fs::path& pluginDir, const PluginLimits& limits) {
    PackageInspection out;
    const std::string dirName = pluginDir.filename().string();
    std::error_code ec;
    if (!fs::is_directory(pluginDir, ec)) {
        out.diagnostics.push_back(error(dirName, "plugins.package.not_a_directory",
                                        "Plugin path '" + pluginDir.string() + "' is not a directory."));
        return out;
    }
    auto manifestPath = resolveInside(pluginDir, kManifestFileName);
    if (!manifestPath.ok()) {
        out.diagnostics.push_back(error(dirName,
                                        manifestPath.error().code == ErrorCode::NotFound ? "plugins.package.no_manifest"
                                                                                          : "plugins.package.bad_manifest_path",
                                        "Plugin directory '" + pluginDir.string() + "': " + manifestPath.error().message));
        return out;
    }
    auto text = readBounded(manifestPath.value(), limits.maxManifestBytes);
    if (!text.ok()) {
        out.diagnostics.push_back(error(dirName,
                                        text.error().code == ErrorCode::ConstraintViolation ? "plugins.manifest.too_large"
                                                                                             : "plugins.package.io_error",
                                        text.error().message));
        return out;
    }
    auto validation = parseManifest(text.value(), limits, dirName);
    const bool manifestOk = validation.ok();
    out.manifest = validation.manifest;
    out.diagnostics = std::move(validation.diagnostics);
    if (!manifestOk) return out;

    const std::string& id = out.manifest.id;
    PluginPackage pkg;
    pkg.directory = fs::canonical(pluginDir, ec);
    pkg.manifest = out.manifest;
    bool failed = false;
    for (std::size_t i = 0; i < out.manifest.libraries.size(); ++i) {
        const auto& rel = out.manifest.libraries[i];
        const std::string ptr = "/contributes/libraries/" + std::to_string(i);
        auto resolved = resolveInside(pkg.directory, rel);
        if (!resolved.ok()) {
            out.diagnostics.push_back(error(id, "plugins.package.bad_path", ptr + ": " + resolved.error().message));
            failed = true;
            continue;
        }
        const auto size = fs::file_size(resolved.value(), ec);
        if (ec) {
            out.diagnostics.push_back(error(id, "plugins.package.io_error", ptr + ": cannot read the size of \"" + rel + "\": " + ec.message() + "."));
            failed = true;
            continue;
        }
        if (size > limits.maxLibraryBytes) {
            out.diagnostics.push_back(error(id, "plugins.package.file_too_large",
                                            ptr + ": \"" + rel + "\" is " + std::to_string(size) +
                                                " bytes; the per-file limit is " + std::to_string(limits.maxLibraryBytes) + " bytes."));
            failed = true;
            continue;
        }
        pkg.totalBytes += size;
        pkg.libraryFiles.push_back(resolved.value());
    }
    if (!failed && pkg.totalBytes > limits.maxTotalBytes) {
        out.diagnostics.push_back(error(id, "plugins.package.too_large",
                                        "The contributed files of plugin '" + id + "' total " + std::to_string(pkg.totalBytes) +
                                            " bytes; the limit is " + std::to_string(limits.maxTotalBytes) + " bytes."));
        failed = true;
    }
    if (!failed) out.package = std::move(pkg);
    return out;
}

core::Result<formats::Library> loadPackageLibraries(const PluginPackage& package, const PluginLimits& limits,
                                                    core::Diagnostics* warnings) {
    const std::string& id = package.manifest.id;
    formats::Library combined;
    std::set<std::string> seen[3];
    std::size_t entries = 0;
    for (std::size_t f = 0; f < package.libraryFiles.size(); ++f) {
        const auto& file = package.libraryFiles[f];
        const std::string& rel = package.manifest.libraries[f];
        // Re-check the size right before reading (the directory may have changed).
        std::error_code ec;
        const auto size = fs::file_size(file, ec);
        if (ec || size > limits.maxLibraryBytes)
            return makeError(ErrorCode::ConstraintViolation,
                             "Plugin '" + id + "': library \"" + rel + "\" is missing or exceeds the per-file limit of " +
                                 std::to_string(limits.maxLibraryBytes) + " bytes.",
                             id);
        core::Diagnostics fileWarnings;
        auto lib = formats::importLibrary(file, {}, &fileWarnings);
        if (!lib.ok())
            return makeError(lib.error().code, "Plugin '" + id + "': library \"" + rel + "\" cannot be loaded: " + lib.error().message, id);
        if (warnings)
            for (auto& w : fileWarnings) {
                w.entityId = id;
                warnings->push_back(std::move(w));
            }
        auto check = [&](int category, const std::string& entityId, const char* list, std::size_t index) -> core::Result<void> {
            const std::string where = "Plugin '" + id + "', library \"" + rel + "\", /payload/" + list + "/" + std::to_string(index) + "/id: ";
            if (auto why = checkNamespacedId(id, entityId); !why.empty())
                return makeError(ErrorCode::ConstraintViolation, where + why + ".", id);
            if (!seen[category].insert(entityId).second)
                return makeError(ErrorCode::ConstraintViolation, where + "id \"" + entityId + "\" is already used by another library of this plugin.", id);
            return {};
        };
        const auto& l = lib.value();
        for (std::size_t i = 0; i < l.materials.size(); ++i)
            if (auto r = check(0, l.materials[i].id.value, "materials", i); !r.ok()) return r.error();
        for (std::size_t i = 0; i < l.edgeBands.size(); ++i)
            if (auto r = check(1, l.edgeBands[i].id.value, "edgeBands", i); !r.ok()) return r.error();
        for (std::size_t i = 0; i < l.hardware.size(); ++i)
            if (auto r = check(2, l.hardware[i].id.value, "hardware", i); !r.ok()) return r.error();
        entries += l.materials.size() + l.edgeBands.size() + l.hardware.size();
        if (entries > limits.maxEntries)
            return makeError(ErrorCode::ConstraintViolation,
                             "Plugin '" + id + "' contributes more than " + std::to_string(limits.maxEntries) + " library entries.", id);
        combined.materials.insert(combined.materials.end(), l.materials.begin(), l.materials.end());
        combined.edgeBands.insert(combined.edgeBands.end(), l.edgeBands.begin(), l.edgeBands.end());
        combined.hardware.insert(combined.hardware.end(), l.hardware.begin(), l.hardware.end());
    }
    return combined;
}

} // namespace fos::plugins
