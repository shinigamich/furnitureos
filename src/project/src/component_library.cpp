// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/project/components.hpp"

#include <algorithm>
#include <set>
#include <tuple>

namespace fos::project {

namespace fs = std::filesystem;

namespace {

core::Diagnostic diag(core::Severity severity, std::string code, std::string message, std::string entity) {
    return core::Diagnostic{severity, std::move(code), std::move(message), std::move(entity)};
}

bool hasComponentExtension(const fs::path& p) { return p.extension().string() == formats::kComponentFileExtension; }

/// `.foscomp` files below `dir` (hidden names skipped), sorted by generic path.
std::vector<fs::path> listFiles(const fs::path& dir, core::Diagnostics& out) {
    std::vector<fs::path> files;
    std::error_code ec;
    fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (!name.empty() && name[0] == '.') {
            if (it->is_directory(ec)) it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file(ec) && hasComponentExtension(it->path())) files.push_back(it->path());
    }
    if (ec)
        out.push_back(diag(core::Severity::Warning, "project.components.scan_incomplete",
                           "The directory '" + dir.generic_string() + "' could not be fully listed: " + ec.message() + ".",
                           dir.generic_string()));
    std::sort(files.begin(), files.end(),
              [](const fs::path& a, const fs::path& b) { return a.generic_string() < b.generic_string(); });
    return files;
}

} // namespace

core::Diagnostics ComponentLibrary::scan(const std::vector<fs::path>& paths, const formats::LoadOptions& options) {
    core::Diagnostics out;
    for (const auto& path : paths) {
        std::error_code ec;
        std::vector<fs::path> files;
        if (fs::is_directory(path, ec)) {
            files = listFiles(path, out);
        } else if (fs::exists(path, ec)) {
            files.push_back(path);
        } else {
            out.push_back(diag(core::Severity::Warning, "project.components.missing_path",
                               "'" + path.generic_string() + "' does not exist; no components were read from it.",
                               path.generic_string()));
            continue;
        }
        for (const auto& file : files) {
            core::Diagnostics warnings;
            auto loaded = loadComponents(file, options, &warnings);
            for (auto& w : warnings) {
                w.entityId = file.generic_string();
                out.push_back(std::move(w));
            }
            if (!loaded) {
                out.push_back(diag(core::Severity::Error, "project.components.load_failed", loaded.error().message,
                                   file.generic_string()));
                continue;
            }
            for (auto& c : loaded.value()) {
                const std::string id = c.meta.id;
                if (auto r = add(std::move(c), file.generic_string()); !r)
                    out.push_back(diag(core::Severity::Warning, "project.components.duplicate_id",
                                       r.error().message + " The copy in '" + file.generic_string() + "' is ignored.", id));
            }
        }
    }
    return out;
}

core::Result<void> ComponentLibrary::add(Component component, std::string source) {
    for (const auto& e : entries_)
        if (e.component.meta.id == component.meta.id)
            return core::makeError(core::ErrorCode::AlreadyExists,
                                   "Component id \"" + component.meta.id + "\" is already indexed (from '" + e.source + "').",
                                   component.meta.id);
    entries_.push_back(ComponentEntry{std::move(source), std::move(component)});
    sort();
    return {};
}

void ComponentLibrary::sort() {
    std::stable_sort(entries_.begin(), entries_.end(), [](const ComponentEntry& a, const ComponentEntry& b) {
        const auto& x = a.component.meta;
        const auto& y = b.component.meta;
        return std::tie(x.category, x.name, x.id, a.source) < std::tie(y.category, y.name, y.id, b.source);
    });
}

std::vector<std::string> ComponentLibrary::categories() const {
    std::set<std::string> s;
    for (const auto& e : entries_)
        if (!e.component.meta.category.empty()) s.insert(e.component.meta.category);
    return {s.begin(), s.end()};
}

std::vector<std::string> ComponentLibrary::tags() const {
    std::set<std::string> s;
    for (const auto& e : entries_) s.insert(e.component.meta.tags.begin(), e.component.meta.tags.end());
    return {s.begin(), s.end()};
}

std::vector<const ComponentEntry*> ComponentLibrary::byCategory(std::string_view category) const {
    std::vector<const ComponentEntry*> out;
    for (const auto& e : entries_)
        if (e.component.meta.category == category) out.push_back(&e);
    return out;
}

std::vector<const ComponentEntry*> ComponentLibrary::byTag(std::string_view tag) const {
    std::vector<const ComponentEntry*> out;
    for (const auto& e : entries_) {
        const auto& t = e.component.meta.tags;
        if (std::find(t.begin(), t.end(), tag) != t.end()) out.push_back(&e);
    }
    return out;
}

const ComponentEntry* ComponentLibrary::find(std::string_view idOrName) const {
    for (const auto& e : entries_)
        if (e.component.meta.id == idOrName) return &e;
    for (const auto& e : entries_)
        if (e.component.meta.name == idOrName) return &e;
    return nullptr;
}

} // namespace fos::project
