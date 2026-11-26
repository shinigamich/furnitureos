// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/production/common.hpp"

namespace fos::production {

// ---------------------------------------------------------------- materials

struct MaterialRow {
    domain::MaterialId materialId;
    std::string name;     ///< Empty when `missing`.
    bool missing = false; ///< Referenced by parts but absent from the model (or empty id).
    double thickness = 0; ///< Material thickness, mm (0 when missing).
    int partCount = 0;
    /// Sum of length * width; contoured parts contribute their exact outline area minus
    /// cut-outs (the material that ends up in the furniture).
    double finishedAreaM2 = 0;
    /// Sum of cutLength * cutWidth. For contoured parts this is the rectangular BLANK
    /// (domain::cutSize) because the blank is what is cut from the sheet before the outline is
    /// routed; the routed-away material is waste, so sheet estimates use the blank.
    double cutAreaM2 = 0;
    double sheetLength = 0, sheetWidth = 0; ///< mm, 0 when missing.
    double sheetAreaM2 = 0;
    /// True when the material exists and has a positive sheet size.
    bool sheetEstimateAvailable = false;
    /// ceil(cutAreaM2 / sheetAreaM2). This is an area-based LOWER BOUND, not a sheet
    /// count: it ignores saw kerf, trimming, grain and the fact that rectangles do not
    /// tile perfectly. The real count comes from the cutting optimiser.
    int sheetsLowerBoundByArea = 0;
    double costPerSheet = 0;         ///< 0 = unknown.
    bool costKnown = false;          ///< costPerSheet > 0 and estimate available.
    double costAtAreaLowerBound = 0; ///< sheetsLowerBoundByArea * costPerSheet when `costKnown`.
};

struct MaterialReport {
    std::vector<MaterialRow> rows; ///< Used materials in model order, then missing ids sorted.
    core::Diagnostics diagnostics;
};

/// Per-material totals. Only materials referenced by at least one part appear. Bought-in parts
/// (domain::isBoughtIn) are left out (one Info production.bought_in.excluded); so are they in
/// edgeBandingReport() and machiningSummary() (without a notice).
MaterialReport materialReport(const domain::Model& model, const Scope& scope = {});

// ------------------------------------------------------------- edge banding

struct EdgeBandingOptions {
    /// Extra tape at EACH end of every banded edge, mm (2 x 25 mm per edge by default).
    double overhangPerEnd = 25.0;
    /// Waste added on top of (applied + overhang), percent. Workshop-specific; default 0.
    double wastePercent = 0.0;
};

struct EdgeBandingRow {
    domain::EdgeBandId edgeBandId;
    std::string name;     ///< Empty when `missing`.
    bool missing = false;
    double thickness = 0, width = 0; ///< mm.
    /// Banded edges; for contoured parts the number of band strips (see below).
    int edgeCount = 0;
    /// Sum of domain::edgeLength of the banded edges. Contoured parts: exact length of the
    /// banded outline segments (arc lengths, geometry::segmentInfo on the finished outline).
    /// Consecutive banded segments with the same band and a tangent junction form ONE strip
    /// (one pair of overhangs); a sharp corner starts a new strip, as on a rectangle.
    double appliedLengthM = 0;
    double overhangAllowanceM = 0; ///< edgeCount * 2 * overhangPerEnd.
    double wasteAllowanceM = 0;    ///< (applied + overhang) * wastePercent / 100.
    double purchaseLengthM = 0;    ///< applied + overhang + waste.
    double costPerMeter = 0;       ///< 0 = unknown.
    bool costKnown = false;
    double cost = 0;               ///< purchaseLengthM * costPerMeter when `costKnown`.
};

struct EdgeBandingReport {
    std::vector<EdgeBandingRow> rows; ///< Used edge bands in model order, then missing ids sorted.
    core::Diagnostics diagnostics;
};

/// Negative option values are treated as 0.
EdgeBandingReport edgeBandingReport(const domain::Model& model, const EdgeBandingOptions& options = {},
                                    const Scope& scope = {});

// ----------------------------------------------------------------- hardware

struct HardwareRow {
    domain::HardwareId definitionId;
    std::string name, manufacturer, article; ///< Empty when `unknownDefinition`.
    domain::HardwareKind kind = domain::HardwareKind::Other;
    bool unknownDefinition = false;  ///< Definition id is not in `Model::hardwareCatalog`.
    bool dimensionsVerified = false; ///< Copied from the definition; false when unknown.
    int instanceCount = 0;           ///< Number of HardwareInstance records.
    int quantity = 0;                ///< Sum of instance quantities.
    /// Linear fittings (HardwareInstance::length > 0, e.g. wall rails, sliding-door tracks):
    /// number of pieces (sum of the quantities of instances with a length) and their total
    /// length in METRES (sum of length * quantity / 1000). Both 0 for non-linear fittings.
    int linearPieces = 0;
    double totalLengthM = 0;
    /// The definition is sold per metre (custom "unit" = "m", see isPricedPerMetre): unitCost is
    /// then a price PER METRE and totalCost = totalLengthM * unitCost. Such a row is priced only
    /// when every counted instance carries a length (otherwise costKnown = false and a
    /// production.hardware.missing_length warning names the instance).
    bool perMetre = false;
    double unitCost = 0;             ///< 0 = unknown. Per piece, or per metre when `perMetre`.
    bool costKnown = false;
    double totalCost = 0;
};

/// True when the definition's custom "unit" is "m": HardwareDefinition::unitCost is then a price
/// per metre of cut-to-length material, not per piece.
bool isPricedPerMetre(const domain::HardwareDefinition& definition);

struct HardwareReport {
    std::vector<HardwareRow> rows; ///< Used definitions in catalogue order, then unknown ids sorted.
    core::Diagnostics diagnostics;
};

/// Instances with quantity <= 0 are skipped with a diagnostic. Lengths of linear instances are
/// summed per definition (HardwareRow::linearPieces / totalLengthM); a negative or non-finite
/// length is ignored with a production.hardware.invalid_length warning.
HardwareReport hardwareReport(const domain::Model& model, const Scope& scope = {});

// ---------------------------------------------------------------- machining

struct HoleGroup {
    double diameter = 0; ///< mm.
    domain::Face face = domain::Face::A;
    int count = 0;
    int throughCount = 0; ///< How many of `count` are through holes.
};

struct MachiningRow {
    int partNumber = 0;
    domain::AssemblyId assemblyId;
    std::string assemblyName;
    domain::PartId partId;
    std::string partName;
    std::vector<HoleGroup> holes; ///< Sorted by diameter, then face (A, B, L1, L2, W1, W2).
    int holeCount = 0, grooveCount = 0, pocketCount = 0;
    double grooveLength = 0;      ///< Sum of groove centre-line lengths, mm.
    /// Contoured parts that are not plain rectangles (ADR-0008): contours routed on the CNC =
    /// the raw outline (domain::cutContour, before edge banding) + one per cut-out ...
    int routedContours = 0;
    /// ... and their total exact length (geometry::perimeter, arcs exact), mm.
    double routingLength = 0;
};

struct MachiningOptions {
    bool includePartsWithoutOperations = false;
    double diameterTolerance = core::kLengthEps; ///< Diameters this close share a group.
};

struct MachiningSummary {
    std::vector<MachiningRow> rows; ///< Part-number order.
    int totalHoles = 0, totalGrooves = 0, totalPockets = 0;
    int totalRoutedContours = 0;
    double totalRoutingLength = 0; ///< mm
    core::Diagnostics diagnostics;
};

/// Counts taken from `Part::operations`; operations are not validated here. Routed contoured
/// parts are listed even without operations; one whose raw outline cannot be computed gets
/// production.shape.invalid and no routing figures.
MachiningSummary machiningSummary(const domain::Model& model, const MachiningOptions& options = {},
                                  const Scope& scope = {});

// ------------------------------------------------------------------ summary

struct AssemblySummary {
    domain::AssemblyId id;
    std::string name;
    int partCount = 0;
    int hardwareQuantity = 0;  ///< Sum of positive instance quantities.
    int operationCount = 0;
    double finishedAreaM2 = 0;
    /// Size of `Assembly::bounds()` in the assembly frame (X width, Y depth, Z height), mm.
    /// All zero for an assembly without parts.
    double envelopeWidth = 0, envelopeDepth = 0, envelopeHeight = 0;
};

struct ProjectSummary {
    int assemblyCount = 0, partCount = 0;
    int materialCount = 0;   ///< Distinct material ids referenced by parts (incl. missing).
    int edgeBandCount = 0;   ///< Distinct edge band ids referenced by parts (incl. missing).
    int hardwareQuantity = 0, operationCount = 0;
    double finishedAreaM2 = 0, cutAreaM2 = 0, edgeBandingAppliedM = 0;
    std::vector<AssemblySummary> assemblies; ///< Model order.
};

ProjectSummary projectSummary(const domain::Model& model, const Scope& scope = {});

} // namespace fos::production
