// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/project_compare.hpp"

#include "fos/project/file.hpp"

namespace fos::app {
namespace {

QString str(const std::string& s) { return QString::fromStdString(s); }
QString opt(const std::optional<std::string>& s) { return s ? str(*s) : QStringLiteral("—"); }
QString len(double mm) { return QString::fromStdString(core::formatLength(mm)); }

TextNode group(const QString& title, std::size_t count) {
    TextNode n;
    n.emphasis = true;
    n.columns = {CompareText::tr("%1 (%2)").arg(title).arg(count)};
    return n;
}

TextNode leaf(const QString& label, const QString& before, const QString& after) {
    TextNode n;
    n.columns = {label, before, after};
    return n;
}

void addChanges(TextNode& parent, const std::vector<project::ValueChange>& changes) {
    for (const auto& c : changes)
        parent.children.push_back(leaf(str(c.path), CompareText::value(c.before), CompareText::value(c.after)));
}

void addSubgroup(TextNode& parent, const QString& title, const std::vector<project::ValueChange>& changes) {
    if (changes.empty()) return;
    TextNode n = group(title, changes.size());
    addChanges(n, changes);
    parent.children.push_back(std::move(n));
}

QString itemType(const std::string& type) {
    if (type == "cabinet") return CompareText::tr("cabinet");
    if (type == "panel") return CompareText::tr("panel");
    return str(type);
}

QString partText(const project::PartRef& p) {
    return CompareText::tr("%1 × %2 × %3 mm, %4").arg(len(p.length), len(p.width), len(p.thickness), str(p.material));
}

QString pointText(const geometry::Vec3& v) {
    return QStringLiteral("(%1, %2, %3)").arg(len(v.x), len(v.y), len(v.z));
}

QString categoryTitle(const std::string& c) {
    if (c == "materials") return CompareText::tr("Materials");
    if (c == "edgeBands") return CompareText::tr("Edge bands");
    if (c == "hardware") return CompareText::tr("Hardware");
    return str(c);
}

QString settingsTitle(const std::string& g) {
    if (g == "cutting") return CompareText::tr("Cutting");
    if (g == "pricing") return CompareText::tr("Pricing");
    if (g == "room") return CompareText::tr("Room");
    if (g == "settings") return CompareText::tr("Project settings");
    return str(g);
}

TextNode itemNode(const project::ItemChange& c) {
    TextNode n;
    n.columns = {CompareText::tr("%1 (%2)").arg(str(c.name), str(c.id))};
    if (c.renamedFrom) n.children.push_back(leaf(CompareText::tr("Name"), str(*c.renamedFrom), str(c.name)));
    if (c.typeBefore) n.children.push_back(leaf(CompareText::tr("Type"), itemType(*c.typeBefore), QString()));
    if (!c.parameters.empty()) {
        TextNode p = group(CompareText::tr("Parameters"), c.parameters.size());
        for (const auto& pc : c.parameters) p.children.push_back(leaf(str(pc.name), opt(pc.before), opt(pc.after)));
        n.children.push_back(std::move(p));
    }
    addSubgroup(n, CompareText::tr("Materials and edges"), c.materials);
    addSubgroup(n, CompareText::tr("Hardware"), c.hardware);
    addSubgroup(n, CompareText::tr("Construction"), c.construction);
    addSubgroup(n, CompareText::tr("Interior"), c.interior);
    addSubgroup(n, CompareText::tr("Fronts"), c.fronts);
    if (c.placement) {
        QString what = c.placement->moved && c.placement->rotated ? CompareText::tr("Placement (moved, rotated)")
                       : c.placement->rotated                     ? CompareText::tr("Placement (rotated)")
                                                                  : CompareText::tr("Placement (moved)");
        n.children.push_back(leaf(what, pointText(c.placement->beforeOrigin), pointText(c.placement->afterOrigin)));
    }
    addSubgroup(n, CompareText::tr("Other"), c.other);
    return n;
}

TextNode modelNode(const project::ModelDiff& m) {
    const std::size_t count = m.added.size() + m.removed.size() + m.changed.size() + m.hardware.size() +
                              m.notGeneratedBefore.size() + m.notGeneratedAfter.size();
    TextNode n = group(CompareText::tr("Production model"), count);
    auto partLabel = [](const project::PartRef& p) { return CompareText::tr("%1 (%2)").arg(str(p.name), str(p.id)); };
    if (!m.added.empty()) {
        TextNode g = group(CompareText::tr("Parts added"), m.added.size());
        for (const auto& p : m.added) g.children.push_back(leaf(partLabel(p), QStringLiteral("—"), partText(p)));
        n.children.push_back(std::move(g));
    }
    if (!m.removed.empty()) {
        TextNode g = group(CompareText::tr("Parts removed"), m.removed.size());
        for (const auto& p : m.removed) g.children.push_back(leaf(partLabel(p), partText(p), QStringLiteral("—")));
        n.children.push_back(std::move(g));
    }
    if (!m.changed.empty()) {
        TextNode g = group(CompareText::tr("Parts changed"), m.changed.size());
        for (const auto& c : m.changed) {
            QStringList what;
            if (c.sizeChanged) what << CompareText::tr("size");
            if (c.materialChanged) what << CompareText::tr("material");
            if (c.edgesChanged) what << CompareText::tr("edge bands");
            if (c.grainChanged) what << CompareText::tr("grain");
            if (c.operationsBefore != c.operationsAfter) what << CompareText::tr("operations");
            TextNode p = leaf(CompareText::tr("%1: %2").arg(partLabel(c.after), what.join(QStringLiteral(", "))),
                              partText(c.before), partText(c.after));
            if (c.operationsBefore != c.operationsAfter)
                p.children.push_back(leaf(CompareText::tr("Operations"), QString::number(c.operationsBefore),
                                          QString::number(c.operationsAfter)));
            g.children.push_back(std::move(p));
        }
        n.children.push_back(std::move(g));
    }
    if (!m.hardware.empty()) {
        TextNode g = group(CompareText::tr("Hardware quantities"), m.hardware.size());
        for (const auto& h : m.hardware)
            g.children.push_back(leaf(str(h.definition), QString::number(h.before), QString::number(h.after)));
        n.children.push_back(std::move(g));
    }
    for (const auto& id : m.notGeneratedBefore) {
        TextNode w = leaf(CompareText::tr("Item %1 could not be generated in the current project").arg(str(id)), {}, {});
        w.warning = true;
        n.children.push_back(std::move(w));
    }
    for (const auto& id : m.notGeneratedAfter) {
        TextNode w = leaf(CompareText::tr("Item %1 could not be generated in the other file").arg(str(id)), {}, {});
        w.warning = true;
        n.children.push_back(std::move(w));
    }
    return n;
}

} // namespace

core::Result<project::ProjectDiff> compareWithFile(const project::Project& current, const std::filesystem::path& other) {
    auto loaded = project::loadProject(other);
    if (!loaded) return loaded.error();
    project::DiffOptions options;
    options.model = true;
    return project::diffProjects(current, loaded.value().project, options);
}

QStringList CompareText::columns() { return {tr("Change"), tr("Current project"), tr("Other file")}; }

QString CompareText::value(const std::optional<formats::Json>& v) {
    if (!v) return QStringLiteral("—");
    if (v->is_string()) return str(v->get<std::string>());
    return str(v->dump());
}

std::vector<TextNode> CompareText::tree(const project::ProjectDiff& diff) {
    std::vector<TextNode> out;
    if (diff.empty()) {
        out.push_back(leaf(tr("No differences"), {}, {}));
        return out;
    }
    if (!diff.metadata.empty()) {
        TextNode g = group(tr("Metadata"), diff.metadata.size());
        addChanges(g, diff.metadata);
        out.push_back(std::move(g));
    }
    if (!diff.libraries.empty()) {
        TextNode g = group(tr("Libraries"), diff.libraries.size());
        for (const auto& c : diff.libraries) {
            TextNode cat = group(categoryTitle(c.category), c.added.size() + c.removed.size() + c.changed.size());
            for (const auto& id : c.added) cat.children.push_back(leaf(tr("Added: %1").arg(str(id)), QStringLiteral("—"), str(id)));
            for (const auto& id : c.removed)
                cat.children.push_back(leaf(tr("Removed: %1").arg(str(id)), str(id), QStringLiteral("—")));
            for (const auto& e : c.changed) {
                TextNode entry;
                entry.columns = {tr("Changed: %1").arg(str(e.id))};
                addChanges(entry, e.fields);
                cat.children.push_back(std::move(entry));
            }
            g.children.push_back(std::move(cat));
        }
        out.push_back(std::move(g));
    }
    const std::size_t items = diff.itemsAdded.size() + diff.itemsRemoved.size() + diff.itemsChanged.size();
    if (items > 0 || diff.itemOrderChanged) {
        TextNode g = group(tr("Items"), items);
        auto ref = [](const project::ItemRef& r) { return tr("%1 (%2, %3)").arg(str(r.name), str(r.id), itemType(r.type)); };
        for (const auto& r : diff.itemsAdded) g.children.push_back(leaf(tr("Added: %1").arg(ref(r)), QStringLiteral("—"), str(r.name)));
        for (const auto& r : diff.itemsRemoved)
            g.children.push_back(leaf(tr("Removed: %1").arg(ref(r)), str(r.name), QStringLiteral("—")));
        for (const auto& c : diff.itemsChanged) g.children.push_back(itemNode(c));
        if (diff.itemOrderChanged) g.children.push_back(leaf(tr("Item order changed"), {}, {}));
        out.push_back(std::move(g));
    }
    if (!diff.settings.empty()) {
        TextNode g = group(tr("Settings"), diff.settings.size());
        for (const auto& s : diff.settings) {
            TextNode sub = group(settingsTitle(s.group), s.changes.size());
            addChanges(sub, s.changes);
            g.children.push_back(std::move(sub));
        }
        out.push_back(std::move(g));
    }
    if (!diff.other.empty()) {
        TextNode g = group(tr("Other"), diff.other.size());
        addChanges(g, diff.other);
        out.push_back(std::move(g));
    }
    if (diff.model && !diff.model->empty()) out.push_back(modelNode(*diff.model));
    return out;
}

std::string compareText(const project::ProjectDiff& diff) { return project::diffText(diff); }

std::string compareJson(const project::ProjectDiff& diff) { return project::diffToJson(diff).dump(2) + "\n"; }

} // namespace fos::app
