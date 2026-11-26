// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// JOB PACK: one self-contained, printable UTF-8 HTML document for the workshop that composes
/// the production outputs of a whole model: cover page (project, date, items, totals), part
/// list, cut list per material, bought-in items, edge-banding jobs per band, hardware (with the
/// total length of linear fittings), packing list, assembly sheets, cutting-plan drawings
/// (inline SVG supplied by the caller) and the warnings of those reports.
///
/// Print layout: A4 landscape (@page), every section starts on a new page, no external
/// resources (fonts, images, stylesheets) and no JavaScript. All text taken from the model or
/// the options is XML-escaped (escapeXml); only the caller-supplied cutting-plan SVGs are
/// embedded as markup (their XML prolog removed, see JobPackPlanImage).
///
/// The production module does not depend on the cutting optimiser, so cutting plans are passed
/// in already rendered (the project/CLI layer renders them with cutting::renderSheetSvg /
/// nesting::renderNestSvg). Pure function of its inputs: identical inputs give byte-identical
/// output (no clock, no addresses, no hash-map iteration order).
#include "fos/core/result.hpp"
#include "fos/production/common.hpp"
#include "fos/production/packing.hpp"
#include "fos/production/reports.hpp"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fos::production {

/// Sections in document order.
enum class JobPackSection { Cover, Parts, CutList, BoughtIn, EdgeBanding, Hardware, Packing, Assembly, Plans, Warnings };

/// Command-line / anchor name: "cover", "parts", "cutlist", "boughtin", "edgebanding",
/// "hardware", "packing", "assembly", "plans", "warnings". Also the HTML id of the section.
const char* toString(JobPackSection section) noexcept;
/// Inverse of toString (exact, lower case); nullopt for anything else.
std::optional<JobPackSection> jobPackSectionFromString(std::string_view name) noexcept;
/// Every section in document order.
std::vector<JobPackSection> allJobPackSections();

/// One rendered cutting / nesting sheet drawing.
struct JobPackPlanImage {
    std::string caption; ///< Escaped heading shown above the drawing, e.g. "Board 18 - sheet 1".
    /// A complete SVG document. Everything before the first "<svg" (XML declaration, DOCTYPE,
    /// comments) and after the last "</svg>" is dropped; the rest is embedded verbatim, so it
    /// must come from a trusted renderer that escapes its own text.
    std::string svg;
};

struct JobPackOptions {
    std::string projectName;
    std::string date;   ///< Printed as given (the module never reads the clock); empty = not shown.
    std::string author; ///< Empty = not shown.
    /// Quantity printed per item (assembly) on the cover; items not listed have quantity 1.
    /// Values < 1 are an InvalidArgument error.
    std::map<domain::AssemblyId, int> itemQuantities;
    /// Sections to include, rendered in document order whatever the order here (duplicates
    /// ignored). Must not be empty.
    std::vector<JobPackSection> sections = allJobPackSections();
    CutListOptions cutList;
    EdgeBandingOptions edgeBanding;
    PackingOptions packing; ///< Validated as by packingList() (only when Packing is included).
    /// Cutting-plan section content; an empty list prints the "none" note.
    std::vector<JobPackPlanImage> cuttingPlans;
    std::vector<std::string> planNotes; ///< Escaped paragraphs above the drawings (e.g. "heuristic layout").
    /// Extra findings for the Warnings section (e.g. cutting-plan diagnostics), after the report ones.
    core::Diagnostics extraDiagnostics;
};

/// Builds the job pack of the whole model. Errors (InvalidArgument): empty `sections`, an item
/// quantity < 1, invalid packing options (Packing included), a plan image without an "<svg"
/// element. The Warnings section lists every Warning / Error diagnostic of the included reports
/// plus `extraDiagnostics`, without exact duplicates, in report order (Info findings omitted);
/// it is left out when there is none.
core::Result<std::string> jobPackHtml(const domain::Model& model, const JobPackOptions& options = {});

} // namespace fos::production
