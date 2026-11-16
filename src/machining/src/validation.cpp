// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/machining/validation.hpp"

#include "fos/machining/face.hpp"
#include "fos/machining/material_region.hpp"
#include "fos/geometry/offset.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace fos::machining {
namespace {
using core::Severity;
using domain::Face;
using domain::Part;
using geometry::Vec2;

std::string num(double v) { return core::formatLength(v, 2); }
std::string at(double u, double v) { return "(" + num(u) + ", " + num(v) + ")"; }

struct Ctx {
    const Part& part;
    const ValidationOptions& opt;
    core::Diagnostics out;
    /// Material of a non-rectangular shaped part (ADR-0008); empty for plain panels.
    std::optional<MaterialRegion> region;

    void add(Severity s, const char* code, std::string msg) {
        out.push_back({s, code, std::move(msg), part.id.str()});
    }
    std::string where(Face f) const { return std::string("face ") + domain::toString(f) + " of '" + part.name + "'"; }
};

std::string label(const char* kind, const domain::Operation& op) {
    return std::string(kind) + " '" + op.id.str() + "'";
}

/// Footprints are shrunk by this much before the containment test, so that an operation
/// that merely touches the outline (a groove running out at a straight edge) is accepted.
constexpr double kFootprintShrink = 1e-4;

geometry::Vec2 xy(const geometry::Vec3& v) noexcept { return {v.x, v.y}; }

geometry::Contour ringContour(std::vector<geometry::Vec2> pts) {
    geometry::Contour c;
    for (const auto& p : pts) c.vertices.push_back({p, 0.0});
    return geometry::normalizedCcw(c);
}

/// XY shadow (part frame) of an operation entering edge face `f`: u in [u0, u1] along the
/// face, from the surface to `depth` into the material, shrunk by kFootprintShrink.
geometry::Contour edgeShadow(const Part& part, Face f, double u0, double u1, double depth) {
    const double s = kFootprintShrink;
    return ringContour({xy(faceToPart(part, f, u0 + s, 0, s)), xy(faceToPart(part, f, u1 - s, 0, s)),
                        xy(faceToPart(part, f, u1 - s, 0, depth - s)), xy(faceToPart(part, f, u0 + s, 0, depth - s))});
}

/// Reports `code` when the footprint does not lie inside the shaped material.
void checkFootprint(Ctx& c, const char* code, const std::string& what, const geometry::Contour& footprint) {
    if (footprint.size() < 3 || !(std::fabs(geometry::signedArea(footprint)) > 0)) return;
    const FootprintResult r = classifyFootprint(*c.region, footprint);
    if (r.placement == FootprintPlacement::Inside) return;
    c.add(Severity::Error, code, what + " " + describe(r) + "; it must lie completely inside the material of the part.");
}

/// Returns true when the hole has usable sizes (and may take part in collision checks).
bool checkHole(Ctx& c, const domain::Operation& op, const domain::Hole& h) {
    const double eps = core::kLengthEps;
    const std::string what = label("Hole", op) + " at " + at(h.u, h.v) + " on " + c.where(h.face);
    if (!(h.diameter > 0)) {
        c.add(Severity::Error, "machining.hole.invalid_size",
              what + " has a non-positive diameter of " + num(h.diameter) + " mm.");
        return false;
    }
    if (!h.through && !(h.depth > 0)) {
        c.add(Severity::Error, "machining.hole.invalid_size",
              what + " has a non-positive depth of " + num(h.depth) + " mm and is not marked as through.");
        return false;
    }
    const FaceExtents e = faceExtents(c.part, h.face);
    const double r = h.diameter / 2;
    const bool edge = isEdgeFace(h.face);
    const bool inside = e.u.contains(h.u) && e.v.contains(h.v);
    if (!inside) {
        c.add(Severity::Error, "machining.hole.out_of_bounds",
              what + " lies outside the face, which spans u 0.." + num(e.u.max) + " mm and v 0.." + num(e.v.max) +
                  " mm.");
    } else {
        if (edge) {
            if (h.v - r < -eps || h.v + r > e.v.max + eps) {
                c.add(Severity::Error, "machining.hole.edge_hole_outside_thickness",
                      what + " has diameter " + num(h.diameter) + " mm and does not fit inside the panel thickness of " +
                          num(e.v.max) + " mm (hole spans v " + num(h.v - r) + ".." + num(h.v + r) + " mm).");
            } else if (std::fabs(h.v - e.v.max / 2) > c.opt.edgeHoleCenterTolerance + eps) {
                c.add(Severity::Warning, "machining.hole.edge_hole_off_center",
                      what + " is " + num(std::fabs(h.v - e.v.max / 2)) + " mm off the centre of the " +
                          num(e.v.max) + " mm panel thickness (centre is at v = " + num(e.v.max / 2) + " mm).");
            }
        }
        double material = std::min(h.u - r, e.u.max - h.u - r);
        if (!edge) material = std::min({material, h.v - r, e.v.max - h.v - r});
        std::string edgeName = "the panel edge";
        bool skipEdgeRule = false;
        if (c.region && !edge) {
            // Shaped part: the centre must be in the material; the edge rule is measured to
            // the nearest outline / cut-out segment instead of the bounding rectangle.
            const geometry::Vec2 centre = xy(faceToPart(c.part, h.face, h.u, h.v, 0));
            const PointInMaterial loc = locateInMaterial(*c.region, centre);
            if (loc.location == MaterialLocation::OutsideOutline || loc.location == MaterialLocation::InCutout) {
                c.add(Severity::Error, "machining.hole.outside_material",
                      what + " lies " +
                          (loc.location == MaterialLocation::InCutout
                               ? "inside cut-out " + std::to_string(loc.cutout) + " of the shaped outline"
                               : std::string("outside the shaped outline")) +
                          "; it must lie in the material of the part.");
                skipEdgeRule = true;
            } else {
                const BoundaryDistance d = distanceToBoundary(*c.region, centre);
                material = d.distance - r;
                edgeName = d.onCutout ? "cut-out " + std::to_string(d.cutout) : "the shaped outline";
            }
        }
        if (c.region && edge) {
            const double s = kFootprintShrink;
            const double depth = effectiveDepth(c.part, h);
            if (depth > 2 * s && r > s)
                checkFootprint(c, "machining.hole.outside_material",
                               what + " (diameter " + num(h.diameter) + " mm, drilled " + num(depth) + " mm)",
                               edgeShadow(c.part, h.face, h.u - r, h.u + r, depth));
        }
        if (!skipEdgeRule && material < c.opt.minEdgeMaterial - eps) {
            if (material < -eps)
                c.add(Severity::Warning, "machining.hole.too_close_to_edge",
                      what + " with diameter " + num(h.diameter) + " mm breaks out of " + edgeName + " by " +
                          num(-material) + " mm.");
            else
                c.add(Severity::Warning, "machining.hole.too_close_to_edge",
                      what + " with diameter " + num(h.diameter) + " mm leaves only " + num(material) +
                          " mm of material to the panel edge (minimum " + num(c.opt.minEdgeMaterial) + " mm).");
        }
    }
    if (!h.through && h.depth > e.materialDepth + eps) {
        c.add(Severity::Error, "machining.hole.too_deep",
              what + " is " + num(h.depth) + " mm deep but the panel is only " + num(e.materialDepth) + " mm " +
                  (edge ? "deep behind that edge." : "thick."));
    }
    return true;
}

void checkGroove(Ctx& c, const domain::Operation& op, const domain::Groove& g) {
    const double eps = core::kLengthEps;
    const std::string what = label("Groove", op) + " from " + at(g.u1, g.v1) + " to " + at(g.u2, g.v2) + " on " +
                             c.where(g.face);
    const Vec2 a{g.u1, g.v1}, b{g.u2, g.v2};
    const double len = length(b - a);
    if (!(g.width > 0) || !(g.depth > 0) || !(len > eps)) {
        c.add(Severity::Error, "machining.groove.invalid_size",
              what + " must have positive width, depth and length (width " + num(g.width) + " mm, depth " +
                  num(g.depth) + " mm, length " + num(len) + " mm).");
        return;
    }
    const FaceExtents e = faceExtents(c.part, g.face);
    const Vec2 dir = (b - a) * (1.0 / len);
    const Vec2 n = Vec2{-dir.y, dir.x} * (g.width / 2);
    bool inside = true;
    for (const Vec2 p : {a + n, a - n, b + n, b - n}) inside = inside && e.u.contains(p.x) && e.v.contains(p.y);
    if (!inside)
        c.add(Severity::Error, "machining.groove.out_of_bounds",
              what + " with width " + num(g.width) + " mm extends beyond the face, which spans u 0.." +
                  num(e.u.max) + " mm and v 0.." + num(e.v.max) + " mm.");
    if (inside && c.region) {
        const double s = kFootprintShrink;
        if (!isEdgeFace(g.face)) {
            const Vec2 d = dir * s, m = n * ((g.width / 2 - s) / (g.width / 2));
            std::vector<Vec2> pts;
            for (const Vec2 p : {a + d + m, b - d + m, b - d - m, a + d - m})
                pts.push_back(xy(faceToPart(c.part, g.face, p.x, p.y, 0)));
            if (len > 2 * s && g.width > 2 * s)
                checkFootprint(c, "machining.groove.outside_material", what + " with width " + num(g.width) + " mm",
                               ringContour(pts));
        } else {
            double u0 = a.x, u1 = a.x;
            for (const Vec2 p : {a + n, a - n, b + n, b - n}) u0 = std::min(u0, p.x), u1 = std::max(u1, p.x);
            const double depth = std::min(g.depth, e.materialDepth);
            if (u1 - u0 > 2 * s && depth > 2 * s)
                checkFootprint(c, "machining.groove.outside_material", what + " (" + num(g.depth) + " mm deep)",
                               edgeShadow(c.part, g.face, u0, u1, depth));
        }
    }
    if (g.depth > e.materialDepth - eps)
        c.add(Severity::Error, "machining.groove.too_deep",
              what + " is " + num(g.depth) + " mm deep but only " + num(e.materialDepth) +
                  " mm of material lies behind that face; the groove would cut the panel apart.");
}

void checkPocket(Ctx& c, const domain::Operation& op, const domain::Pocket& p) {
    const double eps = core::kLengthEps;
    const std::string what = label("Pocket", op) + " on " + c.where(p.face);
    if (!p.through && !(p.depth > 0))
        c.add(Severity::Error, "machining.pocket.invalid_size",
              what + " has a non-positive depth of " + num(p.depth) + " mm and is not marked as through.");
    const FaceExtents e = faceExtents(c.part, p.face);
    if (!p.through && p.depth > e.materialDepth + eps)
        c.add(Severity::Error, "machining.pocket.too_deep",
              what + " is " + num(p.depth) + " mm deep but only " + num(e.materialDepth) +
                  " mm of material lies behind that face.");
    const std::string problem = contourProblem(p.contour);
    if (!problem.empty()) {
        c.add(Severity::Error, "machining.pocket.invalid_contour", what + " has an invalid contour: " + problem);
        return;
    }
    if (signedArea(p.contour) < 0)
        c.add(Severity::Warning, "machining.pocket.contour_clockwise",
              what + " has a clockwise contour (signed area " + num(signedArea(p.contour)) +
                  " mm2); pocket contours must be counter-clockwise.");
    bool inside = true;
    for (std::size_t i = 0; i < p.contour.size(); ++i) {
        const Vec2 q = p.contour[i];
        if (!e.u.contains(q.x) || !e.v.contains(q.y)) {
            c.add(Severity::Error, "machining.pocket.out_of_bounds",
                  what + " has contour point " + std::to_string(i + 1) + " at " + at(q.x, q.y) +
                      " outside the face, which spans u 0.." + num(e.u.max) + " mm and v 0.." + num(e.v.max) +
                      " mm.");
            inside = false;
            break;
        }
    }
    if (!inside || !c.region) return;
    if (!isEdgeFace(p.face)) {
        std::vector<Vec2> pts;
        for (const Vec2 q : p.contour) pts.push_back(xy(faceToPart(c.part, p.face, q.x, q.y, 0)));
        // Shrink by kFootprintShrink so a pocket touching the outline is accepted; a miter
        // offset of the (simple) polygon; when it is not representable, test unshrunk.
        geometry::Polygon2 poly;
        poly.vertices = pts;
        if (auto shrunk = geometry::offsetPolygon(poly, -kFootprintShrink)) pts = shrunk.value().vertices;
        checkFootprint(c, "machining.pocket.outside_material", what, ringContour(pts));
    } else {
        double u0 = p.contour.front().x, u1 = u0;
        for (const Vec2 q : p.contour) u0 = std::min(u0, q.x), u1 = std::max(u1, q.x);
        const double depth = p.through ? e.materialDepth : std::min(p.depth, e.materialDepth);
        if (u1 - u0 > 2 * kFootprintShrink && depth > 2 * kFootprintShrink)
            checkFootprint(c, "machining.pocket.outside_material", what, edgeShadow(c.part, p.face, u0, u1, depth));
    }
}

int orient(Vec2 a, Vec2 b, Vec2 p) noexcept {
    const double v = cross(b - a, p - a);
    // Scale-aware tolerance so nearly collinear points on long edges are still classified.
    const double tol = 1e-9 * (1.0 + length(b - a) * length(p - a));
    return v > tol ? 1 : (v < -tol ? -1 : 0);
}
bool onSegment(Vec2 a, Vec2 b, Vec2 p) noexcept {
    const double eps = core::kLengthEps;
    return p.x >= std::min(a.x, b.x) - eps && p.x <= std::max(a.x, b.x) + eps && p.y >= std::min(a.y, b.y) - eps &&
           p.y <= std::max(a.y, b.y) + eps;
}
/// Closed segments share at least one point.
bool segmentsTouch(Vec2 a, Vec2 b, Vec2 c, Vec2 d) noexcept {
    const int o1 = orient(a, b, c), o2 = orient(a, b, d), o3 = orient(c, d, a), o4 = orient(c, d, b);
    if (o1 != o2 && o3 != o4) return true;
    return (o1 == 0 && onSegment(a, b, c)) || (o2 == 0 && onSegment(a, b, d)) || (o3 == 0 && onSegment(c, d, a)) ||
           (o4 == 0 && onSegment(c, d, b));
}
} // namespace

