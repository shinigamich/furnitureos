// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/domain/model.hpp"

#include <string>
#include <vector>

/// Plain drilling report data (no formatting). Lengths in mm, face coordinates as in
/// `fos/domain/operation.hpp`. Only Hole operations appear here.
namespace fos::machining {

struct DrillingRow {
    domain::AssemblyId assemblyId; ///< Empty for rows produced from a bare part.
    domain::PartId partId;
    std::string partName;
    domain::OperationId operationId;
    domain::Face face = domain::Face::A;
    double u = 0;
    double v = 0;
    double diameter = 0;
    double depth = 0;     ///< Drilled depth; for a through hole the material depth behind the face.
    bool through = false;
    std::string source;   ///< Operation::source ("user" or generating hardware instance / rule).
};

/// Holes of one part sorted by face (A,B,L1,L2,W1,W2), then u, v, diameter, depth, operation id.
std::vector<DrillingRow> drillingRows(const domain::Part& part);
/// Holes of the whole model: assemblies and parts in model order, rows per part as above.
std::vector<DrillingRow> drillingRows(const domain::Model& model);

struct DrillSummaryRow {
    double diameter = 0;
    int holeCount = 0;    ///< All holes of this diameter (blind + through).
    int throughCount = 0; ///< How many of them are through holes.
    double maxBlindDepth = 0; ///< Deepest blind hole of this diameter, 0 when all are through.
};

/// Distinct drill diameters of a model (diameters within core::kLengthEps are merged),
/// ascending by diameter.
std::vector<DrillSummaryRow> drillSummary(const domain::Model& model);

} // namespace fos::machining
