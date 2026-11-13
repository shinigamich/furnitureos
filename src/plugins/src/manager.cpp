// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/plugins/manager.hpp"

#include <algorithm>
#include <map>

namespace fos::plugins {
namespace fs = std::filesystem;

const char* toString(PluginState state) noexcept {
    switch (state) {
    case PluginState::Invalid: return "invalid";
    case PluginState::Incompatible: return "incompatible";
    case PluginState::Shadowed: return "shadowed";
    case PluginState::Disabled: return "disabled";
    case PluginState::Enabled: return "enabled";
    case PluginState::Loaded: return "loaded";
    case PluginState::LoadFailed: return "load-failed";
    }
    return "unknown";
}

namespace {

core::Diagnostic diag(core::Severity s, std::string code, std::string message, std::string entity) {
    return core::Diagnostic{s, std::move(code), std::move(message), std::move(entity)};
}

std::string rangeText(const Compatibility& c) {
    return ">= " + c.minAppVersion.toString() + (c.maxAppVersion ? " and <= " + c.maxAppVersion->toString() : "");
}

} // namespace

PluginManager::PluginManager(PluginManagerOptions options) : options_(std::move(options)) {}

void PluginManager::setDisabledIds(const std::vector<std::string>& ids) {
    disabled_ = std::set<std::string>(ids.begin(), ids.end());
    for (auto& r : records_) {
        if (r.state == PluginState::Enabled && disabled_.count(r.manifest.id)) r.state = PluginState::Disabled;
        else if (r.state == PluginState::Disabled && !disabled_.count(r.manifest.id)) r.state = PluginState::Enabled;
    }
}

std::vector<std::string> PluginManager::disabledIds() const { return {disabled_.begin(), disabled_.end()}; }

void PluginManager::setEnabled(const std::string& id, bool enabled) {
    auto ids = disabledIds();
    if (enabled) ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
    else ids.push_back(id);
    setDisabledIds(ids);
}

const PluginRecord* PluginManager::find(std::string_view id) const {
    for (const auto& r : records_)
        if (r.manifest.id == id && r.state != PluginState::Invalid && r.state != PluginState::Incompatible &&
            r.state != PluginState::Shadowed)
            return &r;
    return nullptr;
}

core::Diagnostics PluginManager::discover() {
    records_.clear();
    core::Diagnostics out;
    for (std::size_t d = 0; d < options_.searchDirs.size(); ++d) {
        const fs::path& dir = options_.searchDirs[d];
        std::error_code ec;
        if (!fs::exists(dir, ec)) {
            out.push_back(diag(core::Severity::Info, "plugins.discovery.search_dir_missing",
                               "Plugin search directory '" + dir.string() + "' does not exist; it was skipped.", {}));
            continue;
        }
        if (!fs::is_directory(dir, ec)) {
            out.push_back(diag(core::Severity::Warning, "plugins.discovery.search_dir_not_directory",
                               "Plugin search path '" + dir.string() + "' is not a directory; it was skipped.", {}));
            continue;
        }
        std::vector<fs::path> candidates;
        fs::directory_iterator it(dir, ec), end;
        if (ec) {
            out.push_back(diag(core::Severity::Warning, "plugins.discovery.unreadable",
                               "Plugin search directory '" + dir.string() + "' cannot be read: " + ec.message() + ".", {}));
            continue;
        }
        for (; it != end; it.increment(ec)) {
            if (ec) break;
            const auto name = it->path().filename().string();
            if (name.empty() || name[0] == '.') continue;
            std::error_code e2;
            if (!fs::is_directory(it->path(), e2)) continue;
            candidates.push_back(it->path());
        }
        // Deterministic: byte-wise order of the directory names, independent of the file system.
        std::sort(candidates.begin(), candidates.end(),
                  [](const fs::path& a, const fs::path& b) { return a.filename().string() < b.filename().string(); });
        for (const auto& c : candidates) {
            PluginRecord rec;
            rec.directory = c;
            rec.searchDirIndex = d;
            auto inspection = inspectPackage(c, options_.limits);
            rec.manifest = inspection.manifest;
            rec.diagnostics = std::move(inspection.diagnostics);
            if (inspection.ok()) {
                const auto dirName = c.filename().string();
                if (dirName != rec.manifest.id) {
                    rec.diagnostics.push_back(diag(core::Severity::Error, "plugins.discovery.directory_name",
                                                   "Plugin directory '" + c.string() + "' contains plugin '" + rec.manifest.id +
                                                       "'; the directory must be named after the plugin id.",
                                                   rec.manifest.id));
                } else if (!isCompatible(rec.manifest.compatibility, options_.appVersion)) {
                    rec.package = std::move(inspection.package);
                    rec.state = PluginState::Incompatible;
                    rec.diagnostics.push_back(diag(core::Severity::Warning, "plugins.discovery.incompatible",
                                                   "Plugin '" + rec.manifest.id + "' " + rec.manifest.version.toString() +
                                                       " requires FurnitureOS " + rangeText(rec.manifest.compatibility) +
                                                       "; this is " + options_.appVersion.toString() + ". It was not loaded.",
                                                   rec.manifest.id));
                } else {
                    rec.package = std::move(inspection.package);
                    rec.state = PluginState::Enabled;
                }
            }
            records_.push_back(std::move(rec));
        }
    }
    // Duplicate ids among valid, compatible records: highest version wins, ties -> scan order.
    std::map<std::string, std::size_t> winner;
    for (std::size_t i = 0; i < records_.size(); ++i) {
        if (records_[i].state != PluginState::Enabled) continue;
        auto [pos, inserted] = winner.emplace(records_[i].manifest.id, i);
        if (!inserted && records_[i].manifest.version > records_[pos->second].manifest.version) pos->second = i;
    }
    for (std::size_t i = 0; i < records_.size(); ++i) {
        auto& r = records_[i];
        if (r.state != PluginState::Enabled) continue;
        const auto w = winner.at(r.manifest.id);
        if (w != i) {
            const auto& win = records_[w];
            r.state = PluginState::Shadowed;
            r.diagnostics.push_back(diag(core::Severity::Warning, "plugins.discovery.shadowed",
                                         "Plugin '" + r.manifest.id + "' " + r.manifest.version.toString() + " in '" +
                                             r.directory.string() + "' is shadowed by version " +
                                             win.manifest.version.toString() + " in '" + win.directory.string() + "'.",
                                         r.manifest.id));
        } else if (disabled_.count(r.manifest.id)) {
            r.state = PluginState::Disabled;
        }
    }
    return out;
}

LoadReport PluginManager::loadInto(formats::Library& target) {
    LoadReport report;
    std::vector<PluginRecord*> enabled;
    for (auto& r : records_)
        if (r.state == PluginState::Enabled) enabled.push_back(&r);
    std::sort(enabled.begin(), enabled.end(),
              [](const PluginRecord* a, const PluginRecord* b) { return a->manifest.id < b->manifest.id; });
    for (auto* r : enabled) {
        const std::string& id = r->manifest.id;
        auto failWith = [&](const std::string& code, const std::string& message) {
            r->state = PluginState::LoadFailed;
            auto d = diag(core::Severity::Error, code, message, id);
            r->diagnostics.push_back(d);
            report.diagnostics.push_back(std::move(d));
        };
        // Re-inspect: the directory may have changed since discover().
        auto inspection = inspectPackage(r->directory, options_.limits);
        if (!inspection.ok()) {
            std::string first = "unknown error";
            for (const auto& d : inspection.diagnostics)
                if (d.severity == core::Severity::Error) {
                    first = d.message;
                    break;
                }
            failWith("plugins.load.invalid", "Plugin '" + id + "' is no longer valid: " + first);
            continue;
        }
        if (inspection.package->manifest.id != id ||
            !samePrecedence(inspection.package->manifest.version, r->manifest.version)) {
            failWith("plugins.load.changed", "Plugin '" + id + "' in '" + r->directory.string() +
                                                 "' changed since discovery; run discovery again.");
            continue;
        }
        core::Diagnostics warnings;
        auto lib = loadPackageLibraries(*inspection.package, options_.limits, &warnings);
        if (!lib.ok()) {
            failWith("plugins.load.library", lib.error().message);
            continue;
        }
        auto merged = formats::mergeLibrary(target, lib.value(), formats::ConflictPolicy::Skip);
        if (!merged.ok()) {
            failWith("plugins.load.merge", "Plugin '" + id + "' could not be merged: " + merged.error().message);
            continue;
        }
        for (const auto& e : merged.value().entries)
            if (e.action == formats::MergeAction::Skipped)
                warnings.push_back(diag(core::Severity::Warning, "plugins.load.skipped_existing",
                                        std::string("Plugin '") + id + "': " + formats::toString(e.category) + " \"" +
                                            e.incomingId + "\" already exists in the library; the existing entry was kept.",
                                        id));
        for (auto& w : warnings) {
            r->diagnostics.push_back(w);
            report.diagnostics.push_back(std::move(w));
        }
        r->state = PluginState::Loaded;
        report.loaded.push_back(PluginLoadResult{id, r->manifest.version.toString(), std::move(merged.value())});
    }
    return report;
}

} // namespace fos::plugins
