// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Edge-banding JOB LIST for the banding machine operator: per edge band (one roll change per
/// band), the parts to band, their cut size and, per banded edge, the edge length, the strip
/// length with overhang and a suggested banding order. Contoured (shaped) parts are listed
/// separately for contour banding. Pure function of a read-only model; all lengths in mm.
#include "fos/production/common.hpp"
#include "fos/production/reports.hpp"

#include <optional>
#include <string>
#include <vector>

namespace fos::production {

/// Printed with every job list. The order is the common workshop practice, NOT a machine
/// requirement: the band of the short edges then covers the ends of the long-edge band.
inline constexpr const char* kBandingOrderNote =
    "Suggested order (common practice, not a machine requirement): long edges first, then short "
    "edges, so the short-edge band overlaps the ends of the long-edge band.";

/// How shaped parts are banded: they cannot run through a straight edge bander.
inline constexpr const char* kContourBandingNote = "contour banding - manual / edge bander with contour unit";

/// One banded straight edge of a rectangular part.
struct BandedEdgeJob {
    domain::EdgeSide side = domain::EdgeSide::L1;
    /// Length of the CUT edge as it comes off the saw (L1/L2: cut length, W1/W2: cut width);
    /// shorter than `edgeLength` by the banding on the two adjacent edges.
    double cutEdgeLength = 0;
    double edgeLength = 0;  ///< Finished edge length (domain::edgeLength), as in edgeBandingReport().
    double stripLength = 0; ///< edgeLength + 2 * overhangPerEnd: the tape to cut for this edge.
    /// Suggested position of this edge in the banding of the part, 1-based over ALL banded edges
    /// of the part (any band): longer edges first; equal lengths in L1, L2, W1, W2 order.
    int step = 0;
};

/// The edges of one part that carry the group's band.
struct PartBandingJob {
    int partNumber = 0; ///< PartRow::number (model-wide, scope independent).
    domain::PartId partId;
    std::string partName;
    domain::AssemblyId assemblyId;
    double length = 0, width = 0;       ///< Finished size.
    double cutLength = 0, cutWidth = 0; ///< domain::cutSize.
    double thickness = 0;
    int stepsTotal = 0;     ///< Banded edges of the part over all bands.
    bool otherBands = false; ///< Some edges of the part carry another band (see the other group).
    std::vector<BandedEdgeJob> edges; ///< In `step` order.
};

/// All rectangular-part edges of one band = one roll on the machine.
struct EdgeBandJobGroup {
    domain::EdgeBandId edgeBandId;
    std::string name;     ///< Empty when `missing`.
    bool missing = false; ///< Referenced by parts but absent from the model.
    double thickness = 0, width = 0; ///< mm; 0 when missing.
    std::vector<PartBandingJob> parts; ///< Part-number order.
    int edgeCount = 0;
    double totalEdgeLength = 0;  ///< Sum of edgeLength.
    double totalStripLength = 0; ///< Sum of stripLength (applied + overhang).
};

/// One banded outline segment of a shaped part.
struct ContourSegmentBand {
    int segment = 0; ///< 0-based index in PartShape::outer.
    domain::EdgeBandId edgeBandId;
    std::optional<double> length; ///< Exact segment length (arcs exact); empty when unavailable.
};

/// One continuous strip (tangent-continuous segments with one band, see edgeBandingReport()).
struct ContourStrip {
    domain::EdgeBandId edgeBandId;
    double length = 0;      ///< Exact applied length.
    double stripLength = 0; ///< length + 2 * overhangPerEnd.
};

/// A shaped part with banded outline segments: never dropped, always listed here.
struct ContourBandingJob {
    int partNumber = 0;
    domain::PartId partId;
    std::string partName;
    domain::AssemblyId assemblyId;
    double cutLength = 0, cutWidth = 0, thickness = 0; ///< Blank (domain::cutSize).
    /// False for a shape whose outline is a plain rectangle (straight strips, still listed here
    /// because its bands are per outline segment, not per L1/L2/W1/W2 edge).
    bool routed = true;
    /// False when the outline is invalid or `outerEdges` has the wrong number of entries:
    /// the segments are then listed without lengths and `strips` is empty (diagnostic added).
    bool lengthsAvailable = true;
    std::vector<ContourSegmentBand> segments; ///< Banded segments in outline order.
    std::vector<ContourStrip> strips;         ///< In outline order.
};

/// Contour-banding totals of one band (shaped parts only).
struct ContourBandTotal {
    domain::EdgeBandId edgeBandId;
    std::string name; ///< Empty when missing.
    bool missing = false;
    int stripCount = 0;
    double totalLength = 0, totalStripLength = 0;
};

struct EdgeBandingJobs {
    double overhangPerEnd = 0; ///< Allowance used (negative / non-finite input -> 0).
    std::vector<EdgeBandJobGroup> groups;     ///< Edge bands in model order, then missing ids sorted.
    std::vector<ContourBandingJob> contour;   ///< Part-number order.
    std::vector<ContourBandTotal> contourTotals; ///< Same band order as `groups`.
    /// production.bought_in.excluded (Info: bought-in parts, e.g. glass panes, are never banded),
    /// production.edgeband.missing, production.scope.unknown_assembly,
    /// production.edgeband_jobs.invalid_edge (edge of non-positive length, skipped),
    /// production.edgeband_jobs.shape_lengths_unavailable,
    /// production.edgeband_jobs.edges_on_shaped_part (Part::edges ignored on a shaped part).
    core::Diagnostics diagnostics;
};

/// Builds the job list. Only `settings.overhangPerEnd` is used (`wastePercent` is a purchasing
/// allowance, not a per-strip one). Parts without banding do not appear.
EdgeBandingJobs edgeBandingJobs(const domain::Model& model, const EdgeBandingOptions& settings = {},
                                const Scope& scope = {});

/// Plain text for the operator: one block per band with totals, then the contour section.
std::string edgeBandingJobsText(const EdgeBandingJobs& jobs);

/// One row per banded edge (Kind "edge") and per contour strip (Kind "contour"); feed to toCsv().
/// Columns: Band, Band name, Band thickness, Band width, Kind, No., Part ID, Part, Cut length,
/// Cut width, Edge, Step, Cut edge length, Edge length, Strip length.
Table edgeBandingJobsTable(const EdgeBandingJobs& jobs);

/// JSON document (UTF-8, numbers in mm, keys in camelCase), deterministic.
std::string edgeBandingJobsJson(const EdgeBandingJobs& jobs);

/// One printable A4 portrait sheet set per band (1 SVG unit = 1 mm on paper): header with band
/// and totals, the order note, one row per part with an edge diagram (banded edges thick,
/// step numbers) and the lengths; shaped parts with this band follow as contour rows. Long
/// lists continue on further pages.
struct EdgeBandJobSheet {
    domain::EdgeBandId edgeBandId;
    std::vector<std::string> pages; ///< SVG documents.
};
std::vector<EdgeBandJobSheet> edgeBandingJobSheetsSvg(const EdgeBandingJobs& jobs);

} // namespace fos::production