double signedArea(const std::vector<Vec2>& c) noexcept {
    double a = 0;
    for (std::size_t i = 0; i < c.size(); ++i) a += cross(c[i], c[(i + 1) % c.size()]);
    return a / 2;
}

std::string contourProblem(const std::vector<Vec2>& c) {
    const std::size_t n = c.size();
    if (n < 3) return "it has " + std::to_string(n) + " point(s) but a closed contour needs at least 3.";
    for (std::size_t i = 0; i < n; ++i)
        if (approxEqual(c[i], c[(i + 1) % n]))
            return "points " + std::to_string(i + 1) + " and " + std::to_string((i + 1) % n + 1) + " coincide at " +
                   at(c[i].x, c[i].y) + ".";
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2 a = c[i], b = c[(i + 1) % n];
        // Adjacent edge: only a fold-back (collinear, reversing direction) is invalid.
        const Vec2 nx = c[(i + 2) % n];
        if (orient(a, b, nx) == 0 && dot(b - a, nx - b) < 0)
            return "the edge after point " + std::to_string((i + 1) % n + 1) + " folds back onto the previous edge.";
        for (std::size_t j = i + 2; j < n; ++j) {
            if (i == 0 && j == n - 1) continue; // adjacent through the closing edge
            if (segmentsTouch(a, b, c[j], c[(j + 1) % n]))
                return "edge " + std::to_string(i + 1) + " (starting at " + at(a.x, a.y) + ") intersects edge " +
                       std::to_string(j + 1) + " (starting at " + at(c[j].x, c[j].y) + ").";
        }
    }
    if (std::fabs(signedArea(c)) <= core::kLengthEps) return "it encloses no area.";
    return {};
}

