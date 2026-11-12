// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Internal: sparse heightmap used by the simulator.
#include "fos/geometry/vec.hpp"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace fos::cam::detail {

/// Square cells of size h over the stock rectangle [x0, x0 + nx*h] x [y0, y0 + ny*h]; a cell is
/// represented by its centre. Each touched cell stores the lowest tool-tip Z that reached it.
class Heightmap {
public:
    struct Cell {
        double minZ;
        std::size_t motion; ///< index of the motion that cut deepest
    };
    Heightmap(double x0, double y0, double lx, double ly, double h, double topZ);

    /// Sweeps a flat-bottomed cylinder of radius r with its tip moving linearly a -> b.
    void cut(geometry::Vec3 a, geometry::Vec3 b, double r, std::size_t motion);
    /// Lowest Z at cell (i, j); +inf when untouched.
    double minZ(int i, int j) const;
    const Cell* cell(int i, int j) const;
    geometry::Vec2 centre(int i, int j) const noexcept { return {x0_ + (i + 0.5) * h_, y0_ + (j + 0.5) * h_}; }
    /// Index range of cells whose centre may lie in [lo, hi] (clamped; empty when lo > hi).
    void range(double lo, double hi, bool xAxis, int& first, int& last) const noexcept;
    /// Touched cells with minZ below the stock top, sorted by (j, i).
    std::vector<std::pair<int, int>> removedCells(double eps) const;
    double topZ() const noexcept { return top_; }

private:
    static std::int64_t key(int i, int j) noexcept { return (static_cast<std::int64_t>(j) << 32) | static_cast<std::uint32_t>(i); }
    double x0_, y0_, h_, top_;
    int nx_, ny_;
    std::unordered_map<std::int64_t, Cell> cells_;
};

} // namespace fos::cam::detail
