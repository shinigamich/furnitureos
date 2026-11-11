// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/diagnostic.hpp"
#include "fos/domain/model.hpp"

#include <optional>
#include <string>
#include <vector>

/// Production documentation derived on demand from a read-only `domain::Model`.
///
/// Every generator in this module is a pure function of its arguments: nothing is
/// cached or stored, and the output order is deterministic (model order, never hash
/// or address order). All lengths are millimetres unless a field name ends in `M`
/// (metres) or `M2` (square metres). Cut sizes come exclusively from
/// `domain::cutSize()`; edge lengths from `domain::edgeLength()`.
///
/// Bad references (missing material / edge band / hardware definition) never abort a
/// report: the affected row is flagged and a `core::Diagnostic` is added.
/// Diagnostic codes used by this module:
///   production.scope.unknown_assembly      production.part.invalid_size
///   production.material.missing            production.material.invalid_sheet
///   production.edgeband.missing            production.hardware.unknown_definition
///   production.hardware.invalid_quantity   production.cost.sheet_count_below_bound
namespace fos::production {

/// Restricts a report to one assembly. Default: the whole model.
/// An id that does not exist yields an empty report plus a diagnostic.
struct Scope {
    std::optional<domain::AssemblyId> assembly;
};

/// Edge banding on one side of one part, resolved against the model.
struct EdgeCell {
    std::optional<domain::EdgeBandId> id; ///< Empty = raw edge.
    std::string name;       ///< Edge band name; the raw id when `missing`.
    double thickness = 0.0; ///< mm; 0 for raw edges and missing references (as in domain::cutSize).
    bool missing = false;   ///< Referenced edge band is not in the model.

    bool banded() const noexcept { return id.has_value(); }
};

/// Generic rectangular table of display strings; input of the CSV and HTML writers.
struct Table {
    std::vector<std::string> header;
    std::vector<std::vector<std::string>> rows;
};

} // namespace fos::production
