// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/domain/operation.hpp"

#include <string>
#include <vector>

/// Drilling pattern generators. They only generate; they do not check panel bounds
/// (run `validateOperations` on the part afterwards). All coordinates are face
/// coordinates (mm) as defined in `fos/domain/operation.hpp`.
///
/// IDS: every generator numbers its operations 1..n in generation order and names them
/// `<idPrefix>.<n>` (e.g. prefix "cab1/side.left/op.row" -> ".../op.row.1"). The same
/// input always yields the same ids. `source` is copied to Operation::source.
///
/// INVALID INPUT (count <= 0, pitch <= 0, empty range) yields an empty vector.
namespace fos::machining {

/// What to drill at every pattern position.
struct HoleSpec {
    domain::Face face = domain::Face::A;
    double diameter = 5.0;
    double depth = 13.0; ///< Ignored when `through`.
    bool through = false;
};

/// `count` holes: start, start + pitch, start + 2*pitch, ... (`pitch` is a (du, dv) vector).
std::vector<domain::Operation> lineOfHoles(const HoleSpec& spec, geometry::Vec2 start, geometry::Vec2 pitch,
                                           int count, const std::string& idPrefix,
                                           const std::string& source = "user");

enum class RowDirection { AlongU, AlongV };

/// A row of holes on the 32 mm system grid.
struct System32Row {
    HoleSpec hole;                               ///< Default 5 mm x 13 mm.
    RowDirection direction = RowDirection::AlongU;
    /// Position of the row on the other face axis (v when the row runs along u), i.e. the
    /// distance of the row from the edge at coordinate 0 (e.g. 37 mm from the front edge).
    double rowOffset = 37.0;
    /// Coordinate (along the row) of the grid origin: holes can only sit at
    /// firstHoleOffset + k * pitch, k = 0, 1, 2, ...
    double firstHoleOffset = 0.0;
    /// Usable range along the row; grid positions outside [rangeMin, rangeMax] are skipped.
    double rangeMin = 0.0;
    double rangeMax = 0.0;
    double pitch = 32.0;
};
std::vector<domain::Operation> system32Row(const System32Row& row, const std::string& idPrefix,
                                           const std::string& source = "user");

/// countU x countV holes at start + (i*pitchU, j*pitchV); generated row by row
/// (j outer, i inner), i.e. ids run along u first.
std::vector<domain::Operation> rectangularGrid(const HoleSpec& spec, geometry::Vec2 start, double pitchU,
                                               double pitchV, int countU, int countV, const std::string& idPrefix,
                                               const std::string& source = "user");

enum class MirrorLine {
    ConstantU, ///< Mirror across the line u = position (u' = 2*position - u, v unchanged).
    ConstantV, ///< Mirror across the line v = position (v' = 2*position - v, u unchanged).
};

/// For every position: a hole at the position followed by its mirror image across the
/// given line (ids 2k-1 and 2k for the k-th position). A position lying on the mirror
/// line yields a single hole. Typical use: symmetric fittings about a panel centre line.
std::vector<domain::Operation> mirroredPairs(const HoleSpec& spec, const std::vector<geometry::Vec2>& positions,
                                             MirrorLine line, double position, const std::string& idPrefix,
                                             const std::string& source = "user");

} // namespace fos::machining
