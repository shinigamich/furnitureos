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
    double finishedAreaM2 = 0; ///< Sum of length * width.
    double cutAreaM2 = 0;      ///< Sum of cutLength * cutWidth.
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

/// Per-material totals. Only materials referenced by at least one part appear.
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
    int edgeCount = 0;
    double appliedLengthM = 0;     ///< Sum of domain::edgeLength of the banded edges.
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
    double unitCost = 0;             ///< 0 = unknown.
    bool costKnown = false;
    double totalCost = 0;
};

struct HardwareReport {
    std::vector<HardwareRow> rows; ///< Used definitions in catalogue order, then unknown ids sorted.
    core::Diagnostics diagnostics;
};

/// Instances with quantity <= 0 are skipped with a diagnostic.
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
};

struct MachiningOptions {
    bool includePartsWithoutOperations = false;
    double diameterTolerance = core::kLengthEps; ///< Diameters this close share a group.
};

struct MachiningSummary {
    std::vector<MachiningRow> rows; ///< Part-number order.
    int totalHoles = 0, totalGrooves = 0, totalPockets = 0;
    core::Diagnostics diagnostics;
};

/// Counts taken from `Part::operations`; operations are not validated here.
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
