// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// View-model of the "Edge-banding jobs" view of the Production workspace. Everything is
/// computed by the production module (production::edgeBandingJobs, edgeBandingJobsTable,
/// edgeBandingJobSheetsSvg in fos/production/edge_banding_jobs.hpp); this file only lays the
/// result out as translated tree rows and writes the export files. Lengths in mm.
#include "fos/app/text_tree.hpp"
#include "fos/core/result.hpp"
#include "fos/production/edge_banding_jobs.hpp"
#include "fos/project/project.hpp"

#include <QCoreApplication>
#include <QString>
#include <QStringList>

#include <filesystem>
#include <string>
#include <vector>

namespace fos::app {

/// production::edgeBandingJobs of `model` with the project's edge-banding allowances
/// (project::productionOptions(project).edgeBanding: overhang per end), whole model.
production::EdgeBandingJobs projectEdgeBandingJobs(const project::Project& project, const domain::Model& model);

/// Translated layout of a job list. Tree columns: Item, Edge, Step, Cut edge length, Edge length,
/// Strip length (see EdgeBandingText::columns()).
struct EdgeBandingJobRows {
    /// One node per band (production order): band caption and totals; children = parts
    /// (part number, name, cut size), grandchildren = banded edges in suggested step order.
    std::vector<TextNode> bands;
    /// Shaped parts (contour banding): one node per part with one child per strip, then one
    /// node per band total. Empty when there are no shaped banded parts.
    std::vector<TextNode> contour;
    QString summary;      ///< "Overhang per end … · n band(s) · n edge(s) · strips total … m".
    QStringList warnings; ///< Diagnostics of the job list (production module messages, English).
};

class EdgeBandingText {
    Q_DECLARE_TR_FUNCTIONS(EdgeBandingText)
public:
    static QStringList columns();
    static EdgeBandingJobRows rows(const production::EdgeBandingJobs& jobs);
    /// production::kBandingOrderNote in the UI language.
    static QString orderNote();
    /// production::kContourBandingNote in the UI language.
    static QString contourNote();
};

/// CSV of production::edgeBandingJobsTable (UTF-8 with BOM).
std::string edgeBandingJobsCsv(const production::EdgeBandingJobs& jobs);

/// production::edgeBandingJobSheetsSvg written per band as "<stem>-<band id><ext>" (a band with
/// several pages: "<stem>-<band id>-1<ext>", ...). Characters other than letters, digits, '-',
/// '_' and '.' in the band id become '_'. Error (NotFound) when no part is banded.
core::Result<std::vector<std::filesystem::path>> exportEdgeBandingSheets(const production::EdgeBandingJobs& jobs,
                                                                         const std::filesystem::path& file);

/// production::boughtInTable with the captions and the item kind in the UI language.
class BoughtInText {
    Q_DECLARE_TR_FUNCTIONS(BoughtInText)
public:
    static production::Table table(const production::BoughtInList& list);
    /// "n item(s), total area x m2" or "No bought-in items."
    static QString summary(const production::BoughtInList& list);
};

/// CSV of BoughtInText::table (UTF-8 with BOM).
std::string boughtInCsv(const production::BoughtInList& list);

} // namespace fos::app
