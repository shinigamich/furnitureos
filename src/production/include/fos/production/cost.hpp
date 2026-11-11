// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/production/reports.hpp"

#include <map>

namespace fos::production {

struct CostOptions {
    std::string currency; ///< Label only (e.g. "UAH", "EUR"); no conversion is performed.
    /// Real sheet counts per material, e.g. from the cutting optimiser. Materials not
    /// listed fall back to the area lower bound.
    std::map<domain::MaterialId, int> sheetCounts;
    EdgeBandingOptions edgeBanding;
};

enum class SheetCountSource {
    AreaLowerBound, ///< ceil(cut area / sheet area): the cost is a LOWER BOUND.
    Supplied,       ///< Taken from CostOptions::sheetCounts.
    Unavailable     ///< Material missing or without a valid sheet size, and none supplied.
};
const char* toString(SheetCountSource s) noexcept;

struct MaterialCostLine {
    domain::MaterialId materialId;
    std::string name;
    bool missing = false;
    int sheets = 0;
    SheetCountSource source = SheetCountSource::Unavailable;
    double costPerSheet = 0;
    bool costKnown = false; ///< False -> `cost` is 0 and the totals are incomplete.
    double cost = 0;
};
struct EdgeBandCostLine {
    domain::EdgeBandId edgeBandId;
    std::string name;
    bool missing = false;
    double purchaseLengthM = 0, costPerMeter = 0;
    bool costKnown = false;
    double cost = 0;
};
struct HardwareCostLine {
    domain::HardwareId definitionId;
    std::string name;
    bool unknownDefinition = false;
    int quantity = 0;
    double unitCost = 0;
    bool costKnown = false;
    double cost = 0;
};

/// Material + edge banding + hardware cost. Labour, machining time, delivery and
/// taxes are not part of this estimate.
struct CostEstimate {
    std::string currency;
    std::vector<MaterialCostLine> materials;
    std::vector<EdgeBandCostLine> edgeBanding;
    std::vector<HardwareCostLine> hardware;
    double materialsTotal = 0, edgeBandingTotal = 0, hardwareTotal = 0, total = 0;
    /// True when at least one material line uses the area lower bound: `materialsTotal`
    /// and `total` are then lower bounds, not quotes.
    bool materialsAtAreaLowerBound = false;
    /// False when any line has an unknown price (cost 0 / missing reference); the
    /// totals then cover only the lines with known prices.
    bool complete = true;
    core::Diagnostics diagnostics;
};

CostEstimate costEstimate(const domain::Model& model, const CostOptions& options = {}, const Scope& scope = {});

} // namespace fos::production
