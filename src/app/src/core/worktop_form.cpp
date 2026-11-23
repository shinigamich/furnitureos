// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/worktop_form.hpp"

#include "fos/app/session.hpp"
#include "fos/app/templates.hpp"
#include "fos/core/units.hpp"

#include <QCoreApplication>

namespace fos::app {
namespace {
// Context "fos::app": lupdate files these free tr() calls under the enclosing namespace.
QString tr(const char* text) { return QCoreApplication::translate("fos::app", text); }
QString len(double mm) { return QString::fromStdString(core::formatLength(mm)); }
QString qs(const std::string& s) { return QString::fromStdString(s); }
} // namespace

std::vector<WorktopChoice> worktopMaterialChoices(const project::Project& project) {
    std::vector<WorktopChoice> out;
    out.push_back({std::string{}, tr("Generic placeholder (38 mm)")});
    for (const auto& m : project.libraries.materials) {
        if (m.id.str() == project::kGenericWorktopMaterial) continue; // = the first entry
        out.push_back({m.id.str(), materialChoiceText(m)});
    }
    return out;
}

std::vector<WorktopChoice> worktopBandChoices(const project::Project& project) {
    std::vector<WorktopChoice> out;
    out.push_back({std::string{}, tr("Automatic")});
    for (const auto& b : project.libraries.edgeBands)
        out.push_back({b.id.str(),
                       tr("%1 — %2 × %3 mm (%4)").arg(qs(b.name), len(b.thickness), len(b.width), qs(b.id.str()))});
    return out;
}

QString worktopCutoutKindTitle(project::WorktopCutoutKind kind) {
    return kind == project::WorktopCutoutKind::Hob ? tr("Hob") : tr("Sink");
}

const std::vector<project::WorktopCutoutKind>& worktopCutoutKinds() {
    static const std::vector<project::WorktopCutoutKind> kinds{project::WorktopCutoutKind::Sink,
                                                               project::WorktopCutoutKind::Hob};
    return kinds;
}

std::string nextWorktopId(const project::Project& project) { return nextRunItemId(project, "worktop"); }

project::WorktopRequest defaultWorktopRequest(const project::Project& project, std::vector<std::string> itemIds) {
    project::WorktopRequest request;
    request.id = nextWorktopId(project);
    request.name = uniqueItemName(project, tr("Worktop").toStdString());
    request.itemIds = std::move(itemIds);
    return request;
}

WorktopPreview worktopPreview(const project::Project& project, const project::WorktopRequest& request) {
    WorktopPreview preview;
    auto plan = project::planWorktop(project, request);
    if (!plan) {
        preview.error = qs(plan.error().message);
        return preview;
    }
    const project::WorktopPlan& p = plan.value();
    preview.ok = true;
    preview.middleX = p.left - p.runStart + p.length / 2;
    for (const auto& item : p.items) preview.pieceIds.push_back(item.id());

    QStringList lines;
    lines << tr("Length %1 mm, depth %2 mm, thickness %3 mm").arg(len(p.length), len(p.depth), len(p.thickness));
    lines << tr("Longest piece allowed: %1 mm").arg(len(p.usableStockLength));
    // Piece lengths from the joint positions (measured from the left end).
    std::vector<double> bounds{0.0};
    bounds.insert(bounds.end(), p.joints.begin(), p.joints.end());
    bounds.push_back(p.length);
    lines << tr("Pieces: %1").arg(p.items.size());
    for (std::size_t i = 0; i < p.items.size() && i + 1 < bounds.size(); ++i)
        lines << tr("  %1 (%2): %3 mm").arg(qs(p.items[i].name()), qs(p.items[i].id()), len(bounds[i + 1] - bounds[i]));
    if (p.joints.empty()) {
        lines << tr("No joints.");
    } else {
        QStringList joints;
        for (double j : p.joints) joints << len(j);
        lines << tr("Butt joints at %1 mm from the left end").arg(joints.join(QStringLiteral(", ")));
    }
    if (p.addedMaterial)
        lines << tr("Adds the material %1 to the library.").arg(qs(p.addedMaterial->id.str()));
    if (p.addedEdgeBand)
        lines << tr("Adds the edge band %1 to the library.").arg(qs(p.addedEdgeBand->id.str()));
    lines << diagnosticLines(p.notes);
    preview.text = lines.join(QLatin1Char('\n'));
    return preview;
}

core::Result<std::vector<std::string>> applyWorktop(Session& session, const project::WorktopRequest& request) {
    auto plan = project::planWorktop(session.project(), request);
    if (!plan) return plan.error();
    std::vector<std::string> ids;
    for (const auto& item : plan.value().items) ids.push_back(item.id());
    if (auto r = session.execute(project::commands::addWorktop(request)); !r) return r.error();
    if (!ids.empty()) session.selectItem(ids.front());
    return ids;
}

} // namespace fos::app
