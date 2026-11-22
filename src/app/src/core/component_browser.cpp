// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/component_browser.hpp"

#include "fos/core/units.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

#include <algorithm>
#include <system_error>

namespace fos::app {
namespace fs = std::filesystem;

namespace {

QString tr(const char* text) { return QCoreApplication::translate("fos::app::ComponentBrowser", text); }
QString q(const std::string& s) { return QString::fromStdString(s); }

bool matches(const project::Component& c, const QString& search) {
    if (search.isEmpty()) return true;
    auto has = [&](const std::string& s) { return q(s).contains(search, Qt::CaseInsensitive); };
    if (has(c.meta.name) || has(c.meta.id) || has(c.meta.description) || has(c.meta.category)) return true;
    return std::any_of(c.meta.tags.begin(), c.meta.tags.end(), has);
}

QString sizeText(double v) { return v > 0 ? q(core::formatLength(v)) : QStringLiteral("?"); }

template <class Entry>
QStringList entryNames(const std::vector<Entry>& entries) {
    QStringList out;
    for (const auto& e : entries) out << QStringLiteral("%1 (%2)").arg(q(e.name), q(e.id.str()));
    return out;
}

} // namespace

fs::path defaultComponentFolder(bool create) {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty()) return {};
    const fs::path folder = fs::path(QDir(base).filePath(QStringLiteral("components")).toStdString());
    if (create) {
        std::error_code ec;
        fs::create_directories(folder, ec); // a failure shows up as a missing-path scan warning
    }
    return folder;
}

core::Diagnostics ComponentBrowserModel::rescan(const std::vector<fs::path>& folders) {
    folders_ = folders;
    library_.clear();
    return library_.scan(folders_);
}

std::vector<ComponentCategoryRow> ComponentBrowserModel::rows(const std::string& tag, const QString& search) const {
    std::vector<ComponentCategoryRow> out;
    const QString needle = search.trimmed();
    // entries() is sorted by category, so equal categories are adjacent.
    for (const auto& entry : library_.entries()) {
        const auto& meta = entry.component.meta;
        if (!tag.empty() && std::find(meta.tags.begin(), meta.tags.end(), tag) == meta.tags.end()) continue;
        if (!matches(entry.component, needle)) continue;
        if (out.empty() || out.back().category != meta.category) out.push_back({meta.category, {}});
        out.back().entries.push_back(&entry);
    }
    // Uncategorised ("" sorts first byte-wise) goes last.
    if (!out.empty() && out.front().category.empty()) std::rotate(out.begin(), out.begin() + 1, out.end());
    return out;
}

ComponentDetails componentDetails(const project::ComponentEntry& entry) {
    const auto& c = entry.component;
    ComponentDetails d;
    d.name = q(c.meta.name);
    d.description = q(c.meta.description);
    d.author = q(c.meta.author);
    d.category = q(c.meta.category);
    d.source = q(entry.source);
    for (const auto& t : c.meta.tags) d.tags << q(t);
    const auto& s = c.meta.previewSize;
    d.size = tr("%1 × %2 × %3 mm").arg(sizeText(s.width), sizeText(s.height), sizeText(s.depth));
    d.materials = entryNames(c.library.materials);
    d.edgeBands = entryNames(c.library.edgeBands);
    d.hardware = entryNames(c.library.hardware);
    return d;
}

core::Result<QStringList> componentConflicts(const project::Project& project, const project::Component& component) {
    auto plan = project::planComponentInsert(project, component, formats::ConflictPolicy::RenameWithNewId,
                                             project::componentIdGenerator("probe"));
    if (!plan) return plan.error();
    QStringList out;
    for (const auto& e : plan.value().entries) {
        if (e.action != formats::MergeAction::Renamed) continue;
        const QString kind = e.category == formats::LibraryCategory::Material   ? tr("material")
                             : e.category == formats::LibraryCategory::EdgeBand ? tr("edge band")
                                                                                 : tr("hardware");
        out << QStringLiteral("%1: %2").arg(kind, q(e.incomingId));
    }
    return out;
}

core::Result<std::string> insertComponentIntoSession(Session& session, const project::Component& component,
                                                     formats::ConflictPolicy policy) {
    const std::string id = nextItemId(session.project(), component.item.isCabinet() ? "cab" : "panel");
    const auto placement = geometry::Transform::translation({session.rightEdge(), 0, 0});
    if (auto r = session.execute(project::insertComponent(component, id, placement, policy)); !r) return r.error();
    session.selectItem(id);
    return id;
}

std::string componentFileName(const std::string& componentId) {
    std::string name = componentId;
    for (char& ch : name) {
        const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
                        ch == '.' || ch == '-' || ch == '_';
        if (!ok) ch = '-';
    }
    if (name.empty() || name.front() == '.') name.insert(0, "component");
    return name + kComponentFileExtension;
}

core::Result<fs::path> saveItemAsComponent(const project::Project& project, const std::string& itemId,
                                           formats::ComponentMeta meta, const fs::path& folder, bool overwrite) {
    if (folder.empty())
        return core::makeError(core::ErrorCode::InvalidArgument, "No component library folder was chosen.");
    auto component = project::makeComponent(project, itemId, std::move(meta));
    if (!component) return component.error();
    std::error_code ec;
    fs::create_directories(folder, ec);
    if (ec)
        return core::makeError(core::ErrorCode::IoError, "Cannot create the component folder '" +
                                                              folder.generic_string() + "': " + ec.message() + ".");
    const fs::path path = folder / componentFileName(component.value().meta.id);
    if (!overwrite && fs::exists(path, ec))
        return core::makeError(core::ErrorCode::AlreadyExists,
                               "The component file '" + path.generic_string() + "' already exists.");
    if (auto r = project::saveComponents(path, {component.value()}); !r) return r.error();
    return path;
}

std::vector<std::string> parseTagList(const QString& text) {
    std::vector<std::string> out;
    for (const QString& part : text.split(QLatin1Char(','))) {
        const std::string tag = part.trimmed().toStdString();
        if (!tag.empty() && std::find(out.begin(), out.end(), tag) == out.end()) out.push_back(tag);
    }
    return out;
}

} // namespace fos::app
