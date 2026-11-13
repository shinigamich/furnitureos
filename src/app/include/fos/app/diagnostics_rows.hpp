// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Diagnostics panel view-model: generator findings of resolve() plus model validation.
#include "fos/project/resolve.hpp"
#include "fos/validation/report.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace fos::app {

enum class DiagnosticSource { Generation, Validation };

struct DiagnosticRow {
    core::Severity severity = core::Severity::Error;
    DiagnosticSource source = DiagnosticSource::Generation;
    std::string code;
    std::string message;  ///< English text from the core (not translated, see README).
    std::string entityId;
    std::string partId;   ///< Part of the model this finding points at; empty when none.
    std::string itemId;   ///< Item it belongs to; empty for project-level findings.
};

struct DiagnosticSummary {
    std::size_t errors = 0, warnings = 0, infos = 0;
};

/// Generation diagnostics (resolved.diagnostics, in order) followed by the validation
/// findings (report order). partId: the finding's part (validation) or the entity when it
/// is a part of the model (generation). itemId: the owning assembly / item.
std::vector<DiagnosticRow> diagnosticRows(const project::Resolved& resolved,
                                          const validation::ValidationReport& validation);
DiagnosticSummary summarize(const std::vector<DiagnosticRow>& rows);

} // namespace fos::app
