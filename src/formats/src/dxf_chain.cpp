// SPDX-License-Identifier: AGPL-3.0-only
// DXF import: chaining loose pieces into loops and turning closed chains into contours.
#include "dxf_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <optional>
#include <utility>

namespace fos::formats::dxf {
namespace {

using geometry::Vec2;
using Cell = std::pair<long long, long long>;

constexpr std::size_t kMaxCandidatesScanned = 32; // per lookup; keeps degenerate files linear
constexpr std::size_t kMaxOpenReports = 50;

struct Endpoint {
    std::size_t chain;
    bool atEnd; ///< true: the chain's end point; false: its start point.
};

Vec2 startOf(const Chain& c) { return c.pieces.front().a; }
Vec2 endOf(const Chain& c) { return c.pieces.back().b; }

void reverseChain(Chain& c) {
    std::reverse(c.pieces.begin(), c.pieces.end());
    for (auto& p : c.pieces) {
        std::swap(p.a, p.b);
        p.bulge = -p.bulge;
    }
}

class EndpointGrid {
public:
    explicit EndpointGrid(double cell) : cell_(cell) {}

    Cell cellOf(Vec2 p) const {
        return {static_cast<long long>(std::floor(p.x / cell_)), static_cast<long long>(std::floor(p.y / cell_))};
    }
    void add(Vec2 p, Endpoint e) { cells_[cellOf(p)].push_back(e); }

