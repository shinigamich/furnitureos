// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// File > Export job pack: production::jobPackHtml of the open project with the project's
/// production settings, its metadata and, when the Plans section is included, the Session's
/// cutting plans rendered as inline SVG (cutting::renderSheetSvg / nesting::renderNestSvg, the
/// same composition as `fos jobpack`). The job-pack content itself is English (production module).
#include "fos/core/result.hpp"
#include "fos/production/job_pack.hpp"
#include "fos/project/outputs.hpp"

#include <QString>

#include <string>
#include <vector>

namespace fos::app {

class Session;

struct JobPackRequest {
    /// Sections to include (document order is fixed by the production module); must not be empty.
    std::vector<production::JobPackSection> sections = production::allJobPackSections();
    std::string date; ///< Printed on the cover as given (caller supplies today); empty = not shown.
};

/// Translated checkbox caption of a section ("Cover page", "Part list", ...).
QString jobPackSectionTitle(production::JobPackSection section);

/// Appends one JobPackPlanImage per guillotine and nesting sheet (model material order, captions
/// as `fos jobpack`), a heuristic-layout note and a note per failed optimisation to `options`;
/// `plans.diagnostics` are appended to options.extraDiagnostics.
void addCuttingPlanImages(const project::CuttingPlans& plans, production::JobPackOptions& options);

/// The job pack of `resolved` with name / author of `project`, its cut-list and edge-banding
/// settings and `plans` (only used when the Plans section is requested; nullptr = no plans).
core::Result<std::string> jobPackHtml(const project::Project& project, const project::Resolved& resolved,
                                      const project::CuttingPlans* plans, const JobPackRequest& request);

/// jobPackHtml of the Session's document; the cutting plans (Session::cuttingPlans) are only
/// computed when Plans is requested. InvalidArgument when the project could not be resolved.
core::Result<std::string> jobPackHtml(Session& session, const JobPackRequest& request);

} // namespace fos::app
