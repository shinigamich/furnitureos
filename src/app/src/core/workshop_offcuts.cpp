// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/workshop_offcuts.hpp"

#include "fos/core/units.hpp"

#include <QCoreApplication>

#include <algorithm>
#include <system_error>

namespace fos::app {
namespace {
// Context "fos::app": lupdate files these free tr() calls under the enclosing namespace.
QString tr(const char* text) { return QCoreApplication::translate("fos::app", text); }
QString len(double mm) { return QString::fromStdString(core::formatLength(mm)); }
bool contains(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}
} // namespace

core::Result<project::RemnantStore> loadWorkshopStore(const std::filesystem::path& path, bool allowMissing) {
    std::error_code ec;
    if (allowMissing && !std::filesystem::exists(path, ec)) return project::RemnantStore{};
    return project::loadRemnantStore(path);
}

project::StoreCuttingRun workshopCuttingRun(const project::Resolved& resolved, const project::Project& project,
                                            const project::RemnantStore& store) {
    project::StoreUseOptions options;
    options.jobName = project.metadata.name;
    return project::cuttingPlansWithStore(resolved, project.cutting, store, options);
}

project::StoreCuttingRun plainCuttingRun(const project::CuttingPlans& plans) {
    project::StoreCuttingRun run;
    run.plans = plans;
    return run;
}

std::vector<WorkshopRemnantRow> workshopRemnantRows(const project::RemnantStore& store,
                                                    const project::StoreCuttingRun& run) {
    std::vector<WorkshopRemnantRow> rows;
    for (const auto& r : store.remnants) {
        WorkshopRemnantRow row;
        row.id = r.id;
        row.materialId = r.materialId;
        row.size = tr("%1 × %2 × %3 mm").arg(len(r.length), len(r.width), len(r.thickness));
        const auto skipped = std::find_if(run.skipped.begin(), run.skipped.end(),
                                          [&](const project::SkippedRemnant& s) { return s.remnantId == r.id; });
        if (skipped != run.skipped.end()) {
            row.status = WorkshopRemnantRow::Status::Skipped;
            row.detail = QString::fromStdString(skipped->reason);
        } else if (contains(run.offered, r.id)) {
            int pieces = 0, sheet = -1;
            for (const auto& u : run.used)
                if (u.remnantId == r.id) {
                    pieces += u.pieceCount;
                    sheet = u.sheetIndex;
                }
            if (sheet >= 0) {
                row.status = WorkshopRemnantRow::Status::Used;
                row.detail = tr("used for %1 pieces (sheet %2)").arg(pieces).arg(sheet + 1);
            } else {
                row.status = WorkshopRemnantRow::Status::OfferedUnused;
                row.detail = tr("offered, not needed");
            }
        } else {
            continue; // a material this project does not cut
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

QString workshopRunSummary(const project::StoreCuttingRun& run) {
    return tr("%1 offcuts offered, %2 used, %3 skipped")
        .arg(run.offered.size())
        .arg(run.used.size())
        .arg(run.skipped.size());
}

project::OffcutImportOptions offcutImportOptions(const project::Project& project,
                                                 const std::filesystem::path& projectFile, const QDate& today) {
    project::OffcutImportOptions o;
    o.date = today.toString(QStringLiteral("yyyy-MM-dd")).toStdString();
    o.source = "offcut of '" + project.metadata.name + "'";
    if (!projectFile.empty()) o.source += " (" + projectFile.filename().string() + ")";
    return o;
}

QStringList storeChangeLines(const project::StoreCommit& commit) {
    QStringList lines;
    for (const auto& r : commit.removed)
        lines << tr("Remove (used): %1").arg(QString::fromStdString(project::describeRemnant(r)));
    for (const auto& r : commit.added)
        lines << tr("Add: %1").arg(QString::fromStdString(project::describeRemnant(r)));
    return lines;
}

} // namespace fos::app
