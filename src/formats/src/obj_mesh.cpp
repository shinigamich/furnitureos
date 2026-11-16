// SPDX-License-Identifier: AGPL-3.0-only
// Ear-clipping triangulation of a polygon with holes (bridge method), for OBJ cap faces.
#include "obj_mesh.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace fos::formats::detail {
namespace {

using geometry::Vec2;

core::Error meshError(const std::string& message) {
    return core::makeError(core::ErrorCode::InvalidArgument, "Cap triangulation: " + message);
}

struct Node {
    Vec2 p;
    std::uint32_t index = 0; ///< Into CapTriangulation::points.
    std::size_t prev = 0, next = 0;
    bool shared = false; ///< Bridge end: another node has the same position.
};

/// > 0 when a -> b -> c turns left (b is a convex corner of a counter-clockwise ring).
double turn(Vec2 a, Vec2 b, Vec2 c) { return cross(b - a, c - b); }

/// Direction d lies strictly inside the cone swept counter-clockwise from u to v (< 180 deg).
bool insideCone(Vec2 u, Vec2 v, Vec2 d) { return cross(u, d) > 0 && cross(d, v) > 0; }

/// Inclusive point-in-triangle for a counter-clockwise triangle.
bool inTriangleCcw(Vec2 a, Vec2 b, Vec2 c, Vec2 p) {
    return cross(b - a, p - a) >= 0 && cross(c - b, p - b) >= 0 && cross(a - c, p - c) >= 0;
}

/// Inclusive point-in-triangle for either orientation.
bool inTriangle(Vec2 a, Vec2 b, Vec2 c, Vec2 p) {
    return turn(a, b, c) >= 0 ? inTriangleCcw(a, b, c, p) : inTriangleCcw(a, c, b, p);
}

class Ring {
public:
    std::vector<Node> nodes;

    /// Appends a closed ring of points (indices first..first+count-1); returns its first node.
    std::size_t addRing(const std::vector<Vec2>& points, std::size_t first, std::size_t count) {
        const std::size_t base = nodes.size();
        for (std::size_t i = 0; i < count; ++i) {
            Node n;
            n.p = points[first + i];
            n.index = static_cast<std::uint32_t>(first + i);
            n.prev = base + (i + count - 1) % count;
            n.next = base + (i + 1) % count;
            nodes.push_back(n);
        }
        return base;
    }

    Vec2 at(std::size_t i) const { return nodes[i].p; }
    Vec2 prevOf(std::size_t i) const { return nodes[nodes[i].prev].p; }
    Vec2 nextOf(std::size_t i) const { return nodes[nodes[i].next].p; }

    /// Direction m - p points into the region at ring node q (interior is to the left).
    bool locallyInside(std::size_t q, Vec2 m) const {
        const Vec2 a = prevOf(q), p = at(q), b = nextOf(q), d = m - p;
        if (turn(a, p, b) > 0) return cross(b - p, d) > 0 && cross(d, a - p) > 0;
        return !(cross(a - p, d) >= 0 && cross(d, b - p) >= 0); // not in the (convex) exterior wedge
    }

