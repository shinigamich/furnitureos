// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Internal pieces of the DXF outline import (not installed).
#include "fos/core/diagnostic.hpp"
#include "fos/formats/dxf_import.hpp"
#include "fos/geometry/vec.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace fos::formats::dxf {

/// One straight or circular piece: a -> b, bulge = tan(sweep / 4), |bulge| <= 1.
struct Piece {
    geometry::Vec2 a, b;
    double bulge = 0.0;
};

/// A connected run of pieces (end of piece i = start of piece i + 1) from one or more entities.
struct Chain {
    std::vector<Piece> pieces;
    std::size_t line = 0; ///< 1-based line of the first entity.
    std::string layer;
};

/// Short number text for messages (at most 4 decimals, no trailing zeros).
std::string num(double v);

/// Chains `open` (each non-empty) by endpoint matching within `tolerance`; closed results are
/// appended to `closed` in the order of their seed chain, open results are reported in `diags`.
void chainPieces(std::vector<Chain> open, double tolerance, std::vector<Chain>& closed, core::Diagnostics& diags);

/// Turns a closed chain into a loop: collinear straight pieces merged, CCW. Reports and returns
/// false when the loop exceeds `maxVertices` or is not a valid simple contour.
bool makeLoop(const Chain& chain, std::size_t maxVertices, DxfLoop& out, core::Diagnostics& diags);

} // namespace fos::formats::dxf
