// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Internal: SVG numbers, transforms and path data -> straight / circular pieces (not installed).
// Error messages returned here carry no "SVG line N:" prefix; the caller adds it.
#include "dxf_internal.hpp"

#include "fos/core/result.hpp"
#include "fos/geometry/vec.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace fos::formats::svg {

using dxf::Piece;
using geometry::Vec2;

/// 2D affine map x' = a x + c y + e, y' = b x + d y + f (SVG matrix(a b c d e f) order).
struct Affine {
    double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
    Vec2 apply(Vec2 p) const { return {a * p.x + c * p.y + e, b * p.x + d * p.y + f}; }
    Vec2 linear(Vec2 v) const { return {a * v.x + c * v.y, b * v.x + d * v.y}; }
    double det() const { return a * d - b * c; }
};

/// l * r: first r, then l.
Affine operator*(const Affine& l, const Affine& r);

/// Status of scanNumber.
enum class NumberScan { Ok, None, OutOfRange };

/// SVG number grammar ([+-] digits [. digits] [e[+-]digits], ".5" allowed) at s[i]; advances i.
NumberScan scanNumber(std::string_view s, std::size_t& i, double& out);

/// Skips white space and at most one comma.
void skipSeparators(std::string_view s, std::size_t& i);

/// Whole-string list of numbers separated by white space and/or commas.
core::Result<std::vector<double>> parseNumberList(std::string_view s, std::string_view what);

/// A `transform` attribute (translate, scale, rotate, matrix; skewX / skewY refused).
core::Result<Affine> parseTransform(std::string_view s);

/// Converts local (user space) primitives into final pieces (mm, panel orientation) under `m`.
/// Pieces shorter than 1e-9 mm are dropped. Arcs of more than 180 degrees are split in two.
class PieceEmitter {
public:
    PieceEmitter(const Affine& m, double maxCoordinate) : m_(m), max_(maxCoordinate) {}

    /// Final position of local point `p`; error when it is not finite or beyond the limit.
    core::Result<Vec2> point(Vec2 p) const;
    void line(Vec2 finalA, Vec2 finalB, std::vector<Piece>& out) const;
    /// SVG elliptical-arc endpoint parameters (local radii, x-axis rotation in degrees);
    /// `a`/`b` are FINAL points. Refused when the ellipse is not a circle after the transform.
    core::Result<void> arc(Vec2 finalA, double rx, double ry, double rotationDeg, bool largeArc, bool sweep,
                           Vec2 finalB, std::vector<Piece>& out) const;
    /// True when the local ellipse (rx, ry, rotation) maps to a circle; `radius` = its radius (mm).
    bool circular(double rx, double ry, double rotationDeg, double& radius) const;
    double det() const { return m_.det(); }

private:
    Affine m_;
    double max_;
};

struct SubPath {
    std::vector<Piece> pieces;
    bool closed = false;
};

/// Parses path data `d` (M L H V A Z, absolute and relative). Bezier commands (C S Q T) are
/// refused (Unsupported). `maxPieces` caps the pieces of this path.
core::Result<std::vector<SubPath>> parsePathData(std::string_view d, const PieceEmitter& emit, std::size_t maxPieces);

/// Exact cos / sin of an angle in degrees (multiples of 90 give exact 0 / +-1).
void cosSinDeg(double deg, double& c, double& s);

} // namespace fos::formats::svg