    /// Joins the hole ring starting at node `holeStart` to the main ring through `start`.
    bool bridge(std::size_t start, std::size_t holeStart) {
        // M: rightmost hole vertex (ties: the first found).
        std::size_t m = holeStart;
        for (std::size_t q = nodes[holeStart].next; q != holeStart; q = nodes[q].next)
            if (at(q).x > at(m).x) m = q;
        const Vec2 M = at(m);

        // Ray M + t (1, 0): nearest crossing with an edge whose interior side faces M. With the
        // interior on the left, those edges run upwards (a.y < b.y).
        double bestX = std::numeric_limits<double>::infinity();
        std::size_t edgeA = 0;
        bool found = false;
        std::size_t q = start;
        do {
            const std::size_t r = nodes[q].next;
            const Vec2 a = at(q), b = at(r);
            if (a.y < b.y && a.y <= M.y && M.y <= b.y) {
                const double x = a.x + (M.y - a.y) * (b.x - a.x) / (b.y - a.y);
                if (x > M.x && x < bestX) {
                    bestX = x;
                    edgeA = q;
                    found = true;
                }
            }
            q = r;
        } while (q != start);
        if (!found) return false;
        const std::size_t edgeB = nodes[edgeA].next;
        const Vec2 I{bestX, M.y};
        std::size_t p;
        if (at(edgeA).y == M.y) p = edgeA;
        else if (at(edgeB).y == M.y) p = edgeB;
        else p = at(edgeA).x > at(edgeB).x ? edgeA : edgeB;

        // A vertex inside triangle (M, I, P) may hide P; take the one with the smallest angle to
        // the ray (ties: the nearest) among those that see M from inside the region.
        if (!(at(p) == I)) {
            const Vec2 P = at(p);
            double bestTan = std::abs(P.y - M.y) / (P.x - M.x);
            double bestDist = length(P - M);
            q = start;
            do {
                const Vec2 c = at(q);
                if (q != p && c.x > M.x && inTriangle(M, I, P, c) && locallyInside(q, M)) {
                    const double tan = std::abs(c.y - M.y) / (c.x - M.x);
                    const double dist = length(c - M);
                    if (tan < bestTan || (tan == bestTan && dist < bestDist)) {
                        p = q;
                        bestTan = tan;
                        bestDist = dist;
                    }
                }
                q = nodes[q].next;
            } while (q != start);
        }
        // Several nodes may share P's position (earlier bridges): use one whose corner sees M.
        if (!locallyInside(p, M)) {
            q = start;
            do {
                if (at(q) == at(p) && locallyInside(q, M)) {
                    p = q;
                    break;
                }
                q = nodes[q].next;
            } while (q != start);
        }

        // Splice: ... P -> M -> hole ... -> M' -> P' -> (old P.next) ...
        const std::size_t m2 = nodes.size();
        nodes.push_back(nodes[m]);
        const std::size_t p2 = nodes.size();
        nodes.push_back(nodes[p]);
        const std::size_t holePrev = nodes[m].prev, pNext = nodes[p].next;
        nodes[p].next = m;
        nodes[m].prev = p;
        nodes[holePrev].next = m2;
        nodes[m2].prev = holePrev;
        nodes[m2].next = p2;
        nodes[p2].prev = m2;
        nodes[p2].next = pNext;
        nodes[pNext].prev = p2;
        for (const std::size_t s : {p, p2, m, m2}) nodes[s].shared = true;
        return true;
    }

    bool isEar(std::size_t b) const {
        const std::size_t ia = nodes[b].prev, ic = nodes[b].next;
        const Vec2 A = at(ia), B = at(b), C = at(ic);
        if (!(turn(A, B, C) > 0)) return false;
        const double minX = std::min({A.x, B.x, C.x}), maxX = std::max({A.x, B.x, C.x});
        const double minY = std::min({A.y, B.y, C.y}), maxY = std::max({A.y, B.y, C.y});
        for (std::size_t q = nodes[ic].next; q != ia; q = nodes[q].next) {
            const Vec2 P = at(q);
            if (P.x < minX || P.x > maxX || P.y < minY || P.y > maxY) continue;
            // Only reflex/straight corners and bridge ends can obstruct (Eberly).
            if (!nodes[q].shared && turn(prevOf(q), P, nextOf(q)) > 0) continue;
            const Vec2 d1 = prevOf(q) - P, d2 = nextOf(q) - P;
            // A node on a triangle corner (bridge duplicate) obstructs when one of its edges
            // enters the triangle.
            if (P == A) {
                if (insideCone(B - A, C - A, d1) || insideCone(B - A, C - A, d2)) return false;
            } else if (P == B) {
                if (insideCone(C - B, A - B, d1) || insideCone(C - B, A - B, d2)) return false;
            } else if (P == C) {
                if (insideCone(A - C, B - C, d1) || insideCone(A - C, B - C, d2)) return false;
            } else if (inTriangleCcw(A, B, C, P)) {
                return false;
            }
        }
        return true;
    }

    void unlink(std::size_t b) {
        nodes[nodes[b].prev].next = nodes[b].next;
        nodes[nodes[b].next].prev = nodes[b].prev;
    }
};

double ringArea(const std::vector<Vec2>& pts, std::size_t first, std::size_t count) {
    double twice = 0;
    for (std::size_t i = 0; i < count; ++i) twice += cross(pts[first + i], pts[first + (i + 1) % count]);
    return twice / 2.0;
}

} // namespace

