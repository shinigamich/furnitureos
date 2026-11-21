// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Edge-band roll planning: one fos::linear job per edge band, whose pieces are the band strips
/// to apply and whose stock is rolls of tape. Available only when the `linear` module is built
/// (target fos::production links fos::linear then).
#include "fos/linear/types.hpp"
#include "fos/production/reports.hpp"

namespace fos::production {

struct EdgeBandRollOptions {
    /// Only `overhangPerEnd` is used (added at each strip end); `wastePercent` is NOT applied:
    /// the roll plan replaces the percentage allowance.
    EdgeBandingOptions edgeBanding;
    double rollLength = 100000.0; ///< mm (100 m), unlimited number of rolls. Must be > 0.
    double kerf = 0.0;            ///< Tape is cut with a knife: 0 by default.
    double minReusableOffcut = 0; ///< Roll remainders at least this long are reusable (0 = none).
    Scope scope;
};

/// The linear job of one edge band.
struct EdgeBandStripJob {
    domain::EdgeBandId edgeBandId;
    std::string name;     ///< Band name; empty when `missing`.
    bool missing = false; ///< Referenced by parts but absent from the model.
    int stripCount = 0;
    double appliedLength = 0; ///< mm, sum of the applied strip lengths (without overhang).
    /// Pieces: one per strip, id "<part id>/<side>" (rectangle edges) or "<part id>/strip<n>"
    /// (contoured outline strips, 1-based in outline order), label = part name, length =
    /// applied length + 2 * overhangPerEnd, quantity 1. Stock: one unlimited roll type "roll"
    /// of `rollLength`, cost = costPerMeter * rollLength / 1000 (0 when unknown).
    linear::LinearJob job;
};

/// One job per used edge band (model order, then missing ids sorted) with the same strip
/// definition as edgeBandingReport(): rectangle edges via domain::edgeLength, contoured outline
/// runs (tangent-continuous banded segments with one band) as single strips. Strips with a
/// non-positive length are skipped. Negative or non-finite overhang is treated as 0; a
/// non-positive or non-finite roll length yields an empty vector.
std::vector<EdgeBandStripJob> edgeBandStripJobs(const domain::Model& model, const EdgeBandRollOptions& options = {});

} // namespace fos::production
