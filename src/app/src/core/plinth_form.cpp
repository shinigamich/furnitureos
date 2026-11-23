// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/plinth_form.hpp"

#include "fos/app/session.hpp"
#include "fos/app/templates.hpp"
#include "fos/core/units.hpp"

#include <QCoreApplication>

#include <cmath>

namespace fos::app {
namespace {
// Context "fos::app": lupdate files these free tr() calls under the enclosing namespace.
QString tr(const char* text) { return QCoreApplication::translate("fos::app", text); }
QString len(double mm) { return QString::fromStdString(core::formatLength(mm)); }
QString qs(const std::string& s) { return QString::fromStdString(s); }
} // namespace

std::vector<FormChoice> plinthMaterialChoices(const project::Project& project) {
    std::vector<FormChoice> out;
    out.push_back({std::string{}, tr("Carcass material of the first cabinet")});
    for (const auto& m : project.libraries.materials) out.push_back({m.id.str(), materialChoiceText(m)});
    return out;
}

std::string nextPlinthId(const project::Project& project) { return nextRunItemId(project, "plinth"); }

project::PlinthRequest defaultPlinthRequest(const project::Project& project, std::vector<std::string> itemIds) {
    project::PlinthRequest request;
    request.id = nextPlinthId(project);
    request.name = uniqueItemName(project, tr("Plinth").toStdString());
    request.itemIds = std::move(itemIds);
    return request;
}

PlinthPreview plinthPreview(const project::Project& project, const project::PlinthRequest& request) {
    PlinthPreview preview;
    auto plan = project::planPlinth(project, request);
    if (!plan) {
        preview.error = qs(plan.error().message);
        return preview;
    }
    const project::PlinthPlan& p = plan.value();
    preview.ok = true;
    for (const auto& item : p.items) preview.itemIds.push_back(item.id());

    QStringList lines;
    lines << tr("Length %1 mm, height %2 mm, thickness %3 mm").arg(len(p.length), len(p.height), len(p.thickness));
    lines << tr("Material: %1").arg(qs(p.materialId));
    lines << tr("Longest piece allowed: %1 mm").arg(len(p.usableStockLength));
    lines << tr("Front pieces: %1").arg(p.frontPieces);
    for (std::size_t i = 0; i < p.frontPieces && i < p.items.size() && i < p.pieces.size(); ++i) {
        const auto& [x0, x1] = p.pieces[i];
        lines << tr("  %1 (%2): %3 mm, from %4 to %5 mm")
                     .arg(qs(p.items[i].name()), qs(p.items[i].id()), len(x1 - x0), len(x0), len(x1));
    }
    // Butt joints: where two consecutive pieces touch (an appliance end panel leaves a gap,
    // which is an interruption, not a joint).
    QStringList joints;
    for (std::size_t i = 1; i < p.pieces.size(); ++i)
        if (std::abs(p.pieces[i].first - p.pieces[i - 1].second) < 1e-6) joints << len(p.pieces[i].first);
    if (joints.isEmpty())
        lines << tr("No joints.");
    else
        lines << tr("Butt joints at %1 mm from the left end").arg(joints.join(QStringLiteral(", ")));
    for (std::size_t i = p.frontPieces; i < p.items.size(); ++i)
        lines << tr("  Return %1 (%2)").arg(qs(p.items[i].name()), qs(p.items[i].id()));
    lines << diagnosticLines(p.notes);
    preview.text = lines.join(QLatin1Char('\n'));
    return preview;
}

core::Result<std::vector<std::string>> applyPlinth(Session& session, const project::PlinthRequest& request) {
    auto plan = project::planPlinth(session.project(), request);
    if (!plan) return plan.error();
    std::vector<std::string> ids;
    for (const auto& item : plan.value().items) ids.push_back(item.id());
    if (auto r = session.execute(project::commands::addPlinth(request)); !r) return r.error();
    if (!ids.empty()) session.selectItem(ids.front());
    return ids;
}

} // namespace fos::app