core::Result<CapTriangulation> triangulateWithHoles(const geometry::Polygon2& outer,
                                                    const std::vector<geometry::Polygon2>& holes) {
    CapTriangulation out;
    std::size_t total = outer.size();
    for (const auto& h : holes) total += h.size();
    if (total > kMaxCapPoints)
        return meshError("the region has " + std::to_string(total) + " points; at most " +
                         std::to_string(kMaxCapPoints) + " are supported");

    // Rings: outer counter-clockwise, holes clockwise.
    double expected = 0;
    for (std::size_t r = 0; r <= holes.size(); ++r) {
        const geometry::Polygon2& ring = r == 0 ? outer : holes[r - 1];
        const std::string name = r == 0 ? std::string("the outer ring") : "hole " + std::to_string(r - 1);
        if (ring.size() < 3) return meshError(name + " has fewer than 3 points");
        const std::size_t first = out.points.size();
        out.ringStart.push_back(first);
        out.points.insert(out.points.end(), ring.vertices.begin(), ring.vertices.end());
        const double area = ringArea(out.points, first, ring.size());
        if (!(std::abs(area) > 0) || !std::isfinite(area)) return meshError(name + " has zero or non-finite area");
        if ((r == 0) != (area > 0)) std::reverse(out.points.begin() + static_cast<std::ptrdiff_t>(first), out.points.end());
        expected += r == 0 ? std::abs(area) : -std::abs(area);
    }
    out.ringStart.push_back(out.points.size());

    Ring ring;
    ring.nodes.reserve(total + 2 * holes.size());
    const std::size_t start = ring.addRing(out.points, 0, outer.size());
    // Holes in descending order of their rightmost x (ties: input order).
    std::vector<std::pair<double, std::size_t>> order;
    std::vector<std::size_t> holeStart(holes.size());
    for (std::size_t h = 0; h < holes.size(); ++h) {
        const std::size_t first = out.ringStart[h + 1], count = out.ringStart[h + 2] - first;
        holeStart[h] = ring.addRing(out.points, first, count);
        double maxX = -std::numeric_limits<double>::infinity();
        for (std::size_t i = 0; i < count; ++i) maxX = std::max(maxX, out.points[first + i].x);
        order.emplace_back(-maxX, h);
    }
    std::stable_sort(order.begin(), order.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [key, h] : order)
        if (!ring.bridge(start, holeStart[h]))
            return meshError("hole " + std::to_string(h) + " could not be joined to the outer ring (is it inside?)");

    // Ear clipping.
    std::size_t remaining = total + 2 * holes.size();
    std::size_t cur = start, stall = 0;
    while (remaining > 3) {
        if (ring.isEar(cur)) {
            const Node& b = ring.nodes[cur];
            out.triangles.push_back({ring.nodes[b.prev].index, b.index, ring.nodes[b.next].index});
            const std::size_t next = b.next;
            ring.unlink(cur);
            --remaining;
            cur = next;
            stall = 0;
            continue;
        }
        cur = ring.nodes[cur].next;
        if (++stall < remaining) continue;
        // A full pass without an ear: drop one zero-area corner (collinear or spike), which
        // removes no area, and retry.
        bool dropped = false;
        std::size_t q = cur;
        for (std::size_t i = 0; i < remaining && !dropped; ++i, q = ring.nodes[q].next) {
            const Vec2 a = ring.prevOf(q), p = ring.at(q), c = ring.nextOf(q);
            if (std::abs(turn(a, p, c)) <= 1e-12 * length(p - a) * length(c - p)) {
                cur = ring.nodes[q].next;
                ring.unlink(q);
                --remaining;
                dropped = true;
            }
        }
        if (!dropped) return meshError("no ear found with " + std::to_string(remaining) + " corners left");
        stall = 0;
    }
    {
        const Node& b = ring.nodes[cur];
        if (turn(ring.prevOf(cur), b.p, ring.nextOf(cur)) > 0)
            out.triangles.push_back({ring.nodes[b.prev].index, b.index, ring.nodes[b.next].index});
    }

    // Safety net: the triangles must tile the region exactly once.
    double sum = 0;
    for (const auto& t : out.triangles) {
        const double twice = turn(out.points[t[0]], out.points[t[1]], out.points[t[2]]);
        if (!(twice > 0)) return meshError("produced a triangle with non-positive area");
        sum += twice / 2.0;
    }
    if (std::abs(sum - expected) > 1e-9 * expected)
        return meshError("the triangles cover " + std::to_string(sum) + " mm^2 but the region has " +
                         std::to_string(expected) + " mm^2");
    return out;
}

} // namespace fos::formats::detail