core::Diagnostics validateOperations(const Part& part, const ValidationOptions& options) {
    Ctx c{part, options, {}, shapedMaterialRegion(part)};

    std::map<std::string, int> idCount;
    for (const auto& op : part.operations) ++idCount[op.id.str()];
    for (const auto& [id, count] : idCount) // std::map: deterministic order
        if (count > 1)
            c.add(Severity::Error, "machining.operation.duplicate_id",
                  "Operation id '" + id + "' is used by " + std::to_string(count) + " operations of '" + part.name +
                      "'; operation ids must be unique within a part.");

    std::vector<std::size_t> holes; // indices of holes with usable sizes
    for (std::size_t i = 0; i < part.operations.size(); ++i) {
        const auto& op = part.operations[i];
        if (const auto* h = std::get_if<domain::Hole>(&op.shape)) {
            if (checkHole(c, op, *h)) holes.push_back(i);
        } else if (const auto* g = std::get_if<domain::Groove>(&op.shape)) {
            checkGroove(c, op, *g);
        } else if (const auto* p = std::get_if<domain::Pocket>(&op.shape)) {
            checkPocket(c, op, *p);
        }
    }

    if (options.checkCollisions) {
        for (std::size_t x = 0; x < holes.size(); ++x) {
            for (std::size_t y = x + 1; y < holes.size(); ++y) {
                const auto& oa = part.operations[holes[x]];
                const auto& ob = part.operations[holes[y]];
                if (options.ignoreCollisionsWithinSameSource && oa.source == ob.source && oa.source != "user") continue;
                const auto& ha = std::get<domain::Hole>(oa.shape);
                const auto& hb = std::get<domain::Hole>(ob.shape);
                if (!holesCollide(part, ha, hb)) continue;
                auto describe = [&](const domain::Operation& op, const domain::Hole& h) {
                    return "'" + op.id.str() + "' (face " + domain::toString(h.face) + " at " + at(h.u, h.v) +
                           ", diameter " + num(h.diameter) + " mm, " +
                           (h.through ? std::string("through") : "depth " + num(h.depth) + " mm") + ")";
                };
                c.add(Severity::Error, "machining.hole.collision",
                      "Holes " + describe(oa, ha) + " and " + describe(ob, hb) + " of '" + part.name +
                          "' intersect inside the panel (" + num(part.length) + " x " + num(part.width) + " x " +
                          num(part.thickness) + " mm).");
            }
        }
    }
    return std::move(c.out);
}

} // namespace fos::machining
