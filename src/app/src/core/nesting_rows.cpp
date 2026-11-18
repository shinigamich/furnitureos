// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/nesting_rows.hpp"

#include <algorithm>
#include <charconv>
#include <limits>

namespace fos::app {

std::vector<NestingSummaryRow> nestingSummary(const project::CuttingPlans& plans) {
    std::vector<NestingSummaryRow> rows;
    for (const auto& plan : plans.nestingPlans) {
        NestingSummaryRow row;
        row.materialId = plan.job.materialId;
        row.materialName = plan.job.materialName;
        row.thickness = plan.job.thickness;
        for (const auto& part : plan.job.job.parts) row.parts += part.quantity;
        if (plan.result) {
            const auto& r = *plan.result;
            row.nested = true;
            row.sheets = static_cast<int>(r.sheets.size());
            row.utilisationPct = r.utilisation * 100.0;
            row.unplaced = static_cast<int>(r.unplaced.size());
            row.problems = static_cast<int>(std::count_if(plan.validation.begin(), plan.validation.end(),
                                                          [](const core::Diagnostic& d) {
                                                              return d.severity == core::Severity::Error;
                                                          }));
            row.iterationsRun = r.iterationsRun;
            row.heuristic = r.isHeuristic;
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

std::vector<CuttingTableRow> cuttingTableRows(const project::CuttingPlans& plans) {
    std::vector<CuttingTableRow> rows;
    for (const auto& m : plans.materials) {
        if (m.guillotine) rows.push_back({m.materialId, PlanKind::Guillotine, *m.guillotine});
        if (m.nesting) rows.push_back({m.materialId, PlanKind::Nesting, *m.nesting});
    }
    return rows;
}

project::NestingSettings nestingSettingsInput(const project::Project& project) {
    return project.cutting.nestingSettings();
}

project::Command nestingSettingsCommand(const project::NestingSettings& settings) {
    return project::commands::setNestingSettings(settings);
}

std::optional<std::uint32_t> parseNestingSeed(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.remove_suffix(1);
    if (text.empty()) return std::nullopt;
    unsigned long long value = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc() || end != text.data() + text.size() ||
        value > std::numeric_limits<std::uint32_t>::max())
        return std::nullopt;
    return static_cast<std::uint32_t>(value);
}

} // namespace fos::app
