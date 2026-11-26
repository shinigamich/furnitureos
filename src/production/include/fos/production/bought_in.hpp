// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// BOUGHT-IN ITEMS list: parts that are ordered finished to size from a supplier instead of
/// being made in the workshop (domain::isBoughtIn; currently the glass panes of glass-panel
/// frame fronts, Part::custom "furniture.glass" = "true"). These parts are left out of the cut
/// list, the sheet material totals, the edge-banding reports / jobs / roll plans and the
/// machining summary (each such output adds one Info production.bought_in.excluded with their
/// count) and appear here instead. Glazing beads are normal parts (cut from the front material).
/// Pure function of a read-only model; lengths in mm, areas in m2.
#include "fos/production/common.hpp"

#include <string>
#include <vector>

namespace fos::production {

/// Item kind printed in the list (the only kind so far).
inline constexpr const char* kGlassPaneKind = "Glass pane";

/// A group of identical bought-in items.
struct BoughtInRow {
    std::string kind = kGlassPaneKind;
    domain::MaterialId materialId;
    std::string materialName; ///< Empty when `materialMissing`.
    bool materialMissing = false;
    /// Size to order, of the FIRST member: width = Part::length, height = Part::width, thickness =
    /// Part::thickness (finished size; a pane has no edge banding). mm.
    double width = 0, height = 0, thickness = 0;
    int quantity = 0;
    double areaEachM2 = 0;  ///< width * height / 1e6 (0 for a non-positive size).
    double totalAreaM2 = 0; ///< quantity * areaEachM2.
    std::vector<domain::PartId> partIds; ///< Part-number order; size == quantity.
    std::vector<int> partNumbers;        ///< PartRow::number of each member.
    std::vector<std::string> partNames;  ///< Distinct names, first-seen order.
};

struct BoughtInList {
    std::vector<BoughtInRow> rows;
    int itemCount = 0;        ///< Sum of quantities.
    double totalAreaM2 = 0;   ///< Sum of totalAreaM2.
    /// production.scope.unknown_assembly, production.material.missing (Warning, per part),
    /// production.part.invalid_size (Warning, per part).
    core::Diagnostics diagnostics;
};

/// Bought-in items of the model (or scope). Parts merge when material id and thickness match and
/// their width x height match within core::kLengthEps in EITHER orientation (a pane has no grain:
/// 400 x 600 and 600 x 400 are the same pane; the row shows the first member's orientation).
/// Sorted by material name, material id, thickness, descending larger side, descending smaller
/// side, then lowest part number.
BoughtInList boughtInList(const domain::Model& model, const Scope& scope = {});

/// Display table: Item, Material, Width, Height, Thickness, Qty, Area each (m2), Total area (m2),
/// Name, Part numbers. English captions (as the default Localisation of tables.hpp); numbers via
/// core::formatLength (lengths with core::kDisplayDecimals, areas with 4 decimals).
Table boughtInTable(const BoughtInList& list);

} // namespace fos::production