    /// Nearest unused endpoint within `tol` of `p` (ties: lowest chain index, start before end).
    /// Used entries met on the way are pruned. `count` receives the number of matches seen.
    std::optional<Endpoint> nearest(Vec2 p, double tol, const std::vector<Chain>& chains, const std::vector<bool>& used,
                                    std::size_t& count) {
        std::optional<Endpoint> best;
        double bestD = 0;
        count = 0;
        const Cell c = cellOf(p);
        std::size_t scanned = 0;
        for (long long dx = -1; dx <= 1; ++dx)
            for (long long dy = -1; dy <= 1; ++dy) {
                auto it = cells_.find({c.first + dx, c.second + dy});
                if (it == cells_.end()) continue;
                auto& v = it->second;
                for (std::size_t k = 0; k < v.size() && scanned < kMaxCandidatesScanned;) {
                    if (used[v[k].chain]) {
                        v[k] = v.back();
                        v.pop_back();
                        continue;
                    }
                    ++scanned;
                    const Endpoint e = v[k++];
                    const Vec2 q = e.atEnd ? endOf(chains[e.chain]) : startOf(chains[e.chain]);
                    const double d = geometry::length(q - p);
                    if (d > tol) continue;
                    ++count;
                    const bool better = !best || d < bestD ||
                                        (d == bestD && (e.chain < best->chain ||
                                                        (e.chain == best->chain && !e.atEnd && best->atEnd)));
                    if (better) {
                        best = e;
                        bestD = d;
                    }
                }
            }
        return best;
    }

private:
    double cell_;
    std::map<Cell, std::vector<Endpoint>> cells_;
};

bool closes(const Chain& c, double tol) {
    if (geometry::length(endOf(c) - startOf(c)) > tol) return false;
    // A single straight piece cannot close; two pieces or one arc can (validated later).
    return c.pieces.size() >= 2 || c.pieces.front().bulge != 0.0;
}

/// Extends `cur` at its end as far as possible. Returns true when it closed.
bool extend(Chain& cur, double tol, const std::vector<Chain>& chains, std::vector<bool>& used, EndpointGrid& grid,
            core::Diagnostics& diags, bool& ambiguityReported) {
    while (true) {
        if (closes(cur, tol)) return true;
        std::size_t count = 0;
        const auto e = grid.nearest(endOf(cur), tol, chains, used, count);
        if (!e) return false;
        if (count > 1 && !ambiguityReported) {
            ambiguityReported = true;
            const Vec2 p = endOf(cur);
            diags.push_back({core::Severity::Warning, "formats.dxf.ambiguous_junction",
                             std::to_string(count + 1) + " outline ends meet at (" + num(p.x) + ", " + num(p.y) +
                                 ") (outline starting at line " + std::to_string(cur.line) +
                                 "); the nearest one was followed. Check the imported outline.",
                             {}});
        }
        used[e->chain] = true;
        Chain next = chains[e->chain];
        if (e->atEnd) reverseChain(next);
        cur.pieces.insert(cur.pieces.end(), next.pieces.begin(), next.pieces.end());
    }
}

bool collinearStraight(const Piece& p, const Piece& q) {
    if (p.bulge != 0.0 || q.bulge != 0.0) return false;
    const Vec2 u = p.b - p.a, v = q.b - q.a;
    const double lu = geometry::length(u), lv = geometry::length(v);
    if (lu <= 0 || lv <= 0) return false;
    return std::fabs(geometry::cross(u, v)) <= 1e-9 * lu * lv && geometry::dot(u, v) > 0;
}

} // namespace

std::string num(double v) {
    if (std::fabs(v) < 5e-5) v = 0;
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.4f", v);
    std::string s = buf;
    if (s.find('.') != std::string::npos) {
        while (s.back() == '0') s.pop_back();
        if (s.back() == '.') s.pop_back();
    }
    return s;
}

void chainPieces(std::vector<Chain> open, double tolerance, std::vector<Chain>& closed, core::Diagnostics& diags) {
    EndpointGrid grid(tolerance);
    for (std::size_t i = 0; i < open.size(); ++i) {
        grid.add(startOf(open[i]), {i, false});
        grid.add(endOf(open[i]), {i, true});
    }
    std::vector<bool> used(open.size(), false);
    std::size_t openCount = 0;
    for (std::size_t i = 0; i < open.size(); ++i) {
        if (used[i]) continue;
        used[i] = true;
        Chain cur = open[i];
        bool ambiguity = false;
        bool done = extend(cur, tolerance, open, used, grid, diags, ambiguity);
        if (!done) {
            reverseChain(cur);
            done = extend(cur, tolerance, open, used, grid, diags, ambiguity);
            reverseChain(cur);
        }
        if (done) {
            closed.push_back(std::move(cur));
            continue;
        }
        if (++openCount > kMaxOpenReports) continue;
        const Vec2 a = startOf(cur), b = endOf(cur);
        diags.push_back({core::Severity::Warning, "formats.dxf.open_chain",
                         "Open outline starting at line " + std::to_string(cur.line) + " (layer '" + cur.layer +
                             "'): " + std::to_string(cur.pieces.size()) + " piece(s) from (" + num(a.x) + ", " +
                             num(a.y) + ") to (" + num(b.x) + ", " + num(b.y) + "), gap " +
                             num(geometry::length(b - a)) +
                             " mm; not imported. Close it in the CAD program or raise the chain tolerance.",
                         {}});
    }
    if (openCount > kMaxOpenReports)
        diags.push_back({core::Severity::Warning, "formats.dxf.open_chain",
                         std::to_string(openCount - kMaxOpenReports) + " further open outline(s) not imported.", {}});
}

bool makeLoop(const Chain& chain, std::size_t maxVertices, DxfLoop& out, core::Diagnostics& diags) {
    std::vector<Piece> merged;
    for (const Piece& p : chain.pieces) {
        if (!merged.empty() && collinearStraight(merged.back(), p)) merged.back().b = p.b;
        else merged.push_back(p);
    }
    if (merged.size() >= 2 && collinearStraight(merged.back(), merged.front())) {
        merged.front().a = merged.back().a;
        merged.pop_back();
    }
    const std::string where = "Closed outline at line " + std::to_string(chain.line) + " (layer '" + chain.layer + "')";
    if (merged.size() > maxVertices) {
        diags.push_back({core::Severity::Warning, "formats.dxf.loop_too_large",
                         where + " has " + std::to_string(merged.size()) + " segments, more than the limit of " +
                             std::to_string(maxVertices) + "; not imported.",
                         {}});
        return false;
    }
    geometry::Contour c;
    c.vertices.reserve(merged.size());
    for (const Piece& p : merged) c.vertices.push_back({p.a, p.bulge});
    if (auto v = geometry::validateContour(c); !v) {
        diags.push_back({core::Severity::Warning, "formats.dxf.invalid_loop",
                         where + " is not a simple closed contour (" + v.error().message + "); not imported.", {}});
        return false;
    }
    out.contour = geometry::normalizedCcw(c);
    out.line = chain.line;
    out.layer = chain.layer;
    return true;
}

} // namespace fos::formats::dxf
