// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Internal: HTML fragments of the job pack sections built from report structures (job_pack.cpp
// composes them). Every string from the model is escaped with escapeXml.
#include "fos/production/assembly_sheet.hpp"
#include "fos/production/edge_banding_jobs.hpp"
#include "fos/production/packing.hpp"

#include <string>

namespace fos::production::jobpack {

/// "<p class=\"empty\">none</p>".
std::string emptyNote();
/// One table + totals per band, then the contour-banding section.
std::string edgeBandingJobsHtml(const EdgeBandingJobs& jobs);
/// Per item: package table (contents, size, weight, flags) and hardware bag (with lengths).
std::string packingHtml(const PackingList& list);
/// Per item: caveat and an ordered list of steps with parts and hardware.
std::string assemblySheetsHtml(const AssemblySheets& sheets);

} // namespace fos::production::jobpack
