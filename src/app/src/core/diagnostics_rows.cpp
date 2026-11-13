// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/diagnostics_rows.hpp"

namespace fos::app {

std::vector<DiagnosticRow> diagnosticRows(const project::Resolved& resolved,
                                          const validation::ValidationReport& validation) {
    std::vector<DiagnosticRow> rows;
    auto ownerOfPart = [&](const std::string& id) -> std::string {
        for (const auto& a : resolved.model.assemblies)
            if (a.findPart(domain::PartId(id))) return a.id.str();
        return {};
    };
    for (const auto& d : resolved.diagnostics) {
        DiagnosticRow row{d.severity, DiagnosticSource::Generation, d.code, d.message, d.entityId, {}, {}};
        if (!d.entityId.empty()) {
            const std::string owner = ownerOfPart(d.entityId);
            if (!owner.empty()) {
                row.partId = d.entityId;
                row.itemId = owner;
            } else {
                // Not a part of the model: an item id, or a "<item>/<slot>" id of a part that
                // was not generated; attribute it to the item when one matches.
                for (const auto& item : resolved.items)
                    if (d.entityId == item.itemId || d.entityId.rfind(item.itemId + "/", 0) == 0) {
                        row.itemId = item.itemId;
                        break;
                    }
            }
        }
        rows.push_back(std::move(row));
    }
    for (const auto& f : validation.diagnostics) {
        DiagnosticRow row{f.severity, DiagnosticSource::Validation, f.code, f.message, f.entityId, f.partId, f.assemblyId};
        if (row.partId.empty() && !ownerOfPart(f.entityId).empty()) row.partId = f.entityId;
        if (row.itemId.empty() && !row.partId.empty()) row.itemId = ownerOfPart(row.partId);
        rows.push_back(std::move(row));
    }
    return rows;
}

DiagnosticSummary summarize(const std::vector<DiagnosticRow>& rows) {
    DiagnosticSummary s;
    for (const auto& r : rows) {
        switch (r.severity) {
        case core::Severity::Error: ++s.errors; break;
        case core::Severity::Warning: ++s.warnings; break;
        case core::Severity::Info: ++s.infos; break;
        }
    }
    return s;
}

} // namespace fos::app
